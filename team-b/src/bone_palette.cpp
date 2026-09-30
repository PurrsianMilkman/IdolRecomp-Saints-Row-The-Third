#include "sr3rig/bone_palette.h"

#include <map>
#include <string>
#include <utility>

namespace sr3rig {

std::vector<sr3mesh::Vertex> remapBlendIndicesThroughPalette(
    const std::vector<sr3mesh::Vertex>& verts,
    const std::vector<uint8_t>& palette,
    size_t rigBoneCount,
    BlendIndexRemapStats* stats) {
    std::vector<sr3mesh::Vertex> out = verts;
    for (sr3mesh::Vertex& v : out) {
        if (stats != nullptr) ++stats->verticesProcessed;
        for (size_t k = 0; k < 4; ++k) {
            const uint8_t bi = v.blendIndices[k];
            if (bi == 255) continue;
            if (bi >= palette.size()) {
                v.blendIndices[k] = 255;
                v.blendWeights[k] = 0.0f;
                if (stats != nullptr) ++stats->lanesDroppedPaletteOutOfRange;
                continue;
            }
            const uint8_t rigIndex = palette[bi];
            if (rigIndex >= rigBoneCount) {
                v.blendIndices[k] = 255;
                v.blendWeights[k] = 0.0f;
                if (stats != nullptr) ++stats->lanesDroppedRigOutOfRange;
                continue;
            }
            v.blendIndices[k] = rigIndex;
            if (stats != nullptr) ++stats->lanesRemapped;
        }
    }
    return out;
}

PaletteSetAssignment assignPaletteSetsPerVertex(const sr3mesh::MeshBlock& mesh,
                                                size_t vertexCount,
                                                int groupIndex) {
    PaletteSetAssignment out;
    const std::vector<sr3mesh::MeshBlock::BonePaletteSet>& sets = mesh.bonePaletteSets();
    if (sets.empty()) {
        out.problem = "this Mesh sub-block declares no bone-palette sets";
        return out;
    }
    if (!mesh.drawGroupsLocated()) {
        out.problem = "this Mesh sub-block's draw groups could not be located, so there is "
                      "nothing to attach a per-range palette set to";
        return out;
    }
    size_t flatRangeCount = 0;
    for (const auto& group : mesh.drawGroups()) flatRangeCount += group.size();
    const std::vector<uint32_t>& perRange = mesh.drawRangePaletteSets();
    if (perRange.size() != flatRangeCount) {
        out.problem = "this Mesh sub-block's per-draw-range palette-set array was not readable "
                      "(HANDOFF Sec9.63.10) - refusing to guess which set each range uses";
        return out;
    }
    if (groupIndex >= 0 && static_cast<size_t>(groupIndex) >= mesh.drawGroups().size()) {
        out.problem = "draw group index out of range";
        return out;
    }

    out.setOfVertex.assign(vertexCount, 0);
    std::vector<int> claimed(vertexCount, -1);
    size_t flat = 0;
    for (size_t g = 0; g < mesh.drawGroups().size(); ++g) {
        for (size_t r = 0; r < mesh.drawGroups()[g].size(); ++r, ++flat) {
            if (groupIndex >= 0 && g != static_cast<size_t>(groupIndex)) continue;
            const uint32_t set = perRange[flat];
            if (set >= sets.size()) {
                out.problem = "a draw range names a palette set that does not exist";
                return out;
            }
            // Only vertices used by a NON-DEGENERATE triangle are actually
            // drawn; a strip's stitch indices name neighbours it never
            // renders, and counting those would report conflicts that do not
            // exist (spec-vertex-format.md Sec8.1).
            for (uint32_t vi : mesh.triangleListForRange(mesh.drawGroups()[g][r])) {
                if (vi >= vertexCount) continue;
                if (claimed[vi] < 0) {
                    claimed[vi] = static_cast<int>(set);
                    out.setOfVertex[vi] = static_cast<uint8_t>(set);
                    ++out.verticesAssigned;
                } else if (static_cast<uint32_t>(claimed[vi]) != set) {
                    ++out.conflicts;
                }
            }
        }
    }
    out.verticesUnreached = static_cast<long long>(vertexCount) - out.verticesAssigned;
    if (out.conflicts != 0) {
        out.problem = "this mesh draws " + std::to_string(out.conflicts) +
                      " vertex use(s) from two draw ranges that name DIFFERENT bone-palette sets, "
                      "so one vertex array cannot carry one remap (HANDOFF Sec9.63.10: 1 of 318 "
                      "shipped meshes, `reynolds`, is like this)";
        return out;
    }
    out.usable = true;
    return out;
}

VertexDuplicationResult resolveVertexSetConflictsByDuplication(const sr3mesh::MeshBlock& mesh,
                                                                size_t vertexCount,
                                                                int groupIndex,
                                                                const std::vector<bool>* rangeMask) {
    VertexDuplicationResult out;
    const std::vector<sr3mesh::MeshBlock::BonePaletteSet>& sets = mesh.bonePaletteSets();
    if (sets.empty()) {
        out.problem = "this Mesh sub-block declares no bone-palette sets";
        return out;
    }
    if (!mesh.drawGroupsLocated()) {
        out.problem = "this Mesh sub-block's draw groups could not be located, so there is "
                      "nothing to attach a per-range palette set to";
        return out;
    }
    size_t flatRangeCount = 0;
    for (const auto& group : mesh.drawGroups()) flatRangeCount += group.size();
    const std::vector<uint32_t>& perRange = mesh.drawRangePaletteSets();
    if (perRange.size() != flatRangeCount) {
        out.problem = "this Mesh sub-block's per-draw-range palette-set array was not readable "
                      "(HANDOFF Sec9.63.10) - refusing to guess which set each range uses";
        return out;
    }
    if (groupIndex < 0 || static_cast<size_t>(groupIndex) >= mesh.drawGroups().size()) {
        out.problem = "draw group index out of range";
        return out;
    }

    // The same flat range index this file's OTHER function uses, so
    // perRange[flat] names the correct set for a range regardless of which
    // group it belongs to.
    size_t flatBase = 0;
    for (int g = 0; g < groupIndex; ++g) flatBase += mesh.drawGroups()[static_cast<size_t>(g)].size();

    out.setOfVertex.assign(vertexCount, 0);
    std::vector<int> claimed(vertexCount, -1);
    // (originalVertexIndex, set) -> duplicate GPU index, so a vertex
    // needed by the SAME set more than once (reynolds' own vertex 4496,
    // referenced by 4 triangle corners in one range) gets exactly one
    // duplicate, not one per use.
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> duplicateFor;

    const auto& group = mesh.drawGroups()[static_cast<size_t>(groupIndex)];
    if (rangeMask != nullptr && rangeMask->size() != group.size()) {
        out.problem = "rangeMask size (" + std::to_string(rangeMask->size()) +
                      ") does not match draw group " + std::to_string(groupIndex) +
                      "'s range count (" + std::to_string(group.size()) + ")";
        return out;
    }
    out.redirectPerRange.assign(group.size(), {});
    for (size_t r = 0; r < group.size(); ++r) {
        if (rangeMask != nullptr && !(*rangeMask)[r]) continue;
        const size_t flat = flatBase + r;
        const uint32_t set = perRange[flat];
        if (set >= sets.size()) {
            out.problem = "a draw range names a palette set that does not exist";
            return out;
        }
        for (uint32_t vi : mesh.triangleListForRange(group[r])) {
            if (vi >= vertexCount) continue;
            if (claimed[vi] < 0) {
                claimed[vi] = static_cast<int>(set);
                out.setOfVertex[vi] = static_cast<uint8_t>(set);
                continue;
            }
            if (static_cast<uint32_t>(claimed[vi]) == set) continue;

            // Conflict: this range needs vi resolved against `set`, but
            // vi's original slot already belongs to a different one.
            // Reuse an existing duplicate for (vi, set) if this exact
            // pairing was already created (by an earlier triangle in this
            // same range, or a different range wanting the same set).
            auto key = std::make_pair(vi, set);
            auto it = duplicateFor.find(key);
            uint32_t dupIndex;
            if (it != duplicateFor.end()) {
                dupIndex = it->second;
            } else {
                dupIndex = static_cast<uint32_t>(vertexCount + out.duplicateSourceIndex.size());
                out.duplicateSourceIndex.push_back(vi);
                out.duplicateSetOfVertex.push_back(static_cast<uint8_t>(set));
                duplicateFor.emplace(key, dupIndex);
                ++out.duplicatesCreated;
            }
            out.redirectPerRange[r][vi] = dupIndex;
        }
    }

    out.usable = true;
    return out;
}

std::vector<sr3mesh::Vertex> remapBlendIndicesThroughPaletteSets(
    const std::vector<sr3mesh::Vertex>& verts,
    const std::vector<uint8_t>& palette,
    const std::vector<sr3mesh::MeshBlock::BonePaletteSet>& sets,
    const std::vector<uint8_t>& setOfVertex,
    size_t rigBoneCount,
    BlendIndexRemapStats* stats) {
    std::vector<sr3mesh::Vertex> out = verts;
    for (size_t i = 0; i < out.size(); ++i) {
        sr3mesh::Vertex& v = out[i];
        if (stats != nullptr) ++stats->verticesProcessed;
        const size_t setIndex = i < setOfVertex.size() ? setOfVertex[i] : 0;
        const bool setOk = setIndex < sets.size();
        const size_t setStart = setOk ? sets[setIndex].start : 0;
        const size_t setCount = setOk ? sets[setIndex].count : 0;
        for (size_t k = 0; k < 4; ++k) {
            const uint8_t bi = v.blendIndices[k];
            if (bi == 255) continue;
            if (!setOk || bi >= setCount || setStart + bi >= palette.size()) {
                v.blendIndices[k] = 255;
                v.blendWeights[k] = 0.0f;
                if (stats != nullptr) ++stats->lanesDroppedPaletteOutOfRange;
                continue;
            }
            const uint8_t rigIndex = palette[setStart + bi];
            if (rigIndex >= rigBoneCount) {
                v.blendIndices[k] = 255;
                v.blendWeights[k] = 0.0f;
                if (stats != nullptr) ++stats->lanesDroppedRigOutOfRange;
                continue;
            }
            v.blendIndices[k] = rigIndex;
            if (stats != nullptr) ++stats->lanesRemapped;
        }
    }
    return out;
}

std::array<float, 3> rigToMeshSpaceInverted(const std::array<float, 3>& rigSpacePosition) {
    return {-rigSpacePosition[0], -rigSpacePosition[1], -rigSpacePosition[2]};
}

Mat3x4 meshSpaceInversionMatrix() {
    Rotation3 m = {{{-1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}}};
    return Mat3x4::fromRotation(m);
}

Mat3x4 conjugateToMeshSpaceInverted(const Mat3x4& skinRig) {
    // Written as the literal product M * skin * M rather than the
    // simplified (R, -t) so the test suite's hand-computed (R, -t)
    // expectation is an independent check on the algebra, not a restatement
    // of the code.
    Mat3x4 m = meshSpaceInversionMatrix();
    return multiply(multiply(m, skinRig), m);
}

} // namespace sr3rig
