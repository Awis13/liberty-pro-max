// Tree shadows, an option (the launcher's Advanced tab sets it):
//   GTAIV_TREE_SHADOWS: trees and bushes cast real moving shadows. Which
//     models are trees is decided by the game's own map data: every model the IDE files mark
//     as an "instance" model (the vegetation of props/vegetation/ext_veg.ide).
//     Drawn as ordinary map models they lose the wind sway the instanced
//     drawing gives them; the extended tree shaders sway them again (VS
//     c233, ff_shaders.cpp), so it does anything only with them
//     (GTAIV_FF_SHADERS).

#include <cstdint>

#include "hook_util.h"
#include "option.h"
#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

bool FfShadersOn();  // update_folder.cpp

using namespace hook;

// Tree shadows. The IDE "objs" reader (00AC6700) makes a model an
// instance model when its flags have bit 3 or its fifth column is 31 (all
// the trees and bushes of ext_veg.ide, nothing else in the game's data):
// it is taken from the instance model store (008DD4C0, cdecl: name) instead
// of the base model store (008DD450, same arguments), and bit 3 is set in
// its flags. Instance models do not cast the moving shadows the other map
// models cast. Here every such model is taken from the base model store
// (28000 entries, about 25000 used): drawn and shadowed like any other map
// model. Bit 3 stays set as the game sets it (the flag setter 00AC5060 does
// not look at it, nor does 00AC5190 after it). The 9 of them
// whose flags have bit 13 (no shadow) still cast none. The vanilla tree
// shaders take the sway from the instanced drawing (colorize.w), so only
// with the extended shaders, which take it from c233. Read once at load:
// the choice holds until restart. The base store does not check its size
// ([00F3E8B8] its capacity, [00F3E8BC] the count): a model moved there
// only while 2000 entries stay free for the rest of the map data, so a
// mod pack with more vegetation gets instance models (no moving shadows)
// rather than an overrun.
void hook_00AC68D2(Ctx& c) {
  static const bool on = OptionOn("GTAIV_TREE_SHADOWS", true) && FfShadersOn();
  constexpr uint32_t kCapacity = 0x00F3E8B8, kCount = 0x00F3E8BC, kKeepFree = 2000;
  const uint32_t capacity = Rd32(kCapacity), count = Rd32(kCount);
  const bool room = capacity > count && capacity - count > kKeepFree;
  c.eax = uint32_t(CallGuest(c, on && room ? 0x008DD450 : 0x008DD4C0, {}));  // name already pushed
}

}  // namespace recomp::gen
