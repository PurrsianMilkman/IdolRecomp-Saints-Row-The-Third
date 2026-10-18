// Synthetic tests for sr3saveflow. Every expected value comes from the TEXT
// of spec-save-format.md ("Free-roam load and save flow" A.1-A.4, 6.1, 10.5,
// 15.1/15.2) and spec-lua-api-behaviour.md 48.1, never from this project's
// own source.
//
// Style follows tests/synthetic_tables_progression_test.cpp.

#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "sr3save/save_directory.h"
#include "sr3save/save_snapshot.h"
#include "sr3saveflow/freeroam_flow.h"
#include "sr3saveflow/snapshot_blocks.h"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        ++g_checks;                                                        \
        if (!(cond)) {                                                     \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":"        \
                      << __LINE__ << "\n";                                 \
            ++g_failures;                                                 \
        }                                                                   \
    } while (0)

using namespace sr3saveflow;

void run(const char* name, void (*fn)()) {
    int before = g_failures;
    fn();
    if (g_failures != before) std::cerr << "  (in " << name << ")\n";
}

void PutU32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[off + i] = static_cast<uint8_t>(v >> (8 * i));
}
uint32_t GetU32(const std::vector<uint8_t>& b, size_t off) {
    return static_cast<uint32_t>(b[off]) | (static_cast<uint32_t>(b[off + 1]) << 8) |
           (static_cast<uint32_t>(b[off + 2]) << 16) | (static_cast<uint32_t>(b[off + 3]) << 24);
}
void PutU16(std::vector<uint8_t>& b, size_t off, uint16_t v) {
    b[off] = static_cast<uint8_t>(v);
    b[off + 1] = static_cast<uint8_t>(v >> 8);
}

const ObjectHandle kSaintsHq{0x0E998295u, 0x004280E4u};

// The A.4 input, byte for byte: 0x1A8E8 bytes, all zero except the listed
// fields.
std::vector<uint8_t> A4Input() {
    std::vector<uint8_t> b(0x1A8E8, 0);
    PutU32(b, 0x008, 94);
    PutU32(b, 0x00C, 1);
    PutU32(b, 0x010, 0x000F0F6E);
    std::memcpy(b.data() + 0x014, "sr3_city", 8);
    b[0x08C + 4] = 14;
    b[0x08C + 5] = 30;
    b[0x08C + 6] = 0;
    PutU32(b, 0x08C + 0xC, 0x9AAAAAAAu);
    const float play = 3600.0f;
    uint32_t pu;
    std::memcpy(&pu, &play, 4);
    PutU32(b, 0x09C, pu);
    PutU32(b, 0x0A0, 3600);
    PutU32(b, 0x058, 0x0E998295u);
    PutU32(b, 0x05C, 0x004280E4u);
    PutU16(b, 0x4E60, 1);
    PutU16(b, 0x4E62, 10);
    for (size_t i = 0; i < 10; ++i) {
        const size_t r = 0x4E68 + 24 * i;
        PutU32(b, r, kKnownCribs[i].handle.lo);
        PutU32(b, r + 4, kKnownCribs[i].handle.hi);
        if (i == 9) {
            b[r + 8] = 0;
            PutU32(b, r + 0xC, 1);
        } else {
            b[r + 8] = 1;
        }
    }
    PutU32(b, 0x4418, 10000);
    PutU32(b, 0x441C, 850);
    PutU32(b, 0x4420, 4);
    PutU32(b, 0x4A70, 500000);
    PutU32(b, 0x4A5C, 0x3F800000u);
    PutU32(b, 0x2B64, 1);
    return b;
}

WorldTransform At(double x, double y, double z) {
    WorldTransform t;
    t.position = {x, y, z};
    return t;
}

// A world whose crib objects are the ten A.3 cribs, with a player-start
// marker at a separate point.
LoadWorld CribWorld() {
    LoadWorld w;
    w.resolveHandle = [](const ObjectHandle& h) -> std::optional<WorldTransform> {
        const int i = KnownCribIndex(h);
        if (i < 0) return std::nullopt;
        return At(kKnownCribs[i].x, kKnownCribs[i].y, kKnownCribs[i].z);
    };
    w.playerStartMarker = []() -> std::optional<WorldTransform> { return At(1, 2, 3); };
    return w;
}

// ===========================================================================
void testClock() {
    CHECK(TimeOfDayFixed(14, 30, 0) == 0x9AAAAAAAu);
    CHECK(TimeOfDayFixed(0, 0, 0) == 0u);
    CHECK(std::fabs(TimeOfDaySeconds(0x9AAAAAAAu) - 52200.0) < 0.001);
    GameClock c;
    c.year = 2011;
    c.month = 10;
    c.day = 15;
    c.weekday = 4;
    c.moonRaw = {7, 0, 0, 0};
    SetClockTimeOfDay(c, 10, 0, 0);
    c.playTime = 948.5f;
    uint8_t raw[20];
    c.encode(raw);
    CHECK(raw[0] == (2011 & 0xFF) && raw[1] == (2011 >> 8));
    CHECK(raw[2] == 10 && raw[3] == 15 && raw[4] == 10 && raw[5] == 0 && raw[7] == 4 && raw[8] == 7);
    const GameClock d = GameClock::decode(raw);
    CHECK(d.year == 2011 && d.month == 10 && d.day == 15 && d.hour == 10);
    CHECK(d.timeOfDayFixed == TimeOfDayFixed(10, 0, 0));
    CHECK(d.playTime == 948.5f);
}

void testA4Load() {
    const std::vector<uint8_t> img = A4Input();
    LoadWorld w = CribWorld();
    // A "tss01" exists in this world, but the mission-object count is 0, so
    // branch (a) is skipped.
    w.findMissionStart = [](const std::string& n) -> std::optional<std::pair<ObjectHandle, WorldTransform>> {
        if (n != "tss01") return std::nullopt;
        return std::make_pair(ObjectHandle{0x11, 0x22}, At(9, 9, 9));
    };
    const LoadPlan p = PlanFreeRoamLoad(img, w);
    CHECK(p.versionOk);
    CHECK(p.zoneName == "sr3_city");

    // Order: streaming wait, resets, version gate, entries 0..31, finished,
    // post-start wait.
    CHECK(p.steps.size() == 3 + 32 + 2);
    CHECK(p.steps[0].kind == LoadStepKind::StreamingWait);
    CHECK(p.steps[1].kind == LoadStepKind::SubsystemResets);
    CHECK(p.steps[2].kind == LoadStepKind::VersionGate);
    for (int e = 0; e < 32; ++e) {
        CHECK(p.steps[3 + e].kind == LoadStepKind::ApplyEntry);
        CHECK(p.steps[3 + e].entry == e);
    }
    CHECK(p.steps[35].kind == LoadStepKind::LoadFinished);
    CHECK(p.steps[36].kind == LoadStepKind::PostStartStreamingWait);

    CHECK(p.clock.hour == 14 && p.clock.minute == 30 && p.clock.second == 0);
    CHECK(p.clock.timeOfDayFixed == 0x9AAAAAAAu);
    CHECK(p.playTime.has_value() && *p.playTime == 3600.0);

    CHECK(p.placement.source == PlacementSource::RespawnCrib);
    CHECK(p.placement.handle == kSaintsHq);
    CHECK(p.placement.transform && p.placement.transform->position.x == 96.874 &&
          p.placement.transform->position.z == 1053.672);
    CHECK(p.streamCentre && p.streamCentre->x == 96.874);

    CHECK(p.cashRaw == 500000);  // $5,000.00
    CHECK(p.respectTotal == 10000 && p.respectInLevel == 850 && p.respectLevel == 4);
    CHECK(p.respectMultiplier == 1.0f);
    CHECK(p.difficulty == 1);
    CHECK(p.regionControlled.empty());  // no takeover region owned
    CHECK(p.cribs.cribsVariable == 1);
    CHECK(p.cribs.records.size() == 10);
    for (size_t i = 0; i < 10; ++i) CHECK(p.cribs.records[i].locked == (i != 9));
    CHECK(KnownCribIndex(p.cribs.records[9].handle) == 9);
    CHECK(std::string(kKnownCribs[9].name) == "Crib_SaintsHQ");
}

void testPlacementBranches() {
    std::vector<uint8_t> img = A4Input();
    const ObjectHandle tss{0x11, 0x22};
    LoadWorld w = CribWorld();
    w.findMissionStart = [tss](const std::string&) -> std::optional<std::pair<ObjectHandle, WorldTransform>> {
        return std::make_pair(tss, At(9, 9, 9));
    };
    // A mission-object record with the "tss01" handle and flag bit 2 clear.
    PutU32(img, 0x37E8, 1);
    PutU32(img, 0x37F0, tss.lo);
    PutU32(img, 0x37F4, tss.hi);
    PutU32(img, 0x37F8, 0x3);
    LoadPlan p = PlanFreeRoamLoad(img, w);
    CHECK(p.placement.source == PlacementSource::MissionStart);
    CHECK(p.placement.transform->position.x == 9.0);
    // Bit 2 set: falls through to the respawn crib.
    PutU32(img, 0x37F8, 0x4);
    p = PlanFreeRoamLoad(img, w);
    CHECK(p.placement.source == PlacementSource::RespawnCrib);
    // The override can be skipped.
    PutU32(img, 0x37F8, 0x0);
    LoadOptions skip;
    skip.tryMissionStartOverride = false;
    CHECK(PlanFreeRoamLoad(img, w, skip).placement.source == PlacementSource::RespawnCrib);

    // Zero handle: the player-start marker.
    img = A4Input();
    PutU32(img, 0x058, 0);
    PutU32(img, 0x05C, 0);
    p = PlanFreeRoamLoad(img, CribWorld());
    CHECK(p.placement.source == PlacementSource::PlayerStartMarker);
    CHECK(p.placement.transform->position.x == 1.0);
    CHECK(p.streamCentre->z == 3.0);
    // Unresolvable handle: the marker too.
    PutU32(img, 0x058, 0xDEAD);
    CHECK(PlanFreeRoamLoad(img, CribWorld()).placement.source == PlacementSource::PlayerStartMarker);

    // Checkpoint: no placement, play time keeps running.
    img = A4Input();
    LoadOptions cp;
    cp.kind = LoadKind::Checkpoint;
    p = PlanFreeRoamLoad(img, CribWorld(), cp);
    CHECK(p.placement.source == PlacementSource::None);
    CHECK(!p.playTime.has_value());
    CHECK(p.cashRaw == 500000);
    // A guest in someone else's session is not placed.
    LoadOptions guest;
    guest.sessionHostOrNoSession = false;
    CHECK(PlanFreeRoamLoad(img, CribWorld(), guest).placement.source == PlacementSource::None);

    // Version other than 94: the driver is skipped silently; the start
    // point is the registered marker.
    PutU32(img, 0x008, 93);
    p = PlanFreeRoamLoad(img, CribWorld());
    CHECK(!p.versionOk);
    CHECK(p.steps.size() == 4);
    CHECK(p.placement.source == PlacementSource::None);
    CHECK(p.streamCentre && p.streamCentre->x == 1.0);
    CHECK(p.respectMultiplier == 1.0f);  // only the reset ran
}

void testZeroMultiplierPitfall() {
    std::vector<uint8_t> img = A4Input();
    PutU32(img, 0x4A5C, 0);
    // Applied verbatim: a zero-filled multiplier loads as 0.0.
    CHECK(PlanFreeRoamLoad(img, CribWorld()).respectMultiplier == 0.0f);
}

void testCashClamp() {
    CHECK(ClampCash(3000000000LL) == 2000000000);
    CHECK(ClampCash(-3000000000LL) == -2000000000);
    CHECK(ClampCash(12345) == 12345);
}

void testSave() {
    CHECK(NearestOwnedCrib("sr3_city", {}, {}).isZero());
    std::vector<CribLocation> cribs;
    for (const KnownCrib& k : kKnownCribs) cribs.push_back({k.handle, true, {k.x, k.y, k.z}});
    cribs[9].locked = false;  // Saints HQ owned
    const Vec3 nearDock{431.0, 8.0, 936.0};
    // Only Saints HQ is owned: it wins even with the player at the dock.
    CHECK(NearestOwnedCrib("sr3_city", nearDock, cribs) == kSaintsHq);
    cribs[3].locked = false;  // Dock_Purchase owned too
    CHECK(NearestOwnedCrib("sr3_city", nearDock, cribs) == kKnownCribs[3].handle);
    CHECK(NearestOwnedCrib("other_zone", nearDock, cribs).isZero());

    std::vector<uint8_t> img = A4Input();
    img[0x2000] = 0xAB;  // an unmodelled span
    img[0x4E68 + 24 * 2 + 0x14] = 0x5A;  // an unmodelled crib-record byte
    FreeRoamSaveState st;
    st.playerPosition = nearDock;
    st.cribPositions = cribs;
    st.clock = SnapshotBlocks::read(img).clock;
    SetClockTimeOfDay(st.clock, 21, 15, 30);
    st.clock.playTime = 4000.75f;
    st.cashRaw = 3000000000LL;
    st.respectTotal = 12000;
    st.respectInLevel = 100;
    st.respectLevel = 5;
    st.respectMultiplier = 1.25f;
    st.cribs = SnapshotBlocks::read(img).cribs;
    st.cribs.records[3].locked = false;
    st.cribs.records[3].a = 1;
    PatchFreeRoamSave(img, st);

    const SnapshotBlocks b = SnapshotBlocks::read(img);
    CHECK(b.respawnCrib == kKnownCribs[3].handle);
    CHECK(b.clock.hour == 21 && b.clock.minute == 15 && b.clock.second == 30);
    CHECK(b.clock.playTime == 4000.75f);
    CHECK(b.playTimeSeconds == 4000);  // truncated
    CHECK(b.cashRaw == 2000000000);
    CHECK(b.respectTotal == 12000 && b.respectLevel == 5);
    CHECK(b.respectMultiplier == 1.25f);
    CHECK(!b.cribs.records[3].locked && b.cribs.records[3].a == 1);
    CHECK(img[0x2000] == 0xAB);
    CHECK(img[0x4E68 + 24 * 2 + 0x14] == 0x5A);
    const sr3save::SaveSnapshot snap = sr3save::SaveSnapshot::parse(sr3save::ByteView(img.data(), img.size()));
    CHECK(snap.hashValid());
    CHECK(snap.levelName() == "sr3_city");

    // Round trip through the load plan.
    const LoadPlan p = PlanFreeRoamLoad(img, CribWorld());
    CHECK(p.placement.source == PlacementSource::RespawnCrib);
    CHECK(p.placement.transform->position.x == kKnownCribs[3].x);
    CHECK(p.playTime && *p.playTime == 4000.0);

    // No owned crib: 0x058 is written zero.
    for (CribLocation& c : st.cribPositions) c.locked = true;
    PatchFreeRoamSave(img, st);
    CHECK(SnapshotBlocks::read(img).respawnCrib.isZero());
    CHECK(GetU32(img, 0x058) == 0 && GetU32(img, 0x05C) == 0);
}

void testNewGame() {
    NewGameOptions o;
    o.baseClock.year = 2011;
    o.baseClock.day = 3;
    SetClockTimeOfDay(o.baseClock, 23, 59, 59);
    o.baseClock.playTime = 99.0f;
    const std::vector<uint8_t> img = BuildNewGameSnapshot(o);
    CHECK(img.size() == 0x1A8E8);
    const SnapshotBlocks b = SnapshotBlocks::read(img);
    CHECK(b.zoneName == "sr3_city");
    CHECK(b.clock.year == 2011 && b.clock.day == 3);
    CHECK(b.clock.hour == 10 && b.clock.minute == 0 && b.clock.second == 0);
    CHECK(b.clock.timeOfDayFixed == TimeOfDayFixed(10, 0, 0));
    CHECK(b.clock.playTime == 0.0f && b.playTimeSeconds == 0);
    CHECK(b.respectTotal == 0 && b.respectInLevel == 0 && b.respectLevel == 0);
    CHECK(b.respectMultiplier == 1.0f);
    CHECK(b.cashRaw == 5700);
    CHECK(b.respawnCrib.isZero());
    CHECK(b.cribs.cribsVariable == 0);
    CHECK(b.cribs.records.size() == 10);
    for (size_t i = 0; i < 10; ++i) {
        CHECK(b.cribs.records[i].locked);
        CHECK(b.cribs.records[i].a == 0 && b.cribs.records[i].b == 0);
        CHECK(b.cribs.records[i].handle == kKnownCribs[i].handle);
    }
    const sr3save::SaveSnapshot snap = sr3save::SaveSnapshot::parse(sr3save::ByteView(img.data(), img.size()));
    CHECK(snap.hashValid());
    CHECK(snap.formatVersion() == 94);
    CHECK(snap.difficultyIndex() == 1);
    const LoadPlan p = PlanFreeRoamLoad(img, CribWorld());
    CHECK(p.placement.source == PlacementSource::PlayerStartMarker);
    CHECK(p.respectMultiplier == 1.0f);

    NewGameOptions zero;
    zero.startingCashRaw = 0;
    CHECK(SnapshotBlocks::read(BuildNewGameSnapshot(zero)).cashRaw == 0);
}

void testAvailability() {
    PauseMenuContext c;
    CHECK(PauseMenuSave(c) == PauseSaveOutcome::Proceed);
    c.hordeMode = true;
    CHECK(PauseMenuSave(c) == PauseSaveOutcome::NotOffered);
    c.hordeMode = false;
    c.machinimaRecording = true;
    CHECK(PauseMenuSave(c) == PauseSaveOutcome::NotOffered);
    c.machinimaRecording = false;
    c.autosaveInProgress = true;
    CHECK(PauseMenuSave(c) == PauseSaveOutcome::RefusedAutosaving);
    c.autosaveInProgress = false;
    c.inMission = true;
    CHECK(PauseMenuSave(c) == PauseSaveOutcome::MissionWarning);
    c.missionWarningShown = true;
    CHECK(PauseMenuSave(c) == PauseSaveOutcome::Proceed);
    c.inActivity = true;
    CHECK(PauseMenuSave(c) == PauseSaveOutcome::ActivityWarning);
    c.activityWarningShown = true;
    CHECK(PauseMenuSave(c) == PauseSaveOutcome::Proceed);

    CHECK(SlotScreenSave(false, false, false) == SlotSaveOutcome::ConfirmOverwrite);
    CHECK(SlotScreenSave(true, false, true) == SlotSaveOutcome::NotEnoughSlots);
    CHECK(SlotScreenSave(true, true, false) == SlotSaveOutcome::NotEnoughSpace);
    CHECK(SlotScreenSave(true, true, true) == SlotSaveOutcome::Save);

    AutosaveGate g;
    CHECK(AutosaveAllowed(AutosaveSource::GameAutosave, g));
    g.missionActive = true;
    CHECK(!AutosaveAllowed(AutosaveSource::GameAutosave, g));
    CHECK(AutosaveAllowed(AutosaveSource::MissionAutosave, g));
    g.missionActive = false;
    g.startupFinished = false;
    CHECK(!AutosaveAllowed(AutosaveSource::GameAutosave, g));
    CHECK(!AutosaveAllowed(AutosaveSource::MissionAutosave, g));
    g.startupFinished = true;
    g.alreadySaving = true;
    CHECK(!AutosaveAllowed(AutosaveSource::MissionAutosave, g));
    g.alreadySaving = false;
    g.sessionBlocked = true;
    CHECK(!AutosaveAllowed(AutosaveSource::GameAutosave, g));
}

void testDirectory() {
    std::array<sr3save::SlotRecord, sr3save::kSlotCount> slots{};
    // An empty directory hashes to 0.
    std::vector<uint8_t> d = BuildSaveDirectory(slots);
    CHECK(d.size() == 0x260);
    CHECK(GetU32(d, 0) == 0 && GetU32(d, 4) == 0);

    const std::vector<uint8_t> img = BuildNewGameSnapshot();
    slots[0] = MakeSlotRecord(img, true);
    slots[3] = MakeSlotRecord(img, false);
    slots[5].active = false;
    slots[5].summaryRaw.fill(0x77);  // inactive: zeroed whole
    std::array<bool, sr3save::kSlotCount> excluded{};
    excluded[3] = true;  // excluded: zeroed whole
    d = BuildSaveDirectory(slots, excluded);
    CHECK(GetU32(d, 4) == 1);
    for (size_t i = 0; i < 25; ++i) {
        CHECK(d[0x008 + 25 * 3 + i] == 0);
        CHECK(d[0x008 + 25 * 5 + i] == 0);
    }
    const sr3save::SaveDirectory dir =
        sr3save::SaveDirectory::parse(vpp::ByteView(d.data(), d.size()), sr3save::SaveDirectory::HashPolicy::Enforce);
    CHECK(dir.hashValid());
    CHECK(dir.activeSlotCount() == 1);
    CHECK(dir.occupiedSlotIndices() == std::vector<int>{0});
    CHECK(dir.slots()[0].summaryRaw == slots[0].summaryRaw);
}

}  // namespace

int main() {
    run("testClock", testClock);
    run("testA4Load", testA4Load);
    run("testPlacementBranches", testPlacementBranches);
    run("testZeroMultiplierPitfall", testZeroMultiplierPitfall);
    run("testCashClamp", testCashClamp);
    run("testSave", testSave);
    run("testNewGame", testNewGame);
    run("testAvailability", testAvailability);
    run("testDirectory", testDirectory);
    if (g_failures != 0) {
        std::cerr << g_failures << " of " << g_checks << " checks FAILED.\n";
        return 1;
    }
    std::cout << g_checks << " checks passed.\n";
    return 0;
}
