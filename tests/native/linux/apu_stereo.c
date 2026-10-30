/* The VP's own mix (MON_VP, the default without the DSP) through the real
 * voice processing: voices set up and started by the PIO methods
 * DirectSound sends (mcpx_apu_vp_write), their PCM read from guest RAM
 * through the scatter-gather table, a frame mixed by mcpx_apu_vp_frame and
 * read from the monitor's output. Ordinary 2D stereo voices are mixed by
 * their sends (vp/stereo_route.h); 3D voices, multipass groups and voices
 * with other sends keep the monitor's mix; the full-DSP monitor leaves the
 * monitor's output alone and fills the mix bins. A voice reused for a new
 * sound starts its HRTF filter from silence. One worker and several give
 * the same output. No guest executable or sound device: the frame thread
 * stays parked (SECTL off) and this test runs the frames. */
#include <math.h>

#include "native/dev/apu/apu_int.h"
#include "native/dev/dev.h"
#include "native/core/irq.h"
#include "native/core/mmio.h"
#include "native/core/vm.h"

static unsigned failures;

static void check(bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    failures += !ok;
}

#define RAM_SIZE   (1u << 20)
#define VOICES_PA  0x10000u     /* the voice table: 256 x 128 bytes */
#define SGE_PA     0x20000u     /* the scatter-gather table */
#define PCM_PA     0x30000u     /* sample pages, mapped in order */
#define PCM_PAGES  64u
#define LOOP_FRAMES 64u         /* each voice loops 64 sample frames */

static uint8_t *g_ram;
static MCPXAPUState *d;

static void wr32(uint32_t pa, uint32_t v)
{
    memcpy(g_ram + pa, &v, 4);
}

static void pio(uint32_t method, uint32_t arg)
{
    mcpx_apu_vp_write(d, method, arg, 4);
}

/* A fresh APU with its voice table, an identity scatter-gather table over
 * the sample pages, and empty voice lists */
static void apu_up(bool dsp, bool hrtf, unsigned workers)
{
    static NcVm vm;
    memset(g_ram, 0, RAM_SIZE);
    vm = (NcVm){.phys = g_ram, .ram_size = RAM_SIZE};
    char w[8];
    snprintf(w, sizeof(w), "%u", workers);
    setenv("NC_APU_WORKERS", w, 1);
    setenv("NC_APU_DSP", dsp ? "1" : "0", 1);
    setenv("NC_APU_HRTF", hrtf ? "1" : "0", 1);
    unsetenv("NC_TRACE_APU");
    nc_dev_apu_init(&vm);
    d = g_state;
    for (uint32_t p = 0; p < PCM_PAGES; p++) {
        wr32(SGE_PA + 8 * p, PCM_PA + 0x1000 * p);
    }
    d->regs[NV_PAPU_VPVADDR] = VOICES_PA;
    d->regs[NV_PAPU_VPSGEADDR] = SGE_PA;
    mcpx_apu_vp_refresh_voice_cache(d, VOICES_PA);
    d->regs[NV_PAPU_TVL2D] = d->regs[NV_PAPU_TVL3D] = d->regs[NV_PAPU_TVLMP] = 0xFFFF;
}

static void apu_down(void)
{
    nc_dev_apu_stop();
    nc_dev_apu_free();
    nc_mmio_reset();
    nc_irq_reset();
}

/* Sample page p holds LOOP_FRAMES 16-bit frames of l (and r, stereo) */
static void pcm_page(uint32_t p, bool stereo, int16_t l, int16_t r)
{
    int16_t *s = (int16_t *)(g_ram + PCM_PA + 0x1000 * p);
    for (uint32_t i = 0; i < LOOP_FRAMES; i++) {
        s[stereo ? 2 * i : i] = l;
        if (stereo) {
            s[2 * i + 1] = r;
        }
    }
}

typedef struct {
    uint16_t handle;
    unsigned list;              /* NV1BA0_PIO_SET_ANTECEDENT_VOICE_LIST_*_TOP */
    bool stereo;
    uint32_t page;              /* its samples' page (a byte offset of page x 4 KiB) */
    int bin[8];
    uint16_t vol[8];
    uint32_t fmt_extra;         /* multipass, its bin, clear-mix */
    uint16_t hrtf;              /* 3D: its HRTF entry, HRTF_NULL_HANDLE for none */
} Voice;

static Voice voice(uint16_t handle, unsigned list, bool stereo, uint32_t page)
{
    Voice v = {.handle = handle, .list = list, .stereo = stereo, .page = page, .hrtf = HRTF_NULL_HANDLE};
    for (int b = 0; b < 8; b++) {
        v.bin[b] = b;
        v.vol[b] = 0xFFF;
    }
    return v;
}

static void start(const Voice *v)
{
    pio(NV1BA0_PIO_SET_CURRENT_VOICE, v->handle);
    pio(NV1BA0_PIO_SET_VOICE_CFG_VBIN, (uint32_t)v->bin[0] | v->bin[1] << 5 | v->bin[2] << 10 |
                                           v->bin[3] << 16 | v->bin[4] << 21 | (uint32_t)v->bin[5] << 26);
    /* a block a sample frame: DirectSound's samples per block are the
     * voice's channels (the field holds one less) */
    pio(NV1BA0_PIO_SET_VOICE_CFG_FMT,
        (uint32_t)v->bin[6] | v->bin[7] << 5 | v->fmt_extra |
            (v->stereo ? NV_PAVS_VOICE_CFG_FMT_STEREO | 1u << 16 : 0) |
            NV_PAVS_VOICE_CFG_FMT_LOOP | NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE_S16 << 28 |
            (uint32_t)NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE_B16 << 30);
    const uint16_t *a = v->vol;
    pio(NV1BA0_PIO_SET_VOICE_TAR_VOLA, (a[6] & 0xF) | (uint32_t)a[0] << 4 | (a[7] & 0xF) << 16 | (uint32_t)a[1] << 20);
    pio(NV1BA0_PIO_SET_VOICE_TAR_VOLB,
        (a[6] >> 4 & 0xF) | (uint32_t)a[2] << 4 | (a[7] >> 4 & 0xF) << 16 | (uint32_t)a[3] << 20);
    pio(NV1BA0_PIO_SET_VOICE_TAR_VOLC,
        (a[6] >> 8 & 0xF) | (uint32_t)a[4] << 4 | (a[7] >> 8 & 0xF) << 16 | (uint32_t)a[5] << 20);
    pio(NV1BA0_PIO_SET_VOICE_TAR_PITCH, 0);
    pio(NV1BA0_PIO_SET_VOICE_TAR_HRTF, v->hrtf);
    pio(NV1BA0_PIO_SET_VOICE_CFG_BUF_BASE, 0x1000 * v->page);
    pio(NV1BA0_PIO_SET_VOICE_CFG_BUF_LBO, 0);
    pio(NV1BA0_PIO_SET_VOICE_CFG_BUF_EBO, LOOP_FRAMES - 1);
    pio(NV1BA0_PIO_SET_ANTECEDENT_VOICE, v->list << 16);
    pio(NV1BA0_PIO_VOICE_ON, v->handle);        /* envelopes off: full level */
}

static int16_t g_out[NUM_SAMPLES_PER_FRAME][2];
static float g_mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME];

/* n frames; the monitor's output of the last (frame_buf's first 32 pairs:
 * ep_frame_div stays 0 here) and its mix bins */
static void frames(int n)
{
    for (int f = 0; f < n; f++) {
        pthread_mutex_lock(&d->lock);
        memset(d->monitor.frame_buf, 0, sizeof(d->monitor.frame_buf));
        memset(g_mixbins, 0, sizeof(g_mixbins));
        mcpx_apu_vp_frame(d, g_mixbins);
        memcpy(g_out, d->monitor.frame_buf, sizeof(g_out));
        pthread_mutex_unlock(&d->lock);
    }
}

static float attenuation(uint16_t vol)
{
    return vol == 0xFFF ? 0.0f : powf(10.0f, vol / (64.0f * -20.0f));
}

/* the monitor's 16-bit sample of x */
static int pcm(float x)
{
    return (int)(fminf(fmaxf(x, -1.0f), 1.0f) * 32767.0f);
}

static bool all(int ch, int want, int tol)
{
    for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
        if (abs(g_out[i][ch] - want) > tol) {
            printf("  sample %d channel %d: %d, expected %d\n", i, ch, g_out[i][ch], want);
            return false;
        }
    }
    return true;
}

static bool channels_equal(void)
{
    for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
        if (g_out[i][0] != g_out[i][1]) {
            return false;
        }
    }
    return true;
}

#define HALF 16384              /* 0.5 */

static void test_2d_mono_panned(void)
{
    apu_up(false, true, 1);
    pcm_page(0, false, HALF, 0);
    Voice v = voice(100, NV1BA0_PIO_SET_ANTECEDENT_VOICE_LIST_2D_TOP, false, 0);
    v.vol[0] = 0;               /* front left, 0 dB; front right muted */
    start(&v);
    frames(4);
    check(all(0, pcm(0.5f), 1) && all(1, 0, 0), "2D mono to front left only: left 0.5, right exactly 0");
    apu_down();

    apu_up(false, true, 1);
    pcm_page(0, false, HALF, 0);
    v.vol[0] = 0x040;           /* -1 dB */
    v.vol[1] = 0x300;           /* -12 dB */
    start(&v);
    frames(4);
    const float l = 0.5f * attenuation(0x040), r = 0.5f * attenuation(0x300);
    check(all(0, pcm(l), 1) && all(1, pcm(r), 1),
          "2D mono, unequal sends: each speaker at its own send's attenuation");
    apu_down();
}

static void test_2d_stereo(void)
{
    apu_up(false, true, 1);
    pcm_page(0, true, HALF, -HALF / 2);
    Voice v = voice(101, NV1BA0_PIO_SET_ANTECEDENT_VOICE_LIST_2D_TOP, true, 0);
    v.vol[0] = 0x080;           /* slot 0 (source left) to front left */
    v.vol[1] = 0x100;           /* slot 1 (source right) to front right */
    start(&v);
    frames(4);
    check(all(0, pcm(0.5f * attenuation(0x080)), 1) && all(1, pcm(-0.25f * attenuation(0x100)), 1),
          "2D stereo: each source channel to its speaker at its gain");
    apu_down();

    apu_up(false, true, 1);
    pcm_page(0, true, HALF, -HALF / 2);
    v.bin[0] = 1;               /* reordered: slot 0 to front right, slot 1 to front left */
    v.bin[1] = 0;
    start(&v);
    frames(4);
    check(all(1, pcm(0.5f * attenuation(0x080)), 1) && all(0, pcm(-0.25f * attenuation(0x100)), 1),
          "2D stereo, reordered bins: destination by bin, source by slot");
    apu_down();
}

static void test_2d_headroom(void)
{
    apu_up(false, true, 1);
    pcm_page(0, false, HALF, 0);
    pio(NV1BA0_PIO_SET_SUBMIX_HEADROOM + 4 * 0, 1);
    pio(NV1BA0_PIO_SET_SUBMIX_HEADROOM + 4 * 1, 2);
    Voice v = voice(102, NV1BA0_PIO_SET_ANTECEDENT_VOICE_LIST_2D_TOP, false, 0);
    v.vol[0] = v.vol[1] = 0;
    start(&v);
    frames(4);
    check(all(0, pcm(0.25f), 1) && all(1, pcm(0.125f), 1), "2D headroom: each bin's own, kept");
    apu_down();
}

/* a send to the centre: the monitor's mix, the loudest send's gain on both
 * channels */
static void test_2d_centre_unsupported(void)
{
    apu_up(false, true, 1);
    pcm_page(0, false, HALF, 0);
    Voice v = voice(103, NV1BA0_PIO_SET_ANTECEDENT_VOICE_LIST_2D_TOP, false, 0);
    v.vol[0] = 0x300;
    v.vol[2] = 0;               /* front centre, 0 dB */
    start(&v);
    frames(4);
    check(all(0, pcm(0.5f), 1) && channels_equal(), "2D with a centre send: the monitor's mix kept");
    apu_down();
}

/* A 3D voice: the monitor's mix (no panning by its sends), with the HRTF
 * filter off, or on with an entry that passes its input (tap 0 both ears) */
static void test_3d(void)
{
    for (int hrtf = 0; hrtf < 2; hrtf++) {
        apu_up(false, hrtf, 1);
        pcm_page(0, false, HALF, 0);
        pio(NV1BA0_PIO_SET_HRTF_SUBMIXES, 0 | 1 << 8 | 2 << 16 | 3 << 24);
        pio(NV1BA0_PIO_SET_CURRENT_HRTF_ENTRY, 5);
        pio(NV1BA0_PIO_SET_HRIR, 127 | 127 << 8);   /* taps 0: 127/128, normalized to 1 */
        Voice v = voice(7, NV1BA0_PIO_SET_ANTECEDENT_VOICE_LIST_3D_TOP, false, 0);
        v.vol[0] = 0;               /* hrtf_submix 0 (front left), 0 dB */
        v.vol[1] = 0x300;           /* front right, -12 dB */
        v.hrtf = 5;
        start(&v);
        frames(hrtf ? 40 : 4);
        check(channels_equal() && (hrtf || all(0, pcm(0.5f), 1)),
              hrtf ? "3D voice, HRTF on: the monitor's mix kept (both channels alike)"
                   : "3D voice, HRTF off: the monitor's mix kept (0.5 on both channels)");
        apu_down();
    }
}

/* A multipass group: a source voice into the multipass bin at -6 dB, its
 * destination from it to both front bins at 0 dB: the destination's mix
 * of the source, once */
static void test_multipass(void)
{
    apu_up(false, true, 1);
    pcm_page(0, false, HALF, 0);
    Voice dst = voice(200, NV1BA0_PIO_SET_ANTECEDENT_VOICE_LIST_MP_TOP, false, 1);
    dst.fmt_extra = NV_PAVS_VOICE_CFG_FMT_MULTIPASS | MULTIPASS_BIN << 16 | NV_PAVS_VOICE_CFG_FMT_CLEAR_MIX;
    dst.vol[0] = dst.vol[1] = 0;
    Voice src = voice(201, NV1BA0_PIO_SET_ANTECEDENT_VOICE_LIST_MP_TOP, false, 0);
    src.bin[0] = MULTIPASS_BIN;
    src.vol[0] = 0x180;
    start(&dst);                /* the list's top is the last started: src, then dst */
    start(&src);
    frames(4);
    check(all(0, pcm(0.5f * attenuation(0x180)), 1) && channels_equal(),
          "multipass group: the destination's mix of its source, once");
    apu_down();
}

/* The full-DSP monitor: the voice's sends into the mix bins, nothing in the
 * monitor's output */
static void test_full_dsp(void)
{
    apu_up(true, true, 1);
    pcm_page(0, false, HALF, 0);
    Voice v = voice(104, NV1BA0_PIO_SET_ANTECEDENT_VOICE_LIST_2D_TOP, false, 0);
    v.vol[0] = 0;
    v.vol[1] = 0x300;
    start(&v);
    frames(4);
    bool bins = true;
    for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
        bins &= fabsf(g_mixbins[0][i] - 0.5f) < 1e-4f && fabsf(g_mixbins[1][i] - 0.5f * attenuation(0x300)) < 1e-4f;
    }
    check(bins && all(0, 0, 0) && all(1, 0, 0), "full-DSP monitor: mix bins by send, monitor output untouched");
    apu_down();
}

/* A 3D voice that played through a delayed HRTF entry, started again on
 * silence: silence from the first frame (its previous sound's tail is
 * cleared at VOICE_ON) */
static void test_voice_reuse_hrtf_history(void)
{
    apu_up(false, true, 1);
    pcm_page(0, false, HALF, 0);
    pcm_page(1, false, 0, 0);
    pio(NV1BA0_PIO_SET_HRTF_SUBMIXES, 0 | 1 << 8 | 2 << 16 | 3 << 24);
    pio(NV1BA0_PIO_SET_CURRENT_HRTF_ENTRY, 9);
    pio(NV1BA0_PIO_SET_HRIR_X, 127 | 127 << 8);     /* tap 30 both ears: a 30-sample delay */
    Voice v = voice(8, NV1BA0_PIO_SET_ANTECEDENT_VOICE_LIST_3D_TOP, false, 0);
    v.vol[0] = v.vol[1] = 0;
    v.hrtf = 9;
    start(&v);
    frames(60);
    const bool sounding = g_out[NUM_SAMPLES_PER_FRAME - 1][0] > 1000;
    d->regs[NV_PAPU_TVL3D] = 0xFFFF;                /* the list again empty: started anew */
    v.page = 1;                                     /* on silence */
    start(&v);
    frames(1);
    check(sounding && all(0, 0, 0) && all(1, 0, 0), "reused 3D voice: its new sound starts from silence");
    apu_down();
}

/* Twelve voices (eligible, a centre send, 3D): one worker and four give the
 * same output, to the rounding of the sums' order */
static void twelve_voices(unsigned workers, int16_t out[NUM_SAMPLES_PER_FRAME][2])
{
    apu_up(false, true, workers);
    for (uint32_t p = 0; p < 12; p++) {
        pcm_page(p, p % 3 == 1, (int16_t)(1000 + 700 * p), (int16_t)(-300 * p));
    }
    pio(NV1BA0_PIO_SET_HRTF_SUBMIXES, 0 | 1 << 8 | 2 << 16 | 3 << 24);
    for (uint16_t k = 0; k < 12; k++) {
        const bool three_d = k % 4 == 3;
        Voice v = voice(three_d ? k : 100 + k,
                        three_d ? NV1BA0_PIO_SET_ANTECEDENT_VOICE_LIST_3D_TOP : NV1BA0_PIO_SET_ANTECEDENT_VOICE_LIST_2D_TOP,
                        k % 3 == 1, k);
        v.vol[0] = (uint16_t)(0x40 * k);
        v.vol[1] = (uint16_t)(0x300 - 0x30 * k);
        if (k % 5 == 2) {
            v.vol[2] = 0x100;   /* a centre send: the monitor's mix */
        }
        start(&v);
    }
    frames(4);
    memcpy(out, g_out, sizeof(g_out));
    apu_down();
}

static void test_workers(void)
{
    int16_t one[NUM_SAMPLES_PER_FRAME][2], four[NUM_SAMPLES_PER_FRAME][2];
    twelve_voices(1, one);
    twelve_voices(4, four);
    int worst = 0;
    bool sound = false;
    for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
        for (int ch = 0; ch < 2; ch++) {
            worst = MAX(worst, abs(one[i][ch] - four[i][ch]));
            sound |= one[i][ch] != 0;
        }
    }
    printf("  workers: largest difference %d (of 12 voices)\n", worst);
    check(sound && worst <= 12, "one worker and four: the same output (within a step a voice)");
}

/* `apu-stereo-smoke bench`: a fixed workload's throughput (not a test, and
 * not a Nova figure): 64 ordinary 2D voices, 32 with a centre send, 32 3D,
 * one worker, 3000 frames */
static int bench(void)
{
    apu_up(false, true, 1);
    for (uint32_t p = 0; p < PCM_PAGES; p++) {
        pcm_page(p, p % 2, (int16_t)(500 + 97 * p), (int16_t)(-200 - 31 * p));
    }
    pio(NV1BA0_PIO_SET_HRTF_SUBMIXES, 0 | 1 << 8 | 2 << 16 | 3 << 24);
    for (uint16_t k = 0; k < 128; k++) {
        const bool three_d = k >= 96;
        Voice v = voice(three_d ? k - 96 : 64 + k,
                        three_d ? NV1BA0_PIO_SET_ANTECEDENT_VOICE_LIST_3D_TOP : NV1BA0_PIO_SET_ANTECEDENT_VOICE_LIST_2D_TOP,
                        k % 2, k % PCM_PAGES);
        v.vol[0] = (uint16_t)(0x100 + 3 * k);
        v.vol[1] = (uint16_t)(0x200 - k);
        if (k >= 64 && k < 96) {
            v.vol[2] = 0x180;
        }
        start(&v);
    }
    frames(100);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    frames(3000);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double s = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    printf("bench: 128 voices, 3000 frames in %.3f s: %.0f frames/s (%.1fx real time), %.2f us a frame\n", s,
           3000 / s, 3000 / s / 1500.0, s / 3000 * 1e6);
    apu_down();
    return 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    alarm(30);
    g_ram = calloc(1, RAM_SIZE);
    if (argc > 1 && !strcmp(argv[1], "bench")) {
        return bench();
    }
    test_2d_mono_panned();
    test_2d_stereo();
    test_2d_headroom();
    test_2d_centre_unsupported();
    test_3d();
    test_multipass();
    test_full_dsp();
    test_voice_reuse_hrtf_history();
    test_workers();
    free(g_ram);
    return failures ? 1 : 0;
}
