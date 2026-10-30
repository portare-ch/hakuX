# Changelog

## linux-v0.12.0-exp.2 (PortareOS, experimental)

The first batch of audio fixes, on top of 0.12.0-exp.1's graphics features. It's a pre-release; 0.11.0 stays the latest stable release.

### What's new

- **Stereo panning** — Ordinary 2D sounds, such as music, menus and effects a game doesn't place in 3D, now play on the left and right speakers as the game pans them. Before, they came out of both speakers at the louder side's level.
- **Reused 3D voices** — A 3D sound no longer starts with the end of the sound its voice played before.
- **No dropped audio** — When RetroArch takes fewer samples than it's offered, the rest is played next instead of dropped. Before, a whole period of sound (about 17 ms) was lost each time, heard as a click or a gap.
- Everything in 0.12.0-exp.1 below.

### Experimental

The audio fixes follow from how the Xbox's audio unit routes and filters a voice, and pass the build's tests; they haven't been listened to in a game yet. Halo's and Ninja Gaiden Black's quiet, off-centre sound may not be fixed by them: 3D sounds, centre and surround channels, and effects such as reverb are still mixed as before. If a game sounds different from 0.11.0, report the game and the scene.

### Current limitations

- Save states are not yet available.
- XLink Kai, which upstream also has, isn't offered in RetroArch yet, and neither is the Internet (NAT).
- CD CHDs (`chdman createcd`) and CHDs that are deltas of another are refused: an Xbox disc is a DVD.
- NBA Inside Drive 2003 saves its prestamped player textures blank: it reads back small images the GPU drew, which hakuX doesn't copy back to memory yet.

## linux-v0.12.0-exp.1 (PortareOS, experimental)

An experimental build with more of the Xbox GPU's graphics features. It's a pre-release; 0.11.0 stays the latest stable release. PortareOS ships it for broad testing.

### What's new

- **More texture formats** — Textures in L16, V16U16, R6G5B5, R5G5B5A1 and R4G4B4A4, and depth textures that a pixel shader reads, now show. Before, they came out white.
- **Window clip** — Exclusive clip windows, and several windows at once, now clip as the Xbox does: split-screen, picture-in-picture and HUD masks. Before, only the box around the windows clipped.
- **Lighting** — Spotlight falloff and specular highlights follow the curve the Xbox's lighting unit computes, instead of a linear cone and an approximated power.
- **Pixel-shader modes** — Depth replace (`texm3x2depth`), BRDF lookups and constant-eye specular reflections (`texm3x3spec`) now work. Before, they were drawn as plain 2D textures.

### Experimental

These follow NVIDIA's documentation, xemu, and the Xbox captures of nxdk_pgraph_tests where they exist. Burnout 3 and Halo showed no glitches in a quick play session on the Nova; other games haven't been tried. A game that looked right in 0.11.0 can look wrong in this build; if one does, report the game and the scene.

### Current limitations

- Save states are not yet available.
- XLink Kai, which upstream also has, isn't offered in RetroArch yet, and neither is the Internet (NAT).
- CD CHDs (`chdman createcd`) and CHDs that are deltas of another are refused: an Xbox disc is a DVD.
- NBA Inside Drive 2003 saves its prestamped player textures blank: it reads back small images the GPU drew, which hakuX doesn't copy back to memory yet.

## linux-v0.11.0 (PortareOS)

System link: play a game's LAN mode with other devices on your network.

### What's new

- **System link (LAN)** — A new core option, **Network**, connects the console to the other consoles on your network. Set it to **LAN (system link)** in **Quick Menu → Core Options** on each device, then close and reopen the game. Devices on the same Wi-Fi find each other. A device the broadcast doesn't reach, such as one on another subnet or a VPN (Tailscale, ZeroTier), can be added by address to `lan-peers.txt` in the save directory. It's off by default.
- **Copied consoles** — Two devices with the same console identity, such as a hard disk copied from one to the other, can't see each other's games. RetroArch now says so, and the device gets an identity of its own the next time the game starts. Its saves stay valid.

Tried on the Nova: Halo's system link game reached another computer on the network, and that computer's frames reached the game. A game between two devices hasn't been tried yet.

### Current limitations

- Save states are not yet available.
- XLink Kai, which upstream also has, isn't offered in RetroArch yet, and neither is the Internet (NAT).
- CD CHDs (`chdman createcd`) and CHDs that are deltas of another are refused: an Xbox disc is a DVD.
- NBA Inside Drive 2003 saves its prestamped player textures blank: it reads back small images the GPU drew, which hakuX doesn't copy back to memory yet.

## linux-v0.10.0 (PortareOS)

A crash fix for NBA Inside Drive 2003, the GPU's antialiasing control, and nxdk homebrew that gets past its start-up.

### What's new

- **NBA Inside Drive 2003** — The game no longer crashes to the launcher after its intro. hakuX had sized its 640x480 front buffer as 640x960 and copied it back to memory at that size, over a page of the game's code. Images a game draws into now stay within the memory block the game gave them. Tried on the Nova: past the intro at full speed.
- **Antialiasing control** — Games that antialias with alpha-to-coverage, such as for foliage, fences and hair, or that draw into only some of a pixel's samples, get those effects. Before, those edges were hard and every sample was drawn.
- **nxdk homebrew** — Programs built with nxdk, the open-source Xbox SDK, no longer stop in their first second. nxdk_pgraph_tests now runs to the end, but what nxdk's graphics library draws doesn't show yet: the screen stays white.
- **First start after updating** — The GPU pipelines are built again once, so a game may stutter briefly the first time.

The antialiasing control follows the GPU's register as documented for its successor, and passes the build's tests and shader checks. It hasn't been tried in a game that uses it yet, and its sample order and coverage pattern haven't been compared against an Xbox.

### Current limitations

- Save states are not yet available.
- Upstream's LAN and XLink Kai backends are included, but this release does not add network settings to RetroArch. Networking is off by default.
- CD CHDs (`chdman createcd`) and CHDs that are deltas of another are refused: an Xbox disc is a DVD.
- NBA Inside Drive 2003 saves its prestamped player textures blank: it reads back small images the GPU drew, which hakuX doesn't copy back to memory yet.

## linux-v0.9.0 (PortareOS)

More of the Xbox GPU's texture and drawing features.

### What's new

- **Anisotropic filtering** — Textures a game filters anisotropically, such as floors and roads seen at an angle, are filtered at the game's degree (2x to 8x, up to what the GPU supports) instead of blurring.
- **Two-sided lighting** — Lit geometry drawn with both sides visible, such as foliage, cloth and flags, gets its own lighting on the back faces instead of the front's.
- **Colour key** — Textures keyed by a colour, such as sprites, no longer show that colour: matching texels become transparent, black, or are not drawn, as the game chooses.
- **Line width** — Lines, such as HUDs, radars and beams, are as wide as the game asks, scaled with the render scale. At 2x they were half as thick as on an Xbox.
- **Dithering** — Games that dither into 16-bit images are dithered, so skies, fog and gradients there no longer show bands.
- **Quincunx and gaussian filters** — Textures read through the Xbox's convolution filters, used by D3D's quincunx and gaussian antialiasing and by blur effects, are filtered with those kernels instead of plainly.
- **Signed textures** — Texture channels a game reads as signed, such as the normal maps of bump lighting, are read as signed, so bumps are no longer flat or inverted.
- **First start after updating** — The GPU pipelines are built again once, so a game may stutter briefly the first time.

These follow the Xbox GPU's registers and its published hardware tests, and pass the build's tests and shader checks. They haven't been tried in games on the Nova yet. The dither pattern, the quincunx kernel and the exact signed range haven't been compared against an Xbox.

### Current limitations

- Save states are not yet available.
- Upstream's LAN and XLink Kai backends are included, but this release does not add network settings to RetroArch. Networking is off by default.
- CD CHDs (`chdman createcd`) and CHDs that are deltas of another are refused: an Xbox disc is a DVD.
- Full disc dumps (Redump), as `.iso` or CHD, haven't been tested with a real game yet.

## linux-v0.8.0 (PortareOS)

Rumble, and a sharper picture by default.

### What's new

- **Rumble** — The game's controller vibration reaches the controllers' motors. It is off by default: turn it on in **Quick Menu → Core Options → Rumble**; the change applies at once. The motors stop when the game is paused, closed or restarted. RetroArch's **Vibration Strength** (Settings → Input → Haptic Feedback/Vibration) scales it, as for every core.
- **Render Scale 2x by default** — Games draw at twice the Xbox's resolution, 1280x960 for most. Text and edges are sharper and no longer uneven on a 1280x960 screen. The overscan stays cropped, so the picture fills a 4:3 screen as on a CRT. Choose **1x** for a game that runs slowly.

### Current limitations

- Save states are not yet available.
- Upstream's LAN and XLink Kai backends are included, but this release does not add network settings to RetroArch. Networking is off by default.
- CD CHDs (`chdman createcd`) and CHDs that are deltas of another are refused: an Xbox disc is a DVD.
- Full disc dumps (Redump), as `.iso` or CHD, haven't been tested with a real game yet.

## linux-v0.7.2 (PortareOS)

Every frame the game draws reaches RetroArch.

### What's fixed

- **Frames shown twice and skipped** — The core took each frame from the game at a moment it estimated from the start of RetroArch's frame, not when it actually looked. When RetroArch spent a varying time first, for example waiting on audio with audio sync on, the game's next frame sometimes arrived just after the look: one frame was shown twice and the next one never. ROCKNIX's RetroArch showed 47.5 new frames a second of the game's 59.9. The moment is now taken when the frame is read. PortareOS's own settings were not affected.

### Current limitations

- Rumble is not passed on to the controllers yet.
- Save states are not yet available.
- Upstream's LAN and XLink Kai backends are included, but this release does not add network settings to RetroArch. Networking is off by default.
- CD CHDs (`chdman createcd`) and CHDs that are deltas of another are refused: an Xbox disc is a DVD.
- Full disc dumps (Redump), as `.iso` or CHD, haven't been tested with a real game yet.

## linux-v0.7.1 (PortareOS)

Sound after a game starts again in RetroArch.

### What's fixed

- **Sound after a restart** — After RetroArch's Restart, a video driver rebuild, or a game switching to another of its programs (Jade Empire after New Game), the game went on without sound. Each new boot now gets its audio output set up again. Reported, with the fix and a test, by the pocket-ui project.
- **Build image** — The pinned Debian image now has a Vulkan loader with Mesa's drivers and `llvm-strip-19`, so `linux-build.sh`'s checks and `linux-package.sh` run in it without extra packages.

### Current limitations

- Rumble is not passed on to the controllers yet.
- Save states are not yet available.
- Upstream's LAN and XLink Kai backends are included, but this release does not add network settings to RetroArch. Networking is off by default.
- CD CHDs (`chdman createcd`) and CHDs that are deltas of another are refused: an Xbox disc is a DVD.
- Full disc dumps (Redump), as `.iso` or CHD, haven't been tested with a real game yet.

## linux-v0.7.0 (PortareOS)

Upstream hakuX 1.2.0, with game compatibility and sound fixes.

### What's new

- **Upstream hakuX 1.2.0** — Includes upstream fixes for Doom 3, Fatal Frame II, Silent Hill 2, Splinter Cell: Double Agent, Ultimate Spider-Man, Grand Theft Auto III, Super Monkey Ball Deluxe, Sega GT 2002, Project Gotham Racing 2 and Dungeons & Dragons Heroes. These game fixes have not been checked on the Nova in this release.
- **Sound** — Includes upstream's fixes for games freezing while they hold audio voices, or continuing to play a voice after stopping it.
- **Linux integration** — The new network and Ethernet controller threads stop with the game, so closing, restarting and loading another game release their resources. Upstream's controller updates use the same hot-plugging state as RetroArch's ports.
- **Getting started** — The README covers installation, game formats, controls and troubleshooting on PortareOS and other ARM64 Linux systems.

### Current limitations

- Rumble is not passed on to the controllers yet.
- Save states are not yet available.
- Upstream's LAN and XLink Kai backends are included, but this release does not add network settings to RetroArch. Networking is off by default.
- CD CHDs (`chdman createcd`) and CHDs that are deltas of another are refused: an Xbox disc is a DVD.
- Full disc dumps (Redump), as `.iso` or CHD, haven't been tested with a real game yet.

## linux-v0.6.0 (PortareOS)

CHD disc images in the RetroArch core.

### What's new

- **CHD disc images** — Games can be `.chd` files made with `chdman createdvd` from an `.iso` image. The core reads them in place, without unpacking them. A CHD takes about a third less space than its `.iso` (Burnout 3: 2.48 GB to 1.49 GB), and the game runs from it as fast as from the `.iso`.

### Fixed

- **Discs that can't be read** — When a game's disc image can't be read, the core closes its file once, not a second time when the game stops.

### Current limitations

- Rumble is not passed on to the controllers yet.
- Save states are not yet available.
- CD CHDs (`chdman createcd`) and CHDs that are deltas of another are refused: an Xbox disc is a DVD.
- Full disc dumps (Redump), as `.iso` or CHD, haven't been tested with a real game yet.

## linux-v0.5.0 (PortareOS)

Players 2-4 in the RetroArch core.

### What's new

- **Players 2-4** — RetroArch's ports 1-4 are players 1-4: a second controller plays as player 2, and so on up to four. Each port has a controller plugged in unless its Device Type (Quick Menu → Controls → Port N Controls) is None.
- **Plugging in and out** — Changing a port's Device Type during the game plugs its controller in or pulls it out, and the game sees it as on a console: most games pause when player 1's controller goes. The ports stay so when the game restarts.

### Current limitations

- Rumble is not passed on to the controllers yet.
- Save states are not yet available.
- Games must be `.iso` disc images.

## linux-v0.4.0 (PortareOS)

The RetroArch core stops and starts games inside RetroArch, and the Linux standalone is gone.

### What's new

- **Restart** — RetroArch's Restart starts the game again inside RetroArch.
- **Games that start another of their programs** — They continue inside RetroArch without restarting it.
- **Closing a game** — The core stops the game inside RetroArch. On PortareOS, RetroArch then returns to the launcher, as with every core; elsewhere it can load another game in the same session.
- **Video driver restarts** — If RetroArch restarts its video driver, the game starts again with a message, instead of RetroArch crashing.
- **No freeze on closing** — If stopping a game ever takes longer than 10 seconds, RetroArch closes instead of freezing.
- **Testing instructions** — [TESTING.md](TESTING.md) covers installing a release, logs and what to check, on PortareOS and on other ARM64 Linux systems.

### Removed

- **The Linux standalone `hakux`** — It wasn't tested, and PortareOS no longer used it. The release now holds only the RetroArch core.

### Current limitations

- Only player 1 is supported.
- Save states are not yet available.
- Games must be `.iso` disc images.

## linux-v0.3.0 (PortareOS)

Core options for the RetroArch core, and upstream hakuX 1.1.0.

### What's new

- **Core options** — RetroArch's Quick Menu has options for the render scale (1x to 4x) and the console's settings: screen format (widescreen games then show 16:9), language (RetroArch's by default) and video region. They take effect the next time the game starts.
- **Overscan crop option** — The border a TV hid is still cropped by default; turning the option off shows the whole picture, at once.
- **PAL games at 60 Hz** — European games that offer 60 Hz now run at it.
- **Upstream hakuX 1.1.0** — Includes upstream's emulator changes up to its version 1.1.0, mainly support for GPUs other than Adreno. Adreno GPUs, such as the Nova's, are unchanged.

### Current limitations

- Closing a game also closes RetroArch.
- Only player 1 is supported.
- Reset and save states are not yet available.
- Games must be XISO disc images with a `.iso` extension.

## linux-v0.2.1 (PortareOS)

Two fixes for the RetroArch core.

### What's fixed

- **Games that switch programs** — When a game starts another of its programs, RetroArch restarts on the same game and continues into it instead of returning to the launcher.
- **Video driver restarts** — If RetroArch restarts its video driver while a game is running, the game now closes and returns to the launcher, with a log entry, instead of possibly crashing.

### Current limitations

- Closing a game also closes RetroArch.
- Only player 1 is supported.
- Reset, core settings and save states are not yet available.
- Games must be XISO disc images with a `.iso` extension.

## linux-v0.2.0 (PortareOS)

Play Xbox games through RetroArch on PortareOS (Retroid Pocket Nova).

### What's new

- **RetroArch support** — Run hakuX as a RetroArch core, with access to RetroArch's menu, hotkeys and controller remapping.
- **Display timing** — RetroArch handles when frames appear on screen, including variable refresh rates and compensation for low frame rates. Graphics stay on the GPU throughout.
- **Xbox timing** — Matches the original Xbox's 59.94 Hz video timing and keeps audio in sync.
- **4:3 display** — Removes the border normally hidden by a TV, so standard-definition games fill a 4:3 screen.
- **Game saves** — Stores the emulated Xbox hard drive in RetroArch's save folder (`/storage/saves/xbox` on PortareOS).
- **Performance logs** — Optional logs show frame rates, frame delivery delays and the core's processing time per frame. Enable them with `HAKUX_DISP_STATS` and `HAKUX_LR_STATS`.

### Current limitations

- Closing a game also closes RetroArch.
- Only player 1 is supported.
- Reset, core settings and save states are not yet available.
- Games must be XISO disc images with a `.iso` extension.

## linux-v0.1.0 (PortareOS)

The first standalone hakuX release for PortareOS on the Retroid Pocket Nova, replacing xemu.

### What's new

- **Standalone emulator** — Play fullscreen with `hakux` on ARM64 Linux (glibc). A command-line version, `xboxrun`, is also available for testing without a display.
- **Audio** — Game audio plays through PipeWire.
- **Controllers** — All connected gamepads control player 1. Press **Home + Start** to quit.
- **PortareOS package** — Adds a script to create reproducible release archives for installation on PortareOS.

## v1.2.0

System link, stability and compatibility improvements.

### Network
- **System link over your network** — Settings → Network → System link → LAN: hakuX devices on the same Wi-Fi network find each other as Xbox consoles on a LAN do. Devices the network's broadcasts don't reach, on another subnet or a VPN such as ZeroTier or Tailscale, are added by address under Other consoles
- **XLink Kai** — system link over the Internet through an XLink Kai engine running on a computer on your network; its web page picks the game's arena
- **Console identity** — each console has its own serial number, network address and online key. A device whose hard disk was copied from another one offers to make itself a new identity, since two consoles with one identity can't see each other


### Games
- **Sega GT 2002** — the cars have their paint, reflections and sun highlights
- **Doom 3** — no longer crashes as its main menu loads
- **Fatal Frame II** — starts and reads its disc in the order it asks for
- **AirForce Delta Storm** — answers the controller and the on-screen controls (it ignored all input)
- **Silent Hill 2** — no longer crashes when it loads a room
- **Splinter Cell: Double Agent** — no longer closes on boot
- **Super Monkey Ball Deluxe** — its title movie plays and its movies aren't black
- **Ultimate Spider-Man** — no longer crashes a second into boot
- **Grand Theft Auto III** — no longer shows its bad-disc screen
- **Project Gotham Racing 2** — the attract replay no longer goes black
- **Dungeons & Dragons Heroes** — the tutorial's prompts show
- **Sound** — games that hold their voices while the sound engine updates them, or stop a voice as it changes, no longer freeze or play a stopped voice on

### Performance
- **Fewer stutters with Turnip** — frames reach the screen without a CPU wait in front of each one

### App
- **Controllers** — only the players with a controller are plugged into the console (player 1 always, for the on-screen controls), and a controller plugs in or out as it connects, disconnects or is assigned to another player, without restarting the game

## v1.1.0

GPUs beyond Adreno (experimental).

### GPUs
- **Mali, Xclipse and PowerVR (experimental)** — hakuX runs on GPUs other than Adreno: features they lack are replaced (BC and 4444 textures decoded up front, standard border colours, the push block in a buffer, D32S8 depth), and known driver bugs are worked around (`hw/xbox/hle/vk/quirks.c`). Burnout 3 and Mercenaries run cleanly on a Pixel 8 (Mali-G715)
- **Vulkan 1.1 drivers** — GPUs whose drivers stop at Vulkan 1.1 (Valhall Mali on older or vendor-capped drivers, Qualcomm's own older drivers, early Xclipse, PowerVR BXM) run through a compatibility layer (`hw/xbox/hle/vk/compat.c`)
- **Older GPUs without descriptor indexing (experimental)** — Bifrost Mali (G71, G72, G76, G52) and PowerVR Rogue: each draw binds its own textures, and depth is clamped in the shader where the GPU can't
- **Adreno** — unchanged: every replacement is off where the GPU has the feature.

### App
- **GPU driver** — a screen of its own (Settings → GPU driver): the system driver and the stored packages in one list, with their versions and authors. Packages are checked when added and can be deleted (deleting the one in use goes back to the system driver)
- **Turnip recommended** — a Qualcomm phone running its own driver older than Vulkan 1.3 suggests a Turnip driver once, from the library
- **Back key** — the device's back key opens the in-game menu, from any input, including the Retroid Pocket 5's own Back. A controller with both Back and Select keeps Back for the menu; on one without a Select key, Back is the Xbox Back button
- **Dark theme** — Settings → Display → App theme: the phone's, light or dark; the in-game menus follow it, in the app's green rather than Android's teal
- **Controller navigation** — the app works with a controller: the focused item stands out (a game is outlined in green), A selects, B goes back, and Y or Select opens a game's menu. The search field opens the keyboard only on A, the library has a refresh button, the mapping sliders move with left and right, release notes scroll, and X deletes a savestate from the in-game Load state list

## v1.0.0

A new core replaces xemu's QEMU-based one.

### New core
- **CPU** — the Xbox's x86 code runs on a FEX JIT (FEXCore fork), and the Xbox kernel is reimplemented natively (`native/kernel`): no QEMU
- **GPU** — the NV2A push buffer is decoded into Vulkan (HLE, `hw/xbox/hle`). Custom drivers (Turnip) load through adrenotools; driver-specific workarounds are quirks (`hw/xbox/hle/vk/quirks.c`, `debug.hakux.quirks`)
- **Audio** — the MCPX APU runs on the core's own services (`native/dev/apu`), out through AAudio
- **Savestates** — per game, in slots, with the hard disk's files
- **Mods and cheats** — per game, from `mods/` (library long-press "Mods & cheats"): XBE patches and replaced disc files; 60 and 120 fps mods for 13 games (`mods/README.md`, generators in `tools/mods`)
- **Controllers** — each physical controller routed to a player with its own mapping; the on-screen controller adds to player 1

### New features
- **Updates** — hakuX looks for a newer release on GitHub when it starts and on demand (Settings → Updates); one found shows its notes and installs from there
- **Frontends** — a game launched from ES-DE or another frontend returns to it when it closes
- **Console clock** — follows the phone's time and time zone, or a time set by hand (Settings → Console → Clock)
- **Start errors** — a game that can't start says why (the file can't be read, isn't an Xbox disc, or its program can't load) and goes back to the library
- **Mods** — Halo 2's 60 fps mod draws characters, vehicles and objects at 60; DOA2's 120 fps mod keeps the select screen's legend

### Performance
- **No pipeline-cache stutter** — each game keeps its own pipeline cache, written only after new compiles; the one shared by every game was rewritten every few seconds and froze the picture for 0.2–0.8 s (Ninja Gaiden Black)
- **Audio** — about 32 ms of latency instead of 128 ms, and less CPU for the sound engine
- **GPU emulation** — faster push-buffer decoding, texture unswizzling (NEON) and hashing, fewer lookups per draw
- **Savestates** — compressed on several threads (a Halo 2 save: 9.6 s to 3.9 s)
- **Idle frames** — the screen isn't redrawn when nothing changed, and the on-screen controller redraws only what changed
- **Disc reads** — a large read waits only its own thread, not the whole game
- **Library** — folders scanned in one pass (38 games: 270 ms to 63 ms)

### App
- **On-screen controller** — the menu button is a green dot
- **Large screens** — Android 16 keeps the landscape lock (the app is declared a game)
- **Storage** — JIT symbol maps left by earlier builds (gigabytes on some devices) are deleted at start
- **Backups** — hakuX's data is left out of Android's cloud backups and device transfers
- **Library** — savestate thumbnails and driver ZIPs load off the main thread

## v0.3.1

### New Features
- **Persistent log capture** — logs kept per session, rotated

### Improvements
- **Texture decompression** — S3TC decoded on several threads; 3D textures no longer use native BC (it corrupted BC3)
- **Surface eviction** — lazy, without a GPU finish per eviction, and no download for VRAM data that is never read
- **Texture cache** — no thrashing from an aggressive memory budget trim
- **Profiling** — all instrumentation behind `NV2A_PERF_LOG` (off by default)
- **XISO conversion** — extract-xiso replaces the Rust xdvdfs converter (no Rust/Cargo dependency)

## v0.3.0

### New Features
- **Per-game settings** — overrides per game in the settings UI
- **Xbox dashboard** — dashboard management, HDD tools, Insignia networking (NAT)
- **Texture dump and replacement**
- **Game compatibility quirks** — a per-game layer (Fable's cycle-breaker)
- **Debug log export** — from the settings
- **Skip boot animation** toggle

### Improvements
- **Vulkan** — native BC texture upload when the GPU has it, batched render sync, NEON blits, descriptor management without bindless textures
- **Vertex shaders** — the emulator stub replaced; uniform uploads optimized
- **Audio** — aligned with x1_box
- **Emulator process** — runs separately again (`:xemu`)
- **XISO** — the original ISO kept after conversion

### Bug Fixes
- Surface eviction VRAM corruption and stale texture sampling
- Vulkan pipeline exhaustion and eviction crashes
- A crash on a quick emulation restart (RCU)
- The 30 fps trap
- XISO conversion launching the original file when SAF renamed the output

## v0.2.1

### Bug Fixes
- **Fix HDD corruption** — restore block device flush on app terminate and background to persist QCOW2 metadata. Cache partition clear now uses synchronous writes
- **Fix game freeze on TB flush** — disable post-flush rewarm on Android which could hang on self-modified code
- **Fix TB cache prewarm crash** — disable startup prewarm on Android. Add build hash to tb_cache.bin for auto-invalidation on new builds
- **Fix on-screen controller hidden on some devices** — filter out virtual/internal devices (accelerometers, gyroscopes) that falsely report as gamepads

### Improvements
- **Simplified FPS overlay** — shows only FPS count
- **Unified log tags** — all logcat output uses hakuX prefix. Filter with: `adb logcat | grep hakuX`
- **HDD cache clear on background thread** — prevents ANR on large images

## v0.2.0

### New Features
- **OpenGL ES renderer** — experimental alternative to Vulkan, selectable in Graphics settings
- **EEPROM editor** — change Xbox language, video standard, resolution modes, aspect ratio, refresh rate
- **Settings index** — reorganized into 4 sections: hakuX Data, Graphics, Debug, EEPROM
- **System file pickers** — change MCPX ROM, Flash ROM, HDD image from settings
- **HDD export** — save a copy of the Xbox HDD to any folder
- **Games folder in settings** — moved from library screen to settings
- **Game search** — search bar with live filtering in game library
- **Pull-to-refresh** — swipe down to rescan games folder
- **XISO progress bar** — real progress tracking across copy/convert/save phases
- **XISO integrity check** — verifies XISO before launch, auto-rebuilds from ISO if corrupt

### Improvements
- **Controller overlay** — D-pad/stick repositioned, LS/RS as separate buttons, menu button, swipe-up gesture, trigger axis fix (-1.0 for release), per-pointer tracking
- **Pause menu** — actually pauses emulation, simplified UI
- **NEON optimizations** — S3TC decompression and texture swizzle (benefits both renderers)
- **XISO conversion** — staged in temp directory, clean up on interruption, write permission check
- **FPS overlay** — shows driver info or "OpenGL ES" label, frame pacing, shader stats
- **Clear code cache** button in Debug settings
- **Graceful VK texture LRU handling** — skip instead of crash when cache exhausted

### Bug Fixes
- Fix TB cache crash when FP settings change between sessions
- Fix pause/resume not working from in-game menu
- Fix GLES geometry shader crash (gl_PointSize unavailable without extension)
- Fix GLES shader version detection (was hardcoded to 320, now auto-detected)
- Fix shader link failure crash (graceful fallback instead of assert)
- Fix shared GLSL changes breaking VK (gated behind renderer check)

### Rebranding
- Package renamed to `com.rfandango.haku_x`
- Debug builds install alongside release (`com.rfandango.haku_x.debug`)

## v0.1.0

- Initial release
