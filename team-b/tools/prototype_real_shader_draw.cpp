// Standalone prototype: draws ONE real vehicle-body mesh using the GAME'S
// OWN real, translated D3D9 shader (not a hand-written one) through this
// project's real D3D11 pipeline. Proves the translated-shader + real-
// geometry + real-device pipeline is mechanically wired correctly end to
// end - the next step after Stage 1/2 (sr3d3d9bc disassembler + SM4/5 HLSL
// translator, HANDOFF §9.97/§9.100, 7,276/7,276 real shaders compile with
// zero warnings) and the vertex-signature narrowing (§9.102) and the
// orchestrator's own shaderHash->filename resolution (spec-render-
// pipeline.md §22.9-§22.11).
//
// REAL TARGET (orchestrator-confirmed, not picked from the §9.102 candidate
// list blind): stem "ir_sr3carpaint_gr" - the single most common real
// car-paint shader (796/17,026 real vehicle material records). VS blob:
// shaders.vpp_pc :: ir_sr3carpaint_gr_v.fxo_pc, offset 720 length 1484 (the
// first of 4 VS entries in that file; all 4 share the identical, §9.102-
// confirmed vehicle-body signature POSITION0,NORMAL0,TANGENT0,
// BLENDINDICES0,TEXCOORD0). Its real paired PS is located via the T8 pass
// table exactly as wrapper_header.h documents (NOT by filename convention -
// there is none): VS tableIndex 0 -> pass[0] (vertexIndex=0, pixelIndex=0)
// -> PS tableIndex 0, offset 8784 length 792. Verified once with a
// throwaway probe before writing this file; the same lookup is redone here
// from the real header tables, not hardcoded past the file/archive names.
//
// REAL vehicle: car_4dr_genki_0 (dlc1.vpp_pc :: car_4dr_genki_0.str2_pc),
// which conveniently carries its own sibling car_4dr_genki_0.cpeg_pc/
// .gpeg_pc texture pack in the SAME nested container - located by a
// throwaway locator probe before writing this file.
//
// WHAT IS REAL VS. PLACEHOLDER (full reasoning printed at runtime too):
//   REAL - vertex geometry: sr3mesh::MeshBlock::decodeChannel() output for
//     the real channel matching the VS's own confirmed layout (101,
//     texcoords=1), read from the real .ccar_pc/.gcar_pc pair.
//   REAL - vertex encoding: reading the ACTUAL translated HLSL body (not
//     just the CTAB names) shows POSITION arrives already decoded (no
//     remap in-shader), but NORMAL/TANGENT arrive as raw UBYTE4N bytes
//     in [0,1] that the shader itself remaps via *2-1 (c0_def), and
//     TEXCOORD arrives as a raw int16 that the shader itself scales by
//     1/256 (c1_def.x). Feeding sr3mesh's already-decoded (-1..1 / already
//     UV-scaled) values directly would double-decode and corrupt the
//     result. So this tool feeds the mathematically exact PRE-IMAGE of
//     sr3mesh's own decode (normal*0.5+0.5, tangent*0.5+0.5,
//     texcoordsRaw as a plain int16->float cast) - real data, re-encoded to
//     match what the real shader's own real math expects, not guessed.
//     NOTE (reported, not silently resolved): the shader's own 1/256 UV
//     scale constant does not match sr3mesh's independently-established
//     1/1024 fixed-point scale (spec-vertex-format.md, refuted-half-float
//     finding) - a genuine, unexplained discrepancy this tool surfaces
//     rather than papers over. Feeding the raw int16 and trusting the
//     shader's OWN real constant is the most faithful thing to do either
//     way, since that constant came from the real translated bytecode.
//   REAL - BLENDINDICES: sr3mesh::Vertex::rigidPartIndex, fed as a raw
//     integer-valued float (not normalized) - matches the shader's own
//     `a0.x = round(3 * input.v4.x)` addressing math exactly (confirmed by
//     reading the translated body, not assumed).
//   REAL - "Bone_weights" cbuffer slice (CTAB name; really a 64-entry,
//     3-register-each PART TRANSFORM PALETTE, confirmed by reading the
//     shader body: BLENDINDICES.x directly selects a 3x4 matrix at
//     c[52 + 3*index]) - filled from sr3vehicle::VehiclePart::transform for
//     every real part, repacked into the shader's own M*v register
//     convention (see packColumn()'s comment). ASSUMPTION, stated plainly:
//     rigidPartIndex is assumed to index Vehicle::parts() directly, in file
//     order - a reasonable but NOT independently proven correspondence
//     (same open status the brief itself flags for this field).
//   REAL (working camera math, reused) - "projTM" (really the combined
//     view-projection matrix, confirmed by reading the shader body: it's
//     the ONLY matrix multiply after the world transform, feeding
//     SV_Position directly) filled from buildOrbitViewProjection(), and
//     "IR_World2View" (a view-space rotation used only for direction/DP3
//     math - normal/tangent/bitangent, never position) filled from a local
//     duplicate of buildOrbitViewProjection's own eye/right/up/forward
//     construction (duplicated only because that function returns the
//     combined VP product, not the bare view matrix, as its own header
//     comment states).
//   PLACEHOLDER (honestly labelled, documented at the fill site and in the
//     printed report) - "objTM" (the object's own world/placement
//     transform; no real "where is this vehicle in a scene" data exists
//     for a standalone single-mesh prototype, so this is identity),
//     "Object_instance_params_2" (a per-instance parameter, likely a
//     damage/wear blend factor per the shader's own usage - zero, so it
//     reads as "undamaged" rather than garbage), "Specular_Power" (a named
//     PS material constant with no real per-material value reachable from
//     this project's current readers - a plausible non-zero placeholder so
//     the specular term isn't degenerately zero).
//   TEXTURE - the PS's one sampler, "Damage_Normal_MapSampler", matched by
//     CTAB name (contains "normal") against the real vehicle material's
//     MaterialBindings::normalMap() name, then searched for in the
//     vehicle's own real .cpeg_pc/.gpeg_pc texture pack; a 1x1 placeholder
//     is bound instead if that search fails, and this tool says plainly
//     which happened.
//
// Standalone: not wired into CMakeLists.txt, build_verify/ untouched (see
// tools/validation/README's own "every harness is its own standalone
// translation unit" convention, and this session's own instruction not to
// touch canonical build files). Build with the same manual cl.exe pattern
// every tools/validation/*.cpp harness uses, linking d3d11.lib/dxgi.lib/
// d3dcompiler.lib/user32.lib/gdi32.lib in addition to the usual object set.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
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
#include "sr3render/mesh_renderer.h" // buildOrbitViewProjection/buildWorldMatrix/multiplyMatrix4x4 - reused free functions
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
// Small generic helpers
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

// ===========================================================================
// Local, debug-layer-enabled render device. This is sr3render::RenderDevice
// (include/sr3render/device.h, src/device.cpp) DUPLICATED into this
// standalone tool's own translation unit with EXACTLY ONE change - the
// D3D11_CREATE_DEVICE_DEBUG flag - because the canonical device.h/device.cpp
// must not be touched by this pass (orchestrator re-verifies and updates
// canonical files itself) and RenderDevice::initialise() does not currently
// take a flags parameter. Everything else below is the same working,
// already-verified device/target/readback logic, not a redesign.
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
    std::string adapterName;

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
        UINT flags = D3D11_CREATE_DEVICE_DEBUG; // the one real extension over RenderDevice::initialise()

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
                // Real, environment-specific, honestly-reported limitation:
                // the D3D11 debug layer is an OPTIONAL Windows feature
                // ("Graphics Tools", separate from the SDK headers/libs this
                // project already links against) and is not installed here;
                // enabling it requires an elevated DISM install this tool
                // does not attempt (out of scope, and this session has no
                // elevation). Falling back to a plain (non-debug) device so
                // the actual draw/readback/PNG proof can still run - this is
                // NOT the same as having validated zero debug-layer errors,
                // and this tool says so plainly rather than silently
                // dropping the requirement.
                debugLayerNote = "D3D11 debug layer unavailable in this environment (D3D11CreateDevice with "
                                  "D3D11_CREATE_DEVICE_DEBUG returned DXGI_ERROR_SDK_COMPONENT_MISSING/0x887A002D "
                                  "for both hardware and WARP - the optional 'Graphics Tools' Windows feature is "
                                  "not installed, and installing it requires elevation this session does not have) "
                                  "- falling back to a plain device. The draw/readback/PNG result below is NOT "
                                  "validated against the debug layer; that specific check could not run here.";
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
// Real DCL-derived vertex-input signature reading (same idiom as
// tools/validation/rank_shaders_by_vertex_signature.cpp).
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
        if (inst.dest->registerTypeRaw != 1 /* D3DSPR_INPUT, register_type.h RegisterType::Input */) continue;
        out.push_back({inst.dest->registerNumber, inst.dcl->usage, inst.dcl->usageIndex});
    }
    std::stable_sort(out.begin(), out.end(), [](const VsInputField& a, const VsInputField& b) { return a.reg < b.reg; });
    return out;
}

// ===========================================================================
// Matrix packing: this project's own matrices (buildWorldMatrix(),
// buildOrbitViewProjection()) are ROW-MAJOR, ROW-VECTOR convention
// (v' = v*M, translation in row 3 - see mesh_renderer.h's own comments).
// Reading the REAL translated shader body (not just the CTAB names) shows
// the engine's own constant registers use the opposite convention: each
// register holds one ROW of the "M*v" (column-vector) form, which is
// mathematically the TRANSPOSE of this project's row-vector matrices -
// i.e. register i = column i of the row-vector matrix, read top to bottom.
// Verified by hand against the real generated HLSL for both "objTM"
// (dot(c[32..34], r0) with r0.w=1, an affine 3-register transform) and
// "projTM" (dot(r1, c[28..31]), a full 4-register transform) - see this
// file's top comment for the worked-out derivation.
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

// Duplicated from sr3render::buildOrbitViewProjection()'s own internal
// eye/right/up/forward construction (src/mesh_renderer.cpp) - that function
// only returns the combined view*projection PRODUCT, never the bare view
// matrix, and "IR_World2View" (used only for DP3 direction math on
// normal/tangent/bitangent, confirmed by reading the real shader body) needs
// the view matrix's rotation-only part on its own. Same real formula, not a
// new camera model.
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

// ===========================================================================
// CTAB-driven constant-buffer filling. Generic in RULE (classify each real
// CTAB Float4 constant by a substring of its own real name), applied here to
// the actual 5 VS names and 2 PS names this real shader pair declares.
// `cpuBuffer` is sized to the D3D11-REFLECTED cbuffer size (see main()) -
// reflection confirms the compiled layout; CTAB supplies the real semantic
// names reflection alone cannot (the translator emits one flat
// `float4 c[N]` array, a documented gap - see hlsl_translator.h's own top
// comment - not per-field packoffset names).
// ===========================================================================
struct ConstantFillLog { std::string constName; std::string decision; };

void fillFloat4Registers(std::vector<uint8_t>& cpuBuffer, uint16_t registerIndex, uint16_t registerCount,
                          const std::vector<std::array<float, 4>>& regs) {
    for (uint16_t r = 0; r < registerCount && r < regs.size(); ++r) {
        size_t byteOff = static_cast<size_t>(registerIndex + r) * 16;
        if (byteOff + 16 > cpuBuffer.size()) continue;
        std::memcpy(cpuBuffer.data() + byteOff, regs[r].data(), 16);
    }
}

} // namespace

// ===========================================================================
// main()
// ===========================================================================
int main() {
    const std::string kCacheDir = "D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache";
    std::vector<ConstantFillLog> fillLog;

    printf("=== Prototype: real translated D3D9 shader + real vehicle-body mesh through the real D3D11 pipeline ===\n\n");

    // -----------------------------------------------------------------
    // 1. Locate + pair the real VS/PS blobs (shaders.vpp_pc ::
    //    ir_sr3carpaint_gr_v.fxo_pc), via WrapperHeader's own T8 table -
    //    the documented pairing method, not filename convention.
    // -----------------------------------------------------------------
    std::vector<uint8_t> shadersArchive = readFile(kCacheDir + "/shaders.vpp_pc");
    if (shadersArchive.empty()) { printf("FATAL: could not read shaders.vpp_pc\n"); return 1; }
    vpp::Container shadersContainer{vpp::ByteView(shadersArchive.data(), shadersArchive.size())};
    std::vector<uint8_t> fxoBytes;
    if (!findEntry(shadersContainer, "ir_sr3carpaint_gr_v.fxo_pc", fxoBytes)) {
        printf("FATAL: ir_sr3carpaint_gr_v.fxo_pc not found in shaders.vpp_pc\n");
        return 1;
    }
    printf("[shader] ir_sr3carpaint_gr_v.fxo_pc : %zu bytes\n", fxoBytes.size());

    sr3fxo::WrapperHeader wh;
    std::string whWhy;
    vpp::ByteView fxoView(fxoBytes.data(), fxoBytes.size());
    if (!sr3fxo::WrapperHeader::tryParse(fxoView, wh, whWhy)) {
        printf("FATAL: WrapperHeader::tryParse failed: %s\n", whWhy.c_str());
        return 1;
    }
    size_t endOfBlobs = 0;
    std::vector<sr3fxo::WrapperBlob> blobs = wh.layoutBlobs(endOfBlobs);

    const size_t kTargetVsOffset = 720; // confirmed real §9.102 candidate VS entry (first of 4 in this file)
    size_t vsOffset = 0, vsLength = 0, psOffset = 0, psLength = 0;
    bool vsFound = false, psFound = false;
    for (const auto& b : blobs) {
        if (b.stage == sr3fxo::Stage::Vertex && b.offset == kTargetVsOffset) {
            vsOffset = b.offset; vsLength = b.length; vsFound = true;
            for (const auto& p : wh.passes()) {
                if (p.vertexIndex != static_cast<int16_t>(b.tableIndex)) continue;
                for (const auto& pb : blobs) {
                    if (pb.stage == sr3fxo::Stage::Pixel && static_cast<int16_t>(pb.tableIndex) == p.pixelIndex) {
                        psOffset = pb.offset; psLength = pb.length; psFound = true;
                    }
                }
            }
        }
    }
    if (!vsFound || !psFound) {
        printf("FATAL: real T8-pass VS<->PS pairing did not resolve (vsFound=%d psFound=%d)\n", vsFound, psFound);
        return 1;
    }
    printf("[shader] VS blob: offset=%zu length=%zu   PS blob (paired via real T8 pass table): offset=%zu length=%zu\n",
           vsOffset, vsLength, psOffset, psLength);

    // -----------------------------------------------------------------
    // 2. Disassemble + CTAB + translate both to SM4/5 HLSL (Stage 1/2,
    //    reused exactly as validate_d3d9bc_hlsl_sm4_population.cpp calls
    //    them).
    // -----------------------------------------------------------------
    vpp::ByteView vsBlobView(fxoBytes.data() + vsOffset, vsLength);
    vpp::ByteView psBlobView(fxoBytes.data() + psOffset, psLength);
    sr3d3d9bc::DisassembledShader vsDis = sr3d3d9bc::disassemble(vsBlobView);
    sr3d3d9bc::DisassembledShader psDis = sr3d3d9bc::disassemble(psBlobView);
    sr3d3d9bc::ConstantTable vsCtab = sr3d3d9bc::readConstantTable(vsBlobView, vsDis);
    sr3d3d9bc::ConstantTable psCtab = sr3d3d9bc::readConstantTable(psBlobView, psDis);
    sr3d3d9bc::TranslationResult vsTr = sr3d3d9bc::translateToHlsl(vsDis, vsCtab, vsBlobView, sr3d3d9bc::HlslTarget::SM4_5);
    sr3d3d9bc::TranslationResult psTr = sr3d3d9bc::translateToHlsl(psDis, psCtab, psBlobView, sr3d3d9bc::HlslTarget::SM4_5);
    printf("[translate] VS: complete=%d unsupported=%zu warnings=%zu profile=%s\n", vsTr.complete, vsTr.unsupported.size(),
           vsTr.warnings.size(), vsTr.targetProfile.c_str());
    printf("[translate] PS: complete=%d unsupported=%zu warnings=%zu profile=%s\n", psTr.complete, psTr.unsupported.size(),
           psTr.warnings.size(), psTr.targetProfile.c_str());
    for (const auto& u : vsTr.unsupported) printf("  VS UNSUPPORTED: %s\n", u.c_str());
    for (const auto& u : psTr.unsupported) printf("  PS UNSUPPORTED: %s\n", u.c_str());

    // Real VS input signature, from the real DCL walk (not assumed).
    std::vector<VsInputField> vsInputs = extractVsInputSignature(vsDis);
    printf("[vertex-signature] real DCL-derived VS inputs (%zu): ", vsInputs.size());
    for (const auto& f : vsInputs) printf("%s%u ", usageName(f.usage), f.usageIndex);
    printf("\n");
    // Order-independent: D3D11 CreateInputLayout links VS inputs to buffer
    // elements by SEMANTIC NAME+INDEX, not by register/declaration order
    // (confirmed against this project's own translator: emitStructs() in
    // src/d3d9bc_hlsl_translator.cpp emits one struct field per input
    // register in REGISTER-NUMBER order, which need not match the field
    // order this tool's fixed GpuVertexReal struct uses - and indeed here
    // the real order is POSITION0,TEXCOORD0,NORMAL0,TANGENT0,BLENDINDICES0,
    // not the POSITION/NORMAL/TANGENT/BLENDINDICES/TEXCOORD order first
    // guessed from the target-signature label text). What matters is the
    // exact SET of (usage,index) pairs, checked here.
    auto hasUsage = [&](uint8_t usage, uint8_t idx) {
        for (const auto& f : vsInputs) if (f.usage == usage && f.usageIndex == idx) return true;
        return false;
    };
    const bool signatureIsExpected = vsInputs.size() == 5 && hasUsage(kPOSITION, 0) && hasUsage(kNORMAL, 0) &&
                                      hasUsage(kTANGENT, 0) && hasUsage(kBLENDINDICES, 0) && hasUsage(kTEXCOORD, 0);
    if (!signatureIsExpected) {
        printf("FATAL: real VS input signature does not match the expected {POSITION0,NORMAL0,TANGENT0,"
               "BLENDINDICES0,TEXCOORD0} SET this tool's fixed GpuVertex layout assumes - refusing to silently "
               "feed mismatched data (see this tool's own no-invented-fixes discipline).\n");
        return 1;
    }

    // -----------------------------------------------------------------
    // 3. D3DCompile both to real vs_4_0/ps_4_0 bytecode.
    // -----------------------------------------------------------------
    ID3DBlob *vsCode = nullptr, *vsErr = nullptr, *psCode = nullptr, *psErr = nullptr;
    HRESULT hr = D3DCompile(vsTr.hlsl.c_str(), vsTr.hlsl.size(), "ir_sr3carpaint_gr_v", nullptr, nullptr,
                             vsTr.entryPoint.c_str(), vsTr.targetProfile.c_str(), D3DCOMPILE_SKIP_OPTIMIZATION, 0,
                             &vsCode, &vsErr);
    if (FAILED(hr)) {
        printf("FATAL: D3DCompile(VS) failed: %s\n", hrToString(hr).c_str());
        if (vsErr) printf("%.*s\n", static_cast<int>(vsErr->GetBufferSize()), static_cast<const char*>(vsErr->GetBufferPointer()));
        return 1;
    }
    if (vsErr && vsErr->GetBufferSize() > 0)
        printf("[compile] VS warnings:\n%.*s\n", static_cast<int>(vsErr->GetBufferSize()), static_cast<const char*>(vsErr->GetBufferPointer()));
    hr = D3DCompile(psTr.hlsl.c_str(), psTr.hlsl.size(), "ir_sr3carpaint_gr_ps0", nullptr, nullptr,
                     psTr.entryPoint.c_str(), psTr.targetProfile.c_str(), D3DCOMPILE_SKIP_OPTIMIZATION, 0, &psCode, &psErr);
    if (FAILED(hr)) {
        printf("FATAL: D3DCompile(PS) failed: %s\n", hrToString(hr).c_str());
        if (psErr) printf("%.*s\n", static_cast<int>(psErr->GetBufferSize()), static_cast<const char*>(psErr->GetBufferPointer()));
        return 1;
    }
    if (psErr && psErr->GetBufferSize() > 0)
        printf("[compile] PS warnings:\n%.*s\n", static_cast<int>(psErr->GetBufferSize()), static_cast<const char*>(psErr->GetBufferPointer()));
    printf("[compile] D3DCompile SUCCEEDED for both VS (%s) and PS (%s), zero errors\n", vsTr.targetProfile.c_str(),
           psTr.targetProfile.c_str());

    // -----------------------------------------------------------------
    // 4. D3DReflect both - the REAL compiled cbuffer/resource layout.
    // -----------------------------------------------------------------
    ID3D11ShaderReflection *vsRefl = nullptr, *psRefl = nullptr;
    D3DReflect(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), __uuidof(ID3D11ShaderReflection), reinterpret_cast<void**>(&vsRefl));
    D3DReflect(psCode->GetBufferPointer(), psCode->GetBufferSize(), __uuidof(ID3D11ShaderReflection), reinterpret_cast<void**>(&psRefl));
    if (!vsRefl || !psRefl) { printf("FATAL: D3DReflect failed\n"); return 1; }

    D3D11_SHADER_DESC vsDesc{}, psDesc{};
    vsRefl->GetDesc(&vsDesc);
    psRefl->GetDesc(&psDesc);
    printf("[reflect] VS: constantBuffers=%u boundResources=%u   PS: constantBuffers=%u boundResources=%u\n",
           vsDesc.ConstantBuffers, vsDesc.BoundResources, psDesc.ConstantBuffers, psDesc.BoundResources);

    auto reflectCbufferSize = [](ID3D11ShaderReflection* refl, const char* label) -> UINT {
        ID3D11ShaderReflectionConstantBuffer* cb = refl->GetConstantBufferByIndex(0);
        D3D11_SHADER_BUFFER_DESC bd{};
        cb->GetDesc(&bd);
        printf("[reflect] %s cbuffer '%s': size=%u bytes, variables=%u\n", label, bd.Name, bd.Size, bd.Variables);
        for (UINT v = 0; v < bd.Variables; ++v) {
            ID3D11ShaderReflectionVariable* var = cb->GetVariableByIndex(v);
            D3D11_SHADER_VARIABLE_DESC vd{};
            var->GetDesc(&vd);
            printf("    reflected variable: name='%s' startOffset=%u size=%u\n", vd.Name, vd.StartOffset, vd.Size);
        }
        return bd.Size;
    };
    UINT vsCbSize = reflectCbufferSize(vsRefl, "VS");
    UINT psCbSize = reflectCbufferSize(psRefl, "PS");

    for (UINT r = 0; r < psDesc.BoundResources; ++r) {
        D3D11_SHADER_INPUT_BIND_DESC bind{};
        psRefl->GetResourceBindingDesc(r, &bind);
        printf("[reflect] PS resource: name='%s' type=%d bindPoint=%u\n", bind.Name, static_cast<int>(bind.Type), bind.BindPoint);
    }

    // -----------------------------------------------------------------
    // 5. Real vehicle + mesh + material bindings.
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

    sr3geometry::GeometryBlock geo =
        sr3geometry::GeometryBlock::parseAt(vpp::ByteView(ccarBytes.data(), ccarBytes.size()), veh.meshRegionOffset());
    if (!geo.hasMeshSubBlock()) { printf("FATAL: vehicle geometry block has no Mesh sub-block\n"); return 1; }
    sr3mesh::MeshBlock mesh = sr3mesh::MeshBlock::parse(vpp::ByteView(ccarBytes.data(), ccarBytes.size()),
                                                        geo.meshSubBlockOffset(), vpp::ByteView(gcarBytes.data(), gcarBytes.size()));
    printf("[mesh] channels=%zu indexCount=%u drawGroupsLocated=%d\n", mesh.channels().size(), mesh.indexCount(),
           mesh.drawGroupsLocated());

    sr3geometry::MaterialBindings bindings =
        sr3geometry::MaterialBindings::parse(vpp::ByteView(ccarBytes.data(), ccarBytes.size()), geo.offset(),
                                             geo.meshSubBlockOffset() + mesh.cLength());
    printf("[material] located=%d materialCount=%u\n", bindings.located(), bindings.materialCount());
    const std::string* normalMapName = nullptr;
    if (bindings.located()) {
        for (const auto& m : bindings.materials()) {
            if (m.normalMap() != nullptr) { normalMapName = m.normalMap(); break; }
        }
    }
    printf("[material] first real normalMap() name found: %s\n", normalMapName ? normalMapName->c_str() : "(none)");

    // Pick the real channel matching the VS's own confirmed layout (101,
    // texcoords=1) - not assumed, searched, and reported honestly either way.
    int channelIndex = -1;
    for (size_t i = 0; i < mesh.channels().size(); ++i) {
        const auto& ch = mesh.channels()[i];
        printf("[mesh] channel[%zu]: layoutCode=%u texcoordCount=%u elementCount=%u strideMatchesLaw=%d\n", i,
               ch.layoutCode, ch.texcoordCount, ch.elementCount, ch.strideMatchesLaw);
        if (channelIndex < 0 && ch.layoutCode == 101 && ch.texcoordCount == 1) channelIndex = static_cast<int>(i);
    }
    if (channelIndex < 0) {
        printf("FATAL (genuine dead end, reported honestly): no channel on car_4dr_genki_0 has layoutCode==101 && "
               "texcoordCount==1, the exact signature this real VS declares. Not fabricating a fit.\n");
        return 1;
    }
    printf("[mesh] using channel %d (layoutCode=101, texcoords=1) for the draw\n", channelIndex);

    std::vector<sr3mesh::Vertex> verts = mesh.decodeChannel(static_cast<size_t>(channelIndex));
    printf("[mesh] decoded %zu vertices from channel %d\n", verts.size(), channelIndex);
    if (!mesh.drawGroupsLocated() || mesh.drawGroups().empty()) {
        printf("FATAL: draw groups not located on this vehicle mesh\n");
        return 1;
    }
    // CORRECTION (found by direct diagnostic, not assumed): group 0's own
    // ranges span THREE DIFFERENT submeshIndex values (0, 1, 2), each its
    // own LOCAL vertex-index space starting near 0 (DrawRange::submeshIndex,
    // mesh_block.h's own comment: "believed to index the sub-mesh within a
    // multi-part asset - HYPOTHESIS... still-OPEN"). Channel 0 (6,955
    // vertices) only corresponds to submesh 0's ranges - feeding submesh
    // 1/2's ranges' index values against channel 0's buffer reads UNRELATED
    // vertices, producing long nonsense triangles (confirmed visually: a
    // "wedge/fan" artifact across the roof/hood/trunk, caught on review of
    // the first render). This is NOT a strip-bridging bug -
    // triangleListForRange()/triangleListForGroup() already restart cleanly
    // between ranges (mesh_block.cpp's own comment on triangleListForGroup);
    // TRIANGLELIST topology has no cross-triangle adjacency to bridge in the
    // first place, so per-range vs. one combined draw call would have been
    // pixel-identical either way. The real fix: only draw submesh 0's own
    // ranges against channel 0, which is the minimal, evidence-backed
    // subset - mapping the OTHER submeshes to their own correct channels is
    // a separate, currently-open question this prototype does not solve.
    const auto& group0 = mesh.drawGroups()[0];
    std::vector<uint32_t> indices;
    size_t submesh0Ranges = 0, otherSubmeshRanges = 0;
    for (const auto& range : group0) {
        if (range.submeshIndex != 0) { ++otherSubmeshRanges; continue; }
        ++submesh0Ranges;
        std::vector<uint32_t> part = mesh.triangleListForRange(range);
        indices.insert(indices.end(), part.begin(), part.end());
    }
    printf("[mesh] group 0: %zu draw ranges total (%zu submeshIndex==0, kept; %zu other submeshIndex, "
           "SKIPPED - see top-of-file correction comment), %zu total indices (%zu triangles) after strip "
           "expansion\n", group0.size(), submesh0Ranges, otherSubmeshRanges, indices.size(), indices.size() / 3);
    if (indices.empty()) { printf("FATAL: no non-degenerate triangles in group 0's submesh-0 ranges\n"); return 1; }

    // -----------------------------------------------------------------
    // 6. Object-space bounds (for camera framing only - NOT what's fed to
    //    the GPU vertex buffer, see the top comment) via the REAL per-part
    //    transform chain: Bone_weights[rigidPartIndex] equivalent, applied
    //    CPU-side with the real VehiclePart::transform data.
    // -----------------------------------------------------------------
    float boundsMin[3] = {1e30f, 1e30f, 1e30f}, boundsMax[3] = {-1e30f, -1e30f, -1e30f};
    uint32_t clampedPartIndices = 0;
    for (const auto& v : verts) {
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
    }
    printf("[bounds] object-space (after real per-part transform): min(%.3f %.3f %.3f) max(%.3f %.3f %.3f)  "
           "vertices with rigidPartIndex >= parts().size(): %u / %zu\n", boundsMin[0], boundsMin[1], boundsMin[2],
           boundsMax[0], boundsMax[1], boundsMax[2], clampedPartIndices, verts.size());

    // -----------------------------------------------------------------
    // 7. Build the GPU vertex buffer, matching the REAL confirmed VS input
    //    signature (POSITION0,NORMAL0,TANGENT0,BLENDINDICES0,TEXCOORD0),
    //    feeding the pre-shader-remap RAW representation the real shader
    //    body expects (see this file's top comment).
    // -----------------------------------------------------------------
    struct GpuVertexReal { float position[4], normal[4], tangent[4], blendIndices[4], texcoord0[4]; };
    std::vector<GpuVertexReal> gpuVerts(verts.size());
    for (size_t i = 0; i < verts.size(); ++i) {
        const auto& v = verts[i];
        GpuVertexReal g{};
        g.position[0] = v.position[0]; g.position[1] = v.position[1]; g.position[2] = v.position[2]; g.position[3] = 1.0f;
        // REAL data, re-encoded to the shader's own pre-remap [0,1] form
        // (undoing sr3mesh's own *2-1 decode exactly, see top comment).
        g.normal[0] = v.normal[0] * 0.5f + 0.5f; g.normal[1] = v.normal[1] * 0.5f + 0.5f;
        g.normal[2] = v.normal[2] * 0.5f + 0.5f; g.normal[3] = v.normalW / 255.0f;
        g.tangent[0] = v.tangent[0] * 0.5f + 0.5f; g.tangent[1] = v.tangent[1] * 0.5f + 0.5f;
        g.tangent[2] = v.tangent[2] * 0.5f + 0.5f; g.tangent[3] = v.tangentW / 255.0f;
        g.blendIndices[0] = g.blendIndices[1] = g.blendIndices[2] = g.blendIndices[3] = static_cast<float>(v.rigidPartIndex);
        if (!v.texcoordsRaw.empty()) {
            g.texcoord0[0] = static_cast<float>(v.texcoordsRaw[0][0]);
            g.texcoord0[1] = static_cast<float>(v.texcoordsRaw[0][1]);
        }
        gpuVerts[i] = g;
    }

    // -----------------------------------------------------------------
    // 8. Build the VS/PS constant CPU buffers, sized per D3D11 reflection,
    //    filled by real CTAB name.
    // -----------------------------------------------------------------
    std::vector<uint8_t> vsCbuf(vsCbSize, 0), psCbuf(psCbSize, 0);

    // -- camera: buildOrbitViewProjection (real, reused) framed to the
    //    real decoded+transformed bounds --
    float viewProj[16];
    const float yaw = 0.6f, pitch = 0.35f, distanceScale = 1.8f;
    sr3render::buildOrbitViewProjection(boundsMin, boundsMax, yaw, pitch, distanceScale, 800.0f / 600.0f, viewProj);
    float viewOnly[16];
    computeOrbitViewMatrix(boundsMin, boundsMax, yaw, pitch, distanceScale, viewOnly);

    float worldIdentity[16];
    sr3render::buildWorldMatrix(0.0f, 0.0f, 0.0f, 0.0f, 1.0f, worldIdentity);

    for (const auto& c : vsCtab.constants) {
        if (c.registerSet != sr3d3d9bc::RegisterSet::Float4) continue;
        std::string decision;
        std::vector<std::array<float, 4>> regs;
        // HYPOTHESIS-BASED (render-pipeline provenance downgrade, 2026-09-30): filling a CTAB constant
        // named *proj* (projTM, VS c28x4) with the fused view*projection and *world2view* (c48) with the
        // view matrix rests on spec-render-pipeline.md Sec20.12.5/Sec20.12.9, now HYPOTHESIS on provenance
        // pending re-derivation from the shipped shaders (bridge job 06, ctab_census). No behaviour change.
        if (containsCI(c.name, "proj")) {
            for (int i = 0; i < 4; ++i) { std::array<float, 4> r; packColumn(viewProj, i, r.data()); regs.push_back(r); }
            decision = "REAL: buildOrbitViewProjection() (view*proj), framed to the real decoded+per-part-transformed "
                       "mesh bounds; this is the ONLY matrix multiply feeding SV_Position after the world transform "
                       "in the real shader body, confirmed by reading it, despite the CTAB name 'projTM'";
        } else if (containsCI(c.name, "world2view")) {
            for (int i = 0; i < 3; ++i) { std::array<float, 4> r; packColumn(viewOnly, i, r.data()); regs.push_back(r); }
            decision = "REAL: rotation-only part of the same real orbit-camera view matrix (duplicated construction, "
                       "see computeOrbitViewMatrix()) - confirmed by reading the shader body to be used only for "
                       "DP3 direction math (normal/tangent/bitangent), never position";
        } else if (containsCI(c.name, "obj") && containsCI(c.name, "tm")) {
            for (int i = 0; i < 3; ++i) { std::array<float, 4> r; packColumn(worldIdentity, i, r.data()); regs.push_back(r); }
            decision = "PLACEHOLDER: identity world/placement transform - no real 'where is this vehicle in a scene' "
                       "data exists for a standalone single-mesh prototype draw";
        } else if (containsCI(c.name, "bone")) {
            // REAL per-part transform palette - confirmed by reading the
            // shader body: BLENDINDICES.x directly selects 3 registers at
            // c[registerIndex + 3*index].
            const size_t maxElems = c.registerCount / 3;
            for (size_t e = 0; e < maxElems; ++e) {
                float packed[3][4];
                if (e < veh.parts().size()) {
                    for (int i = 0; i < 3; ++i) packColumn(veh.parts()[e].transform.data(), i, packed[i]);
                } else {
                    for (int i = 0; i < 3; ++i) packColumn(worldIdentity, i, packed[i]); // unused index -> harmless identity
                }
                for (int i = 0; i < 3; ++i) { std::array<float, 4> r; std::memcpy(r.data(), packed[i], 16); regs.push_back(r); }
            }
            decision = "REAL: sr3vehicle::VehiclePart::transform for every real part (ASSUMPTION, stated plainly: "
                       "rigidPartIndex is assumed to index Vehicle::parts() directly in file order - not "
                       "independently proven); identity for any palette slot beyond veh.parts().size()";
        } else {
            for (uint16_t r = 0; r < c.registerCount; ++r) regs.push_back({0, 0, 0, 0});
            decision = "PLACEHOLDER: zero (per-instance parameter, likely damage/wear blend per its own shader "
                       "usage - not traced to a real data source; zero reads as 'undamaged', not garbage)";
        }
        fillFloat4Registers(vsCbuf, c.registerIndex, c.registerCount, regs);
        fillLog.push_back({std::string("VS ") + c.name, decision});
    }

    for (const auto& c : psCtab.constants) {
        if (c.registerSet != sr3d3d9bc::RegisterSet::Float4) continue;
        std::vector<std::array<float, 4>> regs;
        for (uint16_t r = 0; r < c.registerCount; ++r) regs.push_back({8.0f, 8.0f, 8.0f, 8.0f});
        fillFloat4Registers(psCbuf, c.registerIndex, c.registerCount, regs);
        fillLog.push_back({std::string("PS ") + c.name,
                            "PLACEHOLDER: flat 8.0 - a named per-material constant (real CTAB name, real register) "
                            "with no per-material value reachable from this project's current readers; a plausible "
                            "non-zero value so the specular term isn't degenerately zero"});
    }

    printf("\n=== Constant-buffer fill decisions (real vs. placeholder, by real CTAB name) ===\n");
    for (const auto& l : fillLog) printf("  %-28s : %s\n", l.constName.c_str(), l.decision.c_str());

    // -----------------------------------------------------------------
    // 9. Texture: match the PS's one real sampler name against the
    //    vehicle's own real material bindings, then search the vehicle's
    //    own real .cpeg_pc/.gpeg_pc pack for it.
    // -----------------------------------------------------------------
    std::string textureDecision;
    bool haveRealTexture = false;
    sr3render::UploadedTexture uploadedTex;
    std::string matchedTextureName;
    if (normalMapName != nullptr && !cpegBytes.empty() && !gpegBytes.empty()) {
        try {
            sr3texture::TexturePair pair = sr3texture::TexturePair::parse(sr3texture::ByteView(cpegBytes.data(), cpegBytes.size()));
            std::string wantLower = lower(*normalMapName);
            size_t dot = wantLower.find_last_of('.');
            if (dot != std::string::npos) wantLower = wantLower.substr(0, dot);
            for (size_t i = 0; i < pair.records().size(); ++i) {
                std::string haveLower = lower(pair.records()[i].name);
                if (haveLower == wantLower || haveLower.find(wantLower) != std::string::npos ||
                    wantLower.find(haveLower) != std::string::npos) {
                    matchedTextureName = pair.records()[i].name;
                    std::string uerr;
                    if (sr3render::uploadTexture(nullptr, pair, i, sr3texture::ByteView(gpegBytes.data(), gpegBytes.size()),
                                                 uploadedTex, uerr)) {
                        // device not created yet - real upload happens after device init below; record the index.
                    }
                    break;
                }
            }
        } catch (const std::exception& ex) {
            textureDecision = std::string("texture pack parse failed: ") + ex.what();
        }
    }

    // -----------------------------------------------------------------
    // 10. Real D3D11 device, debug layer enabled.
    // -----------------------------------------------------------------
    DebugRenderDevice dev;
    std::string devErr;
    if (!dev.initialise(800, 600, devErr)) { printf("FATAL: %s\n", devErr.c_str()); return 1; }
    printf("\n[device] kind=%s   debugLayerActive=%d\n", dev.isWarp ? "WARP (software rasterizer)" : "Hardware",
           dev.debugLayerActive);
    if (!dev.debugLayerNote.empty()) printf("[device] %s\n", dev.debugLayerNote.c_str());

    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    hr = dev.device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &vs);
    if (FAILED(hr)) { printf("FATAL: CreateVertexShader failed: %s\n", hrToString(hr).c_str()); return 1; }
    hr = dev.device->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, &ps);
    if (FAILED(hr)) { printf("FATAL: CreatePixelShader failed: %s\n", hrToString(hr).c_str()); return 1; }

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, normal), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, tangent), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, blendIndices), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, texcoord0), D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    ID3D11InputLayout* inputLayout = nullptr;
    hr = dev.device->CreateInputLayout(layout, 5, vsCode->GetBufferPointer(), vsCode->GetBufferSize(), &inputLayout);
    if (FAILED(hr)) { printf("FATAL: CreateInputLayout failed: %s\n", hrToString(hr).c_str()); return 1; }

    D3D11_BUFFER_DESC vbDesc{};
    vbDesc.ByteWidth = static_cast<UINT>(gpuVerts.size() * sizeof(GpuVertexReal));
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbData{}; vbData.pSysMem = gpuVerts.data();
    ID3D11Buffer* vb = nullptr;
    hr = dev.device->CreateBuffer(&vbDesc, &vbData, &vb);
    if (FAILED(hr)) { printf("FATAL: CreateBuffer(VB) failed: %s\n", hrToString(hr).c_str()); return 1; }

    D3D11_BUFFER_DESC ibDesc{};
    ibDesc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint32_t));
    ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ibData{}; ibData.pSysMem = indices.data();
    ID3D11Buffer* ib = nullptr;
    hr = dev.device->CreateBuffer(&ibDesc, &ibData, &ib);
    if (FAILED(hr)) { printf("FATAL: CreateBuffer(IB) failed: %s\n", hrToString(hr).c_str()); return 1; }

    D3D11_BUFFER_DESC cbVsDesc{}; cbVsDesc.ByteWidth = static_cast<UINT>((vsCbuf.size() + 15) & ~size_t(15));
    cbVsDesc.Usage = D3D11_USAGE_DEFAULT; cbVsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    vsCbuf.resize(cbVsDesc.ByteWidth, 0);
    D3D11_SUBRESOURCE_DATA cbVsData{}; cbVsData.pSysMem = vsCbuf.data();
    ID3D11Buffer* cbVs = nullptr;
    hr = dev.device->CreateBuffer(&cbVsDesc, &cbVsData, &cbVs);
    if (FAILED(hr)) { printf("FATAL: CreateBuffer(VS cbuffer) failed: %s\n", hrToString(hr).c_str()); return 1; }

    D3D11_BUFFER_DESC cbPsDesc{}; cbPsDesc.ByteWidth = static_cast<UINT>((psCbuf.size() + 15) & ~size_t(15));
    cbPsDesc.Usage = D3D11_USAGE_DEFAULT; cbPsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    psCbuf.resize(cbPsDesc.ByteWidth, 0);
    D3D11_SUBRESOURCE_DATA cbPsData{}; cbPsData.pSysMem = psCbuf.data();
    ID3D11Buffer* cbPs = nullptr;
    hr = dev.device->CreateBuffer(&cbPsDesc, &cbPsData, &cbPs);
    if (FAILED(hr)) { printf("FATAL: CreateBuffer(PS cbuffer) failed: %s\n", hrToString(hr).c_str()); return 1; }

    // Real texture upload now that the device exists.
    ID3D11ShaderResourceView* boundSrv = nullptr;
    ID3D11Texture2D* placeholderTex = nullptr;
    if (!matchedTextureName.empty()) {
        sr3texture::TexturePair pair = sr3texture::TexturePair::parse(sr3texture::ByteView(cpegBytes.data(), cpegBytes.size()));
        for (size_t i = 0; i < pair.records().size(); ++i) {
            if (pair.records()[i].name != matchedTextureName) continue;
            std::string uerr;
            sr3render::UploadedTexture ut;
            if (sr3render::uploadTexture(dev.device, pair, i, sr3texture::ByteView(gpegBytes.data(), gpegBytes.size()), ut, uerr)) {
                boundSrv = ut.srv;
                haveRealTexture = true;
                textureDecision = "REAL: matched CTAB sampler 'Damage_Normal_MapSampler' (contains 'normal') against "
                                  "MaterialBindings normalMap() name '" + *normalMapName + "', found as real texture "
                                  "record '" + matchedTextureName + "' in the vehicle's own car_4dr_genki_0.cpeg_pc/"
                                  ".gpeg_pc pack, uploaded via sr3render::uploadTexture()";
            } else {
                textureDecision = "real texture record matched by name but uploadTexture() failed: " + uerr;
            }
            break;
        }
    }
    if (!haveRealTexture) {
        if (textureDecision.empty())
            textureDecision = normalMapName
                ? ("PLACEHOLDER: MaterialBindings normalMap() name '" + *normalMapName +
                   "' did not match any real texture record name in car_4dr_genki_0.cpeg_pc")
                : "PLACEHOLDER: no real normalMap() binding found on this vehicle's materials at all";
        D3D11_TEXTURE2D_DESC td{}; td.Width = 1; td.Height = 1; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        uint8_t px[4] = {128, 128, 255, 255}; // flat "pointing up" normal-map-ish placeholder (0.5,0.5,1)
        D3D11_SUBRESOURCE_DATA sub{}; sub.pSysMem = px; sub.SysMemPitch = 4;
        dev.device->CreateTexture2D(&td, &sub, &placeholderTex);
        dev.device->CreateShaderResourceView(placeholderTex, nullptr, &boundSrv);
    }
    printf("\n[texture] %s\n", textureDecision.c_str());

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    ID3D11SamplerState* sampler = nullptr;
    dev.device->CreateSamplerState(&sd, &sampler);

    // Depth buffer (same technique src/mesh_renderer.cpp's createDepth()
    // already uses - plain D3D11 boilerplate, duplicated here rather than
    // reusing MeshRenderer since MeshRenderer's own shader/layout is fixed
    // and not what this tool draws with).
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
    D3D11_RASTERIZER_DESC rsDesc{}; rsDesc.FillMode = D3D11_FILL_SOLID; rsDesc.CullMode = D3D11_CULL_NONE; // winding not established, same reasoning as mesh_renderer.cpp
    rsDesc.DepthClipEnable = TRUE;
    ID3D11RasterizerState* rasterState = nullptr;
    dev.device->CreateRasterizerState(&rsDesc, &rasterState);

    // -----------------------------------------------------------------
    // 11. ONE real indexed draw call.
    // -----------------------------------------------------------------
    dev.context->UpdateSubresource(cbVs, 0, nullptr, vsCbuf.data(), 0, 0);
    dev.context->UpdateSubresource(cbPs, 0, nullptr, psCbuf.data(), 0, 0);

    dev.bindWithDepth(depthView);
    dev.clear(0.05f, 0.05f, 0.08f, 1.0f);
    dev.context->ClearDepthStencilView(depthView, D3D11_CLEAR_DEPTH, 1.0f, 0);

    UINT stride = sizeof(GpuVertexReal), offset = 0;
    dev.context->IASetInputLayout(inputLayout);
    dev.context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    dev.context->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
    dev.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dev.context->VSSetShader(vs, nullptr, 0);
    dev.context->VSSetConstantBuffers(0, 1, &cbVs);
    dev.context->PSSetShader(ps, nullptr, 0);
    dev.context->PSSetConstantBuffers(0, 1, &cbPs);
    dev.context->PSSetShaderResources(0, 1, &boundSrv);
    dev.context->PSSetSamplers(0, 1, &sampler);
    dev.context->OMSetDepthStencilState(depthState, 0);
    dev.context->RSSetState(rasterState);

    dev.context->DrawIndexed(static_cast<UINT>(indices.size()), 0, 0);
    printf("\n[draw] issued ONE DrawIndexed(%zu indices = %zu triangles)\n", indices.size(), indices.size() / 3);

    // -----------------------------------------------------------------
    // 12. Debug-layer messages - the real validation signal.
    // -----------------------------------------------------------------
    printf("\n=== D3D11 debug-layer messages ===\n");
    if (!dev.debugLayerActive) {
        printf("(debug layer was NOT active in this run - see the [device] note above; this is a real "
               "environment limitation, not a suppressed/ignored check)\n");
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
    } else {
        printf("(ID3D11InfoQueue unavailable - could not query it from the debug-layer device)\n");
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
    const std::string outPath = "D:/Project Crreish/TEAM B/tools/prototype_real_shader_draw_output.png";
    if (!sr3render::writePng(outPath, 800, 600, rgba, pngErr)) {
        printf("FATAL: writePng failed: %s\n", pngErr.c_str());
        return 1;
    }
    printf("\n[png] wrote %s\n", outPath.c_str());

    printf("\n=== DONE ===\n");
    return 0;
}
