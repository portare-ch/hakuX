# PortareOS handoff — 2026-10-24

Issue: https://github.com/portare-ch/hakuX-lr/issues/2
Specification: [SPEC.md](SPEC.md)
Build/run instructions: [native/README.linux.md](native/README.linux.md)

## State

Day one, the Linux headless milestone, is integrated: PR #3 merged as
`8f2937308e`, and issue #2 is closed. Day two, the game on the Nova's panel,
is integrated: PR #4 merged as `8c633edfd9`. Day three, sound, merged in
PR #5 as `1a058f38dd`, and day four, controls, in PR #6 as `8d32ef4afd`.
Day five, the launcher, merged in PR #7 as `93bad3d7fe`; the libretro core
and Vulkan handoff merged in PR #10 as `100e9fadef`.

Since 0.4.0 (2026-10-25), PortareOS uses the RetroArch core, with disk and
caches in `/storage/saves/xbox`. The untested Linux standalone was removed
after PortareOS stopped shipping it (portare-ch/portareos#754); its sections
below are historical. The core supports unload/reload, reset, game relaunch
and video driver rebuilds within RetroArch (M2). Ports 1–4 map to players
1–4. Rumble is available from 0.8.0; save states remain unsupported.
For current options and usage, see [native/README.linux.md](native/README.linux.md).
Issue #8's display checks passed on the Nova on 2026-10-25: through
RetroArch, Burnout 3 was shown for two refreshes per frame for 10 minutes
(see "Issue #8" below).
Android validation is outside this fork's scope and was not performed.

The working checkout began at upstream `98b935311184a11b80b6c2a2993536475ca4c0d3`.
That is the FEX submodule addition immediately after the fork's original
`254507d7083a2437725b09c881ebe105c829125c`. The fork now contains both the
FEX addition (merged in PR #1) and the SPEC commit. The issue branch is based
on the fork's `master` at `10957466755614414f5d21fea43c1cb134980729`.
The implementation uses the exact FEX revision required by the SPEC and the
documented graphics dependency revisions. Full source/submodule hashes are
in `tools/native/linux-deps.json`.

The sections below record each milestone's implementation and test environment
at the time.

## Day one: headless core — 2026-10-24

### Implemented

- Platform-aware build/linking and an explicitly selected Linux diagnostic
  audio backend. Android remains the default graphical app configuration.
- Linux property reads for the formerly unguarded Android property calls.
- Conditional Android AHB definitions, extension requirements and imports.
  The Linux renderer retains the ordinary memory-copy fallback. Android
  surface presentation is excluded from the Linux build.
- Paced silent audio with bounded queue accounting, pause and producer
  wakeups. The original emulated APU remains active.
- Explicit headless CLI with a caller-supplied data directory and existing
  boot, fault reporting and timeout behavior.
- CPU/memory/MMIO tests, paced-audio tests and generated synthetic XBE/XISO
  fixtures that exercise successful HLT and failing/timeout paths.
- Vulkan probe using the renderer's actual capability checks.
- Host-aware state size checks for the two records containing differently
  sized Bionic/glibc mutexes. Stored fields and layout hashes are unchanged.
  `statelock.py --sizes` understands both ABI sizes and Clang's assembly
  comments; all 45 size probes reported zero updates.
- Small existing host-test portability fixes: libm linkage, the missing
  sink initializer field and parentheses around intentional shader string
  concatenation. The strict warning-as-error host suite passes.

### Verified results

Build environment: AArch64 Debian container, base image
`debian:stable-slim@sha256:a717a33469731894e77e912ea7d714e27c2dc38ee6554fd1a3102e92999cfc66`.
Clang/LLD 19.1.7, CMake 3.31.6, Ninja 1.12.1, Python 3.13.5, glibc 2.41.
`tools/native/Dockerfile.linux` builds a smaller image with the required tools.

Nova: SM8550 / Snapdragon 8 Gen 2, AArch64, 4096-byte host pages.
PortareOS nightly `20261023`, build `268b91af2b4c24ca953e6b18218934d3edea0677`,
Linux 7.3.0, glibc 2.41. The Debian-built executable's libraries resolved
on this image: libz, libstdc++, libm, libgcc_s and libc, plus the AArch64
loader. There are no Android runtime dependencies.

| Check | Container | Nova |
| --- | --- | --- |
| Real NcCpu/FEX execution, branch/register assertions and system-operation exit | Pass | Pass |
| Kernel initialization, memfd RAM and alias visibility | Pass | Pass |
| Translated dirty-page recovery through `nc_core_fault` | Pass | Pass |
| Translated MMIO read/write recovery through `nc_core_fault` | Pass | Pass |
| Wrong expected result exits 1; unexpected host fault, declined by `nc_core_fault` after the CPU ran, exits 3 | Pass | Pass |
| Paced silent output, full-ring wakeup, pause/resume | Pass | Pass |
| Missing disc exits 2; malformed disc exits 1 | Pass | Pass |
| Synthetic XBE HLT exits 0; infinite-loop XBE times out with exit 5 | Pass | Pass |
| Existing pure HLE tests | 14/14 pass | Not rerun separately |
| Fresh build in the minimal documented image | Build and all 3 CTest groups pass | Same source already tested |
| State schema lock | 16 sections match; 45 sizes unchanged | Same compiled assertions |
| Portable Vulkan capability floor and device creation | Not a hardware substitute | Pass |
| Android native build | Not performed; waived by owner | Not applicable |

The initial Nova audio measurement consumed 57600 bytes over approximately
300 ms, matching 48 kHz stereo signed 16-bit output; a later run consumed
55680 bytes and passed the timing bounds. No commercial game was tested in
this milestone.

GPU: Turnip Adreno 740, Vulkan 1.4.363, Mesa 26.3.0-rc1. Every portable
requirement in `vkdev.c` passed. Optional GPL, EDS3, descriptor buffer,
maintenance5 and dynamic-rendering local read were available. The driver
reported dma-buf and external-memory-fd support, but no external-memory-host
or AHB support. No import path was implemented or validated.

The installed SDL3 is 3.4.16. Its
[KMSDRM Vulkan implementation](https://github.com/libsdl-org/SDL/blob/release-3.4.16/src/video/kmsdrm/SDL_kmsdrmvulkan.c)
requests `VK_KHR_surface` and `VK_KHR_display`; both are present in the
Nova's instance-extension report. Surface creation, presentation, display
ownership and frame pacing were outside this milestone; the headless runner
reported `swapchain=0`.

### Review — 2026-10-24

PR #3 was reviewed against these results. One test gap was fixed: the
unexpected-host-fault case previously faulted before `nc_cpu_create`, so the
smoke handler exited without consulting `nc_core_fault`. It now faults from
host code after the translated checks pass, and `nc_core_fault` must decline
it; resuming it would loop until the timeout fails the test.

After the fix, the container passed `linux-build.sh` (3/3 CTest groups), the
14 HLE tests, the 16-section schema lock and the 45 unchanged record sizes.
Fresh stripped binaries in `/tmp/hakux-day1-review` on the Nova passed
`check.py` (exit 0, 57600 audio bytes in approximately 300 ms) and
`vulkan-probe` (exit 0, same GPU/driver); `ldd` resolved only the libraries
listed above. The device log is `build/evidence/nova-review.log`.

## Day two: the first frame on the panel — 2026-10-24

The renderer runs on Linux: `xboxrun --render` keeps the HLE GPU on without a
display and captures frames. The new `hakux` front end presents them on the
panel through SDL3's KMSDRM backend and the renderer's existing swapchain.

Implemented:

- `swapchain.c` presents into a platform window (`HleWindow`): Android's, or
  an SDL window, whose Vulkan surface SDL creates. Size and surface creation
  are the only platform code; the presenter is otherwise unchanged.
- `vkdev.c` enables the window system's instance extensions given by the
  front end (`hle_vk_set_window_extensions`). SDL's `vkGetInstanceProcAddr`
  is the renderer's loader (`nc_hle_set_vk_loader`), so both share one.
- `native/app/linux_main.c` (`hakux`): fullscreen window at the desktop
  mode, the core thread and host-fault handler, the present loop, the first
  gamepad as player 1 and an orderly exit. `present.c` and the gamepad
  mapping (`gamepad.c`, moved from `app_main.c`) are shared with Android.
  Choreographer vsync is Android-only: on Linux the emulated vblank runs from
  the host clock.
- SDL3 release 3.4.16 (PortareOS's) is pinned and built only to link.
- `kms-grab` reads back a linear scanout framebuffer as a PNG.

Results on the Nova (PortareOS nightly 20261023, Turnip / Mesa 26.3.0-rc1):

| Check | Result |
| --- | --- |
| `xboxrun --render`, BLACK (USA) `45410083` | Push-buffer mode at 30 fps; captures show the Criterion logo (12 s) and the title screen (20 s) |
| `xboxrun --render`, Burnout 3 (USA) `4541005b` | 59.9 fps; captures show the Crash Nav globe (18 s) and the in-engine attract race (36 s) |
| `hakux`, BLACK | KMSDRM 1280x960 at 120 Hz with `VK_KHR_surface` and `VK_KHR_display`; Turnip `swapchain=1`; swapchain 1280x960, 3 images, BGRA8; 640x480 scaled 2x bilinear; first title frame 0.4 s after start; 29.8 fps presented over 40 s; exit 0 |
| `hakux`, Burnout 3 | First title frame after 0.2 s; 59.7 fps presented |
| Panel during `hakux` | Backlight on; plane 48 scans out the swapchain's framebuffer (UBWC) on CRTC 108 at 1280x960 |
| SIGTERM / SIGKILL | Orderly exit 0 in 147 ms / process gone in 121 ms; the launcher then shows its own framebuffer again (`kms-grab`) |
| `check.py` (with the new `--render` case), `vulkan-probe` | Pass |
| Container: 3 CTest groups, 14 HLE tests, state lock | Pass; 16 sections and 45 sizes unchanged |
| Fresh build in the minimal documented image, SDL3 included | Build and all 3 CTest groups pass (`build/clean-build-day2.log`) |

The panel's pixels during the game were not read back: Turnip's display
swapchain is UBWC-compressed, and neither `TU_DEBUG=noubwc` nor
`MESA_VK_WSI_DEBUG=linear` made it linear. The owner then ran `hakux` on
the Nova and confirmed the game on the panel ("it works. beautiful"). A DualSense Edge paired with the Nova was the
gamepad; input was not exercised. Launching from the launcher itself, with
its DRM-master hand-over, was simulated by stopping and starting
`portarelauncher.service`.

Test images for repeated runs are in `/roms/xbox` on the Nova (the SD card's
storage partition, grown to 117 GiB by portare-ch/portareos#749). They are
the user's and are not to be deleted.

## Day three: sound — 2026-10-24

`hakux` plays the APU's output through SDL3's audio: PipeWire 1.6.8 on
PortareOS, to the default sink (the speaker).

- `native/dev/apu/out_sdl.c` follows `out.c` (AAudio): the same ring, fill
  and pause contract, a callback per device request, and SDL following the
  default device. With no device it drains the ring at 48 kHz and retries
  each second, so the APU never stalls.
- On Linux the APU library has no output: each executable brings one.
  `hakux` gets `out_sdl.c`; `xboxrun`, the tests and `vulkan-probe` keep
  `out_null.c`. `HAKUX_DIAGNOSTIC_AUDIO` is no longer required; ON makes
  `hakux` silent too.
- `hakux` initializes SDL audio on its main thread and names itself, so
  PipeWire shows a "hakuX" stream.
- `audio-sdl-smoke` runs the audio contract test against the SDL output.

Results:

| Check | Result |
| --- | --- |
| `audio-sdl-smoke`, Nova through PipeWire (speaker) | 57600 bytes in 300 ms (exactly 48 kHz), 3 of 3 runs |
| `audio-sdl-smoke`, container (SDL's dummy driver) | Pass with device-paced floors: the dummy driver drains 48000 bytes, a sixth slow |
| `hakux`, BLACK | SDL pipewire, speaker, 512-frame callbacks; PipeWire lists an active "hakuX" stream on both speaker channels; APU 1500 frames/s, about 94 callbacks/s, 0 short callbacks |
| Speaker monitor, 7 s recorded during BLACK | -35 to -17 dBFS RMS, peaks to 16924: game audio, not silence |
| `hakux`, Burnout 3, 40 s, ended by SIGKILL | 0 short callbacks in 40 s; APU busy 2-24 % after startup; 59.9 fps; the stream is gone after the kill |
| Owner listening on the Nova | "sounds beautiful" |
| `check.py`, `vulkan-probe`, 14 HLE tests, state lock | Pass |
| Fresh build in the minimal documented image | Build and all 4 CTest groups pass (`build/clean-build-day3.log`) |

In the first fresh build in the minimal image, `audio-sdl-smoke` crashed
once as it exited; 30 further runs of the old test did not repeat it. Two
causes fit, both in the test rather than in `hakux`: SDL's audio thread
still broadcasting to the test's stack-local condition variable after
`main` returned, or running during `exit()`'s static destructors. The test
now keeps them static, pauses the output and leaves by `_exit`, as `hakux`
does; it passed 40 of 40 direct runs and 10 of 10 CTest runs in that image.

Not tested: headphones (SDL should follow the default sink), Bluetooth
audio, a PipeWire restart while playing, and the app pause
(`nc_audio_set_paused`), which `hakux` does not use yet.

## Day four: controls — 2026-10-24

On PortareOS, InputPlumber grabs the Nova's built-in controls (`event3`,
"AYN Odin2 Gamepad") and its side keys (`event5`), and presents them as a
virtual DualSense Edge (`054c:0df2`, profile
`/usr/share/inputplumber/devices/02-retroid-pocket-nova.yaml`). That virtual
pad is the one with input; SDL may also find the grabbed raw device, which
stays silent.

- `hakux` opens every gamepad SDL reports and merges them all into player 1
  (`gamepad.c`'s rules: buttons or'ed, the furthest stick and trigger), so
  the built-in controls work with or without InputPlumber, and a pad plugged
  in plays too. Separate players for several pads are not done yet.
- The title's rumble goes to every pad, sent again each half second while
  it lasts (SDL's rumble times out after the second each send asks for).
- Home + Start quits, as every PortareOS emulator does (the Nova's Home is
  the virtual pad's guide button); the title does not see that Start. In
  the first play test, `hakux` only quit on a guide button held for a
  second, and with the launcher stopped nothing else handled Home + Start:
  the owner could not leave the game. When the launcher runs a game, it also
  closes one that does not quit on Home + Start; PortareOS's kill combo
  (`portsense`) is L1 + Select + Start for a process named with `set_kill`.
- `hakux` captures frames like `xboxrun` (`NC_CAPTURE_EVERY`, into
  `frames/` in its working directory) for scripted checks.

Results: the owner raced in Burnout 3 on the Nova with the built-in
controls: "controls work, pleasing for a first attempt". In the first four
minutes the emulated Duke registered 116 button changes; 59.9 fps with
three dips to 59.2-59.7; no short audio callbacks after the APU's first.
Over 327 s the title changed its rumble 190 times, all sent to the pad,
but the Nova did not vibrate ("rumble did not work"); the owner put it off
("not important for now"). After the fix, the owner left
Burnout 3 with Home + Start ("worked"): `hakux` logged `leaving (Home +
Start)`, exited 0, and the launcher returned.

## Day five: the launcher — 2026-10-24

The launcher (`portarelauncher`) lists a system from
`/usr/config/emulationstation/es_systems.cfg` when the system's directory
has a game with one of its extensions. For a game it drops the DRM master
and runs `runemu.sh <game> -P<system> --core=<core> --emulator=<emulator>`;
for a standalone emulator, `runemu.sh` runs `/usr/bin/start_<core>.sh
<game> <system>`, without the core's `-sa`.

- `tools/native/linux-package.sh` makes the PortareOS package's input from
  a clean checkout (see [native/README.linux.md](native/README.linux.md)).
  `hakux` no longer carries the build tree's RUNPATH.
- PortareOS, on the `hakux-xbox` branch of portare-ch/portareos (no PR
  yet): a `hakux-sa` package installs the release tarball and
  `start_hakux.sh`. On SM8550 hakuX becomes the Xbox system's default
  emulator, with xemu still selectable; the system lists `.iso` and `.chd`
  files, and `start_hakux.sh` hands a CHD to xemu. The other devices keep
  xemu.
- The launcher reads `es_systems.cfg` a line at a time: each `<emulator>`
  and `<core>` must be on its own line, as `xmlstarlet fo` writes the
  generated file. A hand-edited test copy with the whole `<emulators>`
  block on one line gave the launcher the emulator but no core, and it
  refused the game ("No core configured").

Results: on the Nova, the package's files were laid over `/usr/bin` and
the Xbox entry over `es_systems.cfg` (both until the next reboot). The
launcher listed Xbox with BLACK and Burnout 3, and the owner started
Burnout 3 from it ("it works now"): `runemu.sh`, then `start_hakux.sh`,
then `hakux <game> /storage/.config/hakux aspect=auto`, which created the
emulated hard disk and caches there. `runemu.sh` appends the emulator's
output to `/var/log/exec.log`. SDL's "Couldn't open /dev/input/event3"
lines there are the raw controls InputPlumber hides; the virtual pad was
player 1.

## Issue #8: the display through RetroArch — 2026-10-25

#8's checks ran on the Nova with the image of 2026-10-24 (`next`,
`248be686`), using the 0.2.1 release's core, bind-mounted over the image's
0.2.0 for the runs. Each game ran through `runemu.sh`, as the launcher
starts it, under portare-ch/distribution's `tools/display-trace`, which
records every vblank, flip, scanout and Mesa commit. Each was checked with
`tools/display-check --rate 59.94006`, then RetroArch was asked to quit
(SIGTERM). There was no input: Burnout 3 played its attract loop (the
Crash Nav globe and in-engine races), and BLACK its logo and title screen.

| Run | Panel | Frames | Shown for two refreshes, from 1 s | Checks |
| --- | --- | --- | --- | --- |
| Burnout 3, 10 min 19 s | 120.20 Hz mode, `VRR_ENABLED` 1 | 37096 | 37033 of 37034 | All but one: a 2.6 ms phase jump at 556.9 s, where a commit took 3.15 ms to reach the screen; that frame was still shown twice |
| Burnout 3, `xbox.vrr=0`, 60 s | 119.88 Hz mode, `VRR_ENABLED` 0 before, during and after | 3546 | 3493 of 3493 | All but the mean interval, which a 152 ms start-up stall in the first 0.4 s skews; from 1 s it is 16.6831 ms |
| BLACK, 2 min | 120.20 Hz mode, `VRR_ENABLED` 1 | 7125 | 7063 of 7063 | All |

- **No beat.** Burnout 3's mean display interval was 16.6830 ms, against
  1001/60 = 16.6833 ms. Measured against the content's clock, its
  per-minute medians stayed within 0.15 ms of each other. Its only two
  frames shown once came in the first 1.1 s. Eight presents came late, by
  at most 0.78 ms, and none cost a refresh.
- **No FIFO wait.** With VRR, `retro_run` took 1.7-1.9 ms on average, at
  most 4.6 ms after the first 10 s, and each frame reached the screen a
  median 0.11 ms after its target. On fixed refresh `retro_run` took
  7.8 ms, because RetroArch's present waits for the refresh, and each
  frame reached the screen 6.27 ms after its target.
- **Every game frame once.** Burnout 3 handed over 59.9 new frames per
  second in every 10 s window after the first; the longest gap between
  two was 17.6 ms. BLACK handed over 30.0, each one presented by two
  `retro_run` calls. That is four refreshes per game frame at 120 Hz, as
  #8 asked.
- **Pipelines.** Burnout 3 took all 115 of its pipelines from the
  prebuild, which uses the cache, and compiled none. BLACK started from an
  empty cache and compiled 7; the slowest took 6.2 ms.
- **Quit.** SIGTERM ended RetroArch within a second, through 0.2.1's
  context teardown ("vulkan: RetroArch destroyed the context"), and
  `runemu.sh` returned 0. After a run with VRR, `VRR_ENABLED` stays 1: the
  process ends before Mesa lets the CRTC go. The launcher runs VRR itself.

The traces and logs are in `build/evidence/issue-8/` on the development
host (untracked). The runs' files were removed from the Nova afterwards.

## Standalone milestone artifacts

- Source: this repository.
- Development container: `hakux-build`, mounted at `/src`.
- Pinned minimal image: `hakux-linux-day1:latest`.
- Dependencies: `build/deps`; verified build trees: `build/linux`,
  `build/linux-clean-verified` and `build/hle-tests`; stripped device copies:
  `build/deploy`. `build/linux-clean` retains the initial failed image test.
- Logs: `build/linux-checks.log`, `build/clean-build.log`,
  `build/hle-tests.log`, `build/statelock-sizes.log` and
  `build/evidence/nova-boundaries.log`. These generated files are untracked.
- Device diagnostics: `/tmp/hakux-day1`, the review run's
  `/tmp/hakux-day1-review` and day two's `/tmp/hakux-day2`, including
  binaries, generated fixture discs, captured frames and logs. Day two's
  device logs are in `build/evidence/` on the development host (untracked:
  they include guest code bytes). No personal games/saves or launcher configuration
  were changed. No emulator diagnostic process should remain running.
- Day five's test install: `/storage/hakux-test` holds the tarball, its
  files, an overlay upper directory mounted over `/usr/bin` (`hakux`,
  `start_hakux.sh`) and the Xbox `es_systems.cfg` bind-mounted over
  `/usr/config/emulationstation/es_systems.cfg`; the mounts go at reboot.
  The launcher's runs keep their data in `/storage/.config/hakux`, the
  package's data directory.
- Connect using the SSH identity and host saved by `nova-push`. The Nova
  must remain awake for reliable SSH access. Do not publish the key or
  device credentials.

## Remaining work

- Save states. In-process lifecycle (0.4.0) and players 2–4 (0.5.0) are
  implemented; see [LIBRETRO_SPEC.md](LIBRETRO_SPEC.md) for validation targets.
- Complete the long-run audio, pause/resume and relaunch checks in the
  libretro design (display checks, #8, are done). Rumble was added in 0.8.0.
- Check #11 on the Nova: a relaunching title, and a video driver rebuild
  mid-game. The relaunch has run only the harness's synthetic title. A
  quit already ends through the context teardown there (2026-10-25).
- CHD support: done, through libchdr behind the disc reader (`native/core/chd.c`).

Retain clean-build evidence and exact dependency pins when changing the core.

There is no evidence yet for performance relative to xemu, Linux zero-copy
sharing or reliable suspend/title relaunch.
