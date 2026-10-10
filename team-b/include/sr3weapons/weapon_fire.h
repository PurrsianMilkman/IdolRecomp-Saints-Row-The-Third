#pragma once

// sr3weapons/weapon_fire.h - equip-time hitscan fire dispatch (this task,
// 2026-10-08). Built against spec-weapons-runtime.md Sec2.2a-Sec2.2c (the
// vtable-resolved fire dispatch chain, `weapon::+0x8c` = `0x00b86030`) and
// Sec2.3 (the melee/AoE sphere-cast hit-resolution function, confirmed to
// reach the shared damage sink `0x0096fa20` via a shared sphere-cast
// wrapper and the same `0x0067b050`/`-1.0` instant-lethal shape
// `character_kill`, spec-lua-api-behaviour.md Sec3.15, already uses).
//
// WHAT WAS ALREADY IN THIS PROJECT, BEFORE THIS TASK: nothing. A grep for
// `Weapon_Class`/fire/ammo/refire runtime code found only the weapons.xtbl
// DATA reader (sr3tables_weapons::Weapon, include/sr3tables_weapons/
// weapons.h) - no runtime weapon class, no fire dispatch, no ammo/refire
// logic anywhere. HANDOFF.md's own queued-item history says so directly:
// "Hitscan fire call site - still OPEN per spec, not yet chased." So there
// was no pre-existing single-class-vs-polymorphism divergence to fix -
// WeaponInstance below is a from-scratch build, modeled as the engine's own
// single concrete `weapon` class from the start (CONFIRMED, Sec2.2c: ONE
// class for every Weapon_Class category - pistol/rifle/shotgun/melee alike
// - category differences are entirely DATA-driven through Weapon_Class on
// the weapon definition, confirmed by a broad RTTI sweep finding zero
// further-derived classes).
//
// THE CONFIRMED DISPATCH CHAIN THIS MODELS (Sec2.2c, `0x00b86030`-
// `0x00b86107`, then `item::+0x8c` = `0x00902760`), in the real body's own
// order:
//   1. a vehicle-weapon short-circuit: `Weapon_Class == 0x16` aborts
//      immediately - vehicle-mounted weapons never fire through this path
//      at all (a separate, different mechanism, out of this task's scope).
//   2. a "refuse" gate (`weapon::+0x88` = `0x00b81390`, else tail-calls
//      `item::+0x88`) - the real trigger condition is explicitly left OPEN
//      by spec itself; modeled here as a seam that CHOOSES to pass through
//      by default (setRefuseGateTripped() lets a test flip it).
//   3. delegation into `item::+0x8c` (`0x00902760`), whose own CONFIRMED
//      body (minus the parts out of this task's scope - generic listener-
//      notify broadcasts, equip-status bitwork, the gated aim-lead ray
//      block, muzzle FX/particle spawn, none of which call the damage sink
//      per spec's own text) unconditionally arms 3 generic timer-primitive
//      (`0x00d9e140`) writes on the weapon INSTANCE (`this+0xf4`/`+0xfc`/
//      `+0x104`) - `+0xfc` set to a literal `1000` ms, "a strong HIGH-
//      CONFIDENCE candidate for the real per-shot refire-delay gate" (NOT
//      independently CONFIRMED as the live gate value - implemented as the
//      working value per this task's own instruction, since it is the best
//      evidence available).
//   4. weapon-specific sound/FX dispatch (`0x009ba4a0`): dry-fire vs normal
//      hashed sound selection, keyed by whether ammo is available.
//   5. the literal rounds-in-clip decrement (weapon-instance `this+0x1ac`,
//      clamped at zero; saturated to `0x7fffffff` for "infinite ammo",
//      `this+0x1a0` bit `0x40`), sourced from a per-character, per-ammo-
//      type pool keyed by `weapon_def+0x110` (the SAME field identity
//      spec-lua-api-behaviour.md Sec3.15/Sec20.32 already use for the
//      `Ammo` name, per Sec2.2c's own "a confirmed third consumer/firmer
//      identity for that field" note).
//
// THE GENUINELY OPEN PART, NOT INVENTED: for an UN-assisted shot (the
// ordinary trigger-pull - no pre-existing locked target), spec's own traced
// call chain finds NO ray-cast and NO damage call anywhere in the path -
// the only ray-cast in `item::+0x8c` is gated behind an aim-assist/target-
// lead condition that requires a target handle to ALREADY be non-null, so
// it is not how an ordinary shot produces a hit.
//
// UPDATE 2026-10-09 (spec-weapons-runtime.md Sec4.4, Team A spec sync
// `c9ab790`): the damage sink's own CALLER census is now STATICALLY
// EXHAUSTED - 75 call sites across 58 functions, with no vtable installs,
// and NONE of them is hitscan-shaped. The real path is MOST LIKELY a
// projectile/ray Havok contact callback - consistent with, not
// contradicting, the two untested hypotheses this file originally gave.
// Static tracing alone will not find it (a Havok contact callback is
// async/outside any statically-traced synchronous call chain, invisible to
// a static call-graph census) - Team A is preparing an optional, read-only
// LIVE capture that could still find the real call site, so this is NOT
// closed permanently, just closed for further STATIC tracing.
// CastHitscanRay()/IWeaponDamageSink below are CHOSEN, with the static
// search exhausted (Sec4.4); the real path is likely a physics contact
// callback, pending a possible live capture. Not claimed as the traced
// mechanism. They connect this now-CONFIRMED
// dispatch chain to this project's own EXISTING raycast primitive
// (sr3player::RayVsTriangle3D, already used for player-world collision and
// AI line-of-sight elsewhere in this project) and an injectable damage-
// application seam, so "fire -> hit -> damage" actually works end to end
// for gameplay/playtest purposes. See each symbol's own doc comment below
// for the exact boundary.
//
// LAYERING NOTE: this module depends only on sr3tables_weapons (the
// weapons.xtbl row type) and sr3player (Vec3, RayVsTriangle3D) - the same
// tier as sr3airuntime/sr3physics, none of which depend on sr3luahost. It
// is deliberately NOT wired to sr3luahost's CharacterState/character_kill
// machinery here: every other "pure machinery, injectable seams" runtime
// module in this project (sr3physics, sr3animruntime, sr3playeranim) is
// built the same way, standing ready for a future game-loop/Lua-host
// integration rather than reaching upward into it itself. IWeaponDamageSink
// is the seam a future adapter (e.g. one that calls the SAME deterministic
// lethal-hit model stub_character_kill already implements) would implement.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sr3player/character_collision.h"  // sr3player::RayVsTriangle3D - the CHOSEN raycast primitive, see CastHitscanRay below
#include "sr3player/movement.h"             // sr3player::Vec3/operators
#include "sr3tables_weapons/weapons.h"       // sr3tables_weapons::Weapon - the one weapons.xtbl row type this engine's one weapon class reads

namespace sr3weapons {

using sr3player::Vec3;

// Weapon_Class value for vehicle-mounted weapons (spec-tables-weapons-
// combat.md Sec2.5's 23-name enum; sr3tables_weapons::kWeaponClassNames[22]
// == "vehicle"). CONFIRMED (spec-weapons-runtime.md Sec2.2c): this class
// never fires through the weapon::+0x8c dispatch at all.
inline constexpr int32_t kVehicleWeaponClass = 0x16;

// HIGH-CONFIDENCE, not CONFIRMED (spec-weapons-runtime.md Sec2.2c) - see
// this file's own top doc comment, dispatch step 3. This is a DIFFERENT
// candidate from Sec3.1's own holder+0x134 timer (fed from a weapon-
// definition-derived 16-bit value; Sec3.1 itself leaves OPEN whether that
// field is a cached copy of Time_Management.Refire_Delay or a distinct
// one) - this module implements the instance-level +0xfc candidate because
// it is the one Sec2.2c ties directly to the now-CONFIRMED dispatch body,
// per this task's own explicit instruction to implement 1000ms as the
// working value.
inline constexpr double kRefireDelayMs = 1000.0;

// -----------------------------------------------------------------------
// FireTimer - the generic engine-wide timer-primitive stand-in (spec
// Sec3.1: 0x00d9e4c0 "has a value" / 0x00d9e210 "get value" / 0x00d9e140
// "set value"). One instance per weapon-instance timer slot modeled below.
// -----------------------------------------------------------------------
class FireTimer {
public:
    void set(double valueMs) {
        hasValue_ = true;
        valueMs_ = valueMs;
    }
    void clear() {
        hasValue_ = false;
        valueMs_ = 0.0;
    }
    bool hasValue() const { return hasValue_; }
    double get() const { return valueMs_; }

private:
    bool hasValue_ = false;
    double valueMs_ = 0.0;
};

// -----------------------------------------------------------------------
// HitscanTarget - a candidate hit target for the CHOSEN hitscan wiring
// (see this file's top doc comment for why the real base hit-detection call
// site is genuinely OPEN, not reproduced here). Deliberately minimal: this
// is not a character/vehicle model (out of scope for this task). `center`/
// `halfExtent` describe a simple axis-aligned box as a stand-in for real
// per-target collision geometry (a `.clmesh_pc`/ragdoll-capsule shape in
// the real engine) - CHOSEN purely so CastHitscanRay() has concrete
// geometry to test sr3player::RayVsTriangle3D against.
// -----------------------------------------------------------------------
struct HitscanTarget {
    std::string name;
    Vec3 center;
    double halfExtent = 0.5;
    // CHOSEN stand-in for the real "valid target" predicate (spec Sec2.1's
    // shared `0x0091f5e0` check; Sec2.4 corroborates the SAME predicate
    // gates melee/hitscan-targeting/vehicle-destruction alike). A caller
    // sets this false for an already-dead/invalid candidate; this module
    // does not independently reproduce `0x0091f5e0`.
    bool isValidTarget = true;
};

struct HitscanHit {
    std::string targetName;
    double fraction = 1.0;  // along [origin, origin + direction_normalized*maxRange], in [0,1]
    Vec3 position;
    Vec3 normal;
};

// CHOSEN raycast helper - reuses sr3player::RayVsTriangle3D (the SAME
// shared 3D ray primitive sr3player/character_collision.h's own doc
// comment says is used for player-world collision, and
// sr3airuntime/line_of_sight.h's ClosestHitRayVsTriangles reuses for AI
// line-of-sight) rather than inventing a new geometry test. Builds a
// 12-triangle axis-aligned box per valid candidate target and returns the
// globally closest hit across all of them (the same "closest hit wins"
// shape ClosestHitRayVsTriangles already uses). Returns std::nullopt if
// `direction` is ~zero-length, `maxRange` <= 0, or nothing is hit.
std::optional<HitscanHit> CastHitscanRay(const Vec3& origin, const Vec3& direction, double maxRange,
                                          const std::vector<HitscanTarget>& targets);

// -----------------------------------------------------------------------
// IWeaponDamageSink - the injectable seam standing in for this engine's
// generic damage-application routine (`0x0096fa20`). Same house pattern as
// sr3player::ICollisionWorld / sr3airuntime::ILineOfSightWorld: a lower-
// tier gameplay module defines the seam; a higher-tier caller supplies the
// concrete implementation (see this file's top doc comment, LAYERING NOTE).
// -----------------------------------------------------------------------
class IWeaponDamageSink {
public:
    virtual ~IWeaponDamageSink() = default;
    // Models the SAME deterministic lethal-hit shape this project's own
    // existing damage-application entry point already uses (spec-lua-api-
    // behaviour.md Sec3.15's character_kill: a fixed -1.0 lethal constant
    // through the shared damage routine, "guaranteed death via the normal
    // pipeline, not a possibly-survivable hit"; spec-weapons-runtime.md
    // Sec2.3 independently corroborates the identical -1.0/instant-lethal
    // shape for a melee/AoE weapon hit through the SAME damage sink) - not
    // a partial/variable-amount damage call (this project's own
    // character_damage native already documents that the authoritative
    // variable-amount branch is genuinely OPEN - its modifier chain is not
    // re-derivable from static reading alone, src/lua_spec_confirmed_
    // stubs_b15_wave2_core_dispatchers.cpp's own stub_character_damage).
    virtual void applyLethalHit(const std::string& targetName) = 0;
};

// CHOSEN opt-in test/diagnostic stand-in: records every call, applies
// nothing further. Same role as sr3player::EmptyCollisionWorld /
// sr3airuntime::NoHitLineOfSightWorld.
class RecordingDamageSink : public IWeaponDamageSink {
public:
    void applyLethalHit(const std::string& targetName) override {
        lastTarget = targetName;
        ++callCount;
    }
    std::string lastTarget;
    uint64_t callCount = 0;
};

// -----------------------------------------------------------------------
// WeaponInstance - this engine's one concrete runtime `weapon` C++ class
// (spec-weapons-runtime.md Sec2.2c). See this file's top doc comment for
// the full confirmed/HIGH-CONFIDENCE/CHOSEN breakdown this class
// implements.
// -----------------------------------------------------------------------
class WeaponInstance {
public:
    // `def` must outlive this instance (the same non-owning-reference
    // convention this project's other xtbl-row consumers already use).
    // Seeds roundsInClip from def.magazineSize (0 if absent) and
    // infiniteAmmo from the weapon definition's own authored "infinite
    // magazine capacity"/"unlimited ammo" flags (spec-tables-weapons-
    // combat.md Sec2.4, kFlagsAWordNames indices 1/3) - a reasonable but
    // NOT independently confirmed mapping to the real runtime
    // `this+0x1a0` bit `0x40` spec Sec2.2c actually documents; override
    // directly (setInfiniteAmmo/setRoundsInClip) for a test that needs a
    // specific value.
    explicit WeaponInstance(const sr3tables_weapons::Weapon& def);

    void setRoundsInClip(int32_t rounds) { roundsInClip_ = rounds; }
    int32_t roundsInClip() const { return roundsInClip_; }
    void setInfiniteAmmo(bool infinite) { infiniteAmmo_ = infinite; }
    bool infiniteAmmo() const { return infiniteAmmo_; }

    // Lets a test trip the refuse-gate seam (dispatch step 2 - OPEN real
    // trigger condition, CHOSEN pass-through default).
    void setRefuseGateTripped(bool tripped) { refuseGateTripped_ = tripped; }

    enum class FireSoundSelection { kNone, kNormal, kDryFire };

    struct FireOutcome {
        bool refireGateBlocked = false;      // the kRefireDelayMs gate was still active - nothing else below ran
        bool vehicleShortCircuited = false;  // Weapon_Class == kVehicleWeaponClass - CONFIRMED, dispatch never reached
        bool refused = false;                // the refuse-gate seam tripped - OPEN real condition
        bool reachedItemUse = false;         // item::+0x8c was entered (timers armed below)
        FireSoundSelection soundSelected = FireSoundSelection::kNone;
        bool roundConsumed = false;
        int32_t roundsInClipAfter = 0;
        bool hitDetected = false;  // CHOSEN wiring - see IWeaponDamageSink's own doc comment
        std::string hitTargetName;
        bool damageApplied = false;  // CHOSEN wiring
    };

    // One fire attempt. `nowMs` is caller-supplied simulation time (this
    // module has no clock of its own - same injected-input discipline as
    // sr3player::PlayerMovement's own `desiredVelocity` parameter).
    // `origin`/`direction` are the weapon's own aim ray for the CHOSEN
    // hit-detection wiring (this file's top doc comment); `maxRange`
    // should ordinarily be the weapon definition's own Range_Max
    // (sr3tables_weapons::Weapon::rangeMax.value) unless a caller has a
    // specific reason to override it.
    FireOutcome fire(double nowMs, const Vec3& origin, const Vec3& direction, double maxRange,
                      const std::vector<HitscanTarget>& targets, IWeaponDamageSink* damageSink);

    // Read-only access to the 3 timer-primitive writes dispatch step 3
    // arms, for test/diagnostic verification that all 3 are always touched
    // together (spec Sec2.2c: "unconditionally, regardless of the aim-lead
    // gate").
    const FireTimer& timerF4() const { return timerF4_; }
    const FireTimer& timerFc() const { return timerFc_; }
    const FireTimer& timer104() const { return timer104_; }

private:
    const sr3tables_weapons::Weapon& def_;
    int32_t roundsInClip_ = 0;
    bool infiniteAmmo_ = false;
    bool refuseGateTripped_ = false;
    double nextAllowedFireMs_ = 0.0;
    // timerF4_/timer104_: armed (set()) for fidelity to spec's "3 generic
    // timer-primitive writes", but their real written value is OPEN - 0.0
    // is a deterministic placeholder (the same "0 stand-in, not the
    // engine's real value" convention sr3xtbl::Always<T> already
    // documents project-wide), not a reproduced value. timerFc_'s 1000ms
    // value alone is the HIGH-CONFIDENCE cited one (kRefireDelayMs).
    FireTimer timerF4_;
    FireTimer timerFc_;
    FireTimer timer104_;
};

}  // namespace sr3weapons
