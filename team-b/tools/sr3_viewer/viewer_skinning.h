#pragma once

#include "viewer_platform.h"
#include "sr3mesh/mesh_block.h"
#include "sr3rig/animated_pose.h"
#include "sr3rig/bone_palette.h"
#include "sr3rig/pose.h"
#include "sr3rig/rig.h"

namespace sr3viewer {

// Stage 1b of animation playback (HANDOFF.md "RESUME HERE", 2026-09-12
// session, Sec9.53): wires the CPU hierarchical-skinning library
// (sr3rig/pose.h, stage 1 - CONFIRMED to reproduce validate_pose.cpp's
// existing confirmed skinning test to ~1e-7 on real data) into this live
// renderer, so a POSED character can actually be looked at.
//
// The pose itself is NOT from `.anim_pc` - that decode is a separate,
// later step, explicitly out of scope here. It is the exact same
// hand-authored synthetic test validate_pose.cpp already runs and has
// confirmed: pick one bone with a child from a fixed name list (upperarm/
// forearm/thigh/calf/shoulder, in that order), rotate it and its whole
// descendant subtree by a fixed angle about that bone's own rest position,
// leave every other bone identity. Reusing that construction (rather than
// inventing a new one) is what makes this render's numbers comparable to
// stage 1's already-confirmed figures instead of being a fresh, unverified
// guess.
//
// Renders TWO images through the same upload path: an all-identity BIND
// pose (should match the existing unposed `mesh` command's output) and the
// test POSE, both via the MeshRenderer::upload() override-positions
// parameter added for this stage - no shader/pipeline change, same
// position/uv/normal GpuVertex layout as every other command here.


// HANDOFF Sec9.63.9: which skinning convention `pose`/`animpose` use BY
// DEFAULT, auto-detected per mesh from its own bone-palette declaration
// rather than a caller-supplied flag. This is the promotion of Sec9.62/
// Sec9.63's bone-palette + (-x,-y,-z) reading from opt-in to the DEFAULT
// for the population where it is unambiguous (272/318 real mesh/rig
// pairs, single-set - Sec9.63.6). Meshes that declare no palette at all
// (bonePaletteDeclaredCount() == 0) are REFUSED since 2026-09-30
// (manager ruling after spec-vertex-format.md Sec9 step 7b was retracted in
// favour of spec-rig-format.md Sec11.15): no spec gives them a skinning
// rule. Before that they silently took the retracted direct-index +
// (x,-y,-z) reading, which --legacy-skinning still forces on request.
//
// EXTENDED BY HANDOFF Sec9.63.10 to the 46/318 MULTI-SET meshes, which
// Sec9.63.9 still refused because "which set does this draw range use"
// was OPEN. It is not open any more: the block carries the answer in an
// 8-byte record per draw range, right after the 20-byte range records
// (sr3mesh::MeshBlock::drawRangePaletteSets()). Nothing is guessed here -
// a multi-set mesh is skinned ONLY when that array was read complete and
// valid, and only when the per-vertex assignment it implies is
// unambiguous for the draw group being rendered. Anything else still
// refuses, exactly as before: 1 of the 318 (`reynolds`) genuinely shares
// drawn vertices between two sets and still gets no render.
enum class SkinningMode {
    kPaletteSingleSet, // palette + (-x,-y,-z): the promoted default
    kPaletteMultiSet,  // 2-3 palette sets, each draw range's set read from
                       // the block's own per-range field (HANDOFF
                       // Sec9.63.10). Same (-x,-y,-z) convention; the only
                       // difference is that a vertex's lane indexes its
                       // range's SET rather than the whole palette.
    kDirectLegacy,     // direct index + (x,-y,-z), ONLY when
                       // --legacy-skinning is passed: an explicit opt-in
                       // for debugging/comparison. NOT SPEC-BACKED:
                       // spec-vertex-format.md Sec9 step 7b retracted this
                       // reading 2026-09-30 (rig Sec11.15).
    kRefuseNoPalette,  // no palette declared: no spec gives a skinning rule
                       // for such a mesh (OPEN), so refuse. Manager ruling
                       // 2026-09-30; until then this fell back to the
                       // retracted reading above.
    kRefuseMultiSet,   // >1 set AND the per-range selector was not readable
                       // - still no correct reading, so still refuse
    kRefuseTruncated,  // palette declared but not fully readable - refuse
                       // rather than guess (no real file in the 318-mesh
                       // population hits this - Sec9.63.6 measured 318/318
                       // complete - kept as a safety net, not an observed case)
};


struct SkinningModeResult {
    SkinningMode mode = SkinningMode::kDirectLegacy;
    std::string refusalMessage; // set only for the kRefuse* modes
};

bool isSkinningRefusal(SkinningMode mode);

SkinningModeResult resolveSkinningMode(const sr3mesh::MeshBlock& mesh, bool legacyForced);

sr3rig::VertexDuplicationResult applyMultiSetSkinning(const sr3mesh::MeshBlock& mesh,
                                                      std::vector<sr3mesh::Vertex>& verts,
                                                      size_t rigBoneCount, size_t drawGroup,
                                                      sr3rig::BlendIndexRemapStats& remap);

const char* skinningModeLabel(SkinningMode mode);

} // namespace sr3viewer
