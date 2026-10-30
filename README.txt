Validation of DOA3 stereo DSP processing on the Retroid Pocket Nova

The direct-tone.wav and effect-tone.wav files use the same generated 0.25-second, 1 kHz tone at 0.125 of full scale. The direct recording feeds mix bin 0; the effect recording feeds bin 10 (I3DL2). Each includes one second after the input stops. These are individual routing probes, not equal-volume before/after recordings. The effect response has a stereo decay after input ends. Very low residual values near one PCM step are not a measurable reverb tail.

The replay starts from a private capture at mcpx_apu_dsp_frame entry, with the game's GP/EP programs and memory. Each case restores that state and guest RAM, clears host opcode caches, then runs one second of silence before the stimulus. Private game snapshots are not included here. The helper preserves host callbacks and parks the real APU thread while manually running DSP frames.

Seven silence, impulse and tone recordings are byte-identical across the original interpreter, the inlined loop and the cached handler candidate. Frequency-response.json probes 100 Hz, 1 kHz and 8 kHz: direct speaker routes are flat; the effect route damps high frequencies. This is the captured title-screen program state, not a measurement of every arena or sound.

Nova-comparison.json uses an old/new/new/old sequence on CPU 4, with the same state and input. The CPU median frequency was 2.8032 GHz in all four runs. CPU-seconds per audio-second: original 0.604781, optimized 0.542469, a 10.303% reduction. PCM hashes match. The host results are separate Apple-host Linux AArch64 measurements and should not be called Nova CPU utilization.

Voice-comparison.json aligns Gen Fu's same intro utterance in the original Xbox video (https://www.youtube.com/watch?v=fPzoCfkys14, around 50:25) and the Nova recording. The best one-second waveform correlation is 0.850. No broad treble loss was detected in this sample. Different background music, recording paths and lower high-frequency coherence limit the comparison; it does not establish complete audio equivalence or resolve perceived muffling.

The source PR also has a deterministic idle-voice race test (old code fails, guarded code passes), DSP instruction rewrite/bootstrap tests with deliberately stale-handler negative runs, and core-option boot/reset/env-override tests. All nine native/libretro CTest suites pass.

Replay tools (Linux AArch64, from a matching hakuX source checkout):
  cc -O2 -D_GNU_SOURCE -I. apu-dsp-replay.c -ldl -lm -pthread -o apu-dsp-replay
  python3 elf-symbols.py unstripped-core.so symbols.json
  ./apu-dsp-replay unstripped-core.so symbols.json SNAPSHOT_DIRECTORY output-prefix
Optional probes: --bin N --frequency HZ --tone-seconds SECONDS.
The snapshot files are apu.bin, gp.bin, ep.bin and ram.bin, with the exact structs of that source checkout. Use the matching unstripped core for symbol offsets.

DSP56300 JIT validation

The JIT port uses the same seven replay cases. All seven host WAV files are byte-identical to the optimized C interpreter's files. nova-jit-comparison.json records a separate paired C/JIT/JIT/C test using one core with NC_APU_DSP_JIT=0 or 1, CPU4 affinity, and the same captured state and input. Compilation and one second of silence warmup per case are excluded from the timed output. Mean CPU-seconds per audio-second are 0.483115 for C and 0.351026 for JIT: 27.341% less. All PCM hashes match. CPU median frequency is 2.8032 GHz for each run; one JIT run briefly reached 2.592 GHz. The earlier 10.3% interpreter result was a separate session and must not be combined with this measurement as if it were a matched three-way test. This measures a captured title-screen effects program, not every game's audio workload or whole-system power.

The live temporary JIT core on the Nova logs GP JIT and EP JIT, with no audio underruns in the observed session. The device owner played a fight and reported voices, effects and controls behaving correctly. The frame after that test shows the winner screen. This is a successful play test, not a broad compatibility guarantee.

The new apu-jit-smoke suite checks compiled shared X/mixbuffer aliases, Y RAM, rewritten P code, DMA uploads and completion polling, bootstrap/reset, arithmetic, frame completion, WAIT/STOP and bounded GP/EP execution, real APU stereo routing, exact JIT state restoration and pre-JIT state loading. Deliberately disabling P-write, DMA-upload or bootstrap invalidation makes the corresponding execution checks fail. Omitting full JIT snapshot restoration makes both GP and EP exact-state checks fail. The restored implementation passes. The final shared core loads with only 25 retro_* exports and remains within GLIBC_2.38 and GLIBCXX_3.4.31.

JIT replay helper build (from the matching JIT-enabled source checkout):
  cc -O2 -DNC_HAVE_DSP_JIT=1 -I. -Ibuild/linux/dsp-jit/include apu-dsp-replay-jit.c -ldl -lm -pthread -o apu-dsp-replay-jit
  python3 elf-symbols-jit.py unstripped-core.so symbols-jit.json
  NC_APU_DSP_JIT=1 ./apu-dsp-replay-jit unstripped-core.so symbols-jit.json SNAPSHOT_DIRECTORY output-prefix
Use NC_APU_DSP_JIT=0 for the C interpreter. The legacy DSP snapshots are exactly 80136 bytes each; the helper preserves fresh host/JIT pointers and converts the old CPU state through the actual core's restore function. Private game RAM and DSP programs remain excluded.
