# hakuX for PortareOS

Original Xbox emulation for PortareOS on the Retroid Pocket Nova, through
RetroArch or the standalone `hakux` emulator.

Fork of [rfandango/hakuX](https://github.com/rfandango/hakuX); credit to upstream for the emulator core and Android app.

## Playing

- Requires ARM64 Linux (glibc) with Vulkan 1.3.
- Uses your own XISO game images (`.iso`). CHD is not supported yet.
- Keeps the emulated Xbox hard drive and caches in `/storage/saves/xbox`.

No BIOS, MCPX ROM or hard disk image is needed.

The RetroArch core currently supports player 1 only. Closing a game also
closes RetroArch. A game that starts another of its programs restarts
RetroArch on the same game and continues there, without returning to the
launcher. Reset and save states are not yet available.

The core's options, in RetroArch's Quick Menu, are the render scale, the
overscan crop, and the console's own settings: screen format (widescreen),
language and video region. All but the crop take effect when the game starts
next. Filters such as FXAA are RetroArch's shaders.

## Building

- [Linux build, run and packaging instructions](native/README.linux.md)
- [Android build notes](android/README.md) — retained from upstream; not validated by this fork
- [Changelog](CHANGELOG.md)

## License

GNU GPL v3 or later ([COPYING](COPYING)). Third-party code retains its own
licenses; see [LICENSE](LICENSE) and [licenses/](licenses/).
