// The 16-byte prefix of a plain `<name>.bnk_pc` - just enough of the public
// Audiokinetic Wwise SoundBank layout to recover the SoundBankID that
// spec-audio-format.md Sec5's cross-reference check needs.
//
// THIS IS NOT A WWISE BANK PARSER, AND MUST NOT BECOME ONE. Sec3 of that
// spec is explicit: the plain variant is a standard, publicly-documented
// third-party format, and "no cleanroom RE work is owed to this variant's
// internal chunk structure (`HIRC`, `STID`, `STMG`, etc. are all public
// Wwise chunk types)". The entire reason this file exists is that ONE field
// of that public layout is the other half of a Volition-authored
// cross-reference (the `_media.bnk_pc` wrapper's +0x10 - media_bank.h), and
// verifying that link needs the field's value, nothing else.
//
// The first 16 bytes, per the public `BKHD` chunk layout (Sec3):
//
//   +0x00  4  Magic, ASCII `BKHD` (bytes 42 4B 48 44).       CONFIRMED - empirical
//             Read as a little-endian u32 that is 0x44484B42.
//   +0x04  4  Chunk data length. Samples read 0x18/0x1C and   (public Wwise field)
//             similar small values. Exposed raw; nothing in
//             this project interprets it.
//   +0x08  4  Bank-generator version. Samples read 0x34/0x35  (public Wwise field)
//             (decimal 52/53). Exposed raw.
//   +0x0C  4  SoundBankID. The load-bearing field: CONFIRMED  CONFIRMED - empirical
//             equal to the sibling `_media.bnk_pc` wrapper's
//             own +0x10 cross-reference id in 260/260
//             in-archive pairs, plus one verified
//             cross-archive pair (Sec5).
//
// Sec5 also records that the id is global, not per-archive: `interface`'s
// plain bank ships in `sounds.vpp_pc` while its media companion ships in
// `sounds_common.vpp_pc`, and the two still agree (0x40e182ea). A caller
// pairing banks must therefore NOT assume a bank and its media companion
// live in the same physical `.vpp_pc`.

#pragma once

#include <cstddef>
#include <cstdint>

#include "sr3audio/errors.h"
#include "vpp/byte_view.h"

namespace sr3audio {

using vpp::ByteView;

// ASCII `BKHD` read as a little-endian u32.
constexpr uint32_t kWwiseBankMagic = 0x44484B42u;

// Bytes needed to reach the SoundBankID inclusive. This reader never reads
// past it.
constexpr size_t kWwiseBankHeaderPrefixSize = 16;

// The four u32s of that 16-byte prefix. Only `soundBankId` is used by
// anything in this project; the other two are surfaced so a harness can
// report what it saw without a second read.
struct WwiseBankHeaderPrefix {
    uint32_t chunkDataLength = 0;       // +0x04, public Wwise field, uninterpreted here
    uint32_t bankGeneratorVersion = 0;  // +0x08, public Wwise field, uninterpreted here
    uint32_t soundBankId = 0;           // +0x0C, the cross-reference value (Sec5)
};

// True when the first 4 bytes are the `BKHD` magic. Never throws - false for
// a short buffer. Sec2: every entry across the four mode-(b) archives is
// either this shape or the `VWSBPC` wrapper, 805/805 directory entries.
bool hasWwiseBankMagic(ByteView content);

// Reads the 16-byte prefix. Throws FormatError if `content` is shorter than
// 16 bytes or does not start with the `BKHD` magic. Reads nothing else.
WwiseBankHeaderPrefix readWwiseBankHeaderPrefix(ByteView content);

} // namespace sr3audio
