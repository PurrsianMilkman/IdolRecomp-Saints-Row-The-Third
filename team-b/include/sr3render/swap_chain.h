// DXGI swap chain: presents rendered frames to a Window.
//
// Kept separate from RenderDevice on purpose. The device owns the D3D11
// device/context and the offscreen target that the headless path uses;
// this owns only the window-facing resources. That split is what lets the
// verification path (offscreen -> readback -> PNG) stay completely
// independent of whether a window exists at all.
//
// IMPORTANT - it can still be verified. capture() reads the back buffer
// back to CPU memory in the same layout RenderDevice::readBack() produces,
// so a windowed session can write a PNG of exactly what was presented.
// Without that, "the window renders" would rest on a human glance, which
// is weaker evidence than this project accepts elsewhere.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11Texture2D;
struct IDXGISwapChain;

namespace sr3render {

class SwapChain {
public:
    SwapChain() = default;
    ~SwapChain();
    SwapChain(const SwapChain&) = delete;
    SwapChain& operator=(const SwapChain&) = delete;

    // Creates a swap chain for `nativeWindowHandle` (an HWND as void*)
    // using an existing device. Returns false with `error` set on failure.
    bool create(ID3D11Device* device, void* nativeWindowHandle, uint32_t width, uint32_t height,
                std::string& error);

    // Recreates the back buffer at a new size. A no-op if the size is
    // unchanged or degenerate (a minimised window reports 0x0, which must
    // not be passed through to DXGI).
    bool resize(uint32_t width, uint32_t height, std::string& error);

    // Binds the back buffer as the render target and sets a full viewport.
    void bind(ID3D11DeviceContext* context);

    // Clears the back buffer.
    void clear(ID3D11DeviceContext* context, float r, float g, float b, float a);

    // Presents. `vsync` true waits for vblank.
    void present(bool vsync);

    // Reads the back buffer to tightly-packed 8-bit RGBA, top row first -
    // the same layout writePng() expects. This is what keeps the windowed
    // path verifiable rather than merely visible.
    bool capture(ID3D11DeviceContext* context, std::vector<uint8_t>& rgbaOut, std::string& error);

    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    ID3D11RenderTargetView* renderTargetView() const { return rtv_; }

private:
    void releaseBackBufferViews();
    void release();
    bool createBackBufferViews(std::string& error);

    ID3D11Device* device_ = nullptr; // not owned
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D11RenderTargetView* rtv_ = nullptr;
    ID3D11Texture2D* staging_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};

} // namespace sr3render
