#pragma once

// sr3agora/agora_value.h - the GDS_value_* tagged-value type (spec-agora-
// hydra-boundary.md Sec1.1), Agora Games' "Hydra SDK" own public value/
// variant type - the payload container the game's ~65 on-demand report
// functions build before handing an array of these to the shared queue
// wrapper (agora_boundary.h's submitReport, Sec4.1).
//
// CONFIRMED (Sec1.1): the 31-export surface is entirely this one type's
// public C API - init/done/copy/setref, get_type/set_type, get_isnull/
// set_isnull, nine get_<kind>/set_<kind> pairs for bool/int32/int64/
// float32/float64/crc16/crc32/data/text, plus three array-level operations
// (encoded_byte_count/encode_values/decode_*).
//
// WHAT IS MODELLED. Sec1.4 (CONFIRMED, direct disassembly of several zero-
// call-site exports): every setter except set_text is a trivial 2-4
// instruction body - "store the tag byte and the raw value, nothing else" -
// hand-inlined at every one of the ~65 report functions' own call sites
// rather than ever actually called as a function. set_text is the one
// setter with real logic: "strlen-or-explicit-length logic, clamped to
// 0xffffff bytes" (Sec1.3's call-site table). Both are modelled below:
// AgoraValue's nine set_<kind> methods store a kind tag + the raw value
// (matching the "nothing else" shape), and setText applies the CONFIRMED
// 0xffffff clamp.
//
// WHAT IS NOT MODELLED, AND WHY (no invented values). The three array-level
// exports (encoded_byte_count/encode_values/decode_*) and the two numeric
// setters this pass DID disassemble a tag byte for (crc32 = 7, set at
// 0x004d1e10; text = 9, inferred from set_text's own tag write, Sec1.4) are
// the ONLY two of the nine kinds whose real internal tag-byte VALUE is
// CONFIRMED. The spec does not give the other seven kinds' tag bytes, and
// none is invented here: AgoraValueKind's own enumerator values below are
// this library's OWN internal bookkeeping only - never claimed to match the
// original binary's byte-level tag encoding, and never serialized to any
// wire format (the real CRC32/varint encode algorithm inside
// GDS_value_encode_values is Hydra-internal, explicitly out of scope per
// the spec's own Sec0 scope limit, and is not reproduced or faked here).
// Nothing in this project's own call contract (agora_boundary.h) needs
// real encoded bytes: the one CONFIRMED gating condition that depends on a
// value array at all is its FIELD COUNT (Sec4.1 step6/7, ">= 3 fields"),
// which AgoraValueArray's own size() already answers exactly.

#include <cstdint>
#include <string>
#include <vector>

namespace sr3agora {

// Sec1.1's nine value kinds, plus Null (the isnull state, Sec1.1's
// get_isnull/set_isnull pair). See the file banner: these enumerator values
// are library-internal only, NOT a claim about the binary's own tag bytes
// (only crc32 and text have a CONFIRMED tag byte, and this type does not
// reuse those two literal numbers for anything observable).
enum class AgoraValueKind {
    Null,
    Bool,
    Int32,
    Int64,
    Float32,
    Float64,
    Crc16,
    Crc32,
    Data,
    Text,
};

// One GDS_value-shaped tagged value. Default-constructed as Null (Sec1.1
// get_isnull's own default for a freshly init'd value, HIGH confidence by
// structure - a zero-initialized tag naturally reads as "no kind set yet").
class AgoraValue {
public:
    AgoraValue() = default;

    AgoraValueKind kind() const { return kind_; }
    bool isNull() const { return kind_ == AgoraValueKind::Null; }
    void setIsNull() { kind_ = AgoraValueKind::Null; }

    // Sec1.4: each setter below is the hand-inlined "store the tag + the
    // raw value, nothing else" shape - no range check, no side effect
    // beyond recording the kind and the value.
    void setBool(bool v) { kind_ = AgoraValueKind::Bool; boolValue_ = v; }
    void setInt32(int32_t v) { kind_ = AgoraValueKind::Int32; int32Value_ = v; }
    void setInt64(int64_t v) { kind_ = AgoraValueKind::Int64; int64Value_ = v; }
    void setFloat32(float v) { kind_ = AgoraValueKind::Float32; float32Value_ = v; }
    void setFloat64(double v) { kind_ = AgoraValueKind::Float64; float64Value_ = v; }
    void setCrc16(uint16_t v) { kind_ = AgoraValueKind::Crc16; crc16Value_ = v; }
    void setCrc32(uint32_t v) { kind_ = AgoraValueKind::Crc32; crc32Value_ = v; }
    void setData(std::vector<uint8_t> v) { kind_ = AgoraValueKind::Data; dataValue_ = std::move(v); }

    // Sec1.3: "real strlen-or-explicit-length logic, clamped to 0xffffff
    // bytes (the one non-trivial setter)." explicitLength < 0 means "use
    // the string's own length" (the strlen branch); explicitLength >= 0 is
    // the explicit-length branch. Either way, the resulting stored length
    // is clamped to kMaxTextBytes.
    void setText(const std::string& text, int32_t explicitLength = -1) {
        kind_ = AgoraValueKind::Text;
        size_t len = explicitLength >= 0 ? static_cast<size_t>(explicitLength) : text.size();
        if (len > kMaxTextBytes) len = kMaxTextBytes;
        textValue_ = text.substr(0, len);
    }

    bool getBool() const { return boolValue_; }
    int32_t getInt32() const { return int32Value_; }
    int64_t getInt64() const { return int64Value_; }
    float getFloat32() const { return float32Value_; }
    double getFloat64() const { return float64Value_; }
    uint16_t getCrc16() const { return crc16Value_; }
    uint32_t getCrc32() const { return crc32Value_; }
    const std::vector<uint8_t>& getData() const { return dataValue_; }
    const std::string& getText() const { return textValue_; }

    // Sec1.3: the clamp literal itself, exposed for tests.
    static constexpr size_t kMaxTextBytes = 0xffffffu;

private:
    AgoraValueKind kind_ = AgoraValueKind::Null;
    bool boolValue_ = false;
    int32_t int32Value_ = 0;
    int64_t int64Value_ = 0;
    float float32Value_ = 0.0f;
    double float64Value_ = 0.0;
    uint16_t crc16Value_ = 0;
    uint32_t crc32Value_ = 0;
    std::vector<uint8_t> dataValue_;
    std::string textValue_;
};

// Sec4.1 step5/step6: "populate the record's fields" / "a value-array
// pointer and a field count" - the game-side caller's own array of built
// AgoraValue fields, handed to the shared queue wrapper.
using AgoraValueArray = std::vector<AgoraValue>;

}  // namespace sr3agora
