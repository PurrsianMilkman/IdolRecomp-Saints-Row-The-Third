#include "sr3render/device.h"

#include <d3d11.h>
#include <dxgi.h>

#include <cstring>

namespace sr3render {

namespace {

std::string hrToString(HRESULT hr) {
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

std::string narrow(const wchar_t* wide) {
    if (wide == nullptr) return std::string();
    int need = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (need <= 1) return std::string();
    std::string out(static_cast<size_t>(need - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), need, nullptr, nullptr);
    return out;
}

template <typename T>
void safeRelease(T*& p) {
    if (p != nullptr) {
        p->Release();
        p = nullptr;
    }
}

} // namespace

RenderDevice::~RenderDevice() { release(); }

void RenderDevice::release() {
    safeRelease(staging_);
    safeRelease(rtv_);
    safeRelease(target_);
    safeRelease(context_);
    safeRelease(device_);
}

bool RenderDevice::initialise(uint32_t width, uint32_t height, std::string& error) {
    if (width == 0 || height == 0) {
        error = "render target dimensions must be non-zero";
        return false;
    }
    width_ = width;
    height_ = height;

    // Feature level 11_0 is the floor; WARP supports it fully, so there is
    // no need to negotiate downwards.
    const D3D_FEATURE_LEVEL wanted[] = {D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_11_0;
    UINT flags = 0;

    // Hardware first, WARP second. WARP is not a failure mode - it is a
    // legitimate, fully-conformant rasterizer, and is what makes this run
    // headless. Which one was obtained is reported, never assumed.
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, wanted,
                                   1, D3D11_SDK_VERSION, &device_, &got, &context_);
    if (SUCCEEDED(hr)) {
        kind_ = DeviceKind::Hardware;
    } else {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, wanted, 1,
                               D3D11_SDK_VERSION, &device_, &got, &context_);
        if (FAILED(hr)) {
            error = "D3D11CreateDevice failed for both hardware and WARP: " + hrToString(hr);
            return false;
        }
        kind_ = DeviceKind::Warp;
    }

    // Recover the adapter's own description, so logs name the real device
    // rather than just "hardware".
    {
        IDXGIDevice* dxgiDevice = nullptr;
        if (SUCCEEDED(device_->QueryInterface(__uuidof(IDXGIDevice),
                                              reinterpret_cast<void**>(&dxgiDevice)))) {
            IDXGIAdapter* adapter = nullptr;
            if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && adapter != nullptr) {
                DXGI_ADAPTER_DESC desc{};
                if (SUCCEEDED(adapter->GetDesc(&desc))) {
                    adapterName_ = narrow(desc.Description);
                }
                adapter->Release();
            }
            dxgiDevice->Release();
        }
    }

    D3D11_TEXTURE2D_DESC targetDesc{};
    targetDesc.Width = width_;
    targetDesc.Height = height_;
    targetDesc.MipLevels = 1;
    targetDesc.ArraySize = 1;
    targetDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    targetDesc.SampleDesc.Count = 1;
    targetDesc.Usage = D3D11_USAGE_DEFAULT;
    targetDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    hr = device_->CreateTexture2D(&targetDesc, nullptr, &target_);
    if (FAILED(hr)) {
        error = "CreateTexture2D (render target) failed: " + hrToString(hr);
        release();
        return false;
    }

    hr = device_->CreateRenderTargetView(target_, nullptr, &rtv_);
    if (FAILED(hr)) {
        error = "CreateRenderTargetView failed: " + hrToString(hr);
        release();
        return false;
    }

    // A CPU-readable staging copy, created once and reused - this is what
    // makes every milestone verifiable as an actual image.
    D3D11_TEXTURE2D_DESC stagingDesc = targetDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    hr = device_->CreateTexture2D(&stagingDesc, nullptr, &staging_);
    if (FAILED(hr)) {
        error = "CreateTexture2D (staging) failed: " + hrToString(hr);
        release();
        return false;
    }

    return true;
}

void RenderDevice::bindRenderTarget() {
    if (context_ == nullptr) return;
    context_->OMSetRenderTargets(1, &rtv_, nullptr);
    D3D11_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(width_);
    vp.Height = static_cast<float>(height_);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &vp);
}

void RenderDevice::bindRenderTargetWithDepth(ID3D11DepthStencilView* depthView) {
    if (context_ == nullptr) return;
    context_->OMSetRenderTargets(1, &rtv_, depthView);
    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(width_);
    vp.Height = static_cast<float>(height_);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &vp);
}

void RenderDevice::clear(const Rgba8& colour) {
    if (context_ == nullptr || rtv_ == nullptr) return;
    const float rgba[4] = {colour.r / 255.0f, colour.g / 255.0f, colour.b / 255.0f,
                           colour.a / 255.0f};
    context_->ClearRenderTargetView(rtv_, rgba);
}

bool RenderDevice::readBack(std::vector<uint8_t>& rgbaOut, std::string& error) {
    if (context_ == nullptr || target_ == nullptr || staging_ == nullptr) {
        error = "readBack called on an uninitialised device";
        return false;
    }

    context_->CopyResource(staging_, target_);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = context_->Map(staging_, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        error = "Map(staging) failed: " + hrToString(hr);
        return false;
    }

    // RowPitch is the GPU's stride and is usually larger than width*4, so
    // the rows are copied out one at a time rather than in one block.
    rgbaOut.resize(static_cast<size_t>(width_) * height_ * 4);
    const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);
    for (uint32_t y = 0; y < height_; ++y) {
        std::memcpy(rgbaOut.data() + static_cast<size_t>(y) * width_ * 4,
                    src + static_cast<size_t>(y) * mapped.RowPitch,
                    static_cast<size_t>(width_) * 4);
    }

    context_->Unmap(staging_, 0);
    return true;
}

} // namespace sr3render
