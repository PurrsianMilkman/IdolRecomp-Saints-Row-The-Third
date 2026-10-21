// Escort - spec-activity-runtime.md section 6.8. See include/sr3activity/escort.h.

#include "sr3activity/escort.h"

#include <algorithm>
#include <cmath>

namespace sr3activity {

namespace {

std::optional<double> Num(const sr3xtbl::Node* n, const char* name) {
    if (auto v = sr3xtbl::GetFloat(n, name)) return static_cast<double>(*v);
    return std::nullopt;
}

}  // namespace

std::optional<EscortParams> ReadEscortParams(const sr3xtbl::Document& doc, EscortVariant variant, int levelIndex) {
    const sr3xtbl::Node* scope = doc.table() ? doc.table() : doc.root();
    const sr3xtbl::Node* row = scope ? sr3xtbl::FindChild(scope, "Activity") : nullptr;
    const sr3xtbl::Node* levels = row ? sr3xtbl::FindChild(row, "Levels") : nullptr;
    const sr3xtbl::Node* lv = levels ? sr3xtbl::NthChild(levels, "Level", static_cast<size_t>(levelIndex)) : nullptr;
    if (!lv) return std::nullopt;
    EscortParams p;
    p.variant = variant;
    p.satisfactionTimeS = Num(lv, "Satisfaction_Time").value_or(0.0);
    p.footageLimitS = Num(lv, "Footage_Limit").value_or(0.0);
    p.coopFootageLimitS = Num(lv, "Coop_Footage_Limit").value_or(p.footageLimitS);
    p.numComplications = static_cast<int>(Num(lv, "Num_Complications").value_or(0.0));
    p.combosToAdvance = static_cast<int>(Num(lv, "Number_of_combos_to_advance").value_or(0.0));
    return p;
}

EscortRun::EscortRun(const EscortParams& params, bool coop) : p_(params) {
    footageLimitS_ = coop ? p_.coopFootageLimitS : p_.footageLimitS;
    satS_ = p_.satisfactionTimeS;
    footageS_ = footageLimitS_;
}

double EscortRun::satisfactionMeter() const {
    return p_.satisfactionTimeS > 0.0 ? 1.0 - satS_ / p_.satisfactionTimeS : 0.0;
}

double EscortRun::footageMeter() const {
    return footageLimitS_ > 0.0 ? 1.0 - footageS_ / footageLimitS_ : 0.0;
}

void EscortRun::fail(EscortFailReason r, EscortTickResult& out) {
    phase_ = EscortPhase::Failed;
    failReason_ = r;
    out.justFailed = true;
    out.failReason = r;
}

EscortTickResult EscortRun::tick(const EscortFrame& f) {
    EscortTickResult r;
    if (phase_ == EscortPhase::Succeeded || phase_ == EscortPhase::Failed) return r;
    sinceReactionS_ += f.dt;
    sinceComplaintS_ += f.dt;

    // Fail checks first (the tick runs them before the phase switch).
    if (f.clientDead) {
        fail(EscortFailReason::ClientDead, r);
        return r;
    }
    if (f.clientVehicleDestroyed) {
        fail(EscortFailReason::CarDestroyed, r);
        return r;
    }
    if (!f.playerInClientVehicle) {
        if (!abandonRunning_) {
            abandonRunning_ = true;
            abandonLeftS_ = kEscortAbandonS;
        } else {
            abandonLeftS_ -= f.dt;
        }
        r.getBackMessage = true;
        r.getBackSecondsLeft = std::max(0.0, abandonLeftS_);
        if (abandonLeftS_ <= 0.0) {
            fail(EscortFailReason::LeftCar, r);
            return r;
        }
    } else {
        abandonRunning_ = false;
    }
    if (p_.variant == EscortVariant::Genki && f.genkiDistance > kEscortGenkiMaxDistance) {
        fail(EscortFailReason::GenkiTooFar, r);
        return r;
    }
    if (phase_ != EscortPhase::Driving) return r;

    // 1. Filming test.
    for (const EscortPursuer& pz : f.paparazzi) {
        if (pz.distance < kEscortFilmDistance && pz.lineOfSight) {
            r.filming = true;
        } else if (pz.distance < kEscortNearDistance && sinceComplaintS_ >= kEscortComplaintCooldownS) {
            r.complaint = true;
            sinceComplaintS_ = 0.0;
        }
    }
    if (r.filming && sinceReactionS_ >= kEscortFilmReactionS) {
        r.footageReaction = true;
        sinceReactionS_ = 0.0;
    }
    // 2. Filming drains the footage, no satisfaction this frame.
    if (r.filming) {
        footageS_ -= f.dt;
        r.footageWarning = footageS_ < kEscortFootageWarningS;
    } else if (!f.complicationActive && !f.irsOrSpouseWithin25 && !f.hudLock) {
        // 3. Satisfaction: SP pure driving time; Genki its minigame's amount.
        if (p_.variant == EscortVariant::Genki) satS_ -= f.genkiSatisfactionDrainS;
        else if (f.playerDrivingClientVehicle) satS_ -= f.dt;
    }
    // 5. Satisfaction full -> the drop-off.
    if (satS_ < 0.001) {  // sat < 1 ms
        phase_ = EscortPhase::ToDropoff;
        r.satisfactionFull = true;
    }
    // 6. Footage exhausted (base and Genki; the Tiger variant does not fail here).
    if (footageS_ < 0.0 && p_.variant != EscortVariant::Tiger) fail(EscortFailReason::Footage, r);
    return r;
}

bool EscortRun::arriveAtDropoff() {
    if (phase_ != EscortPhase::ToDropoff) return false;
    phase_ = EscortPhase::Succeeded;
    return true;
}

void EscortRun::playerDied() {
    if (phase_ == EscortPhase::Driving || phase_ == EscortPhase::ToDropoff) {
        phase_ = EscortPhase::Failed;
        failReason_ = EscortFailReason::PlayerDied;
    }
}

}  // namespace sr3activity
