// Round-trip tests for spec-save-format.md Sec15's save-trigger wiring
// (added 2026-10-08): mission_autosave/game_autosave/mission_set_checkpoint
// are the three CONFIRMED real save triggers, all funnelling through the
// single real save entry 0x00B94A10 (Sec6.3) - this file proves that path
// is wired for real in this project, not just gate-counted: a mission step
// changes live EngineState::saveGame()/playerCash() world state, the
// native is called (through the SAME Lua-visible entry points
// tools/lua_host_run.cpp would reach), the result is serialized exactly as
// the real snapshot format (Sec6.4), the world state is then mutated AGAIN
// to a different value, and the ORIGINAL value is recovered by loading the
// save/checkpoint back - the only way that last assertion can pass is if
// serialize+deserialize actually round-tripped real data, not merely "ran
// without crashing."
//
// Deliberately does NOT test player position: spec-save-format.md Sec6.6
// confirms the real save carries no player position/orientation anywhere
// (checked directly against the region map's own 99.5% coverage with 0
// unaccounted non-zero bytes) - testing that it round-trips would assert
// behavior the real game does not have. Cash/respect/level/difficulty/
// cheats/barnstorm-stunt counters/collectibles (this project's own
// "inventory of collected world pickups", save_game_state.h) are used
// instead - all CONFIRMED real save fields this project's own EngineState
// now models (save_game_bridge.h).
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"
#include "sr3luahost/save_game_bridge.h"
#include "sr3save/save_snapshot.h"
#include "sr3io/output_path_guard.h"

using namespace sr3luahost;
namespace fs = std::filesystem;

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
    auto r = host.runChunk(L, code, "save_game_bridge_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

std::vector<RegisteredName> fixtureNames() {
    return {
        {"mission_set_checkpoint", "gameplay"},
        {"game_autosave", "ui"},
        {"mission_autosave", "gameplay"},
    };
}

// A fresh, empty sandbox directory under the OS temp dir - never the
// default (process-id-qualified) directory EngineState's constructor
// already picked, so this test's own file-existence/content assertions
// can't collide with another test binary or another block in this same
// file running moments apart.
fs::path makeSandboxDir(const char* tag) {
    fs::path dir = fs::temp_directory_path() /
                   (std::string("sr3_save_bridge_test_") + tag + "_" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    return dir;
}

} // namespace

int main() {
    // ============================================================
    // 1. mission_set_checkpoint (Sec15.3/Sec11.2): the in-memory branch.
    //    "No hash, no file write" - round-trips through
    //    EngineState::lastCheckpointSnapshot() instead.
    // ============================================================
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();

        // Drive a "mission step": the checkpoint gate requires an active
        // mission (Sec1.6/Sec21.27a - see the 2026-10-08 fix this project's
        // own missionContext() doc comment describes).
        es.missionContext().activeAndRunning.set(true);
        // arg 2 defaults false, which consults usePlayerVehicleTransform
        // (Sec1.6) - OPEN until a test sets it; this test's own subject is
        // the save/load round trip, not that unrelated OPEN fact, so it is
        // set to the "common single-player case" (false, per this
        // project's own existing mission_set_checkpoint test) rather than
        // passing arg2=true everywhere, closer to how a real mission
        // script calls it.
        es.missionCheckpoint().usePlayerVehicleTransform.set(false);

        CHECK(!es.lastCheckpointSnapshot().has_value()); // nothing set yet

        // World state BEFORE the checkpoint - this is what must survive.
        es.saveGame().respectTotal = 73628;
        es.saveGame().respectInLevel = 533;
        es.saveGame().playerLevel = 22;
        es.saveGame().difficulty = 2; // hardcore
        es.saveGame().cheatsUsed = false;
        es.saveGame().barnstormsFound = 2;
        es.saveGame().stuntJumpsFound = 3;
        es.saveGame().activeMissionLabelIndex = 7; // display-only, round-tripped as inert data
        es.saveGame().levelName = "sr3_city";
        es.saveGame().collectibles = {{0xAAA00000u, 0xBBB00000u, 0u}, {0xAAA00001u, 0xBBB00001u, 2u}};
        es.playerCash() = 1154391.0; // dollars - matches a real sample's "cash on hand" magnitude (Sec3)

        ok(host, gp, "mission_set_checkpoint('cp1')");
        CHECK(es.missionCheckpoint().setCount == 1);
        CHECK(es.checkpointSnapshotBuildCount() == 1);
        CHECK(es.lastCheckpointSnapshot().has_value());
        CHECK(es.lastCheckpointSnapshot()->size() == sr3save::kSaveSnapshotSize);

        // Sec11.2: the checkpoint buffer carries NO hash (built with
        // computeHash=false) - the real "in memory only" layout, distinct
        // from a file write.
        {
            sr3save::SaveSnapshot raw = sr3save::SaveSnapshot::parse(
                sr3save::ByteView(es.lastCheckpointSnapshot()->data(), es.lastCheckpointSnapshot()->size()));
            CHECK(!raw.hashValid()); // stored 0 != the real computed hash of non-empty content
            CHECK(raw.versionSupported());
        }

        // The world "keeps playing": every one of the fields above changes
        // to something ELSE before the checkpoint is ever loaded back.
        es.saveGame().respectTotal = 1;
        es.saveGame().respectInLevel = 1;
        es.saveGame().playerLevel = 1;
        es.saveGame().difficulty = 0;
        es.saveGame().cheatsUsed = true;
        es.saveGame().barnstormsFound = 0;
        es.saveGame().stuntJumpsFound = 0;
        es.saveGame().activeMissionLabelIndex = 0x7F;
        es.saveGame().levelName = "some_other_zone";
        es.saveGame().collectibles.clear();
        es.playerCash() = 1.0;

        // Loading the checkpoint back must recover the ORIGINAL (pre-
        // mutation) values, not the current ones - the real proof of a
        // working round trip, not just "didn't crash."
        CHECK(applyCheckpointSnapshot(es));
        CHECK(es.saveGame().respectTotal == 73628);
        CHECK(es.saveGame().respectInLevel == 533);
        CHECK(es.saveGame().playerLevel == 22);
        CHECK(es.saveGame().difficulty == 2);
        CHECK(!es.saveGame().cheatsUsed);
        CHECK(es.saveGame().barnstormsFound == 2);
        CHECK(es.saveGame().stuntJumpsFound == 3);
        CHECK(es.saveGame().activeMissionLabelIndex == 7); // inert, round-tripped, never used to resume a mission
        CHECK(es.saveGame().levelName == "sr3_city");
        CHECK(es.saveGame().collectibles.size() == 2);
        CHECK(es.saveGame().collectibles[1].group == 2 && es.saveGame().collectibles[1].handleHi == 0xBBB00001u);
        CHECK(es.playerCash() == 1154391.0);

        // applyCheckpointSnapshot() on a fresh EngineState with no
        // checkpoint ever set reports false rather than fabricating state.
        Host freshHost(fixtureNames());
        CHECK(!applyCheckpointSnapshot(freshHost.engineState()));
    }

    // ============================================================
    // 2. game_autosave (Sec15.2): the real file-write branch. A real
    //    sr3save_00.sr3s_pc is produced on disk (slot 0 - Sec8.2's own
    //    "autosave... 1 in slot 0 of every real directory" convention).
    // ============================================================
    {
        Host host(fixtureNames());
        lua_State* ui = host.uiState();
        EngineState& es = host.engineState();

        fs::path sandbox = makeSandboxDir("autosave");
        es.saveGameDirectory() = sandbox.string();

        // Clear every gate (Sec27.12) so the native actually reaches the
        // real save entry - same convention tests/synthetic_luahost_
        // batch2728_test.cpp already established for this native.
        es.autosave().suppressFlag.set(false);
        es.autosave().secondFlagSet.set(false);
        es.autosave().missionActive.set(false);
        es.autosave().thirdGateBlocks.set(false);
        es.autosave().fourthFlagNonzero.set(false);

        es.saveGame().respectTotal = 40000;
        es.saveGame().respectInLevel = 1200;
        es.saveGame().playerLevel = 15;
        es.saveGame().difficulty = 1;
        es.saveGame().barnstormsFound = 5;
        es.saveGame().collectibles = {{0x1111u, 0x2222u, 3u}};
        es.playerCash() = 987654.0;

        CHECK(es.autosaveFileWriteCount() == 0);
        ok(host, ui, "game_autosave()");
        CHECK(es.autosave().triggeredCount == 1);
        CHECK(es.autosaveFileWriteCount() == 1);
        CHECK(es.lastAutosaveError().empty());
        std::string savedPath = es.lastAutosaveFilePath();
        CHECK(!savedPath.empty());
        CHECK(fs::exists(savedPath));
        CHECK(fs::file_size(savedPath) == sr3save::kSaveSnapshotSize);
        CHECK(fs::path(savedPath).filename() == "sr3save_00.sr3s_pc"); // Sec8.2's own slot-0 convention

        // Read the real on-disk bytes directly (not through the bridge) -
        // proves the FILE itself, not just in-memory state, carries the
        // real content and a verifiable hash (Sec6.1/Sec6.3's own "the
        // real save entry... hash, then queue an async write").
        {
            std::ifstream in(savedPath, std::ios::binary);
            std::vector<uint8_t> bytes(sr3save::kSaveSnapshotSize);
            CHECK(static_cast<bool>(in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))));
            sr3save::SaveSnapshot fromDisk = sr3save::SaveSnapshot::parse(sr3save::ByteView(bytes.data(), bytes.size()));
            CHECK(fromDisk.hashValid());
            CHECK(fromDisk.versionSupported());
            CHECK(fromDisk.respectTotal() == 40000 && fromDisk.playerLevel() == 15);
            CHECK(fromDisk.cashDiv100() == 987654);
        }

        // The world keeps playing after the autosave...
        es.saveGame().respectTotal = 0;
        es.saveGame().respectInLevel = 0;
        es.saveGame().playerLevel = 0;
        es.saveGame().difficulty = 0;
        es.saveGame().barnstormsFound = 0;
        es.saveGame().collectibles.clear();
        es.playerCash() = 0.0;

        // ...and loading the autosave file back recovers what was there
        // AT SAVE TIME, not the current (mutated) state - the file-based
        // mirror of the checkpoint test's in-memory round trip above.
        std::string loadErr;
        CHECK(loadSaveSnapshotFile(savedPath, es, loadErr));
        CHECK(loadErr.empty());
        CHECK(es.saveGame().respectTotal == 40000);
        CHECK(es.saveGame().respectInLevel == 1200);
        CHECK(es.saveGame().playerLevel == 15);
        CHECK(es.saveGame().difficulty == 1);
        CHECK(es.saveGame().barnstormsFound == 5);
        CHECK(es.saveGame().collectibles.size() == 1 && es.saveGame().collectibles[0].group == 3);
        CHECK(es.playerCash() == 987654.0);

        std::error_code ec;
        fs::remove_all(sandbox, ec); // this test's own scratch output, not a project artifact
    }

    // ============================================================
    // 3. mission_autosave (Sec15.2, the SAME real save entry as
    //    game_autosave, Sec15's own "both three bottom out in the same
    //    real save call" finding) - a lighter confirmatory pass: the
    //    deep round-trip machinery is already proven identical above
    //    (both call the same writeAutosaveSnapshot()/loadSaveSnapshotFile()
    //    pair), so this only needs to show THIS native reaches it too.
    // ============================================================
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();

        fs::path sandbox = makeSandboxDir("mission_autosave");
        es.saveGameDirectory() = sandbox.string();
        // Defaults (missionAutosaveSteamCheckOk=true, Busy=false,
        // ChainReady=true) already reach a real kickoff with no setup -
        // CONFIRMED per Sec60.3 (see this native's own stub doc comment).
        es.saveGame().playerLevel = 8;
        es.playerCash() = 42000.0;

        ok(host, gp, "mission_autosave()");
        CHECK(es.missionAutosaveKickoffCount() == 1);
        CHECK(es.autosaveFileWriteCount() == 1);
        std::string savedPath = es.lastAutosaveFilePath();
        CHECK(fs::exists(savedPath) && fs::file_size(savedPath) == sr3save::kSaveSnapshotSize);

        EngineState freshEs; // a separate, otherwise-untouched EngineState - proves the FILE carries the data
        std::string loadErr;
        CHECK(loadSaveSnapshotFile(savedPath, freshEs, loadErr));
        CHECK(freshEs.saveGame().playerLevel == 8);
        CHECK(freshEs.playerCash() == 42000.0);

        std::error_code ec;
        fs::remove_all(sandbox, ec);
    }

    // ============================================================
    // 4. The output-path guard (sr3io::isOutputPathSafe, shared with
    //    src/wav_writer.cpp/src/png_writer.cpp) still applies to this
    //    writer: pointing saveGameDirectory() at the real protected
    //    game-install root must refuse the write outright - the exact
    //    class of mistake that prompted that guard's own creation
    //    (output_path_guard.h's top note: a different writer once
    //    overwrote the real, live ~1.5GB game archive this way). The
    //    refusal is checked BEFORE any filesystem write (writeAutosave
    //    Snapshot() now checks isOutputPathSafe() before create_
    //    directories()/ofstream, not after), so this test never actually
    //    creates anything under that real path - it only asserts the
    //    call reports failure and touches nothing.
    // ============================================================
    {
        Host host(fixtureNames());
        EngineState& es = host.engineState();
        const std::string protectedPath = "D:/Project Crreish/Saints Row 3 CRREISH/__sr3_save_bridge_test_never_written__";
        es.saveGameDirectory() = protectedPath;
        std::string path, err;
        bool wrote = writeAutosaveSnapshot(es, path, err);
        CHECK(!wrote);
        CHECK(!err.empty());
        CHECK(path.empty()); // left untouched on refusal
        CHECK(!fs::exists(protectedPath)); // nothing was ever created under the protected root
    }

    if (g_failures == 0) {
        std::cout << "All save-game-bridge (spec-save-format.md Sec15) round-trip tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) FAILED.\n";
    return 1;
}
