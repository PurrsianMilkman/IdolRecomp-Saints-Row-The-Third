// lua_spec_confirmed_stubs_b31_continuous_spawn_vehicle_enter_tutorial.cpp
//
// Batch b31 (2026-10-08): the 3 natives newly cleared for implementation by
// this project's own cross-reference pass of spec-lua-api-behaviour.md's
// own "cleared for implementation" review-status lines - continuous_spawn_
// start (Sec4.8), on_vehicle_enter (Sec4.12, re-resolved by commit e07a46a),
// tutorial_start (Sec6.19, whose own 210-entry name table was transcribed
// directly into the spec text by commit 3816870). All 3 were previously
// unimplemented (no stub_continuous_spawn_start/stub_on_vehicle_enter/
// stub_tutorial_start existed anywhere in this project before this batch).
// New file, not a retrofit - continuous_spawn_start extends the shared
// spawn-region table lua_spec_confirmed_stubs_b08_batch1_core.cpp's own
// continuous_spawn_stop already establishes (see that file's own updated
// doc comment for the one new line this batch added there); on_vehicle_
// enter extends CharacterState/VehicleState's existing hook-slot catalogue
// (on_vehicle_enter_water, b11); tutorial_start reuses EngineState's
// existing 210-entry tutorialName()/tutorialLookup()/tutorialState()
// machinery (tutorial_advance, b01; tutorial_stop, b21) directly, per this
// project's own "extend the existing dispatch, don't write a parallel
// one-off" house rule.
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

namespace sr3luahost {
namespace {

// ---------------------------------------------------------------------
// continuous_spawn_start (§4.8, real address 0x00a435a0) - 85 calls / 20
// scripts. See EngineState::continuousSpawnRegions()/
// continuousSpawnDefaultTypeFilter()'s own doc comments (engine_state.h)
// for the full field-by-field citation. Arg 1 mandatory string (region
// name, SAME resolver continuous_spawn_stop uses, §4.7). Args 2-10 each
// individually nil-gated: arg 2 (number, truncateEa2596, default 0 - spawn
// count), arg 3 (raw number, default 0.0, * 1000.0 then truncateEa2596 -
// respawn interval seconds -> ms), arg 4 (string, default CONFIRMED ""
// per spec-lua-api-behaviour.md Sec4.8 - see continuousSpawnDefaultTypeFilter()),
// arg 5 (number,
// truncateEa2596, default 1), arg 6 (number, truncateEa2596, default 14,
// raw dword store at +0x38), arg 7 (number, truncateEa2596, default 19,
// raw dword store at +0x3c), arg 8/9 (strings, default absent, via
// setters 0x005ec3f0/0x005ec460), arg 10 (string, default absent, raw
// pointer store at +0x40, no setter). Return: none.
int stub_continuous_spawn_start(lua_State* L) {
    logCall(L, upLog(L), "continuous_spawn_start", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set - SAME resolver continuous_spawn_stop uses (§4.7/§4.13)

    auto& regions = st->continuousSpawnRegions();
    auto existing = regions.find(name);
    if (existing != regions.end()) {
        // Fast re-start path (CONFIRMED): only the active flag is touched -
        // nothing else about the existing record is rewritten.
        st->continuousSpawnActive()[name] = true;
        return 0;
    }

    // New registration: claim a fresh record and write every argument.
    EngineState::ContinuousSpawnRegionRecord rec;
    rec.count = truncateEa2596(optionalNumberDefault(L, 2, 0.0));
    double respawnSeconds = optionalNumberDefault(L, 3, 0.0); // CONFIRMED raw (no rounding) read, converted below
    rec.respawnIntervalMs = truncateEa2596(respawnSeconds * 1000.0);
    bool hasArg4 = lua_gettop(L) >= 4 && lua_type(L, 4) != LUA_TNIL;
    rec.typeNameFilter = hasArg4 ? argString(L, 4) : st->continuousSpawnDefaultTypeFilter(); // CONFIRMED "" when arg 4 absent (spec-lua-api-behaviour.md Sec4.8)
    rec.numericArg5 = truncateEa2596(optionalNumberDefault(L, 5, 1.0));
    rec.numericArg6 = truncateEa2596(optionalNumberDefault(L, 6, 14.0));
    rec.numericArg7 = truncateEa2596(optionalNumberDefault(L, 7, 19.0));
    rec.hasArg8 = lua_gettop(L) >= 8 && lua_type(L, 8) != LUA_TNIL;
    rec.arg8String = rec.hasArg8 ? argString(L, 8) : std::string();
    rec.hasArg9 = lua_gettop(L) >= 9 && lua_type(L, 9) != LUA_TNIL;
    rec.arg9String = rec.hasArg9 ? argString(L, 9) : std::string();
    rec.hasArg10 = lua_gettop(L) >= 10 && lua_type(L, 10) != LUA_TNIL;
    rec.arg10RawPointerString = rec.hasArg10 ? argString(L, 10) : std::string();
    // "started" marker is freshly clear on a brand-new record (CONFIRMED
    // precondition: a just-claimed slot was never previously activated) -
    // the dedicated activation call fires exactly once here.
    rec.started = true;
    ++rec.activationCount;
    regions.emplace(name, std::move(rec));
    st->continuousSpawnActive()[name] = true;
    return 0;
}

// ---------------------------------------------------------------------
// on_vehicle_enter (§4.12, 0x00a588e0) - 76 calls / 14 scripts. RESOLVED
// 2026-10-08 (commit e07a46a): a hook-REGISTRATION native, same shape as
// on_vehicle_enter_water (§14.30) - NOT an immediate numeric-event post
// (the spec's own earlier self-flagged "NEEDS-EXE" alternative reading is
// refuted). Arg 1 (string, mandatory) is a callback/hook name, NOT an
// occupant. Arg 2 (string, mandatory) is a vehicle/seat reference resolved
// through a 3-way branch, each registering arg 1 into a different hook
// slot on the resolved object via the SAME clear/convert/notify template
// (none of the three "posts" anything):
//   1. generic/#PLAYER# chain (playerClassChainResolves()) -> character
//      hook slot 0x16, base +0x1f00, via 0x005e4660.
//   2. type-gated bit 0x4@+0xb (release_to_world's 2nd attempt, §4.4) ->
//      character hook slot 0x17, base +0xf8, via 0x005e4660.
//   3. type-gated bit 0x8@+0xb (vehicle_is_destroyed/vehicle_suppress_
//      npc_exit's own fallback, §4.5/§4.9) -> vehicle hook slot 7, base
//      +0xb0, via 0x00a56d50.
// Branches 2/3 share this project's flat objectResolves() gate and are
// told apart by EngineState::onVehicleEnterResolverKind() (mirrors
// release_to_world's own releaseToWorldKind(), per this task's explicit
// instruction to reuse that exact resolution shape). Return: none.
int stub_on_vehicle_enter(lua_State* L) {
    logCall(L, upLog(L), "on_vehicle_enter", upStateTag(L));
    std::string callback = argString(L, 1);
    std::string target = argString(L, 2);
    EngineState* st = upState(L);

    if (st->playerClassChainResolves(target)) { // CHOSEN default now applies when no test .set() it - branch 1
        st->getOrCreateCharacter(target).onVehicleEnterGenericSlotCallback = callback;
        return 0;
    }
    if (!st->objectResolves().get(target)) return 0; // OPEN until set - neither type-gated resolver matched
    // Spec sync e6691f1 (Sec4.12, FINAL): script_npc (kind 1) and script_vehicle (kind 2) are sibling classes,
    // names never overlap; a name that passed the generic resolver but is no known character is a vehicle.
    // An explicit onVehicleEnterResolverKind().set() (tests) still wins.
    int32_t kind = st->onVehicleEnterResolverKind().known(target)
                       ? st->onVehicleEnterResolverKind().get(target)
                       : (st->hasCharacter(target) ? 1 : 2);
    if (kind == 1) {
        st->getOrCreateCharacter(target).onVehicleEnterCharacterSlotCallback = callback; // branch 2: hook slot 0x17, +0xf8
    } else {
        st->getOrCreateVehicle(target).onVehicleEnterCallback = callback; // branch 3: hook slot 7, +0xb0, via 0x00a56d50
    }
    return 0;
}

// ---------------------------------------------------------------------
// tutorial_start (§6.19, 0x00a603f0). See EngineState::TutorialStart's own
// doc comment (engine_state.h) for the full per-field citation of every
// OPEN global this dispatcher reads. Arguments: arg 1 (string, mandatory)
// - resolved via tutorialLookup() (the SAME 210-entry table tutorial_
// advance/tutorial_stop already share); arg 2 (number, truncateEa2596,
// default 0 - N); args 3-5 (booleans, each default false - F/B4/B5) plus
// an internal hidden literal 1 (P4, never Lua-visible). A failed table
// resolve (-1) is caught here, before the dispatcher runs at all - the
// SAME "caught by the Lua entry itself" pattern tutorial_advance's own
// 2026-10-08 re-derivation already confirms for its own entry point.
// Implements the full 12-step dispatcher order exactly as the spec gives
// it (Sec6.19, "Dispatcher order"). Return: none.
int stub_tutorial_start(lua_State* L) {
    logCall(L, upLog(L), "tutorial_start", upStateTag(L));
    std::string id = argString(L, 1);
    EngineState::TutorialLookup lk = EngineState::tutorialLookup(id);
    if (lk.index < 0) return 0; // CONFIRMED: not in the 210-entry table -> no dispatch at all

    int32_t A = lk.index;
    int64_t N = truncateEa2596(optionalNumberDefault(L, 2, 0.0));
    bool F = optionalBoolDefault(L, 3, false);
    bool B4 = optionalBoolDefault(L, 4, false);
    bool B5 = optionalBoolDefault(L, 5, false);
    // P4 (the hidden internal 4th value) is the literal 1, always - never
    // read from the Lua stack (CONFIRMED, Sec6.19's own "with a literal 1
    // inserted as an internal fourth value not exposed to the Lua-visible
    // signature").

    EngineState* st = upState(L);
    EngineState::TutorialStart& ts = st->tutorialStart();
    auto stateKey = [](int32_t idx) { return EngineState::tutorialStateKey(idx); };

    // Step 1.
    if (A < 189 && ts.suppressionByte0151d5a6.get()) return 0; // OPEN until set
    // Step 2.
    {
        int32_t m = ts.globalMode029443bc.get(); // OPEN until set
        if (m == 1 || m == 2 || m == 3) return 0;
    }
    // Step 3 (structurally unreachable in this project: a successful
    // tutorialLookup() already guarantees 0 <= A < 210, same "always-true
    // precondition modelled anyway for fidelity" shape as continuous_
    // spawn_start's own "started" check - kept for 1:1 structural fidelity
    // with the spec's own 12-step list).
    if (A > 209) return 0;
    // Step 4.
    if (A < 189 && st->coopLocalIsClient() && (ts.flagsByte24.get(stateKey(A)) & 0x10u)) return 0; // OPEN until set (flags)
    // Step 5.
    if (ts.gate5Value00706ab0.get() == 6) return 0; // OPEN until set
    // Step 6.
    {
        bool continue6;
        if (ts.mode014f3d34.get()) { // OPEN until set
            continue6 = (A == 188) || (A >= 196 && A <= 201);
        } else {
            continue6 = (ts.flagsByte24.get(stateKey(A)) & 0x08u) != 0 || ts.noMissionLive006d2910.get(); // OPEN until set (both)
        }
        if (!continue6) return 0;
    }
    // Step 7 (mutates A for every subsequent step).
    if (A == 76 && !ts.byteNonZero0141250d.get()) A = 73; // OPEN until set
    // Step 8.
    if (A >= 189) {
        if (!ts.suppressionByte0151d5a6.get()) st->tutorialState().set(stateKey(A), 1);
    }
    // Step 9.
    if (F && !ts.suppressionByte0151d5a6.get() && st->tutorialState().get(stateKey(A)) == 0) {
        st->tutorialState().set(stateKey(A), 1);
    }
    // Step 10.
    {
        int32_t state = st->tutorialState().get(stateKey(A));
        bool continue10 = (state == 1) || (B4 && state == 4) || (A >= 189);
        if (!continue10) return 0;
    }
    // Step 11.
    if (ts.flagsByte24.get(stateKey(A)) & 0x04u) { // OPEN until set
        ++ts.richWidgetDispatchCount[A];
        st->tutorialState().set(stateKey(A), 4);
    } else {
        EngineState::TutorialStart::EntryRecord& entry = ts.entries[A];
        if (!entry.queuedForSimplePrompt) {
            // Resolved BEFORE any field of this step is mutated: the spec's
            // own text writes the queued-flag, state and all three fields in
            // ONE atomic step ("set state 2 and fill +0x14/+0x18/+0x1c") - an
            // OPEN refusal here must not leave a half-written record (queued
            // but missing its own gated value) that could never arise in the
            // real engine (same "resolve first, mutate only once everything
            // is known" discipline continuous_spawn_start's own new-
            // registration path above already follows).
            int32_t gatedValue = B5 ? 0 : (ts.defaultParam20.known(stateKey(A)) ? ts.defaultParam20.get(stateKey(A)) : EngineState::tutorialDescriptorDefaultParam20(A)); // e6691f1 FINAL table
            entry.queuedForSimplePrompt = true;
            st->tutorialState().set(stateKey(A), 2);
            entry.argument2Value = N;
            entry.argument5GatedValue = gatedValue;
            entry.hiddenLiteralP4 = 1;
        }
    }
    // Step 12: P4 == 1 always (CONFIRMED hardcoded from Lua), so the
    // "descriptor bit 0x10 OR P4==1" disjunction is always true - this
    // reduces to "session exists and the local machine is the host"
    // exactly (Sec6.19's own "a host therefore always replicates
    // tutorial_start" closing line).
    if (st->coopSession().present.get() && st->coopLocalIsHost()) { // OPEN until set (present)
        ++ts.replicateCallCount;
        EngineState::replicateStateChange("tutorial_start", id);
    }
    return 0;
}

} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB31() {
    static const std::vector<std::string> names = {
        "continuous_spawn_start",
        "on_vehicle_enter",
        "tutorial_start",
    };
    return names;
}

void registerB31(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    // All 3 are `gameplay` per tools/lua_all_registered_1490_tagged.txt
    // (grepped directly, not assumed).
    if (wants("continuous_spawn_start"))
        registerOne(L, state, log, stateTag, "continuous_spawn_start", openGuard<stub_continuous_spawn_start>);
    if (wants("on_vehicle_enter"))
        registerOne(L, state, log, stateTag, "on_vehicle_enter", openGuard<stub_on_vehicle_enter>);
    if (wants("tutorial_start"))
        registerOne(L, state, log, stateTag, "tutorial_start", openGuard<stub_tutorial_start>);
}

} // namespace stub_sections
} // namespace sr3luahost
