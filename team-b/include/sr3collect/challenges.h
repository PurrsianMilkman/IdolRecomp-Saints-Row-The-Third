// Challenges - spec-tables-progression.md "Free-roam collectibles, challenges and stat
// tracking" C.3: the 41 rows of sr3_city_challenges.xtbl, their unlock by a completed mission,
// progress from gameplay events, completion and the reward (paid by the caller when the
// hud_challenge banner finishes: respect through the respect multiplier, then cash).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3collect {

constexpr int kStatChallengesCompleted = 67;

enum class ChallengeTrigger { None, Kill, Destroy, Stat, Special };
// Display units. Internal values are metres and seconds.
enum class ChallengeUnit { None, Meters, Miles, Seconds, Minutes, Hours };

struct ChallengeDef {
    std::string name, displayString, description, image, contact;
    double goal = 0.0;  // internal units
    int32_t respect = 0;
    double cash = 0.0;
    ChallengeUnit unit = ChallengeUnit::None;         // the row's Units
    ChallengeUnit displayUnit = ChallengeUnit::None;  // Trigger_Stat/Units when present, else Units
    bool hudPercent = true;  // HUD_Percent (else HUD_Linear)
    double hudStep = 0.0;    // HUD_Percent_Value (percent of goal) or HUD_Linear_Value
    std::optional<std::string> unlockName;  // Unlock_Requirement/Unlock_Name (a mission)
    ChallengeTrigger trigger = ChallengeTrigger::None;
    std::string team, weapon, weaponSlot, damageType;  // Trigger_Kill / Trigger_Destroy
    std::string vehicleType;                           // Trigger_Destroy
    std::vector<std::string> vehicles;                 // Trigger_Destroy/Vehicle_List
    std::string stat;                                  // Trigger_Stat/Stat (a stats.xtbl name)
    std::string specialCase, store;                    // Trigger_Special
};

// The <Table>/<Challenge> rows of sr3_city_challenges.xtbl, in file order.
std::vector<ChallengeDef> ReadChallenges(const sr3xtbl::Document& doc);
ChallengeUnit ChallengeUnitFromName(const std::string& name);
// Internal value (metres / seconds) in a display unit: miles x 0.000621371, minutes / 60,
// hours / 3600; other units unchanged.
double ChallengeDisplayValue(ChallengeUnit unit, double internal);

struct ChallengeKill {
    std::string team;   // the victim's team
    bool hostile = false;
    std::string weapon, weaponSlot, damageType;  // e.g. damage type "Headshot"
};
struct ChallengeDestroy {
    std::string vehicleType;  // e.g. Automobile, Helicopter, Watercraft
    std::string vehicle;      // vehicle name, e.g. sp_tank01
    std::string team, weapon, weaponSlot;
};

struct ChallengeState {
    double value = 0.0;
    bool unlocked = false;
    bool completed = false;
};

struct ChallengeUpdate {
    int index = -1;
    bool notice = false;       // a hud_challenge progress notice is raised
    bool completedNow = false;
};

class ChallengeTracker {
public:
    // Rows without an Unlock_Requirement start unlocked (CHOSEN: the spec gives only the
    // mission-complete unlock path; HANDOFF-C section 9).
    explicit ChallengeTracker(std::vector<ChallengeDef> defs);

    // The mission/activity-complete hook: unlocks every row whose Unlock_Name matches.
    void missionCompleted(const std::string& name);
    // The progress routine: ignored when locked, completed or amount <= 0; completes at the
    // goal and clamps to it; a notice when the value crosses a HUD step or completes.
    ChallengeUpdate progress(int index, double amount);

    // Events (the dispatcher's filters; "Any" matches everything, "Any Hostile But Police"
    // matches a hostile victim that is not Police).
    std::vector<ChallengeUpdate> statChanged(const std::string& stat, double delta);
    std::vector<ChallengeUpdate> kill(const ChallengeKill& k);
    std::vector<ChallengeUpdate> destroy(const ChallengeDestroy& d);
    std::vector<ChallengeUpdate> special(const std::string& specialCase, double amount = 1.0,
                                         const std::string& store = std::string());

    const std::vector<ChallengeDef>& defs() const { return defs_; }
    const std::vector<ChallengeState>& states() const { return states_; }
    ChallengeState& state(int i) { return states_[static_cast<size_t>(i)]; }
    int completedCount() const;
    void unlockAll();  // test aid

private:
    std::vector<ChallengeDef> defs_;
    std::vector<ChallengeState> states_;
};

}  // namespace sr3collect
