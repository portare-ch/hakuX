# PortareOS handoff — 2026-10-24

Issue: https://github.com/portare-ch/hakuX/issues/2
Specification: [SPEC.md](SPEC.md)
Build/run instructions: [native/README.linux.md](native/README.linux.md)

## State

Day one, the Linux headless milestone, is integrated: PR #3 merged as
`1a36787d61`, and issue #2 is closed. Day two, the game on the Nova's panel,
is integrated: PR #4 merged as `7109f3102f`. Day three adds sound; it is on
`portareos/day3-audio` (see below). The owner waived
Android regression validation on 2026-10-24: this fork targets PortareOS,
and Android compatibility is not an acceptance requirement. Android was not
tested.

The working checkout began at upstream `98b935311184a11b80b6c2a2993536475ca4c0d3`.
That is the FEX submodule addition immediately after the fork's original
`254507d7083a2437725b09c881ebe105c829125c`. The fork now contains both the
FEX addition (merged in PR #1) and the SPEC commit. The issue branch is based
on the fork's `master` at `10957466755614414f5d21fea43c1cb134980729`.
The implementation uses the exact FEX revision required by the SPEC and the
documented graphics dependency revisions. Full source/submodule hashes are
in `tools/native/linux-deps.json`.

## Implemented

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

## Verified results

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
55680 bytes and passed the timing bounds. No commercial game was tested.

GPU: Turnip Adreno 740, Vulkan 1.4.363, Mesa 26.3.0-rc1. Every portable
requirement in `vkdev.c` passed. Optional GPL, EDS3, descriptor buffer,
maintenance5 and dynamic-rendering local read were available. The driver
reported dma-buf and external-memory-fd support, but no external-memory-host
or AHB support. No import path was implemented or validated.

The installed SDL3 is 3.4.16. Its
[KMSDRM Vulkan implementation](https://github.com/libsdl-org/SDL/blob/release-3.4.16/src/video/kmsdrm/SDL_kmsdrmvulkan.c)
requests `VK_KHR_surface` and `VK_KHR_display`; both are present in the
Nova's instance-extension report. Surface creation, presentation, display
ownership and frame pacing are still untested. The current Linux core
intentionally reports `swapchain=0`.

## Review — 2026-10-24

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
Over 327 s the title changed its rumble 190 times, all sent to the pad;
whether the Nova vibrated was not reported.

## Local artifacts and device state

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
- Connect using the SSH identity and host saved by `nova-push`. The Nova
  must remain awake for reliable SSH access. Do not publish the key or
  device credentials.

## Next steps for Claude

1. Review and integrate the day-three branch. Android validation was waived
   by the owner; do not install the Android toolchain or spend time on
   Android compatibility for this PortareOS work.
2. Retain the clean-build evidence and the exact dependency pins when
   changing anything.
3. Pacing and players: lock the emulated vblank to the panel (DRM vblank
   events or present timing) in place of Android's Choreographer; confirm
   rumble on the Nova; separate players for several pads.
4. Launcher integration: a `runemu` entry and the quit hotkey, launched from
   the launcher's own DRM-master hand-over. The Xbox library accepts XISO
   (`.iso`) images for now (owner's decision, 2026-10-24).
5. CHD, through a disc-reader abstraction, is low priority: only once
   everything else works. It no longer gates library integration.

There is no evidence yet for playability with input, performance relative to
xemu, Linux zero-copy sharing or reliable suspend/title relaunch.
