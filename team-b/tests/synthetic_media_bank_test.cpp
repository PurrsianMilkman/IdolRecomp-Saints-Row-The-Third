// Synthetic tests for the `_media.bnk_pc` (`VWSBPC`) wrapper reader
// (sr3audio), covering ONLY the CONFIRMED part of spec-audio-format.md
// Sec4/Sec5: the 0x20-byte header, the 16-byte record table, the
// block-quantized chain rule, and the plain-`.bnk_pc` cross-reference.
//
// There is deliberately no test here for anything INSIDE a record's payload.
// Those bytes are third-party Wwise/Ogg Vorbis data (Sec1/Sec3/Sec6.2),
// explicitly out of this project's cleanroom scope, and this reader treats
// them as opaque ranges - see include/sr3audio/media_bank.h.
//
// Fixtures are built directly from the spec text - the field table in Sec4.1
// and the chain rule `offset[i+1] = offset[i] + round_up(size[i] + extra[i],
// 0x800)` in Sec4.2, restated locally as specChainAdvance() below - never by
// calling into src/media_bank.cpp. A fixture derived from the parser only
// proves the parser agrees with itself.
//
// THE CONTROL THIS SUITE EXISTS FOR: testChainHonoursNonZeroExtra and
// testNaiveExtraIgnoringTableIsRejected are a matched pair. Sec4.3 records
// that a first model which ignored `extra` still walked cleanly on 530/536
// real files - i.e. a reader that gets this wrong passes 98.9% of the
// population and looks fine. The first test pins the correct offsets for a
// chain where ignoring `extra` would diverge by exactly one 0x800 block; the
// second feeds the reader a table laid out the WRONG (naive) way and
// requires it to reject that, which is what distinguishes "honours extra"
// from "coincidentally agrees on this fixture".

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "sr3audio/media_bank.h"
#include "sr3audio/wwise_bank_id.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":"          \
                      << __LINE__ << "\n";                                   \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_THROWS(expr)                                                   \
    do {                                                                     \
        bool threw = false;                                                  \
        try { expr; }                                                        \
        catch (const std::exception&) { threw = true; }                     \
        if (!threw) {                                                        \
            std::cerr << "CHECK FAILED (expected throw): " #expr             \
                      << " at " __FILE__ ":" << __LINE__ << "\n";            \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

void putU32At(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    b[off + 0] = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    b[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    b[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

// spec-audio-format.md Sec4.2's chain rule, restated from the spec's own
// prose rather than imported from sr3audio::recordBlockSpan - the fixtures
// below must not be built by the code under test.
const size_t kBlock = 0x800;
size_t specChainAdvance(uint32_t size, uint32_t extra) {
    size_t n = static_cast<size_t>(size) + static_cast<size_t>(extra);
    return (n + kBlock - 1) / kBlock * kBlock;
}

// The naive rule Sec4.3 reports as walking cleanly on 530/536 real files -
// the one that ignores `extra` entirely. Present only so the tests can state
// numerically where it diverges from the confirmed rule.
size_t naiveChainAdvanceIgnoringExtra(uint32_t size, uint32_t /*extra*/) {
    return (static_cast<size_t>(size) + kBlock - 1) / kBlock * kBlock;
}

struct SynthRecord {
    uint32_t offset = 0;
    uint32_t extra = 0;
    uint32_t size = 0;
    uint32_t tag = 0;
};

struct SynthHeader {
    uint32_t crossReferenceId = 0;
    uint32_t fieldAt0x14 = 0;
    uint32_t declaredRecordCount = 0;
    uint32_t fieldAt0x1C = 0;
};

// Builds a `_media.bnk_pc` entry payload from the Sec4.1 field table:
// magic, the 8 constant bytes, four u32s, then the 16-byte record table at
// +0x20, then zero padding out to `totalBytes` (real files are
// over-allocated past the real record count - Sec4.3).
std::vector<uint8_t> buildMediaBank(const SynthHeader& h, const std::vector<SynthRecord>& recs,
                                    size_t totalBytes) {
    std::vector<uint8_t> b(totalBytes, 0x00);

    const uint8_t magic[8] = {0x56, 0x57, 0x53, 0x42, 0x50, 0x43, 0x20, 0x20};
    for (size_t i = 0; i < 8; ++i) b[i] = magic[i];

    const uint8_t constant[8] = {0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00};
    for (size_t i = 0; i < 8; ++i) b[0x08 + i] = constant[i];

    putU32At(b, 0x10, h.crossReferenceId);
    putU32At(b, 0x14, h.fieldAt0x14);
    putU32At(b, 0x18, h.declaredRecordCount);
    putU32At(b, 0x1C, h.fieldAt0x1C);

    for (size_t i = 0; i < recs.size(); ++i) {
        size_t at = 0x20 + i * 16;
        putU32At(b, at + 0x00, recs[i].offset);
        putU32At(b, at + 0x04, recs[i].extra);
        putU32At(b, at + 0x08, recs[i].size);
        putU32At(b, at + 0x0C, recs[i].tag);
    }
    return b;
}

// Builds a plain `.bnk_pc`'s first 16 bytes per the public layout in Sec3.
std::vector<uint8_t> buildWwiseBankPrefix(uint32_t chunkLen, uint32_t genVersion,
                                          uint32_t soundBankId, size_t totalBytes = 64) {
    std::vector<uint8_t> b(totalBytes, 0x00);
    const uint8_t magic[4] = {0x42, 0x4B, 0x48, 0x44}; // ASCII BKHD
    for (size_t i = 0; i < 4; ++i) b[i] = magic[i];
    putU32At(b, 0x04, chunkLen);
    putU32At(b, 0x08, genVersion);
    putU32At(b, 0x0C, soundBankId);
    return b;
}

sr3audio::ByteView view(const std::vector<uint8_t>& b) {
    return sr3audio::ByteView(b.data(), b.size());
}

// ============================================================
// Part A: the 0x20-byte header (Sec4.1).
// ============================================================

void testHeaderFieldsWellFormed() {
    SynthHeader h;
    h.crossReferenceId = 0x22DBA0EFu; // one of Sec5's own quoted real values
    h.fieldAt0x14 = 0x000001A4u;
    h.declaredRecordCount = 2;
    h.fieldAt0x1C = 0xDEADBEEFu;

    std::vector<SynthRecord> recs = {
        {0x800, 0, 0x400, 0x11111111u},
        {0x1000, 0, 0x400, 0x22222222u},
    };
    std::vector<uint8_t> b = buildMediaBank(h, recs, 0x1800);

    sr3audio::MediaBank mb = sr3audio::MediaBank::parse(view(b));
    CHECK(mb.crossReferenceId() == 0x22DBA0EFu);
    CHECK(mb.fieldAt0x14() == 0x000001A4u);
    CHECK(mb.declaredRecordCount() == 2u);
    CHECK(mb.fieldAt0x1C() == 0xDEADBEEFu);
    CHECK(mb.constantAt0x08MatchesShipped());
    CHECK(mb.constantAt0x08()[4] == 0x02);
    CHECK(mb.constantAt0x08()[6] == 0x01);
    CHECK(mb.content().size() == 0x1800u);
}

void testMagicAcceptAndReject() {
    SynthHeader h;
    std::vector<SynthRecord> recs = {{0x800, 0, 0x400, 0}};
    std::vector<uint8_t> good = buildMediaBank(h, recs, 0x1000);
    CHECK(sr3audio::MediaBank::looksLikeMediaBank(view(good)));
    sr3audio::MediaBank::parse(view(good)); // must not throw

    // Every one of the 8 magic bytes is load-bearing, including the two
    // 0x20 space-pad bytes Sec4.1 lists explicitly.
    for (size_t i = 0; i < 8; ++i) {
        std::vector<uint8_t> bad = good;
        bad[i] = static_cast<uint8_t>(bad[i] ^ 0xFF);
        CHECK(!sr3audio::MediaBank::looksLikeMediaBank(view(bad)));
        CHECK_THROWS(sr3audio::MediaBank::parse(view(bad)));
    }

    // A plain `.bnk_pc` (the OTHER shape Sec2 says every audio-archive entry
    // has) must not be mistaken for a wrapper.
    std::vector<uint8_t> plain = buildWwiseBankPrefix(0x18, 0x35, 0x40E182EAu);
    CHECK(!sr3audio::MediaBank::looksLikeMediaBank(view(plain)));
    CHECK_THROWS(sr3audio::MediaBank::parse(view(plain)));

    // Short buffers: discriminator says no, parse throws, neither reads OOB.
    std::vector<uint8_t> tiny(4, 0x56);
    CHECK(!sr3audio::MediaBank::looksLikeMediaBank(view(tiny)));
    CHECK_THROWS(sr3audio::MediaBank::parse(view(tiny)));

    std::vector<uint8_t> justUnderHeader(good.begin(), good.begin() + 0x1F);
    CHECK(sr3audio::MediaBank::looksLikeMediaBank(view(justUnderHeader))); // magic is fine
    CHECK_THROWS(sr3audio::MediaBank::parse(view(justUnderHeader)));       // header is not
}

void testConstantAt0x08IsSurfacedRawAndNotEnforced() {
    // Sec4.1 rates +0x08's ROLE as OPEN. Per this project's standing rule a
    // parse must never fail on account of an OPEN field - a file with a
    // different constant would be a FINDING to report, not a parse error.
    SynthHeader h;
    h.declaredRecordCount = 1;
    std::vector<SynthRecord> recs = {{0x800, 0, 0x400, 0}};
    std::vector<uint8_t> b = buildMediaBank(h, recs, 0x1000);
    b[0x0C] = 0x09; // was 0x02

    sr3audio::MediaBank mb = sr3audio::MediaBank::parse(view(b)); // must NOT throw
    CHECK(!mb.constantAt0x08MatchesShipped());
    CHECK(mb.constantAt0x08()[4] == 0x09);
    // ...and the walk still works, because the field gates nothing.
    CHECK(mb.walk(0x1000).size() == 1u);
}

// ============================================================
// Part B: the record table and the chain rule (Sec4.2).
// ============================================================

void testSimpleChainAllExtraZero() {
    SynthHeader h;
    h.declaredRecordCount = 3;
    std::vector<SynthRecord> recs = {
        {0x800, 0, 0x400, 0xAAAA0001u},  // advance 0x800 -> 0x1000
        {0x1000, 0, 0x800, 0xAAAA0002u}, // advance 0x800 -> 0x1800
        {0x1800, 0, 0x1234, 0xAAAA0003u} // advance 0x1800 -> 0x3000
    };
    const size_t kTotal = 0x3000;
    std::vector<uint8_t> b = buildMediaBank(h, recs, kTotal);

    sr3audio::MediaBank mb = sr3audio::MediaBank::parse(view(b));
    std::vector<sr3audio::MediaBankRecord> walked = mb.walk(kTotal);

    CHECK(walked.size() == 3u);
    CHECK(walked[0].offset == 0x800u);
    CHECK(walked[1].offset == 0x1000u);
    CHECK(walked[2].offset == 0x1800u);
    CHECK(walked[0].size == 0x400u);
    CHECK(walked[2].size == 0x1234u);
    // `tag` is OPEN - carried through raw, uninterpreted.
    CHECK(walked[0].tag == 0xAAAA0001u);
    CHECK(walked[2].tag == 0xAAAA0003u);
    // Every extra is zero here, so the confirmed and naive rules agree -
    // which is exactly why this test alone proves nothing about `extra`.
    for (const auto& r : walked) CHECK(r.extra == 0u);

    // Sec4.1's second, independent signal: +0x18 equals the walked count.
    CHECK(mb.declaredRecordCount() == walked.size());
}

void testZeroRecordChainTerminatesImmediately() {
    // A wrapper whose declared total is exactly record 0's own start: the
    // walk must return empty rather than reading the (zero-padded) table.
    SynthHeader h;
    h.declaredRecordCount = 0;
    std::vector<uint8_t> b = buildMediaBank(h, {}, 0x800);
    sr3audio::MediaBank mb = sr3audio::MediaBank::parse(view(b));
    CHECK(mb.walk(0x800).empty());
}

void testChainHonoursNonZeroExtra() {
    // Record 1 carries extra == 0x800, one of the exact multiples Sec4.2
    // reports observing across the real population (2048..16384).
    //
    //   r0: 0x800  size 0x400  extra 0      -> round_up(0x400)        = 0x800  -> 0x1000
    //   r1: 0x1000 size 0x400  extra 0x800  -> round_up(0x400+0x800)  = 0x1000 -> 0x2000
    //   r2: 0x2000 size 0x1234 extra 0      -> round_up(0x1234)       = 0x1800 -> 0x3800
    //
    // A reader that drops `extra` computes 0x800 for r1 and puts r2 at
    // 0x1800 - "diverging by precisely one extra 0x800 block at the exact
    // record where extra was non-zero", Sec4.3's own description of the 6
    // real files the naive model broke on.
    SynthHeader h;
    h.declaredRecordCount = 3;
    std::vector<SynthRecord> recs = {
        {0x800, 0, 0x400, 0xBBBB0001u},
        {0x1000, 0x800, 0x400, 0xBBBB0002u},
        {0x2000, 0, 0x1234, 0xBBBB0003u},
    };
    const size_t kTotal = 0x3800;

    // State the divergence numerically, from the two locally-restated rules,
    // so this fixture is self-evidently discriminating rather than trusted.
    CHECK(specChainAdvance(0x400, 0x800) == 0x1000u);
    CHECK(naiveChainAdvanceIgnoringExtra(0x400, 0x800) == 0x800u);
    CHECK(specChainAdvance(0x400, 0x800) - naiveChainAdvanceIgnoringExtra(0x400, 0x800) == kBlock);

    std::vector<uint8_t> b = buildMediaBank(h, recs, kTotal);
    sr3audio::MediaBank mb = sr3audio::MediaBank::parse(view(b));
    std::vector<sr3audio::MediaBankRecord> walked = mb.walk(kTotal);

    CHECK(walked.size() == 3u);
    CHECK(walked[0].offset == 0x800u);
    CHECK(walked[1].offset == 0x1000u);
    CHECK(walked[1].extra == 0x800u);
    CHECK(walked[2].offset == 0x2000u); // 0x1800 if `extra` were ignored
    CHECK(mb.declaredRecordCount() == walked.size());

    // And the reader's own exposed span helper agrees with the spec formula.
    CHECK(sr3audio::recordBlockSpan(walked[1]) == 0x1000u);
}

void testNaiveExtraIgnoringTableIsRejected() {
    // THE MUTATION CONTROL. Same records, same non-zero `extra` on r1, but
    // the table is laid out the way a reader that IGNORES `extra` would lay
    // it out (r2 at 0x1800, total 0x3000). A reader that honours `extra`
    // must reject this as a chain break; one that ignores it would accept
    // it happily. Without this half, testChainHonoursNonZeroExtra cannot
    // tell the two readers apart.
    SynthHeader h;
    h.declaredRecordCount = 3;
    std::vector<SynthRecord> recs = {
        {0x800, 0, 0x400, 0xCCCC0001u},
        {0x1000, 0x800, 0x400, 0xCCCC0002u},
        {0x1800, 0, 0x1234, 0xCCCC0003u}, // naive placement
    };
    std::vector<uint8_t> b = buildMediaBank(h, recs, 0x3000);
    sr3audio::MediaBank mb = sr3audio::MediaBank::parse(view(b));
    CHECK_THROWS(mb.walk(0x3000));
}

void testRejectsOvershootOfDeclaredSize() {
    // The correct chain lands on 0x3800; tell the walk the container
    // declared 0x3000 and it must throw rather than return a truncated list.
    SynthHeader h;
    h.declaredRecordCount = 3;
    std::vector<SynthRecord> recs = {
        {0x800, 0, 0x400, 0},
        {0x1000, 0x800, 0x400, 0},
        {0x2000, 0, 0x1234, 0},
    };
    std::vector<uint8_t> b = buildMediaBank(h, recs, 0x3800);
    sr3audio::MediaBank mb = sr3audio::MediaBank::parse(view(b));

    CHECK(mb.walk(0x3800).size() == 3u); // the honest total still works
    CHECK_THROWS(mb.walk(0x3000));       // overshoots on the last record
    CHECK_THROWS(mb.walk(0x1800));       // overshoots earlier
    CHECK_THROWS(mb.walk(0x400));        // smaller than record 0's own start
    CHECK_THROWS(mb.walk(0x3801));       // lands between blocks, never equal
}

void testRejectsUndershootIntoZeroPadding() {
    // Sec4.3's second, worse failure mode - "a separate failure mode worth
    // recording precisely because it looked like success". The real table is
    // 3 records; the file is zero-padded well past them. Declare a total
    // LARGER than where the chain really ends and a naive walk sails on
    // through the padding, because `0 == 0 + round_up(0, 0x800)` holds for
    // every all-zero record. This reader must refuse instead.
    SynthHeader h;
    h.declaredRecordCount = 3;
    std::vector<SynthRecord> recs = {
        {0x800, 0, 0x400, 0},
        {0x1000, 0, 0x400, 0},
        {0x1800, 0, 0x400, 0},
    };
    std::vector<uint8_t> b = buildMediaBank(h, recs, 0x4000); // padding after record 2
    sr3audio::MediaBank mb = sr3audio::MediaBank::parse(view(b));

    CHECK(mb.walk(0x2000).size() == 3u); // the real end
    CHECK_THROWS(mb.walk(0x4000));       // would need record 3, which is zero padding
    CHECK_THROWS(mb.walk(0x2800));       // ditto, one block further on
}

void testRejectsZeroAdvanceRecord() {
    // The residual half of Sec4.3's zero-padding trap. A record whose
    // `offset` DOES equal the running cursor but whose `size` and `extra`
    // are both zero satisfies the chain rule vacuously (`0 == 0 +
    // round_up(0, 0x800)`) and advances the cursor by nothing. Ordinary zero
    // padding past the real table is already caught by the offset/cursor
    // check (padding reads offset 0; the cursor never is 0) - this is the
    // case that check cannot see.
    //
    // THE FIXTURE IS BUILT TO DISCRIMINATE, and that took a second attempt
    // worth recording: a lone zero-span record followed by zero padding
    // throws EITHER WAY (the padding breaks the chain one iteration later),
    // so a reader with no zero-advance guard at all still "passed". The
    // shape below is the one that separates them - the zero-span record is
    // followed by a REAL record sitting at the same offset, so a guardless
    // walk terminates cleanly on the declared size while silently reporting
    // one more record than exists. That is Sec4.3's "silently reinflating
    // the apparent match count", in miniature.
    CHECK(sr3audio::recordBlockSpan({0x800, 0, 0, 0}) == 0u);

    SynthHeader h;
    h.declaredRecordCount = 2;
    std::vector<SynthRecord> recs = {
        {0x800, 0, 0, 0x5A5A5A5Au},      // zero span: cursor does not move
        {0x800, 0, 0x400, 0x5A5A5A5Bu},  // a real record at the SAME offset
    };
    std::vector<uint8_t> b = buildMediaBank(h, recs, 0x1000);
    sr3audio::MediaBank mb = sr3audio::MediaBank::parse(view(b));
    CHECK_THROWS(mb.walk(0x1000)); // a guardless walk returns 2 records and no error
}

void testRejectsChainBreak() {
    SynthHeader h;
    h.declaredRecordCount = 3;
    std::vector<SynthRecord> recs = {
        {0x800, 0, 0x400, 0},
        {0x1800, 0, 0x400, 0}, // should be 0x1000
        {0x2000, 0, 0x400, 0},
    };
    std::vector<uint8_t> b = buildMediaBank(h, recs, 0x2800);
    sr3audio::MediaBank mb = sr3audio::MediaBank::parse(view(b));
    CHECK_THROWS(mb.walk(0x2800));
}

void testAnchorIsReadFromTableNotDerived() {
    // Sec4.2 says record 0's offset is "always 0x800". Measured over the
    // same 536-file population that claim holds in only 278 - the real rule
    // is round_up(0x20 + recordCount*16, 0x800), the first block after the
    // header AND the table (see the anchor note in media_bank.h, and
    // tools/validation/diag_media_bank_first_offset.cpp for the measurement).
    //
    // So a non-0x800 anchor must WALK, not be rejected. The fixture goes one
    // step further and makes the derived rule disagree with the table on
    // purpose - declaredRecordCount 3 derives an anchor of 0x800, while the
    // table says 0x1800 - so this test fails for a reader that computes the
    // anchor from +0x18 instead of reading it, as well as for one that
    // hardcodes 0x800.
    SynthHeader h;
    h.declaredRecordCount = 3;
    std::vector<SynthRecord> recs = {
        {0x1800, 0, 0x400, 0xD0D00001u},
        {0x2000, 0, 0x400, 0xD0D00002u},
        {0x2800, 0, 0x400, 0xD0D00003u},
    };
    std::vector<uint8_t> b = buildMediaBank(h, recs, 0x3000);
    sr3audio::MediaBank mb = sr3audio::MediaBank::parse(view(b));

    CHECK(mb.blockAlignedTableEnd() == 0x800u); // the derived value, deliberately different
    std::vector<sr3audio::MediaBankRecord> walked = mb.walk(0x3000);
    CHECK(walked.size() == 3u);
    CHECK(walked[0].offset == 0x1800u);
    CHECK(walked[2].offset == 0x2800u);
}

void testBlockAlignedTableEndFormula() {
    // The derived rule itself, pinned at its 0x800 boundary - worth a test
    // rather than an eyeball, since the header's own 0x20 bytes shift it off
    // the round number: 126 records end at exactly 0x800, and 127 spill into
    // the next block.
    auto tableEndFor = [](uint32_t count) {
        SynthHeader h;
        h.declaredRecordCount = count;
        std::vector<uint8_t> b = buildMediaBank(h, {{0x800, 0, 0x400, 0}}, 0x1000);
        return sr3audio::MediaBank::parse(view(b)).blockAlignedTableEnd();
    };
    CHECK(tableEndFor(0) == 0x800u);
    CHECK(tableEndFor(126) == 0x800u);  // 0x20 + 126*16 = 0x800 exactly
    CHECK(tableEndFor(127) == 0x1000u); // 0x20 + 127*16 = 0x810, spills
    CHECK(tableEndFor(128) == 0x1000u);
    CHECK(tableEndFor(200) == 0x1000u);  // 0x20 + 200*16  = 0xCA0
    CHECK(tableEndFor(1000) == 0x4000u); // 0x20 + 1000*16 = 0x3EA0
}

void testRejectsUnusableAnchor() {
    SynthHeader h;
    h.declaredRecordCount = 1;

    // Anchor zero - i.e. record 0 is itself the wrapper's zero-padded
    // reserved table space, not a record.
    std::vector<uint8_t> zeroAnchor = buildMediaBank(h, {{0x0, 0, 0x400, 0}}, 0x1000);
    CHECK_THROWS(sr3audio::MediaBank::parse(view(zeroAnchor)).walk(0x1000));

    // Anchor not on a 0x800 boundary. Every offset[0] in the shipped
    // population is a multiple of 0x800 (all ten distinct values observed).
    std::vector<uint8_t> misaligned = buildMediaBank(h, {{0x900, 0, 0x400, 0}}, 0x1100);
    CHECK_THROWS(sr3audio::MediaBank::parse(view(misaligned)).walk(0x1100));

    // A table with no room for record 0 at all.
    std::vector<uint8_t> full = buildMediaBank(h, {{0x800, 0, 0x400, 0}}, 0x1000);
    std::vector<uint8_t> headerOnly(full.begin(), full.begin() + 0x2F);
    CHECK_THROWS(sr3audio::MediaBank::parse(view(headerOnly)).walk(0x1000));
}

void testRejectsTableRunningOffEndOfContent() {
    // A truncated entry: the chain would need a third record, but the bytes
    // stop after two. Must throw, not return a short list.
    SynthHeader h;
    h.declaredRecordCount = 3;
    std::vector<SynthRecord> recs = {
        {0x800, 0, 0x400, 0},
        {0x1000, 0, 0x400, 0},
    };
    std::vector<uint8_t> full = buildMediaBank(h, recs, 0x2000);
    std::vector<uint8_t> truncated(full.begin(), full.begin() + 0x40); // header + 2 records
    sr3audio::MediaBank mb = sr3audio::MediaBank::parse(view(truncated));
    CHECK(mb.walk(0x1800).size() == 2u); // the two records that are actually there
    CHECK_THROWS(mb.walk(0x2000));       // needs a third record; bytes ran out
}

void testDeclaredRecordCountIsMeasuredNotEnforced() {
    // Sec11's lesson: the container's declared size is the terminating
    // oracle, and a record-count-looking header field can be a decoy. So a
    // wrong +0x18 must NOT change the walk's result - it must stay visible
    // as a disagreement the caller can report.
    SynthHeader h;
    h.declaredRecordCount = 99; // deliberately wrong
    std::vector<SynthRecord> recs = {
        {0x800, 0, 0x400, 0},
        {0x1000, 0, 0x400, 0},
    };
    std::vector<uint8_t> b = buildMediaBank(h, recs, 0x1800);
    sr3audio::MediaBank mb = sr3audio::MediaBank::parse(view(b));
    std::vector<sr3audio::MediaBankRecord> walked = mb.walk(0x1800);
    CHECK(walked.size() == 2u);
    CHECK(mb.declaredRecordCount() == 99u);
    CHECK(mb.declaredRecordCount() != walked.size()); // visible, not silently repaired
}

// ============================================================
// Part C: the plain `.bnk_pc` cross-reference (Sec3/Sec5).
// ============================================================

void testWwiseBankHeaderPrefix() {
    std::vector<uint8_t> b = buildWwiseBankPrefix(0x18, 0x35, 0x40E182EAu);
    CHECK(sr3audio::hasWwiseBankMagic(view(b)));
    sr3audio::WwiseBankHeaderPrefix p = sr3audio::readWwiseBankHeaderPrefix(view(b));
    CHECK(p.chunkDataLength == 0x18u);
    CHECK(p.bankGeneratorVersion == 0x35u);
    CHECK(p.soundBankId == 0x40E182EAu); // Sec5's own `interface` example
}

void testWwiseBankHeaderRejections() {
    std::vector<uint8_t> good = buildWwiseBankPrefix(0x1C, 0x34, 0x8024D475u);

    for (size_t i = 0; i < 4; ++i) {
        std::vector<uint8_t> bad = good;
        bad[i] = static_cast<uint8_t>(bad[i] ^ 0xFF);
        CHECK(!sr3audio::hasWwiseBankMagic(view(bad)));
        CHECK_THROWS(sr3audio::readWwiseBankHeaderPrefix(view(bad)));
    }

    std::vector<uint8_t> shortBuf(good.begin(), good.begin() + 15);
    CHECK(sr3audio::hasWwiseBankMagic(view(shortBuf))); // magic is there
    CHECK_THROWS(sr3audio::readWwiseBankHeaderPrefix(view(shortBuf))); // the id is not

    std::vector<uint8_t> tiny(2, 0x42);
    CHECK(!sr3audio::hasWwiseBankMagic(view(tiny)));
    CHECK_THROWS(sr3audio::readWwiseBankHeaderPrefix(view(tiny)));

    // A `_media.bnk_pc` wrapper must not read as a plain bank.
    SynthHeader h;
    std::vector<uint8_t> wrapper = buildMediaBank(h, {{0x800, 0, 0x400, 0}}, 0x1000);
    CHECK(!sr3audio::hasWwiseBankMagic(view(wrapper)));
    CHECK_THROWS(sr3audio::readWwiseBankHeaderPrefix(view(wrapper)));
}

void testCrossReferenceMatchesSiblingSoundBankId() {
    // Sec5: the wrapper's +0x10 equals the sibling bank's +0x0C exactly, a
    // full 32-bit match, 260/260 in-archive pairs. Built here as a matched
    // pair, plus a deliberately mismatched pair so the check is capable of
    // failing (Sec5's own stated failing case: "any pair with differing
    // 32-bit values").
    const uint32_t kId = 0xADC617EEu; // another of Sec5's quoted real values

    SynthHeader h;
    h.crossReferenceId = kId;
    std::vector<uint8_t> media = buildMediaBank(h, {{0x800, 0, 0x400, 0}}, 0x1000);
    std::vector<uint8_t> plain = buildWwiseBankPrefix(0x18, 0x35, kId);

    sr3audio::MediaBank mb = sr3audio::MediaBank::parse(view(media));
    sr3audio::WwiseBankHeaderPrefix p = sr3audio::readWwiseBankHeaderPrefix(view(plain));
    CHECK(mb.crossReferenceId() == p.soundBankId);

    std::vector<uint8_t> otherPlain = buildWwiseBankPrefix(0x18, 0x35, kId ^ 0x1u);
    sr3audio::WwiseBankHeaderPrefix q = sr3audio::readWwiseBankHeaderPrefix(view(otherPlain));
    CHECK(mb.crossReferenceId() != q.soundBankId); // one bit is enough to fail it
}

void run(const char* name, void (*fn)()) {
    try {
        fn();
    } catch (const std::exception& ex) {
        std::cerr << "CHECK FAILED: " << name << " threw: " << ex.what() << "\n";
        ++g_failures;
    }
}

} // namespace

int main() {
    run("testHeaderFieldsWellFormed", testHeaderFieldsWellFormed);
    run("testMagicAcceptAndReject", testMagicAcceptAndReject);
    run("testConstantAt0x08IsSurfacedRawAndNotEnforced",
        testConstantAt0x08IsSurfacedRawAndNotEnforced);

    run("testSimpleChainAllExtraZero", testSimpleChainAllExtraZero);
    run("testZeroRecordChainTerminatesImmediately", testZeroRecordChainTerminatesImmediately);
    run("testChainHonoursNonZeroExtra", testChainHonoursNonZeroExtra);
    run("testNaiveExtraIgnoringTableIsRejected", testNaiveExtraIgnoringTableIsRejected);
    run("testRejectsOvershootOfDeclaredSize", testRejectsOvershootOfDeclaredSize);
    run("testRejectsUndershootIntoZeroPadding", testRejectsUndershootIntoZeroPadding);
    run("testRejectsZeroAdvanceRecord", testRejectsZeroAdvanceRecord);
    run("testRejectsChainBreak", testRejectsChainBreak);
    run("testAnchorIsReadFromTableNotDerived", testAnchorIsReadFromTableNotDerived);
    run("testBlockAlignedTableEndFormula", testBlockAlignedTableEndFormula);
    run("testRejectsUnusableAnchor", testRejectsUnusableAnchor);
    run("testRejectsTableRunningOffEndOfContent", testRejectsTableRunningOffEndOfContent);
    run("testDeclaredRecordCountIsMeasuredNotEnforced",
        testDeclaredRecordCountIsMeasuredNotEnforced);

    run("testWwiseBankHeaderPrefix", testWwiseBankHeaderPrefix);
    run("testWwiseBankHeaderRejections", testWwiseBankHeaderRejections);
    run("testCrossReferenceMatchesSiblingSoundBankId", testCrossReferenceMatchesSiblingSoundBankId);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic media-bank (VWSBPC) tests passed.\n";
    return 0;
}
