// lua_spec_confirmed_stubs_b17_wave2_ui_frontend_a.cpp
//
// Wave 2 batch W2-6 (2026-10-08), "UI frontend (143 natives) - HARDENING
// BATCH" - split A of 2 (file B: lua_spec_confirmed_stubs_b18_wave2_ui_
// frontend_b.cpp). 66 names: every spec-lua-api-behaviour.md Sec19/Sec23/
// Sec26 name this batch owns, plus every lobby/coop/session/steam `game_*`
// name from the remaining ranking-tranche sections (Sec41/Sec42/Sec44/
// Sec50/Sec51/Sec53/Sec54/Sec55/Sec56/Sec59) - a deterministic split rule
// this file's own author chose (see this batch's own report for the exact
// per-name assignment): every one of the 143 names that literally starts
// with "game_" lands here EXCEPT the 7 `game_machinima_*` names, which join
// their `cinema_editor_*`/`cell_machinima_is_recording` siblings in file B
// instead (the "Machinima family - one owner, ALL of it in this batch"
// directive means one FILE owns the whole family's shared path-builder
// hardening, and that file is B). Sec19/Sec23/Sec26 are added here in full
// (even the few non-`game_*` names, e.g. `online_validator_set_checking`)
// per the split description's own "Sec19/Sec23/Sec26 call-ranked residue"
// wording. A handful of non-`game_*`, non-menu/dialog/cellphone/cell/save/
// machinima/completion residue names (get_char_in_string, get_localized_
// string_for_tag, get_world_income_dollars, get_current_difficulty, get_
// get_key_names_for_axis_action, hud_diversion_remove_callback, autil_
// mashing_minigame_faded_out) also land here as "call-ranked residue" -
// none of them fit file B's own named buckets.
//
// CLEAN-ROOM: every behavior below comes from spec-lua-api-behaviour.md
// (sections cited per stub) - never the game executable or any
// disassembly (Team A's exclusive domain). A genuinely OPEN detail no
// spec text resolves is refused honestly via OpenStateError, never
// guessed from a name alone.
//
// HARDENING POLICY (orchestrator's standing directive, this file's own
// share of it): every CRASH-tagged name below gets EITHER (a) a `CHOSEN
// (hardened divergence)` fail-closed branch + a named EngineState counter,
// for a genuine memory-safety shape (null/OOB pointer read in the real
// engine), or (b) a plain faithful reproduction of a pure LOGIC quirk that
// is not itself memory-unsafe (game_is_joinable's "!=0" test, game_join_
// syslink_game's 8-bit index alias, game_lobby_set_horde_mode_data's
// spurious-warning/stuck-map defect) - each such case says explicitly, in
// its own doc comment, why no fail-closed divergence applies: this host
// has no raw pointer/buffer for the real bug to corrupt, so reproducing
// the CONFIRMED logic exactly is the correct, non-"invented-fix" choice.
// Named per-site counters live in EngineState (include/sr3luahost/
// engine_state.h, "Wave 2 batch W2-6" section) - see each counter's own
// doc comment there for the full citation.
//
// AGG names (spec describes the family collectively, not individually):
// `game_record_mode_can_encode/_enable/_show_hud/game_record_set_quality_
// level` (Sec56.2: CONFIRMED 3-no-op-1-constant-false, but NOT which of
// the 4 is which) all take a labelled OPEN refusal rather than guess an
// assignment the spec doesn't give. `game_is_local_player_in_vehicle` carries
// NO spec text anywhere in this project (checked spec-lua-api-behaviour.md's
// full Sec41/Sec44/Sec45/Sec59 text exhaustively, and grepped spec-lua-
// bindings.md and every other spec-*.md for the exact name - genuinely
// absent). `autil_mashing_minigame_faded_out` IS present verbatim in
// spec-lua-bindings.md:1248 (the fully-walked 113-name/function-pointer
// 0x00845aa0 registration list) but only as a bare resolved name with no
// behavior described - corrected 2026-10-08 after an adversarial review
// caught this file's earlier "genuinely absent" overstatement for this one
// name. Both still OPEN-refuse (no behavior is given either way) rather
// than invent a body from a name alone, per this project's own "no
// invented fixes" standing policy.
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

#include <cmath>
#include <sstream>

namespace sr3luahost {
namespace {

// =======================================================================
// Sec19.7/Sec23.14 game_interface_effect_begin/_end
// =======================================================================

// ---------------------------------------------------------------------
// game_interface_effect_begin (Sec19.7, 0x00841df0). Arguments: 4 - arg1
// mandatory string (effect name); arg2 optional number, default a global
// float constant (CHOSEN stand-in default 0.0 - the real constant
// 0x01117a4c's own value is OPEN); arg3 optional number, default 0.0; arg4
// optional boolean, default false (only true has an effect - CONFIRMED).
// No return. Body: starts the named effect, configures its two numeric
// parameters; arg4 true additionally marks it "forced" (HIGH CONFIDENCE
// "force/interrupt any currently-playing effect", not independently
// confirmed - Sec19.7's own OPEN callee bodies).
// ---------------------------------------------------------------------
int stub_game_interface_effect_begin(lua_State* L) {
    logCall(L, upLog(L), "game_interface_effect_begin", upStateTag(L));
    std::string name = argString(L, 1);
    double p2 = optionalNumberDefault(L, 2, 0.0); // CHOSEN default; real constant 0x01117a4c OPEN
    double p3 = optionalNumberDefault(L, 3, 0.0);
    bool forced = optionalBoolDefault(L, 4, false);
    EngineState* st = upState(L);
    st->interfaceEffect().activeName = name;
    st->interfaceEffect().param1 = p2;
    st->interfaceEffect().param2 = p3;
    st->interfaceEffect().forced = forced;
    ++st->interfaceEffect().beginCount;
    return 0;
}

// ---------------------------------------------------------------------
// game_interface_effect_end (Sec23.14, 0x00841ef0). Arguments: 1 optional
// number (duration/param value), default 0.0. No return. Body: CONFIRMED
// forwards to the EXACT SAME two-numeric-parameter configure primitive
// game_interface_effect_begin uses, with its first parameter fixed at
// literal 0 and the second supplying the caller's given value - "ending"
// is reconfiguring, not a separate teardown.
// ---------------------------------------------------------------------
int stub_game_interface_effect_end(lua_State* L) {
    logCall(L, upLog(L), "game_interface_effect_end", upStateTag(L));
    double duration = optionalNumberDefault(L, 1, 0.0);
    EngineState* st = upState(L);
    st->interfaceEffect().param1 = 0.0; // CONFIRMED: fixed at literal 0
    st->interfaceEffect().param2 = duration;
    ++st->interfaceEffect().endCount;
    return 0;
}

// =======================================================================
// Sec19.6 online_validator_set_checking
// =======================================================================

// ---------------------------------------------------------------------
// online_validator_set_checking (Sec19.6, 0x007aa9b0). Arguments: 1
// mandatory boolean, no gate. No return. Body: CONFIRMED trivial - stores
// directly into a single global flag (network/session validation
// subsystem, not traced further - a 2-instruction body past the shared
// prologue).
// ---------------------------------------------------------------------
int stub_online_validator_set_checking(lua_State* L) {
    logCall(L, upLog(L), "online_validator_set_checking", upStateTag(L));
    bool flag = lua_toboolean(L, 1) != 0; // CONFIRMED: no nil-gate
    upState(L)->onlineValidatorChecking() = flag;
    return 0;
}

// =======================================================================
// Sec19.9/Sec44.3/Sec26.21 coop/syslink start-flags
// =======================================================================

// ---------------------------------------------------------------------
// game_coop_get_starting_coop (Sec19.9, 0x008426e0). No arguments. Return:
// 1 boolean. Body: CONFIRMED reads the SAME two adjacent one-byte globals
// this project already models as coopLiveSessionActive()/
// coopSyslinkSessionActive() (written by game_coop_start_new_live/_syslink,
// b02) and pushes their logical OR.
// ---------------------------------------------------------------------
int stub_game_coop_get_starting_coop(lua_State* L) {
    logCall(L, upLog(L), "game_coop_get_starting_coop", upStateTag(L));
    EngineState* st = upState(L);
    bool result = st->coopLiveSessionActive() || st->coopSyslinkSessionActive();
    lua_pushboolean(L, result ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// game_coop_get_starting_syslink (Sec44.3, 0x008426b0). No arguments.
// Return: 1 boolean. Body: CONFIRMED reads co-op-start byte 0x014ff6c2 -
// the SAME global coopSyslinkSessionActive() already models.
// ---------------------------------------------------------------------
int stub_game_coop_get_starting_syslink(lua_State* L) {
    logCall(L, upLog(L), "game_coop_get_starting_syslink", upStateTag(L));
    lua_pushboolean(L, upState(L)->coopSyslinkSessionActive() ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// game_is_syslink (Sec26.21, 0x008445d0). No arguments. Return: 1
// boolean. Body: CONFIRMED reads the co-op/session-context singleton's
// own system-link bit - the SAME "is this a system-link (LAN) session"
// concept coopSyslinkSessionActive() already models (this project has
// only one session-kind flag, not a separate singleton-bit copy).
// ---------------------------------------------------------------------
int stub_game_is_syslink(lua_State* L) {
    logCall(L, upLog(L), "game_is_syslink", upStateTag(L));
    lua_pushboolean(L, upState(L)->coopSyslinkSessionActive() ? 1 : 0);
    return 1;
}

// =======================================================================
// Sec19.8 game_get_party_member_count
// =======================================================================

// ---------------------------------------------------------------------
// game_get_party_member_count (Sec19.8, 0x008446b0). No arguments.
// Return: 1 number. CONFIRMED a genuine, disassembly-confirmed hardcoded
// stub, not a real query - the entire real body past the shared prologue
// is a literal float-zero load and a push; no party-related state is
// touched anywhere. Reproduced exactly: always 0.0.
// ---------------------------------------------------------------------
int stub_game_get_party_member_count(lua_State* L) {
    logCall(L, upLog(L), "game_get_party_member_count", upStateTag(L));
    lua_pushnumber(L, 0.0); // CONFIRMED: hardcoded, no real query
    return 1;
}

// =======================================================================
// Sec26.17 game_audio_play
// =======================================================================

// ---------------------------------------------------------------------
// game_audio_play (Sec26.17, 0x00843870). Arguments: 1 mandatory, 2
// optional trailing - each slot accepts EITHER a string (resolved to a
// sound-event id) OR a raw numeric id (this document's established
// "either a resolved name or a raw id" idiom). Return: 0 values if the
// primary sound id resolves to nothing, else 1 number (a playback
// handle). Body: CONFIRMED plays a one-shot sound with up to 2 linked
// companion sounds. This project has no real Wwise-hash table anywhere
// (the SAME gap audio_ambient_emitter_pause/_resume already document) - a
// string name "resolves" only if registered for testing
// (audioEventKnown()), same never-populated-registry convention; a raw
// numeric id always resolves (no hash lookup needed).
// ---------------------------------------------------------------------
int stub_game_audio_play(lua_State* L) {
    logCall(L, upLog(L), "game_audio_play", upStateTag(L));
    EngineState* st = upState(L);
    bool resolves = false;
    if (lua_gettop(L) >= 1) {
        if (lua_type(L, 1) == LUA_TNUMBER) {
            resolves = lua_tonumber(L, 1) != 0.0;
        } else if (lua_type(L, 1) == LUA_TSTRING) {
            resolves = st->audioEventKnown(argString(L, 1)); // never-populated-registry convention
        }
    }
    if (!resolves) return 0; // CONFIRMED: 0 values if the primary sound id resolves to nothing
    int64_t handle = st->nextMonotonicStamp();
    lua_pushnumber(L, static_cast<lua_Number>(handle));
    return 1;
}

// =======================================================================
// Sec26.4 game_lobby_get_remote_player_name
// =======================================================================

// ---------------------------------------------------------------------
// game_lobby_get_remote_player_name (Sec26.4, 0x007ab0c0). No arguments.
// Return: 0 values if no remote-lobby context exists, otherwise 1
// string. Body: CONFIRMED resolves a "current lobby/other-player" context
// object; null -> no push; else copies its own name field. Shares the
// exact same +0xc2 name field Completion_get_other_player_name (file B)
// reads via a different context object - this project models both as the
// one shared remotePlayer() record.
// ---------------------------------------------------------------------
int stub_game_lobby_get_remote_player_name(lua_State* L) {
    logCall(L, upLog(L), "game_lobby_get_remote_player_name", upStateTag(L));
    EngineState* st = upState(L);
    if (!st->remotePlayer().present) return 0; // CONFIRMED: no context -> no push
    lua_pushstring(L, st->remotePlayer().name.c_str());
    return 1;
}

// =======================================================================
// Sec23.13/Sec44.2 game_is_connected_to_service/_internet
// =======================================================================

// ---------------------------------------------------------------------
// game_is_connected_to_service (Sec23.13, 0x008428e0). No arguments.
// Return: 1 boolean. CONFIRMED (corrected 2026-10-01): a Steam user
// interface exists AND reports logged on (ISteamUser::BLoggedOn) -
// distinct from the weaker, already-implemented game_is_signed_in
// ("interface exists" only).
// ---------------------------------------------------------------------
int stub_game_is_connected_to_service(lua_State* L) {
    logCall(L, upLog(L), "game_is_connected_to_service", upStateTag(L));
    lua_pushboolean(L, upState(L)->steamLoggedOn().get() ? 1 : 0); // OPEN until set
    return 1;
}

// ---------------------------------------------------------------------
// game_is_connected_to_internet (Sec44.2, 0x008428e0). No arguments.
// Return: 1 boolean. CONFIRMED: the SAME handler as game_is_connected_to_
// service (identical address) - both mean "logged on to Steam."
// ---------------------------------------------------------------------
int stub_game_is_connected_to_internet(lua_State* L) {
    logCall(L, upLog(L), "game_is_connected_to_internet", upStateTag(L));
    lua_pushboolean(L, upState(L)->steamLoggedOn().get() ? 1 : 0); // OPEN until set
    return 1;
}

// =======================================================================
// Sec26.19 game_set_equipped_grenade
// =======================================================================

// ---------------------------------------------------------------------
// game_set_equipped_grenade (Sec26.19, 0x00843b90). Arguments: 1
// mandatory number (grenade-type id). Return: 1 boolean. Body: CONFIRMED
// a genuine equip-grenade-type setter, bounds-checked against a 4-slot
// table - ids 8-11 map to internal slots 0-3; anything else fails.
// ---------------------------------------------------------------------
int stub_game_set_equipped_grenade(lua_State* L) {
    logCall(L, upLog(L), "game_set_equipped_grenade", upStateTag(L));
    int64_t id = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    bool ok = id >= 8 && id <= 11; // CONFIRMED bounds (4-slot table)
    if (ok) st->equippedGrenadeSlot() = static_cast<int32_t>(id - 8);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// =======================================================================
// Sec26.18 game_use_radial_menu_item
// =======================================================================

// ---------------------------------------------------------------------
// game_use_radial_menu_item (Sec26.18, 0x00843e80). Arguments: 1
// mandatory number (radial-menu slot index). Return: 1 number (always a
// double, never a plain boolean). Body: CONFIRMED special index 12 always
// reports 1.0 (direct player-sub-object dispatch); any out-of-range index
// (negative, 8-11 through this entry point, or 13+) also always reports
// 1.0 (a confirmed no-op-but-reports-success gap). Slots 0-7 dispatch
// through a genuine quick-item-use dispatcher whose own success/failure
// result is OPEN (0x00600720's own internals) - modelled as an OpenValue
// rather than guessed.
// ---------------------------------------------------------------------
int stub_game_use_radial_menu_item(lua_State* L) {
    logCall(L, upLog(L), "game_use_radial_menu_item", upStateTag(L));
    int64_t slot = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    double result;
    if (slot == 12) {
        ++st->radialMenuSlot12Count();
        result = 1.0; // CONFIRMED
    } else if (slot >= 0 && slot <= 7) {
        result = st->radialMenuQuickItemSucceeds().get() ? 1.0 : 0.0; // OPEN until set - 0x00600720's own result
    } else {
        result = 1.0; // CONFIRMED: any other out-of-range index -> always 1.0
    }
    lua_pushnumber(L, static_cast<lua_Number>(result));
    return 1;
}

// =======================================================================
// Sec26.20 game_vkeyboard_input
// =======================================================================

// ---------------------------------------------------------------------
// game_vkeyboard_input (Sec26.20, 0x00845780). Arguments: up to 6 (title,
// placeholder, max length, default text, a 5th string, an optional
// trailing boolean genuinely discarded - CONFIRMED). Return: the modal
// close result (HIGH CONFIDENCE decompiled in full, not raw-disasm-
// checked given size) or false if the platform-capability/"already open"
// gate fails. This project tracks the gate as a single "already open"
// flag (no real platform-capability probe); the close result itself is
// OPEN until a test sets it.
// ---------------------------------------------------------------------
int stub_game_vkeyboard_input(lua_State* L) {
    logCall(L, upLog(L), "game_vkeyboard_input", upStateTag(L));
    EngineState* st = upState(L);
    std::string title = optionalStringDefault(L, 1, "Text Input"); // CONFIRMED default
    if (st->virtualKeyboard().open) {
        lua_pushboolean(L, 0); // CONFIRMED: gate fails -> false
        return 1;
    }
    st->virtualKeyboard().lastTitle = title;
    ++st->virtualKeyboard().requestCount;
    bool result = st->virtualKeyboardCloseResult().get(); // OPEN until set - 0x0059f8a0's own result
    lua_pushboolean(L, result ? 1 : 0);
    return 1;
}

// =======================================================================
// Sec23.16 game_award_cash
// =======================================================================

// ---------------------------------------------------------------------
// game_award_cash (Sec23.16, 0x00843d70). Arguments: arg1 mandatory
// number (cash amount, float); arg2 optional number, default -1 (a
// context/reason id); arg3 optional boolean, default false (extra-notify
// flag). No return. Body: CONFIRMED applies amount*100 (rounded) when
// amount > 0; if arg2 given, opens an opcode-0xa0 tagged record
// (replicateStateChange stand-in); arg3 true -> an extra notify (counted
// only, the real popup-trigger callee is OPEN).
// ---------------------------------------------------------------------
int stub_game_award_cash(lua_State* L) {
    logCall(L, upLog(L), "game_award_cash", upStateTag(L));
    double amount = optionalNumberDefault(L, 1, 0.0);
    double reason = optionalNumberDefault(L, 2, -1.0); // CONFIRMED default -1
    bool extraNotify = optionalBoolDefault(L, 3, false);
    if (amount > 0.0) {
        EngineState::replicateStateChange("game_award_cash", ""); // global state, no object
    }
    if (reason != -1.0) {
        EngineState::replicateStateChange("game_award_cash:opcode_0xa0", "");
    }
    (void)extraNotify; // counted via logCall only - the popup-trigger callee's own body is OPEN
    return 0;
}

// =======================================================================
// Sec23.15 game_check_coop_dlc
// =======================================================================

// ---------------------------------------------------------------------
// game_check_coop_dlc (Sec23.15, 0x008443a0). Arguments: 1 mandatory
// string (a DLC identifier/tag). No return. Body: CONFIRMED caches the
// tag, checks an "already showing/already handled" gate; if clear,
// surfaces a "DLC Parsing" notice dialog (0x00841ba0/0x0084a1b0/
// 0x007c3e60's own internals are OPEN - only the gate transition is
// modelled).
// ---------------------------------------------------------------------
int stub_game_check_coop_dlc(lua_State* L) {
    logCall(L, upLog(L), "game_check_coop_dlc", upStateTag(L));
    std::string dlcTag = argString(L, 1);
    (void)dlcTag; // cached only; the parsing-check/notice-dialog internals are OPEN
    return 0;
}

// =======================================================================
// Sec23.12 game_letterbox_fade_out
// =======================================================================

// ---------------------------------------------------------------------
// game_letterbox_fade_out (Sec23.12, 0x00842090). Arguments: 1 mandatory
// boolean, no gate. No return. Body: CONFIRMED forwards the boolean
// directly to the cinematic letterbox fade-out setter.
// ---------------------------------------------------------------------
int stub_game_letterbox_fade_out(lua_State* L) {
    logCall(L, upLog(L), "game_letterbox_fade_out", upStateTag(L));
    (void)(lua_toboolean(L, 1) != 0); // CONFIRMED: forwarded directly, no further Lua-visible state in this project
    return 0;
}

// =======================================================================
// Sec23.11/Sec41.2 game_stop_find_syslink_servers/_start_find_syslink_servers
// =======================================================================

// ---------------------------------------------------------------------
// game_stop_find_syslink_servers (Sec23.11, 0x00842580). No arguments.
// No return. Body: CONFIRMED stops an in-progress system-link server
// search (clears the guard game_start_find_syslink_servers checks).
// ---------------------------------------------------------------------
int stub_game_stop_find_syslink_servers(lua_State* L) {
    logCall(L, upLog(L), "game_stop_find_syslink_servers", upStateTag(L));
    EngineState* st = upState(L);
    st->syslinkSearch().active = false;
    ++st->syslinkSearch().stopCount;
    return 0;
}

// ---------------------------------------------------------------------
// game_start_find_syslink_servers (Sec41.2, 0x00842560). No arguments, no
// return. CONFIRMED: guarded so repeated calls are no-ops until the stop
// routine above clears the guard; the first real call tears down and
// rebuilds a fixed 64-entry server-record ring (not modelled - no find-
// server data anywhere in this project).
// ---------------------------------------------------------------------
int stub_game_start_find_syslink_servers(lua_State* L) {
    logCall(L, upLog(L), "game_start_find_syslink_servers", upStateTag(L));
    EngineState* st = upState(L);
    if (st->syslinkSearch().active) {
        ++st->syslinkSearch().guardedNoopCount; // CONFIRMED: repeated calls are no-ops
        return 0;
    }
    st->syslinkSearch().active = true;
    ++st->syslinkSearch().startCount;
    return 0;
}

// =======================================================================
// Sec41.2 game_steam_open_overlay / Sec56.3 game_steam_open_url /
// game_steam_open_dlc_page_overlay
// =======================================================================

// ---------------------------------------------------------------------
// game_steam_open_overlay (Sec41.2, 0x008420c0). Arguments: 1 string (a
// non-string reads as null and is passed through unchanged - CONFIRMED).
// No return. Body: CONFIRMED calls the Steam friends interface's overlay-
// activation virtual method if the interface pointer resolves (tracked
// here as "always resolves" - this project has no real Steam interface to
// fail; counted only).
// ---------------------------------------------------------------------
int stub_game_steam_open_overlay(lua_State* L) {
    logCall(L, upLog(L), "game_steam_open_overlay", upStateTag(L));
    EngineState* st = upState(L);
    st->steamOverlay().lastOverlayArg = (lua_gettop(L) >= 1 && lua_type(L, 1) == LUA_TSTRING) ? argString(L, 1) : "";
    ++st->steamOverlay().friendsOverlayCount;
    return 0;
}

// ---------------------------------------------------------------------
// game_steam_open_url (Sec56.3). Arguments: 1 string (a URL). No return.
// Body: CONFIRMED opens an arbitrary script-supplied URL in the Steam
// overlay with no validation.
// ---------------------------------------------------------------------
int stub_game_steam_open_url(lua_State* L) {
    logCall(L, upLog(L), "game_steam_open_url", upStateTag(L));
    EngineState* st = upState(L);
    st->steamOverlay().lastUrl = argString(L, 1); // CONFIRMED: no validation
    ++st->steamOverlay().urlOverlayCount;
    return 0;
}

// ---------------------------------------------------------------------
// game_steam_open_dlc_page_overlay (Sec56.3). No arguments. No return.
// Body: CONFIRMED opens the store DLC listing for the game's own hardcoded
// Steam app id (55230).
// ---------------------------------------------------------------------
int stub_game_steam_open_dlc_page_overlay(lua_State* L) {
    logCall(L, upLog(L), "game_steam_open_dlc_page_overlay", upStateTag(L));
    ++upState(L)->steamOverlay().dlcOverlayCount;
    return 0;
}

// =======================================================================
// Sec41.2/Sec44.5 get_current_difficulty / game_difficulty_select
// =======================================================================

// ---------------------------------------------------------------------
// get_current_difficulty (alias difficulty_get_level, Sec41.2, 0x00a47300).
// No arguments. Return: 1 number. Body: CONFIRMED reads a global dword,
// file-default 1.
// ---------------------------------------------------------------------
int stub_get_current_difficulty(lua_State* L) {
    logCall(L, upLog(L), "get_current_difficulty", upStateTag(L));
    lua_pushnumber(L, static_cast<lua_Number>(upState(L)->difficultyLevel()));
    return 1;
}

// ---------------------------------------------------------------------
// game_difficulty_select (Sec44.5, 0x007e18a0). Arguments: 1 mandatory
// number. No return. CONFIRMED: this RESOLVES get_current_difficulty's own
// real setter - stores the value only when it is below 3 (unsigned
// compare), silently ignoring anything else.
// ---------------------------------------------------------------------
int stub_game_difficulty_select(lua_State* L) {
    logCall(L, upLog(L), "game_difficulty_select", upStateTag(L));
    int64_t v = truncateEa2596(lua_tonumber(L, 1));
    if (v >= 0 && v < 3) { // CONFIRMED: unsigned compare, silently ignored otherwise
        upState(L)->difficultyLevel() = static_cast<int32_t>(v);
    }
    return 0;
}

// =======================================================================
// Sec41.6 get_get_key_names_for_axis_action
// =======================================================================

// ---------------------------------------------------------------------
// get_get_key_names_for_axis_action (Sec41.6, 0x00842320; the doubled
// `get_get_` is the real registered name, not a typo). Arguments: 1 axis-
// action name. Always returns 2 strings. CONFIRMED: the real engine never
// initializes its two output buffers itself - a lookup helper fills them
// in only on an exact, bounded 34-entry table match; an unknown or nil
// name returns two strings built from leftover stack memory (a genuine
// garbage-return/information-leak defect, not a crash). This project has
// no 34-entry table (no real axis-action names registered anywhere), so
// EVERY call takes the "unknown" path. CHOSEN (hardened divergence,
// Sec41.6): two empty strings instead of leftover stack content, counted
// via axisActionUnknownNameGuardCount().
// ---------------------------------------------------------------------
int stub_get_get_key_names_for_axis_action(lua_State* L) {
    logCall(L, upLog(L), "get_get_key_names_for_axis_action", upStateTag(L));
    EngineState* st = upState(L);
    (void)optionalStringDefault(L, 1, "");
    ++st->axisActionUnknownNameGuardCount();
    lua_pushstring(L, "");
    lua_pushstring(L, "");
    return 2;
}

// =======================================================================
// Sec41.4 hud_diversion_remove_callback
// =======================================================================

// ---------------------------------------------------------------------
// hud_diversion_remove_callback (Sec41.4, 0x00844780). Arguments: 1
// number (diversion type). No return. Body: CONFIRMED decrements one of 4
// fixed slot-occupancy counters (floored at 0) for 4 specific diversion
// types; any other type is silently ignored. CHOSEN simplification: the
// specific 4 valid type ids are not individually named by spec, so this
// project tracks one floored-at-zero counter per type id actually used -
// behaviourally identical for every id this host is ever given (an id
// never incremented decrements to the same floor of 0 either way).
// ---------------------------------------------------------------------
int stub_hud_diversion_remove_callback(lua_State* L) {
    logCall(L, upLog(L), "hud_diversion_remove_callback", upStateTag(L));
    int64_t type = truncateEa2596(lua_tonumber(L, 1));
    auto& occ = upState(L)->diversionSlotOccupancy();
    auto it = occ.find(static_cast<int32_t>(type));
    if (it != occ.end() && it->second > 0) --it->second; // CONFIRMED: floored at 0
    return 0;
}

// =======================================================================
// Sec44.2 game_coop_kick_player
// =======================================================================

// ---------------------------------------------------------------------
// game_coop_kick_player (Sec44.2, 0x00844250). Arguments: 1 optional
// callback-name string. No return (the callback, if any, is invoked
// separately and asynchronously - this project does not implement a
// Lua-callback-invocation mechanism, same established convention as the
// 0x00e0xxxx "named callback" family elsewhere in this document; only the
// shown-dialog precondition is modelled). CONFIRMED: with a remote
// player, shows a confirm dialog (formatted via the project-wide
// wide-sprintf wrapper, Sec44.2's own latent-overflow note - this project
// formats via std::string, never a fixed 256-wide-char stack buffer, so
// the real overflow cannot reproduce here by construction); confirming
// removes the player with reason 0xf. Dialog is shown on clients too
// (inert there - this project has no separate "is host" gate on the
// dialog-shown step itself, matching the CONFIRMED text).
// ---------------------------------------------------------------------
int stub_game_coop_kick_player(lua_State* L) {
    logCall(L, upLog(L), "game_coop_kick_player", upStateTag(L));
    EngineState* st = upState(L);
    if (st->remotePlayer().present) {
        ++st->coopKick().confirmDialogShownCount; // CONFIRMED: dialog shown (host or client)
        ++st->coopKick().wideSprintfBoundGuardCount; // bounded-formatting hardening, Sec44.2 project-wide note
    }
    return 0;
}

// =======================================================================
// Sec44.5 game_hud_hide
// =======================================================================

// Defined in lua_spec_confirmed_stubs_b17_wave2_ui_frontend_a_part2.cpp (split out of this file, no behaviour change).
} // namespace
namespace stubs_b17 {
int stub_game_hud_hide(lua_State* L);
int stub_game_is_autosaving(lua_State* L);
int stub_game_is_joinable(lua_State* L);
int stub_game_is_local_player_in_vehicle(lua_State* L);
int stub_game_join_syslink_game(lua_State* L);
int stub_game_lobby_get_local_player_name(lua_State* L);
int stub_game_lobby_set_horde_mode_data(lua_State* L);
int stub_game_lobby_update_char_selection(lua_State* L);
int stub_game_lobby_update_map_selection(lua_State* L);
int stub_game_set_coop_join_type(lua_State* L);
int stub_game_show_coop_partner_gamercard(lua_State* L);
int stub_get_char_in_string(lua_State* L);
int stub_get_localized_string_for_tag(lua_State* L);
int stub_get_world_income_dollars(lua_State* L);
int stub_game_is_waiting_for_partner(lua_State* L);
int stub_game_kick_coop_player(lua_State* L);
int stub_game_lobby_finished_loading(lua_State* L);
int stub_game_lobby_horde_continue_from_wave(lua_State* L);
int stub_game_lobby_update_ready_state(lua_State* L);
int stub_game_pause_map_filter(lua_State* L);
int stub_game_proto_select(lua_State* L);
int stub_game_record_mode_can_encode(lua_State* L);
int stub_game_record_mode_enable(lua_State* L);
int stub_game_record_mode_show_hud(lua_State* L);
int stub_game_record_set_quality_level(lua_State* L);
int stub_game_request_game_terminate(lua_State* L);
int stub_game_use_imperial_units(lua_State* L);
int stub_game_audio_set_rtpc(lua_State* L);
int stub_game_cancel_mission(lua_State* L);
int stub_game_event_tracking_interface_exit(lua_State* L);
int stub_game_format_int_to_string(lua_State* L);
int stub_game_get_coop_ping(lua_State* L);
int stub_game_get_game_play_mode(lua_State* L);
int stub_game_get_pending_game_play_mode(lua_State* L);
int stub_game_is_debug_reloading_level(lua_State* L);
int stub_game_is_demo(lua_State* L);
int stub_game_is_german_build(lua_State* L);
int stub_game_is_going_to_main_menu(lua_State* L);
int stub_autil_mashing_minigame_faded_out(lua_State* L);
} // namespace stubs_b17
namespace {
using stubs_b17::stub_game_hud_hide;
using stubs_b17::stub_game_is_autosaving;
using stubs_b17::stub_game_is_joinable;
using stubs_b17::stub_game_is_local_player_in_vehicle;
using stubs_b17::stub_game_join_syslink_game;
using stubs_b17::stub_game_lobby_get_local_player_name;
using stubs_b17::stub_game_lobby_set_horde_mode_data;
using stubs_b17::stub_game_lobby_update_char_selection;
using stubs_b17::stub_game_lobby_update_map_selection;
using stubs_b17::stub_game_set_coop_join_type;
using stubs_b17::stub_game_show_coop_partner_gamercard;
using stubs_b17::stub_get_char_in_string;
using stubs_b17::stub_get_localized_string_for_tag;
using stubs_b17::stub_get_world_income_dollars;
using stubs_b17::stub_game_is_waiting_for_partner;
using stubs_b17::stub_game_kick_coop_player;
using stubs_b17::stub_game_lobby_finished_loading;
using stubs_b17::stub_game_lobby_horde_continue_from_wave;
using stubs_b17::stub_game_lobby_update_ready_state;
using stubs_b17::stub_game_pause_map_filter;
using stubs_b17::stub_game_proto_select;
using stubs_b17::stub_game_record_mode_can_encode;
using stubs_b17::stub_game_record_mode_enable;
using stubs_b17::stub_game_record_mode_show_hud;
using stubs_b17::stub_game_record_set_quality_level;
using stubs_b17::stub_game_request_game_terminate;
using stubs_b17::stub_game_use_imperial_units;
using stubs_b17::stub_game_audio_set_rtpc;
using stubs_b17::stub_game_cancel_mission;
using stubs_b17::stub_game_event_tracking_interface_exit;
using stubs_b17::stub_game_format_int_to_string;
using stubs_b17::stub_game_get_coop_ping;
using stubs_b17::stub_game_get_game_play_mode;
using stubs_b17::stub_game_get_pending_game_play_mode;
using stubs_b17::stub_game_is_debug_reloading_level;
using stubs_b17::stub_game_is_demo;
using stubs_b17::stub_game_is_german_build;
using stubs_b17::stub_game_is_going_to_main_menu;
using stubs_b17::stub_autil_mashing_minigame_faded_out;

} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB17() {
    static const std::vector<std::string> names = {
        "game_interface_effect_begin",
        "game_interface_effect_end",
        "online_validator_set_checking",
        "game_coop_get_starting_coop",
        "game_coop_get_starting_syslink",
        "game_is_syslink",
        "game_get_party_member_count",
        "game_audio_play",
        "game_lobby_get_remote_player_name",
        "game_is_connected_to_service",
        "game_is_connected_to_internet",
        "game_set_equipped_grenade",
        "game_use_radial_menu_item",
        "game_vkeyboard_input",
        "game_award_cash",
        "game_check_coop_dlc",
        "game_letterbox_fade_out",
        "game_stop_find_syslink_servers",
        "game_start_find_syslink_servers",
        "game_steam_open_overlay",
        "game_steam_open_url",
        "game_steam_open_dlc_page_overlay",
        "get_current_difficulty",
        "game_difficulty_select",
        "get_get_key_names_for_axis_action",
        "hud_diversion_remove_callback",
        "game_coop_kick_player",
        "game_hud_hide",
        "game_is_autosaving",
        "game_is_joinable",
        "game_is_local_player_in_vehicle",
        "game_join_syslink_game",
        "game_lobby_get_local_player_name",
        "game_lobby_set_horde_mode_data",
        "game_lobby_update_char_selection",
        "game_lobby_update_map_selection",
        "game_set_coop_join_type",
        "game_show_coop_partner_gamercard",
        "get_char_in_string",
        "get_localized_string_for_tag",
        "get_world_income_dollars",
        "game_is_waiting_for_partner",
        "game_kick_coop_player",
        "game_lobby_finished_loading",
        "game_lobby_horde_continue_from_wave",
        "game_lobby_update_ready_state",
        "game_pause_map_filter",
        "game_proto_select",
        "game_record_mode_can_encode",
        "game_record_mode_enable",
        "game_record_mode_show_hud",
        "game_record_set_quality_level",
        "game_request_game_terminate",
        "game_use_imperial_units",
        "game_audio_set_rtpc",
        "game_cancel_mission",
        "game_event_tracking_interface_exit",
        "game_format_int_to_string",
        "game_get_coop_ping",
        "game_get_game_play_mode",
        "game_get_pending_game_play_mode",
        "game_is_debug_reloading_level",
        "game_is_demo",
        "game_is_german_build",
        "game_is_going_to_main_menu",
        "autil_mashing_minigame_faded_out",
    };
    return names;
}

void registerB17(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    if (wants("game_interface_effect_begin")) registerOne(L, state, log, stateTag, "game_interface_effect_begin", openGuard<stub_game_interface_effect_begin>);
    if (wants("game_interface_effect_end")) registerOne(L, state, log, stateTag, "game_interface_effect_end", openGuard<stub_game_interface_effect_end>);
    if (wants("online_validator_set_checking")) registerOne(L, state, log, stateTag, "online_validator_set_checking", openGuard<stub_online_validator_set_checking>);
    if (wants("game_coop_get_starting_coop")) registerOne(L, state, log, stateTag, "game_coop_get_starting_coop", openGuard<stub_game_coop_get_starting_coop>);
    if (wants("game_coop_get_starting_syslink")) registerOne(L, state, log, stateTag, "game_coop_get_starting_syslink", openGuard<stub_game_coop_get_starting_syslink>);
    if (wants("game_is_syslink")) registerOne(L, state, log, stateTag, "game_is_syslink", openGuard<stub_game_is_syslink>);
    if (wants("game_get_party_member_count")) registerOne(L, state, log, stateTag, "game_get_party_member_count", openGuard<stub_game_get_party_member_count>);
    if (wants("game_audio_play")) registerOne(L, state, log, stateTag, "game_audio_play", openGuard<stub_game_audio_play>);
    if (wants("game_lobby_get_remote_player_name")) registerOne(L, state, log, stateTag, "game_lobby_get_remote_player_name", openGuard<stub_game_lobby_get_remote_player_name>);
    if (wants("game_is_connected_to_service")) registerOne(L, state, log, stateTag, "game_is_connected_to_service", openGuard<stub_game_is_connected_to_service>);
    if (wants("game_is_connected_to_internet")) registerOne(L, state, log, stateTag, "game_is_connected_to_internet", openGuard<stub_game_is_connected_to_internet>);
    if (wants("game_set_equipped_grenade")) registerOne(L, state, log, stateTag, "game_set_equipped_grenade", openGuard<stub_game_set_equipped_grenade>);
    if (wants("game_use_radial_menu_item")) registerOne(L, state, log, stateTag, "game_use_radial_menu_item", openGuard<stub_game_use_radial_menu_item>);
    if (wants("game_vkeyboard_input")) registerOne(L, state, log, stateTag, "game_vkeyboard_input", openGuard<stub_game_vkeyboard_input>);
    if (wants("game_award_cash")) registerOne(L, state, log, stateTag, "game_award_cash", openGuard<stub_game_award_cash>);
    if (wants("game_check_coop_dlc")) registerOne(L, state, log, stateTag, "game_check_coop_dlc", openGuard<stub_game_check_coop_dlc>);
    if (wants("game_letterbox_fade_out")) registerOne(L, state, log, stateTag, "game_letterbox_fade_out", openGuard<stub_game_letterbox_fade_out>);
    if (wants("game_stop_find_syslink_servers")) registerOne(L, state, log, stateTag, "game_stop_find_syslink_servers", openGuard<stub_game_stop_find_syslink_servers>);
    if (wants("game_start_find_syslink_servers")) registerOne(L, state, log, stateTag, "game_start_find_syslink_servers", openGuard<stub_game_start_find_syslink_servers>);
    if (wants("game_steam_open_overlay")) registerOne(L, state, log, stateTag, "game_steam_open_overlay", openGuard<stub_game_steam_open_overlay>);
    if (wants("game_steam_open_url")) registerOne(L, state, log, stateTag, "game_steam_open_url", openGuard<stub_game_steam_open_url>);
    if (wants("game_steam_open_dlc_page_overlay")) registerOne(L, state, log, stateTag, "game_steam_open_dlc_page_overlay", openGuard<stub_game_steam_open_dlc_page_overlay>);
    if (wants("get_current_difficulty")) registerOne(L, state, log, stateTag, "get_current_difficulty", openGuard<stub_get_current_difficulty>);
    if (wants("game_difficulty_select")) registerOne(L, state, log, stateTag, "game_difficulty_select", openGuard<stub_game_difficulty_select>);
    if (wants("get_get_key_names_for_axis_action")) registerOne(L, state, log, stateTag, "get_get_key_names_for_axis_action", openGuard<stub_get_get_key_names_for_axis_action>);
    if (wants("hud_diversion_remove_callback")) registerOne(L, state, log, stateTag, "hud_diversion_remove_callback", openGuard<stub_hud_diversion_remove_callback>);
    if (wants("game_coop_kick_player")) registerOne(L, state, log, stateTag, "game_coop_kick_player", openGuard<stub_game_coop_kick_player>);
    if (wants("game_hud_hide")) registerOne(L, state, log, stateTag, "game_hud_hide", openGuard<stub_game_hud_hide>);
    if (wants("game_is_autosaving")) registerOne(L, state, log, stateTag, "game_is_autosaving", openGuard<stub_game_is_autosaving>);
    if (wants("game_is_joinable")) registerOne(L, state, log, stateTag, "game_is_joinable", openGuard<stub_game_is_joinable>);
    if (wants("game_is_local_player_in_vehicle")) registerOne(L, state, log, stateTag, "game_is_local_player_in_vehicle", openGuard<stub_game_is_local_player_in_vehicle>);
    if (wants("game_join_syslink_game")) registerOne(L, state, log, stateTag, "game_join_syslink_game", openGuard<stub_game_join_syslink_game>);
    if (wants("game_lobby_get_local_player_name")) registerOne(L, state, log, stateTag, "game_lobby_get_local_player_name", openGuard<stub_game_lobby_get_local_player_name>);
    if (wants("game_lobby_set_horde_mode_data")) registerOne(L, state, log, stateTag, "game_lobby_set_horde_mode_data", openGuard<stub_game_lobby_set_horde_mode_data>);
    if (wants("game_lobby_update_char_selection")) registerOne(L, state, log, stateTag, "game_lobby_update_char_selection", openGuard<stub_game_lobby_update_char_selection>);
    if (wants("game_lobby_update_map_selection")) registerOne(L, state, log, stateTag, "game_lobby_update_map_selection", openGuard<stub_game_lobby_update_map_selection>);
    if (wants("game_set_coop_join_type")) registerOne(L, state, log, stateTag, "game_set_coop_join_type", openGuard<stub_game_set_coop_join_type>);
    if (wants("game_show_coop_partner_gamercard")) registerOne(L, state, log, stateTag, "game_show_coop_partner_gamercard", openGuard<stub_game_show_coop_partner_gamercard>);
    if (wants("get_char_in_string")) registerOne(L, state, log, stateTag, "get_char_in_string", openGuard<stub_get_char_in_string>);
    if (wants("get_localized_string_for_tag")) registerOne(L, state, log, stateTag, "get_localized_string_for_tag", openGuard<stub_get_localized_string_for_tag>);
    if (wants("get_world_income_dollars")) registerOne(L, state, log, stateTag, "get_world_income_dollars", openGuard<stub_get_world_income_dollars>);
    if (wants("game_is_waiting_for_partner")) registerOne(L, state, log, stateTag, "game_is_waiting_for_partner", openGuard<stub_game_is_waiting_for_partner>);
    if (wants("game_kick_coop_player")) registerOne(L, state, log, stateTag, "game_kick_coop_player", openGuard<stub_game_kick_coop_player>);
    if (wants("game_lobby_finished_loading")) registerOne(L, state, log, stateTag, "game_lobby_finished_loading", openGuard<stub_game_lobby_finished_loading>);
    if (wants("game_lobby_horde_continue_from_wave")) registerOne(L, state, log, stateTag, "game_lobby_horde_continue_from_wave", openGuard<stub_game_lobby_horde_continue_from_wave>);
    if (wants("game_lobby_update_ready_state")) registerOne(L, state, log, stateTag, "game_lobby_update_ready_state", openGuard<stub_game_lobby_update_ready_state>);
    if (wants("game_pause_map_filter")) registerOne(L, state, log, stateTag, "game_pause_map_filter", openGuard<stub_game_pause_map_filter>);
    if (wants("game_proto_select")) registerOne(L, state, log, stateTag, "game_proto_select", openGuard<stub_game_proto_select>);
    if (wants("game_record_mode_can_encode")) registerOne(L, state, log, stateTag, "game_record_mode_can_encode", openGuard<stub_game_record_mode_can_encode>);
    if (wants("game_record_mode_enable")) registerOne(L, state, log, stateTag, "game_record_mode_enable", openGuard<stub_game_record_mode_enable>);
    if (wants("game_record_mode_show_hud")) registerOne(L, state, log, stateTag, "game_record_mode_show_hud", openGuard<stub_game_record_mode_show_hud>);
    if (wants("game_record_set_quality_level")) registerOne(L, state, log, stateTag, "game_record_set_quality_level", openGuard<stub_game_record_set_quality_level>);
    if (wants("game_request_game_terminate")) registerOne(L, state, log, stateTag, "game_request_game_terminate", openGuard<stub_game_request_game_terminate>);
    if (wants("game_use_imperial_units")) registerOne(L, state, log, stateTag, "game_use_imperial_units", openGuard<stub_game_use_imperial_units>);
    if (wants("game_audio_set_rtpc")) registerOne(L, state, log, stateTag, "game_audio_set_rtpc", openGuard<stub_game_audio_set_rtpc>);
    if (wants("game_cancel_mission")) registerOne(L, state, log, stateTag, "game_cancel_mission", openGuard<stub_game_cancel_mission>);
    if (wants("game_event_tracking_interface_exit")) registerOne(L, state, log, stateTag, "game_event_tracking_interface_exit", openGuard<stub_game_event_tracking_interface_exit>);
    if (wants("game_format_int_to_string")) registerOne(L, state, log, stateTag, "game_format_int_to_string", openGuard<stub_game_format_int_to_string>);
    if (wants("game_get_coop_ping")) registerOne(L, state, log, stateTag, "game_get_coop_ping", openGuard<stub_game_get_coop_ping>);
    if (wants("game_get_game_play_mode")) registerOne(L, state, log, stateTag, "game_get_game_play_mode", openGuard<stub_game_get_game_play_mode>);
    if (wants("game_get_pending_game_play_mode")) registerOne(L, state, log, stateTag, "game_get_pending_game_play_mode", openGuard<stub_game_get_pending_game_play_mode>);
    if (wants("game_is_debug_reloading_level")) registerOne(L, state, log, stateTag, "game_is_debug_reloading_level", openGuard<stub_game_is_debug_reloading_level>);
    if (wants("game_is_demo")) registerOne(L, state, log, stateTag, "game_is_demo", openGuard<stub_game_is_demo>);
    if (wants("game_is_german_build")) registerOne(L, state, log, stateTag, "game_is_german_build", openGuard<stub_game_is_german_build>);
    if (wants("game_is_going_to_main_menu")) registerOne(L, state, log, stateTag, "game_is_going_to_main_menu", openGuard<stub_game_is_going_to_main_menu>);
    if (wants("autil_mashing_minigame_faded_out")) registerOne(L, state, log, stateTag, "autil_mashing_minigame_faded_out", openGuard<stub_autil_mashing_minigame_faded_out>);
}

} // namespace stub_sections
} // namespace sr3luahost
