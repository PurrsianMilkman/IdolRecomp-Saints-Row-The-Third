// viewer_craft_wiring.h - the playtest-side adapters between the free-roam viewer and the pure libraries
//   sr3physics/aircraft_watercraft.h   (spec-physics-runtime.md Sec10.13, Team B wiring 10.13.8)  via viewer_craft_rig.h
//   sr3player/freefall.h               (spec-player-runtime.md Sec19)
//
// STATUS (2026-10-19, task AW-15): WIRED. Heli / plane / VTOL / boat models entered through the normal E entry fly
// on a 3-D rigid body (CraftRig) instead of the planar VehicleSubstepSimulator; freefall + parachute runs from the
// on-foot step. Everything is opt-in by data: a vehicle whose Vehicle_Type is 0/1 (cars, bikes) never touches this
// code, and the original genki rig car and all ambient cars keep the old path byte for byte.
// Flags: --spawn-vehicle=NAME[@water|@X,Z] (or =list), --water-level=Y, --no-water, --craft-camrel, --craft-mesh-fwd=x|z|-x|-z,
// --script-craft=..., --script-e-at=F,..., --script-player-lift=FRAME:H, --craft-trace-every=N, --no-freefall.
// Traces: "craft-trace:" and "freefall-trace:" lines; SR3_TRACE_FREEFALL=1 keeps the old read-only start probe line.
#pragma once

#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "viewer_craft_rig.h"
#include "sr3physics/aircraft_watercraft.h"
#include "sr3player/freefall.h"
#include "sr3player/vehicle_camera.h"

namespace sr3viewer_craft {

// Vehicle_Type (spec-vehicle-data.md Sec7.5) -> which per-step law applies (10.13.8 item 1/2).
inline sr3physics::craft::CraftLaw LawForVehicleClass(int vehicleClass) {
    return sr3physics::craft::CraftLawForVehicleClass(vehicleClass);
}

// Free-roam freefall start test per spec-player-runtime.md Sec19.4 step 1 from what the playtest rig knows on foot:
// clearance below the player (position.y - ground), the downward speed measured from the previous frame, and whether
// the character is in the falling locomotion state. The remaining gates default to "not blocked".
struct FreefallProbe {
    double prevY = 0.0;
    bool havePrev = false;
    bool wouldStart = false;
    double fallSpeed = 0.0;
    double clearance = 0.0;

    void Update(double posY, double groundY, double dt, bool fallingState) {
        fallSpeed = (havePrev && dt > 0.0) ? (prevY - posY) / dt : 0.0;
        prevY = posY;
        havePrev = true;
        clearance = posY - groundY;
        sr3player::freefall::StartInputs in;
        in.locomotion = fallingState ? 6 : 0;
        in.fallSpeed = fallSpeed;
        in.groundWithinClearDistance = clearance <= sr3player::freefall::DefaultConstants().minClearDistance;
        wouldStart = sr3player::freefall::CanStartFreefall(in);
    }
};

// Mesh-local axis -> body frame (right, up, forward) per "which mesh axis points forward". The vehicle meshes' long axis
// is mesh Z (measured: car_4dr_standard03 +-2.4 m on Z, +-1 m on X; boat_speed01 +-5.9 m on Z), and the spec body frame
// is x right / y up / z forward (Sec10.13: tail rotor at local z = -7), so the default is mode 1 (identity). Modes keep
// the mapping a proper rotation (determinant +1): 0 = +X forward, 1 = +Z, 2 = -X, 3 = -Z.
inline std::array<V3, 3> MeshAxesToBody(int mode) {
    switch (mode & 3) {
        case 0: return {V3{0, 0, 1}, V3{0, 1, 0}, V3{-1, 0, 0}};
        case 1: return {V3{1, 0, 0}, V3{0, 1, 0}, V3{0, 0, 1}};
        case 2: return {V3{0, 0, -1}, V3{0, 1, 0}, V3{1, 0, 0}};
        default: return {V3{-1, 0, 0}, V3{0, 1, 0}, V3{0, 0, -1}};
    }
}
inline V3 MeshToBody(int mode, V3 p) {
    const auto a = MeshAxesToBody(mode);
    return a[0] * p.x + a[1] * p.y + a[2] * p.z;
}
// Body-frame bounds of a mesh whose raw local bounds are [mn, mx].
inline void MeshBoundsToBody(int mode, V3 mn, V3 mx, V3& outMin, V3& outMax) {
    outMin = {1e30, 1e30, 1e30};
    outMax = {-1e30, -1e30, -1e30};
    for (int i = 0; i < 8; ++i) {
        const V3 p{(i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z};
        const V3 b = MeshToBody(mode, p);
        outMin = {std::min(outMin.x, b.x), std::min(outMin.y, b.y), std::min(outMin.z, b.z)};
        outMax = {std::max(outMax.x, b.x), std::max(outMax.y, b.y), std::max(outMax.z, b.z)};
    }
}
// Raw mesh bounds of a loaded PlaytestVehicleRig-like object (template: this header stays D3D-free).
template <class RigT>
inline bool RawMeshBounds(const RigT& rig, V3& mn, V3& mx) {
    mn = {1e30, 1e30, 1e30};
    mx = {-1e30, -1e30, -1e30};
    size_t n = 0;
    for (const auto& ch : rig.channels)
        for (const auto& p : ch.basePositions) {
            mn = {std::min(mn.x, (double)p[0]), std::min(mn.y, (double)p[1]), std::min(mn.z, (double)p[2])};
            mx = {std::max(mx.x, (double)p[0]), std::max(mx.y, (double)p[1]), std::max(mx.z, (double)p[2])};
            ++n;
        }
    return n > 0;
}
// Row-vector world matrix (rows = images of the mesh X / Y / Z axes, row 3 = translation) for the craft body.
inline void CraftWorldMatrix(const CraftRig& r, int mode, float out[16]) {
    const auto a = MeshAxesToBody(mode);
    for (int i = 0; i < 3; ++i) {
        const V3 w = r.body.basis.ToWorld(a[static_cast<size_t>(i)]);
        out[i * 4 + 0] = static_cast<float>(w.x);
        out[i * 4 + 1] = static_cast<float>(w.y);
        out[i * 4 + 2] = static_cast<float>(w.z);
        out[i * 4 + 3] = 0.0f;
    }
    out[12] = static_cast<float>(r.body.pos.x);
    out[13] = static_cast<float>(r.body.pos.y);
    out[14] = static_cast<float>(r.body.pos.z);
    out[15] = 1.0f;
}

// Chase camera row for a craft (spec-player-runtime.md 4.15 gives the genki row only; per-class rows are CHOSEN here:
// the same follow-swing / FOV machinery, a longer and higher follow for the big machines).
inline sr3player::VehicleCameraParams CraftCameraParams(Kind k) {
    sr3player::VehicleCameraParams p = sr3player::GenkiCameraParams();
    switch (k) {
        case Kind::Heli:
        case Kind::Vtol: p.lookatOffsetY = 1.5; p.followDistance = 17.0; p.followHeight = 5.0; break;
        case Kind::Plane: p.lookatOffsetY = 1.5; p.followDistance = 24.0; p.followHeight = 6.0; break;
        case Kind::Boat: p.lookatOffsetY = 1.2; p.followDistance = 12.0; p.followHeight = 3.5; break;
        default: break;
    }
    p.maxSpeedMs = 44.704;
    return p;
}

// Per-session state (flags, scripts, the rig, freefall state, counters). Lives in PlaytestSession::craft.
struct CraftSessionState {
    // ---- options ----
    std::string spawnName;          // --spawn-vehicle=NAME ("list" prints the craft rows and exits the spawn step)
    int spawnAtFrame = 3;
    double spawnDist = 2.5;         // m ahead of the player (the entry radius is 3 m to the vehicle origin)
    bool spawnDone = false;
    bool spawnWarped = false;       // two-stage spawn (@water / @X,Z): the player has been warped to the target
    int spawnWarpFrame = 0;
    double spawnLaneY = 0.0;
    double waterLevel = 0.0;        // CHOSEN flat sea plane (spec 10.13.6: the source is OPEN)
    bool hasWater = true;
    bool camRelative = false;       // 10.13.2 last row (byte 0x011807dc = 1 in the image) - OFF here: see --craft-camrel
    double camScale = 1.0;          // --craft-cam-scale=S: scales the chase camera follow distance / height (evidence close-ups)
    int meshForwardAxis = 1;         // mesh +Z forward (spec Sec10.13 body frame: x right, y up, z forward); --craft-mesh-fwd=
    int traceEvery = 30;
    bool freefallEnabled = true;
    std::vector<ScriptSeg> script;
    std::vector<int> eFrames;                       // --script-e-at: scripted E presses
    std::vector<std::pair<int, double>> playerLifts;  // --script-player-lift=FRAME:H
    bool registryWanted = false;
    bool registryLoaded = false;

    // ---- runtime ----
    CraftRig rig;
    const CraftTypeRow* row = nullptr;
    int enterFrame = -1;
    bool prevToggleKey = false;
    CraftControls liveControls;          // filled by the input step each frame
    bool chuteEdgeScript = false;
    double lastSpeed = 0.0;
    uint64_t framesFlown = 0;
    uint64_t collisionHits = 0;
    bool chuteEdgeThisFrame = false;     // E edge (or scripted chute) this frame, consumed by the freefall step

    // ---- freefall (spec-player-runtime.md Sec19) ----
    sr3player::freefall::FreefallState ff;
    bool ffPrevGrounded = true;
    double ffPrevY = 0.0, ffPrevX = 0.0, ffPrevZ = 0.0;
    bool ffHavePrev = false;
    double ffVelX = 0.0, ffVelZ = 0.0;   // measured horizontal velocity (start velocity r)
    double ffFallSpeed = 0.0;
    double ffStartY = 0.0, ffPeakFall = 0.0, ffLastLandSpeed = 0.0;
    bool ffOverride = false;             // the collision hook uses (rx, rz) and terminalT this frame
    bool ffChuteKeyPrev = false;
    int ffStarts = 0, ffDeploys = 0, ffCuts = 0, ffLandings = 0;
    double ffAirborneMs = 0.0;
    bool playerGrounded = true;          // from the last collision-hook result
};

inline void PrintCraftTrace(const char* tag, int frame, const CraftRig& r, double groundY, bool haveGround) {
    const pc::Basis& b = r.body.basis;
    std::printf("craft-trace: %s frame %d pos (%.1f %.2f %.1f) vel (%.2f %.2f %.2f) speed %.2f alt %.2f ground %.2f%s "
                "heading %.1f pitch %.1f bank %.1f onGround %d inWater %d%s\n",
                tag, frame, r.body.pos.x, r.body.pos.y, r.body.pos.z, r.body.vel.x, r.body.vel.y, r.body.vel.z,
                pc::Len(r.body.vel), haveGround ? r.body.pos.y - r.bottomOffset - groundY : r.body.pos.y, groundY,
                haveGround ? "" : "(none)", SimHeadingOf(b) * 180.0 / pc::kPi, PitchOf(b) * 180.0 / pc::kPi,
                pc::PlaneBank(b) * 180.0 / pc::kPi, r.onGround ? 1 : 0, r.inWater ? 1 : 0,
                r.kind == Kind::Boat ? " (boat)" : "");
}

}  // namespace sr3viewer_craft
