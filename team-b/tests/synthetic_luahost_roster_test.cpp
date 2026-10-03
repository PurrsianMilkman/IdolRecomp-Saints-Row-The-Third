// Registration-roster regression test for sr3luahost (cloud phase,
// 2026-09-30). Unlike synthetic_luahost_test.cpp's small fixture list, this
// builds the Host from the REAL tools/lua_all_registered_1490_tagged.txt
// (a repo file listing engine function NAMES, no game data) and pins the
// state placement spec-lua-bindings.md states:
//  * §13.7: the 55-name vint_* registrar registers into the UI state only
//    (vint_is_std_res, the dataresponder pair, set/get_property, ...).
//  * §13.5: 8 names are registered into BOTH states although the tag file
//    records one cluster per name (gameplay) - the host adds the UI side.
//  * §13.2/§16.3: the 24 bare globals of the 0x00e0f900 registrar are not
//    in the 1,490 list and not stock Lua 5.1 either (bare max/floor/abs,
//    rand_int, rand_float, round, debug_print are called by scripts). The
//    roster is CONFIRMED since 2026-10-01 (Sec13.2/Sec16.4): all 24 are
//    registered into BOTH states, and only base + coroutine are opened.
#include <iostream>
#include <string>

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

std::string typeOf(lua_State* L, const std::string& name) {
    std::string code = "return type(" + name + ")";
    luaL_loadstring(L, code.c_str());
    lua_pcall(L, 0, 1, 0);
    std::string t = lua_tostring(L, -1) ? lua_tostring(L, -1) : "?";
    lua_settop(L, 0);
    return t;
}
} // namespace

int main() {
    auto names = loadTaggedRegistrationList(CRREISH_TOOLS_DIR "/lua_all_registered_1490_tagged.txt");
    // 1491 (2026-10-02, Sec47): the gameplay registrar's own row count
    // discrepancy resolved - on_mission_item_drop is registered twice under
    // an identical name/handler, so it was simply missing from this file
    // until now. Filename kept as-is (historical, not re-derived from the
    // current count).
    CHECK(names.size() == 1491);
    Host host(names);
    lua_State* gp = host.gameplayState();
    lua_State* ui = host.uiState();

    int vint = 0;
    for (const auto& n : names) {
        if (n.name.rfind("vint_", 0) != 0) continue;
        ++vint;
        CHECK(n.cluster == "ui");
        CHECK(typeOf(ui, n.name) == "function");
        CHECK(typeOf(gp, n.name) == "nil");
    }
    CHECK(vint == 58); // §13.7's 55 plus 3 vint_* names from the other UI registrars, all tagged ui
    for (const char* n : {"vint_is_std_res", "vint_internal_dataresponder_request", "vint_dataresponder_finished",
                          "vint_set_property", "vint_get_property", "vint_get_time_index", "vint_get_safe_frame",
                          "vint_dataitem_get", "vint_object_first_child", "vint_object_clone"})
        CHECK(typeOf(ui, n) == "function");

    for (const char* n : {"audio_object_post_event", "game_is_active_input_gamepad", "hud_display_set_element",
                          "audio_stop", "hud_display_create_state", "hud_display_commit_state",
                          "hud_display_remove_state", "coop_is_active"}) {
        CHECK(typeOf(gp, n) == "function");
        CHECK(typeOf(ui, n) == "function");
    }

    // Stock libraries (spec-lua-bindings.md Sec16.1 creator step 3 / Sec16.4,
    // spec-lua-api-behaviour.md Sec26.27): the base library and `coroutine`
    // only, in both states.
    for (lua_State* L : {gp, ui}) {
        for (const char* lib : {"math", "string", "table", "io", "os", "debug", "package"})
            CHECK(typeOf(L, lib) == "nil");
        CHECK(typeOf(L, "require") == "nil" && typeOf(L, "module") == "nil");
        CHECK(typeOf(L, "coroutine") == "table");
        CHECK(typeOf(L, "coroutine.resume") == "function" && typeOf(L, "coroutine.yield") == "function");
        for (const char* base : {"pcall", "pairs", "ipairs", "setmetatable", "getfenv", "setfenv", "tostring",
                                 "rawget", "newproxy", "_G"})
            CHECK(typeOf(L, base) != "nil");
        CHECK(typeOf(L, "_VERSION") == "string");
    }

    // The 24 bare globals of 0x00e0f900 (Sec13.2/Sec16.4, CONFIRMED
    // 2026-10-01): functions in BOTH states, none in the tagged list, and the
    // spec bodies rather than generic stubs (a generic stub returns nothing).
    CHECK(bareGlobalRoster().size() == 24);
    for (const auto& e : bareGlobalRoster()) {
        CHECK(typeOf(gp, e.name) == "function");
        CHECK(typeOf(ui, e.name) == "function");
        for (const auto& n : names) CHECK(n.name != e.name);
    }
    for (lua_State* L : {gp, ui}) {
        CHECK(typeOf(L, "max(1, 2, 3)") == "number");
        CHECK(typeOf(L, "strstr('abc', 'b')") == "boolean");
        CHECK(typeOf(L, "rand_int(1, 1)") == "number");
    }
    CHECK(host.bareGlobalCount() == 48);

    if (g_failures == 0) {
        std::cout << "ALL sr3luahost ROSTER TESTS PASSED\n";
        return 0;
    }
    std::cerr << g_failures << " CHECK(S) FAILED\n";
    return 1;
}
