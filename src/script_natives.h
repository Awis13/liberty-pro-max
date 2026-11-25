// Calling the game's script commands (natives) from the port's own code:
// the lookup the scripts' loader uses, and a call with the context a
// command's handler takes. The caller gives guest stack space below its
// own (kSize bytes at `base`) and keeps the registers it needs.
#pragma once

#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen::natives {

constexpr gaddr kFindNative = 0x00626AF0;       // esi: the hash; returns the handler
constexpr gaddr kFindPlayerPed = 0x007CD230;    // the player's CPed, 0 while none
constexpr gaddr kCutsceneRunning = 0x007F02B0;  // whether a cutscene is running (al)

// A handler takes one argument, a pointer to its call context: +00 where
// it writes its results, +04 the number of arguments, +08 where they
// are. Here the context is at `base`, the arguments and results (the
// same words) at base + kArgs, and base + kOut on is free for what a
// command writes through a pointer argument.
constexpr uint32_t kCtx = 0x00, kArgs = 0x60, kOut = 0xA0, kSize = 0xC0;

// The handler of the command with this hash, 0 if there is none.
inline gaddr Find(Ctx& c, uint32_t hash) {
  c.esi = hash;
  return gaddr(CallGuest(c, kFindNative, {}));
}

// Calls a handler with 4-byte arguments; returns its first result.
inline uint32_t Call(Ctx& c, uint32_t base, gaddr handler, std::initializer_list<uint32_t> args) {
  const uint32_t ctx = base + kCtx, stack = base + kArgs;
  std::memset(GPtr(ctx), 0, kOut);
  Wr32(ctx + 0x00, stack);  // results
  Wr32(ctx + 0x04, uint32_t(args.size()));
  Wr32(ctx + 0x08, stack);  // arguments
  uint32_t i = 0;
  for (uint32_t a : args) Wr32(stack + 4 * i++, a);
  CallGuest(c, handler, {ctx});
  return Rd32(stack);
}

}  // namespace recomp::gen::natives
