#include "sr3geometry/material_binding.h"

#include <algorithm>

namespace sr3geometry {
namespace {

constexpr uint32_t kSubheaderMagic = 0x424BD00Du;
constexpr size_t kMaterialCountOffset = 0x0C;  // N  (u16, spec-vertex-format.md §8.3/8.4)
constexpr size_t kMaterial2CountOffset = 0x0E; // N2 (u16, §8.4 - a second, earlier count)
constexpr size_t kNameBlobOffset = 0x88;
// The blob opens with a u32 total length followed by eight bytes before the
// first name begins; entry offsets are relative to that first name's start.
// Measured, not assumed: on `brad`, entry offset 22 lands exactly on the
// second name, which only holds with this base.
constexpr size_t kNameBlobFirstName = 9;
constexpr size_t kEntrySize = 12;

// The per-material record's own internal header (spec-vertex-format.md
// §12.8, spec-vehicle-geometry.md §11.2): 0x30 bytes, texture-binding count
// at +0x0C, array A (the binding run) starting immediately after at +0x30 -
// already 8-aligned since the header itself starts 8-aligned and 0x30 is a
// multiple of 8.
constexpr size_t kRecordHeaderSize = 0x30;
constexpr size_t kRecordACountOffset = 0x0C;

size_t alignUp(size_t value, size_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

// Reads a NUL-terminated name and REFUSES anything implausible. A wrong
// offset must fail here rather than return noise that later looks like a
// texture name.
bool resolveName(ByteView content, size_t base, uint32_t offset, size_t blobEnd,
                 std::string& out) {
    size_t at = base + offset;
    if (at >= blobEnd || at >= content.size()) return false;

    // A real name offset lands on a name START: the blob base, or one past
    // a NUL. Without this, an offset landing MID-string still resolves -
    // "_df.tga" cut out of "veh_df.tga" is printable and contains a dot, so
    // every other test here passes it. Measured (§9.34,
    // tools/validation/validate_vehicle_runs.cpp): characters satisfy this
    // 6,064/6,064, so it rejects nothing this reader currently accepts;
    // vehicles show a real 0.56% truncation population, which is what the
    // check exists to refuse.
    if (at != base && !(at > 0 && content.at(at - 1) == 0)) return false;
    std::string s;
    while (at < blobEnd && at < content.size()) {
        uint8_t c = content.at(at);
        if (c == 0) break;
        if (c < 32 || c >= 127) return false;
        s.push_back(static_cast<char>(c));
        if (s.size() > 128) return false;
        ++at;
    }
    if (s.size() < 3) return false;
    if (s.find('.') == std::string::npos) return false;
    out = s;
    return true;
}

} // namespace

MaterialBindings MaterialBindings::parse(ByteView content, size_t subheaderOffset,
                                         size_t meshBlockEnd) {
    MaterialBindings out;

    if (subheaderOffset + kNameBlobOffset + 4 > content.size()) {
        throw FormatError("material binding: sub-header offset " +
                          std::to_string(subheaderOffset) + " runs past the end of the file");
    }
    if (content.readU32LE(subheaderOffset) != kSubheaderMagic) {
        throw FormatError("material binding: no 0x424BD00D magic at the given sub-header "
                          "offset - the caller passed the wrong base");
    }

    out.materialCount_ = content.readU16LE(subheaderOffset + kMaterialCountOffset);
    if (out.materialCount_ == 0) return out;

    const size_t blobAt = subheaderOffset + kNameBlobOffset;
    const uint32_t blobLength = content.readU32LE(blobAt);
    const size_t blobBase = blobAt + kNameBlobFirstName;
    const size_t blobEnd =
        std::min(content.size(), blobAt + static_cast<size_t>(blobLength) + kNameBlobFirstName);
    if (blobBase >= blobEnd) return out;
    if (meshBlockEnd >= content.size()) return out;

    // Walk directly to each material's own record (spec-vertex-format.md
    // §12.8, chaining §8.4's fixed-stride descriptor arrays): `meshBlockEnd`
    // is already the cursor position §12.8 calls `mo + c_len`. Skip the two
    // fixed-stride arrays sized by N2 (+0x0E) and the array sized by N
    // (materialCount_, +0x0C), and the per-material loop's i-th iteration
    // IS material i's own record - no search needed, because each record's
    // position follows from the one before it rather than from a guess.
    //
    // This replaces a prior sliding-window guess-and-check scan that could
    // never locate vehicles: vehicles declare roughly twice the materials
    // their draw ranges reference (spec-vertex-format.md §8.4.3), so an
    // equality guard on "runs found == declared material count" structurally
    // could not hold there (HANDOFF §9.34, spec-vehicle-geometry.md §7.1
    // item 1 / §11.2). Direct arithmetic needs no such count to accept a
    // position. Verified exact, whole population, both carriers, zero
    // exceptions before being applied here (spec-vertex-format.md §12.8:
    // 2,002/2,002 character files, 365/365 vehicle files, every material
    // index on each).
    const uint16_t n2 = content.readU16LE(subheaderOffset + kMaterial2CountOffset);
    size_t cursor = meshBlockEnd;
    if (n2 > 0) {
        cursor = alignUp(cursor, 8);
        cursor += static_cast<size_t>(n2) * 8 + static_cast<size_t>(n2) * 4; // array_A, array_B
    }
    cursor = alignUp(cursor, 8);
    cursor += static_cast<size_t>(out.materialCount_) * 8; // array_C

    std::vector<MaterialBinding> materials;
    materials.reserve(out.materialCount_);
    for (uint16_t i = 0; i < out.materialCount_; ++i) {
        // A file too short to hold the next declared record is truncated
        // relative to what the sub-header promises - not a format error (the
        // caller passed a real sub-header), so this reports "not located"
        // rather than throwing.
        if (cursor + 4 > content.size()) return out;
        const uint32_t declaredSize = content.readU32LE(cursor);
        const size_t headerStart = alignUp(cursor + 4, 8);
        if (headerStart + kRecordHeaderSize > content.size()) return out;

        const uint16_t entryCount = content.readU16LE(headerStart + kRecordACountOffset);
        const size_t runStart = headerStart + kRecordHeaderSize;

        MaterialBinding binding;
        if (runStart + static_cast<size_t>(entryCount) * kEntrySize <= content.size()) {
            for (uint16_t e = 0; e < entryCount; ++e) {
                const size_t entryAt = runStart + static_cast<size_t>(e) * kEntrySize;
                const uint32_t nameOffset = content.readU32LE(entryAt + 0x00);
                const uint32_t paramHash = content.readU32LE(entryAt + 0x04);
                // GUARD (spec-vertex-format.md §8.4.2): a value under 256 or
                // -1 is not a real 32-bit hash - the cheapest available
                // rejection of a truncated/mis-walked entry, and the one
                // that caught live wrong bindings a weaker (!= 0) check let
                // through.
                if (paramHash < 256 || paramHash == 0xFFFFFFFFu) continue;

                TextureRef ref;
                // resolveName() already enforces §8.4.2's other requirement:
                // the offset must land on a name boundary, not mid-string.
                if (!resolveName(content, blobBase, nameOffset, blobEnd, ref.name)) continue;
                ref.slot = content.readU32LE(entryAt + 0x08);
                ref.paramHash = paramHash;
                binding.textures.push_back(std::move(ref));
            }
        }
        // An out-of-bounds or all-rejected array A leaves `binding` with no
        // textures - a legitimate "this material has none", the same result
        // a record that genuinely declares A_count == 0 produces. Not an
        // invitation to guess; diffuse()/normalMap() already return null for
        // it, which is the correct behaviour for both causes.
        materials.push_back(std::move(binding));

        cursor += declaredSize; // measured from the size field's own position (§11.2)
    }

    out.materials_ = std::move(materials);
    out.located_ = true;
    return out;
}

} // namespace sr3geometry
