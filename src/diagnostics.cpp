// Logging around engine functions whose failures are silent otherwise.
//   GTAIV_ALLOC_LOG=1: allocations of 1 MB and more from the game heaps;
//   GTAIV_ALLOC_LOG=2: also counts every allocation by caller and prints
//   the top callers when one fails.

#include <algorithm>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

#include "runtime/lifted.h"
#include "runtime/log.h"
#include "runtime/runtime.h"

namespace recomp::gen {

namespace {

int AllocLog() {
  static const int v = [] {
    const char* e = Env("ALLOC_LOG");
    return e ? *e - '0' : 0;
  }();
  return v;
}

struct Tally {
  uint64_t count = 0, bytes = 0;
};
std::mutex g_tally_mu;
std::map<uint32_t, Tally> g_tally;  // by return address

void PrintTally() {
  std::vector<std::pair<uint32_t, Tally>> v;
  {
    std::lock_guard<std::mutex> l(g_tally_mu);
    v.assign(g_tally.begin(), g_tally.end());
  }
  std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second.bytes > b.second.bytes; });
  for (size_t i = 0; i < v.size() && i < 25; i++)
    WarnF("gtaiv:   %08X  %8llu allocations  %8llu KB\n", v[i].first,
          (unsigned long long)v[i].second.count, (unsigned long long)(v[i].second.bytes >> 10));
}

}  // namespace

// rage::sysMemSimpleAllocator::Allocate (thiscall, size, align, flags;
// ret 0xC): a null result is logged with the heap it came from.
void f_00454860_lifted(Ctx& c);
void f_00454860(Ctx& c) {
  const uint32_t self = c.ecx, size = Rd32(c.esp + 4), align = Rd32(c.esp + 8);
  const uint32_t caller = Rd32(c.esp);
  const int log = AllocLog();
  if (log >= 1 && size >= 0x100000)
    WarnF("gtaiv: alloc %X from %08X (heap %08X)\n", size, caller, self);
  if (log >= 2) {
    std::lock_guard<std::mutex> l(g_tally_mu);
    Tally& t = g_tally[caller];
    t.count++;
    t.bytes += size;
  }
  f_00454860_lifted(c);
  if (c.eax == 0) {
    WarnF("gtaiv: sysMemSimpleAllocator %08X: Allocate(%X, align %X) failed (heap %08X size %X)\n",
          self, size, align, Rd32(self + 0x4C), Rd32(self + 0x50));
    static bool printed = false;
    if (log >= 2 && !printed) {
      printed = true;
      PrintTally();
    }
  }
}

// Frontend state setter (cdecl, state): with GTAIV_FE_LOG=1, each change of
// state or caller is logged.
void f_007BDD80(Ctx& c) {
  const uint32_t state = Rd32(c.esp + 4), caller = Rd32(c.esp);
  static const bool log = [] {
    const char* e = Env("FE_LOG");
    return e && *e == '1';
  }();
  static uint32_t last_state = ~0u, last_caller = 0;
  if (log && (state != last_state || caller != last_caller)) {
    WarnF("gtaiv: frontend state %X from %08X\n", state, caller);
    last_state = state;
    last_caller = caller;
  }
  Wr32(0x010FC650, state);
  c.eax = (c.eax & ~0xFFu) | 1;
  c.esp += 4;
}

}  // namespace recomp::gen
