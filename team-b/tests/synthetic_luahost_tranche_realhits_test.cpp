// Synthetic tests for the 8 real-hit names found across ranking tranches
// 15/17/20/22/23 (spec-lua-api-behaviour.md Sec52/Sec51/Sec53/Sec55/Sec57),
// the orchestrator-directed real-hit-only pass over the entire Sec50-Sec59
// backlog (2026-10-03): a fresh mission drive against the real cache found
// exactly 8 names with a non-zero call count out of the ~229 names across
// those 10 sections; every other name stays a generic logging stub this
// pass. See src/lua_spec_confirmed_stubs.cpp's own batch header comment
// (right before stub_tutorial_lock) for the full citation. Same convention
// as tests/synthetic_luahost_sec44_46_test.cpp: a small, hand-written
// fixture with its own isolated Host, registered names grepped directly
// from tools/lua_all_registered_1490_tagged.txt (all 8 are "gameplay").
#include <iostream>
#include <string>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"
#include "sr3luahost/stub_registry.h"

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

void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "tranche_realhits_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

bool hasGlobal(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    bool f = lua_isfunction(L, -1);
    lua_pop(L, 1);
    return f;
}

std::vector<RegisteredName> batchFixtureNames() {
    return {
        {"tutorial_lock", "gameplay"},
        {"radio_newsbreak_clear", "gameplay"},
        {"group_create_do", "gameplay"},
        {"group_create_hidden_do", "gameplay"},
        {"dlc2_m02_clapboards_get", "gameplay"},
        {"dlc2_m02_clapboards_reset", "gameplay"},
        {"cutscene_play_do", "gameplay"},
        {"cutscene_play_check_done", "gameplay"},
    };
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // Registration: every name lands in gameplay only, never ui.
    {
        Host host(batchFixtureNames());
        for (const auto& rn : batchFixtureNames()) {
            CHECK(hasGlobal(host.gameplayState(), rn.name.c_str()));
            CHECK(!hasGlobal(host.uiState(), rn.name.c_str()));
        }
    }

    Host host(batchFixtureNames());
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();

    // ------------------------------------------------------------------
    // tutorial_lock (Sec52.3): only indices <= 188 take effect; 189-209 and
    // an unresolved name are a silent no-op (CONFIRMED bound; the exact
    // resulting state is OPEN, so this project counts a "would lock" hit
    // instead of fabricating a tutorialState() code - see engine_state.h).
    CHECK(es.tutorialLockCount(1) == 0);
    ok(host, gp, "tutorial_lock('autosave')"); // index 1, in range
    CHECK(es.tutorialLockCount(1) == 1);
    ok(host, gp, "tutorial_lock('autosave')"); // repeatable
    CHECK(es.tutorialLockCount(1) == 2);
    ok(host, gp, "tutorial_lock('TUT_SIXAXIS_BOAT')"); // index 185, in range, case-insensitive
    CHECK(es.tutorialLockCount(185) == 1);
    ok(host, gp, "tutorial_lock('dlc1_act_genki_escort_into')"); // index 189, the broken range
    CHECK(es.tutorialLockCount(189) == 0);
    ok(host, gp, "tutorial_lock('dlc3_win')"); // index 209, the broken range
    CHECK(es.tutorialLockCount(209) == 0);
    ok(host, gp, "tutorial_lock('hint_grab_weapon')"); // not in the table at all (index -1)
    CHECK(es.tutorialLockCount(-1) == 0);

    // ------------------------------------------------------------------
    // radio_newsbreak_clear (Sec51.3): a plain write-only local flag.
    es.radioNewsbreakActive().set(true);
    ok(host, gp, "radio_newsbreak_clear()");
    CHECK(es.radioNewsbreakActive().known() && es.radioNewsbreakActive().get() == false);

    // ------------------------------------------------------------------
    // group_create_do / group_create_hidden_do (Sec53.3): the SAME
    // underlying creation routine; creation happens exactly once - calling
    // either a second time (even the OTHER one) on an already-created name
    // is a no-op (this project tracks only "already created," not which
    // literal/variant was used first, since nothing in scope reads that
    // back).
    CHECK(!es.groupAlreadyCreated("grp1"));
    ok(host, gp, "group_create_do('grp1')");
    CHECK(es.groupAlreadyCreated("grp1"));
    ok(host, gp, "group_create_do('grp1')");       // idempotent, no crash/effect
    ok(host, gp, "group_create_hidden_do('grp1')"); // the OTHER variant on an already-created group: still a no-op
    CHECK(!es.groupAlreadyCreated("grp2"));
    ok(host, gp, "group_create_hidden_do('grp2')");
    CHECK(es.groupAlreadyCreated("grp2"));
    CHECK(!es.groupAlreadyCreated("grp3")); // never created: stays false

    // ------------------------------------------------------------------
    // dlc2_m02_clapboards_get / _reset (Sec55.5): file-default count -1
    // (every index fails the upper bound before the first _reset); _reset
    // clamps to at most 10 (no lower clamp) and zeroes that many flag
    // bytes; _get is 1-based -> 0-based, upper-bound checked (CONFIRMED
    // "no value at all" when out of range), with a HOST-SAFETY refusal (not
    // a reproduced arbitrary read) for any argument <= 0.
    CHECK(es.dlc2ClapboardCount() == -1);
    // Before any _reset call: every index fails the upper bound (count is
    // -1), so _get returns nil (no Lua value at all, not even false) for
    // any index.
    ok(host, gp, "assert(dlc2_m02_clapboards_get(1) == nil)");
    ok(host, gp, "dlc2_m02_clapboards_reset(3)");
    CHECK(es.dlc2ClapboardCount() == 3);
    ok(host, gp, "assert(dlc2_m02_clapboards_get(1) == false)"); // zeroed by reset
    ok(host, gp, "assert(dlc2_m02_clapboards_get(3) == false)"); // index 3 (0-based 2): in range
    ok(host, gp, "assert(dlc2_m02_clapboards_get(4) == nil)");   // index 3 (0-based): out of the count=3 upper bound
    CHECK(es.dlc2ClapboardsGetNegativeIndexGuardCount() == 0);
    ok(host, gp, "assert(dlc2_m02_clapboards_get(0) == nil)"); // HOST-SAFETY: refused, not an arbitrary read
    CHECK(es.dlc2ClapboardsGetNegativeIndexGuardCount() == 1);
    ok(host, gp, "assert(dlc2_m02_clapboards_get(-5) == nil)");
    CHECK(es.dlc2ClapboardsGetNegativeIndexGuardCount() == 2);
    ok(host, gp, "dlc2_m02_clapboards_reset(99)"); // clamped to at most 10
    CHECK(es.dlc2ClapboardCount() == 10);
    ok(host, gp, "dlc2_m02_clapboards_reset(-7)"); // no lower clamp: stored as-is, harmless (zero-fill loop doesn't run)
    CHECK(es.dlc2ClapboardCount() == -7);
    ok(host, gp, "assert(dlc2_m02_clapboards_get(1) == nil)"); // count now -7: every index fails the upper bound again

    // ------------------------------------------------------------------
    // cutscene_play_do / cutscene_play_check_done (Sec57.2): reuses the
    // existing zscene prep gate + screen-fade-request mechanism; a
    // destination table needs EXACTLY two entries or both are dropped,
    // written unconditionally (even on a refused start); a second start
    // while one is already "in progress" is refused, matching the real
    // engine's own "already running" gate, yet still overwrites the
    // destinations. check_done honestly, permanently reports "done" in
    // this host (no in-scope native ever allocates a cutscene manager),
    // matching Sec57.2's own documented "reports done prematurely" bug.
    // zsceneLoadable() is this host's own OPEN-until-set per-name "is this
    // a kind-1 (zscene) entry" map (no installed table in this isolated
    // fixture) - same setup convention tests/synthetic_luahost_test.cpp's
    // own zscene_prep coverage already uses.
    es.zsceneLoadable().set("p_z01", true);
    es.zsceneLoadable().set("p_z02", true);
    CHECK(!es.cutscenePlayInProgress());
    CHECK(es.cutscenePlayDestination1().empty() && es.cutscenePlayDestination2().empty());
    ok(host, gp, "cutscene_play_do('p_z01', nil, {'dest_a', 'dest_b'})");
    CHECK(es.cutscenePlayDestination1() == "dest_a" && es.cutscenePlayDestination2() == "dest_b");
    CHECK(es.cutscenePlayInProgress());
    ok(host, gp, "assert(cutscene_play_check_done() == true)"); // premature "done", CONFIRMED bug reproduced honestly

    // A second start is refused (already in progress) but STILL overwrites
    // the destinations unconditionally (CONFIRMED quirk) - a table with the
    // wrong entry count silently drops both.
    ok(host, gp, "cutscene_play_do('p_z02', true, {'only_one'})");
    CHECK(es.cutscenePlayDestination1().empty() && es.cutscenePlayDestination2().empty());

    // A fresh host (nothing started yet): destinations default empty, and
    // a destination table of the wrong length (3 entries) drops both too.
    {
        Host host2(batchFixtureNames());
        lua_State* gp2 = host2.gameplayState();
        EngineState& es2 = host2.engineState();
        es2.zsceneLoadable().set("p_z03", true);
        ok(host2, gp2, "cutscene_play_do('p_z03', nil, {'a', 'b', 'c'})");
        CHECK(es2.cutscenePlayDestination1().empty() && es2.cutscenePlayDestination2().empty());
        CHECK(es2.cutscenePlayInProgress()); // the start itself still succeeds; only the destinations are dropped
    }

    if (g_failures == 0)
        std::cout << "ranking tranches 15/17/20/22/23 real-hit batch (2026-10-03, 8 functions): "
                     "all checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
