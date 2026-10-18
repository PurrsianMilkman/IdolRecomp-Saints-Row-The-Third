#include "prototype_lit_clmesh_tower2_session.h"

using namespace tower2_proto;

bool tower2_proto::tower2ResolveRenderGroup(Tower2Session& session) {
    auto& clView = session.clView;
    auto& glView = session.glView;
    auto& lm = session.lm;
    auto& chosenMesh = session.chosenMesh;
    const auto& mid = lm.middle();
    // -----------------------------------------------------------------
    // 4. Real render-group-0 Mesh sub-block resolution - identical
    //    mechanism to runClmesh() (tools/sr3_viewer.cpp), reused: chain
    //    the head blocks' own g-segment cursor first, then walk each
    //    render group with the SAME measured headerDisplacement formula
    //    (spec-physics-format.md Sec4.4.6 / HANDOFF Sec9.55.2), keeping
    //    render group 0 - the higher-detail one on every real sample
    //    checked, same convention the existing clmesh golden scenes use.
    // -----------------------------------------------------------------
    std::vector<sr3mesh::MeshBlock> headBlocks = lm.resolveReferencedMeshes(clView, glView);
    // gCursorAfterHeadChain() rounds the HEAD-chain sum up to 16 at this
    // HEAD -> MIDDLE handoff (level_mesh.h note 9) - a bare sum of
    // headBlocks' gLength() is wrong whenever that sum is not already
    // 16-aligned.
    size_t gCursor = sr3clmesh::LevelMesh::gCursorAfterHeadChain(headBlocks);

    bool haveChosenMesh = false;
    const int renderGroupArg = 0;
    for (size_t g = 0; g < mid.renderGroupCount; ++g) {
        const auto& rg = mid.renderGroups[g];
        const size_t headerDisplacement = ((rg.mesh.offset + 16 + 7) / 8 * 8) - rg.mesh.offset;
        try {
            sr3mesh::MeshBlock rgMesh =
                sr3mesh::MeshBlock::parse(clView, rg.mesh.offset, glView, gCursor, headerDisplacement);
            if (static_cast<int>(g) == renderGroupArg) { chosenMesh = rgMesh; haveChosenMesh = true; }
            if (rgMesh.bulkInGFile()) gCursor += rgMesh.gLength();
        } catch (const std::exception& ex) {
            printf("renderGroup[%zu]: Mesh sub-block parse failed: %s\n", g, ex.what());
        }
    }
    if (!haveChosenMesh) { printf("FATAL: renderGroup %d could not be parsed\n", renderGroupArg); return false; }
    if (!chosenMesh.drawGroupsLocated() || chosenMesh.drawGroups().empty()) {
        printf("FATAL: renderGroup %d has no located draw groups\n", renderGroupArg);
        return false;
    }
    const auto& drawGroup0 = chosenMesh.drawGroups()[0];
    printf("[renderGroup %d] channels=%zu indexCount=%u ranges=%zu\n\n", renderGroupArg, chosenMesh.channels().size(),
           chosenMesh.indexCount(), drawGroup0.size());
    return true;
}

bool tower2_proto::tower2ResolveShaders(Tower2Session& session) {
    auto& kW = session.kW;
    auto& kH = session.kH;
    auto& shadersContainer = *session.shadersContainer_;
    auto& stemToFiles = session.stemToFiles;
    auto& stemCrcToStem = session.stemCrcToStem;
    auto& matRecs = session.matRecs;
    auto& dev = session.dev;
    auto& chosenMesh = session.chosenMesh;
    auto& resolutions = session.resolutions;
    auto& gbufferShaders = session.gbufferShaders;
    auto& litShaders = session.litShaders;
    const auto& drawGroup0 = chosenMesh.drawGroups()[0];
    // -----------------------------------------------------------------
    // 5+6. NEW IN THIS FORK (Lead 1 - see top comment): resolve each
    //    DISTINCT real stem used by drawGroup0's ranges to a real,
    //    ACTUALLY-COMPILING .fxo_pc file for BOTH role 4 and role 6,
    //    generalizing the parent fork's own pickVertexFile() (literal
    //    `_v`/`_mv` filename only) to try EVERY real candidate file this
    //    stem has, via candidateTryOrder() (`_v`/`_mv` first, UNCHANGED
    //    precedent, then every other real file for the stem in stable
    //    on-disk order). For each candidate, in order, this tries the
    //    SAME unmodified compileOnePass() (role 4 then role 6) + GPU
    //    object creation the parent fork already used, and keeps the
    //    FIRST candidate where BOTH roles fully succeed - not just
    //    structural T8 pairing (step 3's preview already showed that),
    //    but real D3DCompile + D3D11 reflection + the existing known-VS-
    //    input-signature check + real GPU object creation. Every attempt
    //    (success or fail) is printed, so the choice is fully auditable.
    //    Device creation is moved here (was step 6) since compiling now
    //    happens per-stem rather than after a separate resolution pass.
    // -----------------------------------------------------------------
    std::string devErr;
    if (!dev.initialise(kW, kH, devErr)) { printf("FATAL device: %s\n", devErr.c_str()); return false; }
    printf("[device] kind=%s\n\n", dev.isWarp ? "WARP (software rasterizer)" : "Hardware");

    struct StemResolution {
        bool ok = false;
        std::string vsFileName, reason;
        CompiledShader gb, lit; // role4, role6
    };
    std::map<std::string, StemResolution> stemResolutions;
    auto resolveAndCompileStem = [&](const std::string& stem) {
        if (stemResolutions.count(stem)) return; // already resolved (or attempted) for an earlier range
        StemResolution& out = stemResolutions[stem];
        std::vector<std::string> tryOrder = candidateTryOrder(stemToFiles[stem]);
        printf("[stem '%s'] %zu real candidate file(s) to try, in order:", stem.c_str(), tryOrder.size());
        for (const auto& f : tryOrder) printf(" %s", f.c_str());
        printf("\n");
        for (const auto& candidate : tryOrder) {
            std::vector<uint8_t> fxoBytes;
            if (!findEntry(shadersContainer, candidate, fxoBytes)) {
                printf("  candidate '%s': findEntry FAILED - skipped\n", candidate.c_str());
                continue;
            }
            CompiledShader gb = compileOnePass(dev.device, fxoBytes, candidate, stem, /*role=*/4);
            CompiledShader lit = compileOnePass(dev.device, fxoBytes, candidate, stem, /*role=*/6);
            if (gb.ok) {
                HRESULT hr = dev.device->CreateVertexShader(gb.vsCode->GetBufferPointer(), gb.vsCode->GetBufferSize(), nullptr, &gb.vs);
                if (SUCCEEDED(hr)) hr = dev.device->CreatePixelShader(gb.psCode->GetBufferPointer(), gb.psCode->GetBufferSize(), nullptr, &gb.ps);
                if (SUCCEEDED(hr)) hr = dev.device->CreateInputLayout(kFullLayout, 5, gb.vsCode->GetBufferPointer(), gb.vsCode->GetBufferSize(), &gb.inputLayout);
                if (FAILED(hr)) { gb.ok = false; gb.failReason = "GPU object creation failed: " + hrToString(hr); }
            }
            if (lit.ok) {
                HRESULT hr = dev.device->CreateVertexShader(lit.vsCode->GetBufferPointer(), lit.vsCode->GetBufferSize(), nullptr, &lit.vs);
                if (SUCCEEDED(hr)) hr = dev.device->CreatePixelShader(lit.psCode->GetBufferPointer(), lit.psCode->GetBufferSize(), nullptr, &lit.ps);
                if (SUCCEEDED(hr)) hr = dev.device->CreateInputLayout(kFullLayout, 5, lit.vsCode->GetBufferPointer(), lit.vsCode->GetBufferSize(), &lit.inputLayout);
                if (FAILED(hr)) { lit.ok = false; lit.failReason = "GPU object creation failed: " + hrToString(hr); }
            }
            std::string gbMsg = gb.ok ? "OK (pass " + std::to_string(gb.passIndexUsed) + ")" : ("FAIL: " + gb.failReason);
            std::string litMsg = lit.ok ? "OK (pass " + std::to_string(lit.passIndexUsed) + ")" : ("FAIL: " + lit.failReason);
            printf("  candidate '%s': role4=%s  role6=%s\n", candidate.c_str(), gbMsg.c_str(), litMsg.c_str());
            if (gb.ok && lit.ok) {
                if (gb.ok) printf("    real PS outputs written: %u (SV_Target0..%u)  real Normal_MapSampler register: s%d\n",
                                   gb.psOutputCount, gb.psOutputCount ? gb.psOutputCount - 1 : 0, gb.normalMapSamplerReg);
                printf("    real IR_LBufferSampler register: s%d  Diffuse_MapSampler: s%d  Specular_MapSampler: s%d\n",
                       lit.lBufferSamplerReg, lit.diffuseMapSamplerReg, lit.specularMapSamplerReg);
                out.ok = true;
                out.vsFileName = candidate;
                out.gb = std::move(gb);
                out.lit = std::move(lit);
                break;
            }
        }
        if (!out.ok) {
            out.reason = "no real candidate file for this stem fully compiled for both role4 and role6 (" +
                          std::to_string(tryOrder.size()) + " candidate(s) tried)";
            printf("  [stem '%s'] UNRESOLVED: %s\n", stem.c_str(), out.reason.c_str());
        }
        printf("\n");
    };

    std::map<std::string, std::vector<size_t>> byVsFile;
    for (size_t ri = 0; ri < drawGroup0.size(); ++ri) {
        const auto& range = drawGroup0[ri];
        RangeResolution rr; rr.rangeIdx = ri; rr.materialId = range.materialId; rr.submeshIndex = range.submeshIndex;
        if (range.materialId >= matRecs.size()) { rr.reason = "materialId out of range"; resolutions.push_back(rr); continue; }
        rr.shaderHash = matRecs[range.materialId].hash0;
        auto it = stemCrcToStem.find(rr.shaderHash);
        if (it == stemCrcToStem.end()) { rr.reason = "shaderHash does not resolve to a real stem"; resolutions.push_back(rr); continue; }
        rr.stem = it->second;
        resolveAndCompileStem(rr.stem);
        const StemResolution& sres = stemResolutions[rr.stem];
        if (!sres.ok) { rr.reason = sres.reason; resolutions.push_back(rr); continue; }
        rr.vsFileName = sres.vsFileName;
        rr.ok = true;
        resolutions.push_back(rr);
        byVsFile[rr.vsFileName].push_back(resolutions.size() - 1);
    }
    size_t resolvedCount = 0;
    for (const auto& rr : resolutions) if (rr.ok) ++resolvedCount;
    printf("[resolution] %zu/%zu ranges resolved to an ACTUALLY-COMPILING real shader file (role4+role6), %zu "
           "distinct real VS files\n\n", resolvedCount, resolutions.size(), byVsFile.size());

    // Populate gbufferShaders/litShaders keyed by the resolved vsFileName -
    // downstream code (channel needs, PASS A/C draw loops) is unchanged
    // from the parent fork from here on, since it only ever looks these
    // maps up by rr.vsFileName.
    for (auto& kv : stemResolutions) {
        if (!kv.second.ok) continue;
        gbufferShaders[kv.second.vsFileName] = std::move(kv.second.gb);
        litShaders[kv.second.vsFileName] = std::move(kv.second.lit);
    }
    printf("\n");
    return true;
}

bool tower2_proto::tower2FinalizeShaders(Tower2Session& session) {
    auto& dev = session.dev;
    auto& resolutions = session.resolutions;
    auto& gbufferShaders = session.gbufferShaders;
    auto& litShaders = session.litShaders;
    auto& drawableMaterialIds = session.drawableMaterialIds;
    // -----------------------------------------------------------------
    // NEW IN THIS FORK (diagnostic task, 2026-10-08, orchestrator relay of
    // a sibling agent's finding): the ONLY role6 (lit material pass) PS
    // body this file's own top comment ever independently re-traced
    // byte-for-byte this session is ir_bbsimple1's (materialId 7/11 only)
    // - confirmed there to end with a real lit-composite shape
    // (`output.oC0.xyzw = r2.xyzw * c[37].xyzw`, c[37]=Tint_color). The
    // OTHER 12 of this tower's 14 drawable materials (materialId
    // 0,2,3,4,5,6,9,10,12,13,14,15) resolve, via this fork's OWN
    // generalized candidateTryOrder() (Lead 1 above - try every real
    // candidate file for a stem, not just _v/_mv, until ONE fully
    // compiles for BOTH role4 AND role6), to OTHER real stems/candidate
    // files whose role6 PS body was never individually read this session
    // - the try-order mechanism only checks that a candidate COMPILES for
    // both roles, not that its role6 PS is semantically a lit-material
    // composite (the "role" a file is reached through is a T8 role-table
    // SLOT NUMBER, not a verified semantic guarantee - see Lead 1's own
    // "which one is tried first only matters if an earlier candidate
    // fails to compile" caveat above). A sibling agent flagged this
    // scene's final image as "solid, highly-saturated FLAT colors per
    // face... visually match[ing] a raw G-buffer surface-normal-encoding
    // debug view" and hypothesised PASS C might be reading the wrong
    // intermediate target - independently re-checked THIS session by MD5
    // (the flagged capture is byte-identical, df05d007b471049ab7812944
    // 765e9a5c, to this scene's own already-frozen baseline, so it is the
    // SAME already-documented state, not a new symptom) AND by direct
    // RTV-wiring code read (PASS A writes normalsRT/dsfDataRT/
    // lightingParamRT + gbufferDepth.dsv; PASS B writes lightBufferRT.rtv;
    // PASS C writes dev.rtv + finalDepth.dsv - three disjoint
    // ID3D11RenderTargetView* sets, no shared pointer, no RTV mixup as
    // literally written). This block settles the REMAINING open part of
    // that lead directly: does every one of this tower's real role6 PS
    // bodies actually end in a lit-composite write, or could SOME of the
    // 12 never-traced ones instead be a disguised G-buffer-shaped encode
    // (reached only because the try-order mechanism never checks for
    // that)? Prints the REAL translated HLSL (sr3d3d9bc::translateToHlsl,
    // this project's own existing SM2/SM3-bytecode-to-HLSL clean-room
    // translator - operating on this shader's own already-extracted real
    // bytecode bytes, the SAME translation every compiled pass in this
    // tool already goes through to reach D3DCompile, not a new
    // mechanism) for EVERY distinct real role6 shader this tower
    // resolves to, in full, OPT-IN via SR3_DUMP_ROLE6_HLSL=1 (unset =
    // this block never prints, zero byte change to any existing golden).
    // -----------------------------------------------------------------
    const char* dumpHlslRaw = std::getenv("SR3_DUMP_ROLE6_HLSL");
    const bool kDumpRole6Hlsl = (dumpHlslRaw != nullptr) && (std::string(dumpHlslRaw) == "1");
    if (kDumpRole6Hlsl) {
        printf("=== [DUMP, SR3_DUMP_ROLE6_HLSL=1] Real translated role6 (lit material pass) PS HLSL, "
               "one per DISTINCT resolved shader (%zu distinct real VS file(s)) ===\n", litShaders.size());
        for (auto& kv : litShaders) {
            if (!kv.second.ok) continue;
            std::vector<uint32_t> usedBy;
            for (const auto& rr : resolutions) if (rr.ok && rr.vsFileName == kv.first) usedBy.push_back(rr.materialId);
            printf("--- role6 PS for resolved VS file '%s' (stem '%s', used by materialId(s):", kv.first.c_str(), kv.second.stem.c_str());
            for (uint32_t m : usedBy) printf(" %u", m);
            printf(") ---\n%s\n--- end role6 PS HLSL for '%s' ---\n\n", kv.second.psTr.hlsl.c_str(), kv.first.c_str());
        }
    }

    // A range is drawable in this pipeline only if BOTH its real role-4 and
    // role-6 passes compiled - reported honestly either way.
    size_t drawableRanges = 0;
    for (auto& rr : resolutions) {
        if (!rr.ok) continue;
        bool gbOk = gbufferShaders.count(rr.vsFileName) && gbufferShaders[rr.vsFileName].ok;
        bool litOk = litShaders.count(rr.vsFileName) && litShaders[rr.vsFileName].ok;
        if (!gbOk || !litOk) {
            rr.ok = false;
            rr.reason = "shader '" + rr.vsFileName + "': role4 ok=" + std::to_string(gbOk) + " role6 ok=" + std::to_string(litOk);
            continue;
        }
        ++drawableRanges;
    }
    printf("[final] %zu/%zu real draw ranges drawable through the new pipeline\n", drawableRanges, resolutions.size());
    for (const auto& rr : resolutions) if (rr.ok) drawableMaterialIds.insert(rr.materialId);
    printf("[final] drawable materialId(s):");
    for (uint32_t m : drawableMaterialIds) printf(" %u", m);
    printf("\n\n");
    if (drawableRanges == 0) { printf("FATAL: no drawable range\n"); return false; }
    return true;
}

bool tower2_proto::tower2UploadTextures(Tower2Session& session) {
    auto& cityContainer = *session.cityContainer_;
    auto& dev = session.dev;
    auto& matBindings = session.matBindings;
    auto& drawableMaterialIds = session.drawableMaterialIds;
    auto& materialNormalTex = session.materialNormalTex;
    auto& materialDiffuseTex = session.materialDiffuseTex;
    auto& materialSpecularTex = session.materialSpecularTex;
    // -----------------------------------------------------------------
    // 7. Real per-material texture upload (this task's own item 3) - for
    //    every drawable materialId, resolve its real normal/diffuse/
    //    specular texture names via LevelMesh::materialTextureBinding()
    //    (already computed above, matBindings[]) and upload them through
    //    the SAME findAndUploadTexture()/TextureSearchCache machinery
    //    runClmesh()/runClmeshTile() already use (copied verbatim above -
    //    not reinvented), searching the SAME top-level sr3_city_0.vpp_pc
    //    archive runClmesh() searches from.
    // -----------------------------------------------------------------
    {
        TextureSearchCache texCache;
        for (uint32_t matId : drawableMaterialIds) {
            const sr3geometry::MaterialBinding& mb = matBindings[matId];
            if (const std::string* n = mb.normalMap()) {
                sr3render::UploadedTexture t; std::string foundIn;
                if (findAndUploadTexture(cityContainer, *n, dev.device, t, foundIn, &texCache)) {
                    materialNormalTex[matId] = t;
                    printf("[texture] material[%u] normal:   %s from %s (%ux%u)\n", matId, n->c_str(), foundIn.c_str(), t.width, t.height);
                } else {
                    printf("[texture] material[%u] normal:   %s - NOT FOUND - white placeholder\n", matId, n->c_str());
                }
            }
            if (const std::string* d = mb.diffuse()) {
                sr3render::UploadedTexture t; std::string foundIn;
                if (findAndUploadTexture(cityContainer, *d, dev.device, t, foundIn, &texCache)) {
                    materialDiffuseTex[matId] = t;
                    printf("[texture] material[%u] diffuse:  %s from %s (%ux%u)\n", matId, d->c_str(), foundIn.c_str(), t.width, t.height);
                } else {
                    printf("[texture] material[%u] diffuse:  %s - NOT FOUND - white placeholder\n", matId, d->c_str());
                }
            }
            if (const sr3geometry::TextureRef* s = mb.byParamHash(kParamHashSpecularClmesh)) {
                sr3render::UploadedTexture t; std::string foundIn;
                if (findAndUploadTexture(cityContainer, s->name, dev.device, t, foundIn, &texCache)) {
                    materialSpecularTex[matId] = t;
                    printf("[texture] material[%u] specular: %s from %s (%ux%u)\n", matId, s->name.c_str(), foundIn.c_str(), t.width, t.height);
                } else {
                    printf("[texture] material[%u] specular: %s - NOT FOUND - white placeholder\n", matId, s->name.c_str());
                }
            }
        }
    }
    printf("\n");
    return true;
}

bool tower2_proto::tower2DecodeChannels(Tower2Session& session) {
    auto& chosenMesh = session.chosenMesh;
    auto& resolutions = session.resolutions;
    auto& gbufferShaders = session.gbufferShaders;
    auto& litShaders = session.litShaders;
    auto& channels = session.channels;
    const auto& drawGroup0 = chosenMesh.drawGroups()[0];
    // -----------------------------------------------------------------
    // 8. Real channel decode - identical mechanism to prototype_lit_car
    //    .cpp's own step 4 (union of BOTH role-4/role-6 variants' input
    //    needs per channel), sourced from chosenMesh (this render group's
    //    own real Mesh sub-block) instead of a vehicle mesh.
    // -----------------------------------------------------------------
    std::set<int> neededChannels;
    std::map<int, bool> channelNeedsTangent, channelNeedsNormal;
    for (const auto& rr : resolutions) {
        if (!rr.ok) continue;
        int ch = static_cast<int>(rr.submeshIndex);
        neededChannels.insert(ch);
        for (const CompiledShader* cs : {&gbufferShaders[rr.vsFileName], &litShaders[rr.vsFileName]}) {
            for (const auto& f : cs->vsInputs) {
                if (f.usage == kTANGENT) channelNeedsTangent[ch] = true;
                if (f.usage == kNORMAL) channelNeedsNormal[ch] = true;
            }
        }
    }
    for (int ch : neededChannels) {
        if (ch < 0 || static_cast<size_t>(ch) >= chosenMesh.channels().size()) { printf("  channel %d out of range\n", ch); continue; }
        ChannelData cd;
        const auto& chanMeta = chosenMesh.channels()[static_cast<size_t>(ch)];
        cd.layoutInfo = sr3mesh::layoutInfoFor(chanMeta.layoutCode);
        try { cd.verts = chosenMesh.decodeChannel(static_cast<size_t>(ch)); }
        catch (const std::exception& ex) { printf("  channel %d decodeChannel FAILED: %s\n", ch, ex.what()); continue; }
        printf("[channel %d] layoutCode=%u vertices=%zu hasNormal=%d hasTangent=%d\n", ch, chanMeta.layoutCode,
               cd.verts.size(), cd.layoutInfo.hasNormal, cd.layoutInfo.hasTangent);
        bool placeholderTangent = channelNeedsTangent[ch] && !cd.layoutInfo.hasTangent;
        bool placeholderNormal = channelNeedsNormal[ch] && !cd.layoutInfo.hasNormal;
        if (placeholderTangent) printf("    PLACEHOLDER tangent (channel genuinely has none, a shader needs it)\n");
        if (placeholderNormal) printf("    PLACEHOLDER normal (channel genuinely has none, a shader needs it)\n");
        cd.gpuVerts.resize(cd.verts.size());
        for (size_t i = 0; i < cd.verts.size(); ++i) {
            const auto& v = cd.verts[i];
            GpuVertexReal g{};
            g.position[0] = v.position[0]; g.position[1] = v.position[1]; g.position[2] = v.position[2]; g.position[3] = 1.0f;
            if (cd.layoutInfo.hasNormal) {
                g.normal[0] = v.normal[0]*0.5f+0.5f; g.normal[1] = v.normal[1]*0.5f+0.5f; g.normal[2] = v.normal[2]*0.5f+0.5f; g.normal[3] = v.normalW/255.0f;
            } else { g.normal[0]=0.5f; g.normal[1]=0.5f; g.normal[2]=1.0f; g.normal[3]=0.5f; }
            if (cd.layoutInfo.hasTangent) {
                g.tangent[0] = v.tangent[0]*0.5f+0.5f; g.tangent[1] = v.tangent[1]*0.5f+0.5f; g.tangent[2] = v.tangent[2]*0.5f+0.5f; g.tangent[3] = v.tangentW/255.0f;
            } else { g.tangent[0]=1.0f; g.tangent[1]=0.5f; g.tangent[2]=0.5f; g.tangent[3]=0.5f; }
            // REAL decode value, unchanged mechanism - whatever this static
            // prop's own Mesh sub-block encodes here (the SAME sr3mesh
            // reader vehicles use); no vehicle-style parts() array exists
            // for a .clmesh_pc, so this is never remapped through one.
            g.blendIndices[0]=g.blendIndices[1]=g.blendIndices[2]=g.blendIndices[3] = static_cast<float>(v.rigidPartIndex);
            if (!v.texcoordsRaw.empty()) { g.texcoord0[0] = static_cast<float>(v.texcoordsRaw[0][0]); g.texcoord0[1] = static_cast<float>(v.texcoordsRaw[0][1]); }
            cd.gpuVerts[i] = g;
        }
        channels[ch] = std::move(cd);
    }
    for (const auto& rr : resolutions) {
        if (!rr.ok) continue;
        auto cit = channels.find(static_cast<int>(rr.submeshIndex));
        if (cit == channels.end()) continue;
        std::vector<uint32_t> tri = chosenMesh.triangleListForRange(drawGroup0[rr.rangeIdx]);
        size_t start = cit->second.gpuIndices.size();
        cit->second.gpuIndices.insert(cit->second.gpuIndices.end(), tri.begin(), tri.end());
        cit->second.rangeIndexSpan[rr.rangeIdx] = {start, tri.size()};
    }
    printf("\n");
    return true;
}

