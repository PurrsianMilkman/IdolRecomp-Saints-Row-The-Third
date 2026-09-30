// Synthetic tests for the .asm_pc manifest reader. Every fixture is built
// from the TEXT of spec-asm-format.md (version-11 layout, sections 7.1-7.5),
// never from the reader's own source:
//   * the byte string in test 1 is the spec's section 7.5 worked example,
//     transcribed by hand from that section (its bytes are written out in the
//     spec as hex groups; this test spells them the same way);
//   * the builder below writes exactly the field order of sections 7.1-7.4
//     and shares no code with src/manifest.cpp.
// Each test names the spec statement it rests on and the reader mistake it
// would catch (what "red" would look like), so a green run is not just the
// reader agreeing with itself. Validation against the real shipped files is
// tools/validation/validate_asm_population.cpp.

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "sr3asm/manifest.h"

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

using Bytes = std::vector<uint8_t>;

void u8(Bytes& b, unsigned v) { b.push_back(static_cast<uint8_t>(v & 0xFF)); }
void u16(Bytes& b, unsigned v) { u8(b, v); u8(b, v >> 8); }
void u32(Bytes& b, uint32_t v) { u16(b, v & 0xFFFF); u16(b, v >> 16); }
void lpstr(Bytes& b, const std::string& s) {
    u16(b, static_cast<unsigned>(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}

// ---- fixture description, mirroring spec 7.3 / 7.4 field for field ----
struct E {
    std::string name;
    unsigned type = 0, pool = 0, flags = 0, variant = 0;
    uint32_t primary = 0, secondary = 0;
    unsigned group = 0;
};
struct R {
    std::string name;
    unsigned kind = 0, recordFlags = 0x0080;
    uint32_t headerRegion = 0x1800;
    std::string source;
    Bytes extra;
    uint32_t payloadLength = 0;
    std::vector<E> entries;
    // The size table normally equals each entry's own (primary, secondary)
    // (spec 7.3: 390,134/390,134). Tests that need the table to be read from
    // its own position, not the trailer's, override it here.
    std::vector<std::pair<uint32_t, uint32_t>> sizeTableOverride;
};
using Table = std::vector<std::pair<std::string, unsigned>>;

Bytes buildRecord(const R& r) {
    Bytes b;
    lpstr(b, r.name);
    u8(b, r.kind);
    u16(b, r.recordFlags);
    u16(b, static_cast<unsigned>(r.entries.size())); // s16 count
    u32(b, r.headerRegion);
    lpstr(b, r.source);
    u32(b, static_cast<uint32_t>(r.extra.size()));
    b.insert(b.end(), r.extra.begin(), r.extra.end());
    u32(b, r.payloadLength);
    // size table precedes ALL names (spec 7.3)
    for (size_t i = 0; i < r.entries.size(); ++i) {
        if (!r.sizeTableOverride.empty()) {
            u32(b, r.sizeTableOverride[i].first);
            u32(b, r.sizeTableOverride[i].second);
        } else {
            u32(b, r.entries[i].primary);
            u32(b, r.entries[i].secondary);
        }
    }
    for (const E& e : r.entries) {
        lpstr(b, e.name);
        u8(b, e.type);
        u8(b, e.pool);
        u8(b, e.flags);
        u8(b, e.variant);
        u32(b, e.primary);
        u32(b, e.secondary);
        u8(b, e.group);
    }
    return b;
}

void appendTable(Bytes& b, const Table& t) {
    u32(b, static_cast<uint32_t>(t.size()));
    for (const auto& row : t) {
        lpstr(b, row.first);
        u8(b, row.second);
    }
}

Bytes buildFile(const std::vector<Bytes>& recordBytes, unsigned version = 11,
                const Table& t1 = {}, const Table& t2 = {}, const Table& t3 = {}) {
    Bytes b;
    u32(b, 0xBEEFFEEDu); // on-disk ED FE EF BE
    u16(b, version);
    u16(b, static_cast<unsigned>(recordBytes.size()));
    appendTable(b, t1);
    appendTable(b, t2);
    appendTable(b, t3);
    for (const Bytes& r : recordBytes) b.insert(b.end(), r.begin(), r.end());
    return b;
}

sr3asm::AsmManifest parse(const Bytes& b) {
    return sr3asm::AsmManifest::parse(sr3asm::ByteView(b.data(), b.size()));
}

template <typename F>
bool throwsFormatError(F&& f) {
    try {
        f();
    } catch (const sr3asm::FormatError&) {
        return true;
    }
    return false;
}

// Each numbered block below runs inside run(), so an exception the reader
// should not have thrown (e.g. a mutated reader mis-parsing a fixture) is
// reported as a failure of that block instead of terminating the program and
// hiding every later block.
template <typename F>
void run(int line, F&& f) {
    try {
        f();
    } catch (const std::exception& ex) {
        std::cerr << "UNEXPECTED EXCEPTION in the block at line " << line << ": " << ex.what() << "\n";
        ++g_failures;
    }
}
#define RUN run(__LINE__, [&]

} // namespace

int main() {
    // ---------------------------------------------------------------
    // 1. Spec 7.5 worked example, byte for byte. The hex groups below are
    // the spec's own (decals.vpp_pc, first record).
    //   would catch: a u8 entry_count / missing source_name / missing extra_len
    //   / size table read after the entries / a 12- or 14-byte entry tail --
    //   any of them misplaces the entry name or the sizes.
    // ---------------------------------------------------------------
    RUN {
        Bytes rec;
        auto hex = [&](std::initializer_list<unsigned> bytes) {
            for (unsigned v : bytes) rec.push_back(static_cast<uint8_t>(v));
        };
        hex({0x11, 0x00});
        const std::string n1 = "decal_bullet_wood";
        rec.insert(rec.end(), n1.begin(), n1.end());
        hex({0x1a});
        hex({0x80, 0x00});
        hex({0x01, 0x00});
        hex({0x00, 0x18, 0x00, 0x00});
        hex({0x00, 0x00});
        hex({0x00, 0x00, 0x00, 0x00});
        hex({0xf4, 0x00, 0x00, 0x00});
        hex({0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
        hex({0x1f, 0x00});
        const std::string n2 = "vfx_decal_bullet_wood.matlib_pc";
        CHECK(n2.size() == 0x1f);
        rec.insert(rec.end(), n2.begin(), n2.end());
        hex({0x23, 0x00, 0x00, 0x00});
        hex({0x00, 0x03, 0x00, 0x00});
        hex({0x00, 0x00, 0x00, 0x00});
        hex({0x00});

        // Spec 7.3: 22 bytes lie between the count and the entry's name length
        // (4 + 2 + 4 + 4 fixed bytes plus the 8-byte size table).
        CHECK(rec.size() == 2 + 17 + 1 + 2 + 2 + 22 + 2 + 31 + 13);

        Bytes file = buildFile({rec});
        sr3asm::AsmManifest m = parse(file);
        CHECK(m.version() == 11);
        CHECK(m.declaredRecordCount() == 1);
        CHECK(m.records().size() == 1);
        CHECK(m.bytesConsumed() == file.size());
        CHECK(m.trailingBytes() == 0);
        const sr3asm::ContainerRecord& r = m.records()[0];
        CHECK(r.name == "decal_bullet_wood");
        CHECK(r.containerKind == 26);
        CHECK(r.recordFlags == 0x0080);
        CHECK(r.sizeHintsLive());
        CHECK(r.entryCount == 1);
        CHECK(r.headerRegionSize == 6144);
        CHECK(r.sourceName.empty());
        CHECK(r.extra.empty());
        CHECK(r.payloadLength == 244);
        CHECK(r.sizeTable.size() == 1);
        CHECK(r.sizeTable[0].primary == 768 && r.sizeTable[0].secondary == 0);
        CHECK(r.entries.size() == 1);
        const sr3asm::ManifestEntry& e = r.entries[0];
        CHECK(e.name == "vfx_decal_bullet_wood.matlib_pc");
        CHECK(e.typeId == 35);
        CHECK(e.poolId == 0);
        CHECK(e.entryFlags == 0);
        CHECK(!e.paired());
        CHECK(e.variantSelect == 0);
        CHECK(e.primarySize == 768);
        CHECK(e.secondarySize == 0);
        CHECK(e.allocGroup == 0);
    });

    // ---------------------------------------------------------------
    // 2. Zero-entry record: 19 fixed bytes after `name` (spec 7.3:
    // 1 + 2 + 2 + 4 + 2 + 4 + 4), header_region_size 0x0800 for an empty
    // container. would catch: any width error in the fixed part.
    // ---------------------------------------------------------------
    RUN {
        R r;
        r.name = "Trafficking";
        r.kind = 35;
        r.headerRegion = 0x0800;
        Bytes rec = buildRecord(r);
        CHECK(rec.size() == 2 + r.name.size() + sr3asm::kRecordTailBytes);
        static_assert(sr3asm::kRecordTailBytes == 19, "spec 7.3: 1+2+2+4+2+4+4");
        Bytes file = buildFile({rec, rec});
        sr3asm::AsmManifest m = parse(file);
        CHECK(m.records().size() == 2);
        CHECK(m.records()[0].entryCount == 0 && m.records()[0].entries.empty());
        CHECK(m.records()[0].sizeTable.empty());
        CHECK(m.records()[1].name == "Trafficking");
        CHECK(m.records()[1].headerRegionSize == 0x0800);
        CHECK(m.trailingBytes() == 0);
    });

    // ---------------------------------------------------------------
    // 3. entry_count is s16 and can exceed 255 (spec 7.3: max 3,820; 491
    // shipped records exceed 255). would catch: a u8 count (a u8 reader reads
    // 300 as 44 and then mis-aligns everything after it).
    // ---------------------------------------------------------------
    RUN {
        for (unsigned n : {255u, 256u, 300u, 3820u}) {
            R r;
            r.name = "big";
            r.kind = 30;
            r.recordFlags = 0x0080;
            for (unsigned i = 0; i < n; ++i) {
                E e;
                e.name = "f" + std::to_string(i) + ".czn_pc";
                e.type = 29;
                e.flags = 4;
                e.primary = 1000 + i;
                e.secondary = 2000 + i;
                e.group = 255;
                r.entries.push_back(e);
            }
            R after;
            after.name = "after";
            Bytes file = buildFile({buildRecord(r), buildRecord(after)});
            sr3asm::AsmManifest m = parse(file);
            CHECK(m.records().size() == 2);
            CHECK(m.records()[0].entryCount == static_cast<int16_t>(n));
            CHECK(m.records()[0].entries.size() == n);
            CHECK(m.records()[0].entries.back().primarySize == 1000 + n - 1);
            CHECK(m.records()[0].entries.back().secondarySize == 2000 + n - 1);
            CHECK(m.records()[1].name == "after"); // the record after the big one was found at the right offset
            CHECK(m.trailingBytes() == 0);
            CHECK(m.totalEntries() == n);
        }
    });

    // ---------------------------------------------------------------
    // 4. Size table precedes all names and is read from its own position
    // (spec 7.3, 7.5). The table values deliberately differ from the entries'
    // trailer values so a reader that took sizes from the wrong place shows.
    // would catch: interleaved (size,name) sub-blocks; size table after the
    // entries; size table skipped.
    // ---------------------------------------------------------------
    RUN {
        R r;
        r.name = "two";
        r.kind = 1;
        r.payloadLength = 4242;
        r.entries = {
            {"a.ccar_pc", 1, 8, 4 | 0, 0, 111, 222, 0},
            {"b.cvbm_pc", 16, 30, 4 | 0x40, 1, 333, 444, 255},
        };
        r.sizeTableOverride = {{9001, 9002}, {9003, 9004}};
        Bytes file = buildFile({buildRecord(r)});
        sr3asm::AsmManifest m = parse(file);
        const sr3asm::ContainerRecord& rec = m.records().at(0);
        CHECK(rec.sizeTable.size() == 2);
        CHECK(rec.sizeTable[0].primary == 9001 && rec.sizeTable[0].secondary == 9002);
        CHECK(rec.sizeTable[1].primary == 9003 && rec.sizeTable[1].secondary == 9004);
        CHECK(rec.entries.size() == 2);
        CHECK(rec.entries[0].name == "a.ccar_pc");
        CHECK(rec.entries[0].typeId == 1 && rec.entries[0].poolId == 8);
        CHECK(rec.entries[0].paired());
        CHECK(rec.entries[0].primarySize == 111 && rec.entries[0].secondarySize == 222);
        CHECK(rec.entries[1].name == "b.cvbm_pc");
        CHECK(rec.entries[1].typeId == 16 && rec.entries[1].poolId == 30);
        CHECK(rec.entries[1].entryFlags == 0x44);
        CHECK(rec.entries[1].variantSelect == 1);
        CHECK(rec.entries[1].allocGroup == 255);
        CHECK(rec.entries[1].primarySize == 333 && rec.entries[1].secondarySize == 444);
        CHECK(rec.payloadLength == 4242);
        CHECK(m.trailingBytes() == 0);
    });

    // ---------------------------------------------------------------
    // 5. Non-empty source_name (spec 7.3, version >= 10) and non-zero
    // extra blob (spec 7.3: u32 length + bytes; never occurs in shipped data,
    // width from code only). would catch: a reader that skips source_name or
    // extra (the spec 9.2 control notes the extra-skip control is
    // uninformative on the population; THIS fixture is where it is exercised).
    // ---------------------------------------------------------------
    RUN {
        R r;
        r.name = "1018";
        r.kind = 29;
        r.source = "1018.asm_pc";
        r.extra = {0xDE, 0xAD, 0xBE};
        r.entries = {{"1018_a.czn_pc", 29, 40, 4, 0, 5, 6, 5}};
        R r2;
        r2.name = "next";
        Bytes file = buildFile({buildRecord(r), buildRecord(r2)});
        sr3asm::AsmManifest m = parse(file);
        CHECK(m.records().size() == 2);
        CHECK(m.records()[0].sourceName == "1018.asm_pc");
        CHECK(m.records()[0].extra == (Bytes{0xDE, 0xAD, 0xBE}));
        CHECK(m.records()[0].entries.size() == 1);
        CHECK(m.records()[0].entries[0].name == "1018_a.czn_pc");
        CHECK(m.records()[0].entries[0].allocGroup == 5);
        CHECK(m.records()[1].name == "next");
        CHECK(m.trailingBytes() == 0);
    });

    // ---------------------------------------------------------------
    // 6. record_flags values seen in the population (spec 9.3: 0x0080,
    // 0x0300, 0x0200, 0x0000) and the "size hints" gate; a record without
    // 0x0080 carries zero header_region_size / payload_length but STILL has
    // its size table (present when version >= 11, spec 7.3).
    // would catch: reading the size table only when 0x0080 is set.
    // ---------------------------------------------------------------
    RUN {
        R live;
        live.name = "live";
        live.recordFlags = 0x0080;
        R fx;
        fx.name = "fx";
        fx.kind = 25;
        fx.recordFlags = 0x0300;
        fx.headerRegion = 0;
        fx.payloadLength = 0;
        fx.entries = {{"x.cefct_pc", 4, 0, 4, 0, 10, 20, 0}};
        R plain;
        plain.name = "plain";
        plain.kind = 31;
        plain.recordFlags = 0x0000;
        plain.headerRegion = 0;
        plain.payloadLength = 0;
        plain.entries = {{"y.czn_pc", 29, 0, 4, 0, 30, 40, 0}, {"z.czn_pc", 30, 0, 4, 0, 50, 60, 0}};
        R m200;
        m200.name = "m200";
        m200.kind = 39;
        m200.recordFlags = 0x0200;
        m200.headerRegion = 0;
        m200.entries = {{"buf", 39, 0, 0, 255, 0, 0, 255}};
        Bytes file = buildFile({buildRecord(live), buildRecord(fx), buildRecord(plain), buildRecord(m200)});
        sr3asm::AsmManifest m = parse(file);
        CHECK(m.records().size() == 4);
        CHECK(m.records()[0].sizeHintsLive());
        CHECK(!m.records()[1].sizeHintsLive());
        CHECK(m.records()[1].recordFlags == 0x0300);
        CHECK(!m.records()[2].sizeHintsLive());
        CHECK(m.records()[2].sizeTable.size() == 2);
        CHECK(m.records()[2].entries[1].name == "z.czn_pc");
        CHECK(m.records()[2].entries[1].secondarySize == 60);
        CHECK(m.records()[3].recordFlags == 0x0200);
        CHECK(m.records()[3].entries[0].variantSelect == 255); // spec 7.4: 0xFF only on type 39 (Buffer)
        CHECK(m.records()[3].entries[0].typeId == 39);
        CHECK(m.trailingBytes() == 0);
    });

    // ---------------------------------------------------------------
    // 7. The three tables (spec 7.2): u32 count then {lpstr, u8 id}; ids need
    // not be contiguous (spec 3), including the id-254 standalone row.
    // ---------------------------------------------------------------
    RUN {
        Table t1 = {{"Vehicle slots", 0}, {"Character slots", 1}, {"item preload", 27}, {"GSA_DESTUB_ALLOCATOR", 254}};
        Table t2 = {{"Vehicles", 1}, {"Vehicle PEG", 3}};
        Table t3 = {{"Vehicle", 1}};
        R r;
        r.name = "t";
        Bytes file = buildFile({buildRecord(r)}, 11, t1, t2, t3);
        sr3asm::AsmManifest m = parse(file);
        CHECK(m.poolTable().entries.size() == 4);
        CHECK(m.typeTable().entries.size() == 2);
        CHECK(m.kindTable().entries.size() == 1);
        CHECK(m.fixedTables()[0].entries[3].id == 254);
        CHECK(m.fixedTables()[0].entries[3].name == "GSA_DESTUB_ALLOCATOR");
        CHECK(m.typeTable().findName(3) != nullptr && *m.typeTable().findName(3) == "Vehicle PEG");
        CHECK(m.typeTable().findName(2) == nullptr);
        CHECK(m.kindTable().findName(1) != nullptr && *m.kindTable().findName(1) == "Vehicle");
        CHECK(m.records().size() == 1 && m.records()[0].name == "t");
        CHECK(m.trailingBytes() == 0);
    });

    // ---------------------------------------------------------------
    // 8. Empty manifest: header + three empty tables = 8 + 12 = 20 bytes
    // (spec 7.1 calls the shipped zero-record files "header + tables only").
    // ---------------------------------------------------------------
    RUN {
        Bytes file = buildFile({});
        CHECK(file.size() == 20);
        sr3asm::AsmManifest m = parse(file);
        CHECK(m.declaredRecordCount() == 0 && m.records().empty());
        CHECK(m.bytesConsumed() == 20 && m.trailingBytes() == 0);
    });

    // ---------------------------------------------------------------
    // 9. The exact-consumption gate can fail (spec 9.2's controls, applied
    // to the fixture): (a) appending bytes is reported as trailing bytes, not
    // swallowed; (b) EVERY strict prefix of a valid multi-record file throws
    // FormatError (never a crash, never a silent short parse); (c) an
    // inflated record_count throws instead of yielding fewer records.
    // ---------------------------------------------------------------
    RUN {
        R a;
        a.name = "alpha";
        a.kind = 29;
        a.source = "s.asm_pc";
        a.entries = {{"a1.czn_pc", 29, 40, 4, 0, 7, 8, 5}, {"", 39, 0, 0, 255, 0, 0, 255}};
        R b;
        b.name = "beta";
        b.kind = 1;
        Bytes file = buildFile({buildRecord(a), buildRecord(b)}, 11, {{"p", 1}}, {{"t", 2}}, {{"k", 3}});
        CHECK(parse(file).trailingBytes() == 0);

        Bytes plus = file;
        plus.push_back(0);
        sr3asm::AsmManifest mp = parse(plus);
        CHECK(mp.records().size() == 2);
        CHECK(mp.trailingBytes() == 1);
        CHECK(mp.bytesConsumed() == file.size());

        size_t prefixesThrowing = 0;
        for (size_t len = 0; len < file.size(); ++len) {
            Bytes cut(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(len));
            if (throwsFormatError([&] { parse(cut); })) ++prefixesThrowing;
        }
        CHECK(prefixesThrowing == file.size());

        Bytes inflated = file;
        inflated[6] = 3; // record_count 2 -> 3, low byte at offset 0x06
        CHECK(throwsFormatError([&] { parse(inflated); }));
    });

    // ---------------------------------------------------------------
    // 10. Rejections. Bad magic; versions other than 11 (spec 6.2 documents
    // other versions from code only, so this reader does not guess a layout
    // for them); a negative entry_count (spec 7.3 says signed, is silent on
    // negative values -- rejecting is this reader's policy, not the spec's);
    // counts that could never fit in the buffer must throw, not allocate.
    // ---------------------------------------------------------------
    RUN {
        R r;
        r.name = "x";
        Bytes good = buildFile({buildRecord(r)});
        CHECK(!throwsFormatError([&] { parse(good); }));

        Bytes badMagic = good;
        badMagic[0] ^= 0xFF;
        CHECK(throwsFormatError([&] { parse(badMagic); }));

        for (unsigned v : {0u, 7u, 8u, 10u, 12u}) {
            Bytes bad = buildFile({buildRecord(r)}, v);
            CHECK(throwsFormatError([&] { parse(bad); }));
        }

        R neg;
        neg.name = "neg";
        Bytes negRec = buildRecord(neg);
        // entry_count sits after name(2+3) + kind(1) + flags(2)
        negRec[2 + 3 + 1 + 2] = 0xFF;
        negRec[2 + 3 + 1 + 2 + 1] = 0xFF; // s16 -1
        CHECK(throwsFormatError([&] { parse(buildFile({negRec})); }));

        R huge;
        huge.name = "huge";
        Bytes hugeRec = buildRecord(huge);
        hugeRec[2 + 4 + 1 + 2] = 0xFF;
        hugeRec[2 + 4 + 1 + 2 + 1] = 0x7F; // count 32767 with no bytes behind it
        CHECK(throwsFormatError([&] { parse(buildFile({hugeRec})); }));

        Bytes hugeTable = good;
        hugeTable[8] = 0xFF; hugeTable[9] = 0xFF; hugeTable[10] = 0xFF; hugeTable[11] = 0xFF;
        CHECK(throwsFormatError([&] { parse(hugeTable); }));

        CHECK(throwsFormatError([&] { parse(Bytes{}); }));
        CHECK(throwsFormatError([&] { parse(Bytes{0xED, 0xFE, 0xEF, 0xBE, 11, 0, 0}); }));
    });

    if (g_failures == 0) {
        std::cout << "All synthetic asm-format tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) FAILED.\n";
    return 1;
}
