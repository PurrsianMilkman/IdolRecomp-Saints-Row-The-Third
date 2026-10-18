#include "viewer_platform.h"
#include "viewer_multichannel.h"
#include "sr3render/device.h"
#include "sr3render/mesh_renderer.h"
#include "sr3render/png_writer.h"
#include "sr3render/quad_renderer.h"
#include "sr3render/sun_shadow_map.h"
#include "sr3render/swap_chain.h"
#include "sr3render/tod_clock.h"
#include "sr3render/tod_lighting.h"
#include "sr3render/window.h"
#include "sr3render/texture_upload.h"
#include "sr3mesh/mesh_block.h"
#include "sr3rig/animated_pose.h"
#include "sr3rig/bone_palette.h"
#include "sr3rig/pose.h"
#include "sr3rig/rig.h"

using namespace sr3viewer;

namespace {

// HANDOFF <multi-channel section, 2026-09-29>: renders a draw group whose
// ranges span MORE THAN ONE vertex channel - today the only known real
// case is `alien_e01`/`alien_es01` (HANDOFF Sec9.63.10 open item 3). The
// single-channel multi-set path above (applyMultiSetSkinning(), called
// FIRST and UNCONDITIONALLY by both runPose() and runAnimPose()) already
// detects exactly this case: a nonzero
// sr3rig::BlendIndexRemapStats::lanesDroppedPaletteOutOfRange after a
// correct per-range SET lookup means the range's blend indices don't fit
// the set its own material/context implies, because the range actually
// draws from a DIFFERENT vertex channel than the single one the caller
// uploaded (HANDOFF Sec9.89/Sec9.103/Sec9.104: DrawRange::submeshIndex IS
// the channel index - the DATA question those sections closed; this class
// is the CODE side of that closure, the "NOT YET DONE follow-up" both
// refusal messages used to cite).
//
// DESIGN CHOICE, stated up front because the task brief that produced this
// asked for it explicitly: sr3render::MeshRenderer is built around ONE
// upload() call producing ONE GPU vertex/index buffer pair for a whole
// draw group (its own header says so) - it has no concept of a group whose
// ranges span several source channels. Teaching it that directly would
// mean a second vertex-buffer slot, a per-SubDraw buffer selector, and
// touching draw()/drawTextured()'s inner loop - real surface area on a
// class every other command in this file also depends on (runMesh,
// runScene). Rather than do that, this class keeps MeshRenderer completely
// behaviourally unchanged (its public contract grows by one nullable
// `rangeMask` parameter on upload(), exercised only here - nullptr, every
// pre-existing call site, is byte-for-byte the old code path) and instead
// runs SEVERAL ordinary MeshRenderer instances, one per distinct channel
// the group's ranges actually use, each uploaded with ONLY that channel's
// own ranges, and draws them all into ONE shared render target + depth
// buffer so they composite as a single image - the same pattern this
// session's own tools/prototype_real_shader_draw_multishader.cpp already
// used for the equivalent vehicle problem (decode/upload each channel a
// group's ranges need, one GPU buffer per channel, one draw call per
// range against its own channel's buffer), adapted to go through
// MeshRenderer's existing API instead of bypassing it with raw D3D11 -
// this codebase's other renders all go through MeshRenderer, and nothing
// about this problem needs lower-level control than it already exposes.
// This was the SMALLER change: one new nullable parameter on one existing
// function, versus new internal state (multiple vertex buffers, a
// per-SubDraw channel selector) inside MeshRenderer itself.
//
// Per-channel palette-set resolution reuses the EXACT SAME sr3rig
// machinery applyMultiSetSkinning() already uses for the single-channel
// case (resolveVertexSetConflictsByDuplication() +
// remapBlendIndicesThroughPaletteSets()), scoped to one channel's own
// ranges at a time via that function's new `rangeMask` parameter - same
// additive, nullptr-default pattern as MeshRenderer::upload() above.
class MultiChannelMultiSetRender {
public:
    struct ChannelReport {
        size_t channelIndex = 0;
        size_t rangeCount = 0;
        size_t vertexCount = 0;
        long long duplicatesCreated = 0;
        long long lanesRemapped = 0;
        long long lanesDroppedPaletteOutOfRange = 0;
        long long lanesDroppedRigOutOfRange = 0;
    };

    // Builds one target per distinct DrawRange::submeshIndex value present
    // in mesh.drawGroups()[drawGroup], in ascending channel order (a fixed,
    // reproducible order, not encounter order). Returns false with `error`
    // set for exactly the same structural reasons the single-channel path
    // already refuses on (no sets / draw groups not located / selector
    // unreadable / an out-of-range set index / a genuine same-channel
    // vertex-set conflict) - applied once per channel rather than once
    // across the wrongly-mixed group, so one channel's problem does not
    // hide behind another's. Also refuses (this project's "no invented
    // fixes" discipline) if any channel STILL drops a blend lane once
    // scoped to its own ranges only - on the one population this was
    // checked against (alien_e01 itself, HANDOFF Sec9.103/Sec9.104's own
    // 28/28 self-check) that never happens, and rendering through an
    // unexplained drop anyway would be guessing, not fixing.
    bool build(ID3D11Device* device, const sr3mesh::MeshBlock& mesh, size_t drawGroup,
              size_t rigBoneCount, uint32_t width, uint32_t height, std::string& error) {
        channels_.clear();
        reports_.clear();
        mesh_ = &mesh;
        drawGroup_ = drawGroup;
        if (!mesh.drawGroupsLocated() || drawGroup >= mesh.drawGroups().size()) {
            error = "draw group not located or out of range";
            return false;
        }
        const auto& group = mesh.drawGroups()[drawGroup];
        if (group.empty()) {
            error = "draw group " + std::to_string(drawGroup) + " has no ranges";
            return false;
        }

        // Distinct channels this group's ranges actually use, ascending.
        std::vector<uint32_t> distinctChannels;
        for (const auto& r : group) {
            if (std::find(distinctChannels.begin(), distinctChannels.end(), r.submeshIndex) ==
                distinctChannels.end()) {
                distinctChannels.push_back(r.submeshIndex);
            }
        }
        std::sort(distinctChannels.begin(), distinctChannels.end());

        for (uint32_t c32 : distinctChannels) {
            const size_t c = static_cast<size_t>(c32);
            if (c >= mesh.channels().size()) {
                error = "a draw range names channel " + std::to_string(c) +
                        ", out of range for a " + std::to_string(mesh.channels().size()) +
                        "-channel mesh";
                return false;
            }
            ChannelTarget target;
            target.channelIndex = c;
            target.rangeMask.assign(group.size(), false);
            size_t rangeCount = 0;
            for (size_t r = 0; r < group.size(); ++r) {
                if (group[r].submeshIndex == c32) {
                    target.rangeMask[r] = true;
                    ++rangeCount;
                }
            }

            std::vector<sr3mesh::Vertex> verts;
            try {
                verts = mesh.decodeChannel(c);
            } catch (const std::exception& ex) {
                error = "decodeChannel(" + std::to_string(c) + ") failed: " + ex.what();
                return false;
            }
            if (verts.empty()) {
                error = "channel " + std::to_string(c) + " produced no vertices";
                return false;
            }

            sr3rig::VertexDuplicationResult dup = sr3rig::resolveVertexSetConflictsByDuplication(
                mesh, verts.size(), static_cast<int>(drawGroup), &target.rangeMask);
            if (!dup.usable) {
                error = "channel " + std::to_string(c) + ": " + dup.problem;
                return false;
            }
            std::vector<uint8_t> setOfVertex = dup.setOfVertex;
            for (size_t i = 0; i < dup.duplicateSourceIndex.size(); ++i) {
                verts.push_back(verts[dup.duplicateSourceIndex[i]]);
                setOfVertex.push_back(dup.duplicateSetOfVertex[i]);
            }

            sr3rig::BlendIndexRemapStats remap;
            verts = sr3rig::remapBlendIndicesThroughPaletteSets(
                verts, mesh.bonePalette(), mesh.bonePaletteSets(), setOfVertex, rigBoneCount, &remap);
            if (remap.lanesDroppedPaletteOutOfRange != 0) {
                error = "channel " + std::to_string(c) + ": " +
                        std::to_string(remap.lanesDroppedPaletteOutOfRange) +
                        " blend lane(s) still do not fit their range's own named set even "
                        "scoped to this channel's own ranges only - a genuinely new problem, "
                        "not the known cross-channel cause, refusing rather than guessing";
                return false;
            }

            target.renderer = std::make_unique<sr3render::MeshRenderer>();
            std::string rendererError;
            if (!target.renderer->initialise(device, rendererError)) {
                error = "channel " + std::to_string(c) + " renderer init failed: " + rendererError;
                return false;
            }
            // Shared depth buffer: only the FIRST channel's renderer owns
            // one (see depthView()/clearDepth() below) - every later
            // channel's draw() call relies on that SAME view being bound
            // externally by the caller.
            if (channels_.empty()) {
                if (!target.renderer->createDepth(device, width, height, rendererError)) {
                    error = "shared depth buffer failed: " + rendererError;
                    return false;
                }
            }

            ChannelReport rep;
            rep.channelIndex = c;
            rep.rangeCount = rangeCount;
            rep.vertexCount = verts.size();
            rep.duplicatesCreated = dup.duplicatesCreated;
            rep.lanesRemapped = remap.lanesRemapped;
            rep.lanesDroppedPaletteOutOfRange = remap.lanesDroppedPaletteOutOfRange;
            rep.lanesDroppedRigOutOfRange = remap.lanesDroppedRigOutOfRange;
            reports_.push_back(rep);

            if (dup.duplicatesCreated != 0) {
                target.duplication.sourceIndex = dup.duplicateSourceIndex;
                target.duplication.redirectPerRange = dup.redirectPerRange;
            }
            target.verts = std::move(verts);
            channels_.push_back(std::move(target));
        }
        return true;
    }

    // Skins every channel's own decoded vertices with `skin` (one Mat3x4
    // per rig bone - exactly what bindSkin/poseSkin/skinMesh already are
    // in runPose()/runAnimPose(), unchanged) and re-uploads every
    // channel's MeshRenderer. Accumulates the combined bounding box over
    // ALL channels into boundsMin/boundsMax, so a shared camera sees the
    // whole group, not just one channel's own piece of it.
    bool uploadFrame(ID3D11Device* device, const std::vector<sr3rig::Mat3x4>& skin,
                     std::string& error, float boundsMin[3], float boundsMax[3]) {
        boundsMin[0] = boundsMin[1] = boundsMin[2] = 1e30f;
        boundsMax[0] = boundsMax[1] = boundsMax[2] = -1e30f;
        for (auto& ch : channels_) {
            std::vector<std::array<float, 3>> positions = sr3rig::skinVertices(ch.verts, skin, nullptr);
            const sr3render::VertexDuplication* dupPtr =
                ch.duplication.sourceIndex.empty() ? nullptr : &ch.duplication;
            // texcoordSet is always 0 here, matching every pre-existing
            // pose/animpose call site in this file.
            if (!ch.renderer->upload(device, *mesh_, ch.channelIndex, 0, error, drawGroup_,
                                     &positions, dupPtr, &ch.rangeMask)) {
                error = "channel " + std::to_string(ch.channelIndex) + " upload failed: " + error;
                return false;
            }
            const float* mn = ch.renderer->boundsMin();
            const float* mx = ch.renderer->boundsMax();
            for (int c = 0; c < 3; ++c) {
                if (mn[c] < boundsMin[c]) boundsMin[c] = mn[c];
                if (mx[c] > boundsMax[c]) boundsMax[c] = mx[c];
            }
        }
        return true;
    }

    // Draws every channel's current upload into whatever render target is
    // currently bound - the caller binds the target + shared depth buffer
    // and clears both exactly once per output image, same convention every
    // other multi-draw-call render in this file already follows.
    void draw(ID3D11DeviceContext* context, const float viewProjection[16],
             sr3render::MeshDrawMode mode) {
        for (auto& ch : channels_) ch.renderer->draw(context, viewProjection, mode, nullptr);
    }

    void clearDepth(ID3D11DeviceContext* context) {
        if (!channels_.empty()) channels_.front().renderer->clearDepth(context);
    }

    ID3D11DepthStencilView* depthView() const {
        return channels_.empty() ? nullptr : channels_.front().renderer->depthView();
    }

    size_t channelCount() const { return channels_.size(); }
    const std::vector<ChannelReport>& reports() const { return reports_; }

private:
    struct ChannelTarget {
        size_t channelIndex = 0;
        std::unique_ptr<sr3render::MeshRenderer> renderer;
        std::vector<sr3mesh::Vertex> verts; // decoded + palette-remapped rest pose, incl. duplicates
        std::vector<bool> rangeMask;        // this channel's own ranges within the group
        sr3render::VertexDuplication duplication;
    };
    std::vector<ChannelTarget> channels_;
    std::vector<ChannelReport> reports_;
    const sr3mesh::MeshBlock* mesh_ = nullptr;
    size_t drawGroup_ = 0;
};

} // namespace

namespace sr3viewer {

// Shared driver for both runPose() and runAnimPose()'s multi-channel case:
// builds the per-channel renderer set once, then uploads/draws/reads back/
// writes one PNG per frame. `outPixels`, when non-null, collects each
// frame's raw RGBA8 buffer (so a caller can do its own pixel-diff, exactly
// as the single-channel paths already do for bind-vs-posed).
bool runMultiChannelFrames(sr3render::RenderDevice& device, const sr3mesh::MeshBlock& mesh,
                          size_t drawGroup, size_t rigBoneCount, uint32_t width, uint32_t height,
                          float yaw, float pitch, sr3render::MeshDrawMode mode,
                          const std::vector<MultiChannelFrame>& frames,
                          std::vector<std::vector<uint8_t>>* outPixels) {
    if (frames.empty()) return true;
    MultiChannelMultiSetRender render;
    std::string error;
    if (!render.build(device.device(), mesh, drawGroup, rigBoneCount, width, height, error)) {
        std::fprintf(stderr, "multi-channel setup failed: %s\n", error.c_str());
        return false;
    }
    std::printf("channels : %zu distinct vertex channel(s) drive draw group %zu\n",
               render.channelCount(), drawGroup);
    for (const auto& rep : render.reports()) {
        std::printf("    channel %zu: %zu range(s), %zu vertices (%lld duplicated); lanes "
                   "remapped=%lld dropped(no palette slot)=%lld dropped(rig out of range)=%lld\n",
                   rep.channelIndex, rep.rangeCount, rep.vertexCount, rep.duplicatesCreated,
                   rep.lanesRemapped, rep.lanesDroppedPaletteOutOfRange,
                   rep.lanesDroppedRigOutOfRange);
    }

    float viewProjection[16];
    bool haveCamera = false;
    for (const auto& frame : frames) {
        float boundsMin[3], boundsMax[3];
        if (!render.uploadFrame(device.device(), frame.skin, error, boundsMin, boundsMax)) {
            std::fprintf(stderr, "multi-channel upload failed: %s\n", error.c_str());
            return false;
        }
        if (!haveCamera) {
            sr3render::buildOrbitViewProjection(boundsMin, boundsMax, yaw, pitch, 1.9f,
                                                static_cast<float>(width) / static_cast<float>(height),
                                                viewProjection);
            std::printf("bounds(renderer, combined): %.3f x %.3f x %.3f\n",
                       static_cast<double>(boundsMax[0] - boundsMin[0]),
                       static_cast<double>(boundsMax[1] - boundsMin[1]),
                       static_cast<double>(boundsMax[2] - boundsMin[2]));
            haveCamera = true;
        }
        device.bindRenderTargetWithDepth(render.depthView());
        device.clear({28, 30, 38, 255});
        render.clearDepth(device.context());
        render.draw(device.context(), viewProjection, mode);
        std::vector<uint8_t> pixels;
        if (!device.readBack(pixels, error)) {
            std::fprintf(stderr, "readback failed: %s\n", error.c_str());
            return false;
        }
        if (!sr3render::writePng(frame.outPath, width, height, pixels, error)) {
            std::fprintf(stderr, "png write failed: %s\n", error.c_str());
            return false;
        }
        std::printf("wrote    : %s\n", frame.outPath.c_str());
        if (outPixels != nullptr) outPixels->push_back(std::move(pixels));
    }
    return true;
}

} // namespace sr3viewer