// Does span==0 in an 8-byte translation control record mean legitimate
// padding (skip and keep going, as sr3anim::Payload::walk does), or a
// cursor that has drifted off the real record stream (as a rejecting
// probe implementation assumed)?
//
// probe_anim_span0.cpp already found that the file's own +0x30 oracle
// cannot decide this: the 14 clips carrying a span==0 record all carry
// flags bit 0x40 (sr3anim::kExtraPayloadFlag), and no clip with that bit
// ever lands on +0x30 (spec Sec6c.4) - so "does it land" is silent here
// by construction, not because the skip is wrong.
//
// THIS PROBE asks a different, symmetric question: what do the 8 bytes
// immediately AFTER a span==0 record look like?
//
//   - if span==0 is legitimate and the cursor is still correctly
//     positioned, the next 8 bytes should look like a normal control
//     record - fields inside the distribution real records occupy.
//   - if the cursor has drifted, the next 8 bytes are arbitrary payload
//     content and should NOT look like a normal record.
//
// The instrument (a percentile-bounds "plausibility profile" built from
// real records) is validated BEFORE being trusted: Sec6c.5's own history
// (rotation checks that scored 100% because they could not fail) is the
// standing warning that an unvalidated check is worthless. So this probe
// runs a mandatory gate: build the profile from the CLEAN population only
// (flags bit 0x40 clear, walkComplete() && landedOnDeclaredEnd()), then
// apply it to deliberately WRONG bytes - misaligned reads of real file
// content at +1/+2/+3/+5 bytes from real record positions. Only if the
// profile rejects those at a substantial rate does it get to score the 25
// actual span==0 successors.
//
// Usage: probe_anim_span0_successor <archive.vpp_pc>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3anim/animation.h"
#include "sr3anim/payload.h"
#include "vpp/container.h"

// ---- scaffolding copied from probe_anim_span0.cpp (container/archive walk) ----

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

// ---- the plausibility profile ----

// One decoded 8-byte translation control record: {u16 span, i16 x, i16 y, i16 z}.
struct Rec {
    int32_t span, x, y, z;
};

Rec decodeAt(vpp::ByteView bv, size_t at) {
    Rec r;
    r.span = static_cast<int32_t>(bv.readU16LE(at));
    r.x = static_cast<int32_t>(static_cast<int16_t>(bv.readU16LE(at + 2)));
    r.y = static_cast<int32_t>(static_cast<int16_t>(bv.readU16LE(at + 4)));
    r.z = static_cast<int32_t>(static_cast<int16_t>(bv.readU16LE(at + 6)));
    return r;
}

// shift index -> byte offset used for the Step-3 misalignment control.
constexpr int kShifts[4] = {1, 2, 3, 5};

// Clean-population records (shift 0, i.e. real records) and their
// misaligned siblings at each of the 4 shifts, kept as parallel field
// vectors so percentile() below can work on one field at a time.
std::vector<int32_t> cleanSpan, cleanX, cleanY, cleanZ;
std::vector<int32_t> shiftSpan[4], shiftX[4], shiftY[4], shiftZ[4];

struct Successor {
    std::string clip;
    int trackIdx, recIdx;
    Rec rec;
    bool flag40, lands;
};
std::vector<Successor> g_successors;
std::set<std::string> g_span0Clips;

int g_clipsParsed = 0, g_clipsWalked = 0;
int g_cleanClips = 0, g_flag40ClipsWalked = 0;
long long g_cleanRecords = 0;

void doClip(const std::string& name, const std::vector<uint8_t>& raw) {
    vpp::ByteView bv(raw.data(), raw.size());
    sr3anim::Animation a;
    try { a = sr3anim::Animation::parse(bv); } catch (...) { return; }
    ++g_clipsParsed;

    sr3anim::Payload pl = sr3anim::Payload::walk(bv, a);
    if (!pl.walkComplete()) return;
    ++g_clipsWalked;

    const bool flag40 = (a.flags() & sr3anim::kExtraPayloadFlag) != 0;
    const bool lands = pl.landedOnDeclaredEnd();
    const bool clean = !flag40 && lands;
    if (clean) ++g_cleanClips;
    if (flag40) ++g_flag40ClipsWalked;

    bool clipHasSpan0 = false;

    for (size_t ti = 0; ti < pl.tracks().size(); ++ti) {
        const auto& blk = pl.tracks()[ti];
        if (blk.translationControlStride != 8) continue;

        for (size_t r = 0; r < blk.translationControlCount; ++r) {
            const size_t at = blk.translationControlOffset + r * 8;
            if (at + 8 > bv.size()) break;
            const Rec rec = decodeAt(bv, at);

            if (clean) {
                ++g_cleanRecords;
                cleanSpan.push_back(rec.span);
                cleanX.push_back(rec.x);
                cleanY.push_back(rec.y);
                cleanZ.push_back(rec.z);

                for (int si = 0; si < 4; ++si) {
                    const size_t sat = at + static_cast<size_t>(kShifts[si]);
                    if (sat + 8 <= bv.size()) {
                        const Rec s = decodeAt(bv, sat);
                        shiftSpan[si].push_back(s.span);
                        shiftX[si].push_back(s.x);
                        shiftY[si].push_back(s.y);
                        shiftZ[si].push_back(s.z);
                    }
                }
            }

            if (rec.span == 0) {
                clipHasSpan0 = true;
                const size_t succAt = at + 8;
                if (succAt + 8 <= bv.size()) {
                    Successor s;
                    s.clip = name;
                    s.trackIdx = static_cast<int>(ti);
                    s.recIdx = static_cast<int>(r);
                    s.rec = decodeAt(bv, succAt);
                    s.flag40 = flag40;
                    s.lands = lands;
                    g_successors.push_back(s);
                }
            }
        }
    }

    if (clipHasSpan0) g_span0Clips.insert(name);
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

// Linear-interpolated percentile (numpy 'linear' convention) over a copy
// of the data (sorted in place inside the copy).
double percentile(std::vector<int32_t> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double idx = (p / 100.0) * static_cast<double>(v.size() - 1);
    size_t lo = static_cast<size_t>(std::floor(idx));
    size_t hi = static_cast<size_t>(std::ceil(idx));
    if (hi >= v.size()) hi = v.size() - 1;
    const double frac = idx - static_cast<double>(lo);
    return static_cast<double>(v[lo]) * (1.0 - frac) + static_cast<double>(v[hi]) * frac;
}

// ---- profile bounds, filled in main() after the clean population is known ----
double g_spanLo, g_spanHi, g_xLo, g_xHi, g_yLo, g_yHi, g_zLo, g_zHi;

bool accept(const Rec& r) {
    return r.span >= g_spanLo && r.span <= g_spanHi &&
           r.x >= g_xLo && r.x <= g_xHi &&
           r.y >= g_yLo && r.y <= g_yHi &&
           r.z >= g_zLo && r.z <= g_zHi;
}

// count how many of N parallel-array records pass accept(); returns {accepted, N}.
std::pair<long long, long long> scoreParallel(const std::vector<int32_t>& span,
                                               const std::vector<int32_t>& x,
                                               const std::vector<int32_t>& y,
                                               const std::vector<int32_t>& z) {
    long long acc = 0;
    for (size_t i = 0; i < span.size(); ++i) {
        Rec r{span[i], x[i], y[i], z[i]};
        if (accept(r)) ++acc;
    }
    return {acc, static_cast<long long>(span.size())};
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

    printf("\n=== span==0 successor plausibility probe ===\n");
    printf("clips parsed                      : %d\n", g_clipsParsed);
    printf("clips whose walk completed        : %d\n", g_clipsWalked);
    printf("  clean (flags&0x40==0 && lands)   : %d\n", g_cleanClips);
    printf("  flags&0x40 set                   : %d\n", g_flag40ClipsWalked);
    printf("clips containing a span==0 record  : %zu\n", g_span0Clips.size());
    printf("span==0 records with a usable successor (the 25): %zu\n", g_successors.size());

    printf("\n--- STEP 1: what a failing case looks like ---\n");
    printf("If span==0 marks a drifted cursor, the 8 bytes read as the next\n");
    printf("record are actually mid-payload content (sample deltas, a\n");
    printf("neighboring track's header, etc), not a control record. Concretely\n");
    printf("the profile below must REJECT: a span field outside the range real\n");
    printf("spans occupy (e.g. implausibly large run lengths, or negative-\n");
    printf("looking values from misread high bits), and/or an x/y/z base that\n");
    printf("falls outside the range real bone-local translations occupy (real\n");
    printf("bases cluster tightly per Sec6c.5's evidence table; a misaligned\n");
    printf("read historically produces medians ~50-80x larger). A profile that\n");
    printf("accepts arbitrary byte content regardless of these fields is\n");
    printf("vacuous, which is exactly what Step 3 checks for.\n");

    if (g_cleanRecords < 100) {
        printf("\nToo few clean records (%lld) to build a profile. Stopping.\n", g_cleanRecords);
        return 0;
    }

    printf("\n--- STEP 2: profile bounds, derived from the clean population ---\n");
    g_spanLo = percentile(cleanSpan, 1);   g_spanHi = percentile(cleanSpan, 99);
    g_xLo = percentile(cleanX, 1);         g_xHi = percentile(cleanX, 99);
    g_yLo = percentile(cleanY, 1);         g_yHi = percentile(cleanY, 99);
    g_zLo = percentile(cleanZ, 1);         g_zHi = percentile(cleanZ, 99);
    printf("N (real 8-byte control records, clean population) = %lld\n", g_cleanRecords);
    printf("  span [p1,p99] = [%.2f, %.2f]\n", g_spanLo, g_spanHi);
    printf("  x    [p1,p99] = [%.2f, %.2f]\n", g_xLo, g_xHi);
    printf("  y    [p1,p99] = [%.2f, %.2f]\n", g_yLo, g_yHi);
    printf("  z    [p1,p99] = [%.2f, %.2f]\n", g_zLo, g_zHi);
    printf("accept(rec) = span in bounds AND x in bounds AND y in bounds AND z in bounds\n");

    printf("\n--- STEP 3: validate the profile against MISALIGNED (wrong) bytes ---\n");
    printf("(8 bytes read at +1/+2/+3/+5 from each real clean-population record position)\n");
    auto [baseAcc, baseN] = scoreParallel(cleanSpan, cleanX, cleanY, cleanZ);
    const double baselineRate = baseN ? 100.0 * static_cast<double>(baseAcc) / static_cast<double>(baseN) : 0.0;
    printf("  baseline (real records, shift 0) : %lld / %lld accepted = %.2f%%\n", baseAcc, baseN, baselineRate);

    double shiftRates[4];
    long long shiftAcc[4], shiftN[4];
    for (int si = 0; si < 4; ++si) {
        auto [acc, n] = scoreParallel(shiftSpan[si], shiftX[si], shiftY[si], shiftZ[si]);
        shiftAcc[si] = acc; shiftN[si] = n;
        shiftRates[si] = n ? 100.0 * static_cast<double>(acc) / static_cast<double>(n) : 0.0;
        printf("  shift +%d bytes                   : %lld / %lld accepted = %.2f%%\n",
               kShifts[si], acc, n, shiftRates[si]);
    }
    double maxShiftRate = *std::max_element(shiftRates, shiftRates + 4);
    double avgShiftRate = (shiftRates[0] + shiftRates[1] + shiftRates[2] + shiftRates[3]) / 4.0;
    printf("  max shift acceptance = %.2f%%, avg shift acceptance = %.2f%%\n", maxShiftRate, avgShiftRate);

    if (maxShiftRate > 50.0) {
        printf("\n*** GATE FAILED ***\n");
        printf("At least one misaligned shift is accepted at >50%% (%.2f%%). The\n", maxShiftRate);
        printf("profile CANNOT DISCRIMINATE a real record from arbitrary nearby\n");
        printf("payload bytes. Per the mandatory gate, this probe STOPS here and\n");
        printf("reports NO conclusion about the 25 span==0 successors: doing so\n");
        printf("would reproduce the exact vacuous-check failure mode documented in\n");
        printf("include/sr3anim/payload.h (the rotation-check history) in a new\n");
        printf("costume. The 25 successors were collected (N=%zu) but are\n", g_successors.size());
        printf("deliberately NOT scored against an instrument that has just failed\n");
        printf("its own validation.\n");
        return 0;
    }

    printf("\nGate passed: misaligned bytes are rejected at a substantial rate\n");
    printf("(max shift acceptance %.2f%%, baseline %.2f%%). Proceeding to Step 4.\n",
           maxShiftRate, baselineRate);

    printf("\n--- STEP 4: score the %zu real span==0 successors ---\n", g_successors.size());
    // Four MUTUALLY EXCLUSIVE per-record categories, in this priority order:
    //   TAUTOLOGICAL - the successor itself has span==0. The profile can
    //     NEVER accept this (the clean population it was built from has
    //     zero span==0 records, so span==0 is below its own p1 bound by
    //     construction). Rejecting it is not evidence of drift - it is a
    //     structural blind spot of the instrument and must not be counted
    //     as drift.
    //   DRIFT       - span != 0 but outside the profile's [p1,p99] span
    //     bound entirely (not merely a borderline x/y/z miss) - the
    //     unambiguous "this is not a control record" signature.
    //   ACCEPTED    - passes the full profile (span AND x AND y AND z all
    //     in bounds).
    //   OTHER_REJECT- span in bounds but x/y/z is not. Not seen in this
    //     run but handled rather than silently mis-bucketed if it occurs.
    long long succAccepted = 0;
    struct ClipStats { int examined = 0, drift = 0, accepted = 0, tautological = 0, otherReject = 0; };
    std::map<std::string, ClipStats> perClip;
    for (const auto& s : g_successors) {
        const bool spanInBounds = s.rec.span >= g_spanLo && s.rec.span <= g_spanHi;
        const bool ok = accept(s.rec);
        const char* category;
        auto& cs = perClip[s.clip];
        ++cs.examined;
        if (s.rec.span == 0) { category = "tautological"; ++cs.tautological; }
        else if (!spanInBounds) { category = "DRIFT"; ++cs.drift; }
        else if (ok) { category = "ACCEPT"; ++cs.accepted; ++succAccepted; }
        else { category = "other-reject"; ++cs.otherReject; }
        printf("  %-42s track=%2d rec=%3d -> succ{span=%6d x=%6d y=%6d z=%6d} flags40=%d lands=%d : %s\n",
               s.clip.c_str(), s.trackIdx, s.recIdx, s.rec.span, s.rec.x, s.rec.y, s.rec.z,
               s.flag40 ? 1 : 0, s.lands ? 1 : 0, category);
    }
    const long long succN = static_cast<long long>(g_successors.size());
    const double succRate = succN ? 100.0 * static_cast<double>(succAccepted) / static_cast<double>(succN) : 0.0;
    printf("\nsuccessors accepted (full profile pass) : %lld / %lld = %.2f%%\n", succAccepted, succN, succRate);
    printf("(baseline for real records, for comparison            : %.2f%%)\n", baselineRate);
    printf("(max misaligned-shift acceptance, for comparison       : %.2f%%)\n", maxShiftRate);

    // Per-clip verdict, per the rule: ANY unambiguous-drift successor
    // invalidates the whole clip's walk, regardless of how many other
    // successors in the same clip look fine. Tautological records never
    // count as drift and never count as clean on their own.
    printf("\n--- PER-CLIP VERDICT TABLE (all %zu clips containing a span==0 record) ---\n", g_span0Clips.size());
    printf("Rule: DRIFT if >=1 unambiguous-drift successor (overrides any ACCEPT in the\n");
    printf("same clip); CLEAN if >=1 accepted successor AND zero drift; UNSCORABLE if\n");
    printf("every successor in the clip is tautological (profile has no verdict at all).\n\n");
    printf("  %-42s %9s %7s %9s %13s  %s\n",
           "clip", "examined", "drift", "accepted", "tautological", "VERDICT");
    int nDrift = 0, nClean = 0, nUnscorable = 0, nOther = 0, nBothAcceptAndDrift = 0;
    std::vector<std::string> bothList;
    for (const auto& kv : perClip) {
        const ClipStats& cs = kv.second;
        const char* verdict;
        if (cs.drift >= 1) {
            verdict = "DRIFT";
            ++nDrift;
            if (cs.accepted >= 1) { ++nBothAcceptAndDrift; bothList.push_back(kv.first); }
        } else if (cs.accepted >= 1) {
            verdict = "CLEAN";
            ++nClean;
        } else if (cs.tautological == cs.examined) {
            verdict = "UNSCORABLE";
            ++nUnscorable;
        } else {
            verdict = "AMBIGUOUS(other-reject only)";
            ++nOther;
        }
        printf("  %-42s %9d %7d %9d %13d  %s\n",
               kv.first.c_str(), cs.examined, cs.drift, cs.accepted, cs.tautological, verdict);
    }
    printf("\nBucket totals over %zu clips: DRIFT=%d  CLEAN=%d  UNSCORABLE=%d  other=%d\n",
           g_span0Clips.size(), nDrift, nClean, nUnscorable, nOther);
    printf("Clips with BOTH an accepted successor and a drift successor (drift wins the\n");
    printf("verdict per the stated rule, but the coexistence is reported explicitly so it\n");
    printf("is not averaged away): %d\n", nBothAcceptAndDrift);
    for (const auto& c : bothList) printf("  - %s\n", c.c_str());

    printf("\n--- READING THE OUTCOME (n=%lld successors, %zu clips - SMALL samples, do not over-read) ---\n",
           succN, g_span0Clips.size());
    printf("mostly ACCEPTED (near baseline %.2f%%)      => span==0 is legitimate; skip is right\n", baselineRate);
    printf("mostly REJECTED (near shift rate ~%.2f%%)    => cursor drifted; the rejecting probe was right\n", avgShiftRate);
    printf("SPLIT into two groups                        => both readings right for different clips (see verdict table)\n");
    return 0;
}
