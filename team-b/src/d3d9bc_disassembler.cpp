// Implements sr3d3d9bc::disassemble() (disassembler.h) from spec-d3d9-sm2-
// sm3-bytecode.md alone.
//
// THE ONE STRUCTURAL JUDGMENT CALL THIS FILE MAKES, STATED UP FRONT: spec
// §6 states the general shape "most instructions are followed by one
// destination parameter token and zero or more source parameter tokens",
// but explicitly defers each individual opcode's own operand shape to its
// own (not transcribed) public reference page (§11). This decoder has no
// per-opcode operand-shape table, by design (the task this was built for
// forbids filling that in from outside knowledge the spec document itself
// doesn't state). So, for every non-DCL instruction, it decodes
// POSITIONALLY: the first DWORD in the instruction's length-delimited
// region (spec §9.1) that has bit 31 set (a valid parameter token per spec
// §6.1/§6.2's own "[31] Always 1" rule) is taken as the destination, and
// every following valid parameter token is taken as a source; if the
// predicate bit is set, the LAST parameter token taken is reassigned to
// predicateSource. This is exactly the spec's own stated general rule,
// applied uniformly rather than per opcode - it is known to be wrong for
// the minority of opcodes whose true shape differs (e.g. DEF/DEFB/DEFI
// carry literal immediate data, not source registers - a DEF payload
// DWORD's bit 31 will usually be 0 since it's a float's sign bit, so it is
// correctly NOT force-decoded as a source and instead lands in
// extraDwords; but a dest-less instruction whose sole operand is bit-31-set
// would still be misfiled as a "destination"). The WALK itself never loses
// sync regardless, because it is driven only by the instruction token's own
// length field (spec §9.1), never by this positional guess.

#include "sr3d3d9bc/disassembler.h"

#include "sr3d3d9bc/errors.h"

namespace sr3d3d9bc {
namespace {

uint32_t readU32LE(vpp::ByteView bytes, size_t offset) { return bytes.readU32LE(offset); }

// spec §6.1/§6.2: "[31] Always 1" for both destination and source parameter
// tokens. A DWORD without this bit set is therefore not a parameter token
// at all under this spec's own definition.
bool isParamToken(uint32_t token) { return (token & 0x80000000u) != 0; }

DestinationParam decodeDestToken(uint32_t token) {
    DestinationParam d;
    d.raw = token;
    d.registerTypeRaw = decodeRegisterTypeBits(token);
    d.registerNumber = static_cast<uint16_t>(token & 0x7FFu); // bits [10:0]
    const uint8_t wm = static_cast<uint8_t>((token >> 16) & 0xFu);
    d.writeMask.raw = wm;
    d.writeMask.x = (wm & 0x1u) != 0;
    d.writeMask.y = (wm & 0x2u) != 0;
    d.writeMask.z = (wm & 0x4u) != 0;
    d.writeMask.w = (wm & 0x8u) != 0;
    d.resultModifier.raw = static_cast<uint8_t>((token >> 20) & 0xFu);
    d.resultShiftScaleRaw = static_cast<uint8_t>((token >> 24) & 0xFu);
    d.hasRelativeAddressing = ((token >> 13) & 0x1u) != 0;
    return d;
}

SourceParam decodeSourceToken(uint32_t token) {
    SourceParam s;
    s.raw = token;
    s.registerTypeRaw = decodeRegisterTypeBits(token);
    s.registerNumber = static_cast<uint16_t>(token & 0x7FFu);
    s.swizzle.x = static_cast<uint8_t>((token >> 16) & 0x3u);
    s.swizzle.y = static_cast<uint8_t>((token >> 18) & 0x3u);
    s.swizzle.z = static_cast<uint8_t>((token >> 20) & 0x3u);
    s.swizzle.w = static_cast<uint8_t>((token >> 22) & 0x3u);
    s.modifier = static_cast<SourceModifier>((token >> 24) & 0xFu);
    s.hasRelativeAddressing = ((token >> 13) & 0x1u) != 0;
    return s;
}

// spec §6.3: "formatted exactly like a [source] parameter token"; only the
// register type/number and swizzle are meaningful, so decoded the same way
// a source token's fields are extracted.
RelativeAddressing decodeRelativeAddressing(uint32_t token) {
    RelativeAddressing r;
    r.raw = token;
    r.registerTypeRaw = decodeRegisterTypeBits(token);
    r.registerNumber = static_cast<uint16_t>(token & 0x7FFu);
    r.swizzle.x = static_cast<uint8_t>((token >> 16) & 0x3u);
    r.swizzle.y = static_cast<uint8_t>((token >> 18) & 0x3u);
    r.swizzle.z = static_cast<uint8_t>((token >> 20) & 0x3u);
    r.swizzle.w = static_cast<uint8_t>((token >> 22) & 0x3u);
    return r;
}

DclToken decodeDclToken(uint32_t token) {
    DclToken t;
    t.raw = token;
    t.samplerTextureType = static_cast<uint8_t>((token >> 27) & 0xFu);
    t.usage = static_cast<uint8_t>(token & 0x1Fu);
    t.usageIndex = static_cast<uint8_t>((token >> 16) & 0xFu);
    return t;
}

} // namespace

DisassembledShader disassemble(vpp::ByteView blob) {
    DisassembledShader shader;

    if (blob.size() < 4)
        throw FormatError("blob too small for a version token (spec §2)");
    const uint32_t v = readU32LE(blob, 0);
    const uint32_t hi = v & 0xFFFF0000u;
    if (hi != 0xFFFE0000u && hi != 0xFFFF0000u)
        throw FormatError(
            "version token's high 16 bits are neither 0xFFFE (vertex) nor 0xFFFF (pixel) - spec §2");
    shader.version.raw = v;
    shader.version.isVertexShader = (hi == 0xFFFE0000u);
    shader.version.major = static_cast<uint8_t>((v >> 8) & 0xFFu);
    shader.version.minor = static_cast<uint8_t>(v & 0xFFu);

    const size_t size = blob.size();
    size_t pos = 4;
    shader.status = WalkStatus::NoEndToken;

    while (pos + 4 <= size) {
        const uint32_t tok = readU32LE(blob, pos);

        // End token (spec §5): the full 32 bits, not just the low 16.
        if (tok == 0x0000FFFFu) {
            shader.endTokenOffset = pos;
            pos += 4;
            shader.status = (pos == size) ? WalkStatus::Ok : WalkStatus::TrailingBytes;
            shader.bytesConsumed = pos;
            return shader;
        }

        // Comment token (spec §4): low 16 bits only.
        if ((tok & 0xFFFFu) == 0xFFFEu) {
            const uint32_t payloadDwords = (tok >> 16) & 0x7FFFu;
            const size_t payloadStart = pos + 4;
            const size_t payloadEnd = payloadStart + static_cast<size_t>(payloadDwords) * 4;
            if (payloadEnd > size) {
                shader.status = WalkStatus::UnexpectedEnd;
                shader.bytesConsumed = pos;
                return shader;
            }
            CommentTokenInfo c;
            c.tokenOffset = pos;
            c.payloadDwords = payloadDwords;
            c.looksLikeCtab =
                payloadDwords >= 1 && readU32LE(blob, payloadStart) == 0x42415443u; // "CTAB" (spec §4 application note)
            shader.comments.push_back(c);
            pos = payloadEnd;
            continue;
        }

        // Ordinary instruction token (spec §3) - including PHASE (0xFFFD,
        // a real zero-operand instruction per spec §9) and any opcode this
        // decoder does not recognise; those are classified, not rejected.
        Instruction inst;
        inst.tokenOffset = pos;
        inst.rawToken = tok;
        inst.opcodeRaw = tok & 0xFFFFu;
        inst.opcode = static_cast<Opcode>(inst.opcodeRaw);
        inst.opcodeClass = classifyOpcode(inst.opcodeRaw);
        inst.controlBits = static_cast<uint8_t>((tok >> 16) & 0xFFu);
        inst.length = static_cast<uint8_t>((tok >> 24) & 0xFu);
        inst.predicate = ((tok >> 28) & 0x1u) != 0;
        inst.coIssue = ((tok >> 30) & 0x1u) != 0;

        const size_t bodyStart = pos + 4;
        const size_t bodyEnd = bodyStart + static_cast<size_t>(inst.length) * 4;
        if (bodyEnd > size) {
            shader.status = WalkStatus::UnexpectedEnd;
            shader.bytesConsumed = pos;
            return shader;
        }

        size_t cur = bodyStart;
        if (inst.opcodeRaw == static_cast<uint32_t>(Opcode::DCL)) {
            // DCL's own unique shape (spec §8): instruction token, one
            // plain DWORD, then one destination parameter token - no
            // ordinary source parameters.
            if (cur + 4 <= bodyEnd) {
                inst.dcl = decodeDclToken(readU32LE(blob, cur));
                cur += 4;
            }
            if (cur + 4 <= bodyEnd) {
                const uint32_t dtok = readU32LE(blob, cur);
                if (isParamToken(dtok)) {
                    DestinationParam d = decodeDestToken(dtok);
                    cur += 4;
                    if (d.hasRelativeAddressing && cur + 4 <= bodyEnd) {
                        d.relativeAddressing = decodeRelativeAddressing(readU32LE(blob, cur));
                        cur += 4;
                    }
                    inst.dest = d;
                } else {
                    inst.extraDwords.push_back(dtok);
                    cur += 4;
                }
            }
        } else {
            // General shape (spec §6) - see this file's top-of-file
            // comment for exactly what "positional" means and does not
            // claim to know.
            while (cur + 4 <= bodyEnd) {
                const uint32_t ptok = readU32LE(blob, cur);
                if (!isParamToken(ptok)) {
                    inst.extraDwords.push_back(ptok);
                    cur += 4;
                    continue;
                }
                if (!inst.dest.has_value()) {
                    DestinationParam d = decodeDestToken(ptok);
                    cur += 4;
                    if (d.hasRelativeAddressing && cur + 4 <= bodyEnd) {
                        d.relativeAddressing = decodeRelativeAddressing(readU32LE(blob, cur));
                        cur += 4;
                    }
                    inst.dest = d;
                } else {
                    SourceParam s = decodeSourceToken(ptok);
                    cur += 4;
                    if (s.hasRelativeAddressing && cur + 4 <= bodyEnd) {
                        s.relativeAddressing = decodeRelativeAddressing(readU32LE(blob, cur));
                        cur += 4;
                    }
                    inst.sources.push_back(s);
                }
            }
            if (inst.predicate && !inst.sources.empty()) {
                inst.predicateSource = inst.sources.back();
                inst.sources.pop_back();
            }
        }

        shader.instructions.push_back(std::move(inst));
        pos = bodyEnd;
    }

    shader.status = WalkStatus::NoEndToken;
    shader.bytesConsumed = pos;
    return shader;
}

} // namespace sr3d3d9bc
