#include "sr3render/swap_chain.h"

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

template <typename T>
void safeRelease(T*& p) {
    if (p != nullptr) {
        p->Release();
        p = nullptr;
    }
}

} // namespace

SwapChain::~SwapChain() { release(); }

void SwapChain::releaseBackBufferViews() {
    safeRelease(staging_);
    safeRelease(rtv_);
}

void SwapChain::release() {
    releaseBackBufferViews();
    safeRelease(swapChain_);
    device_ = nullptr;
}

bool SwapChain::createBackBufferViews(std::string& error) {
    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                       reinterpret_cast<void**>(&backBuffer));
    if (FAILED(hr)) {
        error = "swap chain GetBuffer failed: " + hrToString(hr);
        return false;
    }

    hr = device_->CreateRenderTargetView(backBuffer, nullptr, &rtv_);
    if (FAILED(hr)) {
        backBuffer->Release();
        error = "CreateRenderTargetView (back buffer) failed: " + hrToString(hr);
        return false;
    }

    // Staging copy so the presented frame can be read back and written out
    // - see the header comment on why the windowed path stays verifiable.
    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);
    backBuffer->Release();

    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    hr = device_->CreateTexture2D(&desc, nullptr, &staging_);
    if (FAILED(hr)) {
        error = "CreateTexture2D (swap chain staging) failed: " + hrToString(hr);
        return false;
    }
    return true;
}

bool SwapChain::create(ID3D11Device* device, void* nativeWindowHandle, uint32_t width,
                       uint32_t height, std::string& error) {
    if (device == nullptr || nativeWindowHandle == nullptr) {
        error = "null device or window handle";
        return false;
    }
    if (width == 0 || height == 0) {
        error = "swap chain dimensions must be non-zero";
        return false;
    }
    device_ = device;
    width_ = width;
    height_ = height;

    // Reach the DXGI factory through the device rather than creating one
    // independently: a swap chain must come from the same factory that
    // produced the device's adapter, or creation fails on some drivers.
    IDXGIDevice* dxgiDevice = nullptr;
    HRESULT hr = device_->QueryInterface(__uuidof(IDXGIDevice),
                                         reinterpret_cast<void**>(&dxgiDevice));
    if (FAILED(hr)) {
        error = "QueryInterface(IDXGIDevice) failed: " + hrToString(hr);
        return false;
    }
    IDXGIAdapter* adapter = nullptr;
    hr = dxgiDevice->GetAdapter(&adapter);
    dxgiDevice->Release();
    if (FAILED(hr)) {
        error = "IDXGIDevice::GetAdapter failed: " + hrToString(hr);
        return false;
    }
    IDXGIFactory* factory = nullptr;
    hr = adapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory));
    adapter->Release();
    if (FAILED(hr)) {
        error = "IDXGIAdapter::GetParent(IDXGIFactory) failed: " + hrToString(hr);
        return false;
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Width = width;
    desc.BufferDesc.Height = height;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = static_cast<HWND>(nativeWindowHandle);
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    hr = factory->CreateSwapChain(device_, &desc, &swapChain_);
    factory->Release();
    if (FAILED(hr)) {
        error = "CreateSwapChain failed: " + hrToString(hr);
        return false;
    }

    return createBackBufferViews(error);
}

bool SwapChain::resize(uint32_t width, uint32_t height, std::string& error) {
    if (swapChain_ == nullptr) {
        error = "resize called on an uninitialised swap chain";
        return false;
    }
    // A minimised window reports 0x0; passing that to DXGI is an error, and
    // "no change" is the correct behaviour rather than a failure.
    if (width == 0 || height == 0) return true;
    if (width == width_ && height == height_) return true;

    // Every view onto the back buffer must be released before ResizeBuffers
    // or the call fails with the buffers still referenced.
    releaseBackBufferViews();

    HRESULT hr = swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        error = "ResizeBuffers failed: " + hrToString(hr);
        return false;
    }
    width_ = width;
    height_ = height;
    return createBackBufferViews(error);
}

void SwapChain::bind(ID3D11DeviceContext* context) {
    if (context == nullptr || rtv_ == nullptr) return;
    context->OMSetRenderTargets(1, &rtv_, nullptr);
    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(width_);
    vp.Height = static_cast<float>(height_);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context->RSSetViewports(1, &vp);
}

void SwapChain::clear(ID3D11DeviceContext* context, float r, float g, float b, float a) {
    if (context == nullptr || rtv_ == nullptr) return;
    const float rgba[4] = {r, g, b, a};
    context->ClearRenderTargetView(rtv_, rgba);
}

void SwapChain::present(bool vsync) {
    if (swapChain_ == nullptr) return;
    swapChain_->Present(vsync ? 1 : 0, 0);
}

bool SwapChain::capture(ID3D11DeviceContext* context, std::vector<uint8_t>& rgbaOut,
                        std::string& error) {
    if (context == nullptr || swapChain_ == nullptr || staging_ == nullptr) {
        error = "capture called on an uninitialised swap chain";
        return false;
    }

    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                       reinterpret_cast<void**>(&backBuffer));
    if (FAILED(hr)) {
        error = "GetBuffer for capture failed: " + hrToString(hr);
        return false;
    }
    context->CopyResource(staging_, backBuffer);
    backBuffer->Release();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context->Map(staging_, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        error = "Map(swap chain staging) failed: " + hrToString(hr);
        return false;
    }

    rgbaOut.resize(static_cast<size_t>(width_) * height_ * 4);
    const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);
    for (uint32_t y = 0; y < height_; ++y) {
        std::memcpy(rgbaOut.data() + static_cast<size_t>(y) * width_ * 4,
                    src + static_cast<size_t>(y) * mapped.RowPitch,
                    static_cast<size_t>(width_) * 4);
    }
    context->Unmap(staging_, 0);
    return true;
}

} // namespace sr3render
