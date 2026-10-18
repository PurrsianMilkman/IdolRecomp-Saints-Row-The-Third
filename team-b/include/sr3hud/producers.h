// HUD dataitem producers - spec-hud-runtime.md H.1 (cadence) and H.2 (the
// "C++ value and encoding" column). Each producer takes plain engine values
// (no engine-state dependency) and writes the fields of its dataitem through
// DataItem::set, so unchanged fields never mark the item dirty.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "sr3hud/dataitem.h"

namespace sr3hud {

// ---------------------------------------------------------------------------
// Encodings (H.2)
// ---------------------------------------------------------------------------

// health_pct = trunc(1000 * HP / maxHP) / 1000 as a float; 0 when maxHP <= 0.
// HP is the character's current hit points (float), maxHP its int cap.
float HealthPct(float hp, int32_t maxHp);

// cash = integer cents / 100.0, as a float in dollars.
float CashDollars(int64_t cents);

// The notoriety tracks in item order (H.2): 0 Luchadores, 1 Deckers,
// 2 Morningstar, 3 Police. SR3's gang tracks are published under the SR2
// alias names, which are the keys hud_noto.lua looks up.
constexpr size_t kNotorietyTrackCount = 4;
constexpr size_t kPoliceTrack = 3;
extern const char* const kNotorietyTeamNames[kNotorietyTrackCount];  // brotherhood ronin samedi police

// One live notoriety track record: integer level and the points banked
// toward the next level (spec-ai-runtime.md 19.1).
struct NotorietyTrack {
    int32_t level = 0;
    float fraction = 0.0f;
};

// The int threshold table the HUD level divides by: rows 0-4 for the gangs,
// rows 5-9 for police. thresholds[L + 5*(track==police)] is the points
// needed to leave level L, i.e. NotorietyLevelLimit(set, L+1).
using NotorietyThresholds = std::array<int32_t, 10>;

// Displayed level: 5.0 when L >= 5, otherwise L + fraction / T[L + 5*police].
// A non-positive threshold yields L (CHOSEN guard; the shipped table has no
// zero entry at any reachable level).
float NotorietyDisplayLevel(size_t track, const NotorietyTrack& rec,
                            const NotorietyThresholds& thresholds);

// Respect encoding inputs, one call each of the engine's respect getters.
struct RespectInput {
    int32_t totalRespect = 0;  // total
    int32_t level = 0;         // 0-based rank
    double progress = 0.0;     // respect earned toward the next rank
    int32_t need = 0;          // respect needed for the next rank
};

// The respect cap: when need is exactly this value the meter reads full.
constexpr int32_t kRespectCappedNeed = 22000;

// pct = progress / need, or 1.0 when need == 22000 or need <= 0; then
// pct -= floor(pct). So an exactly-full meter (including the capped rank)
// publishes 0.0.
float RespectPct(const RespectInput& in);

// ---------------------------------------------------------------------------
// Producers
// ---------------------------------------------------------------------------

// "sr2_local_player_status": every frame, local player only.
struct PlayerStatusInput {
    bool sprintEnabled = false;
    float hp = 0.0f;
    int32_t maxHp = 0;
    int32_t healthLevel = 0;  // the character's health upgrade level
    bool isDbno = false;
    float bleedOutHealthPct = 0.0f;
    float sprintPct = 0.0f;
    int32_t sprintLevel = 0;
    bool isSprinting = false;
    float orientation = 0.0f;
    bool isMpInvulnerable = false;
    float tunnelVisionAmount = 0.0f;
    bool isInSatelliteMode = false;
    bool isControllingMissile = false;
};
void PushPlayerStatus(DataItem& item, const PlayerStatusInput& in);

// "sr2_local_player_status_infrequent", fields 0, 1, 5-8: every frame.
// Fields 3 (vehicle logo) and 4 (radio station) have their own producers.
struct PlayerStatusInfrequentInput {
    int64_t cashCents = 0;
    uint32_t maxFollowers = 0;
    bool inventoryDisabled = false;
    bool cruiseControlActive = false;
    bool cameraScreenshotEnabled = false;
    bool forceShowCash = false;
    bool showVtolHints = false;
};
void PushPlayerStatusInfrequent(DataItem& item, const PlayerStatusInfrequentInput& in);

// Field 3: pushed from the vehicle enter/exit sites.
void PushVehicleLogo(DataItem& item, const std::string& logo);

// Field 4: pushed only when the current vehicle's radio record changes. The
// record is identified by an opaque id (nullopt = no record); a null record
// pushes "". The station string is the record's own name, e.g.
// "ui_radio_10666_the_blood".
class RadioStationProducer {
public:
    // Returns true when a push was made (the record changed).
    bool update(DataItem& item, std::optional<uint64_t> recordId,
                const std::string& stationName);

private:
    bool primed_ = false;
    std::optional<uint64_t> cached_;
};

// "sr2_local_player_respect": every frame.
class RespectProducer {
public:
    // upgradeScreenState: show_upgrades is published as (state == 2).
    void push(DataItem& item, const RespectInput& in, int32_t upgradeScreenState);
    // force_show is (counter > 0); the counter is decremented once per push.
    void requestForceShow(int32_t frames) { forceShowCounter_ = frames; }
    int32_t forceShowCounter() const { return forceShowCounter_; }

private:
    int32_t forceShowCounter_ = 0;
};

// "sr2_notoriety": pushed on a notoriety change. Item i = track i, fields
// {team_name, level, ""}. While the fake-notoriety override for a track is
// set, its override value is pushed instead of the real level.
struct FakeNotoriety {
    std::array<bool, kNotorietyTrackCount> active{};
    std::array<float, kNotorietyTrackCount> value{};
};
void PushNotoriety(DataItem (&items)[notoriety::kItemCount],
                   const std::array<NotorietyTrack, kNotorietyTrackCount>& tracks,
                   const NotorietyThresholds& thresholds, const FakeNotoriety& fake);

// "sr2_local_player_weapons" ammo/icon fields. Clip = the weapon instance's
// rounds-in-clip; reserve = the reserve getter (the instance's clip-side
// reserve, or the owner's per-ammo-type pool). Both are halved (arithmetic
// shift) when the player is dual wielding.
struct WeaponHudInput {
    std::optional<std::string> weaponBitmap;  // nullopt: published as "" (Lua shows the fist)
    int32_t clip = 0;
    // The producer forces the clip to 0 for one single-round flag case (H.2);
    // the spec does not name the flag, so the caller sets this.
    bool clipForcedZero = false;
    int32_t reserve = 0;
    bool dualWield = false;
    std::string grenadeBitmap;
    int32_t grenadeCount = 0;
    bool noAmmo = false;
    bool infAmmo = false;
    bool grenadeInfAmmo = false;
    bool contFire = false;
    bool noMag = false;
};
void PushWeapons(DataItem& item, const WeaponHudInput& in);

}  // namespace sr3hud
