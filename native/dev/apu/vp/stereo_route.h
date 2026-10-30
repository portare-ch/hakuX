/*
 * hakuX native core — the VP's own stereo mix (MON_VP, the default without
 * the DSP) for 2D voices: a voice routed to the speakers is mixed into the
 * left and right speakers by its speaker sends, each with its own gain, in
 * the stereo downmix of DirectSound's speaker bins: front left and right to
 * their own speaker, the centre to both at -3 dB, a back bin to its own
 * side at -3 dB (ITU-R BS.775's stereo downmix). The LFE bin and the bins
 * that aren't speakers (the 3D crosstalk bins, I3DL2 and the effect sends,
 * the multipass bin) are the guest DSP program's, whose effects this mix
 * doesn't produce: they are left out of it.
 *
 * The route is a sound's, chosen once (vp.c): at its first frame with an
 * audible send, the speakers if one of them goes to a speaker bin, else
 * the monitor's existing mix (a voice sending only to the LFE or effect
 * bins, which would otherwise go silent), until its next VOICE_ON. Not
 * chosen again as its volumes change: a dry send fading to mute beside an
 * effect send fades the voice out, rather than switching it to the
 * monitor's mix with the effect send's level, and a dry send fading in
 * beside an effect send stays in the monitor's mix. 3D voices and
 * multipass groups keep the monitor's mix.
 *
 * Bins 0 to 5 are DirectSound's speaker conventions (DSMIXBIN_FRONT_LEFT to
 * DSMIXBIN_BACK_RIGHT; Cxbx-Reloaded's XbDSoundTypes.h), not a rule of the
 * hardware's. The weights are the ones DirectSound's own DSP image uses for
 * a stereo console, measured in Halo (2026-10-30, with the full DSP): its
 * EP downmixes the speaker bins for the analog output by a 5x2 table in
 * its Y memory (Y:0000-0009, in AC-3's order L, C, R, Ls, Rs: 1 and 0,
 * 0.7071 and 0.7071, 0 and 1, 0.7071 and 0, 0 and 0.7071; no LFE), and
 * tones through each bin came out at 0 and -3.01 dB alike at 100 Hz, 1 kHz
 * and 8 kHz. What the hardware's analog stage and level do isn't measured.
 * A send's source channel follows the mix-bin mixer's contract, its slot
 * modulo the voice's channels (vp.c mix_one_bin); its destination, the bin
 * it names.
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

/* A 2D voice's route in the VP's own mix, a sound's (vp.c) */
enum { VP_ROUTE_UNDECIDED = 0, VP_ROUTE_SPEAKERS = 1, VP_ROUTE_MONITOR = 2 };

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

/* The route a voice's sends ask for now: the speakers with an audible send
 * to a speaker bin, the monitor's mix with audible sends to none, undecided
 * with every send muted. att[] is the sends' attenuate(volume), 0 for a
 * muted send (a muted send may go anywhere). */
static inline int vp_stereo_classify(const int bin[VP_SENDS], const float att[VP_SENDS])
{
    int route = VP_ROUTE_UNDECIDED;
    float w[2];
    for (int b = 0; b < VP_SENDS; b++) {
        if (att[b] != 0.0f) {
            if (vp_downmix_weights(bin[b], w)) {
                return VP_ROUTE_SPEAKERS;
            }
            route = VP_ROUTE_MONITOR;
        }
    }
    return route;
}

/* The gains of a speaker-routed voice's sends to the two speakers: g[o][s]
 * from source channel s to output o (0 left, 1 right), each speaker send's
 * ea * attenuation / 2^(its bin's headroom) added in by its downmix
 * weights; every gain 0 when its speaker sends are muted. */
static inline void vp_stereo_gains(const int bin[VP_SENDS], const float att[VP_SENDS],
                                   const uint8_t headroom[NUM_MIXBINS], unsigned channels, float ea,
                                   float g[2][2])
{
    float w[2];
    g[0][0] = g[0][1] = g[1][0] = g[1][1] = 0.0f;
    for (int b = 0; b < VP_SENDS; b++) {
        if (att[b] == 0.0f || !vp_downmix_weights(bin[b], w)) {
            continue;
        }
        const float a = ea * att[b] / (float)(1u << headroom[bin[b]]);
        g[0][b % channels] += a * w[0];
        g[1][b % channels] += a * w[1];
    }
}

/* The voice's frame into the speakers by its gains (vp_stereo_gains), added
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
