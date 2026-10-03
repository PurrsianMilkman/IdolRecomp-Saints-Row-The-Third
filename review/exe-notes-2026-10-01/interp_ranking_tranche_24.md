# Ranking tranche 24 — 25 previously-unspecced Lua-bound names (Team A, 2026-10-03)

Job definition: `D:\Project Crreish\TEAM A\ghidra\jobs\ranking\tranche-24.json`. The job file's own title states **names
526-550 of the 554 unspecced names, Team B call-count order**; this note uses that range and exactly the 25 names the job
lists. Run locally, read-only, on a private copy of the Ghidra project (`tools\gp_t24`, deleted afterwards), with the
job's own arguments: `CrreishDump.java <out>/lua lua depth:1 maxfuncs:15 maxinsn:500 <25 names>`. **All 25 names
resolved.** Every name string occurs exactly once in `.rdata`. 24 handlers are the code pointer in the slot right after
the name (insn offset +1); **one name, `cell_music_menu_toggle_station`, is registered by a pointer-before-name one-name
registrar** (the `lua` dump landed on `lua_setfield`; the real handler was recovered from a `range` dump of the
registrar, section A). CONFIRMED — dump `index.txt` plus the range dumps of section A.

Follow-up runs on the same private copy, cited by name below:

- "range run": `range` mode over the five small registrars `0x0083ced0`, `0x0083b6e0`, `0x0083ae10`, `0x0083bd90`,
  `0x005f1fc0`;
- "follow-up dump": depth-0 `func` run (`maxinsn:900`) on 29 callees (listed in section L);
- "second follow-up dump": depth-0 `func` run on `0x006f8df0`, `0x006f8f40`, `0x0045b3a0`, `0x0055e220`, `0x00560820`,
  `0x0094b070`, `0x005e4660`, `0x00a40a10`, `0x00a40b70`;
- "third follow-up dump": depth-0 `func` run on `0x00a27c90`, `0x006fb5d0`, `0x0067aed0`, `0x007f0fc0`;
- "ptrs run": `ptrs` mode (16 dwords) on `0x012a2d88`, `0x012ec6f0`, `0x0129a0e0`, `0x01169720`, `0x01169760`,
  `0x012ec710`, `0x0117f890`;
- "xref run": `xref` mode on `0x014f71d5`, `0x014f71d4`, `0x012ec6f0`, `0x022b792d`.

Call counts are Team B's (`D:\Crreish-sync\for-team-b\team-b\tools\lua_reconciliation_called_and_registered_1181.tsv`):
**every name in this tranche is 1 call site in 1 script.** Team B tags the 9 `cell_*` names, the `building_*` name and the
`autil_*` name `ui` and the other 14 `gameplay`; section A shows the registrars agree (11 in six UI-side registrars,
14 in the gameplay registrar `0x00a20840`).

**Already partly covered elsewhere (cited, not re-derived):** `camera_end_look_through` (`spec-lua-api-behaviour.md`
§13.16) is the "end" half of `camera_look_through_do` (E.2) and its record layout lines up exactly with the one found
here. `audio_play_for_navpoint` (tranche 05 D.3) is the string-cue sibling of `audio_play_id_for_navpoint_do` and first
described the shared object-sound core `0x00a2acb0`; section I refines what its returned "playing id" actually is.
`boss_battle_matt_begin` (tranche 05 E.1), `boss_battle_matt_set_player_as_avatar`/`_hide_wings` (§30.2) and
`boss_battle_matt_get_cheat`/`_cheats_start`/`_cheats_stop` (§28.24/§28.25/§24.1) are the siblings of the two boss
bindings in section G. The 255 "not resolved" sentinel seen in section I is the one §2.5 (`audio_play_persona_line`)
and the §-series sentinel catalogue already record.

Labels: **CONFIRMED** = read in the listing of a dump made for this note; **HIGH CONFIDENCE** = follows from a dumped call
or reference, but the callee body was not dumped or a meaning is inferred from strong usage; **HYPOTHESIS** = plausible,
not settled; **OPEN** = not settled (collected in section N).

**Shared conventions, cited not re-described:** every entry opens with the `lua_gettop` prologue (`0x00dfde50`) and
reads arguments with negative stack indices counted from the bottom (`-N` = arg 1, `1-N` = arg 2, …). Primitives as in the
tranche 09/13/16/18/20/22 front matter: `0x00dfe210` `lua_tolstring` (non-string reads as null), `0x00dfe1e0`
`lua_toboolean`, `0x00dfe040` `lua_type` (0 = nil; −1 for the "none" sentinel), `0x00dfe160` `lua_tonumber` (non-number
reads as 0), `0x00ea2596` truncating float-to-int, `0x00dfe590` `lua_pushboolean`, `0x00dfe3a0` `lua_pushnumber`;
`0x00dfe4f0` `lua_pushcclosure` and `0x00dfe830` `lua_setfield` (used by the registrars, pseudo-index −10002 = the
globals table). "Nil-gated optional argument" = the tranche 13 idiom (count checked, then `lua_type` ≠ nil); an
**unconditional** read has no presence check — under the settled missing-argument semantics (tranche 22 M.1, from the
index resolver `0x00dfdc60`) the first missing argument reads the stale slot at the top of the stack and a second missing
argument re-reads an earlier real one; every unconditional read below is flagged "(unconditional)". Resolvers: the
generic character resolvers `0x00a281a0` (full chain, accepts `#PLAYER#`) and `0x00a280c0`, the vehicle resolver
`0x00a281e0`, the per-kind named-object lookup `0x005982e0` (kind row `+0xb` bit `0x02`, the navpoint-like "5th resolver"
of §7.5 / tranche 22 E.1), liveness `0x00853b10` (true = dead/absent), the local-player getter `0x009da4e0`, name hash
`0x00d9e8b0` (case-insensitive CRC, 0 for null). Session getter `0x0087ba20`; the standard record trio (`0x0086f5f0(opcode)`
open, `0x00881110`/`0x00881040` write, `0x0086f1b0(session, 0, 0)` broadcast commit, `0x0086eb20` close) — **with no
session the commit returns without acting**, and single player has no session object at all (§8.27/§26.28); single gate
`0x008addb0(object, 0)` = "this machine has authority over the object". The Lua-callback coroutine mechanism
(`0x00e0ca80` create, `0x00e0cd00` dispatch, `0x00e0ce00` and siblings push arguments) is cited, not re-described; the
three further pushers seen here (`0x00e0ce40` a double, `0x00e0ce20` a float, `0x00e0cfb0` a string) are HIGH CONFIDENCE
members of the same family from their use, not dumped. The 350-slot hook-name pool `0x00a1fcc0` is cited. Constant
stubs whose results are discarded (`0x00d218f0`, and the new `0x00d1e1d0`, section D) are omitted from the bodies.

---

## A. Registrars — where the 25 names live

Seven registrars. CONFIRMED — `index.txt` and the range run:

| registrar | rows | names in this tranche |
|---|---|---|
| cell one-name registrar `0x0083ced0` (**pointer-before-name**) | 1 | `cell_music_menu_toggle_station` |
| cell registrar `0x0083bd90` | 8 | `cell_is_map_disabled`, `cell_is_closing`, `cell_debug_all_enabled`, `cell_machinima_is_recording`, `cell_allow_camera` |
| cell cheats registrar `0x0083b6e0` | 2 | `cell_cheats_unlock_cheat`, `cell_cheats_activate_cheat` |
| cell camera registrar `0x0083ae10` | 2 | `cell_camera_is_enabled` (its sibling row is `cell_camera_enable` → `0x0083ade0`) |
| crib registrar `0x005f1fc0` | 4 | `building_purchase_is_crib_being_purchased` |
| UI helper `0x00845aa0` (`spec-lua-bindings.md` §13.5) | — | `autil_mashing_minigame_faded_out` |
| gameplay `0x00a20840` | — | 14: `camera_script_is_finished`, `camera_look_through_do`, `boss_battle_matt_set_phase`, `boss_battle_matt_qte_begin`, `audio_play_id_for_navpoint_do`, `audio_play_id_for_character_do`, `audio_play_id_do`, `audio_play_for_mover_do`, `audio_play_for_character_weapon_do`, `audio_play_for_character_do`, `audio_play_do`, `assert_screen_is_faded_out`, `airplane_takeoff_do`, `airplane_land_do` |

- **Pointer-before-name case (`0x0083ced0`). CONFIRMED — range run.** The registrar pushes `0x0083ce80` to
  `lua_pushcclosure` *first* (`0x0083cedb`) and only then pushes the name `"cell_music_menu_toggle_station"` for
  `lua_setfield` (`0x0083cee6`). The `lua` dump's "+3" candidate is therefore the `lua_setfield` primitive itself
  (dump `cell_music_menu_toggle_station_0.txt` roots at `0x00dfe830`), and the real handler is **`0x0083ce80`**, read in
  the follow-up dump. This is the sixth tranche to meet the quirk (after 13/16/18/19/21).
- **The four table registrars (`0x0083bd90`, `0x0083b6e0`, `0x0083ae10`, `0x005f1fc0`). CONFIRMED — range run.** Each
  fills a local table and then loops over it reading the closure from `[entry + 4]` and the name from `[entry]`, so the
  table is `{name, function}` pairs and the +1 reading is right. In `0x0083bd90` the table starts with
  `"cell_foreground_transition_out"` at the lowest slot, so `cell_is_map_disabled` → `0x0083bce0`, `cell_is_closing` →
  `0x0083bc80`, `cell_debug_all_enabled` → `0x00a3c670`, `cell_machinima_is_recording` → `0x0083bd10`,
  `cell_allow_camera` → `0x0083bd40`. The second "use" the `lua` dump lists for `cell_cheats_unlock_cheat` and
  `cell_camera_is_enabled` (rooted at `lua_setfield`, files `_1.txt`) is that loop's own name push, not a second
  registration.
- **Gameplay and UI helper pairing.** `0x00a20840` and `0x00845aa0` are name-then-function (tranche 22 section A). It was
  re-checked here against four rows whose handlers are already specced and sit one pair before a name of this tranche:
  `camera_end_look_through` → `0x00a44140` (§13.16) before `camera_look_through_do`; `boss_battle_matt_set_matt_flying`
  → `0x00a408c0` (§18.2) before `boss_battle_matt_set_phase`; `boss_battle_matt_begin` → `0x00a407c0` (tranche 05 E.1)
  before `boss_battle_matt_qte_begin`; `audio_play_for_navpoint` → `0x00a3edf0` (tranche 05 D.3) before
  `audio_play_id_do`. CONFIRMED.
- Several handler addresses (e.g. `0x0083b190`, `0x00842b00`, `0x00a3f030`, `0x00a3efb0`) carry no "[function entry]"
  tag in `index.txt`; the tag only reflects whether a function existed at that moment of the run (the script creates
  missing functions in the read-only session as it dumps, so the same address is tagged in a later entry). Every one
  disassembled cleanly from the registered address.

---

## B. Cell-phone UI queries — 6 functions (cell registrars)

All six read no arguments and return exactly 1 boolean. CONFIRMED — listings.

### B.1 `cell_debug_all_enabled` → `0x00a3c670`

The constant-false stub (tranche 12 B.1): **always false**. So the phone's "debug: everything enabled" switch is off in
this build, unconditionally. CONFIRMED.

### B.2 `cell_machinima_is_recording` → `0x0083bd10`

Pushes `0x00bc55a0()` = **dword `0x029443bc` == 1**. CONFIRMED. `0x029443bc` is a small state machine written by the
`0x00bc4fd0`-`0x00bc6d80` cluster (values 2 and 3 seen written, 1/2/3 compared; 40 uses). HYPOTHESIS: the machinima
recorder state, 1 = recording.

### B.3 `cell_is_map_disabled` → `0x0083bce0`

Pushes `0x006d9c50()`. CONFIRMED — listing, follow-up dump of `0x006d9b90`:
1. `0x006d9b90(list, 6)` answers false when there is no current mission (`0x014c8460` null) or the mission phase
   `0x014c7a14` is 8; otherwise it resolves each of six names through the named-object table at `0x02444db0`
   (`0x004588f0`, count `0x02444dac`; kind row `+0xc` bit `0x04`; alive) and answers **true if the current mission object
   is one of them**. The six names, read from the file image: **`"m01"`, `"m02"`, `"m16"`, `"m21"`, `"m24"`,
   `"dlc2_m02"`** (`0x011169a4`, `0x0111f720`, `0x01125a74`, `0x01125778`, `0x011169a0`, `0x0113a728`).
2. The result is true only if, in addition, byte **`0x014c8358` is 0** (writers `0x006d1e30`, `0x006d86f0`, `0x006d6010`;
   meaning OPEN).

So the phone map is disabled during exactly these six missions (a hard-coded list, not data-driven). Safe.

### B.4 `cell_is_closing` → `0x0083bc80`

On the UI manager `0x012fced8`, `0x007b4b20(type)` (depth-1 dump) finds the first entry of the document list at `+0x30`
(count `+0x2c`) whose `+0x30` field equals `type`, and answers true only if that document is also present in the list at
`+0x4` (count `+0x28`). The binding answers **false only when both type `0x19` and type `0x1a` pass that test; true
otherwise**. CONFIRMED. HYPOTHESIS: `0x19`/`0x1a` are the phone's two screens, so "closing" = "the phone is not fully up".
Note the answer is true when the phone is not open at all.

### B.5 `cell_allow_camera` → `0x0083bd40`

`0x007b3b20(0)` on `0x012fced8` returns the `+0x30` type of entry 0 of the list at `+0x4` (bounded by the count at
`+0x24`; −1 when empty). The binding answers **true when that type is `0x19` or `0x27`**. CONFIRMED. HYPOTHESIS: `0x27` is
the phone-camera screen (the code right after registrar `0x0083ae10` also tests type `0x27`).

### B.6 `cell_camera_is_enabled` → `0x0083adb0`

Pushes **dword `0x02313ddc` == 1**. CONFIRMED. The only writer is the sibling `cell_camera_enable` (`0x0083ade0`,
follow-up dump): arg 1 boolean (unconditional) → `0x02313ddc` = **1 for true, 2 for false**. The dword is zero-filled
at start, so **`cell_camera_is_enabled` is false until a script has called `cell_camera_enable(true)`** — "never set"
and "disabled" read the same. Safe.

---

## C. `cell_music_menu_toggle_station` → `0x0083ce80` (pointer-before-name registrar `0x0083ced0`)

**Arguments:** 1 number = station number (unconditional, truncated, **only the low byte is used**, 1-based). **Return:**
0 values. CONFIRMED — follow-up dump.

**Body (CONFIRMED — follow-up and second follow-up dumps):**
1. The truncated value's low byte is stored over a stack slot whose upper three bytes are stale; both callees read only
   that byte, sign-extended, so the station is taken as a **signed byte** (257 → 1, 255 → −1).
2. `0x0055e220(station)`: station − 1 out of range (negative, or above count `0x013c58dc` − 1) → **true**; otherwise bit 7
   of byte `+0x689` of the station record (records of `0x690` bytes at the pointer `0x013c58cc`).
3. True → `0x00560820(station, 0)`; false → `0x00560820(station, 1)` — a toggle of that bit.
4. `0x00560820(station, v)`: range-checks the same way (does nothing when out of range), writes bit 7 of `+0x689` = `v`,
   and only when `v` is 1: if the local player exists and the record returned by `0x0055ec10` for the player's current
   radio (`0x009b9130(player)`) is this station, calls `0x0055e050(station)` and `0x005602e0` with the player's vehicle
   handle (`+0x16c0`/`+0x16c4`). HYPOTHESIS: bit 7 = "station disabled/hidden", and disabling the station the player is
   listening to retunes the radio; re-enabling has no follow-up.

Out-of-range stations read as "disabled" and the toggle then writes nothing, so a bad number is a silent no-op. Safe.

---

## D. Cheat menu — 2 functions (cell cheats registrar `0x0083b6e0`)

The cheat store, as read in the follow-up dumps (CONFIRMED structure, field meanings HYPOTHESIS unless stated):
- **master table**: records of `0x84` bytes starting `0x014f7228`, count `0x014f71d8`; the name hash is at record `+0x28`;
  byte `+0x6c` is a content id (`0xff` = none), `+0x70`/`+0x74` are the activate/deactivate handlers, `+0x7c` their
  argument; display fields at `+0x2c` and `+0x4c` (strings) and `+0x78` (number).
- **unlocked list**: an array of record pointers at `0x014f6eb0`, count `0x014f71d0`. The space up to the count dword is
  `0x320` bytes = **200 slots**.
- per-record bytes: `+0` "toggle-type", `+1` "currently on", `+2`, `+3` "not allowed in co-op" (HIGH CONFIDENCE from D.2's
  warning text), `+4` "no confirmation needed" (HIGH CONFIDENCE from D.2).
- byte `0x014f71d5` = the cached co-op state: written only by `0x006fb5d0` from `0x00867830` (xref run, third follow-up
  dump). Byte `0x014f71d4`: written 1 by `0x006f98e0` (HYPOTHESIS: "a cheat has been used").
- the accessors `0x006f82a0` (+0), `0x006f8280` (+1), `0x006f82c0` (+3), `0x006f82e0` (+4), `0x006f8310` (+0x2c),
  `0x006f8330` (+0x4c), `0x006f8350` (+0x78) all bound-check the index **unsigned** against `0x014f71d0` and test the slot
  for null. CONFIRMED.

### D.1 `cell_cheats_unlock_cheat` → `0x0083b190`

**Arguments:** 1 string = a tag (unconditional). **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listings, follow-up dumps):**
1. Hash the tag (`0x00d9e8b0`) and look it up with `0x00845170` → `0x008436c0`: 0 when the hash equals the runtime value
   in `0x029c9964`; otherwise a linear search of 12-byte records (keys from `0x02317d74`, count `0x02317b64`, filled by
   `0x008449f0`/`0x00844bc0`) returning the stored pointer — a **wide string** (proved by step 2). Not found → nothing.
2. `0x0083b050(this = UI object 0x02313df8, wide string)`: copy into a 512-byte buffer with `0x00db0560` (takes the
   **low byte of each UTF-16 unit**, at most `0x200`, always terminated at byte `0x1ff`), hash that narrow name again.
3. `0x006f9080(&hash)`: find the hash in the master table and answer "content id ≠ `0xff`". **True → nothing is
   unlocked**, the index stays −1. False → `0x006f90d0(&hash)`: if a record with that hash is already in the unlocked
   list, its index; else, if found in the master table and the content gate `0x0045b3a0(content id)` passes (`0xff` →
   true; otherwise the content table `0x02cda240`, stride `0x108`, entry `+0x101` == id and `+0x103` bits 1 and 2 —
   HYPOTHESIS: DLC installed and enabled), **append the record to the unlocked list** and return the new index; else −1.
4. Lua callback **`"cell_cheats_cheat_unlocked"`** in the UI Lua state (`0x00e1a1b0` → `0x02a45450`), callback `+0x14` =
   `this + 0x24`: with a valid index the arguments are **(index, record `+0x2c` string, record `+0x4c` string, record
   `+0x78` number)**; with −1 the single argument **−1.0**. Dispatched with `0x00e0cd00`.

**Logic notes (CONFIRMED structure):**
- The argument is not a cheat name but a key into the `0x02317d74` string table, whose wide value is narrowed by
  truncation before hashing; a non-ASCII value would hash to the wrong record (latent).
- A cheat whose master record carries a content id (anything but `0xff`) is never unlocked here, even when that
  content is installed (step 3 skips before the gate is consulted). Whether such cheats are unlocked elsewhere is OPEN.
- For a hash that is not in the master table `0x006f9080` uses index −1 and reads the content byte one record before the
  table (`0x014f7210`); the outcome is −1 either way, so this stray static read is harmless.
- The append in `0x006f90d0` has **no capacity check** against the 200-slot array; it is bounded only by the master count
  (each record is appended once). Overflow requires more than 200 master records — OPEN (the count is runtime data).

### D.2 `cell_cheats_activate_cheat` → `0x0083b600`

**Arguments:** 1 number = index into the unlocked list (unconditional, truncated). **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing, follow-up and second follow-up dumps):**
1. The index is stored in the global **`0x02313df0`** before anything else.
2. If record byte `+3` is set and (`0x00867830` — the `coop_is_active` predicate of §3.1 — or `0x008b6c60`, true when
   `0x0088b610()` is null or `0x00877d60(4)` on its result is true; meaning OPEN) → a one-button dialog
   `"MENU_TITLE_WARNING"` / `"CHEAT_DISABLED_IN_COOP"` (`0x007c3d80(title, body, 0, 2, 0)`), and stop.
3. Else, if byte `0x014f71d4` is 0 and record byte `+4` is 0 → a confirmation dialog
   `"CELL_CHEAT_ACTIVIATE_DIALOG_HEADER"` / `"CELL_CHEAT_ACTIVIATE_DIALOG_BODY"` (tags spelled so in the image) with result
   callback `0x0083af50` (`0x007c3de0(title, body, 0x0083af50, 2, 0, 0)`); the dialog's `+0x118` is set to 1 **after a
   null test** (unlike tranche 22 C.2).
4. Else `0x0083af50(0, 0)` at once.

The apply routine `0x0083af50(_, answer)`: a non-zero answer does nothing. Otherwise, with the index **re-read from
`0x02313df0`**:
- toggle-type record (`0x006f82a0`) → `0x006f91e0(index)`: if the record is on, `0x00d48d10()` and the deactivate method
  `0x006f8f40(record, 1, 0)`; else, unless (co-op `0x014f71d5` and record `+3`), the activate method
  `0x006f8df0(record, 1, 0)`; the reported state is record `+1` afterwards;
- otherwise → `0x006f9180(index)`: unless (co-op and record `+3`), `0x006f8df0(record, 1, 0)`; reported state true;
- Lua callback **`"cell_cheats_cheat_activated"`** (callback `+0x14` = `0x02313e1c`) with **(index, state)**.

`0x006f8df0` (activate, second follow-up dump): nothing without a handler at `+0x70`; when record `+4` is 0 calls
`0x006f9b60(flag)` and — unless record `+2` or the "no broadcast" argument — broadcasts an opcode-`0x45` record (sub-code
`0x16`, enable byte 1, the record hash) to the session; then, if the content gate passes, calls the handler
`(+0x7c, flag)`, sets `+1` for toggle-type records and calls `0x00bda8d0(record + 5)` (HYPOTHESIS: a HUD notification).
`0x006f8f40` mirrors it with `+0x74`, enable byte 0, `+1` cleared and `0x00bda930`.

**Crash — null cheat record. CONFIRMED structure (`0x006f9180` → `0x006f91a0` / `0x006f8e2b`).** `0x006f9180` takes the
record as **null when the index is not below the unlocked count** (the compare is unsigned, so negatives too) and then
either reads `[null + 3]` (co-op byte set) or calls `0x006f8df0` with a null record, whose first instruction reads
`[null + 0x70]`. Nothing earlier rejects such an index: the co-op test (`0x006f82c0`) and the "no confirmation" test
(`0x006f82e0`) both answer false for it, so the binding opens the confirmation dialog, and confirming it crashes — or it
crashes at once when `0x014f71d4` is set. Trigger: `cell_cheats_activate_cheat(n)` with any `n` ≥ the number of unlocked
cheats, a negative `n`, or no argument (stale stack value). Fix: reject the index in the binding with `0x006f82a0`-style
bounds before storing it. (`0x006f91e0` has the same null shape but is only reached after `0x006f82a0` accepted the
index.)

**Logic defect — shared pending index. CONFIRMED structure.** The index lives in one global and the dialog callback
re-reads it, so a second `cell_cheats_activate_cheat` before the player answers the first dialog makes **the first
dialog apply the second cheat** (and two dialogs are open). The opcode-`0x45` broadcast and its receiver are not traced
(OPEN); in single player the commit does nothing (§8.27/§26.28).

---

## E. Scripted camera — 2 functions (gameplay registrar)

### E.1 `camera_script_is_finished` → `0x00a40e20`

**Arguments:** none read. **Return:** exactly 1 boolean. CONFIRMED.

**Body (CONFIRMED — listing, depth-1 dump of `0x00578cc0`, follow-up dump of `0x00dad830`):** `0x00578cc0()`:
- camera mode `0x013c87c8` ≠ 6 → **true**;
- otherwise true when the duration `0x013c8acc` is within 0.01 of 0 (`0x00dad830`: strict `|a − b| < eps`) **and** the
  elapsed time `0x013c8f74` > 0; otherwise true when elapsed ÷ duration ≥ (point count `0x013c8ac8` − 1).

Mode 6, the duration, the elapsed time and a point count of 2 are exactly what `camera_look_through_do` sets up (E.2),
so this answers "has the look-through move finished" (HIGH CONFIDENCE). Notes (CONFIRMED arithmetic): with no scripted
camera running it answers true; with a duration of 0 it answers **false on the frame of the call** (0 ÷ 0 is NaN, which
compares false) and true once any time has elapsed. Safe.

### E.2 `camera_look_through_do` → `0x00a46860`

**Arguments:** 1 string = navpoint-like object (unconditional, `0x005982e0`, must be alive); 2 nil-gated number =
duration, default **0.0**; 3 nil-gated boolean = hide the HUD, default **true**. **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing, depth-1 dumps of `0x007efa90`/`0x007f1070`/`0x00579ef0`, third follow-up dump):**
1. **Before the object is resolved:** if the HUD is not already hidden (`0x007efa90(1)` = byte `0x022b792d`) and arg 3 is
   true → `0x007f1070(1)`. That routine (skipped entirely when byte `0x022b792c` is set or bit `0x20000` of `0x014f71e8`)
   counts references in `0x022b7930` (+1, or −1 clamped at 0 for argument 0), sets `0x022b792d` = (count > 0), and on a
   change switches the `"Csafe_frame"` element of the `"tutorial"`, `"hud_diversion"`, `"hud_qte"`, `"hud_whored"`,
   `"hud_btnmash"`, … UI documents. HIGH CONFIDENCE: a reference-counted "cinematic, HUD hidden" mode.
2. Object not resolved or dead → stop.
3. Copy the object's orientation (three rows from `+0x4c`, `0x004add90`) and position (`+0x40`, `0x00da38e0`), then
   `0x00579ef0(current camera position 0x013c8740, current orientation 0x013c8760, target position, target orientation,
   duration)`: builds a two-point camera path (end points pushed outward by the factor at `0x0127e354`) into three
   spline objects at `0x013c8f50`/`0x013c8f5c`/`0x013c8f68`, stores duration `0x013c8acc`, point count `0x013c8ac8` = 2,
   elapsed `0x013c8f74` = 0, and sets **camera mode 6** (`0x00564880(6)`).
4. Broadcast an opcode-`0x43` record: byte **`0x0e`**, byte **0** ("start"), the 32-bit duration, the hide-HUD boolean,
   the object's handle (`0x0086f500`); session commit, close.

**The receiver (CONFIRMED — third follow-up dump).** `0x00a27c90` is referenced only from the data word `0x01169758` =
`0x01169720 + 0x0e × 4`, and reads a sub-code: **0** → duration, boolean, handle, then the same HUD step and
`0x00579ef0` move; **1** → boolean, then `0x0056cc30()`, `0x00564910(6)`, `0x007f1070(0)` and, when the boolean is
true, `0x00567280(0)` — exactly the local sequence §13.16 records for `camera_end_look_through`, which sends the same
`0x0e` / 1 record. This is a third data point for tranche 22 M.4: the opcode-`0x43` sub-handler table is indexed by
sub-code × 4 from `0x01169720` (CONFIRMED for `0x0e`; the ptrs run lists slots 0-31).

**Logic defects (CONFIRMED structure):**
- **Unknown object still hides the HUD.** Step 1 runs before step 2, so a misspelt or dead navpoint takes the HUD-hidden
  reference without moving the camera; the HUD stays hidden until `camera_end_look_through` releases it.
- The hide is skipped when the HUD is already hidden, so the reference is not stacked; but `camera_end_look_through`
  always releases one (§13.16), which can release a reference some other system took (consequence OPEN).
- In single player the broadcast does nothing (§8.27/§26.28).

---

## F. `building_purchase_is_crib_being_purchased` → `0x005eed90` (crib registrar `0x005f1fc0`)

**Arguments:** none read. **Return:** exactly 1 boolean. CONFIRMED — listing.

**Body:** **true when the 64-bit value `0x014a0f90`/`0x014a0f94` is non-zero**. CONFIRMED. Writers: `0x005f0770`,
`0x005f0a60` and code at `0x00a00663`; readers include the sibling `crib_purchase_purchase_crib` (`0x005f1e50`).
HIGH CONFIDENCE: the handle pair of the crib whose purchase is in progress. Safe.

---

## G. Matt boss battle — 2 functions (gameplay registrar)

The state block (CONFIRMED — ptrs run): a name table at **`0x012ec6f0`**, phase count **`0x012ec71c` = 4**, the phase
**`0x012ec720`** (file value 0), and further state from `0x012ec724` on (`0x012ec714` = 5000 and `0x012ec718` = 3000;
tranche 05 E.1 reads a 5000 ms deadline from this block). The table and the words after it, read from the file image:

| index | address | value |
|---|---|---|
| 0 | `0x012ec6f0` | `""` (`0x0129a0e3`, an empty string) |
| 1 | `0x012ec6f4` | `"Mission16_WingRipQTE"` |
| 2, 3, 4 | `0x012ec6f8`-`0x012ec700` | `""` |
| 5 | `0x012ec704` | `"Mission16_FinalQTE"` |
| 6 | `0x012ec708` | `"Mission16_ShootQTE"` |
| 7 | `0x012ec70c` | `"Mission16_StunQTE"` |
| 8 | `0x012ec710` | `"CyberEnergyBall"` |
| 9, 10, 11 | `0x012ec714`-`0x012ec71c` | 5000, 3000, 4 (numbers, not pointers) |
| 12 | `0x012ec720` | the phase itself |

The only instruction references to `0x012ec6f0` are the three in `0x005e7740` (xref run), all indexed by the phase.

### G.1 `boss_battle_matt_set_phase` → `0x00a40b40`

**Arguments:** 1 number = phase (unconditional, truncated). **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing, depth-1 dump of `0x005e6590`, follow-up dump of `0x005e5e40`):** the phase is **stored
first**; then, if it is not below the count 4 (unsigned compare, so negatives too), `0x005e5e40()` runs: it ends a pending
timer (`0x005e5bb0(1)` when `0x012ec730` ≠ −1), resets `0x012ec724`-`0x012ec754` to −1/0 and calls `0x007f1e30(0, −1)`.
**It does not reset the phase**, so an out-of-range phase stays stored. Phases 0-3 are just stored.

### G.2 `boss_battle_matt_qte_begin` → `0x00a40d70`

**Arguments:** 1 string = player character (unconditional, `0x00a280c0`); 2 nil-gated string = navpoint-like target
(`0x005982e0`, alive; absent/unresolved → null). **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing, depth-1 dumps, follow-up and second follow-up dumps):** Matt is `0x005e5ae0()` =
`0x00a281a0("Matt")` (as §30.2). Then `0x005e7af0(player, matt, target)`:
1. `0x005e6ce0(matt)`: for a character (class row `+0xa` bit `0x02`), true when the state field `+0xcc8` is `0x10`, or
   `0xc` with bit `0x04` of `+0x15c5`; for any other object, `[+0xf0] + 4` == `0x0149faa4`. False → nothing happens.
2. `0x009a7c90(matt, 0, 1, 0)` (opens an opcode-`0x41` record, sub-code 4, with Matt's id; meaning OPEN), then the reset
   `0x005e5e40()` (G.1).
3. `0x005e7740(player, matt, target)`: `name = 0x012ec6f0[phase]` — **no bound check** — compared with `""` by
   `_stricmp`; equal → stop. No target → stop. Otherwise `0x0094b070(this = player, …)` (null-safe), `0x009e4940(pc)` where
   `pc` = the player if its class row has `+0xa` bit `0x02`, else null; then, if the name pointer is non-null,
   `0x005e4660(this = pc + 0x1f00, "m16_qte_complete", …)` — registers that hook name through the 350-slot pool
   `0x00a1fcc0` into the table at `this` — and `0x0060e740(pc, name, callback 0x005e76a0, …, target handle)`. HIGH
   CONFIDENCE: starts the named QTE, reporting back through the `"m16_qte_complete"` hook.

So with the shipped phase table only **phase 1 (`"Mission16_WingRipQTE"`)** starts a QTE through this binding; in phases
0, 2 and 3 it only runs steps 1-2.

**Crash 1 — Matt absent. CONFIRMED (`0x005e6ce6` → `0x005e6d1b`).** In `0x005e6ce0` the null test on Matt jumps to the
"not a character" branch, which reads **`[null + 0xf0]`**. `0x00a281a0` returns null when no live object named "Matt"
exists (it rejects dead objects), and nothing between the resolver and this read tests it. Trigger: the binding called
before Matt spawns or after he dies.

**Crash 2 — out-of-range phase. CONFIRMED structure (`0x005e7767`).** After `boss_battle_matt_set_phase(n)` with
`n` ≥ 4, the table read leaves the four-entry table: phases 5-8 silently start the **wrong** QTE (`"Mission16_FinalQTE"`,
`"Mission16_ShootQTE"`, `"Mission16_StunQTE"`, `"CyberEnergyBall"`), phase 9 hands `_stricmp` the "pointer" `0x1388`,
10 → `0xbb8`, 11 → `4`, 12 → `0xc` (the phase itself), all of which fault; a negative or large phase reads an arbitrary
dword. Fix: bound the index by `0x012ec71c` as `0x005e59f0` already does.

**Crash 3 — target given, player not a character. CONFIRMED structure (`0x005e77c4`).** When arg 1 does not resolve, or
resolves to an object without the `+0xa` bit, `pc` is null, but for a phase with a name (1, or 5-8) and a resolved target
the code still calls `0x005e4660` with `this` = **`0x1f00`**, which writes `[0x1f00 + 4·slot]`. (`0x009e4940(null)` runs
first; whether it already faults is OPEN.)

---

## H. `autil_mashing_minigame_faded_out` → `0x00842b00` (UI helper registrar)

**Arguments:** none read. **Return:** 0 values. CONFIRMED.

**Body:** `0x0067aee0()` clears byte **`0x014b4b59`**. CONFIRMED. Its only reader is the getter `0x0067aed0`, called from
`0x00702a50`; the other writers are in the `0x0067aef0`-`0x0067bff0` cluster (one of them, `0x0067b3a0`, can set it).
HYPOTHESIS: the button-mashing minigame waits for the UI script to report the fade-out, and this binding releases that
wait. Safe.

---

## I. Audio — 7 functions (gameplay registrar)

Two play cores are involved (CONFIRMED — depth-1 and follow-up dumps):

- **Event core `0x00a26640(cue, flagA, mode, flagB)`**: cue 0 → returns **255** without playing. Otherwise
  `0x0045d990(cue, 0, &out)` posts the event on the default object `0x02cea938` (returns −1 **without writing `out`**
  when audio is not initialised, byte `0x031728c2`); a non-zero `out` is recorded by `0x00a37ca0` in a 16-slot list
  (`0x02703ce0`, stride `0x58`, count `0x02703cb4`; ignored when full). When `mode` is `0xff` an opcode-`0x45` record is
  broadcast: sub-code `0xb` (`0x00898cb0`), cue (4 bytes), `out` (4 bytes), `flagA`, `flagB`. Returns `out`. The two
  flags are used **only** by that record.
- **Object-sound core** (tranche 05 D.3's `0x00a2acb0`, and `0x00a2ad00` for characters): a request block holds the
  object's handle pair at `+0`/`+4`, the cue at **`+8`** (generic) or **`+0x1c`** (character), and a byte 1 at `+0x28`.
  `0x00a2a850(request, 1)` takes one of **32 emitter slots** (`0x026ea498`, stride `0x18`): a free one, else the first
  whose sound has finished (`0x00a2a560`), else none (returns null). On success it increments the counter
  **`0x026ea798`** (skipping 0) and stores it in the slot and in **request `+0x24`**. Then a `+0x1c` cue goes to
  `0x00a2a780` (re-resolves the handle; needs a live object with class row `+0xa` bit `0x01`; failure sets `+0x24` to 0;
  else `0x0070dcf0(object, cue, 0)`), a `+8` cue to `0x00a2a950` (picks the emitter by the object's kind: e.g. the object
  at `+0x159c` for kind row `+0x6` bit `0x80`, the one at `+0x88` for `+0xc` bit `0x02`). The binding gets request
  `+0x24`. **So the number these bindings return is an engine serial from `0x026ea798`, not a Wwise playing id**
  (refining tranche 05 D.3's wording). CONFIRMED.

All seven push numbers as unsigned 32-bit values (2^32 added to a negative read).

### I.1 `audio_play_do` → `0x00a3f030`

**Arguments:** 1 string = cue (unconditional, `0x00462960`); 2 nil-gated string, **read and discarded**; 3 nil-gated
boolean, default false. **Return:** exactly 1 number. **Body:** `0x00a26640(cue, 0, 0xff, arg 3)`. CONFIRMED.

### I.2 `audio_play_id_do` → `0x00a3f470`

**Arguments:** 1 number = cue id (unconditional, truncated, **reduced to its low 16 bits**); 2 boolean (unconditional).
**Return:** exactly 1 number. **Body:** `0x00a26640(id16, arg 2, 0xff, 0)` — note the boolean goes to the *other* flag
slot than in I.1, and is passed as a full dword whose upper bytes are stale stack content. CONFIRMED.

### I.3 `audio_play_for_character_do` → `0x00a3f1e0`

**Arguments:** 1 string = cue (unconditional); 2 string = character (unconditional, `0x00a281a0`); 3-8 nil-gated
(string, boolean, number, number, boolean, boolean) — **all six read and discarded**. CONFIRMED.

**Return (CONFIRMED):** character not resolved → **2 numbers, 255 and 0** (the double at `0x012a2d88` is 255.0); resolved
→ 1 number: 0 when the cue does not resolve, else `0x00a2ad00(character, cue)` (cue at `+0x1c`).

### I.4 `audio_play_id_for_character_do` → `0x00a3f4f0`

**Arguments:** 1 string = character (unconditional); 2 number = cue id (unconditional, **low 16 bits**); 3 nil-gated
boolean, 4-5 nil-gated numbers — **all three discarded**. CONFIRMED.

**Return (CONFIRMED):** not resolved, or id 0 → 1 number, 0; otherwise `0x00a2ad00(character, id)` and the result is
pushed **twice** (2 identical numbers).

### I.5 `audio_play_for_character_weapon_do` → `0x00a3f370`

**Arguments:** 1 string = cue (unconditional); 2 string = character (unconditional, `0x00a281a0`); 3 nil-gated string,
discarded. **Return:** exactly 1 number (0 when the cue does not resolve). **Body:** request with the cue at `+8` →
`0x00a2acb0` (object path; for a character the emitter is HYPOTHESIS its equipped weapon at `+0x159c`). CONFIRMED.

**Crash — unresolved character. CONFIRMED (`0x00a3f41f`).** The character pointer from `0x00a281a0` is read at `+8` and
`+0xc` **without a null test** (the sibling I.3 tests it). Trigger: any valid cue with a misspelt, unspawned or dead
character → read of address 8.

### I.6 `audio_play_for_mover_do` → `0x00a405c0`

**Arguments:** 1 string = cue (unconditional); 2 string = mover (unconditional; `0x006434a0` — kind row `+0xb` bit `0x01`
on `0x02442750` — alive, and its virtual `+0x68` must answer true); 3 nil-gated string, discarded. **Return:** exactly 1
number; 0 when the cue or the mover fails. **Body:** request (cue at `+8`) → `0x00a2acb0`. CONFIRMED. Safe.

### I.7 `audio_play_id_for_navpoint_do` → `0x00a3ed10`

**Arguments:** 1 string = navpoint-like object (unconditional, `0x005982e0`, alive); 2 number = cue id (unconditional,
**low 16 bits**). **Return:** exactly 1 number; 0 on any failure. **Body:** request (cue at `+8`) → `0x00a2acb0` —
tranche 05 D.3 with a numeric cue. CONFIRMED. Safe.

**Logic defects in this group (CONFIRMED structure unless marked):**
- **Uninitialised playing id (I.1, I.2).** `0x00a26640` reads `out` from a stack slot it never initialises; when audio is
  not initialised `0x0045d990` returns without writing it, so the binding returns **whatever was on the stack**, records
  it in the 16-slot list when non-zero, and broadcasts it.
- **16-bit cue ids (I.2, I.4, I.7).** The ids are cut to 16 bits, while the name path (`0x00462960`,
  `AK::SoundEngine::GetIDFromString` per §2) yields 32-bit ids; a script that passes such an id plays the wrong event or
  none. HYPOTHESIS that scripts pass hashed ids here (the id space of these three is OPEN).
- **Two id spaces.** I.1/I.2 return the event core's `out`; I.3-I.7 return `0x026ea798` serials. Which stop function
  accepts which is OPEN.
- **Inconsistent "failed" answers:** 255 (I.1/I.2 for cue 0; I.3 for an unresolved character, plus a second value 0),
  0 elsewhere; I.4 returns its result twice. A reimplementation must keep these shapes.

---

## J. `assert_screen_is_faded_out` → `0x007c9f50` (gameplay registrar)

**Arguments:** none read. **Return:** 0 values. **Body:** the shared no-op stub (spec §6.1). CONFIRMED. The shipped
assertion never fires, whatever the fade state.

---

## K. Airplanes — 2 functions (gameplay registrar)

Common checks (CONFIRMED — depth-1 dumps): `0x00ab5070(vehicle, 0)` returns the occupant of seat 0 (seat count `+0x824`
when byte `+0xbd0` is 1, else 8; handle at `+0x1358`/`+0x135c` of the seat) — HIGH CONFIDENCE "has a driver";
`0x00ad3160(vehicle)` = (`[+0xbf4] + 0x2c` == 2) — HYPOTHESIS "the vehicle uses the airplane controller"; the request
token comes from `0x006f62b0(vehicle, 2, 0)` (tranche 22 E.3's token allocator; 0 when none).

### K.1 `airplane_takeoff_do` → `0x00a3efb0`

**Arguments:** 1 string = vehicle (unconditional, `0x00a281e0`). **Return:** exactly 1 boolean. CONFIRMED.

**Body (CONFIRMED — listing, depth-1 dump of `0x00b6b450`):** false unless the vehicle resolves, has a seat-0 occupant and
passes `0x00ad3160`; then pushes **`0x00b6b450(vehicle, token)`**: false when `0x00ad3160` fails; without authority
(`0x008addb0`) it forwards an opcode-`0x42` record (sub-code `0x15`, 1, the vehicle, the 16-bit token) to the owner
(`0x008ae020`, `0x0086f110`) and answers **true**; with authority it computes a take-off path from the vehicle's position
and forward vector and, when the check `0x00b6b190` reports a problem, releases the token (`0x006f6680`) and answers
**false**; otherwise it switches the vehicle into flight mode 1 (`0x00b66b30`, `0x00b66e90(vehicle, 1, 0)`,
`0x00b66d80`), stores the target at `+0x2e8`-`+0x2f0`, calls `0x00b6ac10(1)`, records the token at `+0x30c` with bit `0x04`
of `+0xf8`, and answers **true**.

### K.2 `airplane_land_do` → `0x00a404d0`

**Arguments:** 1 string = vehicle (unconditional); 2 string = navpoint-like object (unconditional, `0x005982e0`, alive).
**Return:** exactly 1 boolean. CONFIRMED.

**Body (CONFIRMED — listing, depth-1 dump of `0x00b6aec0`):** false unless both resolve and the seat-0 and controller
checks pass; then `0x00b6aec0(vehicle, position +0x40..+0x48, forward +0x64..+0x6c, token)` — without authority an
opcode-`0x42` record (sub-code `0x15`, 2, the vehicle id, the token, position, forward) to the owner; with authority
(and the controller check again) flight mode 2, target position at `+0x2d8`, forward at `+0x2e8`-`+0x2f0`,
`0x00b6ac10(0)`, token at `+0x30c`. The binding then pushes **true unconditionally** — the routine returns nothing.

**Return-shape note (CONFIRMED):** take-off reports whether the manoeuvre started; landing reports only that the
arguments resolved. Neither has a crash shape (all lookups are tested).

---

## L. Method notes for the follow-up runs

- Range run: `0x0083ced0-0x0083cf40`, `0x0083b6e0-0x0083b780`, `0x0083ae10-0x0083aea0`, `0x0083bd90-0x0083be60`,
  `0x005f1fc0-0x005f2070` (section A).
- Follow-up dump (depth 0, `maxinsn:900`): `0x0083ce80 0x008436c0 0x00db0560 0x006f9080 0x006f90d0 0x00d1e1d0 0x00e1a1b0
  0x006f8310 0x006f8330 0x006f8350 0x006f82a0 0x006f91e0 0x006f8280 0x006f9180 0x005e5e40 0x005e59f0 0x005e5a70
  0x005e7740 0x005e6ce0 0x009a7c90 0x005e7b30 0x00a2a850 0x00a2a780 0x00a2a950 0x0045d990 0x00a37ca0 0x00dad830
  0x0083ade0 0x006d9b90`. `0x009a7c90` was read only to its first record write; `0x00a2a950` only to the emitter choice.
- Second follow-up dump: `0x006f8df0 0x006f8f40 0x0045b3a0 0x0055e220 0x00560820 0x0094b070 0x005e4660 0x00a40a10
  0x00a40b70` (`0x0094b070` only to its null test; `0x00a40a10`/`0x00a40b70` are neighbouring boss rows, read only to
  confirm they are not in this tranche).
- Third follow-up dump: `0x00a27c90` (camera receiver), `0x006fb5d0` (writer of `0x014f71d5`, read only around that
  write), `0x0067aed0`, `0x007f0fc0` (not used).
- Ptrs run and xref run: as listed in the header. `0x0117f890`/`0x0117f894` (the default handle in I.5's request) are 0.
- Primary dumps at depth 1 supplied every body described as "depth-1 dump", including `0x00bc55a0`, `0x006d9c50`,
  `0x007b4b20`, `0x007b3b20`, `0x00578cc0`, `0x007efa90`, `0x007f1070`, `0x00579ef0`, `0x005e6590`, `0x005e5ae0`,
  `0x005e7af0`, `0x00a26640`, `0x00a2acb0`, `0x00a2ad00`, `0x006434a0`, `0x00ab5070`, `0x00ad3160`, `0x006f62b0`,
  `0x00b6b450`, `0x00b6aec0`, `0x006f82c0`, `0x006f82e0`, `0x008b6c60`, `0x0083af50`, `0x0083b050`, `0x0067aee0`.

---

## M. Cross-function observations

1. **Unconditional reads (missing-argument semantics, tranche 22 M.1).** 15 of the 25 handlers read at least one argument
   without a presence check: `cell_music_menu_toggle_station` (1), `cell_cheats_unlock_cheat` (1),
   `cell_cheats_activate_cheat` (1), `camera_look_through_do` (1), `boss_battle_matt_set_phase` (1),
   `boss_battle_matt_qte_begin` (1), `audio_play_do` (1), `audio_play_id_do` (1, 2), `audio_play_for_character_do`
   (1, 2), `audio_play_id_for_character_do` (1, 2), `audio_play_for_character_weapon_do` (1, 2),
   `audio_play_for_mover_do` (1, 2), `audio_play_id_for_navpoint_do` (1, 2), `airplane_takeoff_do` (1),
   `airplane_land_do` (1, 2). At least three of them can turn a missing argument into a crash path: a stale number
   for `cell_cheats_activate_cheat` (D.2) or `boss_battle_matt_set_phase` (G.2 crash 2), and a stale or absent
   character name for `audio_play_for_character_weapon_do` (I.5).
2. **Sibling shows the fix, again.** `audio_play_for_character_do` tests the character, `_weapon_do` does not (I.5);
   `0x005e59f0` bounds the boss phase, `0x005e7740` does not (G.2); the activate dialog's pointer is tested here but the
   index it applies is not (D.2). The `0x005e6ce0` crash is a null test that jumps to the wrong branch.
3. **Global "pending" state behind an asynchronous dialog** (D.2) — the second instance after tranche 22 C.2's repeated
   `game_cancel_mission` dialogs; both let repeated calls stack dialogs.
4. **The opcode-`0x43` sub-handler table** at `0x01169720` (sub-code × 4) is CONFIRMED for a third sub-code (`0x0e`,
   camera look-through start/end; E.2), after `0x2a` and `0x2f` in tranche 22.
5. **Two audio id spaces and 16-bit ids** (section I): the event core returns a post result (possibly uninitialised), the
   object core returns a `0x026ea798` serial; three `_id_` bindings truncate ids to 16 bits.
6. **Discarded arguments:** `audio_play_do` (arg 2), `audio_play_for_character_do` (args 3-8),
   `audio_play_id_for_character_do` (3-5), `_weapon_do` and `_mover_do` (3) — the same "read but inert" pattern as
   tranche 05 D.3 / §2.5.
7. **Constant and no-op bindings:** `cell_debug_all_enabled` (constant false `0x00a3c670`) and
   `assert_screen_is_faded_out` (no-op `0x007c9f50`). `0x00d1e1d0` is one more constant-1 stub whose result is discarded
   (called by `0x0083b050`).
8. **Hard-coded content:** the six map-disabled missions (B.3), the Mission 16 QTE names (G), the HUD documents toggled
   by the cinematic mode (E.2).
9. **No session claims in this tranche.** `camera_look_through_do`, the cheat activate method, the event core and the
   airplane routines open replicated records or test authority; the commits take their no-session path in single player
   per §8.27/§26.28, and nothing here is read as new evidence about sessions.

---

## N. OPEN

- A: none (all 25 handlers confirmed).
- B.2: the meaning of the `0x029443bc` states. B.3: what byte `0x014c8358` means. B.4/B.5: the UI document types `0x19`,
  `0x1a`, `0x27`, and why `0x007b4b20` and `0x007b3b20` bound the list at `+0x4` by different counts (`+0x28`/`+0x24`).
- C: the meaning of bit 7 of the station record `+0x689`; `0x0055e050`, `0x005602e0`, `0x0055ec10`.
- D.1: what fills the `0x02317d74` string table and the value in `0x029c9964`; whether content-gated cheats are unlocked
  elsewhere; the master-table count versus the 200-slot unlocked array.
- D.2: `0x008b6c60`/`0x0088b610`/`0x00877d60(4)`; the opcode-`0x45` sub-code `0x16` receiver; `0x006f9b60`, `0x00d48d10`,
  `0x00bda8d0`/`0x00bda930`; the dialog's answer encoding.
- E.2: the factor at `0x0127e354`; who else takes or releases the `0x007f1070` reference.
- G: `0x009a7c90`'s record, `0x009e4940(null)`, `0x0060e740`; how phases other than 1 start their QTEs; what lies before
  `0x012ec6f0` (negative phases).
- H: the minigame code around `0x00702a50`.
- I: the id space of the three `_id_` bindings; which stop functions accept which ids; `0x0045ea70` (does it always write
  the result); the opcode-`0x45` sub-code `0xb` receiver and the two flags' meaning.
- K: `0x00b6b190` (what makes a take-off fail); the meaning of `0x00ad3160`'s value 2.

---

## O. Direct-answer table

| # | name | registrar | handler | status |
|---|---|---|---|---|
| 1 | `cell_music_menu_toggle_station` | cell one-name `0x0083ced0` (pointer-before-name) | `0x0083ce80` | resolved — toggle bit 7 of station record `+0x689` (1-based, low byte only) (C) |
| 2 | `cell_machinima_is_recording` | cell `0x0083bd90` | `0x0083bd10` | resolved — `0x029443bc` == 1 (B.2) |
| 3 | `cell_is_map_disabled` | cell `0x0083bd90` | `0x0083bce0` | resolved — current mission is m01/m02/m16/m21/m24/dlc2_m02 and `0x014c8358` clear (B.3) |
| 4 | `cell_is_closing` | cell `0x0083bd90` | `0x0083bc80` | resolved — false only while UI types `0x19` and `0x1a` are both up (B.4) |
| 5 | `cell_debug_all_enabled` | cell `0x0083bd90` | `0x00a3c670` | resolved — constant false (B.1) |
| 6 | `cell_cheats_unlock_cheat` | cell cheats `0x0083b6e0` | `0x0083b190` | resolved — tag → string → cheat hash → unlock + `"cell_cheats_cheat_unlocked"` callback (D.1) |
| 7 | `cell_cheats_activate_cheat` | cell cheats `0x0083b6e0` | `0x0083b600` | resolved — co-op warning / confirm dialog / apply; **null-record crash for a bad index** (D.2) |
| 8 | `cell_camera_is_enabled` | cell camera `0x0083ae10` | `0x0083adb0` | resolved — `0x02313ddc` == 1; false until `cell_camera_enable(true)` (B.6) |
| 9 | `cell_allow_camera` | cell `0x0083bd90` | `0x0083bd40` | resolved — top UI document type `0x19` or `0x27` (B.5) |
| 10 | `camera_script_is_finished` | gameplay `0x00a20840` | `0x00a40e20` | resolved — not in camera mode 6, or elapsed ≥ duration (E.1) |
| 11 | `camera_look_through_do` | gameplay | `0x00a46860` | resolved — HUD-hide reference + two-point camera move to an object + `0x43`/`0x0e` broadcast (E.2) |
| 12 | `building_purchase_is_crib_being_purchased` | crib `0x005f1fc0` | `0x005eed90` | resolved — handle pair `0x014a0f90` non-zero (F) |
| 13 | `boss_battle_matt_set_phase` | gameplay | `0x00a40b40` | resolved — stores the phase unchecked; ≥ 4 also resets the QTE block (G.1) |
| 14 | `boss_battle_matt_qte_begin` | gameplay | `0x00a40d70` | resolved — phase-named QTE; **three crash paths** (G.2) |
| 15 | `autil_mashing_minigame_faded_out` | UI helper `0x00845aa0` | `0x00842b00` | resolved — clears byte `0x014b4b59` (H) |
| 16 | `audio_play_id_for_navpoint_do` | gameplay | `0x00a3ed10` | resolved — object sound by 16-bit id; returns serial or 0 (I.7) |
| 17 | `audio_play_id_for_character_do` | gameplay | `0x00a3f4f0` | resolved — character sound by 16-bit id; result pushed twice (I.4) |
| 18 | `audio_play_id_do` | gameplay | `0x00a3f470` | resolved — event by 16-bit id; possibly uninitialised result (I.2) |
| 19 | `audio_play_for_mover_do` | gameplay | `0x00a405c0` | resolved — object sound on a mover; serial or 0 (I.6) |
| 20 | `audio_play_for_character_weapon_do` | gameplay | `0x00a3f370` | resolved — object sound on a character's weapon; **null-character crash** (I.5) |
| 21 | `audio_play_for_character_do` | gameplay | `0x00a3f1e0` | resolved — character sound; (255, 0) when unresolved (I.3) |
| 22 | `audio_play_do` | gameplay | `0x00a3f030` | resolved — event by name; 255 for an unknown cue; possibly uninitialised result (I.1) |
| 23 | `assert_screen_is_faded_out` | gameplay | `0x007c9f50` | resolved — shared no-op stub (J) |
| 24 | `airplane_takeoff_do` | gameplay | `0x00a3efb0` | resolved — start take-off (local or forwarded); returns the result (K.1) |
| 25 | `airplane_land_do` | gameplay | `0x00a404d0` | resolved — start landing at an object; always true once resolved (K.2) |

---

## P. Crash-shaped and logic defects (summary)

| # | where | defect | status |
|---|---|---|---|
| 1 | G.2 `0x005e6ce6` → `0x005e6d1b` | `boss_battle_matt_qte_begin` while Matt is absent or dead → read of `[0 + 0xf0]` | CONFIRMED |
| 2 | D.2 `0x006f9180` → `0x006f91a0` / `0x006f8e2b` | `cell_cheats_activate_cheat` with an index ≥ the unlocked count (or negative/missing) → null cheat record read at `+3` or `+0x70` after confirming (or at once) | CONFIRMED structure |
| 3 | I.5 `0x00a3f41f` | `audio_play_for_character_weapon_do` with a valid cue and an unresolved character → read of address 8 | CONFIRMED |
| 4 | G.2 `0x005e7767` | phase ≥ 4 or negative (via `boss_battle_matt_set_phase`) → unbounded table read; 5-8 start the wrong QTE, 9-12 pass non-pointers to `_stricmp`, others arbitrary | CONFIRMED structure |
| 5 | G.2 `0x005e77c4` | `boss_battle_matt_qte_begin` with a target and a non-character/unresolved player in a named phase → method call with `this` = `0x1f00` (write) | CONFIRMED structure |
| 6 | I `0x00a26640` | `audio_play_do`/`audio_play_id_do` return, record and broadcast an uninitialised stack value when audio is not initialised | CONFIRMED structure |
| 7 | D.2 | pending cheat index in one global; a second call before the dialog is answered redirects the first dialog | CONFIRMED structure |
| 8 | E.2 | `camera_look_through_do` with an unknown object still hides the HUD (reference taken before the lookup) | CONFIRMED structure |
| 9 | I.2/I.4/I.7 | cue ids truncated to 16 bits | CONFIRMED structure, consequence HYPOTHESIS |
| 10 | D.1 `0x006f90d0` | unlocked-list append without a capacity check (200 slots) | CONFIRMED structure, reachability OPEN |
| 11 | D.1 | content-gated cheats never unlocked by this binding; tag narrowed by truncation before hashing | CONFIRMED structure |
| 12 | G.1 | out-of-range phase stored and kept (the reset does not clear it) | CONFIRMED |
| 13 | I.3/I.4 | return arity varies: (255, 0) on failure vs 1 value; id pushed twice | CONFIRMED |
| 14 | K.2 | `airplane_land_do` returns true regardless of the landing routine | CONFIRMED |
| 15 | E.1 | `camera_script_is_finished` true when no scripted camera runs; false on the call frame for duration 0 | CONFIRMED |
| 16 | B.6 | `cell_camera_is_enabled` false until explicitly enabled | CONFIRMED |
| 17 | B.1 / J | constant-false and no-op bindings (`cell_debug_all_enabled`, `assert_screen_is_faded_out`) | CONFIRMED |
| 18 | M.1 | 15 handlers read arguments unconditionally (stale-stack semantics) | CONFIRMED |

---

## Q. Clean-room check

- Addresses are plain hex throughout; no Ghidra auto-names for functions, globals or labels, no decompiler variable
  names, and no pasted pseudocode. Library routines are named only where Ghidra identified them as standard C runtime
  (`_stricmp`, `memset`, `wcsncpy`) or as the public Lua 5.1 API by established project convention (`lua_gettop`,
  `lua_tolstring`, `lua_type`, `lua_pushcclosure`, `lua_setfield`, …), plus the public Wwise entry point already named in
  §2; short game strings (UI tags, mission and QTE names, hook and callback names, UI document names) are quoted as data.
- Self-check pattern:
  `\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4}|LAB_[0-9a-fA-F]+|FUN_[0-9a-fA-F]+|DAT_[0-9a-fA-F]+)\b`.
  Controls first (Python `re`): 14 positives (auto-named locals and parameters, a register input, stack arrays, an
  `unaff_` and an `extraout_` register, a type name, the three label forms), all matched; 7 negatives ("left undefined",
  "in ECX", a bare `0x…` address, prose, and the pattern's own fragments as written in this section), none matched. The
  same control set through `grep -cP` matched the 5 positives and none of the 3 negatives it was given.
- Result on this file before this section was added: **0 hits** (Python `re` line by line, and `grep -cP`). The run was
  repeated on the finished file, including this section, as the last step before reporting: **0 hits**.
- No spec file was edited. The private Ghidra copy `tools\gp_t24` was deleted after the dumps; raw dumps stayed in the
  session scratchpad.
