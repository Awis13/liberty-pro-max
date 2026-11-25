// Spatial Audio (GTAIV_SPATIAL_AUDIO, on by default on the Mac): the game's
// 5.1 through the Mac's spatial renderer (x86recomp, <ID>_SPATIAL_AUDIO;
// main.cpp gives the runtime the port's reading of the option). Where the
// audio menu's speaker setting picks the mixing mode, the game is given a
// surround placement for a stored headphones or stereo while its output
// is six channels (spatial_audio.h says why and which); the stored setting
// stays as the player chose it.

#include "spatial_audio.h"

#include <cstdint>

#include "option.h"
#include "runtime/lifted.h"
#include "runtime/log.h"
#include "runtime/runtime.h"

namespace recomp::gen {

namespace {

constexpr uint32_t kGameChannels = 0x00F2D144;   // 2 or 6, set as its output opens
constexpr uint32_t kSpeakerSetting = 0x010FC1E4;  // the audio menu's speaker setting

}  // namespace

// The selector's mov eax, [0x010FC1E4] in its six-channel case.
void hook_007BC38E(Ctx& c) {
  static const bool on = OptionOn("GTAIV_SPATIAL_AUDIO", true);
  const int stored = int(Rd32(kSpeakerSetting));
  const int given = spatial_audio::SpeakerSetting(stored, on, int(Rd32(kGameChannels)));
  c.eax = uint32_t(given);
  static int logged = -1;
  if (given != stored && logged != stored) {
    logged = stored;
    WarnF("spatial audio: the game's speaker setting %d (not surround) mixed as %d\n", stored, given);
  }
}

}  // namespace recomp::gen
