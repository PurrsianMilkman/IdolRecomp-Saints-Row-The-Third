// Implements sr3d3d9bc::translateToHlsl() (hlsl_translator.h) from
// spec-d3d9-sm2-sm3-bytecode.md §12 (per-opcode semantics), §13 (CTAB) and
// §8 (DCL) ONLY - same discipline as disassembler.cpp/ctab.cpp.
//
// JUDGMENT CALLS TAKEN AT THE SPEC'S OWN FLAGGED-OPEN POINTS (stated up
// front, never silently asserted as settled fact - each is also pushed to
// TranslationResult::warnings when actually exercised):
//
//  - RSQ: spec §12 explicitly says "emit rsqrt(abs(x)) only if empirically
//    needed... flag this rather than guess". No numeric oracle proved it
//    needed, so this translator emits the LITERAL/conservative
//    `rsqrt(x)` (no abs), matching the spec's own "only if empirically
//    needed" framing of abs() as the thing to ADD, not the default.
//
//  - NRM: spec §12 quotes the public page's own text, "Normalizes a 4-D
//    vector," as fact (not flagged open in the same way RSQ is - only the
//    *per-channel formula beyond that* is flagged open). This translator
//    reads all 4 components of src0 (via its own swizzle) and applies
//    HLSL's `normalize()` to the full float4, matching that quoted text
//    literally.
//
//  - IFC's comparison-operator bits: spec §12 explicitly flags this as
//    "not yet pulled... pull it the same way if/when IFC support is
//    actually being implemented." Pulled for this translator from the SAME
//    public mirror of the real SDK header spec-d3d9-sm2-sm3-bytecode.md
//    itself already cites for other enums (apitrace/dxsdk's
//    Include/d3d9types.h) - the `_D3DSHADER_COMPARISON` enum there reads:
//    D3DSPC_RESERVED0=0, D3DSPC_GT=1, D3DSPC_EQ=2, D3DSPC_GE=3, D3DSPC_LT=4,
//    D3DSPC_NE=5, D3DSPC_LE=6, D3DSPC_RESERVED1=7. Microsoft Learn's own
//    IFC/instruction-token prose pages do not spell out WHICH bits of the
//    instruction token's [23:16] control-bits field carry this 3-bit value
//    (checked directly - neither page states it), so the exact bit
//    POSITION (as opposed to the enum's own numeric values, which the
//    header confirms unambiguously) is this translator's own judgment
//    call: bits [18:16] (`controlBits & 0x7`), the universal convention
//    used by every other public D3D9 SM2/3 reference implementation this
//    session cross-checked (Wine's wined3d, MojoShader) and independently
//    corroborated by this project's own already-recorded real-data finding
//    (HANDOFF.md §9.97: Microsoft's real D3DDisassemble rendered a genuine
//    real IFC instance as "if_lt" - LT is enum value 4 - which is only
//    possible if SOME 3-bit subfield of that instance's control byte held
//    exactly 4). Reserved values 0/7 are treated as an honest unsupported
//    construct (flagged), never guessed.
//
//  - Source modifiers (spec §6.4, applied uniformly per §12's own framing):
//    Negate/Abs/NegateAbs are self-evident from their own PUBLIC names (no
//    formula needed). Bias/BiasAndNegate/Sign/SignAndNegate/Complement are
//    NOT given any arithmetic formula by either spec-d3d9-sm2-sm3-
//    bytecode.md OR Microsoft's own source-parameter-token page (checked
//    directly - the page names them but states no formula at all, same
//    "explicitly silent" gap) - this translator does NOT guess x-0.5 /
//    2x-1 / 1-x for them; it flags them unsupported instead. X2/
//    X2AndNegate/DivideByZ/DivideByW are PS 1.4-only (this game is
//    confirmed vs_3_0/ps_3_0-only, spec §12's SINCOS row) and LogicalNot
//    only applies to the predicate register (never written in this game's
//    real corpus - SETP is absent per HANDOFF's own opcode census) - all
//    flagged unsupported if ever actually seen, not implemented.
//
//  - Result modifier Saturate (spec §6.1's `[23:20]` bit 0x1, NOT restated
//    in §12's own per-opcode table): implemented (wraps the RHS in HLSL's
//    `saturate()`) despite being outside §12's literal per-opcode table,
//    because unlike Bias/Sign/Complement its meaning is completely
//    unambiguous (a single universally-named clamp-to-[0,1] operation,
//    same confidence tier as an opcode name like ABS needing no further
//    formula) and it is common enough in real compiler output that
//    silently dropping it would make many real shaders numerically wrong
//    rather than just "less complete." PartialPrecision/Centroid (the
//    other two result-modifier bits) are precision/interpolation HINTS
//    with no effect on the arithmetic result - intentionally ignored, not
//    a gap.
//
//  - DCL's "Face register (PS 3.0+)" / "Position register" shapes (spec
//    §8's table): spec §7's own register-type table names `D3DSPR_
//    MISCTYPE` as covering exactly this ("Miscellaneous single registers
//    (e.g. face/position in PS 3.0 DCL)"), so this translator resolves
//    that register TYPE correctly, but the spec text nowhere states which
//    MiscType register NUMBER means "face" vs "position" (the well-known
//    VFACE=0/VPOS=1 convention is NOT in this project's own spec
//    document), so this translator does not assert that identity - it
//    maps a MiscType-kind register generically to a plain interpolated
//    float4 input (fallback TEXCOORD-shaped semantic) and records a
//    warning every time this DCL shape is actually seen on real data,
//    rather than silently assuming which special register it is.
//
// TARGET PROFILE: vs_3_0 / ps_3_0 (legacy shader model, matching the
// source bytecode's own actual shader model exactly, per spec-fxo-
// format.md's confirmed vs_3_0/ps_3_0-only population) - not vs_4_0/
// ps_4_0. d3dcompiler_47.dll's D3DCompile() still accepts these legacy
// targets. This is a deliberate deviation from the task's suggested
// vs_4_0/ps_4_0 default: retargeting to SM4 would require renaming
// POSITION/COLORn output semantics to SV_Position/SV_Target (an SM4-only
// linkage rule with no counterpart in the source format), splitting every
// sampler into separate Texture2D/SamplerState objects (SM4 dropped the
// combined `sampler2D`/`tex2D()` legacy object model entirely), and
// generally adds translation surface area that has nothing to do with the
// bytecode's own semantics - all of which vs_3_0/ps_3_0 sidesteps because
// it IS the bytecode's own actual model. See HlslTranslation's own
// targetProfile field for exactly what each translated shader used.
//
// SM4/5 RETARGETING FOLLOW-UP (2026-09-28, added after the above was
// already reported CLOSED): the "100% compiles" result above is real but
// was measured against the SAME legacy vs_3_0/ps_3_0 target the source
// bytecode already is - D3DCompile() accepting that HLSL just produces
// more SM1-3 bytecode, which this project's actual renderer (D3D11, per
// HANDOFF.md §9.1's own long-standing constraint - this sandbox's SDKs
// don't even ship d3d9.h) cannot load at all. `HlslTarget::SM4_5` (see
// hlsl_translator.h) is a SECOND emission mode added alongside the
// original (kept working, unmodified default), sharing every line of
// instruction-walk/expression-building logic below - only the four things
// SM4/5 syntax genuinely requires differ, each gated on `target_ ==
// HlslTarget::SM4_5` at its own single point of emission:
//   1. Constants live in one `cbuffer Constants : register(b0) { ... }`
//      wrapping the SAME float4 c[]/int4 i[]/bool b[] arrays (unchanged
//      indexing/relative-addressing logic - only the declaration's outer
//      braces differ), not loose `register(cN)` globals.
//   2. Samplers split into `Texture2D<float4> Name : register(tN);` /
//      `TextureCube<float4> Name : register(tN);` plus a second
//      `SamplerState NameSampler : register(sN);` object; `tex2D`/
//      `texCUBE`/`tex2Dlod`/`texCUBElod` calls become `Name.Sample(
//      NameSampler, ...)` / `Name.SampleLevel(NameSampler, ..., lod)`.
//   3. VS output's POSITION-usage field (the one the rasterizer consumes)
//      is declared `SV_Position` (no numeric index - confirmed empirically
//      that a SUFFIXED `SV_Position0` and even the legacy `POSITION0`
//      spelling both still compile standalone under D3DCompile, since
//      D3DCompile type-checks a shader in isolation with no pipeline-
//      linkage knowledge of whether a rasterizer stage follows - but only
//      bare `SV_Position` is what an actual D3D11 pipeline requires to
//      treat the field as the rasterizer position, which is the real
//      point of this whole follow-up, so that is what is emitted
//      regardless of D3DCompile alone tolerating the legacy spelling).
//      Every OTHER output/input field (TEXCOORDn/COLORn/NORMALn/etc, as
//      confirmed by this follow-up's own tools/validation/
//      d3dctest_semantics_sm4.cpp probe) keeps its ordinary D3DDECLUSAGE-
//      derived name completely unchanged at SM4/5 - arbitrary (non-SV_)
//      semantic names are still just linkage identifiers in SM4/5, not a
//      legacy-only construct, for BOTH vertex-shader input and pixel-
//      shader input (the probe's own case 9 confirmed `POSITION0`/
//      `TEXCOORD0`/`COLOR0`/`NORMAL0` all still compile fine as ordinary
//      ps_4_0 INPUT semantics too).
//   4. PS output color targets are declared `SV_TargetN` (not `COLORN` -
//      the probe's own case 5 confirmed the legacy spelling is REJECTED
//      outright at ps_4_0, error X4502 "invalid ps_4_0 output semantic
//      'COLOR0'"), and depth output is `SV_Depth` (not `DEPTH0`). The
//      SM3 path's own COLORn-contiguity-from-0 gap-fill (needed there
//      because real ps_3_0 bytecode was found to trip D3DCompile's X4538
//      "COLOR outputs must be contiguous from COLOR0") is KEPT for SM4/5
//      too even though the probe found SM4/5's SV_TargetN does NOT
//      actually enforce that contiguity (case 7: an SV_Target0+SV_Target2
//      gap with no SV_Target1 compiled fine) - harmless either way, and
//      sharing the exact same fill logic for both targets is simpler than
//      forking it over a rule that turned out not to matter in practice
//      (0 real multi-render-target shaders in this game's population per
//      the SM3 pass's own finding).
// Everything else - every opcode's arithmetic translation, control-flow
// frame handling, DEF/DEFI literal recovery, register collection, source-
// modifier/result-modifier application - is IDENTICAL between the two
// targets, deliberately: duplicating ~1,100 lines of already-verified
// logic into a second file risked the two paths silently drifting apart,
// which a single shared class with one narrow target-gated branch point
// per genuine syntax difference does not.
//
// REGISTER FILE DECLARATION STYLE (a deliberate, documented judgment call,
// per hlsl_translator.h's own "your call" framing): float/int/bool
// constant registers are declared as ONE indexable array per bank
// (`float4 c[N] : register(c0);` etc.), not as individually-named globals
// per CTAB constant. This is required for correctness, not just style: a
// handful of this game's real vertex shaders use relative addressing
// (`c[a0.x + N]`, spec §6.3) to index a contiguous RUN of constant
// registers (e.g. a bone-matrix palette) that CTAB may describe as several
// separate named constants or one array constant depending on how the
// original HLSL was authored - only a single indexable array can be
// legally read with a register-file-relative runtime index in HLSL, a
// named per-constant declaration cannot. Real CTAB names are still
// preserved as a `c<index>..c<index+count-1> : Name` comment block
// immediately above the array declaration for traceability. Samplers,
// which are never relative-addressed in this instruction set, ARE
// declared individually by their real CTAB name (`sampler2D Name :
// register(sN);`), matching the task's own explicit ask for those.

#include "sr3d3d9bc/hlsl_translator.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <sstream>

namespace sr3d3d9bc {
namespace {

// ---------------------------------------------------------------------------
// Register-kind resolution (spec §7's own table, already structurally
// decoded by register_type.h/disassembler.cpp - this just applies §7's
// documented shader-type/version overload resolution the structural decoder
// deliberately leaves to a caller).
// ---------------------------------------------------------------------------
enum class RegKind {
    Temp, TempF16, Input, ConstFloat, ConstFloat2, ConstFloat3, ConstFloat4,
    Addr, RastOut, AttrOut, TexCrdOut, Output, ConstInt, ColorOut, DepthOut,
    Sampler, ConstBool, Loop, MiscType, Label, Predicate, PSTexture, Unknown
};

RegKind resolveKind(uint32_t raw, bool isVertex, uint8_t major) {
    switch (raw) {
        case 0: return RegKind::Temp;
        case 1: return RegKind::Input;
        case 2: return RegKind::ConstFloat;
        case 3: return isVertex ? RegKind::Addr : RegKind::PSTexture;
        case 4: return RegKind::RastOut;
        case 5: return RegKind::AttrOut;
        case 6: return isVertex ? (major >= 3 ? RegKind::Output : RegKind::TexCrdOut) : RegKind::Unknown;
        case 7: return RegKind::ConstInt;
        case 8: return RegKind::ColorOut;
        case 9: return RegKind::DepthOut;
        case 10: return RegKind::Sampler;
        case 11: return RegKind::ConstFloat2;
        case 12: return RegKind::ConstFloat3;
        case 13: return RegKind::ConstFloat4;
        case 14: return RegKind::ConstBool;
        case 15: return RegKind::Loop;
        case 16: return RegKind::TempF16;
        case 17: return RegKind::MiscType;
        case 18: return RegKind::Label;
        case 19: return RegKind::Predicate;
        default: return RegKind::Unknown;
    }
}

bool isConstFloatKind(RegKind k) {
    return k == RegKind::ConstFloat || k == RegKind::ConstFloat2 || k == RegKind::ConstFloat3 ||
           k == RegKind::ConstFloat4;
}

// Unifies Const/Const2/Const3/Const4 (spec §7: register files 0-2047/2048-
// 4095/4096-6143/6144-8191) into one flat 0-8191 index - matching, per spec
// §13.1, what CTAB's own RegisterSet==Float4 RegisterIndex already counts
// within ("which of the four physically-separate constant memories... not
// which of §7's ~20 register kinds" - i.e. CTAB's index space already IS
// this flat space).
uint32_t logicalFloatIndex(RegKind k, uint32_t num) {
    switch (k) {
        case RegKind::ConstFloat2: return 2048u + num;
        case RegKind::ConstFloat3: return 4096u + num;
        case RegKind::ConstFloat4: return 6144u + num;
        default: return num;
    }
}

char chanLetter(uint8_t c) { return "xyzw"[c & 3u]; }

std::string destMaskString(const WriteMask& m) {
    std::string s;
    if (m.x) s += 'x';
    if (m.y) s += 'y';
    if (m.z) s += 'z';
    if (m.w) s += 'w';
    return s;
}

int maskComponentCount(const WriteMask& m) { return (m.x ? 1 : 0) + (m.y ? 1 : 0) + (m.z ? 1 : 0) + (m.w ? 1 : 0); }

// spec §12's own convention: "srcN.c denotes source N's value in channel c
// after its own swizzle... applied" - selects, for each ACTIVE dest
// channel (x,y,z,w order), the source component that channel's own
// swizzle names.
std::string maskedSwizzleSuffix(const Swizzle& sw, const WriteMask& mask) {
    std::string s;
    if (mask.x) s += chanLetter(sw.x);
    if (mask.y) s += chanLetter(sw.y);
    if (mask.z) s += chanLetter(sw.z);
    if (mask.w) s += chanLetter(sw.w);
    return s;
}

std::string fullSwizzleSuffix(const Swizzle& sw) {
    std::string s;
    s += chanLetter(sw.x);
    s += chanLetter(sw.y);
    s += chanLetter(sw.z);
    s += chanLetter(sw.w);
    return s;
}

std::string sanitizeIdent(const std::string& s, const char* fallbackPrefix, uint32_t fallbackSuffix) {
    std::string out;
    for (char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')
            out += c;
        else
            out += '_';
    }
    if (out.empty() || (out[0] >= '0' && out[0] <= '9')) out = std::string(fallbackPrefix) + std::to_string(fallbackSuffix) + "_" + out;
    return out;
}

std::string usageSemanticBase(uint8_t usage, bool& knownOut) {
    knownOut = true;
    switch (usage) {
        case 0: return "POSITION";
        case 1: return "BLENDWEIGHT";
        case 2: return "BLENDINDICES";
        case 3: return "NORMAL";
        case 4: return "PSIZE";
        case 5: return "TEXCOORD";
        case 6: return "TANGENT";
        case 7: return "BINORMAL";
        case 8: return "TESSFACTOR";
        case 9: return "POSITIONT";
        case 10: return "COLOR";
        case 11: return "FOG";
        case 12: return "DEPTH";
        case 13: return "SAMPLE";
        default: knownOut = false; return "TEXCOORD";
    }
}

float bitsToFloat(uint32_t u) {
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

std::string floatLiteral(float f) {
    std::ostringstream oss;
    oss.precision(9);
    oss << f;
    std::string s = oss.str();
    // Make sure it reads as a float, not an int, to HLSL.
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos && s.find("inf") == std::string::npos &&
        s.find("nan") == std::string::npos)
        s += ".0";
    return s;
}

// One open control-flow frame (spec §12's IF/IFC/ELSE/ENDIF/REP/ENDREP
// rows: "this ISA is a nested block structure... the translator needs to
// track nesting depth", matched here with a plain stack, exactly as §12
// itself suggests).
enum class FrameKind { If, Rep };

// ---------------------------------------------------------------------------

class Translator {
public:
    Translator(const DisassembledShader& shader, const ConstantTable& ctab, vpp::ByteView blob, HlslTarget target)
        : shader_(shader), ctab_(ctab), blob_(blob), isVertex_(shader.version.isVertexShader),
          major_(shader.version.major), target_(target) {}

    TranslationResult run() {
        buildSamplerMap();
        buildConstComments();
        collect();
        emitHeader();
        emitDeclarations();
        emitBody();

        TranslationResult r;
        r.hlsl = assemble();
        r.entryPoint = "main";
        r.targetProfile = (target_ == HlslTarget::SM4_5)
                               ? (std::string(isVertex_ ? "vs_" : "ps_") + "4_0")
                               : (std::string(isVertex_ ? "vs_" : "ps_") + std::to_string(major_) + "_" +
                                  std::to_string(shader_.version.minor));
        r.warnings = warnings_;
        r.unsupported = unsupported_;
        r.complete = unsupported_.empty();
        return r;
    }

private:
    const DisassembledShader& shader_;
    const ConstantTable& ctab_;
    vpp::ByteView blob_;
    bool isVertex_;
    uint8_t major_;
    HlslTarget target_;

    std::vector<std::string> warnings_;
    std::vector<std::string> unsupported_;

    // ---- collected state ----------------------------------------------
    std::set<uint32_t> tempRegs_, tempF16Regs_, addrRegs_;
    std::set<uint32_t> outputRegsSeen_, colorOutRegsSeen_, inputRegsSeen_, miscRegsSeen_, psTexRegsSeen_;
    bool depthOutUsed_ = false;
    bool relFloatAddrUsed_ = false;
    uint32_t maxFloatIndex_ = 0; // exclusive upper bound from direct references
    uint32_t maxIntIndex_ = 0;
    uint32_t maxBoolIndex_ = 0;

    struct FieldInfo {
        uint8_t usage = 5; // TEXCOORD fallback
        uint8_t usageIndex = 0;
        bool fromDcl = false;
    };
    std::map<uint32_t, FieldInfo> inputFields_, outputFields_;

    struct DefFloat { float v[4] = {0, 0, 0, 0}; };
    struct DefInt { int32_t v[4] = {0, 0, 0, 0}; };
    std::map<uint32_t, DefFloat> definedFloats_;
    std::map<uint32_t, DefInt> definedInts_;

    struct SamplerInfo {
        std::string hlslName;
        bool isCube = false;
        bool fromCtab = false;
    };
    std::map<uint32_t, SamplerInfo> samplers_; // key: sampler register number

    std::vector<std::string> constComments_;

    // ---- emitted sections ------------------------------------------------
    std::string headerText_;
    std::string declText_;
    std::string bodyText_;
    int indent_ = 1;

    void emitLine(const std::string& s) { bodyText_ += std::string(static_cast<size_t>(indent_) * 4, ' ') + s + "\n"; }

    std::string assemble() {
        std::string out;
        out += headerText_;
        out += declText_;
        out += (isVertex_ ? "VS_OUTPUT main(VS_INPUT input) {\n" : "PS_OUTPUT main(PS_INPUT input) {\n");
        out += (isVertex_ ? "    VS_OUTPUT output = (VS_OUTPUT)0;\n" : "    PS_OUTPUT output = (PS_OUTPUT)0;\n");
        for (uint32_t n : tempRegs_) out += "    float4 r" + std::to_string(n) + " = 0;\n";
        for (uint32_t n : tempF16Regs_) out += "    float4 rh" + std::to_string(n) + " = 0;\n";
        for (uint32_t n : addrRegs_) out += "    int4 a" + std::to_string(n) + " = 0;\n";
        out += bodyText_;
        out += "    return output;\n";
        out += "}\n";
        return out;
    }

    // -----------------------------------------------------------------
    // Pre-pass helpers
    // -----------------------------------------------------------------
    void buildSamplerMap() {
        if (ctab_.status != CtabStatus::WellFormed) return;
        for (const auto& c : ctab_.constants) {
            if (c.registerSet != RegisterSet::Sampler) continue;
            SamplerInfo info;
            info.hlslName = sanitizeIdent(c.name, "s", c.registerIndex);
            const uint16_t t = c.type.typeRaw;
            if (t == static_cast<uint16_t>(ParameterType::SamplerCube)) {
                info.isCube = true;
            } else if (t == static_cast<uint16_t>(ParameterType::Sampler2D)) {
                info.isCube = false;
            } else {
                info.isCube = false;
                warnings_.push_back("sampler '" + c.name + "' at register s" + std::to_string(c.registerIndex) +
                                     " has CTAB Type=" + std::to_string(t) +
                                     " (neither confirmed Sampler2D=12 nor SamplerCube=14 - population gate's own"
                                     " histogram says only those two occur in real data); defaulted to 2D");
            }
            info.fromCtab = true;
            samplers_[c.registerIndex] = info;
        }
    }

    void buildConstComments() {
        if (ctab_.status != CtabStatus::WellFormed) return;
        for (const auto& c : ctab_.constants) {
            std::ostringstream oss;
            switch (c.registerSet) {
                case RegisterSet::Float4: {
                    uint32_t lo = c.registerIndex, hi = c.registerIndex + (c.registerCount > 0 ? c.registerCount - 1 : 0);
                    oss << "//   c" << lo;
                    if (hi != lo) oss << ".." << "c" << hi;
                    oss << " : " << c.name << "  (float4 x" << c.registerCount << ")";
                    break;
                }
                case RegisterSet::Int4:
                    oss << "//   i" << c.registerIndex << " : " << c.name << "  (int4 x" << c.registerCount << ")";
                    break;
                case RegisterSet::Bool:
                    oss << "//   b" << c.registerIndex << " : " << c.name << "  (bool x" << c.registerCount << ")";
                    break;
                case RegisterSet::Sampler:
                    oss << "//   s" << c.registerIndex << " : " << c.name
                        << (samplers_.count(c.registerIndex) && samplers_[c.registerIndex].isCube ? "  (samplerCUBE)"
                                                                                                    : "  (sampler2D)");
                    break;
            }
            constComments_.push_back(oss.str());
        }
    }

    void noteReg(RegKind k, uint32_t num) {
        switch (k) {
            case RegKind::Temp: tempRegs_.insert(num); break;
            case RegKind::TempF16: tempF16Regs_.insert(num); break;
            case RegKind::Addr: addrRegs_.insert(num); break;
            case RegKind::Output: outputRegsSeen_.insert(num); break;
            case RegKind::ColorOut: colorOutRegsSeen_.insert(num); break;
            case RegKind::DepthOut: depthOutUsed_ = true; break;
            case RegKind::Input: inputRegsSeen_.insert(num); break;
            case RegKind::MiscType: miscRegsSeen_.insert(num); break;
            case RegKind::PSTexture: psTexRegsSeen_.insert(num); break;
            case RegKind::ConstFloat: maxFloatIndex_ = std::max(maxFloatIndex_, num + 1); break;
            case RegKind::ConstFloat2: maxFloatIndex_ = std::max(maxFloatIndex_, 2048u + num + 1); break;
            case RegKind::ConstFloat3: maxFloatIndex_ = std::max(maxFloatIndex_, 4096u + num + 1); break;
            case RegKind::ConstFloat4: maxFloatIndex_ = std::max(maxFloatIndex_, 6144u + num + 1); break;
            case RegKind::ConstInt: maxIntIndex_ = std::max(maxIntIndex_, num + 1); break;
            case RegKind::ConstBool: maxBoolIndex_ = std::max(maxBoolIndex_, num + 1); break;
            default: break;
        }
    }

    void collect() {
        for (size_t ii = 0; ii < shader_.instructions.size(); ++ii) {
            const Instruction& inst = shader_.instructions[ii];
            if (inst.opcode == Opcode::DCL) {
                collectDcl(inst);
                continue;
            }
            if (inst.opcode == Opcode::DEF) {
                collectDef(inst, ii);
                continue;
            }
            if (inst.opcode == Opcode::DEFI) {
                collectDefi(inst, ii);
                continue;
            }
            if (inst.dest) noteReg(resolveKind(inst.dest->registerTypeRaw, isVertex_, major_), inst.dest->registerNumber);
            for (const auto& s : inst.sources) {
                RegKind k = resolveKind(s.registerTypeRaw, isVertex_, major_);
                noteReg(k, s.registerNumber);
                if (s.hasRelativeAddressing && isConstFloatKind(k)) relFloatAddrUsed_ = true;
            }
            if (inst.predicateSource)
                noteReg(resolveKind(inst.predicateSource->registerTypeRaw, isVertex_, major_),
                        inst.predicateSource->registerNumber);
        }
    }

    void collectDcl(const Instruction& inst) {
        if (!inst.dest || !inst.dcl) return;
        RegKind k = resolveKind(inst.dest->registerTypeRaw, isVertex_, major_);
        if (k == RegKind::Input) {
            FieldInfo fi;
            fi.usage = inst.dcl->usage;
            fi.usageIndex = inst.dcl->usageIndex;
            fi.fromDcl = true;
            if (!inputFields_.count(inst.dest->registerNumber)) inputFields_[inst.dest->registerNumber] = fi;
            inputRegsSeen_.insert(inst.dest->registerNumber);
        } else if (k == RegKind::Output) {
            FieldInfo fi;
            fi.usage = inst.dcl->usage;
            fi.usageIndex = inst.dcl->usageIndex;
            fi.fromDcl = true;
            if (!outputFields_.count(inst.dest->registerNumber)) outputFields_[inst.dest->registerNumber] = fi;
            outputRegsSeen_.insert(inst.dest->registerNumber);
        } else if (k == RegKind::Sampler) {
            // Dimensionality/name come from CTAB (buildSamplerMap()), which
            // is guaranteed present on every real blob this game ships
            // (HANDOFF §9.97/§13.4: 7,276/7,276 well-formed CTAB) - the
            // DCL sampler token's own `samplerTextureType` nibble
            // (D3DSAMPLER_TEXTURE_TYPE, spec §8) is not otherwise needed
            // and this spec document does not give that enum's numeric
            // values, so it is deliberately not decoded here.
        } else if (k == RegKind::MiscType) {
            FieldInfo fi;
            fi.usage = 5; // TEXCOORD fallback - see this file's top comment re: Face/Position identity
            fi.usageIndex = 0;
            fi.fromDcl = true;
            miscRegsSeen_.insert(inst.dest->registerNumber);
            warnings_.push_back("DCL of a MiscType register (Face/Position shape, spec §8) at register " +
                                 std::to_string(inst.dest->registerNumber) +
                                 " - spec does not state which register number means Face vs Position, mapped"
                                 " generically to a plain interpolated input, not asserted as VFACE/VPOS");
        } else if (k == RegKind::PSTexture || k == RegKind::TexCrdOut || k == RegKind::RastOut || k == RegKind::AttrOut) {
            unsupported_.push_back("DCL targets a legacy pre-3.0 fixed-function register kind not expected in this"
                                    " game's confirmed vs_3_0/ps_3_0-only population");
        }
    }

    uint32_t literalDwordAt(size_t tokenOffset, int i) const {
        // spec §12's DEF/DEFI rows: dest (1 dword) then exactly 4 literal
        // dwords, positionally, NEVER re-derived from the generic
        // source/extraDwords parse (see hlsl_translator.h's own top
        // comment for why that parse can be unreliable here).
        const size_t off = tokenOffset + 4 /*instruction token*/ + 4 /*dest token*/ + 4u * static_cast<size_t>(i);
        if (off + 4 > blob_.size()) return 0;
        return blob_.readU32LE(off);
    }

    void collectDef(const Instruction& inst, size_t ii) {
        if (!inst.dest) {
            unsupported_.push_back("DEF at instruction " + std::to_string(ii) + " has no decoded destination");
            return;
        }
        RegKind k = resolveKind(inst.dest->registerTypeRaw, isVertex_, major_);
        if (!isConstFloatKind(k)) {
            unsupported_.push_back("DEF at instruction " + std::to_string(ii) +
                                    " targets a non-float-constant register (raw=" +
                                    std::to_string(inst.dest->registerTypeRaw) + ")");
            return;
        }
        uint32_t idx = logicalFloatIndex(k, inst.dest->registerNumber);
        DefFloat d;
        for (int i = 0; i < 4; ++i) d.v[i] = bitsToFloat(literalDwordAt(inst.tokenOffset, i));
        definedFloats_[idx] = d;
    }

    void collectDefi(const Instruction& inst, size_t ii) {
        if (!inst.dest) {
            unsupported_.push_back("DEFI at instruction " + std::to_string(ii) + " has no decoded destination");
            return;
        }
        RegKind k = resolveKind(inst.dest->registerTypeRaw, isVertex_, major_);
        if (k != RegKind::ConstInt) {
            unsupported_.push_back("DEFI at instruction " + std::to_string(ii) +
                                    " targets a non-int-constant register (raw=" +
                                    std::to_string(inst.dest->registerTypeRaw) + ")");
            return;
        }
        DefInt d;
        for (int i = 0; i < 4; ++i) d.v[i] = static_cast<int32_t>(literalDwordAt(inst.tokenOffset, i));
        definedInts_[inst.dest->registerNumber] = d;
    }

    // -----------------------------------------------------------------
    // Header / declarations
    // -----------------------------------------------------------------
    void emitHeader() {
        headerText_ += "// Auto-generated by sr3d3d9bc::translateToHlsl() from spec-d3d9-sm2-sm3-bytecode.md.\n";
        headerText_ += std::string("// Source shader: ") + (isVertex_ ? "vertex" : "pixel") + " shader, version " +
                        std::to_string(major_) + "." + std::to_string(shader_.version.minor) + ", " +
                        std::to_string(shader_.instructions.size()) + " instructions.\n";
        headerText_ += std::string("// Emission target: ") +
                        (target_ == HlslTarget::SM4_5 ? "SM4_5 (cbuffer + Texture2D/TextureCube + SamplerState + SV_*)"
                                                       : "SM3Legacy (loose register(cN) + sampler2D/samplerCUBE + POSITION/COLORn)") +
                        "\n";
        if (!constComments_.empty()) {
            headerText_ += "// Named constants/samplers (spec §13 CTAB) - traceability only; runtime float/int/bool\n";
            headerText_ += "// constant reads below go through the generic c[]/i[]/b[] arrays (register-bound,\n";
            headerText_ += "// needed to support relative addressing, spec §6.3), not these names directly:\n";
            for (const auto& c : constComments_) headerText_ += c + "\n";
        }
        headerText_ += "\n";
    }

    std::string fieldSemantic(const FieldInfo& fi, uint32_t regNum) {
        bool known = true;
        std::string base = usageSemanticBase(fi.usage, known);
        if (!fi.fromDcl || !known) {
            warnings_.push_back("input/output register " + std::to_string(regNum) +
                                 " has no confirmed DCL usage - fell back to TEXCOORD" + std::to_string(regNum));
            return "TEXCOORD" + std::to_string(regNum);
        }
        // TESSFACTOR (usage 8) and POSITIONT (usage 9) are valid D3DDECLUSAGE
        // enumerators (spec §8.1) but were empirically confirmed (this
        // translator's own d3dctest_semantics.cpp probe, per the task's own
        // "confirm against a real D3DCompile" instruction) to be REJECTED
        // as vs_3_0 input semantics by d3dcompiler_47.dll's D3DCompile()
        // itself ("error X4502: invalid vs_3_0 input semantic") - not a
        // spelling/casing mismatch, a genuinely unsupported semantic for
        // this shader model. Neither is expected in this game's real
        // corpus (both are legacy fixed-function-pipeline concepts), but
        // fall back to TEXCOORD rather than emit HLSL already proven not
        // to compile.
        if (fi.usage == 8 || fi.usage == 9) {
            warnings_.push_back("input/output register " + std::to_string(regNum) +
                                 " has DCL usage " + (fi.usage == 8 ? "TESSFACTOR" : "POSITIONT") +
                                 ", empirically confirmed invalid as a vs_3_0 semantic (D3DCompile error X4502) -"
                                 " fell back to TEXCOORD" + std::to_string(regNum));
            return "TEXCOORD" + std::to_string(regNum);
        }
        return base + std::to_string(fi.usageIndex);
    }

    void emitStructs() {
        // Input struct (VS_INPUT / PS_INPUT): every Input-kind register
        // actually referenced, DCL usage where known.
        std::set<uint32_t> allInputRegs = inputRegsSeen_;
        for (auto& kv : inputFields_) allInputRegs.insert(kv.first);
        declText_ += std::string("struct ") + (isVertex_ ? "VS_INPUT" : "PS_INPUT") + " {\n";
        if (allInputRegs.empty()) {
            declText_ += "    float4 _unused0 : TEXCOORD0;\n"; // HLSL structs may not be empty
        }
        for (uint32_t n : allInputRegs) {
            FieldInfo fi = inputFields_.count(n) ? inputFields_[n] : FieldInfo{};
            declText_ += "    float4 v" + std::to_string(n) + " : " + fieldSemantic(fi, n) + ";\n";
        }
        for (uint32_t n : miscRegsSeen_) declText_ += "    float4 misc" + std::to_string(n) + " : TEXCOORD" + std::to_string(20 + n) + ";\n";
        for (uint32_t n : psTexRegsSeen_) declText_ += "    float4 t" + std::to_string(n) + " : TEXCOORD" + std::to_string(30 + n) + ";\n";
        declText_ += "};\n\n";

        // Output struct.
        declText_ += std::string("struct ") + (isVertex_ ? "VS_OUTPUT" : "PS_OUTPUT") + " {\n";
        bool anyOutput = false;
        if (isVertex_) {
            std::set<uint32_t> allOutRegs = outputRegsSeen_;
            for (auto& kv : outputFields_) allOutRegs.insert(kv.first);
            for (uint32_t n : allOutRegs) {
                FieldInfo fi = outputFields_.count(n) ? outputFields_[n] : FieldInfo{};
                // SM4/5: the rasterizer-fed position output MUST be the
                // system-value semantic SV_Position (no numeric index) -
                // empirically confirmed via this follow-up's own
                // d3dctest_semantics_sm4.cpp probe (case 3). Every other
                // VS output keeps its ordinary DCL-usage-derived name
                // unchanged (probe case 4: non-SV varyings alongside
                // SV_Position compile fine at vs_4_0) - same fieldSemantic()
                // used for both targets.
                std::string sem = (target_ == HlslTarget::SM4_5 && fi.fromDcl && fi.usage == 0) ? "SV_Position"
                                                                                                  : fieldSemantic(fi, n);
                declText_ += "    float4 o" + std::to_string(n) + " : " + sem + ";\n";
                anyOutput = true;
            }
        } else {
            // Empirically confirmed (this translator's own
            // d3dctest_semantics.cpp probe): D3DCompile requires COLOR
            // outputs to be CONTIGUOUS from COLOR0 ("error X4538: COLOR
            // outputs must be contiguous from COLOR0 to COLORn") and
            // requires a COLOR0 member to exist at all ("error X4530:
            // pixel shader must minimally write all four components of
            // COLOR0") - so a real shader that only ever writes e.g. oC1
            // (not expected in this game's real corpus, which per
            // HANDOFF's own findings is single-render-target almost
            // everywhere, but not yet proven never to happen) would fail
            // to compile without this. Fill any gap 0..max with an
            // (unwritten, zero-initialized via `(PS_OUTPUT)0`) field
            // rather than emit a signature already proven to reject.
            if (!colorOutRegsSeen_.empty()) {
                uint32_t maxColor = *colorOutRegsSeen_.rbegin();
                for (uint32_t n = 0; n <= maxColor; ++n) {
                    // SM4/5: SV_TargetN (legacy COLORn is REJECTED outright
                    // at ps_4_0 - empirically confirmed, d3dctest_semantics_
                    // sm4.cpp probe case 5, error X4502 "invalid ps_4_0
                    // output semantic 'COLOR0'"). The gap-fill below (0..
                    // maxColor, even for an index this shader never writes)
                    // is KEPT for both targets even though the same probe's
                    // case 7 found SM4/5's SV_TargetN does NOT actually
                    // enforce COLORn's contiguity-from-0 requirement (no
                    // X4538 counterpart) - harmless either way, and sharing
                    // one fill rule for both targets is simpler than
                    // forking it over a rule that turned out not to matter
                    // (0 real multi-render-target shaders in this
                    // population per the SM3 pass's own finding).
                    std::string sem = target_ == HlslTarget::SM4_5 ? ("SV_Target" + std::to_string(n))
                                                                    : ("COLOR" + std::to_string(n));
                    declText_ += "    float4 oC" + std::to_string(n) + " : " + sem + ";\n";
                    if (!colorOutRegsSeen_.count(n))
                        warnings_.push_back("PS writes COLOR" + std::to_string(maxColor) +
                                             " but never COLOR" + std::to_string(n) +
                                             " - added an unwritten (zero) " + sem +
                                             " field (SM3: D3DCompile's contiguity requirement, error X4538;"
                                             " SM4/5: kept for consistency though not strictly required there)");
                }
                anyOutput = true;
            }
            if (depthOutUsed_) {
                declText_ += target_ == HlslTarget::SM4_5 ? "    float oDepth : SV_Depth;\n" : "    float oDepth : DEPTH0;\n";
                anyOutput = true;
            }
        }
        if (!anyOutput) {
            // No real shader in this game's population is expected to hit
            // this (every VS writes a position output, every PS writes at
            // least oC0) - kept only so a degenerate/edge-case blob still
            // produces syntactically valid HLSL rather than an empty struct.
            if (isVertex_)
                declText_ += target_ == HlslTarget::SM4_5 ? "    float4 o0 : SV_Position;\n" : "    float4 o0 : POSITION0;\n";
            else
                declText_ += target_ == HlslTarget::SM4_5 ? "    float4 oC0 : SV_Target0;\n" : "    float4 oC0 : COLOR0;\n";
            warnings_.push_back("shader writes no recognised output register - emitted a placeholder output field");
        }
        declText_ += "};\n\n";
    }

    void emitDeclarations() {
        emitStructs();

        for (const auto& kv : definedFloats_) {
            const DefFloat& d = kv.second;
            declText_ += "static const float4 c" + std::to_string(kv.first) + "_def = float4(" + floatLiteral(d.v[0]) +
                          ", " + floatLiteral(d.v[1]) + ", " + floatLiteral(d.v[2]) + ", " + floatLiteral(d.v[3]) + ");\n";
        }
        for (const auto& kv : definedInts_) {
            const DefInt& d = kv.second;
            declText_ += "static const int4 i" + std::to_string(kv.first) + "_def = int4(" + std::to_string(d.v[0]) +
                          ", " + std::to_string(d.v[1]) + ", " + std::to_string(d.v[2]) + ", " + std::to_string(d.v[3]) +
                          ");\n";
        }
        if (!definedFloats_.empty() || !definedInts_.empty()) declText_ += "\n";

        // Constant register file arrays. Sized generously (full hardware
        // register-file size) whenever relative addressing is observed,
        // since the true runtime upper bound is not known statically -
        // spec §6.3 confirms relative addressing exists but not any
        // specific shader's own intended range.
        uint32_t floatArrayLen = maxFloatIndex_;
        if (relFloatAddrUsed_) floatArrayLen = std::max(floatArrayLen, static_cast<uint32_t>(isVertex_ ? 256 : 224));
        const bool anyConstArray = floatArrayLen > 0 || maxIntIndex_ > 0 || maxBoolIndex_ > 0;
        if (target_ == HlslTarget::SM4_5) {
            // SM4/5 requires runtime constants to live in a cbuffer, not
            // loose `register(cN)` globals (this task's own point 1;
            // confirmed compiling, including WITH relative addressing on
            // the float4 array, via this follow-up's own
            // d3dctest_semantics_sm4.cpp probe case 10). One cbuffer holds
            // all three arrays - they are different MEMBERS of the same
            // buffer here, not separately register-bound the way SM3's c/
            // i/b register FILES were, so there is no per-array register
            // collision to worry about; the buffer itself takes the single
            // constant-buffer register slot b0. Indexing/relative-
            // addressing expressions elsewhere in this file (sourceBase(),
            // etc.) are completely unchanged - they read c[idx]/i[idx]/
            // b[idx] exactly as before, just now resolving to cbuffer
            // members instead of loose globals.
            if (anyConstArray) {
                declText_ += "cbuffer Constants : register(b0) {\n";
                if (floatArrayLen > 0) declText_ += "    float4 c[" + std::to_string(floatArrayLen) + "];\n";
                if (maxIntIndex_ > 0) declText_ += "    int4 i[" + std::to_string(maxIntIndex_) + "];\n";
                if (maxBoolIndex_ > 0) declText_ += "    bool b[" + std::to_string(maxBoolIndex_) + "];\n";
                declText_ += "};\n\n";
            }
        } else {
            if (floatArrayLen > 0) declText_ += "float4 c[" + std::to_string(floatArrayLen) + "] : register(c0);\n";
            if (maxIntIndex_ > 0) declText_ += "int4 i[" + std::to_string(maxIntIndex_) + "] : register(i0);\n";
            if (maxBoolIndex_ > 0) declText_ += "bool b[" + std::to_string(maxBoolIndex_) + "] : register(b0);\n";
            if (anyConstArray) declText_ += "\n";
        }

        for (const auto& kv : samplers_) {
            if (target_ == HlslTarget::SM4_5) {
                // Task's own point 2, syntax confirmed via probe cases 11/
                // 13: a Texture2D<float4>/TextureCube<float4> object plus a
                // SEPARATE SamplerState object, sampler-state variable
                // named `<Name>Sampler` (a plain suffix - real CTAB names
                // in this game's population were not found to already end
                // in "Sampler", so no collision has been observed; a real
                // collision would surface as a genuine D3DCompile
                // redefinition error in the population gate, not be
                // silently mishandled).
                declText_ += std::string(kv.second.isCube ? "TextureCube<float4> " : "Texture2D<float4> ") +
                              kv.second.hlslName + " : register(t" + std::to_string(kv.first) + ");\n";
                declText_ += "SamplerState " + kv.second.hlslName + "Sampler : register(s" + std::to_string(kv.first) +
                              ");\n";
            } else {
                declText_ += std::string(kv.second.isCube ? "samplerCUBE " : "sampler2D ") + kv.second.hlslName +
                              " : register(s" + std::to_string(kv.first) + ");\n";
            }
        }
        if (!samplers_.empty()) declText_ += "\n";
    }

    // -----------------------------------------------------------------
    // Register -> HLSL expression resolution used during body emission
    // -----------------------------------------------------------------
    std::string sourceBase(const SourceParam& s, size_t ii) {
        RegKind k = resolveKind(s.registerTypeRaw, isVertex_, major_);
        switch (k) {
            case RegKind::Temp: return "r" + std::to_string(s.registerNumber);
            case RegKind::TempF16: return "rh" + std::to_string(s.registerNumber);
            case RegKind::Input: return "input.v" + std::to_string(s.registerNumber);
            case RegKind::Addr: return "a" + std::to_string(s.registerNumber);
            case RegKind::Output: return "output.o" + std::to_string(s.registerNumber);
            case RegKind::ColorOut: return "output.oC" + std::to_string(s.registerNumber);
            case RegKind::DepthOut: return "output.oDepth";
            case RegKind::ConstFloat:
            case RegKind::ConstFloat2:
            case RegKind::ConstFloat3:
            case RegKind::ConstFloat4: {
                uint32_t idx = logicalFloatIndex(k, s.registerNumber);
                if (s.hasRelativeAddressing && s.relativeAddressing) {
                    std::string addrBase = "a" + std::to_string(s.relativeAddressing->registerNumber);
                    std::string addrComp(1, chanLetter(s.relativeAddressing->swizzle.x));
                    return "c[" + addrBase + "." + addrComp + " + " + std::to_string(idx) + "]";
                }
                return definedFloats_.count(idx) ? ("c" + std::to_string(idx) + "_def") : ("c[" + std::to_string(idx) + "]");
            }
            case RegKind::ConstInt: {
                uint32_t idx = s.registerNumber;
                return definedInts_.count(idx) ? ("i" + std::to_string(idx) + "_def") : ("i[" + std::to_string(idx) + "]");
            }
            case RegKind::ConstBool: return "b[" + std::to_string(s.registerNumber) + "]";
            case RegKind::MiscType: return "input.misc" + std::to_string(s.registerNumber);
            case RegKind::PSTexture: return "input.t" + std::to_string(s.registerNumber);
            default:
                unsupported_.push_back("unsupported source register kind (raw=" + std::to_string(s.registerTypeRaw) +
                                        ") at instruction " + std::to_string(ii));
                return "0.0";
        }
    }

    std::string destBase(const DestinationParam& d, size_t ii, RegKind* kindOut = nullptr) {
        RegKind k = resolveKind(d.registerTypeRaw, isVertex_, major_);
        if (kindOut) *kindOut = k;
        switch (k) {
            case RegKind::Temp: return "r" + std::to_string(d.registerNumber);
            case RegKind::TempF16: return "rh" + std::to_string(d.registerNumber);
            case RegKind::Addr: return "a" + std::to_string(d.registerNumber);
            case RegKind::Output: return "output.o" + std::to_string(d.registerNumber);
            case RegKind::ColorOut: return "output.oC" + std::to_string(d.registerNumber);
            case RegKind::DepthOut: return "output.oDepth";
            default:
                unsupported_.push_back("unsupported destination register kind (raw=" + std::to_string(d.registerTypeRaw) +
                                        ") at instruction " + std::to_string(ii));
                return "r0 /*unsupported dest*/";
        }
    }

    // spec §6.4 - see this file's top comment for exactly which values are
    // implemented vs. flagged unsupported.
    std::string applyModifier(SourceModifier m, const std::string& expr, size_t ii) {
        switch (m) {
            case SourceModifier::None: return expr;
            case SourceModifier::Negate: return "(-(" + expr + "))";
            case SourceModifier::Abs: return "abs(" + expr + ")";
            case SourceModifier::NegateAbs: return "(-abs(" + expr + "))";
            default:
                unsupported_.push_back("source modifier 0x" +
                                        [&] { std::ostringstream o; o << std::hex << static_cast<int>(m); return o.str(); }() +
                                        " (Bias/Sign/Complement/PS1.4-only/predicate-only/reserved - no formula given by"
                                        " either spec-d3d9-sm2-sm3-bytecode.md or Microsoft's own source-parameter-token"
                                        " page) at instruction " + std::to_string(ii));
                return expr;
        }
    }

    std::string maskedSourceExpr(const SourceParam& s, const WriteMask& mask, size_t ii) {
        std::string base = sourceBase(s, ii);
        std::string swz = maskedSwizzleSuffix(s.swizzle, mask);
        std::string full = swz.empty() ? base : (base + "." + swz);
        return applyModifier(s.modifier, full, ii);
    }

    std::string fullSourceExpr(const SourceParam& s, size_t ii) {
        std::string base = sourceBase(s, ii);
        std::string full = base + "." + fullSwizzleSuffix(s.swizzle);
        return applyModifier(s.modifier, full, ii);
    }

    // IF/IFC/REP have NO real destination (spec §12: IF/REP take only
    // source parameter token(s); IFC takes two). Stage 1's own disassembler
    // is a purely POSITIONAL decoder that does not know any individual
    // opcode's true operand shape (disassembler.cpp's own top comment,
    // spec §6/§11) - it always files the FIRST valid parameter token of any
    // instruction into `Instruction::dest`, regardless of whether that
    // opcode actually has a destination. For these three opcodes that means
    // their real (first) source operand is misfiled into `inst.dest`,
    // decoded using the DESTINATION token's bit layout (write mask, result
    // modifier) rather than the correct SOURCE layout (swizzle, source
    // modifier) - so it cannot be read via `inst.dest` directly. This
    // re-decodes that same raw DWORD using the source-token bit formula
    // (spec §6.2, identical to disassembler.cpp's own decodeSourceToken)
    // to recover the operand this translator actually needs.
    SourceParam reinterpretDestAsSource(const DestinationParam& d) const {
        SourceParam s;
        s.raw = d.raw;
        s.registerTypeRaw = decodeRegisterTypeBits(d.raw);
        s.registerNumber = static_cast<uint16_t>(d.raw & 0x7FFu);
        s.swizzle.x = static_cast<uint8_t>((d.raw >> 16) & 0x3u);
        s.swizzle.y = static_cast<uint8_t>((d.raw >> 18) & 0x3u);
        s.swizzle.z = static_cast<uint8_t>((d.raw >> 20) & 0x3u);
        s.swizzle.w = static_cast<uint8_t>((d.raw >> 22) & 0x3u);
        s.modifier = static_cast<SourceModifier>((d.raw >> 24) & 0xFu);
        s.hasRelativeAddressing = ((d.raw >> 13) & 0x1u) != 0;
        return s;
    }

    // Collects an instruction's true source operand list for IF/IFC/REP:
    // the misfiled `dest` (if present) reinterpreted as a source, in front
    // of whatever `sources` already correctly holds (see
    // reinterpretDestAsSource()'s own comment for why `dest` comes first).
    std::vector<SourceParam> trueSourcesForDestlessOpcode(const Instruction& inst) const {
        std::vector<SourceParam> ops;
        if (inst.dest) ops.push_back(reinterpretDestAsSource(*inst.dest));
        for (const auto& s : inst.sources) ops.push_back(s);
        return ops;
    }

    std::string selectedScalarExpr(const SourceParam& s, size_t ii) {
        std::string base = sourceBase(s, ii);
        std::string full = base + "." + std::string(1, chanLetter(s.swizzle.x));
        return applyModifier(s.modifier, full, ii);
    }

    // Emits `<dest-lvalue> = <rhs>;`, applying Saturate (spec §6.1) and the
    // Addr-register round-to-int conversion (spec §12's MOV row) uniformly.
    void emitAssign(const DestinationParam& d, const std::string& rhsIn, size_t ii) {
        RegKind kind;
        std::string base = destBase(d, ii, &kind);
        std::string rhs = rhsIn;

        // DepthOut's HLSL field is a scalar `float` (depth is inherently
        // one component), but real compiler-emitted bytecode was found (via
        // this translator's own population gate, D3DCompile error X3018
        // "invalid subscript 'xyzw'" on a real oDepth write) to still
        // encode a full xyzw destination write mask on that instruction
        // even though only one component is meaningful. Clamp to the
        // field's actual declared width rather than trust the raw mask's
        // component COUNT for register kinds narrower than float4/int4.
        const int declaredComp = (kind == RegKind::DepthOut) ? 1 : 4;
        const int maskComp = maskComponentCount(d.writeMask);
        std::string lhs;
        if (declaredComp < maskComp) {
            lhs = base; // scalar field is its own single-component lvalue, no subscript
            static const char* letters = "xyzw";
            std::string slice(letters, static_cast<size_t>(declaredComp));
            rhs = "(" + rhs + ")." + slice;
        } else {
            lhs = destMaskString(d.writeMask).empty() ? base : (base + "." + destMaskString(d.writeMask));
        }

        if (kind == RegKind::Addr) {
            int n = maskComponentCount(d.writeMask);
            static const char* castName[5] = {"int", "int", "int2", "int3", "int4"};
            rhs = "(" + std::string(castName[n <= 4 ? n : 4]) + ")round(" + rhs + ")";
        }
        if (d.resultModifier.saturate()) rhs = "saturate(" + rhs + ")";
        emitLine(lhs + " = " + rhs + ";");
    }

    // -----------------------------------------------------------------
    // Body
    // -----------------------------------------------------------------
    void emitBody() {
        std::vector<FrameKind> stack;
        for (size_t ii = 0; ii < shader_.instructions.size(); ++ii) {
            const Instruction& inst = shader_.instructions[ii];
            if (inst.opcode == Opcode::DCL) continue; // consumed in collect()/declarations
            if (inst.predicate) {
                unsupported_.push_back("instruction " + std::to_string(ii) +
                                        " (" + std::string(opcodeName(inst.opcodeRaw) ? opcodeName(inst.opcodeRaw) : "?") +
                                        ") is predicated - this game's real corpus does not use SETP (HANDOFF's own"
                                        " opcode census), so predicate-register semantics were never implemented; the"
                                        " instruction's arithmetic is still emitted, unconditionally");
            }
            emitLine("// " + std::string(opcodeName(inst.opcodeRaw) ? opcodeName(inst.opcodeRaw) : "?"));
            translateOne(inst, ii, stack);
        }
        while (!stack.empty()) {
            unsupported_.push_back("unclosed control-flow block at end of instruction stream (malformed nesting)");
            --indent_;
            emitLine("}");
            stack.pop_back();
        }
    }

    void translateOne(const Instruction& inst, size_t ii, std::vector<FrameKind>& stack) {
        switch (inst.opcode) {
            case Opcode::MOV: {
                if (!inst.dest || inst.sources.size() < 1) return unsupportedShape(inst, ii);
                emitAssign(*inst.dest, maskedSourceExpr(inst.sources[0], inst.dest->writeMask, ii), ii);
                return;
            }
            case Opcode::ADD: {
                if (!inst.dest || inst.sources.size() < 2) return unsupportedShape(inst, ii);
                std::string a = maskedSourceExpr(inst.sources[0], inst.dest->writeMask, ii);
                std::string b = maskedSourceExpr(inst.sources[1], inst.dest->writeMask, ii);
                emitAssign(*inst.dest, "(" + a + " + " + b + ")", ii);
                return;
            }
            case Opcode::SUB: {
                // Not in the real 35-opcode population (spec's own real-
                // shader "sub is emitted as ADD with negate" note,
                // instruction-token.md) - implemented anyway since it is
                // trivial and unambiguous.
                if (!inst.dest || inst.sources.size() < 2) return unsupportedShape(inst, ii);
                std::string a = maskedSourceExpr(inst.sources[0], inst.dest->writeMask, ii);
                std::string b = maskedSourceExpr(inst.sources[1], inst.dest->writeMask, ii);
                emitAssign(*inst.dest, "(" + a + " - " + b + ")", ii);
                return;
            }
            case Opcode::MAD: {
                if (!inst.dest || inst.sources.size() < 3) return unsupportedShape(inst, ii);
                std::string a = maskedSourceExpr(inst.sources[0], inst.dest->writeMask, ii);
                std::string b = maskedSourceExpr(inst.sources[1], inst.dest->writeMask, ii);
                std::string c = maskedSourceExpr(inst.sources[2], inst.dest->writeMask, ii);
                emitAssign(*inst.dest, "mad(" + a + ", " + b + ", " + c + ")", ii);
                return;
            }
            case Opcode::MUL: {
                if (!inst.dest || inst.sources.size() < 2) return unsupportedShape(inst, ii);
                std::string a = maskedSourceExpr(inst.sources[0], inst.dest->writeMask, ii);
                std::string b = maskedSourceExpr(inst.sources[1], inst.dest->writeMask, ii);
                emitAssign(*inst.dest, "(" + a + " * " + b + ")", ii);
                return;
            }
            case Opcode::RCP: {
                if (!inst.dest || inst.sources.size() < 1) return unsupportedShape(inst, ii);
                std::string x = selectedScalarExpr(inst.sources[0], ii);
                emitAssign(*inst.dest, "rcp(" + x + ")", ii);
                return;
            }
            case Opcode::RSQ: {
                // See this file's top comment: literal rsqrt(x), no abs()
                // (spec §12's own "only if empirically needed" framing).
                if (!inst.dest || inst.sources.size() < 1) return unsupportedShape(inst, ii);
                std::string x = selectedScalarExpr(inst.sources[0], ii);
                emitAssign(*inst.dest, "rsqrt(" + x + ")", ii);
                return;
            }
            case Opcode::DP3: {
                if (!inst.dest || inst.sources.size() < 2) return unsupportedShape(inst, ii);
                std::string a = fullSourceExpr(inst.sources[0], ii) + ".xyz";
                std::string b = fullSourceExpr(inst.sources[1], ii) + ".xyz";
                emitAssign(*inst.dest, "dot(" + a + ", " + b + ")", ii);
                return;
            }
            case Opcode::DP4: {
                if (!inst.dest || inst.sources.size() < 2) return unsupportedShape(inst, ii);
                std::string a = fullSourceExpr(inst.sources[0], ii);
                std::string b = fullSourceExpr(inst.sources[1], ii);
                emitAssign(*inst.dest, "dot(" + a + ", " + b + ")", ii);
                return;
            }
            case Opcode::MIN: {
                if (!inst.dest || inst.sources.size() < 2) return unsupportedShape(inst, ii);
                std::string a = maskedSourceExpr(inst.sources[0], inst.dest->writeMask, ii);
                std::string b = maskedSourceExpr(inst.sources[1], inst.dest->writeMask, ii);
                emitAssign(*inst.dest, "min(" + a + ", " + b + ")", ii);
                return;
            }
            case Opcode::MAX: {
                if (!inst.dest || inst.sources.size() < 2) return unsupportedShape(inst, ii);
                std::string a = maskedSourceExpr(inst.sources[0], inst.dest->writeMask, ii);
                std::string b = maskedSourceExpr(inst.sources[1], inst.dest->writeMask, ii);
                emitAssign(*inst.dest, "max(" + a + ", " + b + ")", ii);
                return;
            }
            case Opcode::SLT: {
                if (!inst.dest || inst.sources.size() < 2) return unsupportedShape(inst, ii);
                std::string a = maskedSourceExpr(inst.sources[0], inst.dest->writeMask, ii);
                std::string b = maskedSourceExpr(inst.sources[1], inst.dest->writeMask, ii);
                int n = maskComponentCount(inst.dest->writeMask);
                std::string ty = n <= 1 ? "float" : ("float" + std::to_string(n));
                emitAssign(*inst.dest, "((" + a + " < " + b + ") ? (" + ty + ")1.0 : (" + ty + ")0.0)", ii);
                return;
            }
            case Opcode::SGE: {
                if (!inst.dest || inst.sources.size() < 2) return unsupportedShape(inst, ii);
                std::string a = maskedSourceExpr(inst.sources[0], inst.dest->writeMask, ii);
                std::string b = maskedSourceExpr(inst.sources[1], inst.dest->writeMask, ii);
                int n = maskComponentCount(inst.dest->writeMask);
                std::string ty = n <= 1 ? "float" : ("float" + std::to_string(n));
                emitAssign(*inst.dest, "((" + a + " >= " + b + ") ? (" + ty + ")1.0 : (" + ty + ")0.0)", ii);
                return;
            }
            case Opcode::EXP: {
                if (!inst.dest || inst.sources.size() < 1) return unsupportedShape(inst, ii);
                std::string x = selectedScalarExpr(inst.sources[0], ii);
                emitAssign(*inst.dest, "exp2(" + x + ")", ii);
                return;
            }
            case Opcode::LOG: {
                if (!inst.dest || inst.sources.size() < 1) return unsupportedShape(inst, ii);
                std::string x = selectedScalarExpr(inst.sources[0], ii);
                emitAssign(*inst.dest, "log2(" + x + ")", ii);
                return;
            }
            case Opcode::LRP: {
                if (!inst.dest || inst.sources.size() < 3) return unsupportedShape(inst, ii);
                std::string s0 = maskedSourceExpr(inst.sources[0], inst.dest->writeMask, ii);
                std::string s1 = maskedSourceExpr(inst.sources[1], inst.dest->writeMask, ii);
                std::string s2 = maskedSourceExpr(inst.sources[2], inst.dest->writeMask, ii);
                // spec §12: dest = src0*src1 + (1-src0)*src2 == HLSL lerp(src2, src1, src0).
                emitAssign(*inst.dest, "lerp(" + s2 + ", " + s1 + ", " + s0 + ")", ii);
                return;
            }
            case Opcode::FRC: {
                if (!inst.dest || inst.sources.size() < 1) return unsupportedShape(inst, ii);
                std::string x = maskedSourceExpr(inst.sources[0], inst.dest->writeMask, ii);
                emitAssign(*inst.dest, "frac(" + x + ")", ii);
                return;
            }
            case Opcode::POW: {
                if (!inst.dest || inst.sources.size() < 2) return unsupportedShape(inst, ii);
                std::string a = selectedScalarExpr(inst.sources[0], ii);
                std::string b = selectedScalarExpr(inst.sources[1], ii);
                emitAssign(*inst.dest, "pow(" + a + ", " + b + ")", ii);
                return;
            }
            case Opcode::ABS: {
                if (!inst.dest || inst.sources.size() < 1) return unsupportedShape(inst, ii);
                std::string x = maskedSourceExpr(inst.sources[0], inst.dest->writeMask, ii);
                emitAssign(*inst.dest, "abs(" + x + ")", ii);
                return;
            }
            case Opcode::NRM: {
                if (!inst.dest || inst.sources.size() < 1) return unsupportedShape(inst, ii);
                std::string full = fullSourceExpr(inst.sources[0], ii);
                std::string swz = destMaskString(inst.dest->writeMask);
                emitAssign(*inst.dest, "normalize(" + full + ")" + (swz.empty() ? "" : ("." + swz)), ii);
                return;
            }
            case Opcode::CMP: {
                if (!inst.dest || inst.sources.size() < 3) return unsupportedShape(inst, ii);
                std::string a = maskedSourceExpr(inst.sources[0], inst.dest->writeMask, ii);
                std::string b = maskedSourceExpr(inst.sources[1], inst.dest->writeMask, ii);
                std::string c = maskedSourceExpr(inst.sources[2], inst.dest->writeMask, ii);
                emitAssign(*inst.dest, "((" + a + " >= 0.0) ? (" + b + ") : (" + c + "))", ii);
                return;
            }
            case Opcode::DP2ADD: {
                if (!inst.dest || inst.sources.size() < 3) return unsupportedShape(inst, ii);
                std::string a = fullSourceExpr(inst.sources[0], ii) + ".xy";
                std::string b = fullSourceExpr(inst.sources[1], ii) + ".xy";
                std::string c = selectedScalarExpr(inst.sources[2], ii);
                emitAssign(*inst.dest, "(dot(" + a + ", " + b + ") + " + c + ")", ii);
                return;
            }
            case Opcode::SINCOS: {
                if (!inst.dest || inst.sources.size() < 1) return unsupportedShape(inst, ii);
                std::string x = selectedScalarExpr(inst.sources[0], ii);
                std::string sName = "_sc_s" + std::to_string(ii);
                std::string cName = "_sc_c" + std::to_string(ii);
                emitLine("float " + sName + ", " + cName + "; sincos(" + x + ", " + sName + ", " + cName + ");");
                RegKind kind;
                std::string base = destBase(*inst.dest, ii, &kind);
                if (inst.dest->writeMask.x) emitLine(base + ".x = " + cName + ";");
                if (inst.dest->writeMask.y) emitLine(base + ".y = " + sName + ";");
                if (inst.dest->writeMask.z || inst.dest->writeMask.w)
                    warnings_.push_back("SINCOS at instruction " + std::to_string(ii) +
                                         " writes a Z/W channel - spec §12 says only X/Y may appear; ignored");
                return;
            }
            case Opcode::MOVA: {
                if (!inst.dest || inst.sources.size() < 1) return unsupportedShape(inst, ii);
                emitAssign(*inst.dest, maskedSourceExpr(inst.sources[0], inst.dest->writeMask, ii), ii);
                return;
            }
            case Opcode::DEF:
            case Opcode::DEFI:
                return; // consumed entirely in collect(): emitted as static const, nothing in the body
            case Opcode::TEXKILL: {
                if (!inst.dest) return unsupportedShape(inst, ii);
                RegKind kind;
                std::string base = destBase(*inst.dest, ii, &kind);
                emitLine("clip(" + base + ".xyz);");
                return;
            }
            case Opcode::TEX:
            case Opcode::TEXLDL: {
                if (!inst.dest || inst.sources.size() < 2) return unsupportedShape(inst, ii);
                RegKind sampKind = resolveKind(inst.sources[1].registerTypeRaw, isVertex_, major_);
                if (sampKind != RegKind::Sampler) return unsupportedShape(inst, ii);
                uint32_t sampNum = inst.sources[1].registerNumber;
                std::string sampName;
                bool isCube = false;
                if (samplers_.count(sampNum)) {
                    sampName = samplers_[sampNum].hlslName;
                    isCube = samplers_[sampNum].isCube;
                } else {
                    sampName = "sampler_fallback_s" + std::to_string(sampNum);
                    warnings_.push_back("TEX/TEXLDL at instruction " + std::to_string(ii) + " references sampler s" +
                                         std::to_string(sampNum) + " with no matching CTAB entry - defaulted to 2D,"
                                         " sampler not declared (this shader will not compile without a real CTAB"
                                         " match; not expected on real data, population is 7,276/7,276 CTAB-complete)");
                }
                std::string coord = fullSourceExpr(inst.sources[0], ii);
                // vs_3_0 (SM3Legacy) and vs_4_0/vs_5_0 (SM4_5) both require
                // an EXPLICIT-lod sampling form in a vertex shader (no
                // implicit derivatives available there) - for SM3Legacy
                // that is `tex2Dlod`/`texCUBElod`; for SM4_5 confirmed via
                // this follow-up's own probe (case 12: Texture2D.Sample at
                // vs_4_0 fails with X4532 "cannot map expression to vs_4_0
                // instruction set"; case 12b: Texture2D.SampleLevel at
                // vs_4_0 succeeds) - same useLod condition drives both.
                bool useLod = (inst.opcode == Opcode::TEXLDL) || isVertex_;
                std::string call;
                if (target_ == HlslTarget::SM4_5) {
                    // Task's own point 2: `Name.Sample(NameSampler, uv)` /
                    // `Name.SampleLevel(NameSampler, uv.xy, uv.w)` (probe
                    // cases 11/12b/13 confirmed both forms and both object
                    // types). `coord` is a pure side-effect-free swizzle
                    // expression over a register, but it is still hoisted
                    // into a named temporary rather than duplicated inline
                    // (SampleLevel needs the xy/xyz part and the w/lod part
                    // as two separate arguments) - cheaper to read and
                    // avoids ever depending on that no-side-effects
                    // property holding for some future source shape.
                    std::string sampState = sampName + "Sampler";
                    if (useLod) {
                        std::string tc = "_texc" + std::to_string(ii);
                        emitLine("float4 " + tc + " = " + coord + ";");
                        call = sampName + ".SampleLevel(" + sampState + ", " + tc + (isCube ? ".xyz" : ".xy") + ", " +
                               tc + ".w)";
                    } else {
                        call = sampName + ".Sample(" + sampState + ", " + coord + (isCube ? ".xyz" : ".xy") + ")";
                    }
                } else if (useLod) {
                    call = (isCube ? "texCUBElod(" : "tex2Dlod(") + sampName + ", " + coord + ")";
                } else {
                    call = (isCube ? "texCUBE(" : "tex2D(") + sampName + ", " + coord + (isCube ? ".xyz)" : ".xy)");
                }
                std::string swz = destMaskString(inst.dest->writeMask);
                emitAssign(*inst.dest, call + (swz.empty() ? "" : ("." + swz)), ii);
                return;
            }
            case Opcode::IF: {
                std::vector<SourceParam> ops = trueSourcesForDestlessOpcode(inst);
                if (ops.empty()) return unsupportedShape(inst, ii);
                std::string cond = sourceBase(ops[0], ii);
                emitLine("if (" + cond + ") {");
                ++indent_;
                stack.push_back(FrameKind::If);
                return;
            }
            case Opcode::IFC: {
                std::vector<SourceParam> ops = trueSourcesForDestlessOpcode(inst);
                if (ops.size() < 2) return unsupportedShape(inst, ii);
                std::string a = selectedScalarExpr(ops[0], ii);
                std::string b = selectedScalarExpr(ops[1], ii);
                const uint8_t cmp = inst.controlBits & 0x7u; // see this file's top comment
                const char* op = nullptr;
                switch (cmp) {
                    case 1: op = ">"; break;
                    case 2: op = "=="; break;
                    case 3: op = ">="; break;
                    case 4: op = "<"; break;
                    case 5: op = "!="; break;
                    case 6: op = "<="; break;
                    default:
                        unsupported_.push_back("IFC at instruction " + std::to_string(ii) +
                                                " has reserved comparison bits (" + std::to_string(cmp) + ")");
                        op = "!=";
                        break;
                }
                emitLine("if (" + a + " " + std::string(op) + " " + b + ") {");
                ++indent_;
                stack.push_back(FrameKind::If);
                return;
            }
            case Opcode::ELSE: {
                if (stack.empty() || stack.back() != FrameKind::If) {
                    unsupported_.push_back("ELSE at instruction " + std::to_string(ii) + " with no open IF/IFC block");
                    return;
                }
                --indent_;
                emitLine("} else {");
                ++indent_;
                return;
            }
            case Opcode::ENDIF: {
                if (stack.empty() || stack.back() != FrameKind::If) {
                    unsupported_.push_back("ENDIF at instruction " + std::to_string(ii) + " with no open IF/IFC block");
                    return;
                }
                stack.pop_back();
                --indent_;
                emitLine("}");
                return;
            }
            case Opcode::REP: {
                std::vector<SourceParam> ops = trueSourcesForDestlessOpcode(inst);
                if (ops.empty()) return unsupportedShape(inst, ii);
                std::string count = selectedScalarExpr(ops[0], ii);
                std::string var = "rep" + std::to_string(ii);
                emitLine("for (int " + var + " = 0; " + var + " < (int)(" + count + "); ++" + var + ") {");
                ++indent_;
                stack.push_back(FrameKind::Rep);
                return;
            }
            case Opcode::ENDREP: {
                if (stack.empty() || stack.back() != FrameKind::Rep) {
                    unsupported_.push_back("ENDREP at instruction " + std::to_string(ii) + " with no open REP block");
                    return;
                }
                stack.pop_back();
                --indent_;
                emitLine("}");
                return;
            }
            default:
                unsupported_.push_back("opcode " +
                                        std::string(opcodeName(inst.opcodeRaw) ? opcodeName(inst.opcodeRaw) : "?") +
                                        " (raw " + std::to_string(inst.opcodeRaw) +
                                        ") at instruction " + std::to_string(ii) +
                                        " is outside the 35-opcode real population this translator targets"
                                        " (spec §12)");
                emitLine("// UNTRANSLATED opcode " + std::to_string(inst.opcodeRaw));
                return;
        }
    }

    void unsupportedShape(const Instruction& inst, size_t ii) {
        const char* nm = opcodeName(inst.opcodeRaw);
        unsupported_.push_back(std::string("opcode ") + (nm ? nm : "?") + " at instruction " + std::to_string(ii) +
                                " does not have the operand shape spec §12 documents for it (dest present=" +
                                (inst.dest.has_value() ? "yes" : "no") + ", sources=" +
                                std::to_string(inst.sources.size()) + ", predicate=" + (inst.predicate ? "yes" : "no") +
                                ")");
        emitLine("// UNTRANSLATED (unexpected operand shape) instruction " + std::to_string(ii));
    }
};

} // namespace

TranslationResult translateToHlsl(const DisassembledShader& shader, const ConstantTable& ctab, vpp::ByteView blob,
                                   HlslTarget target) {
    Translator t(shader, ctab, blob, target);
    return t.run();
}

} // namespace sr3d3d9bc
