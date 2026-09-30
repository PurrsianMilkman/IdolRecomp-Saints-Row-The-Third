// Measures, numerically, whether a .ccmesh_pc's OWN stored (unskinned)
// vertex positions actually sit at its paired .rig_pc's bind pose.
//
// WHY THIS EXISTS. HANDOFF.md Sec9.56.2 records a confirmed but
// unexplained anomaly: `reggies.ccmesh_pc` renders plain (no skinning) in
// an arms-down stance while `reggies.rig_pc`'s own rest positions were
// reported as the same arms-extended T-pose as brad/angel/joleen. That
// finding rests on a rendered PNG plus a bounding-box pair; this tool
// replaces the eyeball with per-bone numbers, and prints its own
// provenance so a file-selection mismatch between two separate tools
// cannot masquerade as a data anomaly.
//
// WHAT IT MEASURES (nothing here is new math - every transform comes from
// sr3rig/sr3anim, which HANDOFF Sec9.53/Sec9.56.1 already confirm):
//
//  1. PROVENANCE - which nested container inside the archive each of
//     ccmesh/gcmesh/rig was actually read from, and their byte sizes, so
//     "did both earlier tools read the same bytes" is answerable directly.
//
//  2. MESH EXTENT - the AABB of the decoded channel's vertices, both over
//     ALL vertices and over only those referenced by one draw group's
//     index ranges (the renderer's own `bounds` line is computed over the
//     uploaded group, so both are printed rather than assumed equal).
//
//  3. PER-BONE CLUSTER RESIDUAL - the decisive one. For every bone, the
//     centroid of the mesh vertices whose PRIMARY blend lane (largest
//     weight, 255 = unused) is that bone, compared against that bone's own
//     rest position converted to mesh space via sr3rig::rigToMeshSpace()
//     (HANDOFF Sec9.13's confirmed convention). A mesh genuinely stored at
//     its rig's bind pose puts each cluster within roughly a limb radius
//     of its bone; a mesh stored in a DIFFERENT pose separates them by the
//     amount that pose moved the limb. Printed per bone, so the anomaly is
//     localized to specific joints instead of being a whole-body
//     impression.
//
//  4. OPTIONAL ANIMATED COMPARISON - with --anim-archive/--clip, the same
//     per-bone residual recomputed against bone positions POSED by a real
//     .anim_pc frame, using sr3anim::sampleClipAtTime() +
//     sr3rig::computeAnimatedSkinningMatrices() + conjugateToMeshSpace()
//     exactly as tools/sr3_viewer.cpp's runAnimPose() does. This tests the
//     hypothesis "the mesh is stored at some clip's frame-0 pose" without
//     writing any new skinning math: if some clip's frame reproduces the
//     stored stance, the residual collapses.
//
// It computes statistics for a human to read; it asserts no pass/fail.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "sr3anim/animation.h"
#include "sr3anim/payload.h"
#include "sr3anim/sample.h"
#include "sr3geometry/geometry_block.h"
#include "sr3geometry/material_block.h"
#include "sr3mesh/mesh_block.h"
#include "sr3rig/animated_pose.h"
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
    if (r.status != vpp::DecodeStatus::Ok &&
        r.status != vpp::DecodeStatus::OkUnconfirmedContent &&
        r.status != vpp::DecodeStatus::ContentValidated &&
        r.status != vpp::DecodeStatus::RecoveredSharedStream &&
        r.status != vpp::DecodeStatus::RecoveredSharedStreamLongChain) return false;
    out = std::move(r.data);
    return true;
}

// Same recursive search tools/sr3_viewer.cpp and precheck_topology.cpp use,
// with one addition: it records the container path it found the entry in,
// so this tool can PRINT where its bytes came from instead of assuming.
bool findEntry(const vpp::Container& container, const std::string& name,
               std::vector<uint8_t>& out, std::vector<uint8_t>& siblingOut,
               const std::string& siblingName, const std::string& pathSoFar,
               std::string& foundPath, size_t* foundIndex, size_t* siblingIndex) {
    for (size_t i = 0; i < container.entries().size(); ++i) {
        if (container.entries()[i].name == name) {
            if (!entryBytes(container, i, out)) return false;
            foundPath = pathSoFar;
            if (foundIndex) *foundIndex = i;
            if (!siblingName.empty()) {
                for (size_t j = 0; j < container.entries().size(); ++j) {
                    if (container.entries()[j].name == siblingName) {
                        entryBytes(container, j, siblingOut);
                        if (siblingIndex) *siblingIndex = j;
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
            if (findEntry(nested, name, out, siblingOut, siblingName,
                          pathSoFar + "/" + container.entries()[i].name, foundPath,
                          foundIndex, siblingIndex))
                return true;
        } catch (const std::exception&) {
        }
    }
    return false;
}

struct Aabb {
    std::array<double, 3> lo{1e30, 1e30, 1e30};
    std::array<double, 3> hi{-1e30, -1e30, -1e30};
    size_t n = 0;
    void add(const std::array<float, 3>& p) {
        for (int k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], static_cast<double>(p[k]));
            hi[k] = std::max(hi[k], static_cast<double>(p[k]));
        }
        ++n;
    }
    void print(const char* label) const {
        if (n == 0) { std::printf("%-22s (empty)\n", label); return; }
        std::printf("%-22s n=%-7zu extent = %.4f x %.4f x %.4f   "
                    "min(%.4f,%.4f,%.4f) max(%.4f,%.4f,%.4f)\n",
                    label, n, hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2],
                    lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
    }
};

double dist3(const std::array<double, 3>& a, const std::array<float, 3>& b) {
    double dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// --- Population sweep -------------------------------------------------
//
// Runs the two MAPPING-FREE pose measures over every <stem>.ccmesh_pc /
// <stem>.gcmesh_pc / <stem>.rig_pc triple in an archive, so "this one
// asset is stored in an unusual stance" is a claim with a denominator
// instead of a comparison against two hand-picked neighbours.
//
//   armDroopMesh : angle below horizontal from the rig's own l-clavicle
//                  rest position to the mesh's own extreme +X vertex -
//                  i.e. where the stored geometry actually puts the
//                  outermost point of the left arm.
//   armDroopRig  : the same angle from l-clavicle to l-hand, taken purely
//                  from the rig's rest positions.
//
// A T-pose scores ~0 deg on both; an A-pose scores the arm's droop. The
// two being close means the mesh is stored in the stance its own rig
// describes, whatever that stance is.
void sweepContainer(const vpp::Container& container, const std::string& path, size_t& printed) {
    for (size_t i = 0; i < container.entries().size(); ++i) {
        const std::string& nm = container.entries()[i].name;
        if (nm.size() < 11 || nm.compare(nm.size() - 10, 10, ".ccmesh_pc") != 0) continue;
        const std::string stem = nm.substr(0, nm.size() - 10);
        std::string gName = stem + ".gcmesh_pc", rName = stem + ".rig_pc";
        size_t gi = container.entries().size(), ri = container.entries().size();
        for (size_t j = 0; j < container.entries().size(); ++j) {
            if (container.entries()[j].name == gName) gi = j;
            if (container.entries()[j].name == rName) ri = j;
        }
        if (gi == container.entries().size() || ri == container.entries().size()) continue;
        std::vector<uint8_t> cb, gb, rb;
        if (!entryBytes(container, i, cb) || !entryBytes(container, gi, gb) ||
            !entryBytes(container, ri, rb))
            continue;
        try {
            sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(rb.data(), rb.size()));
            int lc = rig.findBone("l-clavicle"), lh = rig.findBone("l-hand");
            if (lc < 0 || lh < 0) continue;
            sr3geometry::MaterialBlock material =
                sr3geometry::MaterialBlock::parse(vpp::ByteView(cb.data(), cb.size()));
            sr3geometry::GeometryBlock geometry =
                sr3geometry::GeometryBlock::parse(vpp::ByteView(cb.data(), cb.size()), material);
            if (!geometry.hasMeshSubBlock()) continue;
            sr3mesh::MeshBlock mesh = sr3mesh::MeshBlock::parse(
                vpp::ByteView(cb.data(), cb.size()), geometry.meshSubBlockOffset(),
                vpp::ByteView(gb.data(), gb.size()));
            if (mesh.channels().empty()) continue;
            if (!sr3mesh::layoutInfoFor(mesh.channels()[0].layoutCode).hasSkinning) continue;
            std::vector<sr3mesh::Vertex> verts = mesh.decodeChannel(0);
            if (verts.size() < 100) continue;
            std::array<float, 3> clav = sr3rig::rigToMeshSpace(rig.bones()[lc].restPosition);
            std::array<float, 3> hand = sr3rig::rigToMeshSpace(rig.bones()[lh].restPosition);
            Aabb box; size_t ixMax = 0;
            for (size_t v = 0; v < verts.size(); ++v) {
                box.add(verts[v].position);
                if (verts[v].position[0] > verts[ixMax].position[0]) ixMax = v;
            }
            double mdx = static_cast<double>(verts[ixMax].position[0]) - clav[0];
            double mdy = static_cast<double>(clav[1]) - verts[ixMax].position[1];
            double rdx = static_cast<double>(hand[0]) - clav[0];
            double rdy = static_cast<double>(clav[1]) - hand[1];
            const double kRad = 180.0 / 3.14159265358979;
            int rootIdx = -1, bendIdx = rig.findBone("spinebend");
            for (size_t b = 0; b < rig.bones().size(); ++b)
                if (rig.bones()[b].isRoot()) { rootIdx = static_cast<int>(b); break; }
            std::array<float, 3> rootPos{0, 0, 0}, bendPos{0, 0, 0};
            if (rootIdx >= 0) rootPos = rig.bones()[rootIdx].restPosition;
            if (bendIdx >= 0) bendPos = rig.bones()[bendIdx].restPosition;
            std::printf("%-34s bones=%-4zu verts=%-6zu extentXY=%.4f x %.4f  maxXv=(%.4f,%.4f)  "
                        "droopMesh=%7.2f  droopRig=%7.2f  diff=%6.2f  "
                        "root=(%.4f,%.4f,%.4f) spinebend=(%.4f,%.4f,%.4f)\n",
                        stem.c_str(), rig.bones().size(), verts.size(), box.hi[0] - box.lo[0],
                        box.hi[1] - box.lo[1], verts[ixMax].position[0], verts[ixMax].position[1],
                        std::atan2(mdy, mdx) * kRad, std::atan2(rdy, rdx) * kRad,
                        std::atan2(mdy, mdx) * kRad - std::atan2(rdy, rdx) * kRad,
                        rootPos[0], rootPos[1], rootPos[2], bendPos[0], bendPos[1], bendPos[2]);
            ++printed;
        } catch (const std::exception&) {
        }
    }
    for (size_t i = 0; i < container.entries().size(); ++i) {
        if (container.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        try {
            vpp::Container nested = container.openNested(i);
            sweepContainer(nested, path + "/" + container.entries()[i].name, printed);
        } catch (const std::exception&) {
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[2]) == "--sweep") {
        std::vector<uint8_t> archive = readFile(argv[1]);
        if (archive.empty()) { std::fprintf(stderr, "could not read %s\n", argv[1]); return 1; }
        size_t printed = 0;
        try {
            vpp::Container container(vpp::ByteView(archive.data(), archive.size()));
            sweepContainer(container, "", printed);
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "archive error: %s\n", ex.what()); return 1;
        }
        std::printf("SWEEP: %zu mesh/rig pairs measured\n", printed);
        return 0;
    }
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: probe_bindpose_residual <archive.vpp_pc> <name.ccmesh_pc> "
                     "<name.rig_pc>\n"
                     "         [--channel N] [--group N] [--min-cluster N] [--top N]\n"
                     "         [--anim-archive <path> --clip <name.anim_pc> [--frac F]]\n");
        return 1;
    }
    const std::string archivePath = argv[1];
    const std::string cmeshName = argv[2];
    const std::string rigName = argv[3];
    size_t channelIndex = 0, drawGroup = 0, minCluster = 20, topN = 200;
    std::string animArchivePath, clipName;
    float frac = 0.0f;
    for (int i = 4; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--channel" && i + 1 < argc) channelIndex = static_cast<size_t>(std::atoi(argv[++i]));
        else if (a == "--group" && i + 1 < argc) drawGroup = static_cast<size_t>(std::atoi(argv[++i]));
        else if (a == "--min-cluster" && i + 1 < argc) minCluster = static_cast<size_t>(std::atoi(argv[++i]));
        else if (a == "--top" && i + 1 < argc) topN = static_cast<size_t>(std::atoi(argv[++i]));
        else if (a == "--anim-archive" && i + 1 < argc) animArchivePath = argv[++i];
        else if (a == "--clip" && i + 1 < argc) clipName = argv[++i];
        else if (a == "--frac" && i + 1 < argc) frac = static_cast<float>(std::atof(argv[++i]));
        else { std::fprintf(stderr, "unrecognised argument '%s'\n", a.c_str()); return 2; }
    }

    std::string gmeshName = cmeshName;
    size_t dot = gmeshName.find_last_of('.');
    if (dot != std::string::npos && dot + 1 < gmeshName.size()) gmeshName[dot + 1] = 'g';

    std::vector<uint8_t> archive = readFile(archivePath);
    if (archive.empty()) { std::fprintf(stderr, "could not read %s\n", archivePath.c_str()); return 1; }

    std::vector<uint8_t> cb, gb, rb, unused;
    std::string cmeshPath, rigPath;
    size_t cIdx = 0, gIdx = 0, rIdx = 0;
    try {
        vpp::Container container(vpp::ByteView(archive.data(), archive.size()));
        if (!findEntry(container, cmeshName, cb, gb, gmeshName, "", cmeshPath, &cIdx, &gIdx)) {
            std::fprintf(stderr, "could not find '%s'\n", cmeshName.c_str()); return 1;
        }
        if (!findEntry(container, rigName, rb, unused, "", "", rigPath, &rIdx, nullptr)) {
            std::fprintf(stderr, "could not find '%s'\n", rigName.c_str()); return 1;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "archive error: %s\n", ex.what()); return 1;
    }
    if (gb.empty()) { std::fprintf(stderr, "no paired '%s'\n", gmeshName.c_str()); return 1; }

    std::printf("== PROVENANCE ==\n");
    std::printf("archive  : %s\n", archivePath.c_str());
    std::printf("ccmesh   : %s  <- container '%s' entry #%zu, %zu bytes\n",
                cmeshName.c_str(), cmeshPath.empty() ? "(top level)" : cmeshPath.c_str(), cIdx, cb.size());
    std::printf("gcmesh   : %s  <- same container entry #%zu, %zu bytes\n",
                gmeshName.c_str(), gIdx, gb.size());
    std::printf("rig      : %s  <- container '%s' entry #%zu, %zu bytes\n",
                rigName.c_str(), rigPath.empty() ? "(top level)" : rigPath.c_str(), rIdx, rb.size());
    std::printf("same container for mesh and rig: %s\n",
                cmeshPath == rigPath ? "YES" : "NO  <-- file-pairing mismatch");

    // Raw header bytes, so a structural difference between two .ccmesh_pc
    // files can be seen rather than inferred from parsed fields alone.
    // Printed, not interpreted - no meaning is assigned to any byte the
    // spec does not already assign one to.
    {
        std::printf("ccmesh head (first 0x60 bytes):\n");
        for (size_t off = 0; off < 0x60 && off < cb.size(); off += 16) {
            std::printf("  %04zX:", off);
            for (size_t k = 0; k < 16 && off + k < cb.size(); ++k)
                std::printf(" %02X", cb[off + k]);
            std::printf("\n");
        }
    }

    sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(rb.data(), rb.size()));
    const auto& bones = rig.bones();
    std::printf("rig bones: %zu, hash table %s, field_0x2C=%u, attachments=%zu\n",
                bones.size(), rig.hashTableMatchesNames() ? "OK" : "MISMATCH",
                rig.leadingGroupCount(), rig.attachments().size());

    sr3geometry::MaterialBlock material =
        sr3geometry::MaterialBlock::parse(vpp::ByteView(cb.data(), cb.size()));
    sr3geometry::GeometryBlock geometry =
        sr3geometry::GeometryBlock::parse(vpp::ByteView(cb.data(), cb.size()), material);
    if (!geometry.hasMeshSubBlock()) { std::fprintf(stderr, "no Mesh sub-block\n"); return 1; }
    sr3mesh::MeshBlock mesh =
        sr3mesh::MeshBlock::parse(vpp::ByteView(cb.data(), cb.size()),
                                  geometry.meshSubBlockOffset(), vpp::ByteView(gb.data(), gb.size()));
    if (channelIndex >= mesh.channels().size()) { std::fprintf(stderr, "channel out of range\n"); return 1; }
    std::printf("channels : %zu ->", mesh.channels().size());
    for (const auto& ch : mesh.channels())
        std::printf(" [layout %u, %u verts, stride %zu, skinning %s]", ch.layoutCode,
                    ch.elementCount, ch.stride(),
                    sr3mesh::layoutInfoFor(ch.layoutCode).hasSkinning ? "yes" : "no");
    std::printf("\n");
    std::vector<sr3mesh::Vertex> verts = mesh.decodeChannel(channelIndex);
    std::printf("decoded  : channel %zu, %zu vertices\n", channelIndex, verts.size());

    std::printf("\n== MESH EXTENT (mesh space, as STORED - no skinning applied) ==\n");
    Aabb all;
    for (const auto& v : verts) all.add(v.position);
    all.print("all vertices");
    if (mesh.drawGroupsLocated()) {
        std::printf("draw grps: %zu; ranges per group:", mesh.drawGroups().size());
        for (const auto& g : mesh.drawGroups()) std::printf(" %zu", g.size());
        std::printf("\n");
        for (size_t gi = 0; gi < mesh.drawGroups().size(); ++gi) {
            Aabb ga;
            std::vector<uint8_t> used(verts.size(), 0);
            for (const auto& r : mesh.drawGroups()[gi]) {
                for (uint32_t k = 0; k < r.indexCount; ++k) {
                    size_t at = static_cast<size_t>(r.startIndex) + k;
                    if (at >= mesh.indices().size()) break;
                    uint32_t vi = mesh.indices()[at];
                    if (vi < verts.size() && !used[vi]) { used[vi] = 1; ga.add(verts[vi].position); }
                }
            }
            char label[64];
            std::snprintf(label, sizeof label, "group %zu vertices", gi);
            ga.print(label);
        }
    } else {
        std::printf("draw grps: NOT LOCATED\n");
    }

    // Rig rest positions, rig space and mesh space.
    std::vector<uint32_t> parentIdx(bones.size());
    std::vector<std::array<float, 3>> restRig(bones.size()), restMesh(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        parentIdx[i] = bones[i].parentIndex;
        restRig[i] = bones[i].restPosition;
        restMesh[i] = sr3rig::rigToMeshSpace(bones[i].restPosition);
    }
    std::printf("\n== RIG REST EXTENT (converted to mesh space) ==\n");
    Aabb rigAll;
    for (const auto& p : restMesh) rigAll.add(p);
    rigAll.print("all bones");
    int li = rig.findBone("l-hand"), ri = rig.findBone("r-hand");
    if (li >= 0 && ri >= 0) {
        double dx = static_cast<double>(restMesh[li][0]) - restMesh[ri][0];
        double dy = static_cast<double>(restMesh[li][1]) - restMesh[ri][1];
        double dz = static_cast<double>(restMesh[li][2]) - restMesh[ri][2];
        std::printf("l-hand<->r-hand rest separation: %.4f  (dx %.4f dy %.4f dz %.4f)\n",
                    std::sqrt(dx * dx + dy * dy + dz * dz), dx, dy, dz);
        std::printf("  l-hand rest(mesh) = (%.4f, %.4f, %.4f)\n", restMesh[li][0], restMesh[li][1], restMesh[li][2]);
        std::printf("  r-hand rest(mesh) = (%.4f, %.4f, %.4f)\n", restMesh[ri][0], restMesh[ri][1], restMesh[ri][2]);
    }

    // Primary bone per vertex.
    std::vector<int> primary(verts.size(), -1);
    size_t noPrimary = 0;
    for (size_t v = 0; v < verts.size(); ++v) {
        float bestW = -1.0f; int bestB = -1;
        for (size_t k = 0; k < 4; ++k) {
            if (verts[v].blendIndices[k] == 255) continue;
            if (verts[v].blendWeights[k] > bestW) { bestW = verts[v].blendWeights[k]; bestB = verts[v].blendIndices[k]; }
        }
        primary[v] = bestB;
        if (bestB < 0) ++noPrimary;
    }
    std::printf("vertices with no valid blend lane (excluded): %zu / %zu\n", noPrimary, verts.size());
    size_t outOfRange = 0;
    for (size_t v = 0; v < verts.size(); ++v)
        if (primary[v] >= 0 && static_cast<size_t>(primary[v]) >= bones.size()) ++outOfRange;
    std::printf("vertices whose primary bone index is >= rig bone count: %zu\n", outOfRange);

    // Per-bone cluster centroid.
    std::vector<size_t> count(bones.size(), 0);
    std::vector<std::array<double, 3>> sum(bones.size(), std::array<double, 3>{0, 0, 0});
    for (size_t v = 0; v < verts.size(); ++v) {
        int b = primary[v];
        if (b < 0 || static_cast<size_t>(b) >= bones.size()) continue;
        ++count[b];
        for (int k = 0; k < 3; ++k) sum[b][k] += verts[v].position[k];
    }

    auto reportResidual = [&](const char* title, const std::vector<std::array<float, 3>>& bonePos) {
        std::printf("\n== %s ==\n", title);
        std::printf("%-4s %-22s %7s  %8s   %-24s %-24s\n", "idx", "bone", "verts", "dist",
                    "cluster centroid", "bone position");
        struct Row { size_t idx; double d; };
        std::vector<Row> rows;
        double weightedSum = 0.0; size_t weightedN = 0;
        for (size_t b = 0; b < bones.size(); ++b) {
            if (count[b] < minCluster) continue;
            std::array<double, 3> c{sum[b][0] / count[b], sum[b][1] / count[b], sum[b][2] / count[b]};
            double d = dist3(c, bonePos[b]);
            rows.push_back({b, d});
            weightedSum += d * static_cast<double>(count[b]);
            weightedN += count[b];
        }
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.d > b.d; });
        size_t shown = 0;
        for (const auto& r : rows) {
            if (shown++ >= topN) break;
            size_t b = r.idx;
            std::array<double, 3> c{sum[b][0] / count[b], sum[b][1] / count[b], sum[b][2] / count[b]};
            std::printf("%-4zu %-22s %7zu  %8.4f   (%7.4f,%7.4f,%7.4f)  (%7.4f,%7.4f,%7.4f)\n",
                        b, bones[b].name.c_str(), count[b], r.d, c[0], c[1], c[2],
                        bonePos[b][0], bonePos[b][1], bonePos[b][2]);
        }
        std::printf("clusters >= %zu verts: %zu;  vertex-weighted mean residual = %.4f\n",
                    minCluster, rows.size(), weightedN ? weightedSum / static_cast<double>(weightedN) : 0.0);
        // Arm-only aggregate: the limbs a T-pose/arms-down difference moves most.
        double armSum = 0.0; size_t armN = 0, legN = 0; double legSum = 0.0;
        for (const auto& r : rows) {
            const std::string& nm = bones[r.idx].name;
            bool arm = nm.find("arm") != std::string::npos || nm.find("hand") != std::string::npos ||
                       nm.find("clavicle") != std::string::npos || nm.find("finger") != std::string::npos ||
                       nm.find("thumb") != std::string::npos;
            bool leg = nm.find("thigh") != std::string::npos || nm.find("calf") != std::string::npos ||
                       nm.find("foot") != std::string::npos || nm.find("toe") != std::string::npos;
            if (arm) { armSum += r.d * static_cast<double>(count[r.idx]); armN += count[r.idx]; }
            if (leg) { legSum += r.d * static_cast<double>(count[r.idx]); legN += count[r.idx]; }
        }
        std::printf("  arm-group (arm/hand/clavicle/finger/thumb) weighted mean = %.4f over %zu verts\n",
                    armN ? armSum / static_cast<double>(armN) : 0.0, armN);
        std::printf("  leg-group (thigh/calf/foot/toe)           weighted mean = %.4f over %zu verts\n",
                    legN ? legSum / static_cast<double>(legN) : 0.0, legN);
    };

    // --- Mapping-free pose measures ---------------------------------
    // Nothing below uses blend indices to LOCATE anything, so it is immune
    // to any question about how a blend lane maps to a bone. The extreme
    // vertices ARE the fingertips / crown / feet of whatever stance the
    // mesh is stored in, so their positions answer "T-pose or not"
    // directly, and the lanes are printed only as a read-out.
    {
        size_t ixMax = 0, ixMin = 0, iyMax = 0, iyMin = 0;
        for (size_t v = 1; v < verts.size(); ++v) {
            if (verts[v].position[0] > verts[ixMax].position[0]) ixMax = v;
            if (verts[v].position[0] < verts[ixMin].position[0]) ixMin = v;
            if (verts[v].position[1] > verts[iyMax].position[1]) iyMax = v;
            if (verts[v].position[1] < verts[iyMin].position[1]) iyMin = v;
        }
        auto showVertex = [&](const char* what, size_t v) {
            std::printf("%-16s v#%-6zu pos (%7.4f,%7.4f,%7.4f)  lanes", what, v,
                        verts[v].position[0], verts[v].position[1], verts[v].position[2]);
            for (size_t k = 0; k < 4; ++k) {
                if (verts[v].blendIndices[k] == 255) { std::printf("  [--]"); continue; }
                size_t bi = verts[v].blendIndices[k];
                std::printf("  [%u '%s' w=%.3f]", verts[v].blendIndices[k],
                            bi < bones.size() ? bones[bi].name.c_str() : "?",
                            static_cast<double>(verts[v].blendWeights[k]));
            }
            std::printf("\n");
        };
        std::printf("\n== EXTREME VERTICES (found without using any blend index) ==\n");
        showVertex("max X", ixMax);
        showVertex("min X", ixMin);
        showVertex("max Y", iyMax);
        showVertex("min Y", iyMin);
        int lc = rig.findBone("l-clavicle");
        int lh = rig.findBone("l-hand");
        if (lc >= 0) {
            double dy = static_cast<double>(restMesh[lc][1]) - verts[ixMax].position[1];
            double dx = static_cast<double>(verts[ixMax].position[0]) - restMesh[lc][0];
            std::printf("arm droop MESH (max-X vertex vs rig l-clavicle): dx=%.4f dy=%.4f -> "
                        "%.2f deg below horizontal\n",
                        dx, dy, std::atan2(dy, dx) * 180.0 / 3.14159265358979);
        }
        if (lc >= 0 && lh >= 0) {
            double rdy = static_cast<double>(restMesh[lc][1]) - restMesh[lh][1];
            double rdx = static_cast<double>(restMesh[lh][0]) - restMesh[lc][0];
            std::printf("arm droop RIG  (l-hand      vs    l-clavicle): dx=%.4f dy=%.4f -> "
                        "%.2f deg below horizontal\n",
                        rdx, rdy, std::atan2(rdy, rdx) * 180.0 / 3.14159265358979);
        }
    }

    reportResidual("BIND-POSE RESIDUAL: mesh vertex cluster centroid vs rig rest position (mesh space)",
                   restMesh);

    // All-lane WEIGHTED centroid per bone - the exact statistic HANDOFF
    // Sec9.13 describes ("take the weighted centroid of a bone's own
    // vertices and match it against that bone's rest position"), computed
    // here as a second, independent accumulation so a primary-lane
    // artefact cannot be mistaken for a data property.
    {
        std::vector<double> wsum(bones.size(), 0.0);
        std::vector<std::array<double, 3>> wpos(bones.size(), std::array<double, 3>{0, 0, 0});
        for (const auto& v : verts) {
            for (size_t k = 0; k < 4; ++k) {
                if (v.blendIndices[k] == 255) continue;
                size_t b = v.blendIndices[k];
                if (b >= bones.size()) continue;
                double w = static_cast<double>(v.blendWeights[k]);
                if (w <= 0.0) continue;
                wsum[b] += w;
                for (int c = 0; c < 3; ++c) wpos[b][c] += w * v.position[c];
            }
        }
        std::printf("\n== WEIGHTED-CENTROID RANK TEST (Sec9.13's statistic, recomputed) ==\n");
        std::printf("%-4s %-22s %8s %8s   %-24s  %-22s %8s\n", "idx", "bone", "wsum", "dist",
                    "weighted centroid", "nearest bone", "dist");
        size_t rankHits = 0, scored = 0;
        double residualSum = 0.0;
        for (size_t b = 0; b < bones.size(); ++b) {
            if (wsum[b] < 1.0) continue;
            std::array<double, 3> c{wpos[b][0] / wsum[b], wpos[b][1] / wsum[b], wpos[b][2] / wsum[b]};
            double own = dist3(c, restMesh[b]);
            size_t best = 0; double bestD = 1e30;
            for (size_t j = 0; j < bones.size(); ++j) {
                double d = dist3(c, restMesh[j]);
                if (d < bestD) { bestD = d; best = j; }
            }
            ++scored;
            residualSum += own;
            if (best == b) ++rankHits;
            if (scored <= topN)
                std::printf("%-4zu %-22s %8.1f %8.4f   (%7.4f,%7.4f,%7.4f)  %-22s %8.4f%s\n",
                            b, bones[b].name.c_str(), wsum[b], own, c[0], c[1], c[2],
                            bones[best].name.c_str(), bestD, best == b ? "  <= OWN" : "");
        }
        std::printf("rank test: nearest bone to a bone's own weighted centroid IS that bone in "
                    "%zu / %zu bones (%.2f%%); mean own-bone residual = %.4f\n",
                    rankHits, scored, scored ? 100.0 * rankHits / static_cast<double>(scored) : 0.0,
                    scored ? residualSum / static_cast<double>(scored) : 0.0);
    }

    if (!animArchivePath.empty() && !clipName.empty()) {
        std::vector<uint8_t> animArchive = readFile(animArchivePath);
        if (animArchive.empty()) { std::fprintf(stderr, "could not read %s\n", animArchivePath.c_str()); return 1; }
        std::vector<uint8_t> clipBytesVec, unused2;
        std::string clipPath;
        size_t clipIdx = 0;
        try {
            vpp::Container ac(vpp::ByteView(animArchive.data(), animArchive.size()));
            if (!findEntry(ac, clipName, clipBytesVec, unused2, "", "", clipPath, &clipIdx, nullptr)) {
                std::fprintf(stderr, "could not find '%s' in %s\n", clipName.c_str(), animArchivePath.c_str());
                return 1;
            }
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "anim archive error: %s\n", ex.what()); return 1;
        }
        vpp::ByteView clipBytes(clipBytesVec.data(), clipBytesVec.size());
        sr3anim::Animation anim = sr3anim::Animation::parse(clipBytes);
        sr3anim::Payload payload = sr3anim::Payload::walk(clipBytes, anim);
        std::printf("\n== CLIP ==\n");
        std::printf("clip     : %s <- container '%s' entry #%zu, %zu bytes\n", clipName.c_str(),
                    clipPath.empty() ? "(top level)" : clipPath.c_str(), clipIdx, clipBytesVec.size());
        std::printf("anim     : flags=0x%02X duration(+0x06)=%u tracks(+0x0A)=%u hasTable=%s "
                    "walkComplete=%s landedOnDeclaredEnd=%s hasUnaccountedPayload=%s\n",
                    anim.flags(), anim.durationTotal(),
                    static_cast<unsigned>(anim.field_0x0A_rawCount()),
                    anim.hasTrackBoneTable() ? "yes" : "no", payload.walkComplete() ? "yes" : "no",
                    payload.landedOnDeclaredEnd() ? "yes" : "no",
                    payload.hasUnaccountedPayload() ? "yes" : "no");
        size_t maxBoneTouched = 0;
        const bool hasTable = anim.hasTrackBoneTable();
        for (size_t t = 0; t < payload.tracks().size(); ++t) {
            size_t bi = t;
            if (hasTable) {
                size_t at = static_cast<size_t>(anim.trackBoneTableOffset()) + t;
                if (at < clipBytes.size()) bi = clipBytes.at(at);
            }
            if (bi > maxBoneTouched) maxBoneTouched = bi;
        }
        std::printf("compat   : touches bone indices up to %zu, rig has %zu (%s)\n",
                    maxBoneTouched, bones.size(),
                    maxBoneTouched < bones.size() ? "COMPATIBLE" : "INCOMPATIBLE");
        if (maxBoneTouched >= bones.size()) return 1;

        const float t = frac * static_cast<float>(anim.durationTotal());
        std::vector<sr3anim::BoneSample> samples =
            sr3anim::sampleClipAtTime(clipBytes, anim, payload, bones.size(), t);
        std::vector<sr3rig::Rotation3> rotsRig(bones.size(), sr3rig::identityRotation3());
        std::vector<std::array<float, 3>> deltasRig(bones.size(), std::array<float, 3>{0, 0, 0});
        size_t rotTracks = 0, transTracks = 0;
        for (size_t b = 0; b < bones.size(); ++b) {
            const auto& s = samples[b];
            rotsRig[b] = sr3rig::quatToRotation3(s.rotation.x, s.rotation.y, s.rotation.z, s.rotation.w);
            deltasRig[b] = s.translationDelta;
            if (s.hasRotationTrack) ++rotTracks;
            if (s.hasTranslationTrack) ++transTracks;
        }
        std::printf("sampled  : t=%.3f (frac %.3f), bones with rotation track %zu/%zu, "
                    "translation %zu/%zu\n", static_cast<double>(t), static_cast<double>(frac),
                    rotTracks, bones.size(), transTracks, bones.size());

        std::vector<sr3rig::Mat3x4> skinRig =
            sr3rig::computeAnimatedSkinningMatrices(parentIdx, restRig, &rotsRig, &deltasRig);
        std::vector<std::array<float, 3>> posedMesh(bones.size());
        for (size_t b = 0; b < bones.size(); ++b) {
            sr3rig::Mat3x4 sm = sr3rig::conjugateToMeshSpace(skinRig[b]);
            posedMesh[b] = sm.transformPoint(restMesh[b]);
        }
        Aabb posedAabb;
        for (const auto& p : posedMesh) posedAabb.add(p);
        posedAabb.print("posed bones");
        reportResidual("POSED RESIDUAL: mesh vertex cluster centroid vs CLIP-POSED bone position",
                       posedMesh);
    }
    return 0;
}
