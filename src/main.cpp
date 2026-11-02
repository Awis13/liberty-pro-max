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

// integrity.cpp: answers the messages the game sends LaunchGTAIV.exe.
bool GtaivSendMessage(uint32_t hwnd, uint32_t msg, uint32_t wparam, uint32_t lparam,
                      uint32_t* result);

// threads.cpp: the host scheduling class of each thread the game starts.
bool GtaivThreadQos(recomp::gaddr entry, uint64_t param, recomp::platform::ThreadQos* out);

// radio.cpp: the 2018-cut radio songs, restored from an imported payload;
// reads are pointed at it while GTAIV_RADIO is set.
void GtaivRadioInit();
std::string GtaivRadioRedirect(const std::string& guest_path);

#if defined(__APPLE__) && !defined(GTAIV_IOS)
// launcher_mac.mm: the app's launcher window, when run from the bundle.
void RunLauncherIfWanted(int argc, char** argv);
#endif

namespace recomp::gen {
// update_folder.cpp: FusionFix's files from the game's update folder.
std::string UpdateFolderRedirect(const std::string& guest_path);
}  // namespace recomp::gen

namespace {
bool g_radio;
std::string ReadRedirect(const std::string& guest_path) {
  std::string r = recomp::gen::UpdateFolderRedirect(guest_path);
  if (r.empty() && g_radio) r = GtaivRadioRedirect(guest_path);
  return r;
}
}  // namespace

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
  game.mac_fullscreen_space = true;
  // The launcher's window (integrity.cpp).
  game.send_message = GtaivSendMessage;
  // Threads by what they do, not by the game's priorities (threads.cpp).
  game.thread_qos = GtaivThreadQos;
  // The game's reads of its install files: the player's update folder
  // first (update_folder.cpp: FusionFix's data and mods), then, with the
  // launcher's "original radio songs" option, the imported radio payload
  // (radio.cpp), then the install itself.
  if (const char* r = std::getenv("GTAIV_RADIO"); r && *r && *r != '0') {
    GtaivRadioInit();
    g_radio = true;
  }
  game.read_redirect = ReadRedirect;
#ifdef GTAIV_PGO_GENERATE
  game.before_exit = WriteProfile;
#endif
  return recomp::RunProcess(game, exe, game_dir, game_args);
}
