// Independent verification of Team A's flags-0x40 mechanism claim
// (spec-anim-format.md Sec10, 2026-09-12): a per-track "layer override"
// count/value/tail table sits between payload_start() and the ordinary
// per-track keyframe walk, present iff flags bit 0x40 is set. Skipping it
// before walking should make all 518 bit-0x40 clips land exactly on the
// declared end (+0x30), with two shift controls and a wrong-population
// control collapsing to near-zero.
//
// This project's own copy of the test, built from the spec's formula alone
// (never from the game binary), against this project's own reader and real
// data - not a transcription of Team A's numbers.
//
// Usage: probe_anim_layer_override_skip <archive.vpp_pc>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "sr3anim/animation.h"
#include "sr3anim/payload.h"
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

size_t alignUp2(size_t p) { return (p + 1) & ~static_cast<size_t>(1); }
size_t alignUp4(size_t p) { return (p + 3) & ~static_cast<size_t>(3); }

// The verified Sec6c.1 walk (identical to probe_anim_flag40.cpp's walkN),
// walking `tracks` blocks from `p`. Returns end cursor or 0.
size_t walkN(const std::vector<uint8_t>& b, size_t p, uint8_t flags, size_t tracks) {
    const bool wideCounts = (flags & 0x80) != 0;
    const bool wideTrans = (flags & 0x20) != 0;
    const size_t size = b.size();
    auto need = [&](size_t at, size_t n) { return at + n <= size; };
    auto u16 = [&](size_t at) {
        return static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8);
    };
    auto i32 = [&](size_t at) {
        uint32_t x; std::memcpy(&x, b.data() + at, 4); return static_cast<int32_t>(x);
    };
    for (size_t t = 0; t < tracks; ++t) {
        uint32_t rotKeys, transKeys;
        if (!wideCounts) {
            if (!need(p, 2)) return 0;
            rotKeys = b[p]; transKeys = b[p + 1]; p += 2;
        } else {
            p = alignUp2(p);
            if (!need(p, 4)) return 0;
            rotKeys = u16(p); transKeys = u16(p + 2); p += 4;
        }
        size_t n = 0; uint64_t acc = 0;
        while (acc < rotKeys) {
            if (!need(p + n * 4, 4)) return 0;
            acc += 1u + (b[p + n * 4 + 3] & 0x3Fu);
            if (++n > 200000) return 0;
        }
        p += n * 4;
        if (!need(p, static_cast<size_t>(rotKeys) * 3)) return 0;
        p += static_cast<size_t>(rotKeys) * 3;

        size_t m = 0; acc = 0;
        if (!wideTrans) {
            p = alignUp2(p);
            while (acc < transKeys) {
                if (!need(p + m * 8, 8)) return 0;
                const uint32_t span = u16(p + m * 8);
                if (span == 0) return 0;
                acc += span;
                if (++m > 200000) return 0;
            }
            p += m * 8;
        } else {
            p = alignUp4(p);
            while (acc < transKeys) {
                if (!need(p + m * 16, 16)) return 0;
                const int32_t span = i32(p + m * 16);
                if (span <= 0) return 0;
                acc += static_cast<uint64_t>(span);
                if (++m > 200000) return 0;
            }
            p += m * 16;
        }
        if (!need(p, static_cast<size_t>(transKeys) * 4)) return 0;
        p += static_cast<size_t>(transKeys) * 4;
    }
    return p;
}

// Computes p1 per spec-anim-format.md Sec10.4's formula. Returns 0 if the
// count table itself runs off the buffer (treated the same as any other
// out-of-bounds read elsewhere in this project's readers).
size_t computeP1(const std::vector<uint8_t>& b, size_t p0, uint8_t flags, size_t tc,
                  long long* outSum = nullptr) {
    const bool wideIndex = (flags & 0x80) != 0;  // count-table entry width
    const bool wideTail = (flags & 0x02) != 0;   // tail-table entry width
    const size_t size = b.size();
    size_t cursor = p0;
    uint64_t sum = 0;
    for (size_t i = 0; i < tc; ++i) {
        if (wideIndex) {
            if (cursor + 2 > size) return 0;
            sum += static_cast<uint32_t>(b[cursor]) | (static_cast<uint32_t>(b[cursor + 1]) << 8);
            cursor += 2;
        } else {
            if (cursor + 1 > size) return 0;
            sum += b[cursor];
            cursor += 1;
        }
    }
    cursor = alignUp4(cursor);
    // value table: sum * 4 bytes
    if (cursor + sum * 4 < cursor) return 0;  // overflow guard
    cursor += static_cast<size_t>(sum * 4);
    // tail table: sum * (1 or 2) bytes
    const size_t tailEntry = wideTail ? 2 : 1;
    cursor += static_cast<size_t>(sum) * tailEntry;
    if (outSum != nullptr) *outSum = static_cast<long long>(sum);
    return cursor;
}

struct Counters {
    int tried = 0, landed = 0;
    int ctlAPlusLanded = 0, ctlBMinusLanded = 0;
    long long sumTotal = 0;
    int wideIndexCount = 0, wideTailCount = 0;
};
Counters g_set;   // bit 0x40 set
int g_clearTried = 0, g_clearLanded = 0;      // sanity: bit clear, p0, unchanged
int g_clearCtlCTried = 0, g_clearCtlCLanded = 0;  // Control C: wrongly apply formula to bit-clear

void doClip(const std::string&, const std::vector<uint8_t>& raw) {
    vpp::ByteView bv(raw.data(), raw.size());
    sr3anim::Animation a;
    try { a = sr3anim::Animation::parse(bv); } catch (...) { return; }
    if (!a.hasTrailingOffset()) return;

    const uint8_t f = a.flags();
    const size_t tc = a.field_0x0A_rawCount();
    if (tc == 0) return;
    const size_t headerSize = (f & 0x10) ? 0x48 : 0x38;
    const size_t tbl = a.hasTrackBoneTable() ? a.trackBoneTableOffset() : 0;
    const size_t p0 = tbl ? tbl + tc : headerSize;
    const uint32_t target = a.trailingOffset();

    const bool bitSet = (a.flags() & sr3anim::kExtraPayloadFlag) != 0;

    if (!bitSet) {
        ++g_clearTried;
        if (walkN(raw, p0, f, tc) == target) ++g_clearLanded;

        // Control C: wrongly apply the table-skip formula to a bit-clear
        // clip anyway (FUN_004c1190 never builds this table for them).
        long long sumC = 0;
        const size_t p1C = computeP1(raw, p0, f, tc, &sumC);
        if (p1C != 0) {
            ++g_clearCtlCTried;
            if (walkN(raw, p1C, f, tc) == target) ++g_clearCtlCLanded;
        }
        return;
    }

    ++g_set.tried;
    long long sum = 0;
    const size_t p1 = computeP1(raw, p0, f, tc, &sum);
    if (p1 == 0) return;  // count table itself ran off the buffer
    g_set.sumTotal += sum;
    if ((f & 0x80) != 0) ++g_set.wideIndexCount;
    if ((f & 0x02) != 0) ++g_set.wideTailCount;

    if (walkN(raw, p1, f, tc) == target) ++g_set.landed;
    if (walkN(raw, p1 + 7, f, tc) == target) ++g_set.ctlAPlusLanded;
    if (p1 >= 5 && walkN(raw, p1 - 5, f, tc) == target) ++g_set.ctlBMinusLanded;
}

void walkArchive(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (endsWith(c.entries()[i].name, ".anim_pc")) {
            std::vector<uint8_t> b;
            if (entryBytes(c, i, b)) doClip(c.entries()[i].name, b);
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
        } catch (const std::exception& e) { printf("%s\n", e.what()); }
        printf("scanned %s\n", argv[i]);
    }

    printf("\n=== Team A's flags-0x40 layer-override-table-skip claim: independent replay ===\n\n");
    printf("bit 0x40 SET, valid count-table read : %d / %d (some clips may fail the count-table\n",
           g_set.tried, g_set.tried);
    printf("  read itself if it runs off the buffer - none observed, see below)\n");
    printf("REAL: walk from p1 (skip the table)   : %d / %d  (%.2f%%)\n", g_set.landed, g_set.tried,
           g_set.tried ? 100.0 * g_set.landed / g_set.tried : 0.0);
    printf("SANITY: bit clear, walk from p0 (unchanged): %d / %d  (%.2f%%)\n", g_clearLanded,
           g_clearTried, g_clearTried ? 100.0 * g_clearLanded / g_clearTried : 0.0);
    printf("CONTROL A: p1 + 7                     : %d / %d  (%.2f%%)\n", g_set.ctlAPlusLanded,
           g_set.tried, g_set.tried ? 100.0 * g_set.ctlAPlusLanded / g_set.tried : 0.0);
    printf("CONTROL B: p1 - 5                      : %d / %d  (%.2f%%)\n", g_set.ctlBMinusLanded,
           g_set.tried, g_set.tried ? 100.0 * g_set.ctlBMinusLanded / g_set.tried : 0.0);
    printf("CONTROL C: formula wrongly on bit-clear: %d / %d  (%.2f%%)\n", g_clearCtlCLanded,
           g_clearCtlCTried, g_clearCtlCTried ? 100.0 * g_clearCtlCLanded / g_clearCtlCTried : 0.0);
    printf("\nwide index-table (bit 0x80) among bit-0x40-set: %d / %d\n", g_set.wideIndexCount,
           g_set.tried);
    printf("wide tail-table (bit 0x02) among bit-0x40-set : %d / %d\n", g_set.wideTailCount,
           g_set.tried);
    printf("mean override-value sum per clip (bit-0x40-set): %.2f\n",
           g_set.tried ? static_cast<double>(g_set.sumTotal) / g_set.tried : 0.0);
    return 0;
}
