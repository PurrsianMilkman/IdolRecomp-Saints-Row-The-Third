#include "prototype_lit_clmesh_tower2_session.h"

using namespace tower2_proto;

bool tower2_proto::tower2SetupDiagnostics(Tower2Session& session) {
    auto& kRenderDocCapture = session.kRenderDocCapture;
    auto& kRenderDocAbsurdValues = session.kRenderDocAbsurdValues;
    auto& kTestObjInstanceParams = session.kTestObjInstanceParams;
    // NEW DIAGNOSTIC SWITCHES (orchestrator task, 2026-10-08, SAME diag/
    // tower-constant-consumption-control branch, requested as "one more
    // piece of independent evidence" after dumpCbufferRegisterRaw's GPU-
    // readback control above): OPT-IN ONLY, both unset by default (zero
    // behaviour/output change - loadRenderDocApi() above is never even
    // called unless SR3_RENDERDOC_CAPTURE=1).
    //
    // SR3_RENDERDOC_CAPTURE=1: loads RenderDoc's in-process API (if
    // installed on this machine - see loadRenderDocApi()'s own comment)
    // and wraps PASS A/B/C's real draw calls below in a single
    // StartFrameCapture/EndFrameCapture, saving a real .rdc file a human
    // can later open in qrenderdoc.exe to inspect the GPU's own pixel
    // history / shader debug trace - ground truth on top of this file's
    // own code-reading inference, not a replacement for the GPU-readback
    // control already on record in this branch's own commit.
    //
    // SR3_RENDERDOC_ABSURD_VALUES=1: independent of the capture switch
    // above (meaningful combined with it, for the "control" capture), re-
    // uses this EXACT branch's own already-tested CONTROL 1 first extreme
    // substitution - fillPsCbufferTemplate()'s own "target_dimensions"/
    // "ir_pixel_steps" branches below synthesize (1e6,1e6,0,0)/(1e-6,1e-6,
    // 0,0) instead of the real default (800,600,0,0)/(1/800,1/600,0,0) -
    // "per the task's own example" exactly as this branch's prior commit
    // already described and tested (byte-identical result, MD5
    // df05d007b471049ab7812944765e9a5c - see that commit, 108d7fc, for the
    // full reading). Lets the SAME binary produce both the "normal run" and
    // "absurd test values" RenderDoc capture the orchestrator asked for,
    // without a second temporary source edit.
    if (kRenderDocCapture) {
        if (loadRenderDocApi() && g_rdocApi) {
            // Distinct template per value-set so the two runs this task
            // asks for ("a normal run and a run with the absurd test
            // values") never collide on the same filename. Relative to
            // this process's own CWD (this tool otherwise never depends on
            // CWD - every other path in this file is a hardcoded absolute
            // path - so whoever sets SR3_RENDERDOC_CAPTURE=1 controls
            // where renderdoc_captures/ lands by choosing CWD; this
            // session runs it from this worktree's own root, per the
            // orchestrator's "under this worktree" instruction).
            std::string tmpl = kRenderDocAbsurdValues
                ? "renderdoc_captures/tower_absurd_target_dims_pixel_steps"
                : "renderdoc_captures/tower_default";
            g_rdocApi->SetCaptureFilePathTemplate(tmpl.c_str());
            printf("[renderdoc] capture file path template: \"%s\" (RenderDoc appends _frame<N>.rdc)\n", tmpl.c_str());
        }
    }

    // HYPOTHESIS TEST SWITCH (2026-10-08, orchestrator task, relayed from Team
    // A via spec-render-pipeline.md Sec20.12.14 point 5): OPT-IN ONLY, via
    // SR3_TEST_OBJ_INSTANCE_PARAMS=1. Unset (the default, and the only mode
    // this scene's own frozen golden is checked against unless/until a future
    // session explicitly re-freezes it) leaves EVERY byte of this tool's
    // output unchanged from before this test switch existed - see
    // fillVsCbuffer()'s own "object_instance_params" branch below for exactly
    // what this does and does not change, and this file's own top comment /
    // this session's own report for the full reasoning and result. This is a
    // TEST of an unconfirmed HYPOTHESIS (Team A's own spec text: "HYPOTHESIS
    // - plausible mechanism, not shader-bytecode-traced"), not an
    // implementation of a confirmed mechanism - do not mistake a test run's
    // output for a new default.

    // PROMOTED TO REAL DEFAULT BEHAVIOUR (2026-10-08, orchestrator task,
    // spec-render-pipeline.md Sec20.12.15). This was previously an opt-in
    // hypothesis-test switch (SR3_TEST_TARGET_DIMENSIONS=1, commit
    // 57fc8ef) testing whether `rcp(Target_dimensions=0)=INF` feeding a NaN
    // into the IR_GBuffer_DSF_DataSampler UV explains this scene's flat/
    // hard-edged/saturated-per-face symptom (all 5 distinct real role6 PS
    // shaders this tower resolves to open with `r0.x = rcp(c[47].x);
    // r0.z = rcp(c[47].y)` then immediately use c48/IR_Pixel_Steps as a
    // UV-space multiplier). That test alone was verified NEGATIVE (output
    // byte-identical to the broken default) because PASS C never bound
    // IR_GBuffer_DSF_DataSampler to anything but a placeholder - see the
    // dsfDataSamplerReg binding added to PASS C below for the other half of
    // today's fix.
    //
    // Team A's direct disassembly (Sec20.12.15) now makes this the REAL
    // recommended fix, not just a hypothesis: an exhaustive whole-exe string
    // scan found neither name exists anywhere as a literal, and all three
    // most-likely literal/fixed-register producer functions were fully
    // decompiled and contain zero SetPixelShaderConstantF/
    // SetVertexShaderConstantF calls - there is no engine-side producer for
    // either constant anywhere in the executable to reproduce. Team A's own
    // stated practical recommendation: "synthesize (width,height,0,0)/
    // (1/width,1/height,0,0) directly at PS c47/c48 rather than chasing a
    // producer that doesn't exist." This is therefore now the unconditional
    // default in fillPsCbufferTemplate()'s own "target_dimensions"/
    // "ir_pixel_steps" branches below, not gated behind any env var -
    // CHOSEN values (800,600,0,0)/(1/800,1/600,0,0), this tool's own actual
    // render resolution, UNCHANGED from the original hypothesis test.

    printf("=== Lit-clmesh prototype: real G-buffer prepass -> real directional light -> real material pass ===\n");
    printf("(forked from prototype_lit_car.cpp - see this file's own top comment)\n\n");
    if (kTestObjInstanceParams) {
        printf("[HYPOTHESIS TEST] SR3_TEST_OBJ_INSTANCE_PARAMS=1: Object_instance_params/Object_instance_params_2\n");
        printf("  (VS c35/c36) will be filled with a CHOSEN (1,1,1,1) identity value instead of the real-\n");
        printf("  confirmed-zero placeholder, testing spec-render-pipeline.md Sec20.12.14 point 5's stale-D3D9-\n");
        printf("  constant-carryover HYPOTHESIS for this scene's diffuse-vanishes/specular-only symptom. NOT a\n");
        printf("  confirmed value, NOT the default - see fillVsCbuffer()'s own comment and this session's report.\n\n");
    }
    printf("[DEFAULT, 2026-10-08] Target_dimensions/IR_Pixel_Steps (PS c47/c48) are synthesized as CHOSEN\n");
    printf("  (800,600,0,0)/(1/800,1/600,0,0) screen-metadata values (this tool's own actual render resolution),\n");
    printf("  NOT the real-confirmed-zero placeholder - spec-render-pipeline.md Sec20.12.15's own recommendation,\n");
    printf("  since no engine-side producer for either constant exists anywhere in the executable (exhaustive\n");
    printf("  negative). PASS C also now binds IR_GBuffer_DSF_DataSampler@s9 to PASS A's real oC1 output\n");
    printf("  (Sec23.8/Sec23.13) instead of a placeholder - see fillPsCbufferTemplate()'s own comment and this\n");
    printf("  session's report for both halves of this fix.\n\n");
    return true;
}

bool tower2_proto::tower2SetupArchives(Tower2Session& session) {
    auto& kCacheDir = session.kCacheDir;
    auto& shadersArchive = session.shadersArchive;
    auto& stemToFiles = session.stemToFiles;
    auto& stemCrcToStem = session.stemCrcToStem;
    auto& cityArchive = session.cityArchive;
    // -----------------------------------------------------------------
    // 1. Real stem -> .fxo_pc table (identical mechanism to
    //    prototype_lit_car.cpp's own step 1).
    // -----------------------------------------------------------------
    shadersArchive = readFile(kCacheDir + "/shaders.vpp_pc");
    if (shadersArchive.empty()) { printf("FATAL: could not read shaders.vpp_pc\n"); return false; }
    session.shadersContainer_.emplace(vpp::ByteView(shadersArchive.data(), shadersArchive.size())); auto& shadersContainer = *session.shadersContainer_;
    collectFxoStems(shadersContainer, stemToFiles);
    for (const auto& kv : stemToFiles) {
        uint32_t crc = sr3fxo::crc32Raw(reinterpret_cast<const uint8_t*>(kv.first.data()), kv.first.size());
        stemCrcToStem[crc] = kv.first;
    }
    printf("[shaderHash join] real .fxo_pc distinct stems: %zu\n\n", stemToFiles.size());

    // -----------------------------------------------------------------
    // 1b. Open sr3_city_0.vpp_pc once and locate 1018h0.str2_pc (the same
    //     real "Zone (High LOD)" fine-cell container the existing
    //     clmesh_airport_controltower/clmesh_1018h0_props golden scenes
    //     already use) - reused for BOTH the census below and the tower's
    //     own geometry/material load, so this 1.5 GB archive is read and
    //     decompressed only once per run.
    // -----------------------------------------------------------------
    cityArchive = readFile(kCacheDir + "/sr3_city_0.vpp_pc");
    if (cityArchive.empty()) { printf("FATAL: could not read sr3_city_0.vpp_pc\n"); return false; }
    session.cityContainer_.emplace(vpp::ByteView(cityArchive.data(), cityArchive.size())); auto& cityContainer = *session.cityContainer_;
    size_t h0Index = SIZE_MAX;
    for (size_t i = 0; i < cityContainer.entries().size(); ++i) {
        if (lower(cityContainer.entries()[i].name) == "1018h0.str2_pc") { h0Index = i; break; }
    }
    if (h0Index == SIZE_MAX) { printf("FATAL: 1018h0.str2_pc not found at top level of sr3_city_0.vpp_pc\n"); return false; }
    session.h0Container_.emplace(cityContainer.openNested(h0Index)); auto& h0Container = *session.h0Container_;
    printf("[archive] sr3_city_0.vpp_pc :: 1018h0.str2_pc opened, %zu entries\n\n", h0Container.entries().size());
    return true;
}

bool tower2_proto::tower2SetupCensus(Tower2Session& session) {
    auto& stemCrcToStem = session.stemCrcToStem;
    auto& h0Container = *session.h0Container_;
    // -----------------------------------------------------------------
    // 1c. REAL shaderHash (MaterialRecord::hash0) -> real .fxo_pc-stem
    //     resolution census, across EVERY material of EVERY real
    //     .clmesh_pc reachable from 1018h0.str2_pc (this task's own item
    //     2 - "resolve hash0 for every material across all these props").
    //     Printed in full on every run (real tool output, not a separate
    //     report) - iteration order is 1018h0.str2_pc's own on-disk entry
    //     order and each file's own materialRecords() vector order, both
    //     deterministic, so this stdout is byte-exact reproducible.
    // -----------------------------------------------------------------
    size_t censusTotalMaterials = 0, censusResolvedMaterials = 0;
    std::vector<std::string> censusMissList; // explicit, per this task's own instruction
    printf("=== [census] real hash0 -> real .fxo_pc-stem resolution across every .clmesh_pc in 1018h0.str2_pc ===\n");
    for (size_t i = 0; i < h0Container.entries().size(); ++i) {
        const std::string& name = h0Container.entries()[i].name;
        if (!endsWithStr(lower(name), ".clmesh_pc")) continue;
        std::vector<uint8_t> clBytes;
        if (!entryBytes(h0Container, i, clBytes) || clBytes.empty()) {
            printf("  [%s] could not read real bytes - skipped\n", name.c_str());
            continue;
        }
        vpp::ByteView clView(clBytes.data(), clBytes.size());
        sr3clmesh::LevelMesh lm;
        try { lm = sr3clmesh::LevelMesh::parse(clView); }
        catch (const std::exception& ex) {
            printf("  [%s] LevelMesh parse FAILED: %s - skipped\n", name.c_str(), ex.what());
            continue;
        }
        if (!lm.walkComplete()) { printf("  [%s] walk incomplete - skipped\n", name.c_str()); continue; }
        std::vector<sr3clmesh::MaterialRecord> recs = lm.materialRecords(clView);
        size_t fileResolved = 0;
        for (size_t mi = 0; mi < recs.size(); ++mi) {
            ++censusTotalMaterials;
            auto it = stemCrcToStem.find(recs[mi].hash0);
            if (it != stemCrcToStem.end()) {
                ++censusResolvedMaterials;
                ++fileResolved;
            } else {
                char buf[16]; snprintf(buf, sizeof(buf), "0x%08X", recs[mi].hash0);
                censusMissList.push_back(name + "[" + std::to_string(mi) + "] hash0=" + buf);
            }
        }
        printf("  %-32s materials=%zu resolved=%zu\n", name.c_str(), recs.size(), fileResolved);
    }
    printf("[census] TOTAL: %zu/%zu real materials resolved hash0 -> a real .fxo_pc stem (%.2f%%)\n",
           censusResolvedMaterials, censusTotalMaterials,
           censusTotalMaterials ? 100.0 * censusResolvedMaterials / censusTotalMaterials : 0.0);
    printf("[census] MISS LIST (%zu): explicit, not silently placeholdered:\n", censusMissList.size());
    for (const auto& m : censusMissList) printf("    %s\n", m.c_str());
    printf("\n");
    return true;
}

bool tower2_proto::tower2SetupCensus2(Tower2Session& session) {
    auto& shadersContainer = *session.shadersContainer_;
    auto& stemToFiles = session.stemToFiles;
    auto& stemCrcToStem = session.stemCrcToStem;
    auto& h0Container = *session.h0Container_;
    // -----------------------------------------------------------------
    // 1c-2. NEW IN THIS FORK (Lead 1 - see top comment): the SAME census,
    //    re-run for STRUCTURAL drawability under the generalized
    //    candidateTryOrder()/structuralPairOk() rule (every real candidate
    //    file for a resolved stem, not just a literal `_v`/`_mv` name) -
    //    this is the "re-run the census (...all 81 across
    //    1018h0.str2_pc) and report the new drawable count" this task
    //    asked for. Cheap (header-parse only, no compile) so it is safe to
    //    run for the WHOLE population, not just the tower - the tower's
    //    own AUTHORITATIVE count (which also requires a full D3DCompile)
    //    is reported separately below, after step 5/6's real compile.
    // -----------------------------------------------------------------
    size_t census2TotalMaterials = 0, census2StructurallyDrawable = 0, census2OldRuleDrawable = 0;
    printf("=== [census 2, NEW] structural role4+role6 drawability (generalized VS/PS resolution, Lead 1) ===\n");
    for (size_t i = 0; i < h0Container.entries().size(); ++i) {
        const std::string& name = h0Container.entries()[i].name;
        if (!endsWithStr(lower(name), ".clmesh_pc")) continue;
        std::vector<uint8_t> clBytes2;
        if (!entryBytes(h0Container, i, clBytes2) || clBytes2.empty()) continue;
        vpp::ByteView clView2(clBytes2.data(), clBytes2.size());
        sr3clmesh::LevelMesh lm2;
        try { lm2 = sr3clmesh::LevelMesh::parse(clView2); } catch (const std::exception&) { continue; }
        if (!lm2.walkComplete()) continue;
        std::vector<sr3clmesh::MaterialRecord> recs2 = lm2.materialRecords(clView2);
        size_t fileDrawable = 0, fileOldRule = 0;
        for (size_t mi = 0; mi < recs2.size(); ++mi) {
            ++census2TotalMaterials;
            auto it = stemCrcToStem.find(recs2[mi].hash0);
            if (it == stemCrcToStem.end()) continue; // hash0 miss - already reported above
            if (structuralPairOk(shadersContainer, stemToFiles[it->second])) { ++census2StructurallyDrawable; ++fileDrawable; }
            std::string oldReason;
            if (!pickVertexFile(stemToFiles[it->second], oldReason).empty()) { ++census2OldRuleDrawable; ++fileOldRule; }
        }
        printf("  %-32s materials=%zu structurallyDrawable=%zu (was %zu under the old _v/_mv-only rule)\n", name.c_str(),
               recs2.size(), fileDrawable, fileOldRule);
    }
    printf("[census 2] TOTAL: %zu/%zu real materials structurally pair role4+role6 through SOME real candidate "
           "file (%.2f%%) - was %zu/%zu (%.2f%%) under the parent fork's own literal _v/_mv-only rule, same "
           "population, computed in this same loop for a like-for-like comparison\n\n",
           census2StructurallyDrawable, census2TotalMaterials,
           census2TotalMaterials ? 100.0 * census2StructurallyDrawable / census2TotalMaterials : 0.0,
           census2OldRuleDrawable, census2TotalMaterials,
           census2TotalMaterials ? 100.0 * census2OldRuleDrawable / census2TotalMaterials : 0.0);
    return true;
}

bool tower2_proto::tower2SetupEnvironment(Tower2Session& session) {
    auto& kCacheDir = session.kCacheDir;
    auto& realLightRowFound = session.realLightRowFound;
    auto& realTodColor = session.realTodColor;
    auto& realTodIntensity = session.realTodIntensity;
    auto& realAmbientColor = session.realAmbientColor;
    auto& realAmbientIntensity = session.realAmbientIntensity;
    auto& realBackAmbientColor = session.realBackAmbientColor;
    auto& realBackAmbientIntensity = session.realBackAmbientIntensity;
    auto& realExposureMin = session.realExposureMin;
    // -----------------------------------------------------------------
    // 2. REAL environment/weather lighting row - identical mechanism to
    //    prototype_lit_car.cpp's own step 1b (sr3tables_environment reader,
    //    unchanged), pinned the SAME way: Weather_Time_Segment Name="noon"
    //    (spec-tables-environment.md Sec17.1: the real shipped authored
    //    order is sunrise(800)/noon(1430)/sunset(1900)/night(500), i.e.
    //    noon is index 1) x Weather_Stages/Stage Stage_Name="Overcast" (the
    //    only stage cell in "noon" with TOD_Light_Color/Ambient_Color/
    //    Back_Ambient_Color/Exposure all real-present - see the parent
    //    prototype's own comment for the full reasoning, unchanged here).
    //    This lookup is BY NAME ("noon"), not by array index, so it is
    //    already pinned to the noon segment by construction regardless of
    //    the table's on-disk row order - confirmed by the printed row
    //    dump below, which reports whether the real branch actually hit.
    // -----------------------------------------------------------------
    {
        std::vector<uint8_t> miscTables = readFile(kCacheDir + "/misc_tables.vpp_pc");
        std::vector<uint8_t> weatherBytes, todBytes;
        if (!miscTables.empty()) {
            vpp::Container miscContainer{vpp::ByteView(miscTables.data(), miscTables.size())};
            findEntry(miscContainer, "weather.xtbl", weatherBytes);
            findEntry(miscContainer, "weather_time_of_day.xtbl", todBytes);
        }
        if (!todBytes.empty()) {
            sr3xtbl::Document todDoc = sr3xtbl::ParseDocument(todBytes.data(), todBytes.size());
            std::vector<sr3tables_environment::WeatherTimeSegment> segs =
                sr3tables_environment::ParseWeatherTimeOfDayTable(todDoc);
            for (const auto& seg : segs) {
                if (!seg.name || !sr3xtbl::NameEquals(*seg.name, "noon")) continue;
                for (const auto& cell : seg.stages) {
                    if (!cell.stageName || !sr3xtbl::NameEquals(*cell.stageName, "Overcast")) continue;
                    const auto& L = cell.lighting;
                    if (!L.todLightColor.present || !L.ambientColor.present || !L.backAmbientColor.present) break;
                    realTodColor[0] = L.todLightColor.rgb.r.value; realTodColor[1] = L.todLightColor.rgb.g.value;
                    realTodColor[2] = L.todLightColor.rgb.b.value; realTodIntensity = L.todLightColor.intensity;
                    realAmbientColor[0] = L.ambientColor.rgb.r.value; realAmbientColor[1] = L.ambientColor.rgb.g.value;
                    realAmbientColor[2] = L.ambientColor.rgb.b.value; realAmbientIntensity = L.ambientColor.intensity;
                    realBackAmbientColor[0] = L.backAmbientColor.rgb.r.value;
                    realBackAmbientColor[1] = L.backAmbientColor.rgb.g.value;
                    realBackAmbientColor[2] = L.backAmbientColor.rgb.b.value;
                    realBackAmbientIntensity = L.backAmbientColor.intensity;
                    realExposureMin = cell.exposure.exposureMin.value_or(0.0f);
                    realLightRowFound = true;
                    break;
                }
                break;
            }
        }
    }
    if (realLightRowFound) {
        printf("[env table] REAL row wired: weather_time_of_day.xtbl Weather_Time_Segment Name=noon "
               "(Start_Time=1430 HHMM) x Weather_Stages/Stage Stage_Name=Overcast -\n"
               "  TOD_Light_Color=(%.4f,%.4f,%.4f) Intensity=%.4f, Ambient_Color=(%.4f,%.4f,%.4f) Intensity=%.4f,\n"
               "  Back_Ambient_Color=(%.4f,%.4f,%.4f) Intensity=%.4f, Exposure_Min=%.4f "
               "(all real, read via sr3tables_environment from misc_tables.vpp_pc)\n",
               realTodColor[0], realTodColor[1], realTodColor[2], realTodIntensity, realAmbientColor[0],
               realAmbientColor[1], realAmbientColor[2], realAmbientIntensity, realBackAmbientColor[0],
               realBackAmbientColor[1], realBackAmbientColor[2], realBackAmbientIntensity, realExposureMin);
    } else {
        printf("[env table] WARNING: real weather_time_of_day.xtbl noon/Overcast row NOT found this run "
               "(archive/entry missing or reader failed) - falling back to a CHOSEN placeholder light, labelled "
               "as such below. This should not happen against the real misc_tables.vpp_pc archive.\n");
    }
    printf("\n");
    return true;
}

bool tower2_proto::tower2SetupClmesh(Tower2Session& session) {
    auto& shadersContainer = *session.shadersContainer_;
    auto& h0Container = *session.h0Container_;
    auto& clBytes = session.clBytes;
    auto& glBytes = session.glBytes;
    auto& clView = session.clView;
    auto& glView = session.glView;
    auto& lm = session.lm;
    auto& matRecs = session.matRecs;
    auto& matBindings = session.matBindings;
    auto& stemCrcToStem = session.stemCrcToStem;
    auto& stemToFiles = session.stemToFiles;
    // -----------------------------------------------------------------
    // 3. Real airport_controltower.clmesh_pc/.glmesh_pc load, via
    //    sr3clmesh::LevelMesh (this task's own required reader - the
    //    ALREADY-PROVEN mechanism HANDOFF Sec9.133 established, unchanged
    //    here), from the SAME h0Container opened in step 1b above.
    // -----------------------------------------------------------------
    {
        size_t clIdx = SIZE_MAX, glIdx = SIZE_MAX;
        for (size_t i = 0; i < h0Container.entries().size(); ++i) {
            const std::string& n = h0Container.entries()[i].name;
            if (lower(n) == "airport_controltower.clmesh_pc") clIdx = i;
            if (lower(n) == "airport_controltower.glmesh_pc") glIdx = i;
        }
        if (clIdx == SIZE_MAX || glIdx == SIZE_MAX) {
            printf("FATAL: airport_controltower.clmesh_pc/.glmesh_pc not found in 1018h0.str2_pc\n");
            return false;
        }
        if (!entryBytes(h0Container, clIdx, clBytes) || !entryBytes(h0Container, glIdx, glBytes)) {
            printf("FATAL: could not read airport_controltower.clmesh_pc/.glmesh_pc bytes\n");
            return false;
        }
    }
    printf("[clmesh] airport_controltower.clmesh_pc (%zu bytes) + .glmesh_pc (%zu bytes)\n", clBytes.size(),
           glBytes.size());
    clView = vpp::ByteView(clBytes.data(), clBytes.size());
    glView = vpp::ByteView(glBytes.data(), glBytes.size());
    try { lm = sr3clmesh::LevelMesh::parse(clView); }
    catch (const std::exception& ex) { printf("FATAL: LevelMesh parse failed: %s\n", ex.what()); return false; }
    if (!lm.walkComplete()) { printf("FATAL: LevelMesh's own computed walk did not complete\n"); return false; }
    const auto& mid = lm.middle();
    printf("[clmesh] materialCount=%u renderGroupCount=%zu landsOnEof=%d\n", mid.materialCount, mid.renderGroupCount,
           lm.landsOnEof() ? 1 : 0);

    // Real per-material shaderHash + texture-binding dump (same shape
    // runClmesh() already prints, for report parity) - and, per this
    // material, whether it resolves to a DRAWABLE real stem (has a real
    // per-stem _v/_mv vertex file) vs a resolved-but-not-drawable one
    // (real stem exists, no real per-stem VS blob reachable) vs an actual
    // hash0 miss (none, this prop - see the census above).
    matRecs = lm.materialRecords(clView);
    matBindings.resize(matRecs.size());
    printf("[materials] %zu material record(s):\n", matRecs.size());
    for (size_t mi = 0; mi < matRecs.size(); ++mi) {
        matBindings[mi] = lm.materialTextureBinding(mi, clView);
        auto it = stemCrcToStem.find(matRecs[mi].hash0);
        std::string stem = it != stemCrcToStem.end() ? it->second : "";
        // NEW IN THIS FORK (Lead 1 - see top comment): structural preview
        // across EVERY real candidate file for this stem (not just a
        // literal _v/_mv name) - cheap, header-parse only, no compile.
        // The AUTHORITATIVE decision is step 5/6's own real compile-based
        // resolution below; this is a preview, labelled as such.
        bool drawableStem = !stem.empty() && structuralPairOk(shadersContainer, stemToFiles[stem]);
        printf("  material[%zu]: hash0=0x%08X stem=%s structurallyDrawable(preview)=%s texCount=%u"
               " constNames(B)=%u vec4Consts(C)=%u ->",
               mi, matRecs[mi].hash0, stem.empty() ? "(hash0 miss)" : stem.c_str(),
               stem.empty() ? "n/a(hash0 miss)" : (drawableStem ? "yes" : "no(resolved,no candidate real-pairs role4+role6)"),
               matRecs[mi].textureBindingCount, matRecs[mi].constantNameCount, matRecs[mi].vec4ConstantCount);
        for (const auto& t : matBindings[mi].textures)
            printf(" [slot=%u hash=0x%08X '%s']", t.slot, t.paramHash, t.name.c_str());
        if (matBindings[mi].textures.empty()) printf(" (no real texture binding on this material)");
        printf("\n");
        // NEW IN THIS FORK (Lead 2 - see top comment): real B/C shader-
        // constant array dump, printed in full every run (real tool
        // output, not a separate document) - the hash is shown raw here;
        // whether it matches a real CTAB name on THIS material's own
        // resolved shader is resolved later (step 10/10b below, once the
        // shader is actually compiled) and logged there.
        sr3clmesh::LevelMesh::ShaderConstants msc = lm.materialShaderConstants(mi, clView);
        for (const auto& nc : msc.named) {
            printf("      B/C: nameHash=0x%08X value=(%.6f, %.6f, %.6f, %.6f)\n", nc.nameHash, nc.value[0], nc.value[1],
                   nc.value[2], nc.value[3]);
        }
        for (size_t ti = 0; ti < msc.trailingUnnamed.size(); ++ti) {
            const auto& v = msc.trailingUnnamed[ti];
            printf("      C[trailing,unnamed,OPEN] value=(%.6f, %.6f, %.6f, %.6f)\n", v[0], v[1], v[2], v[3]);
        }
    }
    printf("\n");
    return true;
}

