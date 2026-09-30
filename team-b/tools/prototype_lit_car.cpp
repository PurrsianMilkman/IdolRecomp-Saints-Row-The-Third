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

namespace {

// ===========================================================================
// Small generic helpers (identical to the paintcompose fork's own).
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
// Plain (non-swapchain) D3D11 device - same pattern as every other tool in
// this family; kept minimal since this tool renders entirely to its own
// off-screen targets, only using the "default" target/rtv/staging pair for
// the FINAL composited readback.
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

// One off-screen colour render target: texture + RTV + SRV, so it can be
// written by one pass and sampled by the next (the actual G-buffer/light-
// buffer mechanism this whole task is about).
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

// ===========================================================================
// Real DCL-derived vertex-input signature (identical to paintcompose's own).
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
// Matrix helpers. packColumn/transformPointRowVector/computeOrbitViewMatrix
// identical to paintcompose's own (row-major storage, row-vector v*M
// convention, registers uploaded column-by-column - see that file's comment
// for the derivation). buildPerspectiveLH/invert4x4 are NEW in this fork.
// ===========================================================================
void packColumn(const float M[16], int col, float out4[4]) {
    out4[0] = M[0 * 4 + col]; out4[1] = M[1 * 4 + col]; out4[2] = M[2 * 4 + col]; out4[3] = M[3 * 4 + col];
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
// Row-major, row-vector (v*M) LH perspective projection - CHOSEN camera
// parameters (fovY/near/far), NOT extracted from any real game data (no
// prototype in this family has ever had a real captured camera to draw
// from - the orbit framing itself has always been this tool family's own
// diagnostic-viewing choice, unchanged in kind by this fork). Needed
// because paintcompose's own buildOrbitViewProjection() only exposes an
// already-fused view*projection product, and IR_Light_Inv_Proj_TM needs a
// standalone, invertible PROJECTION matrix.
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
// General 4x4 inverse (public-domain cofactor-expansion method, the
// standard textbook algorithm - not project- or vendor-specific). Returns
// false (leaves `out` as the identity) if the matrix is singular, which a
// real perspective projection never is.
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

// GPU vertex layout for real car geometry (identical to paintcompose's own).
struct GpuVertexReal { float position[4], normal[4], tangent[4], blendIndices[4], texcoord0[4]; };
const D3D11_INPUT_ELEMENT_DESC kFullLayout[5] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, normal), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, tangent), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, blendIndices), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, texcoord0), D3D11_INPUT_PER_VERTEX_DATA, 0},
};
// GPU vertex layout for the light pass's own real fullscreen-quad VS
// (real DCL signature POSITION0+TEXCOORD0 only, confirmed by direct HLSL
// dump this pass - see this file's own top comment).
struct GpuVertexQuad { float position[4], texcoord0[4]; };
const D3D11_INPUT_ELEMENT_DESC kQuadLayout[2] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexQuad, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexQuad, texcoord0), D3D11_INPUT_PER_VERTEX_DATA, 0},
};

// ===========================================================================
// STEP 1/2: real shaderHash -> real stem -> real vertex-stage .fxo_pc file
// (identical to paintcompose's own - see that file's top comment).
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

struct MaterialShaderHashResult {
    std::vector<uint32_t> hashes;
    bool complete = false;
    std::string note;
};
struct MaterialConstant { uint32_t nameHash = 0; std::array<float, 4> value{}; };
struct MaterialArrayBCResult {
    std::vector<std::vector<MaterialConstant>> perMaterial;
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
    if (n2 > 0) { cursor = alignUp(cursor, 8); cursor += static_cast<size_t>(n2) * 8 + static_cast<size_t>(n2) * 4; }
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
    if (n2 > 0) { cursor = alignUp(cursor, 8); cursor += static_cast<size_t>(n2) * 8 + static_cast<size_t>(n2) * 4; }
    cursor = alignUp(cursor, 8);
    cursor += static_cast<size_t>(materialCount) * 8;
    for (uint16_t i = 0; i < materialCount; ++i) {
        if (cursor + 4 > content.size()) { result.note = "ran out of bytes at material " + std::to_string(i); return result; }
        uint32_t declaredSize = content.readU32LE(cursor);
        size_t headerStart = alignUp(cursor + 4, 8);
        if (headerStart + 0x30 > content.size()) { result.note = "header overrun at material " + std::to_string(i); return result; }
        result.hashes.push_back(content.readU32LE(headerStart + 0x00));
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
// STEP 3: real T8 VS<->PS pairing BY REAL ROLE NUMBER (NEW in this fork -
// paintcompose's own pairVsPsForPass() takes an already-known pass INDEX;
// this generalizes to "whatever pass index this file's own header ACTUALLY
// declares for role R," read directly via WrapperHeader::roleIndexByte(),
// so it is not hardcoded to ir_sr3carpaint_gr's own pass 0/pass 2 - see
// this file's own top comment: verified this session that all 6 real
// shaders in scope share role4->pass0/role6->pass2, but this function does
// not assume that, it reads each file's own real role table).
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

// ===========================================================================
// One fully resolved+compiled distinct shader PASS (either the role-4
// G-buffer-write variant or the role-6 forward-lit variant of one file).
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
    UINT psOutputCount = 0; // real number of SV_TargetN this PS writes (1 for lit pass, 3 for G-buffer pass)

    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* inputLayout = nullptr;
    ID3D11Buffer* cbVs = nullptr;
    ID3D11Buffer* cbPs = nullptr; // template (real-zero/0.1-floor per Sec20.12.11)
    std::vector<uint8_t> vsCbufCpu, psCbufCpu;
    std::map<uint32_t, ID3D11Buffer*> perMaterialCbPs; // real per-material override, keyed by materialId

    int lBufferSamplerReg = -1;    // s-register of IR_LBufferSampler, if this PS declares it (real T8/CTAB read)
    int normalsSamplerReg = -1;    // s-register this PS binds ITS OWN normals input at, if a light shader
    int lightingSamplerReg = -1;   // ditto for the lighting-parameter input, if a light shader
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

    // Subset check identical to paintcompose's own: any subset of
    // {POSITION0,NORMAL0,TANGENT0,BLENDINDICES0,TEXCOORD0} for car-body
    // shaders (kFullLayout), OR the real light-shader signature
    // {POSITION0,TEXCOORD0} (kQuadLayout) - both real, both checked, not
    // forced.
    auto hasUsage = [&](uint8_t usage, uint8_t idx) {
        for (const auto& f : cs.vsInputs) if (f.usage == usage && f.usageIndex == idx) return true;
        return false;
    };
    bool isQuadShape = cs.vsInputs.size() == 2 && hasUsage(kPOSITION, 0) && hasUsage(kTEXCOORD, 0);
    bool carSubset = hasUsage(kPOSITION, 0);
    for (const auto& f : cs.vsInputs) {
        bool known = (f.usage == kPOSITION && f.usageIndex == 0) || (f.usage == kNORMAL && f.usageIndex == 0) ||
                     (f.usage == kTANGENT && f.usageIndex == 0) || (f.usage == kBLENDINDICES && f.usageIndex == 0) ||
                     (f.usage == kTEXCOORD && f.usageIndex == 0);
        if (!known) carSubset = false;
    }
    if (!isQuadShape && !carSubset) {
        cs.failReason = "real VS input signature (" + signatureToString(cs.vsInputs) + ") is neither the known car "
                         "subset nor the known {POSITION0,TEXCOORD0} light-quad signature - not forced to fit";
        return cs;
    }

    cs.ok = true;
    return cs;
}

} // namespace

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
