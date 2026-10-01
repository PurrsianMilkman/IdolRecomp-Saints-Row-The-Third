# Re-derivation from the executable: spec-tables-environment.md (Team A, 2026-10-01)

Inputs: the spec (`team-a/spec-tables-environment.md`), the desk review (`review/adv_tables-environment.md`), the index `exe_index/tables-environment.txt` (35 addresses, all dumped; bridge jobs 20261001T123123-team-a-ytgi and 20261001T123128-team-a-dvje). Dumps are depth-0 function dumps (listing + decompile + referenced globals) or xref listings; callees are not dumped unless they were a root of the same job. Where a verdict leans on a callee that is not dumped, the label is HIGH CONFIDENCE or OPEN, never "CONFIRMED — disassembly".

Labels: **CONFIRMED — disassembly** = read in these dumps. **CORRECTED** = the spec text is wrong; what is right follows. **OPEN** = the dumps do not settle it; the next-dump list at the end names the addresses.

Clean-room: own words only; element names and addresses (`0xXXXXXXXX`) are cited; no decompiled code, no auto-names.

---

## §1.2 Anchor map — re-verification by cross-reference

**Verdict: partially CONFIRMED — disassembly (5 of 27 table rows), the rest still OPEN (loaders not dumped).**

Evidence (literal pushed inside the dumped loader, callers/refs line of the dump):
- `weather_time_of_day.xtbl` literal `0x0111F08C` is pushed twice (file-exists test, then open) by `0x005A8F20`; `0x005A8F20` calls the segment reader `0x005A89F0`, which calls the cell initialiser `0x00BB6350`, which calls `0x00BA1600`. Row = CONFIRMED.
- `radial_blur.xtbl` literal `0x011238D4` pushed by `0x005CE140`; its only caller is `0x005CE8D0` (the one-time initialiser of §10). Row = CONFIRMED.
- `refraction_situations.xtbl` literal `0x0111E120` pushed by `0x0059EFB0`; callers `0x0059F260` and the DLC driver `0x0059F7B0`. Row = CONFIRMED.
- `external_light_override.xtbl` literal `0x011804C4` pushed by `0x00A72B80`, which calls the reset `0x00A723B0` then the generic reader `0x00A72AC0`; `0x00A72B80`'s single caller is `0x00707C80`. Row = CONFIRMED.
- `effects.xtbl`: `0x005C4B10` takes the file name as an argument (no literal inside); its callers are `0x005C5160` (DLC driver) and `0x005CE8D0`. `vfx.xtbl`: `0x005C3E70` is called only from `0x005C45D0`. `weather.xtbl`: the row reader `0x005A8510` is called only from `0x005A8D10`. `camera_free.xtbl`: `0x0056DCA0` is called only from `0x0056E4C0`. The loader→reader edges of these four rows are CONFIRMED; their filename literals were not in any dumped function.
- Not touched by any dump: the other 18 literal/loader pairs. Still OPEN (next-dump list, group A).

Spec text change: none to the table. New Review status line:
`**Review status (2026-10-01): re-derived from the executable (partial): 5 literal→loader rows (weather_time_of_day, radial_blur, refraction_situations, external_light_override, and the loader→reader edges of effects/vfx/weather/camera_free) CONFIRMED — disassembly; the remaining 18 rows NEEDS-EXE (loaders not dumped).**`

---

## §1.3 Node text for mixed content

**Verdict: OPEN.** The two child-lookup functions in the index (`0x00DABD20`/`0x00DABE80` call `0x00DC4FF0`; the loaders call its thunk `0x00DAB9E0`) only consult `+0x0C`; they do not set it. The text pointer is assigned by the parser: the open helper `0x00DAC9A0` calls the file driver `0x00DC5AC0`, which calls `0x00DC5820`, which calls the tokenizer/tree builder `0x00DC56B0` — the last is not dumped. (Incidental, CONFIRMED — disassembly from `0x00DC5820`: the parse fails with a formatted message carrying the line number when the file does not tokenise, when it holds no element, or when a tag is left unclosed.)

Spec text change: none. Review status: unchanged, add `(2026-10-01: the text rule lives in 0x00DC56B0, not dumped)`.

---

## §1.4 Accessor catalogue

### `0x00DACF60` vs `0x00DACF20` — **CONFIRMED — disassembly, relationship settled**
- `0x00DACF20(dest, node)`: reads the children named by the three one-character literals at `0x01121CB4`, `0x01121CB0`, `0x01121CAC` (bytes `0x58`, `0x59`, `0x5A` = `X`, `Y`, `Z`) from `node` itself into `dest[0..2]` with the always-write float accessor `0x00DACCB0`.
- `0x00DACF60(dest, node, "name")`: looks the named child up with `0x00DC4FF0`; if it exists, does exactly what `0x00DACF20` does on that child; if not, writes nothing. So `0x00DACF60` = "find the named child, then `0x00DACF20` on it". (`0x00DACFB0` and `0x00DAD010` also push the same three literals — two more vec3 variants, not dumped.)

Spec text change — in the `0x00DACF60` row, strike the OPEN note and replace with: `**[CONFIRMED — disassembly 2026-10-01: 0x00DACF60 finds the named child (0x00DC4FF0) and, only if it exists, calls the same X/Y/Z triple of 0x00DACCB0 that 0x00DACF20 applies to a node directly; 0x00DACF20(dest, node) is the "node is the vector" form, 0x00DACF60 the "named child is the vector" form.]**`

### Signed vs unsigned write-if-present integers — **CONFIRMED — disassembly**
- `0x00DABD20(dest, node, "name")`: if the named child exists (or, when `"name"` is `NULL`, the node itself) and has text, copies the text into a 1 KiB buffer, and if the first character is `-` parses from the second character and negates the result; writes `dest` and returns 1; otherwise leaves `dest` untouched and returns 0. **Signed, write-if-present.**
- `0x00DABE80`: identical shape without the `-` check — the text goes straight to the integer grammar `0x00DAB8B0`. **Unsigned, write-if-present.** (That a leading `-` yields 0 follows from the grammar stopping at the first non-digit, as the spec states; `0x00DAB8B0` itself is not dumped — HIGH CONFIDENCE for the "0".)
- Both accept a `NULL` child name, meaning "the node's own text" (the lookup is skipped). Not in the spec; add as a note.

### Which fields use which accessor — **partially CONFIRMED — disassembly**
Read in the dumps: `Start_Time` and `Ramp_Out_Time` (§3.1, `0x005A89F0`), `Priority` (§6.3, `0x005CE140`) and `Blocker/LifeSpan` (§10.2, `0x005C3E70`) all use **`0x00DABC70`** (signed, always-write — the signed one per `spec-tables-traffic-ai.md`; `0x00DABC70` itself is not dumped). `Audio_Occlusion` (§11.6): the caller list of `0x00DABE80` includes `0x006F7780`, the `bitmap_materials` loader — **unsigned, write-if-present**, as the spec says. Not settled (their readers are not dumped): rain `Density`, `On_Time`/`Off_Time`/`Variation`, `Life_Time`/`Fade_Time`, `rampin`/`duration`/`return`, `max_drunk_*`, `Skybox_Layer`, `TODRange` (narrowed, `0x00DABF20`).

### Float grammar edge cases — **CORRECTED (sign) + partially CONFIRMED — disassembly (`0x00DACB20`)**
- **CORRECTED:** the desk note "sign handling is in the accessor, not here" is wrong for floats. `0x00DACB20` itself tests the first character for `-` and negates the integer part, the fraction and the final result; so every float accessor built on it (`0x00DACCB0`, `0x00DACD40`, `0x00DACDE0`, `0x00DACE80` — its four callers) accepts a leading `-`. The desk note is right only for the integer accessors (§ above).
- Leading `.` accepted (integer part 0), and `-.5` works (sign, then `.`). CONFIRMED.
- A leading `0x`/`0X` (tested after the optional `-`) returns 0.0 — so `-0x10` is 0.0 too. CONFIRMED.
- Fraction: digits after the first `.` found anywhere in the text (a `strchr`-style search), each weighted by successive powers of 0.1 in single precision. Exponent: `e`/`E` right after the fraction digits, the rest converted by the C integer parser and applied as 10 to that power (`0x007D9B00`). CONFIRMED.
- Leading `+`, leading whitespace, a bare `-`: the grammar does not skip or accept any of these itself; what happens depends on `0x00DAB8B0` (not dumped). HIGH CONFIDENCE: `+1.5` and ` 1.5` give `0.5` (integer part parses as 0, the fraction is still found by the `.` search); a bare `-` gives 0. Mark these HYPOTHESIS until `0x00DAB8B0` is read.

Spec text change — in the "Engine float grammar" paragraph strike `**[Desk review 2026-09-30: sign handling is in the accessor, not here — per spec-tables-traffic-ai.md §1.3 only 0x00DABC70/0x00DABD20 accept a leading -; 0x00DABDF0/0x00DABE80 read it as 0.]**` and replace with `**[CONFIRMED — disassembly 2026-10-01: the float grammar 0x00DACB20 handles a leading - itself (every float accessor is signed); for integers the sign is handled by the accessor — 0x00DABD20 (and, per spec-tables-traffic-ai.md §1.3, 0x00DABC70) negate after a leading -, 0x00DABE80 (and 0x00DABDF0) pass the text straight to the grammar, which stops at the - and yields 0.]**` In the OPEN note that follows, strike `also the float grammar's handling of a leading +, leading whitespace and a bare - is not stated` and replace with `a leading +, leading whitespace and a bare - are not treated by the grammar itself and fall through to the integer parser 0x00DAB8B0 (not yet read; expected 0 for the integer part)`.

New Review status line (accessor catalogue): `**Review status (2026-10-01): re-derived from the executable: 0x00DACF60/0x00DACF20 relationship CONFIRMED; 0x00DABD20 signed / 0x00DABE80 unsigned CONFIRMED; float sign handling CORRECTED (in the grammar); Start_Time, Ramp_Out_Time, Priority, LifeSpan use 0x00DABC70 and Audio_Occlusion uses 0x00DABE80 (CONFIRMED); the other integer fields' accessors and the +/whitespace/bare-minus cases NEEDS-EXE (0x00DAB8B0 and the remaining row readers).**`

---

## §1.5 File-name skip rule (`0x0045BA50`, `0x0045BAB0`)

**Verdict: CORRECTED.**

Read in `0x0045BA50(dst, descriptor, base_name)`: (1) if bit `0x04` of the descriptor byte at `+0x103` is set, the destination gets the base name alone (format literal `0x0129A584` = `%s`); (2) otherwise the predicate `0x00DA73C0` is called with **only the descriptor's name string (`+0x80`)** — if it returns non-zero, again the base name alone; (3) otherwise `<name>_<base>` via the literal `0x0129EAAC` = `%s_%s`. Formatting is `0x00DA78D0` = bounded `vsnprintf` with a forced NUL at `dst[0x3F]` (size `0x40`). `0x0045BAB0(dst, descriptor, base)` is the same without step (1). **The base name is never examined**: there is no "already starts with the framework name" test. `0x00DA73C0` is a one-argument predicate on the framework name (not dumped; HYPOTHESIS: "name is empty").

Spec text change — strike `skipping the prefix when the framework descriptor's byte at +0x103 has bit 0x04 set or the base name already starts with the framework name` and the OPEN note; replace with `skipping the prefix when the framework descriptor's byte at +0x103 has bit 0x04 set, or when the predicate 0x00DA73C0 applied to the framework name alone (descriptor +0x80) is true (the base name is never examined; the predicate's meaning is OPEN — likely an empty-name test); the buffer is 0x40 bytes, NUL forced at the last byte. 0x0045BAB0 is the same formatter without the +0x103 test (used for the <framework>_vfx_containers name). **[CONFIRMED — disassembly 2026-10-01 for the two formatters; 0x00DA73C0 not read.]**`

New Review status line: `**Review status (2026-10-01): re-derived from the executable: file-name rule CORRECTED (no base-name test; a predicate on the framework name); NEEDS-EXE: 0x00DA73C0; NEEDS-DATA: Info_Slot_Index on base rows.**`

---

## §2 `weather.xtbl` offsets (`0x005A8510`) — bonus check

**Verdict: CONFIRMED — disassembly** for every leaf-element → offset pair in the table (`Chance` `+0x08` … `Audio_Intensity` `+0xC8`; `Next_Stage` count `+0xCC`, capacity `+0xD0`, array `+0xD4`, flag `+0xD8`; colours by `0x00DAD0A0` at `+0x48`, `+0x58`, `+0x64`, `Moon_Color_Multiply` by the if-present `0x00DAD130` at `+0x74`; `DisplayName` falls back to `Name` before `0x0084A1B0`). The group-element names (`Stage_Settings` etc.) are dropped by the decompiler in this dump and were not re-read; the `Chance` return value is likewise not visible. The `Next_Stage` sub-array rule (sized on first load, abandon the row if a later load has more) is CONFIRMED.

Spec text change: none. Review status: append `— leaf offsets re-derived from 0x005A8510 (2026-10-01, CONFIRMED — disassembly)`.

---

## §3.1 Segment level (`0x005A89F0`, `0x005A8F20`)

**Verdict: CONFIRMED (truncation, accessor) + CORRECTED (wrap boundary, `stage_count`).**

- `Start_Time` and `Ramp_Out_Time` are read with `0x00DABC70` (signed, always-write). CONFIRMED.
- `t/100` is a **signed integer division** (multiply-by-reciprocal sequence with the sign fix-up, i.e. truncation toward zero); the minutes are `t − 100·(t/100)`; the value is `((t mod 100)/60 + t/100)/24` in double, rounded to single. CONFIRMED — the integer-hour reading is right.
- **CORRECTED (wrap):** the loop subtracts 1.0 only while the value is **strictly greater than 1.0**; a value of exactly 1.0 (t = 2400) is kept as 1.0, not wrapped to 0. (Loop: compare, branch if ≤ 1.0, else subtract and re-test.)
- `Ramp_Out_Time`: integer, divided by 1440.0. CONFIRMED.
- **CORRECTED (`stage_count`):** in `0x005A8F20`, on the first load every segment's cell array (`+0x0C`) is allocated as **weather capacity (`0x0140E314`) × `0xE48`** bytes, 16-byte aligned; each cell is then constructed by `0x00B9E450` (register-passed cell pointer; not dumped), given the orbital vector `{data = cell+0x304, capacity 15, count 0}` at `+0x2F8`/`+0x2FC`/`+0x300`, and a zero back-pointer at `+0xE44`. The number of `Stage` rows plays no part in the size. On a **re-load** (segment array already present): if the new `Weather_Time_Segment` count exceeds the segment capacity `0x0140E318` the load is abandoned; otherwise every segment's `+0x00`/`+0x04`/`+0x08` is zeroed and every cell is re-constructed with `0x00B9E450` and its orbital count reset to 0 (the cell arrays are not re-allocated). CONFIRMED.
- Cell matching (`0x005A89F0`): for each `Stage`, `Stage_Name` is compared (`_stricmp`) with each weather entry's `+0x00` name in order; the first match gives the cell index `k` = (entry − base)/`0xDC`; the cell `+0x0C + k·0xE48` is passed to `0x00BB6350` together with its orbital vector and the `Stage` node, and the cell's `+0xE44` back-pointer is set to the weather entry. CONFIRMED. Hazard (HIGH CONFIDENCE, not stated): a `Stage` with no `Stage_Name` child hands a `NULL` string to `_stricmp`.

Spec text changes:
- `Start_Time` row: strike `the result is wrapped into [0,1] by repeated −1.0` and both bracketed notes; replace with `t/100 is a truncating (signed) integer division; the result is reduced by 1.0 while it is strictly greater than 1.0 (so t = 2400 gives exactly 1.0, not 0). Read with 0x00DABC70 (signed, always-write). **[CONFIRMED — disassembly 2026-10-01]**`.
- `Weather_Stages/Stage` row: strike `stage_count × 0xE48 bytes per segment` and the OPEN note; replace with `one cell per weather-stage slot: weather capacity (0x0140E314) × 0xE48 bytes, allocated once at first load; a re-load keeps the arrays and re-initialises every cell (0x00B9E450) **[CONFIRMED — disassembly 2026-10-01]**`.
- Segment-array paragraph: strike the bracketed desk note about "sized by the weather capacity" (now stated in the table) and add after "unmatched rows are ignored": `A re-load whose segment count exceeds the first load's capacity 0x0140E318 is abandoned before any row is read.`

New Review status line: `**Review status (2026-10-01): re-derived from the executable (0x005A89F0, 0x005A8F20): integer-hour truncation and signed accessor CONFIRMED; wrap boundary CORRECTED (1.0 kept); cell-array size CORRECTED to weather capacity 0x0140E314; re-load rule added. NEEDS-EXE: cell constructor 0x00B9E450.**`

---

## §3.2 The cell record — presence bits, orbital records, list origin

**Verdict: unassigned bits CONFIRMED (never set by `0x00BA1600`); orbital record gap and list origin still OPEN; 16th-object write-through-NULL CONFIRMED.**

Every presence-bit write in `0x00BA1600` (full listing read; sites at `0x00BA1665`–`0x00BA248C` plus the orbital sites `0x00BA2544`–`0x00BA2597`) maps to exactly the §3.3 assignment:
- `+0x2EC`: bits 0 (West4,3,2,1 in that order — last assignment wins), 1 (East4,3,2,1), 2 West_Zenith, 3 TOD_Light_Color, 4 Ambient_Color, 5 Back_Ambient_Color, 6 Window_Tint, 7 Fog_Color.
- `+0x2ED`: 0 particle_ambient_color, 1 particle_tod_light_color, 2 Fog_Ground, 3 Fog_Atmosphere_Scale, 4 Fog_Density, 5 Fog_Density_Offset, 6 ldr_min, 7 ldr_max.
- `+0x2EE`: 0 bloom_exposure … 7 eye_fade_min; `+0x2EF`: 0 eye_fade_max … 7 tonemap_lum_range (same order as §3.3).
- `+0x2F0`: 0 tonemap_lum_offset, 3 Overhead/Cloud_Front_Color, 4 Overhead/Cloud_Back_Color, 5 Horizon/Cloud_Front_Color, 6 Horizon/Cloud_Back_Color, 7 Horizon/Cloud_Layer_Strength. **Bits 1 and 2: no write site.**
- `+0x2F1`: 0 Horizon/Cloud_Normal_Map_Height, 1 Horizon/Cloud_Highlight, 2 Horizon/`?` (literal `0x012A2CBC` = the single byte `0x3F`), 3 Horizon/Storm_Strength, 4 Overhead/Cloud_Layer_Strength, 5 Overhead/Cloud_Normal_Map_Height, 6 Overhead/Cloud_Highlight, 7 Overhead/`?`.
- `+0x2F2`: 0 Overhead/Storm_Strength, 1 Mountain_Front_Color, 2 Mountain_Back_Color, 3 Mountain_Fog_Color, 4 Mountain_Fog_Density (`+0x1F4`), 5 Mountain_Normal_Map_Height, 6 Backlight_Power, 7 Backlight_Strength.
- `+0x2F3`: 0 Horizon/Cloud_Speed, 1 Overhead/Cloud_Speed. **Bits 2–7: no write site.**
- `+0x2F4`: 2 Ground_Reflection_Gloss, 3 Ground_Reflection_Brightness, 4 Ground_Reflection_Spec_Brightness (the loader literal, as §17.2 says), 5 Star_Strength, 6 Meteor_Strength, 7 Water/Ambient_Color. **Bits 0, 1: no write site.**
- `+0x2F5`: 0 Diffuse_Color1, 1 Diffuse_Color2, 2 Specular_Color, 3 Specular_Alpha, 4 Specular_Power, 5 Falloff_Color, 6 Water/Fog_Color, 7 Crest_Color.
- `+0x2F6`: 0 Crest_Threshold, 1 Ambient_RTPC, 2 Desired_Brightness, 3 Exposure_Min, 4 Exposure_Max, 5 LUT_Filename. **Bits 6, 7: no write site.**
So the 12 bits the desk review counted are exactly the bits this reader never touches; whether another reader (`0x00BA0140`, §15) sets them stays OPEN. Mechanism detail (CONFIRMED): for the nested groups the bit is **assigned** from the accessor's return (set or cleared — that is why the shared West/East bits reflect the last element), while the 21 direct `Stage` children **OR** their bit in.

Scalar offsets: every `0x00DACD40` destination in the dump equals the §3.3 offset (e.g. `+0x100`, `+0x104`, `+0x108`, `+0x10C`, `+0x19C`–`+0x1C0`, `+0x1F4`–`+0x218`, `+0x2A0`, `+0x2A4`, `+0x2D8`–`+0x2E8`; the 21 direct children `+0xE0`, `+0xF0`, `+0x110`–`+0x158`, read through the pointer table `0x01310F58`–`0x01310FA8`). CONFIRMED. Colour destinations are computed inside `0x00BA1510` (called with a counter pointer, the node and the name; the cell is register-passed) — **not re-verified** here (`0x00BA1510` not dumped).

Orbital objects (CONFIRMED — disassembly): `0x00BB6350` clears the 11 presence bytes (`+0x2EC`–`+0x2F6`) and the LUT byte `+0x220`, then for each of the `[0x0290FBB8]` global objects takes the next slot of the cell's vector — **if the count would exceed the capacity 15 the slot pointer is 0 and the following writes go through address 0** — writes name byte 0 = 0, the word at `+0xBC` = 0, `+0xB8` = `0xFFFFFFFF`, and copies `0x1E` bytes of name from `[0x0290FBB0] + 0x565 + i·0x5D0`; nothing else in the `0xC0`-byte record is initialised here. `0x00BA1600` then matches `Object_Name` (`_stricmp`) against the records, and reads `Tint` (literal `0x0111D1D4`, bit 5 of `+0xBC`), `Opacity` → `+0x70` (bit 7), `Scale` → `+0x6C` (bit 6); the `+0xB8` effect index is set from the name at `+0x75` through `0x005C50B0` only when bit 1 of `+0xBD` is set, which `0x00BB6350` has just cleared — dead path CONFIRMED.

Still OPEN: bytes `+0x1E`–`+0x5B` and `+0x74`–`+0xB7` of the orbital record are initialised by the cell constructor `0x00B9E450` if at all (not dumped). `0x0290FBB0` is an **object**, not a bare array: its `+0x00` is the data pointer the `+0x565 + i·0x5D0` names hang off, `+0x08` is the count (`0x0290FBB8`). No dumped function writes either; the xref list shows `0x00BB6400` passing the object's address and writing two of its fields (`0x029152F4`/`0x029152F8` = `+0x5744`/`+0x5748`, see §9.2) — `0x00BB6400` is the probable filler (HYPOTHESIS; it reads the vocabulary indices 82–93 of §15.2).

Spec text changes:
- Strike the OPEN note after the presence-bit paragraph; replace with `**[CONFIRMED — disassembly 2026-10-01: 0x00BA1600 has no write site for those 12 bits (2F0 bits 1–2, 2F3 bits 2–7, 2F4 bits 0–1, 2F6 bits 6–7); every other bit is set exactly as §3.3 lists. The nested-group bits are assigned (set or cleared) from the accessor's result, the 21 direct Stage children OR theirs in. Whether the mission-override reader 0x00BA0140 uses the 12 is OPEN.]**`
- In the orbital OPEN note replace `(the overflow condition) is unknown` with `is unknown; the record bytes outside +0x00–+0x1D, +0x5C–+0x73, +0xB8–+0xBD are untouched by 0x00BB6350/0x00BA1600 and can only come from the cell constructor 0x00B9E450 (not read)`; add `0x0290FBB0 is a list object (+0x00 data pointer, +0x08 count = 0x0290FBB8); its writer is not among the dumped functions (0x00BB6400 is the candidate).`

New Review status line: `**Review status (2026-10-01): re-derived from the executable (0x00BB6350, 0x00BA1600 in full, xrefs of 0x0290FBB0/0x0290FBB8): 12 unassigned presence bits CONFIRMED unassigned by this reader; scalar offsets CONFIRMED; 16-object overflow CONFIRMED; NEEDS-EXE: 0x00B9E450 (orbital record init), 0x00BB6400 (list origin), 0x00BA1510 (colour destinations).**`

---

## §4.1 Wind state machine (`0x005A9990`, `0x005A9640`, `0x005AA310`, `0x005A91A0`)

**Verdict: decoded from the decompiles — HIGH CONFIDENCE for the mechanism (helpers `0x005A95D0`, `0x00DAB6A0`, `0x006FF5D0`/`0x006FF440`, `0x00DAD960` not dumped); two CORRECTIONS to the accessor description.**

Working state: `0x0140E5A8` = pointer to a schedule of `0x24`-byte entries (per entry: `+0x00` wind-stage entry pointer, `+0x04` duration in milliseconds, `+0x08` heading in radians); `0x0140E5A0` = entry count; `0x0140E5A4` = current entry index; `0x0140E5AC`/`0x0140E5B0` = "forced stage" flag and pointer; `0x0140E5B8` = current heading; **`0x012E6BB0` = current intensity (written every tick; its image value 0.5 is only the initial value)**; `0x0140E5AE` = "intensity rising" byte; `0x0140E5AD` = "initialised" byte; `0x0140E5AF`/`0x0140E61C`/`0x0140E624`/`0x0140E5BC` = override flag, override direction (3 floats) and override scalar.

Per tick (`0x005A9990`):
1. Not forced and schedule empty: pick a stage (`0x005A95D0`), fill entries 0 and 1 with it; duration = random in `[avg − var·avg, avg + var·avg]` seconds (`Average_Duration` `+0x0C`, `Variance` `+0x10` as a fraction) × 1000 → entry 1; a clock helper `0x006FF440` is primed with a negative random fraction (≈ 0.48 × a second random × duration) of it; heading 0 = random in `[0, 2π)`, heading 1 = heading 0 ± π/4 random; count = 2, current = 1.
2. Not forced and schedule present: when the elapsed time (`0x006FF5D0`) exceeds the current entry's duration and more entries follow, advance and rewind the clock by that duration.
3. When the current entry is the last: move the last two entries to the front (current = 1, count = 2), pick a new target stage and call `0x005A9640(target)` to append a path; give every appended entry a heading = previous ± π/4 random; then, if all headings are negative add 2π to all, if all are ≥ 2π subtract 2π from all.
4. Forced (`0x0140E5AC` set): schedule = the forced stage twice, random duration, clock reset, count 2, current 1.
5. Output: blend factor from elapsed/duration shifted by ±0.5 and clamped to `[0, 1]`, smoothed by `0x00DAD960(t, 1.0, 0.5)`; intensity `0x012E6BB0` = lerp of the two stages' `Average_Intensity` (`+0x14`); heading `0x0140E5B8` = lerp of the two entries' headings; `0x0140E5AE` = (from-intensity < to-intensity).

`0x005A9640(target)`: builds one node per wind stage (`0x0140E594` count, `0x0140E59C` base, stride `0x28`) with a random duration (as above) and cost = +∞; identifies the current stage (last schedule entry) and the target; if they differ, runs a shortest-path search over `Next_Stage_List` edges (`+0x18` count, `+0x20` array of entry pointers) with edge cost = |Chance(u) − Chance(v)| × duration(v) (`+0x08` is the normalised `Chance`), walks predecessors from the target back, and appends the path's stages (pointer, duration × 1000 ms) to the schedule, bounded by the stage count.

Accessors — CORRECTED: `0x005AA310(dir_out, value_out, index)`: returns 0 unless `0x0140E5AD` is set and `index` ∈ [−2, 5]; always writes the wind direction into the first argument via `0x005A91A0` (override vector `0x0140E61C`/`0x0140E624` when `0x0140E5AF` is set, else `(sin θ, 0, cos θ)` with θ = `0x0140E5B8` — the second trig call is `0x00EA3A70` = cos per §17.3, the first `0x00EA2470` is by elimination sin, HIGH CONFIDENCE); then `index = −2` → the override scalar `0x0140E5BC` if overriding else **the current intensity `0x012E6BB0` (not a constant)**; `−1` → 0.0; `0..5` → the first float of pair `index` in the array at `0x0140E58C`.

Spec text changes: strike `index −2 returns the constant at 0x012E6BB0` → `index −2 returns the override scalar 0x0140E5BC when an override is active, else the current blended intensity 0x012E6BB0 (recomputed every tick by 0x005A9990; 0.5 in the image is only its initial value)`; strike `0x005AA310(?, out*, index) (returns 1 and writes a float; consults 0x005A91A0, which yields a 3-float wind direction …)` → `0x005AA310(dir_out, out*, index) (writes the 3-float wind direction to dir_out through 0x005A91A0 — the override vector, or (sin θ, 0, cos θ) of the current heading θ at 0x0140E5B8 — then one float to out*; returns 0 when the wind system is not initialised (0x0140E5AD) or index is outside −2…5)`. Strike `the stage-transition logic in 0x005A9990 was not decoded` and add a paragraph with the mechanism above, labelled **HIGH CONFIDENCE — decompile; helpers not read**.

New Review status line: `**Review status (2026-10-01): re-derived from the executable: accessor bodies CONFIRMED — disassembly (two corrections); transition logic decoded at HIGH CONFIDENCE (schedule of stage/duration/heading entries, Chance-weighted shortest path over Next_Stage_List); NEEDS-EXE: 0x005A95D0 (target choice), 0x00DAB6A0, 0x00DAD960, 0x006FF5D0/0x006FF440.**`

---

## §4.2 Wind vs tree wind

**Verdict: OPEN.** None of the tree-wind functions (`0x00851B10`, `0x00A70CA0`, `0x00A70D70`, `0x00A71250`, `0x00A71810`) nor the globals `0x0130CBB8`–`0x0130CBC4` is in the index. Incidental support (CONFIRMED — disassembly): the callers of the wind accessors read in these dumps are only `0x0059B9C0`, `0x005CB8B0`, `0x00741940` (for `0x005AA310`) and the per-frame tick `0x005AA170` (for `0x005A9990`) — none in the tree-wind range `0x00A6F000`–`0x00A71400`, consistent with the spec's negative result.

Spec text change: none. Review status: unchanged.

---

## §6.3 `radial_blur.xtbl` (`0x005CE140`)

**Verdict: CONFIRMED — disassembly.** Count `0x0148E8D8` reset to 0; per row: `Name` copied with bound `0x40` to `+0x00` (NUL forced at `+0x3F`); `Strength` `+0x44`, `Duration` `+0x48`, `Radius` `+0x4C`, `Distance_fade` `+0x50` (all `0x00DACCB0`); `Priority` `+0x54` via **`0x00DABC70` (signed, always-write)**; no capacity check. **Bytes `+0x40`–`+0x43` are written by nothing** in the loader — padding (the array is zero-fill static data, so they read 0).

Spec text change: strike the OPEN note; replace with `**[CONFIRMED — disassembly 2026-10-01: +0x40–+0x43 are not written by the loader (padding, zero in the static array); Priority is read with the signed always-write accessor 0x00DABC70.]**` In the table, `Priority` type → `s32 (0x00DABC70)`.

New Review status line: `**Review status (2026-10-01): re-derived from the executable (0x005CE140): layout, accessors and the +0x40 gap CONFIRMED — disassembly; capacity 200 remains layout-derived (HIGH CONFIDENCE).**`

---

## §7.2 DOF selection callers (`0x009E6510`, `0x00A93070`)

**Verdict: CONFIRMED — disassembly for the two names; `0x006948C0` still OPEN.**
- `0x009E6510` (a player zoom-mode toggle: enter when its second argument is non-zero, leave otherwise) hashes the literal `0x01176234` = **`Zoom`** once (cached in `0x0263B10C`, guard bit in `0x0263B110`) and calls the selector `0x0058EF60` on entry when the object is the local player (`0x0262EDFC`); on exit it calls the clear routine `0x0058EBC0`.
- `0x00A93070` (enters a remote-control gun mode) hashes the literal `0x01180F8C` = **`RC Gun`** (cached in `0x0278CAF8`) and calls `0x0058EF60`. The base `dof_situations.xtbl` has no `RC Gun` row (§17.1) — like `Satellite`, a no-op in base data (HIGH CONFIDENCE, from the §17.1 row list).

Spec text change: strike `two more callers (0x009E6510, 0x00A93070) select others (names not recovered)` → `0x009E6510 selects **Zoom** (and clears the situation on leaving zoom), 0x00A93070 selects **RC Gun** (no base-game row — a no-op, like Satellite) **[CONFIRMED — disassembly 2026-10-01]**`.

New Review status line: `**Review status (2026-10-01): re-derived from the executable: the four caller names are now all CONFIRMED — disassembly (Satellite, Fine Aim, Zoom, RC Gun); NEEDS-EXE: the graphics-option predicate 0x006948C0.**`

---

## §7.3 Per-frame DOF (`0x0058F2D0`, `0x0058F000`, globals `0x013C8740`/`0x013C8750`)

**Verdict: `T ≤ 0` branch and snap rule CONFIRMED — disassembly; subject point decoded at HIGH CONFIDENCE; camera-global conflict resolved (two copies written together) at HIGH CONFIDENCE.**

- Filter (`0x0058F2D0`): with `T` = row `+0x18`: if `T > 0`, `u_prev` and `u_target` are the mapped previous (`0x013CF4F8`) and target distances, `step = frame_time (0x0132A0AC) / T`; **if `step ≤ |u_prev − u_target|`** `u_prev` moves by `step` toward `u_target` and `d` is the inverse mapping of the result; **otherwise (difference smaller than the step) `d` is the target distance itself** (snap). **If `T ≤ 0`, no filtering: `d` = the target distance.** Outputs as in the spec; `0x013CF4F8` = the `d` actually used. CONFIRMED — disassembly (mapping and inverse as the spec states).
- Target distance (`d`): measured from the 3 floats at **`0x013C8740`/`0x013C8744`/`0x013C8748`** to a subject point chosen as follows: `0x0058F000(out_dist, player)` with the out-vector register-passed (the stack slot of a candidate point) scans the world object table (`0x03171A64`: count `+0x1EC`, index list `+0x1E4`, pointer table `+0x58`), skips the player and — when the player's `+0x16C0`/`+0x16C4` pair is non-zero — any object whose pair differs, requires flag bit 3 of the object's `+0x33` and a false `0x0096F4F0(object)`, projects the object's position (`0x0094BC80` → `0x00E47D00`) and keeps it only if its screen coordinates lie within half the screen (`0x012E6398`-scaled `0x005DCA00`/`0x005DCA10`) of the centre (`0x02A4E388`/`0x02A4E38C`); among those it keeps the one with the smallest squared distance (`0x0050CAF0`) from **`0x013C8750`**, writes that object's position (`+0x40`–`+0x48`) into the out-vector and returns the object (0 if none). If an object was found, `d` = distance from `0x013C8740` to that object's position; otherwise `d` = distance from `0x013C8740` to the point returned by `0x009CB3C0(out, 0, 0)` (not dumped — HYPOTHESIS: the camera's aim/look-at point). (A far point = 1000 × a vector from `0x004F00F0`/`0x00DA4130` is computed into the same stack slot but is overwritten before use whenever it would be used — effectively dead.)
- Camera globals: `0x013C8740` and `0x013C8750` are **two adjacent 16-byte vectors written together** by the same camera functions (xref lists: `0x00564C20`, `0x00565A40`, `0x00566620`, `0x00566A40`, `0x0057EB10`, `0x00580260`, `0x008134D0`, `0x00813A80`, `0x008140A0` write both). The one writer dumped, `0x00564C20(vec)`, stores the same new vector into **both** and records in `0x013C8738` the largest distance between the old `0x013C8740` and the new value. So the cutscene spec's "camera output position = `0x013C8750`" and this spec's use of `0x013C8740` are not in conflict: both hold the camera position after `0x00564C20`; whether the other writers ever make them differ (e.g. shake applied to one) is OPEN.

Spec text changes: strike the OPEN note and replace with: `**[CONFIRMED — disassembly 2026-10-01: with T ≤ 0 there is no filtering (d = target); the step is taken only when step ≤ |Δu|, otherwise d snaps to the target. The subject point is the position of the nearest on-screen "targetable" object found by 0x0058F000 (objects of the world table 0x03171A64 with bit 3 of +0x33, not excluded by 0x0096F4F0, projecting within half the screen of its centre; nearest to 0x013C8750), else the point returned by 0x009CB3C0 (not read — likely the aim point). 0x013C8740 and 0x013C8750 are two adjacent 16-byte camera vectors written together by the camera update functions (0x00564C20 stores the same vector into both), so the cutscene spec's 0x013C8750 and this section's 0x013C8740 do not conflict; HIGH CONFIDENCE that both are the camera position.]**` Replace `(the distance from the camera position 0x013C8740 to the situation's subject point; two candidate subject points are chosen between by 0x0058F000, not decoded)` with `(the distance from the camera position 0x013C8740–0x013C8748 to the subject point: the nearest on-screen targetable object's position when 0x0058F000 finds one, else the point from 0x009CB3C0)`.

New Review status line: `**Review status (2026-10-01): re-derived from the executable (0x0058F2D0, 0x0058F000, xrefs): T ≤ 0 and snap rule CONFIRMED — disassembly; subject point HIGH CONFIDENCE (0x009CB3C0 not read); camera-position conflict resolved (same vector written to both globals by 0x00564C20); NEEDS-EXE: 0x009CB3C0, the other writers of 0x013C8740/0x013C8750.**`

---

## §8 `refraction_situations.xtbl` (`0x0059EFB0`)

**Verdict: CONFIRMED — disassembly for the layout and `+0x3C`; two additions on the append/update bookkeeping.**

- Row element `refraction_situation`; `name` literal `0x012A2558` = `name` (lower-case); copied with bound `0x48` into a stack buffer and hashed with `0x00D9E8B0(text, 0, −1)` → `+0x00`. `Scale` `+0x04`, `Frequency` `+0x08`, `Offset_Delta` `+0x0C`, `Fade_In_Time` `+0x10`, `Fade_Out_Time` `+0x14` (`0x00DACCB0`); `Duration` `+0x18` write-if-present, else the float at `0x012A2D54` (= `0xBF800000`, −1.0) — CONFIRMED. `Spasm` absent → the eight dwords `+0x1C`–`+0x38` are set to 0; present → `Spasm_Time_Min` `+0x1C`, `Spasm_Time_Max` `+0x20`, `Frequency_Min` `+0x24`, `Frequency_Max` `+0x28`, `Scale_Min` `+0x2C`, `Scale_Max` `+0x30`, `Duration_Min` `+0x34`, `Duration_Max` `+0x38`. **`+0x3C` is written by nothing** — padding of the `0x40` stride (zero in the static array). Framework filter with default `main`, `NULL` filter accepts everything — CONFIRMED.
- Addition (CONFIRMED): in **append mode** the total count `0x013EF58C` is increased by the file's **whole** `refraction_situation` row count *before* the framework filter runs; rows the filter rejects are not written, so a filtered DLC file leaves that many zero (unused) entries at the end of the count. The base-game row count `0x013EF590` is recorded only when the array was empty or in update mode.
- Addition (HIGH CONFIDENCE — not exercised): in **update mode** the count is reset to the file's row count, each row is matched by hash among the first `count` entries, and a row with no match yields a `NULL` entry pointer that is then written through (the same crash shape as §7.1's update path).

Spec text change: strike `bytes +0x3C–+0x3F of the 0x40 stride are not described (padding or an unread field; 0x0059EFB0); and` → `bytes +0x3C–+0x3F are not written by the loader (padding) **[CONFIRMED — disassembly 2026-10-01]**;`. In the "Framework handling" paragraph after `rows are appended after the existing ones` add: `(the count is advanced by the file's full row count before filtering, so rows rejected by the framework filter leave unused zero entries inside the count)`; after `matched to existing entries by CRC instead of appended` add `(an unmatched row dereferences a NULL entry — not exercised)`.

New Review status line: `**Review status (2026-10-01): re-derived from the executable (0x0059EFB0): layout, defaults, +0x3C padding, framework filter CONFIRMED — disassembly; append-count and update-mode behaviour added; NEEDS-DATA: Spasm child census.**`

---

## §9.2 `time_of_day_objects.xtbl` defaults (`0x00BA2720`)

**Verdict: CORRECTED.** The default pair is not produced by stub functions and is not a build constant. `0x00BA2720(on_out, off_out)` reads two **HHMM integers from the current time-of-day definition object**: when the predicate `0x006E1D60()` is true (a mission override is active — HYPOTHESIS for the meaning) the object is `0x006E1D30() + 0x2F8`, otherwise the global object at `0x0290FBB0`; the two fields are at **`+0x5744`** (→ first output, globally `0x029152F4`) and **`+0x5748`** (→ second output, `0x029152F8`). Each is converted with the same integer-hour formula as §3.1/§9.2 (`(t/100 + (t mod 100)/60)/24`, truncating division, no wrap). The two globals are written only by `0x00BB6400` (xref), the reader of the TOD-definition vocabulary of §15.2 — so the defaults are **authored data** of that file (HYPOTHESIS: its `day_begin`/`day_end` elements, by the vocabulary names; not read). Which output the caller `0x005A12E0` uses as "on" is not in the dumps (`0x005A12E0` not dumped).

Spec text change: strike `returned by 0x00BA2720 (the two values are produced by a group of stub functions that return immediately — effectively constants of the build)` → `returned by 0x00BA2720: two HHMM integers read from the active time-of-day definition object (the global at 0x0290FBB0, or the mission-override object 0x006E1D30()+0x2F8 when 0x006E1D60() is true) at +0x5744 and +0x5748 (globally 0x029152F4/0x029152F8, written by the TOD-definition reader 0x00BB6400), each converted exactly like Start_Time (§3.1) **[CONFIRMED — disassembly 2026-10-01; the authored element names behind the two fields are OPEN — likely day_begin/day_end of §15.2]**`. Strike the OPEN note at the end of §9.2 and replace with `**[2026-10-01: the default pair comes from data, not code — see the loader paragraph; which of the two is "on" needs 0x005A12E0.]**`

New Review status line: `**Review status (2026-10-01): re-derived from the executable (0x00BA2720): default source CORRECTED (TOD-definition fields +0x5744/+0x5748, data not constants); NEEDS-EXE: 0x00BB6400 (which elements fill them), 0x005A12E0 (on/off order), 0x006E1D60/0x006E1D30.**`

---

## §9.3 `external_light_override.xtbl` (`0x00DA7890`, `0x00A72AC0`, `0x00A72B80`)

**Verdict: hash algorithm CONFIRMED — disassembly; reduction to the 128-entry index and the slot-1 consumer still OPEN.**

- `0x00DA7890(str)`: `NULL` → `0xFFFFFFFF`; else start at 0 and for each byte until NUL: sign-extend the byte to 32 bits, if it is `A`–`Z` add `0x20` (fold to lower case), then `hash = rotate-left-32(hash, 6) XOR byte`. (Bytes ≥ `0x80` are sign-extended, so they XOR in with the upper 24 bits set.) A prior spec-team note in the dump records the same routine as the engine-wide string hash of `spec-vpp-container.md` §2.2 / `spec-rig-format.md` §5.
- `0x00A72AC0(filename)`: file-exists test, open, for every `light_override` row: `Name` text pointer, then the colour reader `0x00A72720(row, "front_color", name, 1)`, `0x00A72720(row, "back_color", name, 1)`, the float reader `0x00A72A00(row, "front_intensity", name, 1)`, `0x00A72A00(row, "back_intensity", name, 1)` — the **slot argument 1** is CONFIRMED as a literal; free; set `0x02718E59` = 1. The `back_color × back_intensity` rule, the `sscanf` forms and the record creation (`0x008DF740`/`0x008DF690`) live in `0x00A72720`/`0x00A72A00` — not dumped, not re-verified. The bucket reduction (how the 32-bit hash selects one of the 128 index entries at `0x0130D130`) is in `0x008DF740`/`0x008DF690` — not dumped.
- Consumer of slot 1: `0x00A72B80`'s only caller is `0x00707C80` (a start-up/load routine); no reader of the records is in the dumps.

Spec text change: strike the OPEN note → `**[CONFIRMED — disassembly 2026-10-01: 0x00DA7890 = for each byte (A–Z folded to a–z, bytes sign-extended) hash = rotl32(hash, 6) XOR byte, starting from 0; a NULL string hashes to 0xFFFFFFFF. The reduction to the 128-entry index is inside 0x008DF740/0x008DF690 (not read).]**` Add after "The loader always passes slot 1": `(literal argument 1 at all four call sites — CONFIRMED)`.

New Review status line: `**Review status (2026-10-01): re-derived from the executable: hash algorithm CONFIRMED — disassembly; reader skeleton and the slot-1 literal CONFIRMED; NEEDS-EXE: 0x00A72720/0x00A72A00 (value parsing), 0x008DF740/0x008DF690 (bucket reduction), the slot-1 consumer.**`

---

## §10.1 `effects.xtbl` (`0x005C4B10`)

**Verdict: `+0x40`–`+0x43` CONFIRMED padding; duplicate-name policy settled at HIGH CONFIDENCE; four small additions.**

- Layout re-read: `Name` bounded copy `0x40` → `+0x00`; CRC `0x00D9E740(name, 0)` → `+0x44` (`0x0143CB7C` − `0x0143CB38`); **nothing writes `+0x40`–`+0x43`** → padding (zero in the static array). `scale_factor` `+0x64` write-if-present before `visual` is resolved; `+0x48` preset `0xFFFFFFFF`, then `0x005C43F0(visual text)`; the seven visual-gated bools and `vfx_kill_particles_fade_time` `+0x60` exactly as listed; `+0x4C` preset 0, `0x00462960(sound)`, the two switch names copied (bound `0x80`), split at the first byte `0x012A20A0` = `:`, both halves through `0x00462960` → `+0x50`/`+0x54` and `+0x58`/`+0x5C`; the three sound-gated bools; `Damage_Region` absent → `+0x68` = `[0x029C9964]`, `+0x6C` = 0, `+0x70`–`+0x87` = the three dwords at `0x029CDB98`/`0x029CDBA0` twice; present → `Region_Type` hashed with `0x00D9E8B0(text, 0, −1)`, `Region_Shape` compared with `Box`, `Region_Offset`/`Region_Size` through `0x00DACF60`; `Proximity_Refraction` absent → `+0x8C` = `[0x029C9964]`, `+0x90` = `+0x94` = 0; present → `Situation` hashed, `Near_Radius`, `Far_Radius`. Capacity: rows stop when the slot index exceeds `0x2A2`. Keep/drop: visual `−1` **and** sound 0 → look the hash up (`0x005C49D0`), write `0x7FFFFFFF` into its bucket, decrement `0x012E8BA4`; otherwise slot++, `0x0143CB28`++, `+0x89` bit 1. All CONFIRMED — disassembly.
- **Duplicate `Name` (HIGH CONFIDENCE — from the call pattern; `0x005C49D0`/`0x005C4970` not dumped):** after writing the name and CRC into the slot, the loader first calls the lookup `0x005C49D0(&bucket, &crc, &base)`; only when it does **not** report "found" does it call the insert `0x005C4970(&crc, &slot, &base)`. A second row with an already-registered name is therefore written into its own array slot but gets **no hash-table entry — name lookup keeps resolving to the first row**, and the second slot is unreachable by name (it still consumes a slot and is counted if it has a visual or sound). Corollary: a duplicate that has neither visual nor sound tombstones the **first** row's bucket (the lookup finds the first row's entry).
- Additions: (a) `Region_Shape` with any text other than `Box` leaves `+0x6C` untouched (not explicitly zeroed; zero in a fresh array). (b) A switch-name text without `:` writes neither half. (c) The row filter is skipped entirely when the framework argument is `NULL`. (d) A `Damage_Region` without a `Region_Shape` child, or a `Proximity_Refraction` without `Situation`, hands a `NULL` text to `_stricmp`/the hash (hazard, not exercised).

Spec text change: strike the OPEN note → `**[CONFIRMED — disassembly 2026-10-01: +0x40–+0x43 are not written (padding). Duplicate Name: the hash table is consulted before insertion; a repeated name is not inserted again, so lookups keep the first row and the later row occupies an unreachable slot (and, if it has neither visual nor sound, its drop path tombstones the first row's bucket) — HIGH CONFIDENCE, the lookup/insert helpers 0x005C49D0/0x005C4970 not read.]**` In the `Damage_Region/Region_Shape` row replace `any other text, or absent block → 0` with `absent block → 0; any other text leaves the byte untouched (0 in a fresh array)`.

New Review status line: `**Review status (2026-10-01): re-derived from the executable (0x005C4B10 in full): every offset, gating rule, default and the keep/drop rule CONFIRMED — disassembly; +0x40 padding CONFIRMED; duplicate-name policy HIGH CONFIDENCE; NEEDS-EXE: 0x005C49D0/0x005C4970; NEEDS-DATA: Damage_Region children, duplicate Names.**`

---

## §10.2 `vfx.xtbl` with no `LOD` (`0x005C3E70`)

**Verdict: CORRECTED (the question's answer) + layout CONFIRMED — disassembly.**

- `LOD` (literal `0x01122F78` = `LOD`) absent: the byte `+0xA0` is set to 0 and **nothing else in `+0xA1`–`+0xB0` is written** — those bytes keep their prior content, i.e. **0 in the zero-fill static array** (rows are only ever appended, never re-written). The `1.0e8`/`1.0e10` defaults are written **only** when `LOD` is present and `Spawning`/`Distance` are missing. So for the 401 real rows without `LOD`: `+0xA0` = 0 and `Fading_start`/`Fading_end`/`Spawning Distance` = 0.0, `View`/`Restore` = 0, `Update` byte = 0, `Minimum_time` = 0. A reader that keys on `+0xA0` never sees them; one that applies `OrDefault()` on any absence (Team B's reading) models a value the engine does not hold.
- `LOD` present (CONFIRMED): `+0xA0` = 1; `Spawning` absent → `+0xA8` = `0x4CBEBC20` (1.0e8), `+0xA1` = 0; present → `Distance` (always-write) and `View` (literal `0x01122F64`, always-write bool) ; `Distance` absent → `+0xAC` = `+0xB0` = `0x501502F9` (1.0e10); present → `Fading_start`, `Fading_end`, `Restore` bool `+0xA2`; `Update` absent → `+0xA3` = 0; present → `+0xA3` = 1, `Minimum_time` `+0xA4`.
- Also CONFIRMED: `+0x90` preset `0x10`; `VFX/Filename` (literal `0x0111DA8C` = `VFX`) → extension replaced from the five-entry table (`.cefct_pc` first) by `0x00DA8790` into `+0x41`; `Streaming_Category` → `0x005C3D40` enum at `+0x98`; a row is processed only when (category == `Preload`) equals the pass flag; `Cutscene` rows get no resource request (`+0x94` left 0); `Radius` preset 1.0 then write-if-present; `Radius_expands` preset true → bit `0x20`; `Start_time` preset 0; `Blocker` absent → `+0x88` = −1.0, present → bit `0x04`, `Opacity`, the debug message formatted into `0x029F5200`, `LifeSpan` via **`0x00DABC70` (signed, always-write)**; `Radial_blur/Radial_blur_entry` through `0x005CE2D0`; strict framework equality with `main` default.

Spec text change: strike the OPEN note in the `LOD/Distance/Fading_start` row → `**[CONFIRMED — disassembly 2026-10-01: when the whole LOD element is absent only +0xA0 = 0 is written; +0xA1–+0xB0 are left untouched (0 in the zero-filled array) — the 1.0e8/1.0e10 defaults apply only to a present LOD lacking Spawning/Distance.]**` Add to the `LOD` row: `absent → +0xA0 = 0 and nothing else written`. `Blocker/LifeSpan` type → `s32 (0x00DABC70, signed)`.

New Review status line: `**Review status (2026-10-01): re-derived from the executable (0x005C3E70 in full): layout, defaults, pass logic CONFIRMED — disassembly; LOD-absent case CORRECTED (fields stay 0, no defaults); Team B's OrDefault reading should be revisited.**`

---

## §12.2 `camera_free.xtbl` — submode reader (`0x0056B660`) and top reader (`0x0056DCA0`)

**Verdict: CONFIRMED — disassembly (Aspect filter, −1 skip, last-wins) with five additions; `melee_lock_group`/`sway_group` CONFIRMED not read by `0x0056DCA0`.**

- Top reader `0x0056DCA0` looks up exactly eight groups: `panning_group` (→ `0x00569FB0`), `asvct_group` (`default_time` `0x012E3F44`, `stationary_time` `0x012E3F48`, `stationary_threshold` `0x012E3F4C`), `follow_aggression_group` (`default_swing_rate` `0x012E3F50`), `hill_tracking_group` (`default_aggression` `0x012E3F54`, `genki_aggression` `0x012E3F58`), `miscellany_group` (→ `0x0056A4A0`), `submodes` (→ `0x0056B660`), `vehicle_fallbacks` (→ `0x0056A580`), `Vehicle_Aims` (→ `0x0056B880`). **No lookup of `melee_lock_group` or `sway_group`** — if they are read at all it is by the loader `0x0056E4C0` or elsewhere (OPEN). The flat-group floats use the always-write accessor on a possibly-`NULL` group node (hazard of §1.4).
- Submode reader `0x0056B660`, per `submode` row: (1) **Aspect filter, identical to `Vehicle_Aim`'s (§17.5):** the row is skipped when `Aspect` is `widescreen` and the display flag `0x01493661` is clear, or `standard` and the flag is set; any other text passes. (2) `name` (literal `0x012A2558`) matched by `0x00DAC830` against the 61-entry table `0x012E3D48`; **index −1 → the row is skipped** (no out-of-range write). (3) **No duplicate check**: a later row with the same name (that passes the filter) overwrites the same `0x40` record — last wins. (4) Destinations: `lookat_offset` (`0x00DACF60`, X/Y/Z) combined by `0x00DA38E0` into four dwords `+0x00`–`+0x0C`; `min_elevation` `+0x10` and `max_elevation` `+0x14` **multiplied by 0.017453 (degrees → radians)**; `base_fov` `+0x20`; `x_shift` `+0x34`; `default_elevation` `+0x1C` write-if-present — present → ×0.017453 and the byte `+0x18` = 1, absent → `+0x1C` = 0.0 and `+0x18` = 0; `z_dist` and `y_dist` are read **always-write** (`0x00DACCB0`) into locals and passed to `0x0056AD50(z, y)` — **not stored in the record** (what `0x0056AD50` does with them is OPEN); `blend_time` `+0x2C`; `override_exit_blend_time` bool `+0x30`.
- `0x00DAC830(names, count, node)` re-read: `NULL` node → −1; a `NULL` entry in the name table ends the scan with −1; `_stricmp` on the node's own text. (The submode reader passes a fourth argument that the function never reads.)

Spec text changes: strike the OPEN note after the 61-name list → `**[CONFIRMED — disassembly 2026-10-01: the submode reader applies the same Aspect filter as Vehicle_Aim (widescreen/standard against 0x01493661), skips rows whose name is not in the table (index −1), and lets a later row with the same name overwrite the record (no duplicate check). lookat_offset lands in +0x00–+0x0F; min_elevation/max_elevation/default_elevation are converted from degrees to radians; +0x18 is a "default_elevation present" byte; z_dist/y_dist are always-write reads handed to 0x0056AD50 and are not stored in the record.]**` In the group paragraph, after "eight named groups" add `(0x0056DCA0 looks up exactly these eight; it never names melee_lock_group or sway_group — their reader, if any, is the loader 0x0056E4C0 or another function, OPEN)`.

New Review status line: `**Review status (2026-10-01): re-derived from the executable (0x0056B660, 0x0056DCA0, 0x00DAC830): Aspect filter, −1 skip, last-wins, submode destinations and the degree→radian conversions CONFIRMED — disassembly; melee_lock_group/sway_group CONFIRMED absent from the top reader; NEEDS-EXE: 0x0056E4C0, 0x0056AD50, 0x00569FB0 (dampening names); NEEDS-DATA: real panning_group names.**`

---

## §13 Consumer of effects `+0x8C` (`Proximity_Refraction/Situation`)

**Verdict: OPEN.** No xref listing of the effects array and no candidate consumer is in the index; the writer side (`0x005C4B10`) is re-confirmed above (hash of `Situation` stored at `+0x8C`, radii at `+0x90`/`+0x94`).

Spec text change: none. Review status: unchanged.

---

## §15.2 Pointer-table range (`0x01310F10`, `0x01311084`)

**Verdict: CORRECTED — the end is `0x01311084`, not `0x01311074`.** The xref listings show `0x01310F10` (holding a pointer `0x0118C8E4`) read as the table base by `0x00BA0140` and `0x00BA4A10`, and **`0x01311084` (holding a pointer `0x0118C210`) read by `0x00BB6400`** — the reader the spec credits with the indices 82–93. So the table spans `0x01310F10`–`0x01311084` = 94 four-byte entries, matching the 94-name list. The strings themselves were not dumped (names 90–93 not re-verified).

Spec text change: in the §15.2 heading replace `0x01310F10–0x01311074` with `0x01310F10–0x01311084`; strike both bracketed notes → `**[CONFIRMED — disassembly 2026-10-01: the last entry 0x01311084 is read by the TOD-definition reader 0x00BB6400, the first by 0x00BA0140/0x00BA4A10; 94 entries.]**`

New Review status line: `**Review status (2026-10-01): re-derived from the executable (xrefs): table range CORRECTED to 0x01310F10–0x01311084 (94 entries); NEEDS-EXE: 0x00BA0140, 0x00BA4A10, 0x00BA25E0, 0x00BB6400 bodies; the cloud-layer naming stays HIGH CONFIDENCE.**`

---

## §11.1 / §11.5 capacities, §17.4 `0x00E21F70`

**Verdict: OPEN** — none of `0x012FC87C`, `0x012F7BF4`, `0x00E21F70` is in the index.

---

## Summary table

| Unit | Verdict | Evidence (dump) | Spec change |
|---|---|---|---|
| §1.2 anchor map | partial CONFIRMED (5 rows + 4 loader→reader edges); 18 rows OPEN | `0x005A8F20`, `0x005CE140`, `0x0059EFB0`, `0x00A72B80`, callers lines | status line only |
| §1.3 mixed content | OPEN | parser `0x00DC56B0` not dumped (`0x00DC5820`/`0x00DC5AC0` read) | none |
| §1.4 `0x00DACF60` vs `0x00DACF20` | CONFIRMED | `0x00DACF20`, `0x00DACF60` | OPEN note → confirmed note |
| §1.4 signed/unsigned | CONFIRMED (`0x00DABD20` signed, `0x00DABE80` unsigned); 5 fields pinned, rest OPEN | `0x00DABD20`, `0x00DABE80`, `0x005A89F0`, `0x005CE140`, `0x005C3E70` | accessor types for Start_Time, Ramp_Out_Time, Priority, LifeSpan, Audio_Occlusion |
| §1.4 float grammar | CORRECTED (sign in grammar) + partial | `0x00DACB20` | strike desk note, rewrite |
| §1.5 file-name rule | CORRECTED | `0x0045BA50`, `0x0045BAB0` | rewrite the bullet |
| §2 weather offsets | CONFIRMED (bonus) | `0x005A8510` | status line |
| §3.1 segment | CONFIRMED (truncation, accessor) + CORRECTED (wrap at 1.0; cell array = weather capacity) | `0x005A89F0`, `0x005A8F20` | two rows + paragraph |
| §3.2 cell record | CONFIRMED (12 bits unassigned; overflow); OPEN (record gap, list origin) | `0x00BB6350`, `0x00BA1600`, xrefs | two notes |
| §4.1 wind machine | decoded HIGH CONFIDENCE; 2 CORRECTIONS (`0x012E6BB0`, accessor args) | `0x005A9990`, `0x005A9640`, `0x005AA310`, `0x005A91A0` | rewrite accessor sentences, add mechanism |
| §4.2 tree wind | OPEN | — | none |
| §6.3 radial_blur | CONFIRMED (+0x40 padding, Priority signed) | `0x005CE140` | note + type |
| §7.2 DOF callers | CONFIRMED (`Zoom`, `RC Gun`); `0x006948C0` OPEN | `0x009E6510`, `0x00A93070` | sentence |
| §7.3 DOF per-frame | CONFIRMED (T ≤ 0, snap); HIGH CONFIDENCE (subject point, camera globals) | `0x0058F2D0`, `0x0058F000`, xrefs `0x013C8740`/`0x013C8750`, `0x00564C20` | rewrite note + sentence |
| §8 refraction | CONFIRMED (+0x3C padding); 2 additions | `0x0059EFB0` | note + 2 sentences |
| §9.2 TOD-object defaults | CORRECTED (data, not stubs) | `0x00BA2720` | rewrite loader sentence |
| §9.3 hash | CONFIRMED (algorithm); OPEN (reduction, consumer) | `0x00DA7890`, `0x00A72AC0`, `0x00A72B80` | note |
| §10.1 effects | CONFIRMED (+0x40 padding, all offsets); HIGH CONFIDENCE (duplicates) | `0x005C4B10` | note + row |
| §10.2 vfx LOD-absent | CORRECTED (fields stay 0) | `0x005C3E70` | note + row + type |
| §12.2 camera_free | CONFIRMED (filter, −1, last wins, conversions); melee/sway not in top reader | `0x0056B660`, `0x0056DCA0`, `0x00DAC830` | note + sentence |
| §13 effects +0x8C consumer | OPEN | — | none |
| §15.2 pointer table | CORRECTED (end `0x01311084`) | xrefs | heading + note |
| §11.1/§11.5/§17.4 | OPEN | not dumped | none |

Team B impact (from the corrections): (1) `vfx` rows without `LOD` hold zeros, not 1e8/1e10 (§10.2); (2) `Start_Time` 2400 → 1.0, not 0 (§3.1); (3) `time_of_day_objects` defaults are data from the TOD-definition file (§9.2); (4) float grammar is signed for every float field (§1.4); (5) submode `min/max/default_elevation` are degrees→radians and `z_dist`/`y_dist` are always-write (§12.2).

## Next-dump list

Group A — §1.2 literals/loaders not yet seen (func dump, depth 0): `0x005A8D10`, `0x005A93C0`, `0x005A9260`, `0x0059B570`, `0x005993B0`, `0x005991C0`, `0x005A10E0`, `0x0059A800`, `0x0058EC80`, `0x005C36D0`, `0x005A12E0`, `0x005A11C0`, `0x007AC920`, `0x007474F0`, `0x007471F0`, `0x005FD580`, `0x0058BD30`, `0x007549F0`, `0x006F7780`, `0x005D2370`, `0x007D8FB0`, `0x0057C290`, `0x0056E4C0`, `0x00584900`, `0x005CE8D0`, `0x005C5160`, `0x005C45D0`, `0x005CE760`.
Group B — accessors/grammar: `0x00DAB8B0` (integer grammar: `+`, whitespace, bare `-`), `0x00DABC70`, `0x00DABDF0` (plus their xref/caller lists to pin the remaining integer fields), `0x00DACCB0`, `0x00DACD40`, `0x00DC4FF0`, `0x00DC56B0` and `0x00DC52B0` (parser: node text rule for mixed content), `0x00DA73C0` (file-name predicate).
Group C — weather_time_of_day: `0x00B9E450` (cell constructor: orbital record init), `0x00BB6400` and `0x00BA8540`, `0x00BB6C90` (orbital list `0x0290FBB0` origin; the `+0x5744`/`+0x5748` day fields), `0x00BA1510` (colour destinations), `0x00BA0140`, `0x00BA4A10`, `0x00BA25E0` (mission-override readers; the 12 presence bits), xref of `0x029152F4`, range read of `0x01310F10`–`0x01311084` strings.
Group D — wind: `0x005A95D0` (target stage choice), `0x00DAB6A0`, `0x00DAD960`, `0x006FF5D0`, `0x006FF440`, `0x005AA170`, `0x005AA1E0`, `0x00EA2470` (sin check); §4.2: xrefs `0x0130CBB8`–`0x0130CBC4`, `0x00A70CA0`, `0x00A71250`.
Group E — DOF: `0x009CB3C0` (fallback subject point), `0x006948C0` (graphics predicate), `0x00566620`/`0x00566A40` (do the two camera vectors ever differ), `0x0058EF60`, `0x0058EC40`.
Group F — effects/vfx/refraction: `0x005C49D0`, `0x005C4970` (hash lookup/insert — duplicate policy), `0x005C50B0`, `0x005C43F0`, `0x005C3D40`, xref listing of `0x0143CB38`-range reads at `+0x8C` (§13), `0x0059F7B0`.
Group G — external_light_override: `0x00A72720`, `0x00A72A00`, `0x008DF740`, `0x008DF690`, `0x00707C80`, xref `0x02718E59` (slot-1 consumer).
Group H — camera_free: `0x00569FB0` (dampening names), `0x0056AD50` (z_dist/y_dist), `0x0056A4A0`, `0x0056A580`, `0x0056B880`, `0x0056E4C0` (melee_lock_group/sway_group).
Group I — misc: `0x005A12E0`, `0x006E1D60`, `0x006E1D30` (§9.2), `0x00E21F70` (§17.4), reads of `0x012FC87C`, `0x012F7BF4` (§11 capacities).
