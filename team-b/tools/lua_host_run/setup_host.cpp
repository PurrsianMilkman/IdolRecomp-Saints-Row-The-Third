// lua_host_run main() split: host construction, cache tables, hook census, archive walk, .vint_doc context, state tags, preloads.
// Code moved verbatim out of the former single-function main() (see lua_host_run_session.h).
#include "lua_host_run_session.h"

namespace luahostrun {

void setupHost(LuaHostRunSession& sess) {
    auto& hostRng = sess.hostRng;
    auto& hostRngSeed = sess.hostRngSeed;
    auto& displayInit = sess.displayInit;
    auto& displayWidth = sess.displayWidth;
    auto& displayHeight = sess.displayHeight;
    auto& probeUnresolvedNames = sess.probeUnresolvedNames;
    auto& cacheDir = sess.cacheDir;
    auto& registrationListPath = sess.registrationListPath;
    auto& allNames = sess.allNames;
    auto& displayOption = sess.displayOption;
    auto& sceneTableSummary = sess.sceneTableSummary;
    auto& continuousExplosionTableSummary = sess.continuousExplosionTableSummary;
    auto& spawnRankHitPointsSummary = sess.spawnRankHitPointsSummary;
    auto& weaponDefinitionTableSummary = sess.weaponDefinitionTableSummary;
    auto& vehicleTypeClassTableSummary = sess.vehicleTypeClassTableSummary;

    // --- Load the real (current-count) registration list and build the host ---
    allNames = loadTaggedRegistrationList(registrationListPath);
    std::cout << "Loaded " << allNames.size() << " registered names from " << registrationListPath << "\n";
    sess.host_.emplace(allNames);
    auto& host = *sess.host_;
    // --host-rng (HYPOTHESIS / host substitute, opt-in): before any script draws.
    if (hostRng) host.useHostRng(hostRngSeed);
    // --probe-unresolved-names (DIAGNOSTIC ONLY, opt-in, see usage text above
    // and EngineState::enableUnresolvedNameProbe()/probedFieldOr()'s own doc
    // comments): widens the named-object resolution gate, and lets specific
    // per-character/per-vehicle OPEN fields (those routed through
    // probedFieldOr(), e.g. max hit points/ignore-AI) also fall back to a
    // labelled placeholder for a name this same run already probed.
    if (probeUnresolvedNames) {
        host.engineState().enableUnresolvedNameProbe();
        // Same single switch also widens get_dist's own worldPosition gate
        // (EngineState::worldPositionOr(), 2026-10-03 peer follow-up) - a
        // second, deliberately separate DIAGNOSTIC ONLY extension, not
        // gated on probedFieldOr()'s "owner name was itself probed" rule
        // (see that method's own doc comment for why).
        host.engineState().enableWorldPositionProbe();
    }
    // Front-end bring-up, before any script (Sec26.24/Sec26.26: the UI
    // subsystem init 0x008489e0 -> 0x00e23910 runs before the screen_fade
    // init and before UI scripts): the display resolution (host input).
    if (displayInit) {
        host.engineState().vintUiSubsystemInit(displayWidth, displayHeight);
        displayOption = std::to_string(displayWidth) + "x" + std::to_string(displayHeight) +
                        " (UI subsystem init 0x00e23910 run with this host display - CHOSEN default 1280x720 unless "
                        "--display=WxH; a host input, not an engine value; display_mode=" +
                        (host.engineState().vintDisplayMode().known()
                             ? std::to_string(host.engineState().vintDisplayMode().get())
                             : std::string("OPEN")) +
                        ")";
    } else {
        displayOption = "none (--display=none: UI subsystem init not run, the width/height record stays OPEN)";
    }
    // The scene table (Sec26.25 item 5): real data from the cache.
    sceneTableSummary = installSceneTableFromCache(cacheDir, host.engineState());
    // The continuous-explosion definition table (Sec28.16): real data from the cache.
    continuousExplosionTableSummary = installContinuousExplosionTableFromCache(cacheDir, host.engineState());
    // The spawn-rank Hit_Points value set (Sec7.12/Sec34.2): real data from the cache.
    spawnRankHitPointsSummary = installSpawnRankHitPointsFromCache(cacheDir, host.engineState());
    // The weapon-definition name table (Sec4.10/Sec4.11): real data from the cache.
    weaponDefinitionTableSummary = installWeaponDefinitionTableFromCache(cacheDir, host.engineState());
    (void)installInventoryItemTableFromCache(cacheDir, host.engineState());
    // The vehicle-type class table (spec-vehicle-data.md Sec7, vehicle-class
    // xtbl wiring task): real data from the cache. MUST run before
    // installZoneObjectsFromCache() below - see that function's own top
    // comment for why.
    vehicleTypeClassTableSummary =
        installVehicleTypeClassTableFromCache(cacheDir, host.engineState());
    // Real `.czn_pc` Objects (0x2234) data (spec-zone-data-format.md Sec11):
    // the real name<->ObjectRecord resolver (2026-10-08 zone-data
    // fast-follow) - see installZoneObjectsFromCache's own top comment.
    const std::string zoneObjectsSummary = installZoneObjectsFromCache(cacheDir, host.engineState());
    std::cout << "display_option=" << displayOption << "\nzscene_table=" << sceneTableSummary << "\n";
    std::cout << "continuous_explosion_table=" << continuousExplosionTableSummary << "\n";
    std::cout << "spawn_rank_hit_points=" << spawnRankHitPointsSummary << "\n";
    std::cout << "weapon_definition_table=" << weaponDefinitionTableSummary << "\n";
    std::cout << "vehicle_type_class_table=" << vehicleTypeClassTableSummary << "\n";
    std::cout << "zone_objects=" << zoneObjectsSummary << "\n";
    // NEW 2026-10-10 (vehicle-class xtbl wiring task): a direct, named
    // real-data check for the one vehicle instance the removed
    // "m05_heli_pierce" hardcoded special case used to cover unconditionally
    // - reports whether it now resolves its real class via the general,
    // data-driven path (real ingested zone data joined against the real
    // xtbl-sourced vehicle-type table) rather than assuming it still does
    // just because the old special case is gone from the source. Printed
    // unconditionally (cheap, one lookup) so every real-cache run's own
    // stdout/verdict_summary.txt makes this fact plain either way.
    {
        const sr3zone::ObjectRecord* m05Heli = host.engineState().resolveZoneObject("m05_heli_pierce");
        std::string m05HeliStatus;
        if (!m05Heli) {
            m05HeliStatus = "not found in any ingested real zone data";
        } else {
            std::optional<std::string> typeName = sr3zone::ScriptVehicleTypeName(*m05Heli);
            std::optional<std::string> className = sr3zone::ClassNameForHash(m05Heli->classHash);
            bool flyingTypeKnown = host.engineState().hasVehicle("m05_heli_pierce") &&
                                   host.engineState().getOrCreateVehicle("m05_heli_pierce").flyingType.known();
            m05HeliStatus = "found class=" + (className.has_value() ? *className : std::string("(unresolved)")) +
                            " script_vehicle_type_name=" + (typeName.has_value() ? *typeName : std::string("(none)")) +
                            " flyingType_known=" + (flyingTypeKnown ? "true" : "false");
            if (flyingTypeKnown) {
                m05HeliStatus += " flyingType_value=" +
                                  std::to_string(host.engineState().getOrCreateVehicle("m05_heli_pierce").flyingType.get());
            }
        }
        std::cout << "m05_heli_pierce_check=" << m05HeliStatus << "\n";
    }
    std::cout << "Host built: gameplay state has " << host.gameplayStubCount()
              << " stubs, UI state has " << host.uiStubCount() << " stubs ("
              << (host.gameplayStubCount() + host.uiStubCount()) << " total, matching the "
              << allNames.size() << "-name input list)\n";
    std::cout << "  plus the " << bareGlobalRoster().size() << " bare globals of 0x00e0f900 (spec-lua-bindings.md "
              << "Sec13.2/Sec16.4, thread_new/_yield/_kill/_check_done among them) and the scaffold's "
              << "thread_close, all into BOTH states - see include/sr3luahost/bare_globals.h.\n";
}

void reportOpenStateAndHookCensus(LuaHostRunSession& sess) {
    auto& outDir = sess.outDir;
    auto& allNames = sess.allNames;
    auto& host = *sess.host_;
    auto& openStateSummary = sess.openStateSummary;
    auto& hookNamesAlsoRegisteredAsStubs = sess.hookNamesAlsoRegisteredAsStubs;

    // --- OPEN engine-state slots at start (after applySpecInitialState) -----
    // verdict_open_state.tsv: which slots a synced Team A answer has filled.
    {
        std::ofstream os(outDir / "verdict_open_state.tsv");
        os << "area\tglobal\tspec\tkind\tknown\tknown_keys\n";
        size_t known = 0, total = 0;
        for (const auto& r : host.engineState().openSlotInventory()) {
            os << r.area << '\t' << r.global << '\t' << r.spec << '\t' << r.kind << '\t' << (r.known ? 1 : 0) << '\t'
               << r.knownKeys << '\n';
            ++total;
            known += (r.known || r.knownKeys > 0) ? 1 : 0;
        }
        openStateSummary = std::to_string(known) + "/" + std::to_string(total) +
                           " (slots with any value at start; per slot: verdict_open_state.tsv)";
    }

    // --- The confirmed/pattern-based hook census this run fires ----------
    const std::vector<HookSpec>& hooks = confirmedHooks();
    sess.hooks_ = &hooks;
    {
        size_t g1 = 0, g2 = 0, g3 = 0;
        for (auto& h : hooks) {
            if (h.group == HookGroup::Group1_GeneralNamed) ++g1;
            else if (h.group == HookGroup::Group2_LifecycleUnconditional) ++g2;
            else ++g3;
        }
        std::cout << "\nLoaded " << hooks.size() << " confirmed/pattern-based hook names to fire "
                  << "per script (spec-lua-bindings.md Sec8.2-8.4/Sec12.2-12.9): " << g1
                  << " Group-1 general (existence-gated, CONFIRMED), " << g2
                  << " Group-2 lifecycle (UNCONDITIONAL real call, CONFIRMED), " << g3
                  << " Group-3 mission-numbered (existence-gated, HIGH CONFIDENCE / pattern-based, "
                  << "NOT individually dispatcher-confirmed).\n";
        std::cout << "  Every hook is called with ZERO arguments - spec Sec8.3 confirms the call-in "
                  << "MECHANISM only, not per-hook argument shapes, which remain OPEN. NOT fired: "
                  << "Group 4 (7 sprintf-templated per-widget-instance families, spec Sec8.4) and "
                  << "Group 5 (2 data-driven runtime-populated tables, spec Sec8.5) - both are "
                  << "explicitly unbounded/runtime-only per the spec itself, so no static name list "
                  << "exists to fire; see include/sr3luahost/hook_registry.h's own top comment.\n";
    }

    // --- A real, checked subtlety worth stating plainly rather than
    // leaving implicit: does any confirmed/pattern-based hook name ALSO
    // appear in the registered stub list? If so, that hook's "existence"
    // check can read true from the moment Host is constructed - via the
    // pre-registered generic logging stub sitting in that name already -
    // even before any real script defines its own version of that name.
    // Once a real script DOES assign its own function to that same
    // global, ordinary Lua semantics mean the real function replaces the
    // stub for every later firing pass (shared, persistent globals - see
    // this file's own top comment) - so such a hook's own ok/err split
    // over the whole run can be a genuine mixture of "hit the always-
    // succeeding generic stub" and "hit a real, possibly-erroring
    // script-defined override," not one single, uniform behavior
    // throughout. Computed here, not asserted - see the printed names
    // below (0 names is a real, checked negative, not an unexamined one). --
    {
        std::unordered_map<std::string, std::string> registeredCluster;
        for (auto& rn : allNames) registeredCluster[rn.name] = rn.cluster;
        for (auto& h : hooks) {
            auto it = registeredCluster.find(h.name);
            if (it != registeredCluster.end()) {
                hookNamesAlsoRegisteredAsStubs.push_back(h.name + " (registered cluster=" + it->second + ")");
            }
        }
    }
    std::cout << "  Checked overlap between the " << hooks.size() << " hook names and the " << allNames.size()
              << "-name registered stub list: " << hookNamesAlsoRegisteredAsStubs.size() << " name(s) appear in "
              << "BOTH (see this block's own comment for why that matters to how that hook's own numbers "
              << "should be read).";
    if (!hookNamesAlsoRegisteredAsStubs.empty()) {
        std::cout << " Name(s): ";
        for (size_t i = 0; i < hookNamesAlsoRegisteredAsStubs.size(); ++i) {
            if (i) std::cout << ", ";
            std::cout << hookNamesAlsoRegisteredAsStubs[i];
        }
    }
    std::cout << "\n";
}

void walkArchives(LuaHostRunSession& sess) {
    auto& cacheDir = sess.cacheDir;
    auto& archives = sess.archives;
    auto& st = sess.st;
    auto& grandBytes = sess.grandBytes;

    // --- Walk every real archive in cache_dir, exactly like the census ---
    for (auto& ent : fs::directory_iterator(cacheDir)) {
        if (!ent.is_regular_file()) continue;
        if (toLower(ent.path().extension().string()) == ".vpp_pc") archives.push_back(ent.path());
    }
    std::sort(archives.begin(), archives.end());

    for (auto& archivePath : archives) {
        std::string archiveName = archivePath.filename().string();
        std::vector<uint8_t> bytes;
        try {
            bytes = readFile(archivePath);
        } catch (const std::exception& ex) {
            std::cerr << "READ FAILED " << archiveName << ": " << ex.what() << "\n";
            continue;
        }
        grandBytes += bytes.size();
        try {
            vpp::Container root(vpp::ByteView(bytes.data(), bytes.size()));
            walk(st, root, archiveName, "");
        } catch (const std::exception& ex) {
            std::cerr << "PARSE FAILED " << archiveName << ": " << ex.what() << "\n";
        }
        std::cout << "  walked " << archiveName << " (" << bytes.size() << " bytes) - "
                  << st.found.size() << " .lua entries found so far\n";
    }

    std::cout << "Archive walk complete: " << archives.size() << " archives, " << grandBytes
              << " bytes, " << st.stats.entries << " directory entries, " << st.found.size()
              << " real .lua entries found (population denominator for everything below).\n";
}

void setupVintDocs(LuaHostRunSession& sess) {
    auto& vintDocs = sess.vintDocs;
    auto& host = *sess.host_;
    auto& st = sess.st;
    auto& scriptKey = sess.scriptKey;
    auto& parsedDocs = sess.parsedDocs;
    auto& parsedDocSource = sess.parsedDocSource;
    auto& docByScript = sess.docByScript;
    auto& docHandleByParsed = sess.docHandleByParsed;
    auto& docsParseFailed = sess.docsParseFailed;
    auto& docsWithoutScript = sess.docsWithoutScript;
    auto& docScriptDuplicates = sess.docScriptDuplicates;
    auto& docsLoaded = sess.docsLoaded;
    auto& docObjectsLoaded = sess.docObjectsLoaded;
    auto& docPropertiesLoaded = sess.docPropertiesLoaded;
    auto& docContextHits = sess.docContextHits;
    auto& docContextMisses = sess.docContextMisses;

    // --- Real .vint_doc documents -> the UI state's Vint element tree
    // (2026-10-03). Every .vint_doc leaf the walk found is parsed
    // (sr3vintdoc::parseDocument, layout CONFIRMED in spec-vint-doc-format.md
    // at main 67455c3) and indexed by its `lua_script_file` metadata (the
    // value is stored with or without ".lua" in real files; compared
    // case-insensitively without it). Host's document-context resolver then
    // runs each chunk - and each hook function, by the chunk that defined
    // it - in the context of the document naming that chunk as its script,
    // loading the document into EngineState the first time it is needed
    // (CHOSEN association, see Host::setDocumentContextResolver). A chunk no
    // document names keeps the OPEN current document, as before.
    // --no-vint-docs disables all of this (the pre-2026-10-03 behaviour) for
    // A/B measurement.
    scriptKey = [](std::string s) {
        s = toLower(s);
        size_t slash = s.find_last_of("/\\");
        if (slash != std::string::npos) s = s.substr(slash + 1);
        if (s.size() > 4 && s.compare(s.size() - 4, 4, ".lua") == 0) s.resize(s.size() - 4);
        return s;
    };
    if (vintDocs) {
        for (size_t i = 0; i < st.docs.size(); ++i) {
            const auto& d = st.docs[i];
            try {
                parsedDocs.push_back(sr3vintdoc::parseDocument(vpp::ByteView(d.bytes.data(), d.bytes.size())));
                parsedDocSource.push_back(i);
            } catch (const std::exception&) {
                ++docsParseFailed;
                continue;
            }
            const std::string* script = parsedDocs.back().metadataValue("lua_script_file");
            if (!script || script->empty()) { ++docsWithoutScript; continue; }
            if (!docByScript.emplace(scriptKey(*script), parsedDocs.size() - 1).second) ++docScriptDuplicates;
        }
        docHandleByParsed.assign(parsedDocs.size(), 0);
        host.setDocumentContextResolver([&](const std::string& chunkName) -> std::optional<uint32_t> {
            auto it = docByScript.find(scriptKey(chunkName));
            if (it == docByScript.end()) { ++docContextMisses; return std::nullopt; }
            ++docContextHits;
            uint32_t& h = docHandleByParsed[it->second];
            if (h == 0) {
                const auto& src = st.docs[parsedDocSource[it->second]];
                h = host.engineState().loadVintDocument(src.entryName, parsedDocs[it->second]);
                const LoadedVintDocument* ld = host.engineState().loadedVintDocument(h);
                ++docsLoaded;
                docObjectsLoaded += ld ? ld->objectCount : 0;
                docPropertiesLoaded += ld ? ld->propertyCount : 0;
            }
            return h;
        });
    }
    std::cout << "vint_docs: " << st.docs.size() << " .vint_doc entries found, " << parsedDocs.size() << " parsed, "
              << docsParseFailed << " failed to parse, " << docByScript.size() << " distinct lua_script_file names ("
              << docsWithoutScript << " documents name no script, " << docScriptDuplicates
              << " later documents repeat an already-indexed script and are not used)"
              << (vintDocs ? "" : " - DISABLED by --no-vint-docs") << "\n";
}

void resolveStateTags(LuaHostRunSession& sess) {
    auto& outDir = sess.outDir;
    auto& archives = sess.archives;
    auto& st = sess.st;
    auto& scriptTag = sess.scriptTag;
    auto& tagUi = sess.tagUi;
    auto& tagGameplay = sess.tagGameplay;
    auto& tagOpen = sess.tagOpen;
    auto& tagConflict = sess.tagConflict;

    // --- Real per-script state-tag resolution (this task, spec-lua-
    // bindings.md Sec14.5 - see the `statetag` namespace above for the
    // full method/citation). Built once, over the SAME `archives` list
    // already walked for .lua scripts above, then resolved per script so
    // the hook-firing loop below can restrict each script's own hooks to
    // its own real state. -------------------------------------------
    std::cout << "\n=== Real per-script state-tag resolution (spec-lua-bindings.md Sec14.5) ===\n";
    statetag::Index stateIdx = statetag::build(archives);
    std::cout << "Walked " << stateIdx.asmInstancesFound << " real .asm_pc manifests, parsed "
              << stateIdx.manifestsParsed << " OK, " << stateIdx.manifestParseFailures << " parse failure(s).\n";
    std::cout << "Real candidate entries seen: " << stateIdx.uiCandidateEntries
              << " (kind 21/22 + type 27 \"Vint LUA script\"), " << stateIdx.gameplayCandidateEntries
              << " (kind 39/40 + type 32 \"Mission LUA script\"); " << stateIdx.byKey.size()
              << " distinct (container,entry) keys indexed, " << stateIdx.conflicts
              << " real cross-manifest conflict(s) (expected 0 per Sec14.5's own \"cleanly, exhaustively "
              << "separated\" population claim - reported, not hidden, either way).\n";

    scriptTag = std::vector<statetag::Tag>(st.found.size(), statetag::Tag::Open);
    std::vector<std::string> scriptContainerName(st.found.size());
    for (size_t i = 0; i < st.found.size(); ++i) {
        scriptTag[i] = statetag::resolve(stateIdx, st.found[i].pathChain, st.found[i].entryName, &scriptContainerName[i]);
        switch (scriptTag[i]) {
            case statetag::Tag::Ui: ++tagUi; break;
            case statetag::Tag::Gameplay: ++tagGameplay; break;
            case statetag::Tag::Conflict: ++tagConflict; break;
            default: ++tagOpen; break;
        }
    }
    std::cout << "Real population split over " << st.found.size() << " found scripts: ui=" << tagUi
              << " gameplay=" << tagGameplay << " OPEN=" << tagOpen << " conflict=" << tagConflict
              << " (conflict is folded into OPEN's firing behavior below, reported separately here per "
              << "the \"no invented fixes\" policy - a real data disagreement is not the same finding as "
              << "a real absence of data).\n";

    // Real, per-script audit trail - written so this split can be checked
    // row-by-row independently, not just trusted from the aggregate above.
    {
        std::ofstream tagOut(outDir / "verdict_script_state_tags.tsv");
        tagOut << "archive\tpath_chain\tentry_name\tresolved_container_name\tresolved_state_tag\n";
        for (size_t i = 0; i < st.found.size(); ++i) {
            tagOut << sanitizeTsv(st.found[i].archive) << "\t" << sanitizeTsv(st.found[i].pathChain) << "\t"
                   << sanitizeTsv(st.found[i].entryName) << "\t" << sanitizeTsv(scriptContainerName[i]) << "\t"
                   << statetag::tagLabel(scriptTag[i]) << "\n";
        }
    }
    std::cout << "Wrote " << (outDir / "verdict_script_state_tags.tsv").string() << "\n";
}

void runPreloads(LuaHostRunSession& sess) {
    auto& host = *sess.host_;
    auto& archives = sess.archives;
    auto& st = sess.st;
    auto& pre_system_ui = sess.pre_system_ui;
    auto& pre_vint_lib = sess.pre_vint_lib;
    auto& pre_game_ui_globals = sess.pre_game_ui_globals;
    auto& pre_vdo_base = sess.pre_vdo_base;
    auto& pre_vdo_anim = sess.pre_vdo_anim;
    auto& pre_vdo_input = sess.pre_vdo_input;
    auto& pre_cell_foreground = sess.pre_cell_foreground;
    auto& pre_system_gp = sess.pre_system_gp;
    auto& gameLibIndices = sess.gameLibIndices;
    auto& gameLibLoadedOk = sess.gameLibLoadedOk;
    auto& gameLibRanOk = sess.gameLibRanOk;
    auto& gameLibLoadError = sess.gameLibLoadError;
    auto& gameLibRunError = sess.gameLibRunError;

    // --- Real, disassembly-confirmed preload order for both states
    // (spec-lua-bindings.md Sec16.4, folded in 2026-09-30).
    // CONFIRMED and CLEARED for implementation 2026-10-01 (Sec16.1/Sec16.4
    // re-derived from the executable; HANDOFF Requests to Team A item 8
    // answered). Per Sec16.1 a failed preload is silent and later preloads
    // still run, which is what this sequence does. Before this,
    // this host only ever preloaded game_lib.lua, alone, into gameplay -
    // missing system_lib.lua on BOTH states and the entire UI-state chain.
    //   UI state, in order:       system_lib.lua -> vint_lib.lua ->
    //                             game_ui_globals.lua -> vdo_base_object.lua
    //                             -> vdo_anim_object.lua -> vdo_input_tracker.lua
    //   Gameplay state, in order: system_lib.lua -> game_lib.lua (below)
    // Each file is preloaded via the SAME first-found-instance/runChunk
    // pattern game_lib.lua's own block already used - generalized into one
    // small helper here rather than duplicated per file, since it is now
    // used 7 times (was 1). A later/natural instance of any of these names
    // (if one exists elsewhere in the walk) is still tried normally in the
    // main loop below, unchanged - same precedent as game_lib.lua's own
    // "further instances" note.
    // The deferred include queue (spec-lua-bindings.md Sec16.4) loads files by
    // name after a successful load: resolved here against the walked
    // scripts, first found instance, case-insensitively - the same
    // convention as the preloads below (how the engine's file opener treats
    // case is not specced).
    host.setIncludeResolver([&st](const std::string& fileName, std::string& source) {
        std::string want = toLower(fileName);
        for (const auto& f : st.found) {
            if (toLower(f.entryName) == want) {
                source = f.content;
                return true;
            }
        }
        return false;
    });
    auto preloadNamed = [&](const std::string& name, lua_State* L, const std::string& stateLabel) -> PreloadResult {
        PreloadResult pr;
        pr.name = name;
        pr.stateLabel = stateLabel;
        std::string nameLower = toLower(name);
        size_t chosen = SIZE_MAX;
        for (size_t i = 0; i < st.found.size(); ++i) {
            if (toLower(st.found[i].entryName) == nameLower) {
                ++pr.instancesFound;
                if (chosen == SIZE_MAX) chosen = i;
            }
        }
        std::cout << "\n=== preload " << name << " -> " << stateLabel << " state ===\n";
        std::cout << "Found " << pr.instancesFound << " real shipped instance(s) named " << name << ".\n";
        if (chosen == SIZE_MAX) {
            std::cout << "BLOCKING FINDING: no entry named " << name << " was found anywhere in this walk's "
                       << archives.size() << "-archive, " << st.found.size() << "-script population - the real, "
                       << "disassembly-confirmed preload (spec-lua-bindings.md Sec16.4) for the " << stateLabel
                       << " state was skipped for this file.\n";
            return pr;
        }
        const auto& f = st.found[chosen];
        std::cout << "Loading the first found instance for real: archive=" << f.archive << " pathChain=["
                   << f.pathChain << "] (" << f.content.size() << " bytes)\n";
        auto r = host.runChunk(L, f.content, f.entryName);
        pr.loadOk = r.loadOk;
        pr.loadError = r.loadError;
        pr.pcallOk = r.pcallOk;
        pr.pcallError = r.pcallError;
        std::cout << "  luaL_loadbuffer: " << (r.loadOk ? "OK" : ("FAILED: " + r.loadError)) << "\n";
        if (r.loadOk) {
            std::cout << "  lua_pcall: " << (r.pcallOk ? "OK (ran to completion)" : ("FAILED: " + r.pcallError)) << "\n";
        }
        if (pr.instancesFound > 1) {
            std::cout << "  (" << (pr.instancesFound - 1) << " further instance(s) exist in other archives/DLCs - "
                       << "only the first was given this dedicated preload treatment; the rest are tried like any "
                       << "other script in the main loop below, in their own natural walk order.)\n";
        }
        return pr;
    };

    pre_system_ui = preloadNamed("system_lib.lua", host.uiState(), "ui");
    pre_vint_lib = preloadNamed("vint_lib.lua", host.uiState(), "ui");
    pre_game_ui_globals = preloadNamed("game_ui_globals.lua", host.uiState(), "ui");
    pre_vdo_base = preloadNamed("vdo_base_object.lua", host.uiState(), "ui");
    pre_vdo_anim = preloadNamed("vdo_anim_object.lua", host.uiState(), "ui");
    pre_vdo_input = preloadNamed("vdo_input_tracker.lua", host.uiState(), "ui");
    // cell_foreground.lua: NOT part of the real, disassembly-confirmed
    // fixed UI-state preload chain above (spec-lua-bindings.md Sec16.4) -
    // this is this project's OWN stated engineering choice, not a measured
    // fact. Real finding (spec-lua-bindings.md Sec17, 2026-09-30): cell_
    // menu_main.lua's own real parent/container document, cell_foreground
    // .lua, defines an 11-name plain Lua enum (ID_MISSIONS et al, NAME =
    // value, no local) that cell_menu_main.lua depends on at its own very
    // first executable line - not a missing native registration (Sec17's
    // own exhaustive negative: zero native mechanism anywhere in this
    // binary pushes a bare number/boolean global by name at all). No real
    // sibling-document load-order data was found in the asm container this
    // pass, so this is a labelled choice (load cell_foreground.lua first),
    // not a recovered fact - loaded into the UI state only (cell_menu_main
    // .lua's own real home is almost certainly the UI/interface state, a
    // cell-phone menu system, matching the rest of this preload chain), NOT
    // gameplay - so a gameplay-state run of cell_menu_main.lua is expected
    // to keep failing on ID_MISSIONS, a real wrong-state-test artifact per
    // this project's own established Sec9.114 framework, not a new bug.
    pre_cell_foreground = preloadNamed("cell_foreground.lua", host.uiState(), "ui");
    pre_system_gp = preloadNamed("system_lib.lua", host.gameplayState(), "gameplay");

    // --- Locate and load game_lib.lua, into the persistent gameplay
    // state, AFTER system_lib.lua above (real order, spec-lua-bindings.md
    // Sec16.4) and before any mission script - matching the real engine's
    // own documented sequence (Sec13.2 Item 2 / Sec16.2: the "game play"
    // state registers its 1,014-name roster, loads system_lib.lua, THEN
    // loads "game_lib.lua", before any mission script runs against it). ---
    for (size_t i = 0; i < st.found.size(); ++i) {
        if (toLower(st.found[i].entryName) == "game_lib.lua") gameLibIndices.push_back(i);
    }
    std::cout << "\n=== game_lib.lua ===\n";
    std::cout << "Found " << gameLibIndices.size() << " real shipped instance(s) named game_lib.lua.\n";

    size_t gameLibChosenIndex = SIZE_MAX;
    if (!gameLibIndices.empty()) {
        gameLibChosenIndex = gameLibIndices.front();
        const auto& gl = st.found[gameLibChosenIndex];
        std::cout << "Loading the first found instance for real: archive=" << gl.archive
                  << " pathChain=[" << gl.pathChain << "] (" << gl.content.size() << " bytes)\n";
        auto r = host.runChunk(host.gameplayState(), gl.content, gl.entryName);
        gameLibLoadedOk = r.loadOk;
        gameLibLoadError = r.loadError;
        gameLibRanOk = r.pcallOk;
        gameLibRunError = r.pcallError;
        std::cout << "  luaL_loadbuffer: " << (r.loadOk ? "OK" : ("FAILED: " + r.loadError)) << "\n";
        if (r.loadOk) {
            std::cout << "  lua_pcall: " << (r.pcallOk ? "OK (ran to completion)" : ("FAILED: " + r.pcallError)) << "\n";
        }
        if (gameLibIndices.size() > 1) {
            std::cout << "  (" << (gameLibIndices.size() - 1) << " further game_lib.lua instance(s) exist in other "
                      << "archives/DLCs - only the first was given this dedicated first-load treatment; the rest "
                      << "are tried like any other script in the main loop below, in their own natural walk order.)\n";
        }
    } else {
        std::cout << "BLOCKING FINDING: no entry named game_lib.lua was found anywhere in this walk's "
                  << archives.size() << "-archive, " << st.found.size() << "-script population. The gameplay "
                  << "state's stub roster is registered, but the library the real engine loads into that same "
                  << "state before running any mission script (spec-lua-bindings.md Sec13.2) was never located, "
                  << "so nothing further in this run should be read as \"game_lib.lua's own helpers were present.\"\n";
    }
}

} // namespace luahostrun
