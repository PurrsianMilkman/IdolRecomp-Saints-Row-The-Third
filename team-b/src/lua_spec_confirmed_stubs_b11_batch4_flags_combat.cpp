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
    if (c.scriptedActionDispatchGatePasses.get()) { // OPEN until set
        throw OpenStateError("ai_scripted_action_is_processing's own gate-true byte-field/lookup-table comparison (0x012e1c64)",
                             "spec-lua-api-behaviour.md Sec13.7");
    }
    bool result = !c.scriptedActionLookupNonNull.get(); // OPEN until set
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
        CharacterState& c1 = st->getOrCreateCharacter(resolvedC1);
        if (c1.usingHumanShieldQualifies.get()) { // OPEN until set
            result = (c1.humanShieldHostageName == resolvedC2);
        }
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
        int32_t oldCurrent = c.currentHitPoints.get();    // OPEN until set
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
// succeed AND..."), so the slot-name table (0x008dc6b0, codes 0-0xb, OPEN
// contents) is only even consulted once the character resolves. The 5
// hardcoded case-insensitive literal slot names ARE fully given (CONFIRMED,
// branchless arithmetic) and are handled directly; any OTHER slot-name
// string cannot be distinguished from a real table entry (0-0xb) vs. a
// genuine miss (0xff) without that OPEN table, so it is refused rather
// than guessed. codes 0x10/0x11 ("best-non-lethal"/"best-non-explosive")
// reproduce the function's own CONFIRMED DEAD-CODE GAP: its dispatch
// switch never explicitly handles them, so they always fall to "none"
// regardless of inventory (a faithfully-reproduced real defect, not
// hardened away - it is a dispatch gap, not a memory-safety bug). codes
// 0xd/0xe/0xf ("best"/"best-firearm"/"best-melee") route to dedicated
// helpers this project has no real inventory data for - test-registrable
// via CharacterState::inventoryBestSlotItemForTesting, honestly "none"
// absent a registration (a real, CONFIRMED fallback, not a guess).
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
    int code;
    if (caseInsensitiveEquals(slotName, "best")) code = 0xd;
    else if (caseInsensitiveEquals(slotName, "best-firearm")) code = 0xe;
    else if (caseInsensitiveEquals(slotName, "best-melee")) code = 0xf;
    else if (caseInsensitiveEquals(slotName, "best-non-lethal")) code = 0x10;
    else if (caseInsensitiveEquals(slotName, "best-non-explosive")) code = 0x11;
    else {
        throw OpenStateError("inv_item_in_slot's own fixed slot-name enumeration table 0x008dc6b0 (codes 0-0xb)",
                             "spec-lua-api-behaviour.md Sec13.21");
    }
    if (code == 0x10 || code == 0x11) {
        lua_pushstring(L, "none"); // CONFIRMED dead-code gap, Sec13.21
        return 1;
    }
    CharacterState& c = st->getOrCreateCharacter(character);
    auto it = c.inventoryBestSlotItemForTesting.find(code);
    if (it != c.inventoryBestSlotItemForTesting.end() && !it->second.empty()) {
        lua_pushstring(L, it->second.c_str());
    } else {
        lua_pushstring(L, "none"); // CONFIRMED real fallback for no resolved item record
    }
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

// helicopter_set_max_bank_angle (Sec13.23, 0x00a4ce20). 1 mandatory string
// (vehicle) + 1 mandatory number, no nil-gate (degrees). Return none.
// CONFIRMED-by-byte-dump degrees->radians constant (~0.017453, pi/180).
// Double-gate (reuses VehicleState::forceFlagGatePasses) writing
// VehicleState::maxBankAngleRadians.
int stub_helicopter_set_max_bank_angle(lua_State* L) {
    logCall(L, upLog(L), "helicopter_set_max_bank_angle", upStateTag(L));
    std::string name = argString(L, 1);
    double angleDeg = lua_tonumber(L, 2); // mandatory, no nil-gate
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    double angleRad = angleDeg * 0.017453; // CONFIRMED constant, byte-dump-verified
    VehicleState& v = st->getOrCreateVehicle(name);
    if (v.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7/Sec30.5
        v.maxBankAngleRadians = angleRad;
    } else {
        EngineState::replicateStateChange("helicopter_set_max_bank_angle", name);
    }
    return 0;
}

// crib_disable_interface (Sec13.24, 0x00a42c60). 1 mandatory boolean, no
// nil-gate, no name/target. Return none. A pure global on/off switch
// (CONFIRMED full call chain, 2026-10-01 jobs - the "0x0101b4f0 stub" is
// inert, only the global write observable).
int stub_crib_disable_interface(lua_State* L) {
    logCall(L, upLog(L), "crib_disable_interface", upStateTag(L));
    bool disable = lua_toboolean(L, 1) != 0; // mandatory, no nil-gate
    upState(L)->cribInterfaceDisabled() = disable;
    return 0;
}

// character_ragdoll_set_last_valid_position (Sec13.25, 0x00a45f50). 1
// mandatory string (character) + 1 mandatory string (destination,
// resolved via the SAME flat objectResolves() map this project uses for
// every third-tier resolver) + 1 optional boolean, default false. Return
// none. CONFIRMED: ALWAYS opens a record (opcode 0x43, tag 0x29) with NO
// double-gate skip, but ONLY when both names resolve (the call to
// 0x00a26ec0 itself is gated on that). Maintains a per-character 10-entry
// rolling ring buffer (CharacterState::ragdollValidPositionHistory) - the
// destination's own NAME stands in for "the position fields" (no
// coordinate system in this project). false (default): append one entry;
// true: overwrite all 10 with the same value. DIFFERENT from character_
// ragdoll_clear_last_resort_position (Sec13.6) above - see this file's own
// top comment.
// Resolver 0x00a281a0 (arg 1, the character, only - arg 2's destination
// resolver 0x005982e0 is unaffected) - RE-CORRECTED (2026-10-08, direct
// disassembly of its own body): a two-stage #PLAYER#-then-#FOLLOWER#
// wrapper, not PLAYER-only as an earlier reading had it - wired here,
// follower-sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_ragdoll_set_last_valid_position(lua_State* L) {
    logCall(L, upLog(L), "character_ragdoll_set_last_valid_position", upStateTag(L));
    std::string name = argString(L, 1);
    std::string dest = argString(L, 2);
    bool resetAll = optionalBoolDefault(L, 3, false);
    EngineState* st = upState(L);
    std::string resolvedName;
    bool resolved = resolveFollowerAwareObject(st, name, resolvedName) && st->objectResolves().get(dest); // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    if (!resolved) return 0;
    EngineState::replicateStateChange("character_ragdoll_set_last_valid_position", resolvedName); // CONFIRMED always-both shape
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (resetAll) {
        c.ragdollValidPositionHistory.fill(dest); // CONFIRMED: overwrites ALL 10 slots
    } else {
        c.ragdollValidPositionHistoryIndex = (c.ragdollValidPositionHistoryIndex + 1) % 10; // CONFIRMED: 10-slot wrap
        c.ragdollValidPositionHistory[static_cast<size_t>(c.ragdollValidPositionHistoryIndex)] = dest;
    }
    return 0;
}

// character_collision_enable (Sec13.26, 0x00a41120). 1 mandatory string +
// 1 mandatory boolean, no nil-gate. Return none. CONFIRMED: this is really
// ONE fixed caller of a large REASON-CODE dispatcher (0x0094f450), not a
// simple bit flag - the individual reason codes/predicates are OPEN (not
// independently decompiled). Same "record that a call happened, not the
// real internal dispatch" convention as EngineState::callbacksClearedCount_
// (Sec28.17) - per-character count + last-requested value only.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_collision_enable(lua_State* L) {
    logCall(L, upLog(L), "character_collision_enable", upStateTag(L));
    std::string name = argString(L, 1);
    bool enable = lua_toboolean(L, 2) != 0; // mandatory, no nil-gate
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    ++c.collisionEnableCallCount;
    c.lastCollisionEnableValue = enable;
    return 0;
}

// ai_set_personality (Sec13.27, 0x00a3c5b0). 1 mandatory string
// (character) + 1 mandatory string (personality profile), no nil-gate.
// Return none. Arg 2 resolves via a hash+table-scan helper (0x00507e60)
// whose own selection semantics are OPEN - EngineState::
// lookupAiPersonalityProfileCode is the test-registration hook (same
// "never-populated object space" precedent as lookupNotorietyTrackCode),
// CONFIRMED real fallback sentinel 0x14 (20) on no match. The further
// 0x011177e5 gating nuance (Sec13.27's own OPEN item) is not separately
// modelled - this project always proceeds to the double-gate write once a
// profile code is resolved (the common, CONFIRMED top-level path). Raw-
// disassembly-confirmed hidden-`this` delegate (Sec13.29 item 1).
int stub_ai_set_personality(lua_State* L) {
    logCall(L, upLog(L), "ai_set_personality", upStateTag(L));
    std::string name = argString(L, 1);
    std::string profile = argString(L, 2); // mandatory, no nil-gate
    EngineState* st = upState(L);
    // arg1 resolved via 0x00a28150 (Sec13.27) - wired 2026-10-08 (follower-
    // sentinel-resolver pass). arg2 (profile) resolves through the SEPARATE
    // hash-table lookup 0x00507e60 (no #FOLLOWER# acceptance) - unchanged.
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    int32_t code = st->lookupAiPersonalityProfileCode(profile);
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.aiPersonalityProfileCode = code;
    } else {
        EngineState::replicateStateChange("ai_set_personality", resolvedName);
    }
    return 0;
}

// vehicle_suppress_flipping (Sec13.28, 0x00a636a0). 1 mandatory string +
// 1 optional boolean, default true. Return none. Raw-disassembly-confirmed
// hidden-`this` delegate - double-gate writing bit 0x8 of +0x1d7d, packed
// into the SAME combined forceFlags word (VehicleState::
// kSuppressFlippingBit1d7d, this project's own established +0x1d7c/
// +0x1d7d packing convention).
int stub_vehicle_suppress_flipping(lua_State* L) {
    logCall(L, upLog(L), "vehicle_suppress_flipping", upStateTag(L));
    std::string name = argString(L, 1);
    bool suppress = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    if (v.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7/Sec30.5
        v.forceFlags.setBits(VehicleState::kSuppressFlippingBit1d7d,
                              suppress ? VehicleState::kSuppressFlippingBit1d7d : 0u);
    } else {
        EngineState::replicateStateChange("vehicle_suppress_flipping", name);
    }
    return 0;
}

// =======================================================================
// Section 14 batch (spec-lua-api-behaviour.md Sec14, "ranks 151-250
// tranche, part B" - a 27-name subset; mesh_mover_set_invulnerable (14.9),
// party_dismiss_all (14.17) and zscene_is_loaded (14.23) are NOT this
// batch's own assignment, 2026-10-08).
// =======================================================================

// action_play_synced_state (Sec14.1, 0x00a3d3e0). 3 mandatory strings, no
// nil-gates. Return 1 number: -1 if either name fails to resolve, else 1.
// Reuses lookupSyncedActionIndex/kSyncedActionNotFound (the SAME, already-
// corrected 0x0095da50 mechanism action_play_synced_do, Sec33.3, already
// models - a plain name->index lookup, no further table cross-reference
// modelled). Reuses CharacterState::isDeadHighConfidence for actor 1's "is
// dead" check (the SAME field action_play_synced_do's own analogous call
// site already uses, for consistency with that established sibling). The
// per-actor local-authority gate (0x008addb0) is explicitly "not
// previously attributed to a specific subsystem" (Sec14.1's own text) -
// kept SEPARATELY SCOPED (EngineState::syncedActionAppliesLocally()),
// never reusing controlLockGatePasses. Local apply cross-writes a partner
// NAME (no coordinate system in this project, same convention as
// VehicleState::chaseTargetName).
// Resolver 0x00a281a0 (both args 1 and 2) - RE-CORRECTED (2026-10-08,
// direct disassembly of its own body): a two-stage #PLAYER#-then-
// #FOLLOWER# wrapper, not PLAYER-only as an earlier reading had it. This
// was one of the sites explicitly deferred pending that correction -
// wired here, follower-sentinel-resolver pass, via
// resolveFollowerAwareObject().
int stub_action_play_synced_state(lua_State* L) {
    logCall(L, upLog(L), "action_play_synced_state", upStateTag(L));
    std::string actor1 = argString(L, 1);
    std::string actor2 = argString(L, 2);
    std::string actionName = argString(L, 3);
    EngineState* st = upState(L);
    std::string resolvedActor1, resolvedActor2;
    bool resolved = resolveFollowerAwareObject(st, actor1, resolvedActor1) &&
                    resolveFollowerAwareObject(st, actor2, resolvedActor2); // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    if (!resolved) {
        lua_pushnumber(L, -1.0);
        return 1;
    }
    if (!st->getOrCreateCharacter(resolvedActor1).isDeadHighConfidence) {
        int32_t idx = st->lookupSyncedActionIndex(actionName);
        (void)idx; // no further synced-action table cross-reference modelled (same as action_play_synced_do)
        if (st->syncedActionAppliesLocally().get(resolvedActor1)) { // OPEN until set
            CharacterState& c1 = st->getOrCreateCharacter(resolvedActor1);
            CharacterState& c2 = st->getOrCreateCharacter(resolvedActor2);
            c1.syncedActionPartnerName = resolvedActor2;
            c1.syncedActionPaired = true;
            c2.syncedActionPartnerName = resolvedActor1;
            c2.syncedActionPaired = true;
        } else {
            EngineState::replicateStateChange("action_play_synced_state", resolvedActor1);
        }
    }
    lua_pushnumber(L, 1.0); // CONFIRMED: 1 whenever both names resolved, per the Return line
    return 1;
}

// vehicle_disable_chase (Sec14.2, 0x00a62170). 1 mandatory string + 1
// optional boolean, default true. Return none. Direct sibling of
// vehicle_chase (Sec9.4) on the SAME per-vehicle chase/behavior sub-record
// (+0xc68); the real setter receives arg 2 directly after a double
// negation (CONFIRMED). The exact sub-record bit offset is OPEN -
// VehicleState::chaseDisabled is this project's own CHOSEN project-tracked
// stand-in (same convention as kneecappersEnabled).
int stub_vehicle_disable_chase(lua_State* L) {
    logCall(L, upLog(L), "vehicle_disable_chase", upStateTag(L));
    std::string name = argString(L, 1);
    bool disable = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    st->getOrCreateVehicle(name).chaseDisabled = disable;
    return 0;
}

// set_players_can_be_downed (Sec14.3, 0x00a5d4d0). 1 mandatory string
// (gate-only resolve via the generic/"#PLAYER#" chain) + 1 mandatory
// boolean, no nil-gate. Return none. CONFIRMED literal debug tags
// "player"/"sync_flagscant_be_downed" - a NEGATED-sense polarity (the Lua
// argument means "can be downed"; the stored flag is the opposite,
// matching the established negated-boolean convention). Bit 0 of status
// byte +0x28ad (CharacterState::statusByte28ac/28ad, Sec14.31's own
// player-category census).
int stub_set_players_can_be_downed(lua_State* L) {
    logCall(L, upLog(L), "set_players_can_be_downed", upStateTag(L));
    std::string name = argString(L, 1);
    bool canBeDowned = lua_toboolean(L, 2) != 0; // mandatory, no nil-gate
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // gate-only resolve, OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.statusByte28ad.setBits(CharacterState::kCantBeDownedBit28ad,
                                  canBeDowned ? 0u : CharacterState::kCantBeDownedBit28ad); // CONFIRMED negated
    } else {
        EngineState::replicateStateChange("set_players_can_be_downed", name);
    }
    return 0;
}

// set_player_can_take_human_shields (Sec14.4, 0x00a5d400). Same gate-only
// resolve as above + 1 OPTIONAL boolean, default true (a real, confirmed
// asymmetry against set_players_can_be_downed's unconditional arg 2).
// Return none. Debug tags "player"/"sync_flagscant_take_human_shields" -
// bit 7 of status byte +0x28ac (a DIFFERENT byte from set_players_can_be_
// downed's own +0x28ad). HIGH CONFIDENCE negated polarity, by direct
// analogy with Sec14.3's own "identical shape" text (not independently
// re-confirmed in the spec's own prose for this specific entry).
int stub_set_player_can_take_human_shields(lua_State* L) {
    logCall(L, upLog(L), "set_player_can_take_human_shields", upStateTag(L));
    std::string name = argString(L, 1);
    bool canTake = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // gate-only resolve, OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.statusByte28ac.setBits(CharacterState::kCantTakeHumanShieldsBit28ac,
                                  canTake ? 0u : CharacterState::kCantTakeHumanShieldsBit28ac); // HIGH CONFIDENCE negated, by analogy
    } else {
        EngineState::replicateStateChange("set_player_can_take_human_shields", name);
    }
    return 0;
}

// player_human_shield_lock (Sec14.5, 0x00a59720). Same gate-only resolve +
// 1 mandatory boolean, unconditional (matches set_players_can_be_downed's
// shape, not its sibling's optional one). Return none. Debug tags
// "player"/"sync_flagshuman_shield_state_locked" - NO negating prefix, and
// matching that, NO negation applied (a confirmed, checked polarity
// difference from both siblings above). Bit 3 of the SAME status byte
// +0x28ad set_players_can_be_downed also uses.
int stub_player_human_shield_lock(lua_State* L) {
    logCall(L, upLog(L), "player_human_shield_lock", upStateTag(L));
    std::string name = argString(L, 1);
    bool locked = lua_toboolean(L, 2) != 0; // mandatory, no nil-gate
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // gate-only resolve, OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.statusByte28ad.setBits(CharacterState::kHumanShieldLockedBit28ad,
                                  locked ? CharacterState::kHumanShieldLockedBit28ad : 0u); // CONFIRMED: no negation
    } else {
        EngineState::replicateStateChange("player_human_shield_lock", name);
    }
    return 0;
}

// on_mover_enter_water (Sec14.6, 0x00a58200). 1 mandatory string
// (callback, opaque) + 1 mandatory string (object, resolved via the
// generic named-placed-object resolver - this project's flat
// objectResolves() stand-in). Return none. Registers into mover hook slot
// 3 (+0x20/+0x24 sub-record) - this RESOLVES the document's own
// previously-flagged OPEN "3rd unidentified kind" hook-slot array
// (PlacedObjectState::onMoverEnterWaterCallback). Empty callback clears
// (same convention as onMoverDestroyedCallback).
int stub_on_mover_enter_water(lua_State* L) {
    logCall(L, upLog(L), "on_mover_enter_water", upStateTag(L));
    std::string callback = argString(L, 1);
    std::string object = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(object)) return 0; // OPEN until set
    st->getOrCreatePlacedObject(object).onMoverEnterWaterCallback = callback;
    return 0;
}

// on_ai_do_action_complete (Sec14.7, 0x00a577c0). 1 mandatory string
// (callback, opaque) + 1 mandatory string (character reference). Return
// none. CONFIRMED, checked asymmetry against on_death: arg 2 is tested
// against the generic/"#PLAYER#" resolver FIRST, and a successful match
// there means DO NOTHING (reuses EngineState::playerClassChainResolves(),
// the SAME mechanism on_vehicle_destroyed/character_is_dead already
// establish for this exact resolver-order shape). Only the plain-character
// fallback path registers character hook slot 0x14 (20).
int stub_on_ai_do_action_complete(lua_State* L) {
    logCall(L, upLog(L), "on_ai_do_action_complete", upStateTag(L));
    std::string callback = argString(L, 1);
    std::string character = argString(L, 2);
    EngineState* st = upState(L);
    if (st->playerClassChainResolves(character)) { // CHOSEN default now applies when no test .set() it - CONFIRMED: generic/#PLAYER# match => do nothing
        return 0;
    }
    if (st->objectResolves().get(character)) { // OPEN until set - plain-character fallback resolve + liveness
        st->getOrCreateCharacter(character).onAiDoActionCompleteCallback = callback;
    }
    return 0;
}

// mission_set_excluded_reward (Sec14.8, 0x00a526f0). 1 mandatory string
// (reward id). Return none. Gated on the mission being active AND not in
// one specific excluded game-mode state (0x014c8460/0x014c7a14 != 8) -
// SEPARATELY SCOPED from EngineState::MissionContext::activeAndRunning
// (this project's own established "do not merge across call sites unless
// a spec explicitly confirms shared scope" convention, same reasoning as
// that field's own doc comment), plus a per-object-type descriptor bit.
// The reward name is hashed via sr3save::nameHash (CONFIRMED match to the
// real 0x00d9e8b0 hash, reused per se3.4/Sec13.29) and de-duplicated
// against a growing per-mission list; a duplicate is silently ignored, a
// new id is appended and replicated (further site of opcode 0x44, first
// seen Sec13.4).
int stub_mission_set_excluded_reward(lua_State* L) {
    logCall(L, upLog(L), "mission_set_excluded_reward", upStateTag(L));
    std::string reward = argString(L, 1);
    EngineState* st = upState(L);
    auto& m = st->missionExcludedRewards();
    if (m.activeAndRunning.get() && m.descriptorBit8At0xc.get()) { // OPEN until set
        uint32_t hashed = sr3save::nameHash(reward);
        bool dup = false;
        for (uint32_t h : m.excludedRewardHashes) {
            if (h == hashed) {
                dup = true;
                break;
            }
        }
        if (dup) {
            ++m.dedupSkipCount; // CONFIRMED: a duplicate is silently ignored
        } else {
            m.excludedRewardHashes.push_back(hashed);
            EngineState::replicateStateChange("mission_set_excluded_reward", reward); // opcode 0x44
        }
    }
    return 0;
}

// mesh_mover_kill (Sec14.10, 0x00a53000). 1 mandatory string (mover,
// resolved via 0x006434a0 + liveness - this project's flat
// objectResolves() stand-in) + 1 optional string, default absent
// (instigator/owner, resolved the same way when given). Return none. Two
// internal paths depending on which "extended" sub-object the mover has
// (PlacedObjectState::hasScriptedKillSubObjectC0/hasPlainKillSubObjectDc,
// test-settable, CHOSEN false - no generic sub-object graph in this
// project). The +0xc0 scripted path's vtable+0x80 internals are OPEN
// (counted only). The +0xdc path shares the THIRD distinct "should this
// replicate" gate (0x027038c0 == 0, Sec14.9/Sec14.31) with mesh_mover_set_
// invulnerable (NOT this batch's scope) - PlacedObjectState::
// replicateGate027038c0Zero models that comparison's own result directly.
// CONFIRMED, non-obvious finding: with the real call's damage amount
// always 0, this path only FINALIZES an already-at-or-below-zero health
// value into a destroy, never deals damage itself.
// Owner (arg 2) resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct
// disassembly of its own body): a two-stage #PLAYER#-then-#FOLLOWER#
// wrapper, not PLAYER-only as an earlier reading had it - wired here via
// resolveFollowerAwareObject(). The mover's own resolver (arg 1,
// 0x006434a0) is unaffected - distinct resolver, not re-corrected.
int stub_mesh_mover_kill(lua_State* L) {
    logCall(L, upLog(L), "mesh_mover_kill", upStateTag(L));
    std::string name = argString(L, 1);
    std::string owner = optionalStringDefault(L, 2, "");
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    if (!owner.empty()) {
        std::string resolvedOwner;
        (void)resolveFollowerAwareObject(st, owner, resolvedOwner); // OPEN until set; the resolved handle (or 0) is not separately observable in this project's model; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    }
    auto& mover = st->getOrCreatePlacedObject(name);
    if (mover.hasScriptedKillSubObjectC0) {
        ++mover.scriptedKillCount; // vtable+0x80 internals OPEN, count only
    } else if (mover.hasPlainKillSubObjectDc) {
        if (mover.replicateGate027038c0Zero.get()) { // OPEN until set
            EngineState::replicateStateChange("mesh_mover_kill", name);
        } else if (mover.killSubObjectHealth <= 0) {
            mover.killSubObjectHealth = 0;
            ++mover.plainKillFinalizeCount; // CONFIRMED: amount=0 only finalizes an already-<=0 value
        }
    }
    return 0;
}

// helicopter_shoot_navpoint (Sec14.11, 0x00a4fd80). 1 mandatory string
// (helicopter) + 1 mandatory string (navpoint, resolved via the flat
// objectResolves() map) + 1 optional boolean, default true. Return 1
// boolean. Fires only if the helicopter resolves AND is in the SAME AI
// drive-mode state vehicle_chase (Sec9.4) establishes
// (VehicleState::aiDriveModeIsChasing, reused directly, not re-derived -
// Sec14.11's own text explicitly cross-references this exact predicate).
// The shared helicopter-fire dispatcher's own targeting/hit-test logic (no
// coordinate system in this project) is a test-settable outcome
// (VehicleState::helicopterFireSucceeds), shared with helicopter_shoot_
// human below per Sec14.31's own explicit sharing.
int stub_helicopter_shoot_navpoint(lua_State* L) {
    logCall(L, upLog(L), "helicopter_shoot_navpoint", upStateTag(L));
    std::string heli = argString(L, 1);
    std::string navpoint = argString(L, 2);
    bool arg3 = optionalBoolDefault(L, 3, true);
    (void)arg3; // passed through to the shared dispatcher; not separately modelled (see doc comment)
    EngineState* st = upState(L);
    bool resolved = st->objectResolves().get(heli) && st->objectResolves().get(navpoint); // OPEN until set
    if (!resolved) {
        lua_pushboolean(L, 0);
        return 1;
    }
    VehicleState& v = st->getOrCreateVehicle(heli);
    ++v.helicopterShootNavpointCount;
    if (v.aiDriveModeIsChasing) { // CONFIRMED gate, reused from Sec9.4
        lua_pushboolean(L, v.helicopterFireSucceeds.get() ? 1 : 0); // OPEN until set
    } else {
        lua_pushboolean(L, 0);
    }
    return 1;
}

// customization_item_wear (Sec14.12, 0x00a43970). 3 mandatory strings
// (item/variant/color) + 1 optional boolean default true (immediate wear)
// + 1 optional number/flags default 3 + 1 optional boolean default false
// (refresh thumbnails). Return none. Bit 0x1: applies to the primary
// local-player object (reuses EngineState::hasLocalPlayer(), the SAME real
// 0x009da4e0 global). Bit 0x2: additionally applies to the co-op/network-
// roster representation, gated on playerRig().coopPlayerPresent (the SAME
// 0x009df3d0 global, reused directly - the exact precedent this project's
// own b04 batch already established for an identical bit-gated dual
// apply). Shares the SAME dual local-player customization state as
// customization_outfit_wear above (Sec14.31's own cross-batch note for the
// PCU/PCR family). Always runs a fixed UI-refresh tail (counted only -
// internals OPEN).
int stub_customization_item_wear(lua_State* L) {
    logCall(L, upLog(L), "customization_item_wear", upStateTag(L));
    std::string item = argString(L, 1);
    std::string variant = argString(L, 2);
    std::string color = argString(L, 3);
    bool immediateWear = optionalBoolDefault(L, 4, true);
    int64_t flags = static_cast<int64_t>(optionalNumberDefault(L, 5, 3.0));
    bool refreshThumbnails = optionalBoolDefault(L, 6, false);
    EngineState* st = upState(L);
    EngineState::CustomizationItemWearRequest req;
    req.item = item;
    req.variant = variant;
    req.color = color;
    req.immediateWear = immediateWear;
    req.flags = flags;
    req.refreshThumbnails = refreshThumbnails;
    if (flags & 0x1) {
        if (st->hasLocalPlayer().get()) { // OPEN until set
            req.appliedPrimary = true;
        }
    }
    if (flags & 0x2) {
        if (st->playerRig().coopPlayerPresent.get()) { // CONFIRMED false by default (applySpecInitialState item 13) - no-op in single player
            req.appliedSecondRepresentation = true;
        }
    }
    EngineState::replicateStateChange("customization_item_wear", item); // CHOSEN: top-level call, same reasoning as customization_outfit_wear above
    ++st->customizationUiRefreshTailCount();                            // unconditional 4-bit UI refresh + 2 further notify calls, count only
    st->customizationItemWearRequests().push_back(req);
    return 0;
}

// customization_item_revert (Sec14.13, 0x00a438f0). 1 optional number/
// flags, default 3 - the ONLY argument. Return none. Genuinely simpler and
// non-targeted (a real, checked asymmetry against its near-identically-
// named sibling above): bit 0x1 sets a single global "revert pending" flag
// (EngineState::customizationRevertPending()); bit 0x2 replicates (further
// opcode-0x44 site). Shares the same always-run UI-refresh tail counter.
int stub_customization_item_revert(lua_State* L) {
    logCall(L, upLog(L), "customization_item_revert", upStateTag(L));
    int64_t flags = static_cast<int64_t>(optionalNumberDefault(L, 1, 3.0));
    EngineState* st = upState(L);
    if (flags & 0x1) {
        st->customizationRevertPending() = true;
    }
    if (flags & 0x2) {
        EngineState::replicateStateChange("customization_item_revert", "");
    }
    ++st->customizationItemRevertCount();
    ++st->customizationUiRefreshTailCount();
    return 0;
}

// vehicle_set_keyframed_physics (Sec14.14, 0x00a65310). 1 mandatory string
// + 1 mandatory boolean, unconditional. Return none. Decompilation
// degraded by stack-protection instrumentation (flagged, not glossed
// over). A single hardcoded mission vehicle ("m05_heli_pierce", case-
// insensitive) gets an extra orientation-capture block first (internals
// OPEN, counted only via VehicleState::keyframedPhysicsSpecialCaseCount).
// Regardless, the boolean is always written and ALWAYS replicated
// (CONFIRMED - no double-gate skip at this call site; the record's own
// opcode argument was not visible in the decompile, OPEN, not guessed).
int stub_vehicle_set_keyframed_physics(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_keyframed_physics", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0; // mandatory, no nil-gate
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    if (caseInsensitiveEquals(name, "m05_heli_pierce")) {
        ++v.keyframedPhysicsSpecialCaseCount; // extra orientation-capture block, internals OPEN
    }
    v.keyframedPhysics = flag;
    EngineState::replicateStateChange("vehicle_set_keyframed_physics", name); // CONFIRMED: always replicated
    return 0;
}

// vehicle_hide (Sec14.15, 0x00a64250). 1 mandatory string + 1 optional
// boolean, default FALSE (a real, confirmed asymmetry against most other
// optional booleans in this batch, which default true). Return none.
// ALWAYS hides the main body (a literal "hide" parameter is fixed at this
// call site, CONFIRMED); arg 2 ADDITIONALLY hides attached sub-objects/
// occupants (no attachment array modelled - only the two observable
// booleans, VehicleState::hidden/attachmentsHidden). DIFFERENT function
// from the already-registered vehicle_show - see this file's own top
// comment.
int stub_vehicle_hide(lua_State* L) {
    logCall(L, upLog(L), "vehicle_hide", upStateTag(L));
    std::string name = argString(L, 1);
    bool hideAttachments = optionalBoolDefault(L, 2, false); // CONFIRMED default false, asymmetric vs. most of this batch
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    v.hidden = true; // CONFIRMED: always hides the main body
    if (hideAttachments) {
        v.attachmentsHidden = true;
    }
    return 0;
}

// persona_override_persona_start (Sec14.16, 0x00a5ad20). 1 mandatory
// string (character) + 1 mandatory string (persona, resolved via the
// already-independently-catalogued resolver tail 0x00462960->0x0046fd00) -
// this project extends its Sec29 "one shared name map" simplification to
// this resolver too (the tail's own real table contents are not given
// anywhere, same gap class as every other hashed/data-table lookup in this
// batch). Return none. On both resolving, sets CharacterState::
// personaOverrideName (the context getter's own "silently fails if null"
// case, Sec14.16's own OPEN item, is not separately modelled - this
// project always proceeds once both names resolve).
// Character (arg 1) resolver 0x00a281a0 - RE-CORRECTED (2026-10-08,
// direct disassembly of its own body): a two-stage #PLAYER#-then-
// #FOLLOWER# wrapper, not PLAYER-only as an earlier reading had it -
// wired here via resolveFollowerAwareObject(). Arg 2's persona resolver
// (0x00462960->0x0046fd00) is unaffected - distinct resolver.
int stub_persona_override_persona_start(lua_State* L) {
    logCall(L, upLog(L), "persona_override_persona_start", upStateTag(L));
    std::string character = argString(L, 1);
    std::string persona = argString(L, 2);
    EngineState* st = upState(L);
    std::string resolvedCharacter;
    bool resolved = resolveFollowerAwareObject(st, character, resolvedCharacter) && st->objectResolves().get(persona); // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    if (resolved) {
        st->getOrCreateCharacter(resolvedCharacter).personaOverrideName = persona;
    }
    return 0;
}

// helicopter_shoot_human (Sec14.18, 0x00a4d550). 1 mandatory string
// (helicopter) + 1 mandatory string (target character) + 1 optional
// boolean default true (random spread) + 1 optional number default 1.0
// (CONFIRMED .rdata spread-radius constant, shared with debris_flow_set_
// active's own default) + 1 optional boolean default true (immediate).
// Return 1 boolean. CONFIRMED, checked asymmetry against helicopter_shoot_
// navpoint: this native does NOT itself check the AI drive-mode gate - the
// shared dispatcher's own internal gating is what actually governs
// whether the shot lands (VehicleState::helicopterFireSucceeds, shared per
// Sec14.31). Spread/geometry not modelled (no coordinate system).
// Target (arg 2) resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct
// disassembly of its own body): a two-stage #PLAYER#-then-#FOLLOWER#
// wrapper, not PLAYER-only as an earlier reading had it - wired here via
// resolveFollowerAwareObject(). The helicopter's own resolver (arg 1,
// 0x00a281e0) is unaffected - distinct resolver.
int stub_helicopter_shoot_human(lua_State* L) {
    logCall(L, upLog(L), "helicopter_shoot_human", upStateTag(L));
    std::string heli = argString(L, 1);
    std::string target = argString(L, 2);
    bool spread = optionalBoolDefault(L, 3, true);
    double radius = optionalNumberDefault(L, 4, 1.0); // CONFIRMED .rdata constant (same default as debris_flow_set_active)
    bool immediate = optionalBoolDefault(L, 5, true);
    (void)spread;
    (void)radius;
    (void)immediate; // geometry/spread not modelled - no coordinate system in this project
    EngineState* st = upState(L);
    std::string resolvedTarget;
    bool resolved = st->objectResolves().get(heli) && resolveFollowerAwareObject(st, target, resolvedTarget); // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    if (!resolved) {
        lua_pushboolean(L, 0);
        return 1;
    }
    VehicleState& v = st->getOrCreateVehicle(heli);
    ++v.helicopterShootHumanCount;
    lua_pushboolean(L, v.helicopterFireSucceeds.get() ? 1 : 0); // OPEN until set; no drive-mode gate here, see doc comment
    return 1;
}

// drug_effect_set_override_values (Sec14.19, 0x00a474e0). 2 mandatory
// numbers, unconditional + 1 optional number/flags, default 3. Return
// none. Always targets channel 0 of a structurally multi-channel pair of
// parallel arrays (HIGH CONFIDENCE other channels exist and are reachable
// elsewhere, per Sec14.19's own text). Bit 0x2 (default on): replicate
// (THIRD confirmed site of opcode 0x40). Bit 0x1 (default on): writes both
// values directly (EngineState::drugEffectOverrideValueA/B).
int stub_drug_effect_set_override_values(lua_State* L) {
    logCall(L, upLog(L), "drug_effect_set_override_values", upStateTag(L));
    double a = lua_tonumber(L, 1); // mandatory, unconditional
    double b = lua_tonumber(L, 2); // mandatory, unconditional
    int64_t flags = static_cast<int64_t>(optionalNumberDefault(L, 3, 3.0));
    EngineState* st = upState(L);
    if (flags & 0x2) {
        EngineState::replicateStateChange("drug_effect_set_override_values", ""); // THIRD confirmed opcode-0x40 site
    }
    if (flags & 0x1) {
        st->drugEffectOverrideValueA() = a;
        st->drugEffectOverrideValueB() = b;
    }
    return 0;
}

// debris_flow_set_active (Sec14.20, 0x00a48040). 1 mandatory number/flag
// (debris-flow index) + 1 mandatory string (primary location, resolved via
// the flat objectResolves() map) + 3 mandatory numbers (direction vector) +
// 1 optional string, default absent (relative-to location) + 1 optional
// number, default 1.0 (CONFIRMED .rdata radius constant, shared with
// helicopter_shoot_human's own default) + 2 optional booleans, defaults
// true/false. Return none. This project has no coordinate system (stated
// gap) - the direction vector and the +0x64/+0x68/+0x6c field reads are
// NOT modelled, only the CONFIRMED-structure parts: whether the relative-
// to location resolved, the 3-slot fixed global table (size CONFIRMED,
// contents not, EngineState::debrisFlow()), and the host-check gate
// (reuses EngineState::coopLocalIsHost() directly, the SAME real
// singleton Sec8.27/Sec6.22/Sec10.9 already establishes).
int stub_debris_flow_set_active(lua_State* L) {
    logCall(L, upLog(L), "debris_flow_set_active", upStateTag(L));
    int64_t index = truncateEa2596(lua_tonumber(L, 1)); // mandatory, unconditional
    std::string primary = argString(L, 2);
    double dx = lua_tonumber(L, 3);
    double dy = lua_tonumber(L, 4);
    double dz = lua_tonumber(L, 5);
    (void)dx;
    (void)dy;
    (void)dz; // direction vector not modelled further - no coordinate math in this project
    std::string relativeTo = optionalStringDefault(L, 6, "");
    double radius = optionalNumberDefault(L, 7, 1.0); // CONFIRMED .rdata constant
    bool enabled = optionalBoolDefault(L, 8, true);
    bool arg9 = optionalBoolDefault(L, 9, false);
    (void)radius;
    (void)enabled;
    (void)arg9;
    EngineState* st = upState(L);
    (void)st->objectResolves().get(primary); // OPEN until set; the +0x64/+0x68/+0x6c field reads off it are OPEN, not modelled
    auto& flow = st->debrisFlow();
    if (!relativeTo.empty() && st->objectResolves().get(relativeTo)) { // OPEN until set
        ++flow.relativeLocationAppliedCount;
    }
    if (st->coopLocalIsHost()) { // reuses the SAME host-check singleton
        EngineState::replicateStateChange("debris_flow_set_active", primary); // opcode 0x45
    }
    if (index >= 0 && index < 3) {
        flow.slotActive[static_cast<size_t>(index)] = true; // the 3-slot fixed global table, size CONFIRMED
    }
    ++flow.activateCount;
    return 0;
}

// camera_restrict_set_limits (Sec14.21, 0x00a442e0). 4 mandatory numbers
// (min/max yaw, min/max pitch) + 1 optional string, default absent (target
// player) + 1 optional boolean, default false. Return none. The SAME
// degrees-to-radians constant Sec13.23 uses. A genuine 3-way dispatch keyed
// on WHO the limits are for: no arg 5 -> local write only; arg 5 resolves
// to something OTHER than the local player -> replicate only; arg 5 IS the
// local player -> both. The "fixed camera-restriction record" this
// function targets has no other identified owner in this project
// (EngineState::cameraRestrictLimits(), a singleton stand-in). The local-
// player NAME comparison (EngineState::localPlayerNameForTesting()) is a
// SEPARATELY SCOPED new stand-in - no prior field carried the local
// player's own name as a string, only presence/gender booleans.
int stub_camera_restrict_set_limits(lua_State* L) {
    logCall(L, upLog(L), "camera_restrict_set_limits", upStateTag(L));
    double minYaw = lua_tonumber(L, 1);
    double maxYaw = lua_tonumber(L, 2);
    double minPitch = lua_tonumber(L, 3);
    double maxPitch = lua_tonumber(L, 4);
    std::string target = optionalStringDefault(L, 5, "");
    bool arg6 = optionalBoolDefault(L, 6, false);
    EngineState* st = upState(L);
    const double kDegToRad = 0.017453; // CONFIRMED constant, same as Sec13.23
    bool hasTarget = !target.empty();
    bool targetIsLocalPlayer = false;
    if (hasTarget) {
        bool resolved = st->objectResolves().get(target); // OPEN until set (via 0x00a280c0)
        if (resolved && st->localPlayerNameForTesting().known() && st->localPlayerNameForTesting().get() == target) {
            targetIsLocalPlayer = true;
        }
    }
    bool doLocalWrite = !hasTarget || targetIsLocalPlayer;
    bool doReplicate = hasTarget;
    if (doLocalWrite) {
        auto& rec = st->cameraRestrictLimits();
        rec.minYaw = minYaw * kDegToRad;
        rec.maxYaw = maxYaw * kDegToRad;
        rec.minPitch = minPitch * kDegToRad;
        rec.maxPitch = maxPitch * kDegToRad;
        rec.arg6 = arg6;
        rec.written = true;
    }
    if (doReplicate) {
        EngineState::replicateStateChange("camera_restrict_set_limits", target); // opcode 0x43, tag 0xd
    }
    return 0;
}

// audio_conversation_playing (Sec14.22, 0x00a3ca00). 1 mandatory
// number/flag (slot index, -1 = "any"). Return 1 boolean, pure query. A
// GENUINELY DIFFERENT mechanism from audio_any_conversation_playing/
// EngineState::conversationChannelActive (Sec33.4's own per-session-
// channel flag, kept separate per this header's own near-duplicate
// caution) - this one walks a SLOT-INDEXED linked list (0x006de990) or
// queries one slot directly (0x00877dc0). -1: scans every test-registered
// slot (bounded, honest - an absent registration never throws here, since
// an empty real list IS a legitimate, non-OPEN state); a specific slot:
// OPEN until that slot is set.
int stub_audio_conversation_playing(lua_State* L) {
    logCall(L, upLog(L), "audio_conversation_playing", upStateTag(L));
    int64_t slot = truncateEa2596(lua_tonumber(L, 1)); // mandatory, unconditional
    EngineState* st = upState(L);
    bool result = false;
    if (slot == -1) {
        for (int i = 0; i < 64; ++i) { // a bounded scan of plausible test-registered slot ids, not a real list walk
            std::string key = std::to_string(i);
            if (st->conversationSlotPlaying().known(key) && st->conversationSlotPlaying().get(key)) {
                result = true;
                break;
            }
        }
    } else {
        result = st->conversationSlotPlaying().get(std::to_string(slot)); // OPEN until set for a specific slot
    }
    lua_pushboolean(L, result ? 1 : 0);
    return 1;
}

// vehicle_speed_cancel (Sec14.24, 0x00a635f0). 1 mandatory string. Return
// none. Direct undo-counterpart to vehicle_speed_override (Sec9.5) -
// clears the SAME fields that function writes (VehicleState::hasSpeedCap/
// speedCapRaw), no network replication (matches that function's own
// already-confirmed local-only behavior).
int stub_vehicle_speed_cancel(lua_State* L) {
    logCall(L, upLog(L), "vehicle_speed_cancel", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    v.hasSpeedCap = false; // CONFIRMED: undoes vehicle_speed_override's own writes
    v.speedCapRaw = 0.0;
    return 0;
}

// vehicle_disable_flee (Sec14.25, 0x00a622b0). Structurally identical to
// vehicle_disable_chase (Sec14.2) through a distinct address pair, on the
// SAME per-vehicle sub-record (+0xc68). VehicleState::fleeDisabled, same
// CHOSEN stand-in convention.
int stub_vehicle_disable_flee(lua_State* L) {
    logCall(L, upLog(L), "vehicle_disable_flee", upStateTag(L));
    std::string name = argString(L, 1);
    bool disable = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    st->getOrCreateVehicle(name).fleeDisabled = disable;
    return 0;
}

// set_cant_flee_flag (Sec14.26, 0x00a5ce90). 1 mandatory string (dual-mode
// resolver - this project's flat objectResolves() stand-in covers both the
// group and plain-character branches uniformly) + 1 optional boolean,
// default true. Return none. Debug tags "human_ai_data"/
// "ai_force_flagscant_flee" confirm NO negation is applied (unlike the
// "cant_be_downed"/"cant_take_human_shields" pair). Bit 0x20 of byte +0xa -
// a DIFFERENT bit of the SAME real byte CharacterState::followerCanAbandon
// already models bit 0x02 of (CharacterState::cantFleeFlag, kept
// independent per this header's own "two bits, two fields" convention).
int stub_set_cant_flee_flag(lua_State* L) {
    logCall(L, upLog(L), "set_cant_flee_flag", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    // Resolved via 0x00a28150, the "#PLAYER#"-then-"#FOLLOWER#" chain
    // (Sec14.26) - wired 2026-10-08 (follower-sentinel-resolver pass).
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.cantFleeFlag.set(flag);      // CONFIRMED: no negation
    } else {
        EngineState::replicateStateChange("set_cant_flee_flag", resolvedName);
    }
    return 0;
}

// set_behavior_set (Sec14.27, 0x00a5f170). 1 mandatory string (character,
// resolved exclusively via the plain-character path - this project's flat
// objectResolves() stand-in, same exclusive-plain-resolver shape as
// on_ai_do_action_complete above) + 1 mandatory string (behavior-set
// name). Return none. Arg 2 resolves through a dedicated, case-insensitive
// fixed-table lookup (0x004f27f0) this project has no real contents for -
// EngineState::lookupBehaviorSetCode is the test-registration hook (same
// "never-populated object space" precedent), CONFIRMED real sentinel 0x2c
// (44) on no match. The vtable+0x70 gate decides which of two apply paths
// (validated vs. raw-write) ran - both write the SAME observable field in
// this project's model (CharacterState::behaviorSetIndex), so this native
// does NOT force that field to be known before proceeding (it would be an
// invented obstacle to gate a call on a fact that provably cannot change
// its own outcome) - CharacterState::behaviorSetVtableGatePasses stays
// available for a test to .set() directly if it wants to record which
// path a given call is standing in for, but is never read here.
int stub_set_behavior_set(lua_State* L) {
    logCall(L, upLog(L), "set_behavior_set", upStateTag(L));
    std::string name = argString(L, 1);
    std::string behaviorSetName = argString(L, 2); // mandatory, no nil-gate
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    int32_t code = st->lookupBehaviorSetCode(behaviorSetName);
    CharacterState& c = st->getOrCreateCharacter(name);
    c.behaviorSetIndex = code;
    return 0;
}

// roadblocks_enable (Sec14.28, 0x00a5b6b0). 1 optional boolean, default
// true (applies both when absent and whenever the presence/type test
// itself fails - matches optionalBoolDefault's own default=true reading).
// Return none. A single global byte; false additionally fires a cleanup
// (HIGH CONFIDENCE "clear/despawn all active roadblocks", not confirmed).
int stub_roadblocks_enable(lua_State* L) {
    logCall(L, upLog(L), "roadblocks_enable", upStateTag(L));
    bool enable = optionalBoolDefault(L, 1, true);
    EngineState* st = upState(L);
    st->roadblocksEnabled() = enable;
    if (!enable) {
        ++st->roadblocksDisableCleanupCount(); // HIGH CONFIDENCE cleanup, Sec14.28
    }
    return 0;
}

// player_script_controlled_clear (Sec14.29, 0x00a59890). 1 mandatory
// string (gate-only resolve, matches the player-flags-trio idiom). Return
// none. CONFIRMED: the SAME 0x008addb0 mechanism controlLockGatePasses
// already models (Sec7.7/Sec7.22/Sec7.25), "tested for exact value 1 here
// rather than the 'not equal to zero' shape used elsewhere - an immaterial
// difference in comparison form, same underlying test" per Sec14.29's own
// cross-reference - so, unlike ai_scripted_action_is_processing/
// action_play_synced_state above, THIS call site IS attributed to the
// established field and reused directly. Clears CharacterState::
// scriptControlledMode (+0x2888) and bit 0x2 of status byte +0x28ac - the
// SAME byte set_player_can_take_human_shields targets its own bit 0x80 on,
// now shown to carry a second, independently-named flag.
int stub_player_script_controlled_clear(lua_State* L) {
    logCall(L, upLog(L), "player_script_controlled_clear", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // gate-only resolve, OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.controlLockGatePasses.get()) { // SAME 0x008addb0 mechanism as Sec7.7, reused directly per Sec14.29's own cross-ref
        c.scriptControlledMode = 0;
        c.statusByte28ac.setBits(CharacterState::kScriptControlledBit28ac, 0);
    } else {
        EngineState::replicateStateChange("player_script_controlled_clear", name);
    }
    return 0;
}

// on_vehicle_enter_water (Sec14.30, 0x00a589c0). 1 mandatory string
// (callback, opaque) + 1 mandatory string (vehicle, resolved via the flat
// objectResolves() map + liveness). Return none. Registers into VEHICLE
// hook slot 8 (+0x30/+0x34 sub-record, already established) via a THIRD
// distinct registration-helper address (0x00a56d50) sharing the SAME
// clear/convert/notify shape as the character and mover hook-slot helpers
// (Sec14.31's own census). Same empty-string-clears-slot convention as
// onVehicleDestroyedCallback.
int stub_on_vehicle_enter_water(lua_State* L) {
    logCall(L, upLog(L), "on_vehicle_enter_water", upStateTag(L));
    std::string callback = argString(L, 1);
    std::string vehicle = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(vehicle)) return 0; // OPEN until set
    st->getOrCreateVehicle(vehicle).onVehicleEnterWaterCallback = callback;
    return 0;
}

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
