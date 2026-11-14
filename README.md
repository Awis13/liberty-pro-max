# Liberty Pro Max

**Grand Theft Auto IV (PC 1.0.8.0) running natively on Apple silicon Macs and iPads, and experimentally on the iPhone and Apple TV.**

The game's own x86 code is statically recompiled with
[x86recomp](https://github.com/JTM-rootstorm/x86recomp) to C++ and built for
Apple silicon: no Wine, no emulation, no virtual machine. Only the platform
underneath is new: a Metal renderer, a real-time audio mixer, a launcher with
all of the port's options, Game Mode and ultrawide screens on the Mac.

> **This repository contains no part of the game.** You build it on your own
> Mac from your own copy of GTA IV, for the Mac and, if you like, your iPad,
> iPhone or Apple TV.

**Beta.** The story can be played from a save and around the city on the Mac
and the iPad; the iPhone and the Apple TV are experimental.
Expect rough edges, and please report what you find in the
[issues](https://github.com/Awis13/liberty-pro-max/issues).

## Quick start

1. Download **Liberty Pro Max Builder** from the
   [releases](https://github.com/Awis13/liberty-pro-max/releases) and open it.
   It is not signed by Apple: the first time, go to System Settings, Privacy &
   Security, and press **Open Anyway**.
2. Drop your Grand Theft Auto IV folder (or `GTAIV.exe`) on it. If asked,
   install Apple's Command Line Tools with its button (free).
3. Press **Build Liberty Pro Max**. About 5 to 20 minutes later it is in
   Applications: open it, check the options, press **Play**.

For the iPad, the iPhone and the Apple TV, see [Install](#install).

## What you get

| | Mac | iPad and iPhone |
|---|---|---|
| **Renderer** | Metal, written for this port: drawing encoded on its own thread, geometry read in place from the game's memory, several frames in flight | the same |
| **Frame rate** | about 125 fps indoors, 60–100 fps outdoors at 3440×1440, maximum settings, M3 Pro; no frame rate cap unless you set the launcher's frame limit (30, 60 or 120 fps) | 60 fps indoors, 45–60 outdoors at 1792×1344, iPad Air (M4) |
| **Anti-aliasing** | TAA (default), SMAA, MetalFX temporal and SMAA + MetalFX spatial upscaling; TAA and MetalFX sharpened (AMD RCAS, off to high) | SMAA (default), TAA (sharpened as on the Mac) |
| **Screens** | any resolution, ultrawide included, field of view, full screen as a macOS Space with Game Mode | 1536×1152 to 2732×2048 (iPhone: half to all of its own screen), field of view |
| **Launcher** | a window with all options before the game | a settings screen with all options and **Play** |
| **Fixes built in** | about 100 fixes (high frame rate physics, cameras, timers, lights, mirrors, water), each a switch in the launcher | the same |
| **Extras** | the launcher's Extras tab, off until you turn them on: the episodes (their radio songs kept to their own episode, on), turn indicators, pad icons, DualSense triggers and light bar, phone cheats, full damage | the same, in the settings screen's Extras section |
| **More options** | stun punch and executions on anyone, fast loading, continuing the last save, a mods folder, saves shared through iCloud Drive | the same |
| **Controls** | keyboard and mouse, controllers | touch controls, controllers, keyboard |

The iPad's build also runs on an iPhone (experimental): the same, at the
phone's own wide resolutions, played with a controller or a keyboard (the
iPad's touch controls are there too, but small on a phone).

## What you need

- **Your own GTA IV for PC, version 1.0.8.0**: the folder with `GTAIV.exe`
  (SHA-256 `3d90e7c516fa450ca002e5031e62c0f66b404590f33e6cc9793b0da4fffbfd0f`)
  and the game's data next to it (`pc/`, `common/`, …). Only this file, as
  Rockstar signed it, works: a changed or cracked exe does not. The Complete
  Edition (1.2.0.x) is not supported.

  If you own the game on Steam, you can download this version from your own
  account with [DepotDownloader](https://github.com/SteamRE/DepotDownloader)
  (`brew tap steamre/tools && brew install depotdownloader`):

  ```sh
  depotdownloader -app 12210 -depot 12211 -manifest 4406763129603688303 \
    -username <your Steam name> -dir GTAIV-1.0.8.0
  shasum -a 256 GTAIV-1.0.8.0/GTAIV/GTAIV.exe   # must match the SHA-256 above
  ```
- **A Mac with Apple silicon** (M1 or newer), macOS 15 or newer, with the
  **Xcode Command Line Tools** (`xcode-select --install`) and
  [Homebrew](https://brew.sh); about 6 GB free for the build. The Mac builds
  every version.
- **For the iPad**, also:
  - an iPad with an M-series chip, iPadOS 17 or newer, in Developer Mode and
    connected to the Mac by cable, with about 16 GB free;
  - full **Xcode** with a **paid Apple developer account** signed in
    (Xcode › Settings › Accounts): the app needs the *Extended Virtual
    Addressing* and *Increased Memory Limit* capabilities, which free
    accounts do not have;
  - a controller or a keyboard.
- **For the iPhone** (experimental): an iPhone with iOS 17 or newer, in
  Developer Mode and connected to the Mac by cable, with about 16 GB free;
  the same Xcode and paid account; a controller or a keyboard. Which iPhones
  have the memory for the game is not known yet.
- **For the Apple TV** (experimental): an Apple TV 4K (3rd generation,
  A15) with tvOS 17 or newer, paired with Xcode (Window › Devices and
  Simulators) and in Developer Mode, on Ethernet (the game's 15 GB go over
  the network), about 16 GB free; the same paid account; a controller.

## Install

### Mac, with the Builder

See [Quick start](#quick-start). The Builder carries everything that is not
the game's code (the runtime, its libraries, the translator) and compiles only
what it translates from your own `GTAIV.exe`, with Apple's compiler; no
Homebrew. `builder/make_builder.sh` makes it.

### Mac, from the Terminal

```bash
git clone https://github.com/Awis13/liberty-pro-max
cd liberty-pro-max
./install.sh "/path/to/Grand Theft Auto IV"
```

`install.sh` installs the build tools from Homebrew, fetches the pinned
x86recomp, checks your `GTAIV.exe`, translates the game's code, builds, and
puts **Liberty Pro Max** in `/Applications`, with an icon made from the logo
in your game files (the Builder's app has the standard icon for now). The first run takes about 15
minutes on an M-series Mac. Run it again after `git pull` to update.

Saves and settings are kept in `~/Library/Application Support/gtaiv`; the
game's log is `~/Library/Logs/gtaiv.log`. Every graphics setting, high
texture quality too, can be chosen without a `commandline.txt`: the
launcher gives the game `-norestrictions -availablevidmem 2047` itself. A
`commandline.txt` in the game folder is still read, on every device, after
those two, so its own values win (another `-availablevidmem`, say); only
the resolution chosen in the iPad and iPhone launcher, and the Apple TV's
fixed 1920x1080, win over its `-width` and `-height`. Only the game
folder's own `commandline.txt` is read, not one inside a mod or the
`update` folder. Files it takes in with `@name` are read from the game
folder too; the installer copies only `commandline.txt` itself to the iPad,
iPhone and Apple TV, so copy such files there by hand. An `@name` given
after `--` is not read behind a launcher. The log's first lines show the
arguments the game was given.

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
streets drop below 60. **Frame limit** is 60, 40, 30 or Off; Off leaves the
pace to the screen, up to 120 fps on a 120 Hz one. The game's log is
`log.txt` in the app's Documents (Files › On My iPad, or `devicectl`).

### iPhone (experimental)

The same, with the iPhone connected instead (with both connected,
`--iphone` picks the phone):

```bash
./install-ios.sh --iphone YOUR_TEAM_ID
```

The launcher's resolutions are the phone's own screen at half, two
thirds (the default), five sixths and full size: 1912×880 of 2868×1320,
say. Play it with a controller or a keyboard: the touch controls are laid out
for the iPad's screen and come out small on a phone.

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
app's Library/Caches (`devicectl`). The episodes are on there (with no
settings screen, nothing could turn them on), and their folders are copied
with the game's.

The app's icon is made on your Mac from the logo in your own game files,
as on the iPad.

## Using it

### Saves on every device

With iCloud Drive on, the iPad and the iPhone keep a copy of every save in
their iCloud folder (**Liberty Pro Max** in iCloud Drive) and the Mac uses the
same folder: each device takes the newer saves when the game starts and sends
its own while it runs and when it quits. Per save the newer one wins; the
one it replaces, on either side, is kept in that folder's `Backups`.
`install-ios.sh` turns on iCloud for the app (its signing profile then
needs your Apple ID signed in to Xcode). The Apple TV has no iCloud Drive:
the Mac copies its saves to and from it at start and quit, over the cable
or the network, when Xcode is installed. The launcher's "Saves synced"
option turns both off.

### Saves from elsewhere

A save from another PC, or one downloaded (a completed story, say), goes into
the folder the game reads its saves from. On the Mac the launcher's **Open
Saves Folder** opens it:

`~/Library/Application Support/gtaiv/drive_c/Documents and Settings/Player/Local Settings/Application Data/Rockstar Games/GTA IV/savegames/user_ee000000deadc0de`

The game lists 15 slots there, files named `SGTA400` to `SGTA414` (no
extension; `SGTA400` is the menu's first slot).

A save loads when it is laid out as this version of the game (1.0.8.0) lays
its own out, whether it was made with Games for Windows Live or without it.
Which other versions of the game write that layout was not checked. A save
made with Live carries a checksum only Live can read; the port checks the
save's own sum of its data instead ("Saves made with Games for Windows Live
can be loaded (checked by their own sum)", on by default).

Saves of the Complete Edition (1.2.0.x) do not load: some of their blocks
are larger and this version of the game cannot read them. Where a download
offers saves for several versions, take the one for 1.0.8.0.

A save that does not load gives the game's message ("Load failed. Please
check your hard drive and try again.") and, in `~/Library/Logs/gtaiv.log`,
lines starting `warning: gtaiv: save:` with the game's error code, the file
and the reason: another version's save, or a damaged one.

### The episodes

The episodes are off until you turn them on: "The episodes" on the launcher's
**Extras** tab (in its Extras section on the iPad and iPhone; the Apple TV, with
no settings screen yet, has them on). Then, if your game folder has the `TLAD`
and `TBoGT` folders, The Lost and Damned and The Ballad of Gay Tony work: the
game opens with its own screen to choose what to play. Their saves sit with
IV's, in the same slots. With the episodes off their saves are kept, and
"Continue from the last save" goes on from your newest IV save instead, or
starts a new IV game if you have none; turn the episodes on to continue an
episode.

The game shows an episode only if its folder has the episode's list of files,
`TLAD/E1_List.txt` and `TBoGT/E2_List.txt`. The Complete Edition's folders have
them (the game itself stays 1.0.8.0). With the `TLAD` and `TBoGT` folders of
the standalone Episodes from Liberty City (Steam depot 12221) the game shows no
episodes; those Steam gives with `download_depot 12210 12212` work. If a list
is missing, the launcher says so by "The episodes" (Mac, iPad, iPhone), and so
does the log; on the Apple TV, which has no settings screen, only the log does.

With the episodes on and "Continue from the last save" on, the game goes on
from your newest save, in the game it was made in (IV or an episode). With no
save yet it opens on that screen. To play another game than your newest save's,
untick "Continue from the last save". If an episode's folder is missing (say
only IV was copied to the iPad, and that episode's saves came through iCloud),
the game goes on from the newest save of a game that is there.

An `-episode` in your `commandline.txt` (`-episode 1` starts The Lost and
Damned's newest save, `-episode 2` The Ballad of Gay Tony's) works with the
episodes on. With the episodes off it is left out and IV starts; the log says
so.

The episodes also bring radio songs, which the game would add to the same
stations in IV and in the other episode. They play only in their own episode,
so IV's stations keep IV's songs; the option "Episodes' radio songs only in
their own episode, not in IV" turns that off.

In an episode, the `update` folder gives its archives for that episode, such as
`TLAD/...img`, and those named for all of them (`EFLC`). IV's archives and data
files, and the mods folder's archives, stay out of the episodes, as they stop
them from loading.

### The game's language

The game speaks the language your Mac, iPad, iPhone or Apple TV is set to
(the first of your preferred languages): English, French, German, Italian,
Spanish or Russian. Any other language gives the game in English, as before.
On the Mac, choose **Language** on the launcher's General tab; on the iPhone
and iPad, find it in the **Game** section. The choices are **Same as the
system**, **English**, **French**, **German**, **Italian**, **Spanish**, or
**Russian (Русский)**. An explicit language applies at startup, including
when the game's settings or profile previously stored another language.
It does not change your device's language. With **Same as the system**, a
language previously chosen in the game's own menu stays in effect.

The Apple TV, with no settings screen yet, follows the system.
Russian is in the game's language menu only while the system or launcher
selects it. In German the game hides a few statistics
entries, as it does in German on Windows; nothing else changes.

Russian needs the Russian files in your game folder: `common/text/russian.gxt`,
`common/data/fonts_r.dat`, and in `pc/textures` `fonts_r.wtd`,
`fonts_r_streamed_1.wtd` and `title_offline_ru.wtd` (with the episodes on,
also their `common/text/russian.gxt` and `title_offline_e1_ru.wtd`,
`title_offline_e2_ru.wtd`). Steam's install has them. Without them the game
stays in English. The Russian text is older than the English: the few lines it
lacks (some radio song names, messages about downloadable content, one
mission's subtitles) show in English. Saves are the same in every language.

### Your own music

Independence FM plays your own music, as on Windows. Put the files in the
game's User Music folder: on the Mac `~/Library/Application
Support/gtaiv/drive_c/Documents and Settings/Player/My Documents/Rockstar
Games/GTA IV/User Music`; on the iPad and iPhone the same path below `gtaiv`
in the app's Documents (Files › On My iPad, or On My iPhone, › Liberty Pro
Max). Make the folders that are not there yet. Then run **Complete Scan** in
the game's audio settings. Tracks from 20 seconds to two hours long are
taken. The app the Builder makes and the iPad's and iPhone's read MP3 only;
one built with `install.sh` (Homebrew's full FFmpeg) also reads WMA.

### If something goes wrong

The game's log is `~/Library/Logs/gtaiv.log` on the Mac (written anew at every
start), `log.txt` in the app's Documents on the iPad and iPhone, and in its
Library/Caches on the Apple TV.

An environment variable the launcher does not set itself, given when you start
the app from the Terminal, reaches the game. `GTAIV_MEM_REPORT=30` writes two
`mem:` lines to the log every 30 seconds, saying what fills the game's address
space (all of it and the process heap, then its textures, buffers and shared
heap); `GTAIV_FPS_REPORT=30` writes the frame rate:

```bash
GTAIV_MEM_REPORT=30 "/Applications/Liberty Pro Max.app/Contents/MacOS/gtaiv"
```

Every option of the launcher's table, shown or hidden, is set by the launcher
when you press **Play**, over the same variable given this way. To give one
yourself, start the game without the launcher. Its log then goes to the
Terminal, and the options you do not give take the code's defaults, which are
not all the launcher's (continuing the last save, for one, is off; the Metal
renderer needs `GTAIV_GFX=metal`):

```bash
GTAIV_GFX=metal GTAIV_NAME=1 "/Applications/Liberty Pro Max.app/Contents/MacOS/gtaiv" \
  --no-launcher --game-dir "/path/to/Grand Theft Auto IV"
```

### Mods

Each folder in `~/Library/Application Support/gtaiv/drive_c/mods` is a mod (the
launcher's **Open Mods Folder** opens it). Its files replace the game's at the
same path, such as `pc/...` or `common/...`, wherever they are inside it, so a
mod pack's `update` folder can go there whole. Its `.img` archives are added to
the game's. A file in an episode's folder (`TLAD`, `TBoGT`) replaces that
episode's file; archives there, and the files left out for IV, are left out for
the episodes too. If two mods have the same file, the later name wins. Your game
folder is not changed, and **Load mods** turns them off.

The game's own `update` folder is read the same way, so mods already there keep
working.

Only files are loaded: mods with Windows code (`.asi`, `.dll`) do not run. Mods
are not part of this project, and it does not supply or support any. Add them
before `install-ios.sh` to have them on the iPad, iPhone or Apple TV too.

## Fixes and options

The Mac's launcher has three tabs. **General**: the game folder, full screen
or a window (on a display with a notch, also the whole screen), vertical sync,
a frame limit (off, 30, 60 or 120 fps), anti-aliasing (TAA, SMAA, MetalFX)
with the MetalFX resolution and sharpening, the performance overlay,
skipping the logo screens, continuing the last save, the game's language,
mods (**Open Mods Folder**) and the saves' folder (**Open Saves Folder**).
**Extras**: this port's own additions, the episodes, turn indicators, pad
icons, the DualSense's triggers and light bar, phone cheats and full damage,
each off until you turn it on, and the episodes' radio songs kept to their
own episode, on.
**Advanced**: shadow and reflection resolution, field of view, fast loading,
**Combat** (stun punch and executions on anyone), then every fix below, each
a switch. Most fixes follow FusionFix's and ZolikaPatch's (by what they do;
their code is not used), some are this port's own.

On the iPad and iPhone the settings screen has the same in sections: **Game**
(touch controls, continuing the last save, skipping the start screens, the
game's language, mods, fast loading, stun punch and executions on anyone), **Extras**, **Graphics**
(resolution, anti-aliasing, sharpening, frame limit, shadows, reflections, field of view,
the performance overlay) and **Fixes**.

**Pad icons** need the other pads' button pictures, which the game does not
have: the update folder's `update/pc/textures/buttons_360.wtd` must hold,
beside the game's own Xbox 360 button textures, each other pad's set under
the same names with a prefix: `XBONE_` (Xbox One and Series), `PS3_`, `PS4_`,
`PS5_` and `SWITCH_` (`PS4_A_BUTT`, say). A set with a texture missing is not
used. The game's own `pc/textures/buttons_360.wtd` has only the Xbox 360 set,
so without such a file the option changes nothing.

**DualSense** (the PlayStation 5 pad; other pads are left alone): with the
option on, the triggers and the light bar follow the game.

| When | Left trigger | Right trigger | Light bar |
|---|---|---|---|
| Menus, the pause menu, cutscenes | free | free | the pad's own colour |
| Driving (not helicopters) | brake: pushes back | accelerate: pushes back lightly; shakes for a moment on a gear change | the pad's own colour |
| On foot with a gun in hand, not shooting | free | fire: pushes back from early in its travel, then gives way with a click, differently for pistols, submachine guns, rifles, shotguns, sniper rifles and heavy weapons | the pad's own colour |
| Shooting (as the game counts shots, not the trigger) | free | vibrates low in its travel: submachine guns and rifles as long as they fire, at about their rate of fire; pistols, shotguns, sniper rifles and rocket launchers one short kick a shot (shotguns and sniper rifles the strongest); released, the click again | the pad's own colour |
| On foot with fists, a melee or a thrown weapon | free | free | the pad's own colour |
| Wanted, while playing | as above | as above | flashes red and blue, faster from three stars |

The light bar is not touched until you are first wanted; when the stars are
gone, in the pause menu and when the game quits it goes back to the pad's own
colour and the triggers are freed. The game's own vibration (engine, crashes,
gunfire) is the game's, as on any pad, with or without the option. The
option is new and has not been tried on every setup yet: the code handles the
pad by cable and by Bluetooth on the Mac, and through the system's controller
support on the iPad and iPhone. The Apple TV has no settings screen, so it is
off there. Not yet: gyro aiming.

<details>
<summary>All fixes and options</summary>

<!-- fixes:begin -->
106 switches in the launcher (Advanced and Extras tabs; the iPad and iPhone have them on their settings screen). Each one is also an environment variable, `GTAIV_` and the name in capitals in `src/launcher_options.h`. The shader choices there, motion blur among them, are not listed: no launcher shows them, as the extended shaders they belong to are not finished. The launcher sets them to their defaults too; another value takes effect only in a game started without it (see If something goes wrong).

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
| No water flicker | on |

#### Cameras

| Option | Default |
|---|---|
| No forced camera centring at the edge of cover | on |
| Keep camera shake (hand-held, drunk, vehicles) | on |
| Cutscene cameras steady, without sudden zooms or flicker at cuts | on |
| Car camera swings back faster with keyboard and mouse | on |
| Cinematic camera when vaulting walls and jacking cars | on |
| Cinematic camera and slow motion on stunt jumps | on |
| On foot, the camera waits 2 s after the mouse before recentring | on |
| Car camera centred behind the car | off |
| On-foot camera centred behind the player | off |
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
| Pause menu shows the game's version, the archives open and memory use | off |
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
| Neon signs glow from every angle | on |
| Distant city lights as on consoles (from 7 pm, street level) | on |
| Reflections and car dirt as on consoles | on |
| Mirrors show graffiti and other marks near them | on |
| Water surface tiling as on consoles | on |
| No flickering lights where there are many | on |
| Reversing lights at the back of the car | on |
| Rain lit as on consoles (visible at night) | on |
| Police helicopter searchlights do not flicker | on |
| The Night Shadows setting changes how sharp night shadows are | on |
| Street lamps keep the shadows of people and cars when the camera turns or cars drive by | on |
| Brake lights stay on while your car stands | off |
| Keep bloom (glow around bright lights) | on |
| Keep depth of field blur | on |
| The glow of lamps and signs does not cut into what is in front of it | on |
| Every light casts moving shadows (very slow) | off |
| Shadow under cars at any distance | on |
| No shadow patch under cars | off |
| No shadow patch under cars in cutscenes | off |
| High detail reflections (heavy for the GPU) | off |
| Red and amber glass from broken tail lights and indicators | on |
| Tracer on every shot | off |
| TBoGT: tracers drawn in the right place | on |

#### Gameplay

| Option | Default |
|---|---|
| Weapon recoil with the mouse as with a gamepad | on |
| Health from a save at most a full bar (trainer saves) | on |
| Saves made with Games for Windows Live can be loaded (checked by their own sum) | on |
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
| Missions play the alternative car banter | off |

#### Limits, streaming and stability

| Option | Default |
|---|---|
| Draw distance settings apply from the start (costs fps) | off |
| More kinds of cars in traffic (no streets full of taxis) | on |
| Twice the game's limits for people's actions and object lists | on |
| No crash in "In the Crosshairs" at long draw distance | on |
| City not held back from loading by the game's memory limit | on |
| More memory for loading the city | off |
| No crash on phone calls in The Lost and Damned | on |
| Game errors only to the log, no error box (may crash later) | off |
| No crash when someone other than the player aims a thrown weapon | on |

#### Mac, iPad, iPhone and Apple TV

| Option | Default |
|---|---|
| Saves synced with your iPad, iPhone and Apple TV at start and quit (Apple TV: needs Xcode) | on |
| Keep running when the window is not in front | off |

#### Extras

This port's own additions, on the launcher's Extras tab (a section of their own on the iPad and iPhone). The Apple TV, with no settings screen, has the episodes on.

| Option | Default |
|---|---|
| The episodes, when their folders are in the game's | off |
| Episodes' radio songs only in their own episode, not in IV | on |
| Turn indicators when steering, hazard lights after a crash | off |
| Turn indicators by hand: [ and ] or bumpers, both for hazards (needs Turn indicators) | off |
| Route indicators: with a GPS route, the turn indicator comes on before the turn and towards the kerb on arriving (needs Turn indicators, not by hand) | off |
| Pad icons: PlayStation, Xbox One and Switch buttons on screen, for the first connected pad (needs their textures in the update folder) | off |
| DualSense: adaptive triggers when driving and with a gun in hand, light bar flashes red and blue while wanted | off |
| Phone cheats: beta gloves (458-555-0100), beta hair (288-555-0100) | off |
| Full damage: the player takes bullet damage ×3, explosion, fire and thrown object damage ×1.5 | off |

<!-- fixes:end -->

</details>

With turn indicators on, flick the left stick to set an indicator before a
turn; flick the same way to turn it off.

Not yet here from FusionFix: its ambient occlusion (SAO), console gamma, HDR
output, depth effects through transparent surfaces (pre-alpha depth), MSAA in
reflections, camera sensitivity ranges and ragdoll on demand. The episodes'
vehicles and weapons in IV are not here yet. On a Mac, FusionFix's raw input,
keyboard lighting, graphics API choice and plugin loaders have nothing to do.

## Known issues

- Mac, iPad and (experimental) iPhone and Apple TV for now. Linux and the Steam Deck are planned.
- iPad: busy streets need the game's view and detail distances lowered for a steady 60 fps.
- iPhone: best with a controller or a keyboard (the touch controls are the
  iPad's, small on a phone); which models have enough memory is not known yet.
- Apple TV: no settings screen yet; high texture quality does not
  fit its memory (DXT decoded on upload).
- MetalFX temporal upscaling costs about 5 ms a frame at 3440×1440 output,
  more than it saves in this game; TAA at native resolution is the default.
- A few seconds of stutter right after loading, while the city streams in.
- Multiplayer and paid features are not available and not planned.

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
launcher. For the app's icon, before the last build (with Pillow):
`python3 tools/make_ios_icon.py "$GTAIV_GAME_DIR" out/icons --mac`.

Building and testing: `cmake --preset release -DGTAIV_TESTS=ON`, build, then
`ctest --test-dir out/build/release`. That runs the port's unit tests and the
tools' Python tests (`python3 -m unittest discover -s tests` runs those
alone).

For the iPad, after that: `tools/apple_deps.sh`, `cmake --preset iphoneos &&
cmake --build --preset iphoneos`, then `tools/ios_profile.sh`,
`tools/make_ios_icon.py`, `tools/ios_bundle.sh <device>` and
`tools/ios_push_game.py` (each script says what it takes; `install-ios.sh`
runs them in order). The iPhone takes the same build and the same steps.

For the Apple TV, the same with `PLATFORMS=tvos tools/apple_deps.sh`, the
preset `appletvos` and `PLATFORM=tvos` for `tools/ios_profile.sh` and
`tools/ios_bundle.sh`. Its icon is layered: after `tools/make_ios_icon.py`,
`tools/make_tvos_icon.py out/icons` (with Pillow) writes an asset catalog
to `out/icons/tvos` with the mark and the black background as separate
parallax layers (400×240 at 1× and 2×, 1280×768), the Top Shelf images
(1920×720 and 2320×720, at 1× and 2×), and a flat preview,
`out/icons/icon-tvos.png`; `tools/ios_bundle.sh` compiles the catalog into
the app. A second argument uses other artwork than `out/icons/icon1024.png`.
Like everything in `out/`, none of it is in the repository.

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

### Speed

Measured on an M3 Pro at 3440×1440, maximum settings, on the same street:

| Stage | fps |
|---|---|
| First build, generic SDL GPU renderer | 20–52 |
| Own Metal renderer | 34–88 |
| Native SSE math, host CRT math, PGO, ThinLTO | 71–83 |
| Physics and job threads helped by the main thread, 397 hot functions translated to plain C++ | about 90 on average |

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
