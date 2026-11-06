// Rendering and streaming odds and ends. Each one is an option (the
// launcher's Advanced tab sets them), on unless said otherwise:
//   GTAIV_HIGH_DETAIL_REFLECTIONS (off by default): the reflection is
//     drawn from what every render phase found visible, not from its own
//     low detail set (heavy for the GPU);
//   GTAIV_SHAKE_BLUR_FIX: no motion blur mask while the camera shakes
//     (drunk, explosions): the mask flickered over lights and the screen;
//   GTAIV_EXTRA_STREAMING_MEMORY (off by default): streaming may use up
//     to 1.5 times the free memory while the game's address space is under
//     2 GB, tapering to the game's 0.9 at 2.9 GB;
//   GTAIV_MENU_EXTRA_INFO (off by default): the pause menu's info line
//     shows the game's version on the Game tab, and the IMG files open and
//     the streaming memory used on the Graphics tab.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include "hook_util.h"
#include "option.h"
#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

void f_0040EA20_lifted(Ctx& c);

using namespace hook;


// CRenderPhaseReflection adds its entities to the draw list with one call
// (00B6FD70: phase index [edi+938], list flags 131Dh, kind 8, cdecl, all
// pushed). The phase index picks one of the per-phase visibility lists
// (24 slots of 54h bytes at 01502B88), so the reflection only draws what
// its own coarse scan kept. Instead the call is made for the lists of
// phases 0 to 11, kind 0 (the general path), flags 11Dh (bits 9 and 12
// clear, which add list 7 and the sub-lists 3 of lists 2 and 4).
namespace {
bool HighDetailReflections() {
  static const bool on = OptionOn("GTAIV_HIGH_DETAIL_REFLECTIONS", false);
  return on;
}
}  // namespace
void hook_00C5131F(Ctx& c) {  // call 00B6FD70
  if (!HighDetailReflections()) {
    CallGuest(c, 0x00B6FD70, {});
    return;
  }
  for (uint32_t phase = 0; phase < 12; ++phase) CallGuest(c, 0x00B6FD70, {phase, 0x11D, 0});
}

// CDeferredLightingHelper::BeginMotionBlurMask (00A95B00) before the
// scene's motion blur: while the camera shakes (drunk, explosions) the
// stencil mask it sets up flickers over lights and the screen. Not set up
// while the root camera ([00F488EC], what GET_ROOT_CAM returns) has a
// shake running: its shaker ([cam+110]) has byte [+27C] = 1 from the
// start of a shake (00A70C70) until it ends (00A716B5, which also clears
// the mask flag [00F4C819] the function tests).
void hook_0096ABCF(Ctx& c) {  // call 00A95B00
  static const bool on = OptionOn("GTAIV_SHAKE_BLUR_FIX", true);
  if (on) {
    const uint32_t cam = Rd32(0x00F488EC);
    const uint32_t shaker = cam ? Rd32(cam + 0x110) : 0;
    if (shaker && Rd8(shaker + 0x27C) == 1) return;
  }
  CallGuest(c, 0x00A95B00, {});
}

// grcResourceCache::GetAvailableFreeMemory (thiscall, float share; ret 4):
// the memory streaming may still take, from the total times the share
// (0.9 at every call site). The share from how much of the game's address
// space is reserved: 1.5 while at most half of it is, 0.9 from 71% (the
// proportions of 2 and 2.9 GB of a 4 GB process), in between a straight
// line. The game here has the space below the runtime's thunks
// (kThunkBase, just under 2 GB: it is not large-address-aware), so the
// thresholds are shares of that, not of 4 GB. Reserved, not only
// committed, memory is what runs out in a 32-bit process; measured at most
// twice a second (the guest allocator's regions walked).
namespace {
uint64_t GuestReserved() {
  auto& vm = g_proc->vm;
  uint64_t va = 0x10000, used = 0;
  while (va < 0x100000000ull) {
    GuestAllocator::Region r;
    if (vm.Find(va, &r)) {
      used += r.base + r.size - va;
      va = r.base + r.size;
      continue;
    }
    uint64_t base = 0, size = 0;
    vm.FreeGap(va, &base, &size);
    if (!size) break;
    va = base + size;
  }
  return used;
}

float StreamingShare() {
  constexpr double kSpace = double(kThunkBase);
  constexpr double kLow = kSpace * 2048.0 / 4096.0, kHigh = kSpace * 2900.0 / 4096.0;
  constexpr float kMost = 1.5f, kLeast = 0.9f;
  static std::mutex mu;
  static std::chrono::steady_clock::time_point next;
  static float share = kLeast;
  std::lock_guard<std::mutex> l(mu);
  const auto now = std::chrono::steady_clock::now();
  if (now >= next) {
    next = now + std::chrono::milliseconds(500);
    const double used = double(GuestReserved());
    if (used < kLow) share = kMost;
    else if (used >= kHigh) share = kLeast;
    else share = kMost - float((used - kLow) / (kHigh - kLow)) * (kMost - kLeast);
  }
  return share;
}
}  // namespace
void f_0040EA20(Ctx& c) {
  static const bool on = OptionOn("GTAIV_EXTRA_STREAMING_MEMORY", false);
  if (on) WrF32(c.esp + 4, StreamingShare());
  f_0040EA20_lifted(c);
}

// The pause menu's info line (lea edx, [esp+30]: the text it was built
// in, then drawn by 0088BD30). On the Game tab ([010FBF24] = 0) the game's
// version is added below it, on the Graphics tab (31h) the IMG files open
// (255 entries of A0h bytes at 01399800, handle at +98h, -1 when none)
// and the streaming memory (CStreamingInfoManager at 00F493F0: virtual
// limit +20h, used +24h; physical left +2Ch, used +30h). The longer text
// goes into a buffer of our own in guest memory.
namespace {
constexpr uint32_t kTextChars = 2048;

uint32_t TextBuffer() {
  static uint32_t buf = 0;
  static std::once_flag once;
  std::call_once(once, [] {
    const uint32_t size = kTextChars * 2;
    const uint64_t va = g_proc->vm.Reserve(0, size);
    if (va && g_proc->vm.Commit(va, size, Prot::RW)) buf = uint32_t(va);
  });
  return buf;
}

std::string ExtraInfo(uint32_t tab) {
  const std::string line = "~n~                        ~p~";
  if (tab == 0) return line + "GTAIV.exe: v1.0.8.0 (native recompilation)";
  constexpr uint32_t kImgFiles = 0x01399800, kImgCount = 255, kImgSize = 0xA0;
  uint32_t open = 0;
  for (uint32_t i = 0; i < kImgCount; ++i)
    if (Rd32(kImgFiles + i * kImgSize + 0x98) != 0xFFFFFFFF) ++open;
  constexpr uint32_t kInfo = 0x00F493F0;
  constexpr uint32_t kMb = 1024 * 1024;
  auto mb = [](uint32_t bytes) { return (uint64_t(bytes) + kMb / 2) / kMb; };
  const uint64_t phys_used = mb(Rd32(kInfo + 0x30)), phys_left = mb(Rd32(kInfo + 0x2C));
  const uint64_t virt_used = mb(Rd32(kInfo + 0x24)), virt_max = mb(Rd32(kInfo + 0x20));
  char text[256];
  std::snprintf(text, sizeof text,
                "IMG files: %u / %u; streaming memory: %llu / %llu MB; virtual heap: %llu / %llu MB",
                open, kImgCount, (unsigned long long)phys_used,
                (unsigned long long)(phys_used + phys_left), (unsigned long long)virt_used,
                (unsigned long long)virt_max);
  std::string out = line + text;
  if (open >= kImgCount) out += "; ~r~all IMG slots taken, streaming will break";
  return out;
}
}  // namespace
void hook_00488EE6(Ctx& c) {  // lea edx, [esp+30]
  static const bool on = OptionOn("GTAIV_MENU_EXTRA_INFO", false);
  c.edx = c.esp + 0x30;
  if (!on) return;
  const uint32_t tab = Rd32(0x010FBF24);
  if (tab != 0 && tab != 0x31) return;
  const uint32_t buf = TextBuffer();
  if (!buf) return;
  const std::string extra = ExtraInfo(tab);
  uint32_t n = 0;
  for (; n + extra.size() + 1 < kTextChars; ++n) {
    const uint16_t ch = Rd16(c.edx + 2 * n);
    if (!ch) break;
    Wr16(buf + 2 * n, ch);
  }
  for (const char ch : extra) Wr16(buf + 2 * n++, uint8_t(ch));
  Wr16(buf + 2 * n, 0);
  c.edx = buf;
}

}  // namespace recomp::gen
