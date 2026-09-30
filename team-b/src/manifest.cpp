#include "sr3asm/manifest.h"

#include <utility>

// Implemented from spec-asm-format.md sections 6-10 (version-11 layout).
// Every read goes through Cursor, which turns any overrun into a
// FormatError that names what was being read - never a silent short read.

namespace sr3asm {

const std::string* LookupTable::findName(uint8_t id) const {
    for (const LookupEntry& e : entries) {
        if (e.id == id) return &e.name;
    }
    return nullptr;
}

size_t AsmManifest::totalEntries() const {
    size_t n = 0;
    for (const ContainerRecord& r : records_) n += r.entries.size();
    return n;
}

namespace {

class Cursor {
public:
    explicit Cursor(ByteView bytes) : bytes_(bytes) {}

    size_t pos() const { return pos_; }
    size_t remaining() const { return bytes_.size() - pos_; }

    // Throws unless `n` more bytes are available.
    void need(size_t n, const char* what) const {
        if (n > remaining()) {
            throw FormatError(std::string("truncated .asm_pc: not enough bytes for ") + what +
                              " at offset " + std::to_string(pos_));
        }
    }

    uint8_t u8(const char* what) {
        need(1, what);
        return bytes_.at(pos_++);
    }
    uint16_t u16(const char* what) {
        need(2, what);
        uint16_t v = bytes_.readU16LE(pos_);
        pos_ += 2;
        return v;
    }
    uint32_t u32(const char* what) {
        need(4, what);
        uint32_t v = bytes_.readU32LE(pos_);
        pos_ += 4;
        return v;
    }
    // u16 length + that many bytes, no terminator (spec 7 "lpstr").
    std::string lpstr(const char* what) {
        uint16_t len = u16(what);
        need(len, what);
        std::string s(reinterpret_cast<const char*>(bytes_.data() + pos_), len);
        pos_ += len;
        return s;
    }
    std::vector<uint8_t> blob(size_t n, const char* what) {
        need(n, what);
        std::vector<uint8_t> v(bytes_.data() + pos_, bytes_.data() + pos_ + n);
        pos_ += n;
        return v;
    }

private:
    ByteView bytes_;
    size_t pos_ = 0;
};

// Spec 7.2: u32 count, then count x { lpstr name; u8 file_id }.
LookupTable readTable(Cursor& c, const char* what) {
    uint32_t count = c.u32(what);
    // Each row is at least 3 bytes (u16 length + u8 id): reject a count that
    // cannot possibly fit before reserving anything for it.
    if (static_cast<uint64_t>(count) * 3u > c.remaining()) {
        throw FormatError(std::string("truncated .asm_pc: ") + what + " declares " +
                          std::to_string(count) + " rows, more than the remaining bytes can hold");
    }
    LookupTable t;
    t.entries.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        LookupEntry e;
        e.name = c.lpstr(what);
        e.id = c.u8(what);
        t.entries.push_back(std::move(e));
    }
    return t;
}

// Spec 7.4: lpstr name + exactly 13 bytes.
ManifestEntry readEntry(Cursor& c) {
    ManifestEntry e;
    e.name = c.lpstr("entry name");
    c.need(kEntryFixedBytes, "entry fixed bytes");
    e.typeId = c.u8("entry type_id");
    e.poolId = c.u8("entry pool_id");
    e.entryFlags = c.u8("entry entry_flags");
    e.variantSelect = c.u8("entry variant_select");
    e.primarySize = c.u32("entry primary_size");
    e.secondarySize = c.u32("entry secondary_size");
    e.allocGroup = c.u8("entry alloc_group");
    return e;
}

// Spec 7.3.
ContainerRecord readRecord(Cursor& c) {
    ContainerRecord r;
    r.name = c.lpstr("record name");
    r.containerKind = c.u8("record container_kind");
    r.recordFlags = c.u16("record record_flags");
    r.entryCount = static_cast<int16_t>(c.u16("record entry_count"));
    if (r.entryCount < 0) {
        throw FormatError("record '" + r.name + "': negative entry_count (" +
                          std::to_string(r.entryCount) + ")");
    }
    r.headerRegionSize = c.u32("record header_region_size");
    r.sourceName = c.lpstr("record source_name");
    uint32_t extraLen = c.u32("record extra_len");
    r.extra = c.blob(extraLen, "record extra blob");
    r.payloadLength = c.u32("record payload_length");

    const size_t n = static_cast<size_t>(r.entryCount);
    // Size table: 8 bytes per entry, present unconditionally at version 11.
    // Each entry is at least 2 (name length) + 13 bytes. Check the whole
    // record's minimum size before reserving.
    c.need(n * (8 + 2 + kEntryFixedBytes), "record size table and entries");
    r.sizeTable.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        SizePair p;
        p.primary = c.u32("size table primary");
        p.secondary = c.u32("size table secondary");
        r.sizeTable.push_back(p);
    }
    r.entries.reserve(n);
    for (size_t i = 0; i < n; ++i) r.entries.push_back(readEntry(c));
    return r;
}

} // namespace

AsmManifest AsmManifest::parse(ByteView bytes) {
    Cursor c(bytes);
    c.need(kHeaderSize, "the 8-byte header");

    uint32_t magic = c.u32("magic");
    if (magic != kMagic) {
        throw FormatError("bad magic: expected 0xBEEFFEED (spec 7.1; the engine itself never checks it)");
    }

    AsmManifest m;
    m.version_ = c.u16("version");
    if (m.version_ != kSupportedVersion) {
        throw FormatError("unsupported .asm_pc version " + std::to_string(m.version_) +
                          " (only 11 is read; spec 6.2 documents other versions from code only)");
    }
    m.declaredRecordCount_ = c.u16("record_count");

    m.fixedTables_[0] = readTable(c, "table 1 (memory pools)");
    m.fixedTables_[1] = readTable(c, "table 2 (resource types)");
    m.fixedTables_[2] = readTable(c, "table 3 (container kinds)");

    m.records_.reserve(m.declaredRecordCount_);
    for (uint16_t i = 0; i < m.declaredRecordCount_; ++i) {
        m.records_.push_back(readRecord(c));
    }

    m.bytesConsumed_ = c.pos();
    m.trailingBytes_ = c.remaining();
    return m;
}

} // namespace sr3asm
