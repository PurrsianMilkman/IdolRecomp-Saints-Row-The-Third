// Mayhem / Tank Mayhem - spec-activity-runtime.md 6.6/6.7. See include/sr3activity/mayhem.h.

#include "sr3activity/mayhem.h"

#include <algorithm>
#include <cmath>

namespace sr3activity {

namespace {

std::optional<double> Num(const sr3xtbl::Node* n, const char* name) {
    if (!n) return std::nullopt;
    if (auto v = sr3xtbl::GetFloat(n, name)) return static_cast<double>(*v);
    return std::nullopt;
}

const sr3xtbl::Node* ActivityRow(const sr3xtbl::Document& doc) {
    if (const sr3xtbl::Node* t = doc.table())
        if (const sr3xtbl::Node* a = sr3xtbl::FindChild(t, "Activity")) return a;
    return doc.root() ? sr3xtbl::FindChild(doc.root(), "Activity") : nullptr;
}

}  // namespace

std::optional<MayhemParams> ReadMayhemParams(const sr3xtbl::Document& doc, MayhemKind kind, int levelIndex) {
    const sr3xtbl::Node* row = ActivityRow(doc);
    const sr3xtbl::Node* levels = row ? sr3xtbl::FindChild(row, "Levels") : nullptr;
    const sr3xtbl::Node* lv = levels ? sr3xtbl::NthChild(levels, "Level", static_cast<size_t>(levelIndex)) : nullptr;
    if (!lv) return std::nullopt;
    MayhemParams p;
    p.kind = kind;
    p.goal = Num(lv, "Destruction_Goal").value_or(0.0);
    p.goalCoop = Num(lv, "Destruction_Goal_Coop").value_or(p.goal);
    p.durationS = Num(lv, "Duration").value_or(0.0);
    if (const sr3xtbl::Node* ci = sr3xtbl::FindChild(lv, "Combo_Info")) {
        p.humanMult = Num(ci, "Human_Combo_Multiplier").value_or(0.0);
        p.vehicleMult = Num(ci, "Vehicle_Combo_Multiplier").value_or(0.0);
        p.moverMult = Num(ci, "Mover_Combo_Multiplier").value_or(0.0);
        p.completionMult = Num(ci, "Completion_Mult").value_or(0.0);
        p.comboMaxMult = Num(ci, "Combo_Max_Multiplier").value_or(0.0);
        if (const sr3xtbl::Node* cs = sr3xtbl::FindChild(ci, "Combo_Decay_Cutoffs"))
            for (const sr3xtbl::Node* c : sr3xtbl::Children(cs, "Combo_Decay_Cutoff"))
                p.cutoffs.push_back({Num(c, "Destruction_Value").value_or(0.0), Num(c, "Decay_Mult").value_or(0.0)});
    }
    if (const sr3xtbl::Node* ac = sr3xtbl::FindChild(lv, "Activity_Center")) {
        p.hasActivityCenter = true;
        if (const std::string* c = sr3xtbl::ChildText(ac, "Center")) p.centerName = *c;
        p.radius = Num(ac, "Radius").value_or(0.0);
    }
    return p;
}

double MayhemMoverValue(double boundingBoxVolume) {
    return std::min(std::sqrt(std::max(boundingBoxVolume, 0.0)) * 100.0 + 300.0, 1500.0);
}

MayhemRun::MayhemRun(const MayhemParams& params, const MayhemTweaks& tweaks, bool coop)
    : p_(params), tw_(tweaks), coop_(coop) {
    goal_ = coop ? p_.goalCoop : p_.goal;
    durationS_ = p_.durationS;
    comboWindowMs_ = tw_.comboMaxTimeMs;
    leashRunning_ = false;  // armed at 20000 ms and paused
    leashRemainingS_ = kMayhemLeashSeconds;
}

void MayhemRun::setSpeed(double newSpeed) {
    if (newSpeed <= 0.0 || speed_ <= 0.0) return;
    const double r = newSpeed / speed_;
    speed_ = newSpeed;
    comboWindowMs_ = std::round(comboWindowMs_ / r);
    comboRemainingMs_ = std::round(comboRemainingMs_ / r);
}

double MayhemRun::event(const MayhemEvent& e) {
    if (state_ != MayhemState::Running && state_ != MayhemState::WrapUp) return 0.0;
    if (leashRunning_) return 0.0;                 // scores only while the leash timer is paused
    if (!e.victimInAssignedHood) return 0.0;       // kills outside the assigned hoods score nothing
    if (e.cls == MayhemClass::Human && e.killedByNonPlayerAttacker) return 0.0;
    double value = e.hvtValue.value_or(e.baseValue.value_or(e.cls == MayhemClass::Human ? tw_.humanBaseValue : 0.0));
    const double classMult =
        e.cls == MayhemClass::Human ? p_.humanMult : (e.cls == MayhemClass::Vehicle ? p_.vehicleMult : p_.moverMult);

    comboCount_ += 1;
    comboMult_ += classMult;
    if (p_.comboMaxMult > 1.0) comboMult_ = std::min(comboMult_, p_.comboMaxMult);
    const double add = std::round(value * comboMult_);
    banked_ = std::min(banked_ + add, static_cast<double>(kMayhemBankedMax));
    comboCash_ += add;

    // Combo timer: extend a running one, else start it.
    const double increment = coop_ ? tw_.comboIncrementCoopMs : tw_.comboIncrementMs;
    if (comboRunning_) {
        comboRemainingMs_ += std::min(std::round(increment / speed_), comboWindowMs_ - comboRemainingMs_);
    } else {
        comboRemainingMs_ = std::min(std::round(tw_.comboStartTimeMs / speed_), comboWindowMs_);
        comboRunning_ = true;
    }
    // Decay cutoffs.
    while (cutoffIndex_ < p_.cutoffs.size() && comboCash_ >= p_.cutoffs[cutoffIndex_].destructionValue &&
           p_.cutoffs[cutoffIndex_].decayMult >= 1.0) {
        const double newSpeed = p_.cutoffs[cutoffIndex_].decayMult + (goalMet() ? p_.completionMult : 0.0);
        setSpeed(newSpeed);
        ++cutoffIndex_;
    }
    return add;
}

void MayhemRun::endCombo(MayhemTickResult& r) {
    r.comboEnded = true;
    r.comboTotal = comboCash_;
    comboCount_ = 0;
    comboCash_ = 0.0;
    comboMult_ = 1.0;
    cutoffIndex_ = 0;
    comboWindowMs_ = tw_.comboMaxTimeMs;
    speed_ = 1.0 + (goalMet() ? p_.completionMult : 0.0);
    comboRunning_ = false;
    comboRemainingMs_ = 0.0;
}

MayhemTickResult MayhemRun::tick(double dt, double distanceToCenter, bool hvtSettled, bool tankDestroyed) {
    MayhemTickResult r;
    if (state_ == MayhemState::Succeeded || state_ == MayhemState::Failed) return r;
    elapsedS_ += dt;

    // Combo timer.
    if (comboRunning_) {
        comboRemainingMs_ -= dt * 1000.0;
        if (comboRemainingMs_ <= 0.0) endCombo(r);
    }

    // Leash (Activity_Center only).
    if (p_.hasActivityCenter) {
        r.showRadiusRing = distanceToCenter >= 0.75 * p_.radius;
        if (distanceToCenter >= p_.radius) {
            if (!leashRunning_) {
                leashRunning_ = true;
                leashRemainingS_ = kMayhemLeashSeconds;
            }
        } else if (leashRunning_) {
            leashRunning_ = false;
        }
        if (leashRunning_) {
            leashRemainingS_ -= dt;
            r.strayingAway = true;
        }
    }

    const auto fail = [&]() {
        state_ = MayhemState::Failed;
        r.justFailed = true;
        return r;
    };
    if (state_ == MayhemState::Running) {
        if (goalMet()) {
            state_ = MayhemState::WrapUp;
            if (comboRunning_) setSpeed(speed_ + p_.completionMult);  // the last combo winds down fast
        } else if (elapsedS_ >= durationS_) {
            return fail();
        }
    }
    if (leashRunning_ && leashRemainingS_ <= 0.0) return fail();
    if (tankDestroyed && p_.kind != MayhemKind::Mayhem) return fail();
    if (state_ == MayhemState::WrapUp && !comboRunning_ && hvtSettled) {
        state_ = MayhemState::Succeeded;
        r.justSucceeded = true;
    }
    return r;
}

void MayhemRun::playerDied() {
    if (state_ == MayhemState::Running || state_ == MayhemState::WrapUp) state_ = MayhemState::Failed;
}

}  // namespace sr3activity
