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
 * The core cannot stop yet (M2). In PortareOS's build (HAKUX_LR_END_PROCESS)
 * unloading, or RetroArch letting its Vulkan context go, ends RetroArch's
 * process (nc_lr_end), and a title's relaunch starts RetroArch again in its
 * place (relaunch). Otherwise the emulation parks for good and RetroArch runs
 * on (park); the core then refuses another game, as it can't boot a second.
 */
#include "native/libretro/libretro_int.h"
#include "native/core/core.h"
#include "native/core/irq.h"
#include "native/dev/dev.h"
#include "native/hle/hle_native.h"
#include "native/kernel/kernel.h"

#include <dirent.h>
#include <dlfcn.h>
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
#ifndef HAKUX_LR_END_PROCESS
#define HAKUX_LR_END_PROCESS 0
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
static bool g_started;
static const char *g_fail;         /* nc_lr_fail's: the content stops */
static bool g_shutdown_sent;
static char g_game[PATH_MAX];      /* the content's real path */
static bool g_parked;              /* park: the emulation paused for good */

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

bool nc_lr_env(unsigned cmd, void *data)
{
    return g_env && g_env(cmd, data);
}

void nc_lr_fail(const char *why)
{
    nc_log("hakuX-libretro", "failed: %s", why);
    __atomic_store_n(&g_fail, why, __ATOMIC_RELEASE);
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
    if (g_gap.paused && !g_parked) {
        g_gap.paused = false;
        nc_pause_set(false);
    }
    pthread_mutex_unlock(&g_gap.mu);
}

static bool make_dir(const char *path)
{
    return mkdir(path, 0700) == 0 || errno == EEXIST;
}

/* The save directory (PortareOS: /storage/saves/xbox): the console's hard
 * disk (hdd/, its partitions) and the renderer's caches (cache/). */
static bool data_dirs(char *hdd, char *cache, size_t n)
{
    const char *root = NULL;
    if (!g_env(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &root) || !root || !*root) {
        nc_log("hakuX-libretro", "no save directory from the frontend");
        return false;
    }
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

/* PortareOS's build ends RetroArch's process when the game closes
 * (HAKUX_LR_END_PROCESS); HAKUX_LR_END_PROCESS=0 or 1 in the environment
 * chooses for a run (the tests). */
static bool end_process(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("HAKUX_LR_END_PROCESS");
        on = e ? atoi(e) != 0 : HAKUX_LR_END_PROCESS;
    }
    return on;
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
    nc_lr_options_declare(cb);
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
    /* Parked threads outlive retro_deinit: RetroArch's dlclose must not unmap
     * their code. */
    Dl_info self;
    if (dladdr((void *)retro_init, &self) && self.dli_fname) {
        dlopen(self.dli_fname, RTLD_NOW | RTLD_NOLOAD | RTLD_NODELETE);
    }
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
    info->timing.fps = (double)NC_LR_FPS_NUM / (double)NC_LR_FPS_DEN;   /* the Xbox's, exactly */
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
    if (g_started) {                /* a game played in this process (parked) */
        static const struct retro_message msg = {"hakuX plays one game a session: restart RetroArch for another",
                                                 300};
        nc_log("hakuX-libretro", "load: %s", msg.msg);
        g_env(RETRO_ENVIRONMENT_SET_MESSAGE, (void *)&msg);
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
    if (!realpath(game->path, g_game)) {
        snprintf(g_game, sizeof(g_game), "%s", game->path);
    }
    /* RetroArch started again for this game's relaunch: boot the XBE it named. */
    if (end_process()) {
        const char *again = getenv("HAKUX_RELAUNCH");
        nc_core_set_relaunch(again && !strcmp(again, g_game));
        unsetenv("HAKUX_RELAUNCH");
    }
    nc_lr_input_describe(g_env);
    nc_io_set_hdd_root(hdd);
    nc_hle_set_mode("replace");
    nc_hle_set_base_path(cache);
    nc_lr_options_apply();                          /* the render scale, the console's settings */
    nc_dev_nv2a_display_period(NC_LR_FRAME_NS);    /* retro_run's: the vblank locks one to one */
    nc_hle_set_pipeline_options(true, true);   /* the app's defaults */

    struct sigaction sa = {.sa_sigaction = on_fault, .sa_flags = SA_SIGINFO | SA_ONSTACK};
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &g_old_segv);
    sigaction(SIGBUS, &sa, &g_old_bus);

    /* With Vulkan the guest boots in context_reset, on RetroArch's device. */
    const bool vulkan = nc_lr_vk_negotiate();
    if (__atomic_load_n(&g_fail, __ATOMIC_ACQUIRE)) {
        return false;
    }
    nc_log("hakuX-libretro", "loaded %s; data in %s", game->path, hdd);
    return vulkan || nc_lr_start();
}

bool nc_lr_start(void)
{
    if (g_started) {
        return true;
    }
    g_started = true;
    __atomic_store_n(&g_core_state, 1, __ATOMIC_RELEASE);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16u << 20);
    pthread_t t;
    const int err = pthread_create(&t, &attr, core_thread, NULL);
    pthread_attr_destroy(&attr);
    if (err) {
        nc_log("hakuX-libretro", "core thread: %s", strerror(err));
        __atomic_store_n(&g_core_state, -1, __ATOMIC_RELEASE);
        return false;
    }
    pthread_detach(t);
    if (pthread_create(&t, NULL, gap_thread, NULL) == 0) {
        pthread_detach(t);
    }
    return true;
}

RETRO_API bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
    return false;
}

/* Every descriptor but stdin, stdout and stderr closes at the exec, as at an
 * exit: the display, the input devices, the audio server. */
static void close_on_exec(void)
{
    DIR *d = opendir("/proc/self/fd");
    if (!d) {
        return;
    }
    for (const struct dirent *e; (e = readdir(d));) {
        const int fd = atoi(e->d_name);
        const int flags = fd > 2 && fd != dirfd(d) ? fcntl(fd, F_GETFD) : -1;
        if (flags >= 0) {
            fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
        }
    }
    closedir(d);
}

/* A title's XLaunchNewImage (kernel.relaunch; the launch data page is in
 * relaunch.bin, beside the console disk): the core cannot boot again in this
 * process, so RetroArch starts again in its place, on the same command line,
 * with HAKUX_RELAUNCH naming the game. Its load boots the XBE the page names,
 * as Android's activity does with "relaunch". The process stays the same one
 * to its parent (runemu.sh), so the game does not go back to the launcher.
 * Returns if RetroArch can't start again for the game. */
static void relaunch(void)
{
    static char cmd[64 * 1024];
    static char *argv[1024];
    static char mark[PATH_MAX + 32];
    extern char **environ;
    const int fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
    size_t n = 0;
    if (fd >= 0) {
        ssize_t r;
        while (n < sizeof(cmd) - 1 && (r = read(fd, cmd + n, sizeof(cmd) - 1 - n)) > 0) {
            n += (size_t)r;
        }
        close(fd);
    }
    unsigned argc = 0;
    bool on_game = false;
    for (size_t i = 0; i < n && argc < sizeof(argv) / sizeof(*argv) - 1; i += strlen(cmd + i) + 1) {
        char real[PATH_MAX];
        argv[argc++] = cmd + i;
        on_game = on_game || (realpath(cmd + i, real) && !strcmp(real, g_game));
    }
    argv[argc] = NULL;
    if (!on_game) {
        nc_log("hakuX-libretro", "relaunch: RetroArch was not started on the game; it can't start again for it");
        return;
    }
    size_t ne = 0;
    while (environ && environ[ne]) {
        ne++;
    }
    char **envp = calloc(ne + 2, sizeof(*envp));
    if (!envp) {
        return;
    }
    size_t m = 0;
    for (size_t i = 0; i < ne; i++) {
        if (strncmp(environ[i], "HAKUX_RELAUNCH=", 15)) {
            envp[m++] = environ[i];
        }
    }
    snprintf(mark, sizeof(mark), "HAKUX_RELAUNCH=%s", g_game);
    envp[m] = mark;
    nc_log("hakuX-libretro", "relaunch: starting RetroArch again for %s", g_game);
    fflush(NULL);
    close_on_exec();
    sigset_t none;
    sigemptyset(&none);
    pthread_sigmask(SIG_SETMASK, &none, NULL);
    execve("/proc/self/exe", argv, envp);
    nc_log("hakuX-libretro", "relaunch: RetroArch can't start again: %s", strerror(errno));
    free(envp);
}

/* The emulation paused for good, as the core can't stop (M2), and the
 * renderer kept off RetroArch's device, which RetroArch may destroy now
 * (nc_lr_vk_park). Its threads stay, parked; RetroArch runs on. */
static void park(const char *why)
{
    if (g_parked) {
        return;
    }
    pthread_mutex_lock(&g_gap.mu);
    g_parked = true;                /* retro_run doesn't resume it (gap_tick) */
    g_gap.paused = true;
    nc_pause_set(true);
    pthread_mutex_unlock(&g_gap.mu);
    const char *who = NULL;
    const bool still = nc_pause_quiesce(500000000ull, &who);
    nc_lr_vk_park();
    nc_log("hakuX-libretro", "%s: the core cannot stop yet; the emulation is parked for good%s%s", why,
           still ? "" : ", but not the ", still ? "" : who ? who : "?");
}

bool nc_lr_parked(void)
{
    return g_parked;
}

void nc_lr_end(const char *why)
{
    const int state = __atomic_load_n(&g_core_state, __ATOMIC_ACQUIRE);
    if (state == 0) {
        return;
    }
    if (!end_process()) {
        park(why);
        return;
    }
    if (state < 0 && g_core.kernel.relaunch) {
        relaunch();
    }
    nc_log("hakuX-libretro", "%s: the core cannot stop yet; ending the process", why);
    fflush(NULL);
    _exit(0);
}

RETRO_API void retro_unload_game(void)
{
    nc_lr_end("unload");
}

/* HAKUX_LR_STATS=1: every 10 s, how long retro_run takes RetroArch's thread
 * (avg, max) and its video part. */
static void run_stat(uint64_t t0, uint64_t tv, uint64_t t1)
{
    static int on = -1;
    static uint64_t start, n, run_sum, run_max, vid_sum, vid_max;
    if (on < 0) {
        const char *e = getenv("HAKUX_LR_STATS");
        on = e && atoi(e) != 0;
    }
    if (!on) {
        return;
    }
    start = start ? start : t0;
    n++;
    run_sum += t1 - t0;
    run_max = t1 - t0 > run_max ? t1 - t0 : run_max;
    vid_sum += t1 - tv;
    vid_max = t1 - tv > vid_max ? t1 - tv : vid_max;
    if (t1 - start >= 10000000000ull) {
        nc_log("hakuX-libretro", "stats: %.2f retro_run/s, %.2f ms each (max %.2f), video %.2f ms (max %.2f)",
               (double)n * 1e9 / (double)(t1 - start), (double)run_sum / (double)n / 1e6, (double)run_max / 1e6,
               (double)vid_sum / (double)n / 1e6, (double)vid_max / 1e6);
        start = t1;
        n = run_sum = run_max = vid_sum = vid_max = 0;
    }
}

RETRO_API void retro_run(void)
{
    const uint64_t now = now_ns();
    nc_lr_input_run(g_input_poll, g_input_state);
    nc_dev_nv2a_display_vsync(now);         /* RetroArch's frame tick is the display's vsync */
    gap_tick(now);
    nc_lr_audio_run(g_audio_batch);
    const uint64_t tv = now_ns();
    nc_lr_video_run(g_video, now);
    run_stat(now, tv, now_ns());
    nc_lr_options_run();

    const char *why = __atomic_load_n(&g_fail, __ATOMIC_ACQUIRE);
    why = why ? why : __atomic_load_n(&g_core_state, __ATOMIC_ACQUIRE) < 0 ? "the core stopped" : nc_hle_fatal();
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
