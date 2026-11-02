# Liberty Pro Max

**Grand Theft Auto IV (PC 1.0.8.0) running natively on Apple silicon Macs and iPads, and on Apple TV.**

Liberty Pro Max is a native port of the PC version of GTA IV to Mac and iPad (and, experimentally, Apple TV),
made by statically recompiling the game's own x86 code with
[x86recomp](https://github.com/JTM-rootstorm/x86recomp). The executable is
translated to C++ and compiled for Apple silicon, so the game runs as it does
on Windows, with no Wine, no emulation and no virtual machine. Only the
platform underneath is new, and it is built for Apple's devices: a Metal
renderer, a real-time audio mixer, a launcher with all of the port's options,
Game Mode, ultrawide screens on the Mac.

> **This repository contains no part of the game.** You build it on your own
> Mac from your own copy of GTA IV, for the Mac and, if you like, your iPad.

**Beta.** The story can be played from a save and around the city on both.
Expect rough edges, and please report what you find.

## What you get

| | Mac | iPad |
|---|---|---|
| **Renderer** | Metal, written for this port: drawing encoded on its own thread, geometry read in place from the game's memory, several frames in flight | the same |
| **Frame rate** | about 125 fps indoors, 60–100 fps outdoors at 3440×1440, maximum settings, M3 Pro; no 30/60 fps cap | 60 fps indoors, 45–60 outdoors at 1792×1344, iPad Air (M4) |
| **Anti-aliasing** | TAA (default, sharpened), SMAA, MetalFX temporal and SMAA + MetalFX spatial upscaling | SMAA (default), TAA |
| **Screens** | any resolution, ultrawide included, field of view, full screen as a macOS Space with Game Mode | 1536×1152 to 2732×2048, field of view |
| **Launcher** | a window with all options before the game | a settings screen with all options and **Play**; the icon is the game's own IV |
| **Fixes built in** | about 100 fixes from FusionFix and ZolikaPatch (high frame rate physics, cameras, timers, lights, mirrors, water), each a switch in the launcher | the same |
| **Extras** | the original 2008 radio songs (with the Radio Restoration mod), stun punch and executions on anyone, fast loading | the same |
| **Controls** | keyboard and mouse, controllers | controllers, keyboard (no touch controls) |

### Speed

Measured on an M3 Pro at 3440×1440, maximum settings, on the same street:

| Stage | fps |
|---|---|
| First build, generic SDL GPU renderer | 20–52 |
| Own Metal renderer | 34–88 |
| Native SSE math, host CRT math, PGO, ThinLTO | 71–83 |
| Physics and job threads helped by the main thread, 397 hot functions translated to plain C++ | about 90 on average |

## What you need

- **GTA IV for PC, version 1.0.8.0**: the folder with `GTAIV.exe`
  (SHA-256 `3d90e7c516fa450ca002e5031e62c0f66b404590f33e6cc9793b0da4fffbfd0f`)
  and the game's data next to it (`pc/`, `common/`, …). The Steam Complete
  Edition (1.2.0.x) is not supported as it is; the community
  [GTA Downgraders](https://github.com/Tomasak/GTA-Downgraders) turn it into
  1.0.8.0.
- **A Mac with Apple silicon** (M1 or newer), macOS 14 or newer, with the
  **Xcode Command Line Tools** (`xcode-select --install`) and
  [Homebrew](https://brew.sh); about 6 GB free for the build. The Mac builds
  both versions.
- **For the iPad**, also:
  - an iPad with an M-series chip, iPadOS 17 or newer, in Developer Mode and
    connected to the Mac by cable, with about 16 GB free;
  - full **Xcode** with a **paid Apple developer account** signed in
    (Xcode › Settings › Accounts): the app needs the *Extended Virtual
    Addressing* and *Increased Memory Limit* capabilities, which free
    accounts do not have;
  - a controller or a keyboard.
- **For the Apple TV** (experimental): an Apple TV 4K (3rd generation,
  A15) with tvOS 17 or newer, paired with Xcode (Window › Devices and
  Simulators) and in Developer Mode, on Ethernet (the game's 15 GB go over
  the network), about 16 GB free; the same paid account; a controller.

## Install

### Mac

```bash
git clone https://github.com/Awis13/liberty-pro-max
cd liberty-pro-max
./install.sh "/path/to/Grand Theft Auto IV"
```

`install.sh` installs the build tools from Homebrew, fetches the pinned
x86recomp, checks your `GTAIV.exe`, translates the game's code, builds, and
puts **Liberty Pro Max** in `/Applications`. The first run takes about 15
minutes on an M-series Mac. Run it again after `git pull` to update.

Then open **Liberty Pro Max**, pick the game folder in the launcher, and
press **Play**. Saves and settings are kept in
`~/Library/Application Support/gtaiv`; the game's log is
`~/Library/Logs/gtaiv.log`.

### iPad

After the Mac install (the iPad build uses the game's code it translated),
with the iPad connected:

```bash
./install-ios.sh YOUR_TEAM_ID
```

`install-ios.sh` builds the iPad's libraries (SDL3, FFmpeg, glslang,
SPIRV-Cross), makes a signing profile with those capabilities, builds the app
with its icon and installs it, then copies the game's files (15 GB) into it
over the cable, and the restored radio songs if you imported them on the Mac.
The first run takes about 40 minutes.

Then open **Liberty Pro Max** on the iPad, check the settings, press
**Play**. The defaults are 1792×1344 with SMAA at 60 fps; the game's own
graphics menu decides the rest: lower its view and detail distance if busy
streets drop below 60. The game's log is `log.txt` in the app's Documents
(Files › On My iPad, or `devicectl`).

### Apple TV (experimental)

After the Mac install, with the Apple TV paired:

```bash
./install-ios.sh --tv YOUR_TEAM_ID
```

The same steps as for the iPad, built for tvOS. The app has no settings
screen: it starts the game at 1920×1080 with SMAA at 60 fps. The A15 cannot
sample the game's DXT textures, so they are decoded on upload and take
several times their size: keep the game's texture quality at its default,
or the Apple TV's 2.1 GB per app runs out. The log is `log.txt` in the
app's Library/Caches (`devicectl`).

### Recommended (Mac)

Put a `commandline.txt` with `-norestrictions -availablevidmem 2047` in the
game folder to unlock the highest graphics settings. (The iPad's launcher
passes both itself.)

### Original radio songs

The 2018 update removed many songs from the radio. Download the
[Radio Restoration mod](https://github.com/Tomasak/GTA-Downgraders) by
Tomasak / Fusion Team, unpack it, then:

```bash
GTAIV_RADIO_SRC=/path/to/unpacked/mod tools/radio_import.sh
```

and turn on **Original 2008 radio songs** in the launcher. Your game folder is
not changed. Import it before `install-ios.sh` to have the songs on the iPad
too.

## Build by hand

```bash
brew install cmake ninja sdl3 ffmpeg llvm@22 glslang spirv-cross python
git clone --branch liberty https://github.com/Awis13/x86recomp vendor/x86recomp
git -C vendor/x86recomp checkout "$(cut -d' ' -f2 X86RECOMP)"
export GTAIV_GAME_DIR="/path/to/Grand Theft Auto IV"

cmake --preset release && cmake --build --preset release   # the lifter and runtime
tools/lift.sh                                              # the game's code to C++ (generated/)
cmake --preset release && cmake --build --preset release   # the game
```

The app is `out/build/release/Liberty Pro Max.app`; the plain executable,
`out/build/release/gtaiv --game-dir "$GTAIV_GAME_DIR"`, starts without the
launcher.

For the iPad, after that: `tools/apple_deps.sh`, `cmake --preset iphoneos &&
cmake --build --preset iphoneos`, then `tools/ios_profile.sh`,
`tools/make_ios_icon.py`, `tools/ios_bundle.sh <device>` and
`tools/ios_push_game.py` (each script says what it takes; `install-ios.sh`
runs them in order).

For the Apple TV, the same with `PLATFORMS=tvos tools/apple_deps.sh`, the
preset `appletvos` and `PLATFORM=tvos` for `tools/ios_profile.sh` and
`tools/ios_bundle.sh`.

## How it works

- `symbols/` holds the addresses and names of the game's functions (exported
  from Ghidra), and `manifest/gtaiv.manifest` lists this port's changes to the
  game. The lifter reads both and writes the game's code as C++ into
  `generated/`, which is never committed.
- `manifest/native.list` names the hottest functions; `tools/x86n.py`
  translates them from your `GTAIV.exe` into plain C++ (registers as local
  variables, flags only where read) for speed.
- `src/` is the port: the game's settings and fixes, the launcher, the
  protection layer and Games for Windows Live answered offline, thread
  priorities, and the FusionFix and ZolikaPatch fixes.
- `src/launcher_mac.mm` and `src/launcher_ios.mm` are the two launchers;
  `src/launcher_options.h` the options they share.
- The Metal renderer, audio and Windows layer live in x86recomp, on the
  [`liberty`](https://github.com/Awis13/x86recomp/tree/liberty) branch pinned
  in `X86RECOMP`.

## Known limits

- Mac, iPad and (experimental) Apple TV for now. Linux and the Steam Deck are planned.
- Apple TV: no settings screen and no icon yet; high texture quality does
  not fit its memory (DXT decoded on upload).
- iPad: no touch controls; busy streets need the game's distances lowered
  for a steady 60 fps.
- Multiplayer is not available.
- MetalFX temporal upscaling costs about 5 ms a frame at 3440×1440 output,
  more than it saves in this game; TAA at native resolution is the default.
- A few seconds of stutter right after loading, while the city streams in.

## Credits

- [x86recomp](https://github.com/JTM-rootstorm/x86recomp), the static
  recompiler this port is built on.
- [FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) by
  ThirteenAG and contributors, and ZolikaPatch by Zolika1351: the fixes ported
  here follow theirs.
- [WidescreenFixesPack](https://github.com/ThirteenAG/WidescreenFixesPack) by
  ThirteenAG (the ultrawide fix).
- XLivelessAddon and the GTA modding community, for how the PC version's
  launcher checks work.
- The Radio Restoration mod and GTA Downgraders by Tomasak / Fusion Team.

Grand Theft Auto IV is a trademark of Take-Two Interactive Software. This
project is not affiliated with or endorsed by Rockstar Games or Take-Two.

## License

GPL-3.0 (see `LICENSE`), as several fixes follow FusionFix's code.
