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
