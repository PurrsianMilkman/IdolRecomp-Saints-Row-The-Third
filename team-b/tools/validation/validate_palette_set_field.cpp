// HANDOFF Sec9.63.7's OPEN item: the per-draw-range BONE-PALETTE SET
// selector, decoded.
//
// CANDIDATE FIELD: immediately after the 20-byte draw-range records
// (no alignment, no gap) there is an array of `totalRanges` 8-byte
// records, one per draw range in the same file order. Byte +0x00 of
// record i is the index of the palette set that draw range i's blend
// indices address.
//
// This harness tests that candidate over the FULL reachable population
// (every .ccmesh_pc/.gcmesh_pc/.rig_pc triple in the archives given),
// single-set and multi-set alike, with four independent lines of
// evidence and three controls:
//
//  STRUCTURAL (no skinning maths involved)
//   T1  the array fits: rangesEnd + totalRanges*8 <= content size
//   T2  every byte +0x00 is a VALID set index (< set count)
//   T3  on single-set meshes every byte +0x00 is 0
//   T4  the array's own end lands on the end of the Mesh sub-block
//       (meshOffset + cLength), i.e. it is exactly what fills the gap
//
//  CONTENT ASSERTION (the strong one - an independent fact the field
//  is not used to compute, in the spirit of spec Sec8.2's min/maxVertex
//  test): the set the field names must be BIG ENOUGH for the blend
//  indices the range's own vertices actually carry.
//   T5  sets[field[i]].count > max blend index used by range i
//
//  SKINNING AGREEMENT
//   T6  per-range agreement with the majority-vote oracle Sec9.63.7
//       established (each vertex votes for the set that puts it nearest
//       its dominant bone's mesh-space rest position)
//   T7  cluster residual under the field's assignment (F) vs the oracle
//       (E), the concatenated-palette reading (C) and a shuffled control
//
//  CONTROLS
//   X1  the same array read one record late (rangesEnd + 8)
//   X2  the same array read one record early (rangesEnd - 8)
//   X3  byte +0x01 of each record instead of byte +0x00
//
// Usage: validate_palette_set_field <archive.vpp_pc> [more...] [--verbose]
#include <algorithm>
#include <array>
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
double median(std::vector<float> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
size_t alignUp(size_t v, size_t a) { return (v + a - 1) / a * a; }
uint32_t rdU32(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8) |
           (static_cast<uint32_t>(b[at + 2]) << 16) | (static_cast<uint32_t>(b[at + 3]) << 24);
}

struct GroupLayout {
    bool located = false;
    size_t groupArrayAt = 0, rangesAt = 0, totalRanges = 0;
    uint32_t groupCount = 0;
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
                if (start != expected || cnt == 0 || mn > mx || mx >= largest) { valid = false; break; }
                expected = start + cnt;
            }
            if (!valid) break;
            if (expected > mesh.indexCount()) { valid = false; break; }
            ++groups;
        }
        if (valid && expected != mesh.indexCount()) valid = false;
        if (valid && groups == groupCount) {
            out.located = true; out.groupArrayAt = cand; out.rangesAt = rangesAt;
            out.groupCount = groupCount; out.totalRanges = total;
            return out;
        }
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: validate_palette_set_field <archive.vpp_pc> [more...] [--verbose]\n");
        return 1;
    }
    bool verbose = false;
    std::vector<std::string> archives;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--verbose") verbose = true; else archives.push_back(a);
    }

    size_t complete = 0, scored = 0, single = 0, multi = 0, multiChannel = 0;
    size_t t5Strict = 0, t4check = 0;
    size_t x1Agree = 0, x2Agree = 0, x3Agree = 0, xRandAgree = 0;
    std::map<long long, size_t> cLenGapHist;
    size_t t6Tie = 0, t6Impossible = 0, t6Cross = 0, t6Genuine = 0, t6MeshesExplained = 0;
    size_t vConflictAll = 0, vConflictG0 = 0, vReached = 0, vTotal = 0, vMeshesClean = 0, vMeshesCleanG0 = 0;
    std::vector<std::string> vConflictMeshes, vConflictG0Meshes, vConflictTriG0Meshes;
    size_t vConflictTri = 0, vConflictTriG0 = 0, vMeshesCleanTri = 0, vMeshesCleanTriG0 = 0;
    std::vector<std::string> t6GenuineMeshes;
    size_t t1 = 0, t2 = 0, t3 = 0, t3den = 0, t4 = 0;
    size_t t5Ranges = 0, t5RangesTotal = 0, t5Meshes = 0;
    size_t t5RangesMulti = 0, t5RangesMultiTotal = 0;
    size_t t6Ranges = 0, t6RangesTotal = 0, t6Meshes = 0, t6MeshDen = 0;
    // controls
    size_t x1Ranges = 0, x2Ranges = 0, x3Ranges = 0, x1Meshes = 0, x2Meshes = 0, x3Meshes = 0;
    size_t xRandRanges = 0, xRandMeshes = 0;
    // trailing 7 bytes
    std::map<int, size_t> byteNonZero;      // which of bytes 1..7 are ever non-zero
    std::map<uint32_t, size_t> setValueHist;
    std::vector<float> medF, medE, medC, medD;
    size_t farF = 0, farE = 0, scoredVerts = 0;
    size_t fBeatsC = 0, fBeatsD = 0, fEqualsE = 0;
    std::map<std::string, size_t> failures;
    std::vector<std::string> t5Failures, t6Failures;

    for (const std::string& path : archives) {
        std::vector<uint8_t> bytes = readFile(path);
        if (bytes.empty()) { std::fprintf(stderr, "could not read %s\n", path.c_str()); continue; }
        std::map<std::string, Triple> found; size_t nested = 0;
        try { vpp::Container c(vpp::ByteView(bytes.data(), bytes.size())); collect(c, found, nested); }
        catch (const std::exception& e) { std::fprintf(stderr, "%s: %s\n", path.c_str(), e.what()); continue; }
        std::printf("%s: %zu stems (%zu nested containers)\n", path.c_str(), found.size(), nested);

        for (auto& kv : found) {
            const std::string& stem = kv.first;
            Triple& t = kv.second;
            if (!(t.hasC && t.hasG && t.hasR)) continue;
            ++complete;
            try {
                sr3geometry::MaterialBlock material = sr3geometry::MaterialBlock::parse(vpp::ByteView(t.c.data(), t.c.size()));
                sr3geometry::GeometryBlock geometry = sr3geometry::GeometryBlock::parse(vpp::ByteView(t.c.data(), t.c.size()), material);
                if (!geometry.hasMeshSubBlock()) { ++failures["no Mesh sub-block"]; continue; }
                const size_t meshOffset = geometry.meshSubBlockOffset();
                sr3mesh::MeshBlock mesh = sr3mesh::MeshBlock::parse(vpp::ByteView(t.c.data(), t.c.size()), meshOffset,
                                                                    vpp::ByteView(t.g.data(), t.g.size()));
                sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(t.r.data(), t.r.size()));
                const auto& sets = mesh.bonePaletteSets();
                const auto& palette = mesh.bonePalette();
                if (sets.empty() || palette.size() != mesh.bonePaletteDeclaredCount()) {
                    ++failures["no/incomplete palette"]; continue;
                }
                std::vector<size_t> skinnedChannels;
                for (size_t ch = 0; ch < mesh.channels().size(); ++ch)
                    if (sr3mesh::layoutInfoFor(mesh.channels()[ch].layoutCode).hasSkinning) skinnedChannels.push_back(ch);
                if (skinnedChannels.empty()) { ++failures["no skinned channel"]; continue; }
                const size_t skinnedChannel = skinnedChannels[0];
                if (skinnedChannels.size() > 1) ++multiChannel;
                GroupLayout gl = locateGroups(t.c, meshOffset, mesh);
                if (!mesh.drawGroupsLocated() || !gl.located) { ++failures["groups not located"]; continue; }
                ++scored;
                if (sets.size() == 1) ++single; else ++multi;

                const size_t rangesEnd = gl.rangesAt + gl.totalRanges * 20;
                const size_t arrayEnd = rangesEnd + gl.totalRanges * 8;
                bool ok1 = arrayEnd <= t.c.size();
                if (ok1) ++t1;
                if (!ok1) { ++failures["T1 array does not fit"]; continue; }

                uint32_t cLen = rdU32(t.c, meshOffset + sr3mesh::kCLengthOffset);
                const size_t meshEnd = meshOffset + cLen;
                if (arrayEnd + 4 == meshEnd) ++t4;
                cLenGapHist[(long long)meshEnd - (long long)arrayEnd]++;
                if (arrayEnd + 4 <= t.c.size() && rdU32(t.c, arrayEnd) == mesh.checkValue()) ++t4check;

                // Decode the field.
                std::vector<uint32_t> field(gl.totalRanges);
                bool ok2 = true, ok3 = true;
                for (size_t i = 0; i < gl.totalRanges; ++i) {
                    field[i] = t.c[rangesEnd + i * 8];
                    setValueHist[field[i]]++;
                    if (field[i] >= sets.size()) ok2 = false;
                    if (sets.size() == 1 && field[i] != 0) ok3 = false;
                    for (int b = 1; b < 8; ++b)
                        if (t.c[rangesEnd + i * 8 + static_cast<size_t>(b)] != 0) byteNonZero[b]++;
                }
                if (ok2) ++t2;
                if (sets.size() == 1) { ++t3den; if (ok3) ++t3; }

                // --- Channel-aware scoring. A mesh may carry more than one
                // SKINNED channel with different vertex counts, and a draw
                // range indexes exactly one of them (alien_e01: channel 0
                // 6,762 verts stride 32, channel 1 2,621 verts stride 36).
                // Scoring every range against channel 0 silently
                // mis-attributes the ranges that draw from channel 1, so
                // every per-range statistic below is computed per channel
                // and the channel is chosen by the data, not assumed. ---
                const auto& indices = mesh.indices();
                std::vector<const sr3mesh::MeshBlock::DrawRange*> flat;
                for (const auto& grp : mesh.drawGroups()) for (const auto& dr : grp) flat.push_back(&dr);
                if (flat.size() != gl.totalRanges) { ++failures["range count mismatch"]; continue; }

                std::vector<std::vector<sr3mesh::Vertex>> chanVerts;
                for (size_t ci : skinnedChannels) chanVerts.push_back(mesh.decodeChannel(ci));
                const auto& bones = rig.bones();
                std::vector<std::array<float, 3>> restInv(bones.size());
                for (size_t b = 0; b < bones.size(); ++b) restInv[b] = sr3rig::rigToMeshSpaceInverted(bones[b].restPosition);

                auto dominantSlot = [](const sr3mesh::Vertex& v) -> int {
                    float best = -1.0f; int slot = -1;
                    for (size_t k = 0; k < 4; ++k) {
                        if (v.blendIndices[k] == 255 || v.blendWeights[k] <= 0.0f) continue;
                        if (v.blendWeights[k] > best) { best = v.blendWeights[k]; slot = v.blendIndices[k]; }
                    }
                    return slot;
                };

                const size_t NC = chanVerts.size(), NR = gl.totalRanges, NS = sets.size();
                std::vector<int> maxBlend(NR * NC, -1);
                std::vector<double> meanRes(NR * NC * NS, -1.0);
                std::vector<size_t> resN(NR * NC * NS, 0);
                for (size_t i = 0; i < NR; ++i) {
                    const auto& dr = *flat[i];
                    for (size_t c = 0; c < NC; ++c) {
                        const auto& cv = chanVerts[c];
                        std::vector<double> sum(NS, 0.0);
                        std::vector<size_t> cnt(NS, 0);
                        std::vector<char> seen(cv.size(), 0);
                        for (size_t k = dr.startIndex; k < static_cast<size_t>(dr.startIndex) + dr.indexCount && k < indices.size(); ++k) {
                            uint32_t vi = indices[k];
                            if (vi >= cv.size() || seen[vi]) continue;
                            seen[vi] = 1;
                            const auto& v = cv[vi];
                            for (size_t w = 0; w < 4; ++w)
                                if (v.blendIndices[w] != 255 && v.blendWeights[w] > 0.0f &&
                                    static_cast<int>(v.blendIndices[w]) > maxBlend[i * NC + c])
                                    maxBlend[i * NC + c] = v.blendIndices[w];
                            int slot = dominantSlot(v);
                            if (slot < 0) continue;
                            for (size_t s = 0; s < NS; ++s) {
                                if (static_cast<size_t>(slot) >= sets[s].count) continue;
                                uint8_t bone = palette[static_cast<size_t>(sets[s].start) + static_cast<size_t>(slot)];
                                if (bone >= bones.size()) continue;
                                sum[s] += dist3(v.position, restInv[bone]); ++cnt[s];
                            }
                        }
                        for (size_t s = 0; s < NS; ++s) {
                            resN[(i * NC + c) * NS + s] = cnt[s];
                            meanRes[(i * NC + c) * NS + s] = cnt[s] ? sum[s] / static_cast<double>(cnt[s]) : -1.0;
                        }
                    }
                }

                // Which sets are INDISTINGUISHABLE for a range: if, for every
                // dominant slot the range's vertices actually use, two sets
                // map that slot to the SAME rig bone, no residual-based
                // oracle can tell them apart and a disagreement between the
                // field and the oracle there is meaningless. Computed
                // exactly, not inferred from a residual tie.
                std::vector<std::vector<uint8_t>> slotsUsed(NR * NC);
                for (size_t i = 0; i < NR; ++i) {
                    const auto& dr = *flat[i];
                    for (size_t c = 0; c < NC; ++c) {
                        const auto& cv = chanVerts[c];
                        std::vector<char> seenSlot(256, 0), seen(cv.size(), 0);
                        for (size_t k = dr.startIndex; k < static_cast<size_t>(dr.startIndex) + dr.indexCount && k < indices.size(); ++k) {
                            uint32_t vi = indices[k];
                            if (vi >= cv.size() || seen[vi]) continue;
                            seen[vi] = 1;
                            int slot = dominantSlot(cv[vi]);
                            if (slot >= 0 && !seenSlot[slot]) { seenSlot[slot] = 1; slotsUsed[i * NC + c].push_back(static_cast<uint8_t>(slot)); }
                        }
                    }
                }
                auto indistinguishable = [&](size_t i, size_t c, size_t s1, size_t s2) {
                    if (s1 == s2) return true;
                    for (uint8_t slot : slotsUsed[i * NC + c]) {
                        if (slot >= sets[s1].count || slot >= sets[s2].count) return false;
                        if (palette[static_cast<size_t>(sets[s1].start) + slot] !=
                            palette[static_cast<size_t>(sets[s2].start) + slot]) return false;
                    }
                    return !slotsUsed[i * NC + c].empty();
                };

                // ORACLE (the ground truth Sec9.63.7 established, generalised
                // to pick the CHANNEL too): per range, the (channel, set)
                // pair whose mapping gives the lowest mean distance from a
                // vertex to its dominant bone's mesh-space rest position.
                std::vector<int> oracleSet(NR, -1), oracleChan(NR, 0);
                std::vector<double> oracleBest(NR, -1.0), oracleRunnerUp(NR, -1.0);
                for (size_t i = 0; i < NR; ++i) {
                    double best = 1e30; int bs = -1, bc = -1;
                    for (size_t c = 0; c < NC; ++c)
                        for (size_t s = 0; s < NS; ++s) {
                            if (resN[(i * NC + c) * NS + s] == 0) continue;
                            double m = meanRes[(i * NC + c) * NS + s];
                            if (m < best) { best = m; bs = static_cast<int>(s); bc = static_cast<int>(c); }
                        }
                    if (bs < 0) continue;
                    oracleSet[i] = bs; oracleChan[i] = bc; oracleBest[i] = best;
                    double second = 1e30;
                    for (size_t s = 0; s < NS; ++s) {
                        if (static_cast<int>(s) == bs) continue;
                        if (resN[(i * NC + static_cast<size_t>(bc)) * NS + s] == 0) continue;
                        second = std::min(second, meanRes[(i * NC + static_cast<size_t>(bc)) * NS + s]);
                    }
                    oracleRunnerUp[i] = second < 1e29 ? second : -1.0;
                }

                // --- T5: the named set must be big enough for the blend
                // indices this range's own vertices actually carry, on at
                // least one skinned channel. `strictChannel0` is the
                // pre-channel-aware reading, kept so the difference the
                // channel awareness makes is visible rather than assumed.
                auto fitsUnder = [&](const std::vector<uint32_t>& f, bool strictChannel0) {
                    size_t ok = 0;
                    for (size_t i = 0; i < NR; ++i) {
                        if (f[i] >= NS) continue;
                        bool sawAny = false, fits = false;
                        for (size_t c = 0; c < NC; ++c) {
                            if (strictChannel0 && c != 0) break;
                            int mb = maxBlend[i * NC + c];
                            if (mb < 0) continue;
                            sawAny = true;
                            if (static_cast<int>(sets[f[i]].count) > mb) { fits = true; break; }
                        }
                        if (!sawAny || fits) ++ok;
                    }
                    return ok;
                };
                // --- V: is a PER-VERTEX remap well defined? A renderer holds
                // one vertex buffer, so it can only apply one palette set
                // per vertex. That is only sound if no vertex is reached by
                // two draw ranges naming DIFFERENT sets. Measured, not
                // assumed - over all ranges, and again over group 0 alone
                // (the LOD level a renderer actually draws). Channel 0 only,
                // which is the channel the renderer uploads.
                {
                    std::vector<int> vset(chanVerts[0].size(), -1);
                    size_t conflictsAll = 0, conflictsG0 = 0, reached = 0;
                    std::vector<int> vsetG0(chanVerts[0].size(), -1);
                    size_t flatIndex = 0;
                    // Counted over the expanded NON-DEGENERATE triangles, not
                    // over the raw strip indices: a strip is stitched to its
                    // neighbour with repeated indices that form zero-area
                    // triangles (spec Sec8.1), so a range's stitch can name a
                    // neighbouring range's vertex without ever drawing it.
                    // Counting raw indices would report conflicts on vertices
                    // no triangle actually uses.
                    std::vector<int> vsetTri(chanVerts[0].size(), -1), vsetTriG0(chanVerts[0].size(), -1);
                    size_t conflictsTri = 0, conflictsTriG0 = 0;
                    for (size_t g = 0; g < mesh.drawGroups().size(); ++g) {
                        for (size_t r = 0; r < mesh.drawGroups()[g].size(); ++r, ++flatIndex) {
                            const auto& dr = mesh.drawGroups()[g][r];
                            int s = static_cast<int>(field[flatIndex]);
                            for (size_t k = dr.startIndex; k < static_cast<size_t>(dr.startIndex) + dr.indexCount && k < indices.size(); ++k) {
                                uint32_t vi = indices[k];
                                if (vi >= vset.size()) continue;
                                if (vset[vi] < 0) { vset[vi] = s; ++reached; }
                                else if (vset[vi] != s) ++conflictsAll;
                                if (g == 0) {
                                    if (vsetG0[vi] < 0) vsetG0[vi] = s;
                                    else if (vsetG0[vi] != s) ++conflictsG0;
                                }
                            }
                            for (uint32_t vi : mesh.triangleListForRange(dr)) {
                                if (vi >= vsetTri.size()) continue;
                                if (vsetTri[vi] < 0) vsetTri[vi] = s;
                                else if (vsetTri[vi] != s) ++conflictsTri;
                                if (g == 0) {
                                    if (vsetTriG0[vi] < 0) vsetTriG0[vi] = s;
                                    else if (vsetTriG0[vi] != s) ++conflictsTriG0;
                                }
                            }
                        }
                    }
                    vConflictTri += conflictsTri; vConflictTriG0 += conflictsTriG0;
                    if (conflictsTri == 0) ++vMeshesCleanTri;
                    if (conflictsTriG0 == 0) ++vMeshesCleanTriG0;
                    if (conflictsTriG0 != 0) vConflictTriG0Meshes.push_back(stem);
                    vConflictAll += conflictsAll; vConflictG0 += conflictsG0;
                    vReached += reached; vTotal += chanVerts[0].size();
                    if (conflictsAll == 0) ++vMeshesClean;
                    if (conflictsG0 == 0) ++vMeshesCleanG0;
                    if (conflictsAll != 0) vConflictMeshes.push_back(stem);
                    if (conflictsG0 != 0) {
                        vConflictG0Meshes.push_back(stem);
                        std::printf("      V CONFLICT(group 0) %-20s %zu vertices claimed by two sets (chans=%zu, sets=%zu)\n",
                                    stem.c_str(), conflictsG0, skinnedChannels.size(), sets.size());
                    }
                }

                size_t fit = fitsUnder(field, false);
                size_t fitStrict = fitsUnder(field, true);
                t5Ranges += fit; t5RangesTotal += NR;
                t5Strict += fitStrict;
                if (NS > 1) { t5RangesMulti += fit; t5RangesMultiTotal += NR; }
                if (fit == NR) ++t5Meshes; else t5Failures.push_back(stem);

                // Controls - only meaningful on multi-set meshes (on a
                // single-set mesh every reading trivially says 0).
                if (NS > 1) {
                    std::vector<uint32_t> x1(NR), x2(NR), x3(NR), xr(NR);
                    uint32_t s = 0x9E3779B9u ^ static_cast<uint32_t>(NR);
                    for (size_t i = 0; i < NR; ++i) {
                        size_t a1 = rangesEnd + (i + 1) * 8;
                        size_t a2 = rangesEnd + i * 8 - 8;
                        x1[i] = a1 < t.c.size() ? t.c[a1] : 0xFFu;
                        x2[i] = (rangesEnd >= 8 && a2 < t.c.size()) ? t.c[a2] : 0xFFu;
                        x3[i] = t.c[rangesEnd + i * 8 + 1];
                        s = s * 1664525u + 1013904223u;
                        xr[i] = (s >> 16) % static_cast<uint32_t>(NS);
                    }
                    size_t f1 = fitsUnder(x1, false), f2 = fitsUnder(x2, false), f3 = fitsUnder(x3, false), fr = fitsUnder(xr, false);
                    x1Ranges += f1; x2Ranges += f2; x3Ranges += f3; xRandRanges += fr;
                    if (f1 == NR) ++x1Meshes;
                    if (f2 == NR) ++x2Meshes;
                    if (f3 == NR) ++x3Meshes;
                    if (fr == NR) ++xRandMeshes;
                    size_t a1c = 0, a2c = 0, a3c = 0, arc = 0;
                    for (size_t i = 0; i < NR; ++i) {
                        if (oracleSet[i] < 0) continue;
                        if (static_cast<int>(x1[i]) == oracleSet[i]) ++a1c;
                        if (static_cast<int>(x2[i]) == oracleSet[i]) ++a2c;
                        if (static_cast<int>(x3[i]) == oracleSet[i]) ++a3c;
                        if (static_cast<int>(xr[i]) == oracleSet[i]) ++arc;
                    }
                    x1Agree += a1c; x2Agree += a2c; x3Agree += a3c; xRandAgree += arc;
                }

                // T6/T7: skinning agreement, multi-set meshes only.
                if (NS > 1) {
                    size_t agree = 0, scoredRanges = 0;
                    size_t disTie = 0, disImpossible = 0, disCrossChannel = 0, disGenuine = 0;
                    for (size_t i = 0; i < NR; ++i) {
                        if (oracleSet[i] < 0) continue;
                        ++scoredRanges;
                        if (static_cast<int>(field[i]) == oracleSet[i]) { ++agree; continue; }
                        // Classify the disagreement honestly rather than
                        // scoring it as a failure of either side.
                        size_t oc = static_cast<size_t>(oracleChan[i]);
                        if (indistinguishable(i, oc, static_cast<size_t>(oracleSet[i]), field[i])) { ++disTie; continue; }
                        int mbOracleChan = maxBlend[i * NC + oc];
                        if (mbOracleChan >= 0 && static_cast<int>(sets[static_cast<size_t>(oracleSet[i])].count) <= mbOracleChan) {
                            // The oracle named a set too small to hold this
                            // range's own blend indices - it scored only the
                            // sub-population of vertices that happened to fit.
                            ++disImpossible; continue;
                        }
                        // Cross-channel: on some OTHER skinned channel the
                        // field's set both fits this range's blend indices
                        // and scores better than the oracle's set does
                        // there. The residual metric cannot compare two
                        // channels fairly - a 6-bone set is sparser than a
                        // 64-bone one and so always shows a larger mean
                        // distance-to-bone - so it picks the wrong channel,
                        // not the wrong set.
                        bool crossOk = false;
                        for (size_t c = 0; c < NC && !crossOk; ++c) {
                            if (c == oc) continue;
                            int mb = maxBlend[i * NC + c];
                            if (mb < 0 || static_cast<int>(sets[field[i]].count) <= mb) continue;
                            double rField = meanRes[(i * NC + c) * NS + field[i]];
                            double rOracle = meanRes[(i * NC + c) * NS + static_cast<size_t>(oracleSet[i])];
                            if (rField >= 0.0 && (rOracle < 0.0 || rField < rOracle)) crossOk = true;
                        }
                        if (crossOk) { ++disCrossChannel; continue; }
                        ++disGenuine;
                        std::printf("      GENUINE DISAGREEMENT %-20s range %2zu field=%u oracle=%d(ch%d) best=%.4f runnerUp=%.4f margin=%.1f%%\n",
                                    stem.c_str(), i, field[i], oracleSet[i], oracleChan[i], oracleBest[i], oracleRunnerUp[i],
                                    oracleRunnerUp[i] > 0.0 ? 100.0 * (oracleRunnerUp[i] - oracleBest[i]) / oracleBest[i] : -1.0);
                    }
                    t6Ranges += agree; t6RangesTotal += scoredRanges; ++t6MeshDen;
                    t6Tie += disTie; t6Impossible += disImpossible; t6Cross += disCrossChannel; t6Genuine += disGenuine;
                    if (agree == scoredRanges) ++t6Meshes;
                    else t6Failures.push_back(stem);
                    if (agree + disTie + disImpossible + disCrossChannel == scoredRanges) ++t6MeshesExplained;
                    else t6GenuineMeshes.push_back(stem);

                    auto rangeResiduals = [&](const std::vector<int>& assignSet, const std::vector<uint8_t>* flatMap, size_t& far) {
                        std::vector<float> res;
                        for (size_t i = 0; i < NR; ++i) {
                            if (oracleSet[i] < 0) continue;
                            const auto& cv = chanVerts[static_cast<size_t>(oracleChan[i])];
                            const auto& dr = *flat[i];
                            std::vector<char> seen(cv.size(), 0);
                            for (size_t k = dr.startIndex; k < static_cast<size_t>(dr.startIndex) + dr.indexCount && k < indices.size(); ++k) {
                                uint32_t vi = indices[k];
                                if (vi >= cv.size() || seen[vi]) continue;
                                seen[vi] = 1;
                                int slot = dominantSlot(cv[vi]);
                                if (slot < 0) continue;
                                uint8_t bone;
                                if (flatMap) {
                                    if (static_cast<size_t>(slot) >= flatMap->size()) continue;
                                    bone = (*flatMap)[static_cast<size_t>(slot)];
                                } else {
                                    int sx = assignSet[i];
                                    if (sx < 0 || static_cast<size_t>(sx) >= NS) continue;
                                    if (static_cast<size_t>(slot) >= sets[static_cast<size_t>(sx)].count) continue;
                                    bone = palette[static_cast<size_t>(sets[static_cast<size_t>(sx)].start) + static_cast<size_t>(slot)];
                                }
                                if (bone >= bones.size()) continue;
                                float d = dist3(cv[vi].position, restInv[bone]);
                                res.push_back(d);
                                if (d > 0.35f) ++far;
                            }
                        }
                        return res;
                    };
                    std::vector<int> fieldI(NR);
                    for (size_t i = 0; i < NR; ++i) fieldI[i] = static_cast<int>(field[i]);
                    std::vector<uint8_t> shuffled = palette;
                    {
                        uint32_t s = 0x9E3779B9u ^ static_cast<uint32_t>(palette.size());
                        for (size_t k = shuffled.size(); k > 1; --k) {
                            s = s * 1664525u + 1013904223u;
                            std::swap(shuffled[k - 1], shuffled[(s >> 8) % k]);
                        }
                    }
                    size_t fFar = 0, eFar = 0, cFar = 0, dFar = 0;
                    double mF = median(rangeResiduals(fieldI, nullptr, fFar));
                    double mE = median(rangeResiduals(oracleSet, nullptr, eFar));
                    double mC = median(rangeResiduals(fieldI, &palette, cFar));
                    double mD = median(rangeResiduals(fieldI, &shuffled, dFar));
                    farF += fFar; farE += eFar;
                    medF.push_back(static_cast<float>(mF)); medE.push_back(static_cast<float>(mE));
                    medC.push_back(static_cast<float>(mC)); medD.push_back(static_cast<float>(mD));
                    if (mF < mC) ++fBeatsC;
                    if (mF < mD) ++fBeatsD;
                    if (mF <= mE + 1e-6) ++fEqualsE;

                    if (verbose || agree != scoredRanges || fit != NR) {
                        std::printf("  %-28s sets=%zu chans=%zu ranges=%zu  T5fit=%zu/%zu T6agree=%zu/%zu  medF=%.4f medE=%.4f medC=%.4f medD=%.4f\n",
                                    stem.c_str(), NS, NC, NR, fit, NR, agree, scoredRanges, mF, mE, mC, mD);
                        for (size_t i = 0; i < NR; ++i) {
                            if (oracleSet[i] < 0 || static_cast<int>(field[i]) == oracleSet[i]) continue;
                            std::printf("      range %2zu field=%u oracle=%d(ch%d)  best=%.4f runnerUp=%.4f  maxBlend[ch0]=%d setCounts",
                                        i, field[i], oracleSet[i], oracleChan[i], oracleBest[i], oracleRunnerUp[i], maxBlend[i * NC]);
                            for (const auto& sd : sets) std::printf(" %u", static_cast<unsigned>(sd.count));
                            std::printf("  v[%u..%u]\n", flat[i]->minVertex, flat[i]->maxVertex);
                        }
                    }
                }
            } catch (const std::exception& e) {
                ++failures[std::string("exception: ") + e.what()];
            }
        }
    }

    auto med = [](std::vector<float> v) { return median(std::move(v)); };
    std::printf("\n=== POPULATION ===\n");
    std::printf("complete triples                  : %zu\n", complete);
    std::printf("scored (palette + skin + groups)   : %zu   single-set %zu   multi-set %zu   (>1 skinned channel: %zu)\n",
                scored, single, multi, multiChannel);
    std::printf("\n=== STRUCTURAL ===\n");
    std::printf("T1 array fits in content           : %zu / %zu\n", t1, scored);
    std::printf("T2 every value < set count         : %zu / %zu\n", t2, scored);
    std::printf("T3 single-set meshes all zero      : %zu / %zu\n", t3, t3den);
    std::printf("T4 arrayEnd + 4 == mesh block end  : %zu / %zu\n", t4, scored);
    std::printf("T4b the 4 bytes at arrayEnd ARE the block's check value: %zu / %zu\n", t4check, scored);
    std::printf("(meshOffset+cLength) - arrayEnd    :");
    for (auto& kv : cLenGapHist) std::printf(" %lld:%zu", kv.first, kv.second);
    std::printf("\n");
    std::printf("value histogram                    :");
    for (auto& kv : setValueHist) std::printf(" %u:%zu", kv.first, kv.second);
    std::printf("\nnon-zero counts for bytes +1..+7   :");
    for (int b = 1; b < 8; ++b) std::printf(" +%d:%zu", b, byteNonZero.count(b) ? byteNonZero[b] : 0);
    std::printf("\n\n=== T5 CONTENT ASSERTION (named set is big enough for the range's own blend indices) ===\n");
    std::printf("ranges passing (all meshes)        : %zu / %zu\n", t5Ranges, t5RangesTotal);
    std::printf("  same, channel-0-only reading     : %zu / %zu  (the difference is the multi-channel meshes)\n", t5Strict, t5RangesTotal);
    std::printf("ranges passing (multi-set only)    : %zu / %zu\n", t5RangesMulti, t5RangesMultiTotal);
    std::printf("meshes fully passing               : %zu / %zu\n", t5Meshes, scored);
    std::printf("  CONTROL X1 (+8, one record late) : %zu / %zu ranges, %zu / %zu meshes\n", x1Ranges, t5RangesMultiTotal, x1Meshes, multi);
    std::printf("  CONTROL X2 (-8, one record early): %zu / %zu ranges, %zu / %zu meshes\n", x2Ranges, t5RangesMultiTotal, x2Meshes, multi);
    std::printf("  CONTROL X3 (byte +1 not +0)      : %zu / %zu ranges, %zu / %zu meshes\n", x3Ranges, t5RangesMultiTotal, x3Meshes, multi);
    std::printf("  CONTROL Xr (random set per range): %zu / %zu ranges, %zu / %zu meshes\n", xRandRanges, t5RangesMultiTotal, xRandMeshes, multi);
    std::printf("\n=== V PER-VERTEX REMAP WELL-DEFINED (channel 0; is one set per vertex enough?) ===\n");
    std::printf("vertex/set conflicts, all groups    : %zu over %zu meshes; clean meshes %zu / %zu\n",
                vConflictAll, scored, vMeshesClean, scored);
    std::printf("vertex/set conflicts, group 0 only  : %zu; clean meshes %zu / %zu\n", vConflictG0, vMeshesCleanG0, scored);
    std::printf("vertices reached by some draw range : %zu / %zu\n", vReached, vTotal);
    std::printf("NON-DEGENERATE TRIANGLES ONLY - conflicts all groups: %zu, clean meshes %zu / %zu\n",
                vConflictTri, vMeshesCleanTri, scored);
    std::printf("NON-DEGENERATE TRIANGLES ONLY - conflicts group 0   : %zu, clean meshes %zu / %zu\n",
                vConflictTriG0, vMeshesCleanTriG0, scored);
    if (!vConflictTriG0Meshes.empty()) {
        std::printf("meshes with a real (triangle) group-0 conflict (%zu):", vConflictTriG0Meshes.size());
        for (const auto& s : vConflictTriG0Meshes) std::printf(" %s", s.c_str());
        std::printf("\n");
    }
    if (!vConflictMeshes.empty()) {
        std::printf("meshes with a conflict (%zu):", vConflictMeshes.size());
        for (const auto& s : vConflictMeshes) std::printf(" %s", s.c_str());
        std::printf("\n");
    }
    std::printf("\n=== T6 AGREEMENT WITH THE MAJORITY-VOTE ORACLE (multi-set only) ===\n");
    std::printf("ranges agreeing                    : %zu / %zu\n", t6Ranges, t6RangesTotal);
    std::printf("meshes agreeing on every range     : %zu / %zu\n", t6Meshes, t6MeshDen);
    std::printf("  CONTROL X1 (+8) agreeing ranges  : %zu / %zu\n", x1Agree, t6RangesTotal);
    std::printf("  CONTROL X2 (-8) agreeing ranges  : %zu / %zu\n", x2Agree, t6RangesTotal);
    std::printf("  CONTROL X3 (byte +1) agreeing    : %zu / %zu  (byte +1 is always 0, so this is the 'always set 0' null)\n", x3Agree, t6RangesTotal);
    std::printf("  CONTROL Xr (random) agreeing     : %zu / %zu\n", xRandAgree, t6RangesTotal);
    std::printf("disagreements classified           : tie(sets map identically) %zu, oracle-set-impossible %zu, cross-channel %zu, GENUINE %zu\n",
                t6Tie, t6Impossible, t6Cross, t6Genuine);
    std::printf("meshes with no GENUINE disagreement: %zu / %zu\n", t6MeshesExplained, t6MeshDen);
    std::printf("\n=== T7 CLUSTER RESIDUAL (multi-set meshes, population median of per-mesh medians) ===\n");
    std::printf("F field-driven   : %.4f m   far(>0.35m) vertices %zu\n", med(medF), farF);
    std::printf("E vote oracle    : %.4f m   far %zu\n", med(medE), farE);
    std::printf("C concatenated   : %.4f m\n", med(medC));
    std::printf("D shuffled (ctrl): %.4f m\n", med(medD));
    std::printf("F beats C on %zu / %zu; F beats D on %zu / %zu; F == E exactly on %zu / %zu\n",
                fBeatsC, medF.size(), fBeatsD, medF.size(), fEqualsE, medF.size());
    if (!t5Failures.empty()) {
        std::printf("\nT5 failing meshes (%zu):", t5Failures.size());
        for (const auto& s : t5Failures) std::printf(" %s", s.c_str());
        std::printf("\n");
    }
    if (!t6GenuineMeshes.empty()) {
        std::printf("meshes with a GENUINE disagreement (%zu):", t6GenuineMeshes.size());
        for (const auto& s : t6GenuineMeshes) std::printf(" %s", s.c_str());
        std::printf("\n");
    }
    if (!t6Failures.empty()) {
        std::printf("T6 failing meshes (%zu):", t6Failures.size());
        for (const auto& s : t6Failures) std::printf(" %s", s.c_str());
        std::printf("\n");
    }
    if (!failures.empty()) {
        std::printf("\n=== not scored ===\n");
        for (auto& kv : failures) std::printf("  %5zu  %s\n", kv.second, kv.first.c_str());
    }
    return 0;
}
