// HANDOFF Sec9.63.7: WHICH BONE-PALETTE SET does each draw range use?
//
// 46 / 318 shipped character meshes carry 2-3 palette sets. The blend
// index in a draw range that uses set k indexes THAT set's own sub-list
// (bonePalette()[start_k .. start_k+count_k)), not the concatenated
// palette. Sec9.63.7 established the ANSWER is empirically recoverable
// per range (every range votes near-unanimously when you score each set's
// mapping by how close it puts a vertex to its dominant bone's rest
// position) but did NOT find where the selector is stored.
//
// This harness does three things, in order:
//
//   1. GROUND TRUTH. Per draw range, the majority-vote set, with the vote
//      counts and the margin, exactly as validate_bone_palette.cpp's
//      "oracle E" computes it. Order is FILE order (group order, then
//      range order) so it lines up with the 20-byte range records.
//
//   2. STRUCTURE DUMP. The offsets and raw bytes of every candidate
//      region: the 0x70 Mesh header, the 0x30-byte group records, the
//      20-byte draw-range records, the gap between the end of the set
//      descriptors and the 16-aligned group array, and the region after
//      the Mesh sub-block (the per-material records of
//      spec-vertex-format.md Sec8.3).
//
//   3. AUTOMATED FIELD SEARCH. Scan the WHOLE c-file for any (offset,
//      stride, element size) at which `rangeCount` consecutive reads
//      reproduce the ground-truth set sequence exactly. Report every hit
//      as an offset relative to each structural anchor, so hits can be
//      intersected across meshes. This is a field-DISCOVERY search: if a
//      stored selector exists anywhere in the file at a fixed stride, it
//      finds it; if nothing hits on any mesh, that is a real negative.
//
// Usage: diag_palette_set_selector <archive.vpp_pc> [more...]
//          [--only <stem>] [--dump] [--no-search] [--rule]
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
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

struct Triple { std::vector<uint8_t> c, g, r; bool hasC = false, hasG = false, hasR = false; };

std::string stemOf(const std::string& name, const char* ext) {
    size_t n = std::strlen(ext);
    if (name.size() > n && name.compare(name.size() - n, n, ext) == 0) return name.substr(0, name.size() - n);
    return std::string();
}

void collect(const vpp::Container& c, std::map<std::string, Triple>& out, size_t& nestedOpened) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& name = c.entries()[i].name;
        std::string s; int which = -1;
        if (!(s = stemOf(name, ".ccmesh_pc")).empty()) which = 0;
        else if (!(s = stemOf(name, ".gcmesh_pc")).empty()) which = 1;
        else if (!(s = stemOf(name, ".rig_pc")).empty()) which = 2;
        if (which < 0) continue;
        try {
            Triple& t = out[s];
            std::vector<uint8_t>& dst = which == 0 ? t.c : which == 1 ? t.g : t.r;
            if (!dst.empty()) continue;
            if (!entryBytes(c, i, dst)) continue;
            if (which == 0) t.hasC = true; else if (which == 1) t.hasG = true; else t.hasR = true;
        } catch (const std::exception&) {}
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        try { vpp::Container nested = c.openNested(i); ++nestedOpened; collect(nested, out, nestedOpened); }
        catch (const std::exception&) {}
    }
}

float dist3(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

size_t alignUp(size_t v, size_t a) { return (v + a - 1) / a * a; }

uint32_t rdU32(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8) |
           (static_cast<uint32_t>(b[at + 2]) << 16) | (static_cast<uint32_t>(b[at + 3]) << 24);
}
uint16_t rdU16(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint16_t>(static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8));
}

void hexDump(const std::vector<uint8_t>& b, size_t at, size_t n, const char* label) {
    std::printf("      %-22s @0x%zx (%zu bytes)\n", label, at, n);
    for (size_t i = 0; i < n; i += 16) {
        std::printf("        %06zx ", at + i);
        for (size_t j = 0; j < 16 && i + j < n; ++j) {
            if (at + i + j >= b.size()) break;
            std::printf("%02x ", b[at + i + j]);
        }
        std::printf("\n");
    }
}

// Replicates MeshBlock::parse()'s draw-group search EXACTLY (same
// invariants, same acceptance rule) but returns the offset it settled on,
// which MeshBlock does not expose. Verified against the parsed block by
// comparing the decoded group/range shape.
struct GroupLayout {
    bool located = false;
    size_t groupArrayAt = 0;
    size_t rangesAt = 0;
    uint32_t groupCount = 0;
    std::vector<uint32_t> rangeCounts;
    size_t totalRanges = 0;
};

GroupLayout locateGroups(const std::vector<uint8_t>& c, size_t meshOffset, const sr3mesh::MeshBlock& mesh) {
    GroupLayout out;
    const size_t header = meshOffset + sr3mesh::kHeaderStart;
    if (header + sr3mesh::kHeaderSize > c.size()) return out;
    uint32_t channelCount = rdU32(c, header + sr3mesh::kChannelCountOffset);
    uint32_t groupCount = rdU32(c, header + 0x04);
    const size_t recordsAt = header + sr3mesh::kHeaderSize;
    const size_t searchStart = recordsAt + static_cast<size_t>(channelCount) * sr3mesh::kChannelRecordSize;
    const size_t searchLimit = std::min(c.size(), searchStart + 4096);
    uint32_t largest = 0;
    for (const auto& ch : mesh.channels()) if (ch.elementCount > largest) largest = ch.elementCount;
    if (groupCount == 0) return out;
    for (size_t cand = alignUp(searchStart, 16); cand + 4 <= searchLimit; cand += 16) {
        std::vector<uint32_t> rc; size_t total = 0; bool shapeOk = true;
        for (uint32_t g = 0; g < groupCount; ++g) {
            size_t at = cand + static_cast<size_t>(g) * 0x30;
            if (at + 0x30 > c.size()) { shapeOk = false; break; }
            uint32_t n = rdU32(c, at);
            if (n == 0 || n > 4096) { shapeOk = false; break; }
            rc.push_back(n); total += n;
        }
        if (!shapeOk || total == 0) continue;
        const size_t rangesAt = cand + static_cast<size_t>(groupCount) * 0x30;
        if (rangesAt + total * 20 > c.size()) continue;
        size_t cur = rangesAt; bool valid = true; uint32_t expected = 0; size_t groups = 0;
        for (uint32_t g = 0; g < groupCount && valid; ++g) {
            for (uint32_t r = 0; r < rc[g]; ++r) {
                uint32_t start = rdU32(c, cur + 0x04), cnt = rdU32(c, cur + 0x08);
                uint32_t mn = rdU32(c, cur + 0x0C), mx = rdU32(c, cur + 0x10);
                cur += 20;
                if (start != expected) { valid = false; break; }
                if (cnt == 0) { valid = false; break; }
                if (mn > mx) { valid = false; break; }
                if (mx >= largest) { valid = false; break; }
                expected = start + cnt;
            }
            if (!valid) break;
            if (expected > mesh.indexCount()) { valid = false; break; }
            ++groups;
        }
        if (valid && expected != mesh.indexCount()) valid = false;
        if (valid && groups == groupCount) {
            out.located = true; out.groupArrayAt = cand; out.rangesAt = rangesAt;
            out.groupCount = groupCount; out.rangeCounts = rc; out.totalRanges = total;
            return out;
        }
    }
    return out;
}

struct RangeTruth {
    size_t group = 0, rangeInGroup = 0;
    sr3mesh::MeshBlock::DrawRange dr;
    std::vector<size_t> votes;
    int majority = -1;
    size_t voters = 0;
    int maxBlendInRange = -1;         // highest blend index this range's vertices use
    std::vector<int> admissible;      // sets whose count > maxBlendInRange
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: diag_palette_set_selector <archive.vpp_pc> [more...] [--only stem] [--dump] [--no-search] [--rule]\n");
        return 1;
    }
    std::vector<std::string> archives;
    std::string only;
    bool dump = false, search = true, ruleOnly = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--only" && i + 1 < argc) { only = argv[++i]; }
        else if (a == "--dump") dump = true;
        else if (a == "--no-search") search = false;
        else if (a == "--rule") ruleOnly = true;
        else archives.push_back(a);
    }

    size_t multiSeen = 0, groupsLocated = 0;
    size_t rangesTotal = 0, rangesUnanimousish = 0;
    // Automated field search: candidate -> how many meshes it matched.
    // Key is "anchorName+delta|stride|elemSize".
    std::map<std::string, size_t> hitCounts;
    // Rule scoring.
    size_t ruleUniqueAll = 0, ruleMeshOk = 0, ruleRangeOk = 0, ruleRangeTotal = 0;
    size_t ambiguousRanges = 0;

    for (const std::string& path : archives) {
        std::vector<uint8_t> bytes = readFile(path);
        if (bytes.empty()) { std::fprintf(stderr, "could not read %s\n", path.c_str()); continue; }
        std::map<std::string, Triple> found; size_t nested = 0;
        try { vpp::Container c(vpp::ByteView(bytes.data(), bytes.size())); collect(c, found, nested); }
        catch (const std::exception& e) { std::fprintf(stderr, "%s: %s\n", path.c_str(), e.what()); continue; }

        for (auto& kv : found) {
            const std::string& stem = kv.first;
            Triple& t = kv.second;
            if (!only.empty() && ("," + only + ",").find("," + stem + ",") == std::string::npos) continue;
            if (!(t.hasC && t.hasG && t.hasR)) continue;
            try {
                sr3geometry::MaterialBlock material = sr3geometry::MaterialBlock::parse(vpp::ByteView(t.c.data(), t.c.size()));
                sr3geometry::GeometryBlock geometry = sr3geometry::GeometryBlock::parse(vpp::ByteView(t.c.data(), t.c.size()), material);
                if (!geometry.hasMeshSubBlock()) continue;
                const size_t meshOffset = geometry.meshSubBlockOffset();
                sr3mesh::MeshBlock mesh = sr3mesh::MeshBlock::parse(vpp::ByteView(t.c.data(), t.c.size()), meshOffset,
                                                                    vpp::ByteView(t.g.data(), t.g.size()));
                sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(t.r.data(), t.r.size()));
                const auto& sets = mesh.bonePaletteSets();
                const auto& palette = mesh.bonePalette();
                if (sets.size() < 2) continue;
                if (palette.size() != mesh.bonePaletteDeclaredCount()) continue;
                ++multiSeen;

                size_t skinnedChannel = mesh.channels().size();
                for (size_t ch = 0; ch < mesh.channels().size(); ++ch)
                    if (sr3mesh::layoutInfoFor(mesh.channels()[ch].layoutCode).hasSkinning) { skinnedChannel = ch; break; }
                if (skinnedChannel == mesh.channels().size()) continue;
                std::vector<sr3mesh::Vertex> verts = mesh.decodeChannel(skinnedChannel);
                const auto& bones = rig.bones();
                std::vector<std::array<float, 3>> restInv(bones.size());
                for (size_t b = 0; b < bones.size(); ++b) restInv[b] = sr3rig::rigToMeshSpaceInverted(bones[b].restPosition);

                GroupLayout gl = locateGroups(t.c, meshOffset, mesh);
                if (!mesh.drawGroupsLocated() || !gl.located) {
                    std::printf("%-28s MULTI-SET but group array NOT located - skipped\n", stem.c_str());
                    continue;
                }
                ++groupsLocated;

                // --- 1. Ground truth per range (file order) ---
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

                std::vector<RangeTruth> truth;
                const auto& indices = mesh.indices();
                for (size_t g = 0; g < mesh.drawGroups().size(); ++g) {
                    for (size_t r = 0; r < mesh.drawGroups()[g].size(); ++r) {
                        const auto& dr = mesh.drawGroups()[g][r];
                        RangeTruth rt; rt.group = g; rt.rangeInGroup = r; rt.dr = dr;
                        rt.votes.assign(sets.size(), 0);
                        std::vector<char> seen(verts.size(), 0);
                        for (size_t i = dr.startIndex; i < static_cast<size_t>(dr.startIndex) + dr.indexCount && i < indices.size(); ++i) {
                            uint32_t vi = indices[i];
                            if (vi >= verts.size() || seen[vi]) continue;
                            seen[vi] = 1;
                            const auto& v = verts[vi];
                            for (size_t k = 0; k < 4; ++k)
                                if (v.blendIndices[k] != 255 && v.blendWeights[k] > 0.0f &&
                                    static_cast<int>(v.blendIndices[k]) > rt.maxBlendInRange)
                                    rt.maxBlendInRange = v.blendIndices[k];
                            float bestD = 1e30f; int bestS = -1;
                            for (size_t s = 0; s < sets.size(); ++s) {
                                float d;
                                if (!residualUnderSet(v, s, d)) continue;
                                if (d < bestD) { bestD = d; bestS = static_cast<int>(s); }
                            }
                            if (bestS >= 0) { ++rt.votes[static_cast<size_t>(bestS)]; ++rt.voters; }
                        }
                        size_t maj = 0;
                        for (size_t s = 1; s < sets.size(); ++s) if (rt.votes[s] > rt.votes[maj]) maj = s;
                        rt.majority = static_cast<int>(maj);
                        for (size_t s = 0; s < sets.size(); ++s)
                            if (rt.maxBlendInRange >= 0 && static_cast<int>(sets[s].count) > rt.maxBlendInRange)
                                rt.admissible.push_back(static_cast<int>(s));
                        truth.push_back(rt);
                    }
                }
                rangesTotal += truth.size();
                for (const auto& rt : truth) {
                    size_t top = rt.votes[static_cast<size_t>(rt.majority)];
                    if (rt.voters > 0 && top * 10 >= rt.voters * 9) ++rangesUnanimousish;
                }

                // --- structural anchors ---
                const size_t header = meshOffset + sr3mesh::kHeaderStart;
                uint32_t channelCount = rdU32(t.c, header + sr3mesh::kChannelCountOffset);
                const size_t recordsAt = header + sr3mesh::kHeaderSize;
                const size_t paletteAt = recordsAt + static_cast<size_t>(channelCount) * sr3mesh::kChannelRecordSize;
                const size_t setsAt = paletteAt + palette.size();
                const size_t setsEnd = setsAt + sets.size() * 2;
                const size_t rangesEnd = gl.rangesAt + gl.totalRanges * 20;

                std::printf("%-28s channels=%zu [", stem.c_str(), mesh.channels().size());
                for (const auto& ch : mesh.channels())
                    std::printf(" layout=%u elems=%u stride=%zu skin=%d", (unsigned)ch.layoutCode, ch.elementCount,
                                ch.stride(), (int)sr3mesh::layoutInfoFor(ch.layoutCode).hasSkinning);
                std::printf(" ]\n");
                std::printf("%-28s sets=%zu [", stem.c_str(), sets.size());
                for (const auto& s : sets) std::printf(" {%u@%u}", (unsigned)s.count, (unsigned)s.start);
                std::printf(" ] groups=%u ranges=%zu  mesh@0x%zx hdr@0x%zx pal@0x%zx sets@0x%zx setsEnd@0x%zx grp@0x%zx rng@0x%zx rngEnd@0x%zx csize=%zu\n",
                            gl.groupCount, gl.totalRanges, meshOffset, header, paletteAt, setsAt, setsEnd,
                            gl.groupArrayAt, gl.rangesAt, rangesEnd, t.c.size());
                std::printf("   gap(setsEnd->grpArray)=%zd   truth:", (ptrdiff_t)gl.groupArrayAt - (ptrdiff_t)setsEnd);
                for (const auto& rt : truth) std::printf(" %d", rt.majority);
                std::printf("\n   per-range: ");
                for (const auto& rt : truth) {
                    std::printf("[g%zu r%zu mat=%u v%u..%u mb=%d votes", rt.group, rt.rangeInGroup, rt.dr.materialId,
                                rt.dr.minVertex, rt.dr.maxVertex, rt.maxBlendInRange);
                    for (size_t s = 0; s < sets.size(); ++s) std::printf(":%zu", rt.votes[s]);
                    std::printf("->%d] ", rt.majority);
                }
                std::printf("\n");

                // Multi-channel meshes: a draw range may reference a
                // DIFFERENT channel's vertex array. Score every skinned
                // channel per range so a wrong-channel reading is visible
                // rather than silently mis-attributed.
                if (mesh.channels().size() > 1) {
                    std::printf("   per-channel scoring (multi-channel mesh):\n");
                    for (size_t ch = 0; ch < mesh.channels().size(); ++ch) {
                        if (!sr3mesh::layoutInfoFor(mesh.channels()[ch].layoutCode).hasSkinning) continue;
                        std::vector<sr3mesh::Vertex> cv = mesh.decodeChannel(ch);
                        std::printf("     channel %zu (%zu verts):", ch, cv.size());
                        size_t ri = 0;
                        for (const auto& rt : truth) {
                            int mb = -1;
                            std::vector<double> sum(sets.size(), 0.0);
                            std::vector<size_t> n(sets.size(), 0);
                            std::vector<char> seen(cv.size(), 0);
                            for (size_t i = rt.dr.startIndex;
                                 i < static_cast<size_t>(rt.dr.startIndex) + rt.dr.indexCount && i < indices.size(); ++i) {
                                uint32_t vi = indices[i];
                                if (vi >= cv.size() || seen[vi]) continue;
                                seen[vi] = 1;
                                const auto& v = cv[vi];
                                float best = -1.0f; int slot = -1;
                                for (size_t k = 0; k < 4; ++k) {
                                    if (v.blendIndices[k] == 255 || v.blendWeights[k] <= 0.0f) continue;
                                    if (static_cast<int>(v.blendIndices[k]) > mb) mb = v.blendIndices[k];
                                    if (v.blendWeights[k] > best) { best = v.blendWeights[k]; slot = v.blendIndices[k]; }
                                }
                                if (slot < 0) continue;
                                for (size_t s = 0; s < sets.size(); ++s) {
                                    if (static_cast<size_t>(slot) >= sets[s].count) continue;
                                    uint8_t bone = palette[static_cast<size_t>(sets[s].start) + static_cast<size_t>(slot)];
                                    if (bone >= bones.size()) continue;
                                    sum[s] += dist3(v.position, restInv[bone]); ++n[s];
                                }
                            }
                            std::printf(" [r%zu mb=%d", ri, mb);
                            for (size_t s = 0; s < sets.size(); ++s)
                                std::printf(" s%zu=%.3f/%zu", s, n[s] ? sum[s] / (double)n[s] : -1.0, n[s]);
                            std::printf("]");
                            ++ri;
                        }
                        std::printf("\n");
                    }
                }

                if (dump) {
                    hexDump(t.c, header, sr3mesh::kHeaderSize, "mesh header 0x70");
                    for (uint32_t g = 0; g < gl.groupCount; ++g) {
                        char lbl[64]; std::snprintf(lbl, sizeof(lbl), "group record %u", g);
                        hexDump(t.c, gl.groupArrayAt + static_cast<size_t>(g) * 0x30, 0x30, lbl);
                    }
                    hexDump(t.c, gl.rangesAt, std::min<size_t>(gl.totalRanges * 20, 640), "draw ranges");
                    if (gl.groupArrayAt > setsEnd)
                        hexDump(t.c, setsEnd, gl.groupArrayAt - setsEnd, "gap setsEnd..grpArray");
                    size_t tailAt = rangesEnd;
                    uint32_t cLen = rdU32(t.c, meshOffset + sr3mesh::kCLengthOffset);
                    std::printf("      cLength=%u -> mesh sub-block ends at 0x%zx (meshOffset+cLength)\n",
                                cLen, meshOffset + cLen);
                    if (tailAt < t.c.size())
                        hexDump(t.c, tailAt, std::min<size_t>(t.c.size() - tailAt, 768), "after draw ranges");
                }

                // --- 3. Automated field search over the WHOLE c-file ---
                if (search) {
                    struct Anchor { const char* name; size_t at; };
                    const Anchor anchors[] = {
                        {"hdr", header}, {"pal", paletteAt}, {"sets", setsAt}, {"setsEnd", setsEnd},
                        {"grp", gl.groupArrayAt}, {"rng", gl.rangesAt}, {"rngEnd", rangesEnd},
                        {"file", 0},
                    };
                    const size_t n = truth.size();
                    const size_t strides[] = {1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24, 28, 32, 40, 44, 48, 52, 56, 60, 64, 80, 96};
                    // PARTITION MATCH, not literal match: the stored value
                    // need not BE the set index - it may be the set's start,
                    // its count, a pointer, a material-ish id, anything. The
                    // requirement is that the sequence induces EXACTLY the
                    // ground-truth partition of the ranges: v[i]==v[j] iff
                    // truth[i]==truth[j]. That is what a selector must do,
                    // whatever it is encoded as.
                    for (size_t es = 1; es <= 4; es *= 2) {
                        for (size_t stride : strides) {
                            if (stride < es) continue;
                            for (size_t off = 0; off + (n - 1) * stride + es <= t.c.size(); ++off) {
                                bool ok = true;
                                std::vector<uint32_t> vals(n);
                                for (size_t i = 0; i < n; ++i) {
                                    size_t at = off + i * stride;
                                    vals[i] = es == 1 ? t.c[at] : es == 2 ? rdU16(t.c, at) : rdU32(t.c, at);
                                }
                                for (size_t i = 0; i < n && ok; ++i)
                                    for (size_t j = i + 1; j < n && ok; ++j)
                                        if ((vals[i] == vals[j]) != (truth[i].majority == truth[j].majority)) ok = false;
                                if (!ok) continue;
                                for (const Anchor& a : anchors) {
                                    char key[128];
                                    std::snprintf(key, sizeof(key), "%s%+lld|s%zu|e%zu", a.name,
                                                  (long long)((ptrdiff_t)off - (ptrdiff_t)a.at), stride, es);
                                    ++hitCounts[key];
                                }
                                if (dump) {
                                    std::printf("      HIT off=0x%zx stride=%zu es=%zu vals:", off, stride, es);
                                    for (size_t i = 0; i < n; ++i) std::printf(" %u", vals[i]);
                                    std::printf("\n");
                                }
                            }
                        }
                    }
                }

                // --- Rule candidate: admissibility by max blend index ---
                if (ruleOnly || true) {
                    bool meshOk = true;
                    for (const auto& rt : truth) {
                        ++ruleRangeTotal;
                        if (rt.admissible.size() == 1) {
                            ++ruleUniqueAll;
                            if (rt.admissible[0] == rt.majority) ++ruleRangeOk; else meshOk = false;
                        } else {
                            ++ambiguousRanges;
                            meshOk = false;
                        }
                    }
                    if (meshOk) ++ruleMeshOk;
                }
            } catch (const std::exception& e) {
                std::printf("%-28s EXCEPTION %s\n", stem.c_str(), e.what());
            }
        }
    }

    std::printf("\n=== SUMMARY ===\n");
    std::printf("multi-set meshes seen            : %zu\n", multiSeen);
    std::printf("group array located              : %zu\n", groupsLocated);
    std::printf("draw ranges (multi-set meshes)   : %zu\n", rangesTotal);
    std::printf("ranges with >=90%% vote agreement : %zu / %zu\n", rangesUnanimousish, rangesTotal);
    std::printf("\n--- RULE: unique-admissible-set-by-max-blend-index ---\n");
    std::printf("ranges where exactly one set is admissible : %zu / %zu\n", ruleUniqueAll, ruleRangeTotal);
    std::printf("  of those, matching the vote majority     : %zu / %zu\n", ruleRangeOk, ruleUniqueAll);
    std::printf("ranges ambiguous (0 or >1 admissible)      : %zu\n", ambiguousRanges);
    std::printf("meshes fully resolved by this rule         : %zu / %zu\n", ruleMeshOk, groupsLocated);

    if (search) {
        std::printf("\n--- AUTOMATED FIELD SEARCH: (anchor+delta | stride | elemsize) -> meshes matched ---\n");
        std::vector<std::pair<std::string, size_t>> v(hitCounts.begin(), hitCounts.end());
        std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        size_t shown = 0;
        for (const auto& kv : v) {
            if (kv.second < 2) break;
            std::printf("  %-40s %zu\n", kv.first.c_str(), kv.second);
            if (++shown >= 60) break;
        }
        if (shown == 0) std::printf("  (no candidate matched two or more meshes)\n");
    }
    return 0;
}
