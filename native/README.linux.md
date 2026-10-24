# Linux build

The ARM64 Linux build provides the RetroArch core `hakux_libretro.so`, the
standalone `hakux` emulator (SDL3/KMSDRM, PipeWire audio), and the headless
`xboxrun` diagnostic runner. PortareOS launches Xbox games through RetroArch
by default; the standalone remains selectable.

## Build and check

On an ARM64 Linux host, install Clang 19, CMake, Ninja, Git, Python 3 and
zlib development files (the Linux UAPI headers come with the C library's).
The versioned source manifest is
[linux-deps.json](../tools/native/linux-deps.json). The fetcher uses full
commit hashes, including FEX's required submodules, and refuses to build
modified or mismatched dependency checkouts.

From the repository root:

```sh
python3 tools/native/linux-deps.py
CC=clang-19 CXX=clang++-19 sh tools/native/linux-build.sh
```

The fetcher uses `build/deps`, independently of the `external/fex` submodule;
no Android development checkout is needed.

On macOS with Apple's `container`, build the supplied image and run:

```sh
container build --tag hakux-linux --file tools/native/Dockerfile.linux tools/native
container run --rm --cpus 6 --memory 8g --volume "$PWD:/src" --workdir /src \
  hakux-linux sh -c 'python3 tools/native/linux-deps.py && sh tools/native/linux-build.sh'
```

For a second build directory, set `HAKUX_BUILD=/src/build/linux-clean`.
`HAKUX_DEPS` selects another dependency directory for the build (fetch it with
`linux-deps.py --dest <directory>`), and `JOBS` selects build parallelism.
Additional arguments to `linux-build.sh` are passed to CMake;
a PortareOS sysroot/toolchain can be supplied there when required.

The build produces `hakux`, `hakux_libretro.so`, `xboxrun`, `libretro-harness`,
`linux-smoke`, `audio-smoke`, `audio-sdl-smoke`, `vulkan-probe` and `kms-grab`.
SDL3 is pinned at release 3.4.16,
PortareOS's version. It is built without backends, only to link `hakux`
against; the device's own `libSDL3.so.0` (with KMSDRM) is loaded at run time.
CTest runs the CPU/memory, audio, positive/negative boundary and libretro
suites. `audio-sdl-smoke` checks the SDL output's pacing, pause and wakeup
contract on SDL's dummy driver; on the Nova it runs through PipeWire.
`-DHAKUX_DIAGNOSTIC_AUDIO=ON` makes `hakux` silent as well. The boundary
suite constructs its own small synthetic XBE/XISO files; it needs no game
dump, BIOS or proprietary guest software.

The existing pure HLE regressions are a separate suite:

```sh
cmake -S tests/hle -B build/hle-tests -G Ninja -DCMAKE_C_COMPILER=clang-19 \
  -DNC_DEPS="$PWD/build/deps"
cmake --build build/hle-tests -j 4
ctest --test-dir build/hle-tests --output-on-failure
python3 tools/native/statelock.py
python3 tools/native/statelock.py --sizes --build build/linux
```

## Run on the Nova

Copy the executables and `tests/native/linux/check.py` to a dedicated
temporary directory on the device. Stripping copies with `llvm-strip-19`
reduces each core diagnostic from approximately 47 MiB to 6 MiB. Keep the
unstripped build on the development host for debugging.

Within the device's diagnostic directory:

```sh
timeout 60 python3 check.py .
timeout 15 ./vulkan-probe
ldd ./xboxrun
```

The suite verifies guest arithmetic/branches, system-operation exits, the
replacement kernel's initialization, shared RAM aliases and recovery from
translated dirty-page/MMIO faults through `nc_core_fault`. It deliberately
tests an incorrect expectation, an unexpected host fault (from host code
after the CPU ran, which `nc_core_fault` must decline), missing/invalid
discs, a synthetic HLT title and an infinite guest loop. Expected failure
exit codes are asserted; they do not count as successful game boots. It also
boots the synthetic title in `--render` mode; without a Vulkan loader that
run still passes, so drawing itself is checked with real titles below.

For a developer-supplied ISO/XISO, explicitly select headless mode and a
temporary data directory:

```sh
NC_TIMEOUT=30 ./xboxrun --headless /path/to/game.iso /tmp/hakux-diagnostic-data
```

Use a positive `NC_TIMEOUT`. The runner forces graphics off, stores its
emulated disk/cache under the supplied directory and reports the silent
audio backend. `--render` in place of `--headless` keeps the renderer on
without a display and writes a frame every 2 s (`NC_CAPTURE_EVERY`, up to
`NC_CAPTURE_MAX`) to `<data directory>/frames/`. Discs are XISO images;
CHD is not supported yet. `vulkan-probe` checks the same portable capability floor
as the renderer and creates a device, but creates no surface and renders no
frames. It does not establish Linux shared-memory import correctness.

## The game on the panel

`hakux` runs a title fullscreen:

```sh
./hakux /roms/xbox/game.iso /tmp/hakux-data [option...]
```

Options, as the Android app's: `scale=<n>`, `aspect=auto|4:3|16:9|stretch`,
`fxaa=0|1`, `upscale=bilinear|sgsr`, `video=<flags, hex>` and `lang=<n>`;
`exitafter=<seconds>` leaves after that long (tests). Every gamepad is
player 1, merged. Escape, Home + Start (the guide button and Start, as
every PortareOS emulator quits), SIGINT/SIGTERM or the title stopping
leave; the exit code is 0 if a title frame was shown. On KMSDRM the
panel must be free: the launcher drops the DRM master while it runs a game.
Sound goes to the default PipeWire sink, which SDL finds through
`XDG_RUNTIME_DIR` (set by `/etc/profile`); without an output device the
APU's ring is drained silently at 48 kHz. `HAKUX_APU_STATS=1` logs the
APU's frames, callbacks and short callbacks each second. Over SSH, load the
profile and stop the launcher first, and start it again afterwards:

```sh
. /etc/profile
systemctl stop portarelauncher.service
./hakux /roms/xbox/game.iso /tmp/hakux-data exitafter=30
systemctl start portarelauncher.service
```

`kms-grab out.png` saves the framebuffer the panel scans out, when it is
linear (the launcher's). Turnip's swapchain images are UBWC-compressed:
`kms-grab` then reports the framebuffer, its size and modifier, and saves
nothing.

## The libretro core

Load `hakux_libretro.so` in RetroArch with its Vulkan video driver and open
an XISO `.iso` image. The renderer shares RetroArch's Vulkan device and
copies frames on the GPU; the diagnostic harness uses CPU readback.

Closing a game closes RetroArch. Only player 1 is supported; reset, core
options and save states are not yet available. Run-ahead must stay off.
The remaining design targets are in [LIBRETRO_SPEC.md](../LIBRETRO_SPEC.md).

`HAKUX_LR_STATS=1` logs the core's time in `retro_run`, and
`HAKUX_DISP_STATS=1` (any front end) the new frames per second, their age
when taken and the gaps between them.

## The PortareOS packages

PortareOS's `hakux-sa` (the standalone) and `hakux-lr` (the core) packages
install one release tarball. Make it from a clean checkout of the release
commit, in the same image:

```sh
HAKUX_VERSION=0.2.0 CC=clang-19 CXX=clang++-19 sh tools/native/linux-build.sh
CC=clang-19 sh tools/native/linux-package.sh 0.2.0
```

It writes `build/package/hakux-0.2.0-aarch64.tar.gz` and prints its
SHA-256. The archive holds `bin/hakux` and `lib/libretro/hakux_libretro.so`,
stripped and without a RUNPATH, with `lib/libretro/hakux_libretro.info`, the
licences in `share/licenses/hakux/`, and `BUILD-INFO`: the source revision,
the compiler and `linux-deps.json`. Names are sorted, owned by root and dated
at the commit, so the same build packs to the same tarball. Publish it as the
release `linux-v<version>` and set the packages' `PKG_VERSION` and
`PKG_SHA256` to match.

In PortareOS the Xbox system's default is RetroArch with the core; the
standalone stays selectable. Both keep the console's disk in
`/storage/saves/xbox`. Discs are `.iso` (XISO) images.

## Properties

Linux startup properties use names such as
`HAKUX_PROP_DEBUG_HAKUX_APUSPIN=0`. Existing `NC_*`/`HAKUX_*` environment
options remain available. Android properties still use the Android API.
Environment configuration does not provide live control of another process.

See [PORTAREOS_HANDOFF.md](../PORTAREOS_HANDOFF.md) for measured results,
outstanding validation and the next implementation steps.
