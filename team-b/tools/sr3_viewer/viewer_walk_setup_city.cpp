// runWalkDemo() split: city archive and cell discovery.
// Code moved verbatim out of the former single-function runWalkDemo() (see viewer_walk_session.h).
#include "viewer_platform.h"
#include "viewer_walk_session.h"

namespace sr3viewer {

int walkSetupCityArchives(WalkSession& sess) {
    auto& cityArchivePaths = sess.cityArchivePaths;
    auto& archives = sess.archives;
    auto& cells = sess.cells;
    auto& cellByName = sess.cellByName;
    // ---- city ground (byte-for-byte the SAME streaming as `city`) -------------
    for (const auto& path : cityArchivePaths) {
        auto a = std::make_unique<CityArchive>();
        a->path = path;
        {
            const size_t slash = path.find_last_of("/\\");
            a->label = slash == std::string::npos ? path : path.substr(slash + 1);
        }
        a->bytes = mapFileLargeC(path);
        if (a->bytes.empty()) {
            std::fprintf(stderr, "could not read %s\n", path.c_str());
            return 1;
        }
        a->top = std::make_unique<vpp::Container>(vpp::ByteView(a->bytes.data(), a->bytes.size()));
        const size_t archiveIndex = archives.size();
        for (size_t i = 0; i < a->top->entries().size(); ++i) {
            const std::string& en = a->top->entries()[i].name;
            int col = 0, row = 0, n = 0;
            if (parseDefaultHnEntryName(en, col, row, n)) {
                CityCell c;
                c.name = en.substr(0, en.size() - 8);
                c.entryName = en;
                c.archiveIndex = archiveIndex;
                c.entryIndex = i;
                c.col = col;
                c.row = row;
                c.n = n;
                cityFineCellCentre(col, row, n, c.centreX, c.centreZ);
                const std::string key = toLowerLocal(c.name);
                if (cellByName.count(key)) continue; // first archive on the command line wins
                cellByName[key] = cells.size();
                cells.push_back(c);
            }
        }
        archives.push_back(std::move(a));
    }
    if (cells.empty()) {
        std::fprintf(stderr, "no CCRRhN.str2_pc containers found\n");
        return 1;
    }
    return -1;
}

}  // namespace sr3viewer
