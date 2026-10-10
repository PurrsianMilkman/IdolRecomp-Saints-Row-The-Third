// Sun/directional shadow mapping - THIS PROJECT'S OWN clean-room D3D11
// playtest renderer, NOT a reimplementation of the original engine's
// internal pass structure.
//
// WHY THIS EXISTS (spec-render-pipeline.md Sec23.6.4, "Direct Follow-Up Pass
// 2026-10-09"): the real game is CONFIRMED (disassembly, both chains traced
// to a real bind->render->unbind->release sequence) to run a genuine sun/
// cascade shadow-map GENERATION pass, separate from the already-confirmed
// per-point/spot-light shadow path - `0x00491160`, called unconditionally at
// the top of the real per-frame lighting driver, builds up to 4 data-driven
// cascade splits via a scene-bounds-fitting function (`0x00e90400`, not
// traced to completion) and renders each split into the real hardware depth
// target the spec calls SHADOW_DEPTH_TEXTURE/
// RL_RENDER_TEXTURE_SHADOW_DEPTH_TEXTURE. This file does NOT reproduce that
// internal structure - this task's own brief is explicit that the target is
// "an equivalent, real, working shadow-mapping feature for its own clean-
// room renderer", not a byte-for-byte port of the original's 4-cascade
// scene-bounds-fitting function (which was never even traced to completion
// on the spec side). Every simplification below is therefore a CHOSEN v1
// scope reduction, labelled as such, not a fidelity claim:
//
//   * ONE shadow map, not up to 4 cascade splits. A single orthographic
//     projection fit to a CHOSEN local region (see computeLightViewProjection()'s
//     own comment) stands in for the real engine's data-driven multi-split
//     cascade-fitting function (`0x00e90400`).
//   * The light direction reused here is `sr3render::ComputeChosenSunDirection()`
//     (tod_lighting.h) - this project's own already-established "which way is
//     the sun" source of truth, chosen specifically because
//     spec-tables-environment.md Sec18 is a genuine, substantiated spec
//     NEGATIVE ("no Sun_Angle/Sun_Direction-shaped field exists anywhere in
//     this table group") - there is no second, competing real source to
//     reconcile against, so reusing the existing CHOSEN formula rather than
//     inventing a second one is the correct call, not a shortcut.
//   * Hard-edged (single-tap) depth comparison in the lit pixel shader, not
//     PCF/soft shadows - see mesh_renderer.cpp's ps_main for where that
//     happens. A nice-to-have, not attempted this pass.
//   * Point/spot light shadows (the real engine's OTHER confirmed shadow-
//     generation chain, `0x00491ed0`/`0x00e8bbb0`) are explicitly out of
//     scope for this file - sun/cascade only, per this task's own dispatch.

#pragma once

#include <cstdint>
#include <string>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ID3D11DepthStencilView;
struct ID3D11ShaderResourceView;
struct ID3D11SamplerState;

namespace sr3render {

// Single owner of: the shadow-map depth texture + its DSV/SRV, the sun's
// light-space view*projection matrix, and the viewport swap needed to
// render into it. The actual per-object depth-only DRAW calls are issued by
// the caller through MeshRenderer::drawDepthOnly() (mesh_renderer.h) using
// the matrix this class computes - this class does not itself know how to
// draw a mesh (MeshRenderer already owns the vertex/index buffers and input
// layout for every renderable object in this project; duplicating that here
// would be the opposite of "one owner").
class SunShadowMap {
public:
    SunShadowMap() = default;
    ~SunShadowMap();
    SunShadowMap(const SunShadowMap&) = delete;
    SunShadowMap& operator=(const SunShadowMap&) = delete;

    // `size` is both width and height of the square shadow depth target
    // (2048 is this task's own chosen default - reasonable quality for a
    // single, non-cascaded map without being an unreasonable memory/fill-
    // rate cost). Texture is created DXGI_FORMAT_R32_TYPELESS so it can be
    // viewed BOTH as a real D32_FLOAT depth-stencil target (for the
    // generation pass) and as an R32_FLOAT shader-resource view (for the lit
    // pass's shadow sample) - the standard D3D11 dual-view technique for a
    // depth buffer that also needs to be read back as a texture.
    bool initialise(ID3D11Device* device, uint32_t size, std::string& error);

    // Builds this frame's light view*projection matrix (row-major, row-
    // vector convention - matches every other matrix in mesh_renderer.h/
    // .cpp). `sunDirection` is this project's own CHOSEN sun direction
    // (ComputeChosenSunDirection(), tod_lighting.h), read here as "direction
    // FROM a lit surface TOWARD the sun" (the same convention
    // mesh_renderer.cpp's ps_main already uses for its lambert term:
    // dot(normal, sunDirection)) - the light camera is placed on the +sunDirection
    // side of `focusCentre` and looks back along -sunDirection.
    //
    // CHOSEN v1 simplification (see this file's own banner): `focusCentre` +
    // `radius` describe ONE local bounding sphere (e.g. centred on the
    // player/vehicle/camera focus point) rather than the whole streamed-world
    // scene's AABB or the real engine's 4 data-driven cascade splits - a
    // single reasonable bounding volume, honestly labelled, not a fidelity
    // claim about matching the original engine's own cascade-fitting
    // function (`0x00e90400`, spec-render-pipeline.md Sec23.6.4, not traced
    // to completion there either).
    void computeLightViewProjection(const float sunDirection[3], const float focusCentre[3], float radius,
                                     float outLightViewProjection[16]) const;

    // Binds this object's own depth target with ZERO colour render targets
    // (the standard D3D11 depth-only technique - MeshRenderer::drawDepthOnly()
    // binds a null pixel shader to match), clears it to the far plane (1.0),
    // and sets a viewport matching the shadow map's own square size. Call
    // once per frame, before issuing any MeshRenderer::drawDepthOnly() calls
    // with the matrix computeLightViewProjection() returned.
    void beginDepthPass(ID3D11DeviceContext* context);

    // Restores a viewport sized `restoreWidth`x`restoreHeight` (the real
    // back buffer's own size) after the depth pass above. Does NOT itself
    // restore OMSetRenderTargets - the caller already does that every frame
    // for its own real back-buffer RTV/DSV (see runPlaytest()'s own existing
    // per-frame OMSetRenderTargets call), so duplicating it here would be an
    // extra, redundant bind rather than a correctness requirement.
    void endDepthPass(ID3D11DeviceContext* context, uint32_t restoreWidth, uint32_t restoreHeight);

    // The shadow depth texture, viewed as an R32_FLOAT texture, for the lit
    // pass's pixel shader to sample (see mesh_renderer.h's draw()/drawTextured()
    // `shadowMap` parameter). nullptr before a successful initialise().
    ID3D11ShaderResourceView* shaderResourceView() const { return srv_; }
    // A point-filtered, border-addressed sampler (border colour = far/1.0,
    // so a light-space UV that falls outside the shadow map's own coverage
    // reads as "not in shadow" rather than wrapping/clamping into unrelated
    // depth data) - owned here since it is purely a property of how THIS
    // texture should be sampled, not a per-mesh concern.
    ID3D11SamplerState* sampler() const { return sampler_; }

    uint32_t size() const { return size_; }

    // DEBUG ONLY (this task, 2026-10-09, sun-shadow-map out-of-frustum bug
    // follow-up): reads this object's own depth texture back to the CPU
    // (CopyResource to a STAGING texture + Map, the standard D3D11 readback
    // technique - never done on the hot per-frame path, only by an explicit
    // diagnostic call) and writes it out as a grayscale PNG (near=black,
    // far=white, linear remap of the raw [0,1] depth value - NOT
    // normalised to the data's own min/max, so a glance at the image's
    // own brightness is directly comparable across different calls/frames).
    // Exists because the lit pixel shader's own debug tint
    // (shadowDebugViz, mesh_renderer.cpp's ps_main) could only show
    // whether a SAMPLE was in range and what the occlusion TEST decided,
    // not what the shadow map itself actually recorded at each texel -
    // this is the other half of confirming/refuting a diagnosis, looking
    // directly at the data rather than at an inference from it (this
    // project's own standing measurement-discipline habit). Returns false
    // and sets `error` on any D3D11 failure; never throws.
    bool dumpDepthToPng(ID3D11Device* device, ID3D11DeviceContext* context, const std::string& path,
                         std::string& error) const;

private:
    void release();

    uint32_t size_ = 0;
    ID3D11Texture2D* texture_ = nullptr;
    ID3D11DepthStencilView* dsv_ = nullptr;
    ID3D11ShaderResourceView* srv_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr;
};

}  // namespace sr3render
