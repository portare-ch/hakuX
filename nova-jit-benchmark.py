import json, os, pathlib, statistics, subprocess, time
base=pathlib.Path('/tmp/hakux-doa3-debug')
results=[]
for label,engine in [('c2','0'),('j1','1'),('j2','1'),('c3','0')]:
    env=dict(os.environ,NC_APU_DSP_JIT=engine)
    with (base/f'jit-bench-{label}.jsonl').open('w') as out, (base/f'jit-bench-{label}.log').open('w') as err:
        p=subprocess.Popen([str(base/'hakux-dsp-replay-jit'),str(base/'hakux-dsp-jit-unstripped.so'),str(base/'hakux-dsp-jit-symbols.json'),str(base/'dsp-snapshot-title'),str(base/f'jit-bench-{label}')],env=env,stdout=out,stderr=err)
        freq=[]
        while p.poll() is None:
            freq.append(int(pathlib.Path('/sys/devices/system/cpu/cpu4/cpufreq/scaling_cur_freq').read_text()))
            time.sleep(.05)
        assert p.returncode == 0, (label,p.returncode)
    rows=[json.loads(x) for x in (base/f'jit-bench-{label}.jsonl').read_text().splitlines()]
    summary={'label':label,'engine':rows[0]['engine'],'cpu_seconds_per_audio_second':sum(x['thread_cpu_seconds'] for x in rows)/sum(x['seconds'] for x in rows),'frequency_khz_min':min(freq),'frequency_khz_median':statistics.median(freq),'frequency_khz_max':max(freq),'cases':rows}
    results.append(summary)
    print(json.dumps(summary),flush=True)
(base/'jit-benchmark.json').write_text(json.dumps(results,indent=2)+'\n')
