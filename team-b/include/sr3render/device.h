// D3D11 device/render-target layer for the SR3 engine.
//
// WHY D3D11, recorded so the choice isn't re-litigated: this sandbox's
// Windows SDK (10.0.22621 / 10.0.26100) ships d3d11.h, dxgi.h and
// d3dcompiler.h, but NOT d3d9.h - D3D9 would require the deprecated
// standalone DirectX SDK, which is not installed. That alone settles it.
// Two further reasons it is the right call anyway:
//
//  * WARP. D3D11 has a fully-conformant software rasterizer built into
//    Windows, so a device can always be created even with no GPU, no
//    display and no interactive session. Every rendering milestone here
//    therefore runs headless, which is what makes them verifiable in this
//    environment at all.
//  * BC1/BC3 are core D3D11 formats, which is exactly what SR3's texture
//    pixel-format codes 400/402 ARE (spec-texture-format.md Sec4, CLOSED
//    2026-09-11 by decoding and rendering; code 403 also closed 2026-09-20,
//    D3DFMT_R5G6B5, Sec11 - `texture_upload.h`, the sibling file in this
//    same directory, already reflects the closed state this comment did
//    not until fixed 2026-09-29, rule 16).
//
// The game's own .fxo_pc shaders are D3D9 SM1-3 bytecode and CANNOT be fed
// to D3D11 directly. That is a known, accepted consequence: this engine
// writes its own HLSL for now. Reusing the shipped shaders would mean
// either a D3D9 path (no header available) or translating SM1-3 bytecode,
// and neither is needed for the current milestones.
//
// HEADLESS BY DEFAULT. The primary path renders to an offscreen target and
// reads the pixels back, because "it rendered" is only a real claim if
// there is an image to look at (see png_writer.h). A real window/swapchain
// is available but optional.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;

namespace sr3render {

// Which rasterizer the device actually got. Reported rather than assumed:
// on this hardware it may legitimately be either, and a texture that looks
// right on WARP but wrong on hardware (or vice versa) is a finding, not a
// detail.
enum class DeviceKind {
    Hardware, // a real GPU adapter
    Warp,     // Windows' software rasterizer - always available
};

struct Rgba8 {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

// Owns a D3D11 device plus one offscreen RGBA8 render target, and can read
// that target back to CPU memory. Non-copyable; releases everything it
// created on destruction.
class RenderDevice {
public:
    RenderDevice() = default;
    ~RenderDevice();
    RenderDevice(const RenderDevice&) = delete;
    RenderDevice& operator=(const RenderDevice&) = delete;

    // Creates the device and a `width` x `height` offscreen target. Tries a
    // hardware adapter first, then falls back to WARP; `kind()` reports
    // which was obtained. Returns false with `error` set on failure.
    bool initialise(uint32_t width, uint32_t height, std::string& error);

    DeviceKind kind() const { return kind_; }
    const std::string& adapterName() const { return adapterName_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }

    ID3D11Device* device() const { return device_; }
    ID3D11DeviceContext* context() const { return context_; }
    ID3D11RenderTargetView* renderTargetView() const { return rtv_; }

    // Binds the offscreen target and sets a full-size viewport. Call before
    // issuing draws.
    void bindRenderTarget();

    // Same, but with a depth-stencil view attached. 3D geometry needs this:
    // without a depth buffer, triangles paint in index order and back faces
    // overwrite front ones, which looks like corrupt geometry rather than
    // like a missing render state.
    void bindRenderTargetWithDepth(ID3D11DepthStencilView* depthView);

    // Clears the offscreen target to a solid colour.
    void clear(const Rgba8& colour);

    // Copies the offscreen target back to CPU memory as tightly-packed
    // 8-bit RGBA, top row first - the layout writePng() expects. Handles
    // the staging copy and row-pitch unpacking. Returns false with `error`
    // set on failure.
    bool readBack(std::vector<uint8_t>& rgbaOut, std::string& error);

private:
    void release();

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ID3D11Texture2D* target_ = nullptr;
    ID3D11RenderTargetView* rtv_ = nullptr;
    ID3D11Texture2D* staging_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    DeviceKind kind_ = DeviceKind::Warp;
    std::string adapterName_;
};

} // namespace sr3render
