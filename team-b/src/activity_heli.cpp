// Heli Assault - spec-activity-runtime.md section 6.5. See include/sr3activity/heli.h.

#include "sr3activity/heli.h"

#include <cmath>

namespace sr3activity {

namespace {

double Num(const sr3xtbl::Node* n, const char* name, double def = 0.0) {
    if (!n) return def;
    if (auto v = sr3xtbl::GetFloat(n, name)) return *v;
    return def;
}

// The first descendant element with a Level_number child equal to `number`.
const sr3xtbl::Node* FindLevelInfo(const sr3xtbl::Node* n, int number) {
    if (!n) return nullptr;
    for (const sr3xtbl::Node* c : n->children()) {
        if (const std::string* ln = sr3xtbl::ChildText(c, "Level_number"))
            if (sr3xtbl::ParseInt32(*ln) == number) return c;
        if (const sr3xtbl::Node* d = FindLevelInfo(c, number)) return d;
    }
    return nullptr;
}

}  // namespace

std::optional<HeliParams> ReadHeliParams(const sr3xtbl::Document& doc, int levelIndex) {
    const sr3xtbl::Node* scope = doc.table() ? doc.table() : doc.root();
    const sr3xtbl::Node* row = scope ? sr3xtbl::FindChild(scope, "Activity") : nullptr;
    const sr3xtbl::Node* levels = row ? sr3xtbl::FindChild(row, "Levels") : nullptr;
    const sr3xtbl::Node* lv = levels ? sr3xtbl::NthChild(levels, "Level", static_cast<size_t>(levelIndex)) : nullptr;
    if (!lv) return std::nullopt;
    HeliParams p;
    p.objectivesNeeded = static_cast<int>(Num(lv, "Objectives_Needed"));
    p.heliHealthMultiplier = Num(lv, "Heli_Health_Multiplier", 1.0);
    p.friendlyFireMult = Num(lv, "Friendly_Fire_Mult");
    p.timeForLevelS = Num(lv, "Time_For_Level");
    p.useTimer = sr3xtbl::GetBool(lv, "Use_Timer").value_or(false);
    const int number = levelIndex + 1;
    const struct {
        const char* block;
        HeliObjectiveKind kind;
    } blocks[] = {{"Vehicles_destroy_objectives", HeliObjectiveKind::VehiclesDestroy},
                  {"Aircraft_destroy_objectives", HeliObjectiveKind::AircraftDestroy},
                  {"Protect_target_objective", HeliObjectiveKind::ProtectTarget},
                  {"Scripted_gang_fight_objective", HeliObjectiveKind::ScriptedGangFight}};
    for (const auto& b : blocks) {
        const sr3xtbl::Node* info = FindLevelInfo(sr3xtbl::FindChild(row, b.block), number);
        if (!info) continue;
        p.kind = b.kind;
        if (b.kind == HeliObjectiveKind::ProtectTarget) {
            HeliProtectInfo pi;
            pi.vehicleHitPoints = Num(info, "Vehicle_hit_points");
            pi.timeBetweenSpawnsS = Num(info, "Time_between_spawns");
            pi.timeBetweenSpawnsCoopS = Num(info, "Time_between_spawns_coop", pi.timeBetweenSpawnsS);
            pi.chanceOfRocketLauncher = Num(info, "Chance_of_rocket_launcher");
            p.protect = pi;
        }
        break;
    }
    return p;
}

HeliRun::HeliRun(const HeliParams& params) : p_(params) {}

void HeliRun::objectiveCompleted() {
    if (state_ != HeliState::Running) return;
    ++done_;
    if (done_ >= p_.objectivesNeeded) state_ = HeliState::Succeeded;
}

void HeliRun::tick(double dt) {
    if (state_ != HeliState::Running || !p_.useTimer) return;
    elapsedS_ += dt;
    if (elapsedS_ >= p_.timeForLevelS) fail(HeliFail::Time);
}

void HeliRun::fail(HeliFail r) {
    if (state_ != HeliState::Running) return;
    state_ = HeliState::Failed;
    failReason_ = r;
}

}  // namespace sr3activity
