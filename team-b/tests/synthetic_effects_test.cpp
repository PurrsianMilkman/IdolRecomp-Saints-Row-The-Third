// Synthetic tests for the .cefct_pc reader. Every file is built from scratch
// out of the CONFIRMED facts in spec-effects-format.md only: the shared
// material block (§2, §5.2), the `71BW` root header and its count/pointer
// pairs (§5.3), the 0x58-byte sub-object records (§5.4), the P/Q block
// pitches and their contiguity (§6.2 item 1, §6.9, §6.11). No real game data
// is used, so a pass here proves the reader implements the spec's text, not
// that the spec is right (the real-data statement is
// tools/validation/validate_cefct_population.cpp).
//
// The negative cases matter as much as the positive ones: each check the
// reader exposes is shown to FAIL on an input violating exactly that fact.

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "sr3effects/effects.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":" << __LINE__ << "\n"; \
            ++g_failures;                                                              \
        }                                                                              \
    } while (0)

using sr3effects::ByteView;

struct Spec {
    std::vector<std::string> textures = {"vfx_a.tga"};
    std::string source = "vfx_test.effectx";
    uint32_t version = 43;
    uint32_t subObjects = 2;
    uint32_t filters = 1;
    uint32_t c40 = 0, c70 = 0, c90 = 0;
    float duration = 1.5f;
};

size_t align16(size_t v) { return (v + 15) / 16 * 16; }

struct Builder {
    std::vector<uint8_t> file;
    size_t root = 0;

    void put32(size_t abs, uint32_t v) {
        if (file.size() < abs + 4) file.resize(abs + 4, 0);
        for (int i = 0; i < 4; ++i) file[abs + i] = static_cast<uint8_t>(v >> (8 * i));
    }
    void put16(size_t abs, uint16_t v) {
        if (file.size() < abs + 2) file.resize(abs + 2, 0);
        file[abs] = static_cast<uint8_t>(v);
        file[abs + 1] = static_cast<uint8_t>(v >> 8);
    }
    void rput32(size_t rel, uint32_t v) { put32(root + rel, v); }
    void grow(size_t relEnd) {
        if (file.size() < root + relEnd) file.resize(root + relEnd, 0);
    }
    // Appends a NUL-terminated string at the end and returns its root-relative offset.
    uint32_t appendString(const std::string& s) {
        size_t rel = file.size() - root;
        file.insert(file.end(), s.begin(), s.end());
        file.push_back(0);
        return static_cast<uint32_t>(rel);
    }
};

// Builds a file laid out exactly as spec §6.11 describes: records, then P
// blocks at align16(records end) with pitch 0x4A8, then Q blocks at
// align16(P end) with pitch 0xD8, then the other arrays, a shared 16-byte
// blob every P pointer slot targets, and finally the strings, so the end
// field (root+0x38) equals file size - root.
Builder build(const Spec& s) {
    Builder b;
    // Material block (spec §5.2): 0x20-byte header then the name run.
    std::vector<uint8_t> names;
    for (const auto& t : s.textures) {
        names.insert(names.end(), t.begin(), t.end());
        names.push_back(0);
    }
    // The material block holds ONLY the texture names; the length field is
    // their total (strlen+1) - the arrangement measured on real files
    // (spec §5.2's "also spans the .effectx name" is not what the bytes show).
    const uint32_t nameTableLen = static_cast<uint32_t>(names.size());
    b.file.assign(0x20, 0);
    b.put32(0, 0x00043854u);
    b.put32(4, nameTableLen);
    b.put32(0xC, static_cast<uint32_t>(s.textures.size()));
    b.file.insert(b.file.end(), names.begin(), names.end());
    b.root = align16(0x20 + nameTableLen + 1);
    b.file.resize(b.root + 0xB0, 0);

    b.rput32(0x00, 0x57423137u); // "71BW"
    b.rput32(0x04, s.version);
    b.rput32(0x08, 0);
    {
        uint32_t d;
        std::memcpy(&d, &s.duration, 4);
        b.rput32(0x20, d);
    }

    size_t c = 0xB0; // root-relative cursor
    const size_t recs = c;
    c += static_cast<size_t>(s.subObjects) * 0x58;
    const size_t p0 = align16(c);
    c = p0 + static_cast<size_t>(s.subObjects) * 0x4A8;
    const size_t q0 = align16(c);
    c = q0 + static_cast<size_t>(s.subObjects) * 0xD8;
    const size_t filt = align16(c);
    c = filt + static_cast<size_t>(s.filters) * 0x70;
    const size_t a40 = align16(c);
    c = a40 + static_cast<size_t>(s.c40) * 0x88;
    const size_t a70 = align16(c);
    c = a70 + static_cast<size_t>(s.c70) * 0xC8;
    const size_t a90 = align16(c);
    c = a90 + static_cast<size_t>(s.c90) * 0x228;
    const size_t tex = align16(c);
    c = tex + s.textures.size() * 8;
    const size_t blob = align16(c);
    c = blob + 16;
    b.grow(c);

    // Strings go at the very end so the end field can name the file end.
    std::vector<uint32_t> subNames, filtNames, texNames;
    for (uint32_t k = 0; k < s.subObjects; ++k) subNames.push_back(b.appendString("Object0" + std::to_string(k + 1)));
    for (uint32_t k = 0; k < s.filters; ++k) filtNames.push_back(b.appendString("VFX Filter0" + std::to_string(k + 1)));
    for (const auto& t : s.textures) texNames.push_back(b.appendString(t));
    b.rput32(0x18, b.appendString(s.source)); // root+0x18 -> the source-name string (spec §5.3 pointer)

    // Root count/pointer pairs (§5.3).
    b.rput32(0x28, static_cast<uint32_t>(s.textures.size())); b.rput32(0x30, static_cast<uint32_t>(tex));
    b.rput32(0x40, s.c40);                                    b.rput32(0x48, static_cast<uint32_t>(a40));
    b.rput32(0x60, s.subObjects);                             b.rput32(0x68, static_cast<uint32_t>(recs));
    b.rput32(0x70, s.c70);                                    b.rput32(0x78, static_cast<uint32_t>(a70));
    b.rput32(0x80, s.filters);                                b.rput32(0x88, static_cast<uint32_t>(filt));
    b.rput32(0x90, s.c90);                                    b.rput32(0x98, static_cast<uint32_t>(a90));

    for (size_t i = 0; i < s.textures.size(); ++i) b.rput32(tex + i * 8, texNames[i]);

    for (uint32_t k = 0; k < s.subObjects; ++k) {
        const size_t r = recs + static_cast<size_t>(k) * 0x58;
        b.rput32(r + 0x00, subNames[k]);
        b.rput32(r + 0x08, 0xFFFFFFFFu);
        b.rput32(r + 0x10, k == 0 ? 0xEC276F11u : 0xFAA8D302u);
        b.file[b.root + r + 0x14] = 1;
        b.file[b.root + r + 0x15] = 0;
        b.file[b.root + r + 0x16] = 0;
        b.file[b.root + r + 0x17] = (k == 0) ? 1 : 0;
        b.rput32(r + 0x18, 0xFFFFFFFFu);
        b.rput32(r + 0x28, static_cast<uint32_t>(p0 + static_cast<size_t>(k) * 0x4A8));
        b.rput32(r + 0x30, static_cast<uint32_t>(q0 + static_cast<size_t>(k) * 0xD8));

        const size_t p = p0 + static_cast<size_t>(k) * 0x4A8;
        b.put16(b.root + p + 0x02, k == 0 ? 0 : 6); // emitter type
        // Curve count at +0x288: positive on record 0, zero on record 1, so
        // the "+0x290 only when count > 0" rule (§6.2 item 1) is exercised.
        b.rput32(p + 0x288, k == 0 ? 1u : 0u);
        auto slot = [&](size_t off, bool set) { b.rput32(p + off, set ? static_cast<uint32_t>(blob) : 0u); };
        slot(0x1E0, true);
        slot(0x1F0, true);
        for (size_t o = 0x220; o <= 0x480; o += 0x10) slot(o, o != 0x290 || k == 0);
    }
    for (uint32_t k = 0; k < s.filters; ++k) {
        const size_t r = filt + static_cast<size_t>(k) * 0x70;
        b.rput32(r + 0x00, filtNames[k]);
        b.rput32(r + 0x10, 0x7EDA2EE8u);
    }

    b.rput32(0x38, static_cast<uint32_t>(b.file.size() - b.root)); // §5.3: == file size - root
    return b;
}

ByteView view(const std::vector<uint8_t>& v) { return ByteView(v.data(), v.size()); }

bool throwsFormatError(const std::vector<uint8_t>& v) {
    try {
        sr3effects::EffectFile::parse(view(v));
    } catch (const sr3effects::FormatError&) {
        return true;
    }
    return false;
}

} // namespace

int main() {
    // --- §5.2's published table: name-table length -> marker position. ---
    {
        // (65 -> 112), (21 -> 64), (67 -> 112): three real samples' arithmetic.
        struct Row { uint32_t len; size_t root; };
        for (Row r : {Row{65, 112}, Row{21, 64}, Row{67, 112}}) {
            std::vector<uint8_t> v(0x200, 0);
            v[4] = static_cast<uint8_t>(r.len);
            size_t got = 0;
            CHECK(sr3effects::predictRootOffset(view(v), got));
            CHECK(got == r.root);
        }
        // Too small to hold the material header -> false, no throw.
        std::vector<uint8_t> tiny(8, 0);
        size_t got = 0;
        CHECK(!sr3effects::predictRootOffset(view(tiny), got));
    }

    // --- Positive: a well-formed file, every array count zero except the
    // three the spec has real samples of. ---
    {
        Spec s;
        Builder b = build(s);
        sr3effects::EffectFile f = sr3effects::EffectFile::parse(view(b.file));
        CHECK(f.rootOffset() == b.root);
        CHECK(f.version() == 43);
        CHECK(f.duration() == 1.5f);
        CHECK(f.namesParsed());
        CHECK(f.textureNames().size() == 1 && f.textureNames()[0] == "vfx_a.tga");
        {
            std::string src;
            CHECK(f.stringAt(f.pointer18(), src));
            CHECK(src == "vfx_test.effectx");
            CHECK(!f.stringAt(0xFFFFFFFFu, src) && src.empty());
            CHECK(!f.stringAt(0x7FFFFFF0u, src));
        }
        CHECK(f.nameBytesConsumed() == f.nameTableLength());
        CHECK(f.textureCount() == 1);
        CHECK(f.endFieldMatchesFileSize());
        CHECK(f.endField() == b.file.size() - b.root);
        CHECK(f.textureList().count == 1);
        CHECK(f.array40().count == 0 && f.array70().count == 0 && f.array90().count == 0);
        CHECK(f.subObjects().count == 2 && f.filters().count == 1);
        CHECK(f.arrayFitsInFile(f.array40()) && f.arrayFitsInFile(f.array70()) &&
              f.arrayFitsInFile(f.array90()) && f.arrayFitsInFile(f.subObjects()) &&
              f.arrayFitsInFile(f.filters()) && f.arrayFitsInFile(f.textureList()));

        auto recs = f.readSubObjects();
        CHECK(recs.size() == 2);
        CHECK(recs[0].nameValid && recs[0].name == "Object01");
        CHECK(recs[1].nameValid && recs[1].name == "Object02");
        CHECK(recs[0].classId == 0xEC276F11u && recs[1].classId == 0xFAA8D302u);
        CHECK(recs[0].flags[0] == 1 && recs[0].flags[3] == 1 && recs[1].flags[3] == 0);
        auto fc = f.readFilterClassIds();
        CHECK(fc.size() == 1 && fc[0] == 0x7EDA2EE8u);
        auto et = f.readEmitterTypes();
        CHECK(et.size() == 2 && et[0] == 0 && et[1] == 6);

        auto lc = f.checkSubObjectLayout();
        CHECK(lc.applicable && lc.ok());
        CHECK(lc.records == 2);
        // 41 slots on record 0 (count at +0x288 is 1, so +0x290 counts);
        // 40 on record 1 (count 0, so +0x290 is exempt): 81 in total.
        CHECK(lc.slotsChecked == 81);
        CHECK(lc.slotsBad == 0 && lc.slotsNull == 0 && lc.tailNonZero == 0);
    }

    // --- Populated arrays at 0x40/0x70/0x90: their counts are read and their
    // extents (count * stride) tested against the file, nothing else. ---
    {
        Spec s;
        s.c40 = 3; s.c70 = 2; s.c90 = 5;
        Builder b = build(s);
        sr3effects::EffectFile f = sr3effects::EffectFile::parse(view(b.file));
        CHECK(f.array40().count == 3 && f.array40().stride == 0x88);
        CHECK(f.array70().count == 2 && f.array70().stride == 0xC8);
        CHECK(f.array90().count == 5 && f.array90().stride == 0x228);
        CHECK(f.arrayFitsInFile(f.array40()) && f.arrayFitsInFile(f.array70()) && f.arrayFitsInFile(f.array90()));
        CHECK(f.array90().extentEnd() == static_cast<uint64_t>(f.array90().pointer) + 5u * 0x228u);
        CHECK(f.checkSubObjectLayout().ok());
        CHECK(f.endFieldMatchesFileSize());
    }

    // --- Version range 42..44 is accepted; 41 and 45 are not (§5.3). ---
    for (uint32_t v : {42u, 43u, 44u}) {
        Spec s; s.version = v;
        Builder b = build(s);
        CHECK(!throwsFormatError(b.file));
    }
    for (uint32_t v : {0u, 41u, 45u, 0xFFFFFFFFu}) {
        Spec s; s.version = v;
        Builder b = build(s);
        CHECK(throwsFormatError(b.file));
    }

    // --- Negative: hard-rule violations throw. ---
    {
        Spec s;
        Builder b = build(s);
        std::vector<uint8_t> badMagic = b.file;
        badMagic[0] ^= 0xFF;
        CHECK(throwsFormatError(badMagic));

        std::vector<uint8_t> noMarker = b.file;
        noMarker[b.root] ^= 0xFF;
        CHECK(throwsFormatError(noMarker));

        // Marker 16 bytes late: name-table length says root is here, marker is not.
        std::vector<uint8_t> shifted = b.file;
        shifted.insert(shifted.begin() + static_cast<std::ptrdiff_t>(b.root), 16, 0);
        CHECK(throwsFormatError(shifted));

        std::vector<uint8_t> tiny(10, 0);
        CHECK(throwsFormatError(tiny));

        // A name-table length so large the marker would fall outside the file.
        std::vector<uint8_t> hugeLen = b.file;
        hugeLen[4] = 0xFF; hugeLen[5] = 0xFF; hugeLen[6] = 0xFF; hugeLen[7] = 0x7F;
        CHECK(throwsFormatError(hugeLen));

        // File that ends inside the root header.
        std::vector<uint8_t> cut(b.file.begin(), b.file.begin() + static_cast<std::ptrdiff_t>(b.root + 0x40));
        CHECK(throwsFormatError(cut));
    }

    // --- Negative (reported, not thrown): exact-consumption gate. ---
    {
        Spec s;
        Builder b = build(s);
        std::vector<uint8_t> shorter(b.file.begin(), b.file.end() - 1);
        sr3effects::EffectFile f1 = sr3effects::EffectFile::parse(view(shorter));
        CHECK(!f1.endFieldMatchesFileSize());
        CHECK(f1.endSlack() == -1); // declared end lies one byte beyond the file
        std::vector<uint8_t> longer = b.file;
        longer.insert(longer.end(), 16, 0);
        sr3effects::EffectFile f2 = sr3effects::EffectFile::parse(view(longer));
        CHECK(!f2.endFieldMatchesFileSize());
        CHECK(f2.endSlack() == 16); // 16 bytes follow the declared end (the shape real files with c40 > 0 show)
        // The pristine file passes, so the gate is not simply always false.
        sr3effects::EffectFile f0 = sr3effects::EffectFile::parse(view(b.file));
        CHECK(f0.endFieldMatchesFileSize() && f0.endSlack() == 0);
    }

    // --- Negative (reported, not thrown): array extents. ---
    {
        Spec s;
        s.c90 = 5;
        Builder b = build(s);
        // Count so large count*stride exceeds the file (and 32-bit arithmetic if done naively).
        b.rput32(0x90, 0x7FFFFFFFu);
        sr3effects::EffectFile f = sr3effects::EffectFile::parse(view(b.file));
        CHECK(!f.arrayFitsInFile(f.array90()));
        CHECK(f.arrayFitsInFile(f.array40())); // untouched arrays still fit

        // Positive count with a null pointer cannot fit.
        Spec s2; s2.c40 = 1;
        Builder b2 = build(s2);
        b2.rput32(0x48, 0xFFFFFFFFu);
        sr3effects::EffectFile f2 = sr3effects::EffectFile::parse(view(b2.file));
        CHECK(!f2.arrayFitsInFile(f2.array40()));
        // Zero count with a null pointer fits (the on-disk sentinel, §5.3).
        Spec s3;
        Builder b3 = build(s3);
        b3.rput32(0x48, 0xFFFFFFFFu);
        sr3effects::EffectFile f3 = sr3effects::EffectFile::parse(view(b3.file));
        CHECK(f3.arrayFitsInFile(f3.array40()));
    }

    // --- Negative (reported, not thrown): each layout sub-check fails on the
    // input that violates exactly that fact (§6.11). ---
    {
        Spec s;
        Builder pristine = build(s);
        CHECK(sr3effects::EffectFile::parse(view(pristine.file)).checkSubObjectLayout().ok());

        { // sub-object count + 1: extents shift, every P/Q pointer mismatches
            Builder b = pristine;
            b.rput32(0x60, s.subObjects + 1);
            auto lc = sr3effects::EffectFile::parse(view(b.file)).checkSubObjectLayout();
            CHECK(!lc.ok());
            CHECK(lc.paramPointerMismatch > 0);
        }
        { // one record's P pointer moved +16: contiguity broken, only that record
            Builder b = pristine;
            const size_t root = b.root;
            const uint32_t recs = static_cast<uint32_t>(
                b.file[root + 0x68] | (b.file[root + 0x69] << 8) | (b.file[root + 0x6A] << 16));
            const size_t at = root + recs + 1 * 0x58 + 0x28;
            uint32_t v = b.file[at] | (b.file[at + 1] << 8) | (b.file[at + 2] << 16);
            b.put32(at, v + 16);
            auto lc = sr3effects::EffectFile::parse(view(b.file)).checkSubObjectLayout();
            CHECK(!lc.ok());
            CHECK(lc.paramPointerMismatch == 1);
            CHECK(lc.statePointerMismatch == 0);
        }
        { // a Q pointer moved
            Builder b = pristine;
            const size_t root = b.root;
            const uint32_t recs = static_cast<uint32_t>(
                b.file[root + 0x68] | (b.file[root + 0x69] << 8) | (b.file[root + 0x6A] << 16));
            const size_t at = root + recs + 0x30;
            uint32_t v = b.file[at] | (b.file[at + 1] << 8) | (b.file[at + 2] << 16);
            b.put32(at, v + 16);
            auto lc = sr3effects::EffectFile::parse(view(b.file)).checkSubObjectLayout();
            CHECK(!lc.ok());
            CHECK(lc.statePointerMismatch == 1);
            CHECK(lc.paramPointerMismatch == 0);
        }
        { // a P pointer slot made misaligned -> slotsBad
            Builder b = pristine;
            const size_t root = b.root;
            uint32_t recs = static_cast<uint32_t>(
                b.file[root + 0x68] | (b.file[root + 0x69] << 8) | (b.file[root + 0x6A] << 16));
            uint32_t p = static_cast<uint32_t>(
                b.file[root + recs + 0x28] | (b.file[root + recs + 0x29] << 8) | (b.file[root + recs + 0x2A] << 16));
            const size_t slot = root + p + 0x1E0;
            uint32_t v = b.file[slot] | (b.file[slot + 1] << 8) | (b.file[slot + 2] << 16);
            b.put32(slot, v + 8);
            auto lc = sr3effects::EffectFile::parse(view(b.file)).checkSubObjectLayout();
            CHECK(!lc.ok() && lc.slotsBad == 1);
        }
        { // a P pointer slot pointing INTO the Q blocks ("past the Q blocks" violated)
            Builder b = pristine;
            const size_t root = b.root;
            uint32_t recs = static_cast<uint32_t>(
                b.file[root + 0x68] | (b.file[root + 0x69] << 8) | (b.file[root + 0x6A] << 16));
            uint32_t p = static_cast<uint32_t>(
                b.file[root + recs + 0x28] | (b.file[root + recs + 0x29] << 8) | (b.file[root + recs + 0x2A] << 16));
            uint32_t q = static_cast<uint32_t>(
                b.file[root + recs + 0x30] | (b.file[root + recs + 0x31] << 8) | (b.file[root + recs + 0x32] << 16));
            b.put32(root + p + 0x220, q); // 16-aligned, in the file, but not past the Q blocks
            auto lc = sr3effects::EffectFile::parse(view(b.file)).checkSubObjectLayout();
            CHECK(!lc.ok() && lc.slotsBad == 1);
        }
        { // a null slot is tolerated but counted
            Builder b = pristine;
            const size_t root = b.root;
            uint32_t recs = static_cast<uint32_t>(
                b.file[root + 0x68] | (b.file[root + 0x69] << 8) | (b.file[root + 0x6A] << 16));
            uint32_t p = static_cast<uint32_t>(
                b.file[root + recs + 0x28] | (b.file[root + recs + 0x29] << 8) | (b.file[root + recs + 0x2A] << 16));
            b.put32(root + p + 0x230, 0xFFFFFFFFu);
            auto lc = sr3effects::EffectFile::parse(view(b.file)).checkSubObjectLayout();
            CHECK(lc.ok() && lc.slotsNull == 1 && lc.slotsBad == 0);
        }
        { // non-zero P+0x484..0x4A7 (last byte of the block)
            Builder b = pristine;
            const size_t root = b.root;
            uint32_t recs = static_cast<uint32_t>(
                b.file[root + 0x68] | (b.file[root + 0x69] << 8) | (b.file[root + 0x6A] << 16));
            uint32_t p = static_cast<uint32_t>(
                b.file[root + recs + 0x28] | (b.file[root + recs + 0x29] << 8) | (b.file[root + recs + 0x2A] << 16));
            b.file[root + p + 0x4A7] = 1;
            auto lc = sr3effects::EffectFile::parse(view(b.file)).checkSubObjectLayout();
            CHECK(!lc.ok() && lc.tailNonZero == 1);
        }
        { // the +0x290 exemption: a null there is exempt when +0x288 == 0, counted when > 0
            Builder b = pristine;
            const size_t root = b.root;
            uint32_t recs = static_cast<uint32_t>(
                b.file[root + 0x68] | (b.file[root + 0x69] << 8) | (b.file[root + 0x6A] << 16));
            uint32_t p1 = static_cast<uint32_t>(
                b.file[root + recs + 0x58 + 0x28] | (b.file[root + recs + 0x58 + 0x29] << 8) |
                (b.file[root + recs + 0x58 + 0x2A] << 16));
            b.put32(root + p1 + 0x290, 0x12345u); // garbage, but record 1 has count 0 at +0x288
            auto lc = sr3effects::EffectFile::parse(view(b.file)).checkSubObjectLayout();
            CHECK(lc.ok() && lc.slotsChecked == 81);
            b.rput32(p1 + 0x288, 1); // now the slot is live and the garbage must be caught
            lc = sr3effects::EffectFile::parse(view(b.file)).checkSubObjectLayout();
            CHECK(!lc.ok() && lc.slotsChecked == 82 && lc.slotsBad == 1);
        }
    }

    // --- Record edge cases: a null name pointer and an out-of-file one. ---
    {
        Spec s;
        Builder b = build(s);
        const size_t root = b.root;
        uint32_t recs = static_cast<uint32_t>(
            b.file[root + 0x68] | (b.file[root + 0x69] << 8) | (b.file[root + 0x6A] << 16));
        b.put32(root + recs + 0x00, 0xFFFFFFFFu);        // record 0: null name
        b.put32(root + recs + 0x58 + 0x00, 0x7FFFFFF0u); // record 1: far outside the file
        auto r = sr3effects::EffectFile::parse(view(b.file)).readSubObjects();
        CHECK(r.size() == 2);
        CHECK(!r[0].nameValid && r[0].name.empty());
        CHECK(!r[1].nameValid && r[1].name.empty());
    }

    // --- Zero sub-objects: layout check is "not applicable", not "ok". ---
    {
        Spec s; s.subObjects = 0; s.filters = 0;
        Builder b = build(s);
        sr3effects::EffectFile f = sr3effects::EffectFile::parse(view(b.file));
        CHECK(f.readSubObjects().empty());
        auto lc = f.checkSubObjectLayout();
        CHECK(!lc.applicable && !lc.ok());
    }

    // --- Names: several textures, and an unterminated run is reported, not thrown. ---
    {
        Spec s;
        s.textures = {"a.tga", "bb.tga", "ccc.tga"};
        Builder b = build(s);
        sr3effects::EffectFile f = sr3effects::EffectFile::parse(view(b.file));
        CHECK(f.namesParsed() && f.textureNames().size() == 3 && f.textureNames()[2] == "ccc.tga");
        CHECK(f.nameBytesConsumed() == f.nameTableLength()); // by construction of the builder

        // Claim more textures than there are strings: the walk runs into root.
        Builder b2 = build(Spec{});
        b2.put32(0xC, 40);
        sr3effects::EffectFile f2 = sr3effects::EffectFile::parse(view(b2.file));
        CHECK(!f2.namesParsed() && f2.textureNames().empty());
    }

    if (g_failures == 0) {
        std::cout << "All synthetic effects-format tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) FAILED.\n";
    return 1;
}
