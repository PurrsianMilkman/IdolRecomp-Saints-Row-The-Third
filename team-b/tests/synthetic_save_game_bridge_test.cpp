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
        es.saveGame().garageNextId = 42;
        {
            GarageVehicleRecord gv;
            gv.recordId = 18;
            gv.blobLength = 4;
            gv.unknownFlag = 500.0f;
            gv.vehicleTypeIndex = 37;
            gv.enumE = 2;
            gv.enum10 = 3;
            gv.byte12 = 5;
            gv.optionSet[0] = 0xAB;
            gv.optionSet[31] = 0xCD;
            gv.blob[0] = 0x0B; gv.blob[1] = 0xB0; gv.blob[2] = 0x55; gv.blob[3] = 0x0A; // Sec9.2's own 0x0A55B00B header
            gv.stateFlags = 0x41;
            es.saveGame().garageVehicles = {gv};
        }
        {
            WardrobeOwnedItem w0; // colour-0xFF-means-none convention, defaults otherwise
            w0.itemId = 0x12345678u;
            w0.colour0 = 0xFF; w0.colour1 = 0xFF; w0.colour2 = 0xFF;
            w0.wearOption = 0;
            w0.flag = false;
            w0.spareBits = 0;
            WardrobeOwnedItem w1; // nonzero wear-option, real colours
            w1.itemId = 0xABCDEF01u;
            w1.colour0 = 3; w1.colour1 = 7; w1.colour2 = 0xFF;
            w1.wearOption = 9;
            w1.flag = false;
            w1.spareBits = 0;
            WardrobeOwnedItem w2; // flag bit set
            w2.itemId = 0x0F0F0F0Fu;
            w2.colour0 = 0xFF; w2.colour1 = 1; w2.colour2 = 2;
            w2.wearOption = 15;
            w2.flag = true;
            w2.spareBits = 0;
            es.saveGame().wardrobeItems = {w0, w1, w2};
        }
        {
            WeaponInventoryItem wi0;
            wi0.weaponDefId = 0x1A2B3C4Du;
            wi0.unused = 0;
            wi0.ammo = {30, 90, 0, 0};
            wi0.flag0 = 1;
            wi0.flag1 = 0;
            WeaponInventoryItem wi1;
            wi1.weaponDefId = 0x5E6F7081u;
            wi1.unused = 0;
            wi1.ammo = {-1, 8, 16, 2};
            wi1.flag0 = 0;
            wi1.flag1 = 1;
            es.saveGame().weaponInventory = {wi0, wi1};
        }
        {
            sr3save::RegionRecord r0;
            r0.handleLo = 0x1000u; r0.handleHi = 0x2000u; r0.controlled = true;
            sr3save::RegionRecord r1;
            r1.handleLo = 0x3000u; r1.handleHi = 0x4000u; r1.controlled = false;
            es.saveGame().regionRecords = {r0, r1};
        }
        {
            sr3save::ActivityRecord a0;
            a0.nameHash = 0xAAAA1111u; a0.levelsCompleted = 1; a0.completedMask = 0x07; a0.secondMask = 0x03;
            sr3save::ActivityRecord a1;
            a1.nameHash = 0xBBBB2222u; a1.levelsCompleted = 0; a1.completedMask = 0x00; a1.secondMask = 0x00;
            es.saveGame().activityRecords = {a0, a1};
        }
        es.saveGame().activityMarkerFlags = 0x0123456789ABCDEFull;
        es.saveGame().activeCheatIds = {11u, 22u, 33u};
        es.saveGame().statistics[0].rawValue = 777u;
        es.saveGame().statistics[0].denominatorStatId = 0u;
        es.saveGame().statistics[5].rawValue = 42u;
        es.saveGame().statistics[5].denominatorStatId = 4u;

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
        es.saveGame().garageNextId = 1;
        es.saveGame().garageVehicles.clear();
        es.saveGame().wardrobeItems.clear();
        es.saveGame().weaponInventory.clear();
        es.saveGame().regionRecords.clear();
        es.saveGame().activityRecords.clear();
        es.saveGame().activityMarkerFlags = 0;
        es.saveGame().activeCheatIds.clear();
        es.saveGame().statistics[0].rawValue = 1;
        es.saveGame().statistics[0].denominatorStatId = 1;
        es.saveGame().statistics[5].rawValue = 1;
        es.saveGame().statistics[5].denominatorStatId = 1;

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
        CHECK(es.saveGame().garageNextId == 42);
        CHECK(es.saveGame().garageVehicles.size() == 1);
        {
            const GarageVehicleRecord& gv = es.saveGame().garageVehicles[0];
            CHECK(gv.recordId == 18);
            CHECK(gv.blobLength == 4);
            CHECK(gv.unknownFlag == 500.0f);
            CHECK(gv.vehicleTypeIndex == 37);
            CHECK(gv.enumE == 2);
            CHECK(gv.enum10 == 3);
            CHECK(gv.byte12 == 5);
            CHECK(gv.optionSet[0] == 0xAB && gv.optionSet[31] == 0xCD);
            CHECK(gv.blob[0] == 0x0B && gv.blob[1] == 0xB0 && gv.blob[2] == 0x55 && gv.blob[3] == 0x0A);
            CHECK(gv.stateFlags == 0x41);
        }
        CHECK(es.saveGame().wardrobeItems.size() == 3);
        {
            const WardrobeOwnedItem& w0 = es.saveGame().wardrobeItems[0];
            CHECK(w0.itemId == 0x12345678u);
            CHECK(w0.colour0 == 0xFF && w0.colour1 == 0xFF && w0.colour2 == 0xFF);
            CHECK(w0.wearOption == 0);
            CHECK(!w0.flag);
            CHECK(w0.spareBits == 0);
            const WardrobeOwnedItem& w1 = es.saveGame().wardrobeItems[1];
            CHECK(w1.itemId == 0xABCDEF01u);
            CHECK(w1.colour0 == 3 && w1.colour1 == 7 && w1.colour2 == 0xFF);
            CHECK(w1.wearOption == 9);
            CHECK(!w1.flag);
            const WardrobeOwnedItem& w2 = es.saveGame().wardrobeItems[2];
            CHECK(w2.itemId == 0x0F0F0F0Fu);
            CHECK(w2.colour0 == 0xFF && w2.colour1 == 1 && w2.colour2 == 2);
            CHECK(w2.wearOption == 15);
            CHECK(w2.flag);
        }
        CHECK(es.saveGame().weaponInventory.size() == 2);
        {
            const WeaponInventoryItem& wi0 = es.saveGame().weaponInventory[0];
            CHECK(wi0.weaponDefId == 0x1A2B3C4Du);
            CHECK(wi0.unused == 0);
            CHECK(wi0.ammo[0] == 30 && wi0.ammo[1] == 90 && wi0.ammo[2] == 0 && wi0.ammo[3] == 0);
            CHECK(wi0.flag0 == 1);
            CHECK(wi0.flag1 == 0);
            const WeaponInventoryItem& wi1 = es.saveGame().weaponInventory[1];
            CHECK(wi1.weaponDefId == 0x5E6F7081u);
            CHECK(wi1.unused == 0);
            CHECK(wi1.ammo[0] == -1 && wi1.ammo[1] == 8 && wi1.ammo[2] == 16 && wi1.ammo[3] == 2);
            CHECK(wi1.flag0 == 0);
            CHECK(wi1.flag1 == 1);
        }
        CHECK(es.saveGame().regionRecords.size() == 2);
        {
            const sr3save::RegionRecord& r0 = es.saveGame().regionRecords[0];
            CHECK(r0.handleLo == 0x1000u && r0.handleHi == 0x2000u && r0.controlled);
            const sr3save::RegionRecord& r1 = es.saveGame().regionRecords[1];
            CHECK(r1.handleLo == 0x3000u && r1.handleHi == 0x4000u && !r1.controlled);
        }
        CHECK(es.saveGame().activityRecords.size() == 2);
        {
            const sr3save::ActivityRecord& a0 = es.saveGame().activityRecords[0];
            CHECK(a0.nameHash == 0xAAAA1111u && a0.levelsCompleted == 1 && a0.completedMask == 0x07 && a0.secondMask == 0x03);
            const sr3save::ActivityRecord& a1 = es.saveGame().activityRecords[1];
            CHECK(a1.nameHash == 0xBBBB2222u && a1.levelsCompleted == 0);
        }
        CHECK(es.saveGame().activityMarkerFlags == 0x0123456789ABCDEFull);
        CHECK(es.saveGame().activeCheatIds.size() == 3);
        CHECK(es.saveGame().activeCheatIds[0] == 11u && es.saveGame().activeCheatIds[1] == 22u &&
              es.saveGame().activeCheatIds[2] == 33u);
        CHECK(es.saveGame().statistics[0].rawValue == 777u && es.saveGame().statistics[0].denominatorStatId == 0u);
        CHECK(es.saveGame().statistics[5].rawValue == 42u && es.saveGame().statistics[5].denominatorStatId == 4u);

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
        es.saveGame().garageNextId = 9;
        {
            GarageVehicleRecord gv;
            gv.recordId = 3;
            gv.vehicleTypeIndex = 12;
            gv.stateFlags = 0x40;
            es.saveGame().garageVehicles = {gv};
        }
        {
            WardrobeOwnedItem w0;
            w0.itemId = 0x55AA55AAu;
            w0.colour0 = 0xFF; w0.colour1 = 0xFF; w0.colour2 = 0xFF;
            WardrobeOwnedItem w1;
            w1.itemId = 0x77u;
            w1.colour0 = 2; w1.colour1 = 0xFF; w1.colour2 = 4;
            w1.wearOption = 5;
            w1.flag = true;
            es.saveGame().wardrobeItems = {w0, w1};
        }
        {
            WeaponInventoryItem wi0;
            wi0.weaponDefId = 0x0BADF00Du;
            wi0.ammo = {12, 48, 1, 0};
            wi0.flag0 = 0;
            wi0.flag1 = 0;
            WeaponInventoryItem wi1;
            wi1.weaponDefId = 0xCAFEBABEu;
            wi1.ammo = {7, -5, 99, 3};
            wi1.flag0 = 1;
            wi1.flag1 = 1;
            es.saveGame().weaponInventory = {wi0, wi1};
        }
        {
            sr3save::RegionRecord r0;
            r0.handleLo = 0x9000u; r0.handleHi = 0xA000u; r0.controlled = true;
            es.saveGame().regionRecords = {r0};
        }
        {
            sr3save::ActivityRecord a0;
            a0.nameHash = 0xCCCC3333u; a0.levelsCompleted = 1; a0.completedMask = 0x0F; a0.secondMask = 0x05;
            es.saveGame().activityRecords = {a0};
        }
        es.saveGame().activityMarkerFlags = 0xFEDCBA9876543210ull;
        es.saveGame().activeCheatIds = {99u, 100u};
        es.saveGame().statistics[1].rawValue = 555u;
        es.saveGame().statistics[1].denominatorStatId = 0u;

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
            CHECK(fromDisk.garageNextId() == 9);
            CHECK(fromDisk.garageRecords().size() == 1);
            CHECK(fromDisk.garageRecords()[0].recordId == 3 && fromDisk.garageRecords()[0].vehicleTypeIndex == 12);
            CHECK(fromDisk.wardrobeItems().size() == 2);
            CHECK(fromDisk.wardrobeItems()[0].itemId == 0x55AA55AAu);
            CHECK(fromDisk.wardrobeItems()[0].colour0 == 0xFF && fromDisk.wardrobeItems()[0].colour1 == 0xFF &&
                  fromDisk.wardrobeItems()[0].colour2 == 0xFF);
            CHECK(fromDisk.wardrobeItems()[1].itemId == 0x77u);
            CHECK(fromDisk.wardrobeItems()[1].colour0 == 2 && fromDisk.wardrobeItems()[1].colour1 == 0xFF &&
                  fromDisk.wardrobeItems()[1].colour2 == 4);
            CHECK(fromDisk.wardrobeItems()[1].wearOption == 5);
            CHECK(fromDisk.wardrobeItems()[1].flag);
            CHECK(fromDisk.weaponInventory().size() == 2);
            CHECK(fromDisk.weaponInventory()[0].weaponDefId == 0x0BADF00Du);
            CHECK(fromDisk.weaponInventory()[0].ammo[0] == 12 && fromDisk.weaponInventory()[0].ammo[1] == 48 &&
                  fromDisk.weaponInventory()[0].ammo[2] == 1 && fromDisk.weaponInventory()[0].ammo[3] == 0);
            CHECK(fromDisk.weaponInventory()[0].flag0 == 0 && fromDisk.weaponInventory()[0].flag1 == 0);
            CHECK(fromDisk.weaponInventory()[1].weaponDefId == 0xCAFEBABEu);
            CHECK(fromDisk.weaponInventory()[1].ammo[0] == 7 && fromDisk.weaponInventory()[1].ammo[1] == -5 &&
                  fromDisk.weaponInventory()[1].ammo[2] == 99 && fromDisk.weaponInventory()[1].ammo[3] == 3);
            CHECK(fromDisk.weaponInventory()[1].flag0 == 1 && fromDisk.weaponInventory()[1].flag1 == 1);
            CHECK(fromDisk.regionRecords().size() == 1);
            CHECK(fromDisk.regionRecords()[0].handleLo == 0x9000u && fromDisk.regionRecords()[0].handleHi == 0xA000u &&
                  fromDisk.regionRecords()[0].controlled);
            CHECK(fromDisk.activityRecords().size() == 1);
            CHECK(fromDisk.activityRecords()[0].nameHash == 0xCCCC3333u && fromDisk.activityRecords()[0].levelsCompleted == 1 &&
                  fromDisk.activityRecords()[0].completedMask == 0x0F && fromDisk.activityRecords()[0].secondMask == 0x05);
            CHECK(fromDisk.activityMarkerFlagsRaw() == 0xFEDCBA9876543210ull);
            CHECK(fromDisk.activeCheatIds().size() == 2 && fromDisk.activeCheatIds()[0] == 99u && fromDisk.activeCheatIds()[1] == 100u);
            CHECK(fromDisk.statistic(sr3save::kFirstSnapshotStatId + 1) != nullptr &&
                  fromDisk.statistic(sr3save::kFirstSnapshotStatId + 1)->rawValue == 555u);
        }

        // The world keeps playing after the autosave...
        es.saveGame().respectTotal = 0;
        es.saveGame().respectInLevel = 0;
        es.saveGame().playerLevel = 0;
        es.saveGame().difficulty = 0;
        es.saveGame().barnstormsFound = 0;
        es.saveGame().collectibles.clear();
        es.playerCash() = 0.0;
        es.saveGame().garageNextId = 1;
        es.saveGame().garageVehicles.clear();
        es.saveGame().wardrobeItems.clear();
        es.saveGame().weaponInventory.clear();
        es.saveGame().regionRecords.clear();
        es.saveGame().activityRecords.clear();
        es.saveGame().activityMarkerFlags = 0;
        es.saveGame().activeCheatIds.clear();
        es.saveGame().statistics[1].rawValue = 0;
        es.saveGame().statistics[1].denominatorStatId = 0;

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
        CHECK(es.saveGame().garageNextId == 9);
        CHECK(es.saveGame().garageVehicles.size() == 1 && es.saveGame().garageVehicles[0].recordId == 3);
        CHECK(es.saveGame().wardrobeItems.size() == 2);
        CHECK(es.saveGame().wardrobeItems[0].itemId == 0x55AA55AAu);
        CHECK(es.saveGame().wardrobeItems[0].colour0 == 0xFF && es.saveGame().wardrobeItems[0].colour1 == 0xFF &&
              es.saveGame().wardrobeItems[0].colour2 == 0xFF);
        CHECK(es.saveGame().wardrobeItems[1].itemId == 0x77u);
        CHECK(es.saveGame().wardrobeItems[1].colour0 == 2 && es.saveGame().wardrobeItems[1].colour1 == 0xFF &&
              es.saveGame().wardrobeItems[1].colour2 == 4);
        CHECK(es.saveGame().wardrobeItems[1].wearOption == 5);
        CHECK(es.saveGame().wardrobeItems[1].flag);
        CHECK(es.saveGame().weaponInventory.size() == 2);
        CHECK(es.saveGame().weaponInventory[0].weaponDefId == 0x0BADF00Du);
        CHECK(es.saveGame().weaponInventory[0].ammo[0] == 12 && es.saveGame().weaponInventory[0].ammo[1] == 48 &&
              es.saveGame().weaponInventory[0].ammo[2] == 1 && es.saveGame().weaponInventory[0].ammo[3] == 0);
        CHECK(es.saveGame().weaponInventory[0].flag0 == 0 && es.saveGame().weaponInventory[0].flag1 == 0);
        CHECK(es.saveGame().weaponInventory[1].weaponDefId == 0xCAFEBABEu);
        CHECK(es.saveGame().weaponInventory[1].ammo[0] == 7 && es.saveGame().weaponInventory[1].ammo[1] == -5 &&
              es.saveGame().weaponInventory[1].ammo[2] == 99 && es.saveGame().weaponInventory[1].ammo[3] == 3);
        CHECK(es.saveGame().weaponInventory[1].flag0 == 1 && es.saveGame().weaponInventory[1].flag1 == 1);
        CHECK(es.saveGame().regionRecords.size() == 1);
        CHECK(es.saveGame().regionRecords[0].handleLo == 0x9000u && es.saveGame().regionRecords[0].handleHi == 0xA000u &&
              es.saveGame().regionRecords[0].controlled);
        CHECK(es.saveGame().activityRecords.size() == 1);
        CHECK(es.saveGame().activityRecords[0].nameHash == 0xCCCC3333u && es.saveGame().activityRecords[0].levelsCompleted == 1 &&
              es.saveGame().activityRecords[0].completedMask == 0x0F && es.saveGame().activityRecords[0].secondMask == 0x05);
        CHECK(es.saveGame().activityMarkerFlags == 0xFEDCBA9876543210ull);
        CHECK(es.saveGame().activeCheatIds.size() == 2 && es.saveGame().activeCheatIds[0] == 99u && es.saveGame().activeCheatIds[1] == 100u);
        CHECK(es.saveGame().statistics[1].rawValue == 555u && es.saveGame().statistics[1].denominatorStatId == 0u);

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
