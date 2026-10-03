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
    //    Everything else here still has no specced start-up value and stays
    //    OPEN: the pending entry, the load state 0x0153b51c,
    //    0x0153b541 / 0x0153b542, the soundtrack stream globals, the cutscene
    //    state 0x0153b520 and the cutscene manager. The scene table itself is
    //    real data (cutscene.xtbl + <name>.cte_xtbl); lua_host_run installs it
    //    from the archive cache (EngineState::installZsceneTable), not this
    //    function.

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
}

} // namespace sr3luahost
