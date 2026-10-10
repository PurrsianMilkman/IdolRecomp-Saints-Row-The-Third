// Character skin composite-bake renderer (spec-tables-customization.md
// Sec12.1e, CONFIRMED 2026-10-08 - commit f502db5 "Spec sync from Team A
// (orchestrator): tables-customization 12.1e: character skin composite
// bake CONFIRMED").
//
// WHAT THIS IS, per Sec12.1e's own four confirmed findings:
//   1. A real GPU-side compositing pipeline builds a shader material named
//      "skin_composite_material" (constructor 0x009FC110).
//   2. That constructor creates 4 dynamic render targets via a CreateTexture
//      wrapper (0x00487160): 2 at 1024x512/D3DFMT_A8R8G8B8 and 2 at
//      2048x1024/D3DFMT_X8R8G8B8 (format 22), each with a full mip chain
//      (D3DUSAGE_AUTOGENMIPMAP, 12 levels on Ex-style devices for the
//      2048x1024 pair). This module targets the LARGER (2048x1024,
//      X8R8G8B8-equivalent) pair - the one the task this module was built
//      for calls "the composite sheet". The mip chain itself is NOT
//      reproduced here (MipLevels=1): nothing this module does (build +
//      read back one frame for inspection/testing) needs mip levels, and
//      inventing an AUTOGENMIPMAP equivalent would be scope creep beyond
//      what Sec12.1e's own confirmed finding requires a reader to act on.
//   3. `Composite_Layer` rows' x_coord/y_coord/Alpha/Layer placement
//      fields DO have a confirmed reader (0x009FAD60 for position + sort
//      order, 0x009FB590 for alpha): layers are drawn in painter's-
//      algorithm order, sorted by Layer ascending (lowest first).
//   4. The composited result is registered through the same content-hash-
//      deduplicated material pool spec-tables-customization.md Sec12.1c
//      already documents (0x00E5E240 -> 0x00E3FCB0).
//
// Sec12.1f's FOLLOW-UP (same spec file) FULLY CLOSED every question
// Sec12.1e left open, as of its 2026-10-08 continuation pass - nothing
// below is still being narrowed; all three points (Q1/Q2/Q3) are settled:
//   a. Q2 (the mesh-bind instruction) - CLOSED: THERE ISN'T ONE, AND NEVER
//      WAS ONE TO FIND. `DAT_026530F0` (the scratch global 0x009FC110's
//      constructor caches the composite material handle into) has exactly
//      one reader, inside `FUN_009FAD60`: a `PUSH <handle> -> CALL
//      FUN_00E3EBB0 (set per-thread "current material override") -> one
//      virtual call (the compositing quad's OWN draw) -> PUSH 0xFFFF ->
//      CALL FUN_00E3EBB0 (restore the sentinel)` sequence. `FUN_00E3EBB0`/
//      its getter `FUN_00E3EBE0` are generic, engine-wide save/override/
//      draw-one-thing/restore infrastructure (18 callers, several unrelated
//      effects) - NOT character- or mesh-specific in any way. The composite
//      material is bound ONLY for the duration of this one internal draw
//      call; it is never attached to any character, mesh, or render-entity
//      structure. This is why no "bind instruction" was ever found across
//      three separate passes: none exists. See
//      BindCompositeResultToCharacterMesh() below - kept as a deliberately
//      unwired, now-CONFIRMED-unreachable stub (not a placeholder awaiting
//      a future finding).
//   b. Q1 (singleton vs. per-instance) - CONFIRMED: the three owning
//      globals (DAT_02653340/DAT_026532C0/DAT_02653300) ARE genuine C++
//      static-duration singletons (full MSVC static-init idiom traced),
//      fed each frame by a 2-entry "PCR" (player-character-record) slot
//      pool (stride 0x270, exactly 2 slots - the engine's 2-player split-
//      screen co-op cap), not a flat per-character-instance index. This
//      pipeline is CONFIRMED scoped to PLAYER characters specifically (one
//      activation call site traces to "mm_m1_5", the game's own
//      character-creation mission step) - NOT a per-NPC-instance mechanism.
//   c. Q3 (NPC skinning, e.g. Brad) - CLOSED, both halves, and this
//      pipeline is CONFIRMED UNRELATED to it: the `CreateTexture` wrapper
//      this pipeline uses (0x00487160) has exactly 2 callers in the whole
//      binary, neither NPC-keyed (Q3(b)). Direct inspection of the shipped
//      `characters.vpp_pc` archive (543 characters, 1066 `.cpeg_pc` files,
//      Q3(a)) confirms NPCs - Brad (`law_natguard_brad.str2_pc`) included -
//      use individually-named, pre-baked, shipped per-part textures (e.g.
//      `brad_sm_head_d.tga`, `brad_lg_upper_d.tga`) bound through the same
//      ordinary by-name material mechanism Sec12.1c already documents, NOT
//      this compositing pipeline: an exhaustive scan of all 1066 texture
//      files in that archive finds NO 2048x1024 texture anywhere (the
//      largest shipped character texture is 1024x1024). THIS MODULE IS
//      THEREFORE NOT, AND CANNOT BE, A PATH TOWARD BRAD'S OR ANY OTHER
//      NPC'S FACE - that is a separate, already-settled, unrelated
//      mechanism (a per-mesh `.matlib_pc` material record), not something
//      this renderer feeds into under any circumstance.
// No OPEN items remain in Sec12.1f's own scope - nothing here is a Team A
// spec-sync question anymore; cite Sec12.1f directly if re-deriving any of
// this elsewhere.
//
// WHAT THIS MODULE DOES NOT INVENT (read before changing any constant
// below): Sec12.1e confirms x_coord/y_coord are SCALED before being
// written to the vertex-position array, but the spec text available to
// this project does not give the numeric scale factor anywhere - no
// ratio, divisor or literal constant is stated. kOpenPlacementScale below
// is therefore an IDENTITY placeholder (1.0), not a re-derived value - see
// its own comment. Likewise, customization_compositing.xtbl's own
// Composite_Layer schema (include/sr3tables_customization/materials.h,
// confirmed exhaustive per spec-tables-customization.md Sec9.3) has NO
// Width/Height field for a layer's quad - the only non-invented source
// for a quad's pixel extent is therefore its own resolved diffuse
// texture's native size (CompositeLayerPlacement::width/height below),
// which this module takes as an input rather than computing from xtbl
// data that does not exist.
//
// REUSE, NOT A PARALLEL RENDERER: this module builds on
// sr3render::RenderDevice (device/context lifecycle - device.h) and
// sr3render::writePng (png_writer.h) exactly like every other sr3render
// milestone; texture resolution is the caller's job via this project's
// existing by-name PEG/texture-registry lookup
// (findAndUploadTexture()/TextureSearchCache, copied verbatim across
// tools/sr3_viewer.cpp / tools/prototype_lit_clmesh_tower.cpp /
// tools/prototype_lit_clmesh_tower2.cpp - see tools/skin_composite_render.cpp,
// which copies the same verbatim mechanism rather than inventing a new one).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;
struct ID3D11Buffer;
struct ID3D11SamplerState;
struct ID3D11BlendState;

namespace sr3render {

// The confirmed sheet dimensions/format (Sec12.1e point 2, the 2048x1024
// pair - this module targets the larger of the two confirmed render-target
// pairs, see this header's own top comment).
inline constexpr uint32_t kCompositeSheetWidth = 2048;
inline constexpr uint32_t kCompositeSheetHeight = 1024;

// §12.1e point 3: "0x009FAD60 reads +0x28/+0x2C (x_coord/y_coord), SCALES
// them, and writes the result into a per-draw vertex-position array". The
// available spec text confirms a scale happens but gives no numeric
// factor anywhere - no ratio, divisor or literal constant is stated. This
// is an IDENTITY placeholder (raw x_coord/y_coord used directly as
// sheet-pixel coordinates), not a re-derived value, and MUST NOT be cited
// as a Sec12.1e-confirmed number by any caller. It is a named constant
// (rather than a silent "* 1.0f" inline) purely so a real factor can be
// dropped in later, the moment Team A traces it, without touching every
// call site.
inline constexpr float kOpenPlacementScale = 1.0f;

// One composite layer's resolved, placement-ready draw data - the bridge
// between a parsed sr3tables_customization::CompositeLayer row (see
// include/sr3tables_customization/materials.h) and this renderer. Building
// one of these is the CALLER's job (see tools/skin_composite_render.cpp):
// this struct deliberately holds no xtbl-parsing logic and no texture-
// resolution logic, matching this project's own established split (reader
// vs. renderer) for every other sr3render consumer.
struct CompositeLayerPlacement {
    std::string name;    // CompositeLayer::name - diagnostics only, not used for placement or sorting
    int32_t layer = 0;    // CompositeLayer::layer - the CONFIRMED painter's-algorithm sort key (Sec12.1e point 3)
    float x = 0.0f, y = 0.0f;         // CompositeLayer::xCoord/yCoord * kOpenPlacementScale (sheet-pixel top-left corner)
    float width = 0.0f, height = 0.0f; // the RESOLVED diffuse texture's own native pixel size - see this header's
                                        // top comment on why there is no xtbl-authored size field to use instead
    float alpha = 1.0f;   // CompositeLayer::alpha (0..1) - CONFIRMED per-vertex alpha byte (Sec12.1e point 3,
                           // sibling reader 0x009FB590); this project's xtbl float grammar (sr3xtbl::ParseFloat)
                           // has no 0..255 convention for any other "Alpha"-named field anywhere else in this
                           // codebase, so 0..1 is used directly, not rescaled
    ID3D11ShaderResourceView* srv = nullptr; // caller-resolved texture (NOT owned by this struct or by
                                              // CompositeSheetRenderer - the caller's TextureSearchCache owns it)
};

// Sorts `layers`' INDICES ascending by `.layer` (lowest first) - the
// CONFIRMED painter's-algorithm draw order (Sec12.1e point 3: "the same
// function sorts layers by +0x34 (Layer, lowest-first ...)"). std::stable_sort
// so two rows sharing the same Layer value keep their original (input)
// relative order - the available spec text does not say what the real
// engine's own tie-break rule is, and a stable sort is the one choice that
// does not invent one. Pure CPU logic, no GPU resource touched - callable
// and testable with no device at all.
std::vector<size_t> OrderLayersForComposite(const std::vector<CompositeLayerPlacement>& layers);

// Owns the offscreen composite sheet (render target + staging readback
// texture) plus the positioned-textured-quad pipeline that draws each
// layer into it. Does NOT own the ID3D11Device/DeviceContext it is given -
// same "not owned" convention as sr3render::SwapChain/MeshRenderer.
class CompositeSheetRenderer {
public:
    CompositeSheetRenderer() = default;
    ~CompositeSheetRenderer();
    CompositeSheetRenderer(const CompositeSheetRenderer&) = delete;
    CompositeSheetRenderer& operator=(const CompositeSheetRenderer&) = delete;

    // Creates the kCompositeSheetWidth x kCompositeSheetHeight render
    // target (DXGI_FORMAT_B8G8R8A8_UNORM - the direct D3D11 equivalent of
    // the confirmed D3DFMT_X8R8G8B8 target, see composite_renderer.cpp's
    // own comment for the precedent this mapping follows), its CPU-
    // readable staging copy, and the quad-draw pipeline (shaders, sampler,
    // blend state, dynamic vertex buffer). Returns false with `error` set
    // on failure.
    bool initialise(ID3D11Device* device, std::string& error);

    // Binds the sheet as the render target and clears it to `clearColour`
    // (RGBA8, default fully transparent black - a reasonable rendering
    // default for "no layer drawn here yet", NOT itself a Sec12.1e-
    // confirmed clear value). Call once before the first drawLayer().
    void begin(ID3D11DeviceContext* context, const uint8_t clearColour[4] = nullptr);

    // Draws one layer's quad at `layer.x,layer.y` sized `layer.width x
    // layer.height`, alpha-blended (standard SRC_ALPHA/INV_SRC_ALPHA "over"
    // compositing - see this module's .cpp for why that reading of "a
    // per-vertex alpha byte", not a literal D3D9 render-state dump, is
    // used) on top of whatever is already in the sheet. `layer.srv` must
    // be non-null (a layer with no resolved texture must be filtered out
    // by the caller before this point - this function does not silently
    // skip one, which would hide a real resolution failure as if it were
    // an authored "no layer here").
    void drawLayer(ID3D11DeviceContext* context, const CompositeLayerPlacement& layer);

    // Reads the composited sheet back to tightly-packed 8-bit RGBA (top
    // row first - the layout png_writer.h's writePng() expects). The
    // render target's own GPU format is BGRA memory order (B8G8R8A8_UNORM,
    // the X8R8G8B8-equivalent); this function swaps the R/B bytes during
    // readback purely so the OUTPUT buffer matches writePng()'s documented
    // RGBA convention - a presentation-only conversion for the PNG file,
    // not a change to the confirmed render-target format itself.
    bool readBack(ID3D11DeviceContext* context, std::vector<uint8_t>& rgbaOut, std::string& error);

    uint32_t width() const { return kCompositeSheetWidth; }
    uint32_t height() const { return kCompositeSheetHeight; }

    // The composited sheet as a shader resource - the "texture handle" the
    // still-OPEN BindCompositeResultToCharacterMesh() stub (below) would
    // take, once/if Team A traces the real bind instruction. Not sampled
    // by anything in this module itself today. Owned by this renderer;
    // valid only between initialise() and destruction/release.
    ID3D11ShaderResourceView* resultSrv() const { return resultSrv_; }

private:
    void release();

    ID3D11Texture2D* target_ = nullptr;
    ID3D11RenderTargetView* rtv_ = nullptr;
    ID3D11ShaderResourceView* resultSrv_ = nullptr;
    ID3D11Texture2D* staging_ = nullptr;

    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11InputLayout* inputLayout_ = nullptr;
    ID3D11Buffer* vertexBuffer_ = nullptr;
    // The vertex shader's whole cbuffer is just {toClip, pad} (see the
    // .cpp's kShaderSource) - a pure function of this renderer's own fixed
    // kCompositeSheetWidth/Height, so it is written ONCE in begin(), not
    // per drawLayer() call.
    ID3D11Buffer* constantBuffer_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr;
    ID3D11BlendState* blendState_ = nullptr;
};

// Convenience orchestration of the whole build (initialise + begin + sort
// + draw every layer in order) for callers (tools/skin_composite_render.cpp,
// tests) that just want the finished sheet and do not need the lower-level
// primitives above. `layers` need not be pre-sorted - this function calls
// OrderLayersForComposite() itself. Returns false with `error` set if
// `sheet.initialise()` fails; a layer with a null `srv` is skipped with a
// count reported via `skippedUnresolvedCount` rather than drawn as a blank
// quad (see drawLayer()'s own comment on why that distinction matters).
bool BuildCharacterSkinComposite(ID3D11Device* device, ID3D11DeviceContext* context,
                                 const std::vector<CompositeLayerPlacement>& layers,
                                 CompositeSheetRenderer& sheet, std::string& error,
                                 size_t* skippedUnresolvedCount = nullptr);

// ===========================================================================
// CONFIRMED-UNREACHABLE STUB - kept deliberately NOT WIRED and NOT
// implemented. This is no longer an open hook awaiting a future finding;
// Sec12.1f Q2 (2026-10-08 continuation pass) CLOSED the question this
// function was built against, with a negative answer: NO mesh-bind
// instruction exists ANYWHERE in the binary for this pipeline's output.
//
// Sec12.1e point 4/5 found a real, content-hash-deduplicated material pool
// registration for the composited result ("a concrete, structurally
// plausible candidate for what a render entity would bind to"). Sec12.1f
// Q2 traced that registration's own scratch-global cache (`DAT_026530F0`)
// to its sole reader and confirmed the composite material is pushed as a
// generic per-thread "current material override", used for exactly ONE
// internal draw call (the compositing quad's own draw), then popped back
// to the sentinel - the same save/override/draw-one-thing/restore idiom
// 18 unrelated call sites across the renderer already use for other
// effects. It is never attached to any character, mesh, or render-entity
// structure. Sec12.1f also CONFIRMS (Q1) this whole pipeline is a
// singleton scoped to PLAYER characters specifically (a 2-slot PCR pool,
// the engine's 2-player split-screen co-op cap) and CLOSES (Q3) that NPC
// skins (e.g. Brad's) come from an entirely separate, already-identified
// mechanism (individually-named pre-baked textures via each mesh's own
// `.matlib_pc`, confirmed directly against the shipped `characters.vpp_pc`
// archive - no 2048x1024 texture exists anywhere in it). See this header's
// own top comment for the full citation chain.
//
// THIS FUNCTION THEREFORE HAS NO REAL COUNTERPART TO WIRE TO, EVER, FOR
// ANY CHARACTER - PLAYER OR NPC - and is kept only as a documented dead
// end: a reader tempted to "finish" this renderer by binding its output to
// a mesh would be inventing a mechanism Sec12.1f affirmatively proved does
// not exist, which this project's clean-room "no invented fixes" policy
// forbids. It is NOT called from BuildCharacterSkinComposite() or from
// anywhere else in this codebase, and should stay that way.
// ===========================================================================
void BindCompositeResultToCharacterMesh(ID3D11ShaderResourceView* compositeResult,
                                        void* targetMeshOrMaterialHandle);

} // namespace sr3render
