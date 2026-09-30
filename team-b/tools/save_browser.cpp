// Save browser: given a directory containing savedir.sr3d_pc and
// sr3save_NN.sr3s_pc files, enumerates occupied save slots and prints
// each one's level/zone name, save date, save time, and play time in seconds
// - the "what does this save contain, at a glance" view
// spec-save-format.md documents as fully supported (Sec2 + Sec3's
// confirmed fields only; the gameplay-state bulk of each snapshot is out
// of scope, per spec Sec3's closing note).
//
// Usage: save_browser <directory containing savedir.sr3d_pc>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "sr3save/save_directory.h"
#include "sr3save/save_snapshot.h"

namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        throw std::runtime_error("could not open file: " + path);
    }
    std::streamsize size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    if (size > 0 && !f.read(reinterpret_cast<char*>(buf.data()), size)) {
        throw std::runtime_error("failed reading file: " + path);
    }
    return buf;
}

std::string slotFilename(int slotIndex) {
    std::ostringstream oss;
    oss << "sr3save_" << std::setfill('0') << std::setw(2) << slotIndex << ".sr3s_pc";
    return oss.str();
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: save_browser <directory containing savedir.sr3d_pc>\n";
        return 1;
    }

    fs::path dir(argv[1]);

    try {
        std::vector<uint8_t> dirBytes = readFile((dir / "savedir.sr3d_pc").string());
        sr3save::SaveDirectory directory =
            sr3save::SaveDirectory::parse(sr3save::ByteView(dirBytes.data(), dirBytes.size()));

        std::vector<int> occupied = directory.occupiedSlotIndices();
        std::cout << "savedir.sr3d_pc: " << directory.activeSlotCount()
                  << " active slot(s) of " << sr3save::kSlotCount << "\n\n";

        int failures = 0;
        for (int slot : occupied) {
            std::string filename = slotFilename(slot);
            fs::path snapshotPath = dir / filename;
            std::cout << "Slot " << std::setfill('0') << std::setw(2) << slot << " ("
                      << filename << "):\n";
            try {
                std::vector<uint8_t> snapBytes = readFile(snapshotPath.string());
                sr3save::SaveSnapshot snap = sr3save::SaveSnapshot::parse(
                    sr3save::ByteView(snapBytes.data(), snapBytes.size()));

                std::cout << "  Level:     " << snap.levelName() << "\n";
                std::cout << "  Date:      " << snap.saveDate() << "\n";
                std::cout << "  Time:      " << snap.saveTime() << "\n";
                std::cout << "  Play time: " << snap.playTimeSeconds() << " s ("
                          << snap.playTimeMinutes() << " min; whole game-clock seconds, "
                          << "spec-save-format.md Sec6.2, HANDOFF Sec9.74)\n";
                std::cout << "  Activity records: " << snap.activityRecordCount()
                          << " x 16 bytes\n";
            } catch (const std::exception& ex) {
                std::cerr << "  [FAIL] could not read/parse " << filename << ": " << ex.what()
                          << "\n";
                ++failures;
            }
            std::cout << "\n";
        }

        return failures > 0 ? 2 : 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
