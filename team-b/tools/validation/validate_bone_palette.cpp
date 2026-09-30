// Population validation for HANDOFF Sec9.62: the Mesh sub-block's BONE
// PALETTE (sr3mesh::MeshBlock::bonePalette()) and the (-x,-y,-z) rig->mesh
// convention it implies.
//
// For every <stem>.ccmesh_pc / <stem>.gcmesh_pc / <stem>.rig_pc triple in
// the archives given (recursing into nested containers), on the first
// skinned channel:
//
//   INVARIANTS (each counted over the population, not asserted on one file)
//     I1  palette.size() == declared count          (complete)
//     I2  entries strictly ascending
//     I3  every entry < rig bone count
//     I4  highest blend index used by any lane < palette.size()
//     I5  highest blend index used == palette.size() - 1   (the list is
//         exactly the used set, no trailing unused slots) - reported, not
//         required
//
//   CLUSTER RESIDUAL - the per-bone test Sec9.13 used, but now with the
//   index mapping READ FROM THE FILE rather than assumed. For each
//   vertex, the distance from the vertex to the mesh-space rest position
//   of its DOMINANT bone; the per-mesh statistic is the median over
//   vertices. Four configurations:
//     A  direct indices,  (x,-y,-z)   - every earlier section's reading
//     B  palette,         (x,-y,-z)   - palette only
//     C  palette,         (-x,-y,-z)  - Sec9.62's reading
//     D  SHUFFLED palette,(-x,-y,-z)  - control: same list, deterministically
//                                        permuted, so C's improvement has to
//                                        beat "any list of the right size"
//   plus the count of vertices whose dominant bone rest is > 0.35m away
//   (the mis-index signature that first exposed this).
//
// Usage: validate_bone_palette <archive.vpp_pc> [more archives...] [--verbose]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "sr3geometry/geometry_block.h"
#include "sr3geometry/material_block.h"
#include "sr3mesh/mesh_block.h"
#include "sr3rig/bone_palette.h"
#include "sr3rig/pose.h"
#include "sr3rig/rig.h"
#include "vpp/container.h"

namespace {

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    std::vector<uint8_t> b(static_cast<size_t>(n > 0 ? n : 0));
    if (n > 0) { f.seekg(0); f.read(reinterpret_cast<char*>(b.data()), n); }
    return b;
}

bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView r = c.rawEntryBytes(i);
        out.assign(r.data(), r.data() + r.size());
        return true;
    }
    auto r = c.decompressEntry(i);
    if (r.status != vpp::DecodeStatus::Ok && r.status != vpp::DecodeStatus::OkUnconfirmedContent &&
        r.status != vpp::DecodeStatus::ContentValidated &&
        r.status != vpp::DecodeStatus::RecoveredSharedStream &&
        r.status != vpp::DecodeStatus::RecoveredSharedStreamLongChain) return false;
    out = std::move(r.data);
    return true;
}

struct Triple {
    std::vector<uint8_t> c, g, r;
    bool hasC = false, hasG = false, hasR = false;
};

std::string stemOf(const std::string& name, const char* ext) {
    size_t n = std::strlen(ext);
    if (name.size() > n && name.compare(name.size() - n, n, ext) == 0) return name.substr(0, name.size() - n);
    return std::string();
}

// Collect every triple in this container and, recursively, its nested
// containers. Each entry decode is guarded so one bad entry cannot abort
// its siblings (HANDOFF Sec3 lesson 8).
void collect(const vpp::Container& c, std::map<std::string, Triple>& out, size_t& nestedOpened) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& name = c.entries()[i].name;
        std::string s;
        int which = -1;
        if (!(s = stemOf(name, ".ccmesh_pc")).empty()) which = 0;
        else if (!(s = stemOf(name, ".gcmesh_pc")).empty()) which = 1;
        else if (!(s = stemOf(name, ".rig_pc")).empty()) which = 2;
        if (which < 0) continue;
        try {
            Triple& t = out[s];
            std::vector<uint8_t>& dst = which == 0 ? t.c : which == 1 ? t.g : t.r;
            if (!dst.empty()) continue; // first occurrence wins
            if (!entryBytes(c, i, dst)) continue;
            if (which == 0) t.hasC = true; else if (which == 1) t.hasG = true; else t.hasR = true;
        } catch (const std::exception&) {}
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        try {
            vpp::Container nested = c.openNested(i);
            ++nestedOpened;
            collect(nested, out, nestedOpened);
        } catch (const std::exception&) {}
    }
}

float dist3(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

double median(std::vector<float> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

struct ClusterResult { double medianResidual = 0.0; size_t farVertices = 0; size_t scored = 0; };

// mapping[k] = rig bone for blend index k (identity for the direct reading).
ClusterResult cluster(const std::vector<sr3mesh::Vertex>& verts, const std::vector<uint8_t>& mapping,
                      const std::vector<std::array<float, 3>>& restMesh) {
    ClusterResult r;
    std::vector<float> res;
    for (const auto& v : verts) {
        float best = -1.0f; int slot = -1;
        for (size_t k = 0; k < 4; ++k) {
            uint8_t bi = v.blendIndices[k];
            float w = v.blendWeights[k];
            if (bi == 255 || w <= 0.0f) continue;
            if (w > best) { best = w; slot = bi; }
        }
        if (slot < 0 || static_cast<size_t>(slot) >= mapping.size()) continue;
        uint8_t bone = mapping[static_cast<size_t>(slot)];
        if (bone >= restMesh.size()) continue;
        float d = dist3(v.position, restMesh[bone]);
        res.push_back(d);
        if (d > 0.35f) ++r.farVertices;
    }
    r.scored = res.size();
    r.medianResidual = median(res);
    return r;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: validate_bone_palette <archive.vpp_pc> [more...] [--verbose]\n");
        return 1;
    }
    bool verbose = false;
    std::vector<std::string> archives;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--verbose") verbose = true; else archives.push_back(a);
    }

    size_t triples = 0, complete = 0, parsedOk = 0, skinned = 0, withPalette = 0;
    size_t i1 = 0, i2 = 0, i3 = 0, i4 = 0, i5 = 0, i1to4 = 0;
    size_t cBeatsA = 0, cBeatsB = 0, cBeatsD = 0, cUnder15cm = 0, aUnder15cm = 0;
    size_t cFarZero = 0, aFarZero = 0;
    std::vector<float> medA, medB, medC, medD;
    size_t farA = 0, farB = 0, farC = 0, farD = 0, scoredTotal = 0;
    std::map<int, size_t> slackHistogram; // palette.size()-1 - maxBlendIndex
    std::map<std::string, size_t> failures;
    size_t s1 = 0, s2 = 0, s3 = 0, s4 = 0, singleSet = 0, multiSet = 0;
    std::map<int, size_t> setCountHistogram;
    // Hypothesis E for multi-set meshes: draw group g uses palette set g.
    std::vector<float> medE_multi, medC_multi, medD_multi;
    size_t farE_multi = 0, farC_multi = 0, scoredMulti = 0, eBeatsC = 0, eBeatsD = 0, eGroupsMatchSets = 0;

    for (const std::string& path : archives) {
        std::vector<uint8_t> bytes = readFile(path);
        if (bytes.empty()) { std::fprintf(stderr, "could not read %s\n", path.c_str()); continue; }
        std::map<std::string, Triple> found;
        size_t nestedOpened = 0;
        try {
            vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
            collect(c, found, nestedOpened);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s: %s\n", path.c_str(), e.what());
            continue;
        }
        std::printf("%s: %zu stems with a .ccmesh_pc/.gcmesh_pc/.rig_pc member (%zu nested containers opened)\n",
                    path.c_str(), found.size(), nestedOpened);

        for (auto& kv : found) {
            const std::string& stem = kv.first;
            Triple& t = kv.second;
            ++triples;
            if (!(t.hasC && t.hasG && t.hasR)) continue;
            ++complete;
            try {
                sr3geometry::MaterialBlock material = sr3geometry::MaterialBlock::parse(vpp::ByteView(t.c.data(), t.c.size()));
                sr3geometry::GeometryBlock geometry = sr3geometry::GeometryBlock::parse(vpp::ByteView(t.c.data(), t.c.size()), material);
                if (!geometry.hasMeshSubBlock()) { ++failures["no Mesh sub-block"]; continue; }
                sr3mesh::MeshBlock mesh = sr3mesh::MeshBlock::parse(vpp::ByteView(t.c.data(), t.c.size()),
                                                                    geometry.meshSubBlockOffset(),
                                                                    vpp::ByteView(t.g.data(), t.g.size()));
                sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(t.r.data(), t.r.size()));
                ++parsedOk;
                const auto& bones = rig.bones();

                size_t skinnedChannel = mesh.channels().size();
                for (size_t ch = 0; ch < mesh.channels().size(); ++ch) {
                    if (sr3mesh::layoutInfoFor(mesh.channels()[ch].layoutCode).hasSkinning) { skinnedChannel = ch; break; }
                }
                if (skinnedChannel == mesh.channels().size()) { ++failures["no skinned channel"]; continue; }
                ++skinned;
                std::vector<sr3mesh::Vertex> verts = mesh.decodeChannel(skinnedChannel);

                const std::vector<uint8_t>& palette = mesh.bonePalette();
                const uint16_t declared = mesh.bonePaletteDeclaredCount();
                if (declared == 0) { ++failures["skinned channel but palette count 0"]; continue; }
                ++withPalette;

                int maxBlend = -1;
                for (const auto& v : verts)
                    for (size_t k = 0; k < 4; ++k)
                        if (v.blendIndices[k] != 255 && v.blendWeights[k] > 0.0f && v.blendIndices[k] > maxBlend)
                            maxBlend = v.blendIndices[k];

                bool ok1 = palette.size() == declared;
                bool ok2 = true, ok3 = true;
                for (size_t k = 0; k < palette.size(); ++k) {
                    if (k > 0 && palette[k] <= palette[k - 1]) ok2 = false;
                    if (palette[k] >= bones.size()) ok3 = false;
                }
                bool ok4 = maxBlend >= 0 && static_cast<size_t>(maxBlend) < palette.size();
                bool ok5 = ok4 && static_cast<size_t>(maxBlend) + 1 == palette.size();

                // Palette SETS (header +0x48 count; [u8 count][u8 start] descriptors after the palette).
                const auto& sets = mesh.bonePaletteSets();
                size_t setSum = 0;
                bool startsChain = !sets.empty();
                for (size_t s = 0; s < sets.size(); ++s) {
                    if (sets[s].start != setSum) startsChain = false;
                    setSum += sets[s].count;
                }
                bool okS1 = !sets.empty() && sets.size() == mesh.bonePaletteSetCountDeclared();
                bool okS2 = okS1 && startsChain && setSum == palette.size();
                bool okS3 = okS1;  // every set strictly ascending on its own, every entry < rig bones
                for (const auto& st : sets) {
                    for (size_t k = 0; k < st.count; ++k) {
                        size_t at = static_cast<size_t>(st.start) + k;
                        if (at >= palette.size()) { okS3 = false; break; }
                        if (k > 0 && palette[at] <= palette[at - 1]) okS3 = false;
                        if (palette[at] >= bones.size()) okS3 = false;
                    }
                }
                // Max blend index vs the LARGEST set (a per-set index must fit its own set).
                size_t largestSet = 0;
                for (const auto& st : sets) largestSet = std::max(largestSet, static_cast<size_t>(st.count));
                bool okS4 = okS1 && maxBlend >= 0 && static_cast<size_t>(maxBlend) < largestSet;
                if (okS1) ++s1;
                if (okS2) ++s2;
                if (okS3) ++s3;
                if (okS4) ++s4;
                if (sets.size() == 1) ++singleSet; else if (sets.size() > 1) ++multiSet;
                setCountHistogram[static_cast<int>(sets.size())]++;
                if (ok1) ++i1;
                if (ok2) ++i2;
                if (ok3) ++i3;
                if (ok4) ++i4;
                if (ok5) ++i5;
                if (ok1 && ok2 && ok3 && ok4) ++i1to4;
                if (ok4) ++slackHistogram[static_cast<int>(palette.size()) - 1 - maxBlend];

                // Cluster residuals.
                std::vector<std::array<float, 3>> restRot(bones.size()), restInv(bones.size());
                for (size_t b = 0; b < bones.size(); ++b) {
                    restRot[b] = sr3rig::rigToMeshSpace(bones[b].restPosition);
                    restInv[b] = sr3rig::rigToMeshSpaceInverted(bones[b].restPosition);
                }
                std::vector<uint8_t> identity(bones.size());
                for (size_t b = 0; b < bones.size(); ++b) identity[b] = static_cast<uint8_t>(b);
                std::vector<uint8_t> shuffled = palette;
                {
                    // Deterministic LCG Fisher-Yates so the control is reproducible.
                    uint32_t s = 0x9E3779B9u ^ static_cast<uint32_t>(palette.size());
                    for (size_t k = shuffled.size(); k > 1; --k) {
                        s = s * 1664525u + 1013904223u;
                        size_t j = (s >> 8) % k;
                        std::swap(shuffled[k - 1], shuffled[j]);
                    }
                }
                ClusterResult A = cluster(verts, identity, restRot);
                ClusterResult B = ok1 ? cluster(verts, palette, restRot) : ClusterResult{};
                ClusterResult C = ok1 ? cluster(verts, palette, restInv) : ClusterResult{};
                ClusterResult D = ok1 ? cluster(verts, shuffled, restInv) : ClusterResult{};
                if (ok1) {
                    medA.push_back(static_cast<float>(A.medianResidual));
                    medB.push_back(static_cast<float>(B.medianResidual));
                    medC.push_back(static_cast<float>(C.medianResidual));
                    medD.push_back(static_cast<float>(D.medianResidual));
                    farA += A.farVertices; farB += B.farVertices; farC += C.farVertices; farD += D.farVertices;
                    scoredTotal += C.scored;
                    if (C.medianResidual < A.medianResidual) ++cBeatsA;
                    if (C.medianResidual < B.medianResidual) ++cBeatsB;
                    if (C.medianResidual < D.medianResidual) ++cBeatsD;
                    if (C.medianResidual < 0.15) ++cUnder15cm;
                    if (A.medianResidual < 0.15) ++aUnder15cm;
                    if (C.farVertices == 0) ++cFarZero;
                    if (A.farVertices == 0) ++aFarZero;
                }
                // Multi-set meshes: which SET does each draw range index? Not
                // stated anywhere this reader knows. Measure it instead of
                // guessing: for every draw range, every vertex it references
                // votes for the set whose palette entry (at the vertex's
                // dominant blend index) puts the vertex closest to the
                // bone's rest position. A range whose votes are near-
                // unanimous has a well-defined set; the residual under the
                // per-range majority ("oracle") is the best any per-range
                // rule can do, and the per-range table printed below is
                // what a rule has to reproduce (by material id? by vertex
                // span? by order?).
                ClusterResult E;
                bool eApplicable = sets.size() > 1 && okS2 && mesh.drawGroupsLocated();
                if (eApplicable) {
                    const size_t groupCount = mesh.drawGroups().size();
                    if (groupCount == sets.size()) ++eGroupsMatchSets;
                    auto residualUnderSet = [&](const sr3mesh::Vertex& v, size_t set, float& out) -> bool {
                        float best = -1.0f; int slot = -1;
                        for (size_t k = 0; k < 4; ++k) {
                            if (v.blendIndices[k] == 255 || v.blendWeights[k] <= 0.0f) continue;
                            if (v.blendWeights[k] > best) { best = v.blendWeights[k]; slot = v.blendIndices[k]; }
                        }
                        if (slot < 0 || static_cast<size_t>(slot) >= sets[set].count) return false;
                        uint8_t bone = palette[static_cast<size_t>(sets[set].start) + static_cast<size_t>(slot)];
                        if (bone >= bones.size()) return false;
                        out = dist3(v.position, restInv[bone]);
                        return true;
                    };
                    std::vector<float> res;
                    std::vector<int> setOfVertex(verts.size(), -1);
                    const auto& indices = mesh.indices();
                    std::printf("  %-28s MULTI-SET per-range vote table (set votes / mean residual per set):\n", stem.c_str());
                    for (size_t g = 0; g < groupCount; ++g) {
                        const auto& ranges = mesh.drawGroups()[g];
                        for (size_t r = 0; r < ranges.size(); ++r) {
                            const auto& dr = ranges[r];
                            std::vector<size_t> votes(sets.size(), 0);
                            std::vector<double> sum(sets.size(), 0.0);
                            std::vector<size_t> n(sets.size(), 0);
                            std::vector<char> seen(verts.size(), 0);
                            for (size_t i = dr.startIndex; i < dr.startIndex + dr.indexCount && i < indices.size(); ++i) {
                                uint32_t vi = indices[i];
                                if (vi >= verts.size() || seen[vi]) continue;
                                seen[vi] = 1;
                                float bestD = 1e30f; int bestS = -1;
                                for (size_t s = 0; s < sets.size(); ++s) {
                                    float d;
                                    if (!residualUnderSet(verts[vi], s, d)) continue;
                                    sum[s] += d; ++n[s];
                                    if (d < bestD) { bestD = d; bestS = static_cast<int>(s); }
                                }
                                if (bestS >= 0) ++votes[static_cast<size_t>(bestS)];
                            }
                            size_t majority = 0;
                            for (size_t s = 1; s < sets.size(); ++s) if (votes[s] > votes[majority]) majority = s;
                            for (size_t i = dr.startIndex; i < dr.startIndex + dr.indexCount && i < indices.size(); ++i) {
                                uint32_t vi = indices[i];
                                if (vi < verts.size() && setOfVertex[vi] < 0) setOfVertex[vi] = static_cast<int>(majority);
                            }
                            std::printf("      g%zu r%zu mat=%u sub=%u idx[%u+%u] v[%u..%u]  votes:", g, r, dr.materialId,
                                        dr.submeshIndex, dr.startIndex, dr.indexCount, dr.minVertex, dr.maxVertex);
                            for (size_t s = 0; s < sets.size(); ++s) std::printf(" %zu", votes[s]);
                            std::printf("   mean:");
                            for (size_t s = 0; s < sets.size(); ++s) std::printf(" %.3f", n[s] ? sum[s] / static_cast<double>(n[s]) : -1.0);
                            std::printf("   -> set %zu\n", majority);
                        }
                    }
                    for (size_t vi = 0; vi < verts.size(); ++vi) {
                        if (setOfVertex[vi] < 0) continue;
                        float d;
                        if (!residualUnderSet(verts[vi], static_cast<size_t>(setOfVertex[vi]), d)) continue;
                        res.push_back(d);
                        if (d > 0.35f) ++E.farVertices;
                    }
                    E.scored = res.size();
                    E.medianResidual = median(res);
                    medE_multi.push_back(static_cast<float>(E.medianResidual));
                    medC_multi.push_back(static_cast<float>(C.medianResidual));
                    medD_multi.push_back(static_cast<float>(D.medianResidual));
                    farE_multi += E.farVertices; farC_multi += C.farVertices; scoredMulti += E.scored;
                    if (E.medianResidual < C.medianResidual) ++eBeatsC;
                    if (E.medianResidual < D.medianResidual) ++eBeatsD;
                }

                if (verbose || !(ok1 && ok2 && ok3 && ok4) || sets.size() != 1) {
                    std::printf("  %-28s rig=%3zu verts=%6zu palette=%3zu/%3u maxBlend=%3d  I1=%d I2=%d I3=%d I4=%d I5=%d  "
                                "median A=%.3f B=%.3f C=%.3f D=%.3f  far A=%zu C=%zu  sets=%zu/%u [",
                                stem.c_str(), bones.size(), verts.size(), palette.size(), static_cast<unsigned>(declared),
                                maxBlend, ok1, ok2, ok3, ok4, ok5, A.medianResidual, B.medianResidual,
                                C.medianResidual, D.medianResidual, A.farVertices, C.farVertices,
                                sets.size(), static_cast<unsigned>(mesh.bonePaletteSetCountDeclared()));
                    for (const auto& st : sets) std::printf(" {%u@%u}", static_cast<unsigned>(st.count), static_cast<unsigned>(st.start));
                    std::printf(" ] sum=%zu S2=%d S3=%d S4=%d groups=%zu", setSum, okS2, okS3, okS4,
                                mesh.drawGroupsLocated() ? mesh.drawGroups().size() : 0);
                    if (eApplicable) std::printf("  E(per-range oracle)=%.3f far=%zu", E.medianResidual, E.farVertices);
                    std::printf("\n");
                }
            } catch (const std::exception& e) {
                ++failures[std::string("exception: ") + e.what()];
                if (verbose) std::printf("  %-28s EXCEPTION %s\n", stem.c_str(), e.what());
            }
        }
    }

    auto med = [](std::vector<float> v) { return median(std::move(v)); };
    std::printf("\n=== POPULATION ===\n");
    std::printf("stems seen                       : %zu\n", triples);
    std::printf("complete c/g/rig triples         : %zu\n", complete);
    std::printf("parsed (mesh + rig)              : %zu\n", parsedOk);
    std::printf("with a skinned channel           : %zu\n", skinned);
    std::printf("with a declared bone palette     : %zu\n", withPalette);
    std::printf("I1 complete (size == declared)   : %zu / %zu\n", i1, withPalette);
    std::printf("I2 strictly ascending            : %zu / %zu\n", i2, withPalette);
    std::printf("I3 every entry < rig bone count  : %zu / %zu\n", i3, withPalette);
    std::printf("I4 max blend index < palette size: %zu / %zu\n", i4, withPalette);
    std::printf("I5 max blend index == size - 1   : %zu / %zu  (reported, not required)\n", i5, withPalette);
    std::printf("I1..I4 all hold                  : %zu / %zu\n", i1to4, withPalette);
    std::printf("slack (size-1-maxBlend) histogram:");
    for (auto& kv : slackHistogram) std::printf(" %d:%zu", kv.first, kv.second);
    std::printf("\n");
    std::printf("\n=== PALETTE SETS (header +0x48 count, u16 sizes after the palette) ===\n");
    std::printf("S1 set table complete            : %zu / %zu\n", s1, withPalette);
    std::printf("S2 starts chain, sum == palette  : %zu / %zu\n", s2, withPalette);
    std::printf("S3 each set ascending, in range  : %zu / %zu\n", s3, withPalette);
    std::printf("S4 max blend index < largest set : %zu / %zu\n", s4, withPalette);
    std::printf("single-set meshes                : %zu   multi-set: %zu   set-count histogram:", singleSet, multiSet);
    for (auto& kv : setCountHistogram) std::printf(" %d:%zu", kv.first, kv.second);
    std::printf("\n");
    if (!medE_multi.empty()) {
        std::printf("multi-set per-range ORACLE E (each range's majority-vote set): groups==sets on %zu / %zu; "
                    "median residual E=%.4f vs C(concatenated as one list)=%.4f vs D(shuffled)=%.4f; far E=%zu C=%zu of %zu; "
                    "E beats C on %zu / %zu, E beats D on %zu / %zu\n",
                    eGroupsMatchSets, medE_multi.size(), med(medE_multi), med(medC_multi), med(medD_multi),
                    farE_multi, farC_multi, scoredMulti, eBeatsC, medE_multi.size(), eBeatsD, medE_multi.size());
    }
    std::printf("\n=== CLUSTER RESIDUAL (median over vertices, per mesh; population median of those) ===\n");
    std::printf("  A direct,   (x,-y,-z)    : %.4f m   far(>0.35m) vertices %zu / %zu   meshes with far==0: %zu / %zu   median<0.15m: %zu / %zu\n",
                med(medA), farA, scoredTotal, aFarZero, medA.size(), aUnder15cm, medA.size());
    std::printf("  B palette,  (x,-y,-z)    : %.4f m   far %zu / %zu\n", med(medB), farB, scoredTotal);
    std::printf("  C palette,  (-x,-y,-z)   : %.4f m   far %zu / %zu   meshes with far==0: %zu / %zu   median<0.15m: %zu / %zu\n",
                med(medC), farC, scoredTotal, cFarZero, medC.size(), cUnder15cm, medC.size());
    std::printf("  D shuffled, (-x,-y,-z)   : %.4f m   far %zu / %zu   (control)\n", med(medD), farD, scoredTotal);
    std::printf("  C beats A on %zu / %zu meshes; C beats B on %zu / %zu; C beats D (control) on %zu / %zu\n",
                cBeatsA, medC.size(), cBeatsB, medC.size(), cBeatsD, medC.size());
    if (!failures.empty()) {
        std::printf("\n=== not scored ===\n");
        for (auto& kv : failures) std::printf("  %5zu  %s\n", kv.second, kv.first.c_str());
    }
    return 0;
}
