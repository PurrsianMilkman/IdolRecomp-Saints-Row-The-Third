#include "sr3effects/effects.h"

#include <cstring>

namespace sr3effects {

namespace {

uint64_t align16_64(uint64_t v) { return (v + 15) / 16 * 16; }

float readF32(ByteView b, size_t off) {
    uint32_t u = b.readU32LE(off);
    float f;
    std::memcpy(&f, &u, sizeof f);
    return f;
}

ArrayRef readArray(ByteView b, size_t root, size_t countOff, uint32_t stride) {
    ArrayRef a;
    a.count = b.readU32LE(root + countOff);
    a.pointer = b.readU32LE(root + countOff + 8); // spec §5.3: pointer is 8 bytes after its count
    a.stride = stride;
    return a;
}

// Reads a NUL-terminated string starting at `off`, refusing to look at or
// past `limit`. Returns false if no terminator is found before `limit`.
bool readStringBounded(ByteView b, size_t off, size_t limit, std::string& out, size_t& consumed) {
    if (limit > b.size()) limit = b.size();
    for (size_t i = off; i < limit; ++i) {
        if (b.data()[i] == 0) {
            out.assign(reinterpret_cast<const char*>(b.data()) + off, i - off);
            consumed = i - off + 1;
            return true;
        }
    }
    return false;
}

// The 41 P-block pointer slot offsets (spec §6.2 item 1).
void paramPointerSlots(size_t (&out)[kParamPointerSlotCount]) {
    size_t n = 0;
    out[n++] = 0x1E0;
    out[n++] = 0x1F0;
    for (size_t s = 0x220; s <= 0x480; s += 0x10) out[n++] = s;
}

} // namespace

bool predictRootOffset(ByteView bytes, size_t& rootOut) {
    if (bytes.size() < kMaterialHeaderSize) return false;
    uint32_t len = bytes.readU32LE(4);
    // 64-bit so a corrupt length cannot wrap.
    uint64_t r = align16_64(static_cast<uint64_t>(kMaterialHeaderSize) + len + 1);
    if (r > bytes.size()) return false;
    rootOut = static_cast<size_t>(r);
    return true;
}

EffectFile EffectFile::parse(ByteView bytes) {
    if (bytes.size() < kMaterialHeaderSize) {
        throw FormatError("content too small for the shared material block header (spec §5.2)");
    }
    if (bytes.readU32LE(0) != kMaterialBlockMagic) {
        throw FormatError("bad material-block magic: expected 0x00043854 (spec §2, §5.2)");
    }

    EffectFile f;
    f.bytes_ = bytes;
    f.nameTableLength_ = bytes.readU32LE(4);
    f.textureCount_ = bytes.readU32LE(0xC);

    size_t root = 0;
    if (!predictRootOffset(bytes, root) || root + 4 > bytes.size()) {
        throw FormatError(
            "material block's name-table length puts the 71BW marker outside the file (spec §5.2)");
    }
    if (bytes.readU32LE(root) != kRootMarker) {
        throw FormatError(
            "no 71BW marker at align16(0x20 + name-table length + 1) (spec §5.2, CONFIRMED 3/3)");
    }
    if (root + kRootMinimumSize > bytes.size()) {
        throw FormatError("file ends inside the 71BW root header (spec §5.3)");
    }
    f.root_ = root;

    f.version_ = bytes.readU32LE(root + kRootVersion);
    if (f.version_ < kMinVersion || f.version_ > kMaxVersion) {
        throw FormatError("version outside the loader's accepted range 42..44 (spec §5.3)");
    }
    f.flagCount_ = static_cast<int32_t>(bytes.readU32LE(root + kRootFlagCount));
    f.duration_ = readF32(bytes, root + kRootDuration);
    f.secondFloat_ = readF32(bytes, root + kRootSecondFloat);
    f.endField_ = bytes.readU32LE(root + kRootEndField);

    f.textureList_ = readArray(bytes, root, kRootTexListCount, kTextureListStride);
    f.array40_ = readArray(bytes, root, kRootArray40Count, kArray40Stride);
    f.subObjects_ = readArray(bytes, root, kRootSubObjectCount, kSubObjectStride);
    f.array70_ = readArray(bytes, root, kRootArray70Count, kArray70Stride);
    f.filters_ = readArray(bytes, root, kRootFilterCount, kFilterStride);
    f.array90_ = readArray(bytes, root, kRootArray90Count, kArray90Stride);

    f.pointer18_ = bytes.readU32LE(root + 0x18);

    // Texture names: `textureCount` NUL-terminated strings from block+0x20,
    // strictly before root (spec §2).
    {
        size_t pos = kMaterialHeaderSize;
        bool ok = true;
        std::vector<std::string> names;
        for (uint32_t i = 0; i < f.textureCount_; ++i) {
            std::string s;
            size_t used = 0;
            if (!readStringBounded(bytes, pos, root, s, used)) {
                ok = false;
                break;
            }
            names.push_back(std::move(s));
            pos += used;
        }
        if (ok) {
            f.namesParsed_ = true;
            f.textureNames_ = std::move(names);
            f.nameBytesConsumed_ = pos - kMaterialHeaderSize;
        }
    }
    return f;
}

bool EffectFile::stringAt(uint32_t rootRel, std::string& out) const {
    out.clear();
    if (rootRel == kNullPointer) return false;
    const uint64_t abs = static_cast<uint64_t>(root_) + rootRel;
    if (abs >= bytes_.size()) return false;
    size_t used = 0;
    return readStringBounded(bytes_, static_cast<size_t>(abs), bytes_.size(), out, used);
}

bool EffectFile::arrayFitsInFile(const ArrayRef& a) const {
    const uint64_t endRel = bytes_.size() - root_;
    if (a.count == 0) return a.isNull() || a.pointer <= endRel;
    if (a.isNull()) return false;
    return a.extentEnd() <= endRel;
}

std::vector<std::string> EffectFile::readTextureListNames(bool& allResolved) const {
    std::vector<std::string> out;
    allResolved = false;
    if (!arrayFitsInFile(textureList_)) return out;
    allResolved = true;
    for (uint32_t k = 0; k < textureList_.count; ++k) {
        const size_t base = root_ + textureList_.pointer + static_cast<size_t>(k) * kTextureListStride;
        std::string s;
        if (stringAt(bytes_.readU32LE(base), s)) out.push_back(std::move(s));
        else allResolved = false;
    }
    return out;
}

std::vector<SubObject> EffectFile::readSubObjects() const {
    std::vector<SubObject> out;
    if (!arrayFitsInFile(subObjects_) || subObjects_.count == 0) return out;
    out.reserve(subObjects_.count);
    for (uint32_t k = 0; k < subObjects_.count; ++k) {
        const size_t base = root_ + subObjects_.pointer + static_cast<size_t>(k) * kSubObjectStride;
        SubObject r;
        uint32_t namePtr = bytes_.readU32LE(base + 0x00);
        if (namePtr != kNullPointer && root_ + static_cast<uint64_t>(namePtr) < bytes_.size()) {
            size_t used = 0;
            r.nameValid = readStringBounded(bytes_, root_ + namePtr, bytes_.size(), r.name, used);
        }
        r.classId = bytes_.readU32LE(base + 0x10);
        for (int i = 0; i < 4; ++i) r.flags[i] = bytes_.at(base + 0x14 + i);
        r.paramPointer = bytes_.readU32LE(base + 0x28);
        r.statePointer = bytes_.readU32LE(base + 0x30);
        out.push_back(std::move(r));
    }
    return out;
}

std::vector<uint32_t> EffectFile::readFilterClassIds() const {
    std::vector<uint32_t> out;
    if (!arrayFitsInFile(filters_) || filters_.count == 0) return out;
    for (uint32_t k = 0; k < filters_.count; ++k) {
        const size_t base = root_ + filters_.pointer + static_cast<size_t>(k) * kFilterStride;
        out.push_back(bytes_.readU32LE(base + 0x10));
    }
    return out;
}

std::vector<uint16_t> EffectFile::readEmitterTypes() const {
    std::vector<uint16_t> out;
    for (const SubObject& r : readSubObjects()) {
        if (r.paramPointer == kNullPointer) continue;
        if (static_cast<uint64_t>(r.paramPointer) + kParamBlockSize > bytes_.size() - root_) continue;
        out.push_back(bytes_.readU16LE(root_ + r.paramPointer + 0x02));
    }
    return out;
}

SubObjectLayoutCheck EffectFile::checkSubObjectLayout() const {
    SubObjectLayoutCheck c;
    if (subObjects_.count == 0 || !arrayFitsInFile(subObjects_)) return c;
    c.applicable = true;
    c.records = subObjects_.count;

    const uint64_t endRel = bytes_.size() - root_;
    const uint64_t n = subObjects_.count;
    const uint64_t p0 = align16_64(subObjects_.extentEnd());
    const uint64_t q0 = align16_64(p0 + n * kParamBlockSize);
    const uint64_t qEnd = q0 + n * kStateBlockSize;

    size_t slots[kParamPointerSlotCount];
    paramPointerSlots(slots);

    for (uint32_t k = 0; k < subObjects_.count; ++k) {
        const size_t base = root_ + subObjects_.pointer + static_cast<size_t>(k) * kSubObjectStride;
        const uint32_t p = bytes_.readU32LE(base + 0x28);
        const uint32_t q = bytes_.readU32LE(base + 0x30);
        if (p != p0 + static_cast<uint64_t>(k) * kParamBlockSize) ++c.paramPointerMismatch;
        if (q != q0 + static_cast<uint64_t>(k) * kStateBlockSize) ++c.statePointerMismatch;
        if (p == kNullPointer || q == kNullPointer ||
            static_cast<uint64_t>(p) + kParamBlockSize > endRel ||
            static_cast<uint64_t>(q) + kStateBlockSize > endRel) {
            ++c.blockOutOfFile;
            continue;
        }
        const size_t pAbs = root_ + p;
        const uint32_t count288 = bytes_.readU32LE(pAbs + kParamCount288);
        for (size_t s : slots) {
            if (s == kParamSlot290 && count288 == 0) continue; // fixed up only when count > 0 (§6.2 item 1)
            ++c.slotsChecked;
            const uint32_t v = bytes_.readU32LE(pAbs + s);
            if (v == kNullPointer) {
                ++c.slotsNull;
            } else if (v >= endRel || (v % 16) != 0 || v < qEnd) {
                ++c.slotsBad;
            }
        }
        bool tailNz = false;
        for (size_t i = kParamTailBegin; i < kParamBlockSize; ++i) {
            if (bytes_.at(pAbs + i) != 0) {
                tailNz = true;
                break;
            }
        }
        if (tailNz) ++c.tailNonZero;
    }
    return c;
}

} // namespace sr3effects
