// Draws a decoded sr3mesh channel as indexed triangles.
//
// Scope is milestone 3: position + UV, one channel, a fixed pose. No
// skinning (the blend weights/indices are decoded but unused - that needs
// the .rig_pc bone palette), no normal mapping, no lighting model beyond a
// fixed headlight term to make silhouettes readable.
//
// TOPOLOGY IS THE CALLER'S PROBLEM, deliberately. Only ~62% of shipped
// blocks have an index count divisible by three (spec-vertex-format.md
// §8, open item 7), so triangle-list is NOT a safe default. This renderer
// draws a triangle list because that is the only topology currently
// evidenced, and the caller is expected to have checked
// MeshBlock::indexCountDivisibleByThree() first. Drawing a non-multiple-
// of-3 index buffer as a list would silently drop the tail and distort
// the mesh - which looks like a bad decode rather than a wrong assumption,
// and would send someone hunting the wrong bug.
//
// DRAW MODES are diagnostic, not cosmetic. A UV or checker visualisation
// makes a bad texcoord decode obvious as scrambled or wildly-tiled texel
// flow, where a flat-shaded render would hide it completely. They earned
// their keep already: the texcoord encoding was corrected mid-build from
// half-float to signed int16/1024, and these modes are how a regression
// there would be caught rather than shipped.

#pragma once

#include <cstdint>
#include <array>
#include <map>
#include <string>
#include <vector>

#include "sr3mesh/mesh_block.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Buffer;
struct ID3D11InputLayout;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11SamplerState;
struct ID3D11ShaderResourceView;
struct ID3D11DepthStencilView;
struct ID3D11DepthStencilState;
struct ID3D11RasterizerState;

namespace sr3render {

enum class MeshDrawMode {
    UvAsColour,  // raw UV in red/green - scrambled UVs are unmistakable
    Checker,     // procedural checker in UV space - reveals scale and wrap
    Textured,    // sample a real texture
    NormalAsColour,
    // One flat colour per draw range's material id, drawn as one indexed
    // call per range (spec-vertex-format.md Sec8.2). STALE UNTIL 2026-09-29
    // (rule 16): this used to say the materialId -> texture mapping was
    // OPEN (HANDOFF Sec9.18) - CLOSED same day as this comment was written,
    // Sec9.22/Sec9.24 ("blocker lifted"/"blocker closed, on screen"), and
    // `drawTextured()` further down THIS SAME FILE already implements real
    // per-material textures. Kept as a deliberately colour-only diagnostic
    // mode anyway (distinct from `drawTextured()`), since a colour-coded
    // partition is still a useful, unambiguous way to look at how the
    // buffer is split into material-carrying ranges.
    MaterialIdAsColour,
    // ONE caller-supplied flat colour for the WHOLE uploaded range, ignoring
    // material id entirely - see draw()'s new `tint` parameter. Added for
    // tools/sr3_viewer.cpp's `zone-composite` investigation command: when
    // compositing two different channels of the same real Mesh sub-block
    // into one frame (to test whether they are two layers of the same
    // surface or unrelated geometry), a single solid colour per CHANNEL
    // reads unambiguously as "one continuous surface" in a way per-material
    // colouring (MaterialIdAsColour, many small hues) would not - the two
    // schemes would otherwise be hard to tell apart at a glance when both
    // are multi-hued. Appended at the end of the enum so every existing
    // `static_cast<int>(mode)` call site for modes 0-4 is unaffected.
    FlatTint,
};

// A caller-supplied extension of upload()'s own decoded vertex array by
// DUPLICATE entries, plus the per-range index redirection that makes them
// reachable (HANDOFF Sec9.68 / Sec9.63.10 open item 4, `reynolds`). This
// exists for exactly one reason: a draw range's blend indices resolve
// through a per-vertex bone-palette SET (sr3rig::assignPaletteSetsPerVertex()),
// and a CPU vertex buffer can only carry one set's remap per vertex INDEX -
// which is wrong when two ranges in the SAME drawn group disagree about
// which set the SAME vertex index should use (1/318 shipped meshes,
// `reynolds`). Splitting the vertex - a second copy, reachable only from
// the range that needs the other set - is the fix; this struct is how the
// caller hands upload() both halves of it (which extra vertices to add,
// and which existing index references should point at them instead).
//
// `sourceIndex[i]` names which ORIGINAL decodeChannel() index the extra
// GPU vertex at slot `decodeChannel().size() + i` copies its normal/UV
// from - `overridePositions` (already existing) must then cover the
// EXTENDED count so the duplicate's own (differently-skinned) position is
// supplied the same way every other vertex's is.
//
// `redirectPerRange[r]` maps an original vertex index -> the GPU index
// (always >= decodeChannel().size()) that range `r`'s own expanded
// triangle indices should use instead, where `r` is that range's own
// 0-based position within `mesh.drawGroups()[drawGroup]` (the same
// iteration order upload() already uses to build subDraws_). A range with
// no entries here needs no redirection - true for every range on every
// mesh except the one this exists for.
struct VertexDuplication {
    std::vector<uint32_t> sourceIndex;
    std::vector<std::map<uint32_t, uint32_t>> redirectPerRange;
};

class MeshRenderer {
public:
    MeshRenderer() = default;
    ~MeshRenderer();
    MeshRenderer(const MeshRenderer&) = delete;
    MeshRenderer& operator=(const MeshRenderer&) = delete;

    bool initialise(ID3D11Device* device, std::string& error);

    // Uploads one channel's vertices and the block's index buffer.
    // `texcoordSet` selects which UV set when a layout carries several.
    // `drawGroup` selects which LOD group to upload. Group 0 is the
    // highest detail and is the whole mesh, not a fragment of it (spec
    // §8.2 - the groups are LOD steps, so a renderer picks exactly one).
    // Exposed so the other groups can be looked at, which is what turns
    // "these are LOD levels" from an inference into something checked.
    //
    // `overridePositions`, when non-null, replaces v.position per-vertex
    // BEFORE the GpuVertex array is built - same order as decodeChannel()'s
    // output, so index i here must correspond to the i-th sr3mesh::Vertex
    // decodeChannel(channelIndex) would produce. Normals/UVs still come
    // from the decoded vertex unchanged. This is how a CPU-posed mesh
    // (sr3rig::skinVertices() output) gets on screen without teaching this
    // renderer anything about bones or skinning itself - it stays "upload
    // whatever positions the caller computed". Must have exactly
    // decodeChannel(channelIndex)'s vertex count - UNLESS `duplication` is
    // also supplied, in which case it must have exactly
    // decodeChannel(channelIndex)'s count PLUS duplication->sourceIndex.size().
    // Any other size and upload() fails rather than silently truncating/
    // padding a mismatch. Bounding box (boundsMin()/boundsMax()) reflects
    // whichever positions were actually uploaded.
    //
    // `duplication`, when non-null (HANDOFF Sec9.68), extends the vertex
    // buffer by its own sourceIndex.size() extra vertices (attributes other
    // than position copied from the named original) and redirects specific
    // index-buffer references per its redirectPerRange - see
    // VertexDuplication's own comment. nullptr (every pre-existing call
    // site) is exactly the old code path: no extra vertices, no
    // redirection, byte-for-byte unchanged - this is the property to
    // verify, not assume, whenever this parameter's implementation changes.
    //
    // `rangeMask`, when non-null, restricts the ranges of
    // `mesh.drawGroups()[drawGroup]` this call includes to those at
    // position `r` where `(*rangeMask)[r]` is true - every other range is
    // skipped entirely (no triangles, no subDraws_ entry). Must be exactly
    // `mesh.drawGroups()[drawGroup].size()` long when supplied, or this
    // fails rather than guess an alignment. nullptr (every pre-existing
    // call site) means "every range in the group" - byte-for-byte the old
    // behaviour. Added for the multi-channel character meshes (`alien_e01`/
    // `alien_es01` - see sr3_viewer.cpp's MultiChannelMultiSetRender): a
    // draw group whose ranges span more than one vertex CHANNEL
    // (`sr3mesh::MeshBlock::DrawRange::submeshIndex`) cannot be uploaded by
    // one upload() call at all, since this class holds exactly one
    // channel's decoded vertices per call - the caller instead makes one
    // upload() call per channel, each masked to only that channel's own
    // ranges of the group, and draws every resulting MeshRenderer into the
    // same shared render target.
    bool upload(ID3D11Device* device, const sr3mesh::MeshBlock& mesh, size_t channelIndex,
                size_t texcoordSet, std::string& error, size_t drawGroup = 0,
                const std::vector<std::array<float, 3>>* overridePositions = nullptr,
                const VertexDuplication* duplication = nullptr,
                const std::vector<bool>* rangeMask = nullptr);

    // Creates a depth buffer sized to the target. Without depth, back
    // faces paint over front faces and the result is unreadable.
    bool createDepth(ID3D11Device* device, uint32_t width, uint32_t height, std::string& error);

    // `world`, when non-null, is a per-draw-call world/model transform
    // (row-major, row-vector convention - see buildWorldMatrix()) composed
    // with `viewProjection` CPU-side before upload to the shader's existing
    // single `viewProjection` constant-buffer field. This is a deliberate
    // choice, recorded here rather than left implicit: the alternative
    // (adding a second `world` field to the HLSL constant buffer and
    // multiplying on the GPU) would mean touching kShaderSource and the
    // Constants struct that every existing draw call already depends on,
    // for a renderer explicitly flagged as "already confirmed correct
    // elsewhere in this project" (HANDOFF §9's scene-composition task
    // brief). Composing on the CPU and reusing the existing single-matrix
    // contract touches nothing about how the shader consumes it, so it
    // cannot regress it. When `world` is null (every pre-existing call
    // site), this is exactly the old code path: the same pointer is copied
    // into the constant buffer with no multiply, so behaviour is provably
    // unchanged, not just expected to be.
    // `tint`, when mode == MeshDrawMode::FlatTint, is the one flat RGBA
    // colour drawn for the WHOLE uploaded range (single DrawIndexed call,
    // no per-material split) - see MeshDrawMode::FlatTint's own comment.
    // Ignored for every other mode (every pre-existing call site passes
    // neither `world` nor `tint`, and behaves exactly as before - this
    // parameter is purely additive). A null `tint` with mode == FlatTint
    // draws black (the constant buffer's zero-initialised default), not a
    // guessed colour.
    void draw(ID3D11DeviceContext* context, const float viewProjection[16], MeshDrawMode mode,
              ID3D11ShaderResourceView* texture, const float world[16] = nullptr,
              const float tint[4] = nullptr);

    // Textured draw with ONE TEXTURE PER MATERIAL, issuing a separate
    // indexed draw per draw range and binding that range's material
    // (spec-vertex-format.md §8.2). `perMaterial` is indexed by material id;
    // a null entry means that material's texture could not be resolved, and
    // such ranges are drawn in the flat material colour instead of with
    // some other material's texture. Substituting a neighbour's texture
    // would render a plausible, silently wrong character - the failure a
    // screenshot cannot catch (HANDOFF §9.18).
    // See draw()'s comment for what `world` does and why it is composed
    // CPU-side rather than added to the shader's constant buffer.
    void drawTextured(ID3D11DeviceContext* context, const float viewProjection[16],
                      const std::vector<ID3D11ShaderResourceView*>& perMaterial,
                      const float world[16] = nullptr);

    // Bounding box of the uploaded positions, for framing a camera.
    const float* boundsMin() const { return boundsMin_; }
    const float* boundsMax() const { return boundsMax_; }
    uint32_t indexCount() const { return indexCount_; }
    // Non-degenerate triangles actually emitted from the strip.
    uint32_t trianglesEmitted() const { return trianglesEmitted_; }

    // One entry per draw range of group 0, in range order, giving where
    // that range's expanded triangles sit in the index buffer.
    struct SubDraw {
        uint32_t startIndex = 0;
        uint32_t indexCount = 0;
        uint32_t materialId = 0;
    };
    const std::vector<SubDraw>& subDraws() const { return subDraws_; }

    // The colour MaterialIdAsColour assigns to a material id. Exposed so a
    // caller can print a legend matching what is on screen.
    static void materialIdColour(uint32_t materialId, float out[4]);

    // Uploads a skeleton as a line list: one segment per bone, from its
    // parent's rest position to its own. Bones are drawn straight from
    // MODEL-SPACE rest positions with no rotation composition. STALE UNTIL
    // 2026-09-29 (rule 16): this used to say the reason was an OPEN
    // rotation convention (spec-rig-format.md open item 1) - the real,
    // stronger reason (spec-rig-format.md §10 item 1, CLOSED 2026-09-11):
    // the bone record carries NO ORIENTATION OF ANY KIND at all, so there
    // is no convention to determine - bone rotation exists only in
    // `.anim_pc`. Positions still do not need it either way, so this
    // renderer's own behaviour was always correct; only the stated reason
    // was stale.
    bool uploadSkeleton(ID3D11Device* device, const std::vector<std::array<float, 3>>& positions,
                        const std::vector<uint32_t>& parents, std::string& error);
    // See draw()'s comment for what `world` does and why it is composed
    // CPU-side rather than added to the shader's constant buffer.
    void drawSkeleton(ID3D11DeviceContext* context, const float viewProjection[16],
                      const float world[16] = nullptr);

    ID3D11DepthStencilView* depthView() const { return depthView_; }
    void clearDepth(ID3D11DeviceContext* context);

private:
    void release();

    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11InputLayout* inputLayout_ = nullptr;
    ID3D11Buffer* vertexBuffer_ = nullptr;
    ID3D11Buffer* indexBuffer_ = nullptr;
    ID3D11Buffer* constantBuffer_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr;
    ID3D11DepthStencilState* depthState_ = nullptr;
    ID3D11RasterizerState* rasterState_ = nullptr;
    ID3D11DepthStencilView* depthView_ = nullptr;
    uint32_t indexCount_ = 0;
    uint32_t trianglesEmitted_ = 0;
    std::vector<SubDraw> subDraws_;
    ID3D11Buffer* skeletonBuffer_ = nullptr;
    uint32_t skeletonVertexCount_ = 0;
    float boundsMin_[3] = {0, 0, 0};
    float boundsMax_[3] = {0, 0, 0};
};

// Builds a view-projection matrix framing `boundsMin`..`boundsMax` from a
// given yaw/pitch, row-major, ready for the shader. Split out so the
// camera can be reasoned about independently of the renderer.
void buildOrbitViewProjection(const float boundsMin[3], const float boundsMax[3], float yawRadians,
                              float pitchRadians, float distanceScale, float aspect,
                              float outMatrix[16]);

// ---------------------------------------------------------------------
// Scene composition / free-camera support (added for the multi-object
// scene command; see HANDOFF.md §9's engine/renderer-infrastructure task).
// Nothing above this point was changed in behaviour by this addition -
// draw()/drawTextured()/drawSkeleton()'s new `world` parameter defaults to
// null, which is the exact old code path, and buildOrbitViewProjection is
// untouched. These are new, additive functions only.
// ---------------------------------------------------------------------

// Composes two row-major 4x4 matrices under the row-vector convention this
// renderer's shader uses (mul(v, M)): outMatrix = a * b, so that
// transforming a row-vector by `a` and then by `b` equals transforming it
// once by `outMatrix` (v' = (v*a)*b = v*(a*b)). Safe to alias `outMatrix`
// with `a` or `b` - a temporary is used internally.
void multiplyMatrix4x4(const float a[16], const float b[16], float outMatrix[16]);

// Builds a world/model (object-placement) transform: uniform `scale`, then
// a yaw rotation about the world Y axis, then a translation to
// (x, y, z) - row-major, row-vector convention (matches
// buildOrbitViewProjection's view matrix and this header's other
// matrices). Deliberately only yaw + uniform scale + translation: the
// scene-composition task this exists for (HANDOFF §9) needs to place
// several already-upright character/vehicle/prop meshes at different
// positions in one frame, not a general transform hierarchy - adding
// pitch/roll/non-uniform scale here would be unused generality for that
// job, not a limitation of the approach.
void buildWorldMatrix(float x, float y, float z, float yawRadians, float scale,
                      float outMatrix[16]);

// A free-roaming (fly) camera's view-projection matrix: `eye` is the
// camera position given DIRECTLY (unlike buildOrbitViewProjection, which
// derives eye from orbiting a single object's own bounding box - there is
// no one set of bounds to orbit for a multi-object scene). `yawRadians`/
// `pitchRadians` orient the view the same way buildOrbitViewProjection's
// eye-from-centre offset is parameterised (yaw 0, pitch 0 looks down +Z).
// `nearZ`/`farZ` are explicit here because they cannot be derived from a
// single object's extent the way buildOrbitViewProjection derives them.
// Split out as its own function, like buildOrbitViewProjection, so the
// camera is reasoned about independently of the renderer; the single-
// object commands keep using buildOrbitViewProjection unchanged.
void buildFreeViewProjection(const float eye[3], float yawRadians, float pitchRadians,
                             float aspect, float nearZ, float farZ, float outMatrix[16]);

} // namespace sr3render
