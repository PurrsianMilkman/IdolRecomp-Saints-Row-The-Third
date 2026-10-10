// lua_spec_confirmed_stubs_b19_wave2_char_npc_inv.cpp
//
// Wave 2, batch W2-8 ("§38-60 gameplay B: character/NPC/inventory/party/
// HUD/minimap/mission-flow/hooks", 93 names) - this is the "character/NPC/
// inventory/party" half (55 names); the "HUD/minimap/mission-flow/hooks"
// half (38 names) is lua_spec_confirmed_stubs_b20_wave2_hud_minimap_hooks.cpp.
// NEW file, following the exact same namesB0N()/registerB0N() pair pattern
// as every other split file (see lua_spec_confirmed_stubs_registry.cpp's
// own top comment for the full split rationale and shared-registration
// mechanism). All new EngineState fields both this file and its B20
// sibling rely on live in ONE shared block of include/sr3luahost/
// engine_state.h (added alongside this file) - see each field's own doc
// comment there for citation/reasoning, not re-derived per stub here.
//
// Clean-room, strict: every behavior claim below cites its own spec-lua-
// api-behaviour.md section; CONFIRMED/HYPOTHESIS/CHOSEN/OPEN labelled
// per this project's standing discipline. A handful of names in this
// batch (flagged AGG in the dispatch brief) have NO individuated spec text
// at all - only an aggregate "resolved in full, no crash shape" mention
// alongside siblings that DO get full write-ups; those are registered but
// throw OpenStateError on their own actual behavior (see each one's own
// doc comment) rather than fabricate a return value/side effect this spec
// genuinely does not supply - the "no-invented-fixes" policy applied
// literally, not merely cited.
//
// HARDENING POLICY (named memory-safety findings, flagged CRASH in the
// dispatch brief): group_hide_do, inv_item_get_weapon_upgrades,
// ai_force_team_idle, action_sequence_is_playing, perform_killswitch,
// player_get_custom_voice, action_play_custom_do. Each either (a) is
// hardened with a named per-site counter (see its own doc comment for
// which), or (b) has no structural equivalent to harden in this project's
// own simplified object model (explicitly noted per-case, not silently
// skipped) - see each stub's own doc comment for which applies.
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

namespace sr3luahost {
namespace {

// ---------------------------------------------------------------------
// group_is_loaded_internal (Sec41.5, 0x00a4c9f0). 1 group name. Returns 1
// boolean: one bit of the group's own byte, false on a failed lookup.
// CHOSEN: reuses the EXISTING groupAlreadyCreated() existence registry
// (Sec53.3's own group_create_do, already in this codebase) directly as
// the "loaded" bit - the only real "does this group exist" signal this
// project tracks; see EngineState::w28GroupVisibility's own doc comment
// for the full reasoning (including why group_show_do/_hide_do below have
// no member list to walk).
// ---------------------------------------------------------------------
int stub_group_is_loaded_internal(lua_State* L) {
    logCall(L, upLog(L), "group_is_loaded_internal", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    lua_pushboolean(L, st->groupAlreadyCreated(name) ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// group_show_do (Sec41.5, 0x00a4ca60). 1 group name. No return. CONFIRMED
// real null-pointer crash path in the real engine when a group member's
// own object is null/wrong-kind - NOT reproduced: this project (Sec53.3's
// own groupCreateCheckDone() doc comment) deliberately tracks group
// EXISTENCE only, never a member roster, so there is no member list to
// walk and no null member object to dereference - implemented as a safe,
// existence-gated visibility flag instead (a CHOSEN scope limitation).
// CORRECTED 2026-10-08 (review of this batch): "existence-gated" was
// always the stated design (see EngineState::w28GroupVisibility()'s own
// doc comment) but a prior version of this stub never actually checked
// groupAlreadyCreated() before mutating state - hiding/showing a group
// that was never created via group_create_do/_hidden_do silently "worked"
// anyway. Now refuses (no-op, counted) on a never-created group, matching
// group_is_loaded_internal's own read of the identical existence
// registry.
// ---------------------------------------------------------------------
int stub_group_show_do(lua_State* L) {
    logCall(L, upLog(L), "group_show_do", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->groupAlreadyCreated(name)) { // HARDENED: existence-gated, see doc comment above
        st->recordGroupShowOnUncreatedGuard();
        return 0;
    }
    auto& vis = st->w28GroupVisibility(name);
    vis.hidden = false;
    ++vis.showCount;
    return 0;
}

// ---------------------------------------------------------------------
// group_hide_do (Sec41.5, 0x00a4c9a0) CRASH. Same shape/sibling as
// group_show_do above - see that stub's own doc comment for why this
// project's group model has no member list to crash against (the
// hardening requirement is trivially satisfied: there is no unchecked
// per-member dereference in this reimplementation to begin with) and for
// the 2026-10-08 existence-gate correction, which applies identically
// here.
// ---------------------------------------------------------------------
int stub_group_hide_do(lua_State* L) {
    logCall(L, upLog(L), "group_hide_do", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->groupAlreadyCreated(name)) { // HARDENED: existence-gated, see group_show_do's doc comment
        st->recordGroupHideOnUncreatedGuard();
        return 0;
    }
    auto& vis = st->w28GroupVisibility(name);
    vis.hidden = true;
    ++vis.hideCount;
    return 0;
}

// ---------------------------------------------------------------------
// players_naked (Sec51.4; RT 1). CONFIRMED only as "a strip-and-resync
// routine covering both local and remote co-op players" (aggregate text,
// no individuated argument list). CHOSEN argument shape: 1 optional
// boolean, nil-gated default true (this project's own majority convention
// for a bare enable/disable setter where the spec gives no explicit
// default polarity). Applies to the local player always, and to the co-op
// partner only if present (EngineState::playerRig().coopPlayerPresent).
// ---------------------------------------------------------------------
int stub_players_naked(lua_State* L) {
    logCall(L, upLog(L), "players_naked", upStateTag(L));
    bool naked = optionalBoolDefault(L, 1, true);
    EngineState* st = upState(L);
    st->localPlayerNaked() = naked;
    if (st->playerRig().coopPlayerPresent.known() && st->playerRig().coopPlayerPresent.get()) {
        st->coopPlayerNaked() = naked;
    }
    return 0;
}

// ---------------------------------------------------------------------
// melee_get_move_id_from_name (Sec50.5, Sec18/T18). 1 move-name string.
// Returns 65535 (NOT -1/nil) for an unrecognized move name (CONFIRMED).
// CHOSEN: this project has no real move-name->id table anywhere (none is
// given by this spec text; the actual shipped table is engine data this
// project does not have) - every name is therefore genuinely
// "unrecognized" here, so this stub always returns the one CONFIRMED
// sentinel rather than fabricate any mapping.
// ---------------------------------------------------------------------
int stub_melee_get_move_id_from_name(lua_State* L) {
    logCall(L, upLog(L), "melee_get_move_id_from_name", upStateTag(L));
    (void)argString(L, 1);
    lua_pushnumber(L, 65535.0);
    return 1;
}

// ---------------------------------------------------------------------
// player_parachute_has_backpack (Sec60.1; RT 1). CONFIRMED only "resolves
// in full with no crash shape" (aggregate text, no individuated argument
// list). CHOSEN argument shape: 1 optional player-name string, nil-gated
// default "#PLAYER1#" (this project's own established inv_item_* family
// default, Sec41.3). Reuses the EXISTING CharacterState::
// parachuteBackpackWorn field directly (already modelled for the sibling
// player_parachute_wear_backpack setter) - a pure read, no new state.
// ---------------------------------------------------------------------
int stub_player_parachute_has_backpack(lua_State* L) {
    logCall(L, upLog(L), "player_parachute_has_backpack", upStateTag(L));
    std::string name = optionalStringDefault(L, 1, "#PLAYER1#");
    EngineState* st = upState(L);
    bool has = false;
    if (st->objectResolves().get(name)) { // OPEN until set
        has = st->getOrCreateCharacter(name).parachuteBackpackWorn;
    }
    lua_pushboolean(L, has ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// set_cower_variant (Sec39.1, 0x00a5e200). Character + a number (no
// nil-gate, absent reads 0). No return. CONFIRMED clamps to [0,4];
// double-gate "human"/"cower_stand_variant" setter. Stored externally
// (EngineState::w28CharacterMisc, not CharacterState itself - see that
// struct's own doc comment).
// ---------------------------------------------------------------------
int stub_set_cower_variant(lua_State* L) {
    logCall(L, upLog(L), "set_cower_variant", upStateTag(L));
    std::string name = argString(L, 1);
    int64_t variant = truncateEa2596(lua_tonumber(L, 1 + 1)); // no nil-gate, CONFIRMED
    if (variant < 0) variant = 0;
    if (variant > 4) variant = 4;
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        st->w28CharacterMisc(name).cowerVariant = static_cast<int32_t>(variant);
    } else {
        EngineState::replicateStateChange("set_cower_variant", name);
    }
    return 0;
}

// ---------------------------------------------------------------------
// set_unjackable_by_ai_flag (Sec39.2, 0x00a5d920). Vehicle + optional
// boolean, default true. No return. CONFIRMED double-gate "vehicle_ai"/
// "vai_force_flagsunjackable_by_ai" setter, bit 0x02 of the vehicle-AI
// sub-record byte +5 - reuses the EXISTING VehicleState::
// vehicleAiForceFlags field directly (same byte vehicle_set_no_chase,
// Sec33.1, already uses for a DIFFERENT bit).
// ---------------------------------------------------------------------
constexpr uint32_t kUnjackableByAiBit = 0x02;
int stub_set_unjackable_by_ai_flag(lua_State* L) {
    logCall(L, upLog(L), "set_unjackable_by_ai_flag", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    if (v.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7/Sec30.5
        v.vehicleAiForceFlags.setBits(kUnjackableByAiBit, flag ? kUnjackableByAiBit : 0u);
    } else {
        EngineState::replicateStateChange("set_unjackable_by_ai_flag", name);
    }
    return 0;
}

// ---------------------------------------------------------------------
// set_unrecruitable_flag (Sec39.1, 0x00a5da40). Character + optional
// boolean, default true. No return. CONFIRMED double-gate "npc"/
// "npc_force_flagsunrecruitable" setter, bit 0x10 of character byte
// +0x1e02 - stored in W28CharacterMisc::forceFlags1e02 (external, see
// that struct's own doc comment for why it is not inserted into
// CharacterState itself).
// ---------------------------------------------------------------------
constexpr uint32_t kUnrecruitableBit1e02 = 0x10;
int stub_set_unrecruitable_flag(lua_State* L) {
    logCall(L, upLog(L), "set_unrecruitable_flag", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        auto& misc = st->w28CharacterMisc(name);
        misc.forceFlags1e02 = flag ? (misc.forceFlags1e02 | kUnrecruitableBit1e02)
                                    : (misc.forceFlags1e02 & ~kUnrecruitableBit1e02);
    } else {
        EngineState::replicateStateChange("set_unrecruitable_flag", name);
    }
    return 0;
}

// ---------------------------------------------------------------------
// player_override_set_sprint / _set_sprint_recharge / _set_sprint_delay
// (Sec40.1, "/_suffix" shorthand in the spec's own text). Each: 1 number,
// NO presence check (a missing/non-number argument stores literal 0, not
// "cleared" - CONFIRMED, lua_tonumber's own real nil-handling already
// gives 0 for a missing argument, so no special-case code is needed).
// GLOBAL (not per-player). Registered as session-synced variables in the
// real engine (mechanism 0x0086d770) and reset to -1 at mission start -
// this project has NO mission-start/restart hook anywhere (confirmed by
// search, same OPEN gap as every other mission-scoped variable in this
// codebase) so that reset cannot be wired; the CHOSEN real file-backed
// default (-1) is still seeded at construction via applySpecInitialState.
// ---------------------------------------------------------------------
int stub_player_override_set_sprint(lua_State* L) {
    logCall(L, upLog(L), "player_override_set_sprint", upStateTag(L));
    upState(L)->sprintUseOverride() = lua_tonumber(L, 1); // no presence check, CONFIRMED
    return 0;
}
int stub_player_override_set_sprint_recharge(lua_State* L) {
    logCall(L, upLog(L), "player_override_set_sprint_recharge", upStateTag(L));
    upState(L)->sprintRechargeOverride() = lua_tonumber(L, 1);
    return 0;
}
int stub_player_override_set_sprint_delay(lua_State* L) {
    logCall(L, upLog(L), "player_override_set_sprint_delay", upStateTag(L));
    upState(L)->sprintDelayOverride() = lua_tonumber(L, 1);
    return 0;
}
// player_override_clear_sprint / _clear_sprint_recharge / _clear_sprint_delay
// (Sec40.1): no arguments, write -1 (CONFIRMED).
int stub_player_override_clear_sprint(lua_State* L) {
    logCall(L, upLog(L), "player_override_clear_sprint", upStateTag(L));
    upState(L)->sprintUseOverride() = -1.0;
    return 0;
}
int stub_player_override_clear_sprint_recharge(lua_State* L) {
    logCall(L, upLog(L), "player_override_clear_sprint_recharge", upStateTag(L));
    upState(L)->sprintRechargeOverride() = -1.0;
    return 0;
}
int stub_player_override_clear_sprint_delay(lua_State* L) {
    logCall(L, upLog(L), "player_override_clear_sprint_delay", upStateTag(L));
    upState(L)->sprintDelayOverride() = -1.0;
    return 0;
}

// ---------------------------------------------------------------------
// players_in_weird_camera_mode (Sec40.4, 0x00a5a1b0). No arguments.
// Returns 1 boolean. CONFIRMED true if ANY of: a global byte CONFIRMED
// dead code (9-site exhaustive scan, zero writers anywhere in the shipped
// binary - modelled as the literal `false` below, no field needed); a
// second global ("RC Gun" flag, EngineState::rcGunActiveFlag() - written
// only by two functions explicitly OUTSIDE this batch's own 93 names, so
// within this batch it can only ever read false); or - only for the co-op
// PARTNER, never the local player - the partner is in one of two specific
// states (no co-op partner is modelled in this project's single-player
// scope, so this term is also always false here). In single player the
// answer is therefore always false (CONFIRMED structurally, not guessed).
// ---------------------------------------------------------------------
int stub_players_in_weird_camera_mode(lua_State* L) {
    logCall(L, upLog(L), "players_in_weird_camera_mode", upStateTag(L));
    EngineState* st = upState(L);
    bool deadGlobal = false; // CONFIRMED permanently dead in the shipped binary, Sec40.4
    bool rcGun = st->rcGunActiveFlag();
    bool partnerWeird = false; // OPEN: no co-op partner state model in this project's scope
    lua_pushboolean(L, (deadGlobal || rcGun || partnerWeird) ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// npc_brute_always_throws (Sec42.2, 0x00a55190). Character + optional
// boolean, default true. No return. CONFIRMED double-gate setter,
// silently a no-op for a character failing an "is a brute" test
// (HYPOTHESIS, not individually traced) - this project has no brute-kind
// predicate to test, so every resolved character takes the write branch
// (CHOSEN, the simplification this batch's own 93-name scope requires).
// ---------------------------------------------------------------------
int stub_npc_brute_always_throws(lua_State* L) {
    logCall(L, upLog(L), "npc_brute_always_throws", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.forceFlagGatePasses.get()) {
        st->w28CharacterMisc(name).bruteAlwaysThrows = flag;
    } else {
        EngineState::replicateStateChange("npc_brute_always_throws", name);
    }
    return 0;
}

// ---------------------------------------------------------------------
// npc_detection_enable (Sec42.2, 0x00a55300). Character + bare boolean
// (default false). No return. CONFIRMED double-gate setter, silently a
// no-op while a specific global byte is set (meaning/writer OPEN - not in
// this batch's own 93 names, so that global can never be set here;
// CHOSEN: always takes the write branch).
// ---------------------------------------------------------------------
int stub_npc_detection_enable(lua_State* L) {
    logCall(L, upLog(L), "npc_detection_enable", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, false); // CONFIRMED bare default false
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.forceFlagGatePasses.get()) {
        st->w28CharacterMisc(name).npcDetectionEnabled = flag;
    } else {
        EngineState::replicateStateChange("npc_detection_enable", name);
    }
    return 0;
}

// ---------------------------------------------------------------------
// npc_weapon_pickup_override (Sec42.2, 0x00a557d0) - inverted boolean.
// Character + bare boolean (default false). No return. CONFIRMED sets a
// "can't pick up weapons" AI force-flag to the logical NOT of the Lua
// argument. This project stores the RAW argument only (no getter native
// for this bit exists in this batch's own 93 names, so the inversion has
// no in-scope reader to matter for) - see W28CharacterMisc's own doc
// comment.
// ---------------------------------------------------------------------
int stub_npc_weapon_pickup_override(lua_State* L) {
    logCall(L, upLog(L), "npc_weapon_pickup_override", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, false); // CONFIRMED bare default false
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.forceFlagGatePasses.get()) {
        st->w28CharacterMisc(name).npcWeaponPickupOverrideArg = flag;
    } else {
        EngineState::replicateStateChange("npc_weapon_pickup_override", name);
    }
    return 0;
}

// ---------------------------------------------------------------------
// override_npc_run_and_cower_run (Sec42.2, 0x00a58ca0). Character
// (resolved directly, no "#PLAYER#"/"#FOLLOWER#" sentinels accepted) + 2
// animation-set names. No return, NO replication at all (CONFIRMED).
// Writes two animation-index fields directly (nil or unknown name stores
// -1, "no override") - this project has no name->index animation-set
// table (OPEN), so EVERY name is treated as "unknown" here (CHOSEN,
// genuinely no data, same reasoning as melee_get_move_id_from_name
// above) - both fields are always written as -1, which IS the CONFIRMED
// real value for this exact "name doesn't resolve" case.
// ---------------------------------------------------------------------
int stub_override_npc_run_and_cower_run(lua_State* L) {
    logCall(L, upLog(L), "override_npc_run_and_cower_run", upStateTag(L));
    std::string name = argString(L, 1);
    (void)argString(L, 2);
    (void)argString(L, 3);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    auto& misc = st->w28CharacterMisc(name);
    misc.runAnimOverride = -1;      // CHOSEN: no animation-set table in scope, see doc comment
    misc.cowerRunAnimOverride = -1;
    return 0;
}

// ---------------------------------------------------------------------
// party_use_melee_weapons_only (Sec42.2, 0x00a59490). 1 boolean. No
// return. CONFIRMED applies to the local player AND, separately, the
// co-op partner if one exists - not to any other party member.
// ---------------------------------------------------------------------
int stub_party_use_melee_weapons_only(lua_State* L) {
    logCall(L, upLog(L), "party_use_melee_weapons_only", upStateTag(L));
    bool flag = lua_toboolean(L, 1) != 0; // mandatory, no nil-gate (CONFIRMED "1 boolean")
    EngineState* st = upState(L);
    st->partyMeleeOnlyLocal() = flag;
    if (st->playerRig().coopPlayerPresent.known() && st->playerRig().coopPlayerPresent.get()) {
        st->partyMeleeOnlyCoop() = flag;
    }
    return 0;
}

// ---------------------------------------------------------------------
// get_char_skydiving_state (Sec41.5, 0x00a4adc0). 1 character name.
// Returns (state, -1) if resolved, else (-1) alone (CONFIRMED). CHOSEN
// CONFIRMED sentinel 255 ("found, not currently skydiving") for every
// resolved character - this project has no 9-value sub-state model
// (OPEN, no data), so no resolved character is ever reported as
// skydiving; a test can still override via W28CharacterMisc.
// ---------------------------------------------------------------------
int stub_get_char_skydiving_state(lua_State* L) {
    logCall(L, upLog(L), "get_char_skydiving_state", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) { // OPEN until set
        lua_pushnumber(L, -1.0);
        return 1;
    }
    int32_t state = st->w28CharacterMisc(name).skydivingState;
    lua_pushnumber(L, static_cast<lua_Number>(state));
    lua_pushnumber(L, -1.0);
    return 2;
}

// ---------------------------------------------------------------------
// human_has_taken_non_melee_damage_from_player (Sec41.5, 0x00a4d2e0). 1
// character name. Returns 1 boolean: one bit of a character dword, false
// on a failed lookup (CONFIRMED). Who sets the bit is OPEN - no setter
// for it exists in this batch's own 93 names, so it can only ever be
// false unless a test sets it directly via W28CharacterMisc.
// ---------------------------------------------------------------------
int stub_human_has_taken_non_melee_damage_from_player(lua_State* L) {
    logCall(L, upLog(L), "human_has_taken_non_melee_damage_from_player", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool flag = st->objectResolves().get(name) && st->w28CharacterMisc(name).tookNonMeleeDamageFromPlayer; // OPEN until resolver set
    lua_pushboolean(L, flag ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// inv_item_add_ammo (Sec41.3, 0x00a50e40). Character name + item name +
// amount (truncated, no checks). No return. CONFIRMED: if the character
// is not locally owned, forwards via network message with the amount
// truncated to 16 bits - this project's whole scope is single-player
// local-only (no remote character model), so every resolved character is
// treated as locally owned (CHOSEN, consistent with this project's
// established "local-only" convention project-wide).
// ---------------------------------------------------------------------
int stub_inv_item_add_ammo(lua_State* L) {
    logCall(L, upLog(L), "inv_item_add_ammo", upStateTag(L));
    std::string character = argString(L, 1);
    std::string item = argString(L, 2);
    int64_t amount = truncateEa2596(lua_tonumber(L, 3)); // no checks, CONFIRMED
    EngineState* st = upState(L);
    if (st->objectResolves().get(character)) { // OPEN until set
        st->inventoryAmmo()[character][item] += amount;
    }
    return 0;
}

// ---------------------------------------------------------------------
// inv_item_add_do (Sec41.3, 0x00a51dc0). Item name + 3 optional args
// (count default 1; character name default "#PLAYER1#"; boolean default
// false). No return. CONFIRMED: a non-weapon item-table match silently
// ignores the count argument entirely (always adds exactly one) - this
// project has no weapon/item-table distinction (OPEN, no data), so CHOSEN
// to always add the full count argument (the simpler, more informative
// stand-in; the "always exactly one" branch is not modelled since this
// project cannot tell which table a name would have matched).
// ---------------------------------------------------------------------
int stub_inv_item_add_do(lua_State* L) {
    logCall(L, upLog(L), "inv_item_add_do", upStateTag(L));
    std::string item = argString(L, 1);
    int64_t count = truncateEa2596(optionalNumberDefault(L, 2, 1.0));
    std::string character = optionalStringDefault(L, 3, "#PLAYER1#");
    (void)optionalBoolDefault(L, 4, false);
    EngineState* st = upState(L);
    if (st->objectResolves().get(character)) { // OPEN until set
        st->inventoryItemCounts()[character][item] += count;
    }
    return 0;
}

// ---------------------------------------------------------------------
// inv_item_get_weapon_upgrades (Sec41.3, 0x00a51000) CRASH - unchecked
// index, flagged as a real crash risk. Item name + optional character
// name (default "#PLAYER1#"). Returns (level, -1) if found, else (-1)
// alone (CONFIRMED, the trailing -1 sentinel is pushed on BOTH paths).
// The real crash is an unchecked "player slot index" array dereference -
// this project's flat getOrCreateCharacter()-based model has no "player
// slot index" concept at all, so there is no unchecked index here to
// harden (trivially satisfied, not a guessed fix); a settable per-item
// map stands in for the actual upgrade level.
// ---------------------------------------------------------------------
int stub_inv_item_get_weapon_upgrades(lua_State* L) {
    logCall(L, upLog(L), "inv_item_get_weapon_upgrades", upStateTag(L));
    std::string item = argString(L, 1);
    (void)optionalStringDefault(L, 2, "#PLAYER1#");
    EngineState* st = upState(L);
    auto it = st->weaponUpgradeLevels().find(item);
    if (it != st->weaponUpgradeLevels().end()) {
        lua_pushnumber(L, static_cast<lua_Number>(it->second));
        lua_pushnumber(L, -1.0);
        return 2;
    }
    lua_pushnumber(L, -1.0);
    return 1;
}

// ---------------------------------------------------------------------
// character_set_cannot_exit_rc_vehicle (Sec45.1, 0x00a42140). Standard
// double-gate record-and-replicate setter (Sec7.2 shape): writes bit 9 of
// character +0x1c9c locally only when both gates are false and no remote
// owner exists; otherwise sends an opcode-0x46 record. CHOSEN: reuses the
// EXISTING CharacterState::flags1c9c field (not forceFlags1c9c - see this
// batch's own engine_state.h top comment for the pre-existing dual-field
// situation at this address; flags1c9c already models the SIBLING bit 8
// "cannot_exit_vehicles," cross-referenced by the SAME real reader
// 0x00b0b170 this entry's own text cites, so it is the better-evidenced
// of the two fields for a new bit at this address).
// ---------------------------------------------------------------------
constexpr uint32_t kCannotExitRcVehicleBit1c9c = 0x200; // bit 9
int stub_character_set_cannot_exit_rc_vehicle(lua_State* L) {
    logCall(L, upLog(L), "character_set_cannot_exit_rc_vehicle", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.flags1c9c.setBits(kCannotExitRcVehicleBit1c9c, flag ? kCannotExitRcVehicleBit1c9c : 0u);
    } else {
        EngineState::replicateStateChange("character_set_cannot_exit_rc_vehicle", name);
    }
    return 0;
}

// ---------------------------------------------------------------------
// ai_force_team_idle (Sec46.3) CRASH. 1 team-name string. No return.
// CONFIRMED real null-read crash (nil/non-string name hashed through a
// null pointer with no check) HARDENED: a non-string argument is refused
// (no-op, counted) instead. CONFIRMED real scope-widening LOGIC defect
// (an unknown/misspelled team name resolves to the sentinel team id 9,
// which every downstream loop treats as "match every team") is NOT
// hardened away - it is REPLICATED exactly, reusing the existing
// resolveTeamId() resolver, since it is a documented behavior, not a
// memory-safety bug.
// ---------------------------------------------------------------------
int stub_ai_force_team_idle(lua_State* L) {
    logCall(L, upLog(L), "ai_force_team_idle", upStateTag(L));
    EngineState* st = upState(L);
    if (lua_type(L, 1) != LUA_TSTRING) { // HARDENED (CHOSEN divergence): avoid the real null-hash read
        st->recordAiForceTeamIdleNullNameGuard();
        return 0;
    }
    std::string name = argString(L, 1);
    int id = st->resolveTeamId(name);
    auto& idle = st->teamIdleFlags();
    if (id == 9) { // CONFIRMED: sentinel -> "match every team" (scope-widening defect, replicated as-is)
        for (auto& b : idle) b = true;
    } else if (id >= 0 && id < static_cast<int>(idle.size())) {
        idle[static_cast<size_t>(id)] = true;
    }
    return 0;
}

// ---------------------------------------------------------------------
// notoriety_get (Sec50.5, tranche 18) CRASH - real crash path, outcome
// depends on the CRT handler. 1 track-name string. CONFIRMED: an unknown
// or misspelled track name returns ZERO Lua values, not nil or zero.
// Reuses the EXISTING lookupNotorietyTrackCode()/
// kNotorietyTrackCodeNotFound resolver directly. HARDENED (CHOSEN
// divergence): the real engine passes a non-string argument straight into
// the CRT's __stricmp with no null check (can terminate the process) -
// this project folds a non-string argument into the SAME "unknown track"
// (zero values) path a misspelt-but-string name already takes, counted.
// ---------------------------------------------------------------------
int stub_notoriety_get(lua_State* L) {
    logCall(L, upLog(L), "notoriety_get", upStateTag(L));
    EngineState* st = upState(L);
    if (lua_type(L, 1) != LUA_TSTRING) { // HARDENED (CHOSEN divergence): avoid the real CRT null-arg crash
        st->recordNotorietyGetNonStringArgGuard();
        return 0; // CONFIRMED shape for "unknown track": zero Lua values
    }
    std::string track = argString(L, 1);
    int32_t code = st->lookupNotorietyTrackCode(track);
    if (code == EngineState::kNotorietyTrackCodeNotFound) {
        return 0; // CONFIRMED: zero Lua values, not nil/0
    }
    auto it = st->notorietyTrackValues().find(code);
    lua_pushnumber(L, it != st->notorietyTrackValues().end() ? it->second : 0.0);
    return 1;
}

// ---------------------------------------------------------------------
// set_attack_peds_flag (Sec49.3, tranche 16). CONFIRMED a double-gate
// AI-flag setter; the no-owner case does nothing locally rather than
// writing, when neither gate passes and no owner exists (refining
// tranche 13's own description of this setter family). No specific byte/
// bit offset is given in this spec text for THIS setter (unlike the
// force-flag family above) - stored in W28CharacterMisc::attackPedsFlag
// as a plain bool (CHOSEN, no real bit location known).
// ---------------------------------------------------------------------
int stub_set_attack_peds_flag(lua_State* L) {
    logCall(L, upLog(L), "set_attack_peds_flag", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.forceFlagGatePasses.get()) {
        st->w28CharacterMisc(name).attackPedsFlag = flag;
    } else {
        EngineState::replicateStateChange("set_attack_peds_flag", name);
    }
    return 0;
}

// ---------------------------------------------------------------------
// perform_killswitch (Sec51.4, tranche 17) CRASH, narrow trigger. 1
// victim name + 1 killer name. CONFIRMED the victim argument is used only
// for a type check; the real QTE always targets whatever live object is
// currently registered under the LITERAL script name "M21_Killbane" -
// reuses objectResolves() with that literal name directly. HARDENED
// (CHOSEN divergence): the real engine zeroes the lookup result and makes
// a virtual call through it when no such object exists; this project
// refuses (no-op) instead, counted.
// ---------------------------------------------------------------------
int stub_perform_killswitch(lua_State* L) {
    logCall(L, upLog(L), "perform_killswitch", upStateTag(L));
    (void)argString(L, 1); // victim - CONFIRMED used only for a type check, not the real target
    (void)argString(L, 2); // killer
    EngineState* st = upState(L);
    if (!st->objectResolves().get("M21_Killbane")) { // OPEN until set
        st->recordPerformKillswitchMissingKillbaneGuard();
        return 0;
    }
    return 0;
}

// ---------------------------------------------------------------------
// player_action_is_pressed (Sec51.4, tranche 17). CONFIRMED only
// "resolved in full, no further crash shapes (...a bounds-safe 166-row
// action-name lookup...)" (aggregate text, no per-action press-state
// system in this project). A settable per-action map stands in, CHOSEN
// false ("not pressed") for any action never set by a test.
// ---------------------------------------------------------------------
int stub_player_action_is_pressed(lua_State* L) {
    logCall(L, upLog(L), "player_action_is_pressed", upStateTag(L));
    std::string action = argString(L, 1);
    EngineState* st = upState(L);
    auto it = st->actionPressedFlags().find(action);
    lua_pushboolean(L, (it != st->actionPressedFlags().end() && it->second) ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// player_get_custom_voice (Sec51.4, tranche 17) CRASH. 1 player name.
// CONFIRMED the resolver's result is never checked before being
// dereferenced. HARDENED (CHOSEN divergence): refused (returns the
// CHOSEN "no value" sentinel -1, this project's own pervasive "no value"
// convention) on an unresolved player name, counted; a resolved name
// reads a settable per-player voice id, CHOSEN -1 default.
// ---------------------------------------------------------------------
int stub_player_get_custom_voice(lua_State* L) {
    logCall(L, upLog(L), "player_get_custom_voice", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) { // HARDENED (CHOSEN divergence): avoid the real null dereference
        st->recordPlayerGetCustomVoiceUnresolvedGuard();
        lua_pushnumber(L, -1.0);
        return 1;
    }
    auto it = st->customVoiceIds().find(name);
    lua_pushnumber(L, static_cast<lua_Number>(it != st->customVoiceIds().end() ? it->second : -1));
    return 1;
}

// ---------------------------------------------------------------------
// player_take_human_shield_do (Sec51.4, tranche 17). CONFIRMED two
// branches "return success optimistically before anything has actually
// happened" whenever the taker or victim belongs to a remote machine
// (this project's whole scope is single-player local-only, so that branch
// never applies here - every call takes the "local" path). "Force"
// argument defaults to true, meaning an out-of-range victim is grabbed
// anyway unless a script explicitly opts out (this project has no range
// model, so with force=true the grab always proceeds on a resolved pair;
// with force=false and either name unresolved, it refuses). Reuses the
// EXISTING CharacterState::humanShieldHostageName field directly.
// ---------------------------------------------------------------------
int stub_player_take_human_shield_do(lua_State* L) {
    logCall(L, upLog(L), "player_take_human_shield_do", upStateTag(L));
    std::string taker = argString(L, 1);
    std::string victim = argString(L, 2);
    bool force = optionalBoolDefault(L, 3, true); // CONFIRMED default true
    EngineState* st = upState(L);
    bool takerResolved = st->objectResolves().get(taker);
    bool victimResolved = st->objectResolves().get(victim);
    if (!takerResolved) { lua_pushboolean(L, 0); return 1; }
    if (!victimResolved && !force) { lua_pushboolean(L, 0); return 1; }
    st->getOrCreateCharacter(taker).humanShieldHostageName = victim;
    lua_pushboolean(L, 1); // CONFIRMED: optimistic success shape
    return 1;
}

// ---------------------------------------------------------------------
// npc_aim_at_point (Sec50/T18) NOOP - confirmed the project's shared
// no-op stub (0 return values). CHOSEN: reimplemented here as its own
// genuine no-op function (rather than cross-TU-aliasing the existing
// shared-stub call site, which has internal linkage in its own
// translation unit) - observably identical (0 return values, no side
// effect), registered with the exact documented shape.
// ---------------------------------------------------------------------
int stub_npc_aim_at_point(lua_State* L) {
    logCall(L, upLog(L), "npc_aim_at_point", upStateTag(L));
    return 0;
}

// ---------------------------------------------------------------------
// npc_brute_set_no_downed_finisher (Sec50.5, tranche 18). CONFIRMED only
// "straightforward state queries/setters, no further crash shapes"
// (aggregate text). CHOSEN argument shape: character + optional boolean
// default true (this project's own majority convention).
// ---------------------------------------------------------------------
int stub_npc_brute_set_no_downed_finisher(lua_State* L) {
    logCall(L, upLog(L), "npc_brute_set_no_downed_finisher", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->w28CharacterMisc(name).bruteNoDownedFinisher = flag;
    }
    return 0;
}

// ---------------------------------------------------------------------
// npc_dont_auto_equip_or_unequip_weapons (Sec50.5, tranche 18). CONFIRMED
// its own boolean argument defaults to false on omission (an unconditional
// read) - the one exception to this family's usual default-true idiom.
// ---------------------------------------------------------------------
int stub_npc_dont_auto_equip_or_unequip_weapons(lua_State* L) {
    logCall(L, upLog(L), "npc_dont_auto_equip_or_unequip_weapons", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0; // CONFIRMED unconditional read, default false
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->w28CharacterMisc(name).dontAutoEquipUnequipWeapons = flag;
    }
    return 0;
}

// ---------------------------------------------------------------------
// npc_is_in_brute_grab_qte (Sec50.5, tranche 18). CONFIRMED only
// "straightforward state queries/setters, no further crash shapes."
// ---------------------------------------------------------------------
int stub_npc_is_in_brute_grab_qte(lua_State* L) {
    logCall(L, upLog(L), "npc_is_in_brute_grab_qte", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool flag = st->objectResolves().get(name) && st->w28CharacterMisc(name).inBruteGrabQte; // OPEN until resolver set
    lua_pushboolean(L, flag ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// party_dismiss_do (Sec50.5, tranche 18). Takes a Lua table of character
// names (NOT individual arguments) - CONFIRMED "passing anything other
// than a table... raises a Lua error inside the binding itself." Each
// resolved non-player character is removed from its squad via the SAME
// "remove from crew" primitive tranche 16 D.1 already names
// (EngineState::squadMember(name).inRoster, already implemented for
// squad_enable - reused directly, not re-derived).
// ---------------------------------------------------------------------
int stub_party_dismiss_do(lua_State* L) {
    logCall(L, upLog(L), "party_dismiss_do", upStateTag(L));
    if (lua_type(L, 1) != LUA_TTABLE) {
        throw PendingLuaError{"party_dismiss_do: argument 1 must be a table of character names"};
    }
    std::vector<std::string> names = readCharacterNameArray(L, 1);
    EngineState* st = upState(L);
    for (const auto& name : names) {
        if (!st->objectResolves().get(name)) continue; // OPEN until set
        if (st->playerClassChainResolves(name)) continue; // CHOSEN default now applies when no test .set() it - CONFIRMED: non-player characters only
        auto& member = st->squadMember(name);
        member.inRoster = false;
        ++member.dismissCount;
    }
    return 0;
}

// ---------------------------------------------------------------------
// human_being_revived (Sec54.3, tranche 19). CONFIRMED "its own confirmed
// 'unknown input' sentinel value... a flag bit." Returns 1 boolean.
// ---------------------------------------------------------------------
int stub_human_being_revived(lua_State* L) {
    logCall(L, upLog(L), "human_being_revived", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    auto it = st->revivedFlags().find(name);
    lua_pushboolean(L, (it != st->revivedFlags().end() && it->second) ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// human_being_revived_by (Sec54.3, tranche 19) - a real session-dependent
// return-value difference. CONFIRMED single-player literal "#NETID#0"
// when revived by the local player (single player never has a session,
// Sec8.27/Sec26.28); nil indistinguishably for "never revived" or
// "revived by a non-player NPC" (this project cannot distinguish the
// latter without an NPC-reviver-identity model, OPEN, folded into the
// same nil path).
// ---------------------------------------------------------------------
int stub_human_being_revived_by(lua_State* L) {
    logCall(L, upLog(L), "human_being_revived_by", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    auto it = st->revivedByLocalPlayerFlags().find(name);
    if (it != st->revivedByLocalPlayerFlags().end() && it->second) {
        lua_pushstring(L, "#NETID#0"); // CONFIRMED single-player literal
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

// ---------------------------------------------------------------------
// human_downed_set_mars_kb_interactable (Sec54.3, tranche 19). CONFIRMED
// "no repeat-call guard: calling it twice on the same still-downed
// character creates a second on-screen indicator and overwrites the
// stored id of the first one, leaking it." CHOSEN argument shape:
// character + optional numeric id, default 0 (no further shape detail
// given). The leak is made test-observable via a named counter that
// increments whenever an ALREADY-set id is overwritten.
// ---------------------------------------------------------------------
int stub_human_downed_set_mars_kb_interactable(lua_State* L) {
    logCall(L, upLog(L), "human_downed_set_mars_kb_interactable", upStateTag(L));
    std::string name = argString(L, 1);
    int64_t id = truncateEa2596(optionalNumberDefault(L, 2, 0.0));
    EngineState* st = upState(L);
    auto& ids = st->marsKbInteractableId();
    if (ids.count(name) != 0) {
        st->recordMarsKbInteractableLeak(); // CONFIRMED: no repeat-call guard, leaks the first indicator
    }
    ids[name] = id;
    return 0;
}

// ---------------------------------------------------------------------
// inv_item_dual_wield (Sec54.3, tranche 19). CONFIRMED "sets its flag
// locally only with no way to clear it through the binding" (this
// project's single-player scope has no remote-owner forwarding step
// either way). CHOSEN argument shape: character + optional boolean,
// default true (a "clear" call would need false explicitly, matching the
// spec's own "no way to clear" framing loosely - a script can still pass
// false, this project just never auto-clears it).
// ---------------------------------------------------------------------
int stub_inv_item_dual_wield(lua_State* L) {
    logCall(L, upLog(L), "inv_item_dual_wield", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->dualWieldFlags()[name] = flag;
    }
    return 0;
}

// ---------------------------------------------------------------------
// inv_item_get_anim_group (Sec54.3, tranche 19). CONFIRMED "unknown
// input" sentinel "Default". Returns 1 string.
// ---------------------------------------------------------------------
int stub_inv_item_get_anim_group(lua_State* L) {
    logCall(L, upLog(L), "inv_item_get_anim_group", upStateTag(L));
    std::string item = argString(L, 1);
    EngineState* st = upState(L);
    auto it = st->itemAnimGroups().find(item);
    lua_pushstring(L, (it != st->itemAnimGroups().end() ? it->second : std::string("Default")).c_str());
    return 1;
}

// ---------------------------------------------------------------------
// inv_item_get_weapon_slot (Sec54.3, tranche 19). CONFIRMED "unknown
// input" sentinel 255. Returns 1 number.
// ---------------------------------------------------------------------
int stub_inv_item_get_weapon_slot(lua_State* L) {
    logCall(L, upLog(L), "inv_item_get_weapon_slot", upStateTag(L));
    std::string item = argString(L, 1);
    EngineState* st = upState(L);
    auto it = st->itemWeaponSlots().find(item);
    lua_pushnumber(L, static_cast<lua_Number>(it != st->itemWeaponSlots().end() ? it->second : 255));
    return 1;
}

// ---------------------------------------------------------------------
// follower_is_unconscious (Sec55.5, tranche 22) - naming trap, opposite
// polarity from human_is_downed. CONFIRMED tests the SAME "downed" state
// field human_is_downed reads, but through a resolver that REJECTS dead
// objects - so a dead or misspelled-name follower reads as unconscious
// (true), the OPPOSITE of human_is_downed's false for an unresolved name.
// Reuses the EXISTING CharacterState::lifeState field (6=downed).
// ---------------------------------------------------------------------
int stub_follower_is_unconscious(lua_State* L) {
    logCall(L, upLog(L), "follower_is_unconscious", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) { // OPEN until set; CONFIRMED unresolved -> true (opposite of human_is_downed)
        lua_pushboolean(L, 1);
        return 1;
    }
    CharacterState& c = st->getOrCreateCharacter(name);
    lua_pushboolean(L, (c.lifeState.get() == 6) ? 1 : 0); // 6 = downed (CONFIRMED, lifeState's own doc comment)
    return 1;
}

// ---------------------------------------------------------------------
// character_check_resource_loaded (Sec57, AGG). NO individuated spec text
// exists anywhere in spec-lua-api-behaviour.md for this name - only the
// aggregate "Characters - 10 functions (gameplay registrar)... resolved
// in full, no further crash shapes" framing at Sec57.5, which gives zero
// argument/return/side-effect detail (confirmed by an exhaustive search:
// the only other appearances of this exact name anywhere in this
// project's own files are plain roster listings with no behavior).
// Per this project's standing "refuse honestly rather than guess"
// discipline, this is registered (so it is a real global, not a generic
// stub) but throws OpenStateError on its own actual behavior rather than
// fabricate one.
// ---------------------------------------------------------------------
int stub_character_check_resource_loaded(lua_State* L) {
    logCall(L, upLog(L), "character_check_resource_loaded", upStateTag(L));
    throw OpenStateError("character_check_resource_loaded's own argument/return/side-effect shape",
                         "spec-lua-api-behaviour.md Sec57.5 (aggregate-only, AGG)");
}

// ---------------------------------------------------------------------
// character_is_combat_ready (Sec57.5, tranche 23). CONFIRMED sentinel:
// "returns no value (nil), not false, for an unresolved character name."
// CHOSEN: pushes a literal nil (this project's own majority phrasing for
// "returns nil" elsewhere) rather than 0 Lua values. A resolved character
// reads a settable combat-readiness flag, CHOSEN false default (no real
// combat-readiness model is given).
// ---------------------------------------------------------------------
int stub_character_is_combat_ready(lua_State* L) {
    logCall(L, upLog(L), "character_is_combat_ready", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) { // OPEN until set
        lua_pushnil(L); // CONFIRMED: "no value (nil)", not false
        return 1;
    }
    lua_pushboolean(L, st->w28CharacterMisc(name).combatReady ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// character_is_on_fire (Sec57, AGG). Same "no individuated spec text
// anywhere" situation as character_check_resource_loaded above - see that
// stub's own doc comment for the full reasoning.
// ---------------------------------------------------------------------
int stub_character_is_on_fire(lua_State* L) {
    logCall(L, upLog(L), "character_is_on_fire", upStateTag(L));
    throw OpenStateError("character_is_on_fire's own argument/return/side-effect shape",
                         "spec-lua-api-behaviour.md Sec57.5 (aggregate-only, AGG)");
}

// ---------------------------------------------------------------------
// character_parachute_open (Sec57.5, tranche 23) - real resource-leak
// logic defect. CONFIRMED the forced-open path never tests whether the
// character already has a parachute attached before spawning another one
// - every call spawns a new parachute object with no reuse/cleanup of a
// prior one. Reproduced via a monotonic spawn counter (never
// decremented), which IS the CONFIRMED real behavior, not a hardening
// divergence.
// ---------------------------------------------------------------------
int stub_character_parachute_open(lua_State* L) {
    logCall(L, upLog(L), "character_parachute_open", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->recordParachuteOpenSpawn(); // CONFIRMED: every call spawns anew, never reused
    }
    return 0;
}

// ---------------------------------------------------------------------
// character_parachute_play_action (Sec57, AGG). Same "no individuated
// spec text anywhere" situation as character_check_resource_loaded above.
// ---------------------------------------------------------------------
int stub_character_parachute_play_action(lua_State* L) {
    logCall(L, upLog(L), "character_parachute_play_action", upStateTag(L));
    throw OpenStateError("character_parachute_play_action's own argument/return/side-effect shape",
                         "spec-lua-api-behaviour.md Sec57.5 (aggregate-only, AGG)");
}

// ---------------------------------------------------------------------
// character_prevent_kneecapping (Sec57.5, tranche 23). CONFIRMED extends
// the human_force_flags double-gate family, bit 3 of character +0x1c98
// (EXISTING CharacterState::forceFlags1c98 field). CONFIRMED "clears
// immunity when its boolean argument is simply omitted (not left
// unchanged)" - default false, unconditional-read shape.
// ---------------------------------------------------------------------
constexpr uint32_t kPreventKneecappingBit1c98 = 0x8; // bit 3
int stub_character_prevent_kneecapping(lua_State* L) {
    logCall(L, upLog(L), "character_prevent_kneecapping", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, false); // CONFIRMED: omitted -> clears (false)
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.forceFlagGatePasses.get()) {
        c.forceFlags1c98.setBits(kPreventKneecappingBit1c98, flag ? kPreventKneecappingBit1c98 : 0u);
    } else {
        EngineState::replicateStateChange("character_prevent_kneecapping", name);
    }
    return 0;
}

// ---------------------------------------------------------------------
// character_set_over_the_shoulder (Sec57.5, tranche 23). CONFIRMED
// extends the human_force_flags double-gate family, bit 22 of character
// +0x1c98 (EXISTING CharacterState::forceFlags1c98 field). CONFIRMED
// "defaults to on."
// ---------------------------------------------------------------------
constexpr uint32_t kOverTheShoulderBit1c98 = 0x400000; // bit 22
int stub_character_set_over_the_shoulder(lua_State* L) {
    logCall(L, upLog(L), "character_set_over_the_shoulder", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, true); // CONFIRMED default on
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.forceFlagGatePasses.get()) {
        c.forceFlags1c98.setBits(kOverTheShoulderBit1c98, flag ? kOverTheShoulderBit1c98 : 0u);
    } else {
        EngineState::replicateStateChange("character_set_over_the_shoulder", name);
    }
    return 0;
}

// ---------------------------------------------------------------------
// character_take_human_shield_do (Sec57.5, tranche 23). CONFIRMED "no
// range test, no self-target test, no 'already busy' test, unlike its
// already-documented player-targeting sibling (player_take_human_shield_do
// above) - a character can be made to take itself hostage with no
// distance check." Reuses the EXISTING CharacterState::
// humanShieldHostageName field directly.
// ---------------------------------------------------------------------
int stub_character_take_human_shield_do(lua_State* L) {
    logCall(L, upLog(L), "character_take_human_shield_do", upStateTag(L));
    std::string taker = argString(L, 1);
    std::string victim = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(taker)) return 0; // OPEN until set
    st->getOrCreateCharacter(taker).humanShieldHostageName = victim; // CONFIRMED: no range/self/busy test
    return 0;
}

// ---------------------------------------------------------------------
// character_use_synced_finishers (Sec57.5, tranche 23). CONFIRMED extends
// the human_force_flags double-gate family, bit 15 of character +0x1c9c.
// CHOSEN: reuses the EXISTING CharacterState::flags1c9c field (see
// character_set_cannot_exit_rc_vehicle's own doc comment above for the
// pre-existing dual-field-at-this-address reasoning this batch
// consistently resolves the same way).
// ---------------------------------------------------------------------
constexpr uint32_t kSyncedFinishersBit1c9c = 0x8000; // bit 15
int stub_character_use_synced_finishers(lua_State* L) {
    logCall(L, upLog(L), "character_use_synced_finishers", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.forceFlagGatePasses.get()) {
        c.flags1c9c.setBits(kSyncedFinishersBit1c9c, flag ? kSyncedFinishersBit1c9c : 0u);
    } else {
        EngineState::replicateStateChange("character_use_synced_finishers", name);
    }
    return 0;
}

// ---------------------------------------------------------------------
// action_play_custom_do (Sec58, tranche 25) CRASH - a null-character
// crash from the same template as an already-documented sibling, plus a
// return-value trap. CONFIRMED returns -1 (dead), 0 (play failed), 1
// (play "succeeded") - 0 is itself truthy in Lua (a documented trap).
// CONFIRMED a resolved, LIVE character always reaches 1 regardless of
// animation-name validity (the real slot player "claims a slot anyway"
// for an unknown name - this project has no animation-name table either
// way, so this is the documented real behavior, not a modelling
// shortfall). HARDENED (CHOSEN divergence): the real engine skips its own
// dead-check for an unresolved (null) character, reaching an unconditional
// null read; this project resolves the character FIRST (matching sibling
// action_play_synced_do's own argument order,
// lua_spec_confirmed_stubs_b03_vintui_r03_r05.cpp) and refuses with the
// CHOSEN closest documented failure value (0, "play failed" - the real
// crash has no defined return value at all), counted.
// ---------------------------------------------------------------------
int stub_action_play_custom_do(lua_State* L) {
    logCall(L, upLog(L), "action_play_custom_do", upStateTag(L));
    std::string character = argString(L, 1);
    (void)argString(L, 2); // animation name - no animation-name table in scope, see doc comment
    EngineState* st = upState(L);
    if (!st->objectResolves().get(character)) { // HARDENED (CHOSEN divergence): avoid the real null read
        st->recordActionPlayCustomDoUnresolvedGuard();
        lua_pushnumber(L, 0.0);
        return 1;
    }
    CharacterState& c = st->getOrCreateCharacter(character);
    if (c.lifeState.get() == 5) { // 5 = dead (CONFIRMED, lifeState's own doc comment)
        lua_pushnumber(L, -1.0);
        return 1;
    }
    lua_pushnumber(L, 1.0); // CONFIRMED: always "succeeds" for a live character, regardless of animation name
    return 1;
}

} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB19() {
    static const std::vector<std::string> names = {
        "group_is_loaded_internal",
        "group_show_do",
        "group_hide_do",
        "players_naked",
        "melee_get_move_id_from_name",
        "player_parachute_has_backpack",
        "set_cower_variant",
        "set_unjackable_by_ai_flag",
        "set_unrecruitable_flag",
        "player_override_set_sprint",
        "player_override_set_sprint_recharge",
        "player_override_set_sprint_delay",
        "player_override_clear_sprint",
        "player_override_clear_sprint_recharge",
        "player_override_clear_sprint_delay",
        "players_in_weird_camera_mode",
        "npc_brute_always_throws",
        "npc_detection_enable",
        "npc_weapon_pickup_override",
        "override_npc_run_and_cower_run",
        "party_use_melee_weapons_only",
        "get_char_skydiving_state",
        "human_has_taken_non_melee_damage_from_player",
        "inv_item_add_ammo",
        "inv_item_add_do",
        "inv_item_get_weapon_upgrades",
        "character_set_cannot_exit_rc_vehicle",
        "ai_force_team_idle",
        "notoriety_get",
        "set_attack_peds_flag",
        "perform_killswitch",
        "player_action_is_pressed",
        "player_get_custom_voice",
        "player_take_human_shield_do",
        "npc_aim_at_point",
        "npc_brute_set_no_downed_finisher",
        "npc_dont_auto_equip_or_unequip_weapons",
        "npc_is_in_brute_grab_qte",
        "party_dismiss_do",
        "human_being_revived",
        "human_being_revived_by",
        "human_downed_set_mars_kb_interactable",
        "inv_item_dual_wield",
        "inv_item_get_anim_group",
        "inv_item_get_weapon_slot",
        "follower_is_unconscious",
        "character_check_resource_loaded",
        "character_is_combat_ready",
        "character_is_on_fire",
        "character_parachute_open",
        "character_parachute_play_action",
        "character_prevent_kneecapping",
        "character_set_over_the_shoulder",
        "character_take_human_shield_do",
        "character_use_synced_finishers",
        "action_play_custom_do",
    };
    return names;
}

void registerB19(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    if (wants("group_is_loaded_internal")) registerOne(L, state, log, stateTag, "group_is_loaded_internal", openGuard<stub_group_is_loaded_internal>);
    if (wants("group_show_do")) registerOne(L, state, log, stateTag, "group_show_do", openGuard<stub_group_show_do>);
    if (wants("group_hide_do")) registerOne(L, state, log, stateTag, "group_hide_do", openGuard<stub_group_hide_do>);
    if (wants("players_naked")) registerOne(L, state, log, stateTag, "players_naked", openGuard<stub_players_naked>);
    if (wants("melee_get_move_id_from_name")) registerOne(L, state, log, stateTag, "melee_get_move_id_from_name", openGuard<stub_melee_get_move_id_from_name>);
    if (wants("player_parachute_has_backpack")) registerOne(L, state, log, stateTag, "player_parachute_has_backpack", openGuard<stub_player_parachute_has_backpack>);
    if (wants("set_cower_variant")) registerOne(L, state, log, stateTag, "set_cower_variant", openGuard<stub_set_cower_variant>);
    if (wants("set_unjackable_by_ai_flag")) registerOne(L, state, log, stateTag, "set_unjackable_by_ai_flag", openGuard<stub_set_unjackable_by_ai_flag>);
    if (wants("set_unrecruitable_flag")) registerOne(L, state, log, stateTag, "set_unrecruitable_flag", openGuard<stub_set_unrecruitable_flag>);
    if (wants("player_override_set_sprint")) registerOne(L, state, log, stateTag, "player_override_set_sprint", openGuard<stub_player_override_set_sprint>);
    if (wants("player_override_set_sprint_recharge")) registerOne(L, state, log, stateTag, "player_override_set_sprint_recharge", openGuard<stub_player_override_set_sprint_recharge>);
    if (wants("player_override_set_sprint_delay")) registerOne(L, state, log, stateTag, "player_override_set_sprint_delay", openGuard<stub_player_override_set_sprint_delay>);
    if (wants("player_override_clear_sprint")) registerOne(L, state, log, stateTag, "player_override_clear_sprint", openGuard<stub_player_override_clear_sprint>);
    if (wants("player_override_clear_sprint_recharge")) registerOne(L, state, log, stateTag, "player_override_clear_sprint_recharge", openGuard<stub_player_override_clear_sprint_recharge>);
    if (wants("player_override_clear_sprint_delay")) registerOne(L, state, log, stateTag, "player_override_clear_sprint_delay", openGuard<stub_player_override_clear_sprint_delay>);
    if (wants("players_in_weird_camera_mode")) registerOne(L, state, log, stateTag, "players_in_weird_camera_mode", openGuard<stub_players_in_weird_camera_mode>);
    if (wants("npc_brute_always_throws")) registerOne(L, state, log, stateTag, "npc_brute_always_throws", openGuard<stub_npc_brute_always_throws>);
    if (wants("npc_detection_enable")) registerOne(L, state, log, stateTag, "npc_detection_enable", openGuard<stub_npc_detection_enable>);
    if (wants("npc_weapon_pickup_override")) registerOne(L, state, log, stateTag, "npc_weapon_pickup_override", openGuard<stub_npc_weapon_pickup_override>);
    if (wants("override_npc_run_and_cower_run")) registerOne(L, state, log, stateTag, "override_npc_run_and_cower_run", openGuard<stub_override_npc_run_and_cower_run>);
    if (wants("party_use_melee_weapons_only")) registerOne(L, state, log, stateTag, "party_use_melee_weapons_only", openGuard<stub_party_use_melee_weapons_only>);
    if (wants("get_char_skydiving_state")) registerOne(L, state, log, stateTag, "get_char_skydiving_state", openGuard<stub_get_char_skydiving_state>);
    if (wants("human_has_taken_non_melee_damage_from_player")) registerOne(L, state, log, stateTag, "human_has_taken_non_melee_damage_from_player", openGuard<stub_human_has_taken_non_melee_damage_from_player>);
    if (wants("inv_item_add_ammo")) registerOne(L, state, log, stateTag, "inv_item_add_ammo", openGuard<stub_inv_item_add_ammo>);
    if (wants("inv_item_add_do")) registerOne(L, state, log, stateTag, "inv_item_add_do", openGuard<stub_inv_item_add_do>);
    if (wants("inv_item_get_weapon_upgrades")) registerOne(L, state, log, stateTag, "inv_item_get_weapon_upgrades", openGuard<stub_inv_item_get_weapon_upgrades>);
    if (wants("character_set_cannot_exit_rc_vehicle")) registerOne(L, state, log, stateTag, "character_set_cannot_exit_rc_vehicle", openGuard<stub_character_set_cannot_exit_rc_vehicle>);
    if (wants("ai_force_team_idle")) registerOne(L, state, log, stateTag, "ai_force_team_idle", openGuard<stub_ai_force_team_idle>);
    if (wants("notoriety_get")) registerOne(L, state, log, stateTag, "notoriety_get", openGuard<stub_notoriety_get>);
    if (wants("set_attack_peds_flag")) registerOne(L, state, log, stateTag, "set_attack_peds_flag", openGuard<stub_set_attack_peds_flag>);
    if (wants("perform_killswitch")) registerOne(L, state, log, stateTag, "perform_killswitch", openGuard<stub_perform_killswitch>);
    if (wants("player_action_is_pressed")) registerOne(L, state, log, stateTag, "player_action_is_pressed", openGuard<stub_player_action_is_pressed>);
    if (wants("player_get_custom_voice")) registerOne(L, state, log, stateTag, "player_get_custom_voice", openGuard<stub_player_get_custom_voice>);
    if (wants("player_take_human_shield_do")) registerOne(L, state, log, stateTag, "player_take_human_shield_do", openGuard<stub_player_take_human_shield_do>);
    if (wants("npc_aim_at_point")) registerOne(L, state, log, stateTag, "npc_aim_at_point", openGuard<stub_npc_aim_at_point>);
    if (wants("npc_brute_set_no_downed_finisher")) registerOne(L, state, log, stateTag, "npc_brute_set_no_downed_finisher", openGuard<stub_npc_brute_set_no_downed_finisher>);
    if (wants("npc_dont_auto_equip_or_unequip_weapons")) registerOne(L, state, log, stateTag, "npc_dont_auto_equip_or_unequip_weapons", openGuard<stub_npc_dont_auto_equip_or_unequip_weapons>);
    if (wants("npc_is_in_brute_grab_qte")) registerOne(L, state, log, stateTag, "npc_is_in_brute_grab_qte", openGuard<stub_npc_is_in_brute_grab_qte>);
    if (wants("party_dismiss_do")) registerOne(L, state, log, stateTag, "party_dismiss_do", openGuard<stub_party_dismiss_do>);
    if (wants("human_being_revived")) registerOne(L, state, log, stateTag, "human_being_revived", openGuard<stub_human_being_revived>);
    if (wants("human_being_revived_by")) registerOne(L, state, log, stateTag, "human_being_revived_by", openGuard<stub_human_being_revived_by>);
    if (wants("human_downed_set_mars_kb_interactable")) registerOne(L, state, log, stateTag, "human_downed_set_mars_kb_interactable", openGuard<stub_human_downed_set_mars_kb_interactable>);
    if (wants("inv_item_dual_wield")) registerOne(L, state, log, stateTag, "inv_item_dual_wield", openGuard<stub_inv_item_dual_wield>);
    if (wants("inv_item_get_anim_group")) registerOne(L, state, log, stateTag, "inv_item_get_anim_group", openGuard<stub_inv_item_get_anim_group>);
    if (wants("inv_item_get_weapon_slot")) registerOne(L, state, log, stateTag, "inv_item_get_weapon_slot", openGuard<stub_inv_item_get_weapon_slot>);
    if (wants("follower_is_unconscious")) registerOne(L, state, log, stateTag, "follower_is_unconscious", openGuard<stub_follower_is_unconscious>);
    if (wants("character_check_resource_loaded")) registerOne(L, state, log, stateTag, "character_check_resource_loaded", openGuard<stub_character_check_resource_loaded>);
    if (wants("character_is_combat_ready")) registerOne(L, state, log, stateTag, "character_is_combat_ready", openGuard<stub_character_is_combat_ready>);
    if (wants("character_is_on_fire")) registerOne(L, state, log, stateTag, "character_is_on_fire", openGuard<stub_character_is_on_fire>);
    if (wants("character_parachute_open")) registerOne(L, state, log, stateTag, "character_parachute_open", openGuard<stub_character_parachute_open>);
    if (wants("character_parachute_play_action")) registerOne(L, state, log, stateTag, "character_parachute_play_action", openGuard<stub_character_parachute_play_action>);
    if (wants("character_prevent_kneecapping")) registerOne(L, state, log, stateTag, "character_prevent_kneecapping", openGuard<stub_character_prevent_kneecapping>);
    if (wants("character_set_over_the_shoulder")) registerOne(L, state, log, stateTag, "character_set_over_the_shoulder", openGuard<stub_character_set_over_the_shoulder>);
    if (wants("character_take_human_shield_do")) registerOne(L, state, log, stateTag, "character_take_human_shield_do", openGuard<stub_character_take_human_shield_do>);
    if (wants("character_use_synced_finishers")) registerOne(L, state, log, stateTag, "character_use_synced_finishers", openGuard<stub_character_use_synced_finishers>);
    if (wants("action_play_custom_do")) registerOne(L, state, log, stateTag, "action_play_custom_do", openGuard<stub_action_play_custom_do>);
}

} // namespace stub_sections
} // namespace sr3luahost
