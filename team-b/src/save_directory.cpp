#include "sr3save/save_directory.h"

#include <cstdlib>
#include <cstring>

#include "sr3save/save_crc.h"

namespace sr3save {

namespace {

// Field widths in write order (Sec8.2, "Bits" column).
constexpr int kFieldWidth[kSlotSummaryFieldCount] = {6, 7, 9, 5, 17, 2, 1, 1, 7, 8, 7,
                                                     5, 4, 5, 7, 5,  6, 6, 32, 6, 9, 32};

// Sec8.2 "Where (byte.bit, LSB first)" column, transcribed. Grammar per token:
//   "B.b"      one bit: byte B, bit b
//   "B.b0-b1"  bits b0..b1 of byte B (in that order)
//   "ByN"      whole byte(s): "By14-17" = bytes 14..17, each 8 bits LSB first
//              (little-endian multi-byte field); "By19" = byte 19
constexpr const char* kSpecWhere[kSlotSummaryFieldCount] = {
    "0.0-5",                    // 1
    "0.6 0.7 1.0-4",            // 2
    "2.0-7 1.5",                // 3
    "1.6 1.7 3.0-2",            // 4
    "4.0-7 5.0-7 3.3",          // 5
    "3.4 3.5",                  // 6
    "3.6",                      // 7
    "3.7",                      // 8
    "6.0-6",                    // 9
    "7.0-7",                    // 10
    "6.7 8.0-5",                // 11
    "8.6 8.7 9.0-2",            // 12
    "9.3-6",                    // 13
    "9.7 10.0-3",               // 14
    "10.4-7 11.0-2",            // 15
    "11.3-7",                   // 16
    "12.0-5",                   // 17
    "12.6 12.7 13.0-3",         // 18
    "By14-17",                  // 19
    "13.4-7 18.0 18.1",         // 20
    "By19 18.2",                // 21
    "By20-23",                  // 22
};

int parseInt(const char*& p) {
    char* end = nullptr;
    long v = std::strtol(p, &end, 10);
    p = end;
    return static_cast<int>(v);
}

std::vector<BitPos> parseWhere(const char* s) {
    std::vector<BitPos> out;
    const char* p = s;
    while (*p) {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (p[0] == 'B' && p[1] == 'y') {
            p += 2;
            int b0 = parseInt(p);
            int b1 = b0;
            if (*p == '-') {
                ++p;
                b1 = parseInt(p);
            }
            for (int by = b0; by <= b1; ++by) {
                for (int bit = 0; bit < 8; ++bit) out.push_back({by, bit});
            }
        } else {
            int by = parseInt(p);
            ++p; // '.'
            int bit0 = parseInt(p);
            int bit1 = bit0;
            if (*p == '-') {
                ++p;
                bit1 = parseInt(p);
            }
            for (int bit = bit0; bit <= bit1; ++bit) out.push_back({by, bit});
        }
    }
    return out;
}

uint32_t extractBits(const uint8_t* bytes, const std::vector<BitPos>& pos) {
    uint32_t v = 0;
    for (size_t i = 0; i < pos.size(); ++i) {
        uint32_t bit = (bytes[pos[i].byte] >> pos[i].bit) & 1u;
        v |= bit << i;
    }
    return v;
}

void depositBits(uint8_t* bytes, const std::vector<BitPos>& pos, uint32_t value) {
    for (size_t i = 0; i < pos.size(); ++i) {
        uint32_t bit = (value >> i) & 1u;
        if (bit) bytes[pos[i].byte] = static_cast<uint8_t>(bytes[pos[i].byte] | (1u << pos[i].bit));
    }
}

} // namespace

const std::array<std::vector<BitPos>, kSlotSummaryFieldCount>& slotSummaryBitPositionsFromSpecTable() {
    static const std::array<std::vector<BitPos>, kSlotSummaryFieldCount> table = [] {
        std::array<std::vector<BitPos>, kSlotSummaryFieldCount> t;
        for (size_t i = 0; i < kSlotSummaryFieldCount; ++i) t[i] = parseWhere(kSpecWhere[i]);
        return t;
    }();
    return table;
}

std::array<std::vector<BitPos>, kSlotSummaryFieldCount> slotSummaryBitPositionsFromWriterRule() {
    std::array<std::vector<BitPos>, kSlotSummaryFieldCount> out;
    int hi = -1;       // highest byte touched so far
    int part = -1;     // the partially filled byte, if any
    int partBits = 0;  // bits already used in it
    for (size_t f = 0; f < kSlotSummaryFieldCount; ++f) {
        int n = kFieldWidth[f];
        int whole = n / 8;
        int rem = n % 8;
        // the n div 8 whole (low) bytes go byte-aligned at the next unused byte
        for (int w = 0; w < whole; ++w) {
            int byte = ++hi;
            for (int b = 0; b < 8; ++b) out[f].push_back({byte, b});
        }
        // the remaining n mod 8 low bits continue the partially filled byte
        for (int r = 0; r < rem; ++r) {
            if (part < 0 || partBits == 8) {
                part = ++hi;
                partBits = 0;
            }
            out[f].push_back({part, partBits++});
        }
    }
    return out;
}

SlotSummary decodeSlotSummary(const uint8_t* b) {
    const auto& t = slotSummaryBitPositionsFromSpecTable();
    auto v = [&](size_t f) { return extractBits(b, t[f]); };
    SlotSummary s;
    s.reserved1 = v(0);
    s.liveCountA_Open = v(1);
    s.flaggedRecordCount = v(2);
    s.liveCounterB_Open = v(3);
    s.playMinutes = v(4);
    s.difficulty = v(5);
    s.cheatsUsed = v(6) != 0;
    s.autosave = v(7) != 0;
    s.completionPercent = v(8);
    s.formatVersion = v(9);
    s.locationLabelId = v(10);
    s.reserved12 = v(11);
    s.month = v(12);
    s.day = v(13);
    s.year2 = v(14);
    s.hour = v(15);
    s.minute = v(16);
    s.second = v(17);
    s.cashDiv100 = static_cast<int32_t>(v(18));
    s.level = v(19);
    s.regionRecordCount = v(20);
    s.pointsInLevel = v(21);
    return s;
}

std::array<uint8_t, kSlotSummarySize> encodeSlotSummary(const SlotSummary& s) {
    // Positions come from the writer rule (Sec8.1), NOT from the table the decoder
    // uses, so decode(encode(x)) == x is a real cross-check of the two.
    static const auto pos = slotSummaryBitPositionsFromWriterRule();
    std::array<uint8_t, kSlotSummarySize> out{};
    const uint32_t vals[kSlotSummaryFieldCount] = {
        s.reserved1, s.liveCountA_Open, s.flaggedRecordCount, s.liveCounterB_Open, s.playMinutes, s.difficulty,
        s.cheatsUsed ? 1u : 0u, s.autosave ? 1u : 0u, s.completionPercent, s.formatVersion, s.locationLabelId,
        s.reserved12, s.month, s.day, s.year2, s.hour, s.minute, s.second, static_cast<uint32_t>(s.cashDiv100),
        s.level, s.regionRecordCount, s.pointsInLevel};
    for (size_t f = 0; f < kSlotSummaryFieldCount; ++f) depositBits(out.data(), pos[f], vals[f]);
    return out;
}

uint32_t SaveDirectory::computeHash(ByteView bytes) {
    return computeDirectoryHash(bytes);
}

SaveDirectory SaveDirectory::parse(ByteView bytes, HashPolicy policy) {
    if (bytes.size() != kSaveDirectorySize) {
        throw FormatError(
            "savedir.sr3d_pc must be exactly 608 bytes (spec Sec2.1, "
            "CONFIRMED - the real loader rejects any other size, error 3); got " +
            std::to_string(bytes.size()) + " bytes");
    }

    SaveDirectory dir;
    dir.checksumRaw_ = bytes.readU32LE(0x000);
    dir.hashComputed_ = computeDirectoryHash(bytes);
    dir.activeSlotCount_ = bytes.readU32LE(0x004);

    if (policy == HashPolicy::Enforce && dir.checksumRaw_ != dir.hashComputed_) {
        throw FormatError(
            "savedir.sr3d_pc hash mismatch (spec Sec6.1, error 4 in the real loader, which then treats the "
            "whole directory as empty): stored " + std::to_string(dir.checksumRaw_) + ", CRC-32 (init 0, no final XOR) "
            "of bytes 0x008-0x25F is " + std::to_string(dir.hashComputed_));
    }

    uint32_t recountedActive = 0;
    for (size_t i = 0; i < kSlotCount; ++i) {
        size_t off = kSlotTableOffset + i * kSlotRecordStride;
        SlotRecord& slot = dir.slots_[i];

        slot.active = (bytes.at(off + 0x00) != 0);
        if (slot.active) {
            ++recountedActive;
        }
        for (size_t b = 0; b < kSlotSummarySize; ++b) {
            slot.summaryRaw[b] = bytes.at(off + 0x01 + b);
        }
        slot.summary = decodeSlotSummary(slot.summaryRaw.data());
    }

    if (recountedActive != dir.activeSlotCount_) {
        throw FormatError(
            "savedir.sr3d_pc active-slot count mismatch: header field says " +
            std::to_string(dir.activeSlotCount_) + ", but " +
            std::to_string(recountedActive) +
            " slot(s) actually have a non-zero active flag - this is a "
            "CONFIRMED invariant the real loader enforces (spec Sec2.1, error 5), so "
            "this file is either corrupt or from an unobserved format "
            "variant");
    }

    return dir;
}

std::vector<int> SaveDirectory::occupiedSlotIndices() const {
    std::vector<int> result;
    for (size_t i = 0; i < kSlotCount; ++i) {
        if (slots_[i].active) {
            result.push_back(static_cast<int>(i));
        }
    }
    return result;
}

} // namespace sr3save
