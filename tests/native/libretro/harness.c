/*
 * hakuX libretro core — a minimal frontend for its checks (check.py).
 *
 *   libretro-harness <core.so> [<disc image> <frames> <save directory>]
 *
 * Loads the core as RetroArch does (dlopen, every retro_* symbol), reports
 * its system and AV info, and with a disc loads it and calls retro_run at
 * the core's rate, checking that each call hands over exactly one video
 * frame. It stops after <frames>, or when the core asks to shut down; the
 * summary is printed before retro_unload_game (which ends the process
 * until the core can stop).
 */
#include "native/libretro/libretro.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SYMS(X)                                                                                              \
    X(retro_api_version, unsigned (*)(void))                                                                 \
    X(retro_set_environment, void (*)(retro_environment_t))                                                  \
    X(retro_set_video_refresh, void (*)(retro_video_refresh_t))                                              \
    X(retro_set_audio_sample, void (*)(retro_audio_sample_t))                                                \
    X(retro_set_audio_sample_batch, void (*)(retro_audio_sample_batch_t))                                    \
    X(retro_set_input_poll, void (*)(retro_input_poll_t))                                                    \
    X(retro_set_input_state, void (*)(retro_input_state_t))                                                  \
    X(retro_init, void (*)(void))                                                                            \
    X(retro_deinit, void (*)(void))                                                                          \
    X(retro_get_system_info, void (*)(struct retro_system_info *))                                           \
    X(retro_get_system_av_info, void (*)(struct retro_system_av_info *))                                     \
    X(retro_set_controller_port_device, void (*)(unsigned, unsigned))                                        \
    X(retro_reset, void (*)(void))                                                                           \
    X(retro_run, void (*)(void))                                                                             \
    X(retro_serialize_size, size_t (*)(void))                                                                \
    X(retro_serialize, bool (*)(void *, size_t))                                                             \
    X(retro_unserialize, bool (*)(const void *, size_t))                                                     \
    X(retro_cheat_reset, void (*)(void))                                                                     \
    X(retro_cheat_set, void (*)(unsigned, bool, const char *))                                               \
    X(retro_load_game, bool (*)(const struct retro_game_info *))                                             \
    X(retro_load_game_special, bool (*)(unsigned, const struct retro_game_info *, size_t))                   \
    X(retro_unload_game, void (*)(void))                                                                     \
    X(retro_get_region, unsigned (*)(void))                                                                  \
    X(retro_get_memory_data, void *(*)(unsigned))                                                            \
    X(retro_get_memory_size, size_t (*)(unsigned))

#define DECLARE(name, type) static __typeof__(type) p_##name;
SYMS(DECLARE)

static const char *g_save_dir;
static bool g_shutdown;
static unsigned g_video_calls, g_new_frames, g_dupes, g_frame_w, g_frame_h;
static size_t g_audio_frames;

static bool environment(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char **)data = g_save_dir;
        return g_save_dir != NULL;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return *(const enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool *)data = true;
        return true;
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        return true;
    case RETRO_ENVIRONMENT_SHUTDOWN:
        g_shutdown = true;
        return true;
    default:
        return false;
    }
}

static void video(const void *data, unsigned w, unsigned h, size_t pitch)
{
    g_video_calls++;
    if (data) {
        g_new_frames++;
        g_frame_w = w;
        g_frame_h = h;
    } else {
        g_dupes++;
    }
}

static void audio_sample(int16_t l, int16_t r)
{
}

static size_t audio_batch(const int16_t *data, size_t frames)
{
    g_audio_frames += frames;
    return frames;
}

static void input_poll(void)
{
}

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 5) {
        fprintf(stderr, "usage: libretro-harness <core.so> [<disc image> <frames> <save directory>]\n");
        return 2;
    }
    void *core = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!core) {
        fprintf(stderr, "FAIL: dlopen: %s\n", dlerror());
        return 1;
    }
#define RESOLVE(name, type)                                                                                  \
    if (!(p_##name = (type)dlsym(core, #name))) {                                                            \
        fprintf(stderr, "FAIL: missing %s\n", #name);                                                        \
        return 1;                                                                                            \
    }
    SYMS(RESOLVE)
    if (p_retro_api_version() != RETRO_API_VERSION) {
        fprintf(stderr, "FAIL: API version %u\n", p_retro_api_version());
        return 1;
    }
    g_save_dir = argc == 5 ? argv[4] : NULL;
    p_retro_set_environment(environment);
    p_retro_set_video_refresh(video);
    p_retro_set_audio_sample(audio_sample);
    p_retro_set_audio_sample_batch(audio_batch);
    p_retro_set_input_poll(input_poll);
    p_retro_set_input_state(input_state);
    p_retro_init();

    struct retro_system_info si;
    p_retro_get_system_info(&si);
    printf("core: %s %s, extensions %s, need_fullpath %d, block_extract %d\n", si.library_name,
           si.library_version, si.valid_extensions, si.need_fullpath, si.block_extract);
    struct retro_system_av_info av;
    p_retro_get_system_av_info(&av);
    printf("av: %.6f fps, %.0f Hz, %ux%u (max %ux%u), aspect %.3f\n", av.timing.fps, av.timing.sample_rate,
           av.geometry.base_width, av.geometry.base_height, av.geometry.max_width, av.geometry.max_height,
           av.geometry.aspect_ratio);
    printf("serialize size: %zu\n", p_retro_serialize_size());

    if (argc == 2) {
        p_retro_deinit();
        dlclose(core);
        printf("unloaded without a game\n");
        return 0;
    }
    const struct retro_game_info game = {.path = argv[2]};
    if (!p_retro_load_game(&game)) {
        printf("load: refused\n");
        p_retro_deinit();
        dlclose(core);
        return 3;
    }
    const long frames = atol(argv[3]);
    const long period_ns = (long)(1e9 / av.timing.fps);
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    long ran = 0;
    for (; ran < frames && !g_shutdown; ran++) {
        const unsigned before = g_video_calls;
        p_retro_run();
        if (g_video_calls != before + 1) {
            fprintf(stderr, "FAIL: retro_run %ld made %u video calls\n", ran, g_video_calls - before);
            return 1;
        }
        next.tv_nsec += period_ns;
        while (next.tv_nsec >= 1000000000L) {
            next.tv_nsec -= 1000000000L;
            next.tv_sec++;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
    printf("ran %ld frames: %u video calls, %u new frames (last %ux%u), %u dupes, %zu audio frames%s\n", ran,
           g_video_calls, g_new_frames, g_frame_w, g_frame_h, g_dupes, g_audio_frames,
           g_shutdown ? ", the core asked to shut down" : "");
    fflush(stdout);
    p_retro_unload_game();
    p_retro_deinit();
    dlclose(core);
    printf("unloaded\n");
    return 0;
}
