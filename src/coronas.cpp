// The coronas' limit (FusionFix's coronas, by its idea): 768 lights a
// frame can have a corona (the glow of street lamps, car lights, distant
// lights); past that the rest are dropped, and which ones changes from
// frame to frame: lights flicker where there are many, more so with the
// distant lights seen from the street. The two arrays (the registered
// coronas, 0x40 bytes each at 014332D0, and the ones drawn, three buffers
// of 256 x 0x20 bytes at 014272D0) are moved to guest memory of their own
// for 49152 (3 << 14) each: the 42 instructions that address them (all
// of them in the game) use the new bases, the buffers' index shift is 14
// instead of 8, and the three limit checks are raised. GTAIV_CORONA_LIMIT=0
// keeps the game's own.

#include <cstdint>
#include <cstring>
#include <mutex>

#include "hook_util.h"
#include "option.h"
#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

using namespace hook;

namespace {

constexpr uint32_t kOldA = 0x014332D0, kOldB = 0x014272D0;
constexpr uint32_t kShift = 14, kLimit = 3u << kShift;  // 49152

uint32_t g_a = kOldA, g_b = kOldB, g_shift = 8, g_limit = 768;

void Init() {
  static std::once_flag once;
  std::call_once(once, [] {
    if (!OptionOn("GTAIV_CORONA_LIMIT", true)) return;
    const uint32_t a_size = kLimit * 0x40, b_size = kLimit * 0x20;
    const uint64_t a = g_proc->vm.Reserve(0, a_size), b = g_proc->vm.Reserve(0, b_size);
    if (!a || !b || !g_proc->vm.Commit(a, a_size, Prot::RW) || !g_proc->vm.Commit(b, b_size, Prot::RW)) return;
    g_a = uint32_t(a);
    g_b = uint32_t(b);
    g_shift = kShift;
    g_limit = kLimit;
  });
}

// Whichever instruction comes first sets up the new arrays.
uint32_t A(uint32_t off) {
  Init();
  return g_a + off;
}
uint32_t B(uint32_t off) {
  Init();
  return g_b + off;
}
uint32_t Limit() {
  Init();
  return g_limit;
}
uint32_t Shift() {
  Init();
  return g_shift;
}

// fstp dword [va]
void Fstp(Ctx& c, uint32_t va) {
  Wr32(va, x87::StoreF32(c.fpu));
  x87::Pop(c.fpu);
}

}  // namespace

// A loop over the registered coronas: mov esi, 014332D8.
void hook_00A6EDD4(Ctx& c) { c.esi = A(0x8); }

// The limits: registering (cmp esi, 300h), drawing (cmp edi, 300h), and the
// clearing loop's end (cmp eax, 768 * 40h).
void hook_00A6F7B9(Ctx& c) { CmpFlags<4>(c, c.esi, Limit()); }
void hook_00A6EEB2(Ctx& c) { CmpFlags<4>(c, c.edi, Limit()); }
void hook_00A6EFC9(Ctx& c) { CmpFlags<4>(c, c.eax, Limit() * 0x40); }

// The drawn buffers' index ((buffer << 8) + i) << 5: shl reg, 8.
void hook_00A6EEDF(Ctx& c) { c.eax <<= Shift(); }
void hook_00A6F2DA(Ctx& c) { c.ecx <<= Shift(); }
void hook_00A6F14E(Ctx& c) { c.ecx <<= Shift(); }

// The accesses, each as the game's instruction with the array's new base.
void hook_00A6EFC0(Ctx& c) { Wr32(c.eax + A(0x20), c.ecx); }
void hook_00A6F7D8(Ctx& c) { Wr32(c.eax + A(0x3C), Rd32(c.eax + A(0x3C)) | 0x6); }
void hook_00A6F7DF(Ctx& c) { Wr32(c.eax + A(0x20), c.ecx); }
void hook_00A6F7E9(Ctx& c) { Wr8(c.eax + A(0x39), uint8_t(c.ecx)); }
void hook_00A6F7F3(Ctx& c) { Wr8(c.eax + A(0x38), uint8_t(c.edx)); }
void hook_00A6F7FD(Ctx& c) { std::memcpy(GPtr(c.eax + A(0x30)), c.xmm[0].data(), 4); }
void hook_00A6F80B(Ctx& c) { Wr8(c.eax + A(0x3A), uint8_t(c.edx)); }
void hook_00A6F816(Ctx& c) { std::memcpy(GPtr(c.eax + A(0x34)), c.xmm[0].data(), 4); }
void hook_00A6F82A(Ctx& c) { std::memcpy(GPtr(c.eax + A(0x4)), c.xmm[0].data(), 4); }
void hook_00A6F832(Ctx& c) { Fstp(c, c.eax + A(0x0)); }
void hook_00A6F83E(Ctx& c) { std::memcpy(GPtr(c.eax + A(0x8)), c.xmm[1].data(), 4); }
void hook_00A6F84E(Ctx& c) { Fstp(c, c.eax + A(0xC)); }
void hook_00A6F860(Ctx& c) { c.edx = Rd32(c.eax + A(0x3C)); }
void hook_00A6F875(Ctx& c) { std::memcpy(GPtr(c.eax + A(0x24)), c.xmm[0].data(), 4); }
void hook_00A6F888(Ctx& c) { std::memcpy(GPtr(c.eax + A(0x28)), c.xmm[0].data(), 4); }
void hook_00A6F896(Ctx& c) { std::memcpy(GPtr(c.eax + A(0x2C)), c.xmm[0].data(), 4); }
void hook_00A6F89E(Ctx& c) { Wr32(c.eax + A(0x3C), c.ecx); }
void hook_00A6F8B2(Ctx& c) { Fstp(c, c.eax + A(0x10)); }
void hook_00A6F8B8(Ctx& c) { std::memcpy(GPtr(c.eax + A(0x14)), c.xmm[0].data(), 4); }
void hook_00A6F8C0(Ctx& c) { std::memcpy(GPtr(c.eax + A(0x18)), c.xmm[1].data(), 4); }
void hook_00A6F8CB(Ctx& c) { Fstp(c, c.eax + A(0x1C)); }
void hook_00A6F8D1(Ctx& c) { Wr32(c.eax + A(0x3C), Rd32(c.eax + A(0x3C)) | 0x10); }
void hook_00A6F8E9(Ctx& c) { Wr32(c.eax + A(0x3C), c.ecx); }
void hook_00A6EEE7(Ctx& c) { std::memcpy(GPtr(c.eax + B(0x0)), c.xmm[2].data(), 4); }
void hook_00A6EEF5(Ctx& c) { std::memcpy(GPtr(c.eax + B(0x4)), c.xmm[2].data(), 4); }
void hook_00A6EF03(Ctx& c) { std::memcpy(GPtr(c.eax + B(0x8)), c.xmm[2].data(), 4); }
void hook_00A6EF0B(Ctx& c) { std::memcpy(GPtr(c.eax + B(0xC)), c.xmm[0].data(), 4); }
void hook_00A6EF13(Ctx& c) { Fstp(c, c.eax + B(0x14)); }
void hook_00A6EF19(Ctx& c) { Wr8(c.eax + B(0x18), uint8_t(c.edx)); }
void hook_00A6EF23(Ctx& c) { Wr8(c.eax + B(0x19), uint8_t(c.edx)); }
void hook_00A6EF33(Ctx& c) { Wr8(c.eax + B(0x1A), uint8_t(c.edx)); }
void hook_00A6EF39(Ctx& c) { std::memcpy(GPtr(c.eax + B(0x10)), c.xmm[1].data(), 4); }
void hook_00A6EF41(Ctx& c) { Wr8(c.eax + B(0x1B), uint8_t(c.ecx)); }
void hook_00A6F156(Ctx& c) { CmpFlags<1>(c, Rd8(c.ecx + B(0x1B)), 0); }
void hook_00A6F2E2(Ctx& c) { c.esi = Rd8(c.ecx + B(0x18)); }
void hook_00A6F2E9(Ctx& c) { c.eax = Rd8(c.ecx + B(0x19)); }
void hook_00A6F2F0(Ctx& c) { c.edx = Rd8(c.ecx + B(0x1A)); }
void hook_00A6F2F7(Ctx& c) { std::memcpy(c.xmm[1].data(), GPtr(c.ecx + B(0x0)), 16); }
void hook_00A6F2FE(Ctx& c) { LoadSs(c.xmm[0], RdF32(c.ecx + B(0xC))); }
void hook_00A6F306(Ctx& c) { LoadSs(c.xmm[4], RdF32(c.ecx + B(0x10))); }
void hook_00A6F4AD(Ctx& c) { LoadSs(c.xmm[3], RdF32(c.ecx + B(0x14))); }

}  // namespace recomp::gen
