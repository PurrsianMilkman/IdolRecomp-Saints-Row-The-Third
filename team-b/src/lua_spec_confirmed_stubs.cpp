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

// DECISION (2026-10-03, project-wide, relayed from the orchestrator's own
// §55 note/interp_lgdz finding): the real engine's own native-binding layer
// does NOT see a Lua `nil` for an omitted argument the way this project's
// stubs do. It reads the call frame's raw stack slot directly, bypassing
// any `lua_gettop()`-style count check - so a first missing argument reads
// whatever stale value was left on that slot from an earlier, unrelated
// call, and a second missing argument can re-read an EARLIER REAL argument
// of the SAME call (the slots are not independently stale; they alias the
// call's own argument block). This is a real, CONFIRMED quirk of the
// shipped engine's own calling convention - not a property of Lua itself,
// and not something this project's stubs reproduce or could reproduce
// faithfully: every stub here is a genuine Lua 5.1 C function, called
// through Lua 5.1's own real, standard C API (the bundled `lua51`, not a
// custom reimplementation) - `lua_tolstring`/`lua_tonumber`/`lua_toboolean`
// etc. on an index past `lua_gettop()` already have Lua 5.1's own well-
// defined "none" behavior (null/0/false), which is NOT the same value a
// stale VM register would hold. Reproducing the real quirk exactly would
// mean tracking what the PREVIOUS unrelated native call happened to leave
// in the interpreter's own register file - chaotic, call-order-dependent
// state this project has no model of and no way to compute correctly
// (fabricating a plausible-looking "stale" value would be exactly the kind
// of invented data this project's whole OpenValue discipline exists to
// refuse).
//
// CHOSEN (not a spec fact, an explicit, documented engineering call): every
// stub below keeps using Lua 5.1's own standard semantics for a missing
// argument (via `argString`/`optionalBoolDefault`/`optionalNumberDefault`
// below, or an equivalent direct `lua_gettop()`/`lua_type()` check) -
// `nil`/empty/0/false, NOT a fabricated "stale" value. This is a KNOWN,
// acknowledged divergence from the real engine for the specific case of a
// script genuinely passing TOO FEW arguments to a native that reads past
// what was given; every other documented CONFIRMED/HYPOTHESIS/CHOSEN
// behavior in this file is unaffected (this only matters for the "reads
// past the real argument count" case, not the ordinary "argument present
// but nil" case, which Lua itself defines identically in both engines).
// Revisit only if a future spec section gives this project an actual way
// to model register staleness (e.g. a confirmed, bounded set of "likely"
// stale values per call site) - inventing one now would not be faithful,
// it would just be a different guess.

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

    EngineState* st = upState(L);
    CharacterState& character = st->getOrCreateCharacter(name);
    // Every value the body branches on is read BEFORE anything is written,
    // so an OPEN one (open_state.h) refuses the call without a partial
    // update. DIAGNOSTIC fallback (false = "not ignoring", CHOSEN) only for
    // a probed/synthetic name - see EngineState::probedFieldOr's own doc
    // comment; a real name's own OPEN gap is unaffected.
    bool wasIgnoring = st->probedFieldOr(character.ignoreAI, name, false);
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
    EngineState* st = upState(L);
    CharacterState& character = st->getOrCreateCharacter(name);
    // DIAGNOSTIC fallback (100, CHOSEN round placeholder) only for a probed/
    // synthetic name - see EngineState::probedFieldOr's own doc comment; a
    // real name's own OPEN gap (no Hit_Points data available) is unaffected.
    lua_pushnumber(L, static_cast<lua_Number>(st->probedFieldOr(character.maxHitPoints, name, 100)));
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

    EngineState* st = upState(L);
    CharacterState& character = st->getOrCreateCharacter(name);
    // CONFIRMED cross-check: same field get_max_hit_points reads; OPEN until
    // set. DIAGNOSTIC fallback (100, CHOSEN) only for a probed/synthetic
    // name - see EngineState::probedFieldOr's own doc comment.
    int32_t cap = st->probedFieldOr(character.maxHitPoints, name, 100);
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
// UPDATE 2026-10-03: the registry now IS populated for real when a host
// loads a parsed .vint_doc (EngineState::loadVintDocument) - see
// tools/lua_host_run.cpp's vint_docs block and Host::
// setDocumentContextResolver for how the current document is chosen per
// call. This stub's own body is unchanged.
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

// Shared nil-gated optional-string idiom (ranking tranche 04's own
// `shop_enable_nearest`/`item_anim_play` - Sec32.5/Sec32.6): absent or an
// explicit nil takes `defaultValue`; anything else goes through
// lua_tolstring (NULL-safe, same shape as argString above).
std::string optionalStringDefault(lua_State* L, int idx, const std::string& defaultValue) {
    if (lua_gettop(L) >= idx && lua_type(L, idx) != LUA_TNIL) return argString(L, idx);
    return defaultValue;
}

// 0x0083dff0 (ranking tranche 04's own `party_add_do`, Sec32.4): a table's
// own reported length - `t.n` if present (a numeric field named "n"), else
// counted via lua_next. -1 for a missing/nil argument (CONFIRMED per
// Sec30.5's own cross-reference to the same helper). This project's own
// portable stand-in for the real helper's exact internals - the real
// function's own body was not independently decompiled this pass.
int64_t tableLength(lua_State* L, int idx) {
    if (lua_type(L, idx) != LUA_TTABLE) return -1;
    lua_getfield(L, idx, "n");
    if (lua_type(L, -1) == LUA_TNUMBER) {
        int64_t n = static_cast<int64_t>(lua_tonumber(L, -1));
        lua_pop(L, 1);
        return n;
    }
    lua_pop(L, 1);
    int64_t count = 0;
    lua_pushnil(L);
    while (lua_next(L, idx) != 0) {
        ++count;
        lua_pop(L, 1);
    }
    return count;
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
// return-arity contract. Not simulated: the Sec9.2 per-type
// property-descriptor tables (now specified for all 13 types by Team A's U1
// pass; using them is a separate follow-up task), the callback-claim
// mechanism, and the data-responder's own final dispatch FUN_00e1e660 -
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

// Which OpenBits32 byte a force-flag double-gate setter below targets -
// Sec30.5's own 3 setters (and vehicle_set_vulnerable, Sec20.1, added
// 2026-10-02 resumed session) all share the identical gate/replicate shape
// but write DIFFERENT real vehicle bytes (+0x1d7c/+0x1d7d packed into
// VehicleState::forceFlags vs. +0x1d7a's own VehicleState::forceFlags1d7a -
// CONFIRMED distinct offsets, Sec3.7/Sec20.1/Sec46.1) - a function pointer
// selects the field, defaulting to the original +0x1d7c/+0x1d7d byte so
// every existing call site below is unchanged.
OpenBits32& vehicleForceFlags1d7c1d7d(VehicleState& v) { return v.forceFlags; }
OpenBits32& vehicleForceFlags1d7aField(VehicleState& v) { return v.forceFlags1d7a; }

// Shared body for the single-vehicle force-flag setters below (Sec30.5,
// Sec20.1): resolve (reuses objectResolves(), Sec29's "one shared global
// name map" - see VehicleState's own doc comment, engine_state.h), then
// apply the double-gate: gate passes -> set the bit directly; gate fails ->
// replicate only (the no-op stand-in for the real opcode-0x46 record), no
// local bit write - CONFIRMED either/or shape (Sec30.5: "either true ->
// write the bit directly; both false -> open ... instead").
void applyVehicleForceFlagSetter(lua_State* L, const char* name, const std::string& vehicleName, bool flag,
                                  uint32_t bitMask, OpenBits32& (*field)(VehicleState&) = vehicleForceFlags1d7c1d7d) {
    EngineState* st = upState(L);
    if (!st->objectResolves().get(vehicleName)) return; // OPEN until set
    VehicleState& vehicle = st->getOrCreateVehicle(vehicleName);
    if (vehicle.forceFlagGatePasses.get()) { // OPEN until set
        field(vehicle).setBits(bitMask, flag ? bitMask : 0u);
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
    if (!v.bodyActive.get()) return 0; // OPEN until set; CONFIRMED gate +0xbd0==1
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
    st->objectResolves().get(name); // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    c.forcedTargetHandle.clear();
    EngineState::replicateStateChange("ai_clear_priority_target", name);
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
    bool resolved = st->objectResolves().get(name); // OPEN until set
    if (resolved) {
        CharacterState& c = st->getOrCreateCharacter(name);
        c.inScriptedCover.set(value);
        EngineState::replicateStateChange("ai_set_in_scripted_cover", name);
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
    bool resolved = st->objectResolves().get(character) && st->objectResolves().get(object); // OPEN until set
    if (resolved) {
        CharacterState& c = st->getOrCreateCharacter(character);
        c.attentionSourceObjectName = object;
        c.attentionMode = value ? 7 : 3; // CONFIRMED mapping
        EngineState::replicateStateChange("ai_pay_attention_to_position", character);
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
    st->objectResolves().get(character); // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(character);
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

// =======================================================================
// Batch 2026-10-02: spec-lua-api-behaviour.md Sec31 ("pause_map_stag_
// current_district_control" - location and "stag mode") and Sec32
// ("ranking tranche 04", 25 previously-unspecced names). 5 names from
// Sec31's own UI pause-map registrar (FUN_007dfae0, see Sec31.1's own
// 9-entry table) plus the 25 gameplay-registrar names of Sec32 - 30 total.
// See engine_state.h's own per-field/per-method doc comments for exactly
// which real global/mechanism each one stands for.
//
// The 0x009df3d0 correction (Sec14.31/Sec32.8): this document's §8.17/
// §14.31 previously read 0x009df3d0 as a "matching" lookup for the local
// player's own roster entry; ranking tranche 04 (§32.1-A.7) corrects this
// to "the first live-roster entry that is NOT the local player" (i.e. the
// remote co-op player), returning null only when every roster entry
// equals the local player. A grep of this project's own existing source
// for "0x009df3d0" (before this batch) found exactly one prior consumer,
// `customization_restore_player_rig` (Sec28.12,
// EngineState::PlayerRig::coopPlayerPresent) - already modeled as a plain
// presence boolean ("is there a co-op player"), never as a name-matching
// lookup, so it needed NO change for this correction. The only NEW
// consumer this batch adds, `satellite_weapon_mode_exit` below, applies
// the corrected reading directly at its own call site: bit 0x2 of its
// selector mask only ever acts on a player when
// playerRig().coopPlayerPresent is true - in single player (no co-op
// session), that bit is a no-op, never a fallback to the local player.
// =======================================================================

// ---------------------------------------------------------------------
// pause_map_stag_current_district_control (Sec31.2, 0x007de0d0). No
// arguments (the argument count is fetched and discarded - CONFIRMED).
// Return: normally 1 number (owned/total weighted fraction for the
// selected zone, or 1.0 if total <= 0 including NaN). CONFIRMED
// exception: when no zone is selected (0x0229a2ac reads 0), the function
// does NOT return early - it pushes 0 first, then falls through the SAME
// computation a second time (in practice giving 1.0, since no members are
// ever registered under zone handle 0), so it returns 2 numbers: 0, 1.0.
// A reimplementation reproduces this two-value shape faithfully.
// ---------------------------------------------------------------------
int stub_pause_map_stag_current_district_control(lua_State* L) {
    logCall(L, upLog(L), "pause_map_stag_current_district_control", upStateTag(L));
    EngineState* st = upState(L);
    uint32_t zone = st->pauseMapSelectedZone();
    int pushed = 0;
    if (zone == 0) { // CONFIRMED: no early return - pushes 0, then falls through the same computation again
        lua_pushnumber(L, 0.0);
        ++pushed;
    }
    lua_pushnumber(L, st->pauseMapZoneControlFraction(zone));
    ++pushed;
    return pushed;
}

// ---------------------------------------------------------------------
// action_sequence_end (Sec33.3, 0x00a3c270). No arguments, no return.
// CONFIRMED global teardown - see EngineState::actionSequenceEnd's own
// doc comment for the exact fields this project tracks.
// ---------------------------------------------------------------------
int stub_action_sequence_end(lua_State* L) {
    logCall(L, upLog(L), "action_sequence_end", upStateTag(L));
    upState(L)->actionSequenceEnd();
    return 0;
}

// ---------------------------------------------------------------------
// pause_map_is_stag_mode (Sec31.1 table item 4 / Sec31.5, 0x007d98b0). No
// arguments. Return: 1 boolean - the stag-mode flag 0x0229a317 (the SAME
// global game_autosave's own gate reads, Sec27.12/Sec31.5).
// ---------------------------------------------------------------------
int stub_pause_map_is_stag_mode(lua_State* L) {
    logCall(L, upLog(L), "pause_map_is_stag_mode", upStateTag(L));
    lua_pushboolean(L, upState(L)->pauseMapStagMode().get() ? 1 : 0); // OPEN until set
    return 1;
}

// ---------------------------------------------------------------------
// pause_map_is_tutorial_mode (Sec31.1 table item 5 / Sec31.5, 0x007d98e0).
// No arguments. Return: 1 boolean - the tutorial-mode flag 0x0229a318
// (CONFIRMED via Sec31.5's own cross-reference naming this exact getter;
// the setter is Sec32.1's own `pause_map_tutorial_mode` below).
// ---------------------------------------------------------------------
int stub_pause_map_is_tutorial_mode(lua_State* L) {
    logCall(L, upLog(L), "pause_map_is_tutorial_mode", upStateTag(L));
    lua_pushboolean(L, upState(L)->pauseMapTutorialMode().get() ? 1 : 0); // OPEN until set
    return 1;
}

// ---------------------------------------------------------------------
// pause_map_set_gps (Sec31.3, 0x007dba10). CONFIRMED (stag-mode branch
// only - see EngineState::pauseMapSetGpsStagBranch's own doc comment for
// the full gate and for why the non-stag GPS-route argument contract is
// OPEN and not read here). Return: none.
// ---------------------------------------------------------------------
int stub_pause_map_set_gps(lua_State* L) {
    logCall(L, upLog(L), "pause_map_set_gps", upStateTag(L));
    upState(L)->pauseMapSetGpsStagBranch(); // OPEN (stag-mode flag) propagates
    return 0;
}

// ---------------------------------------------------------------------
// audio_set_listener_override (Sec33.4, 0x00a3dc70) / audio_clear_
// listener_override (Sec33.4, 0x00a3c860). Set: 1 object-name string
// (general object resolver, requiring one specific descriptor bit -
// HYPOTHESIS "human/character" kind, not modeled further); clear: none.
// Both CONFIRMED: always store an id and always send a record, no
// authority gate (clear sends the same record with id 0). The per-frame
// position/orientation snapshot and listener substitution are NOT
// modeled (no position/camera data anywhere in this project).
// ---------------------------------------------------------------------
int stub_audio_set_listener_override(lua_State* L) {
    logCall(L, upLog(L), "audio_set_listener_override", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    st->objectResolves().get(name); // OPEN until set
    st->audioListenerOverrideTarget() = name;
    EngineState::replicateStateChange("audio_set_listener_override", name); // CONFIRMED: always sends an opcode-0x45 record
    return 0;
}

int stub_audio_clear_listener_override(lua_State* L) {
    logCall(L, upLog(L), "audio_clear_listener_override", upStateTag(L));
    EngineState* st = upState(L);
    st->audioListenerOverrideTarget().clear();
    EngineState::replicateStateChange("audio_clear_listener_override", ""); // CONFIRMED: sends the same record with id 0
    return 0;
}

// ---------------------------------------------------------------------
// audio_play_for_navpoint (Sec33.4, 0x00a3edf0). 1 sound/event-name
// string + 1 object-name string (third-tier by-name resolver) + 1
// optional string that is read and discarded, never used (the same
// "read but inert" pattern already documented elsewhere in this
// project). Returns 1 number: the playing instance's id, or 0 on
// failure. This project has no real Wwise event table to resolve the
// sound name against - the handle is produced the same way game_UI_
// audio_play's own stand-in does (see its doc comment), regardless of
// the sound name's own content.
// ---------------------------------------------------------------------
int stub_audio_play_for_navpoint(lua_State* L) {
    logCall(L, upLog(L), "audio_play_for_navpoint", upStateTag(L));
    (void)argString(L, 1); // sound/event name - no real Wwise table to resolve against (see doc comment)
    std::string object = argString(L, 2);
    if (lua_gettop(L) >= 3 && lua_type(L, 3) != LUA_TNIL) (void)argString(L, 3); // CONFIRMED read but inert
    EngineState* st = upState(L);
    double id = 0.0;
    if (st->objectResolves().get(object)) { // OPEN until set
        id = static_cast<double>(st->nextAudioVoiceHandle());
    }
    lua_pushnumber(L, id);
    return 1;
}

// ---------------------------------------------------------------------
// pause_map_stag_takeover_do_reward (Sec31.6, 0x007de1c0). No arguments.
// Return: none (CONFIRMED, every path). See
// EngineState::pauseMapStagTakeover's own doc comment for the full
// claim/clear-stag-mode/request-autosave mechanism and for why the real
// engine's own unchecked null-dereference (no zone selected) is refused
// safely here rather than reproduced.
// ---------------------------------------------------------------------
int stub_pause_map_stag_takeover_do_reward(lua_State* L) {
    logCall(L, upLog(L), "pause_map_stag_takeover_do_reward", upStateTag(L));
    upState(L)->pauseMapStagTakeover();
    return 0;
}

// =======================================================================
// Ranking tranche 04 (Sec32), 25 names, gameplay registrar 0x00a20840.
// =======================================================================

// ---------------------------------------------------------------------
// store_interface_is_active (Sec32.1, 0x00a5def0). No arguments. Return:
// 1 boolean - bit 0x1 of byte global 0x022cce86 (runtime-only; OPEN
// writer).
// ---------------------------------------------------------------------
int stub_store_interface_is_active(lua_State* L) {
    logCall(L, upLog(L), "store_interface_is_active", upStateTag(L));
    lua_pushboolean(L, upState(L)->storeInterfaceActive().get() ? 1 : 0); // OPEN until set
    return 1;
}

// ---------------------------------------------------------------------
// spawn_region_max_spawn_dist (Sec32.1, 0x00a5dbf0). 1 number (no nil-
// gate; a non-convertible argument reads as 0.0 via lua_tonumber's own
// real semantics). No return. CONFIRMED: narrows to float, squares it
// (double, narrowed back) into the global - no range check (negative
// squares positive, 0 makes the cap 0).
// ---------------------------------------------------------------------
int stub_spawn_region_max_spawn_dist(lua_State* L) {
    logCall(L, upLog(L), "spawn_region_max_spawn_dist", upStateTag(L));
    double v = lua_tonumber(L, 1);
    float f = static_cast<float>(v);
    upState(L)->setSpawnRegionMaxSpawnDistSquared(f * f);
    return 0;
}

// ---------------------------------------------------------------------
// spawn_region_max_spawn_dist_reset (Sec32.1, 0x00a5dc20). No arguments,
// no return. CONFIRMED: resets the squared cap back to FLT_MAX ("no
// limit") - the sibling setter's own static default.
// ---------------------------------------------------------------------
int stub_spawn_region_max_spawn_dist_reset(lua_State* L) {
    logCall(L, upLog(L), "spawn_region_max_spawn_dist_reset", upStateTag(L));
    upState(L)->resetSpawnRegionMaxSpawnDist();
    return 0;
}

// ---------------------------------------------------------------------
// set_ped_override_density (Sec32.1, 0x00a5d1f0). 1 number, no nil-gate.
// No return. CONFIRMED: value > 0 stores it only if <= 1.0 (above 1.0
// silently ignored, prior value kept); value <= 0 stores -1.0 ("no
// override"). A NaN argument takes the >0 branch (the real test is
// structurally "<=0 ? low : high", so NaN - which fails every ordered
// comparison - falls to the high/">0" branch) then fails the <=1.0 test,
// a no-op - reproduced here via the SAME `!(v <= 0.0)` structure, not a
// literal `v > 0.0` (which would misclassify NaN).
// ---------------------------------------------------------------------
int stub_set_ped_override_density(lua_State* L) {
    logCall(L, upLog(L), "set_ped_override_density", upStateTag(L));
    double v = lua_tonumber(L, 1);
    EngineState* st = upState(L);
    if (!(v <= 0.0)) { // CONFIRMED structure: "> 0" branch, including NaN
        if (v <= 1.0) st->setPedOverrideDensity(static_cast<float>(v));
        // else: silently ignored, prior value kept (CONFIRMED)
    } else {
        st->setPedOverrideDensity(-1.0f);
    }
    return 0;
}

// =======================================================================
// Batch 2026-10-02: spec-lua-api-behaviour.md Sec38 ("ranking tranche
// 06"), Sec39 ("ranking tranche 07") and Sec40 ("ranking tranche 08") -
// 75 previously-unspecced Lua-bound names total (25 each), all low-
// traffic by Team A's own static call-count ranking and, independently,
// ZERO real hits in this session's own latest mission-drive trace
// (results/verify_sched_check/verdict_stub_hits_with_missions.tsv,
// checked directly against all 75 names - none appear there at all).
// Per the orchestrator's own explicit scope instruction, this batch
// implements: (1) EVERY mandated crash guard / faithful quirk the
// orchestrator itself flagged, regardless of call count (safety/
// correctness requirements, not optional); (2) a handful of additional
// names bundled only where they share already-built infrastructure at
// negligible marginal cost (Sec30's own "bundle cheap siblings"
// convention). 16 of the combined 75 names are implemented: 4 from
// Sec38, 7 from Sec39, 5 from Sec40 - see each sub-section's own header
// comment below for exactly which and why; the other 59 remain ordinary
// generic logging stubs this pass (not in specConfirmedStubNames()).
// =======================================================================

// -----------------------------------------------------------------------
// Sec38 ("ranking tranche 06") - 4 of its 25 names: the 2 MANDATED crash
// guards (vcust_preview_wheel_sizing, team_make_hostile), plus the 2
// MANDATED faithful quirks (vehicle_engine_check_running, store_weapon_
// purchase_ammo). The other 21 names (the vehicle-enter family and its
// shared 0x44-byte request record, vehicle_hidden/_anim_playing/
// _engine_start/_disable_weapon_physics/_forced_corpse_removal_enabled/
// _delete_all_corpses, tutorial_visible/_get_case, store_weapon_change_
// weapon/_process_post_bg_covered, store_vehicle_change_mode/_retrieve_
// car_and_exit/_selection_should_lock_controls, vcust_preview_color/
// _palette/_purchase_wheels) are zero real hits with no mandated guard/
// quirk and each would need substantial new untested engine-state
// modeling (an occupancy-slot/eligibility-predicate model, a tutorial-
// queue/case table, a store-UI mode/screen-stack model, zone/palette
// lookup tables) - left as generic stubs this pass, same judgment call
// Sec30's own batch made for its own 16 unimplemented names.
// -----------------------------------------------------------------------

// vcust_preview_wheel_sizing (Sec38.5, 0x0081f5d0) - MANDATED crash guard
// #1. 2 numbers (truncated to a byte each, CONFIRMED). No return.
// CONFIRMED real defect: "If there is no live customization target, the
// vehicle pointer is zero and the very next instruction reads an offset
// of the null pointer - an unconditional access violation." HOST-SAFETY
// DEVIATION (not a spec fact): this host checks EngineState::
// vcustPreview().targetLive (CHOSEN default false - see its own doc
// comment, engine_state.h) instead of performing the equivalent null-
// pointer-shaped read, counting the averted path via
// recordVcustPreviewWheelSizingNullTargetGuard() - "this project does not
// simulate crashes" (cf. group_get_next_npc, Sec28.5, the established
// precedent this task's own instructions cite by name). The live-target
// path's actual wheel-sizing write is OPEN (the +0x688 sub-object's exact
// fields are not given beyond the crash shape itself) - not modeled
// further either way.
int stub_vcust_preview_wheel_sizing(lua_State* L) {
    logCall(L, upLog(L), "vcust_preview_wheel_sizing", upStateTag(L));
    (void)truncateEa2596(lua_tonumber(L, 1)); // 2 numbers, truncated to a byte each (CONFIRMED) - not otherwise modeled
    (void)truncateEa2596(lua_tonumber(L, 2));
    EngineState* st = upState(L);
    if (!st->vcustPreview().targetLive) {
        st->recordVcustPreviewWheelSizingNullTargetGuard(); // HOST-SAFETY, see comment above
        logCall(L, upLog(L), "vcust_preview_wheel_sizing:NULL_TARGET_GUARD", upStateTag(L));
        return 0;
    }
    return 0; // live target: no further confirmed effect to apply (OPEN)
}

// team_make_hostile (Sec38.2, 0x00a607a0) - MANDATED crash guard #2. 2
// strings (team names). No return. CONFIRMED: writes relation value 0
// (hostile) symmetrically with the SAME team-5-mirrors-team-6 behavior
// already established for team_make_unfriendly (Sec30.6, directly above -
// reuses that EXACT infrastructure per this task's own explicit
// instruction not to duplicate it: resolveTeamId()/
// setTeamRelationIfInBounds()/recordTeamRelationOobGuard()), then ALWAYS
// broadcasts an opcode-0x43 record with no authority gate of any kind
// (the same "no authority gate" shape team_make_unfriendly's own stub
// above already has - replicateStateChange only ever runs after a
// successful in-bounds write there too, so this is not a structural
// change for this host). Defect 1 (CONFIRMED): "a non-string team-name
// argument reaches a null-pointer dereference inside the name-hash helper
// with no guard" - already avoided by construction in this project:
// argString() (this file's own established NULL-safe lua_tolstring
// wrapper, used here) never passes a raw null C string into
// resolveTeamId(), substituting "" instead, so this specific hazard never
// arises here regardless of the Lua argument's real type - nothing
// further to guard. Defect 2 (CONFIRMED): "an unrecognized team name
// resolves to id 9, which is larger than the matrix's own row stride -
// writing past the intended row ... which then gets replicated to every
// peer." HOST-SAFETY DEVIATION (not a spec fact): both the out-of-bounds
// write AND its subsequent replication are guarded together here, exactly
// as team_make_unfriendly already does (setTeamRelationIfInBounds's own
// early-false return skips the replicateStateChange call below it, same
// shape, per this task's own "guard and log both the write and the
// replication call" instruction).
int stub_team_make_hostile(lua_State* L) {
    logCall(L, upLog(L), "team_make_hostile", upStateTag(L));
    std::string team1 = argString(L, 1);
    std::string team2 = argString(L, 2);
    EngineState* st = upState(L);
    int id1 = st->resolveTeamId(team1);
    int id2 = st->resolveTeamId(team2);
    if (!st->setTeamRelationIfInBounds(id1, id2, 0)) { // value 0 = hostile (CONFIRMED)
        st->recordTeamRelationOobGuard(); // HOST-SAFETY: shared Sec30.6 infra, see comment above
        logCall(L, upLog(L), "team_make_hostile:OOB_TEAM_INDEX_GUARD", upStateTag(L));
        return 0;
    }
    EngineState::replicateStateChange("team_make_hostile", team1 + "/" + team2); // always, no authority gate (CONFIRMED)
    return 0;
}

// vehicle_engine_check_running (Sec38.1, 0x00a63e50) - MANDATED faithful
// quirk. 1 string. CONFIRMED return-value defect: "On failure (unresolved,
// or a specific object flag clear) pushes false and returns 1 value; on
// success it calls the engine-running-bit getter, DISCARDS the result,
// and returns 0 values - so this query can never answer true." NOT
// "fixed" here into returning a boolean on success, per this task's own
// explicit instruction - implemented exactly zero-return-always-on-
// success. "A specific object flag clear" is this project's own
// VehicleState::engineCheckGatePasses stand-in (OPEN until set, exact
// real bit not given in scope - see its own doc comment).
int stub_vehicle_engine_check_running(lua_State* L) {
    logCall(L, upLog(L), "vehicle_engine_check_running", upStateTag(L));
    std::string vehicleName = argString(L, 1);
    EngineState* st = upState(L);
    bool ok = st->objectResolves().get(vehicleName) &&                            // OPEN until set
              st->getOrCreateVehicle(vehicleName).engineCheckGatePasses.get();     // OPEN until set
    if (!ok) {
        lua_pushboolean(L, 0);
        return 1;
    }
    return 0; // CONFIRMED: zero Lua values on success - this query can never answer true (Sec38.1)
}

// store_weapon_purchase_ammo (Sec38.3, 0x008165c0) - MANDATED faithful
// quirk. Up to 4 numeric arguments (slot/category, item id, amount,
// price - CONFIRMED count/order; no nil-gate given beyond "up to 4", read
// here via lua_tonumber with no gate, matching this project's own
// established "no gate, absent reads 0" idiom used throughout this file
// for similarly under-specified numeric args). No return. CONFIRMED
// quirk: "charging the price and recording the purchase even on failure:
// an empty or non-matching store-list scan still charges the player and
// records an unrelated/garbage item ... No affordability check in this
// function at all." This project has no real weapon-store item/listing
// table anywhere in scope, so the item lookup always takes the real,
// CONFIRMED failure branch here - the SAME "never-populated registry is
// the honest default" precedent as EngineState::resolveTeamId()/
// findVdoObject() - not a simplification of the charge/record contract
// itself (there is none to simplify: both happen unconditionally either
// way). `tag` matches the real notification mechanism's own literal
// string (Sec38.3).
int stub_store_weapon_purchase_ammo(lua_State* L) {
    logCall(L, upLog(L), "store_weapon_purchase_ammo", upStateTag(L));
    // arg 1 (slot/category) / arg 2 (item id) / arg 3 (amount): not otherwise
    // modeled - this host has no real store-list/inventory-slot table (see
    // comment above); only arg 4 (price) feeds an observable effect.
    double price = lua_gettop(L) >= 4 ? lua_tonumber(L, 4) : 0.0;
    EngineState* st = upState(L);
    st->playerCash() -= price; // CONFIRMED: no affordability check anywhere in this function
    st->purchaseLedger().push_back({"weapon-ammo", price, false});
    return 0;
}

// -----------------------------------------------------------------------
// Sec39 ("ranking tranche 07") - 7 of its 25 names: the 4 MANDATED new-
// defect guards (skydive_move_to_do, store_gang_show_question_marks,
// store_gallery_download_hide_list, store_common_rotate_mouse_drag) plus
// save_system_save_game/_load_game (the MANDATED off-by-one faithful
// quirk) and save_system_cancel_coop_load, bundled alongside them at
// negligible marginal cost once the shared SaveSystemUi singleton exists
// (same "bundle cheap siblings" convention as Sec30's own
// vehicle_clear_all_radio_locks addition). The other 18 names
// (rappel_enter/_exit, set_cower_variant/_unrecruitable_flag/
// _unjackable_by_ai_flag, rc_set_max_signal_range, spotlight_heli_
// laser_size, roadblock_create/_destroy, the 5 spawning/world-state
// globals, store_dlc_queue_icon/_is_offer_free, store_clothing_get_
// store_id, store_character_lineup_loaded) are zero real hits with no
// mandated guard/quirk; roadblock_destroy specifically needed its own
// investigation - see this file's own note just below the Sec39 batch
// for why it still stays a generic stub. The rest each need their own
// substantial new engine-state modeling (a rappel queue/attached-object
// model, a road-network/template table, region/world-object resolution)
// - left as generic stubs this pass, same judgment call as Sec38 above.
//
// Correction investigated and CONFIRMED ALREADY CORRECT in this codebase
// (no code change needed): Sec39.6 states "0x00ea2596 (this document's
// established truncating float-to-int cast) is now confirmed to actually
// return a full 64-bit value (EDX:EAX) - most callers only ever use the
// low half, but roadblock_destroy is a real, confirmed instance that
// needs both." This project's existing truncateEa2596() (above in this
// file, Sec4.1) was checked directly: it already returns int64_t (the
// FULL truncated 64-bit value, via std::trunc on the real double range
// [-2^63, 2^63) before a single static_cast<int64_t>), and every existing
// call site already stores that full int64_t (none narrow it to int32) -
// so this project's shared helper was never 32-bit-only and needs no
// widening. roadblock_destroy itself is NOT implemented this pass
// regardless (zero real calls, and the real per-half meaning of
// EDX/EAX - which bits of the 64-bit value the real engine's "roadblock-
// kind descriptor bit" check and "generic object-release routine" each
// read - is not given in scope beyond the crash-shaped-correction fact
// itself; implementing it would mean inventing that split rather than
// widening an existing model). roadblock_create (the paired handle
// producer) would need a road-network-manager/template-table model this
// project also does not have - both stay generic stubs, a stated gap,
// not a missed correction.
// -----------------------------------------------------------------------

// skydive_move_to_do (Sec39.1, 0x00a5f780) - MANDATED guard (a new
// original-game defect; guard/log, do NOT reproduce as a crash). 6
// arguments (character, target name, a bare "use-path" boolean, then 3
// optional: point index, 2 booleans - CONFIRMED count/order; the 3
// optional arguments have no further modeled effect in this host, see
// below). No return. CONFIRMED real defect: "With the flag false, the
// target resolves through the generic third-tier resolver with NO guard
// on failure: an absent/unresolved name leaves the target pointer at
// literal zero, and the function then unconditionally copies 12 bytes
// from address 0x40 into the character - a genuine access violation."
// HOST-SAFETY DEVIATION (not a spec fact): this host skips the equivalent
// copy on an explicitly-known-unresolved non-path target instead of
// performing it, counting the averted path via
// recordSkydiveMoveToNullTargetGuard() - "this project does not simulate
// crashes" (cf. group_get_next_npc, Sec28.5). Uses objectResolves().
// known()+get() rather than a bare get() so an EXPLICITLY-registered
// "does not resolve" name (the real shape of the confirmed defect) takes
// the guarded path directly, rather than a merely-never-registered name
// throwing OPEN_STATE first - the same "known false, not just never
// known" distinction this project's resolveTeamId() sentinel already
// relies on for an analogous guard. The use-path-true branch (a dedicated
// path-object resolver + bounds-checked 12-byte point array read) and
// the success path's own action-state-0x1f switch (0x004dd380)/move-to-
// target field copy are OPEN - not modeled either way (the 3-further-
// gate reproducibility condition Sec39.1 separately describes is about
// the REAL crash's reachability, not about whether this host's own guard
// should apply).
int stub_skydive_move_to_do(lua_State* L) {
    logCall(L, upLog(L), "skydive_move_to_do", upStateTag(L));
    std::string character = argString(L, 1);
    (void)character; // no further confirmed effect modeled in this host either way (see comment above)
    std::string targetName = argString(L, 2);
    bool usePath = lua_toboolean(L, 3) != 0; // bare boolean (CONFIRMED)
    EngineState* st = upState(L);
    if (!usePath) {
        bool targetKnownResolved = st->objectResolves().known(targetName) && st->objectResolves().get(targetName);
        if (!targetKnownResolved) {
            st->recordSkydiveMoveToNullTargetGuard(); // HOST-SAFETY, see comment above
            logCall(L, upLog(L), "skydive_move_to_do:NULL_TARGET_GUARD", upStateTag(L));
            return 0;
        }
    }
    // use-path true, or a confirmed-resolved target: no further confirmed
    // effect modeled (action-state switch / point-array read / move-to
    // field copy all OPEN, see comment above).
    return 0;
}

// store_gang_show_question_marks (Sec39.5, 0x00811170) - MANDATED guard
// (a new original-game defect; guard/log, do NOT reproduce as a crash).
// Bare boolean. No return. CONFIRMED real defect: "If the slot's own
// asset reference is unset AND the fallback 'question mark' asset lookup
// also fails ... the code takes the 'not a question mark' branch anyway
// and dereferences the resulting null pointer." HOST-SAFETY DEVIATION (not
// a spec fact): this host checks EngineState::storePreviewGuards().
// gangPreviewAssetOrFallbackResolved (CHOSEN default false - see its own
// doc comment) instead of performing the equivalent null-pointer-shaped
// read, counting the averted path via gangQuestionMarksNullGuardCount -
// "this project does not simulate crashes" (cf. group_get_next_npc,
// Sec28.5). The slot-index bounds-check hazard the spec separately flags
// as HYPOTHESIS risk (not independently confirmed reachable) is not
// modeled either way - this host's own minimal model takes no slot-index
// argument (no real fixed-size slot table in scope).
int stub_store_gang_show_question_marks(lua_State* L) {
    logCall(L, upLog(L), "store_gang_show_question_marks", upStateTag(L));
    bool wantQuestionMarks = lua_toboolean(L, 1) != 0; // bare boolean (CONFIRMED)
    EngineState* st = upState(L);
    auto& guards = st->storePreviewGuards();
    if (!guards.gangPreviewAssetOrFallbackResolved) {
        ++guards.gangQuestionMarksNullGuardCount; // HOST-SAFETY, see comment above
        logCall(L, upLog(L), "store_gang_show_question_marks:NULL_ASSET_GUARD", upStateTag(L));
        return 0;
    }
    guards.gangPreviewShowingQuestionMarks = wantQuestionMarks; // CHOSEN write-only toggle, this project's own stand-in
    return 0;
}

// store_gallery_download_hide_list (Sec39.5, 0x0080f1a0) - MANDATED guard
// (a new original-game defect; guard/log, do NOT reproduce as a crash).
// Bare boolean. No return. CONFIRMED real defect: "The 'true' (hide) path
// clears a large block of a global gallery-list object AND reads a field
// off the local player - neither the gallery object's own global pointer
// nor the local-player pointer is null-checked anywhere in this
// function." HOST-SAFETY DEVIATION (not a spec fact): this host checks
// EngineState::storePreviewGuards().galleryListObjectResolved (CHOSEN
// default false) AND hasLocalPlayer() (reused - the SAME "player 1
// exists" concept cellphone_animate_start_do already reuses it for,
// Sec28.23) before applying the hide path, instead of performing the
// equivalent unguarded dereferences, counting the averted path via
// galleryHideListNullGuardCount. The spec gives no detail for the "false"
// (show) path beyond the hazard being specific to "true" - not modeled
// (a plain no-op; honestly nothing confirmed to do there).
int stub_store_gallery_download_hide_list(lua_State* L) {
    logCall(L, upLog(L), "store_gallery_download_hide_list", upStateTag(L));
    bool hide = lua_toboolean(L, 1) != 0; // bare boolean (CONFIRMED)
    if (!hide) return 0; // the hazard (and all described effect) is specific to the "true" path
    EngineState* st = upState(L);
    auto& guards = st->storePreviewGuards();
    if (!guards.galleryListObjectResolved) {
        ++guards.galleryHideListNullGuardCount; // HOST-SAFETY, see comment above
        logCall(L, upLog(L), "store_gallery_download_hide_list:NULL_PTR_GUARD", upStateTag(L));
        return 0;
    }
    if (!st->hasLocalPlayer().get()) { // OPEN until set - reused, see comment above
        ++guards.galleryHideListNullGuardCount;
        logCall(L, upLog(L), "store_gallery_download_hide_list:NULL_PTR_GUARD", upStateTag(L));
        return 0;
    }
    return 0; // both resolved: no further confirmed effect to apply beyond the clear itself (OPEN detail)
}

// store_common_rotate_mouse_drag (Sec39.5, 0x0080dc10) - MANDATED CHOSEN
// host-safety stand-in. No arguments, no return. CONFIRMED: "reads mouse-
// delta values from three fixed stack slots, but only copies the real
// values in when a specific input-mode bit is set ... when clear, the
// three stack slots are never initialized at all... A faithful
// reimplementation must treat 'mouse delta unavailable' as zero, not skip
// the rotation or read whatever happens to be on the stack." CHOSEN (not
// a spec fact, per this task's own explicit instruction): this project
// has no real mouse-delta-capture layer anywhere in scope (modeled
// nowhere else in this codebase either), so BOTH the "bit clear" case
// (real uninitialized memory) and the "bit set" case (a real value this
// host has no source for) land on the same explicit, defined 0 rather
// than raw/undefined C++ memory or an invented nonzero value. The actual
// rotation target selection (gang-customization preview / store object /
// fallback local player with no null check) is not modeled - with delta
// 0 there is no further observable effect to apply either way, and this
// project does not track vehicle/player orientation at this granularity.
int stub_store_common_rotate_mouse_drag(lua_State* L) {
    logCall(L, upLog(L), "store_common_rotate_mouse_drag", upStateTag(L));
    EngineState* st = upState(L);
    auto& drag = st->storeCommonRotateMouseDrag();
    drag.lastDeltaX = 0.0; // CHOSEN, see comment above
    drag.lastDeltaY = 0.0;
    drag.lastDeltaZ = 0.0;
    ++drag.callCount;
    return 0;
}

// save_system_save_game (Sec39.4, 0x007d0dd0) - MANDATED faithful quirk.
// 1 number (slot, truncated via 0x00ea2596/truncateEa2596 - CONFIRMED).
// No return. Slot 99 is the special "new save" sentinel (CONFIRMED,
// described as save-specific - "space/slot-count checks, then commits")
// - internals OPEN, not modeled further. CONFIRMED real defect (HIGH
// CONFIDENCE, sibling-confirmed per spec): "both the save-confirm
// callback and the load path bound the slot with slot > count / slot <=
// count, accepting slot == count - one past the end of the slot table."
// This host allows that exact off-by-one (slot == count is treated the
// SAME as slot < count - both "accepted") rather than tightening the
// bound to slot < count, and logs when it fires via saveOffByOneCount.
// The confirmation-dialog/actual record-write internals are OPEN - not
// modeled.
int stub_save_system_save_game(lua_State* L) {
    logCall(L, upLog(L), "save_system_save_game", upStateTag(L));
    int64_t slot = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    auto& ui = st->saveSystemUi();
    ++ui.saveCallCount;
    if (slot == 99) return 0; // CONFIRMED "new save" sentinel - internals OPEN
    uint32_t count = ui.slotCount.get(); // OPEN until set
    if (slot == static_cast<int64_t>(count)) {
        ++ui.saveOffByOneCount; // CONFIRMED-adjacent real defect: slot==count accepted (<=, not <)
        logCall(L, upLog(L), "save_system_save_game:OFF_BY_ONE_SLOT", upStateTag(L));
    }
    // slot < count (ordinary accept) or slot == count (off-by-one, logged
    // above): both "accepted" by the real <= bound; slot > count (and
    // slot != 99) is rejected either way - no further effect to apply
    // (confirmation-dialog/record-write internals OPEN).
    return 0;
}

// save_system_load_game (Sec39.4, 0x007d0e60) - MANDATED faithful quirk,
// the load-path sibling of save_system_save_game above. 1 number (slot,
// truncated). No return. Shares the SAME confirmed off-by-one bound
// (slot == count accepted) - logged via loadOffByOneCount. The slot-99
// "new save" sentinel is NOT modeled here (its own text frames it as a
// save-specific "new save" flow, Sec39.4) - a load call with slot 99
// takes the ordinary bound check like any other value. The separate
// HYPOTHESIS "stale pending-index" deferred-load crash path (reachability
// not settled per spec) is OPEN - not modeled, a stated gap, not invented.
int stub_save_system_load_game(lua_State* L) {
    logCall(L, upLog(L), "save_system_load_game", upStateTag(L));
    int64_t slot = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    auto& ui = st->saveSystemUi();
    ++ui.loadCallCount;
    uint32_t count = ui.slotCount.get(); // OPEN until set
    if (slot == static_cast<int64_t>(count)) {
        ++ui.loadOffByOneCount; // CONFIRMED-adjacent real defect: slot==count accepted (<=, not <)
        logCall(L, upLog(L), "save_system_load_game:OFF_BY_ONE_SLOT", upStateTag(L));
    }
    return 0;
}

// save_system_cancel_coop_load (Sec39.4, 0x007d0e90) - bundled alongside
// the two above at negligible marginal cost (same shared SaveSystemUi
// singleton). No arguments, no return. CONFIRMED: "Sets the save-system's
// own state field to 5 unless already there." HYPOTHESIS: 5 = "co-op load
// cancelled" - not otherwise modeled (this project has no save-system
// state MACHINE beyond this one field, so "unless already there" and
// "set it to 5" produce the identical observable result either way).
int stub_save_system_cancel_coop_load(lua_State* L) {
    logCall(L, upLog(L), "save_system_cancel_coop_load", upStateTag(L));
    upState(L)->saveSystemUi().coopLoadState = 5; // CONFIRMED value (Sec39.4)
    return 0;
}

// -----------------------------------------------------------------------
// Sec40 ("ranking tranche 08") - 5 of its 25 names, all from the 15-entry
// PCU ("clothing store") cluster (Sec40.5/Sec40.6). That cluster's own
// write-up is EXPLICITLY condensed from a fuller interp note not included
// in this spec file ("full detail in the interp note, condensed here",
// Sec40.5), so the exact per-function Lua argument count/order for most
// of the 15 entries is not given at a level this project can implement
// without inventing beyond confirmed text - a genuine decision-fork, not
// an empty backlog, so it is documented here and deferred rather than
// guessed (per this project's own standing "no invented fixes" policy).
// This batch implements only the 5 names whose mandated guard/quirk and
// argument shape ARE given precisely enough in the condensed text itself:
// pcu_is_bra_category/pcu_is_underwear_category (1 number, 1 boolean -
// simple enough to infer with no further invention) and pcu_purchase_
// slot/pcu_purchase_outfit/pcu_wear_store_outfit (the 3 functions this
// task's own mandates name explicitly - a 2nd "price" argument for the
// two purchase entries is this project's own CHOSEN, clearly labelled
// shape, mirroring this document's own established purchase-function
// sibling pattern, see each stub's own doc comment below). The other 10
// PCU names (pcu_get_num_items_owned chief among them - its exact RETURN
// semantics beyond the bit-0-only indexing MECHANISM are not given in
// this condensed section, and inventing "returns a count of X" would
// fabricate behavior the spec text does not state) and all 10 non-PCU
// Sec40 names (the 6 sprint-override globals, radio_set_sing_along_
// allowed_during_missions/_get_station, projectile_fire_from_navpoint,
// players_in_weird_camera_mode - none mandated, zero real hits, each
// needing its own new engine-state modeling) stay ordinary generic
// logging stubs this pass.
// -----------------------------------------------------------------------

// Shared category-lookup body for pcu_is_bra_category/pcu_is_underwear_
// category (Sec40.5). 1 number (category index). Returns 1 boolean.
// CONFIRMED crash-shaped defect: "dereference a null category record when
// the index is out of range." HOST-SAFETY DEVIATION (not a spec fact):
// this project's own minimal category table (EngineState::
// pcuCategoryTable(), a test populates each known index's own "kind" tag
// directly - no real category table in scope) treats any index a test
// never populated the SAME as a real out-of-bounds index, guarding it
// instead of performing the equivalent null-pointer-shaped read.
bool pcuCategoryIsKind(lua_State* L, EngineState* st, const char* fnName, int32_t wantKind) {
    int64_t idx = truncateEa2596(lua_tonumber(L, 1));
    auto& table = st->pcuCategoryTable();
    std::string key = std::to_string(idx);
    if (idx < 0 || !table.kindByIndex.known(key)) {
        ++table.categoryIndexOobGuardCount; // HOST-SAFETY, see comment above
        std::string tag = std::string(fnName) + ":CATEGORY_OOB_GUARD";
        logCall(L, upLog(L), tag.c_str(), upStateTag(L));
        return false; // CHOSEN: false on a guarded out-of-range index - no real "kind" to report
    }
    return table.kindByIndex.get(key) == wantKind;
}

int stub_pcu_is_bra_category(lua_State* L) {
    logCall(L, upLog(L), "pcu_is_bra_category", upStateTag(L));
    bool isBra = pcuCategoryIsKind(L, upState(L), "pcu_is_bra_category", EngineState::PcuCategoryTable::kBra);
    lua_pushboolean(L, isBra ? 1 : 0);
    return 1;
}

int stub_pcu_is_underwear_category(lua_State* L) {
    logCall(L, upLog(L), "pcu_is_underwear_category", upStateTag(L));
    bool isUnderwear =
        pcuCategoryIsKind(L, upState(L), "pcu_is_underwear_category", EngineState::PcuCategoryTable::kUnderwear);
    lua_pushboolean(L, isUnderwear ? 1 : 0);
    return 1;
}

// Shared catalog-outfit index resolution for pcu_wear_store_outfit/
// pcu_purchase_outfit below (Sec40.5/Sec40.6) - see EngineState::
// PcuCatalogOutfits' own doc comment (engine_state.h) for the full "two
// independent counting rules, deliberately not unified" reasoning and the
// "excluded bit read as bit 0" interpretive note. Scans test-populated
// list positions 0.. in order (this project's own minimal stand-in for
// "the end of the real catalog list" - a test marks the list's real
// extent simply by not populating past it, the SAME idiom
// readCharacterNameArray above already uses for "the first nil ends the
// array"), counting only positions whose raw flag word satisfies the
// requested rule, and returns the list POSITION of the `wantIndex`-th
// (0-based) qualifying node, or -1 if fewer than `wantIndex`+1 qualify.
int resolvePcuCatalogOutfitIndex(EngineState::PcuCatalogOutfits& catalog, int64_t wantIndex,
                                  bool countByAnyBitExcludingBit0) {
    int64_t seen = 0;
    for (int pos = 0; pos < 100000; ++pos) {
        std::string key = std::to_string(pos);
        if (!catalog.flagsByIndex.known(key)) break;
        uint32_t flags = catalog.flagsByIndex.get(key);
        bool qualifies = countByAnyBitExcludingBit0 ? ((flags & ~0x1u) != 0) : ((flags & 0x1u) != 0);
        if (qualifies) {
            if (seen == wantIndex) return pos;
            ++seen;
        }
    }
    return -1;
}

// pcu_wear_store_outfit (Sec40.5/Sec40.6) - MANDATED "implement each
// function's own rule separately": numbers catalog-outfit nodes by
// testing "any non-zero flag bit, EXCLUDING one specific bit" (read here
// as bit 0 - see resolvePcuCatalogOutfitIndex's own doc comment). 1
// number (catalog-outfit index - CONFIRMED kind of index for this entry,
// Sec40.5/Sec40.6; exact further argument count beyond this is OPEN, see
// this batch's own header comment on the condensed write-up). No return
// (an apply-style action, same shape as every other setter in this
// document). The actual apply step (the shared outfit-apply routine
// 0x00825cf0, which "mutates its source outfit record's own piece
// ordering in place") is OPEN - not modeled; this host instead records
// the resolved list position as a test-observable result
// (EngineState::pcuInventory().lastWornCatalogPosition), -1 if the index
// doesn't resolve to any qualifying node (OPEN: not confirmed whether the
// real engine has its own distinct not-found behavior here).
int stub_pcu_wear_store_outfit(lua_State* L) {
    logCall(L, upLog(L), "pcu_wear_store_outfit", upStateTag(L));
    int64_t idx = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    int pos = resolvePcuCatalogOutfitIndex(st->pcuCatalogOutfits(), idx, /*countByAnyBitExcludingBit0=*/true);
    st->pcuInventory().lastWornCatalogPosition = pos; // CHOSEN test-observable result, see comment above
    return 0;
}

// pcu_purchase_outfit (Sec40.5/Sec40.6) - MANDATED faithful quirk AND
// MANDATED "implement each function's own rule separately": numbers the
// SAME catalog-outfit list by testing "bit 0 only" - a DIFFERENT rule
// from pcu_wear_store_outfit above, deliberately not unified (the same
// raw index can therefore resolve to a DIFFERENT list position here than
// it does there - a CONFIRMED real indexing inconsistency, directly
// exercised by this pair's own tests). 1 number (catalog-outfit index) +
// an implicit price (CHOSEN 2nd-argument shape - the condensed write-up
// gives the counting rule and the charge-regardless quirk precisely but
// not this function's own full argument list, see this batch's own
// header comment; a 2nd numeric "price" argument mirrors this document's
// own established sibling shape, e.g. vcust_purchase_wheels's "1 number
// (price)", Sec38.5). CONFIRMED real logic defect (the mandated quirk):
// "ignores the inventory-append function's 'full' return value - a
// player who is already at capacity (128 outfits) is charged the full
// price and receives nothing silently." Reproduced here exactly: the
// charge always happens; the saved-outfit append is skipped (and logged)
// once at capacity, never rejected up front. The additional "drops
// individual outfit pieces past the 2048-item cap one at a time" nuance
// needs an outfit-to-pieces association this project does not have (no
// real item/outfit catalog beyond the raw flag words above) - not
// modeled, a stated gap, not invented.
int stub_pcu_purchase_outfit(lua_State* L) {
    logCall(L, upLog(L), "pcu_purchase_outfit", upStateTag(L));
    int64_t idx = truncateEa2596(lua_tonumber(L, 1));
    double price = lua_gettop(L) >= 2 ? lua_tonumber(L, 2) : 0.0; // CHOSEN 2nd-arg price, see comment above
    EngineState* st = upState(L);
    int pos = resolvePcuCatalogOutfitIndex(st->pcuCatalogOutfits(), idx, /*countByAnyBitExcludingBit0=*/false);
    auto& inv = st->pcuInventory();
    inv.lastPurchasedOutfitCatalogPosition = pos; // resolved per this function's OWN rule - see its own doc comment
                                                   // (PcuInventory, engine_state.h) for why this is a separate field
                                                   // from pcu_wear_store_outfit's lastWornCatalogPosition above
    inv.cashBalance -= price; // CONFIRMED: charged regardless of append outcome
    if (inv.savedOutfitCount >= EngineState::PcuInventory::kSavedOutfitCapacity) {
        ++inv.purchaseOutfitChargedWhileFullCount; // CONFIRMED quirk: charged even though the append is refused
        logCall(L, upLog(L), "pcu_purchase_outfit:CHARGED_WHILE_FULL", upStateTag(L));
    } else {
        ++inv.savedOutfitCount;
    }
    return 0;
}

// ---------------------------------------------------------------------
// pause_map_tutorial_mode (Sec32.1, 0x00a594e0). 1 boolean, no nil-gate
// (absent/nil -> false, lua_toboolean's own real semantics). No return.
// CONFIRMED: writes the pause-map tutorial-mode flag 0x0229a318 (the SAME
// global Sec31.1 table item 5's `pause_map_is_tutorial_mode` reads).
// ---------------------------------------------------------------------
int stub_pause_map_tutorial_mode(lua_State* L) {
    logCall(L, upLog(L), "pause_map_tutorial_mode", upStateTag(L));
    bool v = lua_toboolean(L, 1) != 0;
    upState(L)->pauseMapTutorialMode().set(v);
    return 0;
}

// ---------------------------------------------------------------------
// set_time_of_day (Sec32.1, 0x00a5e370). 2 numbers, no nil-gates (hour,
// minute; truncated via 0x00ea2596). No return. See
// EngineState::setTimeOfDay's own doc comment for the CONFIRMED forward-
// only delta arithmetic.
// ---------------------------------------------------------------------
int stub_set_time_of_day(lua_State* L) {
    logCall(L, upLog(L), "set_time_of_day", upStateTag(L));
    int64_t newHour = truncateEa2596(lua_tonumber(L, 1));
    int64_t newMinute = truncateEa2596(lua_tonumber(L, 2));
    upState(L)->setTimeOfDay(newHour, newMinute); // OPEN (current hour/minute) propagates
    return 0;
}

// ---------------------------------------------------------------------
// satellite_weapon_mode_exit (Sec32.1, 0x00a5df20). 1 optional number,
// nil-gated, default 3 - a player-selector bitmask (bit 0x1 = local
// player, bit 0x2 = remote co-op player via 0x009df3d0). No return.
// **The 0x009df3d0 correction applies directly here**: bit 0x2 only
// selects a player when playerRig().coopPlayerPresent is true (CONFIRMED,
// Sec14.31/Sec32.8 - "the first live-roster entry that is NOT the local
// player... returning 0 only when every roster entry equals the local
// player", not a "matching" lookup that could wrongly resolve to the
// local player itself in single player). With no co-op session, bit 0x2
// is therefore a correct no-op, never a fallback onto the local player.
// Local player (bit 0x1) is always resolvable (0x009da4e0, a zero-
// argument getter - no name resolution involved).
// ---------------------------------------------------------------------
int stub_satellite_weapon_mode_exit(lua_State* L) {
    logCall(L, upLog(L), "satellite_weapon_mode_exit", upStateTag(L));
    int64_t mask = truncateEa2596(optionalNumberDefault(L, 1, 3.0));
    EngineState* st = upState(L);
    if (mask & 1) st->satelliteWeaponExit(/*remote=*/false);
    if ((mask & 2) && st->playerRig().coopPlayerPresent.get()) { // OPEN until set; CONFIRMED gate per the 0x009df3d0 correction above
        st->satelliteWeaponExit(/*remote=*/true);
    }
    return 0;
}

// pcu_purchase_slot (Sec40.5) - MANDATED faithful quirk. 1 number (a slot
// id, 0-23 - CONFIRMED kind of index for "several" PCU entries including
// this one, Sec40.5) + an implicit price (CHOSEN 2nd-argument shape, same
// reasoning as pcu_purchase_outfit above). CONFIRMED real logic defect
// (the mandated quirk): "ignores the inventory-append function's 'full'
// return value - a player who is already at capacity (2048 items) is
// charged the full price and receives nothing silently." The function's
// OWN separate crash-shaped defects (an unconditional dereference of a
// global pointer two lines after its own null check, an unchecked
// variant-lookup result, an unchecked store-entry variant pointer, a
// 3-slot stack-array colour copy with no clamp of its own) all need a
// store-entry/variant-lookup model this project does not have in scope -
// not modeled, a stated gap, not invented (none of them are this task's
// own mandated guards for THIS function - only the charge-while-full
// quirk is).
int stub_pcu_purchase_slot(lua_State* L) {
    logCall(L, upLog(L), "pcu_purchase_slot", upStateTag(L));
    int64_t slotId = truncateEa2596(lua_tonumber(L, 1)); // CONFIRMED: a slot id (0-23), Sec40.5
    double price = lua_gettop(L) >= 2 ? lua_tonumber(L, 2) : 0.0; // CHOSEN 2nd-arg price, see comment above
    (void)slotId; // not otherwise modeled - no real per-slot item/variant table in scope
    EngineState* st = upState(L);
    auto& inv = st->pcuInventory();
    inv.cashBalance -= price; // CONFIRMED: no affordability check (same shape as store_weapon_purchase_ammo/vcust_purchase_wheels)
    if (inv.ownedItemCount >= EngineState::PcuInventory::kOwnedItemCapacity) {
        ++inv.purchaseSlotChargedWhileFullCount; // CONFIRMED quirk: charged even though the append is refused
        logCall(L, upLog(L), "pcu_purchase_slot:CHARGED_WHILE_FULL", upStateTag(L));
    } else {
        ++inv.ownedItemCount;
    }
    return 0;
}

// ---------------------------------------------------------------------
// set_seatbelt_flag / set_trailing_aim_flag / set_never_turn_on_player
// (Sec32.2). All three: 1 mandatory character name (reuses
// objectResolves(), this project's own shared-resolver simplification), 1
// optional boolean, nil-gated, default true. No return. CONFIRMED
// record-and-replicate shape, collapsed to "always apply the write, call
// the no-op replicate stand-in" per this project's own established
// set_ignore_ai_flag precedent (replicateStateChange's own doc comment).
// ---------------------------------------------------------------------
int stub_set_seatbelt_flag(lua_State* L) {
    logCall(L, upLog(L), "set_seatbelt_flag", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->seatbeltForceFlag()[name] = value;
        EngineState::replicateStateChange("set_seatbelt_flag", name);
    }
    return 0;
}

int stub_set_trailing_aim_flag(lua_State* L) {
    logCall(L, upLog(L), "set_trailing_aim_flag", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->trailingAimForceFlag()[name] = value;
        EngineState::replicateStateChange("set_trailing_aim_flag", name);
    }
    return 0;
}

int stub_set_never_turn_on_player(lua_State* L) {
    logCall(L, upLog(L), "set_never_turn_on_player", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->neverTurnOnPlayerFlag()[name] = value;
        EngineState::replicateStateChange("set_never_turn_on_player", name);
    }
    return 0;
}

// ---------------------------------------------------------------------
// player_revive (Sec32.3, 0x00a599b0). 1 string ("#PLAYER#" accepted), no
// nil-gate, reuses objectResolves(). No return. CONFIRMED: only acts if
// the character's life-state field (+0xcc8) reads 6 ("downed" - the same
// test Sec7.13 `human_is_downed` uses); "always broadcasts first"
// regardless of authority.
// ---------------------------------------------------------------------
int stub_player_revive(lua_State* L) {
    logCall(L, upLog(L), "player_revive", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        CharacterState& c = st->getOrCreateCharacter(name);
        if (c.lifeState.get() == 6) { // OPEN until set; CONFIRMED test value
            ++c.reviveCount;
            EngineState::replicateStateChange("player_revive", name); // CONFIRMED: always broadcasts first
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// player_warp_to_shore_disable (Sec32.3, 0x00a59a90). 1 string
// ("#PLAYER#" accepted), reuses objectResolves(). No return. CONFIRMED:
// always writes true (hardcoded, not a Lua argument) to +0x28ad bit 0x2.
// Passed WITHOUT a null check to the real setter, but that setter itself
// returns at once on a null receiver - a harmless real no-op, matching
// this project's own OPEN-refusal-on-unresolved-name convention (not a
// crash).
// ---------------------------------------------------------------------
int stub_player_warp_to_shore_disable(lua_State* L) {
    logCall(L, upLog(L), "player_warp_to_shore_disable", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->warpToShoreDisabled()[name] = true; // CONFIRMED: always true, hardcoded
        EngineState::replicateStateChange("player_warp_to_shore_disable", name);
    }
    return 0;
}

// ---------------------------------------------------------------------
// skydive_setup_tank_bailout (Sec32.3, 0x00a5e510). 1 number (stage), no
// nil-gate, truncated. No return. A mission-18 set-piece helper: no name
// resolve at all (both vehicles are hardcoded literals). CONFIRMED:
// always broadcasts first regardless of stage or vehicle existence; stage
// 1 arms, stage 2 starts the named anim; any other stage is a local no-op
// (record still sent).
// ---------------------------------------------------------------------
int stub_skydive_setup_tank_bailout(lua_State* L) {
    logCall(L, upLog(L), "skydive_setup_tank_bailout", upStateTag(L));
    int64_t stage = truncateEa2596(lua_tonumber(L, 1));
    auto& s = upState(L)->skydiveTankBailout();
    ++s.broadcastCount; // CONFIRMED: always, every stage
    if (stage == 1) ++s.armedCount;
    else if (stage == 2) ++s.animStartedCount;
    return 0;
}

// ---------------------------------------------------------------------
// qte_human_is_used (Sec32.3, 0x00a5b180). 1 string, reuses
// objectResolves(). Return: 1 boolean, always. CONFIRMED: an unresolved
// character is false (same "unresolved -> false" convention as
// get_char_vehicle_is_in_air above); else see
// EngineState::qteHumanIsUsed's own doc comment.
// ---------------------------------------------------------------------
int stub_qte_human_is_used(lua_State* L) {
    logCall(L, upLog(L), "qte_human_is_used", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool used = false;
    if (st->objectResolves().get(name)) { // OPEN until set
        used = st->qteHumanIsUsed(name);
    }
    lua_pushboolean(L, used ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// audio_any_conversation_playing (Sec33.4, 0x00a3c640). No arguments.
// Returns 1 boolean: true if any session member currently has a valid,
// non-0xff slot on the global "mission_conv" per-session channel
// (CONFIRMED - this settles this document's own previously-OPEN identity
// question for that channel). This project tracks no per-member array
// (see EngineState::conversationChannelActive's own doc comment) -
// collapsed to one flag per channel name, OPEN until a test sets it.
// ---------------------------------------------------------------------
int stub_audio_any_conversation_playing(lua_State* L) {
    logCall(L, upLog(L), "audio_any_conversation_playing", upStateTag(L));
    bool active = upState(L)->conversationChannelActive().get("mission_conv"); // OPEN until set
    lua_pushboolean(L, active ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// party_add_do (Sec32.4, 0x00a5af70). Arg 1 leader name; arg 2 table of
// follower names (length via 0x0083dff0); arg 3 boolean, no nil-gate; arg
// 4 optional boolean, nil-gated, default false. No return. CONFIRMED
// real stack-overrun hazard (6th-or-later follower) is flagged, not
// reproduced (see PartyState::overflowDetectedCount's own doc comment) -
// processing is clamped to the real 5-slot bound. A failed leader
// resolution's own per-follower "#CLOSEST_PLAYER#" re-resolution fallback
// (Sec32.4) requires a spatial query this project does not have and is
// not modeled - a failed leader resolve is treated as a safe no-op here,
// stated plainly rather than guessed past. arg3's own capacity/force-
// override gate and arg4's leader-kind-bit AI/registration setup have no
// real data this project can reproduce and are also not modeled (read for
// correct stack shape only).
// ---------------------------------------------------------------------
int stub_party_add_do(lua_State* L) {
    logCall(L, upLog(L), "party_add_do", upStateTag(L));
    std::string leader = argString(L, 1);
    int64_t followerCount = tableLength(L, 2);
    (void)(lua_toboolean(L, 3) != 0);          // arg3: no nil-gate; capacity/force-override gate not modeled (OPEN internals)
    (void)optionalBoolDefault(L, 4, false);    // arg4: leader-kind-bit AI/registration setup not modeled (OPEN internals)
    EngineState* st = upState(L);
    if (!st->objectResolves().get(leader)) return 0; // OPEN until set; CONFIRMED per-follower closest-player fallback not modeled here (no spatial query in this host)
    int64_t processed = followerCount < 0 ? 0 : std::min<int64_t>(followerCount, 5);
    if (followerCount > 5) ++st->party().overflowDetectedCount; // CONFIRMED real hazard, not reproduced
    for (int64_t i = 0; i < processed; ++i) {
        lua_rawgeti(L, 2, static_cast<int>(i + 1));
        std::string follower = argString(L, -1);
        lua_pop(L, 1);
        if (!follower.empty() && st->objectResolves().get(follower)) { // OPEN until set
            st->party().membersByLeader[leader].push_back(follower);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// npc_is_in_party (Sec32.4, 0x00a562a0). 1 string, reuses
// objectResolves(). Return: 1 boolean. CONFIRMED: false for an
// unresolved/null character; else HYPOTHESIS "has any party leader" (not
// specific to the local player's own party) - this project checks
// membership across every leader's own follower list.
// ---------------------------------------------------------------------
int stub_npc_is_in_party(lua_State* L) {
    logCall(L, upLog(L), "npc_is_in_party", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool inParty = false;
    if (st->objectResolves().get(name)) { // OPEN until set
        for (const auto& kv : st->party().membersByLeader) {
            const auto& followers = kv.second;
            if (std::find(followers.begin(), followers.end(), name) != followers.end()) {
                inParty = true;
                break;
            }
        }
    }
    lua_pushboolean(L, inParty ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// boss_battle_matt_begin (Sec33.5, 0x00a407c0). 1 optional boolean,
// default true. No return, no replication (CONFIRMED). Resets the
// BossBattleMatt cluster's cheat-slot/id/numeric fields to their neutral
// values (this project's own modelled subset - see BossBattleMatt's own
// doc comment, Sec28.24/Sec28.25; the real "four sub-objects" this spec
// also mentions have no modelled fields here, not guessed past; the
// retry counter 0x012ec720 is NOT confirmed reset by this specific entry
// - distinct from 0x012ec71c, the static limit - left untouched), then
// arms the first deadline (5000ms true / 0 false) and marks it active.
// CONFIRMED: also looks up the character literally named "Matt" and, if
// found and alive, sets bit 0x4000 of its dword +0xec (meaning OPEN).
// Every branch-determining read happens before any write (same "no
// partial update on an OPEN refusal" convention as set_ignore_ai_flag).
// ---------------------------------------------------------------------
int stub_boss_battle_matt_begin(lua_State* L) {
    logCall(L, upLog(L), "boss_battle_matt_begin", upStateTag(L));
    bool value = true; // CONFIRMED default true
    if (lua_gettop(L) >= 1 && lua_type(L, 1) != LUA_TNIL) value = lua_toboolean(L, 1) != 0;

    EngineState* st = upState(L);
    bool mattResolved = st->objectResolves().get("Matt"); // OPEN until set - read before any write
    bool mattAlive = mattResolved && !st->getOrCreateCharacter("Matt").isDeadHighConfidence;

    auto& matt = st->bossBattleMatt();
    matt.cheatSlot.set(-1);
    matt.lastId = -1;
    matt.lastN2 = 0.0;
    matt.lastN3 = 0.0;
    matt.deadlineMs = value ? 5000 : 0; // CONFIRMED
    matt.active = true; // CONFIRMED

    if (mattAlive) {
        st->getOrCreateCharacter("Matt").flagsEc.setBits(0x4000, 0x4000); // CONFIRMED structure, meaning OPEN
    }
    return 0;
}

// ---------------------------------------------------------------------
// npc_go_idle (Sec32.4, 0x00a554b0). 1 string, reuses objectResolves()
// (no "#PLAYER#" step, per spec - this project's shared resolver is
// name-agnostic, so no code-level distinction is needed). No return.
// CONFIRMED: resets the AI-orders sub-object (not individually modeled,
// only counted) and sets the action-override state to 0x19 if in a
// vehicle (stateEnum == 3), else 0 - "the exact rule
// set_ignore_ai_flag already applies" (Sec3.4, reused directly here).
// ---------------------------------------------------------------------
int stub_npc_go_idle(lua_State* L) {
    logCall(L, upLog(L), "npc_go_idle", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        CharacterState& c = st->getOrCreateCharacter(name);
        ++c.aiOrdersResetCount;
        c.actionOverrideId.set(c.stateEnum.get() == 3 ? 0x19 : 0); // OPEN (stateEnum) until set; CONFIRMED rule
    }
    return 0;
}

// ---------------------------------------------------------------------
// auto_pickup_disable (Sec33.5, 0x00a3c8a0). No arguments, no return.
// CONFIRMED: writes a named, session-synchronized variable
// "allow_weapon_auto_pickup" off (session-wide, not per-player).
// ---------------------------------------------------------------------
int stub_auto_pickup_disable(lua_State* L) {
    logCall(L, upLog(L), "auto_pickup_disable", upStateTag(L));
    upState(L)->allowWeaponAutoPickup() = false;
    return 0;
}

// ---------------------------------------------------------------------
// waiting_for_player_dialog (Sec33.5, 0x00a68ea0). 1 boolean, no
// nil-gate. No return. CONFIRMED reference-counted show/hide (nested
// shows need an equal number of hides; a hide at counter 0 does
// nothing); showing increments the counter even when suppressed (this
// project has no "suppressing game-state check" to evaluate - stated
// simplification, see WaitingForPlayerDialog's own doc comment). On the
// final hide, host+coop-active broadcasts an opcode-0x23 record; the
// host also sends the matching show broadcast when the display actually
// appears (same gating applied by symmetry, HIGH CONFIDENCE). Open
// hazard DELIBERATELY reproduced: neither path ever clears "currently
// shown" here (CONFIRMED - only an untraced third writer, out of scope,
// does), so after one real display this stays true for the rest of the
// run.
// ---------------------------------------------------------------------
int stub_waiting_for_player_dialog(lua_State* L) {
    logCall(L, upLog(L), "waiting_for_player_dialog", upStateTag(L));
    bool show = lua_toboolean(L, 1) != 0; // CONFIRMED: no nil-gate
    EngineState* st = upState(L);
    auto& d = st->waitingForPlayerDialog();
    if (show) {
        ++d.refCount; // CONFIRMED: increments even when suppressed
        if (!d.currentlyShown) {
            d.currentlyShown = true;
            if (st->coopIsActive() && st->coopLocalIsHost()) ++d.showBroadcastCount;
        }
        // else: CONFIRMED open hazard - already shown, display branch silently skipped (deliberately reproduced)
    } else if (d.refCount > 0) { // CONFIRMED: a hide at counter 0 does nothing
        --d.refCount;
        if (d.refCount == 0 && st->coopIsActive() && st->coopLocalIsHost()) {
            ++d.hideBroadcastCount; // CONFIRMED: host, while co-op is active
        }
        // CONFIRMED open hazard: "currently shown" is NOT cleared here either.
    }
    return 0;
}

// ---------------------------------------------------------------------
// object_destroy (Sec32.5, 0x00a57630). 1 string; non-string/nil -> no-
// op (CONFIRMED). No return. Reuses objectResolves(); the liveness check,
// capability-predicate gate and per-kind destroy virtual are OPEN/not
// modeled - only that a destroy was processed is recorded.
// ---------------------------------------------------------------------
int stub_object_destroy(lua_State* L) {
    logCall(L, upLog(L), "object_destroy", upStateTag(L));
    if (lua_type(L, 1) != LUA_TSTRING) return 0; // CONFIRMED: non-string/nil -> no-op
    std::string name = argString(L, 1);
    if (upState(L)->objectResolves().get(name)) { // OPEN until set
        upState(L)->destroyedObjects().insert(name);
    }
    return 0;
}

// ---------------------------------------------------------------------
// object_indicator_remove_do (Sec32.5, 0x00a59190). Arg 1 string; arg 2
// optional number, nil-gated, default 3 (bit 0x1 = apply locally, bit 0x2
// = send a record). No return. CONFIRMED: bit 0x1 removes EVERY indicator
// attached to the object, not just one (reuses
// CharacterState::objectIndicators, Sec10.6); bit 0x2 opens an opcode-
// 0x40 sub-tag-4 record (counted only, no networking layer modeled).
// ---------------------------------------------------------------------
int stub_object_indicator_remove_do(lua_State* L) {
    logCall(L, upLog(L), "object_indicator_remove_do", upStateTag(L));
    std::string name = argString(L, 1);
    int64_t mask = truncateEa2596(optionalNumberDefault(L, 2, 3.0));
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        CharacterState& c = st->getOrCreateCharacter(name);
        if (mask & 1) c.objectIndicators.clear(); // CONFIRMED: loop-until-none removal
        if (mask & 2) ++st->indicatorRemoveRecordCount();
    }
    return 0;
}

// ---------------------------------------------------------------------
// minimap_icon_remove_do (Sec32.5, 0x00a54430). Arg 1 string; arg 2
// optional number, nil-gated, default 3 (same bitmask convention). No
// return. CONFIRMED real crash hazard (an unranged 4-entry function-
// pointer table indexed directly by the mask byte) is flagged, not
// reproduced (Sec32.8) - this project clears every recorded minimap icon
// on the resolved object (reuses CharacterState::minimapIcons, Sec10.5)
// regardless of which real per-kind dispatch path the mask would select.
// ---------------------------------------------------------------------
int stub_minimap_icon_remove_do(lua_State* L) {
    logCall(L, upLog(L), "minimap_icon_remove_do", upStateTag(L));
    std::string name = argString(L, 1);
    int64_t mask = truncateEa2596(optionalNumberDefault(L, 2, 3.0));
    (void)mask; // the real per-kind dispatch/unranged table access (Sec32.5/Sec32.8) is not modeled
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        CharacterState& c = st->getOrCreateCharacter(name);
        c.minimapIcons.clear();
    }
    return 0;
}

// ---------------------------------------------------------------------
// shop_enable_nearest (Sec32.5, 0x00a5f650). Arg 1 optional string
// (location name, nil-gated default ""); arg 2 optional boolean, nil-
// gated, default true (enable). No return. CONFIRMED rule: the FIRST shop
// within 15 units is taken immediately, else the closest within 75 units.
// This project has no spatial/world-position system - see
// EngineState::Shop's own doc comment for the test-fixture stand-in (arg
// 1's own role in seeding the search origin is not modeled; read only for
// correct stack shape).
// ---------------------------------------------------------------------
int stub_shop_enable_nearest(lua_State* L) {
    logCall(L, upLog(L), "shop_enable_nearest", upStateTag(L));
    (void)optionalStringDefault(L, 1, ""); // location name - not modeled, see this function's own top comment
    bool enable = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    auto& shops = st->shops();
    int picked = -1;
    for (size_t i = 0; i < shops.size(); ++i) {
        if (shops[i].withinFifteen) { picked = static_cast<int>(i); break; }
    }
    if (picked < 0) {
        for (size_t i = 0; i < shops.size(); ++i) {
            if (shops[i].withinSeventyFive) { picked = static_cast<int>(i); break; } // "closest" tie-breaking beyond the threshold rule itself is not modeled
        }
    }
    if (picked >= 0) {
        shops[static_cast<size_t>(picked)].disabled = !enable;
        ++st->shopEnableRecordCount();
    }
    return 0;
}

// ---------------------------------------------------------------------
// item_show (Sec32.6, 0x00a51d50). 1 string; nil -> no-op (CONFIRMED).
// No return. CONFIRMED record-and-replicate shape, collapsed per this
// project's own established convention (replicateStateChange): always
// clears the "hidden" bit (+0x3b bit 0x1) on a resolved item.
// ---------------------------------------------------------------------
int stub_item_show(lua_State* L) {
    logCall(L, upLog(L), "item_show", upStateTag(L));
    if (lua_type(L, 1) == LUA_TNIL) return 0; // CONFIRMED: nil -> no-op
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->itemHidden()[name] = false; // CONFIRMED: item_show always shows (clears "hidden")
        EngineState::replicateStateChange("item_show", name);
    }
    return 0;
}

// ---------------------------------------------------------------------
// item_anim_play (Sec32.6, 0x00a51ad0). Arg 1 item name; arg 2 animation
// name (nil -> id -1); arg 3 optional boolean, nil-gated, default false;
// arg 4 optional string, nil-gated, default none. No return, no network
// record (local-only, CONFIRMED). CONFIRMED branch: if arg 4 resolves, a
// two-state blend-transition (0.3s) and arg 3 is ignored; else if arg 2
// resolves, a single-state start with flag 0x40 (arg3 true) or 0x10
// (arg3 false); neither resolving is a no-op.
// ---------------------------------------------------------------------
int stub_item_anim_play(lua_State* L) {
    logCall(L, upLog(L), "item_anim_play", upStateTag(L));
    std::string item = argString(L, 1);
    std::string animArg2 = (lua_type(L, 2) == LUA_TSTRING) ? argString(L, 2) : std::string();
    bool arg3 = optionalBoolDefault(L, 3, false);
    std::string animArg4 = optionalStringDefault(L, 4, "");
    EngineState* st = upState(L);
    if (st->objectResolves().get(item)) { // OPEN until set
        auto& rec = st->itemAnimState()[item];
        if (!animArg4.empty()) {
            rec.lastBlendTransitionAnim = animArg4; // arg3 ignored on this path, CONFIRMED
            ++rec.blendTransitionCount;
        } else if (!animArg2.empty()) {
            rec.lastSingleStateAnim = animArg2;
            rec.lastFlag = arg3 ? 0x40 : 0x10; // CONFIRMED branch values; HYPOTHESIS meaning
            ++rec.singleStateStartCount;
        }
        // neither resolving: no-op (CONFIRMED)
    }
    return 0;
}

// ---------------------------------------------------------------------
// radio_set_station (Sec32.7, 0x00a5c1c0). Arg 1 vehicle-or-character
// name (reuses objectResolves() - the vehicle resolver redirects through
// a character's cached vehicle); arg 2 number (station), no nil-gate,
// truncated to a signed byte. No return. CONFIRMED: the opcode record is
// ALWAYS sent once the vehicle resolves, even when the local 1-based
// apply (station <= 0 or above count, or itself marked refused, is
// rejected - never switches the radio off).
// ---------------------------------------------------------------------
int stub_radio_set_station(lua_State* L) {
    logCall(L, upLog(L), "radio_set_station", upStateTag(L));
    std::string target = argString(L, 1);
    int64_t stationWide = truncateEa2596(lua_tonumber(L, 2));
    int8_t station = static_cast<int8_t>(stationWide); // CONFIRMED: truncated to a signed byte
    EngineState* st = upState(L);
    if (st->objectResolves().get(target)) { // OPEN until set
        auto& radio = st->vehicleRadio(target);
        ++radio.recordSentCount; // CONFIRMED: always sent once the vehicle resolves
        if (radio.hasRadio.get() && station >= 1 && station <= radio.stationCount.get() && // OPEN until set
            !radio.stationRefused.get(std::to_string(static_cast<int>(station)))) {        // OPEN until set
            radio.currentStation = station;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// helicopter_shoot_vehicle (Sec32.7, 0x00a4d790). Arg 1 helicopter name;
// arg 2 target name (vehicle resolver); arg 3 optional boolean, nil-
// gated, default true (apply spread); arg 4 optional number, nil-gated,
// default 1.0 (spread radius); arg 5 optional boolean, nil-gated, default
// true. Return: 1 boolean - CONFIRMED false if either name fails to
// resolve or the helicopter fails its AI-drive-state gate; otherwise the
// shared dispatcher's own result, which is itself OPEN (its return value
// is invisible in the decompiled view, per spec) and refused rather than
// fabricated.
// ---------------------------------------------------------------------
int stub_helicopter_shoot_vehicle(lua_State* L) {
    logCall(L, upLog(L), "helicopter_shoot_vehicle", upStateTag(L));
    std::string heli = argString(L, 1);
    std::string target = argString(L, 2);
    (void)optionalBoolDefault(L, 3, true);       // apply spread - not modeled (spread offset has no Lua-visible return effect)
    (void)optionalNumberDefault(L, 4, 1.0);      // spread radius - not modeled
    (void)optionalBoolDefault(L, 5, true);       // arg 5 - OPEN role, not modeled
    EngineState* st = upState(L);
    if (!st->objectResolves().get(heli) || !st->objectResolves().get(target)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!st->helicopter(heli).aiDriveStateOk.get()) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, st->helicopterFireDispatcherResult().get() ? 1 : 0); // OPEN until set - the real dispatcher's own return value is not visible in the decompiled view (Sec32.7)
    return 1;
}

// ---------------------------------------------------------------------
// Batch 2026-10-02 (spec-lua-api-behaviour.md Sec35, the `teleport_coop`
// investigation): teleport_check_done / turn_to_check_done /
// move_to_check_done / vehicle_pathfind_check_done. The real, documented
// reason this batch exists: 3 real missions (dlc2_m01, m13, m19) sit
// suspended forever inside `teleport_coop` (game_lib.lua:2956, a script
// helper - NOT a native), because this project previously left
// teleport_check_done an unregistered generic stub (logged-nil) and a
// script's own `repeat thread_yield() until teleport_check_done(...)`
// idiom can never be satisfied by nil (host.h's own top comment already
// flags this exact real idiom). All 4 share one engine module/convention
// (Sec35.2) - see EngineState's own "teleport_check_done/..." section
// (engine_state.h) for the shared pool's full doc comment; only the
// per-function wrapper shape differs below.
// ---------------------------------------------------------------------

// teleport_check_done (Sec15.17, re-derived 2026-10-02 from the raw
// instruction stream, correcting an earlier inverted decompile-based
// reading). Arguments: 1 mandatory string - an object/character name
// (Sec15.17 itself resolves this via 0x00a281a0; this project has no real
// handle resolver - engine_state.h's own top note - so the raw name
// string IS the lookup key, same convention as CharacterState). Return: 1
// boolean, always (CONFIRMED). Body (CONFIRMED): the name and the literal
// kind 4 (teleport, Sec35.2) are looked up in the shared scripted-request
// pool; the pushed boolean is `status != 0` - true for "done" or "no
// matching request," false only while a request this host is actually
// tracking for this name is genuinely pending.
int stub_teleport_check_done(lua_State* L) {
    logCall(L, upLog(L), "teleport_check_done", upStateTag(L));
    std::string name = argString(L, 1);
    bool done = upState(L)->scriptedRequestCheckDone(name, EngineState::kScriptedRequestKindTeleport);
    lua_pushboolean(L, done ? 1 : 0);
    return 1;
}

// turn_to_check_done (0x00a61dd0; Sec35.2). This specific wrapper's own
// argument/return shape was NOT independently re-read this pass beyond
// "confirmed to use the SAME shared status function, kind 0" (Sec35.2's
// own closing sentence) - so only what IS confirmed is implemented here:
// a 1-mandatory-string lookup against the shared pool (kind 0 = turn-to),
// the identical shape to the sibling teleport_check_done/
// move_to_check_done. HIGH CONFIDENCE (not CONFIRMED) for the exact
// argument position/count; CONFIRMED for the shared status function and
// the done/pending polarity itself (Sec35.2).
int stub_turn_to_check_done(lua_State* L) {
    logCall(L, upLog(L), "turn_to_check_done", upStateTag(L));
    std::string name = argString(L, 1);
    bool done = upState(L)->scriptedRequestCheckDone(name, EngineState::kScriptedRequestKindTurnTo);
    lua_pushboolean(L, done ? 1 : 0);
    return 1;
}

// move_to_check_done (Sec22.15, Sec35.2). The real function takes 8
// arguments (a previously-allocated request id; arg 2 the character name;
// a target/group-member selector; a movement-style code; 3 booleans; a
// group-member index) and its fuller body additionally matches a
// per-character cached request-id tag, resolves a target or group member,
// and - only once genuinely arrived (a distance-under-1-unit-plus-
// predicate test this project does not model) - dispatches an arrival
// animation before clearing its own per-character sub-record (+0xc30/
// +0xc34). None of that fuller pipeline is modeled here: deliberately out
// of this task's own narrow scope (Sec35's brief is the shared pool's
// done/pending CONVENTION fix, not a reimplementation of every wrapper's
// full body - the same "don't invent a generic entity shape" boundary
// engine_state.h's own top note already states for CharacterState).
// What IS implemented, CONFIRMED (Sec35.2/Sec22.15's own corrected status-
// code table): arg 2 (the character name) is used as a genuine per-
// request lookup key - NOT ignored - against the SAME shared pool
// teleport_check_done/turn_to_check_done/vehicle_pathfind_check_done
// consult (kind 2 = move-to/pathfind), with the corrected polarity
// (status != 0 -> true, Sec35.2).
int stub_move_to_check_done(lua_State* L) {
    logCall(L, upLog(L), "move_to_check_done", upStateTag(L));
    std::string name = argString(L, 2);
    bool done = upState(L)->scriptedRequestCheckDone(name, EngineState::kScriptedRequestKindMoveOrPathfind);
    lua_pushboolean(L, done ? 1 : 0);
    return 1;
}

// vehicle_pathfind_check_done (Sec9.10) - the REFERENCE reading Sec35.2's
// own cross-check cites as "already correct", i.e. this one was never
// mis-read in the first place. Arguments: 1 mandatory string - a vehicle
// reference. Return: 1 NUMBER (NOT a boolean, CONFIRMED - lua_pushnumber
// then return 1) - the raw 3-way status code itself: 0 = pending, 1 =
// done (consumed), 2 = no active request OR the vehicle failed to resolve
// OR it has no occupant/part in slot 0 (most plausibly a driver check,
// Sec9.10's own text) - both the "no vehicle" and "no active request"
// routes converge on the SAME literal fallback constant, 2.0. This
// project has no vehicle-resolution/occupant registry (deliberately, same
// boundary as every other entity this project does not model) - every
// name defaults unresolvable, itself the real, CONFIRMED fallback for an
// unmodeled vehicle, not a guess (see
// EngineState::vehiclePathfindCheckDoneCode's own doc comment).
int stub_vehicle_pathfind_check_done(lua_State* L) {
    logCall(L, upLog(L), "vehicle_pathfind_check_done", upStateTag(L));
    std::string name = argString(L, 1);
    lua_pushnumber(L, upState(L)->vehiclePathfindCheckDoneCode(name));
    return 1;
}

// =======================================================================
// Batch 2026-10-02 (resumed session): spec-lua-api-behaviour.md Sec3.7/
// Sec20.1's vehicle-invulnerability bit conflict, now RESOLVED by ranking
// tranche 14 (Sec46.1: bit 0x01 of vehicle +0x1d7a, Sec3.7 right all along,
// Sec20.1's own prior "bit 0x8" was the error) - turn_invulnerable,
// turn_vulnerable, vehicle_set_vulnerable, vehicle_is_invulnerable - plus a
// curated subset of ranking tranches 12-14 (Sec44/Sec45/Sec46) themselves:
// the 2 siblings this correction directly touches' own supporting state
// (character_fake_revival_start/_end, character_take_human_shield_check_
// done, vehicle_turret_base_to_do, vehicle_lights_on, vehicle_tire_
// indicators_alive - all already scaffolded in engine_state.h before this
// session was interrupted), plus a further batch chosen the same way
// Sec33's own batch header (above) explains: a real call-count check
// (results/stub_ranking_with_specced_20261002.tsv, 70 rows, every row with
// >= 1 real measured call) finds ZERO of tranches 12/13/14's 75 names in
// it at all - no real-call-volume signal exists to prioritize BY within
// these tranches (matching their own source line: "Team B call-count
// order," ranks 226-300 of 554 - simply low in the overall static-call
// ranking, not specifically exercised by this trace's own missions either)
// - so, as Sec30/Sec33 both already precedent, breadth is applied instead:
// every remaining name in this batch is one Sec44/Sec45/Sec46 names
// EXPLICITLY enough to implement without inventing a new subsystem this
// project has nowhere else built (no networking, no Steam, no path/node
// graph, no cellphone activity-record state machine, no key-binding
// table). Named tranche-12/13/14 functions NOT in this batch (fov_check_
// xz_plane - ambiguous geometry-argument shape beyond "arg 3 is a half-
// angle"; the Steam/co-op/lobby/machinima/dialog/cinema-editor/cellphone_
// dial/boss-battle/action-sequence/world-despawn/ai_force_team_idle/
// customization_swap_player_rig/debris_flow_set_inactive/camera_script_*/
// cutscene_*/flashpoint_mission_status/autil_hud_mayhem_init/vehicle_set_
// npc_engine_audio/vehicle_set_kneecappers_damage/vint_options_remap_
// reset_bindings families) are deliberately left as ordinary generic
// logging stubs this pass - each would need either real networking/Steam/
// UI-pool/path-graph modeling this project does not build, or (the last
// two vehicle setters) a Lua argument TYPE the spec prose does not state -
// documented here rather than guessed, per this project's own no-invented-
// fixes/skip-and-revisit convention, for a future pass to pick up.
// =======================================================================

// Shared body for turn_invulnerable/turn_vulnerable (Sec3.7/Sec3.8):
// resolve via objectResolves() (this project's one shared name-resolution
// map, Sec29 - see engine_state.h's own top note on why there is no
// separate per-kind resolver), then write BOTH the "invulnerable" and
// "always apply player damage" bits via the SAME double-gate idiom
// (CharacterState::forceFlagGatePasses / VehicleState::forceFlagGatePasses),
// on whichever kind the name resolves to. CONFIRMED (Sec3.7): the real
// function's own resolution chain tries several DIFFERENT mechanisms in
// sequence (the generic/"#PLAYER#"/"#FOLLOWER#" chain, then a plain
// character resolver, then a vehicle resolver) that this project's single
// flat objectResolves() map cannot distinguish by itself - this project's
// own CHOSEN, explicitly-stated disambiguation (NOT a spec fact): if a
// VehicleState entry already exists for `targetName` and no CharacterState
// entry does, treat it as a vehicle; otherwise default to character,
// matching the real chain's own order (the character-oriented paths are
// tried FIRST, so character wins when a test has set up neither or both).
void applyInvulnerabilityFlags(lua_State* L, const char* name, const std::string& targetName, bool invulnerable,
                                bool alwaysApplyPlayerDamage) {
    EngineState* st = upState(L);
    if (!st->objectResolves().get(targetName)) return; // OPEN until set
    bool treatAsVehicle = st->hasVehicle(targetName) && !st->hasCharacter(targetName);
    if (treatAsVehicle) {
        VehicleState& vehicle = st->getOrCreateVehicle(targetName);
        if (vehicle.forceFlagGatePasses.get()) { // OPEN until set
            vehicle.forceFlags1d7a.setBits(VehicleState::kInvulnerableBit1d7a,
                                           invulnerable ? VehicleState::kInvulnerableBit1d7a : 0u);
            vehicle.forceFlags1d7a.setBits(VehicleState::kAlwaysApplyPlayerDamageBit1d7a,
                                           alwaysApplyPlayerDamage ? VehicleState::kAlwaysApplyPlayerDamageBit1d7a : 0u);
        } else {
            EngineState::replicateStateChange(name, targetName);
        }
    } else {
        CharacterState& character = st->getOrCreateCharacter(targetName);
        if (character.forceFlagGatePasses.get()) { // OPEN until set
            character.forceFlags1c98.setBits(CharacterState::kInvulnerableBit1c98,
                                             invulnerable ? CharacterState::kInvulnerableBit1c98 : 0u);
            character.forceFlags1c98.setBits(CharacterState::kAlwaysApplyPlayerDamageBit1c98,
                                             alwaysApplyPlayerDamage ? CharacterState::kAlwaysApplyPlayerDamageBit1c98
                                                                     : 0u);
        } else {
            EngineState::replicateStateChange(name, targetName);
        }
    }
}

// ---------------------------------------------------------------------
// turn_invulnerable (Sec3.7, 0x00a61b80) - 222 calls / 33 scripts (real
// static-call ranking, distinct from this batch's own zero-mission-drive-
// hits finding above). Arg 1 mandatory string; arg 2 optional boolean,
// nil-gated, default false. No return. CONFIRMED: sets the invulnerable
// bit to TRUE (hardcoded, not tied to any argument) and the companion
// "always apply player damage" bit to arg 2, via the double-gate idiom
// above, on whichever kind resolves.
// ---------------------------------------------------------------------
int stub_turn_invulnerable(lua_State* L) {
    logCall(L, upLog(L), "turn_invulnerable", upStateTag(L));
    std::string name = argString(L, 1);
    bool alwaysApplyPlayerDamage = optionalBoolDefault(L, 2, false);
    applyInvulnerabilityFlags(L, "turn_invulnerable", name, /*invulnerable=*/true, alwaysApplyPlayerDamage);
    return 0;
}

// ---------------------------------------------------------------------
// turn_vulnerable (Sec3.8, 0x00a61cb0) - 149 calls / 30 scripts. Arg 1
// mandatory string only - "no second argument is read at all" (CONFIRMED).
// No return. CONFIRMED: the mirror image of turn_invulnerable - clears
// BOTH the invulnerable flag and its companion flag (both hardcoded false)
// on whichever kind resolves, via the same double-gate idiom.
// ---------------------------------------------------------------------
int stub_turn_vulnerable(lua_State* L) {
    logCall(L, upLog(L), "turn_vulnerable", upStateTag(L));
    std::string name = argString(L, 1);
    applyInvulnerabilityFlags(L, "turn_vulnerable", name, /*invulnerable=*/false, /*alwaysApplyPlayerDamage=*/false);
    return 0;
}

// ---------------------------------------------------------------------
// vehicle_set_vulnerable (Sec20.1, 0x00a634f0). 1 mandatory string
// (vehicle name). "No boolean argument exists at all" (CONFIRMED). No
// return. CONFIRMED: delegates to 0x00a7a830(vehicle, false - hardcoded
// literal, never a Lua argument) ONLY - unlike turn_vulnerable above, the
// companion "always apply player damage" setter (0x00a7a9a0) is never
// called from here, so that bit is left untouched entirely. Reuses the
// existing vehicle-only double-gate helper (Sec30.5) directly - this is a
// vehicle-only Lua entry point (resolves via "the already-established
// dedicated vehicle resolver," Sec20.1 - this project's objectResolves()
// stands in for that too, same as every other vehicle function here).
// ---------------------------------------------------------------------
int stub_vehicle_set_vulnerable(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_vulnerable", upStateTag(L));
    std::string vehicleName = argString(L, 1);
    applyVehicleForceFlagSetter(L, "vehicle_set_vulnerable", vehicleName, /*flag=*/false,
                                VehicleState::kInvulnerableBit1d7a, vehicleForceFlags1d7aField);
    return 0;
}

// ---------------------------------------------------------------------
// vehicle_is_invulnerable (Sec46.5/Sec46.1). 1 mandatory string (vehicle
// name). Return: 1 boolean. CONFIRMED: "reads the now-settled bit 0x01 of
// +0x1d7a" - the exact bit turn_invulnerable/vehicle_set_vulnerable write
// above. Unresolved vehicle -> false (this project's own consistent
// default for every unresolved-vehicle boolean getter in this file, same
// as vehicle_is_helicopter/vehicle_is_ready).
// ---------------------------------------------------------------------
int stub_vehicle_is_invulnerable(lua_State* L) {
    logCall(L, upLog(L), "vehicle_is_invulnerable", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool result = false;
    if (st->objectResolves().get(name)) { // OPEN until set
        result = st->getOrCreateVehicle(name).forceFlags1d7a.get(VehicleState::kInvulnerableBit1d7a) != 0; // OPEN until set
    }
    lua_pushboolean(L, result ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// character_fake_revival_start (Sec45.1, 0x00a41f80). 1 mandatory string
// (character name). No return (not stated). CONFIRMED: reads through null
// first (inside a helper, +0xcd2) when `name` does not resolve - a real
// null-read crash, flagged, not reproduced (same "flagged, not reproduced"
// convention as minimap_icon_remove_do's Sec32.8 table-index crash): this
// project simply skips the write. On a resolved name, sets dword +0xe4 bit
// 0x10000 ("fake revival in progress," established together with its
// sibling _end below).
// ---------------------------------------------------------------------
int stub_character_fake_revival_start(lua_State* L) {
    logCall(L, upLog(L), "character_fake_revival_start", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->getOrCreateCharacter(name).flagsE4.setBits(CharacterState::kFakeRevivalInProgressBit,
                                                       CharacterState::kFakeRevivalInProgressBit);
    }
    return 0;
}

// ---------------------------------------------------------------------
// character_fake_revival_end (Sec45.1, 0x00a41fc0). 1 mandatory string
// (character name). No return (not stated). CONFIRMED: "writes through
// null (clearing bit 16 of +0xe4) when the name doesn't resolve" - a real
// null-write crash, flagged, not reproduced. On a resolved name, clears
// the same bit _start above sets.
// ---------------------------------------------------------------------
int stub_character_fake_revival_end(lua_State* L) {
    logCall(L, upLog(L), "character_fake_revival_end", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->getOrCreateCharacter(name).flagsE4.setBits(CharacterState::kFakeRevivalInProgressBit, 0u);
    }
    return 0;
}

// ---------------------------------------------------------------------
// character_take_human_shield_check_done (Sec45.1, 0x00a46600). 2
// mandatory strings (taker, expected hostage name). CONFIRMED: "returns 1,
// 2, or 3 Lua values depending on what resolves (a script reading only the
// first value gets the intended 'taker holds the expected hostage'
// boolean)." This project models only that first, documented boolean - the
// real content of a 2nd/3rd return value is not stated by the spec and is
// refused rather than fabricated. When `taker` does not resolve, "the root
// pushes false and keeps going into the hostage-lookup helper anyway" - a
// real null-read crash (reads null +0x1890), flagged, not reproduced: this
// project stops at the same `false`. This project models only the single,
// locally-owned hostage field (CharacterState::humanShieldHostageName's own
// doc comment - no real networking layer).
// ---------------------------------------------------------------------
int stub_character_take_human_shield_check_done(lua_State* L) {
    logCall(L, upLog(L), "character_take_human_shield_check_done", upStateTag(L));
    std::string taker = argString(L, 1);
    std::string expectedHostage = argString(L, 2);
    EngineState* st = upState(L);
    bool takerHoldsExpectedHostage = false;
    if (st->objectResolves().get(taker)) { // OPEN until set
        // The vacuous "" == "" case (no expected name given at all) is
        // deliberately excluded - "holds nothing" should never read as
        // "holds the (unnamed) expected hostage," this project's own minor
        // defensive choice, not a spec fact.
        takerHoldsExpectedHostage =
            !expectedHostage.empty() && st->getOrCreateCharacter(taker).humanShieldHostageName == expectedHostage;
    }
    lua_pushboolean(L, takerHoldsExpectedHostage ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// vehicle_lights_on (Sec46.5). 1 mandatory string (vehicle name) + 1
// boolean, no stated nil-gate (absent -> false via real lua_toboolean
// semantics, same convention as vehicle_car_alarm_enable, Sec20.3). No
// return. CONFIRMED: `vehicle_lights_on(v, false)` does NOT turn the
// lights off - it clears BOTH the force-on and force-off flags, returning
// the lights to automatic control (a genuine naming trap, not reproduced
// as "off"). The true-path's own exact flag combination beyond "force on"
// is this project's own stated choice (see VehicleState::lightsForceFlags's
// own doc comment).
// ---------------------------------------------------------------------
int stub_vehicle_lights_on(lua_State* L) {
    logCall(L, upLog(L), "vehicle_lights_on", upStateTag(L));
    std::string name = argString(L, 1);
    bool forceOn = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        VehicleState& v = st->getOrCreateVehicle(name);
        uint32_t bothBits = VehicleState::kLightsForceOnBit | VehicleState::kLightsForceOffBit;
        v.lightsForceFlags.setBits(bothBits, forceOn ? VehicleState::kLightsForceOnBit : 0u);
    }
    return 0;
}

// ---------------------------------------------------------------------
// vehicle_tire_indicators_alive (Sec46.5). 1 mandatory string (vehicle
// name). Return: 1 number. CONFIRMED naming-trap short-circuit: reports 8
// ("all alive") whenever the vehicle's tire-indicator object is simply
// DISABLED, not actually alive. The real per-tire alive bitmask (the non-
// disabled case) needs a per-wheel physics pool this project does not
// build (same gap as kneecappersEnabled's own precedent, Sec33.1) -
// refused (OpenStateError) rather than fabricated. Unresolved vehicle: not
// addressed by Sec46.5's own text - this project's own choice (not a spec
// fact), the same "false"-equivalent default every other unresolved-
// vehicle getter in this file uses.
// ---------------------------------------------------------------------
int stub_vehicle_tire_indicators_alive(lua_State* L) {
    logCall(L, upLog(L), "vehicle_tire_indicators_alive", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        VehicleState& v = st->getOrCreateVehicle(name);
        if (v.tireIndicatorObjectDisabled.get()) { // OPEN until set
            lua_pushnumber(L, 8);
            return 1;
        }
        throw OpenStateError("vehicle_tire_indicators_alive per-tire alive bitmask (indicator object not disabled)",
                             "spec-lua-api-behaviour.md Sec46.5");
    }
    lua_pushnumber(L, 0);
    return 1;
}

// ---------------------------------------------------------------------
// vehicle_turret_base_to_do (Sec46.4). 1 vehicle name + 1 target name + 1
// boolean. CONFIRMED: "boolean false on failure, a plain number (0/1/2) on
// an attempt." The target-string length loop runs before its own null
// test, so a nil/non-string target name dereferences null WHEN the vehicle
// resolves and has a seat-0 occupant - a real null-read crash, flagged,
// not reproduced (this project takes the same "boolean false" path rather
// than a "no occupant" early-false and a null-target early-false sharing
// one outcome, since nothing in Sec46.4's text distinguishes their return
// shapes). Beyond that: by design an OBJECT target can never actually
// drive the vehicle (reduces to a single point, below the shared drive
// routine's 2-point minimum) - only a PATH target works, truncated at 24
// nodes - and this project has no real path/node object anywhere (no in-
// scope producer creates one, same "no generic entity shape invented"
// boundary this header's own top note states), so it cannot tell a path
// target from an object target; the real "plain number (0/1/2) on an
// attempt" progress code is refused (OpenStateError) rather than
// fabricated.
// ---------------------------------------------------------------------
int stub_vehicle_turret_base_to_do(lua_State* L) {
    logCall(L, upLog(L), "vehicle_turret_base_to_do", upStateTag(L));
    std::string vehicleName = argString(L, 1);
    std::string targetName = argString(L, 2);
    (void)lua_toboolean(L, 3); // arg 3's own role is not stated by Sec46.4 - read, not modeled further
    EngineState* st = upState(L);
    if (!st->objectResolves().get(vehicleName)) { // OPEN until set
        lua_pushboolean(L, 0); // CONFIRMED: boolean false on failure
        return 1;
    }
    VehicleState& v = st->getOrCreateVehicle(vehicleName);
    if (!v.seat0Occupied.get()) { // OPEN until set - gates the real null-read crash below
        lua_pushboolean(L, 0);
        return 1;
    }
    if (targetName.empty()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    throw OpenStateError("vehicle_turret_base_to_do attempt-progress code (path-target validation not modeled)",
                         "spec-lua-api-behaviour.md Sec46.4");
}

// ---------------------------------------------------------------------
// game_is_pc_dx11 / game_get_ps3_button_swap / game_record_mode_is_
// supported / game_record_mode_is_active (Sec44.1, all 4 resolve to the
// SAME native handler 0x00a3c670). CONFIRMED: no arguments read,
// unconditionally returns false - "this executable is the DX9 build;
// whether a separate DX11 executable binds a different handler is OPEN"
// (not a question this single-build project can answer). Console-overlay/
// recording features with no PC implementation. Each gets its own tiny
// wrapper (not one shared body) - same "each registered name is a real C
// function" convention every other entry in this file follows.
// ---------------------------------------------------------------------
int stub_game_is_pc_dx11(lua_State* L) {
    logCall(L, upLog(L), "game_is_pc_dx11", upStateTag(L));
    lua_pushboolean(L, 0);
    return 1;
}
int stub_game_get_ps3_button_swap(lua_State* L) {
    logCall(L, upLog(L), "game_get_ps3_button_swap", upStateTag(L));
    lua_pushboolean(L, 0);
    return 1;
}
int stub_game_record_mode_is_supported(lua_State* L) {
    logCall(L, upLog(L), "game_record_mode_is_supported", upStateTag(L));
    lua_pushboolean(L, 0);
    return 1;
}
int stub_game_record_mode_is_active(lua_State* L) {
    logCall(L, upLog(L), "game_record_mode_is_active", upStateTag(L));
    lua_pushboolean(L, 0);
    return 1;
}

// ---------------------------------------------------------------------
// game_show_party_ui / game_show_community_sessions_ui (Sec44.1). CONFIRMED:
// both resolve to the existing shared no-op stub 0x007c9f50 - the SAME
// native handler already backing set_mission_author (Sec6.1) - so these
// are genuinely, confirmedly inert, not merely unmodeled.
// ---------------------------------------------------------------------
int stub_game_show_party_ui(lua_State* L) {
    logCall(L, upLog(L), "game_show_party_ui", upStateTag(L));
    return 0;
}
int stub_game_show_community_sessions_ui(lua_State* L) {
    logCall(L, upLog(L), "game_show_community_sessions_ui", upStateTag(L));
    return 0;
}

// ---------------------------------------------------------------------
// flee_to_navpoint (Sec44.5/Sec44.6). CONFIRMED: queues a per-character AI
// event (type 0x30) with the navpoint position and a threat handle;
// "silently dropped if the event pool is empty, the character is dead, or
// it is a player-class object; '#PLAYER#' is not accepted for the fleeing
// character itself." This project has no real AI-event free-list to
// exhaust (stated gap) and no coordinate system to read a real navpoint
// position/threat handle from (project-wide simplification) - only what
// Sec44.5's own prose actually states is modeled: the fleeing character
// (arg 1) must resolve, must not be "#PLAYER#," and must not be dead;
// CharacterState::fleeToNavpointRequestCount counts a qualifying request.
// No return (not stated).
// ---------------------------------------------------------------------
int stub_flee_to_navpoint(lua_State* L) {
    logCall(L, upLog(L), "flee_to_navpoint", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (name != "#PLAYER#" && st->objectResolves().get(name)) { // OPEN until set
        CharacterState& c = st->getOrCreateCharacter(name);
        if (!c.isDeadHighConfidence) {
            ++c.fleeToNavpointRequestCount;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// character_hidden (Sec45.1, 0x00a41540). 1 mandatory string. Return: 1
// boolean. CONFIRMED: reads bit 0 of +0x3b; "an unresolved name reads as
// 'hidden' (true), not an error" - the one inverted-polarity unresolved
// default in this whole file (every other unresolved-name boolean getter
// here defaults to false).
// ---------------------------------------------------------------------
int stub_character_hidden(lua_State* L) {
    logCall(L, upLog(L), "character_hidden", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool hidden = true; // CONFIRMED unresolved default
    if (st->objectResolves().get(name)) { // OPEN until set
        hidden = st->getOrCreateCharacter(name).hiddenFlag.get(); // OPEN until set
    }
    lua_pushboolean(L, hidden ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// ambient_gang_spawn_enable (Sec45.6). 1 boolean, no stated nil-gate.
// CONFIRMED: "a single boolean fans out to all four ambient-gang-enable
// bytes at once - no per-gang Lua control exists at this entry point" -
// modeled as one global value (EngineState::ambientGangSpawnEnabled's own
// doc comment explains why 4 redundant copies would add nothing).
// ---------------------------------------------------------------------
int stub_ambient_gang_spawn_enable(lua_State* L) {
    logCall(L, upLog(L), "ambient_gang_spawn_enable", upStateTag(L));
    upState(L)->ambientGangSpawnEnabled().set(lua_toboolean(L, 1) != 0);
    return 0;
}

// ---------------------------------------------------------------------
// cellphone_animate_stop_do (Sec45.4). CONFIRMED no-op: "calls the
// project's already-known no-op stub."
// ---------------------------------------------------------------------
int stub_cellphone_animate_stop_do(lua_State* L) {
    logCall(L, upLog(L), "cellphone_animate_stop_do", upStateTag(L));
    return 0;
}

// ---------------------------------------------------------------------
// cell_camera_enable / cell_camera_is_enabled (Sec45.4). CONFIRMED: "writes
// a global that has exactly one other reference anywhere in the binary -
// its own getter ... confirmed Lua-side-only state with no engine
// consumer." A single bare global, not per-name.
// ---------------------------------------------------------------------
int stub_cell_camera_enable(lua_State* L) {
    logCall(L, upLog(L), "cell_camera_enable", upStateTag(L));
    upState(L)->cellCameraEnabled().set(lua_toboolean(L, 1) != 0);
    return 0;
}
int stub_cell_camera_is_enabled(lua_State* L) {
    logCall(L, upLog(L), "cell_camera_is_enabled", upStateTag(L));
    lua_pushboolean(L, upState(L)->cellCameraEnabled().get() ? 1 : 0); // OPEN until set
    return 1;
}

// ---------------------------------------------------------------------
// ambient_cop_spawn_enable / action_nodes_shouldnt_flee / action_nodes_
// restrict_spawning (Sec46.3). CONFIRMED: "simple global-byte toggles
// gating AI spawn/flee behavior, none replicated - a co-op client calling
// any of them only changes its own copy." Each write-only in this
// project's scope (no in-scope Lua getter reads any of them back).
// ---------------------------------------------------------------------
int stub_ambient_cop_spawn_enable(lua_State* L) {
    logCall(L, upLog(L), "ambient_cop_spawn_enable", upStateTag(L));
    upState(L)->ambientCopSpawnEnabled().set(lua_toboolean(L, 1) != 0);
    return 0;
}
int stub_action_nodes_shouldnt_flee(lua_State* L) {
    logCall(L, upLog(L), "action_nodes_shouldnt_flee", upStateTag(L));
    upState(L)->actionNodesShouldntFlee().set(lua_toboolean(L, 1) != 0);
    return 0;
}
int stub_action_nodes_restrict_spawning(lua_State* L) {
    logCall(L, upLog(L), "action_nodes_restrict_spawning", upStateTag(L));
    upState(L)->actionNodesRestrictSpawning().set(lua_toboolean(L, 1) != 0);
    return 0;
}

// ---------------------------------------------------------------------
// whored_countdown_finished (Sec46.6, UI sub-registrar 0x006147c0).
// CONFIRMED: "its own guard byte is never written anywhere in the binary
// (so the guard always passes) and the flag it sets has no direct reader
// found - in practice this call only ever sends a network broadcast with
// no host gate." No local state to model (the flag has no reader anywhere
// in the binary) - only the replicate call site is recorded, honestly,
// same "mark the call site, no further effect" convention as every other
// confirmed-inert broadcast in this file.
// ---------------------------------------------------------------------
int stub_whored_countdown_finished(lua_State* L) {
    logCall(L, upLog(L), "whored_countdown_finished", upStateTag(L));
    EngineState::replicateStateChange("whored_countdown_finished", "");
    return 0;
}

// ---------------------------------------------------------------------
// vehicle_set_tire_durability / vehicle_set_tire_damage_multiplier
// (Sec46.5). 1 vehicle name + 1 number, no stated nil-gate. No return.
// CONFIRMED: "silently ignore any value <= 0 (the field can never be reset
// to 0 from Lua this way) and, for an unresolved vehicle name inside a
// session, broadcast a network record for vehicle id 0 rather than doing
// nothing" - the notable quirk §46.7 flags as "a single-gate broadcast
// variant with no local fallback for an unresolved vehicle name": unlike
// this file's double-gate force-flag setters, an unresolved name here
// still replicates (tagged with the literal "vehicle id 0" this project's
// own stand-in for that real fallback id, not a guessed numeric scheme)
// instead of doing nothing at all.
// ---------------------------------------------------------------------
int stub_vehicle_set_tire_durability(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_tire_durability", upStateTag(L));
    std::string name = argString(L, 1);
    double value = lua_tonumber(L, 2);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        if (value > 0.0) st->getOrCreateVehicle(name).tireDurability.set(value);
        EngineState::replicateStateChange("vehicle_set_tire_durability", name);
    } else {
        EngineState::replicateStateChange("vehicle_set_tire_durability", "<vehicle-id-0>");
    }
    return 0;
}
int stub_vehicle_set_tire_damage_multiplier(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_tire_damage_multiplier", upStateTag(L));
    std::string name = argString(L, 1);
    double value = lua_tonumber(L, 2);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        if (value > 0.0) st->getOrCreateVehicle(name).tireDamageMultiplier.set(value);
        EngineState::replicateStateChange("vehicle_set_tire_damage_multiplier", name);
    } else {
        EngineState::replicateStateChange("vehicle_set_tire_damage_multiplier", "<vehicle-id-0>");
    }
    return 0;
}

// Batch 2026-10-03: spec-lua-api-behaviour.md Sec49 ("ranking tranche
// 16") - 25 previously-unspecced Lua-bound names. This batch's own fresh
// mission-drive run (build_verify_tranche16, against the real game
// archives, same convention as Sec38/Sec39/Sec40's own batch) found
// exactly ONE real hit among the 25 names - `spawning_boats` (1
// incremental call from a real mission script, grepped directly from
// that run's own verdict_stub_hits_with_missions.tsv) - every other name
// was zero real hits, the same outcome that combined 75-name batch
// already found for all 75 of ITS names. Per the orchestrator's own
// explicit scope instruction, this batch implements the 8 names
// EXPLICITLY MANDATED regardless of call count (4 crash guards, 4
// faithful quirks - see each stub's own doc comment below and
// engine_state.h's own Sec49 accessor comments for the full citation/
// reasoning): shop_purchase_purchase_shop, spawn_override_set_override_
// category_for_hood, skydive_move_to_check_done, set_char_in_string
// (guards); store_stronghold_game_purchase_upgrade, squad_enable, sfx_
// use_load_images, screen_capture_preview_should_upload (quirks); PLUS
// the 1 name with a real measured hit, `spawning_boats` (9 total). The
// other 16 named this tranche (store_stronghold_upgrade_end_flyby/
// store_gallery_download_show_list/store_vehicle_do_return_to_crib/
// _allow_garage/store_gang_is_unlocked/_bg_covered/_begin_exit/
// store_gallery_upload_character/_display_character/store_crib_init_
// crib_garage/spawn_global_override_set_category/_clear_category/set_
// attack_peds_flag, plus 3 further spawn_*/spawning_* names §49.2's own
// condensed text counts toward its "7 functions" header without naming
// individually) are zero real hits with no mandated guard/quirk - each
// would need its own substantial new engine-state modeling (a store-
// screen-stack/camera-fly-by model, a garage-node/mission-denylist
// table, a gang-store cash/unlock-bit gate, a deferred-upload
// scheduler, a road/world object resolver) this project does not have
// in scope - left as generic stubs this pass, same judgment call Sec38/
// Sec39/Sec40's own batch made for its own 59 unimplemented names.
// -----------------------------------------------------------------------

// shop_purchase_purchase_shop (Sec49.1, 0x00a03600 one-name registrar,
// function-pointer-before-name - the SAME registration quirk tranche 13
// first found) - MANDATED crash guard #1 PLUS the MANDATED faithful
// double-charge quirk, both on the same function. No arguments. See
// EngineState::ShopPurchase's own doc comment (engine_state.h) for the
// full citation. HOST-SAFETY DEVIATION (not a spec fact): guards the
// null-shop dereference via `triggerResolves` (CHOSEN default false)
// instead of performing the equivalent unguarded read, counting the
// averted path via nullShopGuardCount - "this project does not simulate
// crashes" (cf. group_get_next_npc, Sec28.5). Once resolved, EVERY call
// repeats the payment/trigger-disable/broadcast/fly-by side effects
// (the four *RepeatCount fields) - reproduced faithfully, NOT "fixed"
// into a once-only purchase - while `owned` itself is set only once,
// matching the real, CONFIRMED guarded bit exactly.
int stub_shop_purchase_purchase_shop(lua_State* L) {
    logCall(L, upLog(L), "shop_purchase_purchase_shop", upStateTag(L));
    EngineState* st = upState(L);
    auto& sp = st->shopPurchase();
    if (!sp.triggerResolves) {
        ++sp.nullShopGuardCount; // HOST-SAFETY, see comment above
        logCall(L, upLog(L), "shop_purchase_purchase_shop:NULL_SHOP_GUARD", upStateTag(L));
        return 0;
    }
    // CONFIRMED real double-charge defect (Sec49.1): every side effect
    // below repeats on every resolved call; only `owned` just below is
    // guarded to a single call.
    st->playerCash() -= sp.price; // "routed to owner if not locally owned" - not modeled further, no economy-routing layer in scope
    ++sp.paymentRepeatCount;
    ++sp.triggerDisableRepeatCount;
    ++sp.ownedBroadcastRepeatCount;
    ++sp.flyByRepeatCount;
    if (!sp.owned) {
        sp.owned = true; // CONFIRMED: the ONE guarded bit
    }
    return 0;
}

// spawn_override_set_override_category_for_hood (Sec49.2, 0x00a20840
// gameplay registrar) - MANDATED crash guard #2. 2 mandatory strings
// (neighbourhood, category name). See EngineState::SpawnOverride's own
// doc comment (engine_state.h) for the full citation. HOST-SAFETY
// DEVIATION (not a spec fact): guards only the immediate-dispatch host
// branch (the one shape that crashes INSIDE this function, per Sec49.2's
// own text); the "null merely stored, dereferenced later elsewhere"
// branch is not a crash in THIS function, so it is reproduced plainly
// (the null is honestly recorded as stored, not guarded) rather than
// invented into a guard that would not match the real defect's own
// shape. Deliberately does NOT copy the sibling `spawn_global_override_
// set_category`'s own check (that sibling is out of this batch's scope,
// and "fixing" this one would misrepresent a real, confirmed asymmetry
// between the two).
int stub_spawn_override_set_override_category_for_hood(lua_State* L) {
    logCall(L, upLog(L), "spawn_override_set_override_category_for_hood", upStateTag(L));
    std::string hood = argString(L, 1);
    std::string category = argString(L, 2);
    EngineState* st = upState(L);
    auto& so = st->spawnOverride();
    bool resolved = so.categoryNameResolves.known(category) && so.categoryNameResolves.get(category); // OPEN until set
    if (!resolved) {
        bool present = st->coopSession().present.get();                      // OPEN until set
        bool hostImmediate = present && st->coopSession().localIsHost.get(); // OPEN until set, only read when present
        if (hostImmediate) {
            ++so.hoodCategoryImmediateNullDerefGuardCount; // HOST-SAFETY, see comment above
            logCall(L, upLog(L), "spawn_override_set_override_category_for_hood:NULL_CATEGORY_GUARD", upStateTag(L));
            return 0;
        }
        // CONFIRMED real defect, reproduced plainly: the null is stored
        // unguarded here (not a crash in THIS function) - the deferred
        // read happens elsewhere (the per-hood reader or the co-op
        // join-snapshot serializer), out of this batch's own scope.
        so.perHoodCategoryNullStored.insert(hood);
        so.perHoodCategory.erase(hood);
        return 0;
    }
    so.perHoodCategoryNullStored.erase(hood);
    so.perHoodCategory[hood] = category;
    return 0;
}

// spawning_boats (Sec49.2, 0x00a20840 gameplay registrar) - added to this
// batch after its own fresh mission-drive run (build_verify_tranche16)
// found a REAL measured hit: 1 incremental call from a real mission
// script (grepped directly from that run's own verdict_stub_hits_with_
// missions.tsv: call_count_all_inclusive=1, incremental_from_missions=1,
// last_argc=1, last_arg_types=boolean). 1 mandatory boolean. CONFIRMED:
// writes a host-authoritative, session-replicated byte (registered name
// `"os_boat"`) with NO host gate on the write itself - unlike tranche
// 13's sibling `audio_suppress_ambient_player_lines` (Sec45, itself
// unimplemented anywhere in this project), which DOES gate. Reuses
// EngineState::replicateStateChange() (this project's own established
// no-op stand-in for the record-and-replicate idiom, Sec3.4/Sec3.7/
// Sec4.13/Sec7.33 - this project builds no real networking layer).
int stub_spawning_boats(lua_State* L) {
    logCall(L, upLog(L), "spawning_boats", upStateTag(L));
    bool value = lua_toboolean(L, 1) != 0; // bare boolean (CONFIRMED)
    EngineState* st = upState(L);
    auto& sb = st->spawningBoats();
    sb.value = value; // CONFIRMED: no host gate on the write itself
    ++sb.writeCount;
    EngineState::replicateStateChange("os_boat", value ? "true" : "false"); // CONFIRMED: session-replicated, unconditionally
    return 0;
}

// skydive_move_to_check_done (Sec49.3, 0x00a20840 gameplay registrar) -
// MANDATED crash guard #3, a NEW crash shape for this series (pointer-
// arithmetic-survives-null-test, not a plain unguarded dereference). See
// EngineState::SkydiveMoveToCheckDone's own doc comment (engine_state.h)
// for the full citation, including the CHOSEN/labelled argument shape
// (not independently re-derived this pass).
int stub_skydive_move_to_check_done(lua_State* L) {
    logCall(L, upLog(L), "skydive_move_to_check_done", upStateTag(L));
    std::string target = argString(L, 1);
    bool listMode = lua_toboolean(L, 2) != 0; // CHOSEN arg shape, see engine_state.h's own doc comment
    EngineState* st = upState(L);
    bool targetKnownResolved = st->objectResolves().known(target) && st->objectResolves().get(target);
    if (!targetKnownResolved) {
        st->skydiveMoveToCheckDone().nullTargetGuardCount++; // HOST-SAFETY, see comment above
        logCall(L, upLog(L), "skydive_move_to_check_done:NULL_TARGET_GUARD", upStateTag(L));
        // HOST-SAFETY DEVIATION: gives the SAME answer the already-correct
        // list-mode branch gives for the identical failure ("done", true),
        // regardless of which mode this call actually requested - per
        // Sec49.3's own text, that is the correct answer for BOTH modes on
        // this exact failure; only single-object mode's own code path
        // mishandles getting there.
        lua_pushboolean(L, 1);
        return 1;
    }
    (void)listMode; // resolved-target done/pending determination OPEN in either mode, see comment above - refuses rather than invents
    throw OpenStateError("skydive_move_to_check_done resolved-target done/pending determination",
                         "spec-lua-api-behaviour.md Sec49.3");
}

// set_char_in_string (Sec49.4, `ui`) - MANDATED bounds check (a HOST-
// SAFETY stand-in for the real unbounded stack allocation - this project
// has no literal stack guard to overrun). 1 string + 1 0-based index +
// 1 single-character string. See EngineState::SetCharInString's own doc
// comment (engine_state.h) for the full citation, including the CHOSEN
// 1024 cap and the CHOSEN space-fill construction. The "either string
// argument non-a-string reads through null" defect is already avoided
// by construction via this file's own NULL-safe argString() (same
// precedent as team_make_hostile, Sec38.2) - nothing further to guard.
int stub_set_char_in_string(lua_State* L) {
    logCall(L, upLog(L), "set_char_in_string", upStateTag(L));
    std::string base = argString(L, 1);
    int64_t idx = truncateEa2596(lua_tonumber(L, 2)); // 0-based (CONFIRMED); refuses NaN/out-of-range per existing convention
    std::string ch = argString(L, 3);
    EngineState* st = upState(L);
    auto& scs = st->setCharInString();
    if (idx < 0 || idx > EngineState::SetCharInString::kMaxIndex) {
        ++scs.indexOutOfBoundsGuardCount; // HOST-SAFETY, see comment above
        logCall(L, upLog(L), "set_char_in_string:INDEX_OUT_OF_BOUNDS_GUARD", upStateTag(L));
        lua_pushstring(L, base.c_str()); // CHOSEN: return the original string unmodified rather than build an unbounded result
        return 1;
    }
    // CHOSEN, labelled construction (not a spec fact beyond the crash
    // shape itself): pad with spaces to reach `idx`, then write the given
    // character there.
    if (base.size() <= static_cast<size_t>(idx)) {
        base.resize(static_cast<size_t>(idx) + 1, ' ');
    }
    base[static_cast<size_t>(idx)] = ch.empty() ? '\0' : ch[0]; // CHOSEN fallback for a non-single-char/absent 3rd argument
    lua_pushstring(L, base.c_str());
    return 1;
}

// store_stronghold_game_purchase_upgrade (Sec49.1, `ui`) - MANDATED
// faithful quirk (charge-then-no-op) PLUS a MANDATED, separate crash
// guard (no local-player null check). No arguments. See
// EngineState::StrongholdPurchaseUpgrade's own doc comment
// (engine_state.h) for the full citation, including the reconciliation
// against Sec8.27/Sec26.28's own CONFIRMED-false `game_get_is_host`
// verdict - NOT "fixed" into an actual upgrade; reproduced exactly.
int stub_store_stronghold_game_purchase_upgrade(lua_State* L) {
    logCall(L, upLog(L), "store_stronghold_game_purchase_upgrade", upStateTag(L));
    EngineState* st = upState(L);
    auto& su = st->strongholdPurchaseUpgrade();
    if (!su.strongholdResolves) return 0; // CHOSEN default: no ambient stronghold context resolved - nothing to charge or upgrade
    if (!st->hasLocalPlayer().get()) {    // OPEN until set - separate CONFIRMED crash shape (no local-player null check)
        ++su.localPlayerNullGuardCount;   // HOST-SAFETY, see comment above
        logCall(L, upLog(L), "store_stronghold_game_purchase_upgrade:NULL_LOCAL_PLAYER_GUARD", upStateTag(L));
        return 0;
    }
    st->playerCash() -= su.price; // CONFIRMED: charged unconditionally once a stronghold resolves, no affordability check
    ++su.chargeCount;
    // CONFIRMED (Sec49.1, reconciled against Sec8.27/Sec26.28): the
    // level-up setter's own gate - a session exists AND this machine is
    // host - is CONFIRMED false in single player (no session is ever
    // installed, EngineState::coopLocalIsHost()'s own doc comment) - so it
    // silently no-ops here, every time. Net effect: cash is always
    // deducted, the stronghold level never changes, in this host as in
    // single-player retail play.
    bool present = st->coopSession().present.get();                 // OPEN until set
    bool hostBranch = present && st->coopSession().localIsHost.get(); // OPEN until set, only read when present
    if (!hostBranch) {
        ++su.levelUpGateFailedNoopCount;
    }
    // hostBranch == true is unreachable in single player (Sec26.28) but
    // left as a structural branch rather than collapsed, in case a future
    // co-op host integration sets coopSession() present+host - the real
    // stronghold-level-up mechanics themselves are OPEN beyond the gate
    // (not given in the condensed spec text), so nothing further is
    // modeled on that branch either.
    return 0;
}

// squad_enable (Sec49.3, `gameplay`) - MANDATED faithful quirk
// (asymmetric enable/disable). 1 character name + 1 boolean (CONFIRMED
// default false). See EngineState::SquadMember's own doc comment
// (engine_state.h) for the full citation - NOT "fixed" into real
// inverses; reproduced exactly.
int stub_squad_enable(lua_State* L) {
    logCall(L, upLog(L), "squad_enable", upStateTag(L));
    std::string name = argString(L, 1);
    bool enable = lua_gettop(L) >= 2 ? (lua_toboolean(L, 2) != 0) : false; // CONFIRMED default false
    EngineState* st = upState(L);
    auto& member = st->squadMember(name);
    if (!enable) {
        // CONFIRMED: actually dismisses the character - restores stats,
        // removes it from the roster, releases resources. Stat-restore/
        // resource-release internals are OPEN beyond the fact they
        // happen - only the roster-membership bit is modeled.
        member.inRoster = false;
        ++member.dismissCount;
    } else {
        // CONFIRMED real asymmetric-effect defect (Sec49.3): only flips
        // two unrelated flag bits - does NOT re-add the character to the
        // crew. `inRoster` is deliberately left untouched here.
        member.flagBitA = !member.flagBitA; // CHOSEN toggle semantics ("flips", per spec's own word) - real bit meanings OPEN
        member.flagBitB = !member.flagBitB;
        ++member.enableTrueCount;
    }
    return 0;
}

// =======================================================================
// Ranking tranches 15/17/20/22/23 (spec-lua-api-behaviour.md Sec52/Sec51/
// Sec53/Sec55/Sec57), real-hit names only (2026-10-03, orchestrator-
// directed pass): across the full Sec50-Sec59 backlog (10 sections, the
// entire remaining 554-name ranking backlog except tranche 16/Sec49, done
// separately), a fresh mission drive against the real cache found exactly
// 8 names with a real, non-zero call count. Every other name in those 10
// sections stays an ordinary generic logging stub this pass - see the
// implementing commit's own message for the full per-section tally.
// =======================================================================

// tutorial_lock (Sec52.3, tranche 15, 0x00a60??? not individually dumped
// this pass - see engine_state.h's own doc comment on tutorialLockCount()/
// recordTutorialLock() for the full citation and the HOST-SAFETY-adjacent
// "no invented state code" reasoning). 1 mandatory string (tutorial name) -
// real mission-drive call site: argc=1, type=string. No return stated;
// HIGH CONFIDENCE "none", matching this family's own setter/query split
// (tutorial_unlock, Sec24.11, is also "none").
int stub_tutorial_lock(lua_State* L) {
    logCall(L, upLog(L), "tutorial_lock", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    EngineState::TutorialLookup lookup = EngineState::tutorialLookup(name);
    // CONFIRMED (Sec52.3): only indices up to 188 take effect; 189-209 (and
    // an unresolved name, index -1) are a silent no-op.
    if (lookup.index >= 0 && lookup.index <= 188) {
        st->recordTutorialLock(lookup.index);
    }
    return 0;
}

// sfx_use_load_images (Sec49.4, `ui`) - MANDATED faithful quirk (trivial
// hardcoded constant). No modeled state needed - see engine_state.h's
// own note next to EngineState::ScreenCapturePreview.
int stub_sfx_use_load_images(lua_State* L) {
    logCall(L, upLog(L), "sfx_use_load_images", upStateTag(L));
    lua_pushboolean(L, 1); // CONFIRMED hardcoded true (0x0149365c, Sec49.4/Sec26.24)
    return 1;
}

// screen_capture_preview_should_upload (Sec49.4, `ui`) - MANDATED
// faithful quirk PLUS a MANDATED, explicitly labelled CHOSEN stand-in for
// the unmodeled platform-privilege fork. 1 boolean, 0 Lua return values
// either way (CONFIRMED). See EngineState::ScreenCapturePreview's own
// doc comment (engine_state.h) for the full citation.
int stub_screen_capture_preview_should_upload(lua_State* L) {
    logCall(L, upLog(L), "screen_capture_preview_should_upload", upStateTag(L));
    bool wantUpload = lua_toboolean(L, 1) != 0; // bare boolean (CONFIRMED arg)
    EngineState* st = upState(L);
    auto& sc = st->screenCapturePreview();
    if (!wantUpload) {
        ++sc.explicitFalseCancelCount; // CONFIRMED: explicit false -> the "cancel" path
        logCall(L, upLog(L), "screen_capture_preview_should_upload:CANCEL_EXPLICIT_FALSE", upStateTag(L));
        return 0; // CONFIRMED: 0 Lua return values either way
    }
    // CHOSEN (not a spec fact, explicitly labelled): this project has no
    // real platform-privilege layer anywhere in scope to query, so the
    // "platform denied" fork the spec describes (which would silently
    // take the SAME cancel path above, indistinguishable from the
    // caller's own point of view - 0 return values either way) is modeled
    // as ALWAYS "not denied" here - a stated simplification, never a
    // confirmed value.
    ++sc.uploadProceedCount;
    return 0; // CONFIRMED: 0 Lua return values either way
}

// radio_newsbreak_clear (Sec51.3, tranche 17). No arguments - real
// mission-drive call site: argc=0. No return stated. CONFIRMED only as "a
// straightforward local-only state change" - see engine_state.h's own
// radioNewsbreakActive() doc comment for the minimal-model reasoning.
int stub_radio_newsbreak_clear(lua_State* L) {
    logCall(L, upLog(L), "radio_newsbreak_clear", upStateTag(L));
    upState(L)->radioNewsbreakActive().set(false);
    return 0;
}

// group_create_do / group_create_hidden_do (Sec53.3, tranche 20). 1
// mandatory string (group name) - real mission-drive call sites: argc=1,
// type=string, both. No return stated. CONFIRMED: "the same underlying
// creation routine differing only in one passed literal, and creation
// happens exactly once" - see engine_state.h's own groupAlreadyCreated()/
// markGroupCreated() doc comment.
int stub_group_create_do(lua_State* L) {
    logCall(L, upLog(L), "group_create_do", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->groupAlreadyCreated(name)) st->markGroupCreated(name);
    return 0;
}

int stub_group_create_hidden_do(lua_State* L) {
    logCall(L, upLog(L), "group_create_hidden_do", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->groupAlreadyCreated(name)) st->markGroupCreated(name);
    return 0;
}

// dlc2_m02_clapboards_get / dlc2_m02_clapboards_reset (Sec55.5, tranche
// 22). `_get`: 1 number (1-based clapboard index) - real mission-drive
// call site: argc=1, type=number. `_reset`: 1 number (count) - real
// mission-drive call site: argc=1, type=number. Neither return is stated
// beyond `_get`'s own documented "no value at all when out of range";
// `_get` in range pushes the boolean flag byte (see engine_state.h's own
// dlc2ClapboardsGet() doc comment for the full CONFIRMED shape and the
// HOST-SAFETY lower-bound deviation; `_set` is a zero-hit sibling and
// stays a generic stub this pass, per this task's own scope rule).
int stub_dlc2_m02_clapboards_reset(lua_State* L) {
    logCall(L, upLog(L), "dlc2_m02_clapboards_reset", upStateTag(L));
    int32_t rawCount = static_cast<int32_t>(roundToIntOpenMode(lua_tonumber(L, 1)));
    upState(L)->dlc2ClapboardsReset(rawCount);
    return 0;
}

int stub_dlc2_m02_clapboards_get(lua_State* L) {
    logCall(L, upLog(L), "dlc2_m02_clapboards_get", upStateTag(L));
    int32_t clapboardNumber = static_cast<int32_t>(roundToIntOpenMode(lua_tonumber(L, 1)));
    int result = upState(L)->dlc2ClapboardsGet(clapboardNumber);
    if (result < 0) return 0; // CONFIRMED/HOST-SAFETY: no value at all (nil), not false
    lua_pushboolean(L, result);
    return 1;
}

// cutscene_play_do / cutscene_play_check_done (Sec57.2, tranche 23) - see
// EngineState::cutscenePlayDo()'s own doc comment in lua_cutscene.cpp for
// the full design. `_do`: real mission-drive call site argc=5, types
// string,nil,table,nil,nil (name, the read-and-discarded 2nd argument, the
// destination table, 2 further arguments this spec gives no behaviour for
// and the one real call site never populates). `_check_done`: real
// mission-drive call site argc=0 (the single highest hit count of this
// whole 8-name batch, 12121 calls - this pair is where almost every
// mission in this project's own mission-drive baseline parks, per
// "waiting_at=...cutscene_play" in the fresh verdict TSV this pass ran).
int stub_cutscene_play_do(lua_State* L) {
    logCall(L, upLog(L), "cutscene_play_do", upStateTag(L));
    std::string name = argString(L, 1);
    // Arg 2 (optional table-or-boolean): CONFIRMED "read and discarded" -
    // never used for any decision, so not read at all here (functionally
    // identical to reading then ignoring it).
    std::vector<std::string> destinations = readCharacterNameArray(L, 3); // CONFIRMED-shape "a Lua table" reader, Sec2.9's own idiom
    // Args 4/5: no behaviour given by Sec57.2 beyond this point; the one
    // real call site passes nil for both, so nothing further to read.
    upState(L)->cutscenePlayDo(name, std::move(destinations));
    return 0;
}

int stub_cutscene_play_check_done(lua_State* L) {
    logCall(L, upLog(L), "cutscene_play_check_done", upStateTag(L));
    lua_pushboolean(L, upState(L)->cutscenePlayCheckDone() ? 1 : 0);
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
        "action_sequence_end",
        "audio_set_listener_override",
        "audio_clear_listener_override",
        "audio_play_for_navpoint",
        "audio_any_conversation_playing",
        "boss_battle_matt_begin",
        "auto_pickup_disable",
        "waiting_for_player_dialog",
        // Batch 2026-10-02: spec-lua-api-behaviour.md Sec31 (pause-map
        // stag-mode/district-control family, 5 names, `ui` tag) and Sec32
        // (ranking tranche 04, 25 names, `gameplay` tag) - 30 total, both
        // tags grepped directly from tools/lua_all_registered_1490_tagged.txt.
        "pause_map_stag_current_district_control",
        "pause_map_is_stag_mode",
        "pause_map_is_tutorial_mode",
        "pause_map_set_gps",
        "pause_map_stag_takeover_do_reward",
        "store_interface_is_active",
        "spawn_region_max_spawn_dist",
        "spawn_region_max_spawn_dist_reset",
        "set_ped_override_density",
        "pause_map_tutorial_mode",
        "set_time_of_day",
        "satellite_weapon_mode_exit",
        "set_seatbelt_flag",
        "set_trailing_aim_flag",
        "set_never_turn_on_player",
        "player_revive",
        "player_warp_to_shore_disable",
        "skydive_setup_tank_bailout",
        "qte_human_is_used",
        "party_add_do",
        "npc_is_in_party",
        "npc_go_idle",
        "object_destroy",
        "object_indicator_remove_do",
        "minimap_icon_remove_do",
        "shop_enable_nearest",
        "item_show",
        "item_anim_play",
        "radio_set_station",
        "helicopter_shoot_vehicle",
        // Batch 2026-10-02: spec-lua-api-behaviour.md Sec35 (the
        // `teleport_coop` investigation) - all 4 `gameplay` in
        // tools/lua_all_registered_1490_tagged.txt (grepped directly,
        // lines 523/843/858/948).
        "teleport_check_done",
        "turn_to_check_done",
        "move_to_check_done",
        "vehicle_pathfind_check_done",
        // Batch 2026-10-02: spec-lua-api-behaviour.md Sec38/Sec39/Sec40
        // ("ranking tranches 06/07/08"), 16 of their combined 75 names -
        // see this file's own per-tranche batch header comments above for
        // which and why. Real cluster tags verified directly against
        // tools/lua_all_registered_1490_tagged.txt (grepped, not assumed):
        // team_make_hostile/vehicle_engine_check_running/skydive_move_
        // to_do are `gameplay`; every other name below is `ui`.
        "vcust_preview_wheel_sizing",
        "team_make_hostile",
        "vehicle_engine_check_running",
        "store_weapon_purchase_ammo",
        "skydive_move_to_do",
        "store_gang_show_question_marks",
        "store_gallery_download_hide_list",
        "store_common_rotate_mouse_drag",
        "save_system_save_game",
        "save_system_load_game",
        "save_system_cancel_coop_load",
        "pcu_is_bra_category",
        "pcu_is_underwear_category",
        "pcu_purchase_slot",
        "pcu_purchase_outfit",
        "pcu_wear_store_outfit",
        // Batch 2026-10-02 (resumed session): spec-lua-api-behaviour.md
        // Sec3.7/Sec20.1 vehicle-invulnerability bit-conflict resolution
        // (Sec46.1) plus a curated subset of ranking tranches 12-14
        // (Sec44/Sec45/Sec46) - see this file's own batch header comment
        // above (right before stub_turn_invulnerable) for the full
        // selection reasoning and the list of tranche-12/13/14 names
        // deliberately NOT included this pass. All 30 verified `gameplay`/
        // `ui` directly against tools/lua_all_registered_1490_tagged.txt
        // (grepped, not assumed): turn_invulnerable/turn_vulnerable/
        // vehicle_set_vulnerable/vehicle_is_invulnerable/character_fake_
        // revival_start/character_fake_revival_end/character_take_human_
        // shield_check_done/vehicle_turret_base_to_do/vehicle_lights_on/
        // vehicle_tire_indicators_alive/flee_to_navpoint/character_hidden/
        // ambient_gang_spawn_enable/cellphone_animate_stop_do/ambient_cop_
        // spawn_enable/action_nodes_shouldnt_flee/action_nodes_restrict_
        // spawning/vehicle_set_tire_durability/vehicle_set_tire_damage_
        // multiplier are `gameplay`; game_is_pc_dx11/game_get_ps3_button_
        // swap/game_record_mode_is_supported/game_record_mode_is_active/
        // game_show_party_ui/game_show_community_sessions_ui/cell_camera_
        // enable/cell_camera_is_enabled/whored_countdown_finished are `ui`.
        "turn_invulnerable",
        "turn_vulnerable",
        "vehicle_set_vulnerable",
        "vehicle_is_invulnerable",
        "character_fake_revival_start",
        "character_fake_revival_end",
        "character_take_human_shield_check_done",
        "vehicle_lights_on",
        "vehicle_tire_indicators_alive",
        "vehicle_turret_base_to_do",
        "game_is_pc_dx11",
        "game_get_ps3_button_swap",
        "game_record_mode_is_supported",
        "game_record_mode_is_active",
        "game_show_party_ui",
        "game_show_community_sessions_ui",
        "flee_to_navpoint",
        "character_hidden",
        "ambient_gang_spawn_enable",
        "cellphone_animate_stop_do",
        "cell_camera_enable",
        "cell_camera_is_enabled",
        "ambient_cop_spawn_enable",
        "action_nodes_shouldnt_flee",
        "action_nodes_restrict_spawning",
        "whored_countdown_finished",
        "vehicle_set_tire_durability",
        "vehicle_set_tire_damage_multiplier",
        // Batch 2026-10-03: spec-lua-api-behaviour.md Sec49 ("ranking
        // tranche 16"), 9 of its 25 names (the 8 explicitly mandated
        // crash guards/faithful quirks, PLUS spawning_boats - the one
        // name this batch's own fresh mission-drive run found a real
        // measured hit for - see this file's own Sec49 batch header
        // comment above for which and why). Real cluster tags verified
        // directly against tools/lua_all_registered_1490_tagged.txt
        // (grepped, not assumed): spawn_override_set_override_category_
        // for_hood/spawning_boats/skydive_move_to_check_done/squad_enable
        // are `gameplay`; every other name below is `ui`.
        "shop_purchase_purchase_shop",
        "spawn_override_set_override_category_for_hood",
        "spawning_boats",
        "skydive_move_to_check_done",
        "set_char_in_string",
        "store_stronghold_game_purchase_upgrade",
        "squad_enable",
        "sfx_use_load_images",
        "screen_capture_preview_should_upload",
        // Ranking tranches 15/17/20/22/23 real-hit batch (2026-10-03) - the
        // 8 names with a real, non-zero mission-drive call count out of
        // the whole Sec50-Sec59 backlog (see this file's own doc comment
        // above each stub for the per-name citation).
        "tutorial_lock",
        "radio_newsbreak_clear",
        "group_create_do",
        "group_create_hidden_do",
        "dlc2_m02_clapboards_get",
        "dlc2_m02_clapboards_reset",
        "cutscene_play_do",
        "cutscene_play_check_done",
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
    if (wants("action_sequence_end")) registerOne(L, state, log, stateTag, "action_sequence_end", openGuard<stub_action_sequence_end>);
    if (wants("audio_set_listener_override")) registerOne(L, state, log, stateTag, "audio_set_listener_override", openGuard<stub_audio_set_listener_override>);
    if (wants("audio_clear_listener_override")) registerOne(L, state, log, stateTag, "audio_clear_listener_override", openGuard<stub_audio_clear_listener_override>);
    if (wants("audio_play_for_navpoint")) registerOne(L, state, log, stateTag, "audio_play_for_navpoint", openGuard<stub_audio_play_for_navpoint>);
    if (wants("audio_any_conversation_playing")) registerOne(L, state, log, stateTag, "audio_any_conversation_playing", openGuard<stub_audio_any_conversation_playing>);
    if (wants("boss_battle_matt_begin")) registerOne(L, state, log, stateTag, "boss_battle_matt_begin", openGuard<stub_boss_battle_matt_begin>);
    if (wants("auto_pickup_disable")) registerOne(L, state, log, stateTag, "auto_pickup_disable", openGuard<stub_auto_pickup_disable>);
    if (wants("waiting_for_player_dialog")) registerOne(L, state, log, stateTag, "waiting_for_player_dialog", openGuard<stub_waiting_for_player_dialog>);
    if (wants("pause_map_stag_current_district_control")) registerOne(L, state, log, stateTag, "pause_map_stag_current_district_control", openGuard<stub_pause_map_stag_current_district_control>);
    if (wants("pause_map_is_stag_mode")) registerOne(L, state, log, stateTag, "pause_map_is_stag_mode", openGuard<stub_pause_map_is_stag_mode>);
    if (wants("pause_map_is_tutorial_mode")) registerOne(L, state, log, stateTag, "pause_map_is_tutorial_mode", openGuard<stub_pause_map_is_tutorial_mode>);
    if (wants("pause_map_set_gps")) registerOne(L, state, log, stateTag, "pause_map_set_gps", openGuard<stub_pause_map_set_gps>);
    if (wants("pause_map_stag_takeover_do_reward")) registerOne(L, state, log, stateTag, "pause_map_stag_takeover_do_reward", openGuard<stub_pause_map_stag_takeover_do_reward>);
    if (wants("store_interface_is_active")) registerOne(L, state, log, stateTag, "store_interface_is_active", openGuard<stub_store_interface_is_active>);
    if (wants("spawn_region_max_spawn_dist")) registerOne(L, state, log, stateTag, "spawn_region_max_spawn_dist", openGuard<stub_spawn_region_max_spawn_dist>);
    if (wants("spawn_region_max_spawn_dist_reset")) registerOne(L, state, log, stateTag, "spawn_region_max_spawn_dist_reset", openGuard<stub_spawn_region_max_spawn_dist_reset>);
    if (wants("set_ped_override_density")) registerOne(L, state, log, stateTag, "set_ped_override_density", openGuard<stub_set_ped_override_density>);
    if (wants("pause_map_tutorial_mode")) registerOne(L, state, log, stateTag, "pause_map_tutorial_mode", openGuard<stub_pause_map_tutorial_mode>);
    if (wants("set_time_of_day")) registerOne(L, state, log, stateTag, "set_time_of_day", openGuard<stub_set_time_of_day>);
    if (wants("satellite_weapon_mode_exit")) registerOne(L, state, log, stateTag, "satellite_weapon_mode_exit", openGuard<stub_satellite_weapon_mode_exit>);
    if (wants("set_seatbelt_flag")) registerOne(L, state, log, stateTag, "set_seatbelt_flag", openGuard<stub_set_seatbelt_flag>);
    if (wants("set_trailing_aim_flag")) registerOne(L, state, log, stateTag, "set_trailing_aim_flag", openGuard<stub_set_trailing_aim_flag>);
    if (wants("set_never_turn_on_player")) registerOne(L, state, log, stateTag, "set_never_turn_on_player", openGuard<stub_set_never_turn_on_player>);
    if (wants("player_revive")) registerOne(L, state, log, stateTag, "player_revive", openGuard<stub_player_revive>);
    if (wants("player_warp_to_shore_disable")) registerOne(L, state, log, stateTag, "player_warp_to_shore_disable", openGuard<stub_player_warp_to_shore_disable>);
    if (wants("skydive_setup_tank_bailout")) registerOne(L, state, log, stateTag, "skydive_setup_tank_bailout", openGuard<stub_skydive_setup_tank_bailout>);
    if (wants("qte_human_is_used")) registerOne(L, state, log, stateTag, "qte_human_is_used", openGuard<stub_qte_human_is_used>);
    if (wants("party_add_do")) registerOne(L, state, log, stateTag, "party_add_do", openGuard<stub_party_add_do>);
    if (wants("npc_is_in_party")) registerOne(L, state, log, stateTag, "npc_is_in_party", openGuard<stub_npc_is_in_party>);
    if (wants("npc_go_idle")) registerOne(L, state, log, stateTag, "npc_go_idle", openGuard<stub_npc_go_idle>);
    if (wants("object_destroy")) registerOne(L, state, log, stateTag, "object_destroy", openGuard<stub_object_destroy>);
    if (wants("object_indicator_remove_do")) registerOne(L, state, log, stateTag, "object_indicator_remove_do", openGuard<stub_object_indicator_remove_do>);
    if (wants("minimap_icon_remove_do")) registerOne(L, state, log, stateTag, "minimap_icon_remove_do", openGuard<stub_minimap_icon_remove_do>);
    if (wants("shop_enable_nearest")) registerOne(L, state, log, stateTag, "shop_enable_nearest", openGuard<stub_shop_enable_nearest>);
    if (wants("item_show")) registerOne(L, state, log, stateTag, "item_show", openGuard<stub_item_show>);
    if (wants("item_anim_play")) registerOne(L, state, log, stateTag, "item_anim_play", openGuard<stub_item_anim_play>);
    if (wants("radio_set_station")) registerOne(L, state, log, stateTag, "radio_set_station", openGuard<stub_radio_set_station>);
    if (wants("helicopter_shoot_vehicle")) registerOne(L, state, log, stateTag, "helicopter_shoot_vehicle", openGuard<stub_helicopter_shoot_vehicle>);
    if (wants("teleport_check_done")) registerOne(L, state, log, stateTag, "teleport_check_done", openGuard<stub_teleport_check_done>);
    if (wants("turn_to_check_done")) registerOne(L, state, log, stateTag, "turn_to_check_done", openGuard<stub_turn_to_check_done>);
    if (wants("move_to_check_done")) registerOne(L, state, log, stateTag, "move_to_check_done", openGuard<stub_move_to_check_done>);
    if (wants("vehicle_pathfind_check_done")) registerOne(L, state, log, stateTag, "vehicle_pathfind_check_done", openGuard<stub_vehicle_pathfind_check_done>);

    // Batch 2026-10-02: spec-lua-api-behaviour.md Sec38/Sec39/Sec40 ("ranking tranches 06/07/08"), 16 names.
    if (wants("vcust_preview_wheel_sizing")) registerOne(L, state, log, stateTag, "vcust_preview_wheel_sizing", openGuard<stub_vcust_preview_wheel_sizing>);
    if (wants("team_make_hostile")) registerOne(L, state, log, stateTag, "team_make_hostile", openGuard<stub_team_make_hostile>);
    if (wants("vehicle_engine_check_running")) registerOne(L, state, log, stateTag, "vehicle_engine_check_running", openGuard<stub_vehicle_engine_check_running>);
    if (wants("store_weapon_purchase_ammo")) registerOne(L, state, log, stateTag, "store_weapon_purchase_ammo", openGuard<stub_store_weapon_purchase_ammo>);
    if (wants("skydive_move_to_do")) registerOne(L, state, log, stateTag, "skydive_move_to_do", openGuard<stub_skydive_move_to_do>);
    if (wants("store_gang_show_question_marks")) registerOne(L, state, log, stateTag, "store_gang_show_question_marks", openGuard<stub_store_gang_show_question_marks>);
    if (wants("store_gallery_download_hide_list")) registerOne(L, state, log, stateTag, "store_gallery_download_hide_list", openGuard<stub_store_gallery_download_hide_list>);
    if (wants("store_common_rotate_mouse_drag")) registerOne(L, state, log, stateTag, "store_common_rotate_mouse_drag", openGuard<stub_store_common_rotate_mouse_drag>);
    if (wants("save_system_save_game")) registerOne(L, state, log, stateTag, "save_system_save_game", openGuard<stub_save_system_save_game>);
    if (wants("save_system_load_game")) registerOne(L, state, log, stateTag, "save_system_load_game", openGuard<stub_save_system_load_game>);
    if (wants("save_system_cancel_coop_load")) registerOne(L, state, log, stateTag, "save_system_cancel_coop_load", openGuard<stub_save_system_cancel_coop_load>);
    if (wants("pcu_is_bra_category")) registerOne(L, state, log, stateTag, "pcu_is_bra_category", openGuard<stub_pcu_is_bra_category>);
    if (wants("pcu_is_underwear_category")) registerOne(L, state, log, stateTag, "pcu_is_underwear_category", openGuard<stub_pcu_is_underwear_category>);
    if (wants("pcu_purchase_slot")) registerOne(L, state, log, stateTag, "pcu_purchase_slot", openGuard<stub_pcu_purchase_slot>);
    if (wants("pcu_purchase_outfit")) registerOne(L, state, log, stateTag, "pcu_purchase_outfit", openGuard<stub_pcu_purchase_outfit>);
    if (wants("pcu_wear_store_outfit")) registerOne(L, state, log, stateTag, "pcu_wear_store_outfit", openGuard<stub_pcu_wear_store_outfit>);

    // Batch 2026-10-02 (resumed session): Sec3.7/Sec20.1/Sec46.1 vehicle-
    // invulnerability bit correction + curated Sec44/Sec45/Sec46 subset.
    if (wants("turn_invulnerable")) registerOne(L, state, log, stateTag, "turn_invulnerable", openGuard<stub_turn_invulnerable>);
    if (wants("turn_vulnerable")) registerOne(L, state, log, stateTag, "turn_vulnerable", openGuard<stub_turn_vulnerable>);
    if (wants("vehicle_set_vulnerable")) registerOne(L, state, log, stateTag, "vehicle_set_vulnerable", openGuard<stub_vehicle_set_vulnerable>);
    if (wants("vehicle_is_invulnerable")) registerOne(L, state, log, stateTag, "vehicle_is_invulnerable", openGuard<stub_vehicle_is_invulnerable>);
    if (wants("character_fake_revival_start")) registerOne(L, state, log, stateTag, "character_fake_revival_start", openGuard<stub_character_fake_revival_start>);
    if (wants("character_fake_revival_end")) registerOne(L, state, log, stateTag, "character_fake_revival_end", openGuard<stub_character_fake_revival_end>);
    if (wants("character_take_human_shield_check_done")) registerOne(L, state, log, stateTag, "character_take_human_shield_check_done", openGuard<stub_character_take_human_shield_check_done>);
    if (wants("vehicle_lights_on")) registerOne(L, state, log, stateTag, "vehicle_lights_on", openGuard<stub_vehicle_lights_on>);
    if (wants("vehicle_tire_indicators_alive")) registerOne(L, state, log, stateTag, "vehicle_tire_indicators_alive", openGuard<stub_vehicle_tire_indicators_alive>);
    if (wants("vehicle_turret_base_to_do")) registerOne(L, state, log, stateTag, "vehicle_turret_base_to_do", openGuard<stub_vehicle_turret_base_to_do>);
    if (wants("game_is_pc_dx11")) registerOne(L, state, log, stateTag, "game_is_pc_dx11", openGuard<stub_game_is_pc_dx11>);
    if (wants("game_get_ps3_button_swap")) registerOne(L, state, log, stateTag, "game_get_ps3_button_swap", openGuard<stub_game_get_ps3_button_swap>);
    if (wants("game_record_mode_is_supported")) registerOne(L, state, log, stateTag, "game_record_mode_is_supported", openGuard<stub_game_record_mode_is_supported>);
    if (wants("game_record_mode_is_active")) registerOne(L, state, log, stateTag, "game_record_mode_is_active", openGuard<stub_game_record_mode_is_active>);
    if (wants("game_show_party_ui")) registerOne(L, state, log, stateTag, "game_show_party_ui", openGuard<stub_game_show_party_ui>);
    if (wants("game_show_community_sessions_ui")) registerOne(L, state, log, stateTag, "game_show_community_sessions_ui", openGuard<stub_game_show_community_sessions_ui>);
    if (wants("flee_to_navpoint")) registerOne(L, state, log, stateTag, "flee_to_navpoint", openGuard<stub_flee_to_navpoint>);
    if (wants("character_hidden")) registerOne(L, state, log, stateTag, "character_hidden", openGuard<stub_character_hidden>);
    if (wants("ambient_gang_spawn_enable")) registerOne(L, state, log, stateTag, "ambient_gang_spawn_enable", openGuard<stub_ambient_gang_spawn_enable>);
    if (wants("cellphone_animate_stop_do")) registerOne(L, state, log, stateTag, "cellphone_animate_stop_do", openGuard<stub_cellphone_animate_stop_do>);
    if (wants("cell_camera_enable")) registerOne(L, state, log, stateTag, "cell_camera_enable", openGuard<stub_cell_camera_enable>);
    if (wants("cell_camera_is_enabled")) registerOne(L, state, log, stateTag, "cell_camera_is_enabled", openGuard<stub_cell_camera_is_enabled>);
    if (wants("ambient_cop_spawn_enable")) registerOne(L, state, log, stateTag, "ambient_cop_spawn_enable", openGuard<stub_ambient_cop_spawn_enable>);
    if (wants("action_nodes_shouldnt_flee")) registerOne(L, state, log, stateTag, "action_nodes_shouldnt_flee", openGuard<stub_action_nodes_shouldnt_flee>);
    if (wants("action_nodes_restrict_spawning")) registerOne(L, state, log, stateTag, "action_nodes_restrict_spawning", openGuard<stub_action_nodes_restrict_spawning>);
    if (wants("whored_countdown_finished")) registerOne(L, state, log, stateTag, "whored_countdown_finished", openGuard<stub_whored_countdown_finished>);
    if (wants("vehicle_set_tire_durability")) registerOne(L, state, log, stateTag, "vehicle_set_tire_durability", openGuard<stub_vehicle_set_tire_durability>);
    if (wants("vehicle_set_tire_damage_multiplier")) registerOne(L, state, log, stateTag, "vehicle_set_tire_damage_multiplier", openGuard<stub_vehicle_set_tire_damage_multiplier>);

    // Batch 2026-10-03: spec-lua-api-behaviour.md Sec49 ("ranking tranche 16"), 9 names.
    if (wants("shop_purchase_purchase_shop")) registerOne(L, state, log, stateTag, "shop_purchase_purchase_shop", openGuard<stub_shop_purchase_purchase_shop>);
    if (wants("spawn_override_set_override_category_for_hood")) registerOne(L, state, log, stateTag, "spawn_override_set_override_category_for_hood", openGuard<stub_spawn_override_set_override_category_for_hood>);
    if (wants("spawning_boats")) registerOne(L, state, log, stateTag, "spawning_boats", openGuard<stub_spawning_boats>);
    if (wants("skydive_move_to_check_done")) registerOne(L, state, log, stateTag, "skydive_move_to_check_done", openGuard<stub_skydive_move_to_check_done>);
    if (wants("set_char_in_string")) registerOne(L, state, log, stateTag, "set_char_in_string", openGuard<stub_set_char_in_string>);
    if (wants("store_stronghold_game_purchase_upgrade")) registerOne(L, state, log, stateTag, "store_stronghold_game_purchase_upgrade", openGuard<stub_store_stronghold_game_purchase_upgrade>);
    if (wants("squad_enable")) registerOne(L, state, log, stateTag, "squad_enable", openGuard<stub_squad_enable>);
    if (wants("sfx_use_load_images")) registerOne(L, state, log, stateTag, "sfx_use_load_images", openGuard<stub_sfx_use_load_images>);
    if (wants("screen_capture_preview_should_upload")) registerOne(L, state, log, stateTag, "screen_capture_preview_should_upload", openGuard<stub_screen_capture_preview_should_upload>);

    // Ranking tranches 15/17/20/22/23 real-hit batch (2026-10-03).
    if (wants("tutorial_lock")) registerOne(L, state, log, stateTag, "tutorial_lock", openGuard<stub_tutorial_lock>);
    if (wants("radio_newsbreak_clear")) registerOne(L, state, log, stateTag, "radio_newsbreak_clear", openGuard<stub_radio_newsbreak_clear>);
    if (wants("group_create_do")) registerOne(L, state, log, stateTag, "group_create_do", openGuard<stub_group_create_do>);
    if (wants("group_create_hidden_do")) registerOne(L, state, log, stateTag, "group_create_hidden_do", openGuard<stub_group_create_hidden_do>);
    if (wants("dlc2_m02_clapboards_get")) registerOne(L, state, log, stateTag, "dlc2_m02_clapboards_get", openGuard<stub_dlc2_m02_clapboards_get>);
    if (wants("dlc2_m02_clapboards_reset")) registerOne(L, state, log, stateTag, "dlc2_m02_clapboards_reset", openGuard<stub_dlc2_m02_clapboards_reset>);
    if (wants("cutscene_play_do")) registerOne(L, state, log, stateTag, "cutscene_play_do", openGuard<stub_cutscene_play_do>);
    if (wants("cutscene_play_check_done")) registerOne(L, state, log, stateTag, "cutscene_play_check_done", openGuard<stub_cutscene_play_check_done>);
}

} // namespace sr3luahost
