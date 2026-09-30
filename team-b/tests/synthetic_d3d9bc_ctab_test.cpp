// Synthetic tests for sr3d3d9bc::readConstantTable() (ctab.h), built FROM
// THE TEXT of spec-d3d9-sm2-sm3-bytecode.md §13 only - every fixture below
// hand-places raw bytes in the exact field order/width spec §13's own
// tables state (D3DXSHADER_CONSTANTTABLE 28 bytes, D3DXSHADER_CONSTANTINFO
// 20 bytes, D3DXSHADER_TYPEINFO 12 bytes, D3DXSHADER_STRUCTMEMBERINFO 8
// bytes). Mirrors the house style of tests/synthetic_d3d9bc_test.cpp: one
// CHECK per assertion, no reliance on the reader under test to build its
// own inputs.
//
// Because CTAB is a self-referential, relocatable format (every name/
// TypeInfo/StructMemberInfo reference is an offset back into the same
// table), these fixtures are built with a small set of GENERIC byte-packing
// primitives (pushU32LE/pushU16LE/pushCStr/patchU32LE - the same category
// of helper tests/synthetic_d3d9bc_test.cpp's own Dwords() is) rather than
// typed literal offsets: each field is still written by hand, in the exact
// order/width/meaning spec §13 states, one field at a time; only the
// numeric BACK-REFERENCE values (e.g. "the Name string is at table offset
// X") are captured from the position the byte vector was actually at when
// that data was appended, which is measured directly rather than
// independently recomputed - the alternative (typing out ~40 interdependent
// magic-number offsets per fixture by hand) trades a transcription-error
// risk for no additional rigor. None of these helpers know what a
// "ConstantInfo" or "TypeInfo" IS; they only pack raw bytes, so this is not
// round-tripped through any encoder that mirrors ctab.cpp's own parsing
// structure (no such encoder exists anywhere in this codebase - only a
// reader).
//
// A pass here proves the reader implements spec §13's bit/byte layout, not
// that the spec is right or that real shaders look like these - the
// real-data population-gate statement is
// tools/validation/validate_d3d9bc_ctab_population.cpp.

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "sr3d3d9bc/ctab.h"
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

// ---------------------------------------------------------------------------
// Generic byte-packing primitives (see file header comment for why these
// are not "an encoder" of the CTAB format).
// ---------------------------------------------------------------------------

void pushU32LE(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void pushU16LE(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

// Appends `s` plus a single NUL terminator - spec §13: "ordinary
// null-terminated ASCII text".
void pushCStr(std::vector<uint8_t>& b, const std::string& s) {
    for (char c : s) b.push_back(static_cast<uint8_t>(c));
    b.push_back(0);
}

void patchU32LE(std::vector<uint8_t>& b, size_t pos, uint32_t v) {
    b[pos + 0] = static_cast<uint8_t>(v & 0xFF);
    b[pos + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    b[pos + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    b[pos + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

// Packs a list of 32-bit tokens into a little-endian byte buffer (same
// helper as tests/synthetic_d3d9bc_test.cpp's own Dwords()).
std::vector<uint8_t> Dwords(std::initializer_list<uint32_t> toks) {
    std::vector<uint8_t> b;
    for (uint32_t t : toks) pushU32LE(b, t);
    return b;
}

// Wraps a hand-built CTAB table region (everything AFTER the "CTAB" FourCC
// dword - `table`, which must already represent a whole number of dwords'
// worth of content once the FourCC is prefixed; padding to a dword
// boundary is applied here since spec §4's comment-token length field is
// itself dword-granular and padding bytes carry no field meaning) into a
// full, walkable shader blob: version token (spec §2), one comment token
// (spec §4) whose payload is "CTAB" + `table`, then a NOP instruction
// (spec §9, opcode 0, length 0) and the end token (spec §5) - matching
// tests/synthetic_d3d9bc_test.cpp's own testCommentTokenSkipped() shape.
std::vector<uint8_t> BuildShaderWithCtab(std::vector<uint8_t> table) {
    std::vector<uint8_t> buf = Dwords({0xFFFF0300u});  // ps_3_0 version token (spec §2)

    const size_t commentTokenPos = buf.size();
    pushU32LE(buf, 0);  // comment token - patched below once the payload size is known
    const size_t payloadStart = buf.size();
    pushU32LE(buf, 0x42415443u);  // "CTAB" fourcc (spec §4 application note / §13)
    for (uint8_t byte : table) buf.push_back(byte);
    while ((buf.size() - payloadStart) % 4 != 0) buf.push_back(0);  // pad to a whole dword (inert)
    const size_t payloadEnd = buf.size();
    const uint32_t payloadDwords = static_cast<uint32_t>((payloadEnd - payloadStart) / 4);
    patchU32LE(buf, commentTokenPos, ((payloadDwords & 0x7FFFu) << 16) | 0xFFFEu);  // spec §4

    pushU32LE(buf, 0x00000000u);  // NOP: opcode 0, length 0 (spec §9)
    pushU32LE(buf, 0x0000FFFFu);  // end token (spec §5)
    return buf;
}

DisassembledShader Disasm(const std::vector<uint8_t>& bytes) {
    return disassemble(vpp::ByteView(bytes.data(), bytes.size()));
}

// ===========================================================================
// One fixed CTAB table builder used by several fixtures below: appends the
// 28-byte D3DXSHADER_CONSTANTTABLE header (spec §13) with placeholders for
// Creator/ConstantInfo/Target, returning the positions to patch once the
// referenced data's own position is known. `constantsCount`/`constantInfoOff`
// are filled in directly since the caller always knows them up front.
// ---------------------------------------------------------------------------
struct HeaderPatchPoints {
    size_t creatorOffPos;
    size_t targetOffPos;
};

HeaderPatchPoints pushConstantTableHeader(std::vector<uint8_t>& t, uint32_t constantsCount,
                                           uint32_t constantInfoOff) {
    HeaderPatchPoints p{};
    pushU32LE(t, 28);              // Size (+0) - nominal; spec §13 does not define further interpretation
    p.creatorOffPos = t.size();
    pushU32LE(t, 0);                // Creator (+4) - patched by caller
    pushU32LE(t, 0xFFFF0300u);      // Version (+8) - nominal ps_3_0, not interpreted by the reader
    pushU32LE(t, constantsCount);   // Constants (+12)
    pushU32LE(t, constantInfoOff);  // ConstantInfo (+16)
    pushU32LE(t, 0);                // Flags (+20)
    p.targetOffPos = t.size();
    pushU32LE(t, 0);  // Target (+24) - patched by caller
    return p;
}

// Appends one D3DXSHADER_CONSTANTINFO (spec §13, 20 bytes), returning the
// positions of its Name/TypeInfo back-reference fields to patch.
struct ConstantInfoPatchPoints {
    size_t nameOffPos;
    size_t typeInfoOffPos;
};

ConstantInfoPatchPoints pushConstantInfo(std::vector<uint8_t>& t, uint16_t registerSet, uint16_t registerIndex,
                                          uint16_t registerCount) {
    ConstantInfoPatchPoints p{};
    p.nameOffPos = t.size();
    pushU32LE(t, 0);               // Name (+0) - patched by caller
    pushU16LE(t, registerSet);      // RegisterSet (+4)
    pushU16LE(t, registerIndex);    // RegisterIndex (+6)
    pushU16LE(t, registerCount);    // RegisterCount (+8)
    pushU16LE(t, 0);                // Reserved (+10) - unused per spec §13
    p.typeInfoOffPos = t.size();
    pushU32LE(t, 0);  // TypeInfo (+12) - patched by caller
    pushU32LE(t, 0);  // DefaultValue (+16) - 0 ("if none", spec §13)
    return p;
}

// Appends one D3DXSHADER_TYPEINFO (spec §13, 12-byte fixed header).
void pushTypeInfo(std::vector<uint8_t>& t, uint16_t classRaw, uint16_t typeRaw, uint16_t rows, uint16_t columns,
                   uint16_t elements, uint16_t structMembers) {
    pushU16LE(t, classRaw);
    pushU16LE(t, typeRaw);
    pushU16LE(t, rows);
    pushU16LE(t, columns);
    pushU16LE(t, elements);
    pushU16LE(t, structMembers);
}

// Appends one D3DXSHADER_STRUCTMEMBERINFO (spec §13, 8 bytes), returning
// the positions of its Name/TypeInfo back-reference fields.
struct StructMemberPatchPoints {
    size_t nameOffPos;
    size_t typeInfoOffPos;
};

StructMemberPatchPoints pushStructMemberInfo(std::vector<uint8_t>& t) {
    StructMemberPatchPoints p{};
    p.nameOffPos = t.size();
    pushU32LE(t, 0);  // Name (+0) - patched by caller
    p.typeInfoOffPos = t.size();
    pushU32LE(t, 0);  // TypeInfo (+4) - patched by caller
    return p;
}

// ===========================================================================
// Fixture 1: a scalar float constant.
// ===========================================================================
void testScalarFloat() {
    std::vector<uint8_t> t;  // table region, everything after the FourCC dword
    // ConstantInfo array (1 entry) sits right after the 28-byte header, so
    // its table offset is exactly 28.
    HeaderPatchPoints hp = pushConstantTableHeader(t, /*constantsCount=*/1, /*constantInfoOff=*/28);
    CHECK(t.size() == 28);

    ConstantInfoPatchPoints cp = pushConstantInfo(t, /*registerSet=D3DXRS_FLOAT4*/ 2, /*registerIndex=*/7,
                                                   /*registerCount=*/1);
    CHECK(t.size() == 48);  // 28 + 20

    const uint32_t typeInfoOff = static_cast<uint32_t>(t.size());
    pushTypeInfo(t, /*Class=D3DXPC_SCALAR*/ 0, /*Type=D3DXPT_FLOAT*/ 3, /*Rows=*/1, /*Columns=*/1, /*Elements=*/1,
                 /*StructMembers=*/0);
    patchU32LE(t, cp.typeInfoOffPos, typeInfoOff);

    const uint32_t nameOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "g_scalarConst");
    patchU32LE(t, cp.nameOffPos, nameOff);

    const uint32_t creatorOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "T13TEST");
    patchU32LE(t, hp.creatorOffPos, creatorOff);

    const uint32_t targetOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "ps_3_0");
    patchU32LE(t, hp.targetOffPos, targetOff);

    std::vector<uint8_t> blob = BuildShaderWithCtab(t);
    DisassembledShader d = Disasm(blob);
    CHECK(d.status == WalkStatus::Ok);
    CHECK(d.comments.size() == 1 && d.comments[0].looksLikeCtab);

    ConstantTable ct = readConstantTable(vpp::ByteView(blob.data(), blob.size()), d);
    CHECK(ct.status == CtabStatus::WellFormed);
    CHECK(ct.creator == "T13TEST");
    CHECK(ct.target == "ps_3_0");
    CHECK(ct.constantsCount == 1);
    CHECK(ct.constants.size() == 1);
    const ConstantInfo& c = ct.constants[0];
    CHECK(c.name == "g_scalarConst");
    CHECK(c.registerSetRaw == 2 && c.registerSet == RegisterSet::Float4);
    CHECK(c.registerIndex == 7);
    CHECK(c.registerCount == 1);
    CHECK(c.type.classRaw == 0 && c.type.classValue == ParameterClass::Scalar);
    CHECK(c.type.typeRaw == 3 && c.type.typeValue == ParameterType::Float);
    CHECK(c.type.rows == 1 && c.type.columns == 1 && c.type.elements == 1);
    CHECK(c.type.structMembers == 0);
    CHECK(c.type.structMemberInfo.empty());
}

// ===========================================================================
// Fixture 2: a float4 vector constant.
// ===========================================================================
void testVector() {
    std::vector<uint8_t> t;
    HeaderPatchPoints hp = pushConstantTableHeader(t, 1, 28);
    ConstantInfoPatchPoints cp = pushConstantInfo(t, /*Float4*/ 2, /*index*/ 3, /*count*/ 1);

    const uint32_t typeInfoOff = static_cast<uint32_t>(t.size());
    pushTypeInfo(t, /*Class=D3DXPC_VECTOR*/ 1, /*Type=D3DXPT_FLOAT*/ 3, /*Rows=*/1, /*Columns=*/4, /*Elements=*/1, 0);
    patchU32LE(t, cp.typeInfoOffPos, typeInfoOff);

    const uint32_t nameOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "g_vectorConst");
    patchU32LE(t, cp.nameOffPos, nameOff);

    const uint32_t creatorOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "T13TEST");
    patchU32LE(t, hp.creatorOffPos, creatorOff);
    const uint32_t targetOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "vs_3_0");
    patchU32LE(t, hp.targetOffPos, targetOff);

    std::vector<uint8_t> blob = BuildShaderWithCtab(t);
    DisassembledShader d = Disasm(blob);
    ConstantTable ct = readConstantTable(vpp::ByteView(blob.data(), blob.size()), d);
    CHECK(ct.status == CtabStatus::WellFormed);
    CHECK(ct.constants.size() == 1);
    const ConstantInfo& c = ct.constants[0];
    CHECK(c.name == "g_vectorConst");
    CHECK(c.type.classValue == ParameterClass::Vector);
    CHECK(c.type.typeValue == ParameterType::Float);
    CHECK(c.type.rows == 1 && c.type.columns == 4);
}

// ===========================================================================
// Fixture 3: a 4x4 matrix, once as D3DXPC_MATRIX_ROWS and once as
// D3DXPC_MATRIX_COLUMNS - two constants in one table.
// ===========================================================================
void testMatrixRowsVsColumns() {
    std::vector<uint8_t> t;
    HeaderPatchPoints hp = pushConstantTableHeader(t, 2, 28);
    ConstantInfoPatchPoints cp0 = pushConstantInfo(t, /*Float4*/ 2, /*index*/ 10, /*count*/ 4);
    ConstantInfoPatchPoints cp1 = pushConstantInfo(t, /*Float4*/ 2, /*index*/ 20, /*count*/ 4);
    CHECK(t.size() == 28 + 40);  // header + 2 ConstantInfo entries

    const uint32_t typeInfoOff0 = static_cast<uint32_t>(t.size());
    pushTypeInfo(t, /*Class=D3DXPC_MATRIX_ROWS*/ 2, /*Type=D3DXPT_FLOAT*/ 3, /*Rows=*/4, /*Columns=*/4, 1, 0);
    patchU32LE(t, cp0.typeInfoOffPos, typeInfoOff0);

    const uint32_t typeInfoOff1 = static_cast<uint32_t>(t.size());
    pushTypeInfo(t, /*Class=D3DXPC_MATRIX_COLUMNS*/ 3, /*Type=D3DXPT_FLOAT*/ 3, /*Rows=*/4, /*Columns=*/4, 1, 0);
    patchU32LE(t, cp1.typeInfoOffPos, typeInfoOff1);

    const uint32_t name0Off = static_cast<uint32_t>(t.size());
    pushCStr(t, "g_matRows");
    patchU32LE(t, cp0.nameOffPos, name0Off);
    const uint32_t name1Off = static_cast<uint32_t>(t.size());
    pushCStr(t, "g_matColumns");
    patchU32LE(t, cp1.nameOffPos, name1Off);

    const uint32_t creatorOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "T13TEST");
    patchU32LE(t, hp.creatorOffPos, creatorOff);
    const uint32_t targetOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "vs_3_0");
    patchU32LE(t, hp.targetOffPos, targetOff);

    std::vector<uint8_t> blob = BuildShaderWithCtab(t);
    DisassembledShader d = Disasm(blob);
    ConstantTable ct = readConstantTable(vpp::ByteView(blob.data(), blob.size()), d);
    CHECK(ct.status == CtabStatus::WellFormed);
    CHECK(ct.constants.size() == 2);
    CHECK(ct.constants[0].name == "g_matRows");
    CHECK(ct.constants[0].type.classValue == ParameterClass::MatrixRows);
    CHECK(ct.constants[0].type.rows == 4 && ct.constants[0].type.columns == 4);
    CHECK(ct.constants[0].registerCount == 4);
    CHECK(ct.constants[1].name == "g_matColumns");
    CHECK(ct.constants[1].type.classValue == ParameterClass::MatrixColumns);
    CHECK(ct.constants[1].type.rows == 4 && ct.constants[1].type.columns == 4);
    CHECK(ct.constants[1].registerCount == 4);
}

// ===========================================================================
// Fixture 4: a sampler (RegisterSet == D3DXRS_SAMPLER).
// ===========================================================================
void testSampler() {
    std::vector<uint8_t> t;
    HeaderPatchPoints hp = pushConstantTableHeader(t, 1, 28);
    ConstantInfoPatchPoints cp = pushConstantInfo(t, /*D3DXRS_SAMPLER*/ 3, /*index*/ 0, /*count*/ 1);

    const uint32_t typeInfoOff = static_cast<uint32_t>(t.size());
    // D3DXPC_OBJECT (samplers/textures fall here per spec §13.2), Type =
    // D3DXPT_SAMPLER2D (12, spec §13.3).
    pushTypeInfo(t, /*Class=D3DXPC_OBJECT*/ 4, /*Type=D3DXPT_SAMPLER2D*/ 12, /*Rows=*/0, /*Columns=*/0,
                 /*Elements=*/1, 0);
    patchU32LE(t, cp.typeInfoOffPos, typeInfoOff);

    const uint32_t nameOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "g_diffuseSampler");
    patchU32LE(t, cp.nameOffPos, nameOff);

    const uint32_t creatorOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "T13TEST");
    patchU32LE(t, hp.creatorOffPos, creatorOff);
    const uint32_t targetOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "ps_3_0");
    patchU32LE(t, hp.targetOffPos, targetOff);

    std::vector<uint8_t> blob = BuildShaderWithCtab(t);
    DisassembledShader d = Disasm(blob);
    ConstantTable ct = readConstantTable(vpp::ByteView(blob.data(), blob.size()), d);
    CHECK(ct.status == CtabStatus::WellFormed);
    CHECK(ct.constants.size() == 1);
    const ConstantInfo& c = ct.constants[0];
    CHECK(c.name == "g_diffuseSampler");
    CHECK(c.registerSet == RegisterSet::Sampler);
    CHECK(c.type.classValue == ParameterClass::Object);
    CHECK(c.type.typeValue == ParameterType::Sampler2D);
}

// ===========================================================================
// Fixture 5: an array (Elements > 1).
// ===========================================================================
void testArray() {
    std::vector<uint8_t> t;
    HeaderPatchPoints hp = pushConstantTableHeader(t, 1, 28);
    ConstantInfoPatchPoints cp = pushConstantInfo(t, /*Float4*/ 2, /*index*/ 30, /*count*/ 8);

    const uint32_t typeInfoOff = static_cast<uint32_t>(t.size());
    pushTypeInfo(t, /*Class=D3DXPC_VECTOR*/ 1, /*Type=D3DXPT_FLOAT*/ 3, /*Rows=*/1, /*Columns=*/4,
                 /*Elements=*/8, 0);
    patchU32LE(t, cp.typeInfoOffPos, typeInfoOff);

    const uint32_t nameOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "g_lightPositions");
    patchU32LE(t, cp.nameOffPos, nameOff);

    const uint32_t creatorOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "T13TEST");
    patchU32LE(t, hp.creatorOffPos, creatorOff);
    const uint32_t targetOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "ps_3_0");
    patchU32LE(t, hp.targetOffPos, targetOff);

    std::vector<uint8_t> blob = BuildShaderWithCtab(t);
    DisassembledShader d = Disasm(blob);
    ConstantTable ct = readConstantTable(vpp::ByteView(blob.data(), blob.size()), d);
    CHECK(ct.status == CtabStatus::WellFormed);
    const ConstantInfo& c = ct.constants[0];
    CHECK(c.name == "g_lightPositions");
    CHECK(c.type.elements == 8);
    CHECK(c.registerCount == 8);
}

// ===========================================================================
// Fixture 6: a struct-typed constant (Class == D3DXPC_STRUCT), exercising
// D3DXSHADER_STRUCTMEMBERINFO per spec §13's own resolution of the layout
// ambiguity: StructMembers-many 8-byte entries read CONTIGUOUSLY starting
// right after the top TypeInfo's 12-byte fixed header (i.e. at that
// TypeInfo's own table offset + 12).
// ===========================================================================
void testStruct() {
    std::vector<uint8_t> t;
    HeaderPatchPoints hp = pushConstantTableHeader(t, 1, 28);
    ConstantInfoPatchPoints cp = pushConstantInfo(t, /*Float4*/ 2, /*index*/ 40, /*count*/ 2);
    CHECK(t.size() == 48);

    const uint32_t topTypeInfoOff = static_cast<uint32_t>(t.size());  // == 48
    pushTypeInfo(t, /*Class=D3DXPC_STRUCT*/ 5, /*Type=D3DXPT_VOID*/ 0, /*Rows=*/1, /*Columns=*/1,
                 /*Elements=*/1, /*StructMembers=*/2);
    patchU32LE(t, cp.typeInfoOffPos, topTypeInfoOff);
    CHECK(t.size() == 60);  // 48 + 12

    // Per spec §13's resolution, the two STRUCTMEMBERINFO entries (8 bytes
    // each) sit contiguously starting AT topTypeInfoOff+12 (== 60) - i.e.
    // immediately here, not through an indirection DWORD.
    CHECK(t.size() == topTypeInfoOff + 12);
    StructMemberPatchPoints m0 = pushStructMemberInfo(t);
    StructMemberPatchPoints m1 = pushStructMemberInfo(t);
    CHECK(t.size() == 60 + 16);

    // Member 0: "position", a float3 (D3DXPC_VECTOR, Rows=1, Columns=3).
    const uint32_t member0TypeOff = static_cast<uint32_t>(t.size());
    pushTypeInfo(t, /*Class=D3DXPC_VECTOR*/ 1, /*Type=D3DXPT_FLOAT*/ 3, 1, 3, 1, 0);
    patchU32LE(t, m0.typeInfoOffPos, member0TypeOff);

    // Member 1: "intensity", a scalar float.
    const uint32_t member1TypeOff = static_cast<uint32_t>(t.size());
    pushTypeInfo(t, /*Class=D3DXPC_SCALAR*/ 0, /*Type=D3DXPT_FLOAT*/ 3, 1, 1, 1, 0);
    patchU32LE(t, m1.typeInfoOffPos, member1TypeOff);

    const uint32_t member0NameOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "position");
    patchU32LE(t, m0.nameOffPos, member0NameOff);
    const uint32_t member1NameOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "intensity");
    patchU32LE(t, m1.nameOffPos, member1NameOff);

    const uint32_t topNameOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "g_light");
    patchU32LE(t, cp.nameOffPos, topNameOff);

    const uint32_t creatorOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "T13TEST");
    patchU32LE(t, hp.creatorOffPos, creatorOff);
    const uint32_t targetOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "ps_3_0");
    patchU32LE(t, hp.targetOffPos, targetOff);

    std::vector<uint8_t> blob = BuildShaderWithCtab(t);
    DisassembledShader d = Disasm(blob);
    ConstantTable ct = readConstantTable(vpp::ByteView(blob.data(), blob.size()), d);
    CHECK(ct.status == CtabStatus::WellFormed);
    CHECK(ct.constants.size() == 1);
    const ConstantInfo& c = ct.constants[0];
    CHECK(c.name == "g_light");
    CHECK(c.type.classValue == ParameterClass::Struct);
    CHECK(c.type.structMembers == 2);
    CHECK(c.type.structMemberInfo.size() == 2);
    CHECK(c.type.structMemberInfo[0].name == "position");
    CHECK(c.type.structMemberInfo[0].typeInfo.classValue == ParameterClass::Vector);
    CHECK(c.type.structMemberInfo[0].typeInfo.rows == 1 && c.type.structMemberInfo[0].typeInfo.columns == 3);
    CHECK(c.type.structMemberInfo[1].name == "intensity");
    CHECK(c.type.structMemberInfo[1].typeInfo.classValue == ParameterClass::Scalar);
    CHECK(c.type.structMemberInfo[1].typeInfo.rows == 1 && c.type.structMemberInfo[1].typeInfo.columns == 1);
}

// ===========================================================================
// NotPresent bucket, case (a): no comment token at all.
// ===========================================================================
void testNoCommentAtAll() {
    std::vector<uint8_t> blob = Dwords({0xFFFF0300u, 0x00000000u /*NOP*/, 0x0000FFFFu /*end*/});
    DisassembledShader d = Disasm(blob);
    CHECK(d.comments.empty());
    ConstantTable ct = readConstantTable(vpp::ByteView(blob.data(), blob.size()), d);
    CHECK(ct.status == CtabStatus::NotPresent);
}

// ===========================================================================
// NotPresent bucket, case (b): a comment token present but its payload
// does NOT start with the CTAB FourCC.
// ===========================================================================
void testCommentNotCtab() {
    std::vector<uint8_t> blob = Dwords({
        0xFFFF0300u,
        0x0002FFFEu,  // comment token, payload = 2 dwords
        0x11111111u,  // opaque, not "CTAB"
        0x22222222u,
        0x00000000u,  // NOP
        0x0000FFFFu,  // end
    });
    DisassembledShader d = Disasm(blob);
    CHECK(d.comments.size() == 1 && !d.comments[0].looksLikeCtab);
    ConstantTable ct = readConstantTable(vpp::ByteView(blob.data(), blob.size()), d);
    CHECK(ct.status == CtabStatus::NotPresent);
}

// ===========================================================================
// NotPresent bucket, case (c): a CTAB-shaped comment token exists, but it
// is NOT leading - a real instruction precedes it. Exercises the
// instructions-vs-comments ordering check in readConstantTable() itself
// (spec §13.4: "no comment token before the first real instruction at
// all").
// ===========================================================================
void testCtabCommentNotLeading() {
    std::vector<uint8_t> t;
    HeaderPatchPoints hp = pushConstantTableHeader(t, 1, 28);
    ConstantInfoPatchPoints cp = pushConstantInfo(t, 2, 0, 1);
    const uint32_t typeInfoOff = static_cast<uint32_t>(t.size());
    pushTypeInfo(t, 0, 3, 1, 1, 1, 0);
    patchU32LE(t, cp.typeInfoOffPos, typeInfoOff);
    const uint32_t nameOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "g_late");
    patchU32LE(t, cp.nameOffPos, nameOff);
    const uint32_t creatorOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "T13TEST");
    patchU32LE(t, hp.creatorOffPos, creatorOff);
    const uint32_t targetOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "ps_3_0");
    patchU32LE(t, hp.targetOffPos, targetOff);

    // Build the CTAB payload bytes the same way BuildShaderWithCtab() would,
    // but place a real NOP instruction BEFORE the comment token instead of
    // after.
    std::vector<uint8_t> blob = Dwords({0xFFFF0300u, 0x00000000u /*NOP first*/});
    const size_t commentTokenPos = blob.size();
    pushU32LE(blob, 0);
    const size_t payloadStart = blob.size();
    pushU32LE(blob, 0x42415443u);
    for (uint8_t byte : t) blob.push_back(byte);
    while ((blob.size() - payloadStart) % 4 != 0) blob.push_back(0);
    const uint32_t payloadDwords = static_cast<uint32_t>((blob.size() - payloadStart) / 4);
    patchU32LE(blob, commentTokenPos, ((payloadDwords & 0x7FFFu) << 16) | 0xFFFEu);
    pushU32LE(blob, 0x0000FFFFu);  // end

    DisassembledShader d = Disasm(blob);
    CHECK(d.comments.size() == 1 && d.comments[0].looksLikeCtab);
    CHECK(!d.instructions.empty() && d.instructions[0].tokenOffset < d.comments[0].tokenOffset);
    ConstantTable ct = readConstantTable(vpp::ByteView(blob.data(), blob.size()), d);
    CHECK(ct.status == CtabStatus::NotPresent);
}

// ===========================================================================
// Malformed: the comment payload starts with the CTAB FourCC but is too
// small to hold even the 28-byte CONSTANTTABLE header.
// ===========================================================================
void testMalformedHeaderTooSmall() {
    std::vector<uint8_t> blob = Dwords({
        0xFFFF0300u,
        0x0002FFFEu,  // comment token, payload = 2 dwords (FourCC + 1 more - nowhere near 28 bytes)
        0x42415443u,  // "CTAB"
        0x00000000u,
        0x00000000u,  // NOP
        0x0000FFFFu,  // end
    });
    DisassembledShader d = Disasm(blob);
    CHECK(d.comments.size() == 1 && d.comments[0].looksLikeCtab);
    ConstantTable ct = readConstantTable(vpp::ByteView(blob.data(), blob.size()), d);
    CHECK(ct.status == CtabStatus::Malformed);
    CHECK(!ct.malformedReason.empty());
}

// ===========================================================================
// Malformed: Constants * 20 bytes does not fit inside the comment payload.
// ===========================================================================
void testMalformedConstantsOverrun() {
    std::vector<uint8_t> t;
    // Claim 1000 constants but the table region is nowhere near big enough.
    pushConstantTableHeader(t, /*constantsCount=*/1000, /*constantInfoOff=*/28);
    CHECK(t.size() == 28);

    std::vector<uint8_t> blob = BuildShaderWithCtab(t);
    DisassembledShader d = Disasm(blob);
    ConstantTable ct = readConstantTable(vpp::ByteView(blob.data(), blob.size()), d);
    CHECK(ct.status == CtabStatus::Malformed);
    CHECK(ct.malformedReason.find("Constants") != std::string::npos);
}

// ===========================================================================
// Mutation test: take the well-formed scalar fixture (Fixture 1's own
// construction, inlined here so the byte vector is available to corrupt),
// break the ConstantInfo offset field, confirm the reader catches it
// (Malformed), then revert the exact same byte and confirm WellFormed
// again - spec-fork instruction: "break a field's offset/shift, confirm
// the suite catches it, revert".
// ===========================================================================
void testMutationConstantInfoOffset() {
    std::vector<uint8_t> t;
    HeaderPatchPoints hp = pushConstantTableHeader(t, 1, 28);
    ConstantInfoPatchPoints cp = pushConstantInfo(t, 2, 7, 1);
    const uint32_t typeInfoOff = static_cast<uint32_t>(t.size());
    pushTypeInfo(t, 0, 3, 1, 1, 1, 0);
    patchU32LE(t, cp.typeInfoOffPos, typeInfoOff);
    const uint32_t nameOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "g_scalarConst");
    patchU32LE(t, cp.nameOffPos, nameOff);
    const uint32_t creatorOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "T13TEST");
    patchU32LE(t, hp.creatorOffPos, creatorOff);
    const uint32_t targetOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "ps_3_0");
    patchU32LE(t, hp.targetOffPos, targetOff);

    std::vector<uint8_t> blob = BuildShaderWithCtab(t);

    // Sanity: unmutated, this is well-formed (same fixture as Fixture 1).
    {
        DisassembledShader d = Disasm(blob);
        ConstantTable ct = readConstantTable(vpp::ByteView(blob.data(), blob.size()), d);
        CHECK(ct.status == CtabStatus::WellFormed);
    }

    // ConstantTable::ConstantInfo (+16) lives at blob offset (comment
    // token 4 bytes + FourCC 4 bytes + 16) = 24 past the comment token's
    // own start; locate it via the comment token offset actually found
    // rather than a re-derived literal, then corrupt it to a huge,
    // obviously-out-of-range value.
    DisassembledShader d0 = Disasm(blob);
    const size_t constantInfoFieldPos = d0.comments[0].tokenOffset + 4 /*comment token*/ + 4 /*FourCC*/ + 16;
    const uint8_t original0 = blob[constantInfoFieldPos + 0];
    const uint8_t original1 = blob[constantInfoFieldPos + 1];
    const uint8_t original2 = blob[constantInfoFieldPos + 2];
    const uint8_t original3 = blob[constantInfoFieldPos + 3];
    patchU32LE(blob, constantInfoFieldPos, 0xFFFFFFF0u);  // break: ConstantInfo now points far outside the payload

    {
        DisassembledShader d = Disasm(blob);
        ConstantTable ct = readConstantTable(vpp::ByteView(blob.data(), blob.size()), d);
        CHECK(ct.status == CtabStatus::Malformed);  // the suite catches the break
    }

    // Revert.
    blob[constantInfoFieldPos + 0] = original0;
    blob[constantInfoFieldPos + 1] = original1;
    blob[constantInfoFieldPos + 2] = original2;
    blob[constantInfoFieldPos + 3] = original3;
    {
        DisassembledShader d = Disasm(blob);
        ConstantTable ct = readConstantTable(vpp::ByteView(blob.data(), blob.size()), d);
        CHECK(ct.status == CtabStatus::WellFormed);  // back to normal after revert
        CHECK(ct.constants.size() == 1 && ct.constants[0].name == "g_scalarConst");
    }
}

// ===========================================================================
// Mutation test 2: shift the top-level TypeInfo offset by 2 bytes (misalign
// into the middle of the ConstantInfo entry / adjacent data rather than a
// wildly out-of-range value) and confirm the parsed Class/Type/Rows/
// Columns no longer match the intended fixture - the reader still succeeds
// structurally (the shifted offset still lands inside the payload) but the
// CONTENT is now provably wrong, which is exactly the kind of shift bug a
// hand-encoded fixture is supposed to catch. Then revert.
// ===========================================================================
void testMutationTypeInfoShift() {
    std::vector<uint8_t> t;
    HeaderPatchPoints hp = pushConstantTableHeader(t, 1, 28);
    ConstantInfoPatchPoints cp = pushConstantInfo(t, 2, 7, 1);
    const uint32_t typeInfoOff = static_cast<uint32_t>(t.size());
    pushTypeInfo(t, /*Class=D3DXPC_VECTOR*/ 1, /*Type=D3DXPT_FLOAT*/ 3, /*Rows=*/1, /*Columns=*/4, 1, 0);
    patchU32LE(t, cp.typeInfoOffPos, typeInfoOff);
    const uint32_t nameOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "g_shiftTest");
    patchU32LE(t, cp.nameOffPos, nameOff);
    const uint32_t creatorOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "T13TEST");
    patchU32LE(t, hp.creatorOffPos, creatorOff);
    const uint32_t targetOff = static_cast<uint32_t>(t.size());
    pushCStr(t, "ps_3_0");
    patchU32LE(t, hp.targetOffPos, targetOff);

    std::vector<uint8_t> blob = BuildShaderWithCtab(t);

    {
        DisassembledShader d = Disasm(blob);
        ConstantTable ct = readConstantTable(vpp::ByteView(blob.data(), blob.size()), d);
        CHECK(ct.status == CtabStatus::WellFormed);
        CHECK(ct.constants[0].type.classValue == ParameterClass::Vector);
        CHECK(ct.constants[0].type.columns == 4);
    }

    DisassembledShader d0 = Disasm(blob);
    const size_t typeInfoFieldPos = d0.comments[0].tokenOffset + 4 + 4 + 28 /*past header*/ + 12 /*TypeInfo field within ConstantInfo*/;
    const uint32_t before = blob[typeInfoFieldPos] | (blob[typeInfoFieldPos + 1] << 8) |
                             (blob[typeInfoFieldPos + 2] << 16) | (blob[typeInfoFieldPos + 3] << 24);
    patchU32LE(blob, typeInfoFieldPos, before + 2);  // shift by 2 bytes - misaligned, not out of range

    {
        DisassembledShader d = Disasm(blob);
        ConstantTable ct = readConstantTable(vpp::ByteView(blob.data(), blob.size()), d);
        // Structurally still fits (shifted 2 bytes into adjacent Elements/
        // StructMembers territory), but the content is now wrong - Class
        // is no longer D3DXPC_VECTOR(1) once every field is read 2 bytes
        // off from where it was written.
        if (ct.status == CtabStatus::WellFormed) {
            CHECK(!(ct.constants[0].type.classValue == ParameterClass::Vector && ct.constants[0].type.columns == 4));
        } else {
            CHECK(ct.status == CtabStatus::Malformed);  // also an acceptable catch of the shift
        }
    }

    patchU32LE(blob, typeInfoFieldPos, before);  // revert
    {
        DisassembledShader d = Disasm(blob);
        ConstantTable ct = readConstantTable(vpp::ByteView(blob.data(), blob.size()), d);
        CHECK(ct.status == CtabStatus::WellFormed);
        CHECK(ct.constants[0].type.classValue == ParameterClass::Vector);
        CHECK(ct.constants[0].type.columns == 4);
    }
}

}  // namespace

int main() {
    try {
        testScalarFloat();
        testVector();
        testMatrixRowsVsColumns();
        testSampler();
        testArray();
        testStruct();
        testNoCommentAtAll();
        testCommentNotCtab();
        testCtabCommentNotLeading();
        testMalformedHeaderTooSmall();
        testMalformedConstantsOverrun();
        testMutationConstantInfoOffset();
        testMutationTypeInfoShift();
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
