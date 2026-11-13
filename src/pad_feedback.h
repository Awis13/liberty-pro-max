// What a DualSense's adaptive triggers and light bar do for what the
// player is doing (GTAIV_PAD_FEEDBACK): the logic alone, with no game and
// no pad. pad_feedback.cpp reads the game into a State once a frame and
// hands the PadEffects to the runtime, which sends them when they change
// (x86recomp's runtime/hle/pad_effects.h has their form).
//
//   Not playing (menus, the pause menu, cutscenes, dead, no control):
//     both triggers free, the light the pad's own.
//   Driving: the accelerate trigger (right) pushes back lightly, the brake
//     (left) more; a change between forward gears on the move shakes the
//     accelerate trigger for a moment. A wreck's triggers are free.
//   On foot with a gun in hand, not shooting: the fire trigger (right)
//     pushes back from early in its travel and then gives way with a
//     click, by the weapon's class. Shooting (as the game says, not the
//     trigger): it vibrates low in its travel instead, below the click:
//     automatic weapons as long as they fire, at about their rate of fire;
//     the others one short kick a shot. Released, it is the click again.
//     Fists, melee and thrown weapons: free.
//   Wanted, while playing: the light flashes red and blue, faster from
//     three stars. Otherwise the light is the pad's own colour, and it is
//     never set before the player is first wanted.
#pragma once

#include <cstdint>

#include "runtime/hle/pad_effects.h"

namespace pad_feedback {

using recomp::hle::PadColour;
using recomp::hle::PadEffects;
using recomp::hle::TriggerEffect;

enum class Weapon { kNone, kPistol, kShotgun, kSmg, kRifle, kSniper, kHeavy };

constexpr const char* WeaponName(Weapon w) {
  constexpr const char* kNames[] = {"none", "pistol", "shotgun", "smg", "rifle", "sniper", "heavy"};
  return kNames[int(w)];
}

// The class of one of the game's weapon slots (GET_WEAPONTYPE_SLOT: 0
// unarmed, 1 melee, 2 handguns, 3 shotguns, 4 submachine guns, 5 rifles,
// 6 sniper rifles, 7 heavy, 8 thrown, then special ones).
constexpr Weapon WeaponOfSlot(int slot) {
  switch (slot) {
    case 2: return Weapon::kPistol;
    case 3: return Weapon::kShotgun;
    case 4: return Weapon::kSmg;
    case 5: return Weapon::kRifle;
    case 6: return Weapon::kSniper;
    case 7: return Weapon::kHeavy;
    default: return Weapon::kNone;
  }
}

struct State {
  bool playing = false;  // in the game with control of the player
  bool driving = false;  // at the wheel of a vehicle that still drives
  int gear = 0;          // driving: the game's gear number; from 1 the forward gears
  float speed = 0;       // driving: metres a second
  Weapon weapon = Weapon::kNone;  // on foot: in hand
  int weapon_type = 0;   // on foot: the game's weapon type (a change is a new weapon)
  int clip = -1;         // on foot: rounds in its clip, -1 if not known
  bool shooting = false; // on foot: the game's "shooting" flag for the player
  int wanted = 0;        // stars
  uint32_t ms = 0;       // a clock that keeps running
};

constexpr PadColour kRed = {255, 0, 0}, kBlue = {0, 0, 255};
constexpr uint32_t kFlashMs = 400, kFlashFastMs = 200;  // each colour's time
constexpr int kFlashFastFrom = 3;                        // stars
constexpr uint32_t kGearKickMs = 120;
constexpr float kGearKickSpeed = 3.0f;  // no kick below it (pulling away)

constexpr TriggerEffect Resist(uint8_t start, uint8_t strength) {
  return {TriggerEffect::kFeedback, start, 0, strength, 0};
}
constexpr TriggerEffect Break(uint8_t start, uint8_t end, uint8_t strength) {
  return {TriggerEffect::kWeapon, start, end, strength, 0};
}

constexpr TriggerEffect kAccelerate = Resist(1, 2);
constexpr TriggerEffect kBrake = Resist(1, 4);
constexpr TriggerEffect kGearKick = {TriggerEffect::kVibration, 0, 0, 6, 25};

// The fire trigger before a shot, for a weapon class: resistance from
// zone 2 (the earliest a click effect takes) up to the click.
constexpr TriggerEffect FireTrigger(Weapon w) {
  switch (w) {
    case Weapon::kPistol: return Break(2, 5, 6);
    case Weapon::kSmg: return Break(2, 4, 5);
    case Weapon::kRifle: return Break(2, 5, 7);
    case Weapon::kShotgun: return Break(2, 6, 8);
    case Weapon::kSniper: return Break(2, 6, 7);
    case Weapon::kHeavy: return Break(2, 7, 8);
    default: return {};
  }
}

// Whether a class fires for as long as the trigger is held.
constexpr bool Automatic(Weapon w) { return w == Weapon::kSmg || w == Weapon::kRifle; }

// The fire trigger while shooting: vibration from zone 1 on, below every
// click. Automatic weapons: as long as they fire, at a frequency near
// their rate of fire (a value for the class: the weapons' own rates are
// not read). The others: a kick of kKickMs(w) for each shot.
constexpr uint8_t kRecoilStart = 1;
constexpr TriggerEffect Recoil(Weapon w) {
  switch (w) {
    case Weapon::kSmg: return {TriggerEffect::kVibration, kRecoilStart, 0, 6, 15};
    case Weapon::kRifle: return {TriggerEffect::kVibration, kRecoilStart, 0, 8, 10};
    case Weapon::kPistol: return {TriggerEffect::kVibration, kRecoilStart, 0, 6, 30};
    case Weapon::kShotgun: return {TriggerEffect::kVibration, kRecoilStart, 0, 8, 20};
    case Weapon::kSniper: return {TriggerEffect::kVibration, kRecoilStart, 0, 8, 20};
    case Weapon::kHeavy: return {TriggerEffect::kVibration, kRecoilStart, 0, 8, 20};
    default: return {};
  }
}
// A single shot's kick, ms.
constexpr uint32_t KickMs(Weapon w) {
  switch (w) {
    case Weapon::kPistol: return 70;
    case Weapon::kShotgun:
    case Weapon::kSniper: return 120;
    case Weapon::kHeavy: return 150;
    default: return 0;
  }
}
// An automatic weapon counts as still firing this long after its last
// shot: longer than the time between two of its shots.
constexpr uint32_t kBurstHoldMs = 150;

// What the fire trigger is doing (for the log).
enum class Fire { kNone, kReady, kBurst, kKick };
constexpr const char* FireName(Fire f) {
  constexpr const char* kNames[] = {"none", "ready", "burst", "kick"};
  return kNames[int(f)];
}

class PadFeedback {
 public:
  // The effects for the state now; call it once a frame, as the gear
  // change and a shot are seen between two calls.
  // What the fire trigger did at the last Update.
  Fire fire() const { return fire_; }

  PadEffects Update(const State& s) {
    PadEffects e;
    if (!s.playing) {
      ForgetGear();
      ForgetFire();
      return e;
    }
    if (s.wanted > 0) {
      const uint32_t each = s.wanted >= kFlashFastFrom ? kFlashFastMs : kFlashMs;
      e.light = true;
      e.colour = (s.ms / each) % 2 ? kBlue : kRed;
    }
    if (!s.driving) {
      ForgetGear();
      e.right = FireTriggerNow(s);
      return e;
    }
    ForgetFire();
    if (have_gear_ && s.gear != gear_ && s.gear >= 1 && gear_ >= 1 && s.speed >= kGearKickSpeed) {
      kicking_ = true;
      kick_from_ = s.ms;
    }
    if (kicking_ && s.ms - kick_from_ >= kGearKickMs) kicking_ = false;
    have_gear_ = true;
    gear_ = s.gear;
    e.right = kicking_ ? kGearKick : kAccelerate;
    e.left = kBrake;
    return e;
  }

 private:
  // The fire trigger on foot. A shot is the game's shooting flag coming on,
  // or the clip of the same weapon going down (each round, so each shot of
  // a single-shot weapon is one kick whatever the flag does between them).
  TriggerEffect FireTriggerNow(const State& s) {
    if (s.weapon == Weapon::kNone) {
      ForgetFire();
      return {};
    }
    if (!have_weapon_ || s.weapon_type != weapon_type_) {
      ForgetFire();
      have_weapon_ = true;
      weapon_type_ = s.weapon_type;
    } else if ((s.shooting && !shooting_) || (clip_ >= 0 && s.clip >= 0 && s.clip < clip_)) {
      shot_ = true;
      shot_at_ = s.ms;
    }
    shooting_ = s.shooting;
    clip_ = s.clip;
    const uint32_t since = s.ms - shot_at_;
    if (Automatic(s.weapon) ? s.shooting || (shot_ && since < kBurstHoldMs) : shot_ && since < KickMs(s.weapon)) {
      fire_ = Automatic(s.weapon) ? Fire::kBurst : Fire::kKick;
      return Recoil(s.weapon);
    }
    shot_ = false;
    fire_ = Fire::kReady;
    return FireTrigger(s.weapon);
  }
  void ForgetGear() {
    have_gear_ = false;
    kicking_ = false;
  }
  void ForgetFire() {
    have_weapon_ = shot_ = shooting_ = false;
    clip_ = -1;
    fire_ = Fire::kNone;
  }

  bool have_gear_ = false, kicking_ = false;
  int gear_ = 0;
  uint32_t kick_from_ = 0;
  bool have_weapon_ = false, shot_ = false, shooting_ = false;
  int weapon_type_ = 0, clip_ = -1;
  uint32_t shot_at_ = 0;
  Fire fire_ = Fire::kNone;
};

// How often the effects are handed on though they have not changed, in
// frames: a pad that has just connected, or refused them, gets them then.
constexpr uint32_t kResendFrames = 30;

// The frame's step: with the option off nothing is read and nothing
// handed on; on, the state is read once and the effects handed on when
// they have changed, and every kResendFrames.
class PadFeedbackLoop {
 public:
  const PadFeedback& feedback() const { return feedback_; }

  template <class Read, class Send>
  void Frame(bool on, Read&& read, Send&& send) {
    if (!on) return;
    const PadEffects e = feedback_.Update(read());
    const bool due = ++frames_ % kResendFrames == 0;
    if (any_ && e == sent_ && !due) return;
    any_ = true;
    sent_ = e;
    send(e);
  }

 private:
  PadFeedback feedback_;
  PadEffects sent_;
  bool any_ = false;
  uint32_t frames_ = 0;
};

}  // namespace pad_feedback
