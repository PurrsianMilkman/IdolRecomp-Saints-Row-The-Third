#pragma once

// Reader for .fxo_pc compiled-shader wrapper files (spec-fxo-format.md).
//
// NOTE (2026-09-20 spec rewrite): this class implements the spec's FIRST-PASS
// "scan-based" recipe (old §4.1). The rewritten spec (§6-§9) decodes the
// header: it is variable-size, H = 0x80 + 0x10*c0 + 8*(c1+c2+c3+c4) +
// 8*(nVS+nMid+nPS) + 0x10*c8, blobs are placed at the running offset rounded
// up to 16 (NOT 8 - the "8-byte alignment" comments below describe the old
// spec), and the two length fields at 0xA8/0xB0 are simply the first entries
// of the vertex/pixel tables. The header-driven reader, which is the primary
// one now, is sr3fxo::WrapperHeader (wrapper_header.h); it was measured to
// consume 1,691 of 1,691 real .fxo_pc/.fxo_pc_dx11 files exactly. This
// scanner is kept unchanged because it needs no header field and its callers
// and tests already depend on it; where it restarts its search at the next
// 8-byte boundary, the difference from 16 is harmless (it only sets where the
// NEXT scan begins, after a blob's end token).
//
// Original scan-only rationale, kept for the record: uses ONLY the spec's
// "scan-based" extraction recipe (§4.1), never the header-length-field
// approach (§4.2) - the two confirmed length fields (offsets 0xA8/0xB0) were
// validated on exactly one sample, and the second (lower-confidence)
// sample's header is 64 bytes longer, meaning those offsets are very likely
// relative-to-shader-start rather than fixed-absolute - unconfirmed either
// way per spec §5 item 1. The scan-based recipe sidesteps that ambiguity
// entirely: it locates shaders using only the PUBLIC, Microsoft-documented
// D3D9 shader version token and end-of-shader token, with no dependency on
// the wrapper header's own (mostly undecoded) fields.
//
// Per spec §1, .fxo_pc is exposed to the same container-format mode-(a)
// non-first-entry corruption issue seen everywhere else in this project
// (shaders.vpp_pc is mode-(a); the spec team itself could only fully
// trust entry 0 for that reason). This reader has no special knowledge of
// that - callers extracting .fxo_pc entries from a mode-(a) container
// should apply the same OkUnconfirmedContent handling used elsewhere
// (see refineWithFxoValidation() below for the content-level corroborator
// that plugs into that flow).

#include <cstdint>
#include <vector>

#include "sr3fxo/errors.h"
#include "vpp/byte_view.h"

namespace sr3fxo {

using vpp::ByteView;

constexpr uint32_t kMagic = 0x4B42A1EEu; // CONFIRMED (spec §2)
constexpr size_t kLeadInSize = 0x08;     // magic(4) + field@0x04(4), CONFIRMED to exist; scanning starts right after this

// One embedded D3D9 shader located by the scan-based recipe. `offset` and
// `length` describe the byte range STARTING at the version token and
// ENDING at (and including) the 4-byte end-of-shader token - per spec §3,
// this is a complete, byte-for-byte intact, independently valid D3D9
// shader blob with no wrapper-specific framing.
struct EmbeddedShader {
    size_t offset = 0;
    size_t length = 0;
    bool isVertexShader = false; // true: 0xFFFE-type version token (vertex); false: 0xFFFF-type (pixel)
    uint8_t versionMajor = 0;
    uint8_t versionMinor = 0;
};

class ShaderWrapper {
public:
    // Parses `bytes`. Throws FormatError only if the magic doesn't match
    // (spec §2, CONFIRMED). Does NOT throw if zero shaders are found, or
    // if scanning stops partway through (e.g. a version token with no
    // matching end token before content ends) - shaders() simply holds
    // however many were successfully located; see the class-level comment
    // for why this format's ambiguity is reported this way rather than by
    // exception.
    static ShaderWrapper parse(ByteView bytes);

    const std::vector<EmbeddedShader>& shaders() const { return shaders_; }

    // Returns the raw bytes of embedded shader `index` (spec §3: complete,
    // independently valid D3D9 bytecode - hand it to any standard D3D9
    // shader disassembler). Throws FormatError for an out-of-range index.
    ByteView shaderBytes(size_t index) const;

private:
    ByteView bytes_;
    std::vector<EmbeddedShader> shaders_;
};

} // namespace sr3fxo
