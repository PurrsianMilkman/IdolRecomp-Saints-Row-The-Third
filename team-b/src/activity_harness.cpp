// Test-harness helpers for the automated activity runs. See include/sr3activity/harness.h.

#include "sr3activity/harness.h"

#include <algorithm>
#include <cmath>

namespace sr3activity {

ActivityPos StepToward(const ActivityPos& from, const ActivityPos& to, double speed, double dt) {
    const double d = Distance(from, to);
    const double step = speed * dt;
    if (d <= step || d <= 0.0) return to;
    const double f = step / d;
    return {from.x + (to.x - from.x) * f, from.y + (to.y - from.y) * f, from.z + (to.z - from.z) * f};
}

std::optional<FraudBankResult> FraudStrikeHarness::step(FraudRun& run, double dt, const FraudScoringContext& ctx) {
    if (run.state() != FraudState::Running) return std::nullopt;
    if (!inEpisode_) {
        run.ragdollBegin();
        if (!run.hitArmed()) return std::nullopt;  // the 3 s cooldown is still running
        inEpisode_ = true;
        episodeS_ = 0.0;
        ++strikes_;
        FraudImpact car;
        car.dt = dt > 0.0 ? dt : 1.0 / 30.0;
        car.otherMass = s_.vehicleMass;
        const double c = s_.speed / std::sqrt(2.0);
        car.relativeVelocity = {c, 0.0, c};
        car.normalSpeed = s_.speed;
        car.part = BodyPart::Chest;
        car.vehicleName = s_.vehicleName;
        car.vehicleId = strikes_;  // a different car each strike
        run.impact(car, ctx);
        return std::nullopt;
    }
    episodeS_ += dt;
    if (episodeS_ < s_.durationS) {
        // Tumbling on the ground: static contacts, slowing down linearly over the episode.
        const double v = s_.speed * 0.5 * std::max(0.0, 1.0 - episodeS_ / s_.durationS);
        FraudImpact ground;
        ground.dt = dt > 0.0 ? dt : 1.0 / 30.0;
        ground.relativeVelocity = {v, -v * 0.5, 0.0};
        ground.normalSpeed = v * 0.5;
        static const BodyPart kParts[] = {BodyPart::Leg, BodyPart::Arm, BodyPart::Head, BodyPart::Other};
        ground.part = kParts[static_cast<int>(episodeS_ * 8.0) % 4];
        run.impact(ground, ctx);
        return std::nullopt;
    }
    inEpisode_ = false;
    FraudHitInputs in;
    in.airtimeSeconds = s_.durationS;
    in.linearDistance = s_.distanceM;
    return run.ragdollEnd(in, ctx);
}

}  // namespace sr3activity
