# Re-derivation from the executable: spec-tables-vehicle-world.md (Team A, 2026-10-01)

Inputs: the spec (`team-a/spec-tables-vehicle-world.md`, review status lines of 2026-09-30), the desk review
(`review/adv_tables-vehicle-world.md`), and the CrreishDump function dumps / xref listings listed in
`exe_index/tables-vehicle-world.txt` (bridge jobs `20261001T123123-team-a-ytgi` and `20261001T123128-team-a-dvje`).
All dumps are depth-0 listings of one function (callees not expanded); xref listings give the static dword at the
address plus every instruction that references it. Labels: "CONFIRMED — disassembly" only for what was read in
these dumps; otherwise HIGH CONFIDENCE / HYPOTHESIS / OPEN. No repo file was edited; this file is the only output.
Written incrementally, unit by unit, in spec order.

## §2 `vi_enter`/`vi_exit`/`vi_ride` — token table order and loop cap

Dumps read: `ytgi/func7/func_0x00b07920.txt` (whole body 0x00b07920–0x00b07c3c), `ytgi/glob/xref_0x0130e638.txt`.

**Loop cap — CONFIRMED — disassembly.** The `ParameterN` loop (0x00b07a8c–0x00b07b37) builds `Parameter1` in a stack
buffer, reads it with the text accessor, and after each stored index formats `Parameter%d` with N+1 and reads again;
the only exit is the accessor returning null. There is no counter compare, so writes continue at consecutive dwords
past `+0x14` (into `+0x18` `Animation`, `+0x1C`, `+0x20` and the next record). The spec's "no bound check" is right.

**Token compare — CONFIRMED — disassembly.** The inner compare at 0x00b07ae2–0x00b07afb indexes the pointer table at
`0x0130e638`, `_stricmp`s, and stops at index 0x53 = 83 (`CMP EDI,0x53; JL`): 83 entries, indices 0–82. On no match the
index register is zeroed before the store, so no-match stores 0. Entry 0 is `none` (dword at `0x0130e638` =
`0x0129b26c` → "none"), entry 1 is `any` (dword at `0x0130e63c` = `0x011861c0` → "any"). So `none` = 0 and the
no-match/`none` collision noted by the desk review is real.

**Table order — still OPEN.** The xref listing gives only the first dword of the table; the remaining 81 pointers are
not in any dump. The table has a second reader, `FUN_00b0ad10` (five indexed reads of `0x0130e638`), presumably an
index→name printer; not needed for the schema.

**Other details read in the same dump (CONFIRMED — disassembly):** row `Name` is copied bounded (0x40) into a stack
buffer, hashed, and only the hash is kept at `+0x00`; `Element` count at `+0x04` is first set to the child count, then
reset to 0 and incremented per element; `Animation` at `+0x18` is pre-set to −1 and overwritten with the resolver
result only if the child is present; `Animated_Camera_Tests`/`Camera_Pos` fills `+0x20` (count) and `+0x1C` (array,
stride 0xC, one vec3 reader call per `Camera_Pos`); both are zeroed first.

**Spec text change (§2.1 Parameter row):** replace "sample of 20 dumped … [desk review …] … [OPEN — … to be settled against
the executable.]" with: "83 entries, indices 0–82 (compare loop bound 0x53 read in `FUN_00b07920`); entry 0 is `none`
and entry 1 is `any` (first two pointers read from `0x0130e638`/`0x0130e63c`); the remaining 81 pointers in index order
are OPEN until the data range `0x0130e638`–`0x0130e784` is dumped. No-match stores 0, the same value as `none`."
Keep the sentence "No bound check exists" and add "(re-read 2026-10-01: the loop exits only when `ParameterN` is absent)".

**New Review status line (§2):** `**Review status (2026-10-01): CONFIRMED — disassembly for the 83-entry bound, the
no-cap loop, entries 0/1 of the token table and the record offsets (FUN_00b07920 re-read); OPEN: token-table entries
2–82 in index order (data dump 0x0130e638–0x0130e784 pending); row counts 106/99/69 corroborated by Team B.**`

---

## §3 `vehicle_interaction_info.xtbl`

Dumps read: `ytgi/func7/func_0x00b07330.txt` (whole body 0x00b07330–0x00b0791c), `dvje/func2/func_0x00ac1ba0.txt`,
`dvje/glob/xref_0x0130df84.txt`, `dvje/glob/xref_0x0130dfa4.txt`, `ytgi/glob/xref_0x0116bb1c.txt`,
`xref_0x0116bb20.txt`, `xref_0x0116bb48.txt`.

### 3.A Shared `0x0116bb20` table (desk review finding 1) — CORRECTED

`FUN_00b07330` never touches `0x0116bb20`. The `Capsule_Shape` path (0x00b074a8–0x00b074d9) is: find the child node;
if present call the enum matcher with (table `0x0130e8b8`, count 6, node, 0); if the result is ≥ 0, load the dword at
**`0x011861d0` + 4·index** and store it at sub-record `+0x04`. So the indirection table for `Capsule_Shape` is at
**`0x011861d0`**, not `0x0116bb20`. The xref listing for `0x0116bb20` shows exactly one reader, `FUN_008dc8c0`
(items_inventory, §15), so that address belongs to §15 only. The spec's §3.2 address is a transcription error.
The six values `0x0, 0xF, 0x10, 0x11, 0x12, 0xB` were not read in any dump (they would be at `0x011861d0`–`0x011861e7`)
— still HYPOTHESIS until that range is dumped. `0x0116bb1c` holds the tail bytes of the string `single_use` and
`0x0116bb48` the middle of `Default_Count`, so §15's "table[−1]" reads string bytes (see §15).

**Spec text change (§3.2 `+0x04` row):** strike "each maps through a 6-entry indirection table (`0x0116bb20`… values
`0x0, 0xF, 0x10, 0x11, 0x12, 0xB` — HYPOTHESIS on their consumer-side meaning) **[OPEN — … to be settled against the
executable.]**" → "each matched index is translated through a 6-dword table at `0x011861d0` (read in `FUN_00b07330`,
2026-10-01; the six values are not yet read from the image — HYPOTHESIS: `0x0, 0xF, 0x10, 0x11, 0x12, 0xB`); the
field is −1 when the child is absent or matches nothing. `0x0116bb20` is §15's table only."

### 3.B Seat resolver `FUN_00AC1BA0` and the seat-name tables — CONFIRMED in part, remainder OPEN

`FUN_00ac1ba0` (whole body read): for i = 0..7 it `_stricmp`s the argument against pointer table `0x0130dfa4`[i] and,
failing that, against `0x0130df84`[i]; the first hit returns i; after 8 misses it returns −1. Strings visible in the
dumps: `0x0130dfa4`[0] → "driver", [1] → "passenger 1"; `0x0130df84`[0] → "front driver", and the xref for
"front passenger" shows it is stored at `0x0130df88` = `0x0130df84`[1]. So the two-table, eight-index, generic-vs-
specific scheme is CONFIRMED — disassembly; the remaining 12 strings (`passenger 2`–`passenger 7`, `rear driver`,
`rear passenger`, `extra 1`–`extra 4`) are HIGH CONFIDENCE by pattern, not read.

Callers of `FUN_00ac1ba0` (from the dump header): `FUN_00ac9910`, `FUN_00ac9060` and the three sites in `FUN_00b07330`.
Both other callers also push the `Seat` literal (`0x01183968` xref), and `spec-tables-weapons-combat.md` §21 places
`FUN_00AC9060` in the vehicle-info code. That supports "shared with the vehicle table" but does not identify which
vehicle element (`ProhibitedGunfireSeats` or `Seat_Specific_Data`) they parse: OPEN until `FUN_00ac9060` and
`FUN_00ac9910` are dumped. `spec-vehicle-data.md`'s labels "Front Driver Side" etc. are not the literals this resolver
accepts; that spec should be re-checked against those two functions (cross-spec note, not an edit to this spec).

**Spec text change (§3.1):** replace "(`spec-vehicle-data.md` §7.3/§7.4)" by "(`spec-vehicle-data.md` §7.3)" and replace
the OPEN note by: "[Re-derived 2026-10-01: `FUN_00ac1ba0` read in full — two parallel 8-pointer tables, generic
(`0x0130dfa4`: entry 0 `driver`, entry 1 `passenger 1`) tried before specific (`0x0130df84`: entry 0 `front driver`,
entry 1 `front passenger`), return −1 after 8 misses; CONFIRMED — disassembly. Its other callers are `FUN_00ac9060` and
`FUN_00ac9910`, both of which read a `Seat` child in the vehicle-info code; which vehicle element they serve, and the
12 unread strings, remain OPEN. `spec-vehicle-data.md` §7.3's seat labels (`Front Driver Side`…) are descriptions,
not this resolver's literals.]"

### 3.C Reset / absent semantics in `FUN_00b07330` (desk review finding 3) — CORRECTED

Read at 0x00b073df–0x00b07412: before the `Seat_Info` loop, **all eight** sub-records (stride 0x24, 8 iterations) are
re-initialised: `+0x00` ← 0, `+0x04` ← −1, flags byte 1 (`+0x08`) ← its own bit0 only (bits 1–7 cleared), bytes 2 and 3
(`+0x09`, `+0x0A`) ← 0, byte 4 (`+0x0B`) bit0 cleared, `+0x0C` ← −1, `+0x10` ← −1, `+0x14` ← −1, `+0x18`/`+0x1C`/`+0x20`
← 0. So nothing "keeps whatever a previous refresh left there" except bit0 of flags byte 1; the garbled "byte4-adjacent
field" is byte 4 itself (its only flag, bit0, is cleared). Then, per matched `Element`:
- `Seat` (text, required accessor) → resolver; −1 → the whole `Element` is skipped (no field written).
- `Interaction_Point_Set` (text) hashed, linear scan of `DAT_02845ca0` (count `DAT_02845ca4`, stride 0xC, key `+0x00`),
  pointer or 0 → `+0x00`.
- `Capsule_Shape` as in 3.A; absent or no match → stays −1.
- `Flags` child present → each of the 24 literals is tested with the flag-presence helper and its bit is **assigned**
  (set or cleared) — bit positions read instruction by instruction match the spec's §3.2 tables exactly. If `Flags` is
  absent the bits stay at the reset values (all clear).
- After the `Flags` block, whether or not it existed, byte 1 bit0 is OR-ed in (0x00b07725): the "populated" marker is
  set for every matched `Element`, not "after any row".
- `Queue` (text, required accessor) → `+0x0C` pre-set −1, then all 8 `Queue 1`…`Queue 8` pointers at `0x0130e834` are
  compared (no early exit), index stored on match. Entries 0/1 confirmed `Queue 1`/`Queue 2`.
- `Primary_Access_Seat`/`Secondary_Access_Seat` if-present → resolver result (can be −1) → `+0x10`/`+0x14`.
- `Enter_Animations`/`Exit_Animations`/`Ride_Animations` if-present → hash, scan `DAT_02845c90`/`c88`/`c80` with counts
  `DAT_02845c94`/`c8c`/`c84` (stride 0xC), pointer or 0 → `+0x18`/`+0x1C`/`+0x20`.
- Row count `DAT_02845c9c` incremented per row; array base `DAT_02845c98`; record 0x164; `Name` bounded copy 0x40 then
  hash to `+0x40`. All CONFIRMED — disassembly.

**Spec text change (§3.2 tail paragraph):** strike "All four flags bytes … (only bit0 of byte1 and bit0 of byte4-adjacent
field get an explicit unconditional reset before parsing). **[OPEN — …]**" → "Before a row's `Seat_Info` is parsed all
eight sub-records are reset: pointers 0, `Capsule_Shape`/`Queue`/both access seats −1, flags bytes 2–4 cleared, flags
byte 1 reduced to its bit0 (the populated marker, the only value that survives a refresh). Each flag bit is then
assigned from the `Flags` child when present (absent `Flags` leaves all bits clear); byte 1 bit0 is set for every
`Element` whose `Seat` resolves. An `Element` whose `Seat` does not resolve is skipped entirely."
Also §3.2 `+0x08` row: "bit0 is always forced to 1 after any row is read" → "bit0 is set after every `Element` whose
`Seat` resolved".

**New Review status line (§3):** `**Review status (2026-10-01): CONFIRMED — disassembly for the record/sub-record
layout, all 24 flag bits, the reset rules, the `Queue`/point-set/animation cross-references and the two-table seat
resolver (FUN_00b07330 and FUN_00ac1ba0 read in full); CORRECTED: `Capsule_Shape` indirection table is at `0x011861d0`
(`0x0116bb20` belongs to §15); OPEN: the 6 indirection values, 12 of the 16 seat strings, and which vehicle element
`FUN_00ac9060`/`FUN_00ac9910` serve; NEEDS-DATA: 52-row patch copy unchanged.**`

---

## §4 `vehicle_interaction_point_sets.xtbl` — Direction loop bound

Dump read: `ytgi/func7/func_0x00b07090.txt` (whole body), `ytgi/glob/xref_0x0130e8a0.txt`, `xref_0x0130e8d0.txt`.

**CONFIRMED — disassembly: the loop runs 7 times.** At 0x00b07254–0x00b0726d the `Direction` compare indexes
`0x0130e8a0`, increments, and continues while the index is ≤ 6 (`CMP EBX,0x6; JLE`): indices 0–6, i.e. seven pointers,
the seventh being `0x0130e8b8` = §3's `Capsule_Shape[0]` ("stand"). So `stand` is accepted and stores 6. Entries 0/1 read:
"Left" (`0x011715bc`), "Right" (`0x011715f0`). Absent `Direction` → −1 (0x00b07238); present but unmatched → the field
is **not written** (keeps whatever the allocator left; not stated in the spec).

`Interaction_Point_Type` (0x00b071e0–0x00b07202): loop while index ≤ 0x11 → 18 entries, stores index−1 on match
(`None` → −1, `Enter Start` → 0; entries 0/1 read as "None"/"Enter Start"); unmatched → field not written. The text is
fetched with the required-text accessor and passed straight to `_stricmp`, so a row without `Interaction_Point_Type`
would dereference null — effectively mandatory. Element stride 0x1C, offsets `+0x04` vec3, `+0x10` float, `+0x14`,
`+0x18`–`+0x1B` four bool helper calls: all as the spec states. Row stride 0xC, `DAT_02845ca0`/`DAT_02845ca4`: as stated.

`0x0130e8d0` (first dword `0x400f0d84`, no code reference) is not a pointer — it is the end of the string-pointer run,
consistent with `Capsule_Shape` occupying `0x0130e8b8`–`0x0130e8cf` (6 pointers).

**Spec text change (§4.1 `+0x14` row):** strike "**[OPEN — desk review 2026-09-30: whether the `Direction` compare loop
… to be settled against the executable.]**" → "[Re-derived 2026-10-01: the compare loop in `FUN_00b07090` runs for
indices 0–6, so `stand` is accepted and stores 6 — the bleed is live. CONFIRMED — disassembly. A present but unmatched
`Direction` leaves the field unwritten; absent → −1.]" And in the `+0x00` row add: "an unmatched
`Interaction_Point_Type` leaves the field unwritten; the child is required (its text is used without a null check)".

**New Review status line (§4):** `**Review status (2026-10-01): CONFIRMED — disassembly (FUN_00b07090 re-read: 18-entry
type loop storing index−1, 7-iteration `Direction` loop that accepts `stand` = 6, all element offsets); 130-row count
corroborated by Team B; no open exe item.**`

---

## §6 `vehicle_animation_modifiers.xtbl`

Dumps read: `dvje/func2/func_0x00ac92d0.txt` (whole body 0x00ac92d0–0x00ac97ab), `ytgi/func7/func_0x00b81900.txt`,
`dvje/func2/func_0x00ac2400.txt`.

**Seat literal table (4 vs 8) — CONFIRMED — disassembly: four.** There is no table; the `Seat`'s `Name` (bounded copy,
0x40, into a zeroed stack buffer) is compared inline (0x00ac945e–0x00ac94c4) against `driver` → 0, `front passenger`
→ 1, `back left passenger` → 2, `back right passenger` → 3; any other name skips that `Seat` (jump to the next sibling,
0x00ac9753). The slots for indices 4–7 (`+0x95C`–`+0x9AB`) are never touched by this reader; the 160-byte gap in the
vehicle entry simply has room for eight.

**Element tree — CONFIRMED — disassembly** (answers the NEEDS-DATA item too): `Vehicle_Anim_Modifiers` →
`Human_Seat_Offset` (vec3 node, written to `DAT_027c2ecc`) and `Vehicles` → repeated `Vehicle` → `Name` (bounded copy,
**0x18 bytes**, so at most 23 characters are hashed), `Human_Scale`, `Human_Seat_Offset`, **`Seats` → repeated `Seat`**
→ **`Name`** (the seat name is a child, not the `Seat` element's own text), `Human_Seat_Offset`,
`Weapon_Animation_Groups` → repeated `Weapon_Animation_Group` → `Name`, `Human_Seat_Offset`, `Animation_States` →
repeated `Animation_State` → `Name`, `Human_Seat_Offset`. A `Weapon_Animation_Group` without a `Name` child is skipped
(but still counted in the allocation), likewise an `Animation_State` without `Name`.

**Offsets — CONFIRMED — disassembly:** `+0x484` `Human_Scale` (float helper); `+0x488`–`+0x490` vec3 or three zeroed
floats when absent; per seat i: `+0x90C + i·0x14` vec3 (zeroed when absent), `+0x918 + i·0x14` running count,
`+0x91C + i·0x14` array pointer, allocated as (Σ over groups of 1 + number of `Animation_State`) × 0x14. Sub-entry:
`+0x00` category index from the enum matcher on the 7-entry table returned by `FUN_00b81900`, `+0x04` −1 for the
group entry / animation-state resolver result for a state entry, `+0x08` vec3 or zeros.

**7 weapon-category literals:** `FUN_00b81900` returns `0x011899d4`, whose first pointer (`0x0118974c`) is the string
`WPNCAT_MELEE` — the first of the seven is CONFIRMED — disassembly; the other six are HIGH CONFIDENCE (same table is
pushed by `FUN_00b82200` and `FUN_00b85400`, the weapon-category code), unread.

**Vehicle lookup `FUN_00ac2400` — CONFIRMED — disassembly:** hashes the name, scans up to 0x84 = 132 vehicle entries at
`0x027c87b0` (stride 0xB80), accepts an entry only if its byte at `+0x874` has bit7 set and its dword at `+0x18` equals
the hash; returns the entry pointer or 0.

**Spec text changes (§6.1):** in the `+0x90C` row replace the OPEN note by "[Re-derived 2026-10-01: `FUN_00ac92d0`
compares the `Seat`/`Name` text inline against exactly these four literals and skips any other seat; CONFIRMED —
disassembly. Seat slots 4–7 are untouched by this reader.]". Before the table add: "`Vehicle` → `Name` (bounded copy of
0x18 bytes, hashed for `FUN_00AC2400`, which scans the 132-entry vehicle table at `0x027c87b0` by the hash at `+0x18`
of live entries), then `Human_Scale`, `Human_Seat_Offset`, and a **`Seats`** wrapper of repeated **`Seat`** elements,
each identified by a **`Name`** child." In the sub-entry table note "`Weapon_Animation_Group` → `Name` child;
`Animation_States` → `Animation_State` → `Name` child; a group or state without `Name` is skipped". Strike the last
OPEN note of §6.1 (element tree) and the data item in §23. In the `+0x00` sub-entry row: "7-entry weapon-class table
(`FUN_00B81900()` returns `0x011899d4`, entry 0 = `WPNCAT_MELEE`, CONFIRMED — disassembly; entries 1–6 HIGH
CONFIDENCE)".

**New Review status line (§6):** `**Review status (2026-10-01): CONFIRMED — disassembly (FUN_00ac92d0, FUN_00ac2400 and
FUN_00b81900 read in full): element tree incl. `Seats`/`Seat`/`Name`, four inline seat literals, all offsets and the
allocation rule; the 7 category literals beyond `WPNCAT_MELEE` are HIGH CONFIDENCE (data range 0x011899d4–0x011899f0
not dumped); no NEEDS-DATA item remains.**`

---
## §5 `vehicle_wheel_groups.xtbl`

Dumps read: `ytgi/func7/func_0x00a973e0.txt` (whole body 0x00a973e0–0x00a976a3), `ytgi/func6/func_0x00a96040.txt`
(whole body), `ytgi/func5/func_0x0084a280.txt` (whole body).

**Record and loader — CONFIRMED — disassembly.** Count → `DAT_027b0bc0`, array → `DAT_027b15f4`, stride 0x24.
`+0x00` `Name` hash; `+0x04` `Display_Name` through **`FUN_0084a1b0`** (the usual localisation helper — the spec is
right here); `+0x08` `Price` pre-set 0 then the if-present int helper; `+0x18` `Rim_Element` count; three separate
allocations of count×4 bytes: `+0x0C` rim `Display_Name` handles (`FUN_0084a1b0`), `+0x10` `Front_Rim`, `+0x14`
`Rear_Rim`; `+0x20` `S_Element` count; `+0x1C` spinner array (count×0x10). Spinner element: `+0x00` `Display_Name`
handle, `+0x04` `Price` (0 then if-present), `Front_Spinner` if-present → `+0x08`, and only then `Rear_Spinner`
(required accessor) → `+0x0C`; both 0 when `Front_Spinner` is absent. All exactly as §5.1 states.

**Resolver `FUN_00a96040` — CONFIRMED — disassembly (settles Slot-vs-Component and the handle type).** It hashes the
text, then for every `Slot` of `externalized_vehicle_components` (`DAT_027b15f0`, count `DAT_027b0bc4`, stride 0x10)
it reads the slot's **component count byte at `+0x0C`** and **component array pointer at `+0x08`**, and scans the
components (stride 0x3C) comparing each **component's `+0x00` dword** with the hash. It returns a **pointer to the
matching `Component` record**, or 0 after the last slot. So rim/spinner names are matched against the `Component`'s
own `Name` hash (which `FUN_00a96a80`, not dumped, must store at component `+0x00` — HIGH CONFIDENCE, consistent with
§23 item 4), never against the `Slot`; the stored "handle" is a record pointer.

**`FUN_0084A280` identity — CONFIRMED — disassembly that it is a distinct helper** (not a typo for `FUN_0084a1b0`;
`FUN_00a973e0` calls `FUN_0084a1b0`, `FUN_008dc8c0` calls `FUN_0084a280` twice). Its body: if the key is non-null,
hash it (`FUN_00d9e740`) and look it up (`FUN_00849950`); a non-zero result is returned as-is. Otherwise it builds a
0x200-byte stack string beginning with `!!` followed by the second argument (or the key itself when no second
argument, after a `FUN_00849800` call) and hands it to a string-table object at `0x0242dc28` (`FUN_00db0ba0`), i.e. it
creates a visible `!!`-prefixed placeholder for a missing localisation key. HIGH CONFIDENCE on that reading (callees not
dumped); label the helper "localised lookup with `!!` placeholder fallback".

**Spec text changes (§5.1):** replace the OPEN note after `Rim_Element` with "[Re-derived 2026-10-01, `FUN_00a96040`
read in full: the name is hashed and compared with the `+0x00` hash of every `Component` (stride 0x3C) of every
`Slot` (slot `+0x08` array, slot `+0x0C` count byte); the stored value is a pointer to the matching `Component` record,
or 0. CONFIRMED — disassembly. The `Slot` name is never compared.]" and change "resolved … handles" to "pointers to
`Component` records" in the `+0x10`/`+0x14` rows and in the `S_Element` sentence. In §15.1 `+0x08` row and §15/§5
status lines, replace the "distinct helper or typo is OPEN" remark by "`FUN_0084A280` is a distinct helper: hash
lookup via `FUN_00849950`, else a `!!`-prefixed placeholder string is created (read 2026-10-01)".

**New Review status line (§5):** `**Review status (2026-10-01): CONFIRMED — disassembly (FUN_00a973e0 and FUN_00a96040
read in full): record 0x24, parallel arrays, spinner element, and the resolver returns a pointer to the matching
`Component` record by `Name` hash; NEEDS-DATA (unchanged, now a pure check): all 51 `Front_Rim`/`Rear_Rim` values
should exist as `Component` names; 7-row count corroborated by Team B.**`

---

## §7 `externalized_vehicle_components.xtbl`

Dump read: `ytgi/func6/func_0x00a971f0.txt` (whole body 0x00a971f0–0x00a973dd).

**Slot stride — CONFIRMED 0x10; field map — CORRECTED.** Allocation is count·16 (`SHL EAX,0x4`), the per-slot pointer
advances by 0x10, and `FUN_00a96040`/`FUN_00a96130`-style scans use 0x10. The fields written per `Slot` are:

| Offset | Read from `FUN_00a971f0` |
|---|---|
| `+0x00` | **pointer to the `vehicle_cust_slots` row** whose `Name` hash (`+0x04` of that 0x10 record, array `DAT_027b15e4`, count `DAT_027b15dc`) equals the hash of this `Slot`'s `Name`; 0 if none. Not a hash, not matched by `FUN_00A96040`. |
| `+0x04` | `Camera_Info` hash when present, else the dword at `DAT_029c9964` |
| `+0x08` | pointer to the `Component` array (count·0x3C bytes) |
| `+0x0C` | **`u8`** count of `Components/Component` (the count is stored through an 8-bit register, so a slot with ≥ 256 components would wrap) |
| `+0x0D` | `u8` = 1, written unconditionally (the "marker" the spec placed at `+0x08` bit3) |
| `+0x0E`–`+0x0F` | not written |

Each `Component` is filled by `FUN_00a96a80(doc, slot, &local, node)` (not dumped) and then its byte at `+0x38` gets
bit3 OR-ed in — that part matches the spec. Count global `DAT_027b0bc4`, array `DAT_027b15f0`, wrapper path
`Custom_vehicle_properties` → `Component_Slots` → `Slot` → `Components` → `Component`: as stated.

**Spec text change (§7.1 table):** replace the four rows `+0x00`…`+0x10` with the table above and strike the OPEN
note; in the `+0x00` row of §5's cross-reference and in §21 say "`Front_Rim`… → `Component` `+0x00` hash (pointer
result)"; add a note "the `Slot` → `vehicle_cust_slots` link (`+0x00`) is a second cross-reference, by `Name` hash, read
2026-10-01". §23 item 4 (`Component` record internals) stays OPEN: next dump `FUN_00a96a80`.

**New Review status line (§7):** `**Review status (2026-10-01): CONFIRMED — disassembly for the `Slot` record
(FUN_00a971f0 read in full; stride 0x10, fields `+0x00` cust_slots row pointer, `+0x04` camera hash, `+0x08` array,
`+0x0C` u8 count, `+0x0D` marker byte) — CORRECTED from the 2026-09-30 text; OPEN: `Component` record internals
(`FUN_00a96a80` not dumped).**`

---

## §8.2 `vehicle_cust_interface.xtbl`

Dumps read: `ytgi/func5/func_0x00818630.txt` (whole body 0x00818630–0x008188a5), `ytgi/func6/func_0x00a96130.txt`
(whole body).

**`name` match key — CONFIRMED — disassembly: the `vehicle_cust_slots` `Name` hash.** `FUN_00a96130` hashes its
argument and compares it with `+0x04` of each `vehicle_cust_slots` record (`DAT_027b15e4`, count `DAT_027b15dc`,
stride 0x10); it returns a **pointer to that record** or 0. `SlotType` (`+0x08`) is never read. (The same function is
also called by `FUN_0081ef00`, `FUN_008202b0` and `FUN_00aaf9a0`; any spec describing it otherwise is wrong.)

**Record — CORRECTED in two places, otherwise CONFIRMED — disassembly.** 0xA4 bytes, vector-constructed with
`FUN_00818180` as element constructor. `+0x00` `Display_Name` unbounded byte copy (loop copies until NUL, no limit);
`+0x80` `Name` hash; `+0x84` `parent_category` hash or 0; `+0x98` count of `slots/slots`; `+0x9C` pointer to an array of
count dwords, each a **pointer to the resolved `vehicle_cust_slots` row (or 0)** — not an index; `+0xA0` flags byte:
bit0 assigned from the bool helper on `is_color_menu`; **bit1 set when an `is_wheel_menu` child node exists** (its
text is never read — presence only; the bit is never cleared); bit3 assigned from the bool helper on `is_perf_menu`.
Bit2 is not touched by this reader. The 16 bytes `+0x88`–`+0x97` are not written by `FUN_00818630` at all; whatever
they hold comes from the element constructor `FUN_00818180` (not dumped) — still OPEN, but it is now known that no
XML field lands there.

**Spec text changes (§8.2):** `+0x9C` row → "pointer to a `u32[]` array of pointers to the resolved
`vehicle_cust_slots` rows (0 for an unresolved name)"; `+0xA0` row → "bit1 `is_wheel_menu` (set when the child element
is present, regardless of its text)"; `+0x88`–`+0x97` row → "not written by the reader; initialised by the element
constructor `FUN_00818180` (not dumped)"; replace the OPEN note after `name` with "[Re-derived 2026-10-01:
`FUN_00a96130` compares the hash of `name` with the `Name` hash at `+0x04` of each `vehicle_cust_slots` row and
returns the row pointer; `SlotType` is not involved. CONFIRMED — disassembly.]" and drop the NEEDS-DATA item.

**New Review status line (§8.2):** `**Review status (2026-10-01): CONFIRMED — disassembly (FUN_00818630 and
FUN_00a96130 read in full): `name` resolves against `vehicle_cust_slots` `Name` hashes to a row pointer; flags bit0/
bit3 by value, bit1 by presence; OPEN only: contents of `+0x88`–`+0x97` set by constructor `FUN_00818180`; 4-row count
corroborated by Team B.**`

---
## §9.1 `vehicle_cust_color_pool.xtbl` — full record map

Dump read: `ytgi/func6/func_0x00a94d10.txt` (whole body 0x00a94d10–0x00a952dc).

`FUN_00a94d10(allocator, verifyFlag)`: with the flag clear (the load called from `FUN_00a952e0`) it counts `Color` rows
into `DAT_027b15d0`, allocates count×0x24 at `DAT_027b15d4`, and hashes the three special names once
(`Reflection_Cos_Min_Angles`, `Reflection_Inv_Range_Cos_Angles`, `Glass_Color` → a 3-entry local hash table). With the
flag set it re-walks the file and compares each row against the existing record (name by `_stricmp` against `+0x00`,
hashes by equality), returning at the first mismatch — a verification/reload mode.

**Record (0x24) — CONFIRMED — disassembly:**

| Offset | Field |
|---|---|
| `+0x00` | pointer to the row's `Name` string as returned by `FUN_00a74910(name, allocator)` — a string copy/intern (the verify pass `_stricmp`s the XML name against it), not a material handle |
| `+0x04` | `Name` hash, computed with **`FUN_00d9e7e0`** (not the `FUN_00d9e8b0` used by every other loader here — a different hash variant; which one `vehicle_cust_color_sets` uses for its lookup must match, see §9.2) |
| `+0x08` | pointer to the float pool: Σ over numeric shader values of 3 (`Vector_Element` or special kinds 0/1), 1 (`Float_Element`) or 4 (special kind 2, `Glass_Color`) floats |
| `+0x0C` | pointer to `u32[numericCount]` — per numeric shader value, its name hash (written by `FUN_00a94c60(node)` with the destination in ESI — HIGH CONFIDENCE that it hashes the `Shader_Values` entry's name; callee not dumped) or, for the three special names, the pre-hashed special |
| `+0x10` | pointer to `float*[numericCount]` — per numeric shader value, where its floats start in the pool |
| `+0x14` | pointer to the string-value buffer: **one 0x40-byte allocation** (not count×0x40); 0 when there are no string values |
| `+0x18` | pointer to `u32[stringCount]` name hashes of string-valued entries; 0 if none |
| `+0x1C` | pointer to `char*[stringCount]`, each pointing 0x40 bytes further into the buffer at `+0x14`; 0 if none |
| `+0x20` | `u8` count of numeric shader values |
| `+0x21` | `u8` count of string shader values |
| `+0x22`–`+0x23` | not written |

**Element tree — CORRECTED:** `Color` → `Name`, then **`Grid`** → repeated **`Shader_Values`** → `Value` → either
`String` (text non-empty ⇒ string-valued entry), or `Vector_Element` → `Vector` (vec3 reader; each component is
multiplied by 1/255 = 0.003921569 before storing, i.e. colours are authored 0–255) and/or `Float_Element` → `Float`.
`FUN_00a93990(node)` (not dumped) classifies the entry: kind −1 = generic (floats appended to the pool in document
order), kind 0/1/2 = the special names in the order hashed above (3 floats reserved for 0/1, 4 for 2; a
`Float_Element` for a special entry is stored at the slot index that function also returns). Only the first entry of
each special kind gets a slot; later duplicates fall through to the generic path. `FUN_00a94b70()` runs after each row
(no arguments, not dumped).

Hazard worth stating: a `Color` with two or more string-valued shader entries writes past the 0x40-byte buffer (each
string is copied bounded to 0x40 and the destination advances by 0x40 per string).

**Spec text change (§9.1):** replace the three rows with the table above and the tree sentence with the corrected
tree; `+0x00` row: "interned `Name` string pointer (`FUN_00A74910`) — CONFIRMED by the verify-mode `_stricmp`"; drop
"material handle … HYPOTHESIS"; add the 1/255 scaling and the `FUN_00d9e7e0` hash note; strike the OPEN label.

**New Review status line (§9.1):** `**Review status (2026-10-01): CONFIRMED — disassembly for the full 0x24 record
map, allocation sizes, `Grid`/`Shader_Values`/`Value` tree and the 1/255 colour scaling (FUN_00a94d10 read in full);
HIGH CONFIDENCE for the roles of `FUN_00a93990` (kind/slot classifier) and `FUN_00a94c60` (name hash writer), not
dumped; 694-row count corroborated by Team B.**`

---

## §10 `vehicle_surfing_style_two.xtbl`

Dumps read: `ytgi/func4/func_0x006c3eb0.txt` (whole body 0x006c3eb0–0x006c43c9), `ytgi/func3/func_0x006a4ad0.txt`
(whole body), `ytgi/glob/xref_0x012a2dd8.txt`, `xref_0x012a2d90.txt`, `xref_0x01117a4c.txt`.

**Constants.** `0x01117a4c` = `0x3f800000` = 1.0f (CONFIRMED — xref static value). `0x012a2d90` and `0x012a2dd8` are
doubles; the xref lists show only their low dword (0 for both, as for any round double), but the decompiler's
constant folding of the same image prints `* 1000.0` and `/ 100.0` at every use in `FUN_006c3eb0`, so **1000.0** and
**100.0** are HIGH CONFIDENCE (a dump of `0x012a2d94`/`0x012a2ddc` would close it). `spec-tables-audio-radio.md`'s "0 in
the static image" reading is the low dword only. `0x01119df8` is the double 0.44704 (same evidence).

**Rounding — CORRECTED: truncation, not rounding.** Every seconds→ticks store (0x006c3f48–0x006c3fa4 and the six
later sites) does FNSTCW, ORs 0xC00 into the control word (round-toward-zero), FLDCW, FISTP qword, and takes the low
dword. So ticks = trunc(seconds × 1000.0), i.e. 1.9999 s → 1999.

**Field rules — CONFIRMED — disassembly, all as the §10 table says, with these refinements:** `Max_/Min_Surfing_Time`
and `Max_/Min_Handstand_Time` are read into locals pre-set to 0, zeroed together if Max ≤ 0 or Max < Min, then
converted; `Max_Handstand_Percent_Reward` is read straight into `DAT_014c7724`, divided by 100.0 and clamped to
[0, 1.0]; `Surfing_Min_Speed` is clamped ≥ 0, multiplied by 0.44704 and squared with no upper cap (the square is
computed in double and stored as float); `Record_Display_Time`/`Record_Queue_Time` use the if-present float helper
(`DAT_014c771c`/`DAT_014c7720` pre-set 0); `Record_Threshold` clamped ≥ 0 then ÷ 100.0;
`Handstanding_Sensitivity_Multiplier` floored at 1.0; `Max_Respect`/`Max_Lifetime_Respect` ints floored at 0;
`Max_Cash` float floored at 0. `Vehicle_Surfing` is looked up on the opened document exactly as every other loader
looks up its row tag, so it sits at the same level as the other tables' row elements.

**`Balance_Bar_Params` names — CONFIRMED — disassembly (`FUN_006a4ad0`), in offset order into a 22-dword block at
caller object `+0x14`:** `+0x00` `Balanced_Region_Size`, `+0x04` `Balanced_Region_Size_Change`, `+0x08`
`Balanced_Region_Min_Size`, `+0x0C` `Balanced_Region_Acceleration`, `+0x10` `Balanced_Acceleration_Change`, `+0x14`
`Balanced_Acceleration_Max`, `+0x18` `Unbalanced_Region_Acceleration`, `+0x1C` `Unbalanced_Acceleration_Change`,
`+0x20` `Unbalanced_Acceleration_Max`, `+0x24` `Balancing_Acceleration`, `+0x28` `Balancing_Acceleration_Change`,
`+0x2C` `Balancing_Acceleration_Max` (all with the always-float helper, read from the `Balance_Bar_Params` child of
`Vehicle_Surfing`). The remaining ten dwords are runtime state initialised here: `+0x30` = 1.0, `+0x34` = 0, `+0x38` =
0, `+0x3C` = copy of `+0x00`, `+0x40` = copy of `+0x0C`, `+0x44` = copy of `+0x18`, `+0x48` = copy of `+0x24`, `+0x4C`
initialised by `FUN_00d9e140`, `+0x50` = 0, `+0x54` = a field (`+0x328`) of the `@@sr2_balance_meter` asset looked up
through `FUN_00e25850`/`FUN_00e26180`, or 0 when the asset is missing. Team B's expanded names were right.

**Spec text changes (§10):** in the first two rows replace "rounded" with "truncated toward zero (FISTP with RC = 11)";
in the `Max_Handstand_Percent_Reward` row replace the OPEN note with "(`DAT_012a2dd8` = 100.0 double — HIGH
CONFIDENCE from constant folding; low dword only in the xref)"; in the `Balance_Bar_Params` sentence spell out the 12
names above, replace "22-dword local struct" with "22-dword block at the surfing manager's `+0x14` (12 XML floats +
10 runtime dwords listed in `review/rederive_tables-vehicle-world.md` §10)", strike both OPEN notes and the NEEDS-DATA
item.

**New Review status line (§10):** `**Review status (2026-10-01): CONFIRMED — disassembly (FUN_006c3eb0 and
FUN_006a4ad0 read in full): all 16 destinations, clamps, truncating seconds→ms conversion, the 12 `Balance_Bar_Params`
names and the 10 runtime dwords; constants 1000.0 / 100.0 / 0.44704 HIGH CONFIDENCE (decompiler constant folding;
high dwords not in the xref listing); 1.0f CONFIRMED.**`

---
## §11 Shared LightSet — XML→struct mapping (`FUN_005984c0`) and the activation pass (`FUN_00598160`)

Dumps read: `dvje/func1/func_0x005984c0.txt` (listing 0x005984c0–0x0059937c skimmed for literals, decompile read in
full), `ytgi/func2/func_0x00598160.txt` (whole body), `ytgi/glob/xref_0x01117a4c.txt`.

**Loader `FUN_005984c0(filename, preparsedDoc, unused, allocator)` — CONFIRMED — disassembly.** Refuses when the
registry count `DAT_013de2bc` is already 0x80 (128 lightsets). With no pre-parsed document it checks the file exists
(`FUN_00da90d0`) and opens it (`FUN_00dac9a0`); otherwise it wraps the given document (`FUN_00daca90`). Registry
entry i at `0x013de2c8 + i·0x10`: `+0x00` filename hash, `+0x04` row array, `+0x08` `LightSet` count, `+0x0C` an
allocator-derived handle (vtable `+0x5C` call). Row array = count × **0x70**.

**`LightSet` row (0x70) — CONFIRMED — disassembly:** `+0x00` `Name` hash; `+0x04` `StartTime` (int, always); `+0x08`
`EndTime` (int, always); `+0x0C` `Light` array (count × 0xB0); `+0x10` `Light` count; `+0x14` byte zeroed (the mode
byte `FUN_00598160` writes); `+0x18` `Ambient_Grid` array (count × 0x88); `+0x1C` `Ambient_Grid` count; `+0x20`
`Exposure` (float, if-present, default −1.0); `+0x24` `RampExposure` (bool byte, default 0); `+0x30`–`+0x3F` one
16-byte quad zeroed; `+0x40`–`+0x6F` three 16-byte quads copied from the constant block at `0x012490c0`. So the
"two quads" wording means one quad at `+0x30` and a 48-byte block at `+0x40`; `FUN_00598160` fills exactly those (4
dwords from `FUN_00da38e0`, 12 dwords from `FUN_004add90`) — the desk review's reading was right.
Corrections to §11.1's field names: `+0x10` is the **`Light` count** (not a resource pointer); `+0x1C` is the
**`Ambient_Grid` count** (its non-zero test on unload clears `DAT_013de2c0`/`DAT_013de2c1`); `+0x20`/`+0x24` are
`Exposure`/`RampExposure` (fade-in runs when `RampExposure` is false and `Exposure` ≥ 0.0; the threshold is a literal
0.0 compare).

**`Lights` → `Light` record (0xB0) — CONFIRMED — disassembly:** `+0x00` pointer to an allocated copy of `Name`;
`+0x04` `Name` hash; `+0x08` `u16` `Type`: `omni` → 0, `circular spotlight` → 2, any other text leaves the field
unwritten; `+0x0C`–`+0x14` `Color` parsed with a three-float scan of the inline text; `+0x18` `Multiplier` (float,
always); `+0x1C`–`+0x24` `Color` × `Multiplier` (premultiplied copy); `+0x28` flags = 0x4200, OR 0xC00 when
`CastShadows` is true; `+0x2C` category mask: `Category_0` → 4, `Category_1` → 8, `Category_2` → 0x10, `Category_3` →
0x20, and if none is set all four (0x3C) are set; `+0x30`–`+0x3C` `Position` (x, y, z, z) and `+0x40`–`+0x4C` the same
four values again; `+0x50`–`+0x7C` 12 floats produced by `FUN_004cd740` from the two `Orientation` floats (a 3×4
orientation matrix — HIGH CONFIDENCE, callee not dumped); `+0x80`/`+0x84` `Hotspot` (two floats); `+0x88`/`+0x8C`
`Attenuation` (two floats); `+0x90` hash of `Slate_Name` (hash variant `FUN_00d9e7e0`) when present and non-empty;
`+0x98`/`+0x9C` two dwords copied from the slate object found by `FUN_005982e0` (fallback `FUN_00598340`), validated
by `FUN_00853b10`, else 0/0; `+0xA0` byte 0; `+0xA1` `Negative` (bool); `+0xA4`–`+0xAC` zeroed. **Not read by this
loader:** `Template`, `Indoor`, `Outdoor`, `LightCharacter`, `ShadowCharacter`, `LightLevel`, `ShadowLevel` — dead
elements in this reader (same family as §12.3's finding); `Negative` is read but was not seen in the three files.

**`Ambient_Grids` → `Ambient_Grid` record (0x88) — CONFIRMED — disassembly** (not present in this group's three
files, but part of the shared schema): `+0x08`/`+0x0C` slate pair from `Slate_Name` (lookup `FUN_004588f0`, gated on
`DAT_02444dac` ≥ 1 and two flag tests); `+0x10`–`+0x18` `Level_Ambient_Color`; `+0x1C`–`+0x24` `Char_Ambient_Color`
(both three-float scans); `+0x28` `Cutscene_Frame_Start`, `+0x2C` `Cutscene_Frame_End`, `+0x30`
`Cutscene_Shot_Start`, `+0x34` `Cutscene_Shot_End` (ints, −1 if absent); `+0x38`–`+0x40` `Main_Light_Color` and
`+0x44`–`+0x4C` `Light_Back_Color` (three floats, −1,−1,−1 if absent); `+0x50` `Shadow_Hardness`, `+0x54`
`Light_Amplication` (sic), `+0x58` `Contrast`, `+0x5C` `Bleach` (floats, −1.0 if absent); `+0x60` `Light_Map_DynRange`
(1.0 if absent); `+0x64` `Brightness`, `+0x68` `Saturation` (pre-set −1.0, if-present); `+0x6C`–`+0x74` `Tint_Color`
(pre-set −1s, three-float scan if present); `+0x78` `Bloom/Radius`, `+0x7C` `Bloom/Cutoff`, `+0x80` `Bloom/` a third
child whose literal at `0x0111d7e0` was not resolved in the dump (OPEN: one string read).

**Spec text changes (§11.1):** rename the four runtime fields as above; replace "two reflection-probe colour quads at
`+0x30`–`+0x3C` and `+0x40`–`+0x6C`" with "a 16-byte quad at `+0x30` (zeroed at load) and a 48-byte block of three
quads at `+0x40` (initialised from the constant block at `0x012490c0`)"; strike "(and the object-creation path
`FUN_005984c0`, not decompiled this pass)". Add a §11.3 "XML→struct mapping (re-derived 2026-10-01)" with the three
tables above, and in §11.2 mark `Template`/`Indoor`/`Outdoor`/`LightCharacter`/`ShadowCharacter`/`LightLevel`/
`ShadowLevel` as not consumed by `FUN_005984c0`. The "two of the three consumers" sentence stays OPEN (not in scope of
these dumps).

**New Review status line (§11):** `**Review status (2026-10-01): CONFIRMED — disassembly for the LightSet (0x70),
Light (0xB0) and Ambient_Grid (0x88) records, element names, defaults and the registry (FUN_005984c0 and FUN_00598160
read in full); HIGH CONFIDENCE for the orientation-matrix helper; OPEN: the third `Bloom` child literal at
`0x0111d7e0`; 1 × 3 row counts corroborated by Team B.**`

---

## §12 `level_objects.xtbl` (`FUN_008e7490`)

Dumps read: `ytgi/func5/func_0x008e7490.txt` (decompile read in full, listing checked at 0x008e7598–0x008e75cd and the
literal sites), `ytgi/glob/xref_0x0126d2cc.txt`, `xref_0x01115d6c.txt`, `xref_0x012a2dd0.txt`.

**Defaults — CONFIRMED (xref static values and the immediate stores in the builder):** `DAT_0126d2cc` = `0x3f000000`
= **0.5** (`Friction`); `DAT_01115d6c` = `0x3d4ccccd` = **0.05** (`Restitution`, `Angular_Damping`); `DAT_012a2dd0` =
`0x3c23d70a` = **0.01** (`Linear_Damping`); `Surface_Velocity` 0; `Buoyancy_Modifier` and `Vehicle_Repulsor_Scale`
1.0 (`0x3f800000`). `DAT_0125e270` = `0x00000000` = 0.0f (xref static value; the `Lifetime` threshold).

**`Lifetime_seconds` — CONFIRMED — disassembly (0x008e7598–0x008e75ca):** if-present float; if absent or < 0.0 the
field is 0 (base mode); else `(int)trunc(value × 1000.0 + c)` where the double at `0x012a2dc0` is 0.5 by the
decompiler's constant folding (HIGH CONFIDENCE; low dword only): round-half-up to milliseconds.

**`Weight` — CONFIRMED — disassembly:** read as an **integer** (int helper), converted to float; a negative integer
has 2^32 added (i.e. it is reinterpreted as unsigned), then × **0.45359236** — pounds to kilograms.

**Flag bit map — CONFIRMED — disassembly.** `+0xC0`: bits 0 and 1 both set when `Anchored` is present (cleared
together with bit 13 when absent, base mode); bit 13 `Anchored/Dislodge_On_Death`; bit 24 `Movable_By_Humans` (a
**row** child, bool helper; cleared when absent in base mode); bit 25 `Vehicle_Obstacle` = `false`; bit 26
`Vehicle_Obstacle` = `unanchored` (row child; 3-literal table `true`/`false`/`unanchored` built on the stack, `true`
sets nothing); bit 28 `Collision_Sound/VehicleIVS` present; bit 29 `Collision_Sound/ObjectMVS` present; bit 30
`Anchored/Dislodge_Notoriety`; bit 31 `Flag` `receives_bullet_impulse`. `+0xC4`: bit 19 set with `Anchored`; bit 20
cleared after the `Anchored` block (meaning unknown); the `Flags/Flag` literals: 0 `disappear_on_death`, 1
`disable_lights_on_dislodge`, 2 `disable_effects_on_dislodge`, 3 `ignore_human_collision`, 4 `camera_collide`, 5
`vehicle_camera_collide`, 6 `ignore_bullet_collision`, 7 `bullets_penetrate`, 8 `do_not_aim_at_me`, 9
`fire_hydrant`, 10 `non_walkable`, 11 `can_walk_up`, 12 `nearby_player_despawn`, 13 `shatters_against_world`, 14
`blackjack`, 15 `poker`, 16 `zombie`, 17 `basketball`, 18 `tv`, 21 `no_detour_until_moved`, 22
`does_not_generate_detour`, 23 `generate_detour_even_if_in_air`, 24 `breakable_glass`, 25 `ai_los_ignore`, 26
`damaged_by_players_only`, 27 `no_brute_pickup`. Before the chain (base mode) `+0xC4` is masked with `0xFC1FC000`,
i.e. bits 0–13 and 21–25 are cleared while bits 14–20 and 26–31 are left as allocated. Each `Flag` child's text is
compared (`_stricmp`) against the 26 literals; unknown text sets nothing.

**Other reads (CONFIRMED — disassembly):** `Material` bounded copy 0x20 → `FUN_006f76f0` → `u8` `+0x34`;
`Hitpoints` int `+0x30`; `Anchored/Dislodge_Hitpoints` int `+0x58`, `Anchored/Dislodge_Effect` bounded 0x40 →
`FUN_005c50b0` `+0x5C` (−1 when `Anchored` absent), `Anchored/Coins_Released` `+0x60`, the latter two only when the
feature gate `FUN_008e68f0(2)` is on (when it is off the builder returns right after `Dislodge_Hitpoints`, skipping
everything below — a gate, not a schema rule); `Collision_Sound/FoleyCollision` text → `FUN_00561370` `+0x74`,
`VehicleIVS` `+0x68`, `ObjectMVS` `+0x6C`; `Emitting_Sound` if-present → `FUN_00462960` `+0x64`; `Death_Effect` and
`Death_Explosion` are **child nodes** whose text is copied bounded (0x80) then resolved (`+0x78` −1 default, `+0x88` 0
default); `Death_Money/Min` `+0x8C`, `Death_Money/Max` `+0x90`, `Death_Money/Just_Coins` `+0xA1`; `center_of_mass/
com_offset` `+0xA8`, `com_offset_corpse` `+0xB4`. Two feature gates `FUN_008e68f0(0)`/`(3)` must both be on in base
mode or the builder stops after `Material`. Offsets `+0x70`, `+0x7C`–`+0x87` are not written by this builder.

**`Death_Money` vec3 — CORRECTED/OPEN.** The builder calls the vec3-with-presence helper with the element name
**`Cash_Out_Point`** on the `Death_Money` node (`+0x94`, presence byte `+0xA0`). It does not ask for `Death_Money`'s
own `X`/`Y`/`Z`. Unless `FUN_00dacfb0` (not dumped) falls back to the parent's `X`/`Y`/`Z` when the named child is
missing, the real rows' `<X><Y><Z>` under `Death_Money` are never read and `+0xA0` stays 0. The §12.1 claim
"confirmed empirically to come from `Death_Money`'s own `X`/`Y`/`Z` children" describes the data, not the reader.
Next dump: `FUN_00dacfb0`.

**Spec text changes (§12.1):** `+0x38` row → "0 if absent or negative; else trunc(seconds × 1000.0 + 0.5) ms";
`+0x3C` row → "integer pounds, read as unsigned 32-bit, × 0.45359236 → kilograms"; `+0x40`–`+0x4C` defaults → 0.5,
0.05, 0.05, 0.01; `+0x94` row → "`Death_Money/Cash_Out_Point` vec3 (+ presence byte `+0xA0`); whether the helper
falls back to `Death_Money`'s own `X`/`Y`/`Z` is OPEN (`FUN_00dacfb0`)"; `+0xC0` row → the bit list above (bits 0 and
1 both set); `+0xC4` row → "bit 19 `Anchored` marker; `Flag` bits 0–18 and 21–27 as listed in §12.2 (re-derived
2026-10-01)"; §12.2 → append the bit number to each literal; strike all four OPEN notes.

**New Review status line (§12):** `**Review status (2026-10-01): CONFIRMED — disassembly (FUN_008e7490 read in
full): all 27 flag bit positions, the `+0xC0` bit map, element paths, `Weight`/`Lifetime` formulas and the four
defaults (0.5 / 0.05 / 0.05 / 0.01, xref static values); OPEN: `Death_Money` vec3 is requested as a `Cash_Out_Point`
child (`FUN_00dacfb0` fallback behaviour); the `+0x70`/`+0x7C`–`+0x87` bytes are untouched by the builder; 149-row
count corroborated by Team B.**`

---

## §13 `props.xtbl` — literal spelling

Dump read: `ytgi/func3/func_0x0060ea40.txt` (whole body 0x0060ea40–0x0060eb26).

**CORRECTED — the literals have spaces.** The loader compares the bounded (0x100) `Name` copy against a 14-pointer
table at **`0x012ed268`** (loop bound 0xE); entries 0 and 1 are read as **"assault thug"** and **"assault killa"**
(`0x011290cc`, `0x011290bc`). The block index is the table position: `Num_Props` is stored at `DAT_014b02e4 +
4·index`. The remaining 12 strings follow the spec's order only by HIGH CONFIDENCE (data range `0x012ed268`–
`0x012ed29f` not dumped). `Num_Props` uses the always-int helper into a local before the match, so an unmatched row
costs nothing. `memset(0x38)` before parsing: as stated.

**Spec text change (§13):** replace the underscore list and the OPEN note with "14 literals in a pointer table at
`0x012ed268`, spelled with spaces — entries 0/1 read as `assault thug`, `assault killa` (CONFIRMED — disassembly);
the rest, in the order below, HIGH CONFIDENCE: assault gangsta, assault kingpin, kill thug, kill killa, kill gangsta,
kill kingpin, shooting thug, shooting killa, shooting gangsta, shooting kingpin, destroy gang car, collect item
pickup. The block index equals the table position."

**New Review status line (§13):** `**Review status (2026-10-01): CONFIRMED — disassembly (FUN_0060ea40 read in full):
14-entry table at 0x012ed268, space-separated literals (entries 0/1 read), index = table position; entries 2–13
HIGH CONFIDENCE pending a data dump; 14-row count corroborated by Team B.**`

---

## §14 `triggers.xtbl`

Dumps read: `ytgi/func6/func_0x0093e0c0.txt` (decompile in full, listing at 0x0093e130–0x0093e162),
`ytgi/func5/func_0x00802f60.txt` (whole body).

**Hash-table layout — CONFIRMED — disassembly.** `FUN_00dab330(name, 0x14)` gives a bucket 0–19; bucket heads are a
byte array at `0x02622f0d` (20 bytes), chain-next a byte array at `0x02622f21` (20 bytes), 0xFF = end; the comparator
is a virtual call through the object at `0x02622f08` (its first vtable slot) with (key pointer, name); keys are 20
dwords at `0x02622f38`; the slot→instance index is 20 dwords at `0x02622f88`. The odd base addresses are byte
arrays inside one object, so they are fine. Capacity is 20 — exactly the real row count.

**Unmatched name — CORRECTED.** When the chain ends without a match the index is set to **−1 and parsing continues**
(0x0093e15a → 0x0093e15d): `Effect`, `Icon`, frame, `Foley`, `UseMessage` and the flags are written to instance slot
−1, i.e. the 0x24 bytes just below the array base (`0x02623284`–`0x026232A7`). Nothing is "silently discarded".

**`Icon` → `FUN_00802f60` — CONFIRMED structure, HIGH CONFIDENCE meaning.** The text is compared against the first
pointer of each 0xC-byte entry of a 108-entry table at `0x012fff78` (0x510 bytes; entries 0/1 read as
`map_other_replaceme`, `map_other_blip_human`); a hit returns the entry index. Otherwise it is compared against a
second table of 0x24-byte inline strings starting at `0x0115d2f4` (entries read: `icon_class_kill`,
`icon_class_location`), count `DAT_012fff50`; a hit returns the dword stored 4 bytes before that string. Else −1.
So `+0x04` is a map-blip/icon index.

**Icon-frame rule — CORRECTED.** Only when `IconType` matches entry 1 (`Store`) of the 2-pointer table at
`0x011708dc`: `0x18 ≤ icon ≤ 0x21` → 7; `0x22 ≤ icon ≤ 0x23` → 8; any other value leaves `+0x0C` unchanged. Hazard: the
icon value lives in a local that is initialised once before the row loop, so a row with `IconType` = `Store` and no
`Icon` of its own uses the previous row's `Icon` value.

**Fields and flag bits — CONFIRMED — disassembly:** `+0x00` `Effect` (`FUN_005c50b0`), `+0x04` `Icon`, `+0x08` `Foley`
(`FUN_00462960`), `+0x0C` frame, `+0x10` `UseMessage` (`FUN_0084a1b0`), `+0x20` = 1 when `Flags` exists, `+0x21` bits:
0 `check_npcs`, 1 `continuous_activation`, 2 `disabled_for_demo`, 4 `ignore_vehicles`, 5 `ignore_on_foot`; base
`0x026232A8`, stride 0x24. As stated.

**Spec text changes (§14):** replace "A row whose name has no match in that table is silently discarded — nothing is
stored." with "A row whose name has no match is parsed with slot index −1, so its fields land in the 0x24 bytes
before the array base (`0x02623284`); the table holds 20 slots, the shipped file has 20 rows."; `+0x0C` row → the
corrected rule and the stale-`Icon` hazard; `+0x04` row → "map-blip index: position in the 108-entry blip-name table
at `0x012fff78`, else the icon-class value stored before each 0x24-byte name at `0x0115d2f4`, else −1"; replace the
"odd, unaligned addresses" note with the layout above.

**New Review status line (§14):** `**Review status (2026-10-01): CONFIRMED — disassembly (FUN_0093e0c0 and
FUN_00802f60 read in full): hash-table layout (20 slots), icon resolver, frame rule 0x18–0x21 → 7 / 0x22–0x23 → 8, flag
bits; CORRECTED: unmatched names write to slot −1 rather than being discarded; 20-row count corroborated by Team B.**`

---

## §15 `items_inventory.xtbl`

Dumps read: `ytgi/func5/func_0x008dc8c0.txt` (decompile in full, listing at the literal sites),
`ytgi/func5/func_0x008dcb10.txt`, `ytgi/func5/func_0x008dcec0.txt` (whole bodies), `ytgi/glob/xref_0x0116bb1c.txt`,
`xref_0x0116bb20.txt`, `xref_0x0116bb48.txt`.

**Live bit `+0x30` vs `+0x2C` — CONFIRMED `+0x30` bit0 in all three functions.** The loader ORs 1 into the byte at
record `+0x30`; the resolver `FUN_008dcb10` tests `byte [rec+0x30] & 1` over 0x6E = 110 slots from `0x0250adc0`; the
save loader `FUN_008dcec0` walks from `0x0250adc4` (record `+0x04`, the hash) to `0x0250c41c` (= base + 110·0x34) and
tests `byte [ptr+0x2C] & 1` — the same byte, `+0x30` from the record base. `spec-save-format.md` §10.3's "+0x2C" is
measured from the hash field, not the record; the conflict is an offset-base difference, not a disagreement.
`+0x2C` holds `Use_Script` as the spec says.

**`Use_Script` table — CONFIRMED — disassembly:** `FUN_00dac830(table 0x01306704, 1, node, 0)`; the single literal at
`0x01306704` is the pointer `0x0129b26c` = "none". The result indexes `0x0116bb20`: index 0 → dword 0 (`none` → 0);
no match → index −1 → the dword at `0x0116bb1c` = `0x00006573`, the last bytes of the string `single_use` — a
constant 0x6573 for any non-`none` text. The address belongs to this table only (§3 corrected).

**Other fields — CONFIRMED — disassembly:** `+0x00` interned `Name` (`FUN_00a74910`), `+0x04` hash via
`FUN_00d9e7e0` (the same hash variant §9.1 uses, not `FUN_00d9e8b0`); `+0x08`/`+0x0C` `DisplayName` through
`FUN_0084a280(text, namePtr)` (placeholder fallback keyed on the item's own `Name`, §5) then `FUN_00849ff0`, with
the placeholder `0x0111fe04` / 0 when absent; `+0x10` `Bitmap` (required text; empty → −1 else `FUN_00e22290`); `+0x14`
`Impact_shape_min_offset` (if-present, 0); `+0x18` `Cost` (−1), `+0x1C` `Default_Count` (1), `+0x20` `Max_Inventory`
(= `Default_Count`); `+0x24` `Description` (`FUN_0084a280(text, text)` + `FUN_00849ff0`, 0 if absent); `+0x28` `u8`
load ordinal, restarted at 0 per call; count `DAT_0250ad90` incremented per kept row; base pass keeps every row,
DLC pass compares `Framework` (default `main`). The resolver also matches the query against the wide-character
display string at `+0x08` (`_wcsicmp`), so lookups succeed by `Name` or by displayed text.

**Spec text changes (§15.1):** `+0x30` row → strike the OPEN note, add "(re-derived 2026-10-01: loader, resolver
`FUN_008DCB10` and save loader `FUN_008DCEC0` all use record `+0x30` bit0; the save-format spec's `+0x2C` is relative
to the hash field)"; `+0x2C` row → "`none` → 0; any other text → the dword before the table, `0x00006573`
(string bytes of `single_use`), read 2026-10-01"; `+0x08` row → the `FUN_0084A280` description from §5; `+0x04` row
→ "hash via `FUN_00d9e7e0`".

**New Review status line (§15):** `**Review status (2026-10-01): CONFIRMED — disassembly (FUN_008dc8c0, FUN_008dcb10,
FUN_008dcec0 read in full): live bit `+0x30` bit0 in loader, resolver and save loader; `Use_Script` table and its
out-of-range value; all defaults; `FUN_0084A280` identified; 94-row count corroborated by Team B.**`

---

## §16 `items_3d.xtbl` — per-row builder

Dumps read: `ytgi/func6/func_0x00904d10.txt` (decompile in full, listing at the literal sites),
`ytgi/func6/func_0x00905630.txt` (whole body).

**`FUN_00905630(filename, label, …, resetFlag, …)` — CONFIRMED — disassembly:** loads `preload_anim.tbl`
(`FUN_00710320`) into a local table, opens the xtbl, zeroes the item count `DAT_025f5b8c` when the reset flag is set,
and calls `FUN_009052e0(doc, label, …)` — the label (`items_preload_containers` / `items_containers`) is passed
through; this dump neither confirms nor refutes the "profiler label" reading (`FUN_009052e0` not dumped).

**`FUN_00904d10(itemNode, ctx, platformIndex, preloadPass)` — CONFIRMED — disassembly.** Capacity 0x1AE = **430**
entries at `0x025f5b98`, stride 0xB8, count `DAT_025f5b8c`. `streaming_category` (required child; its text at node
`+0x0C`) classifies the item: `Permanent`, `Permanent Mesh Only`, `Permanent Preloaded Head` ⇒ "permanent" class.
A row is kept in the preload pass only if it is permanent-class, and in the normal pass only if it is not **and** its
`Framework` is absent or `main` — the two passes partition the rows (answers how the two labels differ). Fields per
entry (offsets from the entry base):

| Offset | Field |
|---|---|
| `+0x00` | pointer to an allocated copy of `Name` |
| `+0x04` | `Name` hash (`FUN_00d9e8b0`) |
| `+0x08` | `Mesh` filename: the text obtained through `FUN_00dac8c0(item, "Mesh")` with the platform suffix from the pointer table at `0x01152cc8` (entry read: `.csmesh_pc`), allocated copy; 0 if no `Mesh` |
| `+0x0C` | `character_mesh/character_mesh` filename + suffix table `0x01152cf0` (`.ccmesh_pc`); 0 if absent |
| `+0x10` | `character_mesh/<literal 0x0116ed7c>` (the rig, HIGH CONFIDENCE) filename + the rig suffix chosen by platform from `rig_pc`/`rig_ps2`/`rig_ps3`/`rig_xbox`/`rig_xbox2` (index 0 = `rig_pc`) |
| `+0x14` | allocated copy of `character_mesh/Anim_set` text |
| `+0x18` | mesh registration handle (`FUN_006f6f10`) or 0 |
| `+0x1C` | preload link: in the normal pass, when flags bit 17 (0x20000) is set, `FUN_00904180(charMeshName)` is stored here or, if an item of the same `Name` already exists (`FUN_00904c10`), into that item's `+0x1C`; else 0 |
| `+0x20` | byte, 0 |
| `+0x21` | `LargeProp` (bool) |
| `+0x24` | flags: OR of the `Item_Flags/Flag` matches against a **27-literal** table at `0x01308838` (`FUN_00dac740`, not dumped; `inherit_bone_transforms` is one of them per the data), plus 0x20000000 when `Override_Bounding_Box` exists, 0x40000000 when `streaming_category` = `Action Node`, 0x80000000 for permanent-class |
| `+0x2C` | flags 2: bit3 set iff `streaming_category` = `Permanent preloaded head` |
| `+0x30` | `DisplayName` handle (`FUN_0084a1b0`), only when present |
| `+0x58`–`+0x63`, `+0x64`–`+0x6F` | `Override_Bounding_Box` two vec3 children (literals `0x0116ed48`/`0x0116ed44`, unread — HIGH CONFIDENCE min/max) |

`FUN_008fbfd0(entry, ctx, node)` runs first on every entry (not dumped — the `Props`/`Prop`/`Flags` reader the spec
attributes to other passes may live there). `Environmental` and `Action Node` categories feed the mesh-registration
type code (4/5/0x0E/0x0F/0x10) with `FUN_009047c0`/`FUN_00904810` classifying the category text. `Mesh` and
`character_mesh` are read independently (no exclusivity rule in code).

**Spec text change (§16.1):** replace "real per-row content is built by `FUN_00904d10` (… not decompiled this pass …)"
with the table above and the two-pass rule; §16.1 second paragraph: "the two labels select the preload pass
(permanent-class rows) and the normal pass (all other `main` rows) — CONFIRMED — disassembly for the pass split;
the label text itself is only passed through"; §16.1 third paragraph: `Anim_set` → consumed (`+0x14`), `Item_Flags` →
consumed (27-literal table at `0x01308838`, OPEN for the literals), `Glow_Type`/`Scale_Ambient`/`Color_Variants` → not
read by `FUN_00904d10` (HYPOTHESIS: `FUN_008fbfd0`).

**New Review status line (§16):** `**Review status (2026-10-01): CONFIRMED — disassembly for the per-row builder
(FUN_00904d10 read in full): capacity 430, stride 0xB8, 14 field offsets, pass split by `streaming_category`;
OPEN: the 27 `Item_Flags` literals (`0x01308838`), `FUN_008fbfd0`, `FUN_009052e0`, the contained-items element;
391-row count corroborated by Team B.**`

---

## §17 `contacts_sr3.xtbl` — over-capacity

Dump read: `ytgi/func5/func_0x0083b750.txt` (whole body 0x0083b750–0x0083b864).

**CONFIRMED — disassembly.** The row loop's condition is `count < 0x23` (unsigned compare, `JNC` exits) checked
**before** each row: when the count reaches 35 the loop ends and every further `Contact` is skipped — nothing is
written past the array. Record base `0x02313e78`, stride 0x84; `+0x00` `Name` and `+0x40` `Image` are unbounded
byte copies (required text accessor); `+0x80` `Persona` through the thunk to `FUN_00462960`; count `DAT_02315084`
incremented per kept row; the DLC pass filters on `Framework` (default `main`), the base pass keeps every row.

**Spec text change (§17.1):** replace the OPEN note with "(re-derived 2026-10-01: the loader checks `count < 35`
before each row and stops the loop at 35, so a 36th row is skipped; the copies are unbounded — CONFIRMED —
disassembly)".

**New Review status line (§17):** `**Review status (2026-10-01): CONFIRMED — disassembly (FUN_0083b750 read in
full): capacity 35 enforced by a pre-row unsigned compare, no overflow path; 0x84 record; 31-row count corroborated
by Team B.**`

---

## §18 `activity_player_persona_replacement.xtbl` — table base and capacity

Dump read: `ytgi/func3/func_0x00615430.txt` (whole body 0x00615430–0x0061559f).

**CONFIRMED — disassembly.** The per-activity table is at **`0x014b2ac8`**: 8-byte pairs {`+0x00` array pointer,
`+0x04` count}, zeroed from `0x014b2ac8` up to (not including) `0x014b2b64` → **19 activities** (indices 0–18). Per
row: `Name` (required text) → `FUN_00614d70` → activity index or −1 (skipped); count of `Persona_Replacements/
Persona_Replacement` stored at `pair.count`; when > 0 an array of count×8 is allocated with the heap allocator
`FUN_00dad460` (the `0x01495410` heap object, not the table allocator) and each element gets `+0x00` `Original`
(required text → `FUN_0070a2f0`) and `+0x04` `Replacement` (0, then the resolver result when present). The activity
literal list of `FUN_00614d70` is still OPEN (not dumped), as is what `FUN_0070a2f0` returns for an unknown name.

**Spec text change (§18.1):** replace the OPEN note with "(re-derived 2026-10-01: the fixed table is 19 {pointer,
count} pairs at `0x014b2ac8`, zeroed before parsing; element arrays come from the `0x01495410` heap; CONFIRMED —
disassembly; the activity-name list of `FUN_00614d70` and the unknown-persona return of `FUN_0070a2f0` remain OPEN)".

**New Review status line (§18):** `**Review status (2026-10-01): CONFIRMED — disassembly (FUN_00615430 read in full):
19-entry {pointer,count} table at 0x014b2ac8, 8-byte records, `Original` required / `Replacement` optional; OPEN:
`FUN_00614d70` literal list, `FUN_0070a2f0` unknown-name value; 3-row count corroborated by Team B.**`

---

## §19 `airplane_takeoff_land_curves.xtbl` — slope

Dumps read: `ytgi/func7/func_0x00b6a370.txt` (whole body), `ytgi/func8/func_0x00dae5b0.txt` (whole body).

**CONFIRMED — disassembly:** `Landing` → `0x028cd29c`, `Take Off` → `0x028cd2a8`, each {`+0x00` array, `+0x04` count,
`+0x08` slope}; count pre-set 0; array = count×0x10 from the `0x01495410` heap (`FUN_00dad460`); per `Point`: `+0x08`
`Offset_Dist`, `+0x04` `Offset_Height`, `+0x00` = 0, `+0x0C` `Speed` (always-float helper, read in that order); the
point loop also stops at the counted number. Slope: when count ≥ 2, `FUN_00dae5b0(h1 − h0, d1 − d0)` with h = point
`+0x04` and d = point `+0x08`, negated, stored at `+0x08`; otherwise 0. `FUN_00dae5b0(a, b)` returns 0 when both
arguments are 0 and otherwise pushes a then b on the x87 stack and calls the CRT routine at `0x00ea3f4a`. The pair
(both-zero guard, two floats) is the shape of an `atan2` wrapper, but the routine itself was not identified in these
dumps: label the formula "slope = −f(Δheight, Δdistance), f = CRT two-argument routine `0x00ea3f4a`, HYPOTHESIS
atan2(Δheight, Δdistance) in radians". Only the loader's non-reload path (`param == 0`) parses the file.

**Spec text change (§19.1):** replace "(`FUN_00dae5b0`, almost certainly `atan2`; argument order and units not given —
desk review 2026-09-30, OPEN)" with "`FUN_00dae5b0(second.Offset_Height − first.Offset_Height, second.Offset_Dist −
first.Offset_Dist)`, which returns 0 for (0, 0) and otherwise calls the CRT routine at `0x00ea3f4a`; the stored value
is the negation (CONFIRMED — disassembly for the argument order and sign; the routine's identity, HYPOTHESIS atan2,
needs a dump of `0x00ea3f4a`)".

**New Review status line (§19):** `**Review status (2026-10-01): CONFIRMED — disassembly (FUN_00b6a370 and
FUN_00dae5b0 read in full): two curves, 0x10 points, slope argument order and negation; HYPOTHESIS: the CRT routine
`0x00ea3f4a` is atan2; 2-row count corroborated by Team B.**`

---
## Cross-cutting notes for §1, §20–§23

- §1.2 / §20: two hash helpers are in play — `FUN_00d9e8b0` (vi_*, point sets, interaction info, wheel groups,
  components, cust_interface, items_3d, lightset names) and `FUN_00d9e7e0` (color pool, items_inventory, lightset
  `Slate_Name`). Any cross-table lookup must use the same variant as the table it searches (color sets → color pool
  uses `FUN_00d9e8b0` in §9.2's text; re-check `FUN_00a952e0` against the pool's `FUN_00d9e7e0` — new OPEN item).
- §20 index: `Capsule_Shape` indirection → `0x011861d0`; `props` literal table → `0x012ed268`; persona table →
  `0x014b2ac8`; triggers hash object → `0x02622f08` (heads `+0x05`, next `+0x19`, keys `+0x30`, values `+0x80`).
- §21: `Front_Rim`/`Rear_Rim`/spinners → `Component` `+0x00` hash (pointer result); `Slot` `+0x00` → `vehicle_cust_slots`
  row pointer by `Name` hash; `cust_interface` `slots/slots/name` → `vehicle_cust_slots` row pointer by `Name` hash.
- §23 open items: close 1 (color pool map), 2 (lightset mapping), 3 (items_3d builder offsets) and 5 (first weapon
  category literal); keep 4 (`Component` internals, `FUN_00a96a80`); add: `Death_Money`/`Cash_Out_Point` fallback,
  triggers slot −1 write, `Item_Flags` literal table, `FUN_00ac9060`/`FUN_00ac9910` seat usage.
- Clean-room: this file quotes element names and literal strings only; no code was copied.

## Summary table

| Unit | Verdict (2026-10-01) | Key evidence | Remaining OPEN |
|---|---|---|---|
| §2 vi_* | CONFIRMED (83-entry bound, no cap, entries 0/1) | `FUN_00b07920` 0x00b07ae2–0x00b07b37 | token entries 2–82 (data dump) |
| §3 interaction_info | CORRECTED (indirection table at `0x011861d0`; full reset of all 8 sub-records; populated bit per Element) + CONFIRMED flags/resolver | `FUN_00b07330`, `FUN_00ac1ba0` | 6 indirection values; 12 seat strings; `FUN_00ac9060`/`9910` element; 52-row data |
| §4 point_sets | CONFIRMED (7-iteration loop, `stand` = 6; unmatched leaves field unwritten) | `FUN_00b07090` 0x00b07254–0x00b0726d | none |
| §5 wheel_groups | CONFIRMED (Component pointer by `Name` hash; `FUN_0084a280` distinct) | `FUN_00a973e0`, `FUN_00a96040`, `FUN_0084a280` | rim-name data check only |
| §6 anim_modifiers | CONFIRMED (4 inline seat literals; `Seats`/`Seat`/`Name` tree; `WPNCAT_MELEE` entry 0) | `FUN_00ac92d0`, `FUN_00ac2400`, `FUN_00b81900` | 6 category literals (data dump) |
| §7 ext. components | CORRECTED (stride 0x10: `+0x00` cust_slots row ptr, `+0x08` array, `+0x0C` u8 count, `+0x0D` marker) | `FUN_00a971f0` | `Component` record (`FUN_00a96a80`) |
| §8.2 cust_interface | CONFIRMED `Name`-hash key, row pointers; CORRECTED bit1 by presence | `FUN_00818630`, `FUN_00a96130` | `+0x88`–`+0x97` (constructor `FUN_00818180`) |
| §9.1 color_pool | CONFIRMED full 0x24 map; CORRECTED tree (`Grid`/`Shader_Values`/`Value`), `+0x00` string ptr, 1/255 scaling | `FUN_00a94d10` | roles of `FUN_00a93990`/`FUN_00a94c60` (HIGH CONF.) |
| §10 surfing | CONFIRMED 12 names + rules; CORRECTED rounding = truncation; constants 1000/100/0.44704 HIGH CONF. | `FUN_006c3eb0`, `FUN_006a4ad0` | high dwords of the doubles |
| §11 lightset | CONFIRMED three records (0x70/0xB0/0x88) and names; CORRECTED runtime field names | `FUN_005984c0`, `FUN_00598160` | `Bloom` third child literal `0x0111d7e0`; consumer count |
| §12 level_objects | CONFIRMED 27 bit positions, formulas, defaults 0.5/0.05/0.05/0.01; CORRECTED/OPEN `Death_Money` vec3 asks for `Cash_Out_Point` | `FUN_008e7490` + 3 xrefs | `FUN_00dacfb0` fallback |
| §13 props | CORRECTED: literals have spaces; table `0x012ed268`, index = position | `FUN_0060ea40` | entries 2–13 (data dump) |
| §14 triggers | CONFIRMED layout/rule; CORRECTED: unmatched rows write to slot −1 | `FUN_0093e0c0`, `FUN_00802f60` | none |
| §15 items_inventory | CONFIRMED `+0x30` live bit in 3 functions, `Use_Script` table and its −1 value | `FUN_008dc8c0`, `FUN_008dcb10`, `FUN_008dcec0` | none |
| §16 items_3d | CONFIRMED builder offsets, capacity 430, pass split | `FUN_00904d10`, `FUN_00905630` | `Item_Flags` literals, `FUN_008fbfd0`, `FUN_009052e0` |
| §17 contacts | CONFIRMED pre-row `< 35` check, rows beyond skipped | `FUN_0083b750` | none |
| §18 persona_replacement | CONFIRMED 19-pair table at `0x014b2ac8` | `FUN_00615430` | `FUN_00614d70` list, `FUN_0070a2f0` |
| §19 airplane curves | CONFIRMED argument order and sign; routine identity HYPOTHESIS | `FUN_00b6a370`, `FUN_00dae5b0` | `0x00ea3f4a` identity |

Totals: 18 units re-derived; 9 contain at least one CORRECTED statement (§3, §7, §8.2, §9.1, §10, §11, §13, §14, and
§12's `Death_Money`), none remains NEEDS-EXE for its headline question; 12 small OPEN items are listed below.

## Next-dump list

| Address / range | Kind | Why |
|---|---|---|
| `0x0130e638`–`0x0130e784` | data range (83 pointers + strings) | §2 token table order |
| `0x011861d0`–`0x011861e7` | data range (6 dwords) | §3 `Capsule_Shape` indirection values |
| `0x0130df84`–`0x0130dfc3` | data range (2 × 8 pointers + strings) | §3 the 12 unread seat names |
| `0x00ac9060`, `0x00ac9910` | func | §3/§21 which vehicle element uses the seat resolver |
| `0x011899d4`–`0x011899ef` | data range (7 pointers + strings) | §6 weapon-category literals 1–6 |
| `0x00a96a80` | func | §7 `Component` record (0x3C) internals |
| `0x00818180` | func | §8.2 element constructor, bytes `+0x88`–`+0x97` |
| `0x00a93990`, `0x00a94c60`, `0x00a94b70` | func | §9.1 shader-value classifier / name-hash writer / per-row tail |
| `0x00a952e0` | func | §9.2 which hash variant the colour-set lookup uses |
| `0x012a2d94`, `0x012a2ddc`, `0x012a2dc4`, `0x01119dfc` | data (high dwords) | §10/§12 doubles 1000.0, 100.0, 0.5, 0.44704 |
| `0x0111d7e0` | data (string) | §11 third `Bloom` child name |
| `0x004cd740` | func | §11 orientation → 12-float block |
| `0x00dacfb0` | func | §12 `Cash_Out_Point` vs parent `X`/`Y`/`Z` |
| `0x012ed268`–`0x012ed29f` | data range (14 pointers + strings) | §13 literals 2–13 |
| `0x01308838`–`0x013088a3` (27 pointers) and `0x008fbfd0`, `0x009052e0` | data range + func | §16 `Item_Flags` literals, first-pass filler, pass driver |
| `0x00614d70`, `0x0070a2f0` | func | §18 activity list, unknown-persona return |
| `0x00ea3f4a` | func (CRT) | §19 identify the two-argument routine |
