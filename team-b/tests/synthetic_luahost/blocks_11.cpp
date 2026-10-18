// Synthetic sr3luahost tests: test_block_11 .. test_block_11 (split out of synthetic_luahost_test.cpp, bodies verbatim).
#include "synthetic_luahost/common.h"

    // --- The 12 spec-confirmed names promoted from a generic logged-nil
    // stub to real, spec-confirmed behavior (spec_confirmed_stubs.h/.cpp),
    // plus vint_object_find (folded in mid-task, spec-lua-bindings.md
    // Sec13.7/Sec15), plus 9 more (functions 14-22, spec-lua-api-
    // behaviour.md Sec10.1-Sec10.9, a same-shape follow-up pass) - one
    // real test per function, each derived from that function's own spec
    // section, exercised through the real Lua-visible global (never by
    // calling the C++ trampoline directly) so this is a genuine
    // round-trip through the real Lua 5.1 C API. Host is built with
    // specConfirmedFixtureNames() standing in for the real tagged
    // registration list's own 22 rows (same real cluster tags) - Host's
    // own real per-name-cluster split (lua_host.cpp) is exercised for
    // real, not bypassed. -------------------------------------------
void test_block_11() {
        Host host(specConfirmedFixtureNames());
        lua_State* gp = host.gameplayState();
        lua_State* ui = host.uiState();
        // A call reading OPEN engine state (open_state.h) must fail with a
        // Lua error naming that state, until a test sets it.
        auto refusesOpen = [&](lua_State* st, const char* chunk, const char* needle) {
            auto r = host.runChunk(st, chunk, "open_refusal.lua");
            CHECK(r.loadOk && !r.pcallOk);
            CHECK(r.pcallError.find("is OPEN") != std::string::npos);
            CHECK(r.pcallError.find(needle) != std::string::npos);
        };
        EngineState& es = host.engineState();

        // Real per-state placement: 21 of the 22 names are a real function
        // in EXACTLY the state their own cluster tag names, nil in the
        // other - proves the per-name-cluster-aware special registration
        // path (lua_host.cpp) actually works, not just "some state got
        // it." coop_is_active is the one confirmed exception (spec-lua-
        // bindings.md Sec13.5, resolved 2026-09-30): real, unconditional
        // dual-registration into BOTH states - checked as a real function
        // in both below, not nil in either.
        {
            auto r1 = host.runChunk(gp, "assert(type(coop_is_active) == 'function')", "spec1.lua");
            CHECK(r1.loadOk && r1.pcallOk);
            auto r2 = host.runChunk(ui, "assert(type(coop_is_active) == 'function')", "spec2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
            auto r3 = host.runChunk(ui, "assert(type(game_get_key_name) == 'function')", "spec3.lua");
            CHECK(r3.loadOk && r3.pcallOk);
            auto r4 = host.runChunk(gp, "assert(game_get_key_name == nil)", "spec4.lua");
            CHECK(r4.loadOk && r4.pcallOk);
        }

        // 1. coop_is_active (Sec3.1, Sec26.28): no session at start-up
        // (CONFIRMED) -> false. With a session: host test first, else the
        // client gate; >= 2 members (unsigned); every other member passes
        // the slot check. Per-session fields are OPEN until set.
        {
            auto ok = [&](const char* chunk) {
                auto r = host.runChunk(gp, chunk, "coop.lua");
                CHECK(r.loadOk && r.pcallOk);
                if (!r.pcallOk) std::cerr << chunk << ": " << r.pcallError << "\n";
            };
            ok("assert(coop_is_active() == false)");
            auto& cs = es.coopSession();
            cs.present.set(true);
            refusesOpen(gp, "coop_is_active()", "+0x5c == +0x58");
            cs.localIsHost.set(true);
            refusesOpen(gp, "coop_is_active()", "+0x60"); // host: client gate not read
            cs.memberCount.set(1);
            ok("assert(coop_is_active() == false)");    // one member: never active
            cs.memberCount.set(0);
            ok("assert(coop_is_active() == false)");
            cs.memberCount.set(2);
            refusesOpen(gp, "coop_is_active()", "0x00877a90");
            cs.otherMembersPassSlotCheck.set(true);
            ok("assert(coop_is_active() == true)");
            cs.otherMembersPassSlotCheck.set(false);
            ok("assert(coop_is_active() == false)");
            cs.localIsHost.set(false);                  // client
            refusesOpen(gp, "coop_is_active()", "client gate");
            cs.clientGate.set(false);
            ok("assert(coop_is_active() == false)");
            cs.clientGate.set(true);
            cs.otherMembersPassSlotCheck.set(true);
            cs.memberCount.set(3);
            ok("assert(coop_is_active() == true)");
            auto r2 = host.runChunk(ui, "assert(coop_is_active() == true)", "coop_ui.lua"); // dual-registered
            CHECK(r2.loadOk && r2.pcallOk);
            // Back to the start-up state; the session fields return to OPEN.
            cs.present.set(false);
            cs.localIsHost.forget();
            cs.clientGate.forget();
            cs.memberCount.forget();
            cs.otherMembersPassSlotCheck.forget();
            ok("assert(coop_is_active() == false)");
        }

        // 2. game_get_key_name (Sec2.3): CONFIRMED sentinel -1 -> "PC_UNBOUND_KEY".
        // General path: a real GetKeyNameTextW call - only asserted non-empty
        // (real OS/locale-dependent content, not asserted byte-exact).
        {
            auto r = host.runChunk(ui, "assert(game_get_key_name(-1) == 'PC_UNBOUND_KEY')", "keyname1.lua");
            CHECK(r.loadOk && r.pcallOk);
            // 0x1E is the real DirectInput scan code for 'A'
            // (spec-tables-ui-controls.md Sec4.3) - a real, ordinary key,
            // not the sentinel.
            // Non-empty only where the real OS lookup exists (Windows); the
            // portable build has no key-name source and returns "" - the
            // CONFIRMED "1 Lua string" return contract holds on both.
#ifdef _WIN32
            auto r2 = host.runChunk(ui,
                "local n = game_get_key_name(0x1E)\n"
                "assert(type(n) == 'string' and #n > 0)",
                "keyname2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
#else
            // No OS key-name source off Windows: OPEN, not an invented "".
            refusesOpen(ui, "game_get_key_name(0x1E)", "GetKeyNameTextW");
#endif
            // Out-of-int32-range / NaN key codes: truncation unspecified -> OPEN (was UB).
            refusesOpen(ui, "game_get_key_name(1e300)", "key code truncation");
            refusesOpen(ui, "game_get_key_name(0/0)", "key code truncation");
        }

        // 3. game_UI_audio_play (Sec2.2): a handle is ALWAYS returned,
        // strictly increasing across calls (this project's own chosen
        // stand-in - see EngineState::nextAudioVoiceHandle's own doc
        // comment), regardless of whether a sound argument was given.
        {
            auto r = host.runChunk(ui,
                "local h1 = game_UI_audio_play('some_sound')\n"
                "local h2 = game_UI_audio_play()\n"
                "assert(type(h1) == 'number' and type(h2) == 'number' and h2 > h1)",
                "audioplay1.lua");
            CHECK(r.loadOk && r.pcallOk);
        }

        // 4. game_audio_get_audio_id (Sec8.22): CONFIRMED number path
        // (round-then-mask-to-16-bits), CONFIRMED absent->0, CONFIRMED
        // "none"/"" sentinel ->0.
        {
            auto r = host.runChunk(ui,
                "assert(game_audio_get_audio_id(70000.4) == (70000 % 65536))\n"
                // 0x00ea2596 truncates toward zero (Sec4.1, settled 2026-10-01):
                // 70000.9 -> 70000 (not 70001), -1.5 -> -1 -> 0xFFFF (not -2 -> 0xFFFE),
                // 2.5 -> 2 and 3.5 -> 3 (not round-half-even's 2 and 4).
                "assert(game_audio_get_audio_id(70000.9) == (70000 % 65536))\n"
                "assert(game_audio_get_audio_id(-1.5) == 65535)\n"
                "assert(game_audio_get_audio_id(-0.9) == 0)\n"
                "assert(game_audio_get_audio_id(2.5) == 2 and game_audio_get_audio_id(3.5) == 3)\n"
                "assert(game_audio_get_audio_id() == 0)\n"
                "assert(game_audio_get_audio_id('none') == 0)\n"
                "assert(game_audio_get_audio_id('') == 0)\n"
                "assert(game_audio_get_audio_id('NoNe') == 0)", // case-insensitive
                "audioid1.lua");
            CHECK(r.loadOk && r.pcallOk);
            // Any other string needs the Wwise resolver: OPEN, not an invented 0.
            refusesOpen(ui, "game_audio_get_audio_id('some_event')", "Wwise id");
        }

        // 5. game_get_key_name_for_action (Sec8.23): CONFIRMED literal
        // CAA_CAMERA_ROTATE -> STR_THE_MOUSE (case-insensitive match, real
        // __stricmp-equivalent contract); CONFIRMED "no match" -> "".
        // Re-checked 2026-10-02 against spec-tables-ui-controls.md Sec4.1/
        // Sec4.2 (CONFIRMED, not held): the CBA sentinel BUTTON_UNBOUND
        // (value -1, so it never satisfies the CBA "bound" branch) and 6
        // CAA representative entries at axis indices other than 0/2/3 are
        // now individually named in spec text and resolve to "" too.
        {
            auto r = host.runChunk(ui,
                "assert(game_get_key_name_for_action('CAA_CAMERA_ROTATE') == 'STR_THE_MOUSE')\n"
                "assert(game_get_key_name_for_action('caa_camera_rotate') == 'STR_THE_MOUSE')\n"
                "assert(game_get_key_name_for_action('BUTTON_UNBOUND') == '')\n"
                "assert(game_get_key_name_for_action('button_unbound') == '')\n"
                "assert(game_get_key_name_for_action('CAA_CAMERA_ELEVATE') == '')\n"
                "assert(game_get_key_name_for_action('CAA_DRIVE_STEER') == '')\n"
                "assert(game_get_key_name_for_action('CAA_TANK_DRIVE_FORWARD_BACKWARD') == '')\n"
                "assert(game_get_key_name_for_action('CAA_VPC_TURRET_RIGHT') == '')\n"
                "assert(game_get_key_name_for_action('CAA_TURRET_CAMERA_ELEVATE') == '')\n"
                "assert(game_get_key_name_for_action('CAA_PLANE_ROLL_LEFT_RIGHT') == '')",
                "actionname1.lua");
            CHECK(r.loadOk && r.pcallOk);
            // Everything else goes through the CBA/CAA tables + live bindings: OPEN.
            refusesOpen(ui, "game_get_key_name_for_action('CBA_SOME_UNKNOWN_ACTION')", "key binding");
            // The two-key movement axes (CAA axis 2/3) stay OPEN too - their
            // own sub-action resolvers are still unread (Sec8.23's own
            // closing OPEN item), unaffected by the ui-controls table sync.
            refusesOpen(ui, "game_get_key_name_for_action('CAA_WALK_FORWARD_BACKWARD')", "key binding");
        }

        // 6. game_peg_load_with_cb (Sec8.24): CONFIRMED arg-reading
        // contract (name bounds-checked, count clamped 1..6, filenames
        // read while present) plus this task's own real, CONFIRMED-hash
        // bucket-index cross-check (bucket_count=9000, same routine
        // tested directly above).
        {
            auto r = host.runChunk(ui, "game_peg_load_with_cb('req1', 2, 'tex_a.cvbm_pc', 'tex_b.cvbm_pc')", "peg1.lua");
            CHECK(r.loadOk && r.pcallOk);
            CHECK(es.pegLoadRequests().size() == 1);
            const auto& req = es.pegLoadRequests().back();
            CHECK(req.requestName == "req1");
            CHECK(req.filenames.size() == 2);
            CHECK(req.filenames[0].name == "tex_a.cvbm_pc");
            // tex_a hashes to 0xd80fcd8f (>= 2^31): with 9000 buckets signed and
            // unsigned modulo disagree, both OPEN in Sec8.2 -> index stays OPEN.
            CHECK(!req.filenames[0].bucketIndex.known());
            CHECK(req.filenames[1].name == "tex_b.cvbm_pc");
            // tex_b hashes to 0x4010c64c (< 2^31): every reading agrees.
            CHECK(req.filenames[1].bucketIndex.known());
            CHECK(req.filenames[1].bucketIndex.get() == 0x4010c64cu % 9000u);

            // Count out of the confirmed 1..6 range -> no filenames read,
            // even though more string args were supplied.
            auto r2 = host.runChunk(ui, "game_peg_load_with_cb('req2', 0, 'should_not_be_read.cvbm_pc')", "peg2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
            CHECK(es.pegLoadRequests().size() == 2);
            CHECK(es.pegLoadRequests().back().filenames.empty());
        }

        // 7. ai_add_enemy_target (Sec3.9, re-derived 2026-10-07, stub
        // rewritten 2026-10-08, actor gate defaulted true 2026-10-08):
        // actor must resolve, pass the shared "not dead" predicate, AND
        // have descriptor-byte +0x33 bit 0x8 set (aiTargetActorGateBit33 -
        // CHOSEN true by default since a resolved character is presumed
        // already fully spawned, see that field's own doc comment,
        // character_state.h); the "#CLOSEST_PLAYER#" sentinel is
        // redirected to the literal "#PLAYER1#" BEFORE the target resolve
        // (resolveClosestPlayerSentinel(), this project's single-player-
        // only degenerate case); the bits are sourced from arg3 (==0/==2),
        // not arg4/the sentinel match; arg4 fires a SEPARATE, record-free
        // effect; the owner gate (aiTargetOwnerGatePasses, CHOSEN false)
        // selects create-vs-fetch.
        {
            // Actor unresolved -> OPEN, refuses.
            refusesOpen(gp, "ai_add_enemy_target('villain01', '#CLOSEST_PLAYER#', 5, true)", "named-object resolution");
            es.objectResolves().set("villain01", true);
            // Actor resolved; the +0x33 bit 0x8 gate defaults true WITHOUT
            // any explicit set - this is the fix itself (prior to
            // 2026-10-08 this was an OpenValue that refused here instead).
            CHECK(es.getOrCreateCharacter("villain01").aiTargetActorGateBit33 == true);
            // Explicitly clear it to prove the implementation still
            // honours an explicit false (not just always-true regardless
            // of the field).
            es.getOrCreateCharacter("villain01").aiTargetActorGateBit33 = false;
            auto rGate = host.runChunk(gp, "assert(ai_add_enemy_target('villain01', '#CLOSEST_PLAYER#', 5, true) == false)",
                                        "enemy_gate_false.lua");
            CHECK(rGate.loadOk && rGate.pcallOk);
            CHECK(es.getOrCreateCharacter("villain01").enemyTargets.empty());
            es.getOrCreateCharacter("villain01").aiTargetActorGateBit33 = true;

            // Gate passes now; target is "#CLOSEST_PLAYER#" -> redirected
            // to "#PLAYER1#", which applySpecInitialState() already seeds
            // resolved=true (spec-lua-api-behaviour.md Sec13.6/Sec29.4's
            // own CONFIRMED pre-resolved player name - no extra setup
            // needed here).
            CHECK(es.objectResolves().get("#PLAYER1#") == true);

            auto r = host.runChunk(gp,
                "local ok = ai_add_enemy_target('villain01', '#CLOSEST_PLAYER#', 5, true)\n"
                "assert(ok == true)",
                "enemy1.lua");
            CHECK(r.loadOk && r.pcallOk);
            auto& actor = es.getOrCreateCharacter("villain01");
            // Default owner gate is CHOSEN false ("common single-player
            // case") -> unconditional create path, keyed by priority "5".
            CHECK(actor.enemyTargets.count("5") == 1);
            const auto& rec = actor.enemyTargets.at("5");
            CHECK(rec.priorityOrId == 5);
            CHECK(rec.bit02ArgIsZero == false); // arg3 == 5, neither 0 nor 2
            CHECK(rec.bit04ArgIsTwo == false);
            CHECK(rec.createdNewRecord == true);
            // arg4 true fired its OWN separate effect, redirected target name.
            CHECK(actor.lastAiAddEnemyTargetArg4TargetName == "#PLAYER1#");
            CHECK(actor.aiAddEnemyTargetArg4CallCount == 1);

            // priority truncated toward zero (7.9 -> "7"); arg3==0/==2 bits.
            auto rt = host.runChunk(gp, "assert(ai_add_enemy_target('villain01', '#CLOSEST_PLAYER#', 7.9) == true)", "enemy_trunc.lua");
            CHECK(rt.loadOk && rt.pcallOk);
            CHECK(actor.enemyTargets.count("7") == 1 && actor.enemyTargets.count("8") == 0);
            CHECK(actor.aiAddEnemyTargetArg4CallCount == 1); // arg4 absent this time - no second fire
            auto rZero = host.runChunk(gp, "assert(ai_add_enemy_target('villain01', '#CLOSEST_PLAYER#', 0) == true)", "enemy_zero.lua");
            CHECK(rZero.loadOk && rZero.pcallOk);
            CHECK(actor.enemyTargets.count("#PLAYER1#") == 1); // priority 0 -> keyed by the resolved target name
            CHECK(actor.enemyTargets.at("#PLAYER1#").bit02ArgIsZero == true);

            // Owner gate true -> fetch-existing path: a miss writes
            // nothing (key "100" was never created).
            actor.aiTargetOwnerGatePasses = true;
            auto rFetchMiss = host.runChunk(gp, "assert(ai_add_enemy_target('villain01', '#CLOSEST_PLAYER#', 100) == true)",
                                             "enemy_fetch_miss.lua");
            CHECK(rFetchMiss.loadOk && rFetchMiss.pcallOk);
            CHECK(actor.enemyTargets.count("100") == 0); // CONFIRMED: fetch-miss writes nothing
            // A pre-existing record at the same key DOES get updated.
            actor.aiTargetOwnerGatePasses = false;
            host.runChunk(gp, "ai_add_enemy_target('villain01', '#CLOSEST_PLAYER#', 2)", "enemy_seed2.lua");
            CHECK(actor.enemyTargets.at("2").createdNewRecord == true);
            actor.aiTargetOwnerGatePasses = true;
            host.runChunk(gp, "ai_add_enemy_target('villain01', '#CLOSEST_PLAYER#', 2)", "enemy_fetch_hit.lua");
            CHECK(actor.enemyTargets.at("2").createdNewRecord == false); // fetch path - not re-created
            CHECK(actor.enemyTargets.at("2").bit04ArgIsTwo == true);
        }

        // 8. on_take_damage (Sec3.13): the callback name lands on the
        // resolved target's own field.
        {
            auto r = host.runChunk(gp, "on_take_damage('my_callback_fn', 'target_npc')", "takedmg1.lua");
            CHECK(r.loadOk && r.pcallOk);
            CHECK(es.getOrCreateCharacter("target_npc").onTakeDamageCallback == "my_callback_fn");
        }

        // 9. set_ignore_ai_flag (Sec3.4): CONFIRMED default TRUE when arg 2
        // omitted/nil (the one function in this batch defaulting a
        // nil-gated bool to true); explicit false; and the HIGH-
        // CONFIDENCE-tagged conditional action-override side effect when
        // newly enabling while stateEnum==3 ("in a vehicle"). Character
        // spawn state (Sec34.1/Sec34.3, 2026-10-02): a freshly touched
        // name's ignore-AI is now a CONFIRMED default (off), applied by
        // EngineState::getOrCreateCharacter() itself - not OPEN - except
        // for the one path (a bound script NPC, Sec34.4) that correctly
        // stays OPEN. stateEnum ALSO now has a CONFIRMED spawn default
        // (spec-player-runtime.md, 2026-10-08: constructor 0x00954340
        // zeroes +0x16d4 unconditionally - see CharacterState::stateEnum's
        // own doc comment) - a freshly touched character's "newly enabling"
        // branch no longer refuses on stateEnum at all, it now reads a real
        // 0 and falls through to the NEXT read.
        //
        // CORRECTED 2026-10-09 (Sec3.4's own in-place correction, routed
        // from Team A, mission-blocking m07): that next read used to be the
        // (incorrect, now-removed) attackerThreatRef field; it is now
        // CharacterState::stateCodeByte501/aiSubStateByte500 (the real AI
        // mode/sub-state bytes +0x501/+0x500).
        //
        // SUPERSEDED 2026-10-10 (Sec22.3 sync, seeded by
        // feature/vehicle-group-check-done-occupant-conflict, 1645584):
        // stateCodeByte501 ALSO now has a CONFIRMED spawn default (0/Idle),
        // same as stateEnum above. Since the override condition is
        // `stateCodeByte501 != Idle && aiSubStateByte500 != 9` (short-
        // circuited, stateCodeByte501 read first), a freshly-spawned
        // character's own Idle default makes the whole condition false
        // WITHOUT ever reading aiSubStateByte500 - so npc_a's own
        // "newly enabling" calls below no longer refuse at all, they
        // succeed directly (override stays 1, the "leave untouched" value).
        // The aiSubStateByte500-still-OPEN refusal path is still real code
        // (see below) - it just needs a character whose stateCodeByte501 was
        // explicitly set to a non-Idle value first, which npc_a never does.
        {
            // CONFIRMED default (Sec34.1/Sec34.3): known immediately, off,
            // no .set() needed first.
            CHECK(es.getOrCreateCharacter("npc_a").ignoreAI.known());
            CHECK(es.getOrCreateCharacter("npc_a").ignoreAI.get() == false);
            // CONFIRMED default (spec-player-runtime.md, 2026-10-08): known
            // immediately, 0 ("not in a vehicle"), no .set() needed first.
            CHECK(es.getOrCreateCharacter("npc_a").stateEnum.known());
            CHECK(es.getOrCreateCharacter("npc_a").stateEnum.get() == 0);
            // CONFIRMED default (Sec22.3): known immediately, 0/Idle.
            CHECK(es.getOrCreateCharacter("npc_a").stateCodeByte501.known());
            CHECK(es.getOrCreateCharacter("npc_a").stateCodeByte501.get() == sr3luahost::CharacterState::kAiModeIdle);
            // Default arg2 (true) against the off default is "newly
            // enabling": stateEnum's real 0 passes (not a vehicle), and
            // stateCodeByte501's real Idle default short-circuits the
            // override condition to false before aiSubStateByte500 is ever
            // read - the call succeeds outright, no override write.
            auto r0 = host.runChunk(gp, "set_ignore_ai_flag('npc_a')", "ignoreai0.lua");
            CHECK(r0.loadOk && r0.pcallOk);
            CHECK(es.getOrCreateCharacter("npc_a").ignoreAI.get() == true);
            CHECK(!es.getOrCreateCharacter("npc_a").actionOverrideId.known()); // override==1 ("leave") -> no write

            auto r = host.runChunk(gp, "set_ignore_ai_flag('npc_a')", "ignoreai1.lua"); // no 2nd arg -> default true; already true -> no enable branch, stateEnum/stateCodeByte501/aiSubStateByte500 never read
            CHECK(r.loadOk && r.pcallOk);
            CHECK(es.getOrCreateCharacter("npc_a").ignoreAI.get() == true);

            auto r2 = host.runChunk(gp, "set_ignore_ai_flag('npc_a', false)", "ignoreai2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
            CHECK(es.getOrCreateCharacter("npc_a").ignoreAI.get() == false);
            // Newly enabling again: same short-circuit as r0 above (stateCodeByte501
            // on npc_a was never changed from its Idle default) - succeeds directly.
            auto r2b = host.runChunk(gp, "set_ignore_ai_flag('npc_a', true)", "ignoreai2b.lua");
            CHECK(r2b.loadOk && r2b.pcallOk);
            CHECK(es.getOrCreateCharacter("npc_a").ignoreAI.get() == true);
            CHECK(!es.getOrCreateCharacter("npc_a").actionOverrideId.known());

            // The aiSubStateByte500-still-OPEN refusal path (a real code
            // path, not removed by the Sec22.3 sync - it only became
            // unreachable through npc_a's own untouched Idle default):
            // reached by explicitly setting stateCodeByte501 to a non-Idle
            // value first and leaving aiSubStateByte500 OPEN.
            auto& npcD = es.getOrCreateCharacter("npc_d");
            npcD.stateCodeByte501.set(43); // sr3airuntime::mode::Player - anything != Idle(0)
            refusesOpen(gp, "set_ignore_ai_flag('npc_d')", "+0x500");
            CHECK(npcD.ignoreAI.get() == false); // unchanged: refused before any write

            auto& npcB = es.getOrCreateCharacter("npc_b");
            npcB.stateEnum.set(3); // test setup - "in a vehicle" (Sec3.4/Sec3.10); ignoreAI already off via the CONFIRMED default
            CHECK(!npcB.actionOverrideId.known());
            auto r3 = host.runChunk(gp, "set_ignore_ai_flag('npc_b', true)", "ignoreai3.lua"); // false->true: "newly enabling"
            CHECK(r3.loadOk && r3.pcallOk);
            CHECK(es.getOrCreateCharacter("npc_b").actionOverrideId.get() == 0x19); // HIGH CONFIDENCE meaning, CONFIRMED structure
            // Not in a vehicle, AI mode non-Idle and sub-state != 9 -> override cleared to 0.
            auto& npcC = es.getOrCreateCharacter("npc_c");
            npcC.stateEnum.set(0);
            npcC.stateCodeByte501.set(43); // sr3airuntime::mode::Player - anything != Idle(0)
            npcC.aiSubStateByte500.set(0); // anything != 9
            auto r4 = host.runChunk(gp, "set_ignore_ai_flag('npc_c', true)", "ignoreai4.lua");
            CHECK(r4.loadOk && r4.pcallOk);
            CHECK(npcC.actionOverrideId.get() == 0);

            // Sec34.4: a character explicitly bound to a script NPC - the
            // override question (script_npc_flags containing "ignore_ai")
            // is zone data this project does not have (the .czn_pc hold),
            // so it correctly stays OPEN even though an ordinary fresh name
            // would not be.
            es.markScriptNpcBoundForTesting("npc_scriptnpc");
            CHECK(!es.getOrCreateCharacter("npc_scriptnpc").ignoreAI.known());
            refusesOpen(gp, "set_ignore_ai_flag('npc_scriptnpc', false)", "ignore-AI flag");
        }

        // 10./11. get_max_hit_points (Sec7.12) / set_current_hit_points
        // (Sec7.31): the round-trip this task's own brief specifically
        // asked for. set_current_hit_points clamps into [0, max], leaves max
        // untouched (CONFIRMED cross-check, same field); un-gated numeric
        // arg 2 omission resolves to 0 (CONFIRMED); clamped<=0 sets the
        // HIGH-CONFIDENCE-only isDeadHighConfidence flag.
        //
        // UPDATE 2026-10-07 (Sec60.4's own closure; probe_blockers_ranked_
        // 20261007.md item 2, mission `m18`): max hit points is no longer
        // left OPEN for an ordinary, never-touched character -
        // drawOrGetMaxHitPoints() (engine_state.h/lua_engine_state.cpp)
        // draws once and caches. No real spawn-rank table is installed in
        // this synthetic test (installSpawnRankHitPoints() never called),
        // so the draw falls back to the single CHOSEN diagnostic constant,
        // 100 - the exact value this test already asserts below, now via
        // the real default path rather than an explicit .set().
        {
            auto r0 = host.runChunk(gp, "assert(get_max_hit_points('hero') == 100.0)", "hp0.lua");
            CHECK(r0.loadOk && r0.pcallOk);
            CHECK(es.getOrCreateCharacter("hero").maxHitPoints.known() &&
                  es.getOrCreateCharacter("hero").maxHitPoints.get() == 100);
            // set_current_hit_points on a DIFFERENT, still-untouched name
            // draws (and caches) the same CHOSEN fallback for its own cap
            // read - no explicit .set() needed either.
            auto rcap = host.runChunk(gp, "set_current_hit_points('capdraw', 999)", "hp_capdraw.lua");
            CHECK(rcap.loadOk && rcap.pcallOk);
            CHECK(es.getOrCreateCharacter("capdraw").currentHitPoints.get() == 100); // clamped to the drawn cap
            // Re-asserting .set() still wins outright (explicit value, not
            // overwritten by the draw) - same "seed on first touch, don't
            // clobber" convention as every other field in this file.
            es.getOrCreateCharacter("hero").maxHitPoints.set(100);
            auto r = host.runChunk(gp, "assert(get_max_hit_points('hero') == 100.0)", "hp1.lua");
            CHECK(r.loadOk && r.pcallOk);

            auto r2 = host.runChunk(gp, "set_current_hit_points('hero', 50)", "hp2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
            CHECK(es.getOrCreateCharacter("hero").currentHitPoints.get() == 50);
            auto r3 = host.runChunk(gp, "assert(get_max_hit_points('hero') == 100.0)", "hp3.lua"); // untouched
            CHECK(r3.loadOk && r3.pcallOk);
            CHECK(es.getOrCreateCharacter("hero").isDeadHighConfidence == false);

            auto rt = host.runChunk(gp, "set_current_hit_points('hero', 50.9)", "hp_trunc.lua"); // truncation (Sec4.1)
            CHECK(rt.loadOk && rt.pcallOk);
            CHECK(es.getOrCreateCharacter("hero").currentHitPoints.get() == 50);

            auto r4 = host.runChunk(gp, "set_current_hit_points('hero', 999)", "hp4.lua"); // clamp to cap
            CHECK(r4.loadOk && r4.pcallOk);
            CHECK(es.getOrCreateCharacter("hero").currentHitPoints.get() == 100);

            auto r5 = host.runChunk(gp, "set_current_hit_points('hero', -50)", "hp5.lua"); // clamp to 0
            CHECK(r5.loadOk && r5.pcallOk);
            CHECK(es.getOrCreateCharacter("hero").currentHitPoints.get() == 0);
            CHECK(es.getOrCreateCharacter("hero").isDeadHighConfidence == true);

            es.getOrCreateCharacter("freshvictim").maxHitPoints.set(80);
            auto r6 = host.runChunk(gp, "set_current_hit_points('freshvictim')", "hp6.lua"); // omitted arg2 -> 0 (CONFIRMED nil-handling)
            CHECK(r6.loadOk && r6.pcallOk);
            CHECK(es.getOrCreateCharacter("freshvictim").currentHitPoints.get() == 0);

            // Character spawn state (Sec34.1/Sec34.2, 2026-10-02): the
            // CONFIRMED default-path formula, round(M * Hit_Points) with M
            // defaulting to 1.0 for ordinary classes - applied via
            // EngineState::applyCharacterSpawnDefaults() once a caller
            // supplies a resolved Hit_Points value (this project has no way
            // to attach one to a bare name on its own - see
            // CharacterState::maxHitPoints's own doc comment). A fresh name
            // with no such call no longer stays OPEN (2026-10-07, same
            // update as 'hero'/'capdraw' above) - it would draw-and-cache
            // the CHOSEN 100 fallback instead, exactly like 'hero' did, but
            // applyCharacterSpawnDefaults()'s own unconditional set() below
            // overwrites that regardless of which ran first, so 'grunt'
            // ends up at the real 150 either way - demonstrated directly by
            // skipping the draw here (not calling get_max_hit_points first).
            es.applyCharacterSpawnDefaults("grunt", 150); // M=1.0: round(1.0*150) = 150
            CHECK(es.getOrCreateCharacter("grunt").maxHitPoints.get() == 150);
            CHECK(es.getOrCreateCharacter("grunt").currentHitPoints.get() == 150); // Sec34.1 step 2: spawns at full health
            CHECK(es.getOrCreateCharacter("grunt").ignoreAI.get() == false);
            auto rg = host.runChunk(gp, "assert(get_max_hit_points('grunt') == 150.0)", "hp_spawn1.lua");
            CHECK(rg.loadOk && rg.pcallOk);
            // Explicit multiplier, half-away-from-zero rounding (this
            // project's one CONFIRMED round() convention, Sec26.27, reused
            // here per Sec34.2's own "round(...)" text - see
            // RoundHalfAwayFromZero's own doc comment, lua_engine_state.cpp).
            es.applyCharacterSpawnDefaults("elite", 75, 1.5); // round(1.5*75) = round(112.5) = 113
            CHECK(es.getOrCreateCharacter("elite").maxHitPoints.get() == 113);

            // Sec34.4: a character explicitly bound to a script NPC - the
            // override question (script_npc_hp) is zone data this project
            // does not have, so even after the spawn-default formula ran,
            // marking it bound forgets the result back to OPEN rather than
            // guess whether an override would have applied instead.
            es.applyCharacterSpawnDefaults("maybe_scripted", 200);
            CHECK(es.getOrCreateCharacter("maybe_scripted").maxHitPoints.known());
            es.markScriptNpcBoundForTesting("maybe_scripted");
            refusesOpen(gp, "get_max_hit_points('maybe_scripted')", "max hit points");

            // UPDATE 2026-10-08 (zone-data fast-follow, orchestrator follow-up
            // to commit 50bdb29): once a caller DOES already know a specific
            // script-NPC-bound character's own script_npc_hp value
            // (sr3zone::ScriptNpcHp(), spec-zone-data-format.md Sec11.7 - this
            // project still has no by-name resolver that finds that value on
            // its own; see CharacterState::scriptNpcHp's own doc comment -
            // this is the SAME "caller supplies it" pattern
            // applyCharacterSpawnDefaults() already uses for the table case),
            // spec-lua-api-behaviour.md Sec34.2's own CONFIRMED precedence
            // applies: >= 1 wins verbatim over the table. 60000 is the real
            // value Sec34.2's own cross-reference (spec-zone-data-format.md
            // Sec11.7) records for Killbane's two full-fight copies.
            es.applyScriptNpcHitPointsForTesting("scripted_boss", 60000);
            auto rboss = host.runChunk(gp, "assert(get_max_hit_points('scripted_boss') == 60000.0)", "hp_scriptnpc1.lua");
            CHECK(rboss.loadOk && rboss.pcallOk);
            CHECK(es.getOrCreateCharacter("scripted_boss").maxHitPoints.get() == 60000);

            // Sec34.2's own text: a KNOWN value < 1 is the script NPC
            // constructor's own -1 "use the table" default and falls through
            // to the ordinary draw, same as a non-scriptNpcBound character -
            // not left OPEN merely because scriptNpcBound is true.
            es.applyScriptNpcHitPointsForTesting("scripted_unset", -1);
            auto runset = host.runChunk(gp, "assert(get_max_hit_points('scripted_unset') == 100.0)",
                                        "hp_scriptnpc2.lua"); // CHOSEN fallback (no spawn-rank table installed in this test)
            CHECK(runset.loadOk && runset.pcallOk);
            CHECK(es.getOrCreateCharacter("scripted_unset").maxHitPoints.get() == 100);

            // Still genuinely OPEN when no override value was ever supplied
            // at all - the only state any REAL production path in this
            // project reaches today, since no by-name zone-record resolver
            // exists (Sec34.4's own "cannot be determined from code alone").
            // Not a regression: same refusal 'maybe_scripted' above already
            // demonstrates, just restated for the new scriptNpcHp field.
            es.markScriptNpcBoundForTesting("scripted_unknown");
            refusesOpen(gp, "get_max_hit_points('scripted_unknown')", "max hit points");
        }

        // 12. ai_clear_scripted_action (Sec7.24): the confirmed resolve
        // step happens (touches the registry entry into existence) and
        // the call itself never errors, without inventing what internal
        // field 0x004f4c90 actually clears (OPEN, not even HIGH
        // CONFIDENCE, per the spec's own text).
        {
            CHECK(!es.hasCharacter("#PLAYER#"));
            auto r = host.runChunk(gp, "ai_clear_scripted_action('#PLAYER#')", "clearaction1.lua");
            CHECK(r.loadOk && r.pcallOk);
            CHECK(es.hasCharacter("#PLAYER#"));
        }

        // 13. vint_object_find (spec-lua-bindings.md Sec13.7/Sec15) - the
        // 43%-of-all-errors fix. CONFIRMED: always exactly 1 number,
        // 0.0 on every failure path, never nil. This project's registry
        // is never populated by any Lua-visible call in this pass's scope
        // (see engine_state.h's own VdoObject doc comment), so a bare
        // "not found" call is itself the real, honest, CONFIRMED
        // behavior for a script's very first call - exercised first,
        // then the full parent/document-scoped mechanism is exercised
        // directly via EngineState::registerVdoObjectForTesting().
        {
            auto r = host.runChunk(ui,
                "local h = vint_object_find('never_registered_widget')\n"
                "assert(type(h) == 'number' and h == 0.0)",
                "vintfind1.lua");
            CHECK(r.loadOk && r.pcallOk);

            // Document-wide find (no parent/doc args -> the current default
            // document). With objects registered, the answer depends on that
            // document, which is OPEN state until set.
            uint32_t rootHandle = es.registerVdoObjectForTesting("root_widget", /*parentHandle=*/0, /*docHandle=*/0);
            CHECK(rootHandle != 0);
            refusesOpen(ui, "vint_object_find('root_widget')", "current default vint document");
            es.currentDefaultDocHandle().set(0);
            auto r2 = host.runChunk(ui,
                "local h = vint_object_find('root_widget')\n"
                "assert(h == " + std::to_string(rootHandle) + ")",
                "vintfind2.lua");
            CHECK(r2.loadOk && r2.pcallOk);

            // Parent-scoped find: a child registered under rootHandle is
            // found when searching WITH that parent handle, but NOT found
            // when searching a document that doesn't contain it as a root
            // (parent absent, falls back to the default document, which
            // does not have this child as a direct entry keyed to it).
            uint32_t childHandle = es.registerVdoObjectForTesting("child_widget", rootHandle, /*docHandle=*/0);
            CHECK(childHandle != 0);
            auto r3 = host.runChunk(ui,
                "local h = vint_object_find('child_widget', " + std::to_string(rootHandle) + ")\n"
                "assert(h == " + std::to_string(childHandle) + ")",
                "vintfind3.lua");
            CHECK(r3.loadOk && r3.pcallOk);

            // A given-but-unresolvable (bad/stale) parent handle falls
            // back to a document-wide search (CONFIRMED per Sec15's own
            // "or document-wide if no parent was resolved" text) - finds
            // root_widget (a doc-0 root) even though the supplied parent
            // handle (999999) does not exist.
            auto r4 = host.runChunk(ui,
                "local h = vint_object_find('root_widget', 999999)\n"
                "assert(h == " + std::to_string(rootHandle) + ")",
                "vintfind4.lua");
            CHECK(r4.loadOk && r4.pcallOk);

            // A real 3-argument call (name, parent_handle, doc_handle) -
            // doc_handle is only consulted when no parent resolves; here
            // the parent DOES resolve, so doc_handle is irrelevant to the
            // outcome (still finds the child).
            auto r5 = host.runChunk(ui,
                "local h = vint_object_find('child_widget', " + std::to_string(rootHandle) + ", 12345)\n"
                "assert(h == " + std::to_string(childHandle) + ")",
                "vintfind5.lua");
            CHECK(r5.loadOk && r5.pcallOk);
        }

        // 14. store_vehicle_get_state (Sec10.1, Sec26.28): the flag is 0 at
        // load (CONFIRMED) -> 0.0; a real number, not a boolean.
        {
            auto r = host.runChunk(ui,
                "local v = store_vehicle_get_state()\n"
                "assert(type(v) == 'number' and v == 0.0)",
                "vehiclestore1.lua");
            CHECK(r.loadOk && r.pcallOk);
            es.vehicleStoreActive().set(true);
            auto r2 = host.runChunk(ui, "assert(store_vehicle_get_state() == 1.0)", "vehiclestore2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
            es.vehicleStoreActive().set(false);
        }

        // 15. Completion_is_client (Sec10.2) and game_get_is_host (Sec8.27):
        // no session (CONFIRMED start-up, and CONFIRMED - not merely
        // default - for single player as of job "fvzk" 2026-10-01: the
        // session installer is proven statically unreachable there) -> both
        // false; the "with a session" branch below stays a real test of the
        // function's own logic (host pair +0x5c == +0x58, OPEN until set)
        // even though it no longer models anything single player can reach.
        {
            auto ok = [&](const char* chunk) {
                auto r = host.runChunk(ui, chunk, "host.lua");
                CHECK(r.loadOk && r.pcallOk);
                if (!r.pcallOk) std::cerr << chunk << ": " << r.pcallError << "\n";
            };
            ok("assert(Completion_is_client() == false)");
            ok("assert(game_get_is_host() == false)");
            ok("assert(select('#', game_get_is_host()) == 1)");
            auto r0 = host.runChunk(gp, "assert(game_get_is_host == nil)", "host_gp.lua"); // `ui` only
            CHECK(r0.loadOk && r0.pcallOk);
            es.coopSession().present.set(true);
            refusesOpen(ui, "Completion_is_client()", "host check");
            refusesOpen(ui, "game_get_is_host()", "host check");
            es.coopSession().localIsHost.set(true);
            ok("assert(Completion_is_client() == false and game_get_is_host() == true)");
            es.coopSession().localIsHost.set(false);
            ok("assert(Completion_is_client() == true and game_get_is_host() == false)");
            es.coopSession().present.set(false);
            es.coopSession().localIsHost.forget();
            ok("assert(Completion_is_client() == false and game_get_is_host() == false)");
        }

        // 16. game_hud_update_inventory (Sec10.3): CLOSED 2026-10-09 -
        // hasLocalPlayer now has a CONFIRMED default of true (pre-seeded
        // by applySpecInitialState), not OPEN; force it OPEN here to
        // still exercise the refusal path, then true -> triggers a
        // refresh (counter increments); false -> no-op. No Lua return
        // value.
        {
            CHECK(es.hudInventoryRefreshCount() == 0);
            es.hasLocalPlayer().forget();
            refusesOpen(ui, "game_hud_update_inventory()", "local player");
            CHECK(es.hudInventoryRefreshCount() == 0);
            es.hasLocalPlayer().set(true);
            auto r = host.runChunk(ui, "game_hud_update_inventory()", "hudinv1.lua");
            CHECK(r.loadOk && r.pcallOk);
            CHECK(es.hudInventoryRefreshCount() == 1);
            es.hasLocalPlayer().set(false);
            auto r2 = host.runChunk(ui, "game_hud_update_inventory()", "hudinv2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
            CHECK(es.hudInventoryRefreshCount() == 1); // unchanged - no local player
            es.hasLocalPlayer().set(true);
        }

        // 17. tutorial_advance (Sec10.4, Sec6.19, Sec26.28): the 210-name
        // resolver (case-insensitive, first match), then entry state == 3.
        // At start entries 0-188 are in state 0 and 189-209 in state 1, so
        // every name is false. The state is not changed by the call.
        {
            auto ok = [&](const char* chunk) {
                auto r = host.runChunk(gp, chunk, "tut.lua");
                CHECK(r.loadOk && r.pcallOk);
                if (!r.pcallOk) std::cerr << chunk << ": " << r.pcallError << "\n";
            };
            CHECK(std::string(EngineState::tutorialName(0)) == "save");
            CHECK(std::string(EngineState::tutorialName(1)) == "autosave");
            CHECK(std::string(EngineState::tutorialName(122)) == "vehicle entry");
            CHECK(std::string(EngineState::tutorialName(189)) == "dlc1_act_genki_escort_into");
            CHECK(std::string(EngineState::tutorialName(209)) == "dlc3_win");
            // UPDATE 2026-10-08 (Sec6.19 full re-transcription, commit 3816870):
            // index 176 is "dlc" - no longer nullptr/OPEN (see
            // lua_tutorial_names.cpp's own top comment for the full account).
            CHECK(std::string(EngineState::tutorialName(176)) == "dlc" && EngineState::tutorialName(210) == nullptr);
            int named = 0;
            for (int i = 0; i < EngineState::kTutorialEntryCount; ++i) named += EngineState::tutorialName(i) ? 1 : 0;
            CHECK(named == 210); // all 210 now named (was 209 before the 176 fix above)
            CHECK(EngineState::tutorialLookup("AutoSave").index == 1);
            CHECK(EngineState::tutorialLookup("escort_minigame").index == 36); // also at 129: first wins
            CHECK(EngineState::tutorialLookup("TUT_SIXAXIS_BOAT").index == 185);
            CHECK(EngineState::tutorialLookup("hint_grab_weapon").index == -1);
            CHECK(EngineState::tutorialLookup("dlc").index == 176 && !EngineState::tutorialLookup("dlc").couldBeIndex176); // the fixed entry itself resolves directly now
            CHECK(!EngineState::tutorialLookup("hint_grab_weapon").couldBeIndex176);
            // NOTE: couldBeIndex176's own heuristic (lua_engine_state.cpp) is
            // UNCHANGED by the 176 fix above - it still flags any short/non-
            // ASCII unmatched name this way, even though entry 176's identity
            // is no longer genuinely ambiguous (see lua_tutorial_names.cpp's
            // own top comment) - a harmless, now-vestigial false positive
            // (tutorial_advance's own stub only acts on it when it would
            // actually change the answer - never the case now that 176 is
            // known, see stub_tutorial_advance's own body), not re-derived
            // here to avoid touching this project's established convention
            // for an unrelated native's (tutorial_advance) own behaviour.
            CHECK(EngineState::tutorialLookup("ab").couldBeIndex176);                // < 4 characters
            CHECK(EngineState::tutorialLookup("s\xe9ve_x").couldBeIndex176);         // non-ASCII
            CHECK(!EngineState::tutorialLookup("save").couldBeIndex176);              // matched before 176
            CHECK(es.tutorialState().get("0") == 0 && es.tutorialState().get("188") == 0);
            CHECK(es.tutorialState().get("189") == 1 && es.tutorialState().get("209") == 1);

            ok("assert(tutorial_advance('save') == false)");
            ok("assert(tutorial_advance('dlc3_win') == false)");               // state 1
            ok("assert(tutorial_advance('hint_grab_weapon') == false)");       // not in the table
            ok("assert(tutorial_advance('') == false)");                       // could be 176, state 1 != 3
            ok("assert(tutorial_advance() == false)");
            CHECK(es.tutorialAdvanceCount(0) == 0);
            es.tutorialState().set("0", 3);
            ok("assert(tutorial_advance('SAVE') == true)");
            ok("assert(select('#', tutorial_advance('save')) == 1)");
            CHECK(es.tutorialAdvanceCount(0) == 2);
            CHECK(es.tutorialState().get("0") == 3); // unchanged by tutorial_advance
            for (int st : {0, 1, 2, 4}) {
                es.tutorialState().set("0", st);
                ok("assert(tutorial_advance('save') == false)");
            }
            es.tutorialState().set("0", 0);
            es.tutorialState().set("129", 3);
            ok("assert(tutorial_advance('escort_minigame') == false)"); // resolves to 36
            es.tutorialState().set("36", 3);
            ok("assert(tutorial_advance('escort_minigame') == true)");
            es.tutorialState().set("36", 0);
            es.tutorialState().set("129", 0);
            // Entry 176's name is OPEN: refused only when the answer depends on it.
            es.tutorialState().set("176", 3);
            refusesOpen(gp, "tutorial_advance('ab')", "entry 176");
            refusesOpen(gp, "tutorial_advance('s\xe9ve_x')", "entry 176");
            ok("assert(tutorial_advance('hint_grab_weapon') == false)");     // 4+ ASCII: cannot be 176
            es.tutorialState().set("176", 1);
            ok("assert(tutorial_advance('ab') == false)");
        }

        // 18. minimap_icon_add_do (Sec10.5): CONFIRMED nil-gated defaults
        // (arg3 "", arg4 0.0, arg5 3); resolved via getOrCreateCharacter,
        // recorded into that object's own minimapIcons vector - no
        // dispatch-branch modeling, no Lua return value.
        {
            auto r = host.runChunk(gp, "minimap_icon_add_do('waypoint_obj', 'ICON_MISSION')", "minimap1.lua"); // args 3-5 omitted -> defaults
            CHECK(r.loadOk && r.pcallOk);
            auto& obj = es.getOrCreateCharacter("waypoint_obj");
            CHECK(obj.minimapIcons.size() == 1);
            CHECK(obj.minimapIcons[0].iconType == "ICON_MISSION");
            CHECK(obj.minimapIcons[0].group.empty());
            CHECK(obj.minimapIcons[0].param4 == 0.0);
            CHECK(obj.minimapIcons[0].flag5 == 3);

            auto r2 = host.runChunk(gp, "minimap_icon_add_do('waypoint_obj', 'ICON_ALLY', 'group_a', 1.5, 7)", "minimap2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
            CHECK(obj.minimapIcons.size() == 2);
            CHECK(obj.minimapIcons[1].iconType == "ICON_ALLY");
            CHECK(obj.minimapIcons[1].group == "group_a");
            CHECK(obj.minimapIcons[1].param4 == 1.5);
            CHECK(obj.minimapIcons[1].flag5 == 7);
        }

        // 19. object_indicator_add_do (Sec10.6): args 2/3 CONFIRMED
        // mandatory with NO nil gate (real API: omitted -> 0 via
        // lua_tonumber's own nil-handling); args 4/5 CONFIRMED optional,
        // defaults 3/100.0; always returns true through this minimal
        // registry's own resolver (see this function's own doc comment,
        // lua_spec_confirmed_stubs.cpp, for why).
        {
            // Resolution of arg 1 is OPEN until set (the old always-true is gone).
            refusesOpen(gp, "object_indicator_add_do('indicator_obj', 2, 5)", "named-object resolution");
            es.objectResolves().set("unresolved_obj", false);
            auto r0 = host.runChunk(gp, "assert(object_indicator_add_do('unresolved_obj', 2, 5) == false)", "objind0.lua");
            CHECK(r0.loadOk && r0.pcallOk);
            CHECK(es.getOrCreateCharacter("unresolved_obj").objectIndicators.empty());
            es.objectResolves().set("indicator_obj", true);
            auto r = host.runChunk(gp,
                "local ok = object_indicator_add_do('indicator_obj', 2, 5)\n"
                "assert(ok == true)",
                "objind1.lua");
            CHECK(r.loadOk && r.pcallOk);
            auto& obj = es.getOrCreateCharacter("indicator_obj");
            CHECK(obj.objectIndicators.size() == 1);
            CHECK(obj.objectIndicators[0].arg2 == 2);
            CHECK(obj.objectIndicators[0].arg3 == 5);
            CHECK(obj.objectIndicators[0].arg4 == 3);   // default
            CHECK(obj.objectIndicators[0].arg5 == 100.0); // default

            auto r2 = host.runChunk(gp,
                "local ok2 = object_indicator_add_do('indicator_obj', 2, 5, 1, 50.0)\n"
                "assert(ok2 == true)",
                "objind2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
            CHECK(obj.objectIndicators.size() == 2);
            CHECK(obj.objectIndicators[1].arg4 == 1);
            CHECK(obj.objectIndicators[1].arg5 == 50.0);
        }

        // 20. on_qte_animation_trigger (Sec10.7): a SINGLE GLOBAL slot
        // (EngineState-level, not per-character) - non-empty sets it,
        // empty clears it. No Lua return value.
        {
            CHECK(es.qteAnimationTriggerCallback().empty());
            auto r = host.runChunk(gp, "on_qte_animation_trigger('my_qte_cb')", "qte1.lua");
            CHECK(r.loadOk && r.pcallOk);
            CHECK(es.qteAnimationTriggerCallback() == "my_qte_cb");
            auto r2 = host.runChunk(gp, "on_qte_animation_trigger('')", "qte2.lua"); // empty -> clears
            CHECK(r2.loadOk && r2.pcallOk);
            CHECK(es.qteAnimationTriggerCallback().empty());
        }

        // 21. on_revived (Sec10.8): the target character's own dedicated
        // onRevivedCallback field (slot "2" in the real engine's own
        // generic hook-slot mechanism) - non-empty sets it, empty clears
        // it. No Lua return value.
        {
            auto r = host.runChunk(gp, "on_revived('my_revive_cb', 'hero_npc')", "revived1.lua");
            CHECK(r.loadOk && r.pcallOk);
            CHECK(es.getOrCreateCharacter("hero_npc").onRevivedCallback == "my_revive_cb");
            auto r2 = host.runChunk(gp, "on_revived('', 'hero_npc')", "revived2.lua"); // empty -> clears
            CHECK(r2.loadOk && r2.pcallOk);
            CHECK(es.getOrCreateCharacter("hero_npc").onRevivedCallback.empty());
        }

        // 22. game_get_coop_join_type (Sec10.9): default 0; real number
        // return; EngineState::setCoopJoinTypeForTesting() is a C++-only
        // test entry point (no real setter in this task's 9-function
        // scope - the real writer is a different, out-of-scope function).
        {
            refusesOpen(ui, "game_get_coop_join_type()", "0x012f44fc");
            es.coopJoinType().set(0);
            auto r = host.runChunk(ui, "assert(game_get_coop_join_type() == 0)", "jointype1.lua");
            CHECK(r.loadOk && r.pcallOk);
            es.coopJoinType().set(2);
            auto r2 = host.runChunk(ui, "assert(game_get_coop_join_type() == 2)", "jointype2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
        }

        // 23. zscene_is_loaded (Sec14.23 corrected, Sec26.25) and zscene_prep
        // (Sec8.21, Sec26.25). Only the skip byte 0x0153b556 has a specced
        // start-up value (false, Sec26.25 Globals, 2026-10-01); every other
        // zscene first read is OPEN.
        {
            auto check = [&](const char* chunk, const char* tag) {
                auto r = host.runChunk(gp, chunk, tag);
                CHECK(r.loadOk && r.pcallOk);
                if (!r.pcallOk) std::cerr << chunk << ": " << r.pcallError << "\n";
            };
            auto refuses = [&](const char* chunk, const char* needle) {
                auto r = host.runChunk(gp, chunk, "zsref.lua");
                CHECK(r.loadOk && !r.pcallOk);
                CHECK(r.pcallError.find(needle) != std::string::npos);
                if (r.pcallError.find(needle) == std::string::npos) std::cerr << chunk << ": " << r.pcallError << "\n";
            };
            check("assert(type(zscene_is_loaded) == 'function' and type(zscene_prep) == 'function')", "zs0a.lua");
            {
                auto r = host.runChunk(ui, "assert(zscene_is_loaded == nil and zscene_prep == nil)", "zs0b.lua");
                CHECK(r.loadOk && r.pcallOk);
            }
            CHECK(es.zsceneSkipAllCutscenes().known() && es.zsceneSkipAllCutscenes().get() == false);
            refuses("zscene_is_loaded()", "0x0153b51c");          // no name: skip byte (false), then the load state
            refuses("zscene_is_loaded('Scene_A')", "0x00723d20"); // name: table / kind first
            refuses("zscene_is_loaded('Scene_A')", "scene_a");    // keyed lower-case
            CHECK(host.hitLog().hits().count("zscene_is_loaded:OPEN_STATE") == 1);
            // Not a kind-1 entry (or missing): true at once - no inversion.
            es.zsceneLoadable().set("scene_a", false);
            check("assert(zscene_is_loaded('SCENE_A') == true)", "zs1.lua");
            check("assert(select('#', zscene_is_loaded('scene_a')) == 1)", "zs1b.lua");
            // A kind-1 entry: skip byte next (false at start), then the current
            // entry - null at start (Sec26.25 Globals, RESOLVED 2026-10-02,
            // CONFIRMED), so not current -> false.
            es.zsceneLoadable().set("scene_b", true);
            CHECK(es.zsceneCurrent().known() && es.zsceneCurrent().get().empty());
            check("assert(zscene_is_loaded('scene_b') == false)", "zs1c.lua");
            es.zsceneSkipAllCutscenes().set(true);
            check("assert(zscene_is_loaded('scene_b') == true)", "zs2.lua");
            check("assert(zscene_is_loaded() == true)", "zs3.lua");
            es.zsceneSkipAllCutscenes().set(false);
            // An unknown current entry (as after 0x00722f10, whose value is
            // not specced) is refused, naming it.
            es.zsceneCurrent().forget();
            refuses("zscene_is_loaded('scene_b')", "0x0153b530");
            es.zsceneCurrent().set("");                             // no current scene
            check("assert(zscene_is_loaded('scene_b') == false)", "zs4.lua"); // not current -> false
            es.zsceneCurrent().set("scene_b");
            refuses("zscene_is_loaded('scene_b')", "0x0153b51c");
            refuses("zscene_is_loaded()", "0x0153b51c");
            for (int code : {0, 1, 3}) {
                es.zsceneStateCode().set(code);
                check("assert(zscene_is_loaded('scene_b') == false and zscene_is_loaded() == false)", "zs5.lua");
            }
            es.zsceneStateCode().set(2);                            // 2 = loaded
            check("assert(zscene_is_loaded('Scene_B') == true and zscene_is_loaded() == true)", "zs6.lua");
            check("assert(zscene_is_loaded(nil) == true and zscene_is_loaded(true) == true)", "zs7.lua"); // no name
            refuses("zscene_is_loaded(12)", "'12'");                // a number is a name
            es.zsceneLoadable().set("scene_c", true);
            check("assert(zscene_is_loaded('scene_c') == false)", "zs8.lua"); // loaded scene is another one

            // zscene_prep: gate, teardown, pending.
            check("assert(select('#', zscene_prep('scene_a')) == 0)", "zp1.lua"); // not kind 1: nothing
            CHECK(!es.zscenePending().known());
            refuses("zscene_prep('unknown_scene')", "unknown_scene");
            es.zsceneSkipAllCutscenes().set(true);
            check("zscene_prep('scene_c')", "zp2.lua");               // skip byte: refused
            CHECK(!es.zscenePending().known());
            es.zsceneSkipAllCutscenes().set(false);
            check("zscene_prep('scene_b')", "zp3.lua");               // already current: no change
            CHECK(!es.zscenePending().known() && es.zsceneStateCode().get() == 2);
            // Teardown of the current scene reads the cutscene guard (cutscene
            // state 0x0153b520 in 7..13 with a manager whose +8 == 1). Both
            // the cutscene state and its manager pointer are zero-fill .data
            // with no static initializer (spec-lua-api-behaviour.md Sec60.4,
            // 2026-10-07 closure of the mm_p_06 blocker) - CONFIRMED known at
            // construction (idle/0, no cutscene ever played), not OPEN - the
            // explicit guard-off re-check a few lines below already covers
            // the "reaches the handle class next" behaviour this enables.
            CHECK(es.cutsceneState().known() && es.cutsceneState().get() == 0);
            es.cutsceneState().set(8);
            es.cutsceneManager().present.set(true);
            es.cutsceneManager().field8.set(1);                       // under a cutscene: no teardown
            check("zscene_prep('SCENE_C')", "zp4.lua");
            CHECK(es.zscenePending().get() == "scene_c" && es.zsceneStateCode().get() == 2);
            es.zscenePending().forget();
            es.cutsceneState().set(0);                                // guard off
            refuses("zscene_prep('scene_c')", "handle class");
            CHECK(!es.zscenePending().known());
            es.zsceneHandleClass().set("scene_b", 1);                 // not live, c == 0: nothing
            check("zscene_prep('scene_c')", "zp5.lua");
            CHECK(es.zscenePending().get() == "scene_c" && es.zsceneStateCode().get() == 2);
            es.zsceneHandleClass().set("scene_b", 3);                 // live: state := 0
            check("zscene_prep('scene_c')", "zp6.lua");
            CHECK(es.zscenePending().get() == "scene_c" && es.zsceneStateCode().get() == 0);
            CHECK(es.zsceneCurrent().get() == "scene_b");            // current left as it was
            CHECK(!es.zsceneHandleClass().known("scene_b"));          // released handle's class: OPEN
            CHECK(es.zsceneAutoSelectNearest().get() == false && es.zsceneRequeueOnReset().get() == false);
            // The pending entry: false until the per-frame driver promotes it;
            // refused only while that driver is stopped on an OPEN read.
            // 2026-10-02 correction (spec-lua-api-behaviour.md Sec26.25's
            // corrected Host summary, the `mm_p_01` zscene-promotion-driver
            // investigation): completion and promotion each run every frame
            // independently of the cutscene state, which is no longer what
            // stops this frame - both steps instead hit the 'scene_b' handle
            // class this test already released (OPEN since line ~1241 above)
            // while resetting the still-current, not-pending 'scene_b' entry.
            check("assert(zscene_is_loaded('scene_c') == false)", "zs8b.lua");
            es.cutsceneState().forget();
            es.cutsceneHostFrame();                                   // stops on 'scene_b' handle class
            CHECK(es.zscenePendingBlockedRefusals() == 0);
            refuses("zscene_is_loaded('scene_c')", "0x00dafb60");
            CHECK(es.zscenePendingBlockedRefusals() == 1);
            CHECK(!es.cutsceneState().known()); // still never claimed to be anything
            es.cutsceneState().set(0);
            check("assert(zscene_is_loaded('scene_b') == false)", "zs9.lua"); // current, state 0
            check("assert(zscene_is_loaded() == false)", "zs10.lua");
            // No current scene: prep tears nothing down.
            es.zsceneCurrent().set("");
            es.zsceneStateCode().set(1);
            check("zscene_prep('scene_b')", "zp7.lua");
            CHECK(es.zscenePending().get() == "scene_b" && es.zsceneStateCode().get() == 1);
            check("assert(pcall(zscene_is_loaded, 'unset_scene') == false)", "zs16.lua");
        }

        // 24-26. Cloud phase batch 1.
        {
            auto check = [&](const char* chunk, const char* tag) {
                auto r = host.runChunk(gp, chunk, tag);
                CHECK(r.loadOk && r.pcallOk);
            };
            // set_mission_author (Sec6.1): inert, 0 results, any arguments.
            check("assert(select('#', set_mission_author()) == 0)", "sma1.lua");
            check("assert(select('#', set_mission_author('x', 1, true)) == 0)", "sma2.lua");

            // fade_out (Sec2.9, Sec26.24). Before any call: no colour, no
            // requests, state 2 (start-up) with the init run at registration.
            CHECK(!es.hasScreenFadeColour());
            CHECK(es.screenFadeRequests().empty());
            CHECK(es.screenFade().state.get() == 2 && es.screenFade().flag.get() == 1);
            check("assert(select('#', fade_out(2.5)) == 0)", "fo1.lua");
            CHECK(es.hasScreenFadeColour());
            CHECK(es.screenFadeColour().r == 0.0f && es.screenFadeColour().g == 0.0f &&
                  es.screenFadeColour().b == 0.0f && es.screenFadeColour().a == 1.0f);
            CHECK(es.screenFadeRequests().size() == 1);          // default flags 3: bit 0x1
            CHECK(es.screenFadeRequests()[0].durationMs == 2500.0);
            CHECK(es.screenFadeRequests()[0].targetAlpha == 1.0f);
            CHECK(es.screenFade().state.get() == 1 && es.screenFade().target.get() == 3); // fading out
            CHECK(es.screenFade().flag.get() == 0);
            CHECK(es.screenFadeOpcode53Count() == 0);             // bit 0x2, but no session: host gate fails
            // Colour table, one nil component -> 0; flags 1 only.
            check("fade_out(0.5, {255, nil, 51}, 1)", "fo2.lua");
            CHECK(es.screenFadeColour().r == 1.0f && es.screenFadeColour().g == 0.0f &&
                  es.screenFadeColour().b == 51.0f / 255.0f);
            CHECK(es.screenFadeRequests().size() == 2 && es.screenFadeRequests()[1].durationMs == 500.0);
            CHECK(es.screenFadeOpcode53Count() == 0);
            // Flags 2 only: no request; the broadcast is host-gated (Sec26.24).
            check("fade_out(1, nil, 2)", "fo3.lua");
            CHECK(es.screenFadeRequests().size() == 2 && es.screenFadeOpcode53Count() == 0);
            es.coopSession().present.set(true);
            es.coopSession().localIsHost.set(true);
            check("fade_out(1, nil, 2)", "fo3b.lua");
            CHECK(es.screenFadeOpcode53Count() == 1 && es.screenFade().lastBroadcastWasOut.get());
            es.coopSession().localIsHost.set(false);               // client: no record
            check("fade_out(1, nil, 2)", "fo3c.lua");
            CHECK(es.screenFadeOpcode53Count() == 1);
            es.coopSession().localIsHost.forget();
            refusesOpen(gp, "fade_out(1, nil, 2)", "host check");   // gate read before any write
            es.coopSession().present.set(false);
            check("fade_out(1, nil, nil)", "fo4.lua");
            CHECK(es.screenFadeRequests().size() == 3 && es.screenFadeOpcode53Count() == 1);
            // Flags 0: colour still set, nothing requested.
            check("fade_out(1, {10, 20, 30}, 0)", "fo5.lua");
            CHECK(es.screenFadeColour().g == 20.0f / 255.0f);
            CHECK(es.screenFadeRequests().size() == 3 && es.screenFadeOpcode53Count() == 1);
            // trunc(seconds x 1000): 0.0015 s -> 1 ms; a non-finite duration is
            // refused (only when bit 0x1 needs it).
            check("fade_out(0.0015, nil, 1)", "fo5b.lua");
            CHECK(es.screenFadeRequests().back().durationMs == 1.0);
            {
                auto r = host.runChunk(gp, "fade_out(1/0, nil, 1)", "fo5c.lua");
                CHECK(r.loadOk && !r.pcallOk && r.pcallError.find("fade duration") != std::string::npos);
            }
            check("fade_out(0/0, nil, 0)", "fo5d.lua");
            // Absent duration reads as 0 (lua_tonumber, no gate).
            check("fade_out()", "fo6.lua");
            CHECK(es.screenFadeRequests().size() == 5 && es.screenFadeRequests()[4].durationMs == 0.0);
            // A non-table colour argument is indexed like the real call: Lua error.
            {
                size_t before = es.screenFadeRequests().size();
                auto r = host.runChunk(gp, "fade_out(1, 5)", "fo7.lua");
                CHECK(r.loadOk && !r.pcallOk);
                CHECK(r.pcallError.find("attempt to index") != std::string::npos);
                CHECK(es.screenFadeRequests().size() == before); // nothing applied
            }
            // lua_gettable semantics kept (it now runs under fade_out's own
            // lua_pcall, fix for bridge job jklk): __index metamethods are
            // honoured, and an error inside one is an ordinary Lua error.
            check("fade_out(1, setmetatable({}, {__index = function(t, k) return k * 10 end}), 0)", "fo8.lua");
            // Out-of-range / non-finite numbers through the 0x00ea2596 conversion
            // (fade_out's flags argument) are refused as OPEN, not cast (UB).
            {
                auto r = host.runChunk(gp, "fade_out(1, nil, 1e300)", "fo10.lua");
                CHECK(r.loadOk && !r.pcallOk && r.pcallError.find("0x00ea2596") != std::string::npos);
                auto r2 = host.runChunk(gp, "fade_out(1, nil, 0/0)", "fo11.lua");
                CHECK(r2.loadOk && !r2.pcallOk && r2.pcallError.find("0x00ea2596") != std::string::npos);
            }
            CHECK(es.screenFadeColour().r == 10.0f / 255.0f && es.screenFadeColour().b == 30.0f / 255.0f);
            {
                auto r = host.runChunk(gp, "fade_out(1, setmetatable({}, {__index = function() error('boom') end}), 0)", "fo9.lua");
                CHECK(r.loadOk && !r.pcallOk);
                CHECK(r.pcallError.find("boom") != std::string::npos);
            }
            // Refusals raised repeatedly through openGuard, with a heap-allocated
            // (long) scene name, keep their exact message (jklk regression).
            {
                auto r = host.runChunk(gp,
                    // No `string` library in a host state (spec-lua-bindings.md
                    // Sec16.4): every message must equal the first, whose prefix
                    // is checked from C++ below.
                    "for i = 1, 200 do\n"
                    "  local ok, m = pcall(zscene_is_loaded, 'a_scene_name_well_past_the_small_string_buffer')\n"
                    "  assert(not ok and type(m) == 'string')\n"
                    "  GUARD_MSG = GUARD_MSG or m\n"
                    "  assert(m == GUARD_MSG)\n"
                    "  ok = pcall(fade_out, 1, 5)\n"
                    "  assert(not ok)\n"
                    "end", "guard_loop.lua");
                CHECK(r.loadOk && r.pcallOk);
                if (!r.pcallOk) std::cerr << r.pcallError << "\n";
                CHECK(globalString(gp, "GUARD_MSG").rfind("zscene_is_loaded: engine state ", 0) == 0);
            }

            // mission_end_silently (Sec15.23): bit 0x4 always; 0x10 mirrors
            // arg. The word starts fully OPEN; only those two bits become known.
            CHECK(es.missionFlagsWord().knownMask() == 0);
            check("mission_end_silently()", "mes1.lua");
            CHECK(es.missionFlagsWord().knownMask() == 0x14);
            CHECK(es.missionFlagsWord().get(0x14) == 0x4);
            check("mission_end_silently(true)", "mes2.lua");
            CHECK(es.missionFlagsWord().get(0x14) == 0x14);
            check("mission_end_silently(nil)", "mes3.lua");
            CHECK(es.missionFlagsWord().get(0x14) == 0x4);
            check("assert(select('#', mission_end_silently(1)) == 0)", "mes5.lua"); // 1 is truthy
            CHECK(es.missionFlagsWord().get(0x10) == 0x10);
            {
                bool refused = false;
                try {
                    (void)es.missionFlagsWord().get(0x1);
                } catch (const sr3luahost::OpenStateError& e) {
                    refused = std::string(e.what()).find("0x014c848c") != std::string::npos;
                }
                CHECK(refused); // bits no confirmed writer touched stay OPEN
            }
            // Wrong state: gameplay-tagged only.
            auto r = host.runChunk(ui, "assert(fade_out == nil and mission_end_silently == nil)", "b1ui.lua");
            CHECK(r.loadOk && r.pcallOk);
        }

        // 27. sfx_faded_out / sfx_faded_in (UI) and fade_is_fully_faded_out /
        // fade_is_fully_faded_in (gameplay) (Sec26.9, Sec26.24): state == 3 /
        // state == 2, no arguments, one boolean.
        {
            auto both = [&](lua_State* st, const char* chunk) {
                auto r = host.runChunk(st, chunk, "fq.lua");
                CHECK(r.loadOk && r.pcallOk);
                if (!r.pcallOk) std::cerr << chunk << ": " << r.pcallError << "\n";
            };
            for (uint32_t st : {0u, 1u, 2u, 3u}) {
                es.screenFade().state.set(st);
                const std::string out = st == 3 ? "true" : "false";
                const std::string in = st == 2 ? "true" : "false";
                both(ui, ("assert(sfx_faded_out() == " + out + " and sfx_faded_in() == " + in + ")").c_str());
                both(gp, ("assert(fade_is_fully_faded_out() == " + out + " and fade_is_fully_faded_in() == " + in +
                          ")").c_str());
            }
            both(ui, "assert(select('#', sfx_faded_out()) == 1)");
            both(gp, "assert(sfx_faded_out == nil and sfx_faded_in == nil and Screen_fade_transition_complete == nil)");
            both(ui, "assert(fade_is_fully_faded_out == nil and fade_in == nil)");
            es.screenFade().state.forget();
            refusesOpen(ui, "sfx_faded_out()", "0x012e6aa4");
            refusesOpen(gp, "fade_is_fully_faded_in()", "0x012e6aa4");
            auto rl = host.runChunk(ui,
                "for i = 1, 200 do local ok, m = pcall(sfx_faded_out)\n"
                "  assert(not ok and type(m) == 'string'); SFO_MSG = SFO_MSG or m\n"
                "  assert(m == SFO_MSG) end", "sfo_loop.lua");
            CHECK(rl.loadOk && rl.pcallOk);
            CHECK(globalString(ui, "SFO_MSG").rfind("sfx_faded_out: engine state ", 0) == 0);
            es.screenFade().state.set(2);
        }

        // Every one of the 22 calls above must fold into the SAME HitLog
        // ranking every other stub uses (stub_registry.h/thread_scheduler.h).
        for (const char* name : {"coop_is_active", "game_get_key_name", "game_UI_audio_play",
                                  "game_audio_get_audio_id", "game_get_key_name_for_action",
                                  "game_peg_load_with_cb", "ai_add_enemy_target", "on_take_damage",
                                  "set_ignore_ai_flag", "get_max_hit_points", "set_current_hit_points",
                                  "ai_clear_scripted_action", "vint_object_find",
                                  "store_vehicle_get_state", "Completion_is_client",
                                  "game_hud_update_inventory", "tutorial_advance",
                                  "minimap_icon_add_do", "object_indicator_add_do",
                                  "on_qte_animation_trigger", "on_revived", "game_get_coop_join_type",
                                  "zscene_is_loaded", "set_mission_author", "fade_out",
                                  "mission_end_silently", "sfx_faded_out", "game_get_is_host",
                                  "zscene_prep", "sfx_faded_in", "fade_is_fully_faded_out",
                                  "fade_is_fully_faded_in"}) {
            CHECK(host.hitLog().hits().count(name) == 1);
            CHECK(host.hitLog().hits().at(name).callCount >= 1);
        }
    }

