#pragma once

// Shared helpers of tools/prototype_real_shader_draw_multishader_paintcompose.cpp, split out so that
// file stays under ~1,500 lines.  Everything here used to sit in main's file-local anonymous namespace;
// the free functions are now defined in prototype_real_shader_draw_multishader_paintcompose_support.cpp
// (namespace paintcompose), the types are unchanged.

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

namespace paintcompose {

// ===========================================================================
// Small generic helpers (identical to prototype_real_shader_draw.cpp's own)
// ===========================================================================
std::string hrToString(HRESULT hr);
template <typename T>
void safeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

std::vector<uint8_t> readFile(const std::string& p);
bool findEntry(const vpp::Container& c, const std::string& name, std::vector<uint8_t>& out);
std::string lower(std::string s);
bool containsCI(const std::string& haystack, const std::string& needleLower);


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
struct VsInputField { uint16_t reg; uint8_t usage; uint8_t usageIndex; };
std::vector<VsInputField> extractVsInputSignature(const sr3d3d9bc::DisassembledShader& d);
std::string signatureToString(const std::vector<VsInputField>& sig);


// ===========================================================================
// Matrix packing - identical to prototype_real_shader_draw.cpp's own (see
// that file's comment for the row-vector/M*v-register derivation).
// ===========================================================================
void packColumn(const float M[16], int col, float out4[4]);
void transformPointRowVector(const float p[3], const float M[16], float out[3]);
void computeOrbitViewMatrix(const float boundsMin[3], const float boundsMax[3], float yaw, float pitch,
                             float distanceScale, float outView[16]);

struct ConstantFillLog { std::string constName; std::string decision; };
void fillFloat4Registers(std::vector<uint8_t>& cpuBuffer, uint16_t registerIndex, uint16_t registerCount,
                          const std::vector<std::array<float, 4>>& regs);


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

inline const D3D11_INPUT_ELEMENT_DESC kFullLayout[5] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, normal), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, tangent), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, blendIndices), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexReal, texcoord0), D3D11_INPUT_PER_VERTEX_DATA, 0},
};

void collectFxoStems(const vpp::Container& c, std::map<std::string, std::vector<std::string>>& stemToFiles);
std::string pickVertexFile(const std::vector<std::string>& files, std::string& reasonIfNone);

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
                                              uint16_t materialCount);
MaterialShaderHashResult extractMaterialShaderHashes(vpp::ByteView content, size_t subheaderOffset,
                                                       size_t meshSubBlockOffset, uint32_t meshCLength,
                                                       uint16_t materialCount);

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
BlobPairResult pairVsPs(const std::vector<uint8_t>& fxoBytes);
BlobPairResult pairVsPsForPass(const std::vector<uint8_t>& fxoBytes, size_t wantPassIndex);

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

}  // namespace paintcompose
