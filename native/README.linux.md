# Linux build

The ARM64 Linux build provides the RetroArch core `hakux_libretro.so` and
headless diagnostics (`xboxrun`, `vulkan-probe`, `kms-grab`). PortareOS runs
Xbox games through RetroArch with the core. The Linux standalone `hakux` was
dropped in 0.4.0: it wasn't tested, and PortareOS no longer shipped it.
Installing and testing the core is in [TESTING.md](../TESTING.md).

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

The build produces `hakux_libretro.so`, `xboxrun`, `libretro-harness`,
`linux-smoke`, `audio-smoke`, `vulkan-probe` and `kms-grab`. CTest runs the
CPU/memory, audio, positive/negative boundary and libretro suites. The
boundary and libretro suites construct their own small synthetic XBE/XISO
files; they need no game dump, BIOS or proprietary guest software. SDL3 is
fetched only for the HLE tests below, which take the Khronos headers from
it.

The existing pure HLE regressions are a separate suite:

```sh
cmake -S tests/hle -B build/hle-tests -G Ninja -DCMAKE_C_COMPILER=clang-19 \
  -DNC_DEPS="$PWD/build/deps"
cmake --build build/hle-tests -j 4
ctest --test-dir build/hle-tests --output-on-failure
python3 tools/native/statelock.py
python3 tools/native/statelock.py --sizes --build build/linux
```

## Diagnostics on the Nova

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

`kms-grab out.png` saves the framebuffer the panel scans out, when it is
linear (the launcher's). Turnip's swapchain images are UBWC-compressed:
`kms-grab` then reports the framebuffer, its size and modifier, and saves
nothing.

## The libretro core

Load `hakux_libretro.so` in RetroArch with its Vulkan video driver and open
an XISO `.iso` image ([TESTING.md](../TESTING.md) has the steps). The
renderer shares RetroArch's Vulkan device and copies frames on the GPU; the
diagnostic harness uses CPU readback.

Ports 1-4 are players 1-4, each with a controller plugged in unless its
Device Type is None; changing it during the game plugs the controller in or
pulls it out. Rumble and save states are not yet available. Run-ahead
must stay off. Closing the game stops the core in RetroArch's process, and
the next game, a reset, a game that starts another of its programs and
RetroArch rebuilding its video driver (the game then starts again) boot in
the same process.

PortareOS's build (the option `HAKUX_LR_END_PROCESS`, `linux-build.sh`)
ends RetroArch when a stop takes longer than 10 s, rather than hang. With
`HAKUX_LR_STOP=0` in RetroArch's environment it brings back the way before:
closing the game closes RetroArch, and a game that starts another of its
programs starts RetroArch again on it.

Core options (`native/libretro/options.c`). All but the crop are read once,
before the console boots, so a change takes effect when the game starts next:

| Key | Values | Default |
| --- | --- | --- |
| `hakux_render_scale` | `1`-`4`: the renderer's resolution, times the game's | `1` |
| `hakux_crop_overscan` | `enabled`, `disabled` (the whole frame); changes at once | `enabled` |
| `hakux_screen_format` | `normal`, `widescreen` (shown 16:9), `letterbox` | `normal` |
| `hakux_language` | `auto` (RetroArch's), `english`, `japanese`, `german`, `french`, `spanish`, `italian`, `korean`, `chinese`, `portuguese` | `auto` |
| `hakux_video_region` | `auto` (the game's region), `ntsc-m`, `ntsc-j`, `pal` | `auto` |

PAL games are offered 60 Hz (the console's PAL60 setting) whatever the
options. 480p, 720p and 1080i stay off. Filters (FXAA) and the picture's
shape on screen are RetroArch's: its shaders and its aspect ratio setting
("Core provided" shows the core's 4:3 or 16:9).

The remaining design targets are in [LIBRETRO_SPEC.md](../LIBRETRO_SPEC.md).

`HAKUX_LR_STATS=1` logs the core's time in `retro_run`, and
`HAKUX_DISP_STATS=1` (any front end) the new frames per second, their age
when taken and the gaps between them.

## The PortareOS package

PortareOS's `hakux-lr` package installs the release tarball. Make it from a
clean checkout of the release commit, in the same image:

```sh
HAKUX_VERSION=0.4.0 CC=clang-19 CXX=clang++-19 sh tools/native/linux-build.sh
CC=clang-19 sh tools/native/linux-package.sh 0.4.0
```

It writes `build/package/hakux-0.4.0-aarch64.tar.gz` and prints its
SHA-256. The archive holds `lib/libretro/hakux_libretro.so`, stripped and
without a RUNPATH, with `lib/libretro/hakux_libretro.info`, the licences in
`share/licenses/hakux/`, and `BUILD-INFO`: the source revision, the compiler
and `linux-deps.json`. Names are sorted, owned by root and dated at the
commit, so the same build packs to the same tarball. Publish it as the
release `linux-v<version>` and set the package's `PKG_VERSION` and
`PKG_SHA256` to match.

In PortareOS the Xbox system runs RetroArch with the core, which keeps the
console's disk in `/storage/saves/xbox`. Discs are `.iso` images.

## Properties

Linux startup properties use names such as
`HAKUX_PROP_DEBUG_HAKUX_APUSPIN=0`. Existing `NC_*`/`HAKUX_*` environment
options remain available. Android properties still use the Android API.
Environment configuration does not provide live control of another process.

See [PORTAREOS_HANDOFF.md](../PORTAREOS_HANDOFF.md) for measured results,
outstanding validation and the next implementation steps.
