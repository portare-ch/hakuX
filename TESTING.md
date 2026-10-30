# Testing the hakuX core

hakuX's RetroArch core, `hakux_libretro.so`, is made and tested for
PortareOS on the Retroid Pocket Nova (Snapdragon 8 Gen 2, Adreno 740).
Other ARM64 Linux systems with RetroArch should work too, but are not
tested by this fork.

## Before you start

- **Games:** your own discs, as `.iso` images or as CHDs made from them with
  `chdman createdvd -i game.iso -o game.chd`. XISO images and full disc
  dumps (Redump) are tested, as ISO and CHD. CD CHDs (`chdman createcd`)
  don't work: an Xbox disc is a DVD.
- **No BIOS** or other Xbox files are needed.
- **Back up the console's hard disk** before trying a new version. Game saves
  are stored inside it, in the `hdd/` folder of RetroArch's save directory
  (`/storage/saves/xbox/hdd` on PortareOS).
- **Note the version** you test: RetroArch's Main Menu → Information → Core
  Information shows it.

## On PortareOS

### Playing

Put games in `/storage/roms/xbox`. The launcher's Xbox system starts them in
RetroArch with hakuX.

### A newer core than the installed one

To try a release before PortareOS ships it:

1. Download `hakux-<version>-aarch64.tar.gz` from the
   [releases](https://github.com/portare-ch/hakuX/releases), and check it
   against its `.sha256` file.
2. Copy `lib/libretro/hakux_libretro.so` and `lib/libretro/hakux_libretro.info`
   from it into `/storage/cores`.
3. Restart the device. The copies now take the place of the installed core.

To go back, delete both files from `/storage/cores` and restart. Delete them
there, not in `/tmp/cores`: deleting them in `/tmp/cores` hides the
installed core as well.

### Logs

After the problem, before you start another game (each game replaces them):

- `/var/log/exec.log`: the core's lines start with `hakuX-`.
- `/var/log/retroarch/retroarch.log`: RetroArch's own.

For frame rate numbers, start the game over SSH with the statistics on.
They show up in `exec.log` every 10 s, such as
`stats: 59.9 new frames/s` and `retro_run ... ms each`:

```sh
systemctl stop portarelauncher
HAKUX_DISP_STATS=1 HAKUX_LR_STATS=1 runemu.sh "/storage/roms/xbox/<game>.iso" \
  -Pxbox --core=hakux --emulator=retroarch --controllers=
systemctl start portarelauncher
```

## On other ARM64 Linux systems

### What it needs

- **System:** ARM64 (aarch64) Linux with glibc 2.38 or newer, libstdc++ from
  GCC 13 or newer, and zlib.
- **GPU driver:** Vulkan 1.3. Mesa's Turnip on Adreno 740 is tested; other
  GPUs are not tested by this fork.
- **RetroArch:** with its `vulkan` video driver. 1.22.2 as PortareOS builds it
  is tested. With another video driver the core copies every frame through
  the CPU, which is meant for diagnosis only.

### Installing

1. From the release tarball, copy `lib/libretro/hakux_libretro.so` into
   RetroArch's cores directory (Settings → Directory → Cores).
2. Copy `hakux_libretro.info` into its core info directory (Settings →
   Directory → Core Info).

Or load it directly:

```sh
retroarch -v -L /path/to/hakux_libretro.so "/path/to/game.iso" 2>&1 | tee hakux.log
```

The core keeps the console's hard disk in `hdd/` and its shader and pipeline
caches in `cache/`, both under RetroArch's save directory. Leave run-ahead
off.

Release builds are PortareOS's build. If stopping a game takes over 10 s,
they end RetroArch rather than hang. `HAKUX_LR_STOP=0` in RetroArch's
environment brings back the way before 0.4.0: closing a game then closes
RetroArch.

### Logs

Start RetroArch from a terminal, as above. The core's lines start with
`hakuX-`. `HAKUX_DISP_STATS=1` and `HAKUX_LR_STATS=1` in the environment add
the frame rate and the core's time per frame.

## What to check

1. **Booting and playing:** the game gets to its menus and plays, with sound
   and controller 1.
2. **More players:** a second controller (RetroArch's user 2) plays as player
   2, and so on up to 4. Each port has a controller plugged in unless its
   Device Type (Quick Menu → Controls → Port N Controls) is None. Setting
   player 1's to None during the game pulls the controller out: most games
   pause and ask for it. Setting it back plugs it in again.
3. **Speed:** play is smooth. With the statistics on, a 60 fps game shows
   about 59.9 new frames a second and a 30 fps game about 30.
4. **Restart** (Quick Menu → Restart): the game starts again from the
   beginning. This hasn't been confirmed in RetroArch yet; please report what
   you see either way.
5. **Close Content:** the game closes without hanging. On PortareOS,
   RetroArch then returns to the launcher. Elsewhere, load the same game or
   another in the same RetroArch session.
6. **Core options** (Quick Menu → Core Options): Render Scale, Screen Format,
   Language and Video Region take effect the next time the game starts, and
   RetroArch says so. Crop Overscan changes at once.
7. **Game saves:** what the game saves is still there after closing and
   starting it again.
8. **Games that start another of their programs** (some compilations and
   demo discs): they continue without going back to the menu.
9. **Video driver restarts:** some RetroArch video settings restart its video
   driver. The game then starts again, with a message saying so.

## Reporting

Open an issue at https://github.com/portare-ch/hakuX/issues with:

- the hakuX version;
- the device, GPU and driver (Mesa version), and the system (PortareOS
  version or distribution);
- the RetroArch version;
- the game, its region, whether the image is an XISO or a full dump, and
  whether it's an ISO or a CHD;
- what you did, what happened, and what you expected;
- the logs.

Don't attach games or other copyrighted files.

## For developers

The checks run without a device: see
[Build and check](native/README.linux.md#build-and-check). The libretro
harness, `libretro-harness <core.so> <game.iso> <frames> <save directory>`,
loads the core as RetroArch does and can play a real game headless. Its
variables are described at the top of
[harness.c](tests/native/libretro/harness.c): `HARNESS_VULKAN`,
`HARNESS_CYCLES`, `HARNESS_RESET_AT`, `HARNESS_REINIT_AT` and others.
