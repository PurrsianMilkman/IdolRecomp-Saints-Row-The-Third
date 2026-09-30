#include "sr3rig/pose.h"

#include <cmath>
#include <stdexcept>

namespace sr3rig {

Mat3x4 Mat3x4::identity() {
    Mat3x4 r;
    r.m[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    r.m[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    r.m[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    return r;
}

Mat3x4 Mat3x4::translation(const std::array<float, 3>& t) {
    Mat3x4 r = identity();
    r.m[0][3] = t[0];
    r.m[1][3] = t[1];
    r.m[2][3] = t[2];
    return r;
}

Mat3x4 Mat3x4::fromRotation(const std::array<std::array<float, 3>, 3>& rot) {
    Mat3x4 r;
    for (int row = 0; row < 3; ++row) {
        r.m[row][0] = rot[row][0];
        r.m[row][1] = rot[row][1];
        r.m[row][2] = rot[row][2];
        r.m[row][3] = 0.0f;
    }
    return r;
}

std::array<float, 3> Mat3x4::transformPoint(const std::array<float, 3>& p) const {
    return {m[0][0] * p[0] + m[0][1] * p[1] + m[0][2] * p[2] + m[0][3],
            m[1][0] * p[0] + m[1][1] * p[1] + m[1][2] * p[2] + m[1][3],
            m[2][0] * p[0] + m[2][1] * p[1] + m[2][2] * p[2] + m[2][3]};
}

Mat3x4 multiply(const Mat3x4& a, const Mat3x4& b) {
    // Treating both as 4x4 with an implicit [0 0 0 1] bottom row:
    //   C.M[r][c] = sum_k A.M[r][k] * B.M[k][c]     (k = 0..2)
    //   C.t[r]    = sum_k A.M[r][k] * B.t[k] + A.t[r]
    Mat3x4 c;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            c.m[row][col] = a.m[row][0] * b.m[0][col] + a.m[row][1] * b.m[1][col] +
                             a.m[row][2] * b.m[2][col];
        }
        c.m[row][3] = a.m[row][0] * b.m[0][3] + a.m[row][1] * b.m[1][3] +
                      a.m[row][2] * b.m[2][3] + a.m[row][3];
    }
    return c;
}

Rotation3 identityRotation3() {
    return {{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};
}

Rotation3 rotationFromAxisAngle(std::array<float, 3> axis, float angleRadians) {
    // Same formula, same operation order, as validate_pose.cpp's
    // axisAngle()/Mat3 - do not "clean up" the arithmetic here without
    // re-running that harness's comparison, since float non-associativity
    // means a reordering can shift the bit-exact match this is designed to
    // reproduce.
    float l = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    std::array<float, 3> ax{axis[0] / l, axis[1] / l, axis[2] / l};
    float c = std::cos(angleRadians);
    float s = std::sin(angleRadians);
    float t = 1.0f - c;
    Rotation3 r;
    r[0][0] = t * ax[0] * ax[0] + c;
    r[0][1] = t * ax[0] * ax[1] - s * ax[2];
    r[0][2] = t * ax[0] * ax[2] + s * ax[1];
    r[1][0] = t * ax[0] * ax[1] + s * ax[2];
    r[1][1] = t * ax[1] * ax[1] + c;
    r[1][2] = t * ax[1] * ax[2] - s * ax[0];
    r[2][0] = t * ax[0] * ax[2] - s * ax[1];
    r[2][1] = t * ax[1] * ax[2] + s * ax[0];
    r[2][2] = t * ax[2] * ax[2] + c;
    return r;
}

std::array<float, 3> rotate(const Rotation3& r, const std::array<float, 3>& v) {
    return {r[0][0] * v[0] + r[0][1] * v[1] + r[0][2] * v[2],
            r[1][0] * v[0] + r[1][1] * v[1] + r[1][2] * v[2],
            r[2][0] * v[0] + r[2][1] * v[1] + r[2][2] * v[2]};
}

std::array<float, 3> rigToMeshSpace(const std::array<float, 3>& rigSpacePosition) {
    return {rigSpacePosition[0], -rigSpacePosition[1], -rigSpacePosition[2]};
}

std::vector<Mat3x4> computeSkinningMatrices(
    const std::vector<uint32_t>& parentIndices,
    const std::vector<std::array<float, 3>>& restPositions,
    const std::vector<Rotation3>* localRotations) {
    if (restPositions.size() != parentIndices.size()) {
        throw std::invalid_argument(
            "computeSkinningMatrices: restPositions.size() must equal parentIndices.size()");
    }
    const size_t n = parentIndices.size();
    std::vector<Mat3x4> world(n);
    std::vector<Mat3x4> skin(n);

    for (size_t i = 0; i < n; ++i) {
        uint32_t parent = parentIndices[i];
        // Parent-before-child is required for this single forward pass -
        // world[parent] must already be filled in. sr3rig::Rig::parse
        // enforces parentIndices[i] < i for every non-root bone across the
        // whole shipped population (spec-rig-format.md Sec4), so this is a
        // defensive check on a violated precondition, not a real-data path.
        if (parent != kNoParent && parent >= i) {
            throw std::invalid_argument(
                "computeSkinningMatrices: parentIndices must be parent-before-child "
                "(parentIndices[i] < i for every non-root i)");
        }

        Rotation3 R = (localRotations != nullptr && i < localRotations->size())
                          ? (*localRotations)[i]
                          : identityRotation3();

        std::array<float, 3> d;
        if (parent == kNoParent) {
            d = restPositions[i];
        } else {
            d = {restPositions[i][0] - restPositions[parent][0],
                 restPositions[i][1] - restPositions[parent][1],
                 restPositions[i][2] - restPositions[parent][2]};
        }

        // local = T(d_i) * Rot(R_i)
        Mat3x4 local = multiply(Mat3x4::translation(d), Mat3x4::fromRotation(R));
        world[i] = (parent == kNoParent) ? local : multiply(world[parent], local);

        std::array<float, 3> negRest = {-restPositions[i][0], -restPositions[i][1],
                                         -restPositions[i][2]};
        skin[i] = multiply(world[i], Mat3x4::translation(negRest));
    }
    return skin;
}

std::array<float, 3> skinVertexPosition(const sr3mesh::Vertex& v,
                                         const std::vector<Mat3x4>& skin,
                                         SkinningStats* stats) {
    const size_t boneCount = skin.size();
    const std::array<float, 3>& p = v.position;

    float totalWeight = 0.0f;
    std::array<float, 3> acc{0.0f, 0.0f, 0.0f};

    for (int k = 0; k < 4; ++k) {
        uint8_t bi = v.blendIndices[static_cast<size_t>(k)];
        float w = v.blendWeights[static_cast<size_t>(k)];
        if (bi == 255 || w <= 0.0f) continue;
        size_t b = bi;
        if (b >= boneCount) {
            if (stats != nullptr && static_cast<long long>(b) > stats->maxOutOfRangeIndex) {
                stats->maxOutOfRangeIndex = static_cast<long long>(b);
            }
            continue;
        }
        totalWeight += w;
        std::array<float, 3> contrib = skin[b].transformPoint(p);
        acc[0] += w * contrib[0];
        acc[1] += w * contrib[1];
        acc[2] += w * contrib[2];
    }

    if (stats != nullptr) ++stats->verticesProcessed;

    if (totalWeight <= kZeroWeightThreshold) {
        if (stats != nullptr) ++stats->zeroWeightPassthrough;
        return p;
    }
    if (totalWeight < kFullWeightThreshold) {
        if (stats != nullptr) ++stats->lanesDropped;
    }
    return {acc[0] / totalWeight, acc[1] / totalWeight, acc[2] / totalWeight};
}

std::vector<std::array<float, 3>> skinVertices(const std::vector<sr3mesh::Vertex>& verts,
                                                const std::vector<Mat3x4>& skin,
                                                SkinningStats* stats) {
    std::vector<std::array<float, 3>> out(verts.size());
    for (size_t i = 0; i < verts.size(); ++i) {
        out[i] = skinVertexPosition(verts[i], skin, stats);
    }
    return out;
}

} // namespace sr3rig
