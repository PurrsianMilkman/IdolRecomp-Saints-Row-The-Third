// Real-data validation for sr3audio: the `_media.bnk_pc` (`VWSBPC`) block
// directory (spec-audio-format.md Sec4) and its cross-reference to the
// sibling plain `.bnk_pc`'s Wwise SoundBankID (Sec5).
//
// Walks the four mode-(b) audio archives that spec pass used for its own
// whole-population replay - `sounds.vpp_pc`, `sounds_common.vpp_pc`,
// `voices.vpp_pc`, `cutscene_sounds.vpp_pc` - finds every `_media.bnk_pc`
// entry, and walks its record chain against the OUTER container's own
// declared uncompressed size for that entry (the directory's +0x0C field,
// `vpp::PayloadLocation::decompressedLength`). That value is the oracle
// Sec4.2 terminates on; nothing here chooses an endpoint.
//
// `soundboot.vpp_pc` and `sound_turbo.vpp_pc` are deliberately NOT in the
// list: Sec9.1/Sec9.2 record that neither contains a `_media.bnk_pc` at all
// (one ships `_media.mbnk_pc` under a shared-compressed-stream container
// where only entry 0 decodes independently, the other ships `.lm_pc`/`DMLV`
// files that are a different format entirely). Both are flagged OPEN there,
// not chased here. Pass them on the command line if you want to confirm the
// zero - the harness will simply report 0 media entries found.
//
// Figures this reproduces or contradicts (spec-audio-format.md Sec4.2/Sec5,
// Sec10):
//   * 536 `_media.bnk_pc` files walked cleanly, 536/536, zero chain breaks
//     and zero overshoots (122 + 53 + 275 + 86);
//   * 89,631 individual records walked in total;
//   * header +0x18 equals the real walked record count in 536/536 files;
//   * the +0x10 cross-reference equals the sibling bank's SoundBankID in
//     260/260 in-archive pairs (122 + 52 + 86; `voices.vpp_pc` contributes
//     0 pairs because it has no in-archive plain siblings at all).
// Every number below is printed with its own denominator next to the spec's
// claim, and NOT forced to agree with it - a divergence is a finding.
//
// TWO CONTROLS, per tools/validation/README.md's standing requirement that a
// new harness carry its falsifier from the start:
//
//  1. THE `extra`-IGNORING CHAIN CONTROL. Sec4.3 records that the first
//     (wrong) model, which treated `extra` as padding, still walked cleanly
//     on 530/536 files - a reader with that bug passes 98.9% of the real
//     population. This harness re-walks every file a second time with a
//     locally-written naive chain that drops `extra`, and prints how many
//     files that model gets right. If the naive number equals the correct
//     number, this harness has NOT demonstrated anything about `extra` on
//     this population, and says so.
//  2. THE CROSS-REFERENCE VALUE-DIVERSITY CONTROL. Sec5's own stated
//     failure mode for its 260/260 claim is the trivial one: two constant
//     fields agreeing by coincidence. So the harness counts DISTINCT
//     cross-reference ids and distinct SoundBankIDs seen. A match rate of
//     100% over a single repeated value would be worthless; over hundreds of
//     distinct values it is not.
//
// Nothing here parses a Wwise SoundBank beyond its first 16 bytes, and
// nothing reads a record's payload bytes. See include/sr3audio/media_bank.h
// for why that boundary is where it is.
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3audio/media_bank.h"
#include "sr3audio/wwise_bank_id.h"
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

bool endsWith(const std::string& n, const std::string& suffix) {
    if (n.size() < suffix.size()) return false;
    return n.compare(n.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Sec2's two shapes. `_media.bnk_pc` must be tested FIRST: a plain-bank test
// on "foo_media.bnk_pc" would otherwise strip only ".bnk_pc" and claim the
// stem "foo_media".
bool mediaStem(const std::string& n, std::string& stem) {
    const std::string suffix = "_media.bnk_pc";
    if (!endsWith(n, suffix)) return false;
    stem = n.substr(0, n.size() - suffix.size());
    return true;
}

bool plainStem(const std::string& n, std::string& stem) {
    const std::string suffix = ".bnk_pc";
    std::string ignored;
    if (mediaStem(n, ignored)) return false;
    if (!endsWith(n, suffix)) return false;
    stem = n.substr(0, n.size() - suffix.size());
    return true;
}

// Returns a view of the entry's bytes without copying when it is stored raw,
// and falls back to decompression otherwise. `storage` owns the bytes only
// in the decompressed case.
bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& storage,
                vpp::ByteView& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        out = c.rawEntryBytes(i);
        return true;
    }
    auto r = c.decompressEntry(i);
    bool ok = r.status == vpp::DecodeStatus::Ok ||
              r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
              r.status == vpp::DecodeStatus::ContentValidated ||
              r.status == vpp::DecodeStatus::RecoveredSharedStream ||
              r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!ok) return false;
    storage = std::move(r.data);
    out = vpp::ByteView(storage.data(), storage.size());
    return true;
}

// --- Sec4.3's naive model, written out here so the control is explicit and
// visibly WRONG-by-construction rather than a flag toggled inside the
// reader. Returns true if this file walks cleanly under the naive rule. ---
bool naiveWalkIgnoringExtra(vpp::ByteView content, size_t totalEntrySize, long long& recordsOut) {
    recordsOut = 0;
    // Seeded from record 0's own offset, exactly as the real walk is, so the
    // ONLY difference between this control and the reader is `extra`. (The
    // first version of this harness started both at a hardcoded 0x800 and
    // thereby measured two bugs at once - see the anchor note in
    // include/sr3audio/media_bank.h.)
    if (0x20 + 16 > content.size()) return false;
    size_t cursor = content.readU32LE(0x20);
    if (cursor == 0 || cursor % 0x800 != 0) return false;
    for (size_t i = 0;; ++i) {
        if (cursor == totalEntrySize) return true;
        if (cursor > totalEntrySize) return false;
        size_t at = 0x20 + i * 16;
        if (at + 16 > content.size()) return false;
        uint32_t offset = content.readU32LE(at + 0x00);
        uint32_t size = content.readU32LE(at + 0x08); // `extra` at +0x04 deliberately not read
        if (static_cast<size_t>(offset) != cursor) return false;
        size_t span = (static_cast<size_t>(size) + 0x7FF) / 0x800 * 0x800;
        if (span == 0) return false;
        ++recordsOut;
        cursor += span;
    }
}

// --- Population counters -------------------------------------------------
struct ArchiveStats {
    std::string name;
    long long entries = 0;
    long long mediaFound = 0;
    long long mediaWalkedOk = 0;
    long long mediaFailed = 0;
    long long recordsTotal = 0;
    long long plainFound = 0;
    long long plainReadOk = 0;
    long long pairsInArchive = 0;
    long long pairsMatching = 0;
};

std::vector<ArchiveStats> g_archives;

long long g_mediaFound = 0, g_mediaWalkedOk = 0, g_mediaFailed = 0;
long long g_recordsTotal = 0;
long long g_magicOk = 0;
long long g_constant0x08Match = 0;
long long g_declaredCountMatchesWalk = 0;
long long g_record0At0x800 = 0;
long long g_record0AtTableEnd = 0;
long long g_zeroDeclaredCount = 0;
long long g_recordsWithNonZeroExtra = 0;
long long g_filesWithNonZeroExtra = 0;
long long g_nonZeroExtraNotBlockMultiple = 0;
long long g_naiveWalkOk = 0;  // the Sec4.3 control
long long g_naiveRecords = 0;
long long g_plainFound = 0, g_plainReadOk = 0, g_plainFailed = 0;
int g_failDumped = 0;

std::map<uint32_t, long long> g_extraValueHistogram;

// The chain rule adds `size` and `extra` together, so it CANNOT by itself
// tell which of the two middle u32s is which - {offset, extra, size, tag}
// and {offset, size, extra, tag} produce identical arithmetic and identical
// 536/536 replays. What separates them is their value distributions, so
// those are measured here rather than taken on faith.
std::set<uint32_t> g_distinctSizeValues;
long long g_sizeZero = 0, g_sizeBlockMultiple = 0;
uint32_t g_sizeMax = 0;
uint32_t g_sizeMinNonZero = 0xFFFFFFFFu;

// Cross-reference bookkeeping. Collected across ALL archives, then resolved
// once at the end, so both the in-archive figure (Sec5's 260/260) and the
// cross-archive figure (Sec5's `interface` observation) can be reported.
struct MediaRef {
    std::string archive;
    std::string stem;
    uint32_t crossReferenceId = 0;
};
std::vector<MediaRef> g_mediaRefs;
std::map<std::string, std::map<std::string, uint32_t>> g_plainByArchiveStem; // archive -> stem -> id
std::map<std::string, uint32_t> g_plainByStemAnyArchive;
std::set<uint32_t> g_distinctCrossRefIds;
std::set<uint32_t> g_distinctSoundBankIds;

void checkMedia(ArchiveStats& st, const std::string& stem, vpp::ByteView content,
                size_t totalEntrySize) {
    ++g_mediaFound;
    ++st.mediaFound;

    try {
        sr3audio::MediaBank mb = sr3audio::MediaBank::parse(content);
        ++g_magicOk;
        if (mb.constantAt0x08MatchesShipped()) ++g_constant0x08Match;

        std::vector<sr3audio::MediaBankRecord> recs = mb.walk(totalEntrySize);

        ++g_mediaWalkedOk;
        ++st.mediaWalkedOk;
        g_recordsTotal += static_cast<long long>(recs.size());
        st.recordsTotal += static_cast<long long>(recs.size());

        if (mb.declaredRecordCount() == recs.size()) ++g_declaredCountMatchesWalk;
        if (mb.declaredRecordCount() == 0) ++g_zeroDeclaredCount;
        if (!recs.empty()) {
            // Sec4.2's "record 0's offset is always 0x800" vs this project's
            // measured generalisation. Both counted, neither assumed.
            if (recs[0].offset == 0x800) ++g_record0At0x800;
            if (static_cast<size_t>(recs[0].offset) == mb.blockAlignedTableEnd())
                ++g_record0AtTableEnd;
        }

        bool anyExtra = false;
        for (const auto& r : recs) {
            g_distinctSizeValues.insert(r.size);
            if (r.size == 0) ++g_sizeZero;
            if (r.size % 0x800 == 0) ++g_sizeBlockMultiple;
            if (r.size > g_sizeMax) g_sizeMax = r.size;
            if (r.size != 0 && r.size < g_sizeMinNonZero) g_sizeMinNonZero = r.size;
            if (r.extra != 0) {
                anyExtra = true;
                ++g_recordsWithNonZeroExtra;
                ++g_extraValueHistogram[r.extra];
                if (r.extra % 0x800 != 0) ++g_nonZeroExtraNotBlockMultiple;
            }
        }
        if (anyExtra) ++g_filesWithNonZeroExtra;

        // Control 1: the same file under Sec4.3's discarded naive model.
        long long naiveRecords = 0;
        if (naiveWalkIgnoringExtra(content, totalEntrySize, naiveRecords)) {
            ++g_naiveWalkOk;
            g_naiveRecords += naiveRecords;
        }

        MediaRef ref;
        ref.archive = st.name;
        ref.stem = stem;
        ref.crossReferenceId = mb.crossReferenceId();
        g_mediaRefs.push_back(ref);
        g_distinctCrossRefIds.insert(ref.crossReferenceId);
    } catch (const std::exception& ex) {
        ++g_mediaFailed;
        ++st.mediaFailed;
        if (g_failDumped < 12) {
            ++g_failDumped;
            printf("  MEDIA FAIL %s/%s_media.bnk_pc (declared size %zu): %s\n", st.name.c_str(),
                   stem.c_str(), totalEntrySize, ex.what());
        }
    }
}

void checkPlain(ArchiveStats& st, const std::string& stem, vpp::ByteView content) {
    ++g_plainFound;
    ++st.plainFound;
    try {
        sr3audio::WwiseBankHeaderPrefix p = sr3audio::readWwiseBankHeaderPrefix(content);
        ++g_plainReadOk;
        ++st.plainReadOk;
        g_plainByArchiveStem[st.name][stem] = p.soundBankId;
        g_plainByStemAnyArchive[stem] = p.soundBankId;
        g_distinctSoundBankIds.insert(p.soundBankId);
    } catch (const std::exception& ex) {
        ++g_plainFailed;
        if (g_failDumped < 12) {
            ++g_failDumped;
            printf("  PLAIN FAIL %s/%s.bnk_pc: %s\n", st.name.c_str(), stem.c_str(), ex.what());
        }
    }
}

void walkContainer(const vpp::Container& c, ArchiveStats& st) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        ++st.entries;
        const std::string& n = c.entries()[i].name;
        std::string stem;

        // One bad entry must not take out its siblings - HANDOFF Sec9.55.1's
        // lesson about unguarded per-entry loops. Each entry is wrapped.
        try {
            if (mediaStem(n, stem)) {
                std::vector<uint8_t> storage;
                vpp::ByteView bytes;
                if (!entryBytes(c, i, storage, bytes)) {
                    ++g_mediaFound;
                    ++st.mediaFound;
                    ++g_mediaFailed;
                    ++st.mediaFailed;
                    if (g_failDumped < 12) {
                        ++g_failDumped;
                        printf("  MEDIA UNREADABLE %s/%s\n", st.name.c_str(), n.c_str());
                    }
                    continue;
                }
                // The oracle: the outer container's OWN declared uncompressed
                // size for this entry (+0x0C), not bytes.size().
                checkMedia(st, stem, bytes, c.entries()[i].payload.decompressedLength);
            } else if (plainStem(n, stem)) {
                std::vector<uint8_t> storage;
                vpp::ByteView bytes;
                if (!entryBytes(c, i, storage, bytes)) continue;
                checkPlain(st, stem, bytes);
            }
        } catch (const std::exception&) {
            continue;
        }
    }

    // Sec2 measured every entry at the top level of these four archives, so
    // recursion is expected to find nothing; attempted anyway, cheaply, so
    // that "nothing nested" is a measurement rather than an assumption.
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        std::string ignored;
        if (mediaStem(n, ignored) || plainStem(n, ignored)) continue;
        if (c.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        try {
            walkContainer(c.openNested(i), st);
        } catch (const std::exception&) {
        }
    }
}

// Resolved once, before anything is printed, so the per-archive table below
// can show its pair counts rather than zeroes.
long long g_inArchivePairs = 0, g_inArchiveMatch = 0;
long long g_anyArchivePairs = 0, g_anyArchiveMatch = 0;
long long g_crossArchiveOnlyPairs = 0, g_crossArchiveOnlyMatch = 0;

void resolveCrossReferences() {
    std::map<std::string, long long> perArchivePairs, perArchiveMatch;

    for (const auto& ref : g_mediaRefs) {
        auto ait = g_plainByArchiveStem.find(ref.archive);
        bool sameArchive = false;
        if (ait != g_plainByArchiveStem.end()) {
            auto sit = ait->second.find(ref.stem);
            if (sit != ait->second.end()) {
                sameArchive = true;
                ++g_inArchivePairs;
                ++perArchivePairs[ref.archive];
                if (sit->second == ref.crossReferenceId) {
                    ++g_inArchiveMatch;
                    ++perArchiveMatch[ref.archive];
                }
            }
        }
        auto git = g_plainByStemAnyArchive.find(ref.stem);
        if (git != g_plainByStemAnyArchive.end()) {
            ++g_anyArchivePairs;
            if (git->second == ref.crossReferenceId) ++g_anyArchiveMatch;
            if (!sameArchive) {
                ++g_crossArchiveOnlyPairs;
                if (git->second == ref.crossReferenceId) ++g_crossArchiveOnlyMatch;
            }
        }
    }

    for (auto& a : g_archives) {
        a.pairsInArchive = perArchivePairs[a.name];
        a.pairsMatching = perArchiveMatch[a.name];
    }
}

void report() {
    resolveCrossReferences();

    printf("\n=== sr3audio: `_media.bnk_pc` (VWSBPC) wrapper - spec-audio-format.md Sec4 ===\n");
    printf("%-24s %8s %8s %8s %8s %10s %8s %8s\n", "archive", "entries", "media", "walked",
           "failed", "records", "plain", "pairs");
    for (const auto& a : g_archives) {
        printf("%-24s %8lld %8lld %8lld %8lld %10lld %8lld %8lld\n", a.name.c_str(), a.entries,
               a.mediaFound, a.mediaWalkedOk, a.mediaFailed, a.recordsTotal, a.plainFound,
               a.pairsInArchive);
    }

    printf("\n--- walk totals ---\n");
    printf("_media.bnk_pc entries found       : %lld   (spec Sec4.2: 536 across these four)\n",
           g_mediaFound);
    printf("walked cleanly                    : %lld / %lld   (spec: 536/536, zero chain breaks, "
           "zero overshoots)\n",
           g_mediaWalkedOk, g_mediaFound);
    printf("walk failures                     : %lld\n", g_mediaFailed);
    printf("records walked, total             : %lld   (spec: 89,631)\n", g_recordsTotal);
    printf("magic matched at +0x00            : %lld / %lld   (spec Sec4.1: 536/536)\n", g_magicOk,
           g_mediaFound);
    printf("+0x08 == 00 00 00 00 02 00 01 00  : %lld / %lld   (spec Sec4.1: constant in every "
           "sample; OPEN role, not enforced by the reader)\n",
           g_constant0x08Match, g_magicOk);
    printf("+0x18 == walked record count      : %lld / %lld   (spec Sec4.1: 536/536 - an "
           "INDEPENDENT second signal, measured here, never used to terminate the walk)\n",
           g_declaredCountMatchesWalk, g_mediaWalkedOk);
    printf("\n--- record 0's anchor: Sec4.2's literal claim vs this project's measured\n");
    printf("    generalisation. walk() ASSUMES NEITHER - it reads offset[0] from the\n");
    printf("    table - so both lines below are measurements, not gates. ---\n");
    printf("record 0 offset == 0x800          : %lld / %lld   (spec Sec4.2: \"always\")\n",
           g_record0At0x800, g_mediaWalkedOk);
    printf("record 0 offset == round_up(0x20 + count*16, 0x800) : %lld / %lld\n",
           g_record0AtTableEnd, g_mediaWalkedOk);
    printf("files declaring 0 records         : %lld   (the one shape walk() cannot "
           "represent - it needs an anchor to read)\n",
           g_zeroDeclaredCount);

    printf("\n--- the `extra` field (spec Sec4.2/Sec4.3) ---\n");
    printf("records with extra != 0           : %lld / %lld\n", g_recordsWithNonZeroExtra,
           g_recordsTotal);
    printf("files containing any extra != 0   : %lld / %lld   (spec Sec4.3: the naive model broke "
           "on exactly 6 files)\n",
           g_filesWithNonZeroExtra, g_mediaWalkedOk);
    printf("non-zero extra NOT a 0x800 multiple: %lld   (spec Sec4.2: always an exact multiple; "
           "any nonzero here contradicts it)\n",
           g_nonZeroExtraNotBlockMultiple);
    printf("distinct non-zero extra values    :");
    for (const auto& kv : g_extraValueHistogram) printf(" %u(x%lld)", kv.first, kv.second);
    printf("\n                                    (spec Sec4.2 observed: 2048, 4096, 6144, 8192, "
           "10240, 12288, 14336, 16384)\n");

    printf("\n--- which middle u32 is `size` and which is `extra`? The chain rule adds them,\n");
    printf("    so it cannot tell them apart; their value DISTRIBUTIONS can. ---\n");
    printf("+0x04 (`extra`): %zu distinct values, all listed above, all 0x800 multiples\n",
           g_extraValueHistogram.size());
    printf("+0x08 (`size`) : %zu distinct values, %lld zero, %lld a 0x800 multiple, "
           "range %u..%u\n",
           g_distinctSizeValues.size(), g_sizeZero, g_sizeBlockMultiple,
           g_sizeMinNonZero == 0xFFFFFFFFu ? 0u : g_sizeMinNonZero, g_sizeMax);
    printf("    A payload length should be high-cardinality and mostly NOT block-aligned;\n");
    printf("    a block allowance should be low-cardinality and always block-aligned. The\n");
    printf("    two lines above are what justify Sec4.2's field assignment on this data.\n");

    printf("\n--- CONTROL 1: the same population under Sec4.3's discarded naive chain\n");
    printf("    (`extra` dropped entirely). If this equals the correct count, this run has\n");
    printf("    NOT demonstrated that `extra` is load-bearing on this population. ---\n");
    printf("naive model walks cleanly         : %lld / %lld   (spec Sec4.3: 530/536)\n",
           g_naiveWalkOk, g_mediaFound);
    printf("correct model walks cleanly       : %lld / %lld\n", g_mediaWalkedOk, g_mediaFound);
    printf("files the naive model gets wrong  : %lld   (spec Sec4.3: exactly 6)\n",
           g_mediaWalkedOk - g_naiveWalkOk);
    printf("records under the naive model     : %lld   (vs %lld correct)\n", g_naiveRecords,
           g_recordsTotal);

    printf("\n=== sr3audio: cross-reference to the sibling Wwise SoundBankID - Sec5 ===\n");
    printf("plain .bnk_pc entries found       : %lld\n", g_plainFound);
    printf("  16-byte BKHD prefix read OK     : %lld\n", g_plainReadOk);
    printf("  BKHD prefix read failed         : %lld\n", g_plainFailed);

    for (const auto& a : g_archives) {
        printf("  %-24s in-archive pairs %4lld, +0x10 == SoundBankID %4lld\n", a.name.c_str(),
               a.pairsInArchive, a.pairsMatching);
    }
    printf("in-archive sibling pairs          : %lld\n", g_inArchivePairs);
    printf("  +0x10 == sibling SoundBankID    : %lld / %lld   (spec Sec5: 260/260)\n",
           g_inArchiveMatch, g_inArchivePairs);
    printf("cross-archive-only pairs          : %lld\n", g_crossArchiveOnlyPairs);
    printf("  matching                        : %lld / %lld   (spec Sec5 checked exactly one such "
           "pair directly: `interface`, 0x40e182ea)\n",
           g_crossArchiveOnlyMatch, g_crossArchiveOnlyPairs);
    printf("any-archive pairs (union)         : %lld, matching %lld\n", g_anyArchivePairs,
           g_anyArchiveMatch);

    printf("\n--- CONTROL 2: value diversity behind that match rate (Sec5's own stated\n");
    printf("    failure mode: two CONSTANT fields agreeing by coincidence) ---\n");
    printf("distinct +0x10 cross-reference ids : %zu   (over %lld media files)\n",
           g_distinctCrossRefIds.size(), g_mediaWalkedOk);
    printf("distinct SoundBankIDs              : %zu   (over %lld plain banks)\n",
           g_distinctSoundBankIds.size(), g_plainReadOk);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: validate_media_bank.exe <archive.vpp_pc> [...]\n");
        printf("  expected: sounds.vpp_pc sounds_common.vpp_pc voices.vpp_pc "
               "cutscene_sounds.vpp_pc\n");
        return 2;
    }

    for (int i = 1; i < argc; ++i) {
        std::string path = argv[i];
        std::string base = path;
        size_t slash = base.find_last_of("/\\");
        if (slash != std::string::npos) base = base.substr(slash + 1);

        std::vector<uint8_t> b = readFile(path);
        if (b.empty()) {
            printf("skip (empty/unreadable): %s\n", path.c_str());
            continue;
        }
        ArchiveStats st;
        st.name = base;
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            walkContainer(c, st);
            printf("scanned %-24s entries %4lld  media %4lld  plain %4lld\n", base.c_str(),
                   st.entries, st.mediaFound, st.plainFound);
            fflush(stdout);
        } catch (const std::exception& ex) {
            printf("FAILED to open %s: %s\n", path.c_str(), ex.what());
        }
        g_archives.push_back(st);
    }

    report();

    // Refuse to report success when the walk itself failed anywhere - a
    // harness that prints statistics next to a silent failure leaves the
    // reader to reconcile them (HANDOFF Sec3: a check must GATE the result).
    return (g_mediaFailed == 0 && g_mediaFound > 0) ? 0 : 1;
}
