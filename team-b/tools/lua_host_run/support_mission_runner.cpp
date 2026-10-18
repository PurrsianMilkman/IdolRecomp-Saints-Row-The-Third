// lua_host_run helpers: the mission-drive watchdog hook, the per-coroutine trace rings, the --probe-autopilot
// registration capture and the script-thread runner call for a mission's `<stem>_start`.
// Moved verbatim out of tools/lua_host_run.cpp's former anonymous namespace (see lua_host_run_support.h).
#include "lua_host_run_support.h"

namespace luahostrun {

// --trace-host-calls-before-error=N (DIAGNOSTIC ONLY, CHOSEN tooling, off
// by default, N=0): when N > 0, every single resume this tool makes (the
// mission's own initial _start call AND every later scheduler-pass resume
// - both re-arm this SAME hook fresh per resume, see runChunkWithWatchdog/
// callMissionStart/the schedulerPass `before` callback below) keeps a ring
// of the last N global-call names (C stub or Lua, via LUA_HOOKCALL) made
// during THAT ONE resume. Reset at the start of each resume and, if that
// resume's own call ends in a Lua error, printed right there before being
// discarded - so it always reflects "what led to this specific error,"
// never a stale trace from an unrelated resume. Not a spec fact, not a
// behavior change - purely an investigation aid for "what host call fed a
// script-level nil/error," the same spirit as --probe-unresolved-names.
// Real engine behavior is never altered by this flag; only stderr output
// changes.
//
// UPDATED 2026-10-03 (peer follow-up, per-coroutine keying): the first
// version used a single global ring, which is wrong for any question that
// isn't "what led to THIS error" - this mission-drive loop advances the
// WHOLE scheduler every tick (host.runScriptThreadSchedulerPass resumes
// every live thread, not just one mission's own), so by the time a mission
// is classified still_suspended at budget exhaustion, a single global ring
// holds whatever OTHER thread's resume happened to run last that tick, not
// necessarily this mission's own (results/trace_host_calls_20261003.md's
// own "NOT answered" section has the full original finding). Keying by the
// resuming lua_State* (lua_sethook is itself already per-thread in Lua
// 5.1, so the L the hook fires with IS the specific coroutine) fixes this:
// each coroutine gets its own ring, so reading back by its own co is
// always that thread's own last resume, regardless of what else resumed
// around it. Stale entries for a finished/released thread are simply left
// in the map (bounded by the mission count this tool ever touches in one
// run - never reused across tool invocations, so no real leak concern for
// a short-lived diagnostic).
size_t g_traceRingSize = 0;
std::unordered_map<lua_State*, std::vector<std::string>> g_traceRings;

// Per-coroutine, reset at the top of every resume (resetTraceRing, right
// below) - holds only THIS resume's own registration calls.
std::unordered_map<lua_State*, std::vector<CapturedRegistration>> g_registrationCallsThisResume;

void resetTraceRing(lua_State* co) {
    g_traceRings[co].clear();
    // --probe-autopilot (2026-10-07): same per-resume reset as the ring
    // above, purely for memory hygiene across a long-running mission's
    // many later resumes - by the time any LATER reset runs, a mission's
    // own FIRST resume's captures have already been copied out into
    // g_firstResumeRegistrations (callMissionStart, below) and are never
    // read from this map again.
    g_registrationCallsThisResume[co].clear();
}

// First-resume ring (2026-10-07, peer follow-up to this file's own
// "architectural wait" investigation): g_traceRings above is overwritten by
// resetTraceRing() at the top of EVERY resume of a coroutine, so it only
// ever holds a mission's MOST RECENT resume - by the time a mission is
// classified still_suspended at budget exhaustion (after many later
// scheduler-pass resumes of its own thread_yield poll), whatever that
// mission called on its very FIRST resume - its own _start call, where a
// mission typically does its one-time setup (registering a callback such
// as trigger_enable/on_trigger/on_vehicle_enter) before ever reaching its
// wait loop - has long since been discarded. callMissionStart (below) is
// only ever invoked once per mission's coroutine (this harness only ever
// drives a fresh start, never a resume/retry - see callMissionStart's own
// top doc comment), so the ring g_traceRings[co] holds at the instant that
// one call returns IS, completely, that coroutine's first resume. Captured
// there into this second map, once, and never reset or overwritten again -
// purely additive to the existing per-coroutine ring, same diagnostic-only
// contract (opt-in via the same --trace-host-calls-before-error=N flag;
// N=0 keeps this empty and unused, same as g_traceRings).
std::unordered_map<lua_State*, std::vector<std::string>> g_firstResumeTraceRings;

// --probe-autopilot (2026-10-07, this task): a THIRD, further-additive
// extension of the same ring machinery above, opt-in via its own flag
// (g_probeAutopilotEnabled), gated the same way (never touches the Lua
// stack unless explicitly enabled, never changes real engine behavior).
//
// The still_suspended classification site (below, in main()) already
// reads g_firstResumeTraceRings[co] to know WHICH native names a mission
// called on its own first resume - but that ring, like g_traceRings
// itself, only ever records a call's NAME (missionWatchdogHook's own
// LUA_HOOKCALL branch: `info.name`), never its arguments. "Did this
// mission register a callback" needs the name; "what callback, against
// what trigger/character/vehicle" - the exact thing --probe-autopilot's
// own task brief requires ("use only the callback and the exact
// arguments the script itself registered") - needs the arguments too.
// This is new: nothing in this project previously retained a registration
// call's own argument VALUES anywhere retrievable after the call returns
// (genericStubTrampoline, src/lua_stub_registry.cpp, logs only argument
// COUNT and TYPE strings, same for src/lua_spec_confirmed_stubs.cpp's
// logCall(); the few dedicated stubs that DO store a callback name
// (stub_on_take_damage/stub_on_revived) key it per-character in
// EngineState, not per-coroutine/per-call, so reading it back for "what
// did THIS mission's first resume register" would require guessing which
// character it was for).
//
// Measured directly off the Lua stack at the exact LUA_HOOKCALL instant:
// Lua 5.1's call hook fires with the CALLEE already the active function
// (lua_getstack(L, 0, ...) below already relies on this to name it), so
// for the callee - C function or Lua function alike - stack positions
// 1..argc are already exactly its own arguments at that instant; this is
// the same stack state genericStubTrampoline's own body reads when IT
// runs as that callee (`lua_gettop(L)`/`lua_type(L, i)` for i=1..argc,
// src/lua_stub_registry.cpp:70-92) - not a new or different technique,
// just applied from a hook instead of from inside the callee itself.
//
// Only the ONE family of natives this project has independently verified
// (spec-lua-api-behaviour.md, CONFIRMED-structure sections, cross-checked
// against this task's own named examples - see isCallbackRegistrationNative's
// own doc comment for the full citation list and, importantly, which of
// the task's own named examples were checked and EXCLUDED after
// verification) is captured; every other call is untouched, exactly as
// before. (CapturedRegistration/g_registrationCallsThisResume themselves
// are declared earlier, next to g_traceRings/resetTraceRing, purely
// because resetTraceRing needs the map - this comment block covers both.)
//
// Captured once, at the same instant/call site g_firstResumeTraceRings is
// (callMissionStart, below) - never overwritten afterward. Empty unless
// --probe-autopilot is given.
std::unordered_map<lua_State*, std::vector<CapturedRegistration>> g_firstResumeRegistrations;
bool g_probeAutopilotEnabled = false; // set only by --probe-autopilot (main(), below)

// The callback-registration native family --probe-autopilot acts on -
// see g_registrationCallsThisResume's own doc comment above for why this
// capture exists at all. Every entry below is CONFIRMED by direct reading
// of spec-lua-api-behaviour.md (not inferred from this project's own
// earlier summaries) to take a callback-name string as one argument,
// registering it for a LATER dispatch this project's own engine state
// models via the real coroutine-spawn mechanism (UPDATE 2026-10-09,
// spec-lua-api-behaviour.md Sec1.14's own "GAP CLOSED" finding, commit
// 383f653: the real dispatcher 0x00a29e40 hands off to reader 0x00a1fe10,
// which spawns a NEW Lua coroutine for the resolved name rather than
// calling Lua synchronously - the body only runs later, when the scheduler
// 0x00e0cba0 resumes it; the consuming/firing side used to be OPEN when
// this comment was first written, hence the earlier "firing the named Lua
// global with zero arguments" synchronous stand-in below this function's
// own callers used - that stand-in is superseded, see the dispatch block's
// own 2026-10-09 doc comment for the replacement):
//   on_trigger (Sec1.2), on_trigger_exit (Sec1.7), on_death (Sec3.2),
//   on_vehicle_destroyed (Sec3.3), on_attack_performed (Sec3.12),
//   on_take_damage (Sec3.13), on_revived (Sec10.8), on_vehicle_enter
//   (Sec4.12, added 2026-10-08 - see note below), on_vehicle_enter_water
//   (Sec14.30, added 2026-10-08 - see note below): arg 1 callback name,
//   arg 2 trigger/character/vehicle name (`hasTargetArg=true`).
//   on_qte_animation_trigger (Sec10.7): arg 1 callback name ONLY - a
//   single fixed GLOBAL slot, not per-object (`hasTargetArg=false`).
//
// UPDATE 2026-10-08: on_vehicle_enter (Sec4.12) was RE-RESOLVED by commit
// e07a46a (full re-disassembly of 0x005e4660/0x00a56d50, batch
// lua_spec_confirmed_stubs_b31_continuous_spawn_vehicle_enter_tutorial.cpp) -
// the self-contradiction this note used to flag is gone: the earlier
// "IMMEDIATE numeric-event post" reading was simply wrong, refuted and
// superseded, not left genuinely ambiguous. Arg 1 IS a callback/hook name,
// full stop, and the native registers it into one of 3 hook slots
// depending on how arg 2 resolves - the exact same two-string registration
// shape as every other entry in this family. Moved into kTwoArgFamily
// below. on_vehicle_enter_water's OWN exclusion above was only ever "by
// analogy to on_vehicle_enter's uncertainty" - since that uncertainty is
// now resolved (and on_vehicle_enter_water's own Sec14.30 text was already
// independently CONFIRMED on its own terms, and already implemented,
// b11), it is moved into kTwoArgFamily too, for the same reason.
//
// EXPLICITLY CHECKED AND EXCLUDED (this task's own example list named
// three of these four; verified directly against spec-lua-api-behaviour.md
// rather than assumed from the list):
//   trigger_enable (Sec1.1) - has NO callback argument at all (string
//     trigger name, bool enable, numeric flag) - a toggle on an ALREADY-
//     registered trigger, not a registration call itself.
//   on_cellphone - does not exist anywhere in this project's spec docs,
//     source, or the 1490-name tagged registration list; the only
//     cellphone-related natives are audio_play_for_mission_cellphone/
//     audio_remove_mission_cellphone (immediate audio actions, not
//     registration) and cellphone_animate_start_do/_stop_do.
// Also surveyed and excluded as a DIFFERENT, more complex mechanism
// shape (a stored responder/property name or an actual Lua closure, not
// a simple "callback name string" the way every entry above is):
// game_sign_into_network, vint_internal_dataresponder_request,
// game_peg_load_with_cb, vint_set_property's callback-typed case.
bool isCallbackRegistrationNative(const std::string& name, bool* hasTargetArg) {
    static const std::unordered_set<std::string> kTwoArgFamily = {
        "on_trigger", "on_trigger_exit", "on_death", "on_vehicle_destroyed",
        "on_attack_performed", "on_take_damage", "on_revived",
        "on_vehicle_enter", "on_vehicle_enter_water"};
    if (kTwoArgFamily.count(name)) {
        if (hasTargetArg) *hasTargetArg = true;
        return true;
    }
    if (name == "on_qte_animation_trigger") {
        if (hasTargetArg) *hasTargetArg = false;
        return true;
    }
    return false;
}

void printTraceRingIfAny(lua_State* co, const std::string& label, const std::string& errorMessage) {
    if (g_traceRingSize == 0) return;
    const auto& ring = g_traceRings[co];
    std::cerr << "[trace-host-calls] " << label << " error: " << errorMessage << "\n[trace-host-calls] last "
              << ring.size() << " global calls before it:";
    for (const auto& n : ring) std::cerr << " " << n;
    std::cerr << "\n";
}
// Prints a coroutine's own ring with no error - the still_suspended case
// (2026-10-03): a mission that never errors (a thread_yield poll loop) has
// nothing for printTraceRingIfAny's own error-gated print to fire on, so
// "what does this mission's own wait poll" needed this separate entry
// point, reading the SAME per-coroutine ring at budget-exhaustion time
// instead of at an error.
void printTraceRingStillSuspended(lua_State* co, const std::string& label) {
    if (g_traceRingSize == 0) return;
    const auto& ring = g_traceRings[co];
    std::cerr << "[trace-host-calls] " << label << " still suspended (no error) - last " << ring.size()
              << " global calls of its own last resume:";
    for (const auto& n : ring) std::cerr << " " << n;
    std::cerr << "\n";
}
// Prints a coroutine's FIRST-ever resume ring (2026-10-07, see
// g_firstResumeTraceRings' own doc comment above) - answers "what did this
// mission call in its own setup, before it ever reached its wait loop",
// which printTraceRingStillSuspended's most-recent-resume-only ring cannot
// once a mission has yielded more than once (every later poll resume
// overwrites g_traceRings[co] with just that poll's own calls, typically a
// lone thread_yield). Paired with printTraceRingStillSuspended at the same
// still_suspended classification call site, never on its own.
void printTraceRingFirstResume(lua_State* co, const std::string& label) {
    if (g_traceRingSize == 0) return;
    auto it = g_firstResumeTraceRings.find(co);
    if (it == g_firstResumeTraceRings.end()) return;
    const auto& ring = it->second;
    std::cerr << "[trace-host-calls] " << label << " first resume (own _start call) - last " << ring.size()
              << " global calls before it first yielded/finished/errored:";
    for (const auto& n : ring) std::cerr << " " << n;
    std::cerr << "\n";
}
void missionWatchdogHook(lua_State* L, lua_Debug* ar) {
    if (ar->event == LUA_HOOKCALL) {
        lua_Debug info{};
        if (lua_getstack(L, 0, &info) && lua_getinfo(L, "n", &info)) {
            auto& ring = g_traceRings[L];
            ring.push_back(info.name ? info.name : "?");
            if (ring.size() > g_traceRingSize) ring.erase(ring.begin());
            // --probe-autopilot (2026-10-07): see g_registrationCallsThisResume's
            // own doc comment above - gated on g_probeAutopilotEnabled so this
            // never runs (never touches the Lua stack beyond what the ring
            // capture above already does) unless --probe-autopilot was given.
            if (g_probeAutopilotEnabled && info.name) {
                bool hasTargetArg = false;
                if (isCallbackRegistrationNative(info.name, &hasTargetArg)) {
                    CapturedRegistration cr;
                    cr.nativeName = info.name;
                    if (lua_gettop(L) >= 1 && lua_isstring(L, 1)) cr.callbackName = lua_tostring(L, 1);
                    if (hasTargetArg && lua_gettop(L) >= 2 && lua_isstring(L, 2))
                        cr.targetName = lua_tostring(L, 2);
                    g_registrationCallsThisResume[L].push_back(std::move(cr));
                }
            }
        }
        return;
    }
    luaL_error(L, "lua_host_run mission-drive watchdog: call exceeded the %d-instruction budget "
                  "(likely an infinite loop in never-before-executed mission code) - aborted by "
                  "this tool's own safety hook, not hung",
               kMissionWatchdogInstructionBudget);
}
int missionWatchdogHookMask() { return LUA_MASKCOUNT | (g_traceRingSize > 0 ? LUA_MASKCALL : 0); }

// Watchdog-wrapped host.runChunk() - same instruction-budget convention as
// missionWatchdogHook above, applied around the ONE real Lua API call
// (runChunk's own internal lua_pcall) this task's own step-1 pass makes
// that was not already individually watchdog-protected.
Host::RunResult runChunkWithWatchdog(Host& host, lua_State* L, const std::string& source,
                                      const std::string& chunkName) {
    lua_sethook(L, missionWatchdogHook, LUA_MASKCOUNT, kMissionWatchdogInstructionBudget);
    Host::RunResult r = host.runChunk(L, source, chunkName);
    lua_sethook(L, nullptr, 0, 0); // always clear - never leaves the watchdog armed for a later, unrelated call
    return r;
}

// checkpoint/isRestart's call-shape values are now CONFIRMED, not chosen
// (spec-lua-api-behaviour.md Sec36.4, spec-lua-bindings.md Sec14.11, synced
// 2026-10-02): the engine's own two native call sites for a mission's
// `<stem>_start` push exactly two Lua values onto the freshly created
// coroutine - a checkpoint STRING (the literal `"mission start"` on a
// genuine fresh start; the mission's own last-reported checkpoint name,
// e.g. `mission_checkpoints.xtbl`'s `CheckpointName` column, on a resume) -
// and a boolean `is_restart` (`false` on a fresh start, `true` on a
// resume/retry). **Never the number 0** - Sec36.4 is explicit that nothing
// in this mechanism ever produces anything but that string and that
// boolean on a fresh start, and no `mission_checkpoints.xtbl` row is ever
// named `"mission start"` or indexed `0`. This project's host previously
// passed the CHOSEN literal number `0.0` here, which is exactly the real
// `dlc2_m02` "attempt to index local 'cp_data' (a nil value)" bug shape:
// a mission script that only ever keys its checkpoint table by name
// strings (e.g. `checkpoints["mission start"]`/`checkpoints["1"]`) gets
// nil back when indexed by the number `0`.
//
// This harness only ever drives a FRESH start (one `callMissionStart` call
// per mission, below) - there is no resume/retry call site here yet, so
// Sec36.4's second half (`start_fn(<checkpoint_name_string>, true)`,
// reading the current-checkpoint-name global `0x014c8354`) is honestly
// NOT implemented: there is nothing in this harness that would ever call
// it with `isRestart=true`, so modelling that global here would be dead
// code, not a real fix. If a resume/retry drive path is added later, it
// must read the checkpoint-name string from that global (CONFIRMED;
// Sec36.3) rather than inventing one.
//
// The call goes through the script-thread runner, not a bare lua_pcall
// (spec-lua-api-behaviour.md Sec26.27 thread table, 2026-10-01 answer to
// Team B request 11, CONFIRMED: the only writers of the current-thread
// stack 0x02a44d10/0x02a44d14 are the runner 0x00e0cba0's push/pop and a
// start-up zero-out, so every Lua code the engine reaches from mission or
// trigger code runs inside a pushed record; "route every such invocation
// through a resume-shaped call (push a record, call, pop)"). So `_start`
// runs in its own coroutine (a yield inside it is legal) and is the
// current record for the call (thread_new inside it finds a parent).
// BareThreadTable::allocate()/run() is the host's model of that runner;
// `threads` must be the table registered into L (Host::bareGlobals()).
// This already matches Sec36.2/Sec36.4's "as a new coroutine through the
// thread runner, never a plain direct call" requirement - it was the
// checkpoint ARGUMENT that was wrong, not this call mechanism. Which of
// the runner's callers SR3 uses for mission hooks is OPEN and is not
// modelled; only the mechanism is.
MissionStartResult callMissionStart(lua_State* L, BareThreadTable& threads, const std::string& funcName,
                                     const std::string& checkpoint, bool isRestart) {
    MissionStartResult r;
    lua_getglobal(L, funcName.c_str());
    r.existedAsFunction = (lua_type(L, -1) == LUA_TFUNCTION);
    if (!r.existedAsFunction) {
        lua_pop(L, 1);
        return r;
    }
    lua_pop(L, 1);
    r.attemptedCall = true;
    // The runner's step 4 fetches the global by name below the arguments, so
    // only the two arguments go on L's stack (allocate() moves them).
    lua_pushstring(L, checkpoint.c_str());
    lua_pushboolean(L, isRestart ? 1 : 0);
    // hasParent=false: nothing is current before a mission's `_start` runs
    // (the drive loop calls it from the host, not from inside a record), the
    // same root-level situation BareThreadTable::startThread() models. The
    // spec gives no record a mission `_start` would be nested in.
    BareThreadTable::Record* rec = threads.allocate(L, funcName, /*nargs=*/2, /*hasParent=*/false);
    if (!rec) {
        lua_pop(L, 2); // allocate() leaves the arguments when the table is full
        r.callOk = false;
        r.callError = "lua_host_run: script-thread table full (" + std::to_string(BareThreadTable::kCapacity) +
                      " records, HIGH CONFIDENCE capacity) - " + funcName + " not started";
        lua_settop(L, 0);
        return r;
    }
    // Keep the coroutine reachable across run(): when the record finishes or
    // fails, run() releases it (the Record is freed and its registry ref
    // dropped), and its own status / error message is read from it below.
    lua_State* co = rec->co;
    lua_rawgeti(L, LUA_REGISTRYINDEX, rec->ref);
    const int coRef = luaL_ref(L, LUA_REGISTRYINDEX);
    // The watchdog goes on the coroutine: a count hook is per lua_State, and
    // the mission code now runs on `co`, not on L. Threads that thread_new
    // creates inside it inherit it (Lua 5.1 lua_newthread copies the hook).
    resetTraceRing(co); // see g_traceRings' own doc comment above - fresh per resume, keyed by this coroutine
    lua_sethook(co, missionWatchdogHook, missionWatchdogHookMask(), kMissionWatchdogInstructionBudget);
    std::string fetchError;
    const uint16_t recordId = rec->id;
    const bool alive = threads.run(L, rec, fetchError); // rec is dangling once this returns false
    lua_sethook(co, nullptr, 0, 0);
    // Save this coroutine's first-ever resume ring (2026-10-07) - see
    // g_firstResumeTraceRings' own doc comment above. threads.run() just
    // above is this function's one and only lua_resume of `co`, and
    // callMissionStart itself is only ever called once per mission's co
    // (this function's own top doc comment), so g_traceRings[co] right
    // here holds exactly, and completely, that coroutine's first resume -
    // written once into the separate map and never touched again,
    // regardless of what the mission-drive loop's later scheduler-pass
    // resumes go on to do to g_traceRings[co] itself.
    if (g_traceRingSize > 0) g_firstResumeTraceRings[co] = g_traceRings[co];
    // --probe-autopilot (2026-10-07): same first-resume snapshot, same
    // instant, for the registration-argument captures - see
    // g_firstResumeRegistrations' own doc comment above.
    if (g_probeAutopilotEnabled) g_firstResumeRegistrations[co] = g_registrationCallsThisResume[co];
    if (!fetchError.empty()) {
        r.callOk = false;
        r.callError = "lua_host_run: runner could not fetch " + funcName + ": " + fetchError;
    } else if (alive) {
        // Runner step 6: yielded without error -> alive. Not an error; the
        // record stays in the table, and the mission loop's scheduler passes
        // (0x00e0cf50, Sec26.27 RESOLVED 2026-10-02) resume it from then on.
        r.callOk = true;
        r.suspended = true;
        r.recordId = recordId;
        // Capture where it's actually waiting: level 1 is the mission script's
        // own frame that called thread_yield (level 0 is bareGuard's C frame -
        // see this function's own doc comment above). Falls back to level 0 if
        // level 1 isn't available (a yield from somewhere other than the
        // bareGuard<thread_yield> path, if one is ever added).
        lua_Debug ar;
        if (lua_getstack(co, 1, &ar) && lua_getinfo(co, "Sln", &ar)) {
            r.suspendedAt = (ar.short_src[0] ? ar.short_src : "?") + std::string(":") +
                             std::to_string(ar.currentline) +
                             (ar.name ? (std::string(" (in ") + ar.name + ")") : "");
        } else if (lua_getstack(co, 0, &ar) && lua_getinfo(co, "Sln", &ar)) {
            r.suspendedAt = (ar.short_src[0] ? ar.short_src : "?") + std::string(":") +
                             std::to_string(ar.currentline) + " (C frame, no Lua caller level)";
        }
    } else {
        // Finished or failed. The error is read from the coroutine itself
        // (lua_resume marks a failed coroutine dead with the message on its
        // stack), not from threads.errors(): that vector is shared with any
        // thread_new child that failed during the call, so "it grew" would
        // misattribute a child's error to `_start`. This gives the bare
        // message, as the old lua_tostring(L, -1) did, without the runner's
        // "<name>: " prefix - the per-mission error columns and bridge_diff
        // comparisons keep their meaning across runs.
        const int st = lua_status(co);
        if (st != 0 && st != LUA_YIELD) {
            const char* msg = lua_tostring(co, -1);
            r.callOk = false;
            r.callError = msg ? msg : "(no error message)";
            printTraceRingIfAny(co, funcName, r.callError);
        } else {
            r.callOk = true;
        }
    }
    luaL_unref(L, LUA_REGISTRYINDEX, coRef);
    // Same end state as the old bare-pcall version: L's stack empty. Measured
    // (2026-10-02, fixture and real cache): L is already at 0 here - the
    // call's results stay on `co` - so this only guards the contract.
    lua_settop(L, 0);
    return r;
}

} // namespace luahostrun
