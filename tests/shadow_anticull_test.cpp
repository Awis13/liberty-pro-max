// What the sun's shadow cascades keep (src/shadow_anticull.h): a sphere
// within 30 m of the game's camera whatever its size, within 80 m over 26 m
// of radius; not past those, nor with a NaN; the answer 1 with the out
// pointer, 2 without; plane 0's value as the game computes it.

#include <cstdio>
#include <limits>

#include "shadow_anticull.h"

namespace {
namespace sa = shadow_anticull;
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok" : "FAIL", what);
  if (!ok) failures++;
}
}  // namespace

int main() {
  const float cam[3] = {-402.5f, 1490.0f, 21.0f};
  auto at = [&](float dx, float dy, float dz) {
    static float p[3];
    p[0] = cam[0] + dx, p[1] = cam[1] + dy, p[2] = cam[2] + dz;
    return p;
  };
  Check(sa::Kept(cam, at(0, 0, -2), 0.5f) && sa::Kept(cam, at(29.0f, 0, 0), 0.2f) && sa::Kept(cam, at(0, -20, -10), 1),
        "within 30 m: kept whatever the size (the player, a lamp post)");
  Check(!sa::Kept(cam, at(31.0f, 0, 0), 5.0f) && !sa::Kept(cam, at(0, 25.0f, 20.0f), 5.0f),
        "past 30 m and small: left to the game");
  Check(sa::Kept(cam, at(0, 79.0f, 0), 27.0f) && !sa::Kept(cam, at(0, 79.0f, 0), 25.0f) &&
            !sa::Kept(cam, at(0, 81.0f, 0), 27.0f),
        "within 80 m over 26 m of radius kept, smaller or farther not");
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float bad[3] = {nan, cam[1], cam[2]};
  Check(!sa::Kept(cam, bad, 1.0f) && !sa::Kept(cam, at(1, 1, 1), nan), "a NaN centre or radius: not kept");
  Check(sa::KeptAnswer(true) == 1 && sa::KeptAnswer(false) == 2, "the answer: 1 with the out pointer, 2 without");
  const float plane[4] = {0.0f, 1.0f, 0.0f, -1500.0f};
  const float p[3] = {-400.0f, 1495.0f, 18.0f};
  Check(sa::PlaneZero(plane, p, 2.0f) == -3.0f, "plane 0's value: a x + b y + c z + d + r");
  if (failures) return 1;
  std::puts("shadow anticull: ok");
  return 0;
}
