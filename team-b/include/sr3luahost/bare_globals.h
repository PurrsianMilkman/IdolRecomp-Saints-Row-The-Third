// The 24 bare globals of the engine registrar 0x00e0f900
// (spec-lua-bindings.md Sec13.2/Sec16.3/Sec16.4, spec-lua-api-behaviour.md
// Sec26.27, cloud phase 2026-10-01).
//
// CONFIRMED - disassembly (jobs 20261001T020218-team-a-bgcx,
// 20261001T021641-team-a-yduu, 20261001T114555-team-a-lgdz): the generic
// state creator 0x00e0e0b0 registers all 24 names as bare globals (not a
// `math` table) into BOTH Lua states, right after the base library and
// `coroutine`, before system_lib.lua and before every other registrar. Host
// (host.h) registers them first, into both states, through specBareGlobals()
// (spec_confirmed_stubs.h); they are never generic stubs.
//
// Bodies: lua_bare_globals.cpp. Each one implements what Sec26.27 marks
// CONFIRMED; HIGH CONFIDENCE parts are implemented only where labelled at the
// call site; OPEN parts refuse with the project's OpenStateError mechanism
// (open_state.h). The state the bodies need (frame time, the random ring,
// the include queue, the script-thread table) lives in BareGlobalsState,
// owned by Host and shared by both states, as the engine's globals are.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "sr3luahost/lua_c_api.h"
#include "sr3luahost/stub_registry.h"

namespace sr3luahost {

// One roster row (spec-lua-bindings.md Sec13.2 table, CONFIRMED - name,
// order and native address).
struct BareGlobalRosterEntry {
    const char* name;
    uint32_t native;
    bool standardMathName; // "standard math name" in the roster: describes the NAME only (Sec26.27)
};
// All 24, in registration order.
const std::vector<BareGlobalRosterEntry>& bareGlobalRoster();

// ---------------------------------------------------------------------
// The shared random ring (Sec26.27 "The shared random ring" / "The
// random-source object"). CONFIRMED: every draw reads the next 32-bit entry of
// an 8192-entry ring at the cursor, then advances the cursor and wraps it to
// 0 at 8192; a fill resets the cursor and regenerates the whole ring. Before
// the first fill the cursor and entry 0 are 0 (CONFIRMED) and the whole ring
// is zero (HIGH CONFIDENCE), so every draw yields 0.
//
// OPEN: the generator (0x00dab3c0/0x00dab370), the seeds, and who fills the
// ring and when (callers 0x00dd98f0, 0x009c1ff0, 0x005d25f0 not dumped).
//
// Two modes:
//  * FileImage (default): the ring exactly as the file image holds it, and no
//    fill. Draws return 0, so rand_int/rand_float return `lo`. This is the
//    CONFIRMED "before the first fill" behaviour; whether the engine has
//    filled the ring before a given script draws is itself OPEN, so a run in
//    this mode answers for "no fill yet". Any value that would need the
//    generator (a fill) is refused as OPEN.
//  * HostRng: HYPOTHESIS / HOST SUBSTITUTE, opt-in only (Host::useHostRng,
//    lua_host_run --host-rng). Sec26.27's host note: "use the host's own
//    generator behind the same contracts". The ring is filled ONCE, at the
//    switch, from a deterministic host generator (splitmix64, high 32 bits)
//    seeded with an explicit seed; the cursor rules are unchanged and the
//    ring is never refilled ("a host that never refills matches the engine's
//    shape"). Values are NOT the game's.
// ---------------------------------------------------------------------
class BareRandomSource {
public:
    static constexpr size_t kRingSize = 8192;
    enum class Mode { FileImage, HostRng };

    BareRandomSource();
    Mode mode() const { return mode_; }
    uint64_t hostSeed() const { return hostSeed_; }
    void useHostRng(uint64_t seed);
    // Engine fill 0x00dab5a0(seed): needs the OPEN generator in FileImage
    // mode (throws OpenStateError); in HostRng mode refills from the host
    // generator (a non-zero seed reseeds it first, as 0x00dab5a0 does).
    void fill(uint32_t seed);
    // The next ring value; advances the cursor and wraps it at kRingSize.
    uint32_t next();
    size_t cursor() const { return cursor_; }
    uint64_t draws() const { return draws_; }

private:
    uint32_t hostStep();
    Mode mode_ = Mode::FileImage;
    uint64_t hostSeed_ = 0;
    uint64_t hostState_ = 0;
    size_t cursor_ = 0;
    uint64_t draws_ = 0;
    std::vector<uint32_t> ring_;
};

// ---------------------------------------------------------------------
// The deferred `include` queue (spec-lua-bindings.md Sec16.3/Sec16.4).
// CONFIRMED mechanism (HIGH CONFIDENCE link from the global `include`): one
// 64-slot queue shared by all states; `include(name)` loads nothing and
// appends the name; after a successful load the loader drains the queue in
// order into the same state (names queued during the drain load in the same
// pass, through the file worker, so `.lua` is appended when missing,
// case-insensitively, and failures are ignored), then clears it.
// Host::runChunk drains after a successful run. The buffer loader instead
// discards the queue on a failed load (Host::discardIncludeQueue).
// OPEN: the bodies of 0x00e0f010/0x00e0e140 (the duplicate check, HYPOTHESIS,
// is not implemented; a full queue and a non-string argument refuse as OPEN).
// ---------------------------------------------------------------------
struct IncludeLoad {
    std::string name;       // as queued
    std::string fileName;   // with `.lua` appended when missing
    std::string state;      // state tag the drain loaded it into
    bool resolved = false;  // the host resolver returned a source
    bool loadOk = false;
    bool runOk = false;
    std::string error;
};
class IncludeQueue {
public:
    static constexpr size_t kCapacity = 64;
    // Resolves a file name (".lua" appended) to its source; false = cannot be opened.
    using Resolver = std::function<bool(const std::string& fileName, std::string& source)>;

    const std::vector<std::string>& pending() const { return pending_; }
    bool push(const std::string& name); // false when full
    void discard() { pending_.clear(); }
    void setResolver(Resolver r) { resolver_ = std::move(r); }
    // Drain into L (the state whose load just succeeded). Results go to log().
    void drain(lua_State* L, const std::string& stateTag);
    const std::vector<IncludeLoad>& log() const { return log_; }

private:
    std::vector<std::string> pending_;
    Resolver resolver_;
    std::vector<IncludeLoad> log_;
};

// ---------------------------------------------------------------------
// The script-thread table (Sec26.27 "The script-thread table", the runner
// 0x00e0cba0, thread_new/thread_kill/thread_check_done/thread_yield).
// Implemented (CONFIRMED unless labelled): records carrying an id, a
// coroutine, a not-yet-started flag, the global function name, started and
// killed bits and the argument count; the current-thread stack of up to 16
// (currentRecord() is null at depth 0 or above 16); the runner steps 2-7;
// the finder's "alive" probe.
// HIGH CONFIDENCE, implemented with a label: capacity 256 (a full table gives
// 65535); the release on completion.
// HOST SUBSTITUTES (OPEN in the spec), labelled: the id policy (next free id
// counting up from 1, wrapping below 65535); resumes after the first run pass
// no values (the engine re-passes the original argument count; what
// thread_yield returns is OPEN); the error callback selected by the parent's
// +0x08 key is not modelled - the error is recorded in errors() and the
// HitLog instead. The parent's +0x08/+0x14 values are never read by the host.
// OPEN, not implemented: the per-frame scheduler cadence (nothing resumes a
// yielded thread except an explicit resume() call); the engine hooks at
// 0x02a44d60/0x02a44d64; the top-level entry that pushes the first record
// (startThread() is the host's explicit entry for tools and tests).
// ---------------------------------------------------------------------
class BareThreadTable {
public:
    static constexpr uint16_t kNoThread = 65535;
    static constexpr size_t kCapacity = 256;      // HIGH CONFIDENCE (allocators not dumped)
    static constexpr size_t kMaxCurrentDepth = 16;

    struct Record {
        uint16_t id = 0;
        lua_State* co = nullptr;
        int ref = LUA_NOREF;       // anchors `co` in its state's registry
        bool notStarted = true;    // +0x0c
        std::string name;          // +0x10
        bool started = false;      // +0x18 bit 0
        bool killed = false;       // +0x18 bit 2
        int argCount = 0;          // +0x1c
        bool hasParent = false;    // false for a startThread() root (its context is OPEN)
        const void* registry = nullptr; // identifies the owning Lua state (its registry table)
    };

    BareThreadTable() = default;
    BareThreadTable(const BareThreadTable&) = delete;
    BareThreadTable& operator=(const BareThreadTable&) = delete;

    // 0x00e0ceb0: the record on top of the current-thread stack, or null.
    const Record* currentRecord() const;
    size_t currentDepth() const { return current_.size(); }
    size_t liveCount() const;

    // Finder 0x00e0caa0: a record that is not killed, still alive and has `id`.
    const Record* findAlive(uint16_t id) const;
    // thread_kill: sets the killed bit of findAlive(id), nothing else.
    void kill(uint16_t id);

    // Allocates a record for `name` and moves the top `nargs` values of L's
    // stack (above the caller's other values) onto its coroutine. Returns null
    // when the table is full. `hasParent` records whether a current record
    // existed (thread_new) or not (startThread).
    Record* allocate(lua_State* L, const std::string& name, int nargs, bool hasParent);

    // The runner 0x00e0cba0. L must belong to the same Lua state as the
    // record. Returns true when the record is still alive afterwards. Lua
    // errors from the thread never leave this call (lua_resume is protected);
    // a failure to fetch the global by name is reported through
    // `fetchError` (non-empty) after the record is released.
    bool run(lua_State* L, Record* rec, std::string& fetchError);

    // Host entry for tools/tests: allocate a root record for global `name`
    // (no arguments) and run it at once. Returns its id while it is suspended,
    // else kNoThread. The engine's own top-level entry is OPEN.
    uint16_t startThread(lua_State* L, const std::string& name, std::string& fetchError);
    // Runner on the live record `id` (scheduling cadence OPEN: only explicit
    // calls resume). Returns true when it is still alive afterwards.
    bool resume(lua_State* L, uint16_t id, std::string& fetchError);

    // Errors raised inside threads ("<name>: <message>"), in order.
    const std::vector<std::string>& errors() const { return errors_; }

private:
    void release(lua_State* L, Record* rec);
    Record* findById(uint16_t id);
    uint16_t nextId();

    std::vector<std::unique_ptr<Record>> records_;
    std::vector<Record*> current_;
    std::vector<std::string> errors_;
    uint16_t lastId_ = 0;
};

// Everything the 24 bodies keep between calls, one per Host.
class BareGlobalsState {
public:
    // get_frame_time (Sec26.27): the single-precision engine global
    // 0x0132a0b0, 1/30 in the file image (CONFIRMED). Its writer is OPEN, so
    // nothing in the host changes it.
    float frameTime() const { return frameTime_; }

    BareRandomSource& random() { return random_; }
    IncludeQueue& includeQueue() { return includeQueue_; }
    BareThreadTable& threads() { return threads_; }
    HitLog* hitLog() const { return log_; }
    void setHitLog(HitLog* log) { log_ = log; }

private:
    float frameTime_ = 1.0f / 30.0f;
    BareRandomSource random_;
    IncludeQueue includeQueue_;
    BareThreadTable threads_;
    HitLog* log_ = nullptr;
};

// Registers each roster name present in `names` into L as a C closure over
// (state, log, stateTag). Host calls it once per state, before any other
// registration (engine order: base + coroutine, the 24, then system_lib.lua).
void registerBareGlobals(lua_State* L, BareGlobalsState& state, HitLog& log, const std::string& stateTag,
                         const std::vector<std::string>& names);

} // namespace sr3luahost
