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
closes RetroArch; reset, core options and save states are not yet available.

## Building

- [Linux build, run and packaging instructions](native/README.linux.md)
- [Android build notes](android/README.md) — retained from upstream; not validated by this fork
- [Changelog](CHANGELOG.md)

## License

GNU GPL v3 or later ([COPYING](COPYING)). Third-party code retains its own
licenses; see [LICENSE](LICENSE) and [licenses/](licenses/).
