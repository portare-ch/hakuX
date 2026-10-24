# Linux diagnostic build

This is the first PortareOS milestone, tracked in
[issue #2](https://github.com/portare-ch/hakuX/issues/2). It builds the
existing core and a headless runner for AArch64 Linux with 4 KiB pages.
The diagnostic audio sink is silent and paced at 48 kHz stereo. Window
presentation, audible audio and production launcher integration remain open.

## Build and check

On an ARM64 Linux host, install Clang 19, CMake, Ninja, Git, Python 3 and
zlib development files. The versioned source manifest is
[linux-deps.json](../tools/native/linux-deps.json). The fetcher uses full
commit hashes, including FEX's required submodules, and refuses to build
modified or mismatched dependency checkouts.

From the repository root:

```sh
python3 tools/native/linux-deps.py
sh tools/native/linux-build.sh
```

The fetcher uses `build/deps`, independently of the `external/fex` submodule.
This avoids requiring an initialized Android development checkout. The
documented `nv2a_vsh_cpu` revision is pinned by its full SHA; fetching only
current branch tips does not retrieve that older commit.

On macOS with Apple's `container`, build the supplied image and run:

```sh
container build --tag hakux-linux-day1 --file tools/native/Dockerfile.linux tools/native
container run --rm --cpus 6 --memory 8g --volume "$PWD:/src" --workdir /src \
  hakux-linux-day1 sh -c 'python3 tools/native/linux-deps.py && sh tools/native/linux-build.sh'
```

For a second build directory, set `HAKUX_BUILD=/src/build/linux-clean`.
`HAKUX_DEPS` selects another dependency directory, and `JOBS` selects build
parallelism. Additional arguments to `linux-build.sh` are passed to CMake;
a PortareOS sysroot/toolchain can be supplied there when required.

The build produces `xboxrun`, `linux-smoke`, `audio-smoke` and `vulkan-probe`.
CTest runs the CPU/memory and audio tests plus the positive/negative boundary
suite. The boundary suite constructs its own small synthetic XBE/XISO files;
it needs no game dump, BIOS or proprietary guest software.

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
tests an incorrect expectation, an unexpected host fault, missing/invalid
discs, a synthetic HLT title and an infinite guest loop. Expected failure
exit codes are asserted; they do not count as successful game boots.

For a developer-supplied ISO/XISO, explicitly select headless mode and a
temporary data directory:

```sh
NC_TIMEOUT=30 ./xboxrun --headless /path/to/game.iso /tmp/hakux-diagnostic-data
```

Use a positive `NC_TIMEOUT`. The runner forces graphics off, stores its
emulated disk/cache under the supplied directory and reports the silent
audio backend. This developer interface does not change PortareOS's CHD-only
production library. `vulkan-probe` checks the same portable capability floor
as the renderer and creates a device, but creates no surface and renders no
frames. It does not establish Linux shared-memory import correctness.

Linux startup properties use names such as
`HAKUX_PROP_DEBUG_HAKUX_APUSPIN=0`. Existing `NC_*`/`HAKUX_*` environment
options remain available. Android properties still use the Android API.
Environment configuration does not provide live control of another process.

See [PORTAREOS_HANDOFF.md](../PORTAREOS_HANDOFF.md) for measured results,
outstanding validation and the next implementation steps.
