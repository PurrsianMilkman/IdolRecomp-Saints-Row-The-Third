#pragma once

// Stage 1 of the orchestrator's 3-stage D3D9 shader plan: a structural
// disassembler for the PUBLIC Direct3D 9 shader bytecode format (spec-
// d3d9-sm2-sm3-bytecode.md), built ONLY from that document. It is not a
// semantic interpreter (see tokens.h's Instruction comment for exactly what
// that means for `dest`/`sources`) - it walks any real shader blob to
// completion, classifies every opcode it sees against the spec's own §9
// table, and reports anything outside that table as a genuine unknown
// (spec §10's "population gate").
//
// Real input comes from sr3fxo::ShaderWrapper::shaderBytes() or a blob
// range sr3fxo::WrapperHeader::layoutBlobs() locates - either already
// starts at the version token and ends at (and includes) the end token,
// per sr3fxo's own contract. This library does not depend on sr3fxo; it
// only consumes the byte range sr3fxo already identified.

#include "sr3d3d9bc/shader.h"
#include "vpp/byte_view.h"

namespace sr3d3d9bc {

// Disassembles the token stream in `blob`, starting at its version token
// (spec §1/§2) through to the end token (spec §5) or until the buffer is
// exhausted / a token claims more DWORDs than remain, whichever comes
// first. Never throws for a malformed/truncated/unrecognised STREAM
// interior - reported via DisassembledShader::status instead, matching
// this project's existing sr3fxo::ShaderWrapper convention of surfacing
// partial content rather than crashing. Throws FormatError only for the
// one CONFIRMED precondition spec §2 states unconditionally: `blob` must
// be at least 4 bytes, and its first DWORD's high 16 bits must be 0xFFFE
// or 0xFFFF.
//
// An unrecognised OPCODE (anything classifyOpcode() reports as
// UnknownGap/UnknownOther, see opcode.h) is not a decode failure either:
// the instruction is still walked correctly via its own length field (spec
// §9.1) and appended to instructions() with that classification, so a
// caller doing population-gate accounting (spec §10) can find it.
DisassembledShader disassemble(vpp::ByteView blob);

} // namespace sr3d3d9bc
