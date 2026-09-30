#pragma once

// The version token (spec §2), comment tokens (spec §4) and the overall
// disassembled-shader result (spec §1's "version_token { ... }* end_token"
// shape, walked to completion or to the point it stopped).

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "sr3d3d9bc/tokens.h"

namespace sr3d3d9bc {

// The shader's first DWORD (spec §2).
struct VersionToken {
    uint32_t raw = 0;
    bool isVertexShader = false; // true: high 16 bits 0xFFFE; false: 0xFFFF
    uint8_t major = 0;           // bits [15:8]
    uint8_t minor = 0;           // bits [7:0]
};

// One comment token that was skipped (spec §4), kept for diagnostics only -
// this decoder does not interpret comment payloads beyond the CTAB FourCC
// sniff spec §4's own application note calls out.
struct CommentTokenInfo {
    size_t tokenOffset = 0;
    uint32_t payloadDwords = 0;
    bool looksLikeCtab = false; // payload's first DWORD == the "CTAB" FourCC (spec §4 application note)
};

// How the walk over the token stream ended.
enum class WalkStatus {
    Ok,             // reached the 0x0000FFFF end token (spec §5) exactly, no trailing bytes
    TrailingBytes,  // end token found but bytes remain after it in the buffer
    UnexpectedEnd,  // a token/parameter claimed more DWORDs than remained in the buffer
    NoEndToken,     // the buffer was exhausted without ever encountering the end token
};

struct DisassembledShader {
    VersionToken version;
    std::vector<CommentTokenInfo> comments;
    std::vector<Instruction> instructions;
    WalkStatus status = WalkStatus::NoEndToken;
    size_t endTokenOffset = (std::numeric_limits<size_t>::max)();
    size_t bytesConsumed = 0; // cursor position when the walk stopped, success or otherwise
};

} // namespace sr3d3d9bc
