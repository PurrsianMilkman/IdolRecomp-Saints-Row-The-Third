// Tests for include/sr3weapons/damage_model.h: spec-weapons-runtime.md Sec4.5
// worked numbers + Sec4.5.5a routing (spec sync 974bd21).
#include <cmath>
#include <iostream>

#include "sr3weapons/damage_model.h"

namespace {
int g_passed = 0, g_failed = 0;
#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (cond) ++g_passed;                                                            \
        else { ++g_failed; std::cerr << "CHECK FAILED: " #cond " line " << __LINE__ << "\n"; } \
    } while (0)
bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }
using namespace sr3weapons;

sr3tables_weapons::Weapon Rifle() {
    sr3tables_weapons::Weapon w;
    w.damageMaxNpc.value = 200; w.damageMaxPlayer.value = 160;
    w.damageMinNpc.value = 100; w.damageMinPlayer.value = 80;
    w.damageMaxDist = 10.0f; w.damageMinDist = 50.0f;
    w.rangeMax.value = 100.0f;
    return w;
}
}  // namespace

int main() {
    auto w = Rifle();
    // base damage lerp
    CHECK(Near(BaseDamage(w, 0, 5 * 5), 200));
    CHECK(Near(BaseDamage(w, 0, 30 * 30), 150));  // midpoint of 10..50
    CHECK(Near(BaseDamage(w, 0, 60 * 60), 100));
    CHECK(Near(BaseDamage(w, 1, 30 * 30), 120));
    w.damageMinNpc.value = 250;  // far >= near -> near, no lerp
    CHECK(Near(BaseDamage(w, 0, 30 * 30), 200));
    w = Rifle();

    // headshot: spec Sec4.5.3: tweak 8.0 default, 2.0 in vehicle, only player attackers
    ProducerInput in; in.attackerIsPlayer = true; in.hitLocation = 6; in.dist2 = 25;
    auto r = ProduceWeaponHitDamage(w, in, 5);
    CHECK(r.headshot && Near(r.damage, 200 * 8.0f));
    in.victimInVehicleHeadshotRule = true;
    CHECK(Near(ProduceWeaponHitDamage(w, in, 5).damage, 400));
    in.victimInVehicleHeadshotRule = false; in.victimHeadshotMultiplier = 3.0f;
    CHECK(Near(ProduceWeaponHitDamage(w, in, 5).damage, 600));
    in.attackerIsPlayer = false;  // NPC shooters get no headshot multiplier
    r = ProduceWeaponHitDamage(w, in, 5);
    CHECK(!r.headshot && Near(r.damage, 200));
    in.hitLocation = 9;
    CHECK(Near(ProduceWeaponHitDamage(w, in, 5).damage, 0));
    // firearm mult, riot shield, resistance
    in = ProducerInput{}; in.dist2 = 25; in.firearmDamageMultiplier = 0.5f;
    in.riotShieldPredicate = true; in.resistance = 0.1f;
    w.riotShieldDamageMultiplier.value = 0.5f;
    CHECK(Near(ProduceWeaponHitDamage(w, in, 5).damage, 200 * 0.5f * 0.5f * 0.1f));

    // shotgun pellets: t=1 -> shots; t=0.5, shots 8 -> max(1,round(0.6875)) = 1; t=0 -> 1
    CHECK(Near(ShotgunPelletFactor(0, 100, 8), 8));
    CHECK(Near(ShotgunPelletFactor(50, 100, 8), 1));
    CHECK(Near(ShotgunPelletFactor(100, 100, 8), 1));
    CHECK(Near(ShotgunPelletFactor(10, 100, 8), 5));  // t=.9,t2=.81:(1+5.67)*.81=5.40->5

    // sink: difficulty normal 0.75 received, hard 1.5, easy 0.3 (spec 4.5.4 step 7)
    SinkInput s; s.dmg = 10; s.victimIsPlayer = true; s.damageReceivedMult = 1.5f;
    s.curHp = 100; s.maxHp = 100;
    CHECK(Near(ApplyDamageSink(s).damage, 15));
    s = SinkInput{}; s.dmg = 100; s.attackerIsPlayer = true; s.damageDealtMult = 0.75f;
    s.curHp = 1000; s.maxHp = 1000;
    CHECK(Near(ApplyDamageSink(s).damage, 75));
    // step 2 nutshot 500, step 5 human shield 0.25
    s = SinkInput{}; s.dmg = 1; s.flags = 0x800; s.curHp = s.maxHp = 1000;
    CHECK(Near(ApplyDamageSink(s).damage, 500));
    s = SinkInput{}; s.dmg = 100; s.victimIsHeldHostageOfPlayer = true; s.curHp = s.maxHp = 1000;
    CHECK(Near(ApplyDamageSink(s).damage, 25));
    // step 4 fire 0.25 for player, F 0x8000 disables
    s = SinkInput{}; s.dmg = 100; s.victimIsPlayer = true; s.victimBurning = true;
    s.curHp = s.maxHp = 1000;
    CHECK(Near(ApplyDamageSink(s).damage, 25));
    s.flags = 0x8000;
    CHECK(Near(ApplyDamageSink(s).damage, 100));
    // step 8 group scale m/n = 6/12
    s = SinkInput{}; s.dmg = 100; s.victimInSquad = true; s.squadEngaged = 12; s.curHp = s.maxHp = 1000;
    CHECK(Near(ApplyDamageSink(s).damage, 50));
    s.flags = 0x1;  // exempt mask 0x9
    CHECK(Near(ApplyDamageSink(s).damage, 100));
    // step 10c damage assist: full health is unscaled; near death damps to ~x0.1
    CHECK(Near(DamageAssist(10, 1000, 1000), 10));
    CHECK(Near(DamageAssist(1, 0, 1000), 0.1f));  // f=0 -> x0.1
    // 15: negative = lethal -> maxHP; instakill; multipliers 1cbc/1cc0
    s = SinkInput{}; s.dmg = -1; s.curHp = 500; s.maxHp = 1000;
    { auto o = ApplyDamageSink(s); CHECK(o.applied && Near(o.newHp, 0)); }
    s = SinkInput{}; s.dmg = 100; s.victimDamageMult = 0.5f; s.attackerAttackMult = 2.0f; s.curHp = s.maxHp = 1000;
    CHECK(Near(ApplyDamageSink(s).damage, 100));
    s.attackerAttackMult = 3.0f;
    CHECK(Near(ApplyDamageSink(s).damage, 150));
    // bails: dead, hard invuln, immunity timer, scripted invuln, zero damage
    s = SinkInput{}; s.dmg = 100; s.victimDead = true; CHECK(!ApplyDamageSink(s).applied);
    s.victimDead = false; s.victimHardInvulnerable = true; CHECK(!ApplyDamageSink(s).applied);
    s.victimHardInvulnerable = false; s.damageImmunityTimer = true; CHECK(!ApplyDamageSink(s).applied);
    s.damageImmunityTimer = false; s.scriptedInvulnerable = true; CHECK(!ApplyDamageSink(s).applied);
    s.scriptedInvulnerable = false; s.dmg = 0; CHECK(!ApplyDamageSink(s).applied);
    // 17: never-die refills; clamp at 0
    s = SinkInput{}; s.dmg = 500; s.neverDie = true; s.curHp = 100; s.maxHp = 1000;
    CHECK(Near(ApplyDamageSink(s).newHp, 1000));
    s.neverDie = false; CHECK(Near(ApplyDamageSink(s).newHp, 0));
    // 4.5.5a census amounts (160 weapon hit, 2400) through the sink unscaled on a plain NPC
    s = SinkInput{}; s.dmg = 160; s.curHp = s.maxHp = 1000;
    CHECK(Near(ApplyDamageSink(s).damage, 160));
    s.dmg = 2400; s.curHp = s.maxHp = 5000;
    CHECK(Near(ApplyDamageSink(s).newHp, 2600));

    std::cout << g_passed << " tests passed.\n";
    if (g_failed) { std::cerr << g_failed << " FAILED\n"; return 1; }
    return 0;
}
