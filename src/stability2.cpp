// Stability fixes from ZolikaPatch's list, done again for 1.0.8.0 from its
// ini's descriptions only (its code not looked at). Each one is an option
// (the launcher's Advanced tab sets it), on unless said otherwise:
//   GTAIV_AIM_PROJECTILE_FIX: aiming a thrown weapon does not crash when
//     the aiming ped has no player pad (the player's aiming task run by a
//     ped that is not the local player).

#include <cstdint>

#include "hook_util.h"
#include "option.h"
#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

using namespace hook;

// CTaskSimplePlayerAimProjectile's process (00AAE0A0, vtable 00DB885C,
// slot 17), state 0: with the menu setting at [010FC0E4] on it tests a
// byte of the ped's pad (mov ecx, [esp+1C]; cmp [ecx+328D], al; je 00AAE1B6,
// al = 0 here). The pad was taken at 00AAE0E9 from 00996F80, which gives 0
// for a ped that is not the player ([ped+219] clear) or has [ped+218] set,
// and here it is read unchecked. The game's own twin of this test, where
// the task is created (00A63820..00A63836), checks the pad and treats a
// missing pad like a zero byte; do the same (je taken), so a ped with no
// pad also never reads the local player's input at 00AAE17A.
void hook_00AAE16E(Ctx& c) {
  static const bool on = OptionOn("GTAIV_AIM_PROJECTILE_FIX", true);
  c.ecx = Rd32(c.esp + 0x1C);
  if (on && c.ecx == 0) {
    CmpFlags<1>(c, 0, 0);  // as if the byte were 0: je 00AAE1B6 taken
    return;
  }
  CmpFlags<1>(c, Rd8(c.ecx + 0x328D), c.eax);
}

}  // namespace recomp::gen
