// Standalone prototype, forked from tools/prototype_lit_car.cpp (untouched by
// this file - same "fork, don't modify" discipline every prototype in this
// family uses). Reuses that file's own real 3-pass deferred pipeline shape
// (G-buffer prepass -> ir_light_directional pass 0 -> material pass reading
// the light result), its shaderHash/.fxo_pc-stem join, its T8 role-pairing
// mechanism (pairVsPsForRole/compileOnePass, already generalized to "whatever
// pass a file's own real role table names", not hardcoded to car paint), its
// matrix/camera helpers, and its weather_time_of_day.xtbl noon/Overcast real
// light-row lookup, almost verbatim - see that file's own top comment for the
// full provenance of those pieces, not repeated here.
//
// NEW IN THIS FORK (orchestrator task, 2026-09-30): extends the SAME proven
// pipeline from vehicle paint to a real level-mesh city prop - the airport
// control tower (`airport_controltower.clmesh_pc`, `sr3_city_0.vpp_pc ::
// 1018h0.str2_pc`) - using sr3clmesh::LevelMesh instead of sr3vehicle::Vehicle
// + sr3geometry::GeometryBlock as the mesh/material source, and real
// per-material texture upload (findAndUploadTexture()/TextureSearchCache,
// copied verbatim from tools/sr3_viewer.cpp - that project-established
// mechanism, not reinvented) instead of car paint's flat-colour-only system
// (prototype_lit_car.cpp never calls findAndUploadTexture at all - car paint
// has no diffuse texture, see sr3geometry/material_binding.h's own
// diffuse()==nullptr note for vehicles). See this file's own SubagentHandback
// report for the full session narrative; the short version, so this header is
// self-contained if read cold:
//
//   REAL, independently measured/verified THIS session directly against real
//   game bytes (not trusted from any prior paraphrase):
//     - shaderHash (MaterialRecord::hash0) -> real .fxo_pc stem resolution:
//       81/81 (100%) across EVERY real material of EVERY real .clmesh_pc
//       reachable from 1018h0.str2_pc (22 files; the census below runs this
//       for the full population, not just the tower - see step 1c). ZERO
//       misses - so there is no miss list to report for hash0 resolution
//       itself (the task's own explicit ask). This full census is real
//       tool output, printed on every run, not a separate document.
//     - Of the 20 distinct real stems that 81-material population resolves
//       to, only 2 (`ir_bbsimple1`, `ir_bbsimple3`) have a real per-stem
//       `_v`/`_mv` vertex-shader file under this project's existing
//       same-stem-CRC join convention (collectFxoStems/pickVertexFile,
//       unchanged from the vehicle fork) - the other 18 stems' real
//       shaders.vpp_pc entries are pixel-stage-only (`_bs`/`_c`/`_fd`/`_mc`/
//       `_ms`/`_s` suffixes, no `_v`/`_mv`), i.e. genuinely NOT reachable as
//       a compilable VS+PS pair through this project's OWN established join
//       - a real, honestly-reported population fact, not a bug in this
//       tool. For `airport_controltower.clmesh_pc` specifically (19 real
//       materials), this means only materialId 7 and materialId 11 (both
//       stem `ir_bbsimple1`) are actually DRAWABLE through the proven
//       role4(G-buffer-write)->role6(forward-lit) mechanism; every other
//       material's shaderHash still resolves to a real stem (0 hash0
//       misses - see above) but is reported, not silently placeholdered,
//       as "resolved but not drawable (no real per-stem VS file)" - printed
//       explicitly per-material below, distinct from an actual hash0 miss.
//     - ir_bbsimple1's real role4 pass0 PS was independently re-traced THIS
//       session (not assumed from the car-paint case) and writes
//       oC0/oC1/oC2 with the EXACT SAME encode shape prototype_lit_car.cpp
//       already found for ir_sr3carpaint_gr: oC0.xy = mad(normalXY, 0.5,
//       0.5) (encode to [0,1]); oC1.x = saturate(scale*viewZ)
//       (depth-parameter); oC2.x = max(Specular_Power*scale, floor). Same
//       Normals/Depth/Lighting-parameter G-buffer convention, reconfirmed
//       against a second, structurally-independent real shader family.
//     - ir_bbsimple1's real role6 pass2 PS was independently re-traced THIS
//       session and its FINAL instruction is, byte-for-byte the same shape
//       as ir_sr3carpaint_gr's: `output.oC0.xyzw = (r2.xyzw * c[37].xyzw)`
//       where c[37] is this shader's own real Tint_color - i.e. the SAME
//       real "Tint_color=0 (unauthored) zeroes the entire pass" finding
//       applies here too, RE-confirmed, not merely inherited by analogy -
//       so the SAME deliberately-reintroduced PLACEHOLDER Tint_color=1
//       override prototype_lit_car.cpp already carries is kept, unchanged
//       in kind, now on independently-checked grounds for this shader too.
//     - Real per-material texture roles used by the 2 drawable materials
//       (measured directly from this tower's own real texture-binding
//       dump): normal map (paramHash 0x2808EB90, `kParamHashNormalMap`,
//       already a named constant in sr3geometry/material_binding.h),
//       diffuse map (0x69B48F91, `kParamHashDiffuseB`, already named), and
//       a THIRD real role, specular map (0x69B48F91 is taken so this is a
//       distinct hash, 0xE848C9CA - NOT currently a named constant in that
//       shared header; kept as a local `kParamHashSpecularClmesh` in this
//       file rather than editing that shared header for one task-scoped
//       finding). All three are wired to the real role4 `Normal_MapSampler`
//       / role6 `Diffuse_MapSampler` / role6 `Specular_MapSampler` CTAB
//       names via the SAME findAndUploadTexture()/TextureSearchCache
//       mechanism runClmesh()/runClmeshTile() already use (copied verbatim
//       below from tools/sr3_viewer.cpp, not reinvented - see that file's
//       own comments for texCache's own real cause and effect).
//
//   CHOSEN by me (this pass), labelled plainly, matching prototype_lit_car
//   .cpp's own CHOSEN list unchanged in kind (not reproduced in full here -
//   see that file's own top comment): G-buffer/light-buffer pixel format,
//   resolution (800x600, SAME as every tool in this family), the camera
//   (orbit view + standalone invertible LH perspective projection, yaw=
//   0.6/pitch=0.35/distanceScale=1.8 - the SAME literal defaults the
//   existing `clmesh` sr3_viewer command and prototype_lit_car.cpp both
//   already use), IR_Light_Pos direction (weather_time_of_day.xtbl has no
//   direction field - unchanged from the parent prototype).
//
//   HONEST / OPEN, stated plainly, not silently resolved either way (the
//   central finding of this fork, beyond the car-paint case): ir_bbsimple1's
//   real role6 pass2 VS computes its PS's diffuse-texture multiplier
//   (`input.v0`, PS_INPUT TEXCOORD0) as `Diffuse_Color * Object_instance_
//   params` (independently re-traced this session, see the VS HLSL this
//   tool's own investigation dumped), and the same pass's specular
//   contribution further depends on `Specular_Color`/`Specular_Map_Amount`/
//   `Self_Illumination` - FIVE genuine, named, externally-filled CTAB
//   constants with NO established real per-material source anywhere in this
//   codebase (unlike vehicles' proven array-A/B/C per-material-constant
//   mechanism, spec-vehicle-geometry.md Sec11.2). sr3clmesh::LevelMesh::
//   MaterialRecord's OWN `constantNameCount`/`vec4ConstantCount` fields
//   (level_mesh.h +0x0E/+0x0F) structurally resemble that SAME vehicle
//   mechanism's `aCount`/`bCount` header shape (same relative offsets, same
//   "count-driven array past the fixed header" idiom) and are a real,
//   concrete, testable lead for recovering genuine per-material Diffuse_
//   Color/Specular_Color/etc. values in a follow-up session - but decoding a
//   NEW part of this binary format is a separate investigation this task's
//   own "don't invent a new fallback rule" instruction rules out attempting
//   here, and level_mesh.h's own header comment already states this region
//   is "genuinely OPEN". This tool therefore applies its OWN already-
//   established REAL-confirmed-zero-when-unauthored convention (spec-
//   render-pipeline.md Sec20.12.11, unchanged from the parent prototype) to
//   all five, faithfully, with ONLY the one pre-existing Tint_color
//   exception kept. MEASURED CONSEQUENCE (not assumed - via this fork's own
//   NEW printTargetStats() diagnostic readback of the intermediate G-buffer/
//   light-buffer targets, run every time, not a one-off check): the drawable
//   geometry's final per-pixel colour comes out (0,0,0) - a real, flat,
//   deterministic black silhouette against the CHOSEN dark clear colour.
//   Tracing WHY through the actual measured intermediate stats (not just
//   Diffuse_Color/Specular_Color/etc.): role4's real VS ALSO declares named
//   Normal_Map_TilingU/TilingV constants (unlike role6, which bakes its own
//   UV scale as a shader-local DEF constant) - at their own real zero
//   default, every pixel's normal-map sample lands on the SAME texel
//   (UV=(0,0)), so the G-buffer Normals target's oC0.xy comes out UNIFORM
//   (measured: R/G channels read exactly 0 across the WHOLE target,
//   drawn-or-not - only the write-mask alpha varies with real geometry
//   coverage) rather than per-pixel-varying: NOT a bug in this fork's own
//   pipeline wiring (the SAME readback confirms the lighting-parameter
//   target's oC2.x genuinely DOES carry the real Specular_Power 0.1 floor,
//   R channel measured 0..1/255 exactly matching max(0.1/512,1/256)'s real
//   encode - so per-material real data IS reaching the G-buffer; it is
//   specifically the per-PIXEL normal-map detail that a different real
//   unauthored VS constant pair flattens), and the light-accumulation
//   buffer, fed by that uniform normal, ends up UNIFORM too (measured:
//   R,G,B,A constant (21,46,65,91) across the entire target) - but that
//   uniform value genuinely IS the real noon/Overcast weather-row colour
//   run through the real shader math, not an arbitrary number (see the
//   [light-accumulation buffer] printf line, which reports it every run).
//   So: two independent real per-material constants (Normal_Map_TilingU/V
//   at the G-buffer stage; Diffuse_Color/Object_instance_params/Specular_
//   Color/Specular_Map_Amount/Self_Illumination at the material stage)
//   both default to zero under the SAME established convention and both
//   contribute to the final flat-black result - reported fully, via this
//   tool's own diagnostic printfs, not smoothed into one vaguer claim.
//
//   Also OPEN, unchanged in kind from the parent prototype: every other PS
//   constant this tool still fills with the real confirmed-zero/0.1-floor
//   template (Normal_Map_Height, Fog_color, IR_Pixel_Steps, Specular_Alpha,
//   Target_dimensions on the PS side; Object_instance_params_2, Fog_dist,
//   eyePos on the VS side) rather than a per-material authored value -
//   exactly the same standard, not a new one.
//
// Standalone: not wired into CMakeLists.txt, build_verify/ untouched. Built
// via a direct cl.exe/link.exe compile (see this session's own report for
// the exact commands) - the SAME convention every prototype_*.cpp in this
// family already uses (none of them are CMake targets); also compiled+linked
// into tests/golden/_bin by tests/golden/build.bat for the new golden scene
// this fork exists to freeze.

#include "prototype_lit_clmesh_tower_session.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace tower_proto;

// ===========================================================================
// main()
// ===========================================================================
int main(int argc, char** argv) {
    TowerSession s;
    auto& outPath = s.outPath;
    // CHOSEN default output path (this task's own target, hardcoded like
    // every other prototype_*.cpp in this family hardcodes its own real
    // subject); argv[1], when given, overrides it - the ONE command-line
    // affordance this fork adds, needed so tests/golden/golden_scene_check
    // .cpp can capture into its own _work/ directory without this tool
    // otherwise taking any arguments, matching tree_baseline_render.cpp's
    // own "positional out-path" convention for a golden-wired tool.
    if (argc >= 2) outPath = argv[1];

    printf("=== Lit-clmesh prototype: real G-buffer prepass -> real directional light -> real material pass ===\n");
    printf("(forked from prototype_lit_car.cpp - see this file's own top comment)\n\n");


    if (!towerSetupArchives(s)) return 1;
    if (!towerSetupCensus(s)) return 1;
    if (!towerSetupEnvironment(s)) return 1;
    if (!towerSetupClmesh(s)) return 1;
    if (!towerResolveRenderGroup(s)) return 1;
    if (!towerCompileShaders(s)) return 1;
    if (!towerUploadTextures(s)) return 1;
    if (!towerDecodeChannels(s)) return 1;
    if (!towerSetupCamera(s)) return 1;
    if (!towerFillConstantBuffers(s)) return 1;
    if (!towerCreateTargets(s)) return 1;
    if (!towerPassGBuffer(s)) return 1;
    if (!towerPassLight(s)) return 1;
    if (!towerPassMaterial(s)) return 1;
    if (!towerWriteOutput(s)) return 1;
    return 0;
}
