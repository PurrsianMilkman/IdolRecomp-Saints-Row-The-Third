// Two implementations of the documented Sec6c.1 keyframe walk disagree on
// exactly some clips out of the 518 that carry flags bit 0x40 and have a
// trailing (+0x30) offset. Nobody knew which clips or why. This probe finds
// out.
//
//   IMPLEMENTATION A (the authority): sr3anim::Payload::walk(), shipped in
//   src/payload.cpp. Called for real via the linked library - never
//   reimplemented here.
//
//   IMPLEMENTATION B (a probe's private copy): walkN(), copied VERBATIM
//   below from tools/validation/probe_anim_flag40.cpp. It is called with
//   tracks == a.field_0x0A_rawCount(), i.e. the same declared track count A
//   uses internally, so the two calls are meant to be equivalent.
//
// Where they disagree, that disagreement is a direct readout of the gap
// between what someone THOUGHT the walk rule was (B) and what the shipped
// reader actually does (A). This probe:
//
//   1. runs A and B on every qualifying clip and counts agree/disagree,
//   2. for every disagreeing clip, finds the FIRST TRACK INDEX at which the
//      two implementations' behavior actually diverges (by construction,
//      every track before that one is processed identically by both - the
//      only code differences between A and B live INSIDE one track's
//      control-record loops, so if they ever disagree overall, there must
//      be a first track where one of them stops and the other does not),
//   3. reports, per clip, which side said "complete" and which said
//      "failed", the failing side's exact reason (which bounds check, or
//      which of B's extra rules - the span==0 early return or the 200000
//      iteration cap - fired), and the raw numbers needed to check the
//      claim by hand.
//
// Nothing here adopts a reading or fixes either implementation. It measures.
//
// Usage: probe_anim_walk_divergence <archive.vpp_pc>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "sr3anim/animation.h"
#include "sr3anim/payload.h"
#include "vpp/container.h"

// --------------------------------------------------------------------------
// Container / file plumbing. Independent of, and not copied from, any other
// tools/validation file - this is just walking a .vpp_pc via the public
// vpp::Container API to find .anim_pc leaf entries, recursing into nested
// containers the same way every other probe in this tree does.
// --------------------------------------------------------------------------

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

// --------------------------------------------------------------------------
// IMPLEMENTATION B, verbatim. Copied byte-for-byte from
// tools/validation/probe_anim_flag40.cpp (walkN, plus its two small helpers
// alignUp2/alignUp4). NOT modified in any way - this is the exact function
// under test, private copy and all.
// --------------------------------------------------------------------------

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

// --------------------------------------------------------------------------
// Diagnostic instrumentation. NOT part of "implementation B under test" -
// walkN() above is what is actually scored. This is a per-track breakdown of
// the SAME algorithm as walkN, decomposed so the probe can report which
// track index and which specific rule caused a stop, instead of only the
// final yes/no. Every arithmetic step matches walkN exactly; the only
// addition is bookkeeping. A self-check against the verbatim walkN (below,
// in doClip) confirms this decomposition agrees with it on every clip.
// --------------------------------------------------------------------------

struct TrackTrace {
    size_t enterCursor = 0;
    uint32_t rotKeys = 0;
    uint32_t transKeys = 0;
    size_t exitCursor = 0;   // valid only if ok
    bool ok = false;
    std::string reason;      // set only if !ok
};

// One track of implementation B's walk, traced. Mirrors walkN's loop body
// for a single iteration of `t`.
TrackTrace stepTrackB(const std::vector<uint8_t>& b, size_t p, uint8_t flags) {
    TrackTrace tr;
    tr.enterCursor = p;
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

    uint32_t rotKeys = 0, transKeys = 0;
    if (!wideCounts) {
        if (!need(p, 2)) { tr.reason = "bounds:counts(u8)"; return tr; }
        rotKeys = b[p]; transKeys = b[p + 1]; p += 2;
    } else {
        p = alignUp2(p);
        if (!need(p, 4)) { tr.reason = "bounds:counts(u16,aligned)"; return tr; }
        rotKeys = u16(p); transKeys = u16(p + 2); p += 4;
    }
    tr.rotKeys = rotKeys;
    tr.transKeys = transKeys;

    size_t n = 0; uint64_t acc = 0;
    while (acc < rotKeys) {
        if (!need(p + n * 4, 4)) { tr.reason = "bounds:rotctl"; return tr; }
        acc += 1u + (b[p + n * 4 + 3] & 0x3Fu);
        if (++n > 200000) { tr.reason = "cap:rotctl(n>200000)"; return tr; }
    }
    p += n * 4;
    if (!need(p, static_cast<size_t>(rotKeys) * 3)) { tr.reason = "bounds:rotsample"; return tr; }
    p += static_cast<size_t>(rotKeys) * 3;

    size_t m = 0; acc = 0;
    if (!wideTrans) {
        p = alignUp2(p);
        while (acc < transKeys) {
            if (!need(p + m * 8, 8)) { tr.reason = "bounds:transctl8"; return tr; }
            const uint32_t span = u16(p + m * 8);
            if (span == 0) { tr.reason = "span==0:transctl8"; return tr; }
            acc += span;
            if (++m > 200000) { tr.reason = "cap:transctl8(m>200000)"; return tr; }
        }
        p += m * 8;
    } else {
        p = alignUp4(p);
        while (acc < transKeys) {
            if (!need(p + m * 16, 16)) { tr.reason = "bounds:transctl16"; return tr; }
            const int32_t span = i32(p + m * 16);
            if (span <= 0) { tr.reason = "span<=0:transctl16"; return tr; }
            acc += static_cast<uint64_t>(span);
            if (++m > 200000) { tr.reason = "cap:transctl16(m>200000)"; return tr; }
        }
        p += m * 16;
    }
    if (!need(p, static_cast<size_t>(transKeys) * 4)) { tr.reason = "bounds:transsample"; return tr; }
    p += static_cast<size_t>(transKeys) * 4;

    tr.ok = true;
    tr.exitCursor = p;
    return tr;
}

// Runs B track-by-track up to `tracks` iterations, stopping at the first
// failure. Returns how many tracks it fully completed (== tracks if it went
// all the way through) and, if it stopped short, the trace of the track
// that stopped it.
struct BWalkResult {
    size_t completedTracks = 0;
    bool complete = false;
    TrackTrace failingTrack; // valid only if !complete
    std::vector<TrackTrace> traces; // one entry per track attempted (completed or failing)
};

BWalkResult walkBTraced(const std::vector<uint8_t>& b, size_t p, uint8_t flags, size_t tracks) {
    BWalkResult r;
    for (size_t t = 0; t < tracks; ++t) {
        TrackTrace tr = stepTrackB(b, p, flags);
        r.traces.push_back(tr);
        if (!tr.ok) {
            r.failingTrack = tr;
            r.completedTracks = t;
            r.complete = false;
            return r;
        }
        p = tr.exitCursor;
        r.completedTracks = t + 1;
    }
    r.complete = true;
    return r;
}

// --------------------------------------------------------------------------
// Diagnostic-only re-derivation of implementation A's per-track bounds
// failure SITE, for the case where A is the side that fails. This is not
// the authority (sr3anim::Payload::walk, called for real, is) - it exists
// only so a disagreeing clip's report can say WHICH of A's four `return
// out` sites fired, something Payload's public API does not expose. Every
// check mirrors src/payload.cpp's per-track body exactly (same offsets,
// same order, NO span==0 check, NO iteration cap - that absence is the
// point being measured).
// --------------------------------------------------------------------------

TrackTrace stepTrackA(const std::vector<uint8_t>& b, size_t p, uint8_t flags) {
    TrackTrace tr;
    tr.enterCursor = p;
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

    uint32_t rotKeys = 0, transKeys = 0;
    if (!wideCounts) {
        if (!need(p, 2)) { tr.reason = "bounds:counts(u8)"; return tr; }
        rotKeys = b[p]; transKeys = b[p + 1]; p += 2;
    } else {
        p = alignUp2(p);
        if (!need(p, 4)) { tr.reason = "bounds:counts(u16,aligned)"; return tr; }
        rotKeys = u16(p); transKeys = u16(p + 2); p += 4;
    }
    tr.rotKeys = rotKeys;
    tr.transKeys = transKeys;

    size_t n = 0; uint64_t acc = 0;
    while (acc < rotKeys) {
        if (!need(p + n * 4, 4)) { tr.reason = "bounds:rotctl"; return tr; }
        acc += 1u + (b[p + n * 4 + 3] & 0x3Fu);
        ++n; // NO cap in A
    }
    p += n * 4;
    if (!need(p, static_cast<size_t>(rotKeys) * 3)) { tr.reason = "bounds:rotsample"; return tr; }
    p += static_cast<size_t>(rotKeys) * 3;

    size_t m = 0; acc = 0;
    if (!wideTrans) {
        p = alignUp2(p);
        while (acc < transKeys) {
            if (!need(p + m * 8, 8)) { tr.reason = "bounds:transctl8"; return tr; }
            acc += u16(p + m * 8); // NO span==0 check in A
            ++m; // NO cap in A
        }
        p += m * 8;
    } else {
        p = alignUp4(p);
        while (acc < transKeys) {
            if (!need(p + m * 16, 16)) { tr.reason = "bounds:transctl16"; return tr; }
            const int32_t span = i32(p + m * 16);
            if (span <= 0) { tr.reason = "span<=0:transctl16"; return tr; } // A DOES check this in the wide branch
            acc += static_cast<uint64_t>(span);
            ++m; // NO cap in A
        }
        p += m * 16;
    }
    if (!need(p, static_cast<size_t>(transKeys) * 4)) { tr.reason = "bounds:transsample"; return tr; }
    p += static_cast<size_t>(transKeys) * 4;

    tr.ok = true;
    tr.exitCursor = p;
    return tr;
}

// --------------------------------------------------------------------------
// Per-clip comparison.
// --------------------------------------------------------------------------

struct Disagreement {
    std::string name;
    uint8_t flags = 0;
    size_t declaredTracks = 0;
    size_t bones = 0;
    size_t fileSize = 0;
    size_t payloadStart = 0;
    uint32_t trackBoneTableOffset = 0;
    uint32_t declaredEnd = 0;
    bool aComplete = false;
    bool bComplete = false;
    size_t aCompletedTracks = 0;
    size_t bCompletedTracks = 0;
    size_t divergeIdx = 0;
    size_t enterCursorA = 0, enterCursorB = 0;
    uint32_t divergeRotKeys = 0, divergeTransKeys = 0;
    bool haveDivergeCounts = false;
    std::string aOutcomeAtDiverge;  // "completed (exit=...)" or a stepTrackA reason
    std::string bOutcomeAtDiverge;  // "completed (exit=...)" or a stepTrackB reason
    std::string cause;              // classification bucket
    bool selfCheckOk = true;        // walkBTraced agrees with verbatim walkN
};

int g_considered = 0;
int g_agree = 0, g_disagree = 0;
int g_aCompleteBOnly = 0; // A says complete, B says failed
int g_bCompleteAOnly = 0; // B says complete, A says failed (the "reverse" direction)
std::vector<Disagreement> g_diffs;

void doClip(const std::string& name, const std::vector<uint8_t>& raw) {
    vpp::ByteView bv(raw.data(), raw.size());
    sr3anim::Animation a;
    try { a = sr3anim::Animation::parse(bv); } catch (...) { return; }
    if ((a.flags() & sr3anim::kExtraPayloadFlag) == 0) return;
    if (!a.hasTrailingOffset()) return;
    ++g_considered;

    const uint8_t f = a.flags();
    const size_t tc = a.field_0x0A_rawCount();
    const size_t bones = a.field_0x0B_rawCount();
    const uint32_t tbl = a.hasTrackBoneTable() ? a.trackBoneTableOffset() : 0;
    // Same formula as sr3anim::Payload::walk (src/payload.cpp lines ~60-70):
    // table present -> table offset + one byte per track; else the header
    // size, gated by the 0x10 long-header flag.
    const size_t start = tbl ? static_cast<size_t>(tbl) + tc
                              : (a.hasOptionalSection() ? 0x48 : 0x38);

    // --- Implementation A, run for real. ---
    sr3anim::Payload plA = sr3anim::Payload::walk(bv, a);
    const bool aComplete = plA.walkComplete();
    const size_t aCompletedTracks = plA.tracks().size();

    // --- Implementation B, run for real (verbatim walkN) with tracks == tc. ---
    const size_t bEnd = walkN(raw, start, f, tc);
    const bool bComplete = (bEnd != 0);

    if (aComplete == bComplete) { ++g_agree; return; }
    ++g_disagree;
    if (aComplete && !bComplete) ++g_aCompleteBOnly;
    if (bComplete && !aComplete) ++g_bCompleteAOnly;

    Disagreement d;
    d.name = name;
    d.flags = f;
    d.declaredTracks = tc;
    d.bones = bones;
    d.fileSize = raw.size();
    d.payloadStart = start;
    d.trackBoneTableOffset = tbl;
    d.declaredEnd = a.trailingOffset();
    d.aComplete = aComplete;
    d.bComplete = bComplete;
    d.aCompletedTracks = aCompletedTracks;

    // Traced B walk, to find exactly where it stopped (or confirm it went
    // all the way through when B is the side that "completed").
    BWalkResult br = walkBTraced(raw, start, f, tc);
    d.bCompletedTracks = br.completedTracks;
    d.selfCheckOk = (br.complete == bComplete);

    d.divergeIdx = std::min(d.aCompletedTracks, d.bCompletedTracks);

    // Cursor entering the divergent track, from each side independently.
    if (d.divergeIdx > 0) {
        d.enterCursorA = plA.tracks()[d.divergeIdx - 1].blockEnd;
        d.enterCursorB = br.traces[d.divergeIdx - 1].exitCursor;
    } else {
        d.enterCursorA = start;
        d.enterCursorB = start;
    }

    // rotKeys/transKeys for the divergent track: pull from whichever side
    // actually completed the read of those two counts. If A completed the
    // track outright, its TrackBlock has them; otherwise fall back to B's
    // trace entry for that track (present whether or not B's OWN processing
    // of the track succeeded, since the counts are read before any of the
    // divergence points).
    if (d.aCompletedTracks > d.divergeIdx) {
        d.divergeRotKeys = plA.tracks()[d.divergeIdx].rotationKeys;
        d.divergeTransKeys = plA.tracks()[d.divergeIdx].translationKeys;
        d.haveDivergeCounts = true;
        char buf[96];
        snprintf(buf, sizeof buf, "completed track (exit=%zu)",
                 static_cast<size_t>(plA.tracks()[d.divergeIdx].blockEnd));
        d.aOutcomeAtDiverge = buf;
    } else {
        // A is the side that failed at this track.
        TrackTrace tA = stepTrackA(raw, d.enterCursorA, f);
        d.divergeRotKeys = tA.rotKeys;
        d.divergeTransKeys = tA.transKeys;
        d.haveDivergeCounts = true;
        d.aOutcomeAtDiverge = "FAILED: " + tA.reason;
    }

    if (d.bCompletedTracks > d.divergeIdx) {
        char buf[96];
        snprintf(buf, sizeof buf, "completed track (exit=%zu)",
                 static_cast<size_t>(br.traces[d.divergeIdx].exitCursor));
        d.bOutcomeAtDiverge = buf;
        if (!d.haveDivergeCounts) {
            d.divergeRotKeys = br.traces[d.divergeIdx].rotKeys;
            d.divergeTransKeys = br.traces[d.divergeIdx].transKeys;
            d.haveDivergeCounts = true;
        }
    } else {
        // B is the side that failed at this track (br.failingTrack is it,
        // since divergeIdx == br.completedTracks in that case).
        d.bOutcomeAtDiverge = "FAILED: " + br.failingTrack.reason;
        if (!d.haveDivergeCounts) {
            d.divergeRotKeys = br.failingTrack.rotKeys;
            d.divergeTransKeys = br.failingTrack.transKeys;
            d.haveDivergeCounts = true;
        }
    }

    // Classify the cause from whichever side's reason string names a rule
    // that differs between A and B (span==0 early return, or an iteration
    // cap). If the failing side's reason is a plain bounds check, and nothing
    // marks a differing rule, say so explicitly rather than guessing.
    const std::string& failReason = d.aComplete ? br.failingTrack.reason
                                                 : (d.bComplete ? std::string() : std::string());
    std::string reasonToClassify;
    if (d.aComplete && !d.bComplete) reasonToClassify = br.failingTrack.reason;
    else if (d.bComplete && !d.aComplete) {
        // B succeeded further than A - re-derive A's failure reason at the
        // track where A actually stopped (== d.divergeIdx, since aComplete
        // is false here, aCompletedTracks == divergeIdx).
        TrackTrace tA = stepTrackA(raw, d.enterCursorA, f);
        reasonToClassify = "A:" + tA.reason;
    }
    (void)failReason;

    if (reasonToClassify.find("span==0:transctl8") != std::string::npos) {
        d.cause = "B's translation-span==0 early return (A has no such check, keeps going)";
    } else if (reasonToClassify.find("cap:") != std::string::npos) {
        d.cause = "B's 200000-iteration cap (A has no cap)";
    } else if (reasonToClassify.rfind("A:", 0) == 0) {
        d.cause = "A bounds-failed where B's own logic let it continue past the same point (" + reasonToClassify + ")";
    } else if (!reasonToClassify.empty()) {
        d.cause = "plain bounds check divergence, not one of the named candidate rules (" + reasonToClassify + ")";
    } else {
        d.cause = "UNKNOWN - could not classify";
    }

    g_diffs.push_back(std::move(d));
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

    printf("\n=== A (Payload::walk) vs B (walkN, tracks=declared) ===\n");
    printf("clips considered (flags&0x40 && hasTrailingOffset) : %d\n", g_considered);
    printf("  agree                                            : %d\n", g_agree);
    printf("  disagree                                          : %d\n", g_disagree);
    printf("    A complete, B failed                            : %d\n", g_aCompleteBOnly);
    printf("    B complete, A failed                             : %d\n", g_bCompleteAOnly);
    if (g_disagree != 14) {
        printf("  NOTE: disagreement count is %d, not the 14 the task named - "
               "reporting the measured value, not the expected one.\n", g_disagree);
    }

    printf("\n=== disagreeing clips (%d) ===\n", static_cast<int>(g_diffs.size()));
    int causeSpan0 = 0, causeCap = 0, causeAFailed = 0, causePlainBounds = 0, causeUnknown = 0;
    int selfCheckFail = 0;
    for (const auto& d : g_diffs) {
        if (!d.selfCheckOk) ++selfCheckFail;
        printf("\n--- %s ---\n", d.name.c_str());
        printf("  flags=0x%02X declaredTracks(+0x0A)=%zu bones(+0x0B)=%zu fileSize=%zu\n",
               d.flags, d.declaredTracks, d.bones, d.fileSize);
        printf("  payloadStart=%zu trackBoneTableOffset=%u declaredEnd(+0x30)=%u\n",
               d.payloadStart, d.trackBoneTableOffset, d.declaredEnd);
        printf("  A (Payload::walk)  : %s  (completed %zu/%zu tracks)\n",
               d.aComplete ? "COMPLETE" : "FAILED", d.aCompletedTracks, d.declaredTracks);
        printf("  B (walkN)          : %s  (completed %zu/%zu tracks)%s\n",
               d.bComplete ? "COMPLETE" : "FAILED", d.bCompletedTracks, d.declaredTracks,
               d.selfCheckOk ? "" : "  [SELF-CHECK MISMATCH: traced B disagrees with verbatim walkN!]");
        printf("  first divergent track index: %zu\n", d.divergeIdx);
        printf("    rotKeys=%u transKeys=%u (as read at that track)\n",
               d.divergeRotKeys, d.divergeTransKeys);
        printf("    cursor entering track, per A: %zu   per B: %zu%s\n",
               d.enterCursorA, d.enterCursorB,
               d.enterCursorA == d.enterCursorB ? "" : "  [DIFFER - see note]");
        printf("    A at this track: %s\n", d.aOutcomeAtDiverge.c_str());
        printf("    B at this track: %s\n", d.bOutcomeAtDiverge.c_str());
        printf("  CAUSE: %s\n", d.cause.c_str());

        if (d.cause.find("span==0") != std::string::npos) ++causeSpan0;
        else if (d.cause.find("200000-iteration cap") != std::string::npos) ++causeCap;
        else if (d.cause.find("A bounds-failed") != std::string::npos) ++causeAFailed;
        else if (d.cause.find("plain bounds check divergence") != std::string::npos) ++causePlainBounds;
        else ++causeUnknown;
    }

    printf("\n=== cause summary over %d disagreeing clips ===\n", static_cast<int>(g_diffs.size()));
    printf("  B's translation span==0 early return (A has no check, keeps going): %d\n", causeSpan0);
    printf("  B's 200000-iteration cap (A has no cap)                          : %d\n", causeCap);
    printf("  A bounds-failed where B's differing logic let it continue        : %d\n", causeAFailed);
    printf("  plain bounds-check divergence, none of the named candidates      : %d\n", causePlainBounds);
    printf("  cause unknown / could not classify                               : %d\n", causeUnknown);
    printf("  (sum should equal disagreeing clip count: %d)\n", static_cast<int>(g_diffs.size()));
    if (selfCheckFail) {
        printf("  WARNING: %d clip(s) where the traced-B decomposition disagreed with "
               "verbatim walkN - treat their divergence-track finding as suspect.\n", selfCheckFail);
    }

    return 0;
}
