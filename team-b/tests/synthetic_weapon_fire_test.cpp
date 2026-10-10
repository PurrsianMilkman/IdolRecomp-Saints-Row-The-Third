// Synthetic tests for sr3weapons (include/sr3weapons/weapon_fire.h),
// covering the equip-time hitscan fire dispatch chain this task builds
// against spec-weapons-runtime.md Sec2.2a-Sec2.2c/Sec2.3. Every fixture
// below is a hand-built sr3tables_weapons::Weapon row (no real weapons.xtbl
// data needed - the dispatch logic is independent of any specific weapon's
// authored content).
//
// Style follows this project's synthetic-suite convention (see
// tests/synthetic_tables_weapons_test.cpp, tests/synthetic_xtbl_test.cpp): a
// tiny hand-rolled CHECK macro, a running pass count, "<N> tests passed."
// on the last line.
//
// Mutation-check note (this project's process, measurement-discipline.md):
// 3 of the CHECKs below were confirmed to actually catch a broken
// implementation by hand, before this file was finalised:
//   1. testAmmoDecrementClampsAtZero's clamp check: temporarily changed
//      src/weapon_fire.cpp's decrement to a plain subtract with no floor
//      (`roundsInClip_ -= perShot;`) -> `CHECK(out2.roundsInClipAfter == 0)`
//      failed (went negative, -1) as expected; reverted.
//   2. testVehicleWeaponShortCircuit's "nothing else ran" check: temporarily
//      removed the early `return out;` after setting
//      `vehicleShortCircuited = true` -> `CHECK(!out.reachedItemUse)`
//      failed (fell through into the refuse/item-use/timer logic) as
//      expected; reverted.
//   3. testRefireDelayGateBlocksImmediateRefire's gate check: temporarily
//      changed the gate comparison from `nowMs < nextAllowedFireMs_` to
//      `nowMs <= 0` -> `CHECK(blocked.refireGateBlocked)` at nowMs=500
//      failed (fire proceeded instead of being gated) as expected;
//      reverted.

#include <iostream>
#include <string>

#include "sr3weapons/weapon_fire.h"

namespace {

int g_passed = 0;
int g_failed = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (cond) {                                                                     \
            ++g_passed;                                                                 \
        } else {                                                                        \
            ++g_failed;                                                                 \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":" << __LINE__ << "\n"; \
        }                                                                               \
    } while (0)

using namespace sr3weapons;

sr3tables_weapons::Weapon MakeRifleDef() {
    sr3tables_weapons::Weapon w;
    w.name = "test_rifle";
    w.weaponClass = 2;  // "rifle" (kWeaponClassNames index 2)
    w.magazineSize.present = true;
    w.magazineSize.value = 5;
    w.ammoPerShot.present = true;
    w.ammoPerShot.value = 1;
    w.ammo = "Rifle_Ammo";
    w.rangeMax.present = true;
    w.rangeMax.value = 100.0f;
    return w;
}

sr3tables_weapons::Weapon MakeVehicleWeaponDef() {
    sr3tables_weapons::Weapon w;
    w.name = "test_vehicle_weapon";
    w.weaponClass = sr3weapons::kVehicleWeaponClass;  // 0x16
    w.magazineSize.present = true;
    w.magazineSize.value = 999;
    return w;
}

// ---------------------------------------------------------------------------
// Vehicle-weapon short-circuit (CONFIRMED, spec Sec2.2c).
// ---------------------------------------------------------------------------
void testVehicleWeaponShortCircuit() {
    sr3tables_weapons::Weapon def = MakeVehicleWeaponDef();
    WeaponInstance w(def);
    RecordingDamageSink sink;
    std::vector<HitscanTarget> targets;

    WeaponInstance::FireOutcome out =
        w.fire(0.0, Vec3{0, 0, 0}, Vec3{0, 0, 1}, 100.0, targets, &sink);

    CHECK(out.vehicleShortCircuited);
    CHECK(!out.reachedItemUse);
    CHECK(!out.refused);
    CHECK(out.soundSelected == WeaponInstance::FireSoundSelection::kNone);
    CHECK(!out.roundConsumed);
    CHECK(w.roundsInClip() == 999);  // untouched
    CHECK(!w.timerFc().hasValue());  // timers never armed
    CHECK(sink.callCount == 0);
}

// ---------------------------------------------------------------------------
// Refuse gate (OPEN real condition, CHOSEN pass-through default / trip).
// ---------------------------------------------------------------------------
void testRefuseGateTripped() {
    sr3tables_weapons::Weapon def = MakeRifleDef();
    WeaponInstance w(def);
    w.setRefuseGateTripped(true);
    RecordingDamageSink sink;
    std::vector<HitscanTarget> targets;

    WeaponInstance::FireOutcome out = w.fire(0.0, Vec3{0, 0, 0}, Vec3{0, 0, 1}, 100.0, targets, &sink);

    CHECK(out.refused);
    CHECK(!out.vehicleShortCircuited);
    CHECK(!out.reachedItemUse);
    CHECK(w.roundsInClip() == 5);  // untouched
    CHECK(!w.timerFc().hasValue());
}

void testRefuseGateDefaultPassesThrough() {
    sr3tables_weapons::Weapon def = MakeRifleDef();
    WeaponInstance w(def);
    RecordingDamageSink sink;
    std::vector<HitscanTarget> targets;

    WeaponInstance::FireOutcome out = w.fire(0.0, Vec3{0, 0, 0}, Vec3{0, 0, 1}, 100.0, targets, &sink);

    CHECK(!out.refused);
    CHECK(out.reachedItemUse);
}

// ---------------------------------------------------------------------------
// Normal fire: ammo decrement, sound selection, the 3 timer-primitive
// writes (spec Sec2.2c).
// ---------------------------------------------------------------------------
void testNormalFireDecrementsAmmoAndArmsTimers() {
    sr3tables_weapons::Weapon def = MakeRifleDef();
    WeaponInstance w(def);
    RecordingDamageSink sink;
    std::vector<HitscanTarget> targets;

    CHECK(w.roundsInClip() == 5);  // seeded from Magazine_Size

    WeaponInstance::FireOutcome out = w.fire(0.0, Vec3{0, 0, 0}, Vec3{0, 0, 1}, 100.0, targets, &sink);

    CHECK(out.reachedItemUse);
    CHECK(out.soundSelected == WeaponInstance::FireSoundSelection::kNormal);
    CHECK(out.roundConsumed);
    CHECK(out.roundsInClipAfter == 4);
    CHECK(w.roundsInClip() == 4);
    CHECK(!out.hitDetected);  // no targets supplied
    CHECK(!out.damageApplied);
    CHECK(sink.callCount == 0);

    // All 3 generic timer-primitive writes armed together, unconditionally.
    CHECK(w.timerF4().hasValue());
    CHECK(w.timerFc().hasValue());
    CHECK(w.timer104().hasValue());
    CHECK(w.timerFc().get() == kRefireDelayMs);
}

// ---------------------------------------------------------------------------
// Ammo decrement respects Ammo_per_Shot and clamps at zero (spec Sec2.2c).
// ---------------------------------------------------------------------------
void testAmmoDecrementClampsAtZero() {
    sr3tables_weapons::Weapon def = MakeRifleDef();
    def.ammoPerShot.value = 2;  // 2 rounds consumed per shot
    WeaponInstance w(def);
    w.setRoundsInClip(3);

    WeaponInstance::FireOutcome out1 = w.fire(0.0, Vec3{0, 0, 0}, Vec3{0, 0, 1}, 100.0, {}, nullptr);
    CHECK(out1.roundConsumed);
    CHECK(out1.roundsInClipAfter == 1);  // 3 - 2 = 1

    WeaponInstance::FireOutcome out2 = w.fire(2000.0, Vec3{0, 0, 0}, Vec3{0, 0, 1}, 100.0, {}, nullptr);
    CHECK(out2.roundConsumed);
    CHECK(out2.roundsInClipAfter == 0);  // 1 - 2 clamps at 0, never negative
    CHECK(out2.soundSelected == WeaponInstance::FireSoundSelection::kNormal);  // still had 1 round when the shot started
}

// ---------------------------------------------------------------------------
// Dry-fire vs normal sound selection, keyed by ammo availability (spec
// Sec2.2c). A dry-fire attempt does not decrement ammo or run hit-detection.
// ---------------------------------------------------------------------------
void testDryFireWhenEmpty() {
    sr3tables_weapons::Weapon def = MakeRifleDef();
    WeaponInstance w(def);
    w.setRoundsInClip(0);
    RecordingDamageSink sink;
    std::vector<HitscanTarget> targets = {HitscanTarget{"dummy", Vec3{0, 0, 10}, 1.0, true}};

    WeaponInstance::FireOutcome out = w.fire(0.0, Vec3{0, 0, 0}, Vec3{0, 0, 1}, 100.0, targets, &sink);

    CHECK(out.reachedItemUse);
    CHECK(out.soundSelected == WeaponInstance::FireSoundSelection::kDryFire);
    CHECK(!out.roundConsumed);
    CHECK(out.roundsInClipAfter == 0);
    CHECK(!out.hitDetected);
    CHECK(!out.damageApplied);
    CHECK(sink.callCount == 0);
    // Timers still arm - "unconditionally, every fire attempt" per spec.
    CHECK(w.timerFc().hasValue());
}

// ---------------------------------------------------------------------------
// Infinite ammo saturates rather than decrementing, and never dry-fires
// (spec Sec2.2c: this+0x1a0 bit 0x40, saturates to 0x7fffffff).
// ---------------------------------------------------------------------------
void testInfiniteAmmoSaturatesAndNeverDryFires() {
    sr3tables_weapons::Weapon def = MakeRifleDef();
    WeaponInstance w(def);
    w.setRoundsInClip(0);
    w.setInfiniteAmmo(true);

    WeaponInstance::FireOutcome out = w.fire(0.0, Vec3{0, 0, 0}, Vec3{0, 0, 1}, 100.0, {}, nullptr);

    CHECK(out.soundSelected == WeaponInstance::FireSoundSelection::kNormal);
    CHECK(out.roundConsumed);
    CHECK(out.roundsInClipAfter == 0x7fffffff);
    CHECK(w.roundsInClip() == 0x7fffffff);
}

void testInfiniteAmmoDefaultSeededFromFlags() {
    sr3tables_weapons::Weapon def = MakeRifleDef();
    def.flagsA = (1u << 3);  // "unlimited ammo" (kFlagsAWordNames index 3)
    WeaponInstance w(def);
    CHECK(w.infiniteAmmo());

    sr3tables_weapons::Weapon def2 = MakeRifleDef();
    def2.flagsA = (1u << 1);  // "infinite magazine capacity" (index 1)
    WeaponInstance w2(def2);
    CHECK(w2.infiniteAmmo());

    sr3tables_weapons::Weapon def3 = MakeRifleDef();
    def3.flagsA = (1u << 7);  // unrelated bit
    WeaponInstance w3(def3);
    CHECK(!w3.infiniteAmmo());
}

// ---------------------------------------------------------------------------
// Refire-delay gate (HIGH-CONFIDENCE, 1000ms, spec Sec2.2c).
// ---------------------------------------------------------------------------
void testRefireDelayGateBlocksImmediateRefire() {
    sr3tables_weapons::Weapon def = MakeRifleDef();
    WeaponInstance w(def);

    WeaponInstance::FireOutcome first = w.fire(0.0, Vec3{0, 0, 0}, Vec3{0, 0, 1}, 100.0, {}, nullptr);
    CHECK(first.reachedItemUse);
    CHECK(!first.refireGateBlocked);

    WeaponInstance::FireOutcome blocked = w.fire(500.0, Vec3{0, 0, 0}, Vec3{0, 0, 1}, 100.0, {}, nullptr);
    CHECK(blocked.refireGateBlocked);
    CHECK(!blocked.reachedItemUse);
    CHECK(!blocked.roundConsumed);
    CHECK(w.roundsInClip() == 4);  // unchanged by the blocked attempt

    WeaponInstance::FireOutcome allowedAgain = w.fire(1000.0, Vec3{0, 0, 0}, Vec3{0, 0, 1}, 100.0, {}, nullptr);
    CHECK(!allowedAgain.refireGateBlocked);
    CHECK(allowedAgain.reachedItemUse);
    CHECK(allowedAgain.roundConsumed);
    CHECK(w.roundsInClip() == 3);
}

// ---------------------------------------------------------------------------
// CHOSEN hit-detection wiring: a ray-hit on a valid target reaches the
// damage-application call; a miss does not (this task's own required test
// coverage).
// ---------------------------------------------------------------------------
void testHitOnValidTargetReachesDamageSink() {
    sr3tables_weapons::Weapon def = MakeRifleDef();
    WeaponInstance w(def);
    RecordingDamageSink sink;
    // Directly ahead, within range and within the target's own half-extent box.
    std::vector<HitscanTarget> targets = {HitscanTarget{"enemy_01", Vec3{0, 0, 10}, 1.0, true}};

    WeaponInstance::FireOutcome out = w.fire(0.0, Vec3{0, 0, 0}, Vec3{0, 0, 1}, 100.0, targets, &sink);

    CHECK(out.roundConsumed);
    CHECK(out.hitDetected);
    CHECK(out.hitTargetName == "enemy_01");
    CHECK(out.damageApplied);
    CHECK(sink.callCount == 1);
    CHECK(sink.lastTarget == "enemy_01");
}

void testMissDoesNotReachDamageSink() {
    sr3tables_weapons::Weapon def = MakeRifleDef();
    WeaponInstance w(def);
    RecordingDamageSink sink;
    // Well off to the side of the aim ray - should not be hit.
    std::vector<HitscanTarget> targets = {HitscanTarget{"bystander", Vec3{50, 50, 10}, 1.0, true}};

    WeaponInstance::FireOutcome out = w.fire(0.0, Vec3{0, 0, 0}, Vec3{0, 0, 1}, 100.0, targets, &sink);

    CHECK(out.roundConsumed);  // the shot still fired - it just missed
    CHECK(!out.hitDetected);
    CHECK(out.hitTargetName.empty());
    CHECK(!out.damageApplied);
    CHECK(sink.callCount == 0);
}

void testInvalidTargetIsSkipped() {
    sr3tables_weapons::Weapon def = MakeRifleDef();
    WeaponInstance w(def);
    RecordingDamageSink sink;
    // Directly ahead but marked invalid (e.g. already dead) - must not be
    // hit even though it is geometrically on the ray.
    std::vector<HitscanTarget> targets = {HitscanTarget{"corpse", Vec3{0, 0, 10}, 1.0, false}};

    WeaponInstance::FireOutcome out = w.fire(0.0, Vec3{0, 0, 0}, Vec3{0, 0, 1}, 100.0, targets, &sink);

    CHECK(!out.hitDetected);
    CHECK(!out.damageApplied);
    CHECK(sink.callCount == 0);
}

void testDryFireSkipsHitDetectionEvenWithTargetInPath() {
    sr3tables_weapons::Weapon def = MakeRifleDef();
    WeaponInstance w(def);
    w.setRoundsInClip(0);
    RecordingDamageSink sink;
    std::vector<HitscanTarget> targets = {HitscanTarget{"enemy_01", Vec3{0, 0, 10}, 1.0, true}};

    WeaponInstance::FireOutcome out = w.fire(0.0, Vec3{0, 0, 0}, Vec3{0, 0, 1}, 100.0, targets, &sink);

    CHECK(out.soundSelected == WeaponInstance::FireSoundSelection::kDryFire);
    CHECK(!out.hitDetected);
    CHECK(sink.callCount == 0);
}

// ---------------------------------------------------------------------------
// CastHitscanRay direct unit checks.
// ---------------------------------------------------------------------------
void testCastHitscanRayDegenerateInputs() {
    std::vector<HitscanTarget> targets = {HitscanTarget{"t", Vec3{0, 0, 10}, 1.0, true}};
    CHECK(!CastHitscanRay(Vec3{0, 0, 0}, Vec3{0, 0, 0}, 100.0, targets).has_value());  // zero-length direction
    CHECK(!CastHitscanRay(Vec3{0, 0, 0}, Vec3{0, 0, 1}, 0.0, targets).has_value());    // zero range
    CHECK(!CastHitscanRay(Vec3{0, 0, 0}, Vec3{0, 0, 1}, -5.0, targets).has_value());   // negative range
}

void testCastHitscanRayOutOfRangeMisses() {
    // Target sits at z=200, well beyond a 50-unit max range.
    std::vector<HitscanTarget> targets = {HitscanTarget{"far_away", Vec3{0, 0, 200}, 1.0, true}};
    CHECK(!CastHitscanRay(Vec3{0, 0, 0}, Vec3{0, 0, 1}, 50.0, targets).has_value());
}

void testCastHitscanRayPicksClosestOfTwo() {
    std::vector<HitscanTarget> targets = {
        HitscanTarget{"far", Vec3{0, 0, 20}, 1.0, true},
        HitscanTarget{"near", Vec3{0, 0, 5}, 1.0, true},
    };
    std::optional<HitscanHit> hit = CastHitscanRay(Vec3{0, 0, 0}, Vec3{0, 0, 1}, 100.0, targets);
    CHECK(hit.has_value());
    CHECK(hit->targetName == "near");
}

}  // namespace

int main() {
    testVehicleWeaponShortCircuit();
    testRefuseGateTripped();
    testRefuseGateDefaultPassesThrough();
    testNormalFireDecrementsAmmoAndArmsTimers();
    testAmmoDecrementClampsAtZero();
    testDryFireWhenEmpty();
    testInfiniteAmmoSaturatesAndNeverDryFires();
    testInfiniteAmmoDefaultSeededFromFlags();
    testRefireDelayGateBlocksImmediateRefire();
    testHitOnValidTargetReachesDamageSink();
    testMissDoesNotReachDamageSink();
    testInvalidTargetIsSkipped();
    testDryFireSkipsHitDetectionEvenWithTargetInPath();
    testCastHitscanRayDegenerateInputs();
    testCastHitscanRayOutOfRangeMisses();
    testCastHitscanRayPicksClosestOfTwo();

    if (g_failed) {
        std::cerr << g_failed << " check(s) FAILED (" << g_passed << " passed).\n";
        return 1;
    }
    std::cout << g_passed << " tests passed.\n";
    return 0;
}
