#pragma once

// sr3binkcodec - Bink 1.x per-block bitstream reader (video decode core,
// binkw32.dll address space 0x10xxxxxx; see spec-bink-codec.md's own Scope
// line for why this file's addresses are in that space, not the main exe's).
//
// Matches the CONFIRMED accumulator shape from spec-bink-codec.md Sec5.3a:
// "a shared eax(value)/esi(bits-left)/edi(source-ptr) accumulator, refilled
// a full 32-bit dword at a time, LSB-first - but drawn at four different
// fixed widths depending on context (1-bit, 3-bit, 4-bit, 7-bit), plus
// several byte-aligned (non-bit-packed) reads for pattern-control bytes.
// Not one uniform scheme." That same section CLOSES former OPEN items 2/3/8
// by naming this shape explicitly (2026-10-08 spec sync).
//
// WHAT THIS FILE DOES AND DOES NOT CLAIM:
//   - The dword-at-a-time refill, the LSB-first consumption order, and the
//     set of four fixed widths {1,3,4,7} are CONFIRMED (Sec5.3a, quoted
//     above) and are exactly what readBits()/kConfirmedWidths model.
//   - The byte-aligned, non-bit-packed reads Sec5.3a names for
//     "pattern-control bytes" (the DC-fill/PATTERN literal bytes, Sec5.3's
//     case 1 sub 3/5) are modelled by readAlignedByte() below. The EXACT
//     realignment mechanics (whether a partially-consumed accumulator is
//     discarded back to the previous byte boundary before such a read, or
//     whether these reads only ever occur when the accumulator happens to
//     already be byte-aligned) were not given at the instruction level in
//     any spec text available to this task - readAlignedByte() picks the
//     conservative, commonly-correct convention (discard any leftover
//     buffered bits back to the nearest byte boundary of the ORIGINAL
//     source stream, then read the next whole byte directly), flagged here
//     as a DERIVED modelling choice, not an independently CONFIRMED one.
//   - Per-field WIRING - which specific field at which point in a block's
//     decode draws which width, beyond the handful of cases spec-bink-
//     codec.md Sec5.3/Sec5.3a individually names - is NOT claimed here.
//     That wiring, for the three giant engines specifically, is exactly
//     the "exact bit-level entropy math... not traced" gap Sec8 item 2
//     leaves OPEN. See block_dispatch.h for how this reader is actually
//     used, and for the explicit OPEN hooks where that gap bites.

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace sr3binkcodec {

// Confirmed fixed widths this codec's shared accumulator is known to draw
// (spec-bink-codec.md Sec5.3a). readBits() itself is mechanically generic
// (any 1-32 bit width), but callers outside this header should stick to
// these four unless citing a new spec fact - see block_dispatch.h's own
// comments for where (and why) this file derives a 4-bit width for the
// per-block dispatch value itself.
constexpr int kConfirmedBitWidths[] = {1, 3, 4, 7};

inline bool isConfirmedBitWidth(int bits) {
    for (int w : kConfirmedBitWidths) {
        if (w == bits) return true;
    }
    return false;
}

// Bink 1.x shared bit-accumulator reader (spec-bink-codec.md Sec5.3a).
//
// Internally mirrors the confirmed eax(value)/esi(bits-left)/edi(source-ptr)
// shape with a 64-bit accumulator (wide enough to hold a freshly-refilled
// 32-bit dword stacked on top of up to 31 still-buffered bits without ever
// needing a second refill mid-read) and an explicit buffered-bit count.
class BitstreamReader {
public:
    BitstreamReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    // Draws `bits` (1..32) LSB-first from the shared accumulator (Sec5.3a:
    // "refilled a full 32-bit dword at a time, LSB-first"), refilling from
    // the source pointer whenever the buffered count runs short. Past the
    // end of the real data, refills read as zero and set ranOutOfData() -
    // this lets a caller keep going far enough to notice and report the
    // overrun rather than crashing on an out-of-bounds read.
    uint32_t readBits(int bits) {
        if (bits <= 0 || bits > 32) {
            throw std::invalid_argument("BitstreamReader::readBits: width must be 1..32");
        }
        while (bitsBuffered_ < bits) {
            accumulator_ |= static_cast<uint64_t>(nextDwordLE()) << bitsBuffered_;
            bitsBuffered_ += 32;
        }
        const uint64_t mask = (bits == 32) ? 0xFFFFFFFFull : ((uint64_t{1} << bits) - 1);
        const uint32_t value = static_cast<uint32_t>(accumulator_ & mask);
        accumulator_ >>= bits;
        bitsBuffered_ -= bits;
        totalBitsConsumed_ += static_cast<size_t>(bits);
        return value;
    }

    // Looks at the next `bits` (1..32) LSB-first WITHOUT consuming them -
    // refills the accumulator exactly as readBits() would (so a caller can
    // still detect ranOutOfData() from the peek itself), but leaves the
    // read cursor unmoved. Added 2026-10-08 (spec-bink-codec.md Sec5.3g)
    // for the entropy pre-stage's own canonical-Huffman per-symbol lookup:
    // "PEEK desc->A bits, do NOT consume yet" - the table lookup at the
    // peeked value then determines the REAL code length, and only THAT
    // many bits are actually consumed (via a following readBits(code_len)
    // call), which is why a plain readBits(A) cannot be used here. This is
    // a pure addition - every existing caller of readBits()/readAlignedByte()
    // is unaffected.
    uint32_t peekBits(int bits) {
        if (bits <= 0 || bits > 32) {
            throw std::invalid_argument("BitstreamReader::peekBits: width must be 1..32");
        }
        while (bitsBuffered_ < bits) {
            accumulator_ |= static_cast<uint64_t>(nextDwordLE()) << bitsBuffered_;
            bitsBuffered_ += 32;
        }
        const uint64_t mask = (bits == 32) ? 0xFFFFFFFFull : ((uint64_t{1} << bits) - 1);
        return static_cast<uint32_t>(accumulator_ & mask);
    }

    // Consumes `bits` (1..32) LSB-first WITHOUT returning the drawn value -
    // the same bit-cursor effect as `(void)readBits(bits)`, added
    // 2026-10-09 (spec-bink-codec.md Sec5.3g's merge-network subtlety, see
    // entropy_prestage.h's buildCaseSelectorTable()) for callers that
    // already have the value from a prior peekBits() call and only need to
    // advance the cursor by an ACTUAL, data-dependent bit count - which for
    // the merge-network's 3 variable-cost fields can be less than the
    // nominal field width that was peeked. Exists purely so call sites read
    // as "peek, derive the real cost, skip exactly that much" instead of a
    // discarded-return-value readBits() call, which is equivalent but reads
    // as if the nominal width were the real cost.
    void skipBits(int bits) { readBits(bits); }

    // Sec5.3a's "several byte-aligned (non-bit-packed) reads for
    // pattern-control bytes" - see this file's own top comment for the
    // DERIVED realignment convention used here.
    uint8_t readAlignedByte() {
        alignToByte();
        return static_cast<uint8_t>(readBits(8));
    }

    // Discards any bits buffered past the nearest earlier byte boundary of
    // the original source stream (a no-op if already aligned). DERIVED
    // convention - see this file's top comment.
    void alignToByte() {
        const int extra = bitsBuffered_ % 8;
        if (extra != 0) {
            accumulator_ >>= extra;
            bitsBuffered_ -= extra;
        }
    }

    // How many whole source bytes have been pulled into the accumulator so
    // far (refill granularity is one dword = 4 bytes; this is NOT the same
    // as "bytes actually consumed by readBits", since up to 31 bits can sit
    // buffered and unconsumed) - a diagnostic, not a precise cursor.
    size_t sourceBytesRefilled() const { return sourcePos_; }
    size_t totalSize() const { return size_; }
    int bitsBuffered() const { return bitsBuffered_; }

    // Cumulative count of bits actually CONSUMED (readBits()/skipBits() calls
    // only - a peekBits() that is never followed by a skipBits()/readBits()
    // does not advance this) since this reader was constructed. Added
    // 2026-10-09 for tests/real_bink_merge_group_trace_test.cpp, which needs
    // to check the real bit-reader cursor against spec-bink-codec.md
    // Sec5.3g's own per-call bit-position trace after each of the 23
    // per-block-row buildCaseSelectorTable() calls - a cheap diagnostic
    // accessor, not used by any production decode path.
    size_t bitsConsumedSoFar() const { return totalBitsConsumed_; }

    // True once a refill has had to invent zero bytes past the real end of
    // the source buffer - the caller's signal that any result read since is
    // no longer meaningful (bitstream exhausted/overrun), not a real value.
    bool ranOutOfData() const { return ranOutOfData_; }

private:
    uint32_t nextDwordLE() {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            uint8_t byte = 0;
            if (sourcePos_ < size_) {
                byte = data_[sourcePos_];
            } else {
                ranOutOfData_ = true;
            }
            v |= static_cast<uint32_t>(byte) << (8 * i);
            ++sourcePos_;
        }
        return v;
    }

    const uint8_t* data_;
    size_t size_;
    size_t sourcePos_ = 0;
    uint64_t accumulator_ = 0;
    int bitsBuffered_ = 0;
    bool ranOutOfData_ = false;
    size_t totalBitsConsumed_ = 0;
};

}  // namespace sr3binkcodec
