// Test-harness helpers for the automated activity runs (sr3_viewer playtest --auto-activity
// and tests/real_activity_e2e_test.cpp). NOT game behaviour: they stand in for the player's
// driving and for physics the viewer does not have yet, the same way the mission
// --auto-objective harness stands in for navigation. Everything here is CHOSEN.
#pragma once

#include <optional>
#include <string>

#include "sr3activity/activity.h"
#include "sr3activity/fraud.h"

namespace sr3activity {

// Harness travel: moves `from` straight toward `to` by at most speed x dt.
ActivityPos StepToward(const ActivityPos& from, const ActivityPos& to, double speed, double dt);

// A synthetic car strike for Insurance Fraud. The viewer has no player ragdoll and no
// contact physics, so one strike is scripted as one ragdoll episode: a vehicle impact on
// the first frame, then tumbling ground contacts that slow down linearly, then the
// ragdoll end with the episode's airtime and thrown distance.
struct FraudStrikeScript {
    double vehicleMass = 1500.0;  // kg, a mid-size car
    double speed = 15.0;          // m/s at impact (~54 km/h)
    double durationS = 1.5;       // ragdoll length; the airtime counted for the hit
    double distanceM = 12.0;      // thrown distance (Linear_Dist input)
    std::string vehicleName = "car_4dr_standard03";
};

class FraudStrikeHarness {
public:
    explicit FraudStrikeHarness(FraudStrikeScript script = {}) : s_(std::move(script)) {}
    // Per frame. Outside an episode it calls ragdollBegin(); when the run arms the hit (not
    // in the 3 s cooldown) an episode starts. Returns the banked result on the frame the
    // episode ends.
    std::optional<FraudBankResult> step(FraudRun& run, double dt, const FraudScoringContext& ctx);
    bool inEpisode() const { return inEpisode_; }
    int strikes() const { return strikes_; }

private:
    FraudStrikeScript s_;
    bool inEpisode_ = false;
    double episodeS_ = 0.0;
    int strikes_ = 0;
};

}  // namespace sr3activity
