// Implementation for sr3vehicle::CvtfCatalog (.cvtf_pc) — see
// include/sr3vehicle/cvtf.h's top comment for the real, disassembly-
// confirmed Region-1 formula this implements, and why it replaces this
// reader's own first-draft heuristic (same day, superseded before this
// reader was ever relied on for a real finding).

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

// The real, disassembly-confirmed idiom (spec §5.1, FUN_00aaf9a0): a raw
// field value resolves to an absolute file offset `v + kCvtfFixupBase`
// unless it is the sentinel, which the caller checks separately.
size_t resolveFixup(uint32_t v) { return static_cast<size_t>(v) + kCvtfFixupBase; }

} // namespace

bool CvtfCatalog::tryParse(vpp::ByteView bytes, CvtfCatalog& out, std::string& whyNot) {
    if (bytes.size() < kCvtfHeaderSize) { whyNot = "smaller than the 16-byte header"; return false; }
    CvtfCatalog c;
    c.magic_ = bytes.readU32LE(0x00);
    if (c.magic_ != kCvtfMagic) { whyNot = "magic mismatch (not 'ftvc')"; return false; }
    c.sectionCount_ = bytes.readU32LE(0x04);
    if (c.sectionCount_ != kCvtfSectionCount) {
        whyNot = "section_count != 2 (this reader only covers the ID-2 'ftvc' shape spec §5.1 documents; "
                 "ID 8 'Character cvtf' is a structurally different format per spec §5.2 and is out of scope here)";
        return false;
    }
    c.entryCount1_ = bytes.readU32LE(0x08);
    c.entryCount2_ = bytes.readU32LE(0x0C);

    const uint64_t quadCount = static_cast<uint64_t>(c.entryCount1_) + kCvtfRegion1LeadingQuads;
    const uint64_t region1End = kCvtfHeaderSize + quadCount * kCvtfRegion1QuadSize;
    if (region1End > bytes.size()) { whyNot = "Region 1's quad table extends past the end of the file"; return false; }
    const size_t stringRegionStart = static_cast<size_t>(region1End);

    // ---- Region 1: raw quads, resolved via the real a+8/b+8/c+8 formula ----
    size_t maxEntryEnd = stringRegionStart; // running max of every entry's own real end offset - Region 2 lower bound
    c.region1_.resize(static_cast<size_t>(quadCount));
    for (size_t i = 0; i < c.region1_.size(); ++i) {
        const size_t o = kCvtfHeaderSize + i * kCvtfRegion1QuadSize;
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
            size_t nameLen = 0;
            try { nameLen = bytes.cStringLength(nameOff); } catch (const std::out_of_range&) {}
            if (nameOff + nameLen + 1 > maxEntryEnd) maxEntryEnd = nameOff + nameLen + 1;
        }

        e.bIsSentinel = (e.b == kCvtfSentinel);
        if (!e.bIsSentinel) {
            size_t variantOff = resolveFixup(e.b);
            e.variant = readCStringSafe(bytes, variantOff);
            size_t variantLen = 0;
            try { variantLen = bytes.cStringLength(variantOff); } catch (const std::out_of_range&) {}
            if (variantOff + variantLen + 1 > maxEntryEnd) maxEntryEnd = variantOff + variantLen + 1;
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
            size_t arrEnd = arrBase + static_cast<size_t>(e.optionCount) * kCvtfOptionElementSize;
            if (arrEnd > maxEntryEnd) maxEntryEnd = arrEnd;
        }
    }

    // ---- Region 2: best-effort. Lower bound = maxEntryEnd (the highest
    //      real end-offset among every Region-1 entry's own name/variant/
    //      option-array span); then scan forward for the first string
    //      shaped like spec §5.1's own documented Region-2 wheel-name
    //      vocabulary as a confirming anchor (Region 1/2's own boundary is
    //      NOT given by any confirmed formula - see this header's own top
    //      comment). If no such anchor is found within a generous window,
    //      falls back to reading entryCount2() strings starting right at
    //      the lower bound. ----
    {
        size_t searchPos = maxEntryEnd;
        size_t region2Start = maxEntryEnd;
        bool foundAnchor = false;
        size_t stringsScanned = 0;
        constexpr size_t kAnchorScanCap = 200; // generous; see header comment
        while (searchPos < bytes.size() && stringsScanned < kAnchorScanCap) {
            size_t len = 0;
            try {
                len = bytes.cStringLength(searchPos);
            } catch (const std::out_of_range&) {
                break;
            }
            std::string s = readCStringSafe(bytes, searchPos);
            bool looksLikeRegion2 =
                (s.find("_Profile") != std::string::npos) ||
                (s.rfind("W_", 0) == 0 && (s.find("_F") != std::string::npos || s.find("_R") != std::string::npos));
            if (looksLikeRegion2) { region2Start = searchPos; foundAnchor = true; break; }
            searchPos += len + 1;
            ++stringsScanned;
        }
        (void)foundAnchor; // informational only - region2Start already defaults sensibly either way
        size_t pos = region2Start;
        for (uint32_t i = 0; i < c.entryCount2_ && pos < bytes.size(); ++i) {
            size_t len = 0;
            try { len = bytes.cStringLength(pos); } catch (const std::out_of_range&) { break; }
            c.region2Names_.push_back(readCStringSafe(bytes, pos));
            pos += len + 1;
        }
    }

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
