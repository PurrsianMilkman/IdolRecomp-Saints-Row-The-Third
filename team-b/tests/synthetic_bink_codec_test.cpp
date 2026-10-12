// Synthetic (no real game data needed) regression for the Bink decoder
// skeleton - include/sr3binkcodec/{bitstream_reader,block_dispatch,
// planar_buffer,frame_decoder,yuv_to_rgb}.h. Every behaviour checked here is
// hand-constructed: these are NOT claims about what a real Bink file
// contains, they are round-trip checks that this project's own CONFIRMED-
// shape bitstream reader and the fully-reconstructed block cases (block
// copy primary 0/9, DC fill, skip-flag, the dual-row-duplication raw copy
// primary 1 sub 6 - spec-bink-codec.md Sec5.3/Sec5.3b) behave exactly as
// documented, deterministically, against bits this test built itself. See
// tests/real_bink_frame0_decode_test.cpp for the real-data companion (frame
// 0 of the game's own logo movie).

#include <cstdint>
#include <iostream>
#include <vector>

#include "sr3binkcodec/bitstream_reader.h"
#include "sr3binkcodec/block_dispatch.h"
#include "sr3binkcodec/dc_delta_array.h"
#include "sr3binkcodec/entropy_prestage.h"
#include "sr3binkcodec/frame_decoder.h"
#include "sr3binkcodec/planar_buffer.h"
#include "sr3binkcodec/yuv_to_rgb.h"
#include "sr3luahost/open_state.h"
#include "sr3runtimedata/runtime_data_loader.h"
#include "bink_md5.h"

using namespace sr3binkcodec;

namespace {
int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)

void testMd5SelfCheck() {
    // RFC 1321's own published test vectors - confirms tests/bink_md5.h
    // (used by the real-data golden-file verification below) is correct
    // before anything trusts it.
    CHECK(bink_test_md5::computeMd5Hex(reinterpret_cast<const uint8_t*>(""), 0) ==
          "d41d8cd98f00b204e9800998ecf8427e");
    const char* abc = "abc";
    CHECK(bink_test_md5::computeMd5Hex(reinterpret_cast<const uint8_t*>(abc), 3) ==
          "900150983cd24fb0d6963f7d28e17f72");
}

void testBitstreamReaderLsbFirstNibbles() {
    // dword (LE bytes 0xB2,0x00,0x00,0x00) = 0x000000B2 = 0b...10110010.
    // LSB-first: first 4 bits = 0010 (0x2), next 4 bits = 1011 (0xB).
    const uint8_t bytes[4] = {0xB2, 0x00, 0x00, 0x00};
    BitstreamReader br(bytes, sizeof(bytes));
    CHECK(br.readBits(4) == 0x2);
    CHECK(br.readBits(4) == 0xB);
    CHECK(!br.ranOutOfData());
}

void testBitstreamReaderMixedWidths() {
    // Exercise all four CONFIRMED widths (1/3/4/7, spec-bink-codec.md
    // Sec5.3a) back to back from one known dword, LSB-first throughout.
    // dword bits (LSB..MSB) chosen as: 1 bit=1, 3 bits=011(=3,LSB-first
    // meaning bit value 3), 4 bits=1001(=9), 7 bits=0000011(=3 LSB-first).
    // Built by packing bit-by-bit LSB-first into a uint32 then splitting LE.
    uint32_t packed = 0;
    int pos = 0;
    auto pack = [&](uint32_t value, int width) {
        packed |= (value & ((1u << width) - 1)) << pos;
        pos += width;
    };
    pack(1, 1);
    pack(3, 3);
    pack(9, 4);
    pack(3, 7);
    uint8_t bytes[4] = {static_cast<uint8_t>(packed & 0xff), static_cast<uint8_t>((packed >> 8) & 0xff),
                        static_cast<uint8_t>((packed >> 16) & 0xff), static_cast<uint8_t>((packed >> 24) & 0xff)};
    BitstreamReader br(bytes, sizeof(bytes));
    CHECK(br.readBits(1) == 1u);
    CHECK(br.readBits(3) == 3u);
    CHECK(br.readBits(4) == 9u);
    CHECK(br.readBits(7) == 3u);
}

void testBitstreamReaderRefillAcrossDwordBoundary() {
    // 32 bits consumed exactly (one dword), then one more 7-bit read must
    // trigger a second dword refill. Second dword's low 7 bits = 0x55.
    uint8_t bytes[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0x55, 0x00, 0x00, 0x00};
    BitstreamReader br(bytes, sizeof(bytes));
    CHECK(br.readBits(32) == 0xFFFFFFFFu);
    CHECK(br.readBits(7) == (0x55u & 0x7f));
    CHECK(!br.ranOutOfData());
}

void testBitstreamReaderAlignToByte() {
    // byte0=0x05 (low 3 bits = 5), byte1=0xAB. After readBits(3), the
    // remaining 5 bits of byte0 must be discarded by alignToByte() so the
    // next whole-byte read returns byte1 untouched, not a bit-shifted
    // mix of byte0/byte1.
    uint8_t bytes[4] = {0x05, 0xAB, 0x00, 0x00};
    BitstreamReader br(bytes, sizeof(bytes));
    CHECK(br.readBits(3) == 0x5u);
    CHECK(br.readAlignedByte() == 0xAB);
}

void testBitstreamReaderRanOutOfData() {
    uint8_t bytes[1] = {0x00};
    BitstreamReader br(bytes, sizeof(bytes));
    br.readBits(8);   // consumes the one real byte, pads the dword with 3 invented zero bytes
    CHECK(br.ranOutOfData());
}

// A small LSB-first bit packer mirroring BitstreamReader's own consumption
// order exactly: bits appended first are read back first, packed low-bit-
// first within each byte and byte-0-first across bytes - the same
// convention readBits()/nextDwordLE() implement. Used so multi-block
// synthetic bitstreams bit-pack correctly (two 4-bit dispatch reads in a
// row share ONE byte, they are not one byte each) rather than assuming a
// byte-per-field layout that would silently desync from the real reader.
class BitPacker {
public:
    void appendBits(uint32_t value, int width) {
        for (int i = 0; i < width; ++i) {
            const uint32_t bit = (value >> i) & 1u;
            if (bitPos_ == 0) bytes_.push_back(0);
            bytes_.back() |= static_cast<uint8_t>(bit << bitPos_);
            bitPos_ = (bitPos_ + 1) % 8;
        }
    }
    void alignToByte() {
        if (bitPos_ != 0) bitPos_ = 0;  // next appendBits()/appendByte() starts a fresh byte
    }
    void appendByte(uint8_t value) {
        alignToByte();
        bytes_.push_back(value);
    }
    std::vector<uint8_t> bytes(size_t padToAtLeast = 4) const {
        std::vector<uint8_t> out = bytes_;
        while (out.size() < padToAtLeast) out.push_back(0x00);  // pad so a dword refill never runs dry mid-test
        return out;
    }

private:
    std::vector<uint8_t> bytes_;
    int bitPos_ = 0;
};

// Builds one block's own dispatch bits: primary nibble first (LSB-first,
// low nibble of the first byte), optionally followed by a sub-dispatch
// nibble (high nibble of the same byte, per BitstreamReader's own LSB-first
// continuation) and/or literal bytes (byte-aligned, matching
// readAlignedByte()'s own realignment convention).
std::vector<uint8_t> buildBlockBytes(uint32_t primaryNibble, const uint32_t* subRawNibble,
                                      std::initializer_list<uint8_t> literalBytes) {
    BitPacker packer;
    packer.appendBits(primaryNibble, 4);
    if (subRawNibble) packer.appendBits(*subRawNibble, 4);
    for (uint8_t b : literalBytes) packer.appendByte(b);
    return packer.bytes();
}

void testBlockCopyReconstructed() {
    std::vector<uint8_t> bits = buildBlockBytes(0 /*BlockCopy*/, nullptr, {});
    BitstreamReader br(bits.data(), bits.size());

    OwnedPlane prev, dest;
    prev.resize(8, 8, 0);
    dest.resize(8, 8, PlanarYuvFrame::kUndecodedSentinel);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) prev.view().at(x, y) = static_cast<uint8_t>(y * 8 + x);

    PlaneView prevView = prev.view();
    BlockRecord rec = decodeOneBlock(br, dest.view(), &prevView, 0, 0);
    CHECK(rec.outcome == BlockOutcome::Reconstructed);
    CHECK(rec.primary == PrimaryBlockType::BlockCopy);
    bool allMatch = true;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) allMatch &= (dest.view().at(x, y) == prev.view().at(x, y));
    CHECK(allMatch);
}

void testBlockCopyWithNoPreviousFrameLeavesSentinel() {
    std::vector<uint8_t> bits = buildBlockBytes(0, nullptr, {});
    BitstreamReader br(bits.data(), bits.size());
    OwnedPlane dest;
    dest.resize(8, 8, PlanarYuvFrame::kUndecodedSentinel);
    BlockRecord rec = decodeOneBlock(br, dest.view(), nullptr, 0, 0);
    CHECK(rec.outcome == BlockOutcome::Reconstructed);  // dispatch TYPE is still fully known
    bool allSentinel = true;
    for (size_t i = 0; i < dest.size(); ++i) allSentinel &= (dest.data()[i] == PlanarYuvFrame::kUndecodedSentinel);
    CHECK(allSentinel);  // no invented previous-frame pixel content
}

void testSkipFlagNoPixelWrite() {
    uint32_t subRaw = 4;  // sub = 4 - 3 = 1 -> SkipFlag1
    std::vector<uint8_t> bits = buildBlockBytes(1, &subRaw, {});
    BitstreamReader br(bits.data(), bits.size());
    OwnedPlane dest;
    dest.resize(8, 8, PlanarYuvFrame::kUndecodedSentinel);
    BlockRecord rec = decodeOneBlock(br, dest.view(), nullptr, 0, 0);
    CHECK(rec.outcome == BlockOutcome::Reconstructed);
    CHECK(rec.primary == PrimaryBlockType::SubDispatch);
    CHECK(rec.sub.has_value() && *rec.sub == Case1SubType::SkipFlag1);
    bool untouched = true;
    for (size_t i = 0; i < dest.size(); ++i) untouched &= (dest.data()[i] == PlanarYuvFrame::kUndecodedSentinel);
    CHECK(untouched);
}

void testFlatDcFillBroadcastsLiteral() {
    uint32_t subRaw = 6;  // sub = 6 - 3 = 3 -> FlatDcFill
    const uint8_t literal = 0x2A;
    std::vector<uint8_t> bits = buildBlockBytes(1, &subRaw, {literal});
    BitstreamReader br(bits.data(), bits.size());
    OwnedPlane dest;
    dest.resize(8, 8, PlanarYuvFrame::kUndecodedSentinel);
    BlockRecord rec = decodeOneBlock(br, dest.view(), nullptr, 0, 0);
    CHECK(rec.outcome == BlockOutcome::Reconstructed);
    CHECK(rec.sub.has_value() && *rec.sub == Case1SubType::FlatDcFill);
    bool allLiteral = true;
    for (size_t i = 0; i < dest.size(); ++i) allLiteral &= (dest.data()[i] == literal);
    CHECK(allLiteral);
}

void testTwoLiteralPatternReconstructs16x16Block() {
    // sub = 8 - 3 = 5 -> TwoLiteralPattern. spec-bink-codec.md Sec5.3's
    // extended paragraph (CONFIRMED by live execution, 2026-10-09): 2 literal
    // bytes, then exactly 8 raw control bytes, each giving 4 two-bit fields
    // selecting kTwoLiteralBlendTable1/2 - producing a fixed 16x16-pixel
    // block (see twoLiteralPattern16x16()'s own doc comment for the full
    // grammar and this test's hand-computed expectations).
    uint32_t subRaw = 8;
    const uint8_t literal1 = 0x10;
    const uint8_t literal2 = 0xF0;
    // 8 control bytes: byte k has its 4 two-bit fields set to (k%4, (k+1)%4,
    // (k+2)%4, (k+3)%4) low-to-high so every idx value (0-3) appears at
    // least once across the block, exercising all 4 table entries.
    std::initializer_list<uint8_t> controlBytes = {
        0b11100100,  // fields (LSB-first 2 bits each): 0,1,2,3
        0b00011110,  // fields: 2,3,0,1
        0b01001011,  // fields: 3,2,1,0
        0b10110001,  // fields: 1,0,3,2
        0b11100100, 0b00011110, 0b01001011, 0b10110001,
    };
    // NOT buildBlockBytes() here - its own bytes() pads to 4 bytes minimum,
    // which would insert a fake padding byte BEFORE this test's own 8 real
    // control bytes (3 real bytes so far: dispatch nibbles + literal1, so
    // padToAtLeast=4 would add exactly one bogus byte right where control
    // byte 0 needs to be) - same reasoning testRawCopyDualRowDuplicateSub6
    // above already documents for its own 33-real-byte case. Build the whole
    // 11-real-byte sequence (1 dispatch byte + 2 literals + 8 control bytes)
    // directly via BitPacker instead, padding only past the real content.
    BitPacker packer;
    packer.appendBits(1u, 4);
    packer.appendBits(subRaw, 4);
    packer.appendByte(literal1);
    packer.appendByte(literal2);
    for (uint8_t b : controlBytes) packer.appendByte(b);
    std::vector<uint8_t> bits = packer.bytes(16);

    BitstreamReader br(bits.data(), bits.size());
    OwnedPlane dest;
    dest.resize(16, 16, PlanarYuvFrame::kUndecodedSentinel);
    BlockRecord rec = decodeOneBlock(br, dest.view(), nullptr, 0, 0);
    CHECK(rec.outcome == BlockOutcome::Reconstructed);
    CHECK(rec.primary == PrimaryBlockType::SubDispatch);
    CHECK(rec.sub.has_value() && *rec.sub == Case1SubType::TwoLiteralPattern);

    // Hand-computed expectation, reproducing twoLiteralPattern16x16()'s own
    // CHOSEN bit-order/column-order exactly: for control byte `controlBytes[rowPair]`,
    // field = (byte >> (field*2)) & 3 gives idx; the blended dword's byte i
    // (LSB-first) is the pixel at column field*4+i, written identically to
    // BOTH rows of the pair.
    auto expectedPixel = [&](uint8_t idx, int i) -> uint8_t {
        const uint32_t l1b = static_cast<uint32_t>(literal1) * 0x01010101u;
        const uint32_t l2b = static_cast<uint32_t>(literal2) * 0x01010101u;
        const uint32_t blended = (kTwoLiteralBlendTable1[idx] & l2b) | (kTwoLiteralBlendTable2[idx] & l1b);
        return static_cast<uint8_t>((blended >> (8 * i)) & 0xFFu);
    };
    std::vector<uint8_t> controlByteVec(controlBytes.begin(), controlBytes.end());
    bool allMatch = true;
    for (int rowPair = 0; rowPair < 8; ++rowPair) {
        const uint8_t cb = controlByteVec[static_cast<size_t>(rowPair)];
        for (int field = 0; field < 4; ++field) {
            const uint8_t idx = static_cast<uint8_t>((cb >> (field * 2)) & 3u);
            for (int i = 0; i < 4; ++i) {
                const uint8_t expected = expectedPixel(idx, i);
                const int x = field * 4 + i;
                const int y0 = rowPair * 2;
                allMatch &= (dest.view().at(x, y0) == expected);
                allMatch &= (dest.view().at(x, y0 + 1) == expected);
            }
        }
    }
    CHECK(allMatch);

    // Sanity: idx=0 must be solid literal1, idx=3 must be solid literal2 -
    // the two "unambiguous" table entries, independent of this test's own
    // byte-order choice (a wrong byte/field order would still fail THIS
    // check, since both literal bytes are identical across all 4 lanes of
    // their own broadcast).
    CHECK(expectedPixel(0, 0) == literal1 && expectedPixel(0, 3) == literal1);
    CHECK(expectedPixel(3, 0) == literal2 && expectedPixel(3, 3) == literal2);
}

void testGiantEngineFamilyIsOpenHook() {
    // Primary 9 is CONFIRMED (Sec5.3b) and excluded here - its own dedicated
    // test is testPrimary9BlockCopyReconstructed() below. Primary 2/3 (the
    // former "never individually read" cases) are now individually named by
    // Sec5.3b too but remain OPEN hooks for the data-gap reasons
    // block_dispatch.h's own top comment cites per case - included here
    // alongside 4-8 since all seven still resolve to BlockOutcome::OpenHook.
    for (uint32_t primary = 2; primary <= 8; ++primary) {
        std::vector<uint8_t> bits = buildBlockBytes(primary, nullptr, {});
        BitstreamReader br(bits.data(), bits.size());
        OwnedPlane dest;
        dest.resize(8, 8, PlanarYuvFrame::kUndecodedSentinel);
        BlockRecord rec = decodeOneBlock(br, dest.view(), nullptr, 0, 0);
        CHECK(rec.outcome == BlockOutcome::OpenHook);
    }
}

void testPrimary9BlockCopyReconstructed() {
    // Sec5.3b's own closing case-by-case table: "0, 9 | Plain block copy |
    // No - previous frame" - primary 9 must behave identically to primary 0.
    std::vector<uint8_t> bits = buildBlockBytes(9 /*PlainBlockCopy9*/, nullptr, {});
    BitstreamReader br(bits.data(), bits.size());

    OwnedPlane prev, dest;
    prev.resize(8, 8, 0);
    dest.resize(8, 8, PlanarYuvFrame::kUndecodedSentinel);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) prev.view().at(x, y) = static_cast<uint8_t>(0x80 + y * 8 + x);

    PlaneView prevView = prev.view();
    BlockRecord rec = decodeOneBlock(br, dest.view(), &prevView, 0, 0);
    CHECK(rec.outcome == BlockOutcome::Reconstructed);
    CHECK(rec.primary == PrimaryBlockType::PlainBlockCopy9);
    bool allMatch = true;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) allMatch &= (dest.view().at(x, y) == prev.view().at(x, y));
    CHECK(allMatch);
}

void testRawCopyDualRowDuplicateSub6() {
    // sub = 9 - 3 = 6 -> RawCopyDualRowDup6 (spec-bink-codec.md Sec5.3b).
    // 32 literal bytes (4 row-pairs x 2 column-groups x 4 bytes/dword),
    // sequential values so each dword-write's 4 source bytes are easy to
    // identify in the assertion below.
    uint32_t subRaw = 9;
    std::initializer_list<uint8_t> literals = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                                                16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
    // NOT buildBlockBytes() here (its own packer.bytes() only pads to 4) -
    // this test's real content is 33 bytes (1 dispatch + 32 literal), and
    // BitstreamReader::readBits() always refills a FULL dword (4 bytes) at
    // a time even when fewer bits are actually needed, so the very last
    // aligned-byte read would reach 3 bytes past the real 33-byte end and
    // spuriously set ranOutOfData() - pad well past the last real dword
    // refill boundary so that correctly-behaving code is never misread as
    // having run out of data.
    BitPacker packer;
    packer.appendBits(1u, 4);
    packer.appendBits(subRaw, 4);
    for (uint8_t b : literals) packer.appendByte(b);
    std::vector<uint8_t> bits = packer.bytes(40);
    BitstreamReader br(bits.data(), bits.size());
    OwnedPlane dest;
    dest.resize(8, 8, PlanarYuvFrame::kUndecodedSentinel);
    BlockRecord rec = decodeOneBlock(br, dest.view(), nullptr, 0, 0);
    CHECK(rec.outcome == BlockOutcome::Reconstructed);
    CHECK(rec.sub.has_value() && *rec.sub == Case1SubType::RawCopyDualRowDup6);

    // This file's own DERIVED traversal order (rawCopyDualRowDuplicate8x8's
    // doc comment): row-pair-major, column-group-minor, both rows of a pair
    // identical. literalIndex = (rowPair*2 + colGroup)*4 + byteInDword.
    bool allMatch = true;
    for (int rowPair = 0; rowPair < 4; ++rowPair) {
        for (int colGroup = 0; colGroup < 2; ++colGroup) {
            for (int i = 0; i < 4; ++i) {
                const uint8_t expected = static_cast<uint8_t>((rowPair * 2 + colGroup) * 4 + i);
                const int x = colGroup * 4 + i;
                const int y0 = rowPair * 2;
                allMatch &= (dest.view().at(x, y0) == expected);
                allMatch &= (dest.view().at(x, y0 + 1) == expected);
            }
        }
    }
    CHECK(allMatch);
}

void testDcAcIdctEngine2ProducesPixelsViaAcTreeAndIdct() {
    // sub = 5 - 3 = 2 -> DcAcIdctEngine2 (spec-bink-codec.md Sec5.3i,
    // 2026-10-10). The 16-bit DC literal is little-endian per the spec's
    // own "cx = word[eax]; eax += 2" - literal bytes {0x4C, 0x02} -> 0x024C.
    //
    // UPDATED 2026-10-10 (spec-bink-codec.md Sec5.3j): the AC tree decode
    // and IDCT are no longer OPEN - this sub-case now produces real pixels.
    // A generous zero-padded tail follows the DC literal (the AC tree
    // decoder's own real bit-cost is data-dependent and not independently
    // verified - see ac_tree_decoder.h's own top comment - so this test
    // checks the real observable behaviour, not a hand-derived bit count:
    // the outcome is Reconstructed, the DC literal is read correctly, real
    // pixel bytes get written, and re-running against the SAME bytes is
    // deterministic).
    uint32_t subRaw = 5;
    BitPacker packer;
    packer.appendBits(1u, 4);
    packer.appendBits(subRaw, 4);
    packer.appendByte(0x4C);
    packer.appendByte(0x02);
    std::vector<uint8_t> bits = packer.bytes(64);
    BitstreamReader br(bits.data(), bits.size());
    OwnedPlane dest;
    dest.resize(8, 8, PlanarYuvFrame::kUndecodedSentinel);
    BlockRecord rec = decodeOneBlock(br, dest.view(), nullptr, 0, 0);
    CHECK(rec.outcome == BlockOutcome::Reconstructed);  // Sec5.3j CLOSES the AC tree decode + IDCT gaps
    CHECK(rec.sub.has_value() && *rec.sub == Case1SubType::DcAcIdctEngine2);
    CHECK(rec.dcLiteral.has_value());
    CHECK(*rec.dcLiteral == 0x024C);

    bool anyWritten = false;
    for (size_t i = 0; i < dest.size(); ++i) {
        if (dest.data()[i] != PlanarYuvFrame::kUndecodedSentinel) anyWritten = true;
    }
    CHECK(anyWritten);

    BitstreamReader br2(bits.data(), bits.size());
    OwnedPlane dest2;
    dest2.resize(8, 8, PlanarYuvFrame::kUndecodedSentinel);
    BlockRecord rec2 = decodeOneBlock(br2, dest2.view(), nullptr, 0, 0);
    CHECK(rec2.outcome == BlockOutcome::Reconstructed);
    bool same = true;
    for (size_t i = 0; i < dest.size(); ++i) same &= (dest.data()[i] == dest2.data()[i]);
    CHECK(same);
}

void testDcAcIdctEngine2ReadsDcLiteralFromDcArrayByIndexNotMainReader() {
    // fix/bink-dc-array-indexing task (2026-10-11), spec-bink-codec.md
    // Sec5.3p: "descriptor L" is NOT a separate byte-cursor stream (the
    // RETRACTED model the superseded descriptor_l_cursor.h implemented,
    // tested here before this task) - it IS `0x1001a160`'s own per-row DC
    // delta-accumulation array, already fully decoded for the whole row
    // BEFORE any block reads from it, consumed by a plain ARRAY INDEX
    // (`blockCol`). This test proves that split: `br` holds ONLY the
    // dispatch nibbles + a zero-padded tail for the AC tree decode (NOT any
    // DC literal bytes - unlike testDcAcIdctEngine2ProducesPixelsViaAcTreeAndIdct()
    // above, which exercises the legacy/no-dcArray fallback path that still
    // reads DC off `br` directly), while a COMPLETELY SEPARATE, already-
    // decoded DcDeltaArray supplies the real 0x024C DC literal at index 0
    // (column 0 -> index 0, Sec5.3p's own "cursor starts at offset 0").
    // If the DC read were still wired to `br` (the bug the PRIOR task
    // fixed) or to a byte-cursor instead of an array index (this task's own
    // fix), this test's own dcLiteral would come out wrong/zero instead of
    // the array's real 0x024C value at index `blockCol`=0.
    uint32_t subRaw = 5;
    BitPacker packer;
    packer.appendBits(1u, 4);
    packer.appendBits(subRaw, 4);
    // No DC literal bytes here - `br` only carries the dispatch nibbles
    // plus whatever zero-padded tail the AC tree decode/quant nibble read
    // past them (BitPacker::bytes() below pads to 64 bytes).
    std::vector<uint8_t> dispatchBits = packer.bytes(64);
    BitstreamReader br(dispatchBits.data(), dispatchBits.size());

    // A hand-built, already-decoded row array - column 0 (this block's own
    // blockCol) holds the real DC literal 0x024C; later indices are
    // deliberately different values so an off-by-one index bug would be
    // caught.
    const DcDeltaArray rowArray(std::vector<uint16_t>{0x024C, 0x1111, 0x2222});

    OwnedPlane dest;
    dest.resize(8, 8, PlanarYuvFrame::kUndecodedSentinel);
    BlockRecord rec = decodeOneBlock(br, dest.view(), nullptr, 0, /*blockCol=*/0, nullptr, nullptr, &rowArray);
    CHECK(rec.outcome == BlockOutcome::Reconstructed);
    CHECK(rec.sub.has_value() && *rec.sub == Case1SubType::DcAcIdctEngine2);
    CHECK(rec.dcLiteral.has_value());
    CHECK(*rec.dcLiteral == 0x024C);  // from rowArray[0], NOT from `br`'s own (DC-literal-less) bytes

    // Index-by-column: the SAME array, read at blockCol=1, must return the
    // SECOND word, not re-derive/re-read the first.
    BitPacker packer2;
    packer2.appendBits(1u, 4);
    packer2.appendBits(subRaw, 4);
    std::vector<uint8_t> dispatchBits2 = packer2.bytes(64);
    BitstreamReader br2(dispatchBits2.data(), dispatchBits2.size());
    OwnedPlane dest2;
    dest2.resize(8, 8, PlanarYuvFrame::kUndecodedSentinel);
    BlockRecord rec2 = decodeOneBlock(br2, dest2.view(), nullptr, 0, /*blockCol=*/1, nullptr, nullptr, &rowArray);
    CHECK(rec2.outcome == BlockOutcome::Reconstructed);
    CHECK(rec2.dcLiteral.has_value());
    CHECK(*rec2.dcLiteral == 0x1111);

    // Bound check: a block whose column index falls past the array's own
    // decoded word count must report SourceExhausted, not silently read OOB
    // or wrap (dc_delta_array.h's hasIndex()).
    BitPacker packer3;
    packer3.appendBits(1u, 4);
    packer3.appendBits(subRaw, 4);
    std::vector<uint8_t> dispatchBits3 = packer3.bytes(64);
    BitstreamReader br3(dispatchBits3.data(), dispatchBits3.size());
    OwnedPlane dest3;
    dest3.resize(8, 8, PlanarYuvFrame::kUndecodedSentinel);
    BlockRecord rec3 = decodeOneBlock(br3, dest3.view(), nullptr, 0, /*blockCol=*/3, nullptr, nullptr, &rowArray);
    CHECK(rec3.outcome == BlockOutcome::SourceExhausted);
}

void testDecodeRowDcArrayChunkAlgorithm() {
    // UPDATED 2026-10-11 (fix/bink-dc-array-value0-boundary-5.3u task,
    // spec-bink-codec.md Sec5.3u): the PRIOR version of this test hand-built
    // a bitstream where value[0] itself went through the chunk loop's own
    // nibble=0 flat-fill - that was the exact bug Sec5.3u's own live trace
    // found (value[0] must NOT read a class nibble at all). This test is
    // now rewritten to match the corrected, resolved algorithm: value[0] is
    // the FIXED-bound path (a 10-bit field - dc_prestage_sibling_gap.h's
    // kDcSiblingMagnitudeWidth, the real algorithm's own overflow-sanity
    // field, value not otherwise checked here - followed by an 11-bit
    // direct value, dc_delta_array.h's kDcValue0FieldWidth), and only
    // values[1+] go through Sec5.3m's own confirmed per-chunk algorithm,
    // address-exact: a 4-bit class nibble, then (nibble!=0) up to 8 values
    // of [nibble-bit magnitude, +1 sign bit if magnitude!=0], DPCM-
    // accumulated onto a running "previous committed value" that starts at
    // value[0] itself (Sec5.3u item 3) - or (nibble==0) a flat-fill of the
    // unchanged previous value, no further bitstream consumption. Hand-
    // built here bit-for-bit, deterministically - not a claim about any one
    // real file's own actual chunk content (the real worked example is
    // tests/real_bink_dc_value0_boundary_test.cpp, against real data).
    BitPacker packer;
    // Leading count (2026-10-11, fix/bink-dc-array-519-word-commit-run task,
    // spec-bink-codec.md Sec5.3n): a 10-bit field that IS the call's own
    // real word count directly (no longer discarded as "sanity" - Sec5.3n's
    // own leading-count idiom) - set to 12 to match this test's own 12-word
    // expectation below. Then value[0]: fixed-bound path, 11-bit direct
    // value = 50.
    packer.appendBits(12u, kDcSiblingMagnitudeWidth);
    packer.appendBits(50u, kDcValue0FieldWidth);
    // Chunk 1 (values[1..8]): nibble=0 -> flat-fill all 8 words with prev
    // (seeded at value[0]=50).
    packer.appendBits(0u, 4);
    // Chunk 2 (values[9..]): nibble=7 (7-bit magnitude), one real value:
    // magnitude=118, sign=0 (positive) -> prev becomes 50+118=168 for word
    // index 9.
    packer.appendBits(7u, 4);
    packer.appendBits(118u, 7);
    packer.appendBits(0u, 1);  // sign bit: not negated
    // Second value of chunk 2: magnitude=0 -> delta=0, sign bit skipped
    // entirely (Sec5.3m) -> prev stays 168 for word index 10.
    packer.appendBits(0u, 7);
    // Third value: magnitude=20, sign=1 (negate) -> prev becomes 168-20=148.
    packer.appendBits(20u, 7);
    packer.appendBits(1u, 1);
    std::vector<uint8_t> bits = packer.bytes(64);
    BitstreamReader br(bits.data(), bits.size());

    DcDeltaArray arr = decodeRowDcArray(br, /*minCount=*/12);
    CHECK(arr.size() >= 12);
    CHECK(arr.wordAt(0) == 50);  // value[0], fixed-bound, direct
    for (int i = 1; i <= 8; ++i) CHECK(arr.wordAt(static_cast<size_t>(i)) == 50);  // chunk 1's flat-fill
    CHECK(arr.wordAt(9) == 168);
    CHECK(arr.wordAt(10) == 168);  // magnitude==0 -> unchanged accumulator
    CHECK(arr.wordAt(11) == 148);  // 168 - 20, negated delta
}

void testGiant1RleUpsample0IsOpenHookWithNoDcLiteral() {
    // sub = 3 - 3 = 0 -> Giant1RleUpsample0. Unlike DcAcIdctEngine2 above,
    // this sub-case reads nothing at all (no confirmed field width exists
    // for its own RLE/literal token format) - dcLiteral must stay unset,
    // distinguishing it from sub=2's partial real read.
    uint32_t subRaw = 3;
    std::vector<uint8_t> bits = buildBlockBytes(1, &subRaw, {});
    BitstreamReader br(bits.data(), bits.size());
    OwnedPlane dest;
    dest.resize(8, 8, PlanarYuvFrame::kUndecodedSentinel);
    BlockRecord rec = decodeOneBlock(br, dest.view(), nullptr, 0, 0);
    CHECK(rec.outcome == BlockOutcome::OpenHook);
    CHECK(rec.sub.has_value() && *rec.sub == Case1SubType::Giant1RleUpsample0);
    CHECK(!rec.dcLiteral.has_value());
}

void testOutOfRangeDispatchDetected() {
    std::vector<uint8_t> bits = buildBlockBytes(0xA /* 10, > 9 */, nullptr, {});
    BitstreamReader br(bits.data(), bits.size());
    OwnedPlane dest;
    dest.resize(8, 8, PlanarYuvFrame::kUndecodedSentinel);
    BlockRecord rec = decodeOneBlock(br, dest.view(), nullptr, 0, 0);
    CHECK(rec.outcome == BlockOutcome::OutOfRangeDispatch);
}

void testRasterOrderStopsAtFirstOpenHook() {
    // Block (0,0): BlockCopy (reconstructed). Block (0,1): primary=2 (OPEN).
    // 16x8 plane = 2x1 grid of 8x8 blocks. Two 4-bit dispatch reads back to
    // back share ONE byte (LSB-first: block 0's primary in the low nibble,
    // block 1's primary in the high nibble) - see BitPacker's own comment.
    BitPacker packer;
    packer.appendBits(0, 4);  // block 0: primary=0 (BlockCopy)
    packer.appendBits(2, 4);  // block 1: primary=2 (OPEN)
    std::vector<uint8_t> bits = packer.bytes(8);

    BitstreamReader br(bits.data(), bits.size());
    OwnedPlane prev, dest;
    prev.resize(16, 8, 0x77);
    dest.resize(16, 8, PlanarYuvFrame::kUndecodedSentinel);
    PlaneView prevView = prev.view();
    DecodeTrace trace = decodePlaneRasterOrder(br, dest.view(), &prevView, 2, 1);

    CHECK(trace.blocks.size() == 2);
    CHECK(trace.stopReason == StopReason::HitOpenHook);
    CHECK(trace.reconstructedCount() == 1);
    CHECK(trace.openHookCount() == 1);
    auto hist = trace.histogram();
    CHECK(hist.size() == 2);
}

void testDecodeIsDeterministic() {
    uint32_t subRaw = 6;
    std::vector<uint8_t> bits = buildBlockBytes(1, &subRaw, {0x99});

    auto runOnce = [&]() {
        BitstreamReader br(bits.data(), bits.size());
        OwnedPlane dest;
        dest.resize(8, 8, PlanarYuvFrame::kUndecodedSentinel);
        BlockRecord rec = decodeOneBlock(br, dest.view(), nullptr, 0, 0);
        std::vector<uint8_t> out(dest.data(), dest.data() + dest.size());
        return std::make_pair(rec.outcome, out);
    };
    auto a = runOnce();
    auto b = runOnce();
    CHECK(a.first == b.first);
    CHECK(a.second == b.second);
}

void testYuvToRgbLimitedRangeBlackIsNearZero() {
    // spec-bink-codec.md Sec5.6: limited-range black/neutral is Y=16,
    // U=V=128 - must map to (near-)zero RGB, not the full-range 0,0,0
    // point's own different offset math would otherwise give.
    uint8_t r, g, b;
    limitedRangeYuvToRgb(16, 128, 128, r, g, b);
    CHECK(r == 0 && g == 0 && b == 0);
}

// --- entropy_prestage.h (spec-bink-codec.md Sec5.3c): the two pieces that
// ARE fully CONFIRMED, tested directly rather than only indirectly through
// decodeOneBlock()/decodeLumaPlane() below. ---

void testCaseSelectorLengthFieldBitWidthMatchesSpecExample() {
    // spec-bink-codec.md Sec5.3c's own concrete worked example: "N =
    // bit_length(Width/8 + 0x1ff) - for a 1920-wide frame, a concrete,
    // re-checkable 10-bit field." bit_length(1920/8 + 0x1ff) ==
    // bit_length(751) == 10 (751 < 1024 == 2^10, 751 >= 512 == 2^9).
    CHECK(caseSelectorLengthFieldBitWidth(1920) == 10);
    CHECK(bitLength(751) == 10);
    CHECK(bitLength(0) == 0);
    CHECK(bitLength(1) == 1);
    CHECK(bitLength(1023) == 10);
    CHECK(bitLength(1024) == 11);
}

void testCaseSelectorIdentityTableIsIdentity() {
    // spec-bink-codec.md Sec5.3c, CONFIRMED: `0x10018ca0`'s own nibble==0
    // fast path "fills a 16-byte local with the literal identity sequence
    // 0..0xF."
    const std::array<uint8_t, 16> table = caseSelectorIdentityTable();
    for (int i = 0; i < 16; ++i) {
        CHECK(table[static_cast<size_t>(i)] == static_cast<uint8_t>(i));
    }
}

void testBuildCaseSelectorTableMergeAdvancesByActualConsumedBits() {
    // spec-bink-codec.md Sec5.3g's merge-network subtlety (the exact root
    // cause behind this project's own 580-vs-588-bit/LEN=0-vs-528 bug, fixed
    // by this task): mergeBitSelected()'s selector field (3/7/15 bits
    // nominal) is only a MAXIMUM peek width - the real bit cursor must
    // advance by the merge's own returned ACTUAL consumed-bit count, which
    // can be LESS than that nominal width whenever one of the two input
    // sides exhausts early. Hand-built, fully by-hand-simulated mode==1
    // (single merge level, 4 groups of n=2) exercise: nibble=1, flag=0,
    // mode=1, pairwise-swap loop all-0 (temp == identity 0..15), then 4
    // group fields whose real (post-merge) consumed-bit counts are
    // 2,2,2,3 - NOT the uniform nominal 3,3,3,3 an "always eat the nominal
    // width" implementation would consume (group 3 happens to consume the
    // full nominal 3, exercising that no-early-exhaust case too).
    //
    // Bit-by-bit trace (chronological consumption order, bit index 15
    // onward - indices 0-14 are nibble=1/flag=0/mode=1/8 pairwise-swap
    // bits=0, giving temp == identity {0..15}):
    //   g0 (Q=temp[0,1]={0,1}, P=temp[2,3]={2,3}), bits 15,16 = 1,1 -> take
    //     P,P -> table16[0..3] = {2,3,0,1}, actual consumed = 2 (not 3)
    //   g1 (Q=temp[4,5]={4,5}, P=temp[6,7]={6,7}), bits 17,18 = 0,0 -> take
    //     Q,Q -> table16[4..7] = {4,5,6,7}, actual consumed = 2 (not 3)
    //   g2 (Q=temp[8,9]={8,9}, P=temp[10,11]={10,11}), bits 19,20 = 1,1 ->
    //     take P,P -> table16[8..11] = {10,11,8,9}, actual consumed = 2
    //   g3 (Q=temp[12,13]={12,13}, P=temp[14,15]={14,15}), bits 21,22,23 =
    //     0,1,0 -> take Q,P,Q -> table16[12..15] = {12,14,13,15}, actual
    //     consumed = 3 (== nominal here)
    // Real total consumed for the whole call = 15 + (2+2+2+3) = 24 bits. A
    // build that always eats the nominal 3 per group would instead (a)
    // misalign every field from g1 onward by the bits g0 over-reads,
    // producing a DIFFERENT table16 past index 3, and (b) leave the cursor
    // at bit 27 (15 + 3*4), not 24.
    BitPacker packer;
    packer.appendBits(1, 4);  // nibble = 1 (nonzero -> merge-network path, not identity)
    packer.appendBits(0, 1);  // flag = 0 (merge network, not explicit-prefix)
    packer.appendBits(1, 2);  // mode = 1 (one merge level deep)
    for (int i = 0; i < 8; ++i) packer.appendBits(0, 1);  // pairwise-swap loop: temp = identity 0..15
    const int bits15to25[] = {1, 1, 0, 0, 1, 1, 0, 1, 0, 1, 1};  // chronological bits 15..25
    for (int b : bits15to25) packer.appendBits(static_cast<uint32_t>(b), 1);
    std::vector<uint8_t> bits = packer.bytes(/*padToAtLeast=*/32);
    BitstreamReader br(bits.data(), bits.size());

    BuiltTable built = buildCaseSelectorTable(br);
    const std::array<uint8_t, 16> expected = {2, 3, 0, 1, 4, 5, 6, 7, 10, 11, 8, 9, 12, 14, 13, 15};
    CHECK(built.table16 == expected);

    // The real cursor must now sit exactly at bit index 24 (15 header bits +
    // 2+2+2+3 = 9 real group-merge bits), NOT at 27 (15 + 3+3+3+3) where an
    // "always consume the nominal width" implementation would leave it.
    // bits[24]=1, bits[25]=1 were placed as a 2-bit sentinel exactly at that
    // expected boundary (LSB-first value 3) - a buggy, 3-bits-too-far cursor
    // would instead land on the all-zero padding and read 0, not 3.
    CHECK(br.readBits(2) == 3u);
}

// Hand-builds a minimal per-row preamble for CaseSelectorScratchSource's
// FULL spec-bink-codec.md Sec5.3g algorithm: kSiblingTableBuildsPerBlockRow
// (23) build-table calls, each reading nibble==0 (the CONFIRMED trivial
// identity/classByte==4 fast path, 4 bits each - cheapest possible table
// build, keeps these tests' own bit arithmetic simple and fully by-hand-
// verifiable), followed by the LEN field (packer.appendBits(lenValue, 10) -
// matches caseSelectorLengthFieldBitWidth(1920) == 10, Sec5.3c's own worked
// example) and the whole-call mode bit. Callers append whatever the mode
// bit's own payload (flat-fill value, or table-driven symbols) needs next.
void appendPreStagePreambleAndLen(BitPacker& packer, uint32_t lenValue, uint32_t modeBit) {
    for (int i = 0; i < kSiblingTableBuildsPerBlockRow; ++i) {
        packer.appendBits(0, 4);  // nibble==0 on every one of the 23 calls
    }
    packer.appendBits(lenValue, 10);
    packer.appendBits(modeBit, 1);
}

void testCaseSelectorScratchSourceFlatFillPath() {
    // mode_bit==1 (spec-bink-codec.md Sec5.3g): "reads a fresh 4-bit fill
    // value... memset(out, fill_value, remaining)." A hand-built,
    // deterministic exercise of the REAL algorithm (not the prior pass's
    // "stop after 2 reads" stand-in) via beginBlockRow() + next(), exactly
    // how frame_decoder.h's own row loop drives this class.
    BitPacker packer;
    appendPreStagePreambleAndLen(packer, /*lenValue=*/5, /*modeBit=*/1);
    packer.appendBits(7, 4);  // fillValue = 7
    std::vector<uint8_t> bits = packer.bytes(/*padToAtLeast=*/32);
    BitstreamReader br(bits.data(), bits.size());
    CaseSelectorScratchSource source(br, 1920);

    CHECK(source.beginBlockRow() == CaseSelectorStatus::Ok);
    for (int i = 0; i < 5; ++i) {
        uint8_t outByte = 0xFF;
        CHECK(source.next(outByte) == CaseSelectorStatus::Ok);
        CHECK(outByte == 7);
    }
}

void testCaseSelectorScratchSourceTableDrivenLiteralAndRunLength() {
    // mode_bit==0 (spec-bink-codec.md Sec5.3g): the table-driven canonical-
    // Huffman expansion. With call #1's own nibble==0 (identity table16,
    // classByte==4), kLutBlob's own nibble-0 LUT is a trivial fixed 4-bit
    // code (every packed byte is 0x40|i -> code_len=4, raw_symbol=i, see
    // entropy_prestage.h's own top comment) - so each "symbol" in this test
    // is just a plain 4-bit value read directly as raw_symbol, translated
    // to itself by the identity table16 (bl == raw_symbol). Symbol 3 (< 0xC)
    // is a literal; symbol 0xC (>= 0xC) replicates the LAST literal
    // kRunLength[0xC-0xC]==4 times (spec-bink-static-tables.md's own
    // region_bd40 bytes 20-23, independently re-verified by this task).
    // LEN=5 exactly matches 1 literal + a 4-byte run, so the loop ends
    // cleanly (no truncation path exercised here).
    BitPacker packer;
    appendPreStagePreambleAndLen(packer, /*lenValue=*/5, /*modeBit=*/0);
    packer.appendBits(3, 4);     // symbol: raw_symbol=3 -> bl=3 (<0xC) -> literal 3
    packer.appendBits(0xC, 4);   // symbol: raw_symbol=0xC -> bl=0xC -> run of 4x last-literal (3)
    std::vector<uint8_t> bits = packer.bytes(/*padToAtLeast=*/32);
    BitstreamReader br(bits.data(), bits.size());
    CaseSelectorScratchSource source(br, 1920);

    CHECK(source.beginBlockRow() == CaseSelectorStatus::Ok);
    const uint8_t expected[5] = {3, 3, 3, 3, 3};
    for (int i = 0; i < 5; ++i) {
        uint8_t outByte = 0xFF;
        CHECK(source.next(outByte) == CaseSelectorStatus::Ok);
        CHECK(outByte == expected[i]);
    }
}

void testCaseSelectorScratchSourceLenZeroPoisonsPermanently() {
    // spec-bink-codec.md Sec5.3g's own CONFIRMED poison path
    // (0x10019586-0x1001958e): a decoded LEN==0 sets the permanent "-4
    // stall" - every later call for this SAME descriptor must report
    // Poisoned forever, never silently recover.
    BitPacker packer;
    for (int i = 0; i < kSiblingTableBuildsPerBlockRow; ++i) packer.appendBits(0, 4);
    packer.appendBits(0u, 10);  // LEN == 0
    std::vector<uint8_t> bits = packer.bytes(/*padToAtLeast=*/32);
    BitstreamReader br(bits.data(), bits.size());
    CaseSelectorScratchSource source(br, 1920);

    CHECK(source.beginBlockRow() == CaseSelectorStatus::Poisoned);
    uint8_t outByte = 0;
    CHECK(source.next(outByte) == CaseSelectorStatus::Poisoned);
    CHECK(source.beginBlockRow() == CaseSelectorStatus::Poisoned);  // still poisoned on a later row
}

void testCaseSelectorScratchSourceSourceExhaustedOnEmptyInput() {
    // Real callers always call beginBlockRow() first (see its own doc
    // comment) - on a truly empty bitstream, that call's own first of the
    // 23 per-row table-build reads immediately runs off the real data,
    // giving a genuine SourceExhausted (br.ranOutOfData() becomes true from
    // a real attempted read here, unlike
    // testCaseSelectorScratchSourceNextWithoutBeginBlockRowIsSafe() below,
    // whose SourceExhausted comes from next()'s own defensive "no table
    // built yet" guard without ever touching `br`).
    BitstreamReader br(nullptr, 0);
    CaseSelectorScratchSource source(br, 1920);
    const CaseSelectorStatus status = source.beginBlockRow();
    CHECK(status == CaseSelectorStatus::SourceExhausted);
    CHECK(br.ranOutOfData());
}

void testCaseSelectorScratchSourceNextWithoutBeginBlockRowIsSafe() {
    // Calling next() before this source's own required beginBlockRow() has
    // ever run (a misuse of the class's contract - see beginBlockRow()'s
    // own doc comment) must not crash (classByte would default to 0,
    // and peekBits(0) throws) - reported as SourceExhausted rather than
    // invoking undefined behaviour.
    const uint8_t bytes[8] = {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55};
    BitstreamReader br(bytes, sizeof(bytes));
    CaseSelectorScratchSource source(br, 1920);
    uint8_t outByte = 0;
    CHECK(source.next(outByte) == CaseSelectorStatus::SourceExhausted);
}

void testDecodeOneBlockWithCaseSelectorUsesPreStageDecodedByte() {
    // block_dispatch.h's own "ENTROPY PRE-STAGE WIRING": once beginBlockRow()
    // has primed a CaseSelectorScratchSource (as frame_decoder.h's own row
    // loop now does for every row), decodeOneBlock()'s primary dispatch byte
    // must be the pre-stage's own REAL decoded byte, not a raw bitstream
    // nibble - here, the table-driven literal "3" from
    // testCaseSelectorScratchSourceTableDrivenLiteralAndRunLength() above,
    // which is PrimaryBlockType::Giant1FullResTwin (still an OPEN hook body,
    // but a real, in-range, non-guessed dispatch value).
    BitPacker packer;
    appendPreStagePreambleAndLen(packer, /*lenValue=*/5, /*modeBit=*/0);
    packer.appendBits(3, 4);
    packer.appendBits(0xC, 4);
    std::vector<uint8_t> bits = packer.bytes(/*padToAtLeast=*/32);
    BitstreamReader br(bits.data(), bits.size());
    CaseSelectorScratchSource source(br, 1920);
    CHECK(source.beginBlockRow() == CaseSelectorStatus::Ok);

    OwnedPlane dest;
    dest.resize(8, 8, PlanarYuvFrame::kUndecodedSentinel);
    BlockRecord rec = decodeOneBlock(br, dest.view(), nullptr, 0, 0, &source);
    CHECK(rec.rawPrimaryNibble == 3);
    CHECK(rec.primary == PrimaryBlockType::Giant1FullResTwin);
    CHECK(rec.outcome == BlockOutcome::OpenHook);
}

void testDecodeOneBlockWithoutCaseSelectorKeepsLegacyRawNibbleBehaviour() {
    // The caseSelector == nullptr default must preserve this function's
    // ORIGINAL behaviour exactly (every other test in this file already
    // exercises this implicitly; this test names the guarantee directly) -
    // a hand-built primary=0 nibble read straight off `br`, no pre-stage
    // involved at all.
    const uint8_t primary0Bits[4] = {0x00, 0x00, 0x00, 0x00};
    BitstreamReader br(primary0Bits, sizeof(primary0Bits));
    OwnedPlane dest;
    dest.resize(8, 8, PlanarYuvFrame::kUndecodedSentinel);
    BlockRecord rec = decodeOneBlock(br, dest.view(), nullptr, 0, 0);  // caseSelector defaulted to nullptr
    CHECK(rec.outcome == BlockOutcome::Reconstructed);
    CHECK(rec.primary == PrimaryBlockType::BlockCopy);
    CHECK(rec.rawPrimaryNibble == 0);
}

void testDecodeLumaPlaneWiresRealEntropyPreStage() {
    // End-to-end: decodeLumaPlane() (frame_decoder.h) must call
    // CaseSelectorScratchSource::beginBlockRow() once for this 1-block-row
    // frame (FrameDimensions::fromRoundedLuma(16,8) -> a 2-block-column x
    // 1-block-row grid, caseSelectorLengthFieldBitWidth(16) == 10, same as
    // the 1920-wide worked example - see appendPreStagePreambleAndLen()'s
    // own comment) BEFORE either block's own decodeOneBlock(), then feed
    // both blocks' primary dispatch bytes from that ONE pre-stage refill -
    // confirming the Sec5.3g "once per block-row, not once per block" row
    // sequencing (frame_decoder.h's own "ENTROPY PRE-STAGE ROW SEQUENCING"
    // note) is really wired into the real-data entry point, not just
    // reachable via decodeOneBlock()/beginBlockRow() in isolation.
    BitPacker packer;
    appendPreStagePreambleAndLen(packer, /*lenValue=*/2, /*modeBit=*/1);
    packer.appendBits(0, 4);  // fillValue = 0 -> both blocks dispatch as primary=0 (BlockCopy)
    std::vector<uint8_t> bits = packer.bytes(/*padToAtLeast=*/32);
    BitstreamReader br(bits.data(), bits.size());
    PlanarYuvFrame frame;
    frame.allocate(FrameDimensions::fromRoundedLuma(16, 8));
    // hasLeadingSubChunkSpan=false: this hand-built bitstream starts directly
    // with the 23-call pre-stage preamble (appendPreStagePreambleAndLen()
    // above), with no leading 4-byte span field prepended - matching this
    // function's ORIGINAL contract (see frame_decoder.h's own top comment
    // for why that field is a per-file, caller-declared stance, not a
    // universal default).
    DecodeTrace trace = decodeLumaPlane(br, frame, nullptr, /*hasLeadingSubChunkSpan=*/false);
    CHECK(trace.blocks.size() == 2);
    CHECK(trace.stopReason == StopReason::CompletedAllBlocks);
    for (const auto& b : trace.blocks) {
        CHECK(b.outcome == BlockOutcome::Reconstructed);
        CHECK(b.rawPrimaryNibble == 0);
        CHECK(b.primary == PrimaryBlockType::BlockCopy);
    }
}

void testUnwrittenPixelsRenderAsSyntheticTint() {
    FrameDimensions dims = FrameDimensions::fromRoundedLuma(16, 8);
    PlanarYuvFrame frame;
    frame.allocate(dims);
    frame.chromaA().fillFlat(128);
    frame.chromaB().fillFlat(128);
    std::vector<uint8_t> mask(16 * 8, 0);  // nothing written
    std::vector<uint8_t> rgba = renderFrameToRgba(frame, &mask);
    CHECK(rgba[0] == 255 && rgba[1] == 0 && rgba[2] == 255);  // synthetic magenta, not a real colour
}

}  // namespace

int main() {
    // 2026-10-10 (runtime data loader task): this file's own
    // testCaseSelectorScratchSource*/testBuildCaseSelectorTable*/
    // testDecodeOneBlockWithCaseSelector*/testDecodeLumaPlaneWiresReal*
    // tests below all hardcode expected output bytes derived from the REAL
    // Bink case-selector static table values (kClassByte/kLutBlob/etc. -
    // see entropy_prestage.h's own top comment) - every one of them reaches
    // CaseSelectorScratchSource::beginBlockRow(), which reports
    // CaseSelectorStatus::TablesNotLoaded first whenever
    // caseSelectorTablesAvailable() is false, desyncing every one of those
    // tests' own hardcoded expectations. Rather than asserting and FAILING
    // against a zeroed placeholder in that case, attempt the runtime loader
    // first (a cheap no-op when the tables are already compiled in), then
    // SKIP (exit 77) the whole binary if they are still unavailable -
    // same convention this project's real_bink_*_test family already uses.
    sr3runtimedata::ensureRuntimeDataLoaded();
    if (!caseSelectorTablesAvailable()) {
        std::cout << "synthetic_bink_codec_test: SKIPPED - Bink case-selector static tables not "
                      "available (no compiled-in data, and the runtime loader found no verified "
                      "data at the configured game install path) - this file's own tests hardcode "
                      "expected output derived from the real table values, so there is no "
                      "meaningful synthetic-only subset to fall back to.\n";
        return 77;
    }

    testMd5SelfCheck();
    testBitstreamReaderLsbFirstNibbles();
    testBitstreamReaderMixedWidths();
    testBitstreamReaderRefillAcrossDwordBoundary();
    testBitstreamReaderAlignToByte();
    testBitstreamReaderRanOutOfData();
    testBlockCopyReconstructed();
    testBlockCopyWithNoPreviousFrameLeavesSentinel();
    testSkipFlagNoPixelWrite();
    testFlatDcFillBroadcastsLiteral();
    testTwoLiteralPatternReconstructs16x16Block();
    testGiantEngineFamilyIsOpenHook();
    testPrimary9BlockCopyReconstructed();
    testRawCopyDualRowDuplicateSub6();
    testDcAcIdctEngine2ProducesPixelsViaAcTreeAndIdct();
    testDcAcIdctEngine2ReadsDcLiteralFromDcArrayByIndexNotMainReader();
    testDecodeRowDcArrayChunkAlgorithm();
    testGiant1RleUpsample0IsOpenHookWithNoDcLiteral();
    testOutOfRangeDispatchDetected();
    testRasterOrderStopsAtFirstOpenHook();
    testDecodeIsDeterministic();
    testYuvToRgbLimitedRangeBlackIsNearZero();
    testUnwrittenPixelsRenderAsSyntheticTint();
    testCaseSelectorLengthFieldBitWidthMatchesSpecExample();
    testCaseSelectorIdentityTableIsIdentity();
    testBuildCaseSelectorTableMergeAdvancesByActualConsumedBits();
    testCaseSelectorScratchSourceFlatFillPath();
    testCaseSelectorScratchSourceTableDrivenLiteralAndRunLength();
    testCaseSelectorScratchSourceLenZeroPoisonsPermanently();
    testCaseSelectorScratchSourceSourceExhaustedOnEmptyInput();
    testCaseSelectorScratchSourceNextWithoutBeginBlockRowIsSafe();
    testDecodeOneBlockWithCaseSelectorUsesPreStageDecodedByte();
    testDecodeOneBlockWithoutCaseSelectorKeepsLegacyRawNibbleBehaviour();
    testDecodeLumaPlaneWiresRealEntropyPreStage();

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_bink_codec_test: all checks passed\n";
    return 0;
}
