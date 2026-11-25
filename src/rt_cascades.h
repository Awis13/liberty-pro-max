// Ray-traced sun shadows: which of the game's shadow cascades are drawn
// (rt_cascades.cpp, the hook on the cascade phase's draw of a cascade's
// list), as pure logic; tests/rt_cascades_test.cpp checks it.
//
// GTAIV_RT_CASCADES:
//   unset, empty or anything else (Sparse): only the far cascade, on
//     kSparseFrames runs of the cascade phase in kSparsePeriod, drawn as the
//     game draws a cascade but without its entity lists 0 and 1
//     (FarWithoutDynamic); the nearer ones skipped;
//   "all": the four as the game draws them;
//   "far": the far one every run as the game draws it, the others skipped.
// GTAIV_RT_CASCADE_HYBRID=1 (the traced sun shadow leaving out rays where
// a cascade map says a point is surely lit: shadows_lights.cpp) needs all
// four maps drawn every frame, so with it the mode is All whatever
// GTAIV_RT_CASCADES says (EffectiveMode: the one rule the hook, main.cpp
// and the shader's consumer all take).
// Without ray-traced shadows every cascade is drawn as the game has it.
//
// GTAIV_RT_CASCADE_CLEAR_SKIP (on unless "0"): in Sparse mode with
// ray-traced shadows the cascade phase does not enqueue a nearer cascade's
// two drawing commands either (the map's clear and its copy into the
// atlas) while the cascade's quarter of the atlas already holds what they
// would draw (EmptyCascades); "0": every cascade's two enqueued on every
// run of the phase, as the game does.
//
// What a cascade's list holds and how 0095AC40 (list, second argument)
// draws it, by the decompile: three passes, each of entity lists of the
// render list (00959FF0: list, entity list, mode, pass, second argument);
// pass 1 lists 1, 0, 5 (and 7 for a cascade), pass 2 lists 1, 0, 5, pass 0
// lists 0 and 1 (for a cascade only), 2 and 7 (for a cascade only).
// Lists 0 and 1 hold vehicles (entity type 2, which 00959FF0 draws in pass
// 0 only), people (type 3, passes 1 and 2) and other dynamic entities. List
// 7 holds the trees (in pass 0 through the tree imposters, 008C07C0 and
// 008C1BE0, or one by one; in pass 1 one by one).
// A cascade passes 0 as the second argument (008CBF50: 00B6FBF0(list, -1,
// 0)); a local light's shadow map -1, and with -1 0095AC40 also leaves out
// list 7 in passes 1 and 0 (0095AD0D, 0095AF17: no trees), forces pass 0's
// technique without its cascade variant (00AC4EB0(technique, false):
// "wd_masked" where the device has DF24 or DF16 depth and not INTZ or
// RAWZ), and skips the pair of commands around pass 0 (0095AE7E, 0095AF6B:
// 00849E60 false, then true) that turn off the per-geometry visibility
// test of a drawable's parts against the viewport ([018CAE08], read by
// 00426430). So the far cascade is drawn by the game's own call (second
// argument 0: its static lists exactly as the game draws a cascade), and
// lists 0 and 1 are left out in every pass where 00959FF0 is entered
// (SuppressedList): a steady absence of what moves, not one that comes and
// goes with the frames the far cascade is drawn on.

#pragma once

#include <cstdint>
#include <cstring>

namespace gtaiv_rt_cascades {

enum class Mode { Sparse, All, Far };

// What the hook does with a cascade's list.
enum class Decision {
  Original,           // the game's call
  Skip,               // nothing drawn
  FarWithoutDynamic,  // the game's call, entity lists 0 and 1 left out (SuppressedList)
};

constexpr uint32_t kFarBit = 1u << 3;   // the far cascade's bit in the cascade phase's mask
constexpr uint32_t kSparsePeriod = 16;  // runs of the cascade phase
constexpr uint32_t kSparseFrames = 2;   // drawn at the start of each period, two in a row

// The undrawn frames in the camera's view after which the traced scene lets
// a retained instance go: x86recomp's RtScene::kInViewFrames
// (runtime/gfx/rt_scene.h), mirrored here for the port's tests, which cannot
// include that header. The far cascade's gap between two pairs stays below
// it, so what stands still is kept from one pair to the next.
constexpr uint32_t kSceneInViewFrames = 30;
static_assert(kSparsePeriod - kSparseFrames < kSceneInViewFrames,
              "the far cascade's gap between pairs must stay below the scene's undrawn frames in view");

inline Mode ParseMode(const char* e) {
  if (e && !std::strcmp(e, "all")) return Mode::All;
  if (e && !std::strcmp(e, "far")) return Mode::Far;
  return Mode::Sparse;
}

// GTAIV_RT_CASCADE_HYBRID asked for: its value starting with '1'.
inline bool HybridRequested(const char* hybrid_env) { return hybrid_env && *hybrid_env == '1'; }

// The mode in force: All with the hybrid (it reads every cascade map, so
// every one is drawn every frame), else GTAIV_RT_CASCADES parsed.
inline Mode EffectiveMode(const char* cascades_env, const char* hybrid_env) {
  return HybridRequested(hybrid_env) ? Mode::All : ParseMode(cascades_env);
}

// The far cascade's turns in Sparse mode, counted by the host: drawn on the
// first kSparseFrames of every kSparsePeriod turns (not by the game's frame
// counter, which a pause or a menu may set back, splitting a pair).
class SparseSchedule {
 public:
  explicit SparseSchedule(uint32_t turns = 0) : turns_(turns) {}
  // This turn drawn; the next turn counted.
  bool Next() {
    const bool drawn = turns_ % kSparsePeriod < kSparseFrames;
    turns_++;
    return drawn;
  }

 private:
  uint32_t turns_ = 0;  // (wraps at a multiple of kSparsePeriod)
};

// cascade_bit: the bit of the cascade being drawn; `far` is asked only on
// the far cascade's turns in Sparse mode with ray-traced shadows.
inline Decision Decide(Mode mode, bool rt_shadows_on, uint32_t cascade_bit, SparseSchedule& far) {
  if (mode == Mode::All || !rt_shadows_on) return Decision::Original;
  if (cascade_bit != kFarBit) return Decision::Skip;
  if (mode == Mode::Far) return Decision::Original;
  return far.Next() ? Decision::FarWithoutDynamic : Decision::Skip;
}

// The entity lists (00959FF0's second argument) left out of the far
// cascade's draw (Decision::FarWithoutDynamic), in every pass.
inline bool SuppressedList(uint32_t entity_list) { return entity_list == 0 || entity_list == 1; }

// The nearer cascades' empty redraw left out (GTAIV_RT_CASCADE_CLEAR_SKIP).
//
// The cascade phase (008CBF50) draws every cascade into the one cascade
// map ([012284B0], with the depth [012284A8]) and then copies the map into
// the cascade's quarter of the atlas ([012284AC]), by two commands it
// enqueues around the list's draw: 004CE610(008C6AF0) at 008CC0F7 (the
// depth cleared, then one quad over the whole map writing the constant
// -100: nothing here) and 008CAD60(008C8B10, cascade) at 008CC1BD (the
// atlas locked, one quad averaging the map's taps into the quarter, the
// atlas unlocked). With the list skipped the two write the same texels on
// every run: a constant map, so a constant quarter. At a 4096 x 4096 map
// and an 8192 x 2048 atlas that is 0.21 ms of GPU for the map's pass and
// 0.17 ms for the quarter's, for each of the three nearer cascades, every
// frame.
//
// So while a nearer cascade's quarter holds that empty result, neither
// command is enqueued for it and its list is skipped as before: the quarter
// keeps its texels, byte for byte what the two would have written. Both
// commands or neither: the map is shared, and between two runs it holds
// the far cascade, so a copy without the clear before it would put the far
// cascade into a nearer one's quarter. The far cascade's own turn clears
// the map itself, whatever it held. The lock and the unlock of the map
// around the list (0085EE70, 0085EEF0: no clear in either) stay enqueued,
// so the game's stack of targets is as it was; with no draw and no clear
// between them the renderer opens no pass.
//
// A quarter holds the empty result after kEmptySettleRuns runs of the
// phase in a row that drew the cascade in full with its list skipped (the
// first alone would do; the second is for a run whose commands did not
// reach the device). Whatever may have changed the quarter starts that
// count again: the cascade's list drawn (ray-traced shadows off for a
// while), the targets made anew or the device reset (CascadeTargets). And
// on every kEmptyRefreshPeriod-th run in a row the cascade is drawn in
// full all the same, so that a quarter changed by something this does not
// know of is put right within 128 runs.
// Only in Sparse mode: All draws every list, and Far (the far cascade as
// the game draws it, for comparisons) stays as it was.
constexpr uint32_t kEmptySettleRuns = 2;
constexpr uint32_t kEmptyRefreshPeriod = 128;
static_assert(kEmptySettleRuns < kEmptyRefreshPeriod, "a refresh period must hold the runs that settle a quarter");

// What the cascade map and the atlas are made of, as the hook reads it on
// every run: the three targets ([012284B0], [012284A8], [012284AC]), what
// 008C8910 made them from ([0122845C], [01228448], [01228450]: it makes
// them anew exactly when one of these changes), and the count of the
// cascades' device-reset callback (008CB160). A word changed: the textures
// may be new ones, their contents gone.
struct CascadeTargets {
  static constexpr int kWords = 7;
  uint32_t word[kWords] = {};
};

inline bool SameTargets(const CascadeTargets& a, const CascadeTargets& b) {
  return !std::memcmp(a.word, b.word, sizeof a.word);
}

// A cascade's index (0-3) from its bit in the cascade phase's mask; -1 for
// anything else.
inline int CascadeIndex(uint32_t cascade_bit) {
  for (int k = 0; k < 4; k++)
    if (cascade_bit == 1u << k) return k;
  return -1;
}

// A cascade's turn in a run of the phase, as its three hooks pass it:
// Begin at the clear's enqueue, LeftOut and Listed at the list's draw, End
// at the copy's enqueue. On the phase's one thread.
class EmptyCascades {
 public:
  // (For the tests: every cascade's count of empty runs to start from.)
  explicit EmptyCascades(uint32_t empty_runs = 0) {
    for (uint32_t& e : empty_) e = empty_runs;
  }

  // At the clear's enqueue (008CC0F7): whether the cascade's two drawing
  // commands are enqueued on this run. False: neither, and its list is
  // skipped (LeftOut), whatever Decide would say by then. `enabled`:
  // GTAIV_RT_CASCADE_CLEAR_SKIP.
  bool Begin(bool enabled, Mode mode, bool rt_shadows_on, uint32_t cascade_bit, const CascadeTargets& targets) {
    if (have_targets_ && !SameTargets(targets, targets_)) {
      for (uint32_t& e : empty_) e = 0;
    }
    targets_ = targets;
    have_targets_ = true;
    bit_ = cascade_bit;
    list_skipped_ = false;
    left_out_ = false;
    const int k = CascadeIndex(cascade_bit);
    if (k < 0) return true;
    // The cascades whose list Decide skips on every run.
    const bool never_listed = mode == Mode::Sparse && rt_shadows_on && cascade_bit != kFarBit;
    left_out_ = enabled && never_listed && empty_[k] >= kEmptySettleRuns && empty_[k] % kEmptyRefreshPeriod != 0;
    return !left_out_;
  }

  // At the list's draw (008CC106): whether Begin left the cascade's
  // commands out; its list is then not drawn and Decide not asked (in
  // Sparse mode a nearer cascade's turn does not ask the far one's
  // schedule, so nothing is missed).
  bool LeftOut(uint32_t cascade_bit) const { return left_out_ && bit_ == cascade_bit; }

  // What Decide said of the list of a cascade not left out.
  void Listed(uint32_t cascade_bit, Decision d) {
    if (bit_ == cascade_bit) list_skipped_ = d == Decision::Skip;
  }

  // At the copy's enqueue (008CC1BD), the end of the cascade's turn:
  // whether it is enqueued, which is what Begin said of the clear. The
  // cascade's quarter is empty after this run if it was left alone or drawn
  // in full with its list skipped; else (its list drawn, or a turn this did
  // not see from its start) the count starts again.
  bool End(uint32_t cascade_bit) {
    const int k = CascadeIndex(cascade_bit);
    const bool began = k >= 0 && bit_ == cascade_bit;
    const bool left_out = began && left_out_;
    if (k >= 0) empty_[k] = began && (left_out || list_skipped_) ? empty_[k] + 1 : 0;
    bit_ = 0;
    left_out_ = false;
    list_skipped_ = false;
    return !left_out;
  }

 private:
  // Per cascade: the runs in a row that left its quarter of the atlas the
  // empty result (wraps at a multiple of kEmptyRefreshPeriod, into two
  // full runs).
  uint32_t empty_[4] = {};
  CascadeTargets targets_;
  bool have_targets_ = false;
  uint32_t bit_ = 0;           // the cascade whose turn is open (0: none)
  bool left_out_ = false;      // its two commands not enqueued
  bool list_skipped_ = false;  // its list skipped by Decide
};

// GTAIV_RT_GAME_SHADOWS's default: with any mode but All the cascade maps
// are not drawn every frame (the nearer ones never), so a pixel shader that
// still samples them (GtaivRayShadowConsumer: RayShadow::own_map) would
// flicker; "lit": such shaders read a map that is lit everywhere instead.
// Null: the renderer's own default (the game's maps). Only used where ray-
// traced shadows are on (the renderer ignores it otherwise); the variable
// set by hand wins.
inline const char* GameShadowsDefault(Mode mode) { return mode == Mode::All ? nullptr : "lit"; }

}  // namespace gtaiv_rt_cascades
