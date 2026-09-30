// What is the UNIT of the per-key duration byte, and where is its TOTAL
// recorded in the header?
//
// probe_anim_fourth_duration.cpp has just established, empirically and with
// two controls, that the trailing extra byte after each translation key's
// three axis deltas is a PER-KEY DURATION: summed over a track, it lands at
// nearly the same total across every track of one clip (median relative
// spread 0.0057 within-clip vs 0.3670 cross-clip vs 1.4041 for a same-shape
// control, sum(|dx|); 50.27% of clips agree to within 1%). That is exactly
// what a per-key duration must do, since every track in a clip spans the
// same animation.
//
// THE REMAINING QUESTION this probe answers: if sum(byte) over a track is
// the clip's total duration, it should be RECORDED somewhere - most likely
// the header, since nothing else in the confirmed structure carries a
// per-clip total. This probe computes S = the per-track sum of extra bytes
// (median across a clip's tracks, since they agree but not exactly) and
// scores S against every header quantity this reader exposes:
//   - field_0x06_rawCount()            u16 @ +0x06
//   - field_0x06, decomposed            two u8s (low byte, high byte)
//   - field_0x08_rawCount()            u8 @ +0x08
//   - field_0x09_rawCount()            u8 @ +0x09
//   - field_0x0A_rawCount()            u8 @ +0x0A, track count (near-control)
//   - field_0x0B_rawCount()            u8 @ +0x0B, bone count  (near-control)
//   - the entry's file size                          (near-control)
//   - the declared payload end, header +0x30          (near-control)
// against four readings of S - S itself, S/2, S+1, S-1 - since an off-by-one
// or a half-rate encoding is common and would otherwise hide an exact match.
//
// For every candidate this probe reports the exact-match rate (with its
// denominator) AND the Pearson correlation with n, because a correlation is
// not a match: a candidate that merely tracks S loosely is not the same
// finding as one that reproduces it bit-for-bit. It also reports, per
// candidate and per S-reading, a SHUFFLED-S control (S taken from a
// DIFFERENT clip, same technique used throughout this project's probes) -
// this is what "found by chance" looks like, and if a real match rate is
// not far above it, nothing has been found.
//
// The near-controls (track count, bone count, file size, declared payload
// end) are expected to fail. If one of THOSE scores well instead, that is a
// sign the test itself is broken, not that the near-control is secretly the
// answer - this probe says so plainly if it happens.
//
// Data source: the existing reader only - vpp::Container, sr3anim::
// Animation::parse per .anim_pc entry, sr3anim::Payload::walk for track
// layout - restricted to the same clean, oracle-confirmed population used
// throughout: clips without flags bit 0x40 (Sec6c.4) whose walk both
// completes and lands exactly on the file's declared end (Sec6c.1). This
// file only reads; it modifies no existing file, reader, or probe.
//
// Usage: probe_anim_duration_unit <archive.vpp_pc>
#include <algorithm>
#include <cmath>
#include <cstdint>
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

// ---- population counters -------------------------------------------------
int g_clips = 0;
long long g_anim_pc_seen = 0;
long long g_skipped_parse_failed = 0;
long long g_skipped_extra_payload_flag = 0;
long long g_skipped_walk_incomplete_or_off_end = 0;

int g_tracks = 0;
long long g_skipped_no_keys = 0;              // translationKeys == 0
long long g_skipped_wide_translation_stride = 0;
long long g_skipped_control_walk_short = 0;
long long g_skipped_oob = 0;

long long g_tracksOneKey = 0; // translationKeys == 1: byteSum computed (feeds the pooled
                              // S/translationKeys stat) but EXCLUDED from a clip's S median.
                              // A lone byte is capped at 0-255 and cannot represent totals
                              // that run into the hundreds (S reaches 2002 - see below), so
                              // folding it into the same median as multi-key tracks pollutes
                              // it. This mirrors probe_anim_fourth_duration.cpp's "headline
                              // tracks" methodology (translationKeys>=2), which is what
                              // produced the 0.0057 within-clip spread figure this probe's
                              // header cites; without this exclusion the measured spread
                              // does not reproduce that figure.
long long g_clipsHeadlineLT2 = 0;  // <2 tracks with translationKeys>=2: S not computable here
long long g_clipsHeadlineGE2 = 0;  // >=2: S computed, spread/agreement measurable
long long g_clipsAgreeWithin1pct = 0; // among HeadlineGE2, (max-min)/median(S) <= 0.01
long long g_SZeroCount = 0;               // clips where S == 0 exactly
long long g_clipsMissingTrailingOffset = 0; // hasTrailingOffset() false (expected: 0, since
                                             // landedOnDeclaredEnd() already requires it)

// ---- per-clip aligned arrays: index i is the same clip in every vector ---
std::vector<double> g_S;          // per-clip S = median(track sums) across qualifying tracks
std::vector<double> g_field06;    // u16 @ +0x06
std::vector<double> g_field06lo;  // low byte of the above
std::vector<double> g_field06hi;  // high byte of the above
std::vector<double> g_field08;    // u8 @ +0x08
std::vector<double> g_field09;    // u8 @ +0x09
std::vector<double> g_field0A;    // u8 @ +0x0A, track count (near-control)
std::vector<double> g_field0B;    // u8 @ +0x0B, bone count (near-control)
std::vector<double> g_fileSize;   // entry byte size (near-control)

// Declared payload end (+0x30) is guarded separately since a clip could in
// principle lack it (it never does among clips that land on it, but the
// count above says so rather than assuming it).
std::vector<double> g_S_trailing;
std::vector<double> g_trailing;

std::vector<double> g_relSpread; // (max-min)/median(S), one per clip with >=2 qualifying tracks
std::vector<double> g_relSpreadAligned; // same value, but ALWAYS pushed 1:1 with g_S/g_field06/
                                         // etc below (never conditionally skipped), so a
                                         // candidate's match rate can be stratified by it

// Pooled per-track "average gap between keys" = trackSum / translationKeys,
// one entry per qualifying track (not per clip) - this is what the extra
// byte's own scale looks like if it is a frame count at a fixed rate.
std::vector<double> g_gapPerKey;

double mean(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    double s = 0;
    for (double x : v) s += x;
    return s / static_cast<double>(v.size());
}
double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
double pct(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[static_cast<size_t>(p * static_cast<double>(v.size() - 1))];
}
double pearson(const std::vector<double>& x, const std::vector<double>& y) {
    const size_t n = x.size();
    if (n < 2 || y.size() != n) return 0.0;
    double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
    for (size_t i = 0; i < n; ++i) {
        sx += x[i]; sy += y[i];
        sxx += x[i] * x[i]; syy += y[i] * y[i];
        sxy += x[i] * y[i];
    }
    const double num = static_cast<double>(n) * sxy - sx * sy;
    const double den = std::sqrt((static_cast<double>(n) * sxx - sx * sx) *
                                  (static_cast<double>(n) * syy - sy * sy));
    return den > 0 ? num / den : 0.0;
}

struct TrackResult {
    bool ok = false;
    uint32_t translationKeys = 0;
    double byteSum = 0.0;
};

// Reads one track's per-key extra bytes and sums them. Gated exactly as
// probe_anim_fourth_duration.cpp gates it: the 0x20 wide-translation-record
// stride is disassembly-only and unexercised in shipped data (reported, not
// silently folded in), and the control-record walk must cover every key so a
// truncated/malformed run doesn't silently under-sum.
TrackResult doTrack(vpp::ByteView bv, const sr3anim::TrackBlock& blk) {
    TrackResult tr;
    if (blk.translationKeys == 0) { ++g_skipped_no_keys; return tr; }
    if (blk.translationControlStride != 8) {
        ++g_skipped_wide_translation_stride;
        return tr;
    }
    if (blk.translationControlCount == 0) { ++g_skipped_control_walk_short; return tr; }

    const size_t sampleBase = blk.translationSampleOffset;
    const size_t extraBase = sampleBase + blk.translationKeys * 3;
    if (extraBase + blk.translationKeys > bv.size()) { ++g_skipped_oob; return tr; }

    size_t key = 0;
    for (size_t r = 0; r < blk.translationControlCount && key < blk.translationKeys; ++r) {
        const size_t at = blk.translationControlOffset + r * blk.translationControlStride;
        if (at + 8 > bv.size()) break;
        const uint32_t span = bv.readU16LE(at);
        if (span == 0) break;
        for (uint32_t s = 0; s < span && key < blk.translationKeys; ++s, ++key) {}
    }
    if (key != blk.translationKeys) { ++g_skipped_control_walk_short; return tr; }

    double sum = 0.0;
    for (size_t k = 0; k < blk.translationKeys; ++k) sum += static_cast<double>(bv.at(extraBase + k));

    tr.ok = true;
    tr.translationKeys = static_cast<uint32_t>(blk.translationKeys);
    tr.byteSum = sum;
    return tr;
}

void doClip(const std::vector<uint8_t>& raw) {
    vpp::ByteView bv(raw.data(), raw.size());
    sr3anim::Animation a;
    try { a = sr3anim::Animation::parse(bv); } catch (...) { ++g_skipped_parse_failed; return; }
    if ((a.flags() & sr3anim::kExtraPayloadFlag) != 0) { ++g_skipped_extra_payload_flag; return; }
    sr3anim::Payload pl = sr3anim::Payload::walk(bv, a);
    if (!pl.walkComplete() || !pl.landedOnDeclaredEnd()) { ++g_skipped_walk_incomplete_or_off_end; return; }
    ++g_clips;

    std::vector<double> headlineSums; // translationKeys >= 2 only - see g_tracksOneKey comment
    for (const auto& blk : pl.tracks()) {
        TrackResult tr = doTrack(bv, blk);
        if (!tr.ok) continue;
        ++g_tracks;
        g_gapPerKey.push_back(tr.byteSum / static_cast<double>(tr.translationKeys));
        if (tr.translationKeys == 1) { ++g_tracksOneKey; continue; }
        headlineSums.push_back(tr.byteSum);
    }

    if (headlineSums.size() < 2) { ++g_clipsHeadlineLT2; return; }
    ++g_clipsHeadlineGE2;

    const double S = median(headlineSums);
    if (S == 0.0) ++g_SZeroCount;

    double relSpreadThisClip = 0.0;
    {
        const double mn = *std::min_element(headlineSums.begin(), headlineSums.end());
        const double mx = *std::max_element(headlineSums.begin(), headlineSums.end());
        if (S > 0.0) {
            relSpreadThisClip = (mx - mn) / S;
            g_relSpread.push_back(relSpreadThisClip);
            if (relSpreadThisClip <= 0.01) ++g_clipsAgreeWithin1pct;
        } else if (mx == mn) {
            // Degenerate but genuine agreement: every headline track sums to 0.
            relSpreadThisClip = 0.0;
            g_relSpread.push_back(0.0);
            ++g_clipsAgreeWithin1pct;
        } else {
            relSpreadThisClip = 1.0; // S==0 but tracks disagree: treat as "not agreeing"
        }
    }
    // Aligned 1:1 with g_S/g_field06/etc below - used to stratify the best
    // candidate's match rate by how solid S itself is in that clip.
    g_relSpreadAligned.push_back(relSpreadThisClip);

    g_S.push_back(S);
    g_field06.push_back(static_cast<double>(a.field_0x06_rawCount()));
    g_field06lo.push_back(static_cast<double>(a.field_0x06_rawCount() & 0xFF));
    g_field06hi.push_back(static_cast<double>((a.field_0x06_rawCount() >> 8) & 0xFF));
    g_field08.push_back(static_cast<double>(a.field_0x08_rawCount()));
    g_field09.push_back(static_cast<double>(a.field_0x09_rawCount()));
    g_field0A.push_back(static_cast<double>(a.field_0x0A_rawCount()));
    g_field0B.push_back(static_cast<double>(a.field_0x0B_rawCount()));
    g_fileSize.push_back(static_cast<double>(bv.size()));

    if (a.hasTrailingOffset()) {
        g_S_trailing.push_back(S);
        g_trailing.push_back(static_cast<double>(a.trailingOffset()));
    } else {
        ++g_clipsMissingTrailingOffset;
    }
}

void walkArchive(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (endsWith(c.entries()[i].name, ".anim_pc")) {
            ++g_anim_pc_seen;
            std::vector<uint8_t> b;
            if (entryBytes(c, i, b)) doClip(b);
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { walkArchive(c.openNested(i)); } catch (...) {}
        }
    }
}

struct CandidateResult {
    std::string name;
    size_t n = 0;
    long long matchS = 0, matchHalf = 0, matchPlus1 = 0, matchMinus1 = 0;
    long long ctlMatchS = 0, ctlMatchHalf = 0, ctlMatchPlus1 = 0, ctlMatchMinus1 = 0;
    double r = 0.0;
};

// Scores one header candidate against S, S/2, S+1, S-1, and against the same
// four readings computed on a SHUFFLED S (S drawn from a different clip via
// the i -> i + n/2 rotation used throughout this project's probes). Svals
// and cand must be index-aligned (same clip at the same position).
CandidateResult scoreCandidate(const std::string& name, const std::vector<double>& Svals,
                                const std::vector<double>& cand) {
    CandidateResult res;
    res.name = name;
    const size_t n = Svals.size();
    res.n = n;
    if (n == 0 || cand.size() != n) return res;

    for (size_t i = 0; i < n; ++i) {
        const double s = Svals[i], c = cand[i];
        if (s == c) ++res.matchS;
        if (s / 2.0 == c) ++res.matchHalf;
        if (s + 1.0 == c) ++res.matchPlus1;
        if (s - 1.0 == c) ++res.matchMinus1;

        const double shuf = Svals[(i + n / 2) % n];
        if (shuf == c) ++res.ctlMatchS;
        if (shuf / 2.0 == c) ++res.ctlMatchHalf;
        if (shuf + 1.0 == c) ++res.ctlMatchPlus1;
        if (shuf - 1.0 == c) ++res.ctlMatchMinus1;
    }
    res.r = pearson(Svals, cand);
    return res;
}

void printCandidateRow(const CandidateResult& r) {
    auto pctOf = [&](long long m) { return r.n ? 100.0 * static_cast<double>(m) / static_cast<double>(r.n) : 0.0; };
    printf("%-40s %7zu  %7.3f%% %7.3f%% %7.3f%% %7.3f%%   r=%+.4f\n", r.name.c_str(), r.n,
           pctOf(r.matchS), pctOf(r.matchHalf), pctOf(r.matchPlus1), pctOf(r.matchMinus1), r.r);
    printf("%-40s %7s  %7.3f%% %7.3f%% %7.3f%% %7.3f%%\n", "  (shuffled-S control)", "",
           pctOf(r.ctlMatchS), pctOf(r.ctlMatchHalf), pctOf(r.ctlMatchPlus1), pctOf(r.ctlMatchMinus1));
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

    printf("\n=== what is S (the per-track duration total), and where is it in the"
           " header? ===\n");

    printf("\n--- population and skips ---\n");
    printf(".anim_pc entries seen (all archives): %lld\n", g_anim_pc_seen);
    printf("  skipped, parse failed: %lld\n", g_skipped_parse_failed);
    printf("  skipped, flags & 0x40 (unaccounted payload, Sec6c.4): %lld\n", g_skipped_extra_payload_flag);
    printf("  skipped, walk incomplete or missed declared end: %lld\n", g_skipped_walk_incomplete_or_off_end);
    printf("clean clips used: %d\n", g_clips);
    printf("tracks used (passed all QC gates): %d\n", g_tracks);
    printf("  tracks skipped, translationKeys == 0: %lld\n", g_skipped_no_keys);
    printf("  tracks skipped, wide (0x20) translation stride (unexercised branch): %lld\n",
           g_skipped_wide_translation_stride);
    printf("  tracks skipped, control-record walk did not cover all keys: %lld\n", g_skipped_control_walk_short);
    printf("  tracks skipped, extra-byte region out of file bounds: %lld\n", g_skipped_oob);
    printf("tracks with translationKeys == 1 (byteSum feeds the pooled S/translationKeys\n"
           "  stat below, but EXCLUDED from a clip's S median - see comment at"
           " g_tracksOneKey): %lld / %d (%.2f%%)\n",
           g_tracksOneKey, g_tracks,
           g_tracks ? 100.0 * static_cast<double>(g_tracksOneKey) / static_cast<double>(g_tracks) : 0.0);
    printf("clips with <2 headline tracks (translationKeys>=2; S not computable here,"
           " excluded entirely, not silently dropped): %lld\n", g_clipsHeadlineLT2);
    printf("clips with >=2 headline tracks (S computed, spread/agreement measurable): %lld\n",
           g_clipsHeadlineGE2);
    printf("clips missing the +0x30 declared end despite landing on it (should be 0): %lld\n",
           g_clipsMissingTrailingOffset);
    printf("clips with usable S (used in every candidate test below): %zu\n", g_S.size());

    printf("\n--- how solid is S? within-clip agreement across headline tracks"
           " (translationKeys>=2; median across them IS the S used everywhere below) ---\n");
    printf("clips with >=2 headline tracks: %lld\n", g_clipsHeadlineGE2);
    printf("  agree to within 1%% of S ((max-min)/S <= 0.01): %lld (%.2f%%)\n",
           g_clipsAgreeWithin1pct,
           g_clipsHeadlineGE2 ? 100.0 * static_cast<double>(g_clipsAgreeWithin1pct) /
                                     static_cast<double>(g_clipsHeadlineGE2)
                               : 0.0);
    printf("  relative spread (max-min)/S: median %.4f  p90 %.4f  (n=%zu)\n",
           median(g_relSpread), pct(g_relSpread, 0.90), g_relSpread.size());

    printf("\n--- distribution of S itself (n=%zu clips) ---\n", g_S.size());
    printf("median %.1f   p10 %.1f   p90 %.1f   max %.1f\n",
           median(g_S), pct(g_S, 0.10), pct(g_S, 0.90), pct(g_S, 1.00));
    printf("S == 0 exactly: %lld / %zu (%.2f%%)\n", g_SZeroCount, g_S.size(),
           g_S.size() ? 100.0 * static_cast<double>(g_SZeroCount) / static_cast<double>(g_S.size()) : 0.0);

    printf("\n--- distribution of S / translationKeys (mean bytes per key), pooled over"
           " every qualifying TRACK (n=%zu tracks) ---\n", g_gapPerKey.size());
    printf("(if the byte is a frame count at a fixed rate, this is the average gap"
           " between keys)\n");
    printf("median %.3f   p10 %.3f   p90 %.3f   max %.3f\n",
           median(g_gapPerKey), pct(g_gapPerKey, 0.10), pct(g_gapPerKey, 0.90), pct(g_gapPerKey, 1.00));

    printf("\n--- candidate scoring: S vs every header quantity this reader exposes ---\n");
    printf("each candidate is tested against 4 readings of S (S, S/2, S+1, S-1), with\n"
           "its own shuffled-S control (S taken from a DIFFERENT clip) directly below it.\n"
           "A correlation (r) is reported for context but is NOT evidence of identity -\n"
           "only an exact-match rate far above its shuffled control is.\n\n");
    printf("%-40s %8s  %8s %8s %8s %8s\n", "candidate", "n", "match S", "S/2", "S+1", "S-1");

    printCandidateRow(scoreCandidate("field_0x06 (u16 @ +0x06)", g_S, g_field06));
    printCandidateRow(scoreCandidate("field_0x06 low byte (u8)", g_S, g_field06lo));
    printCandidateRow(scoreCandidate("field_0x06 high byte (u8)", g_S, g_field06hi));
    printCandidateRow(scoreCandidate("field_0x08 (u8 @ +0x08)", g_S, g_field08));
    printCandidateRow(scoreCandidate("field_0x09 (u8 @ +0x09)", g_S, g_field09));
    printf("\n-- near-controls, expected to fail --\n");
    printCandidateRow(scoreCandidate("field_0x0A track count (u8)", g_S, g_field0A));
    printCandidateRow(scoreCandidate("field_0x0B bone count (u8)", g_S, g_field0B));
    printCandidateRow(scoreCandidate("file size (entry bytes)", g_S, g_fileSize));
    printCandidateRow(scoreCandidate("declared payload end (+0x30)", g_S_trailing, g_trailing));

    // Mechanical best-candidate scan restricted to the primary (non-near-
    // control) candidates, over all four S-readings, so the report can point
    // at a single number rather than making the reader hunt the table.
    {
        struct Named { const char* name; CandidateResult r; };
        std::vector<Named> primaries = {
            {"field_0x06", scoreCandidate("field_0x06", g_S, g_field06)},
            {"field_0x06 low byte", scoreCandidate("field_0x06 low byte", g_S, g_field06lo)},
            {"field_0x06 high byte", scoreCandidate("field_0x06 high byte", g_S, g_field06hi)},
            {"field_0x08", scoreCandidate("field_0x08", g_S, g_field08)},
            {"field_0x09", scoreCandidate("field_0x09", g_S, g_field09)},
        };
        double bestRate = -1.0, bestCtlRate = 0.0;
        std::string bestName, bestVariant;
        for (auto& p : primaries) {
            struct V { const char* label; long long m, c; };
            V variants[4] = {
                {"S", p.r.matchS, p.r.ctlMatchS},
                {"S/2", p.r.matchHalf, p.r.ctlMatchHalf},
                {"S+1", p.r.matchPlus1, p.r.ctlMatchPlus1},
                {"S-1", p.r.matchMinus1, p.r.ctlMatchMinus1},
            };
            for (auto& v : variants) {
                const double rate = p.r.n ? 100.0 * static_cast<double>(v.m) / static_cast<double>(p.r.n) : 0.0;
                const double ctlRate = p.r.n ? 100.0 * static_cast<double>(v.c) / static_cast<double>(p.r.n) : 0.0;
                if (rate > bestRate) {
                    bestRate = rate;
                    bestCtlRate = ctlRate;
                    bestName = p.name;
                    bestVariant = v.label;
                }
            }
        }
        printf("\n--- best-scoring primary candidate (mandatory control alongside it) ---\n");
        printf("best: %s, reading %s  -  exact match %.3f%%   shuffled-S control %.3f%%\n",
               bestName.c_str(), bestVariant.c_str(), bestRate, bestCtlRate);
        printf("\nauto-verdict (mechanical threshold on the numbers above, stated so the"
               " logic is auditable - read the table yourself before trusting this line):\n");
        if (bestRate > 20.0 && bestRate > bestCtlRate * 5.0 && bestRate - bestCtlRate > 10.0) {
            printf("  CANDIDATE IDENTIFIED - %s (%s) matches S far above its shuffled"
                   " control.\n", bestName.c_str(), bestVariant.c_str());
        } else {
            printf("  NOT IDENTIFIED - no header field tested reproduces S at a rate"
                   " meaningfully above its own shuffled-S control. The duration total"
                   " is not recorded in any of the fields this reader currently exposes.\n");
        }
    }

    // Diagnostic: field_0x06 scored best above but matched S in well under 100%
    // of clips. S itself is a MEDIAN across headline tracks that only agrees to
    // within 1% in about half of clips (see the agreement figure above) - so a
    // "miss" could mean field_0x06 is wrong, OR it could mean S is simply a
    // noisy stand-in for the true total in that clip. Stratifying the SAME
    // match test by how well the clip's own tracks agreed distinguishes them:
    // if the match rate is dramatically higher in the well-agreeing stratum,
    // the misses in the noisy stratum are S's fault, not field_0x06's.
    printf("\n--- diagnostic: is field_0x06's match rate suppressed by S's own noise? ---\n");
    printf("predicate: same equality test (S == field_0x06), stratified by whether THIS\n"
           "clip's headline tracks agreed to within 1%% ((max-min)/S <= 0.01) or not.\n");
    {
        long long nTight = 0, mTight = 0, nLoose = 0, mLoose = 0;
        const size_t n = g_S.size();
        for (size_t i = 0; i < n; ++i) {
            const bool tight = g_relSpreadAligned[i] <= 0.01;
            const bool match = g_S[i] == g_field06[i];
            if (tight) { ++nTight; if (match) ++mTight; }
            else { ++nLoose; if (match) ++mLoose; }
        }
        printf("  tight-agreement clips (<=1%% spread): %lld / %lld matched (%.2f%%)\n",
               mTight, nTight, nTight ? 100.0 * static_cast<double>(mTight) / static_cast<double>(nTight) : 0.0);
        printf("  loose-agreement clips  (>1%% spread): %lld / %lld matched (%.2f%%)\n",
               mLoose, nLoose, nLoose ? 100.0 * static_cast<double>(mLoose) / static_cast<double>(nLoose) : 0.0);
    }

    printf("\n=== end of measurements. Verdict lines above are mechanical threshold"
           " checks on the printed numbers, not a substitute for reading them. ===\n");
    return 0;
}
