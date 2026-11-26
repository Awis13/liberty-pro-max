// What the game culls while shadows are ray traced (src/rt_anticull.h): a
// sphere whose centre lies within 20 m of a perspective viewport's camera
// is kept (just inside and just outside the distance, every axis, a NaN
// centre not kept), an orthographic viewport's never; the answer 1 with
// the out pointer, 2 without; plane 0's value as the game computes it.

#include <cmath>
#include <cstdio>
#include <limits>

#include "rt_anticull.h"

namespace {

namespace ac = gtaiv_anticull;
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
  Check(ac::KeptNear(cam, true, at(0, 0, 0)), "near: the camera's own place kept");
  Check(ac::KeptNear(cam, true, at(19.9f, 0, 0)) && ac::KeptNear(cam, true, at(0, -19.9f, 0)) &&
            ac::KeptNear(cam, true, at(0, 0, 19.9f)),
        "near: 19.9 m on each axis kept (behind the camera too)");
  Check(!ac::KeptNear(cam, true, at(20.1f, 0, 0)) && !ac::KeptNear(cam, true, at(12.0f, 12.0f, 12.0f)),
        "near: past 20 m (on an axis, on a diagonal) left to the game");
  Check(!ac::KeptNear(cam, false, at(1, 1, 1)), "near: an orthographic viewport (a cascade) left to the game");
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float bad[3] = {nan, cam[1], cam[2]};
  Check(!ac::KeptNear(cam, true, bad), "near: a NaN centre not kept");
  Check(ac::KeptAnswer(true) == 1 && ac::KeptAnswer(false) == 2, "the answer: 1 with the out pointer, 2 without");
  const float plane[4] = {0.0f, 1.0f, 0.0f, -1500.0f};
  const float p[3] = {-400.0f, 1495.0f, 18.0f};
  Check(ac::PlaneZero(plane, p, 2.0f) == -3.0f, "plane 0's value: a x + b y + c z + d + r");
  // Buildings and objects: 30 m always, 80 m over 26 m of radius, 500 m
  // over 50.
  Check(ac::KeptStatic(cam, at(0, 29.0f, 0), 1.0f) && !ac::KeptStatic(cam, at(0, 31.0f, 0), 1.0f),
        "static: kept within 30 m whatever its size, not past it when small");
  Check(ac::KeptStatic(cam, at(0, 79.0f, 0), 27.0f) && !ac::KeptStatic(cam, at(0, 79.0f, 0), 25.0f) &&
            !ac::KeptStatic(cam, at(0, 81.0f, 0), 27.0f),
        "static: within 80 m over 26 m of radius kept, smaller or farther not");
  Check(ac::KeptStatic(cam, at(-499.0f, 0, 0), 51.0f) && !ac::KeptStatic(cam, at(-499.0f, 0, 0), 49.0f) &&
            !ac::KeptStatic(cam, at(-501.0f, 0, 0), 51.0f),
        "static: within 500 m over 50 m of radius kept, smaller or farther not");
  Check(!ac::KeptStatic(cam, bad, 100.0f), "static: a NaN centre not kept");
  // Map sections: a square round the footprint's first point, wound as it.
  {
    const float ccw[10] = {0, 0, 10, -5, 12, 0, 10, 5, 0, 0.5f};  // (area > 0)
    float sq[8];
    float area = 0;
    auto signed_area = [](const float* p, int n) {  // (twice the area)
      float a = 0;
      for (int i = 0; i < n; i++) {
        const int j = (i + 1) % n;
        a += p[2 * i] * p[2 * j + 1] - p[2 * j] * p[2 * i + 1];
      }
      return a;
    };
    const bool made = ac::MarkSquare(ccw, 5, 2.0f, sq);
    area = signed_area(sq, 4);
    float cw[10];
    for (int k = 0; k < 5; k++)
      cw[2 * k] = ccw[2 * (k == 0 ? 0 : 5 - k)], cw[2 * k + 1] = ccw[2 * (k == 0 ? 0 : 5 - k) + 1];
    float sq2[8];
    const bool made2 = ac::MarkSquare(cw, 5, 2.0f, sq2);
    bool corners = true;
    for (int k = 0; k < 4; k++) corners = corners && std::fabs(sq[2 * k]) == 2.0f && std::fabs(sq[2 * k + 1]) == 2.0f;
    Check(made && made2 && corners && area == 32.0f && signed_area(sq2, 4) == -32.0f,
          "sections: a 4 x 4 square round the camera's point, wound as the footprint either way");
    float none[8];
    Check(!ac::MarkSquare(ccw, 4, 2.0f, none) && !ac::MarkSquare(ccw, 5, 0.0f, none),
          "sections: not for a footprint of 4 points (not a perspective view) or no size");
  }
  {
    // A view from the origin along +y (near 1 m, far 200 m, 90 degrees
    // across, half a radian-ish up and down), its planes facing in.
    const float n2 = std::sqrt(2.0f), nz = std::sqrt(1.25f);
    const float planes[24] = {0, 1, 0, -1,           0, -1, 0, 200,          1 / n2, 1 / n2, 0, 0,
                              -1 / n2, 1 / n2, 0, 0,  0, 0.5f / nz, 1 / nz, 0, 0, 0.5f / nz, -1 / nz, 0};
    const float overhead[3] = {0, 20, 30}, behind[3] = {0, -50, 0};
    const float straight_down[3] = {0, 0, -1}, toward_view[3] = {0, 1, 0}, away[3] = {0, -1, 0};
    Check(ac::KeptShadow(planes, overhead, 1.0f, straight_down),
          "shadows: a lattice above the top of the picture, its shadow falling into it, kept");
    Check(ac::KeptShadow(planes, behind, 1.0f, toward_view),
          "shadows: something 50 m behind the camera, the sun behind it, its shadow reaching forward: kept");
    Check(!ac::KeptShadow(planes, behind, 1.0f, away),
          "shadows: the same, the sun ahead, its shadow going backwards: not kept");
    const float far_behind[3] = {0, -200, 0};
    Check(!ac::KeptShadow(planes, far_behind, 1.0f, toward_view),
          "shadows: 200 m behind, beyond the reach of a shadow: not kept");
    const float inside[3] = {0, 50, 0};
    Check(ac::KeptShadow(planes, inside, 1.0f, away), "shadows: what is inside the view stays inside");
  }
  {
    const float cam[3] = {0, 0, 0}, at300[3] = {300, 0, 0}, at600[3] = {600, 0, 0};
    Check(ac::KeptStatic(cam, at300, 5.0f, 20.0f) && !ac::KeptStatic(cam, at300, 5.0f, 10.0f) &&
              !ac::KeptStatic(cam, at300, 5.0f) && !ac::KeptStatic(cam, at600, 5.0f, 40.0f),
          "static: within 500 m, kept when over 13 m high (gta4-rtx's far height), not lower, not unknown, not past 500 m");
  }
  if (failures) return 1;
  std::puts("anticull: ok");
  return 0;
}
