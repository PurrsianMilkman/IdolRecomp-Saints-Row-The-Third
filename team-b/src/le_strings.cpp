// sr3locstr - `.le_strings` parsing and key-hash lookup (spec-localization-
// format.md). See include/sr3locstr/le_strings.h for scope.
#include "sr3locstr/le_strings.h"

#include "sr3save/save_crc.h"

namespace sr3locstr {

namespace {

uint16_t rdU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t rdU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

constexpr uint32_t kMagic = 0xa84c7f73u; // Sec2.1
constexpr uint16_t kVersion = 1;         // Sec2.1
constexpr size_t kHeaderBytes = 0x0c;    // Sec2.1

} // namespace

uint32_t keyHash(const std::string& key) { return sr3save::nameHash(key); }

LeStringsTable parseLeStrings(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < kHeaderBytes)
        throw LeStringsFormatError("le_strings: file shorter than the 0x0c-byte header (Sec2.1)");
    const uint8_t* data = bytes.data();
    const size_t size = bytes.size();

    const uint32_t magic = rdU32(data + 0x00);
    if (magic != kMagic) throw LeStringsFormatError("le_strings: bad magic (Sec2.1)");
    const uint16_t version = rdU16(data + 0x04);
    if (version != kVersion) throw LeStringsFormatError("le_strings: unsupported version, must be 1 (Sec2.1)");
    const uint16_t bucketCount = rdU16(data + 0x06);
    const uint32_t recordCount = rdU32(data + 0x08);

    LeStringsTable table;
    table.bucketCount = bucketCount;
    table.recordCount = recordCount;

    const size_t bucketTableBegin = kHeaderBytes;
    const size_t bucketTableBytes = 8u * static_cast<size_t>(bucketCount);
    if (bucketTableBegin + bucketTableBytes > size)
        throw LeStringsFormatError("le_strings: bucket table runs past end of file (Sec2.2)");

    for (uint32_t b = 0; b < static_cast<uint32_t>(bucketCount); ++b) {
        const size_t entryOff = bucketTableBegin + 8u * b;
        const uint32_t count = rdU32(data + entryOff);
        if (count == 0) continue; // Sec2.2: empty bucket, offset 0xffffffff, nothing to read
        const uint32_t arrayOff = rdU32(data + entryOff + 4);
        if (static_cast<size_t>(arrayOff) + 4ull * count > size)
            throw LeStringsFormatError("le_strings: record-offset array runs past end of file (Sec2.3)");
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t recOff = rdU32(data + arrayOff + 4u * i);
            if (static_cast<size_t>(recOff) + 4 > size)
                throw LeStringsFormatError("le_strings: record offset runs past end of file (Sec2.3)");
            const uint32_t hash = rdU32(data + recOff);
            std::u16string text;
            size_t p = static_cast<size_t>(recOff) + 4;
            for (;;) {
                if (p + 2 > size) throw LeStringsFormatError("le_strings: unterminated record text (Sec2.3)");
                const uint16_t unit = rdU16(data + p);
                p += 2;
                if (unit == 0) break;
                text.push_back(static_cast<char16_t>(unit));
            }
            // First entry for a hash wins (never exercised: no shipped US
            // table repeats a hash within itself, spec Sec1/Sec2.3).
            table.byHash.emplace(hash, std::move(text));
        }
    }
    return table;
}

void LeStringsLookup::addTable(LeStringsTable table) { tables_.push_back(std::move(table)); }

size_t LeStringsLookup::tableCount() const { return tables_.size(); }

std::optional<std::u16string> LeStringsLookup::lookup(uint32_t hash) const {
    if (hash == 0) return std::nullopt; // Sec6.1
    for (const LeStringsTable& t : tables_) {
        auto it = t.byHash.find(hash);
        if (it != t.byHash.end()) return it->second;
    }
    return std::nullopt;
}

std::optional<std::u16string> LeStringsLookup::lookupByKey(const std::string& key) const {
    return lookup(keyHash(key));
}

} // namespace sr3locstr
