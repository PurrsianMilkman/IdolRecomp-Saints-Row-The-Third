#include "sr3luahost/spec_confirmed_stubs.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

// Real Win32 API only (GetKeyNameTextW, WideCharToMultiByte) - a standard,
// publicly-documented OS function, called directly, never reverse-
// engineered content. Same real-header convention src/window.cpp already
// uses elsewhere in this project (plain #include <windows.h>, no
// WIN32_LEAN_AND_MEAN/NOMINMAX games - this project has not needed them).
// Non-Windows builds (the portable Linux/CI build, cloud phase 2026-09-30)
// have no OS key-name source; game_get_key_name then takes its existing
// "no real name resolved" path and returns "".
#ifdef _WIN32
#include <windows.h>
#endif

namespace sr3luahost {

namespace {

EngineState* upState(lua_State* L) {
    return static_cast<EngineState*>(lua_touserdata(L, lua_upvalueindex(1)));
}
HitLog* upLog(lua_State* L) {
    return static_cast<HitLog*>(lua_touserdata(L, lua_upvalueindex(2)));
}
const char* upStateTag(lua_State* L) {
    return lua_tostring(L, lua_upvalueindex(3));
}

// Same real-argc/real-arg-type logging convention every other stub in this
// project uses (stub_registry.cpp's genericStubTrampoline,
// lua_thread_scheduler.cpp's logCall) - folds these 12 names into the
// SAME whole-run HitLog ranking tools/lua_host_run.cpp already reports.
void logCall(lua_State* L, HitLog* log, const char* name, const char* state) {
    if (!log) return;
    int argc = lua_gettop(L);
    std::string types;
    types.reserve(static_cast<size_t>(argc) * 8);
    for (int i = 1; i <= argc; ++i) {
        if (i > 1) types += ",";
        types += lua_typename(L, lua_type(L, i));
    }
    log->record(name, argc, types, state ? state : "");
}

// A plain, portable ASCII case-insensitive compare - used for the two
// reserved sentinel-string checks this batch needs
// ("#CLOSEST_PLAYER#"/ai_add_enemy_target, an action name/
// game_get_key_name_for_action's own single implemented CAA entry). The
// real engine uses the real CRT `__stricmp`/`_stricmp` for these
// (spec-lua-api-behaviour.md's own text) - this is a portable, behaviorally
// equivalent stand-in, not a claim of byte-identical CRT internals.
bool caseInsensitiveEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

// lua_tonumber + 0x00ea2596, the engine's double->int64 conversion. Its
// rounding mode is OPEN (spec-lua-api-behaviour.md Sec4.1 after the
// 2026-09-30 consistency review: Sec2/Sec3/Sec3.9 describe it as
// truncation, round-half-correcting or banker's rounding, pending a re-read).
// Every call site goes through this one function so the choice is made once:
// this project's own CHOSEN stand-in is round-half-to-even (std::nearbyint
// under the default FE_TONEAREST mode), kept from before the review. It only
// matters for non-integral arguments.
int64_t roundToIntOpenMode(lua_Number v) {
    return static_cast<int64_t>(std::nearbyint(v));
}

// Small helper matching this batch's own recurring "mandatory string,
// read via lua_tolstring, NULL-safe" shape.
std::string argString(lua_State* L, int idx) {
    const char* s = lua_tolstring(L, idx, nullptr);
    return s ? s : "";
}

// ---------------------------------------------------------------------
// 1. coop_is_active (spec-lua-api-behaviour.md Sec3.1)
// Arguments: none (the shared prologue calls lua_gettop but never uses
// it - CONFIRMED). Return: 1 boolean, pure query. This task's 12
// in-scope functions include no setter for the underlying co-op session
// state (EngineState::setCoopActive is a C++-only, test-only entry
// point - see engine_state.h).
// ---------------------------------------------------------------------
int stub_coop_is_active(lua_State* L) {
    logCall(L, upLog(L), "coop_is_active", upStateTag(L));
    lua_pushboolean(L, upState(L)->isCoopActive() ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// 2. game_get_key_name (Sec2.3)
// Arguments: 1, read UNCONDITIONALLY (no absence/nil gate - CONFIRMED,
// unlike almost every other function this document covers) via
// lua_tonumber, truncated to an integer key/button code.
// CONFIRMED: sentinel -1 (0xffffffff) -> literal tag "PC_UNBOUND_KEY".
// CONFIRMED (but not reproduced byte-for-byte, see below): every other
// value is passed to the real Win32 GetKeyNameTextW, then re-encoded
// into this project's own escaped byte-stream text format.
// Explicit gaps, stated rather than guessed: (a) the 0-6 mouse-button
// branch's own literal tag strings are never given in the spec text
// (only described structurally) - not implemented; (b) the escaped
// re-encoding step's exact per-character byte semantics are not
// specified precisely enough to reproduce byte-for-byte - this stub
// returns a plain UTF-8 string from the real GetKeyNameTextW result
// instead, which satisfies the CONFIRMED "1 Lua string" return contract
// without claiming byte-identical content to the real engine's own
// internal encoding.
// ---------------------------------------------------------------------
int stub_game_get_key_name(lua_State* L) {
    logCall(L, upLog(L), "game_get_key_name", upStateTag(L));
    lua_Number n = lua_tonumber(L, 1); // real API: an absent/non-number arg 1 reads as 0 here, never an error - matches the spec's own "no absence/nil gate" text.
    int32_t code = static_cast<int32_t>(n);
    if (code == -1) {
        lua_pushstring(L, "PC_UNBOUND_KEY");
        return 1;
    }
    // General case: real, standard Win32 GetKeyNameTextW call - this
    // project's own choice of the standard scan-code-in-bits-16-23 lParam
    // packing (the real call site's own exact lParam construction is OPEN
    // per spec: "a visible mismatch between the three arguments the call
    // site passes and the callee's own recovered signature ... not pinned
    // down with full confidence").
#ifdef _WIN32
    LONG lParam = (static_cast<LONG>(code) & 0xFF) << 16;
    wchar_t buf[64] = {};
    int len = GetKeyNameTextW(lParam, buf, 64);
    if (len <= 0) {
        lua_pushstring(L, ""); // no real name resolved - honest empty result, not a fabricated placeholder
        return 1;
    }
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, buf, len, nullptr, 0, nullptr, nullptr);
    std::string utf8;
    if (utf8Len > 0) {
        utf8.resize(static_cast<size_t>(utf8Len));
        WideCharToMultiByte(CP_UTF8, 0, buf, len, utf8.data(), utf8Len, nullptr, nullptr);
    }
    lua_pushlstring(L, utf8.data(), utf8.size());
    return 1;
#else
    lua_pushstring(L, ""); // no OS key-name source off Windows - same honest empty result as a failed lookup
    return 1;
#endif
}

// ---------------------------------------------------------------------
// 3. game_UI_audio_play (Sec2.2)
// Arguments: 1, optional, polymorphic (string -> Wwise-ID resolve;
// number -> already-resolved id). CONFIRMED: a voice/play-instance
// handle is returned EVERY time, regardless of whether the sound
// argument resolved to anything ("or a handle to an unbound, still-
// created voice if no sound argument was given"). This project has no
// real Wwise voice object to create - EngineState::nextAudioVoiceHandle()
// is this project's own stated stand-in (a plain incrementing counter,
// same shape choice as ThreadScheduler::newThread, thread_scheduler.h).
// ---------------------------------------------------------------------
int stub_game_UI_audio_play(lua_State* L) {
    logCall(L, upLog(L), "game_UI_audio_play", upStateTag(L));
    if (lua_gettop(L) >= 1) {
        // Read for real, matching the real body's own polymorphic shape -
        // neither branch changes the handle this project returns (see
        // this function's own doc comment above), since there is no real
        // Wwise ID table here to resolve a string against.
        if (lua_type(L, 1) == LUA_TSTRING) {
            (void)lua_tolstring(L, 1, nullptr);
        } else {
            (void)lua_tonumber(L, 1);
        }
    }
    int64_t handle = upState(L)->nextAudioVoiceHandle();
    lua_pushnumber(L, static_cast<lua_Number>(handle));
    return 1;
}

// ---------------------------------------------------------------------
// 4. game_audio_get_audio_id (Sec8.22)
// Arguments: 1, optional, polymorphic. CONFIRMED: absent -> 0; number ->
// round-then-mask-to-16-bits; string "none"/"" (case-insensitive) -> 0
// (the shared Wwise-resolver sentinel, Sec2's own method note). Explicit
// gap: any OTHER string would resolve through the real
// AK::SoundEngine::GetIDFromString hash, whose real algorithm is a
// Wwise-SDK-internal detail no spec-*.md file in this project states -
// implementing it would require outside knowledge beyond this project's
// clean-room sources, so it falls back to 0 (the same value as the
// invalid sentinel) rather than fabricating a hash.
// ---------------------------------------------------------------------
int stub_game_audio_get_audio_id(lua_State* L) {
    logCall(L, upLog(L), "game_audio_get_audio_id", upStateTag(L));
    double result = 0.0;
    if (lua_gettop(L) >= 1) {
        int t = lua_type(L, 1);
        if (t == LUA_TNUMBER) {
            double raw = lua_tonumber(L, 1);
            int64_t rounded = roundToIntOpenMode(raw); // "round-to-int pair"; rounding mode OPEN, see roundToIntOpenMode
            result = static_cast<double>(static_cast<uint32_t>(rounded) & 0xFFFFu); // masked to 16 bits, CONFIRMED
        } else if (t == LUA_TSTRING) {
            std::string s = argString(L, 1);
            if (s.empty() || caseInsensitiveEquals(s, "none")) {
                result = 0.0; // CONFIRMED sentinel
            } else {
                result = 0.0; // TODO / explicit gap - see this function's own doc comment above
            }
        }
        // else: present but neither string nor number - not covered by the
        // spec's own described branches; result stays 0 rather than guessed.
    }
    lua_pushnumber(L, result);
    return 1;
}

// ---------------------------------------------------------------------
// 5. game_get_key_name_for_action (Sec8.23)
// Arguments: 1 mandatory string (action name), read unconditionally.
// CONFIRMED literal: the axis name "CAA_CAMERA_ROTATE" (index 0 of the
// CAA table, spec-tables-ui-controls.md Sec4.2) resolves to
// "STR_THE_MOUSE". CONFIRMED default: any other/no match yields an
// empty string. Explicit gap: the primary CBA (button-bound-key) branch
// and the CAA_WALK_FORWARD_BACKWARD/CAA_WALK_TURN_LEFT_RIGHT two-key
// formatted branch both need the real PER-ACTION CURRENTLY-BOUND-KEY
// data, which lives in a separate real xtbl file
// (control_binding_sets.xtbl) this task's minimal state-layer scope does
// not load - not fabricated, left as the spec's own confirmed empty-
// string default.
// ---------------------------------------------------------------------
int stub_game_get_key_name_for_action(lua_State* L) {
    logCall(L, upLog(L), "game_get_key_name_for_action", upStateTag(L));
    std::string name = argString(L, 1);
    if (caseInsensitiveEquals(name, "CAA_CAMERA_ROTATE")) {
        lua_pushstring(L, "STR_THE_MOUSE");
        return 1;
    }
    lua_pushstring(L, "");
    return 1;
}

// ---------------------------------------------------------------------
// 6. game_peg_load_with_cb (Sec8.24)
// Arguments: up to 8 (request name, count N, up to N filenames).
// CONFIRMED: arg1 bounds-checked to 64 bytes; arg2 (count) only used
// when in range 1..6; filenames read while present, stopping early at
// the first absent slot; return 0 always. Each filename's real,
// CONFIRMED, empirically-vector-validated hash bucket (bucket_count=9000,
// engine_state.h's own multiply33XorHashBucket) is computed and stored -
// the one real, checkable number this minimal registry can reproduce
// exactly from this function's own confirmed shared resolution chain
// with game_peg_unload (Sec2.7). Explicit gap: the completion-sweep/
// callback-dispatch behavior (Sec8.24's own closing paragraph) is not
// modeled - see PegLoadRequest's own doc comment (engine_state.h).
// ---------------------------------------------------------------------
int stub_game_peg_load_with_cb(lua_State* L) {
    logCall(L, upLog(L), "game_peg_load_with_cb", upStateTag(L));
    PegLoadRequest req;
    std::string name = argString(L, 1);
    if (name.size() > 64) name.resize(64); // CONFIRMED bounds check
    req.requestName = name;

    double nRaw = lua_tonumber(L, 2);
    int64_t count = roundToIntOpenMode(nRaw);
    if (count >= 1 && count <= 6) {
        for (int64_t i = 0; i < count; ++i) {
            int argIndex = 3 + static_cast<int>(i);
            if (lua_gettop(L) < argIndex || lua_type(L, argIndex) != LUA_TSTRING) break; // "breaking out early on any absent slot"
            PegLoadFilenameEntry entry;
            entry.name = argString(L, argIndex);
            entry.bucketIndex = EngineState::multiply33XorHashBucket(entry.name, 9000);
            req.filenames.push_back(std::move(entry));
        }
    }
    upState(L)->pegLoadRequests().push_back(std::move(req));
    return 0;
}

// ---------------------------------------------------------------------
// 7. ai_add_enemy_target (Sec3.9)
// Arguments: 4 (acting character, target name or "#CLOSEST_PLAYER#",
// priority/id number [rounded via 0x00ea2596, mode OPEN since the 2026-09-30 review; was labelled CONFIRMED per the primitive-
// upgrade note], optional bool default false). Return: 1 boolean
// (resolve-and-add success/failure - always true in this minimal
// registry, see engine_state.h's own EnemyTargetRecord doc comment).
// ---------------------------------------------------------------------
int stub_ai_add_enemy_target(lua_State* L) {
    logCall(L, upLog(L), "ai_add_enemy_target", upStateTag(L));
    std::string actor = argString(L, 1);
    std::string target = argString(L, 2);
    double priorityRaw = lua_tonumber(L, 3);
    int64_t priority = roundToIntOpenMode(priorityRaw); // 0x00ea2596; rounding mode OPEN, see roundToIntOpenMode
    bool arg4 = false;
    if (lua_gettop(L) >= 4 && lua_type(L, 4) != LUA_TNIL) arg4 = lua_toboolean(L, 4) != 0;
    bool sentinelMatch = caseInsensitiveEquals(target, "#CLOSEST_PLAYER#");

    EnemyTargetRecord rec;
    rec.priorityOrId = priority;
    rec.arg4Flag = arg4;
    rec.sentinelMatchFlag = sentinelMatch;
    std::string key = (priority != 0) ? std::to_string(priority) : target;

    CharacterState& actorState = upState(L)->getOrCreateCharacter(actor);
    actorState.enemyTargets[key] = rec;

    lua_pushboolean(L, 1);
    return 1;
}

// ---------------------------------------------------------------------
// 8. on_take_damage (Sec3.13)
// Arguments: 2 mandatory strings (callback name, target name). Return:
// none. This registry only models "character"-shaped objects (see
// engine_state.h's own top note) - every resolve branch the spec
// documents (generic/#PLAYER#, plain character, vehicle, 4th kind)
// lands on the SAME CharacterState::onTakeDamageCallback field here,
// a stated simplification.
// ---------------------------------------------------------------------
int stub_on_take_damage(lua_State* L) {
    logCall(L, upLog(L), "on_take_damage", upStateTag(L));
    std::string cb = argString(L, 1);
    std::string target = argString(L, 2);
    CharacterState& targetState = upState(L)->getOrCreateCharacter(target);
    targetState.onTakeDamageCallback = cb;
    return 0;
}

// ---------------------------------------------------------------------
// 9. set_ignore_ai_flag (Sec3.4)
// Arguments: 1 mandatory string, 1 optional bool (nil-gated idiom,
// DEFAULT TRUE when omitted/nil - CONFIRMED, the one function in this
// batch defaulting a nil-gated bool to true rather than false). Writes
// CharacterState::ignoreAI directly (this project always applies the
// write - see EngineState::replicateStateChange's own doc comment for
// why) and calls the no-op replication stand-in. CONFIRMED structure,
// HIGH-CONFIDENCE-tagged sub-detail: when newly enabling (false->true),
// conditionally sets/clears CharacterState::actionOverrideId.
// ---------------------------------------------------------------------
int stub_set_ignore_ai_flag(lua_State* L) {
    logCall(L, upLog(L), "set_ignore_ai_flag", upStateTag(L));
    std::string name = argString(L, 1);
    bool newValue = true; // CONFIRMED default when arg 2 omitted/nil
    if (lua_gettop(L) >= 2 && lua_type(L, 2) != LUA_TNIL) newValue = lua_toboolean(L, 2) != 0;

    CharacterState& character = upState(L)->getOrCreateCharacter(name);
    bool wasIgnoring = character.ignoreAI;
    character.ignoreAI = newValue;
    EngineState::replicateStateChange("set_ignore_ai_flag", name);

    if (newValue && !wasIgnoring) {
        // HIGH CONFIDENCE only for the real-world meaning of id 0x19 and
        // for this exact gating condition (spec's own words) - the
        // STRUCTURE (state-enum-gated conditional override write) itself
        // is CONFIRMED.
        if (character.stateEnum == 3) {
            character.actionOverrideId = 0x19;
        } else if (character.attackerThreatRef != 0) {
            character.actionOverrideId = 0;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// 10. get_max_hit_points (Sec7.12)
// Arguments: 1 mandatory string. Return: 1 number (the CONFIRMED,
// cross-checked-against-set_current_hit_points +0x1cac integer field,
// "stored as a plain integer, converted to float on read"). Pure query.
// ---------------------------------------------------------------------
int stub_get_max_hit_points(lua_State* L) {
    logCall(L, upLog(L), "get_max_hit_points", upStateTag(L));
    std::string name = argString(L, 1);
    CharacterState& character = upState(L)->getOrCreateCharacter(name);
    lua_pushnumber(L, static_cast<lua_Number>(character.maxHitPoints));
    return 1;
}

// ---------------------------------------------------------------------
// 11. set_current_hit_points (Sec7.31)
// Arguments: 1 mandatory string, 1 number read UNCONDITIONALLY (no
// nil/absence gate - omission resolves to 0 via lua_tonumber's own real
// nil-handling, CONFIRMED). CONFIRMED: clamps into [0, cap] where cap is
// the SAME field get_max_hit_points reads; always applied directly here
// (see EngineState::replicateStateChange's own doc comment). HIGH-
// CONFIDENCE-tagged: when the clamped value is <= 0, this project sets
// CharacterState::isDeadHighConfidence - the spec's own further gating
// predicates (0x00973d40/0x0096f540) are OPEN, not even HIGH CONFIDENCE,
// and are not modeled (stated simplification, not silently dropped).
// ---------------------------------------------------------------------
int stub_set_current_hit_points(lua_State* L) {
    logCall(L, upLog(L), "set_current_hit_points", upStateTag(L));
    std::string name = argString(L, 1);
    double raw = lua_tonumber(L, 2); // real API: absent -> 0.0, CONFIRMED nil-handling

    CharacterState& character = upState(L)->getOrCreateCharacter(name);
    int32_t cap = character.maxHitPoints; // CONFIRMED cross-check: same field get_max_hit_points reads
    int64_t rounded = roundToIntOpenMode(raw); // 0x00ea2596; rounding mode OPEN, see roundToIntOpenMode
    int64_t clampedWide = std::max<int64_t>(0, std::min<int64_t>(rounded, cap));
    int32_t clamped = static_cast<int32_t>(clampedWide);

    character.currentHitPoints = clamped;
    EngineState::replicateStateChange("set_current_hit_points", name);

    if (clamped <= 0) {
        character.isDeadHighConfidence = true; // HIGH CONFIDENCE only - see CharacterState::isDeadHighConfidence's own doc comment
    }
    return 0;
}

// ---------------------------------------------------------------------
// 12. ai_clear_scripted_action (Sec7.24)
// Arguments: 1 mandatory string, resolved. CONFIRMED: calls a single-
// argument clear routine (0x004f4c90) whose own body was NOT
// independently decompiled - OPEN, not even HIGH CONFIDENCE, per the
// spec's own text. Implementing a specific cleared field would be
// guessing past that OPEN tag - explicit, stated gap: this stub performs
// only the confirmed resolve step (touching the registry entry into
// existence, same as every other resolver-using function in this batch)
// and returns.
// ---------------------------------------------------------------------
int stub_ai_clear_scripted_action(lua_State* L) {
    logCall(L, upLog(L), "ai_clear_scripted_action", upStateTag(L));
    std::string name = argString(L, 1);
    (void)upState(L)->getOrCreateCharacter(name);
    return 0;
}

// ---------------------------------------------------------------------
// 13. vint_object_find (spec-lua-bindings.md Sec13.7/Sec15) - folded in
// mid-task, see this file's own header doc comment for why. Arguments:
// variable arity 1-3 (name string mandatory; parent_handle number
// optional; doc_handle number optional). CONFIRMED, the critical fact for
// a stub: ALWAYS exactly 1 Lua value, a number, on every path including
// every failure path - never nil, never 0 results. This project has no
// real VDO object tree pre-populated by any Lua-visible call in this
// pass's scope (vint_object_create remains a generic stub) - see
// engine_state.h's own VdoObject doc comment for why every REAL script
// call this pass can exercise therefore honestly takes the real
// CONFIRMED "not found" (0.0) path, which is itself correct behavior for
// a never-populated object space, not a shortcut around the real
// mechanism (EngineState::findVdoObject implements the real parent-then-
// document-scoped lookup in full, exercised directly by this file's own
// tests via EngineState::registerVdoObjectForTesting()).
// ---------------------------------------------------------------------
int stub_vint_object_find(lua_State* L) {
    logCall(L, upLog(L), "vint_object_find", upStateTag(L));
    std::string name = argString(L, 1);

    bool hasParent = lua_gettop(L) >= 2 && lua_type(L, 2) == LUA_TNUMBER; // CONFIRMED: "absent or wrong type -> falls back to a document-relative search"
    uint32_t parentHandle = hasParent ? static_cast<uint32_t>(lua_tonumber(L, 2)) : 0;
    bool hasDoc = lua_gettop(L) >= 3 && lua_type(L, 3) == LUA_TNUMBER;
    uint32_t docHandle = hasDoc ? static_cast<uint32_t>(lua_tonumber(L, 3)) : 0;

    uint32_t handle = upState(L)->findVdoObject(name, hasParent, parentHandle, hasDoc, docHandle);
    lua_pushnumber(L, static_cast<lua_Number>(handle)); // real handle, or the literal 0.0 on any failure path - CONFIRMED, never nil (handle 0 and Lua number 0.0 are observably identical to a caller, matching the real contract exactly)
    return 1;
}

// ---------------------------------------------------------------------
// 14. store_vehicle_get_state (spec-lua-api-behaviour.md Sec10.1)
// Arguments: none (CONFIRMED - shared lua_gettop prologue result never
// read again, same shape as coop_is_active/§3.1 above). Return: 1 number
// (NOT a boolean - lua_pushnumber, CONFIRMED), 0.0 or 1.0 mirroring
// global flag 0x022cdf08. Pure query; this task's 9 in-scope functions
// include no real setter (the real writer, store_vehicle_change_mode, is
// a different, out-of-scope registered name) - EngineState::
// setVehicleStoreActiveForTesting() is a C++-only test entry point, same
// precedent as setCoopActive().
// ---------------------------------------------------------------------
int stub_store_vehicle_get_state(lua_State* L) {
    logCall(L, upLog(L), "store_vehicle_get_state", upStateTag(L));
    lua_pushnumber(L, upState(L)->isVehicleStoreActive() ? 1.0 : 0.0);
    return 1;
}

// ---------------------------------------------------------------------
// 15. Completion_is_client (Sec10.2)
// Arguments: none (same unused-lua_gettop shape). Return: 1 boolean
// (CONFIRMED, lua_pushboolean). Real formula (CONFIRMED, cross-checked
// against game_get_is_host's/Sec8.27's own already-decompiled body,
// which is OUT of this task's scope but already exists as a documented
// citation in the spec): "is there an active co-op session AND is this
// machine NOT the host" - isCoopActive() && !isHost(); returns false
// (not the literal complement of game_get_is_host) when no session
// exists at all.
// ---------------------------------------------------------------------
int stub_Completion_is_client(lua_State* L) {
    logCall(L, upLog(L), "Completion_is_client", upStateTag(L));
    bool result = upState(L)->isCoopActive() && !upState(L)->isHost();
    lua_pushboolean(L, result ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// 16. game_hud_update_inventory (Sec10.3)
// Arguments: none. Return: none (return 0, CONFIRMED, no push). Real
// body: if a local player object exists, triggers a refresh of that
// player's own HUD inventory display (OPEN: full rebuild vs. dirty-flag
// set, not modeled - Sec10.3's own text). This project's minimal stand-
// in: EngineState::hasLocalPlayer() (default true) gates a plain
// increment of EngineState::hudInventoryRefreshCount() - no real HUD to
// refresh.
// ---------------------------------------------------------------------
int stub_game_hud_update_inventory(lua_State* L) {
    logCall(L, upLog(L), "game_hud_update_inventory", upStateTag(L));
    if (upState(L)->hasLocalPlayer()) {
        upState(L)->recordHudInventoryRefresh();
    }
    return 0;
}

// ---------------------------------------------------------------------
// 17. tutorial_advance (Sec10.4)
// Arguments: 1 mandatory string, read unconditionally with no nil/
// absence gate (CONFIRMED) - a tutorial/hint id. Return: 1 boolean
// (CONFIRMED). Real body resolves the id through the SAME 210-entry
// tutorial-descriptor table tutorial_start (Sec6.19) uses, gated on a
// bounds check AND a per-entry kind/type-tag==3 check (CONFIRMED,
// Sec10.4) - this codebase has no reader/table for that real xtbl-driven
// data anywhere (checked directly: no tutorial-descriptor table exists
// in this project), so re-deriving the real bounds/tag gate is out of
// scope. Explicit, stated simplification (NOT a claim of having the real
// table, per this task's own brief): any non-empty string id resolves
// (returns true); an empty/absent id does not (returns false). On a
// resolved id, this project's own no-op stand-in for the real named
// event/telemetry-shaped scope the real function opens (OPEN tier,
// Sec10.4) is a plain per-id call counter, EngineState::
// recordTutorialAdvance()/tutorialAdvanceCount().
// ---------------------------------------------------------------------
int stub_tutorial_advance(lua_State* L) {
    logCall(L, upLog(L), "tutorial_advance", upStateTag(L));
    std::string id = argString(L, 1);
    bool resolved = !id.empty();
    if (resolved) {
        upState(L)->recordTutorialAdvance(id);
    }
    lua_pushboolean(L, resolved ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// 18. minimap_icon_add_do (Sec10.5)
// Arguments: 5 - arg1 string mandatory (object name), arg2 string
// mandatory (icon-type name), arg3 string optional/nil-gated default ""
// (CONFIRMED), arg4 number optional/nil-gated default 0.0 (CONFIRMED),
// arg5 number/flag optional/nil-gated default 3 (CONFIRMED). Return:
// none - every real path returns 0 with no lua_push* call (CONFIRMED).
// Real body is a genuinely bit-tested two-way dispatch (direct add vs. a
// filtered-list broadcast under the DEFAULT arg5 value, per Sec10.5's
// own adversarial-review correction) resolved against real tables this
// project does not have - NOT modeled here, per this task's own brief.
// Minimal stand-in: arg1 resolved through the SAME by-name-key
// getOrCreateCharacter convention this registry already uses everywhere
// else; the call's own raw params are recorded into that object's own
// MinimapIconRecord vector (engine_state.h) so a test can assert it was
// recorded - this project models "an icon-add was requested with these
// params," not the real dispatch branch chosen (see MinimapIconRecord's
// own doc comment for the full reasoning).
// ---------------------------------------------------------------------
int stub_minimap_icon_add_do(lua_State* L) {
    logCall(L, upLog(L), "minimap_icon_add_do", upStateTag(L));
    std::string objectName = argString(L, 1);
    MinimapIconRecord rec;
    rec.iconType = argString(L, 2);
    if (lua_gettop(L) >= 3 && lua_type(L, 3) != LUA_TNIL) rec.group = argString(L, 3);
    if (lua_gettop(L) >= 4 && lua_type(L, 4) != LUA_TNIL) rec.param4 = lua_tonumber(L, 4);
    if (lua_gettop(L) >= 5 && lua_type(L, 5) != LUA_TNIL) {
        rec.flag5 = roundToIntOpenMode(lua_tonumber(L, 5));
    }
    CharacterState& obj = upState(L)->getOrCreateCharacter(objectName);
    obj.minimapIcons.push_back(std::move(rec));
    return 0;
}

// ---------------------------------------------------------------------
// 19. object_indicator_add_do (Sec10.6)
// Arguments: 5 - arg1 string mandatory (object name), arg2 number/flag
// mandatory with NO nil/absence gate at all (CONFIRMED - a genuine
// difference from every optional sibling argument in this cluster), arg3
// number/flag mandatory, also NO nil gate (CONFIRMED), arg4 number/flag
// optional/nil-gated default 3 (CONFIRMED), arg5 number optional/nil-
// gated default 100.0 (a named .rdata float constant, CONFIRMED - NOT a
// bare 0.0 like several sibling functions elsewhere in this document).
// Return: 1 boolean, pushed on EVERY path including failure (CONFIRMED -
// "the function still runs to completion and pushes false rather than
// short-circuiting early" on a real resolve failure). This project's own
// minimal by-name resolver (getOrCreateCharacter) can never itself fail
// to "resolve" a name (it default-constructs on first reference, per
// this registry's own established convention), so every call through
// this minimal stand-in genuinely reaches the real function's own
// SUCCESS return path - true is pushed always, not because the real
// function is claimed to always succeed (Sec10.6 explicitly documents a
// real, separate failure path this project's registry structurally
// cannot exercise), but because this minimal stand-in has no failure
// condition to model honestly. Minimal stand-in for the body: same
// getOrCreateCharacter resolve as minimap_icon_add_do (Sec10.5) above;
// the call's own raw params recorded into that object's own
// ObjectIndicatorRecord vector (engine_state.h) - the real HUD dispatch
// chain (0x008f96f0/0x008f50d0) is NOT modeled.
// ---------------------------------------------------------------------
int stub_object_indicator_add_do(lua_State* L) {
    logCall(L, upLog(L), "object_indicator_add_do", upStateTag(L));
    std::string objectName = argString(L, 1);
    ObjectIndicatorRecord rec;
    rec.arg2 = roundToIntOpenMode(lua_tonumber(L, 2)); // CONFIRMED: no nil/absence gate - real API: absent -> 0.0 via lua_tonumber's own real nil-handling
    rec.arg3 = roundToIntOpenMode(lua_tonumber(L, 3)); // same, CONFIRMED no nil gate
    if (lua_gettop(L) >= 4 && lua_type(L, 4) != LUA_TNIL) rec.arg4 = roundToIntOpenMode(lua_tonumber(L, 4));
    if (lua_gettop(L) >= 5 && lua_type(L, 5) != LUA_TNIL) rec.arg5 = lua_tonumber(L, 5);
    CharacterState& obj = upState(L)->getOrCreateCharacter(objectName);
    obj.objectIndicators.push_back(std::move(rec));
    lua_pushboolean(L, 1); // see this function's own doc comment above for why this minimal stand-in always reaches the real "success" push path
    return 1;
}

// ---------------------------------------------------------------------
// 20. on_qte_animation_trigger (Sec10.7)
// Arguments: 1 mandatory string, read unconditionally with no nil/
// absence gate (CONFIRMED) - a callback name. Return: none (return 0,
// CONFIRMED). Real body registers (non-empty name) or clears (empty
// name) a SINGLE GLOBAL QTE-animation-trigger hook slot, rooted at a
// fixed global base rather than a per-object slot array (CONFIRMED,
// disassembly, Sec10.7) - a genuinely different shape from the
// on_death/on_take_damage/on_revived per-CHARACTER hook-slot family, so
// this project's matching minimal stand-in is a single std::string field
// directly on EngineState (EngineState::qteAnimationTriggerCallback()),
// not per-character. Empty-string argument clears the field to empty.
// ---------------------------------------------------------------------
int stub_on_qte_animation_trigger(lua_State* L) {
    logCall(L, upLog(L), "on_qte_animation_trigger", upStateTag(L));
    std::string cb = argString(L, 1);
    upState(L)->setQteAnimationTriggerCallback(cb); // empty string clears the slot - CONFIRMED, Sec10.7/Sec10.8's shared 0x005e4660 body
    return 0;
}

// ---------------------------------------------------------------------
// 21. on_revived (Sec10.8)
// Arguments: 2 mandatory strings, both read unconditionally with no nil/
// absence gate (CONFIRMED) - arg1 callback name, arg2 character name.
// Return: none (return 0, CONFIRMED). Real body resolves arg2 directly
// through the plain character resolver (never the generic/"#PLAYER#"
// chain first - a genuine, checked difference from on_death/
// on_vehicle_destroyed, Sec10.8) and registers the callback via the SAME
// generic character-hook-slot mechanism on_death/on_take_damage/
// on_attack_performed/on_vehicle_destroyed all reach, at numeric slot 2
// (CONFIRMED, no collision against this document's existing per-
// character slot catalogue). CONFIRMED real refinement (Sec10.8's own
// re-decompilation): an empty-string callback name CLEARS the slot
// rather than setting it. This registry follows the SAME single-
// dedicated-field-per-hook-kind convention on_take_damage's own
// onTakeDamageCallback field already established (CharacterState,
// engine_state.h) rather than a generic slot-array abstraction: a new
// named field, onRevivedCallback, set via the plain getOrCreateCharacter
// resolver, cleared on an empty-string callback argument.
// ---------------------------------------------------------------------
int stub_on_revived(lua_State* L) {
    logCall(L, upLog(L), "on_revived", upStateTag(L));
    std::string cb = argString(L, 1);
    std::string target = argString(L, 2);
    CharacterState& character = upState(L)->getOrCreateCharacter(target);
    character.onRevivedCallback = cb; // empty string clears the slot - CONFIRMED, Sec10.8
    return 0;
}

// ---------------------------------------------------------------------
// 22. game_get_coop_join_type (Sec10.9)
// Arguments: none (same unused-lua_gettop shape as §10.1-§10.3). Return:
// 1 number (CONFIRMED, lua_pushnumber) - a code 0, 1, or 2 read from a
// real, host-authoritative, network-replicated global (CONFIRMED, the
// global's sole writer traced and gated on the identical host-check
// singleton §8.27 establishes). This project builds no real networking
// layer (explicit, standing project convention, same as coopActive_/
// replicateStateChange) - EngineState::coopJoinType() is this project's
// own minimal int stand-in, default 0, test-only setter (no real setter
// is in scope - the real writer is a different, out-of-scope function).
// ---------------------------------------------------------------------
int stub_game_get_coop_join_type(lua_State* L) {
    logCall(L, upLog(L), "game_get_coop_join_type", upStateTag(L));
    lua_pushnumber(L, static_cast<lua_Number>(upState(L)->coopJoinType()));
    return 1;
}

// ---------------------------------------------------------------------
// 23. zscene_is_loaded (spec-lua-api-behaviour.md Sec14.23)
// Arguments: 1 optional string, standard nil-gated idiom, default absent
// (CONFIRMED). Return: 1 boolean (CONFIRMED). Pure query.
// CONFIRMED: the two-tier dispatch and tier 1's exact test (a named
// scene's per-name state reads exactly 1 -> true). Tier 2's global-flag
// and global-state-code (== 2) tests are implemented as literal reads of
// EngineState's opaque stand-ins; no meaning is given to the codes (OPEN).
// OPEN, stubbed: the tier-2 per-record branch, whose read sense is
// "apparently inverted" relative to tier 1 and not reconciled. Reaching it
// returns false (the falsy result the generic stub gave before this
// function existed) and bumps EngineState::zsceneOpenBranchHits().
// Added in the cloud phase (2026-09-30) as a mission-driving blocker
// (HANDOFF Sec9.143: 714K busy-poll calls in the first mission run).
// ---------------------------------------------------------------------
int stub_zscene_is_loaded(lua_State* L) {
    logCall(L, upLog(L), "zscene_is_loaded", upStateTag(L));
    EngineState* es = upState(L);
    bool hasName = lua_gettop(L) >= 1 && lua_type(L, 1) != LUA_TNIL;
    std::string name = hasName ? argString(L, 1) : std::string();

    // Tier 1 (0x00723d20), only when a name is given.
    int fastState = 0;
    if (hasName && es->zsceneFastPathState(name, fastState) && fastState == 1) {
        lua_pushboolean(L, 1);
        return 1;
    }
    // Tier 2 (0x00721db0).
    if (es->zsceneBusyFlag()) {
        lua_pushboolean(L, 1);
        return 1;
    }
    if (hasName && es->zsceneHasTableRecord(name)) {
        // OPEN (Sec14.23): per-record test with unreconciled sense - not implemented.
        es->recordZsceneOpenBranchHit();
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, es->zsceneGlobalStateCode() == 2 ? 1 : 0);
    return 1;
}

void registerOne(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const char* name, lua_CFunction fn) {
    lua_pushlightuserdata(L, &state);
    lua_pushlightuserdata(L, &log);
    lua_pushstring(L, stateTag.c_str());
    lua_pushcclosure(L, fn, 3);
    lua_setglobal(L, name);
}

} // namespace

const std::vector<std::string>& specConfirmedStubNames() {
    static const std::vector<std::string> names = {
        "coop_is_active",
        "game_get_key_name",
        "game_UI_audio_play",
        "game_audio_get_audio_id",
        "game_get_key_name_for_action",
        "game_peg_load_with_cb",
        "ai_add_enemy_target",
        "on_take_damage",
        "set_ignore_ai_flag",
        "get_max_hit_points",
        "set_current_hit_points",
        "ai_clear_scripted_action",
        "vint_object_find",
        // The 9 names from spec-lua-api-behaviour.md Sec10.1-Sec10.9,
        // folded in as a same-shape follow-up pass (see this file's own
        // functions 14-22 above for each one's own citation) - real
        // cluster tags verified directly against
        // tools/lua_all_registered_1490_tagged.txt (grepped, not
        // assumed): store_vehicle_get_state/Completion_is_client/
        // game_hud_update_inventory/game_get_coop_join_type are `ui`;
        // minimap_icon_add_do/object_indicator_add_do/
        // on_qte_animation_trigger/on_revived/tutorial_advance are
        // `gameplay`.
        "store_vehicle_get_state",
        "Completion_is_client",
        "game_hud_update_inventory",
        "tutorial_advance",
        "minimap_icon_add_do",
        "object_indicator_add_do",
        "on_qte_animation_trigger",
        "on_revived",
        "game_get_coop_join_type",
        // Cloud phase (2026-09-30): Sec14.23, tagged `gameplay` in
        // tools/lua_all_registered_1490_tagged.txt (line 1014).
        "zscene_is_loaded",
    };
    return names;
}

void registerSpecConfirmedStubs(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                                 const std::vector<std::string>& namesToRegister) {
    auto wants = [&namesToRegister](const char* name) {
        for (const auto& n : namesToRegister) {
            if (n == name) return true;
        }
        return false;
    };
    // Each call is a no-op unless `name` is really present in
    // `namesToRegister` - see this function's own doc comment
    // (spec_confirmed_stubs.h) for why this filter exists (these 13 names
    // do not all share one real registered cluster tag, so a single
    // Host construction calls this function twice - once per state - each
    // time with only that state's own real subset).
    if (wants("coop_is_active")) registerOne(L, state, log, stateTag, "coop_is_active", stub_coop_is_active);
    if (wants("game_get_key_name")) registerOne(L, state, log, stateTag, "game_get_key_name", stub_game_get_key_name);
    if (wants("game_UI_audio_play")) registerOne(L, state, log, stateTag, "game_UI_audio_play", stub_game_UI_audio_play);
    if (wants("game_audio_get_audio_id")) registerOne(L, state, log, stateTag, "game_audio_get_audio_id", stub_game_audio_get_audio_id);
    if (wants("game_get_key_name_for_action")) registerOne(L, state, log, stateTag, "game_get_key_name_for_action", stub_game_get_key_name_for_action);
    if (wants("game_peg_load_with_cb")) registerOne(L, state, log, stateTag, "game_peg_load_with_cb", stub_game_peg_load_with_cb);
    if (wants("ai_add_enemy_target")) registerOne(L, state, log, stateTag, "ai_add_enemy_target", stub_ai_add_enemy_target);
    if (wants("on_take_damage")) registerOne(L, state, log, stateTag, "on_take_damage", stub_on_take_damage);
    if (wants("set_ignore_ai_flag")) registerOne(L, state, log, stateTag, "set_ignore_ai_flag", stub_set_ignore_ai_flag);
    if (wants("get_max_hit_points")) registerOne(L, state, log, stateTag, "get_max_hit_points", stub_get_max_hit_points);
    if (wants("set_current_hit_points")) registerOne(L, state, log, stateTag, "set_current_hit_points", stub_set_current_hit_points);
    if (wants("ai_clear_scripted_action")) registerOne(L, state, log, stateTag, "ai_clear_scripted_action", stub_ai_clear_scripted_action);
    if (wants("vint_object_find")) registerOne(L, state, log, stateTag, "vint_object_find", stub_vint_object_find);
    if (wants("store_vehicle_get_state")) registerOne(L, state, log, stateTag, "store_vehicle_get_state", stub_store_vehicle_get_state);
    if (wants("Completion_is_client")) registerOne(L, state, log, stateTag, "Completion_is_client", stub_Completion_is_client);
    if (wants("game_hud_update_inventory")) registerOne(L, state, log, stateTag, "game_hud_update_inventory", stub_game_hud_update_inventory);
    if (wants("tutorial_advance")) registerOne(L, state, log, stateTag, "tutorial_advance", stub_tutorial_advance);
    if (wants("minimap_icon_add_do")) registerOne(L, state, log, stateTag, "minimap_icon_add_do", stub_minimap_icon_add_do);
    if (wants("object_indicator_add_do")) registerOne(L, state, log, stateTag, "object_indicator_add_do", stub_object_indicator_add_do);
    if (wants("on_qte_animation_trigger")) registerOne(L, state, log, stateTag, "on_qte_animation_trigger", stub_on_qte_animation_trigger);
    if (wants("on_revived")) registerOne(L, state, log, stateTag, "on_revived", stub_on_revived);
    if (wants("game_get_coop_join_type")) registerOne(L, state, log, stateTag, "game_get_coop_join_type", stub_game_get_coop_join_type);
    if (wants("zscene_is_loaded")) registerOne(L, state, log, stateTag, "zscene_is_loaded", stub_zscene_is_loaded);
}

} // namespace sr3luahost
