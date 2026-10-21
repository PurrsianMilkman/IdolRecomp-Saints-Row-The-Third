// runPlaytest() frame-loop step: GPS waypoint planner (spec-ai-runtime.md Sec23). Engine side only - the route
// line is drawn by the HUD (spec-hud-runtime.md H.6) from g_playtestGps->route().
#include "viewer_platform.h"
#include "viewer_playtest_session.h"

namespace sr3viewer {

namespace {
void gpsTrace(const std::string& s) { std::printf("gps-trace: %s\n", s.c_str()); }
}  // namespace

void stepGps(PlaytestSession& sess, PlaytestFrame& frm) {
    if (!sess.playtestRoadGraph) return;
    if (!sess.gps_) {
        sess.gpsGraph_.emplace(*sess.playtestRoadGraph);
        sess.gps_.emplace(*sess.gpsGraph_);
        sess.gps_->setTrace(gpsTrace);
        g_playtestGps = &*sess.gps_;
    }
    auto& gps = *sess.gps_;
    const int64_t nowMs = sess.nowMsAccum;

    sr3airuntime::gps::PlayerSample p;
    p.charPos = p.startPos = frm.trafficReferencePos;  // the real player (on foot) or real vehicle (driving)
    // GPS-6 / Sec23.6: mover kind from the driven vehicle's Vehicle_Type class (the rig tuning carries it); on foot otherwise.
    // R4F-5 / Sec23.2 step 1: "current or last vehicle" - remember the class of the last vehicle driven.
    // AW-15: while a craft is flown, the current class is the craft's Vehicle_Type.
    const int curClass = sess.craft.rig.active ? sess.craft.rig.row.cls
                                               : (sess.vehicleRig_ ? sess.vehicleRig_->tuning.vehicleClass : 0);
    if (sess.inVehicle) sess.gpsLastVehicleClass = curClass;
    p.mover = sr3airuntime::gps::MoverForPlayer(sess.inVehicle, curClass, sess.gpsLastVehicleClass);

    // Debug flag: --gps-waypoint=X,Z at frame N (pin; mission kind with --gps-waypoint-mission). Y = 0 as the pause map.
    if (sess.gpsWaypointFlag && !sess.gpsWaypointIssued && sess.frames >= sess.gpsWaypointAt) {
        sess.gpsWaypointIssued = true;
        const sr3airuntime::Vec3 goal{sess.gpsWaypointX, 0.0, sess.gpsWaypointZ};
        std::printf("gps-trace: debug waypoint %s at (%.1f, %.1f) frame %d player=(%.1f, %.1f)\n",
                    sess.gpsWaypointMission ? "mission" : "pin", goal.x, goal.z, sess.frames, p.charPos.x, p.charPos.z);
        if (sess.gpsWaypointMission) gps.SetMission(p, goal, nowMs);
        else gps.SetPin(p, goal, nowMs);
    }

    // Lua waypoint_add / waypoint_remove (Sec23.2): mission kind. Mode bit 1 = local apply.
    if (sess.hudEs) {
        for (const auto& rq : sess.hudEs->drainWaypointRequests()) {
            // Sec23.2 table: mode bit 2 sends net message 0x0C (waypoint_add) / 0x0E (clear); bit 1 applies locally. There is no
            // network layer in this port, so bit 2 is only reported; bit 1 is applied below.
            if (rq.mode & 2) std::printf("gps-trace: lua waypoint mode %d bit 2 (net message) not modelled: no network layer\n", rq.mode);
            if (!(rq.mode & 1)) continue;
            if (rq.remove) {
                ++sess.gpsLuaRemoves;
                std::printf("gps-trace: lua waypoint_remove (mode %d)\n", rq.mode);
                gps.Clear(sr3airuntime::gps::Kind::Mission, nowMs);
                continue;
            }
            ++sess.gpsLuaAdds;
            sr3luahost::EngineState* es = sess.hudEs;
            std::optional<sr3luahost::WorldPosition> wp;
            const std::string name = rq.name;
            bool live = false;
            auto cit = es->characters().find(name);
            if (cit != es->characters().end() && cit->second.worldPosition.known()) {
                wp = cit->second.worldPosition.get();
                live = true;
            } else {
                sr3luahost::OpenValue<sr3luahost::WorldPosition> tmp("gps waypoint target", "spec-ai-runtime.md Sec23.2");
                if (es->tryResolveWorldPositionFromZoneData(name, tmp)) wp = tmp.get();
            }
            if (!wp) {
                ++sess.gpsLuaUnresolved;
                std::printf("gps-trace: lua waypoint_add('%s') mode %d: position unresolved, ignored\n", name.c_str(), rq.mode);
                continue;
            }
            std::printf("gps-trace: lua waypoint_add('%s') mode %d -> (%.1f, %.1f)%s\n", name.c_str(), rq.mode, wp->x, wp->z,
                        live ? " tracked" : "");
            const sr3airuntime::Vec3 goal{wp->x, wp->y, wp->z};
            if (live) {  // a vehicle/character/mover goal follows the object (Sec23.2 kind 1, +0xB10)
                gps.SetMission(p, goal, nowMs, [es, name](sr3airuntime::Vec3& out) {
                    auto it = es->characters().find(name);
                    if (it == es->characters().end() || !it->second.worldPosition.known()) return false;
                    const auto& w = it->second.worldPosition.get();
                    out = {w.x, w.y, w.z};
                    return true;
                });
            } else {
                gps.SetMission(p, goal, nowMs);
            }
        }
    }

    gps.Update(p, nowMs);
    if (gps.isPlaced()) {
        const auto& r = gps.route();
        const double d = std::hypot(p.startPos.x - r.goal.x, p.startPos.z - r.goal.z);
        sess.gpsMinDistToGoal = std::min(sess.gpsMinDistToGoal, d);
        if (sess.frames % 300 == 0)
            std::printf("gps-trace: frame %d kind=%d dist_to_goal=%.1f route_lanes=%zu current_lane=%zu route_len=%.1f direct=%d\n",
                        sess.frames, static_cast<int>(r.kind), d, r.lanes.size(), r.currentLane, r.lengthMeters, r.direct ? 1 : 0);
    }
}

void printGpsTrace(const PlaytestSession& sess) {
    if (!sess.gps_) return;
    const auto& st = sess.gps_->stats();
    if (st.plans + st.replans + st.arrivals + st.clears + sess.gpsLuaAdds + sess.gpsLuaRemoves == 0) return;
    std::printf("gps-trace: SUMMARY plans=%u replans=%u arrivals=%u clears=%u failed_plans=%u direct_commits=%u ignored_dup=%u "
                "lua_adds=%llu lua_removes=%llu lua_unresolved=%llu last_route_len=%.1f last_cost=%.1f last_expanded=%u "
                "placed_at_end=%d min_dist_to_goal=%.1f\n",
                st.plans, st.replans, st.arrivals, st.clears, st.failedPlans, st.directCommits, st.ignoredDup,
                static_cast<unsigned long long>(sess.gpsLuaAdds), static_cast<unsigned long long>(sess.gpsLuaRemoves),
                static_cast<unsigned long long>(sess.gpsLuaUnresolved), st.lastLength, st.lastCost, st.lastExpanded,
                sess.gps_->isPlaced() ? 1 : 0, sess.gpsMinDistToGoal > 1e29 ? -1.0 : sess.gpsMinDistToGoal);
}

}  // namespace sr3viewer
