// Reader for `.cefct_pc` particle/visual-effect files (spec-effects-format.md).
//
// Built ONLY from fields the spec marks CONFIRMED (disassembly and/or byte
// replay). It reads the container of the file - the shared material block,
// the `71BW` root header, and the six count/array pairs on that root - and
// nothing inside the per-record parameter blocks beyond the pointer-slot
// layout the spec's own exact-tiling gate (§6.11) already relies on. It does
// NOT decode particle behaviour (curves, colours, sizes): those field maps
// (§6.7) are only partly CONFIRMED and this reader's job is to locate and
// count things, not to re-emit an effect.
//
// LAYOUT (all "root+" offsets are relative to the position of the `71BW`
// marker, which the spec calls "root"; every pointer on disk is either
// 0xFFFFFFFF (null) or a byte offset relative to root - spec §5.3):
//
//   file+0x00  material block: magic 0x00043854, name-table length @+4,
//              zero @+8, texture count @+0xC, texture names from +0x20
//              (spec §2, §5.2). Measured on 1,812/1,812 real files: the
//              length is exactly the sum of the names' (strlen+1) - the
//              `.effectx` source name is NOT inside the block (spec §5.2's
//              wording says it is; the root arithmetic below is unaffected).
//   root       = align16(0x20 + nameTableLength + 1)          (§5.2, 1,812/1,812)
//   root+0x00  marker "71BW" = 0x57423137                      (§2, §5.3)
//   root+0x04  version, guard range 42..44                     (§5.3)
//   root+0x20  float: effect duration in seconds               (§6.2, CONFIRMED)
//   root+0x38  u32 the spec says equals (file size - root)     (§5.3). MEASURED:
//              true for 1,468 of 1,812 real files and false for the other
//              344, all of which have a non-zero count at root+0x40; those
//              files carry a trailing string table of texture names after
//              the declared end. Use endFieldMatchesFileSize() / endSlack()
//              to see which case a file is in; parse() does not enforce it.
//   root+0x28/0x30  count/ptr, stride 8     texture-name list  (§5.3)
//   root+0x40/0x48  count/ptr, stride 0x88  material-ref array (§5.3)
//   root+0x60/0x68  count/ptr, stride 0x58  named sub-objects  (§5.4)
//   root+0x70/0x78  count/ptr, stride 0xC8  (role OPEN)        (§6.2 item 6)
//   root+0x80/0x88  count/ptr, stride 0x70  VFX Filter records (§5.5)
//   root+0x90/0x98  count/ptr, stride 0x228 (role OPEN)        (§5.6)
//
// WHAT THIS READER DELIBERATELY DOES NOT DO:
//
//  * It does not interpret record+0x10 beyond surfacing the raw class id.
//    That the value is a registry class id is CONFIRMED (§6.6); the class
//    NAMES are unrecovered (OPEN), so none are invented here.
//  * It does not read the arrays at root+0x40 / +0x70 / +0x90 element by
//    element. The spec has no populated example of any of them (count 0 in
//    3/3 samples, §5.6/§6.2), so their element layouts are unvalidated -
//    only their extents (count * stride, both CONFIRMED in the fix-up code)
//    are used, and only to test that they fit inside the file.
//  * It never throws for a bounds/layout failure - see errors.h.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "sr3effects/errors.h"
#include "vpp/byte_view.h"

namespace sr3effects {

using vpp::ByteView;

constexpr uint32_t kMaterialBlockMagic = 0x00043854u; // spec §2: read as u16 0x3854 then u16 4
constexpr uint32_t kRootMarker = 0x57423137u;         // "71BW" as on-disk bytes 37 31 42 57
constexpr uint32_t kMinVersion = 42;                  // spec §5.3 guard: [0x2A, 0x2C]
constexpr uint32_t kMaxVersion = 44;
constexpr uint32_t kNullPointer = 0xFFFFFFFFu;        // spec §5.3

constexpr size_t kMaterialHeaderSize = 0x20;          // spec §5.2

// Root-relative field offsets (spec §5.3 / §6.2).
constexpr size_t kRootVersion = 0x04;
constexpr size_t kRootFlagCount = 0x08;   // i32; >0 gates the optional block at +0x10
constexpr size_t kRootDuration = 0x20;    // f32, seconds (CONFIRMED, §6.2 item 2)
constexpr size_t kRootSecondFloat = 0x24; // f32, meaning OPEN
constexpr size_t kRootTexListCount = 0x28;
constexpr size_t kRootEndField = 0x38;    // == file size - root (CONFIRMED, 3/3 in spec)
constexpr size_t kRootArray40Count = 0x40;
constexpr size_t kRootSubObjectCount = 0x60;
constexpr size_t kRootArray70Count = 0x70;
constexpr size_t kRootFilterCount = 0x80;
constexpr size_t kRootArray90Count = 0x90;
constexpr size_t kRootMinimumSize = 0xA0; // through the root+0x98 pointer of the last count/array pair

// Element strides (spec §5.3 table, §5.4, §5.5, §5.6, §6.2 item 6).
constexpr uint32_t kTextureListStride = 8;
constexpr uint32_t kArray40Stride = 0x88;
constexpr uint32_t kSubObjectStride = 0x58;
constexpr uint32_t kArray70Stride = 0xC8;
constexpr uint32_t kFilterStride = 0x70;
constexpr uint32_t kArray90Stride = 0x228;

// Sub-object parameter (P) and render-state (Q) block sizes (spec §6.2 item
// 1, §6.9, §6.11: P pitch 0x4A8, Q pitch 0xD8, both at align16 of the
// previous run's end).
constexpr uint32_t kParamBlockSize = 0x4A8;
constexpr uint32_t kStateBlockSize = 0xD8;

// The 41 pointer slots inside every P block (spec §6.2 item 1): +0x1E0,
// +0x1F0 and every 0x10 from +0x220 to +0x480 inclusive. The slot at
// +0x290 is only fixed up when the count at +0x288 is positive.
constexpr size_t kParamPointerSlotCount = 41;
constexpr size_t kParamSlot290 = 0x290;
constexpr size_t kParamCount288 = 0x288;
constexpr size_t kParamTailBegin = 0x484; // spec §6.11: P+0x484..0x4A7 is zero (13/13)

// One count/pointer pair on the root header. `pointer` is the RAW on-disk
// value (root-relative offset, or kNullPointer). Nothing is resolved.
struct ArrayRef {
    uint32_t count = 0;
    uint32_t pointer = kNullPointer;
    uint32_t stride = 0;

    bool isNull() const { return pointer == kNullPointer; }
    // One past the last byte of the array, root-relative. 64-bit so a
    // corrupt huge count cannot wrap.
    uint64_t extentEnd() const {
        return static_cast<uint64_t>(pointer) + static_cast<uint64_t>(count) * stride;
    }
};

// One record of the root+0x60 array (spec §5.4 / §6.5). Only fields whose
// meaning the spec marks CONFIRMED are surfaced; the four booleans are
// surfaced raw (their bit roles are OPEN).
struct SubObject {
    std::string name;       // empty when nameValid is false
    bool nameValid = false; // name pointer resolved to an in-file NUL-terminated string
    uint32_t classId = 0;   // record+0x10 - raw registry class id (§6.6); names unrecovered
    uint8_t flags[4] = {0, 0, 0, 0}; // record+0x14..0x17, raw
    uint32_t paramPointer = kNullPointer; // record+0x28 (-> P block), raw
    uint32_t statePointer = kNullPointer; // record+0x30 (-> Q block), raw
};

// Result of testing the spec's §6.11 "structure" bullet plus the P-block
// pointer/tail facts against one file. Every field is a COUNT so a
// population harness can report exactly which sub-check failed.
struct SubObjectLayoutCheck {
    bool applicable = false;         // count > 0 and the record array fits in the file
    size_t records = 0;
    size_t paramPointerMismatch = 0; // records whose +0x28 != align16(records end) + k*0x4A8
    size_t statePointerMismatch = 0; // records whose +0x30 != align16(P end) + k*0xD8
    size_t blockOutOfFile = 0;       // records whose P or Q block does not fit inside the file
    size_t slotsChecked = 0;         // P pointer slots examined
    size_t slotsNull = 0;            // slots holding 0xFFFFFFFF (tolerated, counted)
    size_t slotsBad = 0;             // slots neither null nor (in file, 16-aligned, past the Q blocks)
    size_t tailNonZero = 0;          // records whose P+0x484..0x4A7 has a non-zero byte

    bool ok() const {
        return applicable && paramPointerMismatch == 0 && statePointerMismatch == 0 &&
               blockOutOfFile == 0 && slotsBad == 0 && tailNonZero == 0;
    }
};

class EffectFile {
public:
    // Parses `bytes`. Throws FormatError only for the CONFIRMED hard rules:
    // material magic, the `71BW` marker at the predicted root, and a
    // version outside 42..44 (spec §5.2/§5.3). `bytes` must outlive the
    // returned object.
    static EffectFile parse(ByteView bytes);

    size_t rootOffset() const { return root_; }
    size_t fileSize() const { return bytes_.size(); }
    uint32_t version() const { return version_; }
    int32_t flagCount() const { return flagCount_; }
    float duration() const { return duration_; }      // root+0x20, seconds
    float secondFloat() const { return secondFloat_; } // root+0x24, meaning OPEN

    // Material-block fields (spec §5.2).
    uint32_t nameTableLength() const { return nameTableLength_; } // block+4
    uint32_t textureCount() const { return textureCount_; }        // block+0xC
    // The `textureCount` NUL-terminated texture names starting at block+0x20
    // (spec §2, CONFIRMED). namesParsed() is false if that walk left the
    // file or ran into root; textureNames() is then empty.
    //
    // NOT read here: the `.effectx` source-file name. Spec §5.2 says the
    // name-table length also spans it, but real bytes disagree (the length
    // equals exactly the sum of the texture names' (strlen+1) and the source
    // name sits in the root region - see stringAt() and the population
    // harness), so this reader does not look for it inside the block.
    bool namesParsed() const { return namesParsed_; }
    const std::vector<std::string>& textureNames() const { return textureNames_; }
    // Bytes the texture-name walk consumed (sum of strlen+1). Compare with
    // nameTableLength(): on every real file measured they are equal.
    size_t nameBytesConsumed() const { return nameBytesConsumed_; }

    // Reads the NUL-terminated string at root-relative offset `rootRel`.
    // Returns false (and leaves `out` empty) for the null sentinel, an
    // offset outside the file, or an unterminated string. This is the
    // generic pointer-resolution helper; it assigns no meaning to any field.
    bool stringAt(uint32_t rootRel, std::string& out) const;
    // Raw u32 at root+0x18 (the "pointer resolving to a small in-header
    // offset", spec §5.3, content OPEN there).
    uint32_t pointer18() const { return pointer18_; }

    // The u32 at root+0x38 and the identity the spec says it satisfies
    // (spec §5.3: == file size - root, 3/3). Population measurement shows
    // the identity is NOT universal: see endSlack().
    uint32_t endField() const { return endField_; }
    bool endFieldMatchesFileSize() const { return endField_ == bytes_.size() - root_; }
    // (file size - root) - endField, signed: 0 when the identity holds,
    // positive when bytes follow the declared end, negative when the
    // declared end lies beyond the file.
    int64_t endSlack() const {
        return static_cast<int64_t>(bytes_.size() - root_) - static_cast<int64_t>(endField_);
    }

    ArrayRef textureList() const { return textureList_; }
    ArrayRef array40() const { return array40_; }
    ArrayRef subObjects() const { return subObjects_; }
    ArrayRef array70() const { return array70_; }
    ArrayRef filters() const { return filters_; }
    ArrayRef array90() const { return array90_; }

    // True if the array's extent lies inside [root, end of file]: a null or
    // in-file pointer for a zero count, a non-null pointer with
    // pointer + count*stride <= file size - root for a positive count.
    bool arrayFitsInFile(const ArrayRef& a) const;

    // The strings the root+0x28/0x30 list points at ({string_ptr, 0} 8-byte
    // entries, spec §5.3). `allResolved` is false if the list does not fit
    // in the file or any entry's string pointer does not resolve to an
    // in-file NUL-terminated string (unresolved entries are omitted).
    std::vector<std::string> readTextureListNames(bool& allResolved) const;

    // The root+0x60 records. Empty unless subObjects() fits in the file.
    std::vector<SubObject> readSubObjects() const;

    // record+0x10 of every VFX Filter record (spec §6.10, CONFIRMED for 2/2
    // samples). Empty unless filters() fits in the file.
    std::vector<uint32_t> readFilterClassIds() const;

    // P+0x02 (u16 "emitter type", spec §6.7) of every sub-object whose P
    // block fits in the file; blocks that do not fit are skipped.
    std::vector<uint16_t> readEmitterTypes() const;

    // Tests the spec's §6.11 structure facts (see SubObjectLayoutCheck).
    SubObjectLayoutCheck checkSubObjectLayout() const;

private:
    ByteView bytes_;
    size_t root_ = 0;
    uint32_t version_ = 0;
    int32_t flagCount_ = 0;
    float duration_ = 0;
    float secondFloat_ = 0;
    uint32_t nameTableLength_ = 0;
    uint32_t textureCount_ = 0;
    bool namesParsed_ = false;
    std::vector<std::string> textureNames_;
    size_t nameBytesConsumed_ = 0;
    uint32_t pointer18_ = 0;
    uint32_t endField_ = 0;
    ArrayRef textureList_, array40_, subObjects_, array70_, filters_, array90_;
};

// Position of the `71BW` marker the material block predicts, without
// parsing anything else: align16(0x20 + nameTableLength + 1) (spec §5.2).
// Returns false if `bytes` is too small to hold the material header. Does
// NOT check that the marker is actually there.
bool predictRootOffset(ByteView bytes, size_t& rootOut);

} // namespace sr3effects
