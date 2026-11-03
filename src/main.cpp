// gtaiv: Grand Theft Auto IV (PC, 1.0.8.0), natively.
//
//   gtaiv [--game-dir DIR] [--no-launcher] [-- GAME ARGS...]
//
// DIR is the game install: the folder with GTAIV.exe, common/, pc/ and
// the other data folders. Defaults to $GTAIV_GAME_DIR. Arguments after
// `--` go on the game's own command line (the options commandline.txt
// takes, e.g. -norestrictions). Set GTAIV_TRACE=1 to log every HLE call.
// Run from "Liberty Pro Max.app", a launcher window comes first
// (launcher_mac.mm); --no-launcher skips it.

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "runtime/log.h"
#include "runtime/runtime.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if TARGET_OS_IOS || TARGET_OS_TV
// iPad and Apple TV: SDL owns UIKit's startup and calls main (SDL_main) on
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
// launcher_ios.mm: the iPad's settings screen, until Play.
void RunIosLauncher(std::string* game_args);
#endif

namespace recomp::gen {
// update_folder.cpp: FusionFix's files from the game's update folder.
std::string UpdateFolderRedirect(const std::string& guest_path);
}  // namespace recomp::gen

// shadows_lights.cpp: the game's own wrong shader bytecode corrected.
void GtaivShaderTokens(std::vector<uint32_t>& tokens);

namespace {
std::string ReadRedirect(const std::string& guest_path) {
  std::string r = GtaivModsRedirect(guest_path);
  if (r.empty()) r = recomp::gen::UpdateFolderRedirect(guest_path);
  return r;
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
  std::string game_dir, game_args;
#if GTAIV_IOS
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
  setenv("GTAIV_SKIP_INTRO", "1", 0);
  setenv("GTAIV_FPS_REPORT", "10", 0);
#if TARGET_OS_TV
  // The shared vertex and index buffer heap is resident whole (the GPU maps
  // it): 192 MB was never full in play, 512 would take 320 MB more of the
  // Apple TV's 2.1 GB.
  setenv("GTAIV_GPU_HEAP_MB", "192", 0);
#endif
  // The launcher (the player's mods, if any, are in
  // Documents/gtaiv/drive_c/mods).
  RunIosLauncher(&game_args);
#endif
  if (const char* e = std::getenv("GTAIV_GAME_DIR")) game_dir = e;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--game-dir") && i + 1 < argc) {
      game_dir = argv[++i];
    } else if (!std::strcmp(argv[i], "--no-launcher")) {
      // (The launcher's own: started from it.)
    } else if (!std::strcmp(argv[i], "--")) {
      for (i++; i < argc; i++) {
        if (!game_args.empty()) game_args += ' ';
        // Quoted, as on a Windows command line, so an argument with spaces
        // stays one.
        bool quote = std::strpbrk(argv[i], " \t");
        if (quote) game_args += '"';
        game_args += argv[i];
        if (quote) game_args += '"';
      }
    } else {
      std::fprintf(stderr, "usage: %s [--game-dir DIR] [-- GAME ARGS...]\n", argv[0]);
      return 2;
    }
  }
  if (game_dir.empty()) {
    std::fprintf(stderr, "gtaiv: pass --game-dir or set GTAIV_GAME_DIR\n");
    return 2;
  }
  while (game_dir.size() > 1 && game_dir.back() == '/') game_dir.pop_back();
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
  game.mac_fullscreen_space = true;
#endif
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
#if !GTAIV_IOS
  // Saves to the player's iPad and Apple TV (save_sync_mac.mm).
  game.before_exit = [] {
#ifdef GTAIV_PGO_GENERATE
    WriteProfile();
#endif
    GtaivCloudSavesFlush();
    GtaivSyncSaves("after the game");
  };
#elif defined(GTAIV_PGO_GENERATE)
  game.before_exit = WriteProfile;
#endif
#if defined(__APPLE__) && !TARGET_OS_TV
  // The newest saves from iCloud before the game reads any (save_cloud.mm).
  GtaivCloudSavesStart();
#endif
  return recomp::RunProcess(game, exe, game_dir, game_args);
}
