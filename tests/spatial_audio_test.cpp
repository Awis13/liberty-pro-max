// spatial_audio.h: the speaker setting the game mixes by with Spatial Audio
// on (the stored one is not touched).

#include "spatial_audio.h"

#include <cstdio>

namespace {

int g_failures = 0;

void Expect(bool ok, const char* what, int line) {
  if (ok) return;
  std::fprintf(stderr, "spatial_audio_test.cpp:%d: FAILED: %s\n", line, what);
  g_failures++;
}
#define EXPECT(x) Expect((x), #x, __LINE__)

}  // namespace

int main() {
  using spatial_audio::SpeakerSetting;
  // On, six channels: headphones and stereo (front pair only) are given the
  // surround placement; the two surround placements as they are.
  EXPECT(SpeakerSetting(0, true, 6) == 2);
  EXPECT(SpeakerSetting(1, true, 6) == 2);
  EXPECT(SpeakerSetting(2, true, 6) == 2);
  EXPECT(SpeakerSetting(3, true, 6) == 3);
  // The game's output in stereo (the host has no spatial output): as stored.
  for (int s = 0; s < 4; s++) EXPECT(SpeakerSetting(s, true, 2) == s);
  // Off: as stored.
  for (int s = 0; s < 4; s++) EXPECT(SpeakerSetting(s, false, 6) == s);
  if (g_failures) return 1;
  std::printf("ok\n");
  return 0;
}
