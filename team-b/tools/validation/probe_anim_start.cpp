// Where does the .anim_pc keyframe payload actually start?
//
// The first implementation of the Sec6c.1 walk completed on 105 / 4,209
// clips against the spec's published 97.1%, so something upstream of the
// per-track loop is wrong. Rather than guess, score candidate payload
// start offsets against the oracle THE FILE DECLARES - the walk must land
// exactly on the offset at header +0x30.
//
// That oracle is what makes this a measurement rather than a fishing
// expedition: a wrong start cannot land on it except by coincidence, and
// the coincidence rate is itself visible because every candidate is scored
// on the same population.
//
// Usage: probe_anim_start <archive.vpp_pc>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <map>

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

size_t align2(size_t p) { return (p + 1) & ~static_cast<size_t>(1); }
size_t align4(size_t p) { return (p + 3) & ~static_cast<size_t>(3); }

// Returns the end cursor, or 0 if the walk could not complete.
size_t walkFrom(const std::vector<uint8_t>& b, size_t p, uint8_t flags, size_t trackCount) {
    const bool wideCounts = (flags & 0x80) != 0;
    const bool wideTrans = (flags & 0x20) != 0;
    const size_t size = b.size();
    auto need = [&](size_t at, size_t n) { return at + n <= size; };
    auto u16 = [&](size_t at) {
        return static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8);
    };
    auto i32 = [&](size_t at) {
        uint32_t v;
        std::memcpy(&v, b.data() + at, 4);
        return static_cast<int32_t>(v);
    };

    for (size_t t = 0; t < trackCount; ++t) {
        uint32_t rotKeys, transKeys;
        if (!wideCounts) {
            if (!need(p, 2)) return 0;
            rotKeys = b[p];
            transKeys = b[p + 1];
            p += 2;
        } else {
            p = align2(p + 1);
            if (!need(p, 4)) return 0;
            rotKeys = u16(p);
            transKeys = u16(p + 2);
            p += 4;
        }

        size_t n = 0;
        uint64_t acc = 0;
        while (acc < rotKeys) {
            if (!need(p + n * 4, 4)) return 0;
            acc += 1u + (b[p + n * 4 + 3] & 0x3Fu);
            ++n;
            if (n > 100000) return 0;
        }
        p += n * 4;
        if (!need(p, static_cast<size_t>(rotKeys) * 3)) return 0;
        p += static_cast<size_t>(rotKeys) * 3;

        size_t m = 0;
        acc = 0;
        if (!wideTrans) {
            p = align2(p + 1);
            while (acc < transKeys) {
                if (!need(p + m * 8, 8)) return 0;
                const uint32_t span = u16(p + m * 8);
                if (span == 0) return 0; // would never advance
                acc += span;
                ++m;
                if (m > 100000) return 0;
            }
            p += m * 8;
        } else {
            p = align4(p + 3);
            while (acc < transKeys) {
                if (!need(p + m * 16, 16)) return 0;
                const int32_t span = i32(p + m * 16);
                if (span <= 0) return 0;
                acc += static_cast<uint64_t>(span);
                ++m;
                if (m > 100000) return 0;
            }
            p += m * 16;
        }
        if (!need(p, static_cast<size_t>(transKeys) * 4)) return 0;
        p += static_cast<size_t>(transKeys) * 4;
    }
    return p;
}

struct Cand {
    const char* name;
    int walked = 0, landed = 0;
};

int g_clips = 0, g_clean = 0;
int g_searchFound = 0, g_searchUnique = 0;
long long g_hitsTotal = 0;
std::map<size_t, int> g_startHist;
std::vector<std::string> g_examples;
Cand g_c[8] = {
    {"0x38 flat"}, {"0x48 flat"}, {"header size by 0x10 flag"},
    {"+0x28 value (when set)"}, {"+0x28 value + trackCount"},
    {"headerSize + trackCount"}, {"0x40 flat"}, {"0x40 + trackCount"},
};

void doClip(const std::vector<uint8_t>& raw) {
    vpp::ByteView bv(raw.data(), raw.size());
    sr3anim::Animation a;
    try { a = sr3anim::Animation::parse(bv); } catch (...) { return; }
    ++g_clips;
    // Score only on the clean population: clips without flags 0x40, which
    // Sec6c.4 shows are the ones whose walk is supposed to land on +0x30.
    if ((a.flags() & 0x40) != 0) return;
    if (!a.hasTrailingOffset()) return;
    ++g_clean;

    const uint8_t f = a.flags();
    const size_t tc = a.field_0x0A_rawCount();
    const size_t headerSize = (f & 0x10) ? 0x48 : 0x38;
    const size_t tbl = a.hasTrackBoneTable() ? a.trackBoneTableOffset() : 0;

    const size_t starts[8] = {
        0x38, 0x48, headerSize,
        tbl ? tbl : headerSize,
        tbl ? tbl + tc : headerSize,
        headerSize + tc,
        0x40, 0x40 + tc,
    };
    for (int i = 0; i < 8; ++i) {
        const size_t end = walkFrom(raw, starts[i], f, tc);
        if (end != 0) {
            ++g_c[i].walked;
            if (end == a.trailingOffset()) ++g_c[i].landed;
        }
    }

    // SEARCH, since every fixed candidate failed: which start offsets, if
    // any, produce a walk landing exactly on the declared end? Report the
    // AMBIGUITY too - how many distinct starts work per clip - because a
    // search that finds one answer and a search that finds nine are
    // different results and only one of them is a location.
    int hits = 0;
    size_t firstHit = 0;
    const size_t limit = raw.size() < 0x400 ? raw.size() : 0x400;
    for (size_t s = 0x20; s < limit; ++s) {
        if (walkFrom(raw, s, f, tc) == a.trailingOffset()) {
            if (hits == 0) firstHit = s;
            ++hits;
        }
    }
    if (hits > 0) {
        ++g_searchFound;
        g_hitsTotal += hits;
        if (hits == 1) ++g_searchUnique;
        g_startHist[firstHit - (firstHit > headerSize ? headerSize : 0)] += 1;
        if (g_examples.size() < 12) {
            char buf[160];
            snprintf(buf, sizeof buf,
                     "start=0x%02zX headerSize=0x%02zX delta=%+d tracks=%zu flags=0x%02X hits=%d",
                     firstHit, headerSize, static_cast<int>(firstHit) - static_cast<int>(headerSize),
                     tc, f, hits);
            g_examples.push_back(buf);
        }
    }
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
    printf("\nclips %d, clean population (no 0x40, has +0x30) %d\n\n", g_clips, g_clean);
    printf("%-30s %10s %10s %9s\n", "candidate payload start", "walked", "landed", "land%");
    for (int i = 0; i < 8; ++i) {
        printf("%-30s %10d %10d %8.2f%%\n", g_c[i].name, g_c[i].walked, g_c[i].landed,
               g_clean ? 100.0 * g_c[i].landed / g_clean : 0.0);
    }
    printf("\n=== SEARCH: starts that land on the declared end ===\n");
    printf("clips where SOME start works : %d / %d\n", g_searchFound, g_clean);
    printf("  of those, exactly ONE start : %d\n", g_searchUnique);
    printf("  mean working starts per clip: %.2f\n",
           g_searchFound ? static_cast<double>(g_hitsTotal) / g_searchFound : 0.0);
    printf("\nfirst working start, relative to header size:\n");
    for (const auto& kv : g_startHist)
        if (kv.second > 5) printf("  %+5d : %d clips\n", static_cast<int>(kv.first), kv.second);
    printf("\nexamples:\n");
    for (const auto& s : g_examples) printf("  %s\n", s.c_str());
    return 0;
}
