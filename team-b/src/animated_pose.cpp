#include "sr3rig/animated_pose.h"

namespace sr3rig {

std::vector<Mat3x4> computeAnimatedSkinningMatrices(
    const std::vector<uint32_t>& parentIndices,
    const std::vector<std::array<float, 3>>& restPositions,
    const std::vector<Rotation3>* localRotations,
    const std::vector<std::array<float, 3>>* localTranslationDeltas) {
    if (localTranslationDeltas == nullptr) {
        // No animated translation at all - degrade to the untouched
        // stage-1 path exactly (same call, same result).
        return computeSkinningMatrices(parentIndices, restPositions, localRotations);
    }

    const size_t n = parentIndices.size();
    std::vector<std::array<float, 3>> cumDelta(n, std::array<float, 3>{0.0f, 0.0f, 0.0f});
    std::vector<std::array<float, 3>> restAnimated(n);

    for (size_t i = 0; i < n; ++i) {
        std::array<float, 3> delta = (i < localTranslationDeltas->size())
                                          ? (*localTranslationDeltas)[i]
                                          : std::array<float, 3>{0.0f, 0.0f, 0.0f};
        uint32_t parent = parentIndices[i];
        // Parent-before-child is the same precondition
        // computeSkinningMatrices() itself requires (and enforces below,
        // via that call) - cumDelta[parent] is already valid here under
        // that precondition.
        if (parent != kNoParent) {
            cumDelta[i] = {cumDelta[parent][0] + delta[0], cumDelta[parent][1] + delta[1],
                           cumDelta[parent][2] + delta[2]};
        } else {
            cumDelta[i] = delta;
        }
        restAnimated[i] = {restPositions[i][0] + cumDelta[i][0],
                           restPositions[i][1] + cumDelta[i][1],
                           restPositions[i][2] + cumDelta[i][2]};
    }

    std::vector<Mat3x4> skinWrong =
        computeSkinningMatrices(parentIndices, restAnimated, localRotations);

    std::vector<Mat3x4> skin(n);
    for (size_t i = 0; i < n; ++i) {
        skin[i] = multiply(skinWrong[i], Mat3x4::translation(cumDelta[i]));
    }
    return skin;
}

Mat3x4 meshSpaceConjugationMatrix() {
    // The linear part of rigToMeshSpace(): mesh = (rig.x, -rig.y, -rig.z).
    Rotation3 m = {{{1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}}};
    return Mat3x4::fromRotation(m);
}

Mat3x4 conjugateToMeshSpace(const Mat3x4& skinRig) {
    Mat3x4 m = meshSpaceConjugationMatrix();
    return multiply(multiply(m, skinRig), m);
}

Rotation3 quatToRotation3(float x, float y, float z, float w) {
    const float xx = x * x, yy = y * y, zz = z * z;
    const float xy = x * y, xz = x * z, yz = y * z;
    const float wx = w * x, wy = w * y, wz = w * z;
    Rotation3 r;
    r[0][0] = 1.0f - 2.0f * (yy + zz);
    r[0][1] = 2.0f * (xy - wz);
    r[0][2] = 2.0f * (xz + wy);
    r[1][0] = 2.0f * (xy + wz);
    r[1][1] = 1.0f - 2.0f * (xx + zz);
    r[1][2] = 2.0f * (yz - wx);
    r[2][0] = 2.0f * (xz - wy);
    r[2][1] = 2.0f * (yz + wx);
    r[2][2] = 1.0f - 2.0f * (xx + yy);
    return r;
}

} // namespace sr3rig
