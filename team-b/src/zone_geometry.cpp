#include "sr3zone/zone_geometry.h"

#include <exception>
#include <unordered_map>

namespace sr3zone {

namespace {

// Manual little-endian read, bounds NOT checked - callers here only ever
// call this after their own loop condition already proved `pos + 4` fits,
// so this avoids ByteView's per-call bounds-check overhead in what is
// otherwise a tight per-4-bytes scan over up to ~2.5 MB.
uint32_t rawReadU32LE(const uint8_t* data, size_t pos) {
    return static_cast<uint32_t>(data[pos]) | (static_cast<uint32_t>(data[pos + 1]) << 8) |
           (static_cast<uint32_t>(data[pos + 2]) << 16) |
           (static_cast<uint32_t>(data[pos + 3]) << 24);
}

size_t alignUp(size_t value, size_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

// Where the 0x70-byte Mesh header starts relative to a 4-aligned anchor.
//
// The pre-header is: u16 version, pad to 4, three u32 fields (check value,
// c-length, g-length), pad to 8 - i.e. `round_up(anchor + 16, 8)`. For a
// 4-aligned anchor that is `anchor + 0x10` when the anchor is 0 (mod 8)
// and `anchor + 0x14` when it is 4 (mod 8), the extra word being a zero
// filler (spec-zone-data-format.md Sec10.1; HANDOFF Sec9.55.2 measured
// 2,596 anchors closing at 0x10, 247 at 0x14, zero ambiguous).
//
// sr3mesh::kHeaderStart's fixed 0x10 is the first of those two cases and
// is left as sr3mesh's default for every other carrier; zones are the
// population where the 4 (mod 8) case has actually been measured, so this
// is the caller that opts in.
size_t headerDisplacementFor(size_t anchor) {
    return alignUp(anchor + 16, 8) - anchor;
}

} // namespace

std::vector<ZoneMeshBlockEntry> ZoneGeometry::locate(ByteView cznContent, ByteView gznContent) {
    std::vector<ZoneMeshBlockEntry> out;

    const uint8_t* gdata = gznContent.data();
    const size_t gsize = gznContent.size();

    // Index of every 16-aligned u32 in the g-file, by value. This is used
    // for ONE thing only: finding segment 0, which spec-zone-data-format.md
    // Sec10.5 confirms is the one segment that is 16-aligned (it starts at
    // byte 0). Every later segment is located by arithmetic, not search -
    // searching for them was the bug this walk replaces, since a later
    // segment lands at whatever 4-aligned offset the running total puts it
    // at and a 16-aligned search reaches only 7.28% of them.
    std::unordered_map<uint32_t, std::vector<size_t>> gIndex;
    if (!gznContent.empty()) {
        for (size_t pos = 0; pos + 4 <= gsize; pos += 16) {
            gIndex[rawReadU32LE(gdata, pos)].push_back(pos);
        }
    }

    const uint8_t* cdata = cznContent.data();
    const size_t csize = cznContent.size();

    // The chain cursor: where the NEXT g-segment is predicted to begin.
    // Starts at 0 (segment 0 begins at byte 0) and advances by each
    // confirmed segment's own declared g-length. `chainPlaced` records
    // whether any segment has been confirmed yet, i.e. whether the cursor
    // is a prediction or still just the initial guess.
    size_t gCursor = 0;
    bool chainPlaced = false;

    for (size_t pos = 0; pos + 4 <= csize; pos += 4) {
        if (rawReadU32LE(cdata, pos) != sr3mesh::kMeshVersion) continue;

        const size_t headerDisp = headerDisplacementFor(pos);

        // Bytes needed to peek the flags byte at pos + headerDisp +
        // kFlagsOffset before attempting a real parse - cheaper than a
        // full parse attempt for the (common) case where `9` is
        // coincidental data, not a real Mesh sub-block version field.
        if (pos + headerDisp + sr3mesh::kFlagsOffset + 1 > csize) continue;

        const uint8_t flags = cdata[pos + headerDisp + sr3mesh::kFlagsOffset];
        const bool wantsGFile = (flags & sr3mesh::kFlagBulkInGFile) != 0;

        if (!wantsGFile) {
            // Inline candidate: one attempt, gContent unused.
            // sr3mesh::MeshBlock::parse() itself enforces the real
            // invariants (check-value open, exact declared-length
            // consumption, trailing bookend) - a coincidental "9" simply
            // fails to parse and is skipped.
            try {
                sr3mesh::MeshBlock block =
                    sr3mesh::MeshBlock::parse(cznContent, pos, ByteView(), 0, headerDisp);
                if (block.bulkInGFile()) continue; // disagreed with the flags peek - not this path
                ZoneMeshBlockEntry entry;
                entry.cznOffset = pos;
                entry.fromGFile = false;
                entry.block = std::move(block);
                out.push_back(std::move(entry));
            } catch (const std::exception&) {
                // Not a real Mesh sub-block at this offset.
            }
            continue;
        }

        // --- g-backed candidate: place it on the segment chain. ---
        if (gznContent.empty()) continue;
        if (pos + sr3mesh::kGLengthOffset + 4 > csize) continue;
        const uint32_t checkValue = rawReadU32LE(cdata, pos + sr3mesh::kCheckValueOffset);
        const uint32_t declaredGLength = rawReadU32LE(cdata, pos + sr3mesh::kGLengthOffset);

        // The confirmation test, applied to a computed offset before it is
        // trusted: the segment is shaped `[u32 tag][payload][u32 copy of
        // the tag]` and is exactly `declaredGLength` bytes long, so both
        // ends must read the block's own check value. This is a
        // CONFIRMATION of an arithmetically derived position, not a
        // discovery mechanism - spec-zone-data-format.md Sec10.5 measures
        // its controls at 0.13% (same tag+length against a different
        // zone's g-file) and 0-0.21% (declared length perturbed).
        // Shape gates applied BEFORE the chain is touched. All three are
        // implied by sr3mesh::MeshBlock::parse()'s own contract, so none
        // of them can reject a block that parse() would have accepted:
        //
        //  * the walk reaches at least 16 (4-byte tag, then align to 16)
        //    before the 4-byte trailing tag, so a real g-length is >= 20;
        //  * the walk aligns to 4 and then adds 4, so a real g-length is
        //    a multiple of 4;
        //  * a check value of 0 makes the bookend test vacuous - any
        //    stretch of zero padding satisfies it - so a segment tagged 0
        //    cannot be confirmed by this reader at all, and admitting one
        //    on a coincidental match would move the chain cursor on no
        //    evidence. Real check values are hash-like (spec Sec7.3).
        //
        // Without these, a coincidental `9` word carrying checkValue 0 and
        // a tiny g-length can "confirm" itself against padding and derail
        // the whole file's chain. One such anchor exists in
        // `sr3_city~s0715~al` at czn+0x1020 (checkValue 0, declared
        // g-length 8, c-length 7).
        const bool shapePlausible = declaredGLength >= 20 &&
                                    (declaredGLength % 4) == 0 && checkValue != 0;

        auto segmentHolds = [&](size_t off) {
            if (!shapePlausible) return false;
            if (off > gsize || static_cast<size_t>(declaredGLength) > gsize - off) return false;
            if (rawReadU32LE(gdata, off) != checkValue) return false;
            return rawReadU32LE(gdata, off + declaredGLength - 4) == checkValue;
        };

        size_t segmentAt = 0;
        bool located = false;

        // 1. The chain prediction: this segment begins exactly where the
        //    previous one ended. Correct for 37,446 of 37,730 blocks in
        //    multi-block files (spec Sec10.5.1 route 1). On the first
        //    g-backed block this is the confirmed start of segment 0,
        //    byte 0.
        if (segmentHolds(gCursor)) {
            segmentAt = gCursor;
            located = true;
        }

        // 2. `~al` ("always loaded") zones pad between segments to the
        //    next 16-byte boundary (spec Sec10.5 sampled "97 of 928 tiling
        //    files use a skip", all `sr3_city~fNNNN~al`; superseded as a
        //    population figure by this project's chain check, 1,002/1,002
        //    tile with 115 padded blocks - spec Sec10.8 conflict note;
        //    whether only `~al` zones pad is OPEN there). One extra
        //    computed position, still not a search.
        if (!located) {
            const size_t padded = alignUp(gCursor, 16);
            if (padded != gCursor && segmentHolds(padded)) {
                segmentAt = padded;
                located = true;
            }
        }

        // 3. Only while the chain has not been established at all: fall
        //    back to the 16-aligned search for segment 0. This is the one
        //    place the old search survives, and it is confirmed correct -
        //    segment 0 is the only 16-aligned segment (Sec10.4). It exists
        //    so a leading run of coincidental `9` words, or a first block
        //    this reader cannot parse, does not cost the whole file.
        if (!located && !chainPlaced) {
            auto it = gIndex.find(checkValue);
            if (it != gIndex.end()) {
                for (size_t candidate : it->second) {
                    if (segmentHolds(candidate)) {
                        segmentAt = candidate;
                        located = true;
                        break;
                    }
                }
            }
        }

        // Nothing confirmed this candidate at any computed position: it is
        // not a valid chain position. Skip it WITHOUT moving the cursor -
        // a coincidental `9` must not be allowed to derail the real chain
        // behind it.
        if (!located) continue;

        // The bookend held, so a real segment of exactly this length does
        // sit here: advance the chain even if this reader's own parse of
        // it fails below, so one unsupported block (e.g. flags bit 2,
        // multi-stream) does not desynchronise every segment after it.
        gCursor = segmentAt + declaredGLength;
        chainPlaced = true;

        try {
            sr3mesh::MeshBlock block =
                sr3mesh::MeshBlock::parse(cznContent, pos, gznContent, segmentAt, headerDisp);
            if (!block.bulkInGFile()) continue; // shouldn't happen given the peek, but be exact
            ZoneMeshBlockEntry entry;
            entry.cznOffset = pos;
            entry.fromGFile = true;
            entry.gznOffset = segmentAt;
            entry.block = std::move(block);
            out.push_back(std::move(entry));
        } catch (const std::exception&) {
            // Confirmed segment, but this reader could not walk it.
        }
    }

    return out;
}

} // namespace sr3zone
