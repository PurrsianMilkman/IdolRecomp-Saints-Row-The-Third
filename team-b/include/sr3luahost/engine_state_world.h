// EngineState slice: zones, world objects, scripted movement, lights/effects, world toggles.
//
// Split out of engine_state.h (refactor step 3a, 2026-10-17) as a zero-behaviour-change move: every
// type, field, initialiser and comment below moved verbatim (namespace-scope types re-indented one
// level). Types that were nested in EngineState are now namespace-scope types in sr3luahost;
// EngineStateWorldData re-exports each one under its old name, so `EngineState::Type` still
// resolves. EngineState inherits EngineStateWorldData publicly, so `state.member`,
// `EngineState::kConstant` and `state.accessor()` are unchanged for every caller. Members that
// were private in EngineState are `protected` here (the only access change). Method
// declarations stay in EngineState (engine_state.h); bodies are in src/*.cpp.
#pragma once

#include "sr3luahost/engine_state_common.h"

namespace sr3luahost {

// --- Mission-host Phase 2: live position bridge + name->live-object
// resolver (results/design_mission_host_in_engine_loop_20261009.md
// §3(c), "a bridge it does not have today... a new, currently-
// nonexistent name-to-live-object resolver") ----------------------------
//
// UPDATED 2026-10-09, post-merge of Team A's spec-zone-data-format.md
// §11.10 ("Named-object resolution, end to end") + spec-lua-api-
// behaviour.md §4.13's corrected resolver-family paragraph: the REAL
// engine has exactly ONE global name->handle map (registry singleton
// `0x02442750`, `+0x2660`), and - unlike this project's own objectResolves()
// below - that map is itself inherently LIVE: an entry exists in it if
// and only if some real producer (zone placement, vehicle cover-node
// batches, an in-memory engine record including a group member, or
// co-op-replication replay - §11.10 item 2's own closed 4-producer
// list) has actually registered a live handle for that name via
// `0x00457650`, called from registry base-vtable `+0x2c`. Resolving a
// name in the real engine and finding it LIVE are therefore the SAME
// event, not two separate facts the way this project's headless-testing
// objectResolves() treats them (a flat, hand-seeded boolean that answers
// the SAME way whether this host is running headless or hosting a real
// runPlaytest() session, because nothing ever populated it FROM a live
// session in the first place). This resolver is this project's first
// attempt at modelling that real "resolves because it's live" fact
// directly, rather than the pre-existing hand-seeded stand-in.
//
// §11.10 item 3 / §4.13's own corrected resolver-family paragraph also
// confirms a SECOND, separate step this resolver deliberately does NOT
// model: once a name resolves to a handle, a per-native "kind" bit-test
// against that handle's own row in the real class-descriptor table
// `0x02cc9900` (5 confirmed resolver-family members, each testing one
// bit/offset) decides whether THIS PARTICULAR native accepts that kind
// of object. §4.13's own text is explicit that even Team A does not yet
// know the plain-English meaning of any of those bits, and 2 of the
// family's own bit/offsets are still unpinned - so there is no real
// mechanism here for Team B to faithfully reproduce yet. This resolver's
// own kCharacter/kVehicle split below is NOT an attempt at that - it
// simply mirrors which of this project's two PRE-EXISTING, Phase-2-
// independent storage maps (characters_/vehicles_ above) a caller wrote
// a live position into, the same character-vs-vehicle split
// get_dist's own doc comment already admits this project has no real
// way to distinguish from a bare name alone. Treat kCharacter/kVehicle
// as "which existing map this was written through," never as a claim
// about a resolved handle's real `0x02cc9900` class row.
//
// §11.10 item 4 independently strengthens, rather than changes, this
// resolver's one real production use (the bridge call for "#PLAYER1#"
// in tools/sr3_viewer.cpp's own runPlaytest()): `"#PLAYER1#"` is one of
// exactly 8 closed, special-cased sentinels that the REAL generic
// resolver (`0x00a280c0`) short-circuits via a literal-string compare,
// resolving straight to "the current player object" - it never touches
// the name map at all. So in the real engine "#PLAYER1#" is ALWAYS
// resolvable whenever a local player exists, by construction - exactly
// the one case this project's own minimal playtest can actually offer a
// live value for, and exactly why it is the one name bridged today (see
// that call site's own doc comment for the full "no other name is
// bridged" reasoning, unchanged by this spec update).
//
// CHOSEN shape (unchanged by the above - this just grounds it): a plain
// name->kind map, written ONLY by the two bridge*Position() setters
// below (never by a mission script, and never defaulted true for any
// name) - so "is X live" can only become anything but kNone because a
// real per-frame caller (runPlaytest()) actually wrote a real position
// for it THIS session. A name this map has never seen is kNone ("not
// live") - a normal, honest, reportable result, not an error and not
// the same fact as objectResolves() being false (a name can be a real
// resolved mission-fiction object per THIS project's own hand-seeded
// stand-in and still not be live in a minimal playtest that only ever
// tracks the local player - concrete proof: applySpecInitialState()
// pre-seeds BOTH "#PLAYER1#" and "#PLAYER2#" true in objectResolves(),
// Sec29.4, but this project's single-player-only scope means
// "#PLAYER2#" is NEVER live in any session this project can construct).
enum class LiveObjectKind { kNone, kCharacter, kVehicle };

// --- generic get_dist "same stall point" tracker (feature/auto-objective-
// harness, 2026-10-12) --------------------------------------------------
// The m07 spawn-at-barge diagnostic (results/verify_m07_spawn_at_barge_
// 20261012/) manually confirmed that a navigation-gated mission re-issues
// the SAME get_dist(a,b) call every scheduler resume while its own wait
// loop sits unsatisfied (340 identical calls observed for m07), and that
// the non-player name's own tryResolveWorldPositionFromZoneData() position
// IS the real target a scripted player needs to reach. This generalizes
// that observation into reusable state stub_get_dist (src/lua_spec_
// confirmed_stubs_b05_invuln_r15_23.cpp) records on EVERY call, zero
// behavior change to get_dist's own return value - --auto-objective
// (tools/sr3_viewer.cpp) is the only reader. Not get_dist-specific in
// spirit (any "same two names, N frames running" check would work), but
// get_dist is the ONLY position-shaped native this project's missions are
// confirmed (m07, live) to stall on, so it's the only one wired here -
// not inventing a GPS-waypoint or objective-marker tracker this project
// has no CONFIRMED source for (engine_state.h's own waypoint_add/_remove
// comments: "this project has no real waypoint/HUD-marker tracking").
struct GetDistStallInfo {
    std::string nameA;
    std::string nameB;
    WorldPosition posA;
    WorldPosition posB;
    bool knownA = false;
    bool knownB = false;
    uint64_t repeatCount = 0; // consecutive calls with the SAME unordered {a,b} pair, including this one
};

// Sec28.9 `effect_play_finisher`.
struct EffectFinisher {
    OpenValue<bool> gamepadMode{"gamepad flag 0x0141250d", "spec-lua-api-behaviour.md Sec28.9"};
    OpenValueMap<bool> effectIndexValid{"0x005c50b0 icon effect index != -1, keyed by icon name",
                                        "spec-lua-api-behaviour.md Sec28.9"};
    OpenValue<double> successReturn{"0x00a48af0's own return value (Sec9.1/Sec12.10/Sec12.12 family, not modelled further)",
                                    "spec-lua-api-behaviour.md Sec28.9"};
};

// Sec28.16 `continuous_explosion_start`: reconciled with Sec22.24 `continuous_explosion_stop`, one global
// continuous-explosion state.
struct ContinuousExplosion {
    OpenValueMap<bool> definitionResolves{
        "continuous-explosion definition row resolves (0x005eb390, NOT the 0x02442750 singleton family)",
        "spec-lua-api-behaviour.md Sec28.16"};
    // Sec28.16 port pointer (2026-10-12, FINAL): the rows are exactly continuous_explosions.xtbl's;
    // a name matching no row is a silent no-op. Once the real table's names are installed it is COMPLETE.
    bool tableComplete = false;
    void installNames(const std::vector<std::string>& names) {
        for (const std::string& n : names) definitionResolves.set(n, true);
        tableComplete = true;
    }
    bool resolve(const std::string& name) const {
        if (tableComplete && !definitionResolves.known(name)) return false;
        return definitionResolves.get(name);
    }
    OpenValue<bool> active{"0x012ec964 (continuous explosion global state)", "spec-lua-api-behaviour.md Sec28.16"};
    std::string lastDefName, lastTargetName; // CHOSEN, test-observable
    uint64_t startCount = 0;

    // continuous_explosion_stop (Sec22.24, wave 2 batch W2-4): CONFIRMED
    // (reconciled above, 2026-10-01) the SAME one global running flag
    // `active` already models - this native just clears it
    // unconditionally, regardless of any argument (genuinely zero-
    // argument-sensitive per Sec22.24's own text). No new field needed
    // beyond a call count.
    uint64_t stopCount = 0;

    // continuous_explosion_follow_target_add (Sec25.23, wave 2 batch
    // W2-4) - CORRECTED by Sec57's own tranche-23 pass (the liveness
    // check IS made, inside the resolver itself; `+8/+0xc` is a handle
    // pair, not a position). Appends a resolved object's handle (this
    // project's own flat objectResolves()-backed name stands in for the
    // real handle) to a follow-target list capped at 2 entries, ONLY
    // while `active` above is true - a third add silently drops, no
    // duplicate check (both CONFIRMED, Sec57.6/Sec25.23). CHOSEN record
    // shape, kept deliberately SIMPLE (a plain vector of names) so the
    // sibling wave-2 batch that owns continuous_explosion_follow_
    // target_remove (out of THIS batch's scope - different wave-2
    // batch) can extend it later (erase-by-name) without redesigning
    // this list's own shape; this batch does not invent that sibling's
    // own removal semantics.
    static constexpr size_t kFollowTargetCap = 2;
    std::vector<std::string> followTargets;
    uint64_t followTargetAddCount = 0;         // the add actually applied (active + under cap)
    uint64_t followTargetAddDroppedCount = 0;  // inactive OR already at the 2-entry cap
};

// The game clock (0x014ff338), Sec48 - corrects Sec32.1. hour/minute
// (0x014ff33c/0x014ff33d) are now CONFIRMED known from construction, not
// OPEN: Sec48.2's own resolved correction establishes that a fresh
// single-player game (no save loaded, no co-op session - this host
// never models either) always starts at 10:00:00 via the new-game
// routine, since the world-start random-key snap (the only other
// source) is CONFIRMED to never fire without a session object. This is
// what actually closed the dlc3_m01/m19/etc. blocker: those missions
// call set_time_of_day, whose own forward-delta arithmetic (below) reads
// the CURRENT hour/minute first - previously OPEN (no initial value),
// now a real read.
//
// set_time_of_day's real effect (Sec48.4, corrects Sec32.1's own "the
// clock now reads the requested time" assumption): the engine
// unconditionally snaps the clock to the NEAREST time-of-day KEY of the
// active definition after the forward jump - usually NOT leaving
// hour/minute equal to what was requested. The key list is loaded data
// this project does not have (OPEN, Sec48.5's own "Residual OPEN
// items"), so the real post-snap value cannot be computed, only that it
// changed. Modeled honestly: setTimeOfDay() computes and records the
// CONFIRMED forward delta (lastAdvanceSeconds, test-observable), then
// forgets hour/minute rather than fabricating the snapped result - a
// second set_time_of_day call (or a read inside this project's own
// per-frame advance below) correctly sees OPEN again past that point,
// the honest consequence of genuinely not knowing where the snap landed.
//
// Per-frame advance (Sec48.3, CONFIRMED mechanism): advanceFrame()
// advances by realSeconds * kTimeScale game-seconds, CHOSEN to be called
// once per this host's own modelled tick (tools/lua_host_run.cpp passes
// its own per-tick frame-time constant, matching get_frame_time's own
// per-resume value - see that call site's own doc comment). A no-op
// while hour/minute are unknown (advancing an unknown base is still
// unknown, not a value to fabricate). STATED SIMPLIFICATION: wraps
// within one 86400-second day (hour/minute/second only) and does not
// advance date/weekday/moon-day - nothing in scope reads those fields
// (grepped directly: only hour/minute are ever read via Lua), and no
// budget used by this project crosses a full in-game day (40x scale
// needs ~36 real minutes per in-game day; the longest run so far is a
// few real seconds). Revisit if a future budget or spec need ever
// requires the date fields.
struct GameClock {
    OpenValue<int32_t> hour{"game clock hour byte (0x014ff33c)", "spec-lua-api-behaviour.md Sec32.1/Sec48.2/Sec48.4"};
    OpenValue<int32_t> minute{"game clock minute byte (0x014ff33d)", "spec-lua-api-behaviour.md Sec32.1/Sec48.2/Sec48.4"};
    int32_t second = 0; // CONFIRMED 0 at new-game (Sec48.2); not independently exposed to Lua in scope
    static constexpr double kTimeScale = 40.0; // CONFIRMED (Sec48.1/Sec48.3), game-seconds per real second
    int64_t lastAdvanceSeconds = 0;  // CONFIRMED computed total delta (forced non-negative via the hour-wrap rule), seconds
    uint64_t advanceRequestCount = 0;
    double frameAdvanceSecondsAccumulated = 0.0; // test-observable: total real seconds fed to advanceFrame()
};

// --- OPEN-slot inventory (manager prep 2026-10-01) -----------------------

// One row per engine-state slot the stubs read, grouped by area (co-op,
// tutorial, vehicle-store, zscene, fade, vint, other). Rows stay listed
// after a spec answer fills them (known / knownKeys then say so).
// `known` is "value set" for a scalar, "every bit known" for a bit word;
// `knownKeys` counts set keys for a per-name map (scalars: 0/1). lua_host_run
// writes it as verdict_open_state.tsv, so a re-run shows which slots a
// newly implemented answer filled.
struct OpenSlotStatus {
    std::string area;
    std::string global;
    std::string spec;
    std::string kind; // "value", "per-name map" or "bit word"
    bool known = false;
    size_t knownKeys = 0;
};

// debris_flow_set_active (Sec14.20): record-only (no coordinate system
// anywhere in this project, same stated gap WorldPosition's own doc
// comment establishes) - only the real, CONFIRMED-structure parts are
// tracked: whether the relative-to location resolved, the 3-slot fixed
// global table lookup (size CONFIRMED, contents not), and the host-
// check gate (reuses EngineState::coopLocalIsHost() directly, the SAME
// real singleton, Sec8.27/Sec6.22/Sec10.9).
struct DebrisFlowState {
    std::array<bool, 3> slotActive{}; // the 3-slot fixed global table, 0x014fde60
    uint64_t activateCount = 0;
    uint64_t relativeLocationAppliedCount = 0;

    // debris_flow_create (Sec25.21) / debris_flow_destroy (Sec25.20) /
    // debris_flow_add_object (Sec22.21) / debris_flow_add_avoid_object
    // (Sec25.22), wave 2 batch W2-4: CONFIRMED (Sec22.21's own text) the
    // "flow id" debris_flow_add_object walks is the SAME small fixed
    // ~3-entry table (0x014fde60) debris_flow_set_active's own `index`
    // argument already indexes above (slotActive) - this project reuses
    // the SAME 3-slot space rather than inventing a second, disjoint id
    // space. A slot is "allocated" by debris_flow_create and freed by
    // debris_flow_destroy; this is INDEPENDENT of slotActive above
    // (debris_flow_set_active's own, unrelated "active" flag on the same
    // table) - the two features share a table, not a meaning.
    //
    // DESIGN NOTE for the sibling wave-2 batch that owns debris_flow_
    // set_inactive (a REAL memory-safety finding there - a null virtual
    // call on a stale handle, out of THIS batch's scope): `allocated`/
    // `destroyed` are kept as SEPARATE booleans, deliberately NOT folded
    // into one tri-state enum, specifically so that sibling can add its
    // own `inactive` bool alongside them without redesigning this record
    // - do not assume `allocated && !destroyed` implies "safe to use
    // every way" beyond what THIS batch's own natives state.
    struct FlowInstance {
        bool allocated = false;
        bool destroyed = false;
        std::vector<std::string> objects;      // debris_flow_add_object's own resolved names, call order
        std::vector<std::string> avoidObjects;  // debris_flow_add_avoid_object's own resolved names, call order
    };
    static constexpr size_t kFlowSlotCount = 3;
    std::array<FlowInstance, kFlowSlotCount> flows{};
    uint64_t createCount = 0;
    uint64_t createRecycledCount = 0; // CHOSEN (hardened divergence): all 3 slots already live -> reuses slot 0
    uint64_t destroyCount = 0;
    uint64_t destroySkippedCount = 0;       // out-of-range or already-not-allocated id (HARDENING POLICY)
    uint64_t addObjectCount = 0;
    uint64_t addObjectNoMatchCount = 0;     // id does not match an allocated slot -> real "no apply" branch (Sec22.21)
    uint64_t addAvoidObjectCount = 0;
    uint64_t addAvoidObjectSkippedCount = 0; // out-of-range id (HARDENING POLICY - this project always bounds its own array access)
};

// wind_override_set (Sec22.30) / wind_override_clear (Sec24.3): CONFIRMED
// exact math (byte-confirmed constants) - this project CAN and does
// reproduce the real normalize algorithm precisely, since it is pure
// 3-float arithmetic with no world-coordinate dependency. Default (1,0,0)/
// 0.0 matches the real degenerate-input fallback (Sec22.30's own byte-
// confirmed X-axis default). `actionStopSyncedStateFlag` is the SAME
// replication-mode flag (0x024d4461) both natives' own text cites, shared
// with the not-yet-implemented action_stop_synced_state (Sec18.4) -
// CHOSEN default false (no CONFIRMED construction value).
struct WindOverride {
    double x = 1.0, y = 0.0, z = 0.0;
    double magnitude = 0.0;
    uint64_t setCount = 0;
    uint64_t clearCount = 0;
    uint64_t setNotifyCount = 0;
    uint64_t clearNotifyCount = 0;
};

// --- mesh_mover_apply_impulse / _ready / _set_damaged_by_player_only
// (Sec41.1) / mesh_mover_reset_by_zone (Sec50.5) -----------------------
struct MeshMoverRecord {
    bool damagedByPlayerOnly = false; // CONFIRMED bit this setter sets/clears; reader OPEN per spec
};

// light_group_set_intensity (Sec17.24): no real per-kind resolver table
// in this project (same "no fixed id table" class of gap) - collapsed
// to one flat resolves-or-not predicate, test-registerable, plus a
// per-group record of the members a light group "owns" (walked and
// individually re-applied on every intensity change, CONFIRMED).
struct LightGroup {
    double intensity = 0.0;
    bool flagBit2Set = false;
    std::vector<std::string> memberLightNames;
    uint64_t applyCount = 0;
    uint64_t replicateCount = 0; // coordinated-co-op-session-gated record, Sec17.24
};

struct EngineStateWorldData {
    // Former EngineState nested types (defined above at namespace scope), re-exported so
    // `EngineState::Type` keeps naming them.
    using LiveObjectKind = sr3luahost::LiveObjectKind;
    using GetDistStallInfo = sr3luahost::GetDistStallInfo;
    using EffectFinisher = sr3luahost::EffectFinisher;
    using ContinuousExplosion = sr3luahost::ContinuousExplosion;
    using GameClock = sr3luahost::GameClock;
    using OpenSlotStatus = sr3luahost::OpenSlotStatus;
    using DebrisFlowState = sr3luahost::DebrisFlowState;
    using WindOverride = sr3luahost::WindOverride;
    using MeshMoverRecord = sr3luahost::MeshMoverRecord;
    using LightGroup = sr3luahost::LightGroup;

public:
    // Sec21.10.1 type-0 duration is 30000 ms x the difficulty row's Homie_Revive_Timer_Mult (row +0x14); that row is not
    // wired into this host, so the scale is 1.0 until a host sets it (CHOSEN default). Game mode 5 (15000 ms) and the m24
    // no-timer followers are not modelled (OPEN).
    double& downedTimerScale() { return downedTimerScale_; }
    static constexpr double kScriptedMoveRunMetresPerSecond = 5.0;   // CHOSEN
    static constexpr double kScriptedMoveWalkMetresPerSecond = 1.5;  // CHOSEN
    double syncedActionNowSeconds() { return gameClock_.frameAdvanceSecondsAccumulated; }
    // Test-only: resets the resolver's own "is it live" answer for every
    // name (e.g. to model "before the live loop's first frame ever ran")
    // WITHOUT touching any worldPosition already written - matches this
    // project's existing OpenValue "sticky until a new .set()/.forget()"
    // convention elsewhere; this map is a separate liveness fact layered on
    // top of that storage, not a replacement for it.
    void clearLiveObjectsForTesting() { liveObjectKinds_.clear(); }

    // --- teleport_check_done / turn_to_check_done / move_to_check_done /
    // vehicle_pathfind_check_done (spec-lua-api-behaviour.md Sec35, Sec15.17,
    // Sec22.15, Sec9.10; batch 2026-10-02, the `teleport_coop` investigation -
    // Sec35's own "why" note: this is the real blocker for 3 missions,
    // dlc2_m01/m13/m19, parked forever inside `teleport_coop`, a game_lib.lua
    // script helper, NOT a native). ---------------------------------------
    //
    // The real engine's own shared "scripted request" completion pool
    // (Sec35.2): 200 records, each keyed by (object handle, kind) - kind 0 =
    // turn-to, kind 2 = move-to/pathfind (shared by BOTH move_to_check_done
    // AND vehicle_pathfind_check_done - Sec35.2/Sec22.15's own text), kind 4 =
    // teleport. Status query CONFIRMED (Sec35.2, re-derived from the raw
    // instruction stream, correcting an earlier inverted decompile-based
    // reading): 0 = a request exists and is still pending; 1 = a request
    // exists and is done (released back to the free list as a side effect of
    // THIS very read, matching the real engine's own release-on-read
    // behavior); 2 = no matching request at all (never issued, already
    // consumed, superseded, or an unresolved name). This project has no real
    // handle-resolution mechanism (this header's own top note) and models no
    // request-ALLOCATING native in scope - `teleport`, `turn_to`, `move_to`,
    // and `vehicle_pathfind_navmesh_do` all remain generic logging stubs, out
    // of this task's own scope - so the pool is keyed by the plain Lua name
    // string directly (same convention as CharacterState) and is populated
    // ONLY via registerScriptedRequestForTesting(), never by any in-scope Lua
    // call. This is not a guess: per Sec35.6's own explicitly-endorsed
    // "Minimum correct model," a host that models no asynchronous request at
    // all correctly answers "done" (status 2, no request) for every real
    // call - the honest, CONFIRMED answer for an unmodeled allocator, not a
    // simplification of the query's own CONFIRMED mechanism/polarity.
    static constexpr int kScriptedRequestKindTurnTo = 0;          // Sec35.2
    static constexpr int kScriptedRequestKindTeleport = 4;        // Sec35.2

    // --- name resolution the host does not model (cloud phase 2026-09-30) --

    // Whether a name resolves to a live engine object through the resolver
    // the calling function uses (ai_add_enemy_target Sec3.9: "on successful
    // resolution of both names"; object_indicator_add_do Sec10.6). One map
    // for all resolvers - a stated simplification; OPEN until set.
    //
    // Sec29 (2026-10-02) documents the real write side this stands in for:
    // every per-kind resolver this document catalogues (Sec1.1/Sec25.29/
    // Sec28 and others) is a thin kind-filter over ONE shared global name
    // map (singleton 0x02442750's embedded sub-object at 0x02444db0), a
    // case-insensitive multiply-by-33 (0x00dab330) bucket hash, CONFIRMED
    // as a single instance of 2,999 slots (0xBB7) enabled exactly once at
    // start-up (Sec29.3) - not per-zone, not per-kind, so a name registered
    // by any producer below is resolvable by every resolver here, with no
    // per-producer distinction visible at lookup time. This map is keyed by
    // exact std::string equality (unlike the real _stricmp/hash chain) -
    // a simplification, not yet modelled as case-insensitive (Sec29.3).
    //
    // Sec29.2: FOUR producers feed names into that one map: (1) zone object
    // placement's 0x2234 "Game objects, as placed in the WE" record -
    // PARKED, lives inside the held .czn_pc object-placement interior, not
    // lifted, not read here; (2) a vehicle definition's embedded cover-node
    // batch (same loader); (3) "the bulk of the 173 call sites" - in-memory
    // records built directly by engine code (spawners, path/navpoint
    // builders, group objects), fed to 0x00456e30 - NOT individually traced
    // (Sec29.2's own words), so it could register essentially any name a
    // resolver might query; (4) a stream deserialiser (0x00a36a80),
    // HYPOTHESIS co-op replication, not Lua-reachable, replays a name that
    // already existed rather than originating one.
    //
    // Sec29.4: no Lua-bound function can ever CHOOSE a name for an object
    // it creates (unnamed, or engine-named with a generated/uniquified
    // suffix). Engine code separately registers a small CLOSED set of
    // literal names regardless of any zone/data file - "homies",
    // "shopkeepers", "-- Cutscene Script Group --", and the special-cased
    // "#PLAYER1#"/"#PLAYER2#" pair - applySpecInitialState() pre-populates
    // exactly these five as known-true, the full extent of what Sec29
    // confirms unconditionally. Everything else a mission script names by
    // a fixed authored name (e.g. 'Killbane') traces back to producer (1)
    // above - the parked .czn_pc interior - so it correctly, honestly
    // stays OPEN: since producer (3) was never individually traced and
    // backs resolvers for groups/triggers/vehicles/NPCs too, not just
    // mission characters, defaulting any OTHER unknown name to false here
    // would be inventing an answer this project does not have, not
    // reporting a confirmed one (the OpenValue/OpenStateError discipline
    // this whole map exists to honour) - do not widen this pre-population
    // beyond the five literals without new CONFIRMED spec text.
    OpenValueMap<bool>& objectResolves() { return objectResolves_; }

    // Case-insensitive direct accessor onto the real ingested zone data
    // (ZoneObjectResolver::find() - see zone_object_resolver.h's own
    // top comment), for a caller that wants the record itself (class hash,
    // script_npc_* properties, etc.), not just the boolean objectResolves()
    // already exposes. Returns nullptr if `name` was never seen in any
    // ingested zone (case-insensitively).
    const sr3zone::ObjectRecord* resolveZoneObject(const std::string& name) const { return zoneObjects_.find(name); }

    // Diagnostic/population count - how many distinct named records have
    // been ingested across every loadZoneObjectsSection() call so far.
    size_t zoneObjectsLoadedCount() const { return zoneObjects_.size(); }

    // DIAGNOSTIC ONLY (tools/lua_host_run.cpp's --probe-unresolved-names
    // flag, off by default, never enabled by any normal code path or test):
    // widens objectResolves() to synthetically resolve any miss, and
    // remembers which names were synthesized so probedFieldOr() below can
    // extend the same diagnostic stance to a per-character/per-vehicle OPEN
    // field whose real value this project could never compute for a
    // SYNTHETIC name anyway (e.g. max hit points depends on the character's
    // own preset, Sec34.2 - a probe object has no preset, only a fabricated
    // "resolves" placeholder, so refusing to also fabricate ITS value would
    // just move the same "is this a real name" question one field deeper
    // without answering anything new). Real, legitimately-resolved names
    // are never affected by either mechanism.
    void enableUnresolvedNameProbe() { objectResolves_.enableProbeFallback(true, &probedNames_); }
    bool isProbedName(const std::string& name) const { return probedNames_.count(name) != 0; }
    const std::unordered_set<std::string>& probedNames() const { return probedNames_; }
    uint64_t probedFieldReadCount() const { return probedFieldReadCount_; }

    // DIAGNOSTIC ONLY (see enableUnresolvedNameProbe() above): `v` as normal
    // if known; if OPEN and `ownerName` is itself a probed (synthetic) name,
    // returns `fallback` instead of throwing (test-observably counted, never
    // silent); otherwise throws exactly as `v.get()` would (a real name's
    // own real OPEN gap is never widened by this).
    template <typename T>
    T probedFieldOr(const OpenValue<T>& v, const std::string& ownerName, const T& fallback) {
        if (v.known()) return v.get();
        if (isProbedName(ownerName)) {
            ++probedFieldReadCount_;
            return fallback;
        }
        return v.get(); // throws OpenStateError: a real name's real OPEN gap
    }

    // DIAGNOSTIC ONLY, a second --probe-unresolved-names extension
    // (2026-10-03, peer follow-up once get_dist's own worldPosition OPEN
    // gap became the shared next blocker for 5 missions): UNLIKE
    // probedFieldOr() above, this is not gated on the owning name itself
    // having been a probed (synthetic) name - a legitimately-resolved name
    // (one of the 5 CONFIRMED literals, e.g. "#PLAYER1#") can reach
    // get_dist just as easily as a synthetic one, and its position is
    // EQUALLY unknown either way (no spec gives a real spawn position for
    // ANY object, confirmed name or not - see WorldPosition's own doc
    // comment). So this probes on "is an OPEN position itself" directly,
    // once explicitly enabled, rather than requiring a probed owner name -
    // a deliberately different, slightly broader gate than
    // probedFieldOr()'s, same off-by-default/diagnostic-only discipline.
    void enableWorldPositionProbe() { worldPositionProbeEnabled_ = true; }
    uint64_t worldPositionProbeReadCount() const { return worldPositionProbeReadCount_; }
    uint64_t zoneDataWorldPositionHitCount() const { return zoneDataWorldPositionHitCount_; }
    const GetDistStallInfo& getDistStallInfo() const { return getDistStall_; }
    EffectFinisher& effectFinisher() { return effectFinisher_; }
    // The real effects tables' (effects.xtbl + dlc effects) Effect Name set, lower-folded (spec-tables-environment.md
    // 10.1: 0x005c50b0 maps a Name to its slot index, -1 if absent). A name present here has a valid effect index.
    // Never installed -> effectIndexValid stays OPEN exactly as before. A name ABSENT from the installed set stays OPEN too.
    void installEffectNameTable(std::unordered_set<std::string> lowerNames) { effectNamesLower_ = std::move(lowerNames); }
    size_t effectNameTableSize() const { return effectNamesLower_.size(); }
    bool effectNameInTable(const std::string& name) const { return effectNamesLower_.count(AsciiLowerFold(name)) != 0; }

    // Sec28.11 `debris_flow_recycle_object`: 0x006fd4f0's own role is HYPOTHESIS ("return object to pool"); only
    // the real gated call count is modelled, keyed by flow id, not a simulated pool.
    std::unordered_map<int64_t, int>& debrisFlowRecycleCount() { return debrisFlowRecycleCount_; }
    ContinuousExplosion& continuousExplosion() { return continuousExplosion_; }

    // Sec28.18 `city_zone_swap_is_active`: 0x0242dc90, a list of active-swap name hashes. Starts empty - the
    // honest "nothing marked active yet" default (an empty array IS zero entries, not a guess), set by Sec1.9's
    // `city_zone_swap` (out of this batch's scope) or directly by a test.
    std::unordered_set<uint32_t>& activeCityZoneSwapHashes() { return activeCityZoneSwapHashes_; }

    // CHOSEN, project-wide "now" stand-in (no real clock/per-frame update
    // loop exists in this host) - a plain, monotonically-incrementing
    // counter, same "unconfirmed-but-plausible counter/handle shape"
    // precedent as nextAudioVoiceHandle() above. Used by character_dont_
    // regenerate's own +0x19c0 restamp (Sec30.1, Batch 7, 2026-10-08).
    int64_t nextMonotonicStamp() { return nextMonotonicStamp_++; }
    GameClock& gameClock() { return gameClock_; }

    // Sec32.5 `object_destroy`: this project's own minimal "was a destroy
    // processed against a resolved object" record - the liveness check,
    // capability-predicate gate and per-kind destroy virtual are OPEN/not
    // modeled.
    std::unordered_set<std::string>& destroyedObjects() { return destroyedObjects_; }

    // effect_play/explosion_create (Sec9.1/Sec9.3, 2026-10-07 batch): both
    // CONFIRMED to share one finalize-then-push pair (0x005cf190 then
    // 0x00a48af0) for their own success return, a packed double-precision
    // "instance identifier" this project does not reconstruct (the real
    // packing format is not independently traced). A fresh, monotonically-
    // increasing counter stands in, same "CHOSEN simplification, not a
    // guess at a specific real id" class as nextAudioVoiceHandle() above -
    // kept as its OWN counter, deliberately NOT nextAudioVoiceHandle()/
    // nextEffectPostHandle() (both reserved for the audio/Wwise-post
    // handle namespace), since a visual-effect instance id and an audio
    // post id are two different real namespaces and sharing one counter
    // would conflate them for no reason.
    int64_t nextVisualEffectHandle() { return nextVisualEffectHandle_++; }

    // door_close/door_lock/mesh_mover_hide/mesh_mover_show/mesh_mover_reset/
    // mesh_mover_set_invulnerable (Sec9.12/9.14/9.15/9.16/9.17/14.9) - all
    // resolved through the same "named placed-object" resolver (0x006434a0)
    // Sec9.12 itself observes is "a generic named placed-object registry
    // rather than a door-specific one," shared across doors and
    // mesh-movers. Gated on the SAME shared objectResolves() map every
    // other object kind in this project's scope already uses (Sec29's "one
    // shared global name map" finding - VehicleState's own top note already
    // applies the identical precedent to vehicles) - no separate resolver
    // map invented for this family.
    PlacedObjectState& getOrCreatePlacedObject(const std::string& name) { return placedObjects_[name]; }

    // city_zone_swap (Sec1.9, individually executable-re-derived and
    // cleared by Sec60.3). CONFIRMED: something happens to queue, or (under
    // a mission-active+name-match gate this project does not model, Sec1.9's
    // own remaining OPEN item) immediately apply, a zone-pair swap keyed by
    // name and the boolean - neither path has any Lua-visible return value
    // or other project-tracked side effect to distinguish, so which
    // internal path runs does not change this project's own observable
    // contract. Recorded for test-observability only.
    void recordCityZoneSwap(const std::string& name, bool value) { cityZoneSwaps_[name] = value; }
    bool cityZoneSwapKnown(const std::string& name) const { return cityZoneSwaps_.count(name) != 0; }

    // sidewalk_disable_nodes (Sec15.3, individually cleared by Sec60.3).
    // CONFIRMED: resolved via the shared trigger/object resolver, then
    // enables/disables a named group of sidewalk/pathing action-nodes (the
    // vtable `+0x58` method and the final apply's own exact semantics are
    // OPEN - not modelled further, same "resolve + flag, no deeper replay"
    // simplification as the placed-object family above).
    void setSidewalkNodesDisabled(const std::string& name, bool disabled) { sidewalkNodesDisabled_[name] = disabled; }
    bool sidewalkNodesKnown(const std::string& name) const { return sidewalkNodesDisabled_.count(name) != 0; }

    // mip_streaming_pause (Sec60.1). CONFIRMED real cross-script collision:
    // the Lua path always passes bit index 0 of some shared pause bitfield,
    // aliasing every caller onto the exact same bit regardless of the
    // string argument scripts pass (real call sites: 2, 1 script, argument
    // shape `boolean,string` - empirically observed, `verdict_stub_hits_
    // with_missions.tsv`; the string is read but, per Sec60.1's own text,
    // plays no role in which bit is touched). One process-wide bool stands
    // in for that single shared bit. CHOSEN starting default (false, "not
    // paused") - no spec states this bit's construction value either way.
    bool mipStreamingPaused() const { return mipStreamingPaused_; }
    void setMipStreamingPaused(bool paused) { mipStreamingPaused_ = paused; }

    // waypoint_remove (Sec6.17). CONFIRMED: the argument (optional number,
    // default 3) selects a local-removal path (bit 1), a network-replicated
    // path (bit 2), or both (the default 3 runs both) - no Lua-visible
    // return value on any path, and this project has no real waypoint/HUD-
    // navigation object to remove, so neither path has an observable effect
    // to distinguish. Recorded for test-observability only (call count +
    // last mode value) - same "no Lua-visible effect beyond the call
    // itself to model" class as party_dismiss_all above.
    uint64_t waypointRemoveCount() const { return waypointRemoveCount_; }
    int32_t lastWaypointRemoveMode() const { return lastWaypointRemoveMode_; }

    // traffic_disable_drawbridge_area (Sec6.4). CONFIRMED: arg 1 (rounded)
    // builds the fixed name "Spline_Disabler<%03d>" (zero-padded to 3
    // digits), resolved through the SAME shared trigger/object resolver
    // this project already models generically via objectResolves() (Sec1.1,
    // 0x005e4e30); on success, bit (arg1-1) of a 32-bit global bitmask
    // (0x02705fe4) is set/cleared to match arg 2. The resolved trigger's own
    // further dispatch (0x00a6dd70) is OPEN beyond the bitmask write - not
    // modelled further (shared, per Sec6.22's own cross-reference, with
    // traffic_disable_lanes/Sec12.19's identical OPEN boundary). An
    // out-of-range area index (outside 1-32) is a silent no-op on the
    // bitmask - no real failure path is given either way, and a 32-bit mask
    // cannot represent a bit outside that range; this project does not
    // invent one.
    uint32_t drawbridgeDisabledMask() const { return drawbridgeDisabledMask_; }

    // ai_do_scripted_move (Sec3.11): see CharacterState's own new
    // pendingScriptedMoveFlags/pendingScriptedMoveDestination/
    // scriptedMoveDispatchCount fields for the per-character state this
    // native writes.

    // on_attack_performed (Sec3.12): see CharacterState's own new
    // onAttackPerformedGenericSlotCallback/onAttackPerformedCharacterSlotCallback
    // fields.

    // teleport (Sec4.3): see TeleportRequest's own doc comment above.
    // CONFIRMED the resolved target's own class-row bit (0x02cc9900,
    // byte +0xa bit 0x2, HIGH CONFIDENCE "player class") selects between
    // the two mutually exclusive apply paths - this project reuses
    // playerClassChainResolves() (Sec3.3/Sec3.5's own field) as the
    // stand-in for that SAME real-world distinction: a different real
    // resolver family by address, but the identical "is this name player-
    // classed" question, per this header's own Sec29 "one flat map stands
    // in for several similar per-kind resolvers" convention - not a claim
    // that 0x02cc9900's own byte +0xa bit 0x2 and 0x00a27e20's class-bit
    // gate are the SAME physical field, only that this project answers
    // both questions with the one map it already has for the purpose,
    // rather than adding a third near-duplicate field for the same
    // concept.
    std::unordered_map<std::string, TeleportRequest>& teleportRequests() { return teleportRequests_; }
    uint64_t& teleportScriptedRequestCount() { return teleportScriptedRequestCount_; }

    // release_to_world (Sec4.4): CONFIRMED three-tier resolver family
    // (group/character/vehicle, stopping at first success) that this
    // project's own flat objectResolves() map cannot by itself
    // distinguish (Sec29's "one shared map" convention deliberately does
    // not carry per-kind identity). This project's own minimal, dedicated
    // kind classifier for JUST this function: 1 = tier-1 group, 2 =
    // tier-2 character, 3 = tier-3 vehicle - OPEN until a test sets it for
    // a name objectResolves() also reports true for (the two are
    // independent facts a test must configure together). Tier 1 (group)
    // additionally needs its own pre-filtered "currently active member"
    // list (this project has no generic group-membership-with-active-bit
    // structure anywhere - a test supplies the already-filtered list
    // directly, same "test fills in what it needs to exercise" precedent
    // as registerVdoObjectForTesting/markScriptNpcBoundForTesting
    // elsewhere in this header). The shared vtable-slot-+0x5c "release"
    // method itself has no further observable effect in this project's
    // scope (no in-scope reader of "was this object released") - only
    // that it fired, once per released object, is counted.
    OpenValueMap<int32_t>& releaseToWorldKind() { return releaseToWorldKind_; }
    std::unordered_map<std::string, std::vector<std::string>>& releaseToWorldGroupMembers() { return releaseToWorldGroupMembers_; }
    std::unordered_map<std::string, uint64_t>& releasedCount() { return releasedCount_; }
    // release_to_world calls whose name resolved (objectResolves) but matched no group/character/vehicle record
    // this host holds, so the real tier could not be determined (Sec4.4 2026-10-12: native does nothing).
    uint64_t& releaseToWorldUnclassifiedCount() { return releaseToWorldUnclassifiedCount_; }

    // --- move_to_do's own sequential "move action id" (Sec21.22, wave 2
    // batch W2-1): a global counter at 0x0130cb94, "wrapping, skipping 0",
    // whose value is stored into the target's +0xc34 word and returned to
    // Lua. Its INITIAL value is not given anywhere in the spec, so it is OPEN
    // (a returned id is Lua-visible - inventing a start value would be
    // inventing data). HIGH CONFIDENCE: 16-bit wrap (+0xc34 is a 2-byte field,
    // Sec22.15) and increment-then-skip-zero (the natural reading of
    // "wrapping, skipping 0"). allocateMoveToActionId() throws while OPEN.
    OpenValue<int32_t>& moveToActionIdCounter() { return moveToActionIdCounter_; }
    DebrisFlowState& debrisFlow() { return debrisFlow_; }

    // get_object_pos (Sec20.24): the trailing 3 "placeholder" values ARE the
    // SAME fixed `.rdata` constant (0x0112a1e0) on EVERY call, success or
    // failure (CONFIRMED - "never anything object-derived"). No pass of
    // this spec ever independently reads this address's own real bit
    // pattern (unlike 0x01117a4c, later confirmed 1.0 - Sec24.9's own
    // "byte-confirmed" pass) - genuinely OPEN, not merely uncited. Refusing
    // this one honestly (rather than reusing an unrelated already-resolved
    // constant, which would be a silent guess) means get_object_pos itself
    // is OPEN until a test supplies this value - the correct, honest
    // outcome per this project's own discipline.
    OpenValue<double> objectPosTrailingConstant{"get_object_pos's own fixed trailing placeholder constant (0x0112a1e0)",
                                                "spec-lua-api-behaviour.md Sec20.24"};

    // ingame_effect_add_trigger (Sec21.5): effect-name -> numeric id
    // (0x005c50b0, "hashes/interns the name... returns -1 on failure" -
    // CONFIRMED -1 sentinel, a broader named-resource lookup not
    // independently given concrete entries). A plain map (not OpenValueMap)
    // since "-1, not found" is itself the real, CONFIRMED default - not an
    // unknown engine value.
    std::unordered_map<std::string, int32_t>& effectIdByName() { return effectIdByName_; }

    // continuous_explosion_avoid_object_add (Sec21.7): the real fixed 8-slot
    // global position-pair array, gated on a global "active" flag byte and
    // a hard cap of 8 (CONFIRMED). This project has no real position data
    // for the resolved record's own linked-object chain (see
    // PlacedObjectState's own doc comment) - tracks resolved NAMES instead
    // of positions, same cap, same "skip entirely once full" shape.
    static constexpr size_t kContinuousExplosionAvoidObjectCap = 8;
    std::vector<std::string>& continuousExplosionAvoidObjects() { return continuousExplosionAvoidObjects_; }
    WindOverride& windOverride() { return windOverride_; }
    void setIngameEffectIndex(const std::string& triggerName, int32_t v) { ingameEffectIndices_[triggerName] = v; }
    uint64_t& ingameEffectRemoveNotifyCount() { return ingameEffectRemoveNotifyCount_; }
    uint64_t& ingameEffectRemoveClearedCount() { return ingameEffectRemoveClearedCount_; }

    // --- projectile_fire_from_navpoint (Sec40.3) --------------------------
    // "No crash shapes found - every resolution step is properly guarded."
    // No coordinate system to compute a real trajectory - only the gated
    // (both positions resolve, distinct, weapon name resolves, spread <
    // 90 deg) call is counted.
    uint64_t& projectileFireFromNavpointCount() { return projectileFireFromNavpointCount_; }

    // --- path_name_is_path (Sec42.6) --------------------------------------
    // CONFIRMED: "true only for a live object of the specific 'path' kind."
    // This project has no object-kind table - OPEN until a test registers
    // a name's kind.
    OpenValueMap<bool>& pathObjectIsPath() { return pathObjectIsPath_; }
    OpenValueMap<std::string>& closestObjectOverride() { return closestObjectOverride_; }
    MeshMoverRecord& getOrCreateMeshMover(const std::string& name) { return meshMovers_[name]; }
    bool hasMeshMover(const std::string& name) const { return meshMovers_.count(name) != 0; }
    uint64_t& meshMoverApplyImpulseCount() { return meshMoverApplyImpulseCount_; }
    // mesh_mover_reset_by_zone: "resets every free-standing, currently-
    // attached mesh mover in a named loaded zone... no host gate at all."
    // No real per-zone mover list in this project - counted per zone name,
    // honestly, not simulated.
    std::unordered_map<std::string, uint64_t>& meshMoverResetByZoneCounts() { return meshMoverResetByZoneCounts_; }

    // --- fov_check_xz_plane (Sec44.5) --------------------------------------
    // "An XZ-flattened cone test; arg 3 is a half-angle in degrees
    // (default 30)." No coordinate system in this project - the boolean
    // test result is test-settable per (reference, target) pair; a real
    // call with no override is the honest OPEN refusal (no geometry here).
    // CONFIRMED regardless of geometry: "when the target is in view it
    // returns 2 Lua values (true, false) instead of 1" - this logic defect
    // is reproduced directly by the stub, independent of the override.
    OpenValueMap<bool>& fovCheckXzPlaneResult() { return fovCheckXzPlaneResult_; } // key: "ref|target"

    // --- debris_flow_set_inactive (Sec45.2) - CRASH, particle-effect
    // debris flow (NOT EngineState::DebrisFlowState above, which is the
    // UNRELATED world-debris-pile Sec14.20 `debris_flow_set_active`) -----
    // This batch owns only _set_inactive; a sibling wave-2 batch's own
    // debris_flow_create/_destroy/_add_object/_add_avoid_object (not yet
    // merged) is expected to populate particleDebrisFlowKnownIds below -
    // designed to be additive-compatible, not blocked on that merge.
    // HARDENED (host-safety): the real worker sets a stale/unresolvable
    // stored object handle to null and STILL calls a virtual method
    // through it (Sec45.2, "a null dereference reachable whenever a
    // debris-flow entry outlives its object") - this host refuses the
    // deactivate for any flow id not in the registry instead, counting it.
    // "Always replicates (opcode 0x45/0x25) with no host/existence check"
    // - the stub calls replicateStateChange() unconditionally regardless
    // of whether the guard fired.
    std::unordered_set<int64_t>& particleDebrisFlowKnownIds() { return particleDebrisFlowKnownIds_; }
    uint64_t& debrisFlowSetInactiveNullHandleGuardCount() { return debrisFlowSetInactiveNullHandleGuardCount_; }

    // --- continuous_explosion_follow_target_remove (Sec25.23/57.4) --------
    // Shares the SAME <=2-entry global follow-target list a sibling wave-2
    // batch's own continuous_explosion_follow_target_add (not yet merged,
    // 0x005eb3e0) is expected to populate - this batch owns only _remove;
    // field naming ("continuousExplosionFollowTargets_", a plain name
    // vector) is chosen to be the obvious shared name a merge would
    // naturally coordinate on, per this task's own "coordinate field
    // naming if you can infer it" guidance.
    std::vector<std::string>& continuousExplosionFollowTargets() { return continuousExplosionFollowTargets_; }

    // --- is_a_vehicle / is_syn_tower_destroyed (Sec54.3) -------------------
    // is_a_vehicle: no new state - reuses hasVehicle() above directly.
    // is_syn_tower_destroyed: CONFIRMED "nothing more than the already-
    // documented city_zone_swap_is_active with a hardcoded zone name" -
    // reuses the EXISTING cityZoneSwapValue()/recordCityZoneSwap() map
    // directly (Sec28.18/Sec1.9) under the CHOSEN key "syn_tower" (the
    // real literal zone-name string is not given by spec text, only that
    // one is hardcoded - this project's own natural, clearly-labelled
    // stand-in key, not a fabricated gameplay fact).
    static constexpr const char* kSynTowerZoneSwapKey = "syn_tower"; // CHOSEN, see doc comment above

    // --- level_light_enable_all_do / light_group_attach_to_camera /
    // light_group_detach_from_camera (Sec54.2) -----------------------------
    // "a reference-counted 16-slot table (attach increments, detach
    // decrements, removal only at zero)... a 17th attach is silently
    // dropped... a light group that gets despawned while still attached
    // can never be detached through this binding." Modelled as ONE global
    // ref-count against the CONFIRMED 16-slot cap (this project tracks no
    // per-group identity beyond the name key, consistent with "no real
    // light-group table" gap elsewhere).
    std::unordered_map<std::string, int>& lightGroupAttachRefCounts() { return lightGroupAttachRefCounts_; }
    int& lightGroupAttachedSlotsUsed() { return lightGroupAttachedSlotsUsed_; } // total across all groups, capped at 16
    uint64_t& levelLightEnableAllCount() { return levelLightEnableAllCount_; } // "three lighting modes," structure only

    // --- los_check_do (Sec54.2) - CRASH, stuck-poll hazard -----------------
    // CONFIRMED: "returns -1 ('ask again'), then 1 or 0 once resolved...
    // an unresolvable object name returns 0." This project has no cached
    // request-pool mechanism to EVER exhaust (the real stuck-poll hazard's
    // own trigger condition), so the hazard structurally cannot fire here
    // - a real resolved-pair query needs real line-of-sight geometry this
    // project does not have, so it is test-settable (OPEN until set); only
    // the CONFIRMED "unresolvable name -> 0" default is a real fact,
    // applied without an override.
    OpenValueMap<int32_t>& losCheckResult() { return losCheckResult_; } // key: "a|b", values -1/0/1

    // --- flashlights_enable / flashlights_disable (Sec55.5) ----------------
    // CONFIRMED: global byte 0x0262e690, "a world/mission reset routine
    // clears the byte back to 'enabled' independently" - CHOSEN true
    // initial (matches the documented reset value; this project has no
    // separate mission-reset hook in scope to model as a distinct event).
    bool& flashlightsEnabledGlobal() { return flashlightsEnabledGlobal_; }

    // --- waypoint_is_placed (Sec46/T14, AGG) -------------------------------
    // No individual spec text (lumped into §46.5's "11 functions... vehicle
    // flags and queries" cluster with no per-function detail beyond the
    // ones the prose actually names). CHOSEN minimal: a GLOBAL query (no
    // vehicle argument fits the name), false initial, no in-scope setter
    // (the real waypoint_add/_remove family is out of this batch).
    bool& waypointPlaced() { return waypointPlaced_; }

    // --- Sec41.4 hud_diversion_remove_callback: CHOSEN simplification - the
    // real table only has 4 valid slot-occupancy counters for 4 specific
    // (unnamed by spec) diversion types; this project tracks one
    // floored-at-zero counter per type id actually used by a test/script,
    // which is behaviourally equivalent for every type this host is ever
    // given (an id this host was never told about was never incremented
    // either, so decrementing it floors at zero exactly as spec requires).
    std::unordered_map<int32_t, int32_t>& diversionSlotOccupancy() { return diversionSlotOccupancy_; }
    OpenValueMap<bool>& lightGroupResolves() { return lightGroupResolves_; }
    LightGroup& lightGroup(const std::string& name) { return lightGroups_[name]; }

protected: // private in EngineState before the split
    double lastScriptedOrderTickSeconds_ = -1.0;  // tickScriptedOrders: game-clock time of the previous update
    double downedTimerScale_ = 1.0;
    // Mission-host Phase 2's own name->live-object resolver (see
    // liveObjectKind()/bridgeLive*Position() above) - deliberately separate
    // from characters_/vehicles_ above (which hold EVERY name any stub has
    // ever touched, test or real) and from objectResolves_ below (which
    // answers resolution, not liveness).
    std::unordered_map<std::string, LiveObjectKind> liveObjectKinds_;
    int64_t nextVisualEffectHandle_ = 1; // effect_play/explosion_create, Sec9.1/Sec9.3

    // --- teleport_check_done/turn_to_check_done/move_to_check_done/
    // vehicle_pathfind_check_done (Sec35.2), see each public accessor's own
    // doc comment above for citation/reasoning. Keyed by (name, kind) - see
    // kScriptedRequestKind* above for the 3 real kind values. `bool` = done
    // (true) vs. pending (false); absent = "no matching request" (status 2).
    std::map<std::pair<std::string, int>, bool> scriptedRequestDone_;
    // Case-folded: the by-name registry lookup (0x004588f0) is case-insensitive (see OpenValueMap's foldCase note).
    OpenValueMap<bool> objectResolves_{"named-object resolution", "spec-lua-api-behaviour.md Sec3.9/Sec10.6/Sec29", true};
    // NEW 2026-10-08: the real name<->sr3zone::ObjectRecord link -
    // loadZoneObjectsSection()/resolveZoneObject()'s own backing store, see
    // those methods' own doc comments above.
    ZoneObjectResolver zoneObjects_;
    std::unordered_set<std::string> effectNamesLower_;
    // DIAGNOSTIC ONLY state (see enableUnresolvedNameProbe()/probedFieldOr()
    // above) - empty, and probedFieldReadCount_ zero, unless a caller
    // explicitly opts in; never populated by any normal code path or test.
    std::unordered_set<std::string> probedNames_;
    uint64_t probedFieldReadCount_ = 0;
    // DIAGNOSTIC ONLY (see enableWorldPositionProbe()/worldPositionOr()
    // above) - off unless a caller explicitly opts in; never enabled by any
    // normal code path or test.
    bool worldPositionProbeEnabled_ = false;
    uint64_t worldPositionProbeReadCount_ = 0;
    // REAL (not diagnostic) - see tryResolveWorldPositionFromZoneData()'s
    // own doc comment above. Always active, no opt-in flag.
    uint64_t zoneDataWorldPositionHitCount_ = 0;
    GetDistStallInfo getDistStall_; // feature/auto-objective-harness, 2026-10-12
    EffectFinisher effectFinisher_;
    std::unordered_map<int64_t, int> debrisFlowRecycleCount_;
    ContinuousExplosion continuousExplosion_;
    std::unordered_set<uint32_t> activeCityZoneSwapHashes_;
    std::unordered_map<uint32_t, std::vector<ZoneMember>> zoneMembersByZone_;
    GameClock gameClock_;
    std::unordered_set<std::string> destroyedObjects_;
    std::unordered_map<std::string, PlacedObjectState> placedObjects_;
    std::unordered_map<std::string, bool> cityZoneSwaps_;
    std::unordered_map<std::string, bool> sidewalkNodesDisabled_;
    bool mipStreamingPaused_ = false;
    uint64_t waypointRemoveCount_ = 0;                          // Sec6.17
    int32_t lastWaypointRemoveMode_ = 0;                        // Sec6.17
    uint32_t drawbridgeDisabledMask_ = 0;                       // Sec6.4
    std::unordered_map<std::string, TeleportRequest> teleportRequests_;
    uint64_t teleportScriptedRequestCount_ = 0;
    OpenValueMap<int32_t> releaseToWorldKind_{"release_to_world resolved-object kind (1=group/2=character/3=vehicle)",
                                               "spec-lua-api-behaviour.md Sec4.4"};
    std::unordered_map<std::string, std::vector<std::string>> releaseToWorldGroupMembers_;
    uint64_t releaseToWorldUnclassifiedCount_ = 0;
    std::unordered_map<std::string, uint64_t> releasedCount_;
    std::map<std::pair<std::string, int>, int32_t> activeScriptRequestId_; // 0x006f62b0, Sec30.4/30.5/30.6
    int32_t nextScriptRequestSeq_ = 1;                            // wraps at 0xFFF, CONFIRMED 12-bit structure
    int64_t nextMonotonicStamp_ = 1;                               // CHOSEN "now" stand-in, Sec30.1
    // Wave 2 batch W2-1 (b15) - see each accessor's own doc comment above.
    OpenValue<int32_t> moveToActionIdCounter_{"move_to_do action-id counter (0x0130cb94) initial value",
                                              "spec-lua-api-behaviour.md Sec21.22"};
    DebrisFlowState debrisFlow_;
    std::vector<std::string> continuousExplosionAvoidObjects_;
    std::unordered_map<std::string, int32_t> effectIdByName_;
    WindOverride windOverride_;
    std::unordered_map<std::string, int32_t> ingameEffectIndices_;
    uint64_t ingameEffectRemoveNotifyCount_ = 0;
    uint64_t ingameEffectRemoveClearedCount_ = 0;
    uint64_t projectileFireFromNavpointCount_ = 0;
    OpenValueMap<bool> pathObjectIsPath_{"path-kind object resolver (no object-kind table here)",
                                        "spec-lua-api-behaviour.md Sec42.6"};
    OpenValueMap<std::string> closestObjectOverride_{"get_closest_object query (no coordinate system here)",
                                                     "spec-lua-api-behaviour.md Sec53.5"};
    std::unordered_map<std::string, MeshMoverRecord> meshMovers_;
    uint64_t meshMoverApplyImpulseCount_ = 0;
    std::unordered_map<std::string, uint64_t> meshMoverResetByZoneCounts_;
    OpenValueMap<bool> fovCheckXzPlaneResult_{"fov_check_xz_plane XZ-cone test (no coordinate system here)",
                                              "spec-lua-api-behaviour.md Sec44.5"};
    std::unordered_set<int64_t> particleDebrisFlowKnownIds_;
    uint64_t debrisFlowSetInactiveNullHandleGuardCount_ = 0;
    std::vector<std::string> continuousExplosionFollowTargets_;
    std::unordered_map<std::string, int> lightGroupAttachRefCounts_;
    int lightGroupAttachedSlotsUsed_ = 0;
    uint64_t levelLightEnableAllCount_ = 0;
    OpenValueMap<int32_t> losCheckResult_{"los_check_do cached line-of-sight result (no geometry here)",
                                          "spec-lua-api-behaviour.md Sec54.2"};
    bool flashlightsEnabledGlobal_ = true;
    bool waypointPlaced_ = false;
    std::unordered_map<int32_t, int32_t> diversionSlotOccupancy_;
    OpenValueMap<bool> lightGroupResolves_{"light-group name resolution (0x00739690)", "spec-lua-api-behaviour.md Sec17.24"};
    std::unordered_map<std::string, LightGroup> lightGroups_;
};

} // namespace sr3luahost
