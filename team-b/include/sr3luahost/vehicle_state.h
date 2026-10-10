// Vehicle-shaped engine state: VehicleState (and its own nested types).
//
// Split out of engine_state.h (2026-10-08) as a zero-behavior-change
// structural move, so that a merge can only ever land a new field in its
// own domain's file - the monolithic header kept having fields from one
// struct merged into a DIFFERENT struct whose closing brace sat next to
// it. engine_state.h includes this header, so callers keep including
// engine_state.h only; add new fields to these structs HERE.
//
// In the doc comments below, "this header", "this document", "this file"
// and "this header's own top note" keep their pre-split meaning: the
// engine-state header set as a whole (engine_state.h plus the per-domain
// headers it includes), whose top note is at the head of engine_state.h.
// "above"/"below" pointers to a type or member now in another file were
// re-worded to name that file; every other line moved verbatim.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>

#include "sr3luahost/open_state.h"
#include "sr3luahost/world_position.h"

namespace sr3luahost {

// vehicle_set_invulnerable_to_player_explosives / vehicle_disable_explosion_
// and_damage_vfx / vehicle_set_special_override_never_ghost /
// vehicle_clear_all_radio_locks / vehicle_is_vtol (spec-lua-api-behaviour.md
// Sec30.5, "ranking tranche 03") and the vehicle cluster of ranking tranche
// 05 (Sec33.1, batch 2026-10-02 - the first batch to need vehicle-shaped
// state; merged into this one struct rather than two). Same "keyed by the
// plain name string it was given" simplification CharacterState's own top
// note already establishes for characters - the real resolution chain
// Sec30.5's shared preamble describes (character-redirect-when-driving,
// THEN the per-kind vehicle resolver, liveness, a +0x68 gate, virtual slot
// +0x70) is NOT reproduced; every vehicle function in this project's scope
// instead gates on the SAME shared objectResolves() map every other object
// kind already uses (Sec29's own "one shared global name map" finding - a
// vehicle name is just another name in that one map, not a distinctly-
// resolved kind).
struct VehicleState {
    // +0x1d7c/+0x1d7d force-flag bytes (Sec30.5, CONFIRMED structure, 4
    // bits total). Packed here as ONE 16-bit value inside one OpenBits32 -
    // this project's OWN choice, not a real combined engine field: the low
    // byte holds +0x1d7c's own bits 0x2/0x4 verbatim; the high byte holds
    // +0x1d7d's own bits 0x20/0x40 shifted left 8, so both real bytes fit
    // one word without colliding. NOTE: Sec33.1 separately tracks two MORE
    // bits of this SAME +0x1d7c byte (0x8/0x10, see forceFlags1d7c below) -
    // this project keeps them as independent fields rather than unifying
    // into one bit-accurate byte model, since no in-scope function ever
    // reads the byte back as a whole.
    OpenBits32 forceFlags{"vehicle force-flag bytes (+0x1d7c/+0x1d7d)", "spec-lua-api-behaviour.md Sec30.5"};
    static constexpr uint32_t kInvulnerableToPlayerExplosivesBit = 0x0002u; // +0x1d7c bit 0x2
    static constexpr uint32_t kRadioControlsLockedBit = 0x0004u;           // +0x1d7c bit 0x4
    static constexpr uint32_t kDisableExpAndDamageVfxBit = 0x2000u;        // +0x1d7d bit 0x20, shifted <<8
    static constexpr uint32_t kSpecialOverrideNeverGhostBit = 0x4000u;     // +0x1d7d bit 0x40, shifted <<8

    // The shared "variant-1 double-gate" abstraction (Sec30.5: "either
    // true -> write the bit directly; both false -> open an opcode-0x46
    // record with hashed tags instead") - same abstracted-to-one-flag
    // convention CharacterState::CounterOnGrabbed::gatePasses already
    // establishes (Sec28.19) for an analogous real double-gate shape.
    // CONFIRMED that all 4 force-flag setters (plus
    // vehicle_clear_all_radio_locks's own per-vehicle authority check)
    // share this identical shape. UPDATE 2026-10-03: Sec3.7's own
    // single-player resolution (cross-ref Sec6.22) settles this gate for
    // the vehicle side too - see CharacterState::forceFlagGatePasses's own
    // doc comment above for the full trace. Seeded true at construction
    // (EngineState::getOrCreateVehicle), CONFIRMED not CHOSEN; a test can
    // still .set(false) per vehicle to exercise the now known-dead
    // replicate-only branch for structural coverage.
    OpenValue<bool> forceFlagGatePasses{"vehicle force-flag double-gate (0x008ae480/0x008837a0)",
                                        "spec-lua-api-behaviour.md Sec30.5"};

    // Shared vehicle fact, read by BOTH batches: vehicle_is_vtol (Sec30.5:
    // "(+0xbf4)+0x2c equals 4 = VTOL") and vehicle_is_helicopter/
    // vehicle_is_vtol_hover/vehicle_is_vtol_jet (Sec33.1: CONFIRMED values 3
    // = helicopter, 4 = VTOL, tested by name) - ONE real field at the SAME
    // offset, unified here under Sec33.1's name since more consumers use
    // it. UPDATE 2026-10-10 (Team A spec sync, HANDOFF.md commit 849a88e):
    // the field's own real identity, previously HYPOTHESIS here, is now
    // CONFIRMED - it is NOT on the vehicle instance's own +0x100 AI record;
    // it is the vehicle-TYPE DEFINITION record's own class enum (static
    // per-type data, set once at type load, no runtime writer): 0 = car,
    // 2 = airplane, 3 = heli, 4 = VTOL all CONFIRMED; 1/5/6 remain
    // genuinely OPEN (no spec text gives them). No in-scope setter writes
    // it (a test sets it directly, same convention as CharacterState::
    // stateEnum in character_state.h). UPDATE 2026-10-10 (vehicle-class
    // xtbl wiring task): a REAL, general, data-driven producer now exists -
    // EngineState::loadZoneObjectsSection() (src/lua_engine_state.cpp)
    // populates this field for any zone-placed vehicle instance whose own
    // `script_vehicle_type_name` property (sr3zone::ScriptVehicleTypeName())
    // resolves against installVehicleTypeClassTable()'s real xtbl-sourced
    // class data (sr3vehicleinfo::ParseAllVehicles) - replacing the single
    // hardcoded "m05_heli_pierce" name special case getOrCreateVehicle()
    // used to carry (removed this same task; see that method's own doc
    // comment for the full history). A vehicle with no real zone-spawn
    // backing (every synthetic unit-test fixture, or a real name this
    // project's installed table has no matching type for) still has this
    // field set directly by a test, exactly as before.
    OpenValue<int32_t> flyingType{"vehicle flying-type enum ((+0xbf4)+0x2c)", "spec-lua-api-behaviour.md Sec30.5/Sec33.1"};

    // vehicle_is_vtol_hover / vehicle_is_vtol_jet (Sec33.1): vehicle +0x5e8.
    // CONFIRMED the two functions test disjoint {0,3}/{1,2} subsets; the
    // real-world "hover vs jet" meaning of each value is HYPOTHESIS.
    OpenValue<int32_t> vtolState{"vehicle VTOL hover/jet state (+0x5e8)", "spec-lua-api-behaviour.md Sec33.1"};

    // vehicle_is_ready (Sec33.1): three independently-gated bits. Real-
    // world meaning of the two "not ready" bits is OPEN per spec; the
    // "fully set up" bit cross-links to vehicle_set_kneecappers's own
    // deferred-write gate below (HIGH CONFIDENCE, same spec paragraph).
    OpenValue<bool> notReadyBit3b{"vehicle not-ready flag (+0x3b bit 0)", "spec-lua-api-behaviour.md Sec33.1"};
    OpenValue<bool> notReadyBit3a{"vehicle not-ready flag (+0x3a bit 0)", "spec-lua-api-behaviour.md Sec33.1"};
    OpenValue<bool> fullySetUp{"vehicle fully-set-up flag (+0x33 bit 0x8)", "spec-lua-api-behaviour.md Sec33.1"};

    // vehicle_never_flatten_tires (Sec33.1): double-gate setter, local bit
    // +0x1d7b bit 0x1 (this project always applies the local write - see
    // EngineState::replicateStateChange's own doc comment for why). Reader
    // not traced per spec (OPEN) - no Lua-visible query of this bit is in
    // scope, so that gap has no further effect here.
    OpenBits32 forceFlags1d7b{"vehicle force-flags byte +0x1d7b", "spec-lua-api-behaviour.md Sec33.1"};
    // vehicle_infinite_mass/set_unjackable_flag (Sec9.8/Sec9.13, 2026-10-07
    // batch): 2 more confirmed bits on this SAME shared byte, each
    // independently corroborated by its own setter's literal debug-label
    // string ("m_force_flagsinfinite_vehicle_collision_mass"/
    // "m_force_flagsunenterable").
    static constexpr uint32_t kInfiniteMassBit1d7b = 0x2u;
    static constexpr uint32_t kUnjackableBit1d7b = 0x40u;
    // set_unjackable_flag's own top-level wrapper (Sec9.13) ALSO
    // unconditionally writes the same boolean into bit 0x2 of offset +0xac
    // on a secondary sub-object reached via the vehicle's own +0xbf8
    // pointer - no gate, no replication, every call - a genuine asymmetry
    // NOT shared with vehicle_infinite_mass's own analogous wrapper. This
    // project has no modeled +0xbf8 sub-object, so the write is tracked as
    // its own flat field rather than a nested struct (consistent with this
    // header's other "the write is real, the sub-object is not separately
    // modeled" fields).
    bool unjackableSecondarySubObjectFlag = false;
    // vehicle_set_weapons_disarmed (Sec33.1): +0x1d7e bit 0x4.
    OpenBits32 forceFlags1d7e{"vehicle force-flags byte +0x1d7e", "spec-lua-api-behaviour.md Sec33.1"};
    // vehicle_set_sirenlights (Sec33.1): +0x1d7c bit 0x10 (flashing
    // headlights) / bit 0x8 (siren lights) - a DIFFERENT byte from the two
    // above (CONFIRMED distinct offsets).
    OpenBits32 forceFlags1d7c{"vehicle force-flags byte +0x1d7c (headlight 0x10 / siren 0x8)",
                              "spec-lua-api-behaviour.md Sec33.1"};
    // vehicle_set_no_chase (Sec33.1): vehicle-AI sub-record (+0xc68) own
    // sub-byte +5 bit 0x1 - CONFIRMED the SAME setter this document's
    // existing vehicle_disable_chase entry reaches (out of this batch's
    // own scope to implement), through an extra wrapper this project does
    // not model (same "always apply the write directly" convention as
    // every other double-gate setter in this header).
    OpenBits32 vehicleAiForceFlags{"vehicle-AI force-flags byte (+0xc68)+5", "spec-lua-api-behaviour.md Sec33.1"};

    // vehicle_set_kneecappers (Sec33.1): this project has no per-wheel
    // physics/collision pool to simulate (48-vehicle fixed pool, CONFIRMED
    // structure, stated simplification same as several `*_add_do` records
    // elsewhere in this header) - only the two CONFIRMED gate outcomes are
    // tracked: whether kneecappers are currently requested enabled, and
    // whether that request is still deferred (requested before the
    // vehicle was "fully set up", fullySetUp above).
    bool kneecappersEnabled = false;
    bool kneecappersDeferred = false;

    // vehicle_set_sirenlights (Sec33.1): the vehicle-class enum
    // ((+0xbf4)+0x4c4, a DIFFERENT field from flyingType above despite the
    // shared +0xbf4 base - CONFIRMED distinct offsets) and the body-active
    // gate (+0xbd0). The actual Wwise siren SOUND side effect is NOT
    // modeled anywhere here (no Wwise event table in this project - same
    // gap as game_audio_get_audio_id's own Wwise-hash resolver elsewhere).
    OpenValue<int32_t> vehicleClass{"vehicle class enum ((+0xbf4)+0x4c4)", "spec-lua-api-behaviour.md Sec33.1"};
    OpenValue<bool> bodyActive{"vehicle body-active flag (+0xbd0)", "spec-lua-api-behaviour.md Sec33.1"};
    // spec-player-runtime.md Sec13.4 step 5's own gate, `0x00ad3140(vehicle)`:
    // "(vehicle+0xbf4)+0x4c4 == 0xe" - the SAME physical field as vehicleClass
    // above, re-read by the real seat-pick validator 0x00b0fdf0 to decide
    // whether its step 6 (the 8-seat vehicle-wide team-conflict scan) runs at
    // all. Reused here BY VALUE, not duplicated - see
    // computeVehicleEnterSubmitAccepted()'s own doc comment in
    // lua_spec_confirmed_stubs_b15_wave2_core_dispatchers.cpp for the step
    // 6/7 implementation that consumes it.
    static constexpr int32_t kVehicleClassTank = 0xe;

    // vehicle_set_ambient (Sec33.1): +0x16c0 bit 0x1 marker, the vehicle-AI
    // sub-record's mode/sub-mode (CONFIRMED values 6/0x11, HYPOTHESIS real-
    // world meaning), and the speed-cap target - the SAME per-vehicle
    // sub-record field (+0xc68, then +0x9c) `vehicle_speed_override`
    // (Sec9.5, 2026-10-07 batch) now also writes, clamped to at most
    // 1000.0 (both functions' own values converge on the SAME field by
    // address, per Sec9.5's own text). vehicle_set_ambient's own value is
    // stored UNCONVERTED (its own Sec33.1 text: "passes the number
    // unconverted"); vehicle_speed_override converts its own mph argument
    // to m/s (literal constant 0.44703999161720276) BEFORE storing here -
    // the field itself always holds the real engine-side (m/s) value, just
    // reached by two different Lua-argument conventions.
    OpenBits32 ambientFlags{"vehicle ambient marker byte +0x16c0", "spec-lua-api-behaviour.md Sec33.1"};
    int32_t aiMode = 0;    // 0 = never set; CONFIRMED value when set: 6
    int32_t aiSubMode = 0; // 0 = never set; CONFIRMED value when set: 0x11
    bool hasSpeedCap = false;
    double speedCapRaw = 0.0; // unconverted Lua number, clamped to <= 1000.0

    // vehicle_spotlight_is_target_spotted (Sec33.1): the side effect this
    // project CAN model (re-aiming the spotlight at the resolved target,
    // every call) is tracked; the actual range/angle/raycast boolean test
    // needs real position/geometry data this project has nowhere in its
    // state (no coordinate system at all, stated project-wide
    // simplification) - NOT modeled, refused as OPEN by the stub rather
    // than fabricated (same convention as game_audio_get_audio_id's
    // Wwise-hash gap).
    std::string spotlightTargetName; // last re-aimed target name, "" = none

    // vehicle_engine_check_running (Sec38.1, "ranking tranche 06"): "On
    // failure (unresolved, or a specific object flag clear) pushes false
    // and returns 1 value; on success it calls the engine-running-bit
    // getter, discards the result, and returns 0 values" - so "success"
    // here means resolved AND this gate passes, NOT "the engine is
    // actually running" (that bit is read and thrown away either way,
    // CONFIRMED - the mandated "structurally can never answer true"
    // quirk). The exact real bit this "specific object flag" tests is not
    // named by address in scope - this project's own minimal stand-in,
    // scoped only to this one query. OPEN until set.
    OpenValue<bool> engineCheckGatePasses{"vehicle_engine_check_running success gate (a specific object flag)",
                                          "spec-lua-api-behaviour.md Sec38.1"};

    // --- vehicle_set_vulnerable (Sec20.1) / turn_invulnerable (Sec3.7) /
    // vehicle_is_invulnerable (Sec46.5) - ranking tranche 14's own Sec46.1
    // bit-conflict resolution, batch 2026-10-02 -------------------------

    // Vehicle +0x1d7a, bit 0x01 ("invulnerable") / bit 0x02 ("always apply
    // player damage"). RESOLVED 2026-10-02 (ranking tranche 14, Sec46.1):
    // bit 0x01 is CONFIRMED correct by direct disassembly of the merge
    // idiom (Sec3.7 was right all along); Sec20.1's own prior "bit 0x8"
    // reading was the error, now corrected in the spec text itself. A
    // DIFFERENT byte from forceFlags/forceFlags1d7b/forceFlags1d7c/
    // forceFlags1d7e above (all CONFIRMED distinct offsets, Sec30.5/Sec33.1).
    OpenBits32 forceFlags1d7a{"vehicle force-flags byte +0x1d7a (invulnerable 0x01 / always-apply-player-damage 0x02)",
                              "spec-lua-api-behaviour.md Sec3.7/Sec20.1/Sec46.1"};
    static constexpr uint32_t kInvulnerableBit1d7a = 0x01u;
    static constexpr uint32_t kAlwaysApplyPlayerDamageBit1d7a = 0x02u;

    // --- vehicle_lights_on (Sec46.5, "ranking tranche 14") - a faithful
    // naming trap, NOT "fixed": vehicle_lights_on(v, false) does NOT turn
    // the lights off - it clears BOTH the force-on and force-off flags,
    // returning the lights to automatic control (CONFIRMED). The true-path's
    // own exact flag combination beyond "force on" is not separately given
    // by this tranche's text - this project's own minimal, stated choice:
    // true sets force-on and clears force-off (the natural complement of
    // the CONFIRMED false-path's "clear both").
    OpenBits32 lightsForceFlags{"vehicle headlight force-on/force-off flags (naming trap: false = automatic, not off)",
                                "spec-lua-api-behaviour.md Sec46.5"};
    static constexpr uint32_t kLightsForceOnBit = 0x1u;
    static constexpr uint32_t kLightsForceOffBit = 0x2u;

    // --- vehicle_tire_indicators_alive (Sec46.5) - a faithful naming trap:
    // reports 8 ("all alive") whenever the vehicle's tire-indicator object
    // is simply DISABLED, not actually alive (CONFIRMED). This project has
    // no real per-tire alive/damage simulation (same gap as
    // kneecappersEnabled's own "no per-wheel physics pool" precedent,
    // Sec33.1) - only the disabled-object short-circuit is modeled; test-
    // settable, OPEN until set.
    OpenValue<bool> tireIndicatorObjectDisabled{"vehicle tire-indicator object disabled flag",
                                                "spec-lua-api-behaviour.md Sec46.5"};

    // --- vehicle_turret_base_to_do (Sec46.4) - batch 2026-10-02 ---------

    // Whether the vehicle's seat 0 is occupied - gates the CONFIRMED null-
    // read crash (Sec46.4's own text: "a resolvable vehicle that has a
    // seat-0 occupant"). This project has no real seat/occupant array (same
    // gap as kneecappersEnabled's own "no per-wheel physics pool" precedent)
    // - test-settable only, OPEN until set.
    OpenValue<bool> seat0Occupied{"vehicle seat-0 occupant present", "spec-lua-api-behaviour.md Sec46.4"};

    // --- vehicle_pathfind_navmesh_do (Sec22.1, wave 2 batch W2-1) - REUSES
    // seat0Occupied above for its own occupant/part-index-0 accessor gate
    // (0x00ab5070 at index 0, "most plausibly the driver" - the same reading
    // Sec9.10/Sec30.5/Sec46.4 give it). The record is the raw request this
    // host can observe; 0x00b24fa0's own +0xc68 sub-record delegate
    // (0x00b4f150) and code-byte-7 branch are OPEN (Sec22.1) and not modelled.
    struct NavmeshPathfindRequest {
        std::string destinationName; // arg 2 when it resolves; "" = the vehicle's own position/orientation (0x00da38e0)
        bool flagArg3 = false, flagArg4 = false, flagArg5 = false; // packed into one flag byte by 0x00b24fa0
        int64_t numberArg6 = 0;      // arg 6, truncated (0x00dfe160/0x00ea2596)
        int32_t requestId = 0;       // kind 2 (0x006f62b0(vehicle, 2, 0)) - shared namespace with move_to (Sec22.1)
        bool appliedLocally = false; // single gate true -> +0xc68 delegate; false -> opcode-0x41 record
    };
    std::optional<NavmeshPathfindRequest> lastNavmeshPathfindRequest;
    uint64_t navmeshPathfindCount = 0;

    // --- vehicle_set_tire_durability / vehicle_set_tire_damage_multiplier
    // (Sec46.5), batch 2026-10-02 (resumed session) ----------------------

    // CONFIRMED (Sec46.5): both setters "silently ignore any value <= 0
    // (the field can never be reset to 0 from Lua this way)" - the stub
    // enforces that gate before calling set(), so these are only ever
    // known holding a value > 0. The real numeric unit/range is OPEN (not
    // stated beyond "a value"); no in-scope Lua getter reads either field
    // back, so nothing downstream depends on a specific default.
    OpenValue<double> tireDurability{"vehicle tire durability", "spec-lua-api-behaviour.md Sec46.5"};
    OpenValue<double> tireDamageMultiplier{"vehicle tire damage multiplier", "spec-lua-api-behaviour.md Sec46.5"};

    // get_dist's own shared position-lookup (Sec4.2) - see
    // CharacterState::worldPosition's own doc comment for the full
    // reasoning (same field, same OPEN-by-default rationale, same
    // FLT_MAX-vs-OpenStateError distinction). Not yet read by get_dist's
    // own stub (which uses the CharacterState-shaped simplification already
    // established for other dual-typed resolvers, e.g. on_take_damage) -
    // added here so a future vehicle-specific position need doesn't have to
    // re-derive this same reasoning.
    OpenValue<WorldPosition> worldPosition{"vehicle world position (get_dist's own 0x00a455a0 lookup)",
                                            "spec-lua-api-behaviour.md Sec4.2"};

    // --- vehicle_chase (Sec9.4), dedicated pass, 2026-10-07: this
    // document's own single highest real call-volume CONFIRMED-but-
    // unimplemented native (72 static call sites) at the time this was
    // written - see stub_vehicle_chase's own doc comment
    // (lua_spec_confirmed_stubs.cpp) for the full two-path reasoning. ---

    // The per-vehicle drive-mode predicate vehicle_chase's own branch
    // selector reads (0x00ad31a0, true only for internal state values
    // 3/4 - the SAME address the helicopter-fire routine at Sec14.11
    // independently cross-references, "true only for internal state
    // values 3/4, Sec9.4"). CHOSEN default false (a never-chased vehicle
    // is not mid-chase) - this project's own plain tracking field for an
    // observable branch outcome, NOT a literal 1:1 copy of the real
    // engine global (whose own CONFIRMED initial value this spec text
    // does not state), same "self-balancing default" convention as
    // kneecappersEnabled/kneecappersDeferred above. stub_vehicle_chase
    // itself flips this true the first time it commits a fresh chase
    // record - a JUDGMENT CALL (not independently confirmed by its own
    // disassembly citation that 0x00b47110 writes this specific field),
    // made because the spec's own side-effects text frames this native as
    // "starts (or updates an in-progress)" across repeat calls: without
    // this write, no sequence of calls to this one native could ever
    // reach the "already chasing" branch, stranding half of this
    // CONFIRMED two-path body as permanently dead code in this project's
    // model. The exact internal state VALUE written (3 vs 4) stays OPEN,
    // per spec, since which of the two 0x00b47110 actually sets was not
    // independently pinned down - only the boolean predicate is modeled.
    bool aiDriveModeIsChasing = false;

    // The resolved chase-target name (arg 2, default the literal
    // "#PLAYER1#" read directly from .rdata - CONFIRMED distinct from the
    // generic "#PLAYER#" sentinel Sec4.13 documents) and the 4 raw
    // optional booleans (args 3-6, CONFIRMED defaults true/false/true/
    // false), (re)stored on EVERY successful call regardless of which of
    // the two paths is taken (CONFIRMED - the spec's own text: "Either
    // way, the four optional booleans ... are packed into one flag byte
    // alongside the resolved chase-target record before being written").
    // This project has no coordinate system modeled anywhere (the same
    // stated project-wide gap WorldPosition's own doc comment and
    // spotlightTargetName above already note), so the "already chasing"
    // path's own record-and-replicate position push has nothing concrete
    // beyond the target NAME to re-store - the name itself stands in for
    // "the position fields" here. The 4 booleans are stored BY ARGUMENT
    // POSITION, never as named/claimed bits: the spec itself flags the
    // real packed flag byte's bit-to-argument mapping OPEN ("the
    // individual bit-to-argument mapping inside that packed byte was not
    // independently pinned down, per this project's standing caution
    // against asserting a bitfield's meaning from offset/position alone,
    // WALLS.md") - this project does not invent that mapping, so these 4
    // inputs are tracked as its own honest record of "the last call's raw
    // arguments", same convention as HelicopterFlyTo::Request/
    // SignInRequest elsewhere in this header.
    std::string chaseTargetName; // "" = vehicle_chase never successfully called
    bool chaseFlagArg3 = true;   // last call's raw arg 3 (CONFIRMED default true)
    bool chaseFlagArg4 = false;  // last call's raw arg 4 (CONFIRMED default false)
    bool chaseFlagArg5 = true;   // last call's raw arg 5 (CONFIRMED default true)
    bool chaseFlagArg6 = false;  // last call's raw arg 6 (CONFIRMED default false)

    // on_vehicle_destroyed (Sec3.3)'s OWN vehicle-path branch: the
    // vehicle's own hook slot `0` (`+0x30`/`+0x34` sub-record, base
    // `object+0xb0`, via `0x00a56d50`) - reached only when the generic/
    // player-class-gated chain failed (EngineState::playerClassChainResolves()
    // false/absent) and the plain vehicle-name resolver (`0x0062a190`) +
    // liveness (`0x00853b10`) succeeded instead (this project's flat
    // objectResolves() gate stands in for that pair, Sec29's "one shared
    // name map" convention). `0x00a56d50` is CONFIRMED (Sec3.3) to be "an
    // otherwise-identical sibling helper" to `0x005e4660` (the
    // character-hook setter CharacterState::onDeathCallback/
    // onVehicleDestroyedPlayerSlotCallback in character_state.h use) - the same
    // empty-string-clears-slot convention is applied here BY DIRECT
    // ANALOGY, not independently re-derived for this specific setter
    // (flagged, not asserted CONFIRMED for this exact detail). Default
    // empty ("no callback registered").
    std::string onVehicleDestroyedCallback;

    // vehicle_suppress_npc_exit (Sec4.5), batch 2026-10-08 (wave 1 batch
    // 1, §1-4): vehicle-AI sub-record (+0xc68) sub-byte +0x4 - a DIFFERENT
    // sub-byte from vehicleAiForceFlags above (+5, vehicle_set_no_chase,
    // Sec33.1). CONFIRMED (2026-10-07 direct disassembly): bit 0x40
    // ("no_stop_enter_with_driver") and bit 0x20 ("force_no_passenger_exit"),
    // each independently named by its own setter's literal `.rdata` debug-
    // label string. CORRECTION, relayed by the orchestrator and verified
    // directly against the spec text: BOTH bits are always set TOGETHER
    // to the SAME boolean by either call direction - NOT one bit per
    // setter, as an earlier reading of this function wrongly assumed (see
    // stub_vehicle_suppress_npc_exit's own doc comment). A third,
    // independently-confirmed bit (0x2, vehicle_ignore_repulsors/Sec13.8)
    // on this SAME byte is declared below (kIgnoreRepulsorsBit).
    OpenBits32 vehicleAiForceFlagsByte4{"vehicle-AI force-flags byte (+0xc68)+4 (both bits set together)",
                                         "spec-lua-api-behaviour.md Sec4.5"};
    static constexpr uint32_t kNoStopEnterWithDriverBit = 0x40u;
    static constexpr uint32_t kForceNoPassengerExitBit = 0x20u;

    // vehicle_set_obey_traffic_lights (Sec4.5a, closed 2026-10-09 wave 10):
    // bit 0x01 of this SAME +0xc68+4 byte - a fourth confirmed bit joining
    // kNoStopEnterWithDriverBit/kForceNoPassengerExitBit/kIgnoreRepulsorsBit
    // above.
    static constexpr uint32_t kObeyTrafficLightsBit = 0x01u;

    // --- vehicle_set_malfunctioning (spec-lua-api-behaviour.md Sec30.5,
    // "ranking tranche 03"), batch 2026-10-08 (Batch 7, dedicated agent) ---

    // Vehicle +0xf8 bits 0x1/0x2 - a NEW byte, distinct from forceFlags/
    // forceFlags1d7a/1d7b/1d7c/1d7e above (all CONFIRMED distinct offsets
    // elsewhere in this header; +0xf8 is not one of them).
    OpenBits32 forceFlagsF8{"vehicle force-flag byte +0xf8 (malfunction bits 0x1/0x2)",
                            "spec-lua-api-behaviour.md Sec30.5"};
    static constexpr uint32_t kMalfunctionBit1F8 = 0x1u;
    static constexpr uint32_t kMalfunctionBit2F8 = 0x2u;

    // --- vehicle_component_detach (Sec30.5), batch 2026-10-08 (Batch 7) --

    // This project has no real per-vehicle component tree (same "no
    // generic entity shape invented" boundary this header's own top note
    // states) - componentNames is test-populated only (same never-
    // populated-registry convention as VdoObject/VintDataItem), scanned
    // case-insensitively by the stub itself (caseInsensitiveEquals,
    // lua_spec_confirmed_stubs_common.h). detachedComponents marks bit
    // 0x200000 of the real +0xf0 per-component flags word - CONFIRMED
    // "every path regardless of outcome" once a component is actually
    // found (a miss, -1, does nothing at all - no component to mark).
    std::unordered_set<std::string> componentNames;
    std::unordered_set<std::string> detachedComponents;
    uint64_t componentDetachAttempts = 0;

    // --- vehicle_stop_do / teleport_vehicle_to_object's own SHARED
    // 0x00abe510 stop routine (Sec30.5/Sec30.6), batch 2026-10-08 (Batch 7)
    // - see EngineState::stopVehicle()'s own doc comment for the full
    // shared-infrastructure reasoning. ---------------------------------
    uint64_t stopRequestCount = 0; // direct-apply (authority-passes) branch only; the replicate-only branch is counted via replicateStateChange's own call-site marker, not here

    // --- teleport_vehicle_to_object (Sec30.6), batch 2026-10-08 (Batch 7) -

    // The vehicle-name-path-only refusal gate "(+0xbf8)+0xa8 >= 0" - OPEN
    // per spec (no CONFIRMED initial value; "applies only to the vehicle-
    // name path", never the via-character path).
    OpenValue<int32_t> teleportNameGateField0xbf8a8{"(+0xbf8)+0xa8 vehicle-name-path teleport refusal gate",
                                                    "spec-lua-api-behaviour.md Sec30.6"};
    // This project has no orientation/rotation data anywhere (WorldPosition
    // in world_position.h is "plain xyz, no rotation") and no seat/occupant array beyond
    // seat0Occupied below - so the real 4x4 pose build/offset/rotate and
    // the "detaches all seats" step are NOT simulated; only the raw request
    // (CHOSEN, test-observable, same "record the request, not real
    // physics" convention as MinimapIconRecord/ObjectIndicatorRecord) is
    // kept.
    struct TeleportRequest {
        std::string targetName;
        double offsetX = 0.0, offsetY = 0.0, offsetZ = 0.0; // args 3/4/5, CONFIRMED default 0.0 each
        double headingRadians = 0.0;                        // arg 6 * 0.017453, CONFIRMED conversion literal
        bool arg7 = true;                                   // CONFIRMED default true; OPEN what it selects (0x00a874f0)
    };
    std::optional<TeleportRequest> lastTeleportRequest;
    uint64_t teleportRequestCount = 0;
    uint64_t teleportDetachAllSeatsCount = 0; // "detaches all seats" - no seat/occupant array modelled beyond seat0Occupied, counter only

    // --- vehicle_pathfind_to_do (Sec30.5), batch 2026-10-08 (Batch 7) ----
    //
    // Driver check 0x00ab5070(vehicle,0) non-null ("seat 0 = driver, HIGH
    // CONFIDENCE") is the SAME real seat-0-occupant fact
    // VehicleState::seat0Occupied above already models for
    // vehicle_turret_base_to_do (Sec46.4) - reused directly, not
    // duplicated.

    // --- Batch 2026-10-08 (Sec5-9 fan-out, Batch 2): vehicle_max_speed
    // (Sec9.6), vehicle_detonate (Sec9.7), helicopter_set_path_orientation
    // (Sec9.18) - see
    // src/lua_spec_confirmed_stubs_b09_batch2_hud_audio.cpp's own doc
    // comment above each stub_* function for the full citation/reasoning.
    // ---------------------------------------------------------------

    // vehicle_max_speed (Sec9.6): mph->m/s converted value (SAME literal
    // 0.44703999161720276 constant vehicle_speed_override, Sec9.5, uses),
    // handed "by address" to a dedicated setter (0x00a7ded0) this document
    // does not decompile further - no clamp, no gate, and no record-and-
    // replicate call is CONFIRMED either present or absent at this call
    // depth (OPEN, Sec9.6's own text), so this project models only the one
    // thing it IS confident of: a direct, ungated write - the simplest
    // honest reading, not a guess that a clamp/gate/record definitely
    // don't exist.
    double maxSpeedMps = 0.0;

    // vehicle_detonate (Sec9.7): the SAME "destroyed" flag bit
    // vehicle_is_destroyed (Sec4.9, out of this batch's scope) reads - bit
    // 0x1 at offset +0x16c8. OPEN until set (this project has no in-scope
    // writer of this bit elsewhere - a test sets it directly to exercise
    // the already-destroyed no-op branch).
    OpenValue<bool> destroyedFlag16c8{"vehicle byte +0x16c8 bit 0x1 (destroyed)", "spec-lua-api-behaviour.md Sec4.9/Sec9.7"};
    // Test-observable count of calls that passed the destroyed/liveness
    // gate and reached the real damage/force dispatch (0x00a9f4f0) - its
    // own ~1,150-instruction body is OPEN beyond the entry checks already
    // cited (Sec9.7), so nothing beyond "the gate passed" is modeled; per
    // the HARDENING POLICY this is a genuine behavioural gap, not a
    // memory-safety one, so no hardening is needed here.
    uint64_t detonateAppliedCount = 0;

    // helicopter_set_path_orientation (Sec9.18): the orientation/heading
    // value, modeled directly on THIS vehicle (this project's own flat,
    // single-object simplification - the real setter writes either a
    // record-and-replicate opcode-0x42 update or a direct +0x158 field
    // "off a further resolved handle" not confirmed to be the vehicle
    // itself; same "no separate sub-record handle object" simplification
    // vehicle_chase's own +0xc68 sub-record already applies, folded
    // directly into VehicleState).
    double pathOrientation = 0.0;
    // arg 3 (waypoint name), when given and resolved (this project's flat
    // objectResolves() stand-in for the real type-gated resolver + bit
    // test, Sec9.18/Sec9.21 - no real per-object-type descriptor data is
    // modeled, same collapsing convention this whole file already applies
    // project-wide): the association request, recorded honestly as the
    // raw name, not a real link.
    std::string helicopterWaypointAssociation; // "" = never associated

    // --- Batch 5 (ranks 251-350, Sec15/Sec16), 2026-10-08 - see
    // lua_spec_confirmed_stubs_b12_batch5_hud_customization.cpp's own doc
    // comment above each stub_* function for the full citation/reasoning.
    // CORRECTED 2026-10-08 (build fast-follow): this block was originally
    // merged into HudHardeningCounters by mistake (a bad merge-conflict
    // resolution during Batch 5's own merge, extended further by Batch 4's
    // own merge before being fully corrected) even though every field
    // below is accessed on a VehicleState& in lua_spec_confirmed_stubs_
    // b12_batch5_hud_customization.cpp (v.animStartTrackArg2 etc.,
    // VehicleState::kUnenterableForPlayerBit1d7b) - moved here to its real
    // home; no content changed, this is a structural fix only. ---

    // vehicle_anim_start (Sec15.2): no real name->track-id table in this
    // project (the real lookup, 0x004bf810, is a distinct, un-reproduced
    // id table - same "no fixed id table" class of gap as
    // dlc_achievement_counter_set's reader or effect_play's handle). Only
    // the raw request (both track-name arguments) is recorded, matching
    // this header's own "record the raw request, not the real internal
    // dispatch" convention (MinimapIconRecord etc.) - the unconditional
    // record-and-replicate call this function always makes (CONFIRMED,
    // Sec15.2) has no further in-scope observable effect either way, so no
    // gate is modeled on the +0xbd0/+0xa78 anim-controller presence check
    // (nothing in scope reads "did the dispatcher actually run").
    std::string animStartTrackArg2;
    std::string animStartTrackArg3;
    // CHOSEN placeholder: arg 4's own default is an undecoded global float
    // literal (Sec15.2 cites the address 0x01117a4c but not its numeric
    // value) - since nothing in scope reads this value back, a documented
    // placeholder stands in rather than refusing a function nothing
    // observes the result of.
    double animStartBlend = 0.0;
    uint64_t animStartCount = 0;

    // set_unenterable_for_player (Sec15.19): REUSES the existing
    // forceFlags1d7b byte (Sec33.1/Sec9.8/Sec9.13) - bit 0x80, a genuinely
    // new, independently-confirmed bit on that SAME shared byte (Sec15.19's
    // own cross-reference extending Sec9.8/Sec9.13's 2-bit census to 3).
    // The mirrored sub-object write (+0xbf8 -> +0xac bit 0x4) reuses the
    // SAME "no +0xbf8 sub-object modeled, flat field instead" convention
    // unjackableSecondarySubObjectFlag (Sec9.13) already established -
    // kept as its own independent field rather than retrofitting that bit
    // 0x2 field into a shared byte (same header-wide convention).
    static constexpr uint32_t kUnenterableForPlayerBit1d7b = 0x80u;
    bool unenterableForPlayerSecondarySubObjectFlag = false;

    // vehicle_is_tank (Sec15.16): the vtable+0x70 secondary handle's own
    // nested +0xbf4->+0x4c4 field tested == 14. This project does not
    // model that nested nested-handle chain (same "no real handle-
    // resolution mechanism" top-of-file note) - a single OpenValue<bool>
    // stands in for the WHOLE resolved predicate (not the raw field),
    // since this is a pure query and the predicate IS the only Lua-
    // observable fact. OPEN until a test sets it.
    OpenValue<bool> isTankClassPredicate{"vehicle class-record field (+0xbf4 nested handle)+0x4c4 == 14 (Tank)",
                                         "spec-lua-api-behaviour.md Sec15.16"};

    // vehicle_skydiving_start (Sec15.15): the SAME offset NUMBER +0x16c4
    // bit 0x1 a different, unrelated function elsewhere in the full spec
    // text also uses on a DIFFERENT object kind - WALLS.md's own standing
    // caution against inferring identity from a matching offset alone
    // (Sec15.15's own explicit flag) means this is kept as its own,
    // uniquely-named field, never unified with that other sighting.
    // CHOSEN default false ("not yet reached target state" - the natural
    // first-call condition; no in-scope reader). skydiveAnchorSourceName:
    // "" = the vehicle's own default anchor (+0x58/+0x60) was used, else
    // the resolved arg-2 anchor-object name.
    bool skydiveActiveFlag1_16c4 = false;
    std::string skydiveAnchorSourceName;
    uint64_t skydiveStartReplicateCount = 0;
    uint64_t skydiveStartLocalApplyCount = 0;

    // =====================================================================
    // Batch 4 (Sec13+Sec14, native-implementation batch 2026-10-08) - see
    // each field's own citation below.
    // =====================================================================

    // vehicle_ignore_repulsors (Sec13.8): vehicle-AI sub-record (+0xc68) own
    // sub-byte +4 bit 0x2 - a DIFFERENT byte from VehicleState::
    // vehicleAiForceFlags above (that field's own +5 bit 0x1,
    // vehicle_set_no_chase/Sec33.1 - CONFIRMED distinct offsets). Raw-
    // disassembly-confirmed hidden-`this` delegate per Sec13.29 item 1.
    // Reuses the SAME +0xc68+4 byte vehicle_suppress_npc_exit (Sec4.5)
    // already models above as vehicleAiForceFlagsByte4 - a third,
    // independently-confirmed bit (0x2) on that one shared byte, not a
    // separate field (merged here during the Batch 4 merge; both batches
    // picked the identical name/offset independently).
    static constexpr uint32_t kIgnoreRepulsorsBit = 0x2u;

    // vehicle_suppress_flipping (Sec13.28): +0x1d7d bit 0x8 - packed into
    // the SAME combined 16-bit forceFlags word above (this project's own
    // +0x1d7c-low-byte/+0x1d7d-high-byte-shifted-<<8 packing convention,
    // Sec30.5's own doc comment on forceFlags).
    static constexpr uint32_t kSuppressFlippingBit1d7d = 0x800u; // +0x1d7d bit 0x8, shifted <<8

    // set_min_hit_points (Sec13.19) / set_max_hit_points (Sec13.20): vehicle
    // +0x1560 (min), +0x155c (max), +0x1564 (current) - all plain int32,
    // CONFIRMED cross-checked offsets (Sec13.29 cross-function finding #7).
    // No spec gives a real spawn-time default for any of the three - CHOSEN
    // 0 ("never set"), same convention as speedCapRaw/hasSpeedCap above.
    // Named with a trailing "V" to avoid colliding with CharacterState's
    // own identically-named-concept fields in the unrelated struct.
    int32_t minHitPointsV = 0;
    int32_t maxHitPointsV = 0;
    int32_t currentHitPointsV = 0;

    // helicopter_set_max_bank_angle (Sec13.23): flight-control sub-object's
    // own +0x15c field (the resolved angle, in radians, after this
    // function's own CONFIRMED degrees->radians conversion). CHOSEN 0.0 =
    // "never set" (no spec default).
    double maxBankAngleRadians = 0.0;

    // vehicle_disable_chase (Sec14.2) / vehicle_disable_flee (Sec14.25):
    // the exact sub-record bit offset each setter's own delegate
    // (0x00b234f0/0x00b23370) targets is OPEN (not independently traced
    // past the chase/behavior sub-record's own base address, +0xc68,
    // already established) - CHOSEN false ("not disabled") project-tracked
    // stand-ins, same convention as kneecappersEnabled/kneecappersDeferred
    // above (Sec33.1).
    bool chaseDisabled = false;
    bool fleeDisabled = false;

    // vehicle_set_keyframed_physics (Sec14.14): the raw boolean argument,
    // always written and always replicated (CONFIRMED - no double-gate
    // skip at this call site). CHOSEN false = "never set".
    bool keyframedPhysics = false;
    uint64_t keyframedPhysicsSpecialCaseCount = 0; // the hardcoded "m05_heli_pierce" extra block, count only

    // vehicle_hide (Sec14.15): always hides the main body; arg 2 (default
    // false) additionally hides attached sub-objects/occupants - the real
    // per-vehicle attachment array (+0x1358) is NOT modelled (no object-
    // attachment graph anywhere in this project), only the two observable
    // boolean outcomes.
    bool hidden = false;
    bool attachmentsHidden = false;

    // helicopter_shoot_navpoint (Sec14.11) / helicopter_shoot_human
    // (Sec14.18): the shared helicopter-fire dispatcher's (0x00b37250,
    // Sec14.31) own internal targeting/hit-test logic is NOT modelled (no
    // coordinate system anywhere in this project) - test-settable outcome,
    // OPEN until set, shared by BOTH natives per the spec's own explicit
    // sharing (Sec14.18: "the dispatcher's own internal gating is what
    // actually governs whether the shot lands").
    OpenValue<bool> helicopterFireSucceeds{"shared helicopter-fire dispatcher outcome (0x00b37250)",
                                           "spec-lua-api-behaviour.md Sec14.11/Sec14.18/Sec14.31"};
    uint64_t helicopterShootNavpointCount = 0;
    uint64_t helicopterShootHumanCount = 0;

    // on_vehicle_enter_water (Sec14.30): vehicle hook slot 8 (+0x30/+0x34
    // sub-record, already established Sec3.18/Sec3.3's own vehicle-path
    // branch above) - a THIRD distinct registration-helper address
    // (0x00a56d50) sharing the SAME clear/convert/notify shape as the
    // character and mover hook-slot helpers (Sec14.31's own census). Same
    // empty-string-clears-slot convention as onVehicleDestroyedCallback
    // above (same underlying setter).
    std::string onVehicleEnterWaterCallback;

    // on_vehicle_enter (Sec4.12)'s THIRD branch, RESOLVED 2026-10-08: the
    // vehicle's own hook slot 7 (+0x30/+0x34 sub-record, base +0xb0 - the
    // SAME base onVehicleDestroyedCallback/onVehicleEnterWaterCallback
    // above use), reached only when arg 2 resolves via the type-gated bit
    // 0x8@+0xb resolver (the SAME resolver vehicle_is_destroyed/
    // vehicle_suppress_npc_exit's own fallback uses, Sec4.5/Sec4.9) - the
    // SAME 0x00a56d50 vehicle-side helper onVehicleEnterWaterCallback
    // above uses. See CharacterState::onVehicleEnterCharacterSlotCallback's
    // own doc comment for how this project tells this branch apart from
    // the character one (EngineState::onVehicleEnterResolverKind()).
    // Default empty ("no callback registered").
    std::string onVehicleEnterCallback;

    // --- Batch 3 (2026-10-08), spec-lua-api-behaviour.md Sec11+Sec12 (PCR
    // quick-queries + vehicle/HUD/effect cluster) - see each stub's own doc
    // comment (lua_spec_confirmed_stubs_b10_batch3_pcr_vehicle.cpp) for the
    // full per-function citation/reasoning. ---

    // vehicle_set_smoke_and_fire_state (Sec12.6, 0x00a63230): CONFIRMED
    // resolve + unconditional dispatch (0x00aa3f90, OPEN internal) with both
    // booleans independently. CHOSEN false defaults (no stated construction
    // value).
    bool smokeOn = false;
    bool fireVisualOn = false;

    // vehicle_get_driver (Sec12.14, 0x00a62500): CONFIRMED the boolean
    // argument selects between two occupant-getters (0x00ab5100/0x00ab50c0,
    // both OPEN - "any occupant" vs. "specifically the driver seat," not
    // independently distinguished) before converting to a name via
    // 0x00a353d0 (also OPEN). This project has no real occupant/seat
    // registry (same "no per-wheel physics pool"-class gap as
    // kneecappersEnabled above) - one name slot stands in for both getters,
    // OPEN until a test sets it; a resolved vehicle with no driver set
    // refuses as OPEN rather than guessing nil (see
    // stub_vehicle_get_driver's own doc comment for why an UNRESOLVED
    // vehicle, by contrast, cleanly pushes nil with no OPEN refusal).
    OpenValue<std::string> driverName{"vehicle driver/occupant name (0x00ab5100/0x00ab50c0 + 0x00a353d0)",
                                       "spec-lua-api-behaviour.md Sec12.14"};

    // vehicle_show (Sec12.18, 0x00a645c0): CONFIRMED resolve + unconditional
    // "materialize" call (0x00a87780, position pair + 2 zeros, OPEN) plus an
    // optional second call (0x00a77d70, OPEN) when arg 2 is true, scaled by
    // a fixed constant (0x012a2d90) this document's own text calls "not
    // confirmed" - that scale factor is NOT invented here; the raw arg 3 is
    // stored unscaled, with the request itself (whether arg 2 was true)
    // tracked separately so a test can tell the two cases apart without this
    // project asserting a specific numeric conversion.
    bool shownViaVehicleShow = false;
    bool vehicleShowFadeRequested = false;
    double vehicleShowFadeRawArg = 0.0; // raw Lua arg 3, UNSCALED (see doc comment above)

    // vehicle_freeze (Sec12.26, 0x00a63f20): CONFIRMED resolve + dispatch
    // (0x00abe510, OPEN internal) with the boolean. CHOSEN false default.
    bool frozen = false;

    // vehicle_prevent_explosion_fling (Sec12.22, 0x00a62be0): CONFIRMED
    // resolve + dispatch (0x00a7ab10, OPEN internal) - HIGH CONFIDENCE
    // address-adjacent sibling of the vehicle_set_invulnerable/vehicle_set_
    // vulnerable setters (0x00a7a830/0x00a7a9a0, forceFlags1d7a above), but
    // NOT independently confirmed to share that same byte/bit - kept as its
    // OWN flat field rather than asserting an unconfirmed bit position
    // (same "the write is real, the exact bit is not" convention as
    // unjackableSecondarySubObjectFlag above).
    bool preventExplosionFling = false;

    // get_current_hit_points (Sec12.25, 0x00a4bb00): the VEHICLE resolution
    // path's own data source is a completely different, zero-argument
    // global context accessor (0x00a77e10, OPEN) - NOT the same +0x1cb8
    // field CharacterState::currentHitPoints models for the generic/
    // follower/character-fallback-to-mount paths (CONFIRMED distinct
    // sources per Sec12.25's own text). OPEN until a test sets it.
    OpenValue<double> hitPointsForQuery{"vehicle hit-points query source (0x00a77e10 global context)",
                                        "spec-lua-api-behaviour.md Sec12.25"};

    // =====================================================================
    // Wave 2, Batch W2-3 (2026-10-08), spec-lua-api-behaviour.md Sec20+Sec21
    // - see lua_spec_confirmed_stubs_b16_wave2_sec20_21.cpp for full
    // per-native citations.
    // =====================================================================

    // vehicle_car_alarm_enable (Sec20.3): a per-vehicle-type capability gate
    // (sub-object +0xbf4 own +0x2c field, "alarm disabled by design") - OPEN
    // until a test sets it (no spec default per vehicle type). When the gate
    // is clear (0, "not disabled by design"), sets/clears bit 0x8 of the
    // byte at +0x163d - a NEW byte this header has not previously modelled.
    // CONFIRMED no record/replication of any kind for this one.
    OpenValue<bool> alarmDisabledByDesign{"vehicle sub-object (+0xbf4) own +0x2c 'alarm disabled by design' gate",
                                          "spec-lua-api-behaviour.md Sec20.3"};
    OpenBits32 statusByte163d{"vehicle status byte +0x163d", "spec-lua-api-behaviour.md Sec20.3"};
    static constexpr uint32_t kCarAlarmEnabledBit163d = 0x8u;

    // vehicle_repair (Sec20.2): reuses minHitPointsV/maxHitPointsV/
    // currentHitPointsV above (+0x1560/+0x155c/+0x1564, Sec13.19/Sec13.20)
    // for the actual "heal" step (repair amount = maxHitPointsV * fraction,
    // CONFIRMED). The sub-component-reset/physics-recompute gate (live
    // sub-object at +0x1e00 with its own capability byte, AND flag +0xbd0)
    // has no other in-scope reader - abstracted to ONE flag, OPEN until a
    // test sets it (same "abstract an unconfirmed multi-field gate to one
    // flag" convention used throughout this header).
    OpenValue<bool> repairSubObjectActive{"vehicle +0x1e00 sub-object live AND +0xbd0 flag set (repair gate)",
                                          "spec-lua-api-behaviour.md Sec20.2"};
    double lastRepairRoundedArg = -1.0; // raw arg 3, CONFIRMED sentinel default; no further in-scope effect
    uint64_t repairAppliedCount = 0;

    // vehicle_prevent_vehicle_collision_damage (Sec21.30): bit 0x8 of the
    // SAME +0x1d7a byte forceFlags1d7a above already models (Sec3.7/Sec20.1/
    // Sec46.1's own "invulnerable"/"always-apply-player-damage" bits 0x01/
    // 0x02) - a CONFIRMED distinct bit on that same byte, per Sec21.30's own
    // text ("already used by Sec3.7/Sec20.1 - see the bit conflict flagged
    // there"). Dispatched on the RAW vehicle handle directly (not the
    // +0xc68 AI-directive sub-record vehicle_set_allow_ram_ped below uses) -
    // a genuine, CONFIRMED difference in hidden-`this` shape between the
    // two, per Sec21.30's own text; this project's flat double-gate model
    // does not need a separate field to represent that distinction (the
    // SAME forceFlagGatePasses/forceFlags1d7a fields serve both shapes).
    static constexpr uint32_t kImmuneToVehicleCollisionDamageBit1d7a = 0x8u;

    // vehicle_set_allow_ram_ped (Sec21.10): bit 0x80 of the SAME +0xc68+5
    // byte vehicleAiForceFlags above already models (Sec33.1's own
    // vehicle_set_no_chase bit 0x1) - a CONFIRMED second, distinct bit on
    // that shared byte.
    static constexpr uint32_t kAllowRamPedBit = 0x80u;

    // vehicle_anim_finished (Sec21.12): the real progress computation
    // (0x00a76030, a remaining-time/total-duration fraction via 3 further
    // sub-accessors reached through an ambiguous-shape context object) has
    // no other in-scope reader - abstracted to ONE fraction value, OPEN
    // until a test sets it, plus the two CONFIRMED-distinct gates
    // (+0xbd0==1 "custom animation active", +0xa78 non-null "has anim-state
    // sub-object") this native's own text requires before even computing it.
    OpenValue<bool> customAnimActive{"vehicle +0xbd0 == 1 ('custom animation active')", "spec-lua-api-behaviour.md Sec21.12"};
    OpenValue<bool> hasAnimStateSubObject{"vehicle +0xa78 non-null ('has anim-state sub-object')",
                                          "spec-lua-api-behaviour.md Sec21.12"};
    OpenValue<double> animProgressFraction{"vehicle_anim_finished's own computed progress fraction (0x00a76030)",
                                           "spec-lua-api-behaviour.md Sec21.12"};

    // get_char_vehicle_type (Sec21.24): the real chain (occupant's cached-
    // vehicle-association identity key -> 0x004dcf00 -> vehicle's own +0xbf4
    // non-null gate -> an AI-mode gate 0x00ad2f80 whose result arrives via
    // an IMPLICIT EAX side-channel, per the spec's own hidden-argument
    // finding) has no other in-scope reader past the final 0-5 type code -
    // abstracted to ONE field, holding the ALREADY-CLAMPED 0-5 value
    // directly (the clamp-to-6-on-anything-else step itself IS modeled in
    // the stub, deterministically, since it needs no further OPEN data).
    OpenValue<int32_t> vehicleTypeCode{"get_char_vehicle_type's own resolved vehicle-type code (0x00ad2f80 EAX side-channel)",
                                       "spec-lua-api-behaviour.md Sec21.24"};

    // vehicle_skydiving_move_to_do (Sec21.29): the real dispatch target
    // (0x00ac0f60) was not independently decompiled this pass (OPEN, per
    // the spec's own text) - this project records the raw CONFIRMED request
    // (destination name, both optional numeric arguments, CONFIRMED default
    // -1.0 each via the resolved 0x012a2d54 constant) rather than fabricate
    // any further internal effect, same "record the raw request, not the
    // real internals" convention as several fields elsewhere in this header.
    std::string lastSkydivingMoveToDestination;
    double lastSkydivingMoveToArg3 = -1.0;
    double lastSkydivingMoveToArg4 = -1.0;
    uint64_t skydivingMoveToDispatchCount = 0;

    // =====================================================================
    // Wave 2, Batch W2-4 (Sec22+Sec24+Sec25 gameplay residue, 78 names,
    // 2026-10-08) - see src/lua_spec_confirmed_stubs_b22_wave2_sec22.cpp and
    // src/lua_spec_confirmed_stubs_b23_wave2_sec24_25.cpp's own per-stub doc
    // comments for the full citation/reasoning behind each field below.
    // =====================================================================

    // vehicle_make_corpse (Sec22.2): a genuine vehicle-category-list
    // migration (into a global "corpse" list) - this project has no real
    // linked-list/category-list machinery anywhere (no generic entity shape
    // invented), so only the CONFIRMED structural facts are tracked: whether
    // the vehicle is already a corpse (the real no-op gate), the explode
    // argument, and a call count. CHOSEN default false.
    bool isCorpse = false;
    bool corpseExplodeArg = false;
    uint64_t makeCorpseCount = 0;
    uint64_t makeCorpseAlreadyCorpseNoopCount = 0;

    // teleport_player_vehicles (Sec22.5): no coordinate system anywhere in
    // this project (stated gap), so only the CONFIRMED structural fact -
    // THIS vehicle was relocated to a named reference vehicle's position -
    // is recorded, honestly (same "record the request" convention as
    // HelicopterFlyTo::Request/MinimapIconRecord elsewhere in this header).
    std::string lastTeleportRelocatedToName;
    uint64_t teleportRelocateCount = 0;

    // vehicle_spotlight_cancel (Sec24.5): this project does not model the
    // real dual-singleton/id-pair resolution chain (0x024433a8 vs. the
    // generic character/vehicle resolve) - every resolved name (character OR
    // vehicle) is treated as a vehicle spotlight target directly via the
    // flat objectResolves() map, a stated simplification citing the chain's
    // own OPEN complexity. CHOSEN default false ("no active spotlight").
    bool spotlightActive = false;
    uint64_t spotlightCancelCount = 0;

    // vehicle_set_explosion_damage_multiplier (Sec24.6): double-gate
    // (variant 1, reusing forceFlagGatePasses above) setter, tag pair
    // "vehicle"+"explosion_multiplier" - a NEW confirmed float-payload
    // member of that double-gate family (joining CharacterState::
    // npcSpreadMultiplier1e04's own float-payload sighting, Sec24.26).
    // CHOSEN default 1.0 ("no override" - the engine's own neutral
    // multiplier, same convention as CharacterState::damageMultiplier
    // above, Sec15.1).
    double explosionDamageMultiplier = 1.0;

    // vehicle_in_water (Sec24.7) / vehicle_get_smoke_and_fire_state
    // (Sec24.8): 3 NEW pure-query bits - +0xc8 (in water), +0x16c0 bit 1
    // (smoke active) and bit 7 (fire active). CONFIRMED distinct fields from
    // CharacterState's OWN unrelated +0x16c0 field (Sec24.8's own explicit
    // "coincidental offset match" caution) - CHOSEN default false for all 3
    // (no CONFIRMED construction value given).
    bool inWater = false;
    bool smokeActive = false;
    bool fireActive = false;

    // vehicle_enable_physics (Sec24.10): CONFIRMED to share the EXACT SAME
    // capability byte/bit (+0x33 bit 0x8) as the already-modelled fullySetUp
    // above (Sec33.1's own vehicle_is_ready) - reused directly as this
    // native's own gate rather than a second, duplicate field (Sec24.10's
    // own text: "a further confirmed bit of the same generic capability-
    // flags byte... two now-confirmed distinct bits of this capability
    // byte" - bit 0x8 here, bit 0x10 for vehicle_spotlight_cancel's own
    // DIFFERENT capability check, Sec24.5, which this project does not
    // model per the simplification noted above). Unlike a double-gate
    // refusal, Sec24.10's own text states "absent (or resolve fails) ->
    // pushes false" - so the stub reads fullySetUp via .known()/.get()
    // rather than treating an unset bit as an OPEN refusal. (Reviewer note,
    // not yet reconciled: get_vehicle_type, Sec21.24, has an analogous
    // unset-value-reads-as-0/false shape on its own OPEN field, while two
    // OTHER existing readers of fullySetUp - vehicle_is_ready/the kneecapper
    // setter - throw OPEN on an unset bit instead; whether Sec24.10/Sec21.24
    // genuinely differ from those two or whether this is an inconsistency
    // worth reconciling is flagged for a future pass, not resolved here.)
    bool physicsEnabled = false;
    bool physicsEnabledSecondaryArg = false; // the vtable+0x78 call's own 2nd boolean, role OPEN
    uint64_t physicsEnableDispatchCount = 0;

    // =====================================================================
    // Wave 2, Batch W2-7 (2026-10-08): spec-lua-api-behaviour.md Sec38-60 -
    // see src/lua_spec_confirmed_stubs_b26_wave2_vehicles_helicopter.cpp and
    // src/lua_spec_confirmed_stubs_b27_wave2_spawn_world_audio.cpp for the
    // per-native citations this section's own doc comments abbreviate.
    // =====================================================================

    // vehicle_anim_playing (Sec38.1): gated on bodyActive above (+0xbd0==1,
    // CONFIRMED); the animation-set "active without finishing/paused" test
    // itself is HYPOTHESIS on exact bit meanings per spec - test-settable,
    // OPEN until set (this project has no animation-set data table).
    OpenValue<bool> animSetActiveNotFinishing{"vehicle animation-set 'active, not finishing/paused' (HYPOTHESIS bits)",
                                              "spec-lua-api-behaviour.md Sec38.1"};

    // vehicle_delete_all_corpses (Sec38.1) / vehicle_forced_corpse_removal_
    // enabled's own per-tick consumer (Sec38.1): the wrecked-vehicle list
    // this project has no real world-object-manager walk for - only the
    // per-vehicle CONFIRMED-structure facts the delete-all pass needs are
    // tracked. `corpseClassProtected` stands in for the "per-class
    // 'protected' test" (HIGH CONFIDENCE: script/mission-owned) - CHOSEN
    // false default (an ordinary ambient wreck is not protected).
    bool wrecked = false;
    bool corpseClassProtected = false;
    bool hiddenAsCorpse = false;   // protected branch: hidden instead of deleted
    bool deletedAsCorpse = false;  // unprotected branch: deleted outright

    // vehicle_engine_start (Sec38.1): "an already-running engine is
    // stopped first, then restarted - never left alone." CHOSEN false
    // initial (no CONFIRMED spawn-time default stated).
    bool engineRunning = false;
    uint64_t engineStartVtolAudioEventCount = 0; // class 4 (VTOL) + active sub-object, per spec text

    // vehicle_set_kneecappers_damage (Sec46.5): a DIFFERENT field from
    // kneecappersEnabled/kneecappersDeferred above (Sec33.1's own
    // `vehicle_set_kneecappers` - deliberately NOT conflated, per this
    // batch's own transposition-risk note). "Direct, unreplicated field
    // write with no ownership gate" - CHOSEN 0.0 initial (no unit/range
    // given beyond "a value").
    double kneecappersDamage = 0.0;

    // vehicle_set_npc_engine_audio (Sec46.5): "direct, unreplicated field
    // write with no ownership gate" - thin spec beyond that categorization;
    // CHOSEN true initial (engine audio normally on).
    bool npcEngineAudioEnabled = true;

    // vehicle_set_sirens (Sec46.5 cluster, no individual text - NOT the
    // same bit as the already-registered vehicle_set_sirenlights/Sec33.1
    // siren-LIGHTS bit on forceFlags1d7c; kept as its own field
    // deliberately, see this native's own stub doc comment for the full
    // transposition-risk note). CHOSEN false initial (a siren is off by
    // default).
    bool sirensOn = false;

    // get_vehicle_speed (Sec41.6): "0 if nothing resolves"; when resolved,
    // the real velocity-magnitude read (units OPEN) needs physics this
    // project does not have - OPEN until a test sets it (honest refusal on
    // a real resolved-but-unmodelled-speed call, same "no coordinate/
    // physics system" gap as WorldPosition elsewhere).
    OpenValue<int32_t> speedMph{"vehicle speed query (0x00a4c440 velocity magnitude, units OPEN)",
                                "spec-lua-api-behaviour.md Sec41.6"};

    // helicopter_set_dont_use_constraints (Sec41.5): "no helicopter-kind
    // check anywhere - accepts any vehicle." The "second specific bit"
    // gating the allocated-node rebuild is OPEN (HYPOTHESIS: rotor-
    // constraint data) - test-settable, counted only when true (this
    // project has no allocated rotor-constraint-chain model).
    bool dontUseConstraints = false;
    OpenValue<bool> constraintRebuildBit{"helicopter constraint-rebuild gate (second specific bit, OPEN)",
                                        "spec-lua-api-behaviour.md Sec41.5"};
    uint64_t constraintRebuildCount = 0;

    // vehicle_in_air / vehicle_is_flipped (Sec52.2): "resolved in full, no
    // further crash shapes" - no coordinate/physics system in this project
    // to derive either from real geometry, so both are OPEN until a test
    // sets them (same convention as speedMph above).
    OpenValue<bool> inAir{"vehicle in-air state (Sec52.2, no physics model in this project)",
                         "spec-lua-api-behaviour.md Sec52.2"};
    OpenValue<bool> flipped{"vehicle flipped state (Sec52.2, no physics model in this project)",
                           "spec-lua-api-behaviour.md Sec52.2"};

    // vehicle_get_max_speed (Sec52.2): "returns miles per hour... and -1
    // for an unresolved vehicle." "Which writer sets vehicle +0x1d84" is
    // itself a residual OPEN item per spec's own closing list - OPEN until
    // a test sets it.
    OpenValue<double> maxSpeedMph{"vehicle max-speed field +0x1d84 (writer OPEN per spec)",
                                 "spec-lua-api-behaviour.md Sec52.2"};

    // vehicle_allow_ownership_transfer (Sec52.2): "resolved in full, no
    // further crash shapes" - thin beyond that; CHOSEN true initial
    // (ownership transfer normally allowed).
    bool ownershipTransferAllowed = true;

    // vehicle_evacuate (Sec52.2): "discarding its own occupant-liveness
    // check result (dead occupants are evacuated/destroyed exactly like
    // live ones - consequence HYPOTHESIS: benign)" - this project has no
    // real occupant list, so only the gated call itself is counted.
    uint64_t evacuateCount = 0;

    // vehicle_flee (Sec52.2): "drives AI mode 2 (flee)... a player-driven
    // vehicle never flees; an AI mode/distance veto [confirmed permanently
    // dead - not modelled, per spec's own exhaustive-reference-check
    // finding]." playerDriven is test-settable, OPEN (no driver/seat model
    // in this project) - defaults via the honest OpenStateError refusal on
    // a real, un-set-up call, same convention as the other OPEN flags above.
    OpenValue<bool> playerDriven{"vehicle currently player-driven (Sec52.2 veto input)",
                                "spec-lua-api-behaviour.md Sec52.2"};
    bool fleeing = false;

    // vehicle_component_is_shattered / vehicle_component_shatter_glass
    // (Sec52.2): per-component shattered-state map, keyed by component
    // name (case preserved as given - the real native's own `_stricmp`
    // case-insensitivity is not reproduced here, a stated simplification:
    // this project has no per-vehicle component-name table to normalize
    // against). HARDENED (host-safety): a nil/non-string component name
    // would reach the CRT `_stricmp` with a null pointer in the real
    // engine (Sec52.2, "fatal under the stock invalid-parameter handler")
    // - this host's stub refuses (no-op / false) instead and counts it
    // (see stub's own doc comment for the exact guard).
    std::unordered_map<std::string, bool> componentShattered;
    uint64_t componentNullNameGuardCount = 0;

    // vault_heli_hack_for_m01 (Sec52.2): "a narrow, mission-1-specific
    // component-index cache setter" - no further detail; CHOSEN -1 initial
    // ("never cached").
    int32_t vaultHeliComponentIndexCache = -1;

    // vtol_flyby_animation_for_m18 (Sec46.4): "a one-off mission-18 helper
    // (teleport + animation + an unconditionally broadcast network record
    // with no host or ownership gate)" - CHOSEN false initial (never
    // played), test/call-count observable only (no teleport/animation
    // system modelled beyond the gated call itself).
    bool vtolFlybyM18Played = false;

    // world_despawn_all_vehicles (Sec46.3): "walks dedicated ambient/
    // traffic lists and despawns everything not protected (occupied by a
    // player, flagged, etc.), deferring actual deletion through the
    // engine's normal pending-delete queue rather than freeing
    // synchronously." This project models "deferred" as "marked, not
    // removed from the map" - CHOSEN false defaults (an ordinary ambient
    // vehicle is neither player-occupied nor otherwise protected).
    bool despawnPlayerOccupied = false;
    bool despawnProtected = false;
    bool despawnedPendingDelete = false;

    // helicopter_land / helicopter_enter_dropoff / helicopter_set_large_
    // dspiral / helicopter_set_dont_move_in_combat / helicopter_set_dont_
    // explode_in_air / helicopter_clear_max_bank_angle (Sec53.2): "accept
    // any resolved vehicle, not just a helicopter - none of them actually
    // checks the vehicle's AI mode before writing its flag bit." CHOSEN
    // false initial for all four flag setters (normal/undisturbed flight).
    // helicopter_clear_max_bank_angle only clears the override-enabled bit
    // below and explicitly LEAVES maxBankAngleRadians (above, Sec13.23)
    // untouched - CONFIRMED, modelled as its own separate bool rather than
    // zeroing the angle.
    bool largeDspiral = false;
    bool dontMoveInCombat = false;
    bool dontExplodeInAir = false;
    bool maxBankAngleOverrideEnabled = false; // cleared by helicopter_clear_max_bank_angle; angle value untouched
    // helicopter_land (Sec53.2): "returns its success boolean after only
    // the cheap preconditions pass - the actual landing request can still
    // be silently dropped (target farther than 200 units away)." No
    // coordinate system in this project to compute a real distance - the
    // 200-unit distance gate is test-settable (OpenValue<bool>, OPEN until
    // set); the Lua-visible return value still reflects only the cheap
    // precondition (vehicle resolves), matching spec's own "optimistic
    // true" shape - the distance gate only affects whether a LANDED state
    // actually gets set, honestly left OPEN rather than simulated.
    OpenValue<bool> landDistanceGatePasses{"helicopter_land's own <=200-unit distance gate (no coordinate system here)",
                                           "spec-lua-api-behaviour.md Sec53.2"};
    bool landed = false; // only set true when landDistanceGatePasses is known and true
    // helicopter_enter_dropoff (Sec53.2): "fewer than 2 live occupants
    // [vetoes it] unless explicitly overridden." No occupant model in this
    // project - test-settable, OPEN until set (honest refusal), same
    // shape as landDistanceGatePasses above.
    OpenValue<bool> dropoffOccupancyGatePasses{"helicopter_enter_dropoff's own >=2-occupant gate (no occupant model here)",
                                               "spec-lua-api-behaviour.md Sec53.2"};
    bool dropoffRequested = false; // only set true when dropoffOccupancyGatePasses is known and true (or overridden)

    // =====================================================================
    // Wave 2, Batch W2-2 (spec-lua-api-behaviour.md Sec17+Sec18, 2026-10-08)
    // - see src/lua_spec_confirmed_stubs_b21_wave2_sec17_18.cpp's own doc
    // comment above each stub_* function for the full citation/reasoning.
    // =====================================================================

    // vehicle_set_script_hard_goto (Sec17.1): a further confirmed bit on
    // the EXISTING vehicleAiForceFlags byte (+0xc68)+5 above (bit 0x1,
    // vehicle_set_no_chase, Sec33.1).
    static constexpr uint32_t kScriptHardGotoBit = 0x4u;

    // vehicle_set_crazy (Sec17.2): CONFIRMED a genuinely DIFFERENT, simpler
    // dispatch (plain cdecl call, no shared +0xc68 sub-record, no double-
    // gate/record idiom reached within the spec's own one-level decompile
    // budget) - a caution against assuming uniformity with the visually
    // similar §17.1/§17.17. OPEN whether a gate/record exists at all beyond
    // this call depth, so only the direct write is modeled (no
    // EngineState::replicateStateChange call - inventing one would assert a
    // fact the spec explicitly does not confirm).
    bool crazyEnabled = false;

    // vehicle_prevent_transition_to_ambient (Sec17.17): a SECOND confirmed
    // bit-offset within the vehicle +0xc68 AI-directive sub-record, a
    // DIFFERENT byte (+7) from vehicleAiForceFlags (+5) and
    // vehicleAiForceFlagsByte4 (+4) above.
    OpenBits32 vehicleAiForceFlagsByte7{"vehicle-AI force-flags byte (+0xc68)+7",
                                        "spec-lua-api-behaviour.md Sec17.17"};
    static constexpr uint32_t kNeverGoAmbientBit = 0x4u;

    // vehicle_set_use_short_cuts (Sec4.5a, closed 2026-10-09 wave 10): bit
    // 0x20 of sub-record byte +0x6 (absolute vehicle +0xc6e) - a byte not
    // previously catalogued anywhere in this +0xc68 family (existing
    // confirmed bytes were +0x4/+0x5/+0x7), so its own field, not folded
    // into vehicleAiForceFlagsByte4/vehicleAiForceFlags/vehicleAiForceFlagsByte7.
    OpenBits32 vehicleAiForceFlagsByte6{"vehicle-AI force-flags byte (+0xc68)+6",
                                        "spec-lua-api-behaviour.md Sec4.5a"};
    static constexpr uint32_t kUseShortCutsBit = 0x20u;

    // vehicle_set_ignore_rail_obstacles (Sec18.21): a further confirmed bit
    // on the EXISTING vehicleAiForceFlagsByte4 byte (+0xc68)+4 above (bits
    // 0x40/0x20 together, vehicle_suppress_npc_exit, Sec4.5; bit 0x2,
    // vehicle_ignore_repulsors, Sec13.8) - raw-disassembly-confirmed
    // computed-sub-record hidden-`this`, matching vehicle_set_script_hard_
    // goto's own already-confirmed pattern.
    static constexpr uint32_t kIgnoreRailObstaclesBit = 0x4u;

    // vehicle_set_team (Sec18.20): object +0x16e0, a bare unconditional
    // local store - no gate, no record, a real asymmetry against most of
    // this batch's setters. CHOSEN -1 = "never set".
    int32_t teamId = -1;
};

} // namespace sr3luahost
