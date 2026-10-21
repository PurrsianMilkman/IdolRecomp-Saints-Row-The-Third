// runPlaytest() frame-loop pieces for AW-15: player-flown helicopters / planes / VTOL / boats (spec-physics-runtime.md
// Sec10.13 laws via viewer_craft_rig.h) and the on-foot freefall + parachute (spec-player-runtime.md Sec19).
// See viewer_craft_wiring.h for the flag list and what is CHOSEN.
#include "viewer_platform.h"
#include "viewer_playtest_session.h"

#include <set>

namespace sr3viewer {

using namespace sr3viewer_craft;

namespace {
// Raw mesh bounds are expensive to walk; cache the body-frame origin offset per model.
struct BoundsEntry {
    V3 bmin, bmax;
    bool ok = false;
};
std::map<const AmbientVehicleModel*, BoundsEntry>& boundsCache() {
    static std::map<const AmbientVehicleModel*, BoundsEntry> c;
    return c;
}
const BoundsEntry& boundsFor(const PlaytestSession& sess, const AmbientVehicleModel* m) {
    auto& cache = boundsCache();
    static uint64_t cacheSession = 0;  // the cache keys are registry-owned model pointers: drop them with the session
    if (cacheSession != sess.sessionId) { cache.clear(); cacheSession = sess.sessionId; }
    auto it = cache.find(m);
    if (it != cache.end()) return it->second;
    BoundsEntry e;
    V3 mn, mx;
    if (m && RawMeshBounds(m->rig, mn, mx)) {
        MeshBoundsToBody(sess.craft.meshForwardAxis, mn, mx, e.bmin, e.bmax);
        e.ok = true;
    } else {
        e.bmin = {-1.5, -0.6, -3.0};
        e.bmax = {1.5, 1.2, 3.0};
    }
    return cache.emplace(m, e).first->second;
}
CraftEnv makeEnv(PlaytestSession& sess) {
    CraftEnv env;
    PlaytestSession* sp = &sess;
    env.ground = [sp](double x, double z, double fromY, double& out) {
        const auto g = sp->sampleGround(x, z, fromY, -1.0e9f);
        if (g.real()) {
            out = static_cast<double>(g.y);
            return true;
        }
        // The walkable ground of this port has holes (the `~s` strip stand-in, spec-zone-data-format.md 13.5.1(c)); a craft
        // that cannot land is worse than a coarse floor, so CHOSEN fallback: the nearest road / sidewalk node height within
        // 40 m (the road graph is a surface the spec calls independent and within 0.15 m of the collision, 13.5.1(e)).
        double ny = 0.0;
        if (!sp->noNodeGround && sp->nodeGround.Nearest(x, z, 40.0, fromY, ny)) {
            out = ny;
            return true;
        }
        return false;
    };
    env.hasWater = sess.craft.hasWater;
    env.waterY = sess.craft.waterLevel;
    return env;
}
}  // namespace

const CraftTypeRow* craftRowForActiveCar(const PlaytestSession& sess) {
    if (!sess.activeCarModel || CraftRegistry().empty()) return nullptr;
    return FindCraftRow(sess.activeCarModel->key);
}

// The parked active car's mesh-origin Y given the ground height under it: a craft keeps its origin `bottom` above the
// ground; a boat whose ground is under the water plane floats on it instead (draft 0.3 m, CHOSEN, = the exit pose).
float craftParkedRenderY(const PlaytestSession& sess, float groundY) {
    const CraftTypeRow* row = craftRowForActiveCar(sess);
    if (!row) return groundY;
    const double bottom = std::max(0.2, -boundsFor(sess, sess.activeCarModel).bmin.y);
    if (row->cls == 5 && sess.craft.hasWater && groundY < sess.craft.waterLevel)
        return static_cast<float>(sess.craft.waterLevel - 0.3 + bottom);
    return groundY + static_cast<float>(bottom);
}

// ---- --spawn-vehicle=NAME[@water|@X,Z]: park the model in front of the player (E enters it like any parked car) -----
//   NAME            2.5 m ahead of the player (the entry radius is 3 m to the vehicle origin)
//   NAME@X,Z        the player is warped to (X, Z) first and the craft parked 2.5 m ahead there (open ground for a runway)
//   NAME@water      the player is warped next to the nearest boat lane (road-graph segment flag bit 18, spec-zone-data-format.md
//                   13.2a) and the craft is parked on it, facing along the lane
// With a target the spawn is two-stage: warp at spawnAtFrame, park the craft once the ground under it resolves (or 90 frames
// later), so the tile has streamed in.
void craftSpawnStep(PlaytestSession& sess) {
    auto& cs = sess.craft;
    if (cs.spawnName.empty() || cs.spawnName == "list" || cs.spawnDone || sess.frames < cs.spawnAtFrame || sess.inVehicle) return;
    if (!sess.ambientModels_ || !sess.ambientModels_->ready()) {
        cs.spawnDone = true;
        std::printf("[craft] --spawn-vehicle=%s: ambient model registry not ready (needs the vehicles archive and no --no-ambient-models)\n",
                    cs.spawnName.c_str());
        return;
    }
    std::string name = cs.spawnName, where;
    if (const size_t at = name.find('@'); at != std::string::npos) {
        where = name.substr(at + 1);
        name = name.substr(0, at);
    }
    auto& pm = *sess.pm_;
    const bool water = where == "water";
    double tx = 0, tz = 0, theading = 0;
    bool haveTarget = false;
    double fx = sess.heldFacing.x, fz = sess.heldFacing.z;
    {
        const double fl = std::hypot(fx, fz);
        if (fl < 1e-6) { fx = 0; fz = 1; } else { fx /= fl; fz /= fl; }
    }
    if (water) {
        if (!sess.playtestRoadGraph) {
            cs.spawnDone = true;
            std::printf("[craft] --spawn-vehicle=%s@water: no road graph loaded\n", name.c_str());
            return;
        }
        const auto& g = *sess.playtestRoadGraph;
        double best = 1e30;
        size_t bestSeg = SIZE_MAX, boatSegs = 0;
        for (size_t s = 0; s < g.segments.size(); ++s) {
            if (!(g.segments[s].flags & sr3zone::kSegFlagBoat)) continue;
            ++boatSegs;
            const auto& a = g.SegA(s);
            const auto& b = g.SegB(s);
            const double mx = 0.5 * (a.x + b.x), mz = 0.5 * (a.z + b.z);
            const double d = std::hypot(mx - pm.st.position.x, mz - pm.st.position.z);
            if (d < best) { best = d; bestSeg = s; }
        }
        if (bestSeg == SIZE_MAX) {
            cs.spawnDone = true;
            std::printf("[craft] --spawn-vehicle=%s@water: no boat lanes (flag bit 18) in the road graph\n", name.c_str());
            return;
        }
        const auto& a = g.SegA(bestSeg);
        const auto& b = g.SegB(bestSeg);
        tx = 0.5 * (a.x + b.x);
        tz = 0.5 * (a.z + b.z);
        theading = std::atan2(static_cast<double>(b.z - a.z), static_cast<double>(b.x - a.x));
        haveTarget = true;
        if (!cs.spawnWarped)
            std::printf("[craft] water spawn: %zu boat-lane segments; nearest #%zu at (%.1f %.1f %.1f) %.0f m from the player, lane heading %.2f\n",
                        boatSegs, bestSeg, tx, static_cast<double>(0.5f * (a.y + b.y)), tz, best, theading);
        cs.spawnLaneY = 0.5 * (a.y + b.y);
    } else if (!where.empty()) {
        if (std::sscanf(where.c_str(), "%lf,%lf", &tx, &tz) == 2) {
            haveTarget = true;
            theading = std::atan2(fz, fx);
        }
    }
    double px = pm.st.position.x + fx * cs.spawnDist, pz = pm.st.position.z + fz * cs.spawnDist;
    double heading = std::atan2(fz, fx);  // sim convention: forward = (cos h, sin h)
    if (haveTarget) {
        // the player stands cs.spawnDist behind the craft along its heading
        const double hx = std::cos(theading), hz = std::sin(theading);
        if (!cs.spawnWarped) {
            double ny = pm.st.position.y;
            sess.nodeGround.Nearest(tx, tz, 300.0, pm.st.position.y, ny);
            const double wx = tx - hx * cs.spawnDist, wz = tz - hz * cs.spawnDist;
            pm.warp(sr3player::Vec3{wx, water ? std::max(ny, cs.waterLevel) + 1.0 : ny + 1.0, wz});
            cs.spawnWarped = true;
            cs.spawnWarpFrame = sess.frames;
            std::printf("[craft] frame %d: warped the player to (%.1f %.1f %.1f) next to the spawn target (%.1f %.1f)\n", sess.frames, wx,
                        pm.st.position.y, wz, tx, tz);
            return;
        }
        px = tx;
        pz = tz;
        heading = theading;
        // wait for the ground under the craft to resolve (tile streaming), at most 90 frames
        if (sess.frames - cs.spawnWarpFrame < 90) {
            double ny = pm.st.position.y;
            sess.nodeGround.Nearest(px, pz, 300.0, pm.st.position.y, ny);
            const auto probe = sess.sampleGround(px, pz, ny + 2.0, static_cast<float>(ny));
            if (!probe.real() && !water) return;
            if (water && sess.frames - cs.spawnWarpFrame < 30) return;
        }
    }
    cs.spawnDone = true;
    std::string key;
    AmbientVehicleModel* m = sess.ambientModels_->homieVehicleModel(name, &key);
    if (!m) {
        std::printf("[craft] --spawn-vehicle=%s: no such vehicle mesh in the archive (use --spawn-vehicle=list)\n", name.c_str());
        return;
    }
    PlaytestSession::ParkedVehicle pv;
    pv.id = kParkedIdBase + sess.nextParkedId++;
    pv.x = px;
    pv.z = pz;
    pv.heading = heading;
    pv.model = m;
    pv.isGenki = false;
    double refY = pm.st.position.y;
    if (haveTarget) sess.nodeGround.Nearest(px, pz, 300.0, pm.st.position.y, refY);
    const auto g = sess.sampleGround(pv.x, pv.z, haveTarget ? refY + 2.0 : pm.st.position.y + 0.52,
                                     static_cast<float>(haveTarget ? refY : pm.st.position.y));
    const CraftTypeRow* row = FindCraftRow(m->key);
    {   // aircraft / boat meshes: draw every part's highest-detail group (group 0 alone is a few triangles for aircraft)
        static std::set<const AmbientVehicleModel*> lodBuilt;
        static uint64_t lodSession = 0;  // model pointers are registry-owned: reset per session (address reuse)
        if (lodSession != sess.sessionId) { lodBuilt.clear(); lodSession = sess.sessionId; }
        if (row && lodBuilt.insert(m).second) {
            const size_t n = m->rig.RebuildLodHeadChannels(sess.device_->device());
            std::printf("[craft] '%s': drawing %zu LOD-head draw groups (%zu channel renderers) instead of group 0 only\n", m->key.c_str(), n,
                        m->rig.channels.size());
        }
    }
    const BoundsEntry& be = boundsFor(sess, m);
    const float bottom = row ? static_cast<float>(std::max(0.2, -be.bmin.y)) : 0.0f;
    float gy = g.y;
    if (row && row->cls == 5 && cs.hasWater && (!g.real() || g.y < cs.waterLevel)) {
        // a boat is parked afloat on the water plane (open sea, or ground below the plane); where the ground is above the
        // plane it sits on the ground
        gy = static_cast<float>(cs.waterLevel) - 0.3f;
    } else if (!g.real() && haveTarget) {
        gy = static_cast<float>(refY);
    }
    pv.y = gy + bottom;
    sess.parkedVehicles.push_back(pv);
    if (std::getenv("SR3_TRACE_CRAFT_MESH")) {   // mesh census (opt-in diagnostic): helps when a model draws nothing
        size_t verts = 0;
        for (const auto& ch : m->rig.channels) verts += ch.verts.size();
        const auto& dg = m->rig.meshPtr->drawGroups();
        std::printf("[craft] mesh census '%s': ready=%d channels=%zu vertices=%zu materials(srv)=%zu drawGroupsLocated=%d groups=%zu ranges(g0)=%zu wheelsAttached=%zu\n",
                    m->key.c_str(), m->rig.ready ? 1 : 0, m->rig.channels.size(), verts, m->rig.perMaterialSrv.size(),
                    m->rig.meshPtr->drawGroupsLocated() ? 1 : 0, dg.size(), dg.empty() ? 0 : dg[0].size(), m->rig.wheelAttachWorld.size());
        for (const auto& ws : m->rig.wheelSlots)
            std::printf("[craft]   wheel slot part %zu anchor (%.2f %.2f %.2f) front=%d\n", ws.partIndex, ws.anchor[0], ws.anchor[1], ws.anchor[2], ws.isFront ? 1 : 0);
        if (row)
            std::printf("[craft]   row: heli tail (%.2f %.2f %.2f) rotorCenter (%.2f %.2f %.2f) radius %.2f tilt %.3f | plane engines %zu\n",
                        row->heli.tailRotorCenter.x, row->heli.tailRotorCenter.y, row->heli.tailRotorCenter.z, row->heli.rotorCenter.x,
                        row->heli.rotorCenter.y, row->heli.rotorCenter.z, row->heli.rotorRadius, row->heli.rotorTilt,
                        row->plane.enginePoints.size());
    }
    std::printf("[craft] spawned '%s' (class %d%s) at (%.1f %.1f %.1f) heading %.2f; ground %s %.2f; mesh bounds body-frame "
                "(%.2f %.2f %.2f)..(%.2f %.2f %.2f); press E within %.1f m to enter\n",
                m->key.c_str(), row ? row->cls : -1, row ? "" : " NOT a craft class - drives as a ground car", pv.x,
                static_cast<double>(pv.y), pv.z, pv.heading, g.real() ? "mesh" : "none/held", static_cast<double>(g.y), be.bmin.x,
                be.bmin.y, be.bmin.z, be.bmax.x, be.bmax.y, be.bmax.z, static_cast<double>(kVehicleEnterRadius));
}

// ---- scripted "teleport the player up H metres" (stand-in for climbing / being flung; headless freefall evidence) ----
void craftPlayerLiftStep(PlaytestSession& sess) {
    if (sess.inVehicle) return;
    for (const auto& l : sess.craft.playerLifts) {
        if (l.first != sess.frames) continue;
        auto& pm = *sess.pm_;
        sr3player::Vec3 p = pm.st.position;
        p.y += l.second;
        pm.warp(p);
        std::printf("[craft] script: frame %d lifted the player by %.1f m to y=%.2f\n", sess.frames, l.second, p.y);
    }
}

// ---- entering / leaving -----------------------------------------------------------------------------------------------
void craftOnEnter(PlaytestSession& sess) {
    auto& cs = sess.craft;
    cs.rig.Leave();
    sess.vehicleChaseCam.setParams(sr3player::GenkiCameraParams());
    const CraftTypeRow* row = craftRowForActiveCar(sess);
    if (!row) return;
    const Kind kind = KindForVehicleClass(row->cls);
    if (kind == Kind::None) return;
    const BoundsEntry& be = boundsFor(sess, sess.activeCarModel);
    auto& st = sess.vehicleRig_->sim->state_;
    cs.row = row;
    cs.rig.Enter(kind, *row, V3{st.posX, static_cast<double>(sess.vehicleGroundY), st.posZ}, st.heading, be.bmin, be.bmax);
    cs.enterFrame = sess.frames;
    cs.prevToggleKey = false;
    {
        sr3player::VehicleCameraParams cp = CraftCameraParams(kind);
        cp.followDistance *= cs.camScale;
        cp.followHeight *= cs.camScale;
        sess.vehicleChaseCam.setParams(cp);
    }
    std::printf("[craft] entered %s '%s' (class %d, %s data): mass %.0f maxSpeed %.1f half-extents (%.2f %.2f %.2f) bottom %.2f; "
                "controls: %s\n",
                KindName(kind), row->name.c_str(), row->cls,
                (row->dataHeli || row->dataPlane || row->dataBoat) ? "table" : "default-law",
                cs.rig.body.mass, cs.rig.maxSpeed, cs.rig.half.x, cs.rig.half.y, cs.rig.half.z, cs.rig.bottomOffset,
                kind == Kind::Heli ? "W/S D/A cyclic, L-Shift up, L-Ctrl down, Q/C or Space+A/D yaw"
                : kind == Kind::Plane ? "S/W pitch, D/A roll, L-Shift throttle, L-Ctrl brake, Q/C rudder"
                : kind == Kind::Vtol ? "heli keys + F toggles jet/hover (W/S D/A cyclic or pitch/roll)"
                                     : "W/S throttle, A/D steer");
    if (cs.rig.row.cls == 4) cs.rig.vs.heliModel = 1;
}

// Before the existing exit warp: the player leaves from the craft's current altitude (so exiting in the air falls).
void craftOnExitBefore(PlaytestSession& sess) {
    auto& cs = sess.craft;
    if (!cs.rig.active) return;
    sess.vehicleGroundY = static_cast<float>(cs.rig.body.pos.y - cs.rig.bottomOffset);
    if (!cs.rig.onGround && !cs.rig.inWater) {  // stepping out in the air: the player starts falling from rest (CHOSEN)
        cs.playerGrounded = false;
        cs.ffHavePrev = false;
    }
    std::printf("[craft] exit at altitude %.1f m (%s) speed %.1f; stats: max alt %.1f max speed %.1f distance %.0f airborne %.1fs takeoffs %d landings %d\n",
                cs.rig.haveGround ? cs.rig.body.pos.y - cs.rig.bottomOffset - cs.rig.groundYHere : 0.0, KindName(cs.rig.kind),
                pc::Len(cs.rig.body.vel), cs.rig.stats.maxAltitude, cs.rig.stats.maxSpeed, cs.rig.stats.distance,
                cs.rig.stats.airborneSeconds, cs.rig.stats.takeoffs, cs.rig.stats.landings);
}
// After the warp: the empty craft is left parked on the ground below (CHOSEN: parked craft are not simulated, so it is set
// down at once; a re-entry starts from that pose).
void craftOnExitAfter(PlaytestSession& sess) {
    auto& cs = sess.craft;
    if (!cs.rig.active) return;
    double gy = cs.rig.haveGround ? cs.rig.groundYHere : cs.rig.body.pos.y - cs.rig.bottomOffset;
    if (cs.rig.kind == Kind::Boat && cs.hasWater) gy = std::max(gy, cs.waterLevel - 0.3);
    sess.vehicleGroundY = static_cast<float>(gy + cs.rig.bottomOffset);
    sess.vehicleGroundTarget = static_cast<float>(gy);
    sess.vehicleGroundFollower_.emplace(gy);
    cs.rig.Leave();
    sess.vehicleChaseCam.setParams(sr3player::GenkiCameraParams());
}

// ---- the per-frame flight step (replaces PlaytestVehicleRig::Advance while a craft is driven) ------------------------
void craftStepWorld(PlaytestSession& sess, PlaytestFrame& frm) {
    auto& cs = sess.craft;
    auto& r = cs.rig;
    if (!r.active) return;
    auto& st = sess.vehicleRig_->sim->state_;
    CraftControls c = cs.liveControls;
    const bool rawToggle = c.vtolToggle;
    c.vtolToggle = rawToggle && !cs.prevToggleKey;
    cs.prevToggleKey = rawToggle;
    c.camRelative = cs.camRelative;
    c.haveCamHeading = sess.vehicleChaseCamWasDriving;
    {
        const double hc = sess.vehicleChaseCam.heading();  // camera yaw = atan2(z, x) of the camera forward
        c.camHeadingXZ = std::atan2(std::cos(hc), std::sin(hc));  // -> atan2(x, z)
    }
    const CraftEnv env = makeEnv(sess);
    const double px = r.body.pos.x, pz = r.body.pos.z;
    r.Step(frm.dt, c, env);
    ++cs.framesFlown;

    // Horizontal world collision (buildings, walls): the same swept-sphere query the ground rig uses, swept at the craft's
    // own height. CHOSEN radius = 0.3 x the longest hull extent, clamped to [1.2, 4] m.
    if (sess.realCameraCollision_) {
        RealCameraCollisionAsVehicleWorld adapter(*sess.realCameraCollision_);
        sr3physics::VehicleWorldCollisionState s;
        const double h = SimHeadingOf(r.body.basis);
        const double ch = std::cos(h), sh = std::sin(h);
        s.posX = r.body.pos.x;
        s.posZ = r.body.pos.z;
        s.heading = h;
        s.velForwardBody = r.body.vel.x * ch + r.body.vel.z * sh;
        s.velRightBody = r.body.vel.x * sh - r.body.vel.z * ch;
        const double radius = std::min(4.0, std::max(1.2, 0.3 * 2.0 * std::max(r.half.x, r.half.z)));
        const double cx = s.posX, cz = s.posZ;
        sr3physics::ApplyVehicleWorldCollisionResponse(adapter, px, pz, r.body.pos.y, radius, s);
        if (std::fabs(s.posX - cx) > 1e-9 || std::fabs(s.posZ - cz) > 1e-9) {
            r.body.pos.x = s.posX;
            r.body.pos.z = s.posZ;
            r.body.vel.x = s.velForwardBody * ch + s.velRightBody * sh;
            r.body.vel.z = s.velForwardBody * sh - s.velRightBody * ch;
            if (++cs.collisionHits <= 5 || cs.collisionHits % 60 == 0)
                std::printf("[craft-collision] frame %d hit #%llu at (%.1f %.1f %.1f) radius %.1f\n", sess.frames,
                            static_cast<unsigned long long>(cs.collisionHits), r.body.pos.x, r.body.pos.y, r.body.pos.z, radius);
        }
    }

    // Publish the planar pose to the consumers that read the ground rig's state (traffic reference, exit, chase cam, GPS).
    const pc::Basis& b = r.body.basis;
    const double h = SimHeadingOf(b);
    st.posX = r.body.pos.x;
    st.posZ = r.body.pos.z;
    st.heading = h;
    st.velForwardBody = r.body.vel.x * std::cos(h) + r.body.vel.z * std::sin(h);
    st.velRightBody = r.body.vel.x * std::sin(h) - r.body.vel.z * std::cos(h);
    st.yawRate = r.body.angVel.y;
    sess.vehicleGroundY = static_cast<float>(r.body.pos.y);  // render Y = mesh origin

    if (sess.frames % cs.traceEvery == 0)
        PrintCraftTrace(KindName(r.kind), sess.frames, r, r.groundYHere, r.haveGround);
}

// World matrix of the active car (render + shadow). `fallbackY` = the ground rig's render Y.
void craftWorldMatrix(PlaytestSession& sess, float fallbackY, float out[16]) {
    auto& st = sess.vehicleRig_->sim->state_;
    if (sess.craft.rig.active) {
        CraftWorldMatrix(sess.craft.rig, sess.craft.meshForwardAxis, out);
        return;
    }
    if (craftRowForActiveCar(sess)) {  // a parked craft: level on its skids at the stored heading
        CraftRig tmp;
        tmp.body.pos = {st.posX, static_cast<double>(fallbackY), st.posZ};
        tmp.q = FromSimHeading(st.heading);
        tmp.body.basis = BasisOf(tmp.q);
        CraftWorldMatrix(tmp, sess.craft.meshForwardAxis, out);
        return;
    }
    sr3render::buildWorldMatrixSimHeading(static_cast<float>(st.posX), fallbackY, static_cast<float>(st.posZ),
                                static_cast<float>(st.heading), 1.0f, out);
}
void craftParkedWorldMatrix(const PlaytestSession& sess, const PlaytestSession::ParkedVehicle& pk, float out[16]) {
    if (pk.model && !CraftRegistry().empty() && FindCraftRow(pk.model->key)) {
        CraftRig tmp;
        tmp.body.pos = {pk.x, static_cast<double>(pk.y), pk.z};
        tmp.q = FromSimHeading(pk.heading);
        tmp.body.basis = BasisOf(tmp.q);
        CraftWorldMatrix(tmp, sess.craft.meshForwardAxis, out);
        return;
    }
    sr3render::buildWorldMatrixSimHeading(static_cast<float>(pk.x), pk.y, static_cast<float>(pk.z), static_cast<float>(pk.heading), 1.0f, out);
}

// ---- freefall + parachute from the on-foot step (spec-player-runtime.md Sec19) --------------------------------------
namespace ffr = sr3player::freefall;

void freefallPreStep(PlaytestSession& sess, PlaytestFrame& frm, double groundRawY, bool groundReal) {
    auto& cs = sess.craft;
    auto& ff = cs.ff;
    auto& pm = *sess.pm_;
    const double dt = frm.dt;
    cs.ffOverride = false;
    const bool chutePressed = cs.chuteEdgeThisFrame;
    cs.chuteEdgeThisFrame = false;
    if (!cs.freefallEnabled || dt <= 0.0) {
        cs.ffHavePrev = false;
        return;
    }
    const double x = pm.st.position.x, y = pm.st.position.y, z = pm.st.position.z;
    double vx = 0, vz = 0, fall = 0;
    if (cs.ffHavePrev) {
        fall = (cs.ffPrevY - y) / dt;
        vx = (x - cs.ffPrevX) / dt;
        vz = (z - cs.ffPrevZ) / dt;
    }
    cs.ffPrevX = x; cs.ffPrevY = y; cs.ffPrevZ = z;
    cs.ffHavePrev = true;
    cs.ffFallSpeed = fall;
    const bool airborne = !cs.playerGrounded;
    const double clearance = y - groundRawY;
    const bool groundNear = groundReal && clearance <= ffr::DefaultConstants().minClearDistance;
    ffr::TickTimers(ff, dt);
    if (ff.inFreefall) {
        cs.ffPeakFall = std::max(cs.ffPeakFall, fall);
        if (chutePressed) {
            ffr::ButtonInputs bi;
            bi.fallSpeed = fall;
            const ffr::ButtonResult res = ffr::PressActionButton(ff, bi, vx, vz);
            if (res == ffr::ButtonResult::Deployed) {
                ++cs.ffDeploys;
                std::printf("freefall-trace: frame %d chute DEPLOYED at y=%.1f (fall %.1f m/s, clearance %.1f m)\n", sess.frames, y, fall, clearance);
            } else if (res == ffr::ButtonResult::CutAway) {
                ++cs.ffCuts;
                std::printf("freefall-trace: frame %d chute CUT AWAY at y=%.1f (fall %.1f m/s)\n", sess.frames, y, fall);
            } else {
                std::printf("freefall-trace: frame %d action press ignored (deploy lock %.0f ms)\n", sess.frames, ff.lockMs);
            }
        }
    } else if (airborne) {
        ffr::StartInputs si;
        si.locomotion = 6;
        si.fallSpeed = fall;
        si.groundWithinClearDistance = groundNear;
        if (ffr::CanStartFreefall(si)) {
            ffr::StartFreefall(ff, vx, vz, true);
            ++cs.ffStarts;
            cs.ffStartY = y;
            cs.ffPeakFall = fall;
            std::printf("freefall-trace: frame %d freefall START at (%.1f %.1f %.1f) fall %.1f m/s clearance %.1f m\n", sess.frames, x, y, z, fall, clearance);
        } else if (chutePressed && fall > 0.0) {
            // CBA_GAC_ACTION while falling but before the start test passes: deploy starts freefall without a lock (19.4 step 2)
            ffr::Deploy(ff, vx, vz);
            ++cs.ffStarts;
            ++cs.ffDeploys;
            cs.ffStartY = y;
            std::printf("freefall-trace: frame %d chute DEPLOYED directly (starts freefall, no lock) at y=%.1f\n", sess.frames, y);
        }
    }
    if (ff.inFreefall) {
        ffr::MoveInputs mi;
        mi.A = pm.world.readControlsDisabled() ? 0.0 : static_cast<double>(sess.moveSrc.A);
        mi.B = pm.world.readControlsDisabled() ? 0.0 : static_cast<double>(sess.moveSrc.B);
        const sr3player::CameraTransform& cam = sess.followCam_->committed();
        mi.fwdX = cam.forward.x; mi.fwdZ = cam.forward.z;
        mi.rtX = cam.right.x; mi.rtZ = cam.right.z;
        mi.faceX = ff.speed > 0.1 ? ff.facingX : sess.heldFacing.x;
        mi.faceZ = ff.speed > 0.1 ? ff.facingZ : sess.heldFacing.z;
        mi.groundNear = groundNear;
        ffr::UpdateMovement(ff, mi, dt);
        cs.ffOverride = true;
        if (sess.frames % cs.traceEvery == 0)
            std::printf("freefall-trace: frame %d %s y=%.1f fall %.1f T %.1f r (%.1f %.1f) speed %.1f b %.2f clearance %.1f lock %.0f ms\n",
                        sess.frames, ff.chuteOpen ? "CHUTE" : "FREEFALL", y, fall, ff.terminalT, ff.rx, ff.rz, ff.speed, ff.b, clearance, ff.lockMs);
    }
}

void freefallPostStep(PlaytestSession& sess) {
    auto& cs = sess.craft;
    auto& ff = cs.ff;
    if (!ff.inFreefall) return;
    ffr::EndInputs ei;
    ei.landed = cs.playerGrounded;
    ei.locomotion = 8;  // locomotion state is not modelled; only 'landed' ends the free-roam freefall here
    bool playLanding = false;
    const bool wasChute = ff.chuteOpen;
    if (ffr::CheckEnd(ff, ei, playLanding)) {
        ++cs.ffLandings;
        cs.ffLastLandSpeed = cs.ffFallSpeed;
        std::printf("freefall-trace: frame %d LANDED %s at y=%.2f, last fall speed %.1f m/s (peak %.1f), fell %.1f m, landing animation %d\n",
                    sess.frames, wasChute ? "under the chute" : "in freefall (no chute)", sess.pm_->st.position.y,
                    cs.ffFallSpeed, cs.ffPeakFall, cs.ffStartY - sess.pm_->st.position.y, playLanding ? 1 : 0);
        cs.ffOverride = false;
    }
}

void printCraftSummary(const PlaytestSession& sess) {
    const auto& cs = sess.craft;
    if (cs.framesFlown == 0 && cs.ffStarts == 0 && cs.spawnName.empty()) return;
    const auto& s = cs.rig.stats;
    std::printf("craft-trace: SUMMARY kind=%s frames_flown=%llu max_alt=%.1f max_speed=%.1f distance=%.0f airborne_s=%.1f takeoffs=%d "
                "landings=%d max_impact=%.1f max_submerged_m3=%.2f collision_hits=%llu bank_deg=[%.0f..%.0f] water_level=%.2f%s\n",
                KindName(cs.rig.kind), static_cast<unsigned long long>(cs.framesFlown), s.maxAltitude, s.maxSpeed, s.distance,
                s.airborneSeconds, s.takeoffs, s.landings, s.maxImpact, s.maxSubmergedVolume,
                static_cast<unsigned long long>(cs.collisionHits), s.minPlaneBankDeg, s.maxPlaneBankDeg, cs.waterLevel,
                cs.hasWater ? "" : " (no water)");
    std::printf("freefall-trace: SUMMARY starts=%d deploys=%d cutaways=%d landings=%d peak_fall=%.1f last_landing_fall=%.1f in_freefall_at_end=%d\n",
                cs.ffStarts, cs.ffDeploys, cs.ffCuts, cs.ffLandings, cs.ffPeakFall, cs.ffLastLandSpeed, cs.ff.inFreefall ? 1 : 0);
}

}  // namespace sr3viewer
