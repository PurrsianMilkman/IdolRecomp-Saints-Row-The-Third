// Implements sr3d3d9bc::readConstantTable() (ctab.h) from spec-d3d9-sm2-
// sm3-bytecode.md §13 alone.
//
// THE ONE JUDGMENT CALL THIS FILE MAKES, STATED UP FRONT (spec §13's own
// flagged ambiguity): `D3DXSHADER_TYPEINFO` is documented as a 12-byte
// fixed header (Class/Type/Rows/Columns/Elements/StructMembers, six WORDs)
// followed by a field the spec prose labels "StructMemberInfo(+12...)" -
// which, read as a literal field the way every other offset in this format
// is (Name/TypeInfo/Creator/Target/ConstantInfo are all DWORD offsets
// elsewhere in this same spec), would make the struct 16 bytes, not the 12
// the spec explicitly states, or 14 if read as a bare WORD - neither of
// which matches "12 bytes" as written. The spec's own note resolves this
// explicitly rather than leaving it to guesswork: "the struct is documented
// as returning contiguous D3DXSHADER_STRUCTMEMBERINFO entries starting at
// this offset - a translator should read StructMembers-many
// D3DXSHADER_STRUCTMEMBERINFO entries there when Class == D3DXPC_STRUCT".
// This file implements exactly that: for a D3DXPC_STRUCT-classed TYPEINFO,
// StructMembers-many 8-byte D3DXSHADER_STRUCTMEMBERINFO entries are read
// CONTIGUOUSLY starting immediately after the 12-byte fixed header (i.e. at
// this TYPEINFO's own table offset + 12) - no indirection DWORD is read or
// assumed. No real D3DXPC_STRUCT-classed constant was found in the 7,276-
// blob population this was validated against (see
// tools/validation/validate_d3d9bc_ctab_population.cpp's report) - so this
// resolution rests on the spec's own explicit wording, not on having
// exercised it against real data. If a real struct-typed constant ever
// turns up, that population run reports its raw bytes rather than silently
// trusting this interpretation.

#include "sr3d3d9bc/ctab.h"

#include <limits>

namespace sr3d3d9bc {
namespace {

// Note: the CTAB FourCC itself (0x42415443, "CTAB") is not re-checked here
// - disassemble() already sniffs it into CommentTokenInfo::looksLikeCtab
// (spec §4's own application note), which is what readConstantTable()
// below consults rather than re-scanning the payload itself.
constexpr int kMaxStructDepth = 8; // defensive recursion guard against a malformed/cyclic nested TypeInfo offset - no real shader is expected to need anywhere near this many nested struct levels

// True iff [start, start+len) fits entirely within [0, limit) - computed
// this way (rather than `start + len <= limit`) so it stays correct even if
// `start` is already close to size_t's max (an adversarial/corrupt table
// offset), never overflowing the addition.
bool fits(size_t start, size_t len, size_t limit) { return start <= limit && len <= limit - start; }

bool readU16(vpp::ByteView blob, size_t offset, size_t limit, uint16_t& out) {
    if (!fits(offset, 2, limit)) return false;
    out = blob.readU16LE(offset);
    return true;
}

bool readU32(vpp::ByteView blob, size_t offset, size_t limit, uint32_t& out) {
    if (!fits(offset, 4, limit)) return false;
    out = blob.readU32LE(offset);
    return true;
}

// Reads a NUL-terminated ASCII string at absolute (blob-relative) `offset`,
// stopping at `limit` - spec §13: "ordinary null-terminated ASCII text".
// Returns false (a structural violation) if no NUL byte occurs before
// `limit`, i.e. the string is not fully contained in the comment payload.
bool readCString(vpp::ByteView blob, size_t offset, size_t limit, std::string& out) {
    if (offset > limit) return false;
    for (size_t i = offset; i < limit; ++i) {
        if (blob.data()[i] == 0) {
            out.assign(reinterpret_cast<const char*>(blob.data() + offset), i - offset);
            return true;
        }
    }
    return false;
}

bool parseTypeInfo(vpp::ByteView blob, size_t t, uint32_t typeInfoOff, size_t payloadEnd, int depth, TypeInfo& out,
                    std::string& err);

bool parseStructMemberInfo(vpp::ByteView blob, size_t t, size_t entryOffset, size_t payloadEnd, int depth,
                            StructMemberInfo& out, std::string& err) {
    uint32_t nameOff = 0, nestedTypeOff = 0;
    if (!readU32(blob, entryOffset + 0, payloadEnd, nameOff) ||
        !readU32(blob, entryOffset + 4, payloadEnd, nestedTypeOff)) {
        err = "STRUCTMEMBERINFO entry at table-relative byte " + std::to_string(entryOffset - t) +
              " overruns the comment payload";
        return false;
    }
    if (!readCString(blob, t + nameOff, payloadEnd, out.name)) {
        err = "STRUCTMEMBERINFO Name string (table offset " + std::to_string(nameOff) +
              ") is not NUL-terminated within the comment payload";
        return false;
    }
    return parseTypeInfo(blob, t, nestedTypeOff, payloadEnd, depth + 1, out.typeInfo, err);
}

bool parseTypeInfo(vpp::ByteView blob, size_t t, uint32_t typeInfoOff, size_t payloadEnd, int depth, TypeInfo& out,
                    std::string& err) {
    if (depth > kMaxStructDepth) {
        err = "TYPEINFO struct-member recursion exceeded depth " + std::to_string(kMaxStructDepth) +
              " (cyclic/malformed TypeInfo offset?)";
        return false;
    }
    const size_t base = t + typeInfoOff;
    uint16_t classRaw = 0, typeRaw = 0, rows = 0, columns = 0, elements = 0, structMembers = 0;
    if (!readU16(blob, base + 0, payloadEnd, classRaw) || !readU16(blob, base + 2, payloadEnd, typeRaw) ||
        !readU16(blob, base + 4, payloadEnd, rows) || !readU16(blob, base + 6, payloadEnd, columns) ||
        !readU16(blob, base + 8, payloadEnd, elements) || !readU16(blob, base + 10, payloadEnd, structMembers)) {
        err = "TYPEINFO at table offset " + std::to_string(typeInfoOff) +
              " (12-byte fixed header) overruns the comment payload";
        return false;
    }
    out.classRaw = classRaw;
    out.typeRaw = typeRaw;
    out.classValue = static_cast<ParameterClass>(classRaw);
    out.typeValue = static_cast<ParameterType>(typeRaw);
    out.rows = rows;
    out.columns = columns;
    out.elements = elements;
    out.structMembers = structMembers;

    if (classRaw == static_cast<uint16_t>(ParameterClass::Struct) && structMembers > 0) {
        // Spec §13's own resolution (see this file's top comment): read
        // StructMembers-many 8-byte STRUCTMEMBERINFO entries CONTIGUOUSLY
        // starting right after this 12-byte header.
        const size_t membersStart = base + 12;
        out.structMemberInfo.resize(structMembers);
        for (uint16_t i = 0; i < structMembers; ++i) {
            const size_t entryOffset = membersStart + static_cast<size_t>(i) * 8;
            if (!parseStructMemberInfo(blob, t, entryOffset, payloadEnd, depth, out.structMemberInfo[i], err))
                return false;
        }
    }
    return true;
}

}  // namespace

ConstantTable readConstantTable(vpp::ByteView blob, const DisassembledShader& disassembled) {
    ConstantTable result;

    if (disassembled.comments.empty()) {
        result.status = CtabStatus::NotPresent; // "no comment token before the first real instruction at all" (spec §13.4)
        return result;
    }
    const CommentTokenInfo& first = disassembled.comments.front();
    const size_t firstInstrOffset = disassembled.instructions.empty()
                                         ? (std::numeric_limits<size_t>::max)()
                                         : disassembled.instructions.front().tokenOffset;
    if (first.tokenOffset >= firstInstrOffset) {
        // A real instruction preceded this comment token - it is not the
        // LEADING comment spec §13 describes, so this blob counts as
        // having no CTAB regardless of what any later comment contains.
        result.status = CtabStatus::NotPresent;
        return result;
    }
    if (!first.looksLikeCtab) {
        result.status = CtabStatus::NotPresent; // comment token present but its payload doesn't start with the CTAB FourCC
        return result;
    }

    result.commentTokenOffset = first.tokenOffset;
    const size_t payloadStart = first.tokenOffset + 4;
    const size_t payloadEnd = payloadStart + static_cast<size_t>(first.payloadDwords) * 4;
    // payloadEnd <= blob.size() is already guaranteed by disassemble() -
    // it only appends a CommentTokenInfo after checking exactly this, so
    // it is trusted here rather than re-checked.
    const size_t t = payloadStart + 4; // spec §13: table-relative base, the DWORD after the FourCC (matches sr3fxo::inspectD3d9Blob's own "t")
    result.tableBaseOffset = t;

    uint32_t sizeField = 0, creatorOff = 0, version = 0, constantsCount = 0, constantInfoOff = 0, flags = 0,
             targetOff = 0;
    if (!readU32(blob, t + 0, payloadEnd, sizeField) || !readU32(blob, t + 4, payloadEnd, creatorOff) ||
        !readU32(blob, t + 8, payloadEnd, version) || !readU32(blob, t + 12, payloadEnd, constantsCount) ||
        !readU32(blob, t + 16, payloadEnd, constantInfoOff) || !readU32(blob, t + 20, payloadEnd, flags) ||
        !readU32(blob, t + 24, payloadEnd, targetOff)) {
        result.status = CtabStatus::Malformed;
        result.malformedReason = "CONSTANTTABLE 28-byte fixed header overruns the comment payload";
        return result;
    }
    result.size = sizeField;
    result.version = version;
    result.constantsCount = constantsCount;
    result.constantInfoOffset = constantInfoOff;
    result.flags = flags;

    if (!readCString(blob, t + creatorOff, payloadEnd, result.creator)) {
        result.status = CtabStatus::Malformed;
        result.malformedReason =
            "Creator string (table offset " + std::to_string(creatorOff) + ") is not NUL-terminated within the comment payload";
        return result;
    }
    if (!readCString(blob, t + targetOff, payloadEnd, result.target)) {
        result.status = CtabStatus::Malformed;
        result.malformedReason =
            "Target string (table offset " + std::to_string(targetOff) + ") is not NUL-terminated within the comment payload";
        return result;
    }

    const size_t constantInfoStart = t + constantInfoOff;
    if (!fits(constantInfoStart, static_cast<size_t>(constantsCount) * 20, payloadEnd)) {
        result.status = CtabStatus::Malformed;
        result.malformedReason = "Constants (" + std::to_string(constantsCount) +
                                  ") * 20 bytes does not fit inside the comment payload from ConstantInfo offset " +
                                  std::to_string(constantInfoOff);
        return result;
    }

    result.constants.resize(constantsCount);
    for (uint32_t i = 0; i < constantsCount; ++i) {
        const size_t e = constantInfoStart + static_cast<size_t>(i) * 20;
        ConstantInfo& c = result.constants[i];
        uint32_t nameOff = 0, typeInfoOff = 0, defaultValueOff = 0;
        uint16_t registerSetRaw = 0, registerIndex = 0, registerCount = 0;
        if (!readU32(blob, e + 0, payloadEnd, nameOff) || !readU16(blob, e + 4, payloadEnd, registerSetRaw) ||
            !readU16(blob, e + 6, payloadEnd, registerIndex) || !readU16(blob, e + 8, payloadEnd, registerCount) ||
            !readU32(blob, e + 12, payloadEnd, typeInfoOff) || !readU32(blob, e + 16, payloadEnd, defaultValueOff)) {
            result.status = CtabStatus::Malformed;
            result.malformedReason =
                "CONSTANTINFO[" + std::to_string(i) + "] at table offset " + std::to_string(e - t) +
                " overruns the comment payload";
            return result;
        }
        if (!readCString(blob, t + nameOff, payloadEnd, c.name)) {
            result.status = CtabStatus::Malformed;
            result.malformedReason = "CONSTANTINFO[" + std::to_string(i) + "] Name string (table offset " +
                                      std::to_string(nameOff) + ") is not NUL-terminated within the comment payload";
            return result;
        }
        c.registerSetRaw = registerSetRaw;
        c.registerSet = static_cast<RegisterSet>(registerSetRaw);
        c.registerIndex = registerIndex;
        c.registerCount = registerCount;
        c.typeInfoOffset = typeInfoOff;
        c.defaultValueOffset = defaultValueOff;

        std::string err;
        if (!parseTypeInfo(blob, t, typeInfoOff, payloadEnd, 0, c.type, err)) {
            result.status = CtabStatus::Malformed;
            result.malformedReason = "CONSTANTINFO[" + std::to_string(i) + "] (\"" + c.name + "\") " + err;
            return result;
        }
    }

    result.status = CtabStatus::WellFormed;
    return result;
}

}  // namespace sr3d3d9bc
