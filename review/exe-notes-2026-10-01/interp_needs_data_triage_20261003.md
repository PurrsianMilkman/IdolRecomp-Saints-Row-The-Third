# NEEDS-DATA triage across the Team A format specs (2026-10-03)

Investigation only. **No spec file was edited.** This note inventories every live `NEEDS-DATA` marker in the
format specs (`team-a/spec-*.md`, the Lua-binding specs excluded by scope), re-reads what each one actually asks
for, and sorts them into four groups: settled from the executable this pass, desk-only (mislabelled, needs
neither data nor the executable), already settled elsewhere (stale marker), and genuinely needing Team B's data.

Method:

- Every line containing `NEEDS-DATA` in every `spec-*.md` was listed with its nearest section heading
  (174 occurrences in 39 files). Front-matter summaries, tally lines and changelog lines were set aside; the
  remaining unit-level status lines were merged where one unit carries two status lines (a 2026-09-30 desk line
  and a later re-derivation line). Two units whose marker is struck or explicitly closed were dropped (listed in
  §1.2).
- Executable work ran locally and read-only through `ghidra/CrreishDump.java` (`func`, `range`, `ptrs`, `xref`,
  `calls`, `str` modes) against a disposable private copy of the Ghidra project (`tools/gp_needsdata`, deleted
  after the run). Raw dump text stayed in the session scratchpad and is not reproduced here; everything below is
  described in our own words with plain hex addresses.
- **Hold:** nothing below opens or depends on the parked `.czn_pc` object-placement / property-stream interior
  or named-object resolution. Three zone-data items sit next to it and are flagged in §6.

Labels follow house style: CONFIRMED — disassembly, HIGH CONFIDENCE, HYPOTHESIS, OPEN.

## 0. Headline

| Group | Items |
|---|---|
| Live NEEDS-DATA questions found | **112** distinct questions in 113 unit status lines (one line is a cross-reference); plus 5 items that appear only in front-matter "awaiting data" lists, §1.3 |
| Fully settled from the executable this pass | **3**: tables-customization §15.2, tables-ui-controls §12, customization-data §2.2 |
| Executable half settled this pass, a smaller data residue left | **7**: customization-data §2.1, tables-diversions §3, vehicle-data §7.1, terrain §2, terrain §5 item 1, anim §2, geometry §5.1 |
| Desk-only, mislabelled (no data or executable needed) | **1**: mission-packages §3.2 |
| Already settled elsewhere, marker stale (whole or part) | **2 whole/part units**: ctorless-types §2 (whole), tables-progression §14 (the §14.6 part); plus stale halves inside customization-data §2.1 and tables-diversions §3 |
| Genuinely need Team B data access | **100** (of which 3 zone-data items are hold-adjacent, §6) |

Two findings go beyond what the markers asked and matter to implementers:

1. **`customization_outfits.xtbl` `Default_Wear_Option` is matched against the wear option's male mesh
   filename, not its `Name`** (§2.3). `spec-tables-customization.md` §5.1's "resolved against that item's own
   `Wear_Option` list" is right about the list but the key is the filename.
2. **The `.cvtf_pc` header is 48 bytes, not 16, and the field the spec reads as `entry_count_2 = 40` is
   (HIGH CONFIDENCE) the Region-1 table offset, which happens to be 0x28 = 40 in the worked sample** (§2.9).

## 1. Inventory of live NEEDS-DATA markers

Disposition codes: **EXE** settled from the executable this pass; **EXE+D** executable half settled, data
residue left; **DESK** wording only; **STALE** settled elsewhere; **DATA** needs Team B data; **DATA\*** needs
data and is hold-adjacent.

### 1.1 Unit-level markers

| # | Spec § (line) | What it asks for | Disp. |
|---|---|---|---|
| 1 | ai-behavior §2 (l.44) | per-archive `.xtbl` census with the §7 reader | DATA |
| 2 | ai-behavior §3 intro (l.57) | the horde-area claim (`steel_mill_area` vs 3 horde levels) | DATA |
| 3 | ai-behavior §3.2 (l.101) | re-count the 21 records with a real parser; join 5 `Behavior` names to `ai_behavior.xtbl` | DATA |
| 4 | anim §2 (l.58) | contents of `+0x2C`/`+0x34` (`+0x38..+0x3F`); the 271-clip `+0x06` residue; acceptance-function address | EXE+D |
| 5 | anim §6c.3 (l.557) | replay of the 4 bit-`0x01`-clear clips | DATA |
| 6 | asm §9.3 (l.278) | kind-29/30 counts twice `spec-world-streaming.md` §10.2 | DATA |
| 7 | audio §2 (l.58) | 253 vs 251 `sounds.vpp_pc` entries | DATA |
| 8 | audio §9.1 (l.226) | `.mbnk_pc` chain walk on `soundboot.vpp_pc` entry 1 | DATA |
| 9 | audio §9.2 (l.232) | count of 550 entries beginning `DMLV` | DATA |
| 10 | conversation §2 (l.37) | files per voice family | DATA |
| 11 | ctorless-types §2 (l.48) | zero-byte `.gzn_pc` count, 1 vs 81 | STALE |
| 12 | customization-data §2.1 (l.45) | parent of `Shader_Type`; level of `Default_Colors_Grid` | EXE+D (part STALE) |
| 13 | customization-data §2.2 (l.58) | what `Default_Wear_Option` refers to | EXE |
| 14 | customization-data §4 item 5 (l.107) | how many of 621 rows carry non-empty wear-option flag lists | DATA |
| 15 | cutscene-camera §2 (l.40) | where the 96 duplicate entries live | DATA |
| 16 | cutscene-camera §11 (l.331; also cited from §10.6 l.243) | 3.98 s smallest gap vs median/p90; re-run gap histogram | DATA |
| 17 | effects §5.4 (l.165) | `record+0x10` tag histogram per array | DATA |
| 18 | effects §5.5 (l.179) | tile the filter array at stride `0x70` | DATA |
| 19 | effects §5.6 (l.190) | `root+0x90` stride `0x228` (62 elements vs 478 occurrences) | DATA |
| 20 | effects §6.7 (l.404) | P-block field census incl. type 5 ×712 | DATA |
| 21 | effects §6.10 (l.427) | filter record census and blob dumps | DATA |
| 22 | effects §6.11 (l.441) | region tiling gate with the corrected end rule | DATA |
| 23 | effects §7.4 (l.537) | `+0x80 == -1` and `+0x08` over 346 occurrences | DATA |
| 24 | effects §7.5 (l.559) | type/cookie census, `0xC8` tiling over 595 | DATA |
| 25 | effects §7.6 (l.570) | marker, `+0x20 == -1`, `0x228` tiling over 478 | DATA |
| 26 | fxo §7.1 (l.202) | flag/index census | DATA |
| 27 | fxo §7.3 (l.275) | T0/T4 layouts; the 8 register-label mismatches | DATA |
| 28 | fxo §8.2 (l.344) | geometry-stage typing of the middle blobs | DATA |
| 29 | geometry §3.1.1 (l.195) | array-4 population check | DATA |
| 30 | geometry §4.1.5 (l.247) | array-3 record gap, array-1 coverage, pointer-slot second dwords | DATA |
| 31 | geometry §5.1 (l.318) | Region-1 entry start / leading quads; 372 vs 744; Region-2 40 vs 45 | EXE+D |
| 32 | geometry §5.2 (l.340) | no real Character-cvtf sample | DATA |
| 33 | low-mips §1 bullet 2 (l.26) | `high_mips.vpp_pc` header re-dump | DATA |
| 34 | low-mips §5 (l.82) | histogram of `0x164` over all containers | DATA |
| 35 | mission-packages §1 (l.21) | header `0x14C` of top archive and 15 nested; entry counts | DATA |
| 36 | mission-packages §3.2 (l.61) | "first worked example" cross-reference (size already backed) | DESK |
| 37 | mission-packages §5 (l.88) | top-level counts 1,033 / 1,516; DLC listing | DATA |
| 38 | morph §3.3 (l.76) | per-entry `n` histogram / Σ vs 45,243; `id` vs a name hash | DATA |
| 39 | morph §12.1 (l.269) | populations of 56,822 / 170,466; the 80% `max\|q\|` gap | DATA |
| 40 | morph §15.3 (l.1048) | 8-vs-30 trial conflict; non-compact fraction by `N` | DATA |
| 41 | morph §15.5 (l.1113) | 100/100 denominator for a 95-target file; head-region conflict | DATA |
| 42 | physics §4.4.2(f) (l.147) | single-file lead, population check | DATA |
| 43 | physics §4.4.6(i) (l.523) | post-fix per-archive resolution figures | DATA |
| 44 | render §20.12.2 (l.869) | CTAB over all 10 blobs of `ir_sr3carpaint_gr_v.fxo_pc` | DATA |
| 45 | render §20.12.3 (l.875) | DEF/DEFI/DEFB-vs-CTAB collision check per blob | DATA |
| 46 | render §20.12.5 (l.903) | `projTM` semantics from VS bytecode | DATA |
| 47 | render §22.11 (l.1210) | full per-stem census over 393 files | DATA |
| 48 | resource-dispatch §7 item 5a (l.173) | paired-with-secondary-0 / unpaired-with-nonzero counts | DATA |
| 49 | rig §11.5.1 (l.689) | 66,822 = 3 × 22,274 live lanes | DATA |
| 50 | rig §11.18.1 (l.2237) | array 4 = `0..67` over every `.ccmesh_pc`/`.csmesh_pc` | DATA |
| 51 | save §9.3 (l.627) | flag-word bits 3/4 from one anomalous snapshot? | DATA |
| 52 | save §9.6 (l.655) | `07` targets 19 vs 22 | DATA |
| 53 | save §10.5 (l.764) | crib-stash tally covers 15 of 16 | DATA |
| 54 | save §12.4.3 (l.1054) | both tallies cover 15 of 16 | DATA |
| 55 | save §12.5 (l.1087) | set-2 tally 17–18 of 16 | DATA |
| 56 | save §12.9 (l.1190) | four slot pairs vs §11.5's substitution rule | DATA |
| 57 | save §12.10.1 (l.1198) | tally covers 14 of 16 | DATA |
| 58 | save §12.10.3 (l.1221) | tally covers 14 of 16 | DATA |
| 59 | save §12.10.4 (l.1230) | entry-12 tally covers 15 of 16 | DATA |
| 60 | tables-customization §15.2 (l.484) | vec2 vs vec3 for `Specular_bright`/`Specular_dark` | EXE |
| 61 | tables-diversions §2.2 (l.113/115) | 9 vs 11 named fields | DATA |
| 62 | tables-diversions §2.4 (l.125/127) | 12 vs 15 named fields | DATA |
| 63 | tables-diversions §2.5 (l.133) | 8 vs 10 named fields | DATA |
| 64 | tables-diversions §2.7 (l.143/145) | 9 vs 10 named fields | DATA |
| 65 | tables-diversions §2.10 (l.163) | 12 vs 14 named fields | DATA |
| 66 | tables-diversions §3 (l.173) | 11 vs 12 named fields; record base singleton; value at `0x011348c0` | EXE+D (part STALE) |
| 67 | tables-environment §1.5 (l.139/141) | `Info_Slot_Index` on base rows | DATA |
| 68 | tables-environment §3.3 (l.241) | nested element-path census of 4 real segments; `Fog_Density` spelling | DATA |
| 69 | tables-environment §8 (l.470/472) | `Spasm` child census | DATA |
| 70 | tables-environment §10.1 (l.582/584) | the 4 `Damage_Region` rows' children; duplicate `Name`s | DATA |
| 71 | tables-environment §12.1 (l.813) | maximum `Strength_Element` count | DATA |
| 72 | tables-environment §12.2 (l.826/830) | real `panning_group` names | DATA |
| 73 | tables-environment §17.9 (l.1073) | hash of both base `effects.xtbl` copies | DATA |
| 74 | tables-progression §2.1/§2.2 (l.138) | percent-denominator file order and case | DATA |
| 75 | tables-progression §3.2/§3.3 (l.259) | requirement / back-pointer caps on real rows | DATA |
| 76 | tables-progression §4.6 (l.459) | do types 14/23 occur in real data | DATA |
| 77 | tables-progression §14 (l.1329) | §14.1 / §14.6 / §14.12 count conflicts | DATA (§14.6 part STALE) |
| 78 | tables-ui-controls §2.3 (l.93) | Table B key pairing against real saves | DATA |
| 79 | tables-ui-controls §3 / §3.2 (l.99/110) | per-type split of 13 rows; X360-false count | DATA |
| 80 | tables-ui-controls §5 (l.179) | 154/165 CBA and 34/34 CAA unions | DATA |
| 81 | tables-ui-controls §8.1 (l.307) | element-vs-attribute check in the real file | DATA |
| 82 | tables-ui-controls §11 (l.370) | every shipped row has `Controls` | DATA |
| 83 | tables-ui-controls §12 (l.391) | units of `Min_delay`/`Max_delay` | EXE |
| 84 | tables-vehicle-world §3.3 (l.126) | the 52-row patch copy | DATA |
| 85 | tables-vehicle-world §5.2 (l.197) | all 51 `Front_Rim`/`Rear_Rim` values exist as `Component` names | DATA |
| 86 | tables-weapons-combat §10.2 (l.709) | reject rules on the 3 real rows; `Daedalus` element placement | DATA |
| 87 | terrain §2 (l.26) | are the `cc:N` strings counted in the slot count (56 vs 58) | EXE+D |
| 88 | terrain §5 item 1 (l.64) | `cc:N` census, **or the code that parses the `cc:` prefix** | EXE+D |
| 89 | terrain §5.3 (l.92) | material-id range on 535 channels; stem-group test | DATA |
| 90 | vehicle-data §7.1 (l.147) | 106 base files vs 104 base slots; base `start` | EXE+D |
| 91 | vehicle-data §7.2 (l.159) | `Group` coverage | DATA |
| 92 | vertex §12.1 (l.916) | re-run on the corrected population | DATA |
| 93 | vertex §12.2 (l.948) | re-run on the corrected population | DATA |
| 94 | vertex §12.3 (l.1036) | re-run code 24 on 535 channels | DATA |
| 95 | vertex §12.5 (l.1200) | 832,194 vs 3,405,780 denominators; code-101 tangent 4th-byte histogram | DATA |
| 96 | vertex §12.9 (l.1584) | `+0` versus `+16` | DATA |
| 97 | vertex §12.12 (l.2191) | shuffled-bbox control, face-normal test, `+6`/`+22` histograms | DATA |
| 98 | vertex §12.13 (l.2238) | item 3 on 535 channels | DATA |
| 99 | vpp-container §2 (l.86) | raw entries in 68 mixed mode-(b) containers; `+0x04`/`+0x14` zero on all | DATA |
| 100 | vpp-container §2.1 (l.95) | `0x160` vs Σ(name length + 1) | DATA |
| 101 | vpp-container §6 (l.305) | census of `0x150` and `0x164` | DATA |
| 102 | world-streaming §1 (l.19) | full-population mode-(b) count | DATA |
| 103 | world-streaming §2 (l.25) | 360 `sr3_city_0` pairs vs 331 manifests; disjointness | DATA |
| 104 | world-streaming §5.1 (l.86) | `sr3_city.grid_pc` preamble, section gaps, trailer | DATA |
| 105 | world-streaming §10.2 (l.959) | 2× record counts vs asm §9 | DATA |
| 106 | xtbl §3.2 (l.65) | entries carrying `TableDescription` etc. over 2,222 | DATA |
| 107 | xtbl §3.3 (l.71) | per-file ratio of rows with `_Editor` | DATA |
| 108 | xtbl §6.4 (l.164) | per-parent counts; census over 334 `.cte_xtbl` | DATA |
| 109 | xtbl §8 (l.188) | shape-count reconciliation by archive | DATA |
| 110 | zone-data §2 (l.60) | alignment-table labelling; EOF / error definitions (D4) | DATA\* |
| 111 | zone-data §10.1 (l.627) | `p ≡ 2 / 6` (D2) | DATA\* |
| 112 | zone-data §10.8 (l.965) | walker failure cause (D10); 921 vs ~915 (D13); odd parity | DATA\* |
| 113 | (cross-ref) cutscene-camera §10.6 (l.243) | points at row 16; counted there | — |

Row 113 is a pointer, not a separate question, so the live total is **112 distinct questions in 113 status
lines**. Counted by question: 3 EXE, 7 EXE+D, 1 DESK, 1 whole STALE, 100 DATA (3 of them DATA\*).

### 1.2 Markers found but not live

- `spec-tables-animation.md` §3.2 (l.175): the NEEDS-DATA on the `<Trigger>` shape is struck and replaced by
  VALIDATED-BY-DATA (job `…-kmsh`).
- `spec-tables-vehicle-world.md` §6.2 (l.239): says "no NEEDS-DATA item remains".
- `spec-tables-traffic-ai.md`, `spec-tree-format.md`, `spec-extensionless-types.md`,
  `spec-format-inventory.md`: the only hits are "NEEDS-DATA 0" in the front matter.

### 1.3 Front-matter-only "awaiting data" items (no unit status line carries the marker)

Listed for completeness, not counted above: audio §4.2 (`extra` placement, `tag` uniqueness, marked optional);
low-mips §3 (`.cvbm_pc` c-file size histogram for "104–132 bytes"); mission-packages §4 (`source_name` length of
the 13 zero-entry records; same-name collisions); xtbl §3.4 (vector-encoding census); geometry front-matter list
(§3.1 zero fields, §4 `g` bytes 4–15, §4.1 arrays 5/6 gates). All are data questions.

## 2. Settled from the executable this pass

### 2.1 tables-customization §15.2 — `Specular_bright`/`Specular_dark` are three-component colours (vec3) — **EXE**

In the hair-colour reader 0x00835cd0, `Specular_bright` is parsed into record `+0x30` and `Specular_dark` into
record `+0x3C` (call sites 0x00835f05 and 0x00835f14). Both go through 0x00dad130, a two-step helper: it looks
up the named child element (0x00dc4ff0) and, if present, hands the child to 0x00dad0a0. That routine reads three
child elements named by one-character strings at 0x01121ccc, 0x01121cf8 and 0x01121d0c — the bytes read back as
`R`, `G`, `B` — each with the always-write float reader 0x00daccb0, divides each by the double at 0x012a2d88
(bytes `00000000 406FE000` = 255.0), and stores three consecutive floats at `+0`, `+4`, `+8` of the destination.

So each field is an RGB colour authored as `<R>`, `<G>`, `<B>` children in 0–255 and stored as three floats in
0–1. The spacing confirms it: `+0x30` + 12 = `+0x3C`, and `+0x3C` + 12 = `0x48`, the record size. The same helper
is the "RGB ÷ 255" convention `spec-tables-environment.md` §3.3 already names at 0x00DAD0A0.
**CONFIRMED — disassembly.** The §22 item 9 question (vec2 vs vec3) is closed; no data check is needed. A data
spot-check would only confirm that shipped rows author the three children (any missing child gives an
unspecified value, per the always-write reader).

### 2.2 tables-ui-controls §12 — `Min_delay`/`Max_delay` share the cooldowns' unit (milliseconds) — **EXE**

The packed voice-line record (loader 0x00469140, §12's table) is read back by 0x004694c0, which looks the line up
through 0x0046b140 (hash table at 0x01353744, armed byte 0x0135375E, record array at 0x01353758, 12-byte
records) and unpacks it into a work record: local cooldown = bits 0–5, global cooldown = bits 6–9, minimum delay
= bits 10–14 × 70, maximum delay = minimum delay + bits 15–20 × 80, play chance = (bits 21–22 + 1) × 25, priority
= bits 23–27. So the delays are expanded back to the authored unit (quantised in steps of 70 and 80), while the
cooldowns stay in the stored unit.

The play routine 0x00469a80 then: rejects the line when a random integer in [1, 100] (0x00dab660) exceeds the
play chance; when either delay is non-zero, adds a random integer in [minimum delay, maximum delay] to its
incoming delay argument (the fifth stack argument); and passes that delay to 0x00469a30. There the delay is
divided by 1000 with truncation toward zero (multiply-high by `0x10624dd3`, shift right 6, sign fix) and the
quotient is **added to both stored cooldowns** before they are registered (global through 0x00469770, local
through 0x004698b0).

Adding `delay / 1000` to the stored cooldowns means the delays are in the same unit as the authored
`Local_cooldown`/`Global_cooldown`, which the loader also divides by 1000 to get the stored value. **CONFIRMED —
disassembly** for the unpacking, the delay addition and the ÷1000 into the cooldowns; that 0x00dab660 draws a
random integer in its argument range is HIGH CONFIDENCE (inferred from the [1, 100] use; not dumped). **HIGH CONFIDENCE** that the
shared authored unit is milliseconds (it rests on §12's reading that stored cooldowns are seconds; the
dimensional argument is the same one). Consequences an implementer can use: authored `Min_delay` is effectively
quantised to multiples of 70 ms up to 2,170 ms, and the `Max_delay − Min_delay` span to multiples of 80 ms up to
5,040 ms; the chosen delay also extends both cooldowns by its whole seconds.

### 2.3 customization-data §2.2 — `Default_Wear_Option` names a wear option by its male mesh filename — **EXE**

In the outfit reader 0x008290c0, each `Content_Element`'s `Item` is resolved to an item record (`0x7C` stride,
array at 0x022db9c8), then its `Default_Wear_Option` text and that item record are passed to 0x00828390, whose
result is stored at content-element `+0x08`. 0x00828390 walks the item's wear-option array (pointer at item
`+0x24`, count at `+0x28`, `0x28`-byte records) and compares the text case-insensitively (`__stricmp`) against
the string pointer at each wear option's `+0x08`; it returns the first matching wear-option record, or 0.

In the item reader 0x00829650 (normal path, mode byte clear), wear-option `+0x08` is filled at 0x00829afa–
0x00829b21 with a fresh heap copy of `Mesh_Information` → `Male_Mesh_Filename` → `Filename` (fetched at
0x00829ab1–0x00829acd). The wear option's `Name` goes elsewhere: a localised id at `+0x00` and a hash at `+0x04`.
**CONFIRMED — disassembly.**

So the original customization-data §2.2 wording ("references the item's `Wear_Option.Mesh_Information…Filename`")
is right, more precisely *the male* filename, and the desk-review OPEN that preferred "by wear-option `Name`"
should be resolved the other way. `spec-tables-customization.md` §5.1 is right that the lookup is confined to
that item's own list, but should name the key. An unmatched value stores 0 (no wear option). A data check over
the 84 outfits would only tell how many authored values actually match.

### 2.4 customization-data §2.1 — `Default_Colors_Grid` is read at item level only — **EXE+D** (the `Shader_Type` half is STALE, §4)

In 0x00829650 the only lookup of the `Default_Colors_Grid` literal (0x0115f65c) is at 0x0082a212, after the
per-item colour/material sub-record loop has finished (loop back-edge at 0x0082a204), and it is made on the
`Customization_Item` element handle (the stack slot set from the `Customization_Item` first/next lookups at
0x008296ea, 0x0082974c and 0x0082a43e). The colour array goes to item record `+0x34`, its count to `+0x58`.
**CONFIRMED — disassembly**: the engine reads `Default_Colors_Grid` only as a direct child of
`Customization_Item`. Residual data question (low value): whether any real row also authors one inside a
colour/material sub-record, where this reader would ignore it.

### 2.5 tables-diversions §3 — the barnstorming record is a static object at 0x014BBEF0 — **EXE+D**

- **Record base.** 0x00691F10 has exactly one caller, at 0x00693b9f, inside 0x00693ae0, a routine Ghidra never
  made a function. 0x00693ae0 takes its object in ECX, re-initialises 50 sixteen-byte trigger slots from `+0x18`
  (count at `+0x338`) and a few fields at `+0x340`…`+0x380`, then calls 0x00691F10 (this table) and 0x006938e0
  (the `Barnstorming_Trigger` list reader) on the same object. A raw scan finds 0x00693ae0 only as slot 0 of a
  vtable at 0x01134b14 (the dword before it, 0x012a6de0, is the usual type-information pointer). That vtable is
  installed by the constructor at 0x006925c0, which sets `+0x0C` = −1, clears the same 50 slots and sets
  `+0x360`/`+0x364` = −1. The only caller of the constructor is a static-initialiser stub at 0x00ff5480 that loads
  ECX with **0x014BBEF0** and jumps to it; 0x00693ab0 (called from 0x006a34c0) also stores 0x014BBEF0 into a
  manager object's `+0x10` and registers a callback with id `0xE`. 0x014BBEF0 is in zero-filled `.data`.
  **CONFIRMED — disassembly**: the destination record is the static object at 0x014BBEF0, so the absolute field
  addresses are 0x014BC274 (`Max_Distance`, `+0x384`) through 0x014BC2A0 (`Allow_Replay`, `+0x3B0`). That the
  manager registration is the activity registry is HIGH CONFIDENCE only.
- **Value at 0x011348c0**: STALE, already settled at the spec's own §1.4 table row (l.85, `0x3F800000` = 1.0).
- **Field count.** The loader looks up the first `Barnstorming` child once (no row loop), then reads exactly
  **12** named fields, all through the always-write readers (float 0x00daccb0, signed int 0x00dabc70, bool
  0x00dac480 — "always" per the accessor table in `spec-vehicle-data.md` §7.2): `Max_Distance`, `Min_Speed`,
  `Max_Damage`, `Crash_Delay`, `Restart_Delay`, `Knife_Edge_Tolerance`, `Knife_Edge_Multiplier`,
  `Inverted_Tolerance`, `Inverted_Multiplier`, `Allow_Replay`, `Max_Respect`, `Max_Cash`. **CONFIRMED —
  disassembly.** So the prose's 12 is the loader's 12; none is optional, and an absent one leaves an unspecified
  value. Whether the one real row carries 11 or 12 of them is still a data question (row 66 residue).
- The two delays are clamped at 0 from below, multiplied by the double at 0x012a2d90 (1000.0) and converted
  with `FISTP` under a locally forced truncation control word (the `OR 0xC00` on the saved control word), so
  the ms conversion truncates toward zero. CONFIRMED — disassembly (adds the rounding mode §1.4 left open, for
  this table only).

### 2.6 vehicle-data §7.1 — base `_veh.xtbl` files are opened by listed name only — **EXE+D**

From 0x00ace640 → 0x00ace440 → 0x00ace380 (all read in full):

- The base-game call (at 0x00ace812) passes `vehicles.xtbl`, framework `"main"`, a path-prefix string at
  0x0129a0e3 (contents not read this pass) and **start slot 0**. 0x00ace440 keeps each `<Vehicle>` row whose `Framework` (absent → `"main"`)
  equals the framework argument case-insensitively, copying each kept `Name` into a stack array of `0x41`-byte
  slots. CONFIRMED — disassembly. The frame layout leaves exactly 132 × `0x41` bytes for that array (HIGH
  CONFIDENCE from the frame offsets); the collection loop itself has no count check.
- For each kept name it formats `"%s%s_veh.xtbl"` and tests that the file exists (0x00da90d0). **If it does not,
  it loads a fallback file instead** — 0x00acf0a0 returns `heli_standard_04` when the name contains `heli`
  (0x00ea48b0 substring test), else `car_2dr_muscle02` (pointers at 0x0130dfec / 0x0130dfe8) — and overwrites
  that file's `Vehicle`/`Name` text with the listed name. CONFIRMED — disassembly.
- 0x00ace380 then parses **every** `<Vehicle>` element of the opened file into consecutive slots `start + i`,
  stopping when the live count reaches `0x84` or when a slot's live bit is already set, and returns how many it
  added; 0x00ace440 accumulates that into the next file's start. CONFIRMED — disassembly.

Consequences: `_veh.xtbl` files that `vehicles.xtbl` does not list are never opened (no directory scan), so 106
base files against 104 base slots is not a conflict in itself. The duplicate-`Name` file
`vehicle_genericblank_veh.xtbl` is **not** the engine's fallback template (the fallback is
`car_2dr_muscle02_veh.xtbl` itself, renamed in memory); it is reachable only if listed. Base slot count = the
number of `<Vehicle>` elements across the listed base files (normally one per file). Residual data question: the
number of `vehicles.xtbl` rows with `Framework` absent or `main` (104 is implied by the DLC lists starting at
104).

### 2.7 terrain §2 and §5 item 1 — the `cc:` prefix parser — **EXE+D**

The binary holds one `"cc:"` literal, at 0x01164958, used only by 0x00855b30. That routine takes a dependency-
list object and an output integer, zeroes the integer, and for every entry index below the object's u16 count at
`+0x30` fetches the entry's name (0x00ddc6e0: 16-byte entries at the pointer in `+0x28`, name pointer in the first
dword), tests it for `cc:` with a case-insensitive substring search (0x00da7780), and when found **adds the
integer parsed from the name's fourth character onward** (CRT routine 0x00ea47e1, HIGH CONFIDENCE `atoi`) to the
output. It always returns true. CONFIRMED — disassembly.

Its only caller is 0x00858290, the zone dependency resolver: it registers a `"zone header"` container, walks
the same entry list matching extensions (seven light extensions, `.rigx`, `.effectx`, `.srt`, `.fmeshx`,
`.animx`, `.ctdgx`, `.todx`, `.xtbl`, `.cmeshx`, `.smeshx`, `.lmeshx`), walks a table of 14-byte records (u16
count at `+0x1C` of a sub-object at `+0x10`) for `.lmeshx` dependencies, and finally returns the `cc:` sum. The
zone loader 0x00859110 calls the resolver twice (0x00859638, 0x00859673) and adds the two sums.

What this settles: (a) **terrain §2's other OPEN** — "no address is given for the function that references
`.effectx` … `.fmeshx`" — is 0x00858290. (b) The `N` values are **summed**, not used as labels, so `N` is an
additive quantity (HYPOTHESIS: a per-group count or size; the downstream use of the sum in 0x00859110 was not
traced). (c) The `cc:` strings are entries of the same list the resolver indexes, so they occupy entry slots of
the runtime list (HIGH CONFIDENCE). Still OPEN: whether the runtime u16 at `+0x30` equals the file's `+0x0C`
slot count (the file→object mapping was not traced), and the census itself (data). The 14-byte records match the
`.czh_pc` `SR3Z` records of `spec-ctorless-types.md` §5, so this is `.czh_pc` work, **not** the parked `.czn_pc`
interior; the trace stopped at the resolver's return.

### 2.8 anim §2 — the acceptance function is 0x004BEA70 — **EXE+D**

Reached from constructor 0x00731B90 (type 21) and from 0x00707A00 (type 41) via 0x004beb00. 0x004bea70 takes
(handle, buffer, value, flag): it rejects a handle that is negative or not below the count at 0x030f0c64; indexes
a table of `0x24`-byte slots at 0x030f0c68; refuses a slot whose `+0x10` bit 0 is set or whose `+0x20` is
non-null; stores the third argument at `+0x1C` and the buffer at `+0x20`; sets `+0x10` bit 0 when the flag is
set; rejects the file unless the byte at `+0x04` is exactly 14; then, only when byte `+0x05` has bit `0x10`,
zeroes `+0x44` and turns `+0x40` into an absolute pointer (−1 → null, otherwise + buffer). Failure returns −1.
**CONFIRMED — disassembly.** It agrees with §2/§3's description and supplies the missing address. Bonus for the
§5 NEEDS-EXE item: slot `+0x20` is the file buffer pointer (matching §10.1's `+0x20`), `+0x10` holds flags,
`+0x1C` the third argument. The function touches neither `+0x2C` nor `+0x34`, so the data half of the marker
(their contents; the 271-clip `+0x06` residue) is unchanged.

### 2.9 geometry §5.1 — the `.cvtf_pc` header is 48 bytes; where Region 1 starts — **EXE+D**

In the parser 0x00aaf9a0 every offset is relative to file `+0x08`. After checking `ftvc` and `section_count == 2`
it uses ten header dwords at file `+0x08`…`+0x2C`:

| File offset | Use in 0x00aaf9a0 |
|---|---|
| `+0x08` | count of the Region-1 quad table (loop at 0x00aafa80) |
| `+0x0C` | **offset** of the Region-1 quad table (fixed up at 0x00aafa53; quads at value + 8) |
| `+0x10` | offset of a second 16-byte-record table; in each record `+0x04` and `+0x0C` are fixed up and `+0x0C` then goes through 0x00dcecc0 |
| `+0x14` | count of that second table |
| `+0x18` / `+0x1C` | offset / count of a third table, processed last (from 0x00ab01b2) |
| `+0x20` / `+0x24` | offset / count of a dword-offset array; each element goes through 0x00a96040 (the `Component` name resolver of `spec-tables-vehicle-world.md` §5.2) |
| `+0x28` / `+0x2C` | offset / count of a second dword-offset array; each element goes through 0x00a960e0 |

**CONFIRMED — disassembly** for the field uses. This answers OPEN (a): Region-1 entry 0 sits at file offset
`u32(+0x0C) + 8`, and the "two leading header-like quads" are header fields `+0x10`…`+0x2F`. In the worked sample
Region 1 ends at 496 with 28 quads, so it starts at `0x30`, so `u32(+0x0C)` = `0x28` = 40. **HIGH CONFIDENCE:** the
value the spec reads at `+0x0C` as "`entry_count_2 = 40`" is this offset, and its match with the 40 wheel names is
a coincidence; the wheel-name list is more likely one of the counted arrays at `+0x20`/`+0x28` (HYPOTHESIS: 40 names
in one, the seven `Family00N` strings in the other). A one-file hex check of `+0x0C`, `+0x14`, `+0x24` and `+0x2C`
would make this CONFIRMED and would also settle the Region-2 40-vs-45 composition question. Still data: OPEN (b),
classifying all 744 `vehicles.vpp_pc` `.cvtf_pc` entries by type (IDs 2 vs 8).

## 3. Desk-only (mislabelled)

- **mission-packages §3.2 (row 36).** The size half is already backed (2026-10-01 note, job `…-kbhb`). What is
  left is whether `spec-xtbl-format.md` presents `running_man_globals.xtbl` as its "first worked example". The
  desk review already read the xtbl spec and found it does not. This needs a wording fix (drop "first worked
  example" / "fully documented", or point at `spec-xtbl-format.md` §1/§3.1/§4 as "an early sample"), not data.

## 4. Already settled elsewhere — stale markers

- **ctorless-types §2 (row 11), whole.** "1 zero-byte `.gzn_pc`" is answered by two independent sources already in
  the specs: `spec-zone-data-format.md` §7.1, "1,083 `.gzn_pc` ship … 81 are zero bytes" (CONFIRMED — empirical,
  controlled), and Team B's `team-b/HANDOFF.md` §9.55.1, "exactly 1,002 non-empty pairs" = 1,083 − 81, both quoted
  in `spec-terrain-format.md` §1. The ctorless sentence "one shipped `.gzn_pc` is 0 bytes" should be struck and
  pointed at those.
- **tables-progression §14 (row 77), the §14.6 part.** The "80 vs 91 `level_info`" conflict is closed by the
  VALIDATED-BY-DATA note in §14.6 itself (Team B job `20261001T003803-team-b-upsq`, "80 / 254 / 263 … exactly the
  spec's §14.6 text"). The §14.1 (43 named types / 308 vs 312 rows) and §14.12 (5 + 26 + 1 = 32 vs 31) parts are
  still data.
- **customization-data §2.1 (row 12), the `Shader_Type` half.** Settled by `spec-tables-customization.md` §4.3
  (2026-10-02 re-derivation of 0x00829650: read inside the per-`Material_Element` loop on that loop's handle;
  CONFIRMED — disassembly).
- **tables-diversions §3 (row 66), the `0x011348c0` value.** Settled by the spec's own §1.4 row (l.85): `f32`
  `0x3F800000` = 1.0 (2026-10-02). Re-read this pass: the static dword is `0x3F800000`.

## 5. Genuinely needs Team B data (one line each: what settles it)

ai-behavior
- §2: a per-archive listing of every `.xtbl` that carries the `Group_AI_Override` shape, read with the §7 reader.
- §3 intro: the `Level`/area names in `horde_mode.xtbl` (and any other table) against the four override files'
  area names, to see whether `steel_mill_area` exists anywhere.
- §3.2: a parser re-count of records per file in the 4 override files, plus a join of their 5 `Behavior`
  values to `ai_behavior.xtbl` `Behavior/Name`.

anim
- §2 (residue): per-file value histograms of header `+0x2C`, `+0x34` (and `+0x38..+0x3F` in long-header clips)
  over the 4,209 `.anim_pc`, and a listing of the 271 clips behind the `+0x06` residue.
- §6c.3: a decode replay of the 4 bit-`0x01`-clear clips with the extra rotation byte.

asm / world-streaming
- asm §9.3 and world-streaming §10.2: kind-29/30 record counts per manifest and per archive, with and without
  de-duplication by record name (does `stream_grid.asm_pc` repeat the per-tile records?).
- world-streaming §1: the mode-(b) container count over all 6,431 containers.
- world-streaming §2: the `sr3_city_0` tile-pair listing (360) against its 331 manifests, and archive-membership
  disjointness.
- world-streaming §5.1: a hex dump of `sr3_city.grid_pc`'s preamble, the bytes between sections 2→3 and 3→4, and
  the trailer. (An executable route via the reader exists, but its file-name literal is not directly
  searchable through the launcher: format strings with `%` cannot be passed, and the `awld_compact` users sit in
  the zone module next to the hold.)

audio
- §2: names and first 4 bytes of the 2 unaccounted `sounds.vpp_pc` entries.
- §9.1: a `.mbnk_pc` chain walk of `soundboot.vpp_pc` entry 1.
- §9.2: count of `sound_turbo.vpp_pc` `.lm_pc` entries whose first 4 bytes are `DMLV`.

conversation
- §2: per-voice-family file counts over the 4,984 `.ctdg_pc`.

customization-data
- §4 item 5: count of the 621 `customization_items` rows with a non-empty `Active_Flags`/`Required_Flags`/
  `Incompatible_Flags` list (and their child element names).

cutscene-camera
- §2: archive and path of each of the 192 entries, grouped by content hash (where the 96 duplicates live).
- §11 (and §10.6 item 3): a re-run of the inter-key gap histogram (minimum gap above 0.033401 s, median, p90).

effects (all over the 1,812 `.cefct_pc` / 547 distinct contents)
- §5.4: `record+0x10` tag histogram per array.
- §5.5: filter array tiled at stride `0x70` (count × `0x70` lands on the next region).
- §5.6: `root+0x90` tiled at stride `0x228` over the 478 occurrences.
- §6.7: P-block field census incl. type 5 ×712.
- §6.10: filter-record census plus blob dumps.
- §6.11: the region-tiling gate with the corrected end rule and the semantic checks.
- §7.4: `+0x80 == −1` and `+0x08` values over the 346 stride-`0x88` occurrences.
- §7.5: type/cookie census and `0xC8` tiling over 595 occurrences.
- §7.6: marker, `+0x20 == −1` and `0x228` tiling over 478 occurrences / 119 files.

fxo
- §7.1: flag/index census over the 844 `.fxo_pc`.
- §7.3: T0/T4 entry layouts over the population, and a listing of the 8 register-label mismatches.
- §8.2: shader-stage type of the middle blobs (DXBC program type) in the D3D11 build's `.fxo` files.

geometry
- §3.1.1: array-4 identity and size over every `.ccmesh_pc`, and `subheader mod 16` over the vehicle meshes.
- §4.1.5: array-3 record gap, array-1 sample coverage and pointer-slot second dwords over all `.ccmesh_pc`.
- §5.1 (residue): type classification of all 744 `.cvtf_pc` entries in `vehicles.vpp_pc`; one-file hex of
  `+0x0C`/`+0x14`/`+0x24`/`+0x2C` (§2.9).
- §5.2: one real `Character cvtf` file, from the nested `.str2_pc` bundles of `customize_player`,
  `customize_item`, `characters` or `player_morph`.

low-mips
- §1 bullet 2: a header re-dump of `high_mips.vpp_pc`.
- §5: histogram of container header `0x164` over all containers (2,048-alignment, relation to sizes).

mission-packages
- §1: header `0x14C` of `sr3_city_missions.vpp_pc` and of its 15 nested containers, plus their entry counts.
- §5: top-level name lists of `sr3_city_0/1` (the 1,033 / 1,516 figures and the odd count), plus the DLC
  top-level listings.

morph
- §3.3: per-entry `n` histogram and Σ`n` over the 1,541 files against 45,243, and the `id` field tested against
  the engine name hash.
- §12.1: the populations behind 56,822 / 170,466, and the per-triple `max|q|` distribution.
- §15.3: Team B's bounds and compactness tests over the 1,606 bundles, non-compact fraction by `N`.
- §15.5: target count of `cm_pc_fac.cmorph_pc` and the head-region vertex range re-measured.

physics
- §4.4.2(f): the single-file lead checked over all 100,384 `.clmesh_pc`.
- §4.4.6(i): Team B's post-fix per-archive MeshBlock resolution counts.

render-pipeline
- §20.12.2: CTAB reader over all 10 blobs of `ir_sr3carpaint_gr_v.fxo_pc` (entry 495).
- §20.12.3: per-blob check that no DEF/DEFI/DEFB register collides with a CTAB constant.
- §20.12.5: `projTM` semantics from the shipped VS bytecode (the register census is already validated).
- §22.11: per-stem shader census over all 393 vehicle files (37 stems).

resource-dispatch
- §7 item 5a: over the 390,134 manifest entries, per type, the count of paired entries with secondary size 0
  and of unpaired entries with non-zero secondary size.

rig
- §11.5.1: a harness re-run confirming 66,822 live lanes = 3 × 22,274.
- §11.18.1: array-4 count and contents over every `.ccmesh_pc`/`.csmesh_pc`.

save-format (all over the 16 snapshots)
- §9.3: which snapshots carry flag words with bits 3/4 set.
- §9.6: the `07`-target listing (19 vs the per-group list's 22).
- §10.5, §12.4.3, §12.10.4: the missing 16th sample in each tally.
- §12.5: the set-2 tally re-counted (17–18 of 16).
- §12.9: the four slot pairs checked against §11.5's substitution rule.
- §12.10.1, §12.10.3: the 2 missing samples in each tally.

tables-diversions (each table's real row(s), presence census of the named fields)
- §2.2 `stunt_drifting` (9 vs 11), §2.4 `stunt_near_miss` (12 vs 15), §2.5 `stunt_back_seat_driver` (8 vs 10),
  §2.7 `stunt_peel_out` (9 vs 10), §2.10 `stunt_low_flying` (12 vs 14), §3 `barnstorming` (11 vs the
  loader's 12, §2.5).

tables-environment
- §1.5: whether base rows carry `Info_Slot_Index`.
- §3.3: every nested element path in the 4 real lighting segments, and the authored spelling of `Fog_Density`.
- §8: `Spasm` child census in `refraction_situations.xtbl`.
- §10.1: child names of the 4 `Damage_Region` rows; duplicate `Name`s in `effects.xtbl`.
- §12.1: the largest `Strength_Element` count over the 70 `camera_shake` rows (the loader does not check 6).
- §12.2: the real `panning_group` element names.
- §17.9: content hashes of both base `effects.xtbl` copies.

tables-progression
- §2.1/§2.2: order and case of the files feeding the completion-percent denominator in the real table.
- §3.2/§3.3: the largest requirement count and back-pointer count on real achievement rows against the caps.
- §4.6: whether unlockable types 14 and 23 occur in real rows.
- §14 (residue): recount of `unlockables.xtbl` types/rows (43 named vs 44; 308 vs 312) and of
  `spawn_info_categories.xtbl` rows (32 vs 31).

tables-ui-controls
- §2.3: Table B key pairing checked against real saves.
- §3/§3.2: `Scheme_Type` split of the 13 `control_schemes` rows; count with X360 = false.
- §5: Team B reproduction of the 154/165 CBA and 34/34 CAA unions.
- §8.1: whether `vehicle_cameras.xtbl` authors its values as elements or attributes.
- §11: every shipped `control_scheme_text` row has `Controls` (a row without it hits the latent loop).

tables-vehicle-world
- §3.3: the 52-row patch copy compared with its base.
- §5.2: every one of the 51 `Front_Rim`/`Rear_Rim` values found as a `Component` `Name`.

tables-weapons-combat
- §10.2: the reject rules applied to the 3 `continuous_explosions` rows; the parent element of each of the 7
  lower-case elements in the `Daedalus` row.

terrain
- §2 / §5 item 1 (residue): per `.czh_pc` over the 2,971 files, every `cc:N` (value, position, group sizes) and
  the file's `+0x0C` slot count against its string count.
- §5.3: material-id range on the 535-channel population, and the stem-group test.

vehicle-data
- §7.1 (residue): number of `vehicles.xtbl` rows with `Framework` absent or `main`.
- §7.2: every real vehicle's `Group` text against the 19 `vehicle_groups.xtbl` rows.

vertex-format
- §12.1, §12.2: re-run on the corrected population.
- §12.3, §12.13 item 3: re-run code 24 on the 535 channels.
- §12.5: both denominators (832,194 vs 3,405,780) and the full code-101 tangent 4th-byte histogram.
- §12.9: tree position at `+0` versus `+16` over the population.
- §12.12: shuffled-bbox control, per-vertex face-normal test, `+6`/`+22` histograms.

vpp-container
- §2: physical location of the raw entries in the 68 mixed mode-(b) containers; `+0x04`/`+0x14` zero on all
  405,694 entries.
- §2.1: header `0x160` against Σ(name length + 1) over every container.
- §6: census of header `0x150` and `0x164` over every container.

xtbl
- §3.2: counts over the 2,222 entries of `TableDescription`, `EntryCategories`, `Reference`, `Single_Line_XML`.
- §3.3: per-file share of rows carrying `_Editor`.
- §6.4: per-parent element counts in `01_in.cte_xtbl`; element census over the 334 `.cte_xtbl`.
- §8: shape counts (1,817 / 1,959 / 1,962 / 1,595) broken down by archive.

zone-data (hold-adjacent, §6)
- §2: the alignment-variant chain-walk table re-labelled, with "EOF" and "error" defined and the 296
  unaccounted files listed (D4).
- §10.1: the `p ≡ 2 / 6` parity census (D2).
- §10.8: why this walker fails where Team B tiles 1,002/1,002 (D10); the 921 vs ~915 set comparison (D13).

## 6. Hold check

- Nothing in this pass read `.czn_pc` interior data or the named-object resolution path. The `cc:` trace (§2.7)
  went through the zone module's dependency resolver but stayed on the `.czh_pc` header list and stopped at the
  resolver's return value.
- **Hold-adjacent, flagged rather than worked:** zone-data §2 (a top-level chain-walk census over the 2,971
  `.czn_pc`), §10.1 and §10.8 (the `.gzn_pc` / pair tiling residuals). They concern file-level structure that the
  zone-data spec already documents openly, and Team B already reads that population (1,002/1,002), so they are
  listed as Team B data work. They are not part of the parked object-placement interior, but they are close to
  it, so the owner should confirm scope before Team A does any data-side work on them.
- world-streaming §5.1: the executable route to the `grid_pc` reader was not pursued past a string search,
  because the `awld_compact` users (0x00857e70, 0x00859cd0, 0x00860900, 0x00862c00) are zone-module functions next
  to the hold.

## 7. Suggested marker changes (for the spec owner; not applied)

| Spec § | Suggested status |
|---|---|
| tables-customization §15.2 | CONFIRMED — disassembly: vec3 RGB, children `R`/`G`/`B` ÷ 255 (0x00dad0a0); clears §22 item 9 |
| tables-ui-controls §12 | delay unit = cooldown unit (CONFIRMED — disassembly, 0x004694c0/0x00469a80/0x00469a30); ms HIGH CONFIDENCE |
| customization-data §2.2 | CONFIRMED — disassembly: key = male mesh filename (0x00828390 vs wear option `+0x08`); also annotate tables-customization §5.1 |
| customization-data §2.1 | `Shader_Type` → see tables-customization §4.3; `Default_Colors_Grid` item level only (0x0082a212) |
| tables-diversions §3 | base = static object 0x014BBEF0 (CONFIRMED); value already at §1.4; 12 always-read fields; residue = real-row census |
| vehicle-data §7.1 | listed-name loading, fallback template `car_2dr_muscle02`/`heli_standard_04`, start 0; residue = base row count |
| terrain §2 / §5 item 1 | parser 0x00855b30, resolver 0x00858290 (also closes §2's missing address); `N` summed; residue = census |
| anim §2 | acceptance function 0x004bea70 (CONFIRMED); residue = `+0x2C`/`+0x34`, `+0x06` residue |
| geometry §5.1 | 48-byte header; Region 1 at `u32(+0x0C)+8`; `entry_count_2 = 40` reading probably wrong (HIGH CONFIDENCE) |
| mission-packages §3.2 | DESK: wording fix only |
| ctorless-types §2 | STALE: 81 per zone-data §7.1 and Team B §9.55.1 |
| tables-progression §14 | §14.6 conflict closed by the §14.6 VALIDATED note; §14.1/§14.12 remain |

## 8. Clean-room check

This note uses no decompiler auto-names and no pseudocode; functions and data are cited by plain address.
Element and literal names are the engine's own XML/string literals, used as functional identifiers as elsewhere
in the specs. No RTTI class name is given (the type-information pointer in §2.5 is cited by address only).

Self-check, run as the last step on this file (result recorded below by the check script, not by hand):

- Regex controls (must match / must not match): see the result block.
- Regex hits outside this section: see the result block.
- Plain-substring backstop (`unaff_`, `extraout_`, `Stack_`, `Var`, `param_`, `local_`, `FUN_`, `DAT_`, `LAB_`,
  `undefined`), case-sensitive: see the result block.

**Result of the self-check (script output, 2026-10-03):**

- Controls: all behaved as expected.
  - positive controls (one per auto-name family, incl. the three address-suffixed families): 14/14 matched
  - negative controls (plain addresses, engine element names, ordinary words): 0/11 matched
  - known-gap control (a register-alias name whose register part itself contains an underscore): not matched, as documented; the substring backstop covers it
- Regex hits in the whole file: 0; outside this section: 0.
- Backstop substring hits (case-sensitive) in the whole file: 10; outside this section: 0. All are in this section, where the patterns are quoted as the list to check.
