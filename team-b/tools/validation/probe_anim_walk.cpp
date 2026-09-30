// Which reading of the Sec6c.1 per-track walk actually reproduces the
// spec's own result?
//
// Implementing Sec6c.1 literally gave a complete walk on 105 / 4,209 clips
// against the published 97.1%, and a search over every payload start offset
// found one that lands on the +0x30 oracle for only 55 / 3,691 clips, with
// scattered deltas - i.e. noise, not a location. So the error is in the
// WALK, not in where it begins.
//
// Pseudocode in a spec is a compression of real code, and the places it
// compresses are predictable: whether a zero-length section is skipped
// entirely or still consumes its alignment, whether a "+1" belongs to a
// span, whether a trailing per-key byte exists. Rather than guess one at a
// time, enumerate that small space and score every variant against the
// oracle THE FILE DECLARES - the walk must land exactly on header +0x30.
//
// The oracle is what makes this legitimate rather than curve-fitting: it
// is stated by the file, not chosen by me, and a wrong variant has no way
// to hit it across thousands of clips. The ambiguity count is reported too
// - if several variants score high, the search has not identified one.
//
// Usage: probe_anim_walk <archive.vpp_pc>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "sr3anim/animation.h"
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

// ESTABLISHED, round 1: the spec's `align2(p+1)` and `align4(p+3)` are the
// C idioms `(p+1) & ~1` and `(p+3) & ~3` - i.e. ALIGN p UP, not "apply an
// align function to p+1". Reading them the second way applies the offset
// twice and was the original bug: it scored 105/4,209 where aligning up
// scores 3,277/3,691 on the same oracle. Now the baseline, not a variant.
size_t alignUp2(size_t p) { return (p + 1) & ~static_cast<size_t>(1); }
size_t alignUp4(size_t p) { return (p + 3) & ~static_cast<size_t>(3); }

// Variant bits - round 2, exploring what still costs the residual ~10%.
enum {
    V_SKIP_ROT_IF_ZERO = 1 << 0,   // rotKeys==0 consumes nothing at all
    V_SKIP_TRANS_IF_ZERO = 1 << 1, // transKeys==0 consumes nothing at all
    V_ROT_SAMPLES_X4 = 1 << 2,     // the "second runtime mode": 4 bytes/key
    V_TRANS_SAMPLES_X3 = 1 << 3,   // no trailing per-key byte
    // Round 2 showed bits 0,1,5 to be exact no-ops on shipped data (12
    // variants tied at 90.06%), so the residual lies outside that space.
    // The one structural thing not yet varied: 394 clips carry a
    // track->bone table between the header and the payload (Sec6b,
    // HANDOFF 9.31), one byte per track. If the payload starts after it,
    // starting at the header size is wrong for exactly those clips.
    V_START_AFTER_TABLE = 1 << 4,
    V_TRACK_END_ALIGN = 1 << 5,    // align2 at the end of each track block
    V_TRANS_SPAN_PLUS1 = 1 << 6,   // acc += 1 + span, as rotation does
    V_COUNT = 1 << 7,
};

size_t walkVariant(const std::vector<uint8_t>& b, size_t p, uint8_t flags, size_t trackCount,
                   unsigned v) {
    const bool wideCounts = (flags & 0x80) != 0;
    const bool wideTrans = (flags & 0x20) != 0;
    const size_t size = b.size();
    auto need = [&](size_t at, size_t n) { return at + n <= size; };
    auto u16 = [&](size_t at) {
        return static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8);
    };
    auto i32 = [&](size_t at) {
        uint32_t x;
        std::memcpy(&x, b.data() + at, 4);
        return static_cast<int32_t>(x);
    };

    for (size_t t = 0; t < trackCount; ++t) {
        uint32_t rotKeys, transKeys;
        if (!wideCounts) {
            if (!need(p, 2)) return 0;
            rotKeys = b[p];
            transKeys = b[p + 1];
            p += 2;
        } else {
            p = alignUp2(p);
            if (!need(p, 4)) return 0;
            rotKeys = u16(p);
            transKeys = u16(p + 2);
            p += 4;
        }

        if (rotKeys != 0 || !(v & V_SKIP_ROT_IF_ZERO)) {
            size_t n = 0;
            uint64_t acc = 0;
            while (acc < rotKeys) {
                if (!need(p + n * 4, 4)) return 0;
                // The rotation span's "+1" is kept: round 1 scored the
                // without-+1 reading at 1.8% against 88.8%, so it is
                // settled and no longer worth a variant bit.
                acc += 1u + (b[p + n * 4 + 3] & 0x3Fu);
                ++n;
                if (n > 200000) return 0;
            }
            p += n * 4;

            const size_t per = (v & V_ROT_SAMPLES_X4) ? 4 : 3;
            if (!need(p, static_cast<size_t>(rotKeys) * per)) return 0;
            p += static_cast<size_t>(rotKeys) * per;
        }

        if (transKeys != 0 || !(v & V_SKIP_TRANS_IF_ZERO)) {
            size_t m = 0;
            uint64_t acc = 0;
            if (!wideTrans) {
                p = alignUp2(p);
                while (acc < transKeys) {
                    if (!need(p + m * 8, 8)) return 0;
                    const uint32_t span = u16(p + m * 8);
                    const uint64_t add = (v & V_TRANS_SPAN_PLUS1) ? (1u + span) : span;
                    if (add == 0) return 0;
                    acc += add;
                    ++m;
                    if (m > 200000) return 0;
                }
                p += m * 8;
            } else {
                p = alignUp4(p);
                while (acc < transKeys) {
                    if (!need(p + m * 16, 16)) return 0;
                    const int32_t span = i32(p + m * 16);
                    const int64_t add = (v & V_TRANS_SPAN_PLUS1) ? (1 + span) : span;
                    if (add <= 0) return 0;
                    acc += static_cast<uint64_t>(add);
                    ++m;
                    if (m > 200000) return 0;
                }
                p += m * 16;
            }
            const size_t per2 = (v & V_TRANS_SAMPLES_X3) ? 3 : 4;
            if (!need(p, static_cast<size_t>(transKeys) * per2)) return 0;
            p += static_cast<size_t>(transKeys) * per2;
        }
        if (v & V_TRACK_END_ALIGN) p = alignUp2(p);
        {
        }
    }
    return p;
}

int g_clean = 0;
int g_walked[V_COUNT] = {0};
int g_landed[V_COUNT] = {0};
// flags byte -> {clips, landed under the best-known variant}
std::map<uint8_t, std::pair<int, int>> g_byFlags;
constexpr unsigned kBestVariant = V_START_AFTER_TABLE;

void doClip(const std::vector<uint8_t>& raw) {
    vpp::ByteView bv(raw.data(), raw.size());
    sr3anim::Animation a;
    try { a = sr3anim::Animation::parse(bv); } catch (...) { return; }
    if ((a.flags() & 0x40) != 0) return;      // Sec6c.4's other population
    if (!a.hasTrailingOffset()) return;       // no oracle to score against
    ++g_clean;

    const uint8_t f = a.flags();
    const size_t tc = a.field_0x0A_rawCount();
    const size_t headerSize = (f & 0x10) ? 0x48 : 0x38;
    const size_t tbl = a.hasTrackBoneTable() ? a.trackBoneTableOffset() : 0;

    for (unsigned v = 0; v < V_COUNT; ++v) {
        const size_t start = ((v & V_START_AFTER_TABLE) && tbl) ? tbl + tc : headerSize;
        const size_t end = walkVariant(raw, start, f, tc, v);
        if (end != 0) {
            ++g_walked[v];
            if (end == a.trailingOffset()) ++g_landed[v];
        }
    }

    auto& slot = g_byFlags[f];
    ++slot.first;
    const size_t bestStart = (kBestVariant & V_START_AFTER_TABLE) && tbl ? tbl + tc : headerSize;
    if (walkVariant(raw, bestStart, f, tc, kBestVariant) == a.trailingOffset()) ++slot.second;
}

void walkArchive(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (endsWith(c.entries()[i].name, ".anim_pc")) {
            std::vector<uint8_t> b;
            if (entryBytes(c, i, b)) doClip(b);
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { walkArchive(c.openNested(i)); } catch (...) {}
        }
    }
}

const char* bitName(int i) {
    switch (i) {
        case 0: return "skipRotIfZero";
        case 1: return "skipTransIfZero";
        case 2: return "rotSamples*4";
        case 3: return "transSamples*3";
        case 4: return "startAfterTrackTable";
        case 5: return "trackEndAlign";
        case 6: return "transSpan+1";
    }
    return "?";
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> b = readFile(argv[i]);
        if (b.empty()) continue;
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            walkArchive(c);
        } catch (const std::exception& e) { printf("%s: %s\n", argv[i], e.what()); }
        printf("scanned %s\n", argv[i]);
    }

    printf("\nclean population (no flags 0x40, has +0x30): %d\n", g_clean);
    printf("\nTop variants by landing on the file's own declared end:\n");
    printf("%-46s %9s %9s %8s\n", "variant", "walked", "landed", "land%");

    std::vector<unsigned> order;
    for (unsigned v = 0; v < V_COUNT; ++v) order.push_back(v);
    std::sort(order.begin(), order.end(),
              [](unsigned a, unsigned b) { return g_landed[a] > g_landed[b]; });

    for (int k = 0; k < 10; ++k) {
        const unsigned v = order[static_cast<size_t>(k)];
        std::string name;
        for (int i = 0; i < 7; ++i)
            if (v & (1u << i)) { if (!name.empty()) name += "+"; name += bitName(i); }
        if (name.empty()) name = "(spec as written)";
        printf("%-46s %9d %9d %7.2f%%\n", name.c_str(), g_walked[v], g_landed[v],
               g_clean ? 100.0 * g_landed[v] / g_clean : 0.0);
    }

    // AMBIGUITY: how many variants clear a high bar? One is a location;
    // several means this search has not identified anything.
    int over50 = 0, over90 = 0;
    for (unsigned v = 0; v < V_COUNT; ++v) {
        const double r = g_clean ? 100.0 * g_landed[v] / g_clean : 0.0;
        if (r > 50.0) ++over50;
        if (r > 90.0) ++over90;
    }
    printf("\nvariants scoring >50%%: %d    >90%%: %d   (of %d tried)\n", over50, over90,
           V_COUNT);

    // The winner lands on 88.78%, not the spec's 99.89%, so something is
    // still wrong for a minority. Stratify the residual by flags byte -
    // measuring the property on the SUCCESSES as well as the failures,
    // since a trait shared by every failure means nothing until the
    // passing set is shown to lack it (HANDOFF Sec3).
    printf("\nBEST VARIANT, landing stratified by flags byte:\n");
    printf("%-8s %9s %9s %8s\n", "flags", "clips", "landed", "land%");
    for (const auto& kv : g_byFlags) {
        const int clips = kv.second.first, landed = kv.second.second;
        printf("0x%02X     %9d %9d %7.2f%%\n", kv.first, clips, landed,
               clips ? 100.0 * landed / clips : 0.0);
    }
    return 0;
}
