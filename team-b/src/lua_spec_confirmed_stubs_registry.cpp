// lua_spec_confirmed_stubs_registry.cpp
//
// Structural-refactor split, 2026-10-07 (zero behaviour change). This file,
// plus lua_spec_confirmed_stubs_common.h, lua_spec_confirmed_stubs_
// sections.h and the 7 lua_spec_confirmed_stubs_b0N_*.cpp files, together
// replace the former single lua_spec_confirmed_stubs.cpp (8,700+ lines) -
// every native implementation this project has built against spec-lua-api-
// behaviour.md/spec-lua-bindings.md across roughly Sec1-Sec60 of that
// document's own numbering, all appended to the SAME translation unit one
// batch at a time. That file had become this project's single most
// heavily multi-agent-contended file: every new native implementation
// touched the SAME three append-points (the specConfirmedStubNames() name
// array, the registerSpecConfirmedStubs() registration calls, and the stub
// bodies themselves in between), so nearly every parallel-dispatch wave
// that touched Lua natives produced at least one merge conflict here.
//
// THE SPLIT: the file's own internal structure already grouped its stub
// bodies into comment-delimited batches (most marked with a "// =====...
// =====" banner, a handful of smaller ones with just a "// ---...---" box)
// - see each lua_spec_confirmed_stubs_b0N_*.cpp's own top comment for
// exactly which batches it holds. Those 16-odd original batches were
// combined, preserving their own original relative order and NEVER
// splitting one batch's own functions across two files, into 7
// similarly-sized (roughly 800-1500 line) files named b01..b07. A handful
// of helper functions that more than one of those 7 files' own stub bodies
// call (upState/upLog/upStateTag/logCall/caseInsensitiveEquals/
// truncateEa2596/roundToIntOpenMode/argString/optionalBoolDefault/
// optionalNumberDefault/optionalStringDefault/tableLength/
// readCharacterNameArray/the vehicle force-flag setter trio/the
// PendingLuaError type/the openGuard-family registration trampoline) moved
// to lua_spec_confirmed_stubs_common.h instead, as `inline` functions (see
// that header's own top comment for why) - a helper only one file's own
// stubs use stayed exactly where it was, inside that one file's own
// anonymous namespace.
//
// THE SHARED-REGISTRATION MECHANISM (the reason this registry.cpp file
// exists at all): every stub_* C function is still private to its own
// translation unit (defined inside that file's own anonymous namespace,
// internal linkage - unchanged from the original file, where every stub
// was equally private to IT). That means the actual `registerOne(...,
// openGuard<stub_whatever>)` call for a given stub can only be written in
// the SAME .cpp that defines stub_whatever - a different .cpp cannot even
// name it. So each lua_spec_confirmed_stubs_b0N_*.cpp now ALSO defines two
// small functions with external linkage, declared in
// lua_spec_confirmed_stubs_sections.h: `namesB0N()` (its own slice of the
// original name array, verbatim, in original order) and `registerB0N(L,
// state, log, stateTag, wants)` (its own slice of the original
// if-(wants(...))-registerOne(...) calls, verbatim, in original order,
// including the one exception to that one-call-per-name shape - the
// Screen_fade_transition_complete entry's extra attachScreenFadeUiState()/
// screenFadeInit() side effects - reproduced unchanged inside b01). THIS
// file implements the three PUBLIC functions declared in include/
// sr3luahost/spec_confirmed_stubs.h (specConfirmedStubNames(),
// registerSpecConfirmedStubs(), specBareGlobals()) as thin coordinators
// that call all 7 sections' own namesB0N()/registerB0N() in the ORIGINAL
// b01..b07 order and concatenate/sequence the results - so the observable
// behavior (the exact set of registered names, each one's own `wants()`
// gating, and registration order) is byte-for-byte identical to the
// original single-function version. `registerSpecConfirmedStubs()`'s own
// local `wants` predicate used to be a bare lambda closed over
// `namesToRegister`; it is now built the same way but stored in a
// `WantsPredicate` (`std::function<bool(const char*)>`, declared in
// lua_spec_confirmed_stubs_common.h) so the SAME predicate object can be
// passed by reference into all 7 sections in turn - same `wants("name")`
// call shape at every one of the original 302 call sites, so this is not a
// behaviour change, only a change in how the one predicate reaches each
// call site.
//
// THIS KEEPS THE PUBLIC API IDENTICAL: include/sr3luahost/
// spec_confirmed_stubs.h was NOT changed by this split - every caller
// outside this file (host.h/lua_host.cpp, the test suite) still calls
// exactly the same three functions with exactly the same signatures.
//
// FUTURE FAN-OUT (explicitly NOT done by this refactor - a separate,
// later task): the orchestrator's own plan is to let a wave of parallel
// agents each own one of these 7 files (or further new ones) for adding
// brand-new native implementations, without needing to touch a file
// another agent's own section also touches. This file - the one place
// that still lists all 7 sections by name - is the one spot a brand-new
// 8th (or Nth) section would need to add one more namesB0N()/registerB0N()
// pair to; adding new NAMES to an EXISTING section's own file never
// touches this one.
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"
#include "sr3luahost/thread_scheduler.h"

namespace sr3luahost {

const std::vector<SpecBareGlobal>& specBareGlobals() {
    // The 24 bare globals of 0x00e0f900, in roster order, into BOTH states
    // (spec-lua-bindings.md Sec13.2/Sec16.4 "Which state", CONFIRMED -
    // disassembly, job 20261001T020218-team-a-bgcx). Bodies:
    // lua_bare_globals.cpp (spec-lua-api-behaviour.md Sec26.27).
    static const std::vector<SpecBareGlobal> rows = [] {
        std::vector<SpecBareGlobal> out;
        for (const auto& e : bareGlobalRoster())
            out.push_back({e.name, true, true, "spec-lua-api-behaviour.md Sec13.2/Sec16.4; spec-lua-api-behaviour.md Sec26.27"});
        return out;
    }();
    return rows;
}

const std::vector<std::string>& specConfirmedStubNames() {
    // THE SINGLE SOURCE OF TRUTH for "every native name this project's Lua
    // host actually registers through a real, non-generic-stub
    // implementation" (as opposed to registerStubs()'s generic "log and
    // return nil" trampoline, stub_registry.cpp - a totally separate,
    // already-unified track driven by the tagged registration list, never
    // duplicated here). Three tracks, unioned (corrected 2026-10-08,
    // results/lua_natives_backlog_recount_20261008.md - a prior scoping
    // pass's own baseline missed the third one entirely, a real blind
    // spot: a genuinely active registration call, registerThreadClose(),
    // that nothing funnelled through this function at all):
    //   1. the 7 b01-b07 sections below (302 names).
    //   2. the 24 bare globals of 0x00e0f900 (bareGlobalRoster(),
    //      lua_bare_globals.cpp).
    //   3. thread_close (threadCloseNames(), lua_thread_scheduler.cpp) -
    //      the pre-spec ThreadScheduler scaffold's lone surviving member
    //      (thread_scheduler.h), registered by registerThreadClose() into
    //      BOTH states, same as the bare globals, but NOT folded into
    //      track 2's own roster: thread_close is explicitly NOT one of the
    //      disassembly-CONFIRMED "24 bare globals" (its own real engine
    //      existence is OPEN - see registerThreadClose()'s doc comment),
    //      so mixing it into that specific, already-tested-at-24 roster
    //      would misstate its provenance for no real benefit; a 4th,
    //      separately-sourced union component costs three lines here and
    //      keeps every existing "24" assertion in this codebase honest.
    // 302 + 24 + 1 = 327 was the count as of this comment's own last update
    // (b01-b07 only); b08/b09/b14/b12/b11/b10 and now b17/b18 (wave 2 batch
    // W2-6, 2026-10-08, 143 more names: see lua_spec_confirmed_stubs_
    // sections.h's own b17/b18 doc comments) have all appended further
    // sections since without this top-of-function count being kept in
    // sync - the real total is whatever specConfirmedStubNames().size()
    // actually returns, verified end-to-end (not just as C++ lists
    // agreeing with each other) by tests/synthetic_luahost_unified_
    // registry_test.cpp, which checks this return value against the REAL
    // Lua global tables of a real Host after construction.
    static const std::vector<std::string> names = [] {
        std::vector<std::string> out;
        auto append = [&out](const std::vector<std::string>& section) {
            out.insert(out.end(), section.begin(), section.end());
        };
        append(stub_sections::namesB01());
        append(stub_sections::namesB02());
        append(stub_sections::namesB03());
        append(stub_sections::namesB04());
        append(stub_sections::namesB05());
        append(stub_sections::namesB06());
        append(stub_sections::namesB07());
        append(stub_sections::namesB08());
        append(stub_sections::namesB09());
        // b14 (2026-10-08, wave 1 Batch 7 fan-out): appended after b01-b07,
        // same convention a brand-new Nth section always uses per this
        // file's own "FUTURE FAN-OUT" note above - a new name/registration
        // pair, never retrofitted into an existing b0N file.
        append(stub_sections::namesB14());
        append(stub_sections::namesB12());
        append(stub_sections::namesB11());
        append(stub_sections::namesB10());
        append(stub_sections::namesB15()); // 2026-10-08, wave 2 batch W2-1 (core dispatchers)
        // b16 (2026-10-08, wave 2 Batch W2-3 fan-out): Sec20+Sec21 gameplay
        // residue, 57 names - appended after b01-b14/b12/b11/b10, same
        // convention a brand-new Nth section always uses.
        append(stub_sections::namesB16());
        // b22/b23 (2026-10-08, wave 2 batch W2-4): appended after wave 1,
        // same convention a brand-new section always uses per this file's
        // own "FUTURE FAN-OUT" note above.
        append(stub_sections::namesB22());
        append(stub_sections::namesB23());
        // b26/b27 (2026-10-08, wave 2 Batch W2-7 fan-out): appended after
        // all wave-1 sections, same convention as every prior new section.
        append(stub_sections::namesB26());
        append(stub_sections::namesB27());
        // wave 2, Batch W2-5 (2026-10-08): appended after wave 1's b01-b14,
        // same convention as every prior brand-new section.
        append(stub_sections::namesB24());
        append(stub_sections::namesB25());
        // b17/b18 (2026-10-08, wave 2 batch W2-6, "UI frontend (143
        // natives) - HARDENING BATCH"): appended after b01-b14/b12/b11/b10/
        // b15, same convention a brand-new section always uses per this
        // file's own "FUTURE FAN-OUT" note above.
        append(stub_sections::namesB17());
        append(stub_sections::namesB18());
        // b19/b20 (2026-10-08, wave 2 batch W2-8): appended after b01-b14,
        // same convention a brand-new section always uses per this file's
        // own "FUTURE FAN-OUT" note above.
        append(stub_sections::namesB19());
        append(stub_sections::namesB20());
        // b21 (2026-10-08, wave 2 batch W2-2): appended after every wave-1
        // section, same convention a brand-new Nth section always uses per
        // this file's own "FUTURE FAN-OUT" note above.
        append(stub_sections::namesB21());
        // b28 (2026-10-08): the 6-native clearance fast-follow onto wave 1
        // batch 1 (on_dismiss/character_prevent_flinching/character_allow_
        // ragdoll/npc_combat_enable/inv_weapon_add_temporary/inv_weapon_
        // remove_temporary) - appended after every prior section, same
        // convention a brand-new Nth section always uses per this file's
        // own "FUTURE FAN-OUT" note above.
        append(stub_sections::namesB28());
        // b29 (2026-10-08): Sec6 backlog close-out, 10 names - appended
        // after every prior section, same convention a brand-new Nth
        // section always uses per this file's own "FUTURE FAN-OUT" note.
        append(stub_sections::namesB29());
        // b30 (2026-10-08): tranche-F close-out (character_kill,
        // vehicle_is_destroyed) + camera_restrict_set_active, 3 names.
        append(stub_sections::namesB30());
        // b31 (2026-10-08): continuous_spawn_start/on_vehicle_enter/
        // tutorial_start, 3 names.
        append(stub_sections::namesB31());
        return out;
    }();
    // The 24 bare globals (specBareGlobals(), lua_bare_globals.cpp) and
    // thread_close (threadCloseNames(), lua_thread_scheduler.cpp) are spec/
    // registered functions too, so tools and tests that walk this list
    // (the refusal stress test, the unified registry test) cover them;
    // Host registers the 24 through registerBareGlobals() and thread_close
    // through registerThreadClose(), never as generic stubs, and
    // registerSpecConfirmedStubs() leaves all 25 alone.
    static const std::vector<std::string> all = [] {
        std::vector<std::string> out = names;
        for (const auto& e : bareGlobalRoster()) out.push_back(e.name);
        for (const auto& n : threadCloseNames()) out.push_back(n);
        return out;
    }();
    return all;
}

void registerSpecConfirmedStubs(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                                 const std::vector<std::string>& namesToRegister) {
    WantsPredicate wants = [&namesToRegister](const char* name) {
        for (const auto& n : namesToRegister) {
            if (n == name) return true;
        }
        return false;
    };
    // Each call is a no-op unless `name` is really present in
    // `namesToRegister` - see this function's own doc comment
    // (spec_confirmed_stubs.h) for why this filter exists (these names do
    // not all share one real registered cluster tag, so a single Host
    // construction calls this function twice - once per state - each time
    // with only that state's own real subset). Calling all 7 sections in
    // their original b01..b07 order reproduces the original single
    // function's own registration order exactly, including the one
    // non-uniform entry (Screen_fade_transition_complete, inside b01 -
    // see that file's own registerB01()).
    stub_sections::registerB01(L, state, log, stateTag, wants);
    stub_sections::registerB02(L, state, log, stateTag, wants);
    stub_sections::registerB03(L, state, log, stateTag, wants);
    stub_sections::registerB04(L, state, log, stateTag, wants);
    stub_sections::registerB05(L, state, log, stateTag, wants);
    stub_sections::registerB06(L, state, log, stateTag, wants);
    stub_sections::registerB07(L, state, log, stateTag, wants);
    stub_sections::registerB08(L, state, log, stateTag, wants);
    stub_sections::registerB09(L, state, log, stateTag, wants);
    stub_sections::registerB14(L, state, log, stateTag, wants); // 2026-10-08, wave 1 Batch 7 fan-out
    stub_sections::registerB12(L, state, log, stateTag, wants);
    stub_sections::registerB11(L, state, log, stateTag, wants);
    stub_sections::registerB10(L, state, log, stateTag, wants);
    stub_sections::registerB15(L, state, log, stateTag, wants); // 2026-10-08, wave 2 batch W2-1 (core dispatchers)
    stub_sections::registerB16(L, state, log, stateTag, wants); // 2026-10-08, wave 2 Batch W2-3 fan-out
    stub_sections::registerB22(L, state, log, stateTag, wants); // 2026-10-08, wave 2 batch W2-4
    stub_sections::registerB23(L, state, log, stateTag, wants);
    stub_sections::registerB26(L, state, log, stateTag, wants); // 2026-10-08, wave 2 Batch W2-7 fan-out
    stub_sections::registerB27(L, state, log, stateTag, wants);
    stub_sections::registerB24(L, state, log, stateTag, wants); // wave 2, Batch W2-5 (2026-10-08)
    stub_sections::registerB25(L, state, log, stateTag, wants);
    stub_sections::registerB17(L, state, log, stateTag, wants); // 2026-10-08, wave 2 batch W2-6
    stub_sections::registerB18(L, state, log, stateTag, wants);
    stub_sections::registerB19(L, state, log, stateTag, wants); // 2026-10-08, wave 2 batch W2-8
    stub_sections::registerB20(L, state, log, stateTag, wants);
    stub_sections::registerB21(L, state, log, stateTag, wants); // 2026-10-08, wave 2 batch W2-2
    stub_sections::registerB28(L, state, log, stateTag, wants); // 2026-10-08, 6-native clearance fast-follow onto wave 1 batch 1
    stub_sections::registerB29(L, state, log, stateTag, wants); // 2026-10-08, Sec6 backlog close-out
    stub_sections::registerB30(L, state, log, stateTag, wants); // 2026-10-08, tranche-F close-out + camera_restrict_set_active
    stub_sections::registerB31(L, state, log, stateTag, wants); // 2026-10-08, continuous_spawn_start/on_vehicle_enter/tutorial_start
}

} // namespace sr3luahost
