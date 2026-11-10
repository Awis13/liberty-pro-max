#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "runtime/lifted.h"

namespace recomp::gen { void hook_008556E9(Ctx&); }

void Check(float target, float elapsed, bool bypass) {
  recomp::Ctx context{};
  recomp::x87::Init(context.fpu);
  context.esp = 0x1000;
  context.eflags = 0x400; // The compare must preserve the direction flag.
  const auto bits = [](float f) { uint32_t n; std::memcpy(&n, &f, sizeof n); return n; };
  recomp::x87::LoadF32(context.fpu, bits(elapsed));
  recomp::x87::LoadF32(context.fpu, bits(target));
  const auto top = context.fpu.top;
  recomp::gen::hook_008556E9(context);
  recomp::Flags flags;
  recomp::UnpackEFlags(flags, context.eflags);
  assert((flags.cf || flags.zf) == (bypass || target <= elapsed));
  assert(!flags.pf && !flags.of && !flags.sf && !flags.af);
  assert(context.fpu.top == ((top + 2) & 7));
  assert(context.fpu.empty == 0xff);
  assert(context.esp == 0x1000);
  assert(flags.df);
}
int main(int argc, char** argv) {
  assert(argc == 3);
  if (std::strcmp(argv[1], "unset") == 0) unsetenv("GTAIV_FPS_LIMIT");
  else setenv("GTAIV_FPS_LIMIT", argv[1], 1);
  const bool bypass = std::atoi(argv[2]) != 0;
  Check(.015f, .007f, bypass); // The observed native wait must not impose 66.7 fps.
  Check(.015f, .015f, bypass);
  Check(.015f, .020f, bypass);
  std::puts("Native frame-limit branch and x87 state verified");
}
