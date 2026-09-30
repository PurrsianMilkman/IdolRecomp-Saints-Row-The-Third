// Synthetic tests for sr3vintdoc (spec-vint-doc-format.md). Every fixture is
// laid out byte by byte from the spec's own tables (Sec2/Sec3/Sec4/Sec5), not
// produced by the reader. Distinct values in adjacent fields catch
// off-by-one offsets. No game data.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "sr3vintdoc/vint_doc.h"

using namespace sr3vintdoc;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (0)

template <typename F>
bool throwsFormatError(F f) {
    try {
        f();
    } catch (const FormatError&) {
        return true;
    }
    return false;
}

struct Buf {
    std::vector<uint8_t> b;
    void u8(uint8_t v) { b.push_back(v); }
    void u16(uint16_t v) { u8(v & 0xFF); u8(v >> 8); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) u8((v >> (8 * i)) & 0xFF); }
    void f32(float f) { uint32_t u; std::memcpy(&u, &f, 4); u32(u); }
    void str(const std::string& s) { for (char c : s) u8(static_cast<uint8_t>(c)); u8(0); }
    vpp::ByteView view() const { return vpp::ByteView(b.data(), b.size()); }
};

// The Sec2 header with a distinct value in every field.
void writeHeader(Buf& w, uint16_t version, uint32_t meta, uint32_t crit, uint32_t sec16, uint16_t elems, uint16_t anims) {
    w.u32(0x00003027);  // +0x00 magic
    w.u32(0x11111111);  // +0x04 reserved (not enforced)
    w.u16(version);     // +0x08
    w.u32(0x3EB0C000);  // +0x0A OPEN float-like
    w.u32(meta);        // +0x0E
    w.u32(crit);        // +0x12
    w.u32(sec16);       // +0x16
    w.u16(elems);       // +0x1A
    w.u16(anims);       // +0x1C
}

} // namespace

int main() {
    // --- magic / header (Sec1.3, Sec2) --------------------------------
    {
        Buf w;
        writeHeader(w, 2, 3, 4, 0x55, 6, 7);
        CHECK(w.b.size() == kHeaderSize);
        CHECK(looksLikeVintDoc(w.view()));
        Header h = parseHeader(w.view());
        CHECK(h.magic == 0x3027);
        CHECK(h.reserved04 == 0x11111111);
        CHECK(h.version == 2);
        CHECK(h.field0ARaw == 0x3EB0C000);
        CHECK(h.metadataCount == 3);
        CHECK(h.criticalResourceCount == 4);
        CHECK(h.secondaryOffsetRaw == 0x55);
        CHECK(h.elementCount == 6);
        CHECK(h.animationCount == 7);

        // Version outside {1,2} and a non-zero reserved field are reported
        // raw, never thrown (errors.h).
        Buf v9;
        writeHeader(v9, 9, 0, 0, 0, 0, 0);
        CHECK(parseHeader(v9.view()).version == 9);
        Buf vWide;
        writeHeader(vWide, 0x0102, 0, 0, 0, 0, 0); // all 16 bits kept, not truncated
        CHECK(parseHeader(vWide.view()).version == 0x0102);

        Buf shortHdr = w;
        shortHdr.b.pop_back();
        CHECK(throwsFormatError([&] { parseHeader(shortHdr.view()); }));
        Buf bad = w;
        bad.b[0] = 0x28;
        CHECK(!looksLikeVintDoc(bad.view()));
        CHECK(throwsFormatError([&] { parseHeader(bad.view()); }));
        Buf tiny;
        tiny.u16(0x3027);
        CHECK(!looksLikeVintDoc(tiny.view()));
    }

    // --- string table (Sec3.1) -----------------------------------------
    {
        Buf w;
        writeHeader(w, 2, 0, 0, 0, 0, 0);
        w.u32(4);             // N
        w.u32(0);             // "document_depth"
        w.u32(9);             // suffix "depth" of the same stored string
        w.u32(15);            // "bitmap"
        w.u32(1000);          // out of file
        const size_t base = w.b.size();
        w.str("document_depth");
        w.str("bitmap");
        StringTable t = parseStringTable(w.view());
        CHECK(t.offsets.size() == 4);
        CHECK(t.base == kHeaderSize + 4 + 4 * 4);
        CHECK(t.base == base);
        std::string s;
        CHECK(resolveStringNulTerminated(w.view(), t, 0, s) && s == "document_depth");
        CHECK(resolveStringNulTerminated(w.view(), t, 1, s) && s == "depth");
        CHECK(resolveStringNulTerminated(w.view(), t, 2, s) && s == "bitmap");
        CHECK(!resolveStringNulTerminated(w.view(), t, 3, s) && s.empty());
        CHECK(!resolveStringNulTerminated(w.view(), t, 4, s)); // index out of range

        // The wrong base (header +0x16-style, 2 bytes too far) gives the
        // spec's own symptom: a truncated string.
        StringTable wrong = t;
        wrong.base += 2;
        CHECK(resolveStringNulTerminated(w.view(), wrong, 0, s) && s == "cument_depth");

        // No NUL before end of file.
        Buf nonul = w;
        nonul.b.pop_back();
        CHECK(!resolveStringNulTerminated(nonul.view(), parseStringTable(nonul.view()), 2, s));

        // Empty table: base right after the count.
        Buf e;
        writeHeader(e, 1, 0, 0, 0, 0, 0);
        e.u32(0);
        StringTable te = parseStringTable(e.view());
        CHECK(te.offsets.empty() && te.base == kHeaderSize + 4);

        // Count-driven array running past the end is a FormatError, and a
        // huge count must not allocate first.
        Buf over;
        writeHeader(over, 2, 0, 0, 0, 0, 0);
        over.u32(3);
        over.u32(0);
        over.u32(0);
        CHECK(throwsFormatError([&] { parseStringTable(over.view()); }));
        Buf huge;
        writeHeader(huge, 2, 0, 0, 0, 0, 0);
        huge.u32(0xFFFFFFFFu);
        CHECK(throwsFormatError([&] { parseStringTable(huge.view()); }));
        Buf noCount;
        writeHeader(noCount, 2, 0, 0, 0, 0, 0);
        CHECK(throwsFormatError([&] { parseStringTable(noCount.view()); }));
    }

    // --- critical resources and metadata (Sec3.2) ----------------------
    {
        Buf w;
        w.u8(0xA1); w.u32(0xDEADBEEF);             // v1: 5 bytes
        w.u8(0xB2); w.u32(0x01020304); w.u8(0xC3); // v2: 6 bytes
        w.u32(7); w.u32(8);                         // metadata
        Cursor c(w.view(), 0);
        CriticalResource r1 = readCriticalResource(c, 1);
        CHECK(c.pos() == 5);
        CHECK(r1.selectorRaw == 0xA1 && r1.valueRaw == 0xDEADBEEF && !r1.hasVersion2Byte);
        CriticalResource r2 = readCriticalResource(c, 2);
        CHECK(c.pos() == 11);
        CHECK(r2.selectorRaw == 0xB2 && r2.valueRaw == 0x01020304 && r2.hasVersion2Byte && r2.version2ByteRaw == 0xC3);
        MetadataEntry m = readMetadataEntry(c);
        CHECK(m.nameIndex == 7 && m.valueIndex == 8);
        CHECK(c.atEnd());
        CHECK(throwsFormatError([&] { readMetadataEntry(c); }));
    }

    // --- element head (Sec4) --------------------------------------------
    {
        Buf w;
        w.u32(5); w.u32(6); w.u16(0x0203); w.u8(0x7F); w.u8(0xEE);
        Cursor c(w.view(), 0);
        ElementHead e = readElementHead(c);
        CHECK(e.typeIndex == 5 && e.nameIndex == 6 && e.childCount == 0x0203 && e.skippedByteRaw == 0x7F);
        CHECK(c.pos() == 11);
        Buf t = w;
        t.b.resize(10);
        Cursor ct(t.view(), 0);
        CHECK(throwsFormatError([&] { readElementHead(ct); }));

        int n = 0;
        for (const char* name : kRegisteredElementTypes) {
            CHECK(isRegisteredElementType(name));
            ++n;
        }
        CHECK(n == 13);
        CHECK(isRegisteredElementType("sr2_map"));
        CHECK(!isRegisteredElementType("Bitmap"));
        CHECK(!isRegisteredElementType("vdo_bitmap_viewer"));
    }

    // --- property block header + selection rule (Sec5) ----------------
    {
        Buf w;
        w.u32(0x100);     // baseline offset
        w.u8(3);          // override count
        w.u32(10); w.u32(0x200);
        w.u32(11); w.u32(0x300);
        w.u32(11); w.u32(0x400); // a second entry for the same resolution: first match wins
        w.u8(0x99);
        Cursor c(w.view(), 0);
        PropertyBlockHeader h = readPropertyBlockHeader(c);
        CHECK(c.pos() == 5 + 3 * 8);
        CHECK(h.baselineOffsetRaw == 0x100 && h.overrides.size() == 3);
        CHECK(h.overrides[1].resolutionNameIndex == 11 && h.overrides[1].blockOffsetRaw == 0x300);
        CHECK(selectPropertyListOffset(h, [](uint32_t i) { return i == 11; }) == 0x300);
        CHECK(selectPropertyListOffset(h, [](uint32_t i) { return i == 10; }) == 0x200);
        CHECK(selectPropertyListOffset(h, [](uint32_t) { return false; }) == 0x100);
        CHECK(selectPropertyListOffset(h, nullptr) == 0x100);

        Buf none;
        none.u32(0x44); none.u8(0);
        Cursor cn(none.view(), 0);
        PropertyBlockHeader hn = readPropertyBlockHeader(cn);
        CHECK(hn.overrides.empty() && cn.pos() == 5);
    }

    // --- tagged property list (Sec5) ------------------------------------
    {
        CHECK(propertyValueSize(0) == 0);
        CHECK(propertyValueSize(1) == 4 && propertyValueSize(2) == 4 && propertyValueSize(3) == 4 &&
              propertyValueSize(4) == 4);
        CHECK(propertyValueSize(5) == 1 && propertyValueSize(6) == 12 && propertyValueSize(7) == 8);
        CHECK(propertyValueSize(8) == -1 && propertyValueSize(0xFF) == -1);

        Buf w;
        w.u8(1); w.u32(0xA0000001); w.u32(0xFFFFFFFE);
        w.u8(2); w.u32(0xA0000002); w.u32(0x80000000);
        w.u8(3); w.u32(0xA0000003); w.f32(0.345f);
        w.u8(4); w.u32(0xA0000004); w.u32(17);
        w.u8(5); w.u32(0xA0000005); w.u8(1);
        w.u8(6); w.u32(0xA0000006); w.f32(1.0f); w.f32(-2.5f); w.f32(3.25f);
        w.u8(7); w.u32(0xA0000007); w.f32(640.0f); w.f32(480.0f);
        w.u8(5); w.u32(0xA0000008); w.u8(0);
        w.u8(0);
        const size_t end = w.b.size();
        w.u8(0x42); // trailing byte after the terminator
        Cursor c(w.view(), 0);
        PropertyList l = readPropertyList(c);
        CHECK(l.terminated);
        CHECK(c.pos() == end);
        CHECK(l.properties.size() == 8);
        if (l.properties.size() == 8) {
            for (size_t i = 0; i < 7; ++i) {
                CHECK(l.properties[i].tag == i + 1);
                CHECK(l.properties[i].nameHash == 0xA0000001u + i);
            }
            CHECK(l.properties[0].rawU32() == 0xFFFFFFFE);
            CHECK(l.properties[1].rawU32() == 0x80000000);
            CHECK(l.properties[2].f32() == 0.345f);
            CHECK(l.properties[3].rawU32() == 17);
            CHECK(l.properties[4].boolean());
            CHECK(l.properties[5].f32(0) == 1.0f && l.properties[5].f32(1) == -2.5f && l.properties[5].f32(2) == 3.25f);
            CHECK(l.properties[6].f32(0) == 640.0f && l.properties[6].f32(1) == 480.0f);
            CHECK(!l.properties[7].boolean());
        }

        // Empty list: just the terminator.
        Buf e;
        e.u8(0);
        Cursor ce(e.view(), 0);
        PropertyList le = readPropertyList(ce);
        CHECK(le.terminated && le.properties.empty() && ce.pos() == 1);

        // Unknown tag (outside the shipped 1-7): stop, report, leave the
        // cursor on the tag byte; properties read so far are kept.
        Buf u;
        u.u8(3); u.u32(1); u.f32(2.0f);
        u.u8(8); u.u32(2); u.u32(3);
        Cursor cu(u.view(), 0);
        PropertyList lu = readPropertyList(cu);
        CHECK(!lu.terminated && lu.unknownTag == 8);
        CHECK(lu.properties.size() == 1);
        CHECK(cu.pos() == 9);

        // Truncated value or missing terminator: FormatError.
        Buf t1;
        t1.u8(6); t1.u32(1); t1.f32(1.0f);
        Cursor ct1(t1.view(), 0);
        CHECK(throwsFormatError([&] { readPropertyList(ct1); }));
        Buf t2;
        t2.u8(5); t2.u32(1); t2.u8(1);
        Cursor ct2(t2.view(), 0);
        CHECK(throwsFormatError([&] { readPropertyList(ct2); }));
    }

    if (g_failures == 0) {
        std::cout << "ALL sr3vintdoc SYNTHETIC TESTS PASSED\n";
        return 0;
    }
    std::cerr << g_failures << " CHECK(S) FAILED\n";
    return 1;
}
