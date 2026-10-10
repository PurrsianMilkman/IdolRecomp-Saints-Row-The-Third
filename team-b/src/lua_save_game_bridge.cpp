#include "sr3luahost/save_game_bridge.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "sr3io/output_path_guard.h"
#include "sr3luahost/engine_state.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace sr3luahost {

namespace {

namespace fs = std::filesystem;

unsigned long currentProcessId() {
#ifdef _WIN32
    return static_cast<unsigned long>(::GetCurrentProcessId());
#else
    return static_cast<unsigned long>(::getpid());
#endif
}

// Cash is stored in the save as an int32 x100, clamped to +-2,000,000,000
// (spec-save-format.md Sec10.3). EngineState::playerCash() is a plain
// dollar double with no such clamp (store natives just add/subtract
// prices) - this is the one place that boundary is enforced, matching the
// real clamping setter's own documented ceiling rather than letting an
// out-of-range double wrap silently through a naive static_cast.
int32_t toCashRaw(double dollars) {
    double raw = dollars * 100.0;
    if (raw > 2000000000.0) raw = 2000000000.0;
    if (raw < -2000000000.0) raw = -2000000000.0;
    return static_cast<int32_t>(raw);
}

// "HH:MM:SS" / "MM.DD.YY" from the real OS wall clock (spec-save-format.md
// Sec6.3: the header serializer uses the C runtime's own time/date calls;
// Sec3's own parse patterns are exactly "%02d:%02d:%02d" / "%02d.%02d.%02d").
void formatSaveTimeDate(std::string& timeOut, std::string& dateOut) {
    std::time_t now = std::time(nullptr);
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &now);
#else
    localtime_r(&now, &tmBuf);
#endif
    char timeBuf[9];  // "HH:MM:SS\0"
    char dateBuf[9];  // "MM.DD.YY\0"
    std::snprintf(timeBuf, sizeof timeBuf, "%02d:%02d:%02d", tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec);
    std::snprintf(dateBuf, sizeof dateBuf, "%02d.%02d.%02d", tmBuf.tm_mon + 1, tmBuf.tm_mday, (tmBuf.tm_year + 1900) % 100);
    timeOut = timeBuf;
    dateOut = dateBuf;
}

} // namespace

std::string defaultSaveGameDirectory() {
    std::error_code ec;
    fs::path base = fs::temp_directory_path(ec);
    if (ec) base = fs::path("."); // defensive only - never throws
    fs::path dir = base / ("crreish_sr3_saves_" + std::to_string(currentProcessId()));
    return dir.generic_string();
}

sr3save::SaveSnapshotFields buildSaveSnapshotFields(EngineState& st) {
    sr3save::SaveSnapshotFields f;
    const SaveGameState& sg = st.saveGame();
    f.levelName = sg.levelName;
    f.labelIndex = sg.activeMissionLabelIndex;
    f.difficulty = sg.difficulty;
    f.cheatsUsed = sg.cheatsUsed ? 1 : 0;
    f.barnstormsFound = sg.barnstormsFound;
    f.stuntJumpsFound = sg.stuntJumpsFound;
    f.respectTotal = sg.respectTotal;
    f.respectInLevel = sg.respectInLevel;
    f.playerLevel = sg.playerLevel;
    f.cashRaw = toCashRaw(st.playerCash());
    f.collectibles.reserve(sg.collectibles.size());
    for (const CollectedCollectible& c : sg.collectibles) {
        f.collectibles.push_back({c.handleLo, c.handleHi, c.group, 0});
    }
    formatSaveTimeDate(f.saveTime, f.saveDate);
    return f;
}

void applySaveSnapshotFields(const sr3save::SaveSnapshot& snap, EngineState& st) {
    SaveGameState& sg = st.saveGame();
    sg.respectTotal = snap.respectTotal();
    sg.respectInLevel = snap.respectInLevel();
    sg.playerLevel = snap.playerLevel();
    sg.difficulty = snap.difficultyIndex();
    sg.cheatsUsed = snap.cheatsUsed();
    sg.barnstormsFound = snap.barnstormsFound();
    sg.stuntJumpsFound = snap.stuntJumpsFound();
    sg.collectibles.clear();
    sg.collectibles.reserve(snap.collectibles().size());
    for (const sr3save::CollectibleRecord& c : snap.collectibles()) {
        sg.collectibles.push_back({c.handleLo, c.handleHi, c.group});
    }
    // Sec15's own task brief: display-only on load, round-tripped as inert
    // data - never used to resume a mission.
    sg.activeMissionLabelIndex = snap.labelIndex();
    sg.levelName = snap.levelName();
    // cashDiv100() is signed integer division of the raw x100 value
    // (Sec10.3) - the exact inverse of toCashRaw()'s *100.0 above.
    st.playerCash() = static_cast<double>(snap.cashDiv100());
}

bool writeAutosaveSnapshot(EngineState& st, std::string& outPath, std::string& error) {
    try {
        sr3save::SaveSnapshotFields fields = buildSaveSnapshotFields(st);
        std::vector<uint8_t> bytes = sr3save::SaveSnapshot::build(fields, /*computeHash=*/true);

        fs::path dir = fs::path(st.saveGameDirectory());
        // Slot 0: the real directory's own autosave-slot convention
        // (spec-save-format.md Sec8.2, "autosave... 1 in slot 0 of every
        // real directory").
        fs::path path = dir / "sr3save_00.sr3s_pc";

        // Checked BEFORE touching the filesystem at all (not after
        // create_directories()) - a refused write must leave no trace,
        // not even an empty directory, anywhere near a protected root
        // (sr3io::isOutputPathSafe's own top note: the 2026-10-08 incident
        // this guard exists for was exactly a writer creating output where
        // it had no business doing so).
        if (!sr3io::isOutputPathSafe(path.string(), ".sr3s_pc", sr3io::kMaxOverwriteBytesSr3s, error)) {
            st.lastAutosaveError() = error;
            return false;
        }

        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec) {
            error = "could not create save directory '" + dir.string() + "': " + ec.message();
            st.lastAutosaveError() = error;
            return false;
        }

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "could not open '" + path.string() + "' for writing";
            st.lastAutosaveError() = error;
            return false;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            error = "write to '" + path.string() + "' failed";
            st.lastAutosaveError() = error;
            return false;
        }
        out.close();

        outPath = path.string();
        ++st.autosaveFileWriteCount();
        st.lastAutosaveFilePath() = outPath;
        st.lastAutosaveError().clear();
        return true;
    } catch (const std::exception& e) {
        // Never let a C++ exception escape into a Lua-calling frame (this
        // project's own hard rule, lua_spec_confirmed_stubs_common.h's top
        // note: "NO LUA ERROR MAY CROSS A C++ FRAME" - an uncaught
        // std::exception here is the same class of hazard).
        error = std::string("writeAutosaveSnapshot: ") + e.what();
        st.lastAutosaveError() = error;
        return false;
    }
}

std::vector<uint8_t> buildCheckpointSnapshotBytes(EngineState& st) {
    sr3save::SaveSnapshotFields fields = buildSaveSnapshotFields(st);
    return sr3save::SaveSnapshot::build(fields, /*computeHash=*/false);
}

bool applyCheckpointSnapshot(EngineState& st) {
    const std::optional<std::vector<uint8_t>>& snap = st.lastCheckpointSnapshot();
    if (!snap.has_value()) return false;
    try {
        sr3save::SaveSnapshot parsed = sr3save::SaveSnapshot::parse(sr3save::ByteView(snap->data(), snap->size()));
        applySaveSnapshotFields(parsed, st);
        return true;
    } catch (const sr3save::FormatError&) {
        return false; // defensive only - our own builder never produces an unparseable buffer
    }
}

bool loadSaveSnapshotFile(const std::string& path, EngineState& st, std::string& error) {
    try {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in) {
            error = "could not open '" + path + "' for reading";
            return false;
        }
        std::streamsize size = in.tellg();
        if (size < 0) {
            error = "could not determine size of '" + path + "'";
            return false;
        }
        in.seekg(0);
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        if (!bytes.empty() && !in.read(reinterpret_cast<char*>(bytes.data()), size)) {
            error = "failed reading '" + path + "'";
            return false;
        }
        sr3save::SaveSnapshot parsed = sr3save::SaveSnapshot::parse(sr3save::ByteView(bytes.data(), bytes.size()));
        applySaveSnapshotFields(parsed, st);
        return true;
    } catch (const sr3save::FormatError& e) {
        error = std::string("loadSaveSnapshotFile: ") + e.what();
        return false;
    } catch (const std::exception& e) {
        error = std::string("loadSaveSnapshotFile: ") + e.what();
        return false;
    }
}

} // namespace sr3luahost
