# PortareOS day-one handoff — 2026-10-24

Issue: https://github.com/portare-ch/hakuX/issues/2
Specification: [SPEC.md](SPEC.md)
Build/run instructions: [native/README.linux.md](native/README.linux.md)

## State

The Linux headless milestone is complete and runs on the Nova. The owner
waived Android regression validation on 2026-10-24: this fork targets
PortareOS, and Android compatibility is not an acceptance requirement.
Android was not tested. PR #3 is ready for review; issue #2 tracks the change
until it is integrated. The user requested a bounded handoff for Claude;
graphical and production integration work has deliberately not started.

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

## Local artifacts and device state

- Source: `/Users/jmoehle/1234/hakuX`.
- Development container: `hakux-build`, mounted at `/src`.
- Pinned minimal image: `hakux-linux-day1:latest`.
- Dependencies: `build/deps`; verified build trees: `build/linux`,
  `build/linux-clean-verified` and `build/hle-tests`; stripped device copies:
  `build/deploy`. `build/linux-clean` retains the initial failed image test.
- Logs: `build/linux-checks.log`, `build/clean-build.log`,
  `build/hle-tests.log`, `build/statelock-sizes.log` and
  `build/evidence/nova-boundaries.log`. These generated files are untracked.
- Device diagnostics: `/tmp/hakux-day1` and the review run's
  `/tmp/hakux-day1-review`, including binaries, generated fixture discs and
  logs. No personal games/saves or launcher configuration
  were changed. No emulator diagnostic process should remain running.
- Connect using the existing `id_portare` identity and the host saved by
  `nova-push`. The Nova must remain awake for reliable SSH access. Do not
  publish the key or device credentials.

## Next steps for Claude

1. Integrate PR #3; it has been reviewed (see above). Android validation was
   waived by the owner; do not install the Android toolchain or spend time on
   Android compatibility for this PortareOS work. Issue #2 can be completed
   when this implementation is integrated.
2. Retain the successful clean-build evidence in `build/clean-build.log` and
   the reproducible commands and exact dependency pins when changing anything.
3. Specify the next display milestone: replace the Android window boundary
   with SDL3 Vulkan surface creation and use its required instance extensions.
   Reuse the existing swapchain rendering logic. Test direct KMS ownership,
   first-frame presentation and orderly return to the launcher on the Nova.
4. Add audible audio, controller/rumble handling and timing in subsequent
   bounded work. Preserve the proven headless and fault-handling checks.
5. Implement CHD through a disc-reader abstraction before production Xbox
   library integration. Keep the production library CHD-only.

There is no evidence yet for game playability, performance relative to xemu,
Linux zero-copy sharing or reliable suspend/title relaunch.
