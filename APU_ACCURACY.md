# APU accuracy: missing and inaccurate features

An audit of the audio emulation (`native/dev/apu/`, the MCPX APU, and
`native/dev/aci.c`, its AC'97 controller) against the Xbox, made on
2026-10-30 at commit `adf2abaa20`. Rows are ranked by how audible they are,
most audible first. Line numbers refer to that commit; paths are under
`native/dev/apu/` unless given.

The APU is xemu's: a voice processor (VP) that decodes, resamples, filters
and mixes up to 256 voices into 32 mix bins, and two DSP56300s, the global
processor (GP), which runs DirectSound's effects and mixing program, and the
encode processor (EP), which makes the outputs. By default hakuX runs the
GP for only part of each frame (`use_dsp` off, as in xemu), and the VP mixes
the output itself; `NC_APU_DSP=1` runs both DSPs as the Xbox does.

No row has been checked against an Xbox recording. Rows 1-4 are the likely
causes of issue #44 (Halo and Ninja Gaiden Black sound muted and off-centre).

| # | Part | Xbox | Current state | Audible effect | Fix | Status |
|---|---|---|---|---|---|---|
| 1 | **Final mix** (default, DSP off) | The GP program mixes the VP's mix bins to the output: front, centre, LFE, rear, the 3D bins, effect sends. | The VP mixes each voice once at its **loudest** bin volume, equally into left and right (`vp/vp.c:2029`). Per-speaker volumes are ignored. | **Off-centre:** 2D sounds lose their panning; centre, rear and LFE have no placement of their own. | Pan by the bin volumes: FL/FR/BL/BR to left/right, centre and LFE to both at -3 dB. Medium. | #44 |
| 2 | **Headroom** | DirectSound gives mix bins a headroom shift (2^n down, against clipping); the GP program scales them back up. | The default mix divides by the bin's 2^n (`vp/vp.c:2055`) and never restores it. | **Muted:** sounds on bins with headroom are 6, 12 or 18 dB quieter, often the 3D ones. | Don't divide in the default mix. Small. | #44 |
| 3 | **3D audio (HRTF)** | A 31-tap HRTF filter and interaural delay per 3D voice into the crosstalk bins; the GP program cancels crosstalk and mixes for speakers. | The filter is implemented (`vp/hrtf.h`, `vp/vp.c:1989`), but its two-ear output goes straight to the speakers by default. Parameter smoothing is an approximation (`vp/hrtf.h:93`). | **Off-centre, coloured:** a headphone-style image on speakers, unprocessed. | Right with the DSP; without it, fold the 3D output down, or `NC_APU_HRTF=0`. | #44 |
| 4 | **Resampling** (pitch) | The VP converts every voice to 48 kHz; its interpolation isn't documented. | **Linear** interpolation, a fast replacement for xemu's libsamplerate (`vp/resample.c:12`). | **Muffled:** linear interpolation cuts the top of a 22 kHz sound by up to ~8 dB (-1.8 dB at a quarter of the source rate), and aliases. | Higher-order interpolation (4-point cubic or a short sinc), NEON. Medium. | #44 |
| 5 | **GP DSP** (effects, mixing) | A 160 MHz DSP56300 runs DirectSound's image: I3DL2 reverb, crosstalk, the mix. | xemu's interpreter. Off by default: 1000 cycles a frame (`dsp/gp_ep.c:499`), or up to a frame's worth while a CPU thread waits on it (`GP_FRAME_CYCLES`, `dsp/gp_ep.c:462`). Some DMA details are unknown (FIXMEs in `dsp/dsp_dma.c`). | **Dry:** no reverb or environment effects by default. | Default-on if the Nova can afford it (unmeasured); a DSP JIT is large work. | Open |
| 6 | **EP DSP** (outputs) | Encodes Dolby Digital for S/PDIF and feeds the analog output. | Runs only with `NC_APU_DSP=1`; the output is then its FIFO 0, or the GP's buffer when the EP is off. | None by default. | With row 5. | Open |
| 7 | **Per-voice filters** | DLS2 low-pass, parametric EQ, and I3DL2 occlusion / obstruction filtering for 3D voices. | The DLS2 low-pass only (`vp/vp.c:1956`); ParaEQ and the I3DL2 voice filters are missing (`vp/vp.c:1993`). | Sounds behind walls aren't muffled; equalised sounds aren't shaped. | Medium. | Open |
| 8 | **Filter-envelope modulation** | The filter envelope modulates pitch and filter cutoff. | Pitch: yes (`vp/vp.c:1754`). Cutoff: missing (`vp/vp.c:1966`). | Filter sweeps on music and effects don't sweep. | Small. | Open |
| 9 | **Volume envelope shapes** | Delay, attack, hold, decay, sustain, release in the hardware's curves. | Implemented; decay and release curves are approximations (FIXMEs in `vp/vp.c:1142-1253`). | Fades a little off in shape and length. | Needs hardware measurements. | Open |
| 10 | **Volume changes** | Target volumes (`TAR_VOL`), which the name suggests the hardware ramps towards. | Applied immediately, each 32-sample frame; no current-volume state. | Possible zipper noise on fast volume changes. | Small, if the hardware ramps. | Open |
| 11 | **Phase-inverted bins** (6, 7) | A phase flip for matrix-surround encoding. | Not applied (`vp/vp.c:1913`). | Surround-encoded rear sounds collapse in stereo. | Small. | Open |
| 12 | **AC'97 PCM out** | Programs can play PCM through the AC'97 controller directly (nxdk homebrew, some applications). | The controller is emulated, but its PCM out is consumed and **dropped** (`native/dev/aci.c:8`); S/PDIF out doesn't advance. | Homebrew that plays through AC'97 is silent. | Mix it into the output. Small to medium. | Open |
| 13 | **Voice formats** | PCM 8/16/24/32-bit and Xbox ADPCM, mono and stereo, loops, streams, linked and multipass voices. | All implemented. The ADPCM block size is uncertain, 64 or 65 (`apu_regs.h:336`). | None known. | - | Done |
| 14 | **Voice notifications** | Status and position reports to DirectSound. | Mostly done; some flags aren't set (`vp/vp.c:134`). | Rare stream stalls. | Small. | Open |
| 15 | **Console audio mode** | Stereo, mono or surround, and the Dolby Digital / DTS flags, in the EEPROM. | Never written: 0, stereo without Dolby Digital (`native/kernel/ex.c:101`, offset 0x98). | Right for the Nova's speakers. | - | Done |
| 16 | **Output** | 48 kHz. | 48 kHz 16-bit stereo to RetroArch, with an optional volume limit. | None. | - | Done |

## To verify before changing

**The DSP's cost.** `NC_APU_DSP=1` runs the GP and EP programs to
completion every frame, which puts rows 1-3 right as the Xbox does. Whether
the Nova keeps full speed with it, and how much CPU it costs, hasn't been
measured. Compare Halo with and without it, by ear and by frame rate,
before choosing between the DSP by default and fixing the default mix.

## Suggested order

1. The A/B above, on the Nova.
2. If the DSP stays off by default: rows 2, 1 and 4, the cheapest fixes to
   the default mix, in that order.
3. Row 12 for homebrew; rows 7 and 8 for filters.
