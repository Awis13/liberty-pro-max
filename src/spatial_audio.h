// Spatial Audio (GTAIV_SPATIAL_AUDIO, on by default on the Mac): the
// speaker setting the game mixes by. Pure, for spatial_audio.cpp and its
// test.
//
// With the option on, x86recomp reports 5.1 speakers to the game where the
// Mac renders it (DirectSound's GetSpeakerConfig) and the game opens its
// output in six channels (its channel count, 0x00F2D144, is 6). What it
// puts in them follows the audio menu's speaker setting (0x010FC1E4, saved
// in the profile as setting 0x131), as the selector at 0x007BC2B0 reads it
// (at 0x007BC38E, the six-channel case): 0 headphones and 1 stereo make the
// mixing mode 1, which pans everything to the front pair and leaves
// centre, LFE and the back pair silent; 2 and 3 are the two surround
// placements (mode 3). A profile from stereo play holds 0 or 1, so with
// the option on the selector is given 2 for those, the game's own default
// for six channels (0x00481613): its panner then puts a source at the side
// into that side's back channel, where the Mac's 5.1 bed has its surrounds
// (110 degrees). 3, the other placement (speakers on the diagonals), is
// given as chosen. The stored setting is not touched: the menu shows and
// the profile keeps the player's own choice, which is back in effect with
// the option off.
#pragma once

namespace spatial_audio {

constexpr int kSurroundSetting = 2;

// The speaker setting the selector is given, from the one stored, the
// option and the game's output channels.
inline int SpeakerSetting(int stored, bool option_on, int game_channels) {
  if (!option_on || game_channels != 6) return stored;
  return stored == 2 || stored == 3 ? stored : kSurroundSetting;
}

}  // namespace spatial_audio
