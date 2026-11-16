// Which shadow cascades are drawn with ray-traced sun shadows
// (src/rt_cascades.h): GTAIV_RT_CASCADES parsed; without traced shadows and
// with "all" every cascade as the game draws it; with "far" the far one
// only; by default the far one without the entity lists 0 and 1 on two
// turns of sixteen, in pairs, counted by the host's own schedule (its wrap
// included), so that the traced scene keeps what stands still between
// them: it retains a static instance listed two frames in a row, and lets
// one go only after more undrawn frames in view than the longest gap here
// (kSceneInViewFrames, x86recomp's kInViewFrames; its rt_scene_test checks
// that side with this schedule). GTAIV_RT_CASCADE_HYBRID=1 forcing all (the
// mode in force). Which entity lists are left out, and
// GTAIV_RT_GAME_SHADOWS's default.
// And the nearer cascades' empty redraw left out (EmptyCascades,
// GTAIV_RT_CASCADE_CLEAR_SKIP): a run of the cascade phase passed as the
// three hooks pass it; the clear and the copy enqueued both or neither; in
// full on the first two runs, on every 128th, after the list was drawn,
// after the targets changed, outside Sparse mode, without ray-traced
// shadows, with the switch off, and always for the far cascade, whose
// turns stay what they were.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "rt_cascades.h"

namespace {

namespace rc = gtaiv_rt_cascades;
using rc::Decision;
using rc::Mode;

int failures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok" : "FAIL", what);
  if (!ok) failures++;
}

constexpr uint32_t kBits[4] = {1u << 0, 1u << 1, 1u << 2, 1u << 3};

void ModeChecks() {
  Check(rc::ParseMode(nullptr) == Mode::Sparse, "mode: unset is sparse");
  Check(rc::ParseMode("") == Mode::Sparse, "mode: empty is sparse");
  Check(rc::ParseMode("all") == Mode::All, "mode: \"all\"");
  Check(rc::ParseMode("far") == Mode::Far, "mode: \"far\"");
  bool other = true;
  for (const char* e : {"sparse", "ALL", "Far", "all ", " far", "fa", "1", "0"})
    other = other && rc::ParseMode(e) == Mode::Sparse;
  Check(other, "mode: anything else (other case, spaces, prefixes, numbers) is sparse");
}

// The mode in force: All with GTAIV_RT_CASCADE_HYBRID=1 whatever
// GTAIV_RT_CASCADES says; without it GTAIV_RT_CASCADES parsed.
void EffectiveModeChecks() {
  const char* cascades[] = {nullptr, "", "all", "far", "sparse", "ALL"};
  bool forced = true, parsed = true;
  for (const char* c : cascades) {
    forced = forced && rc::EffectiveMode(c, "1") == Mode::All;
    for (const char* h : {static_cast<const char*>(nullptr), "", "0"})
      parsed = parsed && rc::EffectiveMode(c, h) == rc::ParseMode(c);
  }
  Check(forced, "hybrid \"1\": all, whatever GTAIV_RT_CASCADES says (the hybrid reads every cascade map)");
  Check(parsed, "hybrid unset, empty or \"0\": GTAIV_RT_CASCADES parsed");
  Check(rc::HybridRequested("1") && !rc::HybridRequested(nullptr) && !rc::HybridRequested("") &&
            !rc::HybridRequested("0"),
        "hybrid asked for with \"1\", not unset, empty or \"0\"");
  Check(rc::GameShadowsDefault(rc::EffectiveMode("far", "1")) == nullptr,
        "hybrid: GTAIV_RT_GAME_SHADOWS left to the renderer, as with all");
}

// Whether the schedule was asked: a copy of it compared after Decide.
bool Advanced(const rc::SparseSchedule& before, rc::SparseSchedule after) {
  rc::SparseSchedule b = before;
  for (int i = 0; i < int(rc::kSparsePeriod); i++)
    if (b.Next() != after.Next()) return true;
  return false;
}

void DecisionChecks() {
  bool ok = true, untouched = true;
  for (Mode m : {Mode::Sparse, Mode::All, Mode::Far})
    for (uint32_t bit : kBits) {
      rc::SparseSchedule s;
      const rc::SparseSchedule before = s;
      for (int f = 0; f < 40; f++) ok = ok && rc::Decide(m, false, bit, s) == Decision::Original;
      untouched = untouched && !Advanced(before, s);
    }
  Check(ok && untouched,
        "without ray-traced shadows every cascade drawn as the game does, in every mode (the schedule not asked)");

  ok = true;
  for (uint32_t bit : kBits) {
    rc::SparseSchedule s;
    for (int f = 0; f < 40; f++) ok = ok && rc::Decide(Mode::All, true, bit, s) == Decision::Original;
  }
  Check(ok, "all: every cascade drawn as the game does");

  ok = true;
  {
    rc::SparseSchedule s;
    for (int f = 0; f < 40; f++) {
      ok = ok && rc::Decide(Mode::Far, true, rc::kFarBit, s) == Decision::Original;
      for (uint32_t bit : {1u, 2u, 4u}) ok = ok && rc::Decide(Mode::Far, true, bit, s) == Decision::Skip;
    }
  }
  Check(rc::kFarBit == 8u && ok, "far: the far cascade (bit 8) drawn as the game does every frame, bits 1 2 4 skipped");

  ok = true;
  untouched = true;
  {
    rc::SparseSchedule s;
    const rc::SparseSchedule before = s;
    for (int f = 0; f < 40; f++)
      for (uint32_t bit : {1u, 2u, 4u}) ok = ok && rc::Decide(Mode::Sparse, true, bit, s) == Decision::Skip;
    untouched = !Advanced(before, s);
  }
  Check(ok && untouched, "sparse: the nearer cascades skipped every time, the far one's schedule not asked by them");

  // The far cascade's turns: drawn on turns 0 1 16 17 ..., skipped between.
  rc::SparseSchedule s;
  std::vector<Decision> d;
  for (int f = 0; f < 34; f++) {
    for (uint32_t bit : {1u, 2u, 4u}) rc::Decide(Mode::Sparse, true, bit, s);  // (each run of the phase)
    d.push_back(rc::Decide(Mode::Sparse, true, rc::kFarBit, s));
  }
  ok = true;
  for (int f = 0; f < 34; f++) {
    const bool drawn = f % 16 < 2;
    ok = ok && d[size_t(f)] == (drawn ? Decision::FarWithoutDynamic : Decision::Skip);
  }
  Check(ok, "sparse: the far cascade without lists 0 and 1 on its turns 0 1 16 17 32 33, skipped on 2-15 and 18-31");
}

// Over `n` turns of a schedule starting at `start`: the number drawn,
// whether every drawn turn has a drawn neighbour, and the longest run of
// undrawn turns.
struct Run {
  int draws = 0;
  bool paired = true;
  int longest_gap = 0;
};

Run Over(uint32_t start, uint32_t n) {
  rc::SparseSchedule s(start);
  std::vector<bool> d(n);
  for (uint32_t i = 0; i < n; i++) d[i] = rc::Decide(Mode::Sparse, true, rc::kFarBit, s) == Decision::FarWithoutDynamic;
  Run r;
  int gap = 0;
  for (uint32_t i = 0; i < n; i++) {
    if (d[i]) {
      r.draws++;
      const bool before = i > 0 && d[i - 1], after = i + 1 < n && d[i + 1];
      r.paired = r.paired && (before || after);
      gap = 0;
    } else {
      gap++;
      r.longest_gap = gap > r.longest_gap ? gap : r.longest_gap;
    }
  }
  return r;
}

void SequenceChecks() {
  constexpr int kGap = int(rc::kSparsePeriod - rc::kSparseFrames);
  const Run r = Over(0, 160);
  Check(r.draws == 20, "sparse: 20 draws of the far cascade in 160 turns");
  Check(r.paired, "sparse: every drawn turn next to another drawn one (two in a row: the scene retains)");
  Check(r.longest_gap == kGap, "sparse: never more than period - frames (14) undrawn turns in a row");
  Check(r.longest_gap < int(rc::kSceneInViewFrames),
        "sparse: the longest gap below the traced scene's undrawn frames in view (kSceneInViewFrames)");
  // Across the schedule's wrap (the pairs at 0xFFFFFFF0 and 0 on each side).
  const Run w = Over(0xFFFFFFF0u, 32);
  Check(w.draws == 4 && w.paired && w.longest_gap == kGap, "sparse: the same across the schedule counter's wrap");
  const Run m = Over(7, 50);  // (started mid-period)
  Check(m.paired && m.longest_gap <= kGap, "sparse: started anywhere, still in pairs, gaps no longer");
}

void ListChecks() {
  bool ok = rc::SuppressedList(0) && rc::SuppressedList(1);
  for (uint32_t l : {2u, 3u, 4u, 5u, 6u, 7u, 8u, 0xFFFFFFFFu}) ok = ok && !rc::SuppressedList(l);
  Check(ok, "far cascade without what moves: entity lists 0 and 1 left out, the others (7: trees) drawn");
}

void GameShadowsChecks() {
  const char* s = rc::GameShadowsDefault(Mode::Sparse);
  const char* f = rc::GameShadowsDefault(Mode::Far);
  Check(s && !std::strcmp(s, "lit") && f && !std::strcmp(f, "lit") && !rc::GameShadowsDefault(Mode::All),
        "GTAIV_RT_GAME_SHADOWS: lit by default with sparse and far, the renderer's own default with all");
}

// One run of the cascade phase as the hooks pass it (rt_cascades.cpp): for
// each cascade the clear's site, the list's and the copy's.
struct PhaseRun {
  uint32_t cleared = 0;  // cascades (bits) whose clear was enqueued
  uint32_t copied = 0;   // whose copy into the atlas was
  uint32_t listed = 0;   // whose list was drawn
  bool forced = true;    // LeftOut exactly where the clear was not enqueued
};

PhaseRun RunPhase(rc::EmptyCascades& e, rc::SparseSchedule& far, bool enabled, Mode mode, bool rt,
                  const rc::CascadeTargets& targets) {
  PhaseRun r;
  for (uint32_t bit : kBits) {
    const bool clear = e.Begin(enabled, mode, rt, bit, targets);
    if (clear) r.cleared |= bit;
    for (uint32_t other : kBits) r.forced = r.forced && e.LeftOut(other) == (other == bit && !clear);
    if (!e.LeftOut(bit)) {
      const Decision d = rc::Decide(mode, rt, bit, far);
      e.Listed(bit, d);
      if (d != Decision::Skip) r.listed |= bit;
    }
    if (e.End(bit)) r.copied |= bit;
  }
  return r;
}

constexpr uint32_t kNear = 1u | 2u | 4u;
constexpr uint32_t kAllBits = kNear | rc::kFarBit;

// Whether run `n` (from 0) of a row of empty runs draws the nearer
// cascades in full: the first two, and every 128th.
bool FullRun(uint32_t n) { return n < 2 || n % 128 == 0; }

void EmptyCascadeChecks() {
  Check(rc::kEmptySettleRuns == 2 && rc::kEmptyRefreshPeriod == 128,
        "empty cascades: two runs settle a quarter, a full run every 128th");
  Check(rc::CascadeIndex(1) == 0 && rc::CascadeIndex(2) == 1 && rc::CascadeIndex(4) == 2 &&
            rc::CascadeIndex(rc::kFarBit) == 3 && rc::CascadeIndex(0) == -1 && rc::CascadeIndex(3) == -1 &&
            rc::CascadeIndex(16) == -1,
        "empty cascades: a cascade's index from its bit, -1 for anything else");

  const rc::CascadeTargets t{};

  // Sparse with ray-traced shadows: the pattern, both commands or neither,
  // the list never drawn onto a map not cleared, the far cascade in full.
  {
    rc::EmptyCascades e;
    rc::SparseSchedule far;
    bool pattern = true, paired = true, forced = true, far_full = true, no_near_list = true;
    int full = 0;
    for (uint32_t n = 0; n < 1280; n++) {
      const PhaseRun r = RunPhase(e, far, true, Mode::Sparse, true, t);
      pattern = pattern && (r.cleared & kNear) == (FullRun(n) ? kNear : 0u);
      paired = paired && r.cleared == r.copied;
      forced = forced && r.forced;
      far_full = far_full && (r.cleared & rc::kFarBit);
      no_near_list = no_near_list && !(r.listed & kNear);
      if (r.cleared & 1u) full++;
    }
    Check(pattern, "empty cascades: sparse, the nearer three in full on runs 0 1 128 256 ..., left out between");
    Check(paired, "empty cascades: the clear and the copy enqueued both or neither, every cascade, every run");
    Check(forced, "empty cascades: a cascade left out skips its list (and only it)");
    Check(far_full, "empty cascades: the far cascade's clear and copy enqueued on every run");
    Check(no_near_list, "empty cascades: the nearer lists never drawn in sparse mode");
    Check(full == 11, "empty cascades: 11 full runs of a nearer cascade in 1280 (2 + 9 refreshes)");
  }

  // The far cascade's turns are what they were without the skip.
  {
    rc::EmptyCascades on, off;
    rc::SparseSchedule far_on, far_off;
    bool same = true, off_full = true;
    for (uint32_t n = 0; n < 300; n++) {
      const PhaseRun a = RunPhase(on, far_on, true, Mode::Sparse, true, t);
      const PhaseRun b = RunPhase(off, far_off, false, Mode::Sparse, true, t);
      same = same && a.listed == b.listed && (a.listed & rc::kFarBit) == (n % 16 < 2 ? rc::kFarBit : 0u);
      off_full = off_full && b.cleared == kAllBits && b.copied == kAllBits && b.forced;
    }
    Check(same, "empty cascades: the far cascade's list on the same turns (0 1 16 17 ...) with the skip and without");
    Check(off_full, "empty cascades: switched off, every cascade's clear and copy on every run");
  }

  // Outside sparse mode and without ray-traced shadows: in full.
  {
    bool ok = true;
    for (Mode m : {Mode::All, Mode::Far})
      for (bool rt : {false, true}) {
        rc::EmptyCascades e;
        rc::SparseSchedule far;
        for (uint32_t n = 0; n < 300; n++) {
          const PhaseRun r = RunPhase(e, far, true, m, rt, t);
          ok = ok && r.cleared == kAllBits && r.copied == kAllBits && r.forced;
        }
      }
    rc::EmptyCascades e;
    rc::SparseSchedule far;
    for (uint32_t n = 0; n < 300; n++) {
      const PhaseRun r = RunPhase(e, far, true, Mode::Sparse, false, t);
      ok = ok && r.cleared == kAllBits && r.copied == kAllBits && r.listed == kAllBits;
    }
    Check(ok, "empty cascades: all, far, and sparse without ray-traced shadows draw every cascade in full every run");
  }

  // The list drawn (ray-traced shadows off for a run): the quarters are no
  // longer empty, so two full runs again once the shadows are back.
  {
    rc::EmptyCascades e;
    rc::SparseSchedule far;
    bool ok = true;
    for (uint32_t n = 0; n < 10; n++) RunPhase(e, far, true, Mode::Sparse, true, t);
    ok = ok && RunPhase(e, far, true, Mode::Sparse, true, t).cleared == rc::kFarBit;  // (settled)
    const PhaseRun drawn = RunPhase(e, far, true, Mode::Sparse, false, t);
    ok = ok && drawn.cleared == kAllBits && drawn.copied == kAllBits && drawn.listed == kAllBits;
    for (uint32_t n = 0; n < 5; n++) {
      const PhaseRun r = RunPhase(e, far, true, Mode::Sparse, true, t);
      ok = ok && (r.cleared & kNear) == (n < 2 ? kNear : 0u) && r.cleared == r.copied;
    }
    Check(ok, "empty cascades: after a run that drew the lists, two full runs before the next one left out");
  }

  // The targets changed (any word): two full runs again.
  {
    bool ok = true;
    for (int w = 0; w < rc::CascadeTargets::kWords; w++) {
      rc::EmptyCascades e;
      rc::SparseSchedule far;
      rc::CascadeTargets now{};
      for (uint32_t n = 0; n < 10; n++) RunPhase(e, far, true, Mode::Sparse, true, now);
      ok = ok && RunPhase(e, far, true, Mode::Sparse, true, now).cleared == rc::kFarBit;  // (settled)
      now.word[w] += 1;
      for (uint32_t n = 0; n < 5; n++) {
        const PhaseRun r = RunPhase(e, far, true, Mode::Sparse, true, now);
        ok = ok && (r.cleared & kNear) == (n < 2 ? kNear : 0u) && r.cleared == r.copied;
      }
    }
    rc::CascadeTargets a{}, b{};
    ok = ok && rc::SameTargets(a, b);
    b.word[rc::CascadeTargets::kWords - 1] = 1;
    ok = ok && !rc::SameTargets(a, b);
    Check(ok, "empty cascades: a target, a size or the device-reset count changed, two full runs again");
  }

  // Across the count's wrap: left out up to it, then two full runs.
  {
    rc::EmptyCascades e(0xFFFFFFFEu);
    rc::SparseSchedule far;
    bool ok = true;
    const bool want_full[6] = {false, false, true, true, false, false};
    for (bool full : want_full) {
      const PhaseRun r = RunPhase(e, far, true, Mode::Sparse, true, t);
      ok = ok && (r.cleared & kNear) == (full ? kNear : 0u) && r.cleared == r.copied;
    }
    Check(ok, "empty cascades: the same across the count's wrap (two full runs after it)");
  }

  // A turn not seen from its start: the copy enqueued, the count started again.
  {
    rc::EmptyCascades e;
    rc::SparseSchedule far;
    for (uint32_t n = 0; n < 10; n++) RunPhase(e, far, true, Mode::Sparse, true, t);
    bool ok = e.End(1u) && !e.LeftOut(1u);
    const PhaseRun r = RunPhase(e, far, true, Mode::Sparse, true, t);
    ok = ok && r.cleared == (1u | rc::kFarBit) && r.copied == r.cleared;
    Check(ok, "empty cascades: a copy's site without its clear's enqueues the copy, that cascade in full next");
  }
}

}  // namespace

int main() {
  ModeChecks();
  EffectiveModeChecks();
  DecisionChecks();
  SequenceChecks();
  ListChecks();
  GameShadowsChecks();
  EmptyCascadeChecks();
  if (failures) return 1;
  std::puts("rt cascades: ok");
  return 0;
}
