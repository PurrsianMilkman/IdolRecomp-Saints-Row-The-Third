// sr3weapons/damage_model.h - the weapon damage model of spec-weapons-runtime.md
// Sec4.5 (producer side 4.5.3, resistance 4.5.2, sink chain 4.5.4) and the
// call-site routing rule of Sec4.5.5a (live capture 2026-10-13, spec sync
// 974bd21): all producers (weapon hit, impact/physics, area/explosion) feed ONE
// sink as (victim, attacker-or-null, float amount).
//
// Header-only, pure functions; every world fact (is the victim a player, is it
// burning, ...) is an input, because those predicates live in engine state this
// tier does not own. Steps whose meaning the spec leaves OPEN (4.5.4 steps 9,
// 10a, 12, the network parts of 3/6, the mode-27 scalar) are NOT modelled and
// not invented. Step 18 cheat bits are not modelled (cheat-name mapping OPEN).
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <random>
#include <cmath>
#include <cstdint>

#include "sr3tables_weapons/weapons.h"

namespace sr3weapons {

// Sec4.5.5a: which producer reached the sink. Informational only - the sink
// itself is producer-agnostic (Port rule). Which weapon yields 2400 vs 160 is
// OPEN (LOW) and not modelled.
enum class DamageProducer : uint8_t {
    kWeaponHit,   // 0x00968740: hitscan/melee, table damage passed unscaled
    kImpact,      // 0x007844a2 / 0x0078446e: continuous amount from collision
    kExplosion,   // 0x00962806: falloff amount per victim
};

// Sec4.5.3 base damage. column: 0 = NPC target, 1 = player target.
// Lerp only when far < near, between Damage_Max_Dist and Damage_Min_Dist;
// below the near range -> near, beyond the far range -> far.
// D-10 (spec-differential-results.md, vectors C2): checks in the ORIGINAL order (far limit first, so an inverted range returns the
// far value); the interpolation runs in double and is rounded to single once at the end.
inline float BaseDamageRaw(float nearV, float farV, float maxD, float minD, float dist2) {
    if (!(farV < nearV)) return nearV;
    if (dist2 >= static_cast<float>(minD * minD)) return farV;
    if (static_cast<float>(maxD * maxD) >= dist2) return nearV;
    const float root = std::sqrt(dist2);
    const float t = static_cast<float>((static_cast<double>(root) - maxD) / (static_cast<double>(minD) - maxD));
    return static_cast<float>(static_cast<double>(t) * (static_cast<double>(farV) - nearV) + nearV);
}
inline float BaseDamage(const sr3tables_weapons::Weapon& w, int column, float dist2) {
    const float nearV = column == 1 ? w.damageMaxPlayer.value : w.damageMaxNpc.value;
    const float farV = column == 1 ? w.damageMinPlayer.value : w.damageMinNpc.value;
    const float maxD = w.damageMaxDist ? *w.damageMaxDist : 0.0f;
    const float minD = w.damageMinDist ? *w.damageMinDist : w.rangeMax.value;
    return BaseDamageRaw(nearV, farV, maxD, minD, dist2);
}

// Sec4.5.3 shotgun pellet factor 0x00963920 (attacker NOT a player and
// Weapon_Class == 3): t = clamp(1 - d/Range_Max,0,1);
// max(1, round((1 + (Shots-1) t^2) t^2)).
inline float ShotgunPelletFactor(float dist, float rangeMax, int shotsPerRound) {
    float t = rangeMax > 0.0f ? 1.0f - dist / rangeMax : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const float t2 = t * t;
    return std::max(1.0f, std::round((1.0f + float(shotsPerRound - 1) * t2) * t2));
}

// D-9 (spec-differential-results.md): for a non-player shotgun shooter the original multiplies A = 1 + (Shots-1) t^2 by a random
// draw r uniform in [t^2, 1] (single precision: t^2 + (1 - t^2) * u/2^32, u the raw 32-bit table-random value), then rounds half
// away from zero and floors at 1. u = 0 gives the smallest factor (the 3-argument form above).
inline float ShotgunPelletFactor(float dist, float rangeMax, int shotsPerRound, uint32_t drawU32) {
    float t = rangeMax > 0.0f ? 1.0f - dist / rangeMax : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const float t2 = t * t;
    const float a = 1.0f + float(shotsPerRound - 1) * t2;
    const float u = static_cast<float>(static_cast<double>(drawU32) / 4294967296.0);
    const float r = t2 + (1.0f - t2) * u;
    return std::max(1.0f, std::round(a * r));
}

// The game's table random source (D-9): a ring of 8,192 precomputed 32-bit values filled from MT19937 (default seed 5489 before
// the game code runs; the game later re-seeds from a microsecond clock, so any uniform source is acceptable for a port).
class TableRandomRing {
public:
    explicit TableRandomRing(uint32_t seed = 5489u) { reseed(seed); }
    void reseed(uint32_t seed) {
        std::mt19937 mt(seed);
        for (auto& v : ring_) v = mt();
        cursor_ = 0;
    }
    uint32_t next() {
        const uint32_t v = ring_[cursor_];
        cursor_ = (cursor_ + 1) % ring_.size();
        return v;
    }
private:
    std::array<uint32_t, 8192> ring_{};
    size_t cursor_ = 0;
};

struct ProducerInput {
    int column = 0;                          // 0 NPC target, 1 player target
    float dist2 = 0.0f;
    float firearmDamageMultiplier = 1.0f;    // attacker descriptor +0x44
    bool riotShieldPredicate = false;        // 0x00941f60
    bool attackerIsPlayer = false;
    int hitLocation = 0;                     // A+0x13c7; 6 = head
    float victimHeadshotMultiplier = -1.0f;  // descriptor +0x40; -1 = absent
    bool victimInVehicleHeadshotRule = false;  // 0x00ad3670 -> 2.0
    float tweakHeadshotMultiplier = 8.0f;    // tweak Headshot_multiplier
    float resistance = 1.0f;                 // 0x009dd8f0 result, applied before the sink
    uint32_t pelletDrawU32 = 0;              // D-9: raw table-random draw for the NPC shotgun pellet factor (TableRandomRing::next())
};

struct ProducerResult {
    float damage = 0.0f;
    bool headshot = false;
};

// Sec4.5.3 then 4.5.2 ordering: base x firearm x riot-shield x pellets x
// headshot, then producer resistance. Hit-location 9/10/12 force damage to 0.
inline ProducerResult ProduceWeaponHitDamage(const sr3tables_weapons::Weapon& w, const ProducerInput& in,
                                             float dist) {
    ProducerResult r;
    float d = BaseDamage(w, in.column, in.dist2);
    d *= in.firearmDamageMultiplier;
    if (in.riotShieldPredicate) d *= w.riotShieldDamageMultiplier.value;
    if (!in.attackerIsPlayer && w.weaponClass == 3)
        d *= ShotgunPelletFactor(dist, w.rangeMax.value, w.shotsPerRound ? *w.shotsPerRound : 1, in.pelletDrawU32);
    if (in.hitLocation == 6 && in.attackerIsPlayer) {
        float m = in.victimHeadshotMultiplier;
        if (m == -1.0f) m = in.tweakHeadshotMultiplier;
        if (in.victimInVehicleHeadshotRule) m = 2.0f;
        d *= m;
        r.headshot = true;
    }
    if (in.hitLocation == 9 || in.hitLocation == 10 || in.hitLocation == 12) d = 0.0f;
    r.damage = d * in.resistance;
    return r;
}

// Sec4.5.4 step 10c "damage assist": consume dmg in chunks of 0.05*maxHP; per
// chunk f = cur/max - c/max (floored 0); if f < start, c *= 1 + (1 - f/start)*(scale-1).
inline float DamageAssist(float dmg, float curHp, float maxHp, float startPercent = 0.3f,
                          float scaleMult = 0.1f) {
    if (maxHp <= 0.0f) return dmg;
    // D-11 evaluation order (vectors C3): single-precision state, the second division and the inner scale term in double.
    // (The original never ends for maxHP 0; the guard above returns the damage unchanged on purpose.)
    const float chunk = 0.05f * maxHp;
    float remaining = dmg, cur = curHp, acc = 0.0f;
    while (remaining > 0.0f) {
        float c = chunk > remaining ? remaining : chunk;
        remaining = remaining - c;
        const float q = cur / maxHp;
        float f = static_cast<float>(static_cast<double>(q) - static_cast<double>(c) / static_cast<double>(maxHp));
        if (f < 0.0f) f = 0.0f;
        if (startPercent > f) {
            const float s = static_cast<float>((1.0 - static_cast<double>(f) / startPercent) * (static_cast<double>(scaleMult) - 1.0) + 1.0);
            c = c * s;
        }
        acc = acc + c;
        cur = cur - c;
    }
    return acc;
}

struct SinkInput {
    float dmg = 0.0f;
    uint32_t flags = 0;  // F word (Sec4.3)
    bool victimNull = false, victimDead = false, victimHardInvulnerable = false;
    bool victimIsPlayer = false, victimFollowsLocalPlayer = false, attackerIsPlayer = false;
    bool victimBurning = false, victimStunnedOrPanic = false, attackerIsVictim = false;
    bool victimIsHeldHostageOfPlayer = false;
    float fireScalar = 0.25f, stunnedScalar = 0.25f, humanShieldMult = 0.25f,
          testicularOverride = 500.0f;
    float damageDealtMult = 1.0f, damageReceivedMult = 1.0f;  // difficulty record (7)
    bool victimInSquad = false;
    int squadEngaged = 0, squadMax = 6;
    bool avatarState = false;
    float avatarMult = 0.4f;
    float curHp = 100.0f, maxHp = 100.0f;
    bool playerImmune = false;          // 10d
    bool damageImmunityTimer = false;   // 11
    bool scriptedInvulnerable = false;  // 13, exception/F 0x100 resolved by caller
    float operatorDamageMult = 1.0f;    // 14
    bool instakill = false;             // 15 lethal condition (caller evaluated)
    float victimDamageMult = 1.0f, attackerAttackMult = 1.0f;  // +0x1cbc / +0x1cc0
    bool npcToNpc = false;
    float npcToNpcMult = 1.0f;
    bool neverDie = false;              // V+0x1c98 bit 6
};

struct SinkResult {
    bool applied = false;  // false = bail / reaction only, no HP change
    float damage = 0.0f;
    float newHp = 0.0f;
};

// The single sink all producers route into: (victim, attacker-or-null, amount)
// -> in.dmg, with the victim/attacker facts in `in`.
inline SinkResult ApplyDamageSink(const SinkInput& in) {
    SinkResult r;
    r.newHp = in.curHp;
    if (in.victimNull || in.victimDead || in.victimHardInvulnerable) return r;  // 1
    float dmg = in.dmg;
    if (in.flags & 0x800) dmg = in.testicularOverride;  // 2
    if (in.victimIsPlayer) {                            // 4
        if (in.victimBurning && !(in.flags & 0x8000) && !in.attackerIsVictim) dmg *= in.fireScalar;
        else if (in.victimStunnedOrPanic) dmg *= in.stunnedScalar;
    }
    if (in.victimIsHeldHostageOfPlayer) dmg *= in.humanShieldMult;  // 5
    if (in.attackerIsPlayer && dmg > 0) dmg *= in.damageDealtMult;  // 7
    if ((in.victimIsPlayer || in.victimFollowsLocalPlayer) && dmg > 0) dmg *= in.damageReceivedMult;
    if (!in.victimIsPlayer && in.victimInSquad && (in.flags & 0x9) == 0 &&
        in.squadEngaged > in.squadMax)
        dmg *= float(in.squadMax) / float(in.squadEngaged);  // 8
    if (in.victimIsPlayer) {                                 // 10b-d
        if (in.avatarState) dmg *= in.avatarMult;
        dmg = DamageAssist(dmg, in.curHp, in.maxHp);
        if (in.playerImmune) return r;
    }
    if (in.damageImmunityTimer) return r;   // 11
    if (in.scriptedInvulnerable) return r;  // 13
    dmg *= in.operatorDamageMult;           // 14
    if (in.instakill || dmg < 0.0f) {       // 15
        dmg = in.maxHp;
    } else {
        if (in.npcToNpc) dmg *= in.npcToNpcMult;
        dmg *= in.victimDamageMult;
        dmg *= in.attackerAttackMult;
    }
    if (dmg == 0.0f) return r;  // 16
    r.applied = true;           // 17
    r.damage = dmg;
    if (in.neverDie && dmg >= in.curHp && !(in.flags & 0x100)) {
        r.newHp = in.maxHp;
        return r;
    }
    r.newHp = std::clamp(in.curHp - dmg, 0.0f, in.maxHp);
    return r;
}

}  // namespace sr3weapons
