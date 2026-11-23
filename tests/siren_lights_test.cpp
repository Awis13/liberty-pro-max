// A siren bulb's light (src/siren_lights.h).

#include <cmath>
#include <cstdio>

#include "siren_lights.h"

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok" : "FAIL", what);
  if (!ok) failures++;
}

bool Close(float a, float b) { return std::fabs(a - b) < 1e-5f; }

float Dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

}  // namespace

int main() {
  namespace sl = gtaiv_siren;
  sl::Tuning t;
  t.down = 0.0f;
  sl::Bulb b;
  b.pos[0] = 10;
  b.pos[1] = -20;
  b.pos[2] = 30;
  b.axis[0] = 0;
  b.axis[1] = 3;
  b.axis[2] = 0;
  b.has_axis = true;
  b.r = 128;
  b.g = 128;
  b.b = 255;
  sl::Spot s;
  Check(sl::SpotOf(b, t, &s) && Close(s.dir[0], 0) && Close(s.dir[1], 1) && Close(s.dir[2], 0) &&
            Close(Dot(s.dir, s.tangent), 0) && Close(Dot(s.tangent, s.tangent), 1) && s.pos[0] == 10 &&
            s.pos[1] == -20 && s.pos[2] == 30 && s.colour[0] < 30 && s.colour[1] < 30 && s.colour[2] == 255 &&
            s.intensity == t.intensity * t.blue_scale && s.radius == t.radius && s.inner == t.inner && s.outer == t.outer,
        "a turning bulb: a spot at the bulb along its axis, unit direction and tangent, the corona's colour");
  b.axis[0] = 0;
  b.axis[1] = 0;
  b.axis[2] = -2;
  Check(sl::SpotOf(b, t, &s) && Close(s.dir[2], -1) && Close(Dot(s.dir, s.tangent), 0) &&
            Close(Dot(s.tangent, s.tangent), 1),
        "an axis straight down: still a unit tangent across it");
  b.axis[2] = 0;
  Check(!sl::SpotOf(b, t, &s), "an axis of no length: no light");
  b.axis[2] = NAN;
  Check(!sl::SpotOf(b, t, &s), "an axis that is not a number: no light");
  b.axis[2] = 1;
  b.has_axis = false;
  Check(!sl::SpotOf(b, t, &s), "a bulb that only flashes: no light of its own");
  b.has_axis = true;
  b.r = b.g = b.b = 0;
  Check(!sl::SpotOf(b, t, &s), "a black corona: no light");
  // Tipped toward the ground, still of length 1.
  b.r = 255;
  b.axis[0] = 1;
  b.axis[1] = 0;
  b.axis[2] = 0;
  t.down = 0.2f;
  Check(sl::SpotOf(b, t, &s) && s.dir[2] < -0.15f && s.dir[0] > 0.9f && Close(Dot(s.dir, s.dir), 1) &&
            Close(Dot(s.dir, s.tangent), 0) && s.colour[0] == 255 && s.colour[1] == 0 && s.colour[2] == 0,
        "the beam tipped toward the ground; pure red stays red");
  Check(s.intensity == t.intensity, "a red beam at the full intensity, a blue one at blue_scale of it");
  // One bulb a colour: a police car's seven (red, blue alternating).
  sl::Bulb bar[7];
  for (int i = 0; i < 7; i++) {
    bar[i] = b;
    bar[i].r = i % 2 ? 128 : 255;
    bar[i].g = i % 2 ? 128 : 0;
    bar[i].b = i % 2 ? 255 : 0;
  }
  bool lit[7];
  sl::Pick(bar, 7, lit);
  int n = 0;
  for (bool x : lit) n += x;
  Check(n == 2 && lit[0] && lit[5], "seven alternating bulbs: the first red and the last blue lit");
  bar[3].has_axis = false;
  sl::Pick(bar, 7, lit);
  n = 0;
  for (bool x : lit) n += x;
  Check(n == 2 && lit[0] && lit[5] && !lit[3], "a bulb without an axis is not picked");
  const float cam[3] = {0, 0, 0}, near_car[3] = {30, 20, 2}, far_car[3] = {30, 30, 2}, nowhere[3] = {NAN, 0, 0};
  Check(sl::WithinReach(cam, near_car) && !sl::WithinReach(cam, far_car) && !sl::WithinReach(cam, nowhere),
        "beams within kBeamReach of the camera only");
  std::puts(failures ? "siren_lights: FAILED" : "siren_lights: ok");
  return failures ? 1 : 0;
}
