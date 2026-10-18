// runPlaytest() split: frame-loop steps - near-field traffic, peds and roadblock tick.
// Code moved verbatim out of the former single-function runPlaytest() (see viewer_playtest_session.h).
#include "viewer_platform.h"
#include "viewer_playtest_session.h"

namespace sr3viewer {

void stepTrafficSignals(PlaytestSession& sess, PlaytestFrame& frm) {
    auto& heldFacing = sess.heldFacing;
    auto& playtestRoadGraph = sess.playtestRoadGraph;
    auto& finalGroundY = sess.finalGroundY;
    auto& vehicleRig = *sess.vehicleRig_;
    auto& inVehicle = sess.inVehicle;
    auto& signalController = *sess.signalController_;
    auto& signalSwitchCount = sess.signalSwitchCount;
    auto& signalAmberCount = sess.signalAmberCount;
    auto& nowMsAccum = sess.nowMsAccum;
    const auto& focusX = frm.focusX;
    const auto& focusZ = frm.focusZ;
    const auto& trafficReferencePos = frm.trafficReferencePos;
    const auto& trafficReferenceHeadingRad = frm.trafficReferenceHeadingRad;
        // ===================== NEAR-FIELD TRAFFIC + ROADBLOCK TICK (THIS TASK) =====================
        // Both tick from the SAME live focusX/finalGroundY/focusZ position
        // just reconciled against streaming above - the REAL player (on
        // foot) or REAL vehicle (driving) position, never a synthetic
        // script, even under --no-input. heldFacing/vehicleRig.sim->
        // state_.heading supply a live heading for the near-field spawn-
        // candidate bias and the roadblock anchor's own direction resolver.
        frm.trafficReferencePos = sr3airuntime::Vec3{static_cast<double>(focusX), static_cast<double>(finalGroundY),
                                                      static_cast<double>(focusZ)};
        frm.trafficReferenceHeadingRad = inVehicle ? vehicleRig.sim->state_.heading : sr3player::HeadingFromForward(heldFacing);
        frm.trafficReferenceForward = sr3airuntime::Vec3{std::cos(trafficReferenceHeadingRad), 0.0,
                                                          std::sin(trafficReferenceHeadingRad)};

        // ---- Sec20.6.13 traffic signals: one cursor-walk update per frame ----
        if (playtestRoadGraph) {
            const auto signalResident = [&](size_t j) {
                const auto& jp = playtestRoadGraph->junctions[j].pos;
                return std::hypot(static_cast<double>(jp.x) - trafficReferencePos.x,
                                  static_cast<double>(jp.z) - trafficReferencePos.z) <= kSignalResidentRadius;
            };
            signalController.Update(nowMsAccum, signalResident);
            for (const auto& ev : signalController.DrainEvents()) {
                (ev.kind == 1 ? signalSwitchCount : signalAmberCount) += 1;
                const auto& jp = playtestRoadGraph->junctions[ev.junction].pos;
                const double dj = std::hypot(static_cast<double>(jp.x) - trafficReferencePos.x,
                                             static_cast<double>(jp.z) - trafficReferencePos.z);
                if (dj <= kSignalLogRadius)
                    std::printf("signal   : t=%lld ms junction %u (%.0f,%.0f, %.0f m from player) %s -> phase %u\n",
                                static_cast<long long>(ev.timeMs), ev.junction, static_cast<double>(jp.x),
                                static_cast<double>(jp.z), dj, ev.kind == 1 ? "SWITCH" : "AMBER", ev.phase);
            }
        }
}

void stepTrafficVehicles(PlaytestSession& sess, PlaytestFrame& frm) {
    auto& currentSpawnCategory = sess.currentSpawnCategory;
    auto& playtestRoadGraph = sess.playtestRoadGraph;
    const auto& liveNotorietyLevel = sess.liveNotorietyLevel;
    auto& inVehicle = sess.inVehicle;
    auto& nearFieldLive = sess.nearFieldLive;
    auto& nearFieldNextId = sess.nearFieldNextId;
    auto& nearFieldDriver = *sess.nearFieldDriver_;
    auto& nearFieldToggles = *sess.nearFieldToggles_;
    auto& nearFieldRng = *sess.nearFieldRng_;
    auto& nearFieldPickUniform = sess.nearFieldPickUniform;
    auto& fpCamCur = sess.fpCamCur;
    auto& fpCamPrev = sess.fpCamPrev;
    auto& fpCamFwd = sess.fpCamFwd;
    auto& fpCamSamples = sess.fpCamSamples;
    auto& freeroamVehDespawned = sess.freeroamVehDespawned;
    auto& freeroamVehPasses = sess.freeroamVehPasses;
    auto& freeroamHavePrev = sess.freeroamHavePrev;
    sr3airuntime::TrafficLaneGraph& trafficLaneGraph = *sess.trafficLaneGraph_p;
    auto& nearFieldVehicleLaneState = sess.nearFieldVehicleLaneState;
    auto& nearFieldVehicleHeading = sess.nearFieldVehicleHeading;
    auto& nearFieldVehicleSpeed = sess.nearFieldVehicleSpeed;
    auto& nearFieldVehicleStartPosForTrace = sess.nearFieldVehicleStartPosForTrace;
    auto& overtakeGateFireCountForTrace = sess.overtakeGateFireCountForTrace;
    auto& nearFieldVehicleBrakeMemory = sess.nearFieldVehicleBrakeMemory;
    auto& signalController = *sess.signalController_;
    auto& nearFieldVehicleStopLine = sess.nearFieldVehicleStopLine;
    auto& nearFieldVehicleSignalWait = sess.nearFieldVehicleSignalWait;
    auto& nearFieldVehicleProbe = sess.nearFieldVehicleProbe;
    auto& nearFieldVehicleYield = sess.nearFieldVehicleYield;
    auto& signalCarStops = sess.signalCarStops;
    auto& signalCarReleases = sess.signalCarReleases;
    auto& stopLineCarStops = sess.stopLineCarStops;
    auto& stopLineCarReleases = sess.stopLineCarReleases;
    auto& signalCarFramesOnControlled = sess.signalCarFramesOnControlled;
    auto& signalCarFramesOnRedAmber = sess.signalCarFramesOnRedAmber;
    auto& laneRequeryCount = sess.laneRequeryCount;
    auto& obstacleProbeRuns = sess.obstacleProbeRuns;
    auto& obstacleProbeReuses = sess.obstacleProbeReuses;
    auto& yieldBrakeFrames = sess.yieldBrakeFrames;
    auto& yieldStops = sess.yieldStops;
    auto& yieldRuns = sess.yieldRuns;
    auto& yieldConflictsFound = sess.yieldConflictsFound;
    auto& signalControlledLane = sess.signalControlledLane;
    auto& nowMsAccum = sess.nowMsAccum;
    auto& todFraction = sess.todFraction;
    auto& dt = frm.dt;
    const auto& trafficReferencePos = frm.trafficReferencePos;
        // ---- Near-field (fully-simulated) ambient-vehicle traffic - ALWAYS ON. ----
        // Free-roam population (spec-ai-runtime.md Sec20.6.1, freeroam_population.h): target live count
        // (Default Spawn category = CHOSEN stand-in, hood->category Sec20.6.8 not wired), 130 m ring +
        // moving cone, distance despawn 140/65/+60 m. Camera velocity from last frame's position.
        {
            // RG-6 (Sec20.6.1(b)/(d)): measured from the CAMERA position (previous frames), displacement x a fixed
            // 30 (not the real dt). Spawn cone velocity = delta x 30; despawn moving gate = dot(camFwd, delta) x 30.
            // Until two camera samples exist the camera is taken as still.
            const sr3airuntime::Vec3 fpCamDelta =
                fpCamSamples >= 2 ? sr3airuntime::Vec3{fpCamCur.x - fpCamPrev.x, fpCamCur.y - fpCamPrev.y, fpCamCur.z - fpCamPrev.z}
                                  : sr3airuntime::Vec3{0, 0, 0};
            const sr3airuntime::Vec3 fpCamVel = sr3airuntime::CameraVelocityFromDisplacement(fpCamDelta);
            // Centre of the candidate ring = the camera position (Sec20.6.1(c)), horizontal only.
            const sr3airuntime::Vec3 fpCamCentre =
                fpCamSamples >= 1 ? sr3airuntime::Vec3{fpCamCur.x, trafficReferencePos.y, fpCamCur.z} : trafficReferencePos;
            freeroamHavePrev = true;
            const int fpTodIdx = sr3airuntime::TodDensityIndexFromDayFraction(todFraction);
            // Sec20.6.8: category from the hood under the camera (Default Spawn when no hood / no data).
            const sr3airuntime::SpawnCategoryDensity fpCategory = currentSpawnCategory(trafficReferencePos);
            const int fpVehTarget = sr3airuntime::VehicleTargetLiveCount(fpCategory, fpTodIdx,
                                                                         /*notoriety=*/liveNotorietyLevel(), inVehicle);
            const bool nfRunMaintenance =
                nearFieldDriver.ShouldRunMaintenancePass(nowMsAccum, nearFieldToggles, /*zoneDataLoaded=*/true);
            const bool nfRunDespawnSchedule = nearFieldDriver.ShouldRunDespawnSchedule(nowMsAccum);
            if (nfRunMaintenance || nfRunDespawnSchedule) {
                std::vector<sr3airuntime::Vec3> nfCandidates;
                // spawn only while below the target live count (Sec20.6.1(b)); at most one per pass
                const bool fpSpawn = nfRunMaintenance && static_cast<int>(nearFieldLive.size()) < fpVehTarget;
                if (fpSpawn && playtestRoadGraph) {
                    // RG-4: live vehicles per segment ("Segment full": 3, or 5 on a bit-4 segment).
                    g_roadOccupancy.Clear();
                    for (const auto& lv : nearFieldLive) {
                        const auto ls = nearFieldVehicleLaneState.find(lv.id);
                        if (ls != nearFieldVehicleLaneState.end() && ls->second.valid)
                            g_roadOccupancy.Add(static_cast<uint32_t>(ls->second.currentSegmentIndex));
                    }
                    // RG-7: bit-4 highway rule. The player's current segment (player +0x20dc) is tracked by the
                    // engine by means the spec does not give; CHOSEN stand-in: while driving, the nearest road
                    // segment lane within 10 m of the player.
                    g_roadHighwayOnly = false;
                    if (inVehicle) {
                        double bestD = 100.0;  // 10 m squared
                        bool onHwy = false;
                        for (uint32_t s : playtestRoadGraph->SegmentsNear(trafficReferencePos.x, trafficReferencePos.z, 10.0)) {
                            sr3airuntime::Vec3 lp;
                            size_t np = 1;
                            sr3airuntime::ProjectOntoLane(*playtestRoadGraph, s, trafficReferencePos.x, trafficReferencePos.z, lp, np);
                            const double d2 = (lp.x - trafficReferencePos.x) * (lp.x - trafficReferencePos.x) +
                                              (lp.z - trafficReferencePos.z) * (lp.z - trafficReferencePos.z);
                            if (d2 < bestD) {
                                bestD = d2;
                                onHwy = (playtestRoadGraph->segments[s].flags & sr3zone::kSegFlagHighway) != 0;
                            }
                        }
                        const int onHighway = g_roadOccupancy.CountWhere([&](uint32_t s) {
                            return s < playtestRoadGraph->segments.size() &&
                                   (playtestRoadGraph->segments[s].flags & sr3zone::kSegFlagHighway) != 0;
                        });
                        g_roadHighwayOnly = sr3airuntime::HighwayRuleActive(onHwy, onHighway, fpVehTarget);
                    }
                }
                if (fpSpawn) {
                    nfCandidates = GatherNearFieldSpawnCandidatesAroundPlayer(fpCamCentre, fpCamVel, nearFieldRng);
                }
                // distance despawn replaces the old flat 100 m predicate (Sec20.6.1(d))
                if (nfRunMaintenance || nfRunDespawnSchedule) {
                    sr3airuntime::VehicleDespawnContext dc;
                    dc.playerOnFoot = !inVehicle;
                    dc.cameraMoving = sr3airuntime::DespawnCameraMoving(fpCamFwd, fpCamDelta);
                    const size_t before = nearFieldLive.size();
                    // Same Sec20.6.1(d) predicate for any car position (ambient, and the cars the player parked).
                    auto vehDespawn = [&](const sr3airuntime::Vec3& pos) {
                        const double ddx = pos.x - trafficReferencePos.x;
                        const double ddz = pos.z - trafficReferencePos.z;
                        dc.behindCamera = sr3airuntime::VehicleBehindCameraMotion(
                            {pos.x - fpCamCur.x, pos.y - fpCamCur.y, pos.z - fpCamCur.z}, fpCamDelta);
                        // Sec20.6.1(d) "visible": inside the previous frame's camera frustum
                        // (occlusion test 0x005dcad0 not modelled)
                        dc.visible = g_haveFrustum && sr3airuntime::BoxInFrustum(g_frustumViewProj, {pos.x, pos.y, pos.z}, 1.5);
                        const double dist = inVehicle ? std::sqrt(ddx * ddx + ddz * ddz)
                                                      : sr3airuntime::Distance(pos, trafficReferencePos);
                        return sr3airuntime::ShouldDespawnVehicleByDistance(dc, dist);
                    };
                    nearFieldLive.erase(std::remove_if(nearFieldLive.begin(), nearFieldLive.end(),
                                                       [&](const sr3airuntime::NearFieldVehicle& v) { return vehDespawn(v.pos); }),
                                        nearFieldLive.end());
                    freeroamVehDespawned += before - nearFieldLive.size();
                    // Cars the player left parked (enter-ambient-cars): same despawn rule, so they persist while near.
                    // The original parked car is exempt (CHOSEN: it is the mission/smoke-test car).
                    sess.parkedVehicles.erase(
                        std::remove_if(sess.parkedVehicles.begin(), sess.parkedVehicles.end(),
                                       [&](const PlaytestSession::ParkedVehicle& pk) {
                                           return !pk.isGenki &&
                                                  vehDespawn(sr3airuntime::Vec3{pk.x, static_cast<double>(pk.y), pk.z});
                                       }),
                        sess.parkedVehicles.end());
                    // The rig's own parked car, when it is an ambient car the player left: when it despawns the rig
                    // goes back to the original parked car (still in parkedVehicles; it never despawns).
                    if (!inVehicle && !sess.rigCarIsGenki && sess.vehicleRig_ && sess.vehicleRig_->sim) {
                        auto& rst = sess.vehicleRig_->sim->state_;
                        if (vehDespawn(sr3airuntime::Vec3{rst.posX, static_cast<double>(sess.vehicleGroundY), rst.posZ})) {
                            for (size_t i = 0; i < sess.parkedVehicles.size(); ++i) {
                                if (!sess.parkedVehicles[i].isGenki) continue;
                                const auto g = sess.parkedVehicles[i];
                                sess.parkedVehicles.erase(sess.parkedVehicles.begin() + static_cast<std::ptrdiff_t>(i));
                                rst.posX = g.x; rst.posZ = g.z; rst.heading = g.heading;
                                rst.velForwardBody = 0.0; rst.velRightBody = 0.0; rst.yawRate = 0.0;
                                sess.rigCarIsGenki = true;
                                sess.vehicleGroundFollower_.emplace(static_cast<double>(g.y));
                                sess.vehicleGroundY = g.y;
                                sess.vehiclePromptShown = false;
                                std::printf("[ambient-entry] parked ambient car despawned (left behind); rig back to the original car\n");
                                break;
                            }
                        }
                    }
                }
                sr3airuntime::RunNearFieldVehicleMaintenancePass(
                    // runDespawn stays false DELIBERATELY: the Sec20.6.1(d) camera gate above already is the despawn;
                    // true would re-add the old flat 100 m rule (kNearFieldDespawnDistanceSq) on top of it.
                    nearFieldLive, /*runDespawn=*/false, fpSpawn, trafficReferencePos,
                    kNearFieldDespawnDistanceSq, /*eligibleHumanCount=*/0, nfCandidates, nearFieldPickUniform,
                    nearFieldNextId);
                if (fpSpawn) ++freeroamVehPasses;
            }

            // ---- Prune the two side-car driving-state maps against the
            // live population (THIS TASK) - the maintenance pass above may
            // have just despawned vehicles; same "erase on despawn" hygiene
            // nearFieldPedFleeDestination/nearFieldPedWanderDestination
            // already apply to the ped side-car maps elsewhere in this
            // function. O(live) per maintenance tick, not every frame -
            // cheap, and this block only runs on the SAME ticks the
            // maintenance pass itself runs. ----
            if (nfRunMaintenance || nfRunDespawnSchedule) {
                for (auto it = nearFieldVehicleLaneState.begin(); it != nearFieldVehicleLaneState.end();) {
                    const bool stillLive =
                        std::any_of(nearFieldLive.begin(), nearFieldLive.end(),
                                    [id = it->first](const sr3airuntime::NearFieldVehicle& v) { return v.id == id; });
                    if (stillLive) {
                        ++it;
                    } else {
                        nearFieldVehicleHeading.erase(it->first);
                        nearFieldVehicleSpeed.erase(it->first);
                        nearFieldVehicleBrakeMemory.erase(it->first);
                        nearFieldVehicleStopLine.erase(it->first);
                        nearFieldVehicleSignalWait.erase(it->first);
                        nearFieldVehicleProbe.erase(it->first);
                        nearFieldVehicleYield.erase(it->first);
                        it =nearFieldVehicleLaneState.erase(it);
                    }
                }
            }
        }

        // ---- Near-field ambient-vehicle LANE-FOLLOWING drive tick (THIS
        // TASK) - runs EVERY frame (not gated by the maintenance timers
        // above), same "driving is continuous, spawning/despawning is
        // throttled" split the near-field PED population's own flee/wander
        // tick already establishes just below. A vehicle not yet in
        // nearFieldVehicleLaneState (newly spawned this tick, or the very
        // first tick after bootstrap) is snapped onto the nearest lane-
        // graph segment (NearestLaneGraphSegmentStart(), CHOSEN placement
        // rule - see that function's own comment) and given heading 0 as a
        // starting value only; the first real StepLaneFollow() call
        // immediately recomputes a real steer/throttle command from its
        // actual position.
        //
        // ALSO SNAPS nfv.pos ONTO the chosen segment's own first point
        // (CHOSEN, same placement-rule scope as the segment choice itself -
        // Sec18 never addresses initial placement at all). Without this, a
        // vehicle that spawned somewhere off GatherNearFieldSpawnCandidates
        // AroundPlayer()'s own wider radius (which has no awareness of this
        // task's own small lane-graph fixture) could start arbitrarily far
        // from the graph; StepLaneFollow()'s own alignment-AND-distance
        // throttle gate (Sec18.6 step 4, see traffic_lane_follower.h's own
        // file header item 5 second note) requires the vehicle to already
        // be near the lane before it ever receives nonzero throttle - a
        // genuine deadlock caught during this task's own live playtest
        // verification (vehicles spun in place, heading changing every
        // frame, position frozen, across 600 real frames) and fixed here by
        // placement, not by loosening the real confirmed gate. ----
        // Sec20.6.7(c) junction yield input: live cars per lane segment with their distance to the lane end, built
        // once per frame from the previous frame's state (one-frame lag, as the 150-300 ms yield timer is coarser).
        const auto laneEndDistance = [&](const sr3airuntime::LaneFollowState& st, double px, double pz) {
            double dEndLocal = 0.0;
            if (!st.valid || st.currentSegmentIndex >= trafficLaneGraph.segmentCount()) return dEndLocal;
            const auto& poly = trafficLaneGraph.segment(st.currentSegmentIndex).polyline;
            if (poly.empty()) return dEndLocal;
            for (size_t k = std::clamp<size_t>(st.pointCursor, 0, poly.size() - 1); k < poly.size(); ++k) {
                dEndLocal += std::hypot(static_cast<double>(poly[k].x) - px, static_cast<double>(poly[k].z) - pz);
                px = poly[k].x;
                pz = poly[k].z;
            }
            return dEndLocal;
        };
        std::unordered_map<size_t, std::vector<sr3airuntime::YieldOther>> yieldCarsBySegment;
        if (playtestRoadGraph) {
            for (const auto& other : nearFieldLive) {
                const auto st = nearFieldVehicleLaneState.find(other.id);
                const auto sp = nearFieldVehicleSpeed.find(other.id);
                if (st == nearFieldVehicleLaneState.end() || !st->second.valid) continue;
                yieldCarsBySegment[st->second.currentSegmentIndex].push_back(
                    {other.id, laneEndDistance(st->second, other.pos.x, other.pos.z),
                     sp != nearFieldVehicleSpeed.end() ? sp->second : 0.0});
            }
        }
        for (auto& nfv : nearFieldLive) {
            auto stateIt = nearFieldVehicleLaneState.find(nfv.id);
            if (stateIt == nearFieldVehicleLaneState.end() && playtestRoadGraph) {
                // REAL GRAPH: the car was spawned at a lane point chosen by GatherRoadVehicleSpawnPoints();
                // look its segment up (exact lanePos match), else project onto the nearest lane within 25 m.
                // The follower starts mid-lane (cursor = first polyline point ahead) heading along the lane.
                size_t startSeg = trafficLaneGraph.segmentCount();
                size_t nextPt = 1;
                for (const auto& rp : g_lastRoadSpawnPoints) {
                    if (rp.lanePos.x == nfv.pos.x && rp.lanePos.z == nfv.pos.z) {
                        startSeg = rp.segment;
                        nextPt = rp.laneNextPoint;
                        break;
                    }
                }
                if (startSeg == trafficLaneGraph.segmentCount()) {
                    double bestD = 1e300;
                    for (uint32_t s : playtestRoadGraph->SegmentsNear(nfv.pos.x, nfv.pos.z, 25.0)) {
                        sr3airuntime::Vec3 lp;
                        size_t np = 1;
                        sr3airuntime::ProjectOntoLane(*playtestRoadGraph, s, nfv.pos.x, nfv.pos.z, lp, np);
                        const double d = (lp.x - nfv.pos.x) * (lp.x - nfv.pos.x) + (lp.z - nfv.pos.z) * (lp.z - nfv.pos.z);
                        if (d < bestD) { bestD = d; startSeg = s; nextPt = np; }
                    }
                } else {
                    ++g_roadVehLaneSpawns;
                }
                auto laneState = sr3airuntime::LaneFollowState::Begin(trafficLaneGraph, startSeg);
                double startHeading = 0.0;
                if (startSeg < trafficLaneGraph.segmentCount()) {
                    const auto& poly = trafficLaneGraph.segment(startSeg).polyline;
                    nextPt = std::clamp<size_t>(nextPt, 1, poly.size() - 1);
                    laneState.pointCursor = nextPt;
                    startHeading = std::atan2(static_cast<double>(poly[nextPt].z) - poly[nextPt - 1].z,
                                              static_cast<double>(poly[nextPt].x) - poly[nextPt - 1].x);
                }
                stateIt = nearFieldVehicleLaneState.emplace(nfv.id, laneState).first;
                nearFieldVehicleHeading[nfv.id] = startHeading;
                nearFieldVehicleSpeed[nfv.id] = 0.0;
                nearFieldVehicleStartPosForTrace[nfv.id] = nfv.pos;
            }
            if (stateIt == nearFieldVehicleLaneState.end()) {
                const size_t startSeg = NearestLaneGraphSegmentStart(trafficLaneGraph, nfv.pos);
                stateIt = nearFieldVehicleLaneState
                              .emplace(nfv.id, sr3airuntime::LaneFollowState::Begin(trafficLaneGraph, startSeg))
                              .first;
                nearFieldVehicleHeading[nfv.id] = 0.0;
                nearFieldVehicleSpeed[nfv.id] = 0.0;
                if (startSeg < trafficLaneGraph.segmentCount() &&
                    !trafficLaneGraph.segment(startSeg).polyline.empty()) {
                    const auto& p0 = trafficLaneGraph.segment(startSeg).polyline.front();
                    nfv.pos.x = static_cast<double>(p0.x);
                    nfv.pos.z = static_cast<double>(p0.z);
                }
                // overtake-gate-18.6b task diagnostic (see this map's own
                // declaration comment): record the AFTER-snap starting
                // position once, the first tick this vehicle gets a lane
                // state, so the end-of-run summary can report real net
                // displacement.
                nearFieldVehicleStartPosForTrace[nfv.id] = nfv.pos;
            }
            double& heading = nearFieldVehicleHeading[nfv.id];
            double& trackedSpeedMs = nearFieldVehicleSpeed[nfv.id];  // default-constructed 0.0 on first use

            // Sec18.6a overtake-gate task (2026-10-10): resolve the "nearest
            // near-field vehicle AHEAD of this one" stand-in for Sec18.6a's
            // own unresolved "second record" via the shared
            // ResolveNearestNearFieldVehicleAhead() helper above (factored
            // out, not left inline, so a future port of Sec18.6's own
            // separate 0xe/0x10-state steering-write path can reuse the same
            // stand-in resolution - see that helper's own doc comment for
            // why this is a code-sharing convenience only, not a claim the
            // two spec sections name the same engine object). O(live) per
            // vehicle per frame across the whole near-field population -
            // acceptable at this population's own small live-cap sizes (same
            // "cheap enough at this scale" reasoning the rest of this
            // near-field block already relies on). "Ahead" is tested against
            // THIS tick's own not-yet-updated heading (the previous tick's
            // final heading) - a one-tick staleness, same acceptable lag
            // the cached-target re-query mechanism elsewhere in this file
            // already tolerates.
            std::optional<sr3airuntime::OvertakeCandidate> overtakeCandidate = ResolveNearestNearFieldVehicleAhead(
                nearFieldLive, nfv.id, nfv.pos.x, nfv.pos.z, heading, nearFieldVehicleHeading, nearFieldVehicleSpeed);

            sr3airuntime::LaneFollowCommand cmd = sr3airuntime::StepLaneFollow(
                trafficLaneGraph, stateIt->second, nfv.pos.x, nfv.pos.z, heading, sr3airuntime::LaneFollowConfig{},
                trackedSpeedMs, overtakeCandidate);
            if (cmd.overtakeGateFired) ++overtakeGateFireCountForTrace;  // overtake-gate-18.6b task diagnostic
            if (cmd.requeried) ++laneRequeryCount;
            // CHOSEN kinematic integration - see this block's own
            // declaration comment above (kNearFieldVehicleTurnRateRadPerSec/
            // kNearFieldVehicleCruiseSpeedMs) for why no per-vehicle Havok
            // body exists to write cmd.steer/throttle/brake into instead.
            heading += cmd.steer * kNearFieldVehicleTurnRateRadPerSec * static_cast<double>(dt);
            double speedMs = cmd.throttle > 0.0 ? kNearFieldVehicleCruiseSpeedMs * cmd.throttle : 0.0;
            if (playtestRoadGraph) {
                // RG-12 (Sec20.6.3(a)/(b), Sec20.6.6): on the real graph every lane's cruise limit is 10 m/s and the
                // throttle follows the Sec20.6.3(b) law (0.70 from rest, cap 0.7-1.0, falling to ~0 at the limit).
                // Kinematic body, CHOSEN: acceleration = 3 m/s^2 (the constant the Sec20.6.7(c) yield prediction
                // uses) x that throttle fraction of full; an unaligned car stops (as before).
                const double dtD = static_cast<double>(dt);
                double v = trackedSpeedMs;
                if (cmd.headingAligned) {  // RG-12: heading half of the Sec18.6 gate only (see LaneFollowCommand::headingAligned)
                    v += kNearFieldVehicleAccelMs2 *
                         sr3airuntime::AmbientThrottleLaw(sr3airuntime::kAmbientLaneSpeedMs, v, /*flag=*/false) * dtD;
                    v = std::min(v, sr3airuntime::kAmbientLaneSpeedMs);
                    // RG-13 (Sec20.6.7(a)/(b)): obstacle probe + following law. Probe stand-in (CHOSEN geometry:
                    // the real box extents / physics sweep are not available): nearest other near-field vehicle
                    // ahead of the front within L + 3.45 m and within +-2 m laterally; gap = centre distance minus
                    // two half-lengths.
                    auto& mem = nearFieldVehicleBrakeMemory[nfv.id];
                    mem.second = std::max(0.0, mem.second - dtD);
                    // RF-3 (Sec20.6.7(a)): the probe re-runs only when its timer (vehicle +0x1118) expires, re-armed
                    // with trunc(max(0, (15 - v)/15 x 140)) + 60 ms; between runs the previous result is reused.
                    auto& probe = nearFieldVehicleProbe[nfv.id];
                    probe.timerMs -= dtD * 1000.0;
                    const bool probeRuns = probe.timerMs <= 0.0;
                    if (probeRuns) {
                        probe.timerMs = static_cast<double>(sr3airuntime::ObstacleProbeRearmMs(trackedSpeedMs));
                        ++obstacleProbeRuns;
                        probe.hasLead = false;
                    } else {
                        ++obstacleProbeReuses;
                    }
                    const double hx = std::cos(heading), hz = std::sin(heading);
                    const double probeL = trackedSpeedMs * trackedSpeedMs /
                                              (2.0 * (trackedSpeedMs > 20.0 ? 1.0 : 0.75) * kNearFieldVehicleBrakeDecel) +
                                          2.5 + (mem.second > 0.0 ? 6.0 : 0.0);
                    double bestGap = 1e300, leadU = 0.0;
                    uint64_t leadId = 0;
                    for (const auto& other : nearFieldLive) {
                        if (!probeRuns) break;
                        if (other.id == nfv.id) continue;
                        const double ox = other.pos.x - nfv.pos.x, oz = other.pos.z - nfv.pos.z;
                        const double along = ox * hx + oz * hz, lat = -ox * hz + oz * hx;
                        if (along <= 0.0 || std::fabs(lat) > kNearFieldVehicleProbeHalfWidth ||
                            along - kNearFieldVehicleHalfLength > probeL + 3.45)
                            continue;
                        const double gap = along - 2.0 * kNearFieldVehicleHalfLength;
                        if (gap < bestGap) {
                            bestGap = gap;
                            leadId = other.id;
                            const auto os = nearFieldVehicleSpeed.find(other.id);
                            const auto oh = nearFieldVehicleHeading.find(other.id);
                            leadU = (os != nearFieldVehicleSpeed.end() && oh != nearFieldVehicleHeading.end())
                                        ? os->second * std::cos(oh->second - heading)
                                        : 0.0;
                        }
                    }
                    if (probeRuns && leadId != 0) {
                        const bool recent = mem.second > 0.0 && mem.first == leadId;
                        probe.result = sr3airuntime::FollowingLaw(kNearFieldVehicleBrakeDecel, /*kind=*/0, bestGap,
                                                                  leadU, trackedSpeedMs, recent);
                        probe.hasLead = true;
                        if (probe.result.remember) mem = {leadId, 0.3};
                    }
                    if (probe.hasLead) {  // applied every frame; recomputed only when the timer ran
                        if (probe.result.brake > 0.0) v = std::max(0.0, trackedSpeedMs - probe.result.brake * dtD);
                        v = std::min(v, probe.result.cap);
                    }
                    // Sec20.6.13(f)/(g): signal source (kind 2) and stop-line source (kind 4) on the car's current
                    // lane; both gated on mode 8 + obey bit (CHOSEN: all near-field cars here). The distance is
                    // measured to the end node of the lane the car is on (Sec20.6.11(c)).
                    {
                        const size_t curSeg = stateIt->second.currentSegmentIndex;
                        int waitKind = 0;
                        if (stateIt->second.valid && curSeg < playtestRoadGraph->segments.size()) {
                            const auto& poly = trafficLaneGraph.segment(curSeg).polyline;
                            double dEnd = 0.0;
                            if (!poly.empty()) {
                                const size_t nc = std::clamp<size_t>(stateIt->second.pointCursor, 0, poly.size() - 1);
                                double px = nfv.pos.x, pz = nfv.pos.z;
                                for (size_t k = nc; k < poly.size(); ++k) {
                                    dEnd += std::hypot(static_cast<double>(poly[k].x) - px, static_cast<double>(poly[k].z) - pz);
                                    px = poly[k].x;
                                    pz = poly[k].z;
                                }
                            }
                            const int laneSt = signalController.LaneState(curSeg);
                            if (laneSt != 1) ++signalCarFramesOnRedAmber;
                            if (signalControlledLane.empty()) {
                                signalControlledLane.assign(playtestRoadGraph->segments.size(), 0);
                                for (uint16_t s : playtestRoadGraph->phaseLanes) signalControlledLane[s] = 1;
                            }
                            if (signalControlledLane[curSeg]) {
                                ++signalCarFramesOnControlled;
                                if (std::getenv("SR3_TRACE_SIGNALS") && (signalCarFramesOnControlled % 40) == 1)
                                    std::printf("signal-dbg: car %llu lane %zu state %d flags 0x%x dEnd %.1f v %.2f\n",
                                                static_cast<unsigned long long>(nfv.id), curSeg, laneSt,
                                                playtestRoadGraph->segments[curSeg].flags, dEnd, trackedSpeedMs);
                            }
                            if (sr3airuntime::RunsSignalRule(kNearFieldVehicleMode, kNearFieldVehicleObeysLights) &&
                                sr3airuntime::SignalRuleStops(laneSt, trackedSpeedMs, dEnd, kNearFieldVehicleBrakeDecel)) {
                                auto& smem = nearFieldVehicleBrakeMemory[nfv.id];
                                const bool recent = smem.second > 0.0 && smem.first == kSignalMemoryId;
                                const sr3airuntime::FollowingResult fr = sr3airuntime::FollowingLaw(
                                    kNearFieldVehicleBrakeDecel, /*kind=*/2, dEnd, 0.0, trackedSpeedMs, recent);
                                if (fr.remember) smem = {kSignalMemoryId, 0.3};
                                if (fr.brake > 0.0) v = std::min(v, std::max(0.0, trackedSpeedMs - fr.brake * dtD));
                                v = std::min(v, fr.cap);
                                if (fr.brake > 0.0 || fr.cap < 3.0) waitKind = 1;
                            }
                            if (sr3airuntime::RunsStopLineRule(kNearFieldVehicleMode, kNearFieldVehicleObeysLights) &&
                                (playtestRoadGraph->segments[curSeg].flags & sr3zone::kSegFlagStopLine)) {
                                auto& slm = nearFieldVehicleStopLine[nfv.id];
                                const auto sl = sr3airuntime::StopLineStep(slm, static_cast<int32_t>(curSeg), dEnd, trackedSpeedMs,
                                                                           dtD * 1000.0, kNearFieldVehicleBrakeDecel, false);
                                if (sl.active) {
                                    if (sl.brake > 0.0) v = std::min(v, std::max(0.0, trackedSpeedMs - sl.brake * dtD));
                                    v = std::min(v, sl.cap);
                                    if (sl.brake > 0.0 || sl.cap < 3.0) waitKind = waitKind ? waitKind : 2;
                                }
                            }
                            // Sec20.6.7(c) junction yield (law kind 3 at the start of the conflict segment). Runs on its
                            // own 150-300 ms timer (CHOSEN draw: hash of id and run count) and is skipped while a live
                            // signal (kind 2) or stop-line (kind 4) memory entry exists. Data path and stand-ins:
                            // traffic_yield.h.
                            {
                                auto& yc = nearFieldVehicleYield[nfv.id];
                                yc.timerMs -= dtD * 1000.0;
                                if (yc.timerMs <= 0.0) {
                                    yc.timerMs = 150.0 + static_cast<double>((nfv.id * 37u + yc.runs * 61u) % 151u);
                                    ++yc.runs;
                                    const auto& sm = nearFieldVehicleBrakeMemory[nfv.id];
                                    const bool memLive = sm.second > 0.0 && sm.first == kSignalMemoryId;
                                    if (memLive || nearFieldVehicleStopLine[nfv.id].holding) {
                                        yc.yielding = false;
                                    } else {
                                        sr3airuntime::YieldOwn own;
                                        own.id = nfv.id;
                                        own.seg = curSeg;
                                        own.distToSegEnd = dEnd;
                                        own.speed = trackedSpeedMs;
                                        own.headingX = std::cos(heading);
                                        own.headingZ = std::sin(heading);
                                        own.aB = kNearFieldVehicleBrakeDecel;
                                        own.targetSpeed = sr3airuntime::kAmbientLaneSpeedMs;
                                        own.recentlyBraked = yc.yielding;
                                        const auto dec = sr3airuntime::EvaluateJunctionYield(
                                            *playtestRoadGraph, own, [&](size_t s) {
                                                const auto f = yieldCarsBySegment.find(s);
                                                return f == yieldCarsBySegment.end() ? std::vector<sr3airuntime::YieldOther>{}
                                                                                     : f->second;
                                            });
                                        ++yieldRuns;
                                        if (dec.found) ++yieldConflictsFound;
                                        if (dec.yield && !yc.yielding) yc.travelled = 0.0;
                                        yc.yielding = dec.yield;
                                        if (dec.yield) yc.dist = dec.distToConflict + yc.travelled;
                                    }
                                }
                                if (yc.yielding) {
                                    const double dRem = yc.dist - yc.travelled;
                                    const sr3airuntime::FollowingResult fr = sr3airuntime::FollowingLaw(
                                        kNearFieldVehicleBrakeDecel, /*kind=*/3, dRem, 0.0, trackedSpeedMs, true);
                                    if (fr.brake > 0.0) v = std::min(v, std::max(0.0, trackedSpeedMs - fr.brake * dtD));
                                    v = std::min(v, fr.cap);
                                    yc.travelled += v * dtD;
                                    if (fr.brake > 0.0 || fr.cap < 3.0) ++yieldBrakeFrames;
                                    if (v < 0.3 && !yc.stopped) {
                                        yc.stopped = true;
                                        ++yieldStops;
                                    }
                                } else {
                                    yc.stopped = false;
                                }
                            }
                            // diagnostics: cars stopping / releasing at junctions near the player
                            auto& wait = nearFieldVehicleSignalWait[nfv.id];
                            const bool nearPlayer = std::hypot(nfv.pos.x - trafficReferencePos.x, nfv.pos.z - trafficReferencePos.z) <= kSignalLogRadius;
                            if (waitKind != 0 && v < 0.3 && wait.second == 0) {
                                wait = {static_cast<int32_t>(curSeg), waitKind};
                                (waitKind == 1 ? signalCarStops : stopLineCarStops) += 1;
                                if (nearPlayer)
                                    std::printf("signal   : t=%lld ms car %llu STOPPED at %s on lane %zu (state %d, %.1f m to lane end)\n",
                                                static_cast<long long>(nowMsAccum), static_cast<unsigned long long>(nfv.id),
                                                waitKind == 1 ? "signal" : "stop line", curSeg, laneSt, dEnd);
                            } else if (wait.second != 0 && (waitKind == 0 || wait.first != static_cast<int32_t>(curSeg)) && v > 1.0) {
                                (wait.second == 1 ? signalCarReleases : stopLineCarReleases) += 1;
                                if (nearPlayer)
                                    std::printf("signal   : t=%lld ms car %llu RELEASED from %s (lane %d now state %d)\n",
                                                static_cast<long long>(nowMsAccum), static_cast<unsigned long long>(nfv.id),
                                                wait.second == 1 ? "signal" : "stop line", wait.first,
                                                signalController.LaneState(static_cast<size_t>(wait.first)));
                                wait = {-1, 0};
                            }
                        }
                    }
                } else {
                    v = 0.0;
                }
                speedMs = v;
            }
            trackedSpeedMs = speedMs;  // fed into next tick's own overtake closing-speed gate test
            nfv.pos.x += std::cos(heading) * speedMs * static_cast<double>(dt);
            nfv.pos.z += std::sin(heading) * speedMs * static_cast<double>(dt);
        }
}

void stepTrafficPeds(PlaytestSession& sess, PlaytestFrame& frm) {
    auto& freeroamWeatherPedDensity = sess.freeroamWeatherPedDensity;
    auto& currentSpawnCategory = sess.currentSpawnCategory;
    auto& playtestRoadGraph = sess.playtestRoadGraph;
    const auto& liveNotorietyLevel = sess.liveNotorietyLevel;
    auto& pedAi = sess.pedAi;
    auto& pedAiIds = sess.pedAiIds;
    auto& pedAiCowering = sess.pedAiCowering;
    auto& pedAiPlayerFiring = sess.pedAiPlayerFiring;
    auto& pedAiFleeCount = sess.pedAiFleeCount;
    auto& pedAiCowerCount = sess.pedAiCowerCount;
    auto& nearFieldPedReaction = sess.nearFieldPedReaction;
    auto& nearFieldPedFleeDestination = sess.nearFieldPedFleeDestination;
    auto& nearFieldPedWanderDestination = sess.nearFieldPedWanderDestination;
    auto& nearFieldPedSidewalkSegGeo = sess.nearFieldPedSidewalkSegGeo;
    auto& nearFieldToggles = *sess.nearFieldToggles_;
    auto& freeroamPedDespawned = sess.freeroamPedDespawned;
    auto& signalController = *sess.signalController_;
    auto& nearFieldPedCrossing = sess.nearFieldPedCrossing;
    auto& pedCrossWaits = sess.pedCrossWaits;
    auto& pedCrossCrosses = sess.pedCrossCrosses;
    auto& pedCrossAfterWait = sess.pedCrossAfterWait;
    auto& pedCrossUnsignalled = sess.pedCrossUnsignalled;
    auto& nearFieldPedNav = sess.nearFieldPedNav;
    auto& nearFieldPedWalkSpeed = sess.nearFieldPedWalkSpeed;
    auto& pedNavSearches = sess.pedNavSearches;
    auto& pedNavSearchFails = sess.pedNavSearchFails;
    auto& pedNavArrivals = sess.pedNavArrivals;
    auto& pedNavEdgesWalked = sess.pedNavEdgesWalked;
    auto& pedNavEvent8 = sess.pedNavEvent8;
    auto& pedNavEvent8Due = sess.pedNavEvent8Due;
    auto& nearFieldPedsLive = sess.nearFieldPedsLive;
    auto& nearFieldPedNextId = sess.nearFieldPedNextId;
    auto& nearFieldPedDriver = *sess.nearFieldPedDriver_;
    auto& nearFieldPedRng = *sess.nearFieldPedRng_;
    auto& nearFieldPedPickUniform = sess.nearFieldPedPickUniform;
    auto& nearFieldPedGates = *sess.nearFieldPedGates_;
    auto& nowMsAccum = sess.nowMsAccum;
    auto& todFraction = sess.todFraction;
    auto& dt = frm.dt;
    const auto& trafficReferencePos = frm.trafficReferencePos;
    const auto& trafficReferenceHeadingRad = frm.trafficReferenceHeadingRad;
    const auto& trafficReferenceForward = frm.trafficReferenceForward;
        // ---- Near-field (fully-simulated) ambient-pedestrian population -
        // ALWAYS ON (THIS TASK). Follows the vehicle traffic block just
        // above as its exact precedent: ShouldRunTick() combines
        // near_field_ped_population.h's own dual gate (sidewalksLoaded=true,
        // same stand-in reasoning as zoneDataLoaded=true above; AnyEnabled
        // (ToggleCategory::kPed) against the SAME nearFieldToggles instance
        // the vehicle side already enables every category on by default)
        // with its single randomized tick timer (kNearFieldPedTickMinMs/
        // MaxMs, see this function's own doc-comment update above for why
        // these reuse the vehicle sibling's own 200-300ms range). Spawn-only
        // (no despawn predicate exists for this tier - see
        // near_field_ped_population.h's own file header item 3). ----
        {
            const bool pedRunTick = nearFieldPedDriver.ShouldRunTick(nowMsAccum, nearFieldToggles,
                                                                     /*sidewalksLoaded=*/true, kNearFieldPedTickMinMs,
                                                                     kNearFieldPedTickMaxMs);
            if (pedRunTick) {
                const std::vector<sr3airuntime::SidewalkSegment> pedCandidateSegments =
                    GatherNearFieldPedSpawnCandidateSegmentsAroundPlayer(trafficReferencePos,
                                                                         trafficReferenceHeadingRad, nearFieldPedRng);
                // THIS TASK (2026-10-10, spec-ai-runtime.md Sec20.1 re-read):
                // real per-tick visibility gate, replacing the prior unit's
                // own always-false CHOSEN default. Captures THIS frame's
                // trafficReferencePos/trafficReferenceForward by value (both
                // are per-iteration locals, re-declared each frame - capturing
                // by reference here would dangle past this statement) and
                // defers to near_field_ped_population.h's own
                // ApproxVisibleToCamera (CHOSEN horizontal-FOV cone, not a
                // recovered engine frustum - see that function's own comment).
                // Free-roam population (spec-ai-runtime.md Sec20.6.2, freeroam_population.h):
                // far reject 90 m, near reject 10 m (header constant), visible points < 70 m
                // rejected, crowd cap (>= 7 peds within 20 m), lateral offset uniform +-width/2
                // (3 m walkway stand-in), target live count with weather PedDensity (Light Rain
                // 0.5 = playtest stage, per kWeatherStageForPlaytest) and Default Spawn category
                // (CHOSEN stand-in, hood mapping Sec20.6.8 not wired).
                nearFieldPedGates.cameraFarRejectDist = 90.0;
                // Real graph: the per-edge walkway width (SidewalkSegment::width) drives the +-width/2
                // offset inside SelectNearFieldPedSpawn; the 3 m stand-in applies only without a graph.
                nearFieldPedGates.sidewaysOffsetDistance =
                    playtestRoadGraph
                        ? 0.0
                        : sr3airuntime::PedLateralOffsetFromRand(sr3airuntime::RandomUnitFloat01(nearFieldPedRng), 3.0);
                nearFieldPedGates.isVisibleToCamera = [pos = trafficReferencePos,
                                                        fwd = trafficReferenceForward](const sr3airuntime::Vec3& p) {
                    return sr3airuntime::Distance(p, pos) < 70.0 && sr3airuntime::ApproxVisibleToCamera(p, pos, fwd);
                };
                // crowd rule (reject 5) hooked via the point-validity gate: ground ray/clearance stay stand-ins
                nearFieldPedGates.isGroundValid = [&nearFieldPedsLive](const sr3airuntime::Vec3& p) {
                    int near20 = 0;
                    for (const auto& q : nearFieldPedsLive)
                        if (sr3airuntime::Distance(q.pos, p) <= 20.0) ++near20;
                    return near20 < 7;
                };
                const int fpPedTarget = sr3airuntime::PedTargetLiveCount(
                    currentSpawnCategory(trafficReferencePos), sr3airuntime::TodDensityIndexFromDayFraction(todFraction), liveNotorietyLevel(),
                    freeroamWeatherPedDensity);
                // ped despawn: horizontal > 90 m from the camera (Sec20.6.2(d))
                {
                    std::vector<uint64_t> gone;
                    for (const auto& q : nearFieldPedsLive) {
                        const double ddx = q.pos.x - trafficReferencePos.x, ddz = q.pos.z - trafficReferencePos.z;
                        if (sr3airuntime::ShouldDespawnPedByDistance(std::sqrt(ddx * ddx + ddz * ddz))) gone.push_back(q.id);
                    }
                    for (uint64_t id : gone) {
                        nearFieldPedsLive.erase(std::remove_if(nearFieldPedsLive.begin(), nearFieldPedsLive.end(),
                                                               [id](const sr3airuntime::NearFieldPed& q) { return q.id == id; }),
                                                nearFieldPedsLive.end());
                        nearFieldPedReaction.erase(id);
                        nearFieldPedFleeDestination.erase(id);
                        nearFieldPedWanderDestination.erase(id);
                        nearFieldPedSidewalkSegGeo.erase(id);
                    }
                    freeroamPedDespawned += gone.size();
                }
                sr3airuntime::RunNearFieldPedSpawnPass(nearFieldPedsLive, static_cast<size_t>(fpPedTarget),
                                                       /*eligibleHumanCount=*/0, pedCandidateSegments,
                                                       nearFieldPedGates, trafficReferencePos,
                                                       nearFieldPedPickUniform, nearFieldPedNextId);
                // GENERAL-PED-IDLE-TICK TASK (2026-10-10): the spawn pass
                // above places a newly-created ped directly onto a real
                // segment (onSegment=true, currentSegmentId = the real
                // segment it spawned on - near_field_ped_population.h's own
                // "already placed on the chosen segment -> steady-state 14,
                // bypassing 13's own 'waiting to snap' phase" comment) but
                // only carries the segment's ID forward, not its geometry -
                // capture it here (while pedCandidateSegments, the ONLY
                // place that geometry still exists this tick, is in scope)
                // into nearFieldPedSidewalkSegGeo so the general idle-tick
                // block below can translate this ped's segmentProgress into
                // an actual world position from frame one, instead of
                // sitting frozen until it first falls off the segment and
                // re-snaps.
                for (const auto& nfp : nearFieldPedsLive) {
                    if (!nfp.onSegment || nfp.currentSegmentId == 0) continue;
                    if (nearFieldPedSidewalkSegGeo.count(nfp.id)) continue;
                    for (const auto& seg : pedCandidateSegments) {
                        if (seg.id == nfp.currentSegmentId) {
                            nearFieldPedSidewalkSegGeo[nfp.id] = seg;
                            break;
                        }
                    }
                }
            }
        }

        // ---- Per-NPC AI loop for the near-field ambient peds (NPC AI loop task): perception -> decision -> mode ->
        // action through sr3airuntime::NpcAiRuntime. Civilians react to the player's gunfire (flee ~80% / cower ~20%,
        // spec-ai-runtime.md Sec21.4); a Flee decision is executed by the existing sidewalk/wander flee machinery just
        // below (ChooseFleeDestination), a Cower by freezing the ped. The runtime reads each ped's authoritative position
        // every frame and ends a flee beyond 30 m (the Sec21.4 stop rule). ----
        if (pedAi) {
            sr3airuntime::PlayerFacts pf;
            pf.handle = 1;
            pf.pos = trafficReferencePos;
            pf.firingOrAiming = pedAiPlayerFiring;
            pedAiPlayerFiring = false;
            std::vector<sr3airuntime::NpcFacts> pedFacts;
            pedFacts.reserve(nearFieldPedsLive.size());
            std::unordered_set<uint64_t> liveHandles;
            for (const auto& nfp : nearFieldPedsLive) {
                const uint64_t h = nfp.id + 1000;
                liveHandles.insert(h);
                if (!pedAiIds.count(h)) {
                    sr3airuntime::NpcSpawn sp;
                    sp.handle = h;
                    sp.kind = sr3airuntime::NpcKind::Civilian;
                    sp.pos = nfp.pos;
                    pedAi->addNpc(sp, nowMsAccum);
                    pedAiIds.insert(h);
                }
                sr3airuntime::NpcFacts f;
                f.handle = h;
                f.pos = nfp.pos;
                pedFacts.push_back(f);
            }
            for (auto it = pedAiIds.begin(); it != pedAiIds.end();) {
                if (!liveHandles.count(*it)) { pedAi->removeNpc(*it); it = pedAiIds.erase(it); }
                else ++it;
            }
            std::vector<sr3airuntime::NpcStep> pedSteps;
            std::vector<sr3airuntime::NpcShot> pedShots;
            pedAi->tick(nowMsAccum, pf, pedFacts, pedSteps, pedShots);
            pedAiCowering.clear();
            for (const auto& st : pedSteps) {
                const uint64_t id = st.handle - 1000;
                if (st.mode == sr3airuntime::mode::Cower) {
                    pedAiCowering.insert(id);
                } else if (st.mode == sr3airuntime::mode::Flee && !nearFieldPedFleeDestination.count(id)) {
                    for (auto& nfp : nearFieldPedsLive) {
                        if (nfp.id != id) continue;
                        const std::vector<sr3airuntime::SidewalkSegment> fleeSegs =
                            GatherNearFieldPedSpawnCandidateSegmentsAroundPlayer(nfp.pos, 0.0, nearFieldPedRng);
                        const std::optional<sr3airuntime::Vec3> fleeDest =
                            sr3airuntime::ChooseFleeDestination(nfp.pos, trafficReferencePos, fleeSegs);
                        if (fleeDest.has_value()) {
                            nearFieldPedFleeDestination[nfp.id] = *fleeDest;
                            nfp.idleSubState = sr3airuntime::PedIdleSubState::WanderMove;
                            nfp.onSegment = false;
                        } else {
                            nfp.pos = st.pos;  // no sidewalk candidate: the runtime's straight-line flee stands
                        }
                        break;
                    }
                }
            }
            {
                static size_t pedOpenReported = 0;
                for (; pedOpenReported < pedAi->openErrors().size(); ++pedOpenReported)
                    std::printf("[npc-ai] OPEN item skipped a ped tick: %s\n", pedAi->openErrors()[pedOpenReported].c_str());
            }
            for (const auto& l : pedAi->drainLog()) {
                if (l.toMode == sr3airuntime::mode::Flee) ++pedAiFleeCount;
                if (l.toMode == sr3airuntime::mode::Cower) ++pedAiCowerCount;
                std::printf("[npc-ai] t=%lldms ped=%llu mode %d->%d action=%d goal=%d dist=%.1f\n", static_cast<long long>(l.nowMs),
                            static_cast<unsigned long long>(l.handle - 1000), l.fromMode, l.toMode, l.action, l.goalReason, l.distToPlayer);
            }
        }

        // ---- Near-field ambient-ped FLEE + GENERAL IDLE/WANDER/SIDEWALK
        // movement (FLEE part: THIS TASK's own earlier pass, 2026-10-10;
        // GENERAL part: general-ped-idle-tick task, 2026-10-10, closing the
        // wider gap that earlier pass explicitly flagged but did not take
        // on - "ordinary idle peds never actually sidewalk-walk or wander
        // either"). Runs every frame regardless of the walking/driving
        // branch split further down, the same way the spawn pass just
        // above also runs unconditionally - a ped keeps fleeing/wandering
        // while Brad is driving, not just while on foot. This is now the
        // ONLY per-frame Idle/sidewalk/wander ticking wired for ANY
        // near-field ped in this codebase, but it now covers EVERY live
        // ped, not just the Flee case. A ped currently fleeing (present in
        // nearFieldPedFleeDestination) ALWAYS takes the flee branch and
        // never falls into the general branch below it - flee priority
        // over ordinary wandering is enforced by this if/else split itself,
        // not a separate flag. Reuses the real header machinery
        // (TickSidewalk/TickWander/TickOneFrameTransition, Sec20.2) rather
        // than a parallel mover, same discipline both halves already
        // shared. ----
        for (auto& nfp : nearFieldPedsLive) {
            if (pedAiCowering.count(nfp.id)) continue;  // cowering (AI Cower mode): the ped stands where it is
            auto destIt = nearFieldPedFleeDestination.find(nfp.id);
            if (destIt != nearFieldPedFleeDestination.end()) {
                // ---- FLEE (priority branch) - unchanged from the prior task. ----
                if (nfp.idleSubState == sr3airuntime::PedIdleSubState::WanderMove) {
                    nfp.pos = sr3airuntime::StepTowardDestination(
                        nfp.pos, destIt->second, sr3airuntime::kFleeSpeedMsChosen * static_cast<double>(dt));
                    // CHOSEN arrival radius (1.0m) - Sec20.2's own "wander-move
                    // completion criteria... was identified by call shape only,
                    // not independently confirmed"; this file's own
                    // WanderTickParams::hasReachedDestination already documents
                    // that same OPEN item, so this is this task's own CHOSEN
                    // value for it, not a new gap.
                    const double kArriveEpsM = 1.0;
                    sr3airuntime::WanderTickParams wtp;
                    const sr3airuntime::Vec3 destCopy = destIt->second;
                    wtp.hasReachedDestination = [destCopy, kArriveEpsM](const sr3airuntime::Vec3& p) {
                        const double dx = p.x - destCopy.x, dz = p.z - destCopy.z;
                        return (dx * dx + dz * dz) <= kArriveEpsM * kArriveEpsM;
                    };
                    sr3airuntime::Vec3 destInOut = destIt->second;
                    sr3airuntime::TickWander(nfp, wtp, destInOut);
                } else if (nfp.idleSubState == sr3airuntime::PedIdleSubState::WanderToSidewalk) {
                    // Real one-frame transition (Sec20.2) back onto the sidewalk
                    // graph - the ped has arrived and is done fleeing; hand it
                    // back to the (now also ticked, see below) ordinary
                    // sidewalk/idle state rather than leaving it permanently
                    // parked in a wander sub-state.
                    sr3airuntime::TickOneFrameTransition(nfp);
                    nearFieldPedFleeDestination.erase(destIt);
                    nearFieldPedSidewalkSegGeo.erase(nfp.id);  // force a fresh snap next time this ped reaches the sidewalk group below
                }
                continue;  // flee takes priority - never also run the general branch this frame
            }

            const sr3airuntime::Vec3 pedPrePos = nfp.pos;
            // ---- GENERAL ORDINARY IDLE TICK (general-ped-idle-tick task):
            // every near-field ped NOT currently fleeing, every frame,
            // through its real idle sub-state machine (Sec20.2). ----
            switch (sr3airuntime::TickGroupFor(nfp.idleSubState)) {
                case sr3airuntime::PedIdleTickGroup::Sidewalk: {
                    sr3airuntime::SidewalkTickParams sp;
                    sp.advancePerTick = sr3airuntime::kSidewalkAdvancePerSecondChosen * static_cast<double>(dt);
                    // Real abandon TARGET ("sidewalk" -> "wander choose
                    // dest", Sec20.2's own 0x0051bd20) applied at a CHOSEN
                    // TRIGGER point (reaching the stand-in segment's own
                    // end, segmentProgress>=1.0) - Sec20.2 itself flags the
                    // real trigger as unidentified ("what decides to call
                    // it" is OPEN), and separately flags "what happens at
                    // segmentProgress==1" as OPEN; this picks the already-
                    // named real target as the answer to both at once
                    // (reach the end of this stand-in sidewalk -> start
                    // wandering) rather than parking the ped at progress==1
                    // forever, which would silently reproduce the exact
                    // "never actually moves" bug this task exists to fix.
                    sp.shouldAbandonToWander = [&nfp] { return nfp.segmentProgress >= 1.0; };
                    if (playtestRoadGraph) {
                        // Real sidewalk graph present: the stand-in segment walk is replaced by the Sec20.6.9 navigator.
                        // A ped on the sidewalk picks a goal at once ("sidewalk" -> "wander choose dest", Sec20.2) and
                        // walks the searched path in the Wander group below.
                        sp.snapSegmentId = 1;
                        sp.shouldAbandonToWander = [] { return true; };
                        sr3airuntime::TickSidewalk(nfp, sp);
                        break;
                    }
                    if (!nfp.onSegment) {
                        // Re-snap onto a fresh stand-in segment near the
                        // ped's OWN current position - the exact same
                        // GatherNearFieldPedSpawnCandidateSegmentsAroundPlayer
                        // generation call the flee branch above already
                        // uses for its own candidate geometry (ChooseFleeDestination's
                        // call site), just centered on the ped instead of
                        // the camera/player, since this file owns no real
                        // persistent Sidewalks graph to snap onto instead
                        // (file header: "no C++ reader for that content
                        // kind exists").
                        const std::vector<sr3airuntime::SidewalkSegment> snapCandidates =
                            GatherNearFieldPedSpawnCandidateSegmentsAroundPlayer(nfp.pos, 0.0, nearFieldPedRng);
                        if (!snapCandidates.empty()) {
                            const sr3airuntime::SidewalkSegment& seg = snapCandidates[0];
                            sp.snapSegmentId = seg.id != 0 ? seg.id : 1;  // TickSidewalk's own gate treats 0 as "no resolution yet"
                            nearFieldPedSidewalkSegGeo[nfp.id] = seg;
                        }
                    }
                    sr3airuntime::TickSidewalk(nfp, sp);
                    // Move the ped's actual world position (segmentProgress
                    // itself is only the abstract 0..1 completion scalar
                    // TickSidewalk owns - near_field_ped_population.h's own
                    // file header: this file does not touch NearFieldPed::pos
                    // at all, that mapping is the caller's own job). CHOSEN:
                    // step toward the resolved segment's own END point with
                    // the SAME StepTowardDestination helper the wander/flee
                    // branches already use, rather than recomputing an
                    // absolute point from segmentProgress via PointAt - a
                    // freshly-spawned ped's own spawn position (a random
                    // point ALONG the segment plus a sideways offset,
                    // SelectNearFieldPedSpawn) does not generally equal
                    // PointAt(0), so an absolute remap would teleport it
                    // back onto the segment's spine the first tick after
                    // spawn; stepping incrementally from wherever the ped
                    // actually is avoids that regression.
                    if (nfp.onSegment) {
                        auto geoIt = nearFieldPedSidewalkSegGeo.find(nfp.id);
                        if (geoIt != nearFieldPedSidewalkSegGeo.end()) {
                            nfp.pos = sr3airuntime::StepTowardDestination(
                                nfp.pos, geoIt->second.end,
                                sr3airuntime::kWanderWalkSpeedMsChosen * static_cast<double>(dt));
                        }
                    }
                    break;
                }
                case sr3airuntime::PedIdleTickGroup::Wander: {
                    auto wdIt = nearFieldPedWanderDestination.find(nfp.id);
                    if (playtestRoadGraph) {
                        // Sec20.6.9 navigator: goal choice -> best-first path over sidewalk nodes -> edge-to-edge walk
                        // (crossings gated inside the navigator) -> arrival within 0.425 m -> event 8 after 1000 ms.
                        auto navIt = nearFieldPedNav.find(nfp.id);
                        if (nfp.idleSubState == sr3airuntime::PedIdleSubState::WanderMove && navIt != nearFieldPedNav.end() &&
                            navIt->second.active) {
                            sr3airuntime::PedNavigator& nav = navIt->second;
                            const uint64_t ewBefore = nav.edgesWalked, wBefore = nav.waits, cBefore = nav.crosses;
                            const sr3airuntime::PedNavResult nr = sr3airuntime::PedNavigatorStep(
                                nav, *playtestRoadGraph, nfp.pos, static_cast<double>(dt),
                                nearFieldPedWalkSpeed.count(nfp.id) ? nearFieldPedWalkSpeed[nfp.id]
                                                                    : sr3airuntime::kWanderWalkSpeedMsChosen,
                                nowMsAccum, [&](size_t c) {
                                    return c < playtestRoadGraph->crossingLight.size() && signalController.CrossingClosed(c);
                                });
                            pedNavEdgesWalked += nav.edgesWalked - ewBefore;
                            pedCrossWaits += nav.waits - wBefore;
                            pedCrossCrosses += nav.crosses - cBefore;
                            if (nav.crosses > cBefore) {
                                const uint32_t ce = nav.next == 0 ? nav.route.startEdge : nav.route.edges[nav.next - 1];
                                if (playtestRoadGraph->crossingLight[playtestRoadGraph->sidewalkEdges[ce].crossing] < 0) ++pedCrossUnsignalled;
                                if (nav.crossing.waitedBefore) ++pedCrossAfterWait;
                            }
                            if (nr == sr3airuntime::PedNavResult::Arrived) {
                                ++pedNavArrivals;
                                pedNavEvent8Due[nfp.id] = nav.event8DueAtMs;
                            }
                        }
                        for (auto it = pedNavEvent8Due.begin(); it != pedNavEvent8Due.end();) {
                            if (it->second <= nowMsAccum) { ++pedNavEvent8; it = pedNavEvent8Due.erase(it); }
                            else ++it;
                        }
                        sr3airuntime::WanderTickParams wtp2;
                        wtp2.chooseRandomDestination = [&]() -> std::optional<sr3airuntime::Vec3> {
                            const uint32_t goal = sr3airuntime::ChoosePedGoalNode(
                                *playtestRoadGraph, nfp.pos, [&] { return static_cast<double>(nearFieldPedPickUniform(1000000)) / 1000000.0; });
                            if (goal == sr3airuntime::kPedNoIndex) return std::nullopt;
                            ++pedNavSearches;
                            // Sec20.6.9(a): the walk variant is picked once, uniformly, when the ped's navigator state is first created.
                            // CHOSEN: gender is not tracked on NearFieldPed, so the male pool is used (female pool exists in sr3airuntime).
                            if (!nearFieldPedWalkSpeed.count(nfp.id))
                                nearFieldPedWalkSpeed[nfp.id] = sr3airuntime::PickPedWalkSpeed(
                                    false, [&] { return static_cast<double>(nearFieldPedPickUniform(1000000)) / 1000000.0; });
                            sr3airuntime::PedNavigator fresh;
                            if (!sr3airuntime::PedNavigatorBegin(fresh, *playtestRoadGraph, nfp.pos, goal)) {
                                ++pedNavSearchFails;
                                return std::nullopt;
                            }
                            const sr3airuntime::Vec3 g = fresh.goal;
                            nearFieldPedNav[nfp.id] = std::move(fresh);
                            return g;
                        };
                        wtp2.hasReachedDestination = [&](const sr3airuntime::Vec3&) {
                            auto it2 = nearFieldPedNav.find(nfp.id);
                            return it2 == nearFieldPedNav.end() || !it2->second.active;
                        };
                        sr3airuntime::Vec3 destNav = (wdIt != nearFieldPedWanderDestination.end()) ? wdIt->second : sr3airuntime::Vec3{};
                        sr3airuntime::TickWander(nfp, wtp2, destNav);
                        if (nfp.idleSubState == sr3airuntime::PedIdleSubState::WanderMove)
                            nearFieldPedWanderDestination[nfp.id] = destNav;
                        break;
                    }
                    if (nfp.idleSubState == sr3airuntime::PedIdleSubState::WanderMove &&
                        wdIt != nearFieldPedWanderDestination.end()) {
                        nfp.pos = sr3airuntime::StepTowardDestination(
                            nfp.pos, wdIt->second, sr3airuntime::kWanderWalkSpeedMsChosen * static_cast<double>(dt));
                    }
                    const double kArriveEpsM = 1.0;  // same CHOSEN arrival radius the flee branch above already uses
                    sr3airuntime::WanderTickParams wtp;
                    wtp.chooseRandomDestination = [&]() -> std::optional<sr3airuntime::Vec3> {
                        // Same candidate-segment generation call the flee
                        // branch's own ChooseFleeDestination already feeds
                        // on (GatherNearFieldPedSpawnCandidateSegmentsAroundPlayer),
                        // just picking a uniformly-random candidate endpoint
                        // instead of the "furthest away from threat" one -
                        // there is no threat to run from here (Sec20.2's own
                        // "wander choose dest... generates a validated
                        // random candidate point" is itself a random pick,
                        // not a directional one).
                        const std::vector<sr3airuntime::SidewalkSegment> wanderCandidates =
                            GatherNearFieldPedSpawnCandidateSegmentsAroundPlayer(nfp.pos, 0.0, nearFieldPedRng);
                        if (wanderCandidates.empty()) return std::nullopt;
                        const size_t segIdx = nearFieldPedPickUniform(wanderCandidates.size());
                        const sr3airuntime::SidewalkSegment& seg =
                            wanderCandidates[segIdx < wanderCandidates.size() ? segIdx : 0];
                        return (nearFieldPedPickUniform(2) == 0) ? seg.start : seg.end;
                    };
                    if (wdIt != nearFieldPedWanderDestination.end()) {
                        const sr3airuntime::Vec3 destCopy = wdIt->second;
                        wtp.hasReachedDestination = [destCopy, kArriveEpsM](const sr3airuntime::Vec3& p) {
                            const double dx = p.x - destCopy.x, dz = p.z - destCopy.z;
                            return (dx * dx + dz * dz) <= kArriveEpsM * kArriveEpsM;
                        };
                    }
                    sr3airuntime::Vec3 destInOut =
                        (wdIt != nearFieldPedWanderDestination.end()) ? wdIt->second : sr3airuntime::Vec3{};
                    sr3airuntime::TickWander(nfp, wtp, destInOut);
                    if (nfp.idleSubState == sr3airuntime::PedIdleSubState::WanderMove)
                        nearFieldPedWanderDestination[nfp.id] = destInOut;  // just committed (WanderChooseDest -> WanderMove) or already-tracked
                    break;
                }
                case sr3airuntime::PedIdleTickGroup::OneFrameTransition:
                    sr3airuntime::TickOneFrameTransition(nfp);
                    nearFieldPedWanderDestination.erase(nfp.id);
                    nearFieldPedNav.erase(nfp.id);
                    nearFieldPedSidewalkSegGeo.erase(nfp.id);  // force a fresh snap, consistent with onSegment being reset to false by TickOneFrameTransition itself
                    break;
                case sr3airuntime::PedIdleTickGroup::Other:
                    break;  // Idle(9)/WaitToHolsterWeapon(11)/Sleeping(36)/Sitting(37) - out of this file's own scope (Sec20.3), not driven here
            }

            // ---- Crossing gate (Sec20.6.9(c) / Sec20.6.13(h)): a ped about to step onto a sidewalk crossing edge whose
            // light is closed stops at the kerb and waits; open -> crosses; a started crossing is finished; a crossing
            // without a light is never closed. Applies to the walking (sidewalk/wander) ped; the flee branch above is
            // not gated (CHOSEN: the spec describes the sidewalk navigator only). ----
            if (playtestRoadGraph && !nearFieldPedNav.count(nfp.id) /* navigator peds gate their own crossings */ &&
                (nfp.pos.x != pedPrePos.x || nfp.pos.z != pedPrePos.z ||
                                      nearFieldPedCrossing.count(nfp.id))) {
                auto csIt = nearFieldPedCrossing.find(nfp.id);
                sr3airuntime::PedCrossingState& cs =
                    csIt != nearFieldPedCrossing.end() ? csIt->second : nearFieldPedCrossing[nfp.id];
                const sr3airuntime::PedCrossingState before = cs;
                auto applyEdge = [&](uint32_t ei) {
                    const auto& e = playtestRoadGraph->sidewalkEdges[ei];
                    const auto& na = playtestRoadGraph->sidewalkNodes[e.nodeA].pos;
                    const auto& nb = playtestRoadGraph->sidewalkNodes[e.nodeB].pos;
                    const bool closed = e.crossing < playtestRoadGraph->crossingLight.size() &&
                                        signalController.CrossingClosed(e.crossing);
                    nfp.pos = sr3airuntime::PedCrossingGate(pedPrePos, nfp.pos, static_cast<uint64_t>(ei) + 1,
                                                            {na.x, na.y, na.z}, {nb.x, nb.y, nb.z}, e.width(), closed, cs);
                };
                if (cs.edgeId != 0) {
                    applyEdge(static_cast<uint32_t>(cs.edgeId - 1));
                } else {
                    for (uint32_t ei : playtestRoadGraph->SidewalkEdgesNear(pedPrePos.x, pedPrePos.z, 3.0)) {
                        if (playtestRoadGraph->sidewalkEdges[ei].crossing == 0xffff) continue;
                        applyEdge(ei);
                        if (cs.edgeId != 0) break;
                    }
                }
                if (cs.waiting && !before.waiting) ++pedCrossWaits;
                if (cs.committed && !before.committed) {
                    ++pedCrossCrosses;
                    if (cs.edgeId != 0 && playtestRoadGraph->crossingLight[playtestRoadGraph->sidewalkEdges[cs.edgeId - 1].crossing] < 0) ++pedCrossUnsignalled;
                    if (before.waiting) ++pedCrossAfterWait;
                }
                if (cs.edgeId == 0) nearFieldPedCrossing.erase(nfp.id);
            }
        }
        if (nearFieldPedCrossing.size() > nearFieldPedsLive.size() + 64 || nearFieldPedNav.size() > nearFieldPedsLive.size() + 64 || nearFieldPedWalkSpeed.size() > nearFieldPedsLive.size() + 64 ||
            pedNavEvent8Due.size() > nearFieldPedsLive.size() + 64) {  // drop states of despawned peds
            std::unordered_set<uint64_t> liveIds;
            for (const auto& p : nearFieldPedsLive) liveIds.insert(p.id);
            for (auto it = nearFieldPedNav.begin(); it != nearFieldPedNav.end();)
                it = liveIds.count(it->first) ? std::next(it) : nearFieldPedNav.erase(it);
            for (auto it = nearFieldPedWalkSpeed.begin(); it != nearFieldPedWalkSpeed.end();)
                it = liveIds.count(it->first) ? std::next(it) : nearFieldPedWalkSpeed.erase(it);
            for (auto it = pedNavEvent8Due.begin(); it != pedNavEvent8Due.end();)
                it = liveIds.count(it->first) ? std::next(it) : pedNavEvent8Due.erase(it);
            for (auto it = nearFieldPedCrossing.begin(); it != nearFieldPedCrossing.end();)
                it = liveIds.count(it->first) ? std::next(it) : nearFieldPedCrossing.erase(it);
        }
}

// T5 (2026-10-18): report an in-world crime to the police track (spec-ai-runtime.md Sec19.8 G1). CHOSEN: team =
// police only (every reported record is police-only per Sec14.6) and witnessed = true (the Check_Detection test, a
// live player "detecting" the actor, has no cop/LOS system to ask here; Team A question).
void notorietyReportCrime(PlaytestSession& sess, const sr3airuntime::CrimeActivity& activity, const char* what) {
    if (!sess.notoriety_) return;
    ++sess.crimeReports;
    sess.notoriety_->ReportCrime(sr3airuntime::NotorietyTrack::kPolice, activity, /*witnessed=*/true,
                                 /*bonusX3=*/false, sess.nowMsAccum);
    std::printf("[notoriety] frame %d crime report '%s' (%s) police points=%d%s\n", sess.frames, activity.name.c_str(),
                what, activity.police.points,
                sess.debugNotoriety >= 0 ? " [--debug-notoriety override active]" : "");
}

// Per-frame notoriety tick: adopt script writes to the alias tracks, report vehicle-hit-ped crimes, run the Sec19.8
// drain/decay in order, and print a trace line.
void stepNotoriety(PlaytestSession& sess, PlaytestFrame&) {
    if (!sess.notoriety_ || !sess.combatEs_p) return;
    auto& n = *sess.notoriety_;
    auto& states = sess.combatEs_p->notorietyAliasTrackStates();
    const int64_t now = sess.nowMsAccum;
    const bool overridden = sess.debugNotoriety >= 0;
    if (!overridden) {
        // A script's notoriety_set wrote the alias track since we last did: adopt it (CHOSEN, see SetLevel).
        for (int code = 0; code < 4; ++code) {
            auto it = states.find(code);
            const double cur = it == states.end() ? 0.0 : it->second.currentValue;
            if (cur != sess.notorietyAliasLastWritten[static_cast<size_t>(code)]) {
                sess.notorietyAliasLastWritten[static_cast<size_t>(code)] = cur;
                n.SetLevel(static_cast<sr3airuntime::NotorietyTrack>(code),
                           static_cast<int32_t>(std::floor(std::max(0.0, cur))), now);
            }
        }
    }
    if (sess.debugNotorietyPoints > 0) {
        const int dbgPoints = sess.debugNotorietyPoints;
        sr3airuntime::CrimeActivity dbg{"debug crime", 0, {0, 0, sr3airuntime::kNotorietyMaxLevel, false},
                                        {dbgPoints, 0, sr3airuntime::kNotorietyMaxLevel, false}};
        sess.debugNotorietyPoints = -1;  // inject once, on the first frame this step runs
        notorietyReportCrime(sess, dbg, "--debug-notoriety-points");
    }
    // Player vehicle hits a ped. CHOSEN: a "hit" is the vehicle moving faster than 3 m/s with a live near-field ped
    // within 1.5 m (xz) of its pivot; reported once per ped as the CHOSEN "civilian assault" record.
    if (sess.inVehicle && sess.vehicleRig_ && sess.vehicleRig_->sim) {
        const auto& st = sess.vehicleRig_->sim->state_;
        const double speed = std::sqrt(st.velForwardBody * st.velForwardBody + st.velRightBody * st.velRightBody);
        if (speed > 3.0) {
            for (const auto& p : sess.nearFieldPedsLive) {
                const double dx = p.pos.x - st.posX, dz = p.pos.z - st.posZ;
                if (dx * dx + dz * dz <= 1.5 * 1.5 && sess.vehicleHitPedIds.insert(p.id).second)
                    notorietyReportCrime(sess, sr3airuntime::kChosenCivilianAssault, "vehicle hit ped");
            }
        }
    }
    n.Tick(now);
    if (sess.frames % 30 == 0) {
        using sr3airuntime::NotorietyTrack;
        const auto pol = NotorietyTrack::kPolice;
        if (n.level(pol) > 0 || n.fraction(pol) > 0.0 || n.pendingCount() > 0 || sess.crimeReports > 0) {
            size_t live = 0;
            for (const auto& s : sess.roadblockManager_->slots()) live += s.filled ? 1 : 0;
            std::printf("[notoriety] frame %d t=%lldms police level=%d fraction=%.2f decayArmed=%d pending=%zu "
                        "liveLevel=%d reports=%llu roadblock armedDebug=%d sched attempts=%llu placed=%llu "
                        "debugPlaced=%llu slotsFilled=%zu\n",
                        sess.frames, static_cast<long long>(now), n.level(pol), n.fraction(pol),
                        n.decayArmed(pol) ? 1 : 0, n.pendingCount(), sess.liveNotorietyLevel(),
                        static_cast<unsigned long long>(sess.crimeReports), sess.roadblockDebugForce ? 1 : 0,
                        static_cast<unsigned long long>(sess.roadblockSchedAttempts),
                        static_cast<unsigned long long>(sess.roadblockSchedPlaced),
                        static_cast<unsigned long long>(sess.roadblockDebugPlaced), live);
        }
    }
}

void stepTrafficRoadblock(PlaytestSession& sess, PlaytestFrame& frm) {
    auto& freeroamHoodName = sess.freeroamHoodName;
    auto& freeroamCategoryName = sess.freeroamCategoryName;
    auto& playtestRoadGraph = sess.playtestRoadGraph;
    auto& nearFieldPedFleeDestination = sess.nearFieldPedFleeDestination;
    auto& nearFieldLive = sess.nearFieldLive;
    auto& freeroamVehDespawned = sess.freeroamVehDespawned;
    auto& freeroamVehPasses = sess.freeroamVehPasses;
    auto& freeroamPedDespawned = sess.freeroamPedDespawned;
    auto& nearFieldVehicleLaneState = sess.nearFieldVehicleLaneState;
    auto& nearFieldVehicleHeading = sess.nearFieldVehicleHeading;
    auto& nearFieldPedsLive = sess.nearFieldPedsLive;
    const auto& kNearFieldPedLocalCap = sess.kNearFieldPedLocalCap;
    auto& roadblockSlots = sess.roadblockSlots;
    auto& roadblockManager = *sess.roadblockManager_;
    auto& roadblockDriver = *sess.roadblockDriver_;
    auto& roadblockSlotVisual = sess.roadblockSlotVisual;
    auto& frames = sess.frames;
    auto& todFraction = sess.todFraction;
    auto& dt = frm.dt;
    const auto& trafficReferencePos = frm.trafficReferencePos;
    const auto& trafficReferenceForward = frm.trafficReferenceForward;
        // ---- Police roadblock spawn (T5, 2026-10-18). Two triggers: (1) NOTORIETY (spec-ai-runtime.md Sec20.6.10
        // (a)/(c)): NotorietyRoadblockScheduler gates on the police track level 2..5, driving, live count and the
        // per-level Min..Max timer; a due placement fills the (CHOSEN) slot table. (2) the old 'R' debug gate, kept
        // as a harmless force-arm (roadblockDriver.armed): it ticks every frame as before. ----
        sess.roadblockDebugForce = roadblockDriver.armed;
        stepNotoriety(sess, frm);
        auto filledCount = [&]() {
            size_t c = 0;
            for (const auto& s : roadblockManager.slots()) c += s.filled ? 1 : 0;
            return c;
        };
        auto runRoadblockTick = [&](double dtUse, bool spawn) {
            // Snapshot BEFORE the tick so newly-filled slots can be identified afterward purely from the public API
            // (see roadblockSlotVisual's own declaration comment for why this diffing is needed and its edge case).
            std::vector<bool> prevFilled(roadblockManager.slots().size());
            for (size_t i = 0; i < roadblockManager.slots().size(); ++i)
                prevFilled[i] = roadblockManager.slots()[i].filled;
            std::vector<sr3airuntime::RoadblockDispatchResult> nonCoopDispatches;
            if (spawn) {
                auto resolveRoadblockDirection = [&](const sr3airuntime::Vec3&) {
                    return sr3airuntime::ResolveDirectionAlongHeading(trafficReferenceForward);
                };
                sr3airuntime::RoadblockTickResult rbTick =
                    roadblockDriver.RunTick(roadblockManager, dtUse, trafficReferencePos, resolveRoadblockDirection);
                for (auto& d : rbTick.dispatches)
                    if (d.outcome != sr3airuntime::RoadblockDispatchOutcome::kSkippedCoopHandoff)
                        nonCoopDispatches.push_back(d);
            } else {
                roadblockManager.RunEligibilityAndDespawnPass(dtUse);  // despawn timers run even when no spawn is due
            }
            size_t di = 0;
            for (size_t i = 0; i < roadblockManager.slots().size() && di < nonCoopDispatches.size(); ++i) {
                if (!prevFilled[i] && roadblockManager.slots()[i].filled) roadblockSlotVisual[i] = nonCoopDispatches[di++];
            }
            for (size_t i = 0; i < roadblockManager.slots().size(); ++i) {
                if (prevFilled[i] && !roadblockManager.slots()[i].filled) roadblockSlotVisual[i].reset();
            }
        };
        {
            // Police track level via the alias track the HUD/populations read (so --debug-notoriety=N and script
            // notoriety_set also arm roadblocks). CHOSEN "driving" = the player is in a vehicle; the spec's "on a road
            // segment without the water flag" part has no per-frame query in the viewer.
            const auto& aliasStates = sess.combatEs_p->notorietyAliasTrackStates();
            const auto pIt = aliasStates.find(3);
            const int policeLevel = pIt == aliasStates.end()
                                        ? 0
                                        : std::max(0, std::min(5, static_cast<int>(std::floor(pIt->second.currentValue))));
            const size_t filledBefore = filledCount();
            // CHOSEN single-instance stand-in: the one 8-slot table IS the roadblock instance, so it counts as one live
            // roadblock once any slot is filled, and as "no capacity left" (kMaxLiveRoadblocks) once it is full (the
            // spec's per-level Max_active_roadblocks of 2-3 would need several instances).
            const int liveRoadblocks =
                filledBefore >= roadblockManager.slots().size()
                    ? sr3airuntime::NotorietyRoadblockScheduler::kMaxLiveRoadblocks
                    : (filledBefore > 0 ? 1 : 0);
            const auto dec = sess.roadblockSchedule.Update(sess.nowMsAccum, policeLevel, sess.inVehicle, liveRoadblocks);
            if (dec.place) {
                ++sess.roadblockSchedAttempts;
                // One placement = one roadblock instance = the whole CHOSEN slot table; the per-tick throttle
                // (2 ped / 4 vehicle) means several ticks are needed to fill it, so run up to 4 within this frame.
                for (int k = 0; k < 4 && filledCount() < roadblockManager.slots().size(); ++k)
                    runRoadblockTick(k == 0 ? dt : 0.0, true);
                const bool ok = filledCount() > filledBefore;
                sess.roadblockSchedule.ReportPlacement(dec.level, ok, sess.nowMsAccum);
                if (ok) ++sess.roadblockSchedPlaced;
                std::printf("[roadblock] frame %d notoriety-armed placement at police level %d: %s (filled %zu/%zu)\n",
                            frames, dec.level, ok ? "placed" : "failed (retry in 2000ms)", filledCount(),
                            roadblockManager.slots().size());
            } else if (roadblockDriver.ShouldRunTick()) {
                const size_t before = filledCount();
                runRoadblockTick(dt, true);
                if (filledCount() > before) ++sess.roadblockDebugPlaced;
            } else {
                runRoadblockTick(dt, false);
            }
        }

        // Periodic status line (every 30 frames - modest volume over a
        // typical run) - easy to grep for in a capture/ctest log without
        // needing a frame-by-frame dump.
        if (frames % 30 == 0) {
            size_t roadblockFilledNow = 0;
            for (const auto& s : roadblockManager.slots())
                if (s.filled) ++roadblockFilledNow;
            std::printf("[traffic] frame %d near-field live=%zu/%d; roadblock armed=%d filled=%zu/%zu\n", frames,
                        nearFieldLive.size(), sr3airuntime::kNearFieldVehicleCap, roadblockDriver.armed ? 1 : 0,
                        roadblockFilledNow, roadblockSlots.size());
            // TRAFFIC-LANE-FOLLOW-REAL-MECHANISM TASK (2026-10-10): per-
            // vehicle lane-following trajectory trace - live proof that
            // near-field ambient vehicles now follow the real Traffic
            // lane graph (seg/pointCursor advancing, position tracing the
            // segment polyline) rather than sitting at a static spawn
            // position with only a cosmetic heading, and that a junction
            // (segment 0's own two legal next segments) is actually
            // reached and resolved deterministically.
            for (const auto& nfv : nearFieldLive) {
                const auto stIt = nearFieldVehicleLaneState.find(nfv.id);
                const auto hIt = nearFieldVehicleHeading.find(nfv.id);
                std::printf("  [lane] id=%llu pos=(%.2f,%.2f) heading=%.1fdeg seg=%zu valid=%d\n",
                            static_cast<unsigned long long>(nfv.id), nfv.pos.x, nfv.pos.z,
                            (hIt != nearFieldVehicleHeading.end() ? hIt->second * 180.0 / 3.14159265358979323846 : 0.0),
                            (stIt != nearFieldVehicleLaneState.end() ? stIt->second.currentSegmentIndex : 0),
                            (stIt != nearFieldVehicleLaneState.end() ? (stIt->second.valid ? 1 : 0) : -1));
            }
            std::printf("[peds] frame %d near-field ambient peds live=%zu/%zu\n", frames, nearFieldPedsLive.size(),
                        kNearFieldPedLocalCap);
            std::printf("[population] frame %d vehicles live=%zu (spawn passes %zu, distance-despawned %zu) peds live=%zu (distance-despawned %zu) tod_idx=%d hood=%s category=%s graph=%s\n",
                        frames, nearFieldLive.size(), freeroamVehPasses, freeroamVehDespawned, nearFieldPedsLive.size(),
                        freeroamPedDespawned, sr3airuntime::TodDensityIndexFromDayFraction(todFraction),
                        freeroamHoodName.c_str(), freeroamCategoryName.c_str(),
                        playtestRoadGraph ? "REAL road/sidewalk" : "synthetic lane/sidewalk");
            if (playtestRoadGraph) {
                std::printf("[roadgraph] frame %d vehicle gathers=%zu candidates=%zu (avg %.1f) lane-spawns=%zu; "
                            "ped gathers=%zu edges=%zu (avg %.1f)\n",
                            frames, g_roadVehGathers, g_roadVehCandidatesTotal,
                            g_roadVehGathers ? static_cast<double>(g_roadVehCandidatesTotal) / static_cast<double>(g_roadVehGathers) : 0.0,
                            g_roadVehLaneSpawns, g_roadPedGathers, g_roadPedEdgesTotal,
                            g_roadPedGathers ? static_cast<double>(g_roadPedEdgesTotal) / static_cast<double>(g_roadPedGathers) : 0.0);
            }
            // THIS TASK (2026-10-10): per-fleeing-ped position trace, for
            // live verification that a Flee reaction actually moves the
            // ped's own NearFieldPed::pos over subsequent frames, not just
            // that the reaction mode computed correctly (which already
            // worked before this task - see this block's own HANDOFF
            // context).
            for (const auto& nfp : nearFieldPedsLive) {
                if (!nearFieldPedFleeDestination.count(nfp.id)) continue;
                std::printf("[flee-trace] frame %d ped id=%llu pos=(%.2f,%.2f,%.2f) idleSubState=%d\n", frames,
                            static_cast<unsigned long long>(nfp.id), nfp.pos.x, nfp.pos.y, nfp.pos.z,
                            static_cast<int>(nfp.idleSubState));
            }
            // GENERAL-PED-IDLE-TICK TASK (2026-10-10): the SAME per-ped
            // position trace as [flee-trace] above, but for every ORDINARY
            // (non-fleeing) near-field ped - live verification that the
            // general idle/sidewalk/wander tick wired in this task actually
            // moves MULTIPLE ambient peds' own NearFieldPed::pos over time,
            // not just the one that happens to get shot.
            for (const auto& nfp : nearFieldPedsLive) {
                if (nearFieldPedFleeDestination.count(nfp.id)) continue;
                std::printf("[idle-trace] frame %d ped id=%llu pos=(%.2f,%.2f,%.2f) idleSubState=%d\n", frames,
                            static_cast<unsigned long long>(nfp.id), nfp.pos.x, nfp.pos.y, nfp.pos.z,
                            static_cast<int>(nfp.idleSubState));
            }
        }
}

} // namespace sr3viewer
