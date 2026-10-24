# PortareOS handoff — 2026-10-24

Issue: https://github.com/portare-ch/hakuX/issues/2
Specification: [SPEC.md](SPEC.md)
Build/run instructions: [native/README.linux.md](native/README.linux.md)

## State

Day one, the Linux headless milestone, is integrated: PR #3 merged as
`1a36787d61`, and issue #2 is closed. Day two puts the game on the Nova's
panel; it is on `portareos/day2-display` (see below). The owner waived
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
`MESA_VK_WSI_DEBUG=linear` made it linear. Frames shown on the panel were
not inspected by a person. A DualSense Edge paired with the Nova was the
gamepad; input was not exercised. Launching from the launcher itself, with
its DRM-master hand-over, was simulated by stopping and starting
`portarelauncher.service`.

Test images for repeated runs are in `/roms/xbox` on the Nova (the SD card's
storage partition, grown to 117 GiB by portare-ch/portareos#749). They are
the user's and are not to be deleted.

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

1. Review and integrate the day-two branch. Android validation was waived by
   the owner; do not install the Android toolchain or spend time on Android
   compatibility for this PortareOS work.
2. Retain the clean-build evidence and the exact dependency pins when
   changing anything.
3. Audible audio: a Linux output for the APU's ring (ALSA or SDL audio on
   PortareOS's stack), keeping `out_null.c`'s pacing and pause semantics.
4. Input and pacing: exercise the Nova's built-in controls and rumble through
   SDL; lock the emulated vblank to the panel (DRM vblank events or present
   timing) in place of Android's Choreographer.
5. Launcher integration: a `runemu` entry and the quit hotkey, launched from
   the launcher's own DRM-master hand-over.
6. Implement CHD through a disc-reader abstraction before production Xbox
   library integration. Keep the production library CHD-only.

There is no evidence yet for playability with input, performance relative to
xemu, Linux zero-copy sharing or reliable suspend/title relaunch.
