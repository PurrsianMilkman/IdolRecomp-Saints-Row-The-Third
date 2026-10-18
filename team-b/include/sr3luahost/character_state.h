// Character-shaped engine state: CharacterState and the per-character record
// types it holds (EnemyTargetRecord, MinimapIconRecord, ObjectIndicatorRecord).
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

#include "sr3luahost/character_state_spawn_health_ai.h"
#include "sr3luahost/character_state_vehicle_orders.h"
#include "sr3luahost/character_state_scripted_actions.h"

namespace sr3luahost {

// CharacterState is assembled from three data-only base structs plus its own
// last section (Team C refactor, 2026-10-17: the struct was ~2,800 lines).
// The fields moved verbatim and in their original order; bases are laid out
// and initialised first, in declaration order, so construction order is
// unchanged. Names stay reachable as CharacterState::X and c.x.

// One tracked "character"-shaped object (per this header's own top note:
// really just "whatever the resolved name string was", never a true
// distinct engine entity). CLOUD PHASE 2026-09-30: every engine field
// below that a stub READS is an OpenValue (open_state.h) - OPEN for a name
// not yet seen, refusing reads until set, because no spec gives a fresh
// object's values. The per-field notes below that mention a "default"
// describe the pre-conversion stand-ins and are kept for history; the
// stand-in values themselves are gone.
struct CharacterState : CharacterStateSpawnHealthAi, CharacterStateVehicleOrders, CharacterStateScriptedActions {
    // Moved here from CharacterStateVehicleOrders (it reads isAlive from
    // CharacterStateSpawnHealthAi); body unchanged.
    // 0x0096f4f0 alive predicate (spec sync e6691f1, Sec30.5/Sec34.5 round-2 FINAL): alive = !null &&
    // state != 5 && !(byte_e3 & 1). The null case is handled by the resolve step upstream. An explicit
    // isAlive.set() (tests) still wins; otherwise computed live from lifeState/killFinalizedFlagE3 (seeded
    // at construction, so no longer OPEN by default).
    bool aliveByPredicate() const { return isAlive.known() ? isAlive.get() : notDeadForActionPlay(); }

    // =====================================================================
    // Batch 4 (spec-lua-api-behaviour.md Sec13+Sec14, ranks ~151-250,
    // native-implementation batch 2026-10-08, 55 names) - see each field's
    // own citation below; full per-native reasoning lives in the stub
    // file's own doc comments
    // (lua_spec_confirmed_stubs_b11_batch4_flags_combat.cpp).
    // =====================================================================

    // object_indicator_add_objective_text (Sec13.1): one recorded "an
    // objective-text indicator was requested" call against this resolved
    // object - same minimal "record the raw request, not the real HUD
    // dispatch" convention as MinimapIconRecord/ObjectIndicatorRecord above
    // (0x008f4ef0/0x008f76e0's own bodies are OPEN, not modelled).
    struct ObjectiveTextIndicatorRecord {
        std::string iconKey; // arg 2, mandatory, no nil-gate (CONFIRMED, Sec13.1)
        std::string textKey; // arg 3, mandatory, no nil-gate (CONFIRMED, Sec13.1)
    };
    std::vector<ObjectiveTextIndicatorRecord> objectiveTextIndicators;

    // character_set_no_explosive_panic (Sec13.5): object dword +0x1c9c bit
    // 0x4 - a NEW dword this header has not previously modelled (distinct
    // from +0x1c98/+0xec/+0xe4, all CONFIRMED-distinct offsets elsewhere in
    // this header). Same double-gate shape as the forceFlagGatePasses
    // family (gated on the SAME CONFIRMED-true-in-single-player gate,
    // Sec3.7's own cross-reference, raw-disassembly-confirmed hidden-`this`
    // delegate per Sec13.29 item 1).
    OpenBits32 forceFlags1c9c{"character force-flag dword +0x1c9c (no_explosive_panic bit 0x4)",
                              "spec-lua-api-behaviour.md Sec13.5"};
    static constexpr uint32_t kNoExplosivePanicBit1c9c = 0x4u;

    // ai_scripted_action_is_processing (Sec13.7): the gate 0x008addb0's own
    // real-world identity at THIS call site is explicitly unattributed by
    // Sec13.7's own text ("a check this document has not previously
    // attributed to a specific subsystem") - kept SEPARATELY SCOPED from
    // CharacterState::controlLockGatePasses above (Sec7.7/Sec7.22/Sec7.25's
    // own CONFIRMED-true-in-single-player instance of the SAME literal
    // address) for the same reason CharacterState::isAlive is kept separate
    // from isDeadHighConfidence above: nothing in this spec confirms the
    // two call sites share scope/timing, so merging them would assume a
    // fact neither section states. OPEN until a test sets it (no spec gives
    // THIS call site's own single-player resolution).
    OpenValue<bool> scriptedActionDispatchGatePasses{
        "ai_scripted_action_is_processing's own gate 0x008addb0 (separately scoped from controlLockGatePasses)",
        "spec-lua-api-behaviour.md Sec13.7"};

    // set_team (Sec13.18): object +0x1ca8, the character's current team/
    // faction id - CHOSEN sentinel -1 ("never set"), since no spec gives a
    // real spawn-time default for this byte.
    int32_t currentTeamCode = -1;

    // inv_item_in_slot (Sec13.21): keyed by slot CODE, test-registrable
    // only - this project has no real inventory/item-catalog data anywhere
    // (stated, project-wide gap), so any resolved, LIVE slot's held item
    // name is test-registrable only; absent a registration, the function's
    // own CONFIRMED "none" fallback applies honestly (not a guess: an
    // unresolved item record really does fall back to "none" per Sec13.21's
    // own text). CLOSED 2026-10-09: the slot-NAME table 0x008dc6b0 is now
    // fully recovered (11 names, codes 0-0xa: unarmed/melee/pistol/smg/
    // shotgun/rifle/explosive/special/vehicle/grenade/single_use) - of
    // those, codes 0-8 are LIVE (dispatch to the array-of-8-byte-entries
    // lookup, `0x005fec20`'s default arm) and keyed here by their own code;
    // codes 9 (grenade, hard-coded dead inside `0x005fec20`'s own switch)
    // and 0xa (single_use, real inside `0x005fec20` but never reached by
    // this native's own caller-side switch) are CONFIRMED dead, always
    // "none", and never looked up here. The pre-existing 5 "best-*" literal
    // names (outside the table) add codes 0xd/0xe/0xf (LIVE - the dedicated
    // helpers 0x005ff7a0/0x005ff220/0x005ff680, whose own internal item-
    // selection logic is OPEN/not independently decompiled, same "test-
    // registrable, honest none" treatment) and 0x10/0x11 (CONFIRMED dead).
    // Code 0xc ("currently selected slot") is handled inside `0x005fec20`
    // for OTHER callers but is never produced by any of the 11+5 resolvable
    // Lua slot-name strings, so it is unreachable via this native and not
    // modeled here either.
    std::unordered_map<int, std::string> inventoryBestSlotItemForTesting;

    // ai_set_personality (Sec13.27): the profile-name hash/table-scan
    // helper 0x00507e60's own real selection semantics are OPEN (not fully
    // traced against its implicit context argument) - see EngineState::
    // lookupAiPersonalityProfileCode for the test-only registration hook
    // (mirrors lookupNotorietyTrackCode's own "never-populated object
    // space" precedent, Sec3.6). The further 0x011177e5 gating nuance
    // (Sec13.27's own OPEN item) is not separately modelled - this project
    // always proceeds to the double-gate write once a profile code is
    // resolved, matching the common, CONFIRMED top-level path.
    int32_t aiPersonalityProfileCode = -1; // CHOSEN -1 = "never set"

    // character_collision_enable (Sec13.26): the real reason-coded
    // dispatcher (0x0094f450) is NOT modelled (several unidentified
    // predicates/reason codes, Sec13.26's own OPEN list) - same "record
    // that a call happened, not the real internal dispatch" convention as
    // EngineState::callbacksClearedCount_ (Sec28.17). Per-character count
    // and last-requested value only.
    uint64_t collisionEnableCallCount = 0;
    bool lastCollisionEnableValue = false;

    // character_ragdoll_set_last_valid_position (Sec13.25): a per-character
    // 10-entry rolling history ring buffer - DIFFERENT from the already-
    // registered character_ragdoll_set_last_resort_position AND from
    // character_ragdoll_clear_last_resort_position (Sec13.6, a pure GLOBAL
    // with no per-character field at all - see EngineState::
    // ragdollLastResortPositionClearCount for that one) - see the scoping
    // report's own near-duplicate warning. This project has no coordinate
    // system (stated, project-wide gap) - the resolved destination's own
    // NAME stands in for "the position fields" (same convention
    // VehicleState::chaseTargetName already establishes for an identical
    // gap, Sec9.4).
    std::array<std::string, 10> ragdollValidPositionHistory{};
    int ragdollValidPositionHistoryIndex = 0;

    // action_play_synced_state (Sec14.1): the local-apply path's own
    // "cross-write each actor's own position-pair reference into the
    // other" (0x0095f8d0) - this project has no coordinate system (stated
    // gap), so the partner's NAME stands in, same convention as
    // ragdollValidPositionHistory above.
    std::string syncedActionPartnerName;
    bool syncedActionPaired = false;

    // set_cant_flee_flag (Sec14.26): object byte +0xa bit 0x20 - a
    // DIFFERENT bit of the SAME real byte CharacterState::followerCanAbandon
    // above already models bit 0x02 of (Sec7.6) - kept as an independent
    // field, same "two bits of the same byte, independent fields" precedent
    // ignoreAI/inScriptedCover already establish for byte +0x2bc.
    OpenValue<bool> cantFleeFlag{"character byte +0xa bit 0x20 (cant_flee)", "spec-lua-api-behaviour.md Sec14.26"};

    // set_behavior_set (Sec14.27): the name-table resolver 0x004f27f0's own
    // table contents are OPEN - see EngineState::lookupBehaviorSetCode for
    // the test-only registration hook (same "never-populated object space"
    // precedent as ai_set_personality above); sentinel 0x2c (44) is the
    // CONFIRMED real "no match" fallback. +0xe4 real field.
    int32_t behaviorSetIndex = -1; // CHOSEN -1 = "never set"
    // vtable +0x70 gate (Sec14.27): which of the two apply paths (validated
    // vs. raw-write) ran last - test-observable only; both paths write the
    // SAME observable field in this project's model (the validator/
    // finalizer 0x004f36a0's own internals are OPEN).
    OpenValue<bool> behaviorSetVtableGatePasses{"set_behavior_set's own vtable +0x70 gate",
                                                "spec-lua-api-behaviour.md Sec14.27"};

    // on_ai_do_action_complete (Sec14.7): character hook slot 0x14 (20) -
    // reached ONLY via the plain-character resolve path (EngineState::
    // playerClassChainResolves() false/absent for this name - see the
    // stub's own doc comment). Same single-dedicated-field-per-hook-kind
    // convention as onDeathCallback/onRevivedCallback above.
    std::string onAiDoActionCompleteCallback;

    // player_script_controlled_clear (Sec14.29): object +0x2888 mode field
    // - CHOSEN 0 = "not script-controlled". The companion status bit lives
    // on CharacterState::statusByte28ac just below (bit 0x2).
    int32_t scriptControlledMode = 0;

    // --- Player-category status-byte family (Sec14.3-Sec14.5/Sec14.29,
    // independently confirmed via literal debug-tag strings at every site -
    // Sec14.31's own cross-function census): reached via the generic name/
    // "#PLAYER#" resolver (0x00a280c0), whose handle is used only as a
    // validity gate - this project's flat objectResolves() stands in for
    // that gate (Sec29). Modelled on CharacterState directly (the resolved
    // name is tracked via getOrCreateCharacter(), same simplification
    // CharacterState::controlsDisabled/movementDisabled above already use
    // for an identical "player-category object" case, +0x1d99). ---
    OpenBits32 statusByte28ac{
        "character/player status byte +0x28ac (cant_take_human_shields 0x80 / script_controlled 0x2)",
        "spec-lua-api-behaviour.md Sec14.4/Sec14.29/Sec14.31"};
    static constexpr uint32_t kCantTakeHumanShieldsBit28ac = 0x80u;
    static constexpr uint32_t kScriptControlledBit28ac = 0x2u;
    OpenBits32 statusByte28ad{"character/player status byte +0x28ad (cant_be_downed 0x1 / human_shield_locked 0x8)",
                              "spec-lua-api-behaviour.md Sec14.3/Sec14.5/Sec14.31"};
    static constexpr uint32_t kCantBeDownedBit28ad = 0x1u;
    static constexpr uint32_t kHumanShieldLockedBit28ad = 0x8u;

    // character_has_specific_human_shield (Sec13.14): the "qualifies as
    // currently using a human shield" combined predicate (0x009aa260 AND
    // NOT 0x009aa660) - this project's own minimal stand-in, OPEN until a
    // test sets it (the real byte/offset semantics inside 0x009aa260's own
    // duplicated branches are OPEN per Sec13.14's own text). Reuses
    // CharacterState::humanShieldHostageName (Sec45.1) directly for "the
    // actual currently-held-shield handle" - same real concept (the
    // taker's own held-hostage name), already modelled, not duplicated.
    OpenValue<bool> usingHumanShieldQualifies{
        "character_has_specific_human_shield's own combined gate (0x009aa260/0x009aa660)",
        "spec-lua-api-behaviour.md Sec13.14"};

    // npc_revive (Sec13.11): a DIFFERENT real call chain from player_revive
    // (Sec32.3)'s own reviveCount above (0x009a7c90/0x009a78d0 vs.
    // player_revive's own routine) - kept as its own separately-scoped
    // counters per this header's own "separately scoped, not reused"
    // convention (CharacterState::isAlive's own doc comment).
    uint64_t npcReviveFromDownedCount = 0;
    uint64_t npcReviveFromDeadCount = 0;

    // persona_override_persona_start (Sec14.16): the override's own target
    // persona name - the resolver tail's (0x00462960->0x0046fd00) real
    // table contents are OPEN; same "never-populated object space, test-
    // registrable" precedent as ai_set_personality above (gated only on
    // objectResolves(), not separately modelled here).
    std::string personaOverrideName;

    // --- Batch 3 (2026-10-08), spec-lua-api-behaviour.md Sec11+Sec12 (PCR
    // quick-queries + vehicle/HUD/effect cluster) - see each stub's own doc
    // comment (lua_spec_confirmed_stubs_b10_batch3_pcr_vehicle.cpp) for the
    // full per-function citation/reasoning. ---

    // character_set_skydiving_state (Sec12.4, 0x00a45380): CONFIRMED the
    // setter 0x009aee20 is called unconditionally with the numeric state
    // code, no gate - the state code's own real enumeration is OPEN. 0 =
    // never called (this project's own "never set" sentinel, same
    // convention as chaseTargetName's "" on VehicleState, vehicle_state.h).
    int32_t skydivingStateCode = 0;

    // character_set_skydiving (Sec12.13, 0x00a42750): CONFIRMED a true/false
    // argument dispatches to two COMPLETELY DIFFERENT internal functions
    // (0x009ae810/0x009ae150, both OPEN) rather than one setter with a
    // boolean - this project tracks only the observable boolean outcome a
    // test can check, not which of the two internal functions "really" ran.
    bool skydivingOn = false;

    // character_hide (Sec12.5, 0x00a45080): CONFIRMED the SAME visibility-
    // toggle primitive (0x008ccb90) and the SAME "hide" mode-argument pair
    // mesh_mover_hide (Sec9.16, PlacedObjectState::flags082) already uses,
    // here applied to a character instead of a placed object. The attached-
    // object linked-list walk (bit 0x8@+6 class-descriptor gate) is OPEN -
    // this project has no per-character attached-object list anywhere in
    // scope, so only the character's own hide bit is tracked.
    bool hidden = false;

    // set_always_sees_player_flag (Sec12.7, 0x00a5cc80): CONFIRMED resolve +
    // dispatch to an AI-perception setter (0x004dfc80, OPEN internal/hidden-
    // argument convention) - the real construction-time default is not
    // stated anywhere in scope, so CHOSEN false (consistent with this
    // header's "unconfirmed flag defaults false, write-only in scope"
    // convention elsewhere, e.g. npcSuppressPersona above).
    bool alwaysSeesPlayer = false;

    // set_player_can_enter_exit_rc_vehicles (Sec12.27, 0x00a5d390): CONFIRMED
    // third sighting of this document's "stored as the negated sense of an
    // allow_*-style argument" convention (alongside character_allow_ragdoll
    // Sec3.16 and notoriety_set_can_decay Sec6.18) - stored INVERTED, so
    // false here means "can enter/exit RC vehicles" (CHOSEN default, no
    // stated construction value either way).
    bool rcVehiclesBlocked = false;

    // player_parachute_wear_backpack (Sec12.28, 0x00a5a380): the gate this
    // function tests is cited by address as 0x008addb0 - the IDENTICAL
    // address this struct's own controlLockGatePasses above already models
    // as "variant 2," independently CONFIRMED true in single player
    // (Sec3.7/Sec6.22, see controlLockGatePasses's own doc comment). HIGH
    // CONFIDENCE, not independently re-derived for THIS specific call site
    // (per WALLS.md's own caution against asserting shared identity from a
    // bare address match alone) - kept as its OWN field rather than
    // literally reusing controlLockGatePasses, but seeded the SAME way
    // (EngineState::getOrCreateCharacter) for the SAME reason: true selects
    // the direct-local-apply branch (Sec12.28's own text: "the change
    // applies directly and locally ... with no replication at all"); a test
    // can still .set(false) to exercise the dead (in single player) opcode-
    // 0x43 replicate-only branch.
    OpenValue<bool> parachuteBackpackGatePasses{"gate 0x008addb0 for player_parachute_wear_backpack",
                                                 "spec-lua-api-behaviour.md Sec12.28"};
    bool parachuteBackpackWorn = false;

    // =====================================================================
    // Wave 2, Batch W2-3 (2026-10-08), spec-lua-api-behaviour.md Sec20+Sec21
    // ("ranks 351-450 tranche," parts A/C gameplay residue, 57 names) - see
    // each field's own citation below; full per-native reasoning lives in
    // the stub file's own doc comments
    // (lua_spec_confirmed_stubs_b16_wave2_sec20_21.cpp).
    // =====================================================================

    // character_set_only_scripted_grabs (Sec20.26) / character_set_counter_
    // on_grabbed_by_player (Sec20.27) / character_set_only_player_grabs
    // (Sec21.8): 3 further confirmed bits on the SAME shared +0x1c98 force-
    // flag word this struct already models above (forceFlags1c98) -
    // TRANSPOSITION HAZARD, flagged explicitly by the orchestrator's own
    // dispatch: kCounterOnGrabbedByPlayerBit1c98 (Sec20.27, address
    // 0x00a42200) is a COMPLETELY DIFFERENT real function from the ALREADY-
    // REGISTERED `character_set_counter_on_grabbed` (Sec28.19, address
    // 0x00a421a0, lua_spec_confirmed_stubs_b02_sec27_28.cpp) despite the
    // near-identical name - that sibling writes CounterOnGrabbed::value[name]
    // below, a totally separate mechanism; THIS bit must never be written by
    // that other stub, and vice versa (see
    // tests/synthetic_luahost_wave2_w23_test.cpp's own dedicated cross-check
    // test).
    static constexpr uint32_t kOnlyScriptedGrabsBit1c98 = 0x2000000u;       // Sec20.26
    static constexpr uint32_t kCounterOnGrabbedByPlayerBit1c98 = 0x4000000u; // Sec20.27 - NOT CounterOnGrabbed, see above
    static constexpr uint32_t kOnlyPlayerGrabsBit1c98 = 0x8000000u;         // Sec21.8

    // character_disable_revival (Sec21.9) / character_disable_human_downed_
    // interactions (Sec20.31): 2 further confirmed bits on the SAME shared
    // +0x1c9c force-flag dword this struct already models above
    // (forceFlags1c9c, Sec13.5's own "no_explosive_panic" bit 0x4) - CONFIRMED
    // distinct bit positions, per each native's own spec text.
    static constexpr uint32_t kRevivalDisabledBit1c9c = 0x20000u;               // Sec21.9
    static constexpr uint32_t kHumanDownedInteractDisabledBit1c9c = 0x40000u;   // Sec20.31

    // npc_hearing_enable (Sec21.21): character's own "human_ai_data" sub-
    // object (+0x610/+0x614 identity pair, not separately modeled - same
    // "the write is real, the sub-object is not separately modeled"
    // convention as several fields elsewhere in this header) own +9 byte,
    // bit 0x10 ("deaf" - CONFIRMED stored as the NEGATION of the Lua
    // `enable` argument, a further instance of the "negated allow_*/
    // enable_*" convention Sec3.16/Sec6.18/Sec12.9 already establish).
    // Double-gated by the SAME forceFlagGatePasses double-gate above
    // (CONFIRMED true in single player).
    OpenBits32 humanAiDataForceFlagsByte9{"character human_ai_data sub-object (+0x610/+0x614) own +9 byte",
                                          "spec-lua-api-behaviour.md Sec21.21"};
    static constexpr uint32_t kDeafBit = 0x10u;

    // npc_combat_enable (Sec3.17), batch 2026-10-08 (the 6-native clearance
    // fast-follow): a SIBLING byte on the SAME "human_ai_data" sub-object
    // humanAiDataForceFlagsByte9 above already models (RESOLVED 2026-10-08
    // by independent direct disassembly: `0x004e0cf0`'s hidden `this` is
    // the character's AI-data sub-object at handle offset `+0x2b0` -
    // identical sub-object npc_hearing_enable's own `0x004e0e70` writes,
    // confirmed by the shared `+0x610`/`+0x614` identity-pair read inside
    // the record-open branch) - but this native's own bit is at the
    // sub-object's `+8` byte (absolute character `+0x2b8`), NOT the `+9`
    // byte npc_hearing_enable uses, so a separate OpenBits32 field is
    // needed (this header has nowhere else modeled a byte-addressable
    // sub-object as two independent OpenBits32 fields rather than one
    // wider one - kept this way deliberately, matching the project's own
    // "the write is real, the sub-object itself is not separately modeled"
    // convention: there is no shared backing storage to alias between the
    // two bytes). kCombatDisabledBit is stored in the bit's OWN sense
    // ("combat disabled") - CONFIRMED the real call site passes the
    // LOGICAL NEGATION of the Lua `enable` argument (same negated-argument
    // convention as kNeverRagdollBit1c98 above). Same forceFlagGatePasses
    // double-gate as every other force-flag field in this struct.
    OpenBits32 humanAiDataForceFlagsByte8{"character human_ai_data sub-object (+0x610/+0x614) own +8 byte",
                                          "spec-lua-api-behaviour.md Sec3.17"};
    static constexpr uint32_t kCombatDisabledBit = 0x10u;

    // npc_refire_rate_override (Sec21.20): bare, ungated, non-replicated
    // direct write to the character's own +0x1512 word field. 0xffff ("no
    // override") CONFIRMED sentinel default.
    uint16_t npcRefireRateOverride = 0xffffu;

    // human_skydive_create_npc_backpack (Sec20.20): capability gate (class-
    // descriptor row +0xa bit 0x2, "can carry a backpack") - this project's
    // flat objectResolves() cannot carry a per-kind descriptor bit (Sec29's
    // "one shared global name map" simplification), so this is its OWN
    // per-character OpenValue, OPEN until a test sets it (no spec gives a
    // real default per character class).
    OpenValue<bool> canCarryBackpack{"character class-descriptor bit (+0xa bit 0x2, 'can carry a backpack')",
                                     "spec-lua-api-behaviour.md Sec20.20"};

    // character_is_jumping (Sec20.28) / character_ignite (Sec20.29): BOTH
    // natives' own spec text independently cites the SAME real offset
    // (+0xccd) tested against the SAME literal value (4) - Sec20.28 calls it
    // the "jump" state, Sec20.29 calls it "downed" - most plausibly one
    // shared movement/action-state enum where value 4 happens to matter to
    // both readers (not independently reconciled this pass, but modeled as
    // ONE shared field rather than two coincidentally-identical ones, since
    // inventing a second field at the identical cited offset would itself be
    // the less faithful choice). OPEN until a test sets it.
    OpenValue<int32_t> movementStateByteCcd{"character movement/action-state byte (+0xccd)",
                                            "spec-lua-api-behaviour.md Sec20.28/Sec20.29",
                                            0}; // spec a166141: spawn default 0 (none/idle, ctor 0x00954340)
    // character_is_jumping's OWN additional pre-check (arg2=true path only):
    // a SEPARATE movement-state byte (+0xccc, CONFIRMED distinct offset from
    // +0xccd above) tested for value 3, AND an exclusion predicate
    // (0x009b9160) independently cross-referenced elsewhere in this same
    // spec batch (Sec21.11) as "+0x16d4 == 3, specifically 'seated in a
    // vehicle'" - modeled as its own dedicated field (NOT shared with
    // CharacterState::currentVehicleName above, a genuinely different real
    // offset/mechanism: a cached vehicle-association handle vs. a bare
    // occupancy-phase enum).
    OpenValue<int32_t> movementStateByteCcc{"character movement-state byte (+0xccc)",
                                            "spec-lua-api-behaviour.md Sec20.28"};
    // character_is_jumping's own "seated in a vehicle" test (Sec20.28/
    // Sec21.11) reuses stateEnum directly (above, +0x16d4) rather than a
    // second, duplicate field for the SAME real byte - see
    // stub_character_is_jumping's own doc comment (lua_spec_confirmed_
    // stubs_b16_wave2_sec20_21.cpp) for the full post-merge correction note.

    // character_is_in_a_driver_seat (Sec21.26): the real body is a 3-hop
    // chain (identity-key hash-bucket lookup against a fixed tag, a cached-
    // vehicle-association compare, then up to 3 separate seat-occupant
    // accessors, NONE of whose own bodies were independently decompiled this
    // pass - OPEN per the spec's own text) with no other in-scope reader of
    // any intermediate value - abstracted to ONE flag, the final comparison
    // outcome, the same "abstract an unconfirmed multi-step chain to one
    // flag" convention CounterOnGrabbed::gatePasses/the vehicle double-gate
    // already establish elsewhere in this header. Gated additionally on
    // CharacterState::currentVehicleName above being known and non-empty
    // (the "trust a known cached association, skip the further OPEN gates"
    // convention get_char_vehicle_name/Sec12.9 already established).
    // ANSWERED 2026-10-15 (spec 21.26 blocker dlc2_m01, FINAL): the comparison is VehicleState::
    // seat0HoldsCharacter (claim / pending / seated handle of seat 0), no longer a single abstract flag.

    // character_ignite (Sec20.29): dword +0xe8 bit 0x100 ("on fire") - a NEW
    // dword this header has not previously modelled (distinct from +0x1c98/
    // +0x1c9c/+0xec/+0xe4, all CONFIRMED-distinct offsets elsewhere in this
    // header). The fixed 12-simultaneous-fires cap is modeled at the
    // EngineState level (charactersOnFire_ below), not here.
    OpenBits32 flagsE8{"character dword +0xe8", "spec-lua-api-behaviour.md Sec20.29"};
    static constexpr uint32_t kOnFireBit = 0x100u;
    uint64_t igniteCatchFireReactionCount = 0; // 0x00986920 dispatch count (internals OPEN)
    uint64_t igniteHookNotifyCount = 0;        // 0x009e6510 dispatch count, when a hook partner is present instead

    // character_has_human_shield (Sec20.30): the real body is an intricate
    // multi-field chain (+0x1898/+0x189c, +0x1890/+0x1894, +0x18dc/+0x18dd,
    // +0x18d4, a partner-resolution helper 0x009aa130 never independently
    // decompiled) with no other in-scope reader of any intermediate field -
    // abstracted to ONE flag, same convention as inDriverSeatSpecifically
    // above. OPEN until a test sets it.
    OpenValue<bool> hasActiveHumanShieldRelationship{"character_has_human_shield's own final exempt-bit-negation outcome",
                                                     "spec-lua-api-behaviour.md Sec20.30"};

    // human_enter_downed (Sec20.21): reuses CharacterState::lifeState above
    // (+0xcc8, CONFIRMED 6 = "downed") for the "already downed" pre-check.
    // The authoritative-branch predicate 0x00973d40 has no other in-scope
    // reader - abstracted to one OPEN flag (this project's standing single-
    // player simplification: the non-authoritative network-record branch is
    // dead code here, same as the force-flag double-gate family, so only
    // the authoritative branch's own effect is modeled).
    OpenValue<bool> enterDownedApplyGate{"human_enter_downed's own authoritative-branch predicate (0x00973d40)",
                                         "spec-lua-api-behaviour.md Sec20.21"};
    double lastEnterDownedDuration = -1.0; // raw arg 2, CONFIRMED sentinel default
    uint64_t enterDownedAppliedCount = 0;
    // Downed-state expiry (spec-ai-runtime.md Sec21.10.1 and the type-0 note before it). PORT RULE (CHOSEN): ambient and
    // mission NPCs use downed type 0 (stand up and heal at the deadline); types 1-4 only end the downed state at the
    // deadline (release the downed marker, stance 0, clear the downed flag). A downed NPC dies only on further damage (the
    // normal death path). downedDeadlineSeconds < 0 = no deadline armed. The duration argument of human_enter_downed is
    // taken as milliseconds (CHOSEN; the deadline setter 0x00d9e140 works in ms).
    int32_t downedType = 0;
    // Sec21.10.1 (FINAL) downed entry on a lethal hit. Selector inputs are HOST FACTS (default false = no downed state):
    // nothing in this project's data yet says which spawned NPC is a gang brute / gang member / follower (the descriptor
    // +0x48 bit 0 "is a brute" is HIGH-read only), so the host or a test sets them. bleedOutHitPoints is the record's
    // Bleed_Out_Hit_Points (+0x0C); unset = OPEN, and a type-1 entry is then refused (plain death, nothing invented).
    sr3airuntime::combat::DownedSelectorInputs downedFacts;
    std::optional<int32_t> bleedOutHitPoints;
    bool downedBurning = false;                 // type-1 predicate: burning (0x0097cdd0)
    bool downedAttackerBruteBit7 = false;       // type-1 predicate: killer is a live brute with +0x1568 bit 7
    int32_t downedBuffer = 0;                   // type 1: overkill buffer word +0x18e4 (Sec21.10.1)
    bool downedBufferKnown = false;             // false: no Bleed_Out_Hit_Points, so a type-1 hit cannot be applied (OPEN)
    double downedDeadlineSeconds = -1.0;
    uint64_t downedMarkerReleasedCount = 0;
    uint64_t downedReviveAtExpiryCount = 0;  // type 0
    uint64_t downedEndedAtExpiryCount = 0;   // types 1-4

    // on_collision (Sec21.1): PRIMARY path only (generic/`"#PLAYER#"`-
    // resolved object, hook code 0x13 at its own +0x1f00 sub-record) - see
    // PlacedObjectState's own doc comment for why the spec's own SEPARATE
    // vehicle-fallback branch is not modeled.
    std::string onCollisionCallback;

    // on_weapon_equip (Sec21.18): hook code 7, the SAME +0x1f00-anchored
    // sub-record as onCollisionCallback above. NO vehicle-fallback branch
    // (CONFIRMED, unlike on_collision).
    std::string onWeaponEquipCallback;

    // inv_item_is_equipped (Sec20.16) / inv_item_get_equipped_item (Sec20.17)
    // / set_script_animation_group (Sec21.15) / inv_item_equip_slot
    // (Sec21.23): all FOUR natives' own spec text cites the IDENTICAL
    // underlying accessor (0x005fe3e0, hidden `this` = character+0x1b78,
    // flag=false) for "the character's own currently-equipped item" - ONE
    // shared field, not four independent ones (Sec21.15's own text: "the
    // SAME accessor item 21.15's own hidden-`this` correction identifies").
    // "" = nothing equipped.
    std::string equippedItemName;

    // inv_has_item (Sec20.18): this project has no real inventory-contents
    // model (same "no generic entity shape invented" boundary this header's
    // own top note states elsewhere) - a plain map, CHOSEN false default
    // ("does not have the item" is the sane zero-default for an unmodeled,
    // empty-by-default inventory), not an OpenValueMap/OPEN refusal, since
    // "has none of anything" is itself a reasonable, labeled CHOSEN starting
    // state, not a genuinely unknown engine value.
    std::unordered_map<std::string, bool> inventoryItemPresence;

    // inv_item_remove_in_slot (Sec21.4) / inv_item_equip_slot (Sec21.23):
    // per-slot contents, keyed by the RESOLVED NUMERIC slot id (not the slot
    // NAME - both natives' own arg 1 is a slot name/selector, converted to a
    // numeric id first, per each native's own text). "" = slot empty. CHOSEN
    // naming note: this field is intentionally named distinctly from any
    // OTHER wave-2 batch's own `inv_item_remove` (a DIFFERENT, non-slotted
    // native, different spec entry, different batch/file entirely) so the
    // two cannot collide even informally.
    std::unordered_map<int32_t, std::string> inventorySlotContentsBySlotId;
    uint64_t equippedCount = 0;                       // inv_item_equip_slot: real re-equip happened
    uint64_t reequipSkippedAlreadyEquippedCount = 0;  // inv_item_equip_slot: new selection == already-equipped item

    // set_script_animation_group (Sec21.15): the character's own +0x178
    // animation-group id (via its own +0x1da8 controller sub-object, de-
    // duplicating setter 0x004b2140 - only written on a real value change,
    // CONFIRMED). -1 CHOSEN ("never set"). animationGroupPropagatedToWeaponCount
    // counts the SAME de-duplicating write applied to the currently-equipped
    // item's own listener (+0xe8), when one is held (equippedItemName above);
    // scriptAnimationGroupReplicateCount counts the UNCONDITIONAL opcode-0x43
    // record this native's own text documents ("no gate check of any kind
    // found anywhere in the body").
    int32_t scriptAnimationGroupId = -1;
    uint64_t animationGroupPropagatedToWeaponCount = 0;
    uint64_t scriptAnimationGroupReplicateCount = 0;

    // npc_use_closest_action_node_of_type (Sec21.2): the character's own
    // +0x16c/+0x170 search-request fields (radius/type id), written only on
    // a successful resolve+type-validate (CONFIRMED). The derived +0x16a
    // internal success byte has no in-scope reader (the pushed Lua boolean
    // is a hardcoded `true`, CONFIRMED NOT the same value - Sec21.2's own
    // "real, confirmed divergence" finding) so it is not modeled; the
    // unconditional opcode-0x43 network record (success-branch-scoped, per
    // this native's own text) is tracked as a plain counter.
    double lastActionNodeRadius = 10.0; // CONFIRMED nil-gated default
    int32_t lastActionNodeTypeId = -1;
    uint64_t actionNodeNetworkRecordCount = 0;

    // action_stop_custom (Sec21.28): class-descriptor row +0xa bit 0x2 -
    // the SAME bit action_play_is_finished's own "+10 bit 0x02" cites
    // (Sec7.21/Sec7.33), per Sec21.28's own text - NOT independently
    // cross-wired to whatever field (if any) that sibling native's own
    // implementation uses (out of this batch's scope to verify/reconcile;
    // kept as its own dedicated field rather than guess at reuse).
    OpenValue<bool> actionStopCustomCapabilityBit2{"character class-descriptor row +0xa bit 0x2",
                                                   "spec-lua-api-behaviour.md Sec21.28"};
    uint64_t actionStopCustomCallCount = 0;
    uint64_t actionStopCustomInternalDispatchCount = 0; // the capability-gated 0x009e3200 dispatch

    // =====================================================================
    // Wave 2, Batch W2-4 (Sec22+Sec24+Sec25 gameplay residue, 78 names,
    // 2026-10-08) - see src/lua_spec_confirmed_stubs_b22_wave2_sec22.cpp and
    // src/lua_spec_confirmed_stubs_b23_wave2_sec24_25.cpp's own per-stub doc
    // comments for the full citation/reasoning behind each field below.
    // =====================================================================

    // npc_dont_drop_weapon_on_death (Sec22.12): a NEW byte +0x1e00, joining
    // the already-established "human_force_flags"/"vai_force_flags"/
    // "sync_flags"/"vehicle m_force_flags" double-gate tag-pair families
    // with a FOURTH, byte-confirmed "npc" (0x01167294) + "force_flags..."
    // family - same double-gate (variant 1, forceFlagGatePasses above)
    // shape, a new field because it is a genuinely different byte from
    // forceFlags1c98/forceFlags1c9c above.
    OpenBits32 npcForceFlags1e00{"character npc-force-flag byte +0x1e00 (dont_drop_weapon_on_death bit 0x20)",
                                 "spec-lua-api-behaviour.md Sec22.12"};
    static constexpr uint32_t kDontDropWeaponOnDeathBit1e00 = 0x20u;

    // set_suppress_synced_melee_attacks_flag (Sec24.16) / player_super_powered
    // (Sec24.19): 2 more confirmed bits on the ALREADY-MODELLED forceFlags1c98/
    // forceFlags1c9c shared bytes above (Sec3.7/Sec7.20/Sec7.26/Sec7.29) - new
    // tag-pair members "human_force_flagssuppress_synced_melee_attacks" and
    // "human_force_flagssuper_powered", same double-gate shape, no new field
    // needed beyond these 2 constants.
    static constexpr uint32_t kSuppressSyncedMeleeAttacksBit1c98 = 0x40000000u;
    static constexpr uint32_t kSuperPoweredBit1c9c = 0x80000u;

    // npc_weapon_spread (Sec24.26): a NEW float field +0x1e04, SAME "npc"+
    // "spread_multiplier" double-gate family as npcForceFlags1e00 above (a
    // different byte again) but carrying a FLOAT payload, not a bit - this
    // project's own established "non-boolean double-gate payload" shape
    // (cf. VehicleState::explosionDamageMultiplier below, Sec24.6). CHOSEN
    // default 1.0 (Sec24.26's own nil-gated-argument default, byte-confirmed
    // 0x01117a4c) - plain, not OpenValue, since the double-gate always
    // passes in single player (Sec3.7) and no in-scope reader needs a
    // refusal here.
    double npcSpreadMultiplier1e04 = 1.0;

    // human_set_force_holster (Sec25.9): writes DIRECTLY on the resolved
    // character handle itself (CONFIRMED, a real asymmetry against human_
    // set_always_sprint below, which instead targets the +0x2b0 AI-data
    // sub-object) - CHOSEN default false, no in-scope reader beyond this
    // batch's own test.
    bool forceHolster = false;

    // human_set_always_sprint (Sec25.10) / character_set_can_take_human_
    // shield (Sec25.26): 2 more confirmed bits on the per-character AI-data
    // sub-object at +0x2b0 (joining alwaysCowerFlag above, Sec15.4, and the
    // ignoreAI/actionOverrideId fields at the TOP of this
    // struct, which already share this exact +0x2b0 base) - CHOSEN
    // default false/true respectively (no CONFIRMED construction value for
    // either), plain bools, same "no in-scope reader beyond this batch's own
    // test" convention. character_set_can_take_human_shield's own real
    // write is CONFIRMED stored in the NEGATED sense (Sec25.26) - this
    // project keeps the NATURAL (non-negated) "can take shield" sense in
    // this field instead, diverging from the real bit's negated storage;
    // the stub's own write site negates the Lua argument when writing the
    // real bit, so a test reading THIS field sees the natural sense while
    // the real engine's own bit would read inverted (post-merge correction -
    // this comment previously, wrongly, said the negation happens "only at
    // the stub's own write site" as if this field itself stayed negated;
    // it does not - a fresh reviewer confirmed no negation happens anywhere
    // against this field, only (correctly) at the real-bit write site).
    bool alwaysSprintAiData2b0 = false;
    bool canTakeHumanShield = true; // CHOSEN default true ("can" by default)

    // character_release_human_shield (Sec22.26): reuses humanShieldHostageName
    // above directly (already modelled, Sec45.1, "the taker's own held-
    // hostage name") as the single "is this character currently holding a
    // hostage" state; this project does not model the real shield-state
    // byte's own full enum (+0x18d4, no in-scope reader needs it) - only
    // per-branch call counts, CHOSEN record-the-request convention (same as
    // MinimapIconRecord/ObjectIndicatorRecord elsewhere in this header).
    uint64_t humanShieldForcedEjectCount = 0;
    uint64_t humanShieldDelayedReleaseCount = 0;
    uint64_t humanShieldNormalReleaseCount = 0;

    // is_rappelling (Sec22.16): 2 NEW fields, +0xcc8 (state code, true only
    // at 0x12/18) and +0xcce (a companion byte, true only at '#'/0x23) - no
    // CONFIRMED construction default given anywhere in Sec22.16's own text;
    // CHOSEN to values that never match the predicate absent a test
    // explicitly configuring them, matching this project's established
    // "zero-filled, no writer in scope -> doesn't trip" convention (e.g.
    // CharacterState::lifeState/killFinalizedFlagE3 above, Sec34.5).
    int32_t rappelStateCodeCc8 = 0;
    char rappelCompanionByteCce = 0;

    // check_animation_state (Sec22.25): a NEW field +0x1cc4 ("the character's
    // own cached current animation-state id"), compared against EngineState::
    // lookupAnimationStateId(stateName) - the SAME 0x004bf810 shared name->
    // id table stand-in Sec7.11/Sec7.17/Sec25.24 already share (EngineState::
    // registerAnimationStateIdForTesting/lookupAnimationStateId above).
    // CHOSEN default (-2) deliberately distinct from that shared table's own
    // "-1, not found" sentinel, so an unconfigured character can never
    // spuriously equal an unconfigured/unregistered state name's own lookup
    // result.
    int32_t currentAnimationStateId = -2;

    // check_script_animation_state (Sec25.24): a pure READER of
    // scriptAnimationStateId/scriptNpcBound above (already modelled for
    // set_script_animation_state, Sec7.17) - no new field needed; see the
    // stub's own doc comment for the exact field-level match this closes.

    // cower (Sec22.22): per-character "cower" AI reaction state - the
    // target/threat position is NOT modelled (no coordinate system
    // anywhere in this project, the stated project-wide gap), only the
    // CONFIRMED structural facts: whether the state actually took, who the
    // threat was, and the 2 packed boolean modifiers (+0x4a5/+0x4a6, real-
    // world meaning HYPOTHESIS per Sec22.22's own text).
    bool cowering = false;
    std::string cowerThreatName;
    bool cowerArg3 = false;
    bool cowerArg4 = false;
    uint64_t cowerCount = 0;

    // wander_start (Sec24.4): CHOSEN "record the request, not the real
    // position/orientation arithmetic" convention (same as HelicopterFlyTo::
    // Request elsewhere in this header) - default height -1.0 matches
    // Sec24.4's own nil-gated-argument default (byte-confirmed 0x012a2d54).
    std::string wanderTargetName; // "" = sourced from the character's own default (+0x2b0), per Sec24.4
    double wanderHeight = -1.0;
    uint64_t wanderStartCount = 0;

    // player_supress_falling_scream (Sec24.18): bit 0x80 of +0x1d99 -
    // CONFIRMED "always-set, never-clear from this one entry point" (a
    // real clearer exists elsewhere in the binary, 0x00e0e919, out of this
    // batch's scope per Sec24.18's own correction). CHOSEN default false.
    bool fallingScreamSuppressed = false;

    // npc_unholster_best_weapon (Sec24.27): dispatches through the already-
    // catalogued +0x1b78 "equipment" sub-object (a THIRD confirmed consumer,
    // Sec24.27's own text) - 0x00600720's own ~2,900-instruction body is OPEN
    // (not independently decompiled), so only that a resolved character's
    // dispatch ran is counted, honestly.
    uint64_t unholsterBestWeaponCount = 0;

    // human_riding_trigger_end (Sec25.11): a predicate-gated end action; this
    // project has no in-scope setter for the real predicate (0x009a8430), so
    // it stays a plain, test-settable bool (CHOSEN default false - "not
    // currently riding/triggered", matching the common "never configured"
    // case).
    bool ridingTriggerActive = false;
    uint64_t humanRidingTriggerEndCount = 0;

    // inv_item_is_firearm_equipped (Sec25.8): a NEW per-character query flag,
    // CHOSEN default false (0x009ba7e0's own internal check is OPEN).
    bool firearmEquipped = false;

    // force_ai_mode_check_done (Sec25.19): the per-character +0x4aa pending-
    // request tag, CHOSEN plain bool stand-in (no in-scope setter for the
    // real 16-bit tag or the shared queue-lookup helper 0x006f63c0, whose own
    // polarity is flagged OPEN/tension against Sec7.28 by the spec itself) -
    // default false ("no pending tag"), matching the common "never issued a
    // force-AI-mode request" case.
    bool forceAiModePendingTagSet = false;

    // character_is_released (Sec25.28): the per-character vtable+0x70 "is
    // bound/possessed by something" predicate this project does not model a
    // real vtable for - CHOSEN plain bool, default false ("not bound"), so an
    // unconfigured, resolved character reads as "released" (true) by
    // default, matching Sec25.28's own stated "including on any resolve/
    // liveness failure -> true" default-safe direction.
    bool boundOrPossessed = false;

    // character_set_combat_ready (Sec25.25): single-gate (0x008addb0,
    // reusing forceFlagGatePasses as this project's one shared double-gate/
    // single-gate stand-in, same simplification this header uses throughout)
    // "combat ready" state plus an optional numeric mode parameter. CHOSEN
    // defaults (false/0) - no CONFIRMED construction value stated.
    bool combatReady = false;
    int64_t combatReadyMode = 0;
    uint64_t combatReadySetCount = 0;

    // =====================================================================
    // Wave 2, Batch W2-7 (2026-10-08): spec-lua-api-behaviour.md Sec38-60 -
    // see src/lua_spec_confirmed_stubs_b27_wave2_spawn_world_audio.cpp.
    // =====================================================================

    // world_despawn_all_pedestrians (Sec46.3): same "deferred despawn,
    // protected entries skipped" shape as VehicleState::despawnPlayerOccupied
    // et al. above - CHOSEN false defaults (an ordinary ambient pedestrian
    // is not protected).
    bool despawnProtected = false;
    bool despawnedPendingDelete = false;

    // force_throw_do / force_throw_char_do (Sec55.4): "requires the
    // character to have a throwable item equipped/held." This project has
    // no real inventory/equip model (stated gap, same class as every other
    // "no item-table" boundary elsewhere) - test-settable, OPEN until set;
    // a real call against a character with no override throws
    // OpenStateError (honest refusal, not a fabricated "always equipped").
    OpenValue<bool> hasThrowableItemEquipped{"character has a throwable item equipped/held (Sec55.4)",
                                            "spec-lua-api-behaviour.md Sec55.4"};

    // =====================================================================
    // Wave 2, Batch W2-2 (spec-lua-api-behaviour.md Sec17+Sec18, 2026-10-08)
    // - see src/lua_spec_confirmed_stubs_b21_wave2_sec17_18.cpp's own doc
    // comment above each stub_* function for the full citation/reasoning.
    // =====================================================================

    // set_perfect_aim (Sec17.3): an EIGHTH confirmed bit on the shared
    // +0x1c98 flags field (forceFlags1c98 above already models 0x20/0x200/
    // 0x800/0x80/0x10/0x1000/0x4000) - gated by the SAME forceFlagGatePasses
    // double-gate (Sec3.7).
    static constexpr uint32_t kPerfectAimBit1c98 = 0x100u;
    // character_set_always_combat_ready (Sec18.17): a further bit on the
    // SAME +0x1c98 field.
    static constexpr uint32_t kAlwaysCombatReadyBit1c98 = 0x1u;
    // set_suppress_synced_attacks_flag (Sec18.22): a further bit on the
    // SAME +0x1c98 field.
    static constexpr uint32_t kSuppressSyncedAttacksBit1c98 = 0x20000000u;
    // character_make_priority_target (Sec18.18): a further bit on the SAME
    // +0x1c98 field, PLUS an always-written (regardless of the bit's own
    // value) squared-radius field at +0x1ca4 - CONFIRMED asymmetry: the
    // local-apply path stores the radius SQUARED; the network-replicate
    // path (not modeled beyond EngineState::replicateStateChange) carries
    // it un-squared (Sec18.18's own flagged, unresolved receiving-side
    // question).
    static constexpr uint32_t kPriorityTargetBit1c98 = 0x400u;
    double priorityTargetRadiusSquared = 0.0;

    // character_set_never_fall (Sec17.10) / character_set_immune_to_
    // explosion_vomit (Sec18.16): two further bits on the EXISTING shared
    // +0x1c9c dword (forceFlags1c9c above already models bit 0x4,
    // no_explosive_panic, Sec13.5).
    static constexpr uint32_t kNeverFallBit1c9c = 0x10000u;
    static constexpr uint32_t kImmuneToExplosionVomitBit1c9c = 0x1000u;

    // character_set_attack_multiplier (Sec17.11): object +0x1cc0, a raw
    // value field (not a bit) - gated by the SAME double-gate as every
    // other force-flag setter in this struct. CHOSEN default 1.0 ("no
    // scaling" - no stated construction value is given anywhere in scope).
    double attackMultiplier = 1.0;

    // set_attack_enemies_flag (Sec17.5): structurally identical to
    // set_attack_player_flag's own documented "two flag locations, only one
    // replicates" split (attackPlayerBitAc/attackPlayerSecondaryGatePredicate/
    // attackPlayerOnSightBit9 above, Sec7.10) - this is its own, independent
    // sibling instance, NOT a re-use of those fields (a DIFFERENT bit of the
    // SAME +0xac byte, and a DIFFERENT derived "ai_data" sub-object byte,
    // +9 bit 0x2 rather than +9 bit 0x1). The +0xac bit write is CONFIRMED
    // unconditional (no gate at all, written before the vtable +0x68
    // predicate below is even evaluated).
    bool attackEnemiesBitAc = false;
    OpenValue<bool> attackEnemiesSecondaryGatePredicate{
        "character vtable +0x68 predicate for set_attack_enemies_flag (unidentified)",
        "spec-lua-api-behaviour.md Sec17.5"};
    // The derived "human_ai_data" sub-object's own +9 byte - a DIFFERENT
    // byte from aiDataFlags0xa below (+0xa, set_cant_cower_flag, Sec17.20).
    // Gated by forceFlagGatePasses directly (the SAME 0x008ae480/0x008837a0
    // pair, Sec3.7 - the sub-object base differs but the gate predicate
    // itself is the one shared project-wide primitive).
    OpenBits32 aiDataFlags9{"character derived human_ai_data sub-object byte +9",
                            "spec-lua-api-behaviour.md Sec17.5"};
    static constexpr uint32_t kAttackEnemiesOnSightBit9 = 0x2u;

    // set_cant_cower_flag (Sec17.20): object +0xa bit 0x8 - a THIRD
    // confirmed bit on that byte (followerCanAbandon's own 0x2, Sec7.6;
    // cantFleeFlag's own 0x20, Sec14.26) - kept as its own plain field (same
    // "independent fields on the same byte" convention those two
    // establish), gated by forceFlagGatePasses (identity +0x610/+0x614,
    // same "human_ai_data" sub-object delegate family).
    bool cantCowerFlag = false;

    // player_force_vehicle_seat (Sec17.6): two independent global "forced
    // seat" state fields (Sec17.6's own flagged item: "not gated on each
    // other"). The seat-index half is per-target (this project's own
    // CharacterState, gated by forceFlagGatePasses); the vehicle-identity
    // half is EngineState::forcedSeatVehicleName() below (tracked globally,
    // since the spec's own text runs it "independently of whether the
    // target resolved" - its own real `this` object is not confirmed to be
    // the same target character). CHOSEN -1 = "never set".
    int32_t forcedVehicleSeatIndex = -1;

    // character_set_persona (Sec17.9): the persona name/id this project
    // tracks directly as a string (no real name->id hash table, 0x0046fd00,
    // modeled - same "no fixed id table" class of gap as several other
    // name-lookup natives in this header). Gated by controlLockGatePasses
    // directly (the SAME single-gate 0x008addb0 "variant 2" the literal `0`
    // argument at this call site always satisfies, per the stub's own doc
    // comment).
    std::string currentPersonaName;
    uint64_t personaApplyCount = 0;

    // character_ragdoll (Sec17.12): no real physics system in this project
    // (same stated gap as vehicle_chase's own spotlight/position gaps) -
    // only the raw request is tracked. -1 = never called; 0 = last call
    // cleared the ragdoll.
    int64_t ragdollDurationMs = -1;
    double ragdollImpulseMagnitude = 0.0;
    bool ragdollImpulseWasDirectional = false; // true = a resolved source object drove the impulse direction
    uint64_t ragdollStartCount = 0;
    uint64_t ragdollClearCount = 0;

    // character_add_prop (Sec17.13): no real worn-prop object graph in this
    // project - only the resolved, successfully-attached prop names are
    // recorded (same "record the request" convention as
    // ObjectiveTextIndicatorRecord above). Gated on EngineState::
    // propDefinitionResolves() (collapsing the two real lookups,
    // 0x006cf0c0/0x00904c10, into one flat predicate).
    std::vector<std::string> attachedProps;

    // inv_item_remove (Sec17.25): no real inventory list in this project
    // (write-only in scope - nothing reads a character's inventory back) -
    // only that a removal was attempted, and via which path, is counted.
    // Gated by forceFlagGatePasses (variant 3, single-gate-0x008ae480-only -
    // the SAME shared predicate npc_leash_remove/npc_leash_to_nav already
    // reuse, Sec7's own front matter: "0x008ae480 itself is not an
    // independent predicate... the SAME underlying function", CONFIRMED
    // true in single player).
    uint64_t inventoryItemRemoveAppliedCount = 0;
    uint64_t inventoryItemRemoveReplicateCount = 0;

    // set_civilians_flee (Sec17.4): the per-civilian "flee toward/away from
    // anchor" directive the mass-update loop applies via the shared +0xc68
    // AI-directive sub-record (CONFIRMED NOT vehicle-specific, Sec17.31's
    // own cross-function note) - record the request, not real physics (no
    // coordinate system anywhere in this project). fleeAnchorName empty
    // with fleeActive true = the "self-position" variant (no real anchor
    // resolved); fleeActive false = the "clear" variant.
    bool fleeActive = false;
    std::string fleeAnchorName;
    uint64_t fleeDirectiveApplyCount = 0;
    uint64_t fleeDirectiveReplicateCount = 0;

    // ai_do_scripted_take_cover (Sec18.3): the "is already fleeing/covering
    // toward the anchor" state test (0x00540b90) - collapsed to one OPEN
    // predicate per character (no spec-given construction default). Reuses
    // pendingScriptedMoveDestination (Sec3.11) directly for the resolved
    // anchor's own +0x4b0/+0x4b4 handle field - CONFIRMED the SAME physical
    // field, not a separate one.
    // ANSWERED 2026-10-15 (spec 18.3 blocker m02/m10/m12, FINAL): 0x00540b90 is NOT an "already covering" test but
    // the "cover take def" ability gate. It is false only while the NPC is inside a cover stay-down window (the
    // stay-down timer +0x968 armed and the cover definition's pop-up time not yet elapsed). An NPC never told to take
    // cover has both timers at -1 and no cover handle, so the gate is true. This host has no cover system that
    // arms the window, so the field stays false unless a test (or a future cover model) sets it.
    bool coverStayDownActive = false;
    uint64_t scriptedTakeCoverDispatchCount = 0; // 0x004f4d00(char, 8, 0) - fires on EITHER success path

    // action_stop_synced_state (Sec18.4): reuses syncedActionPartnerName/
    // syncedActionPaired (Sec14.1) directly for the "stop" side of the SAME
    // pairing action_play_synced_state establishes. The global replication-
    // mode flag (0x024d4461) this native also tests is EngineState::
    // globalReplicationModeFlag() below - CHOSEN default false (single-
    // player-only project stance), so this always takes the documented
    // "normal local path" branch.
    bool lastSyncedStopArg = false; // raw arg 2, test-observable only
    uint64_t syncedStopLocalApplyCount = 0;

    // action_play_synced_is_finished (Sec18.5): collapses the real per-
    // object animation-controller query (+0xd24->+0x10, then 0x00487e80's
    // -1 "no active state" test, else an elapsed-time compare against
    // tag 0x1d54) into two OPEN predicates per character - no spec-given
    // construction default for either.
    OpenValue<bool> syncedAnimControllerActive{"character animation-controller has an active synced state",
                                                "spec-lua-api-behaviour.md Sec18.5"};
    OpenValue<double> syncedAnimElapsedTime{"character synced-action elapsed time (controller tag 0x1d54)",
                                            "spec-lua-api-behaviour.md Sec18.5"};

    // set_animation_set (Sec18.8): the set-name itself is tracked directly
    // (no real name->id table, 0x004b16c0, modeled). The "re-apply in-
    // progress blend" branch (0x004b21a0 chain) only fires when the
    // controller already has an active instance - CHOSEN false default (no
    // stated construction value; avoids an unnecessary OPEN refusal on the
    // common fresh-character case).
    std::string animationSetName;
    bool hasActiveAnimationInstance = false;
    uint64_t animationSetReapplyCount = 0;

    // player_release_human_shield (Sec18.9): a genuinely rich state machine
    // this project only models at the observable-branch level (no real
    // position/velocity data for the "forcible eject" offset, no timer for
    // the 500-2000ms wait). "isLocalAuthority" CHOSEN true (single-player-
    // only project stance, same reasoning as every other local/network
    // authority split in this header). shieldStateByte18d4 CHOSEN 0 ("no
    // shield" - the natural default, distinct from the CONFIRMED active
    // value 1 and the CONFIRMED cooldown value 4 the setter itself writes).
    bool humanShieldReleaseIsLocalAuthority = true;
    int32_t humanShieldStateByte18d4 = 0;
    OpenValue<bool> humanShieldNormalReleaseGatesPass{
        "player_release_human_shield's own two further gates (0x0091f5e0/0x009feeb0)",
        "spec-lua-api-behaviour.md Sec18.9"};
    uint64_t humanShieldForcibleEjectCount = 0;
    // humanShieldDelayedReleaseCount/humanShieldNormalReleaseCount are NOT
    // re-declared here - this stub's own doc comment (lua_spec_confirmed_
    // stubs_b21_wave2_sec17_18.cpp) explicitly cross-references
    // character_release_human_shield (Sec22.26, above) as the same
    // underlying counters, not a separate pair.
    uint64_t humanShieldReplicateCount = 0;

    // on_incapacitated (Sec18.11): character-kind hook base +0x68/+0x6c,
    // slot 1 - same single-dedicated-field-per-hook-kind convention as
    // onDeathCallback/onRevivedCallback above.
    std::string onIncapacitatedCallback;

    // player_is_rc_driver (Sec18.10): see EngineState::isRcDriverPredicate()
    // below - kept at the EngineState level (keyed by character name) since
    // the resolved VEHICLE identity is also part of the real test and this
    // project has no per-character vehicle-association field to hang it on.

    // player_actions_disable (Sec18.27): reuses the EXISTING, previously
    // write-only actionsEnableBit20_1d99 field (Sec15.21/Sec27.x) directly -
    // this IS that field's own first real reader/writer pass. Gated by
    // controlLockGatePasses (the SAME single-gate 0x008addb0 "variant 2"
    // family, CONFIRMED true in single player). Writes `.set(true)` to
    // DISABLE (resolved 2026-10-08: the field's own "Enable" name is
    // misleading - the bit is actually disabled-sense, per spec-player-
    // runtime.md Sec2.10 "Question B" - see this field's own doc comment
    // above).
    uint64_t playerActionsDisableReplicateCount = 0;

    // --- Wave 6 spec sync (2026-10-09), spec-player-runtime.md Sec15.3a
    // ("Generic damage application, death, and ragdoll hand-off"): closes
    // stub_character_damage's own former "0x0096fa20 authoritative branch"
    // OpenStateError (src/lua_spec_confirmed_stubs_b15_wave2_core_
    // dispatchers.cpp). Health storage itself is the EXISTING maxHitPoints/
    // currentHitPoints pair above (+0x1cac/+0x1cb8, Sec7.12/Sec7.31) -
    // re-confirmed by Sec15.3a, not re-declared here. Sec15.3a describes
    // +0x1cb8 as "(float)"; this project keeps it int32 per Sec7.12's own
    // disassembly-confirmed text ("stored as a plain integer, converted to
    // float on read") - read as: the 0x0094cfc0/0x0094d520 call boundary
    // operates in float, the underlying storage stays the int32 Sec7.12
    // already pinned. Not reconciled further - a genuine, narrow tension
    // between two independently-confirmed passes, flagged rather than
    // silently resolved either way.

    // +0x1584, CONFIRMED: "a 'last damage magnitude' scratch field,
    // max(-delta, 0) of the most recent HP-reducing change." CONFIRMED zero
    // at spawn ("the base constructor zeroes these fields"). Updated by the
    // real damage-apply math in stub_character_damage; no other writer in
    // this project's scope.
    double lastDamageMagnitude = 0.0;

    // The sink's own resistance-scale modifier (Sec15.3a's own "correction
    // to the prior 'caller always computes the final amount' phrasing"):
    // "an integer percentage read off attacker+0x1cac, the same max-health
    // field reused generically here." CONFIRMED the modifier itself exists
    // and is multiplicative; OPEN what populates it for an ORDINARY
    // attacker in the real engine's own generic/reused-layout sense. This
    // project's CharacterState is explicitly "character"-shaped only (this
    // header's own top note), and an ordinary attacking character's own
    // maxHitPoints genuinely means ITS OWN hit-point cap (typically in the
    // hundreds) - reusing it directly as a percentage here, the way the
    // real engine's generic/reused-offset convention would for some OTHER,
    // non-character "kind" of instigator object, would fabricate wildly-
    // wrong scaling (a 400-HP attacker producing 400% damage) this
    // project's own model gives no evidence for. CHOSEN instead: a
    // dedicated, neutral-default (100 = no scaling) field on the
    // INSTIGATOR's own CharacterState, left for a future pass (or a test)
    // to drive non-neutral per-source resistance scaling once a real
    // producer is found - never wired to maxHitPoints.
    int32_t instigatorDamageScalePercent = 100;

    // The sink's own "detecting a lethal hit" kill-eligibility predicate,
    // 0x0096f990 (spec-player-runtime.md Sec15.3a; spec-weapons-runtime.md
    // Sec2.3's own "unidentified predicate", shared with the melee
    // finisher/takedown timing). CONFIRMED the gate exists and must pass
    // before the death-transition (0x00970e70) side-effects below run;
    // OPEN what it actually tests. CHOSEN pass-through default true (so an
    // ordinary lethal hit reaches death normally) - a test can set this
    // false to exercise "health forced to 0, but the death-transition/
    // ragdoll/finalize side-effects below do not run" as a distinct,
    // separately-observable outcome.
    bool lethalHitKillEligible = true;

    // Counts a reach of the death transition 0x00970e70's "reached
    // normally (health already at zero)" branch - the real death
    // side-effects (a virtual dispatch through the victim's own vtable
    // +0x70, a Havok on-demand clear-position raycast via 0x0075d7e0, and a
    // push-back vector). This project's sr3luahost tier has no polymorphic
    // engine-object/vtable system and no physics/Havok integration (that is
    // sr3physics's own tier, not depended on here - the same layering
    // sr3weapons/weapon_fire.h's own doc comment states for itself) - host-
    // side bookkeeping only, not a reproduction of the vtable target or the
    // ray geometry (both explicitly OPEN per spec: "concrete target not
    // resolved - no character-class RTTI/vtable dump was available").
    uint64_t deathTransitionCount = 0;

    // The ragdoll/death-animation hand-off's real decision point,
    // 0x0098ce40's per-character sub-stage byte +0xcce (5-case jump table,
    // values 8-0xc) - CONFIRMED to exist and to be the real branch point;
    // the exact case-value mapping is explicitly OPEN (spec-player-
    // runtime.md Sec15.4's own "Honest gaps" line), so it is not reproduced
    // as a raw byte here. This project instead drives the SAME two named,
    // CONFIRMED branches directly via one CHOSEN test-settable flag:
    //   - true (default): the ragdoll-activation case (readiness 0x0098cc30
    //     + eligibility 0x009a2b00, both folded into this one flag) - calls
    //     the real "activate ragdoll physics" primitive 0x009a5520(victim,
    //     1, 0). Modelled by setting ragdollStateEnumCd2 (above) to a
    //     CHOSEN sentinel value satisfying its own known "!=0 && !=6"
    //     ragdolled exclusion test (character_is_ragdolled, Sec15.14) - the
    //     real engine's own specific ragdolled enum value stays OPEN.
    //   - false: the readiness check fails. Sec15.3a names a real outcome
    //     ONLY for a locally-owned PLAYER-CLASS victim in this case (the
    //     non-ragdoll fallback: force health to 0 - already done by the
    //     main damage math - then call the finalizer 0x00952760(victim, 0)
    //     directly, "a genuine scripted/animation death"). 0x00952760 is
    //     the SAME finalizer character_kill's own arg4=false branch already
    //     models (lifeState.set(5) + killFinalizedFlagE3 +
    //     characterKillFinalizerCount, stub_character_kill, b30) - reused
    //     verbatim here, not a parallel copy, since spec confirms it is the
    //     identical function for both callers; no new field needed for
    //     this branch. A non-player-class victim with this flag false is a
    //     genuine, spec-acknowledged gap (no named outcome either way) -
    //     left unmodelled rather than guessed.
    bool ragdollActivationEligible = true;
    uint64_t deathRagdollActivateCount = 0; // the ragdoll-activation case ran (0x009a5520 stand-in)
};

} // namespace sr3luahost
