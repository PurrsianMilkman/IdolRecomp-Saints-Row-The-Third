// Population/stress test for the real U4 layer-sampler -> compose pipeline
// (sr3animpose, spec-anim-runtime.md Sec11 - see pose_compose.h/
// clip_pose_bridge.h's own file banners for the exact algorithm and
// citations). This is a REAL correctness check against real shipped data,
// not just "it looks fine in brad's own walk demo": it runs
// sr3animpose::ComposeClipPose() against MANY real, clean
// (spec-anim-format.md Sec6c.4/Sec10: flags bit 0x40 clear, walk lands
// exactly on the file's own declared +0x30 offset) locomotion-family
// `.anim_pc` clips, matched against SEVERAL real rigs (different bone
// counts), under several synthetic active-layer scenarios (single clip,
// two-clip cross-fade at several splits, three-clip priority-group
// masking) - checking every resulting bone's rotation/translationDelta for
// NaN/Inf and the rotation's own unit length, and counting the library's
// own HOST-SAFETY GUARD hits (ComposeGuardStats).
//
// Reuses tools/match_anim_rig.cpp's own rig/clip discovery exactly (same
// "clean population" filter, same recursive container walk, same
// compatibility rule: every bone index a clip's tracks touch must be <
// the rig's bone count) rather than re-deriving it.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "sr3anim/animation.h"
#include "sr3anim/payload.h"
#include "sr3animpose/clip_pose_bridge.h"
#include "sr3animpose/pose_compose.h"
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

bool endsWith(const std::string& s, const std::string& x) {
    return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}

bool containsAny(const std::string& nameLower, const std::vector<std::string>& needles) {
    for (const auto& n : needles)
        if (nameLower.find(n) != std::string::npos) return true;
    return false;
}

bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView r = c.rawEntryBytes(i);
        out.assign(r.data(), r.data() + r.size());
        return true;
    }
    auto r = c.decompressEntry(i);
    const bool usable = r.status == vpp::DecodeStatus::Ok || r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
                        r.status == vpp::DecodeStatus::ContentValidated ||
                        r.status == vpp::DecodeStatus::RecoveredSharedStream ||
                        r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!usable) return false;
    out = std::move(r.data);
    return true;
}

struct RigPick {
    std::string name;
    sr3rig::Rig rig;
};

void walkRigs(const vpp::Container& c, const std::vector<std::string>& wantedNames, std::vector<RigPick>& out) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const auto& e = c.entries()[i];
        if (endsWith(e.name, ".rig_pc")) {
            for (const auto& w : wantedNames) {
                if (e.name != w) continue;
                std::vector<uint8_t> b;
                if (entryBytes(c, i, b)) {
                    try {
                        out.push_back({e.name, sr3rig::Rig::parse(vpp::ByteView(b.data(), b.size()))});
                    } catch (const std::exception&) {
                    }
                }
            }
        }
        if (e.payload.kind == vpp::PayloadKind::Raw) {
            try { walkRigs(c.openNested(i), wantedNames, out); } catch (const std::exception&) {}
        }
    }
}

// One real, compatible, loaded clip - keeps bytes/Animation/Payload alive
// for the ClipSource below.
struct LoadedClip {
    std::string name;
    std::vector<uint8_t> bytes;
    sr3anim::Animation animation;
    sr3anim::Payload payload;
};

void walkClipsForRig(const vpp::Container& c, size_t rigBoneCount, size_t cap,
                     const std::vector<std::string>& nameFilter, std::vector<std::unique_ptr<LoadedClip>>& out) {
    for (size_t i = 0; i < c.entries().size() && out.size() < cap; ++i) {
        const auto& e = c.entries()[i];
        if (endsWith(e.name, ".anim_pc")) {
            std::string lower = e.name;
            for (auto& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (!nameFilter.empty() && !containsAny(lower, nameFilter)) continue;
            std::vector<uint8_t> b;
            if (entryBytes(c, i, b)) {
                try {
                    vpp::ByteView bytes(b.data(), b.size());
                    sr3anim::Animation a = sr3anim::Animation::parse(bytes);
                    sr3anim::Payload p = sr3anim::Payload::walk(bytes, a);
                    if (!p.walkComplete() || p.hasUnaccountedPayload() || !a.hasTrailingOffset() ||
                        !p.landedOnDeclaredEnd())
                        continue;  // not the clean population (spec-anim-format.md Sec6c.4/Sec10)
                    size_t maxIdx = 0;
                    if (a.hasTrackBoneTable()) {
                        const uint32_t off = a.trackBoneTableOffset();
                        for (size_t t = 0; t < p.tracks().size(); ++t)
                            if (off + t < bytes.size()) maxIdx = std::max<size_t>(maxIdx, bytes.at(off + t));
                    } else {
                        maxIdx = p.tracks().empty() ? 0 : p.tracks().size() - 1;
                    }
                    if (maxIdx >= rigBoneCount) continue;  // not compatible with this rig
                    auto lc = std::make_unique<LoadedClip>();
                    lc->name = e.name;
                    lc->bytes = std::move(b);
                    lc->animation = a;
                    lc->payload = p;
                    out.push_back(std::move(lc));
                    if (out.size() >= cap) return;
                } catch (const std::exception&) {
                }
            }
        }
        if (e.payload.kind == vpp::PayloadKind::Raw && out.size() < cap) {
            try { walkClipsForRig(c.openNested(i), rigBoneCount, cap, nameFilter, out); } catch (const std::exception&) {}
        }
    }
}

class VectorClipSource : public sr3animpose::ClipSource {
public:
    explicit VectorClipSource(const std::vector<std::unique_ptr<LoadedClip>>& clips) {
        resolved_.reserve(clips.size());
        for (const auto& c : clips) {
            sr3animpose::ResolvedClip rc;
            rc.animation = &c->animation;
            rc.payload = &c->payload;
            rc.bytes = vpp::ByteView(c->bytes.data(), c->bytes.size());
            resolved_.push_back(rc);
        }
    }
    const sr3animpose::ResolvedClip* lookup(int32_t record) override {
        if (record < 0 || static_cast<size_t>(record) >= resolved_.size()) return nullptr;
        return &resolved_[static_cast<size_t>(record)];
    }

private:
    std::vector<sr3animpose::ResolvedClip> resolved_;
};

struct Stats {
    long long scenarios = 0;
    long long bonesChecked = 0;
    long long nonFiniteRotation = 0;
    long long nonFiniteTranslation = 0;
    long long offUnitLength = 0;  // |len(rotation)-1| > tolerance, among bones with any active weight
    long long degenerateGuardHits = 0;
    long long nonFiniteContributorGuardHits = 0;
};

void checkResult(const sr3animpose::PoseComposeResult& r, size_t boneCount, Stats& stats) {
    ++stats.scenarios;
    stats.degenerateGuardHits += static_cast<long long>(r.guard.degenerateQuaternionBones);
    stats.nonFiniteContributorGuardHits += static_cast<long long>(r.guard.nonFiniteContributorsSkipped);
    for (size_t b = 0; b < boneCount && b < r.bones.size(); ++b) {
        ++stats.bonesChecked;
        const sr3anim::BoneSample& s = r.bones[b];
        const bool rotFinite = std::isfinite(s.rotation.x) && std::isfinite(s.rotation.y) &&
                               std::isfinite(s.rotation.z) && std::isfinite(s.rotation.w);
        const bool tFinite = std::isfinite(s.translationDelta[0]) && std::isfinite(s.translationDelta[1]) &&
                             std::isfinite(s.translationDelta[2]);
        if (!rotFinite) { ++stats.nonFiniteRotation; continue; }
        if (!tFinite) ++stats.nonFiniteTranslation;
        const double lenSq = double(s.rotation.x) * s.rotation.x + double(s.rotation.y) * s.rotation.y +
                             double(s.rotation.z) * s.rotation.z + double(s.rotation.w) * s.rotation.w;
        if (std::fabs(lenSq - 1.0) > 1e-3) ++stats.offUnitLength;
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: validate_anim_pose_compose_population <characters.vpp_pc> <preload_anim.vpp_pc>\n");
        return 1;
    }
    const std::string charactersPath = argv[1];
    const std::string animPath = argv[2];

    std::vector<uint8_t> charBytes = readFile(charactersPath);
    std::vector<uint8_t> animBytes = readFile(animPath);
    if (charBytes.empty() || animBytes.empty()) {
        std::fprintf(stderr, "could not read input archives\n");
        return 1;
    }

    std::vector<RigPick> rigs;
    {
        vpp::Container c(vpp::ByteView(charBytes.data(), charBytes.size()));
        walkRigs(c, {"brad.rig_pc", "shaundi.rig_pc"}, rigs);
    }
    if (rigs.empty()) {
        std::fprintf(stderr, "no target rigs found\n");
        return 1;
    }

    // Locomotion-family name filter - "a handful of real locomotion graphs
    // and clips" per this task's own wording; real shipped naming already
    // groups these by substring (list_clean_clips.cpp's own precedent).
    const std::vector<std::string> nameFilter = {"run", "wlk", "walk", "jog", "sprint", "idle", "stand", "bped"};
    constexpr size_t kCapPerRig = 60;

    Stats totalStats;
    for (const RigPick& rp : rigs) {
        std::vector<std::unique_ptr<LoadedClip>> clips;
        {
            vpp::Container c(vpp::ByteView(animBytes.data(), animBytes.size()));
            walkClipsForRig(c, rp.rig.bones().size(), kCapPerRig, nameFilter, clips);
        }
        std::printf("=== rig %s (%zu bones): %zu compatible locomotion-family clean clips loaded ===\n",
                    rp.name.c_str(), rp.rig.bones().size(), clips.size());
        if (clips.empty()) continue;

        VectorClipSource clipSource(clips);
        Stats rigStats;
        const size_t boneCount = rp.rig.bones().size();

        // Scenario A: each clip alone, at several phases across its own
        // duration (weight 1.0, group 0 - the single-layer common case).
        for (size_t ci = 0; ci < clips.size(); ++ci) {
            const float duration = static_cast<float>(clips[ci]->animation.durationTotal());
            for (float frac : {0.0f, 0.25f, 0.5f, 0.75f, 0.99f}) {
                std::vector<sr3animpose::ActiveLayer> layers = {
                    {static_cast<int32_t>(ci), 1.0f, 0, false, frac * duration}};
                sr3animpose::PoseComposeResult r = sr3animpose::ComposeClipPose(layers, clipSource, boneCount);
                checkResult(r, boneCount, rigStats);
            }
        }

        // Scenario B: two-clip cross-fades (spec-anim-runtime.md Sec8.3
        // pass 1's own linear-crossfade shape: weights sum to 1), several
        // splits, over consecutive clip pairs in the loaded set.
        for (size_t ci = 0; ci + 1 < clips.size(); ++ci) {
            const float dA = static_cast<float>(clips[ci]->animation.durationTotal());
            const float dB = static_cast<float>(clips[ci + 1]->animation.durationTotal());
            for (float p : {0.1f, 0.3f, 0.5f, 0.7f, 0.9f}) {
                std::vector<sr3animpose::ActiveLayer> layers = {
                    {static_cast<int32_t>(ci), 1.0f - p, 0, false, 0.4f * dA},
                    {static_cast<int32_t>(ci + 1), p, 0, false, 0.6f * dB},
                };
                sr3animpose::PoseComposeResult r = sr3animpose::ComposeClipPose(layers, clipSource, boneCount);
                checkResult(r, boneCount, rigStats);
            }
        }

        // Scenario C: three-clip priority-group masking (spec-player-
        // runtime.md Sec5.4.3 step 3's own formula) - a base layer (group
        // 0) plus two progressively higher-priority override layers (group
        // 1, group 2) at varying weights, over consecutive clip triples.
        for (size_t ci = 0; ci + 2 < clips.size(); ++ci) {
            const float d0 = static_cast<float>(clips[ci]->animation.durationTotal());
            const float d1 = static_cast<float>(clips[ci + 1]->animation.durationTotal());
            const float d2 = static_cast<float>(clips[ci + 2]->animation.durationTotal());
            for (float w1 : {0.0f, 0.3f, 0.6f, 1.0f}) {
                for (float w2 : {0.0f, 0.4f}) {
                    std::vector<sr3animpose::ActiveLayer> layers = {
                        {static_cast<int32_t>(ci), 1.0f, 0, false, 0.2f * d0},
                        {static_cast<int32_t>(ci + 1), w1, 1, false, 0.5f * d1},
                        {static_cast<int32_t>(ci + 2), w2, 2, false, 0.8f * d2},
                    };
                    sr3animpose::PoseComposeResult r = sr3animpose::ComposeClipPose(layers, clipSource, boneCount);
                    checkResult(r, boneCount, rigStats);
                }
            }
        }

        std::printf("    scenarios=%lld bonesChecked=%lld nonFiniteRotation=%lld nonFiniteTranslation=%lld "
                    "offUnitLength=%lld degenerateGuardHits=%lld nonFiniteContributorGuardHits=%lld\n",
                    rigStats.scenarios, rigStats.bonesChecked, rigStats.nonFiniteRotation,
                    rigStats.nonFiniteTranslation, rigStats.offUnitLength, rigStats.degenerateGuardHits,
                    rigStats.nonFiniteContributorGuardHits);

        totalStats.scenarios += rigStats.scenarios;
        totalStats.bonesChecked += rigStats.bonesChecked;
        totalStats.nonFiniteRotation += rigStats.nonFiniteRotation;
        totalStats.nonFiniteTranslation += rigStats.nonFiniteTranslation;
        totalStats.offUnitLength += rigStats.offUnitLength;
        totalStats.degenerateGuardHits += rigStats.degenerateGuardHits;
        totalStats.nonFiniteContributorGuardHits += rigStats.nonFiniteContributorGuardHits;
    }

    std::printf("\n=== TOTAL across %zu rig(s): scenarios=%lld bonesChecked=%lld nonFiniteRotation=%lld "
                "nonFiniteTranslation=%lld offUnitLength=%lld degenerateGuardHits=%lld "
                "nonFiniteContributorGuardHits=%lld ===\n",
                rigs.size(), totalStats.scenarios, totalStats.bonesChecked, totalStats.nonFiniteRotation,
                totalStats.nonFiniteTranslation, totalStats.offUnitLength, totalStats.degenerateGuardHits,
                totalStats.nonFiniteContributorGuardHits);

    const bool clean = totalStats.nonFiniteRotation == 0 && totalStats.nonFiniteTranslation == 0 &&
                       totalStats.offUnitLength == 0;
    std::printf("%s\n", clean ? "POPULATION TEST CLEAN: no NaN/Inf/off-unit-length transform found."
                              : "POPULATION TEST FOUND ISSUES - see counts above.");
    return clean ? 0 : 1;
}
