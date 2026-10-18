#include "prototype_lit_clmesh_tower_session.h"

using namespace tower_proto;

bool tower_proto::towerSetupCamera(TowerSession& session) {
    auto& channels = session.channels;
    auto& viewOnly = session.viewOnly;
    auto& viewProj = session.viewProj;
    auto& invProj = session.invProj;
    auto& worldIdentity = session.worldIdentity;
    auto& kW = session.kW;
    auto& kH = session.kH;
    // -----------------------------------------------------------------
    // 9. Real object-space bounds, camera. REAL bounds computed directly
    //    from this render group's own decoded vertex positions - NO
    //    per-part transform, since .clmesh_pc has no vehicle-style
    //    parts() array (a static prop's geometry is already in one real
    //    object space) - the SAME mechanism runClmesh()'s own
    //    MeshRenderer::boundsMin()/boundsMax() already uses for this
    //    exact prop. Camera: identical mechanism to prototype_lit_car
    //    .cpp's own step 5 (CHOSEN yaw/pitch/distanceScale/fov, SAME
    //    literal values the existing `clmesh` command and the parent
    //    prototype both already use).
    // -----------------------------------------------------------------
    float boundsMin[3] = {1e30f,1e30f,1e30f}, boundsMax[3] = {-1e30f,-1e30f,-1e30f};
    for (auto& kv : channels) {
        for (const auto& v : kv.second.verts) {
            for (int c = 0; c < 3; ++c) { if (v.position[c] < boundsMin[c]) boundsMin[c] = v.position[c]; if (v.position[c] > boundsMax[c]) boundsMax[c] = v.position[c]; }
        }
    }
    printf("[bounds] min(%.3f %.3f %.3f) max(%.3f %.3f %.3f)\n", boundsMin[0], boundsMin[1], boundsMin[2],
           boundsMax[0], boundsMax[1], boundsMax[2]);

    const float yaw = 0.6f, pitch = 0.35f, distanceScale = 1.8f;
    computeOrbitViewMatrix(boundsMin, boundsMax, yaw, pitch, distanceScale, viewOnly);
    float extent = 0.0f;
    for (int i = 0; i < 3; ++i) { float span = boundsMax[i]-boundsMin[i]; if (span > extent) extent = span; }
    if (extent <= 0.0f) extent = 1.0f;
    float nearZ = extent * 0.05f, farZ = extent * distanceScale * 4.0f; // CHOSEN, see top comment
    float projOnly[16];
    buildPerspectiveLH(0.785398f /* 45 deg, CHOSEN */, static_cast<float>(kW) / kH, nearZ, farZ, projOnly);
    sr3render::multiplyMatrix4x4(viewOnly, projOnly, viewProj);
    bool projInvertible = invert4x4(projOnly, invProj);
    printf("[camera] CHOSEN fovY=45deg near=%.3f far=%.3f (not real game camera data). projection invertible=%d\n",
           nearZ, farZ, projInvertible);
    sr3render::buildWorldMatrix(0.0f, 0.0f, 0.0f, 0.0f, 1.0f, worldIdentity);
    return true;
}

bool tower_proto::towerFillConstantBuffers(TowerSession& session) {
    auto& fillLog = session.fillLog;
    auto& dev = session.dev;
    auto& gbufferShaders = session.gbufferShaders;
    auto& litShaders = session.litShaders;
    auto& channels = session.channels;
    auto& viewProj = session.viewProj;
    auto& viewOnly = session.viewOnly;
    auto& worldIdentity = session.worldIdentity;
    // -----------------------------------------------------------------
    // 10. Real per-shader cbuffer fill - identical CTAB-name-driven logic
    //     to prototype_lit_car.cpp's own step 6, with ONE change: the
    //     "bone"-named branch (Bone_weights, 192 registers/64 elements on
    //     this shader family too) always fills identity, since .clmesh_pc
    //     has no vehicle-style parts() array to index into (a static
    //     prop's geometry already lives in one real object space) - the
    //     SAME identity fallback the parent prototype already applies for
    //     any part index past its own real veh.parts().size(), just
    //     unconditional here since there IS no parts array. The vehicle-
    //     paint-specific "ir_sr3carpaint_gr grime_tiling" branch is
    //     dropped (dead code for this fork's own real stems - car paint
    //     only). No per-material array-B/C override exists for
    //     .clmesh_pc (see this file's own top comment, the central OPEN
    //     finding) so there is no buildPerMaterialOverrides step here;
    //     every material sharing a shader draws with the SAME shared
    //     per-shader cbPs.
    // -----------------------------------------------------------------
    auto fillVsCbuffer = [&](CompiledShader& cs) {
        cs.vsCbufCpu.assign(cs.vsCbSize, 0);
        for (const auto& c : cs.vsCtab.constants) {
            if (c.registerSet != sr3d3d9bc::RegisterSet::Float4) continue;
            std::vector<std::array<float,4>> regs; std::string decision;
            // HYPOTHESIS-BASED (render-pipeline provenance downgrade, 2026-09-30): filling a CTAB constant
            // named *proj* (projTM, VS c28x4) with the fused view*projection and *world2view* (c48) with the
            // view matrix rests on spec-render-pipeline.md Sec20.12.5/Sec20.12.9, now HYPOTHESIS on provenance
            // pending re-derivation from the shipped shaders (bridge job 06, ctab_census). No behaviour change.
            if (containsCI(c.name, "proj")) {
                for (int i = 0; i < 4; ++i) { std::array<float,4> r; packColumn(viewProj, i, r.data()); regs.push_back(r); }
                decision = "REAL: this tool's own view*projection (see top comment for the CHOSEN camera params)";
            } else if (containsCI(c.name, "world2view")) {
                for (int i = 0; i < 3; ++i) { std::array<float,4> r; packColumn(viewOnly, i, r.data()); regs.push_back(r); }
                decision = "REAL: this tool's own view matrix";
            } else if (containsCI(c.name, "obj") && containsCI(c.name, "tm")) {
                for (int i = 0; i < 3; ++i) { std::array<float,4> r; packColumn(worldIdentity, i, r.data()); regs.push_back(r); }
                decision = "PLACEHOLDER: identity world transform (no real per-instance placement - OPEN, see "
                           "runClmeshTile()'s own doc comment in tools/sr3_viewer.cpp for why)";
            } else if (containsCI(c.name, "bone")) {
                const size_t maxElems = c.registerCount / 3;
                for (size_t e = 0; e < maxElems; ++e) {
                    float packed[3][4];
                    for (int i = 0; i < 3; ++i) packColumn(worldIdentity, i, packed[i]);
                    for (int i = 0; i < 3; ++i) { std::array<float,4> r; std::memcpy(r.data(), packed[i], 16); regs.push_back(r); }
                }
                decision = "PLACEHOLDER: identity per slot (.clmesh_pc has no parts()/bone-palette array - a "
                           "static prop is already in one real object space)";
            } else {
                for (uint16_t r = 0; r < c.registerCount; ++r) regs.push_back({0,0,0,0});
                decision = "PLACEHOLDER: zero (untraced per-instance parameter)";
            }
            fillFloat4Registers(cs.vsCbufCpu, c.registerIndex, c.registerCount, regs);
            fillLog.push_back({cs.vsFileName + " role" + std::to_string(cs.roleUsed) + " VS " + c.name, decision});
        }
    };
    auto fillPsCbufferTemplate = [&](CompiledShader& cs) {
        cs.psCbufCpu.assign(cs.psCbSize, 0);
        for (const auto& c : cs.psCtab.constants) {
            if (c.registerSet != sr3d3d9bc::RegisterSet::Float4) continue;
            std::vector<std::array<float,4>> regs; std::string decision;
            if (containsCI(c.name, "specular_power")) {
                for (uint16_t r = 0; r < c.registerCount; ++r) regs.push_back({0.1f,0.1f,0.1f,0.1f});
                decision = "REAL engine default (spec-render-pipeline.md Sec20.12.11): 0.1 floor";
            } else if (containsCI(c.name, "tint_color")) {
                // PLACEHOLDER, carried over from prototype_lit_car.cpp's own
                // deliberately-reintroduced override, and INDEPENDENTLY
                // RE-CONFIRMED this session for ir_bbsimple1's own real
                // role6 pass2 PS body (not just inherited by analogy - see
                // this file's own top comment): its FINAL instruction is
                // unconditionally "output.oC0.xyzw = r2.xyzw * c[37].xyzw"
                // where c[37] IS Tint_color - i.e. with the real-confirmed-
                // zero-when-unauthored regime taken literally, every pixel
                // this pass draws would be EXACTLY (0,0,0,0), defeating
                // this whole pipeline's own visual test, for every
                // material, regardless of the real light-buffer output
                // this task exists to exercise.
                for (uint16_t r = 0; r < c.registerCount; ++r) regs.push_back({1.0f,1.0f,1.0f,1.0f});
                decision = "PLACEHOLDER (carried over + independently re-confirmed this session): real Tint_color=0 "
                           "would make this pass's own final instruction (oC0 = r2*Tint_color) exactly zero for "
                           "every pixel - see top comment for this session's own direct HLSL re-trace";
            } else {
                for (uint16_t r = 0; r < c.registerCount; ++r) regs.push_back({0,0,0,0});
                decision = "REAL confirmed zero (spec-render-pipeline.md Sec20.12.11): unauthored -> memset-zero";
            }
            fillFloat4Registers(cs.psCbufCpu, c.registerIndex, c.registerCount, regs);
            fillLog.push_back({cs.vsFileName + " role" + std::to_string(cs.roleUsed) + " PS " + c.name, decision});
        }
    };
    for (auto& kv : gbufferShaders) { if (kv.second.ok) { fillVsCbuffer(kv.second); fillPsCbufferTemplate(kv.second); } }
    for (auto& kv : litShaders)     { if (kv.second.ok) { fillVsCbuffer(kv.second); fillPsCbufferTemplate(kv.second); } }

    auto createCbuffers = [&](CompiledShader& cs) {
        D3D11_BUFFER_DESC cbVsDesc{}; cbVsDesc.ByteWidth = static_cast<UINT>((cs.vsCbufCpu.size()+15)&~size_t(15));
        cbVsDesc.Usage = D3D11_USAGE_DEFAULT; cbVsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cs.vsCbufCpu.resize(cbVsDesc.ByteWidth, 0);
        D3D11_SUBRESOURCE_DATA cbVsData{}; cbVsData.pSysMem = cs.vsCbufCpu.data();
        dev.device->CreateBuffer(&cbVsDesc, &cbVsData, &cs.cbVs);
        D3D11_BUFFER_DESC cbPsDesc{}; cbPsDesc.ByteWidth = static_cast<UINT>((cs.psCbufCpu.size()+15)&~size_t(15));
        cbPsDesc.Usage = D3D11_USAGE_DEFAULT; cbPsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cs.psCbufCpu.resize(cbPsDesc.ByteWidth, 0);
        D3D11_SUBRESOURCE_DATA cbPsData{}; cbPsData.pSysMem = cs.psCbufCpu.data();
        dev.device->CreateBuffer(&cbPsDesc, &cbPsData, &cs.cbPs);
    };
    for (auto& kv : gbufferShaders) if (kv.second.ok) createCbuffers(kv.second);
    for (auto& kv : litShaders) if (kv.second.ok) createCbuffers(kv.second);

    printf("=== Constant-buffer fill decisions (real vs. placeholder, by real CTAB name) ===\n");
    for (const auto& l : fillLog) printf("  %-56s : %s\n", l.constName.c_str(), l.decision.c_str());
    printf("\n");

    // -----------------------------------------------------------------
    // 11. Per-channel GPU vertex/index buffers - identical to
    //     prototype_lit_car.cpp's own step 7.
    // -----------------------------------------------------------------
    for (auto& kv : channels) {
        ChannelData& cd = kv.second;
        if (cd.gpuVerts.empty() || cd.gpuIndices.empty()) continue;
        D3D11_BUFFER_DESC vbDesc{}; vbDesc.ByteWidth = static_cast<UINT>(cd.gpuVerts.size()*sizeof(GpuVertexReal));
        vbDesc.Usage = D3D11_USAGE_IMMUTABLE; vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vbData{}; vbData.pSysMem = cd.gpuVerts.data();
        dev.device->CreateBuffer(&vbDesc, &vbData, &cd.vb);
        D3D11_BUFFER_DESC ibDesc{}; ibDesc.ByteWidth = static_cast<UINT>(cd.gpuIndices.size()*sizeof(uint32_t));
        ibDesc.Usage = D3D11_USAGE_IMMUTABLE; ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA ibData{}; ibData.pSysMem = cd.gpuIndices.data();
        dev.device->CreateBuffer(&ibDesc, &ibData, &cd.ib);
    }
    return true;
}

bool tower_proto::towerCreateTargets(TowerSession& session) {
    auto& dev = session.dev;
    auto& kW = session.kW;
    auto& kH = session.kH;
    auto& kGBufferFormat = session.kGBufferFormat;
    auto& normalsRT = session.normalsRT;
    auto& depthParamRT = session.depthParamRT;
    auto& lightingParamRT = session.lightingParamRT;
    auto& lightBufferRT = session.lightBufferRT;
    auto& gbufferDepth = session.gbufferDepth;
    auto& depthState = session.depthState;
    auto& noDepthState = session.noDepthState;
    auto& sampler = session.sampler;
    auto& whitePlaceholderSrv = session.whitePlaceholderSrv;
    auto& blackPlaceholderSrv = session.blackPlaceholderSrv;
    // -----------------------------------------------------------------
    // 12. Real off-screen G-buffer targets (3 colour + 1 depth) and the
    //     light-accumulation buffer - identical to prototype_lit_car.cpp's
    //     own step 8.
    // -----------------------------------------------------------------
    std::string tErr;
    bool targetsOk = normalsRT.create(dev.device, kW, kH, kGBufferFormat, tErr);
    if (targetsOk) targetsOk = depthParamRT.create(dev.device, kW, kH, kGBufferFormat, tErr);
    if (targetsOk) targetsOk = lightingParamRT.create(dev.device, kW, kH, kGBufferFormat, tErr);
    if (targetsOk) targetsOk = lightBufferRT.create(dev.device, kW, kH, kGBufferFormat, tErr);
    if (targetsOk) targetsOk = gbufferDepth.create(dev.device, kW, kH, tErr);
    if (!targetsOk) { printf("FATAL: off-screen target creation failed: %s\n", tErr.c_str()); return false; }
    printf("[gbuffer] created 3 real off-screen colour targets (Normals/Depth/Lighting, CHOSEN format %s, %ux%u) "
           "+ 1 real depth-stencil surface + 1 real light-accumulation target.\n", "DXGI_FORMAT_R8G8B8A8_UNORM", kW, kH);

    { D3D11_DEPTH_STENCIL_DESC dsDesc{}; dsDesc.DepthEnable = TRUE; dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
      dsDesc.DepthFunc = D3D11_COMPARISON_LESS; dev.device->CreateDepthStencilState(&dsDesc, &depthState); }
    { D3D11_DEPTH_STENCIL_DESC dsDesc{}; dsDesc.DepthEnable = FALSE; dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
      dev.device->CreateDepthStencilState(&dsDesc, &noDepthState); }
    ID3D11RasterizerState* rasterState = nullptr;
    { D3D11_RASTERIZER_DESC rsDesc{}; rsDesc.FillMode = D3D11_FILL_SOLID; rsDesc.CullMode = D3D11_CULL_NONE; rsDesc.DepthClipEnable = TRUE;
      dev.device->CreateRasterizerState(&rsDesc, &rasterState); }
    { D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
      sd.MaxLOD = D3D11_FLOAT32_MAX; dev.device->CreateSamplerState(&sd, &sampler); }
    ID3D11Texture2D* whitePlaceholderTex = nullptr;
    { D3D11_TEXTURE2D_DESC td{}; td.Width=1; td.Height=1; td.MipLevels=1; td.ArraySize=1; td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
      td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_IMMUTABLE; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
      uint8_t px[4]={255,255,255,255}; D3D11_SUBRESOURCE_DATA sub{}; sub.pSysMem=px; sub.SysMemPitch=4;
      dev.device->CreateTexture2D(&td, &sub, &whitePlaceholderTex); dev.device->CreateShaderResourceView(whitePlaceholderTex, nullptr, &whitePlaceholderSrv); }
    // BLACK placeholder - NEEDED for ir_light_directional pass 0's own real
    // ir_ambient_occlusionSampler (AO.x==1.0 means FULLY OCCLUDED in this
    // shader's own real math - the opposite of white's usual "neutral"
    // meaning elsewhere in this tool family). Unchanged from the parent
    // prototype.
    ID3D11Texture2D* blackPlaceholderTex = nullptr;
    { D3D11_TEXTURE2D_DESC td{}; td.Width=1; td.Height=1; td.MipLevels=1; td.ArraySize=1; td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
      td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_IMMUTABLE; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
      uint8_t px[4]={0,0,0,0}; D3D11_SUBRESOURCE_DATA sub{}; sub.pSysMem=px; sub.SysMemPitch=4;
      dev.device->CreateTexture2D(&td, &sub, &blackPlaceholderTex); dev.device->CreateShaderResourceView(blackPlaceholderTex, nullptr, &blackPlaceholderSrv); }

    dev.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dev.context->RSSetState(rasterState);
    D3D11_VIEWPORT vp{}; vp.Width = static_cast<float>(kW); vp.Height = static_cast<float>(kH); vp.MinDepth=0; vp.MaxDepth=1;
    dev.context->RSSetViewports(1, &vp);
    return true;
}

