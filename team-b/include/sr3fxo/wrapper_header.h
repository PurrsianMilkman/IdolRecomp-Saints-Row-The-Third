#pragma once

// Header-driven reader for the .fxo_pc / .fxo_pc_dx11 wrapper, built from
// spec-fxo-format.md §6-§8 (the second, disassembly pass, 2026-09-20). It
// supersedes the scan-based recipe of the first pass as the PRIMARY way to
// locate the embedded blobs: the header is variable-size and its size is a
// closed-form function of nine counts (§7.2),
//
//     H = 0x80 + 0x10*c0 + 8*(c1+c2+c3+c4) + 8*(nVS+nMid+nPS) + 0x10*c8
//
// and blobs follow at the running offset rounded up to 16 (§6.4, §7.5),
// vertex table first, then middle (geometry) table, then pixel table.
//
// Header-only on purpose: everything here is small, table-driven and needs
// no translation unit of its own.
//
// WHAT IS AND IS NOT CONFIRMED (spec §7, §9): the formula and the 16-byte
// alignment are CONFIRMED by disassembly and by ONE real sample (1/1). T0's
// field meanings, T2/T4, the role table's individual roles, the geometry
// slot's blob placement and any version above 14 are disassembly-only; the
// tables are surfaced raw and no meaning beyond the spec's own labels is
// assigned. Applicability is explicit: tryParse() says WHY a file is not
// covered instead of guessing.
//
// The Direct3D 9 loader does not step over middle-table blobs (§8.2 (b)),
// the Direct3D 11 loader does. layoutBlobs() defaults to the latter (all
// three tables, spec §8.2's "Conclusion"); pass includeMiddle=false to get
// the Direct3D 9 loader's placement.

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "sr3fxo/errors.h"
#include "vpp/byte_view.h"

namespace sr3fxo {

constexpr uint32_t kWrapperMagic = 0x4B42A1EEu;  // spec §6.3 (same value as kMagic)
constexpr int32_t kMinimumVersion = 14;          // spec §6.3: signed compare, "below 14" rejected
constexpr size_t kFixedHeaderSize = 0x80;        // spec §7.1
constexpr size_t kBlobAlignment = 16;            // spec §6.4, §7.5

// Header offsets (spec §7.1).
constexpr size_t kOffVersion = 0x04;
constexpr size_t kOffFlags = 0x08;
constexpr size_t kOffCount0 = 0x0C; // s16 x5: c0..c4 at 0x0C,0x0E,0x10,0x12,0x14
constexpr size_t kOffVertexCount = 0x16;
constexpr size_t kOffMiddleCount = 0x17;
constexpr size_t kOffPixelCount = 0x18;
constexpr size_t kOffRoleBytes = 0x60; // 10 x s8
constexpr size_t kOffGroupBase = 0x6A; // s8
constexpr size_t kOffCount8 = 0x78;    // u32

// Role flag bits for roles 0..9 (spec §7.4); role 10 (the indexed group) uses 0x100.
constexpr uint32_t kRoleFlagBits[10] = {0x001, 0x200, 0x004, 0x400, 0x010, 0x020, 0x040, 0x080, 0x800, 0x1000};
constexpr uint32_t kGroupFlagBit = 0x100;

enum class Stage { Vertex, Middle, Pixel };

// One 8-byte vertex/middle/pixel table entry (spec §7.3). `reserved` is
// ignored by the loader (overwritten with 0 in memory) and is 0 in the sample.
struct StageEntry {
    uint32_t length = 0;
    uint32_t reserved = 0;
};

// One 8-byte T1/T2/T3/T4 entry (spec §7.3).
struct ConstantEntry {
    uint32_t nameHash = 0;         // +0: CRC-32 of the lower-cased name (§7.6)
    uint8_t registerIndex = 0;     // +4: first constant register
    uint8_t byte5 = 0;             // +5: not read by any accessor (OPEN)
    uint8_t registersPerElement = 0; // +6: as declared
    uint8_t elementCount = 0;      // +7
};

// One 0x10-byte T0 entry (spec §7.3): layout CONFIRMED by disassembly,
// meaning HIGH CONFIDENCE, NOT validated on real bytes.
struct SamplerEntry {
    uint32_t nameHash = 0;
    uint8_t bytes[12] = {0}; // +4..+0xF raw
};

// One 0x10-byte T8 "pass" entry (spec §7.3).
struct PassEntry {
    int16_t vertexIndex = -1; // +0, CONFIRMED
    int16_t word2 = -1;       // +2, HYPOTHESIS: middle (geometry) index
    int16_t pixelIndex = -1;  // +4, CONFIRMED
    int16_t word6 = 0;        // +6, OPEN
    uint32_t nameHash = 0;    // +8
    uint32_t word0C = 0;      // +0xC, OPEN
};

// One located blob.
struct WrapperBlob {
    Stage stage = Stage::Vertex;
    size_t tableIndex = 0; // entry index within its stage table
    size_t offset = 0;     // file offset of the blob's first byte
    size_t length = 0;
};

struct LayoutOptions {
    bool includeMiddle = true;    // step over middle-table blobs (Direct3D 11 loader, §8.2)
    size_t alignment = kBlobAlignment;
    bool alignFirstBlob = true;   // §6.4: padding sits before EVERY blob, including the first
    size_t startOffset = static_cast<size_t>(-1); // running offset at start; default = the unrounded header size
};

inline uint32_t crc32Table(size_t i) {
    uint32_t c = static_cast<uint32_t>(i);
    for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    return c;
}

// The ordinary table-driven reflected CRC-32 step with NO initial and NO
// final inversion (spec §7.6): crc = (crc >> 8) ^ table[(byte ^ crc) & 0xFF],
// starting from 0 (or a seed).
inline uint32_t crc32Raw(const uint8_t* data, size_t n, uint32_t crc = 0) {
    for (size_t i = 0; i < n; ++i) crc = (crc >> 8) ^ crc32Table((data[i] ^ crc) & 0xFFu);
    return crc;
}

// The name hash the constant/sampler/pass tables use: CRC (above) over the
// LOWER-CASED ASCII name (spec §7.6 (b)).
inline uint32_t hashLowerName(const std::string& name) {
    uint32_t crc = 0;
    for (char ch : name) {
        uint8_t b = static_cast<uint8_t>(ch);
        if (b >= 'A' && b <= 'Z') b = static_cast<uint8_t>(b - 'A' + 'a');
        crc = (crc >> 8) ^ crc32Table((b ^ crc) & 0xFFu);
    }
    return crc;
}

class WrapperHeader {
public:
    // Non-throwing. Returns true and fills `out` iff the spec's header-size
    // formula is APPLICABLE to `bytes`: at least the 0x80-byte fixed part,
    // the magic, version >= 14 (signed), every count non-negative, and every
    // one of the nine tables fits inside `bytes`. Otherwise returns false and
    // sets `whyNot` (stable short text, usable as a histogram key).
    static bool tryParse(vpp::ByteView bytes, WrapperHeader& out, std::string& whyNot) {
        if (bytes.size() < kFixedHeaderSize) { whyNot = "smaller than the 0x80-byte fixed header"; return false; }
        WrapperHeader h;
        h.fileSize_ = bytes.size();
        h.magic_ = bytes.readU32LE(0);
        if (h.magic_ != kWrapperMagic) { whyNot = "magic mismatch"; return false; }
        h.version_ = static_cast<int32_t>(bytes.readU32LE(kOffVersion));
        if (h.version_ < kMinimumVersion) { whyNot = "version below 14"; return false; }
        h.flags_ = bytes.readU32LE(kOffFlags);
        for (int i = 0; i < 5; ++i) {
            h.c_[i] = static_cast<int16_t>(bytes.readU16LE(kOffCount0 + 2 * static_cast<size_t>(i)));
            if (h.c_[i] < 0) { whyNot = "negative s16 table count"; return false; }
        }
        h.nVS_ = static_cast<int8_t>(bytes.at(kOffVertexCount));
        h.nMid_ = static_cast<int8_t>(bytes.at(kOffMiddleCount));
        h.nPS_ = static_cast<int8_t>(bytes.at(kOffPixelCount));
        if (h.nVS_ < 0 || h.nMid_ < 0 || h.nPS_ < 0) { whyNot = "negative s8 stage count"; return false; }
        h.c8_ = bytes.readU32LE(kOffCount8);

        // 64-bit so a corrupt c8 cannot wrap the sum.
        const uint64_t t0 = kFixedHeaderSize;
        const uint64_t t1 = t0 + 0x10ull * h.c_[0];
        const uint64_t t2 = t1 + 8ull * h.c_[1];
        const uint64_t t3 = t2 + 8ull * h.c_[2];
        const uint64_t t4 = t3 + 8ull * h.c_[3];
        const uint64_t vs = t4 + 8ull * h.c_[4];
        const uint64_t mid = vs + 8ull * static_cast<uint64_t>(h.nVS_);
        const uint64_t ps = mid + 8ull * static_cast<uint64_t>(h.nMid_);
        const uint64_t t8 = ps + 8ull * static_cast<uint64_t>(h.nPS_);
        const uint64_t hend = t8 + 0x10ull * h.c8_;
        if (hend > bytes.size()) { whyNot = "tables extend past the end of the file"; return false; }
        h.headerSize_ = static_cast<size_t>(hend);

        auto readStage = [&](uint64_t at, int n, std::vector<StageEntry>& v) {
            for (int i = 0; i < n; ++i) {
                StageEntry e;
                e.length = bytes.readU32LE(static_cast<size_t>(at) + 8 * static_cast<size_t>(i));
                e.reserved = bytes.readU32LE(static_cast<size_t>(at) + 8 * static_cast<size_t>(i) + 4);
                v.push_back(e);
            }
        };
        auto readConst = [&](uint64_t at, int n, std::vector<ConstantEntry>& v) {
            for (int i = 0; i < n; ++i) {
                const size_t o = static_cast<size_t>(at) + 8 * static_cast<size_t>(i);
                ConstantEntry e;
                e.nameHash = bytes.readU32LE(o);
                e.registerIndex = bytes.at(o + 4);
                e.byte5 = bytes.at(o + 5);
                e.registersPerElement = bytes.at(o + 6);
                e.elementCount = bytes.at(o + 7);
                v.push_back(e);
            }
        };
        for (int i = 0; i < h.c_[0]; ++i) {
            const size_t o = static_cast<size_t>(t0) + 0x10 * static_cast<size_t>(i);
            SamplerEntry s;
            s.nameHash = bytes.readU32LE(o);
            for (int k = 0; k < 12; ++k) s.bytes[k] = bytes.at(o + 4 + static_cast<size_t>(k));
            h.t0_.push_back(s);
        }
        readConst(t1, h.c_[1], h.t1_);
        readConst(t2, h.c_[2], h.t2_);
        readConst(t3, h.c_[3], h.t3_);
        readConst(t4, h.c_[4], h.t4_);
        readStage(vs, h.nVS_, h.vs_);
        readStage(mid, h.nMid_, h.mid_);
        readStage(ps, h.nPS_, h.ps_);
        for (uint32_t i = 0; i < h.c8_; ++i) {
            const size_t o = static_cast<size_t>(t8) + 0x10 * static_cast<size_t>(i);
            PassEntry p;
            p.vertexIndex = static_cast<int16_t>(bytes.readU16LE(o));
            p.word2 = static_cast<int16_t>(bytes.readU16LE(o + 2));
            p.pixelIndex = static_cast<int16_t>(bytes.readU16LE(o + 4));
            p.word6 = static_cast<int16_t>(bytes.readU16LE(o + 6));
            p.nameHash = bytes.readU32LE(o + 8);
            p.word0C = bytes.readU32LE(o + 12);
            h.t8_.push_back(p);
        }
        for (size_t k = 0; k < 10; ++k) h.roleBytes_[k] = static_cast<int8_t>(bytes.at(kOffRoleBytes + k));
        h.groupBase_ = static_cast<int8_t>(bytes.at(kOffGroupBase));
        out = std::move(h);
        whyNot.clear();
        return true;
    }

    // Throwing convenience over tryParse().
    static WrapperHeader parse(vpp::ByteView bytes) {
        WrapperHeader h;
        std::string why;
        if (!tryParse(bytes, h, why)) throw FormatError("fxo wrapper header not applicable: " + why);
        return h;
    }

    uint32_t flags() const { return flags_; }
    int32_t version() const { return version_; }
    int16_t count(size_t tableIndex0to4) const { return c_[tableIndex0to4]; }
    int vertexCount() const { return nVS_; }
    int middleCount() const { return nMid_; }
    int pixelCount() const { return nPS_; }
    uint32_t passCount() const { return c8_; }
    // H: the unrounded header size (spec §7.2).
    size_t headerSize() const { return headerSize_; }
    // The first blob's file offset under the spec's rule: align16(H).
    size_t firstBlobOffset() const { return alignUp(headerSize_, kBlobAlignment); }

    const std::vector<SamplerEntry>& samplers() const { return t0_; }           // T0
    const std::vector<ConstantEntry>& constantsT1() const { return t1_; }
    const std::vector<ConstantEntry>& constantsT2() const { return t2_; }
    const std::vector<ConstantEntry>& constantsT3() const { return t3_; }
    const std::vector<ConstantEntry>& constantsT4() const { return t4_; }
    const std::vector<StageEntry>& vertexTable() const { return vs_; }
    const std::vector<StageEntry>& middleTable() const { return mid_; }
    const std::vector<StageEntry>& pixelTable() const { return ps_; }
    const std::vector<PassEntry>& passes() const { return t8_; }                 // T8
    int8_t roleIndexByte(size_t role0to9) const { return roleBytes_[role0to9]; }
    int8_t groupBase() const { return groupBase_; }

    // Locates every non-empty blob (a length of 0 means "no shader", §6.4),
    // walking VS then (optionally) middle then PS with the running offset.
    // `endOfBlobs` receives the running offset after the last blob (the value
    // the spec says equals the file length on a well-formed file).
    std::vector<WrapperBlob> layoutBlobs(const LayoutOptions& opt, size_t& endOfBlobs) const {
        std::vector<WrapperBlob> out;
        size_t pos = opt.startOffset == static_cast<size_t>(-1) ? headerSize_ : opt.startOffset;
        bool first = true;
        auto walk = [&](Stage st, const std::vector<StageEntry>& t) {
            for (size_t i = 0; i < t.size(); ++i) {
                if (t[i].length == 0) continue;
                size_t start = (first && !opt.alignFirstBlob) ? pos : alignUp(pos, opt.alignment);
                first = false;
                out.push_back({st, i, start, t[i].length});
                pos = start + t[i].length;
            }
        };
        walk(Stage::Vertex, vs_);
        if (opt.includeMiddle) walk(Stage::Middle, mid_);
        walk(Stage::Pixel, ps_);
        endOfBlobs = pos;
        return out;
    }

    // The spec's own rule, defaults throughout.
    std::vector<WrapperBlob> layoutBlobs(size_t& endOfBlobs) const { return layoutBlobs(LayoutOptions{}, endOfBlobs); }

    // "Is role k present?" (spec §7.4). Roles 0..9: the flag bit alone.
    // Role 10 (member n of the indexed group): flag 0x100, base byte >= 0 and
    // base + n < c8.
    bool rolePresent(size_t role, size_t groupMember = 0) const {
        if (role < 10) return (flags_ & kRoleFlagBits[role]) != 0;
        return (flags_ & kGroupFlagBit) != 0 && groupBase_ >= 0 &&
               static_cast<uint64_t>(groupBase_) + groupMember < c8_;
    }

    static size_t alignUp(size_t v, size_t a) { return (v + a - 1) / a * a; }

private:
    size_t fileSize_ = 0;
    uint32_t magic_ = 0;
    int32_t version_ = 0;
    uint32_t flags_ = 0;
    int16_t c_[5] = {0, 0, 0, 0, 0};
    int nVS_ = 0, nMid_ = 0, nPS_ = 0;
    uint32_t c8_ = 0;
    size_t headerSize_ = 0;
    std::vector<SamplerEntry> t0_;
    std::vector<ConstantEntry> t1_, t2_, t3_, t4_;
    std::vector<StageEntry> vs_, mid_, ps_;
    std::vector<PassEntry> t8_;
    int8_t roleBytes_[10] = {0};
    int8_t groupBase_ = 0;
};

} // namespace sr3fxo
