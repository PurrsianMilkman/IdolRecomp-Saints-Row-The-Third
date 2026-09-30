#include "sr3fxo/shader_wrapper.h"

namespace sr3fxo {

namespace {

constexpr uint32_t kVertexTokenHighWord = 0xFFFE0000u; // spec §3: "0xFFFE + major/minor version in the low bytes"
constexpr uint32_t kPixelTokenHighWord = 0xFFFF0000u;  // spec §3: "0xFFFF + version" for a pixel shader
constexpr uint32_t kEndToken = 0x0000FFFFu;             // spec §3: "the standard 4-byte D3D9 end-of-shader token"
// D3D9 shader models only ever ran from 1.0 up to 3.0 (vs/ps_1_1 through
// vs/ps_3_0) - public, documented fact, not proprietary. Confirmed
// necessary empirically: without a MINIMUM bound, scanning real content
// (shaders.vpp_pc) found a spurious 14-byte "pixel shader model 0.0" a
// real shader model of "0.0" doesn't exist in D3D9 at all, so this was a
// coincidental 0xFFFF0000-high-word DWORD elsewhere in the wrapper's own
// (uncertain-size, per spec §5) header region, not a genuine shader.
constexpr uint8_t kMinPlausibleMajorVersion = 1;
constexpr uint8_t kMaxPlausibleMajorVersion = 3;

// Tries to read a D3D9 shader version token at `off`. Returns true and
// fills the outputs if the high word matches a known type AND the
// major/minor bytes are in the plausible D3D9 range - guards against a
// coincidental 0xFFFE/0xFFFF-high-word DWORD elsewhere in the content.
bool tryReadVersionToken(ByteView bytes, size_t off, bool& isVertex, uint8_t& major, uint8_t& minor) {
    if (off + 4 > bytes.size()) return false;
    uint32_t v = bytes.readU32LE(off);
    uint32_t highWord = v & 0xFFFF0000u;
    if (highWord != kVertexTokenHighWord && highWord != kPixelTokenHighWord) return false;
    uint8_t lo = static_cast<uint8_t>(v & 0xFF);
    uint8_t hi = static_cast<uint8_t>((v >> 8) & 0xFF);
    if (hi < kMinPlausibleMajorVersion || hi > kMaxPlausibleMajorVersion) return false;
    isVertex = (highWord == kVertexTokenHighWord);
    major = hi;
    minor = lo;
    return true;
}

} // namespace

ShaderWrapper ShaderWrapper::parse(ByteView bytes) {
    if (bytes.size() < kLeadInSize) {
        throw FormatError("content too small to contain the .fxo_pc lead-in (spec §2)");
    }
    uint32_t magic = bytes.readU32LE(0x00);
    if (magic != kMagic) {
        throw FormatError("bad magic number: expected 0x4B42A1EE (spec §2, CONFIRMED)");
    }

    ShaderWrapper w;
    w.bytes_ = bytes;

    // Scan-based recipe (spec §4.1): starting right after the fixed
    // 8-byte lead-in, scan forward for the next version token; from
    // there, scan forward for the matching end token. Repeat from the
    // next 8-byte-aligned position after that until end of content.
    size_t pos = kLeadInSize;
    while (pos + 4 <= bytes.size()) {
        size_t tokenPos = static_cast<size_t>(-1);
        bool isVertex = false;
        uint8_t major = 0, minor = 0;
        for (size_t i = pos; i + 4 <= bytes.size(); ++i) {
            if (tryReadVersionToken(bytes, i, isVertex, major, minor)) {
                tokenPos = i;
                break;
            }
        }
        if (tokenPos == static_cast<size_t>(-1)) {
            break; // no further shader found
        }

        size_t endTokenPos = static_cast<size_t>(-1);
        for (size_t i = tokenPos + 4; i + 4 <= bytes.size(); ++i) {
            if (bytes.readU32LE(i) == kEndToken) {
                endTokenPos = i;
                break;
            }
        }
        if (endTokenPos == static_cast<size_t>(-1)) {
            break; // version token found but no matching end token before content ends - stop rather than guess
        }

        EmbeddedShader shader;
        shader.offset = tokenPos;
        shader.length = (endTokenPos + 4) - tokenPos;
        shader.isVertexShader = isVertex;
        shader.versionMajor = major;
        shader.versionMinor = minor;
        w.shaders_.push_back(shader);

        // spec §3: "the second shader's start is aligned to an 8-byte
        // boundary" - align up before resuming the scan.
        size_t nextRaw = endTokenPos + 4;
        pos = (nextRaw + 7) / 8 * 8;
    }

    return w;
}

ByteView ShaderWrapper::shaderBytes(size_t index) const {
    if (index >= shaders_.size()) {
        throw FormatError("ShaderWrapper::shaderBytes: index out of range");
    }
    const EmbeddedShader& s = shaders_[index];
    return bytes_.subview(s.offset, s.length);
}

} // namespace sr3fxo
