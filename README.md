# hakuX for PortareOS

Play original Xbox games on the **Retroid Pocket Nova** with
**[PortareOS](https://github.com/portare-ch/portareos)**.
This Linux port of hakuX is developed and tested only on that combination.
It is included in PortareOS and runs games through RetroArch. Other
**aarch64 (ARM64) Linux systems** should also work;
[install the core in RetroArch](#play-on-other-aarch64-linux-systems).

Bring your own game disc images. **No Xbox BIOS, MCPX ROM or hard disk
image is needed**; hakuX creates the console's storage when you start a game.

## hakuX is free

hakuX is free, and always will be. Download the PortareOS Linux core from
[this fork's releases](https://github.com/portare-ch/hakuX/releases), or the
Android app from [upstream's releases](https://github.com/rfandango/hakuX/releases).

If you paid for hakuX, including a subscription, an in-app unlock, a
"premium" build or an app that repackages it, you were scammed. Ask for a
refund wherever you paid and get hakuX here for free.

## Play on PortareOS

1. **Install or update PortareOS.** Follow the
   [installation guide](https://github.com/portare-ch/portareos/blob/next/documentation/INSTALLATION.md)
   for a fresh installation. If you already use PortareOS, update through
   **Settings → About → Update**, then restart.
2. **Copy a game to `/storage/roms/xbox/`.** Use an Xbox XISO disc image
   (`.iso`), or a `.chd` made from it. Keep the game as a disc image; unpack
   any ZIP or 7z archive first.
3. **Open Xbox in the launcher and select your game.** hakuX starts
   automatically. If you copied the game while browsing, press **Home** to
   return to the console list and load it again.

To copy files over Wi-Fi, enable **Settings → SSH**. **Settings → About**
shows the Nova's IP address and password. Connect from your computer with
an SFTP client, using that address, username `root` and the shown password,
then copy your games into `/storage/roms/xbox/`.

During a game, **Home + Start** returns to the launcher. **Home + the top
face button** opens RetroArch's Quick Menu for controls and core options.

<details>
<summary>Install a newer hakuX release before PortareOS includes it</summary>

1. Download `hakux-<version>-aarch64.tar.gz` from the latest `linux-v…`
   [release](https://github.com/portare-ch/hakuX/releases) and extract it.
2. Inside the extracted folder, open `lib/libretro/`. Copy both
   `hakux_libretro.so` and `hakux_libretro.info` to `/storage/cores/` on the
   Nova, creating that directory if needed.
3. Restart the Nova, then launch a game from Xbox as usual.

To return to PortareOS's bundled version, remove those two files from
`/storage/cores/` and restart. Back up `/storage/saves/xbox/hdd/` before
trying a different version; it contains your game saves.

</details>

## Play on other aarch64 Linux systems

The release needs **64-bit ARM Linux**, glibc 2.38 or newer, libstdc++ from
GCC 13 or newer, zlib, and a **Vulkan 1.3** GPU driver. Install RetroArch
through your distribution. These instructions are for trying the core
outside PortareOS; other systems and GPUs are untested.

1. Download and extract `hakux-<version>-aarch64.tar.gz` from the latest
   `linux-v…` [release](https://github.com/portare-ch/hakuX/releases).
2. In RetroArch, open **Settings → Directory** and note the **Cores** and
   **Core Info** directories. From the archive's `lib/libretro/` folder,
   copy `hakux_libretro.so` into Cores and `hakux_libretro.info` into
   Core Info.
3. Set **Settings → Drivers → Video** to **vulkan**. Save the configuration
   through **Main Menu → Configuration File → Save Current Configuration**
   and restart RetroArch.
4. Choose **Load Core → Microsoft - Xbox (hakuX)**, then **Load Content**
   and select your game's `.iso` or `.chd` file.

Use the game's own save function. Save states, rewind and run-ahead are
unsupported; leave them off. hakuX stores game saves in `hdd/` under
RetroArch's save directory. Choose a writable directory under
**Settings → Directory → Save Files**.

## Game formats

**Start with an Xbox XISO `.iso`.** These are the images tested with real
games. Full disc dumps (Redump) are also detected, but haven't been tested
with a real game.

To save space, convert an ISO to CHD with `chdman` on your computer:

```sh
chdman createdvd -i "Game.iso" -o "Game.chd"
```

Copy the resulting `Game.chd` to your games folder. hakuX reads it directly.
CHD support requires hakuX 0.6.0 or newer. Use **createdvd**; CD CHDs made
with `createcd` and CHDs that depend on a parent image are unsupported.

**BLACK** and **Burnout 3** are tested on the Nova. Compatibility varies by
game; other titles may have bugs or fail to start.

## Controls, picture and saves

- **Controllers:** RetroArch ports 1–4 are Xbox players 1–4. Face buttons
  follow their Xbox positions. **L1 is White**, **R1 is Black**, and
  **L2/R2 are the triggers**. Remap through **Quick Menu → Controls**.
  Rumble is not yet available.
- **Picture:** open **Quick Menu → Core Options**. Start with **Render
  Scale: 1x**; higher values draw a sharper picture and need more GPU power.
  Set **Screen Format: Widescreen** for games that support it, and use
  RetroArch's **Core provided** aspect ratio. Close and reopen the game
  after changing render scale, screen format, language or video region.
  **Crop Overscan** changes immediately.
- **Saving:** save inside the game, as on an Xbox. On PortareOS, your saves
  live in `/storage/saves/xbox/hdd/`. Back up that folder to keep them when
  moving or reinstalling. RetroArch save states are not available.

## If something goes wrong

| Problem | What to try |
| --- | --- |
| Xbox or a game is missing from the PortareOS launcher | Check that the file is in `/storage/roms/xbox/` and ends in `.iso` or `.chd`. Extract ZIP/7z archives. Return to the console list with Home. Update PortareOS if CHDs are missing. |
| RetroArch cannot find hakuX | Check that both release files were copied into the directories above, then restart. |
| The game is slow or the picture is black | On other Linux systems, select the **vulkan** video driver and restart RetroArch. Try **Render Scale: 1x**. |
| The game asks for a controller | Open **Quick Menu → Controls → Port 1 Controls** and set **Device Type** to **Xbox Controller**. A port set to None is unplugged. |
| A game fails to start | Try an XISO image and the latest core. For a CHD, check that it was made with `createdvd`. If it still fails, report it below. |

Still stuck? [Open an issue](https://github.com/portare-ch/hakuX/issues)
with the game and region, hakuX version, device and OS, and what happened.
On PortareOS, copy `/var/log/exec.log` and
`/var/log/retroarch/retroarch.log` **before starting another game**.
[The testing guide](TESTING.md#logs) explains collecting logs in more detail.

## Development and credits

[Build for Linux](native/README.linux.md) · [Testing](TESTING.md) ·
[Changelog](CHANGELOG.md)

This is the PortareOS Linux fork of
[rfandango/hakuX](https://github.com/rfandango/hakuX), whose authors made
the emulator core and Android app. For Android, see the upstream project;
this fork's [Android build notes](android/README.md) are retained for
development and are not validated here.

GNU GPL v3 or later ([COPYING](COPYING)). Third-party code retains its own
licenses; see [LICENSE](LICENSE) and [licenses/](licenses/).
