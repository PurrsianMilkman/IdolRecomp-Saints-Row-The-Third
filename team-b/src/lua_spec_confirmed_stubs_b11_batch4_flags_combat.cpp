// lua_spec_confirmed_stubs_b11_batch4_flags_combat.cpp
//
// Wave 1, Batch 4 of the Sec10-28 lua-natives fan-out (2026-10-08) - see
// results/lua_natives_sec10_28_fanout_scoping_20261008.md (Sec0/3/4/5) for
// the full scoping rationale. This is a NEW file (per the refactor's own
// reviewer guidance: do not retrofit into b01-b07), following the exact
// same pattern as the existing 7 (own anonymous-namespace stub_* functions,
// own namesB11()/registerB11(wants) pair declared in
// lua_spec_confirmed_stubs_sections.h, called from
// lua_spec_confirmed_stubs_registry.cpp).
//
// Scope: spec-lua-api-behaviour.md Sec13 (28 names, "ranks 151-250 tranche,
// part C") + a 27-name subset of Sec14 (30 names, "ranks 151-250 tranche,
// part B" - excluding mesh_mover_set_invulnerable/party_dismiss_all/
// zscene_is_loaded, each either already registered elsewhere or out of
// this batch's own assignment). 55 names total.
//
// Near-duplicate warnings (from the scoping report, re-verified at
// implementation time): vehicle_hide (Sec14.15) is a DIFFERENT function
// from the already-registered vehicle_show - no matched-pair internals
// assumed. mesh_mover_kill (Sec14.10) is DIFFERENT from the already-
// registered mesh_mover_set_invulnerable/_hide/_show/_reset - verified
// independently (own sub-object fields, own gate). character_ragdoll_
// clear_last_resort_position (Sec13.6) and character_ragdoll_set_last_
// valid_position (Sec13.25) are each DIFFERENT from the already-registered
// character_ragdoll_set_last_resort_position AND from each other - three
// distinct fields on two distinct structs (see each stub's own doc comment
// and engine_state.h's own field-level citations).
//
// HARDENING POLICY: none of this batch's 55 names are on the scoping
// report's own flagged memory-safety list, and no genuine unbounded-copy/
// OOB-read-write/arbitrary-address-write was found while implementing any
// of them - every "OPEN" item below is a missing-data gap (an unidentified
// helper body, an un-traced table), not a memory-safety defect, so no
// CHOSEN (hardened divergence) marker appears in this file.
//
// Every new engine-state field this batch needed is declared and cited in
// include/sr3luahost/engine_state.h (CharacterState/VehicleState/
// PlacedObjectState/EngineState, each field's own doc comment there gives
// the exact spec subsection and the CONFIRMED/HIGH CONFIDENCE/CHOSEN/OPEN
// tier) - this file's own per-stub comments cross-reference those fields
// by name rather than re-deriving the citation.
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

#include "sr3save/save_crc.h"

#include <cmath>

namespace sr3luahost {
namespace {

// =======================================================================
// Section 13 batch (spec-lua-api-behaviour.md Sec13, "ranks 151-250
// tranche, part C", 28 names, 2026-10-08).
// =======================================================================

// object_indicator_add_objective_text (Sec13.1, 0x00a576a0). 3 mandatory
// strings, no nil-gates. Return none. Resolves arg 1 via the shared
// objectResolves() map (this project's stand-in for 0x00734e90 + the
// liveness gate, Sec29's "one shared name map" convention). The real HUD
// dispatch chain (0x008f4ef0/0x008f76e0) is NOT modelled - only that an
// objective-text indicator was requested is recorded (same convention as
// the already-modelled MinimapIconRecord/ObjectIndicatorRecord).
int stub_object_indicator_add_objective_text(lua_State* L) {
    logCall(L, upLog(L), "object_indicator_add_objective_text", upStateTag(L));
    std::string target = argString(L, 1);
    std::string iconKey = argString(L, 2);
    std::string textKey = argString(L, 3);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(target)) return 0; // OPEN until set
    st->getOrCreateCharacter(target).objectiveTextIndicators.push_back({iconKey, textKey});
    return 0;
}

// notoriety_spawn_group_set_delay (Sec13.2, 0x00a558f0). 1 mandatory
// string (group name) + 1 mandatory number, rounded (delay). Return none.
// Resolves via the 24-entry group table (EngineState::
// notorietySpawnGroupResolves(), a disjoint table from
// lookupNotorietyTrackCode's own TRACK table, Sec3.6). The generic expiry-
// timestamp setter 0x00d9e140 needs a real "now" counter this project does
// not model (no frame-time global anywhere in this header) - the raw
// rounded delay is recorded honestly rather than a fabricated wrapped sum.
int stub_notoriety_spawn_group_set_delay(lua_State* L) {
    logCall(L, upLog(L), "notoriety_spawn_group_set_delay", upStateTag(L));
    std::string name = argString(L, 1);
    int64_t delay = truncateEa2596(lua_tonumber(L, 2)); // mandatory, rounded, no nil-gate
    EngineState* st = upState(L);
    if (st->notorietySpawnGroupResolves().get(name)) { // OPEN until set
        auto& g = st->getOrCreateNotorietySpawnGroup(name);
        ++g.clearFlagCount; // +0x3c1 clear, unconditional on success (CONFIRMED)
        g.lastDelayRequested = delay;
        ++g.delayRequestCount;
    }
    return 0;
}

// hud_prompt (Sec13.3, 0x00a4cff0). 3 mandatory strings, no nil-gates.
// Return 1 boolean, always pushed. Resolves arg 1 via the generic/
// "#PLAYER#" chain (this project's flat objectResolves() stand-in). On
// success: records the request, returns true. On failure: resets the
// global default/HUD-prompt state (0x00a36e60(0), independently
// decompiled, 3 real callers project-wide - counted, not modelled
// further), returns false.
int stub_hud_prompt(lua_State* L) {
    logCall(L, upLog(L), "hud_prompt", upStateTag(L));
    std::string target = argString(L, 1);
    std::string a2 = argString(L, 2);
    std::string a3 = argString(L, 3);
    EngineState* st = upState(L);
    bool resolved = st->objectResolves().get(target); // OPEN until set
    if (resolved) {
        st->recordHudPrompt(target, a2, a3);
        lua_pushboolean(L, 1);
    } else {
        st->recordHudPromptDefaultReset();
        lua_pushboolean(L, 0);
    }
    return 1;
}

// customization_outfit_wear (Sec13.4, 0x00a43b10). 1 mandatory string
// (outfit key) + 1 optional number default 0 (variant) + 1 optional
// number/flags default 3. Return none. Arg 1 is hashed via sr3save::
// nameHash (CONFIRMED match to the real 0x00d9e8b0 CRC-32, see engine_
// state.h's own top comment) - a data-table key, NOT a live-object
// resolve (Sec13.29 item 2). Bit 0x1: the real delegate (0x00619110) has
// its own recursive "already applying" double-gate this project does not
// model a nested-context flag for - CHOSEN: a top-level Lua call is
// treated as NOT already inside a replicated-apply context, so bit 0x1
// always records + replicates at this level. Bit 0x2: gated on the SAME
// 0x009df3d0 "co-op player present" global customization_item_wear below
// and EngineState::playerRig().coopPlayerPresent already model (Sec13.29
// item 8 / Sec14.31's "dual local-player representation" finding) -
// reused directly, not re-derived.
int stub_customization_outfit_wear(lua_State* L) {
    logCall(L, upLog(L), "customization_outfit_wear", upStateTag(L));
    std::string outfitKey = argString(L, 1);
    double variant = optionalNumberDefault(L, 2, 0.0);
    int64_t flags = static_cast<int64_t>(optionalNumberDefault(L, 3, 3.0));
    EngineState* st = upState(L);
    EngineState::CustomizationOutfitWearRequest req;
    req.hashedName = sr3save::nameHash(outfitKey);
    req.variant = variant;
    req.flags = flags;
    if (flags & 0x1) {
        EngineState::replicateStateChange("customization_outfit_wear", outfitKey); // CHOSEN: top-level call, see doc comment above
    }
    if (flags & 0x2) {
        if (st->playerRig().coopPlayerPresent.get()) { // CONFIRMED false by default (applySpecInitialState item 13) - no-op in single player
            req.secondRepresentationApplied = true;
        }
    }
    st->customizationOutfitWearRequests().push_back(req);
    return 0;
}

// character_set_no_explosive_panic (Sec13.5, 0x00a42510). 1 mandatory
// string + 1 optional boolean, default true. Return none. Raw-
// disassembly-confirmed hidden-`this` delegate (Sec13.29 item 1) - double-
// gate (variant 1) writing bit 0x4 of dword +0x1c9c (CharacterState::
// forceFlags1c9c).
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_set_no_explosive_panic(lua_State* L) {
    logCall(L, upLog(L), "character_set_no_explosive_panic", upStateTag(L));
    std::string name = argString(L, 1);
    bool noPanic = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.forceFlags1c9c.setBits(CharacterState::kNoExplosivePanicBit1c9c,
                                  noPanic ? CharacterState::kNoExplosivePanicBit1c9c : 0u);
    } else {
        EngineState::replicateStateChange("character_set_no_explosive_panic", resolvedName);
    }
    return 0;
}

// character_ragdoll_clear_last_resort_position (Sec13.6, 0x00a41ff0). NO
// arguments consumed at all (CONFIRMED - the "inert argument" shape).
// Return none. Unconditionally calls 0x009a2240(3): bit 0x2 opens a
// record (opcode 0x41, tag 0x23) - always replicated; bit 0x1 clears ONE
// GLOBAL cache (0x0262d0d8) - EngineState::ragdollLastResortPositionClearCount
// tracks that this happened. DIFFERENT from the already-registered
// character_ragdoll_set_last_resort_position and from character_ragdoll_
// set_last_valid_position (Sec13.25) below - see this file's own top
// comment for the full near-duplicate warning.
int stub_character_ragdoll_clear_last_resort_position(lua_State* L) {
    logCall(L, upLog(L), "character_ragdoll_clear_last_resort_position", upStateTag(L));
    EngineState* st = upState(L);
    EngineState::replicateStateChange("character_ragdoll_clear_last_resort_position", ""); // bit 0x2: opcode 0x41 tag 0x23, CONFIRMED always
    ++st->ragdollLastResortPositionClearCount(); // bit 0x1: clears global 0x0262d0d8, CONFIRMED always
    return 0;
}

// ai_scripted_action_is_processing (Sec13.7, 0x00a3c490). 1 mandatory
// string. Return 1 boolean: false if unresolved, else the result of
// 0x004f39e0. That function gates on 0x008addb0 - explicitly "a check this
// document has not previously attributed to a specific subsystem" per
// Sec13.7's own text, so this project keeps a SEPARATELY SCOPED gate
// (CharacterState::scriptedActionDispatchGatePasses) rather than reusing
// controlLockGatePasses (the Sec7.7-confirmed instance of the identical
// literal address). Gate false: reuses the SAME +0x4aa registry lookup
// ai_scripted_action_is_complete (Sec7.28, CharacterState::
// scriptedActionLookupNonNull) already models - "returns whether the
// lookup returns 0" per Sec13.7's own text, i.e. the logical negation of
// "lookup succeeded". Gate true: the byte-field/0x46-entry-lookup-table
// (0x012e1c64) comparison is genuinely OPEN (not independently traced,
// and explicitly flagged by the orchestrator's own Batch 7 scoping as
// needing a dedicated CAE-table owner) - refused honestly rather than
// guessed.
int stub_ai_scripted_action_is_processing(lua_State* L) {
    logCall(L, upLog(L), "ai_scripted_action_is_processing", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    // Resolved via 0x00a28150 ("the SAME generic/#PLAYER#-then-#FOLLOWER#
    // chain Sec7.1 already establishes", Sec13.7) - wired 2026-10-08
    // (follower-sentinel-resolver pass).
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        lua_pushboolean(L, 0);
        return 1;
    }
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    // Sec13.7 "Gate polarity, 2026-10-12, FINAL": 0x008addb0 is "this machine has authority over the object" -
    // TRUE in single player. Gate true -> the action-tag match against 0x012e1c64; a host without scripted-
    // action tags returns false ("no scripted action processing"), never an error. An explicit set(false)
    // (tests) still selects the queue-lookup branch.
    if (c.scriptedActionDispatchGatePasses.known() ? c.scriptedActionDispatchGatePasses.get() : true) {
        lua_pushboolean(L, 0);
        return 1;
    }
    bool result = !c.scriptedLookupNonNull(); // ce4556f Sec7.28: no pending ticket -> non-null
    lua_pushboolean(L, result ? 1 : 0);
    return 1;
}

// vehicle_ignore_repulsors (Sec13.8, 0x00a62660). 1 mandatory string + 1
// mandatory boolean, no nil-gate. Return none. Raw-disassembly-confirmed
// hidden-`this` delegate (0x00b22bf0) - double-gate (variant 1) writing
// bit 0x2 of the vehicle-AI sub-record's own +4 byte (VehicleState::
// vehicleAiForceFlagsByte4 - a DIFFERENT byte from vehicleAiForceFlags'
// own +5 bit 0x1, Sec33.1's vehicle_set_no_chase).
int stub_vehicle_ignore_repulsors(lua_State* L) {
    logCall(L, upLog(L), "vehicle_ignore_repulsors", upStateTag(L));
    std::string name = argString(L, 1);
    bool ignore = lua_toboolean(L, 2) != 0; // mandatory, no nil-gate
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    if (v.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7/Sec30.5
        v.vehicleAiForceFlagsByte4.setBits(VehicleState::kIgnoreRepulsorsBit,
                                            ignore ? VehicleState::kIgnoreRepulsorsBit : 0u);
    } else {
        EngineState::replicateStateChange("vehicle_ignore_repulsors", name);
    }
    return 0;
}

// spawn_region_toggle_filter (Sec13.9, 0x00a5fcf0). 1 mandatory string
// (spawn-region name) + 1 mandatory number, rounded (filter mask). Return
// none. Resolves via the already-established spawn-region resolver
// (Sec1.8, this project's flat objectResolves() stand-in), liveness-gated.
// On success: XORs the byte at the region's own +0x95 field - a genuine
// bitwise TOGGLE (CONFIRMED, no record-and-replicate call anywhere in this
// function). EngineState::spawnRegionFilterBits, CHOSEN 0 at first
// reference (same convention as notorietyTrackFlags_, Sec3.6).
int stub_spawn_region_toggle_filter(lua_State* L) {
    logCall(L, upLog(L), "spawn_region_toggle_filter", upStateTag(L));
    std::string name = argString(L, 1);
    int64_t mask = truncateEa2596(lua_tonumber(L, 2)); // mandatory, rounded, no nil-gate
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    uint8_t& bits = st->spawnRegionFilterBits(name);
    bits ^= static_cast<uint8_t>(mask & 0xff); // genuine bitwise TOGGLE, CONFIRMED
    return 0;
}

// refraction_situation_set (Sec13.10, 0x00a5bb70). 1 mandatory string
// (situation key) + 1 optional number/flags, default 3. Return none. Arg 1
// is hashed via sr3save::nameHash (same CRC-32 as customization_outfit_
// wear above - a data-table key lookup, Sec13.29 item 2). Bit 0x1: local
// apply (the real destination slot index, Sec13.10's own OPEN item, is
// not modelled - only that an apply happened). Bit 0x2: replicate (opcode
// 0x43, tag 0x2b). Default 3 runs both (same "local apply + network
// record" shape as waypoint_remove, Sec6.17).
int stub_refraction_situation_set(lua_State* L) {
    logCall(L, upLog(L), "refraction_situation_set", upStateTag(L));
    std::string key = argString(L, 1);
    int64_t flags = static_cast<int64_t>(optionalNumberDefault(L, 2, 3.0));
    EngineState* st = upState(L);
    EngineState::RefractionSituationRequest req;
    req.hashedName = sr3save::nameHash(key);
    req.flags = flags;
    if (flags & 0x1) {
        req.appliedLocally = true;
    }
    if (flags & 0x2) {
        EngineState::replicateStateChange("refraction_situation_set", key);
    }
    st->refractionSituationRequests().push_back(req);
    return 0;
}

// npc_revive (Sec13.11, 0x00a55680). 1 mandatory string. Return none.
// Resolves via objectResolves(). Checks the ALREADY-ESTABLISHED "downed"
// predicate (state 6, CharacterState::lifeState, Sec7.13) first: downed ->
// a revive-from-downed routine (0x009a7c90). Otherwise checks the
// ALREADY-ESTABLISHED "is dead" predicate (state 5, Sec3.5): dead -> a
// DIFFERENT revive-from-dead routine (0x009a78d0). Separately-scoped
// counters (CharacterState::npcReviveFromDownedCount/npcReviveFromDeadCount)
// from player_revive's own reviveCount (Sec32.3, a different real call
// chain) - the real post-revive life-state value is not stated by this
// section either, so lifeState is left untouched, same honest-gap
// precedent as reviveCount's own doc comment.
int stub_npc_revive(lua_State* L) {
    logCall(L, upLog(L), "npc_revive", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    // Resolved via 0x00a28150 ("established, Sec7.1", Sec13.11) - wired
    // 2026-10-08 (follower-sentinel-resolver pass).
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    int32_t life = c.lifeState.get(); // seeded 0 at construction, Sec34.5
    if (life == 6) {
        ++c.npcReviveFromDownedCount;
    } else if (life == 5) {
        ++c.npcReviveFromDeadCount;
    }
    return 0;
}

// hud_timer_hide (Sec13.12, 0x00a4e7f0). 1 mandatory number, rounded
// (slot id 0-3) + 1 mandatory boolean, no nil-gate. Return none. Passes
// through to the real delegate managing a fixed pool of (at most) 4 HUD-
// timer slots (EngineState::hudTimerSlots()): hiding recomputes the
// combined "anything still visible" summary by scanning all 4 slots; un-
// hiding forces the summary to false (CONFIRMED). The layout-mode
// escalation (2 -> 4, meaning OPEN) fires only when the companion byte is
// also set for this slot.
int stub_hud_timer_hide(lua_State* L) {
    logCall(L, upLog(L), "hud_timer_hide", upStateTag(L));
    int64_t slotRaw = truncateEa2596(lua_tonumber(L, 1)); // mandatory, rounded, no nil-gate
    bool hideFlag = lua_toboolean(L, 2) != 0;              // mandatory, no nil-gate
    EngineState* st = upState(L);
    auto& slots = st->hudTimerSlots();
    if (slotRaw >= 0 && slotRaw < 4) {
        size_t slot = static_cast<size_t>(slotRaw);
        slots.hidden[slot] = hideFlag;
        if (hideFlag) {
            bool anyVisible = false;
            for (int i = 0; i < 4; ++i) {
                if (!slots.hidden[i]) anyVisible = true;
            }
            slots.anyVisibleSummary = anyVisible; // CONFIRMED: recompute by scanning all 4 when hiding
        } else {
            slots.anyVisibleSummary = false; // CONFIRMED: forced to 0 when un-hiding
        }
        if (slots.layoutCompanion[slot] && slots.layoutMode == 2) {
            slots.layoutMode = 4; // CONFIRMED escalation structure; meaning of 2/4 OPEN
        }
    }
    return 0;
}

// hud_critical_timer_set (Sec13.13, 0x00a4f5f0). 1 mandatory number,
// rounded (timer id) + 1 mandatory string (label) + 1 optional string,
// default "" (secondary key) + 1 optional number/flags, default 3. Return
// none. CONFIRMED: the inner configure call (0x006d0d40) only actually
// runs ONCE PER WHOLE RUN (gated on a single GLOBAL, 0x014c835c == 0, not
// per-timer) - reproduced directly via EngineState::hudCriticalTimer()'s
// own "configured" flag. The outer function unconditionally logs (tag
// 0xc) and, if a secondary key is given and non-empty, interns it via the
// real ~350-slot name->id registry mechanism (0x00a1fcc0, modelled exactly
// via sequential-id assignment, EngineState::internHudSecondaryKey) and
// always calls the shared bookkeeping/notify function (0x00a29920)
// regardless of whether a secondary key was given (CONFIRMED - the
// resolved-or-not boolean is itself one of that call's own arguments).
int stub_hud_critical_timer_set(lua_State* L) {
    logCall(L, upLog(L), "hud_critical_timer_set", upStateTag(L));
    int64_t timerId = truncateEa2596(lua_tonumber(L, 1)); // mandatory, rounded, no nil-gate
    std::string label = argString(L, 2);                  // mandatory
    std::string secondary = optionalStringDefault(L, 3, "");
    int64_t flags = static_cast<int64_t>(optionalNumberDefault(L, 4, 3.0));
    (void)flags; // consumed by the inner 0x006d0d40 configure step only; this project's one-time-configure model does not need the bit-6d06a0 dispatch beyond "configured happened"
    EngineState* st = upState(L);
    auto& timer = st->hudCriticalTimer();
    if (!timer.configured) { // CONFIRMED: global gate 0x014c835c == 0, runs once per whole run
        timer.configured = true;
        timer.timerId = timerId;
        timer.labelText = label;
    }
    ++st->hudCriticalTimerLogCount(); // unconditional 0x005e4520 tag 0xc
    if (!secondary.empty()) {
        timer.secondaryKey = secondary;
        timer.secondaryKeyResolved = true;
        st->internHudSecondaryKey(secondary);
    }
    ++st->hudCriticalTimerNotifyCount(); // unconditional 0x00a29920 call, CONFIRMED
    return 0;
}

// character_has_specific_human_shield (Sec13.14, 0x00a41490). 2 mandatory
// strings. Return 1 boolean, pure query. Resolves both via objectResolves().
// The combined "currently using a human shield" predicate (0x009aa260 AND
// NOT 0x009aa660) is OPEN (CharacterState::usingHumanShieldQualifies,
// test-settable); when it qualifies, compares against CharacterState::
// humanShieldHostageName (Sec45.1, already modelled - "the taker's own
// held-hostage name", the SAME real concept, reused directly).
// Resolver 0x00a281a0 (both args) - RE-CORRECTED (2026-10-08, direct
// disassembly of its own body): a two-stage #PLAYER#-then-#FOLLOWER#
// wrapper, not PLAYER-only as an earlier reading had it - wired here,
// follower-sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_has_specific_human_shield(lua_State* L) {
    logCall(L, upLog(L), "character_has_specific_human_shield", upStateTag(L));
    std::string c1name = argString(L, 1);
    std::string c2name = argString(L, 2);
    EngineState* st = upState(L);
    std::string resolvedC1, resolvedC2;
    bool r1 = resolveFollowerAwareObject(st, c1name, resolvedC1); // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    bool r2 = resolveFollowerAwareObject(st, c2name, resolvedC2); // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    bool result = false;
    if (r1 && r2) {
        // Gate 0x009aa260/0x009aa660 answered (spec sync 98375ea / bfe0542, blocker dlc3_m01 + m12, FINAL):
        // true iff char 1 currently HOLDS a human shield (state 1 or 2, mutual live link, char 1 not the hostage)
        // and the hostage is char 2. False in ordinary play until the player actually grabs that NPC.
        result = st->characterHasSpecificHumanShield(resolvedC1, resolvedC2);
        if (!result) st->noteShieldPollQuery(resolvedC1, resolvedC2); // --auto-objective harness bookkeeping only
    }
    lua_pushboolean(L, result ? 1 : 0);
    return 1;
}

// camera_shake_play (Sec13.15, 0x00a449f0). 1 mandatory string (profile,
// not resolved) + 1 optional number, default 1.0 (CONFIRMED .rdata
// constant). Return none. Applies locally and ALWAYS replicates (opcode
// 0x43, tag 15) - no double-gate skip (unlike most of this batch's simple
// flag setters).
int stub_camera_shake_play(lua_State* L) {
    logCall(L, upLog(L), "camera_shake_play", upStateTag(L));
    std::string profile = argString(L, 1);
    double magnitude = optionalNumberDefault(L, 2, 1.0);
    EngineState* st = upState(L);
    auto& shake = st->cameraShake();
    shake.lastProfile = profile;
    shake.lastMagnitude = magnitude;
    ++shake.playCount;
    EngineState::replicateStateChange("camera_shake_play", profile); // CONFIRMED: always replicated
    return 0;
}

// camera_end_look_through (Sec13.16, 0x00a44140). 1 optional boolean,
// default true. Return none. Opens a record (opcode 0x43, tag 14) always,
// then ALWAYS runs a fixed 3-call teardown sequence (internals OPEN,
// counted only), plus a 4th call gated specifically on the (default-true)
// boolean.
int stub_camera_end_look_through(lua_State* L) {
    logCall(L, upLog(L), "camera_end_look_through", upStateTag(L));
    bool smooth = optionalBoolDefault(L, 1, true);
    EngineState* st = upState(L);
    EngineState::replicateStateChange("camera_end_look_through", ""); // CONFIRMED: always, opcode 0x43 tag 14
    ++st->cameraEndLookThroughCount();                                // fixed 3-call teardown, internals OPEN
    if (smooth) {
        ++st->cameraEndLookThroughSmoothCount(); // the boolean-gated 4th call
    }
    return 0;
}

// action_nodes_enable (Sec13.17, 0x00a3c020). 1 mandatory boolean, no
// nil-gate, no name/target argument. Return none. A pure global on/off
// switch (same simplicity class as set_ped_density/set_traffic_density).
int stub_action_nodes_enable(lua_State* L) {
    logCall(L, upLog(L), "action_nodes_enable", upStateTag(L));
    bool enable = lua_toboolean(L, 1) != 0; // mandatory, no nil-gate
    upState(L)->actionNodesEnabled() = enable;
    return 0;
}

// set_team (Sec13.18, 0x00a5d780). 1 mandatory string (character) + 1
// mandatory string (team), no nil-gate. Return none. Arg 2 resolves
// through the IDENTICAL gang/faction code enumeration the notoriety-track
// system already uses (0x0094cc60, EngineState::lookupNotorietyTrackCode,
// failure sentinel 9, Sec3.6 - a genuinely new, real finding per Sec13.18's
// own text, reused directly rather than re-derived). Raw-disassembly-
// confirmed hidden-`this` delegate - double-gate (variant 1) writing
// CharacterState::currentTeamCode.
// Resolver 0x00a281a0 (arg 1 only - arg 2's team-code resolver 0x0094cc60
// is unaffected) - RE-CORRECTED (2026-10-08, direct disassembly of its
// own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_set_team(lua_State* L) {
    logCall(L, upLog(L), "set_team", upStateTag(L));
    std::string name = argString(L, 1);
    std::string team = argString(L, 2); // mandatory, no nil-gate
    EngineState* st = upState(L);
    std::string resolvedName;
    bool resolved = resolveFollowerAwareObject(st, name, resolvedName); // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    int32_t code = st->lookupNotorietyTrackCode(team);
    if (resolved && code != EngineState::kNotorietyTrackCodeNotFound) {
        CharacterState& c = st->getOrCreateCharacter(resolvedName);
        if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
            c.currentTeamCode = code;
            c.teamName = team; // authored team name, passed as on_human_killed's first argument
        } else {
            EngineState::replicateStateChange("set_team", resolvedName);
        }
    }
    return 0;
}

// set_min_hit_points (Sec13.19, 0x00a5f540). 1 mandatory string (vehicle)
// + 1 mandatory number, rounded, no nil-gate. Return none. Double-gate
// (reuses VehicleState::forceFlagGatePasses, the SAME CONFIRMED-true-in-
// single-player gate 0x00a80710 shares with every other vehicle double-
// gate setter in this project): writes VehicleState::minHitPointsV and, a
// CONFIRMED cross-field consistency guarantee, raises maxHitPointsV to
// match if the new minimum exceeds it.
int stub_set_min_hit_points(lua_State* L) {
    logCall(L, upLog(L), "set_min_hit_points", upStateTag(L));
    std::string name = argString(L, 1);
    int32_t value = static_cast<int32_t>(truncateEa2596(lua_tonumber(L, 2))); // mandatory, rounded, no nil-gate
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    if (v.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7/Sec30.5
        v.minHitPointsV = value;
        if (value > v.maxHitPointsV) v.maxHitPointsV = value; // CONFIRMED cross-field guarantee
    } else {
        EngineState::replicateStateChange("set_min_hit_points", name);
    }
    return 0;
}

// set_max_hit_points (Sec13.20, 0x00a5f340) - the richest function in this
// batch. 1 mandatory string (resolved through the full chain - this
// project disambiguates character vs. vehicle via hasVehicle()&&!
// hasCharacter(), the SAME CHOSEN precedent lua_spec_confirmed_stubs_b05's
// applyInvulnerabilityFlags already establishes for an identical dual-
// typed-resolve case) + 1 mandatory number, rounded, no nil-gate + 1
// optional boolean, default false ("preserve health percentage" vs. "reset
// current HP to new max"). Return none. CONFIRMED (Sec13.20/Sec13.29#7):
// for a character, +0x1cac = +0x1cb0 (the max-HP BONUS field) + arg - this
// project NEVER writes +0x1cb0/CharacterState's own bonus concept anywhere
// (it stays its real, CONFIRMED-zero spawn value), so the real formula
// collapses to a plain overwrite here, not a separately-tracked delta.
// Reuses EngineState::drawOrGetMaxHitPoints() for the OLD max (same
// Sec60.4 closure get_max_hit_points/set_current_hit_points already use) so
// this native's own ratio-preservation math sees the same resolved value
// regardless of call order. For a vehicle: mirrors the same ratio logic on
// VehicleState::maxHitPointsV/currentHitPointsV/minHitPointsV; the sub-
// component propagation array (+0x68c) is NOT modelled (no attachment
// graph anywhere in this project).
int stub_set_max_hit_points(lua_State* L) {
    logCall(L, upLog(L), "set_max_hit_points", upStateTag(L));
    std::string name = argString(L, 1);
    int32_t newValue = static_cast<int32_t>(truncateEa2596(lua_tonumber(L, 2))); // mandatory, rounded, no nil-gate
    bool resetCurrent = optionalBoolDefault(L, 3, false);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    bool treatAsVehicle = st->hasVehicle(name) && !st->hasCharacter(name); // CHOSEN disambiguation, see doc comment
    if (treatAsVehicle) {
        VehicleState& v = st->getOrCreateVehicle(name);
        int32_t oldMax = v.maxHitPointsV;
        int32_t oldCurrent = v.currentHitPointsV;
        v.maxHitPointsV = newValue;
        if (resetCurrent) {
            v.currentHitPointsV = newValue;
        } else {
            double ratio = (oldMax != 0) ? (static_cast<double>(oldCurrent) / static_cast<double>(oldMax)) : 0.0;
            v.currentHitPointsV = static_cast<int32_t>(std::lround(ratio * newValue));
        }
    } else {
        CharacterState& c = st->getOrCreateCharacter(name);
        int32_t oldMax = st->drawOrGetMaxHitPoints(name); // closes OPEN for an ordinary character, Sec60.4
        int32_t oldCurrent = st->drawOrGetCurrentHitPoints(name); // Sec34.2: starts at the table/max value
        double ratio = (oldMax != 0) ? (static_cast<double>(oldCurrent) / static_cast<double>(oldMax)) : 0.0;
        c.maxHitPoints.set(newValue); // +0x1cac = +0x1cb0 (always 0 in this project) + arg -> plain overwrite, see doc comment
        if (resetCurrent) {
            c.currentHitPoints.set(newValue);
        } else {
            c.currentHitPoints.set(static_cast<int32_t>(std::lround(ratio * newValue)));
        }
    }
    return 0;
}

// inv_item_in_slot (Sec13.21, 0x00a50930). 2 mandatory strings (character,
// slot name). Return 1 string, always. Resolves character via
// objectResolves(); an unresolved character ALWAYS answers "none"
// regardless of the slot name (CONFIRMED structure: "if both resolves
// succeed AND..."), so the slot-name table (0x008dc6b0) is only even
// consulted once the character resolves.
//
// CLOSED 2026-10-09 (spec-lua-api-behaviour.md Sec13.21, cross-confirmed by
// spec-tables-weapons-combat.md Sec2.5/+0x10 and spec-tables-progression.md
// Sec6.3/item 21, both independently citing the SAME 0x008dc6b0 table): the
// table is a CLOSED, fully-recovered function - a case-insensitive linear
// scan over 11 fixed literal strings, returning the matching index (0-0xa)
// or the fixed sentinel 0xff on no match ("255 = unknown", per
// spec-tables-progression.md's own text) - not an open guess. Full table:
// 0=unarmed, 1=melee, 2=pistol, 3=smg, 4=shotgun, 5=rifle, 6=explosive,
// 7=special, 8=vehicle, 9=grenade, 0xa=single_use. On a table miss (0xff),
// the function falls back to the 5 hardcoded case-insensitive "best-*"
// literal compares (CONFIRMED, branchless arithmetic, unchanged from
// before). Because BOTH stages are now fully closed, every possible
// slot-name string is deterministically classified here - there is no
// longer a genuine "can't tell a real miss from an unmodeled name" gap, so
// an unmatched string (0xff from both stages) answers "none" directly
// (CONFIRMED: Sec13.21's own text - "otherwise, INCLUDING any unresolved
// slot code, pushes 'none'") rather than being refused.
//
// Of the 11 table codes, codes 0-8 are LIVE: dispatch (`code < 0xb`) to
// 0x005fec20's own default arm (an array-of-8-byte-entries lookup this
// project has no real inventory data for) - test-registrable via
// CharacterState::inventoryBestSlotItemForTesting, honestly "none" absent
// a registration (same precedent as the pre-existing "best-*" codes
// below). Codes 9 (grenade) and 0xa (single_use) are CONFIRMED DEAD, for
// two structurally different reasons (Sec13.21, 2026-10-09): grenade (9)
// is hard-coded inside 0x005fec20's OWN switch to the same arm as the
// invalid sentinel 0xff, so it can never resolve a held item regardless of
// inventory; single_use (0xa) has real, working handling inside
// 0x005fec20 (a separate +0x60/+0x64 field pair), but this native's own
// caller-side dispatch switch (0x00a50930) never forwards code 0xa to it
// at all (only `<0xb` as a block would reach it, but the table's own
// top-level dispatch here routes 0-8 there and stops short of 0xa - see
// the dispatch logic below) - so it is unreachable via this native for a
// different reason than grenade's. Both faithfully reproduce "none",
// never real inventory simulation.
//
// The 5 hardcoded "best-*" literal names (0xd-0x11, unchanged from before
// this pass): "best"/"best-firearm"/"best-melee" (0xd/0xe/0xf) route to
// dedicated helpers this project has no real inventory data for -
// test-registrable via the SAME inventoryBestSlotItemForTesting map,
// honestly "none" absent a registration. codes 0x10/0x11 ("best-non-
// lethal"/"best-non-explosive") reproduce the function's own CONFIRMED
// DEAD-CODE GAP: its dispatch switch never explicitly handles them, so
// they always fall to "none" regardless of inventory (a faithfully-
// reproduced real defect, not hardened away - a dispatch gap, not a
// memory-safety bug).
//
// Resolver 0x00a281a0 (character arg only - the slot-name resolver
// 0x00a25f80 is unaffected) - RE-CORRECTED (2026-10-08, direct
// disassembly of its own body): a two-stage #PLAYER#-then-#FOLLOWER#
// wrapper, not PLAYER-only as an earlier reading had it - wired here,
// follower-sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_inv_item_in_slot(lua_State* L) {
    logCall(L, upLog(L), "inv_item_in_slot", upStateTag(L));
    std::string character = argString(L, 1);
    std::string slotName = argString(L, 2);
    EngineState* st = upState(L);
    std::string resolvedCharacter;
    bool charResolves = resolveFollowerAwareObject(st, character, resolvedCharacter); // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    if (!charResolves) {
        lua_pushstring(L, "none"); // CONFIRMED: unresolved character always answers "none"
        return 1;
    }
    character = resolvedCharacter;

    // Stage 1: the now-fully-recovered 11-name table (0x008dc6b0), codes 0-0xa.
    static constexpr std::pair<const char*, int> kSlotNameTable[] = {
        {"unarmed", 0x0}, {"melee", 0x1}, {"pistol", 0x2}, {"smg", 0x3},
        {"shotgun", 0x4}, {"rifle", 0x5}, {"explosive", 0x6}, {"special", 0x7},
        {"vehicle", 0x8}, {"grenade", 0x9}, {"single_use", 0xa},
    };
    int code = 0xff;
    for (const auto& entry : kSlotNameTable) {
        if (caseInsensitiveEquals(slotName, entry.first)) { code = entry.second; break; }
    }
    // Stage 2 (only on a stage-1 miss): the 5 hardcoded "best-*" literals (CONFIRMED, branchless arithmetic).
    if (code == 0xff) {
        if (caseInsensitiveEquals(slotName, "best")) code = 0xd;
        else if (caseInsensitiveEquals(slotName, "best-firearm")) code = 0xe;
        else if (caseInsensitiveEquals(slotName, "best-melee")) code = 0xf;
        else if (caseInsensitiveEquals(slotName, "best-non-lethal")) code = 0x10;
        else if (caseInsensitiveEquals(slotName, "best-non-explosive")) code = 0x11;
    }

    // Dead codes (CONFIRMED, Sec13.21): grenade(9)/single_use(0xa) inside
    // the 0-0xa table, best-non-lethal(0x10)/best-non-explosive(0x11)
    // outside it - all always "none" regardless of inventory.
    bool dead = (code == 0x9 || code == 0xa || code == 0x10 || code == 0x11);
    // Live codes: 0-8 (table) and 0xd/0xe/0xf (best-*) - test-registrable,
    // honestly "none" absent a registration. Anything else (0xff genuine
    // miss, or any code this native's own dispatch never reaches) is also
    // "none" - CONFIRMED fallback, not a guess.
    bool live = (code >= 0x0 && code <= 0x8) || code == 0xd || code == 0xe || code == 0xf;

    if (live && !dead) {
        CharacterState& c = st->getOrCreateCharacter(character);
        auto it = c.inventoryBestSlotItemForTesting.find(code);
        if (it != c.inventoryBestSlotItemForTesting.end() && !it->second.empty()) {
            lua_pushstring(L, it->second.c_str());
            return 1;
        }
    }
    lua_pushstring(L, "none"); // CONFIRMED real fallback: dead code, unresolved item record, or genuine miss
    return 1;
}

// hud_achievement_show_progress (Sec13.22, 0x00a4ea90). 5 args: string
// (achievement name), 2 mandatory numbers (current/total progress), 1
// optional number/flags default 3, 1 optional boolean default true.
// Return none. Arg 1 is a data-table key (this project tracks only
// test-registered NAMES, EngineState::knownAchievementsForTesting - the
// small fixed roster's own real contents are not given anywhere). On a
// match, arg 5 (default true) gates: bit 0x2 of arg 4 (default on) ->
// replicate; bit 0x1 of arg 4 (default on) -> toast counter. Separately,
// regardless of arg 5, current==total fires 1 or 2 unidentified completion
// handlers (Sec13.22's own OPEN item re: which bits of arg 4 select which) -
// one shared counter, not individually bit-gated (avoids inventing a bit
// assignment the spec does not give).
int stub_hud_achievement_show_progress(lua_State* L) {
    logCall(L, upLog(L), "hud_achievement_show_progress", upStateTag(L));
    std::string name = argString(L, 1);
    double current = lua_tonumber(L, 2); // mandatory, no nil-gate
    double total = lua_tonumber(L, 3);   // mandatory, no nil-gate
    int64_t flags = static_cast<int64_t>(optionalNumberDefault(L, 4, 3.0));
    bool doToastAndReplicate = optionalBoolDefault(L, 5, true);
    EngineState* st = upState(L);
    bool matched = st->knownAchievementsForTesting().count(name) != 0;
    if (matched) {
        if (doToastAndReplicate) {
            if (flags & 0x2) {
                EngineState::replicateStateChange("hud_achievement_show_progress", name);
                ++st->achievementReplicateCount();
            }
            if (flags & 0x1) {
                ++st->achievementToastCount();
            }
        }
        if (current == total) {
            ++st->achievementCompletionCount();
        }
    }
    return 0;
}

// Defined in lua_spec_confirmed_stubs_b11_batch4_flags_combat_part2.cpp (split out of this file, no behaviour change).
} // namespace
namespace stubs_b11 {
int stub_helicopter_set_max_bank_angle(lua_State* L);
int stub_crib_disable_interface(lua_State* L);
int stub_character_ragdoll_set_last_valid_position(lua_State* L);
int stub_character_collision_enable(lua_State* L);
int stub_ai_set_personality(lua_State* L);
int stub_vehicle_suppress_flipping(lua_State* L);
int stub_action_play_synced_state(lua_State* L);
int stub_vehicle_disable_chase(lua_State* L);
int stub_set_players_can_be_downed(lua_State* L);
int stub_set_player_can_take_human_shields(lua_State* L);
int stub_player_human_shield_lock(lua_State* L);
int stub_on_mover_enter_water(lua_State* L);
int stub_on_ai_do_action_complete(lua_State* L);
int stub_mission_set_excluded_reward(lua_State* L);
int stub_mesh_mover_kill(lua_State* L);
int stub_helicopter_shoot_navpoint(lua_State* L);
int stub_customization_item_wear(lua_State* L);
int stub_customization_item_revert(lua_State* L);
int stub_vehicle_set_keyframed_physics(lua_State* L);
int stub_vehicle_hide(lua_State* L);
int stub_persona_override_persona_start(lua_State* L);
int stub_helicopter_shoot_human(lua_State* L);
int stub_drug_effect_set_override_values(lua_State* L);
int stub_debris_flow_set_active(lua_State* L);
int stub_camera_restrict_set_limits(lua_State* L);
int stub_audio_conversation_playing(lua_State* L);
int stub_vehicle_speed_cancel(lua_State* L);
int stub_vehicle_disable_flee(lua_State* L);
int stub_set_cant_flee_flag(lua_State* L);
int stub_set_behavior_set(lua_State* L);
int stub_roadblocks_enable(lua_State* L);
int stub_player_script_controlled_clear(lua_State* L);
int stub_on_vehicle_enter_water(lua_State* L);
} // namespace stubs_b11
namespace {
using stubs_b11::stub_helicopter_set_max_bank_angle;
using stubs_b11::stub_crib_disable_interface;
using stubs_b11::stub_character_ragdoll_set_last_valid_position;
using stubs_b11::stub_character_collision_enable;
using stubs_b11::stub_ai_set_personality;
using stubs_b11::stub_vehicle_suppress_flipping;
using stubs_b11::stub_action_play_synced_state;
using stubs_b11::stub_vehicle_disable_chase;
using stubs_b11::stub_set_players_can_be_downed;
using stubs_b11::stub_set_player_can_take_human_shields;
using stubs_b11::stub_player_human_shield_lock;
using stubs_b11::stub_on_mover_enter_water;
using stubs_b11::stub_on_ai_do_action_complete;
using stubs_b11::stub_mission_set_excluded_reward;
using stubs_b11::stub_mesh_mover_kill;
using stubs_b11::stub_helicopter_shoot_navpoint;
using stubs_b11::stub_customization_item_wear;
using stubs_b11::stub_customization_item_revert;
using stubs_b11::stub_vehicle_set_keyframed_physics;
using stubs_b11::stub_vehicle_hide;
using stubs_b11::stub_persona_override_persona_start;
using stubs_b11::stub_helicopter_shoot_human;
using stubs_b11::stub_drug_effect_set_override_values;
using stubs_b11::stub_debris_flow_set_active;
using stubs_b11::stub_camera_restrict_set_limits;
using stubs_b11::stub_audio_conversation_playing;
using stubs_b11::stub_vehicle_speed_cancel;
using stubs_b11::stub_vehicle_disable_flee;
using stubs_b11::stub_set_cant_flee_flag;
using stubs_b11::stub_set_behavior_set;
using stubs_b11::stub_roadblocks_enable;
using stubs_b11::stub_player_script_controlled_clear;
using stubs_b11::stub_on_vehicle_enter_water;

} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB11() {
    static const std::vector<std::string> names = {
        // Sec13 (28 names, in spec order)
        "object_indicator_add_objective_text",
        "notoriety_spawn_group_set_delay",
        "hud_prompt",
        "customization_outfit_wear",
        "character_set_no_explosive_panic",
        "character_ragdoll_clear_last_resort_position",
        "ai_scripted_action_is_processing",
        "vehicle_ignore_repulsors",
        "spawn_region_toggle_filter",
        "refraction_situation_set",
        "npc_revive",
        "hud_timer_hide",
        "hud_critical_timer_set",
        "character_has_specific_human_shield",
        "camera_shake_play",
        "camera_end_look_through",
        "action_nodes_enable",
        "set_team",
        "set_min_hit_points",
        "set_max_hit_points",
        "inv_item_in_slot",
        "hud_achievement_show_progress",
        "helicopter_set_max_bank_angle",
        "crib_disable_interface",
        "character_ragdoll_set_last_valid_position",
        "character_collision_enable",
        "ai_set_personality",
        "vehicle_suppress_flipping",
        // Sec14 subset (27 names, in spec order; 14.9/14.17/14.23 excluded)
        "action_play_synced_state",
        "vehicle_disable_chase",
        "set_players_can_be_downed",
        "set_player_can_take_human_shields",
        "player_human_shield_lock",
        "on_mover_enter_water",
        "on_ai_do_action_complete",
        "mission_set_excluded_reward",
        "mesh_mover_kill",
        "helicopter_shoot_navpoint",
        "customization_item_wear",
        "customization_item_revert",
        "vehicle_set_keyframed_physics",
        "vehicle_hide",
        "persona_override_persona_start",
        "helicopter_shoot_human",
        "drug_effect_set_override_values",
        "debris_flow_set_active",
        "camera_restrict_set_limits",
        "audio_conversation_playing",
        "vehicle_speed_cancel",
        "vehicle_disable_flee",
        "set_cant_flee_flag",
        "set_behavior_set",
        "roadblocks_enable",
        "player_script_controlled_clear",
        "on_vehicle_enter_water",
    };
    return names;
}

void registerB11(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag, const WantsPredicate& wants) {
    // Sec13
    if (wants("object_indicator_add_objective_text"))
        registerOne(L, state, log, stateTag, "object_indicator_add_objective_text",
                    openGuard<stub_object_indicator_add_objective_text>);
    if (wants("notoriety_spawn_group_set_delay"))
        registerOne(L, state, log, stateTag, "notoriety_spawn_group_set_delay",
                    openGuard<stub_notoriety_spawn_group_set_delay>);
    if (wants("hud_prompt")) registerOne(L, state, log, stateTag, "hud_prompt", openGuard<stub_hud_prompt>);
    if (wants("customization_outfit_wear"))
        registerOne(L, state, log, stateTag, "customization_outfit_wear", openGuard<stub_customization_outfit_wear>);
    if (wants("character_set_no_explosive_panic"))
        registerOne(L, state, log, stateTag, "character_set_no_explosive_panic",
                    openGuard<stub_character_set_no_explosive_panic>);
    if (wants("character_ragdoll_clear_last_resort_position"))
        registerOne(L, state, log, stateTag, "character_ragdoll_clear_last_resort_position",
                    openGuard<stub_character_ragdoll_clear_last_resort_position>);
    if (wants("ai_scripted_action_is_processing"))
        registerOne(L, state, log, stateTag, "ai_scripted_action_is_processing",
                    openGuard<stub_ai_scripted_action_is_processing>);
    if (wants("vehicle_ignore_repulsors"))
        registerOne(L, state, log, stateTag, "vehicle_ignore_repulsors", openGuard<stub_vehicle_ignore_repulsors>);
    if (wants("spawn_region_toggle_filter"))
        registerOne(L, state, log, stateTag, "spawn_region_toggle_filter", openGuard<stub_spawn_region_toggle_filter>);
    if (wants("refraction_situation_set"))
        registerOne(L, state, log, stateTag, "refraction_situation_set", openGuard<stub_refraction_situation_set>);
    if (wants("npc_revive")) registerOne(L, state, log, stateTag, "npc_revive", openGuard<stub_npc_revive>);
    if (wants("hud_timer_hide")) registerOne(L, state, log, stateTag, "hud_timer_hide", openGuard<stub_hud_timer_hide>);
    if (wants("hud_critical_timer_set"))
        registerOne(L, state, log, stateTag, "hud_critical_timer_set", openGuard<stub_hud_critical_timer_set>);
    if (wants("character_has_specific_human_shield"))
        registerOne(L, state, log, stateTag, "character_has_specific_human_shield",
                    openGuard<stub_character_has_specific_human_shield>);
    if (wants("camera_shake_play"))
        registerOne(L, state, log, stateTag, "camera_shake_play", openGuard<stub_camera_shake_play>);
    if (wants("camera_end_look_through"))
        registerOne(L, state, log, stateTag, "camera_end_look_through", openGuard<stub_camera_end_look_through>);
    if (wants("action_nodes_enable"))
        registerOne(L, state, log, stateTag, "action_nodes_enable", openGuard<stub_action_nodes_enable>);
    if (wants("set_team")) registerOne(L, state, log, stateTag, "set_team", openGuard<stub_set_team>);
    if (wants("set_min_hit_points"))
        registerOne(L, state, log, stateTag, "set_min_hit_points", openGuard<stub_set_min_hit_points>);
    if (wants("set_max_hit_points"))
        registerOne(L, state, log, stateTag, "set_max_hit_points", openGuard<stub_set_max_hit_points>);
    if (wants("inv_item_in_slot")) registerOne(L, state, log, stateTag, "inv_item_in_slot", openGuard<stub_inv_item_in_slot>);
    if (wants("hud_achievement_show_progress"))
        registerOne(L, state, log, stateTag, "hud_achievement_show_progress", openGuard<stub_hud_achievement_show_progress>);
    if (wants("helicopter_set_max_bank_angle"))
        registerOne(L, state, log, stateTag, "helicopter_set_max_bank_angle", openGuard<stub_helicopter_set_max_bank_angle>);
    if (wants("crib_disable_interface"))
        registerOne(L, state, log, stateTag, "crib_disable_interface", openGuard<stub_crib_disable_interface>);
    if (wants("character_ragdoll_set_last_valid_position"))
        registerOne(L, state, log, stateTag, "character_ragdoll_set_last_valid_position",
                    openGuard<stub_character_ragdoll_set_last_valid_position>);
    if (wants("character_collision_enable"))
        registerOne(L, state, log, stateTag, "character_collision_enable", openGuard<stub_character_collision_enable>);
    if (wants("ai_set_personality"))
        registerOne(L, state, log, stateTag, "ai_set_personality", openGuard<stub_ai_set_personality>);
    if (wants("vehicle_suppress_flipping"))
        registerOne(L, state, log, stateTag, "vehicle_suppress_flipping", openGuard<stub_vehicle_suppress_flipping>);

    // Sec14 subset
    if (wants("action_play_synced_state"))
        registerOne(L, state, log, stateTag, "action_play_synced_state", openGuard<stub_action_play_synced_state>);
    if (wants("vehicle_disable_chase"))
        registerOne(L, state, log, stateTag, "vehicle_disable_chase", openGuard<stub_vehicle_disable_chase>);
    if (wants("set_players_can_be_downed"))
        registerOne(L, state, log, stateTag, "set_players_can_be_downed", openGuard<stub_set_players_can_be_downed>);
    if (wants("set_player_can_take_human_shields"))
        registerOne(L, state, log, stateTag, "set_player_can_take_human_shields",
                    openGuard<stub_set_player_can_take_human_shields>);
    if (wants("player_human_shield_lock"))
        registerOne(L, state, log, stateTag, "player_human_shield_lock", openGuard<stub_player_human_shield_lock>);
    if (wants("on_mover_enter_water"))
        registerOne(L, state, log, stateTag, "on_mover_enter_water", openGuard<stub_on_mover_enter_water>);
    if (wants("on_ai_do_action_complete"))
        registerOne(L, state, log, stateTag, "on_ai_do_action_complete", openGuard<stub_on_ai_do_action_complete>);
    if (wants("mission_set_excluded_reward"))
        registerOne(L, state, log, stateTag, "mission_set_excluded_reward", openGuard<stub_mission_set_excluded_reward>);
    if (wants("mesh_mover_kill")) registerOne(L, state, log, stateTag, "mesh_mover_kill", openGuard<stub_mesh_mover_kill>);
    if (wants("helicopter_shoot_navpoint"))
        registerOne(L, state, log, stateTag, "helicopter_shoot_navpoint", openGuard<stub_helicopter_shoot_navpoint>);
    if (wants("customization_item_wear"))
        registerOne(L, state, log, stateTag, "customization_item_wear", openGuard<stub_customization_item_wear>);
    if (wants("customization_item_revert"))
        registerOne(L, state, log, stateTag, "customization_item_revert", openGuard<stub_customization_item_revert>);
    if (wants("vehicle_set_keyframed_physics"))
        registerOne(L, state, log, stateTag, "vehicle_set_keyframed_physics", openGuard<stub_vehicle_set_keyframed_physics>);
    if (wants("vehicle_hide")) registerOne(L, state, log, stateTag, "vehicle_hide", openGuard<stub_vehicle_hide>);
    if (wants("persona_override_persona_start"))
        registerOne(L, state, log, stateTag, "persona_override_persona_start",
                    openGuard<stub_persona_override_persona_start>);
    if (wants("helicopter_shoot_human"))
        registerOne(L, state, log, stateTag, "helicopter_shoot_human", openGuard<stub_helicopter_shoot_human>);
    if (wants("drug_effect_set_override_values"))
        registerOne(L, state, log, stateTag, "drug_effect_set_override_values",
                    openGuard<stub_drug_effect_set_override_values>);
    if (wants("debris_flow_set_active"))
        registerOne(L, state, log, stateTag, "debris_flow_set_active", openGuard<stub_debris_flow_set_active>);
    if (wants("camera_restrict_set_limits"))
        registerOne(L, state, log, stateTag, "camera_restrict_set_limits", openGuard<stub_camera_restrict_set_limits>);
    if (wants("audio_conversation_playing"))
        registerOne(L, state, log, stateTag, "audio_conversation_playing", openGuard<stub_audio_conversation_playing>);
    if (wants("vehicle_speed_cancel"))
        registerOne(L, state, log, stateTag, "vehicle_speed_cancel", openGuard<stub_vehicle_speed_cancel>);
    if (wants("vehicle_disable_flee"))
        registerOne(L, state, log, stateTag, "vehicle_disable_flee", openGuard<stub_vehicle_disable_flee>);
    if (wants("set_cant_flee_flag"))
        registerOne(L, state, log, stateTag, "set_cant_flee_flag", openGuard<stub_set_cant_flee_flag>);
    if (wants("set_behavior_set"))
        registerOne(L, state, log, stateTag, "set_behavior_set", openGuard<stub_set_behavior_set>);
    if (wants("roadblocks_enable"))
        registerOne(L, state, log, stateTag, "roadblocks_enable", openGuard<stub_roadblocks_enable>);
    if (wants("player_script_controlled_clear"))
        registerOne(L, state, log, stateTag, "player_script_controlled_clear",
                    openGuard<stub_player_script_controlled_clear>);
    if (wants("on_vehicle_enter_water"))
        registerOne(L, state, log, stateTag, "on_vehicle_enter_water", openGuard<stub_on_vehicle_enter_water>);
}

} // namespace stub_sections
} // namespace sr3luahost
