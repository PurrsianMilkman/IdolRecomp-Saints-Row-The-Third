// Cheap pre-render screen for HANDOFF.md Sec9.56.1's open question: is
// severe full-skeleton fragmentation specific to brad.ccmesh_pc's own
// rest-pose geometry, or would it show up on any mesh?
//
// Sec9.56.1 root-caused brad's fragmentation to near-degenerate REST-POSE
// mesh edges connecting vertices weighted to kinematically UNRELATED
// bones (worst pairs: l-finger1 to r-hand, 4-19mm apart in bind pose,
// stretching 175x-291x once both hands move independently). This tool
// measures exactly that property directly from a mesh+rig pair, in BIND
// POSE ONLY (no .anim_pc, no skinning math, no rendering) - so a handful
// of mesh candidates can be ranked BEFORE spending a render on each.
//
// Method: for every pair of vertices whose PRIMARY bone (the blend lane
// with the largest weight) differs, and whose two bones are MORE THAN
// `--min-hops` apart in the rig's parent/child tree (a tree, so the hop
// distance between any two bones is unique - computed once via per-bone
// BFS, bone counts here are ~50-100 so all-pairs BFS is trivial), record
// the pair if their bind-pose mesh-space distance is under `--threshold`
// metres. A uniform 3D grid (cell size == threshold) keeps this to O(n)
// candidate comparisons instead of O(n^2) - standard fixed-radius
// neighbour search, not a new algorithm.
//
// This is a SCREEN, not a proof - it does not run any skinning or
// animation, so it cannot by itself show a clip renders cleanly (only
// looking at the rendered PNG can). It exists to rank candidates cheaply,
// exactly as HANDOFF's task description asks for.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "sr3geometry/geometry_block.h"
#include "sr3geometry/material_block.h"
#include "sr3mesh/mesh_block.h"
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
    if (r.status != vpp::DecodeStatus::Ok &&
        r.status != vpp::DecodeStatus::OkUnconfirmedContent &&
        r.status != vpp::DecodeStatus::ContentValidated &&
        r.status != vpp::DecodeStatus::RecoveredSharedStream &&
        r.status != vpp::DecodeStatus::RecoveredSharedStreamLongChain) return false;
    out = std::move(r.data);
    return true;
}

bool findEntry(const vpp::Container& container, const std::string& name,
               std::vector<uint8_t>& out, std::vector<uint8_t>& siblingOut,
               const std::string& siblingName) {
    for (size_t i = 0; i < container.entries().size(); ++i) {
        if (container.entries()[i].name == name) {
            if (!entryBytes(container, i, out)) return false;
            if (!siblingName.empty()) {
                for (size_t j = 0; j < container.entries().size(); ++j) {
                    if (container.entries()[j].name == siblingName) {
                        entryBytes(container, j, siblingOut);
                        break;
                    }
                }
            }
            return true;
        }
    }
    for (size_t i = 0; i < container.entries().size(); ++i) {
        if (container.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        try {
            vpp::Container nested = container.openNested(i);
            if (findEntry(nested, name, out, siblingOut, siblingName)) return true;
        } catch (const std::exception&) {
        }
    }
    return false;
}

struct FlaggedPair {
    size_t va = 0, vb = 0;
    uint32_t boneA = 0, boneB = 0;
    float distance = 0.0f;
    int hops = 0;
};

// HANDOFF.md Sec9.56.1's dominant mechanism is specifically LEFT-vs-RIGHT
// limb conflict (l-finger1 to r-hand) - the first scenario animating both
// hands independently. 'l-'/'r-' is the naming convention visible
// throughout brad.rig_pc (l-clavicle, r-foot, l-finger1, r-handprop, ...).
// Returns 'L', 'R', or 'C' (centerline/unprefixed - spine, head, camera
// bones, etc., which this project's own finding does NOT implicate).
char sideOf(const std::string& name) {
    if (name.size() >= 2 && (name[0] == 'l' || name[0] == 'L') && name[1] == '-') return 'L';
    if (name.size() >= 2 && (name[0] == 'r' || name[0] == 'R') && name[1] == '-') return 'R';
    return 'C';
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: precheck_topology <characters.vpp_pc> <name.ccmesh_pc> <name.rig_pc>\n"
                     "                          [--threshold metres] [--min-hops N] [--channel N]\n");
        return 1;
    }
    const std::string archivePath = argv[1];
    std::string cmeshName = argv[2];
    const std::string rigName = argv[3];
    float threshold = 0.05f;
    int minHops = 3;
    size_t channelIndex = 0;
    std::string nameFilter;
    for (int i = 4; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--threshold" && i + 1 < argc) threshold = static_cast<float>(std::atof(argv[++i]));
        else if (a == "--min-hops" && i + 1 < argc) minHops = std::atoi(argv[++i]);
        else if (a == "--channel" && i + 1 < argc) channelIndex = static_cast<size_t>(std::atoi(argv[++i]));
        else if (a == "--name-filter" && i + 1 < argc) nameFilter = argv[++i];
    }

    std::string gmeshName = cmeshName;
    size_t dot = gmeshName.find_last_of('.');
    if (dot != std::string::npos && dot + 1 < gmeshName.size()) gmeshName[dot + 1] = 'g';

    std::vector<uint8_t> archive = readFile(archivePath);
    if (archive.empty()) { std::fprintf(stderr, "could not read %s\n", archivePath.c_str()); return 1; }

    std::vector<uint8_t> cb, gb, rb, unusedSibling;
    try {
        vpp::Container container(vpp::ByteView(archive.data(), archive.size()));
        if (!findEntry(container, cmeshName, cb, gb, gmeshName)) {
            std::fprintf(stderr, "could not find '%s'\n", cmeshName.c_str());
            return 1;
        }
        if (!findEntry(container, rigName, rb, unusedSibling, "")) {
            std::fprintf(stderr, "could not find '%s'\n", rigName.c_str());
            return 1;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "archive error: %s\n", ex.what());
        return 1;
    }
    if (gb.empty()) {
        std::fprintf(stderr, "found '%s' but not its paired '%s'\n", cmeshName.c_str(), gmeshName.c_str());
        return 1;
    }

    sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(rb.data(), rb.size()));
    const auto& bones = rig.bones();
    std::printf("rig      : %s (%zu bones)\n", rigName.c_str(), bones.size());

    // All-pairs hop distance over the rig's forest (parent/child edges,
    // undirected for BFS). Bone counts here are ~50-100, so this is
    // O(bones^2), trivial.
    std::vector<std::vector<uint32_t>> adjacency(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        if (!bones[i].isRoot()) {
            adjacency[i].push_back(bones[i].parentIndex);
            adjacency[bones[i].parentIndex].push_back(static_cast<uint32_t>(i));
        }
    }
    std::vector<std::vector<int>> hopDist(bones.size(), std::vector<int>(bones.size(), -1));
    for (size_t s = 0; s < bones.size(); ++s) {
        hopDist[s][s] = 0;
        std::queue<uint32_t> q;
        q.push(static_cast<uint32_t>(s));
        while (!q.empty()) {
            uint32_t cur = q.front(); q.pop();
            for (uint32_t nb : adjacency[cur]) {
                if (hopDist[s][nb] < 0) {
                    hopDist[s][nb] = hopDist[s][cur] + 1;
                    q.push(nb);
                }
            }
        }
    }
    // Report any bone pair unreachable from each other (separate roots /
    // disconnected forest) - treated as "infinitely far" (always flagged
    // if close in space), not folded into the hop number silently.
    int maxFiniteHop = 0;
    size_t disconnectedPairs = 0;
    for (size_t i = 0; i < bones.size(); ++i)
        for (size_t j = i + 1; j < bones.size(); ++j) {
            if (hopDist[i][j] < 0) ++disconnectedPairs;
            else maxFiniteHop = std::max(maxFiniteHop, hopDist[i][j]);
        }

    sr3geometry::MaterialBlock material = sr3geometry::MaterialBlock::parse(vpp::ByteView(cb.data(), cb.size()));
    sr3geometry::GeometryBlock geometry = sr3geometry::GeometryBlock::parse(vpp::ByteView(cb.data(), cb.size()), material);
    if (!geometry.hasMeshSubBlock()) { std::fprintf(stderr, "no Mesh sub-block\n"); return 1; }
    sr3mesh::MeshBlock mesh = sr3mesh::MeshBlock::parse(vpp::ByteView(cb.data(), cb.size()),
                                                        geometry.meshSubBlockOffset(),
                                                        vpp::ByteView(gb.data(), gb.size()));
    if (channelIndex >= mesh.channels().size()) { std::fprintf(stderr, "channel out of range\n"); return 1; }
    if (!sr3mesh::layoutInfoFor(mesh.channels()[channelIndex].layoutCode).hasSkinning) {
        std::fprintf(stderr, "channel %zu carries no skinning\n", channelIndex);
        return 1;
    }
    std::vector<sr3mesh::Vertex> verts = mesh.decodeChannel(channelIndex);
    std::printf("mesh     : %s, %zu vertices (channel %zu)\n", cmeshName.c_str(), verts.size(), channelIndex);

    // Primary bone per vertex: the blend lane with the largest weight
    // (255 == unused lane, skipped). Vertices with no valid lane are
    // dropped from the search (nothing to attribute them to).
    std::vector<int> primaryBone(verts.size(), -1);
    size_t noPrimary = 0;
    for (size_t v = 0; v < verts.size(); ++v) {
        float bestW = -1.0f;
        int bestB = -1;
        for (size_t k = 0; k < 4; ++k) {
            if (verts[v].blendIndices[k] == 255) continue;
            if (verts[v].blendWeights[k] > bestW) {
                bestW = verts[v].blendWeights[k];
                bestB = verts[v].blendIndices[k];
            }
        }
        primaryBone[v] = bestB;
        if (bestB < 0) ++noPrimary;
    }
    if (noPrimary) std::printf("note     : %zu / %zu vertices have no valid blend lane (excluded)\n",
                              noPrimary, verts.size());

    // Uniform grid, cell size == threshold, so any pair under threshold
    // distance is guaranteed to fall in the same or a face/edge/corner-
    // adjacent cell (27-cell neighbourhood in 3D).
    auto cellOf = [&](const std::array<float, 3>& p) -> std::array<int64_t, 3> {
        return {static_cast<int64_t>(std::floor(p[0] / threshold)),
                static_cast<int64_t>(std::floor(p[1] / threshold)),
                static_cast<int64_t>(std::floor(p[2] / threshold))};
    };
    struct CellHash {
        size_t operator()(const std::array<int64_t, 3>& c) const {
            size_t h = std::hash<int64_t>()(c[0]);
            h ^= std::hash<int64_t>()(c[1]) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            h ^= std::hash<int64_t>()(c[2]) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            return h;
        }
    };
    std::unordered_map<std::array<int64_t, 3>, std::vector<uint32_t>, CellHash> grid;
    for (size_t v = 0; v < verts.size(); ++v) {
        if (primaryBone[v] < 0) continue;
        grid[cellOf(verts[v].position)].push_back(static_cast<uint32_t>(v));
    }

    std::vector<FlaggedPair> flagged;      // hop-distance filter only
    std::vector<FlaggedPair> flaggedLR;    // hop-distance AND l-/r- side mismatch
    size_t comparisons = 0;
    for (const auto& entry : grid) {
        const auto& cell = entry.first;
        const auto& here = entry.second;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz) {
                    std::array<int64_t, 3> nc{cell[0] + dx, cell[1] + dy, cell[2] + dz};
                    auto it = grid.find(nc);
                    if (it == grid.end()) continue;
                    const auto& there = it->second;
                    for (uint32_t a : here) {
                        for (uint32_t b : there) {
                            if (b <= a) continue; // count each unordered pair once total
                            ++comparisons;
                            int ba = primaryBone[a], bb = primaryBone[b];
                            if (ba == bb) continue;
                            int hops = hopDist[static_cast<size_t>(ba)][static_cast<size_t>(bb)];
                            bool farInHierarchy = (hops < 0) || (hops > minHops);
                            if (!farInHierarchy) continue;
                            float dx2 = verts[a].position[0] - verts[b].position[0];
                            float dy2 = verts[a].position[1] - verts[b].position[1];
                            float dz2 = verts[a].position[2] - verts[b].position[2];
                            float d = std::sqrt(dx2 * dx2 + dy2 * dy2 + dz2 * dz2);
                            if (d < threshold) {
                                FlaggedPair fp{a, b, static_cast<uint32_t>(ba), static_cast<uint32_t>(bb),
                                              d, hops < 0 ? 999 : hops};
                                flagged.push_back(fp);
                                char sa = sideOf(bones[fp.boneA].name), sb = sideOf(bones[fp.boneB].name);
                                if ((sa == 'L' && sb == 'R') || (sa == 'R' && sb == 'L')) flaggedLR.push_back(fp);
                            }
                        }
                    }
                }
    }
    std::sort(flagged.begin(), flagged.end(),
              [](const FlaggedPair& x, const FlaggedPair& y) { return x.distance < y.distance; });
    std::sort(flaggedLR.begin(), flaggedLR.end(),
              [](const FlaggedPair& x, const FlaggedPair& y) { return x.distance < y.distance; });

    std::printf("hierarchy: max finite hop distance %d, %zu bone pairs in separate roots (of %zu total pairs)\n",
                maxFiniteHop, disconnectedPairs, bones.size() * (bones.size() - 1) / 2);
    std::printf("grid     : %zu cells, %zu candidate comparisons, threshold=%.3fm min-hops=%d\n",
                grid.size(), comparisons, static_cast<double>(threshold), minHops);
    std::printf("RESULT   : %zu flagged near-degenerate cross-limb vertex pairs (any far-hop bone pair)\n",
                flagged.size());
    std::printf("RESULT-LR: %zu of those are ALSO l-/r- side mismatches (the class HANDOFF Sec9.56.1 "
                "root-caused brad's fragmentation to)\n", flaggedLR.size());
    std::printf("-- top pairs, ANY far-hop bone pair --\n");
    size_t shown = 0;
    for (const auto& f : flagged) {
        if (shown >= 10) break;
        std::printf("  %6.1fmm  bone[%3u]=%-16s <-> bone[%3u]=%-16s  hops=%d  (v%zu,v%zu)\n",
                    static_cast<double>(f.distance) * 1000.0, f.boneA,
                    bones[f.boneA].name.c_str(), f.boneB, bones[f.boneB].name.c_str(), f.hops,
                    f.va, f.vb);
        ++shown;
    }
    std::printf("-- top pairs, l-/r- SIDE MISMATCH ONLY (the risky class) --\n");
    shown = 0;
    for (const auto& f : flaggedLR) {
        if (shown >= 15) break;
        std::printf("  %6.1fmm  bone[%3u]=%-16s <-> bone[%3u]=%-16s  hops=%d  (v%zu,v%zu)\n",
                    static_cast<double>(f.distance) * 1000.0, f.boneA,
                    bones[f.boneA].name.c_str(), f.boneB, bones[f.boneB].name.c_str(), f.hops,
                    f.va, f.vb);
        ++shown;
    }
    if (!nameFilter.empty()) {
        std::string nf = nameFilter;
        for (auto& c : nf) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::printf("-- ALL flagged pairs (any far-hop) whose EITHER bone name contains '%s' --\n",
                    nameFilter.c_str());
        for (const auto& f : flagged) {
            std::string na = bones[f.boneA].name, nb = bones[f.boneB].name;
            for (auto& c : na) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (auto& c : nb) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (na.find(nf) == std::string::npos && nb.find(nf) == std::string::npos) continue;
            std::printf("  %6.1fmm  bone[%3u]=%-16s <-> bone[%3u]=%-16s  hops=%d  (v%zu,v%zu)\n",
                        static_cast<double>(f.distance) * 1000.0, f.boneA, bones[f.boneA].name.c_str(),
                        f.boneB, bones[f.boneB].name.c_str(), f.hops, f.va, f.vb);
        }
    }
    if (!flaggedLR.empty()) {
        std::printf("WORST-LR : %.2fmm between bone[%u]=%s and bone[%u]=%s, hops=%d\n",
                    static_cast<double>(flaggedLR.front().distance) * 1000.0, flaggedLR.front().boneA,
                    bones[flaggedLR.front().boneA].name.c_str(), flaggedLR.front().boneB,
                    bones[flaggedLR.front().boneB].name.c_str(), flaggedLR.front().hops);
    } else {
        std::printf("WORST-LR : none found under threshold - no cross-side (l- vs r-) conflict this close\n");
    }
    return 0;
}
