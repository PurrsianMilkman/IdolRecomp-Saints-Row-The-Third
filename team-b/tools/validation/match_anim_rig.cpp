// Stage-2 prerequisite: find an .anim_pc clip whose track->bone mapping is
// actually compatible with a real .rig_pc, so sr3_viewer's new `animpose`
// command has something real to play back.
//
// Not a format probe - a SEARCH tool. Lists every .rig_pc in
// characters.vpp_pc (name, bone count) and every CLEAN .anim_pc in
// preload_anim.vpp_pc (name, declared track count +0x0A, the +0x28
// track->bone table if present, and how many tracks actually carry >=2
// rotation keys, since a clip that "fits" but never rotates anything
// renders a static bind pose). "Clean" = flags bit 0x40 clear AND the walk
// lands exactly on the file's own declared +0x30 offset - the population
// this session's own reader is fully confirmed against (HANDOFF Sec9.48,
// spec Sec6c.1: 3,687/3,691).
//
// Compatibility rule: every bone index the clip's tracks touch (via the
// +0x28 table, or identity track==bone when absent) must be < the rig's
// bone count. Reports the best few matches by name so a human command
// line can be built from them.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "sr3anim/animation.h"
#include "sr3anim/payload.h"
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

struct RigInfo {
    std::string name;
    size_t boneCount = 0;
};

struct ClipInfo {
    std::string name;
    size_t trackCount = 0;
    size_t maxBoneIndexTouched = 0;
    size_t animatedTracks = 0;  // rotKeys >= 2
    size_t tracksWithTranslation = 0;
    bool hasTable = false;
};

std::vector<RigInfo> g_rigs;
std::vector<ClipInfo> g_clips;
std::string g_dumpName;

void walkRigs(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const auto& e = c.entries()[i];
        if (endsWith(e.name, ".rig_pc")) {
            std::vector<uint8_t> b;
            if (entryBytes(c, i, b)) {
                try {
                    sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(b.data(), b.size()));
                    g_rigs.push_back({e.name, rig.bones().size()});
                } catch (const std::exception&) {
                }
            }
        }
        if (e.payload.kind == vpp::PayloadKind::Raw) {
            try { walkRigs(c.openNested(i)); } catch (const std::exception&) {}
        }
    }
}

void walkClips(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const auto& e = c.entries()[i];
        if (endsWith(e.name, ".anim_pc")) {
            std::vector<uint8_t> b;
            if (entryBytes(c, i, b)) {
                try {
                    vpp::ByteView bytes(b.data(), b.size());
                    sr3anim::Animation a = sr3anim::Animation::parse(bytes);
                    sr3anim::Payload p = sr3anim::Payload::walk(bytes, a);
                    // Restrict to the confirmed-clean population only.
                    if (p.walkComplete() && !p.hasUnaccountedPayload() &&
                        a.hasTrailingOffset() && p.landedOnDeclaredEnd()) {
                        ClipInfo ci;
                        ci.name = e.name;
                        ci.trackCount = p.tracks().size();
                        ci.hasTable = a.hasTrackBoneTable();
                        size_t maxIdx = ci.trackCount ? ci.trackCount - 1 : 0;
                        if (ci.hasTable) {
                            maxIdx = 0;
                            uint32_t off = a.trackBoneTableOffset();
                            for (size_t t = 0; t < ci.trackCount; ++t) {
                                if (off + t < bytes.size()) {
                                    uint8_t bi = bytes.at(off + t);
                                    if (bi > maxIdx) maxIdx = bi;
                                }
                            }
                        }
                        ci.maxBoneIndexTouched = maxIdx;
                        for (size_t t = 0; t < ci.trackCount; ++t) {
                            if (p.tracks()[t].rotationKeys >= 2) ++ci.animatedTracks;
                            if (p.tracks()[t].translationKeys >= 1) ++ci.tracksWithTranslation;
                        }
                        g_clips.push_back(ci);

                        if (!g_dumpName.empty() && e.name == g_dumpName) {
                            std::printf("\n--- dump: %s ---\n", e.name.c_str());
                            std::printf("flags=0x%02X durationTotal(+0x06)=%u tracks(+0x0A)=%u "
                                        "bones(+0x0B)=%u hasTable=%s trailingOffset=%u "
                                        "landedOnEnd=%s hasUnaccounted=%s\n",
                                        a.flags(), a.durationTotal(),
                                        static_cast<unsigned>(a.field_0x0A_rawCount()),
                                        static_cast<unsigned>(a.field_0x0B_rawCount()),
                                        a.hasTrackBoneTable() ? "yes" : "no",
                                        a.hasTrailingOffset() ? a.trailingOffset() : 0,
                                        p.landedOnDeclaredEnd() ? "yes" : "no",
                                        p.hasUnaccountedPayload() ? "yes" : "no");
                            const auto& rq = a.rootRotation();
                            const auto& rt = a.rootTranslation();
                            std::printf("root rotation=(%.4f %.4f %.4f %.4f) root translation="
                                        "(%.4f %.4f %.4f) |t|=%.4f\n",
                                        rq.x, rq.y, rq.z, rq.w, rt.x, rt.y, rt.z,
                                        std::sqrt(rt.x * rt.x + rt.y * rt.y + rt.z * rt.z));
                            for (size_t t = 0; t < p.tracks().size() && t < 70; ++t) {
                                const auto& tb = p.tracks()[t];
                                auto rots = p.rotations(bytes, t);
                                auto trans = p.translations(bytes, t);
                                std::printf("  track %2zu: rotKeys=%3u transKeys=%3u", t,
                                            tb.rotationKeys, tb.translationKeys);
                                if (!trans.empty()) {
                                    std::printf("  trans[0]=(%.4f,%.4f,%.4f) trans[last]=(%.4f,%.4f,%.4f)",
                                                trans.front().x, trans.front().y, trans.front().z,
                                                trans.back().x, trans.back().y, trans.back().z);
                                }
                                std::printf("\n");
                            }
                        }
                    }
                } catch (const std::exception&) {
                }
            }
        }
        if (e.payload.kind == vpp::PayloadKind::Raw) {
            try { walkClips(c.openNested(i)); } catch (const std::exception&) {}
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: match_anim_rig <characters.vpp_pc> <preload_anim.vpp_pc> "
                              "[rigNameFilter] [dumpClipName]\n");
        return 1;
    }
    std::string rigFilter = argc >= 4 ? argv[3] : "";
    g_dumpName = argc >= 5 ? argv[4] : "";

    {
        std::vector<uint8_t> b = readFile(argv[1]);
        if (b.empty()) { std::fprintf(stderr, "could not read %s\n", argv[1]); return 1; }
        vpp::Container c(vpp::ByteView(b.data(), b.size()));
        walkRigs(c);
    }
    {
        std::vector<uint8_t> b = readFile(argv[2]);
        if (b.empty()) { std::fprintf(stderr, "could not read %s\n", argv[2]); return 1; }
        vpp::Container c(vpp::ByteView(b.data(), b.size()));
        walkClips(c);
    }

    std::printf("rigs found: %zu   clean clips found: %zu\n", g_rigs.size(), g_clips.size());

    for (const RigInfo& rig : g_rigs) {
        if (!rigFilter.empty() && rig.name.find(rigFilter) == std::string::npos) continue;
        std::printf("\n=== rig %s (%zu bones) ===\n", rig.name.c_str(), rig.boneCount);
        std::vector<const ClipInfo*> matches;
        for (const ClipInfo& ci : g_clips) {
            if (ci.maxBoneIndexTouched < rig.boneCount && ci.trackCount > 0) {
                matches.push_back(&ci);
            }
        }
        std::sort(matches.begin(), matches.end(), [](const ClipInfo* a, const ClipInfo* b) {
            if (a->animatedTracks != b->animatedTracks) return a->animatedTracks > b->animatedTracks;
            return a->trackCount > b->trackCount;
        });
        size_t shown = 0;
        for (const ClipInfo* ci : matches) {
            if (shown >= 15) break;
            std::printf("  %-32s tracks=%3zu maxBoneIdx=%3zu animatedTracks=%3zu "
                        "tracksWithTrans=%3zu table=%s\n",
                        ci->name.c_str(), ci->trackCount, ci->maxBoneIndexTouched,
                        ci->animatedTracks, ci->tracksWithTranslation,
                        ci->hasTable ? "yes" : "no");
            ++shown;
        }
        std::printf("  (%zu total compatible clips)\n", matches.size());
    }
    return 0;
}
