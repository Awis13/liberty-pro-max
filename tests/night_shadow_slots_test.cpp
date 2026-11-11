// The night shadows' slots held steady (src/night_shadow_slots.h), on a
// model of 008C4550's ranking: each frame the lights sorted by the distance
// they compete with, the nearest `slots` given a slot, those the next
// frame's held ones. A camera turning in place keeps the same lights (the
// distance from the camera, not from a point ahead of it); two lamps at
// about the same distance do not trade the slot back and forth as the
// player sways between them; walking on to a clearly nearer lamp hands the
// slot over; a light id 0 is never held.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "night_shadow_slots.h"

namespace {

namespace ns = night_shadow_slots;
int failures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok" : "FAIL", what);
  if (!ok) failures++;
}

struct Light {
  uint32_t id;
  float x, y;
};

// One frame's choice: the `slots` lights nearest to (px, py) as they
// compete (held ones at kHeldShare when `steady`), from `ahead` metres in
// front of the camera (heading h, degrees) as the game did when not steady.
std::vector<uint32_t> Rank(const std::vector<Light>& lights, float px, float py, float h, bool steady,
                           const uint32_t (&held)[ns::kSlots], int slots) {
  const float ahead = steady ? 0.0f : 8.0f;
  const float rx = px + std::sin(h * 3.14159265f / 180.0f) * ahead, ry = py + std::cos(h * 3.14159265f / 180.0f) * ahead;
  std::vector<std::pair<float, uint32_t>> r;
  for (const Light& l : lights) {
    const float d = std::hypot(l.x - rx, l.y - ry);
    r.push_back({steady ? ns::RankDistance(d, ns::Held(l.id, held)) : d, l.id});
  }
  std::sort(r.begin(), r.end());
  std::vector<uint32_t> out;
  for (int k = 0; k < slots && k < int(r.size()); k++) out.push_back(r[size_t(k)].second);
  std::sort(out.begin(), out.end());
  return out;
}

void Hold(const std::vector<uint32_t>& chosen, uint32_t (&held)[ns::kSlots]) {
  for (int k = 0; k < ns::kSlots; k++) held[k] = k < int(chosen.size()) ? chosen[size_t(k)] : 0;
}

// How many frames' choices differ from the frame before's.
int Churn(const std::vector<Light>& lights, bool steady, int slots, float (*px)(int), float (*py)(int), float (*h)(int),
          int frames) {
  uint32_t held[ns::kSlots] = {};
  std::vector<uint32_t> last;
  int changes = 0;
  for (int f = 0; f < frames; f++) {
    const std::vector<uint32_t> now = Rank(lights, px(f), py(f), h(f), steady, held, slots);
    if (f > 0 && now != last) changes++;
    Hold(now, held);
    last = now;
  }
  return changes;
}

}  // namespace

int main() {
  // A street: lamps every 12 m on both sides, the player in the middle.
  std::vector<Light> street;
  for (int k = -6; k <= 6; k++) {
    street.push_back({uint32_t(100 + k), float(k) * 12.0f, 5.0f});
    street.push_back({uint32_t(200 + k), float(k) * 12.0f + 6.0f, -5.0f});
  }
  // The camera turning round the standing player, 3 degrees a frame.
  const int turn_game = Churn(street, false, 7, [](int) { return 0.0f; }, [](int) { return 0.0f; },
                              [](int f) { return float(f) * 3.0f; }, 240);
  const int turn_steady = Churn(street, true, 7, [](int) { return 0.0f; }, [](int) { return 0.0f; },
                                [](int f) { return float(f) * 3.0f; }, 240);
  char what[200];
  std::snprintf(what, sizeof what,
                "a camera turning in place: the slots change in %d frames of 240 as the game ranks, %d steady",
                turn_game, turn_steady);
  Check(turn_game > 0 && turn_steady == 0, what);
  // The player swaying between two lamps at about the same distance.
  const std::vector<Light> two = {{1, -6.0f, 0.0f}, {2, 6.0f, 0.0f}};
  const int sway = Churn(two, true, 1, [](int f) { return std::sin(float(f) * 0.3f) * 1.5f; },
                         [](int) { return 0.0f; }, [](int) { return 0.0f; }, 200);
  std::snprintf(what, sizeof what, "two lamps 12 m apart, the player swaying 1.5 m between them: %d changes", sway);
  Check(sway == 0, what);
  // Walking on from one lamp to the next: the slot moves, once.
  const int walk = Churn(two, true, 1, [](int f) { return -8.0f + float(f) * 0.08f; }, [](int) { return 0.0f; },
                         [](int) { return 0.0f; }, 200);
  std::snprintf(what, sizeof what, "walking from one lamp to the other: %d changes", walk);
  Check(walk == 1, what);
  const uint32_t none[ns::kSlots] = {};
  const uint32_t some[ns::kSlots] = {5, 0, 0, 0, 0, 0, 0, 0};
  Check(!ns::Held(0, none) && ns::Held(5, some) && !ns::Held(6, some), "held: only a nonzero id in the table");
  Check(ns::RankDistance(10.0f, true) == 6.0f && ns::RankDistance(10.0f, false) == 10.0f,
        "a held light competes at 0.6 of its distance");
  // The priorities, the game's (even false) and even.
  const uint32_t car = 0x164, lamp = 0x67, forced = 0x467;
  Check(ns::Priority(car, 0, false) == 0 && ns::Priority(lamp, 0, true) == 0, "an empty place is taken");
  Check(ns::Priority(car, lamp, false) == 0 && ns::Priority(lamp, car, false) == 2 &&
            ns::Priority(car, car, false) == 1 && ns::Priority(lamp, lamp, false) == 1,
        "the game's: a car's light before a lamp whatever the distance");
  Check(ns::Priority(car, lamp, true) == 1 && ns::Priority(lamp, car, true) == 1, "even: the distances decide");
  Check(ns::Priority(forced, car, true) == 0 && ns::Priority(car, forced, true) == 2 &&
            ns::Priority(lamp, forced, false) == 2 && ns::Priority(forced, lamp, false) == 0,
        "a 0x400 light first either way");
  // 008C4550's ranking of eight places with 008C32B0's order: eight cars'
  // lights 12-19 m away fill them and the lamp 3 m away is left out; even,
  // the lamp is in.
  for (int even = 0; even < 2; even++) {
    struct L {
      uint32_t flags;
      float d;
    };
    std::vector<L> ls = {{lamp, 3.0f}};
    for (int k = 0; k < 8; k++) ls.push_back({car, 12.0f + float(k)});
    uint32_t place_flags[8] = {};
    float place_d[8] = {};
    for (const L& l : ls) {  // 008C499A's insertion, the place 0 first
      uint32_t f = l.flags;
      float d = l.d;
      for (int k = 0; k < 8; k++) {
        const uint32_t p = ns::Priority(f, place_flags[k], even != 0);
        if (p == 0 || (p == 1 && place_d[k] > d)) {
          std::swap(f, place_flags[k]);
          std::swap(d, place_d[k]);
        }
      }
    }
    const bool lamp_in = std::find(place_flags, place_flags + 8, lamp) != place_flags + 8;
    std::snprintf(what, sizeof what, "a lamp 3 m away among eight cars' lights 12-19 m away: %s (%s)",
                  lamp_in ? "has a slot" : "no slot", even ? "even" : "the game's");
    Check(lamp_in == (even != 0), what);
  }
  if (failures) return 1;
  std::puts("night shadow slots: ok");
  return 0;
}
