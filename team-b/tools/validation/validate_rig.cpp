// Real-data validation for sr3rig, plus the mesh<->rig cross-check.
//
// The cross-check is the interesting part: a mesh's blend indices are
// decoded from .gcmesh_pc and a bone's rest position from .rig_pc, with
// nothing in either decoding referring to the other. If vertices weighted
// to bone B cluster near bone B's rest position, two independently derived
// readings agree - which neither could fake alone.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <array>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3geometry/geometry_block.h"
#include "sr3geometry/material_block.h"
#include "sr3mesh/mesh_block.h"
#include "sr3rig/rig.h"
#include "vpp/container.h"

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}
bool endsWith(const std::string& s, const std::string& x) {
    return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}
bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView r = c.rawEntryBytes(i);
        out.assign(r.data(), r.data() + r.size());
        return true;
    }
    auto r = c.decompressEntry(i);
    bool ok = r.status == vpp::DecodeStatus::Ok ||
              r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
              r.status == vpp::DecodeStatus::ContentValidated ||
              r.status == vpp::DecodeStatus::RecoveredSharedStream ||
              r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!ok) return false;
    out = std::move(r.data);
    return true;
}

int g_rigs = 0, g_ok = 0, g_fail = 0, g_hashOk = 0, g_withAttach = 0;
long long g_bones = 0, g_namedBones = 0, g_attachments = 0;
int g_minBones = 1 << 30, g_maxBones = 0;
std::map<int, int> g_rootCounts;
long long g_rotWithinPi = 0, g_rotTotal = 0;
// spec-rig-format.md §13.4 / §13.2 gates (added 2026-09-20).
long long g_attTotal = 0, g_attRotOk = 0, g_attTagMinus1 = 0;
long long g_attZeroTrans = 0, g_attSameName = 0, g_attBothZeroAndSame = 0;
constexpr int kShifts = 8;
const size_t kShiftOffsets[kShifts] = {0x00, 0x04, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x08};
long long g_attShiftOk[kShifts] = {};          // the last entry (0x08) is the REAL layout
long long g_partitionOk = 0;                   // +0x28 + +0x2C == bone_count
long long g_pairOk[4] = {};                    // adjacent-pair controls
const size_t kPairA[4] = {0x24, 0x30, 0x34, 0x38};
const size_t kPairB[4] = {0x28, 0x34, 0x38, 0x3C};

// Orthonormal (|norm-1| < 0.01, |dot| < 0.01) with determinant +1, for nine
// floats read as three rows.
bool properRotation(const float m[9]) {
    auto dot = [&](int a, int b) {
        return m[a * 3] * m[b * 3] + m[a * 3 + 1] * m[b * 3 + 1] + m[a * 3 + 2] * m[b * 3 + 2];
    };
    for (int r = 0; r < 3; ++r)
        if (std::fabs(std::sqrt(dot(r, r)) - 1.0f) >= 0.01f) return false;
    if (std::fabs(dot(0, 1)) >= 0.01f || std::fabs(dot(0, 2)) >= 0.01f ||
        std::fabs(dot(1, 2)) >= 0.01f)
        return false;
    const float det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                      m[2] * (m[3] * m[7] - m[4] * m[6]);
    return std::fabs(det - 1.0f) < 0.01f;
}
float rdF32(const std::vector<uint8_t>& b, size_t at) {
    float v;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}
uint32_t rdU32(const std::vector<uint8_t>& b, size_t at) {
    uint32_t v;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}
std::set<std::string> g_commonNames;
int g_dumped = 0;

// cross-check
int g_pairsChecked = 0;
long long g_xVerts = 0;
double g_xDistSum = 0.0;
double g_xControlSum = 0.0;
int g_xBoneIndexInRange = 0, g_xBoneIndexTotal = 0;
int g_xMaxIndex = -1, g_xRigBonesMax = 0;
int g_xBoneRankHit = 0, g_xBoneRankTotal = 0;
std::vector<std::array<float,3>> g_xCentroidSum(256, {0.0f,0.0f,0.0f});
std::vector<int> g_xCentroidCount(256, 0);

std::map<std::string, std::vector<uint8_t>> g_rigStore;

void checkRig(const std::string& name, const std::vector<uint8_t>& b) {
    ++g_rigs;
    try {
        sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(b.data(), b.size()));
        ++g_ok;
        if (rig.hashTableMatchesNames()) ++g_hashOk;
        int bones = static_cast<int>(rig.bones().size());
        g_bones += bones;
        if (bones < g_minBones) g_minBones = bones;
        if (bones > g_maxBones) g_maxBones = bones;
        int roots = 0;
        for (const auto& bone : rig.bones()) {
            if (!bone.name.empty()) ++g_namedBones;
            if (bone.isRoot()) ++roots;
            for (float r : bone.negatedParentOffset) {
                ++g_rotTotal;
                if (r >= -3.14159266f && r <= 3.14159266f) ++g_rotWithinPi;
            }
            if (g_commonNames.size() < 14) g_commonNames.insert(bone.name);
        }
        ++g_rootCounts[roots];
        if (!rig.attachments().empty()) ++g_withAttach;
        g_attachments += static_cast<long long>(rig.attachments().size());
        if (rig.groupPartitionHolds()) ++g_partitionOk;
        for (int p = 0; p < 4; ++p)
            if (static_cast<uint64_t>(rdU32(b, kPairA[p])) + rdU32(b, kPairB[p]) ==
                static_cast<uint64_t>(bones))
                ++g_pairOk[p];
        {
            // Raw attachment-array position, for the shifted-offset controls.
            const size_t boneArrayAt = ((0x50 + static_cast<size_t>(bones) * 4) + 7) & ~size_t(7);
            const size_t attArrayAt = boneArrayAt + static_cast<size_t>(bones) * 0x28;
            for (size_t ai = 0; ai < rig.attachments().size(); ++ai) {
                const size_t at = attArrayAt + ai * 0x40;
                for (int s = 0; s < kShifts; ++s) {
                    float m[9];
                    for (int k = 0; k < 9; ++k) m[k] = rdF32(b, at + kShiftOffsets[s] + k * 4);
                    if (properRotation(m)) ++g_attShiftOk[s];
                }
            }
        }
        for (const auto& a : rig.attachments()) {
            ++g_attTotal;
            float m[9];
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) m[r * 3 + c] = a.rotationRows[static_cast<size_t>(r)][static_cast<size_t>(c)];
            if (properRotation(m)) ++g_attRotOk;
            if (a.tag == -1) ++g_attTagMinus1;
            const bool zeroT = a.translation[0] == 0.0f && a.translation[1] == 0.0f &&
                               a.translation[2] == 0.0f;
            const bool sameName = a.parentBoneIndex < rig.bones().size() &&
                                  a.name == rig.bones()[a.parentBoneIndex].name;
            if (zeroT) ++g_attZeroTrans;
            if (sameName) ++g_attSameName;
            if (zeroT && sameName) ++g_attBothZeroAndSame;
            if (zeroT != sameName) {
                // The odd ones out (spec §13.4 says exactly one of each kind).
                printf("  ODD: %s att '%s' -> bone '%s'  t=(%.9g %.9g %.9g)\n", name.c_str(),
                       a.name.c_str(), rig.bones()[a.parentBoneIndex].name.c_str(),
                       a.translation[0], a.translation[1], a.translation[2]);
            }
        }
        if (g_dumped < 3) {
            ++g_dumped;
            printf("  %-28s %3d bones, %2zu attachments, roots %d, hash %s\n", name.c_str(),
                   bones, rig.attachments().size(), roots,
                   rig.hashTableMatchesNames() ? "OK" : "MISMATCH");
            for (int i = 0; i < bones && i < 5; ++i) {
                const auto& bone = rig.bones()[static_cast<size_t>(i)];
                printf("      [%2d] %-14s pos (%7.3f %7.3f %7.3f) parent %d\n", i,
                       bone.name.c_str(), bone.restPosition[0], bone.restPosition[1],
                       bone.restPosition[2],
                       bone.isRoot() ? -1 : static_cast<int>(bone.parentIndex));
            }
        }
        g_rigStore[name] = b;
    } catch (const std::exception& ex) {
        ++g_fail;
        if (g_fail <= 6) printf("  RIG FAIL %s: %s\n", name.c_str(), ex.what());
    }
}

// For a mesh + rig, measure how far each skinned vertex sits from the rest
// position of its highest-weighted bone, against a control that uses a
// deliberately wrong bone.
void crossCheck(const std::string& meshName, const std::vector<uint8_t>& cb,
                const std::vector<uint8_t>& gb, const sr3rig::Rig& rig) {
    try {
        sr3geometry::MaterialBlock mat =
            sr3geometry::MaterialBlock::parse(vpp::ByteView(cb.data(), cb.size()));
        sr3geometry::GeometryBlock geo =
            sr3geometry::GeometryBlock::parse(vpp::ByteView(cb.data(), cb.size()), mat);
        if (!geo.hasMeshSubBlock()) return;
        sr3mesh::MeshBlock mesh = sr3mesh::MeshBlock::parse(
            vpp::ByteView(cb.data(), cb.size()), geo.meshSubBlockOffset(),
            vpp::ByteView(gb.data(), gb.size()));
        if (mesh.channels().empty()) return;
        sr3mesh::LayoutInfo info = sr3mesh::layoutInfoFor(mesh.channels()[0].layoutCode);
        if (!info.hasSkinning) return;

        std::vector<sr3mesh::Vertex> verts = mesh.decodeChannel(0);
        const size_t boneCount = rig.bones().size();
        if (boneCount == 0) return;
        ++g_pairsChecked;

        for (const auto& v : verts) {
            // Highest-weighted bone.
            int best = -1;
            float bestW = -1.0f;
            for (int i = 0; i < 4; ++i) {
                if (v.blendIndices[static_cast<size_t>(i)] == 255) continue;
                if (v.blendWeights[static_cast<size_t>(i)] > bestW) {
                    bestW = v.blendWeights[static_cast<size_t>(i)];
                    best = v.blendIndices[static_cast<size_t>(i)];
                }
            }
            if (best < 0) continue;
            ++g_xBoneIndexTotal;
            if (static_cast<size_t>(best) < boneCount) ++g_xBoneIndexInRange;
            else continue;

            // Transform per Team A: mesh = (rig.x, -rig.y, -rig.z), a 180-degree
            // rotation about X. Verified here rather than taken on trust.
            const auto& bpRaw = rig.bones()[static_cast<size_t>(best)].restPosition;
            const float bp[3] = {bpRaw[0], -bpRaw[1], -bpRaw[2]};
            float dx = v.position[0] - bp[0], dy = v.position[1] - bp[1],
                  dz = v.position[2] - bp[2];
            g_xDistSum += std::sqrt(dx * dx + dy * dy + dz * dz);

            // Control: a deliberately mismatched bone (shifted index).
            size_t wrong = (static_cast<size_t>(best) + boneCount / 2) % boneCount;
            const auto& cpRaw = rig.bones()[wrong].restPosition;
            const float cp[3] = {cpRaw[0], -cpRaw[1], -cpRaw[2]};
            float cx = v.position[0] - cp[0], cy = v.position[1] - cp[1],
                  cz = v.position[2] - cp[2];
            g_xControlSum += std::sqrt(cx * cx + cy * cy + cz * cz);
            ++g_xVerts;

            if (best > g_xMaxIndex) g_xMaxIndex = best;
            g_xCentroidSum[static_cast<size_t>(best)][0] += v.position[0];
            g_xCentroidSum[static_cast<size_t>(best)][1] += v.position[1];
            g_xCentroidSum[static_cast<size_t>(best)][2] += v.position[2];
            ++g_xCentroidCount[static_cast<size_t>(best)];
        }

        // Rank test, far sharper than a mean distance: for each bone that
        // actually has vertices, is the bone whose rest position is nearest
        // that cluster's centroid the SAME bone the indices named? Under
        // direct indexing this should hit often; under an indirection it
        // should sit near chance.
        if (g_xBoneRankTotal < 400) {
            for (size_t bi = 0; bi < boneCount && bi < g_xCentroidCount.size(); ++bi) {
                if (g_xCentroidCount[bi] < 20) continue;
                float cx = g_xCentroidSum[bi][0] / static_cast<float>(g_xCentroidCount[bi]);
                float cy = g_xCentroidSum[bi][1] / static_cast<float>(g_xCentroidCount[bi]);
                float cz = g_xCentroidSum[bi][2] / static_cast<float>(g_xCentroidCount[bi]);
                int nearest = -1;
                float nearestDist = 1e30f;
                for (size_t b2 = 0; b2 < boneCount; ++b2) {
                    // Same transform as above - the rank test was silently
                    // comparing against UNtransformed positions, which made
                    // it fail for a reason that had nothing to do with the
                    // hypothesis it was meant to test.
                    const auto& pr = rig.bones()[b2].restPosition;
                    const float p[3] = {pr[0], -pr[1], -pr[2]};
                    float dx2 = cx - p[0], dy2 = cy - p[1], dz2 = cz - p[2];
                    float d = dx2 * dx2 + dy2 * dy2 + dz2 * dz2;
                    if (d < nearestDist) { nearestDist = d; nearest = static_cast<int>(b2); }
                }
                ++g_xBoneRankTotal;
                if (nearest == static_cast<int>(bi)) ++g_xBoneRankHit;
            }
            g_xCentroidSum.assign(256, {0.0f, 0.0f, 0.0f});
            g_xCentroidCount.assign(256, 0);
            if (static_cast<int>(boneCount) > g_xRigBonesMax) g_xRigBonesMax = static_cast<int>(boneCount);
        }
    } catch (const std::exception&) {
    }
}

std::vector<std::pair<std::string, std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>> g_meshes;

void walk(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        if (endsWith(n, ".rig_pc")) {
            std::vector<uint8_t> b;
            if (entryBytes(c, i, b) && !b.empty()) checkRig(n, b);
        } else if (endsWith(n, ".ccmesh_pc") && g_meshes.size() < 40) {
            std::string gn = n;
            size_t d = gn.find_last_of('.');
            if (d != std::string::npos) gn[d + 1] = 'g';
            std::vector<uint8_t> cb, gb;
            if (!entryBytes(c, i, cb)) continue;
            for (size_t j = 0; j < c.entries().size(); ++j) {
                if (c.entries()[j].name == gn) { entryBytes(c, j, gb); break; }
            }
            if (!gb.empty()) g_meshes.push_back({n, {cb, gb}});
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { walk(c.openNested(i)); } catch (const std::exception&) {}
        }
    }
}

int main(int argc, char** argv) {
    printf("sample rigs:\n");
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> b = readFile(argv[i]);
        if (b.empty()) continue;
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            walk(c);
            printf("scanned %s\n", argv[i]);
            fflush(stdout);
        } catch (const std::exception&) {}
    }

    printf("\n=== sr3rig validation (spec-rig-format.md v2) ===\n");
    printf("rigs found            : %d   (spec Sec2: 585 across preload_rigs + characters)\n", g_rigs);
    printf("parsed OK             : %d\n", g_ok);
    printf("failed                : %d\n", g_fail);
    printf("hash table == hash(lowercased name) : %d / %d   (spec Sec5: 22,274/22,274 bones)\n",
           g_hashOk, g_ok);
    printf("bones total           : %lld   (named: %lld)\n", g_bones, g_namedBones);
    if (g_ok) printf("bone count range      : %d .. %d   (spec Sec2: 1 .. 109)\n", g_minBones, g_maxBones);
    printf("rotation triple within +/-pi        : %lld / %lld   (spec Sec4: 22,157/22,274)\n",
           g_rotWithinPi, g_rotTotal);
    printf("rigs with attachments : %d / %d   (spec Sec2: 286/585)\n", g_withAttach, g_ok);
    printf("\n--- spec §13.4 attachment record (rotation matrix, NOT a quaternion) ---\n");
    printf("rows at +0x08/+0x14/+0x20 orthonormal, det +1 : %lld / %lld   (spec: 12,480/12,480)\n",
           g_attRotOk, g_attTotal);
    printf("controls, first row at another offset (each CAN fail; spec: 0 / 12,480):\n");
    for (int s = 0; s < kShifts - 1; ++s)
        printf("   first row at +0x%02zX : %lld / %lld\n", kShiftOffsets[s], g_attShiftOk[s],
               g_attTotal);
    printf("   (real layout, +0x08, re-derived from raw bytes: %lld / %lld)\n",
           g_attShiftOk[kShifts - 1], g_attTotal);
    printf("tag (+0x3C) == -1                             : %lld / %lld   (spec: 12,480/12,480)\n",
           g_attTagMinus1, g_attTotal);
    printf("zero translation                              : %lld   (spec: 8,490)\n", g_attZeroTrans);
    printf("named after parent bone                       : %lld\n", g_attSameName);
    printf("zero translation AND same name                : %lld   (spec: 8,489)\n",
           g_attBothZeroAndSame);
    printf("\n--- spec §13.2 header partition ---\n");
    printf("+0x28 + +0x2C == bone_count                   : %lld / %d   (spec: 585/585)\n",
           g_partitionOk, g_ok);
    printf("adjacent-pair sum controls (spec: 0/585 each): (+0x24,+0x28) %lld  (+0x30,+0x34) %lld  "
           "(+0x34,+0x38) %lld  (+0x38,+0x3C) %lld\n",
           g_pairOk[0], g_pairOk[1], g_pairOk[2], g_pairOk[3]);
    printf("\n");
    printf("root counts per rig   : ");
    for (const auto& kv : g_rootCounts) printf("%d root(s) x%d  ", kv.first, kv.second);
    printf("\nsample bone names     : ");
    for (const auto& n : g_commonNames) printf("%s ", n.c_str());
    printf("\n");

    // --- mesh <-> rig cross-check ---
    printf("\n=== mesh<->rig cross-validation ===\n");
    if (!g_rigStore.empty() && !g_meshes.empty()) {
        // Pair each mesh with a rig by matching name stems where possible,
        // else use the largest rig as a stand-in for body meshes.
        for (const auto& m : g_meshes) {
            std::string stem = m.first.substr(0, m.first.find('.'));
            const std::vector<uint8_t>* chosen = nullptr;
            for (const auto& r : g_rigStore) {
                if (r.first.substr(0, r.first.find('.')) == stem) { chosen = &r.second; break; }
            }
            if (chosen == nullptr) continue;
            try {
                sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(chosen->data(), chosen->size()));
                crossCheck(m.first, m.second.first, m.second.second, rig);
            } catch (const std::exception&) {}
        }
    }
    printf("mesh/rig pairs matched by name      : %d\n", g_pairsChecked);
    printf("blend indices < rig bone count      : %d / %d\n", g_xBoneIndexInRange, g_xBoneIndexTotal);
    if (g_xVerts > 0) {
        printf("mean distance, vertex -> its own bone's rest position : %.4f\n",
               g_xDistSum / static_cast<double>(g_xVerts));
        printf("mean distance, same vertex -> a WRONG bone (control)  : %.4f\n",
               g_xControlSum / static_cast<double>(g_xVerts));
        printf("ratio (control / own)               : %.2fx   <- near 1.0 means NO agreement\n",
               (g_xDistSum > 0.0) ? (g_xControlSum / g_xDistSum) : 0.0);
        printf("max blend index seen                : %d   (largest rig bone count seen: %d)\n",
               g_xMaxIndex, g_xRigBonesMax);
        printf("rank test - nearest bone to a weight-cluster's centroid IS that cluster's bone:\n");
        printf("   %d / %d (%.1f%%)\n", g_xBoneRankHit, g_xBoneRankTotal,
               g_xBoneRankTotal ? 100.0 * g_xBoneRankHit / g_xBoneRankTotal : 0.0);
    }
    return g_fail == 0 ? 0 : 1;
}
