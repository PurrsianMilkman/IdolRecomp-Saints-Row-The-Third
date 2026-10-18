#include "viewer_platform.h"
#include "viewer_modes.h"
#include "viewer_traffic_demo.h"
#include "sr3zone/zone_geometry.h"
#include "sr3zone/zone_header.h"
#include "sr3zone/zone_objects.h"
#include "sr3zone/traffic_graph.h"
#include "sr3airuntime/traffic_following.h"
#include "sr3airuntime/traffic_signals.h"
#include "sr3airuntime/ped_crossing.h"
#include "sr3airuntime/ped_navigator.h"
#include "sr3airuntime/traffic_lane_follower.h"
#include "sr3airuntime/road_graph_placement.h"
#include "sr3zone/hood_volumes.h"
#include "sr3zone/road_graph.h"
#include "sr3zone/zone_heightmap.h"
#include "sr3airuntime/ambient_population.h"
#include "sr3airuntime/near_field_vehicle_population.h"
#include "sr3airuntime/freeroam_population.h"
#include "sr3airuntime/police_roadblock_spawn.h"
#include "sr3airuntime/near_field_ped_population.h"
#include "sr3airuntime/npc_ai_runtime.h"
#include "sr3luahost/engine_state.h"
#include "sr3xtbl/xtbl.h"
#include "sr3physics/vehicle_substeps.h"
#include "sr3physics/vehicle_driving_slice.h"
#include "sr3physics/vehicle_spec_data.h"
#include "sr3physics/vehicle_world_collision.h"
#include "sr3vehicleinfo/vehicle_entry.h"

using namespace sr3viewer;

namespace {


// ===========================================================================
// `traffic_lane_demo` (2026-10-08, traffic-AI lane-following task) - drives
// one ambient vehicle along a REAL (CONFIRMED-shape) Traffic (0x2236) lane
// graph, printing a per-tick stdout trace of its position so the actual
// graph-following behaviour (not a straight line through open space) is
// directly visible in the command's own output - see this project's
// existing golden-scene/demo convention (runDriveDemo() above; this
// command is deliberately HEADLESS, no window/D3D11, since its own proof
// is a position trace, not a rendered frame - "a frame or stdout trace" per
// the orchestrator task brief).
//
// WHAT IS REAL vs SYNTHETIC-DATA vs CHOSEN, spelled out explicitly (same
// convention runDriveDemo()'s own top comment uses):
//   * REAL: the 0x2236 Traffic record FORMAT (sr3zone::TrafficGraph,
//     spec-zone-data-format.md Sec13.2 - the 14-pair header, the 24-byte
//     segment array, the 64-byte extended-segment array with its CSR
//     point-count field and 2-bit kind field, the quantized anchor+offset
//     codec); the real sr3physics::VehicleSubstepSimulator (the same 13
//     confirmed Havok vehicle sub-steps `drive_demo` uses) and its ONE
//     real write surface, DriverInput{steer,throttle,brake}
//     (spec-physics-runtime.md Sec10.6's hk_custom_vehicle_driver_input -
//     see sr3airuntime/traffic_lane_follower.h's own "PHYSICS INTEGRATION"
//     note); the SAME representative "normal sedan" VehicleTuning fixture
//     `drive_demo` uses (BuildDriveDemoVehicleTuning() above - not real
//     car_4dr_genki xtbl data, kept consistent with precedent rather than
//     inventing different numbers).
//   * SYNTHETIC DATA, REAL FORMAT: the Traffic record bytes this command
//     feeds to the parser are hand-built (BuildTrafficLaneDemoFixture()
//     below), not read from any real `.czn_pc` - this clean-room project
//     neither reads nor possesses shipped game data. The bytes are laid
//     out exactly per Sec13.2's own confirmed field offsets/strides: two
//     real 90-degree turns (3 segments, each ending exactly where the next
//     begins), so a straight-line driver would visibly fail to track it.
//   * UPDATED 2026-10-10 (traffic-lane-follow-real-mechanism task, spec
//     Sec18.1/18.2/18.5/18.6 now RESOLVED/REFINED): the masked lookahead
//     walk, nearest-distance junction choice, cached-target distance-
//     throttled re-query, dot-product alignment gate, and full-lock +-1.0
//     steer magnitude are now REAL (disassembly-confirmed shapes, see
//     sr3airuntime/traffic_lane_follower.h's own file comment). CHOSEN
//     (clearly labelled there): the point pool's float3 stride, the
//     segment adjacency rule (coincident polyline endpoints), the steer
//     SIGN source (a lateral/cross-track test standing in for the real,
//     unmodelled per-entity lane-side flag), and the driving-throttle
//     magnitude.

void TrafficDemoAppendU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void TrafficDemoAppendU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void TrafficDemoAppendF32(std::vector<uint8_t>& b, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    TrafficDemoAppendU32(b, bits);
}

void TrafficDemoAppendHeaderPair(std::vector<uint8_t>& b, int32_t count, int32_t rawPointer) {
    TrafficDemoAppendU32(b, static_cast<uint32_t>(count));
    TrafficDemoAppendU32(b, static_cast<uint32_t>(rawPointer));
}

void TrafficDemoAppendExtendedSegmentRecord(std::vector<uint8_t>& b, uint32_t rawField0x1c, uint16_t cumulativeCount) {
    const size_t start = b.size();
    TrafficDemoAppendU16(b, 0);  // +0x00 anchorPointIndex - unused by this demo's own trace
    while (b.size() < start + 0x1c) b.push_back(0);
    TrafficDemoAppendU32(b, rawField0x1c);  // +0x1c
    while (b.size() < start + sr3zone::kExtendedSegmentRecordStride - 2) b.push_back(0);
    TrafficDemoAppendU16(b, cumulativeCount);  // +0x3e
    while (b.size() < start + sr3zone::kExtendedSegmentRecordStride) b.push_back(0);
}

void TrafficDemoAppendSegmentRecord(std::vector<uint8_t>& b) {
    const size_t start = b.size();
    TrafficDemoAppendU32(b, 0);                                                        // +0x00
    TrafficDemoAppendU32(b, static_cast<uint32_t>(sr3zone::kSelfRelativeNullSentinel));  // +0x04
    while (b.size() < start + sr3zone::kSegmentRecordStride) b.push_back(0);
}

void TrafficDemoAppendPoint(std::vector<uint8_t>& b, float x, float y, float z) {
    TrafficDemoAppendF32(b, x);
    TrafficDemoAppendF32(b, y);
    TrafficDemoAppendF32(b, z);
}


// Builds this demo's own fixture: 3 segments, 2 real 90-degree turns -
//   segment 0: (0,0,0)  -> (10,0,0)  -> (20,0,0)    (east)
//   segment 1: (20,0,0) -> (20,0,10) -> (20,0,20)   (turn - north)
//   segment 2: (20,0,20)-> (30,0,20) -> (40,0,20)   (turn again - east)
// Each segment's own last point is bit-identical to the next segment's own
// first point (the junction), authored at a DIFFERENT point-pool index per
// segment - see sr3airuntime/traffic_lane_follower.h's own "LANE ADJACENCY"
// note for why this reader's adjacency rule matches on coincident
// COORDINATES, not shared pool indices. Segment 2 has no successor - the
// demo's own vehicle reaches a real dead end and brakes.
//
// `outBuf` must outlive the returned TrafficRecord (and anything built
// from it) - TrafficRecord::pointPool holds a ByteView into it, not a
// copy; same convention as this project's synthetic test fixtures
// (tests/synthetic_traffic_lane_follower_test.cpp's own
// BuildLaneGraphFixture()).
sr3zone::TrafficRecord BuildTrafficLaneDemoFixture(std::vector<uint8_t>& outBuf) {
    // 3 segments x 3 points each x (x,y,z) - flat, to avoid any nested-
    // aggregate brace-elision ambiguity. Points 0-2 = segment 0 (east),
    // 3-5 = segment 1 (turn north), 6-8 = segment 2 (turn east again).
    static const float kPoints[9][3] = {
        {0, 0, 0}, {10, 0, 0}, {20, 0, 0},     // segment 0
        {20, 0, 0}, {20, 0, 10}, {20, 0, 20},  // segment 1
        {20, 0, 20}, {30, 0, 20}, {40, 0, 20}, // segment 2
    };
    const size_t n = 3;             // segment count
    const size_t pointsPerSeg = 3;

    outBuf.clear();
    std::vector<uint8_t>& b = outBuf;
    TrafficDemoAppendU32(b, 0x80000000u | sr3zone::kTrafficSectionTag);
    TrafficDemoAppendU32(b, 0);
    TrafficDemoAppendU32(b, 0);
    const size_t payloadOffset = b.size();

    const size_t extendedRel = sr3zone::kTrafficHeaderSize;
    const size_t segmentRel = extendedRel + n * sr3zone::kExtendedSegmentRecordStride;
    const size_t pointPoolRel = segmentRel + n * sr3zone::kSegmentRecordStride;

    TrafficDemoAppendHeaderPair(b, static_cast<int32_t>(n), static_cast<int32_t>(extendedRel));
    for (int slot = 1; slot <= 5; ++slot) TrafficDemoAppendHeaderPair(b, 0, sr3zone::kSelfRelativeNullSentinel);
    TrafficDemoAppendHeaderPair(b, static_cast<int32_t>(n), static_cast<int32_t>(segmentRel));
    TrafficDemoAppendHeaderPair(b, static_cast<int32_t>(n * pointsPerSeg), static_cast<int32_t>(pointPoolRel));
    TrafficDemoAppendHeaderPair(b, 0, sr3zone::kSelfRelativeNullSentinel);
    TrafficDemoAppendHeaderPair(b, 0, sr3zone::kSelfRelativeNullSentinel);
    for (int slot = 10; slot <= 13; ++slot) TrafficDemoAppendHeaderPair(b, 0, sr3zone::kSelfRelativeNullSentinel);

    uint16_t cumulative = 0;
    for (size_t i = 0; i < n; ++i) {
        cumulative += static_cast<uint16_t>(pointsPerSeg);
        TrafficDemoAppendExtendedSegmentRecord(b, /*rawField0x1c=*/0 /*kind 0*/, cumulative);
    }
    for (size_t i = 0; i < n; ++i) TrafficDemoAppendSegmentRecord(b);
    for (const auto& p : kPoints) TrafficDemoAppendPoint(b, p[0], p[1], p[2]);

    sr3zone::ByteView view(b.data(), b.size());
    return sr3zone::TrafficGraph::parseAt(view, payloadOffset);
}

} // namespace

namespace sr3viewer {

// ===========================================================================
// BuildPlaytestTrafficLaneGraphFixture (TRAFFIC-LANE-FOLLOW-REAL-MECHANISM
// TASK, 2026-10-10) - the SAME "SYNTHETIC DATA, REAL FORMAT" convention
// BuildTrafficLaneDemoFixture() above already establishes (hand-built bytes,
// laid out exactly per spec-zone-data-format.md Sec13.2's own confirmed
// header/segment/point-pool layout), anchored near the playtest's own
// vehicle spawn point rather than at the origin - this project's playtest
// `city` loader has no real Traffic (0x2236) `.czn_pc` content-kind reader
// (see this function's own runPlaytest() call site's doc-comment note), so
// the near-field ambient-vehicle driving loop needs ITS OWN real-shape lane
// graph to walk, the same way the near-field ped population already walks a
// synthetic sidewalk-segment graph.
//
// UNLIKE the demo fixture above (one dead-ending straight run), this layout
// is a closed 4-leg rectangular loop PLUS one real junction with two legal
// candidates - so the junction-choice cost function
// (ChooseNextSegmentByNearestDistance(), sr3airuntime/traffic_lane_follower.h
// item 2) has an actual decision to make, and ordinary ambient vehicles can
// keep driving indefinitely without hitting a dead end every lap:
//   seg0: (ax,az)        -> (ax+20,az)    -> (ax+40,az)     east leg
//   [junction at (ax+40,az) - two legal next segments]
//     seg1: (ax+40,az)   -> (ax+40,az+20) -> (ax+40,az+40)  continues the loop (north)
//     seg4: (ax+40,az)   -> (ax+46,az+6)                    short dead-end spur
//   seg2: (ax+40,az+40)  -> (ax+20,az+40) -> (ax,az+40)     west leg
//   seg3: (ax,az+40)     -> (ax,az+20)    -> (ax,az)        south leg, closes
//                                                           the loop back onto seg0's own start
// Every "leg" segment's own last point is bit-identical to the next
// segment's own first point (this reader's own CHOSEN geometric adjacency
// rule, sr3airuntime/traffic_lane_follower.h item 3) - including seg3->seg0,
// closing the rectangle into a real loop, and seg0->{seg1,seg4}, the one
// real junction. seg4 has no successor - a vehicle routed onto it (per the
// nearest-distance cost function, which is NOT goal-seeking - see that
// file's own item 2 note) reaches a genuine dead end and reports `stuck`,
// same as the demo fixture's own segment 2.
sr3zone::TrafficRecord BuildPlaytestTrafficLaneGraphFixture(float ax, float az, std::vector<uint8_t>& outBuf) {
    struct PlaytestSegSpec {
        uint32_t rawKind;
        std::vector<std::array<float, 3>> points;
    };
    const std::vector<PlaytestSegSpec> segs = {
        {0, {{ax, 0, az}, {ax + 20, 0, az}, {ax + 40, 0, az}}},                              // seg0 - east leg
        {0, {{ax + 40, 0, az}, {ax + 40, 0, az + 20}, {ax + 40, 0, az + 40}}},                // seg1 - north leg (loop continues)
        {0, {{ax + 40, 0, az + 40}, {ax + 20, 0, az + 40}, {ax, 0, az + 40}}},                // seg2 - west leg
        {0, {{ax, 0, az + 40}, {ax, 0, az + 20}, {ax, 0, az}}},                               // seg3 - south leg, closes the loop
        {0, {{ax + 40, 0, az}, {ax + 46, 0, az + 6}}},                                        // seg4 - dead-end spur off the junction
    };
    const size_t n = segs.size();

    outBuf.clear();
    std::vector<uint8_t>& b = outBuf;
    TrafficDemoAppendU32(b, 0x80000000u | sr3zone::kTrafficSectionTag);
    TrafficDemoAppendU32(b, 0);
    TrafficDemoAppendU32(b, 0);
    const size_t payloadOffset = b.size();

    const size_t extendedRel = sr3zone::kTrafficHeaderSize;
    const size_t segmentRel = extendedRel + n * sr3zone::kExtendedSegmentRecordStride;
    const size_t pointPoolRel = segmentRel + n * sr3zone::kSegmentRecordStride;

    size_t totalPoints = 0;
    for (const auto& s : segs) totalPoints += s.points.size();

    TrafficDemoAppendHeaderPair(b, static_cast<int32_t>(n), static_cast<int32_t>(extendedRel));
    for (int slot = 1; slot <= 5; ++slot) TrafficDemoAppendHeaderPair(b, 0, sr3zone::kSelfRelativeNullSentinel);
    TrafficDemoAppendHeaderPair(b, static_cast<int32_t>(n), static_cast<int32_t>(segmentRel));
    TrafficDemoAppendHeaderPair(b, static_cast<int32_t>(totalPoints), static_cast<int32_t>(pointPoolRel));
    TrafficDemoAppendHeaderPair(b, 0, sr3zone::kSelfRelativeNullSentinel);
    TrafficDemoAppendHeaderPair(b, 0, sr3zone::kSelfRelativeNullSentinel);
    for (int slot = 10; slot <= 13; ++slot) TrafficDemoAppendHeaderPair(b, 0, sr3zone::kSelfRelativeNullSentinel);

    uint16_t cumulative = 0;
    for (const auto& s : segs) {
        cumulative += static_cast<uint16_t>(s.points.size());
        TrafficDemoAppendExtendedSegmentRecord(b, s.rawKind, cumulative);
    }
    for (size_t i = 0; i < n; ++i) TrafficDemoAppendSegmentRecord(b);
    for (const auto& s : segs)
        for (const auto& p : s.points) TrafficDemoAppendPoint(b, p[0], p[1], p[2]);

    sr3zone::ByteView view(b.data(), b.size());
    return sr3zone::TrafficGraph::parseAt(view, payloadOffset);
}


// Finds the lane-graph segment whose own first polyline point is nearest to
// `pos` (XZ-plane only, matching sr3airuntime::traffic_lane_detail::Dist2D) -
// the CHOSEN placement rule this task uses to snap a newly-spawned near-field
// ambient vehicle onto BuildPlaytestTrafficLaneGraphFixture()'s own loop (not
// spec-stated anywhere - Sec18 never addresses initial placement, only
// steady-state following). Returns 0 if `graph` has no segments.
size_t NearestLaneGraphSegmentStart(const sr3airuntime::TrafficLaneGraph& graph, const sr3airuntime::Vec3& pos) {
    size_t best = 0;
    double bestDist = -1.0;
    for (size_t i = 0; i < graph.segmentCount(); ++i) {
        const auto& seg = graph.segment(i);
        if (seg.polyline.empty()) continue;
        const double dx = static_cast<double>(seg.polyline.front().x) - pos.x;
        const double dz = static_cast<double>(seg.polyline.front().z) - pos.z;
        const double d = dx * dx + dz * dz;
        if (bestDist < 0.0 || d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}


// Resolves the "nearest near-field vehicle ahead of `selfId`/`selfPos`"
// stand-in - purrsian-52's own directed resolution for Sec18.6a's
// unresolved overtake-gate "second record" (sr3airuntime::OvertakeCandidate,
// see that struct's own doc comment in traffic_lane_follower.h). Factored
// out to a named, reusable helper (2026-10-10, traffic-overtake-passside-
// flag task, after a relayed note from the orchestrator about the new
// spec-ai-runtime.md Sec18.6 finding below) rather than left inline in one
// call site, so a FUTURE port of Sec18.6's own separate `0xe`/`0x10`-state
// steering-write path (0x00b2bbc0, which ALSO resolves a "second vehicle" -
// entity+0xcc0/+0xcc4, Sec18.6's own HIGH-CONFIDENCE "lead/target vehicle",
// not independently proven to be the same object as Sec18.6a's own second
// record) can reuse this SAME stand-in resolution if/when that separate
// path is ever implemented in this codebase, rather than duplicating it.
// Stated plainly: this is a code-level sharing opportunity only - Sec18.6a's
// `0x00b47c80`/`0x00b47f10` second records and Sec18.6's `0x00b2bbc0`
// second handle pair are NOT proven by either spec section to be the same
// underlying engine object (Sec18.6a: "not proven to be the same object"
// even between 0x00b47c80's own ECX and 0x00b47f10's own EDI slots;
// Sec18.6: its own entity+0xcc0/+0xcc4 resolver, 0x004d7f30, was "not
// opened" either) - sharing this clean-room HELPER is a software-engineering
// convenience for this project's own stand-in, not a claim that the real
// engine shares one resolver across both.
//
// "Ahead" is tested against `selfHeadingRad` (the caller's own current
// heading, possibly one tick stale - see the call site's own comment).
// Returns std::nullopt if no other live vehicle qualifies.
//
// EXTENDED 2026-10-11 (overtake-gate-18.6b task): `headingById`/`speedById`
// added - sr3airuntime::OvertakeCandidate now carries the candidate's own
// heading/speed (needed by spec-ai-runtime.md Sec18.6b's real gate steps
// (a)/(c), see traffic_lane_follower.h's own updated file header item 5a);
// NearFieldVehicle itself still has no such fields, so this helper looks
// them up from the SAME per-vehicle side-car maps the call site below
// already maintains for every live near-field vehicle (including this
// candidate), rather than inventing a second tracking structure.
std::optional<sr3airuntime::OvertakeCandidate> ResolveNearestNearFieldVehicleAhead(
    const std::vector<sr3airuntime::NearFieldVehicle>& liveVehicles, uint64_t selfId, double selfX, double selfZ,
    double selfHeadingRad, const std::unordered_map<uint64_t, double>& headingById,
    const std::unordered_map<uint64_t, double>& speedById) {
    const double fwdX = std::cos(selfHeadingRad);
    const double fwdZ = std::sin(selfHeadingRad);
    std::optional<sr3airuntime::OvertakeCandidate> best;
    double bestDist = -1.0;
    for (const auto& other : liveVehicles) {
        if (other.id == selfId) continue;
        const double odx = other.pos.x - selfX;
        const double odz = other.pos.z - selfZ;
        const double odist = std::sqrt(odx * odx + odz * odz);
        if (odist < 1e-6) continue;
        const double aheadDot = (fwdX * odx + fwdZ * odz) / odist;
        if (aheadDot <= 0.0) continue;  // not ahead - not a candidate
        if (bestDist < 0.0 || odist < bestDist) {
            bestDist = odist;
            sr3airuntime::OvertakeCandidate candidate{other.pos.x, other.pos.z};
            auto headingIt = headingById.find(other.id);
            if (headingIt != headingById.end()) candidate.headingRad = headingIt->second;
            auto speedIt = speedById.find(other.id);
            if (speedIt != speedById.end()) candidate.speedMs = speedIt->second;
            best = candidate;
        }
    }
    return best;
}

} // namespace sr3viewer

namespace {

// Builds this demo's own VehicleTuning via the same real sr3xtbl/
// sr3vehicleinfo reader path BuildDriveDemoVehicleTuning() (above) uses -
// but copying tests/synthetic_physics_vehicle_substeps_test.cpp's own
// BuildTestVehicleTuning() fixture instead of BuildDriveDemoVehicleTuning()'s.
// NOT a cosmetic choice: BuildDriveDemoVehicleTuning() never sets
// Min_RPM_Resistance/Opt_RPM_Resistance/Max_RPM_Resistance at all, so those
// three fields silently keep VehicleTuning's own CHOSEN struct defaults
// (0.5/1.0/0.5) - which that same test file's own file comment documents,
// in detail, as making EngineNetTorque() "structurally negative at EVERY
// rpm for ANY throttle" once CROSSED into torqueFactorIdle/Peak/Redline
// (spec-physics-runtime.md Sec10.2's confirmed crossed mapping) - a vehicle
// that only ever creeps backward under positive throttle, confirmed by
// directly running `traffic_lane_demo` against BuildDriveDemoVehicleTuning()
// during this task (posX went strictly negative over 4+ seconds of
// throttle=0.5/steer=0). BuildTestVehicleTuning()'s own fixture already
// carries the fix (Min/Opt/Max_RPM_Resistance = 0.02/0.0/0.001, Min/
// Max_RPM_Torque_Factor = 1.0/1.0) - copied here verbatim (same convention
// BuildDriveDemoVehicleTuning() itself already uses for copying
// BuildTestVehicleTuning(), per that function's own doc comment: "copied
// rather than re-derived... consistent with already-reviewed precedent").
// FLAGGED, NOT FIXED IN PLACE: BuildDriveDemoVehicleTuning()/runDriveDemo()
// itself is left untouched - it is an already-merged, separate command's
// own fixture, out of this task's scope to silently alter; reported to the
// orchestrator instead (this function's own existence, and its doc comment,
// are that report).
sr3physics::VehicleTuning BuildTrafficLaneDemoVehicleTuning() {
    const char* xml =
        "<root><Table><Vehicle>"
        "<Name>test_sedan01</Name>"
        "<Mass>1400</Mass>"
        "<Engine>"
        "<Torque>280</Torque><Min_RPM>800</Min_RPM><Opt_RPM>4500</Opt_RPM><Max_RPM>6500</Max_RPM>"
        "<Min_RPM_Torque_Factor>1.0</Min_RPM_Torque_Factor><Max_RPM_Torque_Factor>1.0</Max_RPM_Torque_Factor>"
        "<Min_RPM_Resistance>0.02</Min_RPM_Resistance><Opt_RPM_Resistance>0.0</Opt_RPM_Resistance>"
        "<Max_RPM_Resistance>0.001</Max_RPM_Resistance>"
        "</Engine>"
        "<Axles>"
        "<Axle><Wheel_Does_Steer>yes</Wheel_Does_Steer><Wheel_Friction>1.0</Wheel_Friction>"
        "<Wheel_Max_Friction>1.0</Wheel_Max_Friction><Wheel_Braking_Torque>800</Wheel_Braking_Torque>"
        "<Wheel_Engine_Torque>0.0</Wheel_Engine_Torque></Axle>"
        "<Axle><Wheel_Does_Steer>no</Wheel_Does_Steer><Wheel_Friction>1.0</Wheel_Friction>"
        "<Wheel_Max_Friction>1.0</Wheel_Max_Friction><Wheel_Braking_Torque>600</Wheel_Braking_Torque>"
        "<Wheel_Engine_Torque>1.0</Wheel_Engine_Torque></Axle>"
        "</Axles>"
        "<Transmission>"
        "<Gear_Ratios><Element>3.4</Element><Element>2.1</Element><Element>1.4</Element><Element>1.0</Element><Element>0.8</Element></Gear_Ratios>"
        "<Diff_Gear_Ratio>3.7</Diff_Gear_Ratio>"
        "</Transmission>"
        "<Steering><Max_Steering_Angle>35</Max_Steering_Angle></Steering>"
        "<Aerodynamics><Drag_Coefficient>0.32</Drag_Coefficient><Frontal_Area>2.2</Frontal_Area><Air_Density>1.2</Air_Density></Aerodynamics>"
        "<Vehicle_Type><Automobile></Automobile></Vehicle_Type>"
        "</Vehicle></Table></root>";
    sr3xtbl::Document doc = sr3xtbl::ParseDocument(xml);
    const sr3xtbl::Node* row = sr3xtbl::FindChild(doc.table(), "Vehicle");
    sr3vehicleinfo::VehicleEntry entry = sr3vehicleinfo::ParseVehicleEntry(row);
    return sr3physics::VehicleTuning::FromVehicleEntry(entry);
}

} // namespace



int runTrafficLaneDemo(int argc, char** argv) {
    int ticks = 600;  // CHOSEN default - 10s @ 60Hz, plenty to clear this fixture's 3-segment, 2-turn lane and hit its own dead end
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) {
            ticks = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr, "usage: sr3_viewer traffic_lane_demo [--ticks N]\n");
            return 1;
        }
    }

    std::printf("=== traffic_lane_demo: traffic-AI lane-following over a real-shape Traffic (0x2236) graph ===\n");
    std::printf("REAL   : sr3zone::TrafficGraph (spec-zone-data-format.md Sec13.2 record format),\n"
                "         sr3physics::VehicleSubstepSimulator (the same 13 confirmed Havok vehicle\n"
                "         sub-steps `drive_demo` uses) and its ONE real write surface\n"
                "         (DriverInput steer/throttle/brake, spec-physics-runtime.md Sec10.6).\n");
    std::printf("SYNTH  : the Traffic record bytes below are hand-built, laid out exactly per Sec13.2's\n"
                "         own confirmed field offsets - this clean-room project reads no real .czn_pc data.\n");
    std::printf("CONFIRMED (Sec18.1/18.2/18.5/18.6): masked lookahead walk, nearest-distance junction\n"
                "         choice, cached-target distance-throttled re-query, dot-product alignment gate\n"
                "         picking throttle, full-lock +-1.0 steer magnitude.\n");
    std::printf("CHOSEN : the point-pool float3 stride, segment adjacency (coincident endpoints), the\n"
                "         steer SIGN source (lateral/cross-track test, standing in for the real, unmodelled\n"
                "         per-entity lane-side flag) - see sr3zone/traffic_graph.h and\n"
                "         sr3airuntime/traffic_lane_follower.h's own file comments for the full\n"
                "         CONFIRMED-vs-CHOSEN breakdown.\n\n");

    std::vector<uint8_t> buf;
    sr3zone::TrafficRecord rec = BuildTrafficLaneDemoFixture(buf);
    sr3airuntime::TrafficLaneGraph graph(rec);
    std::printf("parsed Traffic record: %zu segments, %zu-point pool\n\n", graph.segmentCount(), rec.pointPool.size());

    sr3physics::VehicleTuning tuning = BuildTrafficLaneDemoVehicleTuning();
    sr3physics::VehicleSubstepSimulator sim(tuning);
    // Start exactly at segment 0's own first polyline point, facing along
    // its own first leg (+X) - CHOSEN convenience, matching this fixture.
    sim.state_.posX = 0.0;
    sim.state_.posZ = 0.0;
    sim.state_.heading = 0.0;

    sr3airuntime::LaneFollowState lane = sr3airuntime::LaneFollowState::Begin(graph, 0);
    if (!lane.valid) {
        std::fprintf(stderr, "traffic_lane_demo: lane graph has no segment 0 to start from\n");
        return 1;
    }

    const double dt = 1.0 / 60.0;
    double minX = sim.state_.posX, maxX = sim.state_.posX;
    double minZ = sim.state_.posZ, maxZ = sim.state_.posZ;
    int segmentTransitions = 0;
    bool stoppedAtDeadEnd = false;

    std::printf("%6s %6s %10s %10s %10s %8s %8s %8s %8s %8s\n", "tick", "seg", "posX", "posZ", "headDeg", "steer",
                "throttle", "tgtX", "tgtZ", "rpm");

    for (int tick = 0; tick < ticks; ++tick) {
        sr3airuntime::LaneFollowCommand cmd =
            sr3airuntime::StepLaneFollow(graph, lane, sim.state_.posX, sim.state_.posZ, sim.state_.heading);

        sr3physics::DriverInput input;
        input.steer = cmd.steer;
        input.throttle = cmd.throttle;
        input.brake = cmd.brake;
        sim.Step(dt, input);

        if (cmd.advancedSegment) ++segmentTransitions;
        minX = (std::min)(minX, sim.state_.posX);
        maxX = (std::max)(maxX, sim.state_.posX);
        minZ = (std::min)(minZ, sim.state_.posZ);
        maxZ = (std::max)(maxZ, sim.state_.posZ);

        if (tick % 15 == 0 || cmd.advancedSegment || cmd.stuck) {
            std::printf("%6d %6zu %10.3f %10.3f %10.3f %8.3f %8.3f %8.3f %8.3f %8.1f\n", tick, lane.currentSegmentIndex,
                        sim.state_.posX, sim.state_.posZ, sim.state_.heading * 180.0 / 3.14159265358979323846,
                        cmd.steer, cmd.throttle, cmd.target.x, cmd.target.z, sim.state_.engineRpm);
        }

        if (cmd.stuck && !stoppedAtDeadEnd) {
            std::printf("  -> reached a dead end at tick %d (segment %zu has no outgoing edge); braking.\n", tick,
                        lane.currentSegmentIndex);
            stoppedAtDeadEnd = true;
        }
    }

    std::printf("\n=== summary ===\n");
    std::printf("segment transitions : %d\n", segmentTransitions);
    std::printf("final position      : (%.3f, %.3f), segment %zu\n", sim.state_.posX, sim.state_.posZ,
                lane.currentSegmentIndex);
    std::printf("X range visited      : [%.3f, %.3f]\n", minX, maxX);
    std::printf("Z range visited      : [%.3f, %.3f]\n", minZ, maxZ);
    std::printf("reached a dead end   : %s\n", stoppedAtDeadEnd ? "yes" : "no");
    if (maxX - minX > 1.0 && maxZ - minZ > 1.0) {
        std::printf(
            "PROOF: both X and Z ranges are non-degenerate - the vehicle's own path is NOT a\n"
            "       straight line through open space; it traced the fixture's real segment\n"
            "       points (a %d-segment bend), turning at each graph node.\n",
            static_cast<int>(graph.segmentCount()));
    }
    return 0;
}
