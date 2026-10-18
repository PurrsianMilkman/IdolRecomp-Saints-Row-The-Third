#include "viewer_platform.h"
#include "viewer_modes.h"
#include "viewer_city_support.h"
#include "viewer_common.h"
#include "sr3render/device.h"
#include "sr3render/mesh_renderer.h"
#include "sr3render/png_writer.h"
#include "sr3render/quad_renderer.h"
#include "sr3render/sun_shadow_map.h"
#include "sr3render/swap_chain.h"
#include "sr3render/tod_clock.h"
#include "sr3render/tod_lighting.h"
#include "sr3render/window.h"
#include "sr3render/texture_upload.h"
#include "sr3zone/zone_geometry.h"
#include "sr3zone/zone_header.h"
#include "sr3zone/zone_objects.h"
#include "sr3zone/traffic_graph.h"
#include "sr3zone/hood_volumes.h"
#include "sr3zone/road_graph.h"
#include "sr3zone/zone_heightmap.h"
#include "vpp/container.h"

using namespace sr3viewer;

namespace {

// "CCRR^<name>h<N>.str2_pc" - a sub-area/state variant of a fine cell
// (Sec10.3). Only counted, never loaded (see the top comment).
bool looksLikeHnVariantEntryName(const std::string& entryName) {
    if (!endsWithNoCase(entryName, ".str2_pc")) return false;
    const std::string stem = entryName.substr(0, entryName.size() - 8);
    if (stem.size() < 7 || stem.find('^') == std::string::npos) return false;
    const char h = stem[stem.size() - 2];
    const char d = stem[stem.size() - 1];
    return (h == 'h' || h == 'H') && d >= '0' && d <= '3';
}

} // namespace



int runCity(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: sr3_viewer city <sr3_city_0.vpp_pc> [<sr3_city_1.vpp_pc> ...]\n"
                     "                        [--list] [--lattice-check]\n"
                     "                        [--eye X Y Z] [--yaw D] [--pitch D] [--speed U]\n"
                     "                        [--fly-to X Z] [--no-input] [--frames N]\n"
                     "                        [--capture <out.png>] [--capture-frame N]\n"
                     "                        [--capture-at N <out.png>]... [--size W H]\n"
                     "                        [--tint] [--no-textures] [--no-outlines] [--far F]\n"
                     "                        [--window-scale S] [--visible]\n"
                     "       Multi-cell STREAMING viewer for the real hN fine cells (engine\n"
                     "       level 4, 'Zone (High LOD)'): every frame the wanted set is the\n"
                     "       cells whose 160x140 m box overlaps a 159.5x139.5 m window around\n"
                     "       the fly camera's (x,z) (spec-world-streaming.md Sec10.4/Sec11);\n"
                     "       newly-wanted cells are loaded, dropped cells evicted immediately,\n"
                     "       each logged to stdout as [stream] LOAD/EVICT with the frame number.\n"
                     "       Cells sit at their REAL world position (Sec10.6(g) lattice); the\n"
                     "       props INSIDE a cell are on a FAKE local grid (real within-cell\n"
                     "       placement is unresolved - see runCity()'s top comment).\n"
                     "       --list         print every hN container (parsed col/row/N, centre)\n"
                     "                      per archive and exit.\n"
                     "       --lattice-check  re-measure the CC=col/RR=row parse against each\n"
                     "                      tile's own .czh_pc fast-header extents and exit.\n"
                     "       --fly-to X Z   scripted flight: eye moves linearly from --eye to\n"
                     "                      (X, eyeY, Z) over --frames frames.\n"
                     "       --no-input     ignore the keyboard (deterministic scripted runs).\n"
                     "       --tint         flat colour per cell instead of textures.\n"
                     "       --window-scale S  DEBUG override: scale the level-4 window by S for a\n"
                     "                      wide overview capture (NOT the engine's rule; flagged).\n"
                     "       --ground       OPT-IN (default off, so pre-existing goldens here e.g.\n"
                     "                      city_corner stay byte-for-byte unaffected): also render\n"
                     "                      each wanted tile's real ground/terrain geometry (see\n"
                     "                      CityGroundTile's own doc comment) - MaterialIdAsColour,\n"
                     "                      no real texture binding exists for zone data.\n"
                     "       Fly controls (GetAsyncKeyState, same as `scene`): W/A/S/D move,\n"
                     "       Q/E down/up, arrows look, Shift = faster, Escape closes.\n");
        return 1;
    }

    std::vector<std::string> archivePaths;
    int argi = 2;
    while (argi < argc && std::strncmp(argv[argi], "--", 2) != 0) archivePaths.push_back(argv[argi++]);
    bool listOnly = false, latticeCheck = false, noInput = false, tintMode = false, textures = true,
         outlines = true;
    // --ground (2026-10-07 follow-up task, OPT-IN, default OFF): renders
    // each wanted tile's real ground/terrain geometry (CityGroundTile, see
    // its own "GROUND/TERRAIN" top comment) alongside the hN prop cells.
    // Default OFF so this command's own pre-existing golden scenes (e.g.
    // city_corner) stay byte-for-byte unaffected without this flag - the
    // ground mechanism itself is exercised/frozen via `walk --v2` instead
    // (see runWalkDemo()'s "FIX 4" doc comment), which is unconditional for
    // that command once --v2 is given.
    bool groundEnabled = false;
    uint32_t winWidth = 1400, winHeight = 900;
    int maxFrames = -1;
    int captureFrame = 0;
    std::string capturePath;
    std::vector<std::pair<int, std::string>> captureAt;
    float eye[3] = {0.0f, 60.0f, 0.0f};
    bool eyeGiven = false;
    float yaw = 0.0f, pitch = -0.35f;
    float moveSpeed = 60.0f; // m/s, CHOSEN for city scale
    bool flyTo = false;
    float flyTarget[2] = {0.0f, 0.0f};
    float farZ = 3000.0f;
    float windowScale = 1.0f; // 1 = the engine's level-4 window (Sec10.3); anything else is a labelled DEBUG override
    bool visible = false; // off by default - see sr3render/window.h's own doc comment
    for (; argi < argc; ++argi) {
        const std::string arg = argv[argi];
        if (arg == "--list") listOnly = true;
        else if (arg == "--lattice-check") latticeCheck = true;
        else if (arg == "--no-input") noInput = true;
        else if (arg == "--tint") tintMode = true;
        else if (arg == "--no-textures") textures = false;
        else if (arg == "--no-outlines") outlines = false;
        else if (arg == "--size" && argi + 2 < argc) {
            winWidth = static_cast<uint32_t>(std::atoi(argv[++argi]));
            winHeight = static_cast<uint32_t>(std::atoi(argv[++argi]));
        } else if (arg == "--frames" && argi + 1 < argc) {
            maxFrames = std::atoi(argv[++argi]);
        } else if (arg == "--capture" && argi + 1 < argc) {
            capturePath = argv[++argi];
        } else if (arg == "--capture-frame" && argi + 1 < argc) {
            captureFrame = std::atoi(argv[++argi]);
        } else if (arg == "--capture-at" && argi + 2 < argc) {
            const int f = std::atoi(argv[++argi]);
            captureAt.emplace_back(f, argv[++argi]);
        } else if (arg == "--eye" && argi + 3 < argc) {
            eye[0] = static_cast<float>(std::atof(argv[++argi]));
            eye[1] = static_cast<float>(std::atof(argv[++argi]));
            eye[2] = static_cast<float>(std::atof(argv[++argi]));
            eyeGiven = true;
        } else if (arg == "--yaw" && argi + 1 < argc) {
            yaw = static_cast<float>(std::atof(argv[++argi])) * 3.14159265f / 180.0f;
        } else if (arg == "--pitch" && argi + 1 < argc) {
            pitch = static_cast<float>(std::atof(argv[++argi])) * 3.14159265f / 180.0f;
        } else if (arg == "--speed" && argi + 1 < argc) {
            moveSpeed = static_cast<float>(std::atof(argv[++argi]));
        } else if (arg == "--fly-to" && argi + 2 < argc) {
            flyTo = true;
            flyTarget[0] = static_cast<float>(std::atof(argv[++argi]));
            flyTarget[1] = static_cast<float>(std::atof(argv[++argi]));
        } else if (arg == "--far" && argi + 1 < argc) {
            farZ = static_cast<float>(std::atof(argv[++argi]));
        } else if (arg == "--window-scale" && argi + 1 < argc) {
            windowScale = static_cast<float>(std::atof(argv[++argi]));
            if (windowScale <= 0.0f) windowScale = 1.0f;
        } else if (arg == "--ground") {
            groundEnabled = true;
        } else if (arg == "--visible") {
            visible = true;
        } else {
            std::fprintf(stderr, "unrecognised argument '%s'\n", arg.c_str());
            return 2;
        }
    }
    if (!capturePath.empty()) captureAt.emplace_back(captureFrame, capturePath);
    if (archivePaths.empty()) {
        std::fprintf(stderr, "no archive given\n");
        return 2;
    }
    if (flyTo && maxFrames <= 0) {
        std::fprintf(stderr, "--fly-to needs --frames N (the flight is spread over N frames)\n");
        return 2;
    }

    // ---- Open every archive and enumerate its real hN containers from the
    // archive's own directory (top-level entries). ---------------------------
    std::vector<std::unique_ptr<CityArchive>> archives;
    std::vector<CityCell> cells;
    std::map<std::string, size_t> cellByName;
    for (const auto& path : archivePaths) {
        auto a = std::make_unique<CityArchive>();
        a->path = path;
        {
            const size_t slash = path.find_last_of("/\\");
            a->label = slash == std::string::npos ? path : path.substr(slash + 1);
        }
        const double t0 = cityNowMs();
        a->bytes = mapFileLargeC(path);
        if (a->bytes.empty()) {
            std::fprintf(stderr, "could not read %s\n", path.c_str());
            return 1;
        }
        a->top = std::make_unique<vpp::Container>(vpp::ByteView(a->bytes.data(), a->bytes.size()));
        size_t defaults = 0, variants = 0, dupes = 0;
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
                if (cellByName.count(key)) {
                    ++dupes; // first archive on the command line wins
                    std::printf("[city] %s: duplicate %s (already from %s) - ignored\n", a->label.c_str(),
                                en.c_str(), archives[cells[cellByName[key]].archiveIndex]->label.c_str());
                    continue;
                }
                cellByName[key] = cells.size();
                cells.push_back(c);
                ++defaults;
            } else if (looksLikeHnVariantEntryName(en)) {
                ++variants;
            }
        }
        std::printf("[city] %s: %zu top-level entries, %zu default CCRRhN cells, %zu CCRR^name hN variants "
                    "(not loaded), %zu duplicates; read in %.1f s\n",
                    a->label.c_str(), a->top->entries().size(), defaults, variants, dupes,
                    (cityNowMs() - t0) / 1000.0);
        archives.push_back(std::move(a));
    }
    if (cells.empty()) {
        std::fprintf(stderr, "no CCRRhN.str2_pc containers found in the given archive(s)\n");
        return 1;
    }

    if (listOnly) {
        std::printf("[list] name      archive              col row N  fineCol fineRow  centre_x  centre_z\n");
        for (const auto& c : cells) {
            std::printf("[list] %-9s %-20s %3d %3d %d  %7d %7d  %8.1f  %8.1f\n", c.name.c_str(),
                        archives[c.archiveIndex]->label.c_str(), c.col, c.row, c.n, 2 * c.col + (c.n & 1),
                        2 * c.row + (c.n >> 1), static_cast<double>(c.centreX), static_cast<double>(c.centreZ));
        }
        return 0;
    }

    if (latticeCheck) {
        // Sec10.6(g)'s own method, re-run on THIS archive's tiles: midpoint of
        // each tile's placed-object extents (fast `.czh_pc` header origin +
        // SR3Z record position words - header only, never `.czn_pc`) against
        // the formula, under both the CC=col/RR=row parse and the swapped one.
        std::set<std::pair<int, int>> tiles;
        std::map<std::pair<int, int>, size_t> tileArchive;
        for (const auto& c : cells) {
            tiles.insert({c.col, c.row});
            tileArchive[{c.col, c.row}] = c.archiveIndex;
        }
        std::printf("[lattice] tile  records  measured_mid_x  measured_mid_z | CC=col: dx dz | swapped: dx dz\n");
        std::vector<double> adx, adz, sdx, sdz;
        for (const auto& t : tiles) {
            char nm[64];
            std::snprintf(nm, sizeof(nm), "sr3_city~f%02d%02d.czh_pc", t.first, t.second);
            std::vector<uint8_t> czh, unused;
            const CityArchive& a = *archives[tileArchive[t]];
            if (!findEntry(*a.top, nm, czh, unused, "") || czh.empty()) {
                std::printf("[lattice] %02d%02d  (no %s in %s)\n", t.first, t.second, nm, a.label.c_str());
                continue;
            }
            sr3zone::ZoneHeader zh;
            try {
                zh = sr3zone::ZoneHeader::parse(vpp::ByteView(czh.data(), czh.size()));
            } catch (const std::exception& ex) {
                std::printf("[lattice] %02d%02d  %s parse failed: %s\n", t.first, t.second, nm, ex.what());
                continue;
            }
            if (zh.records().empty()) {
                std::printf("[lattice] %02d%02d  0 records - no extents to measure\n", t.first, t.second);
                continue;
            }
            float mnx = 1e30f, mxx = -1e30f, mnz = 1e30f, mxz = -1e30f;
            for (const auto& r : zh.records()) {
                const auto p = sr3zone::RecordPosition(r);
                const float x = p[0] + zh.headerOrigin()[0];
                const float z = p[2] + zh.headerOrigin()[2];
                mnx = (std::min)(mnx, x);
                mxx = (std::max)(mxx, x);
                mnz = (std::min)(mnz, z);
                mxz = (std::max)(mxz, z);
            }
            const float midX = (mnx + mxx) * 0.5f, midZ = (mnz + mxz) * 0.5f;
            float px, pz, sx, sz;
            cityTileCentre(t.first, t.second, px, pz);
            cityTileCentre(t.second, t.first, sx, sz);
            adx.push_back(std::fabs(midX - px));
            adz.push_back(std::fabs(midZ - pz));
            sdx.push_back(std::fabs(midX - sx));
            sdz.push_back(std::fabs(midZ - sz));
            std::printf("[lattice] %02d%02d  %7zu  %14.1f  %14.1f | %7.1f %7.1f | %8.1f %8.1f\n", t.first, t.second,
                        zh.records().size(), static_cast<double>(midX), static_cast<double>(midZ),
                        static_cast<double>(midX - px), static_cast<double>(midZ - pz),
                        static_cast<double>(midX - sx), static_cast<double>(midZ - sz));
        }
        auto median = [](std::vector<double> v) {
            if (v.empty()) return 0.0;
            std::sort(v.begin(), v.end());
            return v[v.size() / 2];
        };
        std::printf("[lattice] %zu tiles measured. median |residual|: CC=col parse dx=%.2f dz=%.2f m;  "
                    "swapped parse dx=%.2f dz=%.2f m\n",
                    adx.size(), median(adx), median(adz), median(sdx), median(sdz));
        return 0;
    }

    if (!eyeGiven) {
        // Default start: above the first enumerated cell (CHOSEN, not data).
        eye[0] = cells.front().centreX;
        eye[2] = cells.front().centreZ;
    }
    const float flyStart[2] = {eye[0], eye[2]};

    std::printf(
        "=====================================================================\n"
        "CELL POSITIONS ARE REAL, PROP POSITIONS INSIDE A CELL ARE NOT.\n"
        "Each hN fine cell is drawn at its real world centre from the\n"
        "spec-world-streaming.md Sec10.6(g) lattice (validated to ~2 m MAD).\n"
        "The props INSIDE each cell are on an ARBITRARY local grid fitted to\n"
        "the cell's 160x140 m footprint - real per-instance placement of\n"
        ".clmesh_pc objects was investigated and did NOT resolve (see\n"
        "runClmeshTile()'s top comment / tools/tile1018_placement_probe.cpp).\n"
        "=====================================================================\n");
    if (windowScale != 1.0f)
        std::printf("WARNING  : --window-scale %.2f is a DEBUG OVERRIDE for overview captures - the wanted\n"
                    "           set is NOT the engine's level-4 rule (window 159.5 x 139.5 m) in this run.\n",
                    static_cast<double>(windowScale));

    std::string error;
    sr3render::Window window;
    if (!window.create("SR3 Viewer - city (hN streaming)", winWidth, winHeight, error, visible)) {
        std::fprintf(stderr, "window creation failed: %s\n", error.c_str());
        return 1;
    }
    sr3render::RenderDevice device;
    if (!device.initialise(16, 16, error)) {
        std::fprintf(stderr, "device init failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("device   : %s (%s)\n",
                device.kind() == sr3render::DeviceKind::Hardware ? "HARDWARE" : "WARP (software)",
                device.adapterName().c_str());
    sr3render::SwapChain swapChain;
    if (!swapChain.create(device.device(), window.nativeHandle(), winWidth, winHeight, error)) {
        std::fprintf(stderr, "swap chain creation failed: %s\n", error.c_str());
        return 1;
    }
    // A persistent renderer that owns the shared depth buffer (cell renderers
    // come and go with streaming) and draws the focus-window outline.
    sr3render::MeshRenderer focusBox;
    if (!uploadCityOutline(focusBox, device.device(), 0.0f, 0.0f, kLevel4WindowX * 0.5f, kLevel4WindowZ * 0.5f,
                           false, error) ||
        !focusBox.createDepth(device.device(), winWidth, winHeight, error)) {
        std::fprintf(stderr, "focus/depth renderer failed: %s\n", error.c_str());
        return 1;
    }
    uint32_t depthWidth = winWidth, depthHeight = winHeight;

    std::map<size_t, std::unique_ptr<CityLoadedCell>> loaded;
    size_t totalLoads = 0, totalEvicts = 0;
    // Real ground/terrain, reconciled at TILE granularity - see
    // loadCityGroundTile()'s/CityGroundTile's own top comment above
    // ("GROUND/TERRAIN") for the root cause and why this is NOT the same
    // thing as the hN prop cells above.
    std::map<std::pair<int, int>, std::unique_ptr<CityGroundTile>> groundTiles;

    auto loadCell = [&](size_t cellIndex, int frame) {
        const CityCell& c = cells[cellIndex];
        CityArchive& a = *archives[c.archiveIndex];
        auto lc = std::make_unique<CityLoadedCell>();
        lc->cellIndex = cellIndex;
        const double t0 = cityNowMs();
        try {
            vpp::Container str2 = a.top->openNested(c.entryIndex);
            std::vector<std::string> clNames;
            for (const auto& e : str2.entries())
                if (endsWithNoCase(e.name, ".clmesh_pc")) clNames.push_back(e.name);
            lc->propEntries = clNames.size();
            for (const auto& nm : clNames) {
                CityProp p;
                std::string why;
                if (loadCityProp(str2, nm, archives, c.archiveIndex, device.device(), textures && !tintMode, p,
                                 why)) {
                    lc->texturesWanted += p.texturesWanted;
                    lc->texturesFound += p.texturesFound;
                    lc->texturesFoundElsewhere += p.texturesFoundElsewhere;
                    lc->props.push_back(std::move(p));
                } else {
                    ++lc->propsFailed;
                    std::printf("[stream]   %s: prop %s SKIPPED: %s\n", c.name.c_str(), nm.c_str(), why.c_str());
                }
            }
        } catch (const std::exception& ex) {
            std::printf("[stream]   %s: container open failed: %s\n", c.name.c_str(), ex.what());
        }
        // FAKE local grid fitted to the cell's real footprint (see the top
        // comment) - slot order = the container's own entry order.
        const size_t m = lc->props.size();
        if (m > 0) {
            const int cols = (std::max)(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(m) *
                                                                                 kFineCellHalfX / kFineCellHalfZ))));
            const int rows = static_cast<int>((m + static_cast<size_t>(cols) - 1) / static_cast<size_t>(cols));
            const float slotX = 2.0f * kFineCellHalfX / static_cast<float>(cols);
            const float slotZ = 2.0f * kFineCellHalfZ / static_cast<float>(rows);
            for (size_t i = 0; i < m; ++i) {
                CityProp& p = lc->props[i];
                const int ci = static_cast<int>(i % static_cast<size_t>(cols));
                const int ri = static_cast<int>(i / static_cast<size_t>(cols));
                const float sx = c.centreX - kFineCellHalfX + (static_cast<float>(ci) + 0.5f) * slotX;
                const float sz = c.centreZ + kFineCellHalfZ - (static_cast<float>(ri) + 0.5f) * slotZ;
                const float lcx = (p.localMin[0] + p.localMax[0]) * 0.5f;
                const float lcz = (p.localMin[2] + p.localMax[2]) * 0.5f;
                if (p.localMax[0] - p.localMin[0] > slotX || p.localMax[2] - p.localMin[2] > slotZ)
                    ++lc->propsOverflowSlot;
                sr3render::buildWorldMatrix(sx - lcx, 0.0f, sz - lcz, 0.0f, 1.0f, p.world);
            }
        }
        // Flat colour for --tint (CHOSEN palette), keyed by TILE so a tile's
        // four fine cells share one colour and the half-tile row stagger
        // shows up as a brick pattern: same-row neighbours differ in col%3,
        // neighbours in the rows above/below differ in row parity.
        static const float kPalette[6][3] = {{0.90f, 0.35f, 0.30f}, {0.30f, 0.75f, 0.35f}, {0.30f, 0.50f, 0.95f},
                                             {0.95f, 0.80f, 0.25f}, {0.80f, 0.40f, 0.85f}, {0.25f, 0.85f, 0.85f}};
        const int pi = (c.col % 3) + 3 * (c.row % 2);
        for (int k = 0; k < 3; ++k) lc->tint[k] = kPalette[pi][k];
        if (outlines) {
            lc->outline = std::make_unique<sr3render::MeshRenderer>();
            std::string oerr;
            if (!uploadCityOutline(*lc->outline, device.device(), c.centreX, c.centreZ, kFineCellHalfX,
                                   kFineCellHalfZ, true, oerr))
                lc->outline.reset();
        }
        lc->loadMs = cityNowMs() - t0;
        ++totalLoads;
        std::printf("[stream] frame %d LOAD  %-8s (%s, col %d row %d N %d) centre (%.1f, %.1f): %zu/%zu props "
                    "uploaded, %zu failed, %zu larger than their fake-grid slot, textures %zu/%zu (%zu of them "
                    "from another archive), %.0f ms\n",
                    frame, c.name.c_str(), a.label.c_str(), c.col, c.row, c.n, static_cast<double>(c.centreX),
                    static_cast<double>(c.centreZ), lc->props.size(), lc->propEntries, lc->propsFailed,
                    lc->propsOverflowSlot, lc->texturesFound, lc->texturesWanted, lc->texturesFoundElsewhere,
                    lc->loadMs);
        loaded[cellIndex] = std::move(lc);
    };

    // The reconciliation (Sec10.4/Sec11.2/Sec11.4): one box for both
    // directions, release dropped cells immediately, then acquire every new
    // cell in the same pass (no budget).
    std::vector<size_t> lastWanted;
    bool firstReconcile = true;
    auto reconcile = [&](int frame) {
        std::vector<size_t> wanted = cityWantedCells(cells, eye[0], eye[2], windowScale);
        if (windowScale == 1.0f && wanted.size() > kLevel4Capacity) {
            std::printf("[stream] frame %d WARNING: %zu cells overlap the level-4 window - Sec10.4 says this "
                        "cannot exceed %zu; lattice/window constants are wrong somewhere\n",
                        frame, wanted.size(), kLevel4Capacity);
        }
        if (!firstReconcile && wanted == lastWanted) return;
        firstReconcile = false;
        lastWanted = wanted;
        std::string names;
        for (size_t idx : wanted) names += (names.empty() ? "" : ",") + cells[idx].name;
        std::printf("[stream] frame %d focus (%.1f, %.1f): wanted = {%s}\n", frame, static_cast<double>(eye[0]),
                    static_cast<double>(eye[2]), names.c_str());
        const std::set<size_t> wantedSet(wanted.begin(), wanted.end());
        for (auto it = loaded.begin(); it != loaded.end();) {
            if (wantedSet.count(it->first) == 0) {
                const CityCell& c = cells[it->first];
                std::printf("[stream] frame %d EVICT %-8s (col %d row %d N %d) - %zu props released\n", frame,
                            c.name.c_str(), c.col, c.row, c.n, it->second->props.size());
                it = loaded.erase(it); // MeshRenderer destructors release the GPU buffers here, immediately
                ++totalEvicts;
            } else {
                ++it;
            }
        }
        for (size_t idx : wanted)
            if (loaded.count(idx) == 0) loadCell(idx, frame);

        // Ground/terrain reconciliation, at TILE granularity (one entry per
        // unique (col,row) among the wanted hN cells - a tile's ground is
        // shared by all 4 of its fine cells). See the "GROUND/TERRAIN" top
        // comment above CityGroundTile for the root cause this fixes. Gated
        // on --ground (default OFF, see that flag's own usage-text comment
        // above): this command's own pre-existing goldens (e.g. city_corner)
        // never pass --ground, so they must see groundTiles stay empty and
        // this whole block skipped, not merely the draw call below - the
        // per-cell "LOAD"/"SKIPPED" stdout lines this block prints are
        // themselves part of city_corner's frozen baseline_stdout.txt, so
        // running this unconditionally would change that golden's stdout
        // even if the image stayed identical.
        if (groundEnabled) {
            std::set<std::pair<int, int>> wantedTiles;
            for (size_t idx : wanted) wantedTiles.insert({cells[idx].col, cells[idx].row});
            for (auto it = groundTiles.begin(); it != groundTiles.end();) {
                if (wantedTiles.count(it->first) == 0) {
                    std::printf("[ground] frame %d EVICT tile %02d%02d\n", frame, it->first.first, it->first.second);
                    it = groundTiles.erase(it);
                } else {
                    ++it;
                }
            }
            for (const auto& t : wantedTiles) {
                if (groundTiles.count(t)) continue;
                auto gt = std::make_unique<CityGroundTile>();
                std::string why;
                if (loadCityGroundTile(t.first, t.second, archives, device.device(), *gt, why)) {
                    std::printf("[ground] frame %d LOAD  tile %02d%02d: %s, %zu range(s), %u triangles\n", frame,
                                t.first, t.second, gt->cznName.c_str(), gt->rangesRendered, gt->triangles);
                } else {
                    std::printf("[ground] frame %d tile %02d%02d: SKIPPED (no real ground rendered): %s\n", frame,
                                t.first, t.second, why.c_str());
                }
                groundTiles[t] = std::move(gt); // cached either way - never retried every frame
            }
        }
        std::fflush(stdout);
    };

    std::printf("camera   : free-fly, start eye (%.1f %.1f %.1f) yaw %.1f pitch %.1f%s "
                "(W/A/S/D move, Q/E down/up, arrows look, Shift=fast, Esc=quit)\n",
                static_cast<double>(eye[0]), static_cast<double>(eye[1]), static_cast<double>(eye[2]),
                static_cast<double>(yaw * 180.0f / 3.14159265f), static_cast<double>(pitch * 180.0f / 3.14159265f),
                noInput ? " [--no-input: keyboard ignored]" : "");
    if (flyTo)
        std::printf("camera   : scripted flight (%.1f, %.1f) -> (%.1f, %.1f) over %d frames\n",
                    static_cast<double>(flyStart[0]), static_cast<double>(flyStart[1]),
                    static_cast<double>(flyTarget[0]), static_cast<double>(flyTarget[1]), maxFrames);

    LARGE_INTEGER freq{}, prevTime{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prevTime);
    int frames = 0;
    while (window.pumpMessages()) {
        if (!swapChain.resize(window.width(), window.height(), error)) {
            std::fprintf(stderr, "swap chain resize failed: %s\n", error.c_str());
            break;
        }
        if (swapChain.width() != depthWidth || swapChain.height() != depthHeight) {
            if (focusBox.createDepth(device.device(), swapChain.width(), swapChain.height(), error)) {
                depthWidth = swapChain.width();
                depthHeight = swapChain.height();
            }
        }
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        float dt = static_cast<float>(static_cast<double>(now.QuadPart - prevTime.QuadPart) /
                                      static_cast<double>(freq.QuadPart));
        prevTime = now;
        if (dt > 0.25f || dt < 0.0f) dt = 0.0f;

        if (flyTo) {
            const float t = maxFrames > 1 ? static_cast<float>(frames) / static_cast<float>(maxFrames - 1) : 1.0f;
            eye[0] = flyStart[0] + (flyTarget[0] - flyStart[0]) * t;
            eye[2] = flyStart[1] + (flyTarget[1] - flyStart[1]) * t;
        }
        if (!noInput) {
            // Same polled-input fly camera as runScene().
            const bool fast = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            const float speed = moveSpeed * (fast ? 3.0f : 1.0f) * dt;
            const float lookSpeed = 1.6f * dt;
            if (GetAsyncKeyState(VK_LEFT) & 0x8000) yaw -= lookSpeed;
            if (GetAsyncKeyState(VK_RIGHT) & 0x8000) yaw += lookSpeed;
            if (GetAsyncKeyState(VK_UP) & 0x8000) pitch += lookSpeed;
            if (GetAsyncKeyState(VK_DOWN) & 0x8000) pitch -= lookSpeed;
            float forward[3] = {std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
            float right[3] = {-forward[2], 0.0f, forward[0]};
            const float rightLen = std::sqrt(right[0] * right[0] + right[2] * right[2]);
            if (rightLen > 0.0f) { right[0] /= rightLen; right[2] /= rightLen; }
            if (GetAsyncKeyState('W') & 0x8000) for (int c = 0; c < 3; ++c) eye[c] += forward[c] * speed;
            if (GetAsyncKeyState('S') & 0x8000) for (int c = 0; c < 3; ++c) eye[c] -= forward[c] * speed;
            if (GetAsyncKeyState('D') & 0x8000) for (int c = 0; c < 3; ++c) eye[c] += right[c] * speed;
            if (GetAsyncKeyState('A') & 0x8000) for (int c = 0; c < 3; ++c) eye[c] -= right[c] * speed;
            if (GetAsyncKeyState('E') & 0x8000) eye[1] += speed;
            if (GetAsyncKeyState('Q') & 0x8000) eye[1] -= speed;
        }
        // CHOSEN limit just short of straight down (+-89.4 deg) so a top-down
        // overview works; buildFreeViewProjection degenerates at exactly 90.
        const float pitchLimit = 1.56f;
        if (pitch > pitchLimit) pitch = pitchLimit;
        if (pitch < -pitchLimit) pitch = -pitchLimit;

        reconcile(frames);

        float viewProjection[16];
        sr3render::buildFreeViewProjection(eye, yaw, pitch,
                                           static_cast<float>(swapChain.width()) /
                                               static_cast<float>(swapChain.height()),
                                           0.5f, farZ, viewProjection);
        swapChain.bind(device.context());
        ID3D11RenderTargetView* rtv = swapChain.renderTargetView();
        device.context()->OMSetRenderTargets(1, &rtv, focusBox.depthView());
        swapChain.clear(device.context(), 0.05f, 0.05f, 0.08f, 1.0f);
        focusBox.clearDepth(device.context());

        // Real ground/terrain (see CityGroundTile's own "GROUND/TERRAIN" top
        // comment) - drawn before the props so the depth-tested opaque props
        // composite over it normally; positions decoded straight from
        // sr3zone::ZoneGeometry are already real world-space coordinates
        // (verified directly against this scene's own loaded props/outline
        // positions - no separate per-tile world matrix is needed, same as
        // every other already-world-placed renderer in this loop that passes
        // nullptr for `world`). No real material/texture binding exists for
        // zone data (runZoneTile()'s own documented dead end) - coloured by
        // real materialId, never a fabricated texture. Gated on --ground,
        // same reasoning as reconcile()'s own gate above - groundTiles is
        // always empty when it's off, so this is belt-and-suspenders with
        // that gate rather than load-bearing on its own, but it keeps this
        // draw call's own intent explicit without relying on the reader
        // tracing back to reconcile() to know why it's a safe no-op.
        if (groundEnabled) {
            for (auto& kv : groundTiles) {
                CityGroundTile& gt = *kv.second;
                for (auto& r : gt.renderers) {
                    // Real textures when resolveGroundTextureBindings()
                    // found any (see tryRenderZoneEntry()'s own "GROUND
                    // TEXTURE RESOLUTION" doc comment) - drawTextured()'s
                    // own per-material fallback still colours any
                    // unresolved materialId, same as before. A tile this
                    // resolved NOTHING for keeps calling the untouched
                    // draw() path below instead (perMaterialSrv.empty()),
                    // never drawTextured() with an empty vector.
                    // SR3_GROUND_TEX_DEBUG=1: which branch this frame
                    // actually took - see groundTexDebugLog()'s own comment.
                    if (groundTexDebugLog()) {
                        std::fprintf(stderr, "[groundtex] draw tile %02d%02d: perMaterialSrv.size()=%zu -> %s\n",
                                    gt.col, gt.row, gt.perMaterialSrv.size(),
                                    gt.perMaterialSrv.empty() ? "MaterialIdAsColour" : "drawTextured");
                    }
                    if (!gt.perMaterialSrv.empty())
                        r->drawTextured(device.context(), viewProjection, gt.perMaterialSrv);
                    else
                        r->draw(device.context(), viewProjection, sr3render::MeshDrawMode::MaterialIdAsColour,
                               nullptr);
                }
            }
        }

        for (auto& kv : loaded) {
            CityLoadedCell& lc = *kv.second;
            for (auto& p : lc.props) {
                for (auto& r : p.renderers) {
                    if (tintMode)
                        r->draw(device.context(), viewProjection, sr3render::MeshDrawMode::FlatTint, nullptr,
                                p.world, lc.tint);
                    else
                        r->drawTextured(device.context(), viewProjection, p.perMaterialSrv, p.world);
                }
            }
            if (lc.outline) lc.outline->drawSkeleton(device.context(), viewProjection);
        }
        if (outlines) {
            float fw[16];
            sr3render::buildWorldMatrix(eye[0], 0.0f, eye[2], 0.0f, 1.0f, fw);
            focusBox.drawSkeleton(device.context(), viewProjection, fw);
        }

        for (const auto& ca : captureAt) {
            if (ca.first != frames) continue;
            std::vector<uint8_t> pixels;
            if (swapChain.capture(device.context(), pixels, error) &&
                sr3render::writePng(ca.second, swapChain.width(), swapChain.height(), pixels, error)) {
                std::string names;
                for (const auto& kv : loaded) names += (names.empty() ? "" : ",") + cells[kv.first].name;
                std::printf("captured : %s (frame %d, eye (%.1f %.1f %.1f), resident {%s})\n", ca.second.c_str(),
                            frames, static_cast<double>(eye[0]), static_cast<double>(eye[1]),
                            static_cast<double>(eye[2]), names.c_str());
            } else {
                std::fprintf(stderr, "capture failed (%s): %s\n", ca.second.c_str(), error.c_str());
            }
        }

        swapChain.present(true);
        ++frames;
        if (maxFrames >= 0 && frames >= maxFrames) break;
    }
    std::printf("frames   : %d presented; %zu cell loads, %zu cell evictions, %zu resident at exit\n", frames,
                totalLoads, totalEvicts, loaded.size());
    return 0;
}
