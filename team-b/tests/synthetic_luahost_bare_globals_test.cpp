// Synthetic tests for the 24 bare globals of 0x00e0f900 (bare_globals.h,
// src/lua_bare_globals.cpp): spec-lua-api-behaviour.md Sec26.27 and
// spec-lua-bindings.md Sec13.2/Sec16.3/Sec16.4. Expected values are the
// spec's own examples where it gives them; single-precision results are
// computed here with the same float narrowing the spec describes.
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "sr3luahost/bare_globals.h"
#include "sr3luahost/host.h"

using namespace sr3luahost;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

// Runs `code` and expects it to succeed (asserts inside the chunk do the work).
void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "bare_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

// Runs `code` and expects a Lua error whose message contains `needle`.
void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "bare_test.lua");
    bool good = r.loadOk && !r.pcallOk && r.pcallError.find(needle) != std::string::npos;
    if (!good) {
        std::cerr << "expected error containing '" << needle << "', got: "
                  << (r.pcallOk ? std::string("(success)") : r.pcallError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

double evalNumber(Host& host, lua_State* L, const std::string& expr) {
    std::string code = "BARE_TEST_RESULT = " + expr;
    ok(host, L, code);
    lua_getglobal(L, "BARE_TEST_RESULT");
    double v = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return v;
}

void setNumber(lua_State* L, const char* name, double v) {
    lua_pushnumber(L, v);
    lua_setglobal(L, name);
}

double f(double d) { return static_cast<double>(static_cast<float>(d)); }

const char* kHelper = "function _GetAnyGlobalSilent(n) return rawget(_G, n) end\n";

} // namespace

int main() {
    // ------------------------------------------------------------------
    // Registration: 24 bare globals, both states, base + coroutine only.
    {
        Host host({});
        for (lua_State* L : {host.gameplayState(), host.uiState()}) {
            for (const auto& e : bareGlobalRoster()) {
                lua_getglobal(L, e.name);
                CHECK(lua_type(L, -1) == LUA_TFUNCTION);
                lua_pop(L, 1);
            }
            ok(host, L, "assert(math == nil and string == nil and table == nil and type(coroutine) == 'table')");
        }
        CHECK(bareGlobalRoster().size() == 24);
        CHECK(std::string(bareGlobalRoster().front().name) == "abs");
        CHECK(std::string(bareGlobalRoster().back().name) == "which_side_of_2d_line");
        int math = 0;
        for (const auto& e : bareGlobalRoster()) math += e.standardMathName ? 1 : 0;
        CHECK(math == 9);
    }

    Host host({});
    lua_State* gp = host.gameplayState();
    lua_State* ui = host.uiState();
    // -2147483648 and -0 are set from C++: as Lua literals, the first trips
    // stock Lua 5.1's own signed overflow in the compiler's constant table
    // (ltable.c luaH_getnum, flagged by UBSan) and the second shares 0's slot.
    for (lua_State* L : {gp, ui}) {
        setNumber(L, "INT_MIN", -2147483648.0);
        setNumber(L, "NZ", -0.0);
        setNumber(L, "BIG", 1e10); // out of int range: as a literal, stock Lua converts it to int (UB)
    }

    // ------------------------------------------------------------------
    // max / min: strictly binary, second wins on tie or NaN, single precision.
    for (lua_State* L : {gp, ui}) {
        ok(host, L,
           "assert(max(1, 2, 3) == 2)\n"
           "assert(min(3, 2, 1) == 2)\n"
           "assert(max(1, 2) == 2 and max(2, 1) == 2 and min(1, 2) == 1 and min(2, 1) == 1)\n"
           // NZ is a runtime -0: a literal -0 shares the constant slot of 0.
           "assert(1 / max(0, NZ) < 0)\n"        // tie: second argument (-0)
           "assert(1 / max(NZ, 0) > 0)\n"
           "assert(1 / min(0, NZ) < 0 and 1 / min(NZ, 0) > 0)\n"
           "assert(max(0/0, 1) == 1)\n"
           "local n = max(1, 0/0); assert(n ~= n)\n"
           "assert(min(0/0, 1) == 1)\n"
           "local m = min(1, 0/0); assert(m ~= m)\n"
           "assert(max(nil, -3) == 0)\n"
           "assert(max('7', 2) == 7)\n"
           "assert(max(16777217, 16777216) == 16777216)\n"
           "assert(max() == 0)\n"
           "assert(select('#', max(1, 2)) == 1)\n");
    }
    CHECK(evalNumber(host, gp, "max(0.1, 0)") == 0.10000000149011612);
    CHECK(evalNumber(host, gp, "min(0.1, 1)") == f(0.1));

    // ------------------------------------------------------------------
    // floor / ceil: 32-bit integer results after narrowing.
    ok(host, gp,
       "assert(floor(2.5) == 2 and floor(-2.5) == -3)\n"
       "assert(ceil(2.1) == 3 and ceil(-2.1) == -2)\n"
       "assert(ceil(-0.5) == 0 and 1 / ceil(-0.5) > 0)\n"   // never -0
       "assert(floor(NZ) == 0 and 1 / floor(NZ) > 0)\n"
       "assert(floor(nil) == 0 and floor('3.7') == 3)\n"
       "assert(floor(0.99999999) == 1)\n"                    // narrowed to 1.0f first
       "assert(floor(-1e-9) == -1)\n"
       "assert(floor(16777217) == 16777216)\n"
       "assert(floor(INT_MIN) == INT_MIN)\n"
       "assert(ceil(5) == 5 and floor(5) == 5)\n");
    // Out of the 32-bit range: the spec's -2147483648 is HIGH CONFIDENCE only.
    fails(host, gp, "floor(2147483584)", "is OPEN");
    fails(host, gp, "ceil(0/0)", "is OPEN");
    fails(host, gp, "floor(-BIG)", "is OPEN");

    // ------------------------------------------------------------------
    // round: half away from zero, narrowed first, edge values.
    ok(host, gp,
       "assert(round(2.5) == 3 and round(3.5) == 4)\n"
       "assert(round(-2.5) == -3 and round(-0.5) == -1)\n"
       "assert(round(0.4) == 0 and round(-0.4) == 0 and 1 / round(-0.4) > 0)\n"
       "assert(round(nil) == 0)\n"
       "assert(round(16777217) == 16777216)\n"
       "assert(round(0/0) == INT_MIN)\n"
       "assert(round(BIG) == INT_MIN and round(-BIG) == INT_MIN)\n"
       "assert(round(2147483647) == INT_MIN)\n"   // narrows to 2^31: out of range
       "assert(round(1.49) == 1 and round(-1.49) == -1)\n");

    // ------------------------------------------------------------------
    // abs, acos, cos, sin, sqrt.
    ok(host, gp,
       "assert(abs(-3) == 3 and abs(3) == 3)\n"
       "assert(1 / abs(NZ) > 0)\n"
       "assert(abs(nil) == 0)\n"
       "assert(abs(16777217) == 16777216)\n"
       "local n = abs(0/0); assert(n ~= n)\n"
       "assert(acos(2) == 0 and acos(1) == 0)\n"
       "assert(cos(nil) == 1 and sin(nil) == 0)\n"
       "assert(sqrt(4) == 2 and sqrt(nil) == 0)\n"
       "local s = sqrt(-1); assert(s ~= s)\n");
    const double kPi = 3.14159265358979323846;
    CHECK(evalNumber(host, gp, "acos(-1)") == f(kPi));
    CHECK(evalNumber(host, gp, "acos(-1)") == 3.1415927410125732);
    CHECK(evalNumber(host, gp, "acos(-5)") == f(kPi));
    CHECK(evalNumber(host, gp, "acos(0/0)") == f(kPi)); // unordered compare -> "at or below -1"
    CHECK(evalNumber(host, gp, "acos(0)") == f(kPi / 2));
    CHECK(evalNumber(host, gp, "acos(nil)") == f(kPi / 2));
    CHECK(evalNumber(host, gp, "acos(0.5)") == f(std::acos(f(0.5))));
    CHECK(evalNumber(host, gp, "sin(1)") == f(std::sin(f(1.0))));
    CHECK(evalNumber(host, gp, "cos(0.1)") == f(std::cos(f(0.1))));
    CHECK(evalNumber(host, gp, "sin(90)") == f(std::sin(90.0))); // radians, no degree conversion
    CHECK(evalNumber(host, gp, "sqrt(2)") == f(std::sqrt(2.0)));

    // ------------------------------------------------------------------
    // strstr: boolean, byte-wise, case-sensitive; null pointer -> raised.
    ok(host, gp,
       "assert(strstr('abc', '') == true)\n"
       "assert(strstr('', 'a') == false)\n"
       "assert(strstr(123, '2') == true)\n"
       "assert(strstr('hello world', 'o w') == true)\n"
       "assert(strstr('abc', 'B') == false)\n"
       "assert(strstr('abc', 'abcd') == false)\n"
       "assert(type(strstr('a', 'a')) == 'boolean')\n");
    fails(host, gp, "strstr(nil, 'a')", "strstr: argument is not a string");
    fails(host, gp, "strstr('a', {})", "strstr: argument is not a string");
    fails(host, gp, "strstr('a', true)", "strstr: argument is not a string");

    // ------------------------------------------------------------------
    // sizeof_table: -1, numeric n override, every pair, not #t.
    ok(host, gp,
       "assert(sizeof_table() == -1 and sizeof_table(nil) == -1)\n"
       "assert(sizeof_table({n = 2.9}) == 2)\n"
       "assert(sizeof_table({n = -2.9}) == -2)\n"
       "assert(sizeof_table({1, 2, 3, n = 0}) == 0)\n"
       "assert(sizeof_table({1, 2, nil, 4}) == 3)\n"
       "assert(sizeof_table({a = 1, b = 2}) == 2)\n"
       "assert(sizeof_table({}) == 0)\n"
       "assert(sizeof_table({n = '5'}) == 1)\n"                          // n not a number: counted
       "assert(sizeof_table(setmetatable({}, {__index = {n = 7}})) == 7)\n" // metamethod-honouring read
       "assert(sizeof_table({[1] = 1, [100] = 2, x = 3}) == 3)\n");
    fails(host, gp, "sizeof_table('abc')", "attempt to index");
    fails(host, gp, "sizeof_table(5)", "attempt to index");
    fails(host, gp, "sizeof_table(setmetatable({}, {__index = function() error('idx boom') end}))", "idx boom");
    fails(host, gp, "local p = newproxy(true); getmetatable(p).__index = function() return nil end; sizeof_table(p)", "is OPEN");
    fails(host, gp, "sizeof_table({n = BIG})", "is OPEN");

    // ------------------------------------------------------------------
    // get_frame_time: the file-image 1/30 as a float.
    CHECK(evalNumber(host, gp, "get_frame_time()") == 0.03333333507180214);
    CHECK(evalNumber(host, ui, "get_frame_time()") == static_cast<double>(1.0f / 30.0f));
    // setFrameTime (spec-lua-api-behaviour.md Sec37.2/Sec37.5, CONFIRMED the
    // writer is a raw, unscaled, uncapped, unpaused measured delta, not the
    // fixed 1/30 the host otherwise starts at): a caller-set value is
    // reflected verbatim, with no scaling/capping/pausing applied by this
    // host, and - matching the real engine's single shared global - is the
    // SAME value read from both Lua states, since BareGlobalsState is one
    // instance per Host.
    host.bareGlobals().setFrameTime(1.0f / 3.0f);
    CHECK(evalNumber(host, gp, "get_frame_time()") == static_cast<double>(1.0f / 3.0f));
    CHECK(evalNumber(host, ui, "get_frame_time()") == static_cast<double>(1.0f / 3.0f));
    host.bareGlobals().setFrameTime(0.0f);
    CHECK(evalNumber(host, gp, "get_frame_time()") == 0.0);
    // Restore the file-image default so later checks in this file (if any
    // come to depend on it) see the same starting value this block itself
    // relied on above.
    host.bareGlobals().setFrameTime(1.0f / 30.0f);
    CHECK(evalNumber(host, gp, "get_frame_time()") == 0.03333333507180214);

    // ------------------------------------------------------------------
    // debug_print / assert_msg: accept anything, return nothing.
    ok(host, gp,
       "assert(select('#', debug_print('x', 1, {}, nil, print)) == 0)\n"
       "assert(select('#', assert_msg(false, 'should not assert')) == 0)\n"
       "assert(select('#', debug_print()) == 0 and select('#', assert_msg()) == 0)\n");

    // ------------------------------------------------------------------
    // geometry helpers.
    ok(host, gp,
       "local x, y = closest_point_on_line_segment(5, 5, 1, 1, 1, 1); assert(x == 1 and y == 1)\n"
       "x, y = closest_point_on_line_segment(2, 3, 0, 0, 4, 0); assert(x == 2 and y == 0)\n"
       "x, y = closest_point_on_line_segment(-3, 1, 0, 0, 4, 0); assert(x == 0 and y == 0)\n"
       "x, y = closest_point_on_line_segment(10, 1, 0, 0, 4, 0); assert(x == 4 and y == 0)\n"
       "x, y = closest_point_on_line_segment(1, 1, 0, 0, 2, 2); assert(x == 1 and y == 1)\n"
       "assert(select('#', closest_point_on_line_segment(0, 0, 0, 0, 1, 1)) == 2)\n"
       "assert(which_side_of_2d_line(0, 1, 0, 0, 1, 0) == 1)\n"     // left of A->B
       "assert(which_side_of_2d_line(0, -1, 0, 0, 1, 0) == -1)\n"   // right
       "assert(which_side_of_2d_line(5, 0, 0, 0, 1, 0) == 0)\n"     // on the line
       "assert(which_side_of_2d_line(0, 2, 0, 0, 3, 0) == 6)\n"     // twice the triangle area
       "assert(select('#', which_side_of_2d_line()) == 1)\n");
    {
        double x = evalNumber(host, gp, "closest_point_on_line_segment(0.1, 0, 0, 0, 1, 0)");
        CHECK(x == 0.10000000149011612);
        double w = evalNumber(host, gp, "which_side_of_2d_line(0.1, 0.2, 0, 0, 1, 0)");
        CHECK(w == f(f(0.2)));
    }

    // ------------------------------------------------------------------
    // rand_int / rand_float, default (file-image ring): every draw is lo.
    {
        Host h({});
        lua_State* L = h.gameplayState();
        CHECK(h.bareGlobals().random().mode() == BareRandomSource::Mode::FileImage);
        ok(h, L,
           "assert(rand_int(1, 10) == 1)\n"
           "assert(rand_int(10, 1) == 1)\n"            // either order
           "assert(rand_int(1.9, 3.9) == 1)\n"         // truncated: {1, 2, 3}
           "assert(rand_int(-1.5, 1.5) == -1)\n"       // truncated toward zero: {-1, 0, 1}
           "assert(rand_int(-1.5, -3.9) == -3)\n"
           "assert(rand_int(5) == 0)\n"                // missing argument read as 0 (host note)
           "assert(rand_int(7, 7) == 7)\n"
           "assert(rand_float(2, 5) == 2 and rand_float(5, 2) == 2)\n"
           "assert(rand_float(-1, -3) == -3)\n");
        CHECK(evalNumber(h, L, "rand_float(0.1, 1)") == f(0.1));
        ok(h, L, "local n = rand_float(0/0, 1); assert(n ~= n)\n"); // NaN: no swap, lo = NaN
        // 12 draws so far (11 + the evalNumber one) - the shared cursor advanced once per draw.
        CHECK(h.bareGlobals().random().draws() == 12);
        CHECK(h.bareGlobals().random().cursor() == 12);
        ok(h, L, "for i = 1, 8192 - 12 do rand_int(0, 1) end");
        CHECK(h.bareGlobals().random().cursor() == 0); // wrapped at 8192
        setNumber(L, "INT_MIN", -2147483648.0);
        setNumber(L, "BIG", 1e10);
        // The full 32-bit range divides by zero in the engine; 2147483647 is also
        // not a float, so the narrowing question (OPEN) refuses it first.
        fails(h, L, "rand_int(INT_MIN, 2147483647)", "is OPEN");
        fails(h, L, "rand_int(0/0, 1)", "is OPEN");
        fails(h, L, "rand_int(1, BIG)", "is OPEN");
        fails(h, L, "rand_int(16777217.5, 1)", "is OPEN"); // narrowing ambiguity (Sec26.27)
        // The engine fill needs the OPEN generator.
        bool refused = false;
        try {
            h.bareGlobals().random().fill(0);
        } catch (const OpenStateError& e) {
            refused = std::string(e.what()).find("0x00dab5a0") != std::string::npos;
        }
        CHECK(refused);
    }

    // rand_int / rand_float with the opt-in host RNG (HYPOTHESIS / host substitute).
    {
        const uint64_t seed = 12345;
        Host h({});
        h.useHostRng(seed);
        lua_State* L = h.gameplayState();
        BareRandomSource mirror;
        mirror.useHostRng(seed);
        CHECK(h.bareGlobals().random().mode() == BareRandomSource::Mode::HostRng);
        // Exact formula: lo + (r mod (hi - lo + 1)), unsigned 32-bit.
        struct Case { int lo, hi; };
        const Case cases[] = {{1, 6}, {6, 1}, {-5, 5}, {0, 0}, {-100, -1}, {1, 1000000}, {-16777216, 16777216}, {-2147483520, 2147483520}};
        for (const auto& c : cases) {
            int lo = c.lo < c.hi ? c.lo : c.hi, hi = c.lo < c.hi ? c.hi : c.lo;
            uint32_t span = static_cast<uint32_t>(hi) - static_cast<uint32_t>(lo) + 1u;
            int32_t expect = static_cast<int32_t>(static_cast<uint32_t>(lo) + mirror.next() % span);
            double got = evalNumber(h, L, "rand_int(" + std::to_string(c.lo) + ", " + std::to_string(c.hi) + ")");
            CHECK(got == expect);
        }
        // rand_float: lo + (hi - lo) * u, u = narrow(r * 2^-32).
        for (int i = 0; i < 50; ++i) {
            float lo = -2.5f, hi = 7.25f;
            float u = static_cast<float>(static_cast<double>(mirror.next()) * std::ldexp(1.0, -32));
            float expect = static_cast<float>(static_cast<double>(lo) + (static_cast<double>(hi) - lo) * u);
            double got = evalNumber(h, L, "rand_float(7.25, -2.5)");
            CHECK(got == static_cast<double>(expect));
        }
        // Inclusive at both ends and in range.
        ok(h, L,
           "local seen = {}\n"
           "for i = 1, 2000 do local v = rand_int(1, 6); assert(v >= 1 and v <= 6 and v == floor(v)); seen[v] = true end\n"
           "for v = 1, 6 do assert(seen[v]) end\n"
           "for i = 1, 2000 do local v = rand_float(3, 4); assert(v >= 3 and v <= 4) end\n");
        // Deterministic per seed; the ring repeats every 8192 draws (no refill).
        Host h2({});
        h2.useHostRng(seed);
        ok(h2, h2.gameplayState(),
           "FIRST = {} for i = 1, 8192 do FIRST[i] = rand_int(0, 1000000) end\n"
           "for i = 1, 8192 do assert(rand_int(0, 1000000) == FIRST[i]) end\n");
        Host h3({});
        h3.useHostRng(seed);
        Host h4({});
        h4.useHostRng(seed + 1);
        double a = evalNumber(h3, h3.gameplayState(), "rand_int(0, 1000000000)");
        double b = evalNumber(h4, h4.gameplayState(), "rand_int(0, 1000000000)");
        double a2 = evalNumber(h2, h2.gameplayState(), "rand_int(0, 1000000000)"); // h2 wrapped back to entry 0
        CHECK(a == a2);
        CHECK(a != b);
        // Host-mode fill: refills from the host generator, cursor reset.
        h3.bareGlobals().random().fill(0);
        CHECK(h3.bareGlobals().random().cursor() == 0);
    }

    // ------------------------------------------------------------------
    // include: deferred queue, drained after a successful load.
    {
        Host h({});
        lua_State* L = h.gameplayState();
        std::map<std::string, std::string> files = {
            {"lib_a.lua", "ORDER = ORDER .. 'a'; include('lib_c')"},       // queued during the drain
            {"lib_b.LUA", "ORDER = ORDER .. 'b'"},                          // `.lua` not appended (case-insensitive)
            {"lib_c.lua", "ORDER = ORDER .. 'c'"},
            {"broken.lua", "error('broken include')"},
        };
        std::vector<std::string> asked;
        h.setIncludeResolver([&](const std::string& name, std::string& src) {
            asked.push_back(name);
            auto it = files.find(name);
            if (it == files.end()) return false;
            src = it->second;
            return true;
        });
        ok(h, L,
           "ORDER = 's'\n"
           "assert(select('#', include('lib_a')) == 0)\n"
           "include('missing')\n"
           "include('broken')\n"
           "include('lib_b.LUA')\n"
           "ORDER = ORDER .. 'e'\n");   // the chunk finishes before any include runs
        ok(h, L, "assert(ORDER == 'seabc', ORDER)");
        const auto& log = h.bareGlobals().includeQueue().log();
        CHECK(log.size() == 5);
        if (log.size() == 5) {
            CHECK(log[0].name == "lib_a" && log[0].fileName == "lib_a.lua" && log[0].runOk && log[0].state == "gameplay");
            CHECK(log[1].name == "missing" && !log[1].resolved);
            CHECK(log[2].name == "broken" && log[2].resolved && log[2].loadOk && !log[2].runOk &&
                  log[2].error.find("broken include") != std::string::npos);
            CHECK(log[3].fileName == "lib_b.LUA" && log[3].runOk);
            CHECK(log[4].name == "lib_c" && log[4].runOk);
        }
        CHECK(h.bareGlobals().includeQueue().pending().empty());
        // A failed load leaves the queue; the next successful load - in ANY
        // state, the queue is shared - drains it there.
        auto bad = h.runChunk(L, "include('lib_c'); error('outer fails')", "bad.lua");
        CHECK(!bad.pcallOk);
        CHECK(h.bareGlobals().includeQueue().pending().size() == 1);
        ok(h, h.uiState(), "ORDER = 'ui'");
        ok(h, h.uiState(), "assert(ORDER == 'uic', ORDER)");
        CHECK(h.bareGlobals().includeQueue().log().back().state == "ui");
        // Buffer-loader behaviour: discard on failure.
        auto bad2 = h.runChunk(L, "include('lib_c'); error('x')", "bad2.lua");
        CHECK(!bad2.pcallOk);
        h.discardIncludeQueue();
        CHECK(h.bareGlobals().includeQueue().pending().empty());
        // OPEN refusals: a non-string name, a full queue.
        fails(h, L, "include({})", "is OPEN");
        fails(h, L, "include(nil)", "is OPEN");
        fails(h, L, "for i = 1, 65 do include('q') end", "include queue full");
        h.discardIncludeQueue();
        // No resolver: queued names fail silently and are logged.
        Host h2({});
        ok(h2, h2.gameplayState(), "include('anything')");
        CHECK(h2.bareGlobals().includeQueue().log().size() == 1 && !h2.bareGlobals().includeQueue().log()[0].resolved);
    }

    // ------------------------------------------------------------------
    // Script threads (thread_new / thread_yield / thread_kill / thread_check_done).
    {
        Host h({});
        lua_State* L = h.gameplayState();
        BareThreadTable& threads = h.bareGlobals().threads();

        // Outside any thread record: thread_yield raises the stock yield error;
        // thread_new needs _GetAnyGlobalSilent, then a current record.
        fails(h, L, "thread_yield()", "attempt to yield across metamethod/C-call boundary");
        fails(h, L, "thread_new('x')", "_GetAnyGlobalSilent");
        ok(h, L, kHelper);
        fails(h, L, "function some_fn() end; thread_new('some_fn')", "no script thread is current");
        ok(h, L, "assert(thread_new(nil) == 65535 and thread_new({}) == 65535)"); // step 2 before the record
        ok(h, L, "assert(thread_new('no_such_global') == 65535)");               // step 3
        CHECK(threads.liveCount() == 0);

        ok(h, L,
           "LOG = {}\n"
           "function child(a, b)\n"
           "  LOG[#LOG + 1] = 'child ' .. a .. b .. ' ' .. select('#', a, b)\n"
           "  CHILD_YIELD_VALUES = select('#', thread_yield())\n"
           "  LOG[#LOG + 1] = 'child resumed'\n"
           "end\n"
           "function finisher() FINISHED = true end\n"
           "function raiser() error('boom in thread') end\n"
           "function main_thread()\n"
           "  local h = thread_new('child', 'x', 'y')\n"
           "  LOG[#LOG + 1] = 'main after new'\n"
           "  R_H = h\n"
           "  R_DONE = thread_check_done(h)\n"
           "  R_FIN = thread_new('finisher')\n"
           "  R_ERR = thread_new('raiser')\n"
           "  R_NUM = thread_new(123)\n"
           "  thread_yield()\n"
           "  LOG[#LOG + 1] = 'main resumed'\n"
           "end\n");
        std::string err;
        uint16_t mainId = threads.startThread(L, "main_thread", err);
        CHECK(err.empty());
        CHECK(mainId != BareThreadTable::kNoThread);
        CHECK(threads.currentDepth() == 0);
        ok(h, L,
           "assert(LOG[1] == 'child xy 2', LOG[1])\n"   // ran at once, with its arguments
           "assert(LOG[2] == 'main after new')\n"
           "assert(type(R_H) == 'number' and R_H ~= 65535)\n"
           "assert(R_DONE == false)\n"
           "assert(FINISHED == true and R_FIN == 65535)\n"  // completed in the first run
           "assert(R_ERR == 65535)\n"
           "assert(R_NUM == 65535)\n"
           "assert(thread_check_done(R_H) == false)\n"
           "assert(thread_check_done(R_H + 65536) == false)\n"   // low 16 bits
           "assert(thread_check_done(R_H + 0.9) == false)\n"     // truncated toward zero
           "assert(thread_check_done(nil) == true)\n");          // id 0
        CHECK(threads.errors().size() == 1 && threads.errors()[0].find("raiser: ") == 0 &&
              threads.errors()[0].find("boom in thread") != std::string::npos);
        CHECK(h.hitLog().hits().count("thread_new:THREAD_ERROR(error callback OPEN)") == 1);
        CHECK(threads.liveCount() == 2); // main + child
        lua_getglobal(L, "R_H");
        uint16_t childId = static_cast<uint16_t>(lua_tonumber(L, -1));
        lua_pop(L, 1);
        CHECK(threads.liveIds() == (std::vector<uint16_t>{mainId, childId}));
        // Re-entrancy flag (+0x18 bit 0, Sec26.27 RESOLVED 2026-10-02,
        // CONFIRMED): the runner set it on each record's run and only the
        // scheduler 0x00e0cf50 clears it, so a direct resume before any pass is
        // refused - reported alive, not resumed.
        CHECK(threads.resume(L, childId, err));
        CHECK(threads.resume(L, mainId, err));
        ok(h, L, "assert(LOG[3] == nil)");
        // One scheduler pass, index order [main, child]: main runs to its end
        // and is released by swap-compaction, which moves child into slot 0;
        // the pass re-examines slot 0 and resumes child in the same pass.
        BareThreadTable::PassResult pass = h.runScriptThreadSchedulerPass();
        CHECK(pass.visited == 2 && pass.resumed == 2 && pass.released == 2 && pass.exemptSkipped == 0);
        ok(h, L,
           "assert(LOG[3] == 'main resumed', LOG[3])\n"
           "assert(LOG[4] == 'child resumed', LOG[4])\n"
           "assert(CHILD_YIELD_VALUES == 0)\n"   // resumed with no values (host substitute)
           "assert(thread_check_done(R_H) == true)\n");
        CHECK(threads.liveCount() == 0);

        // thread_kill: bit only; released at the next run; self-kill runs on
        // to its next yield.
        ok(h, L,
           "function looper() while true do LOOPS = (LOOPS or 0) + 1; thread_yield() end end\n"
           "function selfkiller() thread_yield(); thread_kill(SELF_ID); AFTER_KILL = true; thread_yield(); NEVER = true end\n"
           "function main2()\n"
           "  K1 = thread_new('looper')\n"
           "  SELF_ID = thread_new('selfkiller')\n"
           "  thread_yield()\n"
           "end\n");
        uint16_t m2 = threads.startThread(L, "main2", err);
        CHECK(m2 != BareThreadTable::kNoThread);
        ok(h, L, "assert(thread_check_done(K1) == false); assert(select('#', thread_kill(K1)) == 0)\n"
                 "assert(thread_check_done(K1) == true); thread_kill(K1); thread_kill(K1)");
        CHECK(threads.liveCount() == 3); // killed record kept until its next resume
        lua_getglobal(L, "K1");
        uint16_t k1 = static_cast<uint16_t>(lua_tonumber(L, -1));
        lua_getglobal(L, "SELF_ID");
        uint16_t selfId = static_cast<uint16_t>(lua_tonumber(L, -1));
        lua_pop(L, 2);
        // Step 2 (killed -> release) precedes the step-3 bit-0 guard, so even a
        // direct run releases a killed record at once. Swap-compaction moves
        // the last record (selfkiller) into the freed slot.
        CHECK(!threads.resume(L, k1, err));
        ok(h, L, "assert(LOOPS == 1)");
        CHECK(threads.liveIds() == (std::vector<uint16_t>{m2, selfId}));
        // Pass 1: main2 finishes (released, selfkiller swapped into slot 0 and
        // re-examined); selfkiller kills itself and runs to its next yield.
        pass = h.runScriptThreadSchedulerPass();
        CHECK(pass.visited == 2 && pass.resumed == 2 && pass.released == 1);
        ok(h, L, "assert(AFTER_KILL == true and thread_check_done(SELF_ID) == true)");
        CHECK(threads.liveCount() == 1);
        // Pass 2: the killed record is released without being resumed.
        pass = h.runScriptThreadSchedulerPass();
        CHECK(pass.visited == 1 && pass.released == 1);
        ok(h, L, "assert(NEVER == nil)");
        CHECK(threads.liveCount() == 0);

        // Current-thread stack: 0x00e0ceb0 is null above depth 16, so the 17th
        // nested thread_new raises inside its thread.
        ok(h, L,
           "MAXN = 0\n"
           "function spawn(n) MAXN = n; thread_new('spawn', n + 1) end\n"
           "function spawn_root() spawn(1) end\n");
        CHECK(threads.startThread(L, "spawn_root", err) == BareThreadTable::kNoThread);
        ok(h, L, "assert(MAXN == 17, MAXN)");
        CHECK(threads.errors().back().find("no script thread is current") != std::string::npos);
        CHECK(threads.liveCount() == 0);

        // Capacity 256 (HIGH CONFIDENCE): the root counts as one record.
        ok(h, L,
           "function yielder() thread_yield() end\n"
           "function filler()\n"
           "  local ids = {}\n"
           "  for i = 1, 300 do\n"
           "    local id = thread_new('yielder')\n"
           "    if id == 65535 then FULL_AT = i; break end\n"
           "    assert(ids[id] == nil); ids[id] = true\n"
           "  end\n"
           "  thread_yield()\n"
           "end\n");
        uint16_t fillerId = threads.startThread(L, "filler", err);
        CHECK(fillerId != BareThreadTable::kNoThread);
        ok(h, L, "assert(FULL_AT == 256, FULL_AT)");
        CHECK(threads.liveCount() == 256);

        // A thread record of one state cannot be resumed through the other.
        lua_State* other = h.uiState();
        CHECK(threads.resume(other, fillerId, err)); // left untouched
        CHECK(err.find("another Lua state") != std::string::npos);
    }

    // ------------------------------------------------------------------
    // The scheduler 0x00e0cf50 (spec-lua-api-behaviour.md Sec26.27 thread
    // table, "[RESOLVED 2026-10-02 ...]", CONFIRMED): every live, non-exempt
    // record resumed each pass, no budget, new records visible in the same
    // walk, the exempt list keyed by owning state.
    {
        Host h({});
        lua_State* sgp = h.gameplayState();
        lua_State* sui = h.uiState();
        BareThreadTable& threads = h.bareGlobals().threads();
        std::string err;
        ok(h, sgp, kHelper);

        // A suspended thread actually resumes on the next pass: the
        // fade_out_block shape (poll, thread_yield, poll again).
        ok(h, sgp,
           "FADE_DONE = false; POLLS = 0\n"
           "function waiter() while not FADE_DONE do POLLS = POLLS + 1; thread_yield() end; WAITER_DONE = true end\n");
        uint16_t w = threads.startThread(sgp, "waiter", err);
        CHECK(w != BareThreadTable::kNoThread);
        ok(h, sgp, "assert(POLLS == 1 and WAITER_DONE == nil)");
        BareThreadTable::PassResult p = h.runScriptThreadSchedulerPass();
        CHECK(p.visited == 1 && p.resumed == 1 && p.released == 0);
        ok(h, sgp, "assert(POLLS == 2 and WAITER_DONE == nil)");
        ok(h, sgp, "FADE_DONE = true");
        p = h.runScriptThreadSchedulerPass();
        CHECK(p.resumed == 1 && p.released == 1);
        ok(h, sgp, "assert(WAITER_DONE == true and POLLS == 2)");
        CHECK(threads.liveCount() == 0);

        // No per-pass budget: 200 suspended records plus their parent, all
        // resumed in one pass.
        ok(h, sgp,
           "TICKS = 0\n"
           "function ticker() while true do TICKS = TICKS + 1; thread_yield() end end\n"
           "function many() for i = 1, 200 do thread_new('ticker') end; thread_yield() end\n");
        CHECK(threads.startThread(sgp, "many", err) != BareThreadTable::kNoThread);
        ok(h, sgp, "assert(TICKS == 200)");
        p = h.runScriptThreadSchedulerPass();
        CHECK(p.visited == 201 && p.resumed == 201 && p.released == 1);
        ok(h, sgp, "assert(TICKS == 400)");
        for (uint16_t id : threads.liveIds()) threads.kill(id);
        p = h.runScriptThreadSchedulerPass(); // killed records leave on their next run
        CHECK(p.released == 200 && threads.liveCount() == 0);

        // No pending list: a record thread_new appends mid-walk sits past the
        // walk position and is visited (bit 0 cleared, resumed) in the same pass.
        ok(h, sgp,
           "COUNT = 0\n"
           "function counter() while true do COUNT = COUNT + 1; thread_yield() end end\n"
           "function spawner() thread_yield(); SPAWNED = thread_new('counter'); thread_yield() end\n");
        uint16_t sp = threads.startThread(sgp, "spawner", err);
        CHECK(sp != BareThreadTable::kNoThread);
        p = h.runScriptThreadSchedulerPass();
        CHECK(p.visited == 2 && p.resumed == 2);
        ok(h, sgp, "assert(COUNT == 2, COUNT)"); // once inside thread_new, once by the same pass

        // Pass events: a record's own error is attributed to it; a nested
        // child's error is not.
        threads.kill(sp);
        for (uint16_t id : threads.liveIds()) threads.kill(id);
        h.runScriptThreadSchedulerPass();
        CHECK(threads.liveCount() == 0);
        ok(h, sgp,
           "function late_raiser() thread_yield(); error('late boom') end\n"
           "function child_raiser() error('child boom') end\n"
           "function parent_spawns() thread_yield(); thread_new('child_raiser'); thread_yield() end\n");
        uint16_t lr = threads.startThread(sgp, "late_raiser", err);
        uint16_t ps = threads.startThread(sgp, "parent_spawns", err);
        std::vector<BareThreadTable::PassEvent> events;
        int beforeCalls = 0;
        const size_t errorsBefore = threads.errors().size();
        h.runScriptThreadSchedulerPass([&](const BareThreadTable::Record&) { ++beforeCalls; },
                                       [&](const BareThreadTable::PassEvent& ev) { events.push_back(ev); });
        CHECK(beforeCalls == 2 && events.size() == 2);
        CHECK(threads.errors().size() == errorsBefore + 2);
        for (const auto& ev : events) {
            if (ev.id == lr) {
                CHECK(ev.resumed && ev.bodyRan && !ev.aliveAfter);
                CHECK(ev.error.find("late_raiser: ") == 0 && ev.error.find("late boom") != std::string::npos);
            } else {
                CHECK(ev.id == ps && ev.resumed && ev.bodyRan && ev.aliveAfter && ev.error.empty());
            }
        }
        threads.kill(ps);
        h.runScriptThreadSchedulerPass();
        CHECK(threads.liveCount() == 0);

        // The exempt list: empty at start-up (CONFIRMED), so a UI-state thread
        // is resumed like any other until something exempts its state (the
        // host never does so on its own - when the UI module does is OPEN).
        CHECK(threads.exemptKeyCount() == 0 && !threads.isExempt(sui) && !threads.isExempt(sgp));
        ok(h, sui, "UI_POLLS = 0; function ui_loop() while true do UI_POLLS = UI_POLLS + 1; thread_yield() end end");
        ok(h, sgp, "GP_POLLS = 0; function gp_loop() while true do GP_POLLS = GP_POLLS + 1; thread_yield() end end");
        uint16_t uiId = threads.startThread(sui, "ui_loop", err);
        uint16_t gpId = threads.startThread(sgp, "gp_loop", err);
        CHECK(uiId != BareThreadTable::kNoThread && gpId != BareThreadTable::kNoThread);
        p = h.runScriptThreadSchedulerPass();
        CHECK(p.resumed == 2 && p.exemptSkipped == 0);
        ok(h, sui, "assert(UI_POLLS == 2)");
        ok(h, sgp, "assert(GP_POLLS == 2)");
        // Exempted (refcounted: two adds): the UI thread is not resumed.
        CHECK(threads.exemptState(sui) && threads.exemptState(sui));
        CHECK(threads.isExempt(sui) && !threads.isExempt(sgp) && threads.exemptKeyCount() == 1);
        p = h.runScriptThreadSchedulerPass();
        CHECK(p.visited == 2 && p.resumed == 1 && p.exemptSkipped == 1);
        ok(h, sui, "assert(UI_POLLS == 2)");
        ok(h, sgp, "assert(GP_POLLS == 3)");
        // The pass still cleared the exempt record's bit 0: its own direct
        // driver (the 0x00e0cd00 shape) can resume it - once per pass.
        CHECK(threads.resume(sui, uiId, err));
        ok(h, sui, "assert(UI_POLLS == 3)");
        CHECK(threads.resume(sui, uiId, err));
        ok(h, sui, "assert(UI_POLLS == 3)");
        // One release leaves one reference: still exempt.
        threads.unexemptState(sui);
        CHECK(threads.isExempt(sui));
        p = h.runScriptThreadSchedulerPass();
        CHECK(p.exemptSkipped == 1);
        ok(h, sui, "assert(UI_POLLS == 3)");
        threads.unexemptState(sui);
        CHECK(!threads.isExempt(sui) && threads.exemptKeyCount() == 0);
        p = h.runScriptThreadSchedulerPass();
        CHECK(p.resumed == 2 && p.exemptSkipped == 0);
        ok(h, sui, "assert(UI_POLLS == 4)");
        // Up to 4 distinct keys; a 5th is refused (its behaviour is OPEN).
        Host h2({}), h3({});
        CHECK(threads.exemptState(sgp) && threads.exemptState(sui) && threads.exemptState(h2.gameplayState()) &&
              threads.exemptState(h2.uiState()));
        CHECK(!threads.exemptState(h3.gameplayState()) && threads.exemptKeyCount() == 4);
    }

    // ------------------------------------------------------------------
    // HitLog: bare globals fold into the same ranking, per state.
    {
        Host h({});
        ok(h, h.uiState(), "max(1, 2); max(3, 4); round(1)");
        CHECK(h.hitLog().hits().count("max") == 1 && h.hitLog().hits().at("max").callCount == 2);
        CHECK(h.hitLog().hits().count("round") == 1);
        fails(h, h.gameplayState(), "floor(0/0)", "is OPEN");
        bool loggedOpen = false;
        for (const auto& kv : h.hitLog().hits())
            if (kv.first.rfind("OPEN_STATE:", 0) == 0) loggedOpen = true;
        CHECK(loggedOpen);
    }

    if (g_failures == 0) {
        std::cout << "ALL sr3luahost BARE-GLOBAL TESTS PASSED\n";
        return 0;
    }
    std::cerr << g_failures << " CHECK(S) FAILED\n";
    return 1;
}
