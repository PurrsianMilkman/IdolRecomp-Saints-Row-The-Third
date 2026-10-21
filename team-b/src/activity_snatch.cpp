// Snatch - spec-activity-runtime.md section 6.4. See include/sr3activity/snatch.h.

#include "sr3activity/snatch.h"

#include <algorithm>
#include <functional>

namespace sr3activity {

namespace {

const sr3xtbl::Node* FindDescendant(const sr3xtbl::Node* n, const char* name) {
    if (!n) return nullptr;
    for (const sr3xtbl::Node* c : n->children()) {
        if (sr3xtbl::NameEquals(c->name(), name)) return c;
        if (const sr3xtbl::Node* d = FindDescendant(c, name)) return d;
    }
    return nullptr;
}

}  // namespace

SnatchParams ReadSnatchParams(const sr3xtbl::Document& doc) {
    SnatchParams p;
    const sr3xtbl::Node* scope = doc.table() ? doc.table() : doc.root();
    auto rd = [&](const char* name, double& out) {
        if (const sr3xtbl::Node* n = FindDescendant(scope, name))
            if (n->text()) out = sr3xtbl::ParseFloat(*n->text());
    };
    rd("On_Foot_Dropoff_Dist", p.onFootDropoffDist);
    rd("Vehicle_Dropoff_Dist", p.vehicleDropoffDist);
    rd("In_Range_Pickup_Dist", p.pickupInRangeDist);  // the shipped name of the "in range at 60" field
    return p;
}

SnatchRun::SnatchRun(const SnatchParams& params, const ActivityLevel& level, bool coop) : p_(params) {
    target_ = static_cast<int>(level.hoWinAmount.value_or(0.0));
    durationS_ = coop ? level.coopDuration.value_or(level.duration.value_or(0.0)) : level.duration.value_or(0.0);
}

void SnatchRun::deliver(int count) {
    if (state_ == SnatchState::Running && count > 0) delivered_ += count;
}

SnatchTickResult SnatchRun::tick(double dt) {
    SnatchTickResult r;
    if (state_ != SnatchState::Running) return r;
    elapsedS_ += dt;
    if (delivered_ >= target_ && target_ > 0) {
        state_ = SnatchState::Succeeded;
        r.justSucceeded = true;
    } else if (elapsedS_ >= durationS_) {
        state_ = SnatchState::Failed;
        r.justFailed = true;
    }
    return r;
}

void SnatchRun::playerDied() {
    if (state_ == SnatchState::Running) state_ = SnatchState::Failed;
}

namespace {

std::vector<std::string> Names(const sr3xtbl::Node* block, const char* list, const char* item, const char* field) {
    std::vector<std::string> out;
    for (const sr3xtbl::Node* l : sr3xtbl::Children(block, list))
        for (const sr3xtbl::Node* i : sr3xtbl::Children(l, item))
            if (const std::string* t = sr3xtbl::ChildText(i, field)) out.push_back(*t);
    return out;
}

}  // namespace

SnatchLayout ReadSnatchLayout(const sr3xtbl::Document& doc) {
    SnatchLayout out;
    const sr3xtbl::Node* scope = doc.table() ? doc.table() : doc.root();
    if (const sr3xtbl::Node* info = FindDescendant(scope, "Snatch_Info")) {
        if (const std::string* t = sr3xtbl::ChildText(info, "Navpoint_Name")) out.dropoffNavpoint = *t;
        if (const std::string* t = sr3xtbl::ChildText(info, "Vehicle_Name")) out.playerVehicle = *t;
        if (auto v = sr3xtbl::GetFloat(info, "Recruit_Notification_Dist")) out.recruitDist = *v;
    }
    const sr3xtbl::Node* locs = FindDescendant(scope, "Pickup_Locations");
    for (const sr3xtbl::Node* loc : sr3xtbl::Children(locs, "Pickup_Location")) {
        SnatchPickup p;
        if (const std::string* t = sr3xtbl::ChildText(loc, "Script_Group")) p.scriptGroup = *t;
        p.locationNavpoints = Names(loc, "Location_Navpoints", "Location_Navpoint", "Navpoint_Name");
        for (const sr3xtbl::Node* comps : sr3xtbl::Children(loc, "Pickup_Complications"))
            for (const sr3xtbl::Node* type : sr3xtbl::Children(comps, "Pickup_Complication_Type"))
                for (const sr3xtbl::Node* c : type->children()) {
                    const bool rescue = sr3xtbl::NameEquals(c->name(), "Ho_Rescue");
                    if (!rescue && !sr3xtbl::NameEquals(c->name(), "Standard_Pickup")) continue;
                    SnatchPickupOption o;
                    o.rescue = rescue;
                    o.hos = Names(c, "Hos", "Ho", "Ho_Name");
                    o.pimps = Names(c, "Pimps", "Pimp", "Pimp_Name");
                    o.thugs = Names(c, "Thugs", "Thug", "Thug_Name");
                    if (const std::string* t = sr3xtbl::ChildText(c, "Navpoint_Name")) o.rescueNavpoint = *t;
                    p.options.push_back(std::move(o));
                }
        out.pickups.push_back(std::move(p));
    }
    return out;
}

void SnatchHostages::add(const std::string& name, const ActivityPos& at) {
    h_.push_back({name, at, State::Waiting});
}

SnatchHostages::Update SnatchHostages::update(const ActivityPos& player, bool inVehicle, const ActivityPos& dropoff,
                                              double recruitDist, SnatchRun& run) {
    Update u;
    if (run.state() != SnatchState::Running) return u;
    const bool atDrop = run.atDropoff(Distance(player, dropoff), inVehicle);
    for (Hostage& h : h_) {
        if (h.state == State::Waiting && Distance(player, h.pos) <= recruitDist) {
            h.state = State::Following;
            ++u.recruited;
        }
        if (h.state == State::Following) {
            h.pos = player;
            if (atDrop) {
                h.state = State::Delivered;
                ++u.delivered;
            }
        }
    }
    if (u.delivered > 0) run.deliver(u.delivered);
    return u;
}

int SnatchHostages::count(State s) const {
    return static_cast<int>(std::count_if(h_.begin(), h_.end(), [&](const Hostage& h) { return h.state == s; }));
}

SnatchNotoriety ApplySnatchNotorietyBump(SnatchNotoriety n) {
    n.gang += 1.0;
    n.police = std::clamp(n.police, 0.0, 1.0);
    n.gang = std::clamp(n.gang, 0.0, 3.0);
    return n;
}

}  // namespace sr3activity
