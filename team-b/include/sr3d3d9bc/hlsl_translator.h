#pragma once

// Stage 2 of the orchestrator's D3D9 shader plan: translates an already-
// disassembled shader (sr3d3d9bc::disassemble(), disassembler.h) plus its
// already-read constant table (sr3d3d9bc::readConstantTable(), ctab.h) into
// HLSL source text that a real HLSL compiler accepts and that implements the
// same arithmetic/control-flow the original SM2/3 bytecode does.
//
// Built ONLY from spec-d3d9-sm2-sm3-bytecode.md §12 (per-opcode HLSL-
// equivalent semantics, the 35 real opcodes this game's shaders use), §13
// (CTAB, for named constant/sampler declarations) and §8 (DCL's shapes, for
// input/output signature generation) - same discipline as disassembler.h/
// ctab.h. Where §12 explicitly flags a gap (RSQ's negative-input behavior,
// NRM's exact 4-D formula, IFC's comparison-operator bits), the judgment call
// actually taken is documented in d3d9bc_hlsl_translator.cpp's top comment
// and in TranslationResult::warnings, never silently asserted as fact.
//
// TARGET PROFILE: this translator emits HLSL for the LEGACY vs_3_0/ps_3_0
// shader model, not vs_4_0/ps_4_0 - see d3d9bc_hlsl_translator.cpp's top
// comment for why (it is a materially closer match to the source bytecode's
// own shader model: legacy `sampler2D`/`texCUBE` objects, `POSITION`/
// `COLORn`/`TEXCOORDn` semantics, and `register(cN)`-bound arrays all map
// far more directly than SM4's split Texture/SamplerState objects and
// SV_Position/SV_Target renaming would). d3dcompiler_47.dll's D3DCompile()
// still accepts these legacy targets.
//
// RAW BLOB ACCESS: `blob` must be the SAME byte range `shader` was produced
// from (i.e. `shader == sr3d3d9bc::disassemble(blob)`, matching ctab.h's own
// readConstantTable() contract). It is needed for exactly one thing: DEF/
// DEFI's four literal payload DWORDs. Stage 1's own disassembler does not
// special-case DEF/DEFI (documented limitation, disassembler.cpp's top
// comment / spec §12's DEFI row) - its generic positional parameter-token
// parse can mis-file an arbitrary literal DWORD as a SourceParam (and, worse,
// can mis-consume a FOLLOWING literal DWORD as that mis-filed source's own
// relative-addressing token if the literal's bit 13 happens to be set),
// silently shifting every later field's recovered raw value. This translator
// therefore never trusts Instruction::sources/extraDwords for a DEF/DEFI's
// literal payload - it recomputes each literal DWORD's byte offset directly
// from the instruction's own tokenOffset (instruction token + 1 dest token +
// 4 literal DWORDs, a fixed, unambiguous shape per spec §12's DEFI row) and
// reads it straight from `blob`.

#include <cstdint>
#include <string>
#include <vector>

#include "sr3d3d9bc/ctab.h"
#include "sr3d3d9bc/shader.h"
#include "vpp/byte_view.h"

namespace sr3d3d9bc {

// Which shader-model syntax translateToHlsl() emits. Added for the SM4/5-
// retargeting follow-up (see HANDOFF.md's own 2026-09-28 correction: the
// original SM3Legacy-only translator's "100% compiles" result used
// D3DCompile() against the SAME legacy vs_3_0/ps_3_0 target the bytecode
// already is, which produces more SM1-3 bytecode - NOT usable by this
// project's actual D3D11 renderer, which cannot load SM1-3 bytecode at
// all). Both targets share the identical instruction-walk/expression-
// building logic (every arithmetic opcode, control-flow frame, register
// collection pass) - only DECLARATION SYNTAX and a handful of output-field
// system-value semantics differ between them, which is exactly what this
// one enum parameter isolates, per hlsl_translator.h's own "your call"
// framing on dual-mode-vs-retarget. SM3Legacy is the DEFAULT (every
// existing call site - validate_d3d9bc_hlsl_population.cpp, numeric_verify_
// hlsl.cpp, tests/synthetic_d3d9bc_hlsl_test.cpp - keeps compiling and
// behaving byte-for-byte identically without being touched).
enum class HlslTarget {
    SM3Legacy, // vs_3_0/ps_3_0: loose `register(cN)` globals, sampler2D/texCUBE, POSITION/COLORn/DEPTH0 (unchanged, original behavior)
    SM4_5,     // vs_4_0/ps_4_0: cbuffer-bound constants, Texture2D<float4>+SamplerState with .Sample()/.SampleLevel(), SV_Position/SV_TargetN/SV_Depth
};

struct TranslationResult {
    // True iff every instruction in `shader` was translated into real HLSL
    // (no opcode/construct had to be skipped). `hlsl` is always populated
    // best-effort even when this is false - an untranslatable instruction
    // becomes a `// UNTRANSLATED: ...` comment at its position, not a hard
    // stop, so a caller doing population-gate accounting still gets a
    // compile attempt for the rest of the shader (matching this project's
    // standing "surface partial content, don't crash/abort" convention).
    bool complete = false;

    std::string hlsl;
    std::string entryPoint = "main";
    std::string targetProfile; // SM3Legacy: "vs_3_0"/"ps_3_0" (shader's own actual major.minor); SM4_5: "vs_4_0"/"ps_4_0"

    // Judgment calls taken at a real, spec-flagged open point (RSQ abs(),
    // NRM's 4-D-vs-3-D formula, IFC's comparison-bit mapping, DCL shapes
    // this translator had to fall back on) - never silent.
    std::vector<std::string> warnings;

    // Opcodes/constructs this specific shader needed that this translator
    // could not honestly emit (spec gap, or a construct outside the 35-
    // opcode real population this translator targets). Empty on every real
    // shader in this game's population is the expected/measured outcome
    // (spec §11/§12: the 35 opcodes covered here are exactly what the real
    // 7,276-blob population was measured to use) - a non-empty result here
    // on real data is a genuine finding, not expected noise.
    std::vector<std::string> unsupported;
};

// Translates one already-disassembled shader to HLSL. Never throws - any
// failure to translate a specific instruction is recorded in
// TranslationResult::unsupported (see above), matching this project's
// sr3d3d9bc::DisassembledShader / ConstantTable convention of surfacing
// partial results rather than crashing.
TranslationResult translateToHlsl(const DisassembledShader& shader, const ConstantTable& ctab, vpp::ByteView blob,
                                   HlslTarget target = HlslTarget::SM3Legacy);

} // namespace sr3d3d9bc
