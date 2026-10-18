#pragma once

// Shared helpers of tools/prototype_lit_car.cpp, split out so that file stays under ~1,500 lines.
// Everything here used to sit in main's file-local anonymous namespace; the free functions are now
// defined in prototype_lit_car_support.cpp (namespace litcar), the types are unchanged.

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

namespace litcar {

// ===========================================================================
// Small generic helpers (identical to the paintcompose fork's own).
// ===========================================================================
std::string hrToString(HRESULT hr);
template <typename T>
void safeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

std::vector<uint8_t> readFile(const std::string& p);
bool findEntry(const vpp::Container& c, const std::string& name, std::vector<uint8_t>& out);
std::string lower(std::string s);
bool containsCI(const std::string& haystack, const std::string& needleLower);

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
struct VsInputField { uint16_t reg; uint8_t usage; uint8_t usageIndex; };
std::string signatureToString(const std::vector<VsInputField>& sig);

// ===========================================================================
// Matrix helpers. packColumn/transformPointRowVector/computeOrbitViewMatrix
// identical to paintcompose's own (row-major storage, row-vector v*M
// convention, registers uploaded column-by-column - see that file's comment
// for the derivation). buildPerspectiveLH/invert4x4 are NEW in this fork.
// ===========================================================================
void packColumn(const float M[16], int col, float out4[4]);
void transformPointRowVector(const float p[3], const float M[16], float out[3]);
void computeOrbitViewMatrix(const float boundsMin[3], const float boundsMax[3], float yaw, float pitch,
                             float distanceScale, float outView[16]);
void buildPerspectiveLH(float fovYRadians, float aspect, float nearZ, float farZ, float out[16]);
bool invert4x4(const float m[16], float out[16]);

struct ConstantFillLog { std::string constName; std::string decision; };
void fillFloat4Registers(std::vector<uint8_t>& cpuBuffer, uint16_t registerIndex, uint16_t registerCount,
                          const std::vector<std::array<float, 4>>& regs);

// GPU vertex layout for real car geometry (identical to paintcompose's own).
struct GpuVertexReal { float position[4], normal[4], tangent[4], blendIndices[4], texcoord0[4]; };
inline const D3D11_INPUT_ELEMENT_DESC kFullLayout[5] = {
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
inline const D3D11_INPUT_ELEMENT_DESC kQuadLayout[2] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexQuad, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertexQuad, texcoord0), D3D11_INPUT_PER_VERTEX_DATA, 0},
};

void collectFxoStems(const vpp::Container& c, std::map<std::string, std::vector<std::string>>& stemToFiles);
std::string pickVertexFile(const std::vector<std::string>& files, std::string& reasonIfNone);

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
                                              uint16_t materialCount);
MaterialShaderHashResult extractMaterialShaderHashes(vpp::ByteView content, size_t subheaderOffset,
                                                       size_t meshSubBlockOffset, uint32_t meshCLength,
                                                       uint16_t materialCount);

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
                               const std::string& stem, size_t role);

}  // namespace litcar
