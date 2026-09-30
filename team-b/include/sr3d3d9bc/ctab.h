#pragma once

// Reader for the `CTAB` sub-chunk (the standard D3DX9 shader-reflection
// constant table) carried inside the LEADING comment token of every real
// D3D9 shader this game ships - spec-d3d9-sm2-sm3-bytecode.md §13, built
// ONLY from that document, same discipline as sr3d3d9bc::disassemble()
// itself (disassembler.h/opcode.h/tokens.h). Extends, does not replace,
// sr3fxo::inspectD3d9Blob's existing minimal name/register-only reader
// (include/sr3fxo/d3d9_blob.h) with the D3DXSHADER_TYPEINFO/
// D3DXSHADER_STRUCTMEMBERINFO layer that reader does not parse.
//
// Input is a shader blob's raw bytes PLUS the sr3d3d9bc::disassemble()
// result already computed for that SAME blob - this reader does not
// independently re-scan the token stream for its own comment tokens. It
// reuses DisassembledShader::comments to find the candidate CTAB comment's
// offset/size, and DisassembledShader::instructions only to confirm that
// candidate genuinely PRECEDES the first real instruction (spec §13's own
// "leading comment token" framing, and spec §13.4's "no comment token
// before the first real instruction at all" branch of the population-gate
// bucket definition) - a comment token encountered after an instruction
// would not be "leading" even if its payload happened to start with the
// CTAB FourCC.
//
// Never throws for a malformed/truncated CTAB payload - reported via
// ConstantTable::status instead (CtabStatus), matching this project's
// existing sr3d3d9bc::DisassembledShader::status and
// sr3fxo::ShaderWrapper conventions of surfacing partial/absent content
// rather than crashing.
//
// See src/d3d9bc_ctab.cpp's top comment for how this file resolves the one
// genuine ambiguity spec §13 itself flags: whether a D3DXPC_STRUCT-typed
// constant's D3DXSHADER_STRUCTMEMBERINFO entries are read via an
// indirection DWORD or read CONTIGUOUSLY starting right after TYPEINFO's
// 12-byte fixed header - this reader implements the spec's own stated
// resolution (contiguous, no indirection), and the accompanying validation
// tool reports whatever real struct-typed constants it actually finds.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "sr3d3d9bc/shader.h"
#include "vpp/byte_view.h"

namespace sr3d3d9bc {

// `D3DXREGISTER_SET` (spec §13.1) - which of the four physically-separate
// constant memories a CONSTANTINFO entry's own RegisterIndex counts within.
// This is a DIFFERENT enumeration from register_type.h's D3DSPR_* operand
// register-type field (the bytecode's own instruction-operand register
// kind) - CTAB's RegisterSet is coarser, spec §13.1.
enum class RegisterSet : uint16_t {
    Bool = 0,    // D3DXRS_BOOL
    Int4 = 1,    // D3DXRS_INT4
    Float4 = 2,  // D3DXRS_FLOAT4
    Sampler = 3, // D3DXRS_SAMPLER
};

// `D3DXPARAMETER_CLASS` (spec §13.2).
enum class ParameterClass : uint16_t {
    Scalar = 0,        // D3DXPC_SCALAR
    Vector = 1,        // D3DXPC_VECTOR
    MatrixRows = 2,     // D3DXPC_MATRIX_ROWS
    MatrixColumns = 3,  // D3DXPC_MATRIX_COLUMNS
    Object = 4,          // D3DXPC_OBJECT (samplers/textures fall here)
    Struct = 5,          // D3DXPC_STRUCT
};

// `D3DXPARAMETER_TYPE` (spec §13.3) - full enum; only the ones a real
// constant/sampler in this game's shaders can be are relevant in practice.
enum class ParameterType : uint16_t {
    Void = 0,         // D3DXPT_VOID
    Bool = 1,         // D3DXPT_BOOL
    Int = 2,          // D3DXPT_INT
    Float = 3,        // D3DXPT_FLOAT (the overwhelming majority of real constants, per spec §13.3)
    String = 4,       // D3DXPT_STRING
    Texture = 5,      // D3DXPT_TEXTURE
    Texture1D = 6,    // D3DXPT_TEXTURE1D
    Texture2D = 7,    // D3DXPT_TEXTURE2D
    Texture3D = 8,    // D3DXPT_TEXTURE3D
    TextureCube = 9,  // D3DXPT_TEXTURECUBE
    Sampler = 10,     // D3DXPT_SAMPLER
    Sampler1D = 11,   // D3DXPT_SAMPLER1D
    Sampler2D = 12,   // D3DXPT_SAMPLER2D
    Sampler3D = 13,   // D3DXPT_SAMPLER3D
    SamplerCube = 14, // D3DXPT_SAMPLERCUBE
};

struct StructMemberInfo;

// `D3DXSHADER_TYPEINFO` (spec §13's 12-byte fixed header: Class/Type/Rows/
// Columns/Elements/StructMembers, each a WORD). `structMemberInfo` is
// populated only when classRaw == D3DXPC_STRUCT (5) and structMembers > 0 -
// see this header's own top comment and ctab.cpp for the layout ambiguity
// spec §13 flags and how it is resolved.
struct TypeInfo {
    uint16_t classRaw = 0;
    uint16_t typeRaw = 0;
    ParameterClass classValue = ParameterClass::Scalar; // static_cast<ParameterClass>(classRaw) - may not name a known enumerator, same convention as sr3d3d9bc::Instruction::opcode
    ParameterType typeValue = ParameterType::Void;       // static_cast<ParameterType>(typeRaw), same convention
    uint16_t rows = 0;
    uint16_t columns = 0;
    uint16_t elements = 0;      // array length; 1 if not an array (spec §13)
    uint16_t structMembers = 0; // only meaningful when classRaw == D3DXPC_STRUCT
    std::vector<StructMemberInfo> structMemberInfo;
};

// `D3DXSHADER_STRUCTMEMBERINFO` (spec §13, 8 bytes): a member Name plus a
// nested TypeInfo, recursively (a struct member can itself be a struct).
struct StructMemberInfo {
    std::string name;
    TypeInfo typeInfo;
};

// `D3DXSHADER_CONSTANTINFO` (spec §13, 20 bytes per entry).
struct ConstantInfo {
    std::string name;
    uint16_t registerSetRaw = 0;
    RegisterSet registerSet = RegisterSet::Bool; // static_cast<RegisterSet>(registerSetRaw)
    uint16_t registerIndex = 0;
    uint16_t registerCount = 0;
    uint32_t typeInfoOffset = 0;     // table-relative, kept for diagnostics (also folded into `type` already)
    uint32_t defaultValueOffset = 0; // table-relative, 0 if none - spec §13: existing code doesn't read this and a translator generally doesn't need it either, so its payload is not interpreted here, only the raw offset kept
    TypeInfo type;
};

// This reader's own population-gate classification (spec §13.4). Never
// thrown - see this file's top comment.
enum class CtabStatus {
    // Spec §13.4's "explicitly none" bucket: either no comment token
    // precedes the first real instruction at all, or one does but its
    // payload does not start with the CTAB FourCC.
    NotPresent,
    // Structurally valid per spec §13: the CONSTANTTABLE/CONSTANTINFO/
    // TYPEINFO/STRUCTMEMBERINFO byte ranges this reader computed all land
    // inside the comment payload.
    WellFormed,
    // The comment payload DOES start with the CTAB FourCC but fails a
    // structural check below (malformedReason says which). Not expected on
    // any real compiler-emitted blob - spec §13.4 expects every real blob
    // to land in exactly one of the two buckets above; a Malformed result
    // is therefore a genuine finding, not a silently-absorbed third
    // outcome.
    Malformed,
};

// `D3DXSHADER_CONSTANTTABLE` (spec §13, 28-byte fixed header) plus its
// parsed constants.
struct ConstantTable {
    CtabStatus status = CtabStatus::NotPresent;
    std::string malformedReason; // set only when status == Malformed

    size_t commentTokenOffset = 0; // blob-relative offset of the comment token itself; 0 (meaningless) when status == NotPresent
    size_t tableBaseOffset = 0;    // blob-relative byte offset "t" (spec §13's own base, the DWORD after the FourCC) that every table-relative offset below is measured from

    uint32_t size = 0;    // D3DXSHADER_CONSTANTTABLE::Size - not otherwise interpreted (spec does not define what a translator should do with it)
    std::string creator;  // table-relative string at Creator offset
    uint32_t version = 0;
    uint32_t constantsCount = 0;
    uint32_t constantInfoOffset = 0; // table-relative, kept for diagnostics
    uint32_t flags = 0;
    std::string target; // table-relative string at Target offset, e.g. "vs_3_0"

    std::vector<ConstantInfo> constants;
};

// Parses the CTAB constant table for one already-disassembled shader blob.
// `blob` MUST be the same byte range `disassembled` was produced from
// (i.e. `disassembled == sr3d3d9bc::disassemble(blob)`); this function
// trusts `disassembled`'s own comment/instruction offsets rather than
// re-walking the token stream itself.
ConstantTable readConstantTable(vpp::ByteView blob, const DisassembledShader& disassembled);

} // namespace sr3d3d9bc
