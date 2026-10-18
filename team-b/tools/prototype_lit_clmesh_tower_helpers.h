#pragma once

// Shared helpers of tools/prototype_lit_clmesh_tower.cpp (moved verbatim from that file's former
// anonymous namespace into the named namespace tower_proto: struct definitions, constants and
// declarations here, function bodies in prototype_lit_clmesh_tower_helpers.cpp).

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

namespace tower_proto {

// ===========================================================================
// Small generic helpers (identical to prototype_lit_car.cpp's own).
// ===========================================================================
std::string hrToString(HRESULT hr);
template <typename T>
void safeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

std::vector<uint8_t> readFile(const std::string& p);
bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out);
bool findEntry(const vpp::Container& c, const std::string& name, std::vector<uint8_t>& out);
std::string lower(std::string s);
bool containsCI(const std::string& haystack, const std::string& needleLower);
bool endsWithStr(const std::string& s, const std::string& suffix);
bool endsWithNoCase(const std::string& s, const std::string& suffix);
size_t alignUp(size_t v, size_t a);

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

void printTargetStats(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* tex, uint32_t w, uint32_t h,
                       const char* label);

// ===========================================================================
// Real DCL-derived vertex-input signature (identical to prototype_lit_car
// .cpp's own).
// ===========================================================================
constexpr uint8_t kPOSITION = 0, kBLENDWEIGHT = 1, kBLENDINDICES = 2, kNORMAL = 3, kTEXCOORD = 5, kTANGENT = 6;
const char* usageName(uint8_t u);
struct VsInputField { uint16_t reg; uint8_t usage; uint8_t usageIndex; };
std::vector<VsInputField> extractVsInputSignature(const sr3d3d9bc::DisassembledShader& d);
std::string signatureToString(const std::vector<VsInputField>& sig);

void packColumn(const float M[16], int col, float out4[4]);
void computeOrbitViewMatrix(const float boundsMin[3], const float boundsMax[3], float yaw, float pitch,
                             float distanceScale, float outView[16]);
void buildPerspectiveLH(float fovYRadians, float aspect, float nearZ, float farZ, float out[16]);
bool invert4x4(const float m[16], float out[16]);

struct ConstantFillLog { std::string constName; std::string decision; };
void fillFloat4Registers(std::vector<uint8_t>& cpuBuffer, uint16_t registerIndex, uint16_t registerCount,
                          const std::vector<std::array<float, 4>>& regs);

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

void collectFxoStems(const vpp::Container& c, std::map<std::string, std::vector<std::string>>& stemToFiles);
std::string pickVertexFile(const std::vector<std::string>& files, std::string& reasonIfNone);

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
                               const std::string& stem, size_t role);

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
bool findAndUploadTexture(const vpp::Container& container, const std::string& tgaName,
                          ID3D11Device* device, sr3render::UploadedTexture& out,
                          std::string& foundIn, TextureSearchCache* cache = nullptr);

// MEASURED this session (2026-09-30), NOT an existing sr3geometry named
// constant: real specular-map role hash seen on airport_controltower's own
// real texture-binding dump (e.g. material[7] slot=2 'metal_suports_
// skyscraper_s.tga', material[11] slot=2 'ap_hangar02_a_door02_s.tga').
// kParamHashNormalMap/kParamHashDiffuseA/kParamHashDiffuseB already live in
// sr3geometry/material_binding.h; this one does not yet, so it stays local
// to this file rather than editing that shared header for one task-scoped
// finding.
constexpr uint32_t kParamHashSpecularClmesh = 0xE848C9CAu;

} // namespace tower_proto
