// Trafficking - spec-activity-runtime.md section 6.3. Drive the dealer in the activity car
// through Deal_Use_Count deals (chosen from Deals/Deal), then reach Ending_Nav. No timer.
// Lose: the dealer or the player dead, or the car destroyed (HIGH CONFIDENCE from
// Vehicle_Health). Complications per deal from Level_Complications, at most
// Max_complications. Pass/fail only; the reward is the 6.1 grant.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3activity {

struct TraffickingDeal {
    std::string dealNav, buyerSpawn, buyerType;
};

struct TraffickingComplication {
    std::string name;     // e.g. buyer_flee, buyer_flee_shooting
    double chance = 0.0;  // per deal
};

struct TraffickingParams {
    int dealUseCount = 0;  // Deal_Use_Count
    int maxComplications = 0;  // Max_complications
    std::vector<TraffickingDeal> deals;
    std::vector<TraffickingComplication> complications;
    std::string startingNav, endingNav;
    std::string dealerType, vehicleType;  // the Level's, else the Activity row's
    double vehicleHealth = 0.0;           // Vehicle_Health (Activity row)
    double vehicleOverrideSpeedMph = 0.0; // Vehicle_Override_Speed_MPH
    int policeNotoriety = 0, gangNotoriety = 0;
};

std::optional<TraffickingParams> ReadTraffickingParams(const sr3xtbl::Document& instanceDoc, int levelIndex = 0);

enum class TraffickingState { Dealing, ToEnding, Succeeded, Failed };
enum class TraffickingFail { None, DealerDied, PlayerDied, CarDestroyed };

class TraffickingRun {
public:
    explicit TraffickingRun(const TraffickingParams& params);

    // The deal currently asked for (in Deals order - the selection rule is not specified,
    // CHOSEN), or nullptr once Deal_Use_Count deals are done.
    const TraffickingDeal* currentDeal() const;
    const TraffickingParams& params() const { return p_; }
    // Roll the complications for the current deal with uniform samples in [0,1), one per
    // complication row in order; the first whose sample is under its chance applies, while
    // fewer than Max_complications have applied (CHOSEN: at most one per deal). Returns the
    // complication name, or nullopt.
    std::optional<std::string> rollComplication(const std::vector<double>& samples);
    // The current deal finished; after the last one the run heads to Ending_Nav.
    void completeDeal();
    bool reachEnding();
    void dealerDied() { fail(TraffickingFail::DealerDied); }
    void playerDied() { fail(TraffickingFail::PlayerDied); }
    void carDestroyed() { fail(TraffickingFail::CarDestroyed); }

    int dealsDone() const { return dealsDone_; }
    int dealsNeeded() const { return p_.dealUseCount; }
    int complicationsApplied() const { return complications_; }
    TraffickingState state() const { return state_; }
    TraffickingFail failReason() const { return failReason_; }

private:
    void fail(TraffickingFail r);

    TraffickingParams p_;
    int dealsDone_ = 0;
    int complications_ = 0;
    TraffickingState state_ = TraffickingState::Dealing;
    TraffickingFail failReason_ = TraffickingFail::None;
};

}  // namespace sr3activity
