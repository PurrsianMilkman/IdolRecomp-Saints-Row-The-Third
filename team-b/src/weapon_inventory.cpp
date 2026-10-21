// src/weapon_inventory.cpp - see include/sr3weapons/weapon_inventory.h for the spec/CHOSEN breakdown.

#include "sr3weapons/weapon_inventory.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace sr3weapons {

namespace {
bool IsInfinite(const WeaponInventory::Slot& s) { return s.inst.infiniteAmmo(); }
int32_t MaxClip(const WeaponInventory::Slot& s) {
    return s.def.magazineSize.present ? static_cast<int32_t>(s.def.magazineSize.value) : 0;
}

// Lowercased, spaces removed ("Shot Gun" -> "shotgun").
std::string GroupKey(const std::optional<std::string>& g) {
    std::string k;
    if (g)
        for (char c : *g)
            if (c != ' ') k.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return k;
}
std::string Lower(const std::string& s) {
    std::string k;
    for (char c : s) k.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return k;
}

// Sec5.4.1: the groups that have a reload animation (indices 1,2,3,4,7,8,9,10,16,20,26,31,32,33).
bool GroupHasReloadAnim(const std::string& k) {
    static const char* const kNames[] = {"ak", "pistol", "smg", "rpg", "shotgun", "sawedoff", "rifle",
                                         "sniper", "stungun", "minigun", "dualpistol", "luchador",
                                         "stagrifle", "genki"};
    for (const char* n : kNames)
        if (k == n) return true;
    return false;
}
// Sec5.4.2: reload clip frames (male player). 0 = no figure in the spec.
int ReloadClipFrames(const std::string& k) {
    if (k == "pistol") return 44;
    if (k == "rifle") return 71;
    if (k == "shotgun") return 96;
    if (k == "sawedoff") return 85;
    if (k == "luchador") return 85;  // HIGH: parent fallback to SawedOff
    if (k == "sniper") return 79;
    if (k == "rpg") return 38;
    return 0;
}
}  // namespace

ReloadPlan PlanReload(const sr3tables_weapons::Weapon& def, double scalar) {
    ReloadPlan p;
    const std::string group = GroupKey(def.reloadAnimationGroup ? def.reloadAnimationGroup : def.animationGroup);
    const double ov = (def.reloadOverrideTimeSec && *def.reloadOverrideTimeSec > 0.0f)
                          ? static_cast<double>(*def.reloadOverrideTimeSec) : 0.0;
    if (!GroupHasReloadAnim(group) || (def.flagsB & kFlagBPlayerInstantReload)) {
        p.instant = true;
        p.ms = ov > 0.0 ? std::floor(ov * 1000.0 + 1e-6) : 0.0;  // no scalar applied
        return p;
    }
    if (scalar <= 0.0) scalar = 1.0;
    const int frames = ReloadClipFrames(group);
    // D-26 FINAL: float32 arithmetic of the original. len = frames*160/4800 s (float32); with an override dur = override
    // (float32) and rate = len/dur (1.0 if len is 0); else dur = len, rate 1. Scalar s: rate = f32(s*rate), dur = f32(dur/s);
    // ms = truncate(double(dur)*1000) (a time just below a whole ms loses 1 ms, as in the original).
    if (frames <= 0) {  // no frame count in the spec: CHOSEN default length, original double path
        p.rate = scalar;
        p.ms = std::floor((ov > 0.0 ? ov * 1000.0 : kDefaultReloadMs) / scalar + 1e-6);
        return p;
    }
    const float len = static_cast<float>(static_cast<double>(frames) * 160.0 / 4800.0);
    float dur = len, rate = 1.0f;
    if (ov > 0.0) {
        dur = static_cast<float>(ov);
        rate = len > 0.0f ? static_cast<float>(static_cast<double>(len) / static_cast<double>(dur)) : 1.0f;
    }
    const float s32 = static_cast<float>(scalar);
    rate = static_cast<float>(static_cast<double>(s32) * static_cast<double>(rate));
    dur = static_cast<float>(static_cast<double>(dur) / static_cast<double>(s32));
    p.rate = rate;
    p.ms = std::trunc(static_cast<double>(dur) * 1000.0);
    return p;
}

EquipTiming PlanEquip(const sr3tables_weapons::Weapon& def) {
    const std::string g = GroupKey(def.animationGroup);
    EquipTiming t;
    if (g == "pistol") {  // plym_ps_eqip 14 f, impact start f10
        t.readyMs = std::floor(10 * 1000.0 / 30.0 + 1e-6);
        t.fullMs = std::floor(14 * 1000.0 / 30.0 + 0.5);
    } else if (g == "rifle" || g == "shotgun" || g == "sawedoff" || g == "sniper" || g == "rpg") {
        t.readyMs = std::floor(9 * 1000.0 / 30.0 + 1e-6);  // plym_rfl_sd_equip 17 f, impact start f9
        t.fullMs = std::floor(17 * 1000.0 / 30.0 + 0.5);
    }  // else: no equip clip known -> draws instantly (HIGH for chain-less groups, CHOSEN for unknown names)
    return t;
}

int WeaponInventory::add(const sr3tables_weapons::Weapon& def, int32_t startReserve, int32_t maxReserve) {
    auto s = std::make_unique<Slot>(def);
    // WH-9: word A 0x2 (infinite magazine) / 0x8 (unlimited ammo) set the instance's infinite state.
    if (def.flagsA & (kFlagAInfiniteMagazine | kFlagAUnlimitedAmmo)) s->inst.setInfiniteAmmo(true);
    s->poolKey = def.ammo && !def.ammo->empty() ? Lower(*def.ammo) : "~slot" + std::to_string(slots_.size());
    Pool& pool = pools_[s->poolKey];
    if (maxReserve > 0) pool.cap = maxReserve;
    pool.count = std::max(pool.count, std::max<int32_t>(0, startReserve));
    if (pool.cap > 0) pool.count = std::min(pool.count, pool.cap);
    // Refire gate from the row (Sec3.1, HIGH); absent/0 keeps the original working default.
    if (def.timeManagement.refireDelayMs.present && def.timeManagement.refireDelayMs.value > 0)
        s->inst.setRefireDelayMs(static_cast<double>(def.timeManagement.refireDelayMs.value));
    const bool first = slots_.empty();
    slots_.push_back(std::move(s));
    if (first) active_ = 0;
    return static_cast<int>(slots_.size()) - 1;
}

int32_t WeaponInventory::reserve(int i) const {
    auto it = pools_.find(slots_[i]->poolKey);
    return it == pools_.end() ? 0 : it->second.count;
}
void WeaponInventory::setReserve(int i, int32_t value) {
    Pool& p = pools_[slots_[i]->poolKey];
    p.count = std::max<int32_t>(0, value);
    if (p.cap > 0) p.count = std::min(p.count, p.cap);
}
int32_t WeaponInventory::poolCap(int i) const {
    auto it = pools_.find(slots_[i]->poolKey);
    return it == pools_.end() ? 0 : it->second.cap;
}

int WeaponInventory::currentInvSlot() const {
    return active_ < 0 ? kInvUnarmed : static_cast<int>(slots_[active_]->def.invSlot);
}

int WeaponInventory::findSlot(int invSlot) const {
    for (int i = 0; i < size(); ++i)
        if (static_cast<int>(slots_[i]->def.invSlot) == invSlot) return i;  // one weapon per slot: first wins
    return -1;
}

void WeaponInventory::setSlotDisabled(int invSlot, bool disabled) {
    if (invSlot < 0 || invSlot > 11) return;
    if (disabled) disabledMask_ |= 1u << invSlot;
    else disabledMask_ &= ~(1u << invSlot);
}

bool WeaponInventory::switchTo(int i, double nowMs) {
    if (i == active_ || i >= size()) return false;
    cancelReload(nowMs);  // Sec5.4.2 step 1: a switch cancels any reload (no transfer)
    lastInvSlot_ = currentInvSlot();  // Sec5.4.2 step 3: slot memory = Inv_Slot of the weapon being left
    active_ = i;
    EquipTiming t;  // old weapon stowed at once; empty-handed draws instantly
    if (i >= 0) t = PlanEquip(slots_[i]->def);
    readyAtMs_ = nowMs + t.readyMs;
    equipFullAtMs_ = nowMs + t.fullMs;
    if (i >= 0) ++slots_[i]->switchesTo;
    ++switches_;
    return true;
}

bool WeaponInventory::select(int i, double nowMs) {
    if (i < 0 || i >= size()) return false;
    return switchTo(i, nowMs);
}

bool WeaponInventory::selectSlot(int invSlot, double nowMs) {
    int target = -1;
    if (invSlot == kInvUnarmed) {
        if (active_ < 0) return false;
    } else {
        const bool selectable = (invSlot >= kInvMelee && invSlot <= kInvSpecial) || invSlot == kInvSingleUse;
        if (!selectable || (disabledMask_ & (1u << invSlot))) return false;
        target = findSlot(invSlot);
        if (target < 0) return false;
    }
    if (!switchTo(target, nowMs)) return false;
    radialHideAtMs_ = nowMs + kRadialCloseDelayMs;
    return true;
}

bool WeaponInventory::selectLast(double nowMs) { return selectSlot(lastInvSlot_, nowMs); }

// Sec5.4.3 cycle: step +-1 over Inv_Slot 0..10, wrapping; skip 8, 9, empty, disabled; slot 0 = fists.
bool WeaponInventory::selectNext(double nowMs) {
    if (nowMs < cycleTimerMs_) return false;
    cycleTimerMs_ = nowMs + kNextPrevDelayMs;
    int s = currentInvSlot();
    if (s < 0 || s > 10) s = kInvUnarmed;
    for (int step = 0; step < 11; ++step) {
        s = (s + 1) % 11;
        if (s == kInvVehicle || s == kInvGrenade) continue;
        if (s != kInvUnarmed && (disabledMask_ & (1u << s))) continue;
        if (s != kInvUnarmed && findSlot(s) < 0) continue;
        return selectSlot(s, nowMs);
    }
    return false;
}
bool WeaponInventory::selectPrev(double nowMs) {
    if (nowMs < cycleTimerMs_) return false;
    cycleTimerMs_ = nowMs + kNextPrevDelayMs;
    int s = currentInvSlot();
    if (s < 0 || s > 10) s = kInvUnarmed;
    for (int step = 0; step < 11; ++step) {
        s = (s + 10) % 11;
        if (s == kInvVehicle || s == kInvGrenade) continue;
        if (s != kInvUnarmed && (disabledMask_ & (1u << s))) continue;
        if (s != kInvUnarmed && findSlot(s) < 0) continue;
        return selectSlot(s, nowMs);
    }
    return false;
}

void WeaponInventory::completeTransfer(Slot& s) {
    const int32_t maxClip = MaxClip(s);
    if (IsInfinite(s)) { s.inst.setRoundsInClip(maxClip); return; }  // 0x009c0970 step 1: straight to maxClip
    Pool& pool = pools_[s.poolKey];
    const int32_t take = std::min(maxClip - s.inst.roundsInClip(), pool.count);  // step 2 / Sec2.2c
    if (take > 0) {
        pool.count -= take;
        s.inst.setRoundsInClip(s.inst.roundsInClip() + take);
    }
}

bool WeaponInventory::startReload(double nowMs) {
    Slot* s = active();
    if (!s || switching(nowMs) || reloading_) return false;
    if (s->def.flagsB & kFlagBDisallowReload) return false;
    if (IsInfinite(*s)) return false;
    const int32_t maxClip = MaxClip(*s);
    if (maxClip <= 0 || s->inst.roundsInClip() >= maxClip || reserve(active_) <= 0) return false;
    const ReloadPlan plan = PlanReload(s->def, reloadScalar_);
    reloadRate_ = plan.rate;
    if (plan.instant && plan.ms <= 0.0) {  // transfer on the request frame; +0x1cc never armed
        completeTransfer(*s);
        ++s->reloads;
        return true;
    }
    reloading_ = true;
    reloadDoneAtMs_ = nowMs + plan.ms;
    return true;
}

bool WeaponInventory::cancelReload(double nowMs) {
    if (!reloading_) return false;
    reloading_ = false;  // nothing moves on a cancel
    topUpAtMs_ = nowMs + kIdleTopUpMs;
    return true;
}

void WeaponInventory::update(double nowMs) {
    if (reloading_ && nowMs >= reloadDoneAtMs_) {
        reloading_ = false;
        if (Slot* s = active()) {
            completeTransfer(*s);
            ++s->reloads;
        }
    }
    Slot* s = active();
    if (!s) return;
    const int32_t maxClip = MaxClip(*s);
    // Idle top-up: silent refill, no animation (Sec5.4.1).
    if (topUpAtMs_ >= 0.0 && nowMs >= topUpAtMs_) {
        topUpAtMs_ = -1.0;
        if (!reloading_ && maxClip > 0 && !IsInfinite(*s) && s->inst.roundsInClip() < maxClip) completeTransfer(*s);
    }
    // Auto-reload of an empty clip by the per-frame tick (local player only).
    if (!reloading_ && maxClip > 0 && !IsInfinite(*s) && s->inst.roundsInClip() <= 0) startReload(nowMs);
}

WeaponInventory::FireResult WeaponInventory::fire(double nowMs, const Vec3& origin, const Vec3& direction,
                                                  double maxRange, const std::vector<HitscanTarget>& targets,
                                                  IWeaponDamageSink* sink) {
    FireResult r;
    Slot* s = active();
    if (!s) return r;
    const bool wasReloading = reloading_;
    update(nowMs);
    if (!wasReloading && reloading_) r.autoReloadStarted = true;
    if (switching(nowMs)) { r.blockedBySwitch = true; return r; }
    if (reloading_) { r.blockedByReload = true; return r; }
    r.outcome = s->inst.fire(nowMs, origin, direction, maxRange, targets, sink);
    if (r.outcome.roundConsumed) {
        ++s->shots;
        topUpAtMs_ = nowMs + kIdleTopUpMs;  // the per-shot path arms the idle top-up
        r.autoSwitched = autoSwitchIfEmpty(nowMs);
    }
    if (r.outcome.soundSelected == WeaponInstance::FireSoundSelection::kDryFire) ++s->dryFires;
    return r;
}

bool WeaponInventory::hasAmmo(const Slot& s) const {
    if (IsInfinite(s) || s.inst.roundsInClip() > 0) return true;
    if (s.def.ammoPerShot.present && s.def.ammoPerShot.value == 0) return true;
    auto it = pools_.find(s.poolKey);
    return it != pools_.end() && it->second.count > 0;
}

bool WeaponInventory::autoSwitchIfEmpty(double nowMs) {
    Slot* s = active();
    if (!s || IsInfinite(*s) || MaxClip(*s) <= 0) return false;
    if (s->def.specialCaseType == 7) return false;  // satellite drone exception (RC-gun exception not modelled)
    if (s->inst.roundsInClip() > 0 || hasAmmo(*s)) return false;
    int target = -1;
    const int su = findSlot(kInvSingleUse);
    if (su >= 0 && su != active_ && hasAmmo(*slots_[su])) target = su;
    if (target < 0) {
        const int veh = findSlot(kInvVehicle);
        if (veh >= 0 && veh != active_) target = veh;
    }
    if (target < 0) {
        for (int slotNo : {kInvRifle, kInvShotgun, kInvSmg, kInvPistol, kInvMelee}) {  // 0x012ed0a8 non-dual order
            const int i = findSlot(slotNo);
            if (i >= 0 && i != active_ && hasAmmo(*slots_[i])) { target = i; break; }
        }
    }
    if (target < 0) {  // melee fallback 0x005ff680: a slot-1 weapon regardless of ammo
        const int i = findSlot(kInvMelee);
        if (i >= 0 && i != active_) target = i;
    }
    return target >= 0 && switchTo(target, nowMs);
}

uint32_t WeaponInventory::totalReloads() const {
    uint32_t n = 0;
    for (const auto& s : slots_) n += s->reloads;
    return n;
}

std::string WeaponInventory::traceLine() const {
    std::string out = "weapon-state-trace: active=" + (active() ? active()->def.name : std::string("none")) +
                      " slot=" + std::to_string(active_ + 1) + "/" + std::to_string(size());
    char buf[256];
    for (int i = 0; i < size(); ++i) {
        const Slot& s = *slots_[i];
        char ammo[48];
        if (IsInfinite(s)) std::snprintf(ammo, sizeof ammo, "inf");
        else std::snprintf(ammo, sizeof ammo, "%d/%d", s.inst.roundsInClip(), reserve(i));
        std::snprintf(buf, sizeof buf, " | %s shots=%u dry=%u reloads=%u switches=%u ammo=%s", s.def.name.c_str(),
                      s.shots, s.dryFires, s.reloads, s.switchesTo, ammo);
        out += buf;
    }
    std::snprintf(buf, sizeof buf, " | totalSwitches=%u totalReloads=%u", switches_, totalReloads());
    out += buf;
    return out;
}

}  // namespace sr3weapons
