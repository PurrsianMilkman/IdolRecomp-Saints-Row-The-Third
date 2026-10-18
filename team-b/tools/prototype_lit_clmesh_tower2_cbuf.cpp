#include "prototype_lit_clmesh_tower2_session.h"

using namespace tower2_proto;

bool tower2_proto::tower2SetupCamera(Tower2Session& session) {
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

bool tower2_proto::tower2FillTemplateBuffers(Tower2Session& session) {
    auto& fillLog = session.fillLog;
    auto& kW = session.kW;
    auto& kH = session.kH;
    auto& gbufferShaders = session.gbufferShaders;
    auto& litShaders = session.litShaders;
    auto& viewProj = session.viewProj;
    auto& viewOnly = session.viewOnly;
    auto& worldIdentity = session.worldIdentity;
    auto& kRenderDocAbsurdValues = session.kRenderDocAbsurdValues;
    auto& kTestObjInstanceParams = session.kTestObjInstanceParams;
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
            } else if (containsCI(c.name, "object_instance_params")) {
                // HYPOTHESIS TEST branch (2026-10-08, orchestrator task, spec-
                // render-pipeline.md Sec20.12.14 point 5). Matches BOTH
                // Object_instance_params (VS c35) and Object_instance_params_2
                // (VS c36) via substring - confirmed as the only two real CTAB
                // constants carrying this name prefix in this shader family
                // (this file's own top comment / Sec20.12.14 item 4: 1 VS
                // register = 1 Vector4 each).
                //
                // Team A's exhaustive negative (spec-render-pipeline.md
                // Sec20.12.14): NOTHING in the shipped executable ever writes
                // VS c35 or c36 - not a name-hash mechanism, not a per-material
                // array, not a literal register upload anywhere. Working
                // HYPOTHESIS offered there (explicitly labelled HYPOTHESIS, not
                // CONFIRMED): the real game's D3D9 renderer never clears unused
                // constant registers between draws, so these two registers hold
                // whatever the PREVIOUS draw call happened to leave in them -
                // while this project's own renderer zero-inits every cbuffer
                // fresh each draw (cs.vsCbufCpu.assign(cs.vsCbSize, 0) above),
                // which may be an artifact of cleaner state management rather
                // than a missing data source to wire up.
                //
                // Testing this concretely: this standalone tool gives every
                // drawable material its OWN freshly-allocated per-material
                // cbuffer (step 10b below, mc.litVsCpu etc., all starting from
                // THIS generic template's own zeroed buffer) - there is no
                // genuine prior-draw GPU register state anywhere in this
                // tool's own pipeline to actually inherit, so option (a) from
                // the task brief ("feed from the previous draw's own actual
                // values") is NOT testable here: there is no real "previous
                // draw value" to capture, only ever more zero. Option (b) is
                // what IS testable: a representative, plausible stand-in value.
                //
                // CHOSEN test value (NOT a confirmed fact, NOT derived from
                // any real captured register state - an explicit stand-in):
                // (1,1,1,1). Picked specifically because this file's own
                // earlier, independently-traced finding (top comment) is that
                // ir_bbsimple1's real role6 pass2 VS computes its diffuse-
                // texture multiplier as `Diffuse_Color * Object_instance_
                // params` - so (1,1,1,1) is the multiplicative IDENTITY for
                // the one concrete real use of this constant this project has
                // ever actually traced, making it the most natural "a draw
                // happened to leave a neutral value here" stand-in to test
                // specifically whether the HARD ZERO (as opposed to the mere
                // absence of a deliberate value) is what is actually killing
                // diffuse. A genuinely different leftover value (e.g. a stale
                // tint/damage-blend scalar from some other object's own prior
                // draw) would be equally "plausible" and equally unconfirmed;
                // this is simply the one this session picked and tested, not
                // the only one that could have been chosen.
                if (kTestObjInstanceParams) {
                    for (uint16_t r = 0; r < c.registerCount; ++r) regs.push_back({1.0f,1.0f,1.0f,1.0f});
                    decision = "CHOSEN TEST VALUE (HYPOTHESIS TEST, NOT confirmed, spec-render-pipeline.md "
                               "Sec20.12.14 point 5, SR3_TEST_OBJ_INSTANCE_PARAMS=1): identity (1,1,1,1) stand-in "
                               "for an assumed stale-D3D9-carryover value - see fillVsCbuffer()'s own comment";
                } else {
                    for (uint16_t r = 0; r < c.registerCount; ++r) regs.push_back({0,0,0,0});
                    decision = "PLACEHOLDER: zero (untraced per-instance parameter)";
                }
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
            } else if (containsCI(c.name, "target_dimensions")) {
                // REAL DEFAULT BEHAVIOUR (2026-10-08, orchestrator task,
                // spec-render-pipeline.md Sec20.12.15 - see this file's own
                // top-of-main comment for the full finding). Previously an
                // opt-in hypothesis-test branch (SR3_TEST_TARGET_DIMENSIONS=1,
                // commit 57fc8ef); promoted unconditionally because Team A's
                // direct disassembly closed the question this was testing -
                // there is NO engine-side producer for this constant anywhere
                // in the executable (exhaustive whole-exe string scan, zero
                // hits; the three most-likely literal/fixed-register producer
                // functions fully decompiled, zero constant-upload calls in
                // any of them) - so the real-confirmed-zero-when-unauthored
                // regime that correctly applies to genuinely per-material
                // constants (Sec20.12.11) does not apply here: render-target
                // size is not a per-material-authored property, and there is
                // no traced mechanism to reproduce even if it were. Team A's
                // own stated recommendation: synthesize it directly.
                //
                // CHOSEN value (NOT a disassembly-confirmed producer value -
                // flagged as such per Sec20.12.15 point 5 itself): (800, 600,
                // 0, 0), this tool's own actual render resolution (kW/kH).
                // The 3rd/4th components are 0 for the same reason given
                // historically - matches the ONE existing real precedent for
                // this exact constant name already in this file:
                // ir_light_directional's own VS cbuffer fill (PASS B below)
                // already special-cases "target_dimensions" as REAL
                // {kW, kH, 0, 0} for that shader's own unrelated VS use of it.
                // SR3_RENDERDOC_ABSURD_VALUES=1 (new, this session): reuses
                // this branch's OWN already-tested CONTROL 1 first extreme
                // substitution (commit 108d7fc) instead of the CHOSEN
                // default, so a RenderDoc capture can show, in the actual
                // GPU shader-debug trace, that even an ~1e6x value swing
                // here never changes a drawn pixel - NOT a new value, NOT
                // the default; purely for this diagnostic's own "control"
                // capture.
                if (kRenderDocAbsurdValues) {
                    regs.push_back({1.0e6f, 1.0e6f, 0.0f, 0.0f});
                    decision = "SR3_RENDERDOC_ABSURD_VALUES=1 TEST VALUE (reuses this branch's own commit 108d7fc "
                               "CONTROL 1 first substitution, NOT the default): (1e6,1e6,0,0)"
                               " [CTAB registerIndex=" + std::to_string(c.registerIndex) +
                               " registerCount=" + std::to_string(c.registerCount) + "]";
                } else {
                    regs.push_back({static_cast<float>(kW), static_cast<float>(kH), 0.0f, 0.0f});
                    decision = "CHOSEN VALUE, now the real default (spec-render-pipeline.md Sec20.12.15 - exhaustive "
                               "negative, no engine-side producer exists for this constant anywhere in the executable): "
                               "(800,600,0,0), this tool's own actual render resolution - matches this file's own "
                               "ir_light_directional VS-fill precedent for the SAME constant name (PASS B below)"
                               " [CTAB registerIndex=" + std::to_string(c.registerIndex) +
                               " registerCount=" + std::to_string(c.registerCount) + "]";
                }
            } else if (containsCI(c.name, "ir_pixel_steps") || containsCI(c.name, "pixel_steps")) {
                // Same REAL DEFAULT promotion as target_dimensions immediately
                // above (both constants are read together by the real role6
                // PS preamble - see that branch's own comment for the full
                // Sec20.12.15 finding). CHOSEN value: the reciprocal of this
                // tool's own actual render resolution (1/800, 1/600), the
                // natural "one pixel step in UV space" reading of this
                // constant's own name for an 800x600 render target.
                // SR3_RENDERDOC_ABSURD_VALUES=1: same reasoning as
                // target_dimensions immediately above - reuses CONTROL 1's
                // own already-tested first substitution for this constant
                // (1e-6,1e-6,0,0, a full scale-inversion from the CHOSEN
                // 1/800,1/600 reciprocal, not just a magnitude change).
                if (kRenderDocAbsurdValues) {
                    regs.push_back({1.0e-6f, 1.0e-6f, 0.0f, 0.0f});
                    decision = "SR3_RENDERDOC_ABSURD_VALUES=1 TEST VALUE (reuses this branch's own commit 108d7fc "
                               "CONTROL 1 first substitution, NOT the default): (1e-6,1e-6,0,0)"
                               " [CTAB registerIndex=" + std::to_string(c.registerIndex) +
                               " registerCount=" + std::to_string(c.registerCount) + "]";
                } else {
                    regs.push_back({1.0f / static_cast<float>(kW), 1.0f / static_cast<float>(kH), 0.0f, 0.0f});
                    decision = "CHOSEN VALUE, now the real default (spec-render-pipeline.md Sec20.12.15 - exhaustive "
                               "negative, no engine-side producer exists for this constant anywhere in the executable): "
                               "(1/800,1/600,0,0), reciprocal of this tool's own actual render resolution"
                               " [CTAB registerIndex=" + std::to_string(c.registerIndex) +
                               " registerCount=" + std::to_string(c.registerCount) + "]";
                }
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

    printf("=== Constant-buffer fill decisions, GENERIC TEMPLATE (real vs. placeholder, by real CTAB name) ===\n");
    for (const auto& l : fillLog) printf("  %-56s : %s\n", l.constName.c_str(), l.decision.c_str());
    printf("\n");
    return true;
}

bool tower2_proto::tower2FillMaterialBuffers(Tower2Session& session) {
    auto& fillLog = session.fillLog;
    auto& dev = session.dev;
    auto& lm = session.lm;
    auto& clView = session.clView;
    auto& drawableMaterialIds = session.drawableMaterialIds;
    auto& resolutions = session.resolutions;
    auto& gbufferShaders = session.gbufferShaders;
    auto& litShaders = session.litShaders;
    auto& materialIdToVsFileName = session.materialIdToVsFileName;
    auto& materialCbuffers = session.materialCbuffers;
    // -----------------------------------------------------------------
    // 10b. NEW IN THIS FORK (Lead 2 - see top comment): real PER-MATERIAL
    //    constant-buffer override. The template above (fillVsCbuffer/
    //    fillPsCbufferTemplate, unchanged from the parent fork) is shared
    //    per DISTINCT SHADER; this step makes a per-materialId COPY of
    //    that template for every drawable material and overrides whatever
    //    slots that material's own real B/C arrays
    //    (sr3clmesh::LevelMesh::materialShaderConstants(), new library
    //    accessor - see include/sr3clmesh/level_mesh.h) actually supply,
    //    by matching each B name-hash against the REAL CTAB of THIS
    //    material's own resolved role4 (gb) and role6 (lit) shaders (VS
    //    and PS both searched - a name can live in either stage) via
    //    sr3fxo::hashLowerName(), the SAME lower-cased-name CRC32 primitive
    //    spec-fxo-format.md Sec7.6 defines for T1-T4 constant names. A B
    //    hash that matches nothing on THIS material's own resolved shader
    //    (can happen - a stem's B array can carry names for OTHER roles
    //    this pipeline doesn't use) is logged OPEN and left on the
    //    generic template; C entries past constantNameCount
    //    (`trailingUnnamed`) are genuinely unmatched to any name (see
    //    level_mesh.h's own doc comment) and are never written anywhere.
    //    Every material gets its OWN 4 cbuffers (gbVs/gbPs/litVs/litPs) -
    //    even 2 materials sharing one shader can carry distinct real B/C
    //    values, so a shared-per-shader buffer would be wrong in general
    //    (this tower's own materials happen to share identical values
    //    within a stem, but that is not assumed).
    // -----------------------------------------------------------------
    for (const auto& rr : resolutions) if (rr.ok) materialIdToVsFileName[rr.materialId] = rr.vsFileName;

    for (uint32_t matId : drawableMaterialIds) {
        auto vfIt = materialIdToVsFileName.find(matId);
        if (vfIt == materialIdToVsFileName.end()) continue;
        CompiledShader& gb = gbufferShaders[vfIt->second];
        CompiledShader& lit = litShaders[vfIt->second];
        MaterialCbuffers mc;
        mc.gbVsCpu = gb.vsCbufCpu; mc.gbPsCpu = gb.psCbufCpu;
        mc.litVsCpu = lit.vsCbufCpu; mc.litPsCpu = lit.psCbufCpu;

        sr3clmesh::LevelMesh::ShaderConstants msc = lm.materialShaderConstants(matId, clView);
        for (const auto& nc : msc.named) {
            bool matched = false;
            auto tryStage = [&](sr3d3d9bc::ConstantTable& ctab, std::vector<uint8_t>& cbuf, const char* stageLabel,
                                 const char* passLabel) {
                for (const auto& c : ctab.constants) {
                    if (c.registerSet != sr3d3d9bc::RegisterSet::Float4) continue;
                    if (sr3fxo::hashLowerName(c.name) != nc.nameHash) continue;
                    std::vector<std::array<float, 4>> regs;
                    // CHOSEN when registerCount>1 (not observed in any real
                    // B-matched entry this session - every real match had
                    // registerCount==1): broadcast the SAME real vec4
                    // across every register of that constant, rather than
                    // leaving the later ones on the generic template.
                    for (uint16_t r = 0; r < c.registerCount; ++r) regs.push_back(nc.value);
                    fillFloat4Registers(cbuf, c.registerIndex, c.registerCount, regs);
                    char hashBuf[16]; snprintf(hashBuf, sizeof(hashBuf), "0x%08X", nc.nameHash);
                    fillLog.push_back({"material[" + std::to_string(matId) + "] " + passLabel + " " + stageLabel + " " + c.name,
                                        std::string("REAL per-material (materialShaderConstants B/C array, nameHash=") +
                                            hashBuf + ")"});
                    matched = true;
                }
            };
            tryStage(gb.vsCtab, mc.gbVsCpu, "VS", "role4");
            tryStage(gb.psCtab, mc.gbPsCpu, "PS", "role4");
            tryStage(lit.vsCtab, mc.litVsCpu, "VS", "role6");
            tryStage(lit.psCtab, mc.litPsCpu, "PS", "role6");
            if (!matched) {
                char hashBuf[16]; snprintf(hashBuf, sizeof(hashBuf), "0x%08X", nc.nameHash);
                fillLog.push_back({"material[" + std::to_string(matId) + "] B nameHash=" + hashBuf,
                                    "OPEN: matches no real CTAB constant on this material's own resolved role4/role6 "
                                    "shader (likely names a constant for a DIFFERENT role this pipeline doesn't use) "
                                    "- left on the generic template"});
            }
        }
        if (!msc.trailingUnnamed.empty()) {
            fillLog.push_back({"material[" + std::to_string(matId) + "] C[" + std::to_string(msc.named.size()) +
                                    ".." + std::to_string(msc.named.size() + msc.trailingUnnamed.size() - 1) + "]",
                                "OPEN: " + std::to_string(msc.trailingUnnamed.size()) +
                                    " trailing real vec4 value(s) beyond constantNameCount - genuinely unmatched to "
                                    "any name, not written anywhere (see level_mesh.h's own doc comment)"});
        }

        auto makeCb = [&](std::vector<uint8_t>& cpu, ID3D11Buffer** out) {
            D3D11_BUFFER_DESC d{}; d.ByteWidth = static_cast<UINT>((cpu.size() + 15) & ~size_t(15));
            d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            cpu.resize(d.ByteWidth, 0);
            D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = cpu.data();
            dev.device->CreateBuffer(&d, &sd, out);
        };
        makeCb(mc.gbVsCpu, &mc.cbGbVs); makeCb(mc.gbPsCpu, &mc.cbGbPs);
        makeCb(mc.litVsCpu, &mc.cbLitVs); makeCb(mc.litPsCpu, &mc.cbLitPs);
        materialCbuffers[matId] = std::move(mc);
    }

    printf("=== Constant-buffer fill decisions, PER-MATERIAL real overrides (Lead 2) ===\n");
    for (const auto& l : fillLog) {
        if (l.constName.rfind("material[", 0) == 0) printf("  %-56s : %s\n", l.constName.c_str(), l.decision.c_str());
    }
    printf("\n");
    return true;
}

bool tower2_proto::tower2DumpMaterials(Tower2Session& session) {
    auto& resolutions = session.resolutions;
    auto& drawableMaterialIds = session.drawableMaterialIds;
    auto& materialIdToVsFileName = session.materialIdToVsFileName;
    auto& gbufferShaders = session.gbufferShaders;
    auto& litShaders = session.litShaders;
    auto& materialNormalTex = session.materialNormalTex;
    auto& materialDiffuseTex = session.materialDiffuseTex;
    auto& materialSpecularTex = session.materialSpecularTex;
    // -----------------------------------------------------------------
    // 10c. NEW IN THIS FORK (diagnostic task, 2026-10-08, orchestrator
    //    comparative per-material investigation of the tower's own
    //    "Fresnel/rim-looking specular-only" symptom): a SINGLE
    //    consolidated per-material dump, OPT-IN via SR3_DUMP_MATERIALS=1
    //    (unset = this exact block never prints - zero byte change to
    //    every existing frozen golden's own baseline_stdout.txt, same
    //    opt-in-switch convention as SR3_TEST_OBJ_INSTANCE_PARAMS above).
    //    Makes NO new real/placeholder decision and changes NO render
    //    state or cbuffer fill - it only re-presents, per materialId in
    //    one place, decisions this file's own earlier steps already made
    //    (resolutions/materialNormalTex/materialDiffuseTex/
    //    materialSpecularTex/gbufferShaders/litShaders/fillLog, all
    //    already populated above): the real shaderHash/stem/resolved VS
    //    file, every real CTAB sampler NAME each material's role4/role6
    //    shader declares (not just the 3 already-named ones) with what
    //    that slot actually got bound to this run, and the render-state
    //    this tool's own code sets for these draws (stated once, since it
    //    is shared across every material in this pipeline - see depthState
    //    /rasterState above: no OMSetBlendState call anywhere in this file
    //    means the implicit D3D11 default blend state, i.e. opaque, no
    //    blending, for every pass and every material alike - there is no
    //    per-material blend/depth/raster divergence to report here).
    // -----------------------------------------------------------------
    const char* dumpMaterialsRaw = std::getenv("SR3_DUMP_MATERIALS");
    const bool kDumpMaterials = (dumpMaterialsRaw != nullptr) && (std::string(dumpMaterialsRaw) == "1");
    // NEW DIAGNOSTIC SWITCH (orchestrator task, 2026-10-08, diag/tower-
    // constant-consumption-control): OPT-IN ONLY, via SR3_DUMP_CBUFFER_RAW=1
    // (unset = default = zero behaviour/output change). When set, PASS C's
    // own draw loop below calls dumpCbufferRegisterRaw() immediately before
    // each of this scene's 14 real DrawIndexed calls, reading back cbLitPs's
    // OWN live GPU bytes (via a staging-buffer Map, not the CPU-side source)
    // at float4 registers 47/48 (Target_dimensions/IR_Pixel_Steps) - rules
    // out a silently-stale-or-skipped per-material upload as a distinct
    // question from "was the CPU-side value computed correctly" (the
    // existing SR3_DUMP_ROLE6_HLSL/SR3_DUMP_MATERIALS fill-log already
    // answers the latter).
    if (kDumpMaterials) {
        auto samplerNameForSlot = [](const sr3d3d9bc::ConstantTable& ctab, UINT slot) -> std::string {
            for (const auto& c : ctab.constants)
                if (c.registerSet == sr3d3d9bc::RegisterSet::Sampler && static_cast<UINT>(c.registerIndex) == slot)
                    return c.name;
            return "(unnamed real sampler slot)";
        };
        std::map<uint32_t, uint32_t> matIdToHash0;
        std::map<uint32_t, std::string> matIdToStem;
        for (const auto& rr : resolutions) if (rr.ok) { matIdToHash0[rr.materialId] = rr.shaderHash; matIdToStem[rr.materialId] = rr.stem; }
        printf("=== [DUMP, SR3_DUMP_MATERIALS=1] Per-material consolidated diagnostic "
               "(%zu drawable material(s)) ===\n", drawableMaterialIds.size());
        for (uint32_t matId : drawableMaterialIds) {
            auto vfIt = materialIdToVsFileName.find(matId);
            if (vfIt == materialIdToVsFileName.end()) { printf("  material[%u]: NOT in materialIdToVsFileName (unexpected)\n", matId); continue; }
            CompiledShader& gb = gbufferShaders[vfIt->second];
            CompiledShader& lit = litShaders[vfIt->second];
            printf("  material[%u]: shaderHash(hash0)=0x%08X stem='%s' resolvedVsFile='%s'\n", matId,
                   matIdToHash0[matId], matIdToStem[matId].c_str(), vfIt->second.c_str());
            printf("    role4 (G-buffer prepass) PS real texture slots:\n");
            for (UINT slot : gb.allTexBindPoints) {
                std::string name = samplerNameForSlot(gb.psCtab, slot);
                std::string resolved;
                if (static_cast<int>(slot) == gb.normalMapSamplerReg) {
                    auto it = materialNormalTex.find(matId);
                    resolved = (it != materialNormalTex.end() && it->second.srv)
                        ? "REAL uploaded texture (see [texture] material[.] normal: line above for the filename)"
                        : "NOT FOUND this run - falls back to white placeholder";
                } else {
                    resolved = "WHITE PLACEHOLDER (no real binding wired for this slot in this tool)";
                }
                printf("      s%-2u %-32s : %s\n", slot, name.c_str(), resolved.c_str());
            }
            printf("    role6 (lit material pass) PS real texture slots:\n");
            for (UINT slot : lit.allTexBindPoints) {
                std::string name = samplerNameForSlot(lit.psCtab, slot);
                std::string resolved;
                if (static_cast<int>(slot) == lit.lBufferSamplerReg) {
                    resolved = "REAL: pass B's own light-accumulation buffer output (not a texture file)";
                } else if (static_cast<int>(slot) == lit.diffuseMapSamplerReg) {
                    auto it = materialDiffuseTex.find(matId);
                    resolved = (it != materialDiffuseTex.end() && it->second.srv)
                        ? "REAL uploaded texture (see [texture] material[.] diffuse: line above for the filename)"
                        : "NOT FOUND this run - falls back to white placeholder";
                } else if (static_cast<int>(slot) == lit.specularMapSamplerReg) {
                    auto it = materialSpecularTex.find(matId);
                    resolved = (it != materialSpecularTex.end() && it->second.srv)
                        ? "REAL uploaded texture (see [texture] material[.] specular: line above for the filename)"
                        : "NOT FOUND this run - falls back to white placeholder";
                } else if (static_cast<int>(slot) == lit.dsfDataSamplerReg) {
                    resolved = "REAL: pass A's own real oC1 (DSF_DATA) output (not a texture file) - "
                               "spec-render-pipeline.md Sec23.8, bound 2026-10-08";
                } else {
                    resolved = "WHITE PLACEHOLDER (no real binding wired for this slot in this tool - untraced, see top comment)";
                }
                printf("      s%-2u %-32s : %s\n", slot, name.c_str(), resolved.c_str());
            }
            printf("    render-state for both passes above (SHARED across every material in this pipeline, "
                   "not per-material): depth=D3D11_COMPARISON_LESS/DepthWriteMask=ALL  "
                   "raster=FILL_SOLID/CULL_NONE/DepthClipEnable  "
                   "blend=IMPLICIT D3D11 DEFAULT (no OMSetBlendState call anywhere in this file - opaque, "
                   "no alpha blending, for every pass/material alike)\n");
        }
        printf("\n");
    }
    return true;
}

bool tower2_proto::tower2CreateChannelBuffers(Tower2Session& session) {
    auto& dev = session.dev;
    auto& channels = session.channels;
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

