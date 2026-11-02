// Small helpers for hooks: what the replaced instructions did (flags,
// SSE lanes, guest floats).
#pragma once

#include <cstdint>
#include <cstring>

#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen::hook {

inline uint32_t Bits(float f) {
  uint32_t b;
  std::memcpy(&b, &f, 4);
  return b;
}

// The flags of cmp a, b (32-bit or 8-bit) in c.eflags.
template <int N>
inline void CmpFlags(Ctx& c, uint32_t a, uint32_t b) {
  Flags f;
  UnpackEFlags(f, c.eflags);
  lift::Sub<N>(f, lift::U<N>(a), lift::U<N>(b));
  c.eflags = PackEFlags(f);
}

// The flags of test a, b (8-bit) in c.eflags.
inline void TestFlags8(Ctx& c, uint8_t r) {
  Flags f;
  UnpackEFlags(f, c.eflags);
  lift::Logic<1>(f, r);
  c.eflags = PackEFlags(f);
}

// The flags of comiss a, b in c.eflags.
inline void ComissFlags(Ctx& c, float a, float b) {
  Flags f;
  UnpackEFlags(f, c.eflags);
  const bool unordered = a != a || b != b;
  f.zf = unordered || a == b;
  f.pf = unordered;
  f.cf = unordered || a < b;
  f.of = f.sf = f.af = false;
  c.eflags = PackEFlags(f);
}

inline float Lane0(const Xmm& x) {
  float v;
  std::memcpy(&v, x.data(), 4);
  return v;
}

inline void SetLane0(Xmm& x, float v) { std::memcpy(x.data(), &v, 4); }

// movss xmm, m32: the float, the other lanes zero.
inline void LoadSs(Xmm& x, float v) {
  std::memset(x.data(), 0, 16);
  SetLane0(x, v);
}

inline float RdF32(uint32_t va) {
  const uint32_t b = Rd32(va);
  float v;
  std::memcpy(&v, &b, 4);
  return v;
}

inline void WrF32(uint32_t va, float v) { Wr32(va, Bits(v)); }

inline void Push(Ctx& c, uint32_t v) {
  c.esp -= 4;
  Wr32(c.esp, v);
}


// A guest call put where the game's code makes none (inside a hook on an
// ordinary instruction): the code around it keeps values in registers no
// call may change for it, the SSE and x87 ones too. Saved here and given
// back when this goes, all but esp.
struct KeepRegs {
  Ctx& c;
  Ctx saved;
  explicit KeepRegs(Ctx& c) : c(c), saved(c) {}
  ~KeepRegs() {
    const uint32_t esp = c.esp;
    c = saved;
    c.esp = esp;
  }
};

}  // namespace recomp::gen::hook
