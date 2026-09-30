# Saints Row: The Third — `.cefct_pc` Particle/Visual Effect Format Specification

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1, light follow-on pass (flagged as open in `spec-mission-packages.md` §3 and `spec-geometry-format.md` §3.1)
**Scope:** The internal structure of `.cefct_pc` particle/visual-effect files, beyond the already-documented shared material-reference-block header.
**Method:** Black-box byte/string inspection of three real, fully-reliable samples (`vfx_shockwave_kill_all.cefct_pc`, 12,480 bytes; `vfx_whored_invulnerable.cefct_pc`, 5,264 bytes; `vfx_runningman_fireworks.cefct_pc`, 14,416 bytes — all extracted as entries of mode-(b) shared-stream containers per `spec-mission-packages.md`, so fully reliable, no corruption risk). No disassembly this pass — explicitly scoped as a lighter, stretch-goal follow-on rather than a full target, and treated that way: real structure is documented where found, and the bulk of the format is honestly left open rather than forced.

**UPDATE — disassembly follow-on pass (see §5):** the constructor `FUN_005cee40` (ID 4/15, "Vehicle VFX"/"VFX") was traced forward through its real parse chain and cross-checked byte-for-byte against the same 3 samples (re-extracted fresh from `sr3_city_missions.vpp_pc`'s `Horde Mode`/`Running Man` packages, confirmed identical). §5 supersedes/corrects several black-box guesses below (struck through in place per this project's convention, not deleted) and resolves the per-sub-object record layout that was this document's main open item.

**UPDATE 2 — consumer-side pass (see §6, Agent AB, 2026-09-20):** the runtime code that *reads* a loaded effect (emitter step, particle spawn, record instantiate, class registry, debug overlay, curve evaluator) was located and decoded, so the parameter block behind `record+0x28` is now field-mapped (§6.7), the `record+0x10` tag is a registered emitter-volume class (§6.6), the effect's first float is its duration (§6.2), and a real exact-tiling gate passes 3/3 (§6.11). Two §5 statements were **wrong** and are corrected in §6.2 (the parameter block is *not* scalar-only below `+0x480` — it holds 41 fixed-up pointers). Everything the consumer could not settle is listed in §6.12.

**UPDATE 3 — Q-block/filter-class/sibling-array pass (see §7, Agent AL, 2026-09-23):** unblocked by Team B's full 1,812-entry `.cefct_pc` enumeration, which located real files where `root+0x40/0x48`, `root+0x70/0x78` and `root+0x90/0x98` (all count-0 in every sample §6 had) are actually populated. Ten new real samples decoded 208/13/62 elements of those three arrays (§7.4–§7.6); the six VFX-Filter classes' behaviour was decoded from disassembly — five are per-particle force/decay modifiers, one is an orientation/transform filter (§7.3); and `Q+0x60`'s lazy-rebase mechanism is now exact, though `Q`'s six sub-blobs' interior content remains OPEN via a scoped negative (§7.2). §6.12 item 4's "no populated element in any sample" is resolved; item 1's `P+0x08/0x0C → particle+0x124` gap has its first confirmed consumer.

**Confidence key** (as in prior specs): **CONFIRMED — empirical**, **HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**.

---

## 1. Overview

A `.cefct_pc` file is the compiled/shipped form of an authoring-tool **`.effectx`** source file — every sample examined embeds its own original source filename verbatim (e.g. `vfx_shockwave_kill_all.effectx`), matching the same "compiled-from-an-editable-source" convention already seen elsewhere in this project (`.xtbl`'s tool-facing `TableDescription`, `.cvtf_pc`'s likely relationship to an editable `_cust.xtbl`). **[CONFIRMED — empirical, all 3 samples.]** The file defines a **particle/visual effect made of several independently-named sub-objects** — the closest analogy in this project's own vocabulary is the emitter/layer structure of a modern particle-effect editor: multiple named elements (see §3), each presumably carrying its own timing/parameter data, sharing one effect-level texture reference list.

## 2. Header

Begins with the same shared "material reference block" already documented in `spec-geometry-format.md` §3.1 and `spec-texture-format.md`'s `.matlib_pc` appendix — magic `0x00043854`, a name-table-length field, a zero field, a texture-slot count, then that many null-terminated texture names (3 textures in two samples, 1 in the third — e.g. `vfx_corona_white.tga`, `vfx_smoke_anim_v01.tga`, `vfx_shockwave_01.tga`). **[CONFIRMED — empirical, reused structure, third confirmation of this shared block per `spec-geometry-format.md` §3.1.]**

~~Immediately after this block:~~ **[Corrected 2026-09-20, §5.2 retraction: the source filename is not stored after this block; it is a separate string reached through the pointer at `root+0x18`.]** The effect's own source filename is a null-terminated string, e.g. `vfx_shockwave_kill_all.effectx` (always `<same base name as the container entry>.effectx`). **[CONFIRMED — empirical, 3/3 samples.]**

~~A short distance after that (the gap varies — 11 to 19 bytes across the three samples, not a fixed constant, so there is at least one more small field in between not yet characterized)~~ — **CORRECTED, disassembly, §5.2:** there is no extra field; ~~the material block's own "name-table-length" field (read by the shared validator `FUN_00dd7d70`) spans the texture names *and* the trailing `.effectx` filename as one continuous run of strings, and the small residual gap is exactly the 16-byte alignment padding on that combined run~~ **[RETRACTED 2026-09-20, §5.2: the name-table length covers the texture names only; the gap is the 16-byte alignment pad of the texture-name run]** (0–15 bytes, confirmed 10/12/14 bytes on the 3 samples — a subset of that range, not a separate mystery field). Right after that padding, a **fixed 4-byte marker, value `0x57423137`** (on-disk bytes read as the literal ASCII text `71BW`) appears at the same relative structural position in every sample. **[CONFIRMED — empirical, exact byte-for-byte match, 3/3 samples. *Exact literal required by the format.*]** Immediately following the marker: ~~one small value that differs slightly per sample (43 or 44 in the three samples checked — too narrow a range to characterize confidently)~~ — **CORRECTED, disassembly, §5.3:** a format/schema **version** field, validated by `FUN_0043e420`'s entry guard to the exact range `[0x2a, 0x2c]` = **42–44** (43/44 are just what these 3 samples happen to use; a 4th, older schema value 42 is accepted by the code but not observed in any sample); then ~~two zero dwords~~ — **CORRECTED, disassembly, §5.3:** only the *second* dword is reliably zero — the first is a small flag/count (observed `0` or `1` across the 3 samples) that gates one optional header pointer field, not itself always zero; then a **size field which is exactly `(total file size) − (the 71BW marker's own file offset)`** — confirmed by exact arithmetic on all three samples (e.g. 12,480 − 112 = 12,368, matching the field's real stored value exactly). **[CONFIRMED — empirical, exact match, 3/3 samples.]** ~~Two 32-bit floating-point values immediately follow that size field~~ — **CORRECTED, disassembly, §5.3:** the two floats actually sit *earlier* in the header (marker+0x20/+0x24) than the size field (marker+0x38), not after it — the size field is itself just another instance of the header's own generic offset-fixup convention (a stored relative offset that resolves to exactly one-past-the-end of the file, not a bespoke size value read directly). Restated in file order — small, positive, plausible-looking effect-level parameters (values observed: 1.67/2.17, 3.33/1.00, 5.00/1.01 across the three samples) — **[HIGH CONFIDENCE — inferred to be real tunable parameters from their sane float ranges; specific meaning (duration? scale? intensity?) not determined.]**

A second short marker/tag, `MCKH` (plain ASCII, not further decoded), appears a short distance further in on two of the three samples — **[CONFIRMED present on 2/3 samples; not located in the third, possibly present at a position this pass's string scan didn't cover, or genuinely absent — not resolved.]** **UPDATE, disassembly, §5.6:** now precisely located and structured — `MCKH` is the first 4 bytes of an 8-field header (`"MCKH"`, `u32=7`, `u32=0x20`, 8 zero bytes) sitting at the address the header's (empty-in-all-3-samples) material-reference sub-array pointer resolves to; present exactly where `shockwave`/`whored` place that pointer, genuinely absent in `fireworks` because that file's own pointer resolves elsewhere. No function reading this block was found within the traced `.cefct_pc` parse chain itself — still open which code (if any) consumes it.

## 3. Named sub-objects

The bulk of each file (everything after the header material described above) is organized around a small number of **named sub-objects**, whose names appear as ordinary readable strings scattered through the binary data — these are almost certainly emitter/layer/filter names authored in the original `.effectx` source, baked into the compiled file for lookup/binding purposes (e.g. so game code can reference "the sparks layer" by name). Real names observed, by sample:

| Sample | Named sub-objects found |
|---|---|
| `vfx_shockwave_kill_all.cefct_pc` | `FX_BlastWave01`, `FX_BlastWave_flat`, `FX_BlastWave_flat01`, `Object01`, `VFX Filter01` |
| `vfx_whored_invulnerable.cefct_pc` | `Object01`, `Object02`, `VFX Filter01` |
| `vfx_runningman_fireworks.cefct_pc` | `FX_Sparks`, `FX_Sparks01`, `red_Ribbon`, `red_Ribbon01` |

**[CONFIRMED — empirical, direct string content, all names transcribed exactly as found.]** **UPDATE, disassembly, §5.4:** this table under-counted — the informal string scan missed real sub-object names that the structured array (§5.4) makes explicit: `p_vel_sparks_01` in `vfx_shockwave_kill_all.cefct_pc`, and `VFX_Fireworks_Spawn01`/`VFX_Fireworks_Spawn` in `vfx_runningman_fireworks.cefct_pc` — see §5.4 for the authoritative, disassembly-derived complete lists (also note `VFX Filter01` lives in a separate sibling array, §5.5, not the same array as the rest of this table's names). A consistent naming convention is visible: a base name (`Object`, `FX_Sparks`, `red_Ribbon`, `FX_BlastWave_flat`) frequently appears both bare and with a `01`/`02` numeric suffix — consistent with an authoring tool that auto-numbers duplicated/layered instances of the same effect element. **[HIGH CONFIDENCE — inferred from the consistent pattern across all three samples.]**

Every sample's three (or one) shared texture names (§2) **appear a second time**, near the very end of the file — ~~plausibly per-object texture bindings (each named sub-object selecting one of the effect-level textures) rather than a second, independent reference~~. **[CONFIRMED — empirical, the repeated occurrence.]** **CORRECTED, disassembly, §5.3:** the "why" hypothesis was wrong in the specific mechanism — this is a flat, **effect-level** second list (its own count/array field pair on the file's root header, structurally independent of and not nested inside the named-sub-object array), not a per-object binding table. The named sub-object records (§5.4) do not themselves carry a resolved texture reference in any field this pass decoded.

## 4. What's genuinely open

This pass deliberately did not attempt to fully decode the per-object binary layout — for a 5–14 KB file where only a few hundred bytes of header/marker structure are pinned down, the great majority of each file (the actual per-object parameter/keyframe/timeline data presumably driving particle count, spawn rate, color-over-time curves, etc.) remains **unstructured from this pass's black-box inspection.** Scattered short "readable-looking" byte runs found during string-scanning (e.g. repeated 4-byte sequences like `F(6>` appearing three times in a row in one sample, or `x>0b` appearing many times in another) are almost certainly **coincidentally-printable fragments of packed binary/float data, not real text** — flagged explicitly so a future pass doesn't mistake them for meaningful strings.

**Open items:**
1. ~~The per-sub-object record layout — where each named object's own data starts/ends, and what parameters it carries. Not attempted; would likely need disassembly of the effect-loading/playback code (the same kind of investment `.gcmesh_pc`'s vertex-attribute region needs, per `spec-geometry-format.md` §6) rather than more black-box guessing.~~ **RESOLVED (record boundaries/shape), disassembly, §5.4** — a fixed 0x58-byte struct in an explicit count+array pair, not length-prefixed. The record's *parameter payload* (nested block, §5.4) is only partially decoded; see §5.7/§5.8 for what's still open there.
2. ~~The `MCKH` marker's meaning, and why it wasn't found in the third sample.~~ **ADVANCED (location/structure/absence explained), disassembly, §5.6** — exact byte structure and why it's absent in `fireworks` are now known; which code (if any) reads it is still open.
3. **PARTIALLY ADVANCED (exact location), disassembly, §5.3** — precisely located at marker+0x20/+0x24; ~~the two effect-level float parameters' exact *meaning* (duration? scale? intensity?) is still open — no consuming code traced this pass.~~ **UPDATE, §6.2 item 2: float #1 (`root+0x20`) is the effect DURATION in seconds — RESOLVED (consumer + 13/13 empirical); float #2 (`root+0x24`) still OPEN.**
4. ~~The small (43/44) value immediately after the `71BW` marker.~~ **RESOLVED, disassembly, §5.3** — a format/schema version field, validated range 42–44, gates one additional optional header field (present only at version 44).
5. Only 3 samples were checked, all fairly small (5–14 KB); larger, more complex effects were not sampled and might reveal additional structure ~~(e.g. a real per-object count field, which wasn't identified from these samples)~~ — **RESOLVED, disassembly, §5.4:** a real per-object count field does exist (marker+0x60); the general point stands though — this pass also found a much larger (552-byte-per-record) sibling array in the code (§5.6) that is completely unused (count 0) in all 3 samples, so a bigger/more complex effect file is exactly the kind of sample that could exercise it and reveal more structure — not visible at this scale.

None of these gaps prevent identifying an effect file's source name, its referenced textures, or its named sub-object list from a `.cefct_pc` file — which is real, useful, confirmed structure — but reconstructing or re-emitting the actual visual effect (timing, particle behavior, colors) is not possible from what's documented here.

## 5. Disassembly follow-on: the named sub-object record layout

### 5.1 Method and starting point

Starting point: `FUN_005cee40` (the `.cefct_pc` constructor, registered for resource type IDs 4/"Vehicle VFX" and 15/"VFX", destructor `LAB_005ceef0`/`FUN_005ceef0`), per `spec-format-inventory.md`'s registration table. Followed forward through real calls only (no whole-binary search, per this project's standing methodology, `HANDOFF.md` §5): `FUN_005cee40` → `FUN_00dd7d70` (shared material-block validator) and `FUN_0043e230` → `FUN_0043e1c0` → `FUN_0043e420` (the real ~6,000-byte body parser — the great majority of this pass's findings come from this one function). All Ghidra work done headless (`-noanalysis`, project copy `tools/gp_fx1`) via one-off scripts in `tools/scripts/` (`DecompileCefctCtor.java`, `DecompileCefctHeader.java`, `DecompileCefctChain.java`).

Every offset/field claimed below was **independently re-verified against real file bytes**, not just read off the decompiler — the exact fixup arithmetic the disassembly describes was re-implemented from scratch in Python (`tools/harnesses/cefct_probe.py`) and run against the same 3 samples the original black-box pass used, re-extracted fresh via `tools/harnesses/extract_cefct_samples.py`'s `collect()` (same technique as `vehgeo_bulk.py`, per this project's standard tooling) from `sr3_city_missions.vpp_pc`'s `Horde Mode`/`Running Man` `.str2_pc` packages (per `spec-mission-packages.md` §3) — sizes matched the black-box pass's own figures exactly (12,480 / 5,264 / 14,416 bytes), confirming these are the same files. **[CONFIRMED — disassembly + independent byte-level replay, 3/3 samples throughout this section unless noted otherwise.]**

### 5.2 The constructor chain confirms the black-box header, and pins down the material-block/filename/71BW handoff exactly

`FUN_005cee40` is a thin dispatcher: it looks up an existing resource slot (`FUN_005cecd0`), and if none exists, calls `FUN_00dd7d70(buffer_ptr, &pos, 0)` — the same shared material-reference-block validator already named in `HANDOFF.md` §27.6 and used by `.matlib_pc`/`.ccmesh_pc`/`.czh_pc`/`.clmesh_pc` (6+ confirmed uses project-wide; a prior agent had already left an explanatory comment on this function in the shared Ghidra project, found in place). Disassembly of `FUN_00dd7d70` itself shows exactly what it does: validate the magic as two `u16` halves (`==0x3854` then `==4`, i.e. `0x00043854` read low/high), then advance the cursor by a fixed `0x20`-byte header, then by `(name_table_len read from block+4) + 1` bytes, then align to 16.

Replaying this exact arithmetic in Python against the 3 real files' raw bytes (no Ghidra needed for this part, pure byte math) lands **exactly** on the `71BW` marker's real file offset in all 3 samples:

| Sample | `name_table_len`@block+4 | computed pos (`0x20+len+1`, aligned 16) | literal `71BW` offset | match |
|---|---|---|---|---|
| `vfx_shockwave_kill_all.cefct_pc` | 65 | 112 | 112 | exact |
| `vfx_whored_invulnerable.cefct_pc` | 21 | 64 | 64 | exact |
| `vfx_runningman_fireworks.cefct_pc` | 67 | 112 | 112 | exact |

**[CONFIRMED — disassembly + exact byte-level replay, 3/3.]** ~~This proves the material block's own "name-table-length" field spans **both** the texture name(s) **and** the trailing `.effectx` source filename as one continuous run of null-terminated strings~~ **RETRACTED 2026-09-20 (found by Team B's independent reader over 1,812 entries; re-derived by the orchestrator on the three reliable samples): the arithmetic above lands on the `71BW` marker only because of the 16-byte alignment, not because the source filename is inside the run. The name-table length at block+4 equals the sum of `strlen+1` over the TEXTURE names ONLY (65/65, 67/67, 21/21 on the three samples; Team B: 1,812/1,812), and the `.effectx` source name is NOT in the material block: it is a separate string reached through the pointer at `root+0x18` (`root + value` resolves to `vfx_shockwave_kill_all.effectx` / `vfx_whored_invulnerable.effectx` / `vfx_runningman_fireworks.effectx`, 3/3; Team B 1,812/1,812).** Consequences: §2's "gap" is 16-byte alignment padding of the texture-name run, exactly as §5.2's table computes (`0x20 + len + 1`, aligned 16) — that table's numbers are right; only the interpretation of what the run contains was wrong. *(Also reported by Team B, not re-derived here because it needs their full population: `root+0x08` takes values 0–16, not only 0/1 (the three samples here show 0, 1, 0); the strict end-pointer gate `root+0x38 == filesize − root` fails for 344 of 1,812 entries — all with `root+0x40 > 0` — whose trailing bytes are a 2-aligned NUL-terminated texture-name string table (344/344); `record+0x10` tags `0xA1CC7F44` ×502, `0x4A8FB92D` ×80 and the filter tag `0x00AB1EC0` ×44 (a real tag value, not an address as §6.6 guessed), and `P+0x02` type 5 ×712 which §6 does not list.)* ~~(not two separately-tracked regions as the black-box pass's informal read implied) — resolving §2's "gap" note.~~ *(orphaned fragment of the retracted sentence above.)* It also confirms block+8 (black-box's "zero field") is real disk-zero, and block+0xC = the texture-slot count (3/3/1, exact match to black-box's own count).

### 5.3 The `71BW`-relative header, field by field

All offsets below are relative to the `71BW` marker's own file offset ("**root**" — the marker's absolute file offset is `112`/`64`/`112` for shockwave/whored/fireworks respectively). Every fixed-up field on disk is the sentinel `-1`/`0xFFFFFFFF` (null) or a byte offset **relative to root**; `FUN_0043e420` resolves each one the same way throughout (`base = root`). This exact per-field walk was implemented and independently re-run in `tools/harnesses/cefct_probe.py`.

| root+ | Field | Confirmed content |
|---|---|---|
| `0x00` | magic | `71BW` (`0x57423137`) — already CONFIRMED by black-box, re-confirmed |
| `0x04` | version | u32, validated range `[0x2a,0x2c]` = **42–44** by `FUN_0043e420`'s entry guard (values seen: 43, 43, 44) **[CONFIRMED — disassembly, exact guard code, 3/3]** |
| `0x08` | flag/count | i32, observed `0` or `1`; `>0` gates the optional field at `+0x10` **[CONFIRMED — disassembly + byte match, 3/3]** |
| `0x0c` | zero | always `0` on disk, 3/3 **[CONFIRMED — empirical]** |
| `0x10` | optional pointer | only fixed up/meaningful when `+0x08 > 0` (only shockwave, `flagB=1`); content not decoded — **OPEN** |
| `0x18` | pointer | unconditionally fixed up in all 3 samples; resolves to a small in-header offset (`0x110`/`0xe0`/`0x120`); ~~content not decoded — **OPEN**~~ **RESOLVED (§5.2 retraction): points to the `.effectx` source-name string, 3/3; Team B 1,812/1,812** |
| `0x20` | float #1 | matches black-box's already-reported values exactly (1.6669 / 3.3335 / 5.0002) **[CONFIRMED — location now pinned exactly; ~~meaning still OPEN~~ — RESOLVED, §6.2 item 2: the effect duration in seconds]** |
| `0x24` | float #2 | (2.1724 / 1.0000 / 1.0094) — same status |
| `0x28`/`0x30` | count / array, stride 8 | the effect-level texture-name list's **second occurrence** (§3) — `{string_ptr, 0}` pairs, count == the material header's own texture-slot count (3/1/3), strings dereferenced and confirmed byte-identical to the material-block names **[CONFIRMED — disassembly + string dereference, 3/3]**. A dormant per-string normalization (`if last char == 'n', rewrite to 'a'`) exists in the code but never triggers on real texture names (none end in `n`) — **[CONFIRMED present in code; CONFIRMED never exercised in these 3 samples]** |
| `0x38` | pointer | resolves to exactly `root + (filesize − root)` = one-past-the-end-of-file, i.e. this is the mechanism behind black-box's already-published "size field" — same exact arithmetic, now explained: it's the header's ordinary offset-fixup convention applied to a field whose stored value happens to be the remaining-bytes count, not a bespoke size field **[CONFIRMED — disassembly + exact match, 3/3]** |
| `0x40`/`0x48` | count / array, stride `0x88` (136B) | a **material-reference sub-array** — each element's fields are resolved via the same shared material-resolution helpers used by `.clmesh_pc`/tree/vehicle formats (`FUN_00e718b0`, `FUN_00e401d0`, `FUN_00e3ffd0`, `FUN_00e71090`, `FUN_00e3fcb0` — all already named in `HANDOFF.md` §27.6). **Count is `0` in all 3 real samples — present in code, unexercised by this data.** See §5.6. |
| `0x50`/`0x58` | pointer pair | used only as an optional cross-check against an external registered callback (`DAT_03171a44`, a global function pointer, observed NULL-effective in practice) validating a name-table entry count against `+0x40`'s count; inert since `+0x40`'s count is 0 in all samples — **OPEN** |
| `0x60`/`0x68` | **count / array, stride `0x58` (88B)** | **the named sub-object array — see §5.4, the core finding of this pass** |
| `0x80`/`0x88` | count / array, stride `0x70` (112B) | the sibling **VFX Filter** array — see §5.5 |
| `0x90`/`0x98` | count / array, stride `0x228` (552B) | a much larger per-element structure (~24 further pointer sub-fields). **Count is `0` in all 3 real samples** — see §5.6 |
| `0xa8` | optional pointer | only fixed up when version `> 0x2b` (i.e. version 44) — confirmed real: `fireworks` (version 44) holds a plausible in-range offset there (`0x3750`=14160, valid for a 14,416-byte file); the two version-43 samples hold unrelated leftover bytes at that offset (not even zeroed) — **[CONFIRMED — disassembly + cross-sample comparison that the version gate is functionally real, 3/3]**. Content beyond "it's a valid pointer" not decoded — **OPEN** |

### 5.4 The named sub-object array — record shape, and what's confirmed inside each record

**This directly answers this document's main open item.** Count field: `root+0x60` (u32). Array: `root+0x68` resolves to a run of exactly that many **fixed-size 0x58-byte (88-byte) records** — **not** length-prefixed, **not** discovered by string-scanning at runtime; the count is an explicit on-disk field. **[CONFIRMED — disassembly (the fixup loop's own `while (i < count)` bound) + exact string dereference of every record in all 3 samples, cross-checked against the black-box pass's own name list.]**

Dereferencing `record+0x00` (a pointer, same fixup convention as every other field) gives each record's own **name string** — confirmed for all 13 records across the 3 samples, and this is *more complete* than the original black-box string scan (which missed 3 real names — see §3's update):

| Sample | Records (name @ +0x00) |
|---|---|
| `vfx_shockwave_kill_all.cefct_pc` (5) | `FX_BlastWave01`, `FX_BlastWave_flat`, `FX_BlastWave_flat01`, `Object01`, `p_vel_sparks_01` |
| `vfx_whored_invulnerable.cefct_pc` (2) | `Object02`, `Object01` |
| `vfx_runningman_fireworks.cefct_pc` (6) | `FX_Sparks`, `red_Ribbon`, `FX_Sparks01`, `red_Ribbon01`, `VFX_Fireworks_Spawn01`, `VFX_Fireworks_Spawn` |

Per-record field map (offsets relative to the record's own base), everything below **[CONFIRMED — disassembly, the exact fixup code for this array]** unless noted:

| record+ | Field | Confirmed content |
|---|---|---|
| `0x00` | name pointer | null-terminated ASCII name (table above) |
| `0x08` | pointer (same fixup convention as `+0x00`) | raw on-disk value is `-1` (null) in **all 13 records, 3/3 samples** — present in the format, unpopulated in this data — **ADVANCED, §6.5: it is a material-link *name* string, matched case-insensitively against the `+0x00` names of the `root+0x48` elements by the instantiate routine** |
| `0x0c` | zero | companion of `+0x08`, always 0 |
| `0x10` | u32, not fixed up as a pointer | a value that repeats across records within a file and clusters distinctly — see below, **type-tag hypothesis** |
| `0x14`–`0x17` | small integer/byte-level values | vary per record (e.g. `01 00 00 01` vs `00 00 00 00`); ~~no clean single-scalar reading found — **OPEN, not decoded**~~ **RESOLVED, §6.5: four booleans that become bits `0x20`/`0x40`/`0x80`/`0x100` of the `Q+0x50` flags word at instantiate (what each bit does is still OPEN)** |
| `0x18` | u32 | **always `-1` (0xFFFFFFFF) in all 13 records, 3/3 samples** — reliably present-but-null in this data, not touched by any fixup in the traced code — **OPEN** |
| `0x1c`–`0x27` | zero | always 0 in all 13 records |
| `0x28` | pointer | resolves to a **large nested per-record parameter block** — see below — **DECODED, §6.7 (`0x4A8` bytes, 41 fixed-up pointer slots)** |
| `0x2c` | zero | companion of `+0x28` |
| `0x30` | pointer | resolves to a second block; leading bytes show a recurring-looking 4-byte hash value across several records and small offsets further in — ~~not decoded, **OPEN**~~ **PARTIALLY DECODED, §6.9: a `0xD8`-byte render-state block (flags word at `+0x50`, lazily rebased pointer at `+0x60`, six sub-blob pointers); interior semantics still OPEN** |
| `0x34` | zero | companion of `+0x30` |
| `0x38`–`0x57` | (32 bytes) | **always all-zero in all 13 records, 3/3 samples** — reliably zero in this data, ~~semantically unexplained~~ **EXPLAINED, §6.5: runtime scratch (`+0x38` is the "is a child" flag written by the instantiate pass; `+0x3C..+0x57` untouched)** |

**`record+0x10`, HIGH CONFIDENCE — inferred type/category tag:** this 4-byte value is *not* touched by any pointer-fixup logic (stays as its raw on-disk value) and clusters into a small number of distinct constants shared by multiple records in the same file — e.g. in `fireworks`, 5 of 6 records share `0xec276f11` and 1 (`VFX_Fireworks_Spawn01`) has `0x1d815e4a`; in `shockwave`, the 3 `FX_BlastWave*` records share `0xec276f11` while `Object01`/`p_vel_sparks_01` share a distinct `0xfaa8d302`; in `whored`, both records share `0xec276f11`. `0xec276f11` is the dominant value (10/13 records across all 3 files) with 2 other values appearing only in `shockwave` and `fireworks` respectively. This is consistent with a small enum of sub-object "kinds" (plausibly a hash of an authoring-tool type name, e.g. an emitter-class identifier) but **no lookup table or hash algorithm was traced, and the split does not cleanly follow the name-suffix pattern one might guess (e.g. `VFX_Fireworks_Spawn` and `VFX_Fireworks_Spawn01` do *not* share a value)** — genuinely a partial, honestly-labeled inference, not a confirmed decode. **[HIGH CONFIDENCE — inferred, not CONFIRMED.]** **UPDATE, §6.6: CONFIRMED — the value is a class id looked up in a registry (`VA 0x01352200`); the instantiate routine `0x0043fd50` compares it with each row's tag and calls the row's create-function. Four volume classes: point `0xEC276F11`, cube `0x1D815E4A`, box `0xB3269ACC`, column `0xFAA8D302`. The class NAMES (hash inputs) are not recoverable from the binary's strings — still OPEN.**

**`record+0x28`'s nested block — the actual per-object parameter data, only lightly explored:** the disassembly shows the *first* field this code path touches inside this nested block is at **nested+0x480** (1,152 bytes in) — ~~meaning everything from nested+0x00 through nested+0x47F is plain scalar data never treated as a pointer by this code, and is entirely undecoded.~~ **CORRECTED, §6.2 item 1: `+0x480` was merely the first pointer in *code* order; the loader fixes up 41 pointer slots (`+0x1E0`, `+0x1F0`, and every `0x10` from `+0x220` to `+0x480`), and the block is `0x4A8` bytes. It is now field-mapped in §6.7.** Past `+0x480`, the disassembly continues fixing up **dozens more pointer-typed sub-fields** (at least ~30, mostly spaced `0x10` apart, continuing past `+0x400` relative to where this pass stopped reading) — the shape (many same-size, evenly-spaced pointer slots) is consistent with a set of independent per-property curve/track arrays (the black-box pass's own guess: "color-over-time curves, timing"), but **no individual field was identified or semantically decoded this pass.** Byte-level inspection of the small leading region that *is* read directly (nested+0x00..+0x0f) shows real, non-garbage structure — a pair of small u16 values that vary in a pattern loosely tracking the record's base name/group (e.g. `FX_Sparks`/`FX_Sparks01` both `(0,0)`, `red_Ribbon`/`red_Ribbon01` both `(0,2)` in `fireworks`) followed by a `float32` = **exactly `30.0`**, byte-identical across all 13 records in all 3 samples. **[CONFIRMED — empirical, exact byte match, 13/13 records — a real, consistent field; ~~semantic meaning (a shared default rate/duration/fps? unconfirmed) is OPEN, not guessed further.~~ **RESOLVED, §6.2 item 3: a CPU sub-step rate in Hz (sub-steps per frame = `int(30·dt + 0.5)`, min 1), used only when `P+0x00 != 0`.**]**

This nested block is genuinely large and only lightly explored — full closure was not attempted this pass, consistent with this project's standing discipline not to force a decode once confirmable facts run out (`HANDOFF.md` §27.8). ~~It is the natural next target for a dedicated follow-on.~~ **Done — see §6 (Agent AB).**

### 5.5 The sibling "VFX Filter" array

A **second, structurally separate** named-record array exists at `root+0x80` (count) / `root+0x88` (array), stride `0x70` (112 bytes) — confirmed by direct string dereference of `record+0x00` in both samples that have it:

| Sample | count | Records |
|---|---|---|
| `vfx_shockwave_kill_all.cefct_pc` | 1 | `VFX Filter01` |
| `vfx_whored_invulnerable.cefct_pc` | 1 | `VFX Filter01` |
| `vfx_runningman_fireworks.cefct_pc` | 0 | *(none — matches: this sample has no `VFX Filter*` name)* |

**[CONFIRMED — disassembly + exact string dereference, 3/3 samples, including the negative case.]** This resolves the original black-box table's placement of `VFX Filter01` alongside the other named objects — it is not part of the `root+0x60` object array at all, it lives in this separate sibling structure. Internal record layout beyond the name pointer at `+0x00` was not explored this pass — **OPEN**.

### 5.6 Structures present in the format but unexercised by these 3 samples

Two more count/array pairs exist in `FUN_0043e420`'s field map and are fully wired up in the fixup code, but their **count is `0` in all 3 real samples**, so nothing about their per-element content could be observed this pass:

- **`root+0x40`/`root+0x48`, stride `0x88` (136B)** — the material-reference sub-array (§5.3). When its count is 0, its (still-valid, still-fixed-up) array pointer simply lands wherever the next real data happens to be — in `shockwave`/`whored` that's exactly the `MCKH` 8-field mini-header (`"MCKH"`, `u32=7`, `u32=0x20`, 8 zero bytes — **[CONFIRMED — empirical, exact byte match, 2/3 samples]**); in `fireworks` the pointer resolves somewhere else entirely and no `MCKH` text appears anywhere in that file, which is exactly what the original black-box pass observed (2/3) without being able to explain why. **No function that reads `MCKH` was found within the `.cefct_pc` parse chain traced this pass** — it may belong to a different, not-yet-traced consumer (plausibly the material-resolution subsystem itself, given its positional coincidence with the material array) — **OPEN**.
- **`root+0x90`/`root+0x98`, stride `0x228` (552B)** — a much larger per-element structure than either of the two arrays above, with roughly two dozen further pointer sub-fields per element (spaced mostly `0x10` apart). Entirely unused (count 0) in all 3 samples. This is a strong, concrete instance of §4 item 5's own prediction that larger/more complex effect files might reveal structure invisible at this sample size — **OPEN, needs a larger real sample to observe even one populated element.** **[Resolved: populated samples decoded, §7.6.]**

### 5.7 Summary against this document's original open items (§4)

- Item 1 (per-sub-object record layout) — **RESOLVED for record boundaries/shape** (fixed 0x58-byte struct, explicit count field, not length-prefixed, not scan-discovered); **partially decoded for contents** (name, a likely type-tag, a large under-explored nested parameter block).
- Item 2 (`MCKH`) — **ADVANCED**: exact structure and exact reason for 2/3-sample presence now known; consuming code still open.
- Item 3 (two floats' meaning) — **location resolved**, meaning still open. **[Update: float #1 = duration, §6.2 item 2; float #2 still OPEN.]**
- Item 4 (43/44 value) — **RESOLVED**: version field, range 42–44, gates a real optional field.
- Item 5 (larger samples might show more) — **directly evidenced**: a fully-wired, entirely-unused-in-samples 552-byte-per-record array exists in the code.

### 5.8 What's still genuinely open after this pass

1. ~~The `record+0x28` nested per-object parameter block (§5.4) — by far the largest remaining gap: ~1,152+ bytes of leading scalar data plus ~30 further pointer sub-fields per record, none semantically decoded. This is where particle count/spawn rate/color-curve/timing data, if present per-object, would actually live.~~ **DONE 2026-09-20 (Agent AB), §6.7: field-mapped from the consumer — sim mode, emitter type, capacity (`P+0x1D8`), lifetime, cull distance, acceleration, the colour gradient (C0..C3 + key times), three four-key size tracks with variance, emission-rate/speed/angular-speed curves, child lists; all 41 pointer slots classified; exact tiling gate passes 3/3 (§6.11). Renderer-side meanings of a handful of fields remain OPEN (§6.12 item 1).**
2. ~~`record+0x30`'s block (§5.4) — a second per-record pointer target, not explored at all.~~ ~~**PARTIALLY DONE, §6.9: a `0xD8`-byte render-state block (flags word, lazily rebased pointer, six sub-blobs); its interior semantics are still OPEN.**~~ **FURTHER ADVANCED, §7.2 (Agent AL, 2026-09-23):** the `Q+0x60` lazy-pointer rebase rule is now exact (raw `0`/`0xFFFFFFFF`/offset), and its only found consumer (`0x00e7faf0`) is a generic setter, not a reader — the six sub-blobs' *interior content* is still OPEN, now via a scoped negative (not dereferenced by any of `0x0043fd50`/`0x00e7faf0`/`0x00436760`/`0x00437fc0`/`0x00430e60`/`0x004309c0`/`0x00433c10`).
3. ~~The `record+0x10` type-tag hypothesis is HIGH CONFIDENCE, not CONFIRMED — no hash algorithm or lookup table traced.~~ **RESOLVED for the mapping, §6.6: the lookup table is the class registry at `VA 0x01352200`; the hash inputs (type names) could not be recovered (0 hits over every executable string, five hash variants) — still OPEN as names.**
4. `MCKH`'s consuming code, and the two effect-level floats' semantic meaning — unchanged from before, still open. **UPDATE §6.2: float #1 (`root+0x20`) = effect duration (RESOLVED); `MCKH`'s reader and float #2 (`root+0x24`) are still OPEN.**
5. The two unused-in-samples arrays (§5.6) — need a larger/more complex real sample to observe a populated element of either. **[Resolved: populated samples decoded, §7.4–§7.6.]**
6. `record+0x08`, `record+0x18`, and `record+0x14..0x17` (§5.4) — fields with confirmed but unexplained content (always null / always -1 / small varying bytes respectively). **UPDATE §6.5: `+0x08` (material-link name) and `+0x14..0x17` (four booleans → `Q+0x50` flag bits) are RESOLVED; `+0x18` is still OPEN (not read by the three instantiate functions).**

As before: none of this prevents reliably identifying a `.cefct_pc` file's source name, its textures, or its complete, authoritative named-sub-object list (now via a real structural field, not string-scanning) — but full particle/timeline reconstruction remains out of reach from what's confirmed here.

## 6. Consumer-side decode: the per-record parameter blocks, the class registry, and the runtime that reads them (Agent AB)

**Prepared by:** SPEC TEAM, Agent AB (`gp_fx2`), 2026-09-20. **Scope:** the CONSUMER side of a loaded `.cefct_pc` — the runtime code that reads the records to spawn and update particles — resolving §5.8 items 1–2 (the `record+0x28` parameter block, the `record+0x30` block) and advancing several §5.3/§5.4 "OPEN" cells. **Method:** Ghidra 12.1.3 headless on a disposable project copy (`tools/gp_fx2`), one-off scripts `tools/scripts/Ab*.java`; every field role below was read off the code that consumes it, then validated against the 3 real samples by `tools/harnesses/cefct_params.py` (a from-scratch decoder built on `cefct_probe.py`). Labels: CONFIRMED — disassembly / CONFIRMED — empirical / HIGH CONFIDENCE — inferred / HYPOTHESIS — unconfirmed / OPEN. Cleanroom convention: addresses, offsets, sizes and quoted UI literals only; no decompiled code and no original identifiers.

### 6.1 How the consumer was found, and the anchor table

The loader (§5.1) ends by calling `FUN_00440270`, an *instantiate-the-resource* pass that the previous pass did not follow (it is called from two sites inside `FUN_0043e420`, at `0x0043fb93`/`0x0043fbac`). Independently, the two tag literals that §5.8 item 3 suggested as selective anchors were searched as 32-bit scalar operands over the whole instruction stream: `0xfaa8d302` and `0x1d815e4a` occur **once each, both inside `FUN_005ccb90`** (`0x005cd514`, `0x005cd5ed`); `0xec276f11` never occurs as an instruction operand (only as data, below). `FUN_005ccb90` is a developer **debug overlay** that prints the live emitter's parameters with human-readable labels — the decisive Rosetta stone for this format (§6.7). **[CONFIRMED — disassembly.]**

| Address | Role | Confidence |
|---|---|---|
| `0x0043e310` / `0x0043e1c0` / `0x0043e230` | pool-allocate the resource object (vtable `0x0129E564`, class-name string `vfx_particle_system_filter_instance` sits immediately after the vtable), check the `71BW` marker, call the body parser, then register the object in a global array (`DAT_03171a14`+`0x2c`/`+0x34`); object `+0x0C` = `+0x10` = **root** | CONFIRMED |
| `0x0043e420` tail → `0x00440270` | after all fix-ups: `0x00440220` (per-record instantiate, loops `root+0x60` times), `0x00440030` (link child-emitter lists), `0x00440130` (link element lists), then the filter array | CONFIRMED |
| `0x0043fd50` | **instantiate ONE record** (§6.5): finds the volume class by the record's tag, builds the runtime volume object ("K"), the render descriptor, and the record↔material link | CONFIRMED |
| `0x005ca5d0` | per-frame update of the world "Effect" entity (a `0x290`-byte object): looks up the resource by name through `FUN_005cef40`→`FUN_005cecd0` (the ctor's slot lookup, §5.2) and starts the instance with `FUN_005c7580` | CONFIRMED |
| `0x00430e60` (11,461 B) | **per-frame effect-instance update**: normalised time, GPU-parameter packing, filter/element lists; calls `0x00430400`/`0x004309c0` (per-emitter driver, spawner logic) which call `0x00436760` | CONFIRMED |
| `0x00436760` (6,234 B) | **emitter step**: emission accounting, per-particle integration, age-driven colour/size interpolation | CONFIRMED |
| `0x00437fc0` (8,484 B) | **particle spawn**: samples the curves, applies the random variances, writes the per-particle state | CONFIRMED |
| `0x004360c0`, `0x00436620`, `0x004361c0`, `0x004362c0`, `0x00435f50` | spawn accounting, rate/cull sample, expiry scan, emitter basis, emitter reset (`free capacity := P+0x1D8`) | CONFIRMED |
| `0x00434820` | **the curve evaluator** (§6.4) — 118 call sites | CONFIRMED |
| `0x0042e2f0`–`0x0042ed80`, `0x004356b0`–`0x00435940` | ~40 tiny per-curve getters, each fixing which `P+` slot it reads | CONFIRMED |
| `0x005ccb90` (5,047 B) | the debug overlay (labelled parameter list, string literals quoted in §6.7) | CONFIRMED |
| `VA 0x01352200`, stride `0x14` | the **class registry**: intrusive-list rows `{next, prev, tag, create-fn, init-fn}` (§6.6); list heads `DAT_031729bc` (volume classes, 4 rows) and `DAT_031729b4` (filter classes, 6 rows) | CONFIRMED |

Hash recovery, attempted and negative: none of the 71,008 printable strings (length ≥ 4) of the executable, raw or lower-cased, hashes to any registry tag or to the resource-class id `0xE17C8FA5` under CRC-32 (init 0, no final XOR — this project's standard string hash — and zlib CRC-32), FNV-1, FNV-1a or djb2. The tags are class ids whose source names are not in the binary as plain strings; **the type names stay unrecovered [OPEN]**, but the tags are fully identified by what the registered classes *do* (§6.6).

### 6.2 Corrections to §5 forced by the consumer

1. **§5.4 "nested+0x00..0x47F is plain scalar data never treated as a pointer" is WRONG.** The loader fixes up **41** pointer slots in every parameter block — at `+0x1E0`, `+0x1F0`, and every `0x10` from `+0x220` to `+0x480` inclusive (39 slots) — and `+0x290` only when the count at `+0x288` is positive. `+0x480` was merely the first one in *code* order, not the lowest offset. The block is **`0x4A8` bytes** (contiguous at that pitch, §6.11), of which the high dword of the last slot and the `0x20` bytes after it are zero and unread. **[CONFIRMED — disassembly (fix-up code, all 41 offsets extracted) + empirical (all 41 resolve inside the file, 16-aligned, 13/13 records).]**
2. **Root float #1 (`root+0x20`) is the effect DURATION in seconds** (§5.3 called it "meaning still OPEN"): the instance update divides elapsed time by it and clamps to `[0,1]` (`0x00430e60`, `0x00433c10`, `0x00436620` family), `0x0043fd50` copies it into every record's `P+0x204`, and `0x00440270` copies it into every filter instance. Empirical: the emission-rate curves' key times (§6.4) lie in `[0, duration]` in 13/13 records (control: the neighbouring float `root+0x24` would fail this in `vfx_runningman_fireworks`, key at 1.4 s > 1.0094). **[CONFIRMED — disassembly + empirical.]** `root+0x24` is not read by any function traced here — OPEN.
3. **`30.0` at `P+0x04`** (13/13) is a **CPU sub-step rate in Hz**: the emitter step, when `P+0x00 != 0`, splits each frame into `int(rate*dt + 0.5)` (minimum 1) sub-steps (constant `0.5` read from the binary). It is inert for emitters with `P+0x00 == 0`. **[CONFIRMED — disassembly for the arithmetic; HIGH CONFIDENCE that the unit is Hz.]**
4. **`record+0x10` is a class id in a registry, not just a "type tag"** (§5.4 HIGH CONFIDENCE → CONFIRMED mapping): `0x0043fd50` walks the volume-class list comparing it with each row's tag and calls that row's create-function (§6.6).
5. **§5.4's "unexplained" record cells are now explained** — see §6.5.
6. **The `root+0x40`/`0x48` "material-reference sub-array" is used at instance level** (§6.8), and there is one more count/array pair not listed in §5.3: **`root+0x70`/`root+0x78`** (stride `0xC8`, count `0` in 3/3 samples; consumer `0x00433c10`).

### 6.3 Object model (what the consumer builds from a loaded record)

```
resource object (vtable 0x0129E564)  --+0x0C--> root ("71BW" header)
  root+0x60/+0x68 : records[n]  (0x58 B each)                              (§5.4)
     record+0x28 -> P   (0x4A8-byte parameter block; runtime code reads it IN PLACE)
     record+0x30 -> Q   (0xD8-byte second block, §6.9)
     record+0x10  = class id -> registry row -> create-fn -> "K" volume object
                     K+4 = P,  K+8/+0x0C/+0x10 = size/rate/colour scale (default 1.0)
     record+0x20  <- K (runtime pointer written at instantiate; zero on disk)
  effect instance (per playing effect): elapsed seconds at +0x2C, resource at +0xDC,
     emitter list (+0xA4/+0xA8), each emitter: K at +0x70, live/free particle counts at +0xE4/+0xE8,
     accumulator +0xF0, effect time seen by the curves +0xF4, current rate +0xFC.
```
The runtime never copies P: curve pointers, the shape array and the child lists are read straight out of the file image, which is why the loader only rebases pointers. **[CONFIRMED — disassembly.]**

### 6.4 The property convention: keyed curves

Most of P is a run of **16-byte property slots** `{count : i32, pad, keys : ptr, pad}` — the count is 8 bytes *before* the pointer the loader rebases (so for a slot whose pointer is at `P+X+8` the count is at `P+X`; §6.7 tabulates by the *count* offset `C`, the pointer being at `C+8`). `keys` addresses `count` **8-byte `(time, value)` pairs** of `f32`, sorted by ascending time. The evaluator `0x00434820(out, count, keys, t)` does exactly: `count==1` → `keys[0].value`; `t <= keys[0].time` → first value; `t >= keys[count-1].time` → last value; otherwise binary search for the bracketing pair and **linear interpolation**. There is no other interpolation mode and no per-key tangent. **[CONFIRMED — disassembly (full function) + empirical (13/13 records × 36 curve slots `0x238`..`0x468`: counts 1–9, key times sorted in every slot).]**

**The time axis of a curve is the effect's elapsed time in SECONDS** (`emitter+0xF4`, fed from instance `+0x2C`). Empirically the burst-emission curves are `(0.0, 5000.0)…(0.633, 0.0)` in a 1.667 s effect and three `5000` spikes at 0.10/0.73/1.37 s in a 5.0 s effect. Almost all curves have `count == 1` (a constant); only the emission-rate curve has more than one key (counts 2, 3, 8, 9; §6.7). The dword at `P+0x478`, next to these slots, is *not* a curve count but a byte size (§6.6).

### 6.5 The record (0x58 bytes) — every field, after the consumer

| record+ | Field | Evidence | Confidence |
|---|---|---|---|
| `0x00` | name pointer | (§5.4) | CONFIRMED |
| `0x08` | **material-link name** — a string pointer, `-1` in 13/13. When present, `0x0043fd50` matches it **case-insensitively** against the `+0x00` name of the elements of the `root+0x48` array, and on a hit writes the element/index into `P+0x1F8`/`P+0x200` | CONFIRMED (disassembly); never populated in these samples |
| `0x10` | class id (§6.6) | CONFIRMED |
| `0x14`,`0x15`,`0x16`,`0x17` | four booleans (0/1 in 13/13). At instantiate they become bits **`0x20`, `0x40`, `0x80`, `0x100`** of the `Q+0x50` flags word (§6.9); what each bit *does* is not traced | CONFIRMED (bit mapping); role OPEN |
| `0x18` | `-1` in 13/13; **not read** by `0x0043fd50`/`0x00440030`/`0x00440130` (scoped to those three functions, not a whole-binary negative) | OPEN |
| `0x20`,`0x24` | runtime slot: `K` pointer and `0` — zero on disk | CONFIRMED |
| `0x28` | → P (§6.7) | CONFIRMED |
| `0x30` | → Q (§6.9) | CONFIRMED |
| `0x38` (byte) | runtime "is a child of another record" flag, set by `0x00440030` — zero on disk (this is why §5.4's `0x38`–`0x57` was all zero) | CONFIRMED |
| `0x3C`–`0x57` | not touched by any function traced (zero on disk, 13/13) | OPEN |

### 6.6 The class registry and the four emitter-volume classes

`record+0x10` selects a **volume class** (where a new particle is placed and which way it initially heads). Registry rows at `VA 0x01352208 + 0x14*i` (tag at row+8, create-function at +0xC, and a thunk at +0x10 that hands the class its **shape array**, `P+0x480`). The class objects are `0x64`-byte (`K`) instances; each class has its own vtable with a position/direction generator (slot `+0x14`) and shape-array accessors. **[CONFIRMED — disassembly for each row below; class NAMES are my labels from the generator's behaviour, not recovered identifiers.]**

| Tag | Create-fn / vtable | Behaviour (from the generator) | Shape array (`P+0x480`), byte size returned by the class init | In samples |
|---|---|---|---|---|
| `0xEC276F11` | `0x00441780` / `0x0129E648` | **point**: position = emitter origin; direction random on the unit sphere × scale | init returns `0`; stored `P+0x478 = 1`, target 8 zero bytes | 10 of 13 |
| `0x1D815E4A` | `0x00441820` / `0x0129E684` | **cube**: position = centre + `h·(rx,ry,rz)`, each `r ∈ [-1,1]` | 4 (`h`) | 1 (`VFX_Fireworks_Spawn01`, `h = 0`) |
| `0xB3269ACC` | `0x00441CB0` / `0x0129E6C0` | **box**: uniform in half-extents `(x,y,z)` | 12 | 0 (class behaviour only) |
| `0xFAA8D302` | `0x00441FD0` / `0x0129E6FC` | **column**: square section half-extent `s[0]`, vertical half-height `s[4]`, plus an optional **cone spread** (angle range `P+0x1BC`..`P+0x1C0`, enabled by byte `s[8]`, which the class init computes at instantiate — 0 iff both angles ≈ 0; it is 0 on disk in 2/2 column records, so not file data) | 12 | 2 (`Object01` `0.37,1.0`; `p_vel_sparks_01` `0.13,0.25` + 10° cone) |
| *(base class)* | vtable `0x0129E610` | **rectangle** in the local XY plane, half-extents `s[0], s[4]`; the overlay compares against `0xA1CC7F44` for it — ~~that tag has no registry row of its own~~ **CORRECTED 2026-09-20 by the orchestrator (raw-byte scan of the image): a registry-shaped row with exactly this tag DOES exist, at VA `0x013521EC` — `{next 0, prev 0, tag 0xA1CC7F44, create-fn 0x004414D0, init-fn 0x004417D0}`, immediately before the row this section calls row 0 and carrying the *same* init function `0x004417D0` as the other four volume rows. Whether it is ever linked into the volume list at run time (the static census of link sites found four registrations) was not determined; the abstract-base-class reading is a HYPOTHESIS *(Team B counts 502 real records carrying this tag, §5.2)*. So the image holds five volume-class rows, not four.** ~~(Row 7 of the second list holds `0x00AB1EC0`, which looks like an address rather than a hash.)~~ **CORRECTED — Team B's population count (44 real occurrences, `HANDOFF.md` §27.2) already refuted this; §7.3 (Agent AL) adds the why: `0x00AB1EC0` is a real, working filter class (a point-force/attractor modifier), not dead data.** | 8 | 0 |

Empirical (**CONFIRMED**): every record's tag is one of the four registered volume tags (13/13); `P+0x478` equals the class's declared size for **all three** non-point records (cube 4, column 12, 12); the debug overlay draws exactly these classes by calling the same accessors (box: three half-extents; column: radius + half-height; rectangle: two). Caveat: the overlay identifies the class by a virtual call at vtable `+0x0C`, which is the shared stub `0x00435660` (returns `-1`) in all four vtables *as stored in the image* — so how the overlay's comparisons are satisfied at run time was not established; the class↔tag mapping above rests on the create-function chain instead, not on the overlay.

### 6.7 The parameter block P (record+0x28) — field map

Offsets are relative to `P`. "Evidence" cites the consumer. "Sample" is the value range over the 13 records. The debug overlay prints these parameters with the literal labels **"particle count"**, **"emission rate"**, **"speed"**, **"angular speed s:… e:…"**, **"size s:… a:… b:… e:…"**, **"opacity scale"**, **"color s(…) v(…)"**, **"color a…"**, **"color b…"**, **"color e…"**, **"lifetime"** — each `size`/`color` line is `(value, variance)`; `s/a/b/e` = start, two mid keys, end — which matches the layout below exactly.

**Scalar / flag region**

| P+ | Type | Role | Sample | Confidence |
|---|---|---|---|---|
| `0x00` | u8 | **simulation mode**: `0` = GPU-driven particles (the CPU keeps only a birth time and three random seeds per particle and packs the curve values into a per-emitter parameter block, `0x00430e60`); non-zero = **CPU-simulated** (position/velocity/size/colour integrated per particle by `0x00436760`, own pool and list). Also render-descriptor flag `8` | 0 ×7, 1 ×6 (both spawners, both whored, 2 blast rings) | HIGH — the two branches of `0x00437fc0`/`0x004360c0`/`0x00436760` write disjoint state |
| `0x01` | u8 | **collidable**: particles are handed to external physics callbacks (`DAT_03171a28/38/3c/40`, never set on any traced path) | 0 ×13 | CONFIRMED read; role HIGH; unexercised |
| `0x02` | u16 | **emitter type**: `0` sprite, `2` ribbon (expiry scan may stop at the first live node), `6` spawner (no render/light record; owns children, §6.8), `7` (special: ≤ 8 children, cadence by `P+0x14`), `8` (streak), `10` (mesh, uses the `P+0x1F8/0x200` material link) | 0 ×8, 2 ×2, 6 ×2, 8 ×1 | 2, 6, 10 HIGH (consumer branches + record names `red_Ribbon*`, `VFX_Fireworks_Spawn*`); 8 by name only (`p_vel_sparks_01`) — HYPOTHESIS; 7 code-only |
| `0x04` | f32 | CPU sub-step rate (Hz) (§6.2 item 3) | 30.0 ×13 | HIGH |
| `0x08`,`0x0C` | f32 | pair lerped over particle age into particle field `+0x124`; role OPEN | 0.0 ×13 | CONFIRMED consumer; OPEN meaning |
| `0x10` | u16 | per-particle trail/child-node cap (also copied to the render descriptor) | 0 ×13 | MEDIUM |
| `0x12` | u8 | spin enabled (rotation angle + angular speeds live) | 1 ×10 | HIGH |
| `0x13` | u8 | random initial angle, uniform in `(-2π, +2π)` (constants `4π`, `2π` read from the binary) | = `0x12` | CONFIRMED arithmetic |
| `0x14`,`0x15`,`0x16` | u8 | **size track 2**: enable / multi-key / variance. Disabled → track 2 equals track 1 | `0x14` = 1 ×1 | CONFIRMED |
| `0x17` | u8 | **local space**: particle positions follow the emitter basis (also descriptor flag `2`) | 1 ×2 (the two `whored` records) | HIGH |
| `0x18` | u8 | `==1`: initial velocity = the emitter's own displacement this step (or the parent's velocity) × speed; else along the volume-generated direction | 1 ×4 (rocket sparks/ribbons) | HIGH |
| `0x19`,`0x1A`,`0x1B` | u8 | **size track 3**: enable / multi-key / variance (disabled → equals track 1) | 0 ×13 | CONFIRMED |
| `0x1C` | u8 | size track 1 multi-key (else two-key `s → e`) | 1 ×12 | CONFIRMED |
| `0x1D` | u8 | colour multi-key (else two-key `C0 → C3`) | 1 ×13 | CONFIRMED |
| `0x1E`,`0x1F` | u8 | enable scalar envelope A (`P+0x164`) / B (`P+0x18C`) | mixed | CONFIRMED |
| `0x20` | u8 | copied to a render-descriptor flag (`4`); role unknown | 1 ×13 | OPEN |
| `0x21`,`0x22` | u8 | acceleration enable / acceleration-variance enable | mixed | CONFIRMED |
| `0x23` | u8 | lifetime-variance enable | mixed | CONFIRMED |
| `0x24` | u8 | size track 1 variance enable | 0 ×13 | CONFIRMED |
| `0x25`,`0x26` | u8 | colour drawn at a **random point of the gradient** instead of by age (`0x25`), with a sub-mode byte (`0x26`) | 0 ×13 | MEDIUM; unexercised |
| `0x28`–`0x30` | 3×f32 | acceleration at particle age 0 | 0 | HIGH |
| `0x34`–`0x3C` | 3×f32 | half-range of a random offset added to it (only if `0x22`) | 0 | HIGH |
| `0x40`–`0x48` | 3×f32 | acceleration at age 1 (e.g. `(0, -7.62, 0)` and `(0, -12.7, 0)` — gravity-like) | see left | HIGH — velocity gains `dt·[(1-age)·a0 + age·a1]` per step |
| `0x4C`–`0x54` | 3×f32 | half-range for it (`(-6.35, 5.08, -6.35)`, `(-2.54, …)`) | | HIGH |

**Colour block (RGBA, `f32×4` per colour; only `0x58`–`0xA0` is the gradient)**

| P+ | Field | Notes |
|---|---|---|
| `0x58` | **C0** (age 0) | RGB may exceed 1 (**HDR**: `(42.97, 4.95, 1.0, 1.0)` in `red_Ribbon01`); alpha in `[0,1]` in 13/13 |
| `0x68` | **C1** | |
| `0x78` | **T1** — age at which C1 is reached | `0.2`/`0.25`, `T1 < T2` in (0,1) in all multi-key records |
| `0x7C` | **C2** | |
| `0x8C` | **T2** | `0.5`/`0.75` |
| `0x90` | **C3** (age 1) | |
| `0xA0`–`0xDF` | per-colour variance (4 × RGBA) — the overlay's `v(…)` | 0 ×13 (only the random-gradient path, `0x00435290`, is known to read them) |
| `0xE0`,`0xE4` | **alpha key times** (alpha uses its own T1/T2) | `0.25`, `0.75` |

Interpolation between C0..C3 uses the two times exactly as the code shows (RGB by `T1`/`T2`, alpha by `0xE0`/`0xE4`). **[CONFIRMED — disassembly (all four segments); empirical (RGB finite ≥ 0 and alpha ∈ [0,1] in 13/13, T1 < T2 in every multi-key record).]**

**Two more `6×f32` tracks and a light block**

| P+ | Role | Confidence |
|---|---|---|
| `0x164` (`V0`,`V1`,`T1@0x16C`,`V2`,`T2@0x174`,`V3`) and `0x18C` (`…T1@0x194`, `T2@0x19C`, `V3@0x1A0`) | scalar envelopes A and B: 4-key piecewise-linear over particle age, results stored in two per-particle fields and forwarded to the renderer; the overlay's single **"opacity scale"** label is the only naming hint | layout CONFIRMED; role OPEN (sample: `(0,1,.25,1,.75,1)`, `(1,1,.25,1,.75,1)`) |
| `0xE8`–`0x15F` | **five** `6×f32` four-key tracks (stride `0x18`, `T1` at `+8`, `T2` at `+0x10`, default stops `0.33`/`0.66` — the binary's random-gradient default is the same pair) copied wholesale into the emitter's dynamic-light record by `0x00430e60` | layout CONFIRMED (5 tracks × 13 records fit the pattern); role OPEN (light colour/intensity/radius is a guess) |

**Scalars 0x1B4–0x218 and the list headers**

| P+ | Type | Role | Sample | Confidence |
|---|---|---|---|---|
| `0x1B4`,`0x1B8` | f32,f32 | per-particle scalar `= value + U(±|half-range|)` stored at particle `+0x50`; the CPU integrator uses it as a velocity-proportional decay term (`v -= k·v·dt`); the GPU path forwards it unchanged (block `+0xD8/+0xDC`); the physics-proxy path also reads it. **The magnitudes (`50`/`500`/`5000`/`0`) are implausible as a per-second linear drag — units/semantics OPEN** | 5000 ×2, 500 ×6, 50 ×1, 0 ×4 | CONFIRMED consumer; OPEN meaning |
| `0x1BC`,`0x1C0` | f32,f32 | **cone spread angle range** (radians: `0.698132`=40°, `0.610865`=35°, `0.174533`=10°); read only by the *column* class (`p_vel_sparks_01`: `(10°, 0)`); the two spawners hold 40°/35° but are point/cube classes, which never read them | see left | HIGH |
| `0x1C4` | f32 | half-range of the **speed** (`P+0x278` curve) | 0 / 2 | CONFIRMED |
| `0x1C8`,`0x1CC` | f32,f32 | half-ranges of the **angular speeds** (`0x258`, `0x268`) | 0 | CONFIRMED |
| `0x1D0`,`0x1D4` | f32,f32 | **lifetime** (s) and its half-range (only if `P+0x23`); also the per-step `dt` clamp. Overlay label "lifetime" | 0.3–2.0 s; variance 0.5 ×2 | CONFIRMED |
| `0x1D8` | u32 | **particle capacity** (free budget): `0x00435f50` sets `emitter+0xE8 := P+0x1D8` and `+0xE4 := 0` on reset; spawning is limited to the free count | 100 ×6, 5 ×2, 3 ×3, 50 ×1, 25 ×1 | CONFIRMED |
| `0x1DC`,`0x1E0` | u32,ptr | **child list**: count and pointer to `{record index, 0}` 8-byte entries (§6.8) | 2 for both spawners | CONFIRMED |
| `0x1E8`,`0x1F0` | u32,ptr | **element list**: count and pointer to `{index into the `root+0x90` array, 0}` entries (§6.8) | 0 ×13 | CONFIRMED (linking code `0x00440130`); unexercised |
| `0x1F8`,`0x1FC`,`0x200` | ptr,0,i32 | runtime material link (written by `0x0043fd50`) | 0 on disk | CONFIRMED |
| `0x204` | f32 | runtime copy of `root+0x20` (duration) | 0 on disk | CONFIRMED |
| `0x20C`,`0x210` | f32,f32 | `(value, half-range)` handed to the external physics-proxy factory when `P+1 != 0` | `(1.0, 0)` ×13 | CONFIRMED read; role OPEN |
| `0x218` | f32 | **cull distance**: emission is suppressed when the camera-to-emitter distance exceeds it (squared test; at graphics tier < 3 the squared limit is ×4); `< 0` disables | −1.0 ×12, **100.0 ×1** (`p_vel_sparks_01`) | CONFIRMED |
| `0x21C`,`0x220` | u32,ptr | array of **16-byte** elements `(t, x, y, z)`-looking, small values (e.g. `(0, -0.0092, 0.0594, 0)`); count 1 in 13/13; **no consumer found** | | OPEN |
| `0x228`,`0x230` | count,ptr | occupies a **32-byte** region in 13/13; no consumer found | | OPEN |
| `0x238`,`0x240` | count,ptr | a curve slot; value `1.0` at `t=0` in 13/13 (with a stale second pair `(1.0,1.0)` beyond the count); not read by any traced function | | OPEN |

**Curve slots** (count offset `C`; keys at `C+8`; all evaluated at effect time in seconds unless noted)

| C | Curve | Role | Confidence |
|---|---|---|---|
| `0x248` | **rate** | emission rate, particles/s (× the class's rate scale `K+0xC`) — overlay "emission rate". Keys: `(0,10)`, `(0,25)`, 8/9-key burst trains, `(0,5000)→(0.633,0)`, `(0,2)`, `(0,5)` | CONFIRMED |
| `0x258` / `0x268` | **angular speed** start / end (± `P+0x1C8`/`0x1CC`); sign flipped by a constant `-1.0` depending on facing | CONFIRMED |
| `0x278` | **speed** (± `P+0x1C4`) × the volume-generated direction — overlay "speed" | CONFIRMED |
| `0x288` | value written to `emitter+0x80` each step (count 1, `1.0`; `2.0` in `shockwave` `Object01`); consumer of that field not identified | OPEN |
| **size track 1** `0x298`–`0x328` | `0x298` **s**, `0x2A8` s-var, `0x2B8` **a**, `0x2C8` a-var, `0x2D8` **Ta**, `0x2E8` **b**, `0x2F8` b-var, `0x308` **Tb**, `0x318` **e**, `0x328` e-var. The four values are sampled at spawn (variance = half-range, clamped ≥ 0), the two key times *at every step* (curves of effect time whose values are particle-age fractions). Scaled by `K+8` and the effect's scale | CONFIRMED — overlay "size s/a/b/e" |
| **size track 2** `0x338`–`0x3C8` | same ten-slot shape (`0x338` s, `0x348` s-var, `0x358` a, `0x368` a-var, `0x378` Ta, `0x388` b, `0x398` b-var, `0x3A8` Tb, `0x3B8` e, `0x3C8` e-var); gated by `P+0x14`/`0x15`/`0x16`; **defaults to track 1** | CONFIRMED |
| **size track 3** `0x3D8`–`0x468` | ten slots (`0x3D8`, `0x3E8`, `0x3F8`, `0x408`, `0x418`=Ta, `0x428`, `0x438`, `0x448`=Tb, `0x458`, `0x468`); gated by `P+0x19`/`0x1A`/`0x1B`; defaults to track 1 | CONFIRMED |
| `0x478`,`0x480` | **shape**: byte size and array pointer (§6.6). `0x478` is the *byte size* of the array (4/12/12), not a key count | CONFIRMED |

What the three tracks are *geometrically* (width/height/length?) is not established: the consumer keeps them as three independent per-particle floats and the renderer that reads them was not traced — **HYPOTHESIS: per-axis extents**. Empirical sense-check: `FX_BlastWave01` runs `0 → 12.96 (Ta 0.77) → 14.74 (Tb 0.90) → 17.07` — an expanding ring; `FX_Sparks` `0.147 → 0.675 → 1.470 → 1.984`.

### 6.8 Spawners, children, and the other count/array pairs

* **Children (CONFIRMED, disassembly + empirical).** `P+0x1DC`/`0x1E0` hold a list of record indices; `0x00440030` rewrites each entry to that record's `K` pointer, marks the child (`record+0x38 := 1`) and stores `(array, count)` into `K+0x3C/+0x40/+0x44`. A spawner (`P+2 == 6`) then, when it fires (`0x004309c0`), instantiates each child emitter at its own position and runs one initial step. Empirical: `VFX_Fireworks_Spawn01` → records `2, 3` = `FX_Sparks01`, `red_Ribbon01`; `VFX_Fireworks_Spawn` → records `0, 1` = `FX_Sparks`, `red_Ribbon` (the two `01`/bare pairs match); child counts non-zero **exactly** for the two type-6 records; all indices valid and ≠ self, second dwords 0. 13/13.
* **Element list (`P+0x1E8`/`0x1F0`).** Same scheme against the **`root+0x90`/`0x98` array (stride `0x228`)**: `0x00440130` marks the element (`+0x1FB := 1`) and converts entries to element pointers; `0x004309c0` allocates a runtime object per entry. Each element carries about two dozen keyed-curve slots — count/pointer pairs at `+0xA8/0xB0`, `+0xB8/0xC0`, `+0xC8/0xD0`, `+0xD8/0xE0`, `+0x108/0x110`, `+0x118/0x120` … `+0x1D8/0x1E0`, `+0x1E8/0x1F0`, and `+0x68`/`+0x70`, plus three flag bytes at `+0x1F8..0x1FA` — evaluated each frame at *effect time* by `0x00430e60` into the runtime object's fields. Their meaning (the pattern — position/orientation keys, several colour/intensity/size scalars — suggests a **light-like attachment**) is **HYPOTHESIS**; count 0 in all 3 samples, so nothing here is validated empirically. **[Populated samples since decoded, §7.6.]**
* **`root+0x40`/`0x48` array (stride `0x88`)** — named elements (`+0x00` name string) that a record can bind to by name (`record+0x08`); the instance update reads a pointer at `+0x10` (→ three floats, an offset), a float at `+0x80`, and per-element runtime handles from `root+0x58` (an `i32` array, `-1` = unresolved), and calls the external callback `DAT_03171a28` with a position/orientation to create a body. **HIGH CONFIDENCE: named external collision/physics-object references**; count 0 in 3/3 samples.
* **`root+0x70`/`0x78` (stride `0xC8`)** — see §6.2 item 6; curve pairs at `+0x18/+0x20` and `+0x98/+0xA0` and a colour at `+0x54`, evaluated by `0x00433c10`. Count 0 in 3/3; role OPEN. **[Resolved: field-mapped and validated on 13 real elements, §7.5.]**

### 6.9 The Q block (record+0x30, `0xD8` bytes) and the render descriptor

`0x0043fd50` allocates a small (`0x18`-byte) **render descriptor** per record and stores it in `K+0x38`; the descriptor holds: flags word at `+4`, **`P+0x02` at `+8`**, **`P+0x10` at `+0xC`**, and **Q at `+0x14`**. Flag bits are composed from the record and P: `1`←`P+0x12`, `2`←`P+0x17`, `4`←`P+0x20`, `8`←`P+0x00`, `0x40`←`Q+0x60` non-null. **[CONFIRMED — disassembly.]**

Q itself: `+0x00` u32 hash-looking id (`0xA6792C68` ×10, `0x7E5C7726` ×2 — both ribbons —, `0xAC67A29A` ×1 — `p_vel_sparks_01`; **not** a CRC of any texture name in these files, tested), `+0x04` `0x00010000` (13/13), `+0x08` ptr → 16-byte blob `{id, small int, …}` (id `0x79A00DFF` ×10 with the int 0/1/2 — sprite 0, ribbon 1, spawner/streak 2 in `fireworks`; id `0x333B2CB0` ×3 with `0`,`0xFFFF` — the three blast-wave records), `+0x18` `-1`, pointers at `+0x20`, `+0x30`, `+0x38`, `+0x48` (48/32-byte float blobs; `+0x20`/`+0x38`/`+0x60` often share one target; entries like `26.0`, `11.0`, `3.0` with `-0.0` companions), **`+0x50` flags word** (file value `0x08` or `0x10`; at instantiate `(flags & 0x18)==0 → |= 0x10`, and the four record booleans OR in `0x20/0x40/0x80/0x100`), `+0x58`/`+0x59`/`+0x5A` bytes read by the GPU-parameter packer (`+0x58` as an integer → float), **`+0x60` pointer — the only Q pointer rebased lazily** (at instantiate: `root + value`, `+0x64 := 0`), `+0x68` `(-1,-1,1)`, `+0x74`.. a colour-looking triple (`0.144, 0.323, 0.965` / `0.209, 0.386, 0.965`), `+0x80`.. `(c,c,c)` then `0`, `0.8` (`0.8` in 13/13). **Interior semantics are OPEN** — the renderer that consumes the descriptor was not traced; Q is a render-state block (the flag composition and lazy pointer say so), not particle-simulation data. Sizes of the 6 sub-blobs (16/32/48 bytes) are known only from the tiling, §6.11.

### 6.10 The VFX Filter record (`root+0x80`/`0x88`, stride `0x70`) — cheap partial

`0x00440270` instantiates each filter through the **second registry list** (`DAT_031729b4`, rows 5–10 at `VA 0x01352250+0x14*i`: tags `0x197AEC63`, `0x4A8FB92D`, `0x00AB1EC0`(?) **[a real filter class, §7.3]**, `0x6BAB63EA`, `0xDB1A7FC6`, `0x7EDA2EE8`; create-functions `0x004427B0`, `0x00442A20`, `0x004430D0`, `0x00443720`, `0x00443B70`, `0x004444A0`; common init thunk `0x004430E0`). Record fields **[CONFIRMED — disassembly + empirical, 2/2 filters]**: `+0x00` name ptr; `+0x08` u32 (`5` ×2), `+0x0C` u32 (`1` ×2); **`+0x10` class id — `0x7EDA2EE8` in both samples, a registered filter class** (validated); `+0x14` `-1`; `+0x18` u32 `2`; `+0x20` ptr (16-byte block); two keyed tracks `(+0x38 count, +0x40 keys)` and `(+0x48 count, +0x50 keys)` (`count 1`; the values are copied into the instance as-is); `+0x58` runtime instance pointer; `+0x60` = byte size of the class-parameter blob (`0x20`); `+0x68` ptr to that blob (32 zero bytes here), passed to the class's init. The instance also receives `root+0x20` (duration). The class behaviour (what a "VFX Filter" *does* to the picture) is **OPEN** — the six filter classes were not decompiled. **[Resolved: all six decompiled, §7.3.]**

### 6.11 Validation

`tools/harnesses/cefct_params.py` decodes every record of the 3 real samples (13 records, `vfx_runningman_fireworks` 6, `vfx_shockwave_kill_all` 5, `vfx_whored_invulnerable` 2) and runs 20 checks per file; **all pass, 3/3 files**:

* structure: records contiguous → `P` blocks at `align16(records end)` then pitch `0x4A8` → `Q` blocks at `align16(P end)` then pitch `0xD8` (3/3); all 41 P pointers land in the file, 16-aligned, past the Q blocks (13/13); `P+0x484..0x4A7` zero (13/13).
* **tiling gate (region level, exact):** every pointer target after the last Q block starts a region that runs to the next target; each region has a *model size* from the consumer's own counts (name `ceil16(strlen+1)`; curve slot `max(16, ceil16(8·count))`; `P+0x220` array `16·count`; `P+0x230` slot `32`; child list `ceil16(8·n)`; shape array `ceil16(P+0x478)`; filter sub-blocks; texture list and strings; the zero pad at `root+0x10/0x78/0x98`). Result: **`fireworks` 269 regions (247 modelled-exact, 22 open Q-blobs), `shockwave` 228 (210 + 18, plus the 224-byte optional `root+0x10` block), `whored` 97 (89 + 8); 0 overlaps, 0 mis-sized, 0 unaligned, and the last region ends exactly at the file end** (= the `root+0x38` end pointer). The open regions are exactly the Q sub-blobs (sizes 16/32/48) and, in `shockwave` only, the optional `root+0x10` block (`root+0x08 = 1`; content: a hash `0x89EE6152`, counts `4`/`6`, five sub-pointers to small float arrays — **not decoded**).
* semantic checks: colour block, size-track key times (`0 < Ta ≤ Tb ≤ 1` for all three tracks in 13/13, including disabled defaults), curve curve counts 1–9 with sorted key times in 36 slots × 13 records, lifetimes `0.3–2.0 s` `> 0`, rate-curve times within `[0, root+0x20]` (13/13), `P+0x478` = class size (3/3 non-point), all tags registered (13/13), child lists valid and only on type-6 records (13/13), runtime-scratch cells zero on disk (`record+0x20/0x24/0x38…`, `P+0x1F8..0x207`), `record+0x14..0x17` ∈ {0,1}, filter tags registered (2/2), `P+0x04 == 30.0` (13/13).
* **negative controls:** the duration test rejects the wrong neighbouring field `root+0x24` on `fireworks` (1/3 files can discriminate — the other two have `root+0x24` above every key); the `Ta/Tb` test rejects the value slots `a/b` in 3/3 files.

Harness: `tools/harnesses/cefct_params.py` (run without arguments); `tools/harnesses/exe_va.py` (read-only VA reader used for the registry rows and constants); Ghidra scripts `tools/scripts/AbXrefs.java`, `AbScalar.java`, `AbFuncs.java`, `AbDisp.java`, `gh_ab.ps1`.

### 6.12 What remains OPEN after this pass

1. **Renderer-side meaning** of the per-particle fields the CPU/GPU paths forward: the three "size" tracks' axes, scalar envelopes A/B, the `0xE8` five-track block, `P+0x20`, `P+0x25/0x26`, `P+0x08/0x0C`, the magnitude/units of `P+0x1B4/0x1B8`, and ~~the `Q` block's interior and the four record booleans' flag bits~~ (the four record-boolean → `Q+0x50` flag *bits* were already resolved in §6.5/§6.9; what each bit *does* downstream, and `Q`'s six sub-blobs' interior, are FURTHER ADVANCED but still OPEN per §7.2 — and `P+0x08/0x0C`'s downstream field `particle+0x124` now has a confirmed first consumer, §7.3: two of the five particle-force filter classes multiply their force by it).
2. The three slots `P+0x220`, `P+0x230`, `P+0x240` (extents known, consumers not found in any function traced) and `P+0x288`'s downstream field `emitter+0x80` — negative claims here are scoped to the ~25 functions listed in §6.1, **not** a whole-binary scan.
3. The four `record+0x10` class-id **names**; `0xA1CC7F44` (rectangle) and `0xB3269ACC` (box) never occur in the samples, so their tag-to-record binding is code-only. *(Note: Team B's 1,812-file population has 502 records carrying `0xA1CC7F44`, §5.2 — the tag is live data; this bears on §6.6's abstract-base-class HYPOTHESIS.)*
4. ~~The `root+0x40/0x48`, `root+0x70/0x78` and `root+0x90/0x98` arrays and the six filter classes: consumers located, roles inferred at best, **no populated element in any sample** (count 0 / one trivial filter) — a larger `.cefct_pc` is needed.~~ **RESOLVED (real non-empty samples), §7.4–§7.6 (Agent AL, 2026-09-23):** Team B's full 1,812-entry enumeration found real files with all three arrays populated; 208/13/62 real elements decoded and field-mapped, with two universal empirical constants found (`root+0x40` element `+0x80`, `root+0x90` element `+0x08/+0x0c`). The six filter classes' behaviour is now decoded from disassembly (§7.3: five are per-particle force/decay modifiers, one is an orientation/transform filter) though only the trivial (all-zero) `0x7EDA2EE8` class-blob is empirically validated — the other five classes' real parameter values remain unvalidated.
5. `root+0x24` (float #2), the `MCKH` mini-block's reader (§5.6), the `root+0x10` optional block (`shockwave`, 224 bytes), `record+0x18`, `record+0x3C..0x57`.

## 7. `Q` block, VFX-Filter classes, and the three sibling arrays -- real non-empty samples (Agent AL)

**Prepared by:** SPEC TEAM, Agent AL (`gp_al1`), 2026-09-23. **Scope:** the `record+0x30` render-state block `Q` (§6.9), the six VFX-Filter classes (§6.10), and the three sibling arrays that were present in code but count-`0` in every sample the previous passes had (`root+0x40/0x48`, `root+0x70/0x78`, `root+0x90/0x98`) — unblocked by Team B's full 1,812-entry `.cefct_pc` enumeration, which found real, non-empty instances of all three. **Method:** same as §6 — Ghidra 12.1.3 headless on a disposable project copy (`tools/gp_al1`), one-off scripts `tools/scripts/Al*.java`; every claim below was re-verified against real file bytes, not just read off the decompiler, via a from-scratch decoder (`tools/harnesses/cefct_arrays.py`, built on `cefct_probe.py`/`cefct_params.py`).

### 7.1 New real samples, and what they do and don't cover

Ten new samples were extracted, all as entries of mode-(b) shared-stream containers (container directory flags `0x4803`, bit `0x2` set) — the same fully-reliable extraction path as the three original samples, with **no mode-(a) reliability concerns at all** (checked directly against each container's own flags word before trusting the bytes). **[CONFIRMED — empirical.]**

| Sample | Bytes | Source | `root+0x40`/`0x70`/`0x90` counts | `root+0x60` (records) |
|---|---|---|---|---|
| `vfx_cybertank_projectile.cefct_pc` | 992 | `preload_effects.vpp_pc` / `VFX_CyberTank_Projectile.str2_pc` | 1 / 0 / 0 | 0 |
| `vfx_kneecappers.cefct_pc` | 1,092 | `preload_effects.vpp_pc` / `VFX_KneeCappers.str2_pc` | 1 / 0 / 0 | 0 |
| `vfx_lasersight.cefct_pc` | 1,462 | `preload_effects.vpp_pc` / `VFX_LaserSight.str2_pc` | 1 / 0 / 0 | 0 |
| `vfx_carunderglow.cefct_pc` | 688 | `preload_effects.vpp_pc` / `vfx_carunderglow.str2_pc` | 0 / 1 / 0 | 0 |
| `vfx_brakelight.cefct_pc` | 2,048 | `preload_effects.vpp_pc` / `vfx_brakelight.str2_pc` | 0 / 1 / 1 | 0 |
| `vfx_sun_glare.cefct_pc` | 1,504 | `preload_effects.vpp_pc` / `vfx_sun_glare.str2_pc` | 0 / 0 / 1 | 0 |
| `vfx_amberrunninglights.cefct_pc` | 1,504 | `preload_effects.vpp_pc` / `VFX_AmberRunningLights.str2_pc` | 0 / 0 / 1 | 0 |
| `18_in_planelights.cefct_pc` | 40,296 | `cutscenes.vpp_pc` / `18_in_f.str2_pc` | 3 / 11 / 26 | 0 |
| `vfx_mbrawl_cflashes.cefct_pc` | 37,696 | `sr3_city_0.vpp_pc` / `m21_murderbrawl^m21.str2_pc` | 0 / 0 / 33 | 0 |
| `vfx_roof_destruction.cefct_pc` | 705,316 | `cutscenes.vpp_pc` / `06_out1_f.str2_pc` | 202 / 0 / 0 | 0 |

**Every one of these ten files has `root+0x60` (the named-emitter-record array) empty — they are "attachment-only" effects** (a car light, a runway light rig, a destructible-debris fragment list, a camera-flash bank) that use only the sibling arrays, never a particle emitter. This is useful and expected (it is exactly why these files are the clean, single-purpose examples Team B flagged), but it means **this pass's new empirical validation is scoped to the three arrays' own structure** — it does **not** add any new real-data check of `P`, `Q`, or the filter *records* (§6.7–§6.10), since no new sample has a populated record array. The filter-class *behaviour* findings below (§7.3) are disassembly-only, cross-checked only against the two pre-existing "VFX Filter01" instances (§6.10, unchanged). **[CONFIRMED — empirical, all ten files, `root+0x60` checked directly.]**

### 7.2 The `Q` block (`record+0x30`): the `+0x60` lazy pointer's exact rebase rule, and its consumer identified

§6.9 already established that `Q+0x60` is "the only Q pointer rebased lazily" and that flag `0x40` of the render descriptor is set when it is "non-null". Decompiling the instantiate routine `0x0043fd50` in full (not just the parts already excerpted in §6.9) gives the exact rule, and a genuine consumer:

- On disk, `Q+0x60` holds one of three things: **`0`** (feature absent), **`0xFFFFFFFF`** (feature present but the pointer is null), or a **byte offset** (a real sub-blob, root-relative like every other fixed-up field in this format).
- At instantiate: if the raw value is `0`, nothing happens — the descriptor's flag `0x40` is cleared and `Q+0x60`/`Q+0x64` are left untouched. Otherwise (either sentinel form), `Q+0x64` is zeroed, `Q+0x60` is rewritten to `0` (the `0xFFFFFFFF` case) or to `root + offset` (the real-offset case), the descriptor's flag `0x40` is **set**, and the resolved value is passed to a small helper, `0x00e7faf0`.
- `0x00e7faf0` is a **9-instruction setter**, not a reader: it checks the *descriptor's* own flag `0x40` and, if set, writes its argument into `Q+0x60` and zeroes `Q+0x64` — i.e. it performs exactly the same write `0x0043fd50` already just did inline. It is a generic "(re)point this render descriptor's lazy blob" utility, presumably shared with some other, not-yet-identified call site that changes `Q+0x60` after instantiate (none was found this pass). **[CONFIRMED — disassembly, full body of both functions.]**

**What this does *not* resolve:** neither `0x0043fd50` nor `0x00e7faf0` **read** the bytes *at* the resolved `Q+0x60` target — they only compute and store the pointer. The renderer/GPU-submission code that actually dereferences the render descriptor (held at `K+0x38`, `Q` at descriptor `+0x14`) to consume `Q`'s six sub-blobs' *content* was searched for within this pass's whole anchor set — full decompiles of `0x00436760` (emitter step), `0x00437fc0` (particle spawn), `0x00430e60` (per-frame effect update), `0x004309c0` (spawner logic) and `0x00433c10` — and **no dereference of the descriptor's `+0x14` field (`Q`) was found in any of them**; the only vtable-mediated indirection through the emitter's own `K` object in that code is the already-known position/direction generator at `K`'s vtable `+0x14` (§6.6), unrelated to `Q`. This is a **negative claim scoped exactly to those five functions plus `0x0043fd50`/`0x00e7faf0`** — the actual GPU draw/submission module lies outside this anchor set and was not located. **Q's six sub-blobs' interior remains OPEN**, now for a concrete, stated reason rather than by default. **[CONFIRMED — disassembly, scoped negative.]**

### 7.3 The six VFX-Filter classes: structure and per-class behaviour

**Structural pattern (all six classes, CONFIRMED — disassembly):** each registered filter class (§6.10's registry, `VA 0x01352250`, stride `0x14`) is a **pool-allocated object with its own vtable**. The vtable's first two slots (`+0x00`, `+0x04`) are identical byte-for-byte across all six vtables (generic base-class methods, not filter-specific and not decoded further — out of this task's scope). Five slots are genuinely per-class:

| Slot | Role | Evidence |
|---|---|---|
| `+0x08` | **release** — returns the instance to its class's own free list | identical shape in all 6, only the free-list globals differ |
| `+0x0c` | **GetClassId** — returns the class's own registry tag as a literal constant | CONFIRMED for all 6 (`0x197AEC63`, `0x4A8FB92D`, `0x00AB1EC0`, `0x6BAB63EA`, `0xDB1A7FC6`, `0x7EDA2EE8` respectively, read directly off each function) |
| `+0x10` | **Init** — the target of the shared registry init-fn thunk `0x004430E0` (§6.10); receives the filter record's own class-parameter blob (`filterrec+0x68`, size `filterrec+0x60`) | CONFIRMED for all 6 |
| `+0x14` | **Apply** — the per-frame effect | CONFIRMED for all 6 |
| `+0x18` | **Update** — evaluates this class's own curve(s) ahead of Apply | CONFIRMED for all 6 |

**Five of the six classes are per-particle force/decay modifiers, and share a common shape (CONFIRMED — disassembly):** their `Apply` walks a **private, per-filter-instance doubly-linked particle list** at instance `+0x8c` (next-pointer at particle `+0x3c`) — a list populated at particle-spawn time by `0x00437fc0` (confirmed: the spawn routine inserts a newly-spawned particle into the filter instance's list at `+0x8c`/`+0x94`, the same doubly-linked-list idiom). Their `Update` evaluates a class-owned curve at the effect's elapsed time and stashes the result(s) on the shared per-record render descriptor (the same object passed as argument 2 to Apply/Update) at `+0xA4`/`+0xA8`, which `Apply` then reads back as its force strength. The per-particle struct fields these classes touch are: position `+0x00/+0x04/+0x08`, velocity `+0x10/+0x14/+0x18`, and (two of the five) the age-derived weight `+0x124` already documented as OPEN in §6.12 item 1 (`P+0x08`/`P+0x0C` lerped into it) — **this pass identifies its first real consumer.** **[CONFIRMED — disassembly.]**

| Tag | Create-fn | Behaviour (from `Apply`/`Update`, my own functional label) | Uses particle `+0x124`? | Class-blob size (`filterrec+0x60`) |
|---|---|---|---|---|
| `0x197AEC63` | `0x004427B0` | **single-axis decay**: `velocity.y -= weight × curve(t) × dt` (only the `+0x14` velocity component touched) | yes | `0x10` (one curve, no separate rebase step in `Init` — see caveat below) |
| `0x4A8FB92D` | `0x00442A20` | **rotating-direction force**: builds a 3-axis unit direction from a curve-driven angle, then `velocity += direction × weight × curve(t) × dt` on all three axes; also rotates the direction into the emitter's local basis when the record is local-space | yes | `0x40` (four curve slots) |
| `0x00AB1EC0` | `0x004430D0` | **point force**: inverse-square attraction/repulsion toward an anchor point (the shared descriptor's own `+0x44/0x48/0x4c`), with an inner-radius "drag" zone (plain velocity damping) inside a threshold radius (descriptor `+0xb8`) — a classic gravity-well/point-attractor particle modifier | no | `0x20` (one curve + one scalar + one extra rebased field; `Init` shared with the next row) |
| `0x6BAB63EA` | `0x00443720` | **velocity redirect**: preserves the particle's current speed but re-points its direction along a curve-driven angle | no | `0x20` (`Init` identical function to `0x00AB1EC0`'s row, `0x00443100`) |
| `0xDB1A7FC6` | `0x00443B70` | walks the same per-filter particle list; body is far larger (1,553 decompiled-instruction bytes, dozens of local floats) — **HYPOTHESIS: a more elaborate (noise/turbulence-style) force**, not traced to a confirmed formula this pass | not observed in the portion read | not determined |
| `0x7EDA2EE8` | `0x004444A0` | **not a particle-force class** — see below | n/a | `0x20` (all-zero in both real "VFX Filter01" instances; `Init` is a no-op that ignores the blob entirely) |

The sixth class, **`0x7EDA2EE8`** ("VFX Filter01" — the only tag actually observed in real data, §6.10, 2/2 samples) is structurally different: its `Apply`/`Update` never touch the `+0x8c` particle list. Instead they compute an **orientation/transform** (a helper, `0x00da6c00`, returning a 48-byte/12-float block — consistent with a 3×4 matrix) from two inputs on the shared descriptor object, refine it with generic vector/quaternion math helpers, and write the result back into the descriptor at `+0x44/0x4c` (a 3-float direction) and `+0x50..0x68` (the 12-float block) — the **same target fields** that class `0x4A8FB92D`'s `Update` also writes, confirming argument 2 is one shared "spatial descriptor" object type across every filter class regardless of family. **HIGH CONFIDENCE: an orientation/alignment filter** (e.g. "face velocity" or "align to a reference axis"); the exact geometric meaning of the transform and the identity of its two inputs were not traced further — **OPEN**. **[CONFIRMED — disassembly for the mechanism; HIGH CONFIDENCE for the functional label; both blob content and real per-class parameter values are UNVALIDATED against real data for every class except `0x7EDA2EE8`'s trivial (all-zero) one, per §7.1's scope note.]**

**Class-blob convention (CONFIRMED — disassembly):** `Init` for classes `0x4A8FB92D`/`0x00AB1EC0`/`0x6BAB63EA` explicitly rebases specific blob-internal dwords using the exact same sentinel rule as every other pointer field in this format (`0xFFFFFFFF → 0`, else `+= blob's own base address`) — i.e. the class-parameter blob is itself built from the same `{count, pad, keys(offset), pad}` 16-byte keyed-curve slot already established for `P` (§6.4), just resolved relative to the blob's own address rather than `root`. Class `0x197AEC63`'s `Init` copies its two blob dwords with **no** rebase arithmetic at all, and this pass could not determine why (possibly its one blob-internal pointer is pre-resolved by a pass this pass didn't trace, possibly the two real files simply never exercise the branch that would need it) — **OPEN, honestly left unresolved rather than guessed.**

**Correction to §6.6's registry read:** the mapping in §6.6's table for the second (filter) registry list already carried a note flagging tag `0x00AB1EC0` as looking "like an address rather than a hash" — Team B's independent population count (44 real occurrences across 1,812 files, `HANDOFF.md` §27.2) already refuted that; this pass adds the *why-it-matters*: `0x00AB1EC0` is a real, working filter class (the point-force one above), not dead data.

### 7.4 `root+0x40`/`root+0x48` (stride `0x88`): real names, confirmed and open fields

208 real elements were decoded (1 each in `vfx_cybertank_projectile`/`vfx_kneecappers`/`vfx_lasersight`, 3 in `18_in_planelights`, 202 in `vfx_roof_destruction`). `+0x00`'s name string (already CONFIRMED as the target of `record+0x08`'s case-insensitive match, §6.5) reads, for the first time, real content:

| Sample | Names |
|---|---|
| `vfx_cybertank_projectile` | `Box01` |
| `vfx_kneecappers` | `Cylinder01` |
| `vfx_lasersight` | `Line01` |
| `18_in_planelights` | `Searchlight_innerrcone`, `Searchlight_innerrcone01`, `Searchlight_innerrcone02` |
| `vfx_roof_destruction` | `VFX_Metal_frag_04`, `VFX_Metal_frag_14`, `VFX_Metal_frag_21`, `VFX_Metal_frag_27`, … (202 total, all `VFX_Metal_frag_NN`) |

**[CONFIRMED — empirical, 208/208 elements, exact string dereference.]** These names read as **externally-placed scene objects** the effect record binds to by name — simple collision-primitive names (`Box01`, `Cylinder01`, `Line01` — matching a projectile/tracer/kneecapper-trap effect that would need a collision volume or a beam path), a light-cone name (`Searchlight_innerrcone*`, matching a runway-light cutscene), and per-fragment destructible-piece names (`VFX_Metal_frag_NN`, matching a roof-destruction effect that presumably attaches a VFX layer to each of 202 physical debris pieces). This is a substantially stronger empirical basis for §6.8's existing "HIGH CONFIDENCE: named external collision/physics-object references" than the previous all-zero/count-0 samples could offer, and it is now upgraded to **CONFIRMED — empirical (the naming pattern)**, while the exact external subsystem each name resolves against remains the same HIGH CONFIDENCE inference as before (the consumer, matched by `record+0x08`, was already traced in §6.5; what happens on the *far* side of that name match — which game subsystem owns `Box01`/`Searchlight_innerrcone` objects — was not traced this pass).

Two more per-element facts, confirmed 208/208 across every real element regardless of sample: element `+0x80` is **always `0xFFFFFFFF`** on disk (a runtime-handle slot, unpopulated in every real file, consistent with — but not proven identical to — the root-level `+0x58` handle array §6.8 already described), and element `+0x08` (a plain `u32`, not pointer-fixed-up) is a small integer that varies with the element (`1` for `Box01`, `4`-in-a-different-slot for `Cylinder01`... concretely: `12` for all 3 `Searchlight_innerrcone*`, `1` for `Box01`/`Cylinder01`, `9` for `Line01`, `1` for every one of the 202 `VFX_Metal_frag_*`). **HYPOTHESIS, not confirmed:** a point/complexity count tied to the named shape (`Line01`'s `9` would fit "9 waypoints along the beam"); no consumer of this field was found this pass, so this is offered as a plausible reading of the number, not a decoded fact. The remaining pointer-shaped sub-fields of this `0x88`-byte record (roughly a dozen further slots resolving to small vec4-sized blocks) are structurally present and were spot-checked to resolve inside the file, 16-aligned, but their individual roles were **not** decoded this pass — **OPEN**.

### 7.5 `root+0x70`/`root+0x78` (stride `0xC8`): the light/corona attachment, field-mapped and validated

§6.8 already named this array's consumer (`0x00433c10`) and three of its offsets as a guess (`+0x18/+0x20`, `+0x98/+0xA0`, "a colour at `+0x54`"). Decompiling `0x00433c10` in full gives its complete field map, and 13 real elements (11 in `18_in_planelights`, 1 each in `vfx_brakelight`/`vfx_carunderglow`) validate it:

| Offset | Field | Confirmed role |
|---|---|---|
| `+0x08` | `u16` | **type** (`0`, `1`, `2` or `3` handled; `0` and `2` observed in real data) — selects which type-specific fields below are also copied |
| `+0x18`/`+0x20` | keyed curve ("curve A") | evaluated at the effect's elapsed time; gates the light on/off together with curve B (both compared against a small threshold before the light is even touched this frame) |
| `+0x98`/`+0xA0` | keyed curve ("curve B") | see above |
| `+0x50`, `+0x54` | `f32`, `f32` | a pair, scaled by the effect instance's own scale factor and written into the runtime light object as two adjacent fields — HIGH CONFIDENCE a cone-angle or radius pair (inner/outer) |
| `+0x60` | `f32` | a single scalar written to the runtime light right after the `+0x50/+0x54` pair — role not narrowed further than "a third light shape parameter" |
| `+0x69` | `u8` (bool) | a light-object flag bit, gated further by whether the record itself is also flagged (`record+0x38`'s child bit is checked alongside it) |
| `+0x70` | `u32` (name/hash) | if non-zero, resolved via `0x00dcecc0` to a runtime handle (HIGH CONFIDENCE: a projected "cookie"/gobo texture reference) and stored on the light; gates a further copy of `+0x78/0x7c/0x80/0x84` (four raw values) and `+0x88` (a bool) onto the light object |
| `+0x78`..`+0x84` | 4×`u32` | copied verbatim to the light only when `+0x70` resolves | 
| `+0x88` | `u16` (bool) | copied to the light only when `+0x70` resolves |
| `+0x48`, `+0x4c`, `+0x64`, `0x68`(bool) | type-`1`-only fields | copied to the light only when `type==1` |
| `+0x5c` | type-`2`-only field | copied to the light only when `type==2` |

**[CONFIRMED — disassembly, full body of `0x00433c10`.]** Real values (13/13 elements, all types 0/2, `type` 1/3 unexercised): `18_in_planelights`' 11 runway lights all have `type=0`, curve A ranging from a flat `5.0` constant to a 9-key sequence that ramps `0→0→5→0→5→0` then **repeats its last three keys a second time** (`0.75/0.875/1.0` twice) — a genuine, reproducible non-monotonic key sequence (re-checked directly against the raw bytes, not a decode artefact), consistent with an authored repeating flash/strobe pattern rather than a one-shot ramp; curve B a flat `1`/`2`/`3`; the `+0x50/+0x54` pair `(0, 10/12/20/30)` — plausible cone half-angles in degrees; `+0x60` a constant `2.0` in every one of the 13 elements **except** `vfx_carunderglow` (`type=2`), which has `+0x60=40.0` and its one type-2-only field (`+0x5c`) `=1.15`. `+0x70` (the cookie-texture reference) is `0xFFFFFFFF` (unresolved) in **13/13** real elements — the feature exists in the format and is read by the consumer, but was not exercised by any file this project has examined. **[CONFIRMED — empirical, 13/13.]**

### 7.6 `root+0x90`/`root+0x98` (stride `0x228`): a universal header marker, and the first real counts

62 real elements were decoded (26 in `18_in_planelights`, 33 in `vfx_mbrawl_cflashes`, 1 each in `vfx_amberrunninglights`/`vfx_brakelight`/`vfx_sun_glare`). Two facts hold in **every single one of the 62 elements, across all 5 files**:

- **`+0x08`/`+0x0c` carry the exact same 8-byte marker already documented for `Q+0x00`/`Q+0x04`** (§6.9): `0xA6792C68` then `0x00010000`, byte-for-byte identical, **62/62**. A deliberately-wrong-offset control (checking the marker at `+0x00` instead of `+0x08`) matches **0/62** — the offset, not a coincidence of the constant being common, is what discriminates. This was not visible before because every previously-known sample had this array empty; it strongly suggests these elements carry (or begin with, at a `+0x08` shift) **the same fixed schema-tag pair the render-state block `Q` uses**, i.e. this array's elements are built from the same underlying "typed sub-record" convention as `Q`, not an unrelated structure. **[CONFIRMED — empirical, 62/62, with a negative offset control.]**
- **`+0x20` is `0xFFFFFFFF` in every element, 62/62** — a field the format supports (it is fixed up like every other pointer slot) but that no real file populates. **[CONFIRMED — empirical.]**

Of the count/pointer pairs §6.8 already listed (`+0x70`, `+0xa8`, `+0xb8`, `+0xc8`, `+0xd8`, `+0x108`, `+0x1d8`, `+0x1e8`), every one of them reads `1` in every element **except `+0xa8`, which reads `4` in all 33 `vfx_mbrawl_cflashes` elements** (and `1` everywhere else) — the first real, non-constant count anywhere in this array, confirming it genuinely is a variable-length list slot rather than dead code that only ever happens to hold `1`. The three flag bytes at `+0x1f8..0x1fa` (§6.8) are `(0,0,0)` in every element of four of the five files and **`(1,1,1)` in the one `vfx_brakelight` element** — again a real, sample-dependent value, not a hardcoded constant. **[CONFIRMED — empirical, both findings.]** Beyond these bytes, the ~two-dozen further keyed-curve slots §6.8 already described structurally (`+0x118`..`+0x1c8`, spaced `0x10` apart) were confirmed to exist (the packed data area between the decoded pointers accounts for exactly that many bytes, with no gaps or overlaps in the samples checked) but their individual roles were **not** decoded this pass — **OPEN**, and the tiling was checked only informally (by inspecting sorted pointer targets), not with the same formal region-level gate §6.11 built for `P`/`Q`.

### 7.7 Harness

`tools/harnesses/cefct_arrays.py` (new; built on `cefct_probe.py`/`cefct_params.py`, run without arguments) decodes and validates all three arrays over **all 13 real samples now available** (the original 3 plus these 10): 208 `root+0x40` elements, 13 `root+0x70` elements, 62 `root+0x90` elements. Checks, all passing 13/13 files: every `root+0x40` element's `+0x80` handle is `-1`; every `root+0x70` element's type is in the observed `{0,2}` range and both gating curves have an in-range key count with all key times in a sane bound (this check does **not** require ascending-sorted key times, unlike `P`'s curves in §6.11 — `18_in_planelights`' repeating-tail curve, §7.5, is real data, not a bug, and a strict-sort gate would have failed on it); every `root+0x90` element's `+0x08/+0x0c` marker and `+0x20` null are exact; the `root+0x90` count fields and flag bytes show genuine cross-sample variation (not vacuously constant); and a negative control confirms the `root+0x90` marker test is offset-sensitive. `tools/harnesses/extract_cefct_samples2.py` and `tools/harnesses/find_cefct_big.py` (new) pulled the 10 samples themselves, mode-(a)-aware (via `vpp_modea.py`) though none of the 10 actually needed that path (§7.1).

### 7.8 What remains OPEN after this pass

1. **`Q`'s six sub-blobs' interior content** — the renderer/GPU-submission code that reads them was not located within this pass's anchor set (§7.2's scoped negative); `Q+0x60`'s lazy-rebase *mechanism* is now fully pinned down, but not what the resolved blob contains.
2. **Filter-class parameter blobs**, for every class except the trivial (all-zero) `0x7EDA2EE8` one — the blob layout convention is known (§7.3), but no real file in this project populates a non-trivial blob for classes `0x197AEC63`/`0x4A8FB92D`/`0x00AB1EC0`/`0x6BAB63EA`/`0xDB1A7FC6`, so their real parameter *values* are unvalidated.
3. **`0xDB1A7FC6`'s exact force formula** — structurally the same particle-list-walking family as the other four, but its body is far larger and was not traced to a confirmed mechanism.
4. **`0x7EDA2EE8`'s transform inputs and exact geometric meaning** — the mechanism (build and store a 3×4-ish block via `0x00da6c00` plus generic vector math) is confirmed; what the two inputs represent is not.
5. **Most of `root+0x40`'s and `root+0x90`'s remaining sub-pointer fields** — structurally present, spot-checked to resolve validly, individually undecoded.
6. **`root+0x40` element `+0x08`'s exact meaning** — a plausible (point/complexity-count) reading, not a decoded fact; no consumer found.
7. Every item already listed as OPEN in §5.8/§6.12 that this pass did not touch (the `MCKH` reader, `root+0x24`, `record+0x18`, `record+0x3C..0x57`, the `root+0x40`/`0x48` array's own root-level `+0x50/+0x58` cross-check mechanics, etc.) is unchanged.

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): struck §2's filename-location and name-table-length claims retracted by §5.2 (and the orphaned fragment in §5.2); marked resolved: §5.3 `root+0x18`, §5.6 `root+0x90` array, §5.7 item 3 (float #1 = duration), §5.8 item 5, §6.8 arrays, §6.10 filter classes; noted Team B's 502 `0xA1CC7F44` records at §6.6 and §6.12 item 3; reworded 2 `param_2` tokens.
