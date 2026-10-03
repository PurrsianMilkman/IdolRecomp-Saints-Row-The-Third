// Bodies of the 24 bare globals of 0x00e0f900 (bare_globals.h), from
// spec-lua-api-behaviour.md Sec26.27 and spec-lua-bindings.md
// Sec13.2/Sec16.3/Sec16.4. Labels at each body: CONFIRMED is implemented as
// specced; HIGH CONFIDENCE only where labelled; OPEN refuses through
// OpenStateError (open_state.h).
//
// Shared argument handling (Sec26.27, CONFIRMED): argument k is read at index
// k - 1 - n (n = argument count), so the first argument sits at -n and
// arguments beyond a function's arity are ignored from the end. That is the
// same slot as positional index k whenever n >= k, so the bodies read
// positionally. When fewer arguments are passed than a body reads, the engine
// result depends on the index resolver 0x00dfdc60 (HYPOTHESIS); the spec's
// host note allows treating a missing argument as 0, which is what a
// positional read past the top gives here (lua_tonumber of none = 0).
// Numbers come from lua_tonumber (a nil or non-numeric argument reads as 0,
// strings convert), nothing checks counts or types, and every numeric wrapper
// narrows each argument to single precision before computing and narrows its
// result before widening it for the push.
//
// NO LUA ERROR MAY CROSS A C++ FRAME (lua_spec_confirmed_stubs.cpp, fix after
// bridge job 20260930T225845-team-b-jklk): bodies never call lua_error or an
// erroring Lua API themselves. A refusal is a C++ throw (OpenStateError or
// PendingLuaError); bareGuardCall<Fn> (noinline) turns it into a message on
// the Lua stack, and only bareGuard's frame, which holds no C++ object and no
// try block, calls lua_error. thread_yield's yield is raised the same way
// (lua_yield from bareGuard's frame). Lua code a body must run (a keyed read
// that may hit __index, _GetAnyGlobalSilent, a thread body) runs under
// lua_pcall/lua_resume, so its errors stay inside that call.
#include "sr3luahost/bare_globals.h"

#include <cfloat>
#include <cmath>
#include <cstring>
#include <utility>

#include "sr3luahost/open_state.h"

namespace sr3luahost {

const std::vector<BareGlobalRosterEntry>& bareGlobalRoster() {
    // spec-lua-bindings.md Sec13.2 / spec-lua-api-behaviour.md Sec26.27, CONFIRMED.
    static const std::vector<BareGlobalRosterEntry> rows = {
        {"abs", 0x00e0f140, true},
        {"acos", 0x00e0f180, true},
        {"cos", 0x00e0f1c0, true},
        {"sin", 0x00e0f210, true},
        {"ceil", 0x00e0f260, true},
        {"debug_print", 0x007c9f50, false},
        {"assert_msg", 0x007c9f50, false},
        {"floor", 0x00e0f3a0, true},
        {"get_frame_time", 0x00e0f400, false},
        {"include", 0x00e0f010, false},
        {"max", 0x00e0f2c0, true},
        {"min", 0x00e0f330, true},
        {"rand_float", 0x00e0f430, false},
        {"rand_int", 0x00e0f4c0, false},
        {"round", 0x00e0f530, false},
        {"sizeof_table", 0x00e0f580, false},
        {"sqrt", 0x00e0f5c0, true},
        {"strstr", 0x00e0f080, false},
        {"thread_check_done", 0x00e0f610, false},
        {"thread_kill", 0x00e0f650, false},
        {"thread_new", 0x00e0f680, false},
        {"thread_yield", 0x00e0f0d0, false},
        {"closest_point_on_line_segment", 0x00e0f740, false},
        {"which_side_of_2d_line", 0x00e0f830, false},
    };
    return rows;
}

// ---------------------------------------------------------------------
// BareRandomSource
// ---------------------------------------------------------------------
BareRandomSource::BareRandomSource() : ring_(kRingSize, 0u) {}

uint32_t BareRandomSource::hostStep() {
    // splitmix64 (public-domain reference algorithm), high 32 bits. HOST
    // SUBSTITUTE: the engine generator 0x00dab3c0 is OPEN.
    hostState_ += 0x9E3779B97F4A7C15ull;
    uint64_t z = hostState_;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return static_cast<uint32_t>(z >> 32);
}

void BareRandomSource::useHostRng(uint64_t seed) {
    mode_ = Mode::HostRng;
    hostSeed_ = seed;
    hostState_ = seed;
    for (auto& v : ring_) v = hostStep(); // the whole ring, in order (as the fill does)
    cursor_ = 0;
}

void BareRandomSource::fill(uint32_t seed) {
    if (mode_ == Mode::FileImage) {
        throw OpenStateError("random ring fill 0x00dab5a0 (generator 0x00dab3c0/0x00dab370)",
                             "spec-lua-api-behaviour.md Sec26.27");
    }
    if (seed != 0) hostState_ = seed; // CONFIRMED shape: a non-zero seed reseeds first, zero continues
    for (auto& v : ring_) v = hostStep();
    cursor_ = 0;
}

uint32_t BareRandomSource::next() {
    uint32_t r = ring_[cursor_];
    ++cursor_;
    if (cursor_ == kRingSize) cursor_ = 0; // CONFIRMED: wraps to 0 at 0x2000
    ++draws_;
    return r;
}

// ---------------------------------------------------------------------
// IncludeQueue
// ---------------------------------------------------------------------
namespace {

bool endsWithLuaCaseInsensitive(const std::string& s) {
    if (s.size() < 4) return false;
    const char* tail = s.c_str() + s.size() - 4;
    const char* want = ".lua";
    for (int i = 0; i < 4; ++i) {
        char c = tail[i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != want[i]) return false;
    }
    return true;
}

} // namespace

bool IncludeQueue::push(const std::string& name) {
    if (pending_.size() >= kCapacity) return false;
    pending_.push_back(name);
    return true;
}

void IncludeQueue::drain(lua_State* L, const std::string& stateTag) {
    // CONFIRMED (Sec16.4): in order, names queued during the drain load in the
    // same pass, failures are ignored, then the queue is cleared. Each load is
    // the file worker: `.lua` appended when missing (case-insensitive), the
    // chunk named exactly as queued, run with a protected call, no arguments.
    for (size_t i = 0; i < pending_.size(); ++i) {
        IncludeLoad rec;
        rec.name = pending_[i];
        rec.fileName = endsWithLuaCaseInsensitive(rec.name) ? rec.name : rec.name + ".lua";
        rec.state = stateTag;
        std::string source;
        rec.resolved = resolver_ && resolver_(rec.fileName, source);
        if (rec.resolved) {
            int top = lua_gettop(L);
            rec.loadOk = luaL_loadbuffer(L, source.data(), source.size(), rec.name.c_str()) == 0;
            if (rec.loadOk) {
                rec.runOk = lua_pcall(L, 0, LUA_MULTRET, 0) == 0;
            }
            if (!rec.loadOk || !rec.runOk) {
                const char* m = lua_tostring(L, -1);
                rec.error = m ? m : "(no error message)";
            }
            // The engine leaves results and error values on the stack; the
            // host restores the top so the caller's stack is unchanged.
            lua_settop(L, top);
        } else {
            rec.error = resolver_ ? "cannot be opened (resolver has no such file)" : "no include resolver set";
        }
        log_.push_back(std::move(rec));
    }
    pending_.clear();
}

// ---------------------------------------------------------------------
// BareThreadTable
// ---------------------------------------------------------------------
namespace {

const void* registryOf(lua_State* L) {
    return lua_topointer(L, LUA_REGISTRYINDEX);
}

// Runs under lua_pcall: arg 1 is the global's name; returns the value of the
// plain (metamethod-honouring) global read the runner's step 4 performs
// (field read 0x00dfe610, HIGH CONFIDENCE identity). No C++ object here.
int fetchGlobalProtected(lua_State* L) {
    lua_pushvalue(L, 1);
    lua_gettable(L, LUA_GLOBALSINDEX);
    return 1;
}

} // namespace

const BareThreadTable::Record* BareThreadTable::currentRecord() const {
    // 0x00e0ceb0, CONFIRMED: null when the depth is 0 or above 16.
    if (current_.empty() || current_.size() > kMaxCurrentDepth) return nullptr;
    return current_.back();
}

size_t BareThreadTable::liveCount() const {
    return records_.size();
}

BareThreadTable::Record* BareThreadTable::findById(uint16_t id) {
    for (auto& r : records_)
        if (r->id == id) return r.get();
    return nullptr;
}

const BareThreadTable::Record* BareThreadTable::findAlive(uint16_t id) const {
    // Finder 0x00e0caa0, CONFIRMED: not killed, still alive (not yet started,
    // or an active call frame, or a non-empty stack - call-frame probe
    // 0x00e01710 is a HIGH CONFIDENCE identity, lua_getstack here), that id.
    for (const auto& r : records_) {
        if (r->id != id || r->killed) continue;
        if (r->notStarted) return r.get();
        lua_Debug ar;
        if (lua_getstack(r->co, 0, &ar) > 0) return r.get();
        if (lua_gettop(r->co) > 0) return r.get();
    }
    return nullptr;
}

void BareThreadTable::kill(uint16_t id) {
    // CONFIRMED: sets the killed bit, nothing else; unknown/finished/killed ids do nothing.
    const Record* r = findAlive(id);
    if (r) const_cast<Record*>(r)->killed = true;
}

uint16_t BareThreadTable::nextId() {
    // HOST SUBSTITUTE: the id assignment policy is OPEN (Sec26.27). Counts up
    // from 1, wraps below 65535 ("no thread"), skips ids still in the table.
    for (;;) {
        lastId_ = static_cast<uint16_t>(lastId_ + 1);
        if (lastId_ == 0 || lastId_ == kNoThread) lastId_ = 1;
        if (!findById(lastId_)) return lastId_;
    }
}

BareThreadTable::Record* BareThreadTable::allocate(lua_State* L, const std::string& name, int nargs, bool hasParent) {
    if (records_.size() >= kCapacity) return nullptr; // HIGH CONFIDENCE capacity 256 -> 65535
    auto rec = std::make_unique<Record>();
    rec->id = nextId();
    rec->name = name;
    rec->hasParent = hasParent;
    rec->registry = registryOf(L);
    rec->co = lua_newthread(L);
    // Step 6: the arguments, in order, from the caller's stack to the
    // coroutine's (0x00dfdd80); the new thread object sits above them.
    lua_insert(L, -(nargs + 1));
    if (nargs > 0) lua_xmove(L, rec->co, nargs);
    rec->argCount = nargs;
    rec->ref = luaL_ref(L, LUA_REGISTRYINDEX); // pops the thread object
    records_.push_back(std::move(rec));
    return records_.back().get();
}

void BareThreadTable::release(lua_State* L, Record* rec) {
    // 0x00e0c650: the record is freed and reported not alive. Swap-compaction
    // (Sec26.27, RESOLVED 2026-10-02, CONFIRMED): the freed slot is
    // overwritten by the current last live record, the vacated last slot is
    // cleared and the count decremented - so array order is not creation order
    // once anything has been released.
    luaL_unref(L, LUA_REGISTRYINDEX, rec->ref);
    for (size_t i = 0; i < records_.size(); ++i) {
        if (records_[i].get() == rec) {
            if (i + 1 != records_.size()) records_[i] = std::move(records_.back());
            records_.pop_back();
            break;
        }
    }
}

bool BareThreadTable::run(lua_State* L, Record* rec, std::string& fetchError) {
    fetchError.clear();
    if (rec->registry != registryOf(L)) {
        fetchError = "thread record belongs to another Lua state";
        lastResumeStatus_ = kNoResume;
        return true; // left untouched
    }
    // Step 2: killed -> release.
    if (rec->killed) {
        lastResumeStatus_ = kNoResume;
        release(L, rec);
        return false;
    }
    // Step 3: re-entry guard - a record already resumed is alive, not
    // resumed. The guard is +0x18 bit 0, which step 5 sets on every resume and
    // only the scheduler 0x00e0cf50 clears (Sec26.27, RESOLVED 2026-10-02,
    // CONFIRMED): a record is resumable again only after a schedulerPass().
    // A record on the current-thread stack always has the bit set, so this
    // also covers true re-entry.
    if (rec->started) {
        lastResumeStatus_ = kNoResume;
        return true;
    }
    int nargs = 0;
    if (rec->notStarted) {
        // Step 4: the global named at +0x10 placed below the arguments.
        lua_pushcfunction(L, fetchGlobalProtected);
        lua_pushlstring(L, rec->name.data(), rec->name.size());
        if (lua_pcall(L, 1, 1, 0) != 0) {
            const char* m = lua_tostring(L, -1);
            fetchError = m ? m : "(no error message)";
            lua_pop(L, 1);
            lastResumeStatus_ = kNoResume;
            release(L, rec);
            return false;
        }
        lua_xmove(L, rec->co, 1);
        lua_insert(rec->co, 1);
        rec->notStarted = false;
        nargs = rec->argCount;
    }
    // HOST SUBSTITUTE for later resumes: no values (the engine re-passes the
    // original argument count; thread_yield's value on resumption is OPEN).
    // Step 5: bit 0, push on the current-thread stack, resume, pop.
    rec->started = true;
    current_.push_back(rec);
    int status = lua_resume(rec->co, nargs);
    current_.pop_back();
    // Set after lua_resume returns, so any nested run (a thread_new inside the
    // body) has already written its own value: this is the outer record's.
    lastResumeStatus_ = status;
    if (status == LUA_YIELD) return true; // Step 6: yielded -> alive
    if (status != 0) {
        // Step 6: error -> the error callback keyed by the parent's +0x08 (not
        // modelled, OPEN), then release.
        const char* m = lua_tostring(rec->co, -1);
        errors_.push_back(rec->name + ": " + (m ? m : "(no error message)"));
    }
    release(L, rec); // finished (or failed) -> release, not alive
    return false;
}

uint16_t BareThreadTable::startThread(lua_State* L, const std::string& name, std::string& fetchError) {
    fetchError.clear();
    Record* rec = allocate(L, name, 0, false);
    if (!rec) return kNoThread;
    uint16_t id = rec->id;
    return run(L, rec, fetchError) ? id : kNoThread;
}

bool BareThreadTable::resume(lua_State* L, uint16_t id, std::string& fetchError) {
    fetchError.clear();
    Record* rec = findById(id);
    if (!rec) return false;
    return run(L, rec, fetchError);
}

BareThreadTable::PassResult BareThreadTable::schedulerPass(const std::vector<lua_State*>& states,
                                                            const BeforeRun& before, const AfterRun& after) {
    // 0x00e0cf50 (Sec26.27, RESOLVED 2026-10-02, CONFIRMED - disassembly).
    PassResult result;
    size_t i = 0;
    while (i < records_.size()) { // the live count, re-read every step
        Record* rec = records_[i].get();
        ++result.visited;
        // Unconditional: the re-entrancy flag of every visited record,
        // exempt or not - the only code that clears it.
        rec->started = false;
        lua_State* owner = nullptr;
        for (lua_State* s : states)
            if (s && registryOf(s) == rec->registry) owner = s;
        PassEvent ev;
        ev.id = rec->id;
        ev.name = rec->name;
        bool exempt = false;
        for (const ExemptKey& k : exempt_)
            if (k.registry == rec->registry) exempt = true;
        if (exempt) {
            ev.exempt = true;
            ++result.exemptSkipped;
        } else if (owner) {
            // No budget: every live, non-exempt record is offered a resume.
            if (before) before(*rec);
            ev.killedBefore = rec->killed;
            ev.resumed = true;
            ++result.resumed;
            ev.aliveAfter = run(owner, rec, ev.fetchError); // rec may be freed here
            ev.bodyRan = lastResumeStatus_ != kNoResume;
            // The record's own error is the last one pushed (nested threads'
            // errors, if any, were pushed before it).
            if (ev.bodyRan && lastResumeStatus_ != 0 && lastResumeStatus_ != LUA_YIELD && !errors_.empty())
                ev.error = errors_.back();
        }
        // Swap-compaction: if the visited record left the table, its slot now
        // holds the record that was last, so the same index is examined again
        // (re-reading the slot rather than trusting the runner's return value,
        // which is also "not alive" for a record it leaves alone).
        // (Live ids are unique, so the id identifies the slot's occupant
        // without touching a possibly freed pointer.)
        const bool stillHere = i < records_.size() && records_[i]->id == ev.id;
        if (!stillHere) {
            ++result.released;
            ev.aliveAfter = false;
        } else {
            ++i;
        }
        if (after) after(ev);
    }
    return result;
}

bool BareThreadTable::exemptState(lua_State* L) {
    const void* key = registryOf(L);
    for (ExemptKey& k : exempt_) {
        if (k.registry == key) {
            ++k.refs;
            return true;
        }
    }
    if (exempt_.size() >= kExemptCapacity) return false; // OPEN: what a 5th distinct key does
    exempt_.push_back({key, 1});
    return true;
}

void BareThreadTable::unexemptState(lua_State* L) {
    const void* key = registryOf(L);
    for (size_t i = 0; i < exempt_.size(); ++i) {
        if (exempt_[i].registry != key) continue;
        if (--exempt_[i].refs <= 0) exempt_.erase(exempt_.begin() + static_cast<std::ptrdiff_t>(i));
        return;
    }
}

bool BareThreadTable::isExempt(lua_State* L) const {
    const void* key = registryOf(L);
    for (const ExemptKey& k : exempt_)
        if (k.registry == key) return true;
    return false;
}

std::vector<uint16_t> BareThreadTable::liveIds() const {
    std::vector<uint16_t> ids;
    ids.reserve(records_.size());
    for (const auto& r : records_) ids.push_back(r->id);
    return ids;
}

// ---------------------------------------------------------------------
// The 24 Lua functions.
// ---------------------------------------------------------------------
namespace {

struct PendingLuaError {
    std::string message;
};

BareGlobalsState* upState(lua_State* L) {
    return static_cast<BareGlobalsState*>(lua_touserdata(L, lua_upvalueindex(1)));
}
HitLog* upLog(lua_State* L) {
    return static_cast<HitLog*>(lua_touserdata(L, lua_upvalueindex(2)));
}
const char* upStateTag(lua_State* L) {
    return lua_tostring(L, lua_upvalueindex(3));
}

// Same argc/arg-type logging every other host function uses (HitLog ranking).
void logCall(lua_State* L, const char* name) {
    HitLog* log = upLog(L);
    if (!log) return;
    int argc = lua_gettop(L);
    std::string types;
    for (int i = 1; i <= argc; ++i) {
        if (i > 1) types += ",";
        types += lua_typename(L, lua_type(L, i));
    }
    const char* st = upStateTag(L);
    log->record(name, argc, types, st ? st : "");
}

// IEEE double -> single rounding (what the engine's 32-bit stores do),
// written out for out-of-range magnitudes so the C++ conversion is never
// undefined: values from FLT_MAX + half an ulp up become infinity.
float narrow(double d) {
    if (std::isfinite(d) && std::fabs(d) > static_cast<double>(FLT_MAX)) {
        const double halfUlpAboveMax = static_cast<double>(FLT_MAX) + std::ldexp(1.0, 103);
        float mag = std::fabs(d) >= halfUlpAboveMax ? INFINITY : FLT_MAX;
        return d < 0 ? -mag : mag;
    }
    return static_cast<float>(d);
}

// Argument k as the narrowed float every numeric wrapper computes on.
float argFloat(lua_State* L, int k) {
    return narrow(lua_tonumber(L, k));
}

void pushFloat(lua_State* L, float f) {
    lua_pushnumber(L, static_cast<lua_Number>(f));
}

// lua_tonumber + 0x00ea2596 (truncation toward zero, Sec4.1) for rand_int and
// the thread ids. Refused as OPEN: NaN/inf/out of int64 range (Sec4.1 does not
// settle them), and any value whose truncation depends on whether it was
// narrowed to single precision first - Sec26.27's shared-handling paragraph
// says every numeric wrapper narrows each argument, while the rand_int and
// thread_check_done entries describe a plain lua_tonumber + 0x00ea2596
// conversion. Where both readings agree (every integer below 2^24, every
// value a float holds exactly) the result is the same either way.
int64_t truncArg(lua_Number v, const char* fn) {
    double t = std::trunc(v);
    if (!(t >= -9223372036854775808.0 && t < 9223372036854775808.0)) {
        throw OpenStateError("0x00ea2596 conversion of a non-finite or out-of-range number", "spec-lua-api-behaviour.md Sec4.1");
    }
    double tn = std::trunc(static_cast<double>(narrow(v)));
    if (tn != t) {
        throw OpenStateError(std::string(fn) + ": single-precision narrowing before 0x00ea2596 (shared-handling paragraph vs the " + fn +
                                 " entry)",
                             "spec-lua-api-behaviour.md Sec26.27");
    }
    return static_cast<int64_t>(t);
}

// ---- standard-math names ---------------------------------------------

// abs (0x00e0f140), CONFIRMED: clears the sign bit of the float.
int bare_abs(lua_State* L) {
    logCall(L, "abs");
    pushFloat(L, std::fabs(argFloat(L, 1)));
    return 1;
}

// acos (0x00e0f180): CONFIRMED clamp into [-1, 1] by 0x00e0f0f0 (an unordered
// compare - NaN - takes the "at or below -1" path), result narrowed. HIGH
// CONFIDENCE (labelled): 0x00ea2b90 is the C run-time arc-cosine, radians.
int bare_acos(lua_State* L) {
    logCall(L, "acos");
    float x = argFloat(L, 1);
    if (!(x > -1.0f)) {
        x = -1.0f;
    } else if (x >= 1.0f) {
        x = 1.0f;
    }
    pushFloat(L, narrow(std::acos(static_cast<double>(x))));
    return 1;
}

// cos / sin (0x00e0f1c0 / 0x00e0f210): CONFIRMED no unit conversion, result
// narrowed. HIGH CONFIDENCE (labelled): the callees are the run-time cosine
// and sine (radians); the engine's SSE2/x87 leaves may differ from this C
// library in the last bit before narrowing.
int bare_cos(lua_State* L) {
    logCall(L, "cos");
    pushFloat(L, narrow(std::cos(static_cast<double>(argFloat(L, 1)))));
    return 1;
}
int bare_sin(lua_State* L) {
    logCall(L, "sin");
    pushFloat(L, narrow(std::sin(static_cast<double>(argFloat(L, 1)))));
    return 1;
}

// ceil / floor (0x00e0f260 / 0x00e0f3a0), CONFIRMED: the float, widened, to
// the run-time ceil/floor, narrowed, then converted to a 32-bit integer by a
// truncating conversion (exact for an integral float). Out of range (2^31 and
// up after narrowing, below -2^31, NaN): the spec's -2147483648 is HIGH
// CONFIDENCE only (the SSE2 flag's writer not dumped) - refused as OPEN.
int roundedToInt32(lua_State* L, double integral, const char* fn) {
    float f = narrow(integral);
    if (!(f >= -2147483648.0f && f < 2147483648.0f)) {
        throw OpenStateError(std::string(fn) + " result outside the 32-bit range (0x00ea2560 overflow)",
                             "spec-lua-api-behaviour.md Sec26.27");
    }
    lua_pushnumber(L, static_cast<lua_Number>(static_cast<int32_t>(f)));
    return 1;
}
int bare_ceil(lua_State* L) {
    logCall(L, "ceil");
    return roundedToInt32(L, std::ceil(static_cast<double>(argFloat(L, 1))), "ceil");
}
int bare_floor(lua_State* L) {
    logCall(L, "floor");
    return roundedToInt32(L, std::floor(static_cast<double>(argFloat(L, 1))), "floor");
}

// max / min (0x00e0f2c0 / 0x00e0f330), CONFIRMED: strictly binary; both
// narrowed and compared once; the first argument wins only on a strict
// ordered comparison, otherwise (ties, 0 vs -0, NaN) the second.
int bare_max(lua_State* L) {
    logCall(L, "max");
    float a = argFloat(L, 1);
    float b = argFloat(L, 2);
    pushFloat(L, (b < a) ? a : b);
    return 1;
}
int bare_min(lua_State* L) {
    logCall(L, "min");
    float a = argFloat(L, 1);
    float b = argFloat(L, 2);
    pushFloat(L, (a < b) ? a : b);
    return 1;
}

// sqrt (0x00e0f5c0): CONFIRMED wrapper shape and no negative guard; HIGH
// CONFIDENCE (labelled): 0x00ea3f60 is the run-time square root (NaN for a
// negative argument).
int bare_sqrt(lua_State* L) {
    logCall(L, "sqrt");
    pushFloat(L, narrow(std::sqrt(static_cast<double>(argFloat(L, 1)))));
    return 1;
}

// ---- engine functions ----------------------------------------------------

// debug_print / assert_msg (0x007c9f50), CONFIRMED: read the count, discard
// it, return nothing. The HitLog line is the host's own diagnostics.
int bare_debug_print(lua_State* L) {
    logCall(L, "debug_print");
    return 0;
}
int bare_assert_msg(lua_State* L) {
    logCall(L, "assert_msg");
    return 0;
}

// get_frame_time (0x00e0f400), CONFIRMED: the float at 0x0132a0b0, widened;
// 1/30 in the file image. Writer OPEN, so the value never changes here.
int bare_get_frame_time(lua_State* L) {
    logCall(L, "get_frame_time");
    pushFloat(L, upState(L)->frameTime());
    return 1;
}

// include (0x00e0f010): spec-lua-bindings.md Sec16.3/Sec16.4. CONFIRMED
// mechanism, HIGH CONFIDENCE link to this global (implemented, labelled):
// appends the name to the shared 64-slot queue, loads nothing now. OPEN (the
// body was not re-read): how a non-string argument is read and what a full
// queue does - both refused; no return value is specced, none is pushed.
int bare_include(lua_State* L) {
    logCall(L, "include");
    size_t len = 0;
    const char* s = (lua_type(L, 1) == LUA_TSTRING || lua_type(L, 1) == LUA_TNUMBER) ? lua_tolstring(L, 1, &len) : nullptr;
    if (!s) {
        throw OpenStateError("include argument that is not a string (body 0x00e0f010 not re-read)",
                             "spec-lua-bindings.md Sec16.3");
    }
    if (!upState(L)->includeQueue().push(std::string(s, len))) {
        throw OpenStateError("include queue full (64 slots at 0x02a44e48; 0x00e0e140 not re-read)",
                             "spec-lua-bindings.md Sec16.3");
    }
    return 0;
}

// rand_float (0x00e0f430), CONFIRMED: both narrowed, swapped when the second
// is strictly less (NaN: no swap); u = narrow(r * 2^-32) from the next ring
// value; returns narrow(lo + (hi - lo) * u). The engine evaluates the last
// step in x87 extended precision; double is used here before the narrowing.
int bare_rand_float(lua_State* L) {
    logCall(L, "rand_float");
    float lo = argFloat(L, 1);
    float hi = argFloat(L, 2);
    if (hi < lo) std::swap(lo, hi);
    uint32_t r = upState(L)->random().next();
    float u = narrow(static_cast<double>(r) * 2.3283064365386962890625e-10); // 2^-32, CONFIRMED (0x0111e4c0)
    double v = static_cast<double>(lo) + (static_cast<double>(hi) - static_cast<double>(lo)) * static_cast<double>(u);
    pushFloat(L, narrow(v));
    return 1;
}

// rand_int (0x00e0f4c0), CONFIRMED: both arguments truncated toward zero to
// 32-bit integers (0x00ea2596, Sec4.1); swapped when the second is smaller;
// lo + (r mod (hi - lo + 1)) with an unsigned 32-bit modulus - inclusive at
// both ends. A value outside the 32-bit range is not specced (OPEN refusal).
// hi - lo + 1 == 0 (the full 32-bit range) divides by zero in the engine; "a
// host need not reproduce this" - raised as an error.
int bare_rand_int(lua_State* L) {
    logCall(L, "rand_int");
    int64_t a = truncArg(lua_tonumber(L, 1), "rand_int");
    int64_t b = truncArg(lua_tonumber(L, 2), "rand_int");
    if (a < INT32_MIN || a > INT32_MAX || b < INT32_MIN || b > INT32_MAX) {
        throw OpenStateError("rand_int argument outside the 32-bit range", "spec-lua-api-behaviour.md Sec26.27");
    }
    int32_t lo = static_cast<int32_t>(a);
    int32_t hi = static_cast<int32_t>(b);
    if (hi < lo) std::swap(lo, hi);
    uint32_t span = static_cast<uint32_t>(hi) - static_cast<uint32_t>(lo) + 1u;
    if (span == 0) {
        throw PendingLuaError{"rand_int: the full 32-bit range divides by zero in the engine (Sec26.27); refused by the host"};
    }
    uint32_t r = upState(L)->random().next();
    uint32_t v = static_cast<uint32_t>(lo) + (r % span);
    lua_pushnumber(L, static_cast<lua_Number>(static_cast<int32_t>(v)));
    return 1;
}

// round (0x00e0f530 -> 0x00dad900), CONFIRMED: narrowed first; half away from
// zero (x >= 0: add 0.5 in double and truncate; else subtract 0.5 and
// truncate); NaN and magnitudes outside the 32-bit range give -2147483648;
// the result is an integer (never -0).
int bare_round(lua_State* L) {
    logCall(L, "round");
    double x = static_cast<double>(argFloat(L, 1));
    double t = (x >= 0.0) ? std::trunc(x + 0.5) : std::trunc(x - 0.5);
    int32_t out = INT32_MIN;
    if (t >= -2147483648.0 && t <= 2147483647.0) out = static_cast<int32_t>(t);
    lua_pushnumber(L, static_cast<lua_Number>(out));
    return 1;
}

// sizeof_table body, run under lua_pcall (no C++ object in this frame):
// arg 1 is the value. Returns the raw "n" field when it is a number (true as
// second result), else the pair count (false). A non-table value with an
// __index that let the keyed read pass cannot be walked: returns nil.
int sizeofTableProtected(lua_State* L) {
    lua_getfield(L, 1, "n"); // metamethod-honouring keyed read (raises on a non-indexable value)
    if (lua_type(L, -1) == LUA_TNUMBER) {
        lua_pushboolean(L, 1);
        return 2;
    }
    lua_pop(L, 1);
    if (!lua_istable(L, 1)) {
        lua_pushnil(L);
        lua_pushboolean(L, 0);
        return 2;
    }
    lua_Number count = 0;
    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
        lua_pop(L, 1);
        count += 1;
    }
    lua_pushnumber(L, count);
    lua_pushboolean(L, 0);
    return 2;
}

// sizeof_table (0x00e0f580 -> 0x0083dff0), CONFIRMED: no argument or nil ->
// -1; a numeric "n" field -> that number truncated toward zero, nothing
// counted; otherwise every key/value pair (array and hash parts, holes do not
// stop it). A non-table raises the VM's "attempt to index" error (HIGH
// CONFIDENCE; the spec allows a host to raise) - the protected keyed read's
// own error is re-raised. OPEN refusals: a non-table that the keyed read
// accepted (the engine would walk a non-table), and an "n" whose truncation
// is non-finite or outside the 32-bit range.
int bare_sizeof_table(lua_State* L) {
    logCall(L, "sizeof_table");
    if (lua_gettop(L) < 1 || lua_isnil(L, 1)) {
        lua_pushnumber(L, -1);
        return 1;
    }
    lua_pushcfunction(L, sizeofTableProtected);
    lua_pushvalue(L, 1);
    if (lua_pcall(L, 1, 2, 0) != 0) {
        const char* m = lua_tostring(L, -1);
        std::string message = m ? m : "sizeof_table: error indexing argument 1";
        lua_pop(L, 1);
        throw PendingLuaError{message};
    }
    bool fromN = lua_toboolean(L, -1) != 0;
    bool walked = !lua_isnil(L, -2);
    lua_Number v = lua_tonumber(L, -2);
    lua_pop(L, 2);
    if (!walked) {
        throw OpenStateError("sizeof_table pair walk of a non-table value", "spec-lua-api-behaviour.md Sec26.27");
    }
    if (fromN) {
        double t = std::trunc(v);
        if (!(t >= -2147483648.0 && t <= 2147483647.0)) {
            throw OpenStateError("sizeof_table 'n' field outside the 32-bit range", "spec-lua-api-behaviour.md Sec26.27");
        }
        v = t;
    }
    lua_pushnumber(L, v);
    return 1;
}

// strstr (0x00e0f080), CONFIRMED: both read as strings (numbers convert),
// the C run-time byte-wise substring search, s first; a boolean result.
// A nil/boolean/table/function argument reaches the search as a null pointer
// and crashes the engine; the spec asks a host to raise (or return false):
// raised here.
int bare_strstr(lua_State* L) {
    logCall(L, "strstr");
    const char* s = (lua_type(L, 1) == LUA_TSTRING || lua_type(L, 1) == LUA_TNUMBER) ? lua_tolstring(L, 1, nullptr) : nullptr;
    const char* sub = (lua_type(L, 2) == LUA_TSTRING || lua_type(L, 2) == LUA_TNUMBER) ? lua_tolstring(L, 2, nullptr) : nullptr;
    if (!s || !sub) {
        throw PendingLuaError{"strstr: argument is not a string or number (the engine searches through a null pointer, "
                              "Sec26.27); raised by the host"};
    }
    lua_pushboolean(L, std::strstr(s, sub) != nullptr ? 1 : 0);
    return 1;
}

// thread_new step 3, run under lua_pcall (no C++ object in this frame):
// arg 1 is the name. Returns (helper exists, result is a function).
int getAnyGlobalSilentProtected(lua_State* L) {
    lua_getglobal(L, "_GetAnyGlobalSilent");
    if (lua_type(L, -1) != LUA_TFUNCTION) {
        lua_pushboolean(L, 0);
        lua_pushboolean(L, 0);
        return 2;
    }
    lua_pushvalue(L, 1);
    lua_call(L, 1, 1);
    int isFunction = lua_type(L, -1) == LUA_TFUNCTION;
    lua_pushboolean(L, 1);
    lua_pushboolean(L, isFunction);
    return 2;
}

uint16_t threadIdArg(lua_State* L, const char* fn) {
    return static_cast<uint16_t>(static_cast<uint64_t>(truncArg(lua_tonumber(L, 1), fn)) & 0xFFFFu);
}

// thread_check_done (0x00e0f610), CONFIRMED: id truncated toward zero, low 16
// bits; true when no alive, non-killed record has that id.
int bare_thread_check_done(lua_State* L) {
    logCall(L, "thread_check_done");
    uint16_t id = threadIdArg(L, "thread_check_done");
    lua_pushboolean(L, upState(L)->threads().findAlive(id) == nullptr ? 1 : 0);
    return 1;
}

// thread_kill (0x00e0f650), CONFIRMED: sets the killed bit; no return value.
int bare_thread_kill(lua_State* L) {
    logCall(L, "thread_kill");
    upState(L)->threads().kill(threadIdArg(L, "thread_kill"));
    return 0;
}

// thread_new (0x00e0f680), steps per Sec26.27 (CONFIRMED unless labelled):
// 1. name read as a string (numbers convert); the current record fetched.
// 2. a name that is neither string nor number -> 65535.
// 3. _GetAnyGlobalSilent(name) must return a function, else 65535. The host
//    runs it protected; a missing _GetAnyGlobalSilent, or an error inside it,
//    is OPEN (how 0x00e0cef0 calls it) and refused.
// 4. allocate a record: with no current record the engine reads through a
//    null pointer; the spec allows the host to raise, which it does. A full
//    table (capacity 256, HIGH CONFIDENCE) -> 65535.
// 5. the parent's +0x14 context is copied (never read by the host).
// 6. the remaining arguments move, in order, to the coroutine.
// 7. the runner starts it at once: the global is fetched by name at that
//    moment and runs until its first thread_yield or its end.
// 8. still suspended -> its id; completed or errored -> 65535 (HIGH
//    CONFIDENCE that completion nulls the pointer).
int bare_thread_new(lua_State* L) {
    logCall(L, "thread_new");
    int n = lua_gettop(L);
    int t = lua_type(L, 1);
    if (t != LUA_TSTRING && t != LUA_TNUMBER) {
        lua_pushnumber(L, BareThreadTable::kNoThread);
        return 1;
    }
    size_t len = 0;
    const char* s = lua_tolstring(L, 1, &len);
    std::string name(s, len);
    BareThreadTable& threads = upState(L)->threads();

    // Steps 3: under lua_pcall (the globals read may hit a metamethod, and
    // _GetAnyGlobalSilent is script code), so no Lua error crosses this frame.
    lua_pushcfunction(L, getAnyGlobalSilentProtected);
    lua_pushlstring(L, name.data(), name.size());
    if (lua_pcall(L, 1, 2, 0) != 0) {
        const char* m = lua_tostring(L, -1);
        std::string message = std::string("thread_new: _GetAnyGlobalSilent raised (protection of 0x00e0cef0's call is OPEN): ") +
                              (m ? m : "(no error message)");
        lua_pop(L, 1);
        throw PendingLuaError{message};
    }
    bool helperExists = lua_toboolean(L, -2) != 0;
    bool isFunction = lua_toboolean(L, -1) != 0;
    lua_pop(L, 2);
    if (!helperExists) {
        throw OpenStateError("_GetAnyGlobalSilent is not a function when thread_new runs (expected from system_lib.lua, HYPOTHESIS)",
                             "spec-lua-api-behaviour.md Sec26.27");
    }
    if (!isFunction) {
        lua_pushnumber(L, BareThreadTable::kNoThread);
        return 1;
    }
    if (!threads.currentRecord()) {
        throw PendingLuaError{"thread_new: no script thread is current (the engine reads through a null record here, "
                              "Sec26.27 step 1; its top-level thread entry is OPEN); raised by the host"};
    }
    BareThreadTable::Record* rec = threads.allocate(L, name, n - 1, true);
    if (!rec) {
        lua_settop(L, n);
        lua_pushnumber(L, BareThreadTable::kNoThread);
        return 1;
    }
    uint16_t id = rec->id;
    std::string fetchError;
    size_t errorsBefore = threads.errors().size();
    bool alive = threads.run(L, rec, fetchError);
    if (!fetchError.empty()) throw PendingLuaError{"thread_new: " + fetchError};
    if (threads.errors().size() > errorsBefore && upLog(L)) {
        // The thread raised: the engine calls the error callback keyed by the
        // parent's +0x08 (OPEN, not modelled); the host records it instead.
        const char* st = upStateTag(L);
        upLog(L)->record("thread_new:THREAD_ERROR(error callback OPEN)", 0, "", st ? st : "");
    }
    lua_pushnumber(L, alive ? id : BareThreadTable::kNoThread);
    return 1;
}

// Sentinel results of a body for bareGuard (never a real return count).
constexpr int kGuardRaise = -1000;
constexpr int kGuardYield = -1001;

// thread_yield (0x00e0f0d0), CONFIRMED: discards its arguments and yields the
// current coroutine with no values. Outside a coroutine stock Lua raises
// "attempt to yield across metamethod/C-call boundary" (HIGH CONFIDENCE the
// engine does too) - lua_yield in bareGuard's frame produces exactly that.
int bare_thread_yield(lua_State* L) {
    logCall(L, "thread_yield");
    return kGuardYield;
}

// closest_point_on_line_segment (0x00e0f740 -> 0x00db96b0), CONFIRMED: the
// point of the closed segment A-B nearest to P; A when A == B; else
// A + t(B - A), t = ((P - A).(B - A)) / |B - A|^2 clamped to [0, 1] (a NaN t
// clamps to 0). B - A, the numerator, the denominator, the quotient and each
// output component are narrowed; other intermediates (x87 extended in the
// engine) are double here. Two results, x then y.
int bare_closest_point_on_line_segment(lua_State* L) {
    logCall(L, "closest_point_on_line_segment");
    float px = argFloat(L, 1), py = argFloat(L, 2);
    float ax = argFloat(L, 3), ay = argFloat(L, 4);
    float bx = argFloat(L, 5), by = argFloat(L, 6);
    if (ax == bx && ay == by) {
        pushFloat(L, ax);
        pushFloat(L, ay);
        return 2;
    }
    float dx = narrow(static_cast<double>(bx) - ax);
    float dy = narrow(static_cast<double>(by) - ay);
    float num = narrow((static_cast<double>(px) - ax) * dx + (static_cast<double>(py) - ay) * dy);
    float den = narrow(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy);
    float t = narrow(static_cast<double>(num) / static_cast<double>(den));
    if (!(t >= 0.0f)) t = 0.0f; // below 0, or NaN
    if (t > 1.0f) t = 1.0f;
    pushFloat(L, narrow(static_cast<double>(ax) + static_cast<double>(t) * dx));
    pushFloat(L, narrow(static_cast<double>(ay) + static_cast<double>(t) * dy));
    return 2;
}

// which_side_of_2d_line (0x00e0f830), CONFIRMED formula on a1..a6, rounded
// once to single precision: (a2 - a4)(a5 - a3) - (a1 - a3)(a6 - a4). Computed
// in double here (x87 extended in the engine).
int bare_which_side_of_2d_line(lua_State* L) {
    logCall(L, "which_side_of_2d_line");
    double a1 = argFloat(L, 1), a2 = argFloat(L, 2), a3 = argFloat(L, 3);
    double a4 = argFloat(L, 4), a5 = argFloat(L, 5), a6 = argFloat(L, 6);
    pushFloat(L, narrow((a2 - a4) * (a5 - a3) - (a1 - a3) * (a6 - a4)));
    return 1;
}

#if defined(_MSC_VER)
#define CRREISH_BARE_NOINLINE __declspec(noinline)
#else
#define CRREISH_BARE_NOINLINE __attribute__((noinline))
#endif

// Calls the body. On a refusal, leaves the message on the Lua stack and
// returns kGuardRaise. Never inlined into bareGuard, so the frame that calls
// lua_error/lua_yield has no try block and no C++ object.
template <lua_CFunction Fn>
CRREISH_BARE_NOINLINE int bareGuardCall(lua_State* L) {
    try {
        return Fn(L);
    } catch (const OpenStateError& e) {
        std::string tag = std::string("OPEN_STATE:") + e.global();
        HitLog* log = upLog(L);
        const char* st = upStateTag(L);
        if (log) log->record(tag, lua_gettop(L), "", st ? st : "");
        lua_pushstring(L, e.what());
    } catch (const PendingLuaError& e) {
        lua_pushstring(L, e.message.c_str());
    }
    return kGuardRaise;
}

template <lua_CFunction Fn>
int bareGuard(lua_State* L) {
    int r = bareGuardCall<Fn>(L);
    if (r == kGuardRaise) return lua_error(L);
    if (r == kGuardYield) return lua_yield(L, 0);
    return r;
}

struct BareBinding {
    const char* name;
    lua_CFunction fn;
};

const BareBinding kBindings[] = {
    {"abs", bareGuard<bare_abs>},
    {"acos", bareGuard<bare_acos>},
    {"cos", bareGuard<bare_cos>},
    {"sin", bareGuard<bare_sin>},
    {"ceil", bareGuard<bare_ceil>},
    {"debug_print", bareGuard<bare_debug_print>},
    {"assert_msg", bareGuard<bare_assert_msg>},
    {"floor", bareGuard<bare_floor>},
    {"get_frame_time", bareGuard<bare_get_frame_time>},
    {"include", bareGuard<bare_include>},
    {"max", bareGuard<bare_max>},
    {"min", bareGuard<bare_min>},
    {"rand_float", bareGuard<bare_rand_float>},
    {"rand_int", bareGuard<bare_rand_int>},
    {"round", bareGuard<bare_round>},
    {"sizeof_table", bareGuard<bare_sizeof_table>},
    {"sqrt", bareGuard<bare_sqrt>},
    {"strstr", bareGuard<bare_strstr>},
    {"thread_check_done", bareGuard<bare_thread_check_done>},
    {"thread_kill", bareGuard<bare_thread_kill>},
    {"thread_new", bareGuard<bare_thread_new>},
    {"thread_yield", bareGuard<bare_thread_yield>},
    {"closest_point_on_line_segment", bareGuard<bare_closest_point_on_line_segment>},
    {"which_side_of_2d_line", bareGuard<bare_which_side_of_2d_line>},
};

} // namespace

void registerBareGlobals(lua_State* L, BareGlobalsState& state, HitLog& log, const std::string& stateTag,
                         const std::vector<std::string>& names) {
    state.setHitLog(&log);
    // Registration order = the roster order (Sec13.2), for every wanted name.
    for (const BareBinding& b : kBindings) {
        bool wanted = false;
        for (const auto& n : names)
            if (n == b.name) wanted = true;
        if (!wanted) continue;
        lua_pushlightuserdata(L, &state);
        lua_pushlightuserdata(L, &log);
        lua_pushstring(L, stateTag.c_str());
        lua_pushcclosure(L, b.fn, 3);
        lua_setglobal(L, b.name);
    }
}

} // namespace sr3luahost
