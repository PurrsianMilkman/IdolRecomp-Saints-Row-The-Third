// Shared infrastructure for the lua_spec_confirmed_stubs_*.cpp translation
// units (split 2026-10-07 from the former single 8,700+ line
// lua_spec_confirmed_stubs.cpp - a pure structural refactor, zero behaviour
// change; see lua_spec_confirmed_stubs_registry.cpp's own top comment for
// the full split rationale and shared-registration mechanism).
//
// Everything below was originally defined inline, once, inside that one
// file's own top-level anonymous namespace, and was visible to every stub
// in the file by ordinary enclosing-scope lookup. Splitting the stub bodies
// across multiple translation units means any helper a stub in MORE THAN
// ONE of those files calls can no longer live in a per-TU anonymous
// namespace (internal linkage - invisible outside its own TU): it has to
// move here instead, as a plain `inline` function (or, for the two
// `openGuard`/`openGuardCall` function templates, an ordinary template -
// templates don't need the keyword) directly in the `sr3luahost` namespace.
// Each lua_spec_confirmed_stubs_*.cpp still opens its OWN nested anonymous
// namespace for its own stub bodies and any helper only IT uses; unqualified
// lookup from inside that anonymous namespace still finds everything below
// via normal enclosing-namespace lookup, exactly as it did when all of this
// sat in the same anonymous namespace one scope further in - so every call
// site below is reproduced byte-for-byte from the original file with no
// behavioural change, only a change in which translation unit compiles it
// and (for the helpers below) the addition of `inline`.
//
// A helper that is only ever used by stub bodies that all ended up in the
// SAME split file was deliberately left there instead (see that file's own
// top comment for which) - this header holds only the helpers at least two
// of the split files actually call.
#pragma once

#include "sr3luahost/spec_confirmed_stubs.h"

#include "sr3luahost/bare_globals.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>

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

inline EngineState* upState(lua_State* L) {
    return static_cast<EngineState*>(lua_touserdata(L, lua_upvalueindex(1)));
}
inline HitLog* upLog(lua_State* L) {
    return static_cast<HitLog*>(lua_touserdata(L, lua_upvalueindex(2)));
}
inline const char* upStateTag(lua_State* L) {
    return lua_tostring(L, lua_upvalueindex(3));
}

// Same real-argc/real-arg-type logging convention every other stub in this
// project uses (stub_registry.cpp's genericStubTrampoline,
// lua_thread_scheduler.cpp's logCall) - folds these 12 names into the
// SAME whole-run HitLog ranking tools/lua_host_run.cpp already reports.
inline void logCall(lua_State* L, HitLog* log, const char* name, const char* state) {
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
inline bool caseInsensitiveEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

// The reserved "#CLOSEST_PLAYER#" sentinel (spec-lua-api-behaviour.md
// Sec3.9/Sec32.4, the "Name-resolution sentinels" front-matter note):
// CONFIRMED checked via a literal, case-insensitive string compare (the
// real engine's own __stricmp) directly inside the affected native's own
// target resolver - `ai_add_enemy_target` (0x00a3d180, Sec3.9: "checked
// via __stricmp directly inside the target resolver... as the reference
// point for the nearest-player distance search... closest to the ACTING
// CHARACTER") and `party_add_do`'s own per-follower leader-resolve
// fallback (Sec32.4: "picks whichever co-op player is nearest THAT
// SPECIFIC FOLLOWER") - redirecting, on a match, to "whichever co-op
// player is nearest" the relevant reference point, BEFORE falling back to
// the generic character-resolve chain on a mismatch.
//
// This project models single player only - no remote co-op roster is
// ever constructed here (the SAME scope restriction `player_names_get_all`
// (Sec51.4) already states and relies on: "this host models single player
// only... the table always holds exactly 1 entry"). With exactly one
// possible candidate, "nearest" is never actually contested by any real
// distance comparison - fabricating position data to run a vacuous
// one-candidate distance search would add no real information, only the
// APPEARANCE of fidelity this project's own discipline refuses. So this
// reduces, honestly, to this project's own single real player identity:
// the literal `"#PLAYER1#"` sentinel - the SAME `LOCAL_PLAYER` constant
// `spec-lua-api-behaviour.md` Sec13.6/Sec29.4 already establish as this
// project's canonical pre-resolved player name (`player_names_get_all`'s
// own stub, `stub_player_names_get_all`, reuses the identical literal).
//
// Kept as one shared helper (not inlined per call site) since at least
// two natives need the identical redirect, per this project's own
// "extend the existing dispatch, don't write a parallel one-off" house
// rule.
inline std::string resolveClosestPlayerSentinel(const std::string& name) {
    return caseInsensitiveEquals(name, "#CLOSEST_PLAYER#") ? "#PLAYER1#" : name;
}

// The 4 reserved "#FOLLOWER...#" sentinels (spec-lua-api-behaviour.md Sec3
// front matter, line ~1469-1470, "Name-resolution sentinels"; the 2nd-5th
// of the 8-sentinel CLOSED inventory commit 1681666 establishes by a
// whole-`.rdata` regex sweep plus a raw byte search - no 9th exists).
// CONFIRMED checked by `0x00a26010`, resolved through `0x00a26110` "to the
// Nth entry of a linked companion/homie list." Every function this
// document documents as resolving an argument through that SAME generic/
// `#PLAYER#`-then-`#FOLLOWER#` chain (`0x00a28150`) - or through
// `0x00a26010`/`0x00a26110` directly - implicitly accepts these 4 literals
// too; unlike `#CLOSEST_PLAYER#` (a per-function special case only two
// specific natives recognize, Sec3.9/Sec32.4), `#FOLLOWER#`-family
// acceptance is a property of the SHARED RESOLVER ITSELF, so every stub
// documented to call through it needs this same treatment, not a hand-
// picked subset.
//
// `isFollowerSentinelLiteral()` is a pure, state-free recognizer (same
// portable `__stricmp`-equivalent convention as `caseInsensitiveEquals()`
// itself - no spec text pins case-sensitivity specifically for
// `0x00a26010`, but every other sentinel-literal check in this document
// that DOES state its mechanism names the real CRT `__stricmp`, and this
// project applies that same convention uniformly rather than inventing a
// case-sensitive special case for just this one family).
//
// `resolveFollowerAwareObject()` is the actual drop-in replacement for a
// plain `st->objectResolves().get(name)` call at any such site: it tries
// the 4 literals first (sentinel path ALWAYS takes priority - an ordinary
// registry entry literally named "#FOLLOWER1#", e.g. a test calling
// `objectResolves().set("#FOLLOWER1#", true)`, must never be what makes
// resolution succeed, since the real engine never reaches the ordinary
// by-name registry lookup for these 4 strings at all - Sec3 front matter's
// own text: "checked... as a fallback" describes `#PLAYER#`'s OWN
// position in the chain, but `#FOLLOWER#` is checked by a SEPARATE,
// dedicated function, `0x00a26010`, not the generic by-name lookup
// `0x004588f0` any ordinary name goes through); falling back to the
// unchanged `objectResolves()` path for every name that isn't one of the
// 4 literals.
//
// On a literal match, the backing list and per-literal index are read from
// `EngineState::PartyState::followerSentinelIndex`/`membersByLeader` - see
// that field's own doc comment (engine_state.h) for the full citation
// trail on why EACH of the two open questions it describes (bare-vs-
// numbered aliasing; 0- vs 1-indexing) is modeled as its own independent
// OPEN slot rather than a derived formula, and why `membersByLeader` is
// used as the backing list (HIGH CONFIDENCE, not textually confirmed
// identical to `0x00a26010`'s own internal list).
//
// Three distinct outcomes, matching `get_team`'s own explicit 3-way
// account of this exact path (Sec20.23, item 2): (1) index known AND the
// list has a live entry there -> returns `true`, `resolvedName` is that
// entry's own name - CONFIRMED structural reading (`get_team`: "if the
// name matches AND resolves to a live follower, reads... off THAT
// HANDLE" - no further registry re-check, so this function does not run
// the resolved name back through `objectResolves()` either); (2) index
// known but the list has no such entry (e.g. an empty party) -> returns
// `false`, `resolvedName` left untouched - a genuine resolve FAILURE, not
// an OPEN question (`get_team`: "if the name matched the #FOLLOWER#
// pattern but resolution failed, pushes nil directly - does not fall
// through to the remaining stages" - so callers must NOT fall back to
// `objectResolves()` on a `false` return when `isFollowerSentinelLiteral()`
// is also true); (3) `name` is not one of the 4 literals at all -> this
// function is a pure passthrough to the ordinary, pre-existing
// `objectResolves().get(name)` check, `resolvedName` set to `name`
// unchanged - every call site's behavior for every non-sentinel name is
// byte-for-byte what it was before this helper existed.
//
// Throws OpenStateError (propagates through `openGuard`, same convention
// as every other OPEN-state read in this file - see the "Reading OPEN
// engine state" block above) if `name` IS one of the 4 literals but that
// specific literal's own index slot is still OPEN. This is the ONLY path
// by which this function can throw; a non-sentinel name never touches
// `followerSentinelIndex` at all, so an ordinary call site that happens
// never to be exercised with a literal sentinel argument sees no new
// failure mode from adding this helper.
inline bool isFollowerSentinelLiteral(const std::string& name) {
    static const char* const kFollowerLiterals[] = {"#FOLLOWER#", "#FOLLOWER1#", "#FOLLOWER2#", "#FOLLOWER3#"};
    for (const char* lit : kFollowerLiterals) {
        if (caseInsensitiveEquals(name, lit)) return true;
    }
    return false;
}

inline bool resolveFollowerAwareObject(EngineState* st, const std::string& name, std::string& resolvedName) {
    static const char* const kFollowerLiterals[] = {"#FOLLOWER#", "#FOLLOWER1#", "#FOLLOWER2#", "#FOLLOWER3#"};
    for (const char* lit : kFollowerLiterals) {
        if (!caseInsensitiveEquals(name, lit)) continue;
        int index = st->party().followerSentinelIndex.get(lit); // throws OpenStateError if this literal's own slot is unset
        auto& members = st->party().membersByLeader["#PLAYER1#"]; // CHOSEN backing list - see followerSentinelIndex's own doc comment
        if (index >= 0 && static_cast<size_t>(index) < members.size()) {
            resolvedName = members[static_cast<size_t>(index)];
            return true; // CONFIRMED structural reading, Sec20.23 get_team: reads directly off the resolved handle
        }
        return false; // CONFIRMED, Sec20.23 get_team: matched the pattern but resolution failed -> fails outright
    }
    resolvedName = name;
    return st->objectResolves().get(name); // OPEN until set - unchanged path for every non-sentinel name
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
inline int64_t truncateEa2596(lua_Number v) {
    double r = std::trunc(v);
    if (!(r >= -9223372036854775808.0 && r < 9223372036854775808.0)) {
        throw OpenStateError("0x00ea2596 conversion of a non-finite or out-of-range number",
                             "spec-lua-api-behaviour.md Sec4.1");
    }
    return static_cast<int64_t>(r);
}

// Former name, kept only so concurrent edits of the call sites below merge
// cleanly; the conversion is no longer OPEN (Sec4.1). Prefer truncateEa2596.
inline int64_t roundToIntOpenMode(lua_Number v) { return truncateEa2596(v); }

// Small helper matching this batch's own recurring "mandatory string,
// read via lua_tolstring, NULL-safe" shape.
inline std::string argString(lua_State* L, int idx) {
    const char* s = lua_tolstring(L, idx, nullptr);
    return s ? s : "";
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

// Shared nil-gated optional-boolean idiom (this batch's own recurring
// "standard nil-gated idiom"): absent or an explicit nil takes
// `defaultValue`; anything else goes through lua_toboolean.
inline bool optionalBoolDefault(lua_State* L, int idx, bool defaultValue) {
    if (lua_gettop(L) >= idx && lua_type(L, idx) != LUA_TNIL) return lua_toboolean(L, idx) != 0;
    return defaultValue;
}

// Shared nil-gated optional-number idiom, RAW (no rounding) - used where
// the entry's own text does not say the value goes through the round-
// cast pair.
inline double optionalNumberDefault(lua_State* L, int idx, double defaultValue) {
    if (lua_gettop(L) >= idx && lua_type(L, idx) != LUA_TNIL) return lua_tonumber(L, idx);
    return defaultValue;
}

// Shared nil-gated optional-string idiom (ranking tranche 04's own
// `shop_enable_nearest`/`item_anim_play` - Sec32.5/Sec32.6): absent or an
// explicit nil takes `defaultValue`; anything else goes through
// lua_tolstring (NULL-safe, same shape as argString above).
inline std::string optionalStringDefault(lua_State* L, int idx, const std::string& defaultValue) {
    if (lua_gettop(L) >= idx && lua_type(L, idx) != LUA_TNIL) return argString(L, idx);
    return defaultValue;
}

// 0x0083dff0 (ranking tranche 04's own `party_add_do`, Sec32.4): a table's
// own reported length - `t.n` if present (a numeric field named "n"), else
// counted via lua_next. -1 for a missing/nil argument (CONFIRMED per
// Sec30.5's own cross-reference to the same helper). This project's own
// portable stand-in for the real helper's exact internals - the real
// function's own body was not independently decompiled this pass.
inline int64_t tableLength(lua_State* L, int idx) {
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
inline std::vector<std::string> readCharacterNameArray(lua_State* L, int idx) {
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
inline OpenBits32& vehicleForceFlags1d7c1d7d(VehicleState& v) { return v.forceFlags; }
inline OpenBits32& vehicleForceFlags1d7aField(VehicleState& v) { return v.forceFlags1d7a; }

// Shared body for the single-vehicle force-flag setters below (Sec30.5,
// Sec20.1): resolve (reuses objectResolves(), Sec29's "one shared global
// name map" - see VehicleState's own doc comment, engine_state.h), then
// apply the double-gate: gate passes -> set the bit directly; gate fails ->
// replicate only (the no-op stand-in for the real opcode-0x46 record), no
// local bit write - CONFIRMED either/or shape (Sec30.5: "either true ->
// write the bit directly; both false -> open ... instead"). CONFIRMED
// (Sec3.7's own single-player resolution, 2026-10-03): the gate always
// passes with no session installed, so every vehicle takes the direct-flip
// branch from construction - see VehicleState::forceFlagGatePasses.
inline void applyVehicleForceFlagSetter(lua_State* L, const char* name, const std::string& vehicleName, bool flag,
                                         uint32_t bitMask,
                                         OpenBits32& (*field)(VehicleState&) = vehicleForceFlags1d7c1d7d) {
    EngineState* st = upState(L);
    if (!st->objectResolves().get(vehicleName)) return; // OPEN until set
    VehicleState& vehicle = st->getOrCreateVehicle(vehicleName);
    if (vehicle.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        field(vehicle).setBits(bitMask, flag ? bitMask : 0u);
    } else {
        EngineState::replicateStateChange(name, vehicleName);
    }
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

inline void registerOne(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                         const char* name, lua_CFunction fn) {
    lua_pushlightuserdata(L, &state);
    lua_pushlightuserdata(L, &log);
    lua_pushstring(L, stateTag.c_str());
    lua_pushcclosure(L, fn, 3);
    lua_setglobal(L, name);
}

// A per-section `registerBxx()` (declared in lua_spec_confirmed_stubs_
// sections.h, defined in each lua_spec_confirmed_stubs_*.cpp) takes this
// instead of the original `namesToRegister` vector + its own local `wants`
// lambda - same `wants("name")` call shape at every call site, zero
// behaviour change, but now callable from a `wants` built once in
// lua_spec_confirmed_stubs_registry.cpp and passed by reference into each
// section in turn.
using WantsPredicate = std::function<bool(const char*)>;

} // namespace sr3luahost
