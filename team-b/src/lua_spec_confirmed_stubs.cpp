#include "sr3luahost/spec_confirmed_stubs.h"

#include "sr3luahost/bare_globals.h"

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

// lua_tonumber + 0x00ea2596, the engine's double->int64 conversion.
// SETTLED (spec-lua-api-behaviour.md Sec4.1, 2026-10-01, job
// 20261001T020218-team-a-bgcx, CONFIRMED - disassembly): truncation toward
// zero whatever the FPU rounding mode (the MSVC run-time's ordinary C
// integer cast): 2.9 -> 2, -2.9 -> -2, 0.5 -> 0. It replaces this project's
// earlier stand-in (round-half-to-even via std::nearbyint), chosen while
// the mode was OPEN.
//
// A NaN, an infinity or a value outside int64 range has no specified result
// (Sec4.1 does not settle it, and the C++ cast would be undefined behaviour -
// found by the refusal stress test under Clang UBSan, 2026-10-01), so it is
// refused as OPEN.
int64_t truncateEa2596(lua_Number v) {
    double r = std::trunc(v);
    if (!(r >= -9223372036854775808.0 && r < 9223372036854775808.0)) {
        throw OpenStateError("0x00ea2596 conversion of a non-finite or out-of-range number",
                             "spec-lua-api-behaviour.md Sec4.1");
    }
    return static_cast<int64_t>(r);
}

// Former name, kept only so concurrent edits of the call sites below merge
// cleanly; the conversion is no longer OPEN (Sec4.1). Prefer truncateEa2596.
int64_t roundToIntOpenMode(lua_Number v) { return truncateEa2596(v); }

// Small helper matching this batch's own recurring "mandatory string,
// read via lua_tolstring, NULL-safe" shape.
std::string argString(lua_State* L, int idx) {
    const char* s = lua_tolstring(L, idx, nullptr);
    return s ? s : "";
}

// ---------------------------------------------------------------------
// 1. coop_is_active (spec-lua-api-behaviour.md Sec3.1, Sec26.28)
// Arguments: none (the shared prologue calls lua_gettop but never uses
// it - CONFIRMED). Return: 1 boolean, pure query: the core predicate
// 0x00867830 (EngineState::coopIsActive, CONFIRMED four conditions). With
// no session - the CONFIRMED start-up state (Sec26.28) - false.
// ---------------------------------------------------------------------
int stub_coop_is_active(lua_State* L) {
    logCall(L, upLog(L), "coop_is_active", upStateTag(L));
    lua_pushboolean(L, upState(L)->coopIsActive() ? 1 : 0); // per-session fields OPEN until set
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
    // Truncation of a NaN or an out-of-int32-range number has no specified
    // result (and the cast would be undefined behaviour): refused as OPEN.
    if (!(n > -2147483649.0 && n < 2147483648.0)) {
        throw OpenStateError("key code truncation of a non-finite or out-of-range number",
                             "spec-lua-api-behaviour.md Sec2.3");
    }
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
    // No OS key-name source off Windows: refused as OPEN rather than an
    // invented "" (the portable Linux/CI build only).
    throw OpenStateError("GetKeyNameTextW result for key code " + std::to_string(code), "spec-lua-api-behaviour.md Sec2.3");
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
            int64_t rounded = truncateEa2596(raw); // the lua_tonumber + 0x00ea2596 pair: truncation toward zero (Sec4.1)
            result = static_cast<double>(static_cast<uint32_t>(rounded) & 0xFFFFu); // masked to 16 bits, CONFIRMED
        } else if (t == LUA_TSTRING) {
            std::string s = argString(L, 1);
            if (s.empty() || caseInsensitiveEquals(s, "none")) {
                result = 0.0; // CONFIRMED sentinel
            } else {
                // The Wwise string->id resolution (0x00462960) is not modelled:
                // refused as OPEN rather than returning an invented 0.
                throw OpenStateError("Wwise id for '" + s + "' (0x00462960)", "spec-lua-api-behaviour.md Sec8.22/Sec2.2");
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
    // Every other name goes through the CBA/CAA tables and the live key
    // bindings (Sec8.23); neither is modelled here, so the result is OPEN
    // rather than an invented "".
    throw OpenStateError("key binding for action '" + name + "' (0x005be440 CBA/CAA lookup)",
                         "spec-lua-api-behaviour.md Sec8.23");
}

// ---------------------------------------------------------------------
// 6. game_peg_load_with_cb (Sec8.24)
// Arguments: up to 8 (request name, count N, up to N filenames).
// CONFIRMED: arg1 bounds-checked to 64 bytes; arg2 (count) only used
// when in range 1..6; filenames read while present, stopping early at
// the first absent slot; return 0 always. Each filename's real,
// CONFIRMED, empirically-vector-validated hash bucket (bucket_count=9000,
// engine_state.h's own multiply33XorHashBucket) is computed and stored -
// (OPEN for a name whose hash is >= 2^31 or that has a byte >= 0x80:
// Sec8.2 leaves modulo and char signedness OPEN, see engine_state.h) -
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
    int64_t count = truncateEa2596(nRaw);
    if (count >= 1 && count <= 6) {
        for (int64_t i = 0; i < count; ++i) {
            int argIndex = 3 + static_cast<int>(i);
            if (lua_gettop(L) < argIndex || lua_type(L, argIndex) != LUA_TSTRING) break; // "breaking out early on any absent slot"
            PegLoadFilenameEntry entry;
            entry.name = argString(L, argIndex);
            // Stored only where Sec8.2's OPEN readings agree; OPEN otherwise.
            if (EngineState::multiply33XorHashBucketUnambiguous(entry.name, 9000)) {
                entry.bucketIndex.set(EngineState::multiply33XorHashBucket(entry.name, 9000));
            }
            req.filenames.push_back(std::move(entry));
        }
    }
    upState(L)->pegLoadRequests().push_back(std::move(req));
    return 0;
}

// ---------------------------------------------------------------------
// 7. ai_add_enemy_target (Sec3.9)
// Arguments: 4 (acting character, target name or "#CLOSEST_PLAYER#",
// priority/id number [converted by 0x00ea2596: truncation toward zero, Sec4.1 settled 2026-10-01], optional bool default false). Return: 1 boolean
// (resolve-and-add success/failure - always true in this minimal
// registry, see engine_state.h's own EnemyTargetRecord doc comment).
// ---------------------------------------------------------------------
int stub_ai_add_enemy_target(lua_State* L) {
    logCall(L, upLog(L), "ai_add_enemy_target", upStateTag(L));
    std::string actor = argString(L, 1);
    std::string target = argString(L, 2);
    double priorityRaw = lua_tonumber(L, 3);
    int64_t priority = truncateEa2596(priorityRaw); // 0x00ea2596: truncation toward zero (Sec4.1)
    bool arg4 = false;
    if (lua_gettop(L) >= 4 && lua_type(L, 4) != LUA_TNIL) arg4 = lua_toboolean(L, 4) != 0;
    bool sentinelMatch = caseInsensitiveEquals(target, "#CLOSEST_PLAYER#");

    EnemyTargetRecord rec;
    rec.priorityOrId = priority;
    rec.arg4Flag = arg4;
    rec.sentinelMatchFlag = sentinelMatch;
    std::string key = (priority != 0) ? std::to_string(priority) : target;

    // CONFIRMED: "on successful resolution of both names" the record is
    // added and the boolean result is that success. Resolution is OPEN
    // state here (no object registry); the earlier always-true is gone.
    bool resolved = upState(L)->objectResolves().get(actor) && upState(L)->objectResolves().get(target);
    if (resolved) {
        CharacterState& actorState = upState(L)->getOrCreateCharacter(actor);
        actorState.enemyTargets[key] = rec;
    }
    lua_pushboolean(L, resolved ? 1 : 0);
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
    // Every value the body branches on is read BEFORE anything is written,
    // so an OPEN one (open_state.h) refuses the call without a partial update.
    bool wasIgnoring = character.ignoreAI.get();
    bool enabling = newValue && !wasIgnoring;
    int override = 1; // 1 = leave, 0x19 or 0 = write that value
    if (enabling) {
        // HIGH CONFIDENCE only for the real-world meaning of id 0x19 and
        // for this exact gating condition (spec's own words) - the
        // STRUCTURE (state-enum-gated conditional override write) itself
        // is CONFIRMED.
        if (character.stateEnum.get() == 3) override = 0x19;
        else if (character.attackerThreatRef.get() != 0) override = 0;
    }
    character.ignoreAI.set(newValue);
    EngineState::replicateStateChange("set_ignore_ai_flag", name);
    if (enabling && override != 1) character.actionOverrideId.set(override);
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
    lua_pushnumber(L, static_cast<lua_Number>(character.maxHitPoints.get())); // OPEN until set
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
    int32_t cap = character.maxHitPoints.get(); // CONFIRMED cross-check: same field get_max_hit_points reads; OPEN until set
    int64_t rounded = truncateEa2596(raw); // 0x00ea2596: truncation toward zero (Sec4.1)
    int64_t clampedWide = std::max<int64_t>(0, std::min<int64_t>(rounded, cap));
    int32_t clamped = static_cast<int32_t>(clampedWide);

    character.currentHitPoints.set(clamped);
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
// global flag 0x022cdf08. Pure query. The flag is 0 at load (CONFIRMED,
// Sec26.28), so this returns 0.0 at start-up; its writers are not
// implemented (see EngineState::vehicleStoreActive).
// ---------------------------------------------------------------------
int stub_store_vehicle_get_state(lua_State* L) {
    logCall(L, upLog(L), "store_vehicle_get_state", upStateTag(L));
    lua_pushnumber(L, upState(L)->vehicleStoreActive().get() ? 1.0 : 0.0); // OPEN until set
    return 1;
}

// ---------------------------------------------------------------------
// 15. Completion_is_client (Sec10.2)
// Arguments: none (same unused-lua_gettop shape). Return: 1 boolean
// (CONFIRMED, lua_pushboolean). Real formula (CONFIRMED, cross-checked
// against game_get_is_host's/Sec8.27's own already-decompiled body,
// which is OUT of this task's scope but already exists as a documented
// citation in the spec): "is there an active co-op session AND is this
// machine NOT the host" - EngineState::coopLocalIsClient(); returns false
// (not the literal complement of game_get_is_host) when no session
// exists at all, which is the CONFIRMED start-up state (Sec26.28).
// ---------------------------------------------------------------------
int stub_Completion_is_client(lua_State* L) {
    logCall(L, upLog(L), "Completion_is_client", upStateTag(L));
    // Batch 2026-10-01 (Sec10.2 review status): "active" means "a session
    // object exists", not coop_is_active. No session (start-up) -> false.
    lua_pushboolean(L, upState(L)->coopLocalIsClient() ? 1 : 0);
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
    if (upState(L)->hasLocalPlayer().get()) { // OPEN until set
        upState(L)->recordHudInventoryRefresh();
    }
    return 0;
}

// ---------------------------------------------------------------------
// 17. tutorial_advance (Sec10.4, Sec6.19, Sec26.28; batch 2026-10-01)
// Arguments: 1 mandatory string, read unconditionally with no nil/
// absence gate (CONFIRMED) - a tutorial/hint id (a non-string reads as ""
// here). Return: 1 boolean (CONFIRMED). Body (CONFIRMED): the resolver
// 0x00717780 (EngineState::tutorialLookup over the 210-name table), then
// 0x00716440: index in range and the entry's STATE (+0x0c) == 3, else
// false with no other effect; the state is not changed. On success a named
// UI message is built and dispatched (HYPOTHESIS target; counted only) and
// the result is true. Index 176's name is OPEN: a call whose answer would
// differ if the name were entry 176 is refused.
// ---------------------------------------------------------------------
int stub_tutorial_advance(lua_State* L) {
    logCall(L, upLog(L), "tutorial_advance", upStateTag(L));
    EngineState* es = upState(L);
    const std::string id = argString(L, 1);
    const EngineState::TutorialLookup lk = EngineState::tutorialLookup(id);
    auto inState3 = [es](int index) {
        return es->tutorialState().get(EngineState::tutorialStateKey(index)) == 3;
    };
    const bool result = lk.index >= 0 && inState3(lk.index);
    if (lk.couldBeIndex176 && inState3(176) != result) {
        throw OpenStateError("tutorial name of entry 176 (0x012f5930[176], string OPEN) vs '" + id + "'",
                             "spec-lua-api-behaviour.md Sec26.28");
    }
    if (result) es->recordTutorialAdvance(lk.index);
    lua_pushboolean(L, result ? 1 : 0);
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
        rec.flag5 = truncateEa2596(lua_tonumber(L, 5));
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
    rec.arg2 = truncateEa2596(lua_tonumber(L, 2)); // CONFIRMED: no nil/absence gate - real API: absent -> 0.0 via lua_tonumber's own real nil-handling
    rec.arg3 = truncateEa2596(lua_tonumber(L, 3)); // same, CONFIRMED no nil gate
    if (lua_gettop(L) >= 4 && lua_type(L, 4) != LUA_TNIL) rec.arg4 = truncateEa2596(lua_tonumber(L, 4));
    if (lua_gettop(L) >= 5 && lua_type(L, 5) != LUA_TNIL) rec.arg5 = lua_tonumber(L, 5);
    // Result = whether arg 1 resolves (Sec10.6); OPEN state here, so the
    // earlier always-true is gone.
    bool resolved = upState(L)->objectResolves().get(objectName);
    if (resolved) {
        CharacterState& obj = upState(L)->getOrCreateCharacter(objectName);
        obj.objectIndicators.push_back(std::move(rec));
    }
    lua_pushboolean(L, resolved ? 1 : 0);
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
// layer (explicit, standing project convention, same as coopSession_/
// replicateStateChange) - EngineState::coopJoinType() is this project's
// own minimal int stand-in, default 0, test-only setter (no real setter
// is in scope - the real writer is a different, out-of-scope function).
// ---------------------------------------------------------------------
int stub_game_get_coop_join_type(lua_State* L) {
    logCall(L, upLog(L), "game_get_coop_join_type", upStateTag(L));
    lua_pushnumber(L, static_cast<lua_Number>(upState(L)->coopJoinType().get())); // OPEN until set
    return 1;
}

// ---------------------------------------------------------------------
// Reading OPEN engine state (open_state.h) from a stub: the refusal becomes
// a Lua error naming the global and its spec section. An *Eval helper
// catches the OpenStateError, logs "<name>:OPEN_STATE" in the HitLog (so a
// run shows which OPEN value blocked which name) and rethrows it as a
// PendingLuaError carrying "<name>: <message>"; openGuard raises it.
// ---------------------------------------------------------------------
//
// NO LUA ERROR MAY CROSS A C++ FRAME (fix 2026-09-30, bridge job
// 20260930T225845-team-b-jklk: 0xC0000374 heap corruption on MSVC). A Lua
// error is a longjmp; on MSVC x64 longjmp unwinds C++ frames through their
// EH tables, which /EHsc builds assuming extern "C" calls (lua_error,
// lua_gettable) never throw, so a frame holding a std::string could be
// destroyed twice. So no stub calls lua_error or an erroring Lua API
// itself: a refusal is thrown as a C++ exception (OpenStateError, or
// PendingLuaError with a ready message), and only openGuard's outer frame,
// which holds no C++ object and no try block, calls lua_error.
struct PendingLuaError {
    std::string message;
};

// trunc(seconds x 1000.0) to the helpers' integer millisecond argument
// (Sec2.9/Sec8.13: the double at 0x012a2d90). A NaN, an infinity or a value
// outside int32 has no specified result: refused as OPEN.
int32_t fadeDurationMs(lua_Number seconds) {
    const double ms = seconds * 1000.0;
    if (!(ms > -2147483649.0 && ms < 2147483648.0)) {
        throw OpenStateError("trunc(seconds x 1000.0) of a non-finite or out-of-range fade duration",
                             "spec-lua-api-behaviour.md Sec2.9/Sec26.24");
    }
    return static_cast<int32_t>(ms);
}

[[noreturn]] void throwOpenState(lua_State* L, const char* fn, const OpenStateError& e) {
    std::string tag = std::string(fn) + ":OPEN_STATE";
    logCall(L, upLog(L), tag.c_str(), upStateTag(L));
    throw PendingLuaError{std::string(fn) + ": " + e.what()};
}

// ---------------------------------------------------------------------
// 23. zscene_is_loaded (spec-lua-api-behaviour.md Sec14.23, Sec26.25;
// batch 2026-10-01)
// Arguments: 1 optional name, standard nil-gated idiom (CONFIRMED); a
// string or a number counts as a name, anything else skips the fast path
// like no name. Return: 1 boolean (CONFIRMED). Pure query over the
// corrected truth table (EngineState::zsceneIsLoaded): no name -> skip byte
// or state == 2; a name -> true unless it is a kind-1 table entry, then
// skip byte, then false unless it is the current entry, then state == 2.
// No sense inversion. OPEN reads refuse; so does a named entry that is
// pending (its promotion is OPEN).
// ---------------------------------------------------------------------
int zsceneIsLoadedEval(lua_State* L) {
    logCall(L, upLog(L), "zscene_is_loaded", upStateTag(L));
    EngineState* es = upState(L);
    const int t = lua_gettop(L) >= 1 ? lua_type(L, 1) : LUA_TNONE;
    const bool hasName = t == LUA_TSTRING || t == LUA_TNUMBER;
    std::string name = hasName ? argString(L, 1) : std::string();
    try {
        return es->zsceneIsLoaded(hasName, name) ? 1 : 0;
    } catch (const OpenStateError& e) {
        throwOpenState(L, "zscene_is_loaded", e);
    }
}

int stub_zscene_is_loaded(lua_State* L) {
    int r = zsceneIsLoadedEval(L);
    lua_pushboolean(L, r);
    return 1;
}

// ---------------------------------------------------------------------
// zscene_prep (spec-lua-api-behaviour.md Sec8.21, Sec26.25; batch
// 2026-10-01). Arguments: 1 mandatory string, read unconditionally (a
// non-string reads as "" here). Return: none. Body (CONFIRMED,
// EngineState::zscenePrep): the gate 0x007232e0, the teardown
// 0x00721c20(1, 0, 0) of the current scene, the entry made pending. The
// load itself is started elsewhere (Sec26.25), not here.
// ---------------------------------------------------------------------
int stub_zscene_prep(lua_State* L) {
    logCall(L, upLog(L), "zscene_prep", upStateTag(L));
    std::string name = argString(L, 1);
    try {
        upState(L)->zscenePrep(name);
    } catch (const OpenStateError& e) {
        throwOpenState(L, "zscene_prep", e);
    }
    return 0;
}

// ---------------------------------------------------------------------
// 24. set_mission_author (Sec6.1): CONFIRMED fully inert - the prologue's
// lua_gettop result is never used, nothing is read, pushed or touched.
// ---------------------------------------------------------------------
int stub_set_mission_author(lua_State* L) {
    logCall(L, upLog(L), "set_mission_author", upStateTag(L));
    return 0;
}

// ---------------------------------------------------------------------
// 25. fade_out (Sec2.9, Sec26.24)
// Arg 1: number, read unconditionally via lua_tonumber (absent -> 0).
// Arg 2: optional; if present and non-nil it is indexed as t[1], t[2],
// t[3] with lua_gettable (so a non-table value raises the ordinary Lua
// "attempt to index" error, exactly as the real API call would); each
// nil/absent component -> 0.0; whole table absent/nil -> all 0.0.
// Arg 3: optional flags via lua_tonumber, default 3. (The number->int
// conversion before the bit tests is not described; roundToIntOpenMode.)
// Colour setter with alpha 255, each scaled by 1/255 - unconditional.
// Bit 0x1: the fade-out request helper 0x0059f8c0(trunc(d x 1000), 0, 0)
// (EngineState::screenFadeRequest). Bit 0x2: the broadcast helper
// 0x005a0270 (host-gated opcode-0x53 record). Return: none. All CONFIRMED.
// ---------------------------------------------------------------------
int fadeOutReadColour(lua_State* L) {
    for (int k = 1; k <= 3; ++k) {
        lua_pushnumber(L, k);
        lua_gettable(L, 1);
    }
    return 3;
}

int stub_fade_out(lua_State* L) {
    logCall(L, upLog(L), "fade_out", upStateTag(L));
    EngineState* es = upState(L);
    lua_Number duration = lua_tonumber(L, 1);
    float rgb[3] = {0.0f, 0.0f, 0.0f};
    if (lua_gettop(L) >= 2 && lua_type(L, 2) != LUA_TNIL) {
        lua_pushcfunction(L, fadeOutReadColour);
        lua_pushvalue(L, 2);
        if (lua_pcall(L, 1, 3, 0) != 0) {
            const char* m = lua_tostring(L, -1);
            std::string message = m ? m : "fade_out: error indexing argument 2";
            lua_pop(L, 1);
            throw PendingLuaError{message};
        }
        for (int k = 0; k < 3; ++k) {
            int idx = -3 + k;
            if (lua_type(L, idx) != LUA_TNIL) rgb[k] = static_cast<float>(lua_tonumber(L, idx));
        }
        lua_pop(L, 3);
    }
    int64_t flags = 3;
    if (lua_gettop(L) >= 3 && lua_type(L, 3) != LUA_TNIL) flags = roundToIntOpenMode(lua_tonumber(L, 3));
    // Read before anything is written: the duration conversion and the
    // broadcast's host gate (Sec26.24: session present and +0x5c == +0x58).
    const int32_t durationMs = (flags & 0x1) ? fadeDurationMs(duration) : 0;
    const bool broadcast = (flags & 0x2) && es->coopLocalIsHost();

    EngineState::ScreenFadeColour c;
    c.r = rgb[0] / 255.0f;
    c.g = rgb[1] / 255.0f;
    c.b = rgb[2] / 255.0f;
    c.a = 255.0f / 255.0f;
    es->setScreenFadeColour(c);
    if (flags & 0x1) {
        es->screenFadeRequest(true, durationMs, nullptr, 0);
        es->recordScreenFadeRequest({static_cast<double>(durationMs), 1.0f});
    }
    if (broadcast) es->screenFadeBroadcast(true);
    return 0;
}

// ---------------------------------------------------------------------
// fade_in (Sec8.13, Sec26.24; batch 2026-10-01)
// Arg 1: number, mandatory, read unconditionally (lua_tonumber). Arg 2:
// optional flags, standard nil-gated, default 3 (number->int conversion
// as fade_out's). Bit 0x1: the fade-in request helper
// 0x0059fc40(trunc(d x 1000), 0, 0); bit 0x2: the broadcast helper
// 0x005a0400. No colour. Return: none. All CONFIRMED.
// ---------------------------------------------------------------------
int stub_fade_in(lua_State* L) {
    logCall(L, upLog(L), "fade_in", upStateTag(L));
    EngineState* es = upState(L);
    lua_Number duration = lua_tonumber(L, 1);
    int64_t flags = 3;
    if (lua_gettop(L) >= 2 && lua_type(L, 2) != LUA_TNIL) flags = roundToIntOpenMode(lua_tonumber(L, 2));
    const int32_t durationMs = (flags & 0x1) ? fadeDurationMs(duration) : 0;
    const bool broadcast = (flags & 0x2) && es->coopLocalIsHost();
    if (flags & 0x1) {
        es->screenFadeRequest(false, durationMs, nullptr, 0);
        es->recordScreenFadeRequest({static_cast<double>(durationMs), 0.0f});
    }
    if (broadcast) es->screenFadeBroadcast(false);
    return 0;
}

// ---------------------------------------------------------------------
// The fade-state queries (Sec26.9, Sec26.24, CONFIRMED): no arguments, one
// boolean - state 0x012e6aa4 == 3 (fade_is_fully_faded_out, sfx_faded_out)
// or == 2 (fade_is_fully_faded_in, sfx_faded_in). The start-up state is 2.
// ---------------------------------------------------------------------
template <uint32_t Wanted>
int fadeStateIsEval(lua_State* L, const char* fn) {
    logCall(L, upLog(L), fn, upStateTag(L));
    try {
        return upState(L)->screenFade().state.get() == Wanted ? 1 : 0;
    } catch (const OpenStateError& e) {
        throwOpenState(L, fn, e);
    }
}

int stub_fade_is_fully_faded_out(lua_State* L) {
    lua_pushboolean(L, fadeStateIsEval<3>(L, "fade_is_fully_faded_out"));
    return 1;
}

int stub_fade_is_fully_faded_in(lua_State* L) {
    lua_pushboolean(L, fadeStateIsEval<2>(L, "fade_is_fully_faded_in"));
    return 1;
}

int stub_sfx_faded_in(lua_State* L) {
    lua_pushboolean(L, fadeStateIsEval<2>(L, "sfx_faded_in"));
    return 1;
}

// ---------------------------------------------------------------------
// Screen_fade_transition_complete (0x005a0110, Sec26.24, CONFIRMED): the
// UI-state native the screen_fade script calls when its transition ends.
// No arguments, nothing returned. Runs the completion body (flip 0 -> 2 /
// 1 -> 3, in-flight callback, deferred slot); counted as the real path.
// ---------------------------------------------------------------------
int stub_Screen_fade_transition_complete(lua_State* L) {
    logCall(L, upLog(L), "Screen_fade_transition_complete", upStateTag(L));
    upState(L)->screenFadeCompletionNative();
    return 0;
}

// ---------------------------------------------------------------------
// game_get_is_host (0x008440a0, Sec8.27, Sec26.28; batch 2026-10-01): no
// arguments used; 1 boolean: a session exists and +0x5c == +0x58. No
// session (the CONFIRMED start-up state) -> false. With a session the host
// pair is OPEN state here (nothing installs one).
// ---------------------------------------------------------------------
int stub_game_get_is_host(lua_State* L) {
    logCall(L, upLog(L), "game_get_is_host", upStateTag(L));
    lua_pushboolean(L, upState(L)->coopLocalIsHost() ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// vint_is_std_res (0x00e1a150, Sec26.26; batch 2026-10-01): lua_gettop
// called and ignored; 1 boolean (CONFIRMED). Rule (CONFIRMED):
// first / second < 1.5, or display mode 0x0132bd80 == 2
// (EngineState::vintIsStdRes). Its inputs are OPEN state here.
// ---------------------------------------------------------------------
int stub_vint_is_std_res(lua_State* L) {
    logCall(L, upLog(L), "vint_is_std_res", upStateTag(L));
    lua_pushboolean(L, upState(L)->vintIsStdRes() ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// vint_get_safe_frame (0x00e1b570, Sec26.26; batch 2026-10-01): no
// arguments; 4 numbers (CONFIRMED shape): the integers a = +0x8 and
// b = +0xc of (thread context +0x674)+0x14 times two double constants,
// rounded, in the order (c1*a, c1*b, c2*a, c2*b) (HIGH CONFIDENCE). The
// constants and a, b are OPEN state here; the rounding mode is not stated,
// so a non-integral product is refused too.
// ---------------------------------------------------------------------
int stub_vint_get_safe_frame(lua_State* L) {
    logCall(L, upLog(L), "vint_get_safe_frame", upStateTag(L));
    EngineState* es = upState(L);
    const double a = es->vintSafeFrameA().get();
    const double b = es->vintSafeFrameB().get();
    const double c1 = es->vintSafeFrameScale1().get();
    const double c2 = es->vintSafeFrameScale2().get();
    const double products[4] = {c1 * a, c1 * b, c2 * a, c2 * b}; // HIGH CONFIDENCE order
    for (double v : products) {
        if (!(v == std::floor(v)) || !(std::fabs(v) < 2147483648.0)) {
            throw OpenStateError("rounding of a non-integral or out-of-range safe-frame product (mode not stated)",
                                 "spec-lua-api-behaviour.md Sec26.26");
        }
    }
    for (double v : products) lua_pushnumber(L, v);
    return 4;
}

// ---------------------------------------------------------------------
// 26. mission_end_silently (Sec15.23) - PARTIAL, stated.
// Arg 1: optional boolean, standard nil-gated, default false. Return: none.
// Implemented (CONFIRMED, unconditional): 0x006da7b0 sets bit 0x4 of the
// global mission-flags word and sets/clears bit 0x10 to mirror the arg.
// NOT modelled: everything after it is gated on state this host lacks -
// the "can end mission now" gate (0x006cf930), the active mission's
// restartable flag and cleanup, and the non-host-client network request vs
// local finalize (0x006d9f70). Nothing is faked for those.
// ---------------------------------------------------------------------
int stub_mission_end_silently(lua_State* L) {
    logCall(L, upLog(L), "mission_end_silently", upStateTag(L));
    bool arg = false;
    if (lua_gettop(L) >= 1 && lua_type(L, 1) != LUA_TNIL) arg = lua_toboolean(L, 1) != 0;
    upState(L)->missionFlagsWord().setBits(0x4u | 0x10u, 0x4u | (arg ? 0x10u : 0u));
    return 0;
}

// ---------------------------------------------------------------------
// 27. sfx_faded_out (Sec26.9, Sec26.24): no arguments; 1 boolean, true iff
// the fade-state global 0x012e6aa4 equals 3 (CONFIRMED, 2-instruction
// body). The state starts at 2 (CONFIRMED) and is driven by the fade
// request helpers and Screen_fade_transition_complete.
// ---------------------------------------------------------------------
int stub_sfx_faded_out(lua_State* L) {
    lua_pushboolean(L, fadeStateIsEval<3>(L, "sfx_faded_out"));
    return 1;
}

// Every spec stub is registered through this trampoline. A stub reading
// OPEN engine state (open_state.h) throws OpenStateError; the trampoline
// catches it, logs "OPEN_STATE:<global>" in the HitLog, pushes the message
// and raises it as a Lua error only after every C++ object in this frame
// is gone (Lua errors longjmp). The stub's own frames were already unwound
// by the C++ throw.
#if defined(_MSC_VER)
#define CRREISH_NOINLINE __declspec(noinline)
#else
#define CRREISH_NOINLINE __attribute__((noinline))
#endif

// Calls the stub. On a refusal, leaves the error message on the Lua stack
// and returns -1. Never inlined into openGuard, so the frame that calls
// lua_error below has no try block and no C++ object for an MSVC longjmp
// to unwind.
template <lua_CFunction Fn>
CRREISH_NOINLINE int openGuardCall(lua_State* L) {
    try {
        return Fn(L);
    } catch (const OpenStateError& e) {
        std::string tag = std::string("OPEN_STATE:") + e.global();
        logCall(L, upLog(L), tag.c_str(), upStateTag(L));
        lua_pushstring(L, e.what());
    } catch (const PendingLuaError& e) {
        lua_pushstring(L, e.message.c_str());
    }
    return -1;
}

template <lua_CFunction Fn>
int openGuard(lua_State* L) {
    int pushed = openGuardCall<Fn>(L);
    if (pushed < 0) return lua_error(L);
    return pushed;
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

const std::vector<SpecBareGlobal>& specBareGlobals() {
    // The 24 bare globals of 0x00e0f900, in roster order, into BOTH states
    // (spec-lua-bindings.md Sec13.2/Sec16.4 "Which state", CONFIRMED -
    // disassembly, job 20261001T020218-team-a-bgcx). Bodies:
    // lua_bare_globals.cpp (spec-lua-api-behaviour.md Sec26.27).
    static const std::vector<SpecBareGlobal> rows = [] {
        std::vector<SpecBareGlobal> out;
        for (const auto& e : bareGlobalRoster())
            out.push_back({e.name, true, true, "spec-lua-bindings.md Sec13.2/Sec16.4; spec-lua-api-behaviour.md Sec26.27"});
        return out;
    }();
    return rows;
}

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
        // Cloud phase batch 1 (2026-09-30), all `gameplay` in the tagged
        // list (lines 295/505/776).
        "set_mission_author",
        "fade_out",
        "mission_end_silently",
        // Item-3 scaffolding (2026-09-30): Sec26.9, `ui` (tagged list line 1251).
        "sfx_faded_out",
        // Batch 2026-10-01 (fade Sec26.24, zscene Sec26.25, UI resolution
        // Sec26.26, co-op Sec26.28). Tags in tools/lua_all_registered_1490_
        // tagged.txt: fade_in / fade_is_fully_faded_out / fade_is_fully_faded_in /
        // zscene_prep `gameplay`; sfx_faded_in / Screen_fade_transition_complete /
        // game_get_is_host / vint_is_std_res / vint_get_safe_frame `ui`.
        "fade_in",
        "fade_is_fully_faded_out",
        "fade_is_fully_faded_in",
        "sfx_faded_in",
        "Screen_fade_transition_complete",
        "zscene_prep",
        "game_get_is_host",
        "vint_is_std_res",
        "vint_get_safe_frame",
    };
    // The 24 bare globals (specBareGlobals(), lua_bare_globals.cpp) are spec
    // functions too, so tools and tests that walk this list (the refusal
    // stress test) cover them; Host registers them through
    // registerBareGlobals(), never as generic stubs, and
    // registerSpecConfirmedStubs() leaves them alone.
    static const std::vector<std::string> all = [] {
        std::vector<std::string> out = names;
        for (const auto& e : bareGlobalRoster()) out.push_back(e.name);
        return out;
    }();
    return all;
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
    if (wants("coop_is_active")) registerOne(L, state, log, stateTag, "coop_is_active", openGuard<stub_coop_is_active>);
    if (wants("game_get_key_name")) registerOne(L, state, log, stateTag, "game_get_key_name", openGuard<stub_game_get_key_name>);
    if (wants("game_UI_audio_play")) registerOne(L, state, log, stateTag, "game_UI_audio_play", openGuard<stub_game_UI_audio_play>);
    if (wants("game_audio_get_audio_id")) registerOne(L, state, log, stateTag, "game_audio_get_audio_id", openGuard<stub_game_audio_get_audio_id>);
    if (wants("game_get_key_name_for_action")) registerOne(L, state, log, stateTag, "game_get_key_name_for_action", openGuard<stub_game_get_key_name_for_action>);
    if (wants("game_peg_load_with_cb")) registerOne(L, state, log, stateTag, "game_peg_load_with_cb", openGuard<stub_game_peg_load_with_cb>);
    if (wants("ai_add_enemy_target")) registerOne(L, state, log, stateTag, "ai_add_enemy_target", openGuard<stub_ai_add_enemy_target>);
    if (wants("on_take_damage")) registerOne(L, state, log, stateTag, "on_take_damage", openGuard<stub_on_take_damage>);
    if (wants("set_ignore_ai_flag")) registerOne(L, state, log, stateTag, "set_ignore_ai_flag", openGuard<stub_set_ignore_ai_flag>);
    if (wants("get_max_hit_points")) registerOne(L, state, log, stateTag, "get_max_hit_points", openGuard<stub_get_max_hit_points>);
    if (wants("set_current_hit_points")) registerOne(L, state, log, stateTag, "set_current_hit_points", openGuard<stub_set_current_hit_points>);
    if (wants("ai_clear_scripted_action")) registerOne(L, state, log, stateTag, "ai_clear_scripted_action", openGuard<stub_ai_clear_scripted_action>);
    if (wants("vint_object_find")) registerOne(L, state, log, stateTag, "vint_object_find", openGuard<stub_vint_object_find>);
    if (wants("store_vehicle_get_state")) registerOne(L, state, log, stateTag, "store_vehicle_get_state", openGuard<stub_store_vehicle_get_state>);
    if (wants("Completion_is_client")) registerOne(L, state, log, stateTag, "Completion_is_client", openGuard<stub_Completion_is_client>);
    if (wants("game_hud_update_inventory")) registerOne(L, state, log, stateTag, "game_hud_update_inventory", openGuard<stub_game_hud_update_inventory>);
    if (wants("tutorial_advance")) registerOne(L, state, log, stateTag, "tutorial_advance", openGuard<stub_tutorial_advance>);
    if (wants("minimap_icon_add_do")) registerOne(L, state, log, stateTag, "minimap_icon_add_do", openGuard<stub_minimap_icon_add_do>);
    if (wants("object_indicator_add_do")) registerOne(L, state, log, stateTag, "object_indicator_add_do", openGuard<stub_object_indicator_add_do>);
    if (wants("on_qte_animation_trigger")) registerOne(L, state, log, stateTag, "on_qte_animation_trigger", openGuard<stub_on_qte_animation_trigger>);
    if (wants("on_revived")) registerOne(L, state, log, stateTag, "on_revived", openGuard<stub_on_revived>);
    if (wants("game_get_coop_join_type")) registerOne(L, state, log, stateTag, "game_get_coop_join_type", openGuard<stub_game_get_coop_join_type>);
    if (wants("zscene_is_loaded")) registerOne(L, state, log, stateTag, "zscene_is_loaded", openGuard<stub_zscene_is_loaded>);
    if (wants("set_mission_author")) registerOne(L, state, log, stateTag, "set_mission_author", openGuard<stub_set_mission_author>);
    if (wants("fade_out")) registerOne(L, state, log, stateTag, "fade_out", openGuard<stub_fade_out>);
    if (wants("mission_end_silently")) registerOne(L, state, log, stateTag, "mission_end_silently", openGuard<stub_mission_end_silently>);
    if (wants("sfx_faded_out")) registerOne(L, state, log, stateTag, "sfx_faded_out", openGuard<stub_sfx_faded_out>);
    if (wants("fade_in")) registerOne(L, state, log, stateTag, "fade_in", openGuard<stub_fade_in>);
    if (wants("fade_is_fully_faded_out")) registerOne(L, state, log, stateTag, "fade_is_fully_faded_out", openGuard<stub_fade_is_fully_faded_out>);
    if (wants("fade_is_fully_faded_in")) registerOne(L, state, log, stateTag, "fade_is_fully_faded_in", openGuard<stub_fade_is_fully_faded_in>);
    if (wants("sfx_faded_in")) registerOne(L, state, log, stateTag, "sfx_faded_in", openGuard<stub_sfx_faded_in>);
    if (wants("Screen_fade_transition_complete")) {
        registerOne(L, state, log, stateTag, "Screen_fade_transition_complete",
                    openGuard<stub_Screen_fade_transition_complete>);
        // The state that gets the completion native is the UI state the
        // engine calls screen_fade_do in (Sec26.24). This host has no UI
        // document loader: that state stands in for the "screen_fade"
        // document, and the init 0x0059fa30 (CONFIRMED writes: document id
        // set, state 2, target 2, flag 1) is taken as run with the document
        // found, here, before any script - a host choice (when the engine
        // runs init is not in the spec). Whether screen_fade_do exists is
        // still checked at every request.
        state.attachScreenFadeUiState(L);
        state.screenFadeInit(true);
    }
    if (wants("zscene_prep")) registerOne(L, state, log, stateTag, "zscene_prep", openGuard<stub_zscene_prep>);
    if (wants("game_get_is_host")) registerOne(L, state, log, stateTag, "game_get_is_host", openGuard<stub_game_get_is_host>);
    if (wants("vint_is_std_res")) registerOne(L, state, log, stateTag, "vint_is_std_res", openGuard<stub_vint_is_std_res>);
    if (wants("vint_get_safe_frame")) registerOne(L, state, log, stateTag, "vint_get_safe_frame", openGuard<stub_vint_get_safe_frame>);
}

} // namespace sr3luahost
