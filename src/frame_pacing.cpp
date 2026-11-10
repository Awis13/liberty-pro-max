// The host PresentFrame limiter owns an explicitly configured frame rate.
// The guest frame-timing loop also yields until its own deadline:
// (1 - 0.1)/60 s is 15 ms. Leaving both active makes FPS_LIMIT=0
// run at about 66.7 fps while repeatedly calling Sleep(0).
#include <cmath>
#include <cstdlib>

#include "runtime/lifted.h"

namespace recomp::gen {
namespace {
bool HostFrameLimitConfigured() {
  static const bool configured = [] {
    const char* value = std::getenv("GTAIV_FPS_LIMIT");
    if (!value || !*value) return false;
    char* end = nullptr;
    const double fps = std::strtod(value, &end);
    return end != value && !*end && std::isfinite(fps) && fps >= 0;
  }();
  return configured;
}
}  // namespace

// fcomip st(0),st(1); fstp st(0), immediately before jbe 00855733.
// Keep both x87 pops and the guest's clock update after the wait. Only
// bypass the redundant wait; no simulation step or frame is manufactured.
void hook_008556E9(Ctx& c) {
  const auto result = x87::CompareStEFlags(c.fpu, 1, false);
  x87::Pop(c.fpu);
  x87::StoreSt(c.fpu, 0);
  x87::Pop(c.fpu);
  Flags flags;
  UnpackEFlags(flags, c.eflags);
  flags.zf = result.zf;
  flags.pf = result.pf;
  flags.cf = HostFrameLimitConfigured() || result.cf;
  flags.of = flags.sf = flags.af = false;
  c.eflags = PackEFlags(flags);
}
}  // namespace recomp::gen
