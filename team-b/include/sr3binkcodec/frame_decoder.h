#pragma once

// sr3binkcodec - one-plane raster-scan decode driver + dispatch trace.
//
// spec-bink-codec.md Sec5.6 (CONFIRMED, hand-traced provenance): the real
// decode core's own orchestrator (0x1001cb00) calls the per-row-range
// workhorse (0x1001a530) exactly FOUR times per frame, one call per
// plane-or-sub-case - call 1 = luma (Y), call 2 = unresolved (record+0x1c/
// +0x20, "not confirmed to be a plane write at all"), call 3 = chroma-A,
// call 4 = chroma-B. Each call runs the SAME shared per-row loop (Sec5.3's
// 10-way dispatch) over that call's own plane.
//
// SCOPE DECISION, flagged explicitly rather than silently assumed: this
// file decodes ONLY the luma (Y) plane against real frame bytes.
// Call 2's own role is OPEN (Sec5.6/Sec8 item 6a) - not just its pixel
// math, but whether it consumes any bitstream bytes at all, and if so how
// many, before call 3 (chroma-A) begins. Since calls 1-4 are confirmed to
// run SEQUENTIALLY through one continuous compressed byte range, decoding
// chroma-A/chroma-B from this skeleton would require guessing where call
// 2 ends and call 3 begins - exactly the kind of invented offset this
// project's "no invented fixes" rule forbids. decodeLumaPlane() below is
// therefore the only real-data entry point; chromaA/chromaB decode is a
// named, explicit gap, left unimplemented rather than guessed at, until
// call 2's own role (spec-bink-codec.md Sec5.6/Sec8 item 6a) is resolved.
//
// PLANE SUB-CHUNK SPAN FIELD (RESOLVED FOR main_menu_bg.bik SPECIFICALLY,
// 2026-10-09, fix/bink-merge-group-bit-consumption task's own follow-up):
// the SEPARATE uncertainty this comment used to flag here ("does call 1's
// own bitstream begin at byte offset 0 or offset 4 of the per-frame byte
// range?") is now CONFIRMED for luma, FOR THIS ONE FILE. spec-bink-codec.md
// Sec5.3g's "The absolute number" paragraph: Y's own real per-row bitstream
// data starts at byte offset 4 of the frame-0 blob (absolute file offset
// 0x2910 = frame-0-blob-start 0x290c + 4, a 16-byte test vector match
// independently re-confirmed bit-for-bit by tests/real_bink_merge_group_
// trace_test.cpp's own 23-call trace landing LEN=528 at bit 588 exactly).
// What precedes it (blob bytes 0-3) is a 4-byte little-endian field - Y's
// own "sub-chunk span" - CONFIRMED by a SECOND, independent triangulation in
// the same section: chromaA's own real dispatch position (live-measured,
// spec-bink-codec.md Sec5.3e's own CTX_ECX capture, reproduced again in
// Sec5.3g) lands at EXACTLY blob offset 135028 = 0 (Y's own span-field
// position) + 135028 (the raw field value read there) - with NO further
// 4-byte skip on top of that number. Sec5.3e explicitly tried that further
// "+4" skip as a hypothesis ("lands exactly ON their own would-be
// length-prefix dword, one read short of where it should start") and found
// it made chromaA's FFmpeg match rate WORSE (40.5% -> 0.1%), empirically
// refuting it. See readPlaneSubChunkSpan()/nextPlaneSubChunkPosition()
// below for the exact arithmetic this resolves to, and tests/bink_plane_
// subchunk_span_test.cpp for dedicated coverage (both a synthetic
// deterministic check and a real-data check against the confirmed 135028
// figure above).
//
// NOT established for every Bink file, and EMPIRICALLY REFUTED as a
// universal default - this is why decodeLumaPlane() below takes an
// explicit `hasLeadingSubChunkSpan` parameter instead of always skipping (or
// never skipping) 4 bytes. spec-bink-codec.md Sec1.6's own "On-disk layout"
// paragraph draws exactly this file-by-file line: for files with `D10=0`
// ("no audio tracks" - main_menu_bg.bik, 01_crawl.bik, both confirmed),
// frame N's raw blob is cleanly `file_bytes[offs[N]:offs[N+1]]` with no
// extra bytes; for files with `D10>=1` (one or more audio tracks, Sec1.6's
// own named example is `logo_thq_and_voli_hd.bik` - this project's OTHER
// real golden fixture, tests/golden/bink_logo_frame0/frame0_raw.bin) "extra
// per-track header bytes" are inserted before the frame-offset table,
// "breaking this exact formula - not chased further" by that spec pass.
// This task tried the unconditional-skip hypothesis against the logo
// fixture anyway (the only other real sample available) specifically to
// check whether the SAME per-plane span-field convention also happens to
// hold there despite the unresolved D10 complication, and found a clear
// EMPIRICAL NEGATIVE: with the skip applied, decode desyncs immediately
// (BlockOutcome::OutOfRangeDispatch at block (0,0), raw dispatch nibble 10,
// down from 418/419 blocks reconstructed on the pre-this-task code).
// Per this project's "no invented fixes" rule, that negative result is
// reported as a Team A spec-sync question (does logo_thq_and_voli_hd.bik's
// own already-extracted frame0_raw.bin carry this same per-plane span-field
// convention, a different one, or none at all, given its own D10>=1
// per-track header bytes ahead of the frame-offset table itself?) rather
// than silently forcing either skip=true (confirmed correct for
// main_menu_bg, confirmed WRONG for logo) or skip=false (the reverse) as a
// hardcoded, unstated default. Every real-data call site below states its
// own stance explicitly, backed by which of Sec1.6's two D10 cases its own
// input file is confirmed to be - see decodeLumaPlane()'s own doc comment.
//
// SEPARATELY NOT resolved, flagged rather than assumed: whether this SAME
// convention (a 4-byte span field, skip 4 to reach data, position+rawValue
// to reach the next plane's own span field), for whichever files it DOES
// apply to, also applies uniformly to every later plane transition
// (chromaA->chromaB, and beyond) - Sec5.3g's own triangulation only
// exercises the Y->chromaA transition specifically. This file still only
// decodes luma (see the SCOPE DECISION above, unchanged - call 2's own
// role, Sec5.6/Sec8 item 6a, is still OPEN), so no later transition is
// exercised by any code here yet.
//
// STAGE-BY-STAGE REAL-DATA VALIDATION (2026-10-08, fix/bink-decoder-
// test-vector-validation task, UPDATED 2026-10-08 by fix/bink-entropy-
// prestage, UPDATED AGAIN 2026-10-08 by fix/bink-entropy-full-algorithm,
// UPDATED AGAIN 2026-10-09 by fix/bink-merge-group-bit-consumption's own
// follow-up task - the plane-sub-chunk-span fix this file now carries):
// tests/real_bink_test_vector_validation_test.cpp runs this exact pipeline
// against real main_menu_bg.bik frame 0/1 bytes and compares against
// spec-bink-video-test-vectors.md's own Unicorn-CPU-emulation capture of
// binkw32.dll's real decode, stage by stage. decodeLumaPlane() below
// constructs a real entropy_prestage.h CaseSelectorScratchSource and
// threads it through every block's primary dispatch read (see
// block_dispatch.h's own "ENTROPY PRE-STAGE WIRING" note) - that source now
// implements spec-bink-codec.md Sec5.3g's FULL algorithm, AND (as of this
// task) that test now passes decodeLumaPlane() `hasLeadingSubChunkSpan=true`
// for main_menu_bg.bik (CONFIRMED correct for this D10=0 file - see the
// PLANE SUB-CHUNK SPAN FIELD note above), landing the entropy pre-stage's
// own 23-call table-build sequence on the CONFIRMED-correct bit position
// instead of desyncing from the first call. Result: main_menu_bg.bik frame 0
// no longer immediately hits CaseSelectorPoisoned on block (0,0), AND its
// own first real dispatch byte now byte-exactly MATCHES the real captured
// case_selector_scratch_frame0.bin ground truth (both are 1) - the decode
// then stops one block later for an entirely separate, pre-existing reason
// (primary=1/SubDispatch's own sub-type nibble, read directly off `br`
// rather than through the case-selector source, comes back out of its
// CONFIRMED 0-6 range at this exact real position - a downstream gap this
// task did not touch) - see tests/real_bink_test_vector_validation_test.cpp's
// own STAGE 1 output for the real block-for-block result this now produces.
//
// Primary cases 5/7 (the only ones carrying DC/AC coefficients) are STILL
// OpenHook for an entirely separate, still-open reason (the DC "+128" bug,
// spec-bink-codec.md Sec5.3d/Sec5.3e - explicitly out of scope for both the
// entropy pre-stage task and this one), so this decoder's own trace still
// stops at the first case-5/7 block it reaches - see that test's own report
// for the real reconstructed-block count this now achieves.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sr3binkcodec/bitstream_reader.h"
#include "sr3binkcodec/block_dispatch.h"
#include "sr3binkcodec/entropy_prestage.h"
#include "sr3binkcodec/planar_buffer.h"

namespace sr3binkcodec {

// spec-bink-codec.md Sec5.3g ("The absolute number" paragraph): one plane's
// own 4-byte little-endian "sub-chunk span" field, read at that plane's own
// sub-chunk-start position (a cold/byte-aligned bitstream position - Sec5.3g
// confirms "the bit-reader's cursor is exactly byte- and word-aligned at
// this exact point, with zero bits pre-cached"). `rawFieldValue` is the
// field exactly as stored (e.g. 135028 for main_menu_bg.bik frame 0's own
// Y-plane field, bytes `74 0f 02 00`); `payloadSpan` is `rawFieldValue - 4`,
// i.e. just this plane's own compressed-payload byte count (not counting
// the 4-byte field itself) - see this file's own top comment for the full
// citation trail, including the chromaA-position triangulation that pins
// down the arithmetic below.
struct PlaneSubChunkSpan {
    uint32_t rawFieldValue = 0;
    uint32_t payloadSpan = 0;
};

// Reads one plane's own 4-byte sub-chunk span field at `br`'s CURRENT
// byte-aligned position and leaves `br` positioned at that SAME plane's own
// real entropy-data start (field position + 4) - this is the fix
// decodeLumaPlane() below applies for luma (call it once, before
// constructing the CaseSelectorScratchSource / calling
// decodePlaneRasterOrder()). `br` must be freshly constructed / otherwise
// known to be byte-aligned with nothing buffered yet (readBits(32) on a
// cold BitstreamReader reads exactly the next 4 raw bytes LE, matching the
// "zero bits pre-cached" cold-start Sec5.3g confirms at this exact point).
inline PlaneSubChunkSpan readPlaneSubChunkSpan(BitstreamReader& br) {
    const uint32_t raw = br.readBits(32);
    PlaneSubChunkSpan span;
    span.rawFieldValue = raw;
    span.payloadSpan = (raw >= 4) ? (raw - 4) : 0;
    return span;
}

// Given the byte position (relative to the frame's own raw blob, or any
// other consistent origin) where a plane's own span field was just read
// from, returns the byte position of the NEXT plane's own span field -
// i.e. this plane's own span-field position + 4 (skip this plane's own
// field) + payloadSpan (skip this plane's own compressed payload) - which
// is algebraically identical to `thisPlaneSubChunkPosition +
// span.rawFieldValue` (4 + (rawFieldValue-4) == rawFieldValue), the form
// spec-bink-codec.md Sec5.3g's own chromaA triangulation measures directly
// (Y's span-field position 0 + raw field value 135028 == chromaA's own
// CONFIRMED real dispatch position 135028, with no further "+4" on top -
// see this file's own top comment for the refuted "+4 patch" hypothesis
// that this specifically rules out). NOT confirmed to generalize past this
// one transition (Y->chromaA) - see this file's own top comment.
inline uint64_t nextPlaneSubChunkPosition(uint64_t thisPlaneSubChunkPosition, const PlaneSubChunkSpan& span) {
    return thisPlaneSubChunkPosition + 4 + span.payloadSpan;
}

struct DispatchCount {
    PrimaryBlockType primary;
    std::optional<Case1SubType> sub;
    BlockOutcome outcome = BlockOutcome::Reconstructed;
    uint32_t rawPrimaryNibble = 0;  // meaningful (and distinguishing) only when outcome == OutOfRangeDispatch
    uint64_t count = 0;
};

struct DecodeTrace {
    std::vector<BlockRecord> blocks;  // one entry per block actually attempted
    StopReason stopReason = StopReason::None;
    int blockCols = 0;
    int blockRows = 0;
    int totalBlocksInGrid = 0;

    uint64_t reconstructedCount() const { return countWhere(BlockOutcome::Reconstructed); }
    uint64_t openHookCount() const { return countWhere(BlockOutcome::OpenHook); }
    uint64_t outOfRangeCount() const { return countWhere(BlockOutcome::OutOfRangeDispatch); }
    uint64_t preStageOpenCount() const { return countWhere(BlockOutcome::PreStageOpen); }
    uint64_t caseSelectorPoisonedCount() const { return countWhere(BlockOutcome::CaseSelectorPoisoned); }

    uint64_t countWhere(BlockOutcome o) const {
        uint64_t n = 0;
        for (const auto& b : blocks) {
            if (b.outcome == o) ++n;
        }
        return n;
    }

    // Aggregated per-(primary,sub) histogram, in first-seen order - the
    // "which block-type dispatch path(s) did it hit" report this task asks
    // for. Also keyed on outcome+rawPrimaryNibble: a BlockOutcome::
    // OutOfRangeDispatch record's own `primary` field is only ever its
    // default-constructed value (PrimaryBlockType::BlockCopy - the real
    // nibble was never in 0-9, so it was never assigned, see BlockRecord's
    // own doc comment) - without this, every out-of-range block, regardless
    // of its real raw value, would silently collapse into one histogram row
    // misleadingly printed as "primary=0". Found during this project's own
    // real-data FFmpeg validation pass (2026-10-08).
    std::vector<DispatchCount> histogram() const {
        std::vector<DispatchCount> out;
        for (const auto& b : blocks) {
            bool found = false;
            for (auto& h : out) {
                if (h.primary == b.primary && h.sub == b.sub && h.outcome == b.outcome &&
                    (b.outcome != BlockOutcome::OutOfRangeDispatch || h.rawPrimaryNibble == b.rawPrimaryNibble)) {
                    ++h.count;
                    found = true;
                    break;
                }
            }
            if (!found) out.push_back(DispatchCount{b.primary, b.sub, b.outcome, b.rawPrimaryNibble, 1});
        }
        return out;
    }

    std::string stopReasonText() const {
        switch (stopReason) {
            case StopReason::None:
                return "in progress";
            case StopReason::CompletedAllBlocks:
                return "completed all blocks in the grid";
            case StopReason::HitOpenHook:
                return "stopped at a block whose pixel math is an OPEN spec gap";
            case StopReason::HitOutOfRangeDispatch:
                return "stopped: a dispatch value failed its own CONFIRMED range check "
                       "(0-9 primary / 0-6 case-1-sub) - likely a bitstream alignment problem, "
                       "not a new case body";
            case StopReason::SourceExhausted:
                return "stopped: ran off the end of the real input bytes";
            case StopReason::HitPreStageOpen:
                return "stopped: the case-selector entropy pre-stage (entropy_prestage.h) hit its own "
                       "documented OPEN gap before producing a dispatch byte - see that file's own top "
                       "comment";
            case StopReason::HitCaseSelectorPoison:
                return "stopped: the case-selector entropy pre-stage hit its own CONFIRMED permanent "
                       "poison state (entropy_prestage.h's CaseSelectorStatus::Poisoned, spec-bink-codec.md "
                       "Sec5.3g) - a real decode outcome (decoded LEN==0, or ran off the real bitstream "
                       "mid-expansion), not an implementation gap";
        }
        return "unknown";
    }
};

// ENTROPY PRE-STAGE ROW SEQUENCING (2026-10-08, fix/bink-entropy-full-
// algorithm task, spec-bink-codec.md Sec5.3g): the pre-stage's own table
// rebuild (0x10018ca0, 23 calls - entropy_prestage.h's own
// kSiblingTableBuildsPerBlockRow) happens ONCE PER 8-PIXEL BLOCK-ROW,
// UNCONDITIONALLY - even on rows where the pre-stage's own idempotency
// guard means the LEN/mode-bit/expansion refill itself ends up a no-op
// (most rows; a genuine refill happens roughly every 3-4 rows per Sec5.3f's
// own telemetry). This is why beginBlockRow() is called once per `row`
// iteration here, OUTSIDE the `col` loop - calling it per-BLOCK (the prior
// pass's own structure, before Sec5.3g's call-granularity finding) would
// pay that 23-call bit cost 30x too often for a 240-column-wide frame,
// desyncing the shared bit cursor from the very first row.
inline DecodeTrace decodePlaneRasterOrder(BitstreamReader& br, PlaneView dest, const PlaneView* prev, int blockCols,
                                           int blockRows, CaseSelectorScratchSource* caseSelector = nullptr) {
    DecodeTrace trace;
    trace.blockCols = blockCols;
    trace.blockRows = blockRows;
    trace.totalBlocksInGrid = blockCols * blockRows;

    for (int row = 0; row < blockRows; ++row) {
        if (caseSelector != nullptr) {
            const CaseSelectorStatus rowStatus = caseSelector->beginBlockRow();
            if (rowStatus != CaseSelectorStatus::Ok) {
                // No block in this row got a chance to run at all - record
                // one synthetic BlockRecord at this row's first column so
                // the trace's own stop reason/outcome accounting (and
                // real_bink_test_vector_validation_test.cpp's own STAGE 1
                // comparison loop) has something to point at, matching how
                // a mid-row PreStageOpen/SourceExhausted already behaves.
                BlockRecord rec;
                rec.blockRow = row;
                rec.blockCol = 0;
                rec.outcome = (rowStatus == CaseSelectorStatus::Poisoned) ? BlockOutcome::CaseSelectorPoisoned
                              : (rowStatus == CaseSelectorStatus::SourceExhausted)
                                  ? BlockOutcome::SourceExhausted
                                  : BlockOutcome::PreStageOpen;
                trace.blocks.push_back(rec);
                trace.stopReason = (rowStatus == CaseSelectorStatus::Poisoned)   ? StopReason::HitCaseSelectorPoison
                                    : (rowStatus == CaseSelectorStatus::SourceExhausted)
                                        ? StopReason::SourceExhausted
                                        : StopReason::HitPreStageOpen;
                return trace;
            }
        }
        for (int col = 0; col < blockCols; ++col) {
            BlockRecord rec = decodeOneBlock(br, dest, prev, row, col, caseSelector);
            trace.blocks.push_back(rec);
            if (rec.outcome != BlockOutcome::Reconstructed) {
                switch (rec.outcome) {
                    case BlockOutcome::OpenHook:
                        trace.stopReason = StopReason::HitOpenHook;
                        break;
                    case BlockOutcome::OutOfRangeDispatch:
                        trace.stopReason = StopReason::HitOutOfRangeDispatch;
                        break;
                    case BlockOutcome::SourceExhausted:
                        trace.stopReason = StopReason::SourceExhausted;
                        break;
                    case BlockOutcome::PreStageOpen:
                        trace.stopReason = StopReason::HitPreStageOpen;
                        break;
                    case BlockOutcome::CaseSelectorPoisoned:
                        trace.stopReason = StopReason::HitCaseSelectorPoison;
                        break;
                    default:
                        break;
                }
                return trace;
            }
        }
    }
    trace.stopReason = StopReason::CompletedAllBlocks;
    return trace;
}

// blockCols/blockRows for a plane of the given pixel dimensions, using the
// one CONFIRMED block size (8x8, spec-bink-codec.md Sec5.6) - ceiling
// division, since a plane's own pixel size need not be an exact multiple
// of 8 (same "round up, don't invent a smaller valid grid" convention the
// header's own macroblock-16 rounding already uses, spec-bink-codec.md
// Sec1.2).
inline int blockGridCols(int planeWidth) { return (planeWidth + kBlockSize - 1) / kBlockSize; }
inline int blockGridRows(int planeHeight) { return (planeHeight + kBlockSize - 1) / kBlockSize; }

// Luma-plane real-data entry point (see this file's own top comment for
// the exact scope and the plane-sub-chunk-span question `hasLeadingSubChunkSpan`
// resolves per caller). `prev` is the previous frame's luma plane for the
// block-copy case (Sec5.6); pass nullptr for a true keyframe / "no previous
// frame available" test (frame 0's own case).
//
// `hasLeadingSubChunkSpan`: whether `br` is CURRENTLY positioned at the
// START of Y's own sub-chunk - i.e. at its own 4-byte little-endian span
// field, NOT yet past it (true: this function reads and skips that field
// itself via readPlaneSubChunkSpan() before touching anything else) - or
// already sitting at Y's own real entropy-data start, with no such field to
// skip (false). MANDATORY, no default: per this file's own top "PLANE
// SUB-CHUNK SPAN FIELD" note, this is CONFIRMED true for main_menu_bg.bik
// (a spec-bink-codec.md Sec1.6 `D10=0`/"no audio tracks" file) and
// EMPIRICALLY WRONG for tests/golden/bink_logo_frame0/frame0_raw.bin (a
// `D10>=1` file per that same section) - there is no safe shared default,
// so every caller must say which side of this still-open, per-file question
// its own input stands on, citing the fact that puts it there.
//
// FIX (2026-10-09, fix/bink-merge-group-bit-consumption follow-up):
// when `hasLeadingSubChunkSpan` is true, readPlaneSubChunkSpan() consumes
// Y's own leading 4-byte span field before anything else touches `br` -
// landing the entropy pre-stage's own 23-call table-build sequence
// (CaseSelectorScratchSource::beginBlockRow(), below) on the bit position
// spec-bink-codec.md Sec5.3g confirms is correct (Y-plane data start =
// sub-chunk start + 4), instead of the pre-fix behaviour of handing `br` to
// the pre-stage already-cold at the span field's OWN position (4 bytes/32
// bits too early), which desynced the very first of the 23 calls and
// poisoned block (0,0) immediately on real data (main_menu_bg.bik frame 0)
// - see this file's own top comment for the full citation trail. This
// parameter was added (rather than making the skip unconditional) after
// confirming that same unconditional skip EMPIRICALLY BREAKS the logo
// fixture (OutOfRangeDispatch at block (0,0), down from 418/419 blocks
// reconstructed) - see this file's own top comment for why that file is a
// genuinely different, still-open case, not a bug in this function.
//
// Constructs one entropy_prestage.h CaseSelectorScratchSource (keyed on
// this frame's own luma width, for Sec5.3c's length-field bit-width
// formula) and threads it through every block's primary dispatch read -
// see block_dispatch.h's "ENTROPY PRE-STAGE WIRING" note and
// entropy_prestage.h's own top comment for exactly what that means for
// real-data decode today.
inline DecodeTrace decodeLumaPlane(BitstreamReader& br, PlanarYuvFrame& frame, const PlaneView* prevLuma,
                                    bool hasLeadingSubChunkSpan) {
    if (hasLeadingSubChunkSpan) {
        readPlaneSubChunkSpan(br);  // skip Y's own 4-byte sub-chunk span field (Sec5.3g) - see fix note above
    }
    PlaneView dest = frame.luma().view();
    const int cols = blockGridCols(frame.dims().lumaWidth);
    const int rows = blockGridRows(frame.dims().lumaHeight);
    CaseSelectorScratchSource caseSelector(br, frame.dims().lumaWidth);
    return decodePlaneRasterOrder(br, dest, prevLuma, cols, rows, &caseSelector);
}

// Builds an explicit per-pixel "was this byte actually written by a
// Reconstructed block" mask from the trace's own block list, for
// yuv_to_rgb.h's viewer. Deliberately NOT based on comparing plane bytes
// against PlanarYuvFrame::kUndecodedSentinel - a real reconstructed byte
// (e.g. a DC-fill literal) could coincidentally equal the sentinel value,
// which would misreport a written pixel as unwritten; walking the trace's
// own records instead is exact. Blocks with outcome != Reconstructed (the
// one the trace stopped at, if any) are NOT marked written, even though
// decodeOneBlock() may have partially consumed bitstream bits for it -
// no pixels were produced for that block.
inline std::vector<uint8_t> buildWrittenMask(const DecodeTrace& trace, int planeWidth, int planeHeight) {
    std::vector<uint8_t> mask(static_cast<size_t>(planeWidth) * planeHeight, 0);
    for (const auto& b : trace.blocks) {
        if (b.outcome != BlockOutcome::Reconstructed) continue;
        const int x0 = b.blockCol * kBlockSize;
        const int y0 = b.blockRow * kBlockSize;
        for (int y = 0; y < kBlockSize && (y0 + y) < planeHeight; ++y) {
            for (int x = 0; x < kBlockSize && (x0 + x) < planeWidth; ++x) {
                mask[static_cast<size_t>(y0 + y) * planeWidth + (x0 + x)] = 1;
            }
        }
    }
    return mask;
}

}  // namespace sr3binkcodec
