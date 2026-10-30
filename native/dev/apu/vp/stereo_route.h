/*
 * hakuX native core — the VP's own stereo mix (MON_VP, the default without
 * the DSP) for ordinary 2D stereo: a voice whose every audible send goes to
 * DirectSound's front-left or front-right mix bin is mixed into the left or
 * right speaker by those sends, each with its own gain. Everything else
 * (the centre, LFE, rear and effect bins, the multipass bin) is the guest
 * DSP program's to mix, and keeps the monitor's existing mix (vp.c).
 *
 * Bins 0 and 1 are DirectSound's speaker conventions (DSMIXBIN_FRONT_LEFT,
 * DSMIXBIN_FRONT_RIGHT; Cxbx-Reloaded's XbDSoundTypes.h), not a rule of the
 * hardware's. A send's source channel follows the mix-bin mixer's contract,
 * its slot modulo the voice's channels (vp.c mix_one_bin); its destination,
 * the bin it names.
 */
#ifndef NATIVE_DEV_APU_VP_STEREO_ROUTE_H
#define NATIVE_DEV_APU_VP_STEREO_ROUTE_H

#include <stdbool.h>
#include <stdint.h>

#include "native/dev/apu/apu_regs.h"

#define VP_BIN_FRONT_LEFT  0    /* DSMIXBIN_FRONT_LEFT */
#define VP_BIN_FRONT_RIGHT 1    /* DSMIXBIN_FRONT_RIGHT */
#define VP_SENDS           8

/* The gains of a voice's sends to the two speakers: g[o][s] from source
 * channel s to output o (0 left, 1 right), each send's
 * ea * attenuation / 2^(its bin's headroom) added to its own speaker. att[]
 * is the sends' attenuate(volume), 0 for a muted send (a muted send may go
 * anywhere). false when an audible send goes to any other bin: the voice is
 * not ordinary stereo, g is left as it was. */
static inline bool vp_stereo_route(const int bin[VP_SENDS], const float att[VP_SENDS],
                                   const uint8_t headroom[NUM_MIXBINS], unsigned channels, float ea,
                                   float g[2][2])
{
    for (int b = 0; b < VP_SENDS; b++) {
        if (att[b] != 0.0f && bin[b] != VP_BIN_FRONT_LEFT && bin[b] != VP_BIN_FRONT_RIGHT) {
            return false;
        }
    }
    g[0][0] = g[0][1] = g[1][0] = g[1][1] = 0.0f;
    for (int b = 0; b < VP_SENDS; b++) {
        if (att[b] == 0.0f) {
            continue;
        }
        const int out = bin[b] == VP_BIN_FRONT_RIGHT;
        g[out][b % channels] += ea * att[b] / (float)(1u << headroom[bin[b]]);
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
