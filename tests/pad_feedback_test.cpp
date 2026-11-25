// pad_feedback (src/pad_feedback.h): what a DualSense's triggers and light
// bar do for what the player is doing. Nothing outside play; on foot the
// fire trigger by the weapon's class, the recoil while shooting (one kick
// a shot, or as long as an automatic weapon fires) and nothing for fists,
// melee and thrown weapons; driving, both pedals and the moment's shake on a gear
// change; the light only while wanted. And the frame's step: with the
// option off the game is not read and nothing is handed on.

#include "pad_feedback.h"

#include <cstdint>
#include <cstdio>
#include <iterator>

namespace {

int g_failures = 0;

void Expect(bool ok, const char* what, int line) {
  if (ok) return;
  std::fprintf(stderr, "pad_feedback_test.cpp:%d: FAILED: %s\n", line, what);
  g_failures++;
}
#define EXPECT(x) Expect((x), #x, __LINE__)

using pad_feedback::PadEffects;
using pad_feedback::PadFeedback;
using pad_feedback::PadFeedbackLoop;
using pad_feedback::State;
using pad_feedback::TriggerEffect;
using pad_feedback::Weapon;
using recomp::hle::Normalized;
using recomp::hle::PadColour;

const TriggerEffect kOff;
const Weapon kGuns[] = {Weapon::kPistol, Weapon::kShotgun, Weapon::kSmg,
                        Weapon::kRifle,  Weapon::kSniper,  Weapon::kHeavy};

TriggerEffect Effect(TriggerEffect::Mode mode, int start, int end, int strength, int frequency) {
  return {mode, uint8_t(start), uint8_t(end), uint8_t(strength), uint8_t(frequency)};
}

State OnFoot(Weapon w, int wanted = 0, uint32_t ms = 0) {
  State s;
  s.playing = true;
  s.weapon = w;
  s.wanted = wanted;
  s.ms = ms;
  return s;
}

State Driving(int gear, float speed, uint32_t ms, int wanted = 0) {
  State s;
  s.playing = true;
  s.driving = true;
  s.gear = gear;
  s.speed = speed;
  s.ms = ms;
  s.wanted = wanted;
  return s;
}

// The light left to the pad: no colour asked for.
bool OwnLight(const PadEffects& e) { return !e.light; }
bool Red(const PadEffects& e) { return e.light && e.colour == PadColour{255, 0, 0}; }
bool Blue(const PadEffects& e) { return e.light && e.colour == PadColour{0, 0, 255}; }

// One state alone, with nothing before it.
PadEffects For(const State& s) { return PadFeedback().Update(s); }

void TestNothingOutsidePlay() {
  // Whatever else the game says: in a car, armed, wanted.
  State s = Driving(2, 20, 0, 4);
  s.weapon = Weapon::kShotgun;
  s.playing = false;
  const PadEffects e = For(s);
  EXPECT(e.left == kOff);
  EXPECT(e.right == kOff);
  EXPECT(OwnLight(e));
  s.driving = false;
  EXPECT(For(s).right == kOff);
  EXPECT(OwnLight(For(s)));
  EXPECT(For(State()) == PadEffects());
}

void TestWeaponSlots() {
  using pad_feedback::WeaponOfSlot;
  EXPECT(WeaponOfSlot(0) == Weapon::kNone);  // unarmed
  EXPECT(WeaponOfSlot(1) == Weapon::kNone);  // melee
  EXPECT(WeaponOfSlot(2) == Weapon::kPistol);
  EXPECT(WeaponOfSlot(3) == Weapon::kShotgun);
  EXPECT(WeaponOfSlot(4) == Weapon::kSmg);
  EXPECT(WeaponOfSlot(5) == Weapon::kRifle);
  EXPECT(WeaponOfSlot(6) == Weapon::kSniper);
  EXPECT(WeaponOfSlot(7) == Weapon::kHeavy);
  for (int slot : {-1, 8, 9, 10, 11, 12, 99}) EXPECT(WeaponOfSlot(slot) == Weapon::kNone);  // thrown, special
}

void TestOnFoot() {
  const PadEffects none = For(OnFoot(Weapon::kNone));
  EXPECT(none.left == kOff);
  EXPECT(none.right == kOff);
  EXPECT(OwnLight(none));
  // Before a shot, each class: resistance from zone 2 up to its click.
  EXPECT(For(OnFoot(Weapon::kPistol)).right == Effect(TriggerEffect::kWeapon, 2, 5, 6, 0));
  EXPECT(For(OnFoot(Weapon::kSmg)).right == Effect(TriggerEffect::kWeapon, 2, 4, 5, 0));
  EXPECT(For(OnFoot(Weapon::kRifle)).right == Effect(TriggerEffect::kWeapon, 2, 5, 7, 0));
  EXPECT(For(OnFoot(Weapon::kShotgun)).right == Effect(TriggerEffect::kWeapon, 2, 6, 8, 0));
  EXPECT(For(OnFoot(Weapon::kSniper)).right == Effect(TriggerEffect::kWeapon, 2, 6, 7, 0));
  EXPECT(For(OnFoot(Weapon::kHeavy)).right == Effect(TriggerEffect::kWeapon, 2, 7, 8, 0));
  for (Weapon w : kGuns) {
    const PadEffects e = For(OnFoot(w));
    EXPECT(e.left == kOff);  // the aim trigger is left alone
    EXPECT(e.right == Normalized(e.right));  // in the range a pad takes
  }
  // Each class its own.
  for (size_t a = 0; a < std::size(kGuns); a++)
    for (size_t b = a + 1; b < std::size(kGuns); b++)
      EXPECT(!(For(OnFoot(kGuns[a])).right == For(OnFoot(kGuns[b])).right));
}

// A shooter on foot over frames 16 ms apart: the weapon type, the clip,
// the game's shooting flag.
struct Shooter {
  PadFeedback f;
  Weapon w;
  int type;
  uint32_t ms = 1000;

  PadEffects Frame(int clip, bool shooting, uint32_t step = 16) {
    State s = OnFoot(w, 0, ms += step);
    s.weapon_type = type;
    s.clip = clip;
    s.shooting = shooting;
    return f.Update(s);
  }
};

const TriggerEffect kPistolReady = Effect(TriggerEffect::kWeapon, 2, 5, 6, 0);
const TriggerEffect kPistolKick = Effect(TriggerEffect::kVibration, 1, 0, 6, 30);

void TestAimingDoesNotVibrate() {
  Shooter s{PadFeedback(), Weapon::kRifle, 14};
  for (int i = 0; i < 100; i++) EXPECT(s.Frame(30, false).right == Effect(TriggerEffect::kWeapon, 2, 5, 7, 0));
  EXPECT(s.f.fire() == pad_feedback::Fire::kReady);
}

// A single-shot weapon: one kick a shot, for its length, then the click.
void TestOneKickAShot() {
  Shooter s{PadFeedback(), Weapon::kPistol, 7};
  EXPECT(s.Frame(17, false).right == kPistolReady);
  // The clip goes down: a shot. The kick: vibration from zone 1, 6 of 8, 30 Hz, 70 ms.
  EXPECT(s.Frame(16, false).right == kPistolKick);
  EXPECT(s.f.fire() == pad_feedback::Fire::kKick);
  EXPECT(s.Frame(16, false, 69).right == kPistolKick);  // 69 ms after the shot
  EXPECT(s.Frame(16, false, 1).right == kPistolReady);  // 70 ms: over
  EXPECT(s.f.fire() == pad_feedback::Fire::kReady);
  // The flag coming on is a shot too; staying on is no new one.
  EXPECT(s.Frame(16, true).right == kPistolKick);
  EXPECT(s.Frame(16, true, 70).right == kPistolReady);
  EXPECT(s.Frame(16, true).right == kPistolReady);
  // Flag and clip for the same shot in two frames: one kick (restarted).
  EXPECT(s.Frame(16, false).right == kPistolReady);
  EXPECT(s.Frame(16, true).right == kPistolKick);
  EXPECT(s.Frame(15, true, 50).right == kPistolKick);
  EXPECT(s.Frame(15, true, 69).right == kPistolKick);
  EXPECT(s.Frame(15, false, 1).right == kPistolReady);
  // Reloading (the clip goes up) is no shot.
  EXPECT(s.Frame(17, false).right == kPistolReady);
  // The others' kicks: the strongest for the shotgun and the sniper rifle.
  EXPECT(pad_feedback::Recoil(Weapon::kShotgun) == Effect(TriggerEffect::kVibration, 1, 0, 8, 20));
  EXPECT(pad_feedback::Recoil(Weapon::kSniper) == Effect(TriggerEffect::kVibration, 1, 0, 8, 20));
  EXPECT(pad_feedback::Recoil(Weapon::kHeavy) == Effect(TriggerEffect::kVibration, 1, 0, 8, 20));
  EXPECT(pad_feedback::KickMs(Weapon::kShotgun) == 120);
  EXPECT(pad_feedback::KickMs(Weapon::kSniper) == 120);
  EXPECT(pad_feedback::KickMs(Weapon::kHeavy) == 150);
  Shooter g{PadFeedback(), Weapon::kShotgun, 10};
  g.Frame(8, false);
  EXPECT(g.Frame(7, false).right == Effect(TriggerEffect::kVibration, 1, 0, 8, 20));
  EXPECT(g.Frame(7, false, 119).right.mode == TriggerEffect::kVibration);
  EXPECT(g.Frame(7, false, 1).right == Effect(TriggerEffect::kWeapon, 2, 6, 8, 0));
}

// An automatic weapon: vibration at its rate as long as it fires, the
// click again at once when it stops.
void TestBurst() {
  const TriggerEffect ready = Effect(TriggerEffect::kWeapon, 2, 4, 5, 0);
  const TriggerEffect burst = Effect(TriggerEffect::kVibration, 1, 0, 6, 15);  // smg: 15 Hz
  Shooter s{PadFeedback(), Weapon::kSmg, 12};
  EXPECT(s.Frame(30, false).right == ready);
  EXPECT(s.Frame(29, true).right == burst);
  EXPECT(s.f.fire() == pad_feedback::Fire::kBurst);
  for (int clip = 28; clip > 20; clip--) EXPECT(s.Frame(clip, true, 66).right == burst);
  // The flag goes off between shots: still firing while shots keep coming.
  EXPECT(s.Frame(20, false, 66).right == burst);
  EXPECT(s.Frame(19, false, 66).right == burst);
  // Released: no shot for 150 ms: the click again.
  EXPECT(s.Frame(19, false, 149).right == burst);
  EXPECT(s.Frame(19, false, 1).right == ready);
  EXPECT(s.f.fire() == pad_feedback::Fire::kReady);
  // The flag held on alone is firing (its coming on a shot).
  EXPECT(s.Frame(19, true, 400).right == burst);
  EXPECT(s.Frame(19, true, 1000).right == burst);
  EXPECT(s.Frame(19, false, 16).right == ready);  // off, and its shot long ago
  // A rifle the same, at 10 Hz, a shot every 100 ms.
  const TriggerEffect rifle = Effect(TriggerEffect::kVibration, 1, 0, 8, 10);
  Shooter r{PadFeedback(), Weapon::kRifle, 14};
  r.Frame(30, false);
  for (int clip = 29; clip > 20; clip--) EXPECT(r.Frame(clip, false, 100).right == rifle);
  EXPECT(r.Frame(21, false, 100).right == rifle);
  EXPECT(r.Frame(21, false, 50).right == Effect(TriggerEffect::kWeapon, 2, 5, 7, 0));
}

void TestNoShotAcrossWeapons() {
  // A new weapon with fewer rounds in its clip is no shot.
  Shooter s{PadFeedback(), Weapon::kRifle, 14};
  s.Frame(30, false);
  s.type = 15;
  EXPECT(s.Frame(10, false).right == Effect(TriggerEffect::kWeapon, 2, 5, 7, 0));
  // Nor after a car, a menu, or fists in between.
  s.Frame(30, false);
  {
    State d = Driving(1, 10, s.ms += 16);
    s.f.Update(d);
  }
  EXPECT(s.Frame(5, false).right == Effect(TriggerEffect::kWeapon, 2, 5, 7, 0));
  s.f.Update(State());
  EXPECT(s.Frame(2, false).right == Effect(TriggerEffect::kWeapon, 2, 5, 7, 0));
  // A kick running when the car is entered does not come back after.
  Shooter p{PadFeedback(), Weapon::kPistol, 7};
  p.Frame(17, false);
  EXPECT(p.Frame(16, false).right == kPistolKick);
  p.f.Update(Driving(1, 10, p.ms += 16));
  EXPECT(p.Frame(16, false).right == kPistolReady);
  // A shot long ago does not come back when the clock wraps round.
  Shooter w{PadFeedback(), Weapon::kPistol, 7};
  w.Frame(17, false);
  EXPECT(w.Frame(16, false).right == kPistolKick);
  EXPECT(w.Frame(16, false, 100).right == kPistolReady);
  EXPECT(w.Frame(16, false, uint32_t(0x100000000ull - 100 + 10)).right == kPistolReady);
  // Melee and fists: nothing, whatever the flag.
  EXPECT(pad_feedback::FireTrigger(Weapon::kNone) == kOff);
  EXPECT(pad_feedback::Recoil(Weapon::kNone) == kOff);
  Shooter m{PadFeedback(), Weapon::kNone, 1};
  EXPECT(m.Frame(-1, true).right == kOff);
  EXPECT(m.f.fire() == pad_feedback::Fire::kNone);
}

void TestDriving() {
  State s = Driving(1, 10, 0);
  s.weapon = Weapon::kShotgun;  // in hand at the wheel: the pedals all the same
  const PadEffects e = For(s);
  // Accelerate: feedback from zone 1 at strength 2; the brake at 4.
  EXPECT(e.right == Effect(TriggerEffect::kFeedback, 1, 0, 2, 0));
  EXPECT(e.left == Effect(TriggerEffect::kFeedback, 1, 0, 4, 0));
  EXPECT(e.left.strength > e.right.strength);  // the brake is the harder pedal
  EXPECT(OwnLight(e));
  // Standing, and in gear 0, the same.
  EXPECT(For(Driving(1, 0, 0)) == e);
  EXPECT(For(Driving(0, 5, 0)) == e);
}

// A change between forward gears on the move: the accelerate trigger
// shakes for kGearKickMs, the brake stays.
void TestGearKick() {
  const uint32_t ms = pad_feedback::kGearKickMs;
  PadFeedback f;
  const PadEffects plain = f.Update(Driving(1, 10, 1000));
  EXPECT(plain.right.mode == TriggerEffect::kFeedback);  // getting in is no change
  EXPECT(f.Update(Driving(1, 12, 1016)) == plain);       // nor the same gear
  const PadEffects kick = f.Update(Driving(2, 12, 1032));
  EXPECT(kick.right == Effect(TriggerEffect::kVibration, 0, 0, 6, 25));
  EXPECT(kick.right == Normalized(kick.right));
  EXPECT(kick.left == plain.left);
  EXPECT(f.Update(Driving(2, 12, 1032 + ms - 1)).right == kick.right);
  EXPECT(f.Update(Driving(2, 12, 1032 + ms)) == plain);
  EXPECT(f.Update(Driving(2, 12, 5000)) == plain);
  // Down a gear too.
  EXPECT(f.Update(Driving(1, 12, 5016)).right == kick.right);
  EXPECT(f.Update(Driving(1, 12, 5016 + ms)) == plain);
}

void TestNoGearKick() {
  const PadEffects plain = For(Driving(1, 10, 0));
  {  // Into gear 0 and out of it.
    PadFeedback f;
    f.Update(Driving(1, 10, 0));
    EXPECT(f.Update(Driving(0, 10, 16)) == plain);
    EXPECT(f.Update(Driving(1, 10, 32)) == plain);
  }
  {  // Hardly moving.
    PadFeedback f;
    f.Update(Driving(1, 1, 0));
    EXPECT(f.Update(Driving(2, pad_feedback::kGearKickSpeed - 0.5f, 16)) == plain);
    EXPECT(f.Update(Driving(3, pad_feedback::kGearKickSpeed, 32)).right.mode == TriggerEffect::kVibration);
  }
  {  // A change while hardly moving, then speeding up in that gear: the change is past.
    PadFeedback f;
    f.Update(Driving(1, 1, 0));
    EXPECT(f.Update(Driving(2, 2, 16)) == plain);
    EXPECT(f.Update(Driving(2, 10, 32)) == plain);
    EXPECT(f.Update(Driving(2, 20, 48)) == plain);
  }
  {  // Out of one car and into another in a different gear.
    PadFeedback f;
    f.Update(Driving(3, 20, 0));
    f.Update(OnFoot(Weapon::kNone, 0, 16));
    EXPECT(f.Update(Driving(1, 20, 32)) == plain);
  }
  {  // A menu in between.
    PadFeedback f;
    f.Update(Driving(3, 20, 0));
    f.Update(State());
    EXPECT(f.Update(Driving(2, 20, 32)) == plain);
  }
  {  // Getting out during the shake ends it: on foot the trigger is the weapon's.
    PadFeedback f;
    f.Update(Driving(1, 20, 0));
    EXPECT(f.Update(Driving(2, 20, 16)).right.mode == TriggerEffect::kVibration);
    EXPECT(f.Update(OnFoot(Weapon::kPistol, 0, 32)).right == Effect(TriggerEffect::kWeapon, 2, 5, 6, 0));
    EXPECT(f.Update(Driving(2, 20, 48)) == plain);
  }
  {  // The clock wrapping round during the shake.
    PadFeedback f;
    const uint32_t start = 0xFFFFFFF0u;
    f.Update(Driving(1, 20, start - 16));
    EXPECT(f.Update(Driving(2, 20, start)).right.mode == TriggerEffect::kVibration);
    EXPECT(f.Update(Driving(2, 20, start + 40)).right.mode == TriggerEffect::kVibration);  // past 0
    EXPECT(f.Update(Driving(2, 20, start + pad_feedback::kGearKickMs)) == plain);
  }
}

void TestWantedLight() {
  // Not wanted: no colour is asked for, the light stays the pad's own.
  EXPECT(OwnLight(For(OnFoot(Weapon::kNone, 0, 12345))));
  EXPECT(OwnLight(For(Driving(1, 10, 12345, 0))));
  // One star: red for 400 ms, blue for 400, in turn.
  EXPECT(Red(For(OnFoot(Weapon::kNone, 1, 0))));
  EXPECT(Red(For(OnFoot(Weapon::kNone, 1, 399))));
  EXPECT(Blue(For(OnFoot(Weapon::kNone, 1, 400))));
  EXPECT(Blue(For(OnFoot(Weapon::kNone, 1, 799))));
  EXPECT(Red(For(OnFoot(Weapon::kNone, 1, 800))));
  EXPECT(For(OnFoot(Weapon::kNone, 2, 400)) == For(OnFoot(Weapon::kNone, 1, 400)));
  // From three stars every 200 ms.
  EXPECT(Red(For(OnFoot(Weapon::kNone, 3, 199))));
  EXPECT(Blue(For(OnFoot(Weapon::kNone, 3, 200))));
  EXPECT(Red(For(OnFoot(Weapon::kNone, 6, 400))));
  // At the wheel too, with the pedals as they are; and the triggers on foot.
  const PadEffects driving = For(Driving(1, 10, 400, 1));
  EXPECT(Blue(driving));
  EXPECT(driving.left == Effect(TriggerEffect::kFeedback, 1, 0, 4, 0));
  EXPECT(For(OnFoot(Weapon::kRifle, 1, 0)).right == Effect(TriggerEffect::kWeapon, 2, 5, 7, 0));
  // Not in a menu or a cutscene; and once the stars are gone.
  State s = OnFoot(Weapon::kNone, 3, 200);
  s.playing = false;
  EXPECT(OwnLight(For(s)));
  PadFeedback f;
  EXPECT(Red(f.Update(OnFoot(Weapon::kNone, 1, 0))));
  EXPECT(OwnLight(f.Update(OnFoot(Weapon::kNone, 0, 16))));
}

// The frame's step, with a game that counts its reads and a pad that
// keeps what it was handed.
struct Rig {
  PadFeedbackLoop loop;
  State state;
  int reads = 0, sends = 0;
  PadEffects last;

  void Frame(bool on) {
    loop.Frame(
        on,
        [&] {
          reads++;
          return state;
        },
        [&](const PadEffects& e) {
          sends++;
          last = e;
        });
  }
};

void TestOffReadsAndSendsNothing() {
  Rig r;
  r.state = OnFoot(Weapon::kShotgun, 2, 0);
  for (int i = 0; i < 100; i++) r.Frame(false);
  EXPECT(r.reads == 0);
  EXPECT(r.sends == 0);
}

void TestOnReadsEveryFrameAndSendsNews() {
  Rig r;
  r.Frame(true);
  EXPECT(r.reads == 1);
  EXPECT(r.sends == 1);  // the first frame's, whatever they are
  EXPECT(r.last == PadEffects());
  r.Frame(true);
  EXPECT(r.reads == 2);
  EXPECT(r.sends == 1);  // nothing new
  r.state = OnFoot(Weapon::kPistol);
  r.Frame(true);
  EXPECT(r.sends == 2);
  EXPECT(r.last.right == Effect(TriggerEffect::kWeapon, 2, 5, 6, 0));
  r.Frame(true);
  EXPECT(r.sends == 2);
  // Unchanged, they are handed on again every kResendFrames frames.
  const int before = r.sends;
  for (uint32_t i = 0; i < 3 * pad_feedback::kResendFrames; i++) r.Frame(true);
  EXPECT(r.sends == before + 3);
  EXPECT(r.reads == 4 + 3 * int(pad_feedback::kResendFrames));
  EXPECT(pad_feedback::kResendFrames == 30);
}

}  // namespace

int main() {
  TestNothingOutsidePlay();
  TestWeaponSlots();
  TestOnFoot();
  TestDriving();
  TestGearKick();
  TestNoGearKick();
  TestWantedLight();
  TestAimingDoesNotVibrate();
  TestOneKickAShot();
  TestBurst();
  TestNoShotAcrossWeapons();
  TestOffReadsAndSendsNothing();
  TestOnReadsEveryFrameAndSendsNews();
  if (g_failures) return 1;
  std::puts("pad_feedback_test: ok");
  return 0;
}
