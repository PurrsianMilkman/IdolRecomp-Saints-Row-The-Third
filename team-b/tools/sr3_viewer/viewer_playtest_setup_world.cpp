// runPlaytest() split: city archives, window/device, render assets, streaming and ground setup.
// Code moved verbatim out of the former single-function runPlaytest() (see viewer_playtest_session.h).
#include "viewer_platform.h"
#include "viewer_playtest_session.h"
#include "sr3zone/zone_placement.h"
#include <cstring>
#include <map>

namespace sr3viewer {

int setupCityArchives(PlaytestSession& sess) {
    auto& cityArchivePaths = sess.cityArchivePaths;
    auto& spawnCategoryDensity = sess.spawnCategoryDensity;
    auto& playtestHoods = sess.playtestHoods;
    auto& archives = sess.archives;
    auto& cells = sess.cells;
    auto& cellByName = sess.cellByName;
    auto& playtestRoadGraph = sess.playtestRoadGraph;
    // ---- city ground+props (byte-for-byte the SAME CityArchive/CityCell population
    // runCity()/runWalkDemo() already use). --------------------------------------------
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
                if (cellByName.count(key)) continue;
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

    // ---- real road + sidewalk graph (feature/road-graph) ------------------------------
    sess.playtestRoadGraph = LoadPlaytestRoadGraph(archives, &playtestHoods);
    std::printf("spawn    : %zu hood volumes x %zu spawn categories -> hood category lookup %s\n", playtestHoods.size(),
                spawnCategoryDensity.size(),
                (!playtestHoods.empty() && !spawnCategoryDensity.empty()) ? "ACTIVE" : "inactive (Default Spawn stand-in)");
    g_playtestRoadGraph = playtestRoadGraph.get();
    sess.roadGraphGlobalReset_.emplace();
    return -1;
}

int setupWindowDevice(PlaytestSession& sess) {
    auto& winWidth = sess.winWidth;
    auto& winHeight = sess.winHeight;
    const auto& visible = sess.visible;
    auto& error = sess.error;
    // ---- window/device/swapchain/shared depth -----------------------------------------
    // `visible` defaults to TRUE for this one command, unlike every other command in this
    // file (see this function's own top doc comment and sr3render/window.h's doc comment
    // on Window::create()'s `visible` param) - an interactive playtest with a hidden
    // window would defeat its entire purpose. --headless restores the hidden default for
    // the automated ctest smoke test, which must not steal focus or flash on screen.
    sess.window_.emplace();
    auto& window = *sess.window_;
    // escQuits=false (THIS TASK): Escape is repurposed as the real pause-menu-open binding
    // (spec-frontend-menu-runtime.md Sec7/Sec9 item 2, CBA_GAC_PAUSE_MENU/0x55) - see the main
    // loop's own "PAUSE MENU" block below for the gate/push/dialog chain and F10's own doc
    // comment for the replacement quit key (Window::create's own doc comment: the window can
    // still be closed via its title-bar/X button regardless of this flag).
    if (!window.create("SR3 Viewer - playtest v0 (interactive)", winWidth, winHeight, error, visible,
                        /*escQuits=*/false)) {
        std::fprintf(stderr, "window creation failed: %s\n", error.c_str());
        return 1;
    }
    sess.device_.emplace();
    auto& device = *sess.device_;
    if (!device.initialise(16, 16, error)) {
        std::fprintf(stderr, "device init failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("device   : %s (%s)\n",
                device.kind() == sr3render::DeviceKind::Hardware ? "HARDWARE" : "WARP (software)",
                device.adapterName().c_str());
    return -1;
}

int setupRenderAssets(PlaytestSession& sess) {
    auto& winWidth = sess.winWidth;
    auto& winHeight = sess.winHeight;
    auto& charArchiveBytes = *sess.charArchiveBytes_;
    auto& cb = sess.cb;
    auto& headCb = sess.headCb;
    auto& geometry = *sess.geometry_;
    auto& mesh = *sess.mesh_;
    const auto& channelIndex = sess.channelIndex;
    const auto& drawGroup = sess.drawGroup;
    auto& vertexDuplication = *sess.vertexDuplication_;
    auto& bindPositions = sess.bindPositions;
    auto& headGeometry = *sess.headGeometry_;
    auto& headMesh = *sess.headMesh_;
    const auto& headChannelIndex = sess.headChannelIndex;
    const auto& headDrawGroup = sess.headDrawGroup;
    auto& headVertexDuplication = *sess.headVertexDuplication_;
    auto& headBindPositions = sess.headBindPositions;
    auto& error = sess.error;
    auto& window = *sess.window_;
    auto& device = *sess.device_;
    auto& shadowMapError = sess.shadowMapError;
    const auto& shadowMapReady = sess.shadowMapReady;
    auto& dupPtr = sess.dupPtr;
    auto& characterMaterialTextures = sess.characterMaterialTextures;
    auto& characterHeadMaterialTextures = sess.characterHeadMaterialTextures;
    auto& characterPerMaterialSrv = sess.characterPerMaterialSrv;
    auto& characterHeadPerMaterialSrv = sess.characterHeadPerMaterialSrv;
    auto& characterTextured = sess.characterTextured;
    auto& characterHeadTextured = sess.characterHeadTextured;
    sess.swapChain_.emplace();
    auto& swapChain = *sess.swapChain_;
    if (!swapChain.create(device.device(), window.nativeHandle(), winWidth, winHeight, error)) {
        std::fprintf(stderr, "swap chain creation failed: %s\n", error.c_str());
        return 1;
    }
    // focusBox owns the shared depth buffer (same "only the first renderer owns the depth
    // buffer" pattern as runCity()/runWalkDemo()) but is never drawn here - no debug
    // outline in a build meant for an actual player to look at.
    sess.focusBox_.emplace();
    auto& focusBox = *sess.focusBox_;
    if (!uploadCityOutline(focusBox, device.device(), 0.0f, 0.0f, kLevel4WindowX * 0.5f, kLevel4WindowZ * 0.5f,
                           false, error) ||
        !focusBox.createDepth(device.device(), winWidth, winHeight, error)) {
        std::fprintf(stderr, "focus/depth renderer failed: %s\n", error.c_str());
        return 1;
    }
    sess.depthWidth = winWidth;
    sess.depthHeight = winHeight;

    // Sun/cascade shadow map (THIS TASK, 2026-10-09, sun-shadow-map -
    // include/sr3render/sun_shadow_map.h): ONE owner for the shadow depth
    // target, the light view-projection computation, and the generation
    // pass itself - see that header's own banner for the CONFIRMED-real-
    // mechanism-vs-CHOSEN-v1-simplification breakdown (spec-render-
    // pipeline.md Sec23.6.4). 2048x2048 (CHOSEN default - see that header's
    // own initialise() comment). A failure here is non-fatal: the playtest
    // still renders exactly as it did before this task, just with
    // kShadowMapReady false so draw() calls fall back to their own
    // existing null-default (no shadow) behaviour below.
    // CRREISH_DISABLE_SUN_SHADOW=1 (env var, unset/0 = silent, default): same
    // convention as SR3_GROUND_TEX_DEBUG/SR3_ANIM_DEBUG_LOG elsewhere in this
    // file - a debug escape hatch for producing a genuine before/after A-B
    // comparison (this task's own verification pass) without a second
    // rebuild, never read by any shipped behaviour path otherwise.
    const char* disableShadowEnv = std::getenv("CRREISH_DISABLE_SUN_SHADOW");
    const bool shadowDisabledByEnv = disableShadowEnv != nullptr && disableShadowEnv[0] != '0' && disableShadowEnv[0] != '\0';

    sess.sunShadowMap_.emplace();
    auto& sunShadowMap = *sess.sunShadowMap_;
    sess.shadowMapReady = !shadowDisabledByEnv && sunShadowMap.initialise(device.device(), 2048, shadowMapError);
    if (!shadowMapReady && !shadowDisabledByEnv) {
        std::fprintf(stderr, "sun shadow map init failed (continuing without shadows): %s\n",
                    shadowMapError.c_str());
    }

    sess.characterRenderer_.emplace();
    auto& characterRenderer = *sess.characterRenderer_;
    if (!characterRenderer.initialise(device.device(), error)) {
        std::fprintf(stderr, "character renderer init failed: %s\n", error.c_str());
        return 1;
    }
    sess.dupPtr = vertexDuplication.sourceIndex.empty() ? nullptr : &vertexDuplication;
    if (!characterRenderer.upload(device.device(), mesh, channelIndex, 0, error, drawGroup, &bindPositions, dupPtr)) {
        std::fprintf(stderr, "character upload failed: %s\n", error.c_str());
        return 1;
    }
    sess.characterHeadRenderer_.emplace();
    auto& characterHeadRenderer = *sess.characterHeadRenderer_;
    if (!characterHeadRenderer.initialise(device.device(), error)) {
        std::fprintf(stderr, "character head renderer init failed: %s\n", error.c_str());
        return 1;
    }
    const sr3render::VertexDuplication* headDupPtr =
        headVertexDuplication.sourceIndex.empty() ? nullptr : &headVertexDuplication;
    if (!characterHeadRenderer.upload(device.device(), headMesh, headChannelIndex, 0, error, headDrawGroup,
                                      &headBindPositions, headDupPtr)) {
        std::fprintf(stderr, "character head upload failed: %s\n", error.c_str());
        return 1;
    }

    // Real per-material diffuse textures - identical FIX1/HEAD-FIX resolution path
    // runWalkDemo() uses (sr3geometry::MaterialBindings + findAndUploadTexture), always
    // attempted here (no --v2 gate); falls back to flat colour per-material, honestly,
    // for any slot that does not resolve.
    {
        sr3geometry::MaterialBindings bindings;
        bool haveBindings = false;
        try {
            bindings = sr3geometry::MaterialBindings::parse(vpp::ByteView(cb.data(), cb.size()), geometry.offset(),
                                                            geometry.meshSubBlockOffset() + mesh.cLength());
            haveBindings = bindings.located();
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "brad material binding: %s\n", ex.what());
        }
        if (haveBindings) {
            vpp::Container texContainer(vpp::ByteView(charArchiveBytes.data(), charArchiveBytes.size()));
            characterMaterialTextures.resize(bindings.materialCount());
            characterPerMaterialSrv.assign(bindings.materialCount(), nullptr);
            for (uint32_t m = 0; m < bindings.materialCount(); ++m) {
                const std::string* diffuse = bindings.diffuseFor(m);
                if (diffuse == nullptr) continue;
                std::string foundIn;
                if (findAndUploadTexture(texContainer, *diffuse, device.device(), characterMaterialTextures[m], foundIn))
                    characterPerMaterialSrv[m] = characterMaterialTextures[m].srv;
            }
            characterTextured = true;
        }
    }
    {
        sr3geometry::MaterialBindings headBindings;
        bool haveHeadBindings = false;
        try {
            headBindings = sr3geometry::MaterialBindings::parse(vpp::ByteView(headCb.data(), headCb.size()),
                                                                headGeometry.offset(),
                                                                headGeometry.meshSubBlockOffset() + headMesh.cLength());
            haveHeadBindings = headBindings.located();
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "brad_head_lod material binding: %s\n", ex.what());
        }
        // Head matlib attach+bind - ALWAYS ON here (runWalkDemo()'s own
        // identical mechanism is opt-in via --head-matlib-attach/--head-
        // matlib-bind, kept off by default there so every frozen golden
        // baseline stays byte-for-byte unchanged; playtest has no golden to
        // preserve, and without this the head's own shipped diffuse slot
        // binds to the generic "missing.tga" placeholder, which THIS
        // project's own uploader renders as a solid flat colour - visually
        // reads as "corrupted" to a human player even though it is real
        // shipped data, not a reader bug). See runWalkDemo()'s own long
        // doc comment above its identical block (search
        // "ResolveCharacterMatlibAttachment" in this file) for the full
        // CONFIRMED-vs-CHOSEN citation: the matlib ATTACH step is a
        // confirmed real engine mechanism (spec-tables-customization.md
        // Sec12.1b); which texture replaces which mesh slot is NOT
        // spec-confirmed (Sec12.1c/12.1f) - PickMatlibTextureByRoleSuffix()
        // is a CHOSEN, clearly-labelled heuristic, not a traced fact.
        if (haveHeadBindings) {
            vpp::Container matlibContainer(vpp::ByteView(charArchiveBytes.data(), charArchiveBytes.size()));
            auto loadByName = [&](const std::string& composed, std::vector<uint8_t>& out) {
                std::vector<uint8_t> unusedSibling;
                return findEntry(matlibContainer, composed, out, unusedSibling, "");
            };
            sr3geometry::CharacterMatlibAttachment headMatlibAttachment;
            try {
                headMatlibAttachment = sr3geometry::ResolveCharacterMatlibAttachment("brad", "brad_head", loadByName);
            } catch (const std::exception& ex) {
                std::fprintf(stderr, "brad_head.matlib_pc attachment: %s\n", ex.what());
            }
            if (headMatlibAttachment.attached()) {
                const std::string partHint = "head";
                const std::string* realDiffuse = sr3geometry::PickMatlibTextureByRoleSuffix(
                    headMatlibAttachment.resource.textureNames, partHint, {"_d", "_dp"});
                const std::string* realNormal = sr3geometry::PickMatlibTextureByRoleSuffix(
                    headMatlibAttachment.resource.textureNames, partHint, {"_n"});
                int replacedDiffuse = 0;
                for (sr3geometry::MaterialBinding& mb : headBindings.materialsMutable()) {
                    for (sr3geometry::TextureRef& tref : mb.textures) {
                        if (realDiffuse != nullptr && (tref.paramHash == sr3geometry::kParamHashDiffuseA ||
                                                       tref.paramHash == sr3geometry::kParamHashDiffuseB)) {
                            tref.name = *realDiffuse;
                            ++replacedDiffuse;
                        } else if (realNormal != nullptr && tref.paramHash == sr3geometry::kParamHashNormalMap) {
                            tref.name = *realNormal;
                        }
                    }
                }
                std::printf("character: head matlib attach+bind: %d diffuse-role TextureRef(s) replaced with the "
                            "matlib's own resolved name (CHOSEN role-suffix heuristic - see this block's own "
                            "comment)\n",
                            replacedDiffuse);
            } else {
                std::printf("character: head matlib: not attached (not found) - head stays on its own shipped "
                            "placeholder diffuse\n");
            }
        }
        if (haveHeadBindings) {
            vpp::Container headTexContainer(vpp::ByteView(charArchiveBytes.data(), charArchiveBytes.size()));
            characterHeadMaterialTextures.resize(headBindings.materialCount());
            characterHeadPerMaterialSrv.assign(headBindings.materialCount(), nullptr);
            for (uint32_t m = 0; m < headBindings.materialCount(); ++m) {
                const std::string* diffuse = headBindings.diffuseFor(m);
                if (diffuse == nullptr) continue;
                std::string foundIn;
                if (findAndUploadTexture(headTexContainer, *diffuse, device.device(), characterHeadMaterialTextures[m],
                                         foundIn))
                    characterHeadPerMaterialSrv[m] = characterHeadMaterialTextures[m].srv;
            }
            characterHeadTextured = true;
        }
    }
    std::printf("character: body %s, head %s (real per-material diffuse textures)\n",
                characterTextured ? "textured" : "UNRESOLVED - flat colour",
                characterHeadTextured ? "textured" : "UNRESOLVED - flat colour");
    return -1;
}

int setupStreamingGround(PlaytestSession& sess) {
    const auto& miscTablesPath = sess.miscTablesPath;
    auto& spawnX = sess.spawnX;
    auto& spawnZ = sess.spawnZ;
    auto& spawnYOverrideGiven = sess.spawnYOverrideGiven;
    auto& spawnYOverride = sess.spawnYOverride;
    auto& mouseSensitivity = sess.mouseSensitivity;
    auto& noCollision = sess.noCollision;
    auto& docOf = sess.docOf;
    auto& pm = *sess.pm_;
    auto& heldFacing = sess.heldFacing;
    auto& followCam = *sess.followCam_;
    auto& archives = sess.archives;
    // --load-save: placement, cash, respect, multiplier and time of day before the spawn warp below.
    if (!sess.loadSavePath.empty()) applyLoadedSave(sess);
    auto& cells = sess.cells;
    auto& window = *sess.window_;
    auto& device = *sess.device_;
    auto& loaded = sess.loaded;
    auto& groundTiles = sess.groundTiles;
    auto& totalLoads = sess.totalLoads;
    auto& totalEvicts = sess.totalEvicts;
    auto& loadCell = sess.loadCell;
    auto& lastWanted = sess.lastWanted;
    auto& firstReconcile = sess.firstReconcile;
    auto& reconcile = sess.reconcile;
    auto& finalGroundY = sess.finalGroundY;
    auto& heightmapTiles = sess.heightmapTiles;
    auto& heightmapTileFailed = sess.heightmapTileFailed;
    auto& sampleRealGroundHeight = sess.sampleRealGroundHeight;
    // Real .clmesh_pc collision - ALWAYS wired in this command (no --collision opt-in:
    // "stop at walls" is this task's own deliverable, not an experimental flag). See
    // runWalkDemo()'s own doc comment above ClmeshTriangleCollisionWorld for the full
    // citation of what this does and does not cover (static .clmesh_pc props only, NOT
    // the street/building-shell surface itself - see GROUND-Y MEASUREMENT below for how
    // that gap is handled honestly here).
    sess.collisionWorld_.emplace(loaded);
    auto& collisionWorld = *sess.collisionWorld_;
    sess.realCameraCollision_.emplace(loaded);
    auto& realCameraCollision = *sess.realCameraCollision_;
    followCam.collision = &realCameraCollision;  // real swept-sphere camera collision, always on here

    sess.loadCell = [&](size_t cellIndex, int frame) {
        const CityCell& c = cells[cellIndex];
        CityArchive& a = *archives[c.archiveIndex];
        auto lc = std::make_unique<CityLoadedCell>();
        lc->cellIndex = cellIndex;
        const double t0 = cityNowMs();
        bool placedMode = false;
        size_t instPlaced = 0, meshesUnplaced = 0;
        try {
            vpp::Container str2 = a.top->openNested(c.entryIndex);
            std::vector<std::string> clNames;
            for (const auto& e : str2.entries())
                if (endsWithNoCase(e.name, ".clmesh_pc")) clNames.push_back(e.name);
            lc->propEntries = clNames.size();
            // PLACEMENT (spec-world-streaming.md Sec10.7.1, FINAL): the tile's `~f` .czh_pc records bind each
            // object to `<mesh>.lmeshx` (+12 = byte offset into the header's own name pool) with a real
            // position (+0/+2/+4) and Euler angles (+6/+8/+10). A fine cell holds every mesh whose
            // transformed box overlaps the cell (X/Z, the Sec10.7.1 cell test), so an instance belongs to this
            // cell iff its box overlaps this cell's box. SR3_PROP_GRID=1 forces the old CHOSEN grid stand-in.
            const bool forceGrid = std::getenv("SR3_PROP_GRID") != nullptr;
            std::shared_ptr<std::vector<sr3zone::PlacedObject>> placements;
            if (!forceGrid) {
                static std::map<std::pair<int, int>, std::shared_ptr<std::vector<sr3zone::PlacedObject>>> tileCache;
                auto itc = tileCache.find({c.col, c.row});
                if (itc == tileCache.end()) {
                    std::shared_ptr<std::vector<sr3zone::PlacedObject>> got;
                    char nameBuf[48];
                    std::snprintf(nameBuf, sizeof(nameBuf), "sr3_city~f%02d%02d.czh_pc", c.col, c.row);
                    for (auto& arch : archives) {
                        std::vector<uint8_t> czh, dummy;
                        if (!findEntry(*arch->top, nameBuf, czh, dummy, std::string())) continue;
                        try {
                            got = std::make_shared<std::vector<sr3zone::PlacedObject>>(
                                sr3zone::DecodeZonePlacements(vpp::ByteView(czh.data(), czh.size())));
                        } catch (const std::exception& ex) {
                            std::printf("[placement] tile %02d%02d czh parse failed: %s\n", c.col, c.row, ex.what());
                        }
                        break;
                    }
                    size_t bound = 0;
                    if (got) for (auto& o : *got) bound += o.bound ? 1 : 0;
                    std::printf("[placement] tile %02d%02d: %zu records, %zu bound%s\n", c.col, c.row,
                                got ? got->size() : size_t(0), bound, got ? "" : " (no ~f czh: grid fallback)");
                    if (got && got->empty()) got.reset();
                    itc = tileCache.emplace(std::make_pair(c.col, c.row), got).first;
                }
                placements = itc->second;
            }
            std::map<std::string, std::vector<size_t>> recordsByMesh; // lower-case mesh -> record indices
            if (placements)
                for (size_t i = 0; i < placements->size(); ++i)
                    if ((*placements)[i].bound) recordsByMesh[toLowerLocal((*placements)[i].meshName)].push_back(i);
            placedMode = placements != nullptr;
            for (const auto& nm : clNames) {
                std::vector<size_t> recIdx;
                if (placements) {
                    std::string base = toLowerLocal(nm);
                    base.erase(base.size() - 10); // ".clmesh_pc"
                    auto itr = recordsByMesh.find(base);
                    if (itr == recordsByMesh.end()) { ++meshesUnplaced; continue; } // no record places it (skyline etc.)
                    recIdx = itr->second;
                }
                CityProp p;
                std::string why;
                std::vector<std::array<float, 3>> localCollisionTris;
                if (loadCityProp(str2, nm, archives, c.archiveIndex, device.device(), true, p, why,
                                 &localCollisionTris)) {
                    lc->texturesWanted += p.texturesWanted;
                    lc->texturesFound += p.texturesFound;
                    lc->texturesFoundElsewhere += p.texturesFoundElsewhere;
                    if (placements) {
                        size_t made = 0;
                        for (size_t ri : recIdx) {
                            const sr3zone::PlacedObject& o = (*placements)[ri];
                            float mnx = 1e30f, mxx = -1e30f, mnz = 1e30f, mxz = -1e30f;
                            for (int k = 0; k < 8; ++k) {
                                const auto q = sr3zone::PlacementTransformPoint(
                                    o, {(k & 1) ? p.localMax[0] : p.localMin[0], (k & 2) ? p.localMax[1] : p.localMin[1],
                                        (k & 4) ? p.localMax[2] : p.localMin[2]});
                                mnx = (std::min)(mnx, q[0]); mxx = (std::max)(mxx, q[0]);
                                mnz = (std::min)(mnz, q[2]); mxz = (std::max)(mxz, q[2]);
                            }
                            if (!(mxx > c.centreX - kFineCellHalfX && mnx < c.centreX + kFineCellHalfX &&
                                  mxz > c.centreZ - kFineCellHalfZ && mnz < c.centreZ + kFineCellHalfZ))
                                continue;
                            CityProp inst = p; // shares the GPU renderers; own world matrix + collision
                            const float w[16] = {o.rows[0], o.rows[1], o.rows[2], 0.0f, o.rows[3], o.rows[4], o.rows[5], 0.0f,
                                                 o.rows[6], o.rows[7], o.rows[8], 0.0f, o.position[0], o.position[1],
                                                 o.position[2], 1.0f};
                            std::memcpy(inst.world, w, sizeof(w));
                            if (!localCollisionTris.empty()) {
                                std::vector<std::array<float, 3>> wt;
                                wt.reserve(localCollisionTris.size());
                                for (const auto& v : localCollisionTris) wt.push_back(sr3zone::PlacementTransformPoint(o, v));
                                inst.collisionBvh = BuildCollisionBvh(wt, 0.0f, 0.0f, 0.0f);
                            }
                            lc->props.push_back(std::move(inst));
                            ++made;
                        }
                        instPlaced += made;
                        if (made == 0) ++meshesUnplaced;
                        continue;
                    }
                    p.collisionLocalTrisStash = std::move(localCollisionTris);
                    lc->props.push_back(std::move(p));
                } else {
                    ++lc->propsFailed;
                    std::printf("[stream]   %s: prop %s SKIPPED: %s\n", c.name.c_str(), nm.c_str(), why.c_str());
                }
            }
        } catch (const std::exception& ex) {
            std::printf("[stream]   %s: container open failed: %s\n", c.name.c_str(), ex.what());
        }
        const size_t m = lc->props.size();
        if (placedMode)
            std::printf("[placement] %s: %zu instances placed (%zu mesh entries had no record/instance in this cell)\n",
                        c.name.c_str(), instPlaced, meshesUnplaced);
        if (m > 0 && !placedMode) {
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
                if (!p.collisionLocalTrisStash.empty()) {
                    p.collisionBvh = BuildCollisionBvh(p.collisionLocalTrisStash, p.world[12], p.world[13], p.world[14]);
                    std::vector<std::array<float, 3>>().swap(p.collisionLocalTrisStash);
                }
            }
        }
        lc->loadMs = cityNowMs() - t0;
        ++totalLoads;
        std::printf("[stream] frame %d LOAD  %-8s (%s, col %d row %d N %d) centre (%.1f, %.1f): %zu/%zu props "
                    "uploaded, %zu failed, textures %zu/%zu, %.0f ms\n",
                    frame, c.name.c_str(), a.label.c_str(), c.col, c.row, c.n, static_cast<double>(c.centreX),
                    static_cast<double>(c.centreZ), lc->props.size(), lc->propEntries, lc->propsFailed,
                    lc->texturesFound, lc->texturesWanted, lc->loadMs);
        loaded[cellIndex] = std::move(lc);
    };

    sess.reconcile = [&](int frame, float focusX, float focusZ) {
        std::vector<size_t> wanted = cityWantedCells(cells, focusX, focusZ, 1.0f);
        // Parity check with runCity()'s/runWalkDemo()'s own identical reconcile() copies
        // (THIS TASK, world-streaming-reconcile) - this one didn't have it. Sec10.4: the
        // level-4 window (159.5x139.5m) is smaller than one fine cell (160x140m), so it
        // overlaps 1-4 cells and never more - this never fires in practice, same as in
        // those other two copies, but is cheap insurance against a lattice/window constant
        // drifting out of sync with the real numbers.
        if (wanted.size() > kLevel4Capacity) {
            std::printf("[stream] frame %d WARNING: %zu cells overlap the level-4 window - Sec10.4 says this "
                        "cannot exceed %zu; lattice/window constants are wrong somewhere\n",
                        frame, wanted.size(), kLevel4Capacity);
        }
        if (!firstReconcile && wanted == lastWanted) return;
        firstReconcile = false;
        lastWanted = wanted;
        std::string names;
        for (size_t idx : wanted) names += (names.empty() ? "" : ",") + cells[idx].name;
        std::printf("[stream] frame %d focus (%.1f, %.1f): wanted = {%s}\n", frame, static_cast<double>(focusX),
                    static_cast<double>(focusZ), names.c_str());
        // EVICT logging (THIS TASK, world-streaming-reconcile, 2026-10-08): this reconcile()
        // used to erase dropped cells/tiles SILENTLY - the real release path already existed
        // (map erase -> ~CityLoadedCell()/~CityGroundTile() -> unique_ptr<MeshRenderer> ->
        // ~MeshRenderer() -> real D3D11 Release() calls, confirmed in src/mesh_renderer.cpp),
        // but nothing printed it, so a scripted/ctest run had no stdout evidence eviction was
        // actually happening (as opposed to cells silently piling up forever). Brought to
        // parity with runCity()'s/runWalkDemo()'s own identical reconcile() copies, which
        // already print this - see those functions' own top comments for the citation
        // (spec-world-streaming.md Sec11.4: eviction is immediate, same call as the diff).
        const std::set<size_t> wantedSet(wanted.begin(), wanted.end());
        for (auto it = loaded.begin(); it != loaded.end();) {
            if (wantedSet.count(it->first) == 0) {
                const CityCell& c = cells[it->first];
                std::printf("[stream] frame %d EVICT %-8s (col %d row %d N %d) - %zu props released\n", frame,
                            c.name.c_str(), c.col, c.row, c.n, it->second->props.size());
                it = loaded.erase(it);
                ++totalEvicts;
            } else {
                ++it;
            }
        }
        for (size_t idx : wanted) if (loaded.count(idx) == 0) loadCell(idx, frame);

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
            // buildCollision = true, UNCONDITIONALLY (street-collision-
            // 0x2237-mopp follow-up task, 2026-10-11): this project's own
            // Heightmap-based `sampleRealGroundHeight()` fallback below is
            // ALSO sampled unconditionally (never gated on --no-collision,
            // which this function's own usage text documents as scoped
            // ONLY to "the player's pm.collisionHook" - horizontal wall
            // collision) - real ground-pavement VERTICAL support follows
            // that same unconditional precedent, not the horizontal
            // `noCollision` gate.
            if (loadCityGroundTile(t.first, t.second, archives, device.device(), *gt, why, true, /*allBlocks=*/true)) {
                // Collision diagnostics appended to this LOAD line (street-
                // collision-0x2237-mopp follow-up task, 2026-10-11) - the
                // real triangle count and this tile's own collision-mesh
                // XZ footprint, so a reader can see directly whether/where
                // SampleGroundMeshHeight() has real coverage for this tile
                // without a separate diagnostic command.
                const CollisionBvh& cbvh = gt->collisionBvh;
                const bool haveRoot = cbvh.root >= 0;
                const CollisionBvhNode* rootNode = haveRoot ? &cbvh.nodes[static_cast<size_t>(cbvh.root)] : nullptr;
                std::printf("[ground] frame %d LOAD  tile %02d%02d: %s, %zu range(s), %u triangles (collision "
                            "tris=%zu bounds x[%.1f,%.1f] z[%.1f,%.1f])\n", frame, t.first, t.second,
                            gt->cznName.c_str(), gt->rangesRendered, gt->triangles, cbvh.tris.size(),
                            haveRoot ? static_cast<double>(rootNode->min[0]) : 0.0,
                            haveRoot ? static_cast<double>(rootNode->max[0]) : 0.0,
                            haveRoot ? static_cast<double>(rootNode->min[2]) : 0.0,
                            haveRoot ? static_cast<double>(rootNode->max[2]) : 0.0);
            } else {
                std::printf("[ground] frame %d tile %02d%02d: SKIPPED (no real ground rendered): %s\n", frame,
                            t.first, t.second, why.c_str());
            }
            // Walkable-ground collision (spec 13.5.1): reported for every tile, independent of whether its render mesh loaded.
            if (gt->collisionLoaded) {
                const CollisionBvhNode& cr = gt->collisionBvh.nodes[static_cast<size_t>(gt->collisionBvh.root)];
                std::printf("[ground] frame %d COLLISION tile %02d%02d: %s n1=%zu strip-standin tris=%zu bounds "
                            "x[%.1f,%.1f] y[%.1f,%.1f] z[%.1f,%.1f]\n", frame, t.first, t.second,
                            gt->collisionSource.c_str(), gt->collisionVertexCount, gt->collisionTriangleCount,
                            static_cast<double>(cr.min[0]), static_cast<double>(cr.max[0]),
                            static_cast<double>(cr.min[1]), static_cast<double>(cr.max[1]),
                            static_cast<double>(cr.min[2]), static_cast<double>(cr.max[2]));
            } else {
                std::printf("[ground] frame %d COLLISION tile %02d%02d: NONE (%s) - CHOSEN node-ground fallback applies\n",
                            frame, t.first, t.second, gt->collisionWhy.c_str());
            }
            groundTiles[t] = std::move(gt);
        }
        std::fflush(stdout);
    };

    // ---- GROUND-Y MEASUREMENT (SUBSTITUTION 3, see this function's own top doc comment):
    // load the real ground tiles at the chosen spawn corner BEFORE warping brad there, and
    // read the real decoded geometry's own Y bounds (MeshRenderer::boundsMin()/boundsMax(),
    // already-public accessors - no reader changed for this) rather than assuming the
    // fake-grid y=0 every prop in this scene uses is also right for the REAL `~s` ground
    // mesh now rendered here (findTileZoneEntryName()'s tilde-s fallback, merged earlier
    // this session - confirmed directly via tests/golden/walk_demo/baseline_stdout.txt
    // showing real non-zero triangle counts for exactly this corner's 3 resident tiles,
    // not assumed from any doc comment). ------------------------------------------------
    reconcile(0, spawnX, spawnZ);
    float groundYMin = 0.0f, groundYMax = 0.0f;
    bool haveGroundBounds = false;
    // CHOSEN ground stand-in (results/freeroam_ground_height_20261018.md, Team A questions 1-4; --no-node-ground
    // restores the old behaviour): index the road/sidewalk graph node positions (spec-zone-data-format.md 13.2a,
    // absolute world metres) for the nearest-node ground fallback, and measure finalGroundY only from ground
    // renderers whose own XZ bounds contain the spawn (tile 0915's ~s mesh is near-origin local-space content).
    const bool nodeGroundOn = !sess.noNodeGround;
    if (nodeGroundOn && sess.playtestRoadGraph) {
        for (const auto& n : sess.playtestRoadGraph->nodes) sess.nodeGround.Add(n.x, n.y, n.z);
        for (const auto& n : sess.playtestRoadGraph->sidewalkNodes) sess.nodeGround.Add(n.pos.x, n.pos.y, n.pos.z);
    }
    for (const auto& kv : groundTiles) {
        for (const auto& r : kv.second->renderers) {
            if (nodeGroundOn && !sr3player::BoundsContainXZ(r->boundsMin()[0], r->boundsMax()[0], r->boundsMin()[2],
                                                             r->boundsMax()[2], spawnX, spawnZ))
                continue;
            const float lo = r->boundsMin()[1], hi = r->boundsMax()[1];
            if (!haveGroundBounds) { groundYMin = lo; groundYMax = hi; haveGroundBounds = true; }
            else { groundYMin = (std::min)(groundYMin, lo); groundYMax = (std::max)(groundYMax, hi); }
        }
    }
    // CHOSEN (not a terrain-height-query system - this project has none, and inventing
    // one would be exactly the fabrication this project's "no invented fixes" discipline
    // forbids): a single one-time pick of brad's spawn Y and the character-proxy's flat
    // groundY plane, from the REAL measured range above. Within 3m of 0 keeps the
    // existing fake-grid y=0 convention every prop in this scene already uses (so brad
    // lines up with them too); otherwise uses the real range's own midpoint.
    if (haveGroundBounds && (groundYMin > 3.0f || groundYMax < -3.0f)) {
        finalGroundY = 0.5f * (groundYMin + groundYMax);
    }
    if (nodeGroundOn && !haveGroundBounds) {
        // CHOSEN (last-resort fallback only): no real ground renderer covers the spawn -> nearest graph node's Y.
        double ny = 0.0;
        if (sess.nodeGround.Nearest(spawnX, spawnZ, sr3player::kNodeGroundMaxDistance, 0.0, ny))
            finalGroundY = static_cast<float>(ny);
    }
    // WALKABLE GROUND AT THE SPAWN (G1, spec-zone-data-format.md Sec13.5.1 port recipe steps 3-4): ray from a start
    // height over the resident tiles' walkable collision (the `~s` tail mesh). Start height, spec item 4: CHOSEN for a
    // port that only has (x, z): nearest road/sidewalk node Y + 2 m (keeps rooftops out); otherwise (no node) the
    // collision AABB max Y + 1 m (the topmost surface). Replaces the render-bounds-derived finalGroundY above whenever
    // the collision resolves; the render-bounds / node values above stay only as the fallback for a spawn with no
    // collision mesh (tile failed to load).
    bool spawnCollisionResolved = false;  // read by the one-time probe print below
    double spawnCollisionY = 0.0;
    std::string spawnCollisionHow;
    {
        double hit = 0.0, startY = 0.0;
        bool got = false;
        const char* how = "";
        double ny = 0.0;
        const bool haveNodeSpawn =
            nodeGroundOn && sess.nodeGround.Nearest(spawnX, spawnZ, sr3player::kNodeGroundMaxDistance, 0.0, ny);
        if (haveNodeSpawn) {
            startY = ny + 2.0;
            got = SampleGroundMeshHeight(groundTiles, spawnX, spawnZ, startY, hit);
            how = "node Y + 2 m start (CHOSEN, spec item 4)";
        }
        if (!got) {
            got = SampleGroundMeshTopHeight(groundTiles, spawnX, spawnZ, hit);
            how = "collision AABB max Y + 1 m start (spec item 4)";
        }
        if (!got && startY != 0.0) {
            const double rr = SampleGroundMeshHoleFill(groundTiles, spawnX, spawnZ, startY, hit);
            if (rr > 0.0) {
                got = true;
                static char holeBuf[160];
                std::snprintf(holeBuf, sizeof(holeBuf), "CHOSEN hole fill: median hit of the %.0f m ring (strip stand-in hole at the spawn)", rr);
                how = holeBuf;
            }
        }
        if (!got && nodeGroundOn && haveNodeSpawn) {
            // CHOSEN labelled fallback: no collision under/near the spawn -> the nearest node's Y beats the render-bounds midpoint.
            finalGroundY = static_cast<float>(ny);
        }
        if (got) {
            spawnCollisionResolved = true;
            spawnCollisionY = hit;
            spawnCollisionHow = how;
            finalGroundY = static_cast<float>(hit);
            std::printf("ground-y : walkable collision ground at spawn (%.1f %.1f) = %.3f m [%s]\n",
                        static_cast<double>(spawnX), static_cast<double>(spawnZ), hit, how);
        } else {
            std::printf("ground-y : NO walkable collision resolved at spawn (%.1f %.1f) - falling back to the CHOSEN "
                        "node/render-bounds value %.2f m\n", static_cast<double>(spawnX), static_cast<double>(spawnZ),
                        static_cast<double>(finalGroundY));
        }
    }
    if (spawnYOverrideGiven) finalGroundY = spawnYOverride;  // --spawn-y, see its own doc comment above
    // Honest banner note (this task, 2026-10-08): says directly whether what was just
    // measured is flat/street-level or not, rather than leaving a reader to infer it from
    // raw numbers - see this function's own "RELOCATED" doc comment above spawnX/spawnZ for
    // why the default spawn sits where it does and what was checked before picking it.
    const char* groundShapeNote = !haveGroundBounds ? " (NO ground tile resolved - using 0.0)"
                                 : (groundYMax - groundYMin) <= 3.0f
                                       ? " (flat, street-level - not a rooftop)"
                                       : " (NOT flat - spans more than 3m, elevated/ledge-shaped real geometry)";
    std::printf("ground-y : RENDER-mesh bounds (diagnostic, not collision) [%.2f, %.2f] across %zu loaded tile(s) - spawn/collision "
                "groundY = %.2f%s\n",
                static_cast<double>(groundYMin), static_cast<double>(groundYMax), groundTiles.size(),
                static_cast<double>(finalGroundY), groundShapeNote);
    pm.warp(sr3player::Vec3{spawnX, static_cast<double>(finalGroundY), spawnZ});
    sess.collisionDriver_.emplace(collisionWorld, 0.4, 1.8, static_cast<double>(finalGroundY));
    auto& collisionDriver = *sess.collisionDriver_;

    // ---- REAL per-frame heightmap-based ground following (THIS TASK,
    // 2026-10-08 - the live-playtest "sky-blue gap under Brad's feet"
    // follow-up). Replaces the flat `finalGroundY` plane above with a real
    // per-frame sample of the tile's own Heightmap (0x2244) record
    // (spec-zone-data-format.md Sec13.5, sr3zone/zone_heightmap.h) at the
    // player/vehicle's own CURRENT (x,z), eased through
    // sr3player::GroundFollower (sr3player/ground_follow.h) rather than
    // applied as a hard instant teleport - see that header's own file
    // comment for exactly why, and for the "two different consumers, one
    // shared primitive" split this code follows (the player's own target
    // feeds CharacterProxyDriver::setGroundY() below; the vehicle has no
    // other vertical model at all, so `vehicleGroundY` IS its render Y).
    // `finalGroundY` itself is UNCHANGED above and stays the fallback used
    // whenever no real Heightmap record resolves for the queried tile (a
    // tile never streamed in yet, or a tile whose own record genuinely has
    // none) - see loadCityHeightmapTile()'s own doc comment.
    //
    // HONEST SCOPE, stated once here rather than scattered: this fixes
    // WHERE the character/vehicle's own Y coordinate sits - it does NOT by
    // itself prove any VISIBLE ground geometry renders at that height.
    // Sec13.5's own closing paragraph is explicit that whether 0x2244 data
    // is ever converted into rendered geometry is a SEPARATE, still-OPEN
    // question (cross-reference: spec-terrain-format.md Sec5 item 2) -
    // this code uses Heightmap data ONLY as the scalar elevation-query
    // source Sec13.5 frames as the leading hypothesis, never to build or
    // render a mesh from it (explicitly out of this task's scope). The
    // "sky-blue gap" visual bug may need BOTH this fix AND real visible
    // ground geometry at the sampled height to fully close - this task's
    // own final report states precisely which of the two is and is not
    // demonstrated by the captured frames.
    sess.sampleRealGroundHeight = [&](double x, double z, float fallback) -> float {
        int hcol = 0, hrow = 0;
        cityTileColRowForWorld(static_cast<float>(x), static_cast<float>(z), hcol, hrow);
        const std::pair<int, int> key{hcol, hrow};
        auto it = heightmapTiles.find(key);
        if (it == heightmapTiles.end()) {
            if (heightmapTileFailed.count(key)) return fallback;
            sr3zone::HeightmapRecord rec;
            std::string why;
            if (!loadCityHeightmapTile(hcol, hrow, archives, rec, why)) {
                heightmapTileFailed[key] = true;
                return fallback;
            }
            it = heightmapTiles.emplace(key, rec).first;
        }
        float h = fallback;
        if (sr3zone::SampleHeightmapHeight(it->second, x, z, h)) {
            // CHOSEN: -5 is "no data" (Team A question 4, results/freeroam_ground_height_20261018.md).
            if (!sess.noNodeGround && !sr3player::HeightmapValueHasData(h)) return fallback;
            return h;
        }
        return fallback;
    };
    // REAL `0x2237` ground-pavement collision mesh height, tried FIRST
    // (street-collision-0x2237-mopp follow-up task, 2026-10-11 - see
    // SampleGroundMeshHeight()'s own doc comment above, right after
    // loadCityGroundTile(), for the full spec citation and why the
    // Heightmap/flat-plane chain above stays as the fallback rather than
    // being removed). `referenceY = fallback` disambiguates a multi-hit
    // vertical ray the SAME way the caller's own existing fallback value
    // already represents "the best height known before this query" -
    // consistent with every other call site in this file that threads a
    // `fallback` through unchanged.
    // G1 (TEAM B, 2026-10-18) - THE walkable-ground query for the character, vehicle and ambient actors. The 0x2244
    // heightmap is NOT in this chain any more (aircraft clearance, spec 13.5.1(d)); sampleRealGroundHeight above is kept
    // only for the SR3_TRACE_PLAYER_Y diagnostic and the one-time spawn probe print.
    //   1. ray from startY + 0.05 down 500 m over the resident `~s` tail-collision tiles, closest hit      [Mesh]
    //   2. miss -> CHOSEN retry from the spec's item-4 start height (nearest road/sidewalk node Y + 2 m, else the
    //      collision AABB max Y + 1 m) - the engine always has a position to start from, a port teleported
    //      to a new (x, z) or standing over a strip stand-in hole may not                                  [MeshRetry]
    //   3. still a miss: tile resident with collision -> hold (spec: "on a miss, leave Y unchanged")       [HeldMiss]
    //      tile resident but its collision failed to load -> CHOSEN labelled fallback: nearest node Y     [NodeFallback]
    //      tile not resident -> hold the character (spec item 6, "before the tile is resident")           [HeldNoTile]
    sess.sampleGround = [&](double x, double z, double startY, float holdY) -> PlaytestSession::GroundSampleResult {
        using GS = PlaytestSession::GroundSource;
        PlaytestSession::GroundSampleResult r;
        double y = 0.0;
        if (SampleGroundMeshHeight(groundTiles, x, z, startY, y)) {
            r.y = static_cast<float>(y);
            r.source = GS::Mesh;
            return r;
        }
        double ny = 0.0;
        const bool haveNode = !sess.noNodeGround &&
                              sess.nodeGround.Nearest(x, z, sr3player::kNodeGroundMaxDistance, startY, ny);
        if (haveNode && SampleGroundMeshHeight(groundTiles, x, z, ny + 2.0, y)) {
            r.y = static_cast<float>(y);
            r.source = GS::MeshRetry;
            return r;
        }
        if (!haveNode && SampleGroundMeshTopHeight(groundTiles, x, z, y)) {
            r.y = static_cast<float>(y);
            r.source = GS::MeshRetry;
            return r;
        }
        int tc = 0, tr = 0;
        cityTileColRowForWorld(static_cast<float>(x), static_cast<float>(z), tc, tr);
        const auto it = groundTiles.find({tc, tr});
        if (it == groundTiles.end()) {
            r.y = holdY;
            r.source = GS::HeldNoTile;
        } else if (it->second->collisionLoaded) {
            r.y = holdY;
            r.source = GS::HeldMiss;
        } else if (haveNode) {
            r.y = static_cast<float>(ny);  // CHOSEN fallback: this tile's collision failed to load
            r.source = GS::NodeFallback;
        } else {
            r.y = holdY;
            r.source = GS::HeldMiss;
        }
        return r;
    };
    sess.playerGroundFollower_.emplace(static_cast<double>(finalGroundY));
    sess.vehicleGroundFollower_.emplace(static_cast<double>(finalGroundY));
    sess.vehicleGroundY = finalGroundY;
                                           // reads this INSTEAD OF the flat finalGroundY from here on.
    {
        // One diagnostic probe at the chosen spawn tile, printed once
        // (not per-frame spam), so a reader of this command's own stdout
        // immediately sees whether tile 0915's own Heightmap data was
        // usable here or fell back - the exact question this task's own
        // brief asks to answer precisely for THIS spawn.
        const float probe = sampleRealGroundHeight(static_cast<double>(spawnX), static_cast<double>(spawnZ), -1.0e30f);
        if (probe > -1.0e29f) {
            std::printf("heightmap: 0x2244 sample at spawn (%.1f %.1f) = %.2fm (aircraft-clearance field, NOT ground; spawn ground was "
                        "%.2fm)\n",
                        static_cast<double>(spawnX), static_cast<double>(spawnZ), static_cast<double>(probe),
                        static_cast<double>(finalGroundY));
        } else {
            std::printf("heightmap: NO real Heightmap (0x2244) record resolved at spawn (%.1f %.1f) - staying on "
                        "the flat-plane fallback (%.2fm)\n",
                        static_cast<double>(spawnX), static_cast<double>(spawnZ), static_cast<double>(finalGroundY));
        }
        // Same one-time probe for the REAL `0x2237` ground-pavement
        // collision mesh (street-collision-0x2237-mopp follow-up task,
        // 2026-10-11) - printed so a reader of this command's own stdout
        // can see directly whether the spawn tile's own real mesh
        // resolved (and at what height) rather than falling through to
        // the Heightmap/flat-plane chain above.
        // G1: the probe reports the walkable `~s` tail-mesh result computed in the spawn block above.
        if (spawnCollisionResolved) {
            std::printf("groundmesh: real 0x2237 collision-mesh sample at spawn (%.1f %.1f) = %.2fm [%s] (the "
                        "walkable ground; 0x2244 is not consulted)\n",
                        static_cast<double>(spawnX), static_cast<double>(spawnZ), spawnCollisionY,
                        spawnCollisionHow.c_str());
        } else {
            std::printf("groundmesh: NO real 0x2237 collision-mesh triangle resolved at spawn (%.1f %.1f) - "
                        "CHOSEN node-ground / render-bounds fallback above\n",
                        static_cast<double>(spawnX), static_cast<double>(spawnZ));
        }
    }

    // --no-collision (THIS TASK, world-streaming-reconcile) leaves pm.collisionHook UNSET -
    // see this function's own usage-text doc comment above for why. PlayerMovement's own
    // "if (collisionHook)" gate (src/player_locomotion.cpp) already treats an unset hook as
    // a valid no-op path, so this is not a new PlayerMovement behaviour, just not installing
    // the real one. Default (noCollision == false) is byte-for-byte the pre-existing
    // behaviour every golden/smoke test already exercises.
    sess.jump_.emplace(sess.jumpHeight);
    if (!noCollision) {
        pm.collisionHook = [&](sr3player::Vec3 from, sr3player::Vec3 vel,
                               double dt) -> sr3player::PlayerMovement::CollisionHookResult {
            // T2 jump: the JumpController owns the vertical velocity across frames (launch per spec-player-runtime.md
            // Sec5.6.5 sqrt(2*|g|*h); the airborne gravity and the landing snap onto the ground chain value are
            // CharacterProxyDriver::step()'s). Grounded + no launch -> vy 0, exactly the pre-existing behaviour.
            sr3player::JumpGateInputs gate;
            gate.controlsDisabled = pm.world.readControlsDisabled();
            gate.permitted = (pm.world.permissions2078.get() & 0x200u) != 0;  // Sec3.6.4: +0x2078 bit 0x200
            gate.blocked = pm.world.climbingOrVaulting.get();  // JC-2: 0x00989670 (Sec3.6.4 step 2)
            gate.heightAboveGround = from.y - collisionDriver.groundY();
            vel.y = sess.jump_->beginFrame(gate, from.y);
            // AW-15 freefall / parachute (spec-player-runtime.md Sec19.5): while active the horizontal velocity is the
            // law's r and the vertical fall law runs with the per-frame terminal speed T (45 / 9 ... , Sec5.6.5).
            if (sess.craft.ffOverride) {
                vel.x = sess.craft.ff.rx;
                vel.z = sess.craft.ff.rz;
                collisionDriver.setTerminalFallSpeed(sess.craft.ff.terminalT);
            } else {
                collisionDriver.setTerminalFallSpeed(0.0);  // default 44.704
            }
            const sr3player::CapsuleStepResult r = collisionDriver.step(from, vel, dt);
            sess.craft.playerGrounded = r.grounded;
            sess.jump_->endFrame(r.position.y, r.velocity.y, r.grounded, dt);
            if (pm.st.crouched) sess.jump_->noteCrouchFrame();
            sess.jump_->noteGroundSpeed(std::hypot(r.velocity.x, r.velocity.z), pm.st.crouched);
            for (const auto& h : r.hits) {
                std::printf("[collision] frame-local wall hit: surface %u at (%.2f %.2f %.2f) normal (%.2f %.2f %.2f) "
                            "passes=%d\n",
                            h.surfaceId, h.point.x, h.point.y, h.point.z, h.normal.x, h.normal.y, h.normal.z,
                            r.passesRun);
            }
            return sr3player::PlayerMovement::CollisionHookResult{r.position, r.velocity, r.grounded};
        };
    } else {
        std::printf("collision: --no-collision given - pm.collisionHook left unset (free movement, streaming-proof "
                    "test mode only)\n");
    }

    // Now that pm's FINAL position is known, seed and snap the real follow camera to it
    // (reset()'s own doc comment: "target must be set to the local player" - its snap
    // reads target NOW).
    pm.world.orientation.set(sr3playeranim::OrientationFromFacing(heldFacing));
    sr3playeranim::DriveCameraTarget(followCam, pm);
    sess.cameraFreeDoc_.emplace(docOf("camera_free.xtbl"));
    const auto& cameraFreeDoc = *sess.cameraFreeDoc_;
    const auto cameraFreeParsed = sr3tables_environment::ParseCameraFree(cameraFreeDoc);
    if (!cameraFreeParsed) {
        std::fprintf(stderr, "could not parse camera_free.xtbl from %s\n", miscTablesPath.c_str());
        return 1;
    }
    sess.camSeedReport_.emplace(sr3player::SeedCameraFromCameraFree(followCam, *cameraFreeParsed, /*widescreen=*/false));
    const auto& camSeedReport = *sess.camSeedReport_;
    std::printf("camera   : camera_free.xtbl seeded (%d scalars, %d submode row(s) applied)\n",
                camSeedReport.scalarsSeeded, camSeedReport.submodesSeeded);
    followCam.init();
    followCam.reset();
    const auto& camNow0 = followCam.committed();
    std::printf("camera   : real follow camera active; committed eye after reset (%.2f %.2f %.2f), fov %.2f deg\n",
                camNow0.position.x, camNow0.position.y, camNow0.position.z, camNow0.fov);

    // ---- mouse-look plumbing (live mode): poll-and-recentre, the SAME "polled input, not
    // event-driven" philosophy every GetAsyncKeyState fly camera in this file already
    // uses, applied to the mouse via GetCursorPos/SetCursorPos instead of a WM_MOUSEMOVE
    // hook (Window never installs one - see src/window.cpp's own windowProc). -------------
    sess.hwnd = static_cast<HWND>(window.nativeHandle());
    // Real InputSystem constants (include/sr3player/input.h: mouseSensitivityX = 0.24f,
    // mouseGlobalScale = 0.01f) reused as the mouse-delta scale, rather than an
    // arbitrary invented number - the exact engine formula for raw counts -> radians is
    // OPEN (input.h's own scope-cut notes), so this is a CHOSEN scale built from the two
    // real confirmed constants already in this project, not a new one.
    sess.kMouseRadPerCount = 0.24f * 0.01f * mouseSensitivity;
    return -1;
}

} // namespace sr3viewer
