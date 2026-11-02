# Liberty Pro Max

**Grand Theft Auto IV (PC 1.0.8.0) running natively on Apple silicon Macs and iPads.**

Liberty Pro Max is a native macOS port of the PC version of GTA IV, made by
statically recompiling the game's own x86 code with
[x86recomp](https://github.com/JTM-rootstorm/x86recomp). The executable is
translated to C++ and compiled for Apple silicon, so the game runs as it does
on Windows, with no Wine, no emulation and no virtual machine. Only the
platform underneath is new, and it is built for the Mac: a Metal renderer,
a real-time audio mixer, Game Mode, ultrawide screens.

> **This repository contains no part of the game.** You build it on your own
> Mac from your own copy of GTA IV.

**Beta.** The story can be played from a save and around the city. Expect
rough edges, and please report what you find.

## What you get

| | |
|---|---|
| **Renderer** | Metal, written for this port: drawing is encoded on its own thread, geometry is read in place from the game's memory, several frames are in flight |
| **Frame rate** | about 125 fps indoors and 60–100 fps outdoors at 3440×1440 with maximum settings on an M3 Pro, no 30/60 fps cap |
| **Anti-aliasing** | TAA (default, sharpened), SMAA, MetalFX temporal and SMAA + MetalFX spatial upscaling |
| **Screens** | any resolution, ultrawide included, field-of-view option, full screen as a macOS Space with Game Mode |
| **Fixes built in** | about 100 fixes from FusionFix and ZolikaPatch (high frame rate physics, cameras, timers, lights, mirrors, water), each a switch in the launcher |
| **Extras** | the original 2008 radio songs (with the Radio Restoration mod), stun punch and executions on anyone, fast loading |
| **Launcher** | all of the port's options in one window before the game starts |

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
- **A Mac with Apple silicon** (M1 or newer) and macOS 14 or newer.
- **Xcode Command Line Tools** (`xcode-select --install`) and
  [Homebrew](https://brew.sh).
- About 6 GB free for the build.

## Install

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

Liberty Pro Max also runs on iPad (tested on an iPad Air with M4): the same
game code, a settings screen before the game with **Play**, the app icon made
from the game's own logo.

You need, besides the Mac install above:

- an iPad with an M-series chip, connected to the Mac by cable, in
  Developer Mode;
- full **Xcode** with a **paid Apple developer account** signed in
  (Xcode › Settings › Accounts): the app needs the *Extended Virtual
  Addressing* and *Increased Memory Limit* capabilities, which free accounts
  do not have;
- about 16 GB free on the iPad.

```bash
./install.sh "/path/to/Grand Theft Auto IV"   # first, if not done yet
./install-ios.sh YOUR_TEAM_ID
```

`install-ios.sh` builds the iPad's libraries (SDL3, FFmpeg, glslang,
SPIRV-Cross), makes a signing profile with those capabilities, builds and
installs the app, then copies the game's files (15 GB) into it over the
cable. The first run takes about 40 minutes. A controller or a keyboard
plays the game; there are no touch controls.

On the iPad the defaults are 1792×1344 with SMAA at 60 fps. The game's own
graphics menu decides the rest: lower its view and detail distance if busy
streets drop below 60.

### Recommended

Put a `commandline.txt` with `-norestrictions -availablevidmem 2047` in the
game folder to unlock the highest graphics settings.

### Original radio songs

The 2018 update removed many songs from the radio. Download the
[Radio Restoration mod](https://github.com/Tomasak/GTA-Downgraders) by
Tomasak / Fusion Team, unpack it, then:

```bash
GTAIV_RADIO_SRC=/path/to/unpacked/mod tools/radio_import.sh
```

and turn on **Original 2008 radio songs** in the launcher. Your game folder is
not changed.

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
- The Metal renderer, audio and Windows layer live in x86recomp, on the
  [`liberty`](https://github.com/Awis13/x86recomp/tree/liberty) branch pinned
  in `X86RECOMP`.

## Known limits

- Mac and iPad for now. Linux (and the Steam Deck) and Apple TV are planned.
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
