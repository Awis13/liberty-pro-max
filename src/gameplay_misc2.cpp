// Gameplay fixes and extras, second round, done again for 1.0.8.0 by their
// meaning (FusionFix's ideas, ZolikaPatch's ini descriptions; their code
// and data not taken). The scripts' side was read from the game's own
// script.img (decrypted with the key the executable holds, disassembled
// with the opcode table of its interpreter, 006240F0). Options:
//   GTAIV_OUT_OF_COMMISSION_FIX: the bike-to-helicopter climb in the last
//     mission (finale2: "Out of Commission" and "A Revenger's Tragedy")
//     does not get harder above 30 fps;
//   GTAIV_PHONE_CHEATS: two phone cheats for Niko: 458-555-0100 the
//     beta's gloves, 288-555-0100 the beta's hair (dialled again: back);
//   GTAIV_ALT_DIALOGUE (off by default): missions play the other variant
//     of their car banter, the one a retry plays.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>

#include "hook_util.h"
#include "option.h"
#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

void f_00B51C70_lifted(Ctx& c);
void f_0084D2B0_lifted(Ctx& c);
void f_008B3A30_lifted(Ctx& c);

using namespace hook;

namespace {

// The script virtual machine (rage::scrThread::Run, 006240F0).
constexpr gaddr kCurrentThread = 0x01794FB4;  // the thread running now
constexpr gaddr kGlobals = 0x01794FC0;        // the globals' array
constexpr gaddr kGlobalCount = 0x01794FC4;
constexpr gaddr kFindProgram = 0x00627500;    // esi: program hash; returns it
constexpr gaddr kFindNative = 0x00626AF0;     // esi: native hash; its handler
constexpr gaddr kEpisode = 0x0112D754;        // 0: IV

// thread +08 its program's hash, +10 the offset of the instruction it runs
// (set by NATIVE); program +08 the code, +10 its size. At load each
// NATIVE's hash (bytes 3..6: op, arguments, results, hash) is replaced by
// its handler (006252D0).
constexpr uint8_t kOpNative = 0x2D, kOpI2F = 0x25, kOpPushF = 0x2A, kOpJmp = 0x22,
                  kOpCall = 0x2E, kOpEnter = 0x2F, kOpPushS = 0x28, kOpPGlobal = 0x40,
                  kOpIAdd = 0x01, kOpPush0 = 0x60;

struct ScriptCode {
  uint32_t code = 0, size = 0, pc = 0;
  bool Has(uint32_t off, uint32_t n) const { return code && off + n <= size; }
  uint8_t U8(uint32_t off) const { return Rd8(code + off); }
  uint16_t U16(uint32_t off) const { return uint16_t(U8(off) | U8(off + 1) << 8); }
  uint32_t U32(uint32_t off) const {
    return uint32_t(U8(off)) | uint32_t(U8(off + 1)) << 8 | uint32_t(U8(off + 2)) << 16 |
           uint32_t(U8(off + 3)) << 24;
  }
  // NATIVE with `in` arguments and `out` results calling `handler` at off.
  bool Native(uint32_t off, uint8_t in, uint8_t out, uint32_t handler) const {
    return Has(off, 7) && U8(off) == kOpNative && U8(off + 1) == in && U8(off + 2) == out &&
           U32(off + 3) == handler;
  }
};

// The code of the script whose native is being called, and where.
ScriptCode CurrentScript(Ctx& c) {
  ScriptCode s;
  const uint32_t thread = Rd32(kCurrentThread);
  if (!thread) return s;
  uint32_t program;
  {
    KeepRegs keep(c);
    c.esi = Rd32(thread + 0x08);
    program = uint32_t(CallGuest(c, kFindProgram, {}));
  }
  if (!program) return s;
  s.code = Rd32(program + 0x08);
  s.size = Rd32(program + 0x10);
  s.pc = Rd32(thread + 0x10);
  return s;
}

// A native's handler, as the scripts' code holds it after loading.
uint32_t NativeHandler(Ctx& c, uint32_t hash) {
  KeepRegs keep(c);
  c.esi = hash;
  return uint32_t(CallGuest(c, kFindNative, {}));
}

enum : uint32_t {
  TIMERA = 0x75706300,
  SETTIMERA = 0x32501B1E,
  GET_HASH_KEY = 0x68FF7165,
  GET_PLAYER_ID = 0x62E319C6,
  CONVERT_INT_TO_PLAYERINDEX = 0x5996315E,
  GET_PLAYER_CHAR = 0x511454A9,
  GET_NUMBER_OF_CHAR_DRAWABLE_VARIATIONS = 0x3C293296,
  GET_CHAR_DRAWABLE_VARIATION = 0x1A1A6D83,
  SET_CHAR_COMPONENT_VARIATION = 0x71A52973,
  PRINT_HELP = 0x71076BBA,
};

}  // namespace

// --- The climb onto the helicopter in the last mission (Zolika's
// OutOfCommissionFix). finale2.sco's mash: each press adds 1.5 to the
// climb, and while TIMERA > 500 x GET_FRAME_TIME it loses 0.15 and TIMERA
// starts again (at 01CC47 and, for the other ending, 01F284). At 30 fps
// that is once a frame (4.5 a second); the threshold shrinks with the
// frame, so at 120 fps it is still once a frame, 18 a second, and the
// climb can hardly be won. GET_FRAME_TIME's native (00B51C70: its one
// argument points to the float; CTimer::ms_fTimeStep) gives this check at
// least 0.06 s: a loss at most every 30 timer ms, as at 30 fps (the
// timers count exact ms with fixes.cpp's 00625414). Only where the
// script's next instructions are that check (TIMERA; I2F; PUSHF 500.0), so
// FusionFix's own finale2.sco (time-based already) is left alone.
void f_00B51C70(Ctx& c) {
  static const bool on = OptionOn("GTAIV_OUT_OF_COMMISSION_FIX", true);
  const uint32_t out = Rd32(Rd32(Rd32(c.esp + 4) + 8));
  bool qte = false;
  if (on) {
    static uint32_t timera = 0;
    if (!timera) timera = NativeHandler(c, TIMERA);
    const ScriptCode s = CurrentScript(c);
    const uint32_t p = s.pc;
    qte = timera && s.Has(p, 7) && s.U8(p) == kOpNative && s.U8(p + 1) == 1 && s.U8(p + 2) == 0 &&
          s.Native(p + 7, 0, 1, timera) && s.Has(p + 14, 6) && s.U8(p + 14) == kOpI2F &&
          s.U8(p + 15) == kOpPushF &&
          s.U32(p + 16) == Bits(500.0f);
  }
  f_00B51C70_lifted(c);
  if (qte && out) WrF32(out, std::max(RdF32(out), 0.06f));
}

// --- Phone cheats (FusionFix's, the ones that need nothing of its own).
// spcellphonecalling.sco: when a dialled number rang 2 s with nobody
// answering it stops the ringing (006603) and goes on to the cheat check
// (006617), which hashes the number (global 91 + 128 bytes) and compares
// it with the game's cheats. STOP_MOBILE_PHONE_RINGING's native
// (0084D2B0, no arguments) checks the same number there: recognised only
// where the script's next instructions are "PUSH 0; SETTIMERA; JMP" to a
// CALL of a function that starts "ENTER; PUSHS a; PGLOBAL; PUSHS b; IADD;
// GET_HASH_KEY": the number is at global a + b bytes. Niko's components
// (the exe's table at 00F42C90): 4 hand, 7 hair; playerped.rpf has
// hand_001 and hair_001, the beta's. Help text CHEAT1/CHEAT2 ("Cheat
// activated" / "de-activated") is the game's.
namespace {

// Scripts' native call context (results, argument count, arguments) and
// room for the results and a text label, in guest stack below the caller.
constexpr uint32_t kCtx = 0x00, kArgs = 0x60, kOut = 0xA0, kText = 0xC0, kSize = 0xE0;

uint32_t CallNative(Ctx& c, uint32_t base, uint32_t hash, std::initializer_list<uint32_t> args) {
  const uint32_t fn = NativeHandler(c, hash);
  if (!fn) return 0;
  const uint32_t ctx = base + kCtx, stack = base + kArgs;
  std::memset(GPtr(ctx), 0, kOut);
  Wr32(ctx + 0x00, stack);
  Wr32(ctx + 0x04, uint32_t(args.size()));
  Wr32(ctx + 0x08, stack);
  uint32_t i = 0;
  for (uint32_t a : args) Wr32(stack + 4 * i++, a);
  CallGuest(c, fn, {ctx});
  return Rd32(stack);
}

// The dialled number's address when the phone script is at its cheat
// check, else 0.
uint32_t DialledNumber(Ctx& c) {
  static uint32_t settimera = 0, hashkey = 0;
  if (!settimera) settimera = NativeHandler(c, SETTIMERA);
  if (!hashkey) hashkey = NativeHandler(c, GET_HASH_KEY);
  const ScriptCode s = CurrentScript(c);
  const uint32_t p = s.pc;
  if (!settimera || !hashkey || !s.Has(p, 20) || s.U8(p) != kOpNative || s.U8(p + 7) != kOpPush0 ||
      !s.Native(p + 8, 1, 0, settimera) || s.U8(p + 15) != kOpJmp)
    return 0;
  const uint32_t call = s.U32(p + 16);
  if (!s.Has(call, 5) || s.U8(call) != kOpCall) return 0;
  const uint32_t f = s.U32(call + 1);
  if (!s.Has(f, 19) || s.U8(f) != kOpEnter || s.U8(f + 4) != kOpPushS || s.U8(f + 7) != kOpPGlobal ||
      s.U8(f + 8) != kOpPushS || s.U8(f + 11) != kOpIAdd || !s.Native(f + 12, 1, 1, hashkey))
    return 0;
  const uint32_t global = s.U16(f + 5), bytes = s.U16(f + 9);
  const uint32_t globals = Rd32(kGlobals);
  if (!globals || global >= Rd32(kGlobalCount)) return 0;
  return globals + 4 * global + bytes;
}

std::string GuestString(uint32_t va, size_t max) {
  std::string s;
  for (size_t i = 0; i < max; ++i) {
    const char ch = char(Rd8(va + uint32_t(i)));
    if (!ch) break;
    s += ch;
  }
  return s;
}

// Niko's component `comp` between drawables 0 and 1, with the game's help
// text.
void ToggleComponent(Ctx& c, uint32_t comp) {
  const uint32_t esp = c.esp;
  c.esp -= kSize;
  const uint32_t base = c.esp;
  {
    KeepRegs keep(c);
    const uint32_t player =
        CallNative(c, base, CONVERT_INT_TO_PLAYERINDEX, {CallNative(c, base, GET_PLAYER_ID, {})});
    Wr32(base + kOut, 0);
    CallNative(c, base, GET_PLAYER_CHAR, {player, base + kOut});
    const uint32_t ped = Rd32(base + kOut);
    if (ped && CallNative(c, base, GET_NUMBER_OF_CHAR_DRAWABLE_VARIATIONS, {ped, comp}) > 1) {
      const bool on = CallNative(c, base, GET_CHAR_DRAWABLE_VARIATION, {ped, comp}) == 0;
      CallNative(c, base, SET_CHAR_COMPONENT_VARIATION, {ped, comp, on ? 1u : 0u, 0});
      std::memcpy(GPtr(base + kText), on ? "CHEAT1" : "CHEAT2", 7);
      CallNative(c, base, PRINT_HELP, {base + kText});
    }
  }
  c.esp = esp;
}

}  // namespace

void f_0084D2B0(Ctx& c) {
  static const bool on = OptionOn("GTAIV_PHONE_CHEATS", true);
  uint32_t number = 0;
  if (on && Rd32(kEpisode) == 0) number = DialledNumber(c);
  f_0084D2B0_lifted(c);
  if (!number) return;
  const std::string dialled = GuestString(number, 16);
  if (dialled == "4585550100") ToggleComponent(c, 4);  // beta gloves
  else if (dialled == "2885550100") ToggleComponent(c, 7);  // beta hair
}

// --- Alternative mission dialogue (FusionFix's AltDialogue, its list not
// taken). Some IV missions keep a global counter of their car banter: 0
// plays one conversation (..V1) and moves it on, 1 the other (..V2), past
// that Niko listens to the radio; it lives on across retries, so a retry
// hears the other one. Set to 1 as the mission starts, the first attempt
// plays the retry's variant. The counters, from the 1.0.8.0 scripts: a
// global only that mission reads or writes (startup.sco gives it its
// first value), whose every read is a test opening code that only starts
// a conversation (or ambient speech), with its labels in pairs ..V1/..V2;
// each checked by hand. START_NEW_SCRIPT (00625880) and
// START_NEW_SCRIPT_WITH_ARGS (00625900) both start scripts through the
// launcher pointer [0108E7B8], before their first instruction. Its static
// value 00625B60 is replaced during game init (008B44C9) by 008B3A30, the
// launcher that runs from then on (cdecl: name, arguments, their size,
// stack size; returns the thread id).
namespace {

struct Counter {
  const char* script;
  uint32_t global;
};

// mission, counter (its labels)
constexpr Counter kCounters[] = {
    {"bell7", 64939},      // B6_BANT1V1 / B6_BANT1V2
    {"faustin7", 64731},   // F7_1av1 / F7_1av2
    {"finale1a", 64735},   // FIN1A_B3V1 / FIN1A_B3V2
    {"gambetti1", 64721},  // G1_BANT2V1 / G1_BANT2V2
    {"jimmy1", 64573},     // J1_B2V1 / J1_B2V2
    {"manny1", 64568},     // M1_BANTER / M1_BANTER2
    {"michelle1", 64565},  // MI1_B2V1 / MI1_B2V2
    {"playboy3", 64963},   // PX3_B2V1 / PX3_B2V2
    {"roman2", 64975},     // R2_B1V1 / R2_B1V2
    {"roman2", 64976},     // R2_B2V1 / R2_B2V2
    {"roman4", 64961},     // R4_CAR2 / R4_CAR3
    {"roman7", 64725},     // R7_BANT2v1 / R7_BANT2v2
};

bool SameName(const std::string& a, const char* b) {
  if (a.size() != std::strlen(b)) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) != b[i]) return false;
  return true;
}

}  // namespace

void f_008B3A30(Ctx& c) {
  static const bool on = OptionOn("GTAIV_ALT_DIALOGUE", false);
  const uint32_t name = Rd32(c.esp + 4);
  const uint32_t globals = Rd32(kGlobals);
  if (on && name && globals && Rd32(kEpisode) == 0) {
    const std::string script = GuestString(name, 64);
    const uint32_t count = Rd32(kGlobalCount);
    for (const Counter& k : kCounters)
      if (k.global < count && SameName(script, k.script)) Wr32(globals + 4 * k.global, 1);
  }
  f_008B3A30_lifted(c);
}

}  // namespace recomp::gen
