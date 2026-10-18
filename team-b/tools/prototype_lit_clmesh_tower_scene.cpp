#include "prototype_lit_clmesh_tower_session.h"

using namespace tower_proto;

bool tower_proto::towerResolveRenderGroup(TowerSession& session) {
    auto& stemToFiles = session.stemToFiles;
    auto& stemCrcToStem = session.stemCrcToStem;
    auto& clView = session.clView;
    auto& glView = session.glView;
    auto& lm = session.lm;
    auto& matRecs = session.matRecs;
    auto& chosenMesh = session.chosenMesh;
    auto& resolutions = session.resolutions;
    auto& byVsFile = session.byVsFile;
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

    // -----------------------------------------------------------------
    // 5. Resolve each range's real shader stem + VS file - identical
    //    mechanism to prototype_lit_car.cpp's own step 2, EXCEPT the
    //    shaderHash source is matRecs[range.materialId].hash0 (LevelMesh's
    //    own real per-material hash0, already 100% resolved per the
    //    census above) rather than the vehicle geometry block's own
    //    array. A range's stem can resolve (real hash0 -> real stem) yet
    //    still be marked not-ok here if that stem has no real _v/_mv
    //    file - reported via `reason`, never silently placeholdered.
    // -----------------------------------------------------------------
    for (size_t ri = 0; ri < drawGroup0.size(); ++ri) {
        const auto& range = drawGroup0[ri];
        RangeResolution rr; rr.rangeIdx = ri; rr.materialId = range.materialId; rr.submeshIndex = range.submeshIndex;
        if (range.materialId >= matRecs.size()) { rr.reason = "materialId out of range"; resolutions.push_back(rr); continue; }
        rr.shaderHash = matRecs[range.materialId].hash0;
        auto it = stemCrcToStem.find(rr.shaderHash);
        if (it == stemCrcToStem.end()) { rr.reason = "shaderHash does not resolve to a real stem"; resolutions.push_back(rr); continue; }
        rr.stem = it->second;
        std::string noVertexReason;
        rr.vsFileName = pickVertexFile(stemToFiles[rr.stem], noVertexReason);
        if (rr.vsFileName.empty()) { rr.reason = noVertexReason; resolutions.push_back(rr); continue; }
        rr.ok = true;
        resolutions.push_back(rr);
        byVsFile[rr.vsFileName].push_back(resolutions.size() - 1);
    }
    size_t resolvedCount = 0;
    for (const auto& rr : resolutions) if (rr.ok) ++resolvedCount;
    printf("[resolution] %zu/%zu ranges resolved to a drawable real VS file, %zu distinct real VS files\n\n",
           resolvedCount, resolutions.size(), byVsFile.size());
    return true;
}

bool tower_proto::towerCompileShaders(TowerSession& session) {
    auto& kW = session.kW;
    auto& kH = session.kH;
    auto& shadersContainer = *session.shadersContainer_;
    auto& dev = session.dev;
    auto& resolutions = session.resolutions;
    auto& byVsFile = session.byVsFile;
    auto& gbufferShaders = session.gbufferShaders;
    auto& litShaders = session.litShaders;
    auto& drawableMaterialIds = session.drawableMaterialIds;
    // -----------------------------------------------------------------
    // 6. Compile BOTH real passes (role 4 G-buffer-write, role 6
    //    forward-lit) for each distinct resolved shader file - identical
    //    mechanism to prototype_lit_car.cpp's own step 3.
    // -----------------------------------------------------------------
    std::string devErr;
    if (!dev.initialise(kW, kH, devErr)) { printf("FATAL device: %s\n", devErr.c_str()); return false; }
    printf("[device] kind=%s\n\n", dev.isWarp ? "WARP (software rasterizer)" : "Hardware");

    for (const auto& kv : byVsFile) {
        const std::string& vsFileName = kv.first;
        std::string stem = resolutions[kv.second[0]].stem;
        std::vector<uint8_t> fxoBytes;
        if (!findEntry(shadersContainer, vsFileName, fxoBytes)) {
            printf("[shader '%s'] FATAL: real file not found via top-level lookup\n", vsFileName.c_str());
            continue;
        }
        CompiledShader gb = compileOnePass(dev.device, fxoBytes, vsFileName, stem, /*role=*/4);
        CompiledShader lit = compileOnePass(dev.device, fxoBytes, vsFileName, stem, /*role=*/6);
        gb.rangesUsing = kv.second.size();
        lit.rangesUsing = kv.second.size();
        printf("[shader '%s'] stem='%s' usedByRanges=%zu\n", vsFileName.c_str(), stem.c_str(), kv.second.size());
        printf("  role4 (G-buffer write, real pass index %d): %s%s\n", gb.passIndexUsed, gb.ok ? "OK" : "FAILED: ",
               gb.ok ? "" : gb.failReason.c_str());
        if (gb.ok) printf("    real PS outputs written: %u (SV_Target0..%u)  real Normal_MapSampler register: s%d\n",
                           gb.psOutputCount, gb.psOutputCount ? gb.psOutputCount - 1 : 0, gb.normalMapSamplerReg);
        printf("  role6 (forward lit, real pass index %d): %s%s\n", lit.passIndexUsed, lit.ok ? "OK" : "FAILED: ",
               lit.ok ? "" : lit.failReason.c_str());
        if (lit.ok) printf("    real IR_LBufferSampler register: s%d  Diffuse_MapSampler: s%d  Specular_MapSampler: s%d\n",
                            lit.lBufferSamplerReg, lit.diffuseMapSamplerReg, lit.specularMapSamplerReg);
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
        gbufferShaders[vsFileName] = std::move(gb);
        litShaders[vsFileName] = std::move(lit);
    }
    printf("\n");

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

bool tower_proto::towerUploadTextures(TowerSession& session) {
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

bool tower_proto::towerDecodeChannels(TowerSession& session) {
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

