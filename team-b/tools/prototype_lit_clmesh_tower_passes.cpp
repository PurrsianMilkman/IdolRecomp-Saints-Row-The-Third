#include "prototype_lit_clmesh_tower_session.h"

using namespace tower_proto;

bool tower_proto::towerPassGBuffer(TowerSession& session) {
    auto& dev = session.dev;
    auto& resolutions = session.resolutions;
    auto& channels = session.channels;
    auto& gbufferShaders = session.gbufferShaders;
    auto& whitePlaceholderSrv = session.whitePlaceholderSrv;
    auto& materialNormalTex = session.materialNormalTex;
    auto& normalsRT = session.normalsRT;
    auto& depthParamRT = session.depthParamRT;
    auto& lightingParamRT = session.lightingParamRT;
    auto& gbufferDepth = session.gbufferDepth;
    auto& depthState = session.depthState;
    auto& sampler = session.sampler;
    auto& kW = session.kW;
    auto& kH = session.kH;
    // -----------------------------------------------------------------
    // PASS A: real G-buffer prepass. Every drawable range, its own real
    // role-4 shader, into the 3 real off-screen targets + real DSV. The
    // ONLY change from prototype_lit_car.cpp's own PASS A: a range's
    // Normal_MapSampler (if its role-4 PS declares one - it does, for
    // ir_bbsimple1) is bound to that material's REAL uploaded normal
    // texture (step 7 above) instead of the white placeholder every OTHER
    // texture slot still gets.
    // -----------------------------------------------------------------
    ID3D11RenderTargetView* gbufferRtvs[3] = {normalsRT.rtv, depthParamRT.rtv, lightingParamRT.rtv};
    dev.context->OMSetRenderTargets(3, gbufferRtvs, gbufferDepth.dsv);
    { const float clr[4] = {0,0,0,0}; for (auto rtv : gbufferRtvs) dev.context->ClearRenderTargetView(rtv, clr); }
    dev.context->ClearDepthStencilView(gbufferDepth.dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    dev.context->OMSetDepthStencilState(depthState, 0);
    size_t gbufferDrawCalls = 0;
    for (const auto& rr : resolutions) {
        if (!rr.ok) continue;
        auto cit = channels.find(static_cast<int>(rr.submeshIndex));
        if (cit == channels.end() || !cit->second.vb || !cit->second.ib) continue;
        auto spanIt = cit->second.rangeIndexSpan.find(rr.rangeIdx);
        if (spanIt == cit->second.rangeIndexSpan.end() || spanIt->second.second == 0) continue;
        CompiledShader& cs = gbufferShaders[rr.vsFileName];
        if (!cs.ok || !cs.vs || !cs.ps || !cs.inputLayout) continue;
        UINT stride = sizeof(GpuVertexReal), offset = 0;
        dev.context->IASetInputLayout(cs.inputLayout);
        dev.context->IASetVertexBuffers(0, 1, &cit->second.vb, &stride, &offset);
        dev.context->IASetIndexBuffer(cit->second.ib, DXGI_FORMAT_R32_UINT, 0);
        dev.context->VSSetShader(cs.vs, nullptr, 0);
        dev.context->VSSetConstantBuffers(0, 1, &cs.cbVs);
        dev.context->PSSetShader(cs.ps, nullptr, 0);
        dev.context->PSSetConstantBuffers(0, 1, &cs.cbPs);
        for (UINT slot : cs.allTexBindPoints) {
            ID3D11ShaderResourceView* srv = whitePlaceholderSrv;
            if (static_cast<int>(slot) == cs.normalMapSamplerReg) {
                auto it = materialNormalTex.find(rr.materialId);
                if (it != materialNormalTex.end() && it->second.srv) srv = it->second.srv;
            }
            dev.context->PSSetShaderResources(slot, 1, &srv);
        }
        for (UINT slot : cs.allSamplerBindPoints) dev.context->PSSetSamplers(slot, 1, &sampler);
        UINT startIndex = static_cast<UINT>(spanIt->second.first), count = static_cast<UINT>(spanIt->second.second);
        dev.context->DrawIndexed(count, startIndex, 0);
        ++gbufferDrawCalls;
    }
    printf("[pass A: G-buffer prepass] %zu real DrawIndexed calls into the 3 real off-screen targets + real DSV\n",
           gbufferDrawCalls);
    dev.context->OMSetRenderTargets(0, nullptr, nullptr);
    // Diagnostic readback (see printTargetStats's own comment): confirms
    // directly, not just by trace/assertion, that PASS A's own real output
    // is non-trivial even though the FINAL composited colour (PASS C) later
    // comes out flat black for this prop's drawable materials.
    printTargetStats(dev.device, dev.context, normalsRT.tex, kW, kH, "gbuffer Normals (oC0)");
    printTargetStats(dev.device, dev.context, lightingParamRT.tex, kW, kH, "gbuffer Lighting-parameter (oC2, encodes real Specular_Power)");
    return true;
}

bool tower_proto::towerPassLight(TowerSession& session) {
    auto& dev = session.dev;
    auto& shadersContainer = *session.shadersContainer_;
    auto& fillLog = session.fillLog;
    auto& kW = session.kW;
    auto& kH = session.kH;
    auto& invProj = session.invProj;
    auto& realLightRowFound = session.realLightRowFound;
    auto& realTodColor = session.realTodColor;
    auto& realTodIntensity = session.realTodIntensity;
    auto& realAmbientColor = session.realAmbientColor;
    auto& realAmbientIntensity = session.realAmbientIntensity;
    auto& realBackAmbientColor = session.realBackAmbientColor;
    auto& realBackAmbientIntensity = session.realBackAmbientIntensity;
    auto& realExposureMin = session.realExposureMin;
    auto& normalsRT = session.normalsRT;
    auto& lightingParamRT = session.lightingParamRT;
    auto& lightBufferRT = session.lightBufferRT;
    auto& noDepthState = session.noDepthState;
    auto& sampler = session.sampler;
    auto& blackPlaceholderSrv = session.blackPlaceholderSrv;
    // -----------------------------------------------------------------
    // PASS B: real directional light, ir_light_directional's own real T8
    // pass 0 - identical to prototype_lit_car.cpp's own PASS B, byte-for-
    // byte unchanged (same shader file, same real weather-row fill, same
    // CHOSEN light direction).
    // -----------------------------------------------------------------
    std::vector<uint8_t> lightFxo;
    if (!findEntry(shadersContainer, "ir_light_directional.fxo_pc", lightFxo)) {
        printf("FATAL: ir_light_directional.fxo_pc not found\n"); return false;
    }
    CompiledShader lightCs = compileOnePass(dev.device, lightFxo, "ir_light_directional.fxo_pc", "ir_light_directional", /*role=*/0);
    if (!lightCs.ok) { printf("FATAL: ir_light_directional pass 0 failed to compile: %s\n", lightCs.failReason.c_str()); return false; }
    {
        HRESULT hr = dev.device->CreateVertexShader(lightCs.vsCode->GetBufferPointer(), lightCs.vsCode->GetBufferSize(), nullptr, &lightCs.vs);
        if (SUCCEEDED(hr)) hr = dev.device->CreatePixelShader(lightCs.psCode->GetBufferPointer(), lightCs.psCode->GetBufferSize(), nullptr, &lightCs.ps);
        if (SUCCEEDED(hr)) hr = dev.device->CreateInputLayout(kQuadLayout, 2, lightCs.vsCode->GetBufferPointer(), lightCs.vsCode->GetBufferSize(), &lightCs.inputLayout);
        if (FAILED(hr)) { printf("FATAL: ir_light_directional GPU object creation failed: %s\n", hrToString(hr).c_str()); return false; }
    }
    printf("[light shader] ir_light_directional real pass %d: VS inputs=%s  PS declares IR_GBuffer_NormalsSampler@s%d "
           "IR_GBuffer_LightingSampler@s%d  real PS outputs=%u\n", lightCs.passIndexUsed,
           signatureToString(lightCs.vsInputs).c_str(), lightCs.normalsSamplerReg, lightCs.lightingSamplerReg,
           lightCs.psOutputCount);

    lightCs.vsCbufCpu.assign(lightCs.vsCbSize, 0);
    for (const auto& c : lightCs.vsCtab.constants) {
        if (c.registerSet != sr3d3d9bc::RegisterSet::Float4) continue;
        std::vector<std::array<float,4>> regs; std::string decision;
        if (containsCI(c.name, "inv_proj")) {
            for (int i = 0; i < 4; ++i) { std::array<float,4> r; packColumn(invProj, i, r.data()); regs.push_back(r); }
            decision = "REAL: exact inverse of this tool's own projection matrix (invert4x4, see top comment)";
        } else if (containsCI(c.name, "target_dimensions")) {
            regs.push_back({static_cast<float>(kW), static_cast<float>(kH), 0, 0});
            decision = "REAL for this tool's own chosen resolution";
        } else {
            for (uint16_t r = 0; r < c.registerCount; ++r) regs.push_back({0,0,0,0});
            decision = "PLACEHOLDER: zero (untraced)";
        }
        fillFloat4Registers(lightCs.vsCbufCpu, c.registerIndex, c.registerCount, regs);
        fillLog.push_back({"ir_light_directional VS " + c.name, decision});
    }
    lightCs.psCbufCpu.assign(lightCs.psCbSize, 0);
    const float kChosenLightDirView[3] = {0.35f, 0.55f, -0.55f};
    const float lightScale = realLightRowFound ? realExposureMin : 1.0f;
    const float realSunColorOut[4] = {
        realTodColor[0] * realTodIntensity * lightScale, realTodColor[1] * realTodIntensity * lightScale,
        realTodColor[2] * realTodIntensity * lightScale, 1.0f};
    const float realAmbientOut[4] = {
        realAmbientColor[0] * realAmbientIntensity * lightScale, realAmbientColor[1] * realAmbientIntensity * lightScale,
        realAmbientColor[2] * realAmbientIntensity * lightScale, 1.0f};
    const float realBackOut[4] = {
        realBackAmbientColor[0] * realBackAmbientIntensity * lightScale,
        realBackAmbientColor[1] * realBackAmbientIntensity * lightScale,
        realBackAmbientColor[2] * realBackAmbientIntensity * lightScale, 1.0f};
    for (const auto& c : lightCs.psCtab.constants) {
        if (c.registerSet != sr3d3d9bc::RegisterSet::Float4) continue;
        std::vector<std::array<float,4>> regs; std::string decision;
        if (containsCI(c.name, "ir_light_pos")) {
            float n = std::sqrt(kChosenLightDirView[0]*kChosenLightDirView[0]+kChosenLightDirView[1]*kChosenLightDirView[1]+kChosenLightDirView[2]*kChosenLightDirView[2]);
            regs.push_back({kChosenLightDirView[0]/n, kChosenLightDirView[1]/n, kChosenLightDirView[2]/n, 0});
            decision = "CHOSEN placeholder direction (weather_time_of_day.xtbl's real schema has no light-DIRECTION "
                       "field, only colour/ambient/exposure)";
        } else if (containsCI(c.name, "ir_light_color")) {
            regs.push_back({realSunColorOut[0], realSunColorOut[1], realSunColorOut[2], realSunColorOut[3]});
            decision = realLightRowFound
                ? "REAL: weather_time_of_day.xtbl noon/Overcast TOD_Light_Color*Intensity, CHOSEN scale = this "
                  "same row's real Exposure_Min"
                : "FALLBACK CHOSEN: real row lookup failed this run - see [env table] warning printed above";
        } else if (containsCI(c.name, "back_color")) {
            regs.push_back({realBackOut[0], realBackOut[1], realBackOut[2], realBackOut[3]});
            decision = realLightRowFound
                ? "REAL: weather_time_of_day.xtbl noon/Overcast Back_Ambient_Color*Intensity, CHOSEN scale = this "
                  "same row's real Exposure_Min"
                : "FALLBACK CHOSEN: real row lookup failed this run - see [env table] warning printed above";
        } else if (containsCI(c.name, "v_ambient_render")) {
            regs.push_back({realAmbientOut[0], realAmbientOut[1], realAmbientOut[2], realAmbientOut[3]});
            decision = realLightRowFound
                ? "REAL: weather_time_of_day.xtbl noon/Overcast Ambient_Color*Intensity, CHOSEN scale = this "
                  "same row's real Exposure_Min"
                : "FALLBACK CHOSEN: real row lookup failed this run - see [env table] warning printed above";
        } else {
            for (uint16_t r = 0; r < c.registerCount; ++r) regs.push_back({0,0,0,0});
            decision = "PLACEHOLDER: zero (untraced)";
        }
        fillFloat4Registers(lightCs.psCbufCpu, c.registerIndex, c.registerCount, regs);
        fillLog.push_back({"ir_light_directional PS " + c.name, decision});
    }
    { D3D11_BUFFER_DESC d{}; d.ByteWidth = static_cast<UINT>((lightCs.vsCbufCpu.size()+15)&~size_t(15));
      d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
      lightCs.vsCbufCpu.resize(d.ByteWidth, 0);
      D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = lightCs.vsCbufCpu.data();
      dev.device->CreateBuffer(&d, &sd, &lightCs.cbVs); }
    { D3D11_BUFFER_DESC d{}; d.ByteWidth = static_cast<UINT>((lightCs.psCbufCpu.size()+15)&~size_t(15));
      d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
      lightCs.psCbufCpu.resize(d.ByteWidth, 0);
      D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = lightCs.psCbufCpu.data();
      dev.device->CreateBuffer(&d, &sd, &lightCs.cbPs); }

    GpuVertexQuad quadVerts[4] = {
        {{-1, 1, 0, 1}, {0, 0, 0, 0}}, {{ 1, 1, 0, 1}, {1, 0, 0, 0}},
        {{-1,-1, 0, 1}, {0, 1, 0, 0}}, {{ 1,-1, 0, 1}, {1, 1, 0, 0}},
    };
    uint32_t quadIdx[6] = {0,1,2, 2,1,3};
    ID3D11Buffer* quadVb = nullptr; ID3D11Buffer* quadIb = nullptr;
    { D3D11_BUFFER_DESC vbDesc{}; vbDesc.ByteWidth = sizeof(quadVerts); vbDesc.Usage = D3D11_USAGE_IMMUTABLE; vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
      D3D11_SUBRESOURCE_DATA vbData{}; vbData.pSysMem = quadVerts; dev.device->CreateBuffer(&vbDesc, &vbData, &quadVb); }
    { D3D11_BUFFER_DESC ibDesc{}; ibDesc.ByteWidth = sizeof(quadIdx); ibDesc.Usage = D3D11_USAGE_IMMUTABLE; ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
      D3D11_SUBRESOURCE_DATA ibData{}; ibData.pSysMem = quadIdx; dev.device->CreateBuffer(&ibDesc, &ibData, &quadIb); }

    dev.context->OMSetRenderTargets(1, &lightBufferRT.rtv, nullptr);
    { const float clr[4] = {0,0,0,0}; dev.context->ClearRenderTargetView(lightBufferRT.rtv, clr); }
    dev.context->OMSetDepthStencilState(noDepthState, 0);
    UINT qStride = sizeof(GpuVertexQuad), qOffset = 0;
    dev.context->IASetInputLayout(lightCs.inputLayout);
    dev.context->IASetVertexBuffers(0, 1, &quadVb, &qStride, &qOffset);
    dev.context->IASetIndexBuffer(quadIb, DXGI_FORMAT_R32_UINT, 0);
    dev.context->VSSetShader(lightCs.vs, nullptr, 0);
    dev.context->VSSetConstantBuffers(0, 1, &lightCs.cbVs);
    dev.context->PSSetShader(lightCs.ps, nullptr, 0);
    dev.context->PSSetConstantBuffers(0, 1, &lightCs.cbPs);
    if (lightCs.normalsSamplerReg >= 0) dev.context->PSSetShaderResources(static_cast<UINT>(lightCs.normalsSamplerReg), 1, &normalsRT.srv);
    if (lightCs.lightingSamplerReg >= 0) dev.context->PSSetShaderResources(static_cast<UINT>(lightCs.lightingSamplerReg), 1, &lightingParamRT.srv);
    for (UINT slot : lightCs.allSamplerBindPoints) dev.context->PSSetSamplers(slot, 1, &sampler);
    for (UINT slot : lightCs.allTexBindPoints) {
        if (static_cast<int>(slot) == lightCs.normalsSamplerReg || static_cast<int>(slot) == lightCs.lightingSamplerReg) continue;
        dev.context->PSSetShaderResources(slot, 1, &blackPlaceholderSrv);
    }
    dev.context->DrawIndexed(6, 0, 0);
    printf("[pass B: directional light] 1 real DrawIndexed(6) fullscreen quad, ir_light_directional pass %d, real "
           "IR_GBuffer_NormalsSampler/IR_GBuffer_LightingSampler bound to pass A's own real output -> real light "
           "buffer\n", lightCs.passIndexUsed);
    dev.context->OMSetRenderTargets(0, nullptr, nullptr);
    printTargetStats(dev.device, dev.context, lightBufferRT.tex, kW, kH,
                      "light-accumulation buffer (real noon/Overcast weather row x real G-buffer)");
    return true;
}

bool tower_proto::towerPassMaterial(TowerSession& session) {
    auto& dev = session.dev;
    auto& kW = session.kW;
    auto& kH = session.kH;
    auto& resolutions = session.resolutions;
    auto& channels = session.channels;
    auto& litShaders = session.litShaders;
    auto& whitePlaceholderSrv = session.whitePlaceholderSrv;
    auto& lightBufferRT = session.lightBufferRT;
    auto& materialDiffuseTex = session.materialDiffuseTex;
    auto& materialSpecularTex = session.materialSpecularTex;
    auto& depthState = session.depthState;
    auto& sampler = session.sampler;
    // -----------------------------------------------------------------
    // PASS C: real material pass, role 6 of each shader, with
    // IR_LBufferSampler bound to PASS B's own real output (unchanged
    // mechanism) PLUS - the real upgrade this fork adds over
    // prototype_lit_car.cpp's own PASS C - Diffuse_MapSampler/
    // Specular_MapSampler bound to that material's REAL uploaded diffuse/
    // specular textures (step 7 above) wherever this pass's own real PS
    // CTAB declares them. Any other real texture slot (e.g.
    // IR_GBuffer_DSF_DataSampler - real, present, untraced this session)
    // gets the same neutral white placeholder every other unknown real
    // texture slot in this tool family already uses.
    // -----------------------------------------------------------------
    OffscreenDepth finalDepth;
    std::string fdErr;
    if (!finalDepth.create(dev.device, kW, kH, fdErr)) { printf("FATAL: final depth creation failed: %s\n", fdErr.c_str()); return false; }
    dev.context->OMSetRenderTargets(1, &dev.rtv, finalDepth.dsv);
    { const float clr[4] = {0.05f,0.05f,0.08f,1.0f}; dev.context->ClearRenderTargetView(dev.rtv, clr); }
    dev.context->ClearDepthStencilView(finalDepth.dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    dev.context->OMSetDepthStencilState(depthState, 0);
    size_t finalDrawCalls = 0, finalTriangles = 0;
    for (const auto& rr : resolutions) {
        if (!rr.ok) continue;
        auto cit = channels.find(static_cast<int>(rr.submeshIndex));
        if (cit == channels.end() || !cit->second.vb || !cit->second.ib) continue;
        auto spanIt = cit->second.rangeIndexSpan.find(rr.rangeIdx);
        if (spanIt == cit->second.rangeIndexSpan.end() || spanIt->second.second == 0) continue;
        CompiledShader& cs = litShaders[rr.vsFileName];
        if (!cs.ok || !cs.vs || !cs.ps || !cs.inputLayout) continue;
        UINT stride = sizeof(GpuVertexReal), offset = 0;
        dev.context->IASetInputLayout(cs.inputLayout);
        dev.context->IASetVertexBuffers(0, 1, &cit->second.vb, &stride, &offset);
        dev.context->IASetIndexBuffer(cit->second.ib, DXGI_FORMAT_R32_UINT, 0);
        dev.context->VSSetShader(cs.vs, nullptr, 0);
        dev.context->VSSetConstantBuffers(0, 1, &cs.cbVs);
        dev.context->PSSetShader(cs.ps, nullptr, 0);
        dev.context->PSSetConstantBuffers(0, 1, &cs.cbPs);
        for (UINT slot : cs.allTexBindPoints) {
            ID3D11ShaderResourceView* srv = whitePlaceholderSrv;
            if (static_cast<int>(slot) == cs.lBufferSamplerReg) {
                srv = lightBufferRT.srv; // THE real upgrade PASS B/C together make
            } else if (static_cast<int>(slot) == cs.diffuseMapSamplerReg) {
                auto it = materialDiffuseTex.find(rr.materialId);
                if (it != materialDiffuseTex.end() && it->second.srv) srv = it->second.srv;
            } else if (static_cast<int>(slot) == cs.specularMapSamplerReg) {
                auto it = materialSpecularTex.find(rr.materialId);
                if (it != materialSpecularTex.end() && it->second.srv) srv = it->second.srv;
            }
            dev.context->PSSetShaderResources(slot, 1, &srv);
        }
        for (UINT slot : cs.allSamplerBindPoints) dev.context->PSSetSamplers(slot, 1, &sampler);
        UINT startIndex = static_cast<UINT>(spanIt->second.first), count = static_cast<UINT>(spanIt->second.second);
        dev.context->DrawIndexed(count, startIndex, 0);
        ++finalDrawCalls; finalTriangles += count/3;
    }
    printf("[pass C: material pass] %zu real DrawIndexed calls (%zu triangles), IR_LBufferSampler bound to pass "
           "B's own real light-buffer output, Diffuse_MapSampler/Specular_MapSampler bound to each material's own "
           "real uploaded texture, for every range whose shader declares them\n", finalDrawCalls, finalTriangles);
    return true;
}

bool tower_proto::towerWriteOutput(TowerSession& session) {
    auto& dev = session.dev;
    auto& kW = session.kW;
    auto& kH = session.kH;
    auto& outPath = session.outPath;
    // -----------------------------------------------------------------
    // 13. Read back + PNG + real pixel statistics - identical mechanism
    //     to prototype_lit_car.cpp's own step 9.
    // -----------------------------------------------------------------
    std::vector<uint8_t> rgba; std::string rbErr;
    if (!dev.readBack(rgba, rbErr)) { printf("FATAL: readBack failed: %s\n", rbErr.c_str()); return false; }
    uint64_t sum[4] = {0,0,0,0}; uint8_t minV[4] = {255,255,255,255}, maxV[4] = {0,0,0,0};
    for (size_t i = 0; i < rgba.size(); i += 4)
        for (int c = 0; c < 4; ++c) { uint8_t v = rgba[i+c]; sum[c]+=v; if (v<minV[c]) minV[c]=v; if (v>maxV[c]) maxV[c]=v; }
    size_t pixelCount = rgba.size()/4;
    printf("\n=== Real RGBA8 pixel statistics (%ux%u, %zu pixels) ===\n", kW, kH, pixelCount);
    const char* chan[4] = {"R","G","B","A"};
    for (int c = 0; c < 4; ++c)
        printf("  %s: min=%u max=%u mean=%.3f\n", chan[c], minV[c], maxV[c], pixelCount ? static_cast<double>(sum[c])/pixelCount : 0.0);
    // Real, honest geometry-silhouette count: the CHOSEN clear colour
    // (0.05,0.05,0.08,1.0) rounds to (13,13,20,255) in 8-bit sRGB-free
    // UNORM - any pixel differing from that is real rasterised geometry
    // (drawable-range silhouette), not background. Reported plainly so
    // this task's own honest "final colour comes out black" finding (see
    // top comment) is a measured number, not just an eyeballed PNG.
    size_t geometryPixels = 0;
    for (size_t i = 0; i < rgba.size(); i += 4) {
        if (rgba[i] != 13 || rgba[i+1] != 13 || rgba[i+2] != 20) ++geometryPixels;
    }
    printf("  geometry silhouette (differs from the (13,13,20) clear colour): %zu pixels (%.2f%%)\n", geometryPixels,
           pixelCount ? 100.0 * geometryPixels / pixelCount : 0.0);

    std::string pngErr;
    if (!sr3render::writePng(outPath, kW, kH, rgba, pngErr)) { printf("FATAL: writePng failed: %s\n", pngErr.c_str()); return false; }
    printf("\n[png] wrote %s\n", outPath.c_str());
    printf("\n=== DONE ===\n");
    return true;
}

