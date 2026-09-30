// SYNTHETIC-POSE falsification test for the skinning pipeline.
//
// Bind-pose skinning is the identity (see sr3rig/rig.h), so it cannot fail
// and proves nothing. This builds a NON-identity pose by hand - rotate one
// bone and its descendants by a fixed angle about that bone's own rest
// position - and asks whether the deformation is *structurally* correct.
// No .anim_pc, no keyframe data: the pose is authored here, in the same
// category as any other synthetic fixture.
//
// WHAT THIS CAN AND CANNOT DECIDE.
//
// The rig's rotation convention is OPEN, so there are no bind ROTATIONS to
// be had. This test does not need them: with bind transforms taken as
// pure translations T(p_i), every bone in the rotated subtree shares the
// same skinning matrix
//      Pose_i * InverseBind_i = T(p)*R*T(-p)*T(p_i) * T(-p_i)
//                             = T(p)*R*T(-p)
// which is just "rotate about the joint" and contains no bind rotation at
// all. So the test SIDESTEPS the open convention - and therefore cannot
// decide it either. It tests blend indices, blend weights, the parent
// hierarchy, and the mesh<->rig coordinate transform. It says nothing
// about Euler order vs axis-angle.
//
// WHY IT CAN FAIL. Every metric is run a second time against deliberately
// SCRAMBLED blend indices. If the real run and the scrambled run score
// alike, the metric is not measuring what it claims and the result is
// void - the trap from HANDOFF.md Sec3 (a test whose acceptance region
// contains the null hypothesis).
#include <cmath>
#include <cstdio>
#include <cctype>
#include <fstream>
#include <array>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include "sr3geometry/geometry_block.h"
#include "sr3geometry/material_block.h"
#include "sr3mesh/mesh_block.h"
#include "sr3rig/pose.h"
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

std::map<std::string, std::vector<uint8_t>> g_rigs;
std::vector<std::pair<std::string, std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>> g_meshes;

void walk(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        if (endsWith(n, ".rig_pc")) {
            std::vector<uint8_t> b;
            if (entryBytes(c, i, b) && !b.empty()) g_rigs[n] = b;
        } else if (endsWith(n, ".ccmesh_pc") && g_meshes.size() < 60) {
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

struct Vec3 { float x, y, z; };
static Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static float len3(Vec3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
static std::array<float, 3> toArr(Vec3 v) { return {v.x, v.y, v.z}; }

// Rodrigues rotation, and the Mat3x4 affine type used to build the
// synthetic pose's skin matrices below, now live in sr3rig/pose.h - this
// harness used to carry its own copy (axisAngle()/Mat3/mulM); that was
// exactly the "second implementation" HANDOFF.md Sec3 warns drifts from
// the original, so it was extracted rather than kept in parallel. See
// that header for the Rodrigues formula (bit-identical operation order to
// what stood here before) and the skinning derivation this test checks.

struct Report {
    long long movedVerts = 0;
    long long shouldNotMoveButDid = 0;
    double maxRigidErr = 0.0;
    long long rigidChecked = 0;
    float movedBboxDiag = 0.0f;
    float meshBboxDiag = 0.0f;
    int components = 0;
    int largestComponent = 0;
    double maxEdgeStretch = 0.0;
    long long edgesOver2x = 0;
    long long edgesTotal = 0;
    long long lanesDropped = 0;
    long long limbVerts = 0;
    double maxAbsTear = 0.0;
    long long maxOutOfRangeIndex = -1;
};

// Connectivity of the mesh ITSELF, with no skinning and no pose involved.
//
// This exists to settle the nightblade ambiguity honestly. Its limb
// coherence came out at 7% and I offered two candidate causes without
// eliminating either - which is precisely the error Sec3 warns about, so
// leaving it there was not good enough. The two causes make OPPOSITE
// predictions about this number:
//
//   * if the LOD0 surface is already shattered into many disjoint shells,
//     a limb subset of it CANNOT be more connected than the whole, and 7%
//     is explained by topology alone - nothing to do with skinning;
//   * if the surface is largely one piece, topology is exonerated and the
//     suspicion falls on the mesh<->rig pairing instead.
//
// One number, two hypotheses, opposite predictions. That is what the
// earlier tests lacked.
static void meshTopology(const std::vector<sr3mesh::Vertex>& verts,
                         const std::vector<uint32_t>& tris) {
    const size_t n = verts.size();
    std::vector<char> referenced(n, 0);
    std::vector<std::vector<uint32_t>> adj(n);
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        uint32_t a = tris[t], b = tris[t+1], c = tris[t+2];
        if (a >= n || b >= n || c >= n) continue;
        referenced[a] = referenced[b] = referenced[c] = 1;
        adj[a].push_back(b); adj[b].push_back(a);
        adj[b].push_back(c); adj[c].push_back(b);
        adj[a].push_back(c); adj[c].push_back(a);
    }
    long long refCount = 0;
    for (size_t i = 0; i < n; ++i) refCount += referenced[i];

    std::vector<char> seen(n, 0);
    int shells = 0, largest = 0;
    for (size_t vi = 0; vi < n; ++vi) {
        if (!referenced[vi] || seen[vi]) continue;
        int size = 0;
        std::queue<uint32_t> q;
        q.push(static_cast<uint32_t>(vi));
        seen[vi] = 1;
        while (!q.empty()) {
            uint32_t cur = q.front(); q.pop();
            ++size;
            for (uint32_t nb : adj[cur]) if (!seen[nb]) { seen[nb] = 1; q.push(nb); }
        }
        ++shells;
        if (size > largest) largest = size;
    }
    printf("  BASELINE mesh topology (no skinning): %d shells over %lld referenced verts, "
           "largest %d (%.1f%%)\n", shells, refCount, largest,
           refCount > 0 ? 100.0 * largest / static_cast<double>(refCount) : 0.0);
}

// Applies the synthetic pose via the sr3rig/pose.h library and measures.
// `scramble` produces the control run by permuting the blend indices, so
// every metric has a failure baseline.
//
// `skin` is one Mat3x4 per bone, built by the caller (main(), below) as the
// direct/explicit "rotate about the joint" construction: identity outside
// the rotated subtree, T(pivot)*R*T(-pivot) inside it. That construction
// is test-authoring logic (which bones, which angle) kept here at the call
// site; the actual application of skin[] to every vertex - the part that
// used to be hand-rolled in this loop, weight renormalization, dropped-lane
// counting and all - now goes through sr3rig::skinVertices(), a single
// shared implementation also used by the hierarchical general path this
// harness cross-checks below (see main()).
static Report runPose(const std::vector<sr3mesh::Vertex>& verts,
                      const std::vector<uint32_t>& tris,
                      const std::vector<char>& inSubtree,
                      Vec3 pivot, const std::vector<sr3rig::Mat3x4>& skin,
                      size_t boneCount, int scramble) {
    Report rep;
    const size_t n = verts.size();
    std::vector<char> moved(n, 0);

    // FOURTH FIX. Adjacency comes only from the triangles we were given
    // (draw group 0 = LOD0). A limb vertex that no group-0 triangle
    // references has no edges at all, so it lands in the component count as
    // a singleton and drags coherence down for reasons that have nothing to
    // do with skinning. Restrict the limb set to vertices the graph can
    // actually reach.
    std::vector<char> referenced(n, 0);
    for (size_t t = 0; t < tris.size(); ++t)
        if (tris[t] < n) referenced[tris[t]] = 1;

    // The scramble control permutes blend indices (255 kept as the unused
    // sentinel) BEFORE the shared skinning call, so exactly one place in
    // this file ever applies the scramble formula; both the position
    // computation below and the subtree-weight tally read the same
    // (possibly remapped) indices from `activeVerts`.
    std::vector<sr3mesh::Vertex> scrambledVerts;
    const std::vector<sr3mesh::Vertex>* activeVerts = &verts;
    if (scramble != 0) {
        scrambledVerts = verts;
        for (auto& v : scrambledVerts) {
            for (int k = 0; k < 4; ++k) {
                uint8_t bi = v.blendIndices[static_cast<size_t>(k)];
                if (bi == 255) continue;
                size_t b = (static_cast<size_t>(bi) * 7u + 13u) % boneCount;
                v.blendIndices[static_cast<size_t>(k)] = static_cast<uint8_t>(b);
            }
        }
        activeVerts = &scrambledVerts;
    }

    sr3rig::SkinningStats libStats;
    std::vector<std::array<float, 3>> outArr = sr3rig::skinVertices(*activeVerts, skin, &libStats);
    rep.lanesDropped = libStats.lanesDropped;
    rep.maxOutOfRangeIndex = libStats.maxOutOfRangeIndex;
    std::vector<Vec3> out(n);
    for (size_t vi = 0; vi < n; ++vi) out[vi] = {outArr[vi][0], outArr[vi][1], outArr[vi][2]};

    for (size_t vi = 0; vi < n; ++vi) {
        Vec3 p{verts[vi].position[0], verts[vi].position[1], verts[vi].position[2]};
        const auto& av = (*activeVerts)[vi];

        // Subtree-weight tally: a test-only diagnostic (which fraction of a
        // vertex's USED weight sits on subtree bones), not a second copy of
        // the skinning transform - the transform itself came from
        // sr3rig::skinVertices() above. Same lane-skip/range-check rules as
        // that call (255/non-positive weight skipped, out-of-range index
        // skipped) so the two stay in lockstep by construction.
        float subtreeWeight = 0.0f, totalWeight = 0.0f;
        for (int k = 0; k < 4; ++k) {
            uint8_t bi = av.blendIndices[static_cast<size_t>(k)];
            float w = av.blendWeights[static_cast<size_t>(k)];
            if (bi == 255 || w <= 0.0f) continue;
            if (static_cast<size_t>(bi) >= boneCount) continue;
            totalWeight += w;
            if (inSubtree[bi]) subtreeWeight += w;
        }
        // Matches sr3rig::skinVertexPosition()'s own passthrough rule
        // exactly (same constant) - below this, out[vi] == p already and no
        // metric below applies, same as the original early-continue.
        if (totalWeight <= sr3rig::kZeroWeightThreshold) continue;
        subtreeWeight /= totalWeight;

        float d = len3(sub(out[vi], p));
        // SECOND FIX. "moved at all" (d > 1e-5) is the wrong set to test for
        // coherence: a vertex carrying a 0.01 weight on the subtree twitches
        // by a hair, and those stragglers are scattered over the whole body,
        // shattering the component count into hundreds of one-vertex
        // fragments in BOTH runs. The structural claim is about the vertices
        // that actually belong to the limb, so use MAJORITY weight.
        if (d > 1e-5f) ++rep.movedVerts;
        if (subtreeWeight > 0.5f && referenced[vi]) { moved[vi] = 1; ++rep.limbVerts; }

        // M1: no weight anywhere in the subtree must mean no movement.
        if (subtreeWeight <= 0.0f && d > 1e-5f) ++rep.shouldNotMoveButDid;

        // M2: entirely inside the subtree => rigid rotation about the pivot.
        if (subtreeWeight > 0.999f) {
            float r0 = len3(sub(p, pivot)), r1 = len3(sub(out[vi], pivot));
            if (r0 > 1e-3f) {
                double err = std::fabs(static_cast<double>(r1) - r0) / r0;
                if (err > rep.maxRigidErr) rep.maxRigidErr = err;
                ++rep.rigidChecked;
            }
        }
    }

    // M3: spatial extent of the moved set, against the whole mesh.
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    float mlo[3] = {1e30f, 1e30f, 1e30f}, mhi[3] = {-1e30f, -1e30f, -1e30f};
    for (size_t vi = 0; vi < n; ++vi) {
        const float* q = verts[vi].position.data();
        for (int a = 0; a < 3; ++a) {
            if (q[a] < mlo[a]) mlo[a] = q[a];
            if (q[a] > mhi[a]) mhi[a] = q[a];
        }
        if (!moved[vi]) continue;
        for (int a = 0; a < 3; ++a) {
            if (q[a] < lo[a]) lo[a] = q[a];
            if (q[a] > hi[a]) hi[a] = q[a];
        }
    }
    rep.meshBboxDiag = len3(Vec3{mhi[0]-mlo[0], mhi[1]-mlo[1], mhi[2]-mlo[2]});
    if (rep.movedVerts > 0)
        rep.movedBboxDiag = len3(Vec3{hi[0]-lo[0], hi[1]-lo[1], hi[2]-lo[2]});

    // THIRD FIX. A stretch RATIO on a near-degenerate rest edge is
    // meaningless: an edge 0.001 long whose ends separate by 0.33 reports
    // 328x, and it reported the SAME 328x under scrambled indices - proof
    // the number was describing the edge, not the deformation. Floor the
    // rest length at 0.1% of the model diagonal, and also track the
    // ABSOLUTE separation, which no short edge can inflate.
    const float edgeFloor = rep.meshBboxDiag * 0.001f;

    std::vector<std::vector<uint32_t>> adj(n);
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        uint32_t a = tris[t], b = tris[t+1], c = tris[t+2];
        if (a >= n || b >= n || c >= n) continue;
        adj[a].push_back(b); adj[b].push_back(a);
        adj[b].push_back(c); adj[c].push_back(b);
        adj[a].push_back(c); adj[c].push_back(a);

        const uint32_t e[3][2] = {{a,b},{b,c},{a,c}};
        for (const auto& ed : e) {
            Vec3 p0{verts[ed[0]].position[0], verts[ed[0]].position[1], verts[ed[0]].position[2]};
            Vec3 p1{verts[ed[1]].position[0], verts[ed[1]].position[1], verts[ed[1]].position[2]};
            float o = len3(sub(p1, p0));
            if (o < edgeFloor) continue; // scale-relative: below this the RATIO is noise
            float d2 = len3(sub(out[ed[1]], out[ed[0]]));
            double ratio = static_cast<double>(d2) / o;
            ++rep.edgesTotal;
            if (ratio > rep.maxEdgeStretch) rep.maxEdgeStretch = ratio;
            double tear = std::fabs(static_cast<double>(d2) - o);
            if (tear > rep.maxAbsTear) rep.maxAbsTear = tear;
            if (ratio > 2.0) ++rep.edgesOver2x;
        }
    }

    // M3b: connected components of the moved set over the triangle graph.
    std::vector<char> seen(n, 0);
    for (size_t vi = 0; vi < n; ++vi) {
        if (!moved[vi] || seen[vi]) continue;
        int size = 0;
        std::queue<uint32_t> q;
        q.push(static_cast<uint32_t>(vi));
        seen[vi] = 1;
        while (!q.empty()) {
            uint32_t cur = q.front(); q.pop();
            ++size;
            for (uint32_t nb : adj[cur])
                if (moved[nb] && !seen[nb]) { seen[nb] = 1; q.push(nb); }
        }
        ++rep.components;
        if (size > rep.largestComponent) rep.largestComponent = size;
    }
    return rep;
}

static void printReport(const char* label, const Report& r) {
    printf("  %-20s moved %6lld verts   moved-bbox %.3f of mesh %.3f  (%.0f%%)\n", label,
           r.movedVerts, static_cast<double>(r.movedBboxDiag), static_cast<double>(r.meshBboxDiag),
           r.meshBboxDiag > 0 ? 100.0 * r.movedBboxDiag / r.meshBboxDiag : 0.0);
    printf("  %-20s   M1 zero-weight verts that moved : %lld\n", "", r.shouldNotMoveButDid);
    printf("  %-20s   M2 max rigid-radius error       : %.6f%% over %lld verts\n", "",
           r.maxRigidErr * 100.0, r.rigidChecked);
    printf("  %-20s   M3 connected components         : %d (largest %d)\n", "",
           r.components, r.largestComponent);
    printf("  %-20s   M4 max edge stretch             : %.2fx   edges >2x: %lld / %lld\n", "",
           r.maxEdgeStretch, r.edgesOver2x, r.edgesTotal);
    printf("  %-20s   M3b coherence (largest/limb)    : %.1f%%   limb verts: %lld\n", "",
           r.limbVerts > 0 ? 100.0 * r.largestComponent / static_cast<double>(r.limbVerts) : 0.0,
           r.limbVerts);
    printf("  %-20s   M5 max ABS tear                 : %.4f (%.1f%% of model)  sum<0.999: %lld  OOR idx: %lld\n",
           "", r.maxAbsTear,
           r.meshBboxDiag > 0 ? 100.0 * r.maxAbsTear / r.meshBboxDiag : 0.0,
           r.lanesDropped, r.maxOutOfRangeIndex);
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> b = readFile(argv[i]);
        if (b.empty()) continue;
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            walk(c);
        } catch (const std::exception&) {}
    }
    printf("loaded %zu rigs, %zu meshes\n\n", g_rigs.size(), g_meshes.size());

    int done = 0;
    for (const auto& m : g_meshes) {
        if (done >= 3) break;
        std::string stem = m.first.substr(0, m.first.find('.'));
        const std::vector<uint8_t>* rb = nullptr;
        for (const auto& r : g_rigs)
            if (r.first.substr(0, r.first.find('.')) == stem) { rb = &r.second; break; }
        if (!rb) continue;

        try {
            sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(rb->data(), rb->size()));
            const auto& cb = m.second.first;
            const auto& gb = m.second.second;
            sr3geometry::MaterialBlock mat =
                sr3geometry::MaterialBlock::parse(vpp::ByteView(cb.data(), cb.size()));
            sr3geometry::GeometryBlock geo =
                sr3geometry::GeometryBlock::parse(vpp::ByteView(cb.data(), cb.size()), mat);
            if (!geo.hasMeshSubBlock()) continue;
            sr3mesh::MeshBlock mesh = sr3mesh::MeshBlock::parse(
                vpp::ByteView(cb.data(), cb.size()), geo.meshSubBlockOffset(),
                vpp::ByteView(gb.data(), gb.size()));
            if (mesh.channels().empty()) continue;
            if (!sr3mesh::layoutInfoFor(mesh.channels()[0].layoutCode).hasSkinning) continue;
            std::vector<sr3mesh::Vertex> verts = mesh.decodeChannel(0);
            if (verts.empty()) continue;
            std::vector<uint32_t> tris;
            if (mesh.drawGroupsLocated()) tris = mesh.triangleListForGroup(0);
            if (tris.empty()) continue;

            const auto& bones = rig.bones();
            const char* wanted[] = {"upperarm", "forearm", "thigh", "calf", "shoulder"};
            int target = -1;
            for (const char* w : wanted) {
                for (size_t i = 0; i < bones.size() && target < 0; ++i) {
                    std::string nm = bones[i].name;
                    for (auto& ch : nm) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    if (nm.find(w) != std::string::npos) {
                        bool hasChild = false;
                        for (const auto& b2 : bones)
                            if (!b2.isRoot() && b2.parentIndex == i) { hasChild = true; break; }
                        if (hasChild) target = static_cast<int>(i);
                    }
                }
                if (target >= 0) break;
            }
            if (target < 0) continue;

            std::vector<char> inSubtree(bones.size(), 0);
            inSubtree[static_cast<size_t>(target)] = 1;
            for (size_t i = 0; i < bones.size(); ++i) // parent < child, enforced by the reader
                if (!bones[i].isRoot() && inSubtree[bones[i].parentIndex]) inSubtree[i] = 1;
            int subtreeSize = 0;
            for (char c2 : inSubtree) subtreeSize += c2;

            // mesh = (rig.x, -rig.y, -rig.z)  (HANDOFF.md Sec9.13) - now
            // sr3rig::rigToMeshSpace(), reused rather than re-typed.
            const auto& rp = bones[static_cast<size_t>(target)].restPosition;
            Vec3 pivot{rp[0], -rp[1], -rp[2]};
            sr3rig::Rotation3 rot =
                sr3rig::rotationFromAxisAngle({0.0f, 0.0f, 1.0f}, 45.0f * 3.14159265f / 180.0f);

            printf("=== %s  (rig: %zu bones, %zu verts, %zu tris) ===\n", m.first.c_str(),
                   bones.size(), verts.size(), tris.size() / 3);
            printf("  joint [%d] \"%s\"  subtree %d bones  pivot (%.3f %.3f %.3f)  +45deg about Z\n",
                   target, bones[static_cast<size_t>(target)].name.c_str(), subtreeSize,
                   static_cast<double>(pivot.x), static_cast<double>(pivot.y),
                   static_cast<double>(pivot.z));

            meshTopology(verts, tris);
            printf("  subtree bones:");
            for (size_t i = 0; i < bones.size(); ++i)
                if (inSubtree[i]) printf(" %s", bones[i].name.c_str());
            printf("\n");

            long long norm = 0, tot = 0;
            for (const auto& v : verts) {
                float s = 0.0f;
                bool any = false;
                for (int k = 0; k < 4; ++k) {
                    if (v.blendIndices[static_cast<size_t>(k)] == 255) continue;
                    s += v.blendWeights[static_cast<size_t>(k)];
                    any = true;
                }
                if (!any) continue;
                ++tot;
                if (s > 0.99f && s < 1.01f) ++norm;
            }
            printf("  weights sum to 1 within 1%%      : %lld / %lld\n", norm, tot);

            // Direct/explicit construction of the per-bone skin array for
            // THIS synthetic pose: identity outside the rotated subtree,
            // T(pivot)*R*T(-pivot) ("rotate about the joint") inside it.
            // This IS test-authoring logic (which bones, which angle) - the
            // application of skin[] to vertices, below and inside runPose(),
            // goes entirely through the shared sr3rig::skinVertices().
            std::array<float, 3> pivotArr = toArr(pivot);
            std::array<float, 3> negPivotArr = {-pivotArr[0], -pivotArr[1], -pivotArr[2]};
            sr3rig::Mat3x4 rotateAboutPivot =
                sr3rig::multiply(sr3rig::multiply(sr3rig::Mat3x4::translation(pivotArr),
                                                   sr3rig::Mat3x4::fromRotation(rot)),
                                  sr3rig::Mat3x4::translation(negPivotArr));
            std::vector<sr3rig::Mat3x4> skin(bones.size());
            for (size_t i = 0; i < bones.size(); ++i)
                skin[i] = inSubtree[i] ? rotateAboutPivot : sr3rig::Mat3x4::identity();

            Report real = runPose(verts, tris, inSubtree, pivot, skin, bones.size(), 0);
            Report ctrl = runPose(verts, tris, inSubtree, pivot, skin, bones.size(), 1);
            printReport("REAL indices", real);
            printReport("SCRAMBLED control", ctrl);

            // ---- Regression: the GENERAL hierarchical library path
            // (sr3rig::computeSkinningMatrices, top-down World_i =
            // World_parent(i)*T(d_i)*Rot(R_i)) must reproduce the
            // hand-authored `skin` above exactly, setting R_target = rot
            // and every other bone's local rotation to identity. This is
            // the task's primary regression check: it must match to
            // floating-point precision, not "look similar" - a real
            // mismatch here would mean the hierarchical derivation has a
            // bug (composition order, row/column convention, etc.), not
            // that a looser tolerance is warranted. ----
            {
                std::vector<uint32_t> parentIdx(bones.size());
                std::vector<std::array<float, 3>> restMesh(bones.size());
                for (size_t i = 0; i < bones.size(); ++i) {
                    parentIdx[i] = bones[i].parentIndex;
                    restMesh[i] = sr3rig::rigToMeshSpace(bones[i].restPosition);
                }
                std::vector<sr3rig::Rotation3> rotations(bones.size(), sr3rig::identityRotation3());
                rotations[static_cast<size_t>(target)] = rot;
                std::vector<sr3rig::Mat3x4> skin2 =
                    sr3rig::computeSkinningMatrices(parentIdx, restMesh, &rotations);

                double maxSkinDelta = 0.0;
                for (size_t i = 0; i < skin.size(); ++i)
                    for (int r = 0; r < 3; ++r)
                        for (int c = 0; c < 4; ++c) {
                            double d = std::fabs(static_cast<double>(skin2[i].m[r][c]) -
                                                  static_cast<double>(skin[i].m[r][c]));
                            if (d > maxSkinDelta) maxSkinDelta = d;
                        }

                std::vector<std::array<float, 3>> out2 = sr3rig::skinVertices(verts, skin2);
                std::vector<std::array<float, 3>> out1 = sr3rig::skinVertices(verts, skin);
                double maxVertDelta = 0.0;
                for (size_t vi = 0; vi < verts.size(); ++vi) {
                    double dx = static_cast<double>(out2[vi][0]) - out1[vi][0];
                    double dy = static_cast<double>(out2[vi][1]) - out1[vi][1];
                    double dz = static_cast<double>(out2[vi][2]) - out1[vi][2];
                    double d = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (d > maxVertDelta) maxVertDelta = d;
                }
                printf("  HIERARCHICAL vs DIRECT   : max |Skin_i elem delta| = %.3e over %zu bones"
                       "   max |vertex delta| = %.3e over %zu verts\n",
                       maxSkinDelta, skin.size(), maxVertDelta, verts.size());

                // ---- Bind-pose identity check: all-identity local
                // rotations must reduce every Skin_i to the identity, and
                // therefore leave every vertex position unchanged. ----
                std::vector<sr3rig::Mat3x4> bindSkin =
                    sr3rig::computeSkinningMatrices(parentIdx, restMesh, nullptr);
                double maxBindSkinDev = 0.0;
                for (size_t i = 0; i < bindSkin.size(); ++i)
                    for (int r = 0; r < 3; ++r)
                        for (int c = 0; c < 4; ++c) {
                            float expect = (r == c) ? 1.0f : 0.0f;
                            double d = std::fabs(static_cast<double>(bindSkin[i].m[r][c]) - expect);
                            if (d > maxBindSkinDev) maxBindSkinDev = d;
                        }
                std::vector<std::array<float, 3>> bindOut = sr3rig::skinVertices(verts, bindSkin);
                double maxBindVertDev = 0.0;
                for (size_t vi = 0; vi < verts.size(); ++vi) {
                    double dx = static_cast<double>(bindOut[vi][0]) - verts[vi].position[0];
                    double dy = static_cast<double>(bindOut[vi][1]) - verts[vi].position[1];
                    double dz = static_cast<double>(bindOut[vi][2]) - verts[vi].position[2];
                    double d = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (d > maxBindVertDev) maxBindVertDev = d;
                }
                printf("  BIND-POSE identity       : max |Skin_i - I| = %.3e over %zu bones"
                       " (tol 1e-5)   max |v' - v| = %.3e over %zu verts (tol 1e-5)\n",
                       maxBindSkinDev, bindSkin.size(), maxBindVertDev, verts.size());
            }
            printf("\n");
            ++done;
        } catch (const std::exception& ex) {
            printf("  skip %s: %s\n", m.first.c_str(), ex.what());
        }
    }
    if (done == 0) printf("no skinned mesh+rig pair found\n");
    return 0;
}
