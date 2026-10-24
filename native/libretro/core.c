/*
 * hakuX native core, libretro front end (hakux_libretro.so): RetroArch runs
 * the game (LIBRETRO_SPEC.md).
 *
 * Frames are phase-locked, not stepped: the emulation keeps its own threads
 * (the CPU, the renderer, the APU), and each retro_run is the display's vsync
 * for the NV2A's vblank clock (nc_dev_nv2a_display_vsync), which locks to it
 * as it does to Android's Choreographer. retro_run hands over the newest
 * frame and one period's samples; it never waits for the guest.
 *
 * When RetroArch stops calling (its menu, a stall), the emulation pauses
 * after three periods (nc_pause_set: the CPU parks at its next kernel entry,
 * the devices hold off, the guest's time leaves the pause out), and the next
 * retro_run resumes it.
 *
 * Bring-up (M1): the core cannot stop yet, so unloading ends RetroArch's
 * process; it is not packaged until it can (M2).
 */
#include "native/libretro/libretro_int.h"
#include "native/core/core.h"
#include "native/core/irq.h"
#include "native/dev/dev.h"
#include "native/hle/hle_native.h"
#include "native/kernel/kernel.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef HAKUX_VERSION
#define HAKUX_VERSION "dev"
#endif

#define GAP_NS (3 * NC_LR_FRAME_NS)   /* no retro_run this long: paused */

static retro_environment_t g_env;
static retro_video_refresh_t g_video;
static retro_audio_sample_batch_t g_audio_batch;
static retro_input_poll_t g_input_poll;
static retro_input_state_t g_input_state;

static NcCore g_core;
static int g_disc_fd = -1;
static int g_core_state;           /* 0 none, 1 booting, 2 running, -1 stopped */
static bool g_shutdown_sent;

static struct {
    pthread_mutex_t mu;
    uint64_t last_run_ns;          /* 0: no retro_run yet */
    bool paused;                   /* by a gap, not by the guest */
} g_gap = {.mu = PTHREAD_MUTEX_INITIALIZER};

static struct sigaction g_old_segv, g_old_bus;

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* Guest RAM tracking, MMIO and the JIT's call-ret stack are faults the core
 * resolves; any other is passed on to the handler before ours (RetroArch's,
 * another core's), or to the default action as the fault repeats. */
static void on_fault(int sig, siginfo_t *info, void *ctx)
{
    if (__atomic_load_n(&g_core_state, __ATOMIC_ACQUIRE) != 0 && nc_core_fault(&g_core, sig, info, ctx)) {
        return;
    }
    const struct sigaction *old = sig == SIGSEGV ? &g_old_segv : &g_old_bus;
    if ((old->sa_flags & SA_SIGINFO) && old->sa_sigaction) {
        old->sa_sigaction(sig, info, ctx);
        return;
    }
    if (!(old->sa_flags & SA_SIGINFO) && old->sa_handler != SIG_DFL && old->sa_handler != SIG_IGN) {
        old->sa_handler(sig);
        return;
    }
    struct sigaction dfl = {.sa_handler = SIG_DFL};
    sigaction(sig, &dfl, NULL);
}

static void *core_thread(void *arg)
{
    if (!nc_core_boot(&g_core, g_disc_fd)) {
        nc_log("hakuX-libretro", "core: boot failed");
        __atomic_store_n(&g_core_state, -1, __ATOMIC_RELEASE);
        return NULL;
    }
    __atomic_store_n(&g_core_state, 2, __ATOMIC_RELEASE);
    nc_core_run(&g_core);
    nc_log("hakuX-libretro", "core: stopped%s", g_core.kernel.quit ? " (the title left)" : "");
    __atomic_store_n(&g_core_state, -1, __ATOMIC_RELEASE);
    return NULL;
}

/* RetroArch stopped calling: pause the emulation until it calls again. */
static void *gap_thread(void *arg)
{
    for (;;) {
        const struct timespec ts = {0, (long)NC_LR_FRAME_NS};
        nanosleep(&ts, NULL);
        pthread_mutex_lock(&g_gap.mu);
        if (g_gap.last_run_ns && !g_gap.paused && now_ns() - g_gap.last_run_ns > GAP_NS &&
            __atomic_load_n(&g_core_state, __ATOMIC_ACQUIRE) == 2) {
            g_gap.paused = true;
            nc_pause_set(true);
        }
        pthread_mutex_unlock(&g_gap.mu);
    }
    return NULL;
}

static void gap_tick(uint64_t now)
{
    pthread_mutex_lock(&g_gap.mu);
    g_gap.last_run_ns = now;
    if (g_gap.paused) {
        g_gap.paused = false;
        nc_pause_set(false);
    }
    pthread_mutex_unlock(&g_gap.mu);
}

static bool make_dir(const char *path)
{
    return mkdir(path, 0700) == 0 || errno == EEXIST;
}

/* <save directory>/hakux: the console's hard disk (hdd/, its partitions) and
 * the renderer's caches (cache/). */
static bool data_dirs(char *hdd, char *cache, size_t n)
{
    const char *base = NULL;
    if (!g_env(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &base) || !base || !*base) {
        nc_log("hakuX-libretro", "no save directory from the frontend");
        return false;
    }
    char root[PATH_MAX];
    snprintf(root, sizeof(root), "%s/hakux", base);
    snprintf(hdd, n, "%s/hdd", root);
    snprintf(cache, n, "%s/cache", root);
    bool ok = make_dir(root) && make_dir(hdd) && make_dir(cache);
    for (int i = 0; ok && i <= 5; i++) {
        char p[PATH_MAX + 32];
        snprintf(p, sizeof(p), "%s/Partition%d", hdd, i);
        ok = make_dir(p);
    }
    if (!ok) {
        nc_log("hakuX-libretro", "data directories under %s: %s", root, strerror(errno));
    }
    return ok;
}

RETRO_API unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

RETRO_API void retro_set_environment(retro_environment_t cb)
{
    g_env = cb;
    bool no_game = false;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb)
{
    g_video = cb;
}

RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb)
{
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
    g_audio_batch = cb;
}

RETRO_API void retro_set_input_poll(retro_input_poll_t cb)
{
    g_input_poll = cb;
}

RETRO_API void retro_set_input_state(retro_input_state_t cb)
{
    g_input_state = cb;
}

RETRO_API void retro_init(void)
{
}

RETRO_API void retro_deinit(void)
{
}

RETRO_API void retro_get_system_info(struct retro_system_info *info)
{
    memset(info, 0, sizeof(*info));
    info->library_name = "hakuX";
    info->library_version = HAKUX_VERSION;
    info->valid_extensions = "iso";
    info->need_fullpath = true;
    info->block_extract = true;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
    memset(info, 0, sizeof(*info));
    nc_lr_video_geometry(&info->geometry);
    info->timing.fps = 1e9 / (double)NC_LR_FRAME_NS;
    info->timing.sample_rate = 48000.0;
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
{
}

RETRO_API void retro_reset(void)
{
    nc_log("hakuX-libretro", "reset: not yet (the core cannot stop and boot again before M2)");
}

RETRO_API bool retro_load_game(const struct retro_game_info *game)
{
    if (!game || !game->path) {
        return false;
    }
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    if (!g_env(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
        nc_log("hakuX-libretro", "the frontend has no XRGB8888");
        return false;
    }
    char hdd[PATH_MAX], cache[PATH_MAX];
    if (!data_dirs(hdd, cache, sizeof(hdd))) {
        return false;
    }
    g_disc_fd = open(game->path, O_RDONLY | O_CLOEXEC);
    if (g_disc_fd < 0) {
        nc_log("hakuX-libretro", "%s: %s", game->path, strerror(errno));
        return false;
    }
    nc_lr_input_describe(g_env);
    nc_io_set_hdd_root(hdd);
    setenv("HAKUX_HLE_MODE", "replace", 0);
    setenv("HAKUX_NATIVE_BASE", cache, 1);
    nc_kernel_set_video(-1, NC_AV_PACK_HDTV);
    nc_hle_set_pipeline_options(true, true);   /* the app's defaults */

    struct sigaction sa = {.sa_sigaction = on_fault, .sa_flags = SA_SIGINFO | SA_ONSTACK};
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &g_old_segv);
    sigaction(SIGBUS, &sa, &g_old_bus);

    __atomic_store_n(&g_core_state, 1, __ATOMIC_RELEASE);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16u << 20);
    pthread_t t;
    const int err = pthread_create(&t, &attr, core_thread, NULL);
    pthread_attr_destroy(&attr);
    if (err) {
        nc_log("hakuX-libretro", "core thread: %s", strerror(err));
        return false;
    }
    pthread_detach(t);
    if (pthread_create(&t, NULL, gap_thread, NULL) == 0) {
        pthread_detach(t);
    }
    nc_log("hakuX-libretro", "loaded %s; data in %s", game->path, hdd);
    return true;
}

RETRO_API bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
    return false;
}

RETRO_API void retro_unload_game(void)
{
    if (__atomic_load_n(&g_core_state, __ATOMIC_ACQUIRE) == 0) {
        return;
    }
    /* M1: the core thread has no stop; RetroArch's process ends with it. */
    nc_log("hakuX-libretro", "unload: the core cannot stop yet; ending the process");
    fflush(NULL);
    _exit(0);
}

RETRO_API void retro_run(void)
{
    const uint64_t now = now_ns();
    nc_lr_input_run(g_input_poll, g_input_state);
    nc_dev_nv2a_display_vsync(now);         /* RetroArch's frame tick is the display's vsync */
    gap_tick(now);
    nc_lr_audio_run(g_audio_batch);
    nc_lr_video_run(g_env, g_video, now);

    const char *why = __atomic_load_n(&g_core_state, __ATOMIC_ACQUIRE) < 0 ? "the core stopped" : nc_hle_fatal();
    if (why && !g_shutdown_sent) {
        g_shutdown_sent = true;
        nc_log("hakuX-libretro", "shutting down: %s", why);
        g_env(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
    }
}

RETRO_API unsigned retro_get_region(void)
{
    return RETRO_REGION_NTSC;
}

RETRO_API size_t retro_serialize_size(void)
{
    return 0;
}

RETRO_API bool retro_serialize(void *data, size_t size)
{
    return false;
}

RETRO_API bool retro_unserialize(const void *data, size_t size)
{
    return false;
}

RETRO_API void retro_cheat_reset(void)
{
}

RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
}

RETRO_API void *retro_get_memory_data(unsigned id)
{
    return NULL;
}

RETRO_API size_t retro_get_memory_size(unsigned id)
{
    return 0;
}
