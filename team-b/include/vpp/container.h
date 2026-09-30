#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vpp/byte_view.h"
#include "vpp/format.h"
#include "vpp/payload_locator.h"

namespace vpp {

// One resolved directory entry: its name (read from the filename table via
// the CONFIRMED +0x00 offset field, spec §2), its name hash (spec §2.2,
// clean-room translation - see hash.h), and where PayloadLocator located
// its payload from the CONFIRMED +0x08/+0x0C/+0x10 fields (spec §2, §3.1).
// `name` is empty when the on-disk +0x00 field is the confirmed
// "no name" sentinel (spec §2/§3.3) rather than a real table offset.
struct Entry {
    std::string name;
    uint32_t nameHash = 0;
    PayloadLocation payload;
};

// Outcome of Container::decompressEntry.
//
// HISTORY, and why most of these enumerators are no longer produced
// (HANDOFF §9.78). Compression is signalled per entry (spec §3.1: +0x10).
// This library used to decode a compressed entry at the directory +0x08
// offset. That is wrong for every compressed entry that is not physically
// first: +0x08 is a LOGICAL offset (the offline packer's spacing by
// uncompressed size), and it usually lands inside some OTHER entry's stream.
// Because the output was capped at +0x0C, that decode "succeeded" with the
// right length and the wrong bytes (a prefix of a neighbour) - the "silent
// corruption" of spec-xtbl-format.md §1 - and ~1,585 entries in shaders alone
// failed outright. Measured over all 6,431 shipped containers, the real rules
// are: mode (a) (flags bit 0x2 clear) streams sit at payloadStart + the
// running sum of round_up(+0x10 or +0x0C, 0x800); a mode-(b) container (bit
// 0x2 set) is ONE stream from payloadStart whose length is exactly the sum of
// the compressed entries' +0x0C, each entry the slice at its +0x08.
// Container now applies those rules, so every entry that decodes returns
// plain Ok (validated against the entry's own +0x0C).
//
// OkUnconfirmedContent, ContentValidated, ContentValidationFailed and
// RecoveredSharedStream(LongChain) are RETAINED so the many tools that name
// them keep compiling, but Container::decompressEntry never returns them any
// more. (content_validation.h can still refine a result you hand it.)
enum class DecodeStatus {
    Ok,                        // produced exactly `decompressedLength` (+0x0C) bytes from the entry's real location (see the rules above). Reaching +0x0C is success whether or not zlib reported a formal stream end: these streams do not reliably carry a checked Adler-32 trailer.
    OkUnconfirmedContent,      // NOT produced by Container any more (HANDOFF §9.78). It used to mark non-first mode-(a) entries decoded at the wrong (+0x08) offset. Retained for the tools that name it and for content_validation.h refinement.
    ContentValidated,          // an OkUnconfirmedContent result that a format-specific structural check (content_validation.h) confirmed. Only produced by a refining function, never by decompressEntry().
    ContentValidationFailed,   // an OkUnconfirmedContent result that a structural check found malformed; `data` cleared. Only produced by a refining function, never by decompressEntry().
    RecoveredSharedStream,     // NOT produced any more: the mode (b) backward-walk heuristic it named was replaced by the exact shared-stream rule (HANDOFF §9.78). Retained so tools naming it keep compiling.
    RecoveredSharedStreamLongChain, // as RecoveredSharedStream: no longer produced.
    NoValidZlibHeaderAtOffset, // mode (a): no valid zlib CMF/FLG header at the entry's physical offset, or the offset lies outside the container.
    ZlibStreamError,           // zlib rejected the stream, ran out of input before producing +0x0C bytes, or (mode b) the container's shared-stream layout invariants do not hold.
    SizeMismatch,              // the stream ended formally but short of +0x0C; `data` holds what was decoded.
};

struct DecompressResult {
    DecodeStatus status = DecodeStatus::Ok;
    std::vector<uint8_t> data; // populated for Ok (and SizeMismatch, with the short output); empty for the error statuses
    std::string diagnostic;    // human-readable explanation, always set when status != Ok
};

// Parses one `.vpp_pc` / `.str2_pc` container. Per spec §5, both use
// exactly the same header/directory/filename-table format - a nested
// `.str2_pc` is just this same parser pointed at a directory entry's
// payload region instead of a top-level file, which is why one class
// handles both (spec §5, "a single recursive parser ... handles both").
// Nesting depth is treated as unbounded (spec §5, two levels directly
// confirmed, nothing suggesting a hard limit).
//
// Container does not own the underlying bytes: the caller must keep the
// backing buffer alive for the lifetime of this object and of any
// Container opened recursively from it via openNested().
class Container {
public:
    // Parses `bytes` as a container starting at its own byte 0. Throws
    // FormatError if the header fails CONFIRMED validation (magic,
    // version range, or the directory-table-size invariant).
    explicit Container(ByteView bytes);

    const Header& header() const { return header_; }
    const std::vector<Entry>& entries() const { return entries_; }

    // Opens directory entry `index` as a nested container: re-runs this
    // same parser over that entry's payload bytes (spec §5). Only valid
    // when entries()[index].payload.kind == PayloadKind::Raw. Throws
    // FormatError if the index is out of range, if the entry is marked
    // compressed (decompress first via decompressEntry(), then construct
    // a Container over the result yourself), or if the raw bytes don't
    // actually validate as a container header (i.e. this entry's raw
    // payload is ordinary leaf content, not a nested container - spec §5
    // notes leaf entries like a lone .xtbl are equally valid).
    Container openNested(size_t index) const;

    // Returns the raw on-disk bytes of directory entry `index` (spec §2:
    // an uncompressed entry's payload, exactly `payload.length` bytes
    // starting at `payload.offset`, both relative to this container's own
    // byte range). Only valid when entries()[index].payload.kind ==
    // PayloadKind::Raw. For leaf content that is NOT itself a nested
    // container (e.g. a raw .xtbl or texture file) - use openNested()
    // instead when it is one. Throws FormatError for an out-of-range
    // index, a Compressed entry (decompress it instead), or a payload
    // range that runs past the end of this container. The returned view
    // aliases this Container's own backing buffer - same lifetime rules
    // as openNested().
    ByteView rawEntryBytes(size_t index) const;

    // Decompresses directory entry `index` with zlib (spec §3: standard
    // RFC 1950 zlib-wrapped DEFLATE, the codec itself used unmodified).
    // Only valid when entries()[index].payload.kind ==
    // PayloadKind::Compressed - throws FormatError for an out-of-range
    // index or a Raw entry (caller/usage errors). An entry that cannot be
    // decoded is NOT an exception: it is reported through the returned
    // DecodeStatus so callers can log/skip it rather than the library
    // crashing or fabricating output.
    //
    // Where the bytes are (HANDOFF §9.78, see DecodeStatus above):
    //   mode (a), flags bit 0x2 clear: an independent stream at
    //     entries()[index].payload.offset (payloadStart + the running slot
    //     sum, NOT the +0x08 field), decoded to exactly +0x0C bytes;
    //   mode (b), flags bit 0x2 set: the entry is a slice of the one shared
    //     stream at payloadStart. The stream is decoded once, on demand, and
    //     its decoded prefix is cached inside this Container (a mutable
    //     cache: a Container is therefore not safe for concurrent
    //     decompressEntry() calls from several threads).
    // Success is plain Ok. Mode (b) additionally requires the container's own
    // layout invariants (see decompressModeB); a container that violates them
    // reports ZlibStreamError rather than guessing a slice.
    DecompressResult decompressEntry(size_t index) const;

private:
    // Mode (a): decodes `index` from its own stream. Does the bounds check,
    // the zlib-header sniff, and the +0x0C-bounded inflate. Never throws.
    DecompressResult decompressModeA(size_t index) const;

    // Mode (b): serves `index` as a slice of the shared stream, growing the
    // cached decoded prefix as needed. Never throws.
    DecompressResult decompressModeB(size_t index) const;

    // Core bounded inflate: decodes starting at `physicalOffset` in
    // bytes_, willing to consume up to `maxInputBytes` bytes of input, and
    // stops as soon as `targetSize` output bytes have been produced (or
    // zlib reports completion/error first). Streams in this format do not
    // reliably carry a checked Adler-32 trailer, so reaching `targetSize` is
    // success whether or not zlib reports a formal stream end.
    DecompressResult inflateToSize(size_t physicalOffset, size_t maxInputBytes,
                                    size_t targetSize) const;

    ByteView bytes_;
    Header header_;
    std::vector<Entry> entries_;

    // Mode (b) state. sharedTotal_ is the sum of the compressed entries'
    // +0x0C (the exact length of the decoded shared stream);
    // sharedLayoutValid_ is true iff every compressed entry's +0x08 equals
    // the running sum of the earlier compressed entries' +0x0C, the packing
    // that makes slicing well defined (6,239/6,239 shipped containers).
    uint64_t sharedTotal_ = 0;
    bool sharedLayoutValid_ = false;
    mutable std::vector<uint8_t> sharedStream_; // decoded prefix of the shared stream
};

} // namespace vpp
