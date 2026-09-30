// CROSS-FORMAT: do decoded .anim_pc translations agree with .rig_pc rest
// bone offsets?
//
// This is the strongest kind of check this project has, and the reason is
// structural: NOTHING in either decoding refers to the other. `sr3rig` was
// derived from spec-rig-format.md, `sr3anim`'s payload from
// spec-anim-format.md Sec6c, by different passes, from different
// disassembly, with different scale constants. If the two agree on real
// data, the agreement cannot come from a shared assumption - there isn't
// one.
//
// THE PHYSICAL CLAIM: a translation key is a bone's position relative to
// its parent. Bones are rigid - an upper arm does not change length during
// an animation - so an animated bone-local translation must stay very close
// to the rig's REST offset for that bone. If the two readers are both
// right, |animated - rest| is small. If either scale constant is wrong, or
// the track->bone mapping is wrong, they diverge.
//
// CONTROLS, because "small" is meaningless without a scale:
//   1. SHUFFLED MAPPING - compare each track to a DIFFERENT bone's rest
//      offset. Same numbers, same distributions, wrong correspondence.
//   2. SCALE PERTURBATION - decode translations with the base scale at
//      1/32 instead of 1/64. If the real reading is right this must get
//      clearly worse; if it doesn't, the test cannot see scale errors and
//      should not be quoted as confirming one.
//
// A failing case is physically reachable for both: a wrong mapping puts a
// finger bone's offset against a thigh's, and a wrong scale doubles every
// length. That is what makes this a test rather than a plausibility story.
//
// Usage: validate_anim_rig_crossformat <preload_anim.vpp_pc> <preload_rigs.vpp_pc>
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
    if (r.status != vpp::DecodeStatus::Ok &&
        r.status != vpp::DecodeStatus::OkUnconfirmedContent &&
        r.status != vpp::DecodeStatus::ContentValidated &&
        r.status != vpp::DecodeStatus::RecoveredSharedStream &&
        r.status != vpp::DecodeStatus::RecoveredSharedStreamLongChain) return false;
    out = std::move(r.data);
    return true;
}

// Rigs, keyed by bone count so an animation can be matched to a rig of the
// right size. Exact name pairing is not available (clip names do not encode
// their rig), so bone count is the association - and because that is an
// ASSUMPTION, the report states how many rigs share each count, which is the
// ambiguity this pairing carries.
std::map<size_t, std::vector<sr3rig::Rig>> g_rigsByBoneCount;
int g_rigsLoaded = 0, g_rigsFailed = 0;

std::vector<double> g_real, g_shuffled, g_wrongScale;
long long g_compared = 0;
int g_clips = 0, g_clipsNoRig = 0, g_clipsUsed = 0;
long long g_skippedNoTable = 0;

double median(std::vector<double>& v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
double pct(std::vector<double>& v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[static_cast<size_t>(p * static_cast<double>(v.size() - 1))];
}
double fracWithin(const std::vector<double>& v, double lim) {
    if (v.empty()) return 0.0;
    size_t n = 0;
    for (double d : v) if (d <= lim) ++n;
    return 100.0 * static_cast<double>(n) / static_cast<double>(v.size());
}

void loadRigs(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (endsWith(c.entries()[i].name, ".rig_pc")) {
            std::vector<uint8_t> b;
            if (!entryBytes(c, i, b)) continue;
            try {
                sr3rig::Rig r = sr3rig::Rig::parse(vpp::ByteView(b.data(), b.size()));
                g_rigsByBoneCount[r.bones().size()].push_back(r);
                ++g_rigsLoaded;
            } catch (const std::exception&) { ++g_rigsFailed; }
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { loadRigs(c.openNested(i)); } catch (...) {}
        }
    }
}

void doClip(const std::vector<uint8_t>& raw) {
    vpp::ByteView bv(raw.data(), raw.size());
    sr3anim::Animation a;
    try { a = sr3anim::Animation::parse(bv); } catch (...) { return; }
    ++g_clips;

    // Clean population only: the oracle-confirmed walk.
    if ((a.flags() & sr3anim::kExtraPayloadFlag) != 0) return;
    sr3anim::Payload pl = sr3anim::Payload::walk(bv, a);
    if (!pl.walkComplete() || !pl.landedOnDeclaredEnd()) return;

    const size_t boneCount = a.field_0x0B_rawCount();
    auto it = g_rigsByBoneCount.find(boneCount);
    if (it == g_rigsByBoneCount.end() || it->second.empty()) { ++g_clipsNoRig; return; }
    const sr3rig::Rig& rig = it->second.front();

    // Track -> bone. With a table (Sec6b) each track names its bone; without
    // one the mapping is the identity. Clips WITHOUT a table are used too -
    // excluding them would silently restrict the population.
    const bool hasTable = a.hasTrackBoneTable();
    const size_t tableAt = hasTable ? a.trackBoneTableOffset() : 0;
    if (hasTable && tableAt + pl.tracks().size() > bv.size()) return;
    if (!hasTable) ++g_skippedNoTable;

    ++g_clipsUsed;
    const std::vector<sr3rig::Bone>& bones = rig.bones();

    for (size_t t = 0; t < pl.tracks().size(); ++t) {
        size_t bone = t;
        if (hasTable) {
            const uint8_t v = bv.at(tableAt + t);
            if (v == 255) continue; // spec Sec6b: 255 means "unused"
            bone = v;
        }
        if (bone >= bones.size()) continue;

        const std::array<float, 3> rest = bones[bone].parentRelativeOffset();
        const double rx = rest[0], ry = rest[1], rz = rest[2];

        // The shuffled control: a DIFFERENT bone, deterministically chosen.
        const size_t other = (bone + bones.size() / 2) % bones.size();
        const std::array<float, 3> orest = bones[other].parentRelativeOffset();

        const std::vector<sr3anim::Vec3> tr = pl.translations(bv, t);
        if (tr.empty()) continue;
        // First key is enough: the claim is about bone LENGTH, which every
        // key shares. Using all keys would weight long tracks more heavily
        // without adding independent evidence.
        const sr3anim::Vec3& v = tr.front();
        ++g_compared;

        auto d3 = [](double ax, double ay, double az, double bx, double by, double bz) {
            const double dx = ax - bx, dy = ay - by, dz = az - bz;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        };
        if (g_real.size() < 400000) {
            g_real.push_back(d3(v.x, v.y, v.z, rx, ry, rz));
            g_shuffled.push_back(d3(v.x, v.y, v.z, orest[0], orest[1], orest[2]));
            // Scale control: base decoded at 1/32 instead of 1/64. Rebuilt
            // from the record rather than scaling the result, since only the
            // BASE term carries the scale under test.
            const auto& blk = pl.tracks()[t];
            const size_t at = blk.translationControlOffset;
            if (at + 8 <= bv.size()) {
                const double bx2 = static_cast<int16_t>(bv.readU16LE(at + 2)) / 32.0;
                const double by2 = static_cast<int16_t>(bv.readU16LE(at + 4)) / 32.0;
                const double bz2 = static_cast<int16_t>(bv.readU16LE(at + 6)) / 32.0;
                g_wrongScale.push_back(d3(bx2, by2, bz2, rx, ry, rz));
            }
        }
    }
}

void walkAnims(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (endsWith(c.entries()[i].name, ".anim_pc")) {
            std::vector<uint8_t> b;
            if (entryBytes(c, i, b)) doClip(b);
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { walkAnims(c.openNested(i)); } catch (...) {}
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("usage: validate_anim_rig_crossformat <anim archive> <rig archive>\n");
        return 1;
    }
    std::vector<uint8_t> rb = readFile(argv[2]);
    if (!rb.empty()) {
        try {
            vpp::Container c(vpp::ByteView(rb.data(), rb.size()));
            loadRigs(c);
        } catch (const std::exception& e) { printf("rig archive: %s\n", e.what()); }
    }
    printf("rigs loaded %d (failed %d), distinct bone counts %zu\n", g_rigsLoaded, g_rigsFailed,
           g_rigsByBoneCount.size());

    std::vector<uint8_t> ab = readFile(argv[1]);
    if (!ab.empty()) {
        try {
            vpp::Container c(vpp::ByteView(ab.data(), ab.size()));
            walkAnims(c);
        } catch (const std::exception& e) { printf("anim archive: %s\n", e.what()); }
    }

    printf("\n=== CROSS-FORMAT: anim translation vs rig rest offset ===\n");
    printf("clips seen %d, used %d, no matching rig %d\n", g_clips, g_clipsUsed, g_clipsNoRig);
    printf("clips with NO track->bone table (identity mapping used): %lld\n", g_skippedNoTable);
    printf("bone comparisons: %lld\n", g_compared);

    printf("\nAMBIGUITY of the rig pairing (clips name no rig, so bone count is\n");
    printf("the association - these are how many rigs share each count):\n");
    int shown = 0;
    for (const auto& kv : g_rigsByBoneCount) {
        if (kv.second.size() > 1 && shown++ < 6)
            printf("  %zu bones : %zu rigs\n", kv.first, kv.second.size());
    }
    if (shown == 0) printf("  every bone count maps to exactly one rig\n");

    printf("\n%-38s %9s %9s %9s %9s\n", "", "median", "p90", "<=0.05", "<=0.25");
    struct Row { const char* n; std::vector<double>* v; } rows[] = {
        {"REAL  track->bone mapping", &g_real},
        {"CTL   shuffled bone", &g_shuffled},
        {"CTL   base scale 1/32 not 1/64", &g_wrongScale},
    };
    for (auto& r : rows)
        printf("%-38s %9.4f %9.4f %8.1f%% %8.1f%%\n", r.n, median(*r.v), pct(*r.v, 0.90),
               fracWithin(*r.v, 0.05), fracWithin(*r.v, 0.25));

    const double re = median(g_real);
    printf("\nseparation from controls (higher = stronger):\n");
    printf("  vs shuffled bone : %.2fx\n", re > 0 ? median(g_shuffled) / re : 0.0);
    printf("  vs wrong scale   : %.2fx\n", re > 0 ? median(g_wrongScale) / re : 0.0);
    printf("\nIf the real reading does not clearly beat BOTH controls, this\n");
    printf("test has not confirmed anything and must not be quoted as if it had.\n");
    return 0;
}
