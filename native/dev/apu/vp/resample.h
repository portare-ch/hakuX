/*
 * hakuX native core — the voices' resamplers: libsamplerate's callback API
 * shape under the core's own names, in two qualities:
 * - band-limited sinc, the default: a 32-tap Kaiser-windowed sinc through
 *   NEON (resample_sinc.c), on AArch64; elsewhere linear. A quality
 *   setting, not a claim about the Xbox's undocumented interpolation;
 * - linear (resample.c, NEON): what the core had before, lighter.
 * A resampler pulls its input from the callback, 32 frames at a time, and
 * keeps its phase, history and rate across reads; vp_resample_reset clears
 * them (a voice's new sound). The sinc converter reads its filter's half
 * length ahead of what it emits (16 frames, up to 129 for a voice pitched
 * past 48 kHz).
 */
#ifndef NATIVE_DEV_APU_VP_RESAMPLE_H
#define NATIVE_DEV_APU_VP_RESAMPLE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct VpResampler VpResampler;
typedef long (*vp_resample_cb)(void *cb_data, float **data);

enum { VP_RESAMPLE_LINEAR = 0, VP_RESAMPLE_SINC = 1 };

/* The sinc converter is in this build (AArch64) */
bool vp_resample_sinc_available(void);
const char *vp_resample_name(int quality);

/* A resampler of the quality (linear where sinc isn't built in) for
 * channels channels, its input from cb; NULL when out of memory */
VpResampler *vp_resample_new(vp_resample_cb cb, int quality, int channels, void *cb_data);
/* Up to frames output frames at ratio (output rate / input rate, clamped
 * to 1/256 .. 256) into data; fewer only when the input has ended */
long vp_resample_read(VpResampler *r, double ratio, long frames, float *data);
void vp_resample_reset(VpResampler *r);
VpResampler *vp_resample_delete(VpResampler *r);    /* NULL */
int vp_resample_quality(const VpResampler *r);

/* float samples to 16-bit, clipped to [-1, 1], truncated (the monitor's) */
void vp_float_to_short_array(const float *in, short *out, int len);

#endif
