// Ray tracing: the frame's light list handed to the renderer (rt_lights.cpp,
// the hook; x86recomp's gfx::SetRtLights), as pure logic: one record of the
// game's list to a gfx::RtLight; tests/rt_lights_test.cpp checks it.
//
// The list. The update thread fills one of three buffers of records
// (0x13A6B80 + index * 0x14000: 640 records of 0x80 bytes; their counts at
// [0x013A41B8 + index * 4]); the render thread's command 00A2E9D0
// (CSetRenderLightsDC::Execute) makes the one its command was built with
// current: the count at [0x013A4194] (00A2EA17), the array at [0x00F49528]
// (00A2E9E6), then 00A2E8A0 culls them (types 0 and 2) without changing a
// record. 00A2C680 draws their volumes from it, once a frame.
//
// A record (0x78 bytes copied, 0x80 apart), by 00A2C680 and the constants
// 00A96030 sets for a light's volume:
//   +0x00 direction (normalised by 00A2C680 before use), +0x10 tangent,
//   +0x20 position (world, metres), +0x30 colour rgb, +0x40 intensity
//   (00A96030 sets the four as deferredLightColourAndIntensity: below),
//   +0x44 type, +0x48 flags, +0x4C/+0x50 projected texture, +0x54 radius
//   (00A96030: deferredLightInvSqrRadius = 1 / r^2),
//   +0x58 cos(inner), +0x5C cos(outer), +0x60 id (often 0), +0x64 shadow
//   slot (-1: none), +0x68 interior, +0x6C room, +0x70/+0x74 volume.
// Types: 0 point; 2 spot, its cone offset +0x5C and scale 1 / max(+0x58 -
// +0x5C, 0.01), but with +0x5C below 0 drawn as type 4 (offset -2, scale
// 1: no cone at all, a point); 3 a capsule drawn with colour 0, the dark
// patch under a car, not a light (skipped); 00A2C680 and 00A2E8A0 take no
// other type (skipped). Flags: 0x04 dynamic shadow, 0x08 shaft volume,
// 0x20 interior and 0x40 exterior pixel class, 0x100 vehicle headlights,
// 0x400 the player's.
//
// The game's falloff, the contract for what lights with these (gfx::
// RtLight: radius r, colour already times intensity). The constants'
// names are 00A96A90's lookups (deferredLightColourAndIntensity at
// [01466454], deferredLightInvSqrRadius [01466444], deferredLightDirection
// [0146644C], deferredLightConeOffset [01466488], deferredLightConeScale
// [01466404]), set by 00A96030 (the colour from the record's +0x30, +0x34,
// +0x38, +0x40 as one vector: 00A2C680 passes them so). The maths is the
// stock light-volume pixel shaders' in deferred_lighting.fxc (win32_30;
// extracted with tools/fxc_shaders.py and disassembled by hand), ps6 (the
// same in ps7, ps8, ps9, ps14 (lightVolumeShadowPoint), ps16 and ps17
// (lightVolumeShadowTexPoint); the same constants in nv8's and atidx10's
// ps6). With r3 = gDeferredLightPosition - p (L unnormalised):
//   dp3 r0.z, r3, r3                                        d^2
//   mad_sat r0.z, r0.z, -c73{InvSqrRadius}.x, 1             a = sat(1 - d^2/r^2)
//   mul r0.z, r0.z, r0.z
//   mad r0.z, r0.z, r0.z, -0.1                              a^4 - 0.1
//   mul r0.x, r0.z, 1.11111116                              (a^4 - 0.1) * 1.1111
//   dp3 r0.y, normalize(L + 1e-5), -c72{Direction}
//   add r0.y, r0.y, -c74{ConeOffset}.x
//   mul_sat r0.y, r0.y, c75{ConeScale}.x                    cone
//   mul r2.xyz, c76{ColourAndIntensity}.w, c76.xyz          colour * intensity
//   mul r2.xyz, r0.x * cone, r2
//   cmp r0.xyz, r0.z, r2, 0                                 0 where a^4 < 0.1
// then times sat(dot(N, L)) and the albedo, plus a specular term. So, at a
// point at distance d, L the unit vector from the point to the light:
//   a = saturate(1 - d^2 / r^2);
//   attenuation = (a^4 - 0.1) * 1.1111, 0 where a^4 < 0.1;
//   spot: cone = saturate((dot(L, -direction) - cone_offset) * cone_scale)
//         (gfx::RtLight's dot(D, direction), D = -L); a point (and type 4)
//         gets offset -2, scale 1 from 00A96030: cone 1 everywhere;
//   light = colour * attenuation * cone times the surface's response.
// The a and the cone's sign are what light_shadow_tokens.h already reads
// of the shadowed shaders. Another variant, ps10, ps11, ps12 and ps15
// (fillerVolumeShadowPoint), falls off as (a^2 - 1/3) * 1.5 (0 below) with
// a wrapped diffuse sat((dot(N, L) - 0.2) * 1.25); which records the game
// draws with those techniques (00A2C680's choice by flags) is not
// established here: a consumer that matches the game per light must find
// it before it relies on the a^4 form for every light.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "runtime/gfx/rt_light_list.h"

namespace gtaiv_rt_lights {

using recomp::gfx::RtLight;

constexpr uint32_t kRecordSize = 0x78;    // bytes of a record the game copies
constexpr uint32_t kRecordStride = 0x80;  // between records
constexpr uint32_t kMaxRecords = 640;     // a buffer's records (0x14000 / 0x80)
static_assert(kMaxRecords <= recomp::gfx::kRtLightsMax, "a frame's list fits the renderer's");

// Offsets in a record.
constexpr uint32_t kDirection = 0x00, kPosition = 0x20, kColour = 0x30, kIntensity = 0x40, kType = 0x44, kFlags = 0x48,
                   kRadius = 0x54, kCosInner = 0x58, kCosOuter = 0x5C, kId = 0x60, kInterior = 0x68, kRoom = 0x6C;

// Types (+0x44).
constexpr uint32_t kTypePoint = 0, kTypeSpot = 2, kTypeCapsule = 3;

// Flags (+0x48).
constexpr uint32_t kFlagShadow = 0x04, kFlagFiller = 0x10, kFlagInterior = 0x20, kFlagExterior = 0x40,
                   kFlagClassOne = 0x80, kFlagHeadlights = 0x100;

// Why a record is no light for the renderer.
enum class Skip {
  None,
  Capsule,    // type 3: a dark patch
  OtherType,  // a type the game does not draw as a light
  Dark,       // radius 0 or less, no colour (colour * intensity 0 in every channel) or one darkening (a channel
              // below 0: gfx::RtLightValid drops it too)
  Invalid,    // a number not finite, or a spot without a direction
};

inline float F32(const uint8_t* r, uint32_t off) {
  float v;
  std::memcpy(&v, r + off, 4);
  return v;
}

inline uint32_t U32(const uint8_t* r, uint32_t off) {
  uint32_t v;
  std::memcpy(&v, r + off, 4);
  return v;
}

// The spot's cone as the game's shader gets it (00A96030).
inline float ConeOffset(float cos_outer) { return cos_outer; }
inline float ConeScale(float cos_inner, float cos_outer) {
  const float d = cos_inner - cos_outer;
  return 1.0f / (d <= 0.01f ? 0.01f : d);
}

// The game's flags as gfx::RtLight's.
inline uint32_t MapFlags(uint32_t f) {
  uint32_t o = 0;
  if (f & kFlagShadow) o |= recomp::gfx::kRtLightCastsShadow;
  if (f & kFlagInterior) o |= recomp::gfx::kRtLightInterior;
  if (f & kFlagExterior) o |= recomp::gfx::kRtLightExterior;
  if (f & kFlagHeadlights) o |= recomp::gfx::kRtLightVehicleLamp;
  return o;
}

// The light model and pixel class of the technique 00A2C680 draws a light
// without a shadow slot with (light_volume_tokens.h): 00A2C9CB-00A2CA29,
// the flags tested in this order. With 0x10, a filler (techniques 10, 11,
// 12: ps10 everywhere, ps11 interior, ps12 exterior); else 0x80 without
// 0x100, technique 7 (ps9, class one); else a light volume (4, 5, 6: ps6,
// ps7, ps8). Interior where 0x20 alone is set, exterior where 0x40 alone,
// every pixel with both or neither. (A shadow slot, decided as it draws,
// gives techniques 8 and 9, ps14 and ps15, the same models; a projected
// texture 19 and 20, model 0, the texture not carried.)
inline uint32_t ModelAndClass(uint32_t f) {
  using recomp::gfx::kRtLightClassShift, recomp::gfx::kRtLightModelShift;
  const bool in = f & kFlagInterior, out = f & kFlagExterior;
  const uint32_t side = in && !out ? 1u : out && !in ? 2u : 0u;  // light_volume_tokens.h's classes
  if (f & kFlagFiller) return (1u << kRtLightModelShift) | (side << kRtLightClassShift);
  if ((f & kFlagClassOne) && !(f & kFlagHeadlights)) return 3u << kRtLightClassShift;
  return side << kRtLightClassShift;
}

// The light of `record` (kRecordSize bytes) in `out`; false, with the
// reason in `why` (optional), where it is none.
inline bool Convert(const uint8_t* record, RtLight* out, Skip* why = nullptr) {
  auto skip = [why](Skip s) {
    if (why) *why = s;
    return false;
  };
  const uint32_t type = U32(record, kType);
  if (type == kTypeCapsule) return skip(Skip::Capsule);
  if (type != kTypePoint && type != kTypeSpot) return skip(Skip::OtherType);
  RtLight l{};
  for (int i = 0; i < 3; i++) l.position[i] = F32(record, kPosition + 4 * i);
  l.radius = F32(record, kRadius);
  const float intensity = F32(record, kIntensity);
  for (int i = 0; i < 3; i++) l.color[i] = F32(record, kColour + 4 * i) * intensity;
  const float cos_inner = F32(record, kCosInner), cos_outer = F32(record, kCosOuter);
  bool finite = std::isfinite(l.radius);
  for (int i = 0; i < 3; i++) finite = finite && std::isfinite(l.position[i]) && std::isfinite(l.color[i]);
  if (!finite) return skip(Skip::Invalid);
  if (!(l.radius > 0) || !(l.color[0] > 0 || l.color[1] > 0 || l.color[2] > 0) || l.color[0] < 0 || l.color[1] < 0 ||
      l.color[2] < 0)
    return skip(Skip::Dark);
  // A spot whose outer cosine is below 0 is drawn without a cone (type 4).
  if (type == kTypeSpot && !(cos_outer < 0)) {
    if (!std::isfinite(cos_inner) || !std::isfinite(cos_outer)) return skip(Skip::Invalid);
    const float d[3] = {F32(record, kDirection), F32(record, kDirection + 4), F32(record, kDirection + 8)};
    const float len2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    if (!std::isfinite(len2) || !(len2 > 0)) return skip(Skip::Invalid);
    const float inv = 1.0f / std::sqrt(len2);
    for (int i = 0; i < 3; i++) l.direction[i] = d[i] * inv;
    l.kind = recomp::gfx::RtLightKind::Spot;
    l.cone_offset = ConeOffset(cos_outer);
    l.cone_scale = ConeScale(cos_inner, cos_outer);
  } else {
    l.kind = recomp::gfx::RtLightKind::Point;
  }
  l.flags = MapFlags(U32(record, kFlags)) | ModelAndClass(U32(record, kFlags));
  l.key = U32(record, kId);  // 0: none
  l.space[0] = U32(record, kInterior);
  l.space[1] = U32(record, kRoom);
  *out = l;
  if (why) *why = Skip::None;
  return true;
}

// What ConvertList made of a list: its records, the lights (and spots
// among them), and the records left out by reason (Skip's index).
struct ListCounts {
  uint32_t records = 0, kept = 0, spots = 0;
  uint32_t skipped[5] = {};
};

// The lights of `n` records (kRecordStride apart from `records`, at most
// kMaxRecords) into out[0, kept), in their order, those left out not
// taking a place; returns kept.
inline uint32_t ConvertList(const uint8_t* records, uint32_t n, RtLight* out, ListCounts* counts) {
  ListCounts c;
  c.records = n < kMaxRecords ? n : kMaxRecords;
  for (uint32_t i = 0; records && i < c.records; i++) {
    Skip why = Skip::None;
    if (!Convert(records + size_t(i) * kRecordStride, &out[c.kept], &why)) {
      c.skipped[int(why)]++;
      continue;
    }
    if (out[c.kept].kind == recomp::gfx::RtLightKind::Spot) c.spots++;
    c.kept++;
  }
  if (counts) *counts = c;
  return c.kept;
}

// The count at [0x013A4194] as the hook takes it: 0 below 1, at most a
// buffer's records.
inline uint32_t ClampCount(int32_t n) { return n <= 0 ? 0 : n > int32_t(kMaxRecords) ? kMaxRecords : uint32_t(n); }

}  // namespace gtaiv_rt_lights
