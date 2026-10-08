// Synthetic tests for sr3locstr (spec-localization-format.md): the
// `.le_strings` parser (Sec2) and the key-hash lookup (Sec4/Sec6.1). No
// game data - every file here is hand-built bytes.
#include "sr3locstr/le_strings.h"

#include <iostream>

using namespace sr3locstr;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

void putU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
}
void putU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

// Builds one well-formed `.le_strings` file (Sec2) from (hash, text) pairs,
// with the given bucket count (must be a power of two, per Sec2.1 - this
// builder does not check that, same as the real loader).
std::vector<uint8_t> buildTable(uint16_t bucketCount, const std::vector<std::pair<uint32_t, std::u16string>>& recs) {
    // Bucket assignment first, preserving insertion order within a bucket.
    std::vector<std::vector<size_t>> buckets(bucketCount);
    for (size_t i = 0; i < recs.size(); ++i) buckets[recs[i].first & (bucketCount - 1)].push_back(i);

    std::vector<uint8_t> file;
    putU32(file, 0xa84c7f73u); // magic
    putU16(file, 1);           // version
    putU16(file, bucketCount);
    putU32(file, static_cast<uint32_t>(recs.size()));

    // Placeholder bucket table (8 bytes/entry), patched after we know offsets.
    const size_t bucketTableOff = file.size();
    for (uint16_t b = 0; b < bucketCount; ++b) { putU32(file, 0); putU32(file, 0xffffffffu); }

    // Record-offset arrays, back to back in bucket order.
    std::vector<size_t> arrayOffForBucket(bucketCount, 0);
    for (uint16_t b = 0; b < bucketCount; ++b) {
        if (buckets[b].empty()) continue;
        arrayOffForBucket[b] = file.size();
        for (size_t i = 0; i < buckets[b].size(); ++i) putU32(file, 0); // patched below
    }

    // Records, then patch the arrays and bucket table with real offsets.
    std::vector<size_t> recordOff(recs.size(), 0);
    for (size_t i = 0; i < recs.size(); ++i) {
        recordOff[i] = file.size();
        putU32(file, recs[i].first);
        for (char16_t c : recs[i].second) putU16(file, static_cast<uint16_t>(c));
        putU16(file, 0);
    }
    for (uint16_t b = 0; b < bucketCount; ++b) {
        if (buckets[b].empty()) continue;
        size_t p = arrayOffForBucket[b];
        for (size_t idx : buckets[b]) {
            const uint32_t off = static_cast<uint32_t>(recordOff[idx]);
            file[p + 0] = static_cast<uint8_t>(off & 0xff);
            file[p + 1] = static_cast<uint8_t>((off >> 8) & 0xff);
            file[p + 2] = static_cast<uint8_t>((off >> 16) & 0xff);
            file[p + 3] = static_cast<uint8_t>((off >> 24) & 0xff);
            p += 4;
        }
        const size_t entryOff = bucketTableOff + 8 * b;
        const uint32_t count = static_cast<uint32_t>(buckets[b].size());
        const uint32_t arrOff = static_cast<uint32_t>(arrayOffForBucket[b]);
        file[entryOff + 0] = static_cast<uint8_t>(count & 0xff);
        file[entryOff + 1] = static_cast<uint8_t>((count >> 8) & 0xff);
        file[entryOff + 2] = static_cast<uint8_t>((count >> 16) & 0xff);
        file[entryOff + 3] = static_cast<uint8_t>((count >> 24) & 0xff);
        file[entryOff + 4] = static_cast<uint8_t>(arrOff & 0xff);
        file[entryOff + 5] = static_cast<uint8_t>((arrOff >> 8) & 0xff);
        file[entryOff + 6] = static_cast<uint8_t>((arrOff >> 16) & 0xff);
        file[entryOff + 7] = static_cast<uint8_t>((arrOff >> 24) & 0xff);
    }
    return file;
}

std::u16string u16(const char* s) {
    std::u16string out;
    while (*s) out.push_back(static_cast<char16_t>(static_cast<unsigned char>(*s++)));
    return out;
}

} // namespace

int main() {
    // --- Sec4: the key hash. Known answer from the spec's own worked
    // example (CONTROL_OKAY -> OK, static, hash 0xda87172c), plus
    // case-insensitivity (same section).
    {
        CHECK(keyHash("CONTROL_OKAY") == 0xda87172cu);
        CHECK(keyHash("control_okay") == keyHash("CONTROL_OKAY"));
        CHECK(keyHash("") == 0); // Sec4: "the empty string hashes to 0"
    }

    // --- Sec2: parse a small well-formed file and read it back.
    {
        auto bytes = buildTable(32, {{keyHash("HELLO"), u16("Hi there")}, {keyHash("BYE"), u16("Goodbye")}});
        LeStringsTable t = parseLeStrings(bytes);
        CHECK(t.bucketCount == 32);
        CHECK(t.recordCount == 2);
        CHECK(t.byHash.size() == 2);
        CHECK(t.byHash.at(keyHash("HELLO")) == u16("Hi there"));
        CHECK(t.byHash.at(keyHash("BYE")) == u16("Goodbye"));
    }

    // --- Sec2.2: an empty bucket (count 0, offset 0xffffffff) is skipped
    // cleanly, including when it is the ONLY bucket with no records at all.
    {
        auto bytes = buildTable(64, {});
        LeStringsTable t = parseLeStrings(bytes);
        CHECK(t.byHash.empty());
        CHECK(t.recordCount == 0);
    }

    // --- Several records landing in the same bucket (collision within one
    // bucket - exercises the linear-scan-within-bucket path, Sec2.2/Sec6.1).
    {
        // bucketCount 4 -> mask 3. Pick hashes that collide on the low 2 bits.
        const uint32_t h1 = 0x00000001u, h2 = 0x00000005u, h3 = 0x00000009u; // all & 3 == 1
        auto bytes = buildTable(4, {{h1, u16("one")}, {h2, u16("two")}, {h3, u16("three")}});
        LeStringsTable t = parseLeStrings(bytes);
        CHECK(t.byHash.size() == 3);
        CHECK(t.byHash.at(h1) == u16("one"));
        CHECK(t.byHash.at(h2) == u16("two"));
        CHECK(t.byHash.at(h3) == u16("three"));
    }

    // --- Malformed files: bad magic, bad version, truncated header.
    {
        auto bytes = buildTable(32, {{1, u16("x")}});
        bytes[0] = 0x00; // corrupt the magic
        bool threw = false;
        try { parseLeStrings(bytes); } catch (const LeStringsFormatError&) { threw = true; }
        CHECK(threw);
    }
    {
        auto bytes = buildTable(32, {{1, u16("x")}});
        bytes[4] = 2; bytes[5] = 0; // version 2
        bool threw = false;
        try { parseLeStrings(bytes); } catch (const LeStringsFormatError&) { threw = true; }
        CHECK(threw);
    }
    {
        std::vector<uint8_t> tooShort = {0x73, 0x7f, 0x4c, 0xa8, 0x01, 0x00};
        bool threw = false;
        try { parseLeStrings(tooShort); } catch (const LeStringsFormatError&) { threw = true; }
        CHECK(threw);
    }

    // --- Sec6.1: lookup, hash 0, and multi-table search order.
    {
        LeStringsLookup lk;
        CHECK(lk.tableCount() == 0);
        CHECK(!lk.lookup(0).has_value()); // hash 0 never searches, always nullopt
        CHECK(!lk.lookup(0x12345678u).has_value()); // nothing loaded yet

        auto first = buildTable(32, {{keyHash("SHARED_KEY"), u16("from first table")},
                                      {keyHash("ONLY_FIRST"), u16("only in first")}});
        auto second = buildTable(32, {{keyHash("SHARED_KEY"), u16("from second table")},
                                       {keyHash("ONLY_SECOND"), u16("only in second")}});
        lk.addTable(parseLeStrings(first));
        lk.addTable(parseLeStrings(second));
        CHECK(lk.tableCount() == 2);

        // First-loaded table wins on a shared key (Sec6.1: "first match, in
        // the first slot that has it, wins").
        CHECK(lk.lookupByKey("SHARED_KEY") == u16("from first table"));
        CHECK(lk.lookupByKey("ONLY_FIRST") == u16("only in first"));
        CHECK(lk.lookupByKey("ONLY_SECOND") == u16("only in second"));
        // Case-insensitive lookup (Sec4).
        CHECK(lk.lookupByKey("shared_key") == u16("from first table"));
        // Not a key anywhere -> nullopt.
        CHECK(!lk.lookupByKey("NOT_A_REAL_KEY").has_value());
        // Empty key -> hash 0 -> nullopt without searching (Sec6.2 0x00849df0).
        CHECK(!lk.lookupByKey("").has_value());
    }

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_le_strings_test: all checks passed\n";
    return 0;
}
