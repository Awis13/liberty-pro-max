// The game's statically linked CRT (MSVC 2005): memmove and the SSE2 math
// the compiler calls in place of sin, cos, atan2, pow, acos and tan
// (__libm_sse2_*: arguments in xmm0 and xmm1, the result in xmm0, every
// xmm register volatile), run as the host's instead of hundreds of
// translated instructions a call. memmove is exact; the math may differ
// from the CRT's in the last bit (both are within an ulp of the true
// value).
//
// GTAIV_CRT_CHECK=1 runs each math function's translated original as well
// and, after 20000 calls of one, reports how often the host's result
// differs from it and by how many ulps at most; the original's result is
// kept then.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

void f_00CF59F9_lifted(Ctx& c);
void f_00CF5841_lifted(Ctx& c);
void f_00CF99C0_lifted(Ctx& c);
void f_00CF6861_lifted(Ctx& c);
void f_00CF7456_lifted(Ctx& c);
void f_00CF7930_lifted(Ctx& c);

namespace {

bool Checking() {
  static const bool on = [] {
    const char* e = std::getenv("GTAIV_CRT_CHECK");
    return e && *e && *e != '0';
  }();
  return on;
}

double Arg(const Ctx& c, int reg) {
  double v;
  std::memcpy(&v, c.xmm[reg].data(), 8);
  return v;
}

void Result(Ctx& c, double v) {
  std::memcpy(c.xmm[0].data(), &v, 8);
  c.esp += 4;  // ret
}

// The check: the original's result against the host's.
struct Check {
  const char* name;
  uint64_t calls = 0, differ = 0, max_ulps = 0;

  void Note(double original, double host) {
    uint64_t a, b;
    std::memcpy(&a, &original, 8);
    std::memcpy(&b, &host, 8);
    if (a != b) {
      differ++;
      const uint64_t d = a > b ? a - b : b - a;  // same sign: the ulps apart
      if (d > max_ulps) max_ulps = d;
    }
    if (++calls == 20000)
      std::fprintf(stderr, "gtaiv: CRT %s: %llu of %llu results differ, by %llu ulps at most\n",
                   name, (unsigned long long)differ, (unsigned long long)calls,
                   (unsigned long long)max_ulps);
  }
};

template <void (*Original)(Ctx&)>
void Unary(Ctx& c, double (*host)(double), Check& check) {
  const double x = Arg(c, 0);
  if (Checking()) {
    Original(c);
    check.Note(Arg(c, 0), host(x));
    return;
  }
  Result(c, host(x));
}

template <void (*Original)(Ctx&)>
void Binary(Ctx& c, double (*host)(double, double), Check& check) {
  const double x = Arg(c, 0), y = Arg(c, 1);
  if (Checking()) {
    Original(c);
    check.Note(Arg(c, 0), host(x, y));
    return;
  }
  Result(c, host(x, y));
}

double Sin(double x) { return std::sin(x); }
double Cos(double x) { return std::cos(x); }
double Atan2(double y, double x) { return std::atan2(y, x); }
double Pow(double x, double y) { return std::pow(x, y); }
double Acos(double x) { return std::acos(x); }
double Tan(double x) { return std::tan(x); }

Check g_sin{"sin"}, g_cos{"cos"}, g_atan2{"atan2"}, g_pow{"pow"}, g_acos{"acos"}, g_tan{"tan"};

}  // namespace

void f_00CF59F9(Ctx& c) { Unary<f_00CF59F9_lifted>(c, Cos, g_cos); }
void f_00CF5841(Ctx& c) { Unary<f_00CF5841_lifted>(c, Sin, g_sin); }
void f_00CF99C0(Ctx& c) { Binary<f_00CF99C0_lifted>(c, Atan2, g_atan2); }
void f_00CF6861(Ctx& c) { Binary<f_00CF6861_lifted>(c, Pow, g_pow); }
void f_00CF7456(Ctx& c) { Unary<f_00CF7456_lifted>(c, Acos, g_acos); }
void f_00CF7930(Ctx& c) { Unary<f_00CF7930_lifted>(c, Tan, g_tan); }

// void* __cdecl memmove(void* dst, const void* src, size_t n) (memcpy too):
// returns dst.
void f_00CF52B0(Ctx& c) {
  const uint32_t dst = Rd32(c.esp + 4), src = Rd32(c.esp + 8), n = Rd32(c.esp + 12);
  if (n) std::memmove(GPtr(dst), GPtr(src), n);
  c.eax = dst;
  c.esp += 4;
}

}  // namespace recomp::gen
