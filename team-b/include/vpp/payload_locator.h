#pragma once

// -----------------------------------------------------------------------
// PayloadLocator
//
// Computes each directory entry's payload location directly from the now-
// CONFIRMED directory-entry fields (spec §2: +0x08 data offset, +0x0C
// uncompressed size, +0x10 compressed size / kRawSentinel) and the
// confirmed block-chained region layout (spec §1.1, Header::payloadStart).
//
// The +0x08 field is a LOGICAL offset: it locates raw entries physically, but
// for a compressed entry it is only the offline packer's spacing by
// uncompressed size. HANDOFF §9.78 (measured over all 6,431 shipped
// containers) found the two real rules that the earlier "unexplained decode
// failure" was hiding: mode (a) streams sit at payloadStart + the running sum
// of roundup(+0x10, 0x800), and a mode-(b) container is ONE stream from
// payloadStart sliced at the +0x08 logical offsets. The old rule "decoded
// successfully" for most non-first entries only because the output was capped
// at +0x0C: +0x08 usually lands inside some OTHER entry's stream, whose
// prefix has the right length and the wrong content. Container computes the
// physical offsets; this class supplies the per-entry fields and the cheap
// zlib-header sniff kept as a diagnostic.
// -----------------------------------------------------------------------

#include <cstddef>
#include <cstdint>

#include "vpp/byte_view.h"
#include "vpp/format.h"

namespace vpp {

enum class PayloadKind {
    Raw,        // directory entry +0x10 == kRawSentinel (spec §3.1)
    Compressed, // directory entry +0x10 holds a real value (spec §3.1)
};

struct PayloadLocation {
    // PHYSICAL byte offset (from the container's own start) of this entry's
    // bytes. Raw entries: Header::payloadStart() + the +0x08 field. COMPRESSED
    // entries are NOT at +0x08 (HANDOFF §9.78): in a mode-(a) container
    // (flags bit 0x2 clear) the streams are packed one after another, each
    // slot rounded up to 0x800 by its COMPRESSED size (+0x10) when flags bit
    // 0x1 is set - so this is payloadStart + the running sum of those slots,
    // which Container computes (PayloadLocator::locate alone cannot: it sees
    // one entry). In a mode-(b) container (flags bit 0x2 set) it is the
    // start of the single shared stream (payloadStart) - see sharedStream.
    size_t offset = 0;
    // The directory +0x08 field itself, relative to Header::payloadStart().
    // For raw entries this equals offset - payloadStart. For a mode-(b)
    // compressed entry it is the position of this entry's bytes inside the
    // shared DECOMPRESSED stream (packed with no padding: the running sum of
    // the earlier compressed entries' +0x0C). For a mode-(a) compressed entry
    // it is the offline packer's logical spacing and is NOT a physical
    // position; kept for diagnostics.
    size_t logicalOffset = 0;
    // True for a compressed entry of a mode-(b) container: its bytes are the
    // slice [logicalOffset, logicalOffset + decompressedLength) of the one
    // stream that starts at `offset`.
    bool sharedStream = false;
    // For Raw: on-disk byte length, == uncompressedSize (spec §5.1: an
    // entry's reserved slot is sized from its uncompressed size, and raw
    // entries have no separate compressed size). For Compressed: the
    // on-disk compressed byte length from +0x10 - CONFIRMED as a field to
    // read, but per spec §3.2 not guaranteed to bound an independently
    // decodable stream for every entry; see Container::decompressEntry.
    size_t length = 0;
    size_t decompressedLength = 0; // entry's +0x0C field; meaningful for both kinds, authoritative expected output size for Compressed
    PayloadKind kind = PayloadKind::Raw;
};

class PayloadLocator {
public:
    // Directly computes the location for one directory entry - no
    // scanning, no guessing; every value here is read straight from a
    // CONFIRMED field (spec §1.1, §2, §3.1).
    static PayloadLocation locate(const Header& header, const DirectoryEntry& entry);

    // Secondary sanity check only: does the byte pair at `offset` actually
    // look like a valid zlib CMF/FLG header (spec §4's sniff test,
    // repurposed here as a diagnostic rather than a primary locator)?
    static bool looksLikeZlibHeader(ByteView container, size_t offset);
};

} // namespace vpp
