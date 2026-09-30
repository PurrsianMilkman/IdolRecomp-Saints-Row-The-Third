// Team A's proposed decisive test for HANDOFF Sec9.56.3 (2026-09-13,
// relayed via purrsian-44): the published 175x-291x stretch RATIOS are
// against 4-19mm bind-pose gaps - a tiny denominator inflates any ratio
// regardless of whether the ABSOLUTE motion is actually anomalous. If
// ordinary same-limb mesh edges move by a similar ABSOLUTE amount under a
// real clip, the pose itself is the driver, not a seam defect. This probe
// measures exactly that: real mesh edges (from the actual triangle list,
// not synthetic pairs), classified same-limb vs cross-limb by rig-hierarchy
// hop distance (same convention as precheck_topology.cpp), absolute
// |posed - rest| length in metres for both groups, side by side.
//
// Reuses the CONFIRMED pipeline (sr3rig::computeAnimatedSkinningMatrices,
// conjugateToMeshSpace, skinVertices; sr3anim::sampleClipAtTime) exactly as
// tools/sr3_viewer.cpp's runAnimPose() calls it - no second implementation
// of the pose math, only a different measurement on the same output.
//
// Usage: probe_seam_absolute_displacement <characters.vpp_pc> <mesh.ccmesh_pc>
//        <rig.rig_pc> <anim_archive.vpp_pc> <clip.anim_pc> [--frac F] [--min-hops N]
//        [--bone-palette]
//
// --bone-palette (HANDOFF Sec9.62, OPT-IN): read blend indices through the
// mesh's own bone palette (sr3mesh::MeshBlock::bonePalette()) and use the
// (-x,-y,-z) rig->mesh conjugation - see include/sr3rig/bone_palette.h.
// Without it this program is byte-for-byte the Sec9.56.4 measurement.
// Both modes additionally print a CLASSIFICATION-FREE table over EVERY
// mesh edge (absolute |posed - rest| and the stretch ratio), because the
// cross-limb/same-limb split depends on each vertex's dominant bone, which
// is exactly what the palette changes - the all-edge numbers are the
// before/after comparison that does not move the goalposts.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include "sr3anim/animation.h"
#include "sr3anim/payload.h"
#include "sr3anim/sample.h"
#include "sr3geometry/geometry_block.h"
#include "sr3geometry/material_block.h"
#include "sr3mesh/mesh_block.h"
#include "sr3rig/animated_pose.h"
#include "sr3rig/bone_palette.h"
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
// Exact-name match + recursion into nested containers - matches
// tools/sr3_viewer.cpp's own findEntry() exactly, since these archives
// nest character assets inside sub-containers.
bool findEntry(const vpp::Container& c, const std::string& name, std::vector<uint8_t>& out) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].name == name) return entryBytes(c, i, out);
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        try {
            vpp::Container nested = c.openNested(i);
            if (findEntry(nested, name, out)) return true;
        } catch (const std::exception&) {}
    }
    return false;
}
float dist3(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
char sideOf(const std::string& name) {
    if (name.size() >= 2 && name[0] == 'l' && name[1] == '-') return 'L';
    if (name.size() >= 2 && name[0] == 'r' && name[1] == '-') return 'R';
    return '?';
}

int main(int argc, char** argv) {
    if (argc < 6) {
        printf("usage: probe_seam_absolute_displacement <characters.vpp_pc> <mesh.ccmesh_pc> "
               "<rig.rig_pc> <anim_archive.vpp_pc> <clip.anim_pc> [--frac F] [--min-hops N]\n");
        return 1;
    }
    std::string charArchivePath = argv[1], cmeshName = argv[2], rigName = argv[3],
                animArchivePath = argv[4], clipName = argv[5];
    float frac = 1.0f;
    int minHops = 3;
    bool useBonePalette = false;
    for (int i = 6; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--frac" && i + 1 < argc) frac = static_cast<float>(std::atof(argv[++i]));
        else if (a == "--min-hops" && i + 1 < argc) minHops = std::atoi(argv[++i]);
        else if (a == "--bone-palette") useBonePalette = true;
    }

    std::vector<uint8_t> charArchive = readFile(charArchivePath);
    std::string gmeshName = cmeshName;
    size_t dot = gmeshName.find_last_of('.');
    if (dot != std::string::npos && dot + 1 < gmeshName.size()) gmeshName[dot + 1] = 'g';
    std::vector<uint8_t> cb, gb, rb;
    try {
        vpp::Container c(vpp::ByteView(charArchive.data(), charArchive.size()));
        if (!findEntry(c, cmeshName, cb) || !findEntry(c, gmeshName, gb) || !findEntry(c, rigName, rb)) {
            printf("could not find mesh/gmesh/rig in %s\n", charArchivePath.c_str());
            return 1;
        }
    } catch (const std::exception& e) { printf("archive error: %s\n", e.what()); return 1; }

    std::vector<uint8_t> animArchive = readFile(animArchivePath);
    std::vector<uint8_t> clipBytesVec;
    try {
        vpp::Container c(vpp::ByteView(animArchive.data(), animArchive.size()));
        if (!findEntry(c, clipName, clipBytesVec)) {
            printf("could not find %s\n", clipName.c_str());
            return 1;
        }
    } catch (const std::exception& e) { printf("anim archive error: %s\n", e.what()); return 1; }

    sr3geometry::MaterialBlock material = sr3geometry::MaterialBlock::parse(vpp::ByteView(cb.data(), cb.size()));
    sr3geometry::GeometryBlock geometry =
        sr3geometry::GeometryBlock::parse(vpp::ByteView(cb.data(), cb.size()), material);
    sr3mesh::MeshBlock mesh = sr3mesh::MeshBlock::parse(vpp::ByteView(cb.data(), cb.size()),
                                                        geometry.meshSubBlockOffset(),
                                                        vpp::ByteView(gb.data(), gb.size()));
    std::vector<sr3mesh::Vertex> verts = mesh.decodeChannel(0);
    std::vector<uint32_t> tris = mesh.drawGroupsLocated() ? mesh.triangleListForGroup(0) : std::vector<uint32_t>();
    if (verts.empty() || tris.empty()) { printf("no vertices/triangles\n"); return 1; }

    sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(rb.data(), rb.size()));
    const auto& bones = rig.bones();

    if (useBonePalette) {
        const std::vector<uint8_t>& palette = mesh.bonePalette();
        if (palette.empty() || palette.size() != mesh.bonePaletteDeclaredCount()) {
            printf("--bone-palette: block declares %u entries, %zu readable - refusing to guess\n",
                   static_cast<unsigned>(mesh.bonePaletteDeclaredCount()), palette.size());
            return 1;
        }
        if (mesh.bonePaletteSets().size() != 1) {
            printf("--bone-palette: block has %zu palette SETS - per-range set assignment is OPEN "
                   "(HANDOFF Sec9.62), refusing to treat the concatenation as one palette\n",
                   mesh.bonePaletteSets().size());
            return 1;
        }
        sr3rig::BlendIndexRemapStats remap;
        verts = sr3rig::remapBlendIndicesThroughPalette(verts, palette, bones.size(), &remap);
        printf("bone palette: %zu entries, lanes remapped=%lld dropped(no slot)=%lld dropped(rig OOR)=%lld\n",
               palette.size(), remap.lanesRemapped, remap.lanesDroppedPaletteOutOfRange,
               remap.lanesDroppedRigOutOfRange);
    }
    auto toMesh = [&](const std::array<float, 3>& p) {
        return useBonePalette ? sr3rig::rigToMeshSpaceInverted(p) : sr3rig::rigToMeshSpace(p);
    };
    auto conj = [&](const sr3rig::Mat3x4& m) {
        return useBonePalette ? sr3rig::conjugateToMeshSpaceInverted(m) : sr3rig::conjugateToMeshSpace(m);
    };

    vpp::ByteView clipBytes(clipBytesVec.data(), clipBytesVec.size());
    sr3anim::Animation anim = sr3anim::Animation::parse(clipBytes);
    sr3anim::Payload payload = sr3anim::Payload::walk(clipBytes, anim);
    const float duration = static_cast<float>(anim.durationTotal());
    const float t = frac * duration;
    printf("mesh=%s rig=%zu bones clip=%s duration=%.1f frac=%.2f t=%.2f minHops=%d\n",
           cmeshName.c_str(), bones.size(), clipName.c_str(), duration, frac, t, minHops);

    // All-pairs bone-hop distance (BFS per bone; forest, <=~100 bones).
    std::vector<std::vector<int>> children(bones.size());
    for (size_t i = 0; i < bones.size(); ++i)
        if (!bones[i].isRoot()) children[bones[i].parentIndex].push_back(static_cast<int>(i));
    std::vector<std::vector<int>> hopDist(bones.size(), std::vector<int>(bones.size(), -1));
    for (size_t start = 0; start < bones.size(); ++start) {
        std::queue<int> q;
        q.push(static_cast<int>(start));
        hopDist[start][start] = 0;
        while (!q.empty()) {
            int cur = q.front(); q.pop();
            int nbrs[64]; int nc = 0;
            if (!bones[static_cast<size_t>(cur)].isRoot())
                nbrs[nc++] = static_cast<int>(bones[static_cast<size_t>(cur)].parentIndex);
            for (int ch : children[static_cast<size_t>(cur)])
                if (nc < 64) nbrs[nc++] = ch;
            for (int k = 0; k < nc; ++k) {
                int nb = nbrs[k];
                if (hopDist[start][static_cast<size_t>(nb)] < 0) {
                    hopDist[start][static_cast<size_t>(nb)] = hopDist[start][static_cast<size_t>(cur)] + 1;
                    q.push(nb);
                }
            }
        }
    }

    // Rest (bind, mesh-space) and posed (real clip, mesh-space) positions,
    // via the exact confirmed pipeline sr3_viewer's runAnimPose() uses.
    std::vector<uint32_t> parentIdx(bones.size());
    std::vector<std::array<float, 3>> restRig(bones.size()), restMesh(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        parentIdx[i] = bones[i].parentIndex;
        restRig[i] = bones[i].restPosition;
        restMesh[i] = toMesh(bones[i].restPosition);
    }
    std::vector<sr3rig::Mat3x4> bindSkin = sr3rig::computeSkinningMatrices(parentIdx, restMesh, nullptr);
    std::vector<std::array<float, 3>> bindPositions = sr3rig::skinVertices(verts, bindSkin, nullptr);

    std::vector<sr3anim::BoneSample> samples = sr3anim::sampleClipAtTime(clipBytes, anim, payload, bones.size(), t);
    std::vector<sr3rig::Rotation3> rotsRig(bones.size(), sr3rig::identityRotation3());
    std::vector<std::array<float, 3>> deltasRig(bones.size(), std::array<float, 3>{0, 0, 0});
    for (size_t b = 0; b < bones.size(); ++b) {
        std::string nmLower;
        for (char ch : bones[b].name) nmLower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (nmLower == "camera" || nmLower == "camtarget" || nmLower == "camfov" || nmLower == "camdof")
            continue;  // held at identity/zero - test whether MINORITY blend weight on these
                       // (not just dominant-bone classification) is what's actually moving
                       // the same-limb tail's vertices.
        const auto& s = samples[b];
        rotsRig[b] = sr3rig::quatToRotation3(s.rotation.x, s.rotation.y, s.rotation.z, s.rotation.w);
        deltasRig[b] = s.translationDelta;
    }
    std::vector<sr3rig::Mat3x4> skinRig =
        sr3rig::computeAnimatedSkinningMatrices(parentIdx, restRig, &rotsRig, &deltasRig);
    std::vector<sr3rig::Mat3x4> skinMesh(bones.size());
    for (size_t b = 0; b < bones.size(); ++b) skinMesh[b] = conj(skinRig[b]);
    std::vector<std::array<float, 3>> posedPositions = sr3rig::skinVertices(verts, skinMesh, nullptr);

    // Dominant (highest-weight) bone per vertex. Camera-shot bones
    // (spec-rig-format.md Sec5/Sec8.1: real named bones, but non-skeletal
    // helpers - see HANDOFF Sec9.56.1) are excluded from classification so
    // their scripted, non-articulation motion doesn't contaminate the
    // "ordinary same-limb joint motion" baseline this test needs to be
    // clean.
    auto lowerName = [](std::string s) {
        for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return s;
    };
    std::vector<bool> isCameraBone(bones.size(), false);
    for (size_t i = 0; i < bones.size(); ++i) {
        std::string nm = lowerName(bones[i].name);
        if (nm == "camera" || nm == "camtarget" || nm == "camfov" || nm == "camdof") isCameraBone[i] = true;
    }
    std::vector<int> dominant(verts.size(), -1);
    for (size_t vi = 0; vi < verts.size(); ++vi) {
        float best = -1.0f;
        for (int k = 0; k < 4; ++k) {
            uint8_t bi = verts[vi].blendIndices[static_cast<size_t>(k)];
            float w = verts[vi].blendWeights[static_cast<size_t>(k)];
            if (bi == 255 || w <= 0.0f || bi >= bones.size() || isCameraBone[bi]) continue;
            if (w > best) { best = w; dominant[vi] = bi; }
        }
    }

    // Build unique undirected edges from the triangle list.
    std::set<std::pair<uint32_t, uint32_t>> edgeSet;
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        uint32_t a = tris[i], b = tris[i + 1], c = tris[i + 2];
        if (a >= verts.size() || b >= verts.size() || c >= verts.size()) continue;
        uint32_t e[3][2] = {{a, b}, {b, c}, {a, c}};
        for (auto& ed : e) {
            uint32_t lo = std::min(ed[0], ed[1]), hi = std::max(ed[0], ed[1]);
            edgeSet.insert({lo, hi});
        }
    }

    std::vector<double> crossLimbAbs, sameLimbAbs;
    double worstCrossRest = 1e30, worstCrossAbs = 0.0;
    uint32_t worstCrossA = 0, worstCrossB = 0;
    double worstSameAbs = 0.0;
    uint32_t worstSameA = 0, worstSameB = 0;
    float worstSameRest = 0.0f;

    for (const auto& e : edgeSet) {
        uint32_t va = e.first, vb = e.second;
        int ba = dominant[va], bb = dominant[vb];
        if (ba < 0 || bb < 0) continue;
        float rest = dist3(bindPositions[va], bindPositions[vb]);
        float posed = dist3(posedPositions[va], posedPositions[vb]);
        double absDisp = std::fabs(static_cast<double>(posed) - static_cast<double>(rest));

        int hops = hopDist[static_cast<size_t>(ba)][static_cast<size_t>(bb)];
        char sa = sideOf(bones[static_cast<size_t>(ba)].name), sb = sideOf(bones[static_cast<size_t>(bb)].name);
        bool crossLimb = (hops < 0 || hops > minHops) ||
                          (sa != '?' && sb != '?' && sa != sb);

        if (rest < 0.05f && crossLimb) {
            crossLimbAbs.push_back(absDisp);
            if (rest < worstCrossRest) {
                worstCrossRest = rest;
                worstCrossAbs = absDisp;
                worstCrossA = va;
                worstCrossB = vb;
            }
        } else if (rest >= 0.02f && rest <= 0.30f && !crossLimb) {
            sameLimbAbs.push_back(absDisp);
            if (absDisp > worstSameAbs) {
                worstSameAbs = absDisp;
                worstSameA = va;
                worstSameB = vb;
                worstSameRest = rest;
            }
        }
    }

    auto stat = [](std::vector<double> v, const char* label) {
        if (v.empty()) { printf("  %-30s (none)\n", label); return; }
        std::sort(v.begin(), v.end());
        double med = v[v.size() / 2];
        double p90 = v[static_cast<size_t>(0.9 * static_cast<double>(v.size() - 1))];
        double mx = v.back();
        printf("  %-30s n=%-6zu median=%.4fm  p90=%.4fm  max=%.4fm\n", label, v.size(), med, p90, mx);
    };

    printf("\n=== ABSOLUTE edge displacement under real clip (Team A's proposed test) ===\n");
    stat(crossLimbAbs, "cross-limb, near-degenerate (<5cm rest)");
    stat(sameLimbAbs, "ordinary same-limb (2-30cm rest)");
    if (crossLimbAbs.empty()) {
        printf("\nworst cross-limb pair: (none - no near-degenerate cross-limb edge exists under this reading)\n");
    } else {
        printf("\nworst cross-limb pair: v%u<->v%u  rest=%.5fm  posed_abs_disp=%.5fm  (bones %d/%d = %s/%s)\n",
               worstCrossA, worstCrossB, worstCrossRest, worstCrossAbs, dominant[worstCrossA], dominant[worstCrossB],
               bones[static_cast<size_t>(dominant[worstCrossA])].name.c_str(),
               bones[static_cast<size_t>(dominant[worstCrossB])].name.c_str());
    }
    printf("worst same-limb pair : v%u<->v%u  rest=%.5fm  posed_abs_disp=%.5fm  (bones %d/%d = %s/%s)\n",
           worstSameA, worstSameB, worstSameRest, worstSameAbs, dominant[worstSameA], dominant[worstSameB],
           bones[static_cast<size_t>(dominant[worstSameA])].name.c_str(),
           bones[static_cast<size_t>(dominant[worstSameB])].name.c_str());

    // CLASSIFICATION-FREE table over EVERY edge (HANDOFF Sec9.62). The two
    // groups above are selected by each vertex's dominant bone, and the
    // bone palette changes what that bone IS - so a before/after on those
    // groups alone would be comparing differently-selected edge sets. This
    // table uses the same edge set either way: all unique mesh edges.
    {
        std::vector<double> allAbs, allRatio;
        double worstAbs = 0.0, worstRatio = 0.0;
        uint32_t worstAbsA = 0, worstAbsB = 0, worstRatioA = 0, worstRatioB = 0;
        float worstAbsRest = 0.0f, worstRatioRest = 0.0f;
        size_t over10cm = 0, over25cm = 0, over50cm = 0;
        for (const auto& e : edgeSet) {
            uint32_t va = e.first, vb = e.second;
            float rest = dist3(bindPositions[va], bindPositions[vb]);
            float posed = dist3(posedPositions[va], posedPositions[vb]);
            double absDisp = std::fabs(static_cast<double>(posed) - static_cast<double>(rest));
            allAbs.push_back(absDisp);
            if (absDisp > 0.10) ++over10cm;
            if (absDisp > 0.25) ++over25cm;
            if (absDisp > 0.50) ++over50cm;
            if (absDisp > worstAbs) { worstAbs = absDisp; worstAbsA = va; worstAbsB = vb; worstAbsRest = rest; }
            if (rest > 0.001f) {
                double ratio = static_cast<double>(posed) / static_cast<double>(rest);
                allRatio.push_back(ratio);
                if (ratio > worstRatio) { worstRatio = ratio; worstRatioA = va; worstRatioB = vb; worstRatioRest = rest; }
            }
        }
        auto nameOf = [&](uint32_t v) -> const char* {
            return dominant[v] < 0 ? "(none)" : bones[static_cast<size_t>(dominant[v])].name.c_str();
        };
        printf("\n=== ALL EDGES, classification-free (%s) ===\n",
               useBonePalette ? "BONE PALETTE + (-x,-y,-z)" : "direct indices + (x,-y,-z), Sec9.56.4 baseline");
        stat(allAbs, "all edges |posed-rest| (m)");
        stat(allRatio, "all edges posed/rest ratio");
        printf("  edges with |posed-rest| > 0.10m: %zu   > 0.25m: %zu   > 0.50m: %zu   (of %zu)\n",
               over10cm, over25cm, over50cm, edgeSet.size());
        printf("  worst absolute: v%u<->v%u rest=%.5fm disp=%.5fm (%s / %s)\n", worstAbsA, worstAbsB,
               worstAbsRest, worstAbs, nameOf(worstAbsA), nameOf(worstAbsB));
        printf("  worst ratio   : v%u<->v%u rest=%.5fm ratio=%.2fx (%s / %s)\n", worstRatioA, worstRatioB,
               worstRatioRest, worstRatio, nameOf(worstRatioA), nameOf(worstRatioB));
    }

    // ROOT MOTION TEST (Team A's proposed follow-up, relayed 2026-09-13):
    // does the SKELETAL ROOT bone's own bind-to-posed transform move by
    // enough to be a plausible driver of the same-limb tail? Report the
    // root's own translation magnitude directly - if it's on the same
    // order as the tail's outliers, root motion is a live hypothesis for
    // the tail; if it's tiny, the tail needs a different explanation.
    printf("\n=== ROOT MOTION CHECK ===\n");
    for (size_t i = 0; i < bones.size(); ++i) {
        if (!bones[i].isRoot()) continue;
        std::array<float, 3> restW = toMesh(bones[i].restPosition);
        std::array<float, 3> posedW = skinMesh[i].transformPoint(restW);
        float d = dist3(restW, posedW);
        printf("  root bone [%zu] %-16s : bind-to-posed origin displacement = %.5fm\n", i,
               bones[i].name.c_str(), d);
    }
    printf("\nInterpretation: if cross-limb absolute displacement is COMPARABLE in\n");
    printf("magnitude to ordinary same-limb displacement, the huge RATIO reported\n");
    printf("earlier was mostly a tiny-rest-length artifact, not anomalous motion.\n");
    printf("If cross-limb displacement is an order of magnitude (or more) LARGER\n");
    printf("in absolute terms too, the motion itself is anomalous, not just the ratio.\n");
    return 0;
}
