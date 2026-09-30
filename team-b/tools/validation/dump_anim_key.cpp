// Dump one rotation key's raw bytes and every intermediate value this
// project's decoder computes from them.
//
// Purpose: localize a decoder divergence in ONE exchange instead of trading
// designs. Team A's harness and this one produce different achievable
// ranges from the same spec section (0.884 vs 0.181 per component before
// the bit-allocation fix). Rather than compare implementations in prose,
// both sides run the SAME key and report the first value that differs -
// the same bracketing method that settled the vehicle 2x run-count gap,
// where the answer turned out to be a threshold rather than the structural
// difference both sides had reached for.
//
// Their prediction, stated in advance and therefore falsifiable: the two
// decoders will agree on base_j and diverge at mult_j or delta_j.
//
// Usage: dump_anim_key <archive.vpp_pc> [clipName] [trackIndex] [keyIndex]
#include <cmath>
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

int signExtend6(unsigned v) {
    v &= 0x3Fu;
    return static_cast<int>(v) - ((v & 0x20u) ? 64 : 0);
}
const float kStepSet[4] = {1.0f, 2.0f, 4.0f, 16.0f};
const double kScale = 8.6327287135645750e-05;

std::string g_want;
size_t g_track = 0, g_key = 0;
bool g_done = false;

void dumpClip(const std::string& name, const std::vector<uint8_t>& raw) {
    if (g_done) return;
    if (!g_want.empty() && name != g_want) return;

    vpp::ByteView bv(raw.data(), raw.size());
    sr3anim::Animation a;
    try { a = sr3anim::Animation::parse(bv); } catch (...) { return; }
    if ((a.flags() & sr3anim::kExtraPayloadFlag) != 0) return;

    sr3anim::Payload pl = sr3anim::Payload::walk(bv, a);
    if (!pl.walkComplete() || !pl.landedOnDeclaredEnd()) return;
    if (g_track >= pl.tracks().size()) return;
    const auto& blk = pl.tracks()[g_track];
    if (blk.rotationKeys == 0 || g_key >= blk.rotationKeys) return;

    // Find which control record covers this key, exactly as the decoder does.
    size_t key = 0, rec = 0;
    uint32_t span = 0;
    for (rec = 0; rec < blk.rotationControlCount; ++rec) {
        const size_t at = blk.rotationControlOffset + rec * 4;
        span = 1u + (bv.at(at + 3) & 0x3Fu);
        if (g_key < key + span) break;
        key += span;
    }
    if (rec >= blk.rotationControlCount) return;

    const size_t cat = blk.rotationControlOffset + rec * 4;
    const uint8_t b0 = bv.at(cat + 0), b1 = bv.at(cat + 1);
    const uint8_t b2 = bv.at(cat + 2), b3 = bv.at(cat + 3);
    const size_t sat = blk.rotationSampleOffset + g_key * 3;
    const uint8_t s0 = bv.at(sat + 0), s1 = bv.at(sat + 1), s2 = bv.at(sat + 2);

    printf("=== TEAM B rotation key dump ===\n");
    printf("clip        : %s\n", name.c_str());
    printf("track       : %zu   key: %zu   (of %u rotation keys)\n", g_track, g_key,
           blk.rotationKeys);
    printf("flags       : 0x%02X\n", a.flags());
    printf("control rec : index %zu, file offset 0x%zX, span %u\n", rec, cat, span);
    printf("sample      : file offset 0x%zX\n", sat);
    printf("\nRAW BYTES\n");
    printf("  control [4] : %02X %02X %02X %02X\n", b0, b1, b2, b3);
    printf("  sample  [3] : %02X %02X %02X\n", s0, s1, s2);

    const int stepIdx[3] = {(b0 >> 6) & 3, (b1 >> 6) & 3, (b2 >> 6) & 3};
    const int base[3] = {signExtend6(b0 & 0x3Fu), signExtend6(b1 & 0x3Fu),
                         signExtend6(b2 & 0x3Fu)};
    const float mult[3] = {kStepSet[stepIdx[0]], kStepSet[stepIdx[1]], kStepSet[stepIdx[2]]};
    const int delta[3] = {signExtend6(s0 >> 2),
                          signExtend6(((s0 & 0x03u) << 4) | (s1 >> 4)),
                          signExtend6(((s1 & 0x0Fu) << 2) | (s2 >> 6))};

    printf("\nINTERMEDIATES (this project's reading)\n");
    printf("  %-12s %8s %8s %8s\n", "", "axis 0", "axis 1", "axis 2");
    printf("  %-12s %8d %8d %8d   (control byte >> 6)\n", "stepIdx_j", stepIdx[0], stepIdx[1],
           stepIdx[2]);
    printf("  %-12s %8.0f %8.0f %8.0f   (table {1,2,4,16})\n", "mult_j", mult[0], mult[1],
           mult[2]);
    printf("  %-12s %8d %8d %8d   (signed 6-bit, control byte & 0x3F)\n", "base_j", base[0],
           base[1], base[2]);
    printf("  %-12s %8d %8d %8d   (signed 6-bit, packed across s0..s2)\n", "delta_j", delta[0],
           delta[1], delta[2]);

    printf("\n  combined 6-bit step index = %d*16 + %d*4 + %d = %d\n", stepIdx[0], stepIdx[1],
           stepIdx[2], stepIdx[0] * 16 + stepIdx[1] * 4 + stepIdx[2]);

    printf("\nCOMPONENT ASSEMBLY - THIS IS WHERE WE EXPECT TO DIVERGE.\n");
    printf("The corrected spec states the achievable extreme as\n");
    printf("  (16*32 + 32*64) * 4 * SCALE = 0.884\n");
    printf("but does not write the assembly expression itself, so the\n");
    printf("factors 64 and 4 cannot be placed with certainty from the text.\n");
    printf("Candidates this project can form from the stated fields:\n\n");
    for (int j = 0; j < 3; ++j) {
        const double A = (mult[j] * base[j] + delta[j]) * kScale;
        const double B = (base[j] + mult[j] * delta[j]) * kScale;
        const double C = (mult[j] * base[j] + delta[j] * 64.0) * 4.0 * kScale;
        const double D = (base[j] * 64.0 + mult[j] * delta[j]) * 4.0 * kScale;
        printf("  axis %d:\n", j);
        printf("    A (mult*base + delta)          * SCALE     = %+.6f\n", A);
        printf("    B (base + mult*delta)          * SCALE     = %+.6f\n", B);
        printf("    C (mult*base + delta*64) * 4   * SCALE     = %+.6f   <- matches the\n", C);
        printf("                                                            0.884 ceiling\n");
        printf("    D (base*64 + mult*delta) * 4   * SCALE     = %+.6f\n", D);
    }
    printf("\nThis project's SHIPPING reader currently computes candidate A\n");
    printf("(pre-correction it also used an unsigned base and int8 deltas).\n");
    printf("Please report which of these your decoder produces, or the\n");
    printf("expression if it is none of them, plus your base_j/mult_j/delta_j\n");
    printf("for these exact bytes.\n");
    g_done = true;
}

void walkArchive(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size() && !g_done; ++i) {
        if (endsWith(c.entries()[i].name, ".anim_pc")) {
            std::vector<uint8_t> b;
            if (entryBytes(c, i, b)) dumpClip(c.entries()[i].name, b);
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { walkArchive(c.openNested(i)); } catch (...) {}
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: dump_anim_key <archive> [clip] [track] [key]\n"); return 1; }
    if (argc > 2) g_want = argv[2];
    if (argc > 3) g_track = static_cast<size_t>(atoi(argv[3]));
    if (argc > 4) g_key = static_cast<size_t>(atoi(argv[4]));
    std::vector<uint8_t> b = readFile(argv[1]);
    if (b.empty()) { printf("could not read %s\n", argv[1]); return 1; }
    try {
        vpp::Container c(vpp::ByteView(b.data(), b.size()));
        walkArchive(c);
    } catch (const std::exception& e) { printf("%s\n", e.what()); }
    if (!g_done) printf("no matching clip/track/key found\n");
    return 0;
}
