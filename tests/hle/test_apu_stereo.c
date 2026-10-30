/* The VP's own stereo mix for ordinary 2D voices (native/dev/apu/vp/
 * stereo_route.h, which vp.c's voice_process calls): which voices it takes,
 * and each send's gain into its own speaker. The 3D, multipass and full-DSP
 * cases, and the dispatch through voice processing, are the native core's
 * apu-stereo-smoke (tests/native/linux/apu_stereo.c). */
#include "hle_test.h"

#include <math.h>

#include "native/dev/apu/vp/stereo_route.h"

#define N NUM_SAMPLES_PER_FRAME

/* the VP's attenuation (vp.c attenuation_init): 0xFFF is mute */
static float att_of(uint16_t vol)
{
    return vol == 0xFFF ? 0.0f : powf(10.0f, vol / (64.0f * -20.0f));
}

typedef struct {
    int bin[VP_SENDS];
    uint16_t vol[VP_SENDS];
    uint8_t headroom[NUM_MIXBINS];
    unsigned channels;
    float ea;
} Voice;

/* every send muted, to bins 0..7 in turn */
static Voice voice(unsigned channels)
{
    Voice v;
    memset(&v, 0, sizeof(v));
    for (int b = 0; b < VP_SENDS; b++) {
        v.bin[b] = b;
        v.vol[b] = 0xFFF;
    }
    v.channels = channels;
    v.ea = 1.0f;
    return v;
}

static bool route(const Voice *v, float g[2][2])
{
    float att[VP_SENDS];
    for (int b = 0; b < VP_SENDS; b++) {
        att[b] = att_of(v->vol[b]);
    }
    return vp_stereo_route(v->bin, att, v->headroom, v->channels, v->ea, g);
}

/* a frame of the voice's samples: channel 0 a ramp, channel 1 another (a
 * mono voice's channel 1 is channel 0's copy, as voice_get_samples makes it) */
static void source(float in[N][2], unsigned channels)
{
    for (int i = 0; i < N; i++) {
        in[i][0] = 0.25f + i / 64.0f;
        in[i][1] = channels == 2 ? -0.5f + i / 128.0f : in[i][0];
    }
}

static void mix(const Voice *v, float out[N][2])
{
    float g[2][2], in[N][2];
    HT_CHECK(route(v, g));
    source(in, v->channels);
    memset(out, 0, sizeof(float[N][2]));
    vp_stereo_mix(out, in, g, v->channels);
}

static void test_mono_left_only(void)
{
    Voice v = voice(1);
    v.vol[0] = 0;               /* slot 0 to bin 0 (front left), 0 dB */
    float out[N][2], in[N][2];
    mix(&v, out);
    source(in, 1);
    for (int i = 0; i < N; i++) {
        HT_CHECK(out[i][0] == in[i][0]);
        HT_CHECK(out[i][1] == 0.0f);
    }
}

static void test_mono_right_only(void)
{
    Voice v = voice(1);
    v.vol[1] = 0;               /* slot 1 to bin 1 (front right) */
    float out[N][2], in[N][2];
    mix(&v, out);
    source(in, 1);
    for (int i = 0; i < N; i++) {
        HT_CHECK(out[i][0] == 0.0f);
        HT_CHECK(out[i][1] == in[i][0]);
    }
}

static void test_mono_equal(void)
{
    Voice v = voice(1);
    v.vol[0] = v.vol[1] = 0;
    float out[N][2], in[N][2];
    mix(&v, out);
    source(in, 1);
    for (int i = 0; i < N; i++) {
        HT_CHECK(out[i][0] == out[i][1]);
        HT_CHECK(out[i][0] == in[i][0]);      /* each speaker's own send: not doubled */
    }
}

static void test_mono_unequal(void)
{
    Voice v = voice(1);
    v.vol[0] = 0x040;           /* -1 dB */
    v.vol[1] = 0x300;           /* -12 dB */
    float out[N][2];
    mix(&v, out);
    const float want = att_of(0x300) / att_of(0x040);
    for (int i = 0; i < N; i++) {
        HT_CHECK(fabsf(out[i][1] / out[i][0] - want) < 1e-6f * want);
    }
}

static void test_stereo_standard(void)
{
    Voice v = voice(2);
    v.vol[0] = 0x080;           /* slot 0 (source left) to front left, -2 dB */
    v.vol[1] = 0x100;           /* slot 1 (source right) to front right, -4 dB */
    float out[N][2], in[N][2];
    mix(&v, out);
    source(in, 2);
    for (int i = 0; i < N; i++) {
        HT_CHECK(fabsf(out[i][0] - att_of(0x080) * in[i][0]) < 1e-6f);
        HT_CHECK(fabsf(out[i][1] - att_of(0x100) * in[i][1]) < 1e-6f);
    }
}

/* destination by the bin a send names, source by its slot */
static void test_stereo_reordered(void)
{
    Voice v = voice(2);
    v.bin[0] = VP_BIN_FRONT_RIGHT;
    v.bin[1] = VP_BIN_FRONT_LEFT;
    v.vol[0] = 0x080;
    v.vol[1] = 0x100;
    float out[N][2], in[N][2];
    mix(&v, out);
    source(in, 2);
    for (int i = 0; i < N; i++) {
        HT_CHECK(fabsf(out[i][1] - att_of(0x080) * in[i][0]) < 1e-6f);
        HT_CHECK(fabsf(out[i][0] - att_of(0x100) * in[i][1]) < 1e-6f);
    }
}

static void test_headroom_per_bin(void)
{
    Voice v = voice(1);
    v.vol[0] = v.vol[1] = 0;
    v.headroom[0] = 1;          /* front left: / 2 */
    v.headroom[1] = 2;          /* front right: / 4 */
    float out[N][2], in[N][2];
    mix(&v, out);
    source(in, 1);
    for (int i = 0; i < N; i++) {
        HT_CHECK(out[i][0] == in[i][0] / 2);
        HT_CHECK(out[i][1] == in[i][0] / 4);
    }
}

/* muted sends to any bin, the effect bins and the multipass bin among them,
 * change nothing */
static void test_muted_sends_ignored(void)
{
    Voice a = voice(2), b = voice(2);
    a.vol[0] = b.vol[0] = 0x080;
    a.vol[1] = b.vol[1] = 0x100;
    b.bin[2] = 2;               /* centre */
    b.bin[3] = 3;               /* LFE */
    b.bin[5] = 12;              /* an effect send */
    b.bin[7] = MULTIPASS_BIN;
    float oa[N][2], ob[N][2];
    mix(&a, oa);
    mix(&b, ob);
    HT_CHECK(memcmp(oa, ob, sizeof(oa)) == 0);
}

static void test_silent(void)
{
    Voice v = voice(2);         /* every send muted */
    float out[N][2];
    mix(&v, out);
    for (int i = 0; i < N; i++) {
        HT_CHECK(out[i][0] == 0.0f && out[i][1] == 0.0f);
    }
    v.vol[0] = v.vol[1] = 0;
    v.ea = 0.0f;                /* the amplitude envelope at 0 */
    mix(&v, out);
    for (int i = 0; i < N; i++) {
        HT_CHECK(out[i][0] == 0.0f && out[i][1] == 0.0f);
    }
}

/* two voices add, with no clipping of their own */
static void test_two_voices_add(void)
{
    Voice a = voice(1), b = voice(2);
    a.vol[0] = a.vol[1] = 0;
    b.vol[0] = b.vol[1] = 0;
    a.ea = b.ea = 1.0f;
    float ga[2][2], gb[2][2], ia[N][2], ib[N][2], both[N][2] = {{0}}, oa[N][2] = {{0}}, ob[N][2] = {{0}};
    HT_CHECK(route(&a, ga));
    HT_CHECK(route(&b, gb));
    source(ia, 1);
    source(ib, 2);
    for (int i = 0; i < N; i++) {     /* past full scale together */
        ia[i][0] = ia[i][1] = 0.9f;
        ib[i][0] = 0.8f;
    }
    vp_stereo_mix(both, ia, ga, 1);
    vp_stereo_mix(both, ib, gb, 2);
    vp_stereo_mix(oa, ia, ga, 1);
    vp_stereo_mix(ob, ib, gb, 2);
    for (int i = 0; i < N; i++) {
        HT_CHECK(both[i][0] == oa[i][0] + ob[i][0]);
        HT_CHECK(both[i][1] == oa[i][1] + ob[i][1]);
    }
    HT_CHECK(both[0][0] > 1.0f);
}

/* an audible send to any other bin: not ordinary stereo, the existing mix
 * (g untouched) */
static void test_unsupported_routing(void)
{
    static const int others[] = {2, 3, 4, 5, 12, 20, MULTIPASS_BIN};
    for (size_t k = 0; k < sizeof(others) / sizeof(others[0]); k++) {
        Voice v = voice(1);
        v.vol[0] = v.vol[1] = 0;
        v.bin[4] = others[k];
        v.vol[4] = 0x200;
        float g[2][2] = {{7.0f, 7.0f}, {7.0f, 7.0f}};
        HT_CHECK(!route(&v, g));
        HT_CHECK(g[0][0] == 7.0f && g[0][1] == 7.0f && g[1][0] == 7.0f && g[1][1] == 7.0f);
    }
}

int main(void)
{
    HT_RUN(test_mono_left_only);
    HT_RUN(test_mono_right_only);
    HT_RUN(test_mono_equal);
    HT_RUN(test_mono_unequal);
    HT_RUN(test_stereo_standard);
    HT_RUN(test_stereo_reordered);
    HT_RUN(test_headroom_per_bin);
    HT_RUN(test_muted_sends_ignored);
    HT_RUN(test_silent);
    HT_RUN(test_two_voices_add);
    HT_RUN(test_unsupported_routing);
    HT_DONE();
}
