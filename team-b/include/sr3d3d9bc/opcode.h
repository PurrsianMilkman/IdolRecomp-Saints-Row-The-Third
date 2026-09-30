#pragma once

// Opcode field (spec-d3d9-sm2-sm3-bytecode.md §9): instruction token bits
// [15:0]. Declared in THREE separate numeric bands, not one sequential run
// - the spec's own §9 flags this as "the single most important correction"
// its header cross-check produced, since the Microsoft Learn prose page
// lists these names in declaration order with no numbers shown.

#include <cstdint>

namespace sr3d3d9bc {

enum class Opcode : uint32_t {
    // Band 1 (0-48, contiguous).
    NOP = 0, MOV = 1, ADD = 2, SUB = 3, MAD = 4, MUL = 5, RCP = 6, RSQ = 7,
    DP3 = 8, DP4 = 9, MIN = 10, MAX = 11, SLT = 12, SGE = 13, EXP = 14,
    LOG = 15, LIT = 16, DST = 17, LRP = 18, FRC = 19, M4x4 = 20, M4x3 = 21,
    M3x4 = 22, M3x3 = 23, M3x2 = 24, CALL = 25, CALLNZ = 26, LOOP = 27,
    RET = 28, ENDLOOP = 29, LABEL = 30, DCL = 31, POW = 32, CRS = 33,
    SGN = 34, ABS = 35, NRM = 36, SINCOS = 37, REP = 38, ENDREP = 39,
    IF = 40, IFC = 41, ELSE = 42, ENDIF = 43, BREAK = 44, BREAKC = 45,
    MOVA = 46, DEFB = 47, DEFI = 48,
    // 49-63: "unassigned" per spec §9 - "not real, not reserved-with-a-name,
    // simply absent from the enum." Deliberately no enumerators here; a
    // value in this range classifies as OpcodeClass::UnknownGap below.

    // Band 2 (64-96, contiguous).
    TEXCOORD = 64, TEXKILL = 65, TEX = 66, TEXBEM = 67, TEXBEML = 68,
    TEXREG2AR = 69, TEXREG2GB = 70, TEXM3x2PAD = 71, TEXM3x2TEX = 72,
    TEXM3x3PAD = 73, TEXM3x3TEX = 74,
    // A real, named-but-reserved-for-internal-use value (spec §9) - a
    // population gate must NOT treat this as "unknown" (spec §10).
    RESERVED0 = 75,
    TEXM3x3SPEC = 76, TEXM3x3VSPEC = 77, EXPP = 78, LOGP = 79, CND = 80,
    DEF = 81, TEXREG2RGB = 82, TEXDP3TEX = 83, TEXM3x2DEPTH = 84,
    TEXDP3 = 85, TEXM3x3 = 86, TEXDEPTH = 87, CMP = 88, BEM = 89,
    DP2ADD = 90, DSX = 91, DSY = 92, TEXLDD = 93, SETP = 94, TEXLDL = 95,
    BREAKP = 96,

    // Band 3: high sentinels, non-contiguous with band 2 and with each
    // other. Spec §9 calls out TWO sentinel-vs-real distinctions this enum
    // preserves: PHASE is a REAL, zero-operand PS-1.4-only instruction
    // ("decode it as an ordinary zero-operand instruction", NOT a
    // comment/end alias), while COMMENT and END alias the comment token
    // (§4) and end token (§5) and are not real instructions at all.
    PHASE = 0xFFFD,
    COMMENT = 0xFFFE,
    END = 0xFFFF,

    // A C-enum-width padding sentinel (spec §9), structurally identical to
    // RegisterType::ForceDword. Never a real encoded opcode - the 16-bit
    // opcode field this decoder actually reads cannot represent it at all -
    // carried here only for documentation completeness.
    FORCE_DWORD = 0x7fffffffu,
};

// How a raw 16-bit opcode value classifies against spec §9's bands and
// §10's population-gate rule. Reserved0 and the three sentinels are all
// "known" for §10's own stated meaning of "zero unknown opcodes" even
// though none of them is an ordinary Band1/Band2 instruction.
enum class OpcodeClass {
    Band1,
    Band2,
    Reserved0,
    SentinelPhase,
    SentinelComment,
    SentinelEnd,
    UnknownGap,   // 49-63: spec §9's explicitly-named gap
    UnknownOther, // anything else - a genuine population-gate finding (spec §10)
};

inline OpcodeClass classifyOpcode(uint32_t rawOpcode16) {
    if (rawOpcode16 <= 48) return OpcodeClass::Band1;
    if (rawOpcode16 >= 49 && rawOpcode16 <= 63) return OpcodeClass::UnknownGap;
    if (rawOpcode16 == 75) return OpcodeClass::Reserved0;
    if (rawOpcode16 >= 64 && rawOpcode16 <= 96) return OpcodeClass::Band2;
    if (rawOpcode16 == 0xFFFDu) return OpcodeClass::SentinelPhase;
    if (rawOpcode16 == 0xFFFEu) return OpcodeClass::SentinelComment;
    if (rawOpcode16 == 0xFFFFu) return OpcodeClass::SentinelEnd;
    return OpcodeClass::UnknownOther;
}

// spec §10: "'Zero unknown opcodes' should mean zero hits outside bands
// 1/2/the sentinels/RESERVED0 - not zero RESERVED0 hits."
inline bool isKnownForPopulationGate(OpcodeClass c) {
    return c != OpcodeClass::UnknownGap && c != OpcodeClass::UnknownOther;
}

// One name per Band1/Band2/sentinel/Reserved0 value (spec §9). Returns
// nullptr for the 49-63 gap or any other unrecognised value.
inline const char* opcodeName(uint32_t rawOpcode16) {
    switch (rawOpcode16) {
        case 0: return "NOP"; case 1: return "MOV"; case 2: return "ADD";
        case 3: return "SUB"; case 4: return "MAD"; case 5: return "MUL";
        case 6: return "RCP"; case 7: return "RSQ"; case 8: return "DP3";
        case 9: return "DP4"; case 10: return "MIN"; case 11: return "MAX";
        case 12: return "SLT"; case 13: return "SGE"; case 14: return "EXP";
        case 15: return "LOG"; case 16: return "LIT"; case 17: return "DST";
        case 18: return "LRP"; case 19: return "FRC"; case 20: return "M4x4";
        case 21: return "M4x3"; case 22: return "M3x4"; case 23: return "M3x3";
        case 24: return "M3x2"; case 25: return "CALL"; case 26: return "CALLNZ";
        case 27: return "LOOP"; case 28: return "RET"; case 29: return "ENDLOOP";
        case 30: return "LABEL"; case 31: return "DCL"; case 32: return "POW";
        case 33: return "CRS"; case 34: return "SGN"; case 35: return "ABS";
        case 36: return "NRM"; case 37: return "SINCOS"; case 38: return "REP";
        case 39: return "ENDREP"; case 40: return "IF"; case 41: return "IFC";
        case 42: return "ELSE"; case 43: return "ENDIF"; case 44: return "BREAK";
        case 45: return "BREAKC"; case 46: return "MOVA"; case 47: return "DEFB";
        case 48: return "DEFI";
        case 64: return "TEXCOORD"; case 65: return "TEXKILL"; case 66: return "TEX";
        case 67: return "TEXBEM"; case 68: return "TEXBEML"; case 69: return "TEXREG2AR";
        case 70: return "TEXREG2GB"; case 71: return "TEXM3x2PAD"; case 72: return "TEXM3x2TEX";
        case 73: return "TEXM3x3PAD"; case 74: return "TEXM3x3TEX"; case 75: return "RESERVED0";
        case 76: return "TEXM3x3SPEC"; case 77: return "TEXM3x3VSPEC"; case 78: return "EXPP";
        case 79: return "LOGP"; case 80: return "CND"; case 81: return "DEF";
        case 82: return "TEXREG2RGB"; case 83: return "TEXDP3TEX"; case 84: return "TEXM3x2DEPTH";
        case 85: return "TEXDP3"; case 86: return "TEXM3x3"; case 87: return "TEXDEPTH";
        case 88: return "CMP"; case 89: return "BEM"; case 90: return "DP2ADD";
        case 91: return "DSX"; case 92: return "DSY"; case 93: return "TEXLDD";
        case 94: return "SETP"; case 95: return "TEXLDL"; case 96: return "BREAKP";
        case 0xFFFD: return "PHASE"; case 0xFFFE: return "COMMENT"; case 0xFFFF: return "END";
        default: return nullptr;
    }
}

} // namespace sr3d3d9bc
