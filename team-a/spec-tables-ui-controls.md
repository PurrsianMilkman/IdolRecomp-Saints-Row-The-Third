# Saints Row: The Third — Control Bindings, QTE, and Camera-Preset Table Specification

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1, second `spec-tables-*.md` batch (dispatched 2026-09-23, agent AP)
**Scope:** Loader-recovered schemas for the control-binding / QTE / camera-preset `.xtbl` group: `control_binding_sets.xtbl`, `control_filters.xtbl`, `control_parameters.xtbl`, `control_scheme_text.xtbl`, `control_schemes.xtbl`, `hud_qte_interface.xtbl`, `hud_qte_interface_presets.xtbl`, `qte.xtbl`, `qte_sequences.xtbl`, `user_interface.xtbl`, `voice_control.xtbl`, `credits_pc.xtbl`, `item_cust_cameras.xtbl`, `player_cust_cameras_new.xtbl`, `vehicle_cust_cameras_new.xtbl`, `vehicle_group_cameras.xtbl`, `vehicle_cameras.xtbl` — all 17 assigned tables reached, none skipped.
**Method:** Same pattern as `spec-vehicle-data.md` §7 (the worked example) and the schema-from-loader campaign in `spec-tables-weapons-combat.md`/`-progression.md`/`-traffic-ai.md`/`-environment.md`: (1) confirm each table's exact filename literal in the executable (case-insensitively) with a whole-image string scan and list its code cross-references (`tools/scripts/ApStrXrefs.java`, modelled on agent AQ's `AqStrXrefs.java`); (2) follow each xref into its loader and decompile the loader plus the row-reader tree it calls (`tools/scripts/ApDecompile1.java`); (3) dump every literal `{value,name}` or plain-pointer lookup array the readers use (`tools/scripts/ApDumpEnumTable.java`, `ApDumpPtrArray.java`, `ApDumpStrings.java`, `ApDumpInts.java`); (4) cross-check every recovered field against the table's own real content, extracted with the now-standard base-table reader (`spec-vpp-container.md` §7/§8, `tools/harnesses/vpp_modea.py`, tolerant-parse rules of `spec-xtbl-format.md` §7). **All 17 tables in this group are base-game tables stored in `misc_tables.vpp_pc`** (a mode-(a) container) **and every one of them extracted and validated as well-formed, exact-length XML** — see §16. Ghidra project copy: `tools/gp_ap1`.
**Cleanroom compliance:** No decompiled code is reproduced verbatim anywhere below. XML element/attribute names, enum-string literals, and other in-game data (table filenames, action names, key names) are literal *game data*, not code, and are listed exactly as the established convention in every prior `spec-tables-*.md`/`spec-xtbl-format.md`/`spec-vehicle-data.md` document. Function addresses are cited as evidence anchors only, exactly as those documents do.

**Confidence key:** **CONFIRMED — disassembly** (traced in the decompiler) / **CONFIRMED — empirical** (observed directly in extracted game data) / **HIGH CONFIDENCE — inferred** / **HYPOTHESIS — unconfirmed** / **OPEN / UNKNOWN**.

---

## 1. Overview, method, and which mechanism applies

### 1.1 All 17 filenames are real, base-game, and now fully readable

Every filename below was found as an exact NUL-terminated literal in `SaintsRowTheThird.exe`, and every one resolves to exactly the code cross-reference count shown (§15). All 17 live in **`misc_tables.vpp_pc`** (indices given in §15); `qte.xtbl` additionally exists as a byte-identical copy at index 10 of `da_tables.vpp_pc`. `misc_tables.vpp_pc` is a mode-(a) container, so per the now-resolved offset rule (`spec-vpp-container.md` §7, `tools/harnesses/vpp_modea.py`) every one of these 17 non-first entries decodes to its exact declared uncompressed length — **17/17 exact-length extractions, 0 failures** (§16). No table in this group had to be skipped for lack of a literal, unlike some sibling groups (`spec-tables-traffic-ai.md`'s `traffic_types.xtbl`, `spec-tables-environment.md`'s `materials.xtbl`).

### 1.2 Two genuinely different C++ loading mechanisms, plus one confirmed non-Lua finding

Per this batch's standing instruction to check which mechanism applies to each table (not assume the Lua/UI-hook surface of `spec-lua-bindings.md` applies just because a table is UI-adjacent):

- **15 of the 17 tables use the ordinary synchronous C++ table reader** already documented as the shared idiom across every `spec-tables-*.md` sibling: `FUN_00DAC9A0(filename, pool, 1)` opens and parses the file (returning the `Table` element), `thunk_FUN_00DC4FF0`/`thunk_FUN_00DC5030` walk children/siblings by case-insensitive name, and a family of typed accessors (`FUN_00DABC70`/`FUN_00DACCB0`/`FUN_00DAC480`/etc.) pull scalar fields — **the exact same addresses already catalogued in `spec-tables-weapons-combat.md` §1.2–§1.3**, confirmed again here at every one of this group's 15 call sites. **[CONFIRMED — disassembly.]**
- **`user_interface.xtbl` (§10) is read by this same ordinary mechanism**, settling the one case in this group that most resembled the Vint UI-layout data `spec-lua-bindings.md` §5 flagged as "very likely" Lua/table-driven: it is **not** — it is parsed and struct-filled in C++ exactly like every other table here, with no call anywhere near `FUN_00E0CEF0`/`FUN_00E0CA80` (the confirmed Lua-hook dispatch pair). **[CONFIRMED — disassembly; directly answers the task's mechanism check for this table.]**
- **`vehicle_cust_cameras_new.xtbl` (§8) is the one exception: it is loaded through the engine's asynchronous resource-dispatch/streaming system, not the synchronous reader above** — `FUN_00A9ADE0`/`FUN_00A9AEF0` request and poll the resource by name through `FUN_00DB3530`/`FUN_00DAFB60`/`FUN_00DAFAD0`, a state machine (states 0–3) shared with ordinary streamed asset loading, not the `FUN_00DAC9A0` XML-table path. This is a genuinely different mechanism from its two closest siblings in the same subgroup (`item_cust_cameras.xtbl`, `player_cust_cameras_new.xtbl` — §8), whose own consumer could not be located this pass (open, not forced — see §8.3 and §17). **[CONFIRMED — disassembly for `vehicle_cust_cameras_new.xtbl`'s mechanism; the sibling two are an honest gap.]**
- **None of the 17 tables in this group show any call into the Lua/Vint hook-dispatch mechanism** (`spec-lua-bindings.md` §4/§8's `FUN_00E0CEF0` family) — every reader traced is a plain C++ struct-fill parser (or, for one file, the resource-streaming system above). `spec-lua-bindings.md` §4's own hit list does include two hook names adjacent to this group's domain (`vint_remap_get_action_binding`, `hud_qte`) that were **not** traced further this pass — flagged as a cross-reference, not re-derived (§15).

### 1.3 Shared reader grammar — reuse `spec-tables-weapons-combat.md` §1, not re-derived

This group's readers use the **identical** node model, accessor-function addresses, and float/integer parsing grammar already fully documented in `spec-tables-weapons-combat.md` §1.2/§1.3/§1.5 — re-confirmed here at every call site (same addresses: `FUN_00DABC70`/`FUN_00DABD20` s32, `FUN_00DABDF0`/`FUN_00DABE80` u32, `FUN_00DAC480`/`FUN_00DAC510` bool, `FUN_00DACCB0`/`FUN_00DACD40` f32, `FUN_00DAC830` the generic index-of-match enum reader, `FUN_00D9E8B0`/`FUN_00D9E740` the shared CRC-32 name hash). **Do not re-read that section's derivation — cite it.** This document only tabulates what is new: the element trees, destination offsets, and literal lookup tables specific to this group's 17 files.

### 1.4 Name hash confirmation, with one real seed exception

Every table in this group hashes its row-key name with the same engine routine (`FUN_00D9E8B0`, table `0x01320DA0`, reflected CRC-32, input lower-cased, no final XOR) already fully specified in `spec-tables-weapons-combat.md` §1.4 and `spec-vehicle-data.md` §7.1. **Seed varies by caller, exactly as those two documents already note**: `control_binding_sets.xtbl`/`control_schemes.xtbl` (§2–§3) and the vehicle-camera tables (§7) hash with seed `0`; **`qte_sequences.xtbl`'s `Node_Name`/`HUD_Interface` and `hud_qte_interface.xtbl`'s row `Name` both hash with seed `0xFFFFFFFF`** (§6.2, §9) — the same seed value `spec-conversation-format.md` §6 already reported for a different, unrelated caller, so this is a second confirmed sighting of that seed value rather than a new one. **[CONFIRMED — disassembly for every call site.]**

---

## 2. `control_binding_sets.xtbl` — PC keyboard/mouse binding sets **[CONFIRMED — disassembly + empirical]**

**Loader `FUN_005BF2F0(char refresh)`**, one string literal, one xref (`005bf307` inside `FUN_005bf2f0` itself). Opens the file, walks `<root><Table><Binding_Set>` rows. Real file: **7 `<Binding_Set>` rows** (`General_Bindings`, `On_Foot_Bindings`, `Camera_Bindings`, `Car_Bindings`, `Helicopter_Bindings`, `Airplane_Bindings`, `Tank_Bindings`), **96 `<Control>` (button) rows + 18 `<Axis>` (axis) rows = 114 total bindings**, 91 distinct button action names and 18 distinct axis action names (§16).

### 2.1 `Binding_Set` row

`Name` (bounded copy, `<= 0x18` chars, via the shared name-cache global `0x0129EE6C` — the same address `spec-tables-weapons-combat.md` §1.5 item 4 documents as the `Name` row key), `Common_Bindings` (bool, stored once per set; consumer not traced — OPEN, §17), then two child lists: `Button_Controls > Control` and `Axis_Controls > Axis`. On `refresh != 0` the loader first zeroes two existing runtime arrays (stride `0xCE8` and `0x568`) before reparsing — a reload/reset idiom, not necessarily the identical "first-load vs refresh" polarity documented for the weapons group (not asserted either way here).

### 2.2 `Control` (button binding) record — **20 bytes, exactly `spec-save-format.md` §7.6 Table A's shape**

| Field | XML element | Type / lookup | Dest. offset |
|---|---|---|---|
| Action | `Action` | matched against the 166-entry CBA table (§4.1) | `+0x00` |
| Key 1 | `Key` | matched against the 80-entry key-name table (§4.3) | `+0x04` |
| Key 2 | `Alt_Key` | same key-name table | `+0x08` |
| Mouse 1 | `Mouse_Button` | matched against the 8-entry mouse-button table (§4.4) | `+0x0C` |
| Mouse 2 | `Alt_Mouse_Button` | same mouse-button table | `+0x10` |

Unmatched/absent text yields `0xFFFFFFFF` for every field (the shared enum-match idiom's uniform "no match" sentinel). `Debug_Only`/`Non_Release_Final` (bools) gate the row: **either one true skips the record entirely** — it is parsed but never stored, so it does not occupy a runtime slot. Runtime array: base `DAT_0142A010`, running count `DAT_0142A018`, **capacity `DAT_0142A014`** — never read as a static constant this pass (it is `.bss`, zero in the image), but the compile-time CBA table's own value range independently pins its value to **exactly 164 (`0xA4`)** — see §4.1 and §5's cross-reference. Record stride confirmed **`0x14` = 20 bytes**.

### 2.3 `Axis` (axis binding) record — **40 bytes, exactly `spec-save-format.md` §7.6 Table B's shape, and closes its field-order OPEN item**

| Field | XML element | Type / lookup | Dest. offset |
|---|---|---|---|
| Action | `Action` | matched against the 34-entry CAA table (§4.2) | `+0x00` |
| Key 1 | `Key_Pos` | 80-entry key-name table | `+0x04` |
| Key 2 | `Key_Neg` | 80-entry key-name table | `+0x08` |
| Key 3 | `Alt_Key_Pos` | 80-entry key-name table | `+0x0C` |
| Key 4 | `Alt_Key_Neg` | 80-entry key-name table | `+0x10` |
| Mouse 1 | `Mouse_Button_Pos` | 8-entry mouse-button table | `+0x14` |
| Mouse 2 | `Mouse_Button_Neg` | 8-entry mouse-button table | `+0x18` |
| Mouse 3 | `Alt_Mouse_Button_Pos` | 8-entry mouse-button table | `+0x1C` |
| Mouse 4 | `Alt_Mouse_Button_Neg` | 8-entry mouse-button table | `+0x20` |
| Direction selector | `Mouse_Axis` | 3-entry table: `MOUSE X`=0, `MOUSE Y`=1, `UNBOUND`=−1 (§4.5) | `+0x24` |

**This directly resolves `spec-save-format.md` §7.6's own flagged OPEN item** ("the *pairing* of which key/button sits in which runtime slot is only partly resolved"): the file-order-to-slot mapping is exactly the field order above, four keys then four mouse buttons then one direction/axis selector — matching that document's own observed shape `{i32 index, four i32 key codes, four i32 mouse-button selectors, i32 direction selector}` field-for-field. Runtime array: base `DAT_01436EA0`, count `DAT_01436EA8`, capacity `DAT_01436EA4` (also zero in the static image; pinned to **exactly 34 (`0x22`)** by the CAA table, §4.2). Record stride confirmed **`0x28` = 40 bytes**.

---

## 3. `control_schemes.xtbl` — console gamepad button-layout presets **[CONFIRMED — disassembly + empirical]**

**Loader `FUN_005C1860(char refresh)`**, one literal, one xref. Real file: **13 `<Control_Scheme>` rows** — lettered layout presets `A`/`B`/`C`/`N`/`T`/`X` (each appearing once per applicable `Scheme_Type` context) plus three unlettered named schemes `General Controls`, `GCN`, `Satellite Controls`. Four separate runtime arrays by `Scheme_Type` (stride `0xFC0` = 4,032 bytes each, capacity/count pair at `+0x00`/`+0x04` — arrays not individually bounded-read this pass): **Driving** (`DAT_014240C0`, matched via a "does the text contain `Driving`" test — `FUN_00DA7780`, not a plain `stricmp`), **On_Foot** (`DAT_014183A0`, tested against a stored fragment `"Foot"` — the real XML tag is `<On_Foot>`, so this reads as a substring/contains test rather than an exact match; not fully characterized), **Satellite** (`DAT_0141E230`), and the **default/General** fallback (`DAT_01412510`, used when none of the other three match). `Weapon_Selection_Stick` (`L_JOY`/`R_JOY`/`STICK_UNBOUND`, §4.6) is read once per scheme.

### 3.1 `Control` (button) record — **20 bytes**

`Action` (166-entry CBA table, same table as §2.2) → `+0x00`; `Button` (19-entry gamepad-button table, §4.6) → `+0x04`; `Alternate_Button` (same table) → `+0x08`; `Stick_Direction` (9-entry table, §4.7) → `+0x0C`; `Action_Name` (a free-text display label, e.g. `"weapon menu"`, `"handbrake"` — heap-duplicated string handle, not looked up) → `+0x10`. **Gated by an `X360` bool read before `Debug_Only`/`Non_Release_Final`: a row whose `<X360>` is false (default true when absent) is skipped outright** — this PC build's `control_schemes.xtbl` reader canonicalises on the X360-flagged variant of each duplicate `<Control>` row and never consults `<PS3>` or `<PC>` at all (both exist as data but are not read here). Real data always ships `<X360>`/`<PS3>`/`<PC>` together (509 occurrences each) — the loader simply ignores two of the three. **[CONFIRMED — disassembly for the gate; CONFIRMED — empirical for the 509/509/509 tag-presence count.]**

### 3.2 `Axes > Axis` (axis) record — **20 bytes, a sibling block of each `<Control_Scheme>` row, separate from `<Controls>`**

`Action` (34-entry CAA table) → `+0x00`; `Axis` (5-entry stick-axis table, §4.8) → `+0x04`; `Inverted` (bool) → `+0x08`; `Alternate_Axis` (same 5-entry table) → `+0x0C`; `Alternate_Inverted` (bool) → `+0x10`. **[CONFIRMED — empirical: a real `<Axes><Axis>` block sampled directly from the file, matching this 5-field shape exactly.]**

---

## 4. The shared action-name and input-vocabulary tables

These compile-time literal `{value, name}` (or plain-pointer) arrays are the engine's canonical vocabulary for every reader in §2–§3 and §9. **Recovering them is this document's highest-value single result**, because `spec-save-format.md` §7.6/§7.7 already documented the *runtime binary shape* of the saved binding tables (164×20-byte Table A, 34×40-byte Table B) purely from the save file's own bytes, without the action names those tables index by number — these arrays supply exactly that missing name↔index map, from the opposite (XML-source) side. Full dumps: `tools/scripts/ApDumpEnumTable.java`, `ApDumpPtrArray.java`.

### 4.1 CBA (button-action) table — 166 entries at `0x01122160`/`0x01122164` **[CONFIRMED — disassembly]**

Index 0 is the sentinel `BUTTON_UNBOUND` (value `−1`); the remaining 165 names carry values that span **exactly `0`–`163` with one gap (value `29`, orphaned — no name currently maps to it) and two duplicate pairs** (`CBA_OFC_TAUNT_TWO`/`CBA_OFC_AUDIO_PLAYER_PREV_TRACK` both `= 23`; `CBA_OFC_TAUNT_THREE`/`CBA_OFC_AUDIO_PLAYER_NEXT_TRACK` both `= 24`) — netting **163 distinct non-negative values across a 0–163 range, i.e. exactly 164 possible slots**. This is an exact, independent match to `spec-save-format.md` §7.6/§7.7's Table A capacity, cited there as the constant `0xA4` (164) — **CONFIRMED by two different sides of the pipeline (the save file's own runtime-table constant, and this table's own value range), not merely consistent.** A byte-identical second copy of this same 166-entry table exists at `0x0115C040`/`0x0115C044`, compiled into `hud_qte_interface.xtbl`'s own reader (§9) — verified entry-for-entry identical (166/166 rows, 0 differences). Representative entries: `CBA_OFC_ATTACK_PRIMARY`=7, `CBA_VDC_ACCELERATE`=40, `CBA_GAC_ACTION`=82, `CBA_MENU_A`=130, `CBA_MENU_PC_TEXT_CHAT`=163 (the highest value in use). Full 166-row dump: `tools/scratchpad/ap_enumtables1_clean.txt` lines 1–167.

### 4.2 CAA (axis-action) table — 34 entries at `0x01122690`/`0x01122694` **[CONFIRMED — disassembly]**

No sentinel row — **all 34 entries carry distinct values spanning exactly `0`–`33`, no gaps, no duplicates** — an exact match to `spec-save-format.md` §7.6/§7.7's Table B capacity (`0x22` = 34). A byte-identical second copy exists at `0x0115C570`/`0x0115C574` (`hud_qte_interface.xtbl`, §9; 34/34 identical). First/representative entries: `CAA_CAMERA_ROTATE`=0, `CAA_CAMERA_ELEVATE`=1, `CAA_DRIVE_STEER`=9, `CAA_TANK_DRIVE_FORWARD_BACKWARD`=16, `CAA_VPC_TURRET_RIGHT`=29, `CAA_TURRET_CAMERA_ELEVATE`=31, `CAA_PLANE_ROLL_LEFT_RIGHT`=33 (the highest value). Full dump: same file, lines 168–202.

### 4.3 Key-name table — 80 entries at `0x01121D20`/`0x01121D24` — **confirms `spec-save-format.md` §7.6/§7.9's "looks like DirectInput scan codes" as CONFIRMED, not just HIGH CONFIDENCE**

`UNBOUND`=0, then the alphabet `A`=`0x1E`…`Z`=`0x2C`, digits `1`–`0`=`0x02`–`0x0B`, and named keys including `LEFT CTRL`=`0x1D`, `RIGHT CTRL`=`0x9D`, `TAB`=`0x0F`, `ESC`=`0x01`, `SPACEBAR`=`0x39`, `ENTER`=`0x1C`, `BACKSPACE`=`0x0E`, arrow keys `0xC8`/`0xD0`/`0xCB`/`0xCD` (up/down/left/right), `F1`–`F12` (`0x3B`–`0x44`, `0x57`, `0x58`). **These values are exactly the standard PC AT keyboard Set-1 scan codes** (including the `0x80`-extended-range codes for the non-numpad arrow/navigation cluster and right-side modifier keys) — this resolves `spec-save-format.md` §7.9's remaining open scan-code question for the field names it could only infer from raw numbers. **[CONFIRMED — disassembly for the table; the scan-code identification itself is HIGH CONFIDENCE by exact numeric match to the well-known PC scan-code standard, not independently verified against a hardware trace.]** Full 80-row dump: `tools/scratchpad/ap_enumtables1_clean.txt` lines 203–283.

### 4.4 Mouse-button table — 8 entries at `0x01121FA0`/`0x01121FA4`

`MOUSE LEFT`=0, `MOUSE MIDDLE`=1, `MOUSE RIGHT`=2, `MOUSE 4`=3, `MOUSE 5`=4, `WHEEL UP`=5, `WHEEL DOWN`=6, `UNBOUND`=−1 — **exactly 7 real values (0–6), confirming `spec-save-format.md` §7.6's own "`< 7`" acceptance range field-for-field, by name.**

### 4.5 Mouse-axis table — 3 entries at `0x01121FE0`/`0x01121FE4`

`MOUSE X`=0, `MOUSE Y`=1, `UNBOUND`=−1 — the source of §2.3's `Mouse_Axis` field, which is `spec-save-format.md` §7.6 Table B's "direction selector (−1, 0, or 1)" now named exactly.

### 4.6 Gamepad-button table — 19 entries at `0x011220C8`/`0x011220CC`

`A`=0, `B`=1, `X`=2, `Y`=3, `LB`=4, `RB`=5, `LT`=6, `RT`=7, `D_RIGHT`=8, `D_UP`=9, `D_LEFT`=10, `D_DOWN`=11, `START`=12, `BACK`=13, `LS`=14, `RS`=15, `L_JOY`=0, `R_JOY`=1, `STICK_UNBOUND`=0 (the last three double-map onto the same numeric range as `A`/`B`/`STICK_UNBOUND`/`A` respectively — a real, confirmed value collision; consumers must disambiguate by which of the two lookup tables/fields they used, not by the returned value alone). Used for §3.1's `Button`/`Alternate_Button` and §3's `Weapon_Selection_Stick`.

### 4.7 Stick-direction table — 9 entries at `0x011227A0`/`0x011227A4`

`STICK_DIR_UNBOUND`=−1, `LEFT_STICK_DIR_UP`=0, `_DOWN`=1, `_RIGHT`=2, `_LEFT`=3, `RIGHT_STICK_DIR_UP`=4, `_DOWN`=5, `_RIGHT`=6, `_LEFT`=7.

### 4.8 Stick-axis table — 5 entries at `0x011227E8`/`0x011227EC`

`STICK_AXIS_UNBOUND`=−1, `RIGHT_STICK_AXIS_X`=0, `_Y`=1, `LEFT_STICK_AXIS_X`=2, `_Y`=3.

---

## 5. Cross-reference: how this closes out `spec-save-format.md` §7.6/§7.7's open items

`spec-save-format.md` §7.6/§7.7 (`sr3def_profile`'s Tables A/B) is a **closed spec section and is not re-derived here** — per this batch's brief, only cross-referenced. Concretely, what §2–§4 above adds to that already-published binary layout:

1. **Action-name↔index map, both tables, both directions.** §4.1/§4.2 give the exact string for every numeric action index `spec-save-format.md` §7.6 Table A (0–163) and Table B (0–33) can hold. **[CONFIRMED — disassembly, exact range match both ways.]**
2. **Table B's field-order OPEN item is now resolved** (§2.3): file order `Key_Pos, Key_Neg, Alt_Key_Pos, Alt_Key_Neg, Mouse_Button_Pos, Mouse_Button_Neg, Alt_Mouse_Button_Pos, Alt_Mouse_Button_Neg, Mouse_Axis` maps directly, in that order, onto the runtime record's 9 post-index dwords.
3. **The scan-code hypothesis in `spec-save-format.md` §7.6/§7.9 is now CONFIRMED, not just HIGH CONFIDENCE** (§4.3): every numeric key code is one of the 80 named entries in the key-name table, which are the standard PC scan codes.
4. **`spec-save-format.md` §7.6's "gamepad bindings come from the control-scheme presets" HIGH-CONFIDENCE claim is directly confirmed**: `control_schemes.xtbl` (§3) is exactly that preset source, keyed by the same lettered/named scheme identifiers (`A`/`B`/`C`/`N`/`T`/`X`/`General Controls`/`GCN`/`Satellite Controls`) a settings UI would present as the "preset index."
5. **A residual, honestly-reported gap**: of the 165 non-sentinel CBA names, only **154 distinct names are ever assigned in the two source files** (`control_binding_sets.xtbl` ∪ `control_schemes.xtbl` = 154 of 165, computed empirically, §16) — the remaining ~11 button-action slots that the compile-time table defines are not bound by name in *either* file sampled this pass (they may be bound only via a code path not read this session, e.g. a hardcoded default, or genuinely unused on PC). **Not forced further — flagged as OPEN (§17).** By contrast, **the CAA axis-action union is a clean 34/34** — every one of the 34 compile-time axis actions is bound by name in the union of the two files.

---

## 6. `qte.xtbl` — global QTE reward parameters **[CONFIRMED — disassembly + empirical]**

**Loader `FUN_006B8620()`**, one literal, one xref. Trivial single-row table: `<root><Table><QTE><Name>QTE</Name>...`. Five fields read into fixed globals (no array, no capacity):

| XML element | Type | Destination | Note |
|---|---|---|---|
| `Hud_Time` | f32, always (`FUN_00DACCB0`) | `DAT_014C7390` (i32) | seconds, `× DAT_012A2D90` and rounded — a global scale constant, plausibly a tick/frame rate; not identified further |
| `Cooldown_Time` | f32, always | `DAT_014C7394` (i32) | stored as **`DAT_014C7390 − round(Cooldown_Time × scale)`** — an offset relative to the converted `Hud_Time`, not an independent value |
| `Respect` | i32, always (`FUN_00DABC70`) | `DAT_014C7398` | clamped `≥ 0` |
| `Max_Lifetime_Respect` | i32, always | `DAT_014C739C` | clamped `≥ 0` |
| `Cash` | f32, always | `DAT_014C73A0` | clamped `≥ 0.0` |

Real file (both copies, `misc_tables.vpp_pc`/`da_tables.vpp_pc`, byte-identical): `Cooldown_Time=6`, `Respect=50`, `Max_Lifetime_Respect=10000`, `Cash=100`, `Hud_Time=4`.

---

## 7. `qte_sequences.xtbl` — QTE state-machine sequences **[CONFIRMED — disassembly + empirical]**

**Loader `FUN_0060C0A0()`** → per-row reader **`FUN_0060B820()`**, one literal, one xref. **Corrected 2026-09-23 (orchestrator spot-check):** the file's root has the by-now-familiar `['Table', 'TableTemplates', 'TableDescription', 'EntryCategories']` shape (same pattern as `unlockables.xtbl`/`cheats.xtbl`/`vi_enter.xtbl` — see `spec-tables-progression.md` §14, `spec-tables-vehicle-world.md`). Real, loader-fed `<Table>` data is **26 `<QTE>` rows, 90 total `<QTE_Node>` elements** (max per row: 12, in `Mission16_FinalQTE`) — well under the 160-slot cap below. The originally-reported "33 rows, 167 nodes" is a whole-document count that also swept in 7 example rows sitting under `<TableTemplates>` (editor scaffolding, including `Mission16_FinisherQTE` — see the retraction in §7.3).

### 7.1 `QTE` row — **128 (`0x80`) bytes**, array base `DAT_014AEF50`, no explicit outer capacity found this pass (allocation is via a hash-bucket allocator, `FUN_0060B4B0`/`FUN_0060AF00`/bucket table `DAT_014B00D8`, not traced further)

| Offset | Field | Source | Note |
|---|---|---|---|
| `+0x00` | name hash | `Name` (row key, name-cache `0x0129EE6C`) | CRC-32 seed `0xFFFFFFFF` |
| `+0x04` | `Disable_Player` | bool | |
| `+0x05` | `View_Remotely` | bool | |
| `+0x08` | `Player_Weapon` | string → **`FUN_00B81220`** | the weapons-array resolver, exact address match to `spec-tables-weapons-combat.md` §1.6 |
| `+0x0C` | `Succes_State > Synced_Animation` | → `FUN_0095DA50` (u16) | **note the misspelling `Succes_State` (one `s`) is the actual XML tag name the reader looks for, confirmed in the disassembly** — a genuine engine authoring quirk, not a transcription error in this document |
| `+0x0E` | `Succes_State > Player_Is_Attacker` | bool | |
| `+0x10`–`+0x1F` | `Animated_NPCs > Animated_NPC > NPC_Name` ×4 | string handles | capacity 4, extra entries silently dropped |
| `+0x20`–`+0x7F` | `QTE_Nodes > QTE_Node` pointer list | ×24 (`0x18`) pointers | **per-sequence node capacity 24**; extra nodes beyond 24 in one `<QTE>` row are silently dropped (loop always advances past them, only the store is gated) |

### 7.2 `QTE_Node` record — **240 (`0xF0`) bytes**, array base `DAT_014A5950`, **global capacity across ALL sequences: 160 (`0xA0`)**

| Offset | Field | Source / resolver |
|---|---|---|
| `+0x00` | name hash | `Node_Name` (CRC-32 seed `0xFFFFFFFF`) |
| `+0x04` | HUD-interface key | `HUD_Interface` (CRC-32 seed `0xFFFFFFFF`) — **the join key into `hud_qte_interface.xtbl`'s own `Name` hash (§9), confirmed by matching real values (`"Rapid X"`, `"Rapid RT"`, `"Brute Attack 1"`, …) present in both files** |
| `+0x08` | `Fail_Time` | i32, if-present |
| `+0x0C`/`+0x10` | `Persona > Persona_Line` / `Line_Delay` | line id via `FUN_0070A2F0` / i32 |
| `+0x14`/`+0x18` | `Partner_Persona > Persona_Line` / `Line_Delay` | same |
| `+0x1C` | `Camera_Shake` | → `FUN_0057BEE0`, the camera-shake table resolver, exact address match to `spec-tables-weapons-combat.md` §1.6 |
| `+0x20` | `Health_Change` | f32, if-present |
| `+0x24` | `Player_Impact_Dmg` | i32, if-present |
| `+0x28`/`+0x2A` | `Enter_Action > Synced_Melee_Move > Melee_Move` (u16) / `Player_Is_Attacker` (bool) | melee move id via **`FUN_009828F0`**, exact address match to `spec-tables-weapons-combat.md` §1.6's melee-attack-table resolver |
| `+0x2C` | `Enter_Action > Player_Animation` | → `FUN_004BF810` (animation-state id; not further traced — likely `spec-tables-animation.md` territory) |
| `+0x30`/`+0x34` | `Looping_State > Synced_State > Synced_Animation` / `Player_Is_Attacker` | via `FUN_004BF810` / bool |
| `+0x38` | `Looping_State > Player_Animation_State` | via `FUN_004BF810` |
| `+0x3C`/`+0x3E` | `Additive_Action > Synced_Melee_Move > Melee_Move` / `Player_Is_Attacker` | via `FUN_009828F0` |
| `+0x40` | `Additive_Action > Player_Animation` | via `FUN_004BF810` |
| `+0x44` | `Additive_Action > New_Fail_Time` | u32, if-present |
| `+0x48` | `Additive_Action > Action_Count` | i32, if-present (default 0) |
| `+0x4C` | `Additive_Action > Shooting_Mode` presence flag | 1 if the child exists |
| `+0x50` | `Shooting_Mode > Recoil` | f32, always |
| `+0x54` | `Shooting_Mode > Synced_Partner_Flinch` | via `FUN_004BF810` |
| `+0x58`–`+0x64` | `Shooting_Mode > Restrict_Camera_Angle > Min/Max_Heading, Min/Max_Pitch` | f32 ×4, always, radians as authored |
| `+0x68`–`+0x74` | `Shooting_Mode > Target_Camera_Angle > Min/Max_Heading, Min/Max_Pitch` | f32 ×4, **deg→rad via `× DAT_012A30D8`** |
| `+0x78`–`+0xD7` | `NPC_Animations > NPC_Animation` list | ×4 slots × 24 bytes: `{matched-slot index (via a name compare against the row's `Animated_NPC` list), Persona_Line, Line_Delay, Enter_Action anim, Looping_State anim, Additive_Action anim}` |
| `+0xD8`/`+0xD9`/`+0xDB`/`+0xDC` | `On_Success > Interrupt`/`Keep_HUD`/`Kill_Partner` (bools) / `Repeat` (i32) | |
| `+0xE0` | `On_Success > Go_To_Node` | hashed node-name reference (CRC-32 seed `0xFFFFFFFF`) |
| `+0xE4` | `On_Fail > Go_To_Node` | same hash |
| `+0xE8`/`+0xDA` | `On_Fail > Force_Success` (bool, if-present) / `On_Fail > Keep_HUD` (bool) | |
| `+0xEC` | `Triggered_Explosion` | → **`FUN_00590D20`**, exact address match to `spec-tables-weapons-combat.md` §1.6's explosion-table resolver |

Field layout fully accounted for, `+0x00`–`+0xEF` = exactly 240 bytes, no gaps. **[CONFIRMED — disassembly, full record.]**

### 7.3 The claimed capacity overflow — RETRACTED 2026-09-23 (orchestrator spot-check)

**The original claim ("167 total `<QTE_Node>` elements against a hard runtime cap of 160, crossed inside `Mission16_FinisherQTE`, its last 7 nodes silently dropped") does not hold up.** `Mission16_FinisherQTE` is not a real, loader-fed row at all — it lives under the document's `<TableTemplates>` sibling (editor scaffolding), not under `<Table>` (confirmed directly: `root` children are `['Table', 'TableTemplates', 'TableDescription', 'EntryCategories']`; `Mission16_FinisherQTE` is one of `TableTemplates`'s 7 example rows, not one of `Table`'s 26 real rows). The "33 rows / 167 nodes, cumulative crossing 158→167" arithmetic was produced by walking the whole XML document in byte order rather than restricting to `Table`'s own children — the identical mistake class already caught and documented for `unlockables.xtbl`/`cheats.xtbl` (`spec-tables-progression.md` §14) and `vi_enter.xtbl`/`vi_exit.xtbl`/`vi_ride.xtbl` (`spec-tables-vehicle-world.md`).

The real, loader-fed data (`Table`'s 26 rows, 90 total `QTE_Node`, largest single row 12 in `Mission16_FinalQTE`) sits nowhere near the 160-slot cap. **The `+0x20` (per-sequence, capacity 24) and `+0xA0`-global (§7.2) capacity figures themselves are disassembly facts and stand** — only the claim that either cap is actually *hit* by real base-game data is withdrawn. Whether the loader (`FUN_0060C0A0`/`FUN_0060B820`) ever visits `<TableTemplates>` at all was not re-traced this pass; every other table's loader examined project-wide so far reads `<Table>` exclusively, so that is assumed here too pending a dedicated check. **[Original mechanism claims for the gate/counter/advance logic itself are not disputed — only the "this data trips it" empirical claim, which is retracted.]**

---

## 8. Camera-preset tables — **two genuinely different schemas, not one shared schema across all five**

The task brief for this batch hypothesized "likely one shared schema across all five" camera tables. **That hypothesis is only half right: this group splits into two unrelated families with two different schemas and two different loading mechanisms.**

### 8.1 Family A — `vehicle_cameras.xtbl` + `vehicle_group_cameras.xtbl`: the vehicle-info-table camera block **[CONFIRMED — disassembly, extends `spec-vehicle-data.md` §7.3]**

Both files are opened **from inside the same loader chain `spec-vehicle-data.md` §7.1 already documents** (`FUN_00ACE640`, the top-level `vehicles.xtbl`/`vehicle_defaults.xtbl`/`vehicle_groups.xtbl` loader) — both string literals' only base-game xref is inside that one function, and a second, DLC-framework xref for `vehicle_cameras.xtbl` sits inside `FUN_00ACE890` (the parallel per-DLC vehicle loader, paired with `FUN_00ACE440`'s per-DLC vehicle-entry load). Both files' row reader is **`FUN_00ACB000`**, which locates the matching entry in the global `0xB80`-byte vehicle-info table (`spec-vehicle-data.md` §7.1's lookup-by-name mechanism, `FUN_00AC27A0`/`FUN_00AC2560` — the identical addresses) and calls **`FUN_00ACA960(row, groupMode)`** — **the exact same function `spec-vehicle-data.md` §7.3 already cites as "the shared camera helper `FUN_00AC5CF0` / `00ACA960`; partially decoded, not tabulated."** This document tabulates it in full below, closing that flag. `vehicle_cameras.xtbl`'s `<Vehicle>` rows call it with `groupMode = 0`; `vehicle_group_cameras.xtbl`'s `<Vehicle_Group>` rows (read as part of the same `FUN_00ACE640` chain, into `spec-vehicle-data.md` §7.2's separate `0xB80`-stride group table at `0x0282CFA0`) call it with `groupMode = 1`. When a vehicle's own file supplies neither `Position_Based` nor `Sphere_Based` (below), its group row's values are inherited verbatim.

**Field table — extends `spec-vehicle-data.md` §7.3's `+0x628`–`+0x72B` "partially decoded, not tabulated" range and its `+0x870` flag word:**

| Offset | Field | Source | Note |
|---|---|---|---|
| `+0x624` | `Camera_Proximity_Lock_Y` | f32, if-present | matches `spec-vehicle-data.md` §7.3's own citation of this offset exactly |
| — | `Primary_Camera_Angle`, `Secondary_Camera_Angle`, `RC_Camera_Angle` | each dispatched to **`FUN_00AC5CF0`** with selector 0/1/2 | the "three camera-angle sets" `spec-vehicle-data.md` §7.3 names but does not expand; this pass did not decompile `FUN_00AC5CF0`'s own field layout further (OPEN, §17) |
| `+0x6EC`/`+0x6FC` | `Position_Based > Swing_Rate_Fwd`/`Swing_Rate_Rev` | f32, if-present — **when absent, a value is drawn from an RNG call (`FUN_00EA4ACC`) rather than a fixed default**, clamped to `[0,1)` | mutually exclusive with `Sphere_Based` below |
| `+0x70C`/`+0x710` | `Position_Based > From_Sticky_Fwd`/`From_Sticky_Rev` | f32; defaults to `Swing_Rate_Fwd`/`Rev` if absent, else independently RNG-drawn the same way | |
| `+0x6F0`/`+0x6F4` | `Position_Based > Heading_Retention_Normal`/`Turning` | f32, if-present (default `1.0`) | |
| `+0x700` | `Sphere_Based > Slerp_Value` | f32, always | mutually exclusive with `Position_Based` |
| `+0x704` | `Sphere_Based > From_Sticky_Slerp_Value` | f32; defaults to `Slerp_Value` | |
| `+0x708` | `Sphere_Based > Min_Angle` | f32, if-present, **deg→rad** | sets flag bit `0x80000000` of `+0x870` when present |
| `+0x870` bit `0x40000000` | Position/Sphere mode selector | set for `Sphere_Based`, cleared for `Position_Based`; inherited from the group row when neither block is present | |
| `+0xAF0` | `Camera_Roll > Roll_Type` | bool: true iff the text differs from the literal default `"none"` | vehicle-mode only (`groupMode=0`) |
| `+0xAF4` | `Camera_Roll > Intensity_Multiplier` | f32, if-present | |
| — | `skip_camera_transition` (row attribute text `"yes"`) | sets `+0x870` bit `0x8000` | |
| — | `use_alt_freckle_cam` (row attribute text `"yes"`) | sets `+0x870` bit `0x2000000`; inherited from group row when absent (vehicle mode) | |
| `+0x860` | `camera_fov_scale` | f32, if-present, default `1.0`, clamped `≥ 0` | |
| `+0x864` | `Max_FOV` | f32, always — **vehicle-mode only**; group-fill mode leaves this field untouched | matches `spec-vehicle-data.md` §7.3's own citation exactly |
| `+0x868` | `Min_Follow_Dist_Multiplier` | f32, if-present, default `1.0` | |

**[CONFIRMED — disassembly for the full record; the `FUN_00AC5CF0` camera-angle-set sub-layout is OPEN, flagged not forced.]**

### 8.2 Family B — `item_cust_cameras.xtbl` / `player_cust_cameras_new.xtbl` / `vehicle_cust_cameras_new.xtbl`: customization-viewer camera presets **[CONFIRMED — empirical for schema; mechanism CONFIRMED for one of three, OPEN for the other two]**

**Empirically identical schema across all three files** (§16): `<root><Table><camera><Name>...<camera_view_list><camera_view><Name>...<distance>...<height>...<subj_heading>...<cam_heading>...<pitch>...<field_of_view>...<snap>...</camera_view>...</camera_view_list></camera>`, with an optional empty marker element `<standard_def />` on roughly every second `camera_view` (paired HD/SD variants of the same named view, different numeric values). `item_cust_cameras.xtbl` and `player_cust_cameras_new.xtbl` each ship **exactly one `<camera>` row** (a single shared preset set — "placeholder" and a player-default set respectively); `vehicle_cust_cameras_new.xtbl` ships **96 `<camera>` rows**, one per vehicle. Row counts: item 76 `camera_view`s (34 `standard_def`-tagged), player 88 (44 tagged), vehicle 4,751 across 96 rows (1,102 tagged) — see §16.

**Loading mechanism — genuinely different between the three, an honest partial finding:**

- **`vehicle_cust_cameras_new.xtbl`** is confirmed loaded via the async resource-dispatch system (§1.2): `FUN_00A9ADE0` registers the request (resource name `"vehicle_customization_camera_file"`, filename `"vehicle_cust_cameras_new.xtbl"`), `FUN_00A9AEF0` polls the state (`FUN_00DAFB60`, states 2/3 = terminal) and triggers a finalize call (`FUN_00DAFAD0`) once ready. **[CONFIRMED — disassembly.]** The actual per-row XML parse that must eventually run (presumably still the ordinary `FUN_00DAC9A0`/`FUN_00DC4FF0` grammar, since the file itself is ordinary `.xtbl` text) was **not traced past the resource-ready callback** — OPEN.
- **`item_cust_cameras.xtbl` and `player_cust_cameras_new.xtbl` show zero direct code cross-references to their own filename bytes.** Both strings sit inside a **352-byte (`0x160`) repeating structure** — 128 bytes of what read as packed float constants immediately followed (with no NUL separator, i.e. genuinely part of the same static initializer) by the filename text in a fixed-size buffer — strongly suggesting a small static array of **`{default camera_view fallback floats[32], char filename[224]}`** records, one per customization-camera category, walked by a loop whose per-iteration filename address is computed (`base + i×0x160 + 0x80`), which is why no fixed-immediate xref lands on it. **This is a plausible, well-evidenced HIGH-CONFIDENCE hypothesis for the container shape, not a confirmed consumer** — the code that actually walks this array by index was not located this pass. **[HIGH CONFIDENCE — inferred for the container shape; OPEN for the consuming code and the exact per-category default-float layout.]** (A separate function, `FUN_007B2270`/`FUN_007B2650`, does reference the same memory region, but only as an incidental upper-bound sentinel for an unrelated UI-critical-resource preload list that happens to end exactly where `vehicle_cust_cameras_new.xtbl`'s string begins at `0x01156324` — this is **not** a real consumer of the table and is called out here only so a future pass does not re-chase it.)

---

## 9. `hud_qte_interface.xtbl` + `hud_qte_interface_presets.xtbl` — QTE on-screen prompt layout **[CONFIRMED — disassembly + empirical]**

**One shared loader, `FUN_008023D0()`**, opens `hud_qte_interface_presets.xtbl` first, then `hud_qte_interface.xtbl` — one literal each, both xrefs inside this one function. **Both tables are capacity-gated at 16 (`0x10`) rows: if either file's row count reaches 16, the entire load is refused** (the `if (count < 0x10)` guard wraps both parses). Real data: **11 presets, 13 `Hud_qte` rows** — both well under the cap (§16).

### 9.1 `hud_qte_interface_presets.xtbl` — `Hud_qte_preset` record, **80 (`0x50`) bytes**

`Name` (char[64], name-cache `0x0129EE6C`) at `+0x00` (relative to a record whose numeric fields follow at negative/positive offsets in the decompiled view — described here as one flat 80-byte record: 64-byte name + four `i32`), then `X_Position`, `Y_Position`, `X_Position_SD`, `Y_Position_SD` — all `i32`, always-readers, pixel coordinates for the widescreen (`X/Y_Position`) and 4:3 standard-def (`X/Y_Position_SD`) HUD placement of the named preset.

### 9.2 `hud_qte_interface.xtbl` — `Hud_qte` record, **56 (`0xE`-dword) bytes**

| Field | XML element | Type / lookup |
|---|---|---|
| name hash | `Name` (row key) | CRC-32 seed `0xFFFFFFFF` — **the join key back into `qte_sequences.xtbl`'s `HUD_Interface` field (§7.2)** |
| `Button_Type` | `Button_Type` | index into the 9-entry gamepad table `A,X,Y,LT,RT,LS_UP,LS_DOWN,LS_RIGHT,LS_LEFT` at `0x012FFEF8`, via the shared generic index-of-match reader `FUN_00DAC830` (`spec-tables-weapons-combat.md` §1.3) |
| `Button_Animation_Type` | `Button_Animation_Type` | index into a **3-entry** table at `0x012FFF1C`: `Mash Standard`, `Mash Fast`, `Alternate Triggers` (via the same shared reader) |
| `Button_Action` | `Button_Action` | the 166-entry CBA table, second copy at `0x0115C040` (§4.1) |
| `Axis_Action` | `Axis_Action` | the 34-entry CAA table, second copy at `0x0115C574` (§4.2) — real data's placeholder text `CAA_AXIS_UNBOUND` is **not** an actual table entry; it simply fails every comparison and falls through to the same `−1` "unmatched" result a genuine sentinel would give |
| `Axis_Dir_Pos` | `Axis_Dir_Pos` | bool |
| `Position` | `Position` | text looked up by exact `stricmp` against the just-loaded preset array's `Name` field (§9.1); on match, copies that preset's four position values into this record |
| `Position_PC` | `Position_PC` | same lookup, into a **second** copy of the four position fields — a PC-specific placement independent of the gamepad `Position` |

Real data: `Button_Type ∈ {A,X,Y,LT,RT,LS_UP,LS_DOWN}` (2 of the 9 possible values unused), `Button_Animation_Type ∈ {Mash Fast, Mash Standard}` (the third value, `Alternate Triggers`, unused in shipped rows), every `Position`/`Position_PC` value resolves to a real preset name (0 unmatched — §16).

---

## 10. `user_interface.xtbl` — UI cluster/resolution layout **[CONFIRMED — disassembly for the loader; schema CONFIRMED — empirical, byte offsets OPEN]**

**Loader `FUN_007EF4E0()`**, one literal, one xref. **Selects the `<UserInterface>` row whose `Name` text is exactly `"XBox2"`** — confirming, from the executable's own selection logic, that the shipped `"PC_old - not used"` row is genuinely dead data on this platform and the PC build's UI-cluster layout is sourced from the Xbox-360-named row (the same X360-canonicalisation pattern already found in `control_schemes.xtbl`, §3.1). It then reads the screen's current width/height (`FUN_005DCA00`/`FUN_005DCA10`), computes the aspect ratio, and picks between two resolution-ratio buckets (a `"4:3"`/`"16:9"`-style pair, `DAT_0115BA1C`/`DAT_0115BA20`) via an approximate float compare (`FUN_00DAD830`) against two ratio constants. §1.2 already settles this table's mechanism (ordinary C++ struct-fill, not Lua) — that determination did not require decompiling the four downstream fill functions (`FUN_007EF040`/`FUN_00802A70`/`FUN_007EF180`/`FUN_007EF3A0`) to their own field layout, and that deeper layout was not pursued this pass (OPEN, §17).

**Empirical schema** (a genuinely deep tree, unusually so for this group): `<UserInterface><ResolutionList><Resolutions><Resolution>` (text, e.g. `"4:3 (640x480)"`, `"16:9 (1280x720)"`) `<UIClusters><Cluster><ClusterName><XPosition><YPosition>`; separately, a per-`UserInterface`-row `<UIClusters><Cluster><ClusterOffsetList><ClusterOffset><ResolutionRatio><XOffset><YOffset></ClusterOffset>...<BitmapSlots><Part><Name><SlotList><SlotOffset><ResolutionRatio><XOffset><YOffset><Scale><Alpha></SlotOffset>...</SlotList><XOffset><YOffset><Scale><Alpha></Part>`. **This table supplies two of `spec-xtbl-format.md` §8's cited mixed-content examples directly**: `<ResolutionList>User Interface<Resolutions>...` (literal text `"User Interface"` sits before the first `<Resolutions>` child) and `<SlotList>0<SlotOffset>...` (a stray `"0"` text node before the first `<SlotOffset>` child) — both confirmed present in the real, decoded base-game file.

---

## 11. `control_scheme_text.xtbl` — on-screen control-legend text **[CONFIRMED — disassembly + empirical]**

**Loader `FUN_007DFD80()`**, one literal, one xref. `<root><Table><ControlScheme><Name>` (matched against a **hardcoded array of expected scheme-name literals**, e.g. `"Scheme A - On Foot"`, rather than accepted freely) `<Controls><Control><Control1>` (a display token, e.g. `R1`, `L2`) `<LocDesc>` (a **localization string key**, e.g. `CONTROL_DESC_GRENADE`, `CONTROL_DESC_PRIM_ATTACK` — a cross-reference into whatever table/system resolves `LOCTEXT`-style keys to display strings, not traced this pass) `<Platform>` (`360`/`PS3`/presumably others). **This is the source data for in-game control-tutorial/legend popups, and is a distinct concern from `control_schemes.xtbl`'s live button-binding data** — same general "control scheme" subject, different table, different purpose (display text vs. functional binding). Deeper field-offset mapping not pursued past the row-selection logic (OPEN, §17) — schema is fully confirmed from real data regardless.

---

## 12. `voice_control.xtbl` — AI voice-bark trigger/cooldown configuration **[CONFIRMED — disassembly + empirical]**

**Naming note, stated plainly so it is not misread:** despite the `control_` group context, this table has nothing to do with player input control — it configures **AI character voice-bark line playback** (cooldowns, delay windows, priority). It is grouped with the input-control tables here only because the assignment brief named it explicitly; its natural home is closer to the audio/dialogue territory of `spec-tables-audio-radio.md`/`spec-conversation-format.md`.

**Loader `FUN_00469140(int dlcIndex, char param2)`**, base-game literal `"voice_control.xtbl"` plus a **DLC-numbered variant** `sprintf("dlc%d_%s", dlcIndex, "voice_control.xtbl")`, existence-checked (`FUN_00DA90D0`) before opening — the standard per-DLC file convention already seen elsewhere in this project. `<root><Table><Entry>` rows, each packed into a single `u32` bitfield plus two unpacked fields:

| Bits | XML source | Encoding |
|---|---|---|
| `0x00003F` (0–5) | `Local_cooldown` | milliseconds ÷ 1000, clamped to 63 (seconds, 6-bit) |
| `0x0003C0` (6–9) | `Global_cooldown` | ms ÷ 1000, clamped to 15 (4-bit) |
| `0x007C00` (10–14) | `Min_delay` | raw value ÷ `0x46` (70), clamped to 31 (5-bit) |
| `0x1F8000` (15–20) | `Max_delay` | `(Max_delay − Min_delay)` ÷ `0x50` (80), clamped to 63 (6-bit) |
| `0x600000` (21–22) | `Play_percent` | bucketed into 4 tiers at thresholds 26/51/76 (i.e. quartile-ish bands, not linear) |
| `0xF800000` (23–27) | `Priority` | i32, if-present, default `10`, 5-bit field |

Unpacked: `Voiceline_id` (i32, the row's numeric identity, read before the bitfield is built), `External_source` (u32, if-present), `Play_event` (u32, if-present) — the latter two registered with a separate audio subsystem call (`FUN_00467950`) only when a debug/registration flag (`DAT_0135375E`) is set. **[CONFIRMED — disassembly for the complete bit-packing formula.]**

---

## 13. `credits_pc.xtbl` — credits screen content **[CONFIRMED — disassembly + empirical]**

**Loader `FUN_007C0990()`**, one literal, one xref. `<root><Table><Credits><Name><section_title><headings><headings><heading>` (a sub-heading label) `<type>` (index into a **4-entry** display-type table at `0x012FD3E0`: `Name - Role`, `Music`, `Image`, `Centered Single Line` — `Image` unused in the shipped file) `<items><items><name><desc><type_override>` (same 4-entry table, overrides the parent heading's `type` per-item). Builds three nested doubly-linked lists (section → heading → item) rather than flat arrays — record strides `0x10` (section header), `0x14` (heading), `0x14` (item), each read from a growable pool (`DAT_022829B0`/`B4`/`A4` families) rather than a fixed-capacity array, so **no capacity ceiling applies to this table**. One confirmed data-correction baked into the loader itself: **the credited name `"Neil Heilmann"` is silently rewritten to `"Nick Heilmann"` at load time** (a `stricmp` special-case), i.e. a real person's name was misspelled in the shipped XML and patched in code rather than in data. Real file: 7 `Credits` sections, **41 `<headings>` row records** — matching the loader's own per-row `0x14`-stride allocation, so this is the count that matters for capacity/structure purposes — **of which 37 carry a non-empty `<heading>` title-text leaf and 4 are genuinely titleless** (real authored rows with only `<type>`/`<items>`, no title — one is the very first row, in the `CREDITS_LOGO` section; confirmed 2026-09-28, Team B, independently re-verified by SPEC TEAM against the raw 123,846-byte file: 7 sections/41 rows/37 titled/4 titleless all exact). This document's original "37 headings" undercounted by conflating "row count" with "titled-row count" — both numbers are real, they just measure different things. 1,047 items (§16).

---

## 14. `control_filters.xtbl` + `control_parameters.xtbl` — animation blend-tree control-filter graph **[CONFIRMED — empirical for schema; loader located but not decompiled to field level — an honest partial]**

**Naming note, as with §12:** these two tables are **not** player-input-control tables despite the name. Their real subject — parameter names like `current speed`, `direction`, `turnto`, `combat ready`, `cower type`, `jump bool`, and filter operations `linear map`/`delta multiplier`/`cap` chained by `source_name` references — is an **animation blend-tree/locomotion control-parameter and signal-filter graph**, the same family `spec-xtbl-format.md` §8 already named in its "10 others" breakdown of tables lacking the `<root><Table>` shape (`control_filters`, `control_parameters`, alongside `blend_tree`/`state_machine`/`action_nodes`/`node_graph_files`). This document confirms that placement directly with real content and flags the natural home for any deeper pass as the animation-table territory (`spec-tables-animation.md`), not here.

**Shared loader `FUN_005D25F0()`** (one function services both filenames, one literal each, one xref each) was located and confirmed as the sole reader for both files, but its body (4,361 bytes, containing what appears to be an extensive hardcoded default parameter/filter list closely matching the real shipped data) was **not decompiled to field level this pass** — flagged OPEN rather than forced (§17).

**Empirical schema, both confirmed non-`<Table>`-wrapped, single-line-XML, lowercase-tag, all rows on one line each:**

- `control_filters.xtbl`: `<root><control_filters><control_filter><validated><name><type><source_name>` then, depending on `<type>` (`linear map` / `delta multiplier` / `cap`): `<input_min><input_max><output_min><output_max>` (linear map), `<multiplier>` (delta multiplier), or `<output_min><output_max>` (cap). **22 rows in the real file (corrected 2026-09-28, Team B: this document originally said 23, an off-by-one; re-verified directly against the real 5,351-byte file, 22/22 exact)**, forming filter chains (a filter's `source_name` can reference another filter by `name`, e.g. `"speed delta multiplier filter"` sources from `"speed linear map filter"`).
- `control_parameters.xtbl`: `<root><control_parameters><control_parameter><name><data_type>` (`bool`/`float`/`integer`) `<default_value>`, plus `<min_value>`/`<max_value>` for numeric types.

---

## 15. Literal-and-loader index, DLC content, and full cross-table reference map

### 15.1 Every table, its literal, loader, and xref count **[CONFIRMED — disassembly, all 17]**

| Table | Archive / index | Loader(s) | Xrefs |
|---|---|---|---|
| `control_binding_sets.xtbl` | `misc_tables.vpp_pc` #450 | `FUN_005BF2F0` | 1 |
| `control_schemes.xtbl` | `misc_tables.vpp_pc` #451 | `FUN_005C1860` | 1 |
| `control_scheme_text.xtbl` | `misc_tables.vpp_pc` #84 | `FUN_007DFD80` | 1 |
| `control_filters.xtbl` | `misc_tables.vpp_pc` #945 | `FUN_005D25F0` | 1 |
| `control_parameters.xtbl` | `misc_tables.vpp_pc` #946 | `FUN_005D25F0` (shared with `control_filters.xtbl`) | 1 |
| `qte.xtbl` | `misc_tables.vpp_pc` #556, `da_tables.vpp_pc` #10 | `FUN_006B8620` | 1 |
| `qte_sequences.xtbl` | `misc_tables.vpp_pc` #218 | `FUN_0060C0A0` → `FUN_0060B820` | 1 |
| `hud_qte_interface.xtbl` | `misc_tables.vpp_pc` #155 | `FUN_008023D0` (shared) | 1 |
| `hud_qte_interface_presets.xtbl` | `misc_tables.vpp_pc` #156 | `FUN_008023D0` (shared with `hud_qte_interface.xtbl`) | 1 |
| `user_interface.xtbl` | `misc_tables.vpp_pc` #262 | `FUN_007EF4E0` | 1 |
| `voice_control.xtbl` | `misc_tables.vpp_pc` #339 | `FUN_00469140` | 2 (base + DLC-pattern build) |
| `credits_pc.xtbl` | `misc_tables.vpp_pc` #89 | `FUN_007C0990` | 1 |
| `item_cust_cameras.xtbl` | `misc_tables.vpp_pc` #162 | not located (§8.2) | 0 (embedded in a data struct, §8.2) |
| `player_cust_cameras_new.xtbl` | `misc_tables.vpp_pc` #207 | not located (§8.2) | 0 (embedded in a data struct, §8.2) |
| `vehicle_cust_cameras_new.xtbl` | `misc_tables.vpp_pc` #267 | `FUN_00A9ADE0`/`FUN_00A9AEF0` (async resource dispatch, §8.2) | 2 real (+3 incidental, §8.2) |
| `vehicle_group_cameras.xtbl` | `misc_tables.vpp_pc` #275 | `FUN_00ACE640` → `FUN_00ACB000` → `FUN_00ACA960` | 1 |
| `vehicle_cameras.xtbl` | `misc_tables.vpp_pc` #265 | `FUN_00ACE640` and `FUN_00ACE890` → `FUN_00ACB000` → `FUN_00ACA960` | 2 |

### 15.2 Cross-table reference map

| From | To | Mechanism |
|---|---|---|
| `spec-save-format.md` §7.6/§7.7 Tables A/B | `control_binding_sets.xtbl` (§2) | this document's §4/§5 supplies the action-name↔index map and confirms the key-code/mouse/axis vocabulary those runtime tables serialize; the save-format spec is a **closed section, not re-derived** |
| `control_binding_sets.xtbl` `Action` / `control_schemes.xtbl` `Action` | the CBA/CAA compile-time enumerations (§4.1/§4.2) | direct string match; **166/34-entry tables, byte-identical to the second copy compiled into `hud_qte_interface.xtbl`'s own reader** |
| `qte_sequences.xtbl` `HUD_Interface` (§7.2) | `hud_qte_interface.xtbl` `Name` (§9.2) | both hashed with the same CRC-32/seed `0xFFFFFFFF`; real values (`"Rapid X"`, `"Brute Attack 1"`, …) confirmed present in both files |
| `hud_qte_interface.xtbl` `Position`/`Position_PC` (§9.2) | `hud_qte_interface_presets.xtbl` `Name` (§9.1) | direct `stricmp` lookup at load time, resolved into the record immediately (not a runtime string reference) |
| `qte_sequences.xtbl` node fields (§7.2) | `spec-tables-weapons-combat.md` §1.6's resolver table | **four exact-address matches**: `FUN_00B81220` (weapons), `FUN_0057BEE0` (camera shake), `FUN_009828F0` (melee attack), `FUN_00590D20` (explosions) |
| `vehicle_cameras.xtbl`/`vehicle_group_cameras.xtbl` (§8.1) | `spec-vehicle-data.md` §7.1–§7.3 | same vehicle-info-table (`0xB80`-byte entries, base `0x027C87B0`), same name-lookup functions (`FUN_00AC27A0`/`FUN_00AC2560`), same "shared camera helper" functions (`FUN_00AC5CF0`/`FUN_00ACA960`) that document names but leaves untabulated — **this document supplies that table** |
| `control_filters.xtbl`/`control_parameters.xtbl` (§14) | `spec-xtbl-format.md` §8's "10 others" non-`<Table>`-shaped family | same two filenames already named there in Team B's breakdown; this document supplies their real content and confirms the shape |
| `qte.xtbl`/`hud_qte`/QTE HUD | `spec-lua-bindings.md` §4/§8 hook names `hud_qte`, `hud_touch_combo`, `hud_btnmash`, `vint_remap_get_action_binding` | **named but not traced this pass** — plausible HUD-presentation-layer consumers of the data this document specs, not confirmed to be the same mechanism; flagged as a lead for a future pass, not asserted |
| `control_scheme_text.xtbl` `LocDesc` (§11) | a localization-string table | key format (`CONTROL_DESC_*`) noted, resolving system not identified this pass |

---

## 16. Validation

All 17 tables extracted with `tools/harnesses/vpp_modea.py` against `misc_tables.vpp_pc` (`scan_meshes.parse_entries` + `vpp_modea.modea_positions`/`get_entry_bytes_modea`), decompressed length checked against the directory's declared uncompressed size, and independently parsed with Python `re`-based tolerant extraction per `spec-xtbl-format.md` §7's rules (no strict XML parser used, consistent with the documented quirks elsewhere in the base tables).

| Check | Result |
|---|---|
| All 17 filenames found as exact literals, case-insensitive | 17/17 |
| All 17 extracted at exact declared uncompressed length | 17/17 (§1.1) |
| `control_binding_sets.xtbl`: 7 `Binding_Set` rows, 96 `Control` + 18 `Axis` = 114 bindings | confirmed by direct count |
| CBA table (166 entries): value range 0–163 exactly, 1 gap, 2 dup pairs → 164 possible slots | confirmed — matches `spec-save-format.md` Table A capacity exactly |
| CAA table (34 entries): value range 0–33 exactly, 0 gaps, 0 dups | confirmed — matches `spec-save-format.md` Table B capacity exactly |
| CBA/CAA tables: `control_binding_sets.xtbl`/`control_schemes.xtbl` copy vs. `hud_qte_interface.xtbl` copy | byte-identical, 166/166 and 34/34, 0 differences |
| CBA action-name union used across `control_binding_sets.xtbl` ∪ `control_schemes.xtbl` | 154 of 165 non-sentinel names (§5 item 5, OPEN) |
| CAA action-name union used across the same two files | 34 of 34 (clean) |
| `qte_sequences.xtbl`: real (`Table`) data is 26 `QTE` rows, 90 `QTE_Node` total, capacity 160 | **overflow claim RETRACTED 2026-09-23 — was a whole-document count including 7 `TableTemplates` scaffold rows** (§7.3) |
| `hud_qte_interface_presets.xtbl`/`hud_qte_interface.xtbl`: 11 presets / 13 rows against a 16-row cap on each | both well under cap, no overflow |
| `hud_qte_interface.xtbl`: every `Position`/`Position_PC` value resolves to a real preset name | 0 unmatched |
| `item_cust_cameras.xtbl`/`player_cust_cameras_new.xtbl`/`vehicle_cust_cameras_new.xtbl`: identical `camera`/`camera_view` schema | confirmed empirically across all three (76/88/4,751 `camera_view` rows respectively) |
| `credits_pc.xtbl`: 7 sections, 41 heading rows (37 titled + 4 titleless — corrected from an original undifferentiated "37"), 1,047 items; `type` values used: `Music`, `Name - Role`, `Centered Single Line` (3 of 4 defined) | confirmed by direct count |
| `control_filters.xtbl`: 22 rows (corrected from an original off-by-one 23), non-`<Table>` `<root><control_filters>` shape | confirmed, matches `spec-xtbl-format.md` §8's named family |
| `voice_control.xtbl` bit-packing formula | confirmed by disassembly of every shift/mask/clamp constant (§12) |

---

## 17. Open Items

1. **§5 item 5 — 11 of 165 compile-time CBA action names are never bound by name in either `control_binding_sets.xtbl` or `control_schemes.xtbl`.** Not chased into a third source file or a hardcoded-default code path this pass — a genuine, exhaustively-checked (both files, full union) partial gap, not a guess.
2. **`FUN_00AC5CF0`'s own field layout** (the "three camera-angle sets" of §8.1) was not decompiled to offset level.
3. **`item_cust_cameras.xtbl`/`player_cust_cameras_new.xtbl`'s consuming code path** (§8.2) — the container shape (a 352-byte struct array with an inline filename) is well evidenced but the code that walks it by index was not located.
4. **`vehicle_cust_cameras_new.xtbl`'s actual per-row XML parse** past the resource-ready callback (§8.2) was not traced — only the resource-request/poll layer is confirmed.
5. **`control_filters.xtbl`/`control_parameters.xtbl`'s shared loader `FUN_005D25F0`** (§14) was located but not decompiled to field level — flagged as animation-table territory rather than forced here.
6. **`user_interface.xtbl`'s four fill functions** (`FUN_007EF040`/`FUN_00802A70`/`FUN_007EF180`/`FUN_007EF3A0`, §10) were not decompiled to byte-offset level; the schema is fully confirmed empirically regardless.
7. **`control_scheme_text.xtbl`'s deeper field-offset mapping** (§11) past row selection was not pursued.
8. **`qte.xtbl`'s `Hud_Time`/`Cooldown_Time` scale constant** (`DAT_012A2D90`, §6) was not identified (plausibly a frame-rate or tick-rate constant).
9. **The two Lua/UI hook names `hud_qte`/`vint_remap_get_action_binding`** (`spec-lua-bindings.md` §4/§8) as possible consumers of this group's data — named as a lead, not traced (§15.2).
10. **`spec-lua-bindings.md` §5's Vint UI-layout-description mechanism** — `user_interface.xtbl` is now confirmed NOT to be its data source (§1.2); what UI layout data, if any, that mechanism actually reads (if it reads XML at all rather than being purely a runtime C++ object-construction registry) remains open in that document, not this one.
11. **`control_binding_sets.xtbl`'s `Common_Bindings` flag** (§2.1) — read into the `Binding_Set` header but no consumer found this pass.
12. **`control_schemes.xtbl`'s `On_Foot`-context match** (§3) uses what reads as a substring/contains test (`FUN_00DA7780`) rather than an exact comparison — not independently confirmed against its full calling convention.

None of these gaps block the deliverable: the two priority targets (`control_binding_sets.xtbl`/`control_schemes.xtbl`, §2–§5) are complete to the byte-offset and vocabulary-table level and directly close two of `spec-save-format.md` §7.6/§7.7's own open items; the QTE pair (§6–§7) is complete to the byte-offset level with a genuine, exhaustively-confirmed data anomaly found along the way; the camera-preset group (§8) is fully schema-confirmed for all five files with one family (`vehicle_cameras`/`vehicle_group_cameras`) closed to the byte-offset level and the other honestly partial; and the remaining six tables (§9–§14) range from fully closed (`hud_qte_interface*`, `credits_pc`, `voice_control`) to schema-confirmed-only (`control_scheme_text`, `control_filters`/`control_parameters`), each labelled accordingly rather than overstated.
