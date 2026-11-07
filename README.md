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
| **Extras** | stun punch and executions on anyone, fast loading, a mods folder | the same |
| **Controls** | keyboard and mouse, controllers | touch controls, controllers, keyboard |

### Speed

Measured on an M3 Pro at 3440×1440, maximum settings, on the same street:

| Stage | fps |
|---|---|
| First build, generic SDL GPU renderer | 20–52 |
| Own Metal renderer | 34–88 |
| Native SSE math, host CRT math, PGO, ThinLTO | 71–83 |
| Physics and job threads helped by the main thread, 397 hot functions translated to plain C++ | about 90 on average |

## Fixes and options

On the launcher's **General** tab: resolution and display, anti-aliasing
(TAA, SMAA, MetalFX), shadow and reflection resolution, field of view, fast
loading, skipping the logo screens, continuing the last save, mods, the
performance overlay, on a display with a notch the whole screen. On the **Advanced** tab, every fix
below, each a switch. Most follow FusionFix's and ZolikaPatch's (by what
they do; their code is not used), some are this port's own.

<details>
<summary>All fixes and options</summary>

<!-- fixes:begin -->
101 switches and choices in the launcher (Advanced tab; the iPad has them on its settings screen). Each one is also an environment variable, `GTAIV_` and the name in capitals in `src/launcher_options.h`.

#### High frame rates

The game was made for 30 fps; these keep it right at any frame rate.

| Option | Default |
|---|---|
| Ladders: no stalls before climbing at high frame rates | on |
| No speed-up above 300 fps | on |
| Vehicle and ragdoll physics steady at high frame rates | on |
| Scripted doors and gates move at the right speed | on |
| Camera shake as strong at any frame rate | on |
| Helicopter rotors break as easily at any frame rate | on |
| Loading screen pictures move at console speed | on |
| 30 fps in pool, air hockey, arm wrestling, bowling, drinking | on |
| Radar moves smoothly at high frame rates | on |
| Bonnet camera sway smooth at high frame rates | on |
| Final mission: helicopter climb not harder at high frame rates | on |
| Cutscene animations smooth and in sync with their audio | on |
| No water flicker (buffers swapped in step with rendering) | on |

#### Cameras

| Option | Default |
|---|---|
| No forced camera centring at the edge of cover | on |
| Camera shake (hand-held, drunk, vehicles) | on |
| Cutscene cameras without zoom fits and flicker at cuts | on |
| Car camera swings back faster with keyboard and mouse | on |
| Cinematic camera when vaulting walls and jacking cars | on |
| Cinematic camera and slow motion on stunt jumps | on |
| On foot, the camera waits 2 s after the mouse before recentring | on |
| Car camera centred behind the car | off |
| On-foot camera centred behind Niko | off |
| On foot with a gamepad: camera waits 2 s before recentring | off |
| In vehicles: camera waits 2 s before recentring | off |
| Aiming zoom remembered between aims (as on Xbox) | off |
| First-person helicopter reticle with a gamepad | on |
| Stunt jumps: mouse wheel sets the slow motion | on |
| Video editor: free camera without bounds | on |
| Drive-by shots land in the crosshair when high up | on |
| No motion blur flicker while the camera shakes (drunk) | on |

#### Wide screens and the interface

| Option | Default |
|---|---|
| Pause menu map fits wide screens | on |
| Cutscenes framed 16:9 with side bars on wide screens | on |
| In-game internet and police computer fit wide screens | on |
| Mouse cursor scaled to the screen | on |
| Menu cursor hidden while playing with a gamepad | on |
| Target health on the reticle with the mouse | on |
| Radar stays zoomed out for 3 seconds | on |
| Pause menu opens and closes without delay | on |
| Right click goes back in menus and steps an option down | on |
| Pause menu: version, IMG files and streaming memory | off |
| Pause menu time and money spaced as on consoles | on |
| Menu lists show 32 rows before scrolling | on |

#### FusionFix

Its mission and model fixes need its update folder in the game folder. Its shaders do not work in the port yet, so their options are not listed.

| Option | Default |
|---|---|
| Use FusionFix's mission and model fixes if installed (update folder) | on |

#### Lights, shadows and effects

| Option | Default |
|---|---|
| Neon signs glow from every angle (the game's depth shift corrected) | on |
| Distant city lights as on consoles (from 7 pm, street level) | on |
| Reflections and car dirt as on consoles | on |
| Mirrors show decals near them | on |
| Water surface tiling as on consoles | on |
| No flickering lights where there are many (corona limit 768 -> 49152) | on |
| Reversing lights at the back of the car | on |
| Rain lit as on consoles (visible at night) | on |
| Police helicopter searchlights do not flicker | on |
| Night shadows setting sets their sharpness | on |
| Brake lights stay on while your car stands | off |
| Bloom (glow around bright lights) | on |
| Depth of field blur | on |
| Motion blur: Off, 1/8, 1/4, 1/2, Full | 1/4 |
| Glowing lights and signs without depth artifacts | on |
| Every light casts moving shadows (very slow) | off |
| Shadow under cars at any distance | on |
| No shadow patch under cars | off |
| No shadow patch under cars in cutscenes | off |
| High detail reflections (heavy for the GPU) | off |
| Red and amber glass from broken tail lights and indicators | on |
| Tracer on every shot | off |
| TBoGT: tracers drawn in the right place | on |
| Turn indicators when steering, hazard lights after a crash | off |
| Turn indicators by hand: [ and ] or bumpers, both for hazards | off |

#### Gameplay

| Option | Default |
|---|---|
| Weapon recoil with the mouse as with a gamepad | on |
| Niko takes full damage (the game gives him a third of it) | on |
| Health from a save at most a full bar (trainer saves) | on |
| Saving from the phone works in vehicles | on |
| Radio keeps playing after cutscenes | on |
| Cars do not swerve under trains | on |
| Date does not go back after dying or arrest | on |
| Helicopters fly up to 770 m (as in TBoGT) | off |
| Cops take off their hats in vehicles (as on consoles) | on |
| No double death animation after a melee counter | on |
| Bikes: feet down when stopped on the phone and when reversing | on |
| Walk while zoomed in with a sniper rifle | on |
| Climb ladders by walking into them | off |
| Death and arrest music (cut on PC) | off |
| Cars roll to a stop instead of freezing (costs CPU) | off |
| Parked vehicles keep their wheels turned | off |
| Always run (tap sprint: run or jog, hold: sprint) | off |
| Sniper rifles aim over the shoulder; Jump toggles the scope | off |
| Distant traffic on every road at any height | on |
| Phone cheats: beta gloves (458-555-0100), beta hair (288-555-0100) | on |
| Missions play the alternative car banter | off |

#### Limits, streaming and stability

| Option | Default |
|---|---|
| Draw distance settings apply from the start (costs fps) | off |
| Twice the vehicle budget (no streets full of taxis) | on |
| Bigger task and pointer pools | on |
| No crash in "In the Crosshairs" at long draw distance | on |
| Streaming not held back by the memory budget | on |
| Extra streaming memory while there is room | off |
| No script crash comparing missing strings (TLAD calls) | on |
| Game errors only to the log, no error box (may crash later) | off |
| No crash aiming thrown weapons with a ped that has no pad | on |

#### Mac, iPad and Apple TV

| Option | Default |
|---|---|
| Saves synced with your iPad and Apple TV at start and quit (needs Xcode) | on |
| Keep running when the window is not in front | off |

#### Other

| Option | Default |
|---|---|
| The episodes, when their folders are in the game's | on |

<!-- fixes:end -->

</details>

Flick the left stick to set an indicator before a turn; flick the same way to turn it off.

Not yet here from FusionFix: its ambient occlusion (SAO), console gamma,
HDR output, depth effects through transparent surfaces (pre-alpha depth),
MSAA in reflections, gamepad button icons, camera sensitivity ranges and
ragdoll on demand. The episodes' vehicles and weapons in IV are not
here yet. On a Mac FusionFix's raw input, keyboard
lighting, graphics API choice and plugin loaders have nothing to do.

## What you need

- **Your own GTA IV for PC, version 1.0.8.0**: the folder with `GTAIV.exe`
  (SHA-256 `3d90e7c516fa450ca002e5031e62c0f66b404590f33e6cc9793b0da4fffbfd0f`)
  and the game's data next to it (`pc/`, `common/`, …). Only this file, as
  Rockstar signed it, works: a changed or cracked exe does not. The Complete
  Edition (1.2.0.x) is not supported.
- **A Mac with Apple silicon** (M1 or newer), macOS 15 or newer, with the
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

### Mac, without the Terminal: Liberty Pro Max Builder

Download **Liberty Pro Max Builder** from the
[releases](https://github.com/Awis13/liberty-pro-max/releases) and open it. It
is not signed by Apple, so macOS blocks it the first time: go to System Settings,
Privacy & Security, and press **Open Anyway**. Then:

1. drop your Grand Theft Auto IV folder (or `GTAIV.exe`) on it;
2. if asked, install Apple's Command Line Tools with its button (free);
3. press **Build Liberty Pro Max**.

About 5 to 20 minutes later **Liberty Pro Max** is in Applications. No
Homebrew and nothing else to install: the Builder carries everything that
is not the game's code (the runtime, its libraries, the translator) and
compiles only what it translates from your own `GTAIV.exe`, with Apple's
compiler. `builder/make_builder.sh` makes it.

### Mac, from the Terminal

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
over the cable, and your mods if you have any on the Mac.
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

### Saves on every device

With iCloud Drive on, the iPad keeps a copy of every save in its iCloud
folder (**Liberty Pro Max** in iCloud Drive) and the Mac uses the same
folder: each device takes the newer saves when the game starts and sends
its own while it runs and when it quits. Per save the newer one wins; the
one it replaces, on either side, is kept in that folder's `Backups`.
`install-ios.sh` turns on iCloud for the app (its signing profile then
needs your Apple ID signed in to Xcode). The Apple TV has no iCloud Drive:
the Mac copies its saves to and from it at start and quit, over the cable
or the network, when Xcode is installed. The launcher's "Saves synced"
option turns both off.

### Recommended (Mac)

Put a `commandline.txt` with `-norestrictions -availablevidmem 2047` in the
game folder to unlock the highest graphics settings. (The iPad's launcher
passes both itself.)

### The episodes

If your game folder has the `TLAD` and `TBoGT` folders, The Lost and Damned and
The Ballad of Gay Tony work: the game opens with its own screen to choose what
to play. Their saves sit with IV's, in the same slots. A launcher checkbox turns
them off.

In an episode, the `update` folder gives its archives for that episode, such as
`TLAD/...img`, and those named for all of them (`EFLC`). IV's archives and data
files, and the mods folder's archives, stay out of the episodes, as they stop
them from loading.

### Mods

Each folder in `~/Library/Application Support/gtaiv/drive_c/mods` is a mod (the
launcher's **Open Mods Folder** opens it). Its files replace the game's at the
same path, such as `pc/...` or `common/...`, wherever they are inside it, so a
mod pack's `update` folder can go there whole. Its `.img` archives are added to
the game's, and files for the episodes (`TLAD`, `TBoGT`) are left out. If two
mods have the same file, the later name wins. Your game folder is not changed,
and **Load mods** turns them off.

The game's own `update` folder is read the same way, so mods already there keep
working.

Only files are loaded: mods with Windows code (`.asi`, `.dll`) do not run. Mods
are not part of this project, and it does not supply or support any. Add them
before `install-ios.sh` to have them on the iPad too.

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
- iPad: busy streets need the game's distances lowered
  for a steady 60 fps.
- Multiplayer and paid features are not available and not planned.
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

Grand Theft Auto IV is a trademark of Take-Two Interactive Software. This
project is not affiliated with or endorsed by Rockstar Games or Take-Two.

**Rights holders:** to have anything here taken down, open an
[issue](https://github.com/Awis13/liberty-pro-max/issues) in this repository.
It will be taken down on request, without dispute.

## License

GPL-3.0 (see `LICENSE`), as several fixes follow FusionFix's code.
