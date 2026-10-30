/*
 * hakuX native core — the sinc quality behind resample.c (resample_sinc.c):
 * libsamplerate's converter by an opaque pointer, so its names (src_*,
 * SRC_STATE) stay in that one file.
 */
#ifndef NATIVE_DEV_APU_VP_RESAMPLE_SINC_H
#define NATIVE_DEV_APU_VP_RESAMPLE_SINC_H

void *vp_sinc_new(long (*cb)(void *cb_data, float **data), int channels, void *cb_data);
long vp_sinc_read(void *s, double ratio, long frames, float *data);
void vp_sinc_reset(void *s);
void vp_sinc_delete(void *s);

#endif
