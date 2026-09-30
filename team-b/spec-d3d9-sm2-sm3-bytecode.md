# Direct3D 9 Shader Model 2/3 Bytecode — Token Stream Reference (`spec-d3d9-sm2-sm3-bytecode.md`)

**Origin — read this before anything else.** Every other `spec-*.md` file in this project is Team
A's write-up of what their disassembly work on *this game's executable* found; Team B implements from
their prose and never touches the exe. This document is different in kind: it is **not** derived from
this game or from any disassembly of it. It is Team B's own transcription of a **public, third-party
specification** — Microsoft's own published Direct3D 9 shader bytecode format — sourced directly from
Microsoft Learn's Windows Driver documentation and cross-checked against a public mirror of the actual
`d3d9types.h` SDK header. `spec-fxo-format.md` §1/§3 already established the relevant framing for this
project: "naming and citing a public vendor format by its own well-known structure is a fact, not
proprietary information, exactly as with zlib and Targa/PNG-style formats elsewhere in this project,"
and explicitly invites exactly this: "An implementation that wants the shaders themselves ... can hand
the identified byte range to any standard D3D9 shader disassembler/analysis tool." This document is
that tool's format reference, written once so every future reader/translator in this project cites one
place instead of re-deriving it. It is not proprietary, not reverse-engineered, and not subject to the
game's cleanroom boundary — see `spec-fxo-format.md`'s own precedent before questioning this framing.

**Sources, cited exactly, nothing paraphrased from memory without checking it against these:**
- [Direct3D Shader Codes](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/direct3d-shader-codes) (index page)
- [Shader Code Format](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/shader-code-format)
- [Version Token](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/version-token)
- [Instruction Token](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/instruction-token)
- [Destination Parameter Token](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/destination-parameter-token)
- [Source Parameter Token](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/source-parameter-token)
- [Comment Token](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/comment-token)
- [End Token](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/end-token)
- [DCL Instruction Format](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/dcl-instruction)
- [Shader Relative Addressing](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/shader-relative-addressing)
- [_D3DSHADER_INSTRUCTION_OPCODE_TYPE](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d9types/ne-d3d9types-_d3dshader_instruction_opcode_type) (semantics/operand shape of every opcode)
- [_D3DSHADER_PARAM_REGISTER_TYPE](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d9types/ne-d3d9types-_d3dshader_param_register_type)
- Public mirror of the actual SDK header, used to get **exact numeric enum values** the prose pages above describe only by position (`apitrace/dxsdk`'s `Include/d3d9types.h`, a long-standing public mirror of the Microsoft DirectX SDK header) — **this cross-check mattered**: as §7/§9 below show, the opcode enum is *not* purely sequential and two register-type values are context-overloaded; reading them off the prose pages' listed *order* alone would have been wrong.

**Confidence marking convention (same as every other spec in this project):** everything below is
**[PUBLIC SPEC]** — taken directly from the cited Microsoft documentation, not inferred or guessed.
Where this document adds project-specific application notes (how this connects to `sr3fxo`'s already-
extracted blobs), those are marked **[APPLICATION NOTE]** and are Team B's own, clearly separated from
the public format facts.

## 1. Overall shader token stream shape

A shader is a flat stream of 32-bit little-endian DWORD tokens:

```
version_token
{ [comment_token, comment_payload...] | instruction_token, [params...] }*
end_token
```

**[PUBLIC SPEC]** — Shader Code Format. The first token is always the version token. What follows is a
mix of instruction tokens (each possibly followed by destination/source parameter tokens depending on
the specific opcode's own operand shape) and comment tokens (which the D3DSIO_COMMENT sentinel opcode
also aliases — see §3). The stream ends with the end token. `spec-fxo-format.md` §3 already confirmed,
empirically, that this exact shape (version token → ... → end token `0x0000FFFF`) is what actually
appears in this game's `.fxo_pc` shader blobs — this document supplies the *interior* structure that
prior work didn't need to decode.

## 2. Version token (first DWORD)

**[PUBLIC SPEC]**

| Bits | Field |
|---|---|
| `[7:0]` | Minor version number |
| `[15:8]` | Major version number |
| `[31:16]` | `0xFFFE` = vertex shader, `0xFFFF` = pixel shader |

`spec-fxo-format.md` §3 already identified this exact token in real files (`vs_3_0`/`ps_3_0` on the one
confirmed sample) — this table is that identification's own public-format basis, spelled out in full.

## 3. Instruction token

**[PUBLIC SPEC]**

| Bits | Field |
|---|---|
| `[15:0]` | Opcode (see §7 for the full table). Two values are reserved sentinels handled specially, not real instructions: `0xFFFE` = comment token (§4), `0xFFFF` = end token (§5). A third sentinel, `0xFFFD` (`D3DSIO_PHASE`), is a real, argument-less pixel-shader-1_4-only instruction, not a comment/end alias — decode it as an ordinary zero-operand instruction. |
| `[23:16]` | Opcode-specific control bits (e.g. the comparison kind for `IFC`/`BREAKC`/`SETP`, or the sampler texture-type for a sampler `DCL` — see §8's per-opcode "control bits" column where the public docs specify one). |
| `[27:24]` | **SM2.0+ only** (reserved/0 before that): instruction length in DWORDs, i.e. the count of tokens following the instruction token itself that belong to this one instruction. **This is the field a length-driven decoder should walk by** — see §9. |
| `[28]` | **SM2.0+ only**: predicate bit. If `1`, an extra predicate source token follows this instruction's normal operands. |
| `[29]` | Reserved, `0`. |
| `[30]` | **PS < 2.0 only**: co-issue bit (execute with the previous instruction rather than separately). Reserved/`0` for SM2.0+ and for all vertex shaders. |
| `[31]` | Always `0`. |

## 4. Comment token (aliases opcode `0xFFFE`)

**[PUBLIC SPEC]**

| Bits | Field |
|---|---|
| `[15:0]` | `0xFFFE` (identifies this as a comment, not a real instruction) |
| `[30:16]` | Length of the comment payload that follows, in DWORDs (up to 2^15) |
| `[31]` | `0` |

The payload DWORDs immediately follow and are skipped by a disassembler that doesn't care about
comment content. **[APPLICATION NOTE]** `spec-fxo-format.md` §3 already identified the specific comment
whose payload begins with the `CTAB` FourCC (Microsoft's own public constant-table sub-chunk format,
also not reverse-engineered) as the very first thing after the version token in this game's shaders —
a disassembler built from this document should expect to skip exactly one such comment (the CTAB) right
after the version token on every real shader, then proceed to real instructions.

## 5. End token (aliases opcode `0xFFFF`)

**[PUBLIC SPEC]** A single DWORD, value `0x0000FFFF` exactly (all 32 bits, not just the low 16 — this
differs from the comment token, which only fixes its low 16 bits). Marks the end of the shader code.
`spec-fxo-format.md` §3/§7 already located this exact value as the per-blob terminator in real files.

## 6. Destination and source parameter tokens

Most instructions are followed by one destination parameter token and zero or more source parameter
tokens, per the specific opcode (§8's operand-shape notes).

### 6.1 Destination parameter token

**[PUBLIC SPEC]**

| Bits | Field |
|---|---|
| `[10:0]` | Register number (offset within the register file named by the register-type field below) |
| `[12:11]` | Register-type bits 3-4 (high two bits of the 5-bit register-type value — combine with `[30:28]` below) |
| `[13]` | **VS 3.0+ only**: relative-addressing flag (§6.3). Reserved/`0` for PS and for VS < 3.0. |
| `[15:14]` | Reserved, `0` |
| `[19:16]` | Write mask: bit 16 = X/R, bit 17 = Y/G, bit 18 = Z/B, bit 19 = W/A |
| `[23:20]` | Result modifier, ORed bitmask: `0x1` = Saturate (VS), `0x2` = Partial precision (PS), `0x4` = Centroid (PS) |
| `[27:24]` | **PS < 2.0 only**: result shift scale (signed shift). Reserved/`0` for PS 2.0+ and all VS. |
| `[30:28]` | Register-type bits 0-2 (low three bits — combine with `[12:11]` above) |
| `[31]` | Always `1` |

**Register type is a 5-bit value split non-contiguously across two bit ranges**: bits `[30:28]` are its
low 3 bits, bits `[12:11]` are its high 2 bits. `register_type = (token>>28 & 0x7) | ((token>>11 & 0x3) << 3)`.
See §7 for the enum.

### 6.2 Source parameter token

**[PUBLIC SPEC]**

| Bits | Field |
|---|---|
| `[10:0]` | Register number |
| `[12:11]` | Register-type bits 3-4 (same split-field convention as the destination token) |
| `[13]` | **PS 3.0+ and all VS**: relative-addressing flag (§6.3). Reserved/`0` for PS < 3.0. |
| `[15:14]` | Reserved, `0` |
| `[17:16]` | Swizzle, channel X |
| `[19:18]` | Swizzle, channel Y |
| `[21:20]` | Swizzle, channel Z |
| `[23:22]` | Swizzle, channel W |
| `[27:24]` | Source modifier (§6.4) |
| `[30:28]` | Register-type bits 0-2 |
| `[31]` | Always `1` |

Each 2-bit swizzle sub-field selects which of the *source* register's own components (`0`=X, `1`=Y,
`2`=Z, `3`=W) feeds that *destination* channel — e.g. swizzle bits `19:18 = 0x2` means "channel Y of
the result reads component Z of this source."

### 6.3 Relative addressing token

**[PUBLIC SPEC]** Present only for VS 2.0+ (destination and source) and PS 3.0+ (source only), and only
when the parameter token's own bit 13 is set. One extra DWORD, formatted exactly like a parameter token,
follows the parameter token it modifies: only `D3DSPR_ADDR` or `D3DSPR_LOOP` are valid register types in
it; swizzle bits pick the addressing register's component; bit 31 is `1`; every other field is unused.

### 6.4 Source modifier values (bits `[27:24]` of a source parameter token)

**[PUBLIC SPEC]**, cross-checked against the raw header's `_D3DSHADER_PARAM_SRCMOD_TYPE` enum (values
below are that enum's own numeric constants, confirmed identical to the prose table):

| Value | Meaning |
|---|---|
| `0x0` | None |
| `0x1` | Negate |
| `0x2` | Bias |
| `0x3` | Bias and negate |
| `0x4` | Sign (bx2) |
| `0x5` | Sign (bx2) and negate |
| `0x6` | Complement |
| `0x7` | ×2 (PS 1.4 only) |
| `0x8` | ×2 and negate (PS 1.4 only) |
| `0x9` | Divide by Z (PS 1.4 only) |
| `0xA` | Divide by W (PS 1.4 only) |
| `0xB` | `abs(x)` |
| `0xC` | `-abs(x)` |
| `0xD` | Logical NOT (predicate register only) |
| `0xE`-`0xF` | Reserved |

## 7. Register types (5-bit field, destination `[30:28]`+`[12:11]` / source same)

**[PUBLIC SPEC], numeric values from the raw header** (the Microsoft Learn prose page lists these by
enum *declaration order* without giving numbers — **cross-checking against the actual header mattered
here**: two names share one value each, which reading the prose list as a plain 0,1,2,3… sequence would
have missed entirely):

| Value | Name | Meaning |
|---|---|---|
| 0 | `D3DSPR_TEMP` | Temporary register file |
| 1 | `D3DSPR_INPUT` | Input register file |
| 2 | `D3DSPR_CONST` | Constant register file 0-2047 |
| **3** | `D3DSPR_ADDR` (VS) **or** `D3DSPR_TEXTURE` (PS) | **Same numeric value, meaning depends on which shader type this blob's version token (§2) declared.** Address register for VS; texture register file for PS. |
| 4 | `D3DSPR_RASTOUT` | Rasterizer output (VS) |
| 5 | `D3DSPR_ATTROUT` | Attribute output |
| **6** | `D3DSPR_TEXCRDOUT` (VS < 3.0) **or** `D3DSPR_OUTPUT` (VS ≥ 3.0; reserved for PS) | **Also one shared value** — resolve by shader version, not just shader type. |
| 7 | `D3DSPR_CONSTINT` | Constant integer vector register file |
| 8 | `D3DSPR_COLOROUT` | Color output |
| 9 | `D3DSPR_DEPTHOUT` | Depth output |
| 10 | `D3DSPR_SAMPLER` | Sampler state register file |
| 11 | `D3DSPR_CONST2` | Constant register file 2048-4095 |
| 12 | `D3DSPR_CONST3` | Constant register file 4096-6143 |
| 13 | `D3DSPR_CONST4` | Constant register file 6144-8191 |
| 14 | `D3DSPR_CONSTBOOL` | Constant boolean register file |
| 15 | `D3DSPR_LOOP` | Loop counter register |
| 16 | `D3DSPR_TEMPFLOAT16` | 16-bit float temp register file |
| 17 | `D3DSPR_MISCTYPE` | Miscellaneous single registers (e.g. face/position in PS 3.0 `DCL`) |
| 18 | `D3DSPR_LABEL` | Label (branch/call targets) |
| 19 | `D3DSPR_PREDICATE` | Predicate register |
| `0x7fffffff` | `D3DSPR_FORCE_DWORD` | Compiler padding sentinel — never a real encoded value, exists only to force the C enum to 32 bits. A population gate should never see this. |

## 8. DCL instruction — the one opcode with its own unique-shaped operand DWORD

**[PUBLIC SPEC]** `D3DSIO_DCL` (opcode 31, §9's table) does not take ordinary source parameters. Its
shape is: instruction token, then **one plain DWORD** ("the DCL token") whose own bit layout depends on
*what* is being declared, then one destination parameter token. Six documented shapes, distinguished by
shader type/version and the destination parameter's own resolved register type (§7):

| What's declared | DCL token bit layout | Destination token carries |
|---|---|---|
| Sampler (PS 2.0+) | `[30:27]` = `D3DSAMPLER_TEXTURE_TYPE`; rest reserved/`0`; `[31]`=1 | register number, type `D3DSPR_SAMPLER` |
| Input/texture register (PS, untyped) | all reserved/`0` except `[31]`=1 | register number; write mask = declared components |
| VS 2.0+ input register | `[4:0]` = `D3DDECLUSAGE` value (§8.1); `[19:16]` = usage index; rest reserved/`0`; `[31]`=1 | register number, type `D3DSPR_INPUT`; write mask = declared components |
| PS 3.0+ texture register | `[4:0]` = `D3DDECLUSAGE_TEXCOORD` or `_COLOR` only; `[19:16]` = usage index (0-7 for texcoord, 0 for color); rest reserved/`0`; `[31]`=1 | register number, type `D3DSPR_TEXTURE` |
| Face register (PS 3.0+) | all reserved/`0` except `[31]`=1 | the face register; write mask must be full but is unused |
| Position register | all reserved/`0` except `[31]`=1 | the position register; write mask = declared components |
| VS 3.0+ output register | same layout as the VS input row above, but destination register type is `D3DSPR_OUTPUT` | register number, type `D3DSPR_OUTPUT`; write mask = which components this DCL writes (a register can be declared by more than one DCL with disjoint masks) |

### 8.1 `D3DDECLUSAGE` values (bits `[4:0]` of a `DCL` token, when it applies)

**[PUBLIC SPEC]**, raw-header order (sequential from 0, no gaps, verbatim from the public mirror):
`POSITION`=0, `BLENDWEIGHT`=1, `BLENDINDICES`=2, `NORMAL`=3, `PSIZE`=4, `TEXCOORD`=5, `TANGENT`=6,
`BINORMAL`=7, `TESSFACTOR`=8, `POSITIONT`=9, `COLOR`=10, `FOG`=11, `DEPTH`=12, `SAMPLE`=13.

## 9. Full opcode table (16-bit field, instruction token bits `[15:0]`)

**[PUBLIC SPEC], numeric values from the raw header — this is the single most important correction
this cross-check produced.** The enum is declared in **three separate numeric bands**, not one
sequential run — reading the Microsoft Learn prose page's *listed order* as an implicit 0,1,2,3…
sequence (the natural reading of a plain enumerated list with no numbers shown) would have been wrong
for every opcode from `D3DSIO_TEXCOORD` onward, and would have silently misidentified `PHASE`/
`COMMENT`/`END` as ordinary sequential opcodes 86/87/88 instead of the reserved high sentinels they
actually are:

**Band 1 (0-48, contiguous):**
`NOP`=0, `MOV`=1, `ADD`=2, `SUB`=3, `MAD`=4, `MUL`=5, `RCP`=6, `RSQ`=7, `DP3`=8, `DP4`=9, `MIN`=10,
`MAX`=11, `SLT`=12, `SGE`=13, `EXP`=14, `LOG`=15, `LIT`=16, `DST`=17, `LRP`=18, `FRC`=19, `M4x4`=20,
`M4x3`=21, `M3x4`=22, `M3x3`=23, `M3x2`=24, `CALL`=25, `CALLNZ`=26, `LOOP`=27, `RET`=28, `ENDLOOP`=29,
`LABEL`=30, `DCL`=31, `POW`=32, `CRS`=33, `SGN`=34, `ABS`=35, `NRM`=36, `SINCOS`=37, `REP`=38,
`ENDREP`=39, `IF`=40, `IFC`=41, `ELSE`=42, `ENDIF`=43, `BREAK`=44, `BREAKC`=45, `MOVA`=46, `DEFB`=47,
`DEFI`=48.

**Gap: 49-63 are unassigned** — not real, not reserved-with-a-name, simply absent from the enum. A
value here should be treated the same as any other unknown opcode (§10's population-gate discipline).

**Band 2 (64-96, contiguous):**
`TEXCOORD`=64, `TEXKILL`=65, `TEX`=66, `TEXBEM`=67, `TEXBEML`=68, `TEXREG2AR`=69, `TEXREG2GB`=70,
`TEXM3x2PAD`=71, `TEXM3x2TEX`=72, `TEXM3x3PAD`=73, `TEXM3x3TEX`=74, `RESERVED0`=75 (a real, named-but-
reserved-for-internal-use value — decode it as a recognized-but-unimplemented opcode, not an unknown
one), `TEXM3x3SPEC`=76, `TEXM3x3VSPEC`=77, `EXPP`=78, `LOGP`=79, `CND`=80, `DEF`=81, `TEXREG2RGB`=82,
`TEXDP3TEX`=83, `TEXM3x2DEPTH`=84, `TEXDP3`=85, `TEXM3x3`=86, `TEXDEPTH`=87, `CMP`=88, `BEM`=89,
`DP2ADD`=90, `DSX`=91, `DSY`=92, `TEXLDD`=93, `SETP`=94, `TEXLDL`=95, `BREAKP`=96.

**Band 3 (high sentinels, non-contiguous with band 2 and with each other in value but each individually
load-bearing):** `PHASE`=`0xFFFD` (a real, zero-operand PS-1.4-only instruction — decode normally, do
NOT treat as comment/end), `COMMENT`=`0xFFFE` (§4 — not a normal instruction, switches to comment-skip
mode), `END`=`0xFFFF` (§5 — terminates the stream).

**`FORCE_DWORD`=`0x7fffffff`** is a C-enum-width padding sentinel, structurally identical to
`D3DSPR_FORCE_DWORD` in §7 — never a real encoded opcode, would only appear from a corrupt/misparsed
stream.

### 9.1 Per-opcode instruction length / operand count (SM2.0+: read from the instruction token itself, §3)

For SM2.0+ shaders (which is what this game's `.fxo_pc` blobs actually are per `spec-fxo-format.md`'s
own population figures — `vs_3_0`/`ps_3_0` confirmed), **the instruction-length field at instruction
token bits `[27:24]` is authoritative and self-describing**: it states exactly how many DWORDs follow
before the next instruction token, regardless of whether a decoder also knows that specific opcode's
"normal" operand count from the public reference. **[APPLICATION NOTE]** This is the right primary
walk mechanism for a population-gate disassembler: trust the length field to advance the cursor, and
use the public per-opcode operand-shape documentation (linked above, one page per opcode) only to
*interpret* what's inside that many DWORDs, not to *compute* how far to skip. A shader with an opcode
this document doesn't have full semantic detail for can still be walked correctly and reported as
"unknown/unimplemented opcode N, correctly skipped via its own length field" rather than causing a
decode failure — a materially different, weaker failure mode than losing sync with the stream entirely.

## 10. What "every instruction token decoded, zero unknown opcodes" should mean in practice

**[APPLICATION NOTE]** Per the orchestrator's proposed staging: a first population gate over all 7,276
shader blobs `spec-fxo-format.md` already locates should (a) parse the version token, (b) walk the
comment/instruction stream using §9.1's length-field-driven cursor advance, (c) classify every
instruction token's low-16-bit opcode field against §9's table, and (d) report separately: opcodes in
bands 1/2/the three sentinels (fully known), `RESERVED0` (named but not semantically modeled — a real,
expected value, not a bug), anything in the 49-63 gap or any other value (a genuine unknown, worth
investigating), and confirm the stream lands exactly on the end token with no trailing garbage. "Zero
unknown opcodes" should mean zero hits outside bands 1/2/the sentinels/`RESERVED0` — not zero
`RESERVED0` hits, since that value is a real, documented (if under-specified) part of the format.

## 11. What this document deliberately does not cover

Per-opcode semantic detail beyond what's needed to walk the stream and identify operand *shape* was
originally left for "on demand" pull rather than front-loaded — **§12 below is that on-demand pull**,
done once Stage 1 (`sr3d3d9bc`, HANDOFF §9.97) measured EXACTLY which opcodes this game's own shaders
actually use (35 of the ~90 real ones, real-data population census, not a guess). The `CTAB` constant-
table sub-chunk's own internal layout is a separate public format; it does not need transcribing here
because `sr3fxo::inspectD3d9Blob`/`D3d9BlobInfo` (`include/sr3fxo/d3d9_blob.h`) already parses it —
name, register set, register index, register count per constant — so a translator gets constant/
sampler names for free from existing code rather than needing this document to cover it too.

## 12. Per-opcode semantics — the 35 real opcodes this game's shaders actually use (Stage 2 reference)

**[PUBLIC SPEC]**, sourced the same way as everything above (Microsoft Learn's per-opcode pages, cited
inline), scoped down from the full ~90-opcode instruction set to exactly the 35 values
`sr3d3d9bc`'s population run over all 7,276 real shader blobs measured (HANDOFF §9.97's own opcode
histogram — this is a real-data-driven scope, not an arbitrary subset). A translator targeting this
game's actual shaders needs semantics for these 35 and no others; the remaining ~55 documented opcodes
this game never emits are deliberately left uncovered here — pull them individually, the same way,
if a shader outside the measured population ever needs one.

**Convention below**: `dest.mask` = the destination's active write-mask channels (§6.1); for a per-
channel binary/unary op, the operation applies independently to each active channel using that same
channel's own selected source component(s) (via each source's own swizzle, §6.2) unless stated
otherwise. `srcN.c` denotes source N's value in channel `c` after its own swizzle and source-modifier
(§6.4) have been applied — modifiers apply BEFORE the opcode's own operation, uniformly, and are not
re-stated per opcode below. Every opcode in this section requires a preceding destination parameter
token and the stated number of source parameter tokens (§6), matching the instruction-length field
(§3/§9.1) exactly.

| Opcode | Src count | Semantics (per active `dest` channel unless noted) | Notes / HLSL mapping |
|---|---|---|---|
| `MOV` | 1 | `dest.c = src0.c` | Direct assignment. When destination is `D3DSPR_ADDR` (address register), the spec states float→int conversion via round-to-nearest, not truncation. |
| `ADD` | 2 | `dest.c = src0.c + src1.c` | |
| `MAD` | 3 | `dest.c = src0.c * src1.c + src2.c` | HLSL `mad(a,b,c)`. |
| `MUL` | 2 | `dest.c = src0.c * src1.c` | |
| `RCP` | 1 | `dest.c = 1.0 / src0.selected` | **Requires explicit replicate swizzle on the source** — src0 supplies ONE selected component (whichever the source's own swizzle names), and that single scalar reciprocal is broadcast to every active dest channel. Not a per-channel independent reciprocal. HLSL `rcp(x)` on the one selected scalar, replicated. |
| `RSQ` | 1 | `dest.c = 1.0 / sqrt(abs(src0.selected))` | Same replicate-swizzle-required shape as `RCP`. Microsoft's own docs for this opcode do not separately restate the `abs()`; HLSL's `rsqrt()` intrinsic already defines input `< 0` as undefined/NaN-producing rather than mirroring, so a translator should emit `rsqrt(abs(x))` only if empirically needed — **left as an open implementation question, not asserted as fact**, since the public page for this specific opcode does not state it explicitly (unlike some other D3D ASM references which do) — flag this rather than guess if it matters for a specific real shader. |
| `DP3` | 2 | `dest.c = src0.x*src1.x + src0.y*src1.y + src0.z*src1.z` (same 3-component dot product broadcast to every active dest channel) | HLSL `dot(src0.xyz, src1.xyz)`, replicated. |
| `DP4` | 2 | `dest.c = src0.x*src1.x + src0.y*src1.y + src0.z*src1.z + src0.w*src1.w` (4-component dot, broadcast) | HLSL `dot(src0, src1)`, replicated. |
| `MIN` | 2 | `dest.c = min(src0.c, src1.c)` | |
| `MAX` | 2 | `dest.c = max(src0.c, src1.c)` | |
| `SLT` | 2 | `dest.c = (src0.c < src1.c) ? 1.0 : 0.0` | "Stores the sign (1.0f for TRUE and 0.0f for FALSE)." |
| `SGE` | 2 | `dest.c = (src0.c >= src1.c) ? 1.0 : 0.0` | |
| `EXP` | 1 | `dest.c = pow(2.0, src0.selected)` (full precision) | Requires explicit replicate swizzle, same shape as `RCP`. HLSL `exp2(x)`. |
| `LOG` | 1 | `dest.c = log2(src0.selected)` (full precision) | Requires explicit replicate swizzle. HLSL `log2(x)`. |
| `LRP` | 3 | `dest.c = src0.c * src1.c + (1.0 - src0.c) * src2.c` | "Interpolates linearly between the second and third sources by a proportion specified in the first source." HLSL `lerp(src2, src1, src0)` (note MS's own argument order: proportion is src0, "from"/"to" are src1/src2 — double-check against `lerp`'s own `(a,b,t)` order when emitting, since the natural reading transposes them). |
| `FRC` | 1 | `dest.c = frac(src0.c)` | "Each component of the result is in the range from 0.0 through 1.0." Direct HLSL `frac()`. |
| `POW` | 2 | `dest.c = pow(src0.selected, src1.selected)` (full precision) | Both sources require explicit replicate swizzle (two independent selected scalars, not per-channel). Direct HLSL `pow(a,b)`, replicated. |
| `ABS` | 1 | `dest.c = abs(src0.c)` | |
| `NRM` | 1 | writes a normalized 4-D vector derived from src0 across the active dest channels | "Normalizes a 4-D vector." The page's own text doesn't spell out the exact per-channel formula beyond that; HLSL `normalize(src0)` is the direct semantic match for a 3D-space normalize, but confirm the *4-D* claim (normalize by the full 4-component length, not just xyz) against a real shader's observed dest write-mask before assuming — **flagged as an open precision question, not fabricated**, same as `RSQ` above. |
| `CMP` | 3 | `dest.c = (src0.c >= 0.0) ? src1.c : src2.c` | "Chooses between the second and third sources, based on the first source being greater than or equal to zero... The comparison is done per channel." HLSL ternary or `src0 >= 0 ? src1 : src2` component-wise (e.g. via `lerp`/`step`-based construction, or a per-component `select`). |
| `DP2ADD` | 3 | `dest.c = src0.x*src1.x + src0.y*src1.y + src2.selected` (2-D dot product plus a scalar addend, broadcast) | "Performs a 2-D dot product and scalar addition." `src2`'s own selected replicate-swizzle component is the addend. |
| `SINCOS` | 1 (SM3.0+; 3 pre-3.0, not relevant to this game — see below) | `dest.x = cos(src0.selected)`, `dest.y = sin(src0.selected)`, `dest.z` undefined; **only X and Y may appear in the write mask** | **[Confirmed narrowed to this game's real shape]**: "for pixel and vertex shader version 3\_0 and later, only the first source parameter token is used" — the second/third-source constant-table shape this page documents is a pre-3.0 macro-expansion detail this game never needs (confirmed vs_3_0/ps_3_0-only per `spec-fxo-format.md`'s own population figures). Maps directly and exactly to HLSL's native `sincos(x, out s, out c)` intrinsic — no need to replicate the page's own Taylor-series macro, which exists only because SM2/3 hardware lacked a native transcendental instruction; the translator's HLSL target does not have that limitation. Source: [SINCOS Instruction Format](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/sincos-instruction). |
| `MOVA` | 1 | `dest.c = round_to_nearest(src0.c)`, written to the address register | "Moves data from floating-point register to integer register... values are converted from floating-point using rounding to nearest. The address register is the only destination register allowed." |
| `DEFI` | 0 (dest + 4 literal integer DWORDs, NOT source parameter tokens) | Defines a constant: `dest` (register type must be `D3DSPR_CONSTINT`) is loaded with 4 raw 32-bit signed integers that follow **as plain DWORD literals, not parameter tokens** | **Structural note for the disassembler/translator boundary**: `sr3d3d9bc`'s own Stage-1 decoder (HANDOFF §9.97) deliberately does NOT special-case this — its generic "first bit-31-set DWORD is dest, rest are sources" walk works by accident here only because an arbitrary signed-integer literal's bit 31 is essentially a coin flip, so these 4 DWORDs sometimes get mis-classified as parameter tokens and land in `extraDwords` otherwise. **A translator MUST special-case `DEFI` (and `DEF`/`DEFB` below) explicitly** — read the literal DWORDs positionally (instruction length tells you there are exactly 5 more DWORDs: 1 dest + 4 literal), never re-run the generic parameter-token parse on them. |
| `DEF` | 0 (dest + 4 literal DWORDs, same shape as `DEFI` but interpreted as one 4-D float, not 4 separate ints) | Defines a float constant: `dest` (register type `D3DSPR_CONST`/`CONST2`/`CONST3`/`CONST4`) loaded with 4 literal float DWORDs | Same structural note as `DEFI` — must be special-cased by a translator, not walked as ordinary source parameters. |
| `DCL` | 0 (its own uniquely-shaped token, §8) | Declares an input/output/sampler/texture register — see §8's full table for the 8 shapes and their bit layouts | **This is the opcode a translator needs most for HLSL signature generation**: it is the ONLY source this bytecode format gives for "what are this shader's inputs/outputs and their semantics." Map `D3DDECLUSAGE` values (§8.1) to HLSL semantic names — for the ones this game's real shaders actually use, `POSITION`→`POSITION`, `NORMAL`→`NORMAL`, `TEXCOORD`(+index)→`TEXCOORD`*n*, `COLOR`→`COLOR`, `TANGENT`→`TANGENT`, `BLENDWEIGHT`/`BLENDINDICES`→`BLENDWEIGHT`/`BLENDINDICES` (the HLSL semantic names are, not coincidentally, the same public Microsoft vocabulary — confirm the exact spelling against a real `D3DCompile`/`fxc` HLSL sample rather than assuming a 1:1 name match holds for every usage index/register combination). A sampler `DCL` (register type `D3DSPR_SAMPLER`) declares a `sampler`/`Texture2D` binding at that register slot; correlate its register number against `sr3fxo::D3d9BlobInfo::constants`' `registerSet`/`registerIndex` (already parsed by existing code, §11) to get the real sampler/constant NAME rather than just a bare register number. |
| `TEXKILL` | 0 (a destination-shaped token used as a source — see the opcode's own public page) | If any of the token's first 3 components is `< 0`, discard the current pixel | HLSL `clip(token.xyz)` (HLSL's `clip()` already implements exactly this "discard if any component < 0" rule) or an explicit `if (any(token.xyz < 0)) discard;`. Per the public page: "A temporary or texture register type must be used... A complete write mask must be specified." |
| `TEX` | 2 (this game is confirmed ps_3_0-only, so this is unconditionally the `texld` shape, never the source-less ps_1.x `tex` shape) | `dest = sample(sampler[src1], src0.xy or .xyz or .xyzw depending on the sampler's own declared dimensionality)` | "The `tex` and `texld` assembler instructions both use the `D3DSIO_TEX` opcode. The `texld` instruction applies to pixel shader version 1_4 and later; it has one destination parameter token and two source parameter tokens." First source = texture coordinates, second source uses `D3DSPR_SAMPLER` and identifies which sampler. Direct HLSL `tex2D`/`tex3D`/`texCUBE` (or `.Sample()` on a `Texture*` object) depending on the sampler's own declared dimensionality (from its own `DCL`, §8). |
| `TEXLDL` | 2 | Samples a texture at an explicit, caller-supplied level of detail: the LOD is the 4th (W) component of the first source's texture-coordinate vector, the second source (`D3DSPR_SAMPLER`) identifies the sampler | Direct HLSL `tex2Dlod`/`.SampleLevel()` equivalent — "the particular level of detail (LOD) that is sampled must be specified as the fourth (W) component of the texture coordinate." |
| `IF` | 1 (`D3DSPR_CONSTBOOL`) | Structured control flow: if the boolean source is TRUE, execute through the matching `ELSE`/`ENDIF`, else skip to `ELSE` (if present) or past `ENDIF` | Maps to HLSL `if (src0) { ... }` / `else { ... }` structurally — an `IF`...`ELSE`...`ENDIF` (or `IF`...`ENDIF`) run in the instruction stream is exactly one HLSL `if`/`else` block; the translator needs to track nesting depth to find each `IF`'s matching `ELSE`/`ENDIF` (structurally, not via any explicit "jump target" field — this ISA is a nested block structure, not a jump-based one, for `IF`/`REP`/loops). |
| `IFC` | 2 | Same control-flow shape as `IF`, but the condition is a per-channel comparison between the two sources (both requiring explicit replicate swizzle) rather than reading a boolean register directly; the specific comparison operator (`<`,`>=`, etc.) is encoded in the instruction token's own control bits (§3's `[23:16]` field — this page doesn't enumerate the exact bit-to-operator mapping, a translator needs the `_D3DSHADER_COMPARISON` /`D3DSPC_*` enum, not yet pulled into this document; pull it the same way if/when `IFC` support is actually being implemented) | "Skips a block of code, based on the comparison between sources... if the comparison between ALL source components is TRUE" (note: ALL components, even though each source individually needs a replicate swizzle per the page's own wording — this is a real, slightly confusing detail worth re-reading the source page carefully before implementing, not resolved further here). |
| `ELSE` | 0 | Marks the else-branch of the innermost open `IF`/`IFC` | |
| `ENDIF` | 0 | Closes the innermost open `IF`/`IFC`/`ELSE` block | |
| `REP` | 1 (`D3DSPR_CONSTINT`, X component = iteration count) | Structured loop: repeat the enclosed block (through the matching `ENDREP`) the stated integer number of times | Maps to an HLSL `for (int i = 0; i < src0.x; ++i) { ... }`-shaped loop; same nesting-nearest-match discipline as `IF`/`ENDIF`. |
| `ENDREP` | 0 | Closes the innermost open `REP` block | |

**What is deliberately still open after this section** (same "don't guess" discipline as everywhere
else in this project): `IFC`'s exact per-control-bit comparison operator table (`D3DSPC_*`, not yet
pulled — needed only if/when a translator actually implements `IFC`, which appeared 209 times in the
real population, non-trivial but also not the majority instruction); `RSQ`'s and `NRM`'s exact edge-
case behavior at/near zero/negative inputs (flagged inline above, not fabricated); the precise
HLSL semantic-name mapping for every `D3DDECLUSAGE`×usage-index combination this game's real shaders
actually emit (needs cross-checking against a real HLSL compile, not just the public enum names) —
all three are real, bounded, checkable-later gaps, not blockers for a first translator pass over the
instructions that don't need them.

## 13. The `CTAB` constant table — named constants/samplers, for real HLSL declarations

**[PUBLIC SPEC]**, same sourcing discipline as everywhere above. `CTAB` is the standard D3DX9 shader-
reflection sub-chunk every real shader in this game carries as its first comment token (already
located, not fully parsed, by `spec-fxo-format.md` §3 and `sr3fxo::inspectD3d9Blob`/`D3d9BlobInfo`,
`include/sr3fxo/d3d9_blob.h`, which already reads name/registerSet/registerIndex/registerCount for
each constant but not the type-info this section adds). This is what lets a translator emit named
`cbuffer` members and `sampler`/`Texture2D` declarations instead of bare register numbers — directly
relevant now that `spec-render-pipeline.md` §20.3(d)/§20.4 (Team A, 2026-09-28) confirmed the engine's
OWN constant binding is positional/register-based, not name-hash-based, for every pattern traced
except one post-process function whose register numbers plausibly resolve through exactly this CTAB
data at shader-load time — so CTAB names are the connective tissue between this project's own bytecode
translation and Team A's engine-side register findings, not a redundant parallel mechanism.

**Struct layout** (all offsets DWORD-relative to the comment payload's start, i.e. the DWORD
immediately after the `CTAB` FourCC itself — `sr3fxo::inspectD3d9Blob` already establishes this base,
called `t` there):

`D3DXSHADER_CONSTANTTABLE` (28 bytes): `Size`(+0), `Creator`(+4, string-table offset), `Version`(+8),
`Constants`(+12, count), `ConstantInfo`(+16, offset to the array below), `Flags`(+20), `Target`(+24,
string-table offset, e.g. `"vs_3_0"`).

Each `D3DXSHADER_CONSTANTINFO` (20 bytes, `Constants`-many, at `ConstantInfo`): `Name`(+0, DWORD, table-
relative string offset), `RegisterSet`(+4, WORD, §13.1), `RegisterIndex`(+6, WORD), `RegisterCount`(+8,
WORD), `Reserved`(+10, WORD, unused), `TypeInfo`(+12, DWORD, table-relative offset to the struct
below), `DefaultValue`(+16, DWORD, table-relative offset, 0 if none — already-existing code doesn't
read this; a translator generally doesn't need it either, since real constant VALUES come from the
engine at draw time per §20.3, not from a shader-authored default).

Each `D3DXSHADER_TYPEINFO` (12 bytes, at a `CONSTANTINFO`'s own `TypeInfo` offset): `Class`(+0, WORD,
§13.2), `Type`(+2, WORD, §13.3), `Rows`(+4, WORD), `Columns`(+6, WORD), `Elements`(+8, WORD, array
length; 1 if not an array), `StructMembers`(+10, WORD), `StructMemberInfo`(+12... **note: this makes
the struct 14 bytes by field layout, but the struct is documented as returning contiguous
`D3DXSHADER_STRUCTMEMBERINFO` entries starting at this offset - a translator should read `StructMembers`-
many `D3DXSHADER_STRUCTMEMBERINFO` entries there when `Class == D3DXPC_STRUCT`, and otherwise ignore
this field entirely; not expected to matter for this game's real constants unless one is empirically a
struct type, worth confirming on real data rather than assuming either way**).

Each `D3DXSHADER_STRUCTMEMBERINFO` (8 bytes, only present/relevant when `Class == D3DXPC_STRUCT`):
`Name`(+0, DWORD, table-relative string offset), `TypeInfo`(+4, DWORD, table-relative offset to a
nested `D3DXSHADER_TYPEINFO`, recursively).

All string offsets (`Name`, `Creator`, `Target`) are table-relative (same base as everything else) and
point at ordinary null-terminated ASCII text — `sr3fxo::inspectD3d9Blob` already reads `Name` exactly
this way; `Creator`/`Target` are new, not yet read by existing code, and are purely descriptive
(compiler version string, target profile string) — useful for logging/diagnostics, not required for
binding.

### 13.1 `RegisterSet` values (`D3DXREGISTER_SET`, `CONSTANTINFO` bits, WORD field but only values 0-3 used)

`D3DXRS_BOOL`=0, `D3DXRS_INT4`=1, `D3DXRS_FLOAT4`=2, `D3DXRS_SAMPLER`=3. This is a DIFFERENT
enumeration from §7's `D3DSPR_*` register-type field (the bytecode's own operand register-type, used
inside instruction tokens) — `CTAB`'s `RegisterSet` is coarser (which of the four physically-separate
constant memories: bool/int4/float4/sampler) and names which one `RegisterIndex` counts within, not
which of §7's ~20 register kinds. A `RegisterSet == D3DXRS_SAMPLER` entry is exactly the named-sampler-
binding case §12's `DCL`/`TEX` entries need — `RegisterIndex` there is the sampler register number a
real `TEX`/`TEXLD` instruction's own second source parameter names (§6.2, §12's `TEX` row).

### 13.2 `Class` values (`D3DXPARAMETER_CLASS`)

`D3DXPC_SCALAR`=0, `D3DXPC_VECTOR`=1, `D3DXPC_MATRIX_ROWS`=2, `D3DXPC_MATRIX_COLUMNS`=3,
`D3DXPC_OBJECT`=4 (samplers/textures fall here), `D3DXPC_STRUCT`=5.

### 13.3 `Type` values (`D3DXPARAMETER_TYPE`) — full enum, only the ones a real constant/sampler can be are relevant here

`D3DXPT_VOID`=0, `D3DXPT_BOOL`=1, `D3DXPT_INT`=2, `D3DXPT_FLOAT`=3 (the overwhelming majority of real
constants, given `RegisterSet == D3DXRS_FLOAT4` is this game's dominant constant memory per §20.3's own
findings), `D3DXPT_STRING`=4, `D3DXPT_TEXTURE`=5, `D3DXPT_TEXTURE1D`=6, `D3DXPT_TEXTURE2D`=7,
`D3DXPT_TEXTURE3D`=8, `D3DXPT_TEXTURECUBE`=9, `D3DXPT_SAMPLER`=10, `D3DXPT_SAMPLER1D`=11,
`D3DXPT_SAMPLER2D`=12, `D3DXPT_SAMPLER3D`=13, `D3DXPT_SAMPLERCUBE`=14 (11-14 are what a real
`RegisterSet == D3DXRS_SAMPLER` entry's own `Type` should be — which one tells a translator whether to
declare `sampler2D`/`samplerCUBE`/etc., or the modern `Texture2D`/`TextureCube` equivalent).

### 13.4 What this section is for, concretely

A population-gate CTAB reader over all 7,276 real blobs (extending, not replacing,
`sr3fxo::inspectD3d9Blob`'s existing name/register parsing with `TypeInfo`) should report, per blob:
every constant's name, register set/index/count, class, type, and rows×columns shape — and, as its own
gate, confirm every blob either has a well-formed `CTAB` (structurally valid per this section, e.g.
`Constants` count's entries all land inside the comment payload) or is explicitly counted as having
none, with zero blobs landing in neither bucket. This directly produces, per shader, the join key
(`RegisterSet`+`RegisterIndex`) a translator needs to label a `cbuffer` member or `sampler`/`Texture*`
declaration with its real authored name instead of a bare register number — and, per
`spec-render-pipeline.md` §20.4, is very likely also the actual mechanism the one "parameter-struct"
post-process function (§20.3(d)) resolves through at shader-load time, making this section relevant to
Team A's own open question too, not just this project's translator.
