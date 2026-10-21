// Host-side save-file helpers for sr3.exe (FIRST PLAYABLE). See include/sr3game/frontend.h.
// CHOSEN (not spec'd): the save directory and the file naming. The save CONTENT is the spec'd
// spec-save-format.md snapshot written by sr3saveflow (viewer_playtest_saveflow.cpp).
#include "sr3game/frontend.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#include "sr3save/errors.h"
#include "sr3save/save_snapshot.h"
#include "sr3saveflow/freeroam_flow.h"
#include "sr3saveflow/snapshot_blocks.h"

namespace fs = std::filesystem;

namespace sr3game {

std::string DefaultSaveDirectory() {
    // CHOSEN: %USERPROFILE%\Documents\SR3Recomp\saves ; falls back to ".\saves" next to the working dir.
    const char* home = std::getenv("USERPROFILE");
    if (home && *home) return (fs::path(home) / "Documents" / "SR3Recomp" / "saves").string();
    return (fs::path(".") / "saves").string();
}

std::vector<SaveFileInfo> ListSaveFiles(const std::string& dir) {
    std::vector<SaveFileInfo> out;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return out;
    for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec) || e.path().extension() != ".sr3s_pc") continue;
        SaveFileInfo s;
        s.path = e.path().string();
        s.displayName = e.path().stem().string();
        const auto ft = e.last_write_time(ec);
        // file clock -> system clock (C++17 has no clock_cast): shift by the current clock offset.
        const auto sys = std::chrono::system_clock::now() +
                         std::chrono::duration_cast<std::chrono::system_clock::duration>(
                             ft - fs::file_time_type::clock::now());
        s.lastWriteUnix = std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
        std::ifstream in(e.path(), std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (bytes.size() == sr3save::kSaveSnapshotSize) {
            try {
                const sr3saveflow::LoadPlan plan = sr3saveflow::PlanFreeRoamLoad(bytes, sr3saveflow::LoadWorld{});
                const sr3saveflow::SnapshotBlocks b = sr3saveflow::SnapshotBlocks::read(bytes);
                s.valid = plan.versionOk;
                s.playTimeSeconds = b.playTimeSeconds;
                s.cashRaw = b.cashRaw;
                s.respectLevel = b.respectLevel;
            } catch (const sr3save::FormatError&) {
                s.valid = false;
            }
        }
        out.push_back(std::move(s));
    }
    std::sort(out.begin(), out.end(), [](const SaveFileInfo& a, const SaveFileInfo& b) {
        return a.lastWriteUnix != b.lastWriteUnix ? a.lastWriteUnix > b.lastWriteUnix : a.path < b.path;
    });
    return out;
}

std::string NextSavePath(const std::string& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    for (int n = 1; n <= 99; ++n) {
        char name[32];
        std::snprintf(name, sizeof(name), "sr3save_%02d.sr3s_pc", n);
        const fs::path p = fs::path(dir) / name;
        if (!fs::exists(p, ec)) return p.string();
    }
    return (fs::path(dir) / "sr3save_99.sr3s_pc").string();  // all 99 taken: overwrite the last
}

}  // namespace sr3game
