/*
 * hakuX native core — the frames to RetroArch, in the libretro core.
 *
 * Bring-up (LIBRETRO_SPEC.md M1): the front frame is read back, as xboxrun's
 * captures are (hle_disp_read_front: the gamma ramp is in it), and handed
 * over as XRGB8888. M3 replaces this with the Vulkan image handoff. A
 * retro_run with no new frame shows the last one again (a 30 fps title, a
 * static screen); RetroArch letterboxes it at the aspect reported here.
 *
 * A standard-definition frame loses its overscan, as a TV hid it: 1/30 of
 * its height at the top and bottom and of its width at each side, so the
 * picture keeps its shape and fills a 4:3 screen. Titles left that band
 * black (Burnout 3: 16 lines above and below its 448) or kept their HUD
 * clear of it (the title-safe area).
 */
#include "native/libretro/libretro_int.h"
#include "native/dev/dev.h"
#include "hw/xbox/hle/vk/display.h"

#include <glib.h>
#include <stdlib.h>
#include <string.h>

#define MAX_W 3840
#define MAX_H 2160

static struct {
    uint32_t *buf;
    size_t cap;              /* pixels */
    uint32_t w, h;
    uint32_t gen, swap;      /* the frame shown: the display's generation, its swap */
    bool have, dupe_checked, can_dupe;
} g_v;

/* As present.c's auto: the frame's own shape, but a standard-definition
 * frame (640x480, 720x480: under 16:10) is shown 4:3. */
static float aspect_of(uint32_t w, uint32_t h)
{
    const float fa = (float)w / (float)h;
    return fa < 1.6f ? 4.0f / 3.0f : fa;
}

void nc_lr_video_geometry(struct retro_game_geometry *g)
{
    g->base_width = g_v.have ? g_v.w : 640;
    g->base_height = g_v.have ? g_v.h : 480;
    g->max_width = MAX_W;
    g->max_height = MAX_H;
    g->aspect_ratio = aspect_of(g->base_width, g->base_height);
}

/* The overscan a TV hid, on each side (SD frames: under 16:10). */
static void overscan(uint32_t w, uint32_t h, uint32_t *cx, uint32_t *cy)
{
    const bool sd = (float)w / (float)h < 1.6f;
    *cx = sd ? w / 30 : 0;
    *cy = sd ? h / 30 : 0;
}

/* The front frame, if it is newer than the last one shown, into g_v.buf
 * (without its overscan). */
static bool take_new(retro_environment_t env)
{
    uint32_t gen, swap;
    if (!hle_disp_front(&gen, &swap) || (g_v.have && gen == g_v.gen && swap == g_v.swap)) {
        return false;
    }
    uint32_t w = 0, h = 0;
    uint8_t *rgba = hle_disp_read_front(&w, &h);
    if (!rgba || !w || !h || w > MAX_W || h > MAX_H) {
        g_free(rgba);
        return false;
    }
    uint32_t cx, cy;
    overscan(w, h, &cx, &cy);
    const uint32_t ow = w - 2 * cx, oh = h - 2 * cy;
    const size_t n = (size_t)ow * oh;
    if (n > g_v.cap) {
        uint32_t *b = realloc(g_v.buf, n * sizeof(*b));
        if (!b) {
            g_free(rgba);
            return false;
        }
        g_v.buf = b;
        g_v.cap = n;
    }
    for (uint32_t y = 0; y < oh; y++) {
        const uint8_t *p = rgba + 4 * ((size_t)(y + cy) * w + cx);
        uint32_t *d = g_v.buf + (size_t)y * ow;
        for (uint32_t x = 0; x < ow; x++, p += 4) {
            d[x] = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
        }
    }
    g_free(rgba);
    const bool resized = !g_v.have || ow != g_v.w || oh != g_v.h;
    g_v.w = ow;
    g_v.h = oh;
    g_v.gen = gen;
    g_v.swap = swap;
    g_v.have = true;
    if (resized) {
        struct retro_game_geometry geo;
        nc_lr_video_geometry(&geo);
        env(RETRO_ENVIRONMENT_SET_GEOMETRY, &geo);
        nc_log("hakuX-libretro", "video: %ux%u, %ux%u without its overscan, shown at %.3f", w, h, ow, oh,
               geo.aspect_ratio);
    }
    return true;
}

void nc_lr_video_run(retro_environment_t env, retro_video_refresh_t video, uint64_t now_ns)
{
    static uint32_t black[640 * 480];
    if (!g_v.dupe_checked) {
        g_v.dupe_checked = true;
        g_v.can_dupe = false;
        env(RETRO_ENVIRONMENT_GET_CAN_DUPE, &g_v.can_dupe);
    }
    nc_dev_nv2a_display_pickup(now_ns);     /* the vblank keeps clear of this look */
    if (take_new(env)) {
        video(g_v.buf, g_v.w, g_v.h, g_v.w * sizeof(uint32_t));
    } else if (g_v.have) {
        video(g_v.can_dupe ? NULL : g_v.buf, g_v.w, g_v.h, g_v.w * sizeof(uint32_t));
    } else {
        video(black, 640, 480, 640 * sizeof(uint32_t));
    }
}
