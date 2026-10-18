// Standalone prototype, forked from tools/prototype_lit_clmesh_tower.cpp
// (untouched by this file - same "fork, don't modify" discipline every
// prototype in this family uses; that file was itself forked from
// tools/prototype_lit_car.cpp, also untouched). Reuses the parent fork's own
// real 3-pass deferred pipeline shape (G-buffer prepass -> ir_light_directional
// pass 0 -> material pass reading the light result), its shaderHash/.fxo_pc-
// stem join, its T8 role-pairing mechanism (pairVsPsForRole/compileOnePass),
// its matrix/camera helpers, its weather_time_of_day.xtbl noon/Overcast real
// light-row lookup, and its real per-material texture upload
// (findAndUploadTexture()/TextureSearchCache) almost verbatim - see that
// file's own top comment for the full provenance of those pieces, not
// repeated here.
//
// NEW IN THIS FORK (orchestrator task, 2026-09-30): resolves the parent
// fork's two EXPLICITLY left-OPEN findings - the 17/19 "resolved, not
// drawable" tower materials, and the flat-black final colour on the 2 that
// WERE drawable - each independently VERIFIED against real bytes THIS
// session before being trusted, per this project's own "a spec citation
// must still be re-checked against THIS carrier's real bytes" discipline
// (precedent: HANDOFF Sec9.133). Both leads PANNED OUT, though lead 1's
// panned-out mechanism is NOT the one originally hypothesised:
//
//   LEAD 1 (generalized VS/PS resolution) - the specific "bare-stem file"
//   hypothesis is REFUTED (0/8 of the tower's non-suffixed real stems have
//   a bare `<stem>.fxo_pc` at all - measured, tools/clmesh_lead_probe.cpp),
//   but the BROADER structural claim spec-fxo-format.md Sec6.6 makes (the
//   `_v`/`_mv` suffix is a REGISTRATION-TIME naming convention only, "NOT a
//   structural requirement" - every real .fxo_pc file carries its own
//   independent VS/PS/T8 tables, Sec7.1/7.2) is STRONGLY CONFIRMED: every
//   one of the tower's 8 non-suffixed real stems' OTHER real files
//   (`_bs`/`_c`/`_fd`/`_mc`/`_ms`/`_s` suffixes - the parent fork's own top
//   comment had assumed these were "pixel-stage-only" from the NAME alone,
//   never checked structurally) in fact carries a real, non-empty role4 AND
//   role6 VS+PS T8 pairing - 8/8 stems, EVERY real candidate file checked,
//   not just one. pickVertexFile() below is generalized accordingly: try
//   `_v`/`_mv` first (unchanged precedent, zero behaviour change for the 2
//   already-drawable materialIds), then every other real candidate file for
//   the stem, compiling each through the SAME unmodified compileOnePass()
//   until one fully succeeds for BOTH role4 and role6 (not just structural
//   T8 pairing - real D3DCompile+reflection+signature-check+GPU object
//   creation, so a structurally-fine-but-untranslatable candidate would
//   correctly fall through, though none did: all 8 stems' FIRST tried
//   candidate compiled cleanly). Every attempt is printed for audit.
//
//   LEAD 2 (real per-material shader constants) - CONFIRMED against real
//   bytes: airport_controltower.clmesh_pc's own MaterialRecord B (constant-
//   name-hash)/C (vec4-value) arrays - counted since the parent fork's own
//   session (constantNameCount/vec4ConstantCount, level_mesh.h +0x0E/+0x0F)
//   but never decoded - are real and populated. Newly added
//   sr3clmesh::LevelMesh::materialShaderConstants() (include/sr3clmesh/
//   level_mesh.h, src/level_mesh.cpp - library code, not this fork-only
//   file, so other tools can reuse it) decodes them per spec-foliage-format
//   .md Sec6/Sec6.2's array layout, CONFIRMED to apply unconditionally to
//   .clmesh_pc by spec-physics-format.md Sec4.4.6(h) part 1's own
//   disassembly chain-trace (not assumed by foliage analogy alone). Every
//   real B hash, across all 14 materials this fork's own pipeline actually
//   draws (95/95, 100%, ZERO unmatched - this file's own full run, not just
//   the smaller hand-probe sample), matches a REAL CTAB constant name on
//   that exact material's own resolved shader (CRC-32 of the lower-cased
//   name, sr3fxo::hashLowerName) - see level_mesh.h's own doc comment for
//   the full population figures
//   and the measured index-correspondence rule (positional 1:1, C has 2
//   genuinely-unmatched trailing entries in 18/19 real records - OPEN, not
//   guessed at). Crucially, the REAL values recovered directly refute the
//   parent fork's central "flat black" finding's own root cause: real
//   Normal_Map_TilingU/TilingV read 1.0 (not the zero default that flattened
//   the G-buffer normal), and real Diffuse_Color reads (1,1,1,1) (a neutral
//   "no tint" white, not zero) - this fork wires these real per-material
//   values into the exact same cbuffer slots the parent fork's own generic
//   zero/default template used to fill, via the SAME fillFloat4Registers()
//   mechanism, per-materialId (materials sharing one shader can and do
//   carry distinct real B/C values, though this tower's own happen not to -
//   still handled per-material, not assumed shared).
//
// See this file's own SubagentHandback report for the full session
// narrative; the short version, so this header is self-contained if read
// cold (building on, not repeating, the parent fork's own real/CHOSEN/OPEN
// inventory, unchanged pieces omitted here):
//
//   (Everything below this point, to the "Also OPEN" paragraph, is the
//   PARENT fork's own original narrative, kept verbatim for provenance -
//   its "17/19 not drawable" and "flat black" claims are SUPERSEDED by
//   this fork's own measurements above; read it as history, not current
//   state.)
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
// HYPOTHESIS TEST, ADDED 2026-10-08 (orchestrator task, relayed from Team A
// via spec-render-pipeline.md Sec20.12.14 point 5): Team A's exhaustive
// negative confirms NOTHING in the shipped executable ever writes VS c35
// (Object_instance_params) or c36 (Object_instance_params_2) - full stop, no
// name-hash mechanism, no per-material array, no literal register upload
// anywhere. Team A's own working HYPOTHESIS (explicitly labelled HYPOTHESIS,
// not CONFIRMED, in the spec): the real game's D3D9 renderer never clears
// unused constant registers between draws, so these two registers hold
// whatever the PREVIOUS draw call happened to leave in them, while this
// project's clean-room renderer zero-inits every cbuffer fresh each draw -
// meaning this scene's saturated/neon-looking render could be an artifact of
// OUR OWN cleaner state management, not a missing data source.
//
// Tested concretely this session via a new opt-in switch,
// SR3_TEST_OBJ_INSTANCE_PARAMS=1 (unset = zero behaviour change from before
// this switch existed, verified by MD5: default-mode output is byte-
// identical, df05d007b471049ab7812944765e9a5c, to the existing frozen
// baseline both with and without this switch's code present). See
// fillVsCbuffer()'s own "object_instance_params" branch below for the full
// reasoning and the CHOSEN test value: (1,1,1,1), the multiplicative
// identity for the one concrete real use of this constant this project has
// ever traced (`Diffuse_Color * Object_instance_params` in ir_bbsimple1's
// real role6 pass2 VS, this file's own LEAD/HONEST section above) - NOT a
// confirmed value, NOT a captured real "previous draw" value (this
// standalone tool gives every material its OWN freshly-zeroed per-material
// cbuffer, so there is no genuine prior-draw GPU register state to inherit
// here - option (a) from the task brief was not testable in this tool for
// that reason).
//
// RESULT: NEGATIVE / does not fix the symptom. The test value DOES produce a
// real, measurable change (MD5 of the WARP capture changes from
// df05d007b471049ab7812944765e9a5c to 287f5d71e2c875512eba0b965a91ba58;
// comparePngTolerance against the default-mode capture: 8744/480000 pixels
// exceed a +-64/255 band, maxAbsDiff=127, meanAbsDiff=0.68 over the whole
// 800x600 frame) - but it is small, localized to only 2 of this scene's 14
// drawable materials (the terminal-building roof and a small tower-cap/door
// region - confirmed by rendering the actual tolerance-diff PNG, not just
// reading the numbers; the main tower shaft, which does not reference
// Object_instance_params/_2 in its own resolved shader, shows ZERO diff
// pixels), and it does NOT change the render's dominant visual character.
// Both before and after are flat, hard-edged, saturated cyan/blue/magenta -
// still a Fresnel/rim-looking "specular-only" silhouette with no visible
// diffuse texture/albedo detail, on both the changed AND unchanged parts of
// the geometry (including the tower shaft, where this constant plays no
// role at all and the render is pixel-identical either way) - i.e. this
// hypothesis test does NOT explain this scene's dominant visual symptom.
// This is consistent with, and does not override, this file's own earlier,
// independently-documented, more specific candidate explanation for that
// SAME saturated/neon look (see tests/golden/clmesh_airport_controltower_lit
// /COMMAND.txt's own "HONEST READ" section, 2026-10-07 WARP-migration pass):
// a zero `eyePos` placeholder degenerating the view-dependent Fresnel/
// specular term - a separate, still-untraced lead, out of this task's own
// scope. Per this project's own "don't freeze on a hunch" standing
// discipline, NO new golden baseline was frozen from this test - the
// existing baseline (hardware-and-WARP MD5s both confirmed unchanged by this
// session) remains the frozen truth for this scene. Full reasoning, the
// diff-PNG description, and every number above: this session's own
// SubagentHandback report.
//
// Standalone: not wired into CMakeLists.txt, build_verify/ untouched. Built
// via a direct cl.exe/link.exe compile (see this session's own report for
// the exact commands) - the SAME convention every prototype_*.cpp in this
// family already uses (none of them are CMake targets); also compiled+linked
// into tests/golden/_bin by tests/golden/build.bat for the new golden scene
// this fork exists to freeze.

#include "prototype_lit_clmesh_tower2_session.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace tower2_proto;

// ===========================================================================
// main()
// ===========================================================================
int main(int argc, char** argv) {
    Tower2Session s;
    auto& outPath = s.outPath;
    // CHOSEN default output path (this task's own target, hardcoded like
    // every other prototype_*.cpp in this family hardcodes its own real
    // subject); argv[1], when given, overrides it - the ONE command-line
    // affordance this fork adds, needed so tests/golden/golden_scene_check
    // .cpp can capture into its own _work/ directory without this tool
    // otherwise taking any arguments, matching tree_baseline_render.cpp's
    // own "positional out-path" convention for a golden-wired tool.
    if (argc >= 2) outPath = argv[1];

    if (!tower2SetupDiagnostics(s)) return 1;
    if (!tower2SetupArchives(s)) return 1;
    if (!tower2SetupCensus(s)) return 1;
    if (!tower2SetupCensus2(s)) return 1;
    if (!tower2SetupEnvironment(s)) return 1;
    if (!tower2SetupClmesh(s)) return 1;
    if (!tower2ResolveRenderGroup(s)) return 1;
    if (!tower2ResolveShaders(s)) return 1;
    if (!tower2FinalizeShaders(s)) return 1;
    if (!tower2UploadTextures(s)) return 1;
    if (!tower2DecodeChannels(s)) return 1;
    if (!tower2SetupCamera(s)) return 1;
    if (!tower2FillTemplateBuffers(s)) return 1;
    if (!tower2FillMaterialBuffers(s)) return 1;
    if (!tower2DumpMaterials(s)) return 1;
    if (!tower2CreateChannelBuffers(s)) return 1;
    if (!tower2CreateTargets(s)) return 1;
    if (!tower2PassGBuffer(s)) return 1;
    if (!tower2PassLight(s)) return 1;
    if (!tower2PassMaterial(s)) return 1;
    if (!tower2WriteOutput(s)) return 1;
    return 0;
}
