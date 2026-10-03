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
//
// Re-checked 2026-10-02 against spec-tables-ui-controls.md Sec4.1/Sec4.2
// (cross-cited by Sec8.23 itself), independently confirmed CONFIRMED and
// not held (that document's own 2026-10-01 review status; nothing in
// Sec4.1/Sec4.2 is HYPOTHESIS or NEEDS-DATA/NEEDS-EXE). That table's own
// CBA/CAA *row dumps* are still "Team A working dump, not in this
// repository - only the entries quoted [t]here are published" though, so
// only the literal names actually quoted in spec text (not the full
// 166/34-row vocabulary) can be classified here without guessing.
//
// CONFIRMED literals now recognised, case-insensitively:
// - "CAA_CAMERA_ROTATE" (CAA axis index 0) -> "STR_THE_MOUSE".
// - The CBA sentinel "BUTTON_UNBOUND" (spec-tables-ui-controls.md Sec4.1:
//   the CBA table's own index-0 name, value -1) does NOT satisfy Sec8.23's
//   "CBA entry with a bound (non -1) value" condition, so it falls through
//   to the CAA check (no match there either) -> the CONFIRMED "no match at
//   all" default, "".
// - 6 further CAA representative entries spec-tables-ui-controls.md Sec4.2
//   names outright, none at axis index 0/2/3: CAA_CAMERA_ELEVATE(1),
//   CAA_DRIVE_STEER(9), CAA_TANK_DRIVE_FORWARD_BACKWARD(16),
//   CAA_VPC_TURRET_RIGHT(29), CAA_TURRET_CAMERA_ELEVATE(31),
//   CAA_PLANE_ROLL_LEFT_RIGHT(33) -> each hits Sec8.23's "any other axis
//   index" branch -> "".
//
// Still OPEN, unchanged by this re-check:
// - CAA_WALK_FORWARD_BACKWARD/CAA_WALK_TURN_LEFT_RIGHT (axis 2/3): the
//   two-key sub-action resolvers 0x005be2c0/0x005be340 that format their
//   result are themselves still OPEN per Sec8.23's own closing sentence
//   ("OPEN - ... 0x005be2c0/0x005be340's own bodies") - not resolved by
//   the ui-controls table sync, which only covers the CBA/CAA NAME tables,
//   not these two callees.
// - The primary CBA (button-bound-key) branch, and any action name not
//   individually quoted above: Sec8.23's own CONFIRMED text routes a CBA
//   match through the REAL PER-ACTION CURRENTLY-BOUND-KEY data (from
//   control_binding_sets.xtbl's live binding-set state), which this
//   project's minimal state layer does not load (no active binding set,
//   no runtime per-action key array) - this is genuinely unmodeled engine
//   state, not merely an unconfirmed spec text, so it stays OPEN. Since
//   the full 166-row CBA table (and the remaining 28 CAA rows) are not in
//   this repository either, an arbitrary unrecognised name cannot be
//   safely classified as "definitely not CBA" - returning "" for it would
//   risk inventing a "no binding" answer for what is very likely a real,
//   currently-bound action name. Refusing (OPEN) stays the correct,
//   conservative default for anything not explicitly named above.
// ---------------------------------------------------------------------
int stub_game_get_key_name_for_action(lua_State* L) {
    logCall(L, upLog(L), "game_get_key_name_for_action", upStateTag(L));
    std::string name = argString(L, 1);
    if (caseInsensitiveEquals(name, "CAA_CAMERA_ROTATE")) {
        lua_pushstring(L, "STR_THE_MOUSE");
        return 1;
    }
    static const char* kKnownEmptyResultNames[] = {
        "BUTTON_UNBOUND",                    // CBA sentinel, value -1 (spec-tables-ui-controls.md Sec4.1)
        "CAA_CAMERA_ELEVATE",                // CAA axis 1
        "CAA_DRIVE_STEER",                   // CAA axis 9
        "CAA_TANK_DRIVE_FORWARD_BACKWARD",   // CAA axis 16
        "CAA_VPC_TURRET_RIGHT",              // CAA axis 29
        "CAA_TURRET_CAMERA_ELEVATE",         // CAA axis 31
        "CAA_PLANE_ROLL_LEFT_RIGHT",         // CAA axis 33
    };
    for (const char* known : kKnownEmptyResultNames) {
        if (caseInsensitiveEquals(name, known)) {
            lua_pushstring(L, "");
            return 1;
        }
    }
    // Every other name either needs the live per-action key-binding data
    // (CBA branch) or the still-OPEN two-key sub-action resolvers (CAA
    // axis 2/3), or cannot be safely classified from the entries this
    // repository actually has - the result is OPEN rather than an
    // invented "".
    throw OpenStateError("key binding for action '" + name + "' (0x005be440 CBA/CAA lookup)",
                         "spec-lua-api-behaviour.md Sec8.23 / spec-tables-ui-controls.md Sec4.1-4.2");
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
//
// Character spawn state (Sec34.1/Sec34.3, 2026-10-02): a freshly touched
// name's ignoreAI is no longer OPEN by default - EngineState::
// getOrCreateCharacter() applies the now-CONFIRMED engine default (off) at
// construction, so `wasIgnoring` below reads real state immediately
// instead of refusing, UNLESS the character was marked
// markScriptNpcBoundForTesting() (the zone-held script-NPC override
// question, Sec34.4), which forgets it back to OPEN.
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
//
// Character spawn state (Sec34.2, 2026-10-02): the CONFIRMED default-path
// formula (round(M * Hit_Points), M=1.0 for ordinary classes) is
// implemented as EngineState::applyCharacterSpawnDefaults() - see
// CharacterState::maxHitPoints's own doc comment for why nothing in this
// host calls it automatically for a bare name (preset/level resolution is
// this project's own unmodeled gap). This field therefore still stays
// OPEN here until that's called (by a test, or a future real spawn
// integration) - unchanged refusal behavior for a name with no Hit_Points
// supplied, not a regression.
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
// pending while the host's per-frame driver (EngineState::cutsceneHostFrame,
// Sec26.25) is stopped on an OPEN read, since nothing can promote it then.
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
// game_get_is_host (0x008440a0, Sec8.27, Sec26.28; batch 2026-10-01, label
// updated 2026-10-01 per job "fvzk"): no arguments used; 1 boolean: a
// session exists and +0x5c == +0x58. CONFIRMED false in single player, not
// a fallback default - the session installer's only reachable call site is
// an unconditional-but-never-requested mode-manager state (state 9), so it
// provably never runs during normal play; the host pair being OPEN state
// here is therefore dead code in single player, not an awaited value.
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
// constants are CONFIRMED (job nnlt: 0.075f and 0.925f widened to double,
// set by applySpecInitialState); each product is rounded to nearest
// (EngineState::vintSafeFrameRound: an exact tie or an int32 overflow is
// refused as OPEN). a and b are OPEN state here (no specced writer).
// Worked values (Sec26.26): 1280 x 720 -> 96, 54, 1184, 666.
// ---------------------------------------------------------------------
int stub_vint_get_safe_frame(lua_State* L) {
    logCall(L, upLog(L), "vint_get_safe_frame", upStateTag(L));
    EngineState* es = upState(L);
    const double a = es->vintSafeFrameA().get();
    const double b = es->vintSafeFrameB().get();
    const double c1 = es->vintSafeFrameScale1().get();
    const double c2 = es->vintSafeFrameScale2().get();
    const double products[4] = {c1 * a, c1 * b, c2 * a, c2 * b}; // HIGH CONFIDENCE order
    double rounded[4];
    for (int i = 0; i < 4; ++i) rounded[i] = EngineState::vintSafeFrameRound(products[i]); // all before any push
    for (double v : rounded) lua_pushnumber(L, v);
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

// =======================================================================
// Batch 2026-10-01: spec-lua-api-behaviour.md Sec27 (25 UI-cluster
// functions, "ranks 551-650" tranche part D) and Sec28 (25 gameplay-
// cluster functions, part C) - jobs 20261001T021703-team-a-pwgq /
// 20261001T021707-team-a-jxxz. Every function below implements ONLY its
// own entry's CONFIRMED/CORRECTED body (the current, non-struck-through
// text); a sub-item an entry's own 2026-10-01 review-status line still
// lists as OPEN is modelled as an OpenValue/OpenValueMap read (throws
// OpenStateError, caught by openGuard below) rather than invented - see
// engine_state.h's own per-field doc comment for exactly which real
// global/gate each one stands for. Deep multi-step dispatch chains this
// project has no real data tables for (hook-slot-array bases, class-
// descriptor bit tables, trigger/spatial queries, rig-file loading) are
// NOT separately simulated - only the real, checkable gate/branch and a
// count of "this happened" is modelled, the same "record the request,
// not the full engine" convention already established for
// minimap_icon_add_do/object_indicator_add_do earlier in this file.
// =======================================================================

// Shared nil-gated optional-boolean idiom (this batch's own recurring
// "standard nil-gated idiom"): absent or an explicit nil takes
// `defaultValue`; anything else goes through lua_toboolean.
bool optionalBoolDefault(lua_State* L, int idx, bool defaultValue) {
    if (lua_gettop(L) >= idx && lua_type(L, idx) != LUA_TNIL) return lua_toboolean(L, idx) != 0;
    return defaultValue;
}

// Shared nil-gated optional-number idiom, RAW (no rounding) - used where
// the entry's own text does not say the value goes through the round-
// cast pair.
double optionalNumberDefault(lua_State* L, int idx, double defaultValue) {
    if (lua_gettop(L) >= idx && lua_type(L, idx) != LUA_TNIL) return lua_tonumber(L, idx);
    return defaultValue;
}

// ---------------------------------------------------------------------
// 28. cat_mouse_results_select (Sec27.1, 0x007bd240): 1 mandatory number
// (result-slot/winner index, rounded via the round-cast pair, no nil-
// gate). Return: none. CONFIRMED hidden-`this` chain: resolves the cat-
// and-mouse minigame singleton (0x014c2d10) and, if one exists, remaps
// the index into the object's own +0xe0 selection field (0 -> 1 if the
// object's +0x1c field is 1, else 2; 1 -> 2; any other index leaves it
// unchanged) then, when the field is non-zero, sends a message to the
// co-op session (no real networking layer - counted only, see
// EngineState::replicateStateChange's own convention). OPEN (stated in
// the spec itself): the real-world meaning of selection values 1/2.
// ---------------------------------------------------------------------
int stub_cat_mouse_results_select(lua_State* L) {
    logCall(L, upLog(L), "cat_mouse_results_select", upStateTag(L));
    int64_t idx = truncateEa2596(lua_tonumber(L, 1));
    auto& mg = upState(L)->catMouseMinigame();
    if (mg.present.get()) { // OPEN until set
        int32_t sel = mg.selection;
        if (idx == 0) sel = mg.fieldIc1IsOne.get() ? 1 : 2; // OPEN until set
        else if (idx == 1) sel = 2;
        mg.selection = sel;
        if (sel != 0) ++mg.sessionMessageCount;
    }
    return 0;
}

// ---------------------------------------------------------------------
// 29. cell_is_mission_complete (Sec27.2, 0x00a525a0 - the SAME function
// as Sec20.14's `mission_is_complete`, registered under two names). 1
// mandatory string (mission name). Return: 1 boolean. CONFIRMED body:
// false for an empty name; else resolves through the shared singleton
// resolver family (reuses objectResolves() - this project's own one-
// map-for-all-resolvers simplification, engine_state.h's own top note);
// false unless alive; else the resolved object's own +0x88 bit 0x4.
// ---------------------------------------------------------------------
int stub_cell_is_mission_complete(lua_State* L) {
    logCall(L, upLog(L), "cell_is_mission_complete", upStateTag(L));
    std::string name = argString(L, 1);
    bool complete = false;
    if (!name.empty()) {
        if (upState(L)->objectResolves().get(name)) { // OPEN until set
            complete = upState(L)->missionComplete().get(name); // OPEN until set
        }
    }
    lua_pushboolean(L, complete ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// 30. Completion_should_wait_for_coop (Sec27.3, 0x007bfc10). No
// arguments. Return: 1 boolean. CONFIRMED: resolves the shared
// "completion_screen" UI context; absent -> true (the stated default).
// Present -> when the flag-cluster global 0x02282282 is zero, the
// internal flag comes from a double-deref byte (>=1 true); when
// nonzero, from the per-player walk 0x00877ca0(context,1) (abstracted
// to one flag, see engine_state.h's own CompletionScreen doc comment).
// The value actually pushed is the LOGICAL NEGATION of that internal
// flag (CONFIRMED - the name describes the pushed result directly).
// ---------------------------------------------------------------------
int stub_Completion_should_wait_for_coop(lua_State* L) {
    logCall(L, upLog(L), "Completion_should_wait_for_coop", upStateTag(L));
    auto& cs = upState(L)->completionScreen();
    bool internalFlag = true; // CONFIRMED default when no context resolves
    if (cs.present.get()) { // OPEN until set
        if (!cs.flagClusterSet.get()) { // OPEN until set
            internalFlag = cs.derefByteAtLeastOne.get(); // OPEN until set
        } else {
            internalFlag = cs.everyPlayerAtThreshold1.get(); // OPEN until set
        }
    }
    lua_pushboolean(L, internalFlag ? 0 : 1); // CONFIRMED: pushed value is the negation
    return 1;
}

// ---------------------------------------------------------------------
// 31. Completion_user_is_done_viewing (Sec27.4, 0x007bdc80). No
// arguments. Return: none. CONFIRMED: resolves the SAME
// "completion_screen" context as item 30 above; if present and the
// context's own +4 record is enabled (+2 byte nonzero), stores state 1
// (always != 0x81) and sends a message (no real networking layer here -
// counted only).
// ---------------------------------------------------------------------
int stub_Completion_user_is_done_viewing(lua_State* L) {
    logCall(L, upLog(L), "Completion_user_is_done_viewing", upStateTag(L));
    auto& cs = upState(L)->completionScreen();
    if (cs.present.get()) { // OPEN until set
        if (cs.recordEnabled.get()) { // OPEN until set
            cs.lastStateWritten = 1;
            ++cs.messageSentCount;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// 32. vcust_set_camera_pos (Sec27.5, 0x0081fa80). 1 mandatory string
// (camera-position preset name). Return: none. CONFIRMED: resolves the
// current vehicle-customization target (capability+class-gated, then
// liveness-gated - "proceed when alive", Sec27.5's own resolved
// polarity note); on success hashes the preset name (the real CRC-32,
// sr3save::nameHash - CONFIRMED match to 0x00d9e8b0, see this file's own
// multiply33XorHashBucket neighbour for the project's established reuse
// convention) and selects the camera preset by that hash (0x00a9b080's
// own positioning math is OPEN and not simulated - only the real,
// checkable hash is recorded), then caches whether the preset name
// case-insensitively matches "Standard" into the CONFIRMED-initial-
// value global 0x01300b88.
// ---------------------------------------------------------------------
int stub_vcust_set_camera_pos(lua_State* L) {
    logCall(L, upLog(L), "vcust_set_camera_pos", upStateTag(L));
    std::string preset = argString(L, 1);
    auto& vc = upState(L)->vcustCamera();
    if (vc.targetPresent.get()) {       // OPEN until set
        if (vc.targetValid.get()) {     // OPEN until set
            if (vc.targetAlive.get()) { // OPEN until set; true = alive (resolved polarity)
                vc.lastAppliedPresetHash = sr3save::nameHash(preset);
                vc.isStandardActive = caseInsensitiveEquals(preset, "Standard");
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// 33. pause_menu_has_seen_display_cal_screen (Sec27.6, 0x007e0490). No
// arguments. Return: 1 boolean. CONFIRMED trivial body: pushes global
// 0x02297c80 (zero at start; no in-scope setter writes it).
// ---------------------------------------------------------------------
int stub_pause_menu_has_seen_display_cal_screen(lua_State* L) {
    logCall(L, upLog(L), "pause_menu_has_seen_display_cal_screen", upStateTag(L));
    lua_pushboolean(L, upState(L)->pauseMenuSeenDisplayCalScreen() ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// 34. msn_text_adventure_set_screen (Sec27.7, 0x008410c0). 1 mandatory
// number (screen/state index), rounded. Return: none. CONFIRMED: pure
// global setter, no forwarding call, writes 0x01301124 directly
// (CONFIRMED initial -1).
// ---------------------------------------------------------------------
int stub_msn_text_adventure_set_screen(lua_State* L) {
    logCall(L, upLog(L), "msn_text_adventure_set_screen", upStateTag(L));
    int64_t idx = truncateEa2596(lua_tonumber(L, 1));
    upState(L)->textAdventureScreenIndex() = static_cast<int32_t>(idx);
    return 0;
}

// ---------------------------------------------------------------------
// 35. horde_results_set_end_action (Sec27.8, 0x007c6370). 1 mandatory
// number, rounded. Return: none. CONFIRMED: forwards to a bare setter of
// global 0x012f3b48 (CONFIRMED initial 2).
// ---------------------------------------------------------------------
int stub_horde_results_set_end_action(lua_State* L) {
    logCall(L, upLog(L), "horde_results_set_end_action", upStateTag(L));
    int64_t action = truncateEa2596(lua_tonumber(L, 1));
    upState(L)->hordeResultsEndAction() = static_cast<int32_t>(action);
    return 0;
}

// ---------------------------------------------------------------------
// 36. garage_preview_vehicle (Sec27.9, 0x005f8890). 1 mandatory number
// (index), rounded. Return: none. CONFIRMED: looks up a vehicle-type id
// by index in a table (0x014a1a80), and if it differs from the cached
// preview value (0x014a1d1c), updates the cache and fires a refresh
// (0x005f8670's own internals OPEN - counted only).
// ---------------------------------------------------------------------
int stub_garage_preview_vehicle(lua_State* L) {
    logCall(L, upLog(L), "garage_preview_vehicle", upStateTag(L));
    int64_t idx = truncateEa2596(lua_tonumber(L, 1));
    auto& gp = upState(L)->garagePreview();
    int32_t model = gp.vehicleTypeAtIndex.get(std::to_string(idx)); // OPEN until set
    int32_t cached = gp.previewCache.get();                         // OPEN until set
    if (model != cached) {
        gp.previewCache.set(model);
        ++gp.refreshCount;
    }
    return 0;
}

// ---------------------------------------------------------------------
// 37. game_lobby_coop_finished (Sec27.10, 0x007ab170). No arguments.
// Return: none. CONFIRMED: sets the "co-op lobby finished" global
// (0x0224244c, zero-filled) and issues a fade-out request (duration -1,
// no callback, flag 1 - the exact Sec26.23 signature), reusing the
// existing screen-fade mechanism (Sec26.24).
// ---------------------------------------------------------------------
int stub_game_lobby_coop_finished(lua_State* L) {
    logCall(L, upLog(L), "game_lobby_coop_finished", upStateTag(L));
    upState(L)->coopLobbyFinished() = true;
    upState(L)->screenFadeRequest(true, -1, nullptr, 1); // OPEN if the fade state itself is unset
    return 0;
}

// ---------------------------------------------------------------------
// 38. dialog_box_force_close (Sec27.11, 0x007c4960). 1 mandatory number
// (dialog-type/slot id), rounded. Return: none. CONFIRMED structure
// (407-byte body, summarized per this document's own house style): the
// same self-consistency gate `dialog_box_set_result` (Sec23.18) uses
// must pass; if the slot's timer is armed and not yet expired, the
// close is deferred (counted); otherwise, if not already closing, fires
// the result callback (if any) and a named-callback dispatch (if the
// name isn't blank); finally, if not already fully removed, resets the
// slot. Only the real gates/counts are modelled - the full per-slot
// record and the 0x02282d14 ring's own role (itself still OPEN per the
// spec) are not simulated.
// ---------------------------------------------------------------------
int stub_dialog_box_force_close(lua_State* L) {
    logCall(L, upLog(L), "dialog_box_force_close", upStateTag(L));
    int64_t id = truncateEa2596(lua_tonumber(L, 1));
    std::string key = std::to_string(id);
    auto& d = upState(L)->dialogForceClose();
    if (!d.slotMatchesId.get(key)) return 0; // OPEN until set; self-consistency gate failing is a real no-op
    if (d.timerArmedNotExpired.get(key)) {   // OPEN until set
        ++d.closePendingCount;
        return 0;
    }
    if (!d.alreadyClosing.get(key)) {                                    // OPEN until set
        if (d.hasResultCallback.get(key)) ++d.resultCallbackFiredCount;  // OPEN until set
        if (!d.callbackNameBlank.get(key)) ++d.namedCallbackDispatchCount; // OPEN until set
    }
    if (!d.fullyRemoved.get(key)) { // OPEN until set
        ++d.slotResetCount;
        d.fullyRemoved.set(key, true);
    }
    return 0;
}

// ---------------------------------------------------------------------
// 39. game_autosave (Sec27.12, 0x00841f50). No arguments. Return: none.
// CONFIRMED gate (all five must hold): the suppress flag 0x012fcadc
// clear (CONFIRMED file value 1 = suppressed, so a fresh process does
// nothing without a test clearing it), 0x0290ceca clear, no mission
// active, 0x006f8370 false, 0x0229a317 clear. 0x00b94ff0 (the save
// itself) is OPEN per spec - only "every gate passed" is counted.
// ---------------------------------------------------------------------
int stub_game_autosave(lua_State* L) {
    logCall(L, upLog(L), "game_autosave", upStateTag(L));
    auto& a = upState(L)->autosave();
    if (!a.suppressFlag.get()) {                   // OPEN until set; CONFIRMED initial true
        if (!a.secondFlagSet.get()) {              // OPEN until set
            if (!a.missionActive.get()) {          // OPEN until set
                if (!a.thirdGateBlocks.get()) {     // OPEN until set
                    if (!a.fourthFlagNonzero.get()) { // OPEN until set
                        ++a.triggeredCount;
                    }
                }
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// 40. game_send_pause_menu_player_invite (Sec27.13, 0x008440e0). 1
// mandatory number (player slot), rounded. Return: 1 boolean. CONFIRMED:
// bounds-checks the slot against the shared player-slot count
// (0x02289b94) then reports whether 0x0088e690(record,0) returned zero;
// 0x00d34dd0 is a confirmed inert `return 1` stub (ignored). Out-of-
// range is a well-defined false (no further read needed).
// ---------------------------------------------------------------------
int stub_game_send_pause_menu_player_invite(lua_State* L) {
    logCall(L, upLog(L), "game_send_pause_menu_player_invite", upStateTag(L));
    int64_t slot = truncateEa2596(lua_tonumber(L, 1));
    auto& ps = upState(L)->playerSlots();
    uint32_t count = ps.count.get(); // OPEN until set
    bool ok = false;
    if (slot >= 0 && static_cast<uint64_t>(slot) < count) {
        ok = ps.sendInviteOk.get(std::to_string(slot)); // OPEN until set
    }
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// 41. game_can_send_player_invite (Sec27.14, 0x00844120). Structural
// sibling of item 40: same bounds check, 0x0101b530 is a confirmed
// inert `return 1` stub (ignored), reports 0x0088dfd0's own boolean
// result.
// ---------------------------------------------------------------------
int stub_game_can_send_player_invite(lua_State* L) {
    logCall(L, upLog(L), "game_can_send_player_invite", upStateTag(L));
    int64_t slot = truncateEa2596(lua_tonumber(L, 1));
    auto& ps = upState(L)->playerSlots();
    uint32_t count = ps.count.get(); // OPEN until set
    bool ok = false;
    if (slot >= 0 && static_cast<uint64_t>(slot) < count) {
        ok = ps.canSendInvite.get(std::to_string(slot)); // OPEN until set
    }
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// 42. game_set_coop_friendly_fire (Sec27.15, 0x00844510). 1 mandatory
// number (mode 0/1/2), rounded. Return: none. CONFIRMED 3-way enum
// rotation (0->1, 1->2, 2->0 internally; any other input -> 0) applied
// to the real setter 0x007031e0, which only writes when no session is
// present or the local player is the session host (reuses the existing
// coopSession() fields, Sec3.1/Sec8.27 - the same host/solo write gate
// earlier batches already established).
// ---------------------------------------------------------------------
int stub_game_set_coop_friendly_fire(lua_State* L) {
    logCall(L, upLog(L), "game_set_coop_friendly_fire", upStateTag(L));
    int64_t mode = truncateEa2596(lua_tonumber(L, 1));
    int32_t internalValue = 0; // CONFIRMED: both "mode 0" and "invalid" fall through, differing only in the literal
    if (mode == 0) internalValue = 1;
    else if (mode == 1) internalValue = 2;
    else if (mode == 2) internalValue = 0;
    EngineState* st = upState(L);
    bool present = st->coopSession().present.get();                      // OPEN until set
    bool allowed = !present || st->coopSession().localIsHost.get();      // OPEN until set, only read when present
    if (allowed) st->coopFriendlyFireRaw().set(internalValue);
    return 0;
}

// ---------------------------------------------------------------------
// 43. game_get_coop_friendly_fire (Sec27.16, 0x00844580). No arguments.
// Return: 1 number. CONFIRMED: reads the raw internal mode (0x012f4500,
// file value 1) and remaps it 0->2, 1->0, 2->1 (the exact inverse of
// item 42's own rotation); any value outside {0,1,2} pushes 0.
// ---------------------------------------------------------------------
int stub_game_get_coop_friendly_fire(lua_State* L) {
    logCall(L, upLog(L), "game_get_coop_friendly_fire", upStateTag(L));
    int32_t raw = upState(L)->coopFriendlyFireRaw().get(); // CONFIRMED initial 1 (applySpecInitialState)
    int32_t pushed = 0;
    if (raw == 0) pushed = 2;
    else if (raw == 1) pushed = 0;
    else if (raw == 2) pushed = 1;
    lua_pushnumber(L, pushed);
    return 1;
}

// ---------------------------------------------------------------------
// 44. game_send_party_invites (Sec27.17, 0x007c9f50). CONFIRMED: a
// complete, disassembly-confirmed no-op (the shared stub behind several
// bindings this document already records) - no argument read beyond the
// boilerplate, no other call at all.
// ---------------------------------------------------------------------
int stub_game_send_party_invites(lua_State* L) {
    logCall(L, upLog(L), "game_send_party_invites", upStateTag(L));
    return 0;
}

// ---------------------------------------------------------------------
// 45. game_is_connected_to_network (Sec27.18, 0x008427e0). No arguments.
// Return: 1 boolean. CONFIRMED: the call operand is a jump stub to a
// function that returns 1 unconditionally - always true on PC (the
// platform check was compiled out).
// ---------------------------------------------------------------------
int stub_game_is_connected_to_network(lua_State* L) {
    logCall(L, upLog(L), "game_is_connected_to_network", upStateTag(L));
    lua_pushboolean(L, 1);
    return 1;
}

// ---------------------------------------------------------------------
// 46. game_is_signed_in (Sec27.19, 0x00842810). No arguments. Return: 1
// boolean. CONFIRMED: the call operand is a jump stub that returns true
// iff the Steam user interface pointer (0x0101c54c import) is non-null.
// ---------------------------------------------------------------------
int stub_game_is_signed_in(lua_State* L) {
    logCall(L, upLog(L), "game_is_signed_in", upStateTag(L));
    lua_pushboolean(L, upState(L)->steamInterfaceAvailable().get() ? 1 : 0); // OPEN until set
    return 1;
}

// ---------------------------------------------------------------------
// 47. game_sign_into_network (Sec27.20, 0x00842840). 1 mandatory
// boolean (read unconditionally), 1 mandatory string (read
// unconditionally - a Lua completion-callback name). Return: none.
// CONFIRMED: caches a resolved context's own field (not separately
// modelled - nothing in this batch's scope reads it back) and the given
// callback name; the real async-callback registration is a confirmed
// empty stub on PC (never fires); unconditionally surfaces the PC
// "cannot sign in" notice. This project has no real platform SDK, so
// only the call's own raw arguments are recorded, honestly, as "a
// request" (same convention as MinimapIconRecord/ObjectIndicatorRecord
// elsewhere in this file).
// ---------------------------------------------------------------------
int stub_game_sign_into_network(lua_State* L) {
    logCall(L, upLog(L), "game_sign_into_network", upStateTag(L));
    bool want = lua_toboolean(L, 1) != 0;
    std::string cb = argString(L, 2);
    upState(L)->signInRequests().push_back({want, cb});
    ++upState(L)->signInErrorDialogCount(); // CONFIRMED unconditional every call
    return 0;
}

// ---------------------------------------------------------------------
// 48. game_show_coop_gamercard (Sec27.21, 0x00844640). 1 mandatory
// number (player slot), rounded. Return: none. CONFIRMED: 0x0101b4a0 is
// a confirmed inert `return 1` stub (ignored); bounds-checks the slot
// against the shared player-slot count (0x02289b94) and, in range,
// forwards to 0x0086fe40/0x00870810 (OPEN internals - counted only).
// ---------------------------------------------------------------------
int stub_game_show_coop_gamercard(lua_State* L) {
    logCall(L, upLog(L), "game_show_coop_gamercard", upStateTag(L));
    int64_t slot = truncateEa2596(lua_tonumber(L, 1));
    auto& ps = upState(L)->playerSlots();
    uint32_t count = ps.count.get(); // OPEN until set
    if (slot >= 0 && static_cast<uint64_t>(slot) < count) ++ps.gamercardShownCount;
    return 0;
}

// ---------------------------------------------------------------------
// 49. game_main_menu_join_friend_in_progress (Sec27.22, 0x00844670). 1
// mandatory number (friend slot/index), rounded. Return: 1 boolean.
// CONFIRMED: attempts to join (0x007c9410, bounds-checked against the
// same 0x02289b94 count), then calls the confirmed inert `return 1`
// stub 0x0101b4f0 (ignored, closes Sec13.24's own OPEN on it), then
// pushes the earlier-captured boolean.
// ---------------------------------------------------------------------
int stub_game_main_menu_join_friend_in_progress(lua_State* L) {
    logCall(L, upLog(L), "game_main_menu_join_friend_in_progress", upStateTag(L));
    int64_t slot = truncateEa2596(lua_tonumber(L, 1));
    auto& ps = upState(L)->playerSlots();
    uint32_t count = ps.count.get(); // OPEN until set
    bool joined = false;
    if (slot >= 0 && static_cast<uint64_t>(slot) < count) {
        joined = ps.joinFriendInProgressOk.get(std::to_string(slot)); // OPEN until set
    }
    lua_pushboolean(L, joined ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// 50. game_coop_start_new_live (Sec27.23, 0x008425d0). 1 optional
// boolean, nil-gated, DEFAULT TRUE (a notably different default
// polarity from most optional booleans elsewhere in this document).
// Return: none. CONFIRMED: 0x00d2f540 is a confirmed inert `return 1`
// stub (ignored); forwards the boolean to 0x007027b0, which sets the
// "live session" byte (0x014ff6c1) and clears the mutually-exclusive
// "system-link session" byte (0x014ff6c2) when true.
// ---------------------------------------------------------------------
int stub_game_coop_start_new_live(lua_State* L) {
    logCall(L, upLog(L), "game_coop_start_new_live", upStateTag(L));
    bool b = optionalBoolDefault(L, 1, true); // CONFIRMED default true
    upState(L)->coopLiveSessionActive() = b;
    if (b) upState(L)->coopSyslinkSessionActive() = false;
    return 0;
}

// ---------------------------------------------------------------------
// 51. game_coop_start_new_syslink (Sec27.24, 0x00842640). Structural
// sibling of item 50 (byte-for-byte identical shape): 0x00d34d40 is a
// confirmed inert `return 1` stub (ignored); forwards to 0x007027d0,
// which sets the "system-link session" byte and clears "live session".
// ---------------------------------------------------------------------
int stub_game_coop_start_new_syslink(lua_State* L) {
    logCall(L, upLog(L), "game_coop_start_new_syslink", upStateTag(L));
    bool b = optionalBoolDefault(L, 1, true); // CONFIRMED default true
    upState(L)->coopSyslinkSessionActive() = b;
    if (b) upState(L)->coopLiveSessionActive() = false;
    return 0;
}

// ---------------------------------------------------------------------
// 52. game_get_in_progress_type (Sec27.25, 0x008447b0). No arguments.
// Return: 1 number. CONFIRMED 3-branch body: an active mission's own
// +0xa0 type field; else the cat-and-mouse minigame singleton (the SAME
// 0x014c2d10 item 28 reads) -> fixed literal 4; else a phase-6/7
// activity's own +0xa0 field; else -1.
// ---------------------------------------------------------------------
int stub_game_get_in_progress_type(lua_State* L) {
    logCall(L, upLog(L), "game_get_in_progress_type", upStateTag(L));
    EngineState* st = upState(L);
    double result = -1.0;
    if (st->inProgressType().activeMissionPresent.get()) {       // OPEN until set
        result = st->inProgressType().activeMissionType.get();  // OPEN until set
    } else if (st->catMouseMinigame().present.get()) {           // OPEN until set
        result = 4.0; // CONFIRMED fixed literal
    } else if (st->inProgressType().activityPresent.get()) {    // OPEN until set
        result = st->inProgressType().activityType.get();       // OPEN until set
    }
    lua_pushnumber(L, result);
    return 1;
}

// ---------------------------------------------------------------------
// 53. helicopter_set_dont_death_spiral (Sec28.1, 0x00a4cc40). 1
// mandatory string (vehicle/helicopter reference), 1 mandatory boolean
// (no nil-gate). Return: none. CONFIRMED: resolves via the vehicle
// resolver (reuses objectResolves()) and, on success, sets the resolved
// vehicle's own "don't death-spiral" flag (0x00b37740's own gated
// record-vs-replicate split converges on this one Lua-visible flag per
// this batch's no-networking-layer convention).
// ---------------------------------------------------------------------
int stub_helicopter_set_dont_death_spiral(lua_State* L) {
    logCall(L, upLog(L), "helicopter_set_dont_death_spiral", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0;
    if (upState(L)->objectResolves().get(name)) { // OPEN until set
        upState(L)->helicopterDontDeathSpiral()[name] = flag;
    }
    return 0;
}

// ---------------------------------------------------------------------
// 54. helicopter_fly_to_set_goal_direction (Sec28.2, 0x00a4fa80). 1
// mandatory string (helicopter reference), 1 mandatory string (target
// name - when empty the lookup fails and nothing is applied), 1
// optional boolean, nil-gated, default false (mode: target's own third
// orientation vector vs. delta-to-target). Return: none. CONFIRMED
// structure: resolves the helicopter then gates on the helicopter-
// qualification predicate (0x00ad31a0); resolves the target by name; if
// both resolve, the actual position/orientation arithmetic (OPEN per
// this batch's own scope - nothing reads a computed vector back) is not
// reproduced - only the real branch and whether the apply gate
// (0x00b3f6d0's own AI-record check) passed is recorded, honestly (same
// "record the request" convention as object_indicator_add_do).
// ---------------------------------------------------------------------
int stub_helicopter_fly_to_set_goal_direction(lua_State* L) {
    logCall(L, upLog(L), "helicopter_fly_to_set_goal_direction", upStateTag(L));
    std::string heli = argString(L, 1);
    std::string target = argString(L, 2);
    bool mode = optionalBoolDefault(L, 3, false);
    EngineState* st = upState(L);
    if (st->objectResolves().get(heli)) {                        // OPEN until set
        if (st->helicopterFlyTo().qualifies.get(heli)) {         // OPEN until set
            if (!target.empty() && st->objectResolves().get(target)) { // OPEN until set (only read when target non-empty)
                bool applied = st->helicopterFlyTo().applyGate.get(heli); // OPEN until set
                st->helicopterFlyTo().lastRequest[heli] = {target, mode, applied};
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// 55. hdr_bloom_set_multiplier (Sec28.3, 0x00a4cab0). 1 mandatory
// number, no nil-gate, no rounding (narrowed to a single-precision float
// and forwarded directly - CONFIRMED, the Sec4.1 round-cast pair is NOT
// called here). Return: none. CONFIRMED bare global setter (0x012ec280,
// CONFIRMED initial 1.0).
// ---------------------------------------------------------------------
int stub_hdr_bloom_set_multiplier(lua_State* L) {
    logCall(L, upLog(L), "hdr_bloom_set_multiplier", upStateTag(L));
    double raw = lua_tonumber(L, 1);
    upState(L)->hdrBloomMultiplier() = static_cast<float>(raw);
    return 0;
}

// ---------------------------------------------------------------------
// 56. guardian_angel_enable_indicators (Sec28.4, 0x00a4ab80). 1
// mandatory boolean, no nil-gate, no name argument. Return: none.
// CONFIRMED structure: proceeds only when a mode check (0x006d2910)
// reads 3 (OPEN what the mode means) AND a singleton object (0x00614cb0)
// is non-null, then writes the boolean to that object's own +0x57d
// byte.
// ---------------------------------------------------------------------
int stub_guardian_angel_enable_indicators(lua_State* L) {
    logCall(L, upLog(L), "guardian_angel_enable_indicators", upStateTag(L));
    bool flag = lua_toboolean(L, 1) != 0;
    auto& ga = upState(L)->guardianAngel();
    if (ga.modeIsThree.get()) {       // OPEN until set
        if (ga.objectPresent.get()) { // OPEN until set
            ga.indicatorsEnabled = flag;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// 57. group_get_next_npc (Sec28.5, 0x00a4c590). 1 mandatory string
// (script-group name), 1 mandatory string (current-NPC character name).
// Return: 1 Lua value (string) with return count 1 on success; return
// count 0 with nothing pushed on every failure path (CONFIRMED).
// CONFIRMED structure: both names resolve via the shared singleton
// (reuses objectResolves()); walks the current NPC's own +0x9c "next"
// link, stopping at the group's own +0x40 head. This project treats an
// unresolved group OR character as a safe "return 0" rather than
// reproducing the real binary's own null-deref fault on a bad group
// handle with a live character (a deliberate, labelled simplification -
// this project does not simulate crashes).
// ---------------------------------------------------------------------
int stub_group_get_next_npc(lua_State* L) {
    logCall(L, upLog(L), "group_get_next_npc", upStateTag(L));
    std::string group = argString(L, 1);
    std::string current = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(group)) return 0;   // OPEN until set
    if (!st->objectResolves().get(current)) return 0; // OPEN until set
    const std::string& next = st->groupNextNpcName().get(current); // OPEN until set
    if (next.empty()) return 0; // null +0x9c, or wraparound back to the group head
    lua_pushstring(L, next.c_str());
    return 1;
}

// ---------------------------------------------------------------------
// 58. group_get_first_npc (Sec28.6, 0x00a4c520). 1 mandatory string
// (script-group name). Return: 1 Lua value (string) or nothing pushed
// on failure. CONFIRMED: resolves via the SAME singleton resolver; if
// the group's own +0x40 head is non-null, pushes that member's name.
// ---------------------------------------------------------------------
int stub_group_get_first_npc(lua_State* L) {
    logCall(L, upLog(L), "group_get_first_npc", upStateTag(L));
    std::string group = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(group)) return 0; // OPEN until set
    const std::string& first = st->groupFirstNpcName().get(group); // OPEN until set
    if (first.empty()) return 0;
    lua_pushstring(L, first.c_str());
    return 1;
}

// ---------------------------------------------------------------------
// 59. get_num_humans_in_trigger (Sec28.7, 0x00a4c0c0). 1 mandatory
// string (trigger name). Return: 1 number, always pushed. CONFIRMED:
// resolves the trigger via the shared singleton, then counts a
// category-filtered set of nearby objects inside it via a genuine
// hidden-`this` containment test (full chain re-derived, Sec28.7's own
// 2026-10-01 text); the category/shape-test meanings stay OPEN. An
// unresolved trigger pushes 0 (a well-defined "no matches" answer, not
// a guess).
// ---------------------------------------------------------------------
int stub_get_num_humans_in_trigger(lua_State* L) {
    logCall(L, upLog(L), "get_num_humans_in_trigger", upStateTag(L));
    std::string trigger = argString(L, 1);
    double count = 0.0;
    if (upState(L)->objectResolves().get(trigger)) { // OPEN until set
        count = upState(L)->humansInTriggerCount().get(trigger); // OPEN until set
    }
    lua_pushnumber(L, count);
    return 1;
}

// ---------------------------------------------------------------------
// 60. get_char_vehicle_is_in_air (Sec28.8, 0x00a4b160). 1 mandatory
// string (character reference). Return: 1 boolean, always exactly 1
// value. CONFIRMED: resolves the character (generic/"#PLAYER#" chain,
// reuses objectResolves()), reads the SAME cached active-vehicle id-pair
// Sec24.5 establishes, then queries a clean "in air" predicate
// (0x00ad4040). An unresolved character pushes false.
// ---------------------------------------------------------------------
int stub_get_char_vehicle_is_in_air(lua_State* L) {
    logCall(L, upLog(L), "get_char_vehicle_is_in_air", upStateTag(L));
    std::string name = argString(L, 1);
    bool inAir = false;
    if (upState(L)->objectResolves().get(name)) { // OPEN until set
        inAir = upState(L)->vehicleInAirByCharacter().get(name); // OPEN until set
    }
    lua_pushboolean(L, inAir ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// 61. effect_play_finisher (Sec28.9, 0x00a48da0). 1 mandatory string
// (target object name), 1 optional number (height/Z offset, nil-gated,
// default 0.0), 1 optional number (a selector, nil-gated, default the
// plain integer 3 - the decompiler's "4.2039e-45" is a mistyped dword
// literal, CONFIRMED - rounded via the round-cast pair). Return: 1 Lua
// value - the number -1.0 on every failure path (the fixed icon
// effect's own index is -1, or the target does not resolve); on
// success, 0x00a48af0's own return value (the Sec9.1/Sec12.10/Sec12.12
// shared finalizer/apply family - not modelled further here, see
// EffectFinisher::successReturn's own doc comment). CONFIRMED: the
// played effect is always the fixed PC/gamepad "icon" effect, never a
// name derived from arg 1 (arg 1 is the TARGET, per Sec28.9's own
// correction). The exact real evaluation order between the two failure
// conditions is not stated by the spec; checking the effect index first
// (an "early exit" shape, not reproducing any specific instruction
// order) avoids an unjustified extra OPEN read when the index is
// already invalid.
// ---------------------------------------------------------------------
int stub_effect_play_finisher(lua_State* L) {
    logCall(L, upLog(L), "effect_play_finisher", upStateTag(L));
    std::string target = argString(L, 1);
    double heightOffset = optionalNumberDefault(L, 2, 0.0);
    (void)heightOffset; // stored on the request (meaning OPEN per spec); not Lua-observable in this scope
    int64_t selector = truncateEa2596(optionalNumberDefault(L, 3, 3.0));
    (void)selector; // stored on the request (+0x6c), own meaning OPEN per spec
    EngineState* st = upState(L);
    const char* iconName = st->effectFinisher().gamepadMode.get() ? "vfx_xbox_Icon" : "vfx_pc_Icon"; // OPEN until set
    if (!st->effectFinisher().effectIndexValid.get(iconName)) { // OPEN until set
        lua_pushnumber(L, -1.0); // CONFIRMED failure sentinel
        return 1;
    }
    if (target.empty() || !st->objectResolves().get(target)) { // OPEN until set when target non-empty
        lua_pushnumber(L, -1.0);
        return 1;
    }
    lua_pushnumber(L, st->effectFinisher().successReturn.get()); // OPEN until set
    return 1;
}

// ---------------------------------------------------------------------
// 62. dlc3_m03_set_sprint_waning (Sec28.10, 0x00a46fd0). 1 mandatory
// boolean, no nil-gate, no name argument. Return: none. CONFIRMED bare
// global setter (0x0263ae3a, CONFIRMED zero at start).
// ---------------------------------------------------------------------
int stub_dlc3_m03_set_sprint_waning(lua_State* L) {
    logCall(L, upLog(L), "dlc3_m03_set_sprint_waning", upStateTag(L));
    upState(L)->dlc3SprintWaning() = lua_toboolean(L, 1) != 0;
    return 0;
}

// ---------------------------------------------------------------------
// 63. debris_flow_recycle_object (Sec28.11, 0x00a47fd0). 1 mandatory
// number (debris-flow id, rounded, no nil-gate), 1 mandatory string
// (object reference name). Return: none. CONFIRMED: resolves the object
// via the shared singleton, then hands it to the debris-flow row's own
// third method (0x006fd4f0 - its role is HYPOTHESIS from the name; NOT
// the mirror of debris_flow_add_avoid_object, see Sec28.11's own
// correction). Only the gated call is counted, keyed by flow id.
// ---------------------------------------------------------------------
int stub_debris_flow_recycle_object(lua_State* L) {
    logCall(L, upLog(L), "debris_flow_recycle_object", upStateTag(L));
    int64_t flowId = truncateEa2596(lua_tonumber(L, 1));
    std::string name = argString(L, 2);
    if (upState(L)->objectResolves().get(name)) { // OPEN until set
        ++upState(L)->debrisFlowRecycleCount()[flowId];
    }
    return 0;
}

// ---------------------------------------------------------------------
// 64. customization_restore_player_rig (Sec28.12, 0x00a43c30). 1
// optional number (player-selector bitmask, nil-gated, default 3 - both
// bits). Return: none. CONFIRMED: bit 0 restores local player 1's body
// rig to the gender-appropriate default file (reading the player's own
// +0xa41 byte, unconditionally - no null-check per spec); bit 1 does
// the same for the co-op player, gated on that player existing
// (0x009df3d0 non-null). Only the gated reads/counts are modelled - the
// actual rig-file load (0x009e3400) is a confirmed shared helper, not
// simulated.
// ---------------------------------------------------------------------
int stub_customization_restore_player_rig(lua_State* L) {
    logCall(L, upLog(L), "customization_restore_player_rig", upStateTag(L));
    int64_t mask = truncateEa2596(optionalNumberDefault(L, 1, 3.0));
    auto& rig = upState(L)->playerRig();
    if (mask & 1) {
        (void)rig.localPlayer1Gender.get(); // OPEN until set - "not null-checked", always read
        ++rig.restorePlayer1Count;
    }
    if (mask & 2) {
        if (rig.coopPlayerPresent.get()) { // OPEN until set
            (void)rig.coopPlayerGender.get(); // OPEN until set
            ++rig.restoreCoopCount;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// 65. crib_weapon_add_enable (Sec28.13, 0x00a42c20). No arguments
// consumed. Return: none. CONFIRMED: the enable half of a shared setter
// (0x005ee6f0) writing global 0x012ecb34 (CONFIRMED file value 1).
// ---------------------------------------------------------------------
int stub_crib_weapon_add_enable(lua_State* L) {
    logCall(L, upLog(L), "crib_weapon_add_enable", upStateTag(L));
    upState(L)->cribWeaponAddEnabled() = true;
    return 0;
}

// ---------------------------------------------------------------------
// 66. crib_weapon_add_disable (Sec28.14, 0x00a42c40). No arguments
// consumed. Return: none. CONFIRMED: the SAME shared setter as item 65,
// called with the fixed literal 0 instead. (One of the two named
// examples the manager flagged for this batch: a shared setter with
// argument 0, read from this entry's own exact body, not assumed from
// the name alone.)
// ---------------------------------------------------------------------
int stub_crib_weapon_add_disable(lua_State* L) {
    logCall(L, upLog(L), "crib_weapon_add_disable", upStateTag(L));
    upState(L)->cribWeaponAddEnabled() = false;
    return 0;
}

// ---------------------------------------------------------------------
// 67. crib_unlock_strongold (Sec28.15, 0x00a46500). 1 mandatory string
// (stronghold/crib name - the in-binary name is "strongold", not
// "stronghold"). Return: none. CONFIRMED: resolves via a new confirmed
// member of the shared singleton resolver family, then (with a session
// and as host) unlocks only if the stronghold's own "still locked" bit
// is set; without a session or as a client, takes a different,
// internals-OPEN request path (counted only).
// ---------------------------------------------------------------------
int stub_crib_unlock_strongold(lua_State* L) {
    logCall(L, upLog(L), "crib_unlock_strongold", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    bool present = st->coopSession().present.get(); // OPEN until set
    bool hostBranch = present && st->coopSession().localIsHost.get(); // OPEN until set, only read when present
    auto& sh = st->stronghold();
    if (hostBranch) {
        if (sh.stillLocked.get(name)) { // OPEN until set
            sh.stillLocked.set(name, false);
            ++sh.unlockedCount;
        }
    } else {
        ++sh.clientOrSoloRequestCount;
    }
    return 0;
}

// ---------------------------------------------------------------------
// 68. continuous_explosion_start (Sec28.16, 0x00a46340). 2 mandatory
// strings (definition name, target/position reference name). Return:
// none. CONFIRMED: resolves arg 1 via a PLAIN cdecl resolver (NOT the
// shared singleton family - a genuine negative, modelled as its own
// dedicated map); resolves arg 2 via the shared singleton; if both
// resolve and no continuous explosion is already active (the ONE global
// state this reconciles with Sec22.24's own stop function), starts it.
// ---------------------------------------------------------------------
int stub_continuous_explosion_start(lua_State* L) {
    logCall(L, upLog(L), "continuous_explosion_start", upStateTag(L));
    std::string defName = argString(L, 1);
    std::string targetName = argString(L, 2);
    EngineState* st = upState(L);
    auto& ce = st->continuousExplosion();
    if (!ce.definitionResolves.get(defName)) return 0;     // OPEN until set
    if (!st->objectResolves().get(targetName)) return 0;   // OPEN until set
    if (ce.active.get()) return 0; // OPEN until set; CONFIRMED "ignored if one is already active"
    ce.active.set(true);
    ce.lastDefName = defName;
    ce.lastTargetName = targetName;
    ++ce.startCount;
    return 0;
}

// ---------------------------------------------------------------------
// 69. clear_callbacks_for_obj (Sec28.17, 0x00a46020). 1 mandatory string
// (object reference name, any kind). Return: none. CONFIRMED dispatch
// structure (seven confirmed hidden-`this` findings across six wrapper
// functions, each an N-slot "release when host, write -1" loop over a
// different per-kind hook-array base) - not individually simulated here;
// only that a call cleared callbacks for a resolved object is counted,
// per name (same "record the request" convention as
// minimap_icon_add_do/object_indicator_add_do elsewhere in this file).
// ---------------------------------------------------------------------
int stub_clear_callbacks_for_obj(lua_State* L) {
    logCall(L, upLog(L), "clear_callbacks_for_obj", upStateTag(L));
    std::string name = argString(L, 1);
    if (upState(L)->objectResolves().get(name)) { // OPEN until set
        ++upState(L)->callbacksClearedCount()[name];
    }
    return 0;
}

// ---------------------------------------------------------------------
// 70. city_zone_swap_is_active (Sec28.18, 0x00a42a60). 1 mandatory
// string (city-zone/pair name). Return: 1 boolean. CONFIRMED: hashes the
// name via the real, general-purpose name hash (sr3save::nameHash -
// confirmed match to 0x00d9e8b0) and checks membership in the active-
// swaps hash list (0x0242dc90) - this project's own real, empty-by-
// default set (Sec1.9's `city_zone_swap` setter is out of this batch's
// own scope; an empty set IS the honest "nothing marked active yet"
// default, not a guess).
// ---------------------------------------------------------------------
int stub_city_zone_swap_is_active(lua_State* L) {
    logCall(L, upLog(L), "city_zone_swap_is_active", upStateTag(L));
    std::string name = argString(L, 1);
    uint32_t h = sr3save::nameHash(name);
    bool active = upState(L)->activeCityZoneSwapHashes().count(h) != 0;
    lua_pushboolean(L, active ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// 71. character_set_counter_on_grabbed (Sec28.19, 0x00a421a0). 1
// mandatory string (character reference), 1 mandatory boolean (no nil-
// gate). Return: none. CONFIRMED: resolves via the generic/"#PLAYER#"
// chain (reuses objectResolves()); on success, a double-gate (direct
// path or replicate path, abstracted to one flag - see
// CounterOnGrabbed::gatePasses's own doc comment) determines whether
// the flag is actually applied.
// ---------------------------------------------------------------------
int stub_character_set_counter_on_grabbed(lua_State* L) {
    logCall(L, upLog(L), "character_set_counter_on_grabbed", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        if (st->counterOnGrabbed().gatePasses.get(name)) { // OPEN until set
            st->counterOnGrabbed().value[name] = flag;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// 72. character_remove_child_item_by_name (Sec28.20, 0x00a452d0). 1
// mandatory string (character reference), 1 mandatory string (child
// item name). Return: none. CONFIRMED: resolves the character via the
// generic/"#PLAYER#" chain; removes every child item (case-insensitive
// match) from this project's own minimal children-list stand-in (see
// characterChildItems()'s own doc comment for why the two real
// descriptor-bit gates are not separately simulated).
// ---------------------------------------------------------------------
int stub_character_remove_child_item_by_name(lua_State* L) {
    logCall(L, upLog(L), "character_remove_child_item_by_name", upStateTag(L));
    std::string name = argString(L, 1);
    std::string item = argString(L, 2);
    if (upState(L)->objectResolves().get(name)) { // OPEN until set
        auto& items = upState(L)->characterChildItems()[name];
        items.erase(std::remove_if(items.begin(), items.end(),
                                    [&](const std::string& s) { return caseInsensitiveEquals(s, item); }),
                    items.end());
    }
    return 0;
}

// ---------------------------------------------------------------------
// 73. character_get_gender (Sec28.21, 0x00a43090). 1 mandatory string
// (character reference). Return: 1 number, always pushed (0 on resolve
// failure). CONFIRMED: resolves via the generic/"#PLAYER#" chain, then
// reads the resolved character's own +0xa41 byte (independently cross-
// confirmed against item 64's own read of the same field, Sec28.12).
// ---------------------------------------------------------------------
int stub_character_get_gender(lua_State* L) {
    logCall(L, upLog(L), "character_get_gender", upStateTag(L));
    std::string name = argString(L, 1);
    double gender = 0.0; // CONFIRMED: 0 on resolve failure
    if (upState(L)->objectResolves().get(name)) { // OPEN until set
        gender = upState(L)->characterGender().get(name); // OPEN until set
    }
    lua_pushnumber(L, gender);
    return 1;
}

// ---------------------------------------------------------------------
// 74. character_evacuate_from_all_vehicles (Sec28.22, 0x00a413d0). 1
// mandatory string (character reference), 1 optional boolean, nil-
// gated, default false. Return: none. CONFIRMED: resolves via the
// generic/"#PLAYER#" chain, then forwards the NEGATED boolean plus a
// fixed trailing 0 to a shared occupant-state dispatcher (0x00af3e10,
// already characterized at Sec21.11; its own three-way state dispatch
// is OPEN here, not simulated) - only the gated call is counted.
// ---------------------------------------------------------------------
int stub_character_evacuate_from_all_vehicles(lua_State* L) {
    logCall(L, upLog(L), "character_evacuate_from_all_vehicles", upStateTag(L));
    std::string name = argString(L, 1);
    bool arg2 = optionalBoolDefault(L, 2, false);
    (void)arg2; // forwarded in negated sense to 0x00af3e10 - its own dispatch is OPEN, not simulated
    if (upState(L)->objectResolves().get(name)) { // OPEN until set
        ++upState(L)->evacuateFromVehiclesCount()[name];
    }
    return 0;
}

// ---------------------------------------------------------------------
// 75. cellphone_animate_start_do (Sec28.23, 0x00a46550). No arguments
// consumed (the `lua_gettop` result is read but never used; the pushed
// literal 0 is likewise never read by the callee). Return: none.
// CONFIRMED: when local player 1 exists (reuses the existing
// hasLocalPlayer()) and is not suppressed (0x00943a20), plays the fixed
// "Cell Phone Answer" animation (not itself simulated - only the gated
// play is counted).
// ---------------------------------------------------------------------
int stub_cellphone_animate_start_do(lua_State* L) {
    logCall(L, upLog(L), "cellphone_animate_start_do", upStateTag(L));
    EngineState* st = upState(L);
    if (st->hasLocalPlayer().get()) { // OPEN until set
        if (!st->cellphoneAnimSuppressed().get()) { // OPEN until set
            ++st->cellphoneAnimPlayedCount();
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// 76. boss_battle_matt_get_cheat (Sec28.24, 0x00a40b10). No arguments
// consumed. Return: 1 number, always pushed. CONFIRMED: reads the
// cheat-slot sentinel global (0x012ec730) and pushes it as a double. Its
// only writer in any spec read so far is Sec24.1's `cheats_stop`, out of
// this batch's own scope - stays OPEN until a test (or that function,
// when implemented) sets it.
// ---------------------------------------------------------------------
int stub_boss_battle_matt_get_cheat(lua_State* L) {
    logCall(L, upLog(L), "boss_battle_matt_get_cheat", upStateTag(L));
    int32_t v = upState(L)->bossBattleMatt().cheatSlot.get(); // OPEN until set
    lua_pushnumber(L, static_cast<lua_Number>(v));
    return 1;
}

// ---------------------------------------------------------------------
// 77. boss_battle_matt_cheats_start (Sec28.25, 0x00a40a10). 1 optional
// number (cheat-code id, nil-gated, default 0, via the round-cast pair
// as elsewhere in this document, then clamped so any resolved value
// <= -1 collapses to exactly -1), 1 optional number (nil-gated, default
// 0, NOT rounded - Sec28.25's own text does not say it is), 1 further
// optional number (same), 1 optional boolean, nil-gated, default TRUE.
// Return: none. CONFIRMED: id >= 0 records id/n2/n3 and arms a 3000ms
// deadline; id == -1, gated on the retry counter (0x012ec720) being
// below the CONFIRMED static limit 4, arms a 5000ms (true) or immediate
// (false) deadline instead (the stub call 0x00d2f560 is confirmed
// inert). The shared clock reuses the existing screen-fade host clock
// (the same real global 0x01320d9c both areas cite).
// ---------------------------------------------------------------------
int stub_boss_battle_matt_cheats_start(lua_State* L) {
    logCall(L, upLog(L), "boss_battle_matt_cheats_start", upStateTag(L));
    int64_t id = truncateEa2596(optionalNumberDefault(L, 1, 0.0));
    if (id <= -1) id = -1; // CONFIRMED clamp
    double n2 = optionalNumberDefault(L, 2, 0.0);
    double n3 = optionalNumberDefault(L, 3, 0.0);
    bool flagArg = optionalBoolDefault(L, 4, true); // CONFIRMED default true
    EngineState* st = upState(L);
    auto& bbm = st->bossBattleMatt();
    auto wrapDeadline = [](int64_t clockMs, int64_t addMs) {
        constexpr int64_t kWrap = 1800000000;
        return (clockMs + addMs) % kWrap;
    };
    if (id >= 0) {
        bbm.lastId = static_cast<int32_t>(id);
        bbm.lastN2 = n2;
        bbm.lastN3 = n3;
        bbm.active = true;
        bbm.deadlineMs = wrapDeadline(st->screenFadeClockMs(), 3000);
    } else {
        int32_t retries = bbm.retryCounter.get(); // OPEN until set
        if (retries < EngineState::BossBattleMatt::kRetryLimit) {
            bbm.active = true;
            bbm.deadlineMs = wrapDeadline(st->screenFadeClockMs(), flagArg ? 5000 : 0);
        }
    }
    return 0;
}

// =======================================================================
// Batch 2026-10-02: spec-lua-bindings.md Sec18-Sec21 ("Vint UI API"), the
// 8 names a partner team's own real-mission-drive trace flagged as the
// single biggest unspecced area by runtime call volume (Sec18's own front
// matter: vint_set_property 14,500 calls, vint_get_property 1,692,
// vint_get_time_index 1,206, vint_dataresponder_finished/
// vint_internal_dataresponder_request 1,102 each, vint_dataitem_get 711,
// vint_object_first_child 708, vint_object_clone 509). All eight are
// members of the same 55-name registrar (FUN_00e1dfb0) vint_object_find
// (Sec13.7/Sec15, function 13 above) already belongs to - genuinely
// native for all eight (Sec18's own grep check against the six real
// preload files), never a missing Lua-library load. Each function below
// implements ONLY its own (sub)section's CONFIRMED argument-reading and
// return-arity contract; a deep multi-step dispatch chain this project has
// no real data table for (the Sec9.2 per-type property-descriptor table,
// the tween redirect, the callback-claim mechanism, the data-responder's
// own final dispatch FUN_00e1e660) is NOT separately simulated - same
// "record the real, checkable gate/branch, not the full engine" convention
// the Sec27/Sec28 batch above already established. See engine_state.h's
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
// Sec19.2/Sec20.2 both cite (FUN_00e1a420). Returns how many Lua values it
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
// property's own type/size code - NOT modeled, see
// EngineState::setVintProperty's own doc comment). Return: CONFIRMED -
// always zero Lua values, every path.
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
// property's own type (NOT modeled - see
// EngineState::findVintProperty's own doc comment; this minimal stand-in
// echoes back exactly whatever vint_set_property last stored) - zero Lua
// values (not even nil) on a bad handle or a property-name miss.
// ---------------------------------------------------------------------
int stub_vint_get_property(lua_State* L) {
    logCall(L, upLog(L), "vint_get_property", upStateTag(L));
    uint32_t handle = static_cast<uint32_t>(lua_tonumber(L, 1));
    std::string propertyName = argString(L, 2);
    const std::vector<VintTaggedValue>* values = upState(L)->findVintProperty(handle, propertyName);
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

// 0x0083dff0 (Sec30.5, shared by vehicle_exit_group_do/_check_done): "t.n
// if numeric, else counts via lua_next; -1 for missing/nil [table]." This
// project reads the actual names from a table shaped as the spec's own "a
// Lua table of character names" text describes: a plain Lua array, `t.n`
// (if a number) giving the element count or, absent that, elements read in
// order 1..N until the first nil - the SAME standard contiguous-array idiom
// this file already uses elsewhere for a table argument (fadeOutReadColour
// above, Sec2.9). The exact per-index enumeration algorithm behind "else
// counts via lua_next" is not independently given beyond the count-
// determination method itself, so this project's own standard idiom stands
// in for it (a stated choice, not a guessed engine mechanism). A missing or
// non-table argument returns empty (the "-1" case - no names to process).
std::vector<std::string> readCharacterNameArray(lua_State* L, int idx) {
    std::vector<std::string> out;
    if (idx > lua_gettop(L) || lua_type(L, idx) != LUA_TTABLE) return out;
    lua_pushstring(L, "n");
    lua_gettable(L, idx);
    bool hasN = lua_type(L, -1) == LUA_TNUMBER;
    double n = hasN ? lua_tonumber(L, -1) : 0.0;
    lua_pop(L, 1);
    int count = (hasN && n >= 0.0 && n < 1000000.0) ? static_cast<int>(n) : -1;
    for (int i = 1; count < 0 || i <= count; ++i) {
        lua_pushnumber(L, i);
        lua_gettable(L, idx);
        if (lua_type(L, -1) == LUA_TNIL) { lua_pop(L, 1); break; }
        out.push_back(argString(L, -1));
        lua_pop(L, 1);
        if (i > 100000) break; // a safety bound, not a spec fact - avoids an unbounded loop on a hostile table
    }
    return out;
}

// Shared body for the 3 single-vehicle force-flag setters below (Sec30.5):
// resolve (reuses objectResolves(), Sec29's "one shared global name map" -
// see VehicleState's own doc comment, engine_state.h), then apply the
// double-gate: gate passes -> set the bit directly; gate fails -> replicate
// only (the no-op stand-in for the real opcode-0x46 record), no local bit
// write - CONFIRMED either/or shape (Sec30.5: "either true -> write the bit
// directly; both false -> open ... instead").
void applyVehicleForceFlagSetter(lua_State* L, const char* name, const std::string& vehicleName, bool flag,
                                  uint32_t bitMask) {
    EngineState* st = upState(L);
    if (!st->objectResolves().get(vehicleName)) return; // OPEN until set
    VehicleState& vehicle = st->getOrCreateVehicle(vehicleName);
    if (vehicle.forceFlagGatePasses.get()) { // OPEN until set
        vehicle.forceFlags.setBits(bitMask, flag ? bitMask : 0u);
    } else {
        EngineState::replicateStateChange(name, vehicleName);
    }
}

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
        if (vehicle.forceFlagGatePasses.get()) { // OPEN until set, per vehicle
            vehicle.forceFlags.setBits(VehicleState::kRadioControlsLockedBit, 0u); // hardcoded false
        } else {
            EngineState::replicateStateChange("vehicle_clear_all_radio_locks", entry.first);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// vehicle_is_vtol (Sec30.5, 0x00a66170). 1 mandatory vehicle name. Return:
// exactly 1 boolean, CONFIRMED. Inline resolution (no character redirect -
// this project does not model the redirect distinction either way, see
// VehicleState's own doc comment); true only when VehicleState::
// vehicleClass reads 4. An unresolved name pushes false (CONFIRMED: the
// real inline chain's own liveness/+0x70 failure path yields false here,
// same "unresolved -> false" shape as every boolean-returning query
// elsewhere in this file).
// ---------------------------------------------------------------------
int stub_vehicle_is_vtol(lua_State* L) {
    logCall(L, upLog(L), "vehicle_is_vtol", upStateTag(L));
    std::string vehicleName = argString(L, 1);
    bool isVtol = false;
    EngineState* st = upState(L);
    if (st->objectResolves().get(vehicleName)) { // OPEN until set
        isVtol = st->getOrCreateVehicle(vehicleName).vehicleClass.get() == 4; // OPEN until set
    }
    lua_pushboolean(L, isVtol ? 1 : 0);
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
        if (!c.isAlive.get()) continue; // OPEN until set
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
        // Batch 2026-10-01: spec-lua-api-behaviour.md Sec27 (25 names, all
        // `ui` in tools/lua_all_registered_1490_tagged.txt) and Sec28 (25
        // names, all `gameplay`) - grepped directly, not assumed.
        "cat_mouse_results_select",
        "cell_is_mission_complete",
        "Completion_should_wait_for_coop",
        "Completion_user_is_done_viewing",
        "vcust_set_camera_pos",
        "pause_menu_has_seen_display_cal_screen",
        "msn_text_adventure_set_screen",
        "horde_results_set_end_action",
        "garage_preview_vehicle",
        "game_lobby_coop_finished",
        "dialog_box_force_close",
        "game_autosave",
        "game_send_pause_menu_player_invite",
        "game_can_send_player_invite",
        "game_set_coop_friendly_fire",
        "game_get_coop_friendly_fire",
        "game_send_party_invites",
        "game_is_connected_to_network",
        "game_is_signed_in",
        "game_sign_into_network",
        "game_show_coop_gamercard",
        "game_main_menu_join_friend_in_progress",
        "game_coop_start_new_live",
        "game_coop_start_new_syslink",
        "game_get_in_progress_type",
        "helicopter_set_dont_death_spiral",
        "helicopter_fly_to_set_goal_direction",
        "hdr_bloom_set_multiplier",
        "guardian_angel_enable_indicators",
        "group_get_next_npc",
        "group_get_first_npc",
        "get_num_humans_in_trigger",
        "get_char_vehicle_is_in_air",
        "effect_play_finisher",
        "dlc3_m03_set_sprint_waning",
        "debris_flow_recycle_object",
        "customization_restore_player_rig",
        "crib_weapon_add_enable",
        "crib_weapon_add_disable",
        "crib_unlock_strongold",
        "continuous_explosion_start",
        "clear_callbacks_for_obj",
        "city_zone_swap_is_active",
        "character_set_counter_on_grabbed",
        "character_remove_child_item_by_name",
        "character_get_gender",
        "character_evacuate_from_all_vehicles",
        "cellphone_animate_start_do",
        "boss_battle_matt_get_cheat",
        "boss_battle_matt_cheats_start",
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

    // Batch 2026-10-01: spec-lua-api-behaviour.md Sec27/Sec28 (50 names).
    if (wants("cat_mouse_results_select")) registerOne(L, state, log, stateTag, "cat_mouse_results_select", openGuard<stub_cat_mouse_results_select>);
    if (wants("cell_is_mission_complete")) registerOne(L, state, log, stateTag, "cell_is_mission_complete", openGuard<stub_cell_is_mission_complete>);
    if (wants("Completion_should_wait_for_coop")) registerOne(L, state, log, stateTag, "Completion_should_wait_for_coop", openGuard<stub_Completion_should_wait_for_coop>);
    if (wants("Completion_user_is_done_viewing")) registerOne(L, state, log, stateTag, "Completion_user_is_done_viewing", openGuard<stub_Completion_user_is_done_viewing>);
    if (wants("vcust_set_camera_pos")) registerOne(L, state, log, stateTag, "vcust_set_camera_pos", openGuard<stub_vcust_set_camera_pos>);
    if (wants("pause_menu_has_seen_display_cal_screen")) registerOne(L, state, log, stateTag, "pause_menu_has_seen_display_cal_screen", openGuard<stub_pause_menu_has_seen_display_cal_screen>);
    if (wants("msn_text_adventure_set_screen")) registerOne(L, state, log, stateTag, "msn_text_adventure_set_screen", openGuard<stub_msn_text_adventure_set_screen>);
    if (wants("horde_results_set_end_action")) registerOne(L, state, log, stateTag, "horde_results_set_end_action", openGuard<stub_horde_results_set_end_action>);
    if (wants("garage_preview_vehicle")) registerOne(L, state, log, stateTag, "garage_preview_vehicle", openGuard<stub_garage_preview_vehicle>);
    if (wants("game_lobby_coop_finished")) registerOne(L, state, log, stateTag, "game_lobby_coop_finished", openGuard<stub_game_lobby_coop_finished>);
    if (wants("dialog_box_force_close")) registerOne(L, state, log, stateTag, "dialog_box_force_close", openGuard<stub_dialog_box_force_close>);
    if (wants("game_autosave")) registerOne(L, state, log, stateTag, "game_autosave", openGuard<stub_game_autosave>);
    if (wants("game_send_pause_menu_player_invite")) registerOne(L, state, log, stateTag, "game_send_pause_menu_player_invite", openGuard<stub_game_send_pause_menu_player_invite>);
    if (wants("game_can_send_player_invite")) registerOne(L, state, log, stateTag, "game_can_send_player_invite", openGuard<stub_game_can_send_player_invite>);
    if (wants("game_set_coop_friendly_fire")) registerOne(L, state, log, stateTag, "game_set_coop_friendly_fire", openGuard<stub_game_set_coop_friendly_fire>);
    if (wants("game_get_coop_friendly_fire")) registerOne(L, state, log, stateTag, "game_get_coop_friendly_fire", openGuard<stub_game_get_coop_friendly_fire>);
    if (wants("game_send_party_invites")) registerOne(L, state, log, stateTag, "game_send_party_invites", openGuard<stub_game_send_party_invites>);
    if (wants("game_is_connected_to_network")) registerOne(L, state, log, stateTag, "game_is_connected_to_network", openGuard<stub_game_is_connected_to_network>);
    if (wants("game_is_signed_in")) registerOne(L, state, log, stateTag, "game_is_signed_in", openGuard<stub_game_is_signed_in>);
    if (wants("game_sign_into_network")) registerOne(L, state, log, stateTag, "game_sign_into_network", openGuard<stub_game_sign_into_network>);
    if (wants("game_show_coop_gamercard")) registerOne(L, state, log, stateTag, "game_show_coop_gamercard", openGuard<stub_game_show_coop_gamercard>);
    if (wants("game_main_menu_join_friend_in_progress")) registerOne(L, state, log, stateTag, "game_main_menu_join_friend_in_progress", openGuard<stub_game_main_menu_join_friend_in_progress>);
    if (wants("game_coop_start_new_live")) registerOne(L, state, log, stateTag, "game_coop_start_new_live", openGuard<stub_game_coop_start_new_live>);
    if (wants("game_coop_start_new_syslink")) registerOne(L, state, log, stateTag, "game_coop_start_new_syslink", openGuard<stub_game_coop_start_new_syslink>);
    if (wants("game_get_in_progress_type")) registerOne(L, state, log, stateTag, "game_get_in_progress_type", openGuard<stub_game_get_in_progress_type>);
    if (wants("helicopter_set_dont_death_spiral")) registerOne(L, state, log, stateTag, "helicopter_set_dont_death_spiral", openGuard<stub_helicopter_set_dont_death_spiral>);
    if (wants("helicopter_fly_to_set_goal_direction")) registerOne(L, state, log, stateTag, "helicopter_fly_to_set_goal_direction", openGuard<stub_helicopter_fly_to_set_goal_direction>);
    if (wants("hdr_bloom_set_multiplier")) registerOne(L, state, log, stateTag, "hdr_bloom_set_multiplier", openGuard<stub_hdr_bloom_set_multiplier>);
    if (wants("guardian_angel_enable_indicators")) registerOne(L, state, log, stateTag, "guardian_angel_enable_indicators", openGuard<stub_guardian_angel_enable_indicators>);
    if (wants("group_get_next_npc")) registerOne(L, state, log, stateTag, "group_get_next_npc", openGuard<stub_group_get_next_npc>);
    if (wants("group_get_first_npc")) registerOne(L, state, log, stateTag, "group_get_first_npc", openGuard<stub_group_get_first_npc>);
    if (wants("get_num_humans_in_trigger")) registerOne(L, state, log, stateTag, "get_num_humans_in_trigger", openGuard<stub_get_num_humans_in_trigger>);
    if (wants("get_char_vehicle_is_in_air")) registerOne(L, state, log, stateTag, "get_char_vehicle_is_in_air", openGuard<stub_get_char_vehicle_is_in_air>);
    if (wants("effect_play_finisher")) registerOne(L, state, log, stateTag, "effect_play_finisher", openGuard<stub_effect_play_finisher>);
    if (wants("dlc3_m03_set_sprint_waning")) registerOne(L, state, log, stateTag, "dlc3_m03_set_sprint_waning", openGuard<stub_dlc3_m03_set_sprint_waning>);
    if (wants("debris_flow_recycle_object")) registerOne(L, state, log, stateTag, "debris_flow_recycle_object", openGuard<stub_debris_flow_recycle_object>);
    if (wants("customization_restore_player_rig")) registerOne(L, state, log, stateTag, "customization_restore_player_rig", openGuard<stub_customization_restore_player_rig>);
    if (wants("crib_weapon_add_enable")) registerOne(L, state, log, stateTag, "crib_weapon_add_enable", openGuard<stub_crib_weapon_add_enable>);
    if (wants("crib_weapon_add_disable")) registerOne(L, state, log, stateTag, "crib_weapon_add_disable", openGuard<stub_crib_weapon_add_disable>);
    if (wants("crib_unlock_strongold")) registerOne(L, state, log, stateTag, "crib_unlock_strongold", openGuard<stub_crib_unlock_strongold>);
    if (wants("continuous_explosion_start")) registerOne(L, state, log, stateTag, "continuous_explosion_start", openGuard<stub_continuous_explosion_start>);
    if (wants("clear_callbacks_for_obj")) registerOne(L, state, log, stateTag, "clear_callbacks_for_obj", openGuard<stub_clear_callbacks_for_obj>);
    if (wants("city_zone_swap_is_active")) registerOne(L, state, log, stateTag, "city_zone_swap_is_active", openGuard<stub_city_zone_swap_is_active>);
    if (wants("character_set_counter_on_grabbed")) registerOne(L, state, log, stateTag, "character_set_counter_on_grabbed", openGuard<stub_character_set_counter_on_grabbed>);
    if (wants("character_remove_child_item_by_name")) registerOne(L, state, log, stateTag, "character_remove_child_item_by_name", openGuard<stub_character_remove_child_item_by_name>);
    if (wants("character_get_gender")) registerOne(L, state, log, stateTag, "character_get_gender", openGuard<stub_character_get_gender>);
    if (wants("character_evacuate_from_all_vehicles")) registerOne(L, state, log, stateTag, "character_evacuate_from_all_vehicles", openGuard<stub_character_evacuate_from_all_vehicles>);
    if (wants("cellphone_animate_start_do")) registerOne(L, state, log, stateTag, "cellphone_animate_start_do", openGuard<stub_cellphone_animate_start_do>);
    if (wants("boss_battle_matt_get_cheat")) registerOne(L, state, log, stateTag, "boss_battle_matt_get_cheat", openGuard<stub_boss_battle_matt_get_cheat>);
    if (wants("boss_battle_matt_cheats_start")) registerOne(L, state, log, stateTag, "boss_battle_matt_cheats_start", openGuard<stub_boss_battle_matt_cheats_start>);
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
}

} // namespace sr3luahost
