#include "viewer_platform.h"
#include "viewer_skinning.h"
#include "sr3mesh/mesh_block.h"
#include "sr3rig/animated_pose.h"
#include "sr3rig/bone_palette.h"
#include "sr3rig/pose.h"
#include "sr3rig/rig.h"

using namespace sr3viewer;

namespace sr3viewer {

bool isSkinningRefusal(SkinningMode mode) {
    return mode == SkinningMode::kRefuseMultiSet || mode == SkinningMode::kRefuseTruncated ||
           mode == SkinningMode::kRefuseNoPalette;
}


SkinningModeResult resolveSkinningMode(const sr3mesh::MeshBlock& mesh, bool legacyForced) {
    SkinningModeResult result;
    if (legacyForced) {
        result.mode = SkinningMode::kDirectLegacy;
        return result;
    }
    const uint16_t declared = mesh.bonePaletteDeclaredCount();
    const std::vector<uint8_t>& palette = mesh.bonePalette();
    if (declared == 0 && palette.empty()) {
        result.mode = SkinningMode::kRefuseNoPalette;
        result.refusalMessage =
            "this mesh declares no bone palette: no spec-backed skinning for meshes without a "
            "palette (spec-vertex-format.md Sec9 step 7b / spec-rig-format.md Sec11.15; the old "
            "direct-index + (x,-y,-z) reading is retracted). Pass --legacy-skinning to force "
            "that retracted reading for debugging only.";
        return result;
    }
    if (palette.empty() || palette.size() != declared) {
        char buf[384];
        std::snprintf(buf, sizeof buf,
            "this block declares %u bone-palette entries but %zu were readable - refusing to "
            "guess (HANDOFF Sec9.63.9). Use --legacy-skinning to force the pre-promotion "
            "direct-index reading if you need it anyway.",
            static_cast<unsigned>(declared), palette.size());
        result.mode = SkinningMode::kRefuseTruncated;
        result.refusalMessage = buf;
        return result;
    }
    if (mesh.bonePaletteSets().size() != 1) {
        // HANDOFF Sec9.63.10: which set each draw range uses is no longer
        // OPEN - it is the u32 at +0x00 of the 8-byte record that follows
        // the 20-byte draw-range records, one per range. Take it only if
        // the reader actually read a complete, valid array; otherwise the
        // old refusal stands unchanged, because guessing a set for a
        // multi-set mesh is exactly the bug Sec9.63 fixed.
        size_t flatRangeCount = 0;
        for (const auto& group : mesh.drawGroups()) flatRangeCount += group.size();
        if (mesh.drawGroupsLocated() && flatRangeCount > 0 &&
            mesh.drawRangePaletteSets().size() == flatRangeCount) {
            result.mode = SkinningMode::kPaletteMultiSet;
            return result;
        }
        char buf[640];
        std::snprintf(buf, sizeof buf,
            "this block has %zu bone-palette SETS (header +0x48 declares %u) and its "
            "per-draw-range set selector (HANDOFF Sec9.63.10) could not be read: %zu draw "
            "ranges, %zu selector entries%s. Refusing to treat the concatenation as one "
            "palette. Use --legacy-skinning to force the pre-promotion direct-index reading "
            "if you need it anyway.",
            mesh.bonePaletteSets().size(), static_cast<unsigned>(mesh.bonePaletteSetCountDeclared()),
            flatRangeCount, mesh.drawRangePaletteSets().size(),
            mesh.drawGroupsLocated() ? "" : " (draw groups not located)");
        result.mode = SkinningMode::kRefuseMultiSet;
        result.refusalMessage = buf;
        return result;
    }
    result.mode = SkinningMode::kPaletteSingleSet;
    return result;
}


// HANDOFF Sec9.68: resolves a multi-set mesh's vertex/palette-set
// conflicts by DUPLICATION rather than refusing outright (Sec9.63.10 open
// item 4, `reynolds` - the one shipped mesh where two draw ranges in the
// same drawn group genuinely disagree about which set the SAME vertex
// index should use). `res.usable` is false, with `res.problem` set, for
// exactly the same structural reasons sr3rig::assignPaletteSetsPerVertex()
// already refuses on (no sets / draw groups not located / selector
// unreadable / an out-of-range set index) - this does not loosen that
// contract, it only stops treating a real conflict as one of them.
//
// On success this APPENDS any needed duplicate vertices to `verts` BEFORE
// remapping - so each duplicate's own blend indices get resolved against
// ITS OWN correct set, not copied post-remap from the original - and
// returns both the resolution (so the caller can build a
// sr3render::VertexDuplication from duplicateSourceIndex/redirectPerRange)
// and the remap stats every existing call site already prints.
sr3rig::VertexDuplicationResult applyMultiSetSkinning(const sr3mesh::MeshBlock& mesh,
                                                      std::vector<sr3mesh::Vertex>& verts,
                                                      size_t rigBoneCount, size_t drawGroup,
                                                      sr3rig::BlendIndexRemapStats& remap) {
    sr3rig::VertexDuplicationResult res = sr3rig::resolveVertexSetConflictsByDuplication(
        mesh, verts.size(), static_cast<int>(drawGroup));
    if (!res.usable) return res;
    std::vector<uint8_t> setOfVertex = res.setOfVertex;
    for (size_t i = 0; i < res.duplicateSourceIndex.size(); ++i) {
        verts.push_back(verts[res.duplicateSourceIndex[i]]);
        setOfVertex.push_back(res.duplicateSetOfVertex[i]);
    }
    verts = sr3rig::remapBlendIndicesThroughPaletteSets(verts, mesh.bonePalette(), mesh.bonePaletteSets(),
                                                        setOfVertex, rigBoneCount, &remap);
    return res;
}


const char* skinningModeLabel(SkinningMode mode) {
    switch (mode) {
        case SkinningMode::kPaletteSingleSet:
            return "bone-palette + (-x,-y,-z)  [DEFAULT since HANDOFF Sec9.63.9]";
        case SkinningMode::kPaletteMultiSet:
            return "bone-palette SETS, per draw range + (-x,-y,-z)  [HANDOFF Sec9.63.10]";
        case SkinningMode::kDirectLegacy:
            return "direct-index + (x,-y,-z)  [--legacy-skinning; RETRACTED in spec, debugging only]";
        default:
            return "REFUSED";
    }
}

} // namespace sr3viewer