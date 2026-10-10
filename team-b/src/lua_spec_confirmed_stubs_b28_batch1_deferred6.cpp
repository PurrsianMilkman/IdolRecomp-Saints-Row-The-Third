// lua_spec_confirmed_stubs_b28_batch1_deferred6.cpp
//
// Batch 2026-10-08: the 6 natives wave 1 batch 1 (spec-lua-api-behaviour.md
// §1-4, lua_spec_confirmed_stubs_b08_batch1_core.cpp) originally deferred -
// on_dismiss (§1.11), character_prevent_flinching (§3.14),
// character_allow_ragdoll (§3.16), npc_combat_enable (§3.17),
// inv_weapon_add_temporary (§4.10), inv_weapon_remove_temporary (§4.11).
// b08's own top comment explains WHY they were deferred at the time: each
// still carried its own original 2026-09-30 "NOT yet cleared for
// implementation" Review-status line with no later dated resolution
// anywhere in the document, independently verified by reading all 21 of
// that batch's sections directly.
//
// Later the same day (2026-10-08), a fresh independent direct-disassembly
// pass re-derived all 6 from scratch and dated each one's own Review-status
// line "cleared for implementation" (grepped directly, verbatim): §1.11
// ("RE-CONFIRMED... No OPEN items remain"), §3.14/§3.16/§3.17 ("CONFIRMED by
// independent direct disassembly... No OPEN items... cleared for
// implementation"), §4.10/§4.11 ("CONFIRMED at the structural/address level
// ... No claim refuted... cleared for implementation"). New, separate
// section file (not retrofitted into b08) per this project's own "a new
// name/registration pair, never retrofitted into an existing b0N file"
// fan-out convention (lua_spec_confirmed_stubs_registry.cpp's own top
// comment).
//
// Resolver shapes, per each entry's own 2026-10-08 text:
//   - on_dismiss: the PLAIN character resolver (0x005e4dd0) only - no
//     generic/"#PLAYER#"-chain attempt at all (a genuine behavioral gap
//     from its on_* hook siblings) - modeled via CharacterState::
//     getOrCreateCharacter() directly, same "always lazily resolves, no
//     objectResolves() gate" convention stub_on_revived (b01) already
//     established for the SAME plain-resolver shape.
//   - character_prevent_flinching/character_allow_ragdoll/
//     npc_combat_enable: the generic/"#PLAYER#"-chain resolver (three
//     separate entry stubs into the same family - 0x00a281a0 for the first
//     two, 0x00a28150 for the third) - modeled via this project's existing
//     objectResolves()+getOrCreateCharacter() stand-in, same shape as
//     character_prevent_explosion_fling/character_prevent_bumping/
//     character_set_never_catch_fire (b06) which share npc_combat_enable's
//     and character_prevent_flinching/character_allow_ragdoll's own target
//     force-flag byte.
//   - inv_weapon_add_temporary/inv_weapon_remove_temporary: target via the
//     SAME generic chain as above; weapon name via a SEPARATE, dedicated
//     weapon-definition lookup (EngineState::weaponDefinitionResolves() -
//     see its own doc comment, engine_state.h, for why this is a flat,
//     test-settable stand-in rather than a real loaded table).
//
// HARDENING POLICY reviewed (orchestrator's standing directive, reproduced
// from b08's own text): none of these 6 natives have a spec-confirmed
// memory-unsafe bug in the original binary - no claim of hardening is made
// here.
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

namespace sr3luahost {
namespace {

// ---------------------------------------------------------------------
// on_dismiss (§1.11, 0x00a57dd0) - 100 calls / 25 scripts. See
// CharacterState::onDismissCallback's own doc comment (character_state.h)
// for the full slot-catalogue/resolver-gap citation.
// ---------------------------------------------------------------------
int stub_on_dismiss(lua_State* L) {
    logCall(L, upLog(L), "on_dismiss", upStateTag(L));
    std::string cb = argString(L, 1);
    std::string target = argString(L, 2);
    CharacterState& character = upState(L)->getOrCreateCharacter(target);
    character.onDismissCallback = cb; // empty string clears the slot - CONFIRMED, same 0x005e4660 body as Sec10.8
    return 0;
}

// ---------------------------------------------------------------------
// character_prevent_flinching (§3.14, 0x00a41ec0) - 92 calls / 23 scripts.
// Arg 1 mandatory string; arg 2 read via lua_toboolean with NO nil-gate
// (CONFIRMED, defaults false via Lua's own nil-handling on an omitted
// arg). Writes the "suppress flinch" bit (0x800 at +0x1c98) in its OWN
// direct sense, gated by the SAME forceFlagGatePasses double-gate every
// other force-flag field in this struct uses (CONFIRMED true in single
// player, Sec3.7).
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as this file's own earlier framing had it (see this file's
// own top comment, written just before that correction landed) - wired
// here, follower-sentinel-resolver pass, via resolveFollowerAwareObject().
// ---------------------------------------------------------------------
int stub_character_prevent_flinching(lua_State* L) {
    logCall(L, upLog(L), "character_prevent_flinching", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = lua_toboolean(L, 2) != 0; // CONFIRMED: no nil-gate, Sec3.14
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.forceFlags1c98.setBits(CharacterState::kPreventFlinchingBit1c98,
                                  value ? CharacterState::kPreventFlinchingBit1c98 : 0u);
    } else {
        EngineState::replicateStateChange("character_prevent_flinching", resolvedName);
    }
    return 0;
}

// ---------------------------------------------------------------------
// character_allow_ragdoll (§3.16, 0x00a41080) - 82 calls / 20 scripts. Arg
// 1 mandatory string; arg 2 read via lua_toboolean with no nil-gate (same
// shape as §3.14, defaults false). CONFIRMED polarity flip (read directly
// off the call site's own `test bl,bl; sete dl`): the underlying engine
// flag is a "never ragdoll" bit (0x80 at +0x1c98), so
// character_allow_ragdoll(name, true) CLEARS it and (name, false) SETS
// it - the logical negation of the Lua argument, stored here in the
// bit's own ("never ragdoll") sense. Same double-gate shape.
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as this file's own earlier framing had it (see this file's
// own top comment, written just before that correction landed) - wired
// here, follower-sentinel-resolver pass, via resolveFollowerAwareObject().
// ---------------------------------------------------------------------
int stub_character_allow_ragdoll(lua_State* L) {
    logCall(L, upLog(L), "character_allow_ragdoll", upStateTag(L));
    std::string name = argString(L, 1);
    bool allow = lua_toboolean(L, 2) != 0; // CONFIRMED: no nil-gate, Sec3.16
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        // CONFIRMED logical negation (Sec3.16's own instruction-level read).
        c.forceFlags1c98.setBits(CharacterState::kNeverRagdollBit1c98,
                                  allow ? 0u : CharacterState::kNeverRagdollBit1c98);
    } else {
        EngineState::replicateStateChange("character_allow_ragdoll", resolvedName);
    }
    return 0;
}

// ---------------------------------------------------------------------
// npc_combat_enable (§3.17, 0x00a55290) - 95 calls / 20 scripts. Arg 1
// mandatory string; arg 2 optional boolean, standard nil-gated idiom,
// DEFAULTING TO TRUE (CONFIRMED). RESOLVED 2026-10-08: the underlying
// setter's (0x004e0cf0) hidden `this` is the character's AI-data
// sub-object at handle offset +0x2b0 - the bit (0x10) lives on that
// sub-object's OWN +8 byte (CharacterState::humanAiDataForceFlagsByte8,
// absolute character +0x2b8), NOT the raw character's own +8 the comment
// on the arguments once hypothesized. CONFIRMED logical negation: arg 2
// defaulting true clears "combat disabled," matching the Lua-visible
// function's own name directly. Same double-gate shape (same
// forceFlagGatePasses field - this is still a per-CHARACTER gate, the
// AI-data sub-object is part of the same character).
// ---------------------------------------------------------------------
int stub_npc_combat_enable(lua_State* L) {
    logCall(L, upLog(L), "npc_combat_enable", upStateTag(L));
    std::string name = argString(L, 1);
    bool enable = optionalBoolDefault(L, 2, true); // CONFIRMED default true, Sec3.17
    EngineState* st = upState(L);
    // Resolver 0x00a28150 - CONFIRMED 2026-10-08 (Sec3.17) as the generic/
    // #PLAYER#-then-#FOLLOWER# chain's third entry point, distinct from
    // 0x00a280c0 and from character_prevent_flinching/character_allow_
    // ragdoll's own 0x00a281a0 above (neither of which accepts #FOLLOWER#) -
    // wired here 2026-10-08, follower-sentinel-resolver pass, via
    // resolveFollowerAwareObject().
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        // CONFIRMED logical negation: enable=true clears "combat disabled".
        c.humanAiDataForceFlagsByte8.setBits(CharacterState::kCombatDisabledBit,
                                             enable ? 0u : CharacterState::kCombatDisabledBit);
    } else {
        EngineState::replicateStateChange("npc_combat_enable", resolvedName);
    }
    return 0;
}

// ---------------------------------------------------------------------
// inv_weapon_add_temporary (§4.10, 0x00a510c0) - 88 calls / 23 scripts.
// Arg 1 mandatory string (target, generic/"#PLAYER#" resolver stand-in -
// objectResolves()+getOrCreateCharacter()); arg 2 mandatory string
// (weapon name, the dedicated weapon-definition lookup stand-in -
// EngineState::weaponDefinitionResolves(), case-folded via the SAME
// AsciiLowerFold() the zone-object resolver already established). 6
// further optional arguments, each individually nil-gated: arg 3 (number,
// rounded via truncateEa2596, default 1 - an ammo/round count), args 4-7
// (booleans, default false), arg 8 (number, rounded, default 0). Writes
// the per-target, per-weapon slot (WeaponTemporaryInventorySlot,
// misc_state.h) - see that struct's own doc comment for exactly which
// fields are CONFIRMED vs. stored raw (HIGH-CONFIDENCE-only attribution).
// No return.
// ---------------------------------------------------------------------
int stub_inv_weapon_add_temporary(lua_State* L) {
    logCall(L, upLog(L), "inv_weapon_add_temporary", upStateTag(L));
    std::string target = argString(L, 1);
    std::string weapon = argString(L, 2);
    int64_t ammoCount = truncateEa2596(optionalNumberDefault(L, 3, 1.0)); // CONFIRMED default 1, Sec4.10
    bool arg4 = optionalBoolDefault(L, 4, false);
    bool arg5 = optionalBoolDefault(L, 5, false);
    bool arg6 = optionalBoolDefault(L, 6, false);
    bool arg7 = optionalBoolDefault(L, 7, false);
    int64_t arg8 = truncateEa2596(optionalNumberDefault(L, 8, 0.0)); // CONFIRMED default 0, Sec4.10
    EngineState* st = upState(L);
    if (!st->objectResolves().get(target)) return 0; // OPEN until set
    if (!st->weaponDefinitionResolves().get(AsciiLowerFold(weapon))) return 0; // OPEN until set
    st->getOrCreateCharacter(target); // same "resolver also lazily tracks the character" convention project-wide
    WeaponTemporaryInventorySlot& slot = st->weaponTemporaryInventory()[target][AsciiLowerFold(weapon)];
    slot.temporaryGrant = true; // CONFIRMED marker
    slot.ammoCount = ammoCount; // CONFIRMED arg 3
    slot.rawArg4 = arg4;
    slot.rawArg5 = arg5;
    slot.rawArg6 = arg6;
    slot.rawArg7 = arg7;
    slot.rawArg8 = arg8;
    ++slot.addCount;
    return 0;
}

// ---------------------------------------------------------------------
// inv_weapon_remove_temporary (§4.11, 0x00a50d00) - 78 calls / 19 scripts.
// Exactly 2 mandatory strings - NO optional arguments at all (CONFIRMED,
// unlike its counterpart). Both resolve through the SAME two resolvers
// §4.10 uses. The shared apply routine clears the "temporary grant"
// marker and the recorded ammo/flags on the target's EXISTING slot for
// this weapon - CONFIRMED a literal constant 0 for the apply routine's own
// third parameter is always passed here (its "always perform an
// additional per-target reselection/cleanup step" branch), modeled as an
// unconditional field reset rather than a separate flag, since this
// project tracks no other consumer of that branch distinction. If no
// slot was ever created for this (target, weapon) pair, this is a no-op
// (CONFIRMED structurally: the real routine falls back to a SECONDARY
// lookup keyed by the weapon-definition's own ammo-type grouping when the
// direct slot pointer is empty - this project has no ammo-type-grouping
// model to drive that fallback, genuinely OPEN, not guessed at). The
// equip-state update (if the removed weapon was currently equipped) is
// likewise genuinely OPEN - see WeaponTemporaryInventorySlot's own doc
// comment (misc_state.h) for the full citation of why it is not modeled.
// No return.
// ---------------------------------------------------------------------
int stub_inv_weapon_remove_temporary(lua_State* L) {
    logCall(L, upLog(L), "inv_weapon_remove_temporary", upStateTag(L));
    std::string target = argString(L, 1);
    std::string weapon = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(target)) return 0; // OPEN until set
    if (!st->weaponDefinitionResolves().get(AsciiLowerFold(weapon))) return 0; // OPEN until set
    auto targetIt = st->weaponTemporaryInventory().find(target);
    if (targetIt == st->weaponTemporaryInventory().end()) return 0; // no existing slot - secondary-lookup fallback OPEN, not modeled
    auto slotIt = targetIt->second.find(AsciiLowerFold(weapon));
    if (slotIt == targetIt->second.end()) return 0;
    WeaponTemporaryInventorySlot& slot = slotIt->second;
    slot.temporaryGrant = false; // CONFIRMED marker clear
    slot.ammoCount = 0;          // CONFIRMED "clears the recorded ammo/flags"
    slot.rawArg4 = slot.rawArg5 = slot.rawArg6 = slot.rawArg7 = false;
    slot.rawArg8 = 0;
    ++slot.removeCount;
    return 0;
}

} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB28() {
    static const std::vector<std::string> names = {
        "on_dismiss",
        "character_prevent_flinching",
        "character_allow_ragdoll",
        "npc_combat_enable",
        "inv_weapon_add_temporary",
        "inv_weapon_remove_temporary",
    };
    return names;
}

void registerB28(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    // All 6 are `gameplay` per tools/lua_all_registered_1490_tagged.txt
    // (grepped directly, not assumed); none are dual-registered.
    if (wants("on_dismiss")) registerOne(L, state, log, stateTag, "on_dismiss", openGuard<stub_on_dismiss>);
    if (wants("character_prevent_flinching")) registerOne(L, state, log, stateTag, "character_prevent_flinching", openGuard<stub_character_prevent_flinching>);
    if (wants("character_allow_ragdoll")) registerOne(L, state, log, stateTag, "character_allow_ragdoll", openGuard<stub_character_allow_ragdoll>);
    if (wants("npc_combat_enable")) registerOne(L, state, log, stateTag, "npc_combat_enable", openGuard<stub_npc_combat_enable>);
    if (wants("inv_weapon_add_temporary")) registerOne(L, state, log, stateTag, "inv_weapon_add_temporary", openGuard<stub_inv_weapon_add_temporary>);
    if (wants("inv_weapon_remove_temporary")) registerOne(L, state, log, stateTag, "inv_weapon_remove_temporary", openGuard<stub_inv_weapon_remove_temporary>);
}

} // namespace stub_sections
} // namespace sr3luahost
