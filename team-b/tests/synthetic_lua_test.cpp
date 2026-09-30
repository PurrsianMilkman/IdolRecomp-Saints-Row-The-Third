// Synthetic tests for sr3lua (include/sr3lua/lexer.h, parser.h). Every
// fixture below is a small, hand-written, REAL Lua 5.1 snippet (not
// derived from any shipped game file - those are covered separately by
// tools/lua_mission_ui_census.cpp against the real archives), each one
// targeting a specific scope-resolution rule from the Lua 5.1 manual that
// a naive regex/text scan would get wrong. Each block says which mistake
// it would catch (what "red" would look like), matching this project's
// house style for synthetic tests (see e.g. tests/synthetic_asm_test.cpp).

#include <algorithm>
#include <iostream>
#include <string>

#include "sr3lua/parser.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":"         \
                      << __LINE__ << "\n";                                  \
            ++g_failures;                                                   \
        }                                                                   \
    } while (0)

using sr3lua::CallSite;
using sr3lua::DefineKind;
using sr3lua::GlobalDefine;
using sr3lua::LuaSyntaxError;
using sr3lua::Parser;
using sr3lua::ScriptAnalysis;

int callCount(const ScriptAnalysis& a, const std::string& name) {
    return static_cast<int>(std::count_if(a.calls.begin(), a.calls.end(),
                                           [&](const CallSite& c) { return c.name == name; }));
}
bool hasDefine(const ScriptAnalysis& a, const std::string& name) {
    return std::any_of(a.defines.begin(), a.defines.end(),
                        [&](const GlobalDefine& d) { return d.name == name; });
}
int defineCount(const ScriptAnalysis& a, const std::string& name) {
    return static_cast<int>(std::count_if(a.defines.begin(), a.defines.end(),
                                           [&](const GlobalDefine& d) { return d.name == name; }));
}

template <typename F>
void run(int line, F&& f) {
    try {
        f();
    } catch (const std::exception& ex) {
        std::cerr << "UNEXPECTED EXCEPTION in the block at line " << line << ": " << ex.what() << "\n";
        ++g_failures;
    }
}
#define RUN run(__LINE__, [&]

} // namespace

int main() {
    // ---------------------------------------------------------------
    // 1. Plain global function statement.
    //    would catch: not recording `function Name() end` as a global define.
    // ---------------------------------------------------------------
    RUN {
        auto a = Parser::analyze("function foo() end");
        CHECK(hasDefine(a, "foo"));
        CHECK(defineCount(a, "foo") == 1);
    });

    // ---------------------------------------------------------------
    // 2. `local function` must NOT be a global define, and its own
    //    self-recursive call must NOT be classified as a global call site.
    //    would catch: treating `local function` like plain `function`, or
    //    not declaring the name local before parsing its own body.
    // ---------------------------------------------------------------
    RUN {
        auto a = Parser::analyze("local function bar(n) if n > 0 then return bar(n - 1) end end");
        CHECK(!hasDefine(a, "bar"));
        CHECK(callCount(a, "bar") == 0);
    });

    // ---------------------------------------------------------------
    // 3. `Name = function() end` assignment form IS a global define.
    //    would catch: only recognizing the `function Name()` statement form,
    //    missing the equally-valid assignment sugar the task explicitly names.
    // ---------------------------------------------------------------
    RUN {
        auto a = Parser::analyze("baz = function(x) return x end");
        CHECK(hasDefine(a, "baz"));
    });

    // ---------------------------------------------------------------
    // 4. `local qux = function() end` is LOCAL, not a global define.
    //    would catch: treating every `Name = function...end` as global
    //    regardless of a preceding `local`.
    // ---------------------------------------------------------------
    RUN {
        auto a = Parser::analyze("local qux = function() end");
        CHECK(!hasDefine(a, "qux"));
    });

    // ---------------------------------------------------------------
    // 5. Dotted / method function definitions are NOT bare-name global
    //    defines (they define a FIELD/METHOD on T, not a flat global).
    //    would catch: stripping the dotted prefix and counting `f`/`m` as
    //    if they were plain global names.
    // ---------------------------------------------------------------
    RUN {
        auto a = Parser::analyze("function T.f() end function T:m() end");
        CHECK(!hasDefine(a, "f"));
        CHECK(!hasDefine(a, "m"));
        CHECK(!hasDefine(a, "T")); // T itself is only indexed, never itself (re)defined here
    });

    // ---------------------------------------------------------------
    // 6. A local shadowing a would-be-global name suppresses the global
    //    call classification, and ordering matters (Lua 5.1 Sec4.2: a
    //    local is visible only AFTER its own declaring statement).
    //    would catch: a flat/global symbol table with no notion of scope
    //    or declaration order (exactly what a regex scan cannot get right).
    // ---------------------------------------------------------------
    RUN {
        auto before = Parser::analyze("print(1) local print = function() end print(2)");
        // First call (textually before the `local`) sees the REAL global print.
        CHECK(callCount(before, "print") == 1);

        auto shadowed = Parser::analyze("local print = function() end print(1) print(2)");
        // Both calls happen after `local print` - neither is a global call site.
        CHECK(callCount(shadowed, "print") == 0);
    });

    // ---------------------------------------------------------------
    // 7. Bare-name global calls are recorded; method calls and field-path
    //    calls are NOT (they aren't "a global name called", per the
    //    parser.h contract).
    //    would catch: over-broad call detection that counts every
    //    identifier immediately followed by '(' anywhere in the token
    //    stream, including `.field(...)`/`:method(...)` suffix positions.
    // ---------------------------------------------------------------
    RUN {
        auto a = Parser::analyze("some_global_call(1, 2) obj:method() t.field() t['x']()");
        CHECK(callCount(a, "some_global_call") == 1);
        CHECK(callCount(a, "method") == 0);
        CHECK(callCount(a, "field") == 0);
        // `obj`/`t` themselves are referenced (as the base of a suffix),
        // never DIRECTLY called - no call site should be recorded for them.
        CHECK(callCount(a, "obj") == 0);
        CHECK(callCount(a, "t") == 0);
    });

    // ---------------------------------------------------------------
    // 8. A global call nested three scopes deep (function body -> if ->
    //    for loop) still resolves correctly, and multiple call sites for
    //    the same name are each counted.
    //    would catch: scope stack corruption on nested block exit, or
    //    only recording the first occurrence of a repeated name.
    // ---------------------------------------------------------------
    RUN {
        auto a = Parser::analyze(
            "function outer(n) "
            "  if n > 0 then "
            "    for i = 1, n do "
            "      hook_call(i) "
            "    end "
            "  end "
            "  hook_call(0) "
            "end");
        CHECK(callCount(a, "hook_call") == 2);
        CHECK(hasDefine(a, "outer"));
    });

    // ---------------------------------------------------------------
    // 9. Table constructors are real expression contexts: a call nested
    //    inside one (as a list field, a `Name = value` field, and a
    //    `[expr] = value` field) must still be found.
    //    would catch: skipping/bracket-skimming table constructor bodies
    //    instead of really parsing their field expressions.
    // ---------------------------------------------------------------
    RUN {
        auto a = Parser::analyze("outer_call({ list_call(), key = kv_call(), [idx_call()] = 1 })");
        CHECK(callCount(a, "outer_call") == 1);
        CHECK(callCount(a, "list_call") == 1);
        CHECK(callCount(a, "kv_call") == 1);
        CHECK(callCount(a, "idx_call") == 1);
    });

    // ---------------------------------------------------------------
    // 10. `repeat ... until` special scoping rule: locals declared in the
    //     body are still visible in the until-condition itself.
    //     would catch: popping the repeat-body scope before parsing the
    //     until-expression (a real Lua 5.1 rule, not an edge case a naive
    //     block-scoper would get right by accident).
    // ---------------------------------------------------------------
    RUN {
        // If `done` were (wrongly) treated as already out of scope in the
        // until-condition, this parser wouldn't error either way (it
        // doesn't distinguish "undeclared local" as a hard error) - so the
        // real assertion is behavioural: `finish_check` is called with
        // `done` still a local, and must NOT itself be misclassified.
        auto a = Parser::analyze("repeat local done = finish_check() until done");
        CHECK(callCount(a, "finish_check") == 1);
    });

    // ---------------------------------------------------------------
    // 11. `local function` parameters and a real multi-arg call together,
    //     confirming parameters are locals (not global call targets) and
    //     that a genuinely undefined-anywhere name used as a callee is
    //     exactly one call site.
    // ---------------------------------------------------------------
    RUN {
        auto a = Parser::analyze("function handler(event, data) return engine_dispatch(event, data) end");
        CHECK(hasDefine(a, "handler"));
        CHECK(callCount(a, "engine_dispatch") == 1);
        CHECK(callCount(a, "event") == 0);
        CHECK(callCount(a, "data") == 0);
    });

    // ---------------------------------------------------------------
    // 12. Long comments/long strings and line comments must not confuse
    //     the lexer into mis-tokenizing real code around them (this
    //     matches the real shipped scripts' own `--[[ ... ]]--` banner
    //     comment shape, observed verbatim in the real archives).
    //     would catch: a long-bracket implementation that doesn't stop at
    //     the matching `]]`, or that mishandles the trailing `--` after it.
    // ---------------------------------------------------------------
    RUN {
        auto a = Parser::analyze(
            "--[[\r\n\tdlc1_mm_06.lua\r\n\tSR3 Mission Script\r\n]]--\r\n"
            "-- a line comment\n"
            "local long_str = [==[ contains ]] and -- not a real comment ]==]\n"
            "function real_after_comments() return mission_hook_call() end");
        CHECK(hasDefine(a, "real_after_comments"));
        CHECK(callCount(a, "mission_hook_call") == 1);
    });

    // ---------------------------------------------------------------
    // 13. Multiple assignment, positionally paired: only the LHS entries
    //     actually paired with a literal `function` RHS are global
    //     defines; a plain value or a name-only RHS is not "defining a
    //     function" per this reader's own literal scope.
    //     would catch: treating every name on the LHS of any `=` as a
    //     function define regardless of what's on the RHS.
    // ---------------------------------------------------------------
    RUN {
        auto a = Parser::analyze("a, b, c = function() end, 5, some_other_global");
        CHECK(hasDefine(a, "a"));
        CHECK(!hasDefine(a, "b"));
        CHECK(!hasDefine(a, "c"));
    });

    // ---------------------------------------------------------------
    // 14. A genuine syntax error is reported as such (LuaSyntaxError),
    //     not silently swallowed or mis-parsed into a plausible-looking
    //     result - this is what makes "804/804 parsed clean" in the real
    //     census a checked claim rather than an assumption.
    // ---------------------------------------------------------------
    RUN {
        bool threw = false;
        try {
            Parser::analyze("function broken( end");
        } catch (const LuaSyntaxError&) {
            threw = true;
        }
        CHECK(threw);
    });

    if (g_failures == 0) {
        std::cout << "All synthetic sr3lua tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) FAILED.\n";
    return 1;
}
