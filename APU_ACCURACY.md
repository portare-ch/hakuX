# APU accuracy: missing and inaccurate features

A source audit of the audio emulation (`native/dev/apu/`, the MCPX APU, and
`native/dev/aci.c`, its AC'97 controller), made on 2026-10-30 at commit
`be0ff4cd64` and updated with the first stereo batch (branch
`apu-stereo-first-run`) and the resampler (branch `apu-resample-sinc`). It reads the code against what is known of the
Xbox's audio; no row has been compared with an Xbox recording, so "Xbox"
below is the documented or inferred behaviour, not a measurement. Line
numbers refer to `be0ff4cd64`; paths are under `native/dev/apu/` unless
given.

The APU is xemu's: a voice processor (VP) that decodes, resamples, filters
and mixes up to 256 voices into 32 mix bins, and two DSP56300s, the global
processor (GP), which runs DirectSound's effects and mixing program, and the
encode processor (EP), which makes the outputs. By default (`use_dsp` off,
as in xemu) the VP mixes the output itself (the monitor point `MON_VP`) and
the DSPs get limited interpreter time; `NC_APU_DSP=1` runs them to
completion and takes their output instead.

Rows are in implementation order: what the delivered batches cover first,
then what needs evidence before it can be implemented correctly. Rows 1, 2,
3 and 4 are candidate causes of issue #44 (Halo and Ninja Gaiden Black muted
and off-centre); none has been isolated in those games.

| # | Part | Xbox | Current state | Effect | Next | Status |
|---|---|---|---|---|---|---|
| 1 | **Default mix: panning** | The GP program mixes the VP's mix bins to the output; DirectSound's ordinary 2D sounds go to the front-left and front-right bins (0 and 1) by their sends. | Ordinary 2D stereo is mixed by its sends: a 2D voice outside the multipass groups whose audible sends all go to bin 0 or 1 adds each send's `ea x attenuation / 2^headroom` to its own speaker (`vp/stereo_route.h`). Every other voice keeps the monitor's mix: its loudest send's gain on both channels (`vp/vp.c:2029`). | Ordinary 2D sounds keep their left/right placement. 3D voices, effect sends, centre / rear / LFE and multipass groups are still unpanned. | A downmix policy for centre, rear and LFE (weights, bass management, no double counting), then 3D routing (row 3). | Partly done (first batch) |
| 2 | **Headroom** | DirectSound gives mix bins a headroom shift (2^n down); what the guest's GP program does with it afterwards is not established for every program. | Kept: the default mix divides by each bin's 2^n (`vp/vp.c:2055`, and `vp/stereo_route.h`), with nothing restoring it. | Sounds on bins with headroom are 6, 12 or 18 dB quieter than without it; whether they are on an Xbox is the open question. | A defined DSP mix reference (an image or measurements mapping bins to final stereo gain) before compensating. Not a universal gain fix: removing it changes levels and clipping. | Open (needs evidence) |
| 3 | **3D audio (HRTF)** | A 31-tap HRTF filter and an interaural delay per 3D voice into the HRTF submixes; the guest's GP program processes those for the speakers. | The filter is implemented (`vp/hrtf.h`, `vp/vp.c:1989`); its output goes to the monitor's unpanned mix by default. A reused voice now starts its filter from silence (`hrtf_filter_clear_history` at `VOICE_ON`; before, its previous sound's tail played into the new one). Per-ear coefficient normalization and the parameter smoothing are approximations (`vp/hrtf.h:68`, `:93`). On the mix-bin path a mono voice's right-ear output is lost (`mix_one_bin` takes channel `slot % 1`). | A headphone-style image on speakers, unpanned. Folding the ears together is not a fix (it cancels and loses the ear difference); `NC_APU_HRTF=0` is a diagnostic. | Coefficient scale, ear-to-submix mapping for mono voices, speaker processing: each needs reference coefficients or vectors. | History done (first batch); rest open |
| 4 | **Resampling** (pitch) | The VP converts every voice to 48 kHz; its interpolation isn't documented. | By default a band-limited sinc: 32 Kaiser-windowed taps through NEON, its cutoff at the input's Nyquist, stretched to the output's for a voice pitched past 48 kHz (`vp/resample_sinc.c`; AArch64, elsewhere linear). Linear interpolation, what the core had before, as the `hakux_audio_resampler` option (`vp/resample.c`). Host measurements for 22.05 kHz input: sinc 0.00 dB at 1, 8 and 9 kHz, -0.17 dB at 10 kHz; linear -0.06, -3.94 and -6.34 dB. A 30 kHz tone from 96 kHz input: sinc -126 dB, linear unattenuated. | Sinc: none known. Its filter reads 16 input frames ahead (up to 132 for a voice pitched past 48 kHz), so a voice's guest-visible position leads what is heard by 32 frames, as with linear, and a 22.05 kHz one-shot voice's last 2.7 ms aren't heard (linear's last 2 ms). Linear: high frequencies dulled, downsampled voices alias. | Not a claim about the Xbox's interpolation. On the Nova a sinc voice costs 0.37 us a frame on the X3, 0.75 on an A715 and 2.3 on an A510 (linear 0.07, 0.09 and 0.51); about 4 times that for a voice pitched an octave past 48 kHz. | Done (second run) |
| 5 | **GP DSP** (effects, mixing) | A 160 MHz DSP56300 runs DirectSound's image: I3DL2 reverb, crosstalk, the mix. | Execution: xemu's interpreter, 1000 cycles a frame by default (`dsp/gp_ep.c:499`), up to a hardware frame's worth while a CPU thread waits on it (`GP_FRAME_CYCLES`, `dsp/gp_ep.c:462`), to completion with `NC_APU_DSP=1`. Output: its result is used only with `NC_APU_DSP=1` (and no EP). DMA details are unknown (FIXMEs in `dsp/dsp_dma.c`). | No reverb or environment effects in the default output. The full-DSP path is an experimental reference candidate, not a correctness oracle: it has its own interpreter, DMA, routing and timing uncertainties. | Bounded execution and fault handling, guest-program fixtures, and Nova performance data before any default change. | Open |
| 6 | **EP DSP** (outputs) | Encodes Dolby Digital for S/PDIF and feeds the analog output. | Execution: an enabled EP runs 1000 cycles every 8th frame by default, to completion with `NC_APU_DSP=1`; it is not exclusive to that switch. Output: its FIFO 0 is the output only with `NC_APU_DSP=1`. | None in the default output. | With row 5. | Open |
| 7 | **Libretro output** | - | The consumer now removes from its ring only the frames RetroArch's batch callback accepted; the rest are offered again first, in order, and a refusing frontend backs the ring up (the producer waits at its high watermark) instead of losing them (`native/libretro/audio.c`). Before, a call whose frames the frontend took only part of, or none of, dropped the rest (800 frames of a refused period). | No frames lost when the frontend's buffer is full. Not a cause of steady muffling or a channel shift. | - | Done (first batch) |
| 8 | **Per-voice filters** | DLS2 low-pass, parametric EQ, and I3DL2 occlusion / obstruction filtering for 3D voices. | The DLS2 low-pass only (`vp/vp.c:1956`); ParaEQ and the I3DL2 voice filters are missing (`vp/vp.c:1993`). | Sounds behind walls aren't muffled; equalised sounds aren't shaped. | Register transfer functions or executable reference vectors; the mode names don't establish coefficients or update rules. | Open (needs evidence) |
| 9 | **Filter-envelope modulation** | The filter envelope modulates pitch and filter cutoff. | Pitch: yes (`vp/vp.c:1754`). Cutoff: missing (`vp/vp.c:1966`). | Filter sweeps don't sweep. | The signed scaling and transfer function from a primary reference or hardware vectors, not the FIXME. | Open (needs evidence) |
| 10 | **Volume envelope shapes** | Delay, attack, hold, decay, sustain, release in the hardware's curves. | Implemented; decay and release curves are approximations (FIXMEs in `vp/vp.c:1142-1253`). | Fades a little off in shape and length. | Reference curves or hardware vectors. | Open (needs evidence) |
| 11 | **Volume changes** | Target volumes (`TAR_VOL`); whether the hardware ramps towards them isn't established by the name. | Applied each 32-sample frame; no current-volume state. | Possible zipper noise on fast changes. | Evidence of a ramp first. | Open (needs evidence) |
| 12 | **Phase negation** | Phase-inversion flags for voice send slots 6 and 7 (matrix-surround encoding). These are a voice's send slots, not global mix bins 6 and 7. | Not applied (`vp/vp.c:1913`). | Unverified. | The control bits and their routing semantics, and isolated phase tests, before implementing. | Open (needs evidence) |
| 13 | **AC'97 PCM out** | Programs can play PCM through the AC'97 controller directly (nxdk homebrew, some applications). | Emulated, but its PCM out is consumed and dropped (`native/dev/aci.c:8`); S/PDIF out doesn't advance. | Homebrew that plays through AC'97 is silent. | A synchronized stereo sink, descriptor and interrupt tests, and a defined interaction with the APU's output; nxdk's driver also waits on digital descriptor completion, so advancing analog PCM alone may not complete its queue. | Open |
| 14 | **Voice formats** | PCM 8/16/24/32-bit and Xbox ADPCM, mono and stereo, loops, streams, linked and multipass voices. | Implemented. The ADPCM block size is uncertain, 64 or 65 (`apu_regs.h:336`). | None known. | Validation against hardware. | Implemented, validation outstanding |
| 15 | **Voice notifications** | Status and position reports to DirectSound. | Mostly done; some flags aren't set (`vp/vp.c:134`). | Rare stream stalls. | - | Open |
| 16 | **Console audio mode** | Stereo, mono or surround, and the Dolby Digital / DTS flags, in the EEPROM. | A new EEPROM (`<hdd>/eeprom.bin`) starts at 0: stereo, no Dolby Digital (`native/kernel/ex.c`, offset 0x98). A persisted one is loaded as it is, and titles change it through `ExSaveNonVolatileSetting`. | Right for the Nova's speakers on a new EEPROM. | Leave the user's EEPROM alone; don't advertise multichannel output that isn't produced. | As intended |
| 17 | **Output format** | 48 kHz. | 48 kHz 16-bit stereo to RetroArch, with an optional volume limit. | None. | - | As intended |

## Delivered

**First batch** (no listening tests needed: each follows from explicit
routing, filter-state isolation or the libretro callback contract):

- **HRTF history** (row 3): `hrtf_filter_clear_history` at `VOICE_ON`, and
  for the muted-voice path's resume; coefficients and delays stay.
- **Ordinary 2D stereo panning** (row 1): `vp/stereo_route.h`, called from
  `voice_process`; the monitor's mix is unchanged for every other voice, and
  the full-DSP path doesn't use it.
- **Libretro short writes** (row 7).
- Tests: `tests/hle/test_apu_hrtf.c`, `test_apu_stereo.c` and
  `test_lr_audio.c` (host, also under ASan and UBSan), and
  `tests/native/linux/apu_stereo.c` (`apu-stereo-smoke`, through voice
  processing in the Linux build).

Unchanged by it: the DSP, HRTF, headroom and resampler defaults, voice
positions and notifications, the EEPROM, the frontend frame rate, the worker
count, and the savestate layout (a state load still resets the filters
through `mcpx_apu_vp_reset`).

**Second run** (row 4):

- **Sinc resampler** (`vp/resample_sinc.c`): a 32-tap Kaiser-windowed sinc
  (beta 6) in float, from a table of 128 phases interpolated between, its
  taps through NEON; the filter stretched up to 8 times for voices pitched
  past 48 kHz. The default on AArch64; `linear` stays as the
  `hakux_audio_resampler` core option and `NC_APU_RESAMPLER`, and is what a
  core for any other CPU uses.
- It meets the spec's criteria, 10 kHz included, where libsamplerate's
  `SRC_SINC_MEDIUM_QUALITY` (tried first) left 10 kHz at -3.25 dB and
  `SRC_SINC_FASTEST` at -22.8 dB. libsamplerate's double-precision filter
  cost 3.3 us a voice-frame on the Nova's X3 and 37 on an A510, 14 to 23
  times linear; this one is 2 to 4 times linear.
- A voice's converter is made at its first `VOICE_ON` (5 KB for sinc, 1 KB
  for linear) and kept for its later sounds: none in a frame.
- Tests: `tests/hle/test_apu_resample.c` (host: response, aliasing, unity,
  chunking, pitch changes, the pitch's whole range, reset, lookahead, step
  and tail; `bench` a voice-frame's cost by rate) and
  `tests/native/linux/apu_resample.c` (`apu-resample-smoke`, both qualities
  through voice processing: levels and loops, pitch changes, a voice's end,
  reuse, a starved stream, ADPCM loops; `bench` for throughput and
  allocations).

## Next

1. **Listening:** the sinc resampler in the #44 titles on the Nova.
2. **Evidence-driven:** rows 2, 3 (rest), 5, 8-12 and 13, each after its
   semantics are established by a reference, executable vectors or
   recordings. Issue #44 stays open until its cause is isolated in the
   games.
