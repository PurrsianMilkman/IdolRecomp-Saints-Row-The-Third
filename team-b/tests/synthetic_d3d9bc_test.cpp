// Synthetic tests for sr3d3d9bc, built FROM THE TEXT of
// spec-d3d9-sm2-sm3-bytecode.md only - every fixture below is a raw token
// stream HAND-ENCODED bit-by-bit from the spec's own tables (§2/§3/§4/§5/
// §6/§7/§8/§9), never round-tripped through this project's own encoder
// (there isn't one - this is a disassembler only). Mirrors the house style
// of tests/synthetic_tables_environment_test.cpp: hand-rolled fixtures, one
// CHECK per assertion, no reliance on the reader under test to build its
// own inputs.
//
// A pass here proves the decoder implements the spec's bit layouts, not
// that the spec is right or that real shaders look like these - the
// real-data population-gate statement is
// tools/validation/validate_d3d9bc_population.cpp.
//
// Every raw hex constant below was independently computed from the spec's
// own bit tables by a small script (each field placed via the documented
// shift/mask, e.g. §6.1's register_type split: low3 = regtype & 0x7 at
// bits [30:28], high2 = (regtype>>3) & 0x3 at bits [12:11]) and is not
// copied from the decoder under test.

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "sr3d3d9bc/disassembler.h"
#include "sr3d3d9bc/errors.h"
#include "vpp/byte_view.h"

namespace {

using namespace sr3d3d9bc;

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":" << __LINE__ \
                      << "\n";                                                 \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

// Packs a list of 32-bit tokens into a little-endian byte buffer - the only
// helper these tests lean on; every token VALUE was hand-computed from the
// spec's own bit tables (see the generation script referenced above), not
// produced by any code under test.
std::vector<uint8_t> Dwords(std::initializer_list<uint32_t> toks) {
    std::vector<uint8_t> b;
    b.reserve(toks.size() * 4);
    for (uint32_t t : toks) {
        b.push_back(static_cast<uint8_t>(t & 0xFF));
        b.push_back(static_cast<uint8_t>((t >> 8) & 0xFF));
        b.push_back(static_cast<uint8_t>((t >> 16) & 0xFF));
        b.push_back(static_cast<uint8_t>((t >> 24) & 0xFF));
    }
    return b;
}

DisassembledShader Run(const std::vector<uint8_t>& bytes) {
    return disassemble(vpp::ByteView(bytes.data(), bytes.size()));
}

// ===========================================================================
// §2 - version token
// ===========================================================================
void testVersionToken() {
    // vs_3_0: high 16 = 0xFFFE, major=3, minor=0 (spec §2).
    std::vector<uint8_t> vs = Dwords({0xFFFE0300u, 0x0000FFFFu});
    DisassembledShader d = Run(vs);
    CHECK(d.version.isVertexShader == true);
    CHECK(d.version.major == 3 && d.version.minor == 0);
    CHECK(d.status == WalkStatus::Ok);
    CHECK(d.instructions.empty());
    CHECK(d.bytesConsumed == vs.size());

    // ps_2_0: high 16 = 0xFFFF, major=2, minor=0 (spec §2).
    std::vector<uint8_t> ps = Dwords({0xFFFF0200u, 0x0000FFFFu});
    DisassembledShader d2 = Run(ps);
    CHECK(d2.version.isVertexShader == false);
    CHECK(d2.version.major == 2 && d2.version.minor == 0);

    // Precondition violation (spec §2's only unconditional hard rule):
    // neither 0xFFFE nor 0xFFFF in the high 16 bits.
    std::vector<uint8_t> bad = Dwords({0x12340300u, 0x0000FFFFu});
    bool threw = false;
    try {
        Run(bad);
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
}

// ===========================================================================
// §3/§6 - a simple ADD-shaped instruction: dest + 2 sources
// ===========================================================================
void testDestTwoSources() {
    std::vector<uint8_t> bytes = Dwords({
        0xFFFE0300u,  // version: vs_3_0
        0x03000002u,  // instruction: opcode ADD(2), length=3
        0x800F0000u,  // dest: TEMP r0, mask .xyzw
        0x80E40001u,  // src0: TEMP r1, swizzle .xyzw (identity)
        0x81E40002u,  // src1: TEMP r2, swizzle .xyzw, modifier Negate
        0x0000FFFFu,  // end
    });
    DisassembledShader d = Run(bytes);
    CHECK(d.status == WalkStatus::Ok);
    CHECK(d.instructions.size() == 1);
    const Instruction& i = d.instructions[0];
    CHECK(i.opcode == Opcode::ADD);
    CHECK(i.opcodeClass == OpcodeClass::Band1);
    CHECK(i.length == 3);
    CHECK(!i.predicate && !i.coIssue);
    CHECK(i.dest.has_value());
    CHECK(i.dest->registerTypeRaw == 0);  // TEMP
    CHECK(i.dest->registerNumber == 0);
    CHECK(i.dest->writeMask.x && i.dest->writeMask.y && i.dest->writeMask.z && i.dest->writeMask.w);
    CHECK(i.sources.size() == 2);
    CHECK(i.sources[0].registerTypeRaw == 0 && i.sources[0].registerNumber == 1);
    CHECK(i.sources[0].swizzle.x == 0 && i.sources[0].swizzle.y == 1 && i.sources[0].swizzle.z == 2 &&
          i.sources[0].swizzle.w == 3);
    CHECK(i.sources[0].modifier == SourceModifier::None);
    CHECK(i.sources[1].registerNumber == 2);
    CHECK(i.sources[1].modifier == SourceModifier::Negate);
    CHECK(i.extraDwords.empty());
    CHECK(d.bytesConsumed == bytes.size());
}

// ===========================================================================
// §6.1/§6.2/§6.4 - write mask, non-identity swizzle, source/result modifiers
// ===========================================================================
void testWriteMaskSwizzleModifiers() {
    std::vector<uint8_t> bytes = Dwords({
        0xFFFE0300u,
        0x03000005u,  // instruction: opcode MUL(5), length=3
        0x803A0003u,  // dest: TEMP r3, mask .Y_W_ only, Saturate|PartialPrecision
        0x8B1B0004u,  // src0: TEMP r4, swizzle .wzyx, modifier Abs
        0x86E40005u,  // src1: TEMP r5, swizzle .xyzw, modifier Complement
        0x0000FFFFu,
    });
    DisassembledShader d = Run(bytes);
    CHECK(d.status == WalkStatus::Ok);
    CHECK(d.instructions.size() == 1);
    const Instruction& i = d.instructions[0];
    CHECK(i.opcode == Opcode::MUL);

    CHECK(i.dest.has_value());
    // Partial mask: only Y and W (spec §6.1: bit17=Y, bit19=W).
    CHECK(!i.dest->writeMask.x && i.dest->writeMask.y && !i.dest->writeMask.z && i.dest->writeMask.w);
    CHECK(i.dest->writeMask.raw == 0xA);
    CHECK(i.dest->resultModifier.saturate());
    CHECK(i.dest->resultModifier.partialPrecision());
    CHECK(!i.dest->resultModifier.centroid());

    CHECK(i.sources.size() == 2);
    // .wzyx: X-channel reads W(3), Y reads Z(2), Z reads Y(1), W reads X(0).
    CHECK(i.sources[0].swizzle.x == 3 && i.sources[0].swizzle.y == 2 && i.sources[0].swizzle.z == 1 &&
          i.sources[0].swizzle.w == 0);
    CHECK(i.sources[0].modifier == SourceModifier::Abs);
    CHECK(i.sources[1].modifier == SourceModifier::Complement);
}

// ===========================================================================
// §4 - comment token with a CTAB-shaped payload, correctly skipped
// ===========================================================================
void testCommentTokenSkipped() {
    std::vector<uint8_t> bytes = Dwords({
        0xFFFE0300u,
        0x0003FFFEu,  // comment token: low16=0xFFFE, payload = 3 dwords
        0x42415443u,  // "CTAB" fourcc
        0x11111111u,  // opaque payload
        0x22222222u,  // opaque payload
        0x00000000u,  // instruction: NOP, length=0
        0x0000FFFFu,
    });
    DisassembledShader d = Run(bytes);
    CHECK(d.status == WalkStatus::Ok);
    CHECK(d.comments.size() == 1);
    CHECK(d.comments[0].payloadDwords == 3);
    CHECK(d.comments[0].looksLikeCtab);
    CHECK(d.comments[0].tokenOffset == 4);
    // The comment is NOT a decoded instruction - only the NOP that follows is.
    CHECK(d.instructions.size() == 1);
    CHECK(d.instructions[0].opcode == Opcode::NOP);
    CHECK(d.instructions[0].opcodeClass == OpcodeClass::Band1);
    CHECK(d.instructions[0].length == 0);
    CHECK(!d.instructions[0].dest.has_value());
    CHECK(d.instructions[0].sources.empty());
}

// ===========================================================================
// §8 - DCL instruction's distinct shapes
// ===========================================================================
void testDclSampler() {
    // Sampler DCL (PS 2.0+): DCL token bits [30:27] = sampler texture type;
    // destination register type SAMPLER (spec §8's first row, §7 value 10).
    std::vector<uint8_t> bytes = Dwords({
        0xFFFF0200u,  // ps_2_0
        0x0200001Fu,  // instruction: opcode DCL(31), length=2
        0xA8000000u,  // DCL token: samplerTextureType = 5, bit31=1
        0xA00F0800u,  // dest: SAMPLER r0, mask .xyzw
        0x0000FFFFu,
    });
    DisassembledShader d = Run(bytes);
    CHECK(d.status == WalkStatus::Ok);
    CHECK(d.instructions.size() == 1);
    const Instruction& i = d.instructions[0];
    CHECK(i.opcode == Opcode::DCL);
    CHECK(i.dcl.has_value());
    CHECK(i.dcl->samplerTextureType == 5);
    CHECK(i.dest.has_value());
    CHECK(i.dest->registerTypeRaw == 10);  // SAMPLER
    CHECK(i.sources.empty());              // DCL takes no ordinary source parameters (spec §8)
}

void testDclVsInput() {
    // VS 2.0+ input register: DCL token bits [4:0] = D3DDECLUSAGE (§8.1,
    // TEXCOORD=5), bits [19:16] = usage index; destination register type
    // INPUT (spec §8's third row).
    std::vector<uint8_t> bytes = Dwords({
        0xFFFE0300u,  // vs_3_0
        0x0200001Fu,  // instruction: DCL, length=2
        0x80020005u,  // DCL token: usage=TEXCOORD(5), usageIndex=2
        0x90030001u,  // dest: INPUT r1, mask .xy
        0x0000FFFFu,
    });
    DisassembledShader d = Run(bytes);
    CHECK(d.status == WalkStatus::Ok);
    const Instruction& i = d.instructions[0];
    CHECK(i.dcl.has_value());
    CHECK(i.dcl->usage == 5);
    CHECK(i.dcl->usageIndex == 2);
    CHECK(i.dest->registerTypeRaw == 1);  // INPUT
    CHECK(i.dest->writeMask.x && i.dest->writeMask.y && !i.dest->writeMask.z && !i.dest->writeMask.w);
}

void testDclVsOutput() {
    // VS 3.0+ output register: same DCL-token layout as the VS input row,
    // but destination register type is the OUTPUT/TEXCRDOUT-overloaded raw
    // value 6 (spec §7/§8).
    std::vector<uint8_t> bytes = Dwords({
        0xFFFE0300u,
        0x0200001Fu,
        0x8000000Au,  // DCL token: usage=COLOR(10), usageIndex=0
        0xE00F0002u,  // dest: register-type raw 6 (OUTPUT for VS>=3.0), r2, mask .xyzw
        0x0000FFFFu,
    });
    DisassembledShader d = Run(bytes);
    CHECK(d.status == WalkStatus::Ok);
    const Instruction& i = d.instructions[0];
    CHECK(i.dcl->usage == 10);
    CHECK(i.dcl->usageIndex == 0);
    CHECK(i.dest->registerTypeRaw == 6);
    CHECK(std::string(registerTypeName(6)) == "TEXCRDOUT/OUTPUT");
}

void testDclFaceOrPosition() {
    // Face/position register DCL: all DCL-token bits reserved/0 except
    // bit31; destination register type MISCTYPE (spec §7 explicitly names
    // "face/position in PS 3.0 DCL" as this register file's use).
    std::vector<uint8_t> bytes = Dwords({
        0xFFFF0300u,  // ps_3_0
        0x0200001Fu,
        0x80000000u,  // DCL token: all reserved/0
        0x900F1000u,  // dest: MISCTYPE r0, mask .xyzw
        0x0000FFFFu,
    });
    DisassembledShader d = Run(bytes);
    CHECK(d.status == WalkStatus::Ok);
    const Instruction& i = d.instructions[0];
    CHECK(i.dcl->usage == 0 && i.dcl->usageIndex == 0 && i.dcl->samplerTextureType == 0);
    CHECK(i.dest->registerTypeRaw == 17);  // MISCTYPE
}

// ===========================================================================
// §6.3 - relative addressing token, on both a destination and a source
// ===========================================================================
void testRelativeAddressing() {
    std::vector<uint8_t> bytes = Dwords({
        0xFFFE0300u,
        0x04000001u,  // instruction: MOV(1), length=4 (dest+reladdr+src+reladdr)
        0x800F2006u,  // dest: TEMP r6, mask .xyzw, relative-addressing bit set
        0xB0000000u,  // relative addressing for dest: ADDR r0, swizzle .x
        0x80E42007u,  // src: TEMP r7, swizzle .xyzw, relative-addressing bit set
        0xF0000800u,  // relative addressing for src: LOOP r0, swizzle .x
        0x0000FFFFu,
    });
    DisassembledShader d = Run(bytes);
    CHECK(d.status == WalkStatus::Ok);
    CHECK(d.instructions.size() == 1);
    const Instruction& i = d.instructions[0];
    CHECK(i.dest->hasRelativeAddressing);
    CHECK(i.dest->relativeAddressing.has_value());
    CHECK(i.dest->relativeAddressing->registerTypeRaw == 3);  // ADDR
    CHECK(i.dest->relativeAddressing->registerNumber == 0);
    CHECK(i.dest->relativeAddressing->swizzle.x == 0);

    CHECK(i.sources.size() == 1);
    CHECK(i.sources[0].hasRelativeAddressing);
    CHECK(i.sources[0].relativeAddressing.has_value());
    CHECK(i.sources[0].relativeAddressing->registerTypeRaw == 15);  // LOOP
    CHECK(d.bytesConsumed == bytes.size());
}

// ===========================================================================
// §9 - the three sentinel opcodes: PHASE (real), COMMENT (skip), END (stop)
// ===========================================================================
void testSentinels() {
    // PHASE: real, zero-operand instruction - NOT treated as comment/end.
    std::vector<uint8_t> phaseBytes = Dwords({
        0xFFFF0104u,  // ps_1_4
        0x0000FFFDu,  // instruction: PHASE, length=0
        0x0000FFFFu,
    });
    DisassembledShader d = Run(phaseBytes);
    CHECK(d.status == WalkStatus::Ok);
    CHECK(d.instructions.size() == 1);
    CHECK(d.instructions[0].opcode == Opcode::PHASE);
    CHECK(d.instructions[0].opcodeClass == OpcodeClass::SentinelPhase);
    CHECK(!d.instructions[0].dest.has_value());
    CHECK(d.instructions[0].sources.empty());
    CHECK(isKnownForPopulationGate(d.instructions[0].opcodeClass));

    // COMMENT: switches to skip mode, never appears in instructions().
    for (const Instruction& i : d.instructions) CHECK(i.opcode != Opcode::COMMENT);

    // END: terminates exactly at the last dword - no trailing bytes.
    CHECK(d.endTokenOffset == phaseBytes.size() - 4);
    CHECK(d.bytesConsumed == phaseBytes.size());

    // END with trailing garbage after it: reported, not silently accepted.
    std::vector<uint8_t> trailing = Dwords({0xFFFE0300u, 0x0000FFFFu, 0xDEADBEEFu});
    DisassembledShader dt = Run(trailing);
    CHECK(dt.status == WalkStatus::TrailingBytes);
    CHECK(dt.endTokenOffset == 4);
    CHECK(dt.bytesConsumed == 8);

    // A stream that runs out before ever reaching an end token.
    std::vector<uint8_t> noEnd = Dwords({0xFFFE0300u, 0x00000000u /* NOP */});
    DisassembledShader dn = Run(noEnd);
    CHECK(dn.status == WalkStatus::NoEndToken);

    // A token that claims more DWORDs than remain in the buffer.
    std::vector<uint8_t> overrun = Dwords({0xFFFE0300u, 0x03000002u /* ADD, length 3 */, 0x800F0000u});
    DisassembledShader du = Run(overrun);
    CHECK(du.status == WalkStatus::UnexpectedEnd);
}

// ===========================================================================
// §9/§10 - opcode classification against the population-gate bands
// ===========================================================================
void testOpcodeClassification() {
    CHECK(classifyOpcode(0) == OpcodeClass::Band1);
    CHECK(classifyOpcode(48) == OpcodeClass::Band1);
    CHECK(classifyOpcode(49) == OpcodeClass::UnknownGap);
    CHECK(classifyOpcode(63) == OpcodeClass::UnknownGap);
    CHECK(classifyOpcode(64) == OpcodeClass::Band2);
    CHECK(classifyOpcode(74) == OpcodeClass::Band2);
    CHECK(classifyOpcode(75) == OpcodeClass::Reserved0);
    CHECK(classifyOpcode(76) == OpcodeClass::Band2);
    CHECK(classifyOpcode(96) == OpcodeClass::Band2);
    CHECK(classifyOpcode(97) == OpcodeClass::UnknownOther);
    CHECK(classifyOpcode(0xFFFD) == OpcodeClass::SentinelPhase);
    CHECK(classifyOpcode(0xFFFE) == OpcodeClass::SentinelComment);
    CHECK(classifyOpcode(0xFFFF) == OpcodeClass::SentinelEnd);

    CHECK(isKnownForPopulationGate(OpcodeClass::Band1));
    CHECK(isKnownForPopulationGate(OpcodeClass::Band2));
    CHECK(isKnownForPopulationGate(OpcodeClass::Reserved0));
    CHECK(isKnownForPopulationGate(OpcodeClass::SentinelPhase));
    CHECK(isKnownForPopulationGate(OpcodeClass::SentinelComment));
    CHECK(isKnownForPopulationGate(OpcodeClass::SentinelEnd));
    CHECK(!isKnownForPopulationGate(OpcodeClass::UnknownGap));
    CHECK(!isKnownForPopulationGate(OpcodeClass::UnknownOther));

    CHECK(std::string(opcodeName(2)) == "ADD");
    CHECK(std::string(opcodeName(75)) == "RESERVED0");
    CHECK(std::string(opcodeName(0xFFFD)) == "PHASE");
    CHECK(opcodeName(55) == nullptr);
}

// ===========================================================================
// §6.1/§6.2/§7 - the split/overloaded register-type field
// ===========================================================================
void testRegisterTypeSplitField() {
    // register_type = (token>>28 & 0x7) | ((token>>11 & 0x3) << 3) - spec's
    // own formula, exercised directly against hand-encoded tokens.
    CHECK(decodeRegisterTypeBits(0xD00F0800u) == 13);  // CONST4: low3=5,high2=1
    CHECK(decodeRegisterTypeBits(0xB00F1000u) == 19);  // PREDICATE: low3=3,high2=2
    CHECK(decodeRegisterTypeBits(0xB00F0000u) == 3);   // ADDR/TEXTURE: low3=3,high2=0
    CHECK(decodeRegisterTypeBits(0xE00F0000u) == 6);   // TEXCRDOUT/OUTPUT: low3=6,high2=0

    // The two context-overloaded values (spec §7) both resolve, by numeric
    // value alone, to a joined name - this decoder does not itself pick one
    // (see register_type.h).
    CHECK(std::string(registerTypeName(3)) == "ADDR/TEXTURE");
    CHECK(std::string(registerTypeName(6)) == "TEXCRDOUT/OUTPUT");
    CHECK(registerTypeName(20) == nullptr);  // undocumented value (spec §7 only goes to 19)
}

}  // namespace

int main() {
    try {
        testVersionToken();
        testDestTwoSources();
        testWriteMaskSwizzleModifiers();
        testCommentTokenSkipped();
        testDclSampler();
        testDclVsInput();
        testDclVsOutput();
        testDclFaceOrPosition();
        testRelativeAddressing();
        testSentinels();
        testOpcodeClassification();
        testRegisterTypeSplitField();
    } catch (const std::exception& e) {
        std::cerr << "unexpected exception: " << e.what() << "\n";
        return 1;
    }
    if (g_failures) {
        std::cerr << g_failures << " of " << g_checks << " check(s) failed\n";
        return 1;
    }
    std::cout << g_checks << " tests passed.\n";
    return 0;
}
