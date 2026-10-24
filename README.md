# hakuX

hakuX runs original Xbox games on Android (arm64) and ARM64 Linux
(PortareOS), tuned for Adreno GPUs.

Its core runs the game's x86 code on a JIT (a FEX fork) under a
reimplementation of the Xbox kernel, and draws by decoding the push buffer
the game's own Direct3D sends the NV2A into Vulkan. Audio is the MCPX APU,
ported from xemu.

## Requirements

- An arm64 Android device with Vulkan (custom Turnip drivers can be loaded
  for Adreno GPUs), or ARM64 Linux with Vulkan, such as PortareOS on the
  Retroid Pocket Nova.
- Your own game images (ISO or XISO), dumped from discs you own.

No BIOS, MCPX ROM or hard disk image is needed. hakuX doesn't include any
copyrighted Xbox software.

## Building

See [android/README.md](android/README.md).

On ARM64 Linux, `hakux` runs a game fullscreen through SDL3 (KMSDRM); a
libretro core for RetroArch is in progress ([LIBRETRO_SPEC.md](LIBRETRO_SPEC.md)).
See [native/README.linux.md](native/README.linux.md).

## License

hakuX is free software under the GNU General Public License, version 3 or
later ([COPYING](COPYING)): anyone who distributes it, or a work based on it,
must make the complete source available under the same license. The code
ported from xemu (the MCPX APU) and the third-party components keep their
own licenses, listed in [LICENSE](LICENSE) and [licenses/](licenses/).

## Notes

- hakuX is not affiliated with Microsoft.
