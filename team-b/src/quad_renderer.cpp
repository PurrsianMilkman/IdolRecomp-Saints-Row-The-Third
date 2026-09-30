#include "sr3render/quad_renderer.h"

#include <d3d11.h>
#include <d3dcompiler.h>

namespace sr3render {

namespace {

std::string hrToString(HRESULT hr) {
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

// Fullscreen triangle from SV_VertexID: vertex 0 -> (-1,+1), 1 -> (+3,+1),
// 2 -> (-1,-3). Covers the viewport with one primitive and no buffers.
const char kShaderSource[] = R"HLSL(
struct VSOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOut vs_main(uint id : SV_VertexID) {
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv  = uv;
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

Texture2D    tex : register(t0);
SamplerState smp : register(s0);

float4 ps_main(VSOut i) : SV_Target {
    // Sampled as-is: no gamma correction, no tone mapping, no alpha
    // blending. The point is to see the decoded texels themselves, not a
    // prettier version of them - any "correction" here would risk making
    // a wrong block-format guess look plausible.
    return tex.Sample(smp, i.uv);
}
)HLSL";

bool compile(const char* entry, const char* target, ID3DBlob** blob, std::string& error) {
    ID3DBlob* errors = nullptr;
    HRESULT hr = D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, "sr3render_quad", nullptr,
                            nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob,
                            &errors);
    if (FAILED(hr)) {
        error = std::string("D3DCompile(") + entry + ") failed: " + hrToString(hr);
        if (errors != nullptr) {
            error += " - ";
            error.append(static_cast<const char*>(errors->GetBufferPointer()),
                         errors->GetBufferSize());
            errors->Release();
        }
        return false;
    }
    if (errors != nullptr) errors->Release();
    return true;
}

} // namespace

QuadRenderer::~QuadRenderer() { release(); }

void QuadRenderer::release() {
    if (sampler_ != nullptr) { sampler_->Release(); sampler_ = nullptr; }
    if (ps_ != nullptr) { ps_->Release(); ps_ = nullptr; }
    if (vs_ != nullptr) { vs_->Release(); vs_ = nullptr; }
}

bool QuadRenderer::initialise(ID3D11Device* device, std::string& error) {
    if (device == nullptr) {
        error = "null device";
        return false;
    }

    ID3DBlob* vsBlob = nullptr;
    if (!compile("vs_main", "vs_5_0", &vsBlob, error)) return false;
    HRESULT hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                            nullptr, &vs_);
    vsBlob->Release();
    if (FAILED(hr)) {
        error = "CreateVertexShader failed: " + hrToString(hr);
        return false;
    }

    ID3DBlob* psBlob = nullptr;
    if (!compile("ps_main", "ps_5_0", &psBlob, error)) {
        release();
        return false;
    }
    hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr,
                                   &ps_);
    psBlob->Release();
    if (FAILED(hr)) {
        error = "CreatePixelShader failed: " + hrToString(hr);
        release();
        return false;
    }

    // Point sampling with no mip bias: show the base-level texels as they
    // actually decode. Linear filtering would smooth over exactly the
    // 4x4-block artefacts that reveal a wrong block-format guess.
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
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

    return true;
}

void QuadRenderer::draw(ID3D11DeviceContext* context, ID3D11ShaderResourceView* srv) {
    if (context == nullptr || vs_ == nullptr || ps_ == nullptr) return;
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs_, nullptr, 0);
    context->PSSetShader(ps_, nullptr, 0);
    context->PSSetShaderResources(0, 1, &srv);
    context->PSSetSamplers(0, 1, &sampler_);
    context->Draw(3, 0);
}

} // namespace sr3render
