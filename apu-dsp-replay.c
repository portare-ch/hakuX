#define _GNU_SOURCE
#include <dlfcn.h>
#include <signal.h>
#include <sys/stat.h>
#include "native/core/vm.h"
#include "native/dev/apu/apu_int.h"

typedef void (*init_fn)(NcVm *);
typedef void (*void_fn)(void);
typedef void (*frame_fn)(MCPXAPUState *, float [NUM_MIXBINS][NUM_SAMPLES_PER_FRAME]);

static void die(const char *message)
{
    fprintf(stderr, "replay: %s\n", message);
    exit(1);
}

static void *read_file(const char *path, size_t expected)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    if (fseek(f, 0, SEEK_END) || ftell(f) < 0) die("cannot size input");
    size_t length = (size_t)ftell(f);
    if (expected && expected != length) {
        fprintf(stderr, "%s: expected %zu bytes, got %zu\n", path, expected, length);
        exit(1);
    }
    rewind(f);
    char *data = calloc(1, length+1);
    if (!data || fread(data, 1, length, f) != length) die("cannot read input");
    fclose(f);
    return data;
}

static void *read_snapshot(const char *directory, const char *name, size_t expected)
{
    char path[4096];
    if (snprintf(path, sizeof(path), "%s/%s", directory, name) >= (int)sizeof(path))
        die("snapshot path too long");
    return read_file(path, expected);
}

static uintptr_t symbol(const char *json, uintptr_t base, const char *name)
{
    char key[128];
    snprintf(key, sizeof(key), "\"%s\"", name);
    const char *p = strstr(json, key);
    if (!p || !(p = strchr(p+strlen(key), ':'))) die("symbol missing in JSON");
    while (*++p == ' ' || *p == '\n' || *p == '\t') { }
    if (*p == '"') p++;
    char *end;
    unsigned long long value = strtoull(p, &end, 0);
    if (end == p || !value) die("invalid symbol offset in JSON");
    return base+(uintptr_t)value;
}

static void restore_dsp(DSPState *d, const DSPState *snapshot, const DSPState *host)
{
    memcpy(d, snapshot, sizeof(*d));
    d->core.read_peripheral = host->core.read_peripheral;
    d->core.write_peripheral = host->core.write_peripheral;
    d->dma.core = &d->core;
    d->dma.rw_opaque = host->dma.rw_opaque;
    d->dma.scratch_rw = host->dma.scratch_rw;
    d->dma.fifo_rw = host->dma.fifo_rw;
    memset(d->core.pram_opcache, 0, sizeof(d->core.pram_opcache));
}

static void seed(MCPXAPUState *d, const MCPXAPUState *snapshot,
                 const DSPState *gp, const DSPState *ep,
                 const DSPState *gp_host, const DSPState *ep_host,
                 uint8_t *ram, const uint8_t *ram_snapshot)
{
    memcpy(ram, ram_snapshot, NC_RAM_RETAIL);
    memcpy(d->regs, snapshot->regs, sizeof(d->regs));
    d->regs[NV_PAPU_SECTL] &= ~NV_PAPU_SECTL_XCNTMODE;
    memcpy(d->gp.regs, snapshot->gp.regs, sizeof(d->gp.regs));
    memcpy(d->ep.regs, snapshot->ep.regs, sizeof(d->ep.regs));
    d->ep_frame_div = snapshot->ep_frame_div;
    d->gp.realtime = d->ep.realtime = true;
    restore_dsp(d->gp.dsp, gp, gp_host);
    restore_dsp(d->ep.dsp, ep, ep_host);
    memset(d->monitor.frame_buf, 0, sizeof(d->monitor.frame_buf));
}

static bool step(MCPXAPUState *d, frame_fn frame,
                 float bins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME], int16_t *output)
{
    frame(d, bins);
    bool completed = (d->ep_frame_div+1)%8 == 0;
    if (completed) {
        if (output) memcpy(output, d->monitor.frame_buf, sizeof(d->monitor.frame_buf));
        memset(d->monitor.frame_buf, 0, sizeof(d->monitor.frame_buf));
    }
    d->ep_frame_div++;
    return completed;
}

static double thread_seconds(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t)) die("thread clock unavailable");
    return t.tv_sec+t.tv_nsec*1e-9;
}

static void put16(FILE *f, uint16_t v)
{
    fputc(v & 255, f); fputc(v >> 8, f);
}

static void put32(FILE *f, uint32_t v)
{
    put16(f, v & 65535); put16(f, v >> 16);
}

static void write_wav(const char *path, const int16_t *pcm, size_t frames)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fwrite("RIFF", 1, 4, f); put32(f, 36+(uint32_t)frames*4);
    fwrite("WAVEfmt ", 1, 8, f); put32(f, 16); put16(f, 1); put16(f, 2);
    put32(f, 48000); put32(f, 192000); put16(f, 4); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, (uint32_t)frames*4);
    if (fwrite(pcm, 4, frames, f) != frames || fclose(f)) die("cannot write WAV");
}

static void timed_out(int signal_number)
{
    (void)signal_number;
    static const char message[] = "replay: DSP did not complete within 60 seconds\n";
    write(STDERR_FILENO, message, sizeof(message)-1);
    _Exit(124);
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s core.so symbols.json --init-only\n"
                        "       %s core.so symbols.json snapshot_directory output_prefix [--bin N] [--frequency HZ] [--tone-seconds N]\n", argv[0], argv[0]);
        return 2;
    }
    int selected_bin = -1;
    double frequency = 1000;
    double tone_seconds = .25;
    for (int i = 5; i < argc; i++) {
        if (!strcmp(argv[i], "--bin") && i+1 < argc) {
            selected_bin = atoi(argv[++i]);
            if (selected_bin < 0 || selected_bin >= NUM_MIXBINS) die("bin out of range");
        } else if (!strcmp(argv[i], "--frequency") && i+1 < argc) {
            frequency = strtod(argv[++i], NULL);
            if (!isfinite(frequency) || frequency <= 0 || frequency >= 24000) die("frequency out of range");
        } else if (!strcmp(argv[i], "--tone-seconds") && i+1 < argc) {
            tone_seconds = strtod(argv[++i], NULL);
            if (!isfinite(tone_seconds) || tone_seconds <= 0 || tone_seconds > 5) die("tone duration out of range");
        } else die("unknown replay option");
    }
    setenv("NC_APU_DSP", "1", 1);
    setenv("NC_APU_WORKERS", "1", 1);
    setenv("HAKUX_APU_STATS", "0", 1);
    unsetenv("NC_APU_GP_PROBE"); unsetenv("NC_APU_GP_PROBE_AT");
    signal(SIGALRM, timed_out);
    char *json = read_file(argv[2], 0);
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) die(dlerror());
    void *entry = dlsym(library, "retro_api_version");
    Dl_info info;
    if (!entry || !dladdr(entry, &info)) die("cannot locate loaded core base");
    uintptr_t base = (uintptr_t)info.dli_fbase;
    init_fn init = (init_fn)symbol(json, base, "nc_dev_apu_init");
    void_fn stop = (void_fn)symbol(json, base, "nc_dev_apu_stop");
    void_fn release = (void_fn)symbol(json, base, "nc_dev_apu_free");
    frame_fn frame = (frame_fn)symbol(json, base, "mcpx_apu_dsp_frame");
    MCPXAPUState **state_pointer = (MCPXAPUState **)symbol(json, base, "g_state");
    uint8_t *ram = calloc(1, NC_RAM_RETAIL);
    if (!ram) die("cannot allocate RAM");
    NcVm vm = {.phys=ram, .ram=ram, .ram_size=NC_RAM_RETAIL, .ram_fd=-1};
    alarm(60);
    init(&vm);
    MCPXAPUState *d = *state_pointer;
    if (!d || !d->gp.dsp || !d->ep.dsp) die("APU initialization failed");
    pthread_mutex_lock(&d->lock);
    d->regs[NV_PAPU_SECTL] &= ~NV_PAPU_SECTL_XCNTMODE;
    fprintf(stderr, "replay: base=%p APU=%zu DSP=%zu RAM=%u\n",
            info.dli_fbase, sizeof(*d), sizeof(DSPState), NC_RAM_RETAIL);
    if (argc == 4) {
        if (strcmp(argv[3], "--init-only")) die("expected --init-only");
        printf("{\"init\":\"PASS\",\"apu_size\":%zu,\"dsp_size\":%zu}\n", sizeof(*d), sizeof(DSPState));
    } else {
        MCPXAPUState *apu_snapshot = read_snapshot(argv[3], "apu.bin", sizeof(*d));
        DSPState *gp_snapshot = read_snapshot(argv[3], "gp.bin", sizeof(DSPState));
        DSPState *ep_snapshot = read_snapshot(argv[3], "ep.bin", sizeof(DSPState));
        uint8_t *ram_snapshot = read_snapshot(argv[3], "ram.bin", NC_RAM_RETAIL);
        DSPState *gp_host = malloc(sizeof(DSPState)), *ep_host = malloc(sizeof(DSPState));
        if (!gp_host || !ep_host) die("cannot preserve callback state");
        memcpy(gp_host, d->gp.dsp, sizeof(DSPState));
        memcpy(ep_host, d->ep.dsp, sizeof(DSPState));
        const unsigned bins_to_test[] = {0, 1, 10};
        for (unsigned kind = 0; kind < 3; kind++) {
            for (unsigned which = 0; which < (kind && selected_bin < 0 ? 3u : 1u); which++) {
                unsigned bin = kind && selected_bin >= 0 ? (unsigned)selected_bin : bins_to_test[which];
                seed(d, apu_snapshot, gp_snapshot, ep_snapshot, gp_host, ep_host, ram, ram_snapshot);
                float bins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = {{0}};
                alarm(60);
                double warm_start = thread_seconds();
                for (unsigned i = 0; i < 1500; i++) step(d, frame, bins, NULL);
                while (d->ep_frame_div%8) step(d, frame, bins, NULL);
                double warm_cpu = thread_seconds()-warm_start;
                size_t stimulus = kind == 2 ? (size_t)llround(tone_seconds*48000) : kind == 1 ? 1 : 0;
                size_t blocks = ((stimulus+48000+255)/256)*8;
                size_t capacity = blocks*NUM_SAMPLES_PER_FRAME;
                int16_t *pcm = calloc(capacity*2, sizeof(*pcm));
                if (!pcm) die("cannot allocate PCM");
                size_t output_frames = 0;
                double start = thread_seconds();
                for (size_t block = 0; block < blocks; block++) {
                    memset(bins, 0, sizeof(bins));
                    for (unsigned i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
                        size_t sample = block*NUM_SAMPLES_PER_FRAME+i;
                        if (sample < stimulus)
                            bins[bin][i] = kind == 1 ? .125f : .125f*sinf((float)(2*M_PI*frequency*sample/48000));
                    }
                    if (step(d, frame, bins, pcm+output_frames*2)) output_frames += 256;
                }
                double cpu = thread_seconds()-start;
                uint64_t hash = UINT64_C(0xcbf29ce484222325);
                const uint8_t *bytes = (const uint8_t *)pcm;
                for (size_t i = 0; i < output_frames*4; i++) hash = (hash^bytes[i])*UINT64_C(0x100000001b3);
                char path[4096];
                const char *name = kind == 2 ? "tone" : kind == 1 ? "impulse" : "silence";
                if (snprintf(path, sizeof(path), "%s-%s-bin%u.wav", argv[4], name, bin) >= (int)sizeof(path)) die("output path too long");
                write_wav(path, pcm, output_frames);
                printf("{\"case\":\"%s\",\"bin\":%u,\"frequency_hz\":%.1f,\"stimulus_frames\":%zu,\"frames\":%zu,\"seconds\":%.9f,"
                       "\"thread_cpu_seconds\":%.9f,\"warmup_cpu_seconds\":%.9f,\"pcm_fnv64\":\"%016" PRIx64 "\"}\n",
                       name, bin, frequency, stimulus, output_frames, output_frames/48000.0, cpu, warm_cpu, hash);
                fflush(stdout);
                free(pcm);
            }
        }
        free(gp_host); free(ep_host); free(apu_snapshot); free(gp_snapshot); free(ep_snapshot); free(ram_snapshot);
    }
    alarm(0);
    d->regs[NV_PAPU_IEN] = 0;
    d->regs[NV_PAPU_SECTL] &= ~NV_PAPU_SECTL_XCNTMODE;
    pthread_mutex_unlock(&d->lock);
    stop();
    release();
    free(ram); free(json);
    dlclose(library);
    return 0;
}
