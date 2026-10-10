#include "sr3render/composite_renderer.h"

#include <algorithm>
#include <cstring>
#include <numeric>
#include <vector>

#include <d3d11.h>
#include <d3dcompiler.h>

#include "vppcache/shader_cache.h"

namespace sr3render {

namespace {

std::string hrToString(HRESULT hr) {
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

// Pixel-space positioned textured quad, alpha-blended. Deliberately NOT the
// SV_VertexID fullscreen-triangle trick QuadRenderer (quad_renderer.h) uses
// - each composite layer needs its own position/size/alpha, so this needs a
// real dynamic vertex buffer, the same shape as tools/sr3_viewer.cpp's own
// VintQuadPipeline ("pixel-space positions, UVs and a per-vertex RGBA8
// colour, 4-vertex strip") - but simplified: no render-mode blend codes, no
// masking, no sRGB/half-texel handling, since none of those are things
// spec-tables-customization.md Sec12.1e confirms for THIS pipeline (that
// spec confirms only "Alpha ... per-vertex alpha byte" and the painter's-
// algorithm draw order - see this file's own header comment). Standard
// SRC_ALPHA/INV_SRC_ALPHA "over" compositing is the ordinary, non-invented
// reading of "a per-vertex alpha byte" used to paint one layer over the
// ones already in the sheet; it is NOT a literal D3D9 render-state trace
// the way spec-vint-render.md Sec15.4 gives for the UI pipeline, and is
// flagged here as a reasonable choice rather than a confirmed one.
struct Vertex {
    float x, y;
    float u, v;
    uint8_t rgba[4];
};

const char kShaderSource[] = R"HLSL(
cbuffer Constants : register(b0) {
    float2 toClip;
    float2 pad;
};
struct VSIn {
    float2 pos : POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
};
struct VSOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
};
VSOut vs_main(VSIn i) {
    VSOut o;
    o.pos = float4(i.pos.x * toClip.x - 1.0, 1.0 - i.pos.y * toClip.y, 0.0, 1.0);
    o.uv = i.uv;
    o.col = i.col;
    return o;
}
Texture2D    tex : register(t0);
SamplerState smp : register(s0);
float4 ps_main(VSOut i) : SV_Target {
    float4 t = tex.Sample(smp, i.uv);
    return t * i.col;
}
)HLSL";

bool compile(const char* entry, const char* target, ID3DBlob** blob, std::string& error) {
    const std::string cacheKey =
        vppcache::shaderCompileKey(kShaderSource, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3);
    std::vector<uint8_t> cachedBytecode;
    if (vppcache::tryGetCompiledShader(cacheKey, cachedBytecode)) {
        HRESULT hr = D3DCreateBlob(cachedBytecode.size(), blob);
        if (SUCCEEDED(hr)) {
            std::memcpy((*blob)->GetBufferPointer(), cachedBytecode.data(), cachedBytecode.size());
            return true;
        }
    }

    ID3DBlob* errors = nullptr;
    HRESULT hr = D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, "sr3render_composite", nullptr, nullptr,
                            entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob, &errors);
    if (FAILED(hr)) {
        error = std::string("D3DCompile(") + entry + ") failed: " + hrToString(hr);
        if (errors != nullptr) {
            error += " - ";
            error.append(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
            errors->Release();
        }
        return false;
    }
    if (errors != nullptr) errors->Release();
    vppcache::putCompiledShader(cacheKey, (*blob)->GetBufferPointer(), (*blob)->GetBufferSize());
    return true;
}

template <typename T>
void safeRelease(T*& p) {
    if (p != nullptr) {
        p->Release();
        p = nullptr;
    }
}

uint8_t clampToByte(float v01) {
    if (v01 <= 0.0f) return 0;
    if (v01 >= 1.0f) return 255;
    return static_cast<uint8_t>(v01 * 255.0f + 0.5f);
}

} // namespace

std::vector<size_t> OrderLayersForComposite(const std::vector<CompositeLayerPlacement>& layers) {
    std::vector<size_t> order(layers.size());
    std::iota(order.begin(), order.end(), size_t{0});
    // §12.1e point 3: "the same function sorts layers by +0x34 (Layer,
    // lowest-first - a painter's-algorithm draw order)". stable_sort keeps
    // equal-Layer rows in their original (input) order - see this
    // function's own header comment for why that tie-break, not a
    // different one, is used.
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return layers[a].layer < layers[b].layer;
    });
    return order;
}

CompositeSheetRenderer::~CompositeSheetRenderer() { release(); }

void CompositeSheetRenderer::release() {
    safeRelease(blendState_);
    safeRelease(sampler_);
    safeRelease(constantBuffer_);
    safeRelease(vertexBuffer_);
    safeRelease(inputLayout_);
    safeRelease(ps_);
    safeRelease(vs_);
    safeRelease(staging_);
    safeRelease(resultSrv_);
    safeRelease(rtv_);
    safeRelease(target_);
}

bool CompositeSheetRenderer::initialise(ID3D11Device* device, std::string& error) {
    if (device == nullptr) {
        error = "null device";
        return false;
    }

    // The confirmed render target: 2048x1024, D3DFMT_X8R8G8B8 (format 22,
    // Sec12.1e point 2). FORMAT MAPPING follows this project's OWN already-
    // established convention (sr3render/texture_upload.cpp's formatFor()):
    // D3DFMT_A8R8G8B8 (code 407) maps to DXGI_FORMAT_B8G8R8A8_UNORM as a
    // "Direct" mapping - "the game's bytes are handed to D3D11 unchanged"
    // because the two formats are BIT-IDENTICAL in memory (BGRA byte
    // order). D3DFMT_X8R8G8B8 is the IDENTICAL 32-bit BGRA memory layout
    // to D3DFMT_A8R8G8B8 - the leading "X" means only "alpha is not read",
    // not a different byte order - so the same precedent gives the same
    // direct DXGI equivalent, DXGI_FORMAT_B8G8R8A8_UNORM. This is NOT a new
    // format-mapping rule invented for this task; it is the existing rule
    // applied to a format the existing table did not happen to need yet.
    //
    // MipLevels=1 (not the confirmed 12-level AUTOGENMIPMAP chain, Sec12.1e
    // point 2): this renderer only builds and reads back ONE level for
    // inspection/testing - see this header's own top comment.
    D3D11_TEXTURE2D_DESC targetDesc{};
    targetDesc.Width = kCompositeSheetWidth;
    targetDesc.Height = kCompositeSheetHeight;
    targetDesc.MipLevels = 1;
    targetDesc.ArraySize = 1;
    targetDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    targetDesc.SampleDesc.Count = 1;
    targetDesc.Usage = D3D11_USAGE_DEFAULT;
    targetDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device->CreateTexture2D(&targetDesc, nullptr, &target_);
    if (FAILED(hr)) {
        error = "CreateTexture2D (composite sheet) failed: " + hrToString(hr);
        return false;
    }
    hr = device->CreateRenderTargetView(target_, nullptr, &rtv_);
    if (FAILED(hr)) {
        error = "CreateRenderTargetView failed: " + hrToString(hr);
        release();
        return false;
    }
    hr = device->CreateShaderResourceView(target_, nullptr, &resultSrv_);
    if (FAILED(hr)) {
        error = "CreateShaderResourceView (composite result) failed: " + hrToString(hr);
        release();
        return false;
    }

    D3D11_TEXTURE2D_DESC stagingDesc = targetDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = device->CreateTexture2D(&stagingDesc, nullptr, &staging_);
    if (FAILED(hr)) {
        error = "CreateTexture2D (staging) failed: " + hrToString(hr);
        release();
        return false;
    }

    ID3DBlob* vsBlob = nullptr;
    if (!compile("vs_main", "vs_5_0", &vsBlob, error)) {
        release();
        return false;
    }
    hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs_);
    if (FAILED(hr)) {
        error = "CreateVertexShader failed: " + hrToString(hr);
        vsBlob->Release();
        release();
        return false;
    }
    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    hr = device->CreateInputLayout(layout, 3, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &inputLayout_);
    vsBlob->Release();
    if (FAILED(hr)) {
        error = "CreateInputLayout failed: " + hrToString(hr);
        release();
        return false;
    }

    ID3DBlob* psBlob = nullptr;
    if (!compile("ps_main", "ps_5_0", &psBlob, error)) {
        release();
        return false;
    }
    hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps_);
    psBlob->Release();
    if (FAILED(hr)) {
        error = "CreatePixelShader failed: " + hrToString(hr);
        release();
        return false;
    }

    D3D11_BUFFER_DESC vbDesc{};
    vbDesc.ByteWidth = sizeof(Vertex) * 4;
    vbDesc.Usage = D3D11_USAGE_DYNAMIC;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device->CreateBuffer(&vbDesc, nullptr, &vertexBuffer_);
    if (FAILED(hr)) {
        error = "CreateBuffer (vertex) failed: " + hrToString(hr);
        release();
        return false;
    }

    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.ByteWidth = 16; // {toClip(2 floats), pad(2 floats)} - matches kShaderSource's cbuffer Constants
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device->CreateBuffer(&cbDesc, nullptr, &constantBuffer_);
    if (FAILED(hr)) {
        error = "CreateBuffer (constant) failed: " + hrToString(hr);
        release();
        return false;
    }

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; // not Sec12.1e-confirmed - see this file's own top comment
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device->CreateSamplerState(&sd, &sampler_);
    if (FAILED(hr)) {
        error = "CreateSamplerState failed: " + hrToString(hr);
        release();
        return false;
    }

    // Standard "over" alpha blending (SRC_ALPHA/INV_SRC_ALPHA, both colour
    // and alpha channels) - see this file's own top comment for why this
    // reading, not a literal render-state trace, is used here.
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = device->CreateBlendState(&bd, &blendState_);
    if (FAILED(hr)) {
        error = "CreateBlendState failed: " + hrToString(hr);
        release();
        return false;
    }

    return true;
}

void CompositeSheetRenderer::begin(ID3D11DeviceContext* context, const uint8_t clearColour[4]) {
    if (context == nullptr || rtv_ == nullptr) return;
    static const uint8_t kDefaultClear[4] = {0, 0, 0, 0}; // transparent black - see this header's own comment
    const uint8_t* c = clearColour != nullptr ? clearColour : kDefaultClear;
    const float rgba[4] = {c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, c[3] / 255.0f};
    context->ClearRenderTargetView(rtv_, rgba);

    context->OMSetRenderTargets(1, &rtv_, nullptr);
    D3D11_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(kCompositeSheetWidth);
    vp.Height = static_cast<float>(kCompositeSheetHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context->RSSetViewports(1, &vp);

    context->IASetInputLayout(inputLayout_);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    const UINT stride = sizeof(Vertex), offset = 0;
    context->IASetVertexBuffers(0, 1, &vertexBuffer_, &stride, &offset);
    context->VSSetShader(vs_, nullptr, 0);
    context->PSSetShader(ps_, nullptr, 0);
    context->PSSetSamplers(0, 1, &sampler_);
    const float blendFactor[4] = {0, 0, 0, 0};
    context->OMSetBlendState(blendState_, blendFactor, 0xFFFFFFFFu);

    // toClip only depends on this renderer's own fixed sheet size, so it
    // is written once here rather than per drawLayer() call.
    D3D11_MAPPED_SUBRESOURCE cm{};
    if (SUCCEEDED(context->Map(constantBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &cm))) {
        float* f = static_cast<float*>(cm.pData);
        f[0] = 2.0f / static_cast<float>(kCompositeSheetWidth);
        f[1] = 2.0f / static_cast<float>(kCompositeSheetHeight);
        f[2] = f[3] = 0.0f;
        context->Unmap(constantBuffer_, 0);
    }
    context->VSSetConstantBuffers(0, 1, &constantBuffer_);
}

void CompositeSheetRenderer::drawLayer(ID3D11DeviceContext* context, const CompositeLayerPlacement& layer) {
    if (context == nullptr || layer.srv == nullptr) return; // see this function's own header comment

    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(context->Map(vertexBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    Vertex* v = static_cast<Vertex*>(m.pData);
    const uint8_t a = clampToByte(layer.alpha);
    // Strip order: (x,y) (x+w,y) (x,y+h) (x+w,y+h). RGB is always full
    // white (255) so only the alpha channel modulates the sampled texel -
    // Sec12.1e point 3's own "per-vertex alpha byte, not a separate
    // blend-mode field" - no colour tint mechanism is confirmed anywhere
    // in this pipeline.
    v[0] = {layer.x, layer.y, 0.0f, 0.0f, {255, 255, 255, a}};
    v[1] = {layer.x + layer.width, layer.y, 1.0f, 0.0f, {255, 255, 255, a}};
    v[2] = {layer.x, layer.y + layer.height, 0.0f, 1.0f, {255, 255, 255, a}};
    v[3] = {layer.x + layer.width, layer.y + layer.height, 1.0f, 1.0f, {255, 255, 255, a}};
    context->Unmap(vertexBuffer_, 0);

    context->PSSetShaderResources(0, 1, &layer.srv);
    context->Draw(4, 0);
}

bool CompositeSheetRenderer::readBack(ID3D11DeviceContext* context, std::vector<uint8_t>& rgbaOut,
                                      std::string& error) {
    if (context == nullptr || target_ == nullptr || staging_ == nullptr) {
        error = "readBack called on an uninitialised CompositeSheetRenderer";
        return false;
    }
    context->CopyResource(staging_, target_);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = context->Map(staging_, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        error = "Map(staging) failed: " + hrToString(hr);
        return false;
    }

    rgbaOut.resize(static_cast<size_t>(kCompositeSheetWidth) * kCompositeSheetHeight * 4);
    const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);
    for (uint32_t y = 0; y < kCompositeSheetHeight; ++y) {
        const uint8_t* srcRow = src + static_cast<size_t>(y) * mapped.RowPitch;
        uint8_t* dstRow = rgbaOut.data() + static_cast<size_t>(y) * kCompositeSheetWidth * 4;
        for (uint32_t x = 0; x < kCompositeSheetWidth; ++x) {
            // B8G8R8A8_UNORM memory order is B,G,R,A - swap bytes 0/2 to
            // produce the R,G,B,A order png_writer.h's writePng() expects
            // (see this function's own doc comment in the header).
            dstRow[x * 4 + 0] = srcRow[x * 4 + 2]; // R <- B
            dstRow[x * 4 + 1] = srcRow[x * 4 + 1]; // G
            dstRow[x * 4 + 2] = srcRow[x * 4 + 0]; // B <- R
            dstRow[x * 4 + 3] = srcRow[x * 4 + 3]; // A
        }
    }
    context->Unmap(staging_, 0);
    return true;
}

bool BuildCharacterSkinComposite(ID3D11Device* device, ID3D11DeviceContext* context,
                                 const std::vector<CompositeLayerPlacement>& layers,
                                 CompositeSheetRenderer& sheet, std::string& error,
                                 size_t* skippedUnresolvedCount) {
    if (skippedUnresolvedCount != nullptr) *skippedUnresolvedCount = 0;
    if (!sheet.initialise(device, error)) return false;

    sheet.begin(context);
    const std::vector<size_t> order = OrderLayersForComposite(layers);
    for (size_t idx : order) {
        const CompositeLayerPlacement& layer = layers[idx];
        if (layer.srv == nullptr) {
            if (skippedUnresolvedCount != nullptr) ++*skippedUnresolvedCount;
            continue;
        }
        sheet.drawLayer(context, layer);
    }
    return true;
}

void BindCompositeResultToCharacterMesh(ID3D11ShaderResourceView* /*compositeResult*/,
                                        void* /*targetMeshOrMaterialHandle*/) {
    // Intentionally empty - see this function's own doc comment in
    // composite_renderer.h. Do not implement without a new spec citation
    // for the real bind instruction (Sec12.1e point 4/5).
}

} // namespace sr3render
