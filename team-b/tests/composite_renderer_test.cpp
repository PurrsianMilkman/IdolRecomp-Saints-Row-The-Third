// Unit tests for sr3render::CompositeSheetRenderer / BuildCharacterSkinComposite
// / OrderLayersForComposite (include/sr3render/composite_renderer.h) - the
// character skin composite-bake renderer built per spec-tables-
// customization.md Sec12.1e (CONFIRMED 2026-10-08) and narrowed by Sec12.1f
// (player-only singleton scoping CONFIRMED 2026-10-08) - see that header's
// own top comment for the full spec citation chain, not repeated here.
//
// This task's own brief calls for three things, each with its own test
// below:
//   1. Layer draw ORDER correctness - lowest Layer value drawn first
//      (painter's-algorithm, Sec12.1e point 3) -
//      testOrderLayersForComposite() (pure CPU, the sort itself) and
//      testDrawOrderPaintsHigherLayerOnTop() (GPU, the actual pixel
//      result of drawing in that order).
//   2. Scaled positions landing where expected -
//      testPlacementLandsAtGivenCoordinates(): a CompositeLayerPlacement's
//      already-scaled x/y/width/height (see composite_renderer.h's own
//      comment on kOpenPlacementScale - the scale itself is the CALLER's
//      job, not this renderer's) must land the drawn quad exactly there,
//      not somewhere else.
//   3. Output sheet has the confirmed 2048x1024 dimensions (Sec12.1e
//      point 2) - testSheetDimensions().
//
// GPU-BACKED: this is the first sr3render ctest unit test to create a real
// D3D11 device itself - every other sr3render check so far is a golden-
// scene tool + checked-in baseline image (tools/golden_scene_check.cpp),
// not a ctest-registered unit test. sr3render::RenderDevice's own WARP
// fallback (device.h: "a device can always be created even with no GPU")
// means this still runs headless in CI with no physical GPU present, same
// guarantee every golden-scene tool already relies on.

#include <d3d11.h>

#include <iostream>
#include <string>
#include <vector>

#include "sr3render/composite_renderer.h"
#include "sr3render/device.h"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                               \
    do {                                                                          \
        ++g_checks;                                                               \
        if (!(cond)) {                                                            \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":" << __LINE__   \
                      << "\n";                                                    \
            ++g_failures;                                                         \
        }                                                                         \
    } while (0)

// Creates a 1x1 solid-colour texture + SRV - deliberately the simplest
// possible synthetic "diffuse texture" a test can hand to drawLayer(): a
// single texel samples as that one colour everywhere regardless of UV or
// filtering, so these tests exercise this renderer's OWN quad placement/
// blend/sort logic, not any real texture's content. RGBA8 byte order here
// is arbitrary (not the BGRA order the composite sheet's own render target
// uses internally, see composite_renderer.cpp's readBack() comment) - the
// shader samples it as an ordinary Texture2D either way.
ID3D11ShaderResourceView* makeSolidColorTexture(ID3D11Device* device, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    const uint8_t pixel[4] = {r, g, b, a};
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 1;
    desc.Height = 1;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = pixel;
    init.SysMemPitch = 4;

    ID3D11Texture2D* tex = nullptr;
    if (FAILED(device->CreateTexture2D(&desc, &init, &tex))) return nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    HRESULT hr = device->CreateShaderResourceView(tex, nullptr, &srv);
    tex->Release();
    if (FAILED(hr)) return nullptr;
    return srv;
}

const uint8_t* pixelAt(const std::vector<uint8_t>& rgba, uint32_t sheetWidth, uint32_t x, uint32_t y) {
    return rgba.data() + (static_cast<size_t>(y) * sheetWidth + x) * 4;
}

// ===========================================================================
// 1a. OrderLayersForComposite - pure CPU, no device needed. Sec12.1e point
// 3's own confirmed sort: "sorts layers by +0x34 (Layer, lowest-first - a
// painter's-algorithm draw order)", stable on ties (this project's own
// choice where the available spec text does not give a tie-break rule -
// see OrderLayersForComposite's own header comment).
// ===========================================================================
void testOrderLayersForComposite() {
    std::vector<sr3render::CompositeLayerPlacement> layers(4);
    layers[0].layer = 5;
    layers[1].layer = 1;
    layers[2].layer = 3;
    layers[3].layer = 1; // ties with index 1 - must keep input order (stable_sort)

    const std::vector<size_t> order = sr3render::OrderLayersForComposite(layers);
    const std::vector<size_t> expected = {1, 3, 2, 0};
    CHECK(order == expected);
}

// ===========================================================================
// 3. Output sheet has the confirmed 2048x1024 dimensions (Sec12.1e point 2).
// ===========================================================================
void testSheetDimensions(sr3render::RenderDevice& dev) {
    sr3render::CompositeSheetRenderer sheet;
    std::string error;
    CHECK(sheet.initialise(dev.device(), error));

    CHECK(sr3render::kCompositeSheetWidth == 2048);
    CHECK(sr3render::kCompositeSheetHeight == 1024);
    CHECK(sheet.width() == 2048);
    CHECK(sheet.height() == 1024);

    sheet.begin(dev.context());
    std::vector<uint8_t> rgba;
    CHECK(sheet.readBack(dev.context(), rgba, error));
    CHECK(rgba.size() == static_cast<size_t>(2048) * 1024 * 4);
}

// ===========================================================================
// 2. Scaled positions landing where expected: a layer placed at a known
// (x,y,width,height) must paint its quad exactly there and nowhere else.
// ===========================================================================
void testPlacementLandsAtGivenCoordinates(sr3render::RenderDevice& dev) {
    ID3D11ShaderResourceView* red = makeSolidColorTexture(dev.device(), 255, 0, 0, 255);
    CHECK(red != nullptr);
    if (red == nullptr) return;

    sr3render::CompositeLayerPlacement layer;
    layer.name = "test_layer";
    layer.layer = 0;
    layer.x = 100.0f;
    layer.y = 200.0f;
    layer.width = 50.0f;
    layer.height = 60.0f;
    layer.alpha = 1.0f;
    layer.srv = red;

    sr3render::CompositeSheetRenderer sheet;
    std::string error;
    size_t skipped = 1; // poison, should be overwritten with 0
    CHECK(sr3render::BuildCharacterSkinComposite(dev.device(), dev.context(), {layer}, sheet, error, &skipped));
    CHECK(skipped == 0);

    std::vector<uint8_t> rgba;
    CHECK(sheet.readBack(dev.context(), rgba, error));

    // Centre of the quad (125,230) - well inside [100,150) x [200,260).
    // Opaque (alpha=1) red drawn "over" a transparent-black clear: result
    // is exactly opaque red, not an approximation, so no tolerance needed.
    const uint8_t* centre = pixelAt(rgba, sheet.width(), 125, 230);
    CHECK(centre[0] == 255 && centre[1] == 0 && centre[2] == 0 && centre[3] == 255);

    // Comfortably outside the quad on all four sides - must stay the
    // transparent-black clear colour (begin()'s own default), proving the
    // quad did NOT paint somewhere wider/narrower/offset from what was asked.
    const uint32_t outsideX[] = {20, 125, 125, 400};
    const uint32_t outsideY[] = {230, 20, 500, 230};
    for (int i = 0; i < 4; ++i) {
        const uint8_t* p = pixelAt(rgba, sheet.width(), outsideX[i], outsideY[i]);
        CHECK(p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 0);
    }

    red->Release();
}

// ===========================================================================
// 1b. Draw order correctness, at the pixel level: two fully-opaque,
// fully-overlapping quads with DIFFERENT Layer values, handed to
// BuildCharacterSkinComposite in deliberately-wrong (higher-Layer-first)
// input order. Sec12.1e point 3's confirmed sort (lowest Layer first) must
// still draw the Layer=1 quad UNDER the Layer=5 quad - so the visible
// result must be the Layer=5 colour, never the Layer=1 one. If the sort
// were missing, reversed, or merely "whatever order the caller passed in",
// this test would see the Layer=1 colour instead and fail.
// ===========================================================================
void testDrawOrderPaintsHigherLayerOnTop(sr3render::RenderDevice& dev) {
    ID3D11ShaderResourceView* red = makeSolidColorTexture(dev.device(), 255, 0, 0, 255);
    ID3D11ShaderResourceView* blue = makeSolidColorTexture(dev.device(), 0, 0, 255, 255);
    CHECK(red != nullptr && blue != nullptr);
    if (red == nullptr || blue == nullptr) return;

    sr3render::CompositeLayerPlacement higher; // Layer=5: must end up ON TOP (painted last)
    higher.name = "higher_layer";
    higher.layer = 5;
    higher.x = higher.y = 300.0f;
    higher.width = higher.height = 80.0f;
    higher.alpha = 1.0f;
    higher.srv = blue;

    sr3render::CompositeLayerPlacement lower; // Layer=1: must end up UNDERNEATH (painted first)
    lower.name = "lower_layer";
    lower.layer = 1;
    lower.x = lower.y = 300.0f;
    lower.width = lower.height = 80.0f;
    lower.alpha = 1.0f;
    lower.srv = red;

    // "Wrong" input order on purpose - see this function's own top comment.
    const std::vector<sr3render::CompositeLayerPlacement> layers = {higher, lower};

    sr3render::CompositeSheetRenderer sheet;
    std::string error;
    size_t skipped = 1;
    CHECK(sr3render::BuildCharacterSkinComposite(dev.device(), dev.context(), layers, sheet, error, &skipped));
    CHECK(skipped == 0);

    std::vector<uint8_t> rgba;
    CHECK(sheet.readBack(dev.context(), rgba, error));

    const uint8_t* centre = pixelAt(rgba, sheet.width(), 340, 340);
    CHECK(centre[0] == 0 && centre[1] == 0 && centre[2] == 255 && centre[3] == 255); // blue, opaque - NOT red

    red->Release();
    blue->Release();
}

} // namespace

int main() {
    try {
        testOrderLayersForComposite();

        sr3render::RenderDevice dev;
        std::string error;
        const bool deviceOk = dev.initialise(64, 64, error);
        CHECK(deviceOk);
        if (!deviceOk) {
            std::cerr << "RenderDevice::initialise failed: " << error << " - remaining (GPU-backed) checks skipped\n";
        } else {
            std::cout << "device: " << (dev.kind() == sr3render::DeviceKind::Hardware ? "HARDWARE" : "WARP")
                       << (dev.adapterName().empty() ? "" : (" (" + dev.adapterName() + ")")) << "\n";
            testSheetDimensions(dev);
            testPlacementLandsAtGivenCoordinates(dev);
            testDrawOrderPaintsHigherLayerOnTop(dev);
        }
    } catch (const std::exception& e) {
        std::cerr << "unexpected exception: " << e.what() << "\n";
        return 1;
    }
    if (g_failures) {
        std::cerr << g_failures << " of " << g_checks << " check(s) failed\n";
        return 1;
    }
    std::cout << g_checks << " tests passed.\n";
    return 0;
}
