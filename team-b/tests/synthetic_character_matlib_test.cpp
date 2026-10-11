// Synthetic tests for sr3geometry::ResolveCharacterMatlibAttachment() /
// CharacterMatlibAttachment (spec-tables-customization.md §12.1b - the real
// per-identity matlib RESOLUTION/ATTACHMENT mechanism, traced end to end
// from the executable: 0x00da8790 filename-compose -> 0x00da90d0
// exists-check -> 0x006f6c20/0x006f7330 attach-as-a-typed-(0x16)-slot
// keyed by the identity's own Name).
//
// These fixtures build synthetic MaterialBlock-shaped buffers from scratch
// (header + name table, per material_block.h) rather than touching any real
// archive - this file tests the RESOLUTION/ATTACHMENT model only, not the
// render-time consumption step, which the spec itself leaves OPEN (not
// modelled, not tested, not invented here).

#include <iostream>
#include <string>
#include <vector>

#include "sr3geometry/character_matlib.h"

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

void appendU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}
void appendCString(std::vector<uint8_t>& b, const std::string& s) {
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0x00);
}

// Builds a well-formed MaterialBlock buffer (magic, name-table length,
// +0x08 reserved, textureSlotCount, padding to 0x20, then the NUL-separated
// name table) - exactly the shape material_block.h documents and
// synthetic_ccmesh_test.cpp's own fixture builder uses for its material-
// block prefix.
std::vector<uint8_t> buildMatlib(const std::vector<std::string>& names) {
    std::vector<uint8_t> b;
    appendU32(b, sr3geometry::kMaterialBlockMagic);
    appendU32(b, 0); // name-table length, patched below
    appendU32(b, 0); // +0x08 reserved
    appendU32(b, static_cast<uint32_t>(names.size()));
    b.insert(b.end(), 16, 0x00); // +0x10-+0x1F padding
    const size_t nameTableStart = b.size();
    for (const auto& n : names) appendCString(b, n);
    const uint32_t nameTableLength = static_cast<uint32_t>(b.size() - nameTableStart);
    b[0x04] = static_cast<uint8_t>(nameTableLength & 0xFF);
    b[0x05] = static_cast<uint8_t>((nameTableLength >> 8) & 0xFF);
    b[0x06] = static_cast<uint8_t>((nameTableLength >> 16) & 0xFF);
    b[0x07] = static_cast<uint8_t>((nameTableLength >> 24) & 0xFF);
    return b;
}

// 0x00da8790: "strip extension, append the platform-selected one."
void testComposeMatlibFileName() {
    CHECK(sr3geometry::ComposeMatlibFileName("brad_head") == "brad_head.matlib_pc");
    CHECK(sr3geometry::ComposeMatlibFileName("brad_head.ccmesh_pc") == "brad_head.matlib_pc");
    CHECK(sr3geometry::ComposeMatlibFileName("brad_head.gcmesh_pc") == "brad_head.matlib_pc");
    // No extension to strip at all - still appends cleanly.
    CHECK(sr3geometry::ComposeMatlibFileName("nodot") == "nodot.matlib_pc");
}

// The test this task's own spec names at minimum: brad's matlib resolves to
// a named, type-tagged resource keyed by brad's own character identity,
// deterministically.
void testBradMatlibResolvesNamedTypeTaggedKeyedByIdentity() {
    const std::vector<std::string> names = {
        "brad_sm_head_d.tga",  "brad_sm_head_n.tga",    "skin_ca_02_sb.tga",
        "shd_whiteopaque.tga", "eye_reflect_01_sb.tga", "f_eyelashes_d.tga",
    };
    std::vector<uint8_t> matlibBytes = buildMatlib(names);

    auto loadByName = [&](const std::string& composed, std::vector<uint8_t>& out) {
        if (composed != "brad_head.matlib_pc") return false; // any other name: not this identity's file
        out = matlibBytes;
        return true;
    };

    sr3geometry::CharacterMatlibAttachment a =
        sr3geometry::ResolveCharacterMatlibAttachment("brad", "brad_head", loadByName);

    // Named: the cache key is the identity's own Name, not a texture or mesh
    // name (§12.1b: "keyed by the identity's own Name").
    CHECK(a.identityName == "brad");
    // Type-tagged: registered under resource type 0x16, confirmed at all 4
    // real call sites.
    CHECK(static_cast<uint32_t>(a.type) == 0x16u);
    CHECK(a.type == sr3geometry::CharacterResourceType::kMatlib);
    // Filename-composed (0x00da8790) from the base resource name, not the
    // identity string itself and not the LOD mesh name.
    CHECK(a.composedFileName == "brad_head.matlib_pc");
    // Existence-checked (0x00da90d0) and attached as its own typed slot -
    // not spliced into any mesh's material table (this function never even
    // receives one).
    CHECK(a.locatedInArchive);
    CHECK(a.attached());
    // The attached resource is the matlib's OWN independent content, parsed
    // via the already-CONFIRMED general-purpose MaterialBlock reader - the
    // whole name list, not a single hand-picked texture name.
    CHECK(a.resource.textureSlotCount == names.size());
    CHECK(a.resource.textureNames == names);

    // Deterministic: resolving the same identity against the same archive
    // twice must produce an identical result.
    sr3geometry::CharacterMatlibAttachment a2 =
        sr3geometry::ResolveCharacterMatlibAttachment("brad", "brad_head", loadByName);
    CHECK(a2.identityName == a.identityName);
    CHECK(a2.type == a.type);
    CHECK(a2.composedFileName == a.composedFileName);
    CHECK(a2.locatedInArchive == a.locatedInArchive);
    CHECK(a2.resource.textureNames == a.resource.textureNames);
}

// A different identity composes and is keyed by ITS OWN name, not brad's -
// the mechanism is identity-keyed, not hardcoded to one character.
void testDifferentIdentityKeyedSeparately() {
    std::vector<uint8_t> matlibBytes = buildMatlib({"shaundi_sm_head_d.tga"});
    auto loadByName = [&](const std::string& composed, std::vector<uint8_t>& out) {
        if (composed != "shaundi_head.matlib_pc") return false;
        out = matlibBytes;
        return true;
    };
    sr3geometry::CharacterMatlibAttachment a =
        sr3geometry::ResolveCharacterMatlibAttachment("shaundi", "shaundi_head", loadByName);
    CHECK(a.identityName == "shaundi");
    CHECK(a.composedFileName == "shaundi_head.matlib_pc");
    CHECK(a.attached());
    CHECK(a.resource.textureNames.size() == 1);
    CHECK(a.resource.textureNames[0] == "shaundi_sm_head_d.tga");
}

// 0x00da90d0's negative case: the composed file genuinely does not exist
// in the archive. Resolution still runs (compose always produces a
// definite name); attachment does not.
void testNotFoundIsNotAttached() {
    auto loadByName = [](const std::string&, std::vector<uint8_t>&) { return false; };
    sr3geometry::CharacterMatlibAttachment a =
        sr3geometry::ResolveCharacterMatlibAttachment("brad", "brad_head", loadByName);
    CHECK(a.identityName == "brad");
    CHECK(a.composedFileName == "brad_head.matlib_pc"); // compose step still ran
    CHECK(!a.locatedInArchive);
    CHECK(!a.attached());
    CHECK(a.resource.textureSlotCount == 0);
    CHECK(a.resource.textureNames.empty());
}

// No existence predicate supplied at all - must resolve to "not attached",
// never crash on a null std::function.
void testNullLoaderIsNotAttached() {
    sr3geometry::CharacterMatlibAttachment a =
        sr3geometry::ResolveCharacterMatlibAttachment("brad", "brad_head", nullptr);
    CHECK(a.composedFileName == "brad_head.matlib_pc");
    CHECK(!a.attached());
}

// A real exists-check HIT whose bytes do not actually parse as a
// MaterialBlock (wrong magic) must not be reported as attached - a mis-
// shaped file is not a usable resource attachment, matching every other
// reader in this codebase refusing to guess past a parse failure.
void testExistsButWrongShapeIsNotAttached() {
    std::vector<uint8_t> garbage = {0xDE, 0xAD, 0xBE, 0xEF, 0, 0, 0, 0};
    auto loadByName = [&](const std::string& composed, std::vector<uint8_t>& out) {
        if (composed != "brad_head.matlib_pc") return false;
        out = garbage;
        return true;
    };
    sr3geometry::CharacterMatlibAttachment a =
        sr3geometry::ResolveCharacterMatlibAttachment("brad", "brad_head", loadByName);
    CHECK(a.composedFileName == "brad_head.matlib_pc");
    CHECK(!a.locatedInArchive);
    CHECK(!a.attached());
}

// PickMatlibTextureByRoleSuffix() - the CHOSEN (not spec-confirmed)
// role-suffix heuristic added for the --head-matlib-bind real-bind
// follow-up (spec-tables-customization.md §12.1f). Fixture is brad_head.
// matlib_pc's own REAL 6-entry list, exactly as confirmed against the real
// archive (HANDOFF.md 2026-10-08 follow-up) - not a hand-picked synthetic
// set, so this test reflects the actual disambiguation problem the
// heuristic exists to solve (two different "_d"-suffixed entries in the
// same matlib).
void testPickMatlibTextureByRoleSuffixDisambiguatesRealBradHeadList() {
    const std::vector<std::string> names = {
        "brad_sm_head_d.tga",  "brad_sm_head_n.tga",    "skin_ca_02_sb.tga",
        "shd_whiteopaque.tga", "eye_reflect_01_sb.tga", "f_eyelashes_d.tga",
    };

    // Diffuse: suffix alone is ambiguous (brad_sm_head_d.tga AND
    // f_eyelashes_d.tga both end in "_d") - the "head" substring requirement
    // is what picks the right one, first in on-disk order.
    const std::string* diffuse =
        sr3geometry::PickMatlibTextureByRoleSuffix(names, "head", {"_d", "_dp"});
    CHECK(diffuse != nullptr);
    CHECK(diffuse != nullptr && *diffuse == "brad_sm_head_d.tga");

    // Normal map: only one "_n"-suffixed entry, also contains "head".
    const std::string* normal = sr3geometry::PickMatlibTextureByRoleSuffix(names, "head", {"_n"});
    CHECK(normal != nullptr);
    CHECK(normal != nullptr && *normal == "brad_sm_head_n.tga");

    // Without the substring requirement, suffix-only matching picks the
    // FIRST "_d"-suffixed entry in on-disk order - demonstrating exactly
    // why the hint parameter exists (still deterministic, just not the one
    // this call site wants without the hint).
    const std::string* diffuseNoHint =
        sr3geometry::PickMatlibTextureByRoleSuffix(names, "", {"_d", "_dp"});
    CHECK(diffuseNoHint != nullptr);
    CHECK(diffuseNoHint != nullptr && *diffuseNoHint == "brad_sm_head_d.tga");

    // A hint that matches nothing, or a suffix nothing carries, both report
    // null rather than guessing.
    CHECK(sr3geometry::PickMatlibTextureByRoleSuffix(names, "upperbody", {"_d"}) == nullptr);
    CHECK(sr3geometry::PickMatlibTextureByRoleSuffix(names, "head", {"_zz"}) == nullptr);
    CHECK(sr3geometry::PickMatlibTextureByRoleSuffix({}, "head", {"_d"}) == nullptr);
}

// Case-insensitivity, both for the hint and the suffix - the real archive's
// own filenames are consistently lowercase, but the heuristic itself must
// not silently depend on that.
void testPickMatlibTextureByRoleSuffixCaseInsensitive() {
    const std::vector<std::string> names = {"Brad_SM_Head_D.TGA"};
    const std::string* diffuse = sr3geometry::PickMatlibTextureByRoleSuffix(names, "HEAD", {"_D"});
    CHECK(diffuse != nullptr);
    CHECK(diffuse != nullptr && *diffuse == "Brad_SM_Head_D.TGA");
}

}  // namespace

int main() {
    testComposeMatlibFileName();
    testBradMatlibResolvesNamedTypeTaggedKeyedByIdentity();
    testDifferentIdentityKeyedSeparately();
    testNotFoundIsNotAttached();
    testNullLoaderIsNotAttached();
    testExistsButWrongShapeIsNotAttached();
    testPickMatlibTextureByRoleSuffixDisambiguatesRealBradHeadList();
    testPickMatlibTextureByRoleSuffixCaseInsensitive();

    if (g_failures == 0) {
        std::cout << "All synthetic character-matlib-attachment tests passed.\n";
        return 0;
    }
    std::cerr << g_failures << " check(s) failed.\n";
    return 1;
}
