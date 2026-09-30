// Diagnostic: WHY did 258/536 `_media.bnk_pc` files fail the first run of
// validate_media_bank.exe, all of them at record 0?
//
// spec-audio-format.md Sec4.2 makes two claims that the first version of
// this project's reader folded into one predicate:
//
//   (a) the CHAIN RULE: offset[i+1] = offset[i] + round_up(size[i] +
//       extra[i], 0x800) - "replay-verified, 536/536 files, 89,631 records,
//       zero chain breaks";
//   (b) the ANCHOR: "Record 0's offset is always 0x800".
//
// The reader implemented (b) by starting its cursor at 0x800 and then
// checking every record's own offset against the running cursor - so a file
// where (b) is false fails at record 0 and never tests (a) at all. That is
// exactly the conjunction trap HANDOFF Sec3 names: "a refutation is scoped
// to the CONJUNCTION it tested. Write down which compound claim failed, or
// it drags its innocent components down with it."
//
// This probe separates them. For every media entry it reads record 0's
// offset FROM THE TABLE instead of assuming it, then:
//
//   * reports whether (b) holds - offset[0] == 0x800;
//   * runs the chain rule (a) from whatever offset[0] actually is, and
//     reports whether it lands EXACTLY on the container-declared entry size;
//   * tests one specific candidate explanation for the non-0x800 anchors -
//     that offset[0] is simply the first 0x800 block after the header AND
//     the record table, i.e. round_up(0x20 + recordCount * 16, 0x800),
//     which for a table of more than 127 records is more than 0x800. A
//     failing case for that candidate is any file where the two disagree.
//
// Read-only, prints statistics; no game data is modified and nothing inside
// a record's payload is touched (third-party Wwise bytes - see
// include/sr3audio/media_bank.h).
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "sr3audio/media_bank.h"
#include "vpp/container.h"

namespace {

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n > 0 ? n : 0));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}

bool endsWith(const std::string& n, const std::string& s) {
    return n.size() >= s.size() && n.compare(n.size() - s.size(), s.size(), s) == 0;
}

size_t roundUpBlock(size_t v) { return (v + 0x7FF) / 0x800 * 0x800; }

long long g_files = 0;
long long g_anchorIs0x800 = 0;
long long g_anchorIsTableEnd = 0;          // offset[0] == round_up(0x20 + count*16, 0x800)
long long g_anchorNeither = 0;
long long g_chainClosesFromRealAnchor = 0; // claim (a), tested independently of (b)
long long g_chainFailsFromRealAnchor = 0;
long long g_declaredCountMatchesWalk = 0;
long long g_recordsTotal = 0;
long long g_tableEndAgreesWhenAnchorIs0x800 = 0;
long long g_anchor0x800Files = 0;
std::map<uint32_t, long long> g_anchorHistogram;
int g_dumped = 0;

void check(const std::string& archive, const std::string& name, vpp::ByteView content,
           size_t declaredSize) {
    ++g_files;
    sr3audio::MediaBank mb = sr3audio::MediaBank::parse(content);
    uint32_t count = mb.declaredRecordCount();

    if (0x20 + static_cast<size_t>(count) * 16 > content.size()) {
        printf("  %s/%s: declared count %u does not fit in %zu bytes\n", archive.c_str(),
               name.c_str(), count, content.size());
        return;
    }

    uint32_t anchor = content.readU32LE(0x20);
    ++g_anchorHistogram[anchor];

    size_t tableEnd = roundUpBlock(0x20 + static_cast<size_t>(count) * 16);
    bool anchorIs0x800 = (anchor == 0x800);
    bool anchorIsTableEnd = (static_cast<size_t>(anchor) == tableEnd);

    if (anchorIs0x800) {
        ++g_anchorIs0x800;
        ++g_anchor0x800Files;
        if (anchorIsTableEnd) ++g_tableEndAgreesWhenAnchorIs0x800;
    }
    if (anchorIsTableEnd) ++g_anchorIsTableEnd;
    if (!anchorIs0x800 && !anchorIsTableEnd) {
        ++g_anchorNeither;
        if (g_dumped < 10) {
            ++g_dumped;
            printf("  ANCHOR NEITHER %s/%s: offset[0]=%u (0x%x), count=%u, "
                   "round_up(0x20+count*16,0x800)=%zu (0x%zx)\n",
                   archive.c_str(), name.c_str(), anchor, anchor, count, tableEnd, tableEnd);
        }
    }

    // Claim (a) alone: chain from the REAL anchor, whatever it is.
    size_t cursor = anchor;
    long long walked = 0;
    bool ok = true;
    for (size_t i = 0;; ++i) {
        if (cursor == declaredSize) break;
        if (cursor > declaredSize) { ok = false; break; }
        size_t at = 0x20 + i * 16;
        if (at + 16 > content.size()) { ok = false; break; }
        uint32_t off = content.readU32LE(at + 0x00);
        uint32_t extra = content.readU32LE(at + 0x04);
        uint32_t size = content.readU32LE(at + 0x08);
        if (static_cast<size_t>(off) != cursor) { ok = false; break; }
        size_t span = roundUpBlock(static_cast<size_t>(size) + static_cast<size_t>(extra));
        if (span == 0) { ok = false; break; }
        ++walked;
        cursor += span;
    }

    if (ok) {
        ++g_chainClosesFromRealAnchor;
        g_recordsTotal += walked;
        if (static_cast<long long>(count) == walked) ++g_declaredCountMatchesWalk;
    } else {
        ++g_chainFailsFromRealAnchor;
        if (g_dumped < 10) {
            ++g_dumped;
            printf("  CHAIN STILL FAILS %s/%s: anchor=%u, declared size=%zu, walked %lld\n",
                   archive.c_str(), name.c_str(), anchor, declaredSize, walked);
        }
    }
}

void walkContainer(const vpp::Container& c, const std::string& archive) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        if (!endsWith(n, "_media.bnk_pc")) continue;
        try {
            std::vector<uint8_t> storage;
            vpp::ByteView bytes;
            if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
                bytes = c.rawEntryBytes(i);
            } else {
                auto r = c.decompressEntry(i);
                if (r.data.empty()) continue;
                storage = std::move(r.data);
                bytes = vpp::ByteView(storage.data(), storage.size());
            }
            check(archive, n, bytes, c.entries()[i].payload.decompressedLength);
        } catch (const std::exception& ex) {
            printf("  EXC %s/%s: %s\n", archive.c_str(), n.c_str(), ex.what());
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string path = argv[i];
        std::string base = path;
        size_t slash = base.find_last_of("/\\");
        if (slash != std::string::npos) base = base.substr(slash + 1);
        std::vector<uint8_t> b = readFile(path);
        if (b.empty()) continue;
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            walkContainer(c, base);
            printf("scanned %s\n", base.c_str());
            fflush(stdout);
        } catch (const std::exception& ex) {
            printf("FAILED to open %s: %s\n", path.c_str(), ex.what());
        }
    }

    printf("\n=== record 0's offset: anchor claim vs chain claim, separated ===\n");
    printf("media files examined                       : %lld\n", g_files);
    printf("(b) offset[0] == 0x800                     : %lld / %lld   (spec Sec4.2: \"always\")\n",
           g_anchorIs0x800, g_files);
    printf("    offset[0] == round_up(0x20+count*16,0x800): %lld / %lld\n", g_anchorIsTableEnd,
           g_files);
    printf("    neither                                : %lld\n", g_anchorNeither);
    printf("    of the 0x800-anchored files, table-end also predicts 0x800 : %lld / %lld\n",
           g_tableEndAgreesWhenAnchorIs0x800, g_anchor0x800Files);
    printf("(a) chain closes EXACTLY on the declared size, starting from the\n");
    printf("    real offset[0] rather than an assumed 0x800 : %lld / %lld\n",
           g_chainClosesFromRealAnchor, g_files);
    printf("    chain still fails                      : %lld\n", g_chainFailsFromRealAnchor);
    printf("    records walked, total                  : %lld   (spec Sec4.2: 89,631)\n",
           g_recordsTotal);
    printf("    +0x18 == walked count                  : %lld / %lld   (spec Sec4.1: 536/536)\n",
           g_declaredCountMatchesWalk, g_chainClosesFromRealAnchor);

    printf("\ndistinct offset[0] values observed:\n");
    for (const auto& kv : g_anchorHistogram) {
        printf("  %8u (0x%06x) x %lld\n", kv.first, kv.first, kv.second);
    }
    return 0;
}
