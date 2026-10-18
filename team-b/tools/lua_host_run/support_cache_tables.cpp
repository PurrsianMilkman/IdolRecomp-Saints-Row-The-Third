// lua_host_run helpers: the real cache tables installed into the host's EngineState before any script runs.
// Moved verbatim out of tools/lua_host_run.cpp's former anonymous namespace (see lua_host_run_support.h).
#include "lua_host_run_support.h"

namespace luahostrun {

// --- The zscene / cutscene scene table (spec-lua-api-behaviour.md Sec26.25
// item 5, CONFIRMED; job nnlt) ------------------------------------------------
// 0x0073bfb0's "main" call: names from cutscene.xtbl (inside
// cutscene_tables.vpp), then one <name>.cte_xtbl per name. Where the files
// come from: the engine finds an entry by "a linear, case-insensitive,
// name-string search across every mounted container's directory"
// (spec-vpp-container.md Sec2). CHOSEN: this host searches the one container
// cutscene.xtbl is in, cutscene_tables.vpp_pc at the cache root (which
// containers the engine has mounted at that moment is not specced; every
// shipped .cte_xtbl is in that container). Patch containers: which exist and
// when they are parsed is OPEN, so only the "main" call runs. No container ->
// nothing installed, the table stays OPEN.
// Returns the summary for verdict_summary.txt's zscene_table= line.
std::string installSceneTableFromCache(const fs::path& cacheDir, sr3luahost::EngineState& es) {
    fs::path archivePath;
    for (auto& ent : fs::directory_iterator(cacheDir)) {
        if (ent.is_regular_file() && toLower(ent.path().filename().string()) == "cutscene_tables.vpp_pc") {
            archivePath = ent.path();
            break;
        }
    }
    if (archivePath.empty()) return "not installed (no cutscene_tables.vpp_pc in the cache; the table stays OPEN)";
    std::vector<uint8_t> bytes;
    try {
        bytes = readFile(archivePath);
        vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
        // Entry bytes by case-insensitive name (raw or zlib).
        auto entryBytes = [&c](const std::string& wanted, std::string* foundName) -> std::optional<std::string> {
            const std::string w = toLower(wanted);
            const auto& entries = c.entries();
            for (size_t i = 0; i < entries.size(); ++i) {
                if (toLower(entries[i].name) != w) continue;
                if (foundName) *foundName = entries[i].name;
                if (entries[i].payload.kind == vpp::PayloadKind::Raw) {
                    vpp::ByteView raw = c.rawEntryBytes(i);
                    return std::string(reinterpret_cast<const char*>(raw.data()), raw.size());
                }
                vpp::DecompressResult r = c.decompressEntry(i);
                if (r.status != vpp::DecodeStatus::Ok) return std::nullopt;
                return std::string(reinterpret_cast<const char*>(r.data.data()), r.data.size());
            }
            return std::nullopt;
        };
        std::optional<std::string> names = entryBytes("cutscene.xtbl", nullptr);
        if (!names) return "not installed (cutscene.xtbl not readable in " + archivePath.filename().string() + ")";
        sr3tables_cutscene::SceneTable table = sr3tables_cutscene::BuildMainSceneTable(
            *names, [&entryBytes](const std::string& fileName) -> std::optional<sr3tables_cutscene::OpenedFile> {
                sr3tables_cutscene::OpenedFile f;
                std::optional<std::string> b = entryBytes(fileName, &f.fileName);
                if (!b) return std::nullopt;
                f.bytes = std::move(*b);
                return f;
            });
        std::vector<sr3luahost::EngineState::ZsceneTableRow> rows;
        size_t kind0 = 0, kind1 = 0, kind2 = 0, kindOpen = 0, lightsetUnterminated = 0, soundtrackSplitOpen = 0;
        size_t cameraScriptNames = 0;
        for (const auto& e : table.entries) {
            sr3luahost::EngineState::ZsceneTableRow row;
            row.name = e.name;
            row.crc = e.crc;
            row.kind = e.fields.kind;
            row.cameraScriptName = e.fields.cameraScriptText; // Sec57.2a/Sec10.6 item 7 wiring (2026-10-08)
            if (row.cameraScriptName) ++cameraScriptNames;
            if (e.fields.resources) {
                std::vector<std::string> res;
                for (const auto& r : *e.fields.resources) {
                    res.push_back(r.source == sr3tables_cutscene::SceneResource::Source::AngelFixedNames
                                      ? std::string("(IsAngel: three fixed names, OPEN)")
                                      : (r.textPresent ? r.text : std::string("(Variant absent, OPEN)")));
                }
                row.resources = std::move(res);
            }
            rows.push_back(std::move(row));
            if (!e.fields.kind) ++kindOpen;
            else if (*e.fields.kind == 0) ++kind0;
            else if (*e.fields.kind == 1) ++kind1;
            else ++kind2;
            if (e.fields.lightsetUnterminated) ++lightsetUnterminated;
            if (e.fields.soundtrackSplitOpen) ++soundtrackSplitOpen;
        }
        const size_t installed = rows.size();
        es.installZsceneTable(std::move(rows));

        // Real camera-script byte source (spec-lua-api-behaviour.md
        // Sec57.2a / spec-cutscene-camera-format.md Sec10.6 item 7,
        // 2026-10-08 wiring task): EngineState::cutsceneCameraScriptStep()
        // calls this, by name, for a Zscene/Story-kind scene entry.
        // CHOSEN scope limitation (stated plainly, not hidden): this only
        // searches the SAME container cutscene.xtbl/the .cte_xtbl files
        // came from (archivePath, captured below by an owned COPY of its
        // bytes so the opener outlives this function) - not the real
        // engine's own "linear, case-insensitive, name-string search
        // across every mounted container's directory" (spec-vpp-
        // container.md Sec2, the same rule installSceneTableFromCache's
        // own doc comment above already simplifies this way for
        // .cte_xtbl). A name not found in this one container answers
        // nullopt, which cutsceneCameraScriptStep treats as a REAL
        // "missing script aborts the load" outcome, not a crash.
        auto archiveBytes = std::make_shared<std::vector<uint8_t>>(bytes);
        es.installCameraScriptOpener(
            [archiveBytes](const std::string& fileName) -> std::optional<std::vector<uint8_t>> {
                try {
                    vpp::Container cc(vpp::ByteView(archiveBytes->data(), archiveBytes->size()));
                    const std::string w = toLower(fileName);
                    const auto& entries = cc.entries();
                    for (size_t i = 0; i < entries.size(); ++i) {
                        if (toLower(entries[i].name) != w) continue;
                        if (entries[i].payload.kind == vpp::PayloadKind::Raw) {
                            vpp::ByteView raw = cc.rawEntryBytes(i);
                            return std::vector<uint8_t>(raw.data(), raw.data() + raw.size());
                        }
                        vpp::DecompressResult r = cc.decompressEntry(i);
                        if (r.status != vpp::DecodeStatus::Ok) return std::nullopt;
                        return r.data;
                    }
                    return std::nullopt;
                } catch (const std::exception&) {
                    return std::nullopt;
                }
            });

        return "installed source=" + archivePath.filename().string() +
               " cutscene_elements=" + std::to_string(table.names.cutsceneElements) +
               " names_accepted=" + std::to_string(table.names.accepted.size()) +
               " names_skipped=" + std::to_string(table.names.skipped.size()) +
               " names_case_ambiguous=" + std::to_string(table.names.caseAmbiguous.size()) +
               " capacity=" + std::to_string(table.capacity) + " entries=" + std::to_string(installed) +
               " missing_scene_files=" + std::to_string(table.missingSceneFiles.size()) +
               " unparseable_scene_files=" + std::to_string(table.unparseableSceneFiles.size()) +
               " kind0_designer=" + std::to_string(kind0) + " kind1_zscene=" + std::to_string(kind1) +
               " kind2_story=" + std::to_string(kind2) + " kind_open=" + std::to_string(kindOpen) +
               " lightset_unterminated=" + std::to_string(lightsetUnterminated) +
               " soundtrack_split_open=" + std::to_string(soundtrackSplitOpen) +
               " camera_script_names=" + std::to_string(cameraScriptNames);
    } catch (const std::exception& ex) {
        return std::string("not installed (") + ex.what() + ")";
    }
}

// --- continuous_explosions.xtbl definition table (spec-lua-api-behaviour.md
// Sec28.16 "`continuous_explosion_start`"; spec-tables-weapons-combat.md
// Sec10.2) --------------------------------------------------------------
// The real runtime table (0x014A0310, stride 0x64, cap 3 rows) is read once
// at start-up from continuous_explosions.xtbl via the resolver 0x005eb390 -
// a PLAIN cdecl lookup, NOT the shared 0x02442750 singleton family (Sec28.16
// Body). The file exists in TWO real archives in the real cache with
// different content: misc_tables.vpp_pc (1 row) and patch_compressed.vpp_pc
// (3 rows - exactly the loader's own cap, with zero rows rejected by any of
// Sec10.2's own range checks - spec-tables-weapons-combat.md Sec18.1/
// Sec18.5's own population census, "row count 3 = the loader's own cap
// exactly"). Which container the live game actually mounts LAST for this
// filename (so it is the one 0x005eb390's table-build reads) is not specced
// - Sec10.2's own review status is "NEEDS-DATA" on exactly this question.
// CHOSEN: this host prefers patch_compressed.vpp_pc when present in the
// cache (the copy whose row count already matches the documented cap with
// nothing rejected - the stronger of the two signals for "this is the
// operative copy"), falling back to misc_tables.vpp_pc's copy only if no
// patch_compressed.vpp_pc is in the cache. No DLC archive carries this
// table (Sec10.2), so no further container is searched. Returns the summary
// for verdict_summary.txt's continuous_explosion_table= line.
std::string installContinuousExplosionTableFromCache(const fs::path& cacheDir, sr3luahost::EngineState& es) {
    static const char* const kPreferredArchives[] = {"patch_compressed.vpp_pc", "misc_tables.vpp_pc"};
    std::string notFoundArchives;
    for (const char* archiveName : kPreferredArchives) {
        fs::path archivePath;
        for (auto& ent : fs::directory_iterator(cacheDir)) {
            if (ent.is_regular_file() && toLower(ent.path().filename().string()) == archiveName) {
                archivePath = ent.path();
                break;
            }
        }
        if (archivePath.empty()) {
            if (!notFoundArchives.empty()) notFoundArchives += ", ";
            notFoundArchives += archiveName;
            continue;
        }
        try {
            std::vector<uint8_t> bytes = readFile(archivePath);
            vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
            const auto& entries = c.entries();
            for (size_t i = 0; i < entries.size(); ++i) {
                if (toLower(entries[i].name) != "continuous_explosions.xtbl") continue;
                std::string fileBytes;
                if (entries[i].payload.kind == vpp::PayloadKind::Raw) {
                    vpp::ByteView raw = c.rawEntryBytes(i);
                    fileBytes.assign(reinterpret_cast<const char*>(raw.data()), raw.size());
                } else {
                    vpp::DecompressResult r = c.decompressEntry(i);
                    if (r.status != vpp::DecodeStatus::Ok) {
                        return "not installed (continuous_explosions.xtbl in " + archivePath.filename().string() +
                               " failed to decompress)";
                    }
                    fileBytes.assign(reinterpret_cast<const char*>(r.data.data()), r.data.size());
                }
                sr3xtbl::Document doc = sr3xtbl::ParseDocument(fileBytes);
                std::vector<sr3tables_weapons::ContinuousExplosion> rows = sr3tables_weapons::ParseAllContinuousExplosions(doc);
                std::vector<std::string> ceNames;
                for (const auto& row : rows) {
                    // Every parsed row becomes a real, resolvable definition
                    // name (CONFIRMED set, this host does not reproduce
                    // Sec10.2's own per-row reject-and-drop range checks -
                    // the real population census already found both real
                    // candidate files' rows clean against them).
                    ceNames.push_back(row.name);
                }
                es.continuousExplosion().installNames(ceNames); // table COMPLETE: unknown name = silent no-op (Sec28.16)
                return "installed source=" + archivePath.filename().string() + " rows=" + std::to_string(rows.size());
            }
        } catch (const std::exception& ex) {
            return std::string("not installed (") + ex.what() + ")";
        }
    }
    return "not installed (continuous_explosions.xtbl not found in " + notFoundArchives +
           "; the table stays OPEN)";
}

// --- weapons.xtbl weapon-definition table (spec-lua-api-behaviour.md
// Sec4.10/Sec4.11 "inv_weapon_add_temporary"/"inv_weapon_remove_temporary";
// EngineState::weaponDefinitionResolves() - 2026-10-09 fast-follow, probe
// blockers `m13` ['molotov'], `m19` ['special-sonicgun'], `m24`
// ['pistol-gang']/['rifle-stag']) ----------------------------------------
// The real runtime array both natives' shared `_stricmp` scan searches
// (0x028dc4dc, stride 0x7ec) is NOT a separate thing from this project's
// own already-built, already-proven sr3tables_weapons::Weapon row reader -
// weapons.h's own file-level CONVENTIONS comment documents the exact same
// 0x7EC record stride for the SAME `weapons.xtbl` `<Weapon>` row (spec-
// tables-weapons-combat.md Sec16.4's independent structural cross-check:
// "the last field the weapons reader writes ends exactly at the record
// stride 0x7EC"), so every real `<Weapon>` row this already-existing reader
// successfully parses IS, by construction, a row that would be loaded into
// that exact runtime array - the array's own "loaded" filter bit
// (Sec4.10's own +0x18 bit 0x4) is pure loader-lifecycle bookkeeping, never
// driven by `<Flag>` XML text (weapons.h's own file-level comment already
// puts it out of scope for exactly this reason: a per-row/per-file static
// reader has no "not yet loaded" state of its own to represent). spec-
// tables-weapons-combat.md Sec18.3/Sec18.4's own real-data population
// census found all 82 real base rows structurally clean (every enum/flag
// value documented, no capacity violations, "no value outside any
// documented set was found anywhere in the 82 rows") - this host does not
// reproduce a reject-and-drop loader check for the same reason
// installContinuousExplosionTableFromCache above does not: the real
// population is already known clean against them.
//
// Archives (Sec18.1/Sec18.2, Sec16 - both CONFIRMED, empirical): the 82
// base rows live in `misc_tables.vpp_pc` and ONLY there - unlike
// continuous_explosions.xtbl/spawn_info_ranks.xtbl above, `weapons.xtbl`
// has no larger `patch_compressed.vpp_pc` copy, so there is no archive-
// precedence question to resolve here. The two real DLC extensions
// (`dlc2_weapons.xtbl`, 6 rows; `dlc3_weapons.xtbl`, 3 rows - no DLC1
// weapon-group file exists at all, Sec16) are each a raw top-level entry of
// their own numbered DLC archive ("the DLC archives store their entries
// raw, so their tables are trustworthy", Sec16 Sources). All rows found,
// across every one of these files present, are merged into ONE resolver -
// the real engine's own loader builds the base pass then applies the DLC
// passes into the SAME runtime array (Sec2/Sec2.6), never a separate table
// per archive.
std::string installWeaponDefinitionTableFromCache(const fs::path& cacheDir, sr3luahost::EngineState& es) {
    struct ArchiveFile {
        const char* archive;
        const char* xtbl;
    };
    static const ArchiveFile kFiles[] = {
        {"misc_tables.vpp_pc", "weapons.xtbl"},
        {"dlc2.vpp_pc", "dlc2_weapons.xtbl"},
        {"dlc3.vpp_pc", "dlc3_weapons.xtbl"},
    };
    std::string notFound;
    size_t filesInstalled = 0;
    size_t totalRows = 0;
    std::vector<std::string> allNames;
    for (const ArchiveFile& af : kFiles) {
        fs::path archivePath;
        for (auto& ent : fs::directory_iterator(cacheDir)) {
            if (ent.is_regular_file() && toLower(ent.path().filename().string()) == toLower(std::string(af.archive))) {
                archivePath = ent.path();
                break;
            }
        }
        if (archivePath.empty()) {
            if (!notFound.empty()) notFound += ", ";
            notFound += std::string(af.archive) + " (for " + af.xtbl + ")";
            continue;
        }
        try {
            std::vector<uint8_t> bytes = readFile(archivePath);
            vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
            const auto& entries = c.entries();
            bool found = false;
            for (size_t i = 0; i < entries.size(); ++i) {
                if (toLower(entries[i].name) != toLower(std::string(af.xtbl))) continue;
                found = true;
                std::string fileBytes;
                if (entries[i].payload.kind == vpp::PayloadKind::Raw) {
                    vpp::ByteView raw = c.rawEntryBytes(i);
                    fileBytes.assign(reinterpret_cast<const char*>(raw.data()), raw.size());
                } else {
                    vpp::DecompressResult r = c.decompressEntry(i);
                    if (r.status != vpp::DecodeStatus::Ok) {
                        if (!notFound.empty()) notFound += ", ";
                        notFound += std::string(af.xtbl) + " (failed to decompress in " + archivePath.filename().string() + ")";
                        break;
                    }
                    fileBytes.assign(reinterpret_cast<const char*>(r.data.data()), r.data.size());
                }
                sr3xtbl::Document doc = sr3xtbl::ParseDocument(fileBytes);
                std::vector<sr3tables_weapons::Weapon> rows = sr3tables_weapons::ParseAllWeapons(doc);
                for (const auto& row : rows) {
                    // Every parsed row becomes a real, resolvable weapon-
                    // definition name (CONFIRMED set, this function's own top
                    // comment) - case-folded via the SAME AsciiLowerFold the
                    // native stubs' own `_stricmp`-equivalent lookup already
                    // uses (engine_state.h's weaponDefinitionResolves() doc
                    // comment).
                    allNames.push_back(row.name);
                    es.setWeaponDefinitionClass(row.name, row.weaponClass);
                }
                ++filesInstalled;
                totalRows += rows.size();
                break;
            }
            if (!found) {
                if (!notFound.empty()) notFound += ", ";
                notFound += std::string(af.xtbl) + " (not found in " + archivePath.filename().string() + ")";
            }
        } catch (const std::exception& ex) {
            if (!notFound.empty()) notFound += ", ";
            notFound += std::string(af.xtbl) + " (" + ex.what() + ")";
        }
    }
    if (filesInstalled > 0) es.installWeaponDefinitionNames(allNames);
    std::string summary = "installed files=" + std::to_string(filesInstalled) + "/" +
                           std::to_string(sizeof(kFiles) / sizeof(kFiles[0])) +
                           " rows=" + std::to_string(totalRows) +
                           " names_resolved=" + std::to_string(es.weaponDefinitionResolves().knownCount());
    if (!notFound.empty()) summary += " missing=" + notFound;
    return summary;
}

// items_inventory.xtbl (+ DLC) row names -> inv_item_remove resolver (spec-lua-api-behaviour.md Sec17.25
// "Item lookup source", spec-tables-vehicle-world.md Sec15; DLC files are <framework>_items_inventory.xtbl).
std::string installInventoryItemTableFromCache(const fs::path& cacheDir, sr3luahost::EngineState& es) {
    static const std::pair<const char*, const char*> kFiles[] = {
        {"misc_tables.vpp_pc", "items_inventory.xtbl"}, {"dlc1.vpp_pc", "dlc1_items_inventory.xtbl"},
        {"dlc2.vpp_pc", "dlc2_items_inventory.xtbl"}, {"dlc3.vpp_pc", "dlc3_items_inventory.xtbl"}};
    std::vector<std::string> names;
    size_t files = 0;
    for (const auto& af : kFiles) {
        try {
            for (auto& ent : fs::directory_iterator(cacheDir)) {
                if (!ent.is_regular_file() || toLower(ent.path().filename().string()) != af.first) continue;
                std::vector<uint8_t> bytes = readFile(ent.path());
                vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
                const auto& entries = c.entries();
                for (size_t i = 0; i < entries.size(); ++i) {
                    if (toLower(entries[i].name) != af.second) continue;
                    std::string fileBytes;
                    if (entries[i].payload.kind == vpp::PayloadKind::Raw) {
                        vpp::ByteView raw = c.rawEntryBytes(i);
                        fileBytes.assign(reinterpret_cast<const char*>(raw.data()), raw.size());
                    } else {
                        vpp::DecompressResult r = c.decompressEntry(i);
                        if (r.status != vpp::DecodeStatus::Ok) break;
                        fileBytes.assign(reinterpret_cast<const char*>(r.data.data()), r.data.size());
                    }
                    for (const auto& it : sr3tables_vehicle_world::ParseItemsInventoryTable(sr3xtbl::ParseDocument(fileBytes)))
                        if (it.name) names.push_back(*it.name);
                    ++files;
                    break;
                }
                break;
            }
        } catch (const std::exception&) {}
    }
    if (files > 0) es.installInventoryItemNames(names);
    return "installed files=" + std::to_string(files) + "/4 names=" + std::to_string(names.size());
}

// --- spawn_info_ranks.xtbl Hit_Points values (spec-tables-progression.md
// Sec10.6/Sec14.12; EngineState::drawOrGetMaxHitPoints, spec-lua-api-behaviour.md
// Sec7.12/Sec34.2 - 2026-10-07 closure of the max-hit-points random-per-
// spawn model, probe_blockers_ranked_20261007.md item 2, mission `m18`)
// -------------------------------------------------------------------------
// This is NOT a resolver (no attempt is made here to pick which <Rank>
// record applies to a given bare Lua name - that resolution is this
// project's own unmodeled gap, see CharacterState::maxHitPoints's own doc
// comment). It only installs the REAL set of distinct Hit_Points values the
// table actually ships, so drawOrGetMaxHitPoints's own uniform draw is
// grounded in real shipped data rather than an invented range.
//
// SAME two-archive situation as continuous_explosions.xtbl above, same
// archive-priority mechanism (spec-tables-progression.md Sec14.2's own
// boot/mount trace, generalized explicitly to this table in that same
// section's text, and restated at Sec14.12): `misc_tables.vpp_pc` carries a
// 54-row copy, `patch_compressed.vpp_pc` carries a 55-row superset (the
// extra row is `Homie - Kwilanna`) and the patch copy is the one the real
// shipped game actually loads. 2026-10-07 fast-follow fix (flagged, not
// fixed, by commit 50bdb29's own reviewer): this loader used to read ONLY
// misc_tables.vpp_pc, skipping that same precedence rule - non-observable
// so far only because both archives happen to yield the same 30 distinct
// Hit_Points values (`Homie - Kwilanna`'s own value is a duplicate of one
// already in the 54-row set), not because the rule didn't matter. CHOSEN:
// same kPreferredArchives order as installContinuousExplosionTableFromCache
// above - prefer patch_compressed.vpp_pc, fall back to misc_tables.vpp_pc
// only if no patch_compressed.vpp_pc is in the cache. No DLC archive
// carries this table either (same census method as the continuous-
// explosion loader above). Returns the summary for verdict_summary.txt's
// spawn_rank_hit_points= line.
std::string installSpawnRankHitPointsFromCache(const fs::path& cacheDir, sr3luahost::EngineState& es) {
    static const char* const kPreferredArchives[] = {"patch_compressed.vpp_pc", "misc_tables.vpp_pc"};
    std::string notFoundArchives;
    for (const char* archiveName : kPreferredArchives) {
        fs::path archivePath;
        for (auto& ent : fs::directory_iterator(cacheDir)) {
            if (ent.is_regular_file() && toLower(ent.path().filename().string()) == archiveName) {
                archivePath = ent.path();
                break;
            }
        }
        if (archivePath.empty()) {
            if (!notFoundArchives.empty()) notFoundArchives += ", ";
            notFoundArchives += archiveName;
            continue;
        }
        try {
            std::vector<uint8_t> bytes = readFile(archivePath);
            vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
            const auto& entries = c.entries();
            for (size_t i = 0; i < entries.size(); ++i) {
                if (toLower(entries[i].name) != "spawn_info_ranks.xtbl") continue;
                std::string fileBytes;
                if (entries[i].payload.kind == vpp::PayloadKind::Raw) {
                    vpp::ByteView raw = c.rawEntryBytes(i);
                    fileBytes.assign(reinterpret_cast<const char*>(raw.data()), raw.size());
                } else {
                    vpp::DecompressResult r = c.decompressEntry(i);
                    if (r.status != vpp::DecodeStatus::Ok) {
                        return "not installed (spawn_info_ranks.xtbl in " + archivePath.filename().string() +
                               " failed to decompress)";
                    }
                    fileBytes.assign(reinterpret_cast<const char*>(r.data.data()), r.data.size());
                }
                sr3xtbl::Document doc = sr3xtbl::ParseDocument(fileBytes);
                std::vector<sr3tables_progression::SpawnRank> rows = sr3tables_progression::ParseSpawnInfoRanksTable(doc);
                std::set<int32_t> distinctValues;
                for (const auto& row : rows) distinctValues.insert(static_cast<int32_t>(row.hitPoints.value));
                std::vector<int32_t> values(distinctValues.begin(), distinctValues.end());
                es.installSpawnRankHitPoints(values);
                return "installed source=" + archivePath.filename().string() + " rows=" + std::to_string(rows.size()) +
                       " distinct_hit_points_values=" + std::to_string(values.size());
            }
        } catch (const std::exception& ex) {
            return std::string("not installed (") + ex.what() + ")";
        }
    }
    return "not installed (spawn_info_ranks.xtbl not found in " + notFoundArchives +
           "; the draw falls back to the CHOSEN 100)";
}

// --- Vehicle-type class table real-cache install (2026-10-10, vehicle-class
// xtbl wiring task): wires the existing, already-correct sr3vehicleinfo xtbl
// reader (include/sr3vehicleinfo/vehicle_entry.h, spec-vehicle-data.md
// section 7) into this host for the first time. Previously
// sr3vehicleinfo::ParseAllVehicles was a fully-built, standalone parser with
// nothing in this project's scope ever feeding its output into the Lua
// host's own EngineState - this closes that gap.
//
// Archive list: the 4 archives spec-vehicle-data.md Sec7.9 confirms carry
// real, readable per-vehicle-type tables once the mode-(a) container fix
// landed ("all real <name>_veh.xtbl files are now readable: 106 in
// misc_tables.vpp_pc + 17 in dlc1/2/3.vpp_pc = 123 total").
//
// MUST run before installZoneObjectsFromCache() below (see call order in
// main()) - EngineState::loadZoneObjectsSection() looks up a zone-placed
// vehicle's own `script_vehicle_type_name` property against this table at
// ingest time; installing the table after ingestion would silently miss
// every real vehicle instance ingested before it (installZoneObjectsFromCache's
// own re-ingestion is NOT performed by this tool, so the table must already
// be in place the first and only time each file is ingested).
//
// Recursive walk (same small-per-tool, duplicated-with-attribution
// convention as walkCzn() below, and validate_vehicleinfo_population.cpp's
// own walk()) rather than a flat per-archive scan - matches that tool's own
// finding that a real `<name>_veh.xtbl` entry can sit inside a nested
// container, not only at an archive's own top level.
std::string installVehicleTypeClassTableFromCache(const fs::path& cacheDir, sr3luahost::EngineState& es) {
    static const char* const kArchives[] = {"misc_tables.vpp_pc", "dlc1.vpp_pc", "dlc2.vpp_pc", "dlc3.vpp_pc"};
    std::unordered_map<std::string, int32_t> classByLowerName;
    std::unordered_map<std::string, int32_t> specialByLowerName;
    size_t filesParsed = 0;
    std::string notFoundArchives;

    std::function<void(vpp::Container&)> walk = [&](vpp::Container& c) {
        const auto& entries = c.entries();
        for (size_t i = 0; i < entries.size(); ++i) {
            const std::string& name = entries[i].name;
            const bool isVeh = name.size() > 9 && toLower(name.substr(name.size() - 9)) == "_veh.xtbl";
            if (isVeh) {
                try {
                    std::string fileBytes;
                    if (entries[i].payload.kind == vpp::PayloadKind::Raw) {
                        vpp::ByteView raw = c.rawEntryBytes(i);
                        fileBytes.assign(reinterpret_cast<const char*>(raw.data()), raw.size());
                    } else {
                        vpp::DecompressResult r = c.decompressEntry(i);
                        if (r.status != vpp::DecodeStatus::Ok) continue;
                        fileBytes.assign(reinterpret_cast<const char*>(r.data.data()), r.data.size());
                    }
                    sr3xtbl::Document doc = sr3xtbl::ParseDocument(fileBytes);
                    std::vector<sr3vehicleinfo::VehicleEntry> rows = sr3vehicleinfo::ParseAllVehicles(doc);
                    for (const auto& row : rows) {
                        if (!row.name.has_value() || row.name->empty()) continue;
                        specialByLowerName[sr3luahost::AsciiLowerFold(*row.name)] = row.specialVehicleType < 0 ? 0 : row.specialVehicleType; // Special_Vehicle_Type, a missing tag is 0 (spec Sec33.1)
                        if (row.vehicleType.vehicleClass == sr3vehicleinfo::VehicleClass::Unknown) continue;
                        classByLowerName[sr3luahost::AsciiLowerFold(*row.name)] =
                            static_cast<int32_t>(row.vehicleType.vehicleClass);
                    }
                    ++filesParsed;
                } catch (const std::exception&) {
                    // malformed/unreadable entry - skip, never fabricated
                }
                continue;
            }
            if (entries[i].payload.kind != vpp::PayloadKind::Raw) continue;
            if (toLower(name).size() >= 4 && toLower(name).compare(toLower(name).size() - 4, 4, "xtbl") == 0) continue;
            try {
                vpp::Container nested(c.rawEntryBytes(i));
                walk(nested);
            } catch (const std::exception&) {
                // not a nested container - not every raw entry is one
            }
        }
    };

    for (const char* archiveName : kArchives) {
        fs::path archivePath;
        for (auto& ent : fs::directory_iterator(cacheDir)) {
            if (ent.is_regular_file() && toLower(ent.path().filename().string()) == archiveName) {
                archivePath = ent.path();
                break;
            }
        }
        if (archivePath.empty()) {
            if (!notFoundArchives.empty()) notFoundArchives += ", ";
            notFoundArchives += archiveName;
            continue;
        }
        try {
            std::vector<uint8_t> bytes = readFile(archivePath);
            vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
            walk(c);
        } catch (const std::exception& ex) {
            if (!notFoundArchives.empty()) notFoundArchives += ", ";
            notFoundArchives += std::string(archiveName) + " (" + ex.what() + ")";
        }
    }

    es.installVehicleTypeClassTable(classByLowerName);
    es.installVehicleSpecialTypeTable(specialByLowerName);
    std::string summary = "installed files=" + std::to_string(filesParsed) +
                           " types_resolved=" + std::to_string(es.vehicleTypeClassTableSize());
    if (!notFoundArchives.empty()) summary += " missing=" + notFoundArchives;
    return summary;
}

// --- Zone Objects (0x2234) real-cache install (2026-10-08, zone-data
// fast-follow): wires sr3zone::ZoneObjects - the existing, already-proven
// `.czn_pc` Objects reader (include/sr3zone/zone_objects.h, spec-zone-
// data-format.md Sec11) - into this host's own named-object lookup
// (EngineState::loadZoneObjectsSection(), include/sr3luahost/
// zone_object_resolver.h) for the first time, against REAL shipped zone
// data. Previously NOTHING in this project's scope ever fed a real
// `.czn_pc` into the Lua host - objectResolves()/CharacterState::
// scriptNpcHp were only ever reachable via a test manually calling
// markScriptNpcBoundForTesting()/applyScriptNpcHitPointsForTesting()
// (commit bc7ff16). This closes that gap for a real mission-drive run.
//
// Archive list: the SAME 10 archives spec-zone-data-format.md's own Sec9.4
// population census uses ("every `.czn_pc`/`.gzn_pc` filename pair found
// ... in sr3_city_0.vpp_pc, sr3_city_1.vpp_pc, sr3_city_missions.vpp_pc,
// dlc1.vpp_pc, dlc2.vpp_pc, dlc3.vpp_pc, misc.vpp_pc, startup.vpp_pc,
// patch_compressed.vpp_pc, patch_uncompressed.vpp_pc - 1,083 pairs") - a
// CHOSEN, spec-backed scope (not "every archive in the cache", and not a
// guess at which archives matter).
//
// A third, dedicated recursive walk (leaf test: ".czn_pc") - same
// duplicated-with-attribution convention as the walk()/walkAsm() pair
// below this function (this file's own top comment: a small recursive
// walk per tool/per leaf-type, not factored into one shared traversal).
// Unlike those two, this walk ingests each leaf immediately (one
// sr3zone::ZoneObjects::locate() + EngineState::loadZoneObjectsSection()
// call per `.czn_pc` found) rather than collecting every buffer first -
// 1,083 real files is not large, but there is no reason to hold them all
// in memory at once when each one is fully consumed on sight.
namespace zoneobj {

bool endsWithCznExt(const std::string& lowerName) {
    static const std::string ext = ".czn_pc";
    if (lowerName.size() < ext.size()) return false;
    return lowerName.compare(lowerName.size() - ext.size(), ext.size(), ext) == 0;
}

struct Counters {
    uint64_t cznFilesFound = 0;
    uint64_t cznWithSection = 0;
    uint64_t cznSectionsIncomplete = 0; // located but ZoneObjects' own recordCount could not be satisfied - skipped, not ingested
};

void ingestCznBuffer(sr3luahost::EngineState& es, Counters& ctr, const uint8_t* data, size_t len) {
    ++ctr.cznFilesFound;
    std::vector<sr3zone::ObjectsSection> sections;
    try {
        sections = sr3zone::ZoneObjects::locate(vpp::ByteView(data, len));
    } catch (const std::exception&) {
        return; // malformed candidate - ZoneObjects::locate() already only returns bounds-checked sections; never silently fabricated
    }
    for (const sr3zone::ObjectsSection& section : sections) {
        if (!section.complete) {
            ++ctr.cznSectionsIncomplete;
            continue; // see ObjectsSection::complete's own doc comment - never ingest a section the reader itself flagged as truncated
        }
        ++ctr.cznWithSection;
        es.loadZoneObjectsSection(section);
    }
}

void walkCzn(sr3luahost::EngineState& es, Counters& ctr, const vpp::Container& c) {
    const auto& entries = c.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        std::string name = e.name.empty() ? std::string() : e.name;
        bool isCzn = endsWithCznExt(toLower(name));

        if (e.payload.kind == vpp::PayloadKind::Raw) {
            bool nested = false;
            try {
                vpp::Container child = c.openNested(i);
                nested = true;
                walkCzn(es, ctr, child);
            } catch (const std::exception&) {
                nested = false;
            }
            if (!nested && isCzn) {
                vpp::ByteView raw = c.rawEntryBytes(i);
                ingestCznBuffer(es, ctr, raw.data(), raw.size());
            }
        } else {
            vpp::DecompressResult result = c.decompressEntry(i);
            bool ok = result.status == vpp::DecodeStatus::Ok || result.status == vpp::DecodeStatus::SizeMismatch ||
                      result.status == vpp::DecodeStatus::OkUnconfirmedContent ||
                      result.status == vpp::DecodeStatus::ContentValidated ||
                      result.status == vpp::DecodeStatus::RecoveredSharedStream ||
                      result.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
            if (!ok) continue;
            bool nested = false;
            if (result.data.size() >= 4) {
                uint32_t magic = (uint32_t)result.data[0] | ((uint32_t)result.data[1] << 8) |
                                  ((uint32_t)result.data[2] << 16) | ((uint32_t)result.data[3] << 24);
                if (magic == 0x51890ACEu) {
                    try {
                        vpp::Container child(vpp::ByteView(result.data.data(), result.data.size()));
                        nested = true;
                        walkCzn(es, ctr, child);
                    } catch (const std::exception&) {
                        nested = false;
                    }
                }
            }
            if (!nested && isCzn) {
                ingestCznBuffer(es, ctr, result.data.data(), result.data.size());
            }
        }
    }
}

} // namespace zoneobj

std::string installZoneObjectsFromCache(const fs::path& cacheDir, sr3luahost::EngineState& es) {
    static const char* const kArchives[] = {
        "sr3_city_0.vpp_pc",        "sr3_city_1.vpp_pc", "sr3_city_missions.vpp_pc",
        "dlc1.vpp_pc",              "dlc2.vpp_pc",       "dlc3.vpp_pc",
        "misc.vpp_pc",              "startup.vpp_pc",
        "patch_compressed.vpp_pc",  "patch_uncompressed.vpp_pc",
    };
    zoneobj::Counters ctr;
    std::string archivesMissing;
    size_t archivesFound = 0;
    for (const char* archiveName : kArchives) {
        fs::path archivePath;
        for (auto& ent : fs::directory_iterator(cacheDir)) {
            if (ent.is_regular_file() && toLower(ent.path().filename().string()) == toLower(std::string(archiveName))) {
                archivePath = ent.path();
                break;
            }
        }
        if (archivePath.empty()) {
            if (!archivesMissing.empty()) archivesMissing += ", ";
            archivesMissing += archiveName;
            continue;
        }
        ++archivesFound;
        try {
            std::vector<uint8_t> bytes = readFile(archivePath);
            vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
            zoneobj::walkCzn(es, ctr, c);
        } catch (const std::exception& ex) {
            return "not installed (" + std::string(archiveName) + ": " + ex.what() + ")";
        }
    }
    std::string summary = "installed archives=" + std::to_string(archivesFound) + "/" +
                           std::to_string(sizeof(kArchives) / sizeof(kArchives[0])) +
                           " czn_files_found=" + std::to_string(ctr.cznFilesFound) +
                           " objects_sections=" + std::to_string(ctr.cznWithSection) +
                           " sections_incomplete=" + std::to_string(ctr.cznSectionsIncomplete) +
                           " named_objects_resolved=" + std::to_string(es.zoneObjectsLoadedCount());
    if (!archivesMissing.empty()) summary += " missing_archives=" + archivesMissing;
    return summary;
}

} // namespace luahostrun
