// The game's light records as the renderer gets them (src/rt_lights.h):
// records built byte by byte at the game's offsets, converted: a point; a
// spot with its cone (offset the outer cosine, scale 1 / (inner - outer),
// at least 1 / 0.01) and its direction normalised; a spot whose outer
// cosine is below 0, a point (the game's type 4); the capsule under a car
// and other types left out; radius and colour 0 (or less) left out; NaN
// and infinities left out; the flags mapped; the key from the id; the
// space from the interior and room; a colour channel below 0 left out;
// the count clamped; and a list of records (ConvertList, the hook's loop):
// the lights packed in order, those left out counted by reason, at most
// kMaxRecords taken.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

#include "rt_lights.h"

namespace {

namespace rl = gtaiv_rt_lights;
namespace gfx = recomp::gfx;
using rl::Skip;

int failures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok" : "FAIL", what);
  if (!ok) failures++;
}

bool Near(float a, float b) { return std::fabs(a - b) <= 1e-6f * (1.0f + std::fabs(b)); }

// A record, little-endian bytes at the game's offsets.
struct Record {
  uint8_t b[rl::kRecordStride] = {};
  Record& F(uint32_t off, float v) {
    uint32_t u;
    std::memcpy(&u, &v, 4);
    return U(off, u);
  }
  Record& U(uint32_t off, uint32_t v) {
    for (int i = 0; i < 4; i++) b[off + i] = uint8_t(v >> (8 * i));
    return *this;
  }
  Record& V3(uint32_t off, float x, float y, float z) { return F(off, x).F(off + 4, y).F(off + 8, z); }
};

// A point light at (100, -200, 15), radius 8, colour (1, 0.5, 0.25) times 2.
Record Point() {
  Record r;
  r.V3(0x00, 0, 0, 0).V3(0x20, 100, -200, 15).V3(0x30, 1, 0.5f, 0.25f).F(0x40, 2).U(0x44, 0).F(0x54, 8);
  r.U(0x64, 0xFFFFFFFFu);  // no shadow slot
  return r;
}

// A spot pointing down (direction not unit), inner 0.9, outer 0.7.
Record Spot() {
  Record r = Point();
  r.U(0x44, 2).V3(0x00, 0, 0, -3).V3(0x10, 1, 0, 0).F(0x58, 0.9f).F(0x5C, 0.7f);
  return r;
}

bool Converts(const Record& r, gfx::RtLight* l, Skip* why) {
  // Only the bytes the game copies.
  uint8_t copy[rl::kRecordSize];
  std::memcpy(copy, r.b, sizeof copy);
  return rl::Convert(copy, l, why);
}

void PointChecks() {
  gfx::RtLight l{};
  Skip why = Skip::Invalid;
  const bool ok = Converts(Point(), &l, &why);
  Check(ok && why == Skip::None, "point: a light");
  Check(l.kind == gfx::RtLightKind::Point, "point: kind point");
  Check(l.position[0] == 100 && l.position[1] == -200 && l.position[2] == 15, "point: position (world, metres)");
  Check(l.radius == 8, "point: radius");
  Check(l.color[0] == 2 && l.color[1] == 1 && l.color[2] == 0.5f, "point: colour times intensity");
  Check(l.direction[0] == 0 && l.direction[1] == 0 && l.direction[2] == 0 && l.cone_offset == 0 && l.cone_scale == 0,
        "point: no direction or cone");
  Check(l.flags == 0 && l.key == 0 && l.space[0] == 0 && l.space[1] == 0, "point: no flags, key or space");
  Check(gfx::RtLightValid(l), "point: valid for the renderer");
}

void SpotChecks() {
  gfx::RtLight l{};
  Skip why = Skip::Invalid;
  Check(Converts(Spot(), &l, &why) && why == Skip::None, "spot: a light");
  Check(l.kind == gfx::RtLightKind::Spot, "spot: kind spot");
  Check(l.direction[0] == 0 && l.direction[1] == 0 && l.direction[2] == -1, "spot: direction normalised");
  Check(Near(l.cone_offset, 0.7f), "spot: cone offset the outer cosine");
  Check(Near(l.cone_scale, 1.0f / (0.9f - 0.7f)), "spot: cone scale 1 / (inner - outer)");
  // At the outer edge the cone is 0, at the inner one 1.
  auto cone = [&](float c) { return std::fmin(std::fmax((c - l.cone_offset) * l.cone_scale, 0.0f), 1.0f); };
  Check(Near(cone(0.7f), 0) && Near(cone(0.9f), 1) && Near(cone(0.8f), 0.5f), "spot: cone 0 outside, 1 inside");
  Check(gfx::RtLightValid(l), "spot: valid for the renderer");

  Record narrow = Spot();
  narrow.F(0x58, 0.705f).F(0x5C, 0.7f);
  Converts(narrow, &l, &why);
  Check(Near(l.cone_scale, 100), "spot: inner and outer closer than 0.01: scale 1 / 0.01");
  narrow.F(0x58, 0.6f);
  Converts(narrow, &l, &why);
  Check(Near(l.cone_scale, 100), "spot: inner outside outer: scale 1 / 0.01");
  Check(Near(rl::ConeScale(0.5f, 0.25f), 4) && rl::ConeOffset(0.25f) == 0.25f, "cone maths");

  Record wide = Spot();
  wide.F(0x5C, -0.2f);
  Check(Converts(wide, &l, &why) && l.kind == gfx::RtLightKind::Point && l.cone_scale == 0 && l.direction[2] == 0,
        "spot with an outer cosine below 0: a point (no cone)");
  Record wide_no_dir = wide;
  wide_no_dir.V3(0x00, 0, 0, 0);
  Check(Converts(wide_no_dir, &l, &why) && l.kind == gfx::RtLightKind::Point,
        "spot with an outer cosine below 0: no direction needed");
  Record zero_outer = Spot();
  zero_outer.F(0x5C, 0.0f);
  Check(Converts(zero_outer, &l, &why) && l.kind == gfx::RtLightKind::Spot && l.cone_offset == 0,
        "spot with an outer cosine of 0: still a spot");
}

void SkipChecks() {
  gfx::RtLight l{};
  l.radius = 77;
  Skip why = Skip::None;
  Record capsule = Point();
  capsule.U(0x44, 3);
  Check(!Converts(capsule, &l, &why) && why == Skip::Capsule, "capsule (type 3, a dark patch): left out");
  Check(l.radius == 77, "left out: the output untouched");
  for (uint32_t t : {1u, 4u, 5u, 0xFFFFFFFFu}) {
    Record r = Point();
    r.U(0x44, t);
    why = Skip::None;
    Check(!Converts(r, &l, &why) && why == Skip::OtherType, "another type: left out");
  }
  Record r = Point();
  r.F(0x54, 0);
  Check(!Converts(r, &l, &why) && why == Skip::Dark, "radius 0: left out");
  r.F(0x54, -1);
  Check(!Converts(r, &l, &why) && why == Skip::Dark, "radius below 0: left out");
  r = Point();
  r.F(0x40, 0);
  Check(!Converts(r, &l, &why) && why == Skip::Dark, "intensity 0: left out");
  r.F(0x40, -1);
  Check(!Converts(r, &l, &why) && why == Skip::Dark, "intensity below 0: left out");
  r = Point();
  r.V3(0x30, 0, 0, 0);
  Check(!Converts(r, &l, &why) && why == Skip::Dark, "colour 0: left out");
  r = Point();
  r.V3(0x30, 0, 0, 0.5f);
  Check(Converts(r, &l, &why), "one channel lit: a light");
  r = Point();
  r.V3(0x30, 1, -0.25f, 1);
  Check(!Converts(r, &l, &why) && why == Skip::Dark, "a colour channel below 0 (darkening): left out");

  const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
  const uint32_t at[] = {0x20, 0x24, 0x28, 0x30, 0x34, 0x38, 0x40, 0x54};
  bool all = true;
  for (uint32_t off : at)
    for (float bad : {nan, inf, -inf}) {
      r = Point();
      r.F(off, bad);
      all = all && !Converts(r, &l, &why) && why == Skip::Invalid;
    }
  Check(all, "NaN or infinity in position, colour, intensity or radius: left out as invalid");
  all = true;
  for (uint32_t off : {0x00u, 0x04u, 0x08u, 0x58u, 0x5Cu}) {
    r = Spot();
    r.F(off, nan);
    all = all && !Converts(r, &l, &why) && why == Skip::Invalid;
  }
  Check(all, "spot: NaN in direction or cone: left out as invalid");
  r = Spot();
  r.V3(0x00, 0, 0, 0);
  Check(!Converts(r, &l, &why) && why == Skip::Invalid, "spot without a direction: left out as invalid");
  r = Spot();
  r.V3(0x00, 3e19f, 3e19f, 3e19f);
  Check(!Converts(r, &l, &why) && why == Skip::Invalid, "spot whose direction's length overflows: left out");
  r = Point();
  r.F(0x00, nan).F(0x58, nan).F(0x5C, nan);
  Check(Converts(r, &l, &why), "point: its unused direction and cone fields may hold anything");
}

void FlagChecks() {
  gfx::RtLight l{};
  Skip why;
  struct {
    uint32_t game, want;
    const char* what;
  } cases[] = {
      {0x04, gfx::kRtLightCastsShadow, "flags: 0x04 (dynamic shadow): casts a shadow"},
      {0x20, gfx::kRtLightInterior, "flags: 0x20: interior"},
      {0x40, gfx::kRtLightExterior, "flags: 0x40: exterior"},
      {0x100, gfx::kRtLightVehicleLamp, "flags: 0x100 (headlights): a vehicle's lamp"},
      {0x08 | 0x10 | 0x80 | 0x200 | 0x400, 0, "flags: others (shaft, the player's...) not carried"},
      {0x564, gfx::kRtLightCastsShadow | gfx::kRtLightInterior | gfx::kRtLightExterior | gfx::kRtLightVehicleLamp,
       "flags: together"},
  };
  // (The model and class bits aside: ModelChecks.)
  const uint32_t mask = ~(gfx::kRtLightModelMask | gfx::kRtLightClassMask);
  for (const auto& k : cases) {
    Record r = Spot();
    r.U(0x48, k.game);
    Check(Converts(r, &l, &why) && (l.flags & mask) == k.want, k.what);
  }
}

// The model and class of the technique the game draws a light with
// (00A2C680's choice by flags).
void ModelChecks() {
  gfx::RtLight l{};
  Skip why;
  struct {
    uint32_t game, model, cls;
    const char* what;
  } cases[] = {
      {0x00, 0, 0, "model: a light volume (ps6), every pixel"},
      {0x20, 0, 1, "model: 0x20 alone, interior pixels (ps7)"},
      {0x40, 0, 2, "model: 0x40 alone, exterior pixels (ps8)"},
      {0x60, 0, 0, "model: 0x20 and 0x40, every pixel (ps6)"},
      {0x80, 0, 3, "model: 0x80 without 0x100, class one (ps9)"},
      {0x80 | 0x100, 0, 0, "model: 0x80 with 0x100, a light volume (ps6)"},
      {0x80 | 0x100 | 0x40, 0, 2, "model: 0x80, 0x100 and 0x40, exterior (ps8)"},
      {0x10, 1, 0, "model: 0x10, a filler (ps10)"},
      {0x10 | 0x20, 1, 1, "model: a filler with 0x20 alone, interior (ps11)"},
      {0x10 | 0x40, 1, 2, "model: a filler with 0x40 alone, exterior (ps12)"},
      {0x10 | 0x60, 1, 0, "model: a filler with 0x20 and 0x40, every pixel (ps10)"},
      {0x10 | 0x80, 1, 0, "model: 0x10 before 0x80, a filler (ps10)"},
  };
  for (const auto& k : cases) {
    Record r = Spot();
    r.U(0x48, k.game);
    Check(Converts(r, &l, &why) && gfx::RtLightModel(l.flags) == k.model && gfx::RtLightClass(l.flags) == k.cls,
          k.what);
  }
}

void KeyChecks() {
  gfx::RtLight l{};
  Skip why;
  Record r = Point();
  r.U(0x60, 0x80001234u).U(0x68, 7).U(0x6C, 3);
  Check(Converts(r, &l, &why) && l.key == 0x80001234ull, "key: the id when not 0");
  Check(l.space[0] == 7 && l.space[1] == 3, "space: interior and room");
  r.U(0x60, 0);
  Check(Converts(r, &l, &why) && l.key == 0, "key: 0 without an id");
}

// A list of records as the game lays them out (kRecordStride apart, the
// bytes past kRecordSize junk), converted as the hook does.
void ListChecks() {
  constexpr uint32_t kN = 8;
  static uint8_t buf[kN * rl::kRecordStride];
  std::memset(buf, 0xCD, sizeof buf);
  Record recs[kN] = {Point(), Spot(), Point(), Point(), Spot(), Point(), Point(), Spot()};
  recs[0].U(0x60, 1);
  recs[1].U(0x60, 2);
  recs[2].U(0x44, 3);  // capsule
  recs[3].F(0x54, 0);  // dark
  recs[4].U(0x60, 5);
  recs[5].F(0x20, std::numeric_limits<float>::quiet_NaN());  // invalid
  recs[6].U(0x44, 1);                                        // other type
  recs[7].U(0x60, 8).F(0x5C, -0.5f);                         // a spot drawn as a point
  for (uint32_t i = 0; i < kN; i++) std::memcpy(buf + i * rl::kRecordStride, recs[i].b, rl::kRecordSize);
  gfx::RtLight out[rl::kMaxRecords];
  rl::ListCounts n;
  const uint32_t kept = rl::ConvertList(buf, kN, out, &n);
  Check(kept == 4 && n.kept == 4 && n.records == kN, "list: 4 lights of 8 records");
  Check(out[0].key == 1 && out[1].key == 2 && out[2].key == 5 && out[3].key == 8,
        "list: the lights packed in their order, those left out taking no place");
  Check(out[1].kind == gfx::RtLightKind::Spot && out[2].kind == gfx::RtLightKind::Spot &&
            out[3].kind == gfx::RtLightKind::Point && n.spots == 2,
        "list: spots counted (a spot drawn as a point not)");
  Check(n.skipped[int(Skip::Capsule)] == 1 && n.skipped[int(Skip::Dark)] == 1 && n.skipped[int(Skip::Invalid)] == 1 &&
            n.skipped[int(Skip::OtherType)] == 1 && n.skipped[int(Skip::None)] == 0,
        "list: those left out counted by reason");

  // A full buffer and more: at most kMaxRecords read.
  static uint8_t big[(rl::kMaxRecords + 4) * rl::kRecordStride];
  for (uint32_t i = 0; i < rl::kMaxRecords + 4; i++) {
    Record r = Point();
    r.U(0x60, i + 1);
    std::memcpy(big + i * rl::kRecordStride, r.b, rl::kRecordStride);
  }
  Check(rl::ConvertList(big, rl::kMaxRecords + 4, out, &n) == rl::kMaxRecords && n.records == rl::kMaxRecords &&
            out[rl::kMaxRecords - 1].key == rl::kMaxRecords,
        "list: at most kMaxRecords records read");
  Check(rl::ConvertList(nullptr, 5, out, &n) == 0 && n.kept == 0, "list: no array, no lights");
  Check(rl::ConvertList(buf, 0, out, &n) == 0 && n.records == 0, "list: no records, no lights");
}

void CountChecks() {
  Check(rl::ClampCount(0) == 0 && rl::ClampCount(-5) == 0, "count: none below 1");
  Check(rl::ClampCount(17) == 17 && rl::ClampCount(640) == 640, "count: as it is up to 640");
  Check(rl::ClampCount(641) == 640 && rl::ClampCount(0x7FFFFFFF) == 640, "count: at most a buffer's 640");
}

}  // namespace

int main() {
  PointChecks();
  SpotChecks();
  SkipChecks();
  FlagChecks();
  ModelChecks();
  KeyChecks();
  ListChecks();
  CountChecks();
  if (failures) std::printf("%d failed\n", failures);
  return failures ? 1 : 0;
}
