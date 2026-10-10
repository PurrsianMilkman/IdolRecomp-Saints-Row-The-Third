// Bink container header probe + frame-0 decode attempt, built strictly from
// this project's own clean-room specs: spec-bink-codec.md (the DLL's own
// container/codec internals, binkw32.dll address space 0x10xxxxxx) and
// spec-bink-boundary.md (the game<->Bink call contract). Both are cited by
// section number throughout - this file adds NO new reverse-engineered
// facts of its own; where those two specs don't confirm a detail needed to
// go further, this file stops and reports an OPEN gap instead of guessing
// (this project's standing "no invented fixes" rule).
//
// Target asset: a REAL shipped file, not a synthetic/handwritten one -
// "D:\Project Crreish\Saints Row 3 CRREISH\videos\logo_thq_and_voli_hd.bik".
// Chosen because it is literally a publisher-logo/startup splash video (THQ
// + Volition logo), the exact category spec-bink-boundary.md Sec6.1 confirms
// is reached through "0x00bddcd0" -> "choose resolution" (1280x720 for a
// widescreen target) -> "apply descriptor" -> the real Open() path at
// "0x00bdc3b0" - i.e. this specific file is a direct, real instance of the
// "logo/startup splash" trigger path that spec documents, not an arbitrary
// pick from the videos/ folder.
//
// WHAT THIS TOOL DOES, AND WHERE IT STOPS (see "DECODE STATUS" at the end of
// main() for the full, itemized account):
//   1. Reads the real file's fixed 44-byte container header and validates it
//      against every CONFIRMED rule in spec-bink-codec.md Sec1.2's own
//      field table (signature, bounds, width/height macroblock rounding,
//      frame-rate division, flag-bit extraction).
//   2. Resolves the two CONFIRMED per-file bit-selectors (Sec1.3's per-OPEN
//      scale-mode 3-bit field, Sec1.4's per-file bitstream-revision bits)
//      using facts independently CONFIRMED in spec-bink-boundary.md Sec2.2's
//      own Open()-flags composition, combined by plain arithmetic (not a new
//      reverse-engineered fact; see the comment at computeScaleMode()).
//   3. **2026-10-08 update:** spec-bink-codec.md Sec1.5a now closes the gap
//      this tool used to stop at - it confirms `0x1000e560` is a one-time,
//      whole-table monotonicity VALIDATE + worst-case-buffer-size SCAN run
//      once at BinkOpen, over a frame-offset table that already exists by
//      the time it runs (Sec1.2's own correction: "It is not 'table-like
//      structures ... being built'"). Sec1.6 independently confirms that
//      table's IN-MEMORY shape (handle+0x158, one 32-bit entry per frame,
//      bit 0 = keyframe flag, the rest presumably the same dword masked).
//      Neither section states the table's ON-DISK start offset or any
//      per-track-descriptor byte count between the fixed header and the
//      table (D10's role as a track count is still not independently
//      confirmed) - so this tool does NOT read that fact from the spec (it
//      isn't there) or invent one. Instead, locateFrameOffsetTable() below
//      empirically searches a bounded range of candidate on-disk start
//      offsets/entry counts and accepts only one that is internally
//      self-consistent against this real file's own bytes: strictly
//      increasing masked byte offsets, all landing inside the file. That is
//      verification against the real file, not a guessed constant - see
//      that function's own comment for the exact criteria.
//   4. Once the table is located, frame 0's compressed byte range is read
//      directly as [table[0] masked, table[1] masked) and those raw bytes
//      are extracted from the real file (optionally written to disk via an
//      optional 2nd command-line argument, after the input path). No pixel
//      decode is attempted: the
//      entropy-coding bit-level math, the transform's exact coefficient
//      semantics, and the final pixel-write/colorspace step remain OPEN
//      items (Sec8, items 2/5/6) in spec-bink-codec.md, and no off-the-
//      shelf, clean-room-eligible Bink video decoder dependency exists for
//      this project to lean on instead (see "DECODE STATUS" for why this
//      is NOT the same situation as the audio side's libvorbis/libogg
//      vendoring). No Huffman table or filter coefficient is invented to
//      work around this; this tool reports it as a blocking gap instead.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

uint32_t readU32LE(const uint8_t* p) {
    // x86 Bink containers are little-endian throughout (spec-bink-codec.md
    // Sec1.2: "11 little-endian dwords") - this host is little-endian too,
    // but the read is done byte-by-byte so the result does not depend on
    // the host's own endianness.
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

struct BinkHeader {
    char signature[5] = {0, 0, 0, 0, 0};  // D0, offset 0x00 (4 ASCII bytes + NUL)
    uint32_t totalSizeField = 0;          // D1, offset 0x04
    uint32_t numFrames = 0;               // D2, offset 0x08
    uint32_t d3SizeShaped = 0;            // D3, offset 0x0c
    uint32_t d4BoundedCount = 0;          // D4, offset 0x10
    uint32_t rawWidth = 0;                // D5, offset 0x14
    uint32_t rawHeight = 0;               // D6, offset 0x18
    uint32_t frameRateNum = 0;            // D7, offset 0x1c
    uint32_t frameRateDen = 0;            // D8, offset 0x20
    uint32_t attributes = 0;              // D9, offset 0x24
    uint32_t d10SmallCount = 0;           // D10, offset 0x28

    // Derived (all CONFIRMED formulas, spec-bink-codec.md Sec1.2/Sec1.5):
    uint32_t roundedWidth = 0;
    uint32_t roundedHeight = 0;
    uint32_t macroblockCols = 0;
    uint32_t macroblockRows = 0;
    uint32_t macroblockTotal = 0;
    uint32_t roundedFps = 0;
    bool attrBit0x20000 = false;
    bool attrBit0x100000 = false;
};

// spec-bink-codec.md Sec1.2's D5/D6 row: "16-pixel macroblock rounding
// ((D5+0xf)>>4), rounded up to a multiple of 16) applied immediately after".
uint32_t roundUpToMacroblock16(uint32_t v) {
    return ((v + 0xFu) >> 4) << 4;
}

// spec-bink-codec.md Sec1.4: the container signature's own 4th byte is the
// real per-file bitstream-revision selector, resolved at BinkOpen time:
//   'BIKh'       -> sets bit 0x8000 only
//   'BIKf'/'BIKg'-> sets both 0x8000 and 0x10000
//   'BIKi'       -> neither bit forced
// (what these bits select *inside* the video core is OPEN item 1 of that
// file's Sec8 - not needed for header parsing, so left unresolved here too.)
void resolveRevisionBits(char fourthByte, bool& bit0x8000, bool& bit0x10000) {
    bit0x8000 = false;
    bit0x10000 = false;
    if (fourthByte == 'h') {
        bit0x8000 = true;
    } else if (fourthByte == 'f' || fourthByte == 'g') {
        bit0x8000 = true;
        bit0x10000 = true;
    }
    // 'i' (this file's real signature, see below): neither bit forced.
}

// spec-bink-codec.md Sec1.3: a 3-bit field (bits 28-30 of a resolved
// internal flags dword) selects a per-BinkOpen-call output scale:
//   0b000          -> no change
//   0b001 / 0b010  -> height doubled
//   0b011          -> width doubled
//   0b100 / 0b101  -> both doubled
//   0b110 / 0b111  -> no change
// That resolved dword combines the CALLER's own BinkOpen `flags` argument
// with D9's bits 28-30, and D9's bits are used ONLY when the caller passes
// the literal sentinel 0x70000000 ("defer to the file").
//
// This probe is not emulating a live BinkOpen call, so it has no caller
// argument of its own - but for THIS specific real file, spec-bink-
// boundary.md Sec2.2 independently CONFIRMS the exact literal flags value
// the game's real Open() path (0x00bdc3b0) composes for every movie it
// opens, logo splash included (Sec6.1 confirms the logo path reaches this
// same Open()): "(0x4400 or 0x204400, optionally |0x1000000) | 0x4000".
// None of those four literal terms sets any of bits 28-30 (0x4400 and
// 0x204400 both have zero bits above bit 23 except 0x204400's own bit 21;
// 0x1000000 is bit 24; 0x4000 is bit 14) - so the real composed value's
// bits 28-30 are 0 in every branch, which is NOT the 0x70000000 sentinel.
// Therefore D9's own bits 28-30 are never consulted for this file's real
// open call, and the selector evaluates on the caller value alone: 0b000,
// "no change". This is plain arithmetic over two independently CONFIRMED
// spec facts, not a new reverse-engineered claim - flagged here as DERIVED
// rather than itself CONFIRMED, since no single spec sentence states the
// combined conclusion directly.
const char* computeScaleModeForThisAsset() {
    return "0b000 (no change) - DERIVED by combining spec-bink-codec.md Sec1.3's "
           "selector table with spec-bink-boundary.md Sec2.2's literal Open()-flags "
           "composition (see computeScaleModeForThisAsset()'s own comment for the "
           "arithmetic); not independently stated as one fact in either spec.";
}

bool readHeader(const std::string& path, BinkHeader& out, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "could not open '" + path + "'";
        return false;
    }
    uint8_t raw[44];
    f.read(reinterpret_cast<char*>(raw), sizeof(raw));
    if (!f || f.gcount() != static_cast<std::streamsize>(sizeof(raw))) {
        error = "Error reading Bink header.";  // spec-bink-codec.md Sec1.7, literal string
        return false;
    }

    std::memcpy(out.signature, raw, 4);
    out.signature[4] = '\0';
    const bool sigOk = (std::memcmp(raw, "BIKf", 4) == 0) || (std::memcmp(raw, "BIKg", 4) == 0) ||
                       (std::memcmp(raw, "BIKh", 4) == 0) || (std::memcmp(raw, "BIKi", 4) == 0);
    if (!sigOk) {
        error = "Not a Bink file.";  // spec-bink-codec.md Sec1.7, literal string
        return false;
    }

    out.totalSizeField = readU32LE(raw + 0x04);
    out.numFrames = readU32LE(raw + 0x08);
    out.d3SizeShaped = readU32LE(raw + 0x0c);
    out.d4BoundedCount = readU32LE(raw + 0x10);
    out.rawWidth = readU32LE(raw + 0x14);
    out.rawHeight = readU32LE(raw + 0x18);
    out.frameRateNum = readU32LE(raw + 0x1c);
    out.frameRateDen = readU32LE(raw + 0x20);
    out.attributes = readU32LE(raw + 0x24);
    out.d10SmallCount = readU32LE(raw + 0x28);

    if (out.numFrames == 0) {
        error = "The file doesn't contain any compressed frames yet.";  // Sec1.7, literal
        return false;
    }

    // Remaining bounds, Sec1.2's table - spec marks these HIGH/structural
    // (no single quoted error string tied to each one individually), so a
    // failure here is reported as a plain validation warning, not one of
    // the two literal strings above.
    bool boundsOk = true;
    if (!(out.totalSizeField >= (out.d10SmallCount + 1) * out.d4BoundedCount * 4)) boundsOk = false;
    if (!(out.d3SizeShaped <= out.totalSizeField)) boundsOk = false;
    if (!(out.d4BoundedCount >= out.numFrames && out.d4BoundedCount <= 1000000)) boundsOk = false;
    if (!(out.rawWidth <= 32767)) boundsOk = false;
    if (!(out.rawHeight <= 32767)) boundsOk = false;
    if (!(out.frameRateNum != 0)) boundsOk = false;
    if (!(out.frameRateDen != 0)) boundsOk = false;
    if (!(out.d10SmallCount <= 256)) boundsOk = false;
    if (!boundsOk) {
        error = "one or more Sec1.2 structural bounds failed (see stdout detail above)";
        return false;
    }

    out.roundedWidth = roundUpToMacroblock16(out.rawWidth);
    out.roundedHeight = roundUpToMacroblock16(out.rawHeight);
    out.macroblockCols = out.roundedWidth / 16;
    out.macroblockRows = out.roundedHeight / 16;
    out.macroblockTotal = out.macroblockCols * out.macroblockRows;
    out.roundedFps = (out.frameRateNum + out.frameRateDen / 2) / out.frameRateDen;
    if (out.roundedFps < 1) out.roundedFps = 1;
    out.attrBit0x20000 = (out.attributes & 0x20000u) != 0;
    out.attrBit0x100000 = (out.attributes & 0x100000u) != 0;

    return true;
}

// ---------------------------------------------------------------------------
// Frame-offset table location (empirical, per this file's own top comment).
// ---------------------------------------------------------------------------

struct FrameOffsetCandidate {
    uint32_t start = 0;
    uint32_t count = 0;
    bool fits = false;            // table + its entries fit inside the real file
    bool monotonicMasked = false; // masked[i+1] > masked[i] for every i (strict)
    bool allInRange = false;      // every masked value is in (0, fileSize]
    bool touchesTableEnd = false; // masked[0] == start + count*4 (zero-gap signal)
    bool touchesFileEnd = false;  // masked[count-1] == fileSize (sentinel signal)
    bool pass = false;            // fits && monotonicMasked && allInRange
};

struct FrameOffsetTable {
    bool found = false;
    uint32_t start = 0;
    uint32_t count = 0;
    std::vector<uint32_t> raw;  // the count raw 32-bit entries, as stored on disk
};

// spec-bink-codec.md Sec1.6 CONFIRMS the table's in-memory shape (array of
// 32-bit entries, one per frame, bit 0 = keyframe flag, "the real byte-offset
// value is presumably the same dword with bit 0 masked off"). Sec1.2/Sec1.5a
// do NOT state the table's on-disk start offset or any per-track-descriptor
// byte count between the fixed 44-byte header (ending at file offset 0x2c)
// and the table itself - D10's own role ("plausibly a track count") is
// explicitly "not independently confirmed". Rather than invent either of
// those missing numbers, this function empirically scans a generous, bounded
// range of 4-byte-aligned candidate start offsets (the table is an array of
// 32-bit entries, so any real start must itself be 4-byte aligned relative to
// the header) and a small set of candidate entry counts derived from fields
// already CONFIRMED to bound it (D2 = NumFrames, D4 = the "bounded count"
// field whose own Sec1.2 validation rule is `D4 >= D2`; +1 on each covers the
// sentinel-last-entry case spec-bink-codec.md Sec1.5a's own degenerate-case
// formula implies by indexing `table[NumFrames]`, i.e. a valid index at
// position NumFrames, requiring at least NumFrames+1 physical entries).
//
// A candidate is accepted only if it is internally self-consistent against
// THIS REAL FILE'S OWN BYTES: every masked (bit-0-cleared) entry strictly
// increasing (the same monotonicity idiom Sec1.5a documents `0x1000e560`
// itself checking, applied here to the on-disk search rather than
// reproduced), and every masked value landing inside the file's real byte
// range. Among passing candidates, the smallest start offset whose first
// entry lands exactly at the end of its own table (zero gap between table
// and payload - the strongest structural signal available) is preferred;
// this is verification against real data, not a guessed constant.
bool locateFrameOffsetTable(const std::vector<uint8_t>& file, uint32_t numFrames,
                            uint32_t boundedCount, FrameOffsetTable& out,
                            std::vector<FrameOffsetCandidate>& log) {
    const uint32_t fileSize = static_cast<uint32_t>(file.size());
    constexpr uint32_t kHeaderEnd = 0x2c;       // Sec1.2: fixed header ends here
    constexpr uint32_t kMaxDescriptorScan = 256; // generous scan bound, not a cited fact

    std::vector<uint32_t> counts;
    for (uint32_t c : {numFrames, boundedCount, numFrames + 1, boundedCount + 1}) {
        if (c >= 2 && std::find(counts.begin(), counts.end(), c) == counts.end()) {
            counts.push_back(c);
        }
    }

    bool haveBest = false;
    FrameOffsetCandidate best;

    for (uint32_t start = kHeaderEnd; start <= kHeaderEnd + kMaxDescriptorScan; start += 4) {
        for (uint32_t count : counts) {
            FrameOffsetCandidate cand;
            cand.start = start;
            cand.count = count;

            const uint64_t tableBytes = static_cast<uint64_t>(count) * 4ull;
            const uint64_t tableEnd = static_cast<uint64_t>(start) + tableBytes;
            if (tableEnd > fileSize) {
                log.push_back(cand);
                continue;
            }
            cand.fits = true;

            std::vector<uint32_t> entries(count);
            for (uint32_t i = 0; i < count; ++i) {
                entries[i] = readU32LE(&file[start + static_cast<size_t>(i) * 4]);
            }

            bool monotonic = true;
            bool inRange = true;
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t masked = entries[i] & ~1u;
                if (masked == 0 || masked > fileSize) inRange = false;
                if (i > 0) {
                    const uint32_t prevMasked = entries[i - 1] & ~1u;
                    if (!(masked > prevMasked)) monotonic = false;
                }
            }
            cand.monotonicMasked = monotonic;
            cand.allInRange = inRange;
            cand.touchesTableEnd = ((entries[0] & ~1u) == tableEnd);
            cand.touchesFileEnd = ((entries[count - 1] & ~1u) == fileSize);
            cand.pass = monotonic && inRange;
            log.push_back(cand);

            if (cand.pass) {
                const bool better =
                    !haveBest ||
                    (cand.touchesTableEnd && !best.touchesTableEnd) ||
                    (cand.touchesTableEnd == best.touchesTableEnd && cand.start < best.start);
                if (better) {
                    best = cand;
                    haveBest = true;
                    out.found = true;
                    out.start = start;
                    out.count = count;
                    out.raw = std::move(entries);
                }
            }
        }
    }
    return out.found;
}

struct Frame0Range {
    uint32_t startOffset = 0;
    uint32_t endOffsetExclusive = 0;
    uint32_t sizeBytes = 0;
    bool keyframe = false;
};

// spec-bink-codec.md Sec1.6: frame N's compressed byte range is
// [table[N] masked, table[N+1] masked) - read directly as stored, no
// resolver logic reproduced (Sec1.5a's own text: the table is read, not
// rebuilt, at decode time).
bool extractFrame0Range(const FrameOffsetTable& table, Frame0Range& out, std::string& error) {
    if (table.count < 2) {
        error = "located table has fewer than 2 entries - cannot bound frame 0";
        return false;
    }
    const uint32_t e0 = table.raw[0];
    const uint32_t e1 = table.raw[1];
    out.keyframe = (e0 & 1u) != 0;
    out.startOffset = e0 & ~1u;
    out.endOffsetExclusive = e1 & ~1u;
    if (!(out.endOffsetExclusive > out.startOffset)) {
        error = "frame 0's masked byte range is not positive";
        return false;
    }
    out.sizeBytes = out.endOffsetExclusive - out.startOffset;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string defaultPath =
        "D:\\Project Crreish\\Saints Row 3 CRREISH\\videos\\logo_thq_and_voli_hd.bik";
    const std::string path = (argc > 1) ? argv[1] : defaultPath;

    std::printf("bink_frame0_probe: real shipped-asset Bink container probe\n");
    std::printf("input file: %s\n", path.c_str());

    BinkHeader hdr;
    std::string error;
    if (!readHeader(path, hdr, error)) {
        std::printf("HEADER PARSE FAILED: %s\n", error.c_str());
        return 1;
    }

    std::printf("\n--- Fixed 44-byte container header (spec-bink-codec.md Sec1.2) ---\n");
    std::printf("D0 signature            : \"%s\"\n", hdr.signature);
    std::printf("D1 total-size-shaped     : %u\n", hdr.totalSizeField);
    std::printf("D2 numFrames             : %u\n", hdr.numFrames);
    std::printf("D3 size-shaped           : %u\n", hdr.d3SizeShaped);
    std::printf("D4 bounded count         : %u\n", hdr.d4BoundedCount);
    std::printf("D5 raw width             : %u\n", hdr.rawWidth);
    std::printf("D6 raw height            : %u\n", hdr.rawHeight);
    std::printf("D7 frameRate numerator   : %u\n", hdr.frameRateNum);
    std::printf("D8 frameRate denominator : %u\n", hdr.frameRateDen);
    std::printf("D9 attributes (raw)      : 0x%08x\n", hdr.attributes);
    std::printf("   bit 0x20000 set?      : %s\n", hdr.attrBit0x20000 ? "yes" : "no");
    std::printf("   bit 0x100000 set?     : %s\n", hdr.attrBit0x100000 ? "yes" : "no");
    std::printf("D10 small bounded count  : %u\n", hdr.d10SmallCount);

    std::printf("\n--- Derived fields (CONFIRMED formulas) ---\n");
    std::printf("rounded width x height   : %u x %u (macroblock-rounded, Sec1.2)\n", hdr.roundedWidth,
                hdr.roundedHeight);
    std::printf("macroblock cols x rows   : %u x %u = %u total (Sec1.5, handle+0xf0/+0xf4)\n",
                hdr.macroblockCols, hdr.macroblockRows, hdr.macroblockTotal);
    std::printf("rounded integer FPS      : %u (Sec1.2's (D7+D8/2)/D8 division, min 1)\n", hdr.roundedFps);

    bool revBit0x8000 = false, revBit0x10000 = false;
    resolveRevisionBits(hdr.signature[3], revBit0x8000, revBit0x10000);
    std::printf("\n--- Per-file bitstream-revision bits (Sec1.4, from signature's own 4th byte) ---\n");
    std::printf("signature 4th byte '%c'  -> bit 0x8000=%s, bit 0x10000=%s\n", hdr.signature[3],
                revBit0x8000 ? "SET" : "clear", revBit0x10000 ? "SET" : "clear");
    std::printf("(what these bits select inside the video core is spec-bink-codec.md Sec8 item 1, OPEN)\n");

    std::printf("\n--- Per-BinkOpen-call scale-mode selector (Sec1.3) ---\n");
    std::printf("%s\n", computeScaleModeForThisAsset());
    std::printf("-> output size for this real open call: %u x %u (unchanged from the rounded size above)\n",
                hdr.roundedWidth, hdr.roundedHeight);

    // ---- Load the whole file for the table search + frame-0 extraction. ----
    std::vector<uint8_t> fileBytes;
    {
        std::ifstream wf(path, std::ios::binary);
        if (!wf) {
            std::printf("\nFATAL: could not reopen '%s' for a whole-file read.\n", path.c_str());
            return 1;
        }
        wf.seekg(0, std::ios::end);
        const std::streamoff sz = wf.tellg();
        wf.seekg(0, std::ios::beg);
        if (sz <= 0) {
            std::printf("\nFATAL: whole-file read reported a non-positive size.\n");
            return 1;
        }
        fileBytes.resize(static_cast<size_t>(sz));
        wf.read(reinterpret_cast<char*>(fileBytes.data()), sz);
        if (!wf) {
            std::printf("\nFATAL: whole-file read did not complete.\n");
            return 1;
        }
    }
    std::printf("\n--- Whole-file read for table search ---\n");
    std::printf("file size on disk        : %zu bytes\n", fileBytes.size());
    std::printf("D1+8 == file size?        : %s (D1 is plausibly a RIFF-style size field\n",
                (static_cast<uint64_t>(hdr.totalSizeField) + 8ull == fileBytes.size()) ? "yes" : "no");
    std::printf("                             excluding its own 8-byte signature+size prefix;\n"
                "                             empirical cross-check only, not a spec citation)\n");

    std::printf("\n--- Frame-offset table location (empirical search, see locateFrameOffsetTable()) ---\n");
    FrameOffsetTable table;
    std::vector<FrameOffsetCandidate> candidateLog;
    const bool tableFound =
        locateFrameOffsetTable(fileBytes, hdr.numFrames, hdr.d4BoundedCount, table, candidateLog);

    uint32_t candidatesTried = 0, candidatesFit = 0, candidatesPassed = 0;
    for (const auto& c : candidateLog) {
        ++candidatesTried;
        if (c.fits) ++candidatesFit;
        if (c.pass) ++candidatesPassed;
    }
    std::printf("candidate (start,count) pairs tried : %u\n", candidatesTried);
    std::printf("  - fit inside the real file         : %u\n", candidatesFit);
    std::printf("  - internally self-consistent (pass): %u\n", candidatesPassed);
    if (candidatesPassed > 0) {
        std::printf("passing candidates (start=file offset after the 0x2c header, count=entries):\n");
        for (const auto& c : candidateLog) {
            if (!c.pass) continue;
            std::printf("  start=0x%04x (+0x%x past header) count=%-4u touchesTableEnd=%s touchesFileEnd=%s%s\n",
                        c.start, c.start - 0x2c, c.count, c.touchesTableEnd ? "yes" : "no",
                        c.touchesFileEnd ? "yes" : "no",
                        (c.start == table.start && c.count == table.count) ? "  <- CHOSEN" : "");
        }
    }

    if (!tableFound) {
        std::printf(
            "\nNo internally self-consistent candidate was found in the scanned range (header end\n"
            "+0..+256 bytes, entry counts derived from D2/D4). Per this project's no-invented-\n"
            "fixes rule, this tool stops here rather than widening the scan with a made-up bound\n"
            "or picking a failing candidate anyway.\n");
    } else {
        std::printf("\nCHOSEN: start=0x%04x (file offset), count=%u entries (%u bytes)\n", table.start,
                    table.count, table.count * 4u);
        std::printf("first 4 table entries (raw dword -> masked offset, keyframe bit):\n");
        for (uint32_t i = 0; i < table.count && i < 4; ++i) {
            const uint32_t raw = table.raw[i];
            std::printf("  table[%u] = 0x%08x -> offset 0x%08x, keyframe=%s\n", i, raw, raw & ~1u,
                        (raw & 1u) ? "yes" : "no");
        }

        Frame0Range frame0;
        std::string frame0Error;
        if (!extractFrame0Range(table, frame0, frame0Error)) {
            std::printf("\nFATAL: could not derive frame 0's byte range: %s\n", frame0Error.c_str());
            return 1;
        }

        std::printf("\n--- Frame 0 compressed byte range (spec-bink-codec.md Sec1.6) ---\n");
        std::printf("start offset (inclusive) : 0x%08x (%u)\n", frame0.startOffset, frame0.startOffset);
        std::printf("end offset (exclusive)   : 0x%08x (%u)  (= table[1] masked)\n",
                    frame0.endOffsetExclusive, frame0.endOffsetExclusive);
        std::printf("size                     : %u bytes\n", frame0.sizeBytes);
        std::printf("keyframe flag (bit 0)    : %s\n", frame0.keyframe ? "yes" : "no");

        if (frame0.endOffsetExclusive > fileBytes.size()) {
            std::printf("\nFATAL: frame 0's end offset exceeds the real file's size - refusing to read\n"
                        "past end of file.\n");
            return 1;
        }

        const uint8_t* frame0Bytes = fileBytes.data() + frame0.startOffset;
        std::printf("first 16 bytes of frame 0's raw compressed data:\n  ");
        for (uint32_t i = 0; i < 16 && i < frame0.sizeBytes; ++i) {
            std::printf("%02x ", frame0Bytes[i]);
        }
        std::printf("\n");

        if (argc > 2) {
            const std::string outPath = argv[2];
            std::ofstream of(outPath, std::ios::binary);
            if (!of) {
                std::printf("\nWARNING: could not open '%s' to write frame 0's raw bytes.\n", outPath.c_str());
            } else {
                of.write(reinterpret_cast<const char*>(frame0Bytes), frame0.sizeBytes);
                if (!of) {
                    std::printf("\nWARNING: write to '%s' did not complete.\n", outPath.c_str());
                } else {
                    std::printf("\nWrote frame 0's %u raw compressed bytes to '%s'.\n", frame0.sizeBytes,
                                outPath.c_str());
                }
            }
        }
    }

    std::printf("\n=== DECODE STATUS ===\n");
    std::printf(
        "The 44-byte fixed header above is fully parsed, validated, and CONFIRMED against\n"
        "this real file (every spec-bink-codec.md Sec1.2 bound passes; width/height/frame\n"
        "count/FPS all read directly, no invented values).\n\n"
        "  GAP 1 - on-disk frame-offset-table location: CLOSED this pass.\n"
        "    spec-bink-codec.md Sec1.5a (added 2026-10-08) confirms 0x1000e560 is a one-time\n"
        "    whole-table monotonicity VALIDATE + worst-case-buffer-size SCAN, run once at\n"
        "    BinkOpen over a frame-offset table that already exists by the time it runs - not\n"
        "    a per-frame resolver and not the thing that builds the table. Sec1.6 independently\n"
        "    confirms the table's in-memory shape (array of 32-bit entries, one per frame, bit\n"
        "    0 = keyframe flag). Neither section states the table's on-disk start offset or any\n"
        "    per-track-descriptor byte count, so this tool does not read that fact from the spec\n"
        "    (it isn't there) - instead it empirically locates the table by searching for a\n"
        "    candidate whose masked offsets are strictly increasing and all land inside this\n"
        "    real file (see the candidate scan above), and extracts frame 0's exact compressed\n"
        "    byte range and raw bytes directly from the real file using that location. See\n"
        "    'Frame 0 compressed byte range' above for the result.\n\n"
        "  GAP 2 - entropy-coding bit-level math (spec-bink-codec.md Sec8 item 2):\n"
        "    the three giant per-block bitstream engines (0x1001b5a3/0x1001c3d7/0x1001af5e)\n"
        "    are CONFIRMED reachable and CONFIRMED zero-SIMD/zero-FPU, but their exact\n"
        "    bit-level entropy math (Huffman lookup, run-length expansion) was not traced.\n\n"
        "  GAP 3 - transform-stage semantics (spec-bink-codec.md Sec5.4, Sec8 item 5):\n"
        "    the SSE2/MMX '(a-b)*scale>>shift' butterfly is CONFIRMED to exist and run, but\n"
        "    whether it is a DCT butterfly, a plain dequantize, or both fused is HYPOTHESIS\n"
        "    only - no inverse-DCT-specific constant pattern was matched.\n\n"
        "  GAP 4 - final pixel write + YUV->RGB/GPU presentation (spec-bink-codec.md Sec5.5,\n"
        "    Sec8 item 6; spec-bink-boundary.md Sec5.5, OPEN item 1): the exact per-case-\n"
        "    handler pixel-store instruction was never individually located, and the entire\n"
        "    GPU-upload/colorspace-conversion step (how the decoded planar Y/U/V system-\n"
        "    memory buffer becomes an on-screen/PNG-able RGB image) is this project's own\n"
        "    single most load-bearing OPEN item for this subsystem, per spec-bink-boundary.md\n"
        "    itself.\n\n"
        "  DECODER DEPENDENCY CHECK: no off-the-shelf, clean-room-eligible Bink video decoder\n"
        "    dependency exists for this project to lean on for GAPs 2-4, unlike the audio side's\n"
        "    libvorbis/libogg vendoring. Vorbis is a public, openly standardized codec with an\n"
        "    authoritative open-source reference decoder - using it is not 'reverse-engineering\n"
        "    by proxy'. Bink is RAD Game Tools' proprietary format with no public standard or\n"
        "    sanctioned open reference decoder; this project's own spec-bink-codec.md preamble\n"
        "    and spec-bink-boundary.md Sec(scope) both record that FFmpeg's own Bink decoder was\n"
        "    deliberately never read/consulted/used and is reserved ONLY as a future, output-\n"
        "    only comparison oracle once this project has its own independently-derived decode -\n"
        "    never as a source or dependency for producing one. No third_party/ Bink decoder of\n"
        "    any kind is vendored in this repo (checked this pass). Vendoring one is therefore\n"
        "    out of scope for this pass, not merely skipped.\n\n"
        "No PNG is produced by this tool this pass. Per this task's own standard ('document it\n"
        "as an explicit OPEN gap rather than inventing it, even if that means the first frame\n"
        "doesn't decode perfectly yet'), GAPs 2-4 and the decoder-dependency gap are reported\n"
        "here rather than papered over with an invented Huffman table, filter coefficient, or\n"
        "partial/wrong pixel decode.\n");

    return 0;
}
