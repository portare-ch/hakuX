# hakuX for PortareOS

Original Xbox emulation for PortareOS on the Retroid Pocket Nova, as a
RetroArch core.

Fork of [rfandango/hakuX](https://github.com/rfandango/hakuX); credit to upstream for the emulator core and Android app.

## Playing

- Requires ARM64 Linux (glibc 2.38 or newer) with Vulkan 1.3.
- Uses your own XISO game images (`.iso`). CHD is not supported yet.
- Keeps the emulated Xbox hard drive and caches in `/storage/saves/xbox`.

No BIOS, MCPX ROM or hard disk image is needed.

Ports 1-4 are players 1-4; a port set to None in RetroArch has no controller
plugged in. Rumble and save states are not yet available. It
stops and starts games within RetroArch's process: restarting a game, or a
game starting another of its programs, no longer restarts RetroArch. On
PortareOS, closing a game returns to the launcher, as with every core.

The core's options, in RetroArch's Quick Menu, are the render scale, the
overscan crop, and the console's own settings: screen format (widescreen),
language and video region. All but the crop take effect when the game starts
next. Filters such as FXAA are RetroArch's shaders.

[Testing the core](TESTING.md) covers installing a release, logs and what
to check, on PortareOS and on other ARM64 Linux systems.

## Building

- [Linux build, checks and packaging](native/README.linux.md)
- [Android build notes](android/README.md) — retained from upstream; not validated by this fork
- [Changelog](CHANGELOG.md)

## License

GNU GPL v3 or later ([COPYING](COPYING)). Third-party code retains its own
licenses; see [LICENSE](LICENSE) and [licenses/](licenses/).
