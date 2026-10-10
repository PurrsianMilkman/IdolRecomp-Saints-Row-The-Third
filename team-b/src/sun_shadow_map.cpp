#include "sr3render/sun_shadow_map.h"

#include <d3d11.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "sr3render/png_writer.h"

namespace sr3render {

namespace {

std::string hrToString(HRESULT hr) {
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

template <typename T>
void safeRelease(T*& p) {
    if (p != nullptr) { p->Release(); p = nullptr; }
}

}  // namespace

SunShadowMap::~SunShadowMap() { release(); }

void SunShadowMap::release() {
    safeRelease(sampler_);
    safeRelease(srv_);
    safeRelease(dsv_);
    safeRelease(texture_);
    size_ = 0;
}

bool SunShadowMap::initialise(ID3D11Device* device, uint32_t size, std::string& error) {
    release();
    if (device == nullptr || size == 0) { error = "SunShadowMap::initialise: null device or zero size"; return false; }

    // R32_TYPELESS so the SAME texture can be viewed both as a real
    // D32_FLOAT depth-stencil target (the generation pass below) and as an
    // R32_FLOAT shader-resource view (the lit pass's own shadow sample) -
    // the standard D3D11 dual-view technique for a depth buffer that also
    // needs to be read back as a texture (mesh_renderer.cpp's own
    // createDepth() cannot be reused here for exactly this reason: its
    // DXGI_FORMAT_D32_FLOAT texture is depth-stencil-only, never
    // shader-resource-bindable).
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = size;
    desc.Height = size;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &texture_);
    if (FAILED(hr)) {
        error = "SunShadowMap: CreateTexture2D failed: " + hrToString(hr);
        return false;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    hr = device->CreateDepthStencilView(texture_, &dsvDesc, &dsv_);
    if (FAILED(hr)) {
        error = "SunShadowMap: CreateDepthStencilView failed: " + hrToString(hr);
        release();
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    hr = device->CreateShaderResourceView(texture_, &srvDesc, &srv_);
    if (FAILED(hr)) {
        error = "SunShadowMap: CreateShaderResourceView failed: " + hrToString(hr);
        release();
        return false;
    }

    // Point filter: this task's own v1 is a hard-edged single-tap depth
    // compare (done manually in the lit pixel shader, not a hardware PCF/
    // SamplerComparisonState) - a linear filter on raw (non-comparison)
    // depth values would just blend two unrelated depth numbers together,
    // which is not the same thing as PCF and would look like a soft shadow
    // while actually being wrong. BORDER address mode with a border colour
    // of 1.0 (far plane) means a light-space UV that falls outside this
    // shadow map's own coverage samples as "far", i.e. "not occluded" - the
    // honest behaviour for geometry outside the one map this v1 provides.
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    sd.BorderColor[0] = sd.BorderColor[1] = sd.BorderColor[2] = sd.BorderColor[3] = 1.0f;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device->CreateSamplerState(&sd, &sampler_);
    if (FAILED(hr)) {
        error = "SunShadowMap: CreateSamplerState failed: " + hrToString(hr);
        release();
        return false;
    }

    size_ = size;
    return true;
}

void SunShadowMap::computeLightViewProjection(const float sunDirection[3], const float focusCentre[3], float radius,
                                               float outLightViewProjection[16]) const {
    if (radius <= 0.0f) radius = 1.0f;

    float dir[3] = {sunDirection[0], sunDirection[1], sunDirection[2]};
    float dlen = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (dlen <= 1e-6f) { dir[0] = 0.0f; dir[1] = 1.0f; dir[2] = 0.0f; dlen = 1.0f; }
    for (float& c : dir) c /= dlen;

    // Light "camera" sits on the +sunDirection side of the focus point (i.e.
    // towards the sun) and looks back along -sunDirection - standard
    // directional-light shadow-camera placement, using `sunDirection`'s own
    // already-established "points FROM a lit surface TOWARD the sun"
    // convention (mesh_renderer.cpp's ps_main: dot(normal, sunDirection)).
    const float distance = radius * 3.0f;
    const float eye[3] = {focusCentre[0] + dir[0] * distance, focusCentre[1] + dir[1] * distance,
                          focusCentre[2] + dir[2] * distance};
    float forward[3] = {focusCentre[0] - eye[0], focusCentre[1] - eye[1], focusCentre[2] - eye[2]};
    float flen = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
    if (flen <= 0.0f) flen = 1.0f;
    for (float& c : forward) c /= flen;

    // World-up fallback when the sun sits near-vertical (forward nearly
    // parallel to the usual (0,1,0) up) - same degenerate-case guard
    // mesh_renderer.cpp's own look-at builders do not need (their forward
    // comes from a yaw/pitch orbit, never straight up/down), but a sun at
    // local noon genuinely can be.
    float worldUp[3] = {0.0f, 1.0f, 0.0f};
    if (std::fabs(forward[0] * worldUp[0] + forward[1] * worldUp[1] + forward[2] * worldUp[2]) > 0.999f) {
        worldUp[0] = 0.0f; worldUp[1] = 0.0f; worldUp[2] = 1.0f;
    }

    float right[3] = {forward[1] * worldUp[2] - forward[2] * worldUp[1],
                      forward[2] * worldUp[0] - forward[0] * worldUp[2],
                      forward[0] * worldUp[1] - forward[1] * worldUp[0]};
    float rlen = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    if (rlen <= 0.0f) rlen = 1.0f;
    for (float& c : right) c /= rlen;

    float up[3] = {right[1] * forward[2] - right[2] * forward[1], right[2] * forward[0] - right[0] * forward[2],
                   right[0] * forward[1] - right[1] * forward[0]};

    // Right-handed look-at, row-major/row-vector convention - identical
    // shape to mesh_renderer.cpp's buildOrbitViewProjection()/buildFreeViewProjection()
    // view matrices, just with this light camera's own eye/basis.
    const float view[16] = {
        right[0], up[0], forward[0], 0.0f,
        right[1], up[1], forward[1], 0.0f,
        right[2], up[2], forward[2], 0.0f,
        -(right[0] * eye[0] + right[1] * eye[1] + right[2] * eye[2]),
        -(up[0] * eye[0] + up[1] * eye[1] + up[2] * eye[2]),
        -(forward[0] * eye[0] + forward[1] * eye[1] + forward[2] * eye[2]),
        1.0f,
    };

    // Orthographic projection sized to the CHOSEN bounding sphere (this
    // file's own banner: a single reasonable bounding volume, not the real
    // engine's 4 data-driven cascade splits). D3D convention depth range
    // [0,1] (near=0, far=1) - the SAME convention mesh_renderer.cpp's own
    // perspective projections already use (buildOrbitViewProjection()'s `q`
    // term), so the shadow-map depth values and the lit pass's own light-
    // space depth comparison in ps_main are on identical footing with no
    // extra remap needed.
    // Tightened to bracket the bounding sphere itself (distance +/- a 10%
    // margin), not "0 to comfortably past the far side" - the eye sits
    // `distance` (radius*3) away from the sphere, so geometry only ever
    // occupies [distance-radius, distance+radius] along the light's forward
    // axis; mapping THAT narrow band (not the whole 0..distance+radius
    // range, most of which is empty space between the eye and the scene)
    // into the depth buffer's [0,1] range concentrates this v1 single-map's
    // limited depth precision where the actual casters are, which in turn
    // is what makes a small, fixed shadow bias (below) actually work rather
    // than either acne (bias too small relative to wasted precision) or
    // visible peter-panning (bias oversized to compensate).
    const float nearZ = (std::max)(0.01f * radius, distance - radius * 1.1f);
    const float farZ = distance + radius * 1.1f;
    const float sx = 1.0f / radius;
    const float sy = 1.0f / radius;
    const float sz = 1.0f / (farZ - nearZ);
    const float oz = -nearZ / (farZ - nearZ);
    const float proj[16] = {
        sx, 0.0f, 0.0f, 0.0f,
        0.0f, sy, 0.0f, 0.0f,
        0.0f, 0.0f, sz, 0.0f,
        0.0f, 0.0f, oz, 1.0f,
    };

    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += view[r * 4 + k] * proj[k * 4 + c];
            outLightViewProjection[r * 4 + c] = sum;
        }
    }
}

bool SunShadowMap::dumpDepthToPng(ID3D11Device* device, ID3D11DeviceContext* context, const std::string& path,
                                   std::string& error) const {
    if (device == nullptr || context == nullptr || texture_ == nullptr) {
        error = "SunShadowMap::dumpDepthToPng: not initialised";
        return false;
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = size_;
    desc.Height = size_;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ID3D11Texture2D* staging = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &staging);
    if (FAILED(hr)) {
        error = "SunShadowMap::dumpDepthToPng: CreateTexture2D(staging) failed: " + hrToString(hr);
        return false;
    }
    context->CopyResource(staging, texture_);

    D3D11_MAPPED_SUBRESOURCE m{};
    hr = context->Map(staging, 0, D3D11_MAP_READ, 0, &m);
    if (FAILED(hr)) {
        error = "SunShadowMap::dumpDepthToPng: Map failed: " + hrToString(hr);
        staging->Release();
        return false;
    }

    std::vector<uint8_t> rgba(static_cast<size_t>(size_) * size_ * 4);
    const uint8_t* rowBytes = static_cast<const uint8_t*>(m.pData);
    for (uint32_t y = 0; y < size_; ++y) {
        const float* row = reinterpret_cast<const float*>(rowBytes + static_cast<size_t>(y) * m.RowPitch);
        for (uint32_t x = 0; x < size_; ++x) {
            // Raw [0,1] depth (0 = near, 1 = far/cleared-never-written) mapped
            // directly to [0,255] grayscale - no min/max renormalisation (see
            // this method's own header comment for why).
            float d = row[x];
            if (d < 0.0f) d = 0.0f;
            if (d > 1.0f) d = 1.0f;
            const uint8_t g = static_cast<uint8_t>(d * 255.0f + 0.5f);
            const size_t idx = (static_cast<size_t>(y) * size_ + x) * 4;
            rgba[idx + 0] = g;
            rgba[idx + 1] = g;
            rgba[idx + 2] = g;
            rgba[idx + 3] = 255;
        }
    }
    context->Unmap(staging, 0);
    staging->Release();

    return writePng(path, size_, size_, rgba, error);
}

void SunShadowMap::beginDepthPass(ID3D11DeviceContext* context) {
    if (context == nullptr || dsv_ == nullptr) return;
    context->OMSetRenderTargets(0, nullptr, dsv_);
    context->ClearDepthStencilView(dsv_, D3D11_CLEAR_DEPTH, 1.0f, 0);
    D3D11_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(size_);
    vp.Height = static_cast<float>(size_);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context->RSSetViewports(1, &vp);
}

void SunShadowMap::endDepthPass(ID3D11DeviceContext* context, uint32_t restoreWidth, uint32_t restoreHeight) {
    if (context == nullptr) return;
    D3D11_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(restoreWidth);
    vp.Height = static_cast<float>(restoreHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context->RSSetViewports(1, &vp);
}

}  // namespace sr3render
