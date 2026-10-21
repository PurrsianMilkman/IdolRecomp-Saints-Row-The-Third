// Instanced activities: the per-instance Activity record and the reward grant -
// spec-activity-runtime.md section 6.1 (shared frame: levels, rewards, the reward-grant
// path). Each playable instance (`_a_<tt>_<hood>_<nn>`) carries one .xtbl whose row is
// `Activity`; difficulty is the instance number, and each instance has one Level row
// (the reader supports up to 6).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sr3progression/respect.h"
#include "sr3xtbl/xtbl.h"

namespace sr3activity {

constexpr int kMaxActivityLevels = 6;

// One Levels/Level row: the common fields (0x0061c6e0) plus the per-type fields the
// spec names for the type that reads them (Insurance Fraud: Goal, Coop_Goal, Duration,
// Max_Spots, Crazy_Hit_Chance; Snatch: Ho_Win_Amount, Duration, Coop_Duration).
struct ActivityLevel {
    std::optional<std::string> reward;   // Reward  (an unlockables.xtbl row), "none" when absent
    std::optional<std::string> reward2;  // Reward2
    int32_t cashAward = 0;               // Cash_Award, dollars
    int32_t propsAward = 0;              // Props_Award (respect)
    // Per-type fields, read where present.
    std::optional<double> goal, coopGoal, duration, coopDuration, maxSpots, crazyHitChance, hoWinAmount;
};

struct ActivityInstance {
    std::string name;  // Name
    bool exploredAtStart = false;
    bool endWarpSidewalk = false;
    bool disableTrigger = false;
    bool startLocationEnabled = false;  // Start_Location = Enabled
    std::optional<std::string> playerOutfit, cutscene, startFunction, cleanupFunction;
    // The warp names (four 64-byte slots, 6.1): navpoints of the instance's own zone.
    std::optional<std::string> warpLocation, secondWarpLocation, endWarpLocation, remoteEndWarpLocation;
    std::vector<ActivityLevel> levels;  // at most 6
};

// Read the Activity row of an instance .xtbl. Rows past 6 Level entries are ignored
// (the reader's cap). Per-type fields that sit on the Activity row instead of the Level
// row are taken from there when the Level row lacks them (CHOSEN fallback: the spec gives
// the values per instance, not their element parent).
std::optional<ActivityInstance> ReadActivityInstance(const sr3xtbl::Document& doc);

// A world position (metres, Y up) of a placed object or of the player.
struct ActivityPos {
    double x = 0.0, y = 0.0, z = 0.0;
};
double Distance(const ActivityPos& a, const ActivityPos& b);

// The activity start point. How the in-world activity marker starts an instance is not
// traced (section 1, "Not traced at all"), and the shipped rows have Start_Location
// Disabled, so the start point is a CHOSEN stand-in: the Warp_Location navpoint
// (`local_start`) of the instance zone, entered within kActivityStartRadius.
constexpr double kActivityStartRadius = 3.0;  // CHOSEN
bool AtActivityStart(const ActivityPos& player, const ActivityPos& start, double radius = kActivityStartRadius);

// The activity type codes of the instance names (_a_<tt>_...).
enum class ActivityType { InsuranceFraud, Trafficking, Snatch, HeliAssault, Mayhem, TankMayhem, Escort, Genki, Unknown };
ActivityType TypeFromInstanceName(const std::string& name);  // "_a_if_dt_01" -> InsuranceFraud

// ---------------------------------------------------------------------------
// Reward grant (0x0061bb50), on activity success: for each level whose bit is not set
// in the save record's done byte, set the bit, apply Reward then Reward2, grant
// Props_Award respect then round(Props_Award x (multiplier - 1)), grant Cash_Award cash,
// post HUD hints, and bump the type's completion counter. A replay pays nothing.
// ---------------------------------------------------------------------------

// The persistent per-activity save record (spec-save-format.md 9.3a): byte +8 is the
// done-levels mask.
struct ActivitySaveRecord {
    uint8_t doneMask = 0;
};

constexpr int kHintCash = 0x5b;
constexpr int kHintRespect = 0x5c;
constexpr int kHintCompleteA = 0x13;
constexpr int kHintCompleteB = 0x33;

struct ActivityTypeInfo {
    bool suppressCompletionHints = false;  // activity_types record flag bit 3
    int completionHintId = -1;             // the record's own completion hint, -1 = none
};

struct LevelGrant {
    int level = 0;
    std::vector<std::string> unlocks;  // Reward, Reward2 (as named)
    sr3progression::AwardResult respectFirst;
    sr3progression::AwardResult respectBonus;  // round(Props x (multiplier - 1)); 0 at multiplier 1.0
    int64_t cashRaw = 0;                       // Cash_Award x 100, clamped
};

struct GrantResult {
    std::vector<LevelGrant> levels;  // only levels newly granted
    std::vector<int> hints;          // posted HUD hint ids, in order
    int completionCountIncrement = 0;
};

GrantResult GrantActivityRewards(const ActivityInstance& inst, ActivitySaveRecord& save,
                                 const ActivityTypeInfo& typeInfo, float respectMultiplier,
                                 sr3progression::PlayerRespect& player,
                                 const sr3progression::RespectTable& respectTable,
                                 sr3progression::AppliedUnlocks& applied,
                                 const sr3progression::AwardContext& ctx = {});

}  // namespace sr3activity
