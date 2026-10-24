/*
 * hakuX native core, Linux front end (hakux): the game in a fullscreen SDL3
 * window (on PortareOS the panel itself, through KMSDRM), its frames
 * presented by the HLE GPU's Vulkan swapchain (present.c), the gamepads as
 * player 1's Duke, and the APU's output through SDL audio (out_sdl.c:
 * PipeWire on PortareOS).
 *
 * Every gamepad SDL opens is player 1's, merged (gamepad.c): on PortareOS
 * InputPlumber grabs the Nova's own controls and presents them as a virtual
 * DualSense Edge, so the raw device SDL also finds is silent; a pad plugged
 * in plays too. The title's rumble goes to all of them.
 *
 *   hakux <disc image> <data directory> [option...]
 *
 * The data directory holds the emulated hard disk (hdd/) and the HLE GPU's
 * caches (cache/). Options, as the Android app's: scale=<n>,
 * aspect=auto|4:3|16:9|stretch, fxaa=0|1, upscale=bilinear|sgsr,
 * video=<flags, hex>, lang=<n>; and exitafter=<seconds>, which leaves as
 * the quit keys would after that long (tests). NC_CAPTURE_EVERY captures
 * frames as xboxrun does (run/capture.c), into frames/ here.
 *
 * Leaving (SIGINT/SIGTERM, Escape, Home + Start as every PortareOS emulator
 * quits, or the title stopping) drops the swapchain and SDL, then ends the process: the core
 * thread has no stop. Its exit hands the panel back (the DRM master is
 * released with the process's descriptors), as a SIGKILL from PortareOS's
 * quit hotkey does too.
 */
#include "native/app/app.h"
#include "native/core/core.h"
#include "native/hle/hle_native.h"
#include "native/kernel/kernel.h"
#include "hw/xbox/hle/vk/vkdev.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static NcCore g_core;
static int g_disc_fd = -1;
static SDL_AtomicInt g_core_state; /* 0 booting, 1 running, -1 stopped */

void nc_capture_start(void);       /* run/capture.c */

#define MAX_PADS 8
static SDL_Gamepad *g_pads[MAX_PADS];
static unsigned g_rumbles;         /* the title's rumble changes (logged as it leaves) */

static void pad_added(SDL_JoystickID id)
{
    for (unsigned i = 0; i < MAX_PADS; i++) {
        if (!g_pads[i]) {
            SDL_Gamepad *p = g_pads[i] = SDL_OpenGamepad(id);
            if (!p) {
                nc_log("hakuX-linux", "gamepad: %s", SDL_GetError());
                return;
            }
            const bool rumble =
                SDL_GetBooleanProperty(SDL_GetGamepadProperties(p), SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
            nc_log("hakuX-linux", "gamepad %u: %s (%04x:%04x)%s, player 1", i, SDL_GetGamepadName(p),
                   SDL_GetGamepadVendor(p), SDL_GetGamepadProduct(p), rumble ? ", rumble" : "");
            return;
        }
    }
}

static void pad_removed(SDL_JoystickID id)
{
    for (unsigned i = 0; i < MAX_PADS; i++) {
        if (g_pads[i] && SDL_GetGamepadID(g_pads[i]) == id) {
            nc_log("hakuX-linux", "gamepad %u: removed", i);
            SDL_CloseGamepad(g_pads[i]);
            g_pads[i] = NULL;
        }
    }
}

/* The title's rumble on every pad: sent as it changes, and again each half
 * second while it lasts (each send is good for a second, so a rumble left on
 * by a stopped title ends). */
static void rumble(Uint64 now)
{
    static uint16_t last_l, last_r;
    static Uint64 sent;
    uint16_t l = 0, r = 0;
    nc_pad_rumble(0, &l, &r);
    const bool changed = l != last_l || r != last_r;
    if (!changed && (!(l || r) || now - sent < 500)) {
        return;
    }
    for (unsigned i = 0; i < MAX_PADS; i++) {
        if (g_pads[i]) {
            SDL_RumbleGamepad(g_pads[i], l, r, l || r ? 1000 : 0);
        }
    }
    g_rumbles += changed;
    last_l = l;
    last_r = r;
    sent = now;
}

/* Guest RAM tracking and MMIO are host faults the core resolves; any other
 * is a crash: the default action, as the fault repeats. */
static void on_fault(int sig, siginfo_t *info, void *ctx)
{
    if (nc_core_fault(&g_core, sig, info, ctx)) {
        return;
    }
    static const char msg[] = "hakuX-linux: host fault outside the guest\n";
    write(2, msg, sizeof(msg) - 1);
    struct sigaction dfl = {.sa_handler = SIG_DFL};
    sigaction(sig, &dfl, NULL);
}

static void *core_thread(void *arg)
{
    if (!nc_core_boot(&g_core, g_disc_fd)) {
        nc_log("hakuX-linux", "core: boot failed");
        SDL_SetAtomicInt(&g_core_state, -1);
        return NULL;
    }
    SDL_SetAtomicInt(&g_core_state, 1);
    nc_core_run(&g_core);
    nc_log("hakuX-linux", "core: stopped%s", g_core.kernel.quit ? " (the title left)" : "");
    SDL_SetAtomicInt(&g_core_state, -1);
    return NULL;
}

static bool make_dir(const char *path)
{
    return mkdir(path, 0700) == 0 || errno == EEXIST;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: hakux <disc image> <data directory> [option...]\n");
        return 2;
    }
    char data[PATH_MAX], hdd[PATH_MAX + 16], cache[PATH_MAX + 16];
    if (!make_dir(argv[2]) || !realpath(argv[2], data)) {
        fprintf(stderr, "invalid data directory: %s\n", argv[2]);
        return 2;
    }
    snprintf(hdd, sizeof(hdd), "%s/hdd", data);
    snprintf(cache, sizeof(cache), "%s/cache", data);
    bool ok = make_dir(hdd) && make_dir(cache);
    for (int i = 0; ok && i <= 5; i++) {
        char p[PATH_MAX + 32];
        snprintf(p, sizeof(p), "%s/Partition%d", hdd, i);
        ok = make_dir(p);
    }
    if (!ok) {
        perror("data directories");
        return 2;
    }
    g_disc_fd = open(argv[1], O_RDONLY | O_CLOEXEC);
    if (g_disc_fd < 0) {
        perror(argv[1]);
        return 2;
    }
    nc_io_set_hdd_root(hdd);
    setenv("HAKUX_HLE_MODE", "replace", 0);
    setenv("HAKUX_NATIVE_BASE", cache, 1);

    int64_t video = -1;
    unsigned exit_after = 0;
    for (int i = 3; i < argc; i++) {
        const char *a = argv[i];
        if (!strncmp(a, "scale=", 6)) {
            nc_hle_set_render_scale(atoi(a + 6));
        } else if (!strncmp(a, "aspect=", 7)) {
            nc_present_set_aspect(!strcmp(a + 7, "4:3")       ? NC_ASPECT_4_3
                                  : !strcmp(a + 7, "16:9")    ? NC_ASPECT_16_9
                                  : !strcmp(a + 7, "stretch") ? NC_ASPECT_STRETCH
                                                              : NC_ASPECT_AUTO);
        } else if (!strncmp(a, "fxaa=", 5)) {
            nc_present_set_fxaa(atoi(a + 5) != 0);
        } else if (!strncmp(a, "upscale=", 8)) {
            nc_present_set_upscale(!strcmp(a + 8, "sgsr") ? NC_UPSCALE_SGSR : NC_UPSCALE_BILINEAR);
        } else if (!strncmp(a, "video=", 6)) {
            video = (int64_t)strtoul(a + 6, NULL, 16);
        } else if (!strncmp(a, "lang=", 5)) {
            nc_kernel_set_language(atoi(a + 5));
        } else if (!strncmp(a, "exitafter=", 10)) {
            exit_after = (unsigned)atoi(a + 10);
        } else {
            fprintf(stderr, "unknown option: %s\n", a);
            return 2;
        }
    }
    nc_kernel_set_video(video, NC_AV_PACK_HDTV);
    nc_present_set_widescreen(video >= 0 && (video & NC_VIDEO_WIDESCREEN));
    nc_hle_set_pipeline_options(true, true);   /* the app's defaults */

    SDL_SetAppMetadata("hakuX", NULL, "ch.portare.hakux");   /* the audio stream's name */
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        nc_log("hakuX-linux", "SDL_Init: %s", SDL_GetError());
        return 1;
    }
    /* here on the main thread, before the core opens the output; without it
     * the game runs on, its sound drained unheard */
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        nc_log("hakuX-linux", "SDL audio: %s", SDL_GetError());
    }
    const SDL_DisplayMode *dm = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    const int ww = dm ? dm->w : 1280, wh = dm ? dm->h : 960;
    /* SDL_WINDOW_VULKAN: SDL loads the Vulkan loader, the HLE GPU's too, and
     * on KMSDRM gives the panel up to the driver's display surface */
    SDL_Window *win = SDL_CreateWindow("hakuX", ww, wh, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_VULKAN);
    if (!win) {
        nc_log("hakuX-linux", "window: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    nc_hle_set_vk_loader((void *)SDL_Vulkan_GetVkGetInstanceProcAddr());
    Uint32 next = 0;
    const char *const *exts = SDL_Vulkan_GetInstanceExtensions(&next);
    hle_vk_set_window_extensions(exts, next);
    nc_log("hakuX-linux", "video %s: %dx%d at %.0f Hz, %u Vulkan instance extension(s) for the window",
           SDL_GetCurrentVideoDriver(), ww, wh, dm ? dm->refresh_rate : 0.0f, (unsigned)next);
    for (Uint32 i = 0; i < next; i++) {
        nc_log("hakuX-linux", "  %s", exts[i]);
    }
    if (!nc_present_init()) {
        SDL_Quit();
        return 1;
    }

    struct sigaction sa = {.sa_sigaction = on_fault, .sa_flags = SA_SIGINFO | SA_ONSTACK};
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16u << 20);
    pthread_t core;
    pthread_create(&core, &attr, core_thread, NULL);
    pthread_attr_destroy(&attr);
    nc_capture_start();

    const Uint64 start = SDL_GetTicks();
    bool shown = false;
    const char *why = NULL;
    while (!why) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_EVENT_QUIT:
                why = "quit requested";
                break;
            case SDL_EVENT_KEY_DOWN:
                if (ev.key.key == SDLK_ESCAPE) {
                    why = "Escape";
                }
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                pad_added(ev.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                pad_removed(ev.gdevice.which);
                break;
            default:
                break;
            }
        }
        NcPadState s = {0};
        bool home = false;              /* the guide button: the Duke has none */
        for (unsigned i = 0; i < MAX_PADS; i++) {
            if (g_pads[i]) {
                nc_read_gamepad(g_pads[i], &s);
                home |= SDL_GetGamepadButton(g_pads[i], SDL_GAMEPAD_BUTTON_GUIDE);
            }
        }
        if (home && (s.buttons & NC_PAD_START)) {
            why = "Home + Start";       /* PortareOS's quit: the title doesn't see the Start */
            s.buttons &= (uint16_t)~NC_PAD_START;
        }
        nc_pad_set(0, &s);
        rumble(SDL_GetTicks());

        if (!nc_present_frame(win)) {
            SDL_Delay(8);               /* nothing to wait for the panel on */
        }
        if (!shown && nc_present_shown()) {
            shown = true;
            nc_log("hakuX-linux", "first title frame on screen after %.1f s",
                   (double)(SDL_GetTicks() - start) / 1000.0);
        }
        if (SDL_GetAtomicInt(&g_core_state) < 0) {
            why = "the core stopped";
        } else if (nc_hle_fatal()) {
            why = nc_hle_fatal();
        } else if (exit_after && SDL_GetTicks() - start >= (Uint64)exit_after * 1000) {
            why = "exitafter";
        }
    }
    float fps, avg_ms, last_ms;
    nc_present_stats(&fps, &avg_ms, &last_ms);
    nc_log("hakuX-linux", "leaving (%s) after %.1f s: %s, %.1f fps presented, %u rumble changes", why,
           (double)(SDL_GetTicks() - start) / 1000.0, shown ? "frames were shown" : "no frame shown", fps, g_rumbles);
    uint32_t pw, ph;
    hle_present_begin(NULL, &pw, &ph);  /* no window: the swapchain and surface go */
    SDL_DestroyWindow(win);
    SDL_Quit();
    _exit(shown ? 0 : 1);
}
