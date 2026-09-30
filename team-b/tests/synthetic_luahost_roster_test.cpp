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
//    rand_int, rand_float, round, debug_print are called by scripts). Their
//    exact roster is pending a Team A job, so they are deliberately NOT
//    registered; this test pins that they stay nil until specced.
#include <iostream>
#include <string>

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
    CHECK(names.size() == 1490);
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

    for (const char* n : {"rand_int", "rand_float", "round", "debug_print", "max", "floor", "abs"}) {
        CHECK(typeOf(gp, n) == "nil");
        CHECK(typeOf(ui, n) == "nil");
    }

    if (g_failures == 0) {
        std::cout << "ALL sr3luahost ROSTER TESTS PASSED\n";
        return 0;
    }
    std::cerr << g_failures << " CHECK(S) FAILED\n";
    return 1;
}
