// Bone-palette blend-index remap and the mesh-space convention that goes
// with it. HANDOFF.md Sec9.62 (2026-09-13).
//
// WHAT THIS IS. A `.ccmesh_pc` Mesh sub-block carries a BONE PALETTE
// (sr3mesh::MeshBlock::bonePalette()): a strictly ascending list of rig
// bone indices, one per blend-index value the block's vertices use. A
// vertex whose blend lane says `k` is skinned to rig bone `palette[k]`,
// NOT to rig bone `k`. Read directly from shipped files (brad, angel,
// brute_flamethrower: 56/54/53 entries, count == max blend index + 1,
// every entry a valid rig index), not inferred - see Sec9.62 for the
// byte-level evidence and the population sweep.
//
// WHY IT MATTERS. Every earlier pose/skin result in this project read
// blend indices as direct rig indices. On the shared humanoid rig that
// reading happens to be nearly right for many bones (the palette is the
// rig order with a handful of helper bones removed) and exactly wrong for
// the rest - the palette skips `pelvis`, so from `spine` onward every
// slot is off by one, and each l-/r- pair lands on its mirror. The
// "near-degenerate cross-limb seams" of HANDOFF Sec9.56.1-Sec9.56.4
// (`l-finger1` vertices 4-19mm from `r-hand` vertices) are, under the
// palette, adjacent fingers of the SAME hand.
//
// THE COORDINATE CONSEQUENCE. With the palette applied, the vertices of
// every `l-` bone sit at NEGATIVE mesh-space X while the rig puts `l-`
// bones at POSITIVE rig-space X. So rig -> mesh is (-x, -y, -z), a point
// inversion, not Sec9.13's (x, -y, -z). Sec9.13's fit could not see this:
// with direct indices, each l-/r- slot was already reading its mirror
// bone, and the two sign errors cancelled for exactly the bones that
// could have discriminated. rigToMeshSpaceInverted() and
// conjugateToMeshSpaceInverted() are the (-x,-y,-z) counterparts of
// pose.h's rigToMeshSpace() and animated_pose.h's conjugateToMeshSpace().
// Since the inversion M = -I commutes with everything, M*R*M == R for any
// rotation and M*T(t)*M == T(-t): a rig-space skinning matrix (R, t)
// becomes (R, -t) in mesh space. Hand-checked in
// tests/synthetic_bone_palette_test.cpp.
//
// DEFAULT SINCE HANDOFF Sec9.63.9 (2026-09-13), for single-set meshes
// only. This file's own functions are unchanged - nothing in pose.h /
// animated_pose.h changes either, and this header still does not decide
// anything by itself. What changed is which path `sr3_viewer pose` /
// `animpose` reach WITHOUT a flag: they now call resolveSkinningMode()
// (tools/sr3_viewer.cpp) to auto-detect single-set vs. multi-set from the
// mesh's OWN bonePalette()/bonePaletteSets() declaration and use this
// reading automatically for the 272/318 single-set population (Sec9.63.6).
// STALE UNTIL 2026-09-29 (found and fixed then): this comment used to say
// the 46/318 multi-set meshes "still refuse... Sec9.63.7's per-draw-range
// set selector is still OPEN." That was closed the SAME DAY this comment
// was written, one section later: Sec9.63.10 found the real stored field
// (an 8-byte per-draw-range record, +0x00 = u32 palette-set index), and
// Sec9.68 (2026-09-14) closed the last exception (`reynolds`) via vertex
// duplication - "Nothing else pending on this item," Sec9.68's own words.
// All 46/46 multi-set meshes render correctly as of Sec9.68. This is
// rule 16's own worked example ("one field, one name, everywhere") -
// Sec9.63.10/Sec9.68 updated their OWN status lines but this header
// comment, one file over, sat unread and unfixed for two weeks.
// `--legacy-skinning` forces the pre-promotion direct-index + (x,-y,-z)
// reading on demand, for reproducing Sec9.56.x's historical numbers;
// `--bone-palette` is kept, now a no-op. `probe_seam_absolute_displacement
// --bone-palette` is a comparison/diagnostic tool, deliberately left
// opt-in on both sides so it can still produce the before/after pair.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "sr3mesh/mesh_block.h"
#include "sr3rig/pose.h"

namespace sr3rig {

struct BlendIndexRemapStats {
    long long verticesProcessed = 0;
    // Lanes carrying a live index (not 255) that were mapped through the
    // palette to a rig bone.
    long long lanesRemapped = 0;
    // Live lanes whose blend index was >= palette.size(): no palette slot
    // exists for them. Set to 255 with weight 0 in the output.
    long long lanesDroppedPaletteOutOfRange = 0;
    // Live lanes whose palette slot names a rig index >= rigBoneCount.
    // Same treatment. Zero on every real mesh checked; kept so a wrong
    // rig pairing is visible rather than silently skinned to nothing.
    long long lanesDroppedRigOutOfRange = 0;
};

// Returns a copy of `verts` whose blend indices are rig bone indices:
// out.blendIndices[k] = palette[in.blendIndices[k]]. Lane 255 (unused) is
// preserved. A lane that cannot be mapped (see the stats) becomes 255
// with weight 0, so skinVertexPosition()'s existing edge-case handling
// (drop-and-renormalize) applies unchanged downstream. Position, normal,
// tangent, weights and texcoords are copied through untouched.
std::vector<sr3mesh::Vertex> remapBlendIndicesThroughPalette(
    const std::vector<sr3mesh::Vertex>& verts,
    const std::vector<uint8_t>& palette,
    size_t rigBoneCount,
    BlendIndexRemapStats* stats = nullptr);

// --- MULTI-SET meshes (HANDOFF Sec9.63.10) -------------------------------
//
// 46/318 shipped character meshes split their palette into 2-3 SETS, and a
// draw range's blend index addresses ITS set's sub-list, not the whole
// palette. Which set is not guesswork any more: the Mesh sub-block carries
// an 8-byte record per draw range immediately after the 20-byte range
// records, whose u32 at +0x00 is the set index
// (sr3mesh::MeshBlock::drawRangePaletteSets()).
//
// A CPU skinning path holds ONE vertex array, so it can only apply one set
// per vertex. That is sound only if no drawn triangle in the group being
// rendered reaches a vertex from two ranges naming different sets.
// Measured over the 318-mesh population, counting only NON-DEGENERATE
// triangles (a strip's stitch indices name neighbours it never draws):
// clean on 317/318 for group 0 and 315/318 across all groups. The one
// exception, `reynolds`, really does share 6 drawn vertex-uses between two
// sets and CANNOT be skinned from a single vertex array - hence the
// `usable` flag rather than a silent best effort.

struct PaletteSetAssignment {
    // False means the caller must refuse, not fall back: `problem` says why.
    bool usable = false;
    std::string problem;
    // One set index per vertex; 0 for vertices no drawn triangle reaches
    // (their value is never used, but 0 keeps the array total).
    std::vector<uint8_t> setOfVertex;
    long long verticesAssigned = 0;
    long long verticesUnreached = 0;
    // Vertex uses where a second range named a DIFFERENT set. Non-zero is
    // exactly the condition that makes a single-array remap wrong.
    long long conflicts = 0;
};

// Builds the per-vertex set assignment from the block's own
// drawRangePaletteSets() field. `groupIndex` selects one draw group (LOD
// level) to consider, or -1 for all of them; a renderer draws one group, so
// it should pass that group and not be blocked by a conflict in an LOD it
// is not drawing. Only vertices used by non-degenerate triangles count.
PaletteSetAssignment assignPaletteSetsPerVertex(const sr3mesh::MeshBlock& mesh,
                                                size_t vertexCount,
                                                int groupIndex = -1);

// The per-set counterpart of remapBlendIndicesThroughPalette(): lane `k` of
// vertex `i` maps to rig bone palette[sets[setOfVertex[i]].start + k], and a
// lane whose index is >= that SET's count (not the whole palette's size) is
// dropped to 255/weight 0 and counted. With a single set {palette.size(), 0}
// and an all-zero assignment this is exactly
// remapBlendIndicesThroughPalette() - asserted in the synthetic suite rather
// than assumed, so the multi-set path cannot drift from the single-set one.
std::vector<sr3mesh::Vertex> remapBlendIndicesThroughPaletteSets(
    const std::vector<sr3mesh::Vertex>& verts,
    const std::vector<uint8_t>& palette,
    const std::vector<sr3mesh::MeshBlock::BonePaletteSet>& sets,
    const std::vector<uint8_t>& setOfVertex,
    size_t rigBoneCount,
    BlendIndexRemapStats* stats = nullptr);

// --- reynolds: resolving a genuine vertex/set conflict by duplication
// (HANDOFF Sec9.63.10 open item 4) -----------------------------------------
//
// assignPaletteSetsPerVertex() refuses outright when a drawn triangle in
// the group being rendered reaches a vertex INDEX from two ranges naming
// different sets - one vertex array can only carry one set's remap per
// index. 1/318 shipped meshes (`reynolds`) hits this: 3 vertex indices in
// draw group 0 are shared between two ranges naming different sets (6
// vertex-USES total, since one of the two ranges references some of them
// more than once).
//
// THE FIX IS A RENDERER ENGINEERING CHOICE, NOT A DECODE (stated in
// HANDOFF as the reason it was not done when the refusal was first
// shipped): duplicate the conflicting vertex so each range gets its own
// copy, resolved against its OWN correct set, and redirect that range's
// own triangle references to the duplicate. Nothing about what the file
// SAYS changes - drawRangePaletteSets(), bonePaletteSets() and the
// triangle lists are read exactly as elsewhere in this module. This is
// topology surgery on top of an unchanged decode, the same category of
// thing MeshRenderer::upload() already does when it expands a strip into
// a list.
struct VertexDuplicationResult {
    // False for exactly the same structural reasons
    // assignPaletteSetsPerVertex() refuses (no sets, draw groups not
    // located, selector array unreadable, an out-of-range set index) -
    // this function does not invent a looser contract, it only replaces
    // "refuse on conflict" with "resolve the conflict".
    bool usable = false;
    std::string problem;

    // Set assignment for the ORIGINAL vertexCount indices, first-claim-wins
    // in range order - identical in meaning and construction to
    // PaletteSetAssignment::setOfVertex, and IDENTICAL in value on every
    // mesh that has no conflict (verified in the synthetic suite: this
    // function and assignPaletteSetsPerVertex() must agree everywhere
    // they overlap).
    std::vector<uint8_t> setOfVertex;

    // One entry per duplicate GPU vertex created, beyond the original
    // vertexCount: duplicateSourceIndex[i] names which ORIGINAL vertex
    // index (whose position/normal/UV/blend data) GPU slot
    // `vertexCount + i` copies. Its OWN set assignment is
    // duplicateSetOfVertex[i] (the set the range that needed the
    // duplicate actually wanted).
    std::vector<uint32_t> duplicateSourceIndex;
    std::vector<uint8_t> duplicateSetOfVertex;

    // Per-range index redirection, indexed the SAME way
    // MeshRenderer::upload() iterates mesh.drawGroups()[groupIndex] (0-based
    // within that one group, not the flat across-all-groups range index).
    // redirectPerRange[r][originalVertexIndex] = the GPU index (always
    // >= vertexCount) that range r's own expanded triangle indices should
    // use instead. A range with no entries here needs no redirection at
    // all - true for every range on 317/318 meshes, and for every range
    // but one on `reynolds` itself.
    std::vector<std::map<uint32_t, uint32_t>> redirectPerRange;

    long long duplicatesCreated = 0;
};

// `groupIndex` selects the one draw group (LOD level) a renderer is about
// to draw - same convention as assignPaletteSetsPerVertex(), and for the
// same reason: a conflict in an LOD nothing is drawing should not block
// the one being drawn. `vertexCount` must be decodeChannel(channelIndex)'s
// own count for the channel the caller will skin - the duplicate GPU
// indices this returns are only meaningful paired with that same decode.
//
// `rangeMask`, when non-null, restricts consideration to only the ranges at
// position `r` within `mesh.drawGroups()[groupIndex]` where `(*rangeMask)[r]`
// is true; every other range in the group is treated as absent (it still
// gets an (empty) entry in `redirectPerRange`, at the same index, so a
// caller iterating ranges 0..group.size()-1 keeps using the same `r` this
// function already documents). Must be exactly `group.size()` long when
// supplied, or this refuses with `usable=false` rather than guess an
// alignment. nullptr (every pre-existing call site) means "every range in
// the group" - byte-for-byte the old behaviour.
//
// WHY THIS EXISTS (added for the multi-channel character meshes,
// `alien_e01`/`alien_es01` - HANDOFF Sec9.63.10 open item 3, closed as a
// DATA question by Sec9.89/Sec9.103/Sec9.104): on a mesh whose draw group
// spans MORE THAN ONE vertex channel (`DrawRange::submeshIndex` selects
// which - see mesh_block.h), two ranges from DIFFERENT channels can
// legitimately reuse the same small numeric local vertex index (each
// channel has its own index space starting near 0) - so resolving a
// group's palette-set conflicts/duplication must be scoped to ONE
// channel's own ranges at a time. Without a mask, a range belonging to a
// channel the caller is not even decoding would be walked anyway and its
// (unrelated) vertex indices folded into this channel's own claimed/
// duplicated set - not a hypothetical: this is the exact mechanism behind
// the "blend lanes don't fit their range's named set" symptom that used to
// make `sr3_viewer pose`/`animpose` refuse `alien_e01 --group 4` outright.
VertexDuplicationResult resolveVertexSetConflictsByDuplication(
    const sr3mesh::MeshBlock& mesh, size_t vertexCount, int groupIndex,
    const std::vector<bool>* rangeMask = nullptr);

// mesh = (-rig.x, -rig.y, -rig.z). The counterpart of pose.h's
// rigToMeshSpace() under the palette reading (header comment above).
std::array<float, 3> rigToMeshSpaceInverted(const std::array<float, 3>& rigSpacePosition);

// The linear part of rigToMeshSpaceInverted(): -I.
Mat3x4 meshSpaceInversionMatrix();

// Skin_i^mesh = M * Skin_i^rig * M with M = -I (M is its own inverse), the
// counterpart of animated_pose.h's conjugateToMeshSpace(). Reduces to
// (R, -t) for a rig-space (R, t) - see the header comment.
Mat3x4 conjugateToMeshSpaceInverted(const Mat3x4& skinRig);

} // namespace sr3rig
