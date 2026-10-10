#include "sr3audio/hirc.h"

#include <sstream>

namespace sr3audio {

namespace {

std::string toHex(uint32_t v) {
    std::ostringstream ss;
    ss << "0x" << std::hex << v;
    return ss.str();
}

} // namespace

std::optional<ByteView> findHircChunk(ByteView bank) {
    constexpr size_t kChunkHeaderSize = 8; // FourCC(4) + u32 size(4) - public Wwise chunk framing
    size_t pos = 0;
    while (pos + kChunkHeaderSize <= bank.size()) {
        uint32_t tag = bank.readU32LE(pos);
        uint32_t size = bank.readU32LE(pos + 4);
        size_t payloadStart = pos + kChunkHeaderSize;
        if (payloadStart + static_cast<size_t>(size) > bank.size()) {
            throw FormatError(
                "plain .bnk_pc chunk at offset " + std::to_string(pos) +
                " (tag " + toHex(tag) + ") declares a payload size (" + std::to_string(size) +
                ") that runs past the end of the supplied bytes (spec-audio-format.md Sec3's "
                "public {FourCC,size,payload} chunk framing)");
        }
        if (tag == kHircChunkTag) {
            return ByteView(bank.data() + payloadStart, size);
        }
        pos = payloadStart + size;
    }
    return std::nullopt;
}

std::vector<HircItem> walkHircItems(ByteView hircChunkPayload) {
    constexpr size_t kItemHeaderSize = 5; // u8 kind + u32 length - Sec8.2.1's own "5-byte read"
    constexpr size_t kIdSize = 4;

    if (hircChunkPayload.size() < 4) {
        throw FormatError(
            "HIRC chunk payload too small to contain its own u32 item count: " +
            std::to_string(hircChunkPayload.size()) + " bytes (spec-audio-runtime.md Sec8.6's "
            "public chunk/item framing)");
    }

    uint32_t numItems = hircChunkPayload.readU32LE(0);
    std::vector<HircItem> items;
    items.reserve(numItems);

    size_t pos = 4;
    for (uint32_t i = 0; i < numItems; ++i) {
        if (pos + kItemHeaderSize > hircChunkPayload.size()) {
            throw FormatError(
                "HIRC item " + std::to_string(i) + " of " + std::to_string(numItems) +
                " ran off the end of the chunk payload before its own 5-byte {kind,length} "
                "header could be read (spec-audio-runtime.md Sec8.2.1)");
        }
        uint8_t kind = hircChunkPayload.at(pos);
        uint32_t length = hircChunkPayload.readU32LE(pos + 1);
        size_t itemStart = pos + kItemHeaderSize;

        if (length < kIdSize) {
            throw FormatError(
                "HIRC item " + std::to_string(i) + " (kind " + std::to_string(kind) +
                ") declares length " + std::to_string(length) +
                ", too short to hold even its own 4-byte declared object id "
                "(spec-audio-runtime.md Sec8.2.3)");
        }
        if (itemStart + static_cast<size_t>(length) > hircChunkPayload.size()) {
            throw FormatError(
                "HIRC item " + std::to_string(i) + " (kind " + std::to_string(kind) +
                ") declares length " + std::to_string(length) +
                " but the chunk payload only has " +
                std::to_string(hircChunkPayload.size() - itemStart) +
                " bytes left (spec-audio-runtime.md Sec8.2.2's per-item advance)");
        }

        HircItem item;
        item.kind = kind;
        item.id = hircChunkPayload.readU32LE(itemStart);
        item.body = hircChunkPayload.subview(itemStart + kIdSize, length - kIdSize);
        items.push_back(item);

        pos = itemStart + length;
    }
    return items;
}

bool isHircKindInUnregisteredFallbackRange(uint8_t kind) {
    return kind >= kHircKindUnregisteredFallbackMin && kind <= kHircKindUnregisteredFallbackMax;
}

SoundSourcePluginKind classifySoundSourcePlugin(const SoundSourceDescriptor& d) {
    uint32_t lowNibble = d.pluginOrCodecTypeId & 0xFu;
    if (lowNibble == 0) return SoundSourcePluginKind::kNone;
    if (lowNibble == 1) return SoundSourcePluginKind::kPlainCodec;
    if (lowNibble == 2 || lowNibble == 5) return SoundSourcePluginKind::kEmbeddedParamBlob;
    return SoundSourcePluginKind::kRejected;
}

SoundSourceDescriptor parseSoundSourceDescriptor(ByteView soundSourceBytes) {
    size_t pos = 0;
    auto need = [&](size_t n) {
        if (pos + n > soundSourceBytes.size()) {
            throw FormatError(
                "Sound source descriptor ran off the end of its own bytes at offset " +
                std::to_string(pos) + " needing " + std::to_string(n) +
                " more byte(s), have " + std::to_string(soundSourceBytes.size()) +
                " total (spec-audio-runtime.md Sec9.8's shared byte-stream parser, "
                "0x00f56ba0)");
        }
    };

    SoundSourceDescriptor d;

    need(4);
    d.pluginOrCodecTypeId = soundSourceBytes.readU32LE(pos); // R1
    pos += 4;

    need(4);
    d.streamTypeSelector = soundSourceBytes.readU32LE(pos); // R2
    pos += 4;

    need(4);
    d.sourceMediaId = soundSourceBytes.readU32LE(pos); // R3
    pos += 4;

    need(4);
    d.sizeOrCompanionId = soundSourceBytes.readU32LE(pos); // R4
    pos += 4;

    // R5/R6: only when R2 != 1 (Sec9.8: "R2==1 skips the extra size pair").
    if (d.streamTypeSelector != 1) {
        d.hasPrefetchFields = true;
        need(4);
        d.extraField1 = soundSourceBytes.readU32LE(pos); // R5
        pos += 4;
        need(4);
        d.extraField2 = soundSourceBytes.readU32LE(pos); // R6
        pos += 4;
    }

    need(1);
    d.sourceFlags = soundSourceBytes.at(pos); // B1
    pos += 1;

    // R8/R9 + blob skip: only when (R1&0xF) is 2 or 5 (the embedded
    // parameter-blob generator source case, Sec9.8's branch structure).
    uint32_t lowNibble = d.pluginOrCodecTypeId & 0xFu;
    if (lowNibble == 2 || lowNibble == 5) {
        d.hasEmbeddedParamBlob = true;
        need(4);
        d.paramBlobSize = soundSourceBytes.readU32LE(pos); // R8
        pos += 4;
        need(4);
        d.paramBlobCompanion = soundSourceBytes.readU32LE(pos); // R9
        pos += 4;
        need(static_cast<size_t>(d.paramBlobSize)); // the blob itself - skipped, never decoded
        pos += d.paramBlobSize;
    }

    return d;
}

SoundSourceDescriptor parseSoundHircItem(const HircItem& item) {
    if (item.kind != kHircKindSound) {
        throw FormatError(
            "parseSoundHircItem() called on a non-Sound HIRC item: kind " +
            std::to_string(item.kind) + ", id " + toHex(item.id) +
            " (expected kind " + std::to_string(kHircKindSound) +
            " - spec-audio-runtime.md Sec9.7)");
    }
    constexpr size_t kSkippedBytes = 4; // Sec9.8: role OPEN, confirmed skipped past the id
    if (item.body.size() < kSkippedBytes) {
        throw FormatError(
            "Sound HIRC item id " + toHex(item.id) + " body is only " +
            std::to_string(item.body.size()) +
            " byte(s), too short to hold even the 4 bytes Sec9.8 says are skipped past the id");
    }
    ByteView sourceBytes = item.body.subview(kSkippedBytes, item.body.size() - kSkippedBytes);
    return parseSoundSourceDescriptor(sourceBytes);
}

ResolvedMedia resolveSoundIdToMedia(const std::vector<HircItem>& hircItems, uint32_t soundObjectId,
                                    const std::vector<MediaBankRecord>& mediaRecords) {
    const HircItem* found = nullptr;
    for (const HircItem& item : hircItems) {
        if (item.kind == kHircKindSound && item.id == soundObjectId) {
            found = &item;
            break;
        }
    }
    if (found == nullptr) {
        throw FormatError(
            "no kind-2 Sound HIRC item with id " + toHex(soundObjectId) +
            " found among the " + std::to_string(hircItems.size()) +
            " item(s) supplied (spec-audio-runtime.md Sec9.7: kind 2 is the dominant Sound class)");
    }

    SoundSourceDescriptor d = parseSoundHircItem(*found);
    SoundSourcePluginKind kind = classifySoundSourcePlugin(d);
    if (kind != SoundSourcePluginKind::kPlainCodec) {
        throw FormatError(
            "Sound HIRC item id " + toHex(soundObjectId) +
            " does not classify as the plain single-id codec/source case (kPlainCodec) this "
            "lookup models - its pluginOrCodecTypeId low nibble is " +
            std::to_string(d.pluginOrCodecTypeId & 0xFu) +
            " (spec-audio-runtime.md Sec9.8's branch structure: 0 = empty slot, 2/5 = embedded "
            "parameter-blob generator source, neither is an embedded-media one-shot SFX)");
    }

    for (size_t i = 0; i < mediaRecords.size(); ++i) {
        if (mediaRecords[i].tag == d.sourceMediaId) {
            ResolvedMedia r;
            r.soundObjectId = soundObjectId;
            r.sourceMediaId = d.sourceMediaId;
            r.mediaRecordIndex = i;
            r.offset = mediaRecords[i].offset;
            r.size = mediaRecords[i].size;
            return r;
        }
    }

    throw FormatError(
        "Sound HIRC item id " + toHex(soundObjectId) + "'s own source/media id (" +
        toHex(d.sourceMediaId) +
        ") matches no record's `tag` field among the " + std::to_string(mediaRecords.size()) +
        " _media.bnk_pc record(s) supplied (spec-audio-format.md Sec4.2 / "
        "spec-audio-runtime.md Sec9.8's worked example: this field is CONFIRMED to equal a real "
        "record's `tag` in 5 of 6 real items checked - the one real exception is reported "
        "honestly there too, not forced)");
}

} // namespace sr3audio
