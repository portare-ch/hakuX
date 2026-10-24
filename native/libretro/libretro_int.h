/*
 * hakuX native core — the libretro core's parts (LIBRETRO_SPEC.md):
 * core.c the ABI and the game's lifecycle, audio.c the APU's output,
 * input.c the RetroPad as the Duke, video.c the frames to RetroArch.
 */
#ifndef NATIVE_LIBRETRO_LIBRETRO_INT_H
#define NATIVE_LIBRETRO_LIBRETRO_INT_H

#include "native/core/nc.h"
#include "native/libretro/libretro.h"

/* The emulated video rate, exactly the Xbox's: 60000/1001 Hz (nv2a.c's
 * vblank). The core reports it, and each retro_run is one period. */
#define NC_LR_FPS_NUM 60000ull
#define NC_LR_FPS_DEN 1001ull
#define NC_LR_FRAME_NS (1000000000ull * NC_LR_FPS_DEN / NC_LR_FPS_NUM)   /* whole ns */

/* core.c — the frontend's environment callback; a failure that stops the
 * content (logged; retro_run asks RetroArch to shut down); the guest booted,
 * once (false: it could not be). */
bool nc_lr_env(unsigned cmd, void *data);
void nc_lr_fail(const char *why);
bool nc_lr_start(void);

/* audio.c — one period's samples to RetroArch (retro_run). */
void nc_lr_audio_run(retro_audio_sample_batch_t batch);

/* input.c — each port's RetroPad to its player (retro_run). */
void nc_lr_input_run(retro_input_poll_t poll, retro_input_state_t state);
/* The input descriptors and controller info (retro_set_environment). */
void nc_lr_input_describe(retro_environment_t env);

/* video.c — the newest front frame to RetroArch, exactly one video_cb
 * (retro_run); its source size and the aspect it is shown at; the overscan
 * a TV hid, on each side of a w x h frame; a frame of w x h shown as ow x oh
 * (RetroArch told when that changes). */
void nc_lr_video_run(retro_video_refresh_t video, uint64_t now_ns);
void nc_lr_video_geometry(struct retro_game_geometry *g);
void nc_lr_overscan(uint32_t w, uint32_t h, uint32_t *cx, uint32_t *cy);
void nc_lr_video_size(uint32_t w, uint32_t h, uint32_t ow, uint32_t oh);

/* vulkan.c — the renderer on RetroArch's Vulkan device, the guest booted in
 * its context_reset (load: false when the frontend has none, the frames then
 * read back); the frame handed over as an image (false: not with Vulkan). */
bool nc_lr_vk_negotiate(void);
bool nc_lr_vk_run(retro_video_refresh_t video);

#endif
