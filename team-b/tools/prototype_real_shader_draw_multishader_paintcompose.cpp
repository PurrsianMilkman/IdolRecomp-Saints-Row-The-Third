// Standalone prototype, FORKED AGAIN from
// tools/prototype_real_shader_draw_multishader.cpp (HANDOFF §9.104 - that
// file is left completely untouched, same as its own original
// prototype_real_shader_draw.cpp, HANDOFF §9.103, both untouched).
//
// WHAT THIS FORK ADDS (2026-09-29 follow-up task): §9.103/§9.104's own
// render used ir_sr3carpaint_gr_v.fxo_pc's T8 PASS 0 for the vehicle's
// body-paint ranges. Direct re-verification this session (re-parsing the
// real T8 table, re-disassembling every one of its 5 real PS blobs, reading
// each one's real CTAB and its real translated HLSL body) found pass 0's PS
// is a DEFERRED G-BUFFER pass (3 render targets, encoded normal + specular
// channels, CTAB constants ONLY `Damage_Normal_MapSampler`/`Specular_Power`
// - no paint-color input/output anywhere in it), not the pass that actually
// produces the visible paint color. Passes 2 and 3 (PS at real file offsets
// 10816/len 3464 and 14288/len 4376 respectively - confirmed by direct
// re-parse, exact match to the orchestrating task's own stated offsets) are
// the real forward/composite passes: single render target, real CTAB
// constant `Base_Paint_Color` (register c2, a float3) directly feeding the
// final output color (confirmed by reading the translated HLSL body:
// `r4.xyz = c[2].xyz; ... output.oC0.xyzw = (r1.xyzw * c[37].xyzw)`), along
// with `Tint_color`/`Specular_Color`/`Fresnel_Color`/
// `Ambient_Reflection_Amount` and real environment-reflection/G-buffer-
// readback samplers. Pass 2 was chosen over pass 3 (which adds
// `IR_Similarity_Data`/`IR_Stipple_Pattern_Offset` - plausibly an LOD/
// damage-transition dither variant, untraced either way) as the simpler,
// equally-complete-for-paint-color forward pass - both share the IDENTICAL
// real VS DCL input signature {POSITION0,TEXCOORD0,NORMAL0,TANGENT0,
// BLENDINDICES0} as pass 0 (re-confirmed directly, not assumed), so no
// additional vertex-side placeholder is needed to switch passes.
//
// WHERE THE REAL Base_Paint_Color VALUE ACTUALLY COMES FROM - CORRECTED,
// SAME DAY, AFTER A REAL DEAD END HONESTLY WALKED BACK (not hidden - see
// extractMaterialArrayBC()'s own top comment for the full story). This
// fork's FIRST version read `car_4dr_genki.cvtf_pc` (this session's new
// `.cvtf_pc` reader, include/sr3vehicle/cvtf.h) to find a real customization
// color NAME (`Hero Gloss Hot Pink`, one of several real style-preset hues
// this specific vehicle's file enumerates), then looked that name up in the
// real base-game `vehicle_cust_color_pool.xtbl` (`misc_tables.vpp_pc`, 694
// real `<Color>` rows) - a real, exact row match, genuinely confirming that
// table's `Base_Paint_Color`-named `<Shader_Variable>` mechanism exists and
// contains real numbers. BUT a same-day, independently-verified correction
// (Team A's own register-by-register trace, spec-render-pipeline.md
// §20.12.6) established a MORE DIRECT real source: `Base_Paint_Color` (and
// most of pass 2's other named lighting constants) is authored PER-MATERIAL,
// directly inside car_4dr_genki_0's OWN `.ccar_pc` material record - a
// name-hash-keyed `{hash -> Vector4}` table (array B/C, spec-vehicle-
// geometry.md's own exact byte layout) parsed once per material at
// mesh-load time. **This fork now uses THAT source, not the `.cvtf_pc`/xtbl
// chain** - verified directly against real bytes before adopting (not taken
// on the relay alone): this reader's own computed CRC-32-over-lowercased-
// name hash for `"base_paint_color"` is `0x3E344564`, an EXACT match to a
// real array-B entry in EVERY ONE of car_4dr_genki_0's 6 real
// `ir_sr3carpaint_gr` material records (materialId 0/1/2/3/38/39) - not
// assumed, checked. All 15 of this vehicle's real array-B hashes were
// identified this way (exact hash match against every CTAB constant name
// pass 2 declares): `Specular_Power`, `Specular_Color`, `Base_Paint_Color`,
// `grime_tiling_u/v`, `grime_amount`, `Fresnel_Color`,
// `Fresnel_Min_Cos_Angle`, `Fresnel_Inverse_Cos_Angle_Range`,
// `Reflection_Cos_Min_Angles`, `Reflection_Inv_Range_Cos_Angles`,
// `Ambient_Reflection_Amount`, `Specular_Alpha`, `grime_override`,
// `Self_Illumination`. **Crucially, `Base_Paint_Color` genuinely DIFFERS
// per real material** - materials 0/1 carry (0.65838, 0.68669, 0.70110) (a
// light, cool silver-blue), materials 2/3 carry (0.65838, 0.35640, 0.70110)
// (a muted purple/orchid) - both already plain 0..1 floats, no scaling
// judgement call needed (unlike the superseded xtbl reading, whose raw XML
// units were ~0..255 and needed an explicit, flagged `/255.0` guess). This
// fork therefore builds ONE real PS constant buffer PER DISTINCT REAL
// MATERIAL ID actually used by ir_sr3carpaint_gr's own ranges (step 8b,
// below), not one shared buffer per shader file - the correct granularity
// for genuinely per-material authored data. Every PS constant WITHOUT a
// real array-B match (`Tint_color`, `Fog_color`, `Target_dimensions`,
// `IR_Pixel_Steps` - all genuinely frame/screen-level, not per-material, by
// this same real check) stays at the SAME flat-8.0 placeholder standard
// prototype_real_shader_draw_multishader.cpp already uses - this fork does
// not pretend to have resolved more than it actually did. The `.cvtf_pc`
// reader and its xtbl finding are NOT deleted (both real, both kept working)
// - they answer a genuinely different question ("what would choosing color
// name X look like") from the one this task actually needed answered
// ("what is this specific vehicle's own currently-authored paint value").
//
// Scope of the change: ONLY ranges resolved to stem `ir_sr3carpaint_gr`
// (the vehicle body-paint shader) are redirected to pass 2 with the real
// `Base_Paint_Color`/etc. fill above; every other resolved shader
// (`ir_sr3carviewsphere_g`, `ir_sr3carlightcover`, `ir_bbsimple_1uv_decal`,
// `ir_sr3cardiffusespec_g`, `ir_sr3cardiffusespec`) is drawn exactly as
// prototype_real_shader_draw_multishader.cpp already does (its own pass-0-
// equivalent generic `pairVsPs()` resolution, unchanged) - this fork does
// not touch what already worked for them.
//
// Standalone prototype, FORKED from tools/prototype_real_shader_draw.cpp
// (HANDOFF §9.103 - that file is left completely untouched; read its own
// top comment first for the single-shader mechanism this one extends).
//
// WHAT THIS ADDS (orchestrator's own framing, task text preserved for
// anyone reading this file cold): the single-shader prototype only draws
// car_4dr_genki_0 group 0's submeshIndex==0 ranges (16 of 20) with ONE
// hardcoded real shader (ir_sr3carpaint_gr). The other 16,10,921,...
// actually: the other 4 ranges span submeshIndex 1 and 2 - channel 1
// (layoutCode=100, NO tangent data at all) and channel 2 (layoutCode=101,
// has tangent, 3 texcoord sets) - which the single-shader prototype could
// not draw without fabricating a placeholder tangent for channel 1. This
// file resolves EACH draw range's OWN real shader via its OWN material's
// real shaderHash field, instead of assuming every range uses
// ir_sr3carpaint_gr.
//
// THE REAL JOIN, exactly as independently re-verified this session
// (HANDOFF §9.101's correction, NOT the original wrong negative struck
// through in that section):
//   1. Each material record (position i in the per-material loop ==
//      materialId i, spec-vehicle-geometry.md §11.2) has a shaderHash field
//      at headerStart+0x00. The cursor arithmetic to find headerStart is
//      NOT exposed by include/sr3geometry/material_binding.h (that reader
//      only reads texture bindings) - replicated LOCALLY here, byte for
//      byte, from the session scratchpad tool shader_hash_join.cpp's own
//      extractVehicleShaderHashes() (subheaderOffset = geo.offset(),
//      meshSubBlockOffset = geo.meshSubBlockOffset(), N2 = u16 at
//      subheaderOffset+0x0E - a FIXED HEADER FIELD, not an inline stream
//      value, a bug already found and fixed once in that tool - cursor walk
//      through array_A/array_B/array_C, then the per-material loop reading
//      declaredSize/headerStart+0x00(shaderHash)/headerStart+0x04
//      (secondHash) and advancing by declaredSize). material_binding.cpp is
//      NOT modified - this project's standing practice for this session.
//   2. shaderHash = sr3fxo::crc32Raw() (reflected 0xEDB88320, init 0, no
//      final XOR) of the STAGE-SUFFIX-STRIPPED STEM of a real .fxo_pc name
//      (13 suffixes, spec-fxo-format.md §6.6, longest-match-first - the
//      exact table and stripStem() logic replicated from the session
//      scratchpad tool verify_stem_crc.cpp, independently 17/17-verified
//      there against real vehicle shaderHash values).
//   3. The stem's own real VS file is located by trying "<stem>_v.fxo_pc"
//      first, then "<stem>_mv.fxo_pc" (spec-fxo-format.md §6.6: suffix
//      class 5, variants 0 and 2 respectively - both are "vertex" class,
//      _v is variant 0). If a stem resolves but neither vertex-stage file
//      exists among its real files, that is reported plainly, not
//      papered over. The VS's own real paired PS is then found via the
//      SAME real T8 pass-table pairing logic the single-shader prototype
//      already implements and verified (WrapperHeader::passes()/
//      layoutBlobs()), generalized to run on whichever file this step
//      resolves rather than the one hardcoded file.
//   4. submeshIndex == channel index, 9/9 exact across all 57 real draw
//      groups (this session's own measurement, closing mesh_block.h's own
//      long-standing OPEN comment on DrawRange::submeshIndex) - so a
//      range's own channel is simply mesh.channels()[range.submeshIndex],
//      not assumed to always be channel 0.
//
// GROUPING FOR COMPILE REUSE: ranges are grouped by their resolved VS file
// name (not per-range) so identical shaders (e.g. all body-paint ranges)
// are disassembled/translated/compiled/reflected exactly ONCE, matching
// the task's "avoid redundant compiles" requirement.
//
// REAL VS. PLACEHOLDER, same standard as the single-shader prototype's own
// top comment, extended:
//   REAL - every distinct shader's own actual resolved VS/PS pair, its own
//     real DCL-derived input signature, its own real CTAB-driven constant
//     fill (view/proj camera math, the real per-part transform palette),
//     and (new) its own real per-material texture resolution where the
//     material's own real MaterialBindings binding can be found.
//   PLACEHOLDER, honestly labelled, SAME STANDARD AS THE TEXTURE FALLBACK
//     the single-shader prototype already uses - only engaged when a
//     resolved shader's OWN real input signature needs a field the
//     channel's own real data genuinely lacks (the concrete case this task
//     exists to test: TANGENT0 on channel 1, layoutCode=100, IF a shader
//     needing TANGENT0 actually ends up resolved for it - reported plainly
//     either way, not assumed in advance). Also placeholder, same as
//     before: objTM (identity), the per-instance zero param, PS
//     Specular_Power (flat non-zero), and any material/texture pairing
//     that cannot be resolved to a real record.
//
// Standalone: not wired into CMakeLists.txt, build_verify/ untouched -
// built in an isolated scratch directory, linking the SAME pre-built
// build_verify/*.obj objects (read-only reuse, no write to build_verify/)
// plus d3d11/dxgi/d3dcompiler/user32/gdi32, same as the single-shader
// prototype and every tools/validation/*.cpp harness.

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
#include "sr3render/mesh_renderer.h" // buildOrbitViewProjection/buildWorldMatrix - reused free functions
#include "sr3render/png_writer.h"
#include "sr3render/texture_upload.h"
#include "sr3texture/texture_pair.h"
#include "sr3vehicle/vehicle.h"
#include "vpp/container.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace {

// ===========================================================================
// Small generic helpers (identical to prototype_real_shader_draw.cpp's own)
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
size_t alignUp(size_t v, size_t a) { return (v + a - 1) / a * a; }

// ===========================================================================
// Debug-layer-enabled render device - byte-identical copy of
// prototype_real_shader_draw.cpp's own DebugRenderDevice (see that file's
// comment for why this is duplicated rather than shared: device.h/.cpp are
// canonical files this session does not touch).
// ===========================================================================
struct DebugRenderDevice {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ID3D11Texture2D* target = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11Texture2D* staging = nullptr;
    ID3D11InfoQueue* infoQueue = nullptr;
    uint32_t width = 0, height = 0;
    bool isWarp = false;
    bool debugLayerActive = false;
    std::string debugLayerNote;

    ~DebugRenderDevice() {
        safeRelease(infoQueue);
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
        UINT flags = D3D11_CREATE_DEVICE_DEBUG;

        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, wanted, 1,
                                        D3D11_SDK_VERSION, &device, &got, &context);
        if (SUCCEEDED(hr)) {
            isWarp = false;
            debugLayerActive = true;
        } else {
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, wanted, 1,
                                    D3D11_SDK_VERSION, &device, &got, &context);
            if (SUCCEEDED(hr)) {
                isWarp = true;
                debugLayerActive = true;
            } else if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING) {
                debugLayerNote = "D3D11 debug layer unavailable in this environment (DXGI_ERROR_SDK_COMPONENT_MISSING "
                                  "for both hardware and WARP - same environment limitation the single-shader "
                                  "prototype already reported) - falling back to a plain device.";
                hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, wanted, 1, D3D11_SDK_VERSION,
                                        &device, &got, &context);
                if (SUCCEEDED(hr)) {
                    isWarp = false;
                } else {
                    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, wanted, 1, D3D11_SDK_VERSION,
                                            &device, &got, &context);
                    if (FAILED(hr)) {
                        error = "D3D11CreateDevice failed for both hardware and WARP even without the debug "
                                "layer: " + hrToString(hr);
                        return false;
                    }
                    isWarp = true;
                }
            } else {
                error = "D3D11CreateDevice (debug layer) failed for both hardware and WARP: " + hrToString(hr);
                return false;
            }
        }

        if (SUCCEEDED(device->QueryInterface(__uuidof(ID3D11InfoQueue), reinterpret_cast<void**>(&infoQueue)))) {
            infoQueue->ClearStoredMessages();
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
        sd.Usage = D3D11_USAGE_STAGING;
        sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = device->CreateTexture2D(&sd, nullptr, &staging);
        if (FAILED(hr)) { error = "CreateTexture2D (staging) failed: " + hrToString(hr); return false; }
        return true;
    }

    void bindWithDepth(ID3D11DepthStencilView* depth) {
        context->OMSetRenderTargets(1, &rtv, depth);
        D3D11_VIEWPORT vp{};
        vp.Width = static_cast<float>(width); vp.Height = static_cast<float>(height);
        vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
        context->RSSetViewports(1, &vp);
    }
    void clear(float r, float g, float b, float a) {
        const float c[4] = {r, g, b, a};
        context->ClearRenderTargetView(rtv, c);
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

// ===========================================================================
// Real DCL-derived vertex-input signature (identical idiom to the
// single-shader prototype / tools/validation/rank_shaders_by_vertex_signature.cpp)
// ===========================================================================
constexpr uint8_t kPOSITION = 0, kBLENDWEIGHT = 1, kBLENDINDICES = 2, kNORMAL = 3, kTEXCOORD = 5, kTANGENT = 6;

const char* usageName(uint8_t u) {
    switch (u) {
        case kPOSITION: return "POSITION";
        case kBLENDWEIGHT: return "BLENDWEIGHT";
        case kBLENDINDICES: return "BLENDINDICES";
        case kNORMAL: return "NORMAL";
        case kTEXCOORD: return "TEXCOORD";
        case kTANGENT: return "TANGENT";
        default: return "?";
    }
}
struct VsInputField { uint16_t reg; uint8_t usage; uint8_t usageIndex; };

std::vector<VsInputField> extractVsInputSignature(const sr3d3d9bc::DisassembledShader& d) {
    std::vector<VsInputField> out;
    for (const auto& inst : d.instructions) {
        if (inst.opcode != sr3d3d9bc::Opcode::DCL) continue;
        if (!inst.dcl.has_value() || !inst.dest.has_value()) continue;
        if (inst.dest->registerTypeRaw != 1 /* D3DSPR_INPUT */) continue;
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
// Matrix packing - identical to prototype_real_shader_draw.cpp's own (see
// that file's comment for the row-vector/M*v-register derivation).
// ===========================================================================
void packColumn(const float M[16], int col, float out4[4]) {
    out4[0] = M[0 * 4 + col];
    out4[1] = M[1 * 4 + col];
    out4[2] = M[2 * 4 + col];
    out4[3] = M[3 * 4 + col];
}
void transformPointRowVector(const float p[3], const float M[16], float out[3]) {
    out[0] = p[0] * M[0] + p[1] * M[4] + p[2] * M[8] + M[12];
    out[1] = p[0] * M[1] + p[1] * M[5] + p[2] * M[9] + M[13];
    out[2] = p[0] * M[2] + p[1] * M[6] + p[2] * M[10] + M[14];
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

struct ConstantFillLog { std::string constName; std::string decision; };

void fillFloat4Registers(std::vector<uint8_t>& cpuBuffer, uint16_t registerIndex, uint16_t registerCount,
                          const std::vector<std::array<float, 4>>& regs) {
    for (uint16_t r = 0; r < registerCount && r < regs.size(); ++r) {
        size_t byteOff = static_cast<size_t>(registerIndex + r) * 16;
        if (byteOff + 16 > cpuBuffer.size()) continue;
        std::memcpy(cpuBuffer.data() + byteOff, regs[r].data(), 16);
    }
}

// GPU vertex layout. Fixed superset (POSITION/NORMAL/TANGENT/BLENDINDICES/
// TEXCOORD0) reused across EVERY distinct shader and EVERY channel - a
// resolved shader's own real D3D11_INPUT_ELEMENT_DESC array always uses
// this same 5-slot struct (see buildInputLayout()); D3D11 CreateInputLayout
// tolerates array entries a shader does not consume, so passing all 5
// slots regardless of which subset a given shader's own real DCL signature
// actually declares is standard, harmless practice, not a shortcut around
// per-shader correctness (the per-shader SUBSET CHECK below is what
// enforces correctness, not this struct's shape).
struct GpuVertexReal { float position[4], normal[4], tangent[4], blendIndices[4], texcoord0[4]; };

const D3D11_INPUT_ELEMENT_DESC kFullLayout[5] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, normal), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, tangent), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, blendIndices), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, texcoord0), D3D11_INPUT_PER_VERTEX_DATA, 0},
};

// ===========================================================================
// STEP 1/2: real shaderHash -> real stem -> real vertex-stage .fxo_pc file.
// ===========================================================================

// spec-fxo-format.md §6.6's own 13 suffixes, longest-match-first (same
// table as the session scratchpad tool verify_stem_crc.cpp, independently
// re-derived from the spec's own text, 17/17-verified there).
const std::vector<std::string> kStageSuffixes = {
    "_bms", "_bmc", "_bs", "_bc", "_ms", "_mc", "_mv", "_ts",
    "_fd", "_s", "_c", "_t", "_v",
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
            std::string stem = stripStageSuffix(noExt);
            stemToFiles[stem].push_back(n);
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { collectFxoStems(c.openNested(i), stemToFiles); } catch (const std::exception&) {}
        }
    }
}

// spec-fxo-format.md §6.6: "_v"/"_mv" are BOTH class-5 ("vertex"), variants
// 0 and 2. Prefer "_v" (variant 0); fall back to "_mv" (variant 2) only if
// no "_v" file exists for this stem. If neither exists, reports plainly
// which OTHER real files this stem does have (a genuine possible outcome,
// not assumed impossible per the task's own explicit instruction).
std::string pickVertexFile(const std::vector<std::string>& files, std::string& reasonIfNone) {
    for (const auto& f : files) if (endsWithStr(lower(f), "_v.fxo_pc")) return f;
    for (const auto& f : files) if (endsWithStr(lower(f), "_mv.fxo_pc")) return f;
    reasonIfNone = "stem resolved but has no _v/_mv real file among:";
    for (const auto& f : files) reasonIfNone += " " + f;
    return "";
}

// Replicates shader_hash_join.cpp's extractVehicleShaderHashes() cursor
// arithmetic EXACTLY (see this file's top comment) but returns the FULL
// per-material array (indexed by materialId), not just a flat multiset -
// material_binding.cpp is not modified, same standing rule.
struct MaterialShaderHashResult {
    std::vector<uint32_t> hashes; // hashes[materialId]; may be shorter than materialCount if truncated
    bool complete = false;
    std::string note;
};
// NEW in this fork, decisive correction (coordinator relay, verified
// directly against real bytes before adopting - spec-render-pipeline.md
// §20.12.6 + spec-vehicle-geometry.md's own exact record layout, lines
// 382-392): EVERY material record's fixed 0x30-byte header carries THREE
// array-length fields, not just shaderHash/secondHash/flags -
// `+0x0C A_count(u16)` (texture bindings, already read by
// sr3geometry::MaterialBindings), `+0x0E B_count(u8)`, `+0x0F C_count(u8)`.
// Array B (B_count * 4 bytes, right after array A, 4-aligned) is a plain
// CRC-32-over-lowercased-name hash per entry (the SAME hash scheme as
// sr3fxo::hashLowerName / the shaderHash join, independently re-verified
// here: this reader's own computed hash for "base_paint_color" is
// 0x3E344564, an EXACT match to a real array-B entry in car_4dr_genki_0's
// own material records - not assumed, checked). Array C (C_count * 16
// bytes, right after array B, 16-aligned) is one Vector4 per array-B entry,
// POSITIONALLY paired - i.e. a real, per-material, name-hash-keyed
// {name -> Vector4} table authored directly in THIS vehicle's own
// `.ccar_pc`, confirmed to include `Base_Paint_Color` itself (register c2
// in pass 2's own CTAB) plus most of pass 2's other named lighting
// constants. THIS is the correct, most-direct source for these values -
// more direct than the `.cvtf_pc`/`vehicle_cust_color_pool.xtbl` chain this
// fork's own first draft used, which answers "what would a customization
// CHOICE of a given color name look like," not "what is this specific
// vehicle's own actual authored material value." Both are real; this one
// is closer to the actual render path for an unmodified spawned vehicle.
struct MaterialConstant {
    uint32_t nameHash = 0;
    std::array<float, 4> value{};
};
struct MaterialArrayBCResult {
    std::vector<std::vector<MaterialConstant>> perMaterial; // indexed by materialId
    bool complete = false;
    std::string note;
};
MaterialArrayBCResult extractMaterialArrayBC(vpp::ByteView content, size_t subheaderOffset,
                                              size_t meshSubBlockOffset, uint32_t meshCLength,
                                              uint16_t materialCount) {
    MaterialArrayBCResult result;
    if (materialCount == 0 || materialCount > 4096) { result.note = "implausible materialCount"; return result; }
    const uint16_t n2 = content.readU16LE(subheaderOffset + 0x0E);
    size_t cursor = static_cast<size_t>(meshSubBlockOffset) + meshCLength;
    if (n2 > 0) {
        cursor = alignUp(cursor, 8);
        cursor += static_cast<size_t>(n2) * 8 + static_cast<size_t>(n2) * 4;
    }
    cursor = alignUp(cursor, 8);
    cursor += static_cast<size_t>(materialCount) * 8;

    for (uint16_t i = 0; i < materialCount; ++i) {
        if (cursor + 4 > content.size()) { result.note = "ran out of bytes at material " + std::to_string(i); return result; }
        uint32_t declaredSize = content.readU32LE(cursor);
        size_t headerStart = alignUp(cursor + 4, 8);
        if (headerStart + 0x30 > content.size()) { result.note = "header overrun at material " + std::to_string(i); return result; }
        uint16_t aCount = content.readU16LE(headerStart + 0x0C);
        uint8_t bCount = content.at(headerStart + 0x0E);
        size_t arrayABase = headerStart + 0x30;
        size_t arrayBBase = alignUp(arrayABase + static_cast<size_t>(aCount) * 12, 4);
        size_t arrayCBase = alignUp(arrayBBase + static_cast<size_t>(bCount) * 4, 16);
        std::vector<MaterialConstant> consts;
        for (uint8_t k = 0; k < bCount; ++k) {
            size_t bOff = arrayBBase + static_cast<size_t>(k) * 4;
            size_t cOff = arrayCBase + static_cast<size_t>(k) * 16;
            if (bOff + 4 > content.size() || cOff + 16 > content.size()) break;
            MaterialConstant mc;
            mc.nameHash = content.readU32LE(bOff);
            for (int w = 0; w < 4; ++w) {
                uint32_t u = content.readU32LE(cOff + static_cast<size_t>(w) * 4);
                std::memcpy(&mc.value[static_cast<size_t>(w)], &u, 4);
            }
            consts.push_back(mc);
        }
        result.perMaterial.push_back(std::move(consts));
        if (declaredSize == 0 || declaredSize > 1000000) {
            result.note = "implausible declaredSize=" + std::to_string(declaredSize) + " at material " + std::to_string(i);
            return result;
        }
        cursor += declaredSize;
    }
    result.complete = true;
    return result;
}

MaterialShaderHashResult extractMaterialShaderHashes(vpp::ByteView content, size_t subheaderOffset,
                                                       size_t meshSubBlockOffset, uint32_t meshCLength,
                                                       uint16_t materialCount) {
    MaterialShaderHashResult result;
    if (materialCount == 0 || materialCount > 4096) { result.note = "implausible materialCount"; return result; }
    const uint16_t n2 = content.readU16LE(subheaderOffset + 0x0E);
    size_t cursor = static_cast<size_t>(meshSubBlockOffset) + meshCLength;
    if (n2 > 0) {
        cursor = alignUp(cursor, 8);
        cursor += static_cast<size_t>(n2) * 8 + static_cast<size_t>(n2) * 4; // array_A, array_B
    }
    cursor = alignUp(cursor, 8);
    cursor += static_cast<size_t>(materialCount) * 8; // array_C

    for (uint16_t i = 0; i < materialCount; ++i) {
        if (cursor + 4 > content.size()) { result.note = "ran out of bytes at material " + std::to_string(i); return result; }
        uint32_t declaredSize = content.readU32LE(cursor);
        size_t headerStart = alignUp(cursor + 4, 8);
        if (headerStart + 0x30 > content.size()) { result.note = "header overrun at material " + std::to_string(i); return result; }
        uint32_t shaderHash = content.readU32LE(headerStart + 0x00);
        result.hashes.push_back(shaderHash);
        if (declaredSize == 0 || declaredSize > 1000000) {
            result.note = "implausible declaredSize=" + std::to_string(declaredSize) + " at material " + std::to_string(i);
            return result;
        }
        cursor += declaredSize;
    }
    result.complete = true;
    return result;
}

// ===========================================================================
// STEP 3: real T8 VS<->PS pairing, generalized to run on ANY resolved
// vertex-stage file (same logic prototype_real_shader_draw.cpp already
// verified for ir_sr3carpaint_gr_v.fxo_pc specifically; here it is run
// per resolved file rather than hardcoded to one).
// ===========================================================================
struct BlobPairResult {
    bool ok = false;
    std::string reason;
    size_t vsOffset = 0, vsLength = 0, psOffset = 0, psLength = 0;
    size_t vsTableIndex = 0;
};
BlobPairResult pairVsPs(const std::vector<uint8_t>& fxoBytes) {
    BlobPairResult r;
    sr3fxo::WrapperHeader wh;
    std::string why;
    vpp::ByteView view(fxoBytes.data(), fxoBytes.size());
    if (!sr3fxo::WrapperHeader::tryParse(view, wh, why)) { r.reason = "WrapperHeader::tryParse failed: " + why; return r; }
    size_t endOfBlobs = 0;
    std::vector<sr3fxo::WrapperBlob> blobs = wh.layoutBlobs(endOfBlobs);
    const sr3fxo::WrapperBlob* vsBlob = nullptr;
    for (const auto& b : blobs) {
        if (b.stage == sr3fxo::Stage::Vertex) { vsBlob = &b; break; } // first real (non-zero-length) VS entry
    }
    if (!vsBlob) { r.reason = "file has no non-empty vertex-stage blob"; return r; }
    r.vsOffset = vsBlob->offset; r.vsLength = vsBlob->length; r.vsTableIndex = vsBlob->tableIndex;
    bool psFound = false;
    for (const auto& p : wh.passes()) {
        if (p.vertexIndex != static_cast<int16_t>(vsBlob->tableIndex)) continue;
        for (const auto& pb : blobs) {
            if (pb.stage == sr3fxo::Stage::Pixel && static_cast<int16_t>(pb.tableIndex) == p.pixelIndex) {
                r.psOffset = pb.offset; r.psLength = pb.length; psFound = true;
            }
        }
    }
    if (!psFound) { r.reason = "real T8 pass table has no PS paired with VS tableIndex " + std::to_string(vsBlob->tableIndex); return r; }
    r.ok = true;
    return r;
}

// NEW in this fork: pairs the VS/PS blobs for one EXPLICIT T8 pass index
// (rather than pairVsPs()'s "first non-empty VS blob", which always lands
// on pass 0 for ir_sr3carpaint_gr_v.fxo_pc, since every one of its 5 real
// passes has its own distinct VS table entry - table index 0..4, one per
// pass, not shared). Used ONLY for the ir_sr3carpaint_gr stem, to draw with
// pass 2 (the real forward/composite pass, see this file's own top
// comment) instead of pass 0 (the real deferred G-buffer pass).
BlobPairResult pairVsPsForPass(const std::vector<uint8_t>& fxoBytes, size_t wantPassIndex) {
    BlobPairResult r;
    sr3fxo::WrapperHeader wh;
    std::string why;
    vpp::ByteView view(fxoBytes.data(), fxoBytes.size());
    if (!sr3fxo::WrapperHeader::tryParse(view, wh, why)) { r.reason = "WrapperHeader::tryParse failed: " + why; return r; }
    if (wantPassIndex >= wh.passes().size()) {
        r.reason = "requested pass index " + std::to_string(wantPassIndex) + " >= real passCount " +
                   std::to_string(wh.passes().size());
        return r;
    }
    const sr3fxo::PassEntry& p = wh.passes()[wantPassIndex];
    size_t endOfBlobs = 0;
    std::vector<sr3fxo::WrapperBlob> blobs = wh.layoutBlobs(endOfBlobs);
    const sr3fxo::WrapperBlob* vsBlob = nullptr;
    const sr3fxo::WrapperBlob* psBlob = nullptr;
    for (const auto& b : blobs) {
        if (b.stage == sr3fxo::Stage::Vertex && static_cast<int16_t>(b.tableIndex) == p.vertexIndex) vsBlob = &b;
        if (b.stage == sr3fxo::Stage::Pixel && static_cast<int16_t>(b.tableIndex) == p.pixelIndex) psBlob = &b;
    }
    if (!vsBlob) { r.reason = "pass " + std::to_string(wantPassIndex) + "'s own vertexIndex has no real non-empty VS blob"; return r; }
    if (!psBlob) { r.reason = "pass " + std::to_string(wantPassIndex) + "'s own pixelIndex has no real non-empty PS blob"; return r; }
    r.vsOffset = vsBlob->offset; r.vsLength = vsBlob->length; r.vsTableIndex = vsBlob->tableIndex;
    r.psOffset = psBlob->offset; r.psLength = psBlob->length;
    r.ok = true;
    return r;
}

// SUPERSEDED, kept only as a documented dead end: this fork's FIRST version
// used a single hardcoded "Hero Gloss Hot Pink" color read from
// vehicle_cust_color_pool.xtbl's raw XML text (a real, exact row match -
// numbers below are genuinely what that row contains), applied uniformly to
// every ir_sr3carpaint_gr range. A same-day coordinator relay (independently
// verified against real bytes before being trusted, per this project's own
// "relayed technical claims get verified" discipline - see this file's own
// top-of-file note on extractMaterialArrayBC()) established that the
// correct, more direct source is car_4dr_genki_0's OWN `.ccar_pc` material
// record's array B/C - a real, per-material, name-hash-keyed table that
// DIRECTLY authors Base_Paint_Color (register-exact match confirmed: CRC
// 0x3E344564 for "base_paint_color" found verbatim in the real array-B
// bytes). That per-material real data is what this fork now actually uses
// (see main()'s per-material constant-buffer fill) - .cvtf_pc/xtbl answer a
// DIFFERENT question ("what would choosing color name X look like"), not
// "what is this specific vehicle's own currently-authored value." Left here
// for the record, not deleted, since it was a real, verified (if ultimately
// wrong-layer) finding: "Hero Gloss Hot Pink" Base_Paint_Color =
// (114.95, 5.65434, 58.8387) raw XML units (694-row vehicle_cust_color_pool.xtbl,
// misc_tables.vpp_pc).

// ===========================================================================
// One fully resolved+compiled distinct shader.
// ===========================================================================
struct CompiledShader {
    std::string stem, vsFileName;
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

    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* inputLayout = nullptr;
    ID3D11Buffer* cbVs = nullptr;
    ID3D11Buffer* cbPs = nullptr; // fallback/template (flat-8.0 placeholder for every PS constant)
    std::vector<uint8_t> vsCbufCpu, psCbufCpu;

    // NEW in this fork: for ir_sr3carpaint_gr specifically, ONE real PS
    // cbuffer PER REAL MATERIAL ID actually used (Base_Paint_Color and
    // several sibling constants are genuinely per-material, not per-shader
    // - see main()'s step 8b). Keyed by materialId; falls back to cbPs
    // above for any materialId not present here.
    std::map<uint32_t, ID3D11Buffer*> perMaterialCbPs;

    int texBindPoint = -1, samplerBindPoint = -1;
    std::string psTextureResourceName;
    std::string textureKind = "none"; // "normal" | "diffuse" | "unknown" | "none"

    // NEW in this fork: EVERY real texture/sampler resource this PS
    // reflects (not just the last one, which is all texBindPoint/
    // samplerBindPoint above track) - only populated/used for the
    // ir_sr3carpaint_gr pass-2 shader (see this file's own top comment on
    // the "honest limitation, inherited..." note). Bound to a neutral
    // WHITE placeholder in the draw loop instead of being left unbound, so
    // the real Base_Paint_Color fed via the PS cbuffer is not multiplied to
    // zero by an unbound IR_LBufferSampler/IR_GBuffer_DSF_DataSampler read
    // - itself an honestly-labelled placeholder (neutral "full light/no
    // data" stand-in for real deferred G-buffer/L-buffer render targets
    // this single-pass prototype does not produce), not a claim that the
    // real lighting/G-buffer content is known.
    std::vector<UINT> allTexBindPoints;
    std::vector<UINT> allSamplerBindPoints;

    size_t rangesUsing = 0;
};

} // namespace

// ===========================================================================
// main()
// ===========================================================================
int main() {
    const std::string kCacheDir = "D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache";
    std::vector<ConstantFillLog> fillLog;

    printf("=== Multi-shader prototype: EACH draw range resolves and draws with ITS OWN real shader ===\n");
    printf("(forked from tools/prototype_real_shader_draw.cpp - see this file's own top comment)\n\n");

    // -----------------------------------------------------------------
    // 1. Build the real stem -> real .fxo_pc filename table from
    //    shaders.vpp_pc (STEP 1/2's data side).
    // -----------------------------------------------------------------
    std::vector<uint8_t> shadersArchive = readFile(kCacheDir + "/shaders.vpp_pc");
    if (shadersArchive.empty()) { printf("FATAL: could not read shaders.vpp_pc\n"); return 1; }
    vpp::Container shadersContainer{vpp::ByteView(shadersArchive.data(), shadersArchive.size())};
    std::map<std::string, std::vector<std::string>> stemToFiles;
    collectFxoStems(shadersContainer, stemToFiles);
    std::map<uint32_t, std::string> stemCrcToStem;
    int crcCollisions = 0;
    for (const auto& kv : stemToFiles) {
        uint32_t crc = sr3fxo::crc32Raw(reinterpret_cast<const uint8_t*>(kv.first.data()), kv.first.size());
        auto it = stemCrcToStem.find(crc);
        if (it != stemCrcToStem.end() && it->second != kv.first) ++crcCollisions;
        stemCrcToStem[crc] = kv.first;
    }
    printf("[shaderHash join] real .fxo_pc distinct stems: %zu, distinct stem-CRCs: %zu, collisions: %d\n\n",
           stemToFiles.size(), stemCrcToStem.size(), crcCollisions);

    // -----------------------------------------------------------------
    // 2. Real vehicle + mesh + material bindings + material shaderHash
    //    array (STEP 1's cursor arithmetic, replicated locally).
    // -----------------------------------------------------------------
    std::vector<uint8_t> dlc1 = readFile(kCacheDir + "/dlc1.vpp_pc");
    if (dlc1.empty()) { printf("FATAL: could not read dlc1.vpp_pc\n"); return 1; }
    vpp::Container dlc1Container{vpp::ByteView(dlc1.data(), dlc1.size())};
    std::vector<uint8_t> vehStr2;
    if (!findEntry(dlc1Container, "car_4dr_genki_0.str2_pc", vehStr2)) {
        printf("FATAL: car_4dr_genki_0.str2_pc not found in dlc1.vpp_pc\n");
        return 1;
    }
    vpp::Container vehContainer{vpp::ByteView(vehStr2.data(), vehStr2.size())};
    std::vector<uint8_t> ccarBytes, gcarBytes, cpegBytes, gpegBytes;
    findEntry(vehContainer, "car_4dr_genki_0.ccar_pc", ccarBytes);
    findEntry(vehContainer, "car_4dr_genki_0.gcar_pc", gcarBytes);
    findEntry(vehContainer, "car_4dr_genki_0.cpeg_pc", cpegBytes);
    findEntry(vehContainer, "car_4dr_genki_0.gpeg_pc", gpegBytes);
    if (ccarBytes.empty() || gcarBytes.empty()) { printf("FATAL: car_4dr_genki_0 .ccar_pc/.gcar_pc not found\n"); return 1; }
    printf("[vehicle] car_4dr_genki_0: ccar=%zu bytes gcar=%zu bytes cpeg=%zu bytes gpeg=%zu bytes\n", ccarBytes.size(),
           gcarBytes.size(), cpegBytes.size(), gpegBytes.size());

    sr3vehicle::Vehicle veh = sr3vehicle::Vehicle::parse(vpp::ByteView(ccarBytes.data(), ccarBytes.size()));
    printf("[vehicle] parts=%zu anchor='%s'\n", veh.parts().size(), veh.anchorName().c_str());

    vpp::ByteView ccarView(ccarBytes.data(), ccarBytes.size());
    sr3geometry::GeometryBlock geo = sr3geometry::GeometryBlock::parseAt(ccarView, veh.meshRegionOffset());
    if (!geo.hasMeshSubBlock()) { printf("FATAL: vehicle geometry block has no Mesh sub-block\n"); return 1; }
    sr3mesh::MeshBlock mesh = sr3mesh::MeshBlock::parse(ccarView, geo.meshSubBlockOffset(),
                                                        vpp::ByteView(gcarBytes.data(), gcarBytes.size()));
    printf("[mesh] channels=%zu indexCount=%u drawGroupsLocated=%d\n", mesh.channels().size(), mesh.indexCount(),
           mesh.drawGroupsLocated());
    for (size_t i = 0; i < mesh.channels().size(); ++i) {
        const auto& ch = mesh.channels()[i];
        printf("[mesh] channel[%zu]: layoutCode=%u texcoordCount=%u elementCount=%u\n", i, ch.layoutCode,
               ch.texcoordCount, ch.elementCount);
    }

    sr3geometry::MaterialBindings bindings =
        sr3geometry::MaterialBindings::parse(ccarView, geo.offset(), geo.meshSubBlockOffset() + mesh.cLength());
    printf("[material] located=%d materialCount=%u\n", bindings.located(), bindings.materialCount());
    if (!bindings.located()) { printf("FATAL: material bindings not located\n"); return 1; }

    MaterialShaderHashResult hashResult = extractMaterialShaderHashes(
        ccarView, geo.offset(), geo.meshSubBlockOffset(), mesh.cLength(), bindings.materialCount());
    printf("[shaderHash] extracted %zu/%u material shaderHash values, complete=%d%s%s\n", hashResult.hashes.size(),
           bindings.materialCount(), hashResult.complete, hashResult.note.empty() ? "" : "  note: ",
           hashResult.note.c_str());
    for (size_t i = 0; i < hashResult.hashes.size() && i < 8; ++i)
        printf("  material %zu: shaderHash=0x%08X\n", i, hashResult.hashes[i]);

    // NEW in this fork: the real per-material array B/C data (see
    // extractMaterialArrayBC()'s own top comment) - THE decisive,
    // coordinator-flagged, independently-verified source for
    // Base_Paint_Color and siblings, superseding this fork's own first
    // (xtbl-based) attempt.
    MaterialArrayBCResult arrayBC = extractMaterialArrayBC(
        ccarView, geo.offset(), geo.meshSubBlockOffset(), mesh.cLength(), bindings.materialCount());
    printf("[arrayBC] extracted %zu/%u material array-B/C tables, complete=%d%s%s\n", arrayBC.perMaterial.size(),
           bindings.materialCount(), arrayBC.complete, arrayBC.note.empty() ? "" : "  note: ", arrayBC.note.c_str());
    // Real hash for "base_paint_color" (sr3fxo::crc32Raw over the
    // lowercased name, this project's own established scheme) - computed
    // once, used to identify Base_Paint_Color's own array-B entry by exact
    // match, independently of what CTAB happens to call it in a given pass.
    const uint32_t kHashBasePaintColor = sr3fxo::hashLowerName("base_paint_color");
    printf("[arrayBC] hash('base_paint_color') = 0x%08X (used to locate the real per-material entry below)\n",
           kHashBasePaintColor);
    auto findMaterialConstant = [&](uint32_t materialId, uint32_t nameHash) -> const std::array<float, 4>* {
        if (materialId >= arrayBC.perMaterial.size()) return nullptr;
        for (const auto& mc : arrayBC.perMaterial[materialId])
            if (mc.nameHash == nameHash) return &mc.value;
        return nullptr;
    };
    for (size_t mi = 0; mi < arrayBC.perMaterial.size() && mi < 4; ++mi) {
        const auto* v = findMaterialConstant(static_cast<uint32_t>(mi), kHashBasePaintColor);
        if (v) printf("  material %zu real Base_Paint_Color = (%.5f, %.5f, %.5f, %.5f)\n", mi, (*v)[0], (*v)[1], (*v)[2], (*v)[3]);
    }

    if (!mesh.drawGroupsLocated() || mesh.drawGroups().empty()) { printf("FATAL: draw groups not located\n"); return 1; }
    const auto& group0 = mesh.drawGroups()[0];
    printf("\n[group0] %zu draw ranges total\n\n", group0.size());

    // -----------------------------------------------------------------
    // 3. Resolve EACH range's own real shader stem + vertex file
    //    (STEP 1/2). submeshIndex == channel index (9/9-exact finding),
    //    so channelIndex = range.submeshIndex directly, not searched.
    // -----------------------------------------------------------------
    struct RangeResolution {
        size_t rangeIdx = 0;
        uint32_t materialId = 0, submeshIndex = 0;
        uint32_t shaderHash = 0;
        bool ok = false;
        std::string reason;
        std::string stem, vsFileName;
    };
    std::vector<RangeResolution> resolutions;
    std::map<std::string, std::vector<size_t>> byVsFile; // vsFileName -> indices into `resolutions`, ok==true only

    for (size_t ri = 0; ri < group0.size(); ++ri) {
        const auto& range = group0[ri];
        RangeResolution rr;
        rr.rangeIdx = ri; rr.materialId = range.materialId; rr.submeshIndex = range.submeshIndex;

        if (range.materialId >= hashResult.hashes.size()) {
            rr.reason = "materialId " + std::to_string(range.materialId) + " >= " +
                        std::to_string(hashResult.hashes.size()) + " real shaderHash values extracted (walk truncated)";
            resolutions.push_back(rr);
            continue;
        }
        rr.shaderHash = hashResult.hashes[range.materialId];

        auto it = stemCrcToStem.find(rr.shaderHash);
        if (it == stemCrcToStem.end()) {
            rr.reason = "shaderHash 0x" + hrToString(rr.shaderHash).substr(2) +
                        " (material " + std::to_string(range.materialId) +
                        ") does not resolve to any real .fxo_pc stem-CRC";
            resolutions.push_back(rr);
            continue;
        }
        rr.stem = it->second;

        std::string noVertexReason;
        rr.vsFileName = pickVertexFile(stemToFiles[rr.stem], noVertexReason);
        if (rr.vsFileName.empty()) {
            rr.reason = "stem '" + rr.stem + "' resolved (shaderHash 0x" + hrToString(rr.shaderHash).substr(2) +
                        ") but " + noVertexReason;
            resolutions.push_back(rr);
            continue;
        }

        rr.ok = true;
        resolutions.push_back(rr);
        byVsFile[rr.vsFileName].push_back(resolutions.size() - 1);
    }

    printf("=== Per-range shader resolution (real materialId -> real shaderHash -> real stem -> real VS file) ===\n");
    for (const auto& rr : resolutions) {
        if (rr.ok)
            printf("  range[%2zu] materialId=%3u submeshIndex=%u shaderHash=0x%08X -> stem='%s' vsFile='%s'\n",
                   rr.rangeIdx, rr.materialId, rr.submeshIndex, rr.shaderHash, rr.stem.c_str(), rr.vsFileName.c_str());
        else
            printf("  range[%2zu] materialId=%3u submeshIndex=%u -> UNRESOLVED: %s\n", rr.rangeIdx, rr.materialId,
                   rr.submeshIndex, rr.reason.c_str());
    }
    size_t resolvedCount = 0;
    for (const auto& rr : resolutions) if (rr.ok) ++resolvedCount;
    printf("\n[resolution summary] %zu/%zu ranges resolved to a real VS file, %zu distinct real VS files\n\n",
           resolvedCount, resolutions.size(), byVsFile.size());

    // -----------------------------------------------------------------
    // 4. For each DISTINCT resolved VS file: locate+pair (STEP 3),
    //    disassemble/CTAB/translate/compile/reflect ONCE (STEP 4).
    // -----------------------------------------------------------------
    std::map<std::string, CompiledShader> shaders; // keyed by vsFileName
    std::map<std::string, std::vector<uint8_t>> fxoBytesCache;

    for (const auto& kv : byVsFile) {
        const std::string& vsFileName = kv.first;
        CompiledShader cs;
        cs.vsFileName = vsFileName;
        cs.stem = resolutions[kv.second[0]].stem;
        cs.rangesUsing = kv.second.size();

        std::vector<uint8_t> fxoBytes;
        if (!findEntry(shadersContainer, vsFileName, fxoBytes)) {
            cs.failReason = "real file '" + vsFileName + "' named by the stem table not found via top-level lookup "
                             "in shaders.vpp_pc (may be nested - not chased further this pass)";
            shaders[vsFileName] = cs;
            continue;
        }
        // NEW in this fork: ir_sr3carpaint_gr's own ranges use PASS 2 (the
        // real forward/composite pass with Base_Paint_Color) instead of the
        // generic pairVsPs() resolution, which would otherwise always land
        // on pass 0 (the real deferred G-buffer pass) - see this file's own
        // top comment. Every other shader is completely unaffected.
        const bool usePaintPass2 = (cs.stem == "ir_sr3carpaint_gr");
        BlobPairResult pair = usePaintPass2 ? pairVsPsForPass(fxoBytes, 2) : pairVsPs(fxoBytes);
        if (!pair.ok) {
            cs.failReason = "T8 pairing failed: " + pair.reason;
            shaders[vsFileName] = cs;
            continue;
        }
        if (usePaintPass2)
            printf("[paint-pass override] '%s' (stem='%s'): using real T8 PASS 2 (forward/composite, "
                   "Base_Paint_Color-bearing) instead of the generic pass-0 resolution - VS(offset=%zu len=%zu) "
                   "PS(offset=%zu len=%zu)\n",
                   vsFileName.c_str(), cs.stem.c_str(), pair.vsOffset, pair.vsLength, pair.psOffset, pair.psLength);

        vpp::ByteView vsBlobView(fxoBytes.data() + pair.vsOffset, pair.vsLength);
        vpp::ByteView psBlobView(fxoBytes.data() + pair.psOffset, pair.psLength);
        cs.vsDis = sr3d3d9bc::disassemble(vsBlobView);
        cs.psDis = sr3d3d9bc::disassemble(psBlobView);
        cs.vsCtab = sr3d3d9bc::readConstantTable(vsBlobView, cs.vsDis);
        cs.psCtab = sr3d3d9bc::readConstantTable(psBlobView, cs.psDis);
        cs.vsTr = sr3d3d9bc::translateToHlsl(cs.vsDis, cs.vsCtab, vsBlobView, sr3d3d9bc::HlslTarget::SM4_5);
        cs.psTr = sr3d3d9bc::translateToHlsl(cs.psDis, cs.psCtab, psBlobView, sr3d3d9bc::HlslTarget::SM4_5);
        cs.vsInputs = extractVsInputSignature(cs.vsDis);

        printf("[shader '%s'] stem='%s' usedByRanges=%zu VS(offset=%zu len=%zu) PS(offset=%zu len=%zu)\n",
               vsFileName.c_str(), cs.stem.c_str(), cs.rangesUsing, pair.vsOffset, pair.vsLength, pair.psOffset,
               pair.psLength);
        printf("  [translate] VS complete=%d unsupported=%zu warnings=%zu  PS complete=%d unsupported=%zu warnings=%zu\n",
               cs.vsTr.complete, cs.vsTr.unsupported.size(), cs.vsTr.warnings.size(), cs.psTr.complete,
               cs.psTr.unsupported.size(), cs.psTr.warnings.size());
        for (const auto& u : cs.vsTr.unsupported) printf("    VS UNSUPPORTED: %s\n", u.c_str());
        for (const auto& u : cs.psTr.unsupported) printf("    PS UNSUPPORTED: %s\n", u.c_str());
        printf("  [signature] real DCL-derived VS inputs: %s\n", signatureToString(cs.vsInputs).c_str());

        // Subset check, generalized from the single-shader prototype's own
        // exact-5 check: any real subset of {POSITION0,NORMAL0,TANGENT0,
        // BLENDINDICES0,TEXCOORD0} is acceptable (this is exactly what lets
        // a tangent-less channel pair with a shader that itself doesn't
        // need TANGENT0 - the orchestrator's own stated goal). POSITION0 is
        // mandatory. Anything OUTSIDE that known set is a genuine
        // "doesn't generalize" finding, reported and skipped, not forced.
        auto hasUsage = [&](uint8_t usage, uint8_t idx) {
            for (const auto& f : cs.vsInputs) if (f.usage == usage && f.usageIndex == idx) return true;
            return false;
        };
        bool hasPosition = hasUsage(kPOSITION, 0);
        bool outsideKnownSet = false;
        for (const auto& f : cs.vsInputs) {
            bool known = (f.usage == kPOSITION && f.usageIndex == 0) || (f.usage == kNORMAL && f.usageIndex == 0) ||
                         (f.usage == kTANGENT && f.usageIndex == 0) || (f.usage == kBLENDINDICES && f.usageIndex == 0) ||
                         (f.usage == kTEXCOORD && f.usageIndex == 0);
            if (!known) outsideKnownSet = true;
        }
        if (!hasPosition || outsideKnownSet) {
            cs.failReason = "real VS input signature is not a subset of the known {POSITION0,NORMAL0,TANGENT0,"
                             "BLENDINDICES0,TEXCOORD0} set this tool's fixed GpuVertexReal layout supports "
                             "(signature: " + signatureToString(cs.vsInputs) + ") - a genuine 'doesn't generalize' "
                             "finding, not forced to fit";
            printf("  UNSUPPORTED SIGNATURE: %s\n", cs.failReason.c_str());
            shaders[vsFileName] = cs;
            continue;
        }

        ID3DBlob *vsErr = nullptr, *psErr = nullptr;
        HRESULT hr = D3DCompile(cs.vsTr.hlsl.c_str(), cs.vsTr.hlsl.size(), vsFileName.c_str(), nullptr, nullptr,
                                 cs.vsTr.entryPoint.c_str(), cs.vsTr.targetProfile.c_str(),
                                 D3DCOMPILE_SKIP_OPTIMIZATION, 0, &cs.vsCode, &vsErr);
        if (FAILED(hr)) {
            cs.failReason = "D3DCompile(VS) failed: " + hrToString(hr);
            if (vsErr) cs.failReason += std::string(" - ") + static_cast<const char*>(vsErr->GetBufferPointer());
            safeRelease(vsErr);
            printf("  FATAL for this shader: %s\n", cs.failReason.c_str());
            shaders[vsFileName] = cs;
            continue;
        }
        safeRelease(vsErr);
        hr = D3DCompile(cs.psTr.hlsl.c_str(), cs.psTr.hlsl.size(), (vsFileName + "_ps").c_str(), nullptr, nullptr,
                         cs.psTr.entryPoint.c_str(), cs.psTr.targetProfile.c_str(), D3DCOMPILE_SKIP_OPTIMIZATION, 0,
                         &cs.psCode, &psErr);
        if (FAILED(hr)) {
            cs.failReason = "D3DCompile(PS) failed: " + hrToString(hr);
            if (psErr) cs.failReason += std::string(" - ") + static_cast<const char*>(psErr->GetBufferPointer());
            safeRelease(psErr);
            printf("  FATAL for this shader: %s\n", cs.failReason.c_str());
            shaders[vsFileName] = cs;
            continue;
        }
        safeRelease(psErr);
        printf("  [compile] D3DCompile SUCCEEDED for both VS/PS (%s/%s)\n", cs.vsTr.targetProfile.c_str(),
               cs.psTr.targetProfile.c_str());

        D3DReflect(cs.vsCode->GetBufferPointer(), cs.vsCode->GetBufferSize(), __uuidof(ID3D11ShaderReflection),
                   reinterpret_cast<void**>(&cs.vsRefl));
        D3DReflect(cs.psCode->GetBufferPointer(), cs.psCode->GetBufferSize(), __uuidof(ID3D11ShaderReflection),
                   reinterpret_cast<void**>(&cs.psRefl));
        if (!cs.vsRefl || !cs.psRefl) {
            cs.failReason = "D3DReflect failed";
            shaders[vsFileName] = cs;
            continue;
        }
        {
            ID3D11ShaderReflectionConstantBuffer* cb = cs.vsRefl->GetConstantBufferByIndex(0);
            D3D11_SHADER_BUFFER_DESC bd{};
            if (cb) cb->GetDesc(&bd);
            cs.vsCbSize = bd.Size;
        }
        {
            ID3D11ShaderReflectionConstantBuffer* cb = cs.psRefl->GetConstantBufferByIndex(0);
            D3D11_SHADER_BUFFER_DESC bd{};
            if (cb) cb->GetDesc(&bd);
            cs.psCbSize = bd.Size;
        }
        D3D11_SHADER_DESC psDesc{};
        cs.psRefl->GetDesc(&psDesc);
        for (UINT r = 0; r < psDesc.BoundResources; ++r) {
            D3D11_SHADER_INPUT_BIND_DESC bind{};
            cs.psRefl->GetResourceBindingDesc(r, &bind);
            printf("  [reflect] PS resource: name='%s' type=%d bindPoint=%u\n", bind.Name, static_cast<int>(bind.Type),
                   bind.BindPoint);
            if (bind.Type == D3D_SIT_TEXTURE) {
                cs.allTexBindPoints.push_back(bind.BindPoint);
                cs.texBindPoint = static_cast<int>(bind.BindPoint);
                cs.psTextureResourceName = bind.Name;
                if (containsCI(bind.Name, "normal")) cs.textureKind = "normal";
                else if (containsCI(bind.Name, "diffuse") || containsCI(bind.Name, "albedo") ||
                         containsCI(bind.Name, "color") || containsCI(bind.Name, "colour"))
                    cs.textureKind = "diffuse";
                else
                    cs.textureKind = "unknown";
            } else if (bind.Type == D3D_SIT_SAMPLER) {
                cs.allSamplerBindPoints.push_back(bind.BindPoint);
                cs.samplerBindPoint = static_cast<int>(bind.BindPoint);
            }
        }
        cs.ok = true;
        printf("  RESOLVED AND COMPILED OK. reflected cbuffers: VS=%u bytes PS=%u bytes. textureKind='%s'\n",
               cs.vsCbSize, cs.psCbSize, cs.textureKind.c_str());
        if (usePaintPass2) {
            printf("  [note] pass 2's PS reflects %u real texture/sampler resources (Damage_Normal_MapSampler, "
                   "grime_mapSampler, IR_GBuffer_DSF_DataSampler, Dual_Paraboloid_Map_Front/BackSampler, "
                   "IR_LBufferSampler) - this prototype family's original single-slot design (cs.texBindPoint/"
                   "samplerBindPoint) would leave all but one unbound (D3D11: reads as (0,0,0,0), not a crash).\n"
                   "  A first render done exactly that way rendered successfully but showed NO visible difference "
                   "between the real Base_Paint_Color and a wild sanity-check color (pure red) - traced directly "
                   "against the real translated HLSL body (not guessed): IR_LBufferSampler's real .x channel is a "
                   "genuine MULTIPLICATIVE light-intensity factor on the diffuse-paint term in this shader's own "
                   "real composite math, so leaving it unbound (=0) zeroes the entire paint-color contribution "
                   "regardless of what Base_Paint_Color is set to - the visible body color in that render was "
                   "actually just the flat Fog_color/Tint_color PLACEHOLDERS (8.0 each) times a real per-vertex "
                   "fog interpolant, not the paint color at all. FIX (still honestly a placeholder, not real "
                   "lighting data): every one of this shader's %zu real texture slots and %zu real sampler slots is "
                   "now bound to a NEUTRAL WHITE placeholder (not left unbound), specifically so the real "
                   "Base_Paint_Color's multiplicative dependency chain is not masked to hard zero.\n",
                   psDesc.BoundResources, cs.allTexBindPoints.size(), cs.allSamplerBindPoints.size());
        }
        printf("\n");
        shaders[vsFileName] = cs;
    }

    // Re-mark resolutions whose shader ultimately failed to compile/reflect
    // as unresolved for drawing purposes (still reported above with its
    // real reason).
    size_t drawableRanges = 0;
    for (auto& rr : resolutions) {
        if (rr.ok && !shaders[rr.vsFileName].ok) {
            rr.ok = false;
            rr.reason = "shader '" + rr.vsFileName + "' failed to compile/reflect: " + shaders[rr.vsFileName].failReason;
        }
        if (rr.ok) ++drawableRanges;
    }

    printf("=== Distinct real shader resolution summary ===\n");
    for (const auto& kv : shaders) {
        printf("  '%s' (stem='%s'): %s, usedByRanges=%zu\n", kv.first.c_str(), kv.second.stem.c_str(),
               kv.second.ok ? "OK" : ("FAILED: " + kv.second.failReason).c_str(), kv.second.rangesUsing);
    }
    printf("\n[final] %zu/%zu real draw ranges will actually be drawn, using %zu real distinct shaders\n\n",
           drawableRanges, resolutions.size(), shaders.size());
    if (drawableRanges == 0) {
        printf("FATAL (genuine dead end, reported honestly): no range resolved to a usable real shader.\n");
        return 1;
    }

    // -----------------------------------------------------------------
    // 5. Decode every REAL channel actually needed (submeshIndex set of
    //    the drawable ranges), with placeholder fallback ONLY for a field
    //    a resolved shader needs that the channel's own real layout
    //    genuinely lacks (STEP 5 - the orchestrator's specific "TANGENT0
    //    on layoutCode=100" case, tested here for real, not assumed).
    // -----------------------------------------------------------------
    std::set<int> neededChannels;
    std::map<int, bool> channelNeedsTangent, channelNeedsNormal, channelNeedsTexcoord;
    for (const auto& rr : resolutions) {
        if (!rr.ok) continue;
        int ch = static_cast<int>(rr.submeshIndex);
        neededChannels.insert(ch);
        const auto& cs = shaders[rr.vsFileName];
        bool needsTangent = false, needsNormal = false, needsTexcoord = false;
        for (const auto& f : cs.vsInputs) {
            if (f.usage == kTANGENT) needsTangent = true;
            if (f.usage == kNORMAL) needsNormal = true;
            if (f.usage == kTEXCOORD) needsTexcoord = true;
        }
        channelNeedsTangent[ch] = channelNeedsTangent[ch] || needsTangent;
        channelNeedsNormal[ch] = channelNeedsNormal[ch] || needsNormal;
        channelNeedsTexcoord[ch] = channelNeedsTexcoord[ch] || needsTexcoord;
    }

    struct ChannelData {
        int channelIndex = -1;
        std::vector<sr3mesh::Vertex> verts;
        std::vector<GpuVertexReal> gpuVerts;
        std::vector<uint32_t> gpuIndices; // concatenated across all its ranges, in range order
        std::map<size_t, std::pair<size_t, size_t>> rangeIndexSpan; // rangeIdx -> (startOffset, count) within gpuIndices
        ID3D11Buffer* vb = nullptr;
        ID3D11Buffer* ib = nullptr;
        sr3mesh::LayoutInfo layoutInfo;
        uint8_t layoutCode = 0;
        uint8_t texcoordCount = 0;
    };
    std::map<int, ChannelData> channels;

    printf("=== Real channel decode (channelIndex == submeshIndex, 9/9-exact finding) ===\n");
    for (int ch : neededChannels) {
        if (ch < 0 || static_cast<size_t>(ch) >= mesh.channels().size()) {
            printf("  channel %d: FATAL - out of range (%zu channels total) - all ranges needing it are dropped\n", ch,
                   mesh.channels().size());
            continue;
        }
        ChannelData cd;
        cd.channelIndex = ch;
        const auto& chanMeta = mesh.channels()[static_cast<size_t>(ch)];
        cd.layoutCode = chanMeta.layoutCode;
        cd.texcoordCount = chanMeta.texcoordCount;
        cd.layoutInfo = sr3mesh::layoutInfoFor(chanMeta.layoutCode);
        try {
            cd.verts = mesh.decodeChannel(static_cast<size_t>(ch));
        } catch (const std::exception& ex) {
            printf("  channel %d: decodeChannel FAILED: %s - all ranges needing it are dropped\n", ch, ex.what());
            continue;
        }
        printf("  channel %d: layoutCode=%u texcoordCount=%u vertices=%zu hasNormal=%d hasTangent=%d hasRigidPart=%d\n",
               ch, cd.layoutCode, cd.texcoordCount, cd.verts.size(), cd.layoutInfo.hasNormal, cd.layoutInfo.hasTangent,
               cd.layoutInfo.hasRigidPart);

        bool placeholderTangent = channelNeedsTangent[ch] && !cd.layoutInfo.hasTangent;
        bool placeholderNormal = channelNeedsNormal[ch] && !cd.layoutInfo.hasNormal;
        bool placeholderTexcoord = channelNeedsTexcoord[ch] && cd.texcoordCount == 0;
        if (placeholderTangent)
            printf("    PLACEHOLDER: a resolved shader drawing from this channel declares TANGENT0, but "
                   "layoutCode=%u genuinely has no tangent data (sr3mesh::layoutInfoFor().hasTangent==false) - "
                   "filling a constant placeholder tangent for all %zu vertices in this channel, same fallback "
                   "standard as the single-shader prototype's own texture placeholder\n",
                   cd.layoutCode, cd.verts.size());
        if (placeholderNormal)
            printf("    PLACEHOLDER: a resolved shader declares NORMAL0 but this channel has none - filling a "
                   "constant placeholder normal for all %zu vertices\n", cd.verts.size());
        if (placeholderTexcoord)
            printf("    PLACEHOLDER: a resolved shader declares TEXCOORD0 but this channel has 0 texcoord sets - "
                   "filling (0,0) for all %zu vertices\n", cd.verts.size());

        cd.gpuVerts.resize(cd.verts.size());
        for (size_t i = 0; i < cd.verts.size(); ++i) {
            const auto& v = cd.verts[i];
            GpuVertexReal g{};
            g.position[0] = v.position[0]; g.position[1] = v.position[1]; g.position[2] = v.position[2]; g.position[3] = 1.0f;
            if (cd.layoutInfo.hasNormal) {
                g.normal[0] = v.normal[0] * 0.5f + 0.5f; g.normal[1] = v.normal[1] * 0.5f + 0.5f;
                g.normal[2] = v.normal[2] * 0.5f + 0.5f; g.normal[3] = v.normalW / 255.0f;
            } else {
                g.normal[0] = 0.5f; g.normal[1] = 0.5f; g.normal[2] = 1.0f; g.normal[3] = 0.5f; // pre-image of "flat up" (0,0,1)
            }
            if (cd.layoutInfo.hasTangent) {
                g.tangent[0] = v.tangent[0] * 0.5f + 0.5f; g.tangent[1] = v.tangent[1] * 0.5f + 0.5f;
                g.tangent[2] = v.tangent[2] * 0.5f + 0.5f; g.tangent[3] = v.tangentW / 255.0f;
            } else {
                g.tangent[0] = 1.0f; g.tangent[1] = 0.5f; g.tangent[2] = 0.5f; g.tangent[3] = 0.5f; // pre-image of "flat +X" (1,0,0)
            }
            g.blendIndices[0] = g.blendIndices[1] = g.blendIndices[2] = g.blendIndices[3] =
                static_cast<float>(v.rigidPartIndex);
            if (!v.texcoordsRaw.empty()) {
                g.texcoord0[0] = static_cast<float>(v.texcoordsRaw[0][0]);
                g.texcoord0[1] = static_cast<float>(v.texcoordsRaw[0][1]);
            }
            cd.gpuVerts[i] = g;
        }
        channels[ch] = std::move(cd);
    }

    // Build per-range index spans, concatenated per channel.
    for (const auto& rr : resolutions) {
        if (!rr.ok) continue;
        int ch = static_cast<int>(rr.submeshIndex);
        auto cit = channels.find(ch);
        if (cit == channels.end()) continue; // channel decode failed - already reported above
        std::vector<uint32_t> tri = mesh.triangleListForRange(group0[rr.rangeIdx]);
        size_t start = cit->second.gpuIndices.size();
        cit->second.gpuIndices.insert(cit->second.gpuIndices.end(), tri.begin(), tri.end());
        cit->second.rangeIndexSpan[rr.rangeIdx] = {start, tri.size()};
    }
    printf("\n");

    // -----------------------------------------------------------------
    // 6. Real object-space bounds (camera framing only) across every
    //    channel actually drawn, after the real per-part transform.
    // -----------------------------------------------------------------
    float boundsMin[3] = {1e30f, 1e30f, 1e30f}, boundsMax[3] = {-1e30f, -1e30f, -1e30f};
    uint32_t clampedPartIndices = 0;
    size_t totalVertsForBounds = 0;
    for (auto& kv : channels) {
        for (const auto& v : kv.second.verts) {
            float finalPos[3];
            if (v.rigidPartIndex < veh.parts().size()) {
                transformPointRowVector(v.position.data(), veh.parts()[v.rigidPartIndex].transform.data(), finalPos);
            } else {
                ++clampedPartIndices;
                finalPos[0] = v.position[0]; finalPos[1] = v.position[1]; finalPos[2] = v.position[2];
            }
            for (int c = 0; c < 3; ++c) {
                if (finalPos[c] < boundsMin[c]) boundsMin[c] = finalPos[c];
                if (finalPos[c] > boundsMax[c]) boundsMax[c] = finalPos[c];
            }
            ++totalVertsForBounds;
        }
    }
    printf("[bounds] object-space (after real per-part transform, %zu vertices across %zu channels): "
           "min(%.3f %.3f %.3f) max(%.3f %.3f %.3f)  clamped rigidPartIndex: %u\n", totalVertsForBounds,
           channels.size(), boundsMin[0], boundsMin[1], boundsMin[2], boundsMax[0], boundsMax[1], boundsMax[2],
           clampedPartIndices);

    // -----------------------------------------------------------------
    // 7. Real D3D11 device.
    // -----------------------------------------------------------------
    DebugRenderDevice dev;
    std::string devErr;
    if (!dev.initialise(800, 600, devErr)) { printf("FATAL: %s\n", devErr.c_str()); return 1; }
    printf("\n[device] kind=%s   debugLayerActive=%d\n", dev.isWarp ? "WARP (software rasterizer)" : "Hardware",
           dev.debugLayerActive);
    if (!dev.debugLayerNote.empty()) printf("[device] %s\n", dev.debugLayerNote.c_str());

    // Camera matrices (shared across every shader - same real
    // buildOrbitViewProjection()/view-matrix-duplicate reuse as the
    // single-shader prototype).
    float viewProj[16];
    const float yaw = 0.6f, pitch = 0.35f, distanceScale = 1.8f;
    sr3render::buildOrbitViewProjection(boundsMin, boundsMax, yaw, pitch, distanceScale, 800.0f / 600.0f, viewProj);
    float viewOnly[16];
    computeOrbitViewMatrix(boundsMin, boundsMax, yaw, pitch, distanceScale, viewOnly);
    float worldIdentity[16];
    sr3render::buildWorldMatrix(0.0f, 0.0f, 0.0f, 0.0f, 1.0f, worldIdentity);

    // -----------------------------------------------------------------
    // 8. GPU objects per distinct shader: VS/PS/InputLayout/cbuffers,
    //    filled by the SAME generic CTAB-name-driven logic the
    //    single-shader prototype already uses (STEP 4, run per shader).
    // -----------------------------------------------------------------
    for (auto& kv : shaders) {
        CompiledShader& cs = kv.second;
        if (!cs.ok) continue;
        HRESULT hr = dev.device->CreateVertexShader(cs.vsCode->GetBufferPointer(), cs.vsCode->GetBufferSize(), nullptr, &cs.vs);
        if (FAILED(hr)) { cs.ok = false; cs.failReason = "CreateVertexShader failed: " + hrToString(hr); continue; }
        hr = dev.device->CreatePixelShader(cs.psCode->GetBufferPointer(), cs.psCode->GetBufferSize(), nullptr, &cs.ps);
        if (FAILED(hr)) { cs.ok = false; cs.failReason = "CreatePixelShader failed: " + hrToString(hr); continue; }
        hr = dev.device->CreateInputLayout(kFullLayout, 5, cs.vsCode->GetBufferPointer(), cs.vsCode->GetBufferSize(),
                                            &cs.inputLayout);
        if (FAILED(hr)) { cs.ok = false; cs.failReason = "CreateInputLayout failed: " + hrToString(hr); continue; }

        cs.vsCbufCpu.assign(cs.vsCbSize, 0);
        for (const auto& c : cs.vsCtab.constants) {
            if (c.registerSet != sr3d3d9bc::RegisterSet::Float4) continue;
            std::string decision;
            std::vector<std::array<float, 4>> regs;
            // HYPOTHESIS-BASED (render-pipeline provenance downgrade, 2026-09-30): filling a CTAB constant
            // named *proj* (projTM, VS c28x4) with the fused view*projection and *world2view* (c48) with the
            // view matrix rests on spec-render-pipeline.md Sec20.12.5/Sec20.12.9, now HYPOTHESIS on provenance
            // pending re-derivation from the shipped shaders (bridge job 06, ctab_census). No behaviour change.
            if (containsCI(c.name, "proj")) {
                for (int i = 0; i < 4; ++i) { std::array<float, 4> r; packColumn(viewProj, i, r.data()); regs.push_back(r); }
                decision = "REAL: buildOrbitViewProjection() (view*proj), framed to the real decoded+transformed bounds";
            } else if (containsCI(c.name, "world2view")) {
                for (int i = 0; i < 3; ++i) { std::array<float, 4> r; packColumn(viewOnly, i, r.data()); regs.push_back(r); }
                decision = "REAL: rotation-only part of the same real orbit-camera view matrix";
            } else if (containsCI(c.name, "obj") && containsCI(c.name, "tm")) {
                for (int i = 0; i < 3; ++i) { std::array<float, 4> r; packColumn(worldIdentity, i, r.data()); regs.push_back(r); }
                decision = "PLACEHOLDER: identity world/placement transform - no real scene-placement data exists";
            } else if (containsCI(c.name, "bone")) {
                const size_t maxElems = c.registerCount / 3;
                for (size_t e = 0; e < maxElems; ++e) {
                    float packed[3][4];
                    if (e < veh.parts().size()) {
                        for (int i = 0; i < 3; ++i) packColumn(veh.parts()[e].transform.data(), i, packed[i]);
                    } else {
                        for (int i = 0; i < 3; ++i) packColumn(worldIdentity, i, packed[i]);
                    }
                    for (int i = 0; i < 3; ++i) { std::array<float, 4> r; std::memcpy(r.data(), packed[i], 16); regs.push_back(r); }
                }
                decision = "REAL: sr3vehicle::VehiclePart::transform per real part (ASSUMPTION: rigidPartIndex indexes "
                           "Vehicle::parts() directly in file order)";
            } else if (cs.stem == "ir_sr3carpaint_gr" && containsCI(c.name, "grime_tiling")) {
                // NEW in this fork: real per-material value, same array-B/C
                // mechanism as the PS constants below (step 8b) - hash
                // 0xD12BF5EF/0x4822A455 for grime_tiling_u/v. Filled here
                // (not per-material) because this reader directly checked
                // all 6 real ir_sr3carpaint_gr material records in this
                // vehicle and found grime_tiling_u=grime_tiling_v=3.0
                // IDENTICALLY across every one of them - a real, checked
                // fact for THIS vehicle, not an assumption that it always
                // holds. registerCount==1 for both in this shader's own CTAB.
                for (uint16_t r = 0; r < c.registerCount; ++r) regs.push_back({3.0f, 3.0f, 3.0f, 3.0f});
                decision = "REAL: car_4dr_genki_0.ccar_pc material record array B/C, checked identical (3.0) "
                           "across all 6 real ir_sr3carpaint_gr material instances in this vehicle";
            } else {
                for (uint16_t r = 0; r < c.registerCount; ++r) regs.push_back({0, 0, 0, 0});
                decision = "PLACEHOLDER: zero (untraced per-instance parameter)";
            }
            fillFloat4Registers(cs.vsCbufCpu, c.registerIndex, c.registerCount, regs);
            fillLog.push_back({cs.vsFileName + " VS " + c.name, decision});
        }

        const bool isPaintPass2 = (cs.stem == "ir_sr3carpaint_gr");
        cs.psCbufCpu.assign(cs.psCbSize, 0);
        for (const auto& c : cs.psCtab.constants) {
            if (c.registerSet != sr3d3d9bc::RegisterSet::Float4) continue;
            std::vector<std::array<float, 4>> regs;
            std::string decision;
            // REPLACED 2026-09-29 (Team A, spec-render-pipeline.md §20.12.11,
            // disassembly-confirmed, exhaustive): the per-material constant
            // buffer is memset to zero, then only AUTHORED entries get
            // written in by the remap pass (§20.12.6). Only 2 of this
            // shader's PS constants have any engine default at all -
            // Specular_Power (floored to 0.1 when the authored/zero value is
            // invalid) and Normal_Map_Height (clamped into [0,1], a no-op
            // when the unauthored value is already 0). Every other named
            // constant an individual material doesn't author (Tint_color,
            // Fog_color, grime_*, Fresnel_*, Reflection_*,
            // Ambient_Reflection_Amount, Specular_Color/Alpha, etc.) is a
            // REAL, CONFIRMED zero in the shipped game when unauthored - not
            // a placeholder anymore, this is the engine's own real behaviour.
            if (containsCI(c.name, "specular_power")) {
                for (uint16_t r = 0; r < c.registerCount; ++r) regs.push_back({0.1f, 0.1f, 0.1f, 0.1f});
                decision = "REAL engine default (spec-render-pipeline.md §20.12.11): 0.1 floor, used when the "
                           "material's own authored value (below, if any) is absent/invalid";
            } else {
                for (uint16_t r = 0; r < c.registerCount; ++r) regs.push_back({0.0f, 0.0f, 0.0f, 0.0f});
                decision = "REAL confirmed zero (spec-render-pipeline.md §20.12.11): unauthored per-material "
                           "constants sit at a memset-zero buffer in the shipped game, not a guess - overridden "
                           "per-material below for ir_sr3carpaint_gr where a real name-hash match exists (step 8b)";
            }
            fillFloat4Registers(cs.psCbufCpu, c.registerIndex, c.registerCount, regs);
            fillLog.push_back({cs.vsFileName + " PS " + c.name, decision});
        }
        (void)isPaintPass2; // used below (step 8b), kept here for locality of the flag's definition

        D3D11_BUFFER_DESC cbVsDesc{}; cbVsDesc.ByteWidth = static_cast<UINT>((cs.vsCbufCpu.size() + 15) & ~size_t(15));
        cbVsDesc.Usage = D3D11_USAGE_DEFAULT; cbVsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cs.vsCbufCpu.resize(cbVsDesc.ByteWidth, 0);
        D3D11_SUBRESOURCE_DATA cbVsData{}; cbVsData.pSysMem = cs.vsCbufCpu.data();
        dev.device->CreateBuffer(&cbVsDesc, &cbVsData, &cs.cbVs);

        D3D11_BUFFER_DESC cbPsDesc{}; cbPsDesc.ByteWidth = static_cast<UINT>((cs.psCbufCpu.size() + 15) & ~size_t(15));
        cbPsDesc.Usage = D3D11_USAGE_DEFAULT; cbPsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cs.psCbufCpu.resize(cbPsDesc.ByteWidth, 0);
        D3D11_SUBRESOURCE_DATA cbPsData{}; cbPsData.pSysMem = cs.psCbufCpu.data();
        dev.device->CreateBuffer(&cbPsDesc, &cbPsData, &cs.cbPs);

        // -------------------------------------------------------------
        // Step 8b, NEW in this fork: per-material real PS cbuffer override
        // for ir_sr3carpaint_gr. Base_Paint_Color and its real siblings
        // (Specular_Color/Alpha, Fresnel_*, Reflection_Cos_Min_Angles/
        // Reflection_Inv_Range_Cos_Angles, Ambient_Reflection_Amount,
        // grime_amount/grime_override) are genuinely PER-MATERIAL, not
        // per-shader-file - car_4dr_genki_0's own 4 real ir_sr3carpaint_gr
        // materials (ids 0/1/2/3) carry two DISTINCT real Base_Paint_Color
        // values (materials 0/1 vs 2/3), confirmed directly against real
        // bytes (this file's own top comment / main()'s arrayBC printout).
        // One cbuffer is built per DISTINCT real materialId actually used
        // by this shader's own ranges, starting from the SAME real-zero/
        // Specular_Power-0.1 template above and overriding only the
        // registers with a real array-B name-hash match for THAT specific
        // material.
        // -------------------------------------------------------------
        if (isPaintPass2 && byVsFile.count(kv.first)) {
            std::set<uint32_t> materialIdsUsed;
            for (size_t idx : byVsFile[kv.first]) materialIdsUsed.insert(resolutions[idx].materialId);
            for (uint32_t materialId : materialIdsUsed) {
                std::vector<uint8_t> overridden = cs.psCbufCpu; // start from the real-zero/0.1 template, already sized/padded
                size_t realMatches = 0;
                for (const auto& c : cs.psCtab.constants) {
                    if (c.registerSet != sr3d3d9bc::RegisterSet::Float4) continue;
                    uint32_t h = sr3fxo::hashLowerName(c.name);
                    const std::array<float, 4>* real = findMaterialConstant(materialId, h);
                    if (!real) continue;
                    std::vector<std::array<float, 4>> regs;
                    regs.push_back(*real); // the FULL real Vector4, written verbatim - matches the real engine's own
                                            // "copy declaration-order slot into register-indexed position" mechanism
                                            // (spec-render-pipeline.md §20.12.6), not a component-broadcast guess
                    for (uint16_t r = 1; r < c.registerCount; ++r) regs.push_back({0.0f, 0.0f, 0.0f, 0.0f}); // extra registers beyond the first: no real match, real confirmed zero (§20.12.11), not a placeholder
                    fillFloat4Registers(overridden, c.registerIndex, c.registerCount, regs);
                    ++realMatches;
                    fillLog.push_back({cs.vsFileName + " PS " + c.name + " [material " + std::to_string(materialId) + "]",
                                        "REAL: car_4dr_genki_0.ccar_pc material record array B/C, hash 0x" +
                                        hrToString(h).substr(2) + " exact match, value=(" +
                                        std::to_string((*real)[0]) + ", " + std::to_string((*real)[1]) + ", " +
                                        std::to_string((*real)[2]) + ", " + std::to_string((*real)[3]) + ")"});
                }
                D3D11_BUFFER_DESC d{}; d.ByteWidth = static_cast<UINT>(overridden.size());
                d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = overridden.data();
                ID3D11Buffer* buf = nullptr;
                dev.device->CreateBuffer(&d, &sd, &buf);
                cs.perMaterialCbPs[materialId] = buf;
                printf("[material cbuffer] '%s' materialId=%u: %zu/%zu PS constants matched a real per-material "
                       "value (the rest keep the flat-8.0 placeholder)\n",
                       kv.first.c_str(), materialId, realMatches, cs.psCtab.constants.size());
            }
        }
    }

    printf("\n=== Constant-buffer fill decisions (real vs. placeholder, by real CTAB name, per shader) ===\n");
    for (const auto& l : fillLog) printf("  %-52s : %s\n", l.constName.c_str(), l.decision.c_str());

    // -----------------------------------------------------------------
    // 9. Per-channel GPU vertex/index buffers.
    // -----------------------------------------------------------------
    for (auto& kv : channels) {
        ChannelData& cd = kv.second;
        if (cd.gpuVerts.empty() || cd.gpuIndices.empty()) continue;
        D3D11_BUFFER_DESC vbDesc{}; vbDesc.ByteWidth = static_cast<UINT>(cd.gpuVerts.size() * sizeof(GpuVertexReal));
        vbDesc.Usage = D3D11_USAGE_IMMUTABLE; vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vbData{}; vbData.pSysMem = cd.gpuVerts.data();
        dev.device->CreateBuffer(&vbDesc, &vbData, &cd.vb);

        D3D11_BUFFER_DESC ibDesc{}; ibDesc.ByteWidth = static_cast<UINT>(cd.gpuIndices.size() * sizeof(uint32_t));
        ibDesc.Usage = D3D11_USAGE_IMMUTABLE; ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA ibData{}; ibData.pSysMem = cd.gpuIndices.data();
        dev.device->CreateBuffer(&ibDesc, &ibData, &cd.ib);
    }

    // -----------------------------------------------------------------
    // 10. Real per-range texture resolution: shader's own textureKind
    //     (from its own real PS CTAB/reflection) x the range's own real
    //     material binding (bindings.materials()[materialId]), cached by
    //     matched real texture record name. Falls back to a shared 1x1
    //     placeholder SRV, same standard as the single-shader prototype.
    // -----------------------------------------------------------------
    ID3D11SamplerState* sampler = nullptr;
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    dev.device->CreateSamplerState(&sd, &sampler);

    ID3D11Texture2D* placeholderTex = nullptr;
    ID3D11ShaderResourceView* placeholderSrv = nullptr;
    {
        D3D11_TEXTURE2D_DESC td{}; td.Width = 1; td.Height = 1; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        uint8_t px[4] = {128, 128, 255, 255};
        D3D11_SUBRESOURCE_DATA sub{}; sub.pSysMem = px; sub.SysMemPitch = 4;
        dev.device->CreateTexture2D(&td, &sub, &placeholderTex);
        dev.device->CreateShaderResourceView(placeholderTex, nullptr, &placeholderSrv);
    }

    // NEW in this fork: a NEUTRAL WHITE placeholder, distinct from the
    // normal-map-shaped placeholder above (128,128,255,255 reads as a flat
    // "up" normal, wrong semantics for a lighting/environment read). Used
    // ONLY to fill ir_sr3carpaint_gr pass-2's OTHER real texture slots
    // (IR_GBuffer_DSF_DataSampler, IR_LBufferSampler, the two Dual_
    // Paraboloid environment maps, grime_mapSampler) that this single-pass
    // prototype cannot populate with real deferred-pipeline content -
    // honestly a "full light / no data" stand-in, not a claim of real
    // lighting or environment content. Left them fully unbound instead
    // (this prototype family's original behaviour) confirmed via direct
    // HLSL trace to multiply the entire real Base_Paint_Color contribution
    // to exactly zero (IR_LBufferSampler's real .x channel is a genuine
    // multiplicative light-intensity factor on the diffuse paint term in
    // this shader's own real composite math) - this white placeholder is
    // what actually lets the real fed color become visible at all, and is
    // reported as exactly that: a placeholder enabling a real value to be
    // checked, not itself a real value.
    ID3D11Texture2D* whitePlaceholderTex = nullptr;
    ID3D11ShaderResourceView* whitePlaceholderSrv = nullptr;
    {
        D3D11_TEXTURE2D_DESC td{}; td.Width = 1; td.Height = 1; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        uint8_t px[4] = {255, 255, 255, 255};
        D3D11_SUBRESOURCE_DATA sub{}; sub.pSysMem = px; sub.SysMemPitch = 4;
        dev.device->CreateTexture2D(&td, &sub, &whitePlaceholderTex);
        dev.device->CreateShaderResourceView(whitePlaceholderTex, nullptr, &whitePlaceholderSrv);
    }

    sr3texture::TexturePair texPair;
    bool haveTexPair = false;
    if (!cpegBytes.empty()) {
        try {
            texPair = sr3texture::TexturePair::parse(sr3texture::ByteView(cpegBytes.data(), cpegBytes.size()));
            haveTexPair = true;
        } catch (const std::exception& ex) {
            printf("[texture] cpeg TexturePair::parse failed: %s - every range will use the placeholder texture\n", ex.what());
        }
    }

    std::map<std::string, ID3D11ShaderResourceView*> textureSrvCache; // matched real record name -> SRV
    std::map<size_t, ID3D11ShaderResourceView*> rangeSrv; // rangeIdx -> bound SRV (real or placeholder)
    std::set<std::string> loggedTextureDecisions;

    for (const auto& rr : resolutions) {
        if (!rr.ok) continue;
        const CompiledShader& cs = shaders[rr.vsFileName];
        if (cs.texBindPoint < 0) { rangeSrv[rr.rangeIdx] = nullptr; continue; } // shader has no texture resource at all

        const std::string* wantName = nullptr;
        if (rr.materialId < bindings.materials().size()) {
            const auto& mat = bindings.materials()[rr.materialId];
            if (cs.textureKind == "normal") wantName = mat.normalMap();
            else if (cs.textureKind == "diffuse") wantName = mat.diffuse();
        }

        std::string logKey = rr.vsFileName + "#" + std::to_string(rr.materialId);
        if (wantName != nullptr && haveTexPair) {
            std::string wantLower = lower(*wantName);
            size_t dot = wantLower.find_last_of('.');
            if (dot != std::string::npos) wantLower = wantLower.substr(0, dot);
            std::string matched;
            for (size_t i = 0; i < texPair.records().size(); ++i) {
                std::string haveLower = lower(texPair.records()[i].name);
                if (haveLower == wantLower || haveLower.find(wantLower) != std::string::npos ||
                    wantLower.find(haveLower) != std::string::npos) {
                    matched = texPair.records()[i].name;
                    auto cacheIt = textureSrvCache.find(matched);
                    if (cacheIt != textureSrvCache.end()) {
                        rangeSrv[rr.rangeIdx] = cacheIt->second;
                    } else {
                        sr3render::UploadedTexture ut;
                        std::string uerr;
                        if (sr3render::uploadTexture(dev.device, texPair, i, sr3texture::ByteView(gpegBytes.data(), gpegBytes.size()),
                                                     ut, uerr)) {
                            textureSrvCache[matched] = ut.srv;
                            rangeSrv[rr.rangeIdx] = ut.srv;
                            if (loggedTextureDecisions.insert(logKey).second)
                                printf("[texture] shader='%s' materialId=%u kind=%s -> REAL: matched '%s', uploaded record '%s'\n",
                                       rr.vsFileName.c_str(), rr.materialId, cs.textureKind.c_str(), wantName->c_str(),
                                       matched.c_str());
                        } else {
                            rangeSrv[rr.rangeIdx] = placeholderSrv;
                            if (loggedTextureDecisions.insert(logKey).second)
                                printf("[texture] shader='%s' materialId=%u -> matched record '%s' but uploadTexture() "
                                       "failed: %s - using placeholder\n", rr.vsFileName.c_str(), rr.materialId,
                                       matched.c_str(), uerr.c_str());
                        }
                    }
                    break;
                }
            }
            if (matched.empty()) {
                rangeSrv[rr.rangeIdx] = placeholderSrv;
                if (loggedTextureDecisions.insert(logKey).second)
                    printf("[texture] shader='%s' materialId=%u kind=%s -> PLACEHOLDER: real name '%s' did not match any "
                           "real record in car_4dr_genki_0.cpeg_pc\n", rr.vsFileName.c_str(), rr.materialId,
                           cs.textureKind.c_str(), wantName->c_str());
            }
        } else {
            rangeSrv[rr.rangeIdx] = placeholderSrv;
            if (loggedTextureDecisions.insert(logKey).second)
                printf("[texture] shader='%s' materialId=%u kind=%s -> PLACEHOLDER: no real %s binding found on this "
                       "material (or no cpeg pack)\n", rr.vsFileName.c_str(), rr.materialId, cs.textureKind.c_str(),
                       cs.textureKind.c_str());
        }
    }

    // Depth/raster state (same as single-shader prototype).
    D3D11_TEXTURE2D_DESC depthDesc{}; depthDesc.Width = 800; depthDesc.Height = 600; depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1; depthDesc.Format = DXGI_FORMAT_D32_FLOAT; depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT; depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ID3D11Texture2D* depthTex = nullptr;
    dev.device->CreateTexture2D(&depthDesc, nullptr, &depthTex);
    ID3D11DepthStencilView* depthView = nullptr;
    dev.device->CreateDepthStencilView(depthTex, nullptr, &depthView);
    D3D11_DEPTH_STENCIL_DESC dsDesc{}; dsDesc.DepthEnable = TRUE; dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS;
    ID3D11DepthStencilState* depthState = nullptr;
    dev.device->CreateDepthStencilState(&dsDesc, &depthState);
    D3D11_RASTERIZER_DESC rsDesc{}; rsDesc.FillMode = D3D11_FILL_SOLID; rsDesc.CullMode = D3D11_CULL_NONE;
    rsDesc.DepthClipEnable = TRUE;
    ID3D11RasterizerState* rasterState = nullptr;
    dev.device->CreateRasterizerState(&rsDesc, &rasterState);

    // -----------------------------------------------------------------
    // 11. Draw loop: one DrawIndexed per resolved range, each with ITS
    //     OWN real shader/input layout/cbuffers/texture (STEP 6).
    // -----------------------------------------------------------------
    dev.bindWithDepth(depthView);
    dev.clear(0.05f, 0.05f, 0.08f, 1.0f);
    dev.context->ClearDepthStencilView(depthView, D3D11_CLEAR_DEPTH, 1.0f, 0);
    dev.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dev.context->OMSetDepthStencilState(depthState, 0);
    dev.context->RSSetState(rasterState);

    size_t drawCallCount = 0, totalTriangles = 0;
    std::map<std::string, size_t> trianglesByShader;
    for (const auto& rr : resolutions) {
        if (!rr.ok) continue;
        auto cit = channels.find(static_cast<int>(rr.submeshIndex));
        if (cit == channels.end() || !cit->second.vb || !cit->second.ib) continue;
        auto spanIt = cit->second.rangeIndexSpan.find(rr.rangeIdx);
        if (spanIt == cit->second.rangeIndexSpan.end() || spanIt->second.second == 0) continue;
        const CompiledShader& cs = shaders[rr.vsFileName];
        if (!cs.ok || !cs.vs || !cs.ps || !cs.inputLayout) continue;

        UINT stride = sizeof(GpuVertexReal), offset = 0;
        dev.context->IASetInputLayout(cs.inputLayout);
        dev.context->IASetVertexBuffers(0, 1, &cit->second.vb, &stride, &offset);
        dev.context->IASetIndexBuffer(cit->second.ib, DXGI_FORMAT_R32_UINT, 0);
        dev.context->VSSetShader(cs.vs, nullptr, 0);
        dev.context->VSSetConstantBuffers(0, 1, &cs.cbVs);
        dev.context->PSSetShader(cs.ps, nullptr, 0);
        // NEW in this fork: prefer this range's own real per-material PS
        // cbuffer (step 8b) when one exists - falls back to the shared
        // flat-8.0 template otherwise, unchanged for every non-paint shader.
        {
            auto matIt = cs.perMaterialCbPs.find(rr.materialId);
            ID3D11Buffer* psCb = (matIt != cs.perMaterialCbPs.end()) ? matIt->second : cs.cbPs;
            dev.context->PSSetConstantBuffers(0, 1, &psCb);
        }
        if (cs.stem == "ir_sr3carpaint_gr") {
            // NEW in this fork: bind EVERY real texture/sampler slot this
            // pass-2 shader reflects to the neutral WHITE placeholder (see
            // its own declaration comment) instead of leaving all but one
            // unbound - confirmed by direct HLSL trace to be necessary for
            // the real, correctly-fed Base_Paint_Color to reach the output
            // at all (an unbound IR_LBufferSampler multiplies the whole
            // diffuse-paint term to exactly zero in this shader's own real
            // composite math). This is still a placeholder (no real
            // lighting/environment/G-buffer content), just not a hard zero.
            for (UINT slot : cs.allTexBindPoints) dev.context->PSSetShaderResources(slot, 1, &whitePlaceholderSrv);
            for (UINT slot : cs.allSamplerBindPoints) dev.context->PSSetSamplers(slot, 1, &sampler);
        } else if (cs.texBindPoint >= 0) {
            ID3D11ShaderResourceView* srv = rangeSrv.count(rr.rangeIdx) ? rangeSrv[rr.rangeIdx] : placeholderSrv;
            UINT texSlot = static_cast<UINT>(cs.texBindPoint);
            dev.context->PSSetShaderResources(texSlot, 1, &srv);
            if (cs.samplerBindPoint >= 0) {
                UINT sampSlot = static_cast<UINT>(cs.samplerBindPoint);
                dev.context->PSSetSamplers(sampSlot, 1, &sampler);
            }
        }

        UINT startIndex = static_cast<UINT>(spanIt->second.first);
        UINT count = static_cast<UINT>(spanIt->second.second);
        dev.context->DrawIndexed(count, startIndex, 0);
        ++drawCallCount;
        totalTriangles += count / 3;
        trianglesByShader[rr.vsFileName] += count / 3;
    }
    printf("\n[draw] issued %zu real DrawIndexed calls, %zu triangles total, across %zu real distinct shaders:\n",
           drawCallCount, totalTriangles, shaders.size());
    for (const auto& kv : trianglesByShader)
        printf("  '%s' (stem='%s'): %zu triangles\n", kv.first.c_str(), shaders[kv.first].stem.c_str(), kv.second);

    // -----------------------------------------------------------------
    // 12. Debug-layer messages.
    // -----------------------------------------------------------------
    printf("\n=== D3D11 debug-layer messages ===\n");
    if (!dev.debugLayerActive) {
        printf("(debug layer was NOT active in this run - see the [device] note above)\n");
    } else if (dev.infoQueue) {
        UINT64 n = dev.infoQueue->GetNumStoredMessages();
        printf("stored messages: %llu\n", static_cast<unsigned long long>(n));
        for (UINT64 i = 0; i < n; ++i) {
            SIZE_T len = 0;
            dev.infoQueue->GetMessage(i, nullptr, &len);
            std::vector<uint8_t> buf(len);
            D3D11_MESSAGE* msg = reinterpret_cast<D3D11_MESSAGE*>(buf.data());
            dev.infoQueue->GetMessage(i, msg, &len);
            printf("  [%d] %.*s\n", static_cast<int>(msg->Severity), static_cast<int>(msg->DescriptionByteLength), msg->pDescription);
        }
    }

    // -----------------------------------------------------------------
    // 13. Read back + PNG + real pixel statistics.
    // -----------------------------------------------------------------
    std::vector<uint8_t> rgba;
    std::string rbErr;
    if (!dev.readBack(rgba, rbErr)) { printf("FATAL: readBack failed: %s\n", rbErr.c_str()); return 1; }

    uint64_t sum[4] = {0, 0, 0, 0};
    uint8_t minV[4] = {255, 255, 255, 255}, maxV[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < rgba.size(); i += 4) {
        for (int c = 0; c < 4; ++c) {
            uint8_t v = rgba[i + c];
            sum[c] += v;
            if (v < minV[c]) minV[c] = v;
            if (v > maxV[c]) maxV[c] = v;
        }
    }
    size_t pixelCount = rgba.size() / 4;
    printf("\n=== Real RGBA8 pixel statistics (800x600, %zu pixels) ===\n", pixelCount);
    const char* chan[4] = {"R", "G", "B", "A"};
    for (int c = 0; c < 4; ++c)
        printf("  %s: min=%u max=%u mean=%.3f\n", chan[c], minV[c], maxV[c],
               pixelCount ? static_cast<double>(sum[c]) / static_cast<double>(pixelCount) : 0.0);

    std::string pngErr;
    const std::string outPath = "D:/Project Crreish/TEAM B/tools/prototype_real_shader_draw_multishader_paintcompose_output.png";
    if (!sr3render::writePng(outPath, 800, 600, rgba, pngErr)) {
        printf("FATAL: writePng failed: %s\n", pngErr.c_str());
        return 1;
    }
    printf("\n[png] wrote %s (NEW file - both prior prototypes' own output PNGs are left untouched)\n", outPath.c_str());

    printf("\n=== DONE ===\n");
    return 0;
}
