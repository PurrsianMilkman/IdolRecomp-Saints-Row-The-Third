// EngineState slice: characters and the player: sprint, skydive, rappel, player rig.
//
// Split out of engine_state.h (refactor step 3a, 2026-10-17) as a zero-behaviour-change move: every
// type, field, initialiser and comment below moved verbatim (namespace-scope types re-indented one
// level). Types that were nested in EngineState are now namespace-scope types in sr3luahost;
// EngineStateCharactersData re-exports each one under its old name, so `EngineState::Type` still
// resolves. EngineState inherits EngineStateCharactersData publicly, so `state.member`,
// `EngineState::kConstant` and `state.accessor()` are unchanged for every caller. Members that
// were private in EngineState are `protected` here (the only access change). Method
// declarations stay in EngineState (engine_state.h); bodies are in src/*.cpp.
#pragma once

#include "sr3luahost/engine_state_common.h"

namespace sr3luahost {

// The weapons damage model (sr3weapons::ApplyDamageSink, spec-weapons-runtime.md Sec4.5.4) applied to a
// named character: builds the sink inputs from this state, sets the new HP, records the attacker, and
// reaches the same death tail as applyCharacterDamage (so the death callbacks follow on the next tick).
// `flags` is the sink's F word.
struct DamageModelResult { bool applied = false; float damage = 0.0f; int32_t newHp = 0; bool killed = false; bool downed = false; };

// Sec28.12 `customization_restore_player_rig` / Sec28.21 `character_get_gender` (characterGender() below,
// a SEPARATE per-name map: Sec28.21 resolves by name through the generic chain, Sec28.12 reads a dedicated
// "local player 1"/"co-op player" accessor instead - two genuinely different access paths per the spec text).
struct PlayerRig {
    OpenValue<int32_t> localPlayer1Gender{"local player 1's own +0xa41 gender byte (0x009da4e0)",
                                          "spec-lua-api-behaviour.md Sec28.12"};
    OpenValue<bool> coopPlayerPresent{"0x009df3d0 non-null (second/co-op player)", "spec-lua-api-behaviour.md Sec28.12"};
    OpenValue<int32_t> coopPlayerGender{"co-op player's own +0xa41 gender byte", "spec-lua-api-behaviour.md Sec28.12"};
    uint64_t restorePlayer1Count = 0;
    uint64_t restoreCoopCount = 0;

    // teleport_player_vehicles (Sec22.5, wave 2 batch W2-4): a NEW
    // cached-vehicle-association field pair per player, +0x2880/+0x2884
    // on the PLAYER object - CONFIRMED distinct from the generic
    // character +0x16c0/+0x16c4 pair (CharacterState::currentVehicleName)
    // every other vehicle-association reader in this project already
    // uses. "" = no associated vehicle. coopPlayerPresent above (already
    // modelled, Sec28.12) gates whether player2's own half of this
    // native's relocate ever runs.
    std::string player1VehicleName;
    std::string player2VehicleName;
    uint64_t teleportPlayer1Count = 0;
    uint64_t teleportPlayer2Count = 0;

    // party_allow_max_followers (Sec22.9, wave 2 batch W2-4): per-player
    // "allow max followers" sync flag via the SAME double-gate (variant
    // 1, 0x008ae480/0x008837a0) this header's own forceFlagGatePasses
    // fields already model elsewhere - CONFIRMED (Sec3.7's own single-
    // player resolution) that SAME primitive pair always takes the
    // direct-local-apply branch with no session installed, so a plain,
    // always-true-by-default bool (not an OpenValue gate) stands in,
    // matching forceFlagGatePasses's own "seeded true at construction,
    // not OPEN" precedent, scaled down since PlayerRig has no per-
    // instance construction hook to seed from. Default true (Sec22.9's
    // own nil-gated-argument default).
    bool player1AllowMaxFollowers = true;
    bool player2AllowMaxFollowers = true;
    uint64_t allowMaxFollowersReplicateCount = 0;
};

// Sec28.19 `character_set_counter_on_grabbed`.
struct CounterOnGrabbed {
    // UPDATE 2026-10-03 (Sec3.7's own single-player resolution, cross-
    // ref Sec6.22): this is the SAME 0x008ae480/0x008837a0 gate pair
    // Sec3.7 settles, so it is CONFIRMED true in single player too -
    // seeded true on first touch in stub_character_set_counter_on_
    // grabbed (lua_spec_confirmed_stubs.cpp), not left OPEN awaiting a
    // test. A test can still .set(name, false) to exercise the now
    // known-dead replicate-only branch for structural coverage.
    OpenValueMap<bool> gatePasses{
        "double-gate (0x008ae480/0x008837a0 direct OR 0x008ae3a0 replicate) passes, abstracted to one flag",
        "spec-lua-api-behaviour.md Sec28.19"};
    std::unordered_map<std::string, bool> value; // the per-character flag this batch writes; CHOSEN, write-only in scope
};

// Sec32.3 `skydive_setup_tank_bailout`: the mission-18 cargo-plane/tank
// set piece. No real name resolve at all (both vehicles are hardcoded
// literals, never argument-driven) - a plain counter trio, CONFIRMED
// per-stage branch structure; the named-vehicle-animation dispatch
// internals are not modeled.
struct SkydiveTankBailout {
    uint64_t broadcastCount = 0;   // CONFIRMED: always sent, every stage, regardless of vehicle existence
    uint64_t armedCount = 0;       // stage 1
    uint64_t animStartedCount = 0; // stage 2
};

// skydive_move_to_check_done (Sec49.3, `0x00a20840` gameplay
// registrar, `gameplay`). Argument shape NOT independently re-derived
// this pass (same "HIGH CONFIDENCE, not CONFIRMED" caveat
// turn_to_check_done's own doc comment already carries for an
// analogous gap) - CHOSEN, labelled: 1 string (the target name) + 1
// boolean ("list mode" selector; true = the sibling branch the spec
// calls already-correct, false = the single-object-target-mode branch
// the crash shape belongs to). CONFIRMED (Sec49.3): in single-object
// mode, when the target doesn't resolve, the real code computes
// `null_pointer+0x40` and tests THAT SUM (which survives the null
// check) rather than the pointer itself - a one-off miss; the
// list-mode branch handles the identical failure correctly and
// returns "done". HOST-SAFETY DEVIATION (not a spec fact): rather than
// reproducing the crash, this host gives the SAME answer the
// already-correct sibling branch gives for the identical failure
// ("done", pushed true) - counted via nullTargetGuardCount. A
// resolved target's own done/pending determination (in EITHER mode)
// is OPEN - Sec49.3 gives no further detail beyond the crash shape
// itself, so the stub refuses that case via OpenStateError rather
// than inventing one.
struct SkydiveMoveToCheckDone {
    uint64_t nullTargetGuardCount = 0; // HOST-SAFETY - test-observable only, not a real engine field
};

// set_char_in_string (Sec49.4, `ui`). 1 string + 1 0-based index
// (unlike Lua's usual 1-based convention, CONFIRMED) + 1 single-
// character string. CONFIRMED real crash shapes: (1) "builds its
// result in a stack buffer sized by the index with no cap - a large
// or NaN-derived index can overrun the stack guard outright, reachable
// from script data alone"; (2) "either string argument being non-a-
// string reads through null" - already avoided by construction here
// via this file's own established NULL-safe argString() wrapper (same
// precedent noted for team_make_hostile's identical-shaped defect,
// Sec38.2), nothing further to guard for (2). This project has no
// literal stack guard to overrun (standing project note), so (1) is
// modeled as a MANDATED bounds check: a CHOSEN cap, `kMaxIndex`, below
// the real buffer's own unconfirmed size - chosen as 1024, the SAME
// magnitude independently confirmed elsewhere in this binary for a
// fixed stack string buffer (spec-lua-api-behaviour.md Sec44's
// `game_coop_kick_player`/`0x00db0510` finding: "always tells the CRT
// its buffer holds 1024 wide characters") - NOT claimed to be the SAME
// buffer, only a reasoned size-class stand-in, clearly not a spec
// fact. An index outside [0, kMaxIndex] is guarded and logged rather
// than built. The in-bounds result-construction itself (pad with a
// CHOSEN space fill to reach the index, then write the given
// character there) is this project's own CHOSEN, labelled minimal
// reading of "set a char in a string" - Sec49.4 gives no further
// detail on the real padding/construction algorithm beyond the crash
// shape, so nothing here claims to be the real byte-for-byte result.
struct SetCharInString {
    static constexpr int64_t kMaxIndex = 1024; // CHOSEN cap, see comment above
    uint64_t indexOutOfBoundsGuardCount = 0;   // HOST-SAFETY - test-observable only, not a real engine field
};

// skydive_set_catching_shaundi (Sec20.7): fixed-name entities (no Lua
// name argument at all) - a single EngineState-level record, matching
// the real function's own fixed globals (0x0130a8e4 "active" sentinel,
// 0x0262d3b8 "catching" sub-mode, 0x0130a8e8 cached conversation id).
struct SkydiveCatchingShaundi {
    int32_t activeSentinel = -1; // -1 = inactive (CONFIRMED sentinel); 0 = active
    bool catchingSubMode = false;
    int32_t cachedConversationId = -1; // -1 = none
    uint64_t acquireCount = 0;
    uint64_t releaseCount = 0;
    uint64_t conversationStopCount = 0;
    uint64_t networkRecordCount = 0; // unconditional every call (hardcoded 3rd arg false, CONFIRMED)
};

// --- rappel_enter / rappel_exit (Sec39.1) -----------------------------
// CONFIRMED: "a queued rappel request is stored in a fixed 2-slot
// table (slot 0 = local player, slot 1 = every other character - two
// different NPCs' queued rappels silently overwrite each other's
// slot)." This project has no coordinate/position system to drive the
// real landing logic (+0xcc8/+0xcce sub-states), so only the
// CONFIRMED-structure queue-slot assignment and call counts are
// tracked, honestly, not a simulated landing.
struct RappelQueue {
    std::string slot0; // local player's own queued target name ("" = none)
    std::string slot1; // every OTHER character's queued target name (shared slot, CONFIRMED collision)
};

// --- A consolidated per-character "misc small flag" record for the
// remaining §39.1/§41.5/§42.2/§49.3/§50.5/§57.5 natives this batch
// implements whose exact backing byte/bit offset this spec text does
// NOT give individually (npc_brute_always_throws, npc_detection_enable,
// npc_weapon_pickup_override, set_attack_peds_flag,
// npc_brute_set_no_downed_finisher, npc_dont_auto_equip_or_unequip_
// weapons, npc_is_in_brute_grab_qte,
// human_has_taken_non_melee_damage_from_player, character_is_combat_
// ready, get_char_skydiving_state, set_cower_variant,
// override_npc_run_and_cower_run, set_unrecruitable_flag) - CHOSEN:
// one small per-character map of plain fields rather than 13 separate
// single-field maps, purely to keep this already-large batch's own
// engine_state.h footprint manageable; each field's own CONFIRMED
// default (where the spec states one) is preserved exactly.
struct W28CharacterMisc {
    bool bruteAlwaysThrows = false;
    bool npcDetectionEnabled = false;       // CONFIRMED default false (bare boolean)
    bool npcWeaponPickupOverrideArg = false; // raw Lua argument, CONFIRMED default false (bare boolean) - the real stored bit is this value's logical NOT (§42.2); no getter native is in this batch, so only the raw argument is kept, not the inverted bit
    bool attackPedsFlag = false;
    bool bruteNoDownedFinisher = false;
    bool dontAutoEquipUnequipWeapons = false; // CONFIRMED default false (unconditional read, §50.5 - the one exception to this family's usual default-true idiom)
    bool inBruteGrabQte = false;
    bool tookNonMeleeDamageFromPlayer = false; // §41.5: "who sets the bit is OPEN" - no setter in this batch's own 93 names, so this can only ever be false unless a test sets it
    bool combatReady = false; // CHOSEN default for a resolved character (§57.5 gives no real combat-readiness model; only the unresolved-name "no value" sentinel is CONFIRMED)
    int32_t skydivingState = 255; // CONFIRMED sentinel "found, not currently skydiving" (§41.5) - the CHOSEN default for any resolved character, since no sub-state model exists
    int32_t cowerVariant = -1; // CHOSEN "no variant set" default (§39.1 gives the write-side clamp/mapping, not a rest-state default)
    int32_t runAnimOverride = -1;      // override_npc_run_and_cower_run (§42.2): CONFIRMED -1 = "no override" for an unknown/nil name
    int32_t cowerRunAnimOverride = -1; // same native, second animation-set name
    uint32_t forceFlags1e02 = 0; // set_unrecruitable_flag (§39.1): CONFIRMED bit 0x10 of character byte +0x1e02 - an external stand-in (see this file's own "new feature -> external map" convention note above), not inserted into CharacterState itself
};

struct EngineStateCharactersData {
    // Former EngineState nested types (defined above at namespace scope), re-exported so
    // `EngineState::Type` keeps naming them.
    using DamageModelResult = sr3luahost::DamageModelResult;
    using PlayerRig = sr3luahost::PlayerRig;
    using CounterOnGrabbed = sr3luahost::CounterOnGrabbed;
    using SkydiveTankBailout = sr3luahost::SkydiveTankBailout;
    using SkydiveMoveToCheckDone = sr3luahost::SkydiveMoveToCheckDone;
    using SetCharInString = sr3luahost::SetCharInString;
    using SkydiveCatchingShaundi = sr3luahost::SkydiveCatchingShaundi;
    using RappelQueue = sr3luahost::RappelQueue;
    using W28CharacterMisc = sr3luahost::W28CharacterMisc;

public:
    // Harness bookkeeping (--auto-objective): names the script polled with character_is_dead while alive.
    void noteDeathPollQuery(const std::string& name) { deathPollQueries_.push_back(name); }
    std::vector<std::string> drainDeathPollQueries() { std::vector<std::string> out; out.swap(deathPollQueries_); return out; }
    // Characters in creation order (the manager's index list).
    const std::vector<std::string>& characterCreationOrder() const { return characterOrder_; }
    // Harness bookkeeping (--auto-objective): negative character_has_specific_human_shield queries this frame.
    void noteShieldPollQuery(const std::string& holder, const std::string& hostage) { shieldPollQueries_.push_back({holder, hostage}); }
    size_t characterCount() const { return characters_.size(); }
    // Same convention as vehicles() below - exposed by mutable reference so
    // the real seat-pick validator's step 6/7 occupant-conflict scan
    // (spec-player-runtime.md Sec13.4, computeVehicleEnterSubmitAccepted() /
    // attemptGroupInstantEnter() in lua_spec_confirmed_stubs_b15_wave2_core_
    // dispatchers.cpp) and vehicle_enter_group_check_done's per-member
    // completion walk (spec-lua-api-behaviour.md Sec38.1) can walk every
    // character this host currently knows about, reusing the one existing
    // map rather than a parallel occupancy registry this project does not
    // have (added 2026-10-09).
    std::unordered_map<std::string, CharacterState>& characters() { return characters_; }
    // player_controls_disable's lock handle, the global 0x0263ae3c
    // (Sec7.22/Sec7.7; spec-player-runtime.md Sec2.6 push site 0x009e2ef2).
    // OPEN: no spec gives its static value. (Every keeper tests its handle
    // for 0 and zeroes it after popping, Sec2.6, which suggests a zero
    // static - an inference, not a stated value, so not applied.)
    OpenValue<uint32_t>& playerControlsLockHandle() { return playerControlsLockHandle_; }

    // --- game_hud_update_inventory (Sec10.3) --------------------------------

    // Real body (CONFIRMED, disassembly): reads the current-local-player
    // accessor and, if a local player object exists, triggers a refresh
    // of that player's own HUD inventory display (OPEN: whether the real
    // refresh is a full rebuild vs. a dirty-flag set - not modeled, per
    // this task's own brief).
    //
    // CLOSED 2026-10-09 (mission-census item, missions m17/m01; routed as
    // "local_player_exists" but Sec10.3's own text confirms no Lua native
    // by that name, or any close variant, exists in the full 1,490-entry
    // registered list - `game_hud_update_inventory` is simply one of 20+
    // natives sharing this SAME accessor, 0x009da4e0, and is this
    // project's own chosen home for the finding): `0x009da4e0`'s real
    // body is trivial - it returns the dword at the fixed global address
    // `0x0262edfc` directly, the local player's own live game-object
    // instance, not a resolver chain. Traced all 3 real writers: SET
    // (non-null) by `0x009e1fc0` (the boot-time player-creation/spawn
    // routine) and, independently, by `0x009e3bf0` (a character-preset-
    // swap/rebuild routine, same class id); CLEARED to null by
    // `0x009da150`, a generic per-instance teardown routine, but ONLY when
    // the object being destroyed is the one the global currently points
    // at (a plain pointer-identity check - any other instance's teardown
    // takes an unrelated branch). So "a local player exists" is really
    // "has either constructor path run since the last matching teardown" -
    // populated once per player-object lifetime, nulled exactly when that
    // SAME object is torn down.
    //
    // `hasLocalPlayer_` therefore now carries a CONFIRMED default of true,
    // pre-seeded by applySpecInitialState (lua_spec_initial_state.cpp,
    // item 16 - renumbered when merged alongside the pre-existing items
    // 13-15 added independently on main) - same "pre-seed a newly-CONFIRMED
    // default instead of leaving an OpenValue OPEN" convention the
    // notoriety spawn-group name-table fix used (Sec6.2/Sec13.2,
    // 2026-10-09): a running
    // single-player game has already run one of the 2 real constructor
    // paths before any script's own first tick, and the live playtest
    // viewer (tools/sr3_viewer.cpp) independently reaches the identical
    // conclusion by explicitly calling `.set(true)` for its own HUD/
    // mission sessions. OPEN, honestly flagged, NOT papered over: the "1
    // conditional clearer" is real and identifiable (ordinary player-
    // object teardown, e.g. a respawn/session-end rebuild cycle that
    // tears down the old instance before `0x009e1fc0`/`0x009e3bf0` builds
    // the new one) - this project does not model that teardown window, so
    // a script tick that genuinely runs DURING it would see this field
    // still (wrongly) true. Remains a plain OpenValue, not a bare bool:
    // `.set(false)`/`.forget()` stay available, and every existing test
    // that deliberately exercises the "no local player" path keeps doing
    // so by calling them explicitly, exactly as before this pre-seed.
    // Test-only setter. `recordHudInventoryRefresh()`/
    // `hudInventoryRefreshCount()` are this project's own no-op stand-in
    // for "a real HUD refresh happened" (no real HUD to refresh) - a
    // plain incrementing counter, same shape choice as
    // nextAudioVoiceHandle() above.
    OpenValue<bool>& hasLocalPlayer() { return hasLocalPlayer_; }
    PlayerRig& playerRig() { return playerRig_; }
    CounterOnGrabbed& counterOnGrabbed() { return counterOnGrabbed_; }

    // Sec28.20 `character_remove_child_item_by_name`: this project has no real class-descriptor/children-list
    // engine data; this is this project's own minimal, directly-editable stand-in for the real +0x1c/+0x20
    // list, matching the functional contract (remove every item named X, case-insensitively) without simulating
    // the two descriptor-bit gates (Sec28.20's own corrected 0x00853b30 reading).
    std::unordered_map<std::string, std::vector<std::string>>& characterChildItems() { return characterChildItems_; }

    // Sec28.21 `character_get_gender`: keyed by character name (see the PlayerRig note above for why this is a
    // separate map from localPlayer1Gender/coopPlayerGender).
    OpenValueMap<int32_t>& characterGender() { return characterGender_; }

    // Sec32.2 character force-flag setters (write-only in scope, no Lua
    // getter among these three names) - CHOSEN per-name maps, same
    // "write-only, CHOSEN" convention as helicopterDontDeathSpiral above.
    std::unordered_map<std::string, bool>& seatbeltForceFlag() { return seatbeltForceFlag_; }         // +0x1c9c bit 0x40
    std::unordered_map<std::string, bool>& trailingAimForceFlag() { return trailingAimForceFlag_; }   // +0x1c9c bit 0x1
    std::unordered_map<std::string, bool>& neverTurnOnPlayerFlag() { return neverTurnOnPlayerFlag_; } // +0x2b9 bit 0x40 (AI sub-object)

    // Sec32.3 `player_warp_to_shore_disable`: +0x28ad bit 0x2, write-only in
    // scope (CHOSEN per-name map; always true per Sec32.3's own hardcoded
    // setter argument).
    std::unordered_map<std::string, bool>& warpToShoreDisabled() { return warpToShoreDisabled_; }
    SkydiveTankBailout& skydiveTankBailout() { return skydiveTankBailout_; }

    // skydive_move_to_do (Sec39.1): "the target resolves through the
    // generic third-tier resolver with NO guard on failure: an absent/
    // unresolved name leaves the target pointer at literal zero, and the
    // function then unconditionally copies 12 bytes from address 0x40 into
    // the character - a genuine access violation" (CONFIRMED real defect,
    // new this tranche). HOST-SAFETY DEVIATION (not a spec fact): this
    // host skips the equivalent copy on an explicitly-known-unresolved
    // non-path target instead of performing it, counting the averted path
    // here - "this project does not simulate crashes" (cf. group_get_
    // next_npc, Sec28.5, the established precedent for this exact idiom).
    int skydiveMoveToNullTargetGuardCount() const { return skydiveMoveToNullTargetGuardCount_; }
    void recordSkydiveMoveToNullTargetGuard() { ++skydiveMoveToNullTargetGuardCount_; }
    SkydiveMoveToCheckDone& skydiveMoveToCheckDone() { return skydiveMoveToCheckDone_; }
    SetCharInString& setCharInString() { return setCharInString_; }

    // character_is_ready (Sec7.27) / character_playing_combat_move
    // (Sec25.27). Both OPEN until set - no spec gives a default, and unlike
    // CharacterState::isAlive (0x0096f4f0, already seeded at construction
    // per Sec34.5's own zero-filled-allocation reasoning), neither of these
    // predicates has any stated construction-time value anywhere in scope.
    OpenValueMap<bool>& characterReadyBlocked() { return characterReadyBlocked_; }
    OpenValueMap<bool>& characterPlayingCombatMove() { return characterPlayingCombatMove_; }

    // --- Batch 2026-10-08 (Sec5-9 fan-out, Batch 2) - see each field's
    // own doc comment (CharacterState/VehicleState above, or inline below)
    // for the full citation/reasoning. ---

    // set_animation_state (Sec7.11) / set_script_animation_state
    // (Sec7.17): shared name->state-id table stand-in (0x004bf810) - see
    // CharacterState's own doc comment (above, near the batch header just
    // before attackPlayerBitAc) for the full "collapses two real lookups
    // into one" reasoning. -1 = unregistered/absent (CHOSEN, matches the
    // spec's own already-established "-1 means no override/no entry"
    // convention, Sec7.3/Sec7.11/Sec7.17).
    void registerAnimationStateIdForTesting(const std::string& name, int32_t id) { animationStateIds_[name] = id; }
    double playerSkydiveFallSpeed() const { return playerSkydiveFallSpeed_; }

    // character_ragdoll_clear_last_resort_position (Sec13.6): no per-object
    // target at all (CONFIRMED - the function reads no argument). A single
    // GLOBAL cache clear, always replicated (literal 3 runs both branches,
    // CONFIRMED). DIFFERENT from the already-registered character_ragdoll_
    // set_last_resort_position and from CharacterState::
    // ragdollValidPositionHistory above (Sec13.25) - see the scoping
    // report's own near-duplicate warning.
    uint64_t& ragdollLastResortPositionClearCount() { return ragdollLastResortPositionClearCount_; }
    OpenValue<std::string>& localPlayerNameForTesting() { return localPlayerName_; }

    // set_script_animation_group (Sec21.15): the group-NAME to id table
    // (0x004cce70) was never diffed against any other already-documented
    // table (OPEN, per the spec's own text) - a dedicated, test-settable
    // map, distinct from resolveTeamId()/lookupNotorietyTrackCode()'s own
    // tables.
    std::unordered_map<std::string, int32_t> animationGroupIdByName;

    // human_skydive_create_npc_backpack (Sec20.20): single-slot global
    // tracker (0x0262d3b0) - "" = no character currently tracked.
    std::string currentTrackedBackpackCharacterName;
    uint64_t backpackCreatedCount = 0;
    uint64_t backpackPriorReleasedCount = 0;
    uint64_t backpackSecondaryVisualCreatedCount = 0;

    // character_ignite (Sec20.29): the fixed 12-simultaneous-fires tracking
    // array (CONFIRMED hard cap, previously-uncited) - this project tracks
    // the SET of currently-on-fire character names directly (order/slot
    // reuse is not independently observable without a real timer system),
    // same cap.
    static constexpr size_t kCharactersOnFireCap = 12;
    std::unordered_set<std::string>& charactersOnFire() { return charactersOnFire_; }
    SkydiveCatchingShaundi& skydiveCatchingShaundi() { return skydiveCatchingShaundi_; }

    // base_jumping_enable (Sec22.28): local-apply setter that ALWAYS ALSO
    // fires a notification record (unconditional, not host-gated - the
    // FIRST confirmed opcode-0x44 sighting going through the local-only
    // commit sibling, per Sec22.28's own text). Default true (Sec22.28's own
    // nil-gated-argument default).
    bool& baseJumpingEnabled() { return baseJumpingEnabled_; }
    uint64_t& baseJumpingReplicateCount() { return baseJumpingReplicateCount_; }

    // skydive_set_elev_angles_deg (Sec24.15): CONFIRMED exact degrees-to-
    // radians conversion (byte-confirmed constant 0x012a30d8 = pi/180).
    double& skydiveElevAngleA() { return skydiveElevAngleA_; }
    double& skydiveElevAngleB() { return skydiveElevAngleB_; }

    // is_rappelling (Sec22.16): HARDENING POLICY counter for the resolve-
    // failure path (see src/lua_spec_confirmed_stubs_b22_wave2_sec22.cpp's
    // own stub doc comment) - a genuine, real "near-null read, not
    // independently verified to fault" finding this host deliberately does
    // NOT reproduce as an unverified crash, answering `false` instead.
    uint64_t& isRappellingResolveFailureSafeCount() { return isRappellingResolveFailureSafeCount_; }
    RappelQueue& rappelQueue() { return rappelQueue_; }
    uint64_t& rappelEnterQueuedCount() { return rappelEnterQueuedCount_; }
    uint64_t& rappelEnterImmediateCount() { return rappelEnterImmediateCount_; }
    uint64_t& rappelExitCount() { return rappelExitCount_; }

    // --- force_throw_char_do (Sec55.4) - CRASH hardening counter -----------
    // CONFIRMED: "computes character + 0x40 as a position pointer for the
    // target resolver BEFORE the character is null-checked." HARDENED:
    // this host checks the character for null FIRST (the fix order its own
    // sibling force_throw_do already uses, per spec's own text) instead of
    // reproducing the address-0x40 read, counted here.
    uint64_t& forceThrowCharNullCharacterGuardCount() { return forceThrowCharNullCharacterGuardCount_; }

    // --- Sec53.5 get_char_in_string/get_localized_string_for_tag/
    // get_world_income_dollars -----------------------------------------------
    uint64_t& getCharInStringNullGuardCount() { return getCharInStringNullGuardCount_; }

    // --- player_override_* sprint family (§40.1, 6 names). CONFIRMED
    // GLOBAL (not per-player), file-backed -1 ("no override") statics,
    // mission-scoped (reset to -1 at mission start/restart and registered
    // as a session-synced variable, mechanism 0x0086d770) - but this
    // project has NO mission-start/restart hook and NO generic "session-
    // synced variable" class anywhere in ~7800 lines of existing state
    // (confirmed by direct search: every spec "session-synced variable"
    // write-up elsewhere in this codebase was implemented as a bare field,
    // e.g. allowWeaponAutoPickup() below) - so the mission-scoped reset is
    // OPEN/not wireable in this project as it stands, same documented gap
    // as every other mission-scoped variable here; seeded at the CHOSEN
    // real file-backed default (-1) directly instead.
    double& sprintUseOverride() { return sprintUseOverride_; }
    double& sprintRechargeOverride() { return sprintRechargeOverride_; }
    double& sprintDelayOverride() { return sprintDelayOverride_; }

    // --- players_naked (§51.4): a strip-and-resync routine covering both
    // local and remote co-op players - this project has no single
    // PlayerRig-level "naked" bool yet (PlayerRig above tracks gender/
    // presence only), so two plain bools stand in for "local"/"coop
    // partner," CHOSEN false at construction (no spec-given start value).
    bool& localPlayerNaked() { return localPlayerNaked_; }
    bool& coopPlayerNaked() { return coopPlayerNaked_; }

    // --- human_being_revived/_revived_by (§54.3), human_downed_set_mars_
    // kb_interactable (§54.3). CONFIRMED sentinels: revived-by's own
    // single-player literal is "#NETID#0" (no session ever exists in
    // single player, §8.27/§26.28) when revived by the local player, nil
    // otherwise (never revived, or revived by a non-player NPC - this
    // project cannot distinguish "non-player NPC revived them" from
    // "never revived" without an NPC-reviver identity model, OPEN, folded
    // into the same nil path). mars_kb's own documented "no repeat-call
    // guard... leaks" defect is reproduced via the counter below, which
    // increments whenever a call overwrites an ALREADY-set id.
    std::unordered_map<std::string, bool>& revivedFlags() { return revivedFlags_; }
    std::unordered_map<std::string, bool>& revivedByLocalPlayerFlags() { return revivedByLocalPlayerFlags_; }
    std::unordered_map<std::string, int64_t>& marsKbInteractableId() { return marsKbInteractableId_; }
    uint64_t marsKbInteractableLeakCount() const { return marsKbInteractableLeakCount_; }
    void recordMarsKbInteractableLeak() { ++marsKbInteractableLeakCount_; }
    W28CharacterMisc& w28CharacterMisc(const std::string& name) { return w28CharacterMisc_[name]; }

    // --- character_parachute_open (§57.5): CONFIRMED real resource-leak
    // defect - every call spawns a new parachute object with no reuse/
    // cleanup check. A single monotonic counter (not per-character) is
    // enough to make the leak shape test-observable.
    uint64_t parachuteOpenSpawnCount() const { return parachuteOpenSpawnCount_; }
    void recordParachuteOpenSpawn() { ++parachuteOpenSpawnCount_; }

    // player_can_sprint (Sec18.26): the global sprint-capability byte, plus
    // the disable-cascade's own further writes - no per-character target in
    // this project (the real "current player" accessor, 0x009da4e0, has no
    // clean per-name handle here; same project-wide "singular local player"
    // simplification several other natives already apply, e.g.
    // PlayerRig::localPlayer1Gender). sprintModeIsFive CHOSEN false (the
    // mode/difficulty code `5` branch is HYPOTHESIS and unlikely to be the
    // common case).
    bool& sprintEnabled() { return sprintEnabled_; }
    bool& sprintBit2Cleared() { return sprintBit2Cleared_; }
    // CHOSEN false - a plain bool, not OpenValue: player_can_sprint is a
    // real-hit (RT) target, and the mode `5` branch is HYPOTHESIS/unlikely
    // to be the common case, so an OPEN refusal here would needlessly block
    // a measurement run.
    bool& sprintModeIsFive() { return sprintModeIsFive_; }
    uint64_t& sprintDisableCount() { return sprintDisableCount_; }

protected: // private in EngineState before the split
    std::vector<std::string> characterOrder_;
    std::vector<std::pair<std::string, std::string>> shieldPollQueries_;
    std::vector<std::string> deathPollQueries_;

    std::unordered_map<std::string, CharacterState> characters_;
    OpenValue<uint32_t> playerControlsLockHandle_{"player_controls_disable lock handle global 0x0263ae3c",
                                                  "spec-lua-api-behaviour.md Sec7.7/Sec7.22"};
    OpenValue<bool> hasLocalPlayer_{"local player exists (0x009da4e0)", "spec-lua-api-behaviour.md Sec10.3"};
    PlayerRig playerRig_;
    CounterOnGrabbed counterOnGrabbed_;
    std::unordered_map<std::string, std::vector<std::string>> characterChildItems_;
    OpenValueMap<int32_t> characterGender_{"character +0xa41 gender byte (1=female, else male)",
                                           "spec-lua-api-behaviour.md Sec28.21/Sec28.12/Sec16.8"};
    std::unordered_map<std::string, bool> seatbeltForceFlag_;
    std::unordered_map<std::string, bool> trailingAimForceFlag_;
    std::unordered_map<std::string, bool> neverTurnOnPlayerFlag_;
    std::unordered_map<std::string, bool> warpToShoreDisabled_;
    SkydiveTankBailout skydiveTankBailout_;
    int skydiveMoveToNullTargetGuardCount_ = 0; // Sec39.1 - test-observable only, not a real engine field
    SkydiveMoveToCheckDone skydiveMoveToCheckDone_;
    SetCharInString setCharInString_;

    // --- Native-implementation batch 2026-10-07 - see each public
    // accessor's own doc comment above for citation/reasoning. ---
    OpenValueMap<bool> characterReadyBlocked_{"character_is_ready AI predicate (0x009a1dc0)",
                                              "spec-lua-api-behaviour.md Sec7.27"};
    OpenValueMap<bool> characterPlayingCombatMove_{"character_playing_combat_move predicate (0x009634f0)",
                                                   "spec-lua-api-behaviour.md Sec25.27"};

    // --- Team-A-cleared batch, spec-lua-api-behaviour.md Sec1.1/1.2/1.4/
    // 1.5/1.6/2.1/2.5/2.6/3.2/3.3/3.5/3.6 - see each public accessor's own
    // doc comment above for citation/reasoning. ---
    OpenValueMap<bool> playerClassChainResolves_{"generic/#PLAYER# chain class-bit gate (0x00a27e20)",
                                                 "spec-lua-api-behaviour.md Sec3.3/Sec3.5"};

    // --- Batch 2026-10-08 (Sec5-9 fan-out, Batch 2) - see each public
    // accessor's own doc comment above for citation/reasoning. ---
    std::unordered_map<std::string, int32_t> animationStateIds_;
    double playerSkydiveFallSpeed_ = 0.0;                 // Sec15.18, CHOSEN placeholder (OPEN default literal, no in-scope reader)
    uint64_t ragdollLastResortPositionClearCount_ = 0;
    OpenValue<std::string> localPlayerName_{"local player's own resolved name (0x009da4e0 stand-in)",
                                            "spec-lua-api-behaviour.md Sec14.21"};
    std::unordered_set<std::string> charactersOnFire_;
    SkydiveCatchingShaundi skydiveCatchingShaundi_;
    bool baseJumpingEnabled_ = true;
    uint64_t baseJumpingReplicateCount_ = 0;
    double skydiveElevAngleA_ = 0.0;
    double skydiveElevAngleB_ = 0.0;
    uint64_t isRappellingResolveFailureSafeCount_ = 0;
    uint64_t forceThrowCharNullCharacterGuardCount_ = 0;
    RappelQueue rappelQueue_;
    uint64_t rappelEnterQueuedCount_ = 0;
    uint64_t rappelEnterImmediateCount_ = 0;
    uint64_t rappelExitCount_ = 0;
    uint64_t getCharInStringNullGuardCount_ = 0;
    double sprintUseOverride_ = -1.0;
    double sprintRechargeOverride_ = -1.0;
    double sprintDelayOverride_ = -1.0;
    bool localPlayerNaked_ = false;
    bool coopPlayerNaked_ = false;
    std::unordered_map<std::string, bool> revivedFlags_;
    std::unordered_map<std::string, bool> revivedByLocalPlayerFlags_;
    std::unordered_map<std::string, int64_t> marsKbInteractableId_;
    uint64_t marsKbInteractableLeakCount_ = 0;
    std::unordered_map<std::string, W28CharacterMisc> w28CharacterMisc_;
    uint64_t parachuteOpenSpawnCount_ = 0;
    bool sprintEnabled_ = true;
    bool sprintBit2Cleared_ = false;
    bool sprintModeIsFive_ = false;
    uint64_t sprintDisableCount_ = 0;
};

} // namespace sr3luahost
