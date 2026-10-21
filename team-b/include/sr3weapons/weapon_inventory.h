#pragma once

// sr3weapons/weapon_inventory.h - on-foot weapon handling beyond one pistol: per-weapon clip, owner ammo
// pool, reload, inventory switching and selection (gap #17). Ported from spec-weapons-runtime.md Sec5.4
// (2026-10-18) with Sec2.2c / Sec3.1.
//
// SPEC VALUES IMPLEMENTED:
//   - clip = WeaponInstance rounds-in-clip (+0x1a4, Sec2.2c); max clip = Magazine_Size (the real maxClip
//     0x00b81c30 also folds in upgrades - not modelled).                                   [CONFIRMED]
//   - per shot the clip drops by Ammo_per_Shot, clamped at 0 (WeaponInstance::fire).         [CONFIRMED]
//   - OWNER AMMO POOL (WH-6, Sec5.4.1 0x009c0970 step 2): reserve is held per Ammo row (Ammo, +0x110), shared
//     by every weapon using that row; reload takes min(maxClip - clip, pool) from it.         [FINAL]
//   - pool cap = ammo.xtbl Max_in_Reserve of the Ammo row (tables Sec5.1).                     [CONFIRMED]
//   - reload request/completion (Sec5.4.1): the transfer runs ONCE at the END of the timer; nothing moves on
//     a cancel; a reload with clip full or no reserve is refused; word B 0x80 refuses.         [FINAL]
//   - reload duration (Sec5.4.1/5.4.4): groups outside {AK, Pistol, SMG, RPG, Shot Gun, SawedOff, Rifle,
//     Sniper, Stun Gun, Minigun, DualPistol, Luchador, StagRifle, Genki}, or word B 0x40000000, reload
//     instantly (override*1000 ms when Reload_override_time_sec > 0, else at once); otherwise
//     dur = override > 0 ? override : clipFrames/30, dur /= reloadScalar, ms = trunc(dur*1000);
//     animation rate = (override > 0 ? clipLen/override : 1) * scalar. Reload clip frames per reload
//     group: Pistol 44, Rifle 71, Shot Gun 96, SawedOff 85, Sniper 79, RPG 38 [FINAL, male player],
//     Luchador 85 [HIGH, parent fallback]. The reload scalar is replaced, not multiplied, by
//     setReloadScalar (Faster_Reload_1/2/3 = 1.25/1.5/1.75).                                  [FINAL]
//   - cancel (switch, vehicle entry, jump, ...): disarm, no transfer, arm the 5000 ms idle top-up;
//     every shot and noteBusy() re-arm it; on expiry a ranged clip < maxClip refills silently from the
//     pool with no animation.                                                                [FINAL]
//   - auto-reload: the per-frame tick requests a reload whenever the held ranged weapon's clip is <= 0
//     (local player). Shotguns load the whole clip at once (no per-shell loop).               [FINAL]
//   - switch (Sec5.4.2): there is no flat timer and no holster time: the old weapon is stowed at once;
//     the new weapon fires from the equip clip's `impact start` frame (readyMs = frame*1000/30,
//     equipFullMs = clip frames): Pistol group 333/467 ms, Rifle chain (Rifle, Shot Gun, SawedOff,
//     Sniper, RPG) 300/567 ms [FINAL frames, HIGH inheritance]; groups with no equip
//     clip draw instantly [HIGH].
//   - selection (Sec5.4.3): number key k selects Inv_Slot k-1 (slot 0 = empty-handed); CW/CCW cycle
//     Inv_Slot +-1 wrapping 10 -> 0 / -1 -> 10, skipping slots 8 (vehicle), 9 (grenade), empty and
//     disabled ones, with slot 0 a stop (fists); 50 ms press repeat (Inventory_next_prev_delay_ms);
//     one weapon per Inv_Slot; "last weapon" = the Inv_Slot left by the previous switch; radial shown
//     650 ms (Inventory_close_delay_ms) after a switch.                                      [FINAL]
//   - out-of-ammo auto-switch after a shot (0x009e62f0 / 0x005ff7a0): single_use with ammo, vehicle slot,
//     then slots 5, 4, 3, 2, 1 with ammo, then melee slot 1; otherwise stay empty.            [FINAL]
//   - weapon flags (tables Sec2.4): word A 0x2 infinite magazine, 0x8 unlimited ammo set the instance's
//     infinite state at add() (WH-9; the mapping to instance bit +0x1a0 0x40 is HIGH); word B 0x80
//     disallow reload, 0x40000000 player instant reload.
//   - per-shot refire gate = Time_Management.Refire_Delay (Sec3.1, HIGH; equality OPEN).
// CHOSEN STAND-INS (spec is silent; each is a Team A question, see the task report):
//   - reload clip length for groups that have a reload animation but no frame figure in Sec5.4.2 (AK, SMG,
//     Stun Gun, Minigun, DualPistol, StagRifle, Genki): kDefaultReloadMs.
//   - equip timing for melee 2h / brute (group names unknown) and any group not named above: draw instantly.
//   - reload is refused while the new weapon is still in its draw window (spec lists no such gate);
//     firing is blocked while reloading (spec: no source).
//   - dual-wield, secondary clip, grenade companion exception and RC-gun exception of the auto-switch.
//   - the pool is seeded to max(existing, startReserve) when a second weapon shares an Ammo row.
//   - which weapons the player starts with and their starting reserve (the caller's choice).

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "sr3weapons/weapon_fire.h"

namespace sr3weapons {

inline constexpr double kDefaultReloadMs = 2000.0;  // CHOSEN: reload groups with no frame count in Sec5.4.2
inline constexpr double kIdleTopUpMs = 5000.0;      // FINAL (Sec5.4.1 cancel / idle top-up)
inline constexpr double kNextPrevDelayMs = 50.0;    // FINAL Inventory_next_prev_delay_ms
inline constexpr double kRadialCloseDelayMs = 650.0;  // FINAL Inventory_close_delay_ms

// weapons.xtbl flag bits used here (spec-tables-weapons-combat.md Sec2.4).
inline constexpr uint32_t kFlagAAltUnlimitedAmmo = 0x1;  // secondary only; secondary clip not modelled
inline constexpr uint32_t kFlagAInfiniteMagazine = 0x2;
inline constexpr uint32_t kFlagAUnlimitedAmmo = 0x8;
inline constexpr uint32_t kFlagBCutsceneOnly = 0x1;
inline constexpr uint32_t kFlagBDisallowReload = 0x80;
inline constexpr uint32_t kFlagBPlayerInstantReload = 0x40000000;

// Inv_Slot values (kInvSlotNames).
inline constexpr int kInvUnarmed = 0, kInvMelee = 1, kInvPistol = 2, kInvSmg = 3, kInvShotgun = 4, kInvRifle = 5,
                     kInvExplosive = 6, kInvSpecial = 7, kInvVehicle = 8, kInvGrenade = 9, kInvSingleUse = 10;

struct ReloadPlan {
    bool instant = false;  // no animation: transfer on the request frame unless ms > 0
    double ms = 0.0;       // timer length (instant: override*1000 or 0)
    double rate = 1.0;     // animation rate
};
// Sec5.4.4 step 2. `scalar` = player reload scalar (1.0 / 1.25 / 1.5 / 1.75).
ReloadPlan PlanReload(const sr3tables_weapons::Weapon& def, double scalar);

struct EquipTiming {
    double readyMs = 0.0;  // fire-ready: equip clip `impact start` frame
    double fullMs = 0.0;   // equip clip end
};
EquipTiming PlanEquip(const sr3tables_weapons::Weapon& def);

class WeaponInventory {
public:
    struct Slot {
        explicit Slot(const sr3tables_weapons::Weapon& d) : def(d), inst(def) {}
        sr3tables_weapons::Weapon def;  // owned copy; `inst` references it (Slot is heap-pinned)
        WeaponInstance inst;
        std::string poolKey;            // owner ammo pool key (lowercased Ammo row name)
        uint32_t shots = 0, dryFires = 0, reloads = 0, switchesTo = 0;
    };

    struct FireResult {
        bool blockedBySwitch = false;
        bool blockedByReload = false;
        bool autoReloadStarted = false;
        bool autoSwitched = false;  // out-of-ammo auto-switch ran after this shot
        WeaponInstance::FireOutcome outcome;
    };

    // Adds a weapon; returns its index. The reserve lives in the owner pool of the weapon's Ammo row:
    // the pool is raised to startReserve and clamped to [0, maxReserve] (maxReserve <= 0: no cap known).
    int add(const sr3tables_weapons::Weapon& def, int32_t startReserve, int32_t maxReserve);

    int size() const { return static_cast<int>(slots_.size()); }
    // Active weapon index; -1 = empty-handed (also "no weapons").
    int activeIndex() const { return active_; }
    bool emptyHanded() const { return active_ < 0; }
    Slot* active() { return active_ >= 0 ? slots_[active_].get() : nullptr; }
    const Slot* active() const { return active_ >= 0 ? slots_[active_].get() : nullptr; }
    const Slot& slot(int i) const { return *slots_[i]; }
    Slot& slot(int i) { return *slots_[i]; }

    // Owner ammo pool (WH-6): reserve of weapon i's Ammo row, shared across weapons using that row.
    int32_t reserve(int i) const;
    void setReserve(int i, int32_t value);  // clamped to the pool cap when known
    int32_t poolCap(int i) const;

    // Player reload scalar (replaced, not multiplied). Default 1.0.
    void setReloadScalar(double s) { reloadScalar_ = s; }
    double reloadScalar() const { return reloadScalar_; }

    // True while the active weapon cannot fire yet (inside the equip clip before `impact start`).
    bool switching(double nowMs) const { return nowMs < readyAtMs_; }
    bool reloading() const { return reloading_; }
    double reloadRate() const { return reloadRate_; }  // rate of the reload in progress / last started
    double reloadDeadlineMs() const { return reloadDoneAtMs_; }
    double readyAtMs() const { return readyAtMs_; }
    double equipFullAtMs() const { return equipFullAtMs_; }

    // Raw selection by inventory index (no repeat timer): stows the old weapon, cancels a reload, plays equip.
    bool select(int i, double nowMs);
    // Select by Inv_Slot: 0 = empty-handed, 1..7 and 10 = the weapon in that slot. False if none/disabled
    // or already held. Shows the radial for kRadialCloseDelayMs.
    bool selectSlot(int invSlot, double nowMs);
    // CW (next) / CCW (prev) cycle, Sec5.4.3. Accepted only when the 50 ms repeat timer is idle.
    bool selectNext(double nowMs);
    bool selectPrev(double nowMs);
    // Selector 12: back to the Inv_Slot left by the previous switch.
    bool selectLast(double nowMs);
    int currentInvSlot() const;
    int lastInvSlot() const { return lastInvSlot_; }
    void setSlotDisabled(int invSlot, bool disabled);
    bool radialVisible(double nowMs) const { return nowMs < radialHideAtMs_; }

    // Request a reload of the active weapon (Sec5.4.1 gates). Instant plans transfer at once.
    bool startReload(double nowMs);
    // Cancel a running reload (switch, vehicle entry, melee, ragdoll, jump, freefall): no ammo moves,
    // arms the 5000 ms idle top-up. Returns whether a reload was running.
    bool cancelReload(double nowMs);
    // The human is busy (in a vehicle, scoped, ...): re-arms the idle top-up every call.
    void noteBusy(double nowMs) { topUpAtMs_ = nowMs + kIdleTopUpMs; }

    // Advance timers: completes a due reload, runs the idle top-up, auto-reloads an empty clip.
    void update(double nowMs);

    FireResult fire(double nowMs, const Vec3& origin, const Vec3& direction, double maxRange,
                    const std::vector<HitscanTarget>& targets, IWeaponDamageSink* sink);

    // Out-of-ammo auto-switch (0x009e62f0): returns true if the weapon was switched.
    bool autoSwitchIfEmpty(double nowMs);

    uint32_t totalSwitches() const { return switches_; }
    uint32_t totalReloads() const;

    // One line: "weapon-state-trace: active=NAME slot=i/n | NAME shots=.. dry=.. reloads=.. switches=..
    // ammo=clip/reserve | ...". Infinite weapons print "inf".
    std::string traceLine() const;

private:
    struct Pool { int32_t count = 0; int32_t cap = 0; };
    bool hasAmmo(const Slot& s) const;
    int findSlot(int invSlot) const;
    bool switchTo(int i, double nowMs);  // i < 0: empty-handed
    void completeTransfer(Slot& s);
    std::vector<std::unique_ptr<Slot>> slots_;
    std::map<std::string, Pool> pools_;
    int active_ = -1;
    double readyAtMs_ = 0.0, equipFullAtMs_ = 0.0;
    bool reloading_ = false;
    double reloadDoneAtMs_ = 0.0;
    double reloadRate_ = 1.0;
    double reloadScalar_ = 1.0;
    double topUpAtMs_ = -1.0;  // < 0 = disarmed
    double cycleTimerMs_ = 0.0, radialHideAtMs_ = 0.0;
    int lastInvSlot_ = kInvUnarmed;
    uint32_t disabledMask_ = 0;
    uint32_t switches_ = 0;
};

}  // namespace sr3weapons
