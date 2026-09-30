// Is `.anim_pc` header +0x28 zero everywhere, or an offset to a
// track->bone table?
//
// SETTLED - this harness is kept as the evidence, not as an open question.
//
// `src/animation.cpp` USED TO state "+0x28 (8 bytes) is observed zero
// everywhere and OPEN - not read". Team A reported instead that it is a
// file-relative offset to a track->bone index table, non-zero in 394 of
// 4,209 clips, always equal to the exact header size (0x38 or 0x48
// depending on a flag bit) so the table sits immediately after the header.
//
// Both could not be true, and it was a claim about a field THIS reader
// already documents - so it got measured, and TEAM A WAS RIGHT: 394/4,209
// non-zero, only the two header sizes, and the bytes there pass a
// permutation test 355/394 against a control at 35/394. `src/animation.cpp`
// and `include/sr3anim/animation.h` were corrected accordingly (HANDOFF
// Sec9.31); the reader now surfaces trackBoneTableOffset().
//
// Re-run it to reproduce the numbers, not to decide the question. Reads
// header bytes only; does not touch the keyframe payload, which stays
// parked.
//
// Checks, with a control on the last one:
//   * how often is +0x28 non-zero;
//   * when non-zero, does it equal 0x38 or 0x48 (their "always the header
//     size" claim);
//   * do the bytes at that offset look like a track->bone table - every
//     non-255 entry distinct and in range for a plausible bone count -
//     against a CONTROL reading the same number of bytes from an
//     unrelated offset.
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

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
    bool ok = r.status == vpp::DecodeStatus::Ok ||
              r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
              r.status == vpp::DecodeStatus::ContentValidated ||
              r.status == vpp::DecodeStatus::RecoveredSharedStream ||
              r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!ok) return false;
    out = std::move(r.data);
    return true;
}
static uint32_t u32at(const std::vector<uint8_t>& b, size_t o) {
    if (o + 4 > b.size()) return 0;
    return static_cast<uint32_t>(b[o] | (b[o+1] << 8) | (b[o+2] << 16) |
                                 (static_cast<uint32_t>(b[o+3]) << 24));
}

int g_files = 0, g_tooSmall = 0;
int g_zero = 0, g_nonZero = 0;
std::map<uint32_t, int> g_values;
int g_tableLooksValid = 0, g_controlLooksValid = 0, g_tableTested = 0;
std::vector<std::string> g_samples;

// "Looks like a track->bone table": every non-255 byte distinct, and all
// below a generous bone ceiling. The control applies the identical test to
// the same number of bytes taken from an unrelated part of the file, so a
// test that any byte run would pass is visible as such.
bool looksLikeTable(const std::vector<uint8_t>& b, size_t at, size_t count) {
    if (at + count > b.size() || count == 0) return false;
    std::set<uint8_t> seen;
    for (size_t i = 0; i < count; ++i) {
        uint8_t v = b[at + i];
        if (v == 255) continue;
        if (v > 120) return false;          // beyond any shipped bone count
        if (!seen.insert(v).second) return false; // repeated -> not a permutation
    }
    return !seen.empty();
}

void examine(const std::string& name, const std::vector<uint8_t>& b) {
    if (b.size() < 0x30) { ++g_tooSmall; return; }
    ++g_files;
    const uint32_t v = u32at(b, 0x28);
    if (v == 0) { ++g_zero; return; }
    ++g_nonZero;
    if (g_values.size() < 16) g_values[v] += 1; else if (g_values.count(v)) g_values[v] += 1;

    // Their claim: the value equals the header size, so the table follows
    // immediately. Test the bytes there against a control elsewhere.
    const size_t count = 24; // a plausible track count; enough to be decisive
    if (v + count <= b.size()) {
        ++g_tableTested;
        if (looksLikeTable(b, v, count)) ++g_tableLooksValid;
        // Control: same-sized run from a different, unrelated offset.
        const size_t controlAt = b.size() > (v + 4096 + count) ? v + 4096 : b.size() - count;
        if (looksLikeTable(b, controlAt, count)) ++g_controlLooksValid;
    }
    if (g_samples.size() < 8) {
        char buf[256];
        int n = std::snprintf(buf, sizeof(buf), "%s: +0x28 = 0x%X, bytes there:", name.c_str(), v);
        for (size_t i = 0; i < 14 && v + i < b.size() && n > 0 && n < 200; ++i)
            n += std::snprintf(buf + n, sizeof(buf) - static_cast<size_t>(n), " %u",
                               b[v + i]);
        g_samples.push_back(buf);
    }
}

void walk(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (endsWith(c.entries()[i].name, ".anim_pc")) {
            std::vector<uint8_t> b;
            if (entryBytes(c, i, b) && !b.empty()) examine(c.entries()[i].name, b);
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { walk(c.openNested(i)); } catch (const std::exception&) {}
        }
    }
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> b = readFile(argv[i]);
        if (b.empty()) continue;
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            walk(c);
            printf("scanned %s (%d files)\n", argv[i], g_files);
            fflush(stdout);
        } catch (const std::exception&) {}
    }
    printf("\n=== .anim_pc header +0x28 ===\n");
    printf("files examined : %d   (too small: %d)\n", g_files, g_tooSmall);
    printf("+0x28 == 0     : %d\n", g_zero);
    printf("+0x28 != 0     : %d    <- the reader once claimed 'zero everywhere'; this is why it no longer does\n",
           g_nonZero);
    printf("distinct non-zero values seen:");
    for (const auto& kv : g_values) printf(" 0x%X(x%d)", kv.first, kv.second);
    printf("\n");
    printf("\nbytes at that offset look like a track->bone table:\n");
    printf("  real offset : %d / %d\n", g_tableLooksValid, g_tableTested);
    printf("  CONTROL     : %d / %d\n", g_controlLooksValid, g_tableTested);
    printf("\nsamples:\n");
    for (const auto& s : g_samples) printf("  %s\n", s.c_str());
    return 0;
}
