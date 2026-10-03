// IMPLEMENTED PRE-REVIEW, PENDING CLEARANCE (manager rule 2026-09-30): the
// spec-lua-api-behaviour.md / spec-lua-bindings.md sections this file rests on
// are marked "NOT yet cleared for implementation" by the 2026-09-30 desk
// review (115/122 and 57/58 review-status lines). Kept working, behaviour
// unchanged, until Team A clears them; no new behaviour from those sections
// before then (team-b/HANDOFF.md section A, standing rule).
//
// The Lua 5.1 host scaffold itself: two real, independent lua_State
// instances (embedded stock Lua 5.1, third_party/lua51 - see lua_c_api.h),
// modeling the real engine's own confirmed state split
// (spec-lua-bindings.md Sec13.2: a "game play"/mission state loading the
// 1,014(+)-name gameplay roster plus game_lib.lua, and a separate UI state
// loading the 311+105-name UI/menu/store/HUD roster) - reflected here only
// as far as "which stub roster goes in which state," since the deeper
// state-shape questions (what game_lib.lua's own helpers actually need
// present to run, vint_lib.lua/system_lib.lua/game_ui_globals.lua per
// spec Sec2) are explicitly left open by this pass, not solved.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "sr3luahost/bare_globals.h"
#include "sr3luahost/engine_state.h"
#include "sr3luahost/hook_registry.h"
#include "sr3luahost/lua_c_api.h"
#include "sr3luahost/spec_confirmed_stubs.h"
#include "sr3luahost/stub_registry.h"
#include "sr3luahost/thread_scheduler.h"

namespace sr3luahost {

class Host {
public:
    // Splits `allNames` by its own cluster tag ("gameplay" vs "ui") and
    // builds two fresh Lua 5.1 states (luaL_newstate + the stock base library
    // and `coroutine` table only - spec-lua-bindings.md Sec16.1 step 3 /
    // Sec16.4: no math/string/table/io/os/debug/package), registering each half's
    // stubs into its matching state via registerStubs(). Any cluster tag
    // other than the two the file is documented to use is treated as
    // "gameplay" (a deliberately narrow fallback - the file is pre-verified
    // to only ever say "gameplay" or "ui", so this path is not expected to
    // be exercised; it exists so a surprise third tag fails safe rather
    // than silently dropping names).
    explicit Host(const std::vector<RegisteredName>& allNames);
    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    lua_State* gameplayState() const { return gameplay_; }
    lua_State* uiState() const { return ui_; }
    HitLog& hitLog() { return hitLog_; }

    size_t gameplayStubCount() const { return gameplayStubCount_; }
    size_t uiStubCount() const { return uiStubCount_; }

    // Real luaL_loadbuffer + lua_pcall attempt of `source` (named
    // `chunkName`, so real Lua error messages carry a real, readable
    // source name and real line numbers) against `L`. Every field below is
    // a real, observed Lua 5.1 API result - never invented or guessed.
    struct RunResult {
        bool loadOk = false;
        std::string loadError;  // real luaL_loadbuffer error message, set iff !loadOk
        bool ranPcall = false;  // true iff lua_pcall was actually attempted (i.e. loadOk)
        bool pcallOk = false;
        std::string pcallError; // real lua_pcall error message, set iff ranPcall && !pcallOk
    };
    RunResult runChunk(lua_State* L, const std::string& source, const std::string& chunkName);

    // Real, per-hook outcome of one Host::fireConfirmedHooks() attempt
    // against one hook name, on one state. A genuinely different signal
    // from RunResult above - RunResult reports a script's own TOP-LEVEL
    // execution, this reports whether the engine's real call-INTO
    // mechanism (spec-lua-bindings.md Sec8.3/Sec12.3/Sec12.4/Sec12.6)
    // found/ran one of the confirmed/pattern-based hook names
    // afterward. Never conflated with RunResult - see
    // tools/lua_host_run.cpp's own separate output files.
    struct HookFireResult {
        std::string hookName;
        HookGroup group;
        // Real lua_getglobal + lua_isfunction check result - ALWAYS
        // measured, even for Group 2 (spec Sec12.4: the real engine's own
        // call there is unconditional; this project measures existence
        // anyway, honestly, without gating Group 2's actual call on it -
        // see hook_registry.h).
        bool existedAsFunction = false;
        // True iff this pass actually invoked lua_pcall on the global:
        // Group 1/3 only when existedAsFunction; Group 2 ALWAYS (matching
        // the real engine's own confirmed unconditional call).
        bool attemptedCall = false;
        bool callOk = false;    // valid iff attemptedCall
        std::string callError;  // real lua_pcall error string, valid iff attemptedCall && !callOk
    };

    // Attempts every hook in `hooks` (confirmedHooks(), hook_registry.h)
    // against `L`, in list order, per the real existence-gated mechanism
    // (Group 1/3) or the real unconditional-call mechanism (Group 2) -
    // see hook_registry.h's own top comment for the full, spec-cited
    // rationale. Most hooks are still called with ZERO arguments - spec-
    // lua-bindings.md Sec8.3 confirms the call-in MECHANISM only, not
    // per-hook argument shapes, which remain OPEN for most names (see
    // HookSpec::args's own doc comment, hook_registry.h). A small,
    // explicitly-enumerated set of hooks (spec Sec14, folded in 2026-09-29)
    // pushes real, spec-confirmed argument values instead - each HookArg in
    // spec.args is pushed, in order, before lua_pcall; `nargs` passed to
    // lua_pcall is spec.args.size(), not a hardcoded 0. `scriptFilename`
    // (new parameter, same pass) is the real, on-hand entryName of
    // whichever script this firing pass is currently running against -
    // needed to push HookArgKind::ScriptFilenameString's real value for
    // `_PrepareForDynamicGlobals` (spec Sec14.1); defaults to an empty
    // string for callers (e.g. this file's own synthetic tests) that have
    // no real script context, in which case that one arg is pushed as an
    // empty Lua string rather than fabricating a name. Uses a real Lua 5.1
    // instruction-count
    // watchdog (lua_sethook + LUA_MASKCOUNT - a real, unmodified, public
    // Lua 5.1 debug-API feature, not an invented language behavior) around
    // each individual hook call ONLY - never around runChunk() above, so
    // this cannot perturb HANDOFF.md Sec9.113's already-independently-
    // verified top-level load/pcall numbers. This is a real, evidenced
    // necessity, not defensive-programming-for-its-own-sake: real
    // game_lib.lua text (grepped directly) contains patterns like
    // `repeat thread_yield() until teleport_check_done(REMOTE_PLAYER)`,
    // and every registered stub always returns nil
    // (falsy) - so a hook body that reaches an equivalent always-nil-
    // gated busy loop would otherwise hang this tool forever. The
    // watchdog converts that into a real, recorded Lua error for that one
    // hook call (see lua_host.cpp) instead of a hang; it is a tool-safety
    // measure, not a claim about real engine timing/scheduling behavior.
    std::vector<HookFireResult> fireConfirmedHooks(lua_State* L, const std::vector<HookSpec>& hooks,
                                                    const std::string& scriptFilename = std::string());

    // The single ThreadScheduler backing this Host's thread_new/_yield/
    // _kill/_check_done/_close registration in BOTH states (see
    // thread_scheduler.h). Exposed for tools that want real, direct
    // liveCount()/isDone() introspection beyond what the Lua-visible
    // functions themselves report.
    ThreadScheduler& threadScheduler() { return threadScheduler_; }

    // The real-behavior engine-state layer backing registerSpecConfirmedStubs()
    // (spec_confirmed_stubs.h) - the 13 names this task promotes from a
    // generic logged-nil stub to real, spec-confirmed behavior. One shared
    // instance for the whole Host, matching HitLog/ThreadScheduler's own
    // existing per-Host-instance convention.
    EngineState& engineState() { return engineState_; }

    // The 24 bare globals' shared state (bare_globals.h): frame time, the
    // random ring, the include queue and the script-thread table.
    BareGlobalsState& bareGlobals() { return bareGlobals_; }
    // Registrations made by registerBareGlobals() over both states (24 + 24).
    size_t bareGlobalCount() const { return bareGlobalCount_; }

    // One pass of the script-thread scheduler 0x00e0cf50 over both states
    // (spec-lua-api-behaviour.md thread-table section, MAJOR CORRECTION
    // 2026-10-02, CONFIRMED mechanism - BareThreadTable::schedulerPass).
    // WHEN to call it is still the caller's choice, but the engine's own
    // cadence is no longer a background pump: it runs once per rendered/game
    // frame, on the main thread, from each active game state's own per-frame
    // update (0x00e0dfc0, called from e.g. 0x007b05f0) - see bare_globals.h's
    // own class-top comment for the corrected mechanism. A caller that calls
    // this once per its own rendered/game frame reproduces the real engine
    // exactly; tools/lua_host_run.cpp's mission loop does this (CHOSEN
    // tick-equals-frame mapping, kSchedulerPassesPerTick == 1).
    BareThreadTable::PassResult runScriptThreadSchedulerPass(const BareThreadTable::BeforeRun& before = {},
                                                             const BareThreadTable::AfterRun& after = {}) {
        return bareGlobals_.threads().schedulerPass({gameplay_, ui_}, before, after);
    }

    // HYPOTHESIS / HOST SUBSTITUTE, opt-in only: rand_int/rand_float draw
    // from a ring filled once by a deterministic host generator seeded with
    // `seed`, instead of the CONFIRMED file-image ring (every draw = lo until
    // a fill; the engine generator and its fill are OPEN, Sec26.27). Values
    // are not the game's. Call before any script draws.
    void useHostRng(uint64_t seed) { bareGlobals_.random().useHostRng(seed); }

    // The include queue's file source (spec-lua-bindings.md Sec16.4): maps a
    // file name (".lua" appended when missing) to its source text; false =
    // cannot be opened (ignored, as the engine ignores it). Without a
    // resolver every queued include fails silently and is logged in
    // bareGlobals().includeQueue().log().
    void setIncludeResolver(IncludeQueue::Resolver r) { bareGlobals_.includeQueue().setResolver(std::move(r)); }
    // The buffer loader's behaviour on a failed load (Sec16.4): drop the queue.
    void discardIncludeQueue() { bareGlobals_.includeQueue().discard(); }

    // --- Per-call "current document" context (2026-10-03) ----------------
    //
    // spec-lua-bindings.md Sec15/Sec18.2/Sec19.1: vint_object_find's,
    // vint_object_clone's and vint_get_time_index's "current document" is
    // the +0x14 field of the calling script's context (0x00e0ceb0;
    // spec-lua-api-behaviour.md Sec26.27 reads the same field as "the calling
    // script's own context"). EngineState models it as
    // currentDefaultDocHandle(), OPEN by default. A UI document names its own
    // script in its `lua_script_file` metadata (spec-vint-doc-format.md
    // Sec3.2), so this project CHOOSES (labelled, not a spec fact): code
    // runs in the context of the document whose lua_script_file is the chunk
    // that DEFINED it. With a resolver set:
    //  * runChunk() resolves `chunkName` before the pcall;
    //  * fireConfirmedHooks() resolves the hook function's own defining
    //    chunk (lua_getinfo's `source` - runChunk passes the script's entry
    //    name as the chunk name, so this is that script's name), so a hook
    //    defined by script A but fired during script B's pass (the
    //    persistent-state artifact tools/lua_host_run.cpp's top comment
    //    describes) still runs in A's document, not B's.
    // The resolver returns the document handle, or nullopt for "this chunk
    // has no known document" (the current document is then OPEN for the
    // call, as before). The previous value is restored after every call.
    // Without a resolver (the default, and every existing test) nothing
    // changes. NOT covered: script-thread resumption by the scheduler
    // (thread bodies resume with whatever context the caller left, OPEN
    // under lua_host_run) - Sec26.27's "inherited by child threads" is not
    // modelled.
    using DocumentContextResolver = std::function<std::optional<uint32_t>(const std::string& chunkName)>;
    void setDocumentContextResolver(DocumentContextResolver r) { docContextResolver_ = std::move(r); }

private:
    lua_State* gameplay_ = nullptr;
    lua_State* ui_ = nullptr;
    HitLog hitLog_;
    size_t gameplayStubCount_ = 0;
    size_t uiStubCount_ = 0;
    ThreadScheduler threadScheduler_;
    EngineState engineState_;
    BareGlobalsState bareGlobals_;
    size_t bareGlobalCount_ = 0;
    DocumentContextResolver docContextResolver_;

    // Sets currentDefaultDocHandle() for one call from the resolver (see
    // setDocumentContextResolver) and restores the previous value when it
    // goes out of scope. A no-op without a resolver.
    class DocumentContextScope {
    public:
        DocumentContextScope(Host& h, const std::string& chunkName);
        ~DocumentContextScope();
        DocumentContextScope(const DocumentContextScope&) = delete;
        DocumentContextScope& operator=(const DocumentContextScope&) = delete;

    private:
        Host& host_;
        bool active_ = false;
        bool hadValue_ = false;
        uint32_t previous_ = 0;
    };
};

} // namespace sr3luahost
