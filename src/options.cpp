// Conveniences, each an option (the launcher sets them):
//   GTAIV_SKIP_INTRO=1: the logo and legal screens before the first menu
//     are not shown;
//   GTAIV_CONTINUE=1: the game starts in the newest save, in the game it
//     was made in, instead of the main menu (with the episodes found and
//     no save yet, their chooser comes first; with the episodes off, IV's
//     newest save: see f_0085A520);
//   GTAIV_FOV=degrees: added to the game camera's field of view (wide
//     screens);
//   GTAIV_FAST_LOADING=0 turns off the loading speed-ups (on by default):
//     no 5 s sleep at start, no sleeping while a loading screen is up
//     (FusionFix's loading delays fix, by its idea), and the first
//     loading screens drawn at the frame rate, not at most 64 fps
//     (FusionFix's unlimited loading screens);
//   GTAIV_PHYSICS_HELP=0 turns off main's help with the physics pair list
//     (on by default).
//   GTAIV_DESPAWN_NEED=n: the population's removal pass runs while fewer
//     than n spawn records are due (default 20; the game: 5, so
//     GTAIV_DESPAWN_NEED=5 is as the game); see hook_006D4F92;
//   GTAIV_STUN_PUNCH=1: the stun ("sucker") punch on any unaware ped, not
//     only on those a mission marks; GTAIV_EXECUTIONS=1: the same for the
//     execution move (see hook_00C8DBAB);
//   GTAIV_JOB_WORKERS=n: worker threads of the game's job pool, and jobs
//     per physics step (2-5, default 3 as the game takes on any machine
//     with 4+ CPUs; 4 and 5 measured no faster with main's physics help).

#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

#include "first_menu.h"
#include "option.h"
#include "runtime/lifted.h"
#include "runtime/log.h"
#include "runtime/platform.h"
#include "runtime/runtime.h"

namespace recomp::gen {

void f_00A777C0_lifted(Ctx& c);
void f_0085A520_lifted(Ctx& c);
void f_004944E0_lifted(Ctx& c);

namespace {

bool Option(const char* name) {
  const char* e = std::getenv(name);
  return e && *e == '1';
}

bool g_menu_reached = false;  // the first menu has been asked for

bool FastLoading() {
  static const bool on = [] {
    const char* e = std::getenv("GTAIV_FAST_LOADING");
    return !e || *e != '0';
  }();
  return on;
}

constexpr uint32_t kLoadscreenShown = 0x018CB06C;  // CMenuManager: a loading screen is up
constexpr uint32_t kMenuActive = 0x010FC63F;       // CMenuManager: the pause/front-end menu

// Sleeps while a loading screen is up (and no menu over it) are only the
// loader's pacing: on Windows a 1-15 ms sleep, each wait of the streaming
// loop; skipped, loading finishes sooner.
uint32_t LoadingSleep(uint32_t ms) {
  if (ms && Rd8(kLoadscreenShown) && !Rd8(kMenuActive)) return 0;
  return ms;
}

// GTAIV_LOAD_LOG=1: when loading screens come and go (seconds since start).
void StartLoadLog() {
  const char* e = std::getenv("GTAIV_LOAD_LOG");
  if (!e || *e != '1') return;
  std::thread([] {
    recomp::platform::SetThreadQos({recomp::platform::ThreadQos::kUtility, 0});
    const auto t0 = std::chrono::steady_clock::now();
    bool was = false;
    for (;;) {
      const bool now = Rd8(kLoadscreenShown) != 0;
      if (now != was) {
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::fprintf(stderr, "load: loading screen %s at %.2f s\n", now ? "up" : "down", t);
        was = now;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }).detach();
}

}  // namespace

// The start: a fixed 5 s sleep (push 5000; call Sleep) before the game
// starts loading; with fast loading, none. Also where the loading options
// take effect.
void hook_00401854(Ctx& c) {
  static const bool once = [] {
    if (FastLoading()) hle::SetSleepFilter(LoadingSleep);
    StartLoadLog();
    return true;
  }();
  (void)once;
  c.esp -= 4;
  Wr32(c.esp, FastLoading() ? 0 : 5000);
}

// Physics pair list (004F2030): the workers' jobs pop collision pairs from
// one SList until it is empty, and main only waits for them, unless the
// game runs single-threaded (018CB2B9), when main pops them itself with
// the same job arguments. Here main always pops them too before its wait,
// so it works instead of waiting: cmp byte [018CB2B9], 0 says "not zero".
void hook_004F2036(Ctx& c) {
  static const bool help = [] {
    const char* e = std::getenv("GTAIV_PHYSICS_HELP");
    return !e || *e != '0';
  }();
  const bool zero = !help && Rd8(0x018CB2B9) == 0;
  c.eflags = zero ? (c.eflags | 0x40u) : (c.eflags & ~0x40u);
}

// The game's job pool gtaDefSched gets one worker per bit of the mask made
// here (00855D82: 7, or 3 below 4 CPUs), and the physics step as many jobs
// as 00922630 says (3). Both become GTAIV_JOB_WORKERS. At most 5: the
// physics keeps per-thread contexts in arrays of 6 (0x17933F0, 0x1793408),
// and main takes one too when it helps (hook_004F2036).
uint32_t JobWorkers() {
  static const uint32_t n = [] {
    const char* e = std::getenv("GTAIV_JOB_WORKERS");
    const int v = e ? std::atoi(e) : 3;
    return uint32_t(v < 2 ? 2 : v > 5 ? 5 : v);
  }();
  return n;
}

void hook_00855D82(Ctx& c) { c.eax = (1u << JobWorkers()) - 1; }

void f_00922630(Ctx& c) {
  c.eax = JobWorkers();
  c.esp += 4;  // ret
}

// The population tick's removal pass (006D4F80) loads how many due spawn
// records it may leave pending ([019B3D34], 5, set once at 006CE499) and
// skips all removal while more are due (006D51FC). In a busy district five
// are due almost always, so parked cars, wrecks and far ambient entities
// are hardly ever removed and pile up until the player leaves the district.
// Here the allowance is 20 (mov eax, [019B3D34] gives 20): in a 10-minute
// session in one district the frame no longer grew (12-14 ms of main work
// throughout) and play felt smoother. GTAIV_DESPAWN_NEED=n sets it.
void hook_006D4F92(Ctx& c) {
  static const uint32_t need = [] {
    const char* e = std::getenv("GTAIV_DESPAWN_NEED");
    const int n = e ? std::atoi(e) : 20;
    return uint32_t(n > 0 ? n : 20);
  }();
  c.eax = need;
}

// The melee system's conditions (00C8D9xx, a switch on the condition's
// hash) test the opponent's "ready to be stunned" and "ready to be
// executed" flags, bits 3 and 4 of ped+0x270, which only missions set
// (SET_CHAR_READY_TO_BE_STUNNED / _EXECUTED, e.g. Jimmy in Clean Getaway).
// With GTAIV_STUN_PUNCH / GTAIV_EXECUTIONS every opponent reads as ready,
// as the 2009 StunExecution mod did by setting the flags on nearby peds;
// the move's other conditions (unarmed or pistol, unaware, the reticle)
// still apply. Each opponent hook replaces `mov ecx, [eax+0x270]` (eax:
// opponent). The same flags are also tested on the ped whose moves are
// being chosen (`mov eax, [edx+0x270]`, edx: that ped), the side of the
// one taking the punch: those read as ready too, for every ped but the
// player, so nobody stuns or executes Niko.
bool Gameplay(const char* name) {
  const char* e = std::getenv(name);
  return e && *e == '1';
}

// FindPlayerPed (007CD230): the player's CPed, 0 while there is none.
uint32_t PlayerPed() {
  const uint32_t i = Rd32(0x00F2E0B4);
  if (i == 0xFFFFFFFFu) return 0;
  const uint32_t info = Rd32(0x01100498 + 4 * i);
  return info ? Rd32(info + 0x58C) : 0;
}

void hook_00C8DB86(Ctx& c) {  // isReadyToBeStunned
  static const bool on = Gameplay("GTAIV_STUN_PUNCH");
  c.eax = Rd32(c.edx + 0x270) | (on && c.edx != PlayerPed() ? 0x8u : 0u);
}

void hook_00C8DBD0(Ctx& c) {  // isReadyToBeExecuted
  static const bool on = Gameplay("GTAIV_EXECUTIONS");
  c.eax = Rd32(c.edx + 0x270) | (on && c.edx != PlayerPed() ? 0x10u : 0u);
}

void hook_00C8DBAB(Ctx& c) {  // opponentIsReadyToBeStunned
  static const bool on = Gameplay("GTAIV_STUN_PUNCH");
  c.ecx = Rd32(c.eax + 0x270) | (on ? 0x8u : 0u);
}

void hook_00C8DBF5(Ctx& c) {  // opponentIsReadyToBeExecuted
  static const bool on = Gameplay("GTAIV_EXECUTIONS");
  c.ecx = Rd32(c.eax + 0x270) | (on ? 0x10u : 0u);
}

// The start's loading screens draw a frame only once 15.67 ms passed
// since the last (fld [k]; fcomi: about 64 fps). With fast loading, every
// time round.
void hook_004A18E3(Ctx& c) { x87::LoadF32(c.fpu, FastLoading() ? 0 : Rd32(0x00EDFB24)); }

// Whether the player's profile holds a save: a file SGTA4nn, not empty, in
// the game's save folder, savegames/user_<the profile's XUID> (xlive.cpp's
// fixed one). The game's own scan runs only after the first menu
// (first_menu.h). An error reading the folders ends the search (no
// exception out of the hook): what was found so far counts.
bool HasSave() {
  namespace fs = std::filesystem;
  const fs::path saves = fs::path(recomp::DataDir()) /
                         "drive_c/Documents and Settings/Player/Local Settings/Application Data/"
                         "Rockstar Games/GTA IV/savegames";
  std::error_code ec;
  for (fs::directory_iterator user(saves, ec), end; !ec && user != end; user.increment(ec)) {
    std::string name = user->path().filename().string();
    for (char& ch : name) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    std::error_code fec;
    if (name != "user_ee000000deadc0de" || !user->is_directory(fec)) continue;
    for (fs::directory_iterator f(user->path(), fec); !fec && f != end; f.increment(fec)) {
      std::error_code sec;
      if (!f->is_regular_file(sec)) continue;
      const std::uintmax_t size = f->file_size(sec);
      if (!sec && first_menu::IsSave(f->path().filename().string(), size)) return true;
    }
  }
  return false;
}

// The first menu after the start screens (push 0: the menu up stays, to the
// frontend's state setter 007BDD80). With GTAIV_CONTINUE, state 6: the
// newest save loads, in the game it was made in; but when that menu is the
// episode chooser and there is no save yet, the chooser stays
// (first_menu.h).
void hook_0047D255(Ctx& c) {
  constexpr uint32_t kChooserUp = 0x010FBBB1;  // the episode chooser is up
  static const bool cont = Option("GTAIV_CONTINUE");
  const bool chooser = Rd8(kChooserUp) != 0;
  bool has_save = false;
  if (cont && !g_menu_reached && chooser) {
    has_save = HasSave();
    WarnF(has_save ? "gtaiv: episodes: continue from the newest save, in its own game\n"
                   : "gtaiv: episodes: no save yet: the chooser first\n");
  }
  const uint32_t state = first_menu::State(cont, g_menu_reached, chooser, has_save);
  g_menu_reached = true;
  c.esp -= 4;
  Wr32(c.esp, state);
}

// The start of a game from the title menu or Continue (004944E0, frontend
// state 6) first reads the command line's -episode N (0085A520: then
// [011D5E2A] set and N in [00F348F4]), scans the saves, and loads the
// newest. Given -episode, the scan also keeps each game's newest save
// (0085A2E5: IV's in [00F348E8]), the load takes that game's newest
// (0085C5C8, 00859070) and that game starts (0049469A: [00F348F4], where
// 004946A1 reads [011D5E34] without -episode); with none, a new
// game of it (state 7, 00494643). Without it the newest save of any game
// loads in its own game: an episode's, with the episodes off, ends on the
// game's "The downloadable content required for this autoload is not
// available" and a new IV game. So with the episodes off the port gives
// -episode 0 for that start (IV's newest save, or a new IV game), unless
// the player's command line has an -episode of its own, and takes it back
// when the start is over: later, the Load Game menu's episode save still
// meets the game's own answer (0085B4E0 and 00492E30 read it too).
namespace {
bool g_iv_forced;
}

void f_0085A520(Ctx& c) {
  f_0085A520_lifted(c);
  static const bool episodes = OptionOn("GTAIV_EPISODES", false);
  if (episodes || Rd8(0x011D5E2A)) return;
  Wr8(0x011D5E2A, 1);
  Wr32(0x00F348F4, 0);
  Wr32(0x011D5E34, 0);
  g_iv_forced = true;
  static bool said;
  if (!said) {
    said = true;
    WarnF("gtaiv: continue: the episodes are off: IV's newest save, as with -episode 0\n");
  }
}

void f_004944E0(Ctx& c) {
  f_004944E0_lifted(c);
  if (!g_iv_forced) return;
  g_iv_forced = false;
  Wr8(0x011D5E2A, 0);  // as 0085A520 leaves them without -episode
  Wr32(0x00F348F4, 0xFFFFFFFFu);
}

// A start screen's time on screen in ms (edx), after its count (ecx): the
// short ones (logos, legal text) before the first menu are skipped with
// GTAIV_SKIP_INTRO.
void hook_004A346B(Ctx& c) {
  static const bool skip = Option("GTAIV_SKIP_INTRO");
  c.ecx = Rd32(0x018CB07C);
  c.edx = Rd32(c.esp + 0x1C);
  if (skip && !g_menu_reached && int32_t(c.edx) < 8000) c.edx = 0;
}

// CCamGame::PostProcessHeightBasedFOV (thiscall): the game camera's field
// of view, at this+60, as the game sets it; GTAIV_FOV degrees are added
// (FusionFix's custom FOV, by its idea).
void f_00A777C0(Ctx& c) {
  static const float extra = [] {
    const char* e = std::getenv("GTAIV_FOV");
    return e ? float(std::atof(e)) : 0.0f;
  }();
  const uint32_t self = c.ecx;
  f_00A777C0_lifted(c);
  if (extra != 0.0f) {
    uint32_t bits = Rd32(self + 0x60);
    float fov;
    std::memcpy(&fov, &bits, 4);
    fov += extra;
    std::memcpy(&bits, &fov, 4);
    Wr32(self + 0x60, bits);
  }
}

}  // namespace recomp::gen
