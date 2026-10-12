// Synthetic coverage for the hud-prompt-autoanswer-harness task (2026-10-12):
// a generic, delay-gated "auto-answer" mechanism for a live hud_prompt(target,
// promptId, callbackName) request (spec-lua-api-behaviour.md Sec13.3) that a
// scripted/automated run has no real player input to resolve.
//
// This project's real-mission investigation (branch feature/m05-jump-hud-
// prompt-test, merged commit 917ce08; Team A spec sync dc5b4ce) confirmed
// m05.lua:1141 genuinely stalls waiting for a live hud_prompt("M05_JUMP", ...)
// request's own registered callback (m05_jump_local/m05_jump_remote) - a real
// player-input-gated wait, not an engine bug. A --no-input-style scripted run
// (tools/sr3_viewer.cpp's own --no-input, tools/lua_host_run.cpp's own
// mission-drive tick loop - neither has any real keyboard/button concept)
// never synthesizes that press, so such a mission tick-budget-stalls forever
// regardless of budget, masking whatever its own NEXT real stall point is.
//
// THIS TASK adds a generic (not M05_JUMP-specific) delay-gated auto-answer to
// both tools/sr3_viewer.cpp's live --mission loop (--auto-answer-hud-prompts
// [N], extending the existing --script-hud-prompt-response's immediate-answer
// mechanism with a configurable N-tick delay) and tools/lua_host_run.cpp's
// own per-mission tick loop (--auto-answer-hud-prompts[=N], fired LIVE during
// the normal tick loop, unlike the post-hoc --probe-autopilot diagnostic).
// Both reuse the exact same mechanism this project already proved correct for
// the on_trigger/on_death/on_vehicle_enter callback-registration family
// (spec-lua-api-behaviour.md Sec1.14's "GAP CLOSED" finding, commit 383f653):
// spawn a NEW coroutine for the callback name via BareThreadTable::allocate()
// - the body only runs on a LATER scheduler pass, never synchronously.
//
// Both production sites embed this delay-gated dispatch inline in their own
// main()'s per-tick loop (not a shared/reusable function - same "copied, not
// shared" convention this project's own real_*_test files already use for
// their own per-test runIn() helpers), so this test exercises the exact same
// REAL EngineState/BareThreadTable primitives (recordHudPrompt/
// hudPromptRequests/threads().allocate()/runScriptThreadSchedulerPass) with
// the SAME delay-math both production sites use, the same way tests/
// synthetic_trigger_volume_overlap_test.cpp already does for the sibling
// trigger-volume dispatch mechanism (driving the real mechanism directly,
// rather than spawning the CLI tool, which needs a real/synthetic mission
// script+archive neither production site's test harness convention makes
// convenient for a single generic flag check).
#include <iostream>
#include <string>
#include <vector>

#include "sr3luahost/bare_globals.h"
#include "sr3luahost/engine_state.h"
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

} // namespace

int main() {
    // --- (A) no auto-answer at all: a live hud_prompt request never
    // resolves on its own, no matter how many ticks pass - there is no real
    // player input in this host, and nothing else here ever calls the
    // registered callback. This is the "doesn't resolve on its own" half of
    // this task's required test. ---
    {
        Host host({});
        EngineState& es = host.engineState();
        lua_State* L = host.gameplayState();

        auto r = host.runChunk(L, "M05_JUMPED = false\nfunction m05_jump_local() M05_JUMPED = true end\n",
                                "hud_prompt_test.lua");
        CHECK(r.loadOk && r.pcallOk);

        // The real hud_prompt native's own success path (Sec13.3) - recorded
        // here directly, the same way stub_hud_prompt itself does, standing
        // in for `hud_prompt("M05_JUMP", "...", "m05_jump_local")`.
        es.recordHudPrompt("M05_JUMP", "jump_prompt_id", "m05_jump_local");
        CHECK(es.hudPromptRequests().size() == 1);

        // 50 scheduler passes, no auto-answer logic driving them at all -
        // nothing ever spawns a coroutine for m05_jump_local, so it never
        // runs, no matter how long this host keeps ticking.
        for (int i = 0; i < 50; ++i) {
            host.runScriptThreadSchedulerPass();
        }
        auto ok = host.runChunk(L, "assert(M05_JUMPED == false)", "hud_prompt_test.lua");
        CHECK(ok.loadOk && ok.pcallOk);
        CHECK(host.bareGlobals().threads().liveCount() == 0); // never spawned
    }

    // --- (B)/(C) the new auto-answer mechanism, delay-gated - the EXACT
    // same per-tick shape both tools/sr3_viewer.cpp's --auto-answer-hud-
    // prompts and tools/lua_host_run.cpp's --auto-answer-hud-prompts[=N]
    // use: track each request's own first-seen tick, and once (tick -
    // firstSeen) >= delay, spawn (not call) its callback; the spawn's body
    // then runs on the NEXT scheduler pass, same decoupled-in-time shape
    // Sec1.14 confirms for the real dispatcher. ---
    {
        Host host({});
        EngineState& es = host.engineState();
        lua_State* L = host.gameplayState();
        BareThreadTable& threads = host.bareGlobals().threads();

        auto r = host.runChunk(L, "M05_JUMPED = false\nfunction m05_jump_local() M05_JUMPED = true end\n",
                                "hud_prompt_test.lua");
        CHECK(r.loadOk && r.pcallOk);

        constexpr int kDelayTicks = 15; // this task's own CHOSEN default
        size_t respondedCount = 0;
        std::vector<int> firstSeenTick;

        // Mirrors tools/lua_host_run.cpp's/tools/sr3_viewer.cpp's own
        // "AUTO-ANSWER-HUD-PROMPTS"/"SCRIPT-HUD-PROMPT-RESPONSE" block,
        // copied here rather than shared (same convention those two
        // production sites already use with each other - each embeds its
        // own copy, not a shared helper).
        auto autoAnswerHudPromptsTick = [&](int tick) {
            auto& reqs = es.hudPromptRequests();
            while (firstSeenTick.size() < reqs.size()) firstSeenTick.push_back(tick);
            while (respondedCount < reqs.size()) {
                const auto& req = reqs[respondedCount];
                const int seen = firstSeenTick[respondedCount];
                if (tick - seen < kDelayTicks) break; // not due yet
                ++respondedCount;
                if (req.arg3.empty()) continue;
                threads.allocate(L, req.arg3, /*nargs=*/0, /*hasParent=*/false);
            }
        };

        // Prompt first appears at tick 1 (matches a mission script that
        // calls hud_prompt on its very first resume, e.g. m05.lua:1141's own
        // shape - the request exists before any auto-answer tick runs).
        es.recordHudPrompt("M05_JUMP", "jump_prompt_id", "m05_jump_local");

        // Ticks 1..14 (14 < kDelayTicks): the request has been showing for
        // fewer than kDelayTicks ticks - must NOT fire yet. This is the
        // "doesn't resolve before the configured delay" half.
        for (int tick = 1; tick <= kDelayTicks - 1; ++tick) {
            autoAnswerHudPromptsTick(tick);
            host.runScriptThreadSchedulerPass();
        }
        CHECK(threads.liveCount() == 0); // never spawned yet
        auto notYet = host.runChunk(L, "assert(M05_JUMPED == false)", "hud_prompt_test.lua");
        CHECK(notYet.loadOk && notYet.pcallOk);

        // Tick kDelayTicks (== 15): (tick - firstSeen) == 14 still < 15 -
        // the boundary is exclusive (strictly >=), so this is the LAST tick
        // that must still not fire, pinning down the exact off-by-one this
        // task's own "after N frames of it showing" wording implies (first
        // seen counts as tick 1, so the Nth tick after that is tick N+1).
        autoAnswerHudPromptsTick(kDelayTicks);
        host.runScriptThreadSchedulerPass();
        CHECK(threads.liveCount() == 0);

        // Tick kDelayTicks+1 (== 16): (tick - firstSeen) == 15 >= 15 - now
        // due. Spawn-only this same tick (liveCount reflects the spawn, the
        // callback body has NOT run synchronously).
        autoAnswerHudPromptsTick(kDelayTicks + 1);
        CHECK(threads.liveCount() == 1);
        auto stillNot = host.runChunk(L, "assert(M05_JUMPED == false)", "hud_prompt_test.lua");
        CHECK(stillNot.loadOk && stillNot.pcallOk);

        // The NEXT scheduler pass actually resumes the spawned coroutine -
        // this is the "resolves after the configured delay and its callback
        // fires" half.
        BareThreadTable::PassResult pass = host.runScriptThreadSchedulerPass();
        CHECK(pass.resumed == 1 && pass.released == 1);
        auto fired = host.runChunk(L, "assert(M05_JUMPED == true)", "hud_prompt_test.lua");
        CHECK(fired.loadOk && fired.pcallOk);

        // A later, unrelated prompt request (e.g. a second mission's own
        // HUD prompt) seen on this same tick is independently delay-gated
        // from its OWN first-seen tick, not the first request's - proves
        // the mechanism is generic over every live prompt, not a one-shot
        // special case for a single global request.
        auto r2 = host.runChunk(L, "SECOND_FIRED = false\nfunction second_cb() SECOND_FIRED = true end\n",
                                 "hud_prompt_test.lua");
        CHECK(r2.loadOk && r2.pcallOk);
        es.recordHudPrompt("OTHER_PROMPT", "other_id", "second_cb");
        autoAnswerHudPromptsTick(kDelayTicks + 2); // firstSeen for request #2 is THIS tick
        CHECK(threads.liveCount() == 0); // just spawned-and-resumed #1 released; #2 not due for 15 more ticks
        for (int tick = kDelayTicks + 3; tick <= kDelayTicks + 2 + kDelayTicks; ++tick) {
            autoAnswerHudPromptsTick(tick);
            host.runScriptThreadSchedulerPass();
        }
        auto secondFired = host.runChunk(L, "assert(SECOND_FIRED == true)", "hud_prompt_test.lua");
        CHECK(secondFired.loadOk && secondFired.pcallOk);
    }

    // --- (D) CHOSEN stand-in direct-call behavior (hud-prompt-direct-
    // callback-standin task, 2026-10-12): production (tools/lua_host_run.cpp,
    // tools/sr3_viewer.cpp) now calls Host::callGlobalFunctionByName()
    // SYNCHRONOUSLY on the named callback the moment auto-answer fires,
    // instead of spawning a coroutine via BareThreadTable::allocate() (block
    // B/C above still exercises that older mechanism directly, since the
    // underlying primitive is still real and used elsewhere - just no longer
    // for THIS dispatch). This is the delay-gated mirror of blocks B/C, but
    // with the direct-call dispatch, proving (1) the callback's own side
    // effect is observable immediately (no extra scheduler pass needed,
    // unlike the spawn mechanism) and (2) no coroutine is ever created for
    // this path (threads.liveCount() stays 0 throughout). ---
    {
        Host host({});
        EngineState& es = host.engineState();
        lua_State* L = host.gameplayState();
        BareThreadTable& threads = host.bareGlobals().threads();

        auto r = host.runChunk(L, "M05_JUMPED = false\nfunction m05_jump_local() M05_JUMPED = true end\n",
                                "hud_prompt_test.lua");
        CHECK(r.loadOk && r.pcallOk);

        constexpr int kDelayTicks = 15;
        size_t respondedCount = 0;
        std::vector<int> firstSeenTick;

        // Mirrors tools/lua_host_run.cpp's/tools/sr3_viewer.cpp's CURRENT
        // (direct-call) auto-answer block.
        auto autoAnswerHudPromptsTick = [&](int tick) {
            auto& reqs = es.hudPromptRequests();
            while (firstSeenTick.size() < reqs.size()) firstSeenTick.push_back(tick);
            while (respondedCount < reqs.size()) {
                const auto& req = reqs[respondedCount];
                const int seen = firstSeenTick[respondedCount];
                if (tick - seen < kDelayTicks) break; // not due yet
                ++respondedCount;
                if (req.arg3.empty()) continue;
                std::string callErr;
                host.callGlobalFunctionByName(L, req.arg3, callErr);
            }
        };

        es.recordHudPrompt("M05_JUMP", "jump_prompt_id", "m05_jump_local");

        for (int tick = 1; tick <= kDelayTicks - 1; ++tick) {
            autoAnswerHudPromptsTick(tick);
        }
        CHECK(threads.liveCount() == 0); // never spawns a coroutine - direct call, not spawn
        auto notYet = host.runChunk(L, "assert(M05_JUMPED == false)", "hud_prompt_test.lua");
        CHECK(notYet.loadOk && notYet.pcallOk);

        autoAnswerHudPromptsTick(kDelayTicks); // tick-firstSeen == 14, still not due (same off-by-one as block B/C)
        CHECK(threads.liveCount() == 0);
        auto stillNot = host.runChunk(L, "assert(M05_JUMPED == false)", "hud_prompt_test.lua");
        CHECK(stillNot.loadOk && stillNot.pcallOk);

        // Tick kDelayTicks+1 (== 16): due - the callback runs SYNCHRONOUSLY
        // within this very call, no scheduler pass needed, and no coroutine
        // is ever created.
        autoAnswerHudPromptsTick(kDelayTicks + 1);
        CHECK(threads.liveCount() == 0); // still never spawned anything
        auto fired = host.runChunk(L, "assert(M05_JUMPED == true)", "hud_prompt_test.lua");
        CHECK(fired.loadOk && fired.pcallOk);

        // A request whose arg3 names no real global function at all: the
        // direct call is a no-op (silent existence-check miss, same
        // convention as fireConfirmedHooks()'s own Group 1/3 behavior) -
        // never crashes, never raises a Lua error, just doesn't fire.
        es.recordHudPrompt("GHOST_PROMPT", "ghost_id", "no_such_global_function_xyz");
        for (int tick = kDelayTicks + 2; tick <= kDelayTicks * 2 + 2; ++tick) {
            autoAnswerHudPromptsTick(tick);
        }
        CHECK(threads.liveCount() == 0);
        std::string ghostErr;
        bool ghostCalled = host.callGlobalFunctionByName(L, "no_such_global_function_xyz", ghostErr);
        CHECK(!ghostCalled && ghostErr.empty());
    }

    // --- (Z) Sec13.3 (e8dc04d): interact press consumes the active prompt,
    // returns the arg-3 callback name (called with no args by the caller) and
    // clears the prompt; no active prompt -> press not consumed.
    {
        sr3luahost::EngineState es;
        CHECK(!es.hudPromptActive());
        CHECK(es.takeHudPromptCallbackOnInteract().empty());
        es.recordHudPrompt("#PLAYER#", "M05_JUMP", "m05_jump_local");
        CHECK(es.hudPromptActive());
        CHECK(es.takeHudPromptCallbackOnInteract() == "m05_jump_local");
        CHECK(!es.hudPromptActive());
        CHECK(es.takeHudPromptCallbackOnInteract().empty()); // cleared: fires once only
        es.recordHudPrompt("#PLAYER#", "M05_JUMP", "m05_jump_local");
        es.recordHudPromptDefaultReset(); // 0x00a36e60(0) clears slots
        CHECK(!es.hudPromptActive());
        CHECK(es.takeHudPromptCallbackOnInteract().empty());
    }

    if (g_failures == 0) {
        std::cout << "ALL hud-prompt-autoanswer SYNTHETIC TESTS PASSED\n";
    } else {
        std::cerr << g_failures << " CHECK(S) FAILED\n";
    }
    return g_failures == 0 ? 0 : 1;
}
