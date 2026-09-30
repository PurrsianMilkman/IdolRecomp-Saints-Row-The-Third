// What does flags bit 0x40 add to the .anim_pc payload?
//
// The last open piece of the keyframe payload. 518 of 4,209 clips carry the
// bit; NONE of them land on the declared end at +0x30 after walking the
// +0x0A declared tracks, and 121 fail the walk outright. Team A reported
// that continuing the SAME per-track walk past the declared count lands
// exactly on +0x30 for 334 of the 397 that walk short - so "there is more
// payload, and it parses as more tracks" - but the extra block count
// matched no header field they tested (39 blocks for 310 clips; otherwise
// 74, 113, 90, ...).
//
// This probe attacks the count from the data side now that a verified walk
// exists. It asks two questions and reports the AMBIGUITY for both:
//
//   1. How many extra blocks does each clip need, and is that number
//      predictable from anything in the header?
//   2. Is "extra blocks" even the right frame? A trailing region that
//      merely happens to parse as blocks would also land on +0x30 if the
//      walk is self-terminating, so the alternative - a fixed-size trailer
//      whose length is a function of the track count - is scored on the
//      same population rather than assumed away.
//
// Every candidate is scored against the oracle the file declares. Nothing
// here adopts a reading; it reports what discriminates and what does not.
//
// Usage: probe_anim_flag40 <archive.vpp_pc>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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

size_t alignUp2(size_t p) { return (p + 1) & ~static_cast<size_t>(1); }
size_t alignUp4(size_t p) { return (p + 3) & ~static_cast<size_t>(3); }

// The verified Sec6c.1 walk, but walking `tracks` blocks rather than the
// declared count, so the caller can ask "how many blocks reach the end?".
// Returns the end cursor or 0.
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

int g_withBit = 0, g_declaredLands = 0, g_someNLands = 0, g_neverLands = 0;
long long g_ambiguityTotal = 0;
std::map<int, int> g_extraHist;          // extra blocks -> clips
std::map<std::string, int> g_predictor;  // candidate header field -> hits
int g_scored = 0;
int g_thirdPop = 0, g_outrightFail = 0;
int g_trivial = 0, g_trivialLanded = 0, g_trivialCont = 0, g_trivialThird = 0;
int g_walkDisagree = 0;
int g_ctlTried = 0, g_ctlFound = 0;
long long g_ctlHitsTotal = 0;
// TEAM A'S EXACT ANCHOR, added 2026-09-12 after they handed over their
// harness's predicate directly. Their shift is applied AFTER a CORRECT,
// unshifted walk of the declared `tc` blocks - i.e. to end_of_declared_walk
// - and only the CONTINUATION is searched from there. The control above
// shifts the PAYLOAD START by +7 instead, which corrupts the declared
// portion too. These are different perturbations; scored separately rather
// than assumed equivalent.
int g_ctlB_tried = 0, g_ctlB_found = 0;
long long g_ctlB_hitsTotal = 0;
std::map<int, int> g_ctlB_extraHist;
// DESYNC RATE, added 2026-09-12 per Team A's proposed fast diagnostic: of
// the 512 candidate block-counts tried per clip, what fraction return a
// NONZERO cursor (walk completed without running past the buffer), vs
// silently failing? Their hypothesis: a walker that almost never fails even
// from a garbage-aligned start will land on ANY fixed target from nearly
// anywhere by pure combinatorics over a wide-enough search - which would
// explain both this file's flat shift-response AND the original ambiguity
// finding, independent of the anchor question above.
long long g_desyncReal_total = 0, g_desyncReal_ok = 0;
long long g_desyncA_total = 0, g_desyncA_ok = 0;    // CTL-A: payload-start shift
long long g_desyncB_total = 0, g_desyncB_ok = 0;    // CTL-B: Team A's anchor
int g_ctlCompletesAnyCount = 0; // Team A's guess #2: does "hit" mean
                                // "lands exactly on target" (strict, what
                                // this file has always tested) or "the walk
                                // merely completes for SOME count" (weak)?
                                // If the weak predicate saturates under
                                // shift while the strict one does not, that
                                // is the whole 333-vs-37 discrepancy and
                                // this file's own refutation stands as
                                // measured - it was testing the strict
                                // predicate the whole time.
std::map<int, int> g_ctlExtraHist;
int g_trnLand = 0, g_trnThird = 0, g_trnFail = 0;
bool hasTrn(const std::string& n) { return n.find("trn") != std::string::npos; }
// Root-motion rotation angle in degrees, from the header quaternion at
// +0x0C. Turns the filename correlation into a FORMAT-level property:
// if the third population really is turn-in-place animation, its root
// rotation should be large where the others are not.
std::vector<double> g_rotLand, g_rotThird, g_rotFail;
double rootAngleDeg(const sr3anim::Animation& a) {
    double w = a.rootRotation().w;
    if (w > 1.0) w = 1.0;
    if (w < -1.0) w = -1.0;
    return 2.0 * std::acos(std::fabs(w)) * 180.0 / 3.14159265358979323846;
}
std::map<size_t, int> g_thirdTracks, g_thirdBones, g_failTracks, g_landTracks;
std::vector<std::string> g_thirdExamples;
std::vector<std::string> g_examples;

void doClip(const std::string& name, const std::vector<uint8_t>& raw) {
    vpp::ByteView bv(raw.data(), raw.size());
    sr3anim::Animation a;
    try { a = sr3anim::Animation::parse(bv); } catch (...) { return; }
    if ((a.flags() & sr3anim::kExtraPayloadFlag) == 0) return;
    if (!a.hasTrailingOffset()) return;
    ++g_withBit;

    const uint8_t f = a.flags();
    const size_t tc = a.field_0x0A_rawCount();
    const size_t bones = a.field_0x0B_rawCount();
    const size_t headerSize = (f & 0x10) ? 0x48 : 0x38;
    const size_t tbl = a.hasTrackBoneTable() ? a.trackBoneTableOffset() : 0;
    const size_t start = tbl ? tbl + tc : headerSize;
    const uint32_t target = a.trailingOffset();

    // TRIVIALITY CHECK (raised by Team A, 2026-09-12). A payload whose
    // blocks are ALL EMPTY satisfies "the walk completed" by construction:
    // every offset is correct because nothing had to be located. Such a
    // clip lands in whichever bucket the zeros happen to satisfy, not the
    // bucket it belongs in. "Walks cleanly" and "fails the walk" are
    // exactly the two predicates a zero-filled payload cannot distinguish.
    // So count keys, and report the split with and without these.
    bool trivial = false;
    {
        sr3anim::Payload pl = sr3anim::Payload::walk(bv, a);
        if (pl.walkComplete()) {
            long long keys = 0;
            for (const auto& b : pl.tracks())
                keys += static_cast<long long>(b.rotationKeys) + b.translationKeys;
            trivial = (keys == 0);
        }
    }
    if (trivial) ++g_trivial;

    const size_t declaredEnd = walkN(raw, start, f, tc);
    if (declaredEnd == target) {
        ++g_declaredLands;
        if (trivial) ++g_trivialLanded;
        return;
    }

    // TEAM A'S EXACT CONTROL DESIGN (2026-09-12): shift applied to
    // end_of_declared_walk, not to the payload start, and only run when the
    // declared walk itself parsed cleanly (declaredEnd != 0) - shifting a
    // walk that never produced a real anchor would not be their test either.
    if (declaredEnd != 0) {
        ++g_ctlB_tried;
        int hitsB = 0;
        size_t firstB = 0;
        for (size_t nExtra = 1; nExtra <= 512; ++nExtra) {
            const size_t end = walkN(raw, declaredEnd + 7, f, nExtra);
            ++g_desyncB_total;
            if (end != 0) ++g_desyncB_ok;
            if (end == target) {
                if (hitsB == 0) firstB = nExtra;
                ++hitsB;
                if (hitsB > 4) break;
            }
        }
        if (hitsB > 0) {
            ++g_ctlB_found;
            g_ctlB_hitsTotal += hitsB;
            g_ctlB_extraHist[static_cast<int>(firstB)] += 1;
        }
    }

    // How many blocks DO reach the declared end? Report every N that works,
    // not just the first - a search that cannot say how many answers it
    // found has not located anything (HANDOFF Sec3).
    // SHIFT CONTROL (Team A's caution, 2026-09-12 - the right one to raise).
    // The search below tries 512 candidate block counts. "Exactly one count
    // works" only means LOCATION if a WRONG start does NOT also produce hits
    // at a comparable rate: with a candidate space this wide, a unique fit
    // can be a property of the search rather than of the data. So run the
    // identical continuation search from a deliberately wrong start (+7
    // bytes - the displacement the spec's own walk control uses) and report
    // its hit rate and ambiguity alongside the real one.
    {
        int ctlHits = 0;
        size_t ctlFirst = 0;
        bool ctlCompletesAny = false; // WEAKER predicate: walk merely completes
        for (size_t n = tc + 1; n <= tc + 512; ++n) {
            const size_t end = walkN(raw, start + 7, f, n);
            ++g_desyncA_total;
            if (end != 0) { ++g_desyncA_ok; ctlCompletesAny = true; } // completes, regardless of target
            if (end == target) {
                if (ctlHits == 0) ctlFirst = n;
                ++ctlHits;
            }
            if (ctlHits > 4) break;
        }
        ++g_ctlTried;
        if (ctlCompletesAny) ++g_ctlCompletesAnyCount;
        if (ctlHits > 0) {
            ++g_ctlFound;
            g_ctlHitsTotal += ctlHits;
            // Histogram the CONTROL's extra-block counts too. The real
            // search's "extra = 39 in 310 clips" was published as a finding.
            // If a WRONG start clusters at the same value, that clustering
            // is a property of the search geometry, not of the data -
            // a region defined by the thing being tested cannot test it.
            g_ctlExtraHist[static_cast<int>(ctlFirst - tc)] += 1;
        }
    }

    std::vector<size_t> hits;
    for (size_t n = tc + 1; n <= tc + 512; ++n) {
        const size_t end = walkN(raw, start, f, n);
        ++g_desyncReal_total;
        if (end != 0) ++g_desyncReal_ok;
        if (end == target) hits.push_back(n);
        if (hits.size() > 4) break;
    }
    if (hits.empty()) {
        ++g_neverLands;
        // THE THIRD POPULATION. 190 clips never land, but only ~121 fail
        // the declared walk outright - so ~69 walk short AND are not
        // explained by continuation. Split them here, and measure the same
        // properties on BOTH sides, because a trait shared by every failure
        // means nothing until the landing set is shown to lack it.
        // CORRECTED 2026-09-12. This previously asked walkN() - this file's
        // OWN copy of the walk - whether the declared walk completes. An
        // independent census using the SHIPPING reader
        // (sr3anim::Payload::walk) disagreed on the totals, so the two
        // implementations do not agree on every clip. The shipping reader is
        // the authority; a probe's private copy is not. Count the
        // disagreement explicitly rather than silently preferring one.
        sr3anim::Payload plW = sr3anim::Payload::walk(bv, a);
        const bool walksDeclared = plW.walkComplete();
        const bool walkNSays = walkN(raw, start, f, tc) != 0;
        if (walksDeclared != walkNSays) ++g_walkDisagree;
        if (walksDeclared) {
            ++g_thirdPop;
            if (trivial) ++g_trivialThird;
            if (hasTrn(name)) ++g_trnThird;
            g_rotThird.push_back(rootAngleDeg(a));
            g_thirdTracks[tc] += 1;
            g_thirdBones[bones] += 1;
            if (g_thirdExamples.size() < 10) {
                char buf[200];
                snprintf(buf, sizeof buf, "%-36s tracks=%2zu bones=%2zu flags=0x%02X",
                         name.c_str(), tc, bones, f);
                g_thirdExamples.push_back(buf);
            }
        } else {
            ++g_outrightFail;
            if (hasTrn(name)) ++g_trnFail;
            g_rotFail.push_back(rootAngleDeg(a));
            g_failTracks[tc] += 1;
        }
        return;
    }
    ++g_someNLands;
    g_ambiguityTotal += static_cast<long long>(hits.size());

    const size_t n = hits.front();
    const int extra = static_cast<int>(n - tc);
    g_extraHist[extra] += 1;
    g_landTracks[tc] += 1;
    if (trivial) ++g_trivialCont;
    if (hasTrn(name)) ++g_trnLand;
    g_rotLand.push_back(rootAngleDeg(a));
    ++g_scored;

    // Is the extra count predictable from anything the header states?
    // Scored as candidates, with the population reported, so a hit rate is
    // readable rather than a bare claim.
    if (extra == static_cast<int>(bones)) g_predictor["== +0x0B (rig bone count)"] += 1;
    if (extra == static_cast<int>(bones) - static_cast<int>(tc))
        g_predictor["== bones - tracks (unanimated bones)"] += 1;
    if (extra == static_cast<int>(tc)) g_predictor["== +0x0A (track count)"] += 1;
    if (extra == static_cast<int>(a.field_0x06_rawCount())) g_predictor["== +0x06"] += 1;
    if (extra == static_cast<int>(a.field_0x08_rawCount())) g_predictor["== +0x08"] += 1;
    if (extra == static_cast<int>(a.field_0x09_rawCount())) g_predictor["== +0x09"] += 1;

    if (g_examples.size() < 10) {
        char buf[200];
        snprintf(buf, sizeof buf,
                 "%-34s tracks=%2zu bones=%2zu extra=%3d (bones-tracks=%3d) hits=%zu",
                 name.c_str(), tc, bones, extra,
                 static_cast<int>(bones) - static_cast<int>(tc), hits.size());
        g_examples.push_back(buf);
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
        if (b.empty()) continue;
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            walkArchive(c);
        } catch (const std::exception& e) { printf("%s\n", e.what()); }
        printf("scanned %s\n", argv[i]);
    }

    printf("\n=== flags bit 0x40: what does it add? ===\n");
    printf("clips carrying the bit        : %d\n", g_withBit);
    // TERMINOLOGY, deliberately explicit (Team A's rule, 2026-09-12): the
    // bare verb "land" was doing double duty here - "reaches +0x30 walking
    // the DECLARED track count" (true for 0/518) and "reaches +0x30 when the
    // walk CONTINUES past it" (true for 328/518). Same word, two predicates,
    // opposite answers. Every label below names which one.
    printf("  reach +0x30 at DECLARED count : %d   (expected 0)\n", g_declaredLands);
    printf("  reach +0x30 by CONTINUING     : %d\n", g_someNLands);
    printf("  reach +0x30 at NO count <=+512: %d\n", g_neverLands);
    printf("  mean working block counts   : %.2f   (ambiguity; 1.00 = unique)\n",
           g_someNLands ? static_cast<double>(g_ambiguityTotal) / g_someNLands : 0.0);

    printf("\nSHIFT CONTROL - identical search from a WRONG start (+7 bytes).\n");
    printf("The candidate space is 512 counts wide, so a unique fit proves\n");
    printf("nothing unless a wrong start fails to find one.\n");
    printf("  clips searched from wrong start   : %d\n", g_ctlTried);
    printf("  ...that found ANY working count   : %d  (%.2f%%)\n", g_ctlFound,
           g_ctlTried ? 100.0 * g_ctlFound / g_ctlTried : 0.0);
    printf("  ...their mean working-count count : %.2f\n",
           g_ctlFound ? static_cast<double>(g_ctlHitsTotal) / g_ctlFound : 0.0);
    printf("  REAL start found a count in       : %d  (%.2f%%)\n", g_someNLands,
           g_ctlTried ? 100.0 * g_someNLands / g_ctlTried : 0.0);
    printf("  => if these two rates are close, ambiguity-1 is a property of\n");
    printf("     the SEARCH and the continuation result is unsupported.\n");

    printf("\nTEAM A'S EXACT ANCHOR (2026-09-12): shift +7 applied to\n");
    printf("end_of_declared_walk (correct tc-block walk, then shifted), NOT to\n");
    printf("the payload start. Only run where the declared walk itself parses\n");
    printf("(declaredEnd != 0). This is the direct, same-population comparison\n");
    printf("to their reported 334/397 real vs 37/397 shifted:\n");
    printf("  clips with a valid declaredEnd to shift  : %d\n", g_ctlB_tried);
    printf("  ...that found ANY working count (strict) : %d  (%.2f%%)\n", g_ctlB_found,
           g_ctlB_tried ? 100.0 * g_ctlB_found / g_ctlB_tried : 0.0);
    printf("  ...their mean working-count count        : %.2f\n",
           g_ctlB_found ? static_cast<double>(g_ctlB_hitsTotal) / g_ctlB_found : 0.0);
    printf("  REAL start found a count in              : %d  (%.2f%%)\n", g_someNLands,
           g_ctlB_tried ? 100.0 * g_someNLands / g_ctlB_tried : 0.0);
    printf("  => if this control's rate is now well below the real rate (unlike\n");
    printf("     the payload-start control above), the anchor WAS the answer.\n");

    printf("\nDESYNC RATE (Team A's proposed fast diagnostic, 2026-09-12): of the\n");
    printf("512 candidate block-counts tried per clip, what fraction return a\n");
    printf("NONZERO cursor (walk completed in-bounds) rather than failing\n");
    printf("outright? Their hypothesis: a walker that almost never fails even\n");
    printf("from a garbage-aligned start will land on a fixed target from\n");
    printf("nearly anywhere by pure combinatorics over a wide search.\n");
    printf("  REAL start          : %lld / %lld completed  (%.2f%%)\n",
           g_desyncReal_ok, g_desyncReal_total,
           g_desyncReal_total ? 100.0 * g_desyncReal_ok / g_desyncReal_total : 0.0);
    printf("  CTL-A (payload-start shift, wrong anchor) : %lld / %lld  (%.2f%%)\n",
           g_desyncA_ok, g_desyncA_total,
           g_desyncA_total ? 100.0 * g_desyncA_ok / g_desyncA_total : 0.0);
    printf("  CTL-B (Team A's exact anchor)             : %lld / %lld  (%.2f%%)\n",
           g_desyncB_ok, g_desyncB_total,
           g_desyncB_total ? 100.0 * g_desyncB_ok / g_desyncB_total : 0.0);
    printf("  => if CTL-A's completion rate stays high (near REAL) while\n");
    printf("     CTL-B's drops, that is the mechanism: CTL-A rarely fails even\n");
    printf("     misaligned, so its wide search finds SOME landing almost\n");
    printf("     anywhere; CTL-B desyncs hard, so its search mostly finds none.\n");

    printf("\nPREDICATE CHECK (Team A's guess #2, 2026-09-12): this file has\n");
    printf("ALWAYS scored a hit as landing EXACTLY on the target (u32 == +0x30),\n");
    printf("the STRICT predicate. Their figure may instead count 'walk merely\n");
    printf("completes for SOME count', a WEAKER predicate. Both measured on the\n");
    printf("SAME shifted (+7) runs so this is a direct comparison, not a guess:\n");
    printf("  wrong-start STRICT (lands on target)   : %d / %d  (%.2f%%)\n", g_ctlFound,
           g_ctlTried, g_ctlTried ? 100.0 * g_ctlFound / g_ctlTried : 0.0);
    printf("  wrong-start WEAK (completes, any count) : %d / %d  (%.2f%%)\n",
           g_ctlCompletesAnyCount, g_ctlTried,
           g_ctlTried ? 100.0 * g_ctlCompletesAnyCount / g_ctlTried : 0.0);
    printf("  => if WEAK saturates near 100%% while STRICT does not, the two\n");
    printf("     control runs were never measuring the same event, and THIS\n");
    printf("     file's refutation (strict) stands as measured either way.\n");

    printf("\nAND THE SAME CONTROL, APPLIED TO THE EXTRA-BLOCK HISTOGRAM.\n");
    printf("'extra = 39 in 310 clips' was published as a finding, but the\n");
    printf("extra count is the FIRST WORKING COUNT of that same search. If a\n");
    printf("WRONG start clusters at the same value, the clustering is search\n");
    printf("geometry, not data: a region defined by the thing under test\n");
    printf("cannot test it. CTL-A is the payload-start shift (this file's\n");
    printf("original, now known to be the WRONG anchor); CTL-B is Team A's\n");
    printf("exact anchor (shift applied after the real declared walk).\n\n");
    printf("  %-14s %10s %10s %10s\n", "extra blocks", "REAL", "CTL-A", "CTL-B");
    {
        std::map<int, int> keys;
        for (const auto& kv : g_extraHist) keys[kv.first] = 0;
        for (const auto& kv : g_ctlExtraHist) keys[kv.first] = 0;
        for (const auto& kv : g_ctlB_extraHist) keys[kv.first] = 0;
        std::vector<std::pair<int, int>> byReal;
        for (const auto& kv : keys) {
            const int re = g_extraHist.count(kv.first) ? g_extraHist[kv.first] : 0;
            const int ce = g_ctlExtraHist.count(kv.first) ? g_ctlExtraHist[kv.first] : 0;
            const int cb = g_ctlB_extraHist.count(kv.first) ? g_ctlB_extraHist[kv.first] : 0;
            byReal.push_back({re + ce + cb, kv.first});
        }
        std::sort(byReal.rbegin(), byReal.rend());
        for (size_t i = 0; i < byReal.size() && i < 8; ++i) {
            const int k = byReal[i].second;
            printf("  %-14d %10d %10d %10d\n", k,
                   g_extraHist.count(k) ? g_extraHist[k] : 0,
                   g_ctlExtraHist.count(k) ? g_ctlExtraHist[k] : 0,
                   g_ctlB_extraHist.count(k) ? g_ctlB_extraHist[k] : 0);
        }
        printf("  distinct values: REAL %zu, CTL-A %zu, CTL-B %zu\n", g_extraHist.size(),
               g_ctlExtraHist.size(), g_ctlB_extraHist.size());
    }

    printf("\nEXTRA BLOCK COUNT - distribution (top 12):\n");
    std::vector<std::pair<int, int>> h;
    for (const auto& kv : g_extraHist) h.push_back({kv.second, kv.first});
    std::sort(h.rbegin(), h.rend());
    for (size_t i = 0; i < h.size() && i < 12; ++i)
        printf("  extra = %4d : %4d clips\n", h[i].second, h[i].first);
    printf("  distinct extra values: %zu over %d scored clips\n", g_extraHist.size(), g_scored);

    printf("\nIS THE EXTRA COUNT PREDICTABLE FROM A HEADER FIELD?\n");
    if (g_predictor.empty()) {
        printf("  no candidate matched even once\n");
    } else {
        for (const auto& kv : g_predictor)
            printf("  %-40s %4d / %d  (%.1f%%)\n", kv.first.c_str(), kv.second, g_scored,
                   g_scored ? 100.0 * kv.second / g_scored : 0.0);
    }

    printf("\nexamples:\n");
    for (const auto& s : g_examples) printf("  %s\n", s.c_str());

    printf("\n=== TRIVIALITY CHECK: clips whose walk had nothing to get wrong ===\n");
    printf("A payload of ALL-EMPTY blocks satisfies the walk by construction,\n");
    printf("so it lands in whichever bucket the zeros happen to satisfy.\n");
    printf("  clips with the bit and ZERO keys total : %d / %d\n", g_trivial, g_withBit);
    printf("    ...reached +0x30 at DECLARED count    : %d\n", g_trivialLanded);
    printf("    ...reached +0x30 by CONTINUING        : %d\n", g_trivialCont);
    printf("    ...of those, in the third population  : %d\n", g_trivialThird);
    printf("  => informative population (non-trivial) : %d\n", g_withBit - g_trivial);

    printf("\n=== THE THIRD POPULATION ===\n");
    printf("Clips reaching +0x30 at NO count, split by whether the DECLARED\n");
    printf("count walk even\n");
    printf("completes. The second group is the one nobody has named.\n");
    printf("  walkN vs shipping reader disagree: %d   <- SHOULD BE 0\n", g_walkDisagree);
    printf("  fail the declared walk outright : %d\n", g_outrightFail);
    printf("  walk OK, reach +0x30 at no count : %d  <- third population\n", g_thirdPop);

    // Measure track count on ALL THREE groups, not just the odd one out.
    // A trait shared by every member of a failing set is not a cause until
    // the succeeding set is shown to lack it (HANDOFF Sec3).
    auto top = [](const char* label, const std::map<size_t, int>& m, int n) {
        std::vector<std::pair<int, size_t>> v;
        for (const auto& kv : m) v.push_back({kv.second, kv.first});
        std::sort(v.rbegin(), v.rend());
        printf("  %-22s", label);
        for (int i = 0; i < n && i < static_cast<int>(v.size()); ++i)
            printf(" %zu(x%d)", v[static_cast<size_t>(i)].second, v[static_cast<size_t>(i)].first);
        printf("   [%zu distinct]\n", m.size());
    };
    printf("\ntrack counts, most common first - compared across all three:\n");
    top("reached-by-continuing", g_landTracks, 5);
    top("third population", g_thirdTracks, 5);
    top("outright failures", g_failTracks, 5);

    printf("\nthird-population examples:\n");
    for (const auto& s : g_thirdExamples) printf("  %s\n", s.c_str());

    // The third population's names look overwhelmingly like turn-in-place
    // animations ("trn90_l", "trn_lft", ...). That is exactly the kind of
    // story that explains a sample and predicts nothing, so it gets scored
    // on ALL THREE populations rather than asserted from the examples. If
    // "trn" is common everywhere it discriminates nothing.
    printf("\nNAME SUBSTRING \"trn\" - scored on all three populations:\n");
    printf("  %-24s %5d / %-5d (%5.1f%%)\n", "reached-by-continuing", g_trnLand, g_scored,
           g_scored ? 100.0 * g_trnLand / g_scored : 0.0);
    printf("  %-24s %5d / %-5d (%5.1f%%)\n", "third population", g_trnThird, g_thirdPop,
           g_thirdPop ? 100.0 * g_trnThird / g_thirdPop : 0.0);
    printf("  %-24s %5d / %-5d (%5.1f%%)\n", "outright failures", g_trnFail, g_outrightFail,
           g_outrightFail ? 100.0 * g_trnFail / g_outrightFail : 0.0);

    // A filename correlation is evidence about what these clips ARE, not
    // about the format. Root-motion rotation is a FORMAT-level property, so
    // it tests the same hypothesis against bytes rather than labels.
    printf("\nROOT-MOTION ROTATION ANGLE (header +0x0C) - all three:\n");
    auto stat = [](const char* label, std::vector<double>& v) {
        if (v.empty()) { printf("  %-24s (none)\n", label); return; }
        std::sort(v.begin(), v.end());
        const double med = v[v.size() / 2];
        const double p90 = v[static_cast<size_t>(0.90 * (v.size() - 1))];
        size_t big = 0;
        for (double d : v) if (d > 45.0) ++big;
        printf("  %-24s median %6.2f deg   p90 %7.2f   >45deg %4zu/%-5zu (%5.1f%%)\n", label,
               med, p90, big, v.size(), 100.0 * static_cast<double>(big) / v.size());
    };
    stat("reached-by-continuing", g_rotLand);
    stat("third population", g_rotThird);
    stat("outright failures", g_rotFail);
    return 0;
}
