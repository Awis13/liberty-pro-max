// The host scheduling class of each thread the game starts, by what it
// runs (GameConfig::thread_qos). The game's own priorities say little on
// a Mac: it starts the render thread below normal (-1, which would make
// it a utility thread on efficiency cores) although main waits for it
// every frame. Here: the threads a frame waits for are user-interactive,
// the frame's job workers just below them, streaming and other work in the
// background of a frame user-initiated, I/O and telemetry utility.
// Nothing is background (efficiency cores only) and nothing is pinned:
// Apple silicon has no affinity, only these classes.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>

#include "runtime/lifted.h"
#include "runtime/platform.h"
#include "runtime/runtime.h"

using recomp::platform::ThreadQos;

namespace {

// sysIpcCreateThread starts every thread at this wrapper with a block
// holding the real entry and its argument.
constexpr uint32_t kThreadStartWrapper = 0x00456460;
// The job pool workers (gtaDefSched, WldProbeAsyncSched,
// EntityRenderCacheTask): their argument is the worker, which begins with
// its thread's name ("gtaDefSched0:", "WldProbeAsyncSc").
constexpr uint32_t kJobWorker = 0x00460AC0;

constexpr ThreadQos kInteractive{ThreadQos::kUserInteractive, 0};
constexpr ThreadQos kJob{ThreadQos::kUserInteractive, -5};
constexpr ThreadQos kInitiated{ThreadQos::kUserInitiated, 0};
constexpr ThreadQos kInitiatedLow{ThreadQos::kUserInitiated, -5};
constexpr ThreadQos kUtility{ThreadQos::kUtility, 0};

struct Rule {
  uint32_t entry;
  ThreadQos qos;
};

constexpr Rule kRules[] = {
    {0x00CF529E, kInteractive},   // the process's main thread (the CRT entry)
    {0x008A2EF0, kInteractive},   // [GTA] RenderThread: main waits for its frame
    {0x00788A50, kInteractive},   // [RAGE Audio] Mixer Thread
    {0x00788CF0, kInteractive},   // [RAGE Audio] Mixer Thread (the other kind)
    {0x007BC460, kJob},           // GTA Audio Update: main joins it every frame
    {0x00799A20, kInitiated},     // [RAGE Audio] - Engine Thread
    {0x006B1C10, kInitiated},     // [RAGE] crmtScheduler[n]
    {0x00965010, kInitiated},     // RMPTFX Update
    {0x0045C1C0, kInitiated},     // [RAGE] pgStreamer::Worker: feeds streaming
    {0x0099B8D0, kInitiatedLow},  // PathServerThread
    {0x00971690, kInitiatedLow},  // AsyncProbeMgr
    {0x007DB100, kUtility},       // [GTA] CReplayMgr Save Worker
    {0x007DFCC0, kUtility},       // [GTA] CReplayMgr Load Worker
    {0x0089AC00, kUtility},       // Http Uploading
    {0x009A3870, kUtility},       // Telemetry Worker
    {0x008D0C40, kUtility},       // [GTA Audio] User Music Scan Thread
    {0x007898F0, kUtility},       // [RAGE Audio] User Track Decode Thread
    {0x00AEA430, kUtility},       // Enumerate Content
    {0x004D9150, kUtility},       // [RAGE] rl* workers (presence, friends, ...)
};

}  // namespace

bool GtaivThreadQos(recomp::gaddr entry, uint64_t param, ThreadQos* out) {
  uint32_t va = uint32_t(entry), arg = uint32_t(param);
  if (va == kThreadStartWrapper && arg) {
    va = recomp::Rd32(arg);
    arg = recomp::Rd32(arg + 4);
  }
  if (va == kJobWorker) {
    // Main waits for the frame's pools; the world probes are not waited
    // for within a frame.
    const std::string name = arg ? recomp::GStr(arg) : "";
    const bool frame = !name.compare(0, 11, "gtaDefSched") || !name.compare(0, 12, "EntityRender");
    *out = frame ? kJob : kInitiated;
    return true;
  }
  for (const Rule& r : kRules)
    if (r.entry == va) {
      *out = r.qos;
      return true;
    }
  return false;
}

namespace recomp::gen {

// The audio engine thread waits up to 1 s for the mixer to free one of its
// three buffers (0079BAD5) with Sleep(0) between checks: on Windows a
// cheap yield, here a busy core (0.1-0.8 M calls a second). It sleeps
// 250 us instead, far below a mixer period. GTAIV_AUDIO_SPIN=1: as the
// game. Replaces `push 0; call Sleep` (the caller pops the argument).
void hook_0079BADB(Ctx& c) {
  static const bool spin = [] {
    const char* e = std::getenv("GTAIV_AUDIO_SPIN");
    return e && *e == '1';
  }();
  c.esp -= 4;
  Wr32(c.esp, 0);
  if (spin)
    std::this_thread::yield();
  else
    std::this_thread::sleep_for(std::chrono::microseconds(250));
}

}  // namespace recomp::gen
