// Stability options from ZolikaPatch's list (by its ini's descriptions
// only), done again for 1.0.8.0 where the game's own code shows what to
// change. ZolikaPatch ships both on; here both are off by default (an
// error box tells more than a silent crash, and fewer sleeping objects
// cost CPU), so each is an opt-in on the launcher's Advanced tab:
//   GTAIV_ERRORS_TO_LOG (off by default): the game's fatal error box
//     (00456680, "Unrecoverable fault", SMPA.., D3D.. codes) is not shown;
//     the code and its text go to the log and the game carries on. It may
//     still crash a little later, and then the runtime's crash report says
//     where (ZolikaPatch's TryToSkipAllErrors);
//   GTAIV_NO_PHYSICS_SLEEP (off by default): physics objects (cars above
//     all) go to sleep only when nearly motionless, so they roll to a stop
//     instead of freezing at a crawl. More objects stay awake, which costs
//     some CPU in the physics (ZolikaPatch's NoPhysicsSleep).

#include <cstdint>
#include <string>

#include "hook_util.h"
#include "option.h"
#include "runtime/lifted.h"
#include "runtime/log.h"
#include "runtime/runtime.h"

namespace recomp::gen {

void f_00456680_lifted(Ctx& c);

using namespace hook;

namespace {

// The error codes ("SMPA60", "DD3D50", ...): 193 char* at 01096340; the
// English texts (UTF-16) at the same index from 01096648. 00468F10 picks
// the text for the system's language the same way.
constexpr uint32_t kErrorCodes = 0x01096340, kErrorTextsEnglish = 0x01096648;
constexpr int kErrorCount = 0xC1;

std::string ErrorText(const std::string& code) {
  for (int i = 0; i < kErrorCount; i++) {
    const uint32_t p = Rd32(kErrorCodes + 4 * i);
    if (!p || GStr(p) != code) continue;
    std::string text;
    const uint32_t w = Rd32(kErrorTextsEnglish + 4 * i);
    for (uint32_t k = 0; w && k < 512; k++) {
      const uint16_t ch = Rd16(w + 2 * k);
      if (!ch) break;
      text += ch < 0x80 ? char(ch) : '?';
    }
    return text;
  }
  return "(no text for this code)";
}

}  // namespace

// The game's fatal error (cdecl: code, a, b): a message box with the
// code's text and, when not zero, a and b; then a write to address 0 and
// exit. With GTAIV_ERRORS_TO_LOG the box, the write and the exit are
// skipped: logged, and back to the caller.
void f_00456680(Ctx& c) {
  static const bool on = OptionOn("GTAIV_ERRORS_TO_LOG", false);
  if (!on) {
    f_00456680_lifted(c);
    return;
  }
  const uint32_t code_va = Rd32(c.esp + 4);
  const int32_t a = int32_t(Rd32(c.esp + 8)), b = int32_t(Rd32(c.esp + 12));
  const std::string code = code_va ? GStr(code_va) : std::string("?");
  WarnF("gtaiv: game error %s (%d, %d) from %08X: %s; skipped (GTAIV_ERRORS_TO_LOG)\n",
        code.c_str(), a, b, Rd32(c.esp), ErrorText(code).c_str());
  c.esp += 4;  // cdecl: the return address only
}

// rage::phSleep: an object goes to sleep once its motion stays below
// tolerances at +4 and +8 (0050B750 compares them) for a number of
// frames. Every phSleep set up or reset takes them from two defaults,
// 0.005 and 0.01 (01090B18, 01090B1C, read only by the five places
// below, each the first of the pair). The game itself gives boats in one
// state (00BA8490) 0.0001 for both; with GTAIV_NO_PHYSICS_SLEEP the
// defaults become that, written before the first of them is read.
namespace {

constexpr uint32_t kSleepVelTolerance = 0x01090B18, kSleepAngVelTolerance = 0x01090B1C;

void LowSleepTolerances() {
  static const bool done = [] {
    if (OptionOn("GTAIV_NO_PHYSICS_SLEEP", false)) {
      WrF32(kSleepVelTolerance, 0.0001f);
      WrF32(kSleepAngVelTolerance, 0.0001f);
    }
    return true;
  }();
  (void)done;
}

// movss xmm0, [01090B18]
void LoadSleepVelTolerance(Ctx& c) {
  LowSleepTolerances();
  LoadSs(c.xmm[0], RdF32(kSleepVelTolerance));
}

}  // namespace

void hook_004F0D5E(Ctx& c) { LoadSleepVelTolerance(c); }  // a pool of phSleeps set up
void hook_004F10E6(Ctx& c) { LoadSleepVelTolerance(c); }  // phSleep constructor
void hook_0050B2EF(Ctx& c) { LoadSleepVelTolerance(c); }  // phSleep constructor (with a collider)
void hook_00BA84A2(Ctx& c) { LoadSleepVelTolerance(c); }  // CBoat: back to the defaults

// phSleep reset (0050B350): xmm0 was loaded from 01090B18 at its first
// instruction; here it is stored (movss [ecx+4], xmm0), the value then
// read again in case this was the first read.
void hook_0050B35E(Ctx& c) {
  LowSleepTolerances();
  SetLane0(c.xmm[0], RdF32(kSleepVelTolerance));
  WrF32(c.ecx + 4, Lane0(c.xmm[0]));
}

}  // namespace recomp::gen
