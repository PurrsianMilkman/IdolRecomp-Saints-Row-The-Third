// Synthetic tests for sr3weapons::WeaponInventory (include/sr3weapons/weapon_inventory.h):
// clip + owner ammo pool, reload (spec-weapons-runtime.md Sec2.2c, Sec5.4.1), switching and selection
// (Sec5.4.2/5.4.3), flags, counters, trace line. Reproduces every Sec5.4.5 test vector. Hand-built
// weapons.xtbl rows.

#include <cmath>
#include <iostream>
#include <string>

#include "sr3weapons/weapon_inventory.h"

namespace {
int g_passed = 0, g_failed = 0;
#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (cond) ++g_passed;                                                            \
        else { ++g_failed; std::cerr << "CHECK FAILED: " #cond " at line " << __LINE__ << "\n"; } \
    } while (0)

using namespace sr3weapons;

sr3tables_weapons::Weapon Def(const char* name, int mag, int perShot, int refireMs, const char* group = "",
                              int invSlot = 0xFF, const char* ammo = "", float overrideSec = 0.0f) {
    sr3tables_weapons::Weapon w;
    w.name = name;
    w.weaponClass = 0;
    w.magazineSize.present = true;
    w.magazineSize.value = static_cast<uint16_t>(mag);
    w.ammoPerShot.present = true;
    w.ammoPerShot.value = static_cast<uint16_t>(perShot);
    w.timeManagement.refireDelayMs.present = true;
    w.timeManagement.refireDelayMs.value = static_cast<uint16_t>(refireMs);
    if (group[0]) w.animationGroup = std::string(group);
    w.invSlot = static_cast<uint8_t>(invSlot);
    if (ammo[0]) w.ammo = std::string(ammo);
    if (overrideSec > 0.0f) w.reloadOverrideTimeSec = overrideSec;
    return w;
}
const Vec3 kO{0, 0, 0}, kD{0, 0, 1};
const std::vector<HitscanTarget> kNoTargets;

WeaponInventory::FireResult Fire(WeaponInventory& inv, double t) { return inv.fire(t, kO, kD, 100.0, kNoTargets, nullptr); }

sr3tables_weapons::Weapon PistolGang() { return Def("Pistol-Gang", 8, 1, 100, "Pistol", kInvPistol, "pistol"); }

void testFireConsumesAndDryFires() {
    WeaponInventory inv;
    inv.add(Def("pistol", 3, 1, 100, "Pistol", kInvPistol, "pistol"), 0, 10);  // no reserve
    double t = 1000;
    for (int i = 0; i < 3; ++i) { auto r = Fire(inv, t); CHECK(r.outcome.roundConsumed); t += 200; }
    CHECK(inv.active()->inst.roundsInClip() == 0);
    auto r = Fire(inv, t);
    CHECK(!r.outcome.roundConsumed);
    CHECK(r.outcome.soundSelected == WeaponInstance::FireSoundSelection::kDryFire);
    CHECK(!r.autoReloadStarted);  // no reserve
    CHECK(inv.active()->shots == 3 && inv.active()->dryFires == 1);
}

void testRefireUsesRowDelay() {
    WeaponInventory inv;
    inv.add(Def("smg", 30, 1, 100, "Rifle", kInvSmg, "smg"), 60, 200);
    CHECK(Fire(inv, 1000).outcome.roundConsumed);
    CHECK(Fire(inv, 1050).outcome.refireGateBlocked);
    CHECK(Fire(inv, 1100).outcome.roundConsumed);
}

// ---- Sec5.4.5 vectors -------------------------------------------------------------------------
void testVector1_PistolGang() {
    WeaponInventory inv;
    inv.add(PistolGang(), 40, 100);
    inv.active()->inst.setRoundsInClip(0);
    CHECK(inv.startReload(0));
    CHECK(inv.reloadDeadlineMs() == 1466.0);
    CHECK(std::fabs(inv.reloadRate() - 1.0) < 1e-12);
    inv.update(1465);
    CHECK(inv.reloading() && inv.active()->inst.roundsInClip() == 0);
    inv.update(1466);
    CHECK(!inv.reloading());
    CHECK(inv.active()->inst.roundsInClip() == 8 && inv.reserve(0) == 32);
    CHECK(inv.active()->reloads == 1);
}

void testVector2_FasterReload3() {
    WeaponInventory inv;
    inv.add(PistolGang(), 40, 100);
    inv.setReloadScalar(1.75);
    inv.active()->inst.setRoundsInClip(0);
    CHECK(inv.startReload(0));
    CHECK(inv.reloadDeadlineMs() == 838.0);
    CHECK(std::fabs(inv.reloadRate() - 1.75) < 1e-12);
    inv.setReloadScalar(1.5);  // replaces, does not multiply (a new reload would use exactly 1.5)
    CHECK(inv.reloadScalar() == 1.5);
}

void testVector3_RifleGang() {
    WeaponInventory inv;
    inv.add(Def("Rifle-Gang", 30, 1, 100, "Rifle", kInvRifle, "rifle"), 10, 300);
    inv.active()->inst.setRoundsInClip(12);
    CHECK(inv.startReload(0));
    CHECK(inv.reloadDeadlineMs() == 2366.0);
    inv.update(2366);
    CHECK(inv.active()->inst.roundsInClip() == 22 && inv.reserve(0) == 0);
}

void testVector4_ShotgunPoliceJumpCancel() {
    WeaponInventory inv;
    inv.add(Def("Shotgun-Police", 8, 1, 100, "Shot Gun", kInvShotgun, "shotgun"), 20, 100);
    inv.active()->inst.setRoundsInClip(3);
    CHECK(inv.startReload(0));
    CHECK(inv.reloadDeadlineMs() == 3200.0);
    CHECK(inv.cancelReload(1000));  // jump
    CHECK(!inv.reloading());
    CHECK(inv.active()->inst.roundsInClip() == 3 && inv.reserve(0) == 20);  // no partial shells
    inv.update(3200);                                                       // the old deadline does nothing
    CHECK(inv.active()->inst.roundsInClip() == 3 && inv.reserve(0) == 20);
    inv.update(5999);
    CHECK(inv.active()->inst.roundsInClip() == 3);
    inv.update(6000);  // 5000 ms after the cancel: silent top-up
    CHECK(inv.active()->inst.roundsInClip() == 8 && inv.reserve(0) == 15);
    CHECK(!inv.reloading());
}

void testVector5_LuchadorGrenadeLauncherOverride() {
    WeaponInventory inv;
    auto d = Def("TempPickup-LuchadoreGrenadeLauncher", 1, 1, 100, "Rifle", kInvExplosive, "grenade_l", 2.5f);
    d.reloadAnimationGroup = std::string("Luchador");
    inv.add(d, 5, 50);
    inv.active()->inst.setRoundsInClip(0);
    CHECK(inv.startReload(0));
    CHECK(inv.reloadDeadlineMs() == 2500.0);
    CHECK(std::fabs(inv.reloadRate() - (85.0 / 30.0) / 2.5) < 1e-6);  // D-26: rate is float32  // 1.1333
}

void testVector6_PlayerInstantReload() {
    WeaponInventory inv;
    auto d = Def("brute_minigun", 100, 1, 50, "Minigun", kInvSpecial, "minigun_ammo");
    d.flagsB = kFlagBPlayerInstantReload;
    inv.add(d, 300, 1000);
    inv.active()->inst.setRoundsInClip(10);
    CHECK(inv.startReload(100));
    CHECK(!inv.reloading());  // +0x1cc never armed
    CHECK(inv.active()->inst.roundsInClip() == 100 && inv.reserve(0) == 210);  // transfer on the request frame
}

void testVector7_SwitchDuringReload() {
    WeaponInventory inv;
    inv.add(PistolGang(), 40, 100);
    inv.add(Def("Rifle-Gang", 30, 1, 100, "Rifle", kInvRifle, "rifle"), 90, 300);
    inv.active()->inst.setRoundsInClip(4);
    CHECK(inv.startReload(0));
    CHECK(inv.selectSlot(kInvRifle, 100));
    CHECK(!inv.reloading());
    CHECK(inv.slot(0).inst.roundsInClip() == 4 && inv.reserve(0) == 40);  // clip unchanged, nothing moved
    CHECK(inv.switching(399) && Fire(inv, 399).blockedBySwitch);
    CHECK(inv.readyAtMs() == 400.0);       // fire allowed from 300 ms after the switch
    CHECK(inv.equipFullAtMs() == 667.0);   // the equip clip ends at 567 ms
    CHECK(Fire(inv, 400).outcome.roundConsumed);
    // Pistol draw: 333 / 467 ms.
    WeaponInventory inv2;
    inv2.add(Def("Rifle-Gang", 30, 1, 100, "Rifle", kInvRifle, "rifle"), 90, 300);
    inv2.add(PistolGang(), 40, 100);
    CHECK(inv2.selectSlot(kInvPistol, 1000));
    CHECK(inv2.readyAtMs() == 1333.0 && inv2.equipFullAtMs() == 1467.0);
}

void testVector8_CycleOrder() {
    WeaponInventory inv;
    inv.add(Def("bat", 0, 1, 100, "Melee", kInvMelee), 0, 0);
    inv.add(PistolGang(), 40, 100);
    inv.add(Def("rifle", 30, 1, 100, "Rifle", kInvRifle, "rifle"), 90, 300);
    inv.add(Def("rpg", 1, 1, 100, "RPG", kInvExplosive, "rocket"), 3, 10);
    CHECK(inv.selectSlot(kInvPistol, 0));  // holding the pistol
    double t = 1000;
    CHECK(inv.selectNext(t) && inv.currentInvSlot() == kInvRifle);      t += 100;
    CHECK(inv.selectNext(t) && inv.currentInvSlot() == kInvExplosive);  t += 100;
    CHECK(inv.selectNext(t) && inv.emptyHanded());                      t += 100;  // fists are a stop
    CHECK(inv.selectNext(t) && inv.currentInvSlot() == kInvMelee);      t += 100;
    CHECK(inv.selectPrev(t) && inv.emptyHanded());                      t += 100;  // CCW from melee = fists
    CHECK(inv.selectPrev(t) && inv.currentInvSlot() == kInvExplosive);  t += 100;  // wraps -1 -> 10 and skips empties
    // Selector 12: back to the Inv_Slot left by the previous switch.
    CHECK(inv.lastInvSlot() == kInvUnarmed);
    CHECK(inv.selectLast(t) && inv.emptyHanded());
    // Disabled slots are skipped.
    inv.setSlotDisabled(kInvMelee, true);
    t += 100;
    CHECK(inv.selectNext(t) && inv.currentInvSlot() == kInvPistol);
    CHECK(!inv.selectSlot(kInvMelee, t + 1000));  // disabled
    CHECK(!inv.selectSlot(kInvGrenade, t + 1000));
    CHECK(!inv.selectSlot(kInvSpecial, t + 1000));  // nothing there
}

void testVector9_OutOfAmmoAutoSwitch() {
    WeaponInventory inv;
    inv.add(Def("Explosive-RocketLauncher", 1, 1, 100, "RPG", kInvExplosive, "rocket"), 0, 10);
    inv.add(Def("rifle", 30, 1, 100, "Rifle", kInvRifle, "rifle"), 0, 300);
    inv.add(Def("shotgun", 8, 1, 100, "Shot Gun", kInvShotgun, "shotgun"), 0, 100);
    inv.add(PistolGang(), 20, 100);
    inv.slot(1).inst.setRoundsInClip(0);   // rifle: 0 total
    inv.slot(2).inst.setRoundsInClip(6);   // shotgun: 6 in clip
    auto r = Fire(inv, 1000);  // last rocket, pool 0
    CHECK(r.outcome.roundConsumed && r.autoSwitched);
    CHECK(inv.currentInvSlot() == kInvShotgun);
    CHECK(inv.lastInvSlot() == kInvExplosive);
    // Nothing with ammo anywhere -> empty weapon stays in hand (dry fire).
    WeaponInventory inv2;
    inv2.add(Def("a", 1, 1, 100, "Rifle", kInvRifle, "r"), 0, 10);
    inv2.add(Def("b", 1, 1, 100, "Pistol", kInvPistol, "p"), 0, 10);
    inv2.slot(1).inst.setRoundsInClip(0);
    auto r2 = Fire(inv2, 1000);
    CHECK(r2.outcome.roundConsumed && !r2.autoSwitched && inv2.activeIndex() == 0);
}

void testVector10_NumberKeyRadialAndRepeat() {
    WeaponInventory inv;
    inv.add(Def("rifle", 30, 1, 100, "Rifle", kInvRifle, "rifle"), 90, 300);
    inv.add(PistolGang(), 40, 100);
    inv.add(Def("smg", 30, 1, 100, "Rifle", kInvSmg, "smg"), 90, 300);
    CHECK(inv.selectSlot(kInvPistol, 1000));  // key 3 -> Inv_Slot 2 = pistol
    CHECK(inv.currentInvSlot() == kInvPistol);
    CHECK(inv.radialVisible(1649) && !inv.radialVisible(1650));  // hidden 650 ms later
    // Two CW presses 30 ms apart -> only one step.
    CHECK(inv.selectNext(2000) && inv.currentInvSlot() == kInvSmg);
    CHECK(!inv.selectNext(2030) && inv.currentInvSlot() == kInvSmg);
    CHECK(inv.selectNext(2050) && inv.currentInvSlot() == kInvRifle);
}

// ---- other behaviour -----------------------------------------------------------------------------
void testReloadRefusals() {
    WeaponInventory inv;
    inv.add(Def("full", 10, 1, 100, "Pistol", kInvPistol, "a"), 50, 100);
    CHECK(!inv.startReload(0));  // clip already full
    auto noReload = Def("nr", 10, 1, 100, "Pistol", kInvRifle, "b");
    noReload.flagsB = kFlagBDisallowReload;
    inv.add(noReload, 50, 100);
    inv.select(1, 0);
    inv.slot(1).inst.setRoundsInClip(1);
    CHECK(!inv.startReload(1000));  // flag B 0x80
    auto inf = Def("inf", 10, 1, 100, "Pistol", kInvSmg, "c");
    inf.flagsA = kFlagAUnlimitedAmmo;
    inv.add(inf, 0, 0);
    inv.select(2, 2000);
    CHECK(!inv.startReload(3000));
    CHECK(Fire(inv, 3000).outcome.roundConsumed);
    CHECK(inv.slot(2).inst.roundsInClip() == 0x7fffffff);
    // No reserve.
    WeaponInventory inv2;
    inv2.add(Def("p", 10, 1, 100, "Pistol", kInvPistol, "a"), 0, 100);
    inv2.active()->inst.setRoundsInClip(1);
    CHECK(!inv2.startReload(0));
    // Already reloading.
    WeaponInventory inv3;
    inv3.add(Def("p", 10, 1, 100, "Pistol", kInvPistol, "a"), 50, 100);
    inv3.active()->inst.setRoundsInClip(1);
    CHECK(inv3.startReload(0) && !inv3.startReload(10));
    CHECK(Fire(inv3, 500).blockedByReload);
}

void testNoAnimationGroupsReloadInstantly() {
    // Groups without a reload animation: instant (override*1000 ms when > 0, else at once).
    WeaponInventory inv;
    inv.add(Def("flame", 20, 1, 100, "Flamethrower", kInvSpecial, "gas"), 100, 200);
    inv.active()->inst.setRoundsInClip(0);
    CHECK(inv.startReload(0) && !inv.reloading() && inv.active()->inst.roundsInClip() == 20);
    CHECK(PlanReload(Def("x", 1, 1, 1, "Default", 7), 1.0).instant);
    const ReloadPlan p = PlanReload(Def("x", 1, 1, 1, "Default", 7, "", 1.5f), 1.75);
    CHECK(p.instant && p.ms == 1500.0);  // no scalar on the instant path
    CHECK(PlanReload(Def("x", 1, 1, 1, "", 7), 1.0).instant);  // no group at all
    // Reload group overrides Animation_Group.
    auto d = Def("x", 1, 1, 1, "Default", 7);
    d.reloadAnimationGroup = std::string("Sniper");
    CHECK(PlanReload(d, 1.0).ms == 2633.0);
    CHECK(PlanReload(Def("rpg", 1, 1, 1, "RPG", 6), 1.0).ms == 1266.0);
    CHECK(PlanReload(Def("sg", 1, 1, 1, "SawedOff", 4), 1.0).ms == 2833.0);
    // CHOSEN fallback: reload animation exists but no frame count in the spec (SMG group).
    CHECK(PlanReload(Def("smg", 1, 1, 1, "SMG", 3), 1.0).ms == kDefaultReloadMs);
}

void testAutoReloadOnEmptyTick() {
    WeaponInventory inv;
    inv.add(Def("p", 2, 1, 100, "Pistol", kInvPistol, "a"), 4, 10);
    double t = 1000;
    Fire(inv, t); t += 200;
    auto r = Fire(inv, t);  // last round
    CHECK(r.outcome.roundConsumed && !inv.reloading());
    inv.update(t);  // the per-frame tick requests the reload on clip <= 0
    CHECK(inv.reloading());
    CHECK(inv.reloadDeadlineMs() == t + 1466.0);
    inv.update(t + 1466);
    CHECK(inv.active()->inst.roundsInClip() == 2 && inv.reserve(0) == 2);
    // Via fire(): the tick inside fire() reports it.
    WeaponInventory inv2;
    inv2.add(Def("p", 2, 1, 100, "Pistol", kInvPistol, "a"), 4, 10);
    Fire(inv2, 1000); Fire(inv2, 1200);
    auto r2 = Fire(inv2, 1400);
    CHECK(r2.autoReloadStarted && r2.blockedByReload && inv2.reloading());
}

void testIdleTopUpArmedByShotsAndBusy() {
    WeaponInventory inv;
    inv.add(PistolGang(), 40, 100);
    Fire(inv, 1000);  // clip 7, top-up armed for 6000
    inv.update(5999);
    CHECK(inv.active()->inst.roundsInClip() == 7);
    inv.update(6000);
    CHECK(inv.active()->inst.roundsInClip() == 8 && inv.reserve(0) == 39);
    // The busy state (vehicle) re-arms it every frame.
    Fire(inv, 7000);
    inv.noteBusy(8000);
    inv.update(12999);
    CHECK(inv.active()->inst.roundsInClip() == 7);
    inv.update(13000);
    CHECK(inv.active()->inst.roundsInClip() == 8);
}

void testSharedAmmoPoolWH6() {
    WeaponInventory inv;
    inv.add(Def("smg-a", 10, 1, 100, "Rifle", kInvSmg, "Ammo_SMG"), 30, 90);
    inv.add(Def("smg-b", 10, 1, 100, "Rifle", kInvRifle, "ammo_smg"), 30, 90);  // same Ammo row, case-insensitive
    CHECK(inv.reserve(0) == 30 && inv.reserve(1) == 30);  // not 60: one pool
    inv.slot(0).inst.setRoundsInClip(0);
    CHECK(inv.startReload(0));
    inv.update(5000);
    CHECK(inv.reserve(0) == 20 && inv.reserve(1) == 20);  // both weapons see the drain
    inv.setReserve(1, 999);
    CHECK(inv.reserve(0) == 90 && inv.poolCap(0) == 90);  // clamped to Max_in_Reserve
    // A different Ammo row is independent.
    inv.add(PistolGang(), 40, 100);
    CHECK(inv.reserve(2) == 40 && inv.reserve(0) == 90);
}

void testUnlimitedFlagsAppliedAtAddWH9() {
    WeaponInventory inv;
    auto a = Def("sp_gat01_w", 10, 1, 100, "Rifle", kInvSpecial, "x");
    a.flagsA = kFlagAUnlimitedAmmo;
    inv.add(a, 0, 0);
    CHECK(inv.slot(0).inst.infiniteAmmo());
    auto b = Def("infmag", 10, 1, 100, "Rifle", kInvRifle, "y");
    b.flagsA = kFlagAInfiniteMagazine;
    inv.add(b, 0, 0);
    CHECK(inv.slot(1).inst.infiniteAmmo());
    auto c = Def("normal", 10, 1, 100, "Rifle", kInvSmg, "z");
    c.flagsA = kFlagAAltUnlimitedAmmo;  // secondary only: not modelled, primary stays finite
    inv.add(c, 10, 0);
    CHECK(!inv.slot(2).inst.infiniteAmmo());
    CHECK(inv.traceLine().find("ammo=inf") != std::string::npos);
}

void testSwitchCountersAndEmptyHanded() {
    WeaponInventory inv;
    inv.add(PistolGang(), 40, 100);
    CHECK(!inv.selectSlot(kInvPistol, 0));  // already held
    CHECK(inv.selectSlot(kInvUnarmed, 100) && inv.emptyHanded() && inv.active() == nullptr);
    CHECK(!inv.switching(100));  // fists draw instantly
    CHECK(!inv.startReload(200));
    CHECK(Fire(inv, 300).outcome.roundConsumed == false);
    CHECK(inv.selectSlot(kInvPistol, 400) && inv.totalSwitches() == 2);
    CHECK(inv.slot(0).switchesTo == 1);
}

void testReserveClampAndTrace() {
    WeaponInventory inv;
    inv.add(Def("a", 5, 1, 100, "Pistol", kInvPistol, "a"), 999, 50);
    CHECK(inv.reserve(0) == 50);
    const std::string t = inv.traceLine();
    CHECK(t.rfind("weapon-state-trace: active=a slot=1/1", 0) == 0);
    CHECK(t.find("ammo=5/50") != std::string::npos);
}
}  // namespace

int main() {
    testFireConsumesAndDryFires();
    testRefireUsesRowDelay();
    testVector1_PistolGang();
    testVector2_FasterReload3();
    testVector3_RifleGang();
    testVector4_ShotgunPoliceJumpCancel();
    testVector5_LuchadorGrenadeLauncherOverride();
    testVector6_PlayerInstantReload();
    testVector7_SwitchDuringReload();
    testVector8_CycleOrder();
    testVector9_OutOfAmmoAutoSwitch();
    testVector10_NumberKeyRadialAndRepeat();
    testReloadRefusals();
    testNoAnimationGroupsReloadInstantly();
    testAutoReloadOnEmptyTick();
    testIdleTopUpArmedByShotsAndBusy();
    testSharedAmmoPoolWH6();
    testUnlimitedFlagsAppliedAtAddWH9();
    testSwitchCountersAndEmptyHanded();
    testReserveClampAndTrace();
    std::cout << g_passed << " tests passed.\n";
    if (g_failed) { std::cerr << g_failed << " FAILED\n"; return 1; }
    return 0;
}
