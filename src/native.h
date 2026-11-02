// Native versions of hot game functions (src/native_*.cpp): the x86 code
// translated to plain C++ by tools/x86n.py (registers as locals, the
// function's own stack frame a local array, the SSE registers' low lanes
// host floats, no flags but the ones a branch reads), so that it compiles
// to what an ARM compiler would make of the same source. Same operations
// in the same order: the results are the original's bit for bit (checked
// against the x86 code under Unicorn on random inputs).
//
//   GTAIV_NATIVE=0        the lifted originals instead (on by default);
//   GTAIV_NATIVE_OFF=VA,.. the lifted originals of those only.

#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "runtime/lifted.h"
#include "runtime/runtime.h"
#include "runtime/sse.h"
#include "runtime/x87.h"

namespace recomp::gen::native {

template <class T>
inline T Rd(uint32_t va) {
  T v;
  std::memcpy(&v, ::recomp::g_guest_base + va, sizeof v);
  return v;
}
template <class T>
inline void Wr(uint32_t va, T v) {
  std::memcpy(::recomp::g_guest_base + va, &v, sizeof v);
}
template <class T>
inline T FrRd(const uint8_t* f, int off) {
  T v;
  std::memcpy(&v, f + off, sizeof v);
  return v;
}
template <class T>
inline void FrWr(uint8_t* f, int off, T v) {
  std::memcpy(f + off, &v, sizeof v);
}
inline float F(uint32_t u) {
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
inline uint32_t U(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}

// An SSE register's four lanes.
struct V {
  uint32_t l[4];
};

inline void XmmIn(const Ctx& c, V& x0, V& x1, V& x2, V& x3, V& x4, V& x5, V& x6, V& x7) {
  V* x[8] = {&x0, &x1, &x2, &x3, &x4, &x5, &x6, &x7};
  for (int i = 0; i < 8; i++) std::memcpy(x[i], c.xmm[i].data(), 16);
}
inline void XmmOut(Ctx& c, const V& x0, const V& x1, const V& x2, const V& x3, const V& x4, const V& x5,
                   const V& x6, const V& x7) {
  const V* x[8] = {&x0, &x1, &x2, &x3, &x4, &x5, &x6, &x7};
  for (int i = 0; i < 8; i++) std::memcpy(c.xmm[i].data(), x[i], 16);
}

// CVTTSS2SI / CVTTPS2DQ: truncated, 0x80000000 for NaN and out of range.
inline uint32_t CvttF2I(float f) {
  if (!(f > -2147483904.0f && f < 2147483648.0f)) return 0x80000000u;
  return uint32_t(int32_t(f));
}

// CVTTSD2SI.
inline uint32_t CvttD2I(double d) {
  if (!(d > -2147483649.0 && d < 2147483648.0)) return 0x80000000u;
  return uint32_t(int32_t(d));
}

// A double in an SSE register's lanes 2k, 2k+1.
inline double GetD(const V& v, int k) {
  double d;
  std::memcpy(&d, &v.l[2 * k], 8);
  return d;
}
inline void SetD(V& v, int k, double d) { std::memcpy(&v.l[2 * k], &d, 8); }

// FIST(P) m32 under the control word's rounding; 0x80000000 when out of range.
inline int32_t FistF(const Ctx& c, float f) {
  if (!(f >= -2147483648.0f && f < 2147483648.0f)) return int32_t(0x80000000u);
  switch ((c.fpu.cw >> 10) & 3u) {
    case 0: return int32_t(std::nearbyint(f));  // the host rounds to nearest even
    case 1: return int32_t(std::floor(f));
    case 2: return int32_t(std::ceil(f));
    default: return int32_t(f);
  }
}

// FNSTSW's C0, C2, C3 after FCOM of a with b (unordered: all three).
inline uint32_t FcomSw(double a, double b) {
  if (a != a || b != b) return 0x4500u;
  return a < b ? 0x0100u : a == b ? 0x4000u : 0u;
}

// x87 arithmetic after a FLDCW in translated code assumes the control
// word is back at PC=24/nearest (the game's float-to-int conversions set
// truncation only around a FISTP): reported once if it is not.
inline void CheckCw(const Ctx& c, uint32_t va) {
  if ((c.fpu.cw & 0x0F00u) != 0) {
    static bool said;
    if (!said) {
      said = true;
      std::fprintf(stderr, "gtaiv: native: x87 arithmetic at %08X under control word %04X\n", va, c.fpu.cw);
    }
  }
}

// Calls out of translated code: to a lifted (or replaced) function, or
// through the dispatcher for an indirect target; a tail call returns for
// the translated function.
#define CALL_DIRECT(fn, va, nret) fn(c)
#define CALL_TAIL(fn, va) fn(c)
#define CALL_INDIRECT(t, site) ::recomp::Dispatch(c, t, site)

inline bool On() {
  static const bool on = [] {
    const char* e = std::getenv("GTAIV_NATIVE");
    return !e || *e != '0';
  }();
  return on;
}

// Whether a native version runs for this call: GTAIV_NATIVE on, the
// function not in GTAIV_NATIVE_OFF (a comma-separated list of addresses,
// to bisect), MXCSR as usual and, for a function with x87 arithmetic, the
// FPU at PC=24 with RC=nearest (its floats are the x87's results then).
bool Enabled(uint32_t va);
inline bool Use(Ctx& c, uint32_t va, bool x87) {
  if (!On() || !::recomp::sse::fast::UsualMxcsr(c.mxcsr)) return false;
  if (x87 && (c.fpu.cw & 0x0F00u) != 0) return false;
  return Enabled(va);
}

// A callee's float result, left in st0 by it: taken off the x87 model.
inline uint32_t X87PopF32(Ctx& c) {
  const uint32_t v = ::recomp::x87::StoreF32(c.fpu);
  ::recomp::x87::Pop(c.fpu);
  return v;
}
// This function's float result, left in st0 for the caller.
inline void X87PushF32(Ctx& c, uint32_t v) { ::recomp::x87::LoadF32(c.fpu, v); }

}  // namespace recomp::gen::native
