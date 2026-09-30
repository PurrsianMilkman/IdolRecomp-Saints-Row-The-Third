#include "vpp/payload_locator.h"

namespace vpp {

PayloadLocation PayloadLocator::locate(const Header& header, const DirectoryEntry& entry) {
    PayloadLocation loc;
    loc.offset = header.payloadStart() + entry.dataOffset;
    loc.logicalOffset = entry.dataOffset;
    loc.decompressedLength = entry.uncompressedSize;

    // The per-entry sentinel is AUTHORITATIVE for raw-vs-compressed and is
    // deliberately checked without reference to the container's
    // shared-stream flag (spec Sec5.9): the flag describes stream LAYOUT
    // for entries that are compressed, it does not declare that any entry
    // is compressed. Real archives combine "flag set" with "every entry
    // raw" - test-fixtures/preload_anim.vpp_pc is 4,209 raw entries in a
    // flags-0x2 container - so consulting the flag here would break them
    // outright. `header` is used only for payloadStart() arithmetic above.
    if (entry.compressedSizeOrSentinel == kRawSentinel) {
        loc.kind = PayloadKind::Raw;
        loc.length = entry.uncompressedSize;
    } else {
        loc.kind = PayloadKind::Compressed;
        loc.length = entry.compressedSizeOrSentinel;
    }
    return loc;
}

bool PayloadLocator::looksLikeZlibHeader(ByteView container, size_t offset) {
    if (offset + 2 > container.size()) {
        return false;
    }
    uint8_t cmf = container.at(offset);
    uint8_t flg = container.at(offset + 1);
    if ((cmf & 0x0F) != 0x08) {
        return false;
    }
    return ((static_cast<unsigned>(cmf) * 256u + flg) % 31u) == 0u;
}

} // namespace vpp
