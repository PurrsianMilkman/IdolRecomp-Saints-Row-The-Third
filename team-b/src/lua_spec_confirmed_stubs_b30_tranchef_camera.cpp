// lua_spec_confirmed_stubs_b30_tranchef_camera.cpp
//
// Batch b30 (2026-10-08): closes out three natives the cross-reference
// pass (spec-lua-api-behaviour.md's own "cleared for implementation"/
// CONFIRMED review-status lines against this project's registry) found
// genuinely CONFIRMED-and-unimplemented:
//
//   - character_kill (Sec3.15) and vehicle_is_destroyed (Sec4.9): both
//     were genuinely OPEN (self-contradictory/ambiguous body text) until
//     the 2026-10-08 "tranche F" spec sync (commit 46f90d1) independently
//     re-derived both from scratch and resolved every blocking item.
//   - camera_restrict_set_active (Sec8.12): CONFIRMED since 2026-09-30
//     (every claim in its own Body/Side-effects paragraphs already says
//     CONFIRMED - disassembly), just never implemented by any earlier
//     batch; its own Review-status footer line was simply never updated
//     to say so (the same kind of stale-bookkeeping-only gap tranche F's
//     own entries call out explicitly for themselves).
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

namespace sr3luahost {
namespace {

// ---------------------------------------------------------------------
// character_kill (Sec3.15, 0x00a41a60) - 92 calls/30 scripts. RESOLVED
// 2026-10-08 (tranche F): resolves via the SAME generic/#PLAYER#-then-
// fallback chain as on_vehicle_destroyed/character_is_dead
// (playerClassChainResolves() primary, objectResolves() fallback), then
// routes a FIXED lethal-damage constant (-1.0f) through the shared
// generic damage-application routine (0x0096fa20) with a flags word
// carrying bit 0x100 alone when arg2 is true (tranche F: confirmed no
// conflict with Sec20.32's different caller). This project does not
// simulate 0x0096fa20's own ~5,200-instruction body (hit-point math,
// notoriety/network-authority checks, ragdoll impulse) - the lethal
// constant's own naming and the entry's "not a bypass" framing both read
// as "guaranteed death via the normal pipeline," so the call is modeled
// as deterministic: lifeState -> 5 (the SAME value character_is_dead/
// player_revive already read as "dead/kill-finalized"). See
// CharacterState's own new fields (near lifeState/killFinalizedFlagE3)
// for the full per-field citation. Return: none.
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body, Sec3.15's own text): a two-stage #PLAYER#-then-#FOLLOWER#
// wrapper (0x00a280c0 first, falling back to 0x00a28150) - this was one of
// the sites explicitly deferred pending that correction, follower-
// sentinel-resolver pass. A #FOLLOWER...# literal is translated via
// resolveFollowerAwareObject() first (its own dedicated path, never a
// plain-name resolve); every other name keeps the EXACT original
// playerClassChainResolves()-then-objectResolves() two-path check
// unchanged (resolveFollowerAwareObject()'s own generic fallback checks
// objectResolves() only, not playerClassChainResolves() - preserving that
// OR for ordinary/#PLAYER#-sentinel names here, rather than silently
// narrowing it, matters: a test that only seeds playerClassChainResolves()
// must keep resolving exactly as it did before this pass).
// ---------------------------------------------------------------------
int stub_character_kill(lua_State* L) {
    logCall(L, upLog(L), "character_kill", upStateTag(L));
    std::string name = argString(L, 1);
    bool arg2 = optionalBoolDefault(L, 2, false); // CONFIRMED default false; sets flags-word bit 0x100 alone (Sec3.15, tranche F)
    std::string deathReactionName = optionalStringDefault(L, 3, "");
    bool arg4 = optionalBoolDefault(L, 4, true); // CONFIRMED default true (finalizer is opt-in, runs only when explicitly false)
    EngineState* st = upState(L);
    std::string resolvedName = name;
    bool objectExists;
    if (isFollowerSentinelLiteral(name)) {
        objectExists = resolveFollowerAwareObject(st, name, resolvedName); // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    } else {
        bool primaryResolves = st->playerClassChainResolves(name); // CHOSEN default now applies when no test .set() it - see engine_state.h
        objectExists = primaryResolves || st->objectResolves().get(name); // OPEN until set when primary failed
    }
    if (!objectExists) return 0; // CONFIRMED: a resolve is required to reach the damage call at all
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    ++c.characterKillCallCount;
    c.lastCharacterKillArg2 = arg2; // bit 0x100 alone - real-world PURPOSE stays OPEN (Sec3.15), recorded only
    c.lifeState.set(5); // CONFIRMED-guaranteed lethal hit, modeled as deterministic (see field's own doc comment)
    if (!deathReactionName.empty() || lua_gettop(L) >= 3) {
        // CONFIRMED gate: only looked up once the character is confirmed
        // dead - always true here under this project's own deterministic
        // model, so this always runs when arg3 was given at all.
        c.lastDeathReactionAnimationId = st->lookupAnimationStateId(deathReactionName); // SAME table set_animation_state/Sec7.11 uses
        ++c.deathReactionDispatchCount;
    }
    if (!arg4) { // CONFIRMED: opt-in, default true means SKIP
        if (c.lifeState.known() && c.lifeState.get() == 6) { // CONFIRMED gate: "downed" (Sec32.3/Sec34.5's own value)
            c.lifeState.set(5); // 0x00952760's own confirmed literal write
        }
        c.killFinalizedFlagE3.set(true); // finalizer clears threat/attacker-reference fields - test-observable stand-in only
        // CONFIRMED 2026-10-09 (Sec3.4's own in-place correction): the SAME
        // finalizer (0x009a7610) also clears the real attacker/threat-style
        // handle pair +0x18f0/+0x18f4 - see CharacterState::reviverHandle's
        // own doc comment.
        c.reviverHandle.set(0);
        ++c.characterKillFinalizerCount;
    }
    return 0;
}

// ---------------------------------------------------------------------
// vehicle_is_destroyed (Sec4.9, 0x00a65ff0) - 97 calls/22 scripts.
// RESOLVED 2026-10-08 (tranche F): arg 1 is a VEHICLE name (not an
// occupant, as the pre-tranche-F desk-review text guessed) - resolved
// directly via 0x0062a190, the SAME by-name resolver vehicle_exists
// (Sec9.9) uses (this project's shared objectResolves() stand-in, per
// Sec9.9's own citation - "the project-wide one shared name-resolves map
// stand-in for every per-kind resolver"). After the liveness gate, vtable
// slot +0x70 on the VEHICLE itself (there is no occupant anywhere in this
// function) is read, and its own +0x16c8 bit 0x1 is pushed UNMODIFIED - a
// direct, unnegated read, NOT a negation as the pre-tranche-F text wrongly
// claimed - the SAME destroyedFlag16c8 field vehicle_detonate (Sec9.7)
// already models (VehicleState). Unresolved name, a dead vehicle handle,
// or a failed liveness check all push true (destroyed) as a fail-safe
// default (unchanged by the tranche-F correction). Return: 1 boolean.
// ---------------------------------------------------------------------
int stub_vehicle_is_destroyed(lua_State* L) {
    logCall(L, upLog(L), "vehicle_is_destroyed", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool resolved = st->objectResolves().get(name); // OPEN until set (0x0062a190)
    bool destroyed = true; // CONFIRMED fail-safe default
    if (resolved) {
        VehicleState& v = st->getOrCreateVehicle(name);
        destroyed = v.destroyedFlag16c8.get(); // CONFIRMED: direct, unnegated read (tranche-F correction)
    }
    lua_pushboolean(L, destroyed ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// camera_restrict_set_active (Sec8.12, 0x00a445e0). CONFIRMED: arg1
// mandatory boolean (no nil-gate) - the active/restrict flag; arg2
// optional string (standard nil-gated idiom) - a target name. A 3-way
// dispatch on arg2 (CONFIRMED, corrected 2026-09-29 - "absent" and
// "resolves to current player" are NOT the same case): absent -> the
// local setter runs AND the record-and-replicate path ALSO fires ("the
// absent case does BOTH, not just the local write"); resolves to the
// local player (0x009da4e0, EngineState::localPlayerNameForTesting(),
// the SAME accessor camera_restrict_set_limits/Sec14.21 already models)
// -> local setter only, no replicate ("the only purely-local case");
// resolves to anything else (or fails to resolve) -> local setter skipped
// entirely, only the replicate path runs. This function does NOT use the
// host-check gate several siblings share (Sec8.2/8.5/8.13/8.18) - its own
// branch is controlled purely by what the target resolves to. Return:
// none.
// ---------------------------------------------------------------------
int stub_camera_restrict_set_active(lua_State* L) {
    logCall(L, upLog(L), "camera_restrict_set_active", upStateTag(L));
    bool active = lua_toboolean(L, 1) != 0; // CONFIRMED: mandatory, no nil-gate, Sec8.12
    bool argPresent = lua_gettop(L) >= 2 && lua_type(L, 2) != LUA_TNIL;
    std::string target = argPresent ? argString(L, 2) : "";
    EngineState* st = upState(L);
    if (!argPresent) {
        st->setCameraRestrictionActive(active); // CONFIRMED: local setter runs
        EngineState::replicateStateChange("camera_restrict_set_active", ""); // CONFIRMED: "the absent case does BOTH"
        return 0;
    }
    bool resolved = st->objectResolves().get(target); // OPEN until set (0x00a280c0)
    bool isLocalPlayer = resolved && st->localPlayerNameForTesting().known() && st->localPlayerNameForTesting().get() == target;
    if (isLocalPlayer) {
        st->setCameraRestrictionActive(active); // CONFIRMED: "the only purely-local case"
    } else {
        EngineState::replicateStateChange("camera_restrict_set_active", target); // CONFIRMED: local setter skipped entirely
    }
    return 0;
}

} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB30() {
    static const std::vector<std::string> names = {
        "character_kill",
        "vehicle_is_destroyed",
        "camera_restrict_set_active",
    };
    return names;
}

void registerB30(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    // All 3 are `gameplay` per the 1,014-entry gameplay table citation
    // their own sections give (Sec3/Sec4/Sec8's shared table 0x00a20840).
    if (wants("character_kill"))
        registerOne(L, state, log, stateTag, "character_kill", openGuard<stub_character_kill>);
    if (wants("vehicle_is_destroyed"))
        registerOne(L, state, log, stateTag, "vehicle_is_destroyed", openGuard<stub_vehicle_is_destroyed>);
    if (wants("camera_restrict_set_active"))
        registerOne(L, state, log, stateTag, "camera_restrict_set_active", openGuard<stub_camera_restrict_set_active>);
}

} // namespace stub_sections
} // namespace sr3luahost
