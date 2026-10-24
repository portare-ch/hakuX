/*
 * hakuX native core — the libretro core's parts (LIBRETRO_SPEC.md):
 * core.c the ABI and the game's lifecycle, audio.c the APU's output,
 * input.c the RetroPad as the Duke, video.c the frames to RetroArch.
 */
#ifndef NATIVE_LIBRETRO_LIBRETRO_INT_H
#define NATIVE_LIBRETRO_LIBRETRO_INT_H

#include "native/core/nc.h"
#include "native/libretro/libretro.h"

/* The emulated video period: nv2a.c's FRAME_NS (59.94 Hz). The core
 * reports 1e9 / this as its rate, and each retro_run is one of it. */
#define NC_LR_FRAME_NS 16683333ull

/* audio.c — one period's samples to RetroArch (retro_run). */
void nc_lr_audio_run(retro_audio_sample_batch_t batch);

/* input.c — each port's RetroPad to its player (retro_run). */
void nc_lr_input_run(retro_input_poll_t poll, retro_input_state_t state);
/* The input descriptors and controller info (retro_set_environment). */
void nc_lr_input_describe(retro_environment_t env);

/* video.c — the newest front frame to RetroArch, exactly one video_cb
 * (retro_run); its source size and the aspect it is shown at. */
void nc_lr_video_run(retro_environment_t env, retro_video_refresh_t video, uint64_t now_ns);
void nc_lr_video_geometry(struct retro_game_geometry *g);

#endif
