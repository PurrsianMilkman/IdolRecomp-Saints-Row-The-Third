// Is a translation control record with span == 0 legitimate?
//
// Two implementations of the same walk disagreed on 14 clips. Cause found
// and unanimous: a probe's private copy REJECTED any 8-byte translation
// control record whose span field reads 0; the shipping reader
// (sr3anim::Payload::walk) has no such check and simply advances to the next
// record until the spans cover the track's keys.
//
// Knowing the cause does not say which behaviour is CORRECT, and that
// matters: it decides 14 clips' membership in the 121-vs-69 split.
//
// THE ORACLE, and it is the file's own: a clip whose walk lands EXACTLY on
// the offset declared at header +0x30 has been validated end-to-end by a
// number the file itself states. So:
//
//   if clips containing a span==0 record still land exactly on +0x30,
//   then skipping such records is CORRECT - the file confirms it.
//
//   if span==0 records appear ONLY in clips that never land, the oracle
//   says nothing and the question stays open. That is a real possible
//   outcome and must be reported as such rather than resolved by taste.
//
// Note the asymmetry deliberately: this test can CONFIRM the skip, and it
// can fail to decide. It cannot by itself prove the skip wrong - a clip
// that fails to land could be failing for any number of reasons. Reported
// accordingly.
//
// Usage: probe_anim_span0 <archive.vpp_pc>
#include <cstdio>
#include <fstream>
#include <map>
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

int g_clips = 0, g_walked = 0;
int g_withSpan0 = 0;
int g_span0AndLands = 0, g_span0AndMisses = 0;   // THE decisive split
int g_noSpan0AndLands = 0, g_noSpan0AndMisses = 0;
long long g_span0Records = 0, g_totalRecords = 0;
std::map<int, int> g_span0PerClip;
std::vector<std::string> g_examples;

void doClip(const std::string& name, const std::vector<uint8_t>& raw) {
    vpp::ByteView bv(raw.data(), raw.size());
    sr3anim::Animation a;
    try { a = sr3anim::Animation::parse(bv); } catch (...) { return; }
    ++g_clips;

    sr3anim::Payload pl = sr3anim::Payload::walk(bv, a);
    if (!pl.walkComplete()) return;
    ++g_walked;
    const bool lands = pl.landedOnDeclaredEnd();

    int span0 = 0;
    for (const auto& blk : pl.tracks()) {
        if (blk.translationControlStride != 8) continue;
        for (size_t r = 0; r < blk.translationControlCount; ++r) {
            const size_t at = blk.translationControlOffset + r * 8;
            if (at + 8 > bv.size()) break;
            ++g_totalRecords;
            if (bv.readU16LE(at) == 0) { ++span0; ++g_span0Records; }
        }
    }

    if (span0 > 0) {
        ++g_withSpan0;
        g_span0PerClip[span0] += 1;
        if (lands) ++g_span0AndLands; else ++g_span0AndMisses;
        if (lands && g_examples.size() < 8) {
            char buf[200];
            snprintf(buf, sizeof buf, "%-40s flags=0x%02X span0=%d LANDS on +0x30",
                     name.c_str(), a.flags(), span0);
            g_examples.push_back(buf);
        }
    } else {
        if (lands) ++g_noSpan0AndLands; else ++g_noSpan0AndMisses;
    }
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
        if (b.empty()) { printf("could not read %s\n", argv[i]); continue; }
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            walkArchive(c);
        } catch (const std::exception& e) { printf("%s\n", e.what()); }
        printf("scanned %s\n", argv[i]);
    }

    printf("\n=== span==0 translation control records: legitimate or not? ===\n");
    printf("clips parsed                     : %d\n", g_clips);
    printf("clips whose walk completed       : %d   (only these can be inspected)\n", g_walked);
    printf("8-byte control records seen      : %lld\n", g_totalRecords);
    printf("  ...with span == 0              : %lld\n", g_span0Records);
    printf("clips containing >=1 span==0     : %d\n", g_withSpan0);

    printf("\nTHE DECISIVE SPLIT - does a clip containing a span==0 record still\n");
    printf("land exactly on the offset the FILE ITSELF declares at +0x30?\n\n");
    printf("  %-28s %10s %10s\n", "", "LANDS", "misses");
    printf("  %-28s %10d %10d\n", "contains a span==0 record", g_span0AndLands, g_span0AndMisses);
    printf("  %-28s %10d %10d\n", "contains none", g_noSpan0AndLands, g_noSpan0AndMisses);

    printf("\nspan==0 records per clip (clips with at least one):\n");
    int shown = 0;
    for (const auto& kv : g_span0PerClip) {
        if (shown++ < 10) printf("  %3d record(s) : %d clips\n", kv.first, kv.second);
    }

    printf("\nexamples of clips that contain a span==0 record AND land:\n");
    if (g_examples.empty()) printf("  (none)\n");
    for (const auto& s : g_examples) printf("  %s\n", s.c_str());

    printf("\nREADING THIS RESULT:\n");
    printf("  span0-AND-LANDS > 0  => skipping span==0 is CONFIRMED by the\n");
    printf("                          file's own declared endpoint.\n");
    printf("  span0-AND-LANDS == 0 => the oracle is silent; the question stays\n");
    printf("                          OPEN. This test cannot prove the skip\n");
    printf("                          WRONG, only confirm it or fail to decide.\n");
    return 0;
}
