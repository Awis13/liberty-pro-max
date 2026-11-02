// Small gameplay and menu fixes, done again for 1.0.8.0 by FusionFix's
// ideas (ThirteenAG and contributors, GPL-3.0; its code not taken) and
// ZolikaPatch's (by its ini's descriptions only). Each one is an option
// (the launcher's Advanced tab sets them), on unless said otherwise:
//   GTAIV_GLASS_SHARD_COLOURS: broken tail lights shed red glass and
//     indicators amber glass, not clear;
//   GTAIV_BULLET_TRACERS (off by default): a tracer on every shot, not
//     only on some by chance;
//   GTAIV_TBOGT_TRACER_FX: The Ballad of Gay Tony uses the other episodes'
//     tracer particle (its own one is placed wrongly);
//   GTAIV_PAUSE_INFO_SPACING: the pause menu's time and money line spaced
//     out as on the consoles;
//   GTAIV_COMPARE_STRING_FIX: the scripts' COMPARE_STRING does not crash on
//     a string that is not there (phone calls in The Lost and Damned);
//   GTAIV_LONG_MENU_LISTS: menu lists scroll only past 32 rows, not 16;
//   GTAIV_DEATH_MUSIC (off by default): one of the six death and arrest
//     tunes plays when Niko is wasted or busted (cut on the PC);
//   GTAIV_EDITOR_FREE_CAMERA: the video editor's free camera is not held
//     near the player.

#include <cstdint>
#include <cstring>
#include <mutex>
#include <random>
#include <string_view>

#include "hook_util.h"
#include "option.h"
#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

void f_00B4F390_lifted(Ctx& c);

using namespace hook;

namespace {

// Strings the game reads from guest memory, in one block of their own
// (they live as long as the game).
uint32_t GuestString(std::string_view s) {
  static std::mutex mu;
  static uint32_t block = 0, used = 0;
  constexpr uint32_t kSize = 0x10000;
  std::lock_guard<std::mutex> lock(mu);
  if (!block) {
    const uint64_t b = g_proc->vm.Reserve(0, kSize);
    if (!b || !g_proc->vm.Commit(b, kSize, Prot::RW)) return 0;
    block = uint32_t(b);
  }
  if (used + s.size() + 1 > kSize) return 0;
  const uint32_t va = block + used;
  std::memcpy(GPtr(va), s.data(), s.size());
  Wr8(va + uint32_t(s.size()), 0);
  used += uint32_t(s.size() + 1 + 3) & ~3u;
  return va;
}

// Forces the outcome of the conditional jump after a compare: ComissFlags
// of "below" (CF = 1: jb/jbe taken, jae/ja not) or "above" (CF = ZF = 0).
void Below(Ctx& c) { ComissFlags(c, 0.0f, 1.0f); }
void Above(Ctx& c) { ComissFlags(c, 1.0f, 0.0f); }

}  // namespace

// Vehicle glass shards (a switch on the broken part): the tail lights'
// case (push "veh_glass_clear") and the indicators' (another
// "veh_glass_clear") give clear glass. The game's own type names for red
// and amber glass are in the exe already (D78D08, D78D18; the glass types
// are registered under them at 007ED118).
namespace {
bool GlassShardColours() {
  static const bool on = OptionOn("GTAIV_GLASS_SHARD_COLOURS", true);
  return on;
}
}  // namespace
void hook_00AB79A5(Ctx& c) { Push(c, GlassShardColours() ? 0x00D78D08 : 0x00DB95B8); }
void hook_00AB79AE(Ctx& c) { Push(c, GlassShardColours() ? 0x00D78D18 : 0x00DB95C8); }

// CWeapon::DoWeaponFireFx: a tracer is drawn when the weapon's tracer
// chance ([esp+1C]) is not below a random number (fld; fcomip st, st(1);
// fstp st(0); jb skip). The compare and both pops done here; with the
// option the jump is never taken. The random number is still drawn, as
// in the game.
void hook_009D22B2(Ctx& c) {
  static const bool on = OptionOn("GTAIV_BULLET_TRACERS", false);
  const auto r = x87::CompareStEFlags(c.fpu, 1, false);
  x87::Pop(c.fpu);  // fcomip
  x87::StoreSt(c.fpu, 0);  // fstp st(0)
  x87::Pop(c.fpu);
  Flags f;
  UnpackEFlags(f, c.eflags);
  f.zf = r.zf;
  f.pf = r.pf;
  f.cf = on ? false : r.cf;
  f.of = f.sf = f.af = false;
  c.eflags = PackEFlags(f);
}

// The tracer particle's name: The Ballad of Gay Tony (cmp [episode], 2;
// jne) has one of its own, placed wrongly on the screen. The one of IV and
// The Lost and Damned for it too. Nothing changes in IV.
void hook_00AAAEA9(Ctx& c) {
  static const bool on = OptionOn("GTAIV_TBOGT_TRACER_FX", true);
  CmpFlags<4>(c, on ? 0 : Rd32(0x0112D754), 2);
}

// The pause menu's line with the time and the money: since 1.0.6.0 its
// format strings have one space around each slash (" / %02d:%02d / $%d"),
// the consoles two. Four cases: money before or after the number, and the
// time alone (twice).
namespace {
uint32_t PauseInfoFormat(uint32_t game_va, const char* spaced) {
  static const bool on = OptionOn("GTAIV_PAUSE_INFO_SPACING", true);
  if (!on) return game_va;
  const uint32_t va = GuestString(spaced);
  return va ? va : game_va;
}
}  // namespace
void hook_00488DEA(Ctx& c) {
  static const uint32_t va = PauseInfoFormat(0x00E5C63C, "  /  %02d:%02d  /  $%d");
  Push(c, va);
}
void hook_00488E18(Ctx& c) {
  static const uint32_t va = PauseInfoFormat(0x00E5C650, "  /  %02d:%02d  /  %d$");
  Push(c, va);
}
void hook_00488E4C(Ctx& c) {
  static const uint32_t va = PauseInfoFormat(0x00E5C664, "  /  %02d:%02d");
  Push(c, va);
}
void hook_00488E6E(Ctx& c) {
  static const uint32_t va = PauseInfoFormat(0x00E5C674, "  /  %02d:%02d");
  Push(c, va);
}

// COMPARE_STRING's helper (cdecl (a, b), al = equal): null pointers are
// checked, other bad ones are not, and a script in The Lost and Damned's
// phone calls passes one. Every page of both strings is checked to be
// there before it is read; a string that is not counts as unequal.
namespace {
// A guest page that is committed and readable.
bool Readable(uint32_t va) {
  return g_proc->mem.IsCommitted(va) &&
         (uint8_t(g_proc->mem.QueryProt(va)) & uint8_t(Prot::R)) != 0;
}
bool GuestStringsEqual(uint32_t a, uint32_t b) {
  if (!a || !b) return false;
  for (uint32_t i = 0;; ++i) {
    const uint32_t pa = a + i, pb = b + i;
    if (pa < a || pb < b) return false;  // past the top of the space
    if ((i == 0 || (pa & 0xFFF) == 0) && !Readable(pa)) return false;
    if ((i == 0 || (pb & 0xFFF) == 0) && !Readable(pb)) return false;
    const uint8_t ca = Rd8(pa);
    if (ca != Rd8(pb)) return false;
    if (!ca) return true;
  }
}
}  // namespace
void f_00B4F390(Ctx& c) {
  static const bool on = OptionOn("GTAIV_COMPARE_STRING_FIX", true);
  if (!on) return f_00B4F390_lifted(c);
  c.eax = GuestStringsEqual(Rd32(c.esp + 4), Rd32(c.esp + 8)) ? 1 : 0;
  c.esp += 4;  // cdecl: the return address
}

// Menu lists: past 16 rows a list scrolls (cmp count, 16; jle) by count -
// 8 rows' worth (sub eax, 8), in two drawing functions, and the page with
// the highlighted row is found the same way (cmp row, 16; jg, then add
// row, -16). FusionFix's numbers: past 32 rows, by count - 16.
namespace {
bool LongMenuLists() {
  static const bool on = OptionOn("GTAIV_LONG_MENU_LISTS", true);
  return on;
}
// sub eax, n with its flags.
void SubEax(Ctx& c, uint32_t n) {
  CmpFlags<4>(c, c.eax, n);
  c.eax -= n;
}
}  // namespace
void hook_007C43B5(Ctx& c) { CmpFlags<4>(c, c.eax, LongMenuLists() ? 32 : 16); }
void hook_007C43C1(Ctx& c) { SubEax(c, LongMenuLists() ? 16 : 8); }
void hook_007C9F63(Ctx& c) { CmpFlags<4>(c, c.eax, LongMenuLists() ? 32 : 16); }
void hook_007C9F6F(Ctx& c) { SubEax(c, LongMenuLists() ? 16 : 8); }
void hook_00866F85(Ctx& c) { CmpFlags<4>(c, c.edi, LongMenuLists() ? 32 : 16); }
void hook_00866F9B(Ctx& c) {  // add edi, -16
  const uint32_t n = LongMenuLists() ? 32 : 16;
  Flags f;
  UnpackEFlags(f, c.eflags);
  c.edi = lift::Add<4>(f, c.edi, 0u - n);
  c.eflags = PackEFlags(f);
}

// The death and arrest music: the music state machine asks an empty
// function (00750FF0, always false) whether the player is wasted or
// busted, in three places, so the tune never plays. Answered from the
// game's state ([0112D778]: 1 wasted, 2 busted), in IV only, as
// FusionFix does by default. The tune's name is "DEATH_MUSIC_%02d" with
// 6 always (push 6); one of the six at random instead.
namespace {
bool DeathMusic() {
  static const bool on = OptionOn("GTAIV_DEATH_MUSIC", false);
  return on;
}
void WastedOrBusted(Ctx& c) {  // call 00750FF0 (al = the answer)
  bool r = false;
  if (DeathMusic() && Rd32(0x0112D754) == 0) {
    const uint32_t state = Rd32(0x0112D778);
    r = state == 1 || state == 2;
  }
  c.eax = (c.eax & ~0xFFu) | (r ? 1u : 0u);
}
}  // namespace
void hook_0081E3B8(Ctx& c) { WastedOrBusted(c); }
void hook_0081E4FC(Ctx& c) { WastedOrBusted(c); }
void hook_0081E53A(Ctx& c) { WastedOrBusted(c); }
void hook_0081E46D(Ctx& c) {
  uint32_t n = 6;
  if (DeathMusic()) {
    static std::minstd_rand rng{std::random_device{}()};
    n = std::uniform_int_distribution<uint32_t>(1, 6)(rng);
  }
  Push(c, n);
}

// The video editor's free camera is held near the player by compares:
// one that otherwise shows the editor's RE_MARKER message (cmp [edi+14],
// esi; jae skips it), two against 225 (15 m squared: pulled back, comiss;
// jbe, and flagged out of range, comiss; jb), a move refused past 900
// (three compares in a row, all must pass) and a reset past 900 (comiss;
// jb). Each compare's outcome set as FusionFix's unconditional jumps
// have it (the three in a row all passed, which is where its jump goes);
// without the option the compares as in the game.
namespace {
bool EditorFreeCamera() {
  static const bool on = OptionOn("GTAIV_EDITOR_FREE_CAMERA", true);
  return on;
}
// comiss xmm<a>, <b> as in the game, or the forced outcome.
void Comiss(Ctx& c, float a, float b, void (*forced)(Ctx&)) {
  if (EditorFreeCamera()) forced(c);
  else ComissFlags(c, a, b);
}
}  // namespace
void hook_00A164F7(Ctx& c) {  // cmp [edi+14], esi; jae: taken
  if (EditorFreeCamera()) Above(c);
  else CmpFlags<4>(c, Rd32(c.edi + 0x14), c.esi);
}
void hook_00A1BD13(Ctx& c) {  // comiss xmm7, xmm2; ... jbe: taken
  Comiss(c, Lane0(c.xmm[7]), Lane0(c.xmm[2]), Below);
}
void hook_00A1C081(Ctx& c) {  // comiss xmm1, xmm2; jb: taken
  Comiss(c, Lane0(c.xmm[1]), Lane0(c.xmm[2]), Below);
}
void hook_00A1C2FE(Ctx& c) {  // comiss xmm0, [esp+18]; jb: not taken
  Comiss(c, Lane0(c.xmm[0]), RdF32(c.esp + 0x18), Above);
}
void hook_00A1C305(Ctx& c) {  // comiss xmm0, xmm5; jb: not taken
  Comiss(c, Lane0(c.xmm[0]), Lane0(c.xmm[5]), Above);
}
void hook_00A1C320(Ctx& c) {  // comiss xmm1, xmm0; jb: not taken
  Comiss(c, Lane0(c.xmm[1]), Lane0(c.xmm[0]), Above);
}
void hook_00A1C3EE(Ctx& c) {  // comiss xmm0, [00D7B90C]; jb: taken
  Comiss(c, Lane0(c.xmm[0]), RdF32(0x00D7B90C), Below);
}

}  // namespace recomp::gen
