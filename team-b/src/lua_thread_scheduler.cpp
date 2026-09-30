#include "sr3luahost/thread_scheduler.h"

namespace sr3luahost {

int64_t ThreadScheduler::newThread(lua_State* L) {
    lua_State* co = lua_newthread(L); // real Lua 5.1 API: pushes `co` onto L's own stack, returns its pointer.
    int ref = luaL_ref(L, LUA_REGISTRYINDEX); // pops `co` off L's stack, anchors it in L's own registry so it survives GC while this handle exists.
    int64_t handle = nextHandle_++;
    ThreadHandleEntry e;
    e.ownerState = L;
    e.thread = co;
    e.registryRef = ref;
    e.killed = false;
    handles_[handle] = e;
    return handle;
}

void ThreadScheduler::killOrClose(int64_t handle) {
    auto it = handles_.find(handle);
    if (it == handles_.end()) return;  // unknown handle - silent no-op, see header comment
    if (it->second.killed) return;     // already killed - idempotent
    luaL_unref(it->second.ownerState, LUA_REGISTRYINDEX, it->second.registryRef);
    it->second.killed = true;
}

bool ThreadScheduler::isDone(int64_t handle) const {
    auto it = handles_.find(handle);
    if (it == handles_.end()) return true; // unknown handle: nothing left to wait on
    if (it->second.killed) return true;
    lua_State* co = it->second.thread;
    // The exact real status/stack/top idiom Lua 5.1's OWN coroutine.status
    // (third_party/lua51/src/lbaselib.c, luaB_costatus) uses to decide
    // "dead" - transcribed here, not reinvented, so this project's own
    // "done" reading matches the real reference VM's own logic exactly.
    int status = lua_status(co);
    switch (status) {
        case LUA_YIELD:
            return false; // genuinely suspended mid-run
        case 0: {
            lua_Debug ar;
            if (lua_getstack(co, 0, &ar) > 0) return false; // has an active call frame - "normal" (running something), not done
            if (lua_gettop(co) == 0) return true;            // no frames, empty stack - "dead" by the real idiom
            return false;                                    // no frames but a non-empty stack - "suspended, never yet resumed" (the real idiom's own initial state)
        }
        default:
            return true; // a real error occurred inside this coroutine - "dead" by the real idiom
    }
}

namespace {

ThreadScheduler* upScheduler(lua_State* L) {
    return static_cast<ThreadScheduler*>(lua_touserdata(L, lua_upvalueindex(1)));
}
HitLog* upLog(lua_State* L) {
    return static_cast<HitLog*>(lua_touserdata(L, lua_upvalueindex(2)));
}
// Upvalue 3 (added same-day follow-up pass, mirrors stub_registry.cpp's
// own genericStubTrampoline): the real state tag ("gameplay"/"ui") this
// registration call was made for.
const char* upStateTag(lua_State* L) {
    return lua_tostring(L, lua_upvalueindex(3));
}

// Same real-argc/real-arg-type logging convention as
// stub_registry.cpp's own genericStubTrampoline - factored out here so
// these 5 real functions log identically to every other generic stub
// (folds them into the SAME HitLog ranking, tools/lua_host_run.cpp),
// including the same real per-state tracking.
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

int stub_thread_new(lua_State* L) {
    logCall(L, upLog(L), "thread_new", upStateTag(L));
    int64_t handle = upScheduler(L)->newThread(L);
    lua_pushinteger(L, static_cast<lua_Integer>(handle));
    return 1;
}

int stub_thread_yield(lua_State* L) {
    logCall(L, upLog(L), "thread_yield", upStateTag(L));
    // real Lua 5.1 API: lua_pushthread pushes L's own thread object and
    // returns 1 iff L IS the main thread of its global state. Popped
    // immediately - only the return value is used.
    int isMain = lua_pushthread(L);
    lua_pop(L, 1);
    if (isMain) {
        // No active coroutine to yield from. Every call this project's
        // own firing loop makes (Host::runChunk's top-level pcall, and
        // Host::fireConfirmedHooks's hook calls) executes on the main
        // thread of gameplay_/ui_, never inside a lua_resume()'d
        // coroutine - see thread_scheduler.h's own top "SCOPE" note - so
        // this is the branch every call observed by this pass actually
        // takes. Recorded honestly as a real no-op rather than silently
        // "succeeding" as a real yield would, or erroring.
        return 0;
    }
    // Real yield - only reachable once something in this project actually
    // lua_resume()s a thread_new()-created thread, which nothing does yet.
    return lua_yield(L, 0);
}

int stub_thread_kill(lua_State* L) {
    logCall(L, upLog(L), "thread_kill", upStateTag(L));
    if (lua_gettop(L) >= 1 && lua_isnumber(L, 1)) {
        upScheduler(L)->killOrClose(static_cast<int64_t>(lua_tointeger(L, 1)));
    }
    return 0;
}

int stub_thread_check_done(lua_State* L) {
    logCall(L, upLog(L), "thread_check_done", upStateTag(L));
    bool done = true;
    if (lua_gettop(L) >= 1 && lua_isnumber(L, 1)) {
        done = upScheduler(L)->isDone(static_cast<int64_t>(lua_tointeger(L, 1)));
    }
    lua_pushboolean(L, done ? 1 : 0);
    return 1;
}

int stub_thread_close(lua_State* L) {
    logCall(L, upLog(L), "thread_close", upStateTag(L));
    // Identical to thread_kill - see thread_scheduler.h's own doc note on
    // why no fabricated distinction is made between them.
    if (lua_gettop(L) >= 1 && lua_isnumber(L, 1)) {
        upScheduler(L)->killOrClose(static_cast<int64_t>(lua_tointeger(L, 1)));
    }
    return 0;
}

void registerOne(lua_State* L, ThreadScheduler& sched, HitLog& log, const std::string& stateTag,
                  const char* name, lua_CFunction fn) {
    lua_pushlightuserdata(L, &sched);
    lua_pushlightuserdata(L, &log);
    lua_pushstring(L, stateTag.c_str());
    lua_pushcclosure(L, fn, 3);
    lua_setglobal(L, name);
}

} // namespace

void registerThreadScheduler(lua_State* L, ThreadScheduler& scheduler, HitLog& log, const std::string& stateTag) {
    registerOne(L, scheduler, log, stateTag, "thread_new", stub_thread_new);
    registerOne(L, scheduler, log, stateTag, "thread_yield", stub_thread_yield);
    registerOne(L, scheduler, log, stateTag, "thread_kill", stub_thread_kill);
    registerOne(L, scheduler, log, stateTag, "thread_check_done", stub_thread_check_done);
    registerOne(L, scheduler, log, stateTag, "thread_close", stub_thread_close);
}

} // namespace sr3luahost
