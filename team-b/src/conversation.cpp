#include "sr3conversation/conversation.h"

#include <algorithm>
#include <cstring>

namespace sr3conversation {

int32_t DialogueTurn::field_0x08_asInt32() const {
    int32_t value = 0;
    static_assert(sizeof(value) == sizeof(field_0x08_raw), "i32 must be 4 bytes");
    std::memcpy(&value, &field_0x08_raw, sizeof(value));
    return value;
}

Conversation Conversation::parse(ByteView content) {
    Conversation c;

    if (content.size() < kHeaderSize) {
        throw FormatError("content too small to contain the .ctdg_pc header (44 bytes)");
    }

    if (content.readU32LE(0x00) != kMagic) {
        throw FormatError("bad magic: expected 0x56414344 (\"DCAV\" in file order, spec Sec3, "
                          "CONFIRMED 4,984/4,984)");
    }

    c.version_ = content.readU32LE(0x04);
    if (c.version_ != kRequiredVersion) {
        throw FormatError("unsupported .ctdg_pc version " + std::to_string(c.version_) +
                          ": the consumer requires exactly " + std::to_string(kRequiredVersion) +
                          " (spec Sec3, CONFIRMED)");
    }

    // --- Source name: a FIXED 32-byte field, not a variable-length
    // string - the consumer copies it wholesale as 0x20 bytes (spec Sec3).
    // NUL-terminated and zero-padded when it fits; when the authoring name
    // was longer it is clipped to 31 characters, so a field with no
    // terminator at all is a truncation, not a malformed file. ---
    {
        ByteView field = content.subview(kSourceNameOffset, kSourceNameSize);
        const char* chars = reinterpret_cast<const char*>(field.data());
        size_t length = 0;
        while (length < kSourceNameSize && chars[length] != '\0') {
            ++length;
        }
        // Truncation signature: the engine clips an over-long authoring
        // name to 31 characters AND STILL WRITES THE TERMINATOR, so a
        // clipped name has length 31, not 32. An earlier version of this
        // reader tested for 32 (a field with no terminator at all) and so
        // could never fire: it reported 0 truncated names across all 4,984
        // shipped files, against spec Sec1's 1,187. Length 32 is still
        // flagged, since a field with no terminator is equally not a
        // trustworthy name - it just does not occur in shipped data.
        c.sourceNameTruncated_ = (length >= kSourceNameSize - 1);
        c.sourceName_.assign(chars, length);
    }

    // --- Record count. The consumer rejects 0 and anything above 30
    // BEFORE touching a single record (spec Sec5's validation ladder), so
    // both bounds are checked here in the same order. ---
    uint32_t recordCount = content.readU32LE(kRecordCountOffset);
    if (recordCount == 0) {
        throw FormatError("record count is 0: the consumer rejects an empty conversation "
                          "(spec Sec3/Sec5, CONFIRMED - disassembly)");
    }
    if (recordCount > kMaxRecordCount) {
        throw FormatError("record count " + std::to_string(recordCount) +
                          " exceeds the loader's hard limit of " +
                          std::to_string(kMaxRecordCount) +
                          " (spec Sec3/Sec5, CONFIRMED - disassembly; shipped files use 1-17)");
    }

    // --- The exact-size identity. This is the whole format's structural
    // proof: it held in 4,984/4,984 shipped files with every byte
    // accounted for (spec Sec1/Sec3), so a mismatch means this file does
    // not match the model - not that the reader should read on and hope. ---
    size_t expectedSize = kHeaderSize + static_cast<size_t>(recordCount) * kRecordSize;
    if (content.size() != expectedSize) {
        throw FormatError("size mismatch: " + std::to_string(recordCount) +
                          " records implies exactly " + std::to_string(expectedSize) +
                          " bytes (44 + 12 x count), but content is " +
                          std::to_string(content.size()) +
                          " bytes - spec Sec1 has this identity holding 4,984/4,984");
    }

    c.turns_.reserve(recordCount);
    for (uint32_t i = 0; i < recordCount; ++i) {
        size_t at = kHeaderSize + static_cast<size_t>(i) * kRecordSize;
        DialogueTurn turn;
        turn.speakerId = content.readU32LE(at + 0x00);
        turn.lineId = content.readU32LE(at + 0x04);
        turn.field_0x08_raw = content.readU32LE(at + 0x08);
        c.turns_.push_back(turn);
    }

    return c;
}

std::vector<uint32_t> Conversation::distinctSpeakerIds() const {
    // First-appearance order, mirroring the consumer's own set-building
    // walk over the records in file order (spec Sec4). Linear scan rather
    // than a hash set on purpose: the count is bounded at 30 by the format
    // itself, and shipped files hold 1-4.
    std::vector<uint32_t> distinct;
    for (const DialogueTurn& turn : turns_) {
        if (std::find(distinct.begin(), distinct.end(), turn.speakerId) == distinct.end()) {
            distinct.push_back(turn.speakerId);
        }
    }
    return distinct;
}

} // namespace sr3conversation
