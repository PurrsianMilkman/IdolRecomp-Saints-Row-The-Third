// Free-roam save side, save availability, new game and directory -
// spec-save-format.md A.2, A.3, 6.1 and 15.1/15.2. See
// include/sr3saveflow/freeroam_flow.h.

#include "sr3saveflow/freeroam_flow.h"

#include <cstring>

#include "sr3save/save_crc.h"
#include "sr3save/save_snapshot.h"

namespace sr3saveflow {

ObjectHandle NearestOwnedCrib(const std::string& zoneName, const Vec3& player,
                              const std::vector<CribLocation>& cribs) {
    if (zoneName != kCityZone) return {};
    ObjectHandle best;
    double bestD2 = 0.0;
    bool found = false;
    for (const CribLocation& c : cribs) {
        if (c.locked) continue;
        const double dx = c.position.x - player.x;
        const double dy = c.position.y - player.y;
        const double dz = c.position.z - player.z;
        const double d2 = dx * dx + dy * dy + dz * dz;
        if (!found || d2 < bestD2) {
            best = c.handle;
            bestD2 = d2;
            found = true;
        }
    }
    return best;
}

void PatchFreeRoamSave(std::vector<uint8_t>& image, const FreeRoamSaveState& state) {
    WriteZoneName(image, state.zoneName);
    // The header routine writes an invalid (zero) handle first, then the
    // nearest owned crib when there is one.
    WriteRespawnCrib(image, ObjectHandle{});
    const ObjectHandle crib = NearestOwnedCrib(state.zoneName, state.playerPosition, state.cribPositions);
    if (!crib.isZero()) WriteRespawnCrib(image, crib);
    WriteClock(image, state.clock);
    WriteCash(image, state.cashRaw);
    WriteRespect(image, state.respectTotal, state.respectInLevel, state.respectLevel);
    WriteRespectMultiplier(image, state.respectMultiplier);
    WriteCribBlock(image, state.cribs);
    StampSnapshotHash(image);
}

PauseSaveOutcome PauseMenuSave(const PauseMenuContext& ctx) {
    if (ctx.hordeMode || ctx.machinimaRecording) return PauseSaveOutcome::NotOffered;
    if (ctx.autosaveInProgress) return PauseSaveOutcome::RefusedAutosaving;
    if (ctx.inMission && !ctx.missionWarningShown) return PauseSaveOutcome::MissionWarning;
    if (ctx.inActivity && !ctx.activityWarningShown) return PauseSaveOutcome::ActivityWarning;
    return PauseSaveOutcome::Proceed;
}

SlotSaveOutcome SlotScreenSave(bool newSlot, bool slotAvailable, bool spaceAvailable) {
    if (!newSlot) return SlotSaveOutcome::ConfirmOverwrite;
    if (!slotAvailable) return SlotSaveOutcome::NotEnoughSlots;
    if (!spaceAvailable) return SlotSaveOutcome::NotEnoughSpace;
    return SlotSaveOutcome::Save;
}

bool AutosaveAllowed(AutosaveSource source, const AutosaveGate& gate) {
    if (!gate.startupFinished || gate.alreadySaving) return false;
    if (gate.sessionBlocked || gate.menuBlocked) return false;
    if (source == AutosaveSource::GameAutosave && gate.missionActive) return false;
    return true;
}

std::vector<uint8_t> BuildNewGameSnapshot(const NewGameOptions& options) {
    sr3save::SaveSnapshotFields f;
    f.levelName = kCityZone;
    f.difficulty = options.difficulty;
    f.cashRaw = ClampCash(options.startingCashRaw);
    std::vector<uint8_t> image = sr3save::SaveSnapshot::build(f, false);

    GameClock clock = options.baseClock;
    SetClockTimeOfDay(clock, 10, 0, 0);
    clock.playTime = 0.0f;
    WriteClock(image, clock);
    WriteRespawnCrib(image, ObjectHandle{});
    WriteRespect(image, 0, 0, 0);
    WriteRespectMultiplier(image, 1.0f);

    // A.3 does not list the crib block. These values are the real new-game
    // save's (spec-save-format.md 10.5 data: the "cribs" variable 0 only in the
    // brand-new-game save, 10/10 cribs locked with A = B = 0), in the pool
    // order and with the handles of the A.3 map. CHOSEN (data-backed).
    CribBlock cribs;
    cribs.cribsVariable = 0;
    for (const KnownCrib& k : kKnownCribs) {
        CribRecord r;
        r.handle = k.handle;
        r.locked = true;
        r.flagByteRaw = 1;
        cribs.records.push_back(r);
    }
    WriteCribBlock(image, cribs);
    StampSnapshotHash(image);
    return image;
}

sr3save::SlotRecord MakeSlotRecord(const std::vector<uint8_t>& snapshotImage, bool autosave) {
    const sr3save::SaveSnapshot snap =
        sr3save::SaveSnapshot::parse(sr3save::ByteView(snapshotImage.data(), snapshotImage.size()));
    sr3save::SlotRecord rec;
    rec.active = true;
    rec.summary = snap.deriveDirectorySummary(autosave);
    rec.summaryRaw = sr3save::encodeSlotSummary(rec.summary);
    return rec;
}

std::vector<uint8_t> BuildSaveDirectory(const std::array<sr3save::SlotRecord, sr3save::kSlotCount>& slots,
                                        const std::array<bool, sr3save::kSlotCount>& excluded) {
    std::vector<uint8_t> out(sr3save::kSaveDirectorySize, 0);
    uint32_t count = 0;
    for (size_t i = 0; i < sr3save::kSlotCount; ++i) {
        if (!slots[i].active || excluded[i]) continue;  // zeroed whole
        uint8_t* r = out.data() + sr3save::kSlotTableOffset + sr3save::kSlotRecordStride * i;
        r[0] = 1;
        std::memcpy(r + 1, slots[i].summaryRaw.data(), sr3save::kSlotSummarySize);
        ++count;
    }
    for (int k = 0; k < 4; ++k) out[4 + k] = static_cast<uint8_t>(count >> (8 * k));
    const uint32_t h = sr3save::computeDirectoryHash(vpp::ByteView(out.data(), out.size()));
    for (int k = 0; k < 4; ++k) out[k] = static_cast<uint8_t>(h >> (8 * k));
    return out;
}

}  // namespace sr3saveflow
