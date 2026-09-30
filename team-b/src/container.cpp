#include "vpp/container.h"

#include <algorithm>

#include <zlib.h>

#include "vpp/hash.h"

namespace vpp {

Container::Container(ByteView bytes) : bytes_(bytes) {
    header_ = Header::parse(bytes_);

    size_t nameBase = header_.nameBase();

    entries_.reserve(header_.entryCount);
    for (uint32_t i = 0; i < header_.entryCount; ++i) {
        DirectoryEntry raw = DirectoryEntry::parse(bytes_, i);

        Entry e;
        if (raw.nameOffset == kNoNameSentinel) {
            // spec §2/§3.3, CONFIRMED: 0xFFFFFFFF means "no name" - the
            // loader leaves this as a null pointer rather than resolving
            // it into the name table. This is NOT a valid byte offset;
            // reading the name table at nameBase + 0xFFFFFFFF would run
            // far past the end of the container and throw. Treat as an
            // empty name instead.
            e.name.clear();
        } else {
            size_t nameOff = nameBase + raw.nameOffset;
            size_t nameLen = bytes_.cStringLength(nameOff);
            ByteView nameBytes = bytes_.subview(nameOff, nameLen);
            e.name.assign(reinterpret_cast<const char*>(nameBytes.data()), nameBytes.size());
        }
        e.nameHash = hashFilename(e.name); // hash of "" is well-defined (0) for no-name entries
        e.payload = PayloadLocator::locate(header_, raw);
        entries_.push_back(std::move(e));
    }

    // Physical stream positions (HANDOFF §9.78). The directory +0x08 field
    // locates RAW entries, but for a COMPRESSED entry it is only the offline
    // packer's logical spacing. Measured over every shipped container:
    //  * mode (a) (flags bit 0x2 clear): the streams are laid end to end, each
    //    slot rounded up to 0x800 by its COMPRESSED size (+0x10) when flags
    //    bit 0x1 is set (else by +0x0C) - 4,272/4,272 compressed entries in
    //    all ten shipped mode-(a) containers, against 14 for +0x08;
    //  * mode (b) (bit 0x2 set): one stream from payloadStart; an entry is
    //    the slice [+0x08, +0x08 + +0x0C) of its decoded output, +0x08 being
    //    the packed running sum of the earlier compressed entries' +0x0C -
    //    6,239/6,239 containers, and the stream's decoded length is exactly
    //    the sum of those +0x0C.
    // Not observed in shipped data, so UNVERIFIED: a mode-(a) container that
    // mixes raw and compressed entries (raw entries keep their +0x08 here, and
    // count with +0x0C toward the running sum, as the loader's accumulation
    // does).
    const size_t payloadStart = header_.payloadStart();
    if (header_.isSharedStreamMode()) {
        uint64_t run = 0;
        bool packed = true;
        for (Entry& e : entries_) {
            if (e.payload.kind != PayloadKind::Compressed) continue;
            if (e.payload.logicalOffset != run) packed = false;
            run += e.payload.decompressedLength;
            e.payload.offset = payloadStart;
            e.payload.sharedStream = true;
        }
        sharedTotal_ = run;
        sharedLayoutValid_ = packed;
    } else {
        const bool slotsByCompressedSize = (header_.flagsRaw & kSlotsByCompressedSizeFlag) != 0;
        uint64_t run = 0;
        for (Entry& e : entries_) {
            const bool compressed = e.payload.kind == PayloadKind::Compressed;
            if (compressed) e.payload.offset = payloadStart + static_cast<size_t>(run);
            const uint64_t slot = (compressed && slotsByCompressedSize)
                                      ? e.payload.length
                                      : e.payload.decompressedLength;
            run += roundUpBlock(static_cast<size_t>(slot));
        }
    }
}

Container Container::openNested(size_t index) const {
    if (index >= entries_.size()) {
        throw FormatError("Container::openNested: directory entry index out of range");
    }
    const Entry& entry = entries_[index];
    const PayloadLocation& loc = entry.payload;
    if (loc.kind != PayloadKind::Raw) {
        throw FormatError(
            "entry '" + entry.name +
            "' is marked compressed (directory field +0x10 != sentinel, "
            "spec Sec3.1) - decompress it first via decompressEntry(), "
            "then construct a Container over the result if it turns out "
            "to be a nested container");
    }
    if (loc.offset + loc.length > bytes_.size()) {
        throw FormatError("entry '" + entry.name +
                           "' payload range runs past end of container");
    }
    ByteView sub = bytes_.subview(loc.offset, loc.length);
    return Container(sub); // Container::Container validates the magic/version itself
}

ByteView Container::rawEntryBytes(size_t index) const {
    if (index >= entries_.size()) {
        throw FormatError("Container::rawEntryBytes: directory entry index out of range");
    }
    const Entry& entry = entries_[index];
    const PayloadLocation& loc = entry.payload;
    if (loc.kind != PayloadKind::Raw) {
        throw FormatError("entry '" + entry.name +
                           "' is compressed (directory field +0x10 != sentinel, "
                           "spec Sec3.1) - use decompressEntry() instead");
    }
    if (loc.offset + loc.length > bytes_.size()) {
        throw FormatError("entry '" + entry.name +
                           "' payload range runs past end of container");
    }
    return bytes_.subview(loc.offset, loc.length);
}

DecompressResult Container::decompressEntry(size_t index) const {
    if (index >= entries_.size()) {
        throw FormatError("Container::decompressEntry: directory entry index out of range");
    }
    const PayloadLocation& loc = entries_[index].payload;
    if (loc.kind != PayloadKind::Compressed) {
        throw FormatError(
            "entry '" + entries_[index].name +
            "' is stored raw (directory field +0x10 == sentinel, spec "
            "Sec3.1) - read it directly instead of decompressing");
    }
    return loc.sharedStream ? decompressModeB(index) : decompressModeA(index);
}

DecompressResult Container::decompressModeA(size_t index) const {
    const Entry& entry = entries_[index];
    const PayloadLocation& loc = entry.payload;

    DecompressResult result;

    if (loc.offset + 2 > bytes_.size()) {
        result.status = DecodeStatus::NoValidZlibHeaderAtOffset;
        result.diagnostic = "the computed stream offset for entry '" + entry.name +
                            "' lies outside the container";
        return result;
    }
    if (!PayloadLocator::looksLikeZlibHeader(bytes_, loc.offset)) {
        result.status = DecodeStatus::NoValidZlibHeaderAtOffset;
        result.diagnostic = "no valid zlib CMF/FLG header at the computed stream offset for entry '" +
                            entry.name + "'";
        return result;
    }

    // The stream needs at most +0x10 bytes; a few more are allowed so a
    // stream that runs slightly past its declared size is not cut short.
    const size_t maxInput = std::min(loc.length + 8, bytes_.size() - loc.offset);
    result = inflateToSize(loc.offset, maxInput, loc.decompressedLength);
    if (result.status == DecodeStatus::ZlibStreamError) {
        result.diagnostic = "zlib failed to decode entry '" + entry.name + "' (" + result.diagnostic + ")";
    } else if (result.status == DecodeStatus::SizeMismatch) {
        result.diagnostic = "entry '" + entry.name + "' decompressed to " +
                            std::to_string(result.data.size()) + " bytes, but its +0x0C field says " +
                            std::to_string(loc.decompressedLength);
    }
    return result;
}

DecompressResult Container::decompressModeB(size_t index) const {
    const Entry& entry = entries_[index];
    const PayloadLocation& loc = entry.payload;

    DecompressResult result;
    if (!sharedLayoutValid_) {
        result.status = DecodeStatus::ZlibStreamError;
        result.diagnostic =
            "container is in shared-stream mode (flags bit 0x2) but its compressed entries' +0x08 "
            "offsets are not the packed running sum of the earlier entries' +0x0C, so entry '" +
            entry.name + "' cannot be sliced from the shared stream";
        return result;
    }
    const uint64_t need64 = static_cast<uint64_t>(loc.logicalOffset) + loc.decompressedLength;
    if (need64 > sharedTotal_) {
        result.status = DecodeStatus::ZlibStreamError;
        result.diagnostic = "entry '" + entry.name + "' would extend past the shared stream's total length";
        return result;
    }
    const size_t need = static_cast<size_t>(need64);

    if (sharedStream_.size() < need) {
        if (loc.offset + 2 > bytes_.size()) {
            result.status = DecodeStatus::NoValidZlibHeaderAtOffset;
            result.diagnostic = "the shared stream's start lies outside the container";
            return result;
        }
        if (!PayloadLocator::looksLikeZlibHeader(bytes_, loc.offset)) {
            result.status = DecodeStatus::NoValidZlibHeaderAtOffset;
            result.diagnostic = "no valid zlib CMF/FLG header at the shared stream's start";
            return result;
        }
        // Decode a bigger prefix, growing geometrically so a scan over every
        // entry inflates the stream about twice, not once per entry. Restarts
        // from the stream's beginning each time (a stored z_stream would have
        // to live in the Container; the stream is small next to its cost).
        const uint64_t grown = std::max<uint64_t>({static_cast<uint64_t>(need),
                                                   2ull * sharedStream_.size(), 65536ull});
        const size_t target = static_cast<size_t>(std::min<uint64_t>(sharedTotal_, grown));
        DecompressResult decoded = inflateToSize(loc.offset, bytes_.size() - loc.offset, target);
        if (decoded.status == DecodeStatus::Ok || decoded.status == DecodeStatus::SizeMismatch) {
            sharedStream_ = std::move(decoded.data);
        } else {
            result.status = decoded.status;
            result.diagnostic = "shared stream: " + decoded.diagnostic;
            return result;
        }
        if (sharedStream_.size() < need) {
            result.status = DecodeStatus::SizeMismatch;
            result.diagnostic = "the shared stream ended after " + std::to_string(sharedStream_.size()) +
                                " bytes, before entry '" + entry.name + "' (needs " +
                                std::to_string(need) + ")";
            return result;
        }
    }

    result.status = DecodeStatus::Ok;
    result.data.assign(sharedStream_.begin() + static_cast<std::ptrdiff_t>(loc.logicalOffset),
                       sharedStream_.begin() + static_cast<std::ptrdiff_t>(need));
    return result;
}

DecompressResult Container::inflateToSize(size_t physicalOffset, size_t maxInputBytes,
                                           size_t targetSize) const {
    DecompressResult result;

    z_stream strm{};
    if (inflateInit(&strm) != Z_OK) {
        result.status = DecodeStatus::ZlibStreamError;
        result.diagnostic = "zlib inflateInit failed";
        return result;
    }

    strm.next_in = const_cast<Bytef*>(bytes_.data() + physicalOffset);
    strm.avail_in = static_cast<uInt>(maxInputBytes);

    // Target exactly `targetSize` output bytes. This is deliberately NOT
    // "decode until zlib reports Z_STREAM_END": empirically, against real
    // archive data, entries in this format do not reliably include a
    // valid trailing 4-byte Adler-32 checksum within their confirmed
    // compressed-size (+0x10) byte range - inflate() correctly decodes
    // the full, correct output (verified byte-for-byte against spec §3's
    // own worked example in startup.vpp_pc) but then reports Z_OK ("need
    // more input") rather than Z_STREAM_END, because it's still waiting
    // on trailer bytes this format apparently doesn't provide. The real
    // engine almost certainly stops once it has produced the known
    // uncompressed size, the same way this loop does, rather than
    // requiring a formal checksum-verified stream end.
    std::vector<uint8_t> out(targetSize);

    int ret = Z_OK;
    bool streamError = false;
    std::string errMsg;

    if (targetSize > 0) {
        strm.next_out = out.data();
        strm.avail_out = static_cast<uInt>(targetSize);

        while (strm.total_out < targetSize) {
            ret = inflate(&strm, Z_NO_FLUSH);

            if (ret == Z_STREAM_END) {
                break; // formally complete, possibly short of targetSize - checked below
            }
            if (ret != Z_OK && strm.total_out >= targetSize) {
                // Every requested byte was produced; the error is zlib
                // reading on past the payload. Real entries end their deflate
                // data after +0x0C bytes with a short sync-flush-style tail
                // and then padding, so inflate() reports "invalid stored
                // block lengths" (or a trailer mismatch) IN THE SAME CALL that
                // filled the output. The output is complete and correct.
                break;
            }
            if (ret != Z_OK) {
                // Treat ANY non-Z_OK, non-Z_STREAM_END return as fatal here,
                // not just the three error codes checked previously
                // (Z_STREAM_ERROR/Z_DATA_ERROR/Z_MEM_ERROR). Confirmed via
                // real data (dlc3.vpp_pc): the offset this function is
                // pointed at is only ever a *candidate* zlib stream start -
                // looksLikeZlibHeader's CMF/FLG sniff is loose enough that
                // ordinary compressed bytes occasionally satisfy it by
                // coincidence (this is expected and already handled
                // elsewhere). When that coincidental header also happens to
                // have the FDICT bit set, zlib returns Z_NEED_DICT - and
                // since this format never actually uses a preset
                // dictionary, calling inflate() again on the same state
                // just returns Z_NEED_DICT again forever (avail_in,
                // avail_out, and total_out all stay frozen) rather than
                // erroring out on its own. Z_BUF_ERROR (no forward progress
                // possible) has the same "loop forever if not caught"
                // shape. Bailing out on the first occurrence of either -
                // and of any other code this loop doesn't explicitly know
                // how to make progress on - turns a silent hang into the
                // same graceful ZlibStreamError/mode-(b)-fallback path a
                // normal corrupt stream already takes.
                errMsg = strm.msg ? strm.msg : ("zlib returned unexpected code " + std::to_string(ret));
                streamError = true;
                break;
            }
            if (strm.total_out >= targetSize) {
                // Reached the target byte count in this same call. zlib
                // may still report Z_OK here (not Z_STREAM_END) if it's
                // internally waiting on trailer bytes this format doesn't
                // reliably provide - see the comment above. That's fine:
                // we already have all the real output we need.
                break;
            }
            if (strm.avail_in == 0) {
                errMsg = "input exhausted before producing the target byte count";
                streamError = true;
                break;
            }
        }
    }

    size_t produced = static_cast<size_t>(strm.total_out);
    inflateEnd(&strm);

    if (streamError) {
        result.status = DecodeStatus::ZlibStreamError;
        result.diagnostic = errMsg;
        return result;
    }

    out.resize(produced);

    if (produced != targetSize) {
        result.status = DecodeStatus::SizeMismatch;
        result.data = std::move(out);
        return result;
    }

    result.status = DecodeStatus::Ok;
    result.data = std::move(out);
    return result;
}

} // namespace vpp
