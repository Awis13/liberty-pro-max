// GTAIV_PERF=1: where the main loop's frame goes, for tuning the threads
// (the runtime counts its side with the same switch). Every 2 s the main
// thread logs "gtaiv: perf:" lines:
//   - the frame handoff: main's wait for the render thread (FB8) and the
//     rest of 008A1860 (S: the RMPTFX and audio joins in it, timed too);
//     the render thread's wait for main (FB4) and its frame (00863B50);
//     frames in single-threaded rendering ([render+0xFC8]) and with the
//     2 MB command buffer overflowed ([0x11D6128 + index]);
//   - job waits (00460D50) by caller: how many, how many jobs each waited
//     for, and how long; at each wait, the job slots in use (of 256 at
//     0x18E7538) and each pool's queue depth (of 255), at most;
//   - polls by caller: 004563B0 (WaitForSingleObject(h, 0)) and 00456610
//     with 0 (Sleep(0));
//   - the physics context slots handed out ([0x18CB29C], [0x18CB2A4]; 6
//     each).
// Each thread the game starts (sysIpcCreateThread) is logged once with its
// name, entry and priority and the host QoS class it runs at, and the host
// thread gets the name (for sample and Instruments).
// Off, each replaced function costs a load and a branch.

#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <utility>

#include "runtime/lifted.h"
#include "runtime/log.h"
#include "runtime/platform.h"
#include "runtime/runtime.h"

namespace recomp::gen {

void f_004563B0_lifted(Ctx& c);
void f_004563D0_lifted(Ctx& c);
void f_00456460_lifted(Ctx& c);
void f_00456500_lifted(Ctx& c);
void f_00456610_lifted(Ctx& c);
void f_00460D50_lifted(Ctx& c);
void HelpWithJobs(Ctx& c);
void f_007BBCD0_lifted(Ctx& c);
void f_008A1860_lifted(Ctx& c);
void f_00863B50_lifted(Ctx& c);
void f_00964B90_lifted(Ctx& c);

namespace {

bool On() {
  static const bool on = [] {
    const char* e = Env("PERF");
    return e && *e && *e != '0';
  }();
  return on;
}

uint64_t Now() {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
}

// Durations: count, sum and maximum, in ns, from any thread.
struct Stat {
  std::atomic<uint64_t> n{0}, sum{0}, max{0};
  void Add(uint64_t ns) {
    n.fetch_add(1, std::memory_order_relaxed);
    sum.fetch_add(ns, std::memory_order_relaxed);
    uint64_t m = max.load(std::memory_order_relaxed);
    while (ns > m && !max.compare_exchange_weak(m, ns, std::memory_order_relaxed)) {
    }
  }
  // "avg/max" in ms, per call; the counts start again.
  std::string Take() {
    const uint64_t cn = n.exchange(0), cs = sum.exchange(0), cm = max.exchange(0);
    char b[48];
    std::snprintf(b, sizeof b, "%.2f/%.1f", cn ? double(cs) / double(cn) / 1e6 : 0.0, double(cm) / 1e6);
    return b;
  }
};

// The return addresses that tell the frame handoff's waits apart.
constexpr uint32_t kMainWaitFb8 = 0x008A1879, kMainWaitFb8Single = 0x008A19CA;
constexpr uint32_t kRenderWaitFb4 = 0x008A1B1F;
constexpr uint32_t kRmptfxJoin = 0x008A18B8;

Stat g_main_period, g_handoff, g_wait_fb8, g_rmptfx, g_audio;
Stat g_render_wait_fb4, g_render_frame, g_render_period;
std::atomic<uint64_t> g_frames{0}, g_single_frames{0}, g_overflow_frames{0};

struct JobWaits {
  uint64_t calls = 0, jobs = 0, jobs_max = 0, ns = 0, ns_max = 0;
};
std::mutex g_jobs_mu;
std::map<uint32_t, JobWaits> g_jobs;  // by caller
std::atomic<uint32_t> g_slots_max{0};
std::atomic<uint32_t> g_ring_max[8] = {};

// Polls by caller, without locks (as the runtime's perf counters).
struct Polls {
  static constexpr uint32_t kSlots = 64;
  std::atomic<uint32_t> caller[kSlots] = {};
  std::atomic<uint64_t> n[kSlots] = {};
  void Add(uint32_t k) {
    for (uint32_t i = (k >> 2) % kSlots, probe = 0; probe < kSlots; probe++, i = (i + 1) % kSlots) {
      uint32_t cur = caller[i].load(std::memory_order_relaxed);
      if (cur == 0 && caller[i].compare_exchange_strong(cur, k, std::memory_order_relaxed)) cur = k;
      if (cur == k) {
        n[i].fetch_add(1, std::memory_order_relaxed);
        return;
      }
    }
  }
} g_polls, g_sleep0s;

void UpdateMax(std::atomic<uint32_t>& a, uint32_t v) {
  uint32_t m = a.load(std::memory_order_relaxed);
  while (v > m && !a.compare_exchange_weak(m, v, std::memory_order_relaxed)) {
  }
}

// Jobs in flight: the scheduler's slots in use, each pool's queue depth.
void SampleJobs() {
  uint32_t used = 0;
  for (uint32_t i = 0; i < 256; i++) used += Rd32(0x018E753C + i * 0xA0) != 0;
  UpdateMax(g_slots_max, used);
  const uint32_t pools = std::min<uint32_t>(Rd32(0x018E7534), 8);
  for (uint32_t i = 0; i < pools; i++)
    if (const uint32_t pool = Rd32(0x018E7514 + 4 * i))
      UpdateMax(g_ring_max[i], Rd32(pool + 8 + 0x428));
}

void Report(double seconds) {
  const double frames = double(g_frames.exchange(0));
  WarnF("gtaiv: perf: %.0f frames; main frame %s ms (avg/max), in 008A1860 %s: wait for render "
        "(FB8) %s, RMPTFX join %s, audio join %s; render: wait for main (FB4) %s, its frame %s, "
        "period %s; single-threaded frames %llu, command buffer overflows %llu; physics slots "
        "%u/6 %u/6\n",
        frames, g_main_period.Take().c_str(), g_handoff.Take().c_str(), g_wait_fb8.Take().c_str(),
        g_rmptfx.Take().c_str(), g_audio.Take().c_str(), g_render_wait_fb4.Take().c_str(),
        g_render_frame.Take().c_str(), g_render_period.Take().c_str(),
        (unsigned long long)g_single_frames.exchange(0),
        (unsigned long long)g_overflow_frames.exchange(0), Rd32(0x018CB29C), Rd32(0x018CB2A4));
  std::map<uint32_t, JobWaits> jobs;
  {
    std::lock_guard<std::mutex> l(g_jobs_mu);
    jobs.swap(g_jobs);
  }
  std::string line;
  for (const auto& [caller, j] : jobs) {
    char b[128];
    std::snprintf(b, sizeof b, " %08X: %.1f/frame x %.1f jobs (max %llu), %.2f ms/frame (max %.1f);",
                  caller, frames ? double(j.calls) / frames : 0.0,
                  j.calls ? double(j.jobs) / double(j.calls) : 0.0, (unsigned long long)j.jobs_max,
                  frames ? double(j.ns) / frames / 1e6 : 0.0, double(j.ns_max) / 1e6);
    line += b;
  }
  char rings[96] = "";
  for (uint32_t i = 0, len = 0, pools = std::min<uint32_t>(Rd32(0x018E7534), 8); i < pools; i++)
    len += uint32_t(std::snprintf(rings + len, sizeof rings - len, "%s%u", i ? "," : "",
                                  g_ring_max[i].exchange(0)));
  WarnF("gtaiv: perf: job waits by caller (count, width, time):%s slots in use max %u/256, "
        "queue depth max by pool %s\n",
        line.c_str(), g_slots_max.exchange(0), rings);
  for (Polls* p : {&g_polls, &g_sleep0s}) {
    line.clear();
    std::pair<uint64_t, uint32_t> top[Polls::kSlots];
    uint32_t n = 0;
    for (uint32_t i = 0; i < Polls::kSlots; i++)
      if (const uint64_t k = p->n[i].exchange(0, std::memory_order_relaxed))
        top[n++] = {k, p->caller[i].load(std::memory_order_relaxed)};
    std::sort(top, top + n, [](const auto& a, const auto& b) { return a.first > b.first; });
    for (uint32_t i = 0; i < n && i < 8; i++) {
      char b[32];
      std::snprintf(b, sizeof b, " %08X:%.0f", top[i].second, double(top[i].first) / seconds);
      line += b;
    }
    if (!line.empty())
      WarnF("gtaiv: perf: %s per second from%s\n",
            p == &g_polls ? "polls (004563B0)" : "Sleep(0) (00456610)", line.c_str());
  }
}

// Threads: sysIpcCreateThread's name and priority, by (entry, argument),
// for the thread to log when it starts.
struct Started {
  std::string name;
  int priority;
};
std::mutex g_threads_mu;
// In the order made: threads may share both (two audio mixers).
std::map<std::pair<uint32_t, uint32_t>, std::deque<Started>> g_threads;

}  // namespace

// The main loop's wait for the render thread (bench.cpp's 008A1860 calls
// it in place of the lifted function; thiscall, ecx the render thread).
void MainLoopWaitRender(Ctx& c) {
  if (!On()) return f_008A1860_lifted(c);
  static uint64_t last = 0, report_at = 0;
  const uint64_t t0 = Now();
  if (last) g_main_period.Add(t0 - last);
  last = t0;
  g_frames.fetch_add(1, std::memory_order_relaxed);
  if (Rd8(c.ecx + 0xFC8)) g_single_frames.fetch_add(1, std::memory_order_relaxed);
  if (Rd8(0x011D6128) | Rd8(0x011D6129)) g_overflow_frames.fetch_add(1, std::memory_order_relaxed);
  f_008A1860_lifted(c);
  const uint64_t t1 = Now();
  g_handoff.Add(t1 - t0);
  if (!report_at) report_at = t1 + 2000000000u;
  if (t1 >= report_at) {
    Report(2.0 + double(t1 - report_at) / 1e9);
    report_at = t1 + 2000000000u;
  }
}

// WaitForSingleObject(h, INFINITE) as a bool (cdecl): the frame
// handoff's waits are timed.
void f_004563D0(Ctx& c) {
  if (!On()) return f_004563D0_lifted(c);
  const uint32_t caller = Rd32(c.esp);
  if (caller != kMainWaitFb8 && caller != kMainWaitFb8Single && caller != kRenderWaitFb4)
    return f_004563D0_lifted(c);
  const uint64_t t0 = Now();
  f_004563D0_lifted(c);
  (caller == kRenderWaitFb4 ? g_render_wait_fb4 : g_wait_fb8).Add(Now() - t0);
}

// WaitForSingleObject(h, 0) as a bool (cdecl): counted by caller.
void f_004563B0(Ctx& c) {
  if (On()) g_polls.Add(Rd32(c.esp));
  f_004563B0_lifted(c);
}

// Sleep (cdecl: ms): Sleep(0) counted by caller.
void f_00456610(Ctx& c) {
  if (On() && Rd32(c.esp + 4) == 0) g_sleep0s.Add(Rd32(c.esp));
  f_00456610_lifted(c);
}

// RMPTFX: waits for its update thread (thiscall); timed from the handoff.
void f_00964B90(Ctx& c) {
  if (!On() || Rd32(c.esp) != kRmptfxJoin) return f_00964B90_lifted(c);
  const uint64_t t0 = Now();
  f_00964B90_lifted(c);
  g_rmptfx.Add(Now() - t0);
}

// Audio: waits for its update thread (only from the handoff).
void f_007BBCD0(Ctx& c) {
  if (!On()) return f_007BBCD0_lifted(c);
  const uint64_t t0 = Now();
  f_007BBCD0_lifted(c);
  g_audio.Add(Now() - t0);
}

// The render thread's frame: runs the command buffer main recorded.
void f_00863B50(Ctx& c) {
  if (!On()) return f_00863B50_lifted(c);
  static uint64_t last = 0;  // the render thread's only
  const uint64_t t0 = Now();
  if (last) g_render_period.Add(t0 - last);
  last = t0;
  f_00863B50_lifted(c);
  g_render_frame.Add(Now() - t0);
}

// Waits for `count` jobs (cdecl: count, jobs, wait all), after running
// those no worker has taken yet (jobs.cpp).
void f_00460D50(Ctx& c) {
  HelpWithJobs(c);
  if (!On()) return f_00460D50_lifted(c);
  const uint32_t caller = Rd32(c.esp), count = Rd32(c.esp + 4);
  SampleJobs();
  const uint64_t t0 = Now();
  f_00460D50_lifted(c);
  const uint64_t ns = Now() - t0;
  std::lock_guard<std::mutex> l(g_jobs_mu);
  JobWaits& j = g_jobs[caller];
  j.calls++;
  j.jobs += count;
  j.jobs_max = std::max<uint64_t>(j.jobs_max, count);
  j.ns += ns;
  j.ns_max = std::max(j.ns_max, ns);
}

// sysIpcCreateThread (cdecl: entry, argument, stack size, priority, name).
void f_00456500(Ctx& c) {
  if (On()) {
    const uint32_t name = Rd32(c.esp + 20);
    std::lock_guard<std::mutex> l(g_threads_mu);
    g_threads[{Rd32(c.esp + 4), Rd32(c.esp + 8)}].push_back(
        {name ? GStr(name) : "", int32_t(Rd32(c.esp + 16))});
  }
  f_00456500_lifted(c);
}

// The start of every thread sysIpcCreateThread makes (stdcall: its start
// block, entry and argument first).
void f_00456460(Ctx& c) {
  if (On()) {
    const uint32_t block = Rd32(c.esp + 4), entry = Rd32(block), arg = Rd32(block + 4);
    Started s{"", 0};
    {
      std::lock_guard<std::mutex> l(g_threads_mu);
      auto it = g_threads.find({entry, arg});
      if (it != g_threads.end() && !it->second.empty()) {
        s = it->second.front();
        it->second.pop_front();
      }
    }
#if defined(__APPLE__)
    if (!s.name.empty()) pthread_setname_np(s.name.substr(0, 63).c_str());
#endif
    WarnF("gtaiv: perf: thread %04X \"%s\" entry %08X arg %08X priority %d, QoS %s\n",
          CurrentThread()->tid, s.name.c_str(), entry, arg, s.priority, platform::ThreadQosName());
  }
  f_00456460_lifted(c);
}

}  // namespace recomp::gen
