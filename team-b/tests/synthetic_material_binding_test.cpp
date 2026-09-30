// Synthetic tests for `sr3geometry::MaterialBindings` (HANDOFF §9.69).
//
// Built directly from spec-vertex-format.md §8.4/§8.4.2/§12.8 and
// spec-vehicle-geometry.md §11.2, never from src/material_binding.cpp: a
// 0x424BD00D sub-header (materialCount at +0x0C, a second count N2 at
// +0x0E, a length-prefixed mixed-case name blob at +0x88 opening with a
// mandatory leading NUL), followed by two fixed-stride descriptor arrays
// (sized by N2, then by materialCount) and a per-material loop whose i-th
// iteration is material i's own self-declaring-length record: a u32 size
// field, an 8-aligned 0x30-byte header (texture-binding count at +0x0C),
// then that many 12-byte {name offset, param hash, slot} entries starting
// immediately after the header (already 8-aligned, since 0x30 is a
// multiple of 8).
//
// This reader replaced a sliding-window guess-and-check search that could
// never locate vehicles (their declared material count structurally
// exceeds the number of binding runs actually present). These fixtures
// exercise the arithmetic directly: the N2 skip vehicles need and
// characters don't, the per-entry validity guards from §8.4.2, and
// graceful handling of a file too short for what the sub-header promises.

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "sr3geometry/errors.h"
#include "sr3geometry/material_binding.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":"          \
                      << __LINE__ << "\n";                                   \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_THROWS(expr)                                                  \
    do {                                                                     \
        bool threw = false;                                                  \
        try { expr; }                                                        \
        catch (const sr3geometry::FormatError&) { threw = true; }            \
        if (!threw) {                                                        \
            std::cerr << "CHECK FAILED (expected throw): " #expr             \
                      << " at " __FILE__ ":" << __LINE__ << "\n";            \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

void putU16At(std::vector<uint8_t>& b, size_t off, uint16_t v) {
    b[off + 0] = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}
void putU32At(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    b[off + 0] = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    b[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    b[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}
void appendU32(std::vector<uint8_t>& b, uint32_t v) {
    b.resize(b.size() + 4);
    putU32At(b, b.size() - 4, v);
}
void appendU16(std::vector<uint8_t>& b, uint16_t v) {
    b.resize(b.size() + 2);
    putU16At(b, b.size() - 2, v);
}
void appendCString(std::vector<uint8_t>& b, const std::string& s) {
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0x00);
}
void padTo(std::vector<uint8_t>& b, size_t alignment) {
    while (b.size() % alignment != 0) b.push_back(0x00);
}

struct FixtureEntry {
    uint32_t nameIndex;
    uint32_t hash;
    uint32_t slot;
};

// One material's array-A entries. An empty vector means A_count == 0 - a
// material that legitimately has no texture bindings.
using FixtureMaterial = std::vector<FixtureEntry>;

struct Fixture {
    uint16_t n2 = 0;
    std::vector<std::string> names;
    std::vector<FixtureMaterial> materials;
};

// Builds a buffer `MaterialBindings::parse()` can walk directly, and hands
// back `meshBlockEndOut` - the cursor position right after the sub-header
// region, exactly what a caller would pass as `meshBlockEnd` (in real
// files, `geometry.meshSubBlockOffset() + mesh.cLength()`).
std::vector<uint8_t> buildFixture(const Fixture& fx, size_t& meshBlockEndOut,
                                  size_t& nameBlobLengthFieldOut) {
    std::vector<uint8_t> b;
    appendU32(b, 0x424BD00Du); // sub-header magic
    b.resize(0x0C, 0x00);
    appendU16(b, static_cast<uint16_t>(fx.materials.size())); // +0x0C materialCount
    appendU16(b, fx.n2);                                       // +0x0E N2
    b.resize(0x88, 0x00);

    const size_t blobAt = b.size(); // == 0x88
    appendU32(b, 0);                // blob length, patched below
    b.resize(blobAt + 9, 0x00);     // 5 reserved bytes, then the leading NUL
    const size_t blobBase = b.size();

    std::vector<size_t> nameOffsets;
    for (const auto& n : fx.names) {
        nameOffsets.push_back(b.size() - blobBase);
        appendCString(b, n);
    }
    putU32At(b, blobAt, static_cast<uint32_t>(b.size() - blobBase));
    nameBlobLengthFieldOut = blobAt;

    meshBlockEndOut = b.size();

    if (fx.n2 > 0) {
        padTo(b, 8);
        b.resize(b.size() + static_cast<size_t>(fx.n2) * 8 + static_cast<size_t>(fx.n2) * 4, 0x00);
    }
    padTo(b, 8);
    b.resize(b.size() + fx.materials.size() * 8, 0x00); // array_C

    for (const auto& mat : fx.materials) {
        const size_t sizeFieldAt = b.size();
        appendU32(b, 0); // patched below
        padTo(b, 8);
        const size_t headerStart = b.size();
        b.resize(headerStart + 0x30, 0x00);
        putU16At(b, headerStart + 0x0C, static_cast<uint16_t>(mat.size()));
        for (const auto& e : mat) {
            appendU32(b, static_cast<uint32_t>(nameOffsets.at(e.nameIndex)));
            appendU32(b, e.hash);
            appendU32(b, e.slot);
        }
        putU32At(b, sizeFieldAt, static_cast<uint32_t>(b.size() - sizeFieldAt));
    }
    return b;
}

// §12.8's formula, exercised with no array_A/array_B skip (N2 == 0, the
// character-mesh shape): a two-material fixture whose materials carry a
// known diffuse and a known normal-map hash, checked end to end.
void testBasicWalk() {
    Fixture fx;
    fx.n2 = 0;
    fx.names = {"tex1.tga", "tex2.tga", "tex3.tga"};
    fx.materials = {
        {{0, sr3geometry::kParamHashNormalMap, 0}},
        {{1, sr3geometry::kParamHashDiffuseB, 0}, {2, sr3geometry::kParamHashNormalMap, 1}},
    };
    size_t meshBlockEnd = 0, blobLenField = 0;
    std::vector<uint8_t> b = buildFixture(fx, meshBlockEnd, blobLenField);

    sr3geometry::MaterialBindings bindings = sr3geometry::MaterialBindings::parse(
        vpp::ByteView(b.data(), b.size()), 0, meshBlockEnd);

    CHECK(bindings.located());
    CHECK(bindings.materialCount() == 2);
    CHECK(bindings.materials().size() == 2);
    CHECK(bindings.materials()[0].textures.size() == 1);
    CHECK(bindings.materials()[0].textures[0].name == "tex1.tga");
    CHECK(bindings.materials()[0].textures[0].paramHash == sr3geometry::kParamHashNormalMap);
    CHECK(bindings.materials()[1].textures.size() == 2);
    const std::string* diffuse1 = bindings.diffuseFor(1);
    CHECK(diffuse1 != nullptr && *diffuse1 == "tex2.tga");
    const std::string* normal1 = bindings.materials()[1].normalMap();
    CHECK(normal1 != nullptr && *normal1 == "tex3.tga");
    CHECK(bindings.diffuseFor(0) == nullptr); // material 0 has no diffuse-hash entry
}

// §8.4: on a vehicle-shaped sub-header (N2 > 0), the fixed-stride array_A/
// array_B region between the sub-header and array_C must be skipped by
// BYTE COUNT (N2*8 + N2*4), not stepped over incidentally. Same materials
// as testBasicWalk, only N2 changes - if the skip were wrong, the walk
// would read array_A/array_B's zero-filled bytes as a material's own
// declared size and desynchronise immediately.
void testN2SkipVehicleShape() {
    Fixture fx;
    fx.n2 = 5; // vehicles' second count; character meshes always carry 0
    fx.names = {"tex1.tga", "tex2.tga", "tex3.tga"};
    fx.materials = {
        {{0, sr3geometry::kParamHashNormalMap, 0}},
        {{1, sr3geometry::kParamHashDiffuseB, 0}, {2, sr3geometry::kParamHashNormalMap, 1}},
    };
    size_t meshBlockEnd = 0, blobLenField = 0;
    std::vector<uint8_t> b = buildFixture(fx, meshBlockEnd, blobLenField);

    sr3geometry::MaterialBindings bindings = sr3geometry::MaterialBindings::parse(
        vpp::ByteView(b.data(), b.size()), 0, meshBlockEnd);

    CHECK(bindings.located());
    CHECK(bindings.materialCount() == 2);
    CHECK(bindings.materials()[0].textures.size() == 1);
    CHECK(bindings.materials()[0].textures[0].name == "tex1.tga");
    CHECK(bindings.materials()[1].textures.size() == 2);
    const std::string* diffuse1 = bindings.diffuseFor(1);
    CHECK(diffuse1 != nullptr && *diffuse1 == "tex2.tga");
}

// spec §8.4.3: vehicles legitimately declare materials their draw ranges
// never reference. A record whose own header declares A_count == 0 must
// resolve to "no textures", not be treated as a failure to locate.
void testZeroBindingMaterialIsNotAFailure() {
    Fixture fx;
    fx.n2 = 0;
    fx.names = {"tex1.tga"};
    fx.materials = {{}, {{0, sr3geometry::kParamHashNormalMap, 0}}}; // material 0: no bindings
    size_t meshBlockEnd = 0, blobLenField = 0;
    std::vector<uint8_t> b = buildFixture(fx, meshBlockEnd, blobLenField);

    sr3geometry::MaterialBindings bindings = sr3geometry::MaterialBindings::parse(
        vpp::ByteView(b.data(), b.size()), 0, meshBlockEnd);

    CHECK(bindings.located());
    CHECK(bindings.materials()[0].textures.empty());
    CHECK(bindings.diffuseFor(0) == nullptr);
    CHECK(bindings.materials()[1].textures.size() == 1);
}

// spec §8.4.2 guard 2: a param hash under 256 or 0xFFFFFFFF is not a real
// 32-bit hash. That one entry is dropped; it does not take the rest of the
// material's array, or the file, down with it - the position was reached
// by arithmetic, not by a search whose validity depended on every entry
// agreeing.
void testBadHashDropsOnlyThatEntry() {
    Fixture fx;
    fx.n2 = 0;
    fx.names = {"good.tga", "bad.tga"};
    fx.materials = {{{0, sr3geometry::kParamHashNormalMap, 0}, {1, 5u /* too small */, 1}}};
    size_t meshBlockEnd = 0, blobLenField = 0;
    std::vector<uint8_t> b = buildFixture(fx, meshBlockEnd, blobLenField);

    sr3geometry::MaterialBindings bindings = sr3geometry::MaterialBindings::parse(
        vpp::ByteView(b.data(), b.size()), 0, meshBlockEnd);

    CHECK(bindings.located());
    CHECK(bindings.materials()[0].textures.size() == 1);
    CHECK(bindings.materials()[0].textures[0].name == "good.tga");
}

// spec §8.4.2 guard 1: a name offset landing mid-string (not on the blob
// base or immediately after a NUL) must be rejected even though the bytes
// there still look like a plausible, dotted, printable name - the failure
// mode a naive "printable and has a dot" check misses.
void testMidStringNameOffsetRejected() {
    Fixture fx;
    fx.n2 = 0;
    fx.names = {"prefix_tex.tga"};
    // Entry 0's nameIndex points at the real name start (valid, kept).
    // Entry 1 reuses the same string but +1 byte in - lands one past the
    // boundary, inside "refix_tex.tga", which still passes a bare
    // printable/dot check and must be caught by the boundary rule instead.
    fx.materials = {{{0, sr3geometry::kParamHashNormalMap, 0}}};
    size_t meshBlockEnd = 0, blobLenField = 0;
    std::vector<uint8_t> b = buildFixture(fx, meshBlockEnd, blobLenField);

    // Hand-corrupt the just-built entry's name offset to be one byte off
    // the real boundary (the fixture builder always lands on-boundary, so
    // this mutation is applied directly to the encoded bytes afterward).
    // The single material's single entry starts right after the 0x30-byte
    // record header, itself right after the 4-byte size field, 8-aligned.
    // Locate it the same way the reader does rather than hardcoding an
    // offset, so this test stays valid if the fixture layout above changes.
    sr3geometry::MaterialBindings before = sr3geometry::MaterialBindings::parse(
        vpp::ByteView(b.data(), b.size()), 0, meshBlockEnd);
    CHECK(before.located());
    CHECK(before.materials()[0].textures.size() == 1); // sanity: the un-mutated fixture is valid

    // The entry's name-offset field is the first u32 of the 12-byte entry
    // immediately after the record header; find it by scanning for the
    // known hash that follows it, so this does not depend on restating the
    // header arithmetic a second time.
    size_t entryAt = std::string::npos;
    for (size_t i = 4; i + 8 <= b.size(); ++i) {
        uint32_t hash;
        std::memcpy(&hash, b.data() + i + 4, 4);
        if (hash == sr3geometry::kParamHashNormalMap) { entryAt = i; break; }
    }
    CHECK(entryAt != std::string::npos);
    uint32_t nameOffset;
    std::memcpy(&nameOffset, b.data() + entryAt, 4);
    putU32At(b, entryAt, nameOffset + 1); // now mid-string, not on a boundary

    sr3geometry::MaterialBindings after = sr3geometry::MaterialBindings::parse(
        vpp::ByteView(b.data(), b.size()), 0, meshBlockEnd);
    CHECK(after.located());               // the RECORD is still located...
    CHECK(after.materials()[0].textures.empty()); // ...but the bad entry is dropped, not guessed
}

// A file too short for what the sub-header promises (declared material
// count implies a record this buffer does not actually contain) must
// report "not located" rather than throwing or reading past the end -
// ByteView's own accessors would throw std::out_of_range on that, but the
// reader is expected to check bounds itself and fail closed instead.
void testTruncatedFileNotLocated() {
    Fixture fx;
    fx.n2 = 0;
    fx.names = {"tex1.tga"};
    fx.materials = {{{0, sr3geometry::kParamHashNormalMap, 0}}};
    size_t meshBlockEnd = 0, blobLenField = 0;
    std::vector<uint8_t> full = buildFixture(fx, meshBlockEnd, blobLenField);

    // Cut the buffer off partway through the one material's own record
    // header - short enough that headerStart + 0x30 overruns, long enough
    // that the size field itself is still readable.
    std::vector<uint8_t> truncated(full.begin(), full.begin() + static_cast<long>(meshBlockEnd) + 12);

    sr3geometry::MaterialBindings bindings = sr3geometry::MaterialBindings::parse(
        vpp::ByteView(truncated.data(), truncated.size()), 0, meshBlockEnd);
    CHECK(!bindings.located());
    CHECK(bindings.materialCount() == 1); // the sub-header itself was intact and readable
    CHECK(bindings.materials().empty());
}

// materialCount() == 0 is a legitimate declared state (no materials at
// all), not a truncation - it must resolve immediately to "not located"
// with no further reads attempted.
void testZeroMaterialCount() {
    Fixture fx;
    fx.n2 = 0;
    fx.materials = {};
    size_t meshBlockEnd = 0, blobLenField = 0;
    std::vector<uint8_t> b = buildFixture(fx, meshBlockEnd, blobLenField);

    sr3geometry::MaterialBindings bindings = sr3geometry::MaterialBindings::parse(
        vpp::ByteView(b.data(), b.size()), 0, meshBlockEnd);
    CHECK(!bindings.located());
    CHECK(bindings.materialCount() == 0);
    CHECK(bindings.materials().empty());
}

// Unchanged regression: the wrong sub-header offset (no 0x424BD00D magic)
// must still throw rather than silently walking garbage.
void testWrongMagicThrows() {
    Fixture fx;
    fx.n2 = 0;
    fx.materials = {{{0, sr3geometry::kParamHashNormalMap, 0}}};
    fx.names = {"tex1.tga"};
    size_t meshBlockEnd = 0, blobLenField = 0;
    std::vector<uint8_t> b = buildFixture(fx, meshBlockEnd, blobLenField);
    putU32At(b, 0, 0xDEADBEEFu); // corrupt the magic

    CHECK_THROWS(sr3geometry::MaterialBindings::parse(vpp::ByteView(b.data(), b.size()), 0,
                                                       meshBlockEnd));
}

} // namespace

int main() {
    testBasicWalk();
    testN2SkipVehicleShape();
    testZeroBindingMaterialIsNotAFailure();
    testBadHashDropsOnlyThatEntry();
    testMidStringNameOffsetRejected();
    testTruncatedFileNotLocated();
    testZeroMaterialCount();
    testWrongMagicThrows();

    if (g_failures == 0) {
        std::cout << "All synthetic material-binding tests passed.\n";
        return 0;
    }
    std::cerr << g_failures << " check(s) failed.\n";
    return 1;
}
