#pragma once

// sr3airuntime/police_roadblock_spawn.h - the real per-frame police/
// roadblock spawn MECHANISM: spec-ai-runtime.md Sec19.7 ("The literal
// police/roadblock spawn instruction - found, on a track separate from
// 0x00606b80", 2026-10-08), read alongside Sec19.4-19.6 for the surrounding
// notoriety/police-response investigation this section partly resolves.
//
// THIS IS A THIRD, SIBLING POPULATION SYSTEM, NOT A VARIANT OF
// ambient_population.h. Sec19.7's own confirmed chain:
//
//   0x00702a50 (main per-frame world update, spec-player-runtime.md Sec5.1)
//     -> 0x0090faf0 (per-frame dispatcher, Sec17.5 - the SAME dispatcher
//                    ambient_population.h's driver also hangs off)
//          -> 0x00930e20  (per-frame "roadblock" population tick - gated on
//                          debug cvar 0x026171b0, forced ON by default at
//                          bootstrap by 0x0092ed10)
//               -> 0x00930db0  (per-instance eligibility + despawn-timer test)
//                    -> 0x00930a40  (THE SPAWN-ATTEMPT WALK: re-centers an
//                                    anchor near a tracked reference
//                                    position, walks a slot table, enforces
//                                    a per-category per-TICK spawn-rate
//                                    throttle)
//                         -> 0x0092ff20  (POSITION-RESOLVE + 5-way TYPE DISPATCH)
//                              -> case 2 -> 0x0092f160 -> 0x009d73f0 -> 0x0096eb30   [ped creation]
//                              -> case 3 -> 0x0092f9d0 -> 0x00456e30(0x1e), vtable+0x84 [vehicle creation,
//                                                                 incl. literal "Spike_strip" stricmp]
//                              -> case 4 -> 0x0092fc20 -> 0x00456e30(0x2f)           [3rd category, OPEN identity]
//
// `0x00930e20` is called directly from inside `0x0090faf0`, structurally
// PARALLEL to (not downstream of) `0x00b93670`/`0x00b90960` (the ambient
// ped/vehicle ticks ambient_population.h models) and ALSO parallel to (not
// downstream of) `0x00606b80`'s own notoriety-level-change dispatcher chain
// (Sec19.4) - Handler 3 of that dispatcher (`0x00606e39`) was opened and
// confirmed to be a third instance of the generic "resolve handle, check
// class/liveness, release it" shape, NOT a spawn call. Sec19.4's own closing
// verdict: "the literal instruction that instantiates a police vehicle or
// roadblock at a resolved world position was NOT found in 0x00606b80's own
// chain - it lives on a separate, sibling per-frame tick instead" - this
// file is that sibling tick.
//
// CONFIRMED vs CHOSEN vs OPEN, same confidence key spec-ai-runtime.md's own
// top-of-file "Confidence key" paragraph defines, and the same discipline
// ambient_population.h/traffic_lane_follower.h already apply:
//
// CONFIRMED, ported directly (Sec19.7, full disassembly per that section):
//   - the per-category per-TICK spawn-rate throttle - NOT a total-live-count
//     cap, 3 counters zeroed fresh every tick: categories 0/1 share ONE
//     counter (0x026171a4, at most 1/tick combined), category 2 has its own
//     (0x026171a8, at most 2/tick), category 3 has its own (0x026171ac, at
//     most 4/tick), category 4 has NO counter (always allowed) -
//     RoadblockThrottleState below;
//   - the slot-table walk shape: per UNFILLED slot (this+0xd8, stride 0x18,
//     count this+0xe0), enforce the throttle, dispatch on success; "when no
//     unfilled slot exists, instead re-centers [the anchor]" - modeled
//     exactly as an EITHER/OR per tick in RunSpawnAttemptWalk() below, not
//     "recenter every tick regardless";
//   - the anchor re-center formula: anchor_xyz -= (referencePos_now -
//     referencePos_last) - the exact subtraction Sec19.7 states (this+0x40/
//     +0x44/+0x48) - RoadblockAnchorTracker::Recenter() below. The
//     REFERENCE being the owning player's own position is Sec19.7's own HIGH
//     CONFIDENCE (not independently proven) reading, not CONFIRMED.
//   - the position formula: spawn_xyz = anchor_xyz + direction_xyz *
//     slot->forward_offset, direction "sign-corrected via a dot-product
//     test, normalized" - ResolveSpawnPosition()/SignCorrectDirection()
//     below;
//   - the 5-way type dispatch structure: case 0/1 is a co-op "player
//     standing in this slot" hand-off (writes the resolved object's own
//     +0xc68, the SAME per-player state-dispatch byte 0x00606b80 itself
//     reads, Sec19.4) - NOT a creation call; case 2 is real character/ped
//     creation; case 3 is real vehicle creation, including an explicit
//     __stricmp match against the literal "Spike_strip"; case 4 is a third,
//     real creation category whose real-world identity is OPEN;
//   - the debug cvar's bootstrap default: 0x0092ed10 unconditionally forces
//     byte 0x026171b0 ("roadblock") to 1 before registering it -
//     RoadblockPopulationDriver::roadblockCvarEnabled defaults true.
//
// EXPLICITLY SKIPPED, NOT A GAP (Sec19.7, task brief): case 0/1 (co-op
// "player standing in this slot" hand-off). This project has no
// live-coop-player system - there is nothing for a hand-off to hand off TO.
// RoadblockCategory::kCoopPlayerSlotA/B exist only so the throttle's shared
// 0/1 counter and the slot table's own category tagging can be modeled
// faithfully; DispatchRoadblockSlot() returns kSkippedCoopHandoff for them
// and creates nothing.
//
// CHOSEN - the exact point of each call is documented in place, collected
// here for visibility:
//   1. `slot->forward_offset`'s real authored metres value (Sec19.7: "not
//      resolved this pass... OPEN; the mechanism itself is CONFIRMED").
//      CHOSEN: kChosenDefaultForwardOffsetMetres = 50.0 (mid-range of the
//      task brief's own suggested 30-80m "ahead of player" band for this
//      kind of spawn) - NOT a traced value, a per-slot default a caller may
//      override.
//   2. The road-network direction resolver itself. This project has no real
//      Traffic (0x2236) `.czn_pc` consumer wired up for THIS module (the
//      sibling traffic_lane_follower.h's TrafficLaneGraph exists but needs a
//      real/synthetic sr3zone::TrafficRecord, and ambient_population.h's own
//      "SYNTHETIC-ONLY ZONE BRIDGE" note explains why that dependency isn't
//      pulled in casually) - per the task brief's own explicit fallback,
//      this file's CHOSEN default is "along the reference entity's current
//      heading," a plain straight line (ResolveDirectionAlongHeading()
//      below). The SIGN-CORRECTION step itself (SignCorrectDirection) IS the
//      CONFIRMED mechanism Sec19.7 names; only the CANDIDATE direction fed
//      into it is this file's own stand-in. RunSpawnAttemptWalk() takes the
//      direction resolver as an injected callable precisely so a future unit
//      that traces/builds a real road-network direction source can replace
//      just this one piece without touching the confirmed throttle/anchor/
//      dispatch code.
//   3. The spike-strip vehicle definition name. Sec19.7's case 3 does an
//      exact `__stricmp` against the literal "Spike_strip" against SOME
//      configured name (the slot's own real data source for that name is
//      not detailed by Sec19.7 beyond the comparison rule itself) - this
//      file models it as RoadblockSlot::vehicleDefinitionName, a per-slot
//      field a (future, OPEN per Sec19.5) slot-table WRITER would set.
//      Checked this project's own vehicle/prop tables (sr3vehicleinfo::
//      VehicleEntry::name, sr3tables_vehicle_world) before writing this file
//      - an exhaustive grep across every table header in this repo found
//      ZERO "Spike_strip"/"spike_strip" rows anywhere outside this very spec
//      citation. FindSpikeStripDefinition() below therefore does the REAL
//      comparison rule (case-insensitive, matching __stricmp) against
//      whatever definition names a caller already has loaded, but there is
//      currently no real shipped row to match - kChosenSpikeStripName /
//      kChosenGenericVehicleName are CHOSEN placeholders, not traced rows.
//   4. Case 4's real-world identity (Sec19.7: "real-world identity
//      unresolved - OPEN"). CHOSEN guess, per the task brief's own suggested
//      example: a static barrier/cone-style prop, NOT a vehicle -
//      kChosenThirdCategoryPropName below. Explicitly a guess, not a trace.
//   5. "Per-instance eligibility + despawn-timer test" (0x00930db0) -
//      Sec19.7 names this function's existence and its two stated jobs
//      without detailing either test's own exact condition. CHOSEN reading
//      (RoadblockSpawnManager::RunEligibilityAndDespawnPass): each OCCUPIED
//      slot carries a countdown; on expiry the slot frees itself back to
//      "unfilled" so the spawn-attempt walk can refill it next tick. No
//      "eligibility" condition beyond the timer is modeled - Sec19.7 gives
//      no further detail to model against, and inventing one (e.g. a
//      distance-based despawn) would not be a traced fact.
//   6. kChosenOccupantLifetimeSeconds (the despawn countdown's own
//      duration) - not spec-stated, CHOSEN for this module's own demo/test
//      determinism.
// UPDATED 2026-10-18: the viewer now ALSO arms roadblocks from the police notoriety level via
// roadblock_notoriety_schedule.h (Sec20.6.10); the note below describes THIS driver's own `armed` flag only.
//
// THE HONEST NOTORIETY GATE - READ BEFORE WIRING THIS MODULE UP TO ANYTHING.
// Sec19.5/Sec19.6/Sec19.7 are ALL explicit and consistent: no function in
// this entire chain reads the live notoriety record (0x014A4F70) or either
// notoriety XTBL's own destination grid - "every one treats the manager
// instance's own slot table as pre-populated data, not something these
// functions decide to fill based on live notoriety." Sec19.7's own closing
// paragraph: "What remains genuinely OPEN: the upstream notoriety->schedule
// link." This file does NOT invent that link. RoadblockPopulationDriver::
// armed is a plain, explicitly-labeled bool a caller sets - NOT wired to
// sr3luahost's notoriety record (hud_state.h/engine_state.h) anywhere in
// this file. Two reasons, stated plainly: (a) tying it to a notoriety
// threshold would be inventing the exact fact Team A spent this pass
// confirming does NOT exist in the traced code; (b) sr3airuntime's other
// units (ambient_population.h, traffic_lane_follower.h, navpoint_selector.h,
// cover_node_selector.h) are all deliberately standalone/decoupled from
// sr3luahost - adding that dependency here just to auto-arm this module
// would be a new, one-off coupling for a guess, not a confirmed need. A
// future pass that DOES find the real upstream link should wire it at the
// call site that owns both systems, not inside this module.
//
// NOT MODELED HERE (OPEN per Sec19.6/Sec19.7, not invented): who WRITES a
// roadblock-class instance's own slot table in the first place (Sec19.5's
// narrowed remaining gap); the real cover-arc-evaluator reuse and
// 0x009d73f0/0x00bb9070 staging primitives inside case 2's own creation path
// (modeled here only as the OUTPUT request - a position and a category tag,
// not those two shared engine primitives); the real vtable-slot+0x84/type-ID
// factory call inside case 3/4 (same "model the output, not the engine
// primitive" split); 0x0092c280's own interior (the real road-network
// direction resolver, see CHOSEN item 2 above).

#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "sr3airuntime/common.h"

namespace sr3airuntime {

// ===========================================================================
// Sec19.7: the 5 type-dispatch categories. Values match the real case
// numbers (0/1/2/3/4) for direct correspondence with the spec text.
// ===========================================================================
enum class RoadblockCategory : int32_t {
    kCoopPlayerSlotA = 0,  // case 0 - co-op hand-off, SKIPPED (see file header)
    kCoopPlayerSlotB = 1,  // case 1 - co-op hand-off, SKIPPED
    kPed = 2,              // case 2 - CONFIRMED real character/ped creation
    kVehicle = 3,          // case 3 - CONFIRMED real vehicle creation (incl. "Spike_strip" stricmp)
    kThirdCategory = 4,    // case 4 - CONFIRMED a distinct real creation category; real-world identity OPEN (CHOSEN guess below)
};

// ===========================================================================
// Sec19.7 CONFIRMED: the per-category per-TICK spawn-rate throttle. NOT a
// total-live-count cap - all 3 counters are zeroed fresh every tick by the
// real 0x00930e20.
// ===========================================================================
struct RoadblockThrottleState {
    static constexpr int32_t kCategory01PerTickMax = 1;  // 0x026171a4, shared by categories 0 AND 1
    static constexpr int32_t kCategory2PerTickMax = 2;   // 0x026171a8
    static constexpr int32_t kCategory3PerTickMax = 4;   // 0x026171ac
    // category 4 has NO counter - always allowed (CONFIRMED).

    int32_t category01Count = 0;
    int32_t category2Count = 0;
    int32_t category3Count = 0;

    // CONFIRMED (0x00930e20's own job before calling the rest of the
    // chain): reset every tick.
    void ResetForTick() {
        category01Count = 0;
        category2Count = 0;
        category3Count = 0;
    }

    // Returns true (and consumes one unit of throttle) if this category is
    // still under its own per-tick cap; false if the throttle already holds
    // for this category this tick.
    bool TryConsume(RoadblockCategory category) {
        switch (category) {
            case RoadblockCategory::kCoopPlayerSlotA:
            case RoadblockCategory::kCoopPlayerSlotB:
                if (category01Count >= kCategory01PerTickMax) return false;
                ++category01Count;
                return true;
            case RoadblockCategory::kPed:
                if (category2Count >= kCategory2PerTickMax) return false;
                ++category2Count;
                return true;
            case RoadblockCategory::kVehicle:
                if (category3Count >= kCategory3PerTickMax) return false;
                ++category3Count;
                return true;
            case RoadblockCategory::kThirdCategory:
            default:
                return true;  // CONFIRMED: no counter, always allowed
        }
    }
};

// ===========================================================================
// Sec19.7 CONFIRMED: the sliding anchor. Re-centers every tick the
// spawn-attempt walk finds NO unfilled slot, by subtracting the delta the
// tracked reference position (HIGH CONFIDENCE: the player, not independently
// proven) moved since the last tick.
// ===========================================================================
struct RoadblockAnchorTracker {
    Vec3 anchorPos;                 // this+0x40/+0x44/+0x48
    Vec3 lastReferencePos;
    bool initialized = false;

    // First call seeds both the anchor and the reference-tracking point from
    // the same world position - this file's own reasonable starting
    // condition (Sec19.7 does not state the real initial anchor value).
    void InitializeIfNeeded(const Vec3& referencePos) {
        if (initialized) return;
        anchorPos = referencePos;
        lastReferencePos = referencePos;
        initialized = true;
    }

    // CONFIRMED formula (Sec19.7): anchor_xyz -= (referencePos_now -
    // referencePos_last).
    void Recenter(const Vec3& referencePos) {
        InitializeIfNeeded(referencePos);
        const Vec3 delta = referencePos - lastReferencePos;
        anchorPos = anchorPos - delta;
        lastReferencePos = referencePos;
    }
};

// ===========================================================================
// Sec19.7 CONFIRMED position formula, plus the CONFIRMED-shape (CHOSEN
// candidate-source, see file header item 2) direction resolution.
// ===========================================================================
inline Vec3 NormalizeOrZero(const Vec3& v) {
    const double len = Length(v);
    if (len <= 1e-9) return Vec3{0, 0, 0};
    return v * (1.0 / len);
}

// CONFIRMED mechanism (Sec19.7: "sign-corrected via a dot-product test,
// normalized"): flips `candidateDir` so it points the same general way as
// `referenceForward`, then normalizes. Used regardless of which concrete
// direction SOURCE feeds `candidateDir` (this file's own CHOSEN heading
// stand-in below, or a future real road-network resolver).
inline Vec3 SignCorrectDirection(const Vec3& candidateDir, const Vec3& referenceForward) {
    const Vec3 norm = NormalizeOrZero(candidateDir);
    if (Dot(norm, referenceForward) < 0.0) return norm * -1.0;
    return norm;
}

// CHOSEN (file header item 2): "along the reference entity's current
// heading" - a straight line, used only because this project has no real
// road-network direction resolver wired up to this module yet.
inline Vec3 ResolveDirectionAlongHeading(const Vec3& referenceForward) {
    return SignCorrectDirection(referenceForward, referenceForward);  // trivially same-signed; kept for API symmetry
}

// CONFIRMED (Sec19.7): spawn_xyz = anchor_xyz + direction_xyz * slot->forward_offset.
inline Vec3 ResolveSpawnPosition(const Vec3& anchorPos, const Vec3& direction, double forwardOffsetMetres) {
    return anchorPos + direction * forwardOffsetMetres;
}

// CHOSEN (file header item 1): mid-range of the task brief's own 30-80m
// "ahead of player" band for this kind of spawn - NOT a traced metres value.
inline constexpr double kChosenDefaultForwardOffsetMetres = 50.0;

// ===========================================================================
// Sec19.7 case 3: the literal "Spike_strip" comparison rule (CONFIRMED -
// case-insensitive, matching the real __stricmp), applied to whatever
// definition names a caller already has loaded. See file header item 3 for
// why no real shipped row currently matches in this project.
// ===========================================================================
inline bool CaseInsensitiveEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

inline bool IsSpikeStripDefinitionName(const std::string& name) {
    return CaseInsensitiveEquals(name, "Spike_strip");  // CONFIRMED literal (Sec19.7)
}

// CHOSEN placeholders (file header item 3/4) - not traced rows.
inline constexpr const char* kChosenSpikeStripName = "Spike_strip";           // the literal itself, used when a caller has no better name
inline constexpr const char* kChosenGenericVehicleName = "police_roadblock_car";  // CHOSEN placeholder generic roadblock vehicle
inline constexpr const char* kChosenThirdCategoryPropName = "roadblock_barrier";  // CHOSEN guess (case 4, OPEN identity)

// ===========================================================================
// Sec19.7: one pre-populated slot table entry (this+0xd8, stride 0x18,
// count this+0xe0). `vehicleDefinitionName` only matters when
// category == kVehicle (see file header item 3 for why this field exists on
// the slot rather than being looked up elsewhere).
// ===========================================================================
struct RoadblockSlot {
    bool filled = false;
    RoadblockCategory category = RoadblockCategory::kPed;
    double forwardOffsetMetres = kChosenDefaultForwardOffsetMetres;  // slot->forward_offset - CONFIRMED field/role, CHOSEN default value
    std::string vehicleDefinitionName;  // only read for category == kVehicle

    // CHOSEN despawn-timer bookkeeping (file header item 5/6) - not spec-stated.
    double despawnSecondsRemaining = 0.0;
};

// CHOSEN (file header item 6) - not spec-stated.
inline constexpr double kChosenOccupantLifetimeSeconds = 45.0;

// ===========================================================================
// Sec19.7 case 0/1/2/3/4 creation-request outputs. This module models only
// the OUTPUT of each real creation path (position + category-specific tag),
// not the shared engine primitives that real path stages through - see file
// header's "NOT MODELED HERE" note.
// ===========================================================================
struct PedCreationRequest {
    Vec3 position;
};

enum class RoadblockVehicleKind {
    kSpikeStrip,  // CONFIRMED (Sec19.7 case 3): the literal "Spike_strip" match held
    kGeneric,     // no match - CHOSEN generic roadblock vehicle (file header item 3)
};

struct VehicleCreationRequest {
    Vec3 position;
    RoadblockVehicleKind kind = RoadblockVehicleKind::kGeneric;
    std::string vehicleDefinitionName;
};

struct ThirdCategoryCreationRequest {
    Vec3 position;
    std::string propDefinitionName;  // CHOSEN guess (file header item 4) - OPEN real identity
};

enum class RoadblockDispatchOutcome {
    kSkippedCoopHandoff,   // case 0/1 - NOT a creation call (see file header)
    kPedCreated,           // case 2
    kVehicleCreated,       // case 3
    kThirdCategoryCreated  // case 4
};

struct RoadblockDispatchResult {
    RoadblockDispatchOutcome outcome = RoadblockDispatchOutcome::kSkippedCoopHandoff;
    std::optional<PedCreationRequest> ped;
    std::optional<VehicleCreationRequest> vehicle;
    std::optional<ThirdCategoryCreationRequest> thirdCategory;
};

// Sec19.7 CONFIRMED 5-way structure (`0x0092ff20`). `slot` supplies the
// per-slot configured data (category, and for category==kVehicle, the name
// to run the Spike_strip comparison against).
inline RoadblockDispatchResult DispatchRoadblockSlot(const RoadblockSlot& slot, const Vec3& spawnPosition) {
    RoadblockDispatchResult result;
    switch (slot.category) {
        case RoadblockCategory::kCoopPlayerSlotA:
        case RoadblockCategory::kCoopPlayerSlotB:
            // CONFIRMED (Sec19.7): writes the resolved object's own +0xc68
            // state-dispatch byte - a "live co-op player is standing in
            // this slot" hand-off, NOT a creation call. This project has no
            // live-coop-player system (see file header) - nothing to hand
            // off to, so nothing is created here, deliberately. Documented
            // consequence (see RoadblockSpawnManager::RunSpawnAttemptWalk
            // below): because this outcome is never treated as "filled", a
            // kCoopPlayerSlotA/B slot stays unfilled and is re-attempted
            // EVERY tick, correctly still metered by the shared cat0/1
            // throttle counter each time - without a real co-op occupant
            // there is nothing else for "filled" to mean here.
            result.outcome = RoadblockDispatchOutcome::kSkippedCoopHandoff;
            break;
        case RoadblockCategory::kPed: {
            PedCreationRequest req;
            req.position = spawnPosition;
            result.ped = req;
            result.outcome = RoadblockDispatchOutcome::kPedCreated;
            break;
        }
        case RoadblockCategory::kVehicle: {
            VehicleCreationRequest req;
            req.position = spawnPosition;
            if (IsSpikeStripDefinitionName(slot.vehicleDefinitionName)) {
                req.kind = RoadblockVehicleKind::kSpikeStrip;
                req.vehicleDefinitionName = slot.vehicleDefinitionName;  // preserve the matched slot's own casing
            } else {
                req.kind = RoadblockVehicleKind::kGeneric;
                req.vehicleDefinitionName =
                    slot.vehicleDefinitionName.empty() ? kChosenGenericVehicleName : slot.vehicleDefinitionName;
            }
            result.vehicle = req;
            result.outcome = RoadblockDispatchOutcome::kVehicleCreated;
            break;
        }
        case RoadblockCategory::kThirdCategory: {
            ThirdCategoryCreationRequest req;
            req.position = spawnPosition;
            req.propDefinitionName = kChosenThirdCategoryPropName;  // CHOSEN guess, OPEN real identity
            result.thirdCategory = req;
            result.outcome = RoadblockDispatchOutcome::kThirdCategoryCreated;
            break;
        }
    }
    return result;
}

// ===========================================================================
// One tick's own result - diagnostic/test/demo output, not itself a spec
// quantity.
// ===========================================================================
struct RoadblockTickResult {
    int32_t attemptedSlotCount = 0;   // unfilled slots the walk considered this tick
    int32_t throttledSlotCount = 0;   // of those, how many were blocked by the per-category cap
    bool anchorRecentered = false;    // true iff NO unfilled slot existed this tick (CONFIRMED either/or, Sec19.7)
    Vec3 anchorPositionAfterTick;
    std::vector<RoadblockDispatchResult> dispatches;  // one per slot actually dispatched this tick
};

// ===========================================================================
// Sec19.7: the manager owning one roadblock instance's own pre-populated
// slot table, throttle state, and anchor tracker - the real per-instance
// 0x02cc9900-class object's relevant subset (this+0xd8/+0xe0 slot table,
// this+0x40/+0x44/+0x48 anchor fields). Deliberately NOT a global/static
// singleton (same "caller-owned state" discipline every other sr3airuntime
// unit uses).
// ===========================================================================
class RoadblockSpawnManager {
public:
    explicit RoadblockSpawnManager(std::vector<RoadblockSlot> slots) : slots_(std::move(slots)) {}

    const std::vector<RoadblockSlot>& slots() const { return slots_; }
    const Vec3& anchorPosition() const { return anchor_.anchorPos; }
    bool anchorInitialized() const { return anchor_.initialized; }

    // CONFIRMED (0x00930e20's own first job): reset the throttle every tick,
    // before anything else in the chain runs.
    void BeginTick() { throttle_.ResetForTick(); }

    // CHOSEN model of 0x00930db0 ("per-instance eligibility + despawn-timer
    // test") - see file header item 5. Ticks every OCCUPIED slot's own
    // despawn countdown down by dtSeconds; an expired countdown frees the
    // slot back to unfilled so the walk below can refill it.
    void RunEligibilityAndDespawnPass(double dtSeconds) {
        for (auto& slot : slots_) {
            if (!slot.filled) continue;
            slot.despawnSecondsRemaining -= dtSeconds;
            if (slot.despawnSecondsRemaining <= 0.0) {
                slot.filled = false;
                slot.despawnSecondsRemaining = 0.0;
            }
        }
    }

    // CONFIRMED shape (0x00930a40): walk every UNFILLED slot; per slot,
    // enforce the per-category per-tick throttle, dispatching on success.
    // CONFIRMED either/or (Sec19.7): "when no unfilled slot exists, instead
    // re-centers" the anchor - modeled exactly, not "recenter every tick
    // regardless of slot state."
    RoadblockTickResult RunSpawnAttemptWalk(const Vec3& referencePos,
                                             const std::function<Vec3(const Vec3& anchorPos)>& resolveDirection) {
        anchor_.InitializeIfNeeded(referencePos);

        RoadblockTickResult tick;
        bool anyUnfilled = false;
        for (auto& slot : slots_) {
            if (slot.filled) continue;
            anyUnfilled = true;
            ++tick.attemptedSlotCount;

            if (!throttle_.TryConsume(slot.category)) {
                ++tick.throttledSlotCount;
                continue;
            }

            const Vec3 direction = resolveDirection(anchor_.anchorPos);
            const Vec3 spawnPos = ResolveSpawnPosition(anchor_.anchorPos, direction, slot.forwardOffsetMetres);
            RoadblockDispatchResult dispatch = DispatchRoadblockSlot(slot, spawnPos);
            if (dispatch.outcome != RoadblockDispatchOutcome::kSkippedCoopHandoff) {
                slot.filled = true;
                slot.despawnSecondsRemaining = kChosenOccupantLifetimeSeconds;
            }
            tick.dispatches.push_back(std::move(dispatch));
        }

        if (!anyUnfilled) {
            anchor_.Recenter(referencePos);
            tick.anchorRecentered = true;
        }
        tick.anchorPositionAfterTick = anchor_.anchorPos;
        return tick;
    }

private:
    std::vector<RoadblockSlot> slots_;
    RoadblockThrottleState throttle_;
    RoadblockAnchorTracker anchor_;
};

// ===========================================================================
// Sec19.7 CONFIRMED driver shape: the per-frame gate (`0x00930e20`, debug
// cvar `0x026171b0`, forced ON at bootstrap) around the manager's own tick.
// See file header's "THE HONEST NOTORIETY GATE" note for why `armed` is a
// plain caller-set bool, not wired to this project's own notoriety record.
// ===========================================================================
class RoadblockPopulationDriver {
public:
    // CONFIRMED default (Sec19.7: 0x0092ed10 unconditionally forces the
    // "roadblock" debug cvar to 1 before registering it).
    bool roadblockCvarEnabled = true;

    // CHOSEN explicit arm hook (see file header) - NOT tied to notoriety.
    bool armed = false;

    bool ShouldRunTick() const { return roadblockCvarEnabled && armed; }

    // Runs one full tick of the confirmed chain (throttle reset ->
    // eligibility/despawn pass -> spawn-attempt walk) against `manager`.
    // Caller is expected to check ShouldRunTick() first (mirrors the real
    // 0x0090faf0 gating 0x00930e20's own call).
    RoadblockTickResult RunTick(RoadblockSpawnManager& manager, double dtSeconds, const Vec3& referencePos,
                                 const std::function<Vec3(const Vec3&)>& resolveDirection) {
        manager.BeginTick();
        manager.RunEligibilityAndDespawnPass(dtSeconds);
        return manager.RunSpawnAttemptWalk(referencePos, resolveDirection);
    }
};

}  // namespace sr3airuntime
