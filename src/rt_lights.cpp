// Ray tracing: the frame's light list handed to the renderer.
//
// Once a frame the render thread's command 00A2E9D0 makes the frame's light
// list current ([0x013A4194] count, [0x00F49528] array: rt_lights.h has
// the list and its records); right after it, with ray-traced shadows on
// (GTAIV_RT=shadows or reflections, on a GPU that traces rays: gfx::
// RayTracedShadows, as rt_cascades.cpp asks), every record that is a light
// (not the capsules under cars, nothing dark or broken) goes to x86recomp
// as a gfx::RtLight (gfx::SetRtLights), for that frame's work to read.
// Nothing changes in the game: no light added or altered, nothing drawn.
// The thread is the one making the frame's Direct3D calls, as SetRtLights
// asks. The first list is logged once; the renderer's perf line has the
// counts a frame (GTAIV_PERF: lists, lights given, kept, by kind).

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "rt_lights.h"
#include "runtime/gfx/device.h"
#include "runtime/gfx/rt_light_list.h"
#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

namespace rl = gtaiv_rt_lights;

void f_00A2E9D0_lifted(Ctx& c);

namespace {

constexpr uint32_t kCount = 0x013A4194;  // the current list's count
constexpr uint32_t kArray = 0x00F49528;  // the current list's records

// The first list with lights, once: what it held and what was left out.
void LogOnce(const rl::ListCounts& n) {
  static std::atomic<bool> logged{false};
  if (n.kept == 0 || logged.exchange(true)) return;
  std::fprintf(stderr,
               "rt lights: the frame's light list goes to the renderer: %u records, %u lights (%u point, %u spot); "
               "left out %u capsules, %u of other types, %u dark, %u invalid\n",
               n.records, n.kept, n.kept - n.spots, n.spots, n.skipped[int(rl::Skip::Capsule)],
               n.skipped[int(rl::Skip::OtherType)], n.skipped[int(rl::Skip::Dark)], n.skipped[int(rl::Skip::Invalid)]);
}

// A gunshot's light (00AAC480, once a shot): a point light of colour
// (1, 0.6, 0.2) times 12 (18 for rifles), radius 4 m (5 m for rifles), in
// the list for one frame. Told by that colour and radius.
bool MuzzleLight(const recomp::gfx::RtLight& l) {
  if (l.kind != recomp::gfx::RtLightKind::Point || l.key != 0 || !(l.color[0] > 0)) return false;
  const float g = l.color[1] / l.color[0], b = l.color[2] / l.color[0];
  return std::fabs(g - 0.6f) < 0.02f && std::fabs(b - 0.2f) < 0.02f &&
         (std::fabs(l.radius - 4.0f) < 0.01f || std::fabs(l.radius - 5.0f) < 0.01f);
}

// Gunshots light the place a while (not one frame, whatever the frame
// rate) and further than the game's: GTAIV_MUZZLE_LIGHT_SECONDS how long
// each shot's light stays (0: the game's one frame, nothing changed),
// GTAIV_MUZZLE_LIGHT_RADIUS its radius times that. Only the renderer's
// lights see it (the walls, the ground); what the game draws itself
// (people, cars) has the shot's own frame as before.
struct Muzzle {
  float seconds = 0.05f, radius = 3.0f;
  struct Held {
    recomp::gfx::RtLight light;
    double at = -1;
  };
  static constexpr uint32_t kHeld = 32;
  Held held[kHeld];
  uint32_t next = 0;
  Muzzle() {
    if (const char* e = std::getenv("GTAIV_MUZZLE_LIGHT_SECONDS"); e && *e) seconds = float(std::atof(e));
    if (const char* e = std::getenv("GTAIV_MUZZLE_LIGHT_RADIUS"); e && *e) radius = float(std::atof(e));
    if (!(radius > 0)) radius = 1;
  }
  static double Now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  // The frame's lights[0, n) (room for `cap`): its gunshots widened and
  // kept, those of the frames before still within their time added;
  // returns the new count.
  uint32_t Apply(recomp::gfx::RtLight* lights, uint32_t n, uint32_t cap) {
    if (!(seconds > 0)) return n;
    const double now = Now();
    for (uint32_t i = 0; i < n; i++) {
      if (!MuzzleLight(lights[i])) continue;
      lights[i].radius *= radius;
      held[next] = {lights[i], now};
      next = (next + 1) % kHeld;
    }
    for (const Held& h : held)
      if (h.at >= 0 && h.at < now && now - h.at <= seconds && n < cap) lights[n++] = h.light;
    return n;
  }
};

// The cars of the trains near the player (the vehicle pool's, type 3 at
// +1354, as metro_map.cpp finds them): their places for SetRtPlainPlaces,
// a car's middle, 13 m across and 4 m up and down, so that its body's,
// glass's and seats' forward draws trace no reflection and no lamp ray.
// A platform's frame with a train in view took some 3 ms more of forward
// pass for those rays (2026-11-14).
constexpr uint32_t kVehiclePool = 0x01252CD0;
float ReadF32(uint32_t va) {
  const uint32_t b = Rd32(va);
  float v;
  std::memcpy(&v, &b, 4);
  return v;
}
void TrainPlaces() {
  static std::vector<float> places;
  places.clear();
  const uint32_t pool = Rd32(kVehiclePool);
  if (pool) {
    const uint32_t cars = Rd32(pool), flags = Rd32(pool + 4), count = Rd32(pool + 8), stride = Rd32(pool + 0xC);
    for (uint32_t i = 0; i < count && i < 4096 && cars && flags; i++) {
      if (Rd8(flags + i) & 0x80) continue;
      const uint32_t car = cars + i * stride;
      if (Rd32(car + 0x1354) != 3) continue;
      const uint32_t matrix = Rd32(car + 0x20);
      const uint32_t at = matrix ? matrix + 0x30 : car + 0x10;
      places.insert(places.end(), {ReadF32(at), ReadF32(at + 4), ReadF32(at + 8), 13.0f, 4.0f});
    }
  }
  recomp::gfx::SetRtPlainPlaces(places.data(), places.size() / 5);
}

}  // namespace

// CSetRenderLightsDC::Execute (thiscall, no arguments): the game's, then
// the list it made current to the renderer.
void f_00A2E9D0(Ctx& c) {
  f_00A2E9D0_lifted(c);
  if (!recomp::gfx::RayTracedShadows()) return;
  // (Only this command, on the render thread, fills it.)
  static recomp::gfx::RtLight lights[rl::kMaxRecords + Muzzle::kHeld];
  static Muzzle muzzle;
  const uint32_t n = rl::ClampCount(int32_t(Rd32(kCount)));
  const uint32_t array = Rd32(kArray);
  // (The records lie in guest memory one after another: kRecordStride
  // apart from the array's host address.)
  rl::ListCounts counts;
  uint32_t kept = rl::ConvertList(array ? GPtr(array) : nullptr, n, lights, &counts);
  kept = muzzle.Apply(lights, kept, rl::kMaxRecords + Muzzle::kHeld);
  recomp::gfx::SetRtLights(lights, kept);
  TrainPlaces();
  LogOnce(counts);
}

}  // namespace recomp::gen
