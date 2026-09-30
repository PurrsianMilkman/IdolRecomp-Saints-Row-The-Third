// File-side complement to HANDOFF.md Sec9.56/9.56.1/9.56.2's open question:
// do REAL shipped .anim_pc clips actually rotate the "risky" bones (the
// near-degenerate rest-pose vertex-pair seams already root-caused there)
// far enough to trigger the severe stretch already observed, or were the
// specific test clips tried so far unusually extreme?
//
// This tool does NOT touch sr3rig/pose.h, sr3rig/animated_pose.*, or
// sr3anim/sample.* - it only CALLS their existing, confirmed-correct
// public API, exactly as sr3_viewer's `animpose` command and
// tools/validation/precheck_topology.cpp already do. Two phases:
//
//   PHASE 1 (population scan): walk every CLEAN .anim_pc clip in the given
//   anim archive (same population filter as list_clean_clips.cpp /
//   match_anim_rig.cpp: flags bit 0x40 clear, walk lands exactly on the
//   file's own declared +0x30 offset). For each clip whose track->bone
//   mapping touches one of the risky bone indices, decode that track's
//   keyframe quaternions directly via Payload::rotations() (the
//   CONFIRMED rotation reconstruction - same formula sample.cpp's
//   sampleClipAtTime() uses) and record the MAXIMUM angular deviation
//   from identity (bind pose) any key in that track reaches, in degrees
//   (2*acos(clamp(|w|,0,1))*180/pi - the same formula this project uses
//   elsewhere for root-motion angle, e.g. probe_anim_flag40.cpp's
//   rootAngleDeg()). Using the keyframe values directly rather than a
//   fine time-grid is deliberate and cheap: they are exactly what an
//   animator authored, and the task's own instruction names
//   Payload::rotations() as an equally valid entry point to
//   sampleClipAtTime() for this purpose.
//
//   PHASE 2 (representative stretch check): for a handful of clips
//   spanning the phase-1 distribution (~p10/p50/p90 by per-clip peak
//   angle), reconstruct the FULL clip pose at that clip's own peak
//   moment using sr3anim::sampleClipAtTime() (all bones, exactly as
//   sr3_viewer's runAnimPose does, DEFAULT/non-diagnostic settings: no
//   rotation inversion, no bone isolation, no camera-bone exclusion) and
//   sr3rig::computeAnimatedSkinningMatrices() +
//   sr3rig::conjugateToMeshSpace() + sr3rig::skinVertices(), then measure
//   the distance between the two known near-degenerate vertex pairs
//   (found the same way precheck_topology.cpp does: primary-bone-per-
//   vertex, nearest cross-bone pair) before and after, i.e. the M4-style
//   edge-stretch ratio from validate_pose.cpp/Sec9.14, applied to these
//   specific vertex pairs rather than the whole mesh.
//
// Risky bones are NOT re-derived here from scratch: Sec9.56.1/9.56.2
// already named bone 22 (r-hand) / bone 23 (l-finger1) as the dominant
// cross-body seam pair, and a weapon-attachment marker bone
// (`l-handprop`) near its own sibling fingers as the second mechanism.
// Because bones 0-45 are numerically identical in rest position across
// brad/reggies/angel/brute_flamethrower (confirmed via dump_rig_bones,
// Sec9.56.2), those SAME bone indices are used unchanged for whichever
// rig is passed on the command line - only the exact vertex indices (and
// which of l-handprop's several close siblings is nearest) are
// re-derived per mesh, via the same nearest-pair search
// precheck_topology.cpp uses, restricted to the relevant bone sets.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
#include "sr3rig/pose.h"
#include "sr3rig/rig.h"
#include "vpp/container.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

double angleDegFromIdentity(const sr3anim::Quat& q) {
    double w = q.w;
    if (w > 1.0) w = 1.0;
    if (w < -1.0) w = -1.0;
    return 2.0 * std::acos(std::fabs(w)) * 180.0 / kPi;
}

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    std::vector<uint8_t> b(static_cast<size_t>(n > 0 ? n : 0));
    if (n > 0) { f.seekg(0); f.read(reinterpret_cast<char*>(b.data()), n); }
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

// One risky-bone-track observation within one clip.
struct ClipBoneObservation {
    std::string clipName;
    int boneIdx = -1;
    size_t trackIdx = 0;
    uint32_t rotationKeys = 0;
    uint16_t duration = 0;
    double maxAngleDeg = 0.0;
    size_t peakKeyIndex = 0;
};

std::vector<ClipBoneObservation> g_observations;

// clipName -> per-bone max angle, for building per-clip combined stats.
std::map<std::string, std::map<int, double>> g_perClipBoneAngle;
std::map<std::string, uint16_t> g_clipDuration;
size_t g_totalCleanClips = 0;
size_t g_compatibleClips = 0;

void walkClips(const vpp::Container& c, size_t rigBoneCount, const std::set<int>& riskyBones) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const auto& e = c.entries()[i];
        if (endsWith(e.name, ".anim_pc")) {
            std::vector<uint8_t> b;
            if (entryBytes(c, i, b)) {
                try {
                    vpp::ByteView bytes(b.data(), b.size());
                    sr3anim::Animation a = sr3anim::Animation::parse(bytes);
                    sr3anim::Payload p = sr3anim::Payload::walk(bytes, a);
                    if (p.walkComplete() && !p.hasUnaccountedPayload() &&
                        a.hasTrailingOffset() && p.landedOnDeclaredEnd()) {
                        ++g_totalCleanClips;

                        const auto& tracks = p.tracks();
                        const bool hasTable = a.hasTrackBoneTable();
                        const uint32_t tableOffset = a.trackBoneTableOffset();

                        size_t maxBoneTouched = 0;
                        std::vector<size_t> boneOfTrack(tracks.size());
                        for (size_t t = 0; t < tracks.size(); ++t) {
                            size_t bi = t;
                            if (hasTable) {
                                size_t at = static_cast<size_t>(tableOffset) + t;
                                if (at < bytes.size()) bi = bytes.at(at);
                            }
                            boneOfTrack[t] = bi;
                            if (bi > maxBoneTouched) maxBoneTouched = bi;
                        }
                        if (maxBoneTouched >= rigBoneCount) {
                            // incompatible with this rig - skip entirely,
                            // same rule match_anim_rig.cpp/sr3_viewer use.
                        } else {
                            ++g_compatibleClips;
                            g_clipDuration[e.name] = a.durationTotal();
                            for (size_t t = 0; t < tracks.size(); ++t) {
                                int bi = static_cast<int>(boneOfTrack[t]);
                                if (riskyBones.find(bi) == riskyBones.end()) continue;
                                if (tracks[t].rotationKeys == 0) continue;
                                std::vector<sr3anim::RotationSample> rots = p.rotations(bytes, t);
                                if (rots.empty()) continue;
                                double best = -1.0;
                                size_t bestK = 0;
                                for (size_t k = 0; k < rots.size(); ++k) {
                                    double ang = angleDegFromIdentity(rots[k].value);
                                    if (ang > best) { best = ang; bestK = k; }
                                }
                                ClipBoneObservation obs;
                                obs.clipName = e.name;
                                obs.boneIdx = bi;
                                obs.trackIdx = t;
                                obs.rotationKeys = tracks[t].rotationKeys;
                                obs.duration = a.durationTotal();
                                obs.maxAngleDeg = best;
                                obs.peakKeyIndex = bestK;
                                g_observations.push_back(obs);
                                double& slot = g_perClipBoneAngle[e.name][bi];
                                if (best > slot) slot = best;
                            }
                        }
                    }
                } catch (const std::exception&) {
                }
            }
        }
        if (e.payload.kind == vpp::PayloadKind::Raw) {
            try { walkClips(c.openNested(i), rigBoneCount, riskyBones); } catch (const std::exception&) {}
        }
    }
}

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    double idx = p / 100.0 * static_cast<double>(v.size() - 1);
    size_t lo = static_cast<size_t>(std::floor(idx));
    size_t hi = static_cast<size_t>(std::ceil(idx));
    if (hi >= v.size()) hi = v.size() - 1;
    double frac = idx - static_cast<double>(lo);
    return v[lo] + frac * (v[hi] - v[lo]);
}

void printDistribution(const char* label, const std::vector<double>& vals) {
    if (vals.empty()) {
        std::printf("  %-28s: 0 clips touch this bone\n", label);
        return;
    }
    double mx = *std::max_element(vals.begin(), vals.end());
    std::printf("  %-28s: n=%-5zu median=%6.2f deg  p90=%6.2f deg  max=%6.2f deg\n", label,
                vals.size(), percentile(vals, 50.0), percentile(vals, 90.0), mx);
}

// Finds the closest bind-pose (rest, mesh-space) vertex pair between two
// disjoint bone-index sets, using each vertex's PRIMARY bone (largest
// blend weight lane) - identical method to precheck_topology.cpp, just
// restricted to specific bone sets instead of an all-pairs grid search
// (these sets are small, so a direct O(|A|*|B|) scan is trivial).
struct PairResult {
    bool found = false;
    size_t vA = 0, vB = 0;
    int boneA = -1, boneB = -1;
    float restDist = 0.0f;
};

PairResult findClosestPair(const std::vector<sr3mesh::Vertex>& verts, const std::vector<int>& primaryBone,
                            const std::set<int>& setA, const std::set<int>& setB) {
    std::vector<size_t> listA, listB;
    for (size_t v = 0; v < verts.size(); ++v) {
        if (primaryBone[v] < 0) continue;
        if (setA.count(primaryBone[v])) listA.push_back(v);
        else if (setB.count(primaryBone[v])) listB.push_back(v);
    }
    PairResult best;
    float bestD = 1e30f;
    for (size_t a : listA) {
        for (size_t b : listB) {
            float dx = verts[a].position[0] - verts[b].position[0];
            float dy = verts[a].position[1] - verts[b].position[1];
            float dz = verts[a].position[2] - verts[b].position[2];
            float d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d < bestD) {
                bestD = d;
                best.found = true;
                best.vA = a; best.vB = b;
                best.boneA = primaryBone[a]; best.boneB = primaryBone[b];
                best.restDist = d;
            }
        }
    }
    return best;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr,
                     "usage: measure_risky_rotation <characters.vpp_pc> <preload_anim.vpp_pc> "
                     "<name.ccmesh_pc> <name.rig_pc>\n");
        return 1;
    }
    const std::string charArchivePath = argv[1];
    const std::string animArchivePath = argv[2];
    const std::string cmeshName = argv[3];
    const std::string rigName = argv[4];

    std::string gmeshName = cmeshName;
    size_t dot = gmeshName.find_last_of('.');
    if (dot != std::string::npos && dot + 1 < gmeshName.size()) gmeshName[dot + 1] = 'g';

    std::vector<uint8_t> charArchive = readFile(charArchivePath);
    if (charArchive.empty()) { std::fprintf(stderr, "could not read %s\n", charArchivePath.c_str()); return 1; }

    std::vector<uint8_t> cb, gb, rb, unusedSibling;
    try {
        vpp::Container container(vpp::ByteView(charArchive.data(), charArchive.size()));
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

    sr3geometry::MaterialBlock material = sr3geometry::MaterialBlock::parse(vpp::ByteView(cb.data(), cb.size()));
    sr3geometry::GeometryBlock geometry = sr3geometry::GeometryBlock::parse(vpp::ByteView(cb.data(), cb.size()), material);
    if (!geometry.hasMeshSubBlock()) { std::fprintf(stderr, "no Mesh sub-block\n"); return 1; }
    sr3mesh::MeshBlock mesh = sr3mesh::MeshBlock::parse(vpp::ByteView(cb.data(), cb.size()),
                                                        geometry.meshSubBlockOffset(),
                                                        vpp::ByteView(gb.data(), gb.size()));
    if (mesh.channels().empty() || !sr3mesh::layoutInfoFor(mesh.channels()[0].layoutCode).hasSkinning) {
        std::fprintf(stderr, "channel 0 carries no skinning\n");
        return 1;
    }
    std::vector<sr3mesh::Vertex> verts = mesh.decodeChannel(0);
    std::printf("mesh     : %s, %zu vertices\n", cmeshName.c_str(), verts.size());

    std::vector<int> primaryBone(verts.size(), -1);
    for (size_t v = 0; v < verts.size(); ++v) {
        float bestW = -1.0f; int bestB = -1;
        for (size_t k = 0; k < 4; ++k) {
            if (verts[v].blendIndices[k] == 255) continue;
            if (verts[v].blendWeights[k] > bestW) { bestW = verts[v].blendWeights[k]; bestB = verts[v].blendIndices[k]; }
        }
        primaryBone[v] = bestB;
    }

    // --- Bone indices, per HANDOFF.md Sec9.56.1/9.56.2, UNCHANGED across
    // rigs because bones 0-45 are numerically identical (Sec9.56.2). ---
    const int kBoneRHand = 22, kBoneLFinger1 = 23, kBoneLHandprop = 27, kBoneLHand = 21;

    // Nearest r-hand <-> l-finger1 vertex pair (the dominant seam pair).
    PairResult primaryPair = findClosestPair(verts, primaryBone, {kBoneRHand}, {kBoneLFinger1});

    // Nearest l-handprop <-> (any OTHER bone in l-hand's own subtree)
    // vertex pair - the marker-bone-vs-sibling-finger mechanism. Subtree
    // of bone 21 (l-hand) minus l-hand and l-handprop themselves.
    std::vector<int> lHandSubtree;
    {
        std::vector<char> inSub(bones.size(), 0);
        inSub[kBoneLHand] = 1;
        for (size_t i = 0; i < bones.size(); ++i)
            if (!bones[i].isRoot() && inSub[bones[i].parentIndex]) inSub[i] = 1;
        for (size_t i = 0; i < bones.size(); ++i)
            if (inSub[i] && static_cast<int>(i) != kBoneLHand && static_cast<int>(i) != kBoneLHandprop)
                lHandSubtree.push_back(static_cast<int>(i));
    }
    std::set<int> lHandSiblingSet(lHandSubtree.begin(), lHandSubtree.end());
    PairResult secondaryPair = findClosestPair(verts, primaryBone, {kBoneLHandprop}, lHandSiblingSet);

    auto boneName = [&](int idx) -> std::string {
        if (idx < 0 || static_cast<size_t>(idx) >= bones.size()) return "?";
        return bones[static_cast<size_t>(idx)].name;
    };

    std::printf("\n--- risky vertex pairs (bind pose, mesh space) ---\n");
    if (primaryPair.found)
        std::printf("PRIMARY  : bone[%d]=%s (v%zu) <-> bone[%d]=%s (v%zu)  rest dist = %.2f mm\n",
                    primaryPair.boneA, boneName(primaryPair.boneA).c_str(), primaryPair.vA,
                    primaryPair.boneB, boneName(primaryPair.boneB).c_str(), primaryPair.vB,
                    static_cast<double>(primaryPair.restDist) * 1000.0);
    else
        std::printf("PRIMARY  : no r-hand/l-finger1 vertex pair found (mesh may not weight these bones)\n");
    if (secondaryPair.found)
        std::printf("SECONDARY: bone[%d]=%s (v%zu) <-> bone[%d]=%s (v%zu)  rest dist = %.2f mm\n",
                    secondaryPair.boneA, boneName(secondaryPair.boneA).c_str(), secondaryPair.vA,
                    secondaryPair.boneB, boneName(secondaryPair.boneB).c_str(), secondaryPair.vB,
                    static_cast<double>(secondaryPair.restDist) * 1000.0);
    else
        std::printf("SECONDARY: no l-handprop/sibling vertex pair found\n");

    std::set<int> riskyBones = {kBoneRHand, kBoneLFinger1, kBoneLHandprop};
    if (secondaryPair.found) riskyBones.insert(secondaryPair.boneB);

    // --- PHASE 1: population scan over every clean clip in the anim archive ---
    std::vector<uint8_t> animArchive = readFile(animArchivePath);
    if (animArchive.empty()) { std::fprintf(stderr, "could not read %s\n", animArchivePath.c_str()); return 1; }
    {
        vpp::Container animContainer(vpp::ByteView(animArchive.data(), animArchive.size()));
        walkClips(animContainer, bones.size(), riskyBones);
    }

    std::printf("\n--- PHASE 1: population scan (%s) ---\n", animArchivePath.c_str());
    std::printf("clean clips total: %zu   compatible with this rig: %zu\n", g_totalCleanClips, g_compatibleClips);

    std::map<int, std::vector<double>> perBoneAngles;
    for (const auto& obs : g_observations) perBoneAngles[obs.boneIdx].push_back(obs.maxAngleDeg);

    for (int b : riskyBones) {
        std::string label = "bone[" + std::to_string(b) + "]=" + boneName(b);
        printDistribution(label.c_str(), perBoneAngles[b]);
    }

    // Combined "primary pair" and "secondary pair" per-clip driving angle:
    // for a given clip, how far does the MORE-ROTATED of the pair's two
    // bones get pushed (0 if a side has no track in that clip at all).
    std::vector<double> primaryCombined, secondaryCombined;
    std::vector<std::pair<double, std::string>> primaryRanked, secondaryRanked;
    for (const auto& kv : g_perClipBoneAngle) {
        const std::string& clip = kv.first;
        const auto& m = kv.second;
        auto get = [&](int b) -> double { auto it = m.find(b); return it == m.end() ? 0.0 : it->second; };
        double pa = std::max(get(kBoneRHand), get(kBoneLFinger1));
        if (m.count(kBoneRHand) || m.count(kBoneLFinger1)) {
            primaryCombined.push_back(pa);
            primaryRanked.push_back({pa, clip});
        }
        if (secondaryPair.found) {
            double sa = std::max(get(kBoneLHandprop), get(secondaryPair.boneB));
            if (m.count(kBoneLHandprop) || m.count(secondaryPair.boneB)) {
                secondaryCombined.push_back(sa);
                secondaryRanked.push_back({sa, clip});
            }
        }
    }
    printDistribution("PRIMARY PAIR (max of 22,23)", primaryCombined);
    if (secondaryPair.found)
        printDistribution("SECONDARY PAIR (max of 27,other)", secondaryCombined);

    // --- Top clips by primary-pair driving angle, for the record ---
    std::sort(primaryRanked.begin(), primaryRanked.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    std::printf("\ntop 10 clips by PRIMARY pair (22 r-hand / 23 l-finger1) driving angle:\n");
    for (size_t i = 0; i < primaryRanked.size() && i < 10; ++i)
        std::printf("  %-40s %6.2f deg\n", primaryRanked[i].second.c_str(), primaryRanked[i].first);

    // --- PHASE 2: representative stretch check at p10/p50/p90 ---
    auto runPhase2 = [&](const char* label, const std::vector<std::pair<double, std::string>>& ranked,
                         int boneA, int boneB, size_t vA, size_t vB, float restDist) {
        if (ranked.empty() || restDist <= 0.0f) return;
        std::vector<std::pair<double, std::string>> sorted = ranked;
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        std::printf("\n--- PHASE 2: %s stretch check (vertex %zu <-> %zu, rest %.2fmm) ---\n", label, vA, vB,
                    static_cast<double>(restDist) * 1000.0);
        const double percentiles[3] = {10.0, 50.0, 90.0};
        const char* names[3] = {"p10", "p50 (median)", "p90"};
        std::vector<uint32_t> parentIdx(bones.size());
        std::vector<std::array<float, 3>> restRig(bones.size());
        for (size_t i = 0; i < bones.size(); ++i) {
            parentIdx[i] = bones[i].parentIndex;
            restRig[i] = bones[i].restPosition;
        }
        for (int pi = 0; pi < 3; ++pi) {
            size_t idx = static_cast<size_t>(std::round(percentiles[pi] / 100.0 * (sorted.size() - 1)));
            const std::string& clipName = sorted[idx].second;
            double drivingAngle = sorted[idx].first;

            // Re-locate the clip and re-walk its payload (cheap - single
            // clip) to get the exact peak time for whichever of the two
            // bones drives this clip's angle.
            vpp::Container animContainer(vpp::ByteView(animArchive.data(), animArchive.size()));
            std::vector<uint8_t> clipBytesVec, unused2;
            if (!findEntry(animContainer, clipName, clipBytesVec, unused2, "")) {
                std::printf("  %-14s %-40s  (could not re-locate clip)\n", names[pi], clipName.c_str());
                continue;
            }
            vpp::ByteView clipBytes(clipBytesVec.data(), clipBytesVec.size());
            sr3anim::Animation anim = sr3anim::Animation::parse(clipBytes);
            sr3anim::Payload payload = sr3anim::Payload::walk(clipBytes, anim);
            const float duration = static_cast<float>(anim.durationTotal());

            const auto& tracks = payload.tracks();
            const bool hasTable = anim.hasTrackBoneTable();
            const uint32_t tableOffset = anim.trackBoneTableOffset();
            float peakTime = 0.0f;
            double bestAngle = -1.0;
            for (size_t t = 0; t < tracks.size(); ++t) {
                size_t bi = t;
                if (hasTable) {
                    size_t at = static_cast<size_t>(tableOffset) + t;
                    if (at < clipBytes.size()) bi = clipBytes.at(at);
                }
                if (static_cast<int>(bi) != boneA && static_cast<int>(bi) != boneB) continue;
                if (tracks[t].rotationKeys == 0) continue;
                std::vector<sr3anim::RotationSample> rots = payload.rotations(clipBytes, t);
                for (size_t k = 0; k < rots.size(); ++k) {
                    double ang = angleDegFromIdentity(rots[k].value);
                    if (ang > bestAngle) {
                        bestAngle = ang;
                        peakTime = (rots.size() > 1 && duration > 0.0f)
                                       ? static_cast<float>(k) * duration / static_cast<float>(rots.size() - 1)
                                       : 0.0f;
                    }
                }
            }

            std::vector<sr3anim::BoneSample> samples =
                sr3anim::sampleClipAtTime(clipBytes, anim, payload, bones.size(), peakTime);
            std::vector<sr3rig::Rotation3> rotsRig(bones.size(), sr3rig::identityRotation3());
            std::vector<std::array<float, 3>> deltasRig(bones.size(), std::array<float, 3>{0.0f, 0.0f, 0.0f});
            for (size_t b = 0; b < bones.size() && b < samples.size(); ++b) {
                const auto& s = samples[b];
                rotsRig[b] = sr3rig::quatToRotation3(s.rotation.x, s.rotation.y, s.rotation.z, s.rotation.w);
                deltasRig[b] = s.translationDelta;
            }
            std::vector<sr3rig::Mat3x4> skinRig =
                sr3rig::computeAnimatedSkinningMatrices(parentIdx, restRig, &rotsRig, &deltasRig);
            std::vector<sr3rig::Mat3x4> skinMesh(bones.size());
            for (size_t b = 0; b < bones.size(); ++b) skinMesh[b] = sr3rig::conjugateToMeshSpace(skinRig[b]);
            std::vector<std::array<float, 3>> posed = sr3rig::skinVertices(verts, skinMesh);

            float dx = posed[vA][0] - posed[vB][0];
            float dy = posed[vA][1] - posed[vB][1];
            float dz = posed[vA][2] - posed[vB][2];
            float posedDist = std::sqrt(dx * dx + dy * dy + dz * dz);
            double ratio = static_cast<double>(posedDist) / static_cast<double>(restDist);

            std::printf("  %-14s %-40s driving=%6.2fdeg peakT=%7.2f/%7.2f  posed=%.4f units (%.1fmm->%.1fmm)  RATIO=%.1fx\n",
                        names[pi], clipName.c_str(), drivingAngle, static_cast<double>(peakTime),
                        static_cast<double>(duration), static_cast<double>(posedDist),
                        static_cast<double>(restDist) * 1000.0, static_cast<double>(posedDist) * 1000.0, ratio);
        }
    };

    if (primaryPair.found)
        runPhase2("PRIMARY (r-hand / l-finger1)", primaryRanked, kBoneRHand, kBoneLFinger1,
                  primaryPair.vA, primaryPair.vB, primaryPair.restDist);
    if (secondaryPair.found)
        runPhase2("SECONDARY (l-handprop / sibling)", secondaryRanked, kBoneLHandprop, secondaryPair.boneB,
                  secondaryPair.vA, secondaryPair.vB, secondaryPair.restDist);

    return 0;
}
