// lua_spec_confirmed_stubs_b10_batch3_pcr_vehicle.cpp
//
// Wave 1, Batch 3 of a 7-agent parallel fan-out implementing cleared-but-
// unregistered Lua natives (results/lua_natives_sec10_28_fanout_scoping_
// 20261008.md). This file is NEW - per that scoping report's own "do NOT
// retrofit into b01-b07" guidance, it follows the exact same pattern as the
// 7 existing lua_spec_confirmed_stubs_b0N_*.cpp files (own anonymous-
// namespace stub_* functions, own namesB10()/registerB10() pair declared in
// lua_spec_confirmed_stubs_sections.h, called from
// lua_spec_confirmed_stubs_registry.cpp).
//
// Scope: spec-lua-api-behaviour.md Sec11 ("ranks 151-250 tranche, part D" -
// 12 UI-cluster PCR/PCU/store/pause-menu functions) + Sec12 ("ranks 151-250
// tranche, part A" - 28 of its 29 gameplay-cluster functions; the 29th,
// traffic_disable_lanes/Sec12.19, is already registered in
// lua_spec_confirmed_stubs_b06_playerrt_sec6_7.cpp, Section 6 batch). 40
// names total. See each stub's own doc comment below for its exact spec
// citation; see engine_state.h's own "Batch 3" doc comments (CharacterState/
// VehicleState/EngineState) for the full per-field reasoning the new state
// below rests on.
//
// HARDENING POLICY: none of this batch's 40 named natives are on the
// scoping report's own flagged memory-safety list (dialog_box_create_
// internal/gang_customization_*/vcust_purchase_component/cell_playlist_
// save_list/dlc2_m02_clapboards_set - all in later, unimplemented sections),
// and no genuine memory-safety bug (unbounded copy, OOB read/write,
// arbitrary-address write) was found in any of the 40 functions' own spec
// text while implementing this batch - every OPEN internal below is a
// missing/undecompiled detail, not a confirmed crash shape, so no
// HARDENED-divergence counter was needed this pass.
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

#include <cctype>

namespace sr3luahost {
namespace {

// Portable ASCII lowercase - used only to build the case-insensitive lookup
// keys this batch's own test-populated registries (PcrMorphState::
// sessionResolves/slotResolves, StoreCameraPreset::presetResolves) are keyed
// by. Not a claim about the real engine's own CRT lowering routine (same
// "portable, behaviorally equivalent stand-in" caveat as caseInsensitiveEquals
// in lua_spec_confirmed_stubs_common.h).
std::string toLowerAscii(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// =========================================================================
// Section 11 ("ranks 151-250 tranche, part D", 12 UI-cluster functions).
// =========================================================================

// pcr_is_plastic_surgeon (Sec11.1, 0x00830d10). No arguments. Return: 1
// boolean. CONFIRMED: pushes a single global byte flag directly, no other
// logic. OPEN until a test sets EngineState::pcrPlasticSurgeonMode().
int stub_pcr_is_plastic_surgeon(lua_State* L) {
    logCall(L, upLog(L), "pcr_is_plastic_surgeon", upStateTag(L));
    lua_pushboolean(L, upState(L)->pcrPlasticSurgeonMode().get() ? 1 : 0); // OPEN until set
    return 1;
}

// pcr_mark_changed (Sec11.2, 0x00831020, force-disassembled). No arguments,
// no return. CONFIRMED: sets a single global byte flag to 1, unconditionally
// - no gate, so a plain bool (not OpenValue) is the honest model.
int stub_pcr_mark_changed(lua_State* L) {
    logCall(L, upLog(L), "pcr_mark_changed", upStateTag(L));
    upState(L)->pcrDirty() = true;
    return 0;
}

// pcr_apply_morph_value (Sec11.3, 0x00834fe0). 3 arguments, all read
// positionally and unconditionally (no nil-gate). Arg 1 (string) - session/
// context identifier, resolved via a case-insensitive scan of a small named
// table (0x00831650, contents not given in scope - see PcrMorphState's own
// doc comment, engine_state.h). Arg 2 - type-polymorphic: a number reads as
// an index, anything else (including absent) reads as a category name. Arg
// 3 (number) - the morph blend value, floor-clamped to ~0.0001 when it
// crosses an extreme negative sentinel threshold (CONFIRMED the clamp
// target, ~roughly the threshold per the spec's own "roughly" phrasing - not
// an exact bit pattern given in scope, and dead for any realistic Lua input
// either way). No return.
//
// CONFIRMED on success: writes the (possibly-clamped) value into this
// project's own flat per-(session,slot) store (PcrMorphState::values,
// standing in for the real live morph-value array at the shared
// customization-target singleton's own +0x2088 field) and marks the
// session's pending-recompute dedup flag. The "body gender" literal-name
// midpoint-threshold mesh/body-reload chain (0x00821740/0x00822a00/
// 0x008214c0/0x00821630/0x009dd4a0, gated by a separate pending-flag byte)
// is OPEN past its own trigger condition - this project builds no asset-
// reload pipeline anywhere in scope, so it is not modeled (a stated gap, not
// an invented effect). An unresolved session or slot is the CONFIRMED
// "nothing further happens" case (Sec11.3's own text), not an OPEN refusal -
// both registries are test-populated presence maps (see PcrMorphState's own
// doc comment), checked via .known()+.get() rather than a bare .get() that
// could throw on a merely-never-populated key.
int stub_pcr_apply_morph_value(lua_State* L) {
    logCall(L, upLog(L), "pcr_apply_morph_value", upStateTag(L));
    std::string sessionLower = toLowerAscii(argString(L, 1));
    std::string slotKey;
    if (lua_type(L, 2) == LUA_TNUMBER) {
        int64_t idx = truncateEa2596(lua_tonumber(L, 2));
        slotKey = "idx:" + std::to_string(idx);
    } else {
        slotKey = "cat:" + toLowerAscii(argString(L, 2)); // absent/non-string -> "" category (CONFIRMED NULL-safe read)
    }
    double value = lua_tonumber(L, 3);
    constexpr double kClampThreshold = -3.69e19; // APPROXIMATE per spec's own "roughly" phrasing (Sec11.3) - not an exact bit pattern
    if (value < kClampThreshold) value = 0.0001; // CONFIRMED clamp target (practically dead for normal input)

    EngineState* st = upState(L);
    auto& morph = st->pcrMorph();
    if (!morph.sessionResolves.known(sessionLower) || !morph.sessionResolves.get(sessionLower)) return 0; // CONFIRMED no-op
    std::string compositeKey = EngineState::pcrMorphKey(sessionLower, slotKey);
    if (!morph.slotResolves.known(compositeKey) || !morph.slotResolves.get(compositeKey)) return 0; // CONFIRMED no-op
    morph.values[compositeKey] = value;
    morph.pendingRecompute[sessionLower] = true; // idempotent dedup (CONFIRMED "queue this, no-op if already queued")
    return 0;
}

// pcr_get_morph_value (Sec11.4, 0x00832880). 2 arguments, same shape as
// Sec11.3's first two. Return: 1 value (the current blend value) on success,
// or ZERO Lua values (no push at all) on any resolution failure - a real,
// confirmed asymmetry from the setter (which always returns nothing
// regardless of success). Mirrors the setter's own resolver exactly (see its
// doc comment above); the read-side "returns 0.0 if either pointer is null"
// internal detail (0x009f5c00) is honored by this project's own default-0.0
// read of a never-written (session,slot) pair that otherwise both resolve.
int stub_pcr_get_morph_value(lua_State* L) {
    logCall(L, upLog(L), "pcr_get_morph_value", upStateTag(L));
    std::string sessionLower = toLowerAscii(argString(L, 1));
    std::string slotKey;
    if (lua_type(L, 2) == LUA_TNUMBER) {
        int64_t idx = truncateEa2596(lua_tonumber(L, 2));
        slotKey = "idx:" + std::to_string(idx);
    } else {
        slotKey = "cat:" + toLowerAscii(argString(L, 2));
    }
    EngineState* st = upState(L);
    auto& morph = st->pcrMorph();
    if (!morph.sessionResolves.known(sessionLower) || !morph.sessionResolves.get(sessionLower)) return 0; // CONFIRMED: zero values
    std::string compositeKey = EngineState::pcrMorphKey(sessionLower, slotKey);
    if (!morph.slotResolves.known(compositeKey) || !morph.slotResolves.get(compositeKey)) return 0; // CONFIRMED: zero values
    auto it = morph.values.find(compositeKey);
    lua_pushnumber(L, it != morph.values.end() ? it->second : 0.0); // 0.0 default matches 0x009f5c00's own null-pointer fallback
    return 1;
}

// pcu_is_streaming_done (Sec11.5, 0x0082ad90). No arguments. Return: 1
// boolean. CONFIRMED: two independent pending-work checks ANDed together -
// pushes true only when BOTH report clear. Both OPEN until a test sets
// EngineState::pcuStreaming().
int stub_pcu_is_streaming_done(lua_State* L) {
    logCall(L, upLog(L), "pcu_is_streaming_done", upStateTag(L));
    auto& st = *upState(L);
    bool rowPending = st.pcuStreaming().rowCounterPending.get();       // OPEN until set
    bool listPending = st.pcuStreaming().listMembershipPending.get();  // OPEN until set
    lua_pushboolean(L, (!rowPending && !listPending) ? 1 : 0);
    return 1;
}

// horde_mode_is_active (Sec11.6, 0x00842ad0, 113-entry loop registrar
// 0x00845aa0). No arguments. Return: 1 boolean. CONFIRMED: a 1-line helper
// (0x006e44f0) pushes a single global byte directly. OPEN until set.
int stub_horde_mode_is_active(lua_State* L) {
    logCall(L, upLog(L), "horde_mode_is_active", upStateTag(L));
    lua_pushboolean(L, upState(L)->hordeModeActive().get() ? 1 : 0); // OPEN until set
    return 1;
}

// game_audio_stop (Sec11.7, 0x00843ac0, force-disassembled, 113-entry loop
// registrar 0x00845aa0). 1 mandatory number (audio-instance id, round-to-
// int). No return. CONFIRMED: the real worker (0x0045f1a0) only mutates a
// throwaway local copy of the id and two internal status bits of a
// secondary/legacy audio-instance mechanism - "because the caller only ever
// passes a throwaway local copy of the id, this self-clearing has no effect
// visible outside the call." This project builds no registry for that
// mechanism anywhere in scope (distinct from audio_stop's own fixed 32-slot
// table, Sec8.9/Sec11.7's own cross-reference) - call-count only.
int stub_game_audio_stop(lua_State* L) {
    logCall(L, upLog(L), "game_audio_stop", upStateTag(L));
    (void)truncateEa2596(lua_tonumber(L, 1)); // 1 mandatory number, round-to-int (CONFIRMED)
    upState(L)->recordGameAudioStop();
    return 0;
}

// get_localized_crc_for_tag (Sec11.8, 0x00844730, force-disassembled,
// 113-entry loop registrar 0x00845aa0). 1 mandatory string. Return: 1 number
// (unsigned-32-to-double sign correction). CONFIRMED: resolves the tag via
// the engine's general string hash against a real 14-shard existence table,
// "returning 0 instead of the raw hash if the tag isn't genuinely
// registered." This project has no real localization-tag registry anywhere
// in scope (same "never-populated registry is the honest default" class as
// resolveTeamId()'s team-name table) - every tag is therefore honestly
// "not registered" here, so this always takes the CONFIRMED 0-return path;
// no hash value is invented for any tag.
int stub_get_localized_crc_for_tag(lua_State* L) {
    logCall(L, upLog(L), "get_localized_crc_for_tag", upStateTag(L));
    (void)argString(L, 1); // 1 mandatory string, consumed - no real tag registry in scope (see doc comment)
    lua_pushnumber(L, 0.0); // CONFIRMED fallback for "not genuinely registered" - this project registers none
    return 1;
}

// store_common_mouse_zoom (Sec11.9, 0x0080dcd0). 1 optional number, standard
// nil-gated idiom, default 1.0. No return. CONFIRMED: a shared input-
// snapshot helper's 3rd output value is scaled by a fixed -0.05 multiplier
// and this argument, then accumulated into a single global pending camera-
// zoom delta. This project has no real mouse-delta-capture layer anywhere in
// scope (the SAME stated gap store_common_rotate_mouse_drag's own doc
// comment already establishes for its own 3 snapshot values, sharing this
// function's own wrapper per Sec11.9's text) - the unavailable snapshot
// value is CHOSEN 0.0 (same convention, not a fresh invention), so the
// accumulator's real numeric contribution is always 0; the write path
// itself is still tracked for test observability.
int stub_store_common_mouse_zoom(lua_State* L) {
    logCall(L, upLog(L), "store_common_mouse_zoom", upStateTag(L));
    double arg = optionalNumberDefault(L, 1, 1.0);
    auto& drag = upState(L)->storeCommonRotateMouseDrag();
    constexpr double kUnavailableSnapshotValue = 0.0; // CHOSEN, see doc comment above
    drag.pendingZoomDelta += kUnavailableSnapshotValue * -0.05 * arg;
    ++drag.zoomCallCount;
    return 0;
}

// store_set_camera_pos (Sec11.10, 0x00830670). 1 mandatory string, read
// unconditionally - a named camera-preset/view identifier. No return.
// CONFIRMED: resolves via a hash+linear-scan against a fixed preset table
// whose real row contents this spec text does not supply (test-populated
// presence only, see StoreCameraPreset's own doc comment) - a resolved
// preset moves/aims the store/character-preview camera (not reproduced; no
// camera/world-position system in scope). An unresolved preset name is the
// CONFIRMED "no match" case, not an OPEN refusal.
int stub_store_set_camera_pos(lua_State* L) {
    logCall(L, upLog(L), "store_set_camera_pos", upStateTag(L));
    std::string name = argString(L, 1);
    auto& preset = upState(L)->storeCameraPreset();
    if (preset.presetResolves.known(name) && preset.presetResolves.get(name)) {
        preset.lastAppliedPreset = name;
        ++preset.appliedCount;
    }
    return 0;
}

// pause_menu_accept_options (Sec11.11, 0x007e0430, force-disassembled). No
// arguments, no return. CONFIRMED: a 3-way AND gate (one thunked predicate +
// two flag globals, all individually OPEN) selects whether pending options
// changes commit - collapsed to one gate field (PauseMenuOptions::
// acceptGatePasses), OPEN until a test sets it. On pass: marks the change
// committed (CONFIRMED effect); the trailing routine (0x00705f30) whose
// return is discarded by the Lua wrapper is not modeled.
int stub_pause_menu_accept_options(lua_State* L) {
    logCall(L, upLog(L), "pause_menu_accept_options", upStateTag(L));
    auto& opts = upState(L)->pauseMenuOptions();
    ++opts.acceptCalls;
    if (opts.acceptGatePasses.get()) { // OPEN until set
        opts.committed = true;
    }
    return 0;
}

// vint_options_remap_set_key_binding (Sec11.12, 0x007cd3d0, force-
// disassembled). 3 mandatory arguments, all read unconditionally: arg 1
// (number, rounded, decremented by 1 - 1-based-to-0-based action-slot
// index), arg 2 (number, rounded - raw key/button device code), arg 3
// (boolean). No return. CONFIRMED structural pre-check: a key code of the
// sentinel -1 is a clean no-op with NO gate even consulted. Otherwise,
// proceeds only if a gate (0x007c2060(0), HIGH CONFIDENCE "is a remap
// confirmation dialog already open") is FALSE - OPEN until set. On success:
// stages the pending (action, key, flag) triple and raises a localized
// confirmation dialog (opcode 500) - the actual rebind write happens
// elsewhere, not traced, so only the staging+dialog-raise is modeled.
int stub_vint_options_remap_set_key_binding(lua_State* L) {
    logCall(L, upLog(L), "vint_options_remap_set_key_binding", upStateTag(L));
    int32_t actionIndex0Based = static_cast<int32_t>(truncateEa2596(lua_tonumber(L, 1))) - 1;
    int32_t keyCode = static_cast<int32_t>(truncateEa2596(lua_tonumber(L, 2)));
    bool flag = lua_toboolean(L, 3) != 0;
    if (keyCode == -1) return 0; // CONFIRMED sentinel skip, no gate consulted
    EngineState* st = upState(L);
    auto& staging = st->vintRemapStaging();
    if (!staging.dialogAlreadyOpen.get()) { // OPEN until set
        staging.hasPending = true;
        staging.pendingActionIndex0Based = actionIndex0Based;
        staging.pendingKeyCode = keyCode;
        staging.pendingFlag = flag;
        ++staging.dialogRaisedCount;
    }
    return 0;
}

// =========================================================================
// Section 12 ("ranks 151-250 tranche, part A", 28 of 29 gameplay-cluster
// functions - traffic_disable_lanes/Sec12.19 already registered in
// lua_spec_confirmed_stubs_b06_playerrt_sec6_7.cpp).
// =========================================================================

// hud_critical_timer_stop (Sec12.1, 0x00a4ed00). No arguments consumed
// (shared lua_gettop prologue only). No return. CONFIRMED: a zero-argument
// internal helper (0x006d0dc0, OPEN - a single fixed widget slot, since no
// numeric slot-id argument is ever read) runs, then the shared stale-handle-
// clear idiom runs with tag 0xc - this project builds no HUD timer/bar slot-
// array infrastructure yet (same deferred-infra note as hud_timer_set/stop,
// Sec8.19/8.20). Call-count only.
int stub_hud_critical_timer_stop(lua_State* L) {
    logCall(L, upLog(L), "hud_critical_timer_stop", upStateTag(L));
    upState(L)->recordHudCriticalTimerStop();
    return 0;
}

// audio_play_for_mission_cellphone (Sec12.2, 0x00a3daa0). Up to 6 arguments:
// arg 1 (string, mandatory) - a cellphone-call/audio identifier; arg 2
// (boolean, optional, default false); arg 3 (boolean, optional, default
// true); arg 4 (string, optional, default "") - a callback name; arg 5
// (string, optional, default "") - a second callback name; arg 6 (number/
// flag, optional, default 0). No return. CONFIRMED: allocates/creates a
// mission-cellphone-audio record (0x00a38f30, OPEN layout) and registers up
// to two named Lua callbacks into its own two hook slots, reusing the SAME
// callback-name->hook-reference primitive the character event-hook family
// uses elsewhere - here on a NEW record kind (kind tag 1). Args 2/3/6 are
// consumed per the confirmed shape but not modeled further (no in-scope
// consumer reads them back).
int stub_audio_play_for_mission_cellphone(lua_State* L) {
    logCall(L, upLog(L), "audio_play_for_mission_cellphone", upStateTag(L));
    std::string id = argString(L, 1);
    (void)optionalBoolDefault(L, 2, false);
    (void)optionalBoolDefault(L, 3, true);
    std::string cb1 = optionalStringDefault(L, 4, "");
    std::string cb2 = optionalStringDefault(L, 5, "");
    (void)optionalNumberDefault(L, 6, 0.0);
    auto& record = upState(L)->missionCellphoneAudio(id);
    if (!cb1.empty()) record.hookSlot1 = cb1;
    if (!cb2.empty()) record.hookSlot2 = cb2;
    return 0;
}

// inv_item_remove_all (Sec12.3, 0x00a50be0). 1 optional string, standard
// nil-gated idiom, default the literal "#PLAYER1#" (a distinct default from
// the resolver family's own "#PLAYER#" sentinel). No return. CONFIRMED:
// resolves through the generic/#PLAYER# family, then an internal dispatch
// (0x00602b80, OPEN - whether the literal 0 argument selects a mode) runs on
// success. Same "resolve-only, no modeled field" precedent as
// character_show (Sec7.15) - this project tracks no real per-character
// inventory contents anywhere in scope.
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_inv_item_remove_all(lua_State* L) {
    logCall(L, upLog(L), "inv_item_remove_all", upStateTag(L));
    std::string name = optionalStringDefault(L, 1, "#PLAYER1#"); // CONFIRMED distinct default literal (Sec12.3)
    EngineState* st = upState(L);
    std::string resolvedName;
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        (void)st->getOrCreateCharacter(resolvedName);
    }
    return 0;
}

// character_set_skydiving_state (Sec12.4, 0x00a45380). Up to 5 arguments:
// arg 1 (string, mandatory) - character name; arg 2 (number, mandatory,
// round-to-int) - skydiving state code; arg 3 (boolean, optional, default
// false); arg 4 (string, optional, default absent) - secondary named
// object; arg 5 (boolean, optional, default false). No return. CONFIRMED:
// arg 1 resolves through the generic/#PLAYER# family; arg 4, if given, ALSO
// resolves the same way into a second handle; a setter (0x009aee20, OPEN
// internal) is called unconditionally with the state code. A SEPARATE
// class-descriptor-gated call (0x009e09b0, bit 0x2@+0xa - OPEN, this
// project models no per-object-type class-descriptor table anywhere in
// scope) is not modeled.
// Resolver 0x00a281a0 (both arg 1 and, when given, arg 4) - RE-CORRECTED
// (2026-10-08, direct disassembly of its own body): a two-stage
// #PLAYER#-then-#FOLLOWER# wrapper, not PLAYER-only as an earlier reading
// had it - wired here, follower-sentinel-resolver pass, via
// resolveFollowerAwareObject().
int stub_character_set_skydiving_state(lua_State* L) {
    logCall(L, upLog(L), "character_set_skydiving_state", upStateTag(L));
    std::string name = argString(L, 1);
    int64_t stateCode = truncateEa2596(lua_tonumber(L, 2));
    bool arg3 = optionalBoolDefault(L, 3, false);
    std::string secondaryName = optionalStringDefault(L, 4, "");
    bool arg5 = optionalBoolDefault(L, 5, false);
    (void)arg3;
    (void)arg5; // consumed per confirmed arg shape, not modeled further (class-descriptor-gated call is OPEN)
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    if (!secondaryName.empty()) {
        std::string resolvedSecondary;
        (void)resolveFollowerAwareObject(st, secondaryName, resolvedSecondary); // resolved per spec, 2nd handle not modeled further
    }
    st->getOrCreateCharacter(resolvedName).skydivingStateCode = static_cast<int32_t>(stateCode);
    return 0;
}

// character_hide (Sec12.5, 0x00a45080). 1 mandatory string, no nil-gate. No
// return. CONFIRMED: resolves, then calls the SAME visibility-toggle
// primitive (and the SAME "hide" mode pair) mesh_mover_hide (Sec9.16)
// already uses, here on the character itself. The attached-object linked-
// list walk (bit 0x8@+6 class-descriptor gate, hiding "attached items/
// weapons" too) is OPEN - this project has no per-character attached-object
// list anywhere in scope, so only the character's own hide flag is tracked.
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_hide(lua_State* L) {
    logCall(L, upLog(L), "character_hide", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        st->getOrCreateCharacter(resolvedName).hidden = true;
    }
    return 0;
}

// vehicle_set_smoke_and_fire_state (Sec12.6, 0x00a63230). 3 mandatory
// arguments, no nil-gates: arg 1 (string) - vehicle reference; arg 2
// (boolean) - smoke on/off; arg 3 (boolean) - fire on/off. No return.
// CONFIRMED: resolves via the dedicated vehicle resolver (this project's
// shared objectResolves() stand-in, no separate liveness check needed, same
// as every other vehicle function in scope), then an unconditional dispatch
// (0x00aa3f90, OPEN internal, 2 trailing literal zeros not modeled) sets
// both visual-state flags independently.
int stub_vehicle_set_smoke_and_fire_state(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_smoke_and_fire_state", upStateTag(L));
    std::string name = argString(L, 1);
    bool smoke = lua_toboolean(L, 2) != 0;
    bool fire = lua_toboolean(L, 3) != 0;
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        VehicleState& v = st->getOrCreateVehicle(name);
        v.smokeOn = smoke;
        v.fireVisualOn = fire;
    }
    return 0;
}

// set_always_sees_player_flag (Sec12.7, 0x00a5cc80). 1 mandatory string + 1
// optional boolean, standard nil-gated idiom, default true. No return.
// CONFIRMED: resolves through 0x00a28150, "a member of the #PLAYER#/
// #FOLLOWER#-chain resolver family" (Sec12.7's own text) - wired 2026-10-08
// (follower-sentinel-resolver pass) via resolveFollowerAwareObject() in
// place of a plain objectResolves() stand-in - then a setter (0x004dfc80,
// OPEN internal/hidden-argument convention) runs with the boolean.
int stub_set_always_sees_player_flag(lua_State* L) {
    logCall(L, upLog(L), "set_always_sees_player_flag", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        st->getOrCreateCharacter(resolvedName).alwaysSeesPlayer = value;
    }
    return 0;
}

// inv_weapon_disable_all_but_this_slot (Sec12.8, 0x00a51270). Up to 3
// arguments: arg 1 (number, mandatory, round-to-int) - a weapon-slot index;
// arg 2 (boolean, optional, default false); arg 3 (number/flag, optional,
// default 3). No return. CONFIRMED: arg 1 clamps 0-7 to itself, anything
// else to sentinel 0xff (real effect OPEN - "not traced"), then a dispatch
// (0x007f4650, OPEN internal) runs. CONFIRMED side effect: "disables every
// inventory weapon slot except the given slot." Implicitly the local player
// (no name argument) - this project's single-player-only scope. Arg 2's own
// role in the dispatch is OPEN; consumed per the confirmed shape, not
// modeled further.
int stub_inv_weapon_disable_all_but_this_slot(lua_State* L) {
    logCall(L, upLog(L), "inv_weapon_disable_all_but_this_slot", upStateTag(L));
    int64_t slotRaw = truncateEa2596(lua_tonumber(L, 1));
    (void)optionalBoolDefault(L, 2, false); // OPEN role in the dispatch, consumed not modeled
    (void)optionalNumberDefault(L, 3, 3.0);
    auto& slots = upState(L)->weaponSlots();
    if (slotRaw >= 0 && slotRaw <= 7) {
        for (size_t i = 0; i < slots.disabled.size(); ++i) {
            slots.disabled[i] = (static_cast<int64_t>(i) != slotRaw); // CONFIRMED: every OTHER slot disabled, this one enabled
        }
    } else {
        ++slots.sentinelOobCount; // sentinel 0xff case - real effect OPEN, not guessed
    }
    return 0;
}

// get_char_vehicle_name (Sec12.9, 0x00a4b220). 1 mandatory string, no nil-
// gate - a character name. Return: 1 string via lua_pushstring. CONFIRMED:
// resolves the character, then reads its cached "associated vehicle" field
// pair (+0x16c0/+0x16c4 - this project's EXISTING CharacterState::
// currentVehicleName) through a lookup (0x00458230, OPEN); on success,
// gated by two further class-descriptor/liveness checks (both OPEN - a bit/
// offset combination not independently resolved this pass), extracts the
// vehicle's name. Any failure at any step falls through to a fixed fallback
// string (0x0129a0e3, the SAME "empty/sentinel string" character_exists's
// own resolver anomaly, Sec7.9, already cites) - this project reads that
// sentinel as the empty string "". CHOSEN simplification: a KNOWN cached
// vehicle association is trusted directly, skipping the further OPEN
// liveness/class-bit gates (same "resolve + trust the one CONFIRMED field,
// skip the unmodeled deeper gates" precedent as several other functions in
// this project).
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_get_char_vehicle_name(lua_State* L) {
    logCall(L, upLog(L), "get_char_vehicle_name", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    std::string result; // "" = the CONFIRMED fallback sentinel (0x0129a0e3)
    std::string resolvedName;
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        CharacterState& c = st->getOrCreateCharacter(resolvedName);
        if (c.currentVehicleName.known()) {
            const std::string& v = c.currentVehicleName.get();
            if (!v.empty()) result = v; // CHOSEN: trust a known association (see doc comment)
        }
    }
    lua_pushstring(L, result.c_str());
    return 1;
}

// effect_play_on_script_object (Sec12.10, 0x00a491b0). Up to 6 arguments:
// arg 1 (string, mandatory) - effect name; arg 2 (string, mandatory) -
// target script-object name; arg 3 (string, optional, default absent) -
// attachment-point name; arg 4 (boolean, optional, default false); arg 5
// (number/flag, optional, default 3); arg 6 (number, optional, default 1).
// Return: 1 number - the SAME -1.0 sentinel (kActionPlaySentinelResolveFail)
// effect_play (Sec9.1, already implemented) uses on failure; on success, a
// fresh handle from the SAME shared counter (nextVisualEffectHandle())
// stands in for the real terminal dispatch's own forwarded return value
// (0x00a48af0, OPEN - not independently reconstructed, same stand-in
// effect_play already established). CONFIRMED the effect-name resolves
// against the SAME fixed singleton registry effect_play already uses (one
// shared, kind-spanning name registry, per this document's own raw-
// disassembly confirmation) - reuses the SAME objectResolves() gate for
// consistency, not a second parallel resolver. The target-object resolve,
// attachment-point resolution (two different mechanisms depending on
// target capability) and the finalizer's own internal fields are OPEN - not
// reproduced (this project has no attachment-point/anchor-collection system
// anywhere in scope).
int stub_effect_play_on_script_object(lua_State* L) {
    logCall(L, upLog(L), "effect_play_on_script_object", upStateTag(L));
    std::string effectName = argString(L, 1);
    std::string targetName = argString(L, 2);
    (void)optionalStringDefault(L, 3, ""); // attach-point name, consumed - no anchor-collection system in scope
    (void)optionalBoolDefault(L, 4, false);
    (void)optionalNumberDefault(L, 5, 3.0);
    (void)optionalNumberDefault(L, 6, 1.0);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(effectName) || !st->objectResolves().get(targetName)) { // OPEN until set
        lua_pushnumber(L, EngineState::kActionPlaySentinelResolveFail);
        return 1;
    }
    lua_pushnumber(L, static_cast<lua_Number>(st->nextVisualEffectHandle()));
    return 1;
}

// helicopter_fly_to_do (Sec12.11, 0x00a4ff20). Up to 11 arguments: arg 1
// (string, mandatory) - helicopter/vehicle reference; arg 2 (number,
// mandatory); arg 3 (boolean, mandatory); arg 4 (string, mandatory) -
// secondary named destination; arg 5 (boolean, mandatory); arg 6 (string,
// optional, default ""); arg 7 (number, optional, default 0.0); args 8-11
// (booleans, each optional, default false). Return: 1 boolean - the real
// success/failure of the final dispatch. CONFIRMED: arg 1 resolves through
// the vehicle resolver and a helicopter-specific qualifying gate (0x00ad31a0
// + two further checks, all collapsed into this project's EXISTING
// HelicopterFlyTo::qualifies field, Sec28.2 - reused directly per this
// batch's own cross-batch coordination note, not re-derived). Arg 4
// resolves through a two-tier fallback (also collapsed to objectResolves()).
// CHOSEN (per this project's own "collapse an unmodeled deeper outcome to
// one confirmed-adjacent value" convention): the pushed boolean reflects
// whether the vehicle resolved AND qualified, since both the destination-
// resolved dispatch (0x00b3fda0) and the destination-unresolved pathfind
// fallback (0x00a3b860) are themselves OPEN past this point - not
// independently confirmed which one "really" succeeds, so this project does
// not invent a finer-grained answer. Args 2/3/5/6/7/9/10/11 are consumed per
// the confirmed shape, not modeled further.
//
// **FIX (2026-10-10, named-object-resolution-persona-empty task)**: arg 4
// is called "mandatory" above only in the sense that the real binary
// (0x00a4ff20) always reads/forwards it unconditionally with no nil-gate -
// NOT in the sense that every real call site supplies a non-empty name.
// The real, shipped game_lib.lua (misc.vpp_pc, this project's own
// mission-script carrier, re-extracted directly for this task) documents
// this exact parameter at its own Lua-wrapper layer: `helicopter_fly_to_
// internal`'s own doc comment names it "follow: if non-empty, target
// vehicle to follow" - i.e. the empty string is itself the documented
// "no follow target" value, not an absent/placeholder argument. Both of
// that wrapper's own "direct" callers (`helicopter_fly_to`/
// `helicopter_fly_to_direct`) hard-code `local follow = ""` and forward it
// positionally into this exact argument - confirmed the real, live call
// path for `m05.lua`'s own `helicopter_fly_to_direct(M05_groups.pierce.heli,
// -1, M05_nav.pierce)` (m05_initialize), which previously made this stub
// call `objectResolves().get("")` unconditionally and throw
// OpenStateError("named-object resolution['']...") - a real bug in this
// stub, not a genuine remaining OPEN gap: "no follow target given" and "a
// real name failed to resolve" are different engine facts the shared
// zone-object map must not conflate, same reasoning as this project's own
// pre-existing `target.empty() || !st->objectResolves().get(target)`
// guard convention (e.g. `effect_play_finisher`, Sec28.9). Fixed by adding
// the same empty-string short-circuit here.
int stub_helicopter_fly_to_do(lua_State* L) {
    logCall(L, upLog(L), "helicopter_fly_to_do", upStateTag(L));
    std::string name = argString(L, 1);
    (void)lua_tonumber(L, 2);
    (void)(lua_toboolean(L, 3) != 0);
    std::string destName = argString(L, 4);
    (void)(lua_toboolean(L, 5) != 0);
    (void)optionalStringDefault(L, 6, "");
    (void)optionalNumberDefault(L, 7, 0.0);
    bool useOrientationMode = optionalBoolDefault(L, 8, false);
    (void)optionalBoolDefault(L, 9, false);
    (void)optionalBoolDefault(L, 10, false);
    (void)optionalBoolDefault(L, 11, false);

    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    bool qualifies = st->helicopterFlyTo().qualifies.get(name); // OPEN until set, REUSED existing Sec28.2 field
    if (!qualifies) {
        lua_pushboolean(L, 0);
        return 1;
    }
    EngineState::HelicopterFlyTo::Request req;
    req.targetName = destName;
    req.useOrientationMode = useOrientationMode;
    req.applied = true;
    st->helicopterFlyTo().lastRequest[name] = req;
    // destName empty ("no follow target", the real game_lib.lua-documented
    // meaning - see this function's own top doc comment) is a legitimate
    // no-op, never a registry lookup; only a genuinely-named destination
    // touches objectResolves() (deeper position math OPEN, unchanged).
    if (!destName.empty()) (void)st->objectResolves().get(destName);
    lua_pushboolean(L, 1); // CHOSEN success signal - see doc comment above
    return 1;
}

// effect_play_on_human (Sec12.12, 0x00a48f80). Up to 5 arguments: arg 1
// (string, mandatory) - effect name; arg 2 (string, mandatory) - target
// character name; arg 3 (string, optional, default absent) - bone name;
// arg 4 (boolean, optional, default false); arg 5 (number/flag, optional,
// default 3). Return: same shape as effect_play_on_script_object above -
// -1.0 sentinel on failure, a fresh nextVisualEffectHandle() on success.
// CONFIRMED to share the SAME fixed effect-name registry, index/instance-
// allocation chain and finalizer as effect_play/effect_play_on_script_
// object (Sec9.1/Sec12.10). The bone-name resolution chain (0x00487e80/
// 0x004be170) is OPEN - not reproduced (no bone-index system in scope).
int stub_effect_play_on_human(lua_State* L) {
    logCall(L, upLog(L), "effect_play_on_human", upStateTag(L));
    std::string effectName = argString(L, 1);
    std::string targetName = argString(L, 2);
    (void)optionalStringDefault(L, 3, ""); // bone name, consumed - no bone-index system in scope
    (void)optionalBoolDefault(L, 4, false);
    (void)optionalNumberDefault(L, 5, 3.0);
    EngineState* st = upState(L);
    // Target (arg 2) resolver 0x00a281a0 - RE-CORRECTED (2026-10-08,
    // direct disassembly of its own body): a two-stage #PLAYER#-then-
    // #FOLLOWER# wrapper, not PLAYER-only as an earlier reading had it -
    // wired here via resolveFollowerAwareObject(). Arg 1's effect-name
    // resolver (0x005982e0) is unaffected - distinct resolver.
    std::string resolvedTarget;
    if (!st->objectResolves().get(effectName) || !resolveFollowerAwareObject(st, targetName, resolvedTarget)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        lua_pushnumber(L, EngineState::kActionPlaySentinelResolveFail);
        return 1;
    }
    lua_pushnumber(L, static_cast<lua_Number>(st->nextVisualEffectHandle()));
    return 1;
}

// character_set_skydiving (Sec12.13, 0x00a42750). 2 mandatory arguments, no
// nil-gates: arg 1 (string) - character name; arg 2 (boolean) - skydiving
// on/off. No return. CONFIRMED: resolves, then a genuine asymmetry - true
// calls one function (0x009ae810), false calls a COMPLETELY DIFFERENT one
// (0x009ae150) - not one setter with a boolean. This project tracks only
// the observable boolean outcome; which internal function "really" ran is
// not independently distinguishable from Lua's own perspective (no return
// value either way).
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_set_skydiving(lua_State* L) {
    logCall(L, upLog(L), "character_set_skydiving", upStateTag(L));
    std::string name = argString(L, 1);
    bool on = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    std::string resolvedName;
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        st->getOrCreateCharacter(resolvedName).skydivingOn = on;
    }
    return 0;
}

// vehicle_get_driver (Sec12.14, 0x00a62500). 1 mandatory string + 1
// optional boolean, standard nil-gated idiom, default true. Return: 1
// string (driver/occupant name) on success, or nil on failure. CONFIRMED:
// resolves the vehicle, then one of two occupant-getters (selected by the
// boolean, both OPEN) yields an occupant handle, converted to a name
// (0x00a353d0, OPEN). This project has no real occupant/seat registry
// anywhere in scope - CHOSEN: an UNRESOLVED vehicle cleanly pushes nil (no
// object, nothing to look up - consistent with every other query's "no
// object = no answer" precedent), but a RESOLVED vehicle with no driver
// name recorded genuinely refuses as OPEN (VehicleState::driverName) rather
// than guessing nil, since this project has no confirmed "no driver"
// default for a resolved vehicle to fall back on.
int stub_vehicle_get_driver(lua_State* L) {
    logCall(L, upLog(L), "vehicle_get_driver", upStateTag(L));
    std::string name = argString(L, 1);
    bool useDriverSeat = optionalBoolDefault(L, 2, true);
    (void)useDriverSeat; // selects which of two OPEN internal getters runs - not modeled, one name slot stands in for both
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) { // OPEN until set; CHOSEN: unresolved -> nil (see doc comment)
        lua_pushnil(L);
        return 1;
    }
    const std::string& driver = st->getOrCreateVehicle(name).driverName.get(); // OPEN until set
    if (driver.empty()) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, driver.c_str());
    return 1;
}

// on_vehicle_exit (Sec12.15, 0x00a58a40). 2 mandatory strings, no nil-gates:
// arg 1 - occupant name; arg 2 - vehicle/seat reference. No return.
// CONFIRMED a real three-tier fallback resolver chain (generic/#PLAYER#,
// then plain character, then plain vehicle) posting one of three different
// event codes (0x17/0x18/6, each via a different primitive). This project's
// flat objectResolves() map (Sec29's "one shared global name map" finding)
// cannot structurally distinguish which tier would have matched, so WHICH
// event code fires is OPEN - only that the native fired, for which
// (occupant, vehicle-or-seat) pair, is recorded (a stated simplification,
// not an invented event code).
int stub_on_vehicle_exit(lua_State* L) {
    logCall(L, upLog(L), "on_vehicle_exit", upStateTag(L));
    std::string occupant = argString(L, 1);
    std::string vehicleOrSeat = argString(L, 2);
    EngineState* st = upState(L);
    if (st->objectResolves().get(vehicleOrSeat)) { // OPEN until set; collapsed 3-tier resolver, see doc comment
        st->onVehicleExitEvents().push_back({occupant, vehicleOrSeat});
    }
    return 0;
}

// inv_weapon_disable_slot (Sec12.16, 0x00a51360). Up to 3 arguments,
// structurally identical to inv_weapon_disable_all_but_this_slot (Sec12.8)
// above: arg 1 (number, mandatory) - slot index; arg 2 (boolean, optional,
// default TRUE - a confirmed, checked asymmetry from Sec12.8's own default
// false); arg 3 (number/flag, optional, default 3). No return. CONFIRMED:
// the identical clamp-switch, but a DIFFERENT terminal dispatch
// (0x007f4460, OPEN). CONFIRMED side effect: "disables exactly the given
// slot," leaving the rest untouched (the direct sibling/contrast of
// Sec12.8's own "every slot but this one").
int stub_inv_weapon_disable_slot(lua_State* L) {
    logCall(L, upLog(L), "inv_weapon_disable_slot", upStateTag(L));
    int64_t slotRaw = truncateEa2596(lua_tonumber(L, 1));
    (void)optionalBoolDefault(L, 2, true); // CONFIRMED default asymmetry vs. Sec12.8 - OPEN role in the dispatch
    (void)optionalNumberDefault(L, 3, 3.0);
    auto& slots = upState(L)->weaponSlots();
    if (slotRaw >= 0 && slotRaw <= 7) {
        slots.disabled[static_cast<size_t>(slotRaw)] = true; // CONFIRMED: exactly this slot, others untouched
    } else {
        ++slots.sentinelOobCount; // sentinel 0xff case - real effect OPEN, not guessed
    }
    return 0;
}

// guardian_angel_increment_node_progress (Sec12.17, 0x00a4abb0). No
// arguments consumed. No return. CONFIRMED: the entire compiled body is a
// single unconditional call (0x00626b10, OPEN - "which specific 'current'
// guardian-angel context it advances" not traced), no per-call targeting.
// Call-count only.
int stub_guardian_angel_increment_node_progress(lua_State* L) {
    logCall(L, upLog(L), "guardian_angel_increment_node_progress", upStateTag(L));
    upState(L)->recordGuardianAngelNodeProgress();
    return 0;
}

// vehicle_show (Sec12.18, 0x00a645c0). Up to 3 arguments: arg 1 (string,
// mandatory) - vehicle reference; arg 2 (boolean, optional, default false);
// arg 3 (number, optional, default 0.0). No return. CONFIRMED: resolves,
// then an unconditional "materialize" call (0x00a87780, position pair + 2
// zeros, OPEN - no coordinate system in scope) runs; if arg 2 is true, a
// second call (0x00a77d70, OPEN) runs with arg 3 scaled by a fixed constant
// this document's own text calls "not confirmed" - NOT invented here; the
// raw argument is stored unscaled, only the request itself (whether arg 2
// was true) is tracked as a confirmed fact.
int stub_vehicle_show(lua_State* L) {
    logCall(L, upLog(L), "vehicle_show", upStateTag(L));
    std::string name = argString(L, 1);
    bool fadeRequested = optionalBoolDefault(L, 2, false);
    double fadeArg = optionalNumberDefault(L, 3, 0.0);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        VehicleState& v = st->getOrCreateVehicle(name);
        v.shownViaVehicleShow = true;
        if (fadeRequested) {
            v.vehicleShowFadeRequested = true;
            v.vehicleShowFadeRawArg = fadeArg; // UNSCALED - see doc comment above
        }
    }
    return 0;
}

// radio_block (Sec12.20, 0x00a5c290). 1 mandatory string, no nil-gate - a
// vehicle OR character reference. No return. CONFIRMED: tried first as a
// vehicle; on failure, tried as a character, following their cached vehicle
// association (CharacterState::currentVehicleName - the SAME field get_
// char_vehicle_name/Sec12.9 reads, "a confirmed reuse of the character's
// cached-vehicle-association fields between two of this tranche's own
// functions"). This project's flat objectResolves() map cannot
// structurally distinguish "is this name a vehicle" from "is this name a
// character" the way the real two-tier resolver does - CHOSEN tie-break: an
// already-materialized VehicleState record, or the absence of any
// CharacterState record at all, is read as "tier 1: a vehicle name
// directly" (matching the resolver's own stated priority order); otherwise
// the name is treated as a character and its cached vehicle association is
// followed. The terminal dispatch (0x00556680) is OPEN - only which vehicle
// got blocked is tracked.
//
// Resolver 0x00a281a0 (the character-tier fallback) - RE-CORRECTED
// (2026-10-08, direct disassembly of its own body): a two-stage
// #PLAYER#-then-#FOLLOWER# wrapper, not PLAYER-only as an earlier reading
// had it - wired here, follower-sentinel-resolver pass, via
// resolveFollowerAwareObject(). A FOLLOWER literal is never a vehicle
// name, so this is safe to apply at the single flat gate below regardless
// of which tier ultimately matches.
int stub_radio_block(lua_State* L) {
    logCall(L, upLog(L), "radio_block", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        std::string targetVehicle;
        if (st->hasVehicle(resolvedName) || !st->hasCharacter(resolvedName)) {
            targetVehicle = resolvedName; // CHOSEN tier-1 reading, see doc comment above
        } else {
            targetVehicle = st->getOrCreateCharacter(resolvedName).currentVehicleName.get(); // OPEN until set
        }
        if (!targetVehicle.empty()) st->recordRadioBlock(targetVehicle);
    }
    return 0;
}

// vehicle_set_invulnerable (Sec12.21, 0x00a62f70). 1 mandatory string + 1
// optional boolean, standard nil-gated idiom, default true. No return.
// RESOLVED (Sec46.1): CONFIRMED to reach the exact same setter (0x00a7a830)
// turn_invulnerable's (Sec3.7) own vehicle branch already uses - merges the
// boolean directly into VehicleState::forceFlags1d7a's existing
// kInvulnerableBit1d7a bit, no double-gate cited for this specific entry
// point.
int stub_vehicle_set_invulnerable(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_invulnerable", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        VehicleState& v = st->getOrCreateVehicle(name);
        v.forceFlags1d7a.setBits(VehicleState::kInvulnerableBit1d7a, value ? VehicleState::kInvulnerableBit1d7a : 0u);
    }
    return 0;
}

// vehicle_prevent_explosion_fling (Sec12.22, 0x00a62be0). 2 mandatory
// arguments, no nil-gates: arg 1 (string) - vehicle reference; arg 2
// (boolean). No return. CONFIRMED: resolves, then a dispatch (0x00a7ab10,
// OPEN internal) runs with the boolean - HIGH CONFIDENCE address-adjacent
// sibling of the invulnerability setters, but NOT independently confirmed
// to share that same byte/bit, so tracked as its own flat field.
int stub_vehicle_prevent_explosion_fling(lua_State* L) {
    logCall(L, upLog(L), "vehicle_prevent_explosion_fling", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->getOrCreateVehicle(name).preventExplosionFling = value;
    }
    return 0;
}

// camera_restrict_target_object (Sec12.23, 0x00a45860). Up to 6 arguments,
// all optional, standard nil-gated idiom throughout: arg 1 (string, default
// absent) - restriction target; arg 2 (number, default 0.0) - radius; arg 3
// (string, default absent) - "except for this viewer" check; args 4-6
// (numbers, default 0.0 each). No return. CONFIRMED a genuinely subtle two-
// part gate: by default (arg 3 absent), the restriction applies BOTH
// locally AND is queued for network replication; if arg 3 resolves to
// exactly the local player, replication is suppressed (local-only); if arg
// 3 resolves to anything else, local application is suppressed instead
// (replicate-only). This project represents "the local player" the SAME way
// every other function in scope already does - the reserved "#PLAYER#"
// sentinel string (this document's own Sec3 front matter) - rather than
// inventing a separate identity check. The deep two-resolver target lookup
// and position/radius dispatch selection (0x0057a950/0x0057ab80/0x0057aca0)
// are OPEN and not reproduced (no camera/world-position system in scope).
int stub_camera_restrict_target_object(lua_State* L) {
    logCall(L, upLog(L), "camera_restrict_target_object", upStateTag(L));
    std::string targetName = optionalStringDefault(L, 1, "");
    double radius = optionalNumberDefault(L, 2, 0.0);
    std::string exceptViewer = optionalStringDefault(L, 3, "");
    (void)optionalNumberDefault(L, 4, 0.0);
    (void)optionalNumberDefault(L, 5, 0.0);
    (void)optionalNumberDefault(L, 6, 0.0);

    bool localApply = true, replicate = true;
    if (!exceptViewer.empty()) {
        bool isLocalPlayer = caseInsensitiveEquals(exceptViewer, "#PLAYER#"); // CHOSEN reading, see doc comment above
        if (isLocalPlayer) replicate = false;
        else localApply = false;
    }

    EngineState* st = upState(L);
    auto& r = st->cameraRestrictTarget();
    ++r.callCount;
    r.targetName = targetName;
    r.radius = radius;
    r.hasTarget = !targetName.empty() && st->objectResolves().get(targetName); // OPEN until set when non-empty
    r.localApplied = localApply;
    r.replicated = replicate;
    if (replicate) EngineState::replicateStateChange("camera_restrict_target_object", targetName);
    return 0;
}

// waypoint_add (Sec12.24, 0x00a68cf0). Up to 2 arguments: arg 1 (string,
// mandatory) - waypoint target name; arg 2 (number/flag, optional, default
// 3). Return: 1 number - the SAME -1.0 sentinel (kActionPlaySentinelResolve
// Fail) effect_play/the effect_play_on_* family use, overwritten with a
// plain 0.0 on "at least one success path" per Sec12.24's own text.
// CONFIRMED: the 3-tier-plus-fallback resolver priority chain collapses to
// this project's one shared objectResolves() gate (Sec29 convention).
// CONFIRMED a genuine asymmetry: NO record-and-replicate block anywhere in
// this function's own body (unlike its documented counterpart waypoint_
// remove, Sec6.17) - not modeled, consistent with the confirmed absence.
int stub_waypoint_add(lua_State* L) {
    logCall(L, upLog(L), "waypoint_add", upStateTag(L));
    std::string name = argString(L, 1);
    (void)optionalNumberDefault(L, 2, 3.0);
    double result = EngineState::kActionPlaySentinelResolveFail;
    if (upState(L)->objectResolves().get(name)) result = 0.0; // OPEN until set; CONFIRMED overwrite on a success path
    lua_pushnumber(L, result);
    return 1;
}

// get_current_hit_points (Sec12.25, 0x00a4bb00). 1 mandatory string, no
// nil-gate. Return: 1 number, default 0.0 if nothing resolves. CONFIRMED a
// genuinely different data source per resolution path: the generic/
// #PLAYER#/#FOLLOWER#/character-fallback-to-mount paths all read the SAME
// +0x1cb8 field (this project's EXISTING CharacterState::currentHitPoints);
// the VEHICLE path reads a completely different zero-argument global
// context accessor (0x00a77e10, OPEN - VehicleState::hitPointsForQuery, new
// this batch). The unidentified 4th-kind placed-object path (+0xdc/+0x138)
// is OPEN and not modeled (a stated gap). This project's flat objectResolves
// () map cannot distinguish a character from a vehicle the SAME way the
// real 5-tier resolver chain does - CHOSEN tie-break (same reasoning as
// radio_block above): an already-materialized VehicleState record with no
// CharacterState record is read as the vehicle path; everything else takes
// the character/follower path. A #FOLLOWER...# literal (wired 2026-10-08,
// follower-sentinel-resolver pass) bypasses that tie-break entirely: Sec12.25
// is explicit that its resolved handle reads the SAME +0x1cb8 field as the
// generic/character path - a companion is always character-shaped, never a
// vehicle, so there is nothing to disambiguate.
int stub_get_current_hit_points(lua_State* L) {
    logCall(L, upLog(L), "get_current_hit_points", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    double hp = 0.0;
    std::string resolvedName;
    if (isFollowerSentinelLiteral(name)) {
        if (resolveFollowerAwareObject(st, name, resolvedName)) { // may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
            hp = st->drawOrGetCurrentHitPoints(resolvedName); // Sec34.2: starts at the table/max value
        }
    } else if (st->objectResolves().get(name)) { // OPEN until set
        if (st->hasVehicle(name) && !st->hasCharacter(name)) {
            hp = st->getOrCreateVehicle(name).hitPointsForQuery.get(); // OPEN until set
        } else {
            hp = st->drawOrGetCurrentHitPoints(name); // Sec34.2: starts at the table/max value
        }
    }
    lua_pushnumber(L, hp);
    return 1;
}

// vehicle_freeze (Sec12.26, 0x00a63f20). 1 mandatory string + 1 optional
// boolean, standard nil-gated idiom, default false. No return. CONFIRMED:
// resolves, then a dispatch (0x00abe510, OPEN internal, position pair not
// modeled) runs with the boolean.
int stub_vehicle_freeze(lua_State* L) {
    logCall(L, upLog(L), "vehicle_freeze", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = optionalBoolDefault(L, 2, false);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->getOrCreateVehicle(name).frozen = value;
    }
    return 0;
}

// set_player_can_enter_exit_rc_vehicles (Sec12.27, 0x00a5d390). 1 mandatory
// string + 1 optional boolean, standard nil-gated idiom, default true. No
// return. CONFIRMED: resolves through the generic/#PLAYER# chain, then a
// setter (0x009e0280, OPEN internal) runs with the LOGICAL NEGATION of the
// boolean - a third confirmed instance of this document's "flag stored as
// the negated sense of an allow_*-style argument" convention.
int stub_set_player_can_enter_exit_rc_vehicles(lua_State* L) {
    logCall(L, upLog(L), "set_player_can_enter_exit_rc_vehicles", upStateTag(L));
    std::string name = argString(L, 1);
    bool canEnterExit = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->getOrCreateCharacter(name).rcVehiclesBlocked = !canEnterExit; // CONFIRMED stored inverted
    }
    return 0;
}

// player_parachute_wear_backpack (Sec12.28, 0x00a5a380). 2 mandatory
// arguments, no nil-gates: arg 1 (string) - character reference; arg 2
// (boolean) - wear/equip flag. No return (implicit void - no lua_push* call
// anywhere in the real body). CONFIRMED: resolves, then a gate (0x008addb0)
// selects between a direct local apply (0x009daeb0, no replication) when
// TRUE, or a single-target opcode-0x43 record-and-replicate when FALSE. This
// gate shares its own address with CharacterState::controlLockGatePasses
// (already independently CONFIRMED true in single player, Sec3.7/Sec6.22) -
// HIGH CONFIDENCE, not independently re-derived for this specific call site
// (see CharacterState::parachuteBackpackGatePasses's own doc comment for the
// full WALLS.md-aware caveat) - seeded the SAME way at construction.
int stub_player_parachute_wear_backpack(lua_State* L) {
    logCall(L, upLog(L), "player_parachute_wear_backpack", upStateTag(L));
    std::string name = argString(L, 1);
    bool wear = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.parachuteBackpackGatePasses.get()) { // HIGH CONFIDENCE true in single player, see doc comment above
        c.parachuteBackpackWorn = wear;
    } else {
        EngineState::replicateStateChange("player_parachute_wear_backpack", name); // single-target opcode-0x43 (dead in single player)
    }
    return 0;
}

// persona_override_persona_stop (Sec12.29, 0x00a5ad80). 1 mandatory string,
// no nil-gate - a persona name. No return. CONFIRMED: resolves through the
// generic/#PLAYER# family, then a dispatch (0x0070cb40, OPEN internal) runs
// on the resolved handle. Same "resolve-only, no modeled field" precedent as
// character_show (Sec7.15) - this project tracks no real persona-override
// state anywhere in scope.
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_persona_override_persona_stop(lua_State* L) {
    logCall(L, upLog(L), "persona_override_persona_stop", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        (void)st->getOrCreateCharacter(resolvedName);
    }
    return 0;
}

} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB10() {
    static const std::vector<std::string> names = {
        // Section 11 ("ranks 151-250 tranche, part D", 12 UI-cluster names).
        "pcr_is_plastic_surgeon",
        "pcr_mark_changed",
        "pcr_apply_morph_value",
        "pcr_get_morph_value",
        "pcu_is_streaming_done",
        "horde_mode_is_active",
        "game_audio_stop",
        "get_localized_crc_for_tag",
        "store_common_mouse_zoom",
        "store_set_camera_pos",
        "pause_menu_accept_options",
        "vint_options_remap_set_key_binding",
        // Section 12 ("ranks 151-250 tranche, part A", 28 of 29 gameplay-
        // cluster names - traffic_disable_lanes/Sec12.19 already registered
        // in lua_spec_confirmed_stubs_b06_playerrt_sec6_7.cpp).
        "hud_critical_timer_stop",
        "audio_play_for_mission_cellphone",
        "inv_item_remove_all",
        "character_set_skydiving_state",
        "character_hide",
        "vehicle_set_smoke_and_fire_state",
        "set_always_sees_player_flag",
        "inv_weapon_disable_all_but_this_slot",
        "get_char_vehicle_name",
        "effect_play_on_script_object",
        "helicopter_fly_to_do",
        "effect_play_on_human",
        "character_set_skydiving",
        "vehicle_get_driver",
        "on_vehicle_exit",
        "inv_weapon_disable_slot",
        "guardian_angel_increment_node_progress",
        "vehicle_show",
        "radio_block",
        "vehicle_set_invulnerable",
        "vehicle_prevent_explosion_fling",
        "camera_restrict_target_object",
        "waypoint_add",
        "get_current_hit_points",
        "vehicle_freeze",
        "set_player_can_enter_exit_rc_vehicles",
        "player_parachute_wear_backpack",
        "persona_override_persona_stop",
    };
    return names;
}

void registerB10(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    // Section 11.
    if (wants("pcr_is_plastic_surgeon")) registerOne(L, state, log, stateTag, "pcr_is_plastic_surgeon", openGuard<stub_pcr_is_plastic_surgeon>);
    if (wants("pcr_mark_changed")) registerOne(L, state, log, stateTag, "pcr_mark_changed", openGuard<stub_pcr_mark_changed>);
    if (wants("pcr_apply_morph_value")) registerOne(L, state, log, stateTag, "pcr_apply_morph_value", openGuard<stub_pcr_apply_morph_value>);
    if (wants("pcr_get_morph_value")) registerOne(L, state, log, stateTag, "pcr_get_morph_value", openGuard<stub_pcr_get_morph_value>);
    if (wants("pcu_is_streaming_done")) registerOne(L, state, log, stateTag, "pcu_is_streaming_done", openGuard<stub_pcu_is_streaming_done>);
    if (wants("horde_mode_is_active")) registerOne(L, state, log, stateTag, "horde_mode_is_active", openGuard<stub_horde_mode_is_active>);
    if (wants("game_audio_stop")) registerOne(L, state, log, stateTag, "game_audio_stop", openGuard<stub_game_audio_stop>);
    if (wants("get_localized_crc_for_tag")) registerOne(L, state, log, stateTag, "get_localized_crc_for_tag", openGuard<stub_get_localized_crc_for_tag>);
    if (wants("store_common_mouse_zoom")) registerOne(L, state, log, stateTag, "store_common_mouse_zoom", openGuard<stub_store_common_mouse_zoom>);
    if (wants("store_set_camera_pos")) registerOne(L, state, log, stateTag, "store_set_camera_pos", openGuard<stub_store_set_camera_pos>);
    if (wants("pause_menu_accept_options")) registerOne(L, state, log, stateTag, "pause_menu_accept_options", openGuard<stub_pause_menu_accept_options>);
    if (wants("vint_options_remap_set_key_binding")) registerOne(L, state, log, stateTag, "vint_options_remap_set_key_binding", openGuard<stub_vint_options_remap_set_key_binding>);
    // Section 12.
    if (wants("hud_critical_timer_stop")) registerOne(L, state, log, stateTag, "hud_critical_timer_stop", openGuard<stub_hud_critical_timer_stop>);
    if (wants("audio_play_for_mission_cellphone")) registerOne(L, state, log, stateTag, "audio_play_for_mission_cellphone", openGuard<stub_audio_play_for_mission_cellphone>);
    if (wants("inv_item_remove_all")) registerOne(L, state, log, stateTag, "inv_item_remove_all", openGuard<stub_inv_item_remove_all>);
    if (wants("character_set_skydiving_state")) registerOne(L, state, log, stateTag, "character_set_skydiving_state", openGuard<stub_character_set_skydiving_state>);
    if (wants("character_hide")) registerOne(L, state, log, stateTag, "character_hide", openGuard<stub_character_hide>);
    if (wants("vehicle_set_smoke_and_fire_state")) registerOne(L, state, log, stateTag, "vehicle_set_smoke_and_fire_state", openGuard<stub_vehicle_set_smoke_and_fire_state>);
    if (wants("set_always_sees_player_flag")) registerOne(L, state, log, stateTag, "set_always_sees_player_flag", openGuard<stub_set_always_sees_player_flag>);
    if (wants("inv_weapon_disable_all_but_this_slot")) registerOne(L, state, log, stateTag, "inv_weapon_disable_all_but_this_slot", openGuard<stub_inv_weapon_disable_all_but_this_slot>);
    if (wants("get_char_vehicle_name")) registerOne(L, state, log, stateTag, "get_char_vehicle_name", openGuard<stub_get_char_vehicle_name>);
    if (wants("effect_play_on_script_object")) registerOne(L, state, log, stateTag, "effect_play_on_script_object", openGuard<stub_effect_play_on_script_object>);
    if (wants("helicopter_fly_to_do")) registerOne(L, state, log, stateTag, "helicopter_fly_to_do", openGuard<stub_helicopter_fly_to_do>);
    if (wants("effect_play_on_human")) registerOne(L, state, log, stateTag, "effect_play_on_human", openGuard<stub_effect_play_on_human>);
    if (wants("character_set_skydiving")) registerOne(L, state, log, stateTag, "character_set_skydiving", openGuard<stub_character_set_skydiving>);
    if (wants("vehicle_get_driver")) registerOne(L, state, log, stateTag, "vehicle_get_driver", openGuard<stub_vehicle_get_driver>);
    if (wants("on_vehicle_exit")) registerOne(L, state, log, stateTag, "on_vehicle_exit", openGuard<stub_on_vehicle_exit>);
    if (wants("inv_weapon_disable_slot")) registerOne(L, state, log, stateTag, "inv_weapon_disable_slot", openGuard<stub_inv_weapon_disable_slot>);
    if (wants("guardian_angel_increment_node_progress")) registerOne(L, state, log, stateTag, "guardian_angel_increment_node_progress", openGuard<stub_guardian_angel_increment_node_progress>);
    if (wants("vehicle_show")) registerOne(L, state, log, stateTag, "vehicle_show", openGuard<stub_vehicle_show>);
    if (wants("radio_block")) registerOne(L, state, log, stateTag, "radio_block", openGuard<stub_radio_block>);
    if (wants("vehicle_set_invulnerable")) registerOne(L, state, log, stateTag, "vehicle_set_invulnerable", openGuard<stub_vehicle_set_invulnerable>);
    if (wants("vehicle_prevent_explosion_fling")) registerOne(L, state, log, stateTag, "vehicle_prevent_explosion_fling", openGuard<stub_vehicle_prevent_explosion_fling>);
    if (wants("camera_restrict_target_object")) registerOne(L, state, log, stateTag, "camera_restrict_target_object", openGuard<stub_camera_restrict_target_object>);
    if (wants("waypoint_add")) registerOne(L, state, log, stateTag, "waypoint_add", openGuard<stub_waypoint_add>);
    if (wants("get_current_hit_points")) registerOne(L, state, log, stateTag, "get_current_hit_points", openGuard<stub_get_current_hit_points>);
    if (wants("vehicle_freeze")) registerOne(L, state, log, stateTag, "vehicle_freeze", openGuard<stub_vehicle_freeze>);
    if (wants("set_player_can_enter_exit_rc_vehicles")) registerOne(L, state, log, stateTag, "set_player_can_enter_exit_rc_vehicles", openGuard<stub_set_player_can_enter_exit_rc_vehicles>);
    if (wants("player_parachute_wear_backpack")) registerOne(L, state, log, stateTag, "player_parachute_wear_backpack", openGuard<stub_player_parachute_wear_backpack>);
    if (wants("persona_override_persona_stop")) registerOne(L, state, log, stateTag, "persona_override_persona_stop", openGuard<stub_persona_override_persona_stop>);
}

} // namespace stub_sections
} // namespace sr3luahost
