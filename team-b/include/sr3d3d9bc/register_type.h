#pragma once

// Register-type field (spec-d3d9-sm2-sm3-bytecode.md §7): a 5-bit value
// split non-contiguously across a destination/source parameter token's bits
// [30:28] (low 3 bits) and [12:11] (high 2 bits) - §6.1/§6.2's own formula,
// reproduced exactly by decodeRegisterTypeBits() below.

#include <cstdint>

namespace sr3d3d9bc {

// Two numeric values are genuinely overloaded by shader type/version rather
// than naming one thing each - spec §7 flags this explicitly as the thing a
// naive "read the Microsoft Learn prose list in declaration order" reading
// would have missed entirely:
//   3  -> D3DSPR_ADDR (VS) or D3DSPR_TEXTURE (PS)
//   6  -> D3DSPR_TEXCRDOUT (VS < 3.0) or D3DSPR_OUTPUT (VS >= 3.0; reserved for PS)
// Both names are kept as same-valued enumerators (legal in C++) so a caller
// can spell either one. This is a purely STRUCTURAL decoder (see
// disassembler.h) - it does not itself resolve which meaning applies to a
// given register-type-3-or-6 occurrence, since doing so needs the owning
// shader's type/version, which the spec ties the resolution to (§7) but
// which a single parameter token does not carry on its own.
enum class RegisterType : uint32_t {
    Temp = 0,
    Input = 1,
    Const = 2,
    Addr = 3,
    Texture = 3,
    RastOut = 4,
    AttrOut = 5,
    TexCrdOut = 6,
    Output = 6,
    ConstInt = 7,
    ColorOut = 8,
    DepthOut = 9,
    Sampler = 10,
    Const2 = 11,
    Const3 = 12,
    Const4 = 13,
    ConstBool = 14,
    Loop = 15,
    TempFloat16 = 16,
    MiscType = 17,
    Label = 18,
    Predicate = 19,
    // D3DSPR_FORCE_DWORD (spec §7): compiler padding sentinel, never a real
    // encoded 5-bit value. Carried here only for documentation completeness;
    // decodeRegisterTypeBits() can never produce it (it only ever returns a
    // 5-bit value, 0-31).
    ForceDword = 0x7fffffffu,
};

// register_type = (token>>28 & 0x7) | ((token>>11 & 0x3) << 3) - spec
// §6.1/§6.2's own stated formula, shared by destination and source tokens.
inline uint32_t decodeRegisterTypeBits(uint32_t token) {
    return ((token >> 28) & 0x7u) | (((token >> 11) & 0x3u) << 3);
}

// One canonical name per numeric value (spec §7), for diagnostics/
// histograms that have no shader type/version context to resolve the two
// overloaded values with - those report both names joined. Returns nullptr
// for any value spec §7 does not document (20-31; 0x7fffffff is handled
// separately since it can never come from decodeRegisterTypeBits()).
inline const char* registerTypeName(uint32_t rawValue) {
    switch (rawValue) {
        case 0: return "TEMP";
        case 1: return "INPUT";
        case 2: return "CONST";
        case 3: return "ADDR/TEXTURE";
        case 4: return "RASTOUT";
        case 5: return "ATTROUT";
        case 6: return "TEXCRDOUT/OUTPUT";
        case 7: return "CONSTINT";
        case 8: return "COLOROUT";
        case 9: return "DEPTHOUT";
        case 10: return "SAMPLER";
        case 11: return "CONST2";
        case 12: return "CONST3";
        case 13: return "CONST4";
        case 14: return "CONSTBOOL";
        case 15: return "LOOP";
        case 16: return "TEMPFLOAT16";
        case 17: return "MISCTYPE";
        case 18: return "LABEL";
        case 19: return "PREDICATE";
        default: return nullptr;
    }
}

} // namespace sr3d3d9bc
