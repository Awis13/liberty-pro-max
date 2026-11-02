// Distant vehicle impostors, by the idea of ZolikaPatch (its ini's
// description only). An option (the launcher's Advanced tab sets it), on
// by default as in ZolikaPatch's ini:
//   GTAIV_NO_IMPOSTOR_HEIGHT_CUTOFF: the far traffic drawn along the roads
//     of distantlights.dat (cars by day, their lights by night) uses both
//     of the file's road lists at any camera height, not the second one
//     only from 75 m up.
// The cascaded shadows' resolution (FusionFix's high resolution shadows)
// is in src/ff_shaders.cpp with the rest of FusionFix's shadow options.

#include <cstdint>

#include "hook_util.h"
#include "option.h"
#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

using namespace hook;

// The distant vehicle impostors (the "disableimposters" switch, [016E6660],
// turns the whole set off): by day 00CA03B0 (texture "imp_car"), by night
// 00CA0FA0 (texture "corona"); 00C9F880 is the static distant lights
// (GTAIV_LOD_LIGHTS, community_fixes.cpp). Each walks the road links of
// distantlights.dat in two lists ([016E0E48], [016E42E0] per cell) and
// draws the second only while (camera z - 75) * 0.025 > 0: the gate is
// that value's sign alone (comiss with 0.0, then jbe past the list).
// subss xmm0, [75.0] made to give 40, so the value is 1 at any height.
namespace {
bool NoImpostorHeightCutoff() {
  static const bool on = OptionOn("GTAIV_NO_IMPOSTOR_HEIGHT_CUTOFF", true);
  return on;
}
void ImpostorHeight(Ctx& c) {
  if (NoImpostorHeightCutoff()) SetLane0(c.xmm[0], 40.0f);
  else SetLane0(c.xmm[0], Lane0(c.xmm[0]) - RdF32(0x00D9D640));
}
}  // namespace
void hook_00CA057A(Ctx& c) { ImpostorHeight(c); }  // by day: subss xmm0, [75.0]
void hook_00CA117B(Ctx& c) { ImpostorHeight(c); }  // by night: subss xmm0, [75.0]

}  // namespace recomp::gen
