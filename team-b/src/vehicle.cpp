#include "sr3vehicle/vehicle.h"

#include <cstring>

namespace sr3vehicle {
namespace {

float readF32LE(ByteView content, size_t at) {
    uint32_t bits = content.readU32LE(at);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// Reads a NUL-terminated name at a file-relative offset, refusing anything
// that is not printable so a wrong pointer fails loudly rather than
// returning noise that later looks like a part name.
bool readName(ByteView content, uint32_t offset, std::string& out) {
    if (offset == kAbsent || offset >= content.size()) return false;
    size_t at = offset;
    std::string s;
    while (at < content.size()) {
        uint8_t c = content.at(at);
        if (c == 0) break;
        if (c < 32 || c >= 127) return false;
        s.push_back(static_cast<char>(c));
        if (s.size() > 128) return false;
        ++at;
    }
    if (s.empty()) return false;
    out = s;
    return true;
}

} // namespace

const char* partTypeName(uint32_t type) {
    switch (type) {
        case 1:  return "fixed body piece";
        case 2:  return "front wheel";
        case 3:  return "rear wheel";
        case 4:  return "hinged panel";
        case 6:  return "additional front axle";
        case 8:  return "door window";
        case 14: return "aircraft wing";
        case 17: return "suspension";
        case 18: return "front wheel (set 2)";
        case 22: return "steering";
        case 23: return "rear wheel (set 2)";
        case 24: return "rear wheel (set 3)";
        case 25: return "rotor / blade";
        // Types 5, 7, 9-13, 15, 16, 19-21, 26 occur at most 92 times each
        // and the spec leaves them OPEN. Returning nullptr keeps that
        // uncertainty visible instead of inventing a label.
        default: return nullptr;
    }
}

bool Vehicle::hasEngineSpecialCase(const std::string& vehicleName) {
    return specialCaseNote(vehicleName) != nullptr;
}

const char* Vehicle::specialCaseNote(const std::string& vehicleName) {
    if (vehicleName == "truck_2dr_garbage01")
        return "engine sets flag 0x8000 on 'Hatch_Rear'";
    if (vehicleName == "sp_backhoe01")
        return "engine sets flag 0x8000 on every hinged part except 'frontRarm'/'frontRarmB'";
    if (vehicleName == "truck_2dr_tow01")
        return "engine sets flag 0x8000 on 'tow_cable_swing'";
    if (vehicleName == "car_2dr_muscle04")
        return "engine sets flag 0x8000 on 'trunk'";
    return nullptr;
}

bool readGcarCrossReference(ByteView gcar, uint32_t& outValue) {
    if (gcar.size() < 16) return false;
    outValue = gcar.readU32LE(0);
    return true;
}

Vehicle Vehicle::parse(ByteView content) {
    Vehicle out;

    if (content.size() < kPartArrayOffset) {
        throw FormatError("content too small to contain the .ccar_pc header and part array "
                          "(needs at least 0x3A0 bytes)");
    }
    const uint32_t magic = content.readU32LE(0x00);
    if (magic != kHeaderMagic) {
        throw FormatError("header +0x00 reads " + std::to_string(magic) + ", not 0x38 - the "
                          "loader requires 0x38 and it holds in 393/393 shipped vehicles");
    }

    out.meshRegionOffset_ = content.readU32LE(kMeshRegionOffset);
    out.morphOffset_ = content.readU32LE(kMorphOffset);
    out.gcarMorphOffset_ = content.readU32LE(kGcarMorphOffset);

    if (out.meshRegionOffset_ >= content.size()) {
        throw FormatError("embedded mesh offset " + std::to_string(out.meshRegionOffset_) +
                          " runs past the end of the file");
    }
    // The morph offset is either absent or a real offset into THIS file.
    // The +0x0C companion is deliberately NOT range-checked here: it indexes
    // the paired .gcar_pc and exceeds this file's size in all 326 cases.
    if (out.morphOffset_ != kAbsent && out.morphOffset_ >= content.size()) {
        throw FormatError("embedded morph offset " + std::to_string(out.morphOffset_) +
                          " runs past the end of the file");
    }

    readName(content, content.readU32LE(kAnchorNameOffset), out.anchorName_);

    const uint32_t partCount = content.readU32LE(kPartCountOffset);
    if (partCount == 0) {
        throw FormatError("part count is 0 - the spec observes 2..43 in 393/393");
    }
    const size_t partBytes = static_cast<size_t>(partCount) * kPartRecordSize;
    if (kPartArrayOffset + partBytes > content.size()) {
        throw FormatError("part array (" + std::to_string(partCount) + " x 0xE0 bytes) runs "
                          "past the end of the file");
    }

    out.parts_.reserve(partCount);
    for (uint32_t i = 0; i < partCount; ++i) {
        const size_t at = kPartArrayOffset + static_cast<size_t>(i) * kPartRecordSize;
        VehiclePart part;

        for (size_t e = 0; e < 16; ++e) {
            part.transform[e] = readF32LE(content, at + e * 4);
        }

        const uint32_t nameOffset = content.readU32LE(at + 0x40);
        if (!readName(content, nameOffset, part.name)) {
            throw FormatError("part " + std::to_string(i) + "'s name pointer " +
                              std::to_string(nameOffset) + " does not resolve - the spec has "
                              "these resolving 9,951/9,951, so a miss means the layout is wrong");
        }

        part.partType = content.readU32LE(at + 0x44);
        part.flags = content.readU32LE(at + 0x48);
        part.parentIndex = content.readU32LE(at + 0x4C);
        if (part.parentIndex != kNoParent && part.parentIndex >= partCount) {
            throw FormatError("part " + std::to_string(i) + " has parent index " +
                              std::to_string(part.parentIndex) + ", which is neither -1 nor "
                              "less than the part count " + std::to_string(partCount));
        }
        part.excludedFromBounds = (content.at(at + 0x68) & 0x80u) != 0;

        out.parts_.push_back(std::move(part));
    }

    return out;
}

} // namespace sr3vehicle
