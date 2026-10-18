// Standalone prototype, forked from
// tools/prototype_real_shader_draw_multishader_paintcompose.cpp (untouched
// by this file - same "fork, don't modify" discipline every prototype in
// this family uses). Reuses that file's own real container-loading,
// shaderHash/array-B-C material extraction, T8 pairing, CTAB-driven
// cbuffer-fill, and PNG/stat machinery almost verbatim; see that file's own
// top comment for the full provenance of those pieces.
//
// NEW IN THIS FORK (orchestrator task, 2026-09-29): a real 3-pass deferred
// pipeline - G-buffer prepass, one real directional light, then the real
// material (paint) pass reading the light result - instead of the white-
// placeholder IR_LBufferSampler bind paintcompose used. See this file's own
// SubagentHandback report for exactly what is REAL vs. CHOSEN vs. OPEN;
// the short version, so this header is self-contained if read cold:
//
//   REAL, independently verified THIS session directly against the actual
//   CTAB bytes/HLSL (not trusted from any prior paraphrase):
//     - All 6 real shaders car_4dr_genki_0 group 0 uses share the IDENTICAL
//       real T8 role table: role 4 -> pass index 0 (roleIndexByte(4)==0),
//       role 6 -> pass index 2 (roleIndexByte(6)==2). Read directly via
//       sr3fxo::WrapperHeader::roleIndexByte()/rolePresent(), not assumed
//       from ir_sr3carpaint_gr alone.
//     - Pass 0 (role 4)'s PS is genuinely cheap (1-3 constants, no paint
//       constants) and writes 3 outputs (oC0/oC1/oC2 -> SV_Target0-2).
//     - Pass 2 (role 6)'s PS declares IR_LBufferSampler at register s12 in
//       ALL 6 shaders (verified directly, not just ir_sr3carpaint_gr).
//     - The oC0/oC1/oC2 -> Normals/Depth/Lighting mapping used below was
//       WORKED OUT from the real translated HLSL of ir_sr3carpaint_gr's
//       pass 0 PS (the write side) and ir_light_directional's pass 0 PS
//       (the read side), not guessed:
//         oC0 -> IR_GBuffer_NormalsSampler (Normals): CONFIRMED round-trip
//           - write: oC0.xy = tangentSpaceNormal.xy * 0.5 + 0.5 (encode to
//             [0,1]); read: sampled.xy * 2 - 1 (decode back to [-1,1]),
//             exact inverse of the write's own scale+bias.
//         oC2 -> IR_GBuffer_LightingSampler (a packed specular/roughness
//           PARAMETER buffer, not a final light colour - matches this
//           session's own earlier bytecode finding, HANDOFF Sec9.107 Part
//           3): CONFIRMED round-trip - write: oC2.x = Specular_Power/512
//           (floored at 1/256); read: sampled.x * 512 used directly as a
//           POW() exponent - an exact numeric round trip through the
//           encode/decode, the strongest evidence found this pass.
//         oC1 -> IR_GBuffer_DepthSampler: HIGH CONFIDENCE, NOT round-trip
//           confirmed (the read side was never traced: ir_light_directional
//           PASS 0, the no-shadow variant this tool uses, does not declare
//           a depth sampler at all and never reads oC1 back). Assigned by
//           elimination (the only one of the 3 named G-buffer samplers left
//           unassigned) PLUS real supporting math on the write side: oC1.x
//           = saturate(viewSpaceZ / 32768) - viewSpaceZ itself independently
//           traced through the VS to a real dot-product against
//           IR_World2View's own rows, a genuine linear-depth-shaped
//           computation. Flagged honestly as the weakest-evidenced of the
//           3, not claimed at the same confidence as the other two.
//     - Because ir_light_directional's PASS 0 PS never reads
//       IR_GBuffer_DepthSampler at all, this tool's G-buffer prepass still
//       WRITES oC1/Depth (for pipeline-shape completeness, all 3 targets +
//       1 real depth-stencil surface, matching spec-render-pipeline.md
//       Sec23.5's disassembly-confirmed "3 colour RTs + 1 DSV" bind shape)
//       but the light pass below never SAMPLES it - genuinely unneeded for
//       this specific real light-shader pass, not an oversight.
//     - The real fullscreen-quad VS (ir_light_directional pass 0's own VS)
//       declares POSITION0+TEXCOORD0 and passes POSITION0 straight through
//       to SV_Position with w=1 (no projTM multiply) - confirmed by reading
//       its own translated HLSL body - i.e. it expects pre-transformed NDC
//       clip-space coordinates directly, which is what this tool feeds it.
//
//   UPDATED THIS PASS (orchestrator follow-up task, 2026-09-29): IR_Light_
//   Color/back_color/V_ambient_render are now REAL, read at runtime from
//   the actual weather.xtbl + weather_time_of_day.xtbl entries in
//   misc_tables.vpp_pc via the ALREADY-EXISTING sr3tables_environment
//   reader (include/sr3tables_environment/, src/tables_environment.cpp -
//   not a new reader, reused unchanged) - see step 1b in main() for the
//   full load/selection code. Row used: Weather_Time_Segment Name="noon"
//   (real Start_Time=1430 HHMM) x Weather_Stages/Stage Stage_Name=
//   "Overcast" (real weather.xtbl Chance=20.0/100) - picked because it is
//   the ONLY stage cell in the "noon" segment with TOD_Light_Color,
//   Ambient_Color, Back_Ambient_Color AND Exposure all real-present, and
//   because "Clear Skies" (weather.xtbl's own most-common stage,
//   Chance=60.0) turns out to have NO Weather_Stages/Stage row in ANY of
//   weather_time_of_day.xtbl's 4 real segments at all - a genuine real-
//   data finding, not a gap in this tool (see step 1b's own comment for
//   detail). The 3 real colours are each multiplied by their own real
//   per-row Intensity and by this SAME row's real Exposure_Min (0.12) as
//   a table-sourced (not arbitrary) LDR scale - see the PS constant-fill
//   site's own comment for exactly why. IR_Light_Pos (DIRECTION) is NOT
//   real: weather_time_of_day.xtbl's schema has no direction field at
//   all, only colour/ambient/exposure - stayed CHOSEN, honestly, rather
//   than forcing a fit that is not in the real data.
//
//   CHOSEN by me (this pass), labelled plainly, not claimed as real:
//     - G-buffer/light-buffer pixel format (DXGI_FORMAT_R8G8B8A8_UNORM) and
//       resolution (800x600, matching every other tool in this family) -
//       spec-render-pipeline.md Sec23.10 states the real format was never
//       confirmed on the disassembly side.
//     - The camera (an explicit LH perspective projection + orbit view this
//       tool builds itself, so a real, invertible projection-only matrix
//       exists to feed IR_Light_Inv_Proj_TM - paintcompose's own
//       buildOrbitViewProjection() only exposes an already-fused
//       view*projection product, not a separately invertible projection).
//     - IR_Light_Pos (DIRECTION ONLY, as of this pass - see the "UPDATED
//       THIS PASS" note above for why colour/ambient moved to REAL):
//       weather_time_of_day.xtbl's real schema (spec-tables-environment.md
//       3.3) has TOD_Light_Color/Ambient_Color/Back_Ambient_Color but no
//       light-DIRECTION field anywhere, so a real per-pixel sun direction
//       could not be sourced from this table honestly; the same CHOSEN
//       view-space vector (found by direct HLSL trace of the N.L sign
//       convention, see the PS constant-fill site's own comment) is kept.
//       NOT presented as real.
//     - Fullscreen-light-volume quad geometry: the brief's own stated
//       standard, safe choice for a directional light, not flagged further.
//
//   OPEN, stated honestly, not silently resolved either way:
//     - Whether the 3 G-buffer targets' real engine PIXEL FORMAT is exactly
//       R8G8B8A8_UNORM (Team A's own Sec23.10 - never confirmed either
//       side).
//     - oC1's exact real semantic (see above - HIGH CONFIDENCE only, not
//       round-trip confirmed like oC0/oC2).
//     - Every OTHER PS constant this tool still fills with the real
//       confirmed-zero/0.1-floor template (spec-render-pipeline.md
//       Sec20.12.11) rather than a per-material authored value, exactly as
//       paintcompose already documents - unchanged by this fork.
//
// Standalone: not wired into CMakeLists.txt, build_verify/ untouched.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>

#include "sr3d3d9bc/ctab.h"
#include "sr3d3d9bc/disassembler.h"
#include "sr3d3d9bc/errors.h"
#include "sr3d3d9bc/hlsl_translator.h"
#include "sr3fxo/wrapper_header.h"
#include "sr3geometry/geometry_block.h"
#include "sr3geometry/material_binding.h"
#include "sr3mesh/mesh_block.h"
#include "sr3render/mesh_renderer.h" // buildOrbitViewProjection/buildWorldMatrix/multiplyMatrix4x4
#include "sr3render/png_writer.h"
#include "sr3render/texture_upload.h"
#include "sr3tables_environment/tables.h" // NEW in this fork: real weather_time_of_day.xtbl row for the light pass
#include "sr3texture/texture_pair.h"
#include "sr3vehicle/vehicle.h"
#include "vpp/container.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

#include "prototype_lit_car_support.h"

using namespace litcar;


// ===========================================================================
// main()
// ===========================================================================
int main() {
    const std::string kCacheDir = "D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache";
    std::vector<ConstantFillLog> fillLog;
    const uint32_t kW = 800, kH = 600;
    const DXGI_FORMAT kGBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM; // CHOSEN, not confirmed real - see top comment

    printf("=== Lit-car prototype: real G-buffer prepass -> real directional light -> real material pass ===\n");
    printf("(forked from prototype_real_shader_draw_multishader_paintcompose.cpp - see this file's own top comment)\n\n");

    // -----------------------------------------------------------------
    // 1. Real stem -> .fxo_pc table + vehicle/mesh/material data (same
    //    as paintcompose's own steps 1-2).
    // -----------------------------------------------------------------
    std::vector<uint8_t> shadersArchive = readFile(kCacheDir + "/shaders.vpp_pc");
    if (shadersArchive.empty()) { printf("FATAL: could not read shaders.vpp_pc\n"); return 1; }
    vpp::Container shadersContainer{vpp::ByteView(shadersArchive.data(), shadersArchive.size())};
    std::map<std::string, std::vector<std::string>> stemToFiles;
    collectFxoStems(shadersContainer, stemToFiles);
    std::map<uint32_t, std::string> stemCrcToStem;
    for (const auto& kv : stemToFiles) {
        uint32_t crc = sr3fxo::crc32Raw(reinterpret_cast<const uint8_t*>(kv.first.data()), kv.first.size());
        stemCrcToStem[crc] = kv.first;
    }
    printf("[shaderHash join] real .fxo_pc distinct stems: %zu\n\n", stemToFiles.size());

    std::vector<uint8_t> dlc1 = readFile(kCacheDir + "/dlc1.vpp_pc");
    if (dlc1.empty()) { printf("FATAL: could not read dlc1.vpp_pc\n"); return 1; }
    vpp::Container dlc1Container{vpp::ByteView(dlc1.data(), dlc1.size())};
    std::vector<uint8_t> vehStr2;
    if (!findEntry(dlc1Container, "car_4dr_genki_0.str2_pc", vehStr2)) { printf("FATAL: vehicle str2 not found\n"); return 1; }
    vpp::Container vehContainer{vpp::ByteView(vehStr2.data(), vehStr2.size())};
    std::vector<uint8_t> ccarBytes, gcarBytes, cpegBytes, gpegBytes;
    findEntry(vehContainer, "car_4dr_genki_0.ccar_pc", ccarBytes);
    findEntry(vehContainer, "car_4dr_genki_0.gcar_pc", gcarBytes);
    findEntry(vehContainer, "car_4dr_genki_0.cpeg_pc", cpegBytes);
    findEntry(vehContainer, "car_4dr_genki_0.gpeg_pc", gpegBytes);
    if (ccarBytes.empty() || gcarBytes.empty()) { printf("FATAL: ccar/gcar not found\n"); return 1; }

    // -----------------------------------------------------------------
    // 1b. REAL environment/weather lighting row (NEW in this fork,
    //     orchestrator task 2026-09-29). Loads the actual weather.xtbl +
    //     weather_time_of_day.xtbl entries from misc_tables.vpp_pc via
    //     the already-existing sr3tables_environment reader
    //     (include/sr3tables_environment/, src/tables_environment.cpp -
    //     built on sr3xtbl, from spec-tables-environment.md alone; not a
    //     new reader, reused unchanged) and picks ONE real row for this
    //     pass's directional light:
    //       Weather_Time_Segment Name="noon" (real Start_Time=1430 HHMM -
    //       the table's own name for its midday segment; the literal time
    //       is 2:30pm, not 12:00 - stated plainly, not smoothed over)
    //       x Weather_Stages/Stage Stage_Name="Overcast" (a real, ordinary
    //       weather.xtbl stage, real base Chance=20.0/100).
    //     "Clear Skies" - weather.xtbl's own MOST common real stage,
    //     Chance=60.0 - was deliberately NOT used: independently confirmed
    //     this pass that weather_time_of_day.xtbl's real shipped data has
    //     NO Weather_Stages/Stage row named "Clear Skies" in ANY of its 4
    //     real Weather_Time_Segment rows (sunrise/noon/sunset/night) - a
    //     genuine real-data finding, not an oversight of this tool: the
    //     table simply never authors a per-time-of-day override for its
    //     own default weather, consistent with spec 3.2's documented "a
    //     cell never matched by name stays zeroed, all presence bits
    //     clear" behaviour. Among the 4 real stage cells the "noon"
    //     segment DOES carry (Overcast/Light Rain/Heavy Rain/Post-Storm),
    //     "Overcast" is the only one where TOD_Light_Color, Ambient_Color,
    //     Back_Ambient_Color AND the Exposure block are ALL real-present
    //     (Post-Storm - the other high-Chance stage, 50.0 - has both
    //     Ambient_Color and Back_Ambient_Color absent in every segment) -
    //     the most fully-populated, defensible real row available, not
    //     cherry-picked for its numeric values.
    // -----------------------------------------------------------------
    bool realLightRowFound = false;
    float realTodColor[3] = {0, 0, 0}, realTodIntensity = 0.0f;
    float realAmbientColor[3] = {0, 0, 0}, realAmbientIntensity = 0.0f;
    float realBackAmbientColor[3] = {0, 0, 0}, realBackAmbientIntensity = 0.0f;
    float realExposureMin = 0.0f;
    {
        std::vector<uint8_t> miscTables = readFile(kCacheDir + "/misc_tables.vpp_pc");
        std::vector<uint8_t> weatherBytes, todBytes;
        if (!miscTables.empty()) {
            vpp::Container miscContainer{vpp::ByteView(miscTables.data(), miscTables.size())};
            findEntry(miscContainer, "weather.xtbl", weatherBytes);          // located but not otherwise used
            findEntry(miscContainer, "weather_time_of_day.xtbl", todBytes);  // below (kept for parity with the
        }                                                                     // validation harness's own real find)
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

    sr3vehicle::Vehicle veh = sr3vehicle::Vehicle::parse(vpp::ByteView(ccarBytes.data(), ccarBytes.size()));
    vpp::ByteView ccarView(ccarBytes.data(), ccarBytes.size());
    sr3geometry::GeometryBlock geo = sr3geometry::GeometryBlock::parseAt(ccarView, veh.meshRegionOffset());
    if (!geo.hasMeshSubBlock()) { printf("FATAL: no Mesh sub-block\n"); return 1; }
    sr3mesh::MeshBlock mesh = sr3mesh::MeshBlock::parse(ccarView, geo.meshSubBlockOffset(),
                                                        vpp::ByteView(gcarBytes.data(), gcarBytes.size()));
    printf("[mesh] channels=%zu\n", mesh.channels().size());

    sr3geometry::MaterialBindings bindings =
        sr3geometry::MaterialBindings::parse(ccarView, geo.offset(), geo.meshSubBlockOffset() + mesh.cLength());
    if (!bindings.located()) { printf("FATAL: material bindings not located\n"); return 1; }

    MaterialShaderHashResult hashResult = extractMaterialShaderHashes(
        ccarView, geo.offset(), geo.meshSubBlockOffset(), mesh.cLength(), bindings.materialCount());
    printf("[shaderHash] extracted %zu/%u material shaderHash values, complete=%d\n", hashResult.hashes.size(),
           bindings.materialCount(), hashResult.complete);

    MaterialArrayBCResult arrayBC = extractMaterialArrayBC(
        ccarView, geo.offset(), geo.meshSubBlockOffset(), mesh.cLength(), bindings.materialCount());
    printf("[arrayBC] extracted %zu/%u material array-B/C tables, complete=%d\n", arrayBC.perMaterial.size(),
           bindings.materialCount(), arrayBC.complete);
    auto findMaterialConstant = [&](uint32_t materialId, uint32_t nameHash) -> const std::array<float, 4>* {
        if (materialId >= arrayBC.perMaterial.size()) return nullptr;
        for (const auto& mc : arrayBC.perMaterial[materialId]) if (mc.nameHash == nameHash) return &mc.value;
        return nullptr;
    };

    if (!mesh.drawGroupsLocated() || mesh.drawGroups().empty()) { printf("FATAL: draw groups not located\n"); return 1; }
    const auto& group0 = mesh.drawGroups()[0];
    printf("\n[group0] %zu draw ranges total\n\n", group0.size());

    // -----------------------------------------------------------------
    // 2. Resolve each range's real shader stem + VS file (identical to
    //    paintcompose's own step 3).
    // -----------------------------------------------------------------
    struct RangeResolution {
        size_t rangeIdx = 0; uint32_t materialId = 0, submeshIndex = 0, shaderHash = 0;
        bool ok = false; std::string reason, stem, vsFileName;
    };
    std::vector<RangeResolution> resolutions;
    std::map<std::string, std::vector<size_t>> byVsFile;
    for (size_t ri = 0; ri < group0.size(); ++ri) {
        const auto& range = group0[ri];
        RangeResolution rr; rr.rangeIdx = ri; rr.materialId = range.materialId; rr.submeshIndex = range.submeshIndex;
        if (range.materialId >= hashResult.hashes.size()) { rr.reason = "materialId out of range"; resolutions.push_back(rr); continue; }
        rr.shaderHash = hashResult.hashes[range.materialId];
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
    printf("[resolution] %zu/%zu ranges resolved, %zu distinct real VS files\n\n", resolvedCount, resolutions.size(),
           byVsFile.size());

    // -----------------------------------------------------------------
    // 3. Compile BOTH real passes (role 4 G-buffer-write, role 6 forward-
    //    lit) for each distinct resolved shader file. Role numbers read
    //    from each file's OWN real T8 role table (sr3fxo::WrapperHeader::
    //    roleIndexByte()), not hardcoded to ir_sr3carpaint_gr's pass 0/2.
    // -----------------------------------------------------------------
    RenderDevice dev;
    std::string devErr;
    if (!dev.initialise(kW, kH, devErr)) { printf("FATAL device: %s\n", devErr.c_str()); return 1; }
    printf("[device] kind=%s\n\n", dev.isWarp ? "WARP (software rasterizer)" : "Hardware");

    std::map<std::string, CompiledShader> gbufferShaders; // role 4
    std::map<std::string, CompiledShader> litShaders;     // role 6
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
        if (gb.ok) printf("    real PS outputs written: %u (SV_Target0..%u)\n", gb.psOutputCount,
                           gb.psOutputCount ? gb.psOutputCount - 1 : 0);
        printf("  role6 (forward lit, real pass index %d): %s%s\n", lit.passIndexUsed, lit.ok ? "OK" : "FAILED: ",
               lit.ok ? "" : lit.failReason.c_str());
        if (lit.ok) printf("    real IR_LBufferSampler register: s%d\n", lit.lBufferSamplerReg);
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

    // A range is drawable in this new pipeline only if BOTH its real
    // role-4 and role-6 passes compiled - reported honestly either way.
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
    printf("[final] %zu/%zu real draw ranges drawable through the new pipeline\n\n", drawableRanges, resolutions.size());
    if (drawableRanges == 0) { printf("FATAL: no drawable range\n"); return 1; }

    // -----------------------------------------------------------------
    // 4. Real channel decode (identical to paintcompose's own step 5,
    //    union of BOTH role-4/role-6 variants' input needs per channel).
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
    struct ChannelData {
        std::vector<sr3mesh::Vertex> verts;
        std::vector<GpuVertexReal> gpuVerts;
        std::vector<uint32_t> gpuIndices;
        std::map<size_t, std::pair<size_t, size_t>> rangeIndexSpan;
        ID3D11Buffer* vb = nullptr; ID3D11Buffer* ib = nullptr;
        sr3mesh::LayoutInfo layoutInfo;
    };
    std::map<int, ChannelData> channels;
    for (int ch : neededChannels) {
        if (ch < 0 || static_cast<size_t>(ch) >= mesh.channels().size()) { printf("  channel %d out of range\n", ch); continue; }
        ChannelData cd;
        const auto& chanMeta = mesh.channels()[static_cast<size_t>(ch)];
        cd.layoutInfo = sr3mesh::layoutInfoFor(chanMeta.layoutCode);
        try { cd.verts = mesh.decodeChannel(static_cast<size_t>(ch)); }
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
        std::vector<uint32_t> tri = mesh.triangleListForRange(group0[rr.rangeIdx]);
        size_t start = cit->second.gpuIndices.size();
        cit->second.gpuIndices.insert(cit->second.gpuIndices.end(), tri.begin(), tri.end());
        cit->second.rangeIndexSpan[rr.rangeIdx] = {start, tri.size()};
    }
    printf("\n");

    // -----------------------------------------------------------------
    // 5. Real object-space bounds, camera (view + a genuine standalone
    //    projection this time, so IR_Light_Inv_Proj_TM has something real
    //    to invert), part transforms (identical philosophy to
    //    paintcompose, extended with buildPerspectiveLH/invert4x4 - both
    //    NEW, CHOSEN camera parameters, see top comment).
    // -----------------------------------------------------------------
    float boundsMin[3] = {1e30f,1e30f,1e30f}, boundsMax[3] = {-1e30f,-1e30f,-1e30f};
    for (auto& kv : channels) {
        for (const auto& v : kv.second.verts) {
            float finalPos[3];
            if (v.rigidPartIndex < veh.parts().size()) transformPointRowVector(v.position.data(), veh.parts()[v.rigidPartIndex].transform.data(), finalPos);
            else { finalPos[0]=v.position[0]; finalPos[1]=v.position[1]; finalPos[2]=v.position[2]; }
            for (int c = 0; c < 3; ++c) { if (finalPos[c] < boundsMin[c]) boundsMin[c] = finalPos[c]; if (finalPos[c] > boundsMax[c]) boundsMax[c] = finalPos[c]; }
        }
    }
    printf("[bounds] min(%.3f %.3f %.3f) max(%.3f %.3f %.3f)\n", boundsMin[0], boundsMin[1], boundsMin[2],
           boundsMax[0], boundsMax[1], boundsMax[2]);

    const float yaw = 0.6f, pitch = 0.35f, distanceScale = 1.8f;
    float viewOnly[16];
    computeOrbitViewMatrix(boundsMin, boundsMax, yaw, pitch, distanceScale, viewOnly);
    float extent = 0.0f;
    for (int i = 0; i < 3; ++i) { float span = boundsMax[i]-boundsMin[i]; if (span > extent) extent = span; }
    if (extent <= 0.0f) extent = 1.0f;
    float nearZ = extent * 0.05f, farZ = extent * distanceScale * 4.0f; // CHOSEN, see top comment
    float projOnly[16];
    buildPerspectiveLH(0.785398f /* 45 deg, CHOSEN */, static_cast<float>(kW) / kH, nearZ, farZ, projOnly);
    float viewProj[16];
    sr3render::multiplyMatrix4x4(viewOnly, projOnly, viewProj); // real linked project function, row-vector v*view*proj
    float invProj[16];
    bool projInvertible = invert4x4(projOnly, invProj);
    printf("[camera] CHOSEN fovY=45deg near=%.3f far=%.3f (not real game camera data - see top comment). "
           "projection invertible=%d\n", nearZ, farZ, projInvertible);
    float worldIdentity[16];
    sr3render::buildWorldMatrix(0.0f, 0.0f, 0.0f, 0.0f, 1.0f, worldIdentity);

    // -----------------------------------------------------------------
    // 6. Real per-shader cbuffer fill - identical CTAB-name-driven logic
    //    to paintcompose's own step 8/8b, run for BOTH the role-4 and
    //    role-6 compiled variant of every drawable shader.
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
                decision = "PLACEHOLDER: identity world transform";
            } else if (containsCI(c.name, "bone")) {
                const size_t maxElems = c.registerCount / 3;
                for (size_t e = 0; e < maxElems; ++e) {
                    float packed[3][4];
                    if (e < veh.parts().size()) for (int i = 0; i < 3; ++i) packColumn(veh.parts()[e].transform.data(), i, packed[i]);
                    else for (int i = 0; i < 3; ++i) packColumn(worldIdentity, i, packed[i]);
                    for (int i = 0; i < 3; ++i) { std::array<float,4> r; std::memcpy(r.data(), packed[i], 16); regs.push_back(r); }
                }
                decision = "REAL: sr3vehicle::VehiclePart::transform per real part";
            } else if (cs.stem == "ir_sr3carpaint_gr" && containsCI(c.name, "grime_tiling")) {
                for (uint16_t r = 0; r < c.registerCount; ++r) regs.push_back({3.0f,3.0f,3.0f,3.0f});
                decision = "REAL: checked identical (3.0) across this vehicle's ir_sr3carpaint_gr materials";
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
                // PLACEHOLDER, deliberately reintroduced - NOT the literal
                // real-confirmed-zero template used for every other name.
                // Found this pass, by reading ir_sr3carpaint_gr pass 2's
                // OWN real translated PS body directly (not paraphrase):
                // its FINAL instruction is unconditionally
                // "output.oC0.xyzw = r1.xyzw * c[37].xyzw" where c[37] IS
                // Tint_color - i.e. with the real-confirmed-zero-when-
                // unauthored regime taken literally, EVERY pixel this pass
                // draws is EXACTLY (0,0,0,0), for EVERY material,
                // regardless of Base_Paint_Color or the real light buffer
                // this task exists to test - independently reproduced: the
                // first run of this exact tool, before this override, gave
                // pixel stats statistically indistinguishable from pure
                // background. Team A's own spec-render-pipeline.md
                // Sec20.12.11 confirms Tint_color has ZERO real occurrences
                // anywhere in the executable's own memory - so the shipped
                // game evidently gets a real, non-zero Tint_color from some
                // OTHER mechanism this project has not yet located (most
                // plausibly the same still-OPEN "material-buffer GPU-
                // upload call site", Sec20.12.6/.10). A literal (1,1,1,1)
                // multiplicative-identity placeholder is used here so the
                // rest of this pass's real math (Base_Paint_Color, the real
                // light buffer) can actually reach the output pixel and be
                // visually checked - exactly the same honestly-labelled
                // move paintcompose's own FIRST iteration made for the
                // identical reason, before Sec20.12.11 was published.
                for (uint16_t r = 0; r < c.registerCount; ++r) regs.push_back({1.0f,1.0f,1.0f,1.0f});
                decision = "PLACEHOLDER (deliberately reintroduced): real Tint_color=0 makes this pass's own final "
                           "instruction (oC0 = r1*Tint_color) exactly zero for every pixel, defeating this task's own "
                           "test - see top comment / this decision's own inline comment for the exact traced line";
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

    // Real per-material override for BOTH role-4 (Specular_Power) and
    // role-6 (Base_Paint_Color and siblings) - same array-B/C mechanism,
    // applied to whichever named constants each pass's own real CTAB
    // actually declares.
    auto buildPerMaterialOverrides = [&](CompiledShader& cs, const std::vector<size_t>& rangeIdxs) {
        std::set<uint32_t> materialIdsUsed;
        for (size_t idx : rangeIdxs) materialIdsUsed.insert(resolutions[idx].materialId);
        for (uint32_t materialId : materialIdsUsed) {
            std::vector<uint8_t> overridden = cs.psCbufCpu;
            size_t realMatches = 0;
            for (const auto& c : cs.psCtab.constants) {
                if (c.registerSet != sr3d3d9bc::RegisterSet::Float4) continue;
                uint32_t h = sr3fxo::hashLowerName(c.name);
                const std::array<float,4>* real = findMaterialConstant(materialId, h);
                if (!real) continue;
                std::vector<std::array<float,4>> regs; regs.push_back(*real);
                for (uint16_t r = 1; r < c.registerCount; ++r) regs.push_back({0,0,0,0});
                fillFloat4Registers(overridden, c.registerIndex, c.registerCount, regs);
                ++realMatches;
            }
            D3D11_BUFFER_DESC d{}; d.ByteWidth = static_cast<UINT>(overridden.size());
            d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = overridden.data();
            ID3D11Buffer* buf = nullptr;
            dev.device->CreateBuffer(&d, &sd, &buf);
            cs.perMaterialCbPs[materialId] = buf;
            printf("[material cbuffer] '%s' role%d materialId=%u: %zu/%zu PS constants matched a real per-material value\n",
                   cs.vsFileName.c_str(), cs.roleUsed, materialId, realMatches, cs.psCtab.constants.size());
        }
    };
    for (auto& kv : byVsFile) {
        if (gbufferShaders.count(kv.first) && gbufferShaders[kv.first].ok) buildPerMaterialOverrides(gbufferShaders[kv.first], kv.second);
        if (litShaders.count(kv.first) && litShaders[kv.first].ok) buildPerMaterialOverrides(litShaders[kv.first], kv.second);
    }

    // Real cbuffer GPU objects for both variants.
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

    printf("\n=== Constant-buffer fill decisions (real vs. placeholder, by real CTAB name) ===\n");
    for (const auto& l : fillLog) printf("  %-56s : %s\n", l.constName.c_str(), l.decision.c_str());

    // -----------------------------------------------------------------
    // 7. Per-channel GPU vertex/index buffers (identical to paintcompose).
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

    // -----------------------------------------------------------------
    // 8. Real off-screen G-buffer targets (3 colour + 1 depth) and the
    //    light-accumulation buffer. Format/resolution CHOSEN, see top
    //    comment. Common states.
    // -----------------------------------------------------------------
    OffscreenTarget normalsRT, depthParamRT, lightingParamRT, lightBufferRT;
    OffscreenDepth gbufferDepth;
    std::string tErr;
    bool targetsOk = normalsRT.create(dev.device, kW, kH, kGBufferFormat, tErr);
    if (targetsOk) targetsOk = depthParamRT.create(dev.device, kW, kH, kGBufferFormat, tErr);
    if (targetsOk) targetsOk = lightingParamRT.create(dev.device, kW, kH, kGBufferFormat, tErr);
    if (targetsOk) targetsOk = lightBufferRT.create(dev.device, kW, kH, kGBufferFormat, tErr);
    if (targetsOk) targetsOk = gbufferDepth.create(dev.device, kW, kH, tErr);
    if (!targetsOk) { printf("FATAL: off-screen target creation failed: %s\n", tErr.c_str()); return 1; }
    printf("\n[gbuffer] created 3 real off-screen colour targets (Normals/Depth/Lighting, CHOSEN format %s, %ux%u) "
           "+ 1 real depth-stencil surface + 1 real light-accumulation target - see top comment for the oC0/1/2 "
           "mapping evidence.\n", "DXGI_FORMAT_R8G8B8A8_UNORM", kW, kH);

    ID3D11DepthStencilState* depthState = nullptr;
    { D3D11_DEPTH_STENCIL_DESC dsDesc{}; dsDesc.DepthEnable = TRUE; dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
      dsDesc.DepthFunc = D3D11_COMPARISON_LESS; dev.device->CreateDepthStencilState(&dsDesc, &depthState); }
    ID3D11DepthStencilState* noDepthState = nullptr;
    { D3D11_DEPTH_STENCIL_DESC dsDesc{}; dsDesc.DepthEnable = FALSE; dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
      dev.device->CreateDepthStencilState(&dsDesc, &noDepthState); }
    ID3D11RasterizerState* rasterState = nullptr;
    { D3D11_RASTERIZER_DESC rsDesc{}; rsDesc.FillMode = D3D11_FILL_SOLID; rsDesc.CullMode = D3D11_CULL_NONE; rsDesc.DepthClipEnable = TRUE;
      dev.device->CreateRasterizerState(&rsDesc, &rasterState); }
    ID3D11SamplerState* sampler = nullptr;
    { D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
      sd.MaxLOD = D3D11_FLOAT32_MAX; dev.device->CreateSamplerState(&sd, &sampler); }
    ID3D11Texture2D* whitePlaceholderTex = nullptr; ID3D11ShaderResourceView* whitePlaceholderSrv = nullptr;
    { D3D11_TEXTURE2D_DESC td{}; td.Width=1; td.Height=1; td.MipLevels=1; td.ArraySize=1; td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
      td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_IMMUTABLE; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
      uint8_t px[4]={255,255,255,255}; D3D11_SUBRESOURCE_DATA sub{}; sub.pSysMem=px; sub.SysMemPitch=4;
      dev.device->CreateTexture2D(&td, &sub, &whitePlaceholderTex); dev.device->CreateShaderResourceView(whitePlaceholderTex, nullptr, &whitePlaceholderSrv); }
    // A second, BLACK (0,0,0,0) placeholder - NEEDED (not cosmetic) for
    // ir_light_directional pass 0's own real ir_ambient_occlusionSampler:
    // confirmed by direct HLSL read (see this pass's own diagnostic dump)
    // that this shader computes r0.y = saturate(1 - AO.x) i.e. AO.x==1.0
    // ("white") means FULLY OCCLUDED (zero ambient), the OPPOSITE of the
    // "neutral/no data" meaning white carries for every other placeholder
    // in this tool family - using white here would silently zero the real
    // ambient term. Black (AO.x==0.0) is the real "no occlusion" neutral.
    ID3D11Texture2D* blackPlaceholderTex = nullptr; ID3D11ShaderResourceView* blackPlaceholderSrv = nullptr;
    { D3D11_TEXTURE2D_DESC td{}; td.Width=1; td.Height=1; td.MipLevels=1; td.ArraySize=1; td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
      td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_IMMUTABLE; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
      uint8_t px[4]={0,0,0,0}; D3D11_SUBRESOURCE_DATA sub{}; sub.pSysMem=px; sub.SysMemPitch=4;
      dev.device->CreateTexture2D(&td, &sub, &blackPlaceholderTex); dev.device->CreateShaderResourceView(blackPlaceholderTex, nullptr, &blackPlaceholderSrv); }

    dev.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dev.context->RSSetState(rasterState);
    D3D11_VIEWPORT vp{}; vp.Width = static_cast<float>(kW); vp.Height = static_cast<float>(kH); vp.MinDepth=0; vp.MaxDepth=1;
    dev.context->RSSetViewports(1, &vp);

    // -----------------------------------------------------------------
    // PASS A: real G-buffer prepass. Every drawable range, its own real
    // role-4 shader, into the 3 real off-screen targets + real DSV.
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
        { auto matIt = cs.perMaterialCbPs.find(rr.materialId);
          ID3D11Buffer* psCb = (matIt != cs.perMaterialCbPs.end()) ? matIt->second : cs.cbPs;
          dev.context->PSSetConstantBuffers(0, 1, &psCb); }
        for (UINT slot : cs.allTexBindPoints) dev.context->PSSetShaderResources(slot, 1, &whitePlaceholderSrv);
        for (UINT slot : cs.allSamplerBindPoints) dev.context->PSSetSamplers(slot, 1, &sampler);
        UINT startIndex = static_cast<UINT>(spanIt->second.first), count = static_cast<UINT>(spanIt->second.second);
        dev.context->DrawIndexed(count, startIndex, 0);
        ++gbufferDrawCalls;
    }
    printf("[pass A: G-buffer prepass] %zu real DrawIndexed calls into the 3 real off-screen targets + real DSV\n",
           gbufferDrawCalls);
    ID3D11ShaderResourceView* nullSrvs3[3] = {nullptr,nullptr,nullptr};
    dev.context->OMSetRenderTargets(0, nullptr, nullptr); // unbind before sampling as SRVs below

    // -----------------------------------------------------------------
    // PASS B: real directional light, ir_light_directional's own real T8
    // pass 0 (the no-shadow, single-pass variant - a deliberate, brief-
    // sanctioned simpler choice; it also genuinely never reads
    // IR_GBuffer_DepthSampler at all, confirmed by direct HLSL dump, so
    // depthParamRT is written by pass A for pipeline-shape completeness
    // but not sampled here).
    // -----------------------------------------------------------------
    std::vector<uint8_t> lightFxo;
    if (!findEntry(shadersContainer, "ir_light_directional.fxo_pc", lightFxo)) {
        printf("FATAL: ir_light_directional.fxo_pc not found\n"); return 1;
    }
    CompiledShader lightCs = compileOnePass(dev.device, lightFxo, "ir_light_directional.fxo_pc", "ir_light_directional", /*role=*/0);
    if (!lightCs.ok) { printf("FATAL: ir_light_directional pass 0 failed to compile: %s\n", lightCs.failReason.c_str()); return 1; }
    {
        HRESULT hr = dev.device->CreateVertexShader(lightCs.vsCode->GetBufferPointer(), lightCs.vsCode->GetBufferSize(), nullptr, &lightCs.vs);
        if (SUCCEEDED(hr)) hr = dev.device->CreatePixelShader(lightCs.psCode->GetBufferPointer(), lightCs.psCode->GetBufferSize(), nullptr, &lightCs.ps);
        if (SUCCEEDED(hr)) hr = dev.device->CreateInputLayout(kQuadLayout, 2, lightCs.vsCode->GetBufferPointer(), lightCs.vsCode->GetBufferSize(), &lightCs.inputLayout);
        if (FAILED(hr)) { printf("FATAL: ir_light_directional GPU object creation failed: %s\n", hrToString(hr).c_str()); return 1; }
    }
    printf("[light shader] ir_light_directional real pass %d: VS inputs=%s  PS declares IR_GBuffer_NormalsSampler@s%d "
           "IR_GBuffer_LightingSampler@s%d  real PS outputs=%u\n", lightCs.passIndexUsed,
           signatureToString(lightCs.vsInputs).c_str(), lightCs.normalsSamplerReg, lightCs.lightingSamplerReg,
           lightCs.psOutputCount);

    // Real VS cbuffer fill: IR_Light_Inv_Proj_TM (real inverse of THIS
    // tool's own projection matrix) + Target_dimensions (this tool's own
    // chosen resolution, a real value for that choice).
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
    // Real PS cbuffer fill: IR_Light_Color/back_color/V_ambient_render are
    // now REAL, from the real weather_time_of_day.xtbl noon/Overcast row
    // loaded in step 1b above (see that step's own comment for exactly
    // which row and why). IR_Light_Pos (DIRECTION) stays CHOSEN: this
    // table's real schema (spec-tables-environment.md 3.3, cross-checked
    // against include/sr3tables_environment/weather_time_of_day.h) has no
    // light-DIRECTION field anywhere - only colour/ambient/exposure - so
    // forcing a "real" direction out of it would be fabricating a fit that
    // is not there, exactly what this task's own brief warns against.
    // Shadow_map_enabled (bool) left false (real for "no shadow data
    // bound", matching this pass-0 variant) - unchanged.
    lightCs.psCbufCpu.assign(lightCs.psCbSize, 0);
    // CHOSEN direction, in VIEW SPACE (the G-buffer normal this samples
    // against is view-space - traced directly from the real VS: the TBN
    // basis is rotated by IR_World2View as its last step before being
    // written to the G-buffer, confirmed via HLSL dump). Z is NEGATIVE
    // (pointing back out of the screen, toward the eye) because this
    // tool's own view convention (computeOrbitViewMatrix) places +Z
    // "forward" (into the screen); a light meant to illuminate the
    // camera-facing side of the car must point back toward -Z, or the
    // real N.L term saturates to 0 over the whole visible surface - found
    // by direct HLSL trace + one corrective iteration, not guessed blind.
    // UNCHANGED this pass (still CHOSEN - see immediately above).
    const float kChosenLightDirView[3] = {0.35f, 0.55f, -0.55f};
    // REAL colours x REAL per-row Intensity, x a CHOSEN-but-table-SOURCED
    // scale term. Each of the 3 real colours above is authored as an RGB
    // triple (<=1 per channel) times a SEPARATE, much larger HDR-domain
    // Intensity (13.0 / 1.2 / 2.5 for this row) meant for a real tonemap/
    // exposure pipeline downstream - which this prototype's light-
    // accumulation target (a CHOSEN plain R8G8B8A8_UNORM surface, no
    // tonemap pass - see top comment) does not implement. Multiplying by
    // the raw real Intensity directly would feed values as large as 13.0
    // into an LDR write and blow out harder than the prior placeholder did
    // (checked - see this file's own SubagentHandback report for the
    // before/after pixel stats). Rather than invent an arbitrary damping
    // number, this SAME real row's own District_Lighting/Exposure/
    // Exposure_Min (0.12) is used as the scale - the one field in this
    // table whose documented purpose (spec 3.3) is exactly "how much of
    // this scene's brightness reaches the display." Every number
    // multiplied below is real and table-sourced; the multiplication
    // formula itself (colour * Intensity * Exposure_Min) is NOT a
    // confirmed real shader/tonemap formula (that pipeline was not traced
    // this pass) - it is this pass's own reasonable, labelled choice for
    // compressing an HDR-authored row into this LDR prototype.
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
                       "field, only colour/ambient/exposure - see top comment / step 1b)";
        } else if (containsCI(c.name, "ir_light_color")) {
            regs.push_back({realSunColorOut[0], realSunColorOut[1], realSunColorOut[2], realSunColorOut[3]});
            decision = realLightRowFound
                ? "REAL: weather_time_of_day.xtbl noon/Overcast TOD_Light_Color*Intensity, CHOSEN scale = this "
                  "same row's real Exposure_Min (see step 1b / this constant fill's own comment)"
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

    // Real fullscreen-covering quad (2 triangles), POSITION0 already in
    // NDC clip space (this shader's own VS passes it straight through -
    // confirmed via HLSL dump, see top comment), TEXCOORD0 the matching
    // [0,1] UV.
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
    // Any OTHER real texture slot this light PS declares (shadow map,
    // ambient-occlusion map - real slots, no real content available this
    // pass) gets the same neutral white placeholder standard the rest of
    // this tool family uses, so it doesn't multiply/blend to zero.
    for (UINT slot : lightCs.allTexBindPoints) {
        if (static_cast<int>(slot) == lightCs.normalsSamplerReg || static_cast<int>(slot) == lightCs.lightingSamplerReg) continue;
        // BLACK, not white - see blackPlaceholderSrv's own declaration
        // comment (ir_ambient_occlusionSampler reads white as "fully
        // occluded" in this shader's own real math).
        dev.context->PSSetShaderResources(slot, 1, &blackPlaceholderSrv);
    }
    dev.context->DrawIndexed(6, 0, 0);
    printf("[pass B: directional light] 1 real DrawIndexed(6) fullscreen quad, ir_light_directional pass %d, real "
           "IR_GBuffer_NormalsSampler/IR_GBuffer_LightingSampler bound to pass A's own real output -> real light "
           "buffer\n", lightCs.passIndexUsed);
    dev.context->OMSetRenderTargets(0, nullptr, nullptr);

    // -----------------------------------------------------------------
    // PASS C: real material (paint) pass, role 6 of each shader, with
    // IR_LBufferSampler bound to PASS B's own real output - the actual
    // upgrade this task exists to make (paintcompose's own fork bound
    // this same real register to a white placeholder instead).
    // -----------------------------------------------------------------
    OffscreenDepth finalDepth;
    std::string fdErr;
    if (!finalDepth.create(dev.device, kW, kH, fdErr)) { printf("FATAL: final depth creation failed: %s\n", fdErr.c_str()); return 1; }
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
        { auto matIt = cs.perMaterialCbPs.find(rr.materialId);
          ID3D11Buffer* psCb = (matIt != cs.perMaterialCbPs.end()) ? matIt->second : cs.cbPs;
          dev.context->PSSetConstantBuffers(0, 1, &psCb); }
        for (UINT slot : cs.allTexBindPoints) {
            ID3D11ShaderResourceView* srv = whitePlaceholderSrv;
            if (static_cast<int>(slot) == cs.lBufferSamplerReg) srv = lightBufferRT.srv; // THE real upgrade
            dev.context->PSSetShaderResources(slot, 1, &srv);
        }
        for (UINT slot : cs.allSamplerBindPoints) dev.context->PSSetSamplers(slot, 1, &sampler);
        UINT startIndex = static_cast<UINT>(spanIt->second.first), count = static_cast<UINT>(spanIt->second.second);
        dev.context->DrawIndexed(count, startIndex, 0);
        ++finalDrawCalls; finalTriangles += count/3;
    }
    printf("[pass C: material/paint pass] %zu real DrawIndexed calls (%zu triangles), IR_LBufferSampler bound to "
           "pass B's own real light-buffer output for every range whose shader declares it\n", finalDrawCalls, finalTriangles);

    // -----------------------------------------------------------------
    // 9. Read back + PNG + real pixel statistics.
    // -----------------------------------------------------------------
    std::vector<uint8_t> rgba; std::string rbErr;
    if (!dev.readBack(rgba, rbErr)) { printf("FATAL: readBack failed: %s\n", rbErr.c_str()); return 1; }
    uint64_t sum[4] = {0,0,0,0}; uint8_t minV[4] = {255,255,255,255}, maxV[4] = {0,0,0,0};
    for (size_t i = 0; i < rgba.size(); i += 4)
        for (int c = 0; c < 4; ++c) { uint8_t v = rgba[i+c]; sum[c]+=v; if (v<minV[c]) minV[c]=v; if (v>maxV[c]) maxV[c]=v; }
    size_t pixelCount = rgba.size()/4;
    printf("\n=== Real RGBA8 pixel statistics (%ux%u, %zu pixels) ===\n", kW, kH, pixelCount);
    const char* chan[4] = {"R","G","B","A"};
    for (int c = 0; c < 4; ++c)
        printf("  %s: min=%u max=%u mean=%.3f\n", chan[c], minV[c], maxV[c], pixelCount ? static_cast<double>(sum[c])/pixelCount : 0.0);
    // NEW in this pass: a real blow-out/saturation fraction (the background
    // clear colour is dark, (0.05,0.05,0.08) -> ~(13,13,20) in 8-bit, so any
    // near-white pixel below is genuine foreground - car surface - blow-
    // out, not background) - lets the "did the real light row improve or
    // worsen the blown-out hood" question in this pass's own brief be
    // answered with a real number instead of eyeballing the PNG alone.
    size_t pureWhite = 0, nearWhite250 = 0;
    for (size_t i = 0; i < rgba.size(); i += 4) {
        if (rgba[i] == 255 && rgba[i+1] == 255 && rgba[i+2] == 255) ++pureWhite;
        if (rgba[i] >= 250 && rgba[i+1] >= 250 && rgba[i+2] >= 250) ++nearWhite250;
    }
    printf("  blow-out: pure-white(255,255,255) pixels=%zu (%.2f%%), near-white(>=250,>=250,>=250) pixels=%zu (%.2f%%)\n",
           pureWhite, pixelCount ? 100.0 * pureWhite / pixelCount : 0.0, nearWhite250,
           pixelCount ? 100.0 * nearWhite250 / pixelCount : 0.0);

    std::string pngErr;
    const std::string outPath = "D:/Project Crreish/TEAM B/tools/prototype_lit_car_output.png";
    if (!sr3render::writePng(outPath, kW, kH, rgba, pngErr)) { printf("FATAL: writePng failed: %s\n", pngErr.c_str()); return 1; }
    printf("\n[png] wrote %s\n", outPath.c_str());
    printf("\n=== DONE ===\n");
    return 0;
}
