// Synthetic round-trip test for the vpp/str2 reader.
//
// We have no access to real .vpp_pc/.str2_pc game files in this
// environment (and per the clean-room rule, no access to the game binary
// either) - only the spec document. So this test builds small synthetic
// archives *in memory*, from scratch, using nothing but the CONFIRMED /
// HIGH CONFIDENCE on-disk layout facts from spec-vpp-container.md (magic,
// version, chained block-quantized region layout, full directory entry
// field layout, filename table format, per-entry compression signal),
// then feeds them through the real vpp::Container reader and checks that
// recursive walking, filename resolution, and zlib decompression all
// round-trip correctly.
//
// This proves the *parser logic* is internally consistent and matches the
// spec's confirmed layout. It does NOT prove the parser matches the real
// game's files - that can only be verified against actual .vpp_pc data,
// which is out of scope for the clean team by design.

#include "alloc_guard.h"
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include <zlib.h>

#include "vpp/container.h"
#include "vpp/content_validation.h"
#include "vpp/hash.h"

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

void writeU32LE(std::vector<uint8_t>& blob, size_t off, uint32_t v) {
    blob[off + 0] = static_cast<uint8_t>(v & 0xFF);
    blob[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    blob[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    blob[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

std::vector<uint8_t> zlibCompress(const std::vector<uint8_t>& input) {
    uLongf boundLen = compressBound(static_cast<uLong>(input.size()));
    std::vector<uint8_t> out(boundLen);
    uLongf destLen = boundLen;
    int ret = compress2(out.data(), &destLen, input.data(),
                         static_cast<uLong>(input.size()), Z_BEST_COMPRESSION);
    if (ret != Z_OK) {
        throw std::runtime_error("test harness: compress2 failed");
    }
    out.resize(destLen);
    return out;
}

struct EntrySpec {
    std::string name;
    std::vector<uint8_t> payloadBytes; // actual on-disk bytes for this entry
    uint32_t uncompressedSize = 0;     // +0x0C
    bool compressed = false;           // false -> +0x10 = kRawSentinel; true -> +0x10 = payloadBytes.size()
};

EntrySpec rawEntry(std::string name, std::vector<uint8_t> bytes) {
    EntrySpec s;
    s.uncompressedSize = static_cast<uint32_t>(bytes.size());
    s.name = std::move(name);
    s.payloadBytes = std::move(bytes);
    s.compressed = false;
    return s;
}

EntrySpec compressedEntry(std::string name, const std::vector<uint8_t>& plaintext) {
    EntrySpec s;
    s.uncompressedSize = static_cast<uint32_t>(plaintext.size());
    s.name = std::move(name);
    s.payloadBytes = zlibCompress(plaintext);
    s.compressed = true;
    return s;
}

// Builds one container blob following exactly the confirmed layout from
// spec Sec1.1/Sec2: magic/version at 0x000/0x004, size fields at
// 0x154/0x158/0x15C/0x160, 24-byte directory entries (all 5 confirmed
// fields) at dir_base = 0x800, filename table at the *computed* name_base
// (NOT a fixed 0x1000 - that assumption was wrong and is exactly what the
// spec correction fixed), payload data at the computed payload_start.
//
// The directory +0x08 field is the offline packer's LOGICAL spacing:
// round_up(+0x0C, 0x800) per earlier entry. Physical placement follows the
// loader's slot accumulation (spec §2 +0x04 row, HANDOFF §9.78): when flags
// bit 0x1 is set a COMPRESSED entry's slot is round_up(its compressed size
// +0x10, 0x800), so physical != logical for every compressed entry after the
// first; when the bit is clear every slot is round_up(+0x0C, 0x800) and the
// two coincide. The last entry is unpadded (total size only).
std::vector<uint8_t> buildContainer(const std::vector<EntrySpec>& specs, uint32_t flags = 0) {
    uint32_t entryCount = static_cast<uint32_t>(specs.size());

    std::vector<uint8_t> nameTable;
    std::vector<uint32_t> nameOffsets;
    for (const auto& s : specs) {
        nameOffsets.push_back(static_cast<uint32_t>(nameTable.size()));
        nameTable.insert(nameTable.end(), s.name.begin(), s.name.end());
        nameTable.push_back(0);
    }
    uint32_t nameTableSize = static_cast<uint32_t>(nameTable.size());
    uint32_t dirTableSize = entryCount * 24;

    size_t dirBase = vpp::kDirectoryOffset;
    size_t nameBase = dirBase + vpp::roundUpBlock(dirTableSize);
    size_t payloadStart = nameBase + vpp::roundUpBlock(nameTableSize);

    std::vector<uint32_t> dataOffsets(specs.size());     // directory +0x08 (logical)
    std::vector<size_t> physOffsets(specs.size());       // real position, relative to payloadStart
    size_t logical = 0, physical = 0;
    for (size_t i = 0; i < specs.size(); ++i) {
        dataOffsets[i] = static_cast<uint32_t>(logical);
        physOffsets[i] = physical;
        logical += vpp::roundUpBlock(specs[i].uncompressedSize);
        const bool bySize = (flags & vpp::kSlotsByCompressedSizeFlag) != 0 && specs[i].compressed;
        physical += vpp::roundUpBlock(bySize ? specs[i].payloadBytes.size()
                                             : specs[i].uncompressedSize);
    }
    size_t endPhysical = 0;
    for (size_t i = 0; i < specs.size(); ++i) {
        endPhysical = std::max(endPhysical, physOffsets[i] + specs[i].payloadBytes.size());
    }
    size_t totalSize = payloadStart + endPhysical;

    std::vector<uint8_t> blob(totalSize, 0);

    writeU32LE(blob, 0x000, vpp::kMagic);
    writeU32LE(blob, 0x004, 6);
    writeU32LE(blob, 0x14C, flags);
    writeU32LE(blob, 0x150, 0);           // OPEN/UNKNOWN field, left zero
    writeU32LE(blob, 0x154, entryCount);
    writeU32LE(blob, 0x158, static_cast<uint32_t>(totalSize));
    writeU32LE(blob, 0x15C, dirTableSize);
    writeU32LE(blob, 0x160, nameTableSize);
    writeU32LE(blob, 0x164, 0);           // OPEN/UNKNOWN field, left zero
    writeU32LE(blob, 0x168, 0xFFFFFFFFu); // not read by the reader (see format.h); value irrelevant here

    for (uint32_t i = 0; i < entryCount; ++i) {
        size_t off = dirBase + i * 24;
        writeU32LE(blob, off + 0x00, nameOffsets[i]);
        writeU32LE(blob, off + 0x04, 0); // OPEN/UNKNOWN, always zero per spec
        writeU32LE(blob, off + 0x08, dataOffsets[i]);
        writeU32LE(blob, off + 0x0C, specs[i].uncompressedSize);
        writeU32LE(blob, off + 0x10,
                    specs[i].compressed ? static_cast<uint32_t>(specs[i].payloadBytes.size())
                                        : vpp::kRawSentinel);
        // +0x14 intentionally left zero (runtime-only per spec).
    }

    std::copy(nameTable.begin(), nameTable.end(), blob.begin() + nameBase);

    for (size_t i = 0; i < specs.size(); ++i) {
        size_t off = payloadStart + physOffsets[i];
        std::copy(specs[i].payloadBytes.begin(), specs[i].payloadBytes.end(),
                   blob.begin() + off);
    }

    return blob;
}

// Builds an N-entry MODE (b) container (flags bit 0x2, spec §3.2b / §3.3,
// HANDOFF §9.78): ONE zlib stream at payloadStart covers every entry's bytes
// back to back, and each entry's directory +0x08 is its position inside the
// DECOMPRESSED stream - the packed running sum of the earlier entries' +0x0C,
// with no padding. Each entry's +0x10 is bookkeeping only here (a placeholder;
// in real archives the per-entry values sum to the header's cached 0x168).
// `breakLayout` shifts entry 1's +0x08 so the packing invariant fails.
std::vector<uint8_t> buildSharedStreamArchive(const std::vector<std::string>& names,
                                              const std::vector<std::vector<uint8_t>>& plaintexts,
                                              uint32_t flags, bool breakLayout = false) {
    std::vector<uint8_t> combined;
    for (const auto& p : plaintexts) combined.insert(combined.end(), p.begin(), p.end());
    const std::vector<uint8_t> stream = zlibCompress(combined);

    std::vector<uint8_t> nameTable;
    std::vector<uint32_t> nameOffsets;
    for (const auto& n : names) {
        nameOffsets.push_back(static_cast<uint32_t>(nameTable.size()));
        nameTable.insert(nameTable.end(), n.begin(), n.end());
        nameTable.push_back(0);
    }
    const uint32_t nameTableSize = static_cast<uint32_t>(nameTable.size());
    const uint32_t entryCount = static_cast<uint32_t>(names.size());
    const uint32_t dirTableSize = entryCount * 24;
    const size_t dirBase = vpp::kDirectoryOffset;
    const size_t nameBase = dirBase + vpp::roundUpBlock(dirTableSize);
    const size_t payloadStart = nameBase + vpp::roundUpBlock(nameTableSize);
    const size_t totalSize = payloadStart + stream.size();

    std::vector<uint8_t> blob(totalSize, 0);
    writeU32LE(blob, 0x000, vpp::kMagic);
    writeU32LE(blob, 0x004, 6);
    writeU32LE(blob, 0x14C, flags);
    writeU32LE(blob, 0x150, 0);
    writeU32LE(blob, 0x154, entryCount);
    writeU32LE(blob, 0x158, static_cast<uint32_t>(totalSize));
    writeU32LE(blob, 0x15C, dirTableSize);
    writeU32LE(blob, 0x160, nameTableSize);
    writeU32LE(blob, 0x164, 0);
    writeU32LE(blob, 0x168, static_cast<uint32_t>(stream.size()));

    uint32_t run = 0;
    for (uint32_t i = 0; i < entryCount; ++i) {
        const size_t off = dirBase + i * 24;
        writeU32LE(blob, off + 0x00, nameOffsets[i]);
        writeU32LE(blob, off + 0x04, 0);
        writeU32LE(blob, off + 0x08, run + ((breakLayout && i == 1) ? 1u : 0u));
        writeU32LE(blob, off + 0x0C, static_cast<uint32_t>(plaintexts[i].size()));
        writeU32LE(blob, off + 0x10, i == 0 ? static_cast<uint32_t>(stream.size()) : 4u);
        run += static_cast<uint32_t>(plaintexts[i].size());
    }
    std::copy(nameTable.begin(), nameTable.end(), blob.begin() + nameBase);
    std::copy(stream.begin(), stream.end(), blob.begin() + payloadStart);
    return blob;
}

} // namespace

int main() {
    // --- Build a small tree, conceptually similar to decals.vpp_pc (spec
    // Sec5): a top-level container whose entries are themselves nested,
    // raw/uncompressed .str2_pc containers - plus one branch going a level
    // deeper, and zlib-compressed leaves to exercise the confirmed
    // per-entry compression fields (spec Sec2/Sec3.1) in isolation.
    const std::string plaintext1 =
        "Hello from leaf_compressed.xtbl - clean room synthetic payload #1.";
    std::vector<uint8_t> plainBytes1(plaintext1.begin(), plaintext1.end());
    std::vector<uint8_t> level1a =
        buildContainer({compressedEntry("leaf_compressed.xtbl", plainBytes1)});

    const std::string plaintext2 =
        "Hello from leaf2.txt, nested two levels deep - synthetic payload #2.";
    std::vector<uint8_t> plainBytes2(plaintext2.begin(), plaintext2.end());
    std::vector<uint8_t> level2 =
        buildContainer({compressedEntry("leaf2.txt", plainBytes2)});
    std::vector<uint8_t> level1b =
        buildContainer({rawEntry("level2.str2_pc", level2)});

    std::vector<uint8_t> root = buildContainer({
        rawEntry("level1_a.str2_pc", level1a),
        rawEntry("level1_b.str2_pc", level1b),
    });

    // --- Top-level parse ---
    vpp::Container r(vpp::ByteView(root.data(), root.size()));
    CHECK(r.header().version == 6);
    CHECK(r.header().entryCount == 2);
    CHECK(r.header().totalSize == root.size());
    CHECK(r.header().payloadStart() < root.size());
    CHECK(r.entries().size() == 2);
    CHECK(r.entries()[0].name == "level1_a.str2_pc");
    CHECK(r.entries()[1].name == "level1_b.str2_pc");
    CHECK(r.entries()[0].payload.kind == vpp::PayloadKind::Raw);
    CHECK(r.entries()[1].payload.kind == vpp::PayloadKind::Raw);

    // --- Recursive open + decompress, branch A ---
    vpp::Container a = r.openNested(0);
    CHECK(a.header().entryCount == 1);
    CHECK(a.entries().size() == 1);
    CHECK(a.entries()[0].name == "leaf_compressed.xtbl");
    CHECK(a.entries()[0].payload.kind == vpp::PayloadKind::Compressed);
    {
        vpp::DecompressResult res = a.decompressEntry(0);
        CHECK(res.status == vpp::DecodeStatus::Ok);
        std::string got(res.data.begin(), res.data.end());
        CHECK(got == plaintext1);
    }

    // --- Recursive open, branch B, two levels deep ---
    vpp::Container b = r.openNested(1);
    CHECK(b.entries().size() == 1);
    CHECK(b.entries()[0].name == "level2.str2_pc");
    CHECK(b.entries()[0].payload.kind == vpp::PayloadKind::Raw);

    vpp::Container lvl2 = b.openNested(0);
    CHECK(lvl2.entries().size() == 1);
    CHECK(lvl2.entries()[0].name == "leaf2.txt");
    CHECK(lvl2.entries()[0].payload.kind == vpp::PayloadKind::Compressed);
    {
        vpp::DecompressResult res = lvl2.decompressEntry(0);
        CHECK(res.status == vpp::DecodeStatus::Ok);
        std::string got(res.data.begin(), res.data.end());
        CHECK(got == plaintext2);
    }

    // --- Filename hash: case-insensitivity per spec Sec2.2 pseudocode ---
    CHECK(r.entries()[0].nameHash == vpp::hashFilename("level1_a.str2_pc"));
    CHECK(r.entries()[0].nameHash == vpp::hashFilename("LEVEL1_A.STR2_PC"));
    CHECK(vpp::hashFilename("a") != vpp::hashFilename("b"));

    // --- A large directory table (> 85 entries) must push the filename
    // table past the old, now-known-wrong fixed 0x1000 assumption - this
    // is exactly the bug the spec correction called out (a 110-entry real
    // sample had its filename table start at 0x1800, not 0x1000). Build
    // 90 tiny raw leaf entries and confirm names/offsets still resolve.
    {
        std::vector<EntrySpec> many;
        for (int i = 0; i < 90; ++i) {
            std::vector<uint8_t> tiny{static_cast<uint8_t>(i)};
            many.push_back(rawEntry("leaf_" + std::to_string(i) + ".bin", tiny));
        }
        std::vector<uint8_t> bigBlob = buildContainer(many);
        vpp::Container big(vpp::ByteView(bigBlob.data(), bigBlob.size()));
        CHECK(big.header().entryCount == 90);
        // dirTableSize = 90*24 = 2160 > 0x800 (2048), so name_base must be
        // pushed to the next block (0x800 + round_up(2160,0x800) = 0x800 +
        // 0x1000 = 0x1800), NOT the old fixed 0x1000.
        CHECK(big.header().nameBase() == 0x1800);
        CHECK(big.entries()[0].name == "leaf_0.bin");
        CHECK(big.entries()[89].name == "leaf_89.bin");
        CHECK(big.entries()[47].payload.kind == vpp::PayloadKind::Raw);
    }

    // --- MODE (a) physical offsets (HANDOFF §9.78). A multi-entry container
    // with flags 0x4801 (bit 0x2 clear = mode (a), bit 0x1 set): the streams
    // are laid end to end, each slot rounded up by its COMPRESSED size, while
    // the directory +0x08 field is the packer's logical spacing by
    // UNCOMPRESSED size. Built so the two disagree for every entry after the
    // first (large, poorly-compressible payloads keep the compressed size
    // well below the uncompressed size). The old reader decoded at +0x08,
    // which lands inside some other entry's stream: the output was capped at
    // +0x0C so it "succeeded" with the right length and the wrong bytes.
    // Every entry must now come back Ok and byte-exact.
    {
        std::vector<std::vector<uint8_t>> plain;
        for (int k = 0; k < 4; ++k) {
            std::vector<uint8_t> p;
            // ~6 KB of repetitive-but-distinct text per entry: compresses to
            // a few hundred bytes, so slots differ hugely from +0x0C spacing.
            for (int rep = 0; rep < 150; ++rep) {
                const std::string line = "entry" + std::to_string(k) + " row " + std::to_string(rep) +
                                         " <Value>" + std::to_string(rep * (k + 3)) + "</Value>\r\n";
                p.insert(p.end(), line.begin(), line.end());
            }
            plain.push_back(std::move(p));
        }
        std::vector<EntrySpec> specs;
        for (int k = 0; k < 4; ++k) {
            specs.push_back(compressedEntry("entry" + std::to_string(k) + ".xtbl", plain[k]));
        }
        std::vector<uint8_t> archive = buildContainer(specs, 0x4801u);

        vpp::Container c(vpp::ByteView(archive.data(), archive.size()));
        CHECK(!c.header().isSharedStreamMode());
        CHECK(c.entries().size() == 4);
        bool physicalDiffersFromLogical = false;
        for (size_t i = 0; i < 4; ++i) {
            const auto& loc = c.entries()[i].payload;
            if (i > 0 && loc.offset != c.header().payloadStart() + loc.logicalOffset) {
                physicalDiffersFromLogical = true;
            }
            vpp::DecompressResult dr = c.decompressEntry(i);
            CHECK(dr.status == vpp::DecodeStatus::Ok);
            CHECK(dr.data == plain[i]);
        }
        // Guards the fixture itself: if physical == logical here the test
        // could not tell the old rule from the new one.
        CHECK(physicalDiffersFromLogical);

        // CONTROL: the same bytes read the OLD way (a stream at +0x08 with
        // output capped at +0x0C) must NOT reproduce entry 1 - it lands
        // inside another entry's stream. Proves the fixture separates the
        // rules, so the Ok/byte-exact checks above are not vacuous.
        {
            const auto& loc1 = c.entries()[1].payload;
            const size_t oldOffset = c.header().payloadStart() + loc1.logicalOffset;
            std::vector<uint8_t> out(loc1.decompressedLength);
            z_stream s{};
            inflateInit(&s);
            s.next_in = const_cast<Bytef*>(archive.data() + oldOffset);
            s.avail_in = static_cast<uInt>(archive.size() - oldOffset);
            s.next_out = out.data();
            s.avail_out = static_cast<uInt>(out.size());
            const int zr = inflate(&s, Z_NO_FLUSH);
            const bool producedAll = s.total_out == out.size();
            inflateEnd(&s);
            CHECK(!(producedAll && (zr == Z_OK || zr == Z_STREAM_END) && out == plain[1]));
        }
    }

    // --- Bit 0x1 clear (flags 0x4800): every slot is round_up(+0x0C), so the
    // physical position equals +0x08 and a plain multi-entry mode-(a)
    // container still decodes exactly, every entry Ok.
    {
        const std::string p0 = "Entry zero content.";
        const std::string p1 = "Entry one content.";
        const std::string p2 = "Entry two content.";
        std::vector<uint8_t> archive = buildContainer(
            {compressedEntry("entry0.bin", std::vector<uint8_t>(p0.begin(), p0.end())),
             compressedEntry("entry1.bin", std::vector<uint8_t>(p1.begin(), p1.end())),
             compressedEntry("entry2.bin", std::vector<uint8_t>(p2.begin(), p2.end()))},
            0x4800u);
        vpp::Container c(vpp::ByteView(archive.data(), archive.size()));
        CHECK(c.header().entryCount == 3);
        CHECK(!c.header().isSharedStreamMode());
        for (size_t i = 0; i < 3; ++i) {
            CHECK(c.entries()[i].payload.offset ==
                  c.header().payloadStart() + c.entries()[i].payload.logicalOffset);
        }
        const std::string* expect[3] = {&p0, &p1, &p2};
        for (size_t i = 0; i < 3; ++i) {
            vpp::DecompressResult dr = c.decompressEntry(i);
            CHECK(dr.status == vpp::DecodeStatus::Ok);
            CHECK(std::string(dr.data.begin(), dr.data.end()) == *expect[i]);
        }
    }

    // --- A single-entry container is unaffected.
    {
        const std::string soloPlain = "Solo entry - single-entry container.";
        std::vector<uint8_t> soloBytes(soloPlain.begin(), soloPlain.end());
        std::vector<uint8_t> archive = buildContainer({compressedEntry("solo.bin", soloBytes)});
        vpp::Container c(vpp::ByteView(archive.data(), archive.size()));
        vpp::DecompressResult soloRes = c.decompressEntry(0);
        CHECK(soloRes.status == vpp::DecodeStatus::Ok);
        CHECK(std::string(soloRes.data.begin(), soloRes.data.end()) == soloPlain);
    }

    // --- MODE (b) shared stream (flags bit 0x2, e.g. 0x4803): ONE zlib
    // stream at payloadStart covers every compressed entry; entry i is the
    // slice at its +0x08 (the packed running sum of the earlier +0x0C).
    // Three entries, none padded, decoded out of order and repeatedly (the
    // decoded prefix is cached and grown), plus a raw-only control.
    {
        const std::string sA = "Shared stream, entry A - the first slice.";
        const std::string sB = "Shared stream, entry B - the middle slice, a little longer than A.";
        const std::string sC = "Shared stream, entry C - the final slice.";
        std::vector<std::vector<uint8_t>> plain = {
            std::vector<uint8_t>(sA.begin(), sA.end()),
            std::vector<uint8_t>(sB.begin(), sB.end()),
            std::vector<uint8_t>(sC.begin(), sC.end())};
        std::vector<uint8_t> archive = buildSharedStreamArchive(
            {"shared_a.xtbl", "shared_b.xtbl", "shared_c.xtbl"}, plain, 0x4803u);

        vpp::Container c(vpp::ByteView(archive.data(), archive.size()));
        CHECK(c.header().isSharedStreamMode());
        CHECK(c.entries().size() == 3);
        for (size_t i = 0; i < 3; ++i) {
            CHECK(c.entries()[i].payload.kind == vpp::PayloadKind::Compressed);
            CHECK(c.entries()[i].payload.sharedStream);
            CHECK(c.entries()[i].payload.offset == c.header().payloadStart());
        }
        const size_t order[] = {2, 0, 1, 2, 1, 0};
        for (size_t i : order) {
            vpp::DecompressResult dr = c.decompressEntry(i);
            CHECK(dr.status == vpp::DecodeStatus::Ok);
            CHECK(dr.data == plain[i]);
        }
    }

    // --- A shared-stream container whose +0x08 offsets are NOT the packed
    // running sum cannot be sliced: reported, not guessed.
    {
        const std::string sA = "First entry of a broken shared-stream layout.";
        const std::string sB = "Second entry of a broken shared-stream layout.";
        std::vector<std::vector<uint8_t>> plain = {
            std::vector<uint8_t>(sA.begin(), sA.end()),
            std::vector<uint8_t>(sB.begin(), sB.end())};
        std::vector<uint8_t> archive = buildSharedStreamArchive(
            {"broken_a.xtbl", "broken_b.xtbl"}, plain, 0x4803u, /*breakLayout=*/true);
        vpp::Container c(vpp::ByteView(archive.data(), archive.size()));
        vpp::DecompressResult dr = c.decompressEntry(1);
        CHECK(dr.status == vpp::DecodeStatus::ZlibStreamError);
        CHECK(!dr.diagnostic.empty());
        CHECK(dr.data.empty());
    }

    // --- The flag is authoritative: the SAME shared-stream bytes with bit
    // 0x2 clear are a mode-(a) container, so the non-first entry is looked
    // for at its slot position - which holds no such stream. It must not
    // come back as Ok with the right content.
    {
        const std::string sA = "Flag-authority case, entry A of the shared stream.";
        const std::string sB = "Flag-authority case, entry B of the shared stream.";
        std::vector<std::vector<uint8_t>> plain = {
            std::vector<uint8_t>(sA.begin(), sA.end()),
            std::vector<uint8_t>(sB.begin(), sB.end())};
        std::vector<uint8_t> archive = buildSharedStreamArchive(
            {"auth_a.xtbl", "auth_b.xtbl"}, plain, 0u);
        vpp::Container c(vpp::ByteView(archive.data(), archive.size()));
        CHECK(!c.header().isSharedStreamMode());
        vpp::DecompressResult dr = c.decompressEntry(1);
        CHECK(!(dr.status == vpp::DecodeStatus::Ok && dr.data == plain[1]));
    }

    // --- No-name entry (spec Sec2/Sec3.3, CONFIRMED this pass): directory
    // field +0x00 == 0xFFFFFFFF means "no name" rather than a real
    // filename-table offset. An earlier revision of this parser would have
    // computed nameBase + 0xFFFFFFFF and thrown trying to read a filename
    // there - must instead parse cleanly with an empty name.
    {
        std::vector<EntrySpec> specs;
        specs.push_back(rawEntry("named_entry.bin", {1, 2, 3}));
        std::vector<uint8_t> archive = buildContainer(specs);
        // Patch entry 0's +0x00 field to the no-name sentinel after the
        // fact - buildContainer always assigns real name offsets, so this
        // is the simplest way to synthesize the edge case without a whole
        // separate builder.
        writeU32LE(archive, vpp::kDirectoryOffset + 0x00, vpp::kNoNameSentinel);

        vpp::Container c(vpp::ByteView(archive.data(), archive.size()));
        CHECK(c.entries().size() == 1);
        CHECK(c.entries()[0].name.empty());
        CHECK(c.entries()[0].nameHash == vpp::hashFilename(""));
        CHECK(c.entries()[0].payload.kind == vpp::PayloadKind::Raw);
    }

    // --- Regression test: a compressed entry whose on-disk data (and
    // confirmed +0x10 length) does NOT include a full, valid trailing
    // 4-byte Adler-32 checksum. Discovered by running this parser against
    // real game archives (test-fixtures/startup.vpp_pc, both entries):
    // zlib's inflate() reports Z_OK, not Z_STREAM_END, once it has
    // decoded all the real content but is still waiting on trailer bytes
    // this format doesn't reliably provide within the entry's confirmed
    // compressed-size field. decompressEntry must treat "produced ==
    // confirmed uncompressed-size (+0x0C)" as success regardless of the
    // formal Z_STREAM_END signal, or this regresses to incorrectly
    // reporting every such entry as a decode failure.
    {
        const std::string plaintext3 = "Regression payload for the missing-trailer case.";
        std::vector<uint8_t> plainBytes3(plaintext3.begin(), plaintext3.end());
        std::vector<uint8_t> fullCompressed = zlibCompress(plainBytes3);
        CHECK(fullCompressed.size() > 4);
        // Simulate the on-disk shape observed in real archives: the
        // stored bytes (and the length the directory entry claims) stop
        // right after the real deflate content, without the trailer.
        std::vector<uint8_t> truncated(fullCompressed.begin(), fullCompressed.end() - 4);

        EntrySpec spec;
        spec.name = "leaf_no_trailer.txt";
        spec.uncompressedSize = static_cast<uint32_t>(plainBytes3.size());
        spec.payloadBytes = truncated;
        spec.compressed = true;

        std::vector<uint8_t> archive = buildContainer({spec});
        vpp::Container c(vpp::ByteView(archive.data(), archive.size()));
        CHECK(c.entries()[0].payload.kind == vpp::PayloadKind::Compressed);
        vpp::DecompressResult res = c.decompressEntry(0);
        CHECK(res.status == vpp::DecodeStatus::Ok);
        std::string got(res.data.begin(), res.data.end());
        CHECK(got == plaintext3);
    }

    // --- Empty archive (entryCount == 0) should parse cleanly ---
    {
        std::vector<uint8_t> empty = buildContainer({});
        vpp::Container ec(vpp::ByteView(empty.data(), empty.size()));
        CHECK(ec.header().entryCount == 0);
        CHECK(ec.entries().empty());
    }

    // --- Negative test: corrupted magic must be rejected (spec Sec1,
    // CONFIRMED hard validation rule) ---
    {
        std::vector<uint8_t> bad = root;
        bad[0] ^= 0xFF;
        bool threw = false;
        try {
            vpp::Container broken(vpp::ByteView(bad.data(), bad.size()));
            (void)broken;
        } catch (const vpp::FormatError&) {
            threw = true;
        }
        CHECK(threw);
    }

    // --- Negative test: out-of-range version must be rejected ---
    {
        std::vector<uint8_t> bad = root;
        writeU32LE(bad, 0x004, 99);
        bool threw = false;
        try {
            vpp::Container broken(vpp::ByteView(bad.data(), bad.size()));
            (void)broken;
        } catch (const vpp::FormatError&) {
            threw = true;
        }
        CHECK(threw);
    }

    // --- Anomaly-reporting test (spec Sec3.2): a "compressed" entry whose
    // +0x10 field is a real (non-sentinel) value, but whose computed
    // offset does not actually contain a valid zlib header - this is
    // exactly the confirmed-offset-but-undecodable situation Sec3.2
    // documents. decompressEntry must report it via DecodeStatus, not
    // throw and not fabricate output.
    {
        std::vector<uint8_t> corrupt = level1a;
        vpp::Container tmp(vpp::ByteView(corrupt.data(), corrupt.size()));
        size_t payloadOff = tmp.header().payloadStart() + 0; // entry 0's data offset is 0
        // Stomp the zlib CMF/FLG header bytes so they no longer look valid.
        corrupt[payloadOff] = 0x00;
        corrupt[payloadOff + 1] = 0x00;
        vpp::Container broken(vpp::ByteView(corrupt.data(), corrupt.size()));
        vpp::DecompressResult res = broken.decompressEntry(0);
        CHECK(res.status == vpp::DecodeStatus::NoValidZlibHeaderAtOffset);
        CHECK(!res.diagnostic.empty());
        CHECK(res.data.empty());
    }

    // --- The per-entry raw sentinel (+0x10) OUTRANKS the container's
    // shared-stream flag (0x2). This is the exact shape of a real archive:
    // preload_anim.vpp_pc (test-fixtures/, 17MB) sets flags 0x2 yet stores
    // all 4,209 of its entries raw. The two signals are genuinely
    // independent - the flag describes stream LAYOUT for entries that are
    // compressed, it does NOT declare that any entry is compressed - so a
    // reader that checks the flag first and tries to inflate the payload
    // fails outright on this archive. (Team A hit exactly that bug in
    // their own tooling; see spec-vpp-container.md §5.9. This library was
    // already correct - PayloadLocator::locate decides kind solely from
    // the sentinel and never consults the header flag - but nothing
    // pinned that ordering down, so this test does.)
    {
        const std::string contentA = "Raw entry A in a flag-0x2 container.";
        const std::string contentB = "Raw entry B in the same container.";
        std::vector<uint8_t> archive = buildContainer(
            {rawEntry("anim_a.rig_pc", std::vector<uint8_t>(contentA.begin(), contentA.end())),
             rawEntry("anim_b.rig_pc", std::vector<uint8_t>(contentB.begin(), contentB.end()))});
        // Set the shared-stream flag while leaving both entries' +0x10 at
        // the raw sentinel - the real preload_anim.vpp_pc combination.
        writeU32LE(archive, 0x14C, vpp::kSharedStreamModeFlag);

        vpp::Container c(vpp::ByteView(archive.data(), archive.size()));
        CHECK(c.header().isSharedStreamMode()); // flag really is set...
        // ...and yet both entries must still be RAW, on the sentinel's say-so.
        CHECK(c.entries()[0].payload.kind == vpp::PayloadKind::Raw);
        CHECK(c.entries()[1].payload.kind == vpp::PayloadKind::Raw);

        // Raw content must come back byte-exact, with no inflate attempted.
        vpp::ByteView gotA = c.rawEntryBytes(0);
        CHECK(std::string(gotA.data(), gotA.data() + gotA.size()) == contentA);
        vpp::ByteView gotB = c.rawEntryBytes(1);
        CHECK(std::string(gotB.data(), gotB.data() + gotB.size()) == contentB);

        // And decompressEntry must refuse outright rather than trying to
        // inflate raw bytes just because the container flag is set.
        bool threwA = false;
        try {
            c.decompressEntry(0);
        } catch (const vpp::FormatError&) {
            threwA = true;
        }
        CHECK(threwA);
    }

    // --- Fuzz regression (fuzz/regressions/vpp, second reproducer): an entry
    // count far beyond what the buffer's 24-byte directory records can hold
    // must not be trusted for an up-front reservation.
    {
        const std::string text = "x";
        std::vector<uint8_t> blob = buildContainer({rawEntry("a.txt", std::vector<uint8_t>(text.begin(), text.end()))});
        writeU32LE(blob, 0x154, 0x03A00000u);      // entry count (spec Sec1.1 +0x154): ~61M
        writeU32LE(blob, 0x15C, 0x03A00000u * 24); // directory size kept consistent (== count x 24, checked by Header::parse)
        bool badAlloc = false, threw = false;
        {
            allocguard::AllocCap cap(256u << 20);
            try {
                vpp::Container c(vpp::ByteView(blob.data(), blob.size()));
            } catch (const std::bad_alloc&) {
                badAlloc = true;
            } catch (const std::exception&) {
                threw = true;
            }
        }
        CHECK(!badAlloc);
        CHECK(threw);
    }

    // --- Fuzz regression (fuzz/regressions/vpp): a compressed entry whose
    // declared +0x0C size no DEFLATE stream of its length could produce
    // (1032:1 is the format's maximum) is rejected before allocating it.
    {
        const std::string text = "tiny payload";
        EntrySpec e = compressedEntry("huge.bin", std::vector<uint8_t>(text.begin(), text.end()));
        e.uncompressedSize = 0xF0000000u; // ~3.75 GB claimed for a ~20-byte stream
        std::vector<uint8_t> blob = buildContainer({e});
        vpp::Container c(vpp::ByteView(blob.data(), blob.size()));
        bool badAlloc = false;
        vpp::DecompressResult dr;
        {
            allocguard::AllocCap cap(256u << 20);
            try {
                dr = c.decompressEntry(0);
            } catch (const std::bad_alloc&) {
                badAlloc = true;
            }
        }
        CHECK(!badAlloc);
        CHECK(dr.status == vpp::DecodeStatus::ZlibStreamError);
        CHECK(dr.data.empty());
        CHECK(!dr.diagnostic.empty());
    }

    if (g_failures == 0) {
        std::cout << "All synthetic archive tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) FAILED.\n";
    return 1;
}
