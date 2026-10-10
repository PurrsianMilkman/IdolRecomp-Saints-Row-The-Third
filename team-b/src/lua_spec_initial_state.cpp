// Initial engine-state values from Team A's specs (manager prep 2026-10-01).
//
// Host's constructor calls applySpecInitialState() once, before any script
// runs. Each area below is where a confirmed answer goes when its spec is
// synced: set the slot (OpenValue::set / OpenValueMap::set / OpenBits32::setBits)
// with the spec section in a comment, flip the matching expectation in
// tests/synthetic_luahost_test.cpp (the open-slot inventory checks), and send
// bridge-jobs/02f_mission_drive_after_answer.json. lua_host_run's
// verdict_open_state.tsv then shows the slot as known.
//
// Only CONFIRMED values go here; HYPOTHESIS/OPEN stay unset.
#include "sr3luahost/engine_state.h"

namespace sr3luahost {

void applySpecInitialState(EngineState& es) {
    // 1. co-op session (spec-lua-api-behaviour.md Sec26.28, CONFIRMED -
    //    disassembly): the singleton 0x024d8534 is zero at load and no
    //    resolved code installs a session; "a host starts with no session".
    //    So coop_is_active, game_get_is_host and Completion_is_client all
    //    answer false. The per-session fields are read only with a session
    //    and stay OPEN, as does the co-op join type 0x012f44fc (Sec10.9) and
    //    whether single player installs a one-member host session (Sec26.28).
    es.coopSession().present.set(false);

    // 2. tutorial table 0x0151d600 (Sec6.19, Sec10.4, Sec26.28, CONFIRMED -
    //    disassembly): zero at load; the registration routine 0x007178c0 fills
    //    it in one pass, entries 0-188 in state 0 and 189-209 in state 1. These
    //    are the values after that fill. When the tutorial initialiser
    //    0x00715f20 runs is OPEN; before it every entry is 0. Either way no
    //    entry is in state 3, so tutorial_advance is false for every name on a
    //    fresh process (Sec10.4). Entry 176 is set too: its STATE is
    //    confirmed by the fill; only its name is OPEN.
    for (int i = 0; i < EngineState::kTutorialEntryCount; ++i) {
        es.tutorialState().set(EngineState::tutorialStateKey(i), i <= 188 ? 0 : 1);
    }

    // 3. vehicle-store flag 0x022cdf08 (Sec10.1, Sec26.28, CONFIRMED -
    //    disassembly): 0 at load, so store_vehicle_get_state returns 0.0.
    es.vehicleStoreActive().set(false);

    // 4. zscene and the cutscene machine (Sec26.25, Sec14.23, Sec8.21).
    //    The skip_all_cutscenes byte 0x0153b556 is false at load (Sec26.25
    //    Globals table, 2026-10-01 re-derivation, CONFIRMED - disassembly):
    //    it is zero-fill .data with no static initializer; its one direct
    //    code-write (0x0072e04d in 0x0072df50's opcode-0 case) only clears it;
    //    the registration 0x0086d770("skip_all_cutscenes", &0x0153b556, 1,
    //    isHost, 0) writes no default (1 is the size in bytes) and its host
    //    copy-in never fires (no second registration of the name). Only the
    //    by-name console/config/command-line path can set it, which no
    //    mission or Lua code reaches.
    es.zsceneSkipAllCutscenes().set(false);
    //    The current scene entry 0x0153b530 is null ("no current scene
    //    entry") at load (Sec26.25 Globals table, "Initial value RESOLVED
    //    2026-10-02", CONFIRMED - disassembly): zero-fill .data with no static
    //    initializer, the same mechanism as 0x0153b556 above; an exhaustive
    //    whole-binary write census finds exactly 6 writes, all inside the
    //    functions that row's own "set/cleared by" column names (0x00720320,
    //    0x00721c20, 0x00720410, 0x00722f10, 0x007231e0, 0x00728440) - no
    //    module-init site, no by-name/cvar registration. "" is this slot's
    //    null (engine_state.h).
    es.zsceneCurrent().set("");
    //    The cutscene state 0x0153b520 is 0 ("idle") at load (Sec26.25 item 6,
    //    "the five cases of 0x007258a0: 0 idle", and Sec60.4's 2026-10-07
    //    closure, relayed via the orchestrator as the mm_p_06 blocker fix,
    //    probe_blockers_ranked_20261007.md item 6: "both the state and its
    //    manager pointer are zero-fill with no static initializer, so with no
    //    cutscene ever played the guard is simply false and needs no manager
    //    value") - the SAME zero-fill-.data mechanism as 0x0153b530/0x0153b556
    //    above, CONFIRMED. Team A's own text corrects an earlier assumption
    //    that the guard read 0x0153b530 (already closed separately, a
    //    different global) rather than 0x0153b520. Only the state needs a
    //    value here - the cutscene manager (*0x0153b528) stays OPEN below,
    //    exactly as Sec60.4 says it must ("needs no manager value"): with the
    //    state known and outside 7..13, EngineState::zsceneCutsceneGuard()'s
    //    own AND short-circuits false without ever reading the manager (see
    //    its doc comment, lua_cutscene.cpp).
    es.screenFade().cutsceneState.set(0);
    //    The re-queue-on-reset byte 0x0153b542 is permanently zero in
    //    practice (Sec26.25 Globals table, 2026-10-02 correction, CONFIRMED -
    //    disassembly): its only writer is the teardown 0x00721c20 from its
    //    own second argument, and all seven of its call sites in the binary
    //    pass 0 - the "re-queue the current entry on reset" branch this
    //    document describes never actually fires through any Lua/mission-
    //    reachable path. This is a fact about the byte's value in EVERY run,
    //    not merely a start-up guess, so it is set here rather than left for
    //    a test to supply per-scenario.
    es.zsceneRequeueOnReset().set(false);
    //    The nearest scene-bearing world object (list 0x03171a64) the idle
    //    driver 0x00728440 would auto-prep (Sec26.25 Host summary, "idle
    //    auto-select"): this host has no world-object manager anywhere
    //    (checked directly - no such subsystem exists in this codebase), so
    //    the real list is unconditionally empty here, not merely "not yet
    //    populated" - "" ("none found") is this field's own CONFIRMED (by
    //    this host's own construction) value, the same reasoning this
    //    project already applies to the co-op session (item 1 above).
    //    2026-10-09, zscene_prep / mm_p_06 + mm_p_01 blocker fix: closing
    //    this alongside the handle-class default below (zscenePromotionStep,
    //    lua_cutscene.cpp) is what lets a bare zscene_prep with no streaming
    //    backend settle into a clean, non-throwing steady state instead of
    //    refusing - see that function's own doc comment (engine_state.h).
    es.zsceneNearestWorldObjectScene().set("");
    //    Everything else here still has no specced start-up value and stays
    //    OPEN: the pending entry, the load state 0x0153b51c, 0x0153b541, the
    //    soundtrack stream globals, and the cutscene manager (*0x0153b528).
    //    The scene table itself is real data (cutscene.xtbl + <name>.cte_xtbl);
    //    lua_host_run installs it from the archive cache
    //    (EngineState::installZsceneTable), not this function. The handle
    //    class 0x00dafb60 (per scene entry) is NOT defaulted here either - see
    //    zscenePromotionStep()'s own doc comment (engine_state.h): it gets a
    //    real default, narrowly, at the one event (a fresh promotion) that
    //    would produce it, not at construction.

    // 5. screen fade (Sec26.24, CONFIRMED - disassembly, "Globals" table: the
    //    file-backed values the executable starts with). The init 0x0059fa30
    //    later writes state 2 / target 2 / flag 1 and the document id; the
    //    host runs it when it registers Screen_fade_transition_complete in the
    //    UI state (lua_spec_confirmed_stubs.cpp). The completion callbacks
    //    0x013effcc / 0x013effd0 are 0 (EngineState::ScreenFade defaults).
    //    The mode-stack top and the cutscene state 0x0153b520 that the
    //    per-frame routine reads stay OPEN.
    EngineState::ScreenFade& f = es.screenFade();
    f.state.set(2);                // 0x012e6aa4: fully faded in, the start-up state
    f.target.set(2);               // 0x012e6aa8
    f.flag.set(0);                 // 0x013effc8 (init writes 1)
    f.documentLoaded.set(false);   // 0x012e6aa0 = -1
    f.logoAt.set(-1);              // 0x012e6aac
    f.holdLogoUntil.set(-1);       // 0x012e6ab0
    f.imagesAt.set(-1);            // 0x012e6ab4
    f.holdImagesUntil.set(-1);     // 0x012e6ab8
    f.autoSaveStamp.set(-1);       // 0x012e6abc
    f.autoSaveCounter.set(0);      // 0x013effd4
    // 0x0149365c: 0 in the file, set to 1 unconditionally by the engine
    // start-up 0x005d1a30 and never cleared (Sec26.24 Globals, job nnlt,
    // CONFIRMED) - before any script, so scripts always see 1.
    f.useLoadImages.set(true);
    f.lastBroadcastWasOut.set(false); // 0x013effc5

    // 6. UI resolution (Sec26.26, CONFIRMED - disassembly, job nnlt): the two
    //    safe-frame constants, read as two dwords each: 0x0115ba60 =
    //    0x3FB3333340000000 (0.075f widened to double) and 0x0116dfc0 =
    //    0x3FED9999A0000000 (0.925f widened) - NOT the decimals 0.075 / 0.925,
    //    which tie at dimensions = 20 (mod 40). The display mode 0x0132bd80 is
    //    -1 in the file, but the UI subsystem init 0x00e23910 recomputes it
    //    before any script runs, from the display resolution - a host input
    //    (EngineState::vintUiSubsystemInit, lua_host_run --display); until then
    //    the mode, the width/height globals and the per-thread record stay OPEN.
    //    The safe-frame source object's +0x8 / +0xc have no specced writer.
    es.vintSafeFrameScale1().set(EngineState::doubleFromBits(EngineState::kSafeFrameScale1Bits));
    es.vintSafeFrameScale2().set(EngineState::doubleFromBits(EngineState::kSafeFrameScale2Bits));

    // 7. Batch 2026-10-01, spec-lua-api-behaviour.md Sec27/Sec28: the only two
    //    OpenValue slots this batch's own spec text gives a CONFIRMED file
    //    value for (every other new OpenValue/OpenValueMap stays OPEN - see
    //    engine_state.h's own per-field doc comments; plain, non-OpenValue
    //    fields with a CONFIRMED initial value use a default member
    //    initializer instead, so they need no entry here).
    es.coopFriendlyFireRaw().set(1);       // 0x012f4500 (Sec27.15/Sec27.16)
    es.autosave().suppressFlag.set(true);  // 0x012fcadc (Sec27.12)

    // 8. named-object resolution (Sec29, 2026-10-02, CONFIRMED - disassembly,
    //    write side of the shared 0x02442750 by-name resolver family). No
    //    Lua-bound function can ever choose a name for an object it creates
    //    (Sec29.4); engine code separately registers a small CLOSED set of
    //    literal names into the shared 2,999-slot name map regardless of any
    //    zone/data file: "homies", "shopkeepers", "-- Cutscene Script Group
    //    --", and the special-cased "#PLAYER1#"/"#PLAYER2#" pair. These five
    //    are the full extent of what Sec29 confirms unconditionally - every
    //    other name (including a mission character's own fixed name, e.g.
    //    'Killbane') traces back to the still-parked .czn_pc zone-placement
    //    interior (Sec29.2 producer 1) or to 173 in-memory call sites never
    //    individually traced (producer 3, could register essentially any
    //    name) - so it correctly stays OPEN; do not widen this set without
    //    new CONFIRMED spec text (see objectResolves()'s own doc comment,
    //    engine_state.h).
    es.objectResolves().set("homies", true);
    es.objectResolves().set("shopkeepers", true);
    es.objectResolves().set("-- Cutscene Script Group --", true);
    es.objectResolves().set("#PLAYER1#", true);
    es.objectResolves().set("#PLAYER2#", true);

    // Sec3.3's own 2026-10-07 refutation (on_vehicle_destroyed) establishes
    // that the generic/#PLAYER# chain's class-bit gate (0x00a27e20,
    // EngineState::playerClassChainResolves()) is reachable ONLY via the
    // literal "#PLAYER#" sentinel forms - i.e. exactly the two names Sec29.4
    // just above already established as the real, closed-set player
    // objects ("#PLAYER1#"/"#PLAYER2#", pre-populated true in
    // objectResolves() for the identical reason). This is the same
    // CONFIRMED fact, not a new guess: whatever carries the player-class
    // descriptor bit the generic chain tests for is, by the project's own
    // existing "#PLAYER1#"/"#PLAYER2#" convention, these same two names.
    // Measured regression this pass (fresh real mission-drive run, before
    // this fix: missions_with_start_call_ok dropped from 47/49 to 29/49 -
    // on_vehicle_destroyed's own `_start`-time hook registration against
    // "#PLAYER1#" was hard-refusing on a never-before-populated map,
    // where objectResolves() already answered this exact question) - fixed
    // by applying the identical pre-population here instead of leaving a
    // second, narrower map OPEN for a fact the project already knows.
    es.playerClassChainResolves().set("#PLAYER1#", true);
    es.playerClassChainResolves().set("#PLAYER2#", true);

    // 9. player gender initial state (spec-lua-api-behaviour.md Sec16.8/
    //    Sec16.8a, spec-save-format.md Sec10.4, spec-tables-customization.md
    //    Sec16.2, 2026-10-02, CONFIRMED - disassembly + empirical). A freshly
    //    constructed character object (0x00954340) holds the engine's own
    //    "unspecified" sentinel pair, +0xa41 (gender) := 3, +0xa40 (race) :=
    //    5 - but Sec16.8's own producer census shows no script ever observes
    //    that raw pair in practice: by the time any Lua code can query the
    //    player, game-start step 0x00708330 has already applied exactly one
    //    of (a) a loaded save's own character-record bytes
    //    (+0x203c/+0x203d, spec-save-format.md Sec10.4), (b) the
    //    player_presets.xtbl row marked Default, or (c) a co-op guest
    //    record. This host (tools/lua_host_run.cpp) never loads a real
    //    .sr3save/.sr3s_pc/.sr3d_pc file anywhere in the mission drive -
    //    checked directly, no reference to any save path in that file - so
    //    path (a) never applies here, and path (c) needs a co-op session
    //    which item 1 above confirms is absent at start; the applicable path
    //    is always (b), the fresh-game default-preset step (0x008373d0).
    //    The shipped data's one Default-marked player_presets.xtbl row is
    //    "male_white": Gender=0 (male), Race=3 (white) - the record's own
    //    +0x5d/+0x5c bytes (spec-tables-customization.md Sec16.2), using the
    //    vocabulary 0=male/1=female/3=unspecified-sentinel for Gender
    //    (0x00831fc0) and 0 asian/1 black/2 hispanic/3 white/
    //    5=unspecified-sentinel for Race (0x00832000), both confirmed against
    //    the shared string tables 0x01160028/0x01160030
    //    (spec-lua-api-behaviour.md Sec16.8).
    //    Only the gender byte has a modelled consumer in this host
    //    (characterGender(), feeding character_get_gender/Sec28.21/the
    //    player-rig accessor family of Sec28.12) - there is no
    //    race/ethnicity-reading stub in this codebase to extend, so only
    //    Gender is set here; Race stays unmodelled (not OPEN-by-omission,
    //    just genuinely unused by any stub yet).
    //    "#PLAYER1#" only: a second player's gender depends on the co-op
    //    guest-record path (c), which stays OPEN - no "#PLAYER2#" character
    //    object is even constructed while co-op is absent, so nothing here
    //    would resolve it, and inventing a value for it would be exactly the
    //    kind of guess this project's OpenValue discipline exists to refuse.
    es.characterGender().set("#PLAYER1#", 0); // male - the shipped Default preset row

    // 10. game clock initial state (spec-lua-api-behaviour.md Sec48.2,
    //     2026-10-03, CONFIRMED - disassembly + the orchestrator's own
    //     reconciliation of the single-player/host-session question).
    //     Engine boot sets 13 May 2004 10:17:33; the new-game routine
    //     (reached before any mission script runs) then rewrites ONLY the
    //     time to 10:00:00, keeping the date. The one other initial-state
    //     source, a world-start random-key snap, CONFIRMED never fires
    //     without a co-op session object - this host never models one
    //     (item 1 above) - and this host never loads a save (checked
    //     directly, no save path referenced anywhere in tools/
    //     lua_host_run.cpp), so the applicable path is always 10:00:00.
    //     This closes the dlc3_m01/m19/etc. blocker: set_time_of_day's own
    //     forward-delta arithmetic reads the CURRENT hour/minute first,
    //     which previously had no initial value to read.
    es.gameClock().hour.set(10);
    es.gameClock().minute.set(0);

    // 11. player control-lock handle 0x0263ae3c (spec-player-runtime.md
    //     Sec2.10, synced 2026-10-03, CONFIRMED): null (0) at session or
    //     mission start - zero at image load, and a session-start reset
    //     forces it to 0. The handle is otherwise self-balancing: its only
    //     creator is player_controls_disable's own direct-apply push, its
    //     only destroyer is player_controls_enable's own matching pop, so no
    //     other code path ever needs to re-seed it mid-run.
    es.playerControlsLockHandle().set(0);

    // 12. notoriety ALIAS-track resolver (spec-lua-api-behaviour.md
    //     Sec6.3/Sec6.22, 2026-10-08, CONFIRMED - disassembly, the first
    //     alias of each of the 3 parallel 4-entry .rdata arrays, read
    //     verbatim): "luchadores"/"brotherhood"/"los_carnales" -> codes
    //     0/1/2. The remaining 3 members of each 4-entry array are not
    //     given by spec and are NOT guessed at - a test can add further
    //     aliases via registerNotorietyAliasTrackCodeForTesting(); any
    //     other name keeps resolving to the real, CONFIRMED sentinel 5
    //     (kNotorietyAliasTrackNotFound), not an OPEN unknown.
    es.registerNotorietyAliasTrackCodeForTesting("luchadores", 0);
    es.registerNotorietyAliasTrackCodeForTesting("brotherhood", 1);
    es.registerNotorietyAliasTrackCodeForTesting("los_carnales", 2);

    // 13. second/co-op player presence, playerRig().coopPlayerPresent
    //     (spec-lua-api-behaviour.md Sec28.12, mechanism corrected Sec14.31/
    //     Sec32.8, 2026-10-09, CONFIRMED - disassembly, chained from facts
    //     already established elsewhere in this same file/header rather than
    //     re-derived). `0x009df3d0` scans the per-client roster array at the
    //     object-manager singleton `0x03171a64`'s own `+0x1f0`/`+0x1f8` fields
    //     for the first entry that is NOT the local-player singleton
    //     `0x0262edfc`, returning null/false when every entry equals it
    //     (spec-ai-runtime.md item 7's independent disassembly confirmation,
    //     cross-cited by Sec14.31's own "dual local-player representation"
    //     finding). That array is the live network-client list: a second
    //     entry can only exist once an actual second client has joined a
    //     >=2-member co-op session. Item 1 above already establishes, CONFIRMED
    //     by disassembly, that this host's session singleton `0x024d8534` is
    //     zero at load and that no resolved code anywhere installs a session
    //     ("a host starts with no session") - the identical fact
    //     `customizationCreateCharacter`'s own hardening (engine_state.h,
    //     CustomizationCreationState) and `setPersonaGlobalSituationOverride`
    //     already reuse `coopSession().present` for. Item 9 above
    //     independently reaches the same conclusion from the character side:
    //     "no '#PLAYER2#' character object is even constructed while co-op is
    //     absent." With no second client ever joining, the roster array this
    //     host models never grows past the local player, so `0x009df3d0`'s
    //     own CONFIRMED mechanism can only ever yield "every entry equals the
    //     local singleton" - false, not OPEN. This closes the shared
    //     dlc2_m01/dlc2_m02/dlc2_m03/dlc3_m02/m12/m24 blocker
    //     (customization_restore_player_rig's default-mask bit 1, Sec28.12)
    //     without touching coopPlayerGender, which stays correctly OPEN: it
    //     is read only inside the `coopPlayerPresent` branch, which this
    //     default now never enters.
    es.playerRig().coopPlayerPresent.set(false);

    // 14. notoriety spawn-GROUP resolver 0x008ed350 (spec-lua-api-behaviour.md
    //     Sec6.2/Sec6.10/Sec13.2): "a case-insensitive linear scan over 24
    //     fixed name strings read directly from .rdata, the first being the
    //     literal 'police'... returns a group-record pointer or null" -
    //     Sec6.2's own text leaves which 24 names populate the table OPEN
    //     ("which 24 real names populate the group table beyond the
    //     confirmed first entry 'police'"). CONFIRMED by cross-reference,
    //     2026-10-09: spec-tables-progression.md Sec6.3 independently
    //     derives the exact same 24-entry name table for the
    //     notoriety_spawn.xtbl loader (FUN_008EEC00 -> FUN_008ED480, name
    //     list 0x013080F8, destination structures at 0x01308098) - and its
    //     own destination-structure layout cites the IDENTICAL offsets
    //     Sec6.2 reads off the resolved "group record" (level array pointer
    //     +0x3E8, first/last level +0x3E0/+0x3E4), which is conclusive that
    //     0x008ed350 resolves into that SAME fixed array of 24 runtime
    //     structures, not a separate table. Sec6.3's own review status:
    //     "re-derived from the executable 2026-10-01 ... CONFIRMED -
    //     disassembly (24 destinations...) - cleared for implementation."
    //     Resolution success is a pure name-table match (one of the 24
    //     fixed strings, independent of whether notoriety_spawn.xtbl itself
    //     populated that entry's data) - this project's own
    //     src/tables_progression.cpp::kNotorietySpawnGroupNames[24] already
    //     carries the identical, tested list; reproduced verbatim here
    //     (same "duplicate a short confirmed literal table with a citation,
    //     don't cross-link modules" convention already used for
    //     resolveFakeNotorietySlot's kSr3/kSr2/kSr1 arrays). Only these
    //     EXACT (lowercase) 24 spellings resolve; any other name - including
    //     a different-case spelling of one of these 24, since case-folding
    //     the lookup itself is out of this fix's scope - stays OPEN-until-
    //     set, same discipline as every other not-given name in this map.
    static const char* const kNotorietySpawnGroupNames[24] = {
        "police", "police_motorcycle", "police_heli", "police_blackhawk", "police_attack_heli",
        "police_waverunner", "police_speedboat", "police_riot", "police_tank", "police_ng",
        "luchadores", "deckers", "morningstar", "morningstar_heli", "stag",
        "stag_riot", "stag_heli", "stag_attack_heli", "stag_tank", "whored_one",
        "whored_two", "survival_bikers", "survival_mascots", "survival_bums",
    };
    for (const char* name : kNotorietySpawnGroupNames) {
        es.notorietySpawnGroupResolves().set(name, true);
    }

    // 15. tutorial_start dispatcher step 2, EngineState::TutorialStart::
    //     globalMode029443bc (spec-lua-api-behaviour.md Sec6.19 correction,
    //     2026-10-10, Team A spec sync routed via the dlc2_m03 mission-
    //     blocking probe): 0x029443bc is the machinima recorder's OWN
    //     recording-state machine (0 idle/1 recording/2,3 finalizing/4
    //     teardown), NOT a "tutorial dispatcher global" - that framing is
    //     retracted. CONFIRMED zero-fill default: a fresh single-player
    //     game with no machinima capture in progress reads 0 (idle), the
    //     same CONFIRMED default MachinimaState::recording already assumes
    //     for the same underlying global (Sec44.4/Sec59.1's game_machinima_
    //     is_recording/cell_machinima_is_recording, 0x029443bc==1). This
    //     closes the dlc2_m03 blocker at tutorial_start's own step 2 (was
    //     OPEN, stopped every call): 0 is never 1, 2 or 3, so step 2 never
    //     stops the dispatcher in a normal game.
    es.tutorialStart().globalMode029443bc.set(0);
}

} // namespace sr3luahost
