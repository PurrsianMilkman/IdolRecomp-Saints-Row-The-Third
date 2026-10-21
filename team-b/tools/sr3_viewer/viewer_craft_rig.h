// viewer_craft_rig.h - the free-roam 3-D rigid body for player-flown helicopters, planes, VTOLs and boats
// (TEAM B task AW-15, 2026-10-19). Pure (no D3D, no engine globals) so tests/synthetic_craft_rig_test.cpp can fly it.
//
// SPEC USED (the ONLY source of engine facts): spec-physics-runtime.md Sec10.13 (laws live in
// include/sr3physics/aircraft_watercraft.h, called here unchanged), 10.13.1 step order, 10.13.6 buoyancy,
// 10.13.8 Team B wiring; spec-tables-ui-controls.md 18.3 default PC bindings (Helicopter / Plane / Car sets);
// spec-player-runtime.md 4.15 (chase camera, reused).
//
// What the rig adds around the laws (everything below is CHOSEN unless a spec section is cited):
//   * Havok's job: gravity (0,-9.8,0) each substep, semi-implicit Euler integration of position and of the
//     orientation quaternion (q' = q + 0.5 * omega * q * dt), world-frame omega. The laws only change v / omega.
//   * Ground contact (the laws never see terrain): four footprint corners (CHOSEN +-0.6 of the mesh half extents,
//     bottomOffset below the origin) against the walkable ground height under the craft (one probe per frame, flat
//     approximation between corners). Penetration is pushed out along +Y, downward velocity is removed (inelastic),
//     no crash damage (10.13.7 is not wired). On the ground: heli skid friction, plane wheel model (lateral kill,
//     rolling drag, brake, nose-wheel yaw: the wheeled step 0x00ad97c0 of Sec10 is NOT modelled), upright levelling.
//   * Water (Sec10.13.6 says the water-height source is OPEN): a flat plane at waterY, CHOSEN 0.0 m (--water-level
//     overrides); where the ground is above it there is no water. Boats: the hull box (mesh bounds) is cut into a
//     grid of cells, each runs SubmergedBoxApprox + BuoyancyStep (rho 1000 * V * 9.8 * k, k = Buoyancy_Modifier or
//     0.8) so a pitch/roll righting torque exists; drag is the 10.13.8 item 2 placeholder v *= (1 - c dt) (GUESS c)
//     plus a CHOSEN keel (sideways velocity killed) and angular damping.
//   * Speed clamp (Sec10.11.10): |v| <= Maximum_Speed (row) else the 44.704 m/s (100 mph) cap of the vehicle table.
//   * Sub-stepping: dt is split into steps of at most 1/60 s (the laws' vectors use 1/30).
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "sr3physics/aircraft_watercraft.h"
#include "sr3player/freefall.h"

namespace sr3viewer_craft {

namespace pc = sr3physics::craft;
using pc::V3;

enum class Kind { None, Heli, Plane, Vtol, Boat };

inline Kind KindForVehicleClass(int cls) {
    switch (cls) {
        case 2: return Kind::Plane;
        case 3: return Kind::Heli;
        case 4: return Kind::Vtol;
        case 5: return Kind::Boat;
        default: return Kind::None;
    }
}
inline const char* KindName(Kind k) {
    switch (k) {
        case Kind::Heli: return "heli";
        case Kind::Plane: return "plane";
        case Kind::Vtol: return "vtol";
        case Kind::Boat: return "boat";
        default: return "none";
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Vehicle-table row subset the rig needs (filled by the viewer from sr3vehicleinfo::VehicleEntry, spec-vehicle-data.md
// Sec7.5; fields the reader does not expose keep the law defaults, which are the spec test-vector values).
// ---------------------------------------------------------------------------------------------------------------
struct CraftTypeRow {
    std::string name;
    int cls = -1;
    double mass = 0.0;           // Mass (0 = use the law default)
    double buoyancy = 0.0;       // Water_Physics/Buoyancy_Modifier (0 = absent)
    double maxSpeed = 0.0;       // Maximum_Speed (m/s, 0 = absent)
    pc::HeliParams heli;         // class 3, or the VTOL Helicopter_Mode block
    pc::PlaneParams plane;       // class 2, or the VTOL Airplane_Mode block
    pc::BoatParams boat;         // class 5
    bool dataHeli = false, dataPlane = false, dataBoat = false;  // true when the block came from the table
};

inline std::map<std::string, CraftTypeRow>& CraftRegistry() {
    static std::map<std::string, CraftTypeRow> r;  // lower-case vehicle name -> row (classes 2..5 only)
    return r;
}
inline std::string LowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// Model key ("heli_standard_02_0" or the plain name) -> row; nullptr when the vehicle is not a craft class.
inline const CraftTypeRow* FindCraftRow(const std::string& modelKey) {
    const auto& reg = CraftRegistry();
    std::string k = LowerAscii(modelKey);
    auto it = reg.find(k);
    if (it != reg.end()) return &it->second;
    const size_t us = k.find_last_of('_');
    if (us != std::string::npos && us + 1 < k.size() && std::isdigit(static_cast<unsigned char>(k[us + 1])) &&
        k.size() - us <= 3) {
        it = reg.find(k.substr(0, us));
        if (it != reg.end()) return &it->second;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------
// Orientation quaternion. Identity = body frame equal to world: right +X, up +Y, forward +Z.
// ---------------------------------------------------------------------------------------------------------------
struct Quat {
    double w = 1, x = 0, y = 0, z = 0;
};
inline Quat Mul(const Quat& a, const Quat& b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}
inline Quat Normalized(Quat q) {
    const double n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (n < 1e-12) return Quat{};
    return {q.w / n, q.x / n, q.y / n, q.z / n};
}
inline V3 Rotate(const Quat& q, V3 v) {
    const V3 u{q.x, q.y, q.z};
    const V3 t = pc::Cross(u, v) * 2.0;
    return v + t * q.w + pc::Cross(u, t);
}
inline pc::Basis BasisOf(const Quat& q) {
    pc::Basis b;
    b.right = Rotate(q, {1, 0, 0});
    b.up = Rotate(q, {0, 1, 0});
    b.fwd = Rotate(q, {0, 0, 1});
    return b;
}
inline Quat AxisAngle(V3 axis, double ang) {
    const V3 a = pc::Norm(axis, {0, 1, 0});
    const double s = std::sin(0.5 * ang);
    return {std::cos(0.5 * ang), a.x * s, a.y * s, a.z * s};
}
// q' = q + 0.5 * (0, omega) * q * dt  (omega in world frame).
inline Quat Integrate(const Quat& q, V3 omega, double dt) {
    const Quat dq = Mul(Quat{0.0, omega.x, omega.y, omega.z}, q);
    return Normalized(Quat{q.w + 0.5 * dt * dq.w, q.x + 0.5 * dt * dq.x, q.y + 0.5 * dt * dq.y, q.z + 0.5 * dt * dq.z});
}
inline Quat Slerp(const Quat& a, Quat b, double t) {
    double d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    if (d < 0) {
        b = {-b.w, -b.x, -b.y, -b.z};
        d = -d;
    }
    if (d > 0.9995) {
        return Normalized(Quat{a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t});
    }
    const double th = std::acos(std::min(1.0, d));
    const double s0 = std::sin((1 - t) * th) / std::sin(th), s1 = std::sin(t * th) / std::sin(th);
    return Normalized(Quat{a.w * s0 + b.w * s1, a.x * s0 + b.x * s1, a.y * s0 + b.y * s1, a.z * s0 + b.z * s1});
}
// Heading (sim convention of vehicle_substeps.h): forward on the ground plane = (cos h, sin h) in (x, z).
inline Quat FromSimHeading(double h, double pitch = 0.0) {
    const double theta = pc::kPi / 2.0 - h;  // rotation about +Y taking +Z to (cos h, 0, sin h)
    const Quat yaw = AxisAngle({0, 1, 0}, theta);
    return Normalized(Mul(yaw, AxisAngle({1, 0, 0}, -pitch)));  // positive pitch = nose up
}
inline double SimHeadingOf(const pc::Basis& b) { return std::atan2(b.fwd.z, b.fwd.x); }
inline double PitchOf(const pc::Basis& b) { return std::asin(pc::Clamp(b.fwd.y, -1.0, 1.0)); }  // + = nose up
inline double YawHeadingXZ(V3 v) { return std::atan2(v.x, v.z); }  // atan2(x, z): freefall.h Q-F1 convention

// ---------------------------------------------------------------------------------------------------------------
// Controls (device-neutral; the viewer fills them from the Sec18.3 default PC bindings or from a script).
// ---------------------------------------------------------------------------------------------------------------
struct CraftControls {
    double fwd = 0;        // W - S   (+1 = W)
    double right = 0;      // D - A   (+1 = D)
    double up = 0;         // L-Shift: heli collective up / plane throttle / (boat unused)
    double down = 0;       // L-Ctrl : heli collective down / plane brake-reverse
    double yawKeys = 0;    // +1 = turn-right key, -1 = turn-left key (CHOSEN Q / C; also Space + A/D for heli)
    bool turnSelect = false;   // Space: CBA_VDC_HELI_DRIVE_TURN_SELECT (A/D act as the turn keys)
    bool vtolToggle = false;   // F edge: VTOL jet / hover request
    bool camRelative = false;  // heli camera-relative flying (10.13.2 last row)
    double camHeadingXZ = 0;   // atan2(x, z) of the chase camera forward
    bool haveCamHeading = false;
};

// Environment queries supplied by the caller (viewer: sr3 walkable ground ray + the flat water plane).
struct CraftEnv {
    // ground height under (x, z); false = no real ground (open sea, tile not resident).
    std::function<bool(double x, double z, double fromY, double& outY)> ground;
    bool hasWater = true;
    double waterY = 0.0;
};

struct CraftStats {
    double maxAltitude = 0, maxSpeed = 0, distance = 0, airborneSeconds = 0, seconds = 0;
    int takeoffs = 0, landings = 0;
    double lastImpact = 0, maxImpact = 0;
    double maxSubmergedVolume = 0;
    double minPlaneBankDeg = 0, maxPlaneBankDeg = 0;
};

class CraftRig {
public:
    Kind kind = Kind::None;
    CraftTypeRow row;
    pc::CraftBody body;
    Quat q;
    pc::HeliParams hp;
    pc::PlaneParams pp;
    pc::BoatParams bp;
    pc::HeliState hs;
    pc::PlaneState ps;
    pc::VtolState vs;
    pc::BoatState bs;
    double vtolAnimMs = 0.0;
    // geometry in the BODY frame (right, up, fwd) relative to the mesh origin which is the body origin / COM
    double bottomOffset = 1.0;           // origin height above the lowest footprint point
    V3 half{1.2, 0.8, 3.0};              // half extents (right, up, fwd)
    V3 hullCenter{0, 0, 0};              // bounds centre (body frame)
    double maxSpeed = 44.704;
    double buoyancyK = 0.8;
    double waterDragC = 0.5;             // GUESS, 10.13.8 item 2
    double waterAngDamp = 2.5;           // CHOSEN
    bool active = false;
    // Sec10.13.7 note (Team A 2026-10-20, HIGH): an empty craft is NOT set down; the laws keep running. occupied=false:
    // a helicopter (no driver, no remote) uses a = a_lim + 4.9 with collective 0 (sinks under half-cancelled gravity), a
    // plane loses its throttle, a boat floats on its buoyancy. Enter() sets it true; Vacate() clears it.
    bool occupied = true;
    bool onGround = false, inWater = false, propUnder = false;
    double groundYHere = 0.0;
    bool haveGround = false;
    double lastGroundY = 0.0;
    bool haveLastGround = false;
    int groundMissFrames = 0;
    CraftStats stats;
    // last-step diagnostics
    double lastCollective = 0, lastThrottle = 0;
    double submergedVolume = 0;

    bool airborne() const { return !onGround && !inWater; }

    // pos = origin position; the mesh bounds (body frame) are given as min/max corners (right, up, fwd).
    void Enter(Kind k, const CraftTypeRow& r, V3 position, double simHeading, V3 boundsMin, V3 boundsMax) {
        kind = k;
        row = r;
        hp = r.heli;
        pp = r.plane;
        bp = r.boat;
        const double mass = r.mass > 1.0 ? r.mass : (k == Kind::Heli || k == Kind::Vtol ? hp.mass : (k == Kind::Plane ? pp.mass : bp.mass));
        hp.mass = pp.mass = bp.mass = mass;
        hs = pc::HeliState{};
        ps = pc::PlaneState{};
        vs = pc::VtolState{};
        bs = pc::BoatState{};
        vtolAnimMs = 0;
        maxSpeed = r.maxSpeed > 1.0 ? r.maxSpeed : 44.704;
        buoyancyK = r.buoyancy > 0.01 ? r.buoyancy : 0.8;
        body = pc::CraftBody{};
        body.mass = mass;
        body.pos = position;
        q = FromSimHeading(simHeading);
        body.basis = BasisOf(q);
        half = {0.5 * (boundsMax.x - boundsMin.x), 0.5 * (boundsMax.y - boundsMin.y), 0.5 * (boundsMax.z - boundsMin.z)};
        hullCenter = {0.5 * (boundsMax.x + boundsMin.x), 0.5 * (boundsMax.y + boundsMin.y), 0.5 * (boundsMax.z + boundsMin.z)};
        bottomOffset = std::max(0.2, -boundsMin.y);
        // Inertia is Havok's (Q-A1: the spec gives only the mass). CHOSEN here: the solid-box tensor of the mesh bounds
        // (I = M (a^2 + b^2) / 3 for half extents a, b about the third axis) instead of the law default M/4 per axis,
        // which turned a held cyclic into a flip.
        {
            const double hx = std::max(0.5, half.x), hy = std::max(0.5, half.y), hz = std::max(0.5, half.z);
            body.invInertiaLocal = {3.0 / (mass * (hy * hy + hz * hz)),   // about right (pitch)
                                    3.0 / (mass * (hx * hx + hz * hz)),   // about up (yaw)
                                    3.0 / (mass * (hx * hx + hy * hy))};  // about forward (roll)
            // Helicopters: every torque of 10.13.2 scales with M (rotor ring k_i M R, tail force 4 M), so the response only
            // depends on the radius of gyration k. A hull-sized k (about 2 m) turns a held cyclic into a flip; CHOSEN
            // k = 0.8 x Rotor_Radius (3.2 m for the 4 m rotor), which settles a full forward stick near 20 deg of tilt, the
            // angle spec vector H3 uses.
            if (k == Kind::Heli || k == Kind::Vtol) {
                const double kg = std::max(2.0, 0.8 * (r.heli.rotorRadius > 0.5 ? r.heli.rotorRadius : 4.0));
                const double inv = 1.0 / (mass * kg * kg);
                body.invInertiaLocal = {inv, inv, inv};
            }
        }
        // The reader does not expose Engine_Thrust_Position (10.13.5 names it, spec-vehicle-data 7.5 does not): CHOSEN at the
        // stern bottom of the hull so the prop is under water when the boat floats.
        bp.thrustPosition = {hullCenter.x, boundsMin.y + 0.15, boundsMin.z + 0.4};
        stats = CraftStats{};
        onGround = inWater = propUnder = false;
        haveLastGround = false;
        groundMissFrames = 0;
        active = true;
        occupied = true;
    }
    void Leave() { active = false; }
    void Vacate() { occupied = false; }  // the pilot got out; the craft keeps being stepped (not wired in the playtest yet)

    pc::Basis Basis() const { return body.basis; }

    // One viewer frame.
    void Step(double dt, const CraftControls& c, const CraftEnv& env) {
        if (!active || dt <= 0.0) return;
        // one ground probe per frame (flat approximation between the corners)
        {
            double gy = 0.0;
            haveGround = env.ground && env.ground(body.pos.x, body.pos.z, body.pos.y + 2.0, gy);
            if (haveGround) {
                lastGroundY = gy;
                groundMissFrames = 0;
                haveLastGround = true;
            } else if (haveLastGround && ++groundMissFrames <= 20) {
                // a single missed ray (tile seam, hole) keeps the previous height: "on a miss, leave Y unchanged"
                // (spec-zone-data-format.md 13.5.1), for at most 20 frames
                haveGround = true;
                gy = lastGroundY;
            }
            groundYHere = haveGround ? gy : -1.0e9;
        }
        const int n = std::max(1, static_cast<int>(std::ceil(dt / (1.0 / 60.0) - 1e-9)));
        const double h = dt / n;
        const V3 before = body.pos;
        const bool wasAir = airborne();
        for (int i = 0; i < n; ++i) SubStep(h, c, env, i == 0 ? c.vtolToggle : false);
        const V3 d = body.pos - before;
        stats.distance += std::sqrt(d.x * d.x + d.z * d.z);
        stats.seconds += dt;
        stats.maxAltitude = std::max(stats.maxAltitude, haveGround ? body.pos.y - groundYHere : body.pos.y);
        stats.maxSpeed = std::max(stats.maxSpeed, pc::Len(body.vel));
        if (airborne()) stats.airborneSeconds += dt;
        if (wasAir && !airborne()) ++stats.landings;
        if (!wasAir && airborne()) ++stats.takeoffs;
        if (kind == Kind::Plane || (kind == Kind::Vtol && vs.heliModel == 0)) {
            const double bank = pc::PlaneBank(body.basis) * 180.0 / pc::kPi;
            stats.minPlaneBankDeg = std::min(stats.minPlaneBankDeg, bank);
            stats.maxPlaneBankDeg = std::max(stats.maxPlaneBankDeg, bank);
        }
    }

private:
    void CornerWorld(int i, V3& out, double scale = 0.6) const {
        const double sx = (i & 1) ? 1.0 : -1.0, sz = (i & 2) ? 1.0 : -1.0;
        const V3 local{sx * half.x * scale + hullCenter.x, -bottomOffset, sz * half.z * scale + hullCenter.z};
        out = body.pos + body.basis.ToWorld(local);
    }

    void SubStep(double h, const CraftControls& c, const CraftEnv& env, bool toggleEdge) {
        body.basis = BasisOf(q);
        // ---- water sampling first (boat law needs inWater / propUnder) ----
        double waterLevel = env.hasWater ? env.waterY : -1.0e9;
        submergedVolume = 0.0;
        std::vector<pc::SubmergedSample> cells;
        if (kind == Kind::Boat) {
            const int nx = 2, nz = 5;
            const double cw = 2.0 * half.x / nx, cl = 2.0 * half.z / nz;
            // Only the hull proper floats: the bounds also hold masts and cabins, which would give 17 g of buoyancy when
            // pushed under. CHOSEN hull height = min(full height, 1.2 m) above the keel (Havok clips the real shapes).
            const double hullH = std::min(2.0 * half.y, 1.2);
            const double hullMidY = (hullCenter.y - half.y) + 0.5 * hullH;
            for (int ix = 0; ix < nx; ++ix)
                for (int iz = 0; iz < nz; ++iz) {
                    const V3 local{hullCenter.x - half.x + cw * (ix + 0.5), hullMidY, hullCenter.z - half.z + cl * (iz + 0.5)};
                    const V3 wc = body.pos + body.basis.ToWorld(local);
                    // water exists here only where the ground is below the plane (or unknown: open sea); one ground
                    // probe per frame (the craft centre) decides for the whole hull (CHOSEN flat approximation)
                    const double wy = (haveGround && groundYHere > waterLevel) ? -1.0e9 : waterLevel;
                    pc::SubmergedSample s = pc::SubmergedBoxApprox(wc, 0.5 * hullH, cw, cl, wy);
                    if (s.volume > 0.0) {
                        submergedVolume += s.volume;
                        cells.push_back(s);
                    }
                }
            stats.maxSubmergedVolume = std::max(stats.maxSubmergedVolume, submergedVolume);
            inWater = submergedVolume > 0.0;
            const V3 P = body.pos + body.basis.ToWorld(bp.thrustPosition);
            propUnder = inWater && P.y < waterLevel;
        } else {
            inWater = false;
            propUnder = false;
        }

        // ---- the law (Sec10.13.1 order is inside each *Step) ----
        const double speed = pc::Len(body.vel);
        switch (kind) {
            case Kind::Heli: HeliLaw(h, c, hp, hs); break;
            case Kind::Plane: PlaneLaw(h, c, pp, ps); break;
            case Kind::Vtol: {
                vtolAnimMs += h * 1000.0;
                const bool jetReached = vs.machine == 1 && vtolAnimMs >= 1000.0;    // CHOSEN 1 s wing animation
                const bool hoverReached = vs.machine == 3 && vtolAnimMs >= 1000.0;
                const int before = vs.machine;
                pc::VtolMachineStep(vs, toggleEdge, jetReached, hoverReached);
                if (vs.machine != before) vtolAnimMs = 0.0;
                pc::VtolApplyRequest(vs);
                pc::VtolKickStep(vs, body, h);
                if (vs.heliModel == 1) HeliLaw(h, c, row.heli, hs);
                else PlaneLaw(h, c, row.plane, ps);
                break;
            }
            case Kind::Boat: {
                pc::BoatInput in;
                in.throttle = pc::Clamp(c.fwd, -1.0, 1.0);
                in.steer = pc::Clamp(c.right, -1.0, 1.0);
                in.pitch = pc::Clamp(-c.fwd, 0.0, 1.0) * 1.0;  // brake - accelerate (Y'), only the braking part tightens LBS
                in.stopAssist = false;
                lastThrottle = in.throttle;
                pc::BoatStep(bp, bs, in, inWater, propUnder, onGround, body, h);
                for (const auto& s : cells) pc::BuoyancyStep(body, s, buoyancyK, h);
                if (inWater) {
                    pc::WaterDragPlaceholder(body, waterDragC, h);
                    // keel: sideways velocity relative to the hull is killed (CHOSEN)
                    const double side = pc::Dot(body.vel, body.basis.right);
                    body.vel = body.vel - body.basis.right * (side * std::min(1.0, 3.0 * h));
                    body.angVel = body.angVel * std::max(0.0, 1.0 - waterAngDamp * h);
                    body.vel.y *= std::max(0.0, 1.0 - 2.0 * h);  // heave damping (CHOSEN; the drag law is OPEN)
                }
                break;
            }
            default: break;
        }
        (void)speed;

        // ---- Havok's part: gravity, speed clamp, integration ----
        body.vel.y -= pc::kGravity * h;
        const double sp = pc::Len(body.vel);
        const double cap = maxSpeed;  // Sec10.11.10 hard clamp (Maximum_Speed of the row, else the 100 mph table cap)
        if (sp > cap) body.vel = body.vel * (cap / sp);
        body.pos += body.vel * h;
        q = Integrate(q, body.angVel, h);
        body.basis = BasisOf(q);

        ResolveContacts(h, c, env, waterLevel);
    }

    void HeliLaw(double h, const CraftControls& c, const pc::HeliParams& p, pc::HeliState& s) {
        pc::HeliInput in;
        in.collective = pc::Clamp(c.up - c.down, -1.0, 1.0);
        lastCollective = in.collective;
        const bool turnHeld = c.turnSelect || std::fabs(c.yawKeys) > 0.0;
        const double axisLR = c.turnSelect ? 0.0 : c.right;                 // Space held: A/D become the turn keys
        double yawKey = c.yawKeys;
        if (c.turnSelect) yawKey += c.right;
        double mag, psi;
        pc::HeliCyclicFromXY(-axisLR, -c.fwd, mag, psi);  // X = -LEFT_RIGHT, Y = -FORWARD_BACKWARD (10.13.2 table)
        double yaw = pc::HeliYawFromKeys(yawKey < -0.01, yawKey > 0.01);
        if (c.camRelative && c.haveCamHeading) {
            const double d = pc::WrapPi(YawHeadingXZ(body.basis.fwd) - c.camHeadingXZ);
            const pc::HeliCameraRelative r = pc::HeliApplyCameraRelative(psi, mag, d, yaw, turnHeld);
            psi = r.psi;
            yaw = r.yaw;
        }
        in.cyclicMag = mag;
        in.cyclicAngle = psi;
        in.yaw = yaw;
        pc::HeliFlags fl;
        fl.rotorHealthy = true;
        fl.hasDriverOrRemote = occupied;
        fl.resting = pc::HeliRestPredicate(p, onGround, pc::Len(body.vel), in.collective);
        pc::HeliStep(p, s, in, fl, body, h);
    }

    void PlaneLaw(double h, const CraftControls& c, const pc::PlaneParams& p, pc::PlaneState& s) {
        pc::PlaneInput in;
        double Xp, Yp;
        // raw X = roll axis (D = +1); raw Y = -PITCH_UP_DOWN with S (pull back) = nose up = in.pitch +1 (see the test)
        pc::PlaneCircleToSquare(c.right, c.fwd * -1.0, Xp, Yp);
        in.roll = pc::Clamp(Xp, -1.0, 1.0);
        in.pitch = pc::Clamp(Yp, -1.0, 1.0);
        in.rudder = pc::Clamp(-c.yawKeys, -1.0, 1.0);
        in.throttle = occupied ? pc::Clamp(c.up - c.down, -1.0, 1.0) : 0.0;  // an empty plane loses its throttle
        lastThrottle = in.throttle;
        pc::PlaneFlags fl;
        fl.onGroundPartial = onGround;
        fl.onGround = onGround;
        pc::PlaneStep(p, s, in, fl, body, h);
        lastPitchInput = in.pitch;
    }
    double lastPitchInput = 0.0;

    // Levels orientation toward (yaw kept, pitch target, roll 0) by fraction f.
    void LevelTo(double pitchTarget, double f) {
        const double hdg = SimHeadingOf(body.basis);
        const Quat tgt = FromSimHeading(hdg, pitchTarget);
        q = Slerp(q, tgt, pc::Clamp(f, 0.0, 1.0));
        body.basis = BasisOf(q);
    }

    void ResolveContacts(double h, const CraftControls& c, const CraftEnv& env, double waterLevel) {
        (void)env;
        (void)waterLevel;
        bool contact = false;
        double pen = 0.0;
        if (haveGround) {
            for (int i = 0; i < 4; ++i) {
                V3 w;
                CornerWorld(i, w);
                pen = std::max(pen, groundYHere - w.y);
            }
            contact = pen > 0.0 || (body.pos.y - bottomOffset) <= groundYHere + 0.05;
        }
        if (!haveGround && kind == Kind::Boat && body.pos.y < waterLevel - 60.0) {  // safety floor for open sea
            body.pos.y = waterLevel - 60.0;
            body.vel.y = std::max(0.0, body.vel.y);
        }
        if (contact) {
            if (pen > 0.0) body.pos.y += pen;
            if (body.vel.y < 0.0) {
                stats.lastImpact = -body.vel.y;
                stats.maxImpact = std::max(stats.maxImpact, stats.lastImpact);
                body.vel.y = 0.0;
            }
            const double u = pc::Dot(body.vel, body.basis.fwd);
            switch (kind) {
                case Kind::Heli:
                case Kind::Vtol: {
                    if (kind == Kind::Heli || vs.heliModel == 1) {
                        body.vel.x *= std::max(0.0, 1.0 - 3.0 * h);  // skid friction
                        body.vel.z *= std::max(0.0, 1.0 - 3.0 * h);
                        body.angVel = body.angVel * std::max(0.0, 1.0 - 4.0 * h);
                        LevelTo(0.0, 4.0 * h);
                        break;
                    }
                    [[fallthrough]];
                }
                case Kind::Plane: {
                    // wheel model (CHOSEN): lateral friction, rolling drag, brake, nose-wheel yaw
                    const V3 fwdH = pc::Norm(V3{body.basis.fwd.x, 0, body.basis.fwd.z}, {1, 0, 0});
                    const V3 rightH{fwdH.z, 0, -fwdH.x};
                    const double lat = pc::Dot(body.vel, rightH);
                    body.vel = body.vel - rightH * (lat * std::min(1.0, 8.0 * h));
                    const double brake = (c.down > 0.0 && u > 0.5) ? 0.6 * c.down : 0.0;
                    const double drag = 0.05 + brake;
                    body.vel.x *= std::max(0.0, 1.0 - drag * h);
                    body.vel.z *= std::max(0.0, 1.0 - drag * h);
                    const bool rotate = u > 0.6 * pp.liftMinSpeed && lastPitchInput > 0.05;
                    body.angVel = body.angVel * std::max(0.0, 1.0 - 3.0 * h);
                    if (!rotate) LevelTo(0.0, 3.0 * h);
                    else {
                        // keep the wings level but let the nose come up
                        const double hdg = SimHeadingOf(body.basis);
                        const double pit = std::max(0.0, PitchOf(body.basis));
                        q = Slerp(q, FromSimHeading(hdg, pit), pc::Clamp(3.0 * h, 0.0, 1.0));
                        body.basis = BasisOf(q);
                    }
                    // nose-wheel steering while rolling (the law's yaw needs bank): yaw rate follows the roll keys
                    const double steer = pc::Clamp(c.right + c.yawKeys, -1.0, 1.0);
                    const double wantYaw = steer * 0.5 * pc::Clamp(std::fabs(u) / 6.0, 0.0, 1.0) * (u >= 0 ? 1.0 : -1.0);
                    body.angVel.y = body.angVel.y + (-wantYaw - body.angVel.y) * std::min(1.0, 5.0 * h);
                    break;
                }
                case Kind::Boat: {
                    body.vel.x *= std::max(0.0, 1.0 - 2.5 * h);  // hull grounded on the bed
                    body.vel.z *= std::max(0.0, 1.0 - 2.5 * h);
                    body.angVel = body.angVel * std::max(0.0, 1.0 - 3.0 * h);
                    break;
                }
                default: break;
            }
        }
        onGround = contact;
    }
};

// ---------------------------------------------------------------------------------------------------------------
// Scripted controls: --script-craft=START-END:ch=val[,ch=val...][;START-END:...]  (frame numbers, inclusive)
// channels: fwd right up down yaw space (1 = held)  vtol (edge on START frame) chute (edge on START frame)
// ---------------------------------------------------------------------------------------------------------------
struct ScriptSeg {
    int a = 0, b = 0;
    CraftControls c;
    bool chute = false;
};
inline void ParseCraftScript(const std::string& spec, std::vector<ScriptSeg>& out) {
    size_t pos = 0;
    while (pos < spec.size()) {
        size_t semi = spec.find(';', pos);
        if (semi == std::string::npos) semi = spec.size();
        const std::string seg = spec.substr(pos, semi - pos);
        pos = semi + 1;
        const size_t colon = seg.find(':');
        if (colon == std::string::npos) continue;
        ScriptSeg s;
        const std::string range = seg.substr(0, colon);
        const size_t dash = range.find('-');
        s.a = std::atoi(range.c_str());
        s.b = dash == std::string::npos ? s.a : std::atoi(range.c_str() + dash + 1);
        size_t p2 = colon + 1;
        while (p2 <= seg.size()) {
            size_t comma = seg.find(',', p2);
            if (comma == std::string::npos) comma = seg.size();
            const std::string kv = seg.substr(p2, comma - p2);
            p2 = comma + 1;
            const size_t eq = kv.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = kv.substr(0, eq);
            const double v = std::atof(kv.c_str() + eq + 1);
            if (k == "fwd") s.c.fwd = v;
            else if (k == "right") s.c.right = v;
            else if (k == "up") s.c.up = v;
            else if (k == "down") s.c.down = v;
            else if (k == "yaw") s.c.yawKeys = v;
            else if (k == "space") s.c.turnSelect = v != 0.0;
            else if (k == "vtol") s.c.vtolToggle = v != 0.0;
            else if (k == "chute") s.chute = v != 0.0;
            if (comma >= seg.size()) break;
        }
        out.push_back(s);
    }
}
// Combined controls for a frame (later segments override earlier ones per channel only through addition of
// non-overlapping ranges; edge channels fire on the first frame of their segment).
inline CraftControls ScriptControlsAt(const std::vector<ScriptSeg>& segs, int frame, bool& chuteEdge) {
    CraftControls out;
    chuteEdge = false;
    for (const ScriptSeg& s : segs) {
        if (frame < s.a || frame > s.b) continue;
        out.fwd += s.c.fwd;
        out.right += s.c.right;
        out.up += s.c.up;
        out.down += s.c.down;
        out.yawKeys += s.c.yawKeys;
        out.turnSelect = out.turnSelect || s.c.turnSelect;
        if (frame == s.a) {
            out.vtolToggle = out.vtolToggle || s.c.vtolToggle;
            chuteEdge = chuteEdge || s.chute;
        }
    }
    return out;
}

}  // namespace sr3viewer_craft
