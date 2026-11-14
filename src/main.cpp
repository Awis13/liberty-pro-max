// gtaiv: Grand Theft Auto IV (PC, 1.0.8.0), natively.
//
//   gtaiv [--game-dir DIR] [--no-launcher] [-- GAME ARGS...]
//
// DIR is the game install: the folder with GTAIV.exe, common/, pc/ and
// the other data folders. Defaults to $GTAIV_GAME_DIR. Arguments after
// `--` go on the game's own command line (the options commandline.txt
// takes, e.g. -norestrictions; given any, the game itself skips that file).
// Started by a launcher (--no-launcher; always on iOS) the game also gets
// the port's defaults and the player's commandline.txt first (game_args.h).
// Set GTAIV_TRACE=1 to log every HLE call.
// Run from "Liberty Pro Max.app", a launcher window comes first
// (launcher_mac.mm); --no-launcher skips it.

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "game_args.h"
#include "language.h"
#include "option.h"
#include "runtime/log.h"
#include "runtime/runtime.h"

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <TargetConditionals.h>
#endif
#if TARGET_OS_IOS || TARGET_OS_TV
// iPad, iPhone and Apple TV: SDL owns UIKit's startup and calls main (SDL_main) on
// the main thread. The game's files are in <data>/game (copied there with
// devicectl: too big for the bundle), what the game writes (the C: drive,
// the saves, the settings) below <data>/gtaiv, the log to <data>/log.txt.
// <data> is Documents on the iPad; Library/Caches on Apple TV, which has no
// Documents (the system empties Caches only when its storage runs out).
#include <unistd.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#define GTAIV_IOS 1
#endif

// integrity.cpp: answers the messages the game sends LaunchGTAIV.exe.
bool GtaivSendMessage(uint32_t hwnd, uint32_t msg, uint32_t wparam, uint32_t lparam,
                      uint32_t* result);

// threads.cpp: the host scheduling class of each thread the game starts.
bool GtaivThreadQos(recomp::gaddr entry, uint64_t param, recomp::platform::ThreadQos* out);

// mods.cpp: the player's mods; the game's reads are pointed at their files.
void GtaivModsInit();
std::string GtaivModsRedirect(const std::string& guest_path);

#if defined(__APPLE__) && !defined(GTAIV_IOS)
// launcher_mac.mm: the app's launcher window, when run from the bundle.
void RunLauncherIfWanted(int argc, char** argv);
#endif
#if GTAIV_IOS
// launcher_ios.mm: the settings screen, until Play (its own arguments for
// the game); and whether this is an iPhone, with its launcher's
// resolutions as a display mode list.
void RunIosLauncher(std::vector<std::string>* launcher_args);
bool IosPhoneDisplay(std::string* modes);
#if !TARGET_OS_TV
void GtaivTouchStart();  // touch_ios.mm
#endif
#endif

namespace recomp::gen {
// update_folder.cpp: FusionFix's files from the game's update folder.
std::string UpdateFolderRedirect(const std::string& guest_path);
// ff_shaders.cpp: the current viewport's near and far planes.
bool GtaivDepthPlanes(float* near_plane, float* far_plane);
// pad_feedback.cpp: a DualSense's triggers freed as the game exits.
void GtaivPadFeedbackExit();
}  // namespace recomp::gen

// shadows_lights.cpp: the game's own wrong shader bytecode corrected.
void GtaivShaderTokens(std::vector<uint32_t>& tokens);

namespace {
std::string ReadRedirect(const std::string& guest_path) {
  std::string r = GtaivModsRedirect(guest_path);
  if (r.empty()) r = recomp::gen::UpdateFolderRedirect(guest_path);
  return r;
}

// The game's language: the first of the system's preferred languages (the
// Mac's, the iPad's: Settings, Language & Region) as a Windows language
// id (language.h); English elsewhere.
uint16_t SystemLangId(std::string* code) {
  uint16_t id = language::kEnglish;
#if defined(__APPLE__)
  if (CFArrayRef langs = CFLocaleCopyPreferredLanguages()) {
    char buf[64];
    if (CFArrayGetCount(langs) > 0 &&
        CFStringGetCString(static_cast<CFStringRef>(CFArrayGetValueAtIndex(langs, 0)), buf, sizeof buf,
                           kCFStringEncodingUTF8)) {
      *code = buf;
      id = language::LangIdForLocale(buf);
    }
    CFRelease(langs);
  }
#endif
  return id;
}
}  // namespace

#if !GTAIV_IOS
extern "C" void GtaivSyncSaves(const char* when);  // save_sync_mac.mm
#endif
#if defined(__APPLE__) && !TARGET_OS_TV
extern "C" void GtaivCloudSavesStart();  // save_cloud.mm
extern "C" void GtaivCloudSavesFlush();
#endif

#ifdef GTAIV_PGO_GENERATE
// An instrumented build (RECOMP_PGO=generate): the counts, written on
// ExitProcess.
extern "C" int __llvm_profile_write_file(void);
static void WriteProfile() { __llvm_profile_write_file(); }
#endif

int main(int argc, char** argv) {
#if defined(__APPLE__) && !defined(GTAIV_IOS)
  RunLauncherIfWanted(argc, argv);
  // GTAIV_LOG=path (the launcher sets it): stderr, the game's log, there.
  if (const char* log = std::getenv("GTAIV_LOG"); log && *log) std::freopen(log, "w", stderr);
#endif
  std::string game_dir;
  // The game's arguments (game_args.h): those after "--"; the iOS
  // launcher's own (its resolution); and whether a launcher started the
  // game (then the defaults and the player's commandline.txt come first).
  std::vector<std::string> game_args, launcher_args;
  bool from_launcher = false;
#if GTAIV_IOS
  from_launcher = true;
  if (const char* home = std::getenv("HOME")) {
#if TARGET_OS_TV
    const std::string docs = std::string(home) + "/Library/Caches";
    // The Siri Remote is not a controller for this game (it would take the
    // first pad's slot); a real gamepad plays it.
    SDL_SetHint(SDL_HINT_TV_REMOTE_AS_JOYSTICK, "0");
#else
    const std::string docs = std::string(home) + "/Documents";
#endif
    // (GTAIV_LOG_CONSOLE=1: stderr stays the console, for devicectl --console.)
    const char* console = std::getenv("GTAIV_LOG_CONSOLE");
    if (!(console && *console == '1')) {
      std::freopen((docs + "/log.txt").c_str(), "w", stderr);
      std::setvbuf(stderr, nullptr, _IOLBF, 0);
    }
    setenv("XDG_DATA_HOME", docs.c_str(), 0);
    game_dir = docs + "/game";
    chdir(docs.c_str());
  }
  // What the Mac's launcher would set: the Metal renderer, SMAA, vsync (60
  // Hz: the energy the iPad can keep up), no start screens.
  setenv("GTAIV_GFX", "metal", 0);
  setenv("RECOMP_UPSCALER", "smaa", 0);
  setenv("GTAIV_VSYNC", "1", 0);
#if !TARGET_OS_TV
  // An iPhone's display mode is portrait and the app runs in landscape:
  // the game is given the screen that way round, and the launcher's
  // resolutions as display modes (the game keeps only a resolution it
  // finds among them when its graphics menu applies).
  if (std::string modes; IosPhoneDisplay(&modes)) {
    setenv("GTAIV_DISPLAY_LANDSCAPE", "1", 0);
    setenv("GTAIV_DISPLAY_EXTRA_MODES", modes.c_str(), 0);
  }
#endif
  setenv("GTAIV_SKIP_INTRO", "1", 0);
  setenv("GTAIV_FPS_REPORT", "10", 0);
#if TARGET_OS_TV
  // The shared vertex and index buffer heap is resident whole (the GPU maps
  // it): 192 MB was never full in play, the runtime's default of 256 would
  // take 64 MB more of the Apple TV's 2.1 GB.
  setenv("GTAIV_GPU_HEAP_MB", "192", 0);
#endif
  // The launcher (the player's mods, if any, are in
  // Documents/gtaiv/drive_c/mods).
  RunIosLauncher(&launcher_args);
#if !TARGET_OS_TV
  GtaivTouchStart();
#endif
#endif
  if (const char* e = std::getenv("GTAIV_GAME_DIR")) game_dir = e;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--game-dir") && i + 1 < argc) {
      game_dir = argv[++i];
    } else if (!std::strcmp(argv[i], "--no-launcher")) {
      // (The Mac launcher's own: started from it, launcher_mac.mm.) It
      // also puts the game's arguments together as behind any launcher:
      // the port's defaults and the player's commandline.txt come first.
      from_launcher = true;
    } else if (!std::strcmp(argv[i], "--")) {
      for (i++; i < argc; i++) game_args.push_back(argv[i]);
    } else {
      std::fprintf(stderr,
                   "usage: %s [--game-dir DIR] [--no-launcher] [-- GAME ARGS...]\n"
                   "  --no-launcher  started by the launcher: the game gets the port's defaults\n"
                   "                 (-norestrictions -availablevidmem 2047, or GTAIV_VIDMEM's MB),\n"
                   "                 then the game folder's commandline.txt, then GAME ARGS\n",
                   argv[0]);
      return 2;
    }
  }
  if (game_dir.empty()) {
    std::fprintf(stderr, "gtaiv: pass --game-dir or set GTAIV_GAME_DIR\n");
    return 2;
  }
  while (game_dir.size() > 1 && game_dir.back() == '/') game_dir.pop_back();
  // Behind a launcher: the defaults, the player's commandline.txt (read
  // here, its bytes as the game would read them), the launcher's own, then
  // anything after "--". Run by hand without --no-launcher, only those
  // after "--" (with none, the game reads its commandline.txt itself).
  if (from_launcher) {
    launcher_args.insert(launcher_args.end(), game_args.begin(), game_args.end());
    game_args = gtaiv::LauncherGameArgs(game_dir, launcher_args);
  }
  // Quoted as the game's CRT splits a Windows command line, so each one
  // stays one argument.
  const std::string command_line = gtaiv::JoinArgs(game_args);
  if (!command_line.empty()) std::fprintf(stderr, "gtaiv: the game's arguments: %s\n", command_line.c_str());
  if (const char* t = std::getenv("GTAIV_TRACE")) recomp::g_trace = *t && *t != '0';

  std::string exe = game_dir + "/GTAIV.exe";
  struct stat st;
  if (stat(exe.c_str(), &st) != 0) {
    std::fprintf(stderr, "gtaiv: %s not found\n", exe.c_str());
    return 2;
  }

  recomp::GameConfig game;
  game.id = "gtaiv";
  game.guest_dir = "C:\\Program Files\\Rockstar Games\\Grand Theft Auto IV";
  game.exe_name = "GTAIV.exe";
  // The game needs 1.5 GiB of RAM: with less it never leaves its first
  // loading screen.
  game.total_phys_mb = 4096;
  // Video memory (GetAvailableTextureMem): the graphics menu refuses any
  // setting whose budget exceeds it (it beeps and reverts). The most a
  // signed 32-bit byte count holds.
  game.video_memory_mb = 2047;
  // Full screen as macOS does it: Game Mode turns on (Liberty Pro Max.app).
#if !GTAIV_IOS
  // (A Space stays below a display's notch: the launcher's choice of a
  // borderless window over all of it.)
  {
    const char* n = std::getenv("GTAIV_OVER_NOTCH");
    game.mac_fullscreen_space = !(n && *n == '1');
  }
#endif
  // The game in the system's language, unless the launcher's choice is
  // English (GTAIV_SYSTEM_LANGUAGE=0). GTAIV_LANG_ID=0x0419 (the runtime's
  // switch, for test runs) overrides both.
  if (OptionOn("GTAIV_SYSTEM_LANGUAGE", true)) {
    std::string code;
    game.user_lang_id = SystemLangId(&code);
    if (!code.empty())
      std::fprintf(stderr, "gtaiv: the system's language %s: the game's language id %04X\n", code.c_str(),
                   unsigned(game.user_lang_id));
  }
  // The launcher's window (integrity.cpp).
  game.send_message = GtaivSendMessage;
  // Threads by what they do, not by the game's priorities (threads.cpp).
  game.thread_qos = GtaivThreadQos;
  // The game's reads of its install files: the player's mods first
  // (mods.cpp), then the update folder (update_folder.cpp: FusionFix's data),
  // then the install itself.
  if (const char* m = std::getenv("GTAIV_MODS"); !m || *m != '0') GtaivModsInit();
  game.read_redirect = ReadRedirect;
  game.shader_tokens = GtaivShaderTokens;
  // Depth from w (x86recomp's gfx/reversed_z.h): the map's layers a few
  // millimetres apart (posters on walls) keep their order. rage's shaders
  // transform with gWorldViewProj in c8-c11; the exact planes come from
  // the current viewport.
  game.wvp_register = 8;
  game.depth_planes = recomp::gen::GtaivDepthPlanes;
#if !GTAIV_IOS
  // Saves to the player's iPad and Apple TV (save_sync_mac.mm).
  game.before_exit = [] {
#ifdef GTAIV_PGO_GENERATE
    WriteProfile();
#endif
    recomp::gen::GtaivPadFeedbackExit();
    GtaivCloudSavesFlush();
    GtaivSyncSaves("after the game");
  };
#elif !TARGET_OS_TV
  // The iPad: saves up to iCloud (save_cloud.mm); quitting through the
  // game's own menu never sends the app to the background.
  game.before_exit = [] {
#ifdef GTAIV_PGO_GENERATE
    WriteProfile();
#endif
    recomp::gen::GtaivPadFeedbackExit();
    GtaivCloudSavesFlush();
  };
#else
  game.before_exit = [] {
#ifdef GTAIV_PGO_GENERATE
    WriteProfile();
#endif
    recomp::gen::GtaivPadFeedbackExit();
  };
#endif
#if defined(__APPLE__) && !TARGET_OS_TV
  // The newest saves from iCloud before the game reads any (save_cloud.mm).
  GtaivCloudSavesStart();
#endif
  return recomp::RunProcess(game, exe, game_dir, command_line);
}
