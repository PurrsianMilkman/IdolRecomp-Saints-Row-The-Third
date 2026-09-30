// Do REAL shipped .anim_pc clips actually rotate the "risky" seam bones
// (§9.56/§9.56.1/§9.56.2) far enough to trigger the observed 175x-291x
// near-degenerate-edge stretch, or did the handful of test clips tried so
// far happen to be unusually extreme? File-side complement to Team A's
// disassembly investigation into how the shipped engine avoids visible
// seams - this probe only asks whether typical clips would even need such
// a technique, using nothing but the already-CONFIRMED rotation decode.
//
// Usage: probe_seam_rotation_range <characters.vpp_pc> <anim_archive.vpp_pc>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "sr3anim/animation.h"
#include "sr3anim/payload.h"
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
    if (r.status != vpp::DecodeStatus::Ok && r.status != vpp::DecodeStatus::OkUnconfirmedContent &&
        r.status != vpp::DecodeStatus::ContentValidated &&
        r.status != vpp::DecodeStatus::RecoveredSharedStream &&
        r.status != vpp::DecodeStatus::RecoveredSharedStreamLongChain) return false;
    out = std::move(r.data);
    return true;
}
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

double angleDegFromQuat(float w) {
    double ww = w;
    if (ww > 1.0) ww = 1.0;
    if (ww < -1.0) ww = -1.0;
    return 2.0 * std::acos(std::fabs(ww)) * 180.0 / 3.14159265358979323846;
}

struct BoneRisk { std::string name; size_t index; };

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("usage: probe_seam_rotation_range <characters.vpp_pc> <anim_archive.vpp_pc>\n");
        return 1;
    }
    std::vector<uint8_t> charArchive = readFile(argv[1]);
    std::vector<uint8_t> animArchiveBytes = readFile(argv[2]);
    if (charArchive.empty() || animArchiveBytes.empty()) {
        printf("could not read one of the archives\n");
        return 1;
    }

    // Find brad.rig_pc and resolve the risky bone indices by name.
    std::vector<uint8_t> rigBytes;
    try {
        vpp::Container c(vpp::ByteView(charArchive.data(), charArchive.size()));
        std::function<bool(const vpp::Container&)> findRig = [&](const vpp::Container& cc) -> bool {
            for (size_t i = 0; i < cc.entries().size(); ++i) {
                if (endsWith(cc.entries()[i].name, "brad.rig_pc")) {
                    if (entryBytes(cc, i, rigBytes)) return true;
                }
            }
            for (size_t i = 0; i < cc.entries().size(); ++i) {
                if (cc.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
                    try { if (findRig(cc.openNested(i))) return true; } catch (const std::exception&) {}
                }
            }
            return false;
        };
        findRig(c);
    } catch (const std::exception& e) {
        printf("archive error: %s\n", e.what());
        return 1;
    }
    if (rigBytes.empty()) {
        printf("brad.rig_pc not found\n");
        return 1;
    }
    sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(rigBytes.data(), rigBytes.size()));
    const auto& bones = rig.bones();

    std::vector<BoneRisk> risky;
    const char* wanted[] = {"r-hand", "l-finger1", "l-handprop", "l-finger4", "l-thumb1"};
    for (const char* w : wanted) {
        for (size_t i = 0; i < bones.size(); ++i) {
            if (lower(bones[i].name) == lower(std::string(w))) {
                risky.push_back({bones[i].name, i});
                break;
            }
        }
    }
    printf("risky bones resolved on brad.rig_pc (%zu bones total):\n", bones.size());
    for (const auto& r : risky) printf("  [%zu] %s\n", r.index, r.name.c_str());
    if (risky.empty()) {
        printf("no risky bones resolved by name - aborting\n");
        return 1;
    }

    // Per risky bone: every clip's max rotation angle (degrees) reached on
    // whichever track maps to that bone.
    std::map<size_t, std::vector<double>> maxAngleByBone;
    std::map<size_t, std::vector<std::string>> clipNamesByBone;
    int clipsScanned = 0, clipsWalked = 0;

    std::vector<std::pair<std::string, std::vector<uint8_t>>> clips;
    try {
        vpp::Container c(vpp::ByteView(animArchiveBytes.data(), animArchiveBytes.size()));
        std::vector<const vpp::Container*> stack{&c};
        // Simple recursive walk mirroring this project's other harnesses.
        std::function<void(const vpp::Container&)> walk = [&](const vpp::Container& cc) {
            for (size_t i = 0; i < cc.entries().size(); ++i) {
                if (endsWith(cc.entries()[i].name, ".anim_pc")) {
                    std::vector<uint8_t> b;
                    if (entryBytes(cc, i, b)) clips.push_back({cc.entries()[i].name, b});
                }
                if (cc.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
                    try { walk(cc.openNested(i)); } catch (const std::exception&) {}
                }
            }
        };
        walk(c);
    } catch (const std::exception& e) {
        printf("anim archive error: %s\n", e.what());
        return 1;
    }
    printf("\n.anim_pc clips found: %zu\n", clips.size());

    for (const auto& kv : clips) {
        ++clipsScanned;
        const std::string& name = kv.first;
        const std::vector<uint8_t>& raw = kv.second;
        vpp::ByteView bv(raw.data(), raw.size());
        sr3anim::Animation a;
        try { a = sr3anim::Animation::parse(bv); } catch (...) { continue; }
        sr3anim::Payload pl = sr3anim::Payload::walk(bv, a);
        if (!pl.walkComplete()) continue;
        ++clipsWalked;

        const bool hasTable = a.hasTrackBoneTable();
        const uint32_t tableOffset = a.trackBoneTableOffset();
        const auto& tracks = pl.tracks();

        for (size_t trackIdx = 0; trackIdx < tracks.size(); ++trackIdx) {
            size_t boneIdx = trackIdx;
            if (hasTable) {
                size_t at = static_cast<size_t>(tableOffset) + trackIdx;
                if (at >= raw.size()) continue;
                boneIdx = raw[at];
            }
            bool isRisky = false;
            for (const auto& r : risky) if (r.index == boneIdx) { isRisky = true; break; }
            if (!isRisky) continue;

            std::vector<sr3anim::RotationSample> rots = pl.rotations(bv, trackIdx);
            if (rots.empty()) continue;
            double maxAngle = 0.0;
            for (const auto& rs : rots) {
                double ang = angleDegFromQuat(rs.value.w);
                if (ang > maxAngle) maxAngle = ang;
            }
            maxAngleByBone[boneIdx].push_back(maxAngle);
            if (clipNamesByBone[boneIdx].size() < 5) clipNamesByBone[boneIdx].push_back(name);
        }
    }

    printf("clips scanned: %d   clips with a complete walk: %d\n\n", clipsScanned, clipsWalked);

    auto pct = [](std::vector<double> v, double p) {
        if (v.empty()) return 0.0;
        std::sort(v.begin(), v.end());
        size_t idx = static_cast<size_t>(p * static_cast<double>(v.size() - 1));
        return v[idx];
    };

    printf("=== max rotation angle (degrees) reached per clip, by risky bone ===\n");
    for (const auto& r : risky) {
        auto it = maxAngleByBone.find(r.index);
        if (it == maxAngleByBone.end() || it->second.empty()) {
            printf("  [%zu] %-14s : 0 clips touch this bone\n", r.index, r.name.c_str());
            continue;
        }
        const auto& v = it->second;
        double mn = *std::min_element(v.begin(), v.end());
        double mx = *std::max_element(v.begin(), v.end());
        double med = pct(v, 0.5);
        double p90 = pct(v, 0.9);
        printf("  [%zu] %-14s : %zu clips   min=%.1f  median=%.1f  p90=%.1f  max=%.1f (degrees)\n",
               r.index, r.name.c_str(), v.size(), mn, med, p90, mx);
        printf("       example clips: ");
        for (const auto& n : clipNamesByBone[r.index]) printf("%s ", n.c_str());
        printf("\n");
    }
    return 0;
}
