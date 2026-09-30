// Cheap follow-up to match_anim_rig.cpp (HANDOFF.md Sec9.56.1's open
// question: "whether a different mesh/clip pairing renders cleanly").
//
// match_anim_rig.cpp already finds mesh/rig/clip compatibility, but its
// per-rig listing is sorted by animatedTracks (busiest clip first) and
// capped at 15, so it can never surface a CALM clip (walk/idle/gesture)
// over a busy cinematic one ("_shot", "auto_entr*", "epicheli_exit", the
// exact kind of clip that produced brad's fragmentation). This is a pure
// listing tool: same "clean" population filter as match_anim_rig (flags
// bit 0x40 clear, walk lands exactly on the file's own declared +0x30
// offset), but dumps every clip whose name contains a given substring
// (case-insensitive), regardless of how many tracks it animates, so a
// human can pick "walk_bwd_beltbuckle" over "auto_entrl_extct_shot" for a
// first "does this generalize" test.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "sr3anim/animation.h"
#include "sr3anim/payload.h"
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

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
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

std::string g_filter;
size_t g_shown = 0;
size_t g_totalClean = 0;

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
                    if (p.walkComplete() && !p.hasUnaccountedPayload() &&
                        a.hasTrailingOffset() && p.landedOnDeclaredEnd()) {
                        ++g_totalClean;
                        if (g_filter.empty() || lower(e.name).find(g_filter) != std::string::npos) {
                            size_t animatedTracks = 0, transTracks = 0;
                            for (const auto& t : p.tracks()) {
                                if (t.rotationKeys >= 2) ++animatedTracks;
                                if (t.translationKeys >= 1) ++transTracks;
                            }
                            std::printf("  %-36s tracks=%3zu animatedTracks=%3zu tracksWithTrans=%3zu "
                                        "duration=%u table=%s\n",
                                        e.name.c_str(), p.tracks().size(), animatedTracks, transTracks,
                                        a.durationTotal(), a.hasTrackBoneTable() ? "yes" : "no");
                            ++g_shown;
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
    if (argc < 2) {
        std::fprintf(stderr, "usage: list_clean_clips <anim_archive.vpp_pc> [nameSubstringFilter]\n");
        return 1;
    }
    if (argc >= 3) g_filter = lower(argv[2]);

    std::vector<uint8_t> b = readFile(argv[1]);
    if (b.empty()) { std::fprintf(stderr, "could not read %s\n", argv[1]); return 1; }
    vpp::Container c(vpp::ByteView(b.data(), b.size()));
    walkClips(c);

    std::printf("total clean clips: %zu   matching '%s': %zu\n", g_totalClean, g_filter.c_str(),
                g_shown);
    return 0;
}
