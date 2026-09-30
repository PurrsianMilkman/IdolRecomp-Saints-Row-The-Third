// Synthetic tests for the .cpeg_pc/.gpeg_pc texture-pair reader and its
// content validator, REWRITTEN against spec-texture-format.md Sec10-Sec12.
//
// Every fixture here is built from the SPEC TEXT, never from the reader's
// own source: field offsets come from the Sec10.2 header table and the
// Sec10.3 record table, and every expected number is either quoted from the
// spec (Sec5's worked example, Sec7's 800x324 UI texture, Sec10.5's cube
// shapes, Sec11.2's 240x240 texture, Sec12.3's 5,592,400-byte maximum) or
// worked out by hand from the Sec10.5/Sec11.1 formulas in the comments next
// to it - not obtained by calling the code under test and copying its
// answer. Several "CONTROL" cases deliberately break a rule (shifted
// offsets, level count off by one, missing clamp, swapped channels) and
// assert that the check DOES fail, so a green run cannot be a reader
// agreeing with itself.

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "sr3texture/content_validation.h"
#include "sr3texture/texture_pair.h"

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

void appendU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void patchU32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    b[at + 0] = static_cast<uint8_t>(v & 0xFF);
    b[at + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    b[at + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    b[at + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

struct SyntheticRecord {
    uint32_t dataOffset = 0;
    uint16_t width = 0, height = 0;
    uint32_t format = 0;
    uint16_t gridCols = 1, gridRows = 1;
    uint16_t group = 1;
    uint16_t flags = 0;
    uint8_t rate = 1;
    uint8_t levels = 1;
    uint32_t size = 0;
    std::string name; // written to the name table only when this record starts a group
    bool writeName = true;
};

struct BuildOptions {
    uint16_t version = 13;
    uint16_t platform = 0;
    uint32_t gSize = 0;
    int shiftWidthField = 0; // CONTROL: bytes of junk inserted before +0x08, pushing every later field
};

// Builds a .cpeg_pc exactly per Sec10.2 (24-byte header) / Sec10.3
// (72-byte record) / Sec10.4 (name table, one name per record group).
std::vector<uint8_t> buildCpeg(const std::vector<SyntheticRecord>& recs, const BuildOptions& opt = {}) {
    std::vector<uint8_t> blob;
    appendU32(blob, 0x564B4547u);                          // 0x00 magic "GEKV"
    appendU16(blob, opt.version);                          // 0x04 version
    appendU16(blob, opt.platform);                         // 0x06 platform index
    appendU32(blob, 0);                                    // 0x08 own size, patched below
    appendU32(blob, opt.gSize);                            // 0x0C g-file size
    appendU16(blob, static_cast<uint16_t>(recs.size()));   // 0x10 count copy 1
    appendU16(blob, 0);                                    // 0x12 runtime flag, zero on disk
    appendU16(blob, static_cast<uint16_t>(recs.size()));   // 0x14 THE count
    appendU16(blob, 16);                                   // 0x16 constant 16

    for (const auto& r : recs) {
        size_t start = blob.size();
        appendU32(blob, r.dataOffset);                     // +0x00
        appendU32(blob, 0);                                // +0x04 pointer-slot high half
        if (opt.shiftWidthField > 0) blob.insert(blob.end(), static_cast<size_t>(opt.shiftWidthField), 0xA5);
        appendU16(blob, r.width);                          // +0x08
        appendU16(blob, r.height);                         // +0x0A
        appendU32(blob, r.format);                         // +0x0C
        appendU16(blob, r.gridCols);                       // +0x10
        appendU16(blob, r.gridRows);                       // +0x12
        appendU16(blob, r.group);                          // +0x14
        appendU16(blob, r.flags);                          // +0x16
        blob.insert(blob.end(), 8, 0);                     // +0x18 name-pointer slot
        appendU16(blob, 0);                                // +0x20 merge-only field
        blob.push_back(r.rate);                            // +0x22 animation rate (1 byte)
        blob.push_back(r.levels);                          // +0x23 level count (1 byte)
        appendU32(blob, r.size);                           // +0x24 total size (4 bytes)
        blob.insert(blob.end(), 32, 0);                    // +0x28..+0x47 runtime storage
        if (opt.shiftWidthField == 0 && blob.size() - start != 72) {
            std::cerr << "buildCpeg: internal error, record size != 72\n";
            std::abort();
        }
        if (opt.shiftWidthField > 0) blob.resize(start + 72); // keep the stride at 72; only the field positions move
    }
    for (const auto& r : recs) {
        if (!r.writeName) continue;
        blob.insert(blob.end(), r.name.begin(), r.name.end());
        blob.push_back(0);
    }
    patchU32(blob, 0x08, static_cast<uint32_t>(blob.size()));
    return blob;
}

sr3texture::ByteView view(const std::vector<uint8_t>& v) { return sr3texture::ByteView(v.data(), v.size()); }

template <typename F>
bool throwsFormatError(F f) {
    try {
        f();
    } catch (const sr3texture::FormatError&) {
        return true;
    }
    return false;
}

template <typename T>
T opaque(T v) {
    volatile T x = v; // defeats constant folding so a CONTROL comparison is a real runtime check
    return x;
}

uint32_t align16(uint32_t v) { return (v + 15u) & ~15u; }

} // namespace

int main() {
    using sr3texture::computeLevelLayout;
    using sr3texture::LevelLayout;

    // ------------------------------------------------------------------
    // Sec5 worked example, with the level counts the Sec10.5 rule implies.
    // 256x256 DXT1 (400), 7 levels 256..4: 32768+8192+2048+512+128+32+8 =
    // 43,688; 128x128 DXT5 (402), 6 levels 128..4: 16384+4096+1024+256+64+16
    // = 21,840; the second offset is align16(43,688) = 43,696 (Sec5/Sec10.5).
    // ------------------------------------------------------------------
    {
        std::vector<SyntheticRecord> recs(2);
        recs[0] = {0, 256, 256, 400, 1, 1, 1, 0, 1, 7, 43688, "vfx_blast_decal_01_n.tga"};
        recs[1] = {43696, 128, 128, 402, 1, 1, 1, 0, 1, 6, 21840, "vfx_blast_decal_01.tga"};
        BuildOptions opt;
        opt.gSize = align16(43696 + 21840); // Sec10.5: the g length is the last region's end, rounded up to 16
        std::vector<uint8_t> blob = buildCpeg(recs, opt);
        sr3texture::TexturePair t = sr3texture::TexturePair::parse(view(blob));
        CHECK(t.version() == 13);
        CHECK(t.platformIndex() == 0);
        CHECK(t.records().size() == 2);
        CHECK(t.recordCountCopy() == 2);
        CHECK(t.headerConstant() == 16);
        CHECK(t.records()[0].name == "vfx_blast_decal_01_n.tga");
        CHECK(t.records()[0].width == 256 && t.records()[0].height == 256);
        CHECK(t.records()[0].pixelFormat == 400);
        CHECK(t.records()[0].levelCount == 7);
        CHECK(t.records()[0].compressedSize == 43688);
        CHECK(t.records()[0].gpegOffset == 0);
        CHECK(t.records()[1].name == "vfx_blast_decal_01.tga");
        CHECK(t.records()[1].pixelFormat == 402);
        CHECK(t.records()[1].levelCount == 6);
        CHECK(t.records()[1].compressedSize == 21840);
        CHECK(t.records()[1].gpegOffset == 43696);
        CHECK(align16(43688) == 43696);

        // The layout function must reproduce the spec's quoted totals.
        CHECK(t.levelLayout(0).totalBytes == 43688);
        CHECK(t.levelLayout(1).totalBytes == 21840);
        CHECK(t.levelLayout(0).levels.size() == 7);
        CHECK(t.levelLayout(0).levels[0].bytes == 32768);
        CHECK(t.levelLayout(0).levels[6].width == 4 && t.levelLayout(0).levels[6].height == 4);
        CHECK(t.levelLayout(0).levels[6].bytes == 8);
        CHECK(t.levelLayout(1).levels[0].bytes == 16384);
        CHECK(t.levelLayout(1).levels[5].bytes == 16);

        // CONTROL: level count off by one in either direction must NOT
        // reproduce the spec's total (Sec12.5 C3).
        LevelLayout l;
        std::string why;
        CHECK(computeLevelLayout(400, 256, 256, 8, false, l, why) && l.totalBytes != 43688); // 43,688 + 8 (an extra 4x4)
        CHECK(computeLevelLayout(400, 256, 256, 6, false, l, why) && l.totalBytes != 43688); // 43,688 - 8
        CHECK(computeLevelLayout(400, 256, 256, 6, false, l, why) && l.totalBytes == 43680);
    }

    // ------------------------------------------------------------------
    // 32-BIT size field (Sec10.3: 23,749 shipped records exceed 65,535;
    // maximum 5,592,400). The old reader took 16 bits.
    // ------------------------------------------------------------------
    {
        std::vector<SyntheticRecord> recs(2);
        recs[0] = {0, 2048, 2048, 402, 1, 1, 1, 0, 1, 10, 5592400, "big.tga"};
        recs[1] = {5592400, 256, 256, 402, 1, 1, 1, 0, 1, 7, 87376, "b.tga"};
        std::vector<uint8_t> blob = buildCpeg(recs);
        sr3texture::TexturePair t = sr3texture::TexturePair::parse(view(blob));
        CHECK(t.records()[0].compressedSize == 5592400);
        CHECK(t.records()[1].compressedSize == 87376);
        // CONTROL: the low 16 bits alone (what the old 2-byte reading saw)
        // are NOT the value.
        const uint32_t bigSize = opaque(5592400u);
        CHECK((bigSize & 0xFFFFu) != bigSize);
        CHECK((bigSize & 0xFFFFu) == bigSize - 85u * 65536u); // 0x555550 -> 0x5550 = 21840
        CHECK(t.records()[1].compressedSize > 65535);
        // 2048x2048 DXT5 (4,194,304 B base) with 10 levels 2048..4: the
        // geometric sum 4,194,304 * (1 + 1/4 + ... + 4^-9) = 5,592,400
        // exactly - the spec's Sec12.4 maximum record size.
        CHECK(t.levelLayout(0).levels.size() == 10);
        CHECK(t.levelLayout(0).totalBytes == 5592400);
        // 256x256 DXT5 with 7 levels 256..4: 65536+16384+4096+1024+256+64+16 = 87,376.
        CHECK(t.levelLayout(1).totalBytes == 87376);
    }

    // ------------------------------------------------------------------
    // Group rule (Sec10.4): one name per GROUP, group size = record +0x14.
    // ------------------------------------------------------------------
    {
        std::vector<SyntheticRecord> recs(3);
        recs[0] = {0, 64, 64, 400, 1, 1, 2, 0, 1, 1, 2048, "first.tga"};      // group of 2 starting here
        recs[1] = {2048, 64, 64, 400, 1, 1, 1, 0, 1, 1, 2048, ""};            // shares record 0's name
        recs[1].writeName = false;
        recs[2] = {4096, 32, 32, 400, 1, 1, 1, 0, 1, 1, 512, "second.tga"};
        std::vector<uint8_t> blob = buildCpeg(recs);
        sr3texture::TexturePair t = sr3texture::TexturePair::parse(view(blob));
        CHECK(t.records().size() == 3);
        CHECK(t.records()[0].name == "first.tga");
        CHECK(t.records()[1].name == "first.tga");
        CHECK(t.records()[2].name == "second.tga");
        CHECK(t.records()[0].groupCount == 2);

        // CONTROL: the same file interpreted one-name-per-record would run
        // out of names (3 records, 2 names) - so a per-record reader cannot
        // pass this test.
        // Group count 0 (no forward progress, Sec10.4) must be rejected.
        recs[0].group = 0;
        std::vector<uint8_t> bad = buildCpeg(recs);
        CHECK(throwsFormatError([&] { sr3texture::TexturePair::parse(view(bad)); }));
    }

    // ------------------------------------------------------------------
    // Header: version accepts only 13 (Sec10.2), platform is separate,
    // count is a SIGNED 16-bit at 0x14, own size must equal the file.
    // ------------------------------------------------------------------
    {
        SyntheticRecord r = {0, 8, 8, 400, 1, 1, 1, 0, 1, 1, 32, "a.tga"};
        BuildOptions o12;
        o12.version = 12;
        BuildOptions o14;
        o14.version = 14;
        BuildOptions oPlat;
        oPlat.platform = 1;
        CHECK(throwsFormatError([&] { sr3texture::TexturePair::parse(view(buildCpeg({r}, o12))); }));
        CHECK(throwsFormatError([&] { sr3texture::TexturePair::parse(view(buildCpeg({r}, o14))); }));
        std::vector<uint8_t> plat = buildCpeg({r}, oPlat);
        sr3texture::TexturePair t = sr3texture::TexturePair::parse(view(plat));
        CHECK(t.version() == 13 && t.platformIndex() == 1); // 0x04 and 0x06 are two separate u16s

        // Negative signed count: 0xFFFF at 0x14.
        std::vector<uint8_t> neg = buildCpeg({r});
        neg[0x14] = 0xFF;
        neg[0x15] = 0xFF;
        CHECK(throwsFormatError([&] { sr3texture::TexturePair::parse(view(neg)); }));

        // Count too large for the file (C8-style header-count-plus-one).
        std::vector<uint8_t> plusOne = buildCpeg({r});
        plusOne[0x14] = 2;
        CHECK(throwsFormatError([&] { sr3texture::TexturePair::parse(view(plusOne)); }));

        // Bad magic, own-size mismatch, truncated header.
        std::vector<uint8_t> badMagic = buildCpeg({r});
        badMagic[0] ^= 0xFF;
        CHECK(throwsFormatError([&] { sr3texture::TexturePair::parse(view(badMagic)); }));
        std::vector<uint8_t> strayByte = buildCpeg({r});
        strayByte.push_back(0);
        CHECK(throwsFormatError([&] { sr3texture::TexturePair::parse(view(strayByte)); }));
        std::vector<uint8_t> tiny = {0x47, 0x45, 0x4B, 0x56};
        CHECK(throwsFormatError([&] { sr3texture::TexturePair::parse(view(tiny)); }));

        // Name table not exactly filling the rest (extra byte, own size patched to match).
        std::vector<uint8_t> extra = buildCpeg({r});
        extra.push_back(0xAB);
        patchU32(extra, 0x08, static_cast<uint32_t>(extra.size()));
        CHECK(throwsFormatError([&] { sr3texture::TexturePair::parse(view(extra)); }));

        // Unterminated final name must be a FormatError, not an out_of_range escape.
        std::vector<uint8_t> unterminated = buildCpeg({r});
        unterminated.pop_back(); // drop the trailing NUL
        patchU32(unterminated, 0x08, static_cast<uint32_t>(unterminated.size()));
        CHECK(throwsFormatError([&] { sr3texture::TexturePair::parse(view(unterminated)); }));
    }

    // Zero-texture file (Sec12.2: 24 bytes, count 0) parses to an empty set.
    {
        std::vector<uint8_t> blob = buildCpeg({});
        CHECK(blob.size() == 24);
        sr3texture::TexturePair t = sr3texture::TexturePair::parse(view(blob));
        CHECK(t.records().empty());
    }

    // ------------------------------------------------------------------
    // CONTROL - shifted field offsets: with a junk block pushing the
    // width/height/format/size fields 2 bytes later, the values the test
    // asserts above must NOT come back. Proves the value checks can fail.
    // ------------------------------------------------------------------
    {
        SyntheticRecord r = {0, 256, 128, 402, 1, 1, 1, 0, 1, 6, 21840, "shift.tga"};
        BuildOptions shifted;
        shifted.shiftWidthField = 2;
        std::vector<uint8_t> blob = buildCpeg({r}, shifted);
        bool parsed = true;
        sr3texture::TexturePair t;
        try {
            t = sr3texture::TexturePair::parse(view(blob));
        } catch (const sr3texture::FormatError&) {
            parsed = false;
        }
        // Either parse rejects the mangled file, or the fields differ from the intended ones.
        CHECK(!parsed || !(t.records()[0].width == 256 && t.records()[0].height == 128 &&
                           t.records()[0].pixelFormat == 402 && t.records()[0].compressedSize == 21840));
    }

    // ------------------------------------------------------------------
    // Pixel-format table (Sec11.1).
    // ------------------------------------------------------------------
    {
        sr3texture::PixelFormatInfo info;
        CHECK(sr3texture::lookupPixelFormat(403, info) && info.blockEdge == 1 &&
              std::string(info.d3d9Name) == "D3DFMT_R5G6B5" && info.shipped);
        CHECK(sr3texture::lookupPixelFormat(400, info) && info.blockEdge == 4 && std::string(info.d3d9Name) == "D3DFMT_DXT1");
        CHECK(sr3texture::lookupPixelFormat(401, info) && info.blockEdge == 4 && std::string(info.d3d9Name) == "D3DFMT_DXT3");
        CHECK(sr3texture::lookupPixelFormat(402, info) && info.blockEdge == 4 && std::string(info.d3d9Name) == "D3DFMT_DXT5");
        CHECK(sr3texture::lookupPixelFormat(701, info) && info.blockEdge == 4 && std::string(info.d3d9Name) == "D3DFMT_DXT5" && !info.shipped);
        CHECK(sr3texture::lookupPixelFormat(404, info) && !info.shipped && info.blockEdge == 1);
        CHECK(sr3texture::lookupPixelFormat(410, info) && info.shipped && std::string(info.d3d9Name) == "D3DFMT_A8");
        // Codes the game's switch rejects (Sec11.1): neighbours of the range and 603.
        CHECK(!sr3texture::lookupPixelFormat(399, info));
        CHECK(!sr3texture::lookupPixelFormat(411, info));
        CHECK(!sr3texture::lookupPixelFormat(603, info));
        CHECK(!sr3texture::lookupPixelFormat(0, info));

        // Row-byte column of Sec11.1, hand-evaluated.
        CHECK(sr3texture::sourceRowBytes(400, 256) == 512);   // (256>>1)*4
        CHECK(sr3texture::sourceRowBytes(400, 4) == 8);       // one 4x4 DXT1 block
        CHECK(sr3texture::sourceRowBytes(401, 256) == 1024);  // w*4
        CHECK(sr3texture::sourceRowBytes(402, 4) == 16);      // one 4x4 DXT5 block
        CHECK(sr3texture::sourceRowBytes(403, 640) == 1280);  // w*2
        CHECK(sr3texture::sourceRowBytes(406, 10) == 30);     // w*3
        CHECK(sr3texture::sourceRowBytes(407, 64) == 256);    // w*4
        CHECK(sr3texture::sourceRowBytes(410, 64) == 64);     // w
        CHECK(sr3texture::sourceRowBytes(999, 64) == 0);
        // CONTROL (Sec12.5 C5): 403 as 4 B/px would give 2560, not 1280.
        CHECK(sr3texture::sourceRowBytes(403, 640) != 640 * 4);
    }

    // ------------------------------------------------------------------
    // 403 sizes from the spec text: 240x240 one level = 115,200 (Sec11.2);
    // 800x324 = 518,400 (Sec7); 1280x720 and 640x512 minimap (Sec11.2).
    // ------------------------------------------------------------------
    {
        LevelLayout l;
        std::string why;
        CHECK(computeLevelLayout(403, 240, 240, 1, false, l, why) && l.totalBytes == 115200 && l.levels.size() == 1);
        CHECK(computeLevelLayout(403, 800, 324, 1, false, l, why) && l.totalBytes == 518400);
        CHECK(computeLevelLayout(403, 1280, 720, 1, false, l, why) && l.totalBytes == 1280ull * 720 * 2);
        CHECK(computeLevelLayout(403, 640, 512, 1, false, l, why) && l.totalBytes == 655360);
        CHECK(l.faceCount == 1);
        // CONTROL: the "4 bytes per pixel" reading is 2x too big (fails on 709/709 in the spec, Sec12.5 C5).
        CHECK(computeLevelLayout(403, 240, 240, 1, false, l, why) && l.totalBytes != 240ull * 240 * 4);
        // Unknown code and impossible cube shape are reported, not asserted through.
        CHECK(!computeLevelLayout(603, 64, 64, 1, false, l, why) && !why.empty());
        CHECK(!computeLevelLayout(400, 128, 770, 1, true, l, why)); // 770 % 6 != 0
    }

    // ------------------------------------------------------------------
    // Mip clamp (Sec10.5). All expected totals derived by counting 4x4
    // blocks (DXT1 = 8 bytes/block) or pixels by hand.
    // ------------------------------------------------------------------
    {
        LevelLayout l;
        std::string why;
        // 8x8 DXT1, 3 levels: 8x8 = 4 blocks = 32, then 4x4 = 8, then the
        // width/height CLAMP at 4 keeps the third level at 4x4 = 8 -> 48.
        CHECK(computeLevelLayout(400, 8, 8, 3, false, l, why) && l.totalBytes == 48);
        CHECK(l.levels[2].width == 4 && l.levels[2].height == 4);
        // CONTROL: WITHOUT the clamp the third level would be 2x2 and the
        // spec's literal per-level formula (height * ((w>>1)*4) / 4) would
        // give 2 bytes there, for an unclamped total of 32 + 8 + 2 = 42.
        // Hand-evaluate that formula independently of the code under test:
        auto literalLevel = [](uint64_t w, uint64_t h) { return h * ((w >> 1) * 4) / 4; };
        const uint64_t unclamped = literalLevel(opaque<uint64_t>(8), 8) + literalLevel(opaque<uint64_t>(4), 4) + literalLevel(opaque<uint64_t>(2), 2);
        CHECK(unclamped == 42);
        CHECK(l.totalBytes != unclamped);
        // Non-square DXT1 256x32, 7 levels. Block counts by hand:
        // 64x8=512 blocks, 32x4=128, 16x2=32, 8x1=8, 4x1=4, 2x1=2, 1x1=1
        // -> x8 bytes = 4096+1024+256+64+32+16+8 = 5,496.
        CHECK(computeLevelLayout(400, 256, 32, 7, false, l, why) && l.totalBytes == 5496);
        CHECK(l.levels[3].width == 32 && l.levels[3].height == 4);
        CHECK(l.levels[4].width == 16 && l.levels[4].height == 4); // height stalled at 4, width still halving
        // Non-DXT clamps at 1: 403 8x2, 4 levels = 32+8+4+2 = 46 bytes.
        CHECK(computeLevelLayout(403, 8, 2, 4, false, l, why) && l.totalBytes == 46);
        CHECK(l.levels[3].width == 1 && l.levels[3].height == 1);
        // A8 (410) 4x4, 3 levels: 16 + 4 + 1 = 21.
        CHECK(computeLevelLayout(410, 4, 4, 3, false, l, why) && l.totalBytes == 21);
        // Single-level ("no chain at all", Sec4): 64x64 DXT5 = 4096.
        CHECK(computeLevelLayout(402, 64, 64, 1, false, l, why) && l.totalBytes == 4096);
    }

    // ------------------------------------------------------------------
    // Cube records (Sec10.5): flag 0x8, height = 6 x edge, faces stacked.
    // Shipped shapes 128x768, 64x384, 256x1536, all format 400, 1 level.
    // 128x128 DXT1 face = 32x32 blocks x 8 = 8,192; x6 = 49,152.
    // ------------------------------------------------------------------
    {
        LevelLayout l;
        std::string why;
        CHECK(computeLevelLayout(400, 128, 768, 1, true, l, why));
        CHECK(l.faceCount == 6 && l.faceBytes == 8192 && l.totalBytes == 49152);
        CHECK(l.levels[0].height == 128); // per-face height
        // 64x384: 16x16x8 = 2048 x6 = 12,288; 256x1536: 64x64x8 = 32768 x6 = 196,608.
        CHECK(computeLevelLayout(400, 64, 384, 1, true, l, why) && l.totalBytes == 12288);
        CHECK(computeLevelLayout(400, 256, 1536, 1, true, l, why) && l.totalBytes == 196608);
        // Record-level: the flag, not the shape, selects the cube rule.
        std::vector<SyntheticRecord> recs(1);
        recs[0] = {0, 128, 768, 400, 1, 1, 1, 0x0008, 1, 1, 49152, "cloudy.tga"};
        std::vector<uint8_t> blob = buildCpeg(recs);
        sr3texture::TexturePair t = sr3texture::TexturePair::parse(view(blob));
        CHECK(t.records()[0].isCubeMap() && !t.records()[0].hasFlipbookGrid());
        CHECK(t.levelLayout(0).totalBytes == 49152);
        // Same dimensions WITHOUT the cube flag give the same total for one
        // level (the spec notes shipped data cannot tell them apart) but a
        // different face count: this pins that the flag is what is read.
        recs[0].flags = 0;
        std::vector<uint8_t> flat = buildCpeg(recs);
        CHECK(sr3texture::TexturePair::parse(view(flat)).levelLayout(0).faceCount == 1);
        CHECK(sr3texture::TexturePair::parse(view(flat)).levelLayout(0).totalBytes == 49152);
    }

    // ------------------------------------------------------------------
    // Flags / grid fields (Sec10.3, Sec10.7) and the level byte at +0x23.
    // ------------------------------------------------------------------
    {
        std::vector<SyntheticRecord> recs(1);
        recs[0] = {0, 256, 256, 402, 4, 4, 1, 0x0101, 1, 7, 87376, "vfx_smoke_anim_v01.tga"};
        std::vector<uint8_t> blob = buildCpeg(recs);
        sr3texture::TexturePair t = sr3texture::TexturePair::parse(view(blob));
        const auto& r = t.records()[0];
        CHECK(r.hasFlipbookGrid() && !r.isCubeMap());
        CHECK(r.gridColumns == 4 && r.gridRows == 4);
        CHECK(r.flags == 0x0101);
        CHECK(r.levelCount == 7);
        CHECK(r.animationRate == 1);
        // pixelFormat is only the LOW 16 bits (Sec10.3); the raw dword is kept.
        std::vector<SyntheticRecord> hi(1);
        hi[0] = {0, 8, 8, 0xABCD0190u, 1, 1, 1, 0, 1, 1, 32, "hi.tga"}; // low 16 bits = 400
        sr3texture::TexturePair th = sr3texture::TexturePair::parse(view(buildCpeg(hi)));
        CHECK(th.records()[0].pixelFormat == 400 && th.records()[0].pixelFormatRaw == 0xABCD0190u);
    }

    // ------------------------------------------------------------------
    // R5G6B5 expansion (Sec11.2: little-endian word, R = bits 15-11,
    // G = 10-5, B = 4-0). Hand-worked values.
    // ------------------------------------------------------------------
    {
        const uint8_t src[] = {
            0x00, 0xF8, // 0xF800 -> pure red
            0xE0, 0x07, // 0x07E0 -> pure green
            0x1F, 0x00, // 0x001F -> pure blue
            0xFF, 0xFF, // white
            0x00, 0x00, // black
            0x10, 0x84, // 0x8410: R=16 G=32 B=16 -> (16*255+15)/31=132, (32*255+31)/63=130, 132
            0x1F, 0xF8, // 0xF81F -> magenta (R=31,B=31)
        };
        uint8_t dst[7 * 4] = {};
        sr3texture::expandR5G6B5ToRgba8(src, 7, dst);
        auto px = [&](int i, int c) { return static_cast<int>(dst[i * 4 + c]); };
        CHECK(px(0, 0) == 255 && px(0, 1) == 0 && px(0, 2) == 0 && px(0, 3) == 255);
        CHECK(px(1, 0) == 0 && px(1, 1) == 255 && px(1, 2) == 0 && px(1, 3) == 255);
        CHECK(px(2, 0) == 0 && px(2, 1) == 0 && px(2, 2) == 255 && px(2, 3) == 255);
        CHECK(px(3, 0) == 255 && px(3, 1) == 255 && px(3, 2) == 255);
        CHECK(px(4, 0) == 0 && px(4, 1) == 0 && px(4, 2) == 0 && px(4, 3) == 255);
        CHECK(px(5, 0) == 132 && px(5, 1) == 130 && px(5, 2) == 132);
        CHECK(px(6, 0) == 255 && px(6, 1) == 0 && px(6, 2) == 255);
        // CONTROL (Sec11.2 "red/blue swapped: blue skin"): a swapped decode
        // would make the 0xF800 word blue - assert ours does not.
        CHECK(!(px(0, 0) == 0 && px(0, 2) == 255));
        // CONTROL: byte order. If the word were read big-endian, bytes
        // {0x00,0xF8} would be 0x00F8 = R0 G7 B24, which is NOT pure red.
        CHECK(!(px(0, 1) == (7 * 255 + 31) / 63));
    }

    // ------------------------------------------------------------------
    // validateAgainstGpeg + pixelBytes.
    // ------------------------------------------------------------------
    {
        std::vector<SyntheticRecord> recs(2);
        recs[0] = {0, 64, 64, 400, 1, 1, 1, 0, 1, 5, 2728, "a.tga"}; // 2048+512+128+32+8 = 2728
        recs[1] = {2736, 32, 32, 402, 1, 1, 1, 0, 1, 4, 1360, "b.tga"}; // 1024+256+64+16 = 1360; 2736 = align16(2728)
        BuildOptions opt;
        opt.gSize = align16(2736 + 1360); // 4096
        std::vector<uint8_t> blob = buildCpeg(recs, opt);
        sr3texture::TexturePair t = sr3texture::TexturePair::parse(view(blob));
        CHECK(t.levelLayout(0).totalBytes == 2728);
        CHECK(t.levelLayout(1).totalBytes == 1360);
        CHECK(opt.gSize == 4096);
        std::vector<uint8_t> gpeg(opt.gSize, 0xEE);
        t.validateAgainstGpeg(view(gpeg)); // must not throw
        CHECK(t.pixelBytes(0, view(gpeg)).size() == 2728);
        CHECK(t.pixelBytes(1, view(gpeg)).size() == 1360);
        CHECK(t.pixelBytes(1, view(gpeg)).data() == gpeg.data() + 2736);
        CHECK(throwsFormatError([&] { t.pixelBytes(2, view(gpeg)); }));
        // Wrong g size (pairedSize mismatch).
        std::vector<uint8_t> shortG(100, 0);
        CHECK(throwsFormatError([&] { t.validateAgainstGpeg(view(shortG)); }));
        // Sentinel offset: no data (Sec10.2), skipped by validate, refused by pixelBytes.
        std::vector<SyntheticRecord> nod(1);
        nod[0] = {0xFFFFFFFFu, 8, 8, 400, 1, 1, 1, 0, 1, 1, 32, "n.tga"};
        BuildOptions optN;
        optN.gSize = 0;
        sr3texture::TexturePair tn = sr3texture::TexturePair::parse(view(buildCpeg(nod, optN)));
        CHECK(!tn.records()[0].hasData());
        std::vector<uint8_t> emptyG;
        tn.validateAgainstGpeg(view(emptyG));
        CHECK(throwsFormatError([&] { tn.pixelBytes(0, view(emptyG)); }));
        // Range past the end of g: a record claiming more bytes than the file has.
        std::vector<SyntheticRecord> over(1);
        over[0] = {0, 64, 64, 400, 1, 1, 1, 0, 1, 5, 2728, "o.tga"};
        BuildOptions optO;
        optO.gSize = 1000;
        sr3texture::TexturePair to = sr3texture::TexturePair::parse(view(buildCpeg(over, optO)));
        std::vector<uint8_t> g1000(1000, 0);
        CHECK(throwsFormatError([&] { to.validateAgainstGpeg(view(g1000)); }));
    }

    // ------------------------------------------------------------------
    // Content validation.
    // ------------------------------------------------------------------
    CHECK(sr3texture::looksLikeCpegFilename("bloodsplat01.cpeg_pc"));
    CHECK(sr3texture::looksLikeCpegFilename("UI_SAVE_0.CVBM_PC")); // case-insensitive
    CHECK(!sr3texture::looksLikeCpegFilename("bloodsplat01.gpeg_pc")); // GPU-side, deliberately not matched
    CHECK(!sr3texture::looksLikeCpegFilename("ui_save_0.gvbm_pc"));
    CHECK(!sr3texture::looksLikeCpegFilename("texture.dds"));
    CHECK(!sr3texture::looksLikeCpegFilename("no_extension_at_all"));

    {
        SyntheticRecord r = {0, 8, 8, 400, 1, 1, 1, 0, 1, 1, 32, "a.tga"};
        std::vector<uint8_t> blob = buildCpeg({r});
        CHECK(sr3texture::validateCpegContent(blob).status == sr3texture::CpegValidation::WellFormed);
        std::vector<uint8_t> notCpeg = {0x00, 0x01, 0x02, 0x03};
        CHECK(sr3texture::validateCpegContent(notCpeg).status == sr3texture::CpegValidation::NotWellFormed);
        // An unterminated name must be reported as not-well-formed, not thrown out of the validator.
        std::vector<uint8_t> unterminated = blob;
        unterminated.pop_back();
        patchU32(unterminated, 0x08, static_cast<uint32_t>(unterminated.size()));
        CHECK(sr3texture::validateCpegContent(unterminated).status == sr3texture::CpegValidation::NotWellFormed);

        // refineWithCpegValidation: PERMANENT NO-OP (HANDOFF.md §9.78 -
        // decompressEntry never produces OkUnconfirmedContent any more),
        // passes EVERY status through completely unchanged, including
        // OkUnconfirmedContent itself.
        vpp::DecompressResult in;
        in.status = vpp::DecodeStatus::Ok;
        in.data = {0x00, 0x01, 0x02};
        CHECK(sr3texture::refineWithCpegValidation(in).status == vpp::DecodeStatus::Ok); // untouched

        vpp::DecompressResult good;
        good.status = vpp::DecodeStatus::OkUnconfirmedContent;
        good.data = blob;
        vpp::DecompressResult goodOut = sr3texture::refineWithCpegValidation(good);
        CHECK(goodOut.status == vpp::DecodeStatus::OkUnconfirmedContent); // unchanged - no-op
        CHECK(goodOut.data == blob);

        vpp::DecompressResult bad;
        bad.status = vpp::DecodeStatus::OkUnconfirmedContent;
        bad.data = notCpeg;
        vpp::DecompressResult badOut = sr3texture::refineWithCpegValidation(bad);
        CHECK(badOut.status == vpp::DecodeStatus::OkUnconfirmedContent); // unchanged - no-op, even for content that would have failed the old check
        CHECK(badOut.data == notCpeg);
    }

    if (g_failures == 0) {
        std::cout << "All synthetic texture-format tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) FAILED.\n";
    return 1;
}
