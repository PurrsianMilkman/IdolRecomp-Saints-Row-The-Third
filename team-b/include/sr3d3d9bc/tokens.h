#pragma once

// Destination/source parameter token model (spec-d3d9-sm2-sm3-bytecode.md
// §6) and the one decoded instruction that owns them (§3).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "sr3d3d9bc/opcode.h"
#include "sr3d3d9bc/register_type.h"

namespace sr3d3d9bc {

// Destination write mask, token bits [19:16] - spec §6.1: bit16=X/R,
// bit17=Y/G, bit18=Z/B, bit19=W/A.
struct WriteMask {
    bool x = false, y = false, z = false, w = false;
    uint8_t raw = 0; // the raw 4-bit nibble, for diagnostics
};

// Source swizzle, token bits [23:16] as four 2-bit channel selectors - spec
// §6.2. Each value (0=X, 1=Y, 2=Z, 3=W) names which SOURCE component feeds
// that destination channel.
struct Swizzle {
    uint8_t x = 0, y = 0, z = 0, w = 0;
};

// Source modifier, token bits [27:24] of a source parameter token - spec
// §6.4, cross-checked against _D3DSHADER_PARAM_SRCMOD_TYPE's own numeric
// constants.
enum class SourceModifier : uint8_t {
    None = 0x0,
    Negate = 0x1,
    Bias = 0x2,
    BiasAndNegate = 0x3,
    Sign = 0x4,
    SignAndNegate = 0x5,
    Complement = 0x6,
    X2 = 0x7,
    X2AndNegate = 0x8,
    DivideByZ = 0x9,
    DivideByW = 0xA,
    Abs = 0xB,
    NegateAbs = 0xC,
    LogicalNot = 0xD,
    Reserved0xE = 0xE,
    Reserved0xF = 0xF,
};

// Result modifier, destination token bits [23:20] - spec §6.1, an ORed
// bitmask (more than one bit can be set together).
struct ResultModifier {
    uint8_t raw = 0;
    bool saturate() const { return (raw & 0x1u) != 0; }         // VS
    bool partialPrecision() const { return (raw & 0x2u) != 0; } // PS
    bool centroid() const { return (raw & 0x4u) != 0; }         // PS
};

// The relative-addressing DWORD (spec §6.3): "formatted exactly like a
// [source] parameter token" (it carries a swizzle), present only when the
// parameter token it modifies has its own bit 13 set. Only D3DSPR_ADDR or
// D3DSPR_LOOP are valid register types here per spec; that constraint is
// surfaced for a caller to check, not enforced by this structural decoder.
struct RelativeAddressing {
    uint32_t raw = 0;
    uint32_t registerTypeRaw = 0; // spec §6.1/§6.2 split-field formula
    uint16_t registerNumber = 0;  // bits [10:0]
    Swizzle swizzle;
};

struct DestinationParam {
    uint32_t raw = 0;
    uint32_t registerTypeRaw = 0; // spec §6.1 split-field formula (register_type.h)
    uint16_t registerNumber = 0;  // bits [10:0]
    WriteMask writeMask;
    ResultModifier resultModifier;
    // Result shift scale, bits [27:24] (PS < 2.0 only; reserved/0 for PS
    // 2.0+ and all VS - spec §6.1). Kept as the raw 4-bit value: the spec
    // does not state the numeric mapping from this nibble to an actual
    // shift amount (e.g. two's-complement sign extension), so none is
    // assumed here - see the disassembler's top-level report for this
    // project's judgment-call log.
    uint8_t resultShiftScaleRaw = 0;
    bool hasRelativeAddressing = false; // bit 13 (VS 2.0+ only per spec §6.1; read unconditionally here - see disassembler.cpp)
    std::optional<RelativeAddressing> relativeAddressing;
};

struct SourceParam {
    uint32_t raw = 0;
    uint32_t registerTypeRaw = 0;
    uint16_t registerNumber = 0;
    Swizzle swizzle;
    SourceModifier modifier = SourceModifier::None;
    bool hasRelativeAddressing = false; // bit 13 (PS 3.0+ source / all VS per spec §6.2; read unconditionally here)
    std::optional<RelativeAddressing> relativeAddressing;
};

// The DCL instruction's own one-plain-DWORD token (spec §8). Fields are
// decoded generically by bit position; which ones are semantically live
// depends on what is being declared (the destination parameter's resolved
// register type plus the shader's own type/version, spec §8's table) -
// this structural decoder does not resolve that, it only extracts the bits.
struct DclToken {
    uint32_t raw = 0;
    uint8_t samplerTextureType = 0; // bits [30:27] - live only for a sampler DCL (PS 2.0+)
    uint8_t usage = 0;              // bits [4:0] - D3DDECLUSAGE (spec §8.1), live only for a VS input / PS3.0+ texture DCL
    uint8_t usageIndex = 0;         // bits [19:16] - live alongside `usage`
};

// One decoded instruction (spec §3). `dest`/`sources` are populated
// POSITIONALLY per spec §6's own stated general shape ("most instructions
// ... one destination parameter token and zero or more source parameter
// tokens") - see disassembler.h/.cpp for why this decoder cannot know any
// individual opcode's true operand shape from this spec document alone,
// and how it stays honest about that rather than guessing per opcode.
struct Instruction {
    size_t tokenOffset = 0; // byte offset of the instruction token within the shader blob
    uint32_t rawToken = 0;
    uint32_t opcodeRaw = 0; // bits [15:0]
    Opcode opcode = Opcode::NOP;
    OpcodeClass opcodeClass = OpcodeClass::UnknownOther;
    uint8_t controlBits = 0; // bits [23:16]: opcode-specific (spec §3) - not interpreted here
    uint8_t length = 0;      // bits [27:24]: DWORDs following the instruction token (SM2.0+, spec §3/§9.1)
    bool predicate = false;  // bit 28
    bool coIssue = false;    // bit 30 (PS < 2.0 only; reserved/0 for SM2.0+ and all VS)

    std::optional<DclToken> dcl; // populated only when opcode == DCL (spec §8)

    std::optional<DestinationParam> dest;
    std::vector<SourceParam> sources;
    // The extra predicate source token (spec §3: "an extra predicate source
    // token follows this instruction's normal operands") when `predicate`
    // is set. Taken POSITIONALLY as the last parameter token decoded within
    // this instruction's length-delimited region - the spec ties it to
    // "normal operands" ending, which is opcode-specific and not given here,
    // but "last token before the length-delimited region ends" needs no
    // opcode-specific knowledge and matches the spec's own wording.
    std::optional<SourceParam> predicateSource;

    // DWORDs within this instruction's length-delimited region that did NOT
    // have bit 31 set, so are not valid parameter tokens per spec §6.1/
    // §6.2's own "[31] Always 1" rule - most notably the literal immediate
    // data DEF/DEFB/DEFI instructions carry, whose shape this spec document
    // does not describe (§11: per-opcode semantic detail beyond operand
    // shape was deliberately not transcribed). Recorded raw rather than
    // mis-decoded as a bogus source register.
    std::vector<uint32_t> extraDwords;
};

} // namespace sr3d3d9bc
