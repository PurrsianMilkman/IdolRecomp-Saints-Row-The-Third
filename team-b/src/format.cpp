#include "vpp/format.h"

namespace vpp {

Header Header::parse(ByteView container) {
    if (container.size() < kDirectoryOffset) {
        throw FormatError(
            "container too small to contain a fixed header (spec Sec1.1: "
            "header region is 0x800 bytes)");
    }

    uint32_t magic = container.readU32LE(0x000);
    if (magic != kMagic) {
        throw FormatError(
            "bad magic number: expected 0x51890ACE (spec Sec1, CONFIRMED)");
    }

    Header h;
    h.version = container.readU32LE(0x004);
    if (h.version < kVersionMin || h.version > kVersionMax) {
        throw FormatError(
            "unsupported version: loader is confirmed to accept 5-6 "
            "inclusive (spec Sec1); note version 5 was never actually "
            "observed on disk, so its behavior is untested even though "
            "this parser accepts it");
    }

    h.flagsRaw = container.readU32LE(0x14C);
    h.field_0x150_unknown = container.readU32LE(0x150);
    h.entryCount = container.readU32LE(0x154);
    h.totalSize = container.readU32LE(0x158);
    h.directoryTableSize = container.readU32LE(0x15C);
    h.nameTableSize = container.readU32LE(0x160);
    h.field_0x164_unknown = container.readU32LE(0x164);
    // 0x168 is intentionally not read - see the comment on Header in
    // format.h; payloadStart() reconstructs the same information from
    // fields that are already independently confirmed.

    uint64_t expectedDirSize =
        static_cast<uint64_t>(h.entryCount) * kDirectoryEntryStride;
    if (h.directoryTableSize != expectedDirSize) {
        throw FormatError(
            "directory table size does not equal entry_count * 24 - this "
            "is a CONFIRMED invariant per spec Sec1/Sec2 in every sample "
            "checked, so either this archive is malformed/truncated, or it "
            "uses a layout not observed during the spec pass");
    }

    return h;
}

DirectoryEntry DirectoryEntry::parse(ByteView container, size_t entryIndex) {
    size_t off = kDirectoryOffset + entryIndex * kDirectoryEntryStride;
    DirectoryEntry e;
    e.nameOffset = container.readU32LE(off + 0x00);
    e.field_0x04_unknown = container.readU32LE(off + 0x04);
    e.dataOffset = container.readU32LE(off + 0x08);
    e.uncompressedSize = container.readU32LE(off + 0x0C);
    e.compressedSizeOrSentinel = container.readU32LE(off + 0x10);
    // +0x14 (runtime-only back-pointer slot) intentionally not read.
    return e;
}

} // namespace vpp
