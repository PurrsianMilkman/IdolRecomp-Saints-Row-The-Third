// HUD dataitem producers - spec-hud-runtime.md H.1/H.2. See
// include/sr3hud/producers.h.

#include "sr3hud/producers.h"

#include <cmath>

namespace sr3hud {

const char* const kNotorietyTeamNames[kNotorietyTrackCount] = {
    "brotherhood", "ronin", "samedi", "police"};

float HealthPct(float hp, int32_t maxHp) {
    if (maxHp <= 0) return 0.0f;
    // trunc(1000 * HP / maxHP) / 1000
    const float scaled = 1000.0f * hp / static_cast<float>(maxHp);
    return std::trunc(scaled) / 1000.0f;
}

float CashDollars(int64_t cents) {
    return static_cast<float>(static_cast<double>(cents) / 100.0);
}

float NotorietyDisplayLevel(size_t track, const NotorietyTrack& rec,
                            const NotorietyThresholds& thresholds) {
    const int32_t L = rec.level;
    if (L >= 5) return 5.0f;
    const size_t row = static_cast<size_t>(L < 0 ? 0 : L) + (track == kPoliceTrack ? 5u : 0u);
    const int32_t t = thresholds[row];
    if (t <= 0) return static_cast<float>(L);
    return static_cast<float>(L) + rec.fraction / static_cast<float>(t);
}

float RespectPct(const RespectInput& in) {
    double pct;
    if (in.need == kRespectCappedNeed || in.need <= 0)
        pct = 1.0;
    else
        pct = in.progress / static_cast<double>(in.need);
    pct -= std::floor(pct);
    return static_cast<float>(pct);
}

void PushPlayerStatus(DataItem& item, const PlayerStatusInput& in) {
    using namespace status;
    item.set(SprintEnabled, in.sprintEnabled);
    item.set(status::HealthPct, sr3hud::HealthPct(in.hp, in.maxHp));
    item.set(HealthLevel, in.healthLevel);
    item.set(IsDbno, in.isDbno);
    item.set(BleedOutHealthPct, in.bleedOutHealthPct);
    item.set(SprintPct, in.sprintPct);
    item.set(SprintLevel, in.sprintLevel);
    item.set(IsSprinting, in.isSprinting);
    item.set(Orientation, in.orientation);
    item.set(IsMpInvulnerable, in.isMpInvulnerable);
    item.set(TunnelVisionAmount, in.tunnelVisionAmount);
    item.set(IsInSatelliteMode, in.isInSatelliteMode);
    item.set(IsControllingMissile, in.isControllingMissile);
}

void PushPlayerStatusInfrequent(DataItem& item, const PlayerStatusInfrequentInput& in) {
    using namespace infrequent;
    item.set(Cash, CashDollars(in.cashCents));
    item.set(MaxFollowers, in.maxFollowers);
    item.set(CruiseControlActive, in.cruiseControlActive);
    item.set(CameraScreenshotEnabled, in.cameraScreenshotEnabled);
    item.set(ForceShowCash, in.forceShowCash);
    item.set(ShowVtolHints, in.showVtolHints);
    // Field 2 is not in the spec's every-frame list (fields 0, 1, 5-8); it
    // is written here too so a caller has one entry point for the flags it
    // owns. Unchanged values never dirty the item.
    item.set(InventoryDisabled, in.inventoryDisabled);
}

void PushVehicleLogo(DataItem& item, const std::string& logo) {
    item.set(infrequent::VehicleLogo, logo);
}

bool RadioStationProducer::update(DataItem& item, std::optional<uint64_t> recordId,
                                  const std::string& stationName) {
    if (primed_ && cached_ == recordId) return false;
    primed_ = true;
    cached_ = recordId;
    item.set(infrequent::RadioStation, recordId ? stationName : std::string());
    return true;
}

void RespectProducer::push(DataItem& item, const RespectInput& in, int32_t upgradeScreenState) {
    using namespace respect;
    item.set(TotalRespect, in.totalRespect);
    item.set(respect::RespectPct, sr3hud::RespectPct(in));
    item.set(RespectLevel, in.level);
    item.set(ShowUpgrades, upgradeScreenState == 2);
    item.set(ForceShowRespect, forceShowCounter_ > 0);
    if (forceShowCounter_ > 0) --forceShowCounter_;
}

void PushNotoriety(DataItem (&items)[notoriety::kItemCount],
                   const std::array<NotorietyTrack, kNotorietyTrackCount>& tracks,
                   const NotorietyThresholds& thresholds, const FakeNotoriety& fake) {
    for (size_t i = 0; i < kNotorietyTrackCount; ++i) {
        const float level = fake.active[i] ? fake.value[i]
                                           : NotorietyDisplayLevel(i, tracks[i], thresholds);
        items[i].set(notoriety::TeamName, std::string(kNotorietyTeamNames[i]));
        items[i].set(notoriety::NotoLevel, level);
        items[i].set(notoriety::NotoIcon, std::string());
    }
}

void PushWeapons(DataItem& item, const WeaponHudInput& in) {
    using namespace weapons;
    int32_t clip = in.clipForcedZero ? 0 : in.clip;
    int32_t reserve = in.reserve;
    if (in.dualWield) {
        clip >>= 1;
        reserve >>= 1;
    }
    item.set(WpnBmpName, in.weaponBitmap ? *in.weaponBitmap : std::string());
    item.set(RdyAmmo, clip);
    item.set(RsvAmmo, reserve);
    item.set(GrenadeBmpName, in.grenadeBitmap);
    item.set(GrenadeAmmo, in.grenadeCount);
    item.set(DualWield, in.dualWield);
    item.set(NoAmmo, in.noAmmo);
    item.set(InfAmmo, in.infAmmo);
    item.set(GndInfAmmo, in.grenadeInfAmmo);
    item.set(ContFire, in.contFire);
    item.set(NoMag, in.noMag);
}

}  // namespace sr3hud
