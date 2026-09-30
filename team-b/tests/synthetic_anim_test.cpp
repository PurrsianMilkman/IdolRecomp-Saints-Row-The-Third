// Synthetic tests for the .anim_pc reader (spec-anim-format.md), covering
// only the CONFIRMED header this reader implements. Includes the shapes
// the spec calls out explicitly as real and easy to get wrong: the
// flags-bit-0x10 gate on +0x40, the -1-means-null convention, and the 12
// real sub-0x48-byte files that must parse rather than be rejected.

#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "sr3anim/animation.h"

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

void putU32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    b[off + 0] = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    b[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    b[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

void putU16(std::vector<uint8_t>& b, size_t off, uint16_t v) {
    b[off + 0] = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

void putF32(std::vector<uint8_t>& b, size_t off, float v) {
    uint32_t raw = 0;
    std::memcpy(&raw, &v, sizeof(raw));
    putU32(b, off, raw);
}

struct AnimSpec {
    uint8_t version = sr3anim::kRequiredVersion;
    uint8_t flags = 0x09; // the most common real value (2167 of 4209 files)
    float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f; // identity rotation
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;            // in-place by default
    uint32_t trailingOffset = 0;
    uint32_t optionalOffset = 0;
    size_t totalSize = 0x80;
};

std::vector<uint8_t> buildAnim(const AnimSpec& s) {
    std::vector<uint8_t> b(s.totalSize, 0x00);
    putU32(b, 0x00, sr3anim::kMagic);
    b[0x04] = s.version;
    b[0x05] = s.flags;
    putU16(b, 0x06, 81); // plausible real value (population mean ~81)
    b[0x08] = 3;
    b[0x09] = 4;
    b[0x0A] = 45;
    b[0x0B] = 66;
    putF32(b, 0x0C, s.qx);
    putF32(b, 0x10, s.qy);
    putF32(b, 0x14, s.qz);
    putF32(b, 0x18, s.qw);
    putF32(b, 0x1C, s.tx);
    putF32(b, 0x20, s.ty);
    putF32(b, 0x24, s.tz);
    if (s.totalSize >= 0x34) putU32(b, 0x30, s.trailingOffset);
    if (s.totalSize >= 0x48) putU32(b, 0x40, s.optionalOffset);
    return b;
}

sr3anim::Animation parseSpec(const AnimSpec& s, std::vector<uint8_t>& storage) {
    storage = buildAnim(s);
    return sr3anim::Animation::parse(sr3anim::ByteView(storage.data(), storage.size()));
}

} // namespace

int main() {
    // --- Baseline: identity rotation, in-place, no optional section. ---
    {
        AnimSpec s;
        s.trailingOffset = 0x70;
        std::vector<uint8_t> blob;
        sr3anim::Animation a = parseSpec(s, blob);
        CHECK(a.version() == 14);
        CHECK(a.flags() == 0x09);
        CHECK(!a.hasOptionalSection());       // bit 0x10 clear
        CHECK(!a.hasOptionalSectionOffset());
        CHECK(a.rootRotation().w == 1.0f);
        CHECK(std::fabs(a.rootRotationNormSquared() - 1.0f) <= 1e-6f);
        CHECK(a.rootTranslation().x == 0.0f && a.rootTranslation().y == 0.0f &&
              a.rootTranslation().z == 0.0f);
        CHECK(a.hasTrailingOffset());
        CHECK(a.trailingOffset() == 0x70);
        // Uninterpreted counts must round-trip exactly as stored.
        CHECK(a.field_0x06_rawCount() == 81);
        CHECK(a.field_0x08_rawCount() == 3);
        CHECK(a.field_0x09_rawCount() == 4);
        CHECK(a.field_0x0A_rawCount() == 45);
        CHECK(a.field_0x0B_rawCount() == 66);
    }

    // --- A real-shaped 90-degree rotation with root translation: w =
    // sqrt(2)/2 is the exact value spec Sec4 reports for 286 real files.
    // The quaternion must still normalize to 1. ---
    {
        AnimSpec s;
        const float kHalfRoot2 = 0.70710678f;
        s.qx = 0.0f; s.qy = kHalfRoot2; s.qz = 0.0f; s.qw = kHalfRoot2;
        s.tx = 3.0f; s.ty = 0.0f; s.tz = 4.0f; // magnitude exactly 5
        std::vector<uint8_t> blob;
        sr3anim::Animation a = parseSpec(s, blob);
        CHECK(std::fabs(a.rootRotationNormSquared() - 1.0f) <= 1e-4f);
        CHECK(a.rootRotation().w > 0.7f && a.rootRotation().w < 0.71f);
        const auto& t = a.rootTranslation();
        CHECK(std::fabs(std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z) - 5.0f) <= 1e-5f);
    }

    // --- Flags bit 0x10 SET: the optional section at +0x40 must be read.
    // (spec Sec3 - CONFIRMED via disassembly that the loader only touches
    // +0x40 when this bit is set.) ---
    {
        AnimSpec s;
        s.flags = 0x1d; // a real observed value (87 files), bit 0x10 set
        s.optionalOffset = 0x60;
        std::vector<uint8_t> blob;
        sr3anim::Animation a = parseSpec(s, blob);
        CHECK(a.hasOptionalSection());
        CHECK(a.hasOptionalSectionOffset());
        CHECK(a.optionalSectionOffset() == 0x60);
        CHECK(!a.optionalSectionIsNull());
    }

    // --- Flags bit 0x10 set but the offset is -1: the loader's documented
    // "null" convention (spec Sec2). ---
    {
        AnimSpec s;
        s.flags = 0x1d;
        s.optionalOffset = sr3anim::kNullOffset;
        std::vector<uint8_t> blob;
        sr3anim::Animation a = parseSpec(s, blob);
        CHECK(a.hasOptionalSectionOffset());
        CHECK(a.optionalSectionIsNull());
    }

    // --- Flags bit 0x10 CLEAR: +0x40 must NOT be reported, even though
    // the bytes there hold something that would look like a valid offset.
    // This is the gate that matters - 3.4% of real ungated files have
    // offset-shaped bytes at +0x40 purely by chance (spec Sec3), and a
    // reader that ignores the flag would report those as real sections. ---
    {
        AnimSpec s;
        s.flags = 0x09;         // bit 0x10 clear
        s.optionalOffset = 0x60; // plausible-looking bytes sitting there anyway
        std::vector<uint8_t> blob;
        sr3anim::Animation a = parseSpec(s, blob);
        CHECK(!a.hasOptionalSection());
        CHECK(!a.hasOptionalSectionOffset()); // must not be surfaced
    }

    // --- The 12 real sub-0x48-byte files (spec Sec8 item 6: static prop
    // poses, all flags 0x09, identity-ish transform) must PARSE, not be
    // rejected for being short. Core fields still come through; the
    // length-dependent ones simply report absent. ---
    {
        AnimSpec s;
        s.totalSize = sr3anim::kCoreHeaderSize; // exactly the confirmed core, nothing more
        std::vector<uint8_t> blob;
        sr3anim::Animation a = parseSpec(s, blob);
        CHECK(a.version() == 14);
        CHECK(a.rootRotation().w == 1.0f);
        CHECK(!a.hasTrailingOffset());        // no room for +0x30
        CHECK(!a.hasOptionalSectionOffset()); // no room for +0x40
    }

    // --- Negative: bad magic must be rejected. ---
    {
        AnimSpec s;
        std::vector<uint8_t> blob = buildAnim(s);
        blob[0] ^= 0xFF;
        bool threw = false;
        try {
            sr3anim::Animation::parse(sr3anim::ByteView(blob.data(), blob.size()));
        } catch (const sr3anim::FormatError&) {
            threw = true;
        }
        CHECK(threw);
    }

    // --- Negative: any version other than exactly 14 must be rejected -
    // the real loader enforces this (spec Sec2, CONFIRMED both ways). ---
    {
        for (uint8_t bad : {uint8_t{0}, uint8_t{13}, uint8_t{15}, uint8_t{255}}) {
            AnimSpec s;
            s.version = bad;
            std::vector<uint8_t> blob = buildAnim(s);
            bool threw = false;
            try {
                sr3anim::Animation::parse(sr3anim::ByteView(blob.data(), blob.size()));
            } catch (const sr3anim::FormatError&) {
                threw = true;
            }
            CHECK(threw);
        }
    }

    // --- Negative: too short to hold even the confirmed core. ---
    {
        AnimSpec s;
        std::vector<uint8_t> blob = buildAnim(s);
        blob.resize(sr3anim::kCoreHeaderSize - 1);
        bool threw = false;
        try {
            sr3anim::Animation::parse(sr3anim::ByteView(blob.data(), blob.size()));
        } catch (const sr3anim::FormatError&) {
            threw = true;
        }
        CHECK(threw);
    }

    if (g_failures == 0) {
        std::cout << "All synthetic anim-format tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) FAILED.\n";
    return 1;
}
