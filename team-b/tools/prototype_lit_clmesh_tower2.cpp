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
// Standalone: not wired into CMakeLists.txt, build_verify/ untouched. Built
// via a direct cl.exe/link.exe compile (see this session's own report for
// the exact commands) - the SAME convention every prototype_*.cpp in this
// family already uses (none of them are CMake targets); also compiled+linked
// into tests/golden/_bin by tests/golden/build.bat for the new golden scene
// this fork exists to freeze.

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

#include "sr3clmesh/level_mesh.h"
#include "sr3d3d9bc/ctab.h"
#include "sr3d3d9bc/disassembler.h"
#include "sr3d3d9bc/errors.h"
#include "sr3d3d9bc/hlsl_translator.h"
#include "sr3fxo/wrapper_header.h"
#include "sr3geometry/material_binding.h"
#include "sr3mesh/mesh_block.h"
#include "sr3render/mesh_renderer.h" // buildOrbitViewProjection/buildWorldMatrix/multiplyMatrix4x4
#include "sr3render/png_writer.h"
#include "sr3render/texture_upload.h"
#include "sr3tables_environment/tables.h" // real weather_time_of_day.xtbl row for the light pass
#include "sr3texture/texture_pair.h"
#include "vpp/container.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace {

// ===========================================================================
// Small generic helpers (identical to prototype_lit_car.cpp's own).
// ===========================================================================
std::string hrToString(HRESULT hr) {
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}
template <typename T>
void safeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}
bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView r = c.rawEntryBytes(i);
        out.assign(r.data(), r.data() + r.size());
        return true;
    }
    auto r = c.decompressEntry(i);
    bool ok = r.status == vpp::DecodeStatus::Ok || r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
              r.status == vpp::DecodeStatus::ContentValidated || r.status == vpp::DecodeStatus::RecoveredSharedStream ||
              r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!ok) return false;
    out = std::move(r.data);
    return true;
}
bool findEntry(const vpp::Container& c, const std::string& name, std::vector<uint8_t>& out) {
    for (size_t i = 0; i < c.entries().size(); ++i)
        if (c.entries()[i].name == name) return entryBytes(c, i, out);
    return false;
}
std::string lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}
bool containsCI(const std::string& haystack, const std::string& needleLower) {
    return lower(haystack).find(needleLower) != std::string::npos;
}
bool endsWithStr(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
bool endsWithNoCase(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
        char a = s[s.size() - suffix.size() + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}
size_t alignUp(size_t v, size_t a) { return (v + a - 1) / a * a; }

// ===========================================================================
// Plain (non-swapchain) D3D11 device - identical to prototype_lit_car.cpp's
// own.
// ===========================================================================
struct RenderDevice {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ID3D11Texture2D* target = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11Texture2D* staging = nullptr;
    uint32_t width = 0, height = 0;
    bool isWarp = false;

    ~RenderDevice() {
        safeRelease(staging);
        safeRelease(rtv);
        safeRelease(target);
        safeRelease(context);
        safeRelease(device);
    }

    bool initialise(uint32_t w, uint32_t h, std::string& error) {
        width = w; height = h;
        const D3D_FEATURE_LEVEL wanted[] = {D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_11_0;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, wanted, 1,
                                        D3D11_SDK_VERSION, &device, &got, &context);
        if (FAILED(hr)) {
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, wanted, 1, D3D11_SDK_VERSION,
                                    &device, &got, &context);
            if (FAILED(hr)) { error = "D3D11CreateDevice failed for both hardware and WARP: " + hrToString(hr); return false; }
            isWarp = true;
        }
        D3D11_TEXTURE2D_DESC td{};
        td.Width = width; td.Height = height; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        hr = device->CreateTexture2D(&td, nullptr, &target);
        if (FAILED(hr)) { error = "CreateTexture2D (target) failed: " + hrToString(hr); return false; }
        hr = device->CreateRenderTargetView(target, nullptr, &rtv);
        if (FAILED(hr)) { error = "CreateRenderTargetView failed: " + hrToString(hr); return false; }
        D3D11_TEXTURE2D_DESC sd = td;
        sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = device->CreateTexture2D(&sd, nullptr, &staging);
        if (FAILED(hr)) { error = "CreateTexture2D (staging) failed: " + hrToString(hr); return false; }
        return true;
    }
    bool readBack(std::vector<uint8_t>& rgba, std::string& error) {
        context->CopyResource(staging, target);
        D3D11_MAPPED_SUBRESOURCE m{};
        HRESULT hr = context->Map(staging, 0, D3D11_MAP_READ, 0, &m);
        if (FAILED(hr)) { error = "Map(staging) failed: " + hrToString(hr); return false; }
        rgba.resize(static_cast<size_t>(width) * height * 4);
        const uint8_t* src = static_cast<const uint8_t*>(m.pData);
        for (uint32_t y = 0; y < height; ++y)
            std::memcpy(rgba.data() + static_cast<size_t>(y) * width * 4, src + static_cast<size_t>(y) * m.RowPitch,
                        static_cast<size_t>(width) * 4);
        context->Unmap(staging, 0);
        return true;
    }
};

struct OffscreenTarget {
    ID3D11Texture2D* tex = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    bool create(ID3D11Device* dev, uint32_t w, uint32_t h, DXGI_FORMAT fmt, std::string& error) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = fmt; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = dev->CreateTexture2D(&td, nullptr, &tex);
        if (FAILED(hr)) { error = "CreateTexture2D failed: " + hrToString(hr); return false; }
        hr = dev->CreateRenderTargetView(tex, nullptr, &rtv);
        if (FAILED(hr)) { error = "CreateRenderTargetView failed: " + hrToString(hr); return false; }
        hr = dev->CreateShaderResourceView(tex, nullptr, &srv);
        if (FAILED(hr)) { error = "CreateShaderResourceView failed: " + hrToString(hr); return false; }
        return true;
    }
    void release() { safeRelease(srv); safeRelease(rtv); safeRelease(tex); }
};

struct OffscreenDepth {
    ID3D11Texture2D* tex = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    bool create(ID3D11Device* dev, uint32_t w, uint32_t h, std::string& error) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_D32_FLOAT; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        HRESULT hr = dev->CreateTexture2D(&td, nullptr, &tex);
        if (FAILED(hr)) { error = "CreateTexture2D(depth) failed: " + hrToString(hr); return false; }
        hr = dev->CreateDepthStencilView(tex, nullptr, &dsv);
        if (FAILED(hr)) { error = "CreateDepthStencilView failed: " + hrToString(hr); return false; }
        return true;
    }
    void release() { safeRelease(dsv); safeRelease(tex); }
};

// NEW in this fork: a diagnostic readback of an intermediate off-screen
// target (G-buffer normals, light-accumulation buffer, ...), printed as the
// SAME per-channel min/max/mean stats the final composited image gets -
// concrete, measured evidence (not just an assertion) that the pipeline's
// upstream stages are genuinely non-trivial even where the FINAL per-
// material colour comes out flat black (see this file's own top comment).
void printTargetStats(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* tex, uint32_t w, uint32_t h,
                       const char* label) {
    D3D11_TEXTURE2D_DESC td{}; tex->GetDesc(&td);
    D3D11_TEXTURE2D_DESC sd = td;
    sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    if (FAILED(device->CreateTexture2D(&sd, nullptr, &staging))) { printf("  [%s] staging texture creation failed\n", label); return; }
    context->CopyResource(staging, tex);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &m))) { printf("  [%s] Map failed\n", label); staging->Release(); return; }
    uint64_t sum[4] = {0,0,0,0}; uint8_t minV[4] = {255,255,255,255}, maxV[4] = {0,0,0,0};
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* row = static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch;
        for (uint32_t x = 0; x < w; ++x)
            for (int c = 0; c < 4; ++c) { uint8_t v = row[x*4+c]; sum[c]+=v; if (v<minV[c]) minV[c]=v; if (v>maxV[c]) maxV[c]=v; }
    }
    context->Unmap(staging, 0);
    staging->Release();
    size_t n = static_cast<size_t>(w) * h;
    printf("  [%s] R:min=%u,max=%u,mean=%.2f  G:min=%u,max=%u,mean=%.2f  B:min=%u,max=%u,mean=%.2f  A:min=%u,max=%u,mean=%.2f\n",
           label, minV[0], maxV[0], n ? (double)sum[0]/n : 0.0, minV[1], maxV[1], n ? (double)sum[1]/n : 0.0,
           minV[2], maxV[2], n ? (double)sum[2]/n : 0.0, minV[3], maxV[3], n ? (double)sum[3]/n : 0.0);
}

// ===========================================================================
// Real DCL-derived vertex-input signature (identical to prototype_lit_car
// .cpp's own).
// ===========================================================================
constexpr uint8_t kPOSITION = 0, kBLENDWEIGHT = 1, kBLENDINDICES = 2, kNORMAL = 3, kTEXCOORD = 5, kTANGENT = 6;
const char* usageName(uint8_t u) {
    switch (u) {
        case kPOSITION: return "POSITION"; case kBLENDWEIGHT: return "BLENDWEIGHT";
        case kBLENDINDICES: return "BLENDINDICES"; case kNORMAL: return "NORMAL";
        case kTEXCOORD: return "TEXCOORD"; case kTANGENT: return "TANGENT"; default: return "?";
    }
}
struct VsInputField { uint16_t reg; uint8_t usage; uint8_t usageIndex; };
std::vector<VsInputField> extractVsInputSignature(const sr3d3d9bc::DisassembledShader& d) {
    std::vector<VsInputField> out;
    for (const auto& inst : d.instructions) {
        if (inst.opcode != sr3d3d9bc::Opcode::DCL) continue;
        if (!inst.dcl.has_value() || !inst.dest.has_value()) continue;
        if (inst.dest->registerTypeRaw != 1) continue;
        out.push_back({inst.dest->registerNumber, inst.dcl->usage, inst.dcl->usageIndex});
    }
    std::stable_sort(out.begin(), out.end(), [](const VsInputField& a, const VsInputField& b) { return a.reg < b.reg; });
    return out;
}
std::string signatureToString(const std::vector<VsInputField>& sig) {
    std::string s;
    for (const auto& f : sig) { s += usageName(f.usage); s += std::to_string(f.usageIndex); s += " "; }
    return s;
}

// ===========================================================================
// Matrix helpers - identical to prototype_lit_car.cpp's own.
// ===========================================================================
void packColumn(const float M[16], int col, float out4[4]) {
    out4[0] = M[0 * 4 + col]; out4[1] = M[1 * 4 + col]; out4[2] = M[2 * 4 + col]; out4[3] = M[3 * 4 + col];
}
void computeOrbitViewMatrix(const float boundsMin[3], const float boundsMax[3], float yaw, float pitch,
                             float distanceScale, float outView[16]) {
    float centre[3]; float extent = 0.0f;
    for (int i = 0; i < 3; ++i) {
        centre[i] = (boundsMin[i] + boundsMax[i]) * 0.5f;
        float span = boundsMax[i] - boundsMin[i];
        if (span > extent) extent = span;
    }
    if (extent <= 0.0f) extent = 1.0f;
    const float distance = extent * distanceScale;
    float eye[3] = {centre[0] + distance * std::cos(pitch) * std::sin(yaw), centre[1] + distance * std::sin(pitch),
                     centre[2] + distance * std::cos(pitch) * std::cos(yaw)};
    float forward[3] = {centre[0] - eye[0], centre[1] - eye[1], centre[2] - eye[2]};
    float flen = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
    if (flen <= 0.0f) flen = 1.0f;
    for (float& f : forward) f /= flen;
    const float worldUp[3] = {0, 1, 0};
    float right[3] = {forward[1] * worldUp[2] - forward[2] * worldUp[1], forward[2] * worldUp[0] - forward[0] * worldUp[2],
                       forward[0] * worldUp[1] - forward[1] * worldUp[0]};
    float rlen = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    if (rlen <= 0.0f) rlen = 1.0f;
    for (float& r : right) r /= rlen;
    float up[3] = {right[1] * forward[2] - right[2] * forward[1], right[2] * forward[0] - right[0] * forward[2],
                   right[0] * forward[1] - right[1] * forward[0]};
    float view[16] = {right[0], up[0], forward[0], 0, right[1], up[1], forward[1], 0,
                       right[2], up[2], forward[2], 0,
                       -(right[0]*eye[0]+right[1]*eye[1]+right[2]*eye[2]),
                       -(up[0]*eye[0]+up[1]*eye[1]+up[2]*eye[2]),
                       -(forward[0]*eye[0]+forward[1]*eye[1]+forward[2]*eye[2]), 1};
    std::memcpy(outView, view, sizeof(view));
}
void buildPerspectiveLH(float fovYRadians, float aspect, float nearZ, float farZ, float out[16]) {
    float yScale = 1.0f / std::tan(fovYRadians * 0.5f);
    float xScale = yScale / aspect;
    float M[16] = {
        xScale, 0, 0, 0,
        0, yScale, 0, 0,
        0, 0, farZ / (farZ - nearZ), 1,
        0, 0, -nearZ * farZ / (farZ - nearZ), 0,
    };
    std::memcpy(out, M, sizeof(M));
}
bool invert4x4(const float m[16], float out[16]) {
    float inv[16];
    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (std::fabs(det) < 1e-12f) {
        for (int i = 0; i < 16; ++i) out[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        return false;
    }
    det = 1.0f / det;
    for (int i = 0; i < 16; ++i) out[i] = inv[i] * det;
    return true;
}

struct ConstantFillLog { std::string constName; std::string decision; };
void fillFloat4Registers(std::vector<uint8_t>& cpuBuffer, uint16_t registerIndex, uint16_t registerCount,
                          const std::vector<std::array<float, 4>>& regs) {
    for (uint16_t r = 0; r < registerCount && r < regs.size(); ++r) {
        size_t byteOff = static_cast<size_t>(registerIndex + r) * 16;
        if (byteOff + 16 > cpuBuffer.size()) continue;
        std::memcpy(cpuBuffer.data() + byteOff, regs[r].data(), 16);
    }
}

// GPU vertex layout - identical to prototype_lit_car.cpp's own (a static
// prop's real channel decode goes through the SAME sr3mesh::Vertex shape a
// vehicle's does - shared reader, spec-vertex-format.md).
struct GpuVertexReal { float position[4], normal[4], tangent[4], blendIndices[4], texcoord0[4]; };
const D3D11_INPUT_ELEMENT_DESC kFullLayout[5] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, normal), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, tangent), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, blendIndices), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, texcoord0), D3D11_INPUT_PER_VERTEX_DATA, 0},
};
struct GpuVertexQuad { float position[4], texcoord0[4]; };
const D3D11_INPUT_ELEMENT_DESC kQuadLayout[2] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexQuad, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexQuad, texcoord0), D3D11_INPUT_PER_VERTEX_DATA, 0},
};

// ===========================================================================
// real shaderHash -> real stem -> real vertex-stage .fxo_pc file (identical
// to prototype_lit_car.cpp's own - see spec-fxo-format.md Sec6.6 for the
// real 13-suffix stripping rule this implements).
// ===========================================================================
const std::vector<std::string> kStageSuffixes = {
    "_bms", "_bmc", "_bs", "_bc", "_ms", "_mc", "_mv", "_ts", "_fd", "_s", "_c", "_t", "_v",
};
std::string stripStageSuffix(const std::string& lowerNoExt) {
    for (const auto& suf : kStageSuffixes)
        if (endsWithStr(lowerNoExt, suf)) return lowerNoExt.substr(0, lowerNoExt.size() - suf.size());
    return lowerNoExt;
}
void collectFxoStems(const vpp::Container& c, std::map<std::string, std::vector<std::string>>& stemToFiles) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        if (endsWithStr(lower(n), ".fxo_pc")) {
            std::string noExt = lower(n).substr(0, n.size() - 7);
            stemToFiles[stripStageSuffix(noExt)].push_back(n);
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { collectFxoStems(c.openNested(i), stemToFiles); } catch (const std::exception&) {}
        }
    }
}
std::string pickVertexFile(const std::vector<std::string>& files, std::string& reasonIfNone) {
    for (const auto& f : files) if (endsWithStr(lower(f), "_v.fxo_pc")) return f;
    for (const auto& f : files) if (endsWithStr(lower(f), "_mv.fxo_pc")) return f;
    reasonIfNone = "stem resolved but has no _v/_mv real file among:";
    for (const auto& f : files) reasonIfNone += " " + f;
    return "";
}

// NEW IN THIS FORK (Lead 1 - see top comment): the try-order this fork
// actually resolves a stem's shader file through. `_v`/`_mv` first,
// UNCHANGED precedent (zero behaviour change for the 2 already-drawable
// materialIds); then every OTHER real candidate file for the stem, in
// `files`' own order (container on-disk order, stable/deterministic - see
// collectFxoStems()). CHOSEN, documented: this is a plain, auditable
// try-order, not a semantic ranking of the suffixes (`_bs`/`_c`/`_fd`/
// `_mc`/`_ms`/`_s` - spec-fxo-format.md Sec6.6 names them only as
// class/variant slots in a 400-entry registration library, and does not
// say what each class MEANS; decoding that is a separate investigation
// this task does not require, since - measured this session,
// tools/clmesh_lead_probe.cpp - EVERY real candidate for all 8 of this
// tower's non-suffixed stems structurally pairs role4 AND role6 equally,
// so which one is tried first only matters if an earlier candidate fails
// to actually COMPILE, which this fork's own try-loop (below, where this
// order is consumed) handles by falling through, not by guessing which
// suffix is "correct").
std::vector<std::string> candidateTryOrder(const std::vector<std::string>& files) {
    std::vector<std::string> order;
    for (const auto& f : files) if (endsWithStr(lower(f), "_v.fxo_pc")) order.push_back(f);
    for (const auto& f : files) if (endsWithStr(lower(f), "_mv.fxo_pc")) order.push_back(f);
    for (const auto& f : files) {
        bool already = false;
        for (const auto& t : order) if (t == f) { already = true; break; }
        if (!already) order.push_back(f);
    }
    return order;
}

// ===========================================================================
// Real T8 VS<->PS pairing BY REAL ROLE NUMBER - identical to
// prototype_lit_car.cpp's own (already generalized to "whatever pass index
// this file's own header ACTUALLY declares for role R", not hardcoded to
// any one shader family).
// ===========================================================================
struct BlobPairResult {
    bool ok = false;
    std::string reason;
    size_t vsOffset = 0, vsLength = 0, psOffset = 0, psLength = 0;
    int passIndex = -1;
};
BlobPairResult pairVsPsForRole(const std::vector<uint8_t>& fxoBytes, size_t role) {
    BlobPairResult r;
    sr3fxo::WrapperHeader wh;
    std::string why;
    vpp::ByteView view(fxoBytes.data(), fxoBytes.size());
    if (!sr3fxo::WrapperHeader::tryParse(view, wh, why)) { r.reason = "WrapperHeader::tryParse failed: " + why; return r; }
    if (!wh.rolePresent(role)) { r.reason = "real role table: role " + std::to_string(role) + " not present (flags=0x" +
                                             hrToString(wh.flags()) + ")"; return r; }
    int8_t passIdx = wh.roleIndexByte(role);
    if (passIdx < 0 || static_cast<size_t>(passIdx) >= wh.passes().size()) {
        r.reason = "real role table: role " + std::to_string(role) + " roleIndexByte=" + std::to_string(passIdx) +
                   " out of range (passCount=" + std::to_string(wh.passes().size()) + ")";
        return r;
    }
    r.passIndex = passIdx;
    const sr3fxo::PassEntry& p = wh.passes()[static_cast<size_t>(passIdx)];
    size_t endOfBlobs = 0;
    std::vector<sr3fxo::WrapperBlob> blobs = wh.layoutBlobs(endOfBlobs);
    const sr3fxo::WrapperBlob* vsBlob = nullptr;
    const sr3fxo::WrapperBlob* psBlob = nullptr;
    for (const auto& b : blobs) {
        if (b.stage == sr3fxo::Stage::Vertex && static_cast<int16_t>(b.tableIndex) == p.vertexIndex) vsBlob = &b;
        if (b.stage == sr3fxo::Stage::Pixel && static_cast<int16_t>(b.tableIndex) == p.pixelIndex) psBlob = &b;
    }
    if (!vsBlob) { r.reason = "pass " + std::to_string(passIdx) + " (role " + std::to_string(role) + ")'s vertexIndex has no real non-empty VS blob"; return r; }
    if (!psBlob) { r.reason = "pass " + std::to_string(passIdx) + " (role " + std::to_string(role) + ")'s pixelIndex has no real non-empty PS blob"; return r; }
    r.vsOffset = vsBlob->offset; r.vsLength = vsBlob->length;
    r.psOffset = psBlob->offset; r.psLength = psBlob->length;
    r.ok = true;
    return r;
}

// NEW IN THIS FORK: cheap (no D3DCompile, header-parse only) structural
// preview - "does SOME real candidate file for this stem carry a real
// role4 AND role6 T8 VS+PS pairing" - used only for the early per-material
// report (step 3 below), before the device exists. The AUTHORITATIVE
// drawable decision is the real compiled one (step 5/6, which tries the
// SAME candidates through the full compileOnePass()+GPU-object-creation
// pipeline and can reject a structurally-fine-but-untranslatable
// candidate) - this is a preview, labelled as such in its own printf.
bool structuralPairOk(const vpp::Container& shadersContainer, const std::vector<std::string>& candidates) {
    for (const auto& f : candidates) {
        std::vector<uint8_t> fxoBytes;
        if (!findEntry(shadersContainer, f, fxoBytes)) continue;
        if (pairVsPsForRole(fxoBytes, 4).ok && pairVsPsForRole(fxoBytes, 6).ok) return true;
    }
    return false;
}

// ===========================================================================
// One fully resolved+compiled distinct shader PASS. Extends
// prototype_lit_car.cpp's own CompiledShader with 3 NEW real-sampler-name
// fields (normalMapSamplerReg/diffuseMapSamplerReg/specularMapSamplerReg) -
// this building-shader family declares real Normal_MapSampler/Diffuse_Map
// Sampler/Specular_MapSampler CTAB names car paint's role4/role6 never did
// (car paint has no diffuse texture at all - see this file's own top
// comment), so real per-material textures can be bound to them.
// ===========================================================================
struct CompiledShader {
    std::string stem, vsFileName;
    int roleUsed = -1, passIndexUsed = -1;
    bool ok = false;
    std::string failReason;

    sr3d3d9bc::DisassembledShader vsDis, psDis;
    sr3d3d9bc::ConstantTable vsCtab, psCtab;
    sr3d3d9bc::TranslationResult vsTr, psTr;
    std::vector<VsInputField> vsInputs;

    ID3DBlob* vsCode = nullptr;
    ID3DBlob* psCode = nullptr;
    ID3D11ShaderReflection* vsRefl = nullptr;
    ID3D11ShaderReflection* psRefl = nullptr;
    UINT vsCbSize = 0, psCbSize = 0;
    UINT psOutputCount = 0;

    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* inputLayout = nullptr;
    ID3D11Buffer* cbVs = nullptr;
    ID3D11Buffer* cbPs = nullptr;
    std::vector<uint8_t> vsCbufCpu, psCbufCpu;

    int lBufferSamplerReg = -1;     // s-register of IR_LBufferSampler, if this PS declares it
    int normalsSamplerReg = -1;     // s-register of IR_GBuffer_NormalsSampler (light shader only)
    int lightingSamplerReg = -1;    // s-register of IR_GBuffer_LightingSampler (light shader only)
    int normalMapSamplerReg = -1;   // NEW: s-register of this material's real Normal_MapSampler (role4)
    int diffuseMapSamplerReg = -1;  // NEW: s-register of this material's real Diffuse_MapSampler (role6)
    int specularMapSamplerReg = -1; // NEW: s-register of this material's real Specular_MapSampler (role6)
    std::vector<UINT> allTexBindPoints;
    std::vector<UINT> allSamplerBindPoints;
    size_t rangesUsing = 0;
};

CompiledShader compileOnePass(ID3D11Device* device, const std::vector<uint8_t>& fxoBytes, const std::string& vsFileName,
                               const std::string& stem, size_t role) {
    CompiledShader cs;
    cs.vsFileName = vsFileName; cs.stem = stem; cs.roleUsed = static_cast<int>(role);
    BlobPairResult pair = pairVsPsForRole(fxoBytes, role);
    if (!pair.ok) { cs.failReason = "role " + std::to_string(role) + " pairing failed: " + pair.reason; return cs; }
    cs.passIndexUsed = pair.passIndex;

    vpp::ByteView vsBlobView(fxoBytes.data() + pair.vsOffset, pair.vsLength);
    vpp::ByteView psBlobView(fxoBytes.data() + pair.psOffset, pair.psLength);
    cs.vsDis = sr3d3d9bc::disassemble(vsBlobView);
    cs.psDis = sr3d3d9bc::disassemble(psBlobView);
    cs.vsCtab = sr3d3d9bc::readConstantTable(vsBlobView, cs.vsDis);
    cs.psCtab = sr3d3d9bc::readConstantTable(psBlobView, cs.psDis);
    cs.vsTr = sr3d3d9bc::translateToHlsl(cs.vsDis, cs.vsCtab, vsBlobView, sr3d3d9bc::HlslTarget::SM4_5);
    cs.psTr = sr3d3d9bc::translateToHlsl(cs.psDis, cs.psCtab, psBlobView, sr3d3d9bc::HlslTarget::SM4_5);
    cs.vsInputs = extractVsInputSignature(cs.vsDis);

    for (const auto& c : cs.psCtab.constants) {
        if (c.registerSet != sr3d3d9bc::RegisterSet::Sampler) continue;
        if (containsCI(c.name, "ir_lbuffersampler")) cs.lBufferSamplerReg = c.registerIndex;
        if (containsCI(c.name, "ir_gbuffer_normalssampler")) cs.normalsSamplerReg = c.registerIndex;
        if (containsCI(c.name, "ir_gbuffer_lightingsampler")) cs.lightingSamplerReg = c.registerIndex;
        // NEW real sampler-name matches this building-shader family declares
        // (car paint's role4/role6 never did - see top comment).
        if (containsCI(c.name, "normal_mapsampler")) cs.normalMapSamplerReg = c.registerIndex;
        if (containsCI(c.name, "diffuse_mapsampler")) cs.diffuseMapSamplerReg = c.registerIndex;
        if (containsCI(c.name, "specular_mapsampler")) cs.specularMapSamplerReg = c.registerIndex;
    }

    ID3DBlob *vsErr = nullptr, *psErr = nullptr;
    HRESULT hr = D3DCompile(cs.vsTr.hlsl.c_str(), cs.vsTr.hlsl.size(), vsFileName.c_str(), nullptr, nullptr,
                             cs.vsTr.entryPoint.c_str(), cs.vsTr.targetProfile.c_str(), D3DCOMPILE_SKIP_OPTIMIZATION, 0,
                             &cs.vsCode, &vsErr);
    if (FAILED(hr)) {
        cs.failReason = "D3DCompile(VS) failed: " + hrToString(hr);
        if (vsErr) cs.failReason += std::string(" - ") + static_cast<const char*>(vsErr->GetBufferPointer());
        safeRelease(vsErr);
        return cs;
    }
    safeRelease(vsErr);
    hr = D3DCompile(cs.psTr.hlsl.c_str(), cs.psTr.hlsl.size(), (vsFileName + "_ps" + std::to_string(role)).c_str(),
                     nullptr, nullptr, cs.psTr.entryPoint.c_str(), cs.psTr.targetProfile.c_str(),
                     D3DCOMPILE_SKIP_OPTIMIZATION, 0, &cs.psCode, &psErr);
    if (FAILED(hr)) {
        cs.failReason = "D3DCompile(PS) failed: " + hrToString(hr);
        if (psErr) cs.failReason += std::string(" - ") + static_cast<const char*>(psErr->GetBufferPointer());
        safeRelease(psErr);
        return cs;
    }
    safeRelease(psErr);

    D3DReflect(cs.vsCode->GetBufferPointer(), cs.vsCode->GetBufferSize(), __uuidof(ID3D11ShaderReflection),
               reinterpret_cast<void**>(&cs.vsRefl));
    D3DReflect(cs.psCode->GetBufferPointer(), cs.psCode->GetBufferSize(), __uuidof(ID3D11ShaderReflection),
               reinterpret_cast<void**>(&cs.psRefl));
    if (!cs.vsRefl || !cs.psRefl) { cs.failReason = "D3DReflect failed"; return cs; }
    { ID3D11ShaderReflectionConstantBuffer* cb = cs.vsRefl->GetConstantBufferByIndex(0); D3D11_SHADER_BUFFER_DESC bd{};
      if (cb) cb->GetDesc(&bd); cs.vsCbSize = bd.Size; }
    { ID3D11ShaderReflectionConstantBuffer* cb = cs.psRefl->GetConstantBufferByIndex(0); D3D11_SHADER_BUFFER_DESC bd{};
      if (cb) cb->GetDesc(&bd); cs.psCbSize = bd.Size; }
    D3D11_SHADER_DESC psDesc{};
    cs.psRefl->GetDesc(&psDesc);
    cs.psOutputCount = psDesc.OutputParameters;
    for (UINT r = 0; r < psDesc.BoundResources; ++r) {
        D3D11_SHADER_INPUT_BIND_DESC bind{};
        cs.psRefl->GetResourceBindingDesc(r, &bind);
        if (bind.Type == D3D_SIT_TEXTURE) cs.allTexBindPoints.push_back(bind.BindPoint);
        else if (bind.Type == D3D_SIT_SAMPLER) cs.allSamplerBindPoints.push_back(bind.BindPoint);
    }

    // Subset check identical to prototype_lit_car.cpp's own: any subset of
    // {POSITION0,NORMAL0,TANGENT0,BLENDINDICES0,TEXCOORD0}, OR the real
    // light-shader signature {POSITION0,TEXCOORD0}. Measured this session:
    // ir_bbsimple1's real role4/role6 VS input DCLs are subsets of the SAME
    // known set (register ORDER differs from car paint's, but D3D11
    // CreateInputLayout matches by semantic name+index, not register order,
    // so kFullLayout below needs no change for that).
    auto hasUsage = [&](uint8_t usage, uint8_t idx) {
        for (const auto& f : cs.vsInputs) if (f.usage == usage && f.usageIndex == idx) return true;
        return false;
    };
    bool isQuadShape = cs.vsInputs.size() == 2 && hasUsage(kPOSITION, 0) && hasUsage(kTEXCOORD, 0);
    bool knownSubset = hasUsage(kPOSITION, 0);
    for (const auto& f : cs.vsInputs) {
        bool known = (f.usage == kPOSITION && f.usageIndex == 0) || (f.usage == kNORMAL && f.usageIndex == 0) ||
                     (f.usage == kTANGENT && f.usageIndex == 0) || (f.usage == kBLENDINDICES && f.usageIndex == 0) ||
                     (f.usage == kTEXCOORD && f.usageIndex == 0);
        if (!known) knownSubset = false;
    }
    if (!isQuadShape && !knownSubset) {
        cs.failReason = "real VS input signature (" + signatureToString(cs.vsInputs) + ") is neither the known "
                         "subset nor the known {POSITION0,TEXCOORD0} light-quad signature - not forced to fit";
        return cs;
    }

    cs.ok = true;
    return cs;
}

// ===========================================================================
// Real per-material texture upload machinery - copied VERBATIM from
// tools/sr3_viewer.cpp (TextureSearchCache/buildTextureSearchCache/
// findAndUploadTexture, its own runClmesh()/runClmeshTile() mechanism) since
// this project's prototype_*.cpp family never had a need for it before (car
// paint has no diffuse texture at all). Reused, not reinvented - see that
// file's own comments for texCache's real cause (a many-material prop's
// naive per-lookup whole-archive walk measured in the multiple-minutes
// range against sr3_city_0.vpp_pc without it).
// ===========================================================================
struct TextureSearchCache {
    bool built = false;
    struct Located {
        std::shared_ptr<std::vector<uint8_t>> cpegBytes;
        std::shared_ptr<std::vector<uint8_t>> gpegBytes;
        sr3texture::TexturePair pair;
        size_t recordIndex = 0;
        std::string recordNameLower;
        std::string foundIn;
    };
    std::vector<Located> all;
    std::map<std::string, sr3render::UploadedTexture> uploadedByWantedName;
};
void buildTextureSearchCache(const vpp::Container& container, TextureSearchCache& cache) {
    for (size_t i = 0; i < container.entries().size(); ++i) {
        const std::string& name = container.entries()[i].name;
        if (endsWithNoCase(name, ".cpeg_pc") || endsWithNoCase(name, ".cvbm_pc")) {
            std::vector<uint8_t> cbytes;
            if (entryBytes(container, i, cbytes) && !cbytes.empty()) {
                try {
                    auto cbytesPtr = std::make_shared<std::vector<uint8_t>>(std::move(cbytes));
                    sr3texture::TexturePair pair = sr3texture::TexturePair::parse(
                        sr3texture::ByteView(cbytesPtr->data(), cbytesPtr->size()));
                    std::string gname = name;
                    size_t dot = gname.find_last_of('.');
                    if (dot != std::string::npos) gname[dot + 1] = 'g';
                    std::vector<uint8_t> gbytes;
                    for (size_t j = 0; j < container.entries().size(); ++j) {
                        if (container.entries()[j].name == gname) {
                            entryBytes(container, j, gbytes);
                            break;
                        }
                    }
                    if (!gbytes.empty()) {
                        auto gbytesPtr = std::make_shared<std::vector<uint8_t>>(std::move(gbytes));
                        for (size_t r = 0; r < pair.records().size(); ++r) {
                            TextureSearchCache::Located loc;
                            loc.cpegBytes = cbytesPtr;
                            loc.gpegBytes = gbytesPtr;
                            loc.pair = pair;
                            loc.recordIndex = r;
                            loc.recordNameLower = lower(pair.records()[r].name);
                            loc.foundIn = name;
                            cache.all.push_back(std::move(loc));
                        }
                    }
                } catch (const std::exception&) {
                }
            }
        }
        if (container.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try {
                vpp::Container nested = container.openNested(i);
                buildTextureSearchCache(nested, cache);
            } catch (const std::exception&) {
            }
        }
    }
}
bool findAndUploadTexture(const vpp::Container& container, const std::string& tgaName,
                          ID3D11Device* device, sr3render::UploadedTexture& out,
                          std::string& foundIn, TextureSearchCache* cache = nullptr) {
    if (cache != nullptr) {
        if (!cache->built) {
            buildTextureSearchCache(container, *cache);
            cache->built = true;
        }
        const std::string wantedLower = lower(tgaName);
        auto memoIt = cache->uploadedByWantedName.find(wantedLower);
        if (memoIt != cache->uploadedByWantedName.end()) {
            out = memoIt->second;
            if (out.srv == nullptr) return false;
            for (const auto& loc : cache->all) {
                if (endsWithNoCase(loc.recordNameLower, wantedLower)) { foundIn = loc.foundIn; break; }
            }
            return true;
        }
        for (const auto& loc : cache->all) {
            if (!endsWithNoCase(loc.recordNameLower, wantedLower)) continue;
            std::string error;
            if (sr3render::uploadTexture(device, loc.pair, loc.recordIndex,
                                         sr3texture::ByteView(loc.gpegBytes->data(), loc.gpegBytes->size()), out,
                                         error)) {
                foundIn = loc.foundIn;
                cache->uploadedByWantedName[wantedLower] = out;
                return true;
            }
        }
        sr3render::UploadedTexture notFound;
        cache->uploadedByWantedName[wantedLower] = notFound;
        return false;
    }
    for (size_t i = 0; i < container.entries().size(); ++i) {
        const std::string& name = container.entries()[i].name;
        if (endsWithNoCase(name, ".cpeg_pc") || endsWithNoCase(name, ".cvbm_pc")) {
            std::vector<uint8_t> cbytes;
            if (!entryBytes(container, i, cbytes) || cbytes.empty()) continue;
            try {
                sr3texture::TexturePair pair = sr3texture::TexturePair::parse(
                    sr3texture::ByteView(cbytes.data(), cbytes.size()));
                for (size_t r = 0; r < pair.records().size(); ++r) {
                    if (!endsWithNoCase(pair.records()[r].name, tgaName)) continue;
                    std::string gname = name;
                    size_t dot = gname.find_last_of('.');
                    if (dot != std::string::npos) gname[dot + 1] = 'g';
                    std::vector<uint8_t> gbytes;
                    for (size_t j = 0; j < container.entries().size(); ++j) {
                        if (container.entries()[j].name == gname) {
                            entryBytes(container, j, gbytes);
                            break;
                        }
                    }
                    if (gbytes.empty()) continue;
                    std::string error;
                    if (sr3render::uploadTexture(device, pair, r,
                                                 sr3texture::ByteView(gbytes.data(), gbytes.size()),
                                                 out, error)) {
                        foundIn = name;
                        return true;
                    }
                }
            } catch (const std::exception&) {
            }
        }
        if (container.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try {
                vpp::Container nested = container.openNested(i);
                if (findAndUploadTexture(nested, tgaName, device, out, foundIn)) return true;
            } catch (const std::exception&) {
            }
        }
    }
    return false;
}

// MEASURED this session (2026-09-30), NOT an existing sr3geometry named
// constant: real specular-map role hash seen on airport_controltower's own
// real texture-binding dump (e.g. material[7] slot=2 'metal_suports_
// skyscraper_s.tga', material[11] slot=2 'ap_hangar02_a_door02_s.tga').
// kParamHashNormalMap/kParamHashDiffuseA/kParamHashDiffuseB already live in
// sr3geometry/material_binding.h; this one does not yet, so it stays local
// to this file rather than editing that shared header for one task-scoped
// finding.
constexpr uint32_t kParamHashSpecularClmesh = 0xE848C9CAu;

} // namespace

// ===========================================================================
// main()
// ===========================================================================
int main(int argc, char** argv) {
    const std::string kCacheDir = "D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache";
    std::vector<ConstantFillLog> fillLog;
    const uint32_t kW = 800, kH = 600; // CHOSEN, same as every tool in this family
    const DXGI_FORMAT kGBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM; // CHOSEN, not confirmed real
    // CHOSEN default output path (this task's own target, hardcoded like
    // every other prototype_*.cpp in this family hardcodes its own real
    // subject); argv[1], when given, overrides it - the ONE command-line
    // affordance this fork adds, needed so tests/golden/golden_scene_check
    // .cpp can capture into its own _work/ directory without this tool
    // otherwise taking any arguments, matching tree_baseline_render.cpp's
    // own "positional out-path" convention for a golden-wired tool.
    std::string outPath = "D:/Project Crreish/TEAM B/tools/prototype_lit_clmesh_tower2_output.png";
    if (argc >= 2) outPath = argv[1];

    printf("=== Lit-clmesh prototype: real G-buffer prepass -> real directional light -> real material pass ===\n");
    printf("(forked from prototype_lit_car.cpp - see this file's own top comment)\n\n");

    // -----------------------------------------------------------------
    // 1. Real stem -> .fxo_pc table (identical mechanism to
    //    prototype_lit_car.cpp's own step 1).
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

    // -----------------------------------------------------------------
    // 1b. Open sr3_city_0.vpp_pc once and locate 1018h0.str2_pc (the same
    //     real "Zone (High LOD)" fine-cell container the existing
    //     clmesh_airport_controltower/clmesh_1018h0_props golden scenes
    //     already use) - reused for BOTH the census below and the tower's
    //     own geometry/material load, so this 1.5 GB archive is read and
    //     decompressed only once per run.
    // -----------------------------------------------------------------
    std::vector<uint8_t> cityArchive = readFile(kCacheDir + "/sr3_city_0.vpp_pc");
    if (cityArchive.empty()) { printf("FATAL: could not read sr3_city_0.vpp_pc\n"); return 1; }
    vpp::Container cityContainer{vpp::ByteView(cityArchive.data(), cityArchive.size())};
    size_t h0Index = SIZE_MAX;
    for (size_t i = 0; i < cityContainer.entries().size(); ++i) {
        if (lower(cityContainer.entries()[i].name) == "1018h0.str2_pc") { h0Index = i; break; }
    }
    if (h0Index == SIZE_MAX) { printf("FATAL: 1018h0.str2_pc not found at top level of sr3_city_0.vpp_pc\n"); return 1; }
    vpp::Container h0Container = cityContainer.openNested(h0Index);
    printf("[archive] sr3_city_0.vpp_pc :: 1018h0.str2_pc opened, %zu entries\n\n", h0Container.entries().size());

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

    // -----------------------------------------------------------------
    // 3. Real airport_controltower.clmesh_pc/.glmesh_pc load, via
    //    sr3clmesh::LevelMesh (this task's own required reader - the
    //    ALREADY-PROVEN mechanism HANDOFF Sec9.133 established, unchanged
    //    here), from the SAME h0Container opened in step 1b above.
    // -----------------------------------------------------------------
    std::vector<uint8_t> clBytes, glBytes;
    {
        size_t clIdx = SIZE_MAX, glIdx = SIZE_MAX;
        for (size_t i = 0; i < h0Container.entries().size(); ++i) {
            const std::string& n = h0Container.entries()[i].name;
            if (lower(n) == "airport_controltower.clmesh_pc") clIdx = i;
            if (lower(n) == "airport_controltower.glmesh_pc") glIdx = i;
        }
        if (clIdx == SIZE_MAX || glIdx == SIZE_MAX) {
            printf("FATAL: airport_controltower.clmesh_pc/.glmesh_pc not found in 1018h0.str2_pc\n");
            return 1;
        }
        if (!entryBytes(h0Container, clIdx, clBytes) || !entryBytes(h0Container, glIdx, glBytes)) {
            printf("FATAL: could not read airport_controltower.clmesh_pc/.glmesh_pc bytes\n");
            return 1;
        }
    }
    printf("[clmesh] airport_controltower.clmesh_pc (%zu bytes) + .glmesh_pc (%zu bytes)\n", clBytes.size(),
           glBytes.size());
    vpp::ByteView clView(clBytes.data(), clBytes.size());
    vpp::ByteView glView(glBytes.data(), glBytes.size());
    sr3clmesh::LevelMesh lm;
    try { lm = sr3clmesh::LevelMesh::parse(clView); }
    catch (const std::exception& ex) { printf("FATAL: LevelMesh parse failed: %s\n", ex.what()); return 1; }
    if (!lm.walkComplete()) { printf("FATAL: LevelMesh's own computed walk did not complete\n"); return 1; }
    const auto& mid = lm.middle();
    printf("[clmesh] materialCount=%u renderGroupCount=%zu landsOnEof=%d\n", mid.materialCount, mid.renderGroupCount,
           lm.landsOnEof() ? 1 : 0);

    // Real per-material shaderHash + texture-binding dump (same shape
    // runClmesh() already prints, for report parity) - and, per this
    // material, whether it resolves to a DRAWABLE real stem (has a real
    // per-stem _v/_mv vertex file) vs a resolved-but-not-drawable one
    // (real stem exists, no real per-stem VS blob reachable) vs an actual
    // hash0 miss (none, this prop - see the census above).
    std::vector<sr3clmesh::MaterialRecord> matRecs = lm.materialRecords(clView);
    std::vector<sr3geometry::MaterialBinding> matBindings(matRecs.size());
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
    size_t gCursor = 0;
    for (auto& hb : headBlocks) if (hb.bulkInGFile()) gCursor += hb.gLength();

    sr3mesh::MeshBlock chosenMesh;
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
    if (!haveChosenMesh) { printf("FATAL: renderGroup %d could not be parsed\n", renderGroupArg); return 1; }
    if (!chosenMesh.drawGroupsLocated() || chosenMesh.drawGroups().empty()) {
        printf("FATAL: renderGroup %d has no located draw groups\n", renderGroupArg);
        return 1;
    }
    const auto& drawGroup0 = chosenMesh.drawGroups()[0];
    printf("[renderGroup %d] channels=%zu indexCount=%u ranges=%zu\n\n", renderGroupArg, chosenMesh.channels().size(),
           chosenMesh.indexCount(), drawGroup0.size());

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
    RenderDevice dev;
    std::string devErr;
    if (!dev.initialise(kW, kH, devErr)) { printf("FATAL device: %s\n", devErr.c_str()); return 1; }
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

    struct RangeResolution {
        size_t rangeIdx = 0; uint32_t materialId = 0, submeshIndex = 0, shaderHash = 0;
        bool ok = false; std::string reason, stem, vsFileName;
    };
    std::vector<RangeResolution> resolutions;
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
    std::map<std::string, CompiledShader> gbufferShaders; // role 4
    std::map<std::string, CompiledShader> litShaders;     // role 6
    for (auto& kv : stemResolutions) {
        if (!kv.second.ok) continue;
        gbufferShaders[kv.second.vsFileName] = std::move(kv.second.gb);
        litShaders[kv.second.vsFileName] = std::move(kv.second.lit);
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
    std::set<uint32_t> drawableMaterialIds;
    for (const auto& rr : resolutions) if (rr.ok) drawableMaterialIds.insert(rr.materialId);
    printf("[final] drawable materialId(s):");
    for (uint32_t m : drawableMaterialIds) printf(" %u", m);
    printf("\n\n");
    if (drawableRanges == 0) { printf("FATAL: no drawable range\n"); return 1; }

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
    std::map<uint32_t, sr3render::UploadedTexture> materialNormalTex, materialDiffuseTex, materialSpecularTex;
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
    float viewOnly[16];
    computeOrbitViewMatrix(boundsMin, boundsMax, yaw, pitch, distanceScale, viewOnly);
    float extent = 0.0f;
    for (int i = 0; i < 3; ++i) { float span = boundsMax[i]-boundsMin[i]; if (span > extent) extent = span; }
    if (extent <= 0.0f) extent = 1.0f;
    float nearZ = extent * 0.05f, farZ = extent * distanceScale * 4.0f; // CHOSEN, see top comment
    float projOnly[16];
    buildPerspectiveLH(0.785398f /* 45 deg, CHOSEN */, static_cast<float>(kW) / kH, nearZ, farZ, projOnly);
    float viewProj[16];
    sr3render::multiplyMatrix4x4(viewOnly, projOnly, viewProj);
    float invProj[16];
    bool projInvertible = invert4x4(projOnly, invProj);
    printf("[camera] CHOSEN fovY=45deg near=%.3f far=%.3f (not real game camera data). projection invertible=%d\n",
           nearZ, farZ, projInvertible);
    float worldIdentity[16];
    sr3render::buildWorldMatrix(0.0f, 0.0f, 0.0f, 0.0f, 1.0f, worldIdentity);

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

    printf("=== Constant-buffer fill decisions, GENERIC TEMPLATE (real vs. placeholder, by real CTAB name) ===\n");
    for (const auto& l : fillLog) printf("  %-56s : %s\n", l.constName.c_str(), l.decision.c_str());
    printf("\n");

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
    struct MaterialCbuffers {
        std::vector<uint8_t> gbVsCpu, gbPsCpu, litVsCpu, litPsCpu;
        ID3D11Buffer *cbGbVs = nullptr, *cbGbPs = nullptr, *cbLitVs = nullptr, *cbLitPs = nullptr;
    };
    std::map<uint32_t, std::string> materialIdToVsFileName;
    for (const auto& rr : resolutions) if (rr.ok) materialIdToVsFileName[rr.materialId] = rr.vsFileName;

    std::map<uint32_t, MaterialCbuffers> materialCbuffers;
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

    // -----------------------------------------------------------------
    // 12. Real off-screen G-buffer targets (3 colour + 1 depth) and the
    //     light-accumulation buffer - identical to prototype_lit_car.cpp's
    //     own step 8.
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
    printf("[gbuffer] created 3 real off-screen colour targets (Normals/Depth/Lighting, CHOSEN format %s, %ux%u) "
           "+ 1 real depth-stencil surface + 1 real light-accumulation target.\n", "DXGI_FORMAT_R8G8B8A8_UNORM", kW, kH);

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
    // BLACK placeholder - NEEDED for ir_light_directional pass 0's own real
    // ir_ambient_occlusionSampler (AO.x==1.0 means FULLY OCCLUDED in this
    // shader's own real math - the opposite of white's usual "neutral"
    // meaning elsewhere in this tool family). Unchanged from the parent
    // prototype.
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
        auto mcIt = materialCbuffers.find(rr.materialId);
        if (mcIt == materialCbuffers.end() || !mcIt->second.cbGbVs || !mcIt->second.cbGbPs) continue;
        UINT stride = sizeof(GpuVertexReal), offset = 0;
        dev.context->IASetInputLayout(cs.inputLayout);
        dev.context->IASetVertexBuffers(0, 1, &cit->second.vb, &stride, &offset);
        dev.context->IASetIndexBuffer(cit->second.ib, DXGI_FORMAT_R32_UINT, 0);
        dev.context->VSSetShader(cs.vs, nullptr, 0);
        // NEW IN THIS FORK (Lead 2): this material's OWN real per-material
        // cbuffer (template + real B/C overrides), not the shared
        // per-shader one.
        dev.context->VSSetConstantBuffers(0, 1, &mcIt->second.cbGbVs);
        dev.context->PSSetShader(cs.ps, nullptr, 0);
        dev.context->PSSetConstantBuffers(0, 1, &mcIt->second.cbGbPs);
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

    // -----------------------------------------------------------------
    // PASS B: real directional light, ir_light_directional's own real T8
    // pass 0 - identical to prototype_lit_car.cpp's own PASS B, byte-for-
    // byte unchanged (same shader file, same real weather-row fill, same
    // CHOSEN light direction).
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
        auto mcIt = materialCbuffers.find(rr.materialId);
        if (mcIt == materialCbuffers.end() || !mcIt->second.cbLitVs || !mcIt->second.cbLitPs) continue;
        UINT stride = sizeof(GpuVertexReal), offset = 0;
        dev.context->IASetInputLayout(cs.inputLayout);
        dev.context->IASetVertexBuffers(0, 1, &cit->second.vb, &stride, &offset);
        dev.context->IASetIndexBuffer(cit->second.ib, DXGI_FORMAT_R32_UINT, 0);
        dev.context->VSSetShader(cs.vs, nullptr, 0);
        // NEW IN THIS FORK (Lead 2): this material's OWN real per-material
        // cbuffer (template + real B/C overrides), not the shared
        // per-shader one.
        dev.context->VSSetConstantBuffers(0, 1, &mcIt->second.cbLitVs);
        dev.context->PSSetShader(cs.ps, nullptr, 0);
        dev.context->PSSetConstantBuffers(0, 1, &mcIt->second.cbLitPs);
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

    // -----------------------------------------------------------------
    // 13. Read back + PNG + real pixel statistics - identical mechanism
    //     to prototype_lit_car.cpp's own step 9.
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
    if (!sr3render::writePng(outPath, kW, kH, rgba, pngErr)) { printf("FATAL: writePng failed: %s\n", pngErr.c_str()); return 1; }
    printf("\n[png] wrote %s\n", outPath.c_str());
    printf("\n=== DONE ===\n");
    return 0;
}
