/*
 * hakuX native core — the VP's own stereo mix (MON_VP, the default without
 * the DSP) for 2D voices: a voice with an audible send to a speaker bin is
 * mixed into the left and right speakers by its speaker sends, each with
 * its own gain, in the stereo downmix of DirectSound's speaker bins: front
 * left and right to their own speaker, the centre to both at -3 dB, a back
 * bin to its own side at -3 dB (ITU-R BS.775's stereo downmix). The LFE bin
 * and the bins that aren't speakers (the 3D crosstalk bins, I3DL2 and the
 * effect sends, the multipass bin) are the guest DSP program's, whose
 * effects this mix doesn't produce: they are left out of it. A voice whose
 * every audible send goes to those keeps the monitor's existing mix (vp.c),
 * as do 3D voices and multipass groups.
 *
 * Bins 0 to 5 are DirectSound's speaker conventions (DSMIXBIN_FRONT_LEFT to
 * DSMIXBIN_BACK_RIGHT; Cxbx-Reloaded's XbDSoundTypes.h), not a rule of the
 * hardware's; how the guest's DSP program downmixes them for a stereo
 * console isn't established, so the weights are the standard downmix's,
 * not a measurement. A send's source channel follows the mix-bin mixer's
 * contract, its slot modulo the voice's channels (vp.c mix_one_bin); its
 * destination, the bin it names.
 */
#ifndef NATIVE_DEV_APU_VP_STEREO_ROUTE_H
#define NATIVE_DEV_APU_VP_STEREO_ROUTE_H

#include <stdbool.h>
#include <stdint.h>

#include "native/dev/apu/apu_regs.h"

#define VP_BIN_FRONT_LEFT   0   /* DSMIXBIN_FRONT_LEFT */
#define VP_BIN_FRONT_RIGHT  1   /* DSMIXBIN_FRONT_RIGHT */
#define VP_BIN_FRONT_CENTER 2   /* DSMIXBIN_FRONT_CENTER */
#define VP_BIN_LFE          3   /* DSMIXBIN_LOW_FREQUENCY */
#define VP_BIN_BACK_LEFT    4   /* DSMIXBIN_BACK_LEFT */
#define VP_BIN_BACK_RIGHT   5   /* DSMIXBIN_BACK_RIGHT */
#define VP_SENDS            8
#define VP_DOWNMIX_SIDE     0.70710678f     /* -3 dB */

/* A bin's share of the left and right speakers in the stereo downmix;
 * false for a bin that isn't a speaker heard in it (the LFE and every bin
 * past the backs) */
static inline bool vp_downmix_weights(int bin, float w[2])
{
    switch (bin) {
    case VP_BIN_FRONT_LEFT:
        w[0] = 1.0f, w[1] = 0.0f;
        return true;
    case VP_BIN_FRONT_RIGHT:
        w[0] = 0.0f, w[1] = 1.0f;
        return true;
    case VP_BIN_FRONT_CENTER:
        w[0] = w[1] = VP_DOWNMIX_SIDE;
        return true;
    case VP_BIN_BACK_LEFT:
        w[0] = VP_DOWNMIX_SIDE, w[1] = 0.0f;
        return true;
    case VP_BIN_BACK_RIGHT:
        w[0] = 0.0f, w[1] = VP_DOWNMIX_SIDE;
        return true;
    default:
        return false;
    }
}

/* The gains of a voice's sends to the two speakers: g[o][s] from source
 * channel s to output o (0 left, 1 right), each speaker send's
 * ea * attenuation / 2^(its bin's headroom) added in by its downmix
 * weights. att[] is the sends' attenuate(volume), 0 for a muted send (a
 * muted send may go anywhere). false when no audible send goes to a
 * speaker bin: the monitor's mix, g left as it was. */
static inline bool vp_stereo_route(const int bin[VP_SENDS], const float att[VP_SENDS],
                                   const uint8_t headroom[NUM_MIXBINS], unsigned channels, float ea,
                                   float g[2][2])
{
    float w[2];
    bool heard = false;
    for (int b = 0; b < VP_SENDS && !heard; b++) {
        heard = att[b] != 0.0f && vp_downmix_weights(bin[b], w);
    }
    if (!heard) {
        return false;
    }
    g[0][0] = g[0][1] = g[1][0] = g[1][1] = 0.0f;
    for (int b = 0; b < VP_SENDS; b++) {
        if (att[b] == 0.0f || !vp_downmix_weights(bin[b], w)) {
            continue;
        }
        const float a = ea * att[b] / (float)(1u << headroom[bin[b]]);
        g[0][b % channels] += a * w[0];
        g[1][b % channels] += a * w[1];
    }
    return true;
}

/* The voice's frame into the speakers by its gains (vp_stereo_route), added
 * to out: a mono voice's one channel, a stereo voice's two. */
static inline void vp_stereo_mix(float out[NUM_SAMPLES_PER_FRAME][2],
                                 const float in[NUM_SAMPLES_PER_FRAME][2], const float g[2][2],
                                 unsigned channels)
{
    if (channels == 1) {
        for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
            out[i][0] += g[0][0] * in[i][0];
            out[i][1] += g[1][0] * in[i][0];
        }
        return;
    }
    for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
        out[i][0] += g[0][0] * in[i][0] + g[0][1] * in[i][1];
        out[i][1] += g[1][0] * in[i][0] + g[1][1] * in[i][1];
    }
}

#endif
