// Synthetic tests for sr3d3d9bc::translateToHlsl() (hlsl_translator.h),
// built FROM THE TEXT of spec-d3d9-sm2-sm3-bytecode.md §12 (per-opcode
// semantics)/§13 (CTAB)/§8 (DCL) only - one fixture per opcode this
// translator targets (the 35 real opcodes spec §12 documents), checking
// STRUCTURALLY that the emitted HLSL contains the shape §12's own formula
// says it should (e.g. a MAD instruction's translation contains a
// `mad(...)`-shaped expression with the right operands), not that a real
// HLSL compiler accepts it (that is
// tools/validation/validate_d3d9bc_hlsl_population.cpp's job, over all
// 7,276 real blobs).
//
// Per the brief for this translator, fixtures here are DIRECTLY
// CONSTRUCTED DisassembledShader/ConstantTable/Instruction values (setting
// the already-decoded struct fields sr3d3d9bc::disassemble()/
// readConstantTable() would have produced), not hand-encoded raw token
// bytes run back through the real decoder - this translator's input
// CONTRACT is those two structs, so exercising it directly against
// hand-built values of them is the more direct test of the translator
// itself (disassembler.cpp/ctab.cpp's own bit-level decode is already
// covered by tests/synthetic_d3d9bc_test.cpp and
// tests/synthetic_d3d9bc_ctab_test.cpp). The one exception is the DEF/DEFI
// literal-payload tests, which also hand-place real bytes in a small
// `blob` buffer at the exact offset hlsl_translator.h's own contract says
// the translator reads from - see hlsl_translator.h's top comment for why
// it needs raw blob access at all.
//
// Mirrors the house CHECK-macro style of tests/synthetic_d3d9bc_test.cpp.

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "sr3d3d9bc/ctab.h"
#include "sr3d3d9bc/hlsl_translator.h"
#include "sr3d3d9bc/shader.h"
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

#define CHECK_CONTAINS(hay, needle)                                                                    \
    do {                                                                                                \
        ++g_checks;                                                                                     \
        if ((hay).find((needle)) == std::string::npos) {                                                \
            std::cerr << "CHECK_CONTAINS FAILED at " __FILE__ ":" << __LINE__ << ": expected to find \"" \
                      << (needle) << "\"\n--- full HLSL ---\n"                                           \
                      << (hay) << "\n-----------------\n";                                               \
            ++g_failures;                                                                                \
        }                                                                                                \
    } while (0)

#define CHECK_NOT_CONTAINS(hay, needle)                                                                      \
    do {                                                                                                      \
        ++g_checks;                                                                                            \
        if ((hay).find((needle)) != std::string::npos) {                                                       \
            std::cerr << "CHECK_NOT_CONTAINS FAILED at " __FILE__ ":" << __LINE__ << ": did not expect \"" \
                      << (needle) << "\"\n";                                                                    \
            ++g_failures;                                                                                      \
        }                                                                                                      \
    } while (0)

// ---------------------------------------------------------------------------
// Struct-field builders (spec §6.1/§6.2/§7/§9 field meanings - pure
// value-assignment convenience, no opcode semantics encoded here).
// ---------------------------------------------------------------------------

// register_type.h's own D3DSPR_* raw values, spelled out locally so every
// fixture below is self-contained and readable without cross-referencing
// another header while auditing a test.
constexpr uint32_t RT_TEMP = 0, RT_INPUT = 1, RT_CONST = 2, RT_ADDR = 3, RT_TEXCRDOUT_OR_OUTPUT = 6, RT_CONSTINT = 7,
                    RT_COLOROUT = 8, RT_DEPTHOUT = 9, RT_SAMPLER = 10, RT_CONSTBOOL = 14;

SourceParam mkSrc(uint32_t regType, uint16_t regNum, uint8_t sx = 0, uint8_t sy = 1, uint8_t sz = 2, uint8_t sw = 3,
                   SourceModifier mod = SourceModifier::None) {
    SourceParam s;
    s.registerTypeRaw = regType;
    s.registerNumber = regNum;
    s.swizzle = {sx, sy, sz, sw};
    s.modifier = mod;
    return s;
}

DestinationParam mkDst(uint32_t regType, uint16_t regNum, bool x = true, bool y = true, bool z = true, bool w = true,
                        uint8_t resultModRaw = 0) {
    DestinationParam d;
    d.registerTypeRaw = regType;
    d.registerNumber = regNum;
    d.writeMask.x = x;
    d.writeMask.y = y;
    d.writeMask.z = z;
    d.writeMask.w = w;
    d.writeMask.raw = static_cast<uint8_t>((x ? 1 : 0) | (y ? 2 : 0) | (z ? 4 : 0) | (w ? 8 : 0));
    d.resultModifier.raw = resultModRaw;
    return d;
}

uint32_t rawSourceToken(uint32_t regType, uint16_t regNum, uint8_t sx = 0, uint8_t sy = 1, uint8_t sz = 2,
                         uint8_t sw = 3, SourceModifier mod = SourceModifier::None) {
    uint32_t low3 = regType & 0x7u, high2 = (regType >> 3) & 0x3u;
    uint32_t swiz =
        (static_cast<uint32_t>(sx) & 0x3u) | ((static_cast<uint32_t>(sy) & 0x3u) << 2) |
        ((static_cast<uint32_t>(sz) & 0x3u) << 4) | ((static_cast<uint32_t>(sw) & 0x3u) << 6);
    return 0x80000000u | (low3 << 28) | (high2 << 11) | (static_cast<uint32_t>(mod) << 24) | (swiz << 16) |
           (regNum & 0x7FFu);
}

Instruction mkInst(Opcode op, size_t tokenOffset = 0) {
    Instruction i;
    i.opcode = op;
    i.opcodeRaw = static_cast<uint32_t>(op);
    i.opcodeClass = classifyOpcode(i.opcodeRaw);
    i.tokenOffset = tokenOffset;
    i.length = 0;
    return i;
}

DisassembledShader mkShader(bool isVertex, uint8_t major, uint8_t minor, std::vector<Instruction> instrs) {
    DisassembledShader d;
    d.version.isVertexShader = isVertex;
    d.version.major = major;
    d.version.minor = minor;
    d.status = WalkStatus::Ok;
    d.instructions = std::move(instrs);
    return d;
}

// A generic, empty-CTAB shader (no named constants needed) plus a small
// zeroed blob (large enough for every test that doesn't need real DEF/DEFI
// literal bytes - see hlsl_translator.h's contract for when that matters).
ConstantTable emptyCtab() {
    ConstantTable c;
    c.status = CtabStatus::NotPresent;
    return c;
}

std::vector<uint8_t> zeroBlob(size_t n = 256) { return std::vector<uint8_t>(n, 0); }

TranslationResult translate(const DisassembledShader& shader, const ConstantTable& ctab,
                             const std::vector<uint8_t>& blob) {
    return translateToHlsl(shader, ctab, vpp::ByteView(blob.data(), blob.size()));
}

// ===========================================================================
// One test per opcode §12 documents (35 real opcodes), each checking the
// emitted HLSL contains the shape §12's own formula/HLSL-mapping column
// states.
// ===========================================================================

void testMov() {
    Instruction i = mkInst(Opcode::MOV);
    i.dest = mkDst(RT_TEMP, 0);
    i.sources = {mkSrc(RT_INPUT, 0)};
    auto shader = mkShader(true, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "r0.xyzw = input.v0.xyzw;");
    CHECK(r.complete);
}

void testAdd() {
    Instruction i = mkInst(Opcode::ADD);
    i.dest = mkDst(RT_TEMP, 0, true, true, true, false); // .xyz
    i.sources = {mkSrc(RT_TEMP, 1), mkSrc(RT_TEMP, 2, 0, 1, 2, 3, SourceModifier::Negate)};
    auto shader = mkShader(true, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "r0.xyz = (r1.xyz + (-(r2.xyz)));");
    CHECK(r.complete);
}

void testMad() {
    Instruction i = mkInst(Opcode::MAD);
    i.dest = mkDst(RT_TEMP, 0);
    i.sources = {mkSrc(RT_TEMP, 1), mkSrc(RT_TEMP, 2), mkSrc(RT_TEMP, 3)};
    auto shader = mkShader(true, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "r0.xyzw = mad(r1.xyzw, r2.xyzw, r3.xyzw);");
}

void testMul() {
    Instruction i = mkInst(Opcode::MUL);
    i.dest = mkDst(RT_TEMP, 0);
    i.sources = {mkSrc(RT_TEMP, 1), mkSrc(RT_TEMP, 2)};
    auto shader = mkShader(true, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "r0.xyzw = (r1.xyzw * r2.xyzw);");
}

void testRcp() {
    Instruction i = mkInst(Opcode::RCP);
    i.dest = mkDst(RT_TEMP, 0);
    i.sources = {mkSrc(RT_TEMP, 1, 0, 0, 0, 0)}; // replicate .x
    auto shader = mkShader(true, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "rcp(r1.x)");
}

void testRsq() {
    // spec §12's own flagged-open point: this translator emits the
    // LITERAL rsqrt(x), no abs() (see d3d9bc_hlsl_translator.cpp's top
    // comment) - assert that judgment call structurally.
    Instruction i = mkInst(Opcode::RSQ);
    i.dest = mkDst(RT_TEMP, 0);
    i.sources = {mkSrc(RT_TEMP, 1, 0, 0, 0, 0)};
    auto shader = mkShader(true, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "rsqrt(r1.x)");
    CHECK_NOT_CONTAINS(r.hlsl, "rsqrt(abs(");
}

void testDp3() {
    Instruction i = mkInst(Opcode::DP3);
    i.dest = mkDst(RT_TEMP, 0, true, true, true, false);
    i.sources = {mkSrc(RT_TEMP, 1), mkSrc(RT_TEMP, 2)};
    auto shader = mkShader(true, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "dot(r1.xyzw.xyz, r2.xyzw.xyz)");
}

void testDp4() {
    Instruction i = mkInst(Opcode::DP4);
    i.dest = mkDst(RT_TEMP, 0);
    i.sources = {mkSrc(RT_TEMP, 1), mkSrc(RT_TEMP, 2)};
    auto shader = mkShader(true, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "dot(r1.xyzw, r2.xyzw)");
}

void testMin() {
    Instruction i = mkInst(Opcode::MIN);
    i.dest = mkDst(RT_TEMP, 0);
    i.sources = {mkSrc(RT_TEMP, 1), mkSrc(RT_TEMP, 2)};
    auto shader = mkShader(true, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "min(r1.xyzw, r2.xyzw)");
}

void testMax() {
    Instruction i = mkInst(Opcode::MAX);
    i.dest = mkDst(RT_TEMP, 0);
    i.sources = {mkSrc(RT_TEMP, 1), mkSrc(RT_TEMP, 2)};
    auto shader = mkShader(true, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "max(r1.xyzw, r2.xyzw)");
}

void testSlt() {
    Instruction i = mkInst(Opcode::SLT);
    i.dest = mkDst(RT_TEMP, 0);
    i.sources = {mkSrc(RT_TEMP, 1), mkSrc(RT_TEMP, 2)};
    auto shader = mkShader(true, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "(r1.xyzw < r2.xyzw)");
    CHECK_CONTAINS(r.hlsl, "1.0");
    CHECK_CONTAINS(r.hlsl, "0.0");
}

void testSge() {
    Instruction i = mkInst(Opcode::SGE);
    i.dest = mkDst(RT_TEMP, 0);
    i.sources = {mkSrc(RT_TEMP, 1), mkSrc(RT_TEMP, 2)};
    auto shader = mkShader(true, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "(r1.xyzw >= r2.xyzw)");
}

void testExp() {
    Instruction i = mkInst(Opcode::EXP);
    i.dest = mkDst(RT_TEMP, 0);
    i.sources = {mkSrc(RT_TEMP, 1, 0, 0, 0, 0)};
    auto shader = mkShader(true, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "exp2(r1.x)");
}

void testLog() {
    Instruction i = mkInst(Opcode::LOG);
    i.dest = mkDst(RT_TEMP, 0);
    i.sources = {mkSrc(RT_TEMP, 1, 0, 0, 0, 0)};
    auto shader = mkShader(true, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "log2(r1.x)");
}

void testLrp() {
    // spec §12: dest = src0*src1 + (1-src0)*src2 == HLSL lerp(src2, src1,
    // src0) - MS's own argument order is transposed from lerp's (a,b,t);
    // assert the translator actually emits the CORRECT (transposed) order,
    // not the naive reading.
    Instruction i = mkInst(Opcode::LRP);
    i.dest = mkDst(RT_TEMP, 0);
    i.sources = {mkSrc(RT_TEMP, 1) /*proportion*/, mkSrc(RT_TEMP, 2) /*"to"*/, mkSrc(RT_TEMP, 3) /*"from"*/};
    auto shader = mkShader(true, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "lerp(r3.xyzw, r2.xyzw, r1.xyzw)");
}

void testFrc() {
    Instruction i = mkInst(Opcode::FRC);
    i.dest = mkDst(RT_TEMP, 0);
    i.sources = {mkSrc(RT_TEMP, 1)};
    auto shader = mkShader(true, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "frac(r1.xyzw)");
}

void testPow() {
    Instruction i = mkInst(Opcode::POW);
    i.dest = mkDst(RT_TEMP, 0);
    i.sources = {mkSrc(RT_TEMP, 1, 0, 0, 0, 0), mkSrc(RT_TEMP, 2, 1, 1, 1, 1)};
    auto shader = mkShader(true, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "pow(r1.x, r2.y)");
}

void testAbs() {
    Instruction i = mkInst(Opcode::ABS);
    i.dest = mkDst(RT_TEMP, 0);
    i.sources = {mkSrc(RT_TEMP, 1)};
    auto shader = mkShader(true, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "r0.xyzw = abs(r1.xyzw);");
}

void testNrm() {
    // spec §12: quoted as fact ("Normalizes a 4-D vector") - full 4-D
    // source, sliced by the dest write mask.
    Instruction i = mkInst(Opcode::NRM);
    i.dest = mkDst(RT_TEMP, 0, true, true, true, false); // .xyz
    i.sources = {mkSrc(RT_TEMP, 1)};
    auto shader = mkShader(true, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "normalize(r1.xyzw).xyz");
}

void testCmp() {
    Instruction i = mkInst(Opcode::CMP);
    i.dest = mkDst(RT_TEMP, 0);
    i.sources = {mkSrc(RT_TEMP, 1), mkSrc(RT_TEMP, 2), mkSrc(RT_TEMP, 3)};
    auto shader = mkShader(false, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "(r1.xyzw >= 0.0) ? (r2.xyzw) : (r3.xyzw)");
}

void testDp2add() {
    Instruction i = mkInst(Opcode::DP2ADD);
    i.dest = mkDst(RT_TEMP, 0);
    i.sources = {mkSrc(RT_TEMP, 1), mkSrc(RT_TEMP, 2), mkSrc(RT_TEMP, 3, 2, 2, 2, 2)};
    auto shader = mkShader(false, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "dot(r1.xyzw.xy, r2.xyzw.xy)");
    CHECK_CONTAINS(r.hlsl, "r3.z"); // selected replicate component (swizzle.x==2 -> 'z')
}

void testSincos() {
    Instruction i = mkInst(Opcode::SINCOS, 100);
    i.dest = mkDst(RT_TEMP, 0, true, true, false, false); // .xy
    i.sources = {mkSrc(RT_TEMP, 1, 0, 0, 0, 0)};
    auto shader = mkShader(true, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "sincos(r1.x,");
    CHECK_CONTAINS(r.hlsl, ".x = _sc_c");
    CHECK_CONTAINS(r.hlsl, ".y = _sc_s");
    CHECK(r.complete);
}

void testMova() {
    Instruction i = mkInst(Opcode::MOVA);
    i.dest = mkDst(RT_ADDR, 0, true, false, false, false); // .x only
    i.sources = {mkSrc(RT_TEMP, 1)};
    auto shader = mkShader(true, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "a0.x = (int)round(r1.x);");
}

void testDef() {
    // Literal float4(1,2,3,4) placed at tokenOffset+8..+23 - see
    // hlsl_translator.h's contract.
    Instruction def = mkInst(Opcode::DEF, 0);
    def.dest = mkDst(RT_CONST, 5);
    def.length = 5;
    Instruction mov = mkInst(Opcode::MOV, 100);
    mov.dest = mkDst(RT_TEMP, 0);
    mov.sources = {mkSrc(RT_CONST, 5)};
    auto shader = mkShader(true, 3, 0, {def, mov});

    std::vector<uint8_t> blob = zeroBlob();
    auto putF = [&](size_t off, float f) { std::memcpy(blob.data() + off, &f, 4); };
    putF(8, 1.0f);
    putF(12, 2.0f);
    putF(16, 3.0f);
    putF(20, 4.0f);

    auto r = translate(shader, emptyCtab(), blob);
    CHECK_CONTAINS(r.hlsl, "static const float4 c5_def = float4(1.0, 2.0, 3.0, 4.0);");
    CHECK_CONTAINS(r.hlsl, "r0.xyzw = c5_def.xyzw;");
    CHECK_NOT_CONTAINS(r.hlsl, "c[5]");
}

void testDefi() {
    Instruction defi = mkInst(Opcode::DEFI, 0);
    defi.dest = mkDst(RT_CONSTINT, 2);
    defi.length = 5;
    Instruction mov = mkInst(Opcode::MOV, 100);
    mov.dest = mkDst(RT_TEMP, 0);
    mov.sources = {mkSrc(RT_CONSTINT, 2)};
    auto shader = mkShader(true, 3, 0, {defi, mov});

    std::vector<uint8_t> blob = zeroBlob();
    auto putI = [&](size_t off, int32_t v) { std::memcpy(blob.data() + off, &v, 4); };
    putI(8, 10);
    putI(12, -1);
    putI(16, 0);
    putI(20, 7);

    auto r = translate(shader, emptyCtab(), blob);
    CHECK_CONTAINS(r.hlsl, "static const int4 i2_def = int4(10, -1, 0, 7);");
    CHECK_CONTAINS(r.hlsl, "r0.xyzw = i2_def.xyzw;");
}

// Regression test for a real population-gate finding: real compiler-
// emitted bytecode can carry a full .xyzw write mask on a DEPTHOUT
// destination even though HLSL's own `oDepth`-shaped field is scalar
// (D3DCompile error X3018 "invalid subscript 'xyzw'" on a real shader
// before this fix) - assert the translator clamps to the field's actual
// declared width instead of trusting the raw mask's component count.
void testDepthOutWideMaskClamped() {
    Instruction i = mkInst(Opcode::MOV);
    i.dest = mkDst(RT_DEPTHOUT, 0); // full .xyzw mask, as real bytecode was found to encode
    i.sources = {mkSrc(RT_TEMP, 0)};
    auto shader = mkShader(false, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "output.oDepth = (r0.xyzw).x;");
    CHECK_NOT_CONTAINS(r.hlsl, "oDepth.xyzw");
    CHECK(r.complete);
}

void testDcl() {
    // §8's VS 2.0+ input row + VS 3.0+ output row: usage/usageIndex ->
    // HLSL semantic on the generated VS_INPUT/VS_OUTPUT struct fields.
    Instruction dclIn = mkInst(Opcode::DCL, 0);
    dclIn.dcl = DclToken{};
    dclIn.dcl->usage = 3; // NORMAL (§8.1)
    dclIn.dcl->usageIndex = 0;
    dclIn.dest = mkDst(RT_INPUT, 2, true, true, true, false);

    Instruction dclOut = mkInst(Opcode::DCL, 100);
    dclOut.dcl = DclToken{};
    dclOut.dcl->usage = 0; // POSITION
    dclOut.dcl->usageIndex = 0;
    dclOut.dest = mkDst(RT_TEXCRDOUT_OR_OUTPUT, 0);

    Instruction mov = mkInst(Opcode::MOV, 200);
    mov.dest = mkDst(RT_TEXCRDOUT_OR_OUTPUT, 0);
    mov.sources = {mkSrc(RT_INPUT, 2)};

    auto shader = mkShader(true, 3, 0, {dclIn, dclOut, mov});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "v2 : NORMAL0;");
    CHECK_CONTAINS(r.hlsl, "o0 : POSITION0;");
}

void testTexkill() {
    Instruction i = mkInst(Opcode::TEXKILL);
    i.dest = mkDst(RT_TEMP, 0); // "a destination-shaped token used as a source", spec §12
    auto shader = mkShader(false, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "clip(r0.xyz);");
}

ConstantTable ctabWithOneSampler(const std::string& name, uint16_t regIndex, uint16_t typeRaw) {
    ConstantTable c;
    c.status = CtabStatus::WellFormed;
    ConstantInfo ci;
    ci.name = name;
    ci.registerSetRaw = static_cast<uint16_t>(RegisterSet::Sampler);
    ci.registerSet = RegisterSet::Sampler;
    ci.registerIndex = regIndex;
    ci.registerCount = 1;
    ci.type.classRaw = static_cast<uint16_t>(ParameterClass::Object);
    ci.type.typeRaw = typeRaw;
    c.constants.push_back(ci);
    return c;
}

void testTex2D() {
    Instruction i = mkInst(Opcode::TEX);
    i.dest = mkDst(RT_TEMP, 0);
    i.sources = {mkSrc(RT_INPUT, 0), mkSrc(RT_SAMPLER, 0)};
    auto shader = mkShader(false, 3, 0, {i}); // pixel shader -> plain tex2D, not the *lod variant
    auto ctab = ctabWithOneSampler("DiffuseMap", 0, static_cast<uint16_t>(ParameterType::Sampler2D));
    auto r = translate(shader, ctab, zeroBlob());
    CHECK_CONTAINS(r.hlsl, "sampler2D DiffuseMap : register(s0);");
    CHECK_CONTAINS(r.hlsl, "tex2D(DiffuseMap, input.v0.xyzw.xy)");
}

void testTexCube() {
    Instruction i = mkInst(Opcode::TEX);
    i.dest = mkDst(RT_TEMP, 0);
    i.sources = {mkSrc(RT_INPUT, 0), mkSrc(RT_SAMPLER, 1)};
    auto shader = mkShader(false, 3, 0, {i});
    auto ctab = ctabWithOneSampler("EnvMap", 1, static_cast<uint16_t>(ParameterType::SamplerCube));
    auto r = translate(shader, ctab, zeroBlob());
    CHECK_CONTAINS(r.hlsl, "samplerCUBE EnvMap : register(s1);");
    CHECK_CONTAINS(r.hlsl, "texCUBE(EnvMap, input.v0.xyzw.xyz)");
}

void testTexldl() {
    Instruction i = mkInst(Opcode::TEXLDL);
    i.dest = mkDst(RT_TEMP, 0);
    i.sources = {mkSrc(RT_INPUT, 0), mkSrc(RT_SAMPLER, 0)};
    auto shader = mkShader(false, 3, 0, {i});
    auto ctab = ctabWithOneSampler("DiffuseMap", 0, static_cast<uint16_t>(ParameterType::Sampler2D));
    auto r = translate(shader, ctab, zeroBlob());
    CHECK_CONTAINS(r.hlsl, "tex2Dlod(DiffuseMap, input.v0.xyzw)");
}

void testTexVertexShaderUsesLodVariant() {
    // vs_3_0 has no implicit derivatives - plain TEX in a VS must use the
    // *lod intrinsic even though the opcode is TEX, not TEXLDL.
    Instruction i = mkInst(Opcode::TEX);
    i.dest = mkDst(RT_TEMP, 0);
    i.sources = {mkSrc(RT_INPUT, 0), mkSrc(RT_SAMPLER, 0)};
    auto shader = mkShader(true, 3, 0, {i});
    auto ctab = ctabWithOneSampler("HeightMap", 0, static_cast<uint16_t>(ParameterType::Sampler2D));
    auto r = translate(shader, ctab, zeroBlob());
    CHECK_CONTAINS(r.hlsl, "tex2Dlod(HeightMap, input.v0.xyzw)");
}

void testIf() {
    Instruction ifI = mkInst(Opcode::IF, 0);
    ifI.sources = {mkSrc(RT_CONSTBOOL, 2)};
    Instruction mov = mkInst(Opcode::MOV, 100);
    mov.dest = mkDst(RT_TEMP, 0);
    mov.sources = {mkSrc(RT_TEMP, 1)};
    Instruction endif = mkInst(Opcode::ENDIF, 200);
    auto shader = mkShader(true, 3, 0, {ifI, mov, endif});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "if (b[2]) {");
    CHECK(r.complete);
}

// Regression test for a real bug the population gate found (not just
// inspection): Stage 1's disassembler (disassembler.cpp) is purely
// positional with no per-opcode operand-shape table (its own documented
// limitation) - for IF/IFC/REP, which have NO real destination, it always
// files the first (only, for IF/REP) source token into `Instruction::dest`
// instead of `sources`, decoded using the DESTINATION bit layout. This is
// exactly what disassemble() actually produces for every real IF/IFC/REP
// in this game's shaders - the other control-flow tests above use the
// "already in sources" shape, which turned out to never actually occur on
// real data; this one uses the shape that does.
void testIfStage1DestMisfiling() {
    Instruction ifI = mkInst(Opcode::IF, 0);
    DestinationParam misfiled;
    misfiled.raw = rawSourceToken(RT_CONSTBOOL, 3);
    ifI.dest = misfiled; // sources left EMPTY - matching real disassemble() output
    Instruction endif = mkInst(Opcode::ENDIF, 100);
    auto shader = mkShader(true, 3, 0, {ifI, endif});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "if (b[3]) {");
    CHECK(r.complete);
}

void testIfcStage1DestMisfiling() {
    Instruction ifc = mkInst(Opcode::IFC, 0);
    ifc.controlBits = 4; // LT
    DestinationParam misfiled;
    misfiled.raw = rawSourceToken(RT_TEMP, 1, 0, 0, 0, 0);
    ifc.dest = misfiled;
    ifc.sources = {mkSrc(RT_TEMP, 2, 0, 0, 0, 0)}; // second operand correctly filed
    Instruction endif = mkInst(Opcode::ENDIF, 100);
    auto shader = mkShader(true, 3, 0, {ifc, endif});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "if (r1.x < r2.x) {");
    CHECK(r.complete);
}

void testRepStage1DestMisfiling() {
    Instruction rep = mkInst(Opcode::REP, 0);
    DestinationParam misfiled;
    misfiled.raw = rawSourceToken(RT_CONSTINT, 4, 0, 0, 0, 0);
    rep.dest = misfiled;
    Instruction endrep = mkInst(Opcode::ENDREP, 100);
    auto shader = mkShader(true, 3, 0, {rep, endrep});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "for (int rep0 = 0; rep0 < (int)(i[4].x); ++rep0) {");
    CHECK(r.complete);
}

void testIfElseEndif() {
    Instruction ifI = mkInst(Opcode::IF, 0);
    ifI.sources = {mkSrc(RT_CONSTBOOL, 0)};
    Instruction elseI = mkInst(Opcode::ELSE, 100);
    Instruction endif = mkInst(Opcode::ENDIF, 200);
    auto shader = mkShader(true, 3, 0, {ifI, elseI, endif});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "} else {");
    CHECK(r.complete);
}

// IFC's comparison-bit mapping (see d3d9bc_hlsl_translator.cpp's top
// comment for exactly where this came from: apitrace/dxsdk's d3d9types.h
// mirror, the same public source spec-d3d9-sm2-sm3-bytecode.md already
// cites for other enums). One case per real D3DSPC_* value (1-6).
void testIfcComparisons() {
    struct Case { uint8_t bits; const char* op; };
    const Case cases[] = {
        {1, ">"}, {2, "=="}, {3, ">="}, {4, "<"}, {5, "!="}, {6, "<="},
    };
    for (const auto& c : cases) {
        Instruction ifc = mkInst(Opcode::IFC, 0);
        ifc.controlBits = c.bits;
        ifc.sources = {mkSrc(RT_TEMP, 1, 0, 0, 0, 0), mkSrc(RT_TEMP, 2, 0, 0, 0, 0)};
        Instruction endif = mkInst(Opcode::ENDIF, 100);
        auto shader = mkShader(true, 3, 0, {ifc, endif});
        auto r = translate(shader, emptyCtab(), zeroBlob());
        CHECK_CONTAINS(r.hlsl, std::string("if (r1.x ") + c.op + " r2.x) {");
        CHECK(r.complete);
    }
    // Reserved values (0, 7) must NOT be silently guessed.
    Instruction ifcBad = mkInst(Opcode::IFC, 0);
    ifcBad.controlBits = 0;
    ifcBad.sources = {mkSrc(RT_TEMP, 1, 0, 0, 0, 0), mkSrc(RT_TEMP, 2, 0, 0, 0, 0)};
    Instruction endif = mkInst(Opcode::ENDIF, 100);
    auto shaderBad = mkShader(true, 3, 0, {ifcBad, endif});
    auto rBad = translate(shaderBad, emptyCtab(), zeroBlob());
    CHECK(!rBad.complete);
    CHECK(!rBad.unsupported.empty());
}

void testRep() {
    Instruction rep = mkInst(Opcode::REP, 0);
    rep.sources = {mkSrc(RT_CONSTINT, 0, 0, 0, 0, 0)};
    Instruction mov = mkInst(Opcode::MOV, 100);
    mov.dest = mkDst(RT_TEMP, 0);
    mov.sources = {mkSrc(RT_TEMP, 1)};
    Instruction endrep = mkInst(Opcode::ENDREP, 200);
    auto shader = mkShader(true, 3, 0, {rep, mov, endrep});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "for (int rep0 = 0; rep0 < (int)(i[0].x); ++rep0) {");
    CHECK(r.complete);
}

// Nested REP-inside-IF, matching ENDIF/ENDREP to the correct nearest open
// block (spec §12: "nested BLOCK structure... track nesting depth").
void testNestedControlFlow() {
    Instruction ifI = mkInst(Opcode::IF, 0);
    ifI.sources = {mkSrc(RT_CONSTBOOL, 0)};
    Instruction rep = mkInst(Opcode::REP, 100);
    rep.sources = {mkSrc(RT_CONSTINT, 0, 0, 0, 0, 0)};
    Instruction mov = mkInst(Opcode::MOV, 200);
    mov.dest = mkDst(RT_TEMP, 0);
    mov.sources = {mkSrc(RT_TEMP, 1)};
    Instruction endrep = mkInst(Opcode::ENDREP, 300);
    Instruction endif = mkInst(Opcode::ENDIF, 400);
    auto shader = mkShader(true, 3, 0, {ifI, rep, mov, endrep, endif});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK(r.complete);
    // Both closing braces must appear, correctly balanced (no
    // "unclosed block" / mismatched-frame finding).
    CHECK(r.unsupported.empty());
}

// ===========================================================================
// Result-modifier Saturate (spec §6.1, applied uniformly per emitAssign()) -
// outside §12's own per-opcode table but implemented per this file's own
// stated judgment call; verify it round-trips correctly for one opcode.
// ===========================================================================
void testSaturate() {
    Instruction i = mkInst(Opcode::MUL);
    i.dest = mkDst(RT_TEMP, 0, true, true, true, true, 0x1 /*Saturate*/);
    i.sources = {mkSrc(RT_TEMP, 1), mkSrc(RT_TEMP, 2)};
    auto shader = mkShader(true, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK_CONTAINS(r.hlsl, "r0.xyzw = saturate((r1.xyzw * r2.xyzw));");
}

// ===========================================================================
// A genuinely unsupported source modifier (Bias - spec §6.4 names it but
// gives no formula, same as Microsoft's own source-parameter-token page;
// see this translator's top comment) must be flagged, not guessed.
// ===========================================================================
void testUnsupportedModifierIsFlaggedNotGuessed() {
    Instruction i = mkInst(Opcode::MOV);
    i.dest = mkDst(RT_TEMP, 0);
    i.sources = {mkSrc(RT_INPUT, 0, 0, 1, 2, 3, SourceModifier::Bias)};
    auto shader = mkShader(true, 3, 0, {i});
    auto r = translate(shader, emptyCtab(), zeroBlob());
    CHECK(!r.complete);
    bool foundBiasNote = false;
    for (const auto& u : r.unsupported)
        if (u.find("0x2") != std::string::npos) foundBiasNote = true;
    CHECK(foundBiasNote);
}

} // namespace

int main() {
    testMov();
    testAdd();
    testMad();
    testMul();
    testRcp();
    testRsq();
    testDp3();
    testDp4();
    testMin();
    testMax();
    testSlt();
    testSge();
    testExp();
    testLog();
    testLrp();
    testFrc();
    testPow();
    testAbs();
    testNrm();
    testCmp();
    testDp2add();
    testSincos();
    testMova();
    testDef();
    testDefi();
    testDepthOutWideMaskClamped();
    testDcl();
    testTexkill();
    testTex2D();
    testTexCube();
    testTexldl();
    testTexVertexShaderUsesLodVariant();
    testIf();
    testIfStage1DestMisfiling();
    testIfcStage1DestMisfiling();
    testRepStage1DestMisfiling();
    testIfElseEndif();
    testIfcComparisons();
    testRep();
    testNestedControlFlow();
    testSaturate();
    testUnsupportedModifierIsFlaggedNotGuessed();

    std::cout << g_checks - g_failures << "/" << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
