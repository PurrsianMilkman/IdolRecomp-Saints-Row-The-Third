// An independent census of the .anim_pc population, with every filter's
// effect counted separately.
//
// WHY THIS EXISTS. This project reported "518 clips carry flags bit 0x40",
// split 328 / 135 / 55. Team A's spec carries figures that look adjacent but
// are not obviously the same population. Rather than reconcile two numbers
// arithmetically - which is exactly the cross-boundary error both teams keep
// hitting - each side re-derives its own from the files, states its
// predicate, and reports the number WITH the predicate attached. If two
// independently-derived numbers still disagree, the disagreement is the
// finding.
//
// So this tool deliberately does NOT reference any external figure. It
// states a predicate and counts what satisfies it, showing the attrition at
// every step so the denominator is never implicit.
//
// PREDICATE, stated in full:
//   a clip is COUNTED if, and only if:
//     (a) the archive entry name ends in ".anim_pc", AND
//     (b) sr3anim::Animation::parse() accepts it (magic 'ANIM', version 14), AND
//     (c) the flags byte at +0x05 has bit 0x40 set.
//   It is NOT required to have a +0x30 field, to walk, or to land anywhere.
//   Those are reported as sub-counts, never as filters on the headline.
//
// Usage: probe_anim_population <archive.vpp_pc>
#include <cstdio>
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

int g_named = 0, g_extractFailed = 0, g_parseFailed = 0, g_parsed = 0;
int g_bitSet = 0, g_bitClear = 0;
int g_bitSetNoTrailing = 0, g_bitClearNoTrailing = 0;
// Within each flags stratum: walk completes? lands on the declared end?
int g_setWalk = 0, g_setNoWalk = 0, g_setLand = 0, g_setMiss = 0;
int g_clearWalk = 0, g_clearNoWalk = 0, g_clearLand = 0, g_clearMiss = 0;

void doClip(const std::vector<uint8_t>& raw) {
    vpp::ByteView bv(raw.data(), raw.size());
    sr3anim::Animation a;
    try {
        a = sr3anim::Animation::parse(bv);
    } catch (...) {
        ++g_parseFailed;
        return;
    }
    ++g_parsed;

    const bool bit = (a.flags() & sr3anim::kExtraPayloadFlag) != 0;
    if (bit) ++g_bitSet; else ++g_bitClear;
    if (!a.hasTrailingOffset()) {
        if (bit) ++g_bitSetNoTrailing; else ++g_bitClearNoTrailing;
    }

    sr3anim::Payload pl = sr3anim::Payload::walk(bv, a);
    const bool walked = pl.walkComplete();
    const bool landed = walked && pl.landedOnDeclaredEnd();

    if (bit) {
        if (walked) ++g_setWalk; else ++g_setNoWalk;
        if (walked) { if (landed) ++g_setLand; else ++g_setMiss; }
    } else {
        if (walked) ++g_clearWalk; else ++g_clearNoWalk;
        if (walked) { if (landed) ++g_clearLand; else ++g_clearMiss; }
    }
}

void walkArchive(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (endsWith(c.entries()[i].name, ".anim_pc")) {
            ++g_named;
            std::vector<uint8_t> b;
            if (!entryBytes(c, i, b)) { ++g_extractFailed; continue; }
            doClip(b);
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { walkArchive(c.openNested(i)); } catch (...) {}
        }
    }
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> b = readFile(argv[i]);
        if (b.empty()) { printf("could not read %s\n", argv[i]); continue; }
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            walkArchive(c);
        } catch (const std::exception& e) { printf("%s\n", e.what()); }
        printf("scanned %s\n", argv[i]);
    }

    printf("\n=== .anim_pc POPULATION CENSUS (predicate stated, no external figure used) ===\n");
    printf("entries named *.anim_pc            : %d\n", g_named);
    printf("  could not be extracted           : %d\n", g_extractFailed);
    printf("  rejected by Animation::parse     : %d\n", g_parseFailed);
    printf("  PARSED                           : %d\n", g_parsed);

    printf("\nsplit by flags bit 0x40 (this is the headline number):\n");
    printf("  bit 0x40 SET                     : %d\n", g_bitSet);
    printf("  bit 0x40 CLEAR                   : %d\n", g_bitClear);
    printf("  (sum)                            : %d\n", g_bitSet + g_bitClear);

    printf("\nclips lacking a +0x30 field, per stratum - reported, NOT filtered:\n");
    printf("  bit SET,   no +0x30              : %d\n", g_bitSetNoTrailing);
    printf("  bit CLEAR, no +0x30              : %d\n", g_bitClearNoTrailing);

    printf("\nwalk outcome, WITHIN each stratum:\n");
    printf("  %-34s %8s %8s\n", "", "bit SET", "bit CLR");
    printf("  %-34s %8d %8d\n", "walk completes", g_setWalk, g_clearWalk);
    printf("  %-34s %8d %8d\n", "walk does NOT complete", g_setNoWalk, g_clearNoWalk);
    printf("  %-34s %8d %8d\n", "  ...of walkers, lands on +0x30", g_setLand, g_clearLand);
    printf("  %-34s %8d %8d\n", "  ...of walkers, misses +0x30", g_setMiss, g_clearMiss);

    printf("\nCORPUS-WIDE totals (both strata combined) - stated separately\n");
    printf("because a corpus-wide residual and a per-stratum one are DIFFERENT\n");
    printf("quantities, and conflating them is how two teams get numbers that\n");
    printf("look adjacent but are not:\n");
    printf("  walks, whole corpus              : %d\n", g_setWalk + g_clearWalk);
    printf("  walk failures, whole corpus      : %d\n", g_setNoWalk + g_clearNoWalk);
    printf("  walk-but-miss, whole corpus      : %d\n", g_setMiss + g_clearMiss);
    printf("  lands, whole corpus              : %d\n", g_setLand + g_clearLand);
    return 0;
}
