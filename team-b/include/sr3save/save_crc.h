#pragma once

// The save-data hash (spec-save-format.md Sec6.1, CONFIRMED - disassembly +
// empirical: directory 4/4, snapshot 16/16) and the engine name hash that
// the same table-driven routine produces for strings (Sec9.8, CONFIRMED -
// empirical).
//
// It is a plain reflected CRC-32 (polynomial 0xEDB88320, per byte
// crc = (crc >> 8) ^ T[(crc ^ byte) & 0xFF]) with the two properties that
// make it NOT the familiar zlib/PNG CRC: the initial value is 0 and the
// result is NOT complemented. Do not reach for a zlib crc32() here.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "vpp/byte_view.h"

namespace sr3save {

constexpr uint32_t kCrcPolynomialReflected = 0xEDB88320u; // Sec6.1

// Directory: CRC over bytes [0x008, 0x260) only - the 600-byte slot table,
// NOT the count at 0x004 (Sec6.1).
constexpr size_t kDirectoryHashBegin = 0x008;
constexpr size_t kDirectoryHashEnd = 0x260;

// Snapshot: the in-memory buffer is 0x1AA00 bytes, the hash covers
// [0x004, 0x1AA00) but only the first 0x1A8E8 bytes are written to the file,
// so a from-file recomputation is file[0x004:0x1A8E8] followed by 0x118 zero
// bytes (Sec6.1, Sec6.3).
constexpr size_t kSnapshotFileSize = 0x1A8E8;       // 108,776
constexpr size_t kSnapshotBufferSize = 0x1AA00;     // in-memory buffer
constexpr size_t kSnapshotHashBegin = 0x004;
constexpr size_t kSnapshotHashZeroTail = kSnapshotBufferSize - kSnapshotFileSize; // 0x118

namespace detail {

inline std::array<uint32_t, 256> makeCrcTable() {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1u) ? (kCrcPolynomialReflected ^ (c >> 1)) : (c >> 1);
        }
        t[i] = c;
    }
    return t;
}

} // namespace detail

// The 256-entry table (Sec6.1: T[1] = 0x77073096, T[2] = 0xEE0E612C, ...).
inline const std::array<uint32_t, 256>& crcTable() {
    static const std::array<uint32_t, 256> table = detail::makeCrcTable();
    return table;
}

// Feeds `n` bytes into a running CRC. `crc` is the running value (initial 0
// for every use in the save formats); no final XOR is applied.
inline uint32_t crcUpdate(uint32_t crc, const uint8_t* p, size_t n) {
    const auto& t = crcTable();
    for (size_t i = 0; i < n; ++i) {
        crc = (crc >> 8) ^ t[(crc ^ p[i]) & 0xFFu];
    }
    return crc;
}

inline uint32_t crcUpdateZeros(uint32_t crc, size_t count) {
    const auto& t = crcTable();
    for (size_t i = 0; i < count; ++i) {
        crc = (crc >> 8) ^ t[crc & 0xFFu];
    }
    return crc;
}

// Init-0, no-final-XOR CRC-32 over [offset, offset + length) of `bytes`.
inline uint32_t saveCrc(vpp::ByteView bytes, size_t offset, size_t length) {
    vpp::ByteView sub = bytes.subview(offset, length);
    return crcUpdate(0u, sub.data(), sub.size());
}

// Hash a directory file must carry at offset 0x000 (Sec6.1). `bytes` must be
// the full 608-byte file.
inline uint32_t computeDirectoryHash(vpp::ByteView bytes) {
    return saveCrc(bytes, kDirectoryHashBegin, kDirectoryHashEnd - kDirectoryHashBegin);
}

// Hash a snapshot file carries at offset 0x000 (Sec6.1): CRC of
// file[0x004:0x1A8E8] followed by 0x118 zero bytes. `bytes` must be the full
// 108,776-byte file. Written by the game on save; NOT verified by the game
// on load (Sec11.1), so a mismatch is informational only.
inline uint32_t computeSnapshotHash(vpp::ByteView bytes) {
    vpp::ByteView body = bytes.subview(kSnapshotHashBegin, kSnapshotFileSize - kSnapshotHashBegin);
    uint32_t crc = crcUpdate(0u, body.data(), body.size());
    return crcUpdateZeros(crc, kSnapshotHashZeroTail);
}

// The engine's name hash (Sec9.8, shared by activity keys, cheat ids,
// challenge/shop/item ids, ...): the same CRC (init 0, no final XOR) over the
// lower-cased ASCII name, without the terminator.
inline uint32_t nameHash(const std::string& name) {
    uint32_t crc = 0;
    const auto& t = crcTable();
    for (char ch : name) {
        uint8_t b = static_cast<uint8_t>(ch);
        if (b >= 'A' && b <= 'Z') b = static_cast<uint8_t>(b - 'A' + 'a');
        crc = (crc >> 8) ^ t[(crc ^ b) & 0xFFu];
    }
    return crc;
}

} // namespace sr3save
