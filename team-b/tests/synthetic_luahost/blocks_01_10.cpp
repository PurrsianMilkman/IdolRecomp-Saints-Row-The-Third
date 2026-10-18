// Synthetic sr3luahost tests: test_block_1 .. test_block_10 (split out of synthetic_luahost_test.cpp, bodies verbatim).
#include "synthetic_luahost/common.h"

    // --- loadTaggedRegistrationList: the real tab-separated format -------
void test_block_1() {
        std::string path = "synthetic_luahost_test_taglist.tmp.txt";
        {
            std::ofstream f(path);
            f << "waypoint_add\tgameplay\n";
            f << "garage_remove_vehicle\tui\n";
            f << "\n";                 // blank line - must be skipped
            f << "malformed_no_tab\n"; // no tab - must be skipped, not throw
            f << "trailing_cr\tgameplay\r\n";
        }
        auto names = loadTaggedRegistrationList(path);
        std::remove(path.c_str());
        CHECK(names.size() == 3);
        CHECK(names[0].name == "waypoint_add" && names[0].cluster == "gameplay");
        CHECK(names[1].name == "garage_remove_vehicle" && names[1].cluster == "ui");
        CHECK(names[2].name == "trailing_cr" && names[2].cluster == "gameplay"); // CRLF stripped
    }

    // --- Host: gameplay/UI split, and the stub's own logging contract ----
    // NOTE (2026-10-08, Batch 3/results/lua_natives_sec10_28_fanout_scoping_
    // 20261008.md): this block's own generic-gameplay-stub example used to
    // be the real name waypoint_add, until that name itself became a real
    // spec-confirmed native this same day (Sec12.24) - Host's own
    // constructor now filters it OUT of the generic tagged-list split (see
    // its own top comment) and registers the REAL implementation instead,
    // which no longer returns nil for a 3-arg call. Swapped to a synthetic
    // placeholder name that can never collide with a real registered
    // native, so this block tests the GENERIC stub mechanism itself, not
    // any one real native's own (necessarily changing) behavior.
void test_block_2() {
        std::vector<RegisteredName> names = {
            {"zzz_generic_stub_example", "gameplay"},
            {"garage_remove_vehicle", "ui"},
        };
        Host host(names);
        CHECK(host.gameplayStubCount() == 1);
        // 1 (garage_remove_vehicle) + 8 real, unconditionally dual-
        // registered names (spec-lua-bindings.md Sec13.5's own "8
        // overlaps": audio_object_post_event, game_is_active_input_gamepad,
        // hud_display_set_element, audio_stop, hud_display_create_state,
        // hud_display_commit_state, hud_display_remove_state - all 7 via
        // uiNames - plus coop_is_active via specUiNames) - lua_host.cpp's
        // own Host constructor registers these into BOTH states
        // unconditionally, independent of whether this minimal fixture
        // even mentions them, because the real engine itself does (this is
        // a real, disassembly-confirmed fact about the binary, not a
        // property of any particular input list) = 9.
        CHECK(host.uiStubCount() == 9);

        // A gameplay-tagged name is a real Lua function in the gameplay
        // state, and simply absent (nil) in the UI state - proving the
        // split is real, not just a shared roster registered twice.
        {
            auto r = host.runChunk(host.gameplayState(), "return type(zzz_generic_stub_example)", "chunk1");
            CHECK(r.loadOk && r.pcallOk);
        }
        {
            auto r = host.runChunk(host.uiState(), "assert(zzz_generic_stub_example == nil)", "chunk2");
            CHECK(r.loadOk && r.pcallOk);
        }
        {
            auto r = host.runChunk(host.gameplayState(), "assert(garage_remove_vehicle == nil)", "chunk3");
            CHECK(r.loadOk && r.pcallOk);
        }

        // Calling the stub: real argc, real arg types (via lua_type), and
        // a real nil return - the task's exact specified stub contract,
        // checked from the Lua side of the real C API round-trip.
        {
            auto r = host.runChunk(host.gameplayState(),
                                    "local a, b, c = zzz_generic_stub_example(1, 'x', true)\n"
                                    "assert(a == nil and b == nil and c == nil)",
                                    "chunk4");
            CHECK(r.loadOk && r.pcallOk);
        }
        CHECK(host.hitLog().hits().count("zzz_generic_stub_example") == 1);
        CHECK(host.hitLog().hits().at("zzz_generic_stub_example").callCount == 1);
        CHECK(host.hitLog().hits().at("zzz_generic_stub_example").lastArgc == 3);
        CHECK(host.hitLog().hits().at("zzz_generic_stub_example").lastArgTypes == "number,string,boolean");
        // Real per-state tracking (added same-day follow-up pass): a call
        // made against the gameplay state must be tallied under
        // gameplayCallCount, not uiCallCount - measured directly at the
        // real call site, never inferred from this name's own registered
        // cluster tag ("gameplay" here, but that's incidental to this
        // check).
        CHECK(host.hitLog().hits().at("zzz_generic_stub_example").gameplayCallCount == 1);
        CHECK(host.hitLog().hits().at("zzz_generic_stub_example").uiCallCount == 0);

        // Calling it again accumulates the hit count (a real, whole-run
        // aggregate, not a per-call snapshot).
        {
            auto r = host.runChunk(host.gameplayState(), "zzz_generic_stub_example()", "chunk5");
            CHECK(r.loadOk && r.pcallOk);
        }
        CHECK(host.hitLog().hits().at("zzz_generic_stub_example").callCount == 2);
        CHECK(host.hitLog().hits().at("zzz_generic_stub_example").lastArgc == 0);
        CHECK(host.hitLog().hits().at("zzz_generic_stub_example").lastArgTypes.empty());
    }

    // --- runChunk: a real syntax error and a real runtime error -----------
void test_block_3() {
        std::vector<RegisteredName> names; // no stubs needed for this fixture
        Host host(names);

        // Real Lua 5.1 syntax error - luaL_loadbuffer must fail, with a
        // real message, and lua_pcall must never even be attempted.
        {
            auto r = host.runChunk(host.gameplayState(), "function broken(", "bad_syntax.lua");
            CHECK(!r.loadOk);
            CHECK(!r.loadError.empty());
            CHECK(!r.ranPcall);
        }

        // Loads fine, but errors at runtime (calling a nil global) - a
        // real, distinct lua_pcall failure, with the loaded chunk's own
        // name/line real in the message.
        {
            auto r = host.runChunk(host.gameplayState(), "this_name_is_not_defined_anywhere()", "bad_runtime.lua");
            CHECK(r.loadOk);
            CHECK(r.ranPcall);
            CHECK(!r.pcallOk);
            CHECK(!r.pcallError.empty());
        }

        // A persistent state really does persist: a global defined by one
        // chunk is visible to the next chunk run against the SAME state -
        // the real engine's own "load game_lib.lua once, then run mission
        // scripts against the same state" model this scaffold follows.
        {
            auto r1 = host.runChunk(host.gameplayState(), "GLOBAL_FROM_EARLIER_SCRIPT = 42", "first.lua");
            CHECK(r1.loadOk && r1.pcallOk);
            auto r2 = host.runChunk(host.gameplayState(), "assert(GLOBAL_FROM_EARLIER_SCRIPT == 42)", "second.lua");
            CHECK(r2.loadOk && r2.pcallOk);
        }
    }

    // --- fireConfirmedHooks: existence-gated Group 1/3 behavior ----------
void test_block_4() {
        std::vector<RegisteredName> names; // no stubs needed
        Host host(names);
        lua_State* L = host.gameplayState();

        // Not defined at all -> not called, no error (matches spec
        // Sec8.3's silent existence check).
        {
            std::vector<HookSpec> hooks = {{"not_defined_hook_xyz", HookGroup::Group1_GeneralNamed, {}}};
            auto results = host.fireConfirmedHooks(L, hooks);
            CHECK(results.size() == 1);
            CHECK(!results[0].existedAsFunction);
            CHECK(!results[0].attemptedCall);
        }

        // Defined, runs clean.
        {
            auto r = host.runChunk(L, "function my_ok_hook() GLOBAL_HOOK_RAN = true end", "def1.lua");
            CHECK(r.loadOk && r.pcallOk);
            std::vector<HookSpec> hooks = {{"my_ok_hook", HookGroup::Group1_GeneralNamed, {}}};
            auto results = host.fireConfirmedHooks(L, hooks);
            CHECK(results.size() == 1);
            CHECK(results[0].existedAsFunction);
            CHECK(results[0].attemptedCall);
            CHECK(results[0].callOk);
            auto check = host.runChunk(L, "assert(GLOBAL_HOOK_RAN == true)", "check1.lua");
            CHECK(check.loadOk && check.pcallOk);
        }

        // Defined, errors at runtime - a real, distinct failure.
        {
            auto r = host.runChunk(L, "function my_bad_hook() error('deliberate test error') end", "def2.lua");
            CHECK(r.loadOk && r.pcallOk);
            std::vector<HookSpec> hooks = {{"my_bad_hook", HookGroup::Group1_GeneralNamed, {}}};
            auto results = host.fireConfirmedHooks(L, hooks);
            CHECK(results[0].existedAsFunction);
            CHECK(results[0].attemptedCall);
            CHECK(!results[0].callOk);
            CHECK(results[0].callError.find("deliberate test error") != std::string::npos);
        }

        // Defined as a non-function global (e.g. a number) - must NOT be
        // treated as existing (real lua_isfunction check), so Group 1/3
        // correctly skips it rather than erroring trying to call a number.
        {
            auto r = host.runChunk(L, "my_non_function_hook = 42", "def3.lua");
            CHECK(r.loadOk && r.pcallOk);
            std::vector<HookSpec> hooks = {{"my_non_function_hook", HookGroup::Group1_GeneralNamed, {}}};
            auto results = host.fireConfirmedHooks(L, hooks);
            CHECK(!results[0].existedAsFunction);
            CHECK(!results[0].attemptedCall);
        }
    }

    // --- fireConfirmedHooks: Group 2's real UNCONDITIONAL call (spec
    // Sec12.4) - existence is measured, but the call is attempted
    // regardless, unlike Group 1/3. -------------------------------------
void test_block_5() {
        std::vector<RegisteredName> names;
        Host host(names);
        lua_State* L = host.gameplayState();

        // Not defined: existence check false, but the call is still
        // ATTEMPTED (real engine's own unconditional-call behavior, spec
        // Sec12.4) and fails with a real Lua error - not silently skipped
        // like Group 1/3 would.
        {
            std::vector<HookSpec> hooks = {{"_NotDefinedLifecycleHook", HookGroup::Group2_LifecycleUnconditional, {}}};
            auto results = host.fireConfirmedHooks(L, hooks);
            CHECK(results.size() == 1);
            CHECK(!results[0].existedAsFunction);
            CHECK(results[0].attemptedCall);
            CHECK(!results[0].callOk);
            CHECK(!results[0].callError.empty());
        }

        // Defined: existence true, call attempted and succeeds.
        {
            auto r = host.runChunk(L, "function _DefinedLifecycleHook() end", "def4.lua");
            CHECK(r.loadOk && r.pcallOk);
            std::vector<HookSpec> hooks = {{"_DefinedLifecycleHook", HookGroup::Group2_LifecycleUnconditional, {}}};
            auto results = host.fireConfirmedHooks(L, hooks);
            CHECK(results[0].existedAsFunction);
            CHECK(results[0].attemptedCall);
            CHECK(results[0].callOk);
        }
    }

    // --- fireConfirmedHooks: the instruction-count watchdog really
    // aborts a runaway hook instead of hanging the caller (this project's
    // own real, found risk - real game_lib.lua text has patterns like
    // `repeat thread_yield() until <always-nil-returning stub>`, and
    // thread_yield is a genuine no-op on the main thread, so such a loop
    // would otherwise spin forever). --------------------------------
void test_block_6() {
        std::vector<RegisteredName> names;
        Host host(names);
        lua_State* L = host.gameplayState();
        auto r = host.runChunk(L, "function my_infinite_hook() while true do end end", "def5.lua");
        CHECK(r.loadOk && r.pcallOk);
        std::vector<HookSpec> hooks = {{"my_infinite_hook", HookGroup::Group1_GeneralNamed, {}}};
        auto results = host.fireConfirmedHooks(L, hooks);
        CHECK(results.size() == 1);
        CHECK(results[0].existedAsFunction);
        CHECK(results[0].attemptedCall);
        CHECK(!results[0].callOk); // the watchdog must have aborted it, not hung this test
        CHECK(results[0].callError.find("watchdog") != std::string::npos);

        // The state must still be perfectly usable afterward - the
        // watchdog's own error must not leave anything corrupted (its
        // lua_sethook is always cleared, the stack is always settled).
        auto after = host.runChunk(L, "assert(1 + 1 == 2)", "after_watchdog.lua");
        CHECK(after.loadOk && after.pcallOk);
    }

    // --- thread_* (2026-10-01): thread_new/thread_yield/thread_kill/
    // thread_check_done are now bare globals of 0x00e0f900 with the
    // CONFIRMED Sec26.27 behaviour (bare_globals.h; full tests in
    // tests/synthetic_luahost_bare_globals_test.cpp). thread_close stays the
    // ThreadScheduler scaffold's (not in the engine roster; OPEN). ---------
void test_block_7() {
        std::vector<RegisteredName> names;
        Host host(names);
        lua_State* L = host.gameplayState();
        auto r1 = host.runChunk(L,
            "thread_close(5)\n"                         // scaffold no-op
            "assert(select('#', thread_kill(65535)) == 0)\n" // no thread: nothing happens, no result
            "assert(thread_check_done(1) == true)",      // no such record -> done
            "thread1.lua");
        CHECK(r1.loadOk && r1.pcallOk);
        // thread_yield outside any coroutine: stock Lua's yield error (Sec26.27,
        // HIGH CONFIDENCE), no longer the scaffold's silent no-op.
        auto r4 = host.runChunk(L, "thread_yield()", "thread4.lua");
        CHECK(r4.loadOk && !r4.pcallOk);
        CHECK(r4.pcallError.find("attempt to yield across metamethod/C-call boundary") != std::string::npos);
        // All of them fold into the same HitLog ranking.
        for (const char* n : {"thread_close", "thread_kill", "thread_check_done", "thread_yield"})
            CHECK(host.hitLog().hits().count(n) == 1);
    }

    // --- confirmedHooks(): the real 138-entry static table (133 original
    // + 5 more Group-1 names, spec Sec14.6, same-day follow-up), sanity-
    // checked structurally (not re-deriving the spec's own text here -
    // just proving the shipped table is well-formed: the right total, the
    // right per-group split, every name non-empty, unique, and every
    // args list EMPTY except the small, explicitly-enumerated set spec
    // Sec14 gives a real argument contract for). -------------------------
void test_block_8() {
        const auto& hooks = confirmedHooks();
        CHECK(hooks.size() == 138);
        // Real, spec-confirmed (Sec14) per-hook argument counts - every
        // OTHER hook not listed here must have an EMPTY args list.
        std::unordered_map<std::string, size_t> expectedArgCount = {
            {"_PrepareForDynamicGlobals", 1},
            {"cmp_common_screen_start", 1},
            {"hud_running_man_event_update", 1},
        };
        size_t g1 = 0, g2 = 0, g3 = 0;
        std::vector<std::string> allNames;
        for (auto& h : hooks) {
            CHECK(!h.name.empty());
            auto it = expectedArgCount.find(h.name);
            if (it != expectedArgCount.end()) {
                CHECK(h.args.size() == it->second);
            } else {
                CHECK(h.args.empty());
            }
            allNames.push_back(h.name);
            if (h.group == HookGroup::Group1_GeneralNamed) ++g1;
            else if (h.group == HookGroup::Group2_LifecycleUnconditional) ++g2;
            else if (h.group == HookGroup::Group3_MissionNumberedPatternBased) ++g3;
        }
        CHECK(g1 == 78); // 73 original + 5 new (spec Sec14.6)
        CHECK(g2 == 2);
        CHECK(g3 == 58);
        std::sort(allNames.begin(), allNames.end());
        CHECK(std::adjacent_find(allNames.begin(), allNames.end()) == allNames.end()); // no duplicates

        // The 5 new Sec14.6 names are really present and really Group 1.
        for (const char* name : {"store_weapon_uncover_weapon", "store_gallery_upload_complete",
                                  "store_gallery_download_list_complete",
                                  "screen_capture_open_preview_dialog", "dialog_build"}) {
            CHECK(std::find(allNames.begin(), allNames.end(), std::string(name)) != allNames.end());
        }
    }

    // --- fireConfirmedHooks: real argument pushing (spec Sec14) - a
    // literal number, a literal string, and the real per-call
    // scriptFilename context value for HookArgKind::ScriptFilenameString.
    // ------------------------------------------------------------------
void test_block_9() {
        std::vector<RegisteredName> names;
        Host host(names);
        lua_State* L = host.gameplayState();

        // A literal number arg really arrives as a real Lua number.
        {
            auto r = host.runChunk(L, "function my_number_hook(x) assert(x == 0.0) end", "defn.lua");
            CHECK(r.loadOk && r.pcallOk);
            HookSpec spec;
            spec.name = "my_number_hook";
            spec.group = HookGroup::Group1_GeneralNamed;
            spec.args = {sr3luahost::HookArg::number(0.0)};
            auto results = host.fireConfirmedHooks(L, {spec});
            CHECK(results[0].existedAsFunction);
            CHECK(results[0].attemptedCall);
            CHECK(results[0].callOk);
        }

        // ScriptFilenameString really pushes the real scriptFilename this
        // call was given - not a placeholder, not empty (when a real one
        // is supplied).
        {
            auto r = host.runChunk(L, "function my_filename_hook(name) assert(name == 'real_script.lua') end", "defs.lua");
            CHECK(r.loadOk && r.pcallOk);
            HookSpec spec;
            spec.name = "my_filename_hook";
            spec.group = HookGroup::Group2_LifecycleUnconditional;
            spec.args = {sr3luahost::HookArg::scriptFilename()};
            auto results = host.fireConfirmedHooks(L, {spec}, "real_script.lua");
            CHECK(results[0].attemptedCall);
            CHECK(results[0].callOk);
        }
    }

    // --- EngineState::multiply33XorHashBucket(): the real, CONFIRMED,
    // empirically vector-validated "multiply-33/XOR" hash
    // (spec-texture-format.md Sec8.2), cross-confirmed by
    // spec-lua-api-behaviour.md Sec5.3's own 5 real I/O vectors -
    // reproduced here exactly, byte-for-byte against those documented
    // vectors, not just "some hash." -----------------------------------
void test_block_10() {
        CHECK(EngineState::multiply33XorHashBucket("Tint_color", 0x20) == 0x5);
        CHECK(EngineState::multiply33XorHashBucket("continuous_spawn_start", 0x20) == 0x16);
        CHECK(EngineState::multiply33XorHashBucket("", 0x20) == 0x0);
        CHECK(EngineState::multiply33XorHashBucket("SAINTS_ROW_THE_THIRD", 0x100) == 0x7d);
        CHECK(EngineState::multiply33XorHashBucket("a", 0x20) == 0x1);
        // Sec8.2 (2026-09-30): modulo and char signedness are OPEN, so a
        // bucket is only unambiguous for ASCII names, and for a
        // non-power-of-two count only when the hash is below 2^31.
        CHECK(EngineState::multiply33XorHashBucketUnambiguous("tex_a.cvbm_pc", 1024));  // power of two
        CHECK(!EngineState::multiply33XorHashBucketUnambiguous("tex_a.cvbm_pc", 9000)); // 0xd80fcd8f
        CHECK(EngineState::multiply33XorHashBucketUnambiguous("tex_b.cvbm_pc", 9000));  // 0x4010c64c
        CHECK(EngineState::multiply33XorHashBucketUnambiguous("", 9000));
        CHECK(!EngineState::multiply33XorHashBucketUnambiguous("a\xe9", 0x20));        // byte >= 0x80
        CHECK(!EngineState::multiply33XorHashBucketUnambiguous("a\x80", 9000));
    }

