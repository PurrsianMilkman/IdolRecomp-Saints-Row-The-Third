#pragma once

// sr3audioruntime - the Wwise-specific Vorbis wrapping layer on top of the
// RIFF/WAVE container vorbis_container.h already parses (spec-audio-
// runtime.md Sec11.11, unit U-W4c, "The Wwise-specific Vorbis wrapping
// layer: packet framing, header rebuild, codebook storage/selection,
// seek/loop metadata"). This file adds exactly the three CONFIRMED pieces
// Sec11.11 found that vorbis_container.h's own original author never saw
// (that file's own header comment explains why they live here instead of
// there):
//
//   1. parseVorbChunk()  - the proprietary 42-byte "vorb" RIFF chunk
//      (Sec11.11.2). This project copies ALL 42 bytes raw (CONFIRMED
//      mechanism: "a shared RIFF/WAVE chunk walker... does not interpret
//      'vorb' at all - on first seeing the tag it copies exactly 42 bytes
//      raw") and interprets the two fields Sec11.11.2 itself commits to a
//      CONFIRMED identity, at the byte position this project's own direct
//      measurement of real shipped data puts them (see the 2026-10-08
//      correction note below, not Sec11.11.2's own literal "byte 4/5" /
//      "byte 0-3" positions - a real, reported discrepancy, not a silent
//      override). Every other vorb byte (the "~16-23" offset-shaped
//      fields, Sec11.11.2's own table) is surfaced raw and uninterpreted,
//      same practice as media_bank.h's OPEN fields.
//
//      CORRECTION (2026-10-08, this project's own audio bring-up task,
//      empirical, not disassembly): Sec11.11.2's table puts the block-size
//      exponent pair at "vorb-chunk byte 4/5" (decode-state +0x7c/+0x7d,
//      the first two bytes of the 38-byte copy) and the sample-count
//      fallback dword at "byte 0-3, read separately" (the one dword
//      skipped by that copy). Read literally and tested against 800 real
//      "vorb" chunks (400 each from two unrelated, independently-produced
//      archives - cutscene_sounds.vpp_pc and sounds.vpp_pc, one voice
//      content and one general-SFX/content archive, different sample
//      rates and channel counts): byte[4] is NOT a legal exponent in any
//      sample (raw values 0xb0-0xdd, 176-221 decimal - far outside the
//      [6,13] range `0x00fa6370`'s own CONFIRMED check enforces), and the
//      4 bytes immediately preceding the chunk's data in the FILE (this
//      project's original reading of "the separately-read dword") are the
//      chunk's own RIFF size field - constant 0x2a (42) in every sample,
//      not sample-count-shaped at all.
//
//      The SAME 800-sample measurement finds a clean, alternative fit:
//      raw42[40] and raw42[41] (the chunk's OWN LAST two bytes) are
//      perfectly invariant - exactly 0x08/0x0b (block sizes 256/2048, a
//      legal, very ordinary Vorbis I block-size pair) in all 800 samples,
//      across both archives, every sample rate and channel count seen -
//      and raw42[0..3] (the chunk's own FIRST dword, internal to the 42
//      bytes rather than preceding them in the file) varies per sample in
//      a way consistent with a real sample count (e.g. 2,228,448 at
//      48 kHz mono = 46.4s, matching that record's own compressed size).
//      This project therefore reads the block-size exponents from
//      raw42[40]/raw42[41] and the fallback dword from raw42[0..3] below -
//      an empirical correction to Sec11.11.2's literal byte positions,
//      not a rejection of its FIELD IDENTITIES (both fields are exactly
//      what Sec11.11.2 says they are; only their offset within the 42
//      bytes is corrected here, by direct measurement of real shipped
//      data, the same standing practice media_bank.h's own "anchor"
//      correction to Sec4.2 already established for this project). Per
//      this project's own clean-room boundary (Team B never disassembles,
//      never traces FUN_*/0x00fa6370 itself to explain the discrepancy -
//      that is relayed to Team A as a spec-sync question, not solved
//      here): it is left OPEN why `0x00fa6370`'s own disassembly-derived
//      byte positions, as Sec11.11.2 currently states them, do not match
//      this project's own real-data measurement - a reasonable guess is a
//      transcription slip (`+0x7c`/`+0x7d` possibly intended `+0xa0`/
//      `+0xa1`, the actual LAST two bytes of the same 38-byte copy, i.e.
//      vorb-chunk byte 40/41, exactly where this measurement finds them),
//      but this project does not assert that guess as fact.
//
//   2. parseStandardWaveFmt() - the standard, public WAVEFORMATEX "fmt "
//      sub-chunk (channel count + sample rate) vorbis_container.h's own
//      RIFF walker already LOCATES (hasFmt/fmtOffset/fmtSize) but does not
//      itself decode the fields of. This is the ordinary, long-public
//      Microsoft WAVEFORMAT layout, not a Wwise or Volition invention -
//      same "public file-format knowledge" footing vorbis_container.h's own
//      header comment already claims for plain RIFF chunk framing.
//
//   3. walkFramedPackets() - Sec11.11.1's CONFIRMED packet-framing
//      primitive: "a fixed 16-bit little-endian length prefix precedes
//      each compressed packet inline in the 'data' chunk's byte stream -
//      no Ogg page headers, no CRC, no lacing table." Applied to the
//      "data" chunk's own bytes, the first framed unit is the stripped
//      Vorbis setup packet (Sec11.11.3: "reads one more 16-bit-prefixed
//      packet off the same compressed stream... using the same framing");
//      every subsequent framed unit is a compressed audio packet. This
//      walker also reports whether the framing is internally consistent -
//      whether the sum of every (2 + length) span exactly reaches the end
//      of the supplied bytes - the same "does the file's own declared
//      oracle land exactly" check media_bank.h's walk() already applies to
//      the (structurally unrelated, but methodologically identical) media
//      bank record chain.
//
// What this file deliberately does NOT do: decode a single Vorbis bit of
// either the setup packet or an audio packet. That is real_vorbis_decode.h's
// job, using the real, vendored libvorbis - not this file, which only
// recovers byte ranges per Sec11.11's own CONFIRMED container/framing facts.

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "sr3audioruntime/vorbis_container.h"

namespace sr3audioruntime {

// Generic RIFF sub-chunk locator, the same walk vorbis_container.h's own
// parseRiffWaveContainer() already runs, but for a caller-chosen FourCC -
// needed here for "vorb", which that function deliberately does not
// recognise (see this file's own top comment for why the two stay
// independent rather than coupling this addition to already-reviewed code).
// Returns the FIRST matching sub-chunk's {dataOffset, dataSize}, or
// std::nullopt if the buffer is not a valid RIFF/WAVE container or no such
// chunk exists.
inline std::optional<std::pair<size_t, size_t>> findRiffSubChunk(const uint8_t* data, size_t size,
                                                                  uint32_t fourCc) {
    if (data == nullptr || size < 12) return std::nullopt;
    auto tag = [&](size_t off) -> uint32_t {
        return static_cast<uint32_t>(data[off]) | (static_cast<uint32_t>(data[off + 1]) << 8) |
               (static_cast<uint32_t>(data[off + 2]) << 16) | (static_cast<uint32_t>(data[off + 3]) << 24);
    };
    constexpr uint32_t kRIFF = 0x46464952;  // "RIFF"
    constexpr uint32_t kWAVE = 0x45564157;  // "WAVE"
    if (tag(0) != kRIFF || tag(8) != kWAVE) return std::nullopt;

    size_t offset = 12;
    while (offset + 8 <= size) {
        uint32_t chunkTag = tag(offset);
        uint32_t chunkSize = tag(offset + 4);
        size_t chunkDataOffset = offset + 8;
        if (chunkDataOffset > size) break;
        size_t available = size - chunkDataOffset;
        size_t clampedSize = std::min<size_t>(chunkSize, available);
        if (chunkTag == fourCc) return std::make_pair(chunkDataOffset, clampedSize);
        size_t advance = chunkSize + (chunkSize & 1u);
        if (chunkDataOffset + advance <= offset) break;
        offset = chunkDataOffset + advance;
    }
    return std::nullopt;
}

// ASCII "vorb" read as a little-endian u32, same convention
// vorbis_container.h uses for "fmt "/"cue "/"data"/"LIST".
constexpr uint32_t kFourCcVorb = 0x62726f76;

// Sec11.11.2: the 42 raw bytes of the "vorb" chunk, plus the two fields this
// project interprets (block-size exponents) and the sample-count/loop-point
// fallback dword (Sec11.11.5) - both read from WITHIN the 42 bytes, at the
// positions this project's own 2026-10-08 real-data measurement confirms
// (see this file's own top comment's correction note): the fallback dword
// is the chunk's own FIRST 4 bytes (raw42[0..3], "that dword... deliberately
// excluded from the main 38-byte vorb copy"), and the block-size exponents
// are the chunk's own LAST 2 bytes (raw42[40]/raw42[41]) - not byte 4/5 as
// Sec11.11.2's table literally states; see the correction note for the
// 800-real-sample measurement this is based on.
struct WwiseVorbChunk {
    bool present = false;
    std::vector<uint8_t> raw42;  // exact copy, CONFIRMED mechanism

    // raw42[0..3] (empirically corrected position, see top-of-file note).
    // HIGH: "total sample count" (Sec11.11.2/11.11.5) - matches a plausible
    // real duration in every sample checked (e.g. 2,228,448 at 48 kHz mono
    // = 46.4s, consistent with that record's own compressed size).
    uint32_t sampleCountFallbackField = 0;

    // raw42[40]/raw42[41] (empirically corrected position - 100% invariant
    // at 0x08/0x0b across 800 real samples from two unrelated archives, see
    // top-of-file note; NOT Sec11.11.2's literal "+0x7c/+0x7d" <- "vorb-
    // chunk byte 4/5", which byte[4]/byte[5] fail outright on real data).
    // Direct byte values, no nibble masking (0x08 and 0x0b are already
    // legal exponents as full bytes; masking would be a no-op for these two
    // real values anyway, but is deliberately not applied since nothing in
    // this project's own evidence calls for it).
    uint8_t blockSizeExponent0 = 0;
    uint8_t blockSizeExponent1 = 0;
    uint32_t blockSize0 = 0;  // 1 << blockSizeExponent0
    uint32_t blockSize1 = 0;  // 1 << blockSizeExponent1

    // Sec11.11.2: "rejects them unless 64 <= blocksize0 <= blocksize1 <=
    // 8192 - the exact legal range the public Vorbis I spec gives for its
    // identification header's block-size pair." Computed here, not
    // asserted - a caller decides what a failure means for its own
    // purposes (this project's own "measure, don't gate" convention,
    // media_bank.h's blockAlignedTableEnd()/declaredRecordCount() being the
    // precedent for exposing a check as data rather than throwing on it).
    bool blockSizesWithinVorbisSpecRange() const {
        return blockSize0 >= 64 && blockSize0 <= blockSize1 && blockSize1 <= 8192;
    }
};

// Sec11.11.2: copies 42 raw bytes starting at `vorbChunkOffset` (the "vorb"
// chunk's own payload start, as vorbis_container.h's RIFF walker would
// report it if that walker recognised the tag - which it deliberately does
// not, per this file's own header comment). Returns `present=false` (not an
// exception) if `vorbChunkOffset` plus the needed span does not fit within
// `data`/`size` - a missing/short "vorb" chunk is a real, surfaceable
// outcome, not a format violation this project has any CONFIRMED rule to
// reject by.
inline WwiseVorbChunk parseVorbChunk(const uint8_t* data, size_t size, size_t vorbChunkOffset) {
    WwiseVorbChunk out;
    if (data == nullptr) return out;
    if (vorbChunkOffset + 42 > size) return out;

    out.raw42.assign(data + vorbChunkOffset, data + vorbChunkOffset + 42);
    out.sampleCountFallbackField = static_cast<uint32_t>(out.raw42[0]) |
                                    (static_cast<uint32_t>(out.raw42[1]) << 8) |
                                    (static_cast<uint32_t>(out.raw42[2]) << 16) |
                                    (static_cast<uint32_t>(out.raw42[3]) << 24);
    out.blockSizeExponent0 = out.raw42[40];
    out.blockSizeExponent1 = out.raw42[41];
    // Guard against a shift-by->=32 (undefined behaviour): a garbage/non-
    // real "vorb" chunk could carry any byte value here, and this struct
    // makes no claim the exponent is well-formed before the caller checks
    // blockSizesWithinVorbisSpecRange() - 0 safely fails that check either
    // way (0 < the legal minimum, 64), same as any other illegal value.
    out.blockSize0 = out.blockSizeExponent0 < 32 ? (1u << out.blockSizeExponent0) : 0u;
    out.blockSize1 = out.blockSizeExponent1 < 32 ? (1u << out.blockSizeExponent1) : 0u;
    out.present = true;
    return out;
}

// The standard, public Microsoft WAVEFORMAT "fmt " sub-chunk fields this
// project needs (channel count + sample rate, to seed a reconstructed
// Vorbis identification header - real_vorbis_decode.h). Public file-format
// knowledge, not a Wwise/Volition invention.
struct StandardWaveFmt {
    bool present = false;
    uint16_t formatTag = 0;
    uint16_t channels = 0;
    uint32_t samplesPerSec = 0;
    uint32_t avgBytesPerSec = 0;
    uint16_t blockAlign = 0;
    uint16_t bitsPerSample = 0;
};

inline StandardWaveFmt parseStandardWaveFmt(const uint8_t* data, size_t fmtOffset, size_t fmtSize) {
    StandardWaveFmt out;
    if (data == nullptr || fmtSize < 16) return out;
    auto u16 = [&](size_t off) -> uint16_t {
        return static_cast<uint16_t>(data[fmtOffset + off]) | (static_cast<uint16_t>(data[fmtOffset + off + 1]) << 8);
    };
    auto u32 = [&](size_t off) -> uint32_t {
        return static_cast<uint32_t>(data[fmtOffset + off]) | (static_cast<uint32_t>(data[fmtOffset + off + 1]) << 8) |
               (static_cast<uint32_t>(data[fmtOffset + off + 2]) << 16) |
               (static_cast<uint32_t>(data[fmtOffset + off + 3]) << 24);
    };
    out.formatTag = u16(0);
    out.channels = u16(2);
    out.samplesPerSec = u32(4);
    out.avgBytesPerSec = u32(8);
    out.blockAlign = u16(12);
    out.bitsPerSample = fmtSize >= 18 ? u16(14) : 0;
    out.present = true;
    return out;
}

// Sec11.11.1: one length-prefixed unit from the "data" chunk's own packet
// stream - "a fixed 16-bit little-endian length prefix precedes each
// compressed packet inline... no Ogg page headers, no CRC, no lacing
// table." `offset`/`length` are relative to the SAME buffer the caller
// passed to walkFramedPackets() (i.e. already absolute into the full RIFF
// blob, not re-based to 0 at the "data" chunk) so a caller can slice
// directly without re-deriving offsets.
struct FramedPacket {
    size_t offset = 0;
    size_t length = 0;
};

struct FramedPacketWalkResult {
    std::vector<FramedPacket> packets;
    // True only if the sum of every (2 + length) span lands EXACTLY on
    // dataChunkSize with no leftover and no overrun - this project's own
    // "does the file's own declared oracle land exactly" check
    // (media_bank.h's walk() is the precedent this mirrors), applied here
    // to a structurally different but methodologically identical framing
    // rule. A caller gets the packets found so far either way; this flag is
    // the one true/false "was the framing self-consistent" answer.
    bool consistent = false;
    // Set when `consistent` is false: the byte offset (relative to
    // dataChunkOffset) where the walk stopped being able to proceed - a
    // truncated 2-byte length prefix, or a declared length reaching past
    // dataChunkSize.
    size_t stoppedAtOffset = 0;
};

inline FramedPacketWalkResult walkFramedPackets(const uint8_t* data, size_t dataChunkOffset, size_t dataChunkSize) {
    FramedPacketWalkResult out;
    size_t cursor = 0;
    while (true) {
        if (cursor + 2 > dataChunkSize) {
            out.consistent = (cursor == dataChunkSize);
            out.stoppedAtOffset = cursor;
            break;
        }
        const size_t lenFieldOffset = dataChunkOffset + cursor;
        const uint16_t len = static_cast<uint16_t>(data[lenFieldOffset]) |
                              static_cast<uint16_t>(static_cast<uint16_t>(data[lenFieldOffset + 1]) << 8);
        const size_t payloadOffset = dataChunkOffset + cursor + 2;
        if (cursor + 2 + len > dataChunkSize) {
            out.consistent = false;
            out.stoppedAtOffset = cursor;
            break;
        }
        out.packets.push_back(FramedPacket{payloadOffset, len});
        cursor += 2 + len;
        if (cursor == dataChunkSize) {
            out.consistent = true;
            out.stoppedAtOffset = cursor;
            break;
        }
    }
    return out;
}

}  // namespace sr3audioruntime
