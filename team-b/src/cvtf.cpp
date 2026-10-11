// Implementation for sr3vehicle::CvtfCatalog (.cvtf_pc) — see
// include/sr3vehicle/cvtf.h's top comment for the real, disassembly-
// confirmed Region-1 formula this implements, and why it replaces this
// reader's own first-draft heuristic (same day, superseded before this
// reader was ever relied on for a real finding).
//
// CORRECTED 2026-10-03: the header is 48 bytes, not 16 - see cvtf.h's top
// comment for the real-byte check (against car_4dr_standard03.cvtf_pc)
// that confirmed spec-geometry-format.md §5.1's HIGH CONFIDENCE finding
// and settled the Region-2 "40 vs 45" composition question (45).

#include "sr3vehicle/cvtf.h"

#include <algorithm>
#include <cctype>

namespace sr3vehicle {

namespace {

std::string toLower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Reads one NUL-terminated C string starting at `offset`. Returns an empty
// string (not a throw) if `offset` is out of range or no terminator is
// found within the buffer — a malformed per-entry pointer should not abort
// the whole catalogue, matching this project's "surface partial content"
// convention (sr3fxo::ShaderWrapper, sr3d3d9bc::DisassembledShader, etc.).
std::string readCStringSafe(vpp::ByteView bytes, size_t offset) {
    if (offset >= bytes.size()) return {};
    size_t len = 0;
    try {
        len = bytes.cStringLength(offset);
    } catch (const std::out_of_range&) {
        return {};
    }
    std::string s;
    s.reserve(len);
    for (size_t i = 0; i < len; ++i) s.push_back(static_cast<char>(bytes.at(offset + i)));
    return s;
}

bool isPrintableAscii(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        uint8_t b = static_cast<uint8_t>(c);
        if (b < 0x20 || b > 0x7E) return false;
    }
    return true;
}

// The real, disassembly-confirmed idiom (spec §5.1, 0x00aaf9a0): a raw
// field value resolves to an absolute file offset `v + kCvtfFixupBase`
// unless it is the sentinel, which the caller checks separately.
size_t resolveFixup(uint32_t v) { return static_cast<size_t>(v) + kCvtfFixupBase; }

} // namespace

bool CvtfCatalog::tryParse(vpp::ByteView bytes, CvtfCatalog& out, std::string& whyNot) {
    if (bytes.size() < kCvtfHeaderSize) { whyNot = "smaller than the 48-byte header"; return false; }
    CvtfCatalog c;
    c.magic_ = bytes.readU32LE(0x00);
    if (c.magic_ != kCvtfMagic) { whyNot = "magic mismatch (not 'ftvc')"; return false; }
    c.sectionCount_ = bytes.readU32LE(0x04);
    if (c.sectionCount_ != kCvtfSectionCount) {
        whyNot = "section_count != 2 (this reader only covers the ID-2 'ftvc' shape spec §5.1 documents; "
                 "ID 8 'Character cvtf' is a structurally different format per spec §5.2 and is out of scope here)";
        return false;
    }
    c.region1Count_ = bytes.readU32LE(0x08);
    c.region1OffsetRaw_ = bytes.readU32LE(0x0C);
    const uint32_t table2OffsetRaw = bytes.readU32LE(0x10);
    c.table2Count_ = bytes.readU32LE(0x14);
    const uint32_t table3OffsetRaw = bytes.readU32LE(0x18);
    c.table3Count_ = bytes.readU32LE(0x1C);
    const uint32_t array1OffsetRaw = bytes.readU32LE(0x20);
    const uint32_t array1Count = bytes.readU32LE(0x24);
    const uint32_t array2OffsetRaw = bytes.readU32LE(0x28);
    const uint32_t array2Count = bytes.readU32LE(0x2C);

    if (table2OffsetRaw != kCvtfSentinel) c.table2Offset_ = resolveFixup(table2OffsetRaw);
    if (table3OffsetRaw != kCvtfSentinel) c.table3Offset_ = resolveFixup(table3OffsetRaw);

    if (c.region1OffsetRaw_ == kCvtfSentinel) {
        whyNot = "Region 1's own offset field (+0x0C) is the sentinel - no component table in this file";
        return false;
    }
    const size_t region1Start = resolveFixup(c.region1OffsetRaw_);
    if (region1Start > bytes.size()) { whyNot = "Region 1's quad table starts past the end of the file"; return false; }
    const uint64_t region1End = static_cast<uint64_t>(region1Start) +
                                 static_cast<uint64_t>(c.region1Count_) * kCvtfRegion1QuadSize;
    if (region1End > bytes.size()) { whyNot = "Region 1's quad table extends past the end of the file"; return false; }
    const size_t stringRegionStart = static_cast<size_t>(region1End);

    // ---- Region 1: raw quads, resolved via the real a+8/b+8/c+8 formula ----
    c.region1_.resize(static_cast<size_t>(c.region1Count_));
    for (size_t i = 0; i < c.region1_.size(); ++i) {
        const size_t o = region1Start + i * kCvtfRegion1QuadSize;
        CvtfRegion1Entry& e = c.region1_[i];
        e.a = bytes.readU32LE(o + 0x00);
        e.b = bytes.readU32LE(o + 0x04);
        e.c = bytes.readU32LE(o + 0x08);
        e.tag = bytes.readU32LE(o + 0x0C);
        e.quadIndex = i;
        e.optionCount = e.tag & 0xFFu;
        e.tagUpperBits = e.tag >> 8;

        e.aIsSentinel = (e.a == kCvtfSentinel);
        if (!e.aIsSentinel) {
            size_t nameOff = resolveFixup(e.a);
            e.name = readCStringSafe(bytes, nameOff);
        }

        e.bIsSentinel = (e.b == kCvtfSentinel);
        if (!e.bIsSentinel) {
            size_t variantOff = resolveFixup(e.b);
            e.variant = readCStringSafe(bytes, variantOff);
        }

        e.cIsSentinel = (e.c == kCvtfSentinel);
        if (!e.cIsSentinel) {
            size_t arrBase = resolveFixup(e.c);
            for (uint32_t k = 0; k < e.optionCount; ++k) {
                size_t elemOff = arrBase + static_cast<size_t>(k) * kCvtfOptionElementSize;
                if (elemOff + kCvtfOptionElementSize > bytes.size()) break; // truncated - report what fit, don't throw
                CvtfOptionElement el;
                for (int w = 0; w < 15; ++w) el.words[static_cast<size_t>(w)] = bytes.readU32LE(elemOff + static_cast<size_t>(w) * 4);
                if (el.words[1] != kCvtfSentinel) {
                    std::string s = readCStringSafe(bytes, resolveFixup(el.words[1]));
                    if (isPrintableAscii(s)) el.word1AsString = s;
                }
                e.options.push_back(std::move(el));
            }
        }
    }

    // ---- Region 2 (wheel names) + the "Family00N" array: both are real
    //      dword-offset arrays (header fields +0x20/+0x24 and +0x28/+0x2C),
    //      NOT a content-shape scan - CORRECTED 2026-10-03. Each array is a
    //      flat run of `count` raw u32 offsets starting at the array's own
    //      resolved offset; each element is ITSELF resolved via the same
    //      sentinel/+8 idiom to a plain string. This reader's own real-byte
    //      check (this header's own top comment) found 45/45 and 7/7
    //      elements resolving cleanly in the one real sample tested - no
    //      scanning forward from Region 1's own end offset to guess where
    //      Region 2 starts is needed any more; the array's own offset
    //      field gives an exact location, independent of Region 1
    //      entirely. ----
    auto readDwordOffsetArray = [&](uint32_t offsetRaw, uint32_t count, std::vector<std::string>& outNames) {
        if (offsetRaw == kCvtfSentinel) return; // absent - leave outNames empty
        const size_t arrBase = resolveFixup(offsetRaw);
        for (uint32_t i = 0; i < count; ++i) {
            const size_t elemOff = arrBase + static_cast<size_t>(i) * 4;
            if (elemOff + 4 > bytes.size()) break; // truncated - report what fit, don't throw
            const uint32_t raw = bytes.readU32LE(elemOff);
            if (raw == kCvtfSentinel) { outNames.emplace_back(); continue; }
            outNames.push_back(readCStringSafe(bytes, resolveFixup(raw)));
        }
    };
    readDwordOffsetArray(array1OffsetRaw, array1Count, c.region2Names_);
    readDwordOffsetArray(array2OffsetRaw, array2Count, c.familyNames_);

    // ---- allStrings(): the reliable, boundary-agnostic fallback — every
    //      NUL-terminated string from stringRegionStart to EOF, in order.
    //      NOTE: this walks the SAME byte range Region 1's own option
    //      arrays (raw binary, not text) also occupy, so entries here can
    //      include short garbage-looking "strings" that are really
    //      fragments of binary option-array bytes reinterpreted as text -
    //      an inherent, honestly-stated property of a boundary-agnostic
    //      byte-level scan, not a bug. Prefer region1()'s own
    //      name/variant/options fields (the real, structured parse) over
    //      this for anything Region 1 already covers; use this mainly for
    //      Region 2/3 content search. ----
    {
        size_t pos = stringRegionStart;
        while (pos < bytes.size()) {
            size_t len = 0;
            try {
                len = bytes.cStringLength(pos);
            } catch (const std::out_of_range&) {
                break; // trailing unterminated bytes at EOF — stop, don't fabricate a string
            }
            CvtfRawString rs;
            rs.offset = pos;
            rs.text = readCStringSafe(bytes, pos);
            c.allStrings_.push_back(std::move(rs));
            pos += len + 1;
        }
    }

    out = std::move(c);
    whyNot.clear();
    return true;
}

CvtfCatalog CvtfCatalog::parse(vpp::ByteView bytes) {
    CvtfCatalog c;
    std::string why;
    if (!tryParse(bytes, c, why)) throw FormatError("cvtf catalog not applicable: " + why);
    return c;
}

const CvtfRegion1Entry* CvtfCatalog::findEntryByName(const std::string& name) const {
    const std::string wantLower = toLower(name);
    for (const auto& e : region1_)
        if (toLower(e.name) == wantLower) return &e;
    for (const auto& e : region1_)
        if (toLower(e.name).find(wantLower) != std::string::npos) return &e;
    return nullptr;
}

} // namespace sr3vehicle
