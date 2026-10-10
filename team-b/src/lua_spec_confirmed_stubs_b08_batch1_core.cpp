// lua_spec_confirmed_stubs_b08_batch1_core.cpp
//
// Wave 1, batch 1 (2026-10-08): spec-lua-api-behaviour.md §1-4
// (trigger/mission/teleport/combat-hook core), per the orchestrator's own
// fan-out scoping (results/lua_natives_sec10_28_fanout_scoping_20261008.md,
// "Batch 1 - Trigger/mission/teleport/combat-hook core (§1-4) - 21 natives,
// HIGHEST PRIORITY"). New file per the refactor's own reviewer guidance -
// not retrofitted into b01-b07. See lua_spec_confirmed_stubs_registry.cpp's
// own top comment for the shared-registration mechanism.
//
// CORRECTION TO THE SCOPING REPORT'S OWN COUNT (independently verified by
// reading all 21 of this batch's own spec-lua-api-behaviour.md sections
// directly, not assumed from the scoping report's "cleared" count): 6 of
// the 21 assigned names each still carried their OWN original 2026-09-30
// "Review status: ... NOT yet cleared for implementation" line, verbatim,
// with NO later dated resolution anywhere in the document - the exact same
// disqualifying signal the scoping report itself already used to correctly
// exclude `camera_restrict_set_active` (§8.12). Per this project's own
// no-invented-fixes/skip-and-revisit policy, these 6 are deferred, not
// implemented here, and are listed below with their own citation so a
// future pass does not have to re-derive this finding:
//
//   - `on_dismiss` (§1.11): body carries an inline "[Resolved by §3: ...]"
//     cross-reference closing its one open item, but the entry's OWN
//     Review-status line (2026-09-30, "DESK-PASS... NOT yet cleared") was
//     never bumped, and - unlike its cleared §1 siblings - no dated
//     "re-derived from the executable, direct disassembly of 0x00a57dd0"
//     statement exists anywhere for this entry specifically.
//   - `character_prevent_flinching` (§3.14), `character_allow_ragdoll`
//     (§3.16), `npc_combat_enable` (§3.17): each entry's own Review-status
//     line is still the original "DESK-PASS"/"NEEDS-EXE... NOT yet cleared
//     for implementation" (2026-09-30), with no later dated update, despite
//     §3's OWN cluster-wide summary line admitting several §3 entries
//     "stay uncleared until [re-]derivation... is read" - §3.11/3.12/3.13
//     visibly got that later pass (explicit "RESOLVED (2026-10-07)" lines);
//     these three did not.
//   - `inv_weapon_add_temporary` (§4.10), `inv_weapon_remove_temporary`
//     (§4.11): same pattern - still "DESK-PASS... NOT yet cleared for
//     implementation" (2026-09-30) with no later resolution anywhere in
//     the document (grepped directly for both names end to end).
//
// UPDATE 2026-10-08 (later the same day): all 6 were independently
// re-derived from scratch and each one's own Review-status line was dated
// "cleared for implementation" (grepped directly, verbatim, in the current
// spec-lua-api-behaviour.md). All 6 are now implemented - see
// lua_spec_confirmed_stubs_b28_batch1_deferred6.cpp, a new section file
// (not retrofitted here), per this project's own "never retrofit a new
// name into an existing b0N file" fan-out convention.
//
// This leaves 15 of the 21 actually implemented below, in original batch
// order: objective_text (§1.3), on_trigger_exit (§1.7), spawn_region_enable
// (§1.8), door_open (§1.10), audio_conversation_play (§2.10), game_peg_unload
// (§2.7), pop_screen (§2.8), object_is_in_trigger (§1.12), ai_do_scripted_move
// (§3.11), on_attack_performed (§3.12), teleport (§4.3), release_to_world
// (§4.4), vehicle_suppress_npc_exit (§4.5), set_player_can_enter_exit_vehicles
// (§4.6), continuous_spawn_stop (§4.7).
//
// HARDENING POLICY reviewed (orchestrator's standing directive): none of
// these 15 natives have a spec-confirmed memory-unsafe bug in the original
// binary (none are on the scoping report's own flagged memory-safety list,
// and none of the 21 sections read for this batch state an OOB/arbitrary-
// write finding). `objective_text`'s own slot index is kept in a
// `std::unordered_map` rather than a fixed 4-element array specifically so
// no slot value can ever write out of bounds - but this is a safe-by-
// construction property of the data structure chosen, not a documented
// "hardened divergence" from a confirmed bug (Sec1.3's own text never
// states whether the real `< 4` bound check is signed or unsigned, so no
// claim is made that the original binary actually goes out of bounds for a
// negative slot). No `CHOSEN (hardened divergence)` label or counter
// appears in this file for that reason.
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

namespace sr3luahost {
namespace {

// objective_text (§1.3, 0x00a56920) - 424 calls / 41 scripts. CONFIRMED: 6
// arguments - arg 1 mandatory number (slot index, truncateEa2596, read
// unconditionally), arg 2 mandatory string (text key), arg 3/4 optional
// strings (default the shared empty-string constant), arg 5/6 optional
// numbers (defaults 3/0, same truncateEa2596 pair). Return: none. This
// project has no real localization table (0x00849df0/0x026e7e68/
// 0x00604740/0x00604500 all OPEN per spec) - see ObjectiveTextSlot's own
// doc comment (engine_state.h) for the full "record the request, not the
// resolved internals" reasoning, and for why a slot outside [0,4) is
// simply rejected (matching the one real CONFIRMED bound check, "must be
// < 4") rather than ever reaching the map.
int stub_objective_text(lua_State* L) {
    logCall(L, upLog(L), "objective_text", upStateTag(L));
    int64_t slot = truncateEa2596(lua_tonumber(L, 1)); // CONFIRMED: unconditional read, no nil-gate
    std::string textKey = argString(L, 2);
    std::string arg3 = optionalStringDefault(L, 3, "");
    bool hasArg4 = lua_gettop(L) >= 4 && lua_type(L, 4) != LUA_TNIL;
    std::string arg4 = hasArg4 ? argString(L, 4) : std::string();
    int64_t flag5 = truncateEa2596(optionalNumberDefault(L, 5, 3.0));
    int64_t flag6 = truncateEa2596(optionalNumberDefault(L, 6, 0.0));
    if (slot < 0 || slot >= 4) return 0; // CONFIRMED bound check ("must be < 4") - matches the real failure outcome
    EngineState* st = upState(L);
    ObjectiveTextSlot& s = st->objectiveTextSlots()[slot];
    s.rawTextKey = textKey;
    // CONFIRMED (0x007ee110): "%s/%s" when arg 4 is present AND non-empty
    // (byte-content compare, not just non-null), else arg 3 verbatim.
    s.combinedAuxText = (hasArg4 && !arg4.empty()) ? (arg3 + "/" + arg4) : arg3;
    s.flag5 = flag5;
    s.flag6 = flag6;
    ++s.writeCount;
    return 0;
}

// on_trigger_exit (§1.7, 0x00a58720) - 104 calls / 23 scripts. CONFIRMED:
// 2 mandatory strings - arg 1 an opaque callback name, arg 2 a trigger
// name resolved via the shared trigger/object resolver (this project's
// flat objectResolves() stand-in). Structurally identical to on_trigger
// (§1.2, already registered in b01/b07 - not re-implemented here) but
// registers into a SEPARATE exit-mode slot (+0xfc, not +0xf8) via mode
// value 1 (not 0). Return: none.
int stub_on_trigger_exit(lua_State* L) {
    logCall(L, upLog(L), "on_trigger_exit", upStateTag(L));
    std::string callbackName = argString(L, 1);
    std::string triggerName = argString(L, 2);
    EngineState* st = upState(L);
    if (st->objectResolves().get(triggerName)) { // OPEN until set
        st->registerOnTriggerExitCallback(triggerName, callbackName);
    }
    return 0;
}

// spawn_region_enable (§1.8, 0x00a5fc00) - 104 calls / 14 scripts.
// CONFIRMED: 2 mandatory arguments, BOTH read unconditionally with no
// nil-gate - arg 1 (string) a spawn-region name, arg 2 (boolean) an
// enable flag. Resolved via a resolver distinct from the trigger/object
// one (0x006ed4d0) - this project's flat objectResolves() stand-in (Sec29
// "one shared name map" convention). Sets/clears bit 0x1 of a per-region
// byte at handle +0x94, leaving the other 7 bits untouched (this project
// has no other in-scope writer of that byte, so a plain bool suffices in
// place of a bit-accurate OpenBits32). Return: none.
int stub_spawn_region_enable(lua_State* L) {
    logCall(L, upLog(L), "spawn_region_enable", upStateTag(L));
    std::string name = argString(L, 1);
    bool enable = lua_toboolean(L, 2) != 0; // CONFIRMED: both args read unconditionally, no nil-gate
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->spawnRegionEnabled()[name] = enable;
    }
    return 0;
}

// door_open (§1.10, 0x00a488c0) - 101 calls / 21 scripts. CONFIRMED: 6
// arguments - arg 1 mandatory string (door name, resolved via the shared
// named-placed-object registry door_close/door_lock/mesh_mover_* already
// use, getOrCreatePlacedObject()). Arg 2 (bool, default false), arg 3
// (bool, default true), arg 4/5/6 (strings, default absent), each
// individually nil-gated. Return: 1 boolean.
//
// Body: if the door fails to resolve, pushes false (this project's
// established convention project-wide: a resolved-but-dead/absent object
// is a defined "false" outcome, distinct from a name NEVER configured at
// all, which still throws OPEN via objectResolves().get() itself).
//
// If arg 4 is absent: the simple open call (0x00a31920) runs with just
// args 2/3 and its OWN result becomes the pushed return value - the
// spec's own closing sentence ("Success is signalled by pushing 1; any
// failed branch pushes 0") is read here as the function's OVERALL return
// convention (HIGH CONFIDENCE, not independently traced past
// 0x00a31920's own entry): once the door resolves, no further stated
// failure condition applies to this path, so it pushes true.
//
// If arg 4 IS present: resolved via the SAME flat objectResolves() stand-
// in (the real engine uses a second, distinct by-name resolver,
// 0x00a281a0, for this argument - collapsed into the one shared map per
// this project's standing Sec29 convention). On success, gated on the
// door's own +0xc8 interaction-anchor sub-object being present
// (PlacedObjectState::hasInteractionAnchor, OPEN until set): if present,
// records the rich-path request (resolved arg4 name, raw arg5 anim-state
// name, raw arg6 target name if it independently resolves) and pushes
// true; otherwise pushes false (CONFIRMED failed-branch convention).
//
// Resolver 0x00a281a0 (arg 4 only - arg 1's door resolver is the
// DIFFERENT 0x006434a0, arg 6's is the DIFFERENT 0x005982e0, neither
// re-corrected) - RE-CORRECTED (2026-10-08, direct disassembly of
// 0x00a281a0's own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper,
// not PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_door_open(lua_State* L) {
    logCall(L, upLog(L), "door_open", upStateTag(L));
    std::string doorName = argString(L, 1);
    bool arg2 = optionalBoolDefault(L, 2, false);
    bool arg3 = optionalBoolDefault(L, 3, true);
    (void)arg2; (void)arg3; // consumed for the real call shape only - 0x00a31920's own body is not modelled beyond its stated inputs
    bool hasArg4 = lua_gettop(L) >= 4 && lua_type(L, 4) != LUA_TNIL;
    std::string arg4Name = hasArg4 ? argString(L, 4) : std::string();
    std::string arg5 = optionalStringDefault(L, 5, "");
    bool hasArg6 = lua_gettop(L) >= 6 && lua_type(L, 6) != LUA_TNIL;
    std::string arg6Name = hasArg6 ? argString(L, 6) : std::string();

    EngineState* st = upState(L);
    if (!st->objectResolves().get(doorName)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    PlacedObjectState& door = st->getOrCreatePlacedObject(doorName);
    if (!hasArg4) {
        ++door.simpleOpenCount;
        lua_pushboolean(L, 1); // HIGH CONFIDENCE - see doc comment above
        return 1;
    }
    std::string resolvedArg4Name;
    if (!resolveFollowerAwareObject(st, arg4Name, resolvedArg4Name)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!door.hasInteractionAnchor.get()) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    door.lastInteractionCharacterName = resolvedArg4Name;
    door.lastInteractionAnimStateName = arg5;
    door.lastInteractionTargetName.clear();
    if (hasArg6 && st->objectResolves().get(arg6Name)) { // OPEN until set
        door.lastInteractionTargetName = arg6Name;
    }
    ++door.richPathAppliedCount;
    lua_pushboolean(L, 1);
    return 1;
}

// audio_conversation_play (§2.10, 0x00a3c9d0) - 129 calls / 41 scripts.
// CONFIRMED: 1 mandatory number, read unconditionally, truncated to a
// byte. Return: none. See ConversationPlay's own doc comment
// (engine_state.h) for the full host-gate/shared-lookup reasoning: the
// opcode-0x51 queue path shares audio_conversation_end's own CONFIRMED-
// false-in-single-player host check and is not modelled further; the
// mark/interrupt step and the independent 0x008788e0 direct dispatch both
// key off the SAME "current conversation" lookup, genuinely OPEN.
int stub_audio_conversation_play(lua_State* L) {
    logCall(L, upLog(L), "audio_conversation_play", upStateTag(L));
    uint8_t idByte = static_cast<uint8_t>(truncateEa2596(lua_tonumber(L, 1)) & 0xFF); // 1 mandatory number, truncated to a byte (CONFIRMED)
    EngineState* st = upState(L);
    ConversationPlay& cp = st->conversationPlay();
    ++cp.playCount;
    cp.lastIdByte = idByte;
    // Host-gated opcode-0x51 queue: CONFIRMED false in single player (the
    // identical gate audio_conversation_end, §8.2, already establishes) -
    // never fires in this project's scope; called only so this path is
    // visibly exercised/not silently skipped, matching that function's
    // own "reduces to argument parsing" boundary for the same gate.
    (void)st->coopLocalIsHost();
    if (cp.currentResolves.get()) { // OPEN until set
        bool wasAlreadyActive = cp.alreadyActiveFlag;
        cp.alreadyActiveFlag = true;
        if (wasAlreadyActive) {
            ++cp.interruptCount; // CONFIRMED mark-while-already-set -> interrupt/stop pair
        }
        ++cp.directDispatchCount; // CONFIRMED: mode bit 0x2 always set (literal mode 3), no host check
    }
    return 0;
}

// game_peg_unload (§2.7, 0x00845650) - 122 calls / 41 scripts. CONFIRMED:
// 1 optional argument - absent/nil/non-string, OR a name that never
// resolves, OR a resource not currently in one of the two CONFIRMED
// loaded states (2 or 3) is a silent no-op. Return: none. See
// EngineState::pegInLoadedState()'s own doc comment for why "never
// configured" maps to the same CONFIRMED no-op outcome, not a refusal.
int stub_game_peg_unload(lua_State* L) {
    logCall(L, upLog(L), "game_peg_unload", upStateTag(L));
    if (lua_gettop(L) < 1 || lua_type(L, 1) != LUA_TSTRING) return 0; // CONFIRMED: nil/missing/non-string -> silent no-op
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->pegInLoadedState().known(name) && st->pegInLoadedState().get(name)) {
        st->recordPegUnload(name);
    }
    return 0;
}

// pop_screen (§2.8, 0x00842aa0) - 106 calls / 45 scripts. CONFIRMED: no
// arguments used. Return: none. Pops the current top entry off the UI
// screen-stack singleton whenever the stack is non-empty (capacity+depth
// both positive) - see EngineState::uiScreenStackDepth()'s own doc
// comment for the CHOSEN starting-depth reasoning (push_screen, §8.26, is
// out of this batch's scope).
int stub_pop_screen(lua_State* L) {
    logCall(L, upLog(L), "pop_screen", upStateTag(L));
    (void)lua_gettop(L); // CONFIRMED: called, result never read again
    EngineState* st = upState(L);
    if (st->uiScreenStackDepth() > 0) {
        --st->uiScreenStackDepth();
        ++st->popScreenCount();
    }
    return 0;
}

// object_is_in_trigger (§1.12, 0x00a57720) - 86 calls / 19 scripts.
// CONFIRMED (2026-10-01, direct disassembly): arg 1 = trigger (resolved
// via the shared objectResolves() stand-in), arg 2 = object (resolved via
// the SAME flat map, standing in for the real position-lookup resolver
// 0x00a455a0 - this project's project-wide "no coordinate system"
// simplification). Arg 2 is parsed UNCONDITIONALLY even when arg 1 fails
// to resolve (CONFIRMED) - reproduced by reading both via .get() before
// branching (note: a name NEVER configured at all still throws OPEN from
// objectResolves() itself, the same convention every other named-object
// native in this project uses; only a name explicitly .set(name,false) by
// a test takes the defined "resolve failed" branch below). Return: 1
// boolean - the real geometric containment test's own outcome, genuinely
// OPEN per spec (no in-scope geometry/extents data), tracked as its own
// fact keyed by the (trigger, object) pair.
int stub_object_is_in_trigger(lua_State* L) {
    logCall(L, upLog(L), "object_is_in_trigger", upStateTag(L));
    std::string triggerName = argString(L, 1);
    std::string objectName = argString(L, 2);
    EngineState* st = upState(L);
    bool triggerResolved = st->objectResolves().get(triggerName); // OPEN until set
    bool objectResolved = st->objectResolves().get(objectName);   // OPEN until set - parsed unconditionally (CONFIRMED)
    if (!triggerResolved || !objectResolved) {
        lua_pushboolean(L, 0);
        return 1;
    }
    bool contains = st->triggerContainsObject().get(
        EngineState::triggerContainsObjectKey(triggerName, objectName)); // OPEN until set
    lua_pushboolean(L, contains ? 1 : 0);
    return 1;
}

// ai_do_scripted_move (§3.11, 0x00a3e610) - 154 calls / 29 scripts.
// CONFIRMED: 4 arguments - arg 1 mandatory string (character, generic
// chain), arg 2 mandatory string (destination, a 5th distinct per-kind
// resolver, this project's flat objectResolves() stand-in), arg 3/4
// optional booleans, both defaulting true. Return: 1 boolean - CONFIRMED
// hardcoded true on EVERY path except arg 1 failing to resolve (the
// boolean only answers "did the named character resolve," not "was
// anything actually dispatched"). On success (destination also resolves):
// merges args 3/4 into the character's own pending-scripted-move flags
// byte and bumps the AI-mode-0x18 dispatch counter - see CharacterState's
// own new fields for the full citation.
int stub_ai_do_scripted_move(lua_State* L) {
    logCall(L, upLog(L), "ai_do_scripted_move", upStateTag(L));
    std::string character = argString(L, 1);
    std::string destination = argString(L, 2);
    bool arg3 = optionalBoolDefault(L, 3, true);
    bool arg4 = optionalBoolDefault(L, 4, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(character)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    bool destResolves = st->objectResolves().get(destination); // OPEN until set
    if (destResolves) {
        CharacterState& c = st->getOrCreateCharacter(character);
        c.pendingScriptedMoveFlags.setBits(0x0Cu, (arg3 ? 0x04u : 0u) | (arg4 ? 0x08u : 0u));
        c.pendingScriptedMoveDestination = destination;
        ++c.scriptedMoveDispatchCount;
    }
    lua_pushboolean(L, 1); // CONFIRMED hardcoded true - see doc comment above
    return 1;
}

// on_attack_performed (§3.12, 0x00a57850) - 130 calls / 15 scripts.
// CONFIRMED: 2 mandatory strings (callback name, character name), same
// shape as on_death. Registers into hook slot 8 (base +0x1f00) on the
// generic/#PLAYER#-resolve path (this project's playerClassChainResolves()
// stand-in) or slot 13/0xd (base +0xf8) on the plain-character-resolve
// fallback (objectResolves()) - two different slots for one nominal
// event, CONFIRMED hardcoded per-call-site literals, no deeper semantic
// split. Return: none.
int stub_on_attack_performed(lua_State* L) {
    logCall(L, upLog(L), "on_attack_performed", upStateTag(L));
    std::string callbackName = argString(L, 1);
    std::string targetName = argString(L, 2);
    EngineState* st = upState(L);
    if (st->playerClassChainResolves(targetName)) { // CHOSEN default now applies when no test .set() it - generic/#PLAYER# path -> slot 8
        st->getOrCreateCharacter(targetName).onAttackPerformedGenericSlotCallback = callbackName;
    } else if (st->objectResolves().get(targetName)) { // OPEN until set - plain-character fallback -> slot 13 (0xd)
        st->getOrCreateCharacter(targetName).onAttackPerformedCharacterSlotCallback = callbackName;
    }
    return 0;
}

// teleport (§4.3, 0x00a60e30) - 181 calls / 33 scripts. CONFIRMED: 2
// mandatory strings (arg 1 the mover, arg 2 the destination reference) +
// 6 optional arguments (arg 3 bool default false, arg 4 bool default
// true, args 5-7 numbers default 0.0 each, arg 8 bool default true).
// Return: none. On EITHER resolve failure (dest via the type-gated
// resolver family §4.13, or the mover via the player-first character
// chain - both this project's flat objectResolves() stand-in), the
// function returns immediately with NO request allocated at all
// (CONFIRMED). On success: allocates a kind-4 scripted request (counted,
// not individually modelled - teleport_check_done, §35.2, is out of this
// batch's scope) then dispatches via one of two mutually exclusive paths
// selected by the resolved mover's own class-row bit - see
// TeleportRequest's own doc comment (engine_state.h) for the
// playerClassChainResolves() reuse reasoning. arg 3 ("exit vehicles") is
// CONFIRMED forwarded ONLY on the player path.
//
// Mover resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct
// disassembly of its own body): a two-stage #PLAYER#-then-#FOLLOWER#
// wrapper, not PLAYER-only as an earlier reading had it - wired here,
// follower-sentinel-resolver pass, via resolveFollowerAwareObject(). The
// destination resolver family (§4.13) is unaffected - distinct resolver,
// not re-corrected.
int stub_teleport(lua_State* L) {
    logCall(L, upLog(L), "teleport", upStateTag(L));
    std::string target = argString(L, 1);
    std::string dest = argString(L, 2);
    bool arg3 = optionalBoolDefault(L, 3, false);
    bool arg4 = optionalBoolDefault(L, 4, true);
    double arg5 = optionalNumberDefault(L, 5, 0.0);
    double arg6 = optionalNumberDefault(L, 6, 0.0);
    double arg7 = optionalNumberDefault(L, 7, 0.0);
    bool arg8 = optionalBoolDefault(L, 8, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(dest)) return 0;   // OPEN until set - destination resolver family (§4.13)
    std::string resolvedTarget;
    if (!resolveFollowerAwareObject(st, target, resolvedTarget)) return 0; // OPEN until set - player-first character chain (0x00a281a0); may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    ++st->teleportScriptedRequestCount(); // CONFIRMED: a kind-4 scripted request is always allocated on success
    TeleportRequest& req = st->teleportRequests()[resolvedTarget];
    ++req.callCount;
    req.destinationName = dest;
    req.offsetX = arg5; req.offsetY = arg6; req.offsetZ = arg7;
    req.arg4 = arg4;
    req.arg8 = arg8;
    bool isPlayerClass = st->playerClassChainResolves(resolvedTarget); // CHOSEN default now applies when no test .set() it - see engine_state.h
    req.tookPlayerPath = isPlayerClass;
    req.exitVehiclesArg3 = isPlayerClass && arg3; // CONFIRMED: NOT forwarded on the non-player path
    return 0;
}

// release_to_world (§4.4, 0x00a5cb10) - 128 calls / 27 scripts. CONFIRMED:
// 1 mandatory string. Three-tier resolver family (group/character/
// vehicle, stopping at first success, all liveness-checked via this
// project's shared objectResolves() flat map) - see
// EngineState::releaseToWorldKind()'s own doc comment for the dedicated
// kind-classifier reasoning. Tier 2/3 (character/vehicle): releases the
// one resolved object directly. Tier 1 (group): releases every currently-
// active member of the named group (a test-supplied, pre-filtered list -
// this project has no generic group-membership-with-active-bit structure).
// Return: none.
int stub_release_to_world(lua_State* L) {
    logCall(L, upLog(L), "release_to_world", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    int32_t kind = st->releaseToWorldKind().get(name); // OPEN until set - which tier resolved
    if (kind == 1) {
        for (const auto& member : st->releaseToWorldGroupMembers()[name]) {
            ++st->releasedCount()[member];
        }
    } else {
        ++st->releasedCount()[name]; // tiers 2/3 - same vtable-slot-+0x5c release method either way
    }
    return 0;
}

// vehicle_suppress_npc_exit (§4.5, 0x00a63710) - 116 calls / 24 scripts.
// CONFIRMED: 1 mandatory string (vehicle, resolved via this project's
// shared objectResolves() flat map, standing in for the dedicated
// object-to-vehicle-instance resolver 0x00a281e0) + 1 optional boolean,
// default true. CORRECTION relayed by the orchestrator and independently
// verified against the spec text directly: the two setter thunks do NOT
// each own one bit - BOTH bits (0x40 "no_stop_enter_with_driver" and 0x20
// "force_no_passenger_exit") are set TOGETHER to the same boolean by
// EITHER call direction (true-thunk 0x00b25930 / false-thunk 0x00b25950
// both call the identical pair of bit-setters, 0x00b231f0 then
// 0x00b23070, with the SAME literal boolean). Same double-gate shape
// (direct flip when forceFlagGatePasses, else record-and-replicate) as
// every other force-flag field in this struct. Return: none.
int stub_vehicle_suppress_npc_exit(lua_State* L) {
    logCall(L, upLog(L), "vehicle_suppress_npc_exit", upStateTag(L));
    std::string name = argString(L, 1);
    bool suppress = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    uint32_t mask = VehicleState::kNoStopEnterWithDriverBit | VehicleState::kForceNoPassengerExitBit; // BOTH bits together, CONFIRMED
    if (v.forceFlagGatePasses.get()) { // CONFIRMED true in single player, §3.7
        v.vehicleAiForceFlagsByte4.setBits(mask, suppress ? mask : 0u);
    } else {
        EngineState::replicateStateChange("vehicle_suppress_npc_exit", name);
    }
    return 0;
}

// set_player_can_enter_exit_vehicles (§4.6, 0x00a5d320) - 109 calls / 18
// scripts. CONFIRMED: 1 mandatory string (target, resolved via this
// project's shared objectResolves() flat map, standing in for the
// generic name/"#PLAYER#" resolver 0x00a280c0) + 1 optional boolean,
// default true ("can enter/exit"). The real setter is called with the
// LOGICAL NEGATION of the argument - stored here as the bit's own sense
// ("cannot enter/exit"). Same double-gate shape as every other force-flag
// field in CharacterState. Return: none.
int stub_set_player_can_enter_exit_vehicles(lua_State* L) {
    logCall(L, upLog(L), "set_player_can_enter_exit_vehicles", upStateTag(L));
    std::string name = argString(L, 1);
    bool canEnterExit = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    bool cant = !canEnterExit; // CONFIRMED logical negation
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, §3.7
        c.flags28ac.setBits(CharacterState::kCantEnterExitVehiclesBit28ac,
                             cant ? CharacterState::kCantEnterExitVehiclesBit28ac : 0u);
    } else {
        EngineState::replicateStateChange("set_player_can_enter_exit_vehicles", name);
    }
    return 0;
}

// continuous_spawn_stop (§4.7, real address 0x00a42b50) - 109 calls / 20
// scripts. CONFIRMED: 1 mandatory string (spawn-region name, resolved via
// the SAME resolver family release_to_world's tier-1 member uses - this
// project's shared objectResolves() flat map) + 1 optional boolean,
// default false. If a slot exists, it is unconditionally reset regardless
// of the boolean (CONFIRMED); the boolean is consulted only when the
// slot's own activity marker was already non-zero (CONFIRMED). NEW
// 2026-10-08 (continuous_spawn_start, §4.8, now implemented - see
// lua_spec_confirmed_stubs_b31_continuous_spawn_vehicle_enter_tutorial.cpp):
// the reset routine unconditionally zeroes/defaults the WHOLE slot
// (CONFIRMED, already cited above before continuous_spawn_start existed
// to give this project a detailed per-region record) - now that one does
// (EngineState::continuousSpawnRegions()), it is erased here too, so a
// subsequent continuous_spawn_start for the same region takes the
// "claim a new slot" branch again, not the fast re-start path. Return:
// none.
int stub_continuous_spawn_stop(lua_State* L) {
    logCall(L, upLog(L), "continuous_spawn_stop", upStateTag(L));
    std::string name = argString(L, 1);
    bool arg2 = optionalBoolDefault(L, 2, false);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    ++st->continuousSpawnStopCount()[name];
    auto it = st->continuousSpawnActive().find(name);
    bool wasActive = it != st->continuousSpawnActive().end() && it->second;
    if (wasActive) {
        st->continuousSpawnLastStopBoolean()[name] = arg2; // CONFIRMED: only consulted on this path
    }
    st->continuousSpawnActive()[name] = false; // CONFIRMED: unconditional reset regardless of the boolean
    st->continuousSpawnRegions().erase(name);  // NEW 2026-10-08: the reset zeroes the WHOLE slot (see doc comment above)
    return 0;
}

} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB08() {
    static const std::vector<std::string> names = {
        "objective_text",
        "on_trigger_exit",
        "spawn_region_enable",
        "door_open",
        "audio_conversation_play",
        "game_peg_unload",
        "pop_screen",
        "object_is_in_trigger",
        "ai_do_scripted_move",
        "on_attack_performed",
        "teleport",
        "release_to_world",
        "vehicle_suppress_npc_exit",
        "set_player_can_enter_exit_vehicles",
        "continuous_spawn_stop",
    };
    return names;
}

void registerB08(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    // All 15 are `gameplay` per tools/lua_all_registered_1490_tagged.txt
    // (grepped directly, not assumed), EXCEPT game_peg_unload and
    // pop_screen, which are `ui`; none are dual-registered (each appears
    // exactly once in that file).
    if (wants("objective_text")) registerOne(L, state, log, stateTag, "objective_text", openGuard<stub_objective_text>);
    if (wants("on_trigger_exit")) registerOne(L, state, log, stateTag, "on_trigger_exit", openGuard<stub_on_trigger_exit>);
    if (wants("spawn_region_enable")) registerOne(L, state, log, stateTag, "spawn_region_enable", openGuard<stub_spawn_region_enable>);
    if (wants("door_open")) registerOne(L, state, log, stateTag, "door_open", openGuard<stub_door_open>);
    if (wants("audio_conversation_play")) registerOne(L, state, log, stateTag, "audio_conversation_play", openGuard<stub_audio_conversation_play>);
    if (wants("game_peg_unload")) registerOne(L, state, log, stateTag, "game_peg_unload", openGuard<stub_game_peg_unload>);
    if (wants("pop_screen")) registerOne(L, state, log, stateTag, "pop_screen", openGuard<stub_pop_screen>);
    if (wants("object_is_in_trigger")) registerOne(L, state, log, stateTag, "object_is_in_trigger", openGuard<stub_object_is_in_trigger>);
    if (wants("ai_do_scripted_move")) registerOne(L, state, log, stateTag, "ai_do_scripted_move", openGuard<stub_ai_do_scripted_move>);
    if (wants("on_attack_performed")) registerOne(L, state, log, stateTag, "on_attack_performed", openGuard<stub_on_attack_performed>);
    if (wants("teleport")) registerOne(L, state, log, stateTag, "teleport", openGuard<stub_teleport>);
    if (wants("release_to_world")) registerOne(L, state, log, stateTag, "release_to_world", openGuard<stub_release_to_world>);
    if (wants("vehicle_suppress_npc_exit")) registerOne(L, state, log, stateTag, "vehicle_suppress_npc_exit", openGuard<stub_vehicle_suppress_npc_exit>);
    if (wants("set_player_can_enter_exit_vehicles")) registerOne(L, state, log, stateTag, "set_player_can_enter_exit_vehicles", openGuard<stub_set_player_can_enter_exit_vehicles>);
    if (wants("continuous_spawn_stop")) registerOne(L, state, log, stateTag, "continuous_spawn_stop", openGuard<stub_continuous_spawn_stop>);
}

} // namespace stub_sections
} // namespace sr3luahost
