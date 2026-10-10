// src/weapon_fire.cpp - see include/sr3weapons/weapon_fire.h for the full
// confirmed/HIGH-CONFIDENCE/CHOSEN citation breakdown this file implements.

#include "sr3weapons/weapon_fire.h"

#include <array>
#include <cmath>

namespace sr3weapons {

namespace {

struct Triangle {
    Vec3 a, b, c;
};

// 12 triangles of an axis-aligned box - CHOSEN per-target hit-volume
// stand-in, see HitscanTarget's own doc comment (weapon_fire.h).
std::array<Triangle, 12> BuildAabbTriangles(const Vec3& center, double halfExtent) {
    const double e = halfExtent;
    const Vec3 c000{center.x - e, center.y - e, center.z - e};
    const Vec3 c001{center.x - e, center.y - e, center.z + e};
    const Vec3 c010{center.x - e, center.y + e, center.z - e};
    const Vec3 c011{center.x - e, center.y + e, center.z + e};
    const Vec3 c100{center.x + e, center.y - e, center.z - e};
    const Vec3 c101{center.x + e, center.y - e, center.z + e};
    const Vec3 c110{center.x + e, center.y + e, center.z - e};
    const Vec3 c111{center.x + e, center.y + e, center.z + e};
    return {{
        // -X face / +X face
        {c000, c001, c011},
        {c000, c011, c010},
        {c100, c110, c111},
        {c100, c111, c101},
        // -Y face / +Y face
        {c000, c100, c101},
        {c000, c101, c001},
        {c010, c011, c111},
        {c010, c111, c110},
        // -Z face / +Z face
        {c000, c010, c110},
        {c000, c110, c100},
        {c001, c101, c111},
        {c001, c111, c011},
    }};
}

}  // namespace

std::optional<HitscanHit> CastHitscanRay(const Vec3& origin, const Vec3& direction, double maxRange,
                                          const std::vector<HitscanTarget>& targets) {
    const double len = sr3player::Length(direction);
    if (len <= 1e-9 || maxRange <= 0.0) return std::nullopt;
    const Vec3 dirNorm = direction * (1.0 / len);
    const Vec3 to = origin + dirNorm * maxRange;

    double bestF = 1.0;
    bool found = false;
    HitscanHit bestHit;
    for (const HitscanTarget& target : targets) {
        if (!target.isValidTarget) continue;
        const std::array<Triangle, 12> triangles = BuildAabbTriangles(target.center, target.halfExtent);
        for (const Triangle& tri : triangles) {
            double f;
            Vec3 normal;
            if (sr3player::RayVsTriangle3D(origin.x, origin.y, origin.z, to.x, to.y, to.z, tri.a.x, tri.a.y, tri.a.z,
                                            tri.b.x, tri.b.y, tri.b.z, tri.c.x, tri.c.y, tri.c.z, f, normal) &&
                f < bestF) {
                bestF = f;
                found = true;
                bestHit.targetName = target.name;
                bestHit.fraction = f;
                bestHit.position = origin + (to - origin) * f;
                bestHit.normal = normal;
            }
        }
    }
    if (!found) return std::nullopt;
    return bestHit;
}

WeaponInstance::WeaponInstance(const sr3tables_weapons::Weapon& def)
    : def_(def),
      roundsInClip_(def.magazineSize.present ? static_cast<int32_t>(def.magazineSize.value) : 0),
      // kFlagsAWordNames index 1 = "infinite magazine capacity", index 3 =
      // "unlimited ammo" (sr3tables_weapons/weapons.h) - CHOSEN default
      // seeding, see this field's own doc comment in the header.
      infiniteAmmo_((def.flagsA & 0xAu) != 0) {}

WeaponInstance::FireOutcome WeaponInstance::fire(double nowMs, const Vec3& origin, const Vec3& direction,
                                                  double maxRange, const std::vector<HitscanTarget>& targets,
                                                  IWeaponDamageSink* damageSink) {
    FireOutcome out;

    // Pre-flight refire-delay gate (spec Sec3.1/Sec3.2's own framing: a
    // separate function at the player-input layer decides whether to even
    // invoke the weapon::+0x8c vtable dispatch at all - modeled here as a
    // gate checked before anything else in this self-contained class).
    if (nowMs < nextAllowedFireMs_) {
        out.refireGateBlocked = true;
        return out;
    }

    // CONFIRMED (Sec2.2c): vehicle-mounted weapons never fire through this
    // dispatch at all.
    if (def_.weaponClass == kVehicleWeaponClass) {
        out.vehicleShortCircuited = true;
        return out;
    }

    // The refuse gate (weapon::+0x88 -> 0x00b81390, else tail-calls
    // item::+0x88) - OPEN real condition, CHOSEN pass-through default.
    if (refuseGateTripped_) {
        out.refused = true;
        return out;
    }

    // item::+0x8c (0x00902760) entered - CONFIRMED (Sec2.2c).
    out.reachedItemUse = true;

    // Unconditionally, regardless of ammo/aim-lead (spec Sec2.2c: "arms 3
    // generic timer-primitive writes on the weapon INSTANCE... with +0xfc
    // set to a literal 1000ms" - see header doc comments for the other two).
    timerF4_.set(0.0);
    timerFc_.set(kRefireDelayMs);
    timer104_.set(0.0);
    nextAllowedFireMs_ = nowMs + kRefireDelayMs;

    // Weapon-specific sound/FX dispatch (0x009ba4a0): dry-fire vs normal,
    // keyed by whether ammo is available (spec Sec2.2c).
    const bool hasAmmo = infiniteAmmo_ || roundsInClip_ > 0;
    out.soundSelected = hasAmmo ? FireSoundSelection::kNormal : FireSoundSelection::kDryFire;
    if (!hasAmmo) {
        out.roundsInClipAfter = roundsInClip_;
        return out;  // no round to fire - no decrement, no hit-detection
    }

    // Rounds-in-clip decrement (spec Sec2.2c: weapon-instance this+0x1ac,
    // clamped at zero; infinite ammo saturates instead of decrementing,
    // this+0x1a0 bit 0x40).
    if (infiniteAmmo_) {
        roundsInClip_ = 0x7fffffff;
    } else {
        int32_t perShot = def_.ammoPerShot.present ? static_cast<int32_t>(def_.ammoPerShot.value) : 1;
        if (perShot <= 0) perShot = 1;
        roundsInClip_ = (roundsInClip_ > perShot) ? (roundsInClip_ - perShot) : 0;  // clamp at zero
    }
    out.roundConsumed = true;
    out.roundsInClipAfter = roundsInClip_;

    // CHOSEN wiring (NOT the traced mechanism - see weapon_fire.h's own top
    // doc comment and IWeaponDamageSink's own doc comment for the exact
    // boundary). Connects this now-CONFIRMED dispatch chain to this
    // project's own existing raycast primitive and an injectable damage-
    // application seam.
    const std::optional<HitscanHit> hit = CastHitscanRay(origin, direction, maxRange, targets);
    if (hit) {
        out.hitDetected = true;
        out.hitTargetName = hit->targetName;
        if (damageSink) {
            damageSink->applyLethalHit(hit->targetName);
            out.damageApplied = true;
        }
    }
    return out;
}

}  // namespace sr3weapons
