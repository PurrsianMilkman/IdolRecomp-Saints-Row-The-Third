// A REAL, but deliberately MINIMAL, backing store for this project's own
// registration of thread_new/thread_yield/thread_kill/thread_check_done/
// thread_close.
//
// Why this exists (confirmed directly, not guessed, per HANDOFF.md's own
// write-up for this task): these 5 names are called extensively by real
// shipped scripts (thread_yield alone: 1,474 call sites / 94 scripts,
// thread_new 931/113, thread_kill 591/85, thread_check_done 192/33,
// thread_close 1/1 - tools/lua_reconciliation_called_not_registered_101.tsv)
// but are NEITHER in the 1,430-name registered list NOR defined anywhere in
// game_lib.lua's own real text (grepped directly: zero `function thread_`,
// zero `thread_yield =`, zero use of Lua's own real `coroutine.*` library
// either). The real engine is expected to provide these as real
// C-registered functions backing Lua's own coroutine model; nothing in
// this project did until this pass.
//
// Real evidence used to shape this minimal implementation (game_lib.lua's
// own real call sites, read directly - this is real shipped game data,
// not disassembly):
//   local handle = thread_new("group_create_check_done_loop", group_name)
//   while (not thread_check_done(handle)) do thread_yield() end
//   ...
//   if (type(threads) == "number") then thread_kill(threads) ... end
// This confirms: thread_new's first argument is a NAME (a string, not a
// Lua function reference - real call sites pass a literal engine-routine
// name plus ordinary data arguments, never a closure), a handle is
// checked with `type(x) == "number"`, and is later passed straight to
// thread_kill/thread_check_done. This project's own choice to represent a
// handle as a plain Lua number (see ThreadScheduler::newThread) is
// therefore evidenced by real call-site usage, not invented from nothing
// - but the real ENGINE's own internal handle representation remains
// UNCONFIRMED; this is a minimal placeholder good enough to let scripts
// progress, not a claim about what the real engine actually returns.
//
// SCOPE, stated plainly (see also each function's own doc comment below,
// and this project's HANDOFF.md write-up for this task): this is NOT a
// scheduler that actually runs coroutine bodies. thread_new() creates a
// real, empty Lua 5.1 thread (lua_newthread, third_party/lua51's real,
// unmodified public C API - not invented) and returns a handle; nothing in
// this pass ever lua_resume()s it (this project's own hook-firing pass,
// hook_registry.h/Host::fireConfirmedHooks, always runs on the MAIN
// thread of gameplay_/ui_, never inside a resumed coroutine). Concretely
// OPEN / left unconfirmed by this pass:
//   - Real blocking/scheduling semantics: does thread_yield() actually
//     suspend the CALLING script until some later engine tick resumes it,
//     or something else entirely? Not modeled - this project's own
//     thread_yield() is a real lua_yield() ONLY when called from inside an
//     actually-resumed coroutine (never the case yet in this project's own
//     firing loop; see thread_yield's own doc comment), a no-op otherwise.
//   - Real return values thread_new/thread_check_done actually produce on
//     the real engine.
//   - Real scheduling order across multiple simultaneously-live threads.
//   - Whether thread_kill and thread_close are really behaviorally
//     distinct on the real engine - no call-site or spec evidence was
//     found this pass to support a real difference, so both are
//     implemented identically here (stated plainly, not a guessed-at
//     distinction).
#pragma once

#include <cstdint>
#include <unordered_map>

#include "sr3luahost/lua_c_api.h"
#include "sr3luahost/stub_registry.h"

namespace sr3luahost {

// One live (or recently killed) coroutine handle's bookkeeping.
struct ThreadHandleEntry {
    // The lua_State whose registry `registryRef` below lives in - i.e.
    // whichever of Host's two persistent states (gameplay_ or ui_) called
    // thread_new(). ALL registry operations on this handle (thread_kill/
    // thread_close's real luaL_unref call) use THIS pointer, never
    // whatever L a later call happens to receive - this project's two
    // states are each built from an entirely separate luaL_newstate(), so
    // they do NOT share one global state/registry, and using the wrong
    // one would corrupt an unrelated Lua registry. See thread_scheduler.cpp.
    lua_State* ownerState = nullptr;
    // The real coroutine lua_State* created by lua_newthread(ownerState) -
    // queried by lua_status()/lua_getstack()/lua_gettop() for
    // ThreadScheduler::isDone(), never independently allocated or freed
    // directly (it is owned by ownerState's own garbage collector; keeping
    // `registryRef` alive is what keeps this pointer valid).
    lua_State* thread = nullptr;
    // luaL_ref(ownerState, LUA_REGISTRYINDEX, ...) result - anchors
    // `thread` against garbage collection while this handle exists.
    // Released by killOrClose().
    int registryRef = 0;
    // Set by killOrClose() (thread_kill()/thread_close()). Checked BEFORE
    // any real lua_status() query in isDone() - once killed, a handle
    // reads as done regardless of what the underlying coroutine's own
    // real status would say, since the whole point of kill/close is "stop
    // waiting on this."
    bool killed = false;
};

// Minimal handle table + the real Lua 5.1 coroutine-status idiom, shared
// by every thread_* trampoline (lua_thread_scheduler.cpp) registered into
// a given Host's two persistent states. One instance is meant to span the
// whole run (both states, every script), matching HitLog's own convention
// (stub_registry.h) - a handle created while running one script legitimately
// stays alive and gets queried while running a LATER script against the
// SAME persistent state, since that state's own globals (which may hold
// the handle number in a table/variable a later hook reads) persist across
// scripts exactly like everything else in this scaffold.
class ThreadScheduler {
public:
    // thread_new(): create + anchor a new real Lua 5.1 thread off `L`
    // (lua_newthread + luaL_ref, both real, unmodified, public Lua 5.1 C
    // API - third_party/lua51), return a fresh integer handle starting at
    // 1 and incrementing per call for the lifetime of this ThreadScheduler
    // instance (never reused, even across L's two different owning
    // states, so a handle numeric value alone never collides). No Lua
    // function body is attached to the created thread - see this header's
    // own top "SCOPE" note: nothing in this pass ever resumes it. `L`'s
    // own stack is left exactly as it was on entry - the created thread is
    // referenced only via the registry (see .cpp).
    int64_t newThread(lua_State* L);

    // thread_kill()/thread_close(): IDENTICAL implementation - see this
    // header's own top "SCOPE" note on why no fabricated distinction is
    // made between them. Marks the handle killed and releases its
    // registry ref (against its own stored ownerState, never whatever L
    // the call happens to arrive on). An unknown handle (already killed,
    // or a foreign/bogus id) is a silent no-op - real scripts are observed
    // (game_lib.lua) guarding thread_kill with a type/sentinel check
    // first, but a defensive no-op here costs nothing and avoids a crash
    // on a value this project's own minimal implementation didn't
    // anticipate.
    void killOrClose(int64_t handle);

    // thread_check_done(): true if the handle is unknown, was
    // killed/closed, OR the real underlying coroutine reads as "dead" by
    // the EXACT real status/stack/top idiom Lua 5.1's OWN
    // coroutine.status (third_party/lua51/src/lbaselib.c, luaB_costatus)
    // uses - transcribed faithfully in thread_scheduler.cpp, not
    // reinvented, so this project's own "done" reading matches the real
    // reference VM's own logic exactly for this one predicate. Honest,
    // un-invented consequence of this minimal implementation (stated
    // plainly, not hidden): since thread_new() never attaches a body
    // function and nothing ever lua_resume()s the thread, EVERY handle's
    // real status/frame/top reading is "no frames, empty stack" - which
    // this exact real idiom classifies as "dead" - so thread_check_done()
    // returns true on the very first check for any handle this pass ever
    // creates. A real, multi-tick-spanning coroutine body is NOT modeled -
    // see this header's own top "SCOPE" note.
    bool isDone(int64_t handle) const;

    size_t liveCount() const { return handles_.size(); }

private:
    int64_t nextHandle_ = 1;
    std::unordered_map<int64_t, ThreadHandleEntry> handles_;
};

// Registers the 5 real thread_* globals into `L`, backed by `scheduler`,
// logging every real call into `log` via the exact same HitLog::record()
// every generic stub (stub_registry.h) uses - so these 5 names fold into
// the SAME whole-run stub-hit ranking the verdict tool
// (tools/lua_host_run.cpp) already reports, rather than needing a second,
// separate counter. `stateTag` ("gameplay"/"ui") is the real state this
// particular registration call is for - passed through to every
// HitLog::record() call these 5 functions make (same real per-state
// tracking added to registerStubs(), stub_registry.h/.cpp, same day).
void registerThreadScheduler(lua_State* L, ThreadScheduler& scheduler, HitLog& log, const std::string& stateTag);

// thread_close only (2026-10-01). thread_new/thread_yield/thread_kill/
// thread_check_done are now the CONFIRMED bare globals of 0x00e0f900
// (bare_globals.h, spec-lua-api-behaviour.md Sec26.27), which Host registers
// instead of this scaffold's versions. thread_close is not in that roster nor
// in the 1,490-name registration list, so whether the engine provides it at
// all is OPEN; Host keeps this scaffold's thread_close (a no-op on any id the
// bare-global thread table hands out, since the two tables are separate).
void registerThreadClose(lua_State* L, ThreadScheduler& scheduler, HitLog& log, const std::string& stateTag);

} // namespace sr3luahost
