#include "sr3audio/wwise_bank_id.h"

namespace sr3audio {

bool hasWwiseBankMagic(ByteView content) {
    if (content.size() < 4) return false;
    return content.readU32LE(0) == kWwiseBankMagic;
}

WwiseBankHeaderPrefix readWwiseBankHeaderPrefix(ByteView content) {
    if (content.size() < kWwiseBankHeaderPrefixSize) {
        throw FormatError("content too small to contain a BKHD header prefix: " +
                          std::to_string(content.size()) + " bytes, need " +
                          std::to_string(kWwiseBankHeaderPrefixSize) +
                          " (spec-audio-format.md Sec3)");
    }
    if (!hasWwiseBankMagic(content)) {
        throw FormatError("bad BKHD magic at +0x00: expected bytes 42 4B 48 44 "
                          "(spec-audio-format.md Sec3)");
    }

    WwiseBankHeaderPrefix p;
    p.chunkDataLength = content.readU32LE(0x04);      // public Wwise field, uninterpreted
    p.bankGeneratorVersion = content.readU32LE(0x08); // public Wwise field, uninterpreted
    p.soundBankId = content.readU32LE(0x0C);          // the cross-reference value (Sec5)
    return p;
    // Nothing past +0x10 is read, deliberately - see wwise_bank_id.h.
}

} // namespace sr3audio
