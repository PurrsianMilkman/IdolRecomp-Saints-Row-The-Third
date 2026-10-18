// EngineState slice: vehicles, helicopters, airplanes, roadblocks, parking, traffic.
//
// Split out of engine_state.h (refactor step 3a, 2026-10-17) as a zero-behaviour-change move: every
// type, field, initialiser and comment below moved verbatim (namespace-scope types re-indented one
// level). Types that were nested in EngineState are now namespace-scope types in sr3luahost;
// EngineStateVehiclesData re-exports each one under its old name, so `EngineState::Type` still
// resolves. EngineState inherits EngineStateVehiclesData publicly, so `state.member`,
// `EngineState::kConstant` and `state.accessor()` are unchanged for every caller. Members that
// were private in EngineState are `protected` here (the only access change). Method
// declarations stay in EngineState (engine_state.h); bodies are in src/*.cpp.
#pragma once

#include "sr3luahost/engine_state_common.h"

namespace sr3luahost {

// Sec28.2 `helicopter_fly_to_set_goal_direction`. UPDATE 2026-10-10
// (Team A spec sync, HANDOFF.md commit 849a88e): "heli AI record +0x2c"
// in this label's own name is now known to be a holdover from this
// project's prior (wrong) reading - the real field is the vehicle-TYPE
// definition record's own class enum at (vehicle+0xbf4)+0x2c, static
// per-type data with no runtime writer (CONFIRMED values 0 car/2
// airplane/3 heli/4 VTOL; 1/5/6 OPEN) - the SAME physical field
// VehicleState::flyingType above already models under its own,
// already-corrected doc comment. Label text kept verbatim (it is also
// this field's own OpenStateError diagnostic string, matched by
// existing tests). UPDATE 2026-10-10 (vehicle-class xtbl wiring task):
// EngineState::loadZoneObjectsSection() (src/lua_engine_state.cpp) now
// populates this map for real, for any zone-placed vehicle instance
// whose own `script_vehicle_type_name` property resolves - via
// installVehicleTypeClassTable()'s real xtbl-sourced class data - to
// class 3 (Helicopter) or 4 (Vtol), replacing the single hardcoded
// "m05_heli_pierce" name seed this project used to carry here (see
// getOrCreateVehicle()'s own history for the full removal note).
struct HelicopterFlyTo {
    OpenValueMap<bool> qualifies{"0x00ad31a0: heli AI record +0x2c in {3,4}", "spec-lua-api-behaviour.md Sec28.2"};
    OpenValueMap<bool> applyGate{"heli AI record (0x00a79470) +0x4b0 == 1", "spec-lua-api-behaviour.md Sec28.2"};
    struct Request {
        std::string targetName;
        bool useOrientationMode = false;
        bool applied = false;
    };
    // The real position/orientation arithmetic (Sec28.2's own body) is not reproduced - nothing in this
    // batch's scope reads a computed vector back, so only the real BRANCH taken is recorded, honestly, not
    // invented numbers (same "record the request" convention as MinimapIconRecord above).
    std::unordered_map<std::string, Request> lastRequest;

    // helicopter_go_to_set_target (Sec25.14) / helicopter_fly_to_stop
    // (Sec25.15), wave 2 batch W2-4 - EXTENDS this SAME struct rather
    // than a parallel one, per that batch's own cross-batch coordination
    // note (the helicopter-AI family spans wave 1 Batch 3's
    // helicopter_fly_to_do/helicopter_fly_to_set_goal_direction above
    // and this wave-2 batch). helicopter_go_to_set_target's own 4-tier
    // target-resolution fallback chain (generic/waypoint-table/character/
    // vehicle, each via a DIFFERENT real resolver) collapses to this
    // project's one flat objectResolves() map, the SAME simplification
    // this header already applies project-wide (Sec29's "one shared
    // global name map" finding) - which TIER matched is not
    // distinguishable and is not modelled, only that SOME target
    // resolved. helicopter_fly_to_stop reuses `qualifies` above (the
    // SAME heli-AI-record gate helicopter_fly_to_do/_set_goal_direction
    // already share, Sec12.11/Sec28.2) rather than inventing a second
    // gate - stopping just clears whichever of lastRequest/
    // lastGoToTarget is present for that helicopter, honestly, since
    // 0x00b37050/0x00b38280's own internals are OPEN.
    struct GoToTarget {
        std::string targetName;
        bool applied = false;
    };
    std::unordered_map<std::string, GoToTarget> lastGoToTarget;
    uint64_t goToSetTargetCount = 0;
    uint64_t flyToStopCount = 0;
};

// Sec32.7 `helicopter_shoot_vehicle`: the AI-drive-state gate per
// helicopter name, plus the shared fire dispatcher's own OPEN return
// value (Sec32.7: "its own return value is invisible in the decompiled
// view" - a SINGLE shared OpenValue, not per-call/per-name data, since
// this is a structural "we don't know this dispatcher's return contract
// on success" marker, not state belonging to any one helicopter).
struct Helicopter {
    OpenValue<bool> aiDriveStateOk{"helicopter AI-drive-state gate", "spec-lua-api-behaviour.md Sec32.7"};

    // helicopter_enter_retreat (Sec17.27), wave 2 batch W2-2
    // (2026-10-08): the SAME heli-specific sub-object (0x00a79470)
    // HelicopterFlyTo::applyGate above already cites, here testing a
    // DIFFERENT condition on its own +0x4b0 mode enum (!= 6, not == 1) -
    // kept as its own OPEN field rather than overloading applyGate's
    // own established "== 1" semantics. retreatTransitionCount only
    // increments when the mode enum is confirmed != 6 (CONFIRMED: the
    // whole clear/transition/notify/height-check/cleanup chain is
    // gated on that one test).
    OpenValue<int32_t> subObjectModeEnum{"heli sub-object (0x00a79470) mode enum +0x4b0",
                                         "spec-lua-api-behaviour.md Sec17.27"};
    uint64_t retreatTransitionCount = 0; // 0x00b350a0(vehicle, 5, 0) fired
    bool field158Cleared = false;
    // The height check (0x00b41670 vs the fixed constant 0x012a2d70) -
    // no real position/geometry data in scope (same stated project-wide
    // gap as vehicle_chase's own spotlight check) - OPEN until a test
    // sets it.
    OpenValue<bool> heightCheckTrips{"helicopter retreat height check (0x00b41670 vs 0x012a2d70)",
                                     "spec-lua-api-behaviour.md Sec17.27"};
    bool heightTripFlag268 = false;
    int64_t retreatDeadlineStamp = 0; // 0 = never scheduled; else EngineState::nextMonotonicStamp()+1500
    uint64_t retreatReplicateCount = 0;
};

// spawning_boats (Sec49.2, `0x00a20840` gameplay registrar,
// `gameplay`) - added to this batch after its own fresh mission-drive
// pass found a REAL measured hit (1 incremental call from a real
// mission script, not just a static call-site count - see this
// header's own top-of-batch comment and lua_spec_confirmed_stubs.cpp's
// own Sec49 batch header comment). 1 mandatory boolean. CONFIRMED:
// writes a host-authoritative, session-replicated byte (registered
// name `"os_boat"`) with NO host gate on the write itself - unlike
// tranche 13's sibling `audio_suppress_ambient_player_lines` (Sec45,
// itself unimplemented anywhere in this project), which DOES gate.
struct SpawningBoats {
    bool value = false; // CHOSEN initial value - the "os_boat" replicated byte itself
    uint64_t writeCount = 0;
};

// get_char_vehicle_name (Sec12.9, 0x00a4b220): reuses
// CharacterState::currentVehicleName directly (+0x16c0/+0x16c4, the SAME
// field radio_block below also reads) - no new EngineState-level state
// needed. See stub_get_char_vehicle_name's own doc comment for the
// CHOSEN "trust a known association, skip the further OPEN liveness/
// class-bit gates" simplification.

// effect_play_on_script_object/effect_play_on_human (Sec12.10/Sec12.12,
// 0x00a491b0/0x00a48f80): CONFIRMED to share effect_play's (Sec9.1,
// already implemented) own fixed effect-name registry singleton, index/
// instance-allocation chain and finalizer, differing only in which
// resolved object's position fields the instance attaches to - reuses
// the SAME -1.0 failure sentinel (kActionPlaySentinelResolveFail) and
// nextVisualEffectHandle() success-handle stand-in effect_play already
// established, rather than inventing a second, parallel convention. No
// new EngineState-level state needed.

// helicopter_fly_to_do (Sec12.11, 0x00a4ff20): CONFIRMED to share the
// qualifying-vehicle gate (0x00ad31a0, "true only for internal state
// values 3/4") this project's EXISTING HelicopterFlyTo struct above
// (Sec28.2) already models as `qualifies` - reused directly rather than
// re-derived, per this batch's own cross-batch coordination note (the
// helicopter-AI family spans this batch and Batch 4). `lastRequest`
// (also pre-existing) is likewise reused to record the resolved
// destination name and the orientation-mode boolean (arg 8) this
// native's own CONFIRMED argument shape supplies. No new EngineState-
// level state needed - see HelicopterFlyTo's own doc comment above.

// on_vehicle_exit (Sec12.15, 0x00a58a40): CONFIRMED a real, three-tier
// fallback resolver chain (generic/#PLAYER#, then plain character, then
// plain vehicle) posting one of three different event codes
// (0x17/0x18/6) - this project's flat objectResolves() map (Sec29's
// "one shared global name map" finding) cannot distinguish WHICH tier
// matched, so which event code would have fired is OPEN; only that the
// native fired, for which (occupant, vehicle-or-seat) pair, is recorded
// - a stated simplification, not an invented event code.
struct OnVehicleExitEvent {
    std::string occupant;
    std::string vehicleOrSeat;
};

// =====================================================================
// Wave 2, Batch W2-7 (2026-10-08): spec-lua-api-behaviour.md Sec38-60,
// "gameplay A: vehicles/world/spawn/effects/audio/camera" (113 names).
// See src/lua_spec_confirmed_stubs_b26_wave2_vehicles_helicopter.cpp
// (vehicle/helicopter/airplane/traffic) and src/lua_spec_confirmed_
// stubs_b27_wave2_spawn_world_audio.cpp (spawn/world/effects/audio/
// camera/mission-specific) for the full per-native citations this
// section's own doc comments abbreviate.
// =====================================================================

// --- roadblock_create / roadblock_destroy (Sec39.2) ------------------
// CONFIRMED: "returns exactly 1 number: the new roadblock's 64-bit
// handle converted to a double, or -1 on any failure." "The template
// lookup keeps scanning after a match and returns the LAST one" - this
// project has no real template table, so templateName is stored
// verbatim (the "last match wins" behaviour is inherent: a second
// createRoadblock call with the same template name is simply a new,
// independent record - no lookup/collapsing happens here, matching
// observable behaviour for the common case of distinct template
// names). Placement success needs real road-network/geometry data this
// project does not have (8 candidate spots within 7.0 units, a
// per-template acceptance test) - placementResolves is test-settable
// (OPEN until set); a real call against an unregistered placement
// object name REFUSES (treated as placement failure -> -1), the
// honest minimal default rather than fabricating road-network state.
struct RoadblockRecord {
    std::string templateName;
    std::string placementObjectName;
};

// --- force_fire_do / force_fire_target_do / force_throw_do /
// force_throw_char_do (Sec55.4): reuse CharacterState's own EXISTING
// caeStagedActionIndex/caeStagedTargetName/caeCommittedActionIndex/
// caeCommittedTargetName fields (Sec30.4, 70-entry CAE table) directly
// - action 0 = fire (CONFIRMED literal), action 4 = throw grenade
// (CONFIRMED literal, "same pattern as ai_suggest_action"). No new
// EngineState/CharacterState fields needed beyond
// CharacterState::hasThrowableItemEquipped (added above, Sec55.4).

// --- airplane_takeoff_do / airplane_land_do (Sec59.7) /
// airplane_fly_to_do (Sec58) ---------------------------------------
// CONFIRMED: airplane_land_do "always pushes true once its two name
// arguments resolve, regardless of what the underlying landing
// routine itself reports" (a return-value trap, modelled directly by
// the stub, not by this state). airplane_fly_to_do's navpoint form
// "can never work" (CONFIRMED: always returns false) and "leaks a
// request id... silently cancels [a] previously-started path flight"
// - this project has no per-vehicle flight-request slot to actually
// cancel, so only the leak/supersede FACT is counted, honestly, not a
// simulated flight state.
struct AirplaneRecord {
    bool takeoffRequested = false;
    bool pathFlightInProgress = false; // set by the (out-of-scope) path-target form; this batch only clears/
                                        // supersedes it via the leaked-request path, per Sec58's own text
};

struct EngineStateVehiclesData {
    // Former EngineState nested types (defined above at namespace scope), re-exported so
    // `EngineState::Type` keeps naming them.
    using HelicopterFlyTo = sr3luahost::HelicopterFlyTo;
    using Helicopter = sr3luahost::Helicopter;
    using SpawningBoats = sr3luahost::SpawningBoats;
    using OnVehicleExitEvent = sr3luahost::OnVehicleExitEvent;
    using RoadblockRecord = sr3luahost::RoadblockRecord;
    using AirplaneRecord = sr3luahost::AirplaneRecord;

public:
    void noteRcDriverPollQuery(const std::string& character, const std::string& vehicle) { rcPollQueries_.push_back({character, vehicle}); }
    size_t vehicleTypeClassTableSize() const { return vehicleTypeClassByLowerName_.size(); }
    size_t vehicleSpecialTypeTableSize() const { return vehicleSpecialTypeByLowerName_.size(); }
    size_t vehicleCount() const { return vehicles_.size(); }
    std::unordered_map<std::string, VehicleState>& vehicles() { return vehicles_; }
    static constexpr int kScriptedRequestKindMoveOrPathfind = 2;  // Sec35.2

    // --- store_vehicle_get_state (Sec10.1) ----------------------------------

    // Real body: reads global flag `0x022cdf08`, pushes 0.0 if it reads 0,
    // else 1.0 (CONFIRMED, disassembly); HIGH CONFIDENCE that the flag's
    // real-world meaning is "is the vehicle-store UI currently in an
    // active (non-default) mode." CONFIRMED (Sec10.1/Sec26.28, 2026-10-01):
    // the flag is 0 at load (applySpecInitialState). Its three writers
    // (0x00815120 = store_vehicle_change_mode, 0x005fa820, 0x00820cd0) are
    // CONFIRMED bodies, but none is implemented here: the Lua argument shape
    // of store_vehicle_change_mode is not in the spec and the triggers of the
    // other two are OPEN, so the flag only changes from a test.
    OpenValue<bool>& vehicleStoreActive() { return vehicleStoreActive_; }
    // feature/route-table-driving: negative character_is_in_vehicle(#PLAYER1#[, V]) queries this frame (harness
    // bookkeeping only; V == "" for the any-vehicle form). Drained once per frame by --auto-objective.
    void noteVehicleWaitQuery(const std::string& vehicleName) { vehicleWaitQueries_.push_back(vehicleName); }
    std::vector<std::string> drainVehicleWaitQueries() { std::vector<std::string> out; out.swap(vehicleWaitQueries_); return out; }

    // Sec28.1 `helicopter_set_dont_death_spiral`: the vehicle's own flag this batch writes (gated on
    // objectResolves()); CHOSEN plain map, write-only in scope (either real write path converges on this one
    // Lua-visible flag per this batch's own no-networking-layer convention).
    std::unordered_map<std::string, bool>& helicopterDontDeathSpiral() { return helicopterDontDeathSpiral_; }
    HelicopterFlyTo& helicopterFlyTo() { return helicopterFlyTo_; }

    // Sec28.8 `get_char_vehicle_is_in_air`.
    OpenValueMap<bool>& vehicleInAirByCharacter() { return vehicleInAirByCharacter_; }

    // Sec28.22 `character_evacuate_from_all_vehicles`: 0x00af3e10's own occupant-state dispatch (1/2/3) is OPEN
    // per spec; only the gated call count is modelled, keyed by character name.
    std::unordered_map<std::string, uint64_t>& evacuateFromVehiclesCount() { return evacuateFromVehiclesCount_; }

    // Sec30.5 `vehicle_exit_group_do`'s own CONFIRMED crash-shaped edge
    // (Sec30.7): the real engine calls 0x00a7bdc0(arg3) on the first
    // vehicle found among the resolved, state-3 characters in its table
    // argument - or, when none was found across the whole table, with a
    // NULL `this` ("confirmed from the listing, XOR ECX,ECX"; Sec30.7:
    // "OPEN whether 0x00a7bdc0 tolerates that"). HOST-SAFETY DEVIATION
    // (not a spec fact, per this task's own explicit instruction): the
    // real engine reaches that unconfirmed-safety null-`this` call here;
    // this host guards the found-vehicle name explicitly and never
    // attempts the equivalent call on an unresolved target, matching the
    // established "this project does not simulate crashes" precedent
    // (cf. group_get_next_npc, Sec28.5, item 57 in lua_spec_confirmed_
    // stubs.cpp). This counter is this project's OWN test-observable
    // proof the guard fired - NOT a real engine field - same convention
    // as VintDataResponderRecord::dispatchAttempts.
    int vehicleExitGroupNullThisGuardCount() const { return vehicleExitGroupNullThisGuardCount_; }
    void recordVehicleExitGroupNullThisGuard() { ++vehicleExitGroupNullThisGuardCount_; }
    Helicopter& helicopter(const std::string& name) { return helicopters_[name]; }
    OpenValue<bool>& helicopterFireDispatcherResult() { return helicopterFireDispatcherResult_; }
    SpawningBoats& spawningBoats() { return spawningBoats_; }

    // traffic_disable_lanes (Sec12.19, individually cleared by Sec60.3).
    // CONFIRMED: resolved via the shared trigger/object resolver, then
    // enables/disables a named trigger volume of traffic lanes (the exact
    // internal dispatch, 0x00a6dd70, is OPEN beyond "shared with
    // traffic_disable_drawbridge_area, Sec6.4" - not modelled further).
    void setTrafficLanesEnabled(const std::string& name, bool enabled) { trafficLanesEnabled_[name] = enabled; }
    bool trafficLanesKnown(const std::string& name) const { return trafficLanesEnabled_.count(name) != 0; }
    float trafficDensity() const { return trafficDensity_; }
    void setTrafficDensity(float v) { trafficDensity_ = v; }
    bool vehicleSpawningEnabled() const { return vehicleSpawningEnabled_; }
    void setVehicleSpawningEnabled(bool enabled) { vehicleSpawningEnabled_ = enabled; }

    // on_vehicle_enter (Sec4.12): mirrors release_to_world's own
    // releaseToWorldKind() (Sec4.4) exactly, per this task's explicit
    // instruction to reuse that resolution shape - this project's flat
    // objectResolves() map alone cannot distinguish WHICH of the two
    // type-gated resolver-family members (bit 0x4@+0xb "character" vs bit
    // 0x8@+0xb "vehicle") matched for a name it reports true for. 1 =
    // bit-0x4 character tier (CharacterState::onVehicleEnterCharacterSlot
    // Callback, hook slot 0x17), 2 = bit-0x8 vehicle tier
    // (VehicleState::onVehicleEnterCallback, hook slot 7). OPEN until a
    // test sets it for a name objectResolves() also reports true for
    // (independent facts a test must configure together, same convention
    // as releaseToWorldKind()). Not reached at all when arg 2 resolves via
    // the generic/`#PLAYER#` chain first (playerClassChainResolves()).
    OpenValueMap<int32_t>& onVehicleEnterResolverKind() { return onVehicleEnterResolverKind_; }

    // --- vehicle_pathfind_navmesh_do (Sec22.1, wave 2 batch W2-1): the raw
    // result of the record-and-replicate dispatcher 0x00b24fa0, pushed to Lua
    // as a number. Its return semantics are not given (Sec22.1 describes
    // only its branch structure) - OPEN, one host-level value, same shape as
    // vehicle_pathfind_to_do's own pathfindResult() for its DIFFERENT
    // dispatcher 0x00b2b140 (kept separate: different address).
    OpenValue<double>& vehicleNavmeshDispatchResult() { return vehicleNavmeshDispatchResult_; }

    // --- vehicle_pathfind_to_do (Sec30.5) --------------------------------
    //
    // 0x00b2b140's own return value: OPEN per spec ("OPEN: 0x00b2b140's
    // own semantics/return meaning, what arg 3/4 select") - a single
    // shared stand-in (this project has no per-vehicle pathfinding model
    // to give a finer-grained value), test-settable; a real call with no
    // test override throws OpenStateError, the honest "never fabricate a
    // physics result" outcome.
    OpenValue<double>& pathfindResult() { return pathfindResult_; }
    // HOST-SAFETY (orchestrator's standing hardening policy, not a spec
    // fact): the real engine takes strlen() of arg 2 BEFORE testing its
    // Lua type (CONFIRMED crash-shaped edge, Sec30.5: "Takes strlen of arg
    // 2 before the NULL test - a non-string arg 2 is dereferenced"). This
    // host never dereferences a non-string Lua value as a C string - see
    // the stub's own doc comment for exactly which safe path it takes
    // instead. Test-observable only, not a real engine field.
    int vehiclePathfindNonStringArg2GuardCount() const { return vehiclePathfindNonStringArg2GuardCount_; }
    void recordVehiclePathfindNonStringArg2Guard() { ++vehiclePathfindNonStringArg2GuardCount_; }

    // --- vehicle_set_2d_tank_controls (Sec30.5): global byte 0x0278cad8 -
    bool& twoDTankControlsEnabled() { return twoDTankControlsEnabled_; }

    // --- vehicle_rc_get_signal_strength (Sec30.5): global float
    // 0x0278cad4, the RC signal range R used in `1 - d/R` - OPEN per spec
    // ("OPEN: R's own source (only writer 0x00a906b0)"). -----------------
    OpenValue<double>& rcSignalRange() { return rcSignalRange_; }
    bool& malfunctionGlobalByte02625852() { return malfunctionGlobalByte02625852_; }

    // roadblocks_enable (Sec14.28): a single global byte - CHOSEN true
    // ("enabled"), matching optionalBoolDefault's own default=true reading
    // for an absent/non-boolean argument (Sec14.28's own text: "applies
    // both when absent and whenever the presence/type test itself fails").
    bool& roadblocksEnabled() { return roadblocksEnabled_; }
    uint64_t& roadblocksDisableCleanupCount() { return roadblocksDisableCleanupCount_; }
    std::vector<OnVehicleExitEvent>& onVehicleExitEvents() { return onVehicleExitEvents_; }

    // drug_effect_clear_override_values (Sec21.25): reuses drugEffectOverrideValueA/B()
    // (Sec14.19's own channel-0 fields) directly - the sibling clear/set
    // pair for the identical real channel-0 slot. No new state needed
    // beyond those two already-existing fields.

    // team_make_allies (Sec20.6): reuses setTeamRelationIfInBounds()/
    // recordTeamRelationOobGuard() directly (that function's own doc
    // comment already anticipated this: "team_make_allies ... and
    // team_make_unfriendly share one correct implementation if
    // team_make_allies is ever added") - relation value 4, no new state.
    // Per spec-lua-api-behaviour.md Sec30.6 ("the two 'auxiliary arrays'
    // that note names are the SAME relation matrix at rows/column 6"),
    // team 5's own mirroring onto team 6 is NOT a separate, unmodeled
    // mechanism - it IS setTeamRelationIfInBounds()'s own existing team-5-
    // mirrors-team-6 refinement (established by team_make_unfriendly/
    // Sec30.6), already exercised by this native through the shared call.
    // (Post-merge correction: this comment previously claimed a distinct,
    // unmodeled "team 5 two-auxiliary-array" gap here - a fresh reviewer
    // found that claim false; the address arithmetic for row 6
    // (0x0262d940 + 6*8 = 0x0262d970) confirms it is the same mechanism.)

    // =========================================================================
    // Wave 2, Batch W2-4 (Sec22+Sec24+Sec25 gameplay residue, 78 names,
    // 2026-10-08) - the EngineState-level (not per-character/per-vehicle) new
    // state this batch needs. See src/lua_spec_confirmed_stubs_b22_wave2_sec22.
    // cpp and src/lua_spec_confirmed_stubs_b23_wave2_sec24_25.cpp's own per-
    // stub doc comments for the full citation/reasoning behind each field.
    // =========================================================================

    // spotlight_heli_laser_enable (Sec22.6): a single global, no resolver, no
    // gate, no record - CHOSEN default false (no CONFIRMED construction value).
    bool& spotlightHeliLaserEnabled() { return spotlightHeliLaserEnabled_; }

    // roadblocks_ambient_traffic_enable (Sec22.7): CONFIRMED only written
    // "when a live session exists AND this machine is host" - reuses
    // coopLocalIsHost() directly (CONFIRMED false in single player project-
    // wide), so this is a real, confirmed no-op in single player; the field
    // exists for structural/test coverage (a test can flip coopLocalIsHost()'s
    // own backing state to exercise the write branch).
    bool& roadblocksAmbientTrafficEnabled() { return roadblocksAmbientTrafficEnabled_; }
    uint64_t& roadblocksAmbientTrafficNoSessionSkipCount() { return roadblocksAmbientTrafficNoSessionSkipCount_; }
                                            // simply "does this handle exist in the registry")
    bool roadblockExists(int64_t handle) const { return roadblocks_.count(handle) != 0; }
    size_t roadblockCount() const { return roadblocks_.size(); }
    OpenValueMap<bool>& roadblockPlacementResolves() { return roadblockPlacementResolves_; }

    // --- rc_set_max_signal_range (Sec39.2) --------------------------------
    // CONFIRMED: "returns the PREVIOUS range... swaps the new value into a
    // global float." "The global's own start-up default was not traced
    // (OPEN)" - the FIRST real call (no prior .set()) honestly throws
    // OpenStateError via OpenValue::get() below, per this project's
    // standing no-invented-defaults policy.
    OpenValue<double>& rcMaxSignalRange() { return rcMaxSignalRange_; }
    // set_cops_shooting_from_vehicles: CONFIRMED "the engine's own reset
    // default is false... calling this setter with no argument at all sets
    // it true" - the FIELD default below is the engine's own CONFIRMED
    // reset value (false); the stub's own optional-bool-default-true
    // argument handling is what produces the documented mismatch.
    bool& copsShootingFromVehicles() { return copsShootingFromVehicles_; }

    // --- spotlight_heli_laser_size (Sec39.2) ------------------------------
    // CONFIRMED default 1.0; "no clamp, no replication, no host reset
    // found anywhere."
    double& spotlightHeliLaserSize() { return spotlightHeliLaserSize_; }

    // --- vehicle_forced_corpse_removal_enabled (Sec38.1) --------------------
    // GLOBAL byte, host+session-gated write. CONFIRMED: "reset to ON at
    // every world init" - CHOSEN true initial (matches that documented
    // reset value).
    bool& vehicleForcedCorpseRemovalEnabled() { return vehicleForcedCorpseRemovalEnabled_; }

    // --- vehicle_detonate_random_parked_car (Sec52.2) hardening counters --
    // See stub_vehicle_detonate_random_parked_car's own doc comment
    // (lua_spec_confirmed_stubs_b26_wave2_vehicles_helicopter.cpp) for the
    // full CHOSEN (hardened divergence) reasoning - two distinct named
    // counters, one per refusal shape, per this batch's hardening policy.
    uint64_t& vehicleDetonateRandomNoReferenceGuardCount() { return vehicleDetonateRandomNoReferenceGuardCount_; }
    uint64_t& vehicleDetonateRandomFailedReferenceGuardCount() { return vehicleDetonateRandomFailedReferenceGuardCount_; }
    // Third counter: reference name given AND resolves. Per spec Sec52.2,
    // the reference object is a location anchor for an angle/distance
    // window, not the detonation target itself - the default (all-zero)
    // window admits nothing, so this host correctly detonates NOTHING and
    // leaves the resolved reference object's own state untouched.
    uint64_t& vehicleDetonateRandomResolvedReferenceNoOpCount() { return vehicleDetonateRandomResolvedReferenceNoOpCount_; }

    // --- vtol_flyby_animation_for_m18 (Sec46.4) ---------------------------
    // "a one-off mission-18 helper (teleport + animation + an
    // unconditionally broadcast network record with no host or ownership
    // gate)" - VehicleState::vtolFlybyM18Played above tracks the per-
    // vehicle play flag; this counts the unconditional broadcast.
    uint64_t& vtolFlybyM18BroadcastCount() { return vtolFlybyM18BroadcastCount_; }

    // --- parking_spot_set_max / parking_spot_reset_max (Sec42.6) ---------
    // CONFIRMED: "values above 18 or negative are silently ignored (only
    // 0-18 accepted)... not clamped, simply dropped." Reset writes the
    // hard-coded default 18 - CHOSEN as the initial value too (reset's own
    // CONFIRMED value is the only concrete number spec gives for this
    // field at all).
    int32_t& parkingSpotMax() { return parkingSpotMax_; }
    std::unordered_map<std::string, AirplaneRecord>& airplanes() { return airplanes_; }
    uint64_t& airplaneFlyToLeakedRequestCount() { return airplaneFlyToLeakedRequestCount_; }
    // HARDENED (host-safety): the real engine measures the target-name
    // argument's strlen() BEFORE null-checking it (Sec58, "the identical
    // bug already documented in vehicle_pathfind_to_do") - this host
    // refuses (treats as empty/unresolved) instead of reading a non-string
    // Lua value as a C string, counted here.
    uint64_t& airplaneFlyToNullTargetGuardCount() { return airplaneFlyToNullTargetGuardCount_; }

    // --- players_in_weird_camera_mode (§40.4): CONFIRMED the FIRST of its
    // two contributing globals is permanently dead code in the shipped
    // binary (a 9-site exhaustive scan found zero writers) - modelled as a
    // literal `false` constant in the stub body, no field needed. The
    // SECOND global (an "RC Gun" flag) is written only by two functions
    // explicitly OUT of this batch's own 93-name list (OPEN, not wired
    // here) - this project tracks it anyway as a settable field so a
    // future batch implementing those two setters can wire it without
    // another engine_state.h edit, but within THIS batch it can only ever
    // read false (CHOSEN, honestly reflects "no in-scope writer").
    bool& rcGunActiveFlag() { return rcGunActiveFlag_; }
    std::unordered_map<std::string, std::string>& onHitPedVehicleCallbacks() { return onHitPedVehicleCallbacks_; }
    std::unordered_map<std::string, std::string>& onVehicleDebrisFlowRecycledCallbacks() { return onVehicleDebrisFlowRecycledCallbacks_; }

    // helicopters_set_jitter_override (Sec18.13): two adjacent globals
    // (min/max, HYPOTHESIS which is which), both defaulting to the SAME
    // unresolved fixed float constant (0x01117a4c) - CHOSEN placeholder
    // 0.0 (no in-scope reader depends on a specific default value).
    double& helicopterJitterMin() { return helicopterJitterMin_; }
    double& helicopterJitterMax() { return helicopterJitterMax_; }

    // parking_spot_disable_all (Sec18.29): CONFIRMED (raw-disassembly-
    // verified hidden-argument finding) that, despite its own name, this
    // native only ever touches ONE fixed slot (index 1), never "all" - the
    // Lua-facing boolean sense is "enable" (true -> clear slot 1) vs.
    // "disable" (false -> set slot 1), inverted from the name. CHOSEN true
    // ("enabled" - no stated construction default).
    bool& parkingSpotSlot1Enabled() { return parkingSpotSlot1Enabled_; }

    // difficulty_get_level (Sec17.30): CONFIRMED (0x00a47300) to be the
    // SAME real function get_current_difficulty (Sec41.2, above) already
    // models under its own alias name - reuses difficultyLevel()/
    // difficultyLevel_ directly, not a second, duplicate field with a
    // weaker (OPEN) default than the one already CONFIRMED there.

    // player_force_vehicle_seat (Sec17.6): the global "forced seat vehicle"
    // identity half - see CharacterState::forcedVehicleSeatIndex's own doc
    // comment for the full two-independent-globals reasoning. "" = the
    // fixed default struct (no vehicle name given).
    std::string& forcedSeatVehicleName() { return forcedSeatVehicleName_; }

    // player_is_rc_driver (Sec18.10): collapses the full resolve-both-
    // sides/driver-seat-position-compare chain into one OPEN predicate per
    // character name (a pure query, same "the predicate IS the only Lua-
    // observable fact" convention as VehicleState::isTankClassPredicate).
    OpenValueMap<bool>& isRcDriverPredicate() { return isRcDriverPredicate_; }

    // vehicle_set_team (Sec18.20): the six-team-id table's own real-world
    // name mapping is OPEN (Sec18.20's own flagged item) - CONFIRMED only
    // the default fallback value (9, "no match"), so this project models a
    // test-registerable name->id map with that one CONFIRMED default, not
    // an invented mapping.
    static constexpr int32_t kVehicleTeamIdDefault = 9;

protected: // private in EngineState before the split
    std::unordered_map<std::string, std::string> onVehicleExitByTarget_;
    std::vector<std::pair<std::string, std::string>> rcPollQueries_;
    std::unordered_map<std::string, VehicleState> vehicles_;
    std::map<std::string, std::string> vehiclePathfindGoal_;   // Sec9.10a: vehicle -> goal object name
    std::unordered_set<std::string> vehiclePathfindResolvable_; // Sec9.10, test-only gate

    // --- fields backing the 9 functions from spec-lua-api-behaviour.md
    // Sec10.1-Sec10.9, see each field's own public-accessor doc comment
    // above for citation/reasoning. ---
    OpenValue<bool> vehicleStoreActive_{"vehicle-store active flag (0x022cdf08)", "spec-lua-api-behaviour.md Sec10.1/Sec26.28"};
    // installVehicleTypeClassTable()'s own backing store, see that method's
    // own doc comment above.
    std::unordered_map<std::string, int32_t> vehicleTypeClassByLowerName_;
    std::unordered_map<std::string, int32_t> vehicleSpecialTypeByLowerName_;
    std::vector<std::string> vehicleWaitQueries_;       // ditto (feature/route-table-driving)

    std::unordered_map<std::string, bool> helicopterDontDeathSpiral_;
    HelicopterFlyTo helicopterFlyTo_;
    OpenValueMap<bool> vehicleInAirByCharacter_{"character's cached active vehicle is airborne (0x00ad4040)",
                                                "spec-lua-api-behaviour.md Sec28.8"};
    std::unordered_map<std::string, uint64_t> evacuateFromVehiclesCount_;
    int vehicleExitGroupNullThisGuardCount_ = 0; // Sec30.5/Sec30.7 - test-observable only, not a real engine field
    std::unordered_map<std::string, Helicopter> helicopters_;
    OpenValue<bool> helicopterFireDispatcherResult_{
        "shared helicopter-fire dispatcher's own return value (invisible in the decompiled view)",
        "spec-lua-api-behaviour.md Sec32.7"};
    SpawningBoats spawningBoats_;
    std::unordered_map<std::string, bool> trafficLanesEnabled_;
    float trafficDensity_ = 0.0f;                               // Sec6.14, CHOSEN default
    bool vehicleSpawningEnabled_ = true;                        // Sec6.15, CHOSEN default
    OpenValueMap<int32_t> onVehicleEnterResolverKind_{
        "on_vehicle_enter resolved-object kind (1=character bit 0x4@+0xb/2=vehicle bit 0x8@+0xb)",
        "spec-lua-api-behaviour.md Sec4.12"};
    OpenValue<double> pathfindResult_{"0x00b2b140 return value (semantics unknown)",
                                      "spec-lua-api-behaviour.md Sec30.5/Sec30.7"};
    int vehiclePathfindNonStringArg2GuardCount_ = 0;              // HOST-SAFETY counter, Sec30.5/Sec30.7
    bool twoDTankControlsEnabled_ = false;                        // Sec30.5, global byte 0x0278cad8
    OpenValue<double> rcSignalRange_{"RC signal range global 0x0278cad4", "spec-lua-api-behaviour.md Sec30.5"};
    bool malfunctionGlobalByte02625852_ = false;                  // Sec30.5
    OpenValue<double> vehicleNavmeshDispatchResult_{"0x00b24fa0 navmesh-pathfind dispatcher raw result",
                                                    "spec-lua-api-behaviour.md Sec22.1"};
    bool roadblocksEnabled_ = true;
    uint64_t roadblocksDisableCleanupCount_ = 0;
    std::vector<OnVehicleExitEvent> onVehicleExitEvents_;

    // --- Wave 2, Batch W2-4 (Sec22+Sec24+Sec25 gameplay residue, 78 names,
    // 2026-10-08) - see each public accessor's own doc comment above for
    // citation/reasoning. ---
    bool spotlightHeliLaserEnabled_ = false;
    bool roadblocksAmbientTrafficEnabled_ = false;
    uint64_t roadblocksAmbientTrafficNoSessionSkipCount_ = 0;

    // =====================================================================
    // Wave 2, Batch W2-7 (2026-10-08) private backing fields - see each
    // public accessor's own doc comment above for citation/reasoning.
    // =====================================================================
    std::unordered_map<int64_t, RoadblockRecord> roadblocks_;
    int64_t nextRoadblockHandle_ = 1;
    OpenValueMap<bool> roadblockPlacementResolves_{"roadblock placement-object resolve (no road-network data here)",
                                                   "spec-lua-api-behaviour.md Sec39.2"};
    // Start-up value 200.0: RESOLVED 2026-10-12 (Sec39.2), tweak_table.xtbl `RC_vehicle_gun_max_distance`
    // (seeded by applySpecInitialState()).
    OpenValue<double> rcMaxSignalRange_{"RC max signal range global float",
                                        "spec-lua-api-behaviour.md Sec39.2"};
    bool copsShootingFromVehicles_ = false;
    double spotlightHeliLaserSize_ = 1.0;
    bool vehicleForcedCorpseRemovalEnabled_ = true;
    uint64_t vehicleDetonateRandomNoReferenceGuardCount_ = 0;
    uint64_t vehicleDetonateRandomFailedReferenceGuardCount_ = 0;
    uint64_t vehicleDetonateRandomResolvedReferenceNoOpCount_ = 0;
    uint64_t vtolFlybyM18BroadcastCount_ = 0;
    int32_t parkingSpotMax_ = 18;
    std::unordered_map<std::string, AirplaneRecord> airplanes_;
    uint64_t airplaneFlyToLeakedRequestCount_ = 0;
    uint64_t airplaneFlyToNullTargetGuardCount_ = 0;
    bool rcGunActiveFlag_ = false;
    std::unordered_map<std::string, std::string> onHitPedVehicleCallbacks_;
    std::unordered_map<std::string, std::string> onVehicleDebrisFlowRecycledCallbacks_;
    double helicopterJitterMin_ = 0.0;
    double helicopterJitterMax_ = 0.0;
    bool parkingSpotSlot1Enabled_ = true;
    std::string forcedSeatVehicleName_;
    OpenValueMap<bool> isRcDriverPredicate_{"player_is_rc_driver's own combined resolve/position-compare predicate",
                                            "spec-lua-api-behaviour.md Sec18.10"};
    std::unordered_map<std::string, int32_t> vehicleTeamIdByName_;
};

} // namespace sr3luahost
