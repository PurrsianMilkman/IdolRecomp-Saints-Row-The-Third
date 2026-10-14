// lua_spec_confirmed_stubs_b03_vintui_r03_r05.cpp
//
// spec-lua-bindings.md Sec18-Sec21 ("Vint UI API", 8 names) + ranking tranche 03 (Sec30, 9 names) + ranking tranche 05 (Sec33, 25 names) and the Sec14.1 0x009df3d0 correction.
//
// One of 7 files split 2026-10-07 from the former single 8,700+ line
// lua_spec_confirmed_stubs.cpp (a pure structural refactor, zero behaviour
// change - every stub body, comment and citation below is reproduced
// verbatim from that file). See lua_spec_confirmed_stubs_registry.cpp's own
// top comment for the full split rationale and the shared-registration
// mechanism (lua_spec_confirmed_stubs_sections.h/_common.h).
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

namespace sr3luahost {
namespace {

// =======================================================================
// Batch 2026-10-02: spec-lua-bindings.md Sec18-Sec21 ("Vint UI API"), the
// 8 names a partner team's own real-mission-drive trace flagged as the
// single biggest unspecced area by runtime call volume (Sec18's own front
// matter: vint_set_property 14,500 calls, vint_get_property 1,692,
// vint_get_time_index 1,206, vint_dataresponder_finished/
// vint_internal_dataresponder_request 1,102 each, vint_dataitem_get 711,
// vint_object_first_child 708, vint_object_clone 509). All eight are
// members of the same 55-name registrar (0x00e1dfb0) vint_object_find
// (Sec13.7/Sec15, function 13 above) already belongs to - genuinely
// native for all eight (Sec18's own grep check against the six real
// preload files), never a missing Lua-library load. Each function below
// implements ONLY its own (sub)section's CONFIRMED argument-reading and
// return-arity contract. Not simulated: the Sec9.2 per-type
// property-descriptor tables (now specified for all 13 types by Team A's U1
// pass; using them is a separate follow-up task), the callback-claim
// mechanism, and the data-responder's own final dispatch 0x00e1e660 -
// same "record the real, checkable gate/branch, not the full engine"
// convention the Sec27/Sec28 batch above already established. Tweens: per
// the corrected Sec20.1 (2026-10-03), setting start_value/end_value writes
// the tween's OWN property, with no redirect to the target; only the
// target's type code is borrowed for parsing. vint_set_property below
// already writes to the handle it was given, so that behaviour is right;
// the type-code parsing is part of the unimplemented descriptor tables. See engine_state.h's
// own per-field/per-method doc comments for exactly which real
// global/mechanism each one stands for.
// =======================================================================

// A generic captured Lua scalar (VintTaggedValue, engine_state.h) read from
// one Lua stack slot - number/boolean/string, or None for anything else
// (nil, table, function, ...) rather than guessing a type. Shared by
// vint_dataitem_get's own field storage and vint_set_property/
// vint_get_property's own property-value bag (both cite the same real
// 24-byte tagged-value record shape, Sec19.2/Sec20).
VintTaggedValue readVintTaggedValue(lua_State* L, int idx) {
    VintTaggedValue v;
    switch (lua_type(L, idx)) {
        case LUA_TNUMBER:
            v.kind = VintTaggedValue::Kind::Number;
            v.number = lua_tonumber(L, idx);
            break;
        case LUA_TBOOLEAN:
            v.kind = VintTaggedValue::Kind::Boolean;
            v.boolean = lua_toboolean(L, idx) != 0;
            break;
        case LUA_TSTRING:
            v.kind = VintTaggedValue::Kind::String;
            v.text = argString(L, idx);
            break;
        default:
            v.kind = VintTaggedValue::Kind::None;
            break;
    }
    return v;
}

// The matching push side - the generic "push one typed variant" helper
// Sec19.2/Sec20.2 both cite (0x00e1a420). Returns how many Lua values it
// pushed (0 for a None slot - defensive; a populated field/property this
// project itself stored is never None, since readVintTaggedValue only
// produces one from a real Lua value already on the stack at write time).
int pushVintTaggedValue(lua_State* L, const VintTaggedValue& v) {
    switch (v.kind) {
        case VintTaggedValue::Kind::Number:
            lua_pushnumber(L, v.number);
            return 1;
        case VintTaggedValue::Kind::Boolean:
            lua_pushboolean(L, v.boolean ? 1 : 0);
            return 1;
        case VintTaggedValue::Kind::String:
            lua_pushstring(L, v.text.c_str());
            return 1;
        default:
            return 0;
    }
}

// ---------------------------------------------------------------------
// vint_object_first_child (Sec18.1). Arguments: 1, mandatory, unconditional
// lua_tonumber+truncate (no type gate - CONFIRMED). Return: CONFIRMED, a
// genuine difference from vint_object_find's own "always one value"
// convention - zero Lua values (not even 0.0) on EITHER a bad handle or a
// resolved object with no first child; exactly one number (the child's
// handle) when a real first child exists.
// ---------------------------------------------------------------------
int stub_vint_object_first_child(lua_State* L) {
    logCall(L, upLog(L), "vint_object_first_child", upStateTag(L));
    uint32_t handle = static_cast<uint32_t>(lua_tonumber(L, 1));
    uint32_t child = upState(L)->vdoObjectFirstChild(handle);
    if (child == 0) return 0; // CONFIRMED: bad handle, or no first child -> zero Lua values
    lua_pushnumber(L, static_cast<lua_Number>(child));
    return 1;
}

// ---------------------------------------------------------------------
// vint_object_clone (Sec18.2). Arguments: variable arity 1-2 - orig_handle
// (number, mandatory, unconditional lua_tonumber+truncate); parent_handle
// (number, optional - absent/nil falls back to the original's own parent,
// CONFIRMED). The type gate on arg 2 (absent-or-wrong-type -> fallback)
// mirrors vint_object_find's own sibling argument in the SAME registrar
// (Sec15) - HIGH CONFIDENCE by that analogy, not independently re-confirmed
// for this exact call site (Sec18.2's own text only states the absent/nil
// case explicitly). Return: CONFIRMED, matches vint_object_find's own
// "always exactly one number" convention exactly - 0.0 on every failure
// path (no current document - an OPEN read, see EngineState::cloneVdoObject
// - or an unresolvable orig_handle), the new clone's real handle on success.
// ---------------------------------------------------------------------
int stub_vint_object_clone(lua_State* L) {
    logCall(L, upLog(L), "vint_object_clone", upStateTag(L));
    uint32_t orig = static_cast<uint32_t>(lua_tonumber(L, 1));
    bool hasParent = lua_gettop(L) >= 2 && lua_type(L, 2) == LUA_TNUMBER;
    uint32_t parentArg = hasParent ? static_cast<uint32_t>(lua_tonumber(L, 2)) : 0;
    uint32_t handle = upState(L)->cloneVdoObject(orig, hasParent, parentArg); // OPEN (current document) propagates
    lua_pushnumber(L, static_cast<lua_Number>(handle));
    return 1;
}

// ---------------------------------------------------------------------
// vint_get_time_index (Sec19.1). Arguments: variable arity 0-1 -
// doc_handle (number, optional; CONFIRMED a genuine difference from every
// other optional numeric-handle argument in this document: absent, nil, OR
// the literal number 0 all take the SAME current-document fallback path).
// Return: CONFIRMED - zero Lua values on failure, exactly one number (the
// time-index float) on success; this minimal host cannot separately
// reach the real failure path (see EngineState::vintGetTimeIndex's own doc
// comment), so every call either returns a real, test-set value or refuses
// as OPEN.
// ---------------------------------------------------------------------
int stub_vint_get_time_index(lua_State* L) {
    logCall(L, upLog(L), "vint_get_time_index", upStateTag(L));
    bool isNumberArg = lua_gettop(L) >= 1 && lua_type(L, 1) == LUA_TNUMBER;
    double rawArg = isNumberArg ? lua_tonumber(L, 1) : 0.0;
    bool explicitNonZero = isNumberArg && rawArg != 0.0; // CONFIRMED: an explicit 0 is NOT "explicit" here
    uint32_t explicitDoc = explicitNonZero ? static_cast<uint32_t>(rawArg) : 0;
    double t = upState(L)->vintGetTimeIndex(explicitNonZero, explicitDoc); // OPEN propagates
    lua_pushnumber(L, t);
    return 1;
}

// ---------------------------------------------------------------------
// vint_dataitem_get (Sec19.2). Arguments: 1, mandatory, unconditional
// lua_tonumber+truncate. Return: CONFIRMED - zero Lua values on a bad
// handle; otherwise every populated field of the resolved data item, in
// order (bounded at 32, real shipped script only ever reads the first 24).
// A separate handle space from the VDO-object handles above (CONFIRMED,
// Sec19.2 - a distinct resolver).
// ---------------------------------------------------------------------
int stub_vint_dataitem_get(lua_State* L) {
    logCall(L, upLog(L), "vint_dataitem_get", upStateTag(L));
    uint32_t handle = static_cast<uint32_t>(lua_tonumber(L, 1));
    const std::vector<VintTaggedValue>* fields = upState(L)->findVintDataItemFields(handle);
    if (!fields) return 0; // CONFIRMED: bad handle -> zero Lua values
    int pushed = 0;
    for (const auto& f : *fields) pushed += pushVintTaggedValue(L, f);
    return pushed;
}

// ---------------------------------------------------------------------
// vint_set_property (Sec20.1). Arguments: variable arity 2+ - handle
// (number, type-gated via lua_type == LUA_TNUMBER; an absent/non-numeric
// handle leaves it the literal 0 rather than aborting, CONFIRMED - a
// genuine difference from vint_get_property's own unconditional handle
// read below); property_name (string, mandatory - a missing or non-string
// value makes the WHOLE call a silent no-op, CONFIRMED); value... (1-3
// trailing values, whose real required type/count depends on the named
// property's own type/size code - parsed by EngineState::setVintProperty
// through the per-type descriptor tables, 2026-10-03; read its doc comment
// for the typed/raw/refused cases - an OpenStateError there surfaces as a
// Lua error through bareGuardCall). Return: CONFIRMED - always zero Lua
// values, every path that returns.
// ---------------------------------------------------------------------
int stub_vint_set_property(lua_State* L) {
    logCall(L, upLog(L), "vint_set_property", upStateTag(L));
    uint32_t handle = 0;
    if (lua_gettop(L) >= 1 && lua_type(L, 1) == LUA_TNUMBER) handle = static_cast<uint32_t>(lua_tonumber(L, 1));
    if (lua_gettop(L) < 2 || lua_type(L, 2) != LUA_TSTRING) return 0; // CONFIRMED: missing/non-string property_name -> silent no-op
    std::string propertyName = argString(L, 2);
    std::vector<VintTaggedValue> values;
    for (int i = 3, top = lua_gettop(L); i <= top; ++i) values.push_back(readVintTaggedValue(L, i));
    upState(L)->setVintProperty(handle, propertyName, std::move(values)); // silently no-ops on an unresolved handle
    return 0; // CONFIRMED: always zero Lua values, every path
}

// ---------------------------------------------------------------------
// vint_get_property (Sec20.2). Arguments: 2, fixed - handle (number,
// mandatory, unconditional lua_tonumber+truncate - no type gate, unlike
// vint_set_property's own handle read above); property_name (string,
// mandatory). Return: CONFIRMED, variable arity driven by the named
// property's own type - EngineState::getVintProperty (2026-10-03: typed
// through the per-type descriptor tables, computed screen_nw/se/size,
// OpenStateError where the value or its push form is not known; untyped
// test objects keep the old echo) - zero Lua values (not even nil) on a bad
// handle or a property-name miss.
// ---------------------------------------------------------------------
int stub_vint_get_property(lua_State* L) {
    logCall(L, upLog(L), "vint_get_property", upStateTag(L));
    uint32_t handle = static_cast<uint32_t>(lua_tonumber(L, 1));
    std::string propertyName = argString(L, 2);
    std::optional<std::vector<VintTaggedValue>> values = upState(L)->getVintProperty(handle, propertyName);
    if (!values) return 0; // CONFIRMED: not found / bad handle -> zero Lua values
    int pushed = 0;
    for (const auto& v : *values) pushed += pushVintTaggedValue(L, v);
    return pushed;
}

// ---------------------------------------------------------------------
// vint_dataresponder_finished (Sec21.1). Arguments: 1, mandatory string.
// Return: CONFIRMED, always exactly 1 boolean - true if no record exists
// for `name` at all (the real, honestly-flagged quirk) or the found
// record's own completion flag is set; false only when a record exists
// and that flag is clear.
// ---------------------------------------------------------------------
int stub_vint_dataresponder_finished(lua_State* L) {
    logCall(L, upLog(L), "vint_dataresponder_finished", upStateTag(L));
    std::string name = argString(L, 1);
    lua_pushboolean(L, upState(L)->dataResponderFinished(name) ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// vint_internal_dataresponder_request (Sec21.2). Arguments: variable arity
// 3+ - data_responder_name (string, mandatory); callback_function_name
// (string, conditionally required - absent/non-string makes the whole
// call a silent no-op); max_records_to_return (number, conditionally
// required the same way); up to 16 further trailing arguments (NOT
// modeled - see EngineState::dataResponderRequest's own doc comment, they
// have no Lua-visible consequence among this pass's two in-scope
// functions). Return: CONFIRMED, always zero Lua values, every path
// including the no-op path. CONFIRMED, a genuinely surprising negative:
// this native never creates the named record - a name with no existing
// record is itself a complete, silent no-op.
// ---------------------------------------------------------------------
int stub_vint_internal_dataresponder_request(lua_State* L) {
    logCall(L, upLog(L), "vint_internal_dataresponder_request", upStateTag(L));
    std::string name = argString(L, 1);
    bool validCallback = lua_gettop(L) >= 2 && lua_type(L, 2) == LUA_TSTRING;
    bool validMax = lua_gettop(L) >= 3 && lua_type(L, 3) == LUA_TNUMBER;
    upState(L)->dataResponderRequest(name, validCallback, validMax);
    return 0; // CONFIRMED: always zero Lua values, every path including the no-op path
}

// =======================================================================
// Batch 2026-10-02: spec-lua-api-behaviour.md Sec30 ("ranking tranche 03"),
// 9 of its 25 CONFIRMED names. All 25 were investigated and written up by
// Team A (Sec30.1-Sec30.7); per the orchestrator's own explicit scope
// instruction for this pass ("implement the CONFIRMED entries that the
// runtime ranking or mission drive actually hit; others can wait"), this
// batch covers:
//   - The 2 names with a real, non-zero measured call count in this
//     session's own latest trace (results/verify_sched_check/verdict_stub_
//     hits_with_missions.tsv, the scheduler-verification run against the
//     full 1490-tagged registry): vehicle_disable_explosion_and_damage_vfx
//     (28 calls, 3 static call sites across 2 distinct scripts) and
//     auto_pickup_enable (1 call, 4 static call sites across 2 scripts).
//   - vehicle_exit_group_do and team_make_unfriendly, the two functions
//     this tranche's own cross-function note (Sec30.7) flags as reaching a
//     CONFIRMED crash-shaped edge in the real engine (a NULL-`this` call,
//     an out-of-bounds relation-matrix index) - implemented with an
//     explicit, labelled HOST-SAFETY guard per this task's own instruction
//     (see each function's own doc comment below for the exact reasoning;
//     this is a deliberate, stated deviation from faithful reproduction,
//     never presented as a spec fact).
//   - vehicle_exit_group_check_done, vehicle_exit_group_do's own "polling
//     partner" (Sec30.5) - implemented alongside it since it shares the
//     same table-reading helper and CharacterState fields, at negligible
//     marginal cost over implementing vehicle_exit_group_do alone.
//   - vehicle_set_invulnerable_to_player_explosives and vehicle_set_
//     special_override_never_ghost, the 2 further single-vehicle setters
//     Sec30.5 describes as sharing the IDENTICAL "variant-1 double-gate"
//     shape as vehicle_disable_explosion_and_damage_vfx, and vehicle_
//     clear_all_radio_locks, the bulk-clear sibling of the same force-flag
//     word - all 4 bundled together once the shared VehicleState/
//     force-flag infrastructure exists for the first one, at negligible
//     marginal cost.
//   - vehicle_is_vtol, a simple shared-fact query (Sec30.5: "true only
//     when class 4") reusing the same VehicleState/objectResolves()
//     infrastructure the other vehicle functions above already need.
//
// Deliberately NOT implemented this pass (zero measured real hits in the
// trace above, AND each would need its own substantial new, untested
// engine-state modeling - e.g. the 70-entry CAE combat-action table
// (Sec30.4) and its per-character staged/committed action record for
// ai_suggest_action/ai_do_scripted_advance; a customization/rig-swap
// sequencing model for boss_battle_matt_set_player_as_avatar; a world-
// object-manager-based ambient-emitter search for audio_ambient_emitter_
// pause/resume; seat/occupant arrays, screen-distortion effects, path-
// point arrays and recursive component detachment for vehicle_stop_do/
// vehicle_pathfind_to_do/vehicle_set_malfunctioning/vehicle_component_
// detach/vehicle_set_2d_tank_controls/vehicle_rc_get_signal_strength;
// turn_to_do/teleport_vehicle_to_object's own deep pose/orientation
// helpers; character_dont_regenerate/character_clear_combat_move's own
// force-flag/combat-move-slot machinery): character_dont_regenerate,
// character_clear_combat_move, boss_battle_matt_set_player_as_avatar,
// boss_battle_matt_hide_wings, audio_ambient_emitter_pause, audio_ambient_
// emitter_resume, ai_suggest_action, ai_do_scripted_advance, vehicle_set_
// 2d_tank_controls, vehicle_rc_get_signal_strength, vehicle_stop_do,
// vehicle_pathfind_to_do, vehicle_set_malfunctioning, vehicle_component_
// detach, turn_to_do, teleport_vehicle_to_object. These remain ordinary
// generic logging stubs (ARE in tools/lua_all_registered_1490_tagged.txt,
// NOT in specConfirmedStubNames()) - left for a future pass if real call
// evidence or further scoping time justifies the deeper modeling each one
// needs. All 25 names are tagged `gameplay` in tools/lua_all_registered_
// 1490_tagged.txt (grepped directly, not assumed).
// =======================================================================


// ---------------------------------------------------------------------
// vehicle_set_invulnerable_to_player_explosives (Sec30.5, 0x00a66270). 1
// mandatory vehicle name + 1 boolean (lua_toboolean, no nil-gate, absent ->
// false - CONFIRMED). No return. Sets/clears VehicleState::forceFlags bit
// kInvulnerableToPlayerExplosivesBit (+0x1d7c bit 0x2) via the shared
// double-gate helper above.
// ---------------------------------------------------------------------
int stub_vehicle_set_invulnerable_to_player_explosives(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_invulnerable_to_player_explosives", upStateTag(L));
    std::string vehicleName = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0;
    applyVehicleForceFlagSetter(L, "vehicle_set_invulnerable_to_player_explosives", vehicleName, flag,
                                VehicleState::kInvulnerableToPlayerExplosivesBit);
    return 0;
}

// ---------------------------------------------------------------------
// vehicle_disable_explosion_and_damage_vfx (Sec30.5, 0x00a621f0). 1
// mandatory vehicle name + 1 boolean (no nil-gate, absent -> false -
// CONFIRMED). No return. Sets/clears VehicleState::forceFlags bit
// kDisableExpAndDamageVfxBit (+0x1d7d bit 0x20). Measured 28 real calls, 3
// static call sites across 2 distinct real scripts (results/verify_sched_
// check/verdict_stub_hits_with_missions.tsv) - the highest real call count
// of any of this tranche's 25 names, so this is this batch's top priority.
// ---------------------------------------------------------------------
int stub_vehicle_disable_explosion_and_damage_vfx(lua_State* L) {
    logCall(L, upLog(L), "vehicle_disable_explosion_and_damage_vfx", upStateTag(L));
    std::string vehicleName = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0;
    applyVehicleForceFlagSetter(L, "vehicle_disable_explosion_and_damage_vfx", vehicleName, flag,
                                VehicleState::kDisableExpAndDamageVfxBit);
    return 0;
}

// ---------------------------------------------------------------------
// vehicle_set_special_override_never_ghost (Sec30.5, 0x00a63300). 1
// mandatory vehicle name + 1 boolean (no nil-gate, absent -> false -
// CONFIRMED). No return. Sets/clears VehicleState::forceFlags bit
// kSpecialOverrideNeverGhostBit (+0x1d7d bit 0x40). CONFIRMED: this
// setter has two further engine callers beyond this Lua entry point - not
// modeled (out of this project's own Lua-visible scope).
// ---------------------------------------------------------------------
int stub_vehicle_set_special_override_never_ghost(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_special_override_never_ghost", upStateTag(L));
    std::string vehicleName = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0;
    applyVehicleForceFlagSetter(L, "vehicle_set_special_override_never_ghost", vehicleName, flag,
                                VehicleState::kSpecialOverrideNeverGhostBit);
    return 0;
}

// ---------------------------------------------------------------------
// vehicle_clear_all_radio_locks (Sec30.5, 0x00a647e0). No arguments. No
// return. CONFIRMED: walks every vehicle in the world object manager and
// clears the radio_controls_locked bit (+0x1d7c bit 0x4) on each (literal
// false, hardcoded) - a per-vehicle authority miss emits one opcode-0x46
// record per such vehicle rather than skipping it. This project has no
// real world object manager / spawned-vehicle enumeration (stated
// simplification, VehicleState's own doc comment) - "every vehicle in the
// world object manager" stands in here for "every vehicle this host
// currently knows about" (EngineState::vehicles(), populated only by the
// single-vehicle setters above or a test), the same minimal-registry
// convention this project already applies elsewhere (e.g. the Sec29 named-
// object map).
// ---------------------------------------------------------------------
int stub_vehicle_clear_all_radio_locks(lua_State* L) {
    logCall(L, upLog(L), "vehicle_clear_all_radio_locks", upStateTag(L));
    EngineState* st = upState(L);
    for (auto& entry : st->vehicles()) {
        VehicleState& vehicle = entry.second;
        if (vehicle.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
            vehicle.forceFlags.setBits(VehicleState::kRadioControlsLockedBit, 0u); // hardcoded false
        } else {
            EngineState::replicateStateChange("vehicle_clear_all_radio_locks", entry.first);
        }
    }
    return 0;
}

// =======================================================================
// Batch 2026-10-02: spec-lua-api-behaviour.md Sec33 ("ranking tranche 05",
// 25 names) + the Sec14.1 0x0095da50 correction. All 25 names are
// `gameplay`-tagged in tools/lua_all_registered_1490_tagged.txt (grepped
// directly, not assumed). Real call-count evidence (results/
// verdict_stub_hits_with_missions_20261002.tsv, tools/
// lua_reconciliation_called_and_registered_1181.tsv): every one of these
// 25 names has ZERO hits in the real mission-drive run (static script call
// sites only, 2-3 each, per Sec33's own "low-traffic" framing) - so there
// is no real-call-volume signal to prioritize BY within this tranche; each
// entry below is implemented to the depth Sec33 confirms, without
// simulating subsystems this project has nowhere else built (physics
// pools, Wwise events, positions/raycasts - see each doc comment for the
// specific stated gap).
// =======================================================================

// ---------------------------------------------------------------------
// vehicle_is_helicopter (Sec33.1, 0x00a66070). 1 mandatory string
// (vehicle name) - CONFIRMED: bypasses the usual vehicle-instance
// resolver entirely (this project's single shared objectResolves() map
// already stands in for every per-kind resolver, Sec29, so there is no
// separate "character redirect" step to skip here). Return: 1 boolean -
// true only when flyingType == 3 (CONFIRMED; 4 = VTOL answers false
// here, unlike this document's other helicopter-family commands which
// accept 3 OR 4).
// ---------------------------------------------------------------------
int stub_vehicle_is_helicopter(lua_State* L) {
    logCall(L, upLog(L), "vehicle_is_helicopter", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool result = false;
    if (st->objectResolves().get(name)) { // OPEN until set
        result = st->getOrCreateVehicle(name).flyingType.get() == 3; // OPEN until set
    }
    lua_pushboolean(L, result ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// vehicle_is_vtol_hover / vehicle_is_vtol_jet (Sec33.1, 0x00a64700 /
// 0x00a64770). 1 string via the vehicle resolver. CONFIRMED real
// stack-discipline defect: the success branch falls through into the
// failure path's own push, so a TRUE result pushes 2 values (true, then
// a stray false) while every other path pushes exactly 1 (false) - a
// byte-for-byte-faithful reimplementation must reproduce this. Gate:
// flyingType == 4 (not 3); "hover" = vtolState in {0,3}, "jet" =
// vtolState in {1,2} (HYPOTHESIS state meanings, CONFIRMED membership
// test and disjointness).
// ---------------------------------------------------------------------
int stub_vehicle_is_vtol_hover(lua_State* L) {
    logCall(L, upLog(L), "vehicle_is_vtol_hover", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool success = false;
    if (st->objectResolves().get(name)) { // OPEN until set
        auto& v = st->getOrCreateVehicle(name);
        if (v.flyingType.get() == 4) { // OPEN until set
            int32_t s = v.vtolState.get(); // OPEN until set
            success = (s == 0 || s == 3);
        }
    }
    if (success) {
        lua_pushboolean(L, 1);
        lua_pushboolean(L, 0); // CONFIRMED stack-discipline defect: stray extra false after a successful true
        return 2;
    }
    lua_pushboolean(L, 0);
    return 1;
}

int stub_vehicle_is_vtol_jet(lua_State* L) {
    logCall(L, upLog(L), "vehicle_is_vtol_jet", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool success = false;
    if (st->objectResolves().get(name)) { // OPEN until set
        auto& v = st->getOrCreateVehicle(name);
        if (v.flyingType.get() == 4) { // OPEN until set
            int32_t s = v.vtolState.get(); // OPEN until set
            success = (s == 1 || s == 2);
        }
    }
    if (success) {
        lua_pushboolean(L, 1);
        lua_pushboolean(L, 0); // CONFIRMED stack-discipline defect: stray extra false after a successful true
        return 2;
    }
    lua_pushboolean(L, 0);
    return 1;
}

// ---------------------------------------------------------------------
// vehicle_is_ready (Sec33.1, 0x00a62820). 1 string via the vehicle
// resolver. CONFIRMED: ready = +0x3b bit0 clear AND +0x3a bit0 clear AND
// +0x33 bit 0x8 set; unresolved ("or the vehicle is null") -> false.
// ---------------------------------------------------------------------
int stub_vehicle_is_ready(lua_State* L) {
    logCall(L, upLog(L), "vehicle_is_ready", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool ready = false;
    if (st->objectResolves().get(name)) { // OPEN until set
        auto& v = st->getOrCreateVehicle(name);
        bool notReady = v.notReadyBit3b.get() || v.notReadyBit3a.get() || !v.fullySetUp.get(); // OPEN until set, each
        ready = !notReady;
    }
    lua_pushboolean(L, ready ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// vehicle_never_flatten_tires (Sec33.1, 0x00a62a60). 1 string + 1
// optional boolean, default true (standard nil-gated idiom). No return.
// CONFIRMED double-gate setter: this project always applies the local
// write directly (replicateStateChange's own doc comment) - local write
// = vehicle byte +0x1d7b bit 0x1; tags "vehicle"/
// "m_force_flagsnever_flatten_tires". No resolver mentioned in spec, so
// none is required here (same convention as set_ignore_ai_flag).
// ---------------------------------------------------------------------
int stub_vehicle_never_flatten_tires(lua_State* L) {
    logCall(L, upLog(L), "vehicle_never_flatten_tires", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = true; // CONFIRMED default when arg 2 omitted/nil
    if (lua_gettop(L) >= 2 && lua_type(L, 2) != LUA_TNIL) value = lua_toboolean(L, 2) != 0;
    auto& v = upState(L)->getOrCreateVehicle(name);
    v.forceFlags1d7b.setBits(0x1, value ? 0x1u : 0x0u);
    EngineState::replicateStateChange("vehicle_never_flatten_tires", name);
    return 0;
}

// ---------------------------------------------------------------------
// vehicle_set_weapons_disarmed (Sec33.1, 0x00a63520). 1 string + 1
// boolean, no nil-gate (absent -> false, real lua_toboolean semantics).
// No return. Same double-gate shape: local write = +0x1d7e bit 0x4; tags
// "vehicle"/"m_force_flagsweapons_disabled".
// ---------------------------------------------------------------------
int stub_vehicle_set_weapons_disarmed(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_weapons_disarmed", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = lua_toboolean(L, 2) != 0; // CONFIRMED: no nil-gate, absent -> false
    auto& v = upState(L)->getOrCreateVehicle(name);
    v.forceFlags1d7e.setBits(0x4, value ? 0x4u : 0x0u);
    EngineState::replicateStateChange("vehicle_set_weapons_disarmed", name);
    return 0;
}

// ---------------------------------------------------------------------
// vehicle_set_no_chase (Sec33.1, 0x00a62b80). 1 string + 1 boolean, no
// nil-gate. No return. Setter runs on the vehicle-AI sub-record (handle
// +0xc68), writing sub-byte +5 bit 0x1; tags "vehicle_ai"/
// "vai_force_flagsno_chase" - CONFIRMED the same setter this document's
// existing vehicle_disable_chase entry reaches (out of this batch's own
// scope), through an extra wrapper not modeled here.
// ---------------------------------------------------------------------
int stub_vehicle_set_no_chase(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_no_chase", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = lua_toboolean(L, 2) != 0; // CONFIRMED: no nil-gate
    auto& v = upState(L)->getOrCreateVehicle(name);
    v.vehicleAiForceFlags.setBits(0x1, value ? 0x1u : 0x0u);
    EngineState::replicateStateChange("vehicle_set_no_chase", name);
    return 0;
}

// ---------------------------------------------------------------------
// vehicle_set_kneecappers (Sec33.1, 0x00a62fe0). 1 string + 1 optional
// boolean, default true. No return. No replication at this level
// (CONFIRMED). This project has no per-wheel physics/collision pool
// (48-vehicle fixed pool, CONFIRMED structure) - only the two CONFIRMED
// gate outcomes are tracked (see VehicleState's own doc comment):
// enabling while not yet "set up" (+0x33 bit 0x8 clear) only sets the
// deferred-request bit; enabling once set up marks it actually enabled;
// disabling clears both (also clearing the deferred bit "if that's all
// there was", CONFIRMED).
// ---------------------------------------------------------------------
int stub_vehicle_set_kneecappers(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_kneecappers", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = true; // CONFIRMED default when arg 2 omitted/nil
    if (lua_gettop(L) >= 2 && lua_type(L, 2) != LUA_TNIL) value = lua_toboolean(L, 2) != 0;
    auto& v = upState(L)->getOrCreateVehicle(name);
    if (value) {
        if (!v.fullySetUp.get()) { // OPEN until set
            v.kneecappersDeferred = true; // CONFIRMED: not yet "set up" -> deferred-request bit only
        } else {
            v.kneecappersEnabled = true;
            v.kneecappersDeferred = false;
        }
    } else {
        v.kneecappersEnabled = false;
        v.kneecappersDeferred = false; // CONFIRMED: disabling also clears the deferred bit if that's all there was
    }
    return 0;
}

// ---------------------------------------------------------------------
// vehicle_is_vtol (Sec30.5, 0x00a66170). 1 mandatory vehicle name. Return:
// exactly 1 boolean, CONFIRMED. Inline resolution (no character redirect -
// this project does not model the redirect distinction either way, see
// VehicleState's own doc comment); true only when VehicleState::flyingType
// (the SAME (+0xbf4)+0x2c field Sec33.1's vehicle_is_helicopter/vtol_hover/
// vtol_jet also read, unified under that name during the merge of the two
// batches) reads 4. An unresolved name pushes false (CONFIRMED: the real
// inline chain's own liveness/+0x70 failure path yields false here, same
// "unresolved -> false" shape as every boolean-returning query elsewhere in
// this file).
// ---------------------------------------------------------------------
int stub_vehicle_is_vtol(lua_State* L) {
    logCall(L, upLog(L), "vehicle_is_vtol", upStateTag(L));
    std::string vehicleName = argString(L, 1);
    bool isVtol = false;
    EngineState* st = upState(L);
    if (st->objectResolves().get(vehicleName)) { // OPEN until set
        isVtol = st->getOrCreateVehicle(vehicleName).flyingType.get() == 4; // OPEN until set
    }
    lua_pushboolean(L, isVtol ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// vehicle_set_sirenlights (Sec33.1, 0x00a64490). 1 string + 1 boolean,
// no nil-gate. No return. CONFIRMED: re-resolves the vehicle (this
// project's one shared objectResolves() map stands in for the "by id
// pair, bypassing the normal resolver" mechanism) and requires body-
// active byte +0xbd0 == 1. Class switch on (+0xbf4)+0x4c4: classes 3/5/
// 6/7/8/11 get both headlight (+0x1d7c bit 0x10) and siren (bit 0x8)
// bits following the boolean; class 12 gets the siren bit only; every
// OTHER class depends on a per-class "allow" record flag this project
// has no real vehicle-definition table to read (stated gap, same "no
// real data table" convention as several Sec27/Sec28 entries) - refused
// as OPEN rather than guessed. The real Wwise siren SOUND side effect is
// not modeled (no Wwise event table anywhere in this project).
// ---------------------------------------------------------------------
int stub_vehicle_set_sirenlights(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_sirenlights", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = lua_toboolean(L, 2) != 0; // CONFIRMED: no nil-gate
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    auto& v = st->getOrCreateVehicle(name);
    if (!v.bodyActiveOrDefault()) return 0; // ce4556f: +0xbd0 default 1 for a live vehicle
    int32_t cls = v.vehicleClass.get(); // OPEN until set
    switch (cls) {
        case 3: case 5: case 6: case 7: case 8: case 11:
            v.forceFlags1d7c.setBits(0x18, value ? 0x18u : 0x0u); // CONFIRMED: both bits follow the boolean
            break;
        case 12:
            v.forceFlags1d7c.setBits(0x8, value ? 0x8u : 0x0u); // CONFIRMED: siren only
            break;
        default:
            throw OpenStateError("vehicle class " + std::to_string(cls) + " per-class siren/headlight allow flag",
                                 "spec-lua-api-behaviour.md Sec33.1");
    }
    return 0;
}

// ---------------------------------------------------------------------
// vehicle_set_ambient (Sec33.1, 0x00a62ad0). 1 string + 1 optional
// number, default -1.0. No return. CONFIRMED: marks +0x16c0 bit 0x1,
// puts the vehicle-AI sub-record into mode 6/sub-mode 0x11 (authority-
// gated the usual way - this project always applies the local write
// directly, replicateStateChange's own doc comment). If the number is
// >= 0.0, also caps speed (clamped to at most 1000.0) via the SAME
// setter vehicle_speed_override uses (out of this batch's scope) -
// CONFIRMED passed UNCONVERTED (no mph->m/s factor, unlike that other
// entry).
// ---------------------------------------------------------------------
int stub_vehicle_set_ambient(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_ambient", upStateTag(L));
    std::string name = argString(L, 1);
    double speed = -1.0; // CONFIRMED default
    if (lua_gettop(L) >= 2 && lua_type(L, 2) != LUA_TNIL) speed = lua_tonumber(L, 2);
    auto& v = upState(L)->getOrCreateVehicle(name);
    v.ambientFlags.setBits(0x1, 0x1);
    v.aiMode = 6;
    v.aiSubMode = 0x11;
    EngineState::replicateStateChange("vehicle_set_ambient", name);
    if (speed >= 0.0) {
        v.hasSpeedCap = true;
        v.speedCapRaw = std::min<double>(speed, 1000.0); // CONFIRMED clamp, CONFIRMED unconverted
    }
    return 0;
}

// ---------------------------------------------------------------------
// vehicle_spotlight_is_target_spotted (Sec33.1, 0x00a65e70). 1 string
// (vehicle or its driver) + 1 string (target, via the general object
// resolver). 1 boolean. CONFIRMED NOT a pure query: writes the target's
// id pair into the vehicle's spotlight object (re-aiming it) EVERY call,
// before testing anything - modeled here even though the actual test
// cannot be. The real test (range <= 80, <=10 degree cone, unobstructed
// raycast to a point 1.85 units above the target) needs real position/
// geometry data this project has nowhere in its state (no coordinate
// system at all, project-wide stated simplification) - refused as OPEN
// rather than fabricated once both names resolve.
// ---------------------------------------------------------------------
int stub_vehicle_spotlight_is_target_spotted(lua_State* L) {
    logCall(L, upLog(L), "vehicle_spotlight_is_target_spotted", upStateTag(L));
    std::string vehicleOrDriver = argString(L, 1);
    std::string target = argString(L, 2);
    EngineState* st = upState(L);
    bool vehicleResolved = st->objectResolves().get(vehicleOrDriver); // OPEN until set
    bool targetResolved = st->objectResolves().get(target);           // OPEN until set
    if (vehicleResolved && targetResolved) {
        st->getOrCreateVehicle(vehicleOrDriver).spotlightTargetName = target; // CONFIRMED side effect, every call
        throw OpenStateError("vehicle spotlight range/angle/raycast test (no position data modelled)",
                             "spec-lua-api-behaviour.md Sec33.1");
    }
    lua_pushboolean(L, 0); // CONFIRMED structure requires both; unresolved treated as a safe false (this project does not simulate the real gate order)
    return 1;
}

// ---------------------------------------------------------------------
// auto_pickup_enable (Sec30.3, 0x00a3c880). No arguments. No return.
// CONFIRMED: writes the hardcoded byte 1 to global 0x01308698
// (EngineState::autoPickupEnabled(), file-backed default "enabled" per
// spec's own text - already true by construction). "This entry can only
// enable" (CONFIRMED) - there is no in-scope disable counterpart (the
// adjacent registrar slot's own disable sibling is HYPOTHESIS, not dumped,
// not one of this tranche's 25 confirmed names, so not implemented here).
// Measured: 1 real call, 4 static call sites across 2 distinct scripts
// (results/verify_sched_check/verdict_stub_hits_with_missions.tsv).
// ---------------------------------------------------------------------
int stub_auto_pickup_enable(lua_State* L) {
    logCall(L, upLog(L), "auto_pickup_enable", upStateTag(L));
    upState(L)->autoPickupEnabled() = true;
    return 0;
}

// ---------------------------------------------------------------------
// ai_clear_priority_target (Sec33.2, 0x00a3c320). 1 string, resolved
// through the established character chain (Sec33.2's own intro: "all
// four resolve their character argument"). No return. CONFIRMED single-
// gate setter: sets the AI forced-target field to the null id pair.
// ---------------------------------------------------------------------
int stub_ai_clear_priority_target(lua_State* L) {
    logCall(L, upLog(L), "ai_clear_priority_target", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    // This stub has never gated on objectResolves() for an ordinary name
    // (unchanged - deliberately NOT newly gated here, per this pass's own
    // policy against adding a brand-new resolve gate where none existed).
    // Only the 4 reserved #FOLLOWER...# literals (Sec33.2's own text; wired
    // 2026-10-08, follower-sentinel-resolver pass) get real resolution,
    // since a literal "#FOLLOWER1#" is never itself a registered name.
    std::string resolvedName = name;
    if (isFollowerSentinelLiteral(name)) {
        if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // may throw OpenStateError for an unresolved-index literal; a known-but-empty list is a genuine no-op
    } else {
        st->objectResolves().get(name); // OPEN until set
    }
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    c.forcedTargetHandle.clear();
    EngineState::replicateStateChange("ai_clear_priority_target", resolvedName);
    return 0;
}

// ---------------------------------------------------------------------
// vehicle_exit_group_do (Sec30.5, 0x00a67a00). Arguments: arg 1 boolean (no
// nil-gate, CONFIRMED - no further Lua-visible effect modeled, its real
// role inside the OPEN exit-request builder 0x00af3cd0 is not given) + arg
// 2/3 optional booleans (nil-gated, default false - arg 3 is forwarded to
// 0x00a7bdc0, see below; arg 2's own role is likewise inside the OPEN
// 0x00af3cd0) + arg 4 the table of character names (CONFIRMED: must be the
// 4th argument - fewer args sees no table and does nothing, same as an
// empty/missing table). Return: exactly 1 boolean, true iff >=1 named
// character resolves (objectResolves(), reused per Sec29) and is in
// vehicle-state 3 ("seated", HYPOTHESIS - CharacterState::stateEnum,
// already-established +0x16d4 field, Sec3.4/Sec3.10/Sec30.5). Per the
// spec's own exact wording, Sec30.5 does NOT additionally gate this
// function's own state-3 test on the "is dead/gone" predicate the way
// vehicle_exit_group_check_done's own paragraph explicitly does below - a
// stated, preserved asymmetry (copying the spec's own quirks, not
// "cleaning them up", per Sec30.7's own instruction elsewhere in this
// document) - so CharacterState::isAlive is intentionally NOT read here.
//
// For each such character, the real engine "builds an exit request and
// hands it to 0x00af3cd0" (OPEN, not dumped - no further effect modeled)
// and remembers the FIRST vehicle found via the character's own vehicle
// link (CharacterState::currentVehicleName, a new field scoped only to
// this function - see its own doc comment, engine_state.h).
//
// CONFIRMED crash-shaped edge, explicitly flagged by Sec30.7: the real
// engine then calls 0x00a7bdc0(arg3) on whatever vehicle was found - or,
// when none was found across the whole table, with a NULL `this`
// ("confirmed from the listing, XOR ECX,ECX"); Sec30.7 leaves OPEN
// "whether 0x00a7bdc0 tolerates that". 0x00a7bdc0's own body was never
// independently dumped, so this host has no further real effect to apply
// on the found-vehicle path either way. HOST-SAFETY DEVIATION (not a spec
// fact, per this task's own explicit instruction): the real engine reaches
// that unconfirmed-safety null-`this` call on the no-vehicle-found path;
// this host guards explicitly instead of performing the equivalent call on
// an unresolved target, and counts the averted path via
// EngineState::recordVehicleExitGroupNullThisGuard() (test-observable
// only, not a real engine field) - "this project does not simulate
// crashes" (cf. group_get_next_npc above, Sec28.5, item 57, the
// established precedent for this exact idiom).
// ---------------------------------------------------------------------
int stub_vehicle_exit_group_do(lua_State* L) {
    logCall(L, upLog(L), "vehicle_exit_group_do", upStateTag(L));
    (void)lua_toboolean(L, 1); // arg 1: no nil-gate, CONFIRMED; no further effect modeled (see comment above)
    bool arg3 = (lua_gettop(L) >= 3 && lua_type(L, 3) != LUA_TNIL) ? (lua_toboolean(L, 3) != 0) : false;
    (void)arg3; // forwarded to 0x00a7bdc0 per spec; not modeled further (callee OPEN, not dumped)
    std::vector<std::string> names = readCharacterNameArray(L, 4); // arg 4 must be 4th, CONFIRMED
    EngineState* st = upState(L);
    bool any = false;
    std::string foundVehicle; // empty = "no vehicle found" (the null-`this` case)
    for (const auto& name : names) {
        if (!st->objectResolves().get(name)) continue; // OPEN until set
        CharacterState& c = st->getOrCreateCharacter(name);
        if (c.stateEnum.get() != 3) continue; // OPEN until set
        any = true; // this character got an exit request (0x00af3cd0, OPEN - no further effect modeled)
        if (foundVehicle.empty()) {
            foundVehicle = c.currentVehicleName.get(); // OPEN until set
        }
    }
    if (foundVehicle.empty()) {
        st->recordVehicleExitGroupNullThisGuard(); // HOST-SAFETY: null-`this` guard, see comment above
    }
    lua_pushboolean(L, any ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// ai_set_in_scripted_cover (Sec33.2, 0x00a3c3a0). 1 string + 1 boolean,
// no nil-gate. Returns 1 boolean: false only if the character doesn't
// resolve (CONFIRMED). Double-gate setter on AI force-flag byte +0x2bc
// bit 0x1 (this project always applies the local write directly). When
// false, CONFIRMED: additionally queues scripted action 13 with no
// target.
// ---------------------------------------------------------------------
int stub_ai_set_in_scripted_cover(lua_State* L) {
    logCall(L, upLog(L), "ai_set_in_scripted_cover", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = lua_toboolean(L, 2) != 0; // CONFIRMED: no nil-gate
    EngineState* st = upState(L);
    // All four Sec33.2 functions resolve their character argument through
    // the established #FOLLOWER#-family character chain (Sec33.2's own
    // text) - wired 2026-10-08 (follower-sentinel-resolver pass).
    std::string resolvedName;
    bool resolved = resolveFollowerAwareObject(st, name, resolvedName); // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    if (resolved) {
        CharacterState& c = st->getOrCreateCharacter(resolvedName);
        c.inScriptedCover.set(value);
        EngineState::replicateStateChange("ai_set_in_scripted_cover", resolvedName);
        if (!value) c.scriptedAction.set(13); // CONFIRMED: queues scripted action 13, no target
    }
    lua_pushboolean(L, resolved ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// vehicle_exit_group_check_done (Sec30.5, 0x00a66b00). 1 table argument
// (same shape/reader as vehicle_exit_group_do above). Return: exactly 1
// boolean - CONFIRMED false if ANY listed character resolves
// (objectResolves()), is alive (CharacterState::isAlive, the shared "is
// dead/gone" predicate 0x0096f4f0 Sec30.5's own preamble names), and is in
// vehicle-state 2 or 3 (CharacterState::stateEnum); true otherwise,
// INCLUDING an empty or missing table (CONFIRMED) - "the polling partner"
// of vehicle_exit_group_do above.
// ---------------------------------------------------------------------
int stub_vehicle_exit_group_check_done(lua_State* L) {
    logCall(L, upLog(L), "vehicle_exit_group_check_done", upStateTag(L));
    std::vector<std::string> names = readCharacterNameArray(L, 1);
    EngineState* st = upState(L);
    for (const auto& name : names) {
        if (!st->objectResolves().get(name)) continue; // OPEN until set
        CharacterState& c = st->getOrCreateCharacter(name);
        if (!c.aliveByPredicate()) continue; // OPEN until set
        int32_t state = c.stateEnum.get(); // OPEN until set
        if (state == 2 || state == 3) {
            lua_pushboolean(L, 0);
            return 1;
        }
    }
    lua_pushboolean(L, 1);
    return 1;
}

// ---------------------------------------------------------------------
// team_make_unfriendly (Sec30.6, 0x00a60940). 2 mandatory strings (team
// names). No return. CONFIRMED: both names resolve via the established
// team-name-to-id helper 0x0094cc60 (EngineState::resolveTeamId() - see
// its own doc comment, engine_state.h, for why an unregistered name
// honestly takes the real CONFIRMED sentinel-9 "unknown name" path rather
// than refusing). 0x009b7130(id1, id2, 1, true) writes relation value 1
// symmetrically (EngineState::setTeamRelationIfInBounds(), including
// Sec30.6's own "team 5 mirrors team 6" refinement of Sec20.6).
//
// CONFIRMED crash-shaped edge, explicitly flagged by Sec30.7 (also
// Sec30.6's own text): an unrecognized name's sentinel id 9 indexes PAST
// the CONFIRMED 8x8 relation-matrix block - "a crash-shaped edge... OPEN,
// not confirmed" whether it truly faults. HOST-SAFETY DEVIATION (not a
// spec fact, per this task's own explicit instruction): either way, this
// host bounds-checks both ids first (setTeamRelationIfInBounds returns
// false on an out-of-range id) and skips the write entirely rather than
// performing the equivalent out-of-bounds C++ array write, logging the
// averted path via the same logCall/HitLog mechanism every stub already
// uses - "this project does not simulate crashes" (cf. group_get_next_npc
// above, Sec28.5, item 57, the established precedent for this exact
// idiom). Since this project has no real team-name registry (no spec
// gives the actual in-game team name strings/ids), every call this host
// sees in practice takes this guarded path unless a test explicitly
// registers both names' ids first - itself the honest behavior of a
// never-populated registry, not a simplification of the return contract
// (there is none; return is always none).
// ---------------------------------------------------------------------
int stub_team_make_unfriendly(lua_State* L) {
    logCall(L, upLog(L), "team_make_unfriendly", upStateTag(L));
    std::string team1 = argString(L, 1);
    std::string team2 = argString(L, 2);
    EngineState* st = upState(L);
    int id1 = st->resolveTeamId(team1);
    int id2 = st->resolveTeamId(team2);
    if (!st->setTeamRelationIfInBounds(id1, id2, 1)) {
        st->recordTeamRelationOobGuard(); // HOST-SAFETY: out-of-bounds team index guard, see comment above
        logCall(L, upLog(L), "team_make_unfriendly:OOB_TEAM_INDEX_GUARD", upStateTag(L));
        return 0;
    }
    EngineState::replicateStateChange("team_make_unfriendly", team1 + "/" + team2);
    return 0;
}

// ---------------------------------------------------------------------
// ai_pay_attention_to_position (Sec33.2, 0x00a3d720). 1 character string
// + 1 object string (general object resolver) + 1 optional boolean,
// default true. No return. CONFIRMED: locally copies the object's
// position into the character's "attention" sub-block and sets
// attention mode 7 (true) or 3 (false) - bit 0x4 of the mode IS the Lua
// boolean. This project has no real position data to copy (stated
// simplification - records only the resolved source object's own name).
// ---------------------------------------------------------------------
int stub_ai_pay_attention_to_position(lua_State* L) {
    logCall(L, upLog(L), "ai_pay_attention_to_position", upStateTag(L));
    std::string character = argString(L, 1);
    std::string object = argString(L, 2);
    bool value = true; // CONFIRMED default true
    if (lua_gettop(L) >= 3 && lua_type(L, 3) != LUA_TNIL) value = lua_toboolean(L, 3) != 0;
    EngineState* st = upState(L);
    // arg1 (character) resolves through the #FOLLOWER#-family chain
    // (Sec33.2's own text) - wired 2026-10-08 (follower-sentinel-resolver
    // pass). arg2 (object) resolves via the general object resolver
    // (unchanged, plain objectResolves()).
    std::string resolvedCharacter;
    bool resolved = resolveFollowerAwareObject(st, character, resolvedCharacter) && st->objectResolves().get(object); // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    if (resolved) {
        CharacterState& c = st->getOrCreateCharacter(resolvedCharacter);
        c.attentionSourceObjectName = object;
        c.attentionMode = value ? 7 : 3; // CONFIRMED mapping
        EngineState::replicateStateChange("ai_pay_attention_to_position", resolvedCharacter);
    }
    return 0;
}

// ---------------------------------------------------------------------
// ai_do_scripted_rush (Sec33.2, 0x00a3e870). 1 character string + 1
// optional target string, default none. Returns 1 boolean. CONFIRMED:
// with a target, always copies its position and starts scripted action
// 22 (no stated refusal condition on this path); without a target,
// accepted immediately if the current scripted action is already 22,
// else gated on reachability/distance/melee-engagement checks this
// project has no position/combat-state data to evaluate (no coordinate
// system, no engagement tracking anywhere in this project) - refused as
// OPEN rather than guessed.
// ---------------------------------------------------------------------
int stub_ai_do_scripted_rush(lua_State* L) {
    logCall(L, upLog(L), "ai_do_scripted_rush", upStateTag(L));
    std::string character = argString(L, 1);
    bool hasTarget = lua_gettop(L) >= 2 && lua_type(L, 2) != LUA_TNIL;
    std::string target = hasTarget ? argString(L, 2) : "";
    EngineState* st = upState(L);
    // Unchanged ungated shape for an ordinary name (deliberately NOT newly
    // gated, per this pass's own policy); only the 4 #FOLLOWER...# literals
    // (Sec33.2's own text) get real resolution - wired 2026-10-08,
    // follower-sentinel-resolver pass.
    std::string resolvedCharacter = character;
    if (isFollowerSentinelLiteral(character)) {
        resolveFollowerAwareObject(st, character, resolvedCharacter); // may throw OpenStateError for an unresolved-index literal; resolvedCharacter left unchanged (== the raw literal) if genuinely unresolved, matching this stub's own pre-existing "always touches, never gates" shape
    } else {
        st->objectResolves().get(character); // OPEN until set
    }
    CharacterState& c = st->getOrCreateCharacter(resolvedCharacter);
    bool result;
    if (hasTarget) {
        c.scriptedRushTargetName = target; // CONFIRMED: copies the target's position directly (no position data modelled - stores the name instead)
        c.scriptedAction.set(22);
        result = true;
    } else if (c.scriptedAction.known() && c.scriptedAction.get() == 22) {
        result = true; // CONFIRMED: "accepted immediately if the character's current scripted action is already 22"
    } else {
        throw OpenStateError("ai_do_scripted_rush reachability/distance/melee checks (no position data modelled)",
                             "spec-lua-api-behaviour.md Sec33.2");
    }
    lua_pushboolean(L, result ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// action_play_synced_do (Sec33.3, 0x00a3fe10). 2 mandatory actor-name
// strings + 1 mandatory synced-action-name string + 1 optional anchor-
// object string, default none. Returns 1 number: CONFIRMED -1 if both
// actors resolved and actor 1 is dead; else 1 (this project's own "no
// further real play-failure condition modelled" default - "what a null
// actor does inside the final play call" is itself OPEN per spec, and
// this project's resolve step already refuses rather than reaching a
// null-handle path). The synced-action name is looked up via the
// CORRECTED Sec14.1 mechanism (lookupSyncedActionIndex - 0x0095da50: a
// plain name -> index lookup, NOT a hidden-argument identity match); the
// further "cross-references a further table entry when found" detail is
// not modeled (no further table exists in this project's state). In
// multiplayer, CONFIRMED: always sends a record in addition to playing
// locally.
// ---------------------------------------------------------------------
int stub_action_play_synced_do(lua_State* L) {
    logCall(L, upLog(L), "action_play_synced_do", upStateTag(L));
    std::string actor1 = argString(L, 1);
    std::string actor2 = argString(L, 2);
    std::string actionName = argString(L, 3);
    std::string anchor = (lua_gettop(L) >= 4 && lua_type(L, 4) != LUA_TNIL) ? argString(L, 4) : "";
    (void)anchor; // CONFIRMED optional, used only for the anchor's transform, which this project does not track

    EngineState* st = upState(L);
    bool resolved = st->objectResolves().get(actor1) && st->objectResolves().get(actor2); // OPEN until set
    if (resolved && st->getOrCreateCharacter(actor1).isDeadHighConfidence) {
        lua_pushnumber(L, -1.0);
        return 1;
    }
    int32_t index = st->lookupSyncedActionIndex(actionName); // 0x0095da50 CORRECTED (Sec14.1/Sec33.3): plain name lookup
    (void)index; // no further synced-action table to cross-reference once found (not modelled)
    if (st->coopIsActive()) {
        EngineState::replicateStateChange("action_play_synced_do", actor1); // CONFIRMED: always sends an opcode-0x43 sub-code-19 record in multiplayer
    }
    lua_pushnumber(L, 1.0); // CONFIRMED: 1 when the final play call succeeds - no further failure condition modelled
    return 1;
}

// ---------------------------------------------------------------------
// action_play_directional_stumble_do (Sec33.3, 0x00a3f7d0). 1 character
// string + 1 reference-object string + 1 optional boolean (selects
// between two animation-id sets), default false. CONFIRMED: return is a
// variable number of values - pushes -1.0 for each individual failure
// (character unresolved; object unresolved/dead; character dead)
// followed by an unconditional final 1.0, the real return value being
// the push count; "nothing returns early." CONFIRMED: an unresolved
// character/object causes a real null-pointer-dereference crash in the
// shipped game - this project does not simulate crashes (same stated
// simplification as group_get_next_npc's own doc comment above) and
// instead continues to the unconditional final push like every other
// tracked failure. The "animation id not found" failure and the front/
// back heading comparison are NOT modeled (no animation-id table, no
// position/heading data anywhere in this project).
// ---------------------------------------------------------------------
int stub_action_play_directional_stumble_do(lua_State* L) {
    logCall(L, upLog(L), "action_play_directional_stumble_do", upStateTag(L));
    std::string character = argString(L, 1);
    std::string object = argString(L, 2);
    if (lua_gettop(L) >= 3 && lua_type(L, 3) != LUA_TNIL) (void)(lua_toboolean(L, 3) != 0); // read, no further modeled use (see doc comment)

    EngineState* st = upState(L);
    int pushed = 0;
    bool characterResolved = st->objectResolves().get(character); // OPEN until set
    if (!characterResolved) { lua_pushnumber(L, -1.0); ++pushed; }
    bool objectResolved = st->objectResolves().get(object); // OPEN until set
    bool objectDead = objectResolved && st->getOrCreateCharacter(object).isDeadHighConfidence;
    if (!objectResolved || objectDead) { lua_pushnumber(L, -1.0); ++pushed; }
    bool characterDead = characterResolved && st->getOrCreateCharacter(character).isDeadHighConfidence;
    if (characterDead) { lua_pushnumber(L, -1.0); ++pushed; }
    lua_pushnumber(L, 1.0); // CONFIRMED: unconditional final push
    ++pushed;
    return pushed;
}


} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB03() {
    static const std::vector<std::string> names = {
        // Batch 2026-10-02: spec-lua-bindings.md Sec18-Sec21 ("Vint UI
        // API"), all 8 `ui` in tools/lua_all_registered_1490_tagged.txt
        // (grepped directly, lines 1443/1446/1455/1458/1462/1464/1469/1475).
        "vint_object_first_child",
        "vint_object_clone",
        "vint_get_time_index",
        "vint_dataitem_get",
        "vint_set_property",
        "vint_get_property",
        "vint_dataresponder_finished",
        "vint_internal_dataresponder_request",
        // Batch 2026-10-02: spec-lua-api-behaviour.md Sec30 ("ranking
        // tranche 03"), 9 of its 25 names - all `gameplay` in
        // tools/lua_all_registered_1490_tagged.txt (grepped directly). See
        // this file's own Sec30 batch header comment above for which of
        // the 25 these are and why.
        "vehicle_set_invulnerable_to_player_explosives",
        "vehicle_disable_explosion_and_damage_vfx",
        "vehicle_set_special_override_never_ghost",
        "vehicle_clear_all_radio_locks",
        "vehicle_is_vtol",
        "auto_pickup_enable",
        "vehicle_exit_group_do",
        "vehicle_exit_group_check_done",
        "team_make_unfriendly",
        // Batch 2026-10-02: spec-lua-api-behaviour.md Sec33 ("ranking
        // tranche 05", 25 names), all `gameplay` in tools/
        // lua_all_registered_1490_tagged.txt (grepped directly, lines
        // 935/1000/1001/937/945/984/947/965/972/946/994/25/35/34/31/7/5/
        // 17/74/44/67/43/92/83/1003).
        "vehicle_is_helicopter",
        "vehicle_is_vtol_hover",
        "vehicle_is_vtol_jet",
        "vehicle_is_ready",
        "vehicle_never_flatten_tires",
        "vehicle_set_weapons_disarmed",
        "vehicle_set_no_chase",
        "vehicle_set_kneecappers",
        "vehicle_set_sirenlights",
        "vehicle_set_ambient",
        "vehicle_spotlight_is_target_spotted",
        "ai_clear_priority_target",
        "ai_set_in_scripted_cover",
        "ai_pay_attention_to_position",
        "ai_do_scripted_rush",
        "action_play_synced_do",
        "action_play_directional_stumble_do",
    };
    return names;
}

void registerB03(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    if (wants("vint_object_first_child")) registerOne(L, state, log, stateTag, "vint_object_first_child", openGuard<stub_vint_object_first_child>);
    if (wants("vint_object_clone")) registerOne(L, state, log, stateTag, "vint_object_clone", openGuard<stub_vint_object_clone>);
    if (wants("vint_get_time_index")) registerOne(L, state, log, stateTag, "vint_get_time_index", openGuard<stub_vint_get_time_index>);
    if (wants("vint_dataitem_get")) registerOne(L, state, log, stateTag, "vint_dataitem_get", openGuard<stub_vint_dataitem_get>);
    if (wants("vint_set_property")) registerOne(L, state, log, stateTag, "vint_set_property", openGuard<stub_vint_set_property>);
    if (wants("vint_get_property")) registerOne(L, state, log, stateTag, "vint_get_property", openGuard<stub_vint_get_property>);
    if (wants("vint_dataresponder_finished")) registerOne(L, state, log, stateTag, "vint_dataresponder_finished", openGuard<stub_vint_dataresponder_finished>);
    if (wants("vint_internal_dataresponder_request")) registerOne(L, state, log, stateTag, "vint_internal_dataresponder_request", openGuard<stub_vint_internal_dataresponder_request>);

    // Batch 2026-10-02: spec-lua-api-behaviour.md Sec30 ("ranking tranche 03"), 9 names.
    if (wants("vehicle_set_invulnerable_to_player_explosives")) registerOne(L, state, log, stateTag, "vehicle_set_invulnerable_to_player_explosives", openGuard<stub_vehicle_set_invulnerable_to_player_explosives>);
    if (wants("vehicle_disable_explosion_and_damage_vfx")) registerOne(L, state, log, stateTag, "vehicle_disable_explosion_and_damage_vfx", openGuard<stub_vehicle_disable_explosion_and_damage_vfx>);
    if (wants("vehicle_set_special_override_never_ghost")) registerOne(L, state, log, stateTag, "vehicle_set_special_override_never_ghost", openGuard<stub_vehicle_set_special_override_never_ghost>);
    if (wants("vehicle_clear_all_radio_locks")) registerOne(L, state, log, stateTag, "vehicle_clear_all_radio_locks", openGuard<stub_vehicle_clear_all_radio_locks>);
    if (wants("vehicle_is_vtol")) registerOne(L, state, log, stateTag, "vehicle_is_vtol", openGuard<stub_vehicle_is_vtol>);
    if (wants("auto_pickup_enable")) registerOne(L, state, log, stateTag, "auto_pickup_enable", openGuard<stub_auto_pickup_enable>);
    if (wants("vehicle_exit_group_do")) registerOne(L, state, log, stateTag, "vehicle_exit_group_do", openGuard<stub_vehicle_exit_group_do>);
    if (wants("vehicle_exit_group_check_done")) registerOne(L, state, log, stateTag, "vehicle_exit_group_check_done", openGuard<stub_vehicle_exit_group_check_done>);
    if (wants("team_make_unfriendly")) registerOne(L, state, log, stateTag, "team_make_unfriendly", openGuard<stub_team_make_unfriendly>);
    // Batch 2026-10-02: spec-lua-api-behaviour.md Sec33 (25 names).
    if (wants("vehicle_is_helicopter")) registerOne(L, state, log, stateTag, "vehicle_is_helicopter", openGuard<stub_vehicle_is_helicopter>);
    if (wants("vehicle_is_vtol_hover")) registerOne(L, state, log, stateTag, "vehicle_is_vtol_hover", openGuard<stub_vehicle_is_vtol_hover>);
    if (wants("vehicle_is_vtol_jet")) registerOne(L, state, log, stateTag, "vehicle_is_vtol_jet", openGuard<stub_vehicle_is_vtol_jet>);
    if (wants("vehicle_is_ready")) registerOne(L, state, log, stateTag, "vehicle_is_ready", openGuard<stub_vehicle_is_ready>);
    if (wants("vehicle_never_flatten_tires")) registerOne(L, state, log, stateTag, "vehicle_never_flatten_tires", openGuard<stub_vehicle_never_flatten_tires>);
    if (wants("vehicle_set_weapons_disarmed")) registerOne(L, state, log, stateTag, "vehicle_set_weapons_disarmed", openGuard<stub_vehicle_set_weapons_disarmed>);
    if (wants("vehicle_set_no_chase")) registerOne(L, state, log, stateTag, "vehicle_set_no_chase", openGuard<stub_vehicle_set_no_chase>);
    if (wants("vehicle_set_kneecappers")) registerOne(L, state, log, stateTag, "vehicle_set_kneecappers", openGuard<stub_vehicle_set_kneecappers>);
    if (wants("vehicle_set_sirenlights")) registerOne(L, state, log, stateTag, "vehicle_set_sirenlights", openGuard<stub_vehicle_set_sirenlights>);
    if (wants("vehicle_set_ambient")) registerOne(L, state, log, stateTag, "vehicle_set_ambient", openGuard<stub_vehicle_set_ambient>);
    if (wants("vehicle_spotlight_is_target_spotted")) registerOne(L, state, log, stateTag, "vehicle_spotlight_is_target_spotted", openGuard<stub_vehicle_spotlight_is_target_spotted>);
    if (wants("ai_clear_priority_target")) registerOne(L, state, log, stateTag, "ai_clear_priority_target", openGuard<stub_ai_clear_priority_target>);
    if (wants("ai_set_in_scripted_cover")) registerOne(L, state, log, stateTag, "ai_set_in_scripted_cover", openGuard<stub_ai_set_in_scripted_cover>);
    if (wants("ai_pay_attention_to_position")) registerOne(L, state, log, stateTag, "ai_pay_attention_to_position", openGuard<stub_ai_pay_attention_to_position>);
    if (wants("ai_do_scripted_rush")) registerOne(L, state, log, stateTag, "ai_do_scripted_rush", openGuard<stub_ai_do_scripted_rush>);
    if (wants("action_play_synced_do")) registerOne(L, state, log, stateTag, "action_play_synced_do", openGuard<stub_action_play_synced_do>);
    if (wants("action_play_directional_stumble_do")) registerOne(L, state, log, stateTag, "action_play_directional_stumble_do", openGuard<stub_action_play_directional_stumble_do>);
}

} // namespace stub_sections
} // namespace sr3luahost
