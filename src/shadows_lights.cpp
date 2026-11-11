// Shadows and lights, for 1.0.8.0. Each one an option (the launcher's
// Advanced tab sets them):
//   GTAIV_VEHICLE_NIGHT_SHADOWS: vehicles cast shadows from street lights,
//     tunnel lights and searchlights, and people in vehicles too; the
//     headlights then cast none. It draws vehicles with a shadow
//     technique only the extended vehicle shaders have, so it does anything
//     only with them (GTAIV_FF_SHADERS);
//   GTAIV_EMISSIVE_NO_DEPTH_WRITE: glowing surfaces (lamps, signs, strobe
//     lights) drawn without writing depth, as before patch 1.0.6.0 (no
//     glow cutting into what is in front of it);
//   GTAIV_DYNAMIC_SHADOWS_EVERYWHERE (off by default): every placed light
//     that may cast shadows casts moving ones (very costly);
//   GTAIV_CAR_SHADOW_ANY_DISTANCE: the dark patch under cars does not
//     fade out at 70-80 m (on by default);
//   GTAIV_NO_STATIC_CAR_SHADOWS (off by default): no dark patch under cars
//     at all; GTAIV_NO_CUTSCENE_STATIC_CAR_SHADOWS (off by default): none
//     in cutscenes.

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <vector>

#include "hook_util.h"
#include "night_shadow_slots.h"
#include "option.h"
#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

bool FfShadersOn();  // update_folder.cpp

using namespace hook;

namespace {

constexpr uint32_t kCutsceneState = 0x011515D0;  // non-zero while a cutscene runs (007F02B0)
constexpr uint32_t kCutsceneName = 0x0114FECC;   // the current cutscene's name
constexpr uint32_t kRenderPhase = 0x014663BC;    // CRenderPhase being drawn (0 outside)

}  // namespace


// Vehicle night shadows. The shadow maps of local lights (street lights,
// tunnel lights, searchlights) are filled by 0095AC40 (cdecl: render list,
// cascade or -1): with -1 it leaves out the entity lists 0 and 1, so
// vehicles never shadow there. Here they are drawn into it too, with the
// shadow technique for local lights the extended vehicle shaders add
// ("wd_local", pushed and popped around them as the game does its own).
// The technique group is registered just after the game's own "wd"
// (shaders map the groups known when they load). People sitting in a
// vehicle are left out of shadows (0095A41B) while vehicles cast none;
// now they cast them. The headlights' own shadows (light flag 4) go:
// with the car in the shadow map its own lights would be blocked by it.
namespace {

std::atomic<int32_t> g_local_tech{-1};  // the "wd_local" technique group

bool VehicleNightShadowsWanted() {
  static const bool on = OptionOn("GTAIV_VEHICLE_NIGHT_SHADOWS", true) && FfShadersOn();
  return on;
}

bool VehicleNightShadows() { return VehicleNightShadowsWanted() && g_local_tech.load() >= 0; }

// A draw command put in the list being built: new (0085E390, cdecl: size,
// 0), then grcDrawCommand::Append (00475920, thiscall).
void AppendCommand(Ctx& c, uint32_t obj) {
  c.ecx = obj;
  CallGuest(c, 0x00475920, {});
}

// CShaderFx push forced technique: its constructor 0085FB60 (thiscall:
// group), 12 bytes. False when the list has no room (then nothing else is
// added, so no pop takes off the technique the game pushed).
bool PushTechnique(Ctx& c, int32_t group) {
  const uint32_t obj = uint32_t(CallGuest(c, 0x0085E390, {0xC, 0}));
  if (!obj) return false;
  c.ecx = obj;
  CallGuest(c, 0x0085FB60, {uint32_t(group)});
  AppendCommand(c, obj);
  return true;
}

// The pop, 8 bytes; the game builds it inline (0095AF2B): the base
// command's vtable and serial number (low 14 bits of +4 from the counter
// [00F35058]), then its own vtable.
void PopTechnique(Ctx& c) {
  const uint32_t obj = uint32_t(CallGuest(c, 0x0085E390, {8, 0}));
  if (!obj) return;
  Wr32(obj, 0x00D76990);
  const uint32_t serial = Rd32(0x00F35058);
  Wr32(obj + 4, Rd32(obj + 4) ^ ((Rd32(obj + 4) ^ serial) & 0x3FFF));
  Wr32(0x00F35058, serial + 1);
  Wr32(obj, 0x00D95F9C);
  AppendCommand(c, obj);
}

}  // namespace

// After "wd" is registered (call 004185D0; push 1): "wd_local" too, by the
// same function (cdecl: name; it copies the name), the name on the stack.
void hook_008CB242(Ctx& c) {
  if (VehicleNightShadowsWanted() && g_local_tech.load() < 0) {
    int32_t group;
    {
      KeepRegs keep(c);
      static const char kName[] = "wd_local";
      c.esp -= 16;
      std::memcpy(GPtr(c.esp), kName, sizeof(kName));
      group = int32_t(CallGuest(c, 0x004185D0, {c.esp}));
      c.esp += 16;
    }
    g_local_tech.store(group);
  }
  Push(c, 1);
}

// 0095AC40: cmp byte [esp+13], bl (its second argument is not -1: the sun's
// cascades, 008CC106, pass 0; the local lights' phases, 00C53xxx, -1);
// pop ebp; je past lists 0 and 1. For a local light they are drawn here
// with "wd_local", all their entities (last argument -1, as the game's).
// The night shadows' slots held steady (night_shadow_slots.h; GTAIV_NIGHT_
// SHADOW_STABLE=0: as the game had them).
namespace {
bool NightShadowsStable() {
  static const bool on = OptionOn("GTAIV_NIGHT_SHADOW_STABLE", true);
  return on;
}
}  // namespace
// 008C4550: movss xmm3, [00F3E174] (-8.0), the step from the camera to the
// point the lights are ranked from: 0, the camera itself.
void hook_008C45CA(Ctx& c) { LoadSs(c.xmm[3], NightShadowsStable() ? 0.0f : RdF32(0x00F3E174)); }
// 008C4550: mov [esp+2C], edx, as a light (its record at esi + eax) with its
// distance in xmm1 goes into the ranking: a light that held a slot in the
// frame before competes at kHeldShare of it.
void hook_008C499A(Ctx& c) {
  Wr32(c.esp + 0x2C, c.edx);
  if (!NightShadowsStable()) return;
  namespace ns = night_shadow_slots;
  uint32_t ids[ns::kSlots];
  for (int k = 0; k < ns::kSlots; k++) ids[k] = Rd32(ns::kSlotTable + uint32_t(k) * ns::kSlotStride + ns::kSlotId);
  const uint32_t id = Rd32(c.esi + c.eax + 0x60);
  if (ns::Held(id, ids)) SetLane0(c.xmm[1], ns::RankDistance(Lane0(c.xmm[1]), true));
}

void hook_0095AEE9(Ctx& c) {
  const uint8_t cascade = Rd8(c.esp + 0x13);
  if (!cascade && VehicleNightShadows()) {
    KeepRegs keep(c);
    const uint32_t list = c.edi;
    if (PushTechnique(c, g_local_tech.load())) {
      CallGuest(c, 0x00959FF0, {list, 0, 0, 0, 0xFFFFFFFFu});
      CallGuest(c, 0x00959FF0, {list, 1, 0, 0, 0xFFFFFFFFu});
      PopTechnique(c);
    }
  }
  CmpFlags<1>(c, cascade, uint8_t(c.ebx));
  c.ebp = Rd32(c.esp);
  c.esp += 4;
}

// 00959FF0, the shadow entity list: cmp eax, 3 (a ped; jne: not one),
// then a ped in a vehicle ([ped+26C] & 4) is skipped outside the cascades.
// Not when vehicles shadow too.
void hook_0095A41B(Ctx& c) { CmpFlags<4>(c, VehicleNightShadows() ? 0 : c.eax, 3); }

// The headlights' light (00A2E5F0, its flags the third argument): 504h for
// the player's car, 104h for others. Flag 4 (dynamic shadows) dropped.
namespace {
uint32_t HeadlightFlags(uint32_t flags) { return VehicleNightShadows() ? flags & ~4u : flags; }
}  // namespace
void hook_008F71E1(Ctx& c) { Push(c, HeadlightFlags(0x504)); }
void hook_008F73EB(Ctx& c) { Push(c, HeadlightFlags(0x504)); }
void hook_008F722D(Ctx& c) { Push(c, HeadlightFlags(0x104)); }
void hook_008F7421(Ctx& c) { Push(c, HeadlightFlags(0x104)); }


// CRenderer::AddToDrawListEntityListStandard: before the emissive lists
// (types 4 and 5) a render state command turns depth writes on (rage
// state 6 = D3DRS_ZWRITEENABLE; push 1 to its constructor 0085F2B0), since
// patch 1.0.6.0; glow then hides what is drawn after it (strobe lights in
// clubs). Off, as before; but on as the game has it in the intro cutscene
// and at QUB3D's height (the arcade game is drawn above 3000 m), where it
// is needed.
void hook_00959C3E(Ctx& c) {
  static const bool on = OptionOn("GTAIV_EMISSIVE_NO_DEPTH_WRITE", true);
  bool write = true;
  if (on) {
    write = false;
    const uint32_t phase = Rd32(kRenderPhase);
    if (phase && RdF32(phase + 0x128) >= 3000.0f) write = true;  // camera z
    if (Rd32(kCutsceneState)) {
      const char* name = reinterpret_cast<const char*>(GPtr(kCutsceneName));
      const char kIntro[] = "intro";
      bool same = true;
      for (size_t i = 0; i < sizeof(kIntro); ++i)
        if (std::tolower(uint8_t(name[i])) != kIntro[i]) {
          same = false;
          break;
        }
      if (same) write = true;
    }
  }
  Push(c, write ? 1 : 0);
}


// The light built from a model's light effect (00A6F900): flag 4, dynamic
// shadows, when the light may cast them ([ebp+38]) and the effect says so
// (test [esi+4C], 8000h; je). Every such light, with the option.
void hook_00A700CA(Ctx& c) {
  static const bool on = OptionOn("GTAIV_DYNAMIC_SHADOWS_EVERYWHERE", false);
  TestFlags8(c, on || (Rd32(c.esi + 0x4C) & 0x8000) ? 1 : 0);
}


// The dark patch under a car or bike (00A2E5F0 type 3 through 008F6F30),
// from 008FB0B0: its strength fades with the distance, clamp((80 - d) /
// 10) (movss xmm0, [80.0]), and below 0.01 it is not drawn (comiss xmm6,
// [0.01]; jbe).
void hook_008FB168(Ctx& c) {
  static const bool on = OptionOn("GTAIV_CAR_SHADOW_ANY_DISTANCE", true);
  LoadSs(c.xmm[0], on ? 1.0e30f : RdF32(0x00D9D63C));
}
void hook_008FB380(Ctx& c) {
  static const bool none = OptionOn("GTAIV_NO_STATIC_CAR_SHADOWS", false);
  static const bool none_in_cutscenes = OptionOn("GTAIV_NO_CUTSCENE_STATIC_CAR_SHADOWS", false);
  const float threshold = RdF32(0x00D9D638);
  if (none || (none_in_cutscenes && Rd32(kCutsceneState)))
    ComissFlags(c, 0.0f, threshold);  // below: jbe taken
  else
    ComissFlags(c, Lane0(c.xmm[6]), threshold);
}

}  // namespace recomp::gen

// A model shader of the update folder's that a mod's model needs while the
// rest of its shaders are off (update_folder.cpp): its logarithmic depth (from PS c209, not set
// without them) does not belong with the game's own depth. The write to oDepth
// goes; nothing else in it reads what is computed for it.
namespace {
void DropLogDepth(std::vector<uint32_t>& tokens) {
  if (tokens.empty() || (tokens[0] & 0xFFFF0000) != 0xFFFF0000) return;  // a pixel shader's
  constexpr uint32_t kComment = 0xFFFE, kEnd = 0xFFFF, kDef = 0x51, kDcl = 0x1F;
  const auto type = [](uint32_t t) { return ((t >> 28) & 7) | ((t >> 8) & 0x18); };
  bool reads_c209 = false;
  size_t write = 0, write_tokens = 0;
  for (size_t i = 1; i < tokens.size();) {
    const uint32_t op = tokens[i] & 0xFFFF;
    if (op == kEnd) break;
    if (op == kComment) {
      i += 1 + ((tokens[i] >> 16) & 0x7FFF);
      continue;
    }
    const size_t n = (tokens[i] >> 24) & 0xF;
    if (op != kDef && op != kDcl && n && i + n < tokens.size()) {
      if (type(tokens[i + 1]) == 9) {  // D3DSPR_DEPTHOUT
        write = i;
        write_tokens = 1 + n;
      }
      for (size_t k = 2; k <= n; k++)
        if (type(tokens[i + k]) == 2 && (tokens[i + k] & 0x7FF) == 209) reads_c209 = true;  // D3DSPR_CONST
    }
    i += 1 + n;
  }
  if (write && reads_c209) tokens.erase(tokens.begin() + write, tokens.begin() + write + write_tokens);
}
}  // namespace

// The glowing signs of gta_emissivestrong.fxc (JIMS, Comrades Bar...) sit on
// or just behind what they light, and the shader moves them towards the camera
// by its zShift (c210.x), as every other emissive shader does: z - zShift. In
// this one the sign is wrong (add oPos.z, r0.z, c210.x, in all six of the
// game's shader folders and in no other shader), so they go behind the wall and
// show only at some angles: the operand gets its negation. Instructions are
// walked (comments skipped), in vertex shaders only.
// GTAIV_SIGN_GLOW_FIX=0 leaves it.
void GtaivShaderTokens(std::vector<uint32_t>& tokens) {
  if (!recomp::gen::FfShadersOn()) DropLogDepth(tokens);
  static const bool on = OptionOn("GTAIV_SIGN_GLOW_FIX", true);
  if (!on || tokens.empty() || (tokens[0] & 0xFFFF0000) != 0xFFFE0000) return;  // a vertex shader's
  for (size_t i = 1; i < tokens.size();) {
    const uint32_t op = tokens[i] & 0xFFFF;
    if (op == 0xFFFF) break;  // end
    if (op == 0xFFFE) {       // a comment
      i += 1 + ((tokens[i] >> 16) & 0x7FFF);
      continue;
    }
    if (tokens[i] == 0x03000002 && i + 3 < tokens.size() && tokens[i + 1] == 0xE0040000 &&
        tokens[i + 2] == 0x80AA0000 && tokens[i + 3] == 0xA00000D2)
      tokens[i + 3] = 0xA10000D2;  // add oPos.z, r0.z, -c210.x
    i += 1 + ((tokens[i] >> 24) & 0xF);
  }
}
