#include "sr3vintdoc/vint_doc.h"

#include <algorithm>
#include <cstring>
#include <utility>

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

// ---- Full-document walk (CHOSEN layout, see vint_doc.h) -----------------

DocumentStringTable parseTrailingStringTable(vpp::ByteView bytes, const Header& h) {
    DocumentStringTable t;
    t.start = h.secondaryOffsetRaw;
    if (t.start < kHeaderSize || t.start > bytes.size() || bytes.size() - t.start < 8) {
        throw FormatError("vint_doc: string table at header +0x16 (" + std::to_string(t.start) +
                          ") does not fit in the file (size " + std::to_string(bytes.size()) + ")");
    }
    Cursor c(bytes, t.start);
    uint32_t count = c.u32();
    if (count == 0) throw FormatError("vint_doc: string count 0 (the loader fails the whole document, Sec3.1)");
    t.poolSize = c.u32();
    size_t afterSizes = c.pos();
    if ((bytes.size() - afterSizes) / 4 < count) {
        throw FormatError("vint_doc: string table of " + std::to_string(count) + " entries runs past end of file");
    }
    size_t poolStart = afterSizes + static_cast<size_t>(count) * 4;
    // The loader's own check (0x00e1fbe0): L + 4N <= bytes remaining.
    if (bytes.size() - poolStart < t.poolSize) {
        throw FormatError("vint_doc: string table (" + std::to_string(count) + " entries, pool " +
                          std::to_string(t.poolSize) + " bytes) runs past end of file");
    }
    t.endsAtEof = bytes.size() - poolStart == t.poolSize;
    t.offsets.reserve(count);
    t.strings.reserve(count);
    for (uint32_t i = 0; i < count; ++i) t.offsets.push_back(c.u32());
    const uint8_t* pool = bytes.data() + poolStart;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t o = t.offsets[i];
        if (o >= t.poolSize) {
            throw FormatError("vint_doc: string " + std::to_string(i) + " offset " + std::to_string(o) +
                              " is outside the " + std::to_string(t.poolSize) + "-byte pool");
        }
        const void* nul = std::memchr(pool + o, 0, t.poolSize - o);
        if (!nul) throw FormatError("vint_doc: string " + std::to_string(i) + " has no NUL inside the pool");
        t.strings.emplace_back(reinterpret_cast<const char*>(pool + o), static_cast<const uint8_t*>(nul) - (pool + o));
    }
    return t;
}

ElementRecordHead readElementRecordHead(Cursor& c) {
    ElementRecordHead e;
    e.nameIndex = c.u32();
    e.typeIndex = c.u32();
    e.childCount = c.u16();
    e.rawByte = c.u8();
    return e;
}

std::vector<Property> ElementNode::effectiveProperties(const std::string& activeResolution) const {
    std::vector<Property> out;
    const ResolutionOverride* match = nullptr;
    if (!activeResolution.empty()) {
        for (const auto& o : overrides) {
            if (o.resolutionName == activeResolution) { match = &o; break; } // first match only; later pairs never read
        }
    }
    std::vector<uint32_t> applied;
    if (match) {
        for (const auto& p : match->list.properties) {
            out.push_back(p);
            applied.push_back(p.nameHash);
        }
    }
    for (const auto& p : baseline.properties) {
        if (std::find(applied.begin(), applied.end(), p.nameHash) != applied.end()) continue; // consumed, not applied: the override wins
        out.push_back(p);
    }
    return out;
}

const std::string* Document::metadataValue(const std::string& name) const {
    for (const auto& m : metadataStrings) {
        if (m.name == name) return &m.value;
    }
    return nullptr;
}

namespace {

size_t countRecords(const std::vector<ElementNode>& v) {
    size_t n = 0;
    for (const auto& e : v) n += 1 + countRecords(e.children);
    return n;
}

struct Walker {
    vpp::ByteView file; // the whole document: property-list jumps are file-absolute (Sec5, 0x00e29860)
    vpp::ByteView tree; // [0, header +0x16): main-cursor reads are bounded by the string table's start
    const DocumentStringTable& strings;
    size_t records = 0;

    const std::string& str(uint32_t index, const char* what, size_t at) const {
        if (index >= strings.strings.size()) {
            throw FormatError(std::string("vint_doc: ") + what + " string index " + std::to_string(index) +
                              " at " + std::to_string(at) + " is outside the " +
                              std::to_string(strings.strings.size()) + "-entry string table");
        }
        return strings.strings[index];
    }

    PropertyList readListAt(size_t at) const {
        Cursor lc(file, at);
        PropertyList l = readPropertyList(lc);
        if (!l.terminated) {
            throw FormatError("vint_doc: property list at " + std::to_string(at) + " has unknown tag " +
                              std::to_string(l.unknownTag) + " at " + std::to_string(lc.pos()));
        }
        return l;
    }

    // The loader rejects an offset >= file size (its caller then ignores the
    // failure and reads on from a misplaced cursor); this parser refuses
    // such a file instead of reproducing that garbage read. No shipped file
    // has one.
    size_t checkedOffset(uint32_t off, size_t elementAt) const {
        if (off >= file.size()) {
            throw FormatError("vint_doc: property list offset " + std::to_string(off) + " of element at " +
                              std::to_string(elementAt) + " is outside the file");
        }
        return off;
    }

    // Returns the position just past `list`'s zero terminator when read from `at`.
    static size_t listEnd(size_t at, const PropertyList& list) {
        size_t n = at;
        for (const auto& p : list.properties) n += 1 + 4 + static_cast<size_t>(propertyValueSize(p.tag));
        return n + 1;
    }

    ElementNode element(Cursor& c, int depth) {
        if (depth > 64) throw FormatError("vint_doc: element tree deeper than 64 levels");
        // Each record takes at least 16 bytes (11-byte head + 5-byte block
        // header) plus a 1-byte list, so this bound can never reject a file
        // the layout accepts; it only stops a hostile child count early.
        if (++records > tree.size() / 17 + 1) throw FormatError("vint_doc: more element records than the tree can hold");
        ElementNode n;
        n.fileOffset = c.pos();
        ElementRecordHead head = readElementRecordHead(c);
        n.nameIndex = head.nameIndex;
        n.typeIndex = head.typeIndex;
        n.rawByte = head.rawByte;
        n.name = str(head.nameIndex, "element name", n.fileOffset);
        n.type = str(head.typeIndex, "element type", n.fileOffset);

        // Sec5 property block (0x00e29860, CONFIRMED - spec-vint-doc-format.md
        // at 67455c3): u32 baseline offset, u8 pair count, count x 8-byte
        // (resolution-name index, override offset) pairs; both offsets
        // file-absolute. The loader reads the matching override list (if
        // any), then ALWAYS jumps to the baseline and reads it, and does NOT
        // restore the cursor: children follow the baseline list's zero tag.
        // This parser reads EVERY override list (not only an active one) so
        // the document keeps all of them; the selection happens later
        // (ElementNode::effectiveProperties).
        PropertyBlockHeader block = readPropertyBlockHeader(c);
        size_t afterTable = c.pos();
        n.overrides.resize(block.overrides.size());
        size_t expect = afterTable; // only for the inline/contiguous diagnostic below
        for (size_t i = 0; i < block.overrides.size(); ++i) {
            auto& o = n.overrides[i];
            o.resolutionNameIndex = block.overrides[i].resolutionNameIndex;
            o.resolutionName = str(o.resolutionNameIndex, "override resolution name", n.fileOffset);
            size_t at = checkedOffset(block.overrides[i].blockOffsetRaw, n.fileOffset);
            o.list = readListAt(at);
            if (at != expect) n.listsInlineInOrder = false;
            expect = listEnd(at, o.list);
        }
        size_t baseAt = checkedOffset(block.baselineOffsetRaw, n.fileOffset);
        if (baseAt != expect) n.listsInlineInOrder = false;
        n.baseline = readListAt(baseAt);
        c.seek(listEnd(baseAt, n.baseline)); // CONFIRMED: cursor left just past the baseline list
        n.children.reserve(head.childCount);
        for (uint16_t i = 0; i < head.childCount; ++i) n.children.push_back(element(c, depth + 1));
        return n;
    }
};

} // namespace

size_t Document::totalRecordCount() const {
    return countRecords(elements) + countRecords(animations);
}

Document parseDocument(vpp::ByteView bytes) {
    Document d;
    d.header = parseHeader(bytes);
    d.strings = parseTrailingStringTable(bytes, d.header);

    vpp::ByteView tree(bytes.data(), d.strings.start);
    Walker w{bytes, tree, d.strings};
    Cursor c(tree, kHeaderSize);
    for (uint32_t i = 0; i < d.header.criticalResourceCount; ++i) {
        if (i > tree.size()) throw FormatError("vint_doc: critical-resource count larger than the file");
        d.criticalResources.push_back(readCriticalResource(c, d.header.version));
    }
    for (uint32_t i = 0; i < d.header.metadataCount; ++i) {
        if (i > tree.size()) throw FormatError("vint_doc: metadata count larger than the file");
        size_t at = c.pos();
        MetadataEntry m = readMetadataEntry(c);
        d.metadata.push_back(m);
        d.metadataStrings.push_back({w.str(m.nameIndex, "metadata name", at), w.str(m.valueIndex, "metadata value", at)});
    }
    for (uint16_t i = 0; i < d.header.elementCount; ++i) d.elements.push_back(w.element(c, 0));
    for (uint16_t i = 0; i < d.header.animationCount; ++i) d.animations.push_back(w.element(c, 0));
    d.treeEnd = c.pos();
    return d;
}

} // namespace sr3vintdoc
