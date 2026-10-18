// EngineState slice: NPC AI, teams, parties/squads, synced actions, notoriety, spawning.
//
// Split out of engine_state.h (refactor step 3a, 2026-10-17) as a zero-behaviour-change move: every
// type, field, initialiser and comment below moved verbatim (namespace-scope types re-indented one
// level). Types that were nested in EngineState are now namespace-scope types in sr3luahost;
// EngineStateAiData re-exports each one under its old name, so `EngineState::Type` still
// resolves. EngineState inherits EngineStateAiData publicly, so `state.member`,
// `EngineState::kConstant` and `state.accessor()` are unchanged for every caller. Members that
// were private in EngineState are `protected` here (the only access change). Method
// declarations stay in EngineState (engine_state.h); bodies are in src/*.cpp.
#pragma once

#include "sr3luahost/engine_state_common.h"

namespace sr3luahost {
struct NpcAiStats {
    uint64_t ticks = 0, npcsTicked = 0, shotsFired = 0, shotsHit = 0, damageRefused = 0, skippedNoPlayerPosition = 0;
    uint64_t hostileNpcTicks = 0;               // NPC-ticks where a LIVE, AI-driven NPC was hostile to the player
    uint64_t hostileSuppressedTicks = 0;        // hostile NPC-ticks of NPCs that are dead or under ignore_ai (the AI cannot act)
    double minHostileDistance = 1e30;           // closest a hostile NPC ever was to the player (diagnostic)
    std::array<uint64_t, 10> teamNpcTicks{};     // NPC-ticks per team id 0..7 (8 unused, 9 = no/unknown team)
    double damageToPlayer = 0;
    uint64_t losChecks = 0, losBlocked = 0;                       // AI-3: rays cast by the NPC loop / answered "blocked"
    uint64_t coverSearches = 0, coverFound = 0, coverNoNode = 0;  // AI-7: Sec21.3 searches by outcome
    uint64_t coverNodes = 0;                                      // AI-7: size of the zone cover_node pool
    uint64_t coverNodesInBox = 0, coverShieldTests = 0;           // AI-7: nodes gathered by the AABB / shield rays cast
    double coverNearestNodeXZ = 1e30, coverNearestNodeDy = 0;     // AI-7 diagnostic
    uint64_t downedEntered = 0;                                   // AI-11: lethal hits that entered the downed state
};
// --- Ranking tranche 05 (Sec33), batch 2026-10-02: non-vehicle, non-
// character state. See each function's own trampoline doc comment
// (lua_spec_confirmed_stubs.cpp) for the full citation. -------------

// action_sequence_end (Sec33.3): this project has no real "scripted
// cutscene sequence" object, "tracked handle," or per-player scripted-
// camera/target field elsewhere - a self-contained, test-observable
// stand-in: a shared "sequence active" flag and per-player scripted-
// camera/target names, cleared by this call; a combined tracked-handle
// release count (the real body releases up to 2 distinct handles;
// nothing else in scope distinguishes them); a host-only broadcast
// count (opcode 0x49, byte value 2, CONFIRMED gated on coopLocalIsHost()).
struct ActionSequence {
    bool active = false;
    std::string localScriptedCameraTarget;
    std::string remoteScriptedCameraTarget;
    int trackedHandleReleaseCount = 0;
    int hostBroadcastCount = 0;
};

// Sec32.4 `party_add_do`/`npc_is_in_party`: this project's own name-keyed
// stand-in for the real party-record lookup (0x005088c0, OPEN per spec).
// `membersByLeader` is bounded to 5 real adds per call, matching the
// real function's own fixed 5-slot local array (Sec32.4's own CONFIRMED
// structural bound) - NOT the full capacity/force-override/network-
// ownership-handoff/leader-kind-bit AI setup mechanism, which has no
// real data this project can reproduce.
struct PartyState {
    std::unordered_map<std::string, std::vector<std::string>> membersByLeader;
    // CONFIRMED real stack-overrun hazard (Sec32.4/Sec32.8: a 6th-or-
    // later follower overwrites the saved return address and beyond) -
    // NOT reproduced as an actual memory-unsafe crash; only flagged and
    // counted (same "flag, don't reproduce" precedent as the other two
    // crash paths this tranche finds, Sec32.8).
    uint64_t overflowDetectedCount = 0;

    // party_set_dismissable (Sec22.8, wave 2 batch W2-4): a bare global
    // byte (0x0130af61, CONFIRMED the byte immediately adjacent to the
    // already-documented party_set_recruitable's own 0x0130af60,
    // Sec18.28) - no resolver, no gate. CHOSEN default false (no
    // CONFIRMED construction value stated).
    bool dismissable = false;

    // The 4 reserved "#FOLLOWER...#" name-resolution sentinels (spec-
    // lua-api-behaviour.md Sec3 front matter, line ~1469-1470, "Name-
    // resolution sentinels" - the 2nd-5th of the 8-sentinel CLOSED
    // inventory commit 1681666 establishes): CONFIRMED "checked by
    // `0x00a26010`, resolved through `0x00a26110` to the Nth entry of a
    // linked companion/homie list" - but the spec text never pins which
    // N each of the 4 literals maps to, and an exhaustive grep of every
    // spec-*.md for "FOLLOWER"/"0x00a26010"/"0x00a26110" (2026-10-08,
    // this field's own introduction) found no section that closes
    // either of two genuinely separate open questions:
    //
    //   (a) whether the BARE "#FOLLOWER#" literal shares the SAME
    //       resolved index as "#FOLLOWER1#" (an alias) or denotes a
    //       distinct "any/first live follower" case - Sec3's own front
    //       matter just lists all four side by side with no textual
    //       distinction given for the bare form;
    //
    //   (b) whether the numbered trio is 0- or 1-indexed into the list
    //       (does "#FOLLOWER1#" mean list entry [0] or [1]?) - this is
    //       the SAME pair repeatedly flagged, by name, as "the exact
    //       group-vs-character semantics of `0x00a26010`/`0x00a26110`"
    //       every time this document cites it (Sec14.26
    //       `set_cant_flee_flag`'s own side-effects line; a cross-
    //       function open-questions list elsewhere in the same tranche,
    //       item 5; Sec20.23 `get_team`'s own 4-stage-cascade writeup)
    //       without ever being closed by a later section, even after
    //       the 2026-10-08 sentinel-inventory closure pass (which closed
    //       WHICH 8 strings exist, not what each one indexes to).
    //
    // Per this project's own "no invented fixes" house rule, NEITHER
    // question is decided here with a guessed formula (e.g. "numbered
    // literals are 1-indexed, bare means index 0" would be exactly as
    // plausible, and exactly as unconfirmed, as the opposite choice).
    // Instead each of the 4 literals gets its OWN independent OpenValue
    // slot, keyed by the exact literal string - deriving #FOLLOWER2#/
    // #FOLLOWER3# as some fixed offset from #FOLLOWER1#, or aliasing
    // bare #FOLLOWER# to #FOLLOWER1#, would itself be inventing the
    // very relationship (a)/(b) leave open. A slot stays OPEN (throws
    // OpenStateError, naming the literal, via OpenValueMap<int>::get())
    // until a test - or a future spec update - sets it explicitly;
    // resolveFollowerAwareObject() (lua_spec_confirmed_stubs_common.h)
    // never defaults one.
    //
    // Backing list: CHOSEN as this SAME struct's own `membersByLeader`
    // (party_add_do, Sec32.4), keyed by this project's single-player
    // leader literal "#PLAYER1#" - HIGH CONFIDENCE, NOT independently
    // spec-confirmed to be the SAME physical list `0x00a26010`/
    // `0x00a26110` walk (the spec's own words are just "a linked
    // companion/homie list," never cross-referenced by address/offset
    // to party_add_do's own fixed 5-slot array - a genuinely separate
    // OPEN identity question from (a)/(b) above, documented here rather
    // than silently assumed). The reuse follows this project's OWN
    // existing precedent, not a new assumption invented for this field:
    // lua_spec_confirmed_stubs_b15_wave2_core_dispatchers.cpp's own
    // `isFollowingALeader()` already treats `membersByLeader` generically
    // as "is this character a follower of anyone" for the Sec38.1 "enter
    // flips the follower flag" side effect, not narrowly scoped to
    // `party_add_do`'s own text either.
    OpenValueMap<int> followerSentinelIndex{"follower sentinel index",
                                             "spec-lua-api-behaviour.md Sec3 front matter (line ~1469-1470)"};
};

// spawn_override_set_override_category_for_hood (Sec49.2,
// `0x00a20840` gameplay registrar, `gameplay`). 2 strings
// (neighbourhood, category name). CONFIRMED real defect: the
// category-name lookup result is never checked before being stored
// and dispatched; on a host with a session, the dispatch inside THIS
// call immediately dereferences the null category; everywhere else
// the null is merely stored (dereferenced later by the per-hood
// reader or the co-op join-snapshot serializer, both out of this
// batch's own scope - not modeled here, since nothing in THIS
// function crashes on that path). Direct contrast (Sec49.2): the
// sibling global setter `spawn_global_override_set_category` DOES
// check its own lookup - that sibling is NOT "fixed" onto this one;
// `categoryNameResolves` is guard-checked here ONLY for the
// immediate-dispatch host branch, faithfully leaving the per-hood
// setter as the one missing its sibling's guard.
struct SpawnOverride {
    OpenValueMap<bool> categoryNameResolves{"spawn-override category-name lookup resolves",
                                             "spec-lua-api-behaviour.md Sec49.2"};
    std::unordered_map<std::string, std::string> perHoodCategory; // CHOSEN: last successfully-resolved category, by hood
    std::unordered_set<std::string> perHoodCategoryNullStored;    // CHOSEN: hoods currently holding the real defect's unchecked null (deferred read, not modeled further)
    uint64_t hoodCategoryImmediateNullDerefGuardCount = 0; // HOST-SAFETY - test-observable only, not a real engine field
};

// squad_enable (Sec49.3, `gameplay`). 1 character name + 1 boolean
// (CONFIRMED default false). CONFIRMED real asymmetric-effect defect:
// `squad_enable(name, false)` actually dismisses the character from
// the player's crew (restores stats, removes it from the roster,
// releases resources - this project's own minimal stand-in only models
// the roster-membership bit, `inRoster`; the stat-restore/resource-
// release internals are OPEN, not given beyond the fact they happen);
// `squad_enable(name, true)` only flips two unrelated flag bits
// (`flagBitA`/`flagBitB` - CHOSEN names, real meanings OPEN) and does
// NOT re-add the character to the crew - `inRoster` is deliberately
// left untouched on the true path, faithfully reproducing the defect
// rather than "fixing" the two calls into real inverses.
struct SquadMember {
    bool inRoster = false;
    bool flagBitA = false;
    bool flagBitB = false;
    uint64_t dismissCount = 0;
    uint64_t enableTrueCount = 0;
};
// Per-(alias-track-code) state, the five setters' own shared record -
// CONFIRMED distinct storage from lookupNotorietyTrackCode's own track
// records (Sec6.22: "a THIRD, independent... mechanism", not proven to
// share an instance with anything else in this file). Every write is
// gated the SAME way (Sec6.22: "every record-and-replicate call site
// newly traced in THIS cluster... gates on... 0x0087ba20" -
// EngineState::coopLocalIsHost(), NOT the forceFlagGatePasses double-
// gate the rest of this file uses) - host: direct write; non-host:
// EngineState::replicateStateChange() only, no local write (the SAME
// either/or shape every other record-and-replicate setter in this file
// already follows).
struct NotorietyAliasTrackState {
    double currentValue = 0.0;  // 0x006079c0 (notoriety_set, Sec6.7, sub-tag 5)
    double minValue = 0.0;      // 0x00607ea0 (notoriety_set_min, Sec6.11, sub-tag 2)
    double maxValue = 0.0;      // 0x006080d0 (notoriety_set_max, Sec6.5, sub-tag 3)
    uint64_t setCount = 0, setMinCount = 0, setMaxCount = 0, setMinAndMaxCount = 0, resetCount = 0;
};

// continuous_spawn_start (Sec4.8, real address 0x00a435a0), batch
// 2026-10-08: writes into the SAME shared spawn-region table
// continuous_spawn_stop resets above (see that stub's own new
// erase() call, lua_spec_confirmed_stubs_b08_batch1_core.cpp). RESOLVED
// 2026-10-08 (re-derived from 0x00a435a0->0x005eda60, scratchpad/
// lua_natives_argcount_tutorial_table_2026-10-08.md): a brand-new
// region gets every argument written into a fresh record (count,
// respawn-interval-ms, three raw numeric fields args 5-7, the
// type-name filter arg 4 plus two trailing optional strings args 8/9
// via three dedicated setters 0x005ec4d0/0x005ec3f0/0x005ec460, and
// the third trailing string arg 10 as a raw-pointer field store with
// no setter) and is then activated once (CONFIRMED: a per-slot
// "started" marker, freshly clear on a brand-new record, gates a
// one-time activation call). An EXISTING record (one
// continuous_spawn_stop has not fully reset) takes a fast re-start
// path instead: ONLY the active flag (continuousSpawnActive() above)
// is touched, nothing else is rewritten (CONFIRMED). "Region handle"
// (one of the ten values the spec's own prose lists as written) is
// this project's map key itself - the SAME flat-name convention
// continuousSpawnActive() above already uses - not a separate stored
// field.
//
// Args 6/7's own rounded values are individually cited as raw dword
// stores at +0x38/+0x3c (CONFIRMED). Arg 5 is grouped in the same
// sentence as "three raw numeric fields (args 5-7)" but its own exact
// offset byte is NOT individually cited the way 6/7's are - modelled
// here as its own plain field anyway, per this task's own "OPEN means
// meaning unknown, not value unknown" distinction (CONFIRMED: a real
// default value (1) and a real storage location exist for it; OPEN:
// which exact byte, and all three numbers' real-world purpose). Arg 4's
// own default just below was formerly this same kind of case (value
// unconfirmed), but Team A has since confirmed the literal itself (see
// continuousSpawnDefaultTypeFilter()'s own doc comment below).
struct ContinuousSpawnRegionRecord {
    int64_t count = 0;                 // arg 2, truncateEa2596, default 0 (CONFIRMED)
    int64_t respawnIntervalMs = 0;      // arg 3 * 1000.0, truncateEa2596, default 0 (CONFIRMED)
    std::string typeNameFilter;         // arg 4, via setter 0x005ec4d0; default from continuousSpawnDefaultTypeFilter() below when arg 4 is absent (CONFIRMED "", see that accessor's doc comment)
    int64_t numericArg5 = 1;            // arg 5, truncateEa2596, default 1 (CONFIRMED value; OPEN meaning + exact offset, see doc comment above)
    int64_t numericArg6 = 14;           // arg 6, truncateEa2596, default 14, raw dword store at +0x38 (CONFIRMED location; OPEN meaning)
    int64_t numericArg7 = 19;           // arg 7, truncateEa2596, default 19, raw dword store at +0x3c (CONFIRMED location; OPEN meaning)
    bool hasArg8 = false;
    std::string arg8String;              // via setter 0x005ec3f0; default absent/NULL (CONFIRMED)
    bool hasArg9 = false;
    std::string arg9String;              // via setter 0x005ec460; default absent/NULL (CONFIRMED)
    bool hasArg10 = false;
    std::string arg10RawPointerString;   // raw pointer store at +0x40, NO setter call; default absent/NULL (CONFIRMED)
    bool started = false;                // per-slot "started" marker byte (CONFIRMED structure); always freshly clear on a brand-new record in this project's closed-world model
    uint64_t activationCount = 0;         // how many times the dedicated activation call fired (CONFIRMED: once, on the first registration only)
};
struct NotorietySpawnGroupState {
    uint64_t clearFlagCount = 0; // +0x3c1 clear, unconditional on success (CONFIRMED)
    // The generic expiry-timestamp setter 0x00d9e140 needs a real "now"
    // counter (0x01320d94) this project does not model anywhere (no
    // frame-time/now global exists in this header) - the raw rounded
    // delta argument is recorded honestly instead of a fabricated
    // wrapped sum, same "record the raw request" convention as
    // MinimapIconRecord above.
    int64_t lastDelayRequested = 0;
    uint64_t delayRequestCount = 0;

    // notoriety_spawn_group_set_destination (Sec25.1) / notoriety_spawn_
    // group_clear_destination (Sec25.2), wave 2 batch W2-4: SAME group
    // table (0x008ed350) as the rest of this struct, a shared setter
    // (0x008edfb0, newly identified by Sec25.1's own text - this
    // project's own field-level writes past the call shape are not
    // further decompiled, OPEN). "" = no destination set (the real
    // cleared/zeroed-buffer case).
    std::string destinationTargetName;
    uint64_t setDestinationCount = 0;
    uint64_t clearDestinationCount = 0;

    // notoriety_force_no_spawn_group (Sec6.2's sibling, Sec6.10, batch
    // b29, 2026-10-08): the SAME `+0x455` "force no spawn" flag byte
    // Sec3.6's own notoriety_force_no_spawn already models (track-
    // keyed, lookupNotorietyTrackCode) - here reached through THIS
    // group resolver instead, confirmed (Sec6.10) to be a single,
    // unconditional, ungated instruction write (no host-check, no
    // replicate - unlike the alias-track setters above). HIGH
    // CONFIDENCE, not independently proven with a shared-instance
    // check (Sec6.22's own hedge), that the group/track records share
    // one underlying type - kept as its OWN field regardless, since a
    // DIFFERENT resolver reaches it here.
    bool forceNoSpawn = false;
    uint64_t forceNoSpawnSetCount = 0;

    // disable_specific_notoriety_group (Sec6.2, batch b29): per-(tier,
    // item-name) flag byte within this group's own tier/sub-item table
    // (+0x3e8). CONFIRMED mechanism (resolve group -> range-check tier
    // -> case-insensitive item-name scan -> write); the per-group tier
    // MIN/MAX bounds are not given by spec (see
    // notorietyGroupTierInRangeResolves() above) so the in-range gate
    // is OPEN-until-set, not guessed - this map only holds the write
    // itself, once in-range is confirmed.
    std::map<int64_t, std::unordered_map<std::string, bool>> tierItemFlags;
    uint64_t disableSpecificGroupWriteCount = 0;
};

// trigger_get_radius / ingame_effect_add_trigger / notoriety_set_
// override_timer / hud_push_screen (Sec21.3/21.5/21.6/21.13): host-only
// broadcasts reuse the EXISTING coopLocalIsHost() gate above (Sec8.27) -
// no new gate field needed.

// notoriety_set_override_timer (Sec21.3): resolved via the SAME 24-
// entry track table lookupNotorietyTrackCode() already models (Sec3.6,
// HIGH CONFIDENCE per Sec21.3's own cross-reference to Sec6.2's
// identical-shaped group table). Per-track override state, keyed by
// track name (this project's own name-keying convention, standing in
// for the real index-keyed table).
struct NotorietyOverrideTimer {
    bool enabled = false;
    double minValue = 0.0;
    double maxValue = 0.0;
};

// notoriety_restricted_zones_enable (Sec22.14): global enable flag plus
// the small fixed 4-entry restricted-zone slot table (0x026ea7f8) this
// native clears when genuinely disabling - no in-scope setter ever
// populates a slot (OPEN, no in-scope writer), so the clear path is only
// ever counted, never observed changing a real value.
struct NotorietyRestrictedZones {
    bool enabled = true; // Sec22.14's own nil-gated-argument default
    std::array<int32_t, 4> slots{-1, -1, -1, -1};
    uint64_t setCount = 0;
    uint64_t clearedSlotCount = 0;
};

// continuous_spawn_set_orders (Sec22.23): resolves its group name via
// the SAME "guardian angel"/script-group table group_destroy_do (Sec25.16)
// and group_create_check_done (Sec22.18, a DIFFERENT batch) already share
// (guardianAngelGroupKnown/guardianAngelGroupRegistered above) - reused
// directly rather than a second, duplicate group table. The order-type
// name is matched against the CONFIRMED 4-entry literal table ("attack",
// "defend", "moveflee", "idle" - byte-confirmed, Sec22.23's own text);
// anything else is index 4 (the real sentinel, not a 5th slot). The
// anchor position is NOT modelled (no coordinate system) - only whether
// an anchor name resolved is recorded, honestly.
struct ContinuousSpawnOrder {
    int32_t orderTypeIndex = 4; // 4 = sentinel/no-match, matching the real table's own out-of-range behaviour
    std::string anchorName;      // "" = world-origin default (anchor absent or unresolved)
    double radius = 0.0;
    uint64_t setCount = 0;
};

// --- get_closest_npc_to_object / get_closest_object (Sec41.6/53.5) ---
// Both need real spatial-query data this project has nowhere in its
// state (no coordinate system at all, stated project-wide
// simplification). CONFIRMED STRUCTURAL defaults when nothing
// qualifies (NOT themselves OPEN, unlike the query result for a
// specific real case): get_closest_npc_to_object -> (FLT_MAX, ""),
// get_closest_object -> "" alone, never nil. A test overrides either
// per reference-object name via these OpenValueMaps; a real call with
// no override takes the CONFIRMED "nothing qualifies" default rather
// than throwing (the default itself is a real, spec-given fact).
struct ClosestNpcResult { double distance; std::string name; };

// --- boss_battle_matt_qte_begin / boss_battle_matt_set_phase
// (Sec59.5): see BossBattleMatt's own new fields above (phase/
// kPhaseTableSize/qteBeginOutOfRangePhaseGuardCount/
// qteBeginMattNotLiveGuardCount/qteBeginUnresolvedPlayerGuardCount) -
// CHOSEN literal object name "Matt" for the liveness check
// (objectResolves()), matching the spec's own prose ("the boss
// character 'Matt'").

// --- spawn_global_override_set_category / _clear_category (Sec49.2) --
// CONFIRMED: "the global override is not replicated at all mid-
// session - it only reaches a newly-joining client via the one-time
// join snapshot." A plain string value, "" = cleared/none.
struct SpawnGlobalOverride { std::string category; };

// --- Groups (§41.5 group_is_loaded_internal/group_show_do/
// group_hide_do). "Loaded" is CHOSEN to reuse the EXISTING
// groupAlreadyCreated()/markGroupCreated() existence registry
// (Sec53.3's own group_create_do, already in this codebase) directly -
// the only real "does this group exist" signal this project tracks;
// §41.5's own "false on a failed lookup" wording is a natural fit for
// "never created." group_show_do/group_hide_do's own documented crash
// (a null member object reached while walking the group's member
// list) has NO structural equivalent in this project: Sec53.3's own
// groupCreateCheckDone() doc comment already establishes this project
// deliberately tracks group EXISTENCE only, never a member roster - so
// there is no member list to walk and nothing to null-dereference;
// implemented instead as a safe existence-gated visibility flag (a
// CHOSEN scope limitation, not a fabricated member list). CORRECTED
// 2026-10-08 (review of this batch): this comment always claimed
// "existence-gated," but a prior version of the two stubs never
// actually called groupAlreadyCreated() before mutating
// w28GroupVisibility_ - hiding/showing a group that was NEVER created
// silently set a visibility flag anyway, contradicting this comment
// and leaving group_is_loaded_internal('G1')==false inconsistent with
// a group that nonetheless had real show/hide state. §41.5's own text
// doesn't separately spell out what the real function does for an
// unresolved/never-created group name (it only documents the member-
// object null-deref crash on an ALREADY-resolved group), but every
// other named-object resolve in this spec early-returns on a failed
// lookup rather than fabricating state - consistent with, not
// contradicted by, this project's own stated "existence-gated" design
// - so the fix is to make the CODE match the comment's always-stated
// intent (option (a): add the gate), not to weaken the comment.
// group_show_do/group_hide_do now both refuse (no-op, counted below)
// when groupAlreadyCreated(name) is false, exactly like
// group_is_loaded_internal already reads that same registry.
struct W28GroupVisibility {
    bool hidden = false;
    uint64_t showCount = 0;
    uint64_t hideCount = 0;
};

// --- action_sequence_setup/_is_playing/_advance (§46.2). A NEW struct
// (NOT the existing `ActionSequence` struct above, which is scoped
// only to action_sequence_end/Sec33.3 and has no vehicle pointer/
// control-lock pool - reusing that name here would collide two
// genuinely different real mechanisms under one C++ type). CONFIRMED
// "action_sequence_setup with no vehicle name clears the sequence in
// single player" (this project's whole scope) and "leaks one control-
// lock slot per repeated call" (replicated via the monotonic counter
// below, never decremented - no release function is in this batch).
struct ActionSequenceScriptBlock {
    bool active = false;
    std::string vehicleName;
    uint64_t controlLockLeakCount = 0; // CONFIRMED: never released in this batch's own scope
    uint64_t advanceCount = 0;
};

struct EngineStateAiData {
    // Former EngineState nested types (defined above at namespace scope), re-exported so
    // `EngineState::Type` keeps naming them.
    using NpcAiStats = sr3luahost::NpcAiStats;
    using ActionSequence = sr3luahost::ActionSequence;
    using PartyState = sr3luahost::PartyState;
    using SpawnOverride = sr3luahost::SpawnOverride;
    using SquadMember = sr3luahost::SquadMember;
    using NotorietyAliasTrackState = sr3luahost::NotorietyAliasTrackState;
    using ContinuousSpawnRegionRecord = sr3luahost::ContinuousSpawnRegionRecord;
    using NotorietySpawnGroupState = sr3luahost::NotorietySpawnGroupState;
    using NotorietyOverrideTimer = sr3luahost::NotorietyOverrideTimer;
    using NotorietyRestrictedZones = sr3luahost::NotorietyRestrictedZones;
    using ContinuousSpawnOrder = sr3luahost::ContinuousSpawnOrder;
    using ClosestNpcResult = sr3luahost::ClosestNpcResult;
    using SpawnGlobalOverride = sr3luahost::SpawnGlobalOverride;
    using W28GroupVisibility = sr3luahost::W28GroupVisibility;
    using ActionSequenceScriptBlock = sr3luahost::ActionSequenceScriptBlock;

public:
    const NpcAiStats& npcAiStats() const { return npcAiStats_; }
    // Set by the host for the frame: the player fired or aimed (a civilian's gunfire/aim threat input).
    bool npcAiPlayerFiringThisFrame = false;
    // Only characters within this horizontal distance of the player run AI (CHOSEN: a zone holds thousands of script_npc
    // placements, only those streamed in around the player are live humans).
    double npcAiActiveRadius = 150.0;
    // Synced actions (spec Sec18.5, blocker m07): action_play_synced_do success gives both characters an active
    // synced instance starting now with the animation's length; ends when played out or stopped.
    // CHOSEN stand-in length (the spec's own suggestion): 3 s, since no animation lengths are modelled here.
    static constexpr double kSyncedActionStandInSeconds = 3.0;

    // 2026-10-07 (Sec60.4's closure of the max-hit-points random-per-spawn
    // model; probe_blockers_ranked_20261007.md item 2, mission `m18`): the
    // REAL set of Hit_Points values spawn_info_ranks.xtbl ships, installed
    // from the real archive cache by lua_host_run.cpp
    // (installContinuousExplosionTableFromCache's own sibling for this
    // table - see that file's comment for the archive precedence question).
    // Empty (the default - every synthetic unit test, and any run that
    // never installs it) means drawOrGetMaxHitPoints() below falls back to
    // the single already-established CHOSEN diagnostic constant, 100.
    void installSpawnRankHitPoints(std::vector<int32_t> values) { spawnRankHitPoints_ = std::move(values); }
    size_t spawnRankHitPointsCount() const { return spawnRankHitPoints_.size(); }

    // --- 0x0095da50 (Sec14.1 CORRECTED + Sec33.3 action_play_synced_do) ---
    //
    // 0x0095da50 is a plain name -> synced-action-definition index lookup
    // (hash the name, scan a global table, sentinel kSyncedActionNotFound
    // for "not found") - NOT a hidden-argument identity match against an
    // actor, as this document's own Sec14.1 entry (action_play_synced_state,
    // out of this batch's scope to implement) previously read it before the
    // 2026-10-02 correction this same investigation applied in place there.
    // This project has no real table of synced-action definitions loaded
    // anywhere (no in-scope function populates one), so every name honestly
    // takes the real, CONFIRMED "not found" path unless a test registers
    // one directly - same "never-populated object space" precedent as
    // VdoObject/vint_object_find above.
    static constexpr int32_t kSyncedActionNotFound = 0xffff;

    // Sec30.6 `team_make_unfriendly` / Sec20.6 `team_make_allies` (the
    // latter out of this tranche's own scope, not Lua-registered by this
    // project): both resolve a team name via the established helper
    // 0x0094cc60 (hash 0x00dab330 + lookup 0x005961a0, default sentinel 9
    // for an unrecognized name - CONFIRMED mechanism, both Sec20.6 and
    // Sec30.6). This project has no real team-name registry (no spec
    // gives the actual in-game team name strings or their real ids) -
    // resolveTeamId() is this project's own minimal stand-in: a name a
    // test has not registered takes the real, CONFIRMED "unknown name"
    // path (sentinel id 9) - the SAME "never-populated real registry,
    // so the honest default IS the real behavior" precedent as
    // EngineState::findVdoObject's own doc comment.
    void registerTeamIdForTesting(const std::string& name, int id) { teamIdByName_[name] = id; }

    // 0x009b7130(id1, id2, value, true) writes `value` symmetrically into
    // an 8x8 (CONFIRMED, Sec20.6) relation-matrix byte block (base
    // 0x0262d940, stride 8) - valid indices 0..7. Sentinel id 9 (or any id
    // outside 0..7, such as a resolver bug this project cannot rule out)
    // indexes PAST that block - Sec30.7's own cross-function note flags
    // this as "a crash-shaped edge... OPEN, not confirmed" (whether it
    // truly faults or just corrupts adjacent memory is not independently
    // settled). HOST-SAFETY DEVIATION (not a spec fact, per this task's
    // own explicit instruction): either way, this host must not perform
    // the equivalent out-of-bounds write into its own 8x8 array - both
    // ids are bounds-checked first; an out-of-range id makes this return
    // false and skip the write entirely (the caller logs the averted path
    // - see stub_team_make_unfriendly), matching the same "this project
    // does not simulate crashes" precedent used throughout this file.
    // Also applies Sec30.6's own refinement of Sec20.6: "whenever team 5
    // is one side, team 6 gets the identical relation" (the SAME base
    // matrix at rows/columns 6, not a separate auxiliary array -
    // CONFIRMED, address arithmetic) - done here so both team_make_allies
    // (out of scope) and team_make_unfriendly share one correct
    // implementation if team_make_allies is ever added.
    static constexpr int kTeamRelationMatrixDim = 8; // CONFIRMED 8x8 block, Sec20.6/Sec30.6
    int teamRelationOobGuardCount() const { return teamRelationOobGuardCount_; }
    void recordTeamRelationOobGuard() { ++teamRelationOobGuardCount_; }
    ActionSequence& actionSequence() { return actionSequence_; }

    // Sec32.1 `spawn_region_max_spawn_dist`/`spawn_region_max_spawn_dist_reset`:
    // 0x013092f0, a squared max-spawn-distance cap. CONFIRMED file-backed
    // default FLT_MAX ("no limit"). No range check on the setter (CONFIRMED:
    // negative squares positive, 0 makes the cap 0) - the stub applies that
    // arithmetic directly (lua_spec_confirmed_stubs.cpp), this is a plain
    // read/write pair.
    float spawnRegionMaxSpawnDistSquared() const { return spawnRegionMaxSpawnDistSquared_; }
    void setSpawnRegionMaxSpawnDistSquared(float v) { spawnRegionMaxSpawnDistSquared_ = v; }

    // Sec32.1 `set_ped_override_density`: 0x01308af0. CONFIRMED file-backed
    // default -1.0 ("no override").
    float pedOverrideDensity() const { return pedOverrideDensity_; }
    void setPedOverrideDensity(float v) { pedOverrideDensity_ = v; } // caller (the stub) applies the CONFIRMED >0/<=1.0/NaN branch logic before calling this
    PartyState& party() { return party_; }

    // --- Sec44/Sec45/Sec46 tranche follow-up (ranking tranches 12-14),
    // batch 2026-10-02 (resumed session) -----------------------------------

    // ambient_gang_spawn_enable (Sec45.6): CONFIRMED "a single boolean fans
    // out to all four ambient-gang-enable bytes at once - no per-gang Lua
    // control exists at this entry point." Since nothing in this project's
    // scope can ever observe the 4 bytes independently (no per-gang getter,
    // and the only writer sets them in lockstep), they are modeled as ONE
    // global value rather than 4 redundant copies - a stated simplification,
    // not a claim the real engine stores only one byte.
    OpenValue<bool>& ambientGangSpawnEnabled() { return ambientGangSpawnEnabled_; }

    // ambient_cop_spawn_enable / action_nodes_shouldnt_flee /
    // action_nodes_restrict_spawning (Sec46.3): CONFIRMED "simple global-
    // byte toggles gating AI spawn/flee behavior, none replicated." Three
    // genuinely distinct bytes (unlike the ambient-gang case above, nothing
    // in Sec46.3's text says these three share storage) - each write-only
    // in this project's scope (no in-scope Lua getter reads any of them
    // back; a test can).
    OpenValue<bool>& ambientCopSpawnEnabled() { return ambientCopSpawnEnabled_; }
    OpenValue<bool>& actionNodesShouldntFlee() { return actionNodesShouldntFlee_; }
    OpenValue<bool>& actionNodesRestrictSpawning() { return actionNodesRestrictSpawning_; }
    SpawnOverride& spawnOverride() { return spawnOverride_; }
    SquadMember& squadMember(const std::string& name) { return squadMembers_[name]; }

    // group_create_do / group_create_hidden_do (Sec53.3, tranche 20).
    // CONFIRMED: "the same underlying creation routine differing only in
    // one passed literal, and creation happens exactly once - calling
    // group_create_hidden_do on a group group_create_do already created
    // does nothing at all." One shared "has this name already been
    // created" set - which literal (visible/hidden) was used first is
    // never read back by anything in this project's scope, so it is not
    // separately tracked (would be fabricated detail).
    bool groupAlreadyCreated(const std::string& name) const { return groupsCreated_.count(name) != 0; }

    int actionPlayDeadGuardCount() const { return actionPlayDeadGuardCount_; }
    void recordActionPlayDeadGuard() { ++actionPlayDeadGuardCount_; }
    int actionPlayLookupMissCount() const { return actionPlayLookupMissCount_; }
    void recordActionPlayLookupMiss() { ++actionPlayLookupMissCount_; }

    // action_play_is_finished (Sec7.21): CONFIRMED structure - "if resolved
    // AND a zero-argument context check (0x00487e80) succeeds (result !=
    // -1) AND the object's action-slot value ... is still below the
    // threshold argument, returns false. Otherwise returns true[, +
    // conditional notify]." The "context check" queries whether the
    // character's animation controller currently has an active sub-state -
    // something only action_play_do's own OPEN internal dispatch
    // (0x00a275e0, Sec22.29) could ever establish, and this project does
    // not model those internals (see stub_action_play_do's own doc comment
    // and the lookup-miss reasoning just above). Nothing in this project's
    // scope can therefore ever make the context check report anything
    // other than "no active state" - the SAME "this host never allocates
    // X, so it honestly and permanently reproduces the 'reports done
    // prematurely' [structural branch]" reasoning cutscenePlayCheckDone()
    // above already documents, here applied to a different CONFIRMED
    // branch of a different function. Both the "character resolves" and
    // "character doesn't resolve" paths converge on this SAME "otherwise:
    // true" branch in this project's scope (Sec22.29's own text: "if
    // resolved AND ... Otherwise returns true" - not-resolved is one of
    // the ways the AND is false), so resolving the character is not
    // needed to answer honestly here. The conditional notify
    // (0x009e3200(0,0)) has no Lua-visible effect anywhere in this
    // project's scope (an internal engine call, not a further native) and
    // so is not separately modeled, same as the unobservable internals
    // this file already declines to fabricate elsewhere (e.g.
    // cutscenePlayDo's own uninitialized script-group-handle array).
    bool actionPlayIsFinished() const { return true; }

    // party_dismiss_all (Sec14.17, individually cleared by Sec60.3) - a
    // genuine zero-argument command, dismisses the local player's own
    // party/companion roster (both representations when a co-op session is
    // present, per the SAME 0x009df3d0 check satellite_weapon_mode_exit/
    // Sec32.1 already reuses - st->playerRig().coopPlayerPresent). This
    // project has no real roster-membership walk (same "no registry"
    // simplification class as groupsCreated_ above) - test-observable call
    // count only.
    uint64_t partyDismissAllCount() const { return partyDismissAllCount_; }
    void recordPartyDismissAll() { ++partyDismissAllCount_; }

    // group_create_check_done (Sec22.18). CONFIRMED: an unresolved/not-alive
    // group is treated as "done" (true). This project builds no per-member
    // registry for any group (group_create_do/_hidden_do above only record
    // that a name was created, never a member list), so the resolved-group
    // branch's own two-list completion walk (0x00a2afc0, OPEN internal
    // field offsets per Sec22.18) is vacuously "both lists exhausted clean"
    // under this project's own stated no-member-registry simplification -
    // the same "a registry this project deliberately does not build is
    // itself the confirmed fallback" reasoning vehicle_pathfind_check_done
    // (Sec9.10) already established, not a fresh guess. Always true.
    bool groupCreateCheckDone(const std::string& /*name*/) const { return true; }

    // --- Section 6 batch (2026-10-07, full-document spec sweep) -
    // set_ped_density/set_traffic_density/notoriety_set_can_decay/
    // waypoint_remove/spawning_pedestrians/spawning_vehicles/traffic_
    // disable_drawbridge_area/guardian_angel_register_script_group/
    // guardian_angel_unregister_script_group (Sec6.9/6.14/6.18/6.17/6.13/
    // 6.15/6.4/6.6/6.8). See each accessor's own doc comment below for
    // citation/reasoning. The notoriety track-value setters (Sec6.3/6.5/
    // 6.7/6.11/6.16) and disable_specific_notoriety_group/notoriety_force_
    // no_spawn_group (Sec6.2/6.10) were deliberately NOT in THIS batch -
    // they were thought to need a real alias/group-name table the spec
    // text does not fully supply; mission_end_success/mission_end_to_
    // activity/mission_set_cancel_warp_location (Sec6.12/6.20/6.21) were
    // deliberately NOT in this batch either, pending a mission-state model.
    // BOTH blockers are resolved by batch b29 (2026-10-08, see below): the
    // MissionContext struct (Sec1.5/1.6, implemented in the interim by the
    // Team-A-cleared batch below) already models the "mission is active"
    // gate these three share with mission_end_failure, and the alias/group
    // tables' own CONFIRMED mechanism (case-insensitive scan, a stated
    // sentinel on no match) is modeled the SAME "OPEN-until-a-test-sets-it"
    // way every other not-fully-enumerated table in this project already
    // is (lookupNotorietyTrackCode's own identical precedent, just above
    // this batch) - not an invention, the established idiom for exactly
    // this situation. See the b29 fields further below (notoriety alias
    // track state, the group-record extensions on NotorietySpawnGroupState,
    // and the mission-gate reuses) for the full citations. tutorial_start
    // (Sec6.19) stays OUT of scope - its 210-entry descriptor table and
    // 12-step dispatcher remain a disproportionate new subsystem for one
    // native, and no later spec update has changed that.

    // set_ped_density/set_traffic_density (Sec6.9/Sec6.14). CONFIRMED: two
    // sibling global floats (0x01308ab4/0x01308ab0, address-adjacent - HIGH
    // CONFIDENCE sibling fields of one small world-population-density
    // settings block), each written directly from arg 1; nothing else is
    // read or written. No other function in this document reads either
    // value back, and no CONFIRMED startup value is given either way -
    // write-only in this project's own observable scope; the starting value
    // 0.0f is CHOSEN, not a spec fact.
    float pedDensity() const { return pedDensity_; }
    void setPedDensity(float v) { pedDensity_ = v; }

    // notoriety_set_can_decay (Sec6.18). CONFIRMED: a single GLOBAL flag
    // (0x014a4c0f), not per-track - unlike every other notoriety_set_*
    // function in this cluster. CONFIRMED: the stored byte is the LOGICAL
    // NEGATION of the Lua-visible argument (the same "flag stored as the
    // negated sense of an allow_*-style argument" convention Sec3.16
    // already established for character_allow_ragdoll). CHOSEN starting
    // value (false, "decay allowed") - no spec states this byte's
    // construction value either way.
    bool notorietyDecaySuppressed() const { return notorietyDecaySuppressed_; }
    void setNotorietyCanDecay(bool canDecay) { notorietyDecaySuppressed_ = !canDecay; }

    // spawning_pedestrians/spawning_vehicles (Sec6.13/Sec6.15). CONFIRMED:
    // each sets or clears slot 1 of its own per-category state-slot
    // array(s) - this project models only this CONFIRMED master on/off
    // toggle, not the full 8-slot arrays (the other 7 slots' real-world
    // identity is OPEN) or the combined cross-array cleanup
    // spawning_pedestrians' own optional arg 2 can trigger when every
    // tracked slot in both arrays reads inactive (gated on "two more
    // internal checks," OPEN - not modelled further). CHOSEN starting
    // value (true, "ambient spawning on") - no spec states either array's
    // construction value, but an ambient-population game starting with all
    // spawning OFF by default is not a credible reading either; flagged as
    // CHOSEN, not derived.
    bool pedestrianSpawningEnabled() const { return pedestrianSpawningEnabled_; }
    void setPedestrianSpawningEnabled(bool enabled) { pedestrianSpawningEnabled_ = enabled; }

    // --- batch b29 (2026-10-08): the rest of Section 6 -------------------
    //
    // notoriety_set_min_and_max/notoriety_set_max/notoriety_set/
    // notoriety_set_min/notoriety_reset (Sec6.3/6.5/6.7/6.11/6.16). CONFIRMED
    // (Sec6.3/Sec6.22): a THIRD, independent notoriety-track name resolver
    // (0x00605160), distinct from lookupNotorietyTrackCode's own
    // 0x0094cc60/sentinel-9 resolver above (kept SEPARATE on purpose - the
    // same WALLS.md caution against conflating two resolvers that merely
    // resemble each other) - "a case-insensitive scan across three parallel
    // 4-entry alias-name arrays... returning a shared 0-3 index or the
    // literal 5 on no match." Only the first alias of each 4-entry array is
    // given verbatim from .rdata ("luchadores"/"brotherhood"/"los_carnales"
    // -> codes 0/1/2, seeded at construction below, applySpecInitialState)
    // - the remaining 3 members of each array are not given by spec, so
    // this project models the CONFIRMED mechanism (case-insensitive lookup,
    // deterministic sentinel 5 on no match - itself a real, confirmed
    // behaviour, not an OPEN unknown) via a plain seeded map a test can add
    // further aliases to, the same "test-registrable, not invented" shape
    // lookupNotorietyTrackCode itself already established for its own
    // resolver.
    static constexpr int32_t kNotorietyAliasTrackNotFound = 5;
    std::unordered_map<int32_t, NotorietyAliasTrackState>& notorietyAliasTrackStates() { return notorietyAliasTrackStates_; }
    // notoriety_reset's own first call (0x00609810, Sec6.16): a wholesale
    // notoriety UI/meter reset - CONFIRMED host-only, with NO replicated
    // fallback at all on a non-host machine (a genuine, confirmed asymmetry
    // against its four sibling setters above, none of which silently do
    // nothing on the non-authoritative path). This project has no real
    // 800-byte buffer/UI meter to clear - a test-observable call counter
    // only, same "no Lua-visible effect beyond the call itself" convention
    // as waypointRemoveCount above.
    uint64_t notorietyUiMeterResetCount() const { return notorietyUiMeterResetCount_; }
    void recordNotorietyUiMeterReset() { ++notorietyUiMeterResetCount_; }

    // disable_specific_notoriety_group/notoriety_force_no_spawn_group
    // (Sec6.2/6.10). CONFIRMED: BOTH resolve arg 1 through the SAME 24-name
    // GROUP table (0x008ed350) notoriety_spawn_group_set_delay/_destination
    // already model via notorietySpawnGroupResolves()/
    // getOrCreateNotorietySpawnGroup() below (Sec13.2/Sec25.1) - reused
    // directly here, not a new table, since it is confirmed to be the exact
    // same real resolver (see NotorietySpawnGroupState's own doc comment
    // further below for the two new fields this batch adds to it:
    // forceNoSpawn/tierItemFlags).
    //
    // disable_specific_notoriety_group's own per-group tier range-check
    // (+0x3e0/+0x3e4 min/max VALUES, i.e. which tier numbers are valid for a
    // given group - distinct from the group NAME resolving at all, see
    // notorietySpawnGroupResolves()'s own doc comment above for that half):
    // NOT given by spec for ANY of the 24 groups, including "police" -
    // spec-tables-progression.md Sec6.3/6.4 gives the field's layout/
    // meaning and the full 24-name list but not one real per-group tier
    // count or level_info row (base-only xtbl, no sample). Modeled as a
    // dedicated OPEN-until-set resolve, keyed "<group>#<tier>", same
    // discipline as every other not-given bound in this project (inventing
    // a plausible-looking range would be exactly the kind of guess this
    // project's OpenValue discipline exists to refuse).
    OpenValueMap<bool>& notorietyGroupTierInRangeResolves() { return notorietyGroupTierInRangeResolves_; }

    // notoriety_force_no_spawn (Sec3.6). CONFIRMED: arg 1 names a
    // notoriety/gang TRACK, resolved via a lookup DISTINCT from the general
    // named-object resolver (objectResolves()) - 0x0094cc60, a team/gang-
    // name-to-id hash, into one of codes {1,2,3,4,5,6} or the CONFIRMED
    // real failure sentinel `9` on a miss. The real string<->code mapping
    // is explicitly OPEN per Sec3.6's own text (two independent passes,
    // using two different methods, both failed to locate the registry's
    // populating code) - this project exposes a test-only registration
    // hook, mirroring the already-established lookupSyncedActionIndex()/
    // registerSyncedActionForTesting() "never-populated object space"
    // precedent above. An unregistered name answers the REAL CONFIRMED
    // failure sentinel, 9 - not an invented default, exactly the code's own
    // documented fallback (Sec3.6: "an unrecognized track name is silently
    // a no-op").
    static constexpr int32_t kNotorietyTrackCodeNotFound = 9;
    // One per-track flag byte, keyed by the EXACT literal track-record base
    // address Sec3.6's own re-confirmed disassembly enumerates (the
    // `0x008ecda0(this, flag)` setter's own `this` pointer - CONFIRMED
    // `mov byte ptr [ecx+0x455], al`, a direct SET to the passed boolean,
    // not an XOR toggle despite this section's own looser "toggles" prose
    // elsewhere). CHOSEN false ("never toggled") initial state for each -
    // no spec states an initial real game-data value for any of these
    // bytes, only the toggle mechanism itself.
    void setNotorietyTrackFlag(uint32_t trackBaseAddress, bool value) { notorietyTrackFlags_[trackBaseAddress] = value; }
    bool notorietyTrackFlagKnown(uint32_t trackBaseAddress) const { return notorietyTrackFlags_.count(trackBaseAddress) != 0; }

    // spawn_region_enable (Sec1.8): per-spawn-region byte +0x94 bit 0x1,
    // CONFIRMED branchless conditional-XOR, single-bit-only scope. This
    // project's flat objectResolves() map stands in for the dedicated
    // 0x006ed4d0 registry (Sec29's "one shared name map" convention,
    // already applied project-wide to vehicles/characters/placed objects).
    std::unordered_map<std::string, bool>& spawnRegionEnabled() { return spawnRegionEnabled_; }

    // vehicle_suppress_npc_exit (Sec4.5): see VehicleState's own new
    // vehicleAiForceFlagsByte4 field.

    // set_player_can_enter_exit_vehicles (Sec4.6): see CharacterState's
    // own new flags28ac field.

    // continuous_spawn_stop (Sec4.7): CONFIRMED resolves via the SAME
    // resolver family release_to_world's tier-1 (group/spawn-region)
    // member uses (bit 0x20@+0xa) - this project's flat objectResolves()
    // stand-in, same convention as every other single-resolver setter in
    // this header. If a slot for this region exists, it is unconditionally
    // reset regardless of the boolean (CONFIRMED); the boolean is
    // consulted only in the narrower per-entity-dispatch path reached ONLY
    // when the slot's own activity marker was already non-zero (CONFIRMED)
    // - continuous_spawn_start (Sec4.8, the only in-scope writer of that
    // marker) is OUT of this batch's scope, so in THIS project's closed
    // world no slot is EVER marked active by any in-scope native; a test
    // can still mark one active directly to exercise that path for
    // structural coverage. The per-entity vtable-+0x58/+0x5c dispatch
    // itself has no plain-English identity (genuinely OPEN per spec,
    // "structurally unresolvable by static disassembly alone") - not
    // modelled beyond which boolean a since-marked-active region's stop
    // call carried.
    std::unordered_map<std::string, bool>& continuousSpawnActive() { return continuousSpawnActive_; }
    std::unordered_map<std::string, uint64_t>& continuousSpawnStopCount() { return continuousSpawnStopCount_; }
    std::unordered_map<std::string, bool>& continuousSpawnLastStopBoolean() { return continuousSpawnLastStopBoolean_; }
    std::unordered_map<std::string, ContinuousSpawnRegionRecord>& continuousSpawnRegions() { return continuousSpawnRegions_; }
    // Arg 4's own default ("a fixed literal constant" - Sec4.8's own text,
    // a spawned-object/type-name filter): CONFIRMED by Team A
    // (spec-lua-api-behaviour.md Sec4.8) to be the literal empty string
    // "" - not just that a literal constant exists, but the literal value
    // itself. Formerly modelled as OpenValue<std::string> (value
    // genuinely unknown, unlike args 5-7's already-confirmed numeric
    // defaults); now a plain defaulted member, same pattern as
    // numericArg5/6/7 above.
    std::string& continuousSpawnDefaultTypeFilter() { return continuousSpawnDefaultTypeFilter_; }
    void registerActionNameForTesting(const std::string& name, int32_t index) { actionNameIndices_[name] = index; }

    // =====================================================================
    // Batch 4 (spec-lua-api-behaviour.md Sec13+Sec14, ranks ~151-250,
    // native-implementation batch 2026-10-08, 55 names) - see each field's
    // own citation below; full per-native reasoning lives in the stub
    // file's own doc comments
    // (lua_spec_confirmed_stubs_b11_batch4_flags_combat.cpp).
    // =====================================================================

    // notoriety_spawn_group_set_delay (Sec13.2): arg 1 names a notoriety
    // GROUP, resolved via a 24-entry group-name table (0x008ed350) - a
    // genuinely DIFFERENT, disjoint table from lookupNotorietyTrackCode's
    // own TRACK table (Sec3.6). An unregistered group name is a real,
    // CONFIRMED no-op (Sec13.2's own text has no stated failure sentinel to
    // reuse, so this project's own OpenValueMap<bool> OPEN-until-set
    // discipline applies directly). Sec6.2's own text leaves WHICH 24 names
    // populate the table OPEN in isolation, but applySpecInitialState
    // (lua_spec_initial_state.cpp, item 13, 2026-10-09) now pre-seeds the
    // exact 24 to resolve=true, CONFIRMED by cross-reference against
    // spec-tables-progression.md Sec6.3 (the notoriety_spawn.xtbl loader's
    // own independently-derived 24-name table and matching +0x3E0/+0x3E4/
    // +0x3E8 destination-structure offsets - conclusive that both specs
    // describe the SAME fixed array). Any name outside those 24 (or a
    // different-case spelling of one of them - case-folding this lookup is
    // out of that fix's scope) stays genuinely OPEN-until-set.
    OpenValueMap<bool>& notorietySpawnGroupResolves() { return notorietySpawnGroupResolves_; }

    // spawn_region_toggle_filter (Sec13.9): the byte at the resolved
    // region's own +0x95 field - CHOSEN 0 ("never toggled"), same
    // convention as notorietyTrackFlags_ above (Sec3.6).
    uint8_t& spawnRegionFilterBits(const std::string& name) { return spawnRegionFilterBits_[name]; }

    // set_behavior_set (Sec14.27): same "never-populated object space"
    // test-registration hook as above, sentinel 0x2c (44) the CONFIRMED
    // real "no match" fallback.
    static constexpr int32_t kBehaviorSetNotFound = 0x2c;
    void registerBehaviorSetForTesting(const std::string& name, int32_t code) { behaviorSetCodes_[name] = code; }

    // action_play_synced_state (Sec14.1): reuses lookupSyncedActionIndex/
    // registerSyncedActionForTesting/kSyncedActionNotFound above directly
    // (the SAME real mechanism, 0x0095da50, already corrected and modelled
    // for action_play_synced_do's own identical call, Sec33.3). The per-
    // actor local-authority gate (0x008addb0) is explicitly "not previously
    // attributed to a specific subsystem" by Sec14.1's own text - kept
    // SEPARATELY SCOPED from controlLockGatePasses, same reasoning as
    // CharacterState::scriptedActionDispatchGatePasses above, but here
    // keyed per NAMED actor (Sec14.1's own "per-actor" phrasing), not a
    // flat global.
    OpenValueMap<bool>& syncedActionAppliesLocally() { return syncedActionAppliesLocally_; }

    // action_nodes_enable (Sec13.17): a single global byte - CHOSEN false
    // ("not enabled" - no spec gives a real start-up value).
    bool& actionNodesEnabled() { return actionNodesEnabled_; }

    // spawning_allow_cop_ambient_spawns (Sec21.14): a bare global boolean.
    // CHOSEN true default (the nil-gated Lua argument's own CONFIRMED
    // default is true; absent any other in-scope writer, starting the
    // global at that same "allowed" value is the more faithful choice than
    // an arbitrary false).
    bool& copAmbientSpawnsAllowed() { return copAmbientSpawnsAllowed_; }

    // get_team (Sec20.23): the real 4-stage resolver cascade (each stage
    // reading a DIFFERENT team-id-byte offset off a DIFFERENT kind of
    // handle) collapses to this project's one shared objectResolves() map
    // (Sec29 convention) - nothing in this project's own scope ever WRITES
    // a per-object team id (vehicle_set_team, Sec18.20, is not itself
    // implemented in this project yet), so this is a pure, write-less query
    // surface: OPEN until a test sets a team name directly.
    OpenValueMap<std::string>& teamNameByObject() { return teamNameByObject_; }

    // set_force_combat_ready_team (Sec21.16): a bare global, reusing the
    // SAME name->id lookup resolveTeamId() already models (0x0094cc60,
    // Sec30.6/Sec20.6) - CONFIRMED sentinel 9 default (matches
    // resolveTeamId()'s own not-found default, the same "team-wide, no
    // gate, no resolve" global this native's own text describes).
    int32_t forcedCombatReadyTeamId = 9;
    std::unordered_map<std::string, NotorietyOverrideTimer>& notorietyOverrideTimers() { return notorietyOverrideTimers_; }

    // npc_use_closest_action_node_of_type (Sec21.2): the type-NAME to id
    // conversion (0x008c8550) plus its own validator (0x008c7ba0) have no
    // CONFIRMED concrete table entries anywhere in scope - a dedicated,
    // test-settable validity map, OPEN until a test registers a type name.
    OpenValueMap<bool>& actionNodeTypeNameValid() { return actionNodeTypeNameValid_; }

    // notoriety_spawn_only_air (Sec22.13): CHOSEN ONE global bool standing in
    // for the real 9-separate-fixed-object write (no generic entity shape
    // invented) - default false (no CONFIRMED construction value).
    bool& notorietySpawnOnlyAir() { return notorietySpawnOnlyAir_; }
    NotorietyRestrictedZones& notorietyRestrictedZones() { return notorietyRestrictedZones_; }
    bool& actionStopSyncedStateFlag() { return actionStopSyncedStateFlag_; }

    // group_destroy_do (Sec25.16): reuses guardianAngelGroupKnown/
    // guardianAngelGroupRegistered above (Sec6.6/Sec6.8) directly - a
    // destroyed group is marked un-registered (CHOSEN: a destroyed group
    // should not go on resolving as alive) via setGuardianAngelGroupRegistered,
    // plus this call count.
    uint64_t& groupDestroyedCount() { return groupDestroyedCount_; }

    // --- spawning/world-state globals (Sec39.3) ---------------------------
    // spawning_allow_gang_ambient_spawns: "writes the SAME value to four
    // separate global bytes at once... four independent category gates
    // Lua can only ever set in lockstep" (HYPOTHESIS) - modelled as ONE
    // flag since nothing in scope reads the four bytes individually.
    // CHOSEN false initial (no CONFIRMED spawn-time default).
    bool& gangAmbientSpawnsAllowed() { return gangAmbientSpawnsAllowed_; }
    // spawn_region_enable_los: "a bare boolean (absent = disables)...
    // writes the INVERSE of the argument" - the stub applies the inversion;
    // this map holds the resolved "skip LOS test" bit per region name.
    std::unordered_map<std::string, bool>& spawnRegionLos() { return spawnRegionLos_; }
    // set_saints_hated / set_stag_notoriety_area_active: bare booleans,
    // session-synced-variable-registered bytes ("saints_hated"/
    // "stag_notoriety") - CHOSEN false initial (no CONFIRMED default).
    bool& saintsHated() { return saintsHated_; }
    bool& stagNotorietyAreaActive() { return stagNotorietyAreaActive_; }
    // "AND - only when this machine is host and the value actually
    // changed - runs a host-gated spawn/despawn step that releases every
    // tracked handle... when turning off" - this project has no tracked-
    // handle table to release, so only the gated call itself is counted
    // (same "record that a call happened" convention used throughout).
    uint64_t& stagNotorietyAreaHostStepCount() { return stagNotorietyAreaHostStepCount_; }
    OpenValueMap<ClosestNpcResult>& closestNpcOverride() { return closestNpcOverride_; }
    static constexpr int kLightGroupAttachSlots = 16;
    SpawnGlobalOverride& spawnGlobalOverride() { return spawnGlobalOverride_; }

    // --- spawn_region_min_spawn_dist / _reset (Sec32.1, `0x00a5dc40`/
    // `0x00a5dc70`) --------------------------------------------------------
    // UPDATED 2026-10-09 (spec sync 3ecbf84, closing this pair's own
    // previously-AGG "shape only, CHOSEN 0.0" placeholder): CONFIRMED real
    // body, the exact sibling pair of spawnRegionMaxSpawnDistSquared_
    // above, same mechanism, different global. Set: narrows the argument
    // to float and stores `d * d` (squared, exactly like the MAX
    // sibling's own setter - the stub was previously storing the RAW
    // value, a real bug this sync also fixes). Reset: copies back a
    // file-backed read-only constant `0x451c4000` (= 50.0f² = 2500.0f) -
    // the CONFIRMED compile-time default, replacing the old placeholder's
    // CHOSEN 0.0. Edges (CONFIRMED): a negative input squares to the same
    // positive value as its magnitude (no sign rejection); NaN makes the
    // later `<` comparison always false, silently disabling the floor (see
    // SpawnRegionAnnulusEligibleForPlayer below, which reproduces that
    // same "NaN never triggers the too-close rejection" behaviour by using
    // a plain `<` against the stored squared value, not a NaN-safe
    // comparison).
    float spawnRegionMinSpawnDistSquared() const { return spawnRegionMinSpawnDistSquared_; }
    void setSpawnRegionMinSpawnDistSquared(float v) { spawnRegionMinSpawnDistSquared_ = v; }
    void resetSpawnRegionMinSpawnDist() { spawnRegionMinSpawnDistSquared_ = 2500.0f; } // CONFIRMED 50.0f^2

    // --- get_stag_active (Sec53/T20) ---------------------------------------
    // "resolved in full, no crash shape" - no further detail. CHOSEN false
    // initial (an "active" flag's natural boot state).
    bool& stagActive() { return stagActive_; }
    W28GroupVisibility& w28GroupVisibility(const std::string& name) { return w28GroupVisibility_[name]; }
    uint64_t groupShowOnUncreatedGuardCount() const { return groupShowOnUncreatedGuardCount_; }
    void recordGroupShowOnUncreatedGuard() { ++groupShowOnUncreatedGuardCount_; }
    uint64_t groupHideOnUncreatedGuardCount() const { return groupHideOnUncreatedGuardCount_; }
    void recordGroupHideOnUncreatedGuard() { ++groupHideOnUncreatedGuardCount_; }

    // --- notoriety_get (§50.5). Reuses the EXISTING
    // lookupNotorietyTrackCode()/kNotorietyTrackCodeNotFound resolver
    // directly (do not re-derive) - a resolved code's actual numeric LEVEL
    // has no existing backing store anywhere (confirmed by search), so a
    // new per-code map is added; an unresolved/unknown track name is
    // CONFIRMED to push zero Lua values (handled in the stub, not here).
    // HARDENED (CHOSEN divergence, §50/T18 CRASH): the real engine passes
    // a non-string argument straight into the CRT's __stricmp with no
    // null check, which can terminate the process - this project instead
    // folds a non-string argument into the SAME "unknown track" (zero
    // values) path every misspelt-but-string name already takes, counted
    // via the named guard below.
    std::unordered_map<int32_t, double>& notorietyTrackValues() { return notorietyTrackValues_; }
    uint64_t notorietyGetNonStringArgGuardCount() const { return notorietyGetNonStringArgGuardCount_; }
    void recordNotorietyGetNonStringArgGuard() { ++notorietyGetNonStringArgGuardCount_; }

    // --- ai_force_team_idle (§46.3). Reuses the EXISTING
    // resolveTeamId()/teamIdByName_ resolver (sentinel 9 on miss) exactly
    // as-is. CONFIRMED real, intentionally-replicated logic defect (not a
    // memory-safety bug, so NOT hardened away): an unknown/misspelled team
    // name resolves to the SAME sentinel 9 every downstream loop here
    // treats as "match every team," so this project idles all 8 tracked
    // teams in that case too, matching the real documented scope-widening
    // behavior exactly. HARDENED (CHOSEN divergence) only for the
    // SEPARATE, genuine memory-safety finding: a nil/non-string team name
    // reaches a null-pointer dereference in the real hash helper with no
    // guard - this project refuses (no-op) instead, counted below.
    std::array<bool, 8>& teamIdleFlags() { return teamIdleFlags_; }
    uint64_t aiForceTeamIdleNullNameGuardCount() const { return aiForceTeamIdleNullNameGuardCount_; }
    void recordAiForceTeamIdleNullNameGuard() { ++aiForceTeamIdleNullNameGuardCount_; }
    ActionSequenceScriptBlock& actionSequenceScript() { return actionSequenceScript_; }
    // HARDENED (CHOSEN divergence, §46.2 CRASH): action_sequence_is_playing
    // always resets the local player's script mode as a side effect of the
    // query; the real engine's null-tolerant "is locally owned" helper lets
    // this proceed even with no local player, reaching a null+0x2888 write.
    // This project only performs the (test-observable, no-op-shaped) side
    // effect when hasLocalPlayer() is known true; otherwise refused, counted.
    uint64_t actionSequenceIsPlayingNullPlayerGuardCount() const { return actionSequenceIsPlayingNullPlayerGuardCount_; }
    void recordActionSequenceIsPlayingNullPlayerGuard() { ++actionSequenceIsPlayingNullPlayerGuardCount_; }

    // --- player_action_is_pressed (§51.4): "a bounds-safe 166-row action-
    // name lookup" (aggregate text only, no per-action press-state system
    // exists in this project) - a settable per-action map stands in,
    // CHOSEN false ("not pressed") for any action never set by a test.
    std::unordered_map<std::string, bool>& actionPressedFlags() { return actionPressedFlags_; }

    // --- action_play_custom_do (§58 CRASH). CONFIRMED sentinels: -1 dead,
    // 0 play-failed, 1 play-succeeded (truthy even on failure - 0 is
    // truthy in Lua, per §58's own flagged trap). CONFIRMED a resolved,
    // live character ALWAYS reaches 1 regardless of animation-name
    // validity (the real slot player "claims a slot anyway" for an
    // unknown name) - this project has no animation-name table to
    // distinguish known/unknown either way, so this is not a modelling
    // shortfall, it is the documented real behavior. HARDENED (CHOSEN
    // divergence): the real engine skips its own dead-check for an
    // unresolved (null) character and crashes; this project refuses
    // instead, returning the CHOSEN closest documented failure value (0,
    // "play failed" - the real crash has no defined return value at all),
    // counted below.
    uint64_t actionPlayCustomDoUnresolvedGuardCount() const { return actionPlayCustomDoUnresolvedGuardCount_; }
    void recordActionPlayCustomDoUnresolvedGuard() { ++actionPlayCustomDoUnresolvedGuardCount_; }

    // --- set_cower_variant/set_unrecruitable_flag/set_attack_peds_flag/
    // the 3 npc_* setters above all follow this project's established
    // double-gate shape (c.forceFlagGatePasses.get() check against the
    // EXISTING per-character CharacterState field, then either write the
    // W28CharacterMisc field above or EngineState::replicateStateChange) -
    // implemented per-stub, same inlined shape as every other double-gate
    // setter in this codebase (no shared template exists for characters
    // either, confirmed by research - see stub_character_set_no_explosive_
    // panic, lua_spec_confirmed_stubs_b11_batch4_flags_combat.cpp, for the
    // exact pattern this batch's own new stubs copy).

    // --- party_use_melee_weapons_only (§42.2): CONFIRMED applies to the
    // local player AND, separately, the co-op partner if one exists - not
    // to any other party member. Two plain bools (no per-character map
    // needed, this is a whole-player-slot setting).
    bool& partyMeleeOnlyLocal() { return partyMeleeOnlyLocal_; }
    bool& partyMeleeOnlyCoop() { return partyMeleeOnlyCoop_; }

    // =====================================================================
    // Wave 2, Batch W2-2 (spec-lua-api-behaviour.md Sec17+Sec18, 2026-10-08)
    // - see src/lua_spec_confirmed_stubs_b21_wave2_sec17_18.cpp's own doc
    // comment above each stub_* function for the full citation/reasoning.
    // =====================================================================

    // set_stag_active (Sec17.19): a single global byte, no resolver, no
    // gate, no record - the simplest function in its batch. Reuses the
    // SAME stagActive()/stagActive_ get_stag_active (Sec53/T20, above)
    // already models - not a second, duplicate field (get/set on one real
    // global flag).

    // party_set_recruitable (Sec18.28): a single global byte.
    bool& partyRecruitable() { return partyRecruitable_; }

    // notoriety_force_no_air_spawn (Sec18.12): SIX separate global objects'
    // own +0x455 byte, all written identically by the SAME one-line setter
    // every call - collapsed to one flag since nothing in scope
    // distinguishes the six objects from each other.
    bool& notorietyForceNoAirSpawn() { return notorietyForceNoAirSpawn_; }
    void registerHumanKindForTesting(const std::string& kindName) { humanKindResolves_.insert(kindName); }
    bool humanKindResolves(const std::string& kindName) const { return humanKindResolves_.count(kindName) != 0; }

    // set_civilians_flee (Sec17.4), the largest non-COMPLEX entry in Sec17-
    // 22: no generic world-object registry exists in this project (stated,
    // project-wide gap) - the global tracked-object array (0x03171a64) this
    // native walks is modeled as a never-populated-by-default test registry
    // (same convention as QteSlotEntry/registerActiveQteSlotForTesting),
    // so the mass-update loop is a safe no-op until a test registers at
    // least one "civilian" character name. Per-civilian AI-mode-skip gate
    // (0x00ad2f80, "skipped if the mode equals both 2 then 4 in sequence")
    // collapsed to one OPEN predicate per name. The arg-3 anchor resolve
    // (when given and arg 1/flee is true) is tracked as the last resolved
    // anchor NAME (no coordinate system, same project-wide gap). The
    // arg-2-forwarding two-global-write helper (0x009060e0, own role OPEN)
    // is tracked honestly as two flat globals plus a gated pointer-copy
    // counter - not a guess at 0x009060e0's real role.
    void registerCivilianForTesting(const std::string& name) { civilianNames_.push_back(name); }
    const std::vector<std::string>& civilianNames() const { return civilianNames_; }
    OpenValueMap<bool>& civilianFleeAiModeSkip() { return civilianFleeAiModeSkip_; }
    std::string& lastCivilianFleeAnchorName() { return lastCivilianFleeAnchorName_; }
    bool& civilianFleeHelperGlobalA() { return civilianFleeHelperGlobalA_; }
    bool& civilianFleeHelperGlobalB() { return civilianFleeHelperGlobalB_; }
    uint64_t& civilianFleeHelperPointerCopyCount() { return civilianFleeHelperPointerCopyCount_; }

    // group_members_alive (Sec18.14): a dedicated group-name resolver
    // (0x005eab60) and a circular sibling-linked member list - both
    // collapsed to flat, test-registerable state (same "never-populated
    // object space" precedent as the civilian registry above).
    OpenValueMap<bool>& groupResolves() { return groupResolves_; }
    OpenValueMap<bool>& groupHasMembersBit() { return groupHasMembersBit_; }

    // spawn_override_change_hood_alternate_spawning (Sec18.6): a dedicated
    // hood/zone resolver (0x00624610) collapsed to the shared objectResolves()
    // map (Sec29 convention) directly - no separate predicate needed, since
    // nothing else in scope needs to distinguish "hood" resolution from any
    // other named-object resolution. The per-hood toggle bit and the host-
    // only extra notify (reuses coopLocalIsHost() directly) are new.
    std::unordered_map<std::string, bool>& hoodAlternateSpawning() { return hoodAlternateSpawning_; }

protected: // private in EngineState before the split
    std::unordered_map<std::string, uint64_t> npcAiHandles_;       // character name -> runtime handle
    std::unordered_map<uint64_t, std::string> npcAiNames_;         // runtime handle -> character name
    std::unordered_map<std::string, int32_t> npcAiLastHp_;         // last seen hit points (player damage detection)
    std::array<int, 4> npcAiLastTrackLevel_{0, 0, 0, 0};           // notoriety track levels last seen (Sec19.4 flip)
    uint64_t npcAiNextHandle_ = 100;
    NpcAiStats npcAiStats_;
    size_t npcAiOpenReported_ = 0;
    std::unordered_set<std::string> npcAiHostileLogged_;

    // --- action_play_do (Sec22.29), see the public accessor doc comments
    // above for citation/reasoning. Test-observable only, not real engine
    // fields - same convention as vehicleExitGroupNullThisGuardCount_.
    int actionPlayDeadGuardCount_ = 0;
    int actionPlayLookupMissCount_ = 0;
    // drawOrGetMaxHitPoints()'s own real-data source - see
    // installSpawnRankHitPoints()'s doc comment.
    std::vector<int32_t> spawnRankHitPoints_;
    std::unordered_map<std::string, int> teamIdByName_; // Sec30.6/Sec20.6, test-only; unregistered -> sentinel 9
    // CONFIRMED 8x8 (Sec20.6); -1 = "never written" (this project's own sentinel, not a spec fact).
    std::array<std::array<int, kTeamRelationMatrixDim>, kTeamRelationMatrixDim> teamRelation_ = [] {
        std::array<std::array<int, kTeamRelationMatrixDim>, kTeamRelationMatrixDim> m{};
        for (auto& row : m) row.fill(-1);
        return m;
    }();
    int teamRelationOobGuardCount_ = 0; // Sec30.6/Sec30.7 - test-observable only, not a real engine field

    // --- Ranking tranche 05 (Sec33), batch 2026-10-02 - see each public
    // accessor's own doc comment above for citation/reasoning. vehicles_
    // itself is declared once already above (shared with Sec30.5's own
    // vehicle cluster, same VehicleState struct). ---
    std::unordered_map<std::string, int32_t> syncedActionIndexByName_;
    ActionSequence actionSequence_;
    float spawnRegionMaxSpawnDistSquared_ = 3.402823466e+38f; // FLT_MAX, CONFIRMED file-backed default
    float pedOverrideDensity_ = -1.0f;                        // CONFIRMED file-backed default ("no override")
    PartyState party_;

    // Sec44/Sec45/Sec46 tranche follow-up (batch 2026-10-02, resumed session) - see the public accessors above.
    OpenValue<bool> ambientGangSpawnEnabled_{"ambient-gang spawn-enable bytes (all 4, fanned out together)",
                                             "spec-lua-api-behaviour.md Sec45.6"};
    OpenValue<bool> ambientCopSpawnEnabled_{"ambient_cop_spawn_enable global byte", "spec-lua-api-behaviour.md Sec46.3"};
    OpenValue<bool> actionNodesShouldntFlee_{"action_nodes_shouldnt_flee global byte", "spec-lua-api-behaviour.md Sec46.3"};
    OpenValue<bool> actionNodesRestrictSpawning_{"action_nodes_restrict_spawning global byte",
                                                 "spec-lua-api-behaviour.md Sec46.3"};
    SpawnOverride spawnOverride_;
    std::unordered_map<std::string, SquadMember> squadMembers_;
    std::unordered_set<std::string> groupsCreated_;    // Sec53.3
    std::unordered_map<std::string, std::vector<std::string>> pendingGroupMembers_; // lower(group) -> member alias names (ce4556f Sec29)
    uint64_t partyDismissAllCount_ = 0;

    // --- Section 6 batch (2026-10-07, full-document spec sweep) - see each
    // public accessor's own doc comment above for citation/reasoning. ---
    float pedDensity_ = 0.0f;                                   // Sec6.9, CHOSEN default
    bool notorietyDecaySuppressed_ = false;                     // Sec6.18, CHOSEN default
    bool pedestrianSpawningEnabled_ = true;                     // Sec6.13, CHOSEN default
    std::unordered_map<std::string, int32_t> notorietyTrackCodes_;
    std::unordered_map<uint32_t, bool> notorietyTrackFlags_;
    std::unordered_map<std::string, bool> spawnRegionEnabled_;
    std::unordered_map<std::string, bool> continuousSpawnActive_;
    std::unordered_map<std::string, uint64_t> continuousSpawnStopCount_;
    std::unordered_map<std::string, bool> continuousSpawnLastStopBoolean_;
    std::unordered_map<std::string, ContinuousSpawnRegionRecord> continuousSpawnRegions_; // Sec4.8, batch 2026-10-08
    // continuous_spawn_start arg 4 default (type-name filter): CONFIRMED
    // "" per spec-lua-api-behaviour.md Sec4.8.
    std::string continuousSpawnDefaultTypeFilter_ = "";
    std::unordered_map<std::string, int32_t> actionNameIndices_;

    // --- Batch 4 (Sec13+Sec14, native-implementation batch 2026-10-08) -
    // see each public accessor's own doc comment above for citation/
    // reasoning. ---
    OpenValueMap<bool> notorietySpawnGroupResolves_{"notoriety spawn-group resolution (0x008ed350)",
                                                     "spec-lua-api-behaviour.md Sec13.2"};
    std::unordered_map<std::string, NotorietySpawnGroupState> notorietySpawnGroups_;

    // --- batch b29 (2026-10-08): the rest of Section 6 - see each public
    // accessor's own doc comment above for citation/reasoning. ---
    std::unordered_map<std::string, int32_t> notorietyAliasTrackCodes_; // 0x00605160, seeded below
    std::unordered_map<int32_t, NotorietyAliasTrackState> notorietyAliasTrackStates_;
    uint64_t notorietyUiMeterResetCount_ = 0;
    OpenValueMap<bool> notorietyGroupTierInRangeResolves_{"notoriety group tier in-range (+0x3e0/+0x3e4, per-group not given)",
                                                           "spec-lua-api-behaviour.md Sec6.2"};
    std::unordered_map<std::string, uint8_t> spawnRegionFilterBits_;
    std::unordered_map<std::string, int32_t> aiPersonalityProfileCodes_;
    std::unordered_map<std::string, int32_t> behaviorSetCodes_;
    OpenValueMap<bool> syncedActionAppliesLocally_{"action_play_synced_state's own per-actor local-authority gate",
                                                   "spec-lua-api-behaviour.md Sec14.1"};
    bool actionNodesEnabled_ = false;
    bool copAmbientSpawnsAllowed_ = true;
    OpenValueMap<std::string> teamNameByObject_{"per-object team-name reverse lookup (0x02623a80)",
                                                "spec-lua-api-behaviour.md Sec20.23"};
    std::unordered_map<std::string, NotorietyOverrideTimer> notorietyOverrideTimers_;
    OpenValueMap<bool> actionNodeTypeNameValid_{"action-node type-name validity (0x008c8550/0x008c7ba0)",
                                                "spec-lua-api-behaviour.md Sec21.2"};
    bool notorietySpawnOnlyAir_ = false;
    NotorietyRestrictedZones notorietyRestrictedZones_;
    bool actionStopSyncedStateFlag_ = false;
    std::unordered_map<std::string, ContinuousSpawnOrder> continuousSpawnOrders_;
    uint64_t groupDestroyedCount_ = 0;
    bool gangAmbientSpawnsAllowed_ = false;
    std::unordered_map<std::string, bool> spawnRegionLos_;
    bool saintsHated_ = false;
    bool stagNotorietyAreaActive_ = false;
    uint64_t stagNotorietyAreaHostStepCount_ = 0;
    OpenValueMap<ClosestNpcResult> closestNpcOverride_{"get_closest_npc_to_object query (no coordinate system here)",
                                                       "spec-lua-api-behaviour.md Sec41.6"};
    SpawnGlobalOverride spawnGlobalOverride_;
    // spawnOverrideCategoryForHood_ (the old disconnected per-hood-clear-only
    // placeholder map) removed 2026-10-09 (spec sync 3ecbf84): SET and CLEAR
    // now both read/write the SAME real state, SpawnOverride::perHoodCategory/
    // ::perHoodCategoryNullStored above - see spawnOverrideClearOverrideForHood()'s
    // own doc comment for the bug this fixes.
    float spawnRegionMinSpawnDistSquared_ = 2500.0f; // CONFIRMED file-backed default, 50.0f^2 (spec sync 3ecbf84)
    bool stagActive_ = false;
    std::unordered_map<std::string, W28GroupVisibility> w28GroupVisibility_;
    uint64_t groupShowOnUncreatedGuardCount_ = 0;
    uint64_t groupHideOnUncreatedGuardCount_ = 0;
    std::unordered_map<int32_t, double> notorietyTrackValues_;
    uint64_t notorietyGetNonStringArgGuardCount_ = 0;
    std::array<bool, 8> teamIdleFlags_{};
    uint64_t aiForceTeamIdleNullNameGuardCount_ = 0;
    ActionSequenceScriptBlock actionSequenceScript_;
    uint64_t actionSequenceIsPlayingNullPlayerGuardCount_ = 0;
    std::unordered_map<std::string, bool> actionPressedFlags_;
    uint64_t actionPlayCustomDoUnresolvedGuardCount_ = 0;
    bool partyMeleeOnlyLocal_ = false;
    bool partyMeleeOnlyCoop_ = false;
    // --- Wave 2, Batch W2-2 (2026-10-08), spec-lua-api-behaviour.md
    // Sec17+Sec18 - see each public accessor's own doc comment above for
    // citation/reasoning. (stagActive_ itself is NOT re-declared here -
    // reuses the existing field above, see set_stag_active's own doc
    // comment on the public accessor side.) ---
    bool partyRecruitable_ = false;
    bool notorietyForceNoAirSpawn_ = false;
    std::unordered_set<std::string> humanKindResolves_;
    std::vector<std::string> civilianNames_;
    OpenValueMap<bool> civilianFleeAiModeSkip_{"set_civilians_flee's own per-civilian AI-mode-skip gate (0x00ad2f80)",
                                               "spec-lua-api-behaviour.md Sec17.4"};
    std::string lastCivilianFleeAnchorName_;
    bool civilianFleeHelperGlobalA_ = false;
    bool civilianFleeHelperGlobalB_ = false;
    uint64_t civilianFleeHelperPointerCopyCount_ = 0;
    OpenValueMap<bool> groupResolves_{"group-name resolution (0x005eab60)", "spec-lua-api-behaviour.md Sec18.14"};
    OpenValueMap<bool> groupHasMembersBit_{"group 'has members' bit (0x2@+0x3a)", "spec-lua-api-behaviour.md Sec18.14"};
    std::unordered_map<std::string, std::vector<std::string>> groupMemberNames_;
    std::unordered_map<std::string, bool> hoodAlternateSpawning_;
};

} // namespace sr3luahost
