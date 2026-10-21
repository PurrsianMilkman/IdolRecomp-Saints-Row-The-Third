// Snatch - spec-activity-runtime.md section 6.4. Deliver `Ho_Win_Amount` rescued NPCs to
// the drop-off before `Duration` (co-op: `Coop_Duration`) runs out; lose on timer expiry
// or death. No scoring beyond the count; the reward is the 6.1 grant.
#pragma once

#include <optional>

#include "sr3activity/activity.h"
#include "sr3xtbl/xtbl.h"

namespace sr3activity {

struct SnatchParams {
    double onFootDropoffDist = 6.0;   // On_Foot_Dropoff_Dist
    double vehicleDropoffDist = 8.0;  // Vehicle_Dropoff_Dist
    double pickupInRangeDist = 60.0;  // In_Range_Pickup_Dist: the pickup becomes "in range" at 60
};

// On_Foot_Dropoff_Dist / Vehicle_Dropoff_Dist / In_Range_Pickup_Dist from the instance's
// Activity row (searched among its descendants; the shipped rows keep them in
// Level/Snatch_Info). Defaults are the shipped 6 / 8 / 60.
SnatchParams ReadSnatchParams(const sr3xtbl::Document& instanceDoc);

enum class SnatchState { Running, Succeeded, Failed };

struct SnatchTickResult {
    bool justSucceeded = false;
    bool justFailed = false;  // timer expiry
};

class SnatchRun {
public:
    SnatchRun(const SnatchParams& params, const ActivityLevel& level, bool coop = false);

    bool pickupInRange(double distanceToPickup) const { return distanceToPickup <= p_.pickupInRangeDist; }
    bool atDropoff(double distanceToDropoff, bool inVehicle) const {
        return distanceToDropoff <= (inVehicle ? p_.vehicleDropoffDist : p_.onFootDropoffDist);
    }
    // Rescued NPCs handed over at the drop-off.
    void deliver(int count = 1);
    // Per frame: the win is tested before the timer, so the delivery that reaches the
    // target in the last frame wins.
    SnatchTickResult tick(double dt);
    void playerDied();

    int delivered() const { return delivered_; }
    int target() const { return target_; }
    double timeLeft() const { return durationS_ - elapsedS_; }
    SnatchState state() const { return state_; }

private:
    SnatchParams p_;
    int target_ = 0;
    double durationS_ = 0.0;
    double elapsedS_ = 0.0;
    int delivered_ = 0;
    SnatchState state_ = SnatchState::Running;
};

// The run's notoriety bump: 1 for the gang track, with the levels clamped to police 0-1
// and gang 0-3 (6.4).
struct SnatchNotoriety {
    double police = 0.0;
    double gang = 0.0;
};
SnatchNotoriety ApplySnatchNotorietyBump(SnatchNotoriety current);

// The instance's pickup and drop-off layout: names of navpoints and script NPCs placed in
// the instance's own zone (`<instance>.str2_pc`), from Level/Snatch_Info and
// Level/Pickup_Locations of the real rows.
struct SnatchPickupOption {
    bool rescue = false;  // Ho_Rescue (else Standard_Pickup)
    std::vector<std::string> hos, pimps, thugs;
    std::optional<std::string> rescueNavpoint;  // Ho_Rescue/Navpoint_Name
};
struct SnatchPickup {
    std::string scriptGroup;
    std::vector<std::string> locationNavpoints;
    std::vector<SnatchPickupOption> options;  // one per pickup complication
};
struct SnatchLayout {
    std::string dropoffNavpoint;               // Snatch_Info/Navpoint_Name
    std::optional<std::string> playerVehicle;  // Snatch_Info/Vehicle_Name
    double recruitDist = 15.0;                 // Snatch_Info/Recruit_Notification_Dist
    std::vector<SnatchPickup> pickups;         // in row order
};
SnatchLayout ReadSnatchLayout(const sr3xtbl::Document& instanceDoc);

// The rescued NPCs of a run. 6.4 gives the drop-off radii and the count; how a hostage
// joins the player is not specified, so the join rule is CHOSEN (HANDOFF-C section 8):
// a waiting hostage within `recruitDist` (Recruit_Notification_Dist) of the player
// follows the player (the pimps and thugs are not fought). While the player is at the
// drop-off (On_Foot_Dropoff_Dist / Vehicle_Dropoff_Dist, SnatchRun::atDropoff) every
// following hostage is handed over to the run.
class SnatchHostages {
public:
    enum class State { Waiting, Following, Delivered };
    struct Hostage {
        std::string name;
        ActivityPos pos;
        State state = State::Waiting;
    };
    struct Update {
        int recruited = 0;
        int delivered = 0;
    };
    void add(const std::string& name, const ActivityPos& at);
    Update update(const ActivityPos& player, bool inVehicle, const ActivityPos& dropoff, double recruitDist,
                  SnatchRun& run);
    const std::vector<Hostage>& hostages() const { return h_; }
    int count(State s) const;

private:
    std::vector<Hostage> h_;
};

}  // namespace sr3activity
