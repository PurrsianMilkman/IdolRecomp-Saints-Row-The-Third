#include "sr3audio/media_bank.h"

namespace sr3audio {

namespace {

// round_up(v, 0x800). Restated here rather than imported from
// vpp::roundUpBlock - see the note on kMediaBankBlockSize in media_bank.h
// for why the numeric coincidence is not treated as a shared rule.
size_t roundUpBlock(size_t v) {
    return (v + (kMediaBankBlockSize - 1)) / kMediaBankBlockSize * kMediaBankBlockSize;
}

} // namespace

size_t recordBlockSpan(const MediaBankRecord& r) {
    // Widened to size_t before the add: `size` and `extra` are both u32 on
    // disk and a malformed pair could wrap a 32-bit add, turning an absurd
    // record into a plausible-looking small advance. On a 64-bit build this
    // cannot wrap, and the walk's own overshoot check then rejects it.
    return roundUpBlock(static_cast<size_t>(r.size) + static_cast<size_t>(r.extra));
}

bool MediaBank::looksLikeMediaBank(ByteView content) {
    if (content.size() < kMediaBankMagic.size()) return false;
    for (size_t i = 0; i < kMediaBankMagic.size(); ++i) {
        if (content.at(i) != kMediaBankMagic[i]) return false;
    }
    return true;
}

MediaBank MediaBank::parse(ByteView content) {
    if (content.size() < kMediaBankHeaderSize) {
        throw FormatError("content too small to contain the VWSBPC header: " +
                          std::to_string(content.size()) + " bytes, need " +
                          std::to_string(kMediaBankHeaderSize) +
                          " (spec-audio-format.md Sec4.1)");
    }

    // --- +0x00: the one hard gate. CONFIRMED identical across all 536
    // shipped files (Sec4.1). ---
    if (!looksLikeMediaBank(content)) {
        throw FormatError("bad VWSBPC magic at +0x00: expected bytes "
                          "56 57 53 42 50 43 20 20 (spec-audio-format.md Sec4.1)");
    }

    MediaBank b;
    b.content_ = content;

    // --- +0x08: raw, uninterpreted. Its VALUE never varied across the
    // population, but its ROLE is OPEN, so it is neither decoded into named
    // sub-fields nor enforced here. ---
    for (size_t i = 0; i < b.constantAt0x08_.size(); ++i) {
        b.constantAt0x08_[i] = content.at(0x08 + i);
    }

    b.crossReferenceId_ = content.readU32LE(0x10);   // CONFIRMED (Sec5)
    b.fieldAt0x14_ = content.readU32LE(0x14);        // OPEN - raw
    b.declaredRecordCount_ = content.readU32LE(0x18); // CONFIRMED (Sec4.1)
    b.fieldAt0x1C_ = content.readU32LE(0x1C);        // OPEN - raw

    return b;
}

size_t MediaBank::blockAlignedTableEnd() const {
    return roundUpBlock(kMediaBankHeaderSize +
                        static_cast<size_t>(declaredRecordCount_) * kMediaBankRecordSize);
}

std::vector<MediaBankRecord> MediaBank::walk(size_t totalEntrySize) const {
    std::vector<MediaBankRecord> records;

    // --- The one degenerate shape: a wrapper declaring no records at all
    // has no record 0, and therefore no anchor to read. Its only
    // self-consistent walk is the empty one, ending at the block-aligned end
    // of the header-plus-table region. This is the ONLY place walk() ever
    // consults the +0x18 count field - a real, non-empty walk never does,
    // which is what keeps that field an independent signal (see the class
    // comment). Whether any shipped file is this shape is MEASURED by
    // tools/validation/validate_media_bank.cpp, not assumed here. ---
    if (declaredRecordCount_ == 0) {
        if (totalEntrySize == blockAlignedTableEnd()) return records;
        throw FormatError(
            "VWSBPC header declares 0 records but the container-declared entry size " +
            std::to_string(totalEntrySize) + " is not the block-aligned end of the header/table "
            "region (" + std::to_string(blockAlignedTableEnd()) +
            ") - there is no record 0 to anchor a chain on");
    }

    // --- The anchor. READ, not assumed. Sec4.2 says record 0's offset is
    // "always 0x800"; this project measured that at 278/536 and the
    // block-aligned-table-end generalisation at 536/536 (see the anchor note
    // in media_bank.h). Taking the value the file itself states is both
    // correct on the whole population and independent of the +0x18 count
    // field that the generalisation would otherwise drag into the walk. ---
    if (kMediaBankHeaderSize + kMediaBankRecordSize > content_.size()) {
        throw FormatError("content too small to contain record 0 of the VWSBPC table: " +
                          std::to_string(content_.size()) + " bytes");
    }
    size_t cursor = content_.readU32LE(kMediaBankHeaderSize);
    if (cursor == 0 || cursor % kMediaBankBlockSize != 0) {
        throw FormatError(
            "VWSBPC record 0 offset " + std::to_string(cursor) +
            " is not a non-zero multiple of 0x800 - every offset[0] in the 536-file shipped "
            "population is (spec-audio-format.md Sec4.2, as corrected in media_bank.h's anchor "
            "note); a zero here is the wrapper's own zero-padded reserved table space, not a "
            "record");
    }

    for (size_t i = 0;; ++i) {
        // Termination: the container's own declared size for this entry, an
        // oracle the container publishes (Sec4.2/Sec11). Checked BEFORE
        // reading another record, so a chain that lands exactly on the
        // declared end never touches the zero-padded reserved table space
        // beyond the real record count.
        if (cursor == totalEntrySize) return records;

        if (cursor > totalEntrySize) {
            throw FormatError(
                "VWSBPC chain overshot the container-declared entry size: cursor " +
                std::to_string(cursor) + " passed " + std::to_string(totalEntrySize) +
                " after " + std::to_string(records.size()) +
                " record(s) (spec-audio-format.md Sec4.2: the walk must land EXACTLY on "
                "the declared size, 536/536 with zero overshoots)");
        }

        size_t at = kMediaBankHeaderSize + i * kMediaBankRecordSize;
        if (at + kMediaBankRecordSize > content_.size()) {
            throw FormatError(
                "VWSBPC record table ran off the end of the entry before the chain "
                "reached the container-declared size: record " + std::to_string(i) +
                " would need bytes " + std::to_string(at) + ".." +
                std::to_string(at + kMediaBankRecordSize) + " of " +
                std::to_string(content_.size()) + ", cursor is at " + std::to_string(cursor) +
                " of " + std::to_string(totalEntrySize) + " (undershooting chain)");
        }

        MediaBankRecord r;
        r.offset = content_.readU32LE(at + 0x00);
        r.extra = content_.readU32LE(at + 0x04);
        r.size = content_.readU32LE(at + 0x08);
        r.tag = content_.readU32LE(at + 0x0C); // OPEN - carried raw, never read by this walk

        if (static_cast<size_t>(r.offset) != cursor) {
            throw FormatError(
                "VWSBPC chain break at record " + std::to_string(i) + ": its offset field reads " +
                std::to_string(r.offset) + " but the chain rule puts it at " +
                std::to_string(cursor) +
                " (spec-audio-format.md Sec4.2: offset[i+1] = offset[i] + "
                "round_up(size[i] + extra[i], 0x800), CONFIRMED 536/536, 89,631 records, "
                "zero chain breaks)");
        }

        size_t span = recordBlockSpan(r);
        if (span == 0) {
            // size == 0 && extra == 0 - the vacuous `0 == 0 +
            // round_up(0, 0x800)` predicate Sec4.3/Sec11 record. Note the
            // division of labour, since it is easy to think this guard is
            // the whole answer: ordinary zero padding past the real end of
            // the table is caught by the offset/cursor check ABOVE (padding
            // reads offset 0, and the cursor is never 0 - it starts at
            // 0x800). What this guard catches is the case that check cannot
            // see, a zero-span record sitting exactly AT the cursor, which
            // would otherwise loop forever with the record count climbing.
            // Both together are what stop a walk from silently reinflating
            // its own match rate.
            throw FormatError(
                "VWSBPC record " + std::to_string(i) +
                " advances the chain by zero bytes (size == 0 and extra == 0) - this is the "
                "zero-padded reserved-table-space trap spec-audio-format.md Sec4.3/Sec11 "
                "document, not a valid record; cursor " + std::to_string(cursor) + " of " +
                std::to_string(totalEntrySize));
        }

        records.push_back(r);
        cursor += span;
    }
}

} // namespace sr3audio
