// EngineState slice: save game, autosave, checkpoints.
//
// Split out of engine_state.h (refactor step 3a, 2026-10-17) as a zero-behaviour-change move: every
// type, field, initialiser and comment below moved verbatim (namespace-scope types re-indented one
// level). Types that were nested in EngineState are now namespace-scope types in sr3luahost;
// EngineStateSaveData re-exports each one under its old name, so `EngineState::Type` still
// resolves. EngineState inherits EngineStateSaveData publicly, so `state.member`,
// `EngineState::kConstant` and `state.accessor()` are unchanged for every caller. Members that
// were private in EngineState are `protected` here (the only access change). Method
// declarations stay in EngineState (engine_state.h); bodies are in src/*.cpp.
#pragma once

#include "sr3luahost/engine_state_common.h"

namespace sr3luahost {

// Sec27.12 `game_autosave`.
struct Autosave {
    OpenValue<bool> suppressFlag{"0x012fcadc (CONFIRMED file value 1 = suppressed)",
                                 "spec-lua-api-behaviour.md Sec27.12"};
    OpenValue<bool> secondFlagSet{"0x0290ceca nonzero", "spec-lua-api-behaviour.md Sec27.12"};
    OpenValue<bool> missionActive{"0x006cecb0 (mission active)", "spec-lua-api-behaviour.md Sec27.12"};
    OpenValue<bool> thirdGateBlocks{"0x006f8370 result (itself OPEN per spec)", "spec-lua-api-behaviour.md Sec27.12"};
    OpenValue<bool> fourthFlagNonzero{"0x0229a317 nonzero", "spec-lua-api-behaviour.md Sec27.12"};
    uint64_t triggeredCount = 0; // 0x00b94ff0 itself OPEN; counts "every gate passed, would have saved"
};

// Sec39.4 save-system UI cluster (save_system_save_game/_load_game/
// _cancel_coop_load): one shared singleton; slot records are a fixed
// table whose count is read from one field (OPEN - the real at-rest
// value/writer isn't given in scope). CONFIRMED real defect (HIGH
// CONFIDENCE per spec, sibling-confirmed): both the save-confirm
// callback and the load path bound the slot with `slot <= count`,
// accepting `slot == count` - one past the end of the table. The
// mandated faithful quirk: this host allows that exact off-by-one
// (not "correctly" rejecting it) and logs when it fires, rather than
// tightening the bound to `<`.
struct SaveSystemUi {
    OpenValue<uint32_t> slotCount{"save-system UI slot-record count", "spec-lua-api-behaviour.md Sec39.4"};
    uint64_t saveCallCount = 0;
    uint64_t loadCallCount = 0;
    uint64_t saveOffByOneCount = 0; // CONFIRMED-adjacent real defect: slot==count accepted (<=, not <)
    uint64_t loadOffByOneCount = 0;
    // save_system_cancel_coop_load's own state field (Sec39.4: "sets
    // the save-system's own state field to 5 unless already there").
    // CHOSEN write-only default - the real at-rest value is OPEN/not
    // given in scope, same "CHOSEN write-only in scope" convention as
    // CatMouseMinigame::selection above.
    int32_t coopLoadState = 0;
};

// --- Sec51.2 save_game_wrap_up/save_system_change_device/_delete_game/
// _set_operation (reuses the existing saveSystemUi() where relevant) ---
struct SaveGameExtra {
    uint64_t wrapUpCount = 0;
    uint64_t changeDeviceCount = 0;
    bool jobInProgress = false; // gates delete
    OpenValue<bool> steamUserInterfaceAvailable{"save-system Steam user interface availability",
                                                 "spec-lua-api-behaviour.md Sec51.2"};
    uint64_t deleteDroppedCount = 0;
    uint64_t deleteAppliedCount = 0;
    int32_t lastOperation = 0; // CONFIRMED dead write (nothing reads it)
};

struct EngineStateSaveData {
    // Former EngineState nested types (defined above at namespace scope), re-exported so
    // `EngineState::Type` keeps naming them.
    using Autosave = sr3luahost::Autosave;
    using SaveSystemUi = sr3luahost::SaveSystemUi;
    using SaveGameExtra = sr3luahost::SaveGameExtra;

public:
    Autosave& autosave() { return autosave_; }
    // =====================================================================
    // pause-map stag mode / district control (spec-lua-api-behaviour.md
    // Sec31) + pause_map_tutorial_mode (Sec32.1) - batch 2026-10-02.
    // =====================================================================

    // 0x0229a317 - the SAME global Sec27.12's `game_autosave` already reads
    // (autosave().fourthFlagNonzero: "0x0229a317 nonzero" blocks autosave -
    // Sec31.5's own cross-reference: "the autosave gate... autosave is
    // suppressed while stag mode is on"). Reused directly rather than
    // duplicated, so a stag-mode write from the pause-map functions below is
    // honestly visible to the autosave gate too, and vice versa.
    OpenValue<bool>& pauseMapStagMode() { return autosave_.fourthFlagNonzero; }
    SaveSystemUi& saveSystemUi() { return saveSystemUi_; }

    // spec-save-format.md Sec15 wiring (2026-10-08): the live, mutable
    // subset of a real save snapshot's player-state fields this project
    // tracks (save_game_state.h's own top note has the full scope
    // statement - respect/level/difficulty/cheats/barnstorm/stunt-jump
    // counters/collectibles/the display-only mission label/zone name; cash
    // reuses the existing playerCash() below instead of a second balance).
    SaveGameState& saveGame() { return saveGame_; }

    // Where mission_autosave/game_autosave (save_game_bridge.h's
    // writeAutosaveSnapshot()) write the real sr3save_NN.sr3s_pc file this
    // task's own brief asks for. No prior save-path concept existed in this
    // project (checked before adding this - save_browser.cpp takes a
    // directory as a CLI argument instead) - defaults to a clearly-labelled
    // scratch location (the OS temp directory, process-id-qualified so
    // concurrent test binaries never race on the same slot file) rather
    // than any real game save-data location, which this project has no
    // business writing into (see sr3io::isOutputPathSafe's own protected-
    // root list, output_path_guard.h, which every write still goes through
    // regardless of this setting). A test overrides it to its own sandbox
    // directory.
    std::string& saveGameDirectory() { return saveGameDirectory_; }

    // mission_set_checkpoint's OTHER real mode (Sec11.2/Sec15.3): the same
    // writer, built into caller-owned memory instead of a file - "no hash,
    // no file write... an in-memory checkpoint snapshot, not a file save"
    // (this task's own brief). Stored here (rather than discarded) so a
    // later load can retrieve it - see save_game_bridge.h's
    // applyCheckpointSnapshot(). std::nullopt = "no checkpoint set yet."
    std::optional<std::vector<uint8_t>>& lastCheckpointSnapshot() { return lastCheckpointSnapshot_; }
    uint64_t& checkpointSnapshotBuildCount() { return checkpointSnapshotBuildCount_; }

    // Test/observability counters for the real file-write side (save_game_
    // bridge.h's writeAutosaveSnapshot()) - mirrors this project's own
    // established "plain call counter, not a spec fact" convention (e.g.
    // MissionContext::missionEndFailureDispatchCount above).
    uint64_t& autosaveFileWriteCount() { return autosaveFileWriteCount_; }
    std::string& lastAutosaveFilePath() { return lastAutosaveFilePath_; }
    std::string& lastAutosaveError() { return lastAutosaveError_; }

    // --- Sec44.5 game_is_autosaving: CHOSEN simplification - one flag
    // standing in for the real 3-test combination (the real native itself
    // already omits one of the three, per spec; this project tracks the
    // net observable result directly rather than re-deriving all 3 tests).
    bool& autosaveInProgressSimplified() { return autosaveInProgressSimplified_; }
    SaveGameExtra& saveGameExtra() { return saveGameExtra_; }

    // mission_autosave (Sec17.23, corrected per Sec60.3 - implemented from
    // the correction, NOT Sec17.23 alone): a silent-return-on-Steam-check-
    // failure path gates the WHOLE function (no record, no save attempt at
    // all) - CHOSEN true ("Steam available," this project models no real
    // Steam integration anywhere else either, and failing this gate closed
    // by default would silently block every autosave in every test). The
    // busy predicate's own polarity is CONFIRMED inverted from Sec17.23's
    // original text: the real save kickoff proceeds only when busy is
    // FALSE - CHOSEN false ("not busy" - the common fresh-run case). The
    // native explicitly skips game_autosave's own extra mission gates (so
    // it can save mid-mission) - this project has no mission-gate state to
    // skip in the first place, so that correction has no further code
    // effect here beyond NOT adding one.
    bool& missionAutosaveSteamCheckOk() { return missionAutosaveSteamCheckOk_; }
    bool& missionAutosaveBusy() { return missionAutosaveBusy_; }
    // CHOSEN true ("ready") - a plain bool, not OpenValue: mission_autosave
    // is a real-hit (RT) target, and an unresolved OPEN refusal on this
    // internal readiness chain would needlessly block a measurement run
    // over a detail nothing in scope can independently observe either way.
    bool& missionAutosaveChainReady() { return missionAutosaveChainReady_; }
    bool& missionAutosaveDeferredPending() { return missionAutosaveDeferredPending_; }
    uint64_t& missionAutosaveStubCallCount() { return missionAutosaveStubCallCount_; }
    uint64_t& missionAutosaveKickoffCount() { return missionAutosaveKickoffCount_; }

protected: // private in EngineState before the split
    Autosave autosave_;
    SaveSystemUi saveSystemUi_;

    // spec-save-format.md Sec15 wiring (2026-10-08) - see each public
    // accessor's own doc comment above (next to missionCheckpoint()) for
    // citation/reasoning.
    SaveGameState saveGame_;
    std::string saveGameDirectory_; // set in the constructor below (defaultSaveGameDirectory())
    std::optional<std::vector<uint8_t>> lastCheckpointSnapshot_;
    uint64_t checkpointSnapshotBuildCount_ = 0;
    uint64_t autosaveFileWriteCount_ = 0;
    std::string lastAutosaveFilePath_;
    std::string lastAutosaveError_;
    bool autosaveInProgressSimplified_ = false;
    SaveGameExtra saveGameExtra_;
    bool missionAutosaveSteamCheckOk_ = true;
    bool missionAutosaveBusy_ = false;
    bool missionAutosaveChainReady_ = true;
    bool missionAutosaveDeferredPending_ = false;
    uint64_t missionAutosaveStubCallCount_ = 0;
    uint64_t missionAutosaveKickoffCount_ = 0;
};

} // namespace sr3luahost
