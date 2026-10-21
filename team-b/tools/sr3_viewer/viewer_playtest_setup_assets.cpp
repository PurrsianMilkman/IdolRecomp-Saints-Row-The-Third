// runPlaytest() split: character, tables, animation runtime and input-source setup.
// Code moved verbatim out of the former single-function runPlaytest() (see viewer_playtest_session.h).
#include "viewer_platform.h"
#include "viewer_playtest_session.h"

namespace sr3viewer {

int setupBrad(PlaytestSession& sess) {
    const auto& charactersPath = sess.charactersPath;
    auto& cb = sess.cb;
    auto& gb = sess.gb;
    auto& rb = sess.rb;
    auto& unusedSibling = sess.unusedSibling;
    auto& headCb = sess.headCb;
    auto& headGb = sess.headGb;
    const auto& channelIndex = sess.channelIndex;
    const auto& drawGroup = sess.drawGroup;
    const auto& useBonePalette = sess.useBonePalette;
    auto& parentIdx = sess.parentIdx;
    auto& restMesh = sess.restMesh;
    auto& restRig = sess.restRig;
    auto& bindSkin = sess.bindSkin;
    auto& verts = sess.verts;
    const auto& headChannelIndex = sess.headChannelIndex;
    const auto& headDrawGroup = sess.headDrawGroup;
    auto& headRestMesh = sess.headRestMesh;
    auto& headBindSkin = sess.headBindSkin;
    auto& headVerts = sess.headVerts;
    // ---- brad (rigged character) - IDENTICAL load/skin to runWalkDemo() above, including the
    // HEAD FIX (brad_head_lod.ccmesh_pc is a separate top-level entry in the same archive) - see
    // that function's own long comment block for the full citation, not repeated here. ----------
    sess.charArchiveBytes_.emplace(mapFileLargeC(charactersPath));
    auto& charArchiveBytes = *sess.charArchiveBytes_;
    if (charArchiveBytes.empty()) {
        std::fprintf(stderr, "could not read %s\n", charactersPath.c_str());
        return 1;
    }
    try {
        vpp::Container charContainer(vpp::ByteView(charArchiveBytes.data(), charArchiveBytes.size()));
        if (!findEntry(charContainer, "brad.ccmesh_pc", cb, gb, "brad.gcmesh_pc")) {
            std::fprintf(stderr, "could not find brad.ccmesh_pc in %s\n", charactersPath.c_str());
            return 1;
        }
        if (!findEntry(charContainer, "brad.rig_pc", rb, unusedSibling, "")) {
            std::fprintf(stderr, "could not find brad.rig_pc in %s\n", charactersPath.c_str());
            return 1;
        }
        if (!findEntry(charContainer, "brad_head_lod.ccmesh_pc", headCb, headGb, "brad_head_lod.gcmesh_pc")) {
            std::fprintf(stderr, "could not find brad_head_lod.ccmesh_pc in %s\n", charactersPath.c_str());
            return 1;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "characters archive error: %s\n", ex.what());
        return 1;
    }
    if (gb.empty() || headGb.empty()) {
        std::fprintf(stderr, "brad.gcmesh_pc / brad_head_lod.gcmesh_pc missing\n");
        return 1;
    }
    sess.material_.emplace(sr3geometry::MaterialBlock::parse(vpp::ByteView(cb.data(), cb.size())));
    auto& material = *sess.material_;
    sess.geometry_.emplace(sr3geometry::GeometryBlock::parse(vpp::ByteView(cb.data(), cb.size()), material));
    auto& geometry = *sess.geometry_;
    if (!geometry.hasMeshSubBlock()) {
        std::fprintf(stderr, "brad.ccmesh_pc has no Mesh sub-block\n");
        return 1;
    }
    sess.mesh_.emplace(sr3mesh::MeshBlock::parse(vpp::ByteView(cb.data(), cb.size()),
                                                        geometry.meshSubBlockOffset(), vpp::ByteView(gb.data(), gb.size())));
    auto& mesh = *sess.mesh_;
    if (channelIndex >= mesh.channels().size() ||
        !sr3mesh::layoutInfoFor(mesh.channels()[channelIndex].layoutCode).hasSkinning) {
        std::fprintf(stderr, "brad channel 0 missing or carries no skinning\n");
        return 1;
    }
    sess.rig_.emplace(sr3rig::Rig::parse(vpp::ByteView(rb.data(), rb.size())));
    auto& rig = *sess.rig_;
    sess.bones_p = &(rig.bones());
    const auto& bones = *sess.bones_p;
    std::printf("character: brad, %zu bones (channel 0, group 0)\n", bones.size());
    sess.skinModeResult_.emplace(resolveSkinningMode(mesh, /*legacyForced=*/false));
    const auto& skinModeResult = *sess.skinModeResult_;
    if (isSkinningRefusal(skinModeResult.mode)) {
        std::fprintf(stderr, "%s\n", skinModeResult.refusalMessage.c_str());
        return 1;
    }
    sess.useBonePalette = skinModeResult.mode == SkinningMode::kPaletteSingleSet ||
                                skinModeResult.mode == SkinningMode::kPaletteMultiSet;
    sess.parentIdx = std::vector<uint32_t>(bones.size());
    sess.restMesh = std::vector<std::array<float, 3>>(bones.size());
    sess.restRig = std::vector<std::array<float, 3>>(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        parentIdx[i] = bones[i].parentIndex;
        restMesh[i] = useBonePalette ? sr3rig::rigToMeshSpaceInverted(bones[i].restPosition)
                                     : sr3rig::rigToMeshSpace(bones[i].restPosition);
        restRig[i] = bones[i].restPosition;
    }
    sess.bindSkin = sr3rig::computeSkinningMatrices(parentIdx, restMesh, nullptr);
    sess.verts = mesh.decodeChannel(channelIndex);
    if (verts.empty()) {
        std::fprintf(stderr, "brad channel 0 produced no vertices\n");
        return 1;
    }
    sess.vertexDuplication_.emplace();
    auto& vertexDuplication = *sess.vertexDuplication_;
    if (skinModeResult.mode == SkinningMode::kPaletteMultiSet) {
        sr3rig::BlendIndexRemapStats remap;
        sr3rig::VertexDuplicationResult dup = applyMultiSetSkinning(mesh, verts, bones.size(), drawGroup, remap);
        if (!dup.usable) {
            std::fprintf(stderr, "%s\n", dup.problem.c_str());
            return 1;
        }
        if (dup.duplicatesCreated != 0) {
            vertexDuplication.sourceIndex = dup.duplicateSourceIndex;
            vertexDuplication.redirectPerRange = dup.redirectPerRange;
        }
    } else if (useBonePalette) {
        sr3rig::BlendIndexRemapStats remap;
        verts = sr3rig::remapBlendIndicesThroughPalette(verts, mesh.bonePalette(), bones.size(), &remap);
    }
    sess.bindStats_.emplace();
    auto& bindStats = *sess.bindStats_;
    sess.bindPositions = sr3rig::skinVertices(verts, bindSkin, &bindStats);

    sess.headMaterial_.emplace(sr3geometry::MaterialBlock::parse(vpp::ByteView(headCb.data(), headCb.size())));
    auto& headMaterial = *sess.headMaterial_;
    sess.headGeometry_.emplace(sr3geometry::GeometryBlock::parse(vpp::ByteView(headCb.data(), headCb.size()), headMaterial));
    auto& headGeometry = *sess.headGeometry_;
    if (!headGeometry.hasMeshSubBlock()) {
        std::fprintf(stderr, "brad_head_lod.ccmesh_pc has no Mesh sub-block\n");
        return 1;
    }
    sess.headMesh_.emplace(sr3mesh::MeshBlock::parse(vpp::ByteView(headCb.data(), headCb.size()),
                                                            headGeometry.meshSubBlockOffset(),
                                                            vpp::ByteView(headGb.data(), headGb.size())));
    auto& headMesh = *sess.headMesh_;
    if (headChannelIndex >= headMesh.channels().size() ||
        !sr3mesh::layoutInfoFor(headMesh.channels()[headChannelIndex].layoutCode).hasSkinning) {
        std::fprintf(stderr, "brad_head_lod channel 0 missing or carries no skinning\n");
        return 1;
    }
    sess.headSkinModeResult_.emplace(resolveSkinningMode(headMesh, /*legacyForced=*/false));
    const auto& headSkinModeResult = *sess.headSkinModeResult_;
    if (isSkinningRefusal(headSkinModeResult.mode)) {
        std::fprintf(stderr, "%s\n", headSkinModeResult.refusalMessage.c_str());
        return 1;
    }
    const bool headUseBonePalette = headSkinModeResult.mode == SkinningMode::kPaletteSingleSet ||
                                    headSkinModeResult.mode == SkinningMode::kPaletteMultiSet;
    sess.headRestMesh = std::vector<std::array<float, 3>>(bones.size());
    for (size_t i = 0; i < bones.size(); ++i)
        headRestMesh[i] = headUseBonePalette ? sr3rig::rigToMeshSpaceInverted(bones[i].restPosition)
                                             : sr3rig::rigToMeshSpace(bones[i].restPosition);
    sess.headBindSkin = sr3rig::computeSkinningMatrices(parentIdx, headRestMesh, nullptr);
    sess.headVerts = headMesh.decodeChannel(headChannelIndex);
    if (headVerts.empty()) {
        std::fprintf(stderr, "brad_head_lod channel 0 produced no vertices\n");
        return 1;
    }
    sess.headVertexDuplication_.emplace();
    auto& headVertexDuplication = *sess.headVertexDuplication_;
    if (headSkinModeResult.mode == SkinningMode::kPaletteMultiSet) {
        sr3rig::BlendIndexRemapStats headRemap;
        sr3rig::VertexDuplicationResult headDup =
            applyMultiSetSkinning(headMesh, headVerts, bones.size(), headDrawGroup, headRemap);
        if (!headDup.usable) {
            std::fprintf(stderr, "%s\n", headDup.problem.c_str());
            return 1;
        }
        if (headDup.duplicatesCreated != 0) {
            headVertexDuplication.sourceIndex = headDup.duplicateSourceIndex;
            headVertexDuplication.redirectPerRange = headDup.redirectPerRange;
        }
    } else if (headUseBonePalette) {
        sr3rig::BlendIndexRemapStats headRemap;
        headVerts = sr3rig::remapBlendIndicesThroughPalette(headVerts, headMesh.bonePalette(), bones.size(), &headRemap);
    }
    sess.headBindPositions = sr3rig::skinVertices(headVerts, headBindSkin, nullptr);
    return -1;
}

int setupTables(PlaytestSession& sess) {
    const auto& miscTablesPath = sess.miscTablesPath;
    auto& noNpcAi = sess.noNpcAi;
    auto& miscRead = sess.miscRead;
    auto& docOf = sess.docOf;
    auto& todSegments = sess.todSegments;
    auto& spawnCategoryDensity = sess.spawnCategoryDensity;
    auto& playtestHoods = sess.playtestHoods;
    auto& freeroamWeatherPedDensity = sess.freeroamWeatherPedDensity;
    auto& freeroamHoodName = sess.freeroamHoodName;
    auto& freeroamCategoryName = sess.freeroamCategoryName;
    auto& inventoryItemNames = sess.inventoryItemNames;
    auto& items3dNames = sess.items3dNames;
    auto& weaponDefNames = sess.weaponDefNames;
    auto& weaponDefClasses = sess.weaponDefClasses;
    auto& bradWeaponDefFound = sess.bradWeaponDefFound;
    auto& npcAiTables = sess.npcAiTables;
    auto& bradWeapon = sess.bradWeapon;
    // ---- misc_tables.vpp_pc: real control-parameter/graph tables - IDENTICAL to runWalkDemo(). ----
    sess.miscBytes_.emplace(mapFileLargeC(miscTablesPath));
    auto& miscBytes = *sess.miscBytes_;
    if (miscBytes.empty()) {
        std::fprintf(stderr, "could not read %s\n", miscTablesPath.c_str());
        return 1;
    }
    sess.miscTop_.emplace(vpp::ByteView(miscBytes.data(), miscBytes.size()));
    auto& miscTop = *sess.miscTop_;
    sess.miscRead = [&](const std::string& name) -> std::optional<std::string> {
        std::vector<uint8_t> out, sib;
        if (!findEntry(miscTop, name, out, sib, std::string()) || out.empty()) return std::nullopt;
        return std::string(reinterpret_cast<const char*>(out.data()), out.size());
    };
    sess.docOf = [&](const std::string& name) -> sr3xtbl::Document {
        auto s = miscRead(name);
        if (!s) throw std::runtime_error("could not find '" + name + "' in " + miscTablesPath);
        return sr3xtbl::ParseDocument(std::string_view(*s));
    };

    // ---- weather_time_of_day.xtbl: the real per-frame TOD lighting blend ----
    // (this task, 2026-10-08, sky/lighting/TOD - spec-tables-environment.md
    // Sec18). weather.xtbl is NOT read here: Sec18's own weather-STAGE state
    // machine (the Chance-weighted stage picker/schedule array) is not
    // ported to this project (see sr3render/tod_lighting.h's banner for why
    // that is a CHOSEN, explicitly-honest scope reduction, not an oversight)
    // - all this command needs from the real tables is
    // weather_time_of_day.xtbl's own per-segment cells for ONE CHOSEN
    // constant weather-stage name, kWeatherStageForPlaytest below. Optional,
    // like the other docOf() tables already loaded by this same function:
    // falls back to an empty segment list (-> TodLightingResult's own old-
    // behaviour defaults, see ComputeTodLighting()'s doc comment) rather
    // than aborting the whole playtest, so a --misc-tables archive that
    // happens to lack this table (synthetic/test fixtures, a stripped
    // archive, ...) still runs, just without live TOD variation.
    // CHOSEN (NOT spec-derived): "Light Rain" is the one weather.xtbl stage
    // confirmed, by direct read of the real misc_tables.vpp_pc
    // (2026-10-08), to have a populated Ambient_Color/Fog_Color/
    // TOD_Light_Color cell in EVERY one of the real weather_time_of_day.xtbl
    // segments (sunrise/noon/sunset/night) - "Clear Skies" (weather.xtbl's
    // own highest-Chance stage) has NO cell at all in this table in real
    // data, and the other three real stages are each missing at least one
    // field in at least one segment. Picking the one stage with full real
    // coverage avoids this consumer's own "absent colour -> that struct's
    // 0/1.0 default" fallback (tod_lighting.h's banner) ever firing on the
    // happy path, for the most visually complete result this real data can
    // give without simulating a weather schedule.
    try {
        const sr3xtbl::Document wtodDoc = docOf("weather_time_of_day.xtbl");
        todSegments = sr3tables_environment::ParseWeatherTimeOfDayTable(wtodDoc);
        std::printf("weather  : weather_time_of_day.xtbl - %zu segment(s) loaded\n", todSegments.size());
    } catch (const std::exception& ex) {
        std::printf("weather  : weather_time_of_day.xtbl not available (%s) - TOD lighting stays at its old fixed "
                    "CHOSEN defaults\n", ex.what());
    }

    // ---- spawn_info_categories.xtbl: per-category CarDay/CarNight/PedDay/PedNight
    // (spec-tables-progression.md Sec10.6) for the hood -> category lookup (spec-ai-runtime.md
    // Sec20.6.8). Optional; without it (or without hood volumes) the population stays on the
    // Default Spawn stand-in. ----
    try {
        const sr3xtbl::Document catDoc = docOf("spawn_info_categories.xtbl");
        for (const auto& c : sr3tables_progression::ParseSpawnInfoCategoriesTable(catDoc)) {
            if (!c.name) continue;
            spawnCategoryDensity[*c.name] = {c.carDay.value, c.carNight.value, c.pedDay.value, c.pedNight.value};
        }
        std::printf("spawn    : spawn_info_categories.xtbl - %zu categories\n", spawnCategoryDensity.size());
    } catch (const std::exception& ex) {
        std::printf("spawn    : spawn_info_categories.xtbl not available (%s) - Default Spawn stand-in\n", ex.what());
    }
    // Sec20.6.0(d) / RG-10: the ped density factor is the CURRENT weather stage's weather.xtbl Stage_Settings/
    // PedDensity (Clear Skies 1.0, Overcast 1.0, Post-Storm 0.75, Light Rain 0.5, Heavy Rain 0.1). Gap #9: the
    // current stage now comes from sr3render::WeatherScheduler (spec-tables-environment Sec18; --weather=<stage>
    // pins it) and drives BOTH this factor and the TOD lighting stage every frame (weatherTick). Without
    // weather.xtbl the factor stays 1.0 and the lighting uses the old fixed defaults.
    try {
        const sr3xtbl::Document wxDoc = docOf("weather.xtbl");
        auto infos = sr3render::BuildWeatherStageInfo(sr3tables_environment::ParseWeatherTable(wxDoc));
        for (const auto& s : infos) {
            std::string nx;
            for (int n : s.next) nx += (nx.empty() ? "" : ",") + infos[n].name;
            std::printf("weather  : stage '%s' chance=%.3f avgDuration=%.1fs variance=%.2f pedDensity=%.2f next=[%s]\n",
                        s.name.c_str(), static_cast<double>(s.chance), static_cast<double>(s.averageDuration),
                        static_cast<double>(s.variance), static_cast<double>(s.pedDensity), nx.c_str());
        }
        if (!infos.empty()) {
            sess.weatherSched.emplace(std::move(infos), sess.weatherSeed);
            if (!sess.weatherPinName.empty() && !sess.weatherSched->Pin(sess.weatherPinName))
                std::printf("weather  : --weather=%s names no weather.xtbl stage - running the schedule instead\n",
                            sess.weatherPinName.c_str());
            sess.weatherRefreshConsumers();
            const auto st = sess.weatherSched->Current();
            std::printf("weather-trace: start stage=%s%s seed=0x%X timeScale=%.2f pedDensity=%.2f light=%s\n",
                        sess.weatherSched->StageName(st.currentStage).c_str(), st.pinned ? " (pinned)" : "",
                        sess.weatherSeed, sess.weatherTimeScale, static_cast<double>(freeroamWeatherPedDensity),
                        sess.weatherLightA.c_str());
        }
    } catch (const std::exception& ex) {
        std::printf("weather  : weather.xtbl not available (%s) - ped density factor 1.0\n", ex.what());
    }
    // Sec20.6.8: the first hood whose BSP volume contains the point -> its hood_spawn_category (the
    // alt-category / script override flags are not wired: no script setter drives them here); no hood or an
    // unknown category -> "Default Spawn" (the table row when present, else the DefaultSpawnCategory() constants).
    sess.currentSpawnCategory = [&](const sr3airuntime::Vec3& p) -> sr3airuntime::SpawnCategoryDensity {
        freeroamHoodName = "(none)";
        freeroamCategoryName = "Default Spawn";
        if (const sr3zone::HoodVolume* h = sr3zone::FindHood(playtestHoods, p.x, p.y, p.z)) {
            freeroamHoodName = h->name;
            auto it = spawnCategoryDensity.find(h->spawnCategory);
            if (it != spawnCategoryDensity.end()) {
                freeroamCategoryName = h->spawnCategory;
                return it->second;
            }
        }
        auto def = spawnCategoryDensity.find("Default Spawn");
        return def != spawnCategoryDensity.end() ? def->second : sr3airuntime::DefaultSpawnCategory();
    };

    // ---- weapons.xtbl: Brad's REAL weapon (THIS TASK, playtest-weapon-fire-
    // wiring) ---------------------------------------------------------------
    // spec-tables-weapons-combat.md's own confirmed location ("the 82 base
    // rows live in misc_tables.vpp_pc and ONLY there") - the SAME archive
    // already open via docOf() above, same optional/non-aborting convention
    // as weather_time_of_day.xtbl just above (a stripped/synthetic
    // misc_tables.vpp_pc without this table still runs the rest of the
    // playtest; the fire key below simply has nothing to dispatch). Picks
    // the first real parsed row whose Weapon_Class is "pistol" (0, spec
    // Sec2.5's 23-name enum) - falling back to the first row that is not
    // "vehicle"-class (sr3weapons::kVehicleWeaponClass, CONFIRMED never to
    // fire through this dispatch at all, weapon_fire.h) if no pistol-class
    // row is present - rather than hardcoding a specific weapon NAME this
    // task never independently verified exists in every build of this
    // table (spec-tables-weapons-combat.md's own "82 real weapon names"
    // were not individually enumerated by name in this task's own spec
    // reading). bradWeaponDef is a real authored sr3tables_weapons::Weapon
    // row, not a fabricated placeholder.
    sess.bradWeaponDef_.emplace();
    auto& bradWeaponDef = *sess.bradWeaponDef_;
    try {
        const sr3xtbl::Document weaponsDoc = docOf("weapons.xtbl");
        std::vector<sr3tables_weapons::Weapon> allWeapons = sr3tables_weapons::ParseAllWeapons(weaponsDoc);
        for (const sr3tables_weapons::Weapon& w : allWeapons) { weaponDefNames.push_back(w.name); weaponDefClasses.emplace_back(w.name, w.weaponClass); }
        {
            // DLC rows (spec-tables-weapons-combat.md Sec2 "DLC rows"): sibling dlc2/dlc3 archives, optional.
            const std::filesystem::path dir = std::filesystem::path(miscTablesPath).parent_path();
            const std::pair<const char*, const char*> dlc[] = {{"dlc2.vpp_pc", "dlc2_weapons.xtbl"}, {"dlc3.vpp_pc", "dlc3_weapons.xtbl"}};
            for (const auto& d : dlc) {
                try {
                    MappedBytes db = mapFileLargeC((dir / d.first).string());
                    if (db.empty()) continue;
                    vpp::Container dc(vpp::ByteView(db.data(), db.size()));
                    std::vector<uint8_t> out, sib;
                    if (!findEntry(dc, d.second, out, sib, std::string()) || out.empty()) continue;
                    for (const auto& w : sr3tables_weapons::ParseAllWeapons(sr3xtbl::ParseDocument(std::string_view(reinterpret_cast<const char*>(out.data()), out.size())))) { weaponDefNames.push_back(w.name); weaponDefClasses.emplace_back(w.name, w.weaponClass); }
                } catch (const std::exception&) {}
            }
        }
        // items_inventory.xtbl (+DLC) row names (spec-lua-api-behaviour.md Sec17.25 "Item lookup source").
        try {
            for (const auto& n : sr3tables_vehicle_world::ParseItemsInventoryTable(docOf("items_inventory.xtbl")))
                if (n.name) inventoryItemNames.push_back(*n.name);
            const std::filesystem::path dir = std::filesystem::path(miscTablesPath).parent_path();
            const std::pair<const char*, const char*> idlc[] = {{"dlc1.vpp_pc", "dlc1_items_inventory.xtbl"}, {"dlc2.vpp_pc", "dlc2_items_inventory.xtbl"}, {"dlc3.vpp_pc", "dlc3_items_inventory.xtbl"}};
            for (const auto& d : idlc) {
                try {
                    MappedBytes db = mapFileLargeC((dir / d.first).string());
                    if (db.empty()) continue;
                    vpp::Container dc(vpp::ByteView(db.data(), db.size()));
                    std::vector<uint8_t> out, sib;
                    if (!findEntry(dc, d.second, out, sib, std::string()) || out.empty()) continue;
                    for (const auto& n : sr3tables_vehicle_world::ParseItemsInventoryTable(sr3xtbl::ParseDocument(std::string_view(reinterpret_cast<const char*>(out.data()), out.size()))))
                        if (n.name) inventoryItemNames.push_back(*n.name);
                } catch (const std::exception&) {}
            }
        } catch (const std::exception&) {}
        // items_3d.xtbl base names (spec-lua-api-behaviour.md Sec17.13 f752324: character_add_prop resolver).
        try {
            for (const auto& n : sr3tables_vehicle_world::ParseItems3DTable(docOf("items_3d.xtbl")))
                if (n.name) items3dNames.push_back(*n.name);
        } catch (const std::exception&) {}
        for (const sr3tables_weapons::Weapon& w : allWeapons) {
            if (w.weaponClass == 0 /* pistol, spec Sec2.5 */) {
                bradWeaponDef = w;
                bradWeaponDefFound = true;
                break;
            }
        }
        if (!bradWeaponDefFound) {
            for (const sr3tables_weapons::Weapon& w : allWeapons) {
                if (w.weaponClass != sr3weapons::kVehicleWeaponClass) {
                    bradWeaponDef = w;
                    bradWeaponDefFound = true;
                    break;
                }
            }
        }
        if (bradWeaponDefFound) {
            // ---- Gap #17: the player's weapon inventory. CHOSEN starting loadout (spec gives none): slot 1 =
            // Brad's pistol row (above), slot 2 = first SMG-slot row, slot 3 = first shotgun-slot row, each the
            // first non-vehicle, non-cutscene-only, finite-ammo base-game (framework main) row with a magazine. CHOSEN starting reserve: 3 magazines,
            // capped by the ammo.xtbl Max_in_Reserve of the row's Ammo (CONFIRMED cap, tables Sec5.1). ----
            std::vector<sr3tables_weapons::AmmoRecord> ammoRows;
            try { ammoRows = sr3tables_weapons::ParseAllAmmo(docOf("ammo.xtbl")); } catch (const std::exception&) {}
            sess.weaponInv = std::make_unique<sr3weapons::WeaponInventory>();
            auto addToInv = [&](const sr3tables_weapons::Weapon& w) {
                const int32_t mag = w.magazineSize.present ? static_cast<int32_t>(w.magazineSize.value) : 0;
                int32_t cap = 0;
                if (w.ammo) {
                    for (const auto& a : ammoRows)
                        if (a.name.size() == w.ammo->size() &&
                            std::equal(a.name.begin(), a.name.end(), w.ammo->begin(),
                                       [](char x, char y) { return std::tolower((unsigned char)x) == std::tolower((unsigned char)y); }) &&
                            a.maxInReserve.present) { cap = static_cast<int32_t>(a.maxInReserve.value); break; }
                }
                const int slot = sess.weaponInv->add(w, 3 * mag, cap);
                std::printf("weapon   : inventory slot %d = '%s' (Weapon_Class=%d Inv_Slot=%d clip=%d reserve=%d/cap %d refire=%d ms)\n",
                            slot + 1, w.name.c_str(), w.weaponClass, static_cast<int>(w.invSlot), mag,
                            sess.weaponInv->reserve(slot), cap,
                            w.timeManagement.refireDelayMs.present ? static_cast<int>(w.timeManagement.refireDelayMs.value) : 0);
            };
            addToInv(bradWeaponDef);
            for (const uint8_t wantSlot : {uint8_t(3), uint8_t(4)}) {  // kInvSlotNames: smg 3, shotgun 4
                for (const sr3tables_weapons::Weapon& w : allWeapons) {
                    if (w.invSlot != wantSlot || w.weaponClass == sr3weapons::kVehicleWeaponClass) continue;
                    if (!w.magazineSize.present || w.magazineSize.value == 0 || (w.flagsB & sr3weapons::kFlagBCutsceneOnly) || (w.flagsA & (sr3weapons::kFlagAInfiniteMagazine | sr3weapons::kFlagAUnlimitedAmmo)) || (w.framework && *w.framework != "main") || w.name == bradWeaponDef.name) continue;
                    addToInv(w);
                    break;
                }
            }
            std::printf("weapon   : keys (CHOSEN): R reload, 1-8 select Inv_Slot k-1 (key 1 = fists), 9 last, Q CCW, Tab CW (Sec5.4.3)\n");
            std::printf("weapon   : Brad equipped with real weapons.xtbl row '%s' (Weapon_Class=%d, %zu total rows "
                        "parsed) - LMB fires it on foot\n",
                        bradWeaponDef.name.c_str(), bradWeaponDef.weaponClass, allWeapons.size());
        } else {
            std::printf("weapon   : weapons.xtbl parsed (%zu rows) but every row is vehicle-class - Brad has no "
                        "weapon this run\n", allWeapons.size());
        }
    } catch (const std::exception& ex) {
        std::printf("weapon   : weapons.xtbl not available (%s) - Brad has no weapon this run (LMB is a no-op)\n",
                    ex.what());
    }
    // ---- NPC AI tables (NPC AI loop task): ai_goals / ai_behavior / combat_actions / ai_personalities from the SAME
    // misc_tables.vpp_pc, loaded exactly as the engine's loaders do (BuildNpcAiTables). Optional: without them the
    // per-NPC AI loop stays off (no invented data).
    if (!noNpcAi) {
        try {
            const sr3tables_trafficai::AiGoalsTable goalsT = sr3tables_trafficai::ParseAiGoalsTable(docOf("ai_goals.xtbl"));
            const sr3tables_trafficai::AiBehaviorTable behT = sr3tables_trafficai::ParseAiBehaviorTable(docOf("ai_behavior.xtbl"));
            const std::vector<sr3tables_weapons::CombatActionOverrideRow> actT =
                sr3tables_weapons::ParseAllCombatActionOverrides(docOf("combat_actions.xtbl"));
            sr3tables_trafficai::AiPersonalitiesTable persT;
            bool havePers = false;
            try {
                persT = sr3tables_trafficai::ParseAiPersonalitiesTable(docOf("ai_personalities.xtbl"));
                havePers = true;
            } catch (const std::exception&) {}
            auto built = std::make_shared<sr3airuntime::NpcAiTables>(
                sr3airuntime::BuildNpcAiTables(goalsT, behT, actT, havePers ? &persT : nullptr));
            size_t listed = 0;
            for (const auto& l : built->goalActionLists)
                if (l[0] != sr3airuntime::ActionBlock::kNoAction) ++listed;
            std::printf("npc-ai   : tables loaded - %zu goal rows (%zu non-empty lists), %zu behaviours, %zu combat_actions rows; "
                        "civilian flee_health_pct=%d\n",
                        goalsT.rows.size(), listed, behT.rows.size(), actT.size(), built->civilianFleeHealthPct);
            npcAiTables = built;
        } catch (const std::exception& ex) {
            std::printf("npc-ai   : AI tables not available (%s) - the per-NPC AI loop is off this run\n", ex.what());
        }
    }
    if (bradWeaponDefFound) bradWeapon = std::make_unique<sr3weapons::WeaponInstance>(bradWeaponDef);
    // The damage sink itself (EngineStateLethalDamageSink, needs hudEs/
    // standaloneCombatEs - declared further below, once hudEs is finalized)
    // is constructed later in this function - see the "WEAPON FIRE: damage
    // sink" block, just after the `if (hudEnabled || missionEnabled)` block.
    return -1;
}

int setupAnimRuntime(PlaytestSession& sess) {
    auto& miscRead = sess.miscRead;
    auto& docOf = sess.docOf;
    auto& states = sess.states;
    auto& actions = sess.actions;
    auto& triggerNames = sess.triggerNames;
    auto& walkLeftFootFrames = sess.walkLeftFootFrames;
    auto& walkRightFootFrames = sess.walkRightFootFrames;
    auto& walkTriggerFramesFound = sess.walkTriggerFramesFound;
    auto& footstepEventClipFilename = sess.footstepEventClipFilename;
    auto& motion = sess.motion;
    sess.paramsDoc_.emplace(docOf("control_parameters.xtbl"));
    const auto& paramsDoc = *sess.paramsDoc_;
    sess.registry_.emplace(sr3animruntime::ControlParameterRegistry::load(paramsDoc));
    const auto& registry = *sess.registry_;
    sess.filtersDoc_.emplace(docOf("control_filters.xtbl"));
    const auto& filtersDoc = *sess.filtersDoc_;
    sess.filters_.emplace(sr3animruntime::ControlFilterTable::load(filtersDoc, registry));
    const auto& filters = *sess.filters_;
    {
        const sr3xtbl::Document d1 = docOf("anim_states.xtbl");
        for (const auto& s : sr3tables_animation::ParseAnimStatesTable(d1)) if (s.name) states.push_back(*s.name);
        const sr3xtbl::Document d2 = docOf("anim_actions.xtbl");
        for (const auto& s : sr3tables_animation::ParseAnimActionsTable(d2)) if (s.name) actions.push_back(*s.name);
        const sr3xtbl::Document d3 = docOf("anim_triggers.xtbl");
        for (const auto& s : sr3tables_animation::ParseAnimTriggersTable(d3)) if (s.name) triggerNames.push_back(*s.name);
    }
    sess.stateActions_.emplace(sr3animruntime::StateActionRegistry::build({}, states, actions));
    const auto& stateActions = *sess.stateActions_;
    sess.triggers_.emplace(sr3animruntime::TriggerRegistry::build(triggerNames));
    const auto& triggers = *sess.triggers_;

    // ---- REAL foot-plant trigger frames for the walk clip (THIS TASK, 2026-10-10, orchestrator
    // correction after spec-audio-runtime.md Sec11a.8 / spec-anim-runtime.md Sec7.13's own
    // 2026-10-10 update) --------------------------------------------------------------------
    // anim_files.xtbl's generic `left-foot`/`right-foot` Triggers/Trigger markers (each with a
    // real Frame child) are REAL, universally-authored data - 1,170/1,149 occurrences base-game-
    // wide (Sec11a.8). This is the real foot-plant frame the walk/run/sprint locomotion clips
    // themselves carry, as opposed to the earlier fixed-cadence GUESS (kFootstepCadenceFrames
    // below) this task's original placeholder used.
    // HONEST CAVEAT: RealClipRegistry's own doc comment above ("WHICH CLIPS, AND WHY THESE")
    // already states "plym_ps_wlk.anim_pc" is a CHOSEN rig-compatible substitute for the real
    // "player state machine" walk clip, NOT confirmed to be the production mapping's own walk
    // clip ("this project has no anim_files.xtbl-backed clip registry anywhere"). So this lookup
    // only activates when anim_files.xtbl happens to carry a row whose Filename stem is literally
    // "plym_ps_wlk" (matched case-insensitively, extension-agnostic - the xtbl's own Filename is
    // ".animx", the loaded clip is ".anim_pc", same base name) AND that row carries at least one
    // left-foot/right-foot trigger. If not found, this falls back to the original cadence guess
    // below - NOT invented, an honest degrade matching this task's own "keep the trigger-timing
    // side as-is if genuinely open" instruction.
    bool footstepEventRowFound = false;
    int32_t footstepEventFrame = 0;
    try {
        const sr3xtbl::Document animFilesDoc = docOf("anim_files.xtbl");
        const std::vector<sr3tables_animation::AnimFile> animFiles =
            sr3tables_animation::ParseAnimFilesTable(animFilesDoc);
        auto stemOfLower = [](const std::string& fn) {
            std::string s = fn;
            const size_t dot = s.find_last_of('.');
            if (dot != std::string::npos) s = s.substr(0, dot);
            for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        };
        for (const auto& f : animFiles) {
            if (!f.filename || stemOfLower(*f.filename) != "plym_ps_wlk") continue;
            for (size_t i = 0; i < f.triggerNames.size(); ++i) {
                std::string n = f.triggerNames[i];
                for (auto& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (i >= f.triggerFrames.size()) continue;
                if (n == "left-foot") walkLeftFootFrames.push_back(f.triggerFrames[i]);
                else if (n == "right-foot") walkRightFootFrames.push_back(f.triggerFrames[i]);
            }
            break;  // first matching row only - same "first match" convention used elsewhere in this file
        }
        walkTriggerFramesFound = !walkLeftFootFrames.empty() || !walkRightFootFrames.empty();

        // ---- REAL FOOTSTEP event identity + CHOSEN switch selection (THIS TASK) -----------
        // spec-audio-runtime.md Sec11a.8: the real Wwise event for footsteps is the literal
        // string "FOOTSTEP", sourced from these SAME anim_files.xtbl rows (144 in the base
        // game carry a Sounds/Sound with Audio_Event=="FOOTSTEP", each pairing a MATERIAL_TYPE
        // switch (CONCRETE/METAL_SOLID/FLESH) with a SHOE_TYPE or MOVEMENT_TYPE switch). Open-
        // world walking has no author-baked material context (those 144 rows are fixed-context
        // clips - vehicle floor entry/exit, the Brute NPC kit - not general locomotion, Sec11a.8
        // point 2), so per purrsian-52's direction this uses MATERIAL_TYPE:CONCRETE +
        // SHOE_TYPE:SNEAKER as a labelled CHOSEN default for general walking - NOT invented
        // freshly here, just implemented per that direction. First matching row wins (same
        // "first match" convention used throughout this file).
        for (const auto& f : animFiles) {
            for (const auto& s : f.sounds) {
                if (!s.audioEvent || *s.audioEvent != "FOOTSTEP") continue;
                const bool sw0Match = (s.audioSwitch0 && *s.audioSwitch0 == "MATERIAL_TYPE:CONCRETE") ||
                                       (s.audioSwitch1 && *s.audioSwitch1 == "MATERIAL_TYPE:CONCRETE");
                const bool sw1Match = (s.audioSwitch0 && *s.audioSwitch0 == "SHOE_TYPE:SNEAKER") ||
                                      (s.audioSwitch1 && *s.audioSwitch1 == "SHOE_TYPE:SNEAKER");
                if (!sw0Match || !sw1Match) continue;
                footstepEventRowFound = true;
                footstepEventClipFilename = f.filename.value_or(std::string());
                footstepEventFrame = s.frame.value;
                break;
            }
            if (footstepEventRowFound) break;
        }
    } catch (const std::exception&) {
        // anim_files.xtbl not available this run - falls back to the cadence guess below, same
        // honest-fallback convention every other optional docOf() table in this function uses.
    }
    std::printf("footstep : real left-foot/right-foot trigger frames for 'plym_ps_wlk' %s (%zu/%zu) - %s\n",
                walkTriggerFramesFound ? "FOUND" : "NOT found this run", walkLeftFootFrames.size(),
                walkRightFootFrames.size(),
                walkTriggerFramesFound ? "driving real footstep timing" : "falling back to fixed-cadence guess");
    if (footstepEventRowFound) {
        std::printf("footstep : real anim_files.xtbl row for event='FOOTSTEP' "
                    "MATERIAL_TYPE:CONCRETE+SHOE_TYPE:SNEAKER (CHOSEN per purrsian-52) FOUND - "
                    "clip='%s' frame=%d. HONEST GAP: resolving this event+switch identity to a\n"
                    // ^ %s/%d filled below (clip filename, clip-local frame) - keep in sync.

                    "         specific Wwise media id requires walking Event(kind4)->Action(kind3)->\n"
                    "         SwitchContainer, and NO switch-container HIRC layout is reverse-engineered\n"
                    "         anywhere in this project (spec-audio-runtime.md Sec9.6/9.7/9.8 document\n"
                    "         Event->Action->plain-Sound only) - guessing that binary layout here would be\n"
                    "         exactly the kind of invented engine behaviour this project's no-invented-\n"
                    "         fixes rule forbids. The demo therefore still plays the same real, proven-\n"
                    "         decodable SFX clip findFirstPlayableSfxClip() resolves below, now correctly\n"
                    "         labelled as a stand-in for a KNOWN real identity rather than an arbitrary pick.\n",
                    footstepEventClipFilename.c_str(), footstepEventFrame);
    } else {
        std::printf("footstep : no anim_files.xtbl row matched event='FOOTSTEP' with the CHOSEN "
                    "MATERIAL_TYPE:CONCRETE+SHOE_TYPE:SNEAKER switches this run (xtbl not available, or no "
                    "such row) - falling back to findFirstPlayableSfxClip()'s own real-but-unidentified pick.\n");
    }

    sess.listDoc_.emplace(docOf("node_graph_files.xtbl"));
    const auto& listDoc = *sess.listDoc_;
    sess.loadIn_.emplace();
    auto& loadIn = *sess.loadIn_;
    loadIn.registry = &registry;
    loadIn.filters = &filters;
    loadIn.stateActions = &stateActions;
    loadIn.triggers = &triggers;
    loadIn.openFile = miscRead;
    sess.lib_.emplace(sr3animruntime::LoadNodeGraphs(listDoc, loadIn));
    const auto& lib = *sess.lib_;
    if (lib.aborted) {
        std::fprintf(stderr, "node graph load aborted at pass %d, file '%s'\n", lib.abortedPass, lib.abortedFile.c_str());
        return 1;
    }
    std::printf("graphs   : %zu loaded from node_graph_files.xtbl\n", lib.graphs.size());
    const int32_t gi = lib.findGraph("player state machine");
    if (gi < 0) {
        std::fprintf(stderr, "could not find graph 'player state machine' among %zu loaded graphs\n", lib.graphs.size());
        return 1;
    }
    std::printf("graph    : 'player state machine' found (index %d of %zu loaded)\n", gi, lib.graphs.size());

    sess.pool_.emplace();
    auto& pool = *sess.pool_;
    sess.svc_.emplace();
    auto& svc = *sess.svc_;
    svc.tagToRecord = [](int32_t tagIndex) { return tagIndex + 100; };
    svc.duration = [](int32_t) { return 1.0f; };
    sess.bridge_.emplace(lib, gi, registry, pool, svc, motion, filters);
    return -1;
}

int setupPoseAndInput(PlaytestSession& sess) {
    const auto& charactersPath = sess.charactersPath;
    auto& spawnX = sess.spawnX;
    auto& spawnZ = sess.spawnZ;
    auto& animArchiveOverride = sess.animArchiveOverride;
    auto& docOf = sess.docOf;
    const auto& stateActions = *sess.stateActions_;
    auto& walkLeftFootFrames = sess.walkLeftFootFrames;
    auto& walkRightFootFrames = sess.walkRightFootFrames;
    auto& svc = *sess.svc_;
    auto& animArchiveBytes = sess.animArchiveBytes;
    auto& walkClipDurationUnits = sess.walkClipDurationUnits;
    auto& rideSets = sess.rideSets;
    auto& demoVehicleInteractionInfoFound = sess.demoVehicleInteractionInfoFound;
    auto& rideClipRecords = sess.rideClipRecords;
    auto& moveSrc = sess.moveSrc;
    auto& stamina = sess.stamina;
    auto& lookSrc = sess.lookSrc;
    // ---- real U4 pose compose clips - ALWAYS attempted here (runWalkDemo() gates this
    // behind --v2; a first playable build has no "legacy v1" to stay byte-identical to,
    // so it is unconditional). Falls back to the rigid-body render, honestly, if the
    // archive or clips are not found - same fallback runWalkDemo() itself uses. ----------
    sess.clipRegistry_.emplace();
    auto& clipRegistry = *sess.clipRegistry_;
    int32_t runRecord = -1, jogRecord = -1, wlkRecord = -1;
    {
        std::string animArchivePath = animArchiveOverride;
        if (animArchivePath.empty()) {
            const size_t slash = charactersPath.find_last_of("/\\");
            const std::string dir = (slash == std::string::npos) ? std::string() : charactersPath.substr(0, slash + 1);
            animArchivePath = dir + "preload_anim.vpp_pc";
        }
        for (auto& ch : animArchivePath) if (ch == '\\') ch = '/';
        animArchiveBytes = std::make_unique<MappedBytes>(mapFileLargeC(animArchivePath));
        if (animArchiveBytes->empty()) {
            std::printf("anim     : could not read %s - falling back to the rigid-body render\n",
                        animArchivePath.c_str());
        } else {
            try {
                vpp::Container animContainer(vpp::ByteView(animArchiveBytes->data(), animArchiveBytes->size()));
                runRecord = clipRegistry.load(animContainer, "plym_ps_run.anim_pc");
                jogRecord = clipRegistry.load(animContainer, "plym_ps_jog.anim_pc");
                wlkRecord = clipRegistry.load(animContainer, "plym_ps_wlk.anim_pc");
            } catch (const std::exception& ex) {
                std::printf("anim     : %s open failed: %s\n", animArchivePath.c_str(), ex.what());
            }
            std::printf("anim     : real locomotion clips from %s: run=%s jog=%s wlk=%s\n", animArchivePath.c_str(),
                        runRecord >= 0 ? "loaded" : "MISSING", jogRecord >= 0 ? "loaded" : "MISSING",
                        wlkRecord >= 0 ? "loaded" : "MISSING");
        }
        if (runRecord >= 0 || jogRecord >= 0 || wlkRecord >= 0) {
            svc.tagToRecord = [&stateActions, runRecord, jogRecord, wlkRecord](int32_t tagIndex) -> int32_t {
                std::string name;
                if (tagIndex >= 0 && static_cast<size_t>(tagIndex) < stateActions.names().size())
                    name = stateActions.names()[static_cast<size_t>(tagIndex)];
                for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (name == "run" && runRecord >= 0) return runRecord;
                if (name == "jog" && jogRecord >= 0) return jogRecord;
                if (name == "walk" && wlkRecord >= 0) return wlkRecord;
                return tagIndex + 100;
            };
            svc.duration = [&clipRegistry](int32_t record) -> float {
                const sr3animpose::ResolvedClip* rc = clipRegistry.lookup(record);
                return rc != nullptr ? static_cast<float>(rc->animation->durationTotal()) / 30.0f : 1.0f;
            };
        }
    }
    // The walk clip's OWN real durationTotal() (THIS TASK, footstep-real-event) - same
    // "clip header word +0x06" 30Hz-tick unit the xtbl Trigger Frame fields above are authored
    // in (clip_pose_bridge.h's own "TIME UNITS" doc comment) - needed to map a real Trigger
    // Frame into the [0,1) phase space loopPhase (just below) already tracks.
    if (wlkRecord >= 0) {
        if (const sr3animpose::ResolvedClip* rc = clipRegistry.lookup(wlkRecord)) {
            walkClipDurationUnits = static_cast<int32_t>(rc->animation->durationTotal());
        }
    }
    std::printf("footstep : walk clip durationTotal()=%d (30Hz units) - left-foot frames=[%s] right-foot frames=[%s]\n",
                walkClipDurationUnits,
                [&] { std::string s; for (int32_t f : walkLeftFootFrames) s += std::to_string(f) + ","; return s; }().c_str(),
                [&] { std::string s; for (int32_t f : walkRightFootFrames) s += std::to_string(f) + ","; return s; }().c_str());
    sess.loopPhase_.emplace();
    sess.realPoseEnabled = runRecord >= 0 || jogRecord >= 0 || wlkRecord >= 0;

    // ---- seated/driving pose (THIS TASK, 2026-10-09, spec-player-runtime.md Sec7.4b) -------
    // Replaces the standing BIND POSE this command previously rendered (or, before that,
    // hid outright - see this function's own "Character rendering" doc comment just above
    // the render gate below) for a vehicle occupant with a REAL, data-driven seated pose:
    // seat -> real Vehicle_Interaction_Info row's Ride_Animations name -> real vi_ride.xtbl
    // Animation string -> a CHOSEN real .anim_pc clip (sr3player::seated_ride_pose.h owns
    // this whole resolution+application chain - see that header's own banner for exactly
    // what is spec-CONFIRMED vs. CHOSEN). This command's own hardcoded demo vehicle mesh
    // (car_4dr_genki_0.ccar_pc) is a DLC mesh absent from the real vehicles.xtbl manifest
    // (confirmed by the vehicle-occupant-hide-flag task's own investigation, same scope
    // boundary reused here) with no VehicleEntry::vehicleInteractionInfo of its own to read -
    // so, same spirit as that task's own --vehicle-interaction-name override, this command
    // uses a CHOSEN stand-in real row, "truck_2dr_classic01" (an ordinary car, verified this
    // session to have Driver seat Ride_Animations = "standard driver" -> vi_ride.xtbl
    // Animation "drive network"), rather than inventing a fake row for the demo mesh.
    sess.demoVehicleInteractionInfo_.emplace();
    auto& demoVehicleInteractionInfo = *sess.demoVehicleInteractionInfo_;
    try {
        const sr3xtbl::Document rideDoc = docOf("vi_ride.xtbl");
        rideSets = sr3tables_vehicle_world::ParseViRideTable(rideDoc);
        const sr3xtbl::Document infoDoc = docOf("vehicle_interaction_info.xtbl");
        for (const auto& row : sr3tables_vehicle_world::ParseVehicleInteractionInfoTable(infoDoc)) {
            if (row.name.has_value() && *row.name == "truck_2dr_classic01") {
                demoVehicleInteractionInfo = row;
                demoVehicleInteractionInfoFound = true;
                break;
            }
        }
        std::printf("seatpose : vi_ride.xtbl %zu set(s); vehicle_interaction_info.xtbl demo row ('truck_2dr_classic01') "
                    "%s\n",
                    rideSets.size(), demoVehicleInteractionInfoFound ? "found" : "NOT FOUND");
    } catch (const std::exception& ex) {
        std::printf("seatpose : vi_ride.xtbl/vehicle_interaction_info.xtbl not available (%s) - the vehicle occupant "
                    "will stay on its bind pose\n",
                    ex.what());
    }
    int32_t rideRecord = -1;
    if (animArchiveBytes && !animArchiveBytes->empty()) {
        try {
            vpp::Container animContainer(vpp::ByteView(animArchiveBytes->data(), animArchiveBytes->size()));
            rideRecord = clipRegistry.load(animContainer, "auto_ridel.anim_pc");
        } catch (const std::exception& ex) {
            std::printf("seatpose : auto_ridel.anim_pc load failed: %s\n", ex.what());
        }
    }
    std::printf("seatpose : real driver ride clip 'auto_ridel.anim_pc' %s\n",
                rideRecord >= 0 ? "loaded" : "MISSING - occupant will stay on its bind pose");
    if (rideRecord >= 0) rideClipRecords["auto_ridel.anim_pc"] = rideRecord;
    sess.seatedPoseApplier_.emplace();

    // ---- the simulated player: PlaytestMoveSource replaces WalkScriptSource. Live mode
    // polls REAL GetAsyncKeyState; --no-input drives a scripted fallback below in the
    // main loop (SUBSTITUTION 1, see this function's own top doc comment). -------------
    sess.pm_.emplace(moveSrc, stamina);
    auto& pm = *sess.pm_;
    sr3player::SeedOnFootCommonCase(pm.world);
    pm.world.controlsDisabled.set(false);
    pm.world.movementDisabled.set(false);
    pm.world.permissions2078.set(0x4000u | 0x2000u | 0x200u);  // 0x200 = jump/climb permitted (Sec3.6.4, T2)
    // SYNTHETIC tweak values - same precedent as runWalkDemo() (copied from
    // tests/synthetic_player_movement_test.cpp's Rig; no tweak_table.xtbl load here).
    pm.tweaks.maxRunSpeed.set(10);
    pm.tweaks.maxSprintSpeed.set(15);
    pm.tweaks.crouchMoveSpeed.set(4);
    pm.tweaks.maxFineAimSpeed.set(3);
    pm.tweaks.acceleration.set(20);
    pm.tweaks.diveRollAcceleration.set(40);
    pm.tweaks.crouchClickSensitivity.set(0.5);
    pm.warp(sr3player::Vec3{spawnX, 0.0, spawnZ});  // provisional Y - corrected below, see GROUND-Y MEASUREMENT

    // ---- the follow camera: PlaytestLookSource replaces FixedLookSource. Live mode polls
    // REAL mouse deltas; --no-input drives a small scripted pan below (SUBSTITUTION 1). ---
    sess.followCam_.emplace(lookSrc);
    auto& followCam = *sess.followCam_;
    sr3player::SeedFollowCameraOnFootCommonCase(followCam);
    followCam.world.controlsDisabled.set(false);
    followCam.world.sprintPadDescriptorByte5.set(false);
    followCam.target.sizeFactor.set(1.0);
    followCam.target.noCollision.set(false);  // real collision resolve, see RealCameraCollision below
    return -1;
}

} // namespace sr3viewer
