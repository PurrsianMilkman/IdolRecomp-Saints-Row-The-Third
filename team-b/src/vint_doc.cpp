#include "sr3vintdoc/vint_doc.h"

#include <cstring>

namespace sr3vintdoc {

bool looksLikeVintDoc(vpp::ByteView bytes) {
    return bytes.size() >= 4 && bytes.readU32LE(0) == kMagic;
}

void Cursor::need(size_t n) const {
    if (pos_ > bytes_.size() || bytes_.size() - pos_ < n) {
        throw FormatError("vint_doc: read of " + std::to_string(n) + " byte(s) at " + std::to_string(pos_) +
                          " runs past end of file (size " + std::to_string(bytes_.size()) + ")");
    }
}

uint8_t Cursor::u8() {
    need(1);
    return bytes_.at(pos_++);
}

uint16_t Cursor::u16() {
    need(2);
    uint16_t v = bytes_.readU16LE(pos_);
    pos_ += 2;
    return v;
}

uint32_t Cursor::u32() {
    need(4);
    uint32_t v = bytes_.readU32LE(pos_);
    pos_ += 4;
    return v;
}

float Cursor::f32() {
    uint32_t bits = u32();
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

void Cursor::skip(size_t n) {
    need(n);
    pos_ += n;
}

Header parseHeader(vpp::ByteView bytes) {
    if (bytes.size() < kHeaderSize) {
        throw FormatError("vint_doc: " + std::to_string(bytes.size()) + " bytes is shorter than the 30-byte header");
    }
    if (!looksLikeVintDoc(bytes)) throw FormatError("vint_doc: magic is not 0x00003027");
    Cursor c(bytes, 0);
    Header h;
    h.magic = c.u32();
    h.reserved04 = c.u32();
    h.version = c.u16();
    h.field0ARaw = c.u32();
    h.metadataCount = c.u32();
    h.criticalResourceCount = c.u32();
    h.secondaryOffsetRaw = c.u32();
    h.elementCount = c.u16();
    h.animationCount = c.u16();
    return h;
}

StringTable parseStringTable(vpp::ByteView bytes) {
    Cursor c(bytes, kHeaderSize);
    uint32_t count = c.u32();
    // Bound the allocation by what the file can hold before reserving.
    if ((bytes.size() - c.pos()) / 4 < count) {
        throw FormatError("vint_doc: string-offset array of " + std::to_string(count) +
                          " entries runs past end of file");
    }
    StringTable t;
    t.offsets.reserve(count);
    for (uint32_t i = 0; i < count; ++i) t.offsets.push_back(c.u32());
    t.base = c.pos();
    return t;
}

bool resolveStringNulTerminated(vpp::ByteView bytes, const StringTable& table, uint32_t index, std::string& out) {
    out.clear();
    if (index >= table.offsets.size()) return false;
    uint64_t start = static_cast<uint64_t>(table.base) + table.offsets[index];
    if (start >= bytes.size()) return false;
    const uint8_t* p = bytes.data() + start;
    const void* nul = std::memchr(p, 0, bytes.size() - static_cast<size_t>(start));
    if (!nul) return false;
    out.assign(reinterpret_cast<const char*>(p), static_cast<const uint8_t*>(nul) - p);
    return true;
}

CriticalResource readCriticalResource(Cursor& c, uint16_t version) {
    CriticalResource r;
    r.selectorRaw = c.u8();
    r.valueRaw = c.u32();
    if (version == 2) {
        r.hasVersion2Byte = true;
        r.version2ByteRaw = c.u8();
    }
    return r;
}

MetadataEntry readMetadataEntry(Cursor& c) {
    MetadataEntry m;
    m.nameIndex = c.u32();
    m.valueIndex = c.u32();
    return m;
}

ElementHead readElementHead(Cursor& c) {
    ElementHead e;
    e.typeIndex = c.u32();
    e.nameIndex = c.u32();
    e.childCount = c.u16();
    e.skippedByteRaw = c.u8();
    return e;
}

const char* const kRegisteredElementTypes[13] = {
    "element", "tween", "animation", "group", "clip", "bitmap", "text",
    "point", "video", "sr2_map", "bitmap_circle", "document", "gradient",
};

bool isRegisteredElementType(const std::string& name) {
    for (const char* t : kRegisteredElementTypes) {
        if (name == t) return true;
    }
    return false;
}

PropertyBlockHeader readPropertyBlockHeader(Cursor& c) {
    PropertyBlockHeader h;
    h.baselineOffsetRaw = c.u32();
    uint8_t n = c.u8();
    h.overrides.reserve(n);
    for (uint8_t i = 0; i < n; ++i) {
        OverrideEntry o;
        o.resolutionNameIndex = c.u32();
        o.blockOffsetRaw = c.u32();
        h.overrides.push_back(o);
    }
    return h;
}

uint32_t selectPropertyListOffset(const PropertyBlockHeader& h,
                                  const std::function<bool(uint32_t resolutionNameIndex)>& matchesActive) {
    for (const auto& o : h.overrides) {
        if (matchesActive && matchesActive(o.resolutionNameIndex)) return o.blockOffsetRaw;
    }
    return h.baselineOffsetRaw;
}

int propertyValueSize(uint8_t tag) {
    switch (tag) {
        case 0: return 0;
        case 1: case 2: case 3: case 4: return 4;
        case 5: return 1;
        case 6: return 12;
        case 7: return 8;
        default: return -1;
    }
}

uint32_t Property::rawU32() const {
    return static_cast<uint32_t>(value[0]) | (static_cast<uint32_t>(value[1]) << 8) |
           (static_cast<uint32_t>(value[2]) << 16) | (static_cast<uint32_t>(value[3]) << 24);
}

float Property::f32(size_t i) const {
    if (i >= 3) i = 2;
    uint32_t bits = static_cast<uint32_t>(value[i * 4]) | (static_cast<uint32_t>(value[i * 4 + 1]) << 8) |
                    (static_cast<uint32_t>(value[i * 4 + 2]) << 16) | (static_cast<uint32_t>(value[i * 4 + 3]) << 24);
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

bool Property::boolean() const {
    return value[0] != 0;
}

PropertyList readPropertyList(Cursor& c) {
    PropertyList list;
    while (true) {
        size_t tagPos = c.pos();
        uint8_t tag = c.u8();
        if (tag == 0) {
            list.terminated = true;
            return list;
        }
        int size = propertyValueSize(tag);
        if (size < 0) {
            c.seek(tagPos);
            list.unknownTag = tag;
            return list;
        }
        Property p;
        p.tag = tag;
        p.nameHash = c.u32();
        for (int i = 0; i < size; ++i) p.value[i] = c.u8();
        list.properties.push_back(p);
    }
}

} // namespace sr3vintdoc
