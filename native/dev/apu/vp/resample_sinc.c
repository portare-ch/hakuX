/*
 * hakuX native core — the band-limited sinc quality (resample.h):
 * libsamplerate's callback API with SRC_SINC_MEDIUM_QUALITY (its 97 dB,
 * 90 % bandwidth filter: flat to 9 kHz of a 22.05 kHz voice, -3.25 dB at
 * 10 kHz; chosen over FASTEST's 80 % and BEST's cost). libsamplerate
 * allocates the converter once, at vp_sinc_new; reads and resets don't
 * allocate. Built only with libsamplerate (cmake/samplerate.cmake:
 * NC_HAVE_SINC).
 */
#include <samplerate.h>

#include "resample_sinc.h"

void *vp_sinc_new(long (*cb)(void *cb_data, float **data), int channels, void *cb_data)
{
    int err = 0;
    SRC_STATE *s = src_callback_new(cb, SRC_SINC_MEDIUM_QUALITY, channels, &err, cb_data);
    return err ? src_delete(s) : s;
}

/* libsamplerate takes ratios from 1/256 to 256: the VP's pitch range
 * (2^(-pitch / 4096), pitch a signed 16-bit step) is exactly that */
long vp_sinc_read(void *s, double ratio, long frames, float *data)
{
    if (!(ratio >= 1.0 / 256)) {
        ratio = 1.0 / 256;
    } else if (ratio > 256.0) {
        ratio = 256.0;
    }
    return src_callback_read(s, ratio, frames, data);
}

void vp_sinc_reset(void *s)
{
    src_reset(s);
}

void vp_sinc_delete(void *s)
{
    src_delete(s);
}
