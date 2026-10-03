# Ranking tranche 20 — 25 previously-unspecced Lua-bound names (Team A, 2026-10-03)

Job definition: `D:\Project Crreish\TEAM A\ghidra\jobs\ranking\tranche-20.json`. The job file's own title states **names
426-450 of the 554 unspecced names, Team B call-count order**; this note uses that range and exactly the 25 names the job
lists. Run locally, read-only, on a private copy of the Ghidra project (`tools\gp_t20`, deleted afterwards), with the
job's own arguments: `CrreishDump.java <out>/lua lua depth:1 maxfuncs:15 maxinsn:500 <25 names>`. **All 25 names
resolved.** Every name string occurs exactly once in `.rdata`, and for all 25 the handler is the code pointer stored in
the slot right after the name (insn offset +1) — CONFIRMED — dump `index.txt`, cross-checked against the range run for
the two table registrars that are new or partly new to this series (section A). No name in this tranche is registered by a
pointer-before-name one-name registrar; the one `lua`-mode "use" that landed on the `lua_setfield` primitive
(`garage_clear_new_1.txt`, rooted at `0x00dfe830`) is the garage registrar's loop reading the **first** row of its table,
exactly as tranche 18 section A describes for the pause map — not a second handler.

Follow-up runs on the same private copy, cited by name below:

- "range run": `range` mode on the garage registrar `0x005fc500`, the gang-customization registrar `0x0083a900`, and the
  unlock sibling `0x00a4cf70`;
- "xref run": `xref` mode on `0x023013f0`/`0x023013f4`/`0x023013f8`, `0x0083a900`, `0x0083a630`, `0x00a4cf40`, `0x014a1dc0`,
  `0x014a1d1c`;
- "follow-up dump": depth-0 `func` run on 25 callees (listed in section J);
- "second follow-up dump": depth-0 `func` run on `0x005f63e0`, `0x00a28150`, `0x008ccb90`;
- "second range run": the four sibling gang-customization handlers and the two slot-array loops (section J).

Call counts are Team B's (`D:\Crreish-sync\for-team-b\team-b\tools\lua_reconciliation_called_and_registered_1181.tsv`):
**every name in this tranche is 1 call site in 1 script.** Team B tags 11 names `ui` (`get_world_income_dollars`,
`get_localized_string_for_tag`, `get_char_in_string`, the seven `garage_*` and `gang_customization_select_vehicle`) and 14
`gameplay`; section A shows the registrars agree exactly (14 in the gameplay registrar, 11 under UI registrars).

**Already partly covered elsewhere (cited, re-verified, not re-derived):** `get_world_income_dollars` appears with its
handler in the pause-map table of `spec-lua-api-behaviour.md` §31.1 but has no body entry; `get_stag_active`'s byte is
already identified (setter `set_stag_active` §17.19; synced variable `"stag_active"`, tranche 07 C.4). Tranche 16 B.2 left
**"which binding owns `0x005f87c0`"** OPEN — it is `garage_can_customize` (H.6). `spec-lua-api-behaviour.md` §27.9
describes the garage array `0x014a1a80` as "vehicle-type/model ids"; this tranche's garage bindings show it holds
**pointers to garage-entry records** (H, correction in K.4).

Labels: **CONFIRMED** = read in the listing of a dump made for this note; **HIGH CONFIDENCE** = follows from a dumped call
or reference, but the callee body was not dumped or a meaning is inferred from strong usage; **HYPOTHESIS** = plausible,
not settled; **OPEN** = not settled (collected in section L).

**Shared conventions, cited not re-described:** every entry opens with the `lua_gettop` prologue (`0x00dfde50`) and
reads arguments with negative stack indices counted from the bottom (`-N` = arg 1, `1-N` = arg 2, …). Primitives as in the
tranche 09/13/16/18 front matter: `0x00dfe210` `lua_tolstring` (non-string reads as null), `0x00dfe1e0` `lua_toboolean`,
`0x00dfe040` `lua_type` (0 = nil, 5 = table), `0x00dfe160` `lua_tonumber` (non-number reads as 0), `0x00ea2596` truncating
float-to-int, `0x00dfe590` `lua_pushboolean`, `0x00dfe3a0` `lua_pushnumber`, `0x00dfe5e0` `lua_gettable`, `0x00dfde60`
`lua_settop`, `0x00dfec70` `lua_next`; `0x00dfe420` pushes a C string (null pushes nil). "Nil-gated optional argument" =
the tranche 13 idiom; an **unconditional** read has no presence check (missing boolean = false, missing number = 0,
missing string = null). Resolvers: the vehicle resolver `0x00a281e0` (`spec-lua-api-behaviour.md` §28.1 / §4.5), the
character resolver `0x00a28150` (null-safe — second follow-up dump), the per-kind named-object lookups on `0x02442750`
of tranche 16/18's front matter (`0x005982e0` kind row `+0xb` bit `0x02`, the "5th resolver" of §7.5 used for teleport
destinations; `0x005eab60` script groups, kind row `+0xa` bit `0x20`, §6.6 / §25.16; `0x00734e90` kind row `+0x6` bit
`0x02`, §10.5). Handle resolution `0x00458230` on `0x024433a8` with key `0x031d152c` and the `+0x33` bit `0x10` rejection;
liveness `0x00853b10`; kind-flag rows `[0x02cc9900 + 4·(object byte +0x34)]`. The **double-gate** record-and-replicate
setter shape (`0x008ae480` then `0x008837a0`; owner `0x008ae3a0`; opcode `0x46` with two debug-tag strings; gates false and
no owner → nothing) is `spec-lua-api-behaviour.md` §7.2 / tranche 13 B.3 / tranche 16 D.2 / tranche 18 F.2. The
**single-gate numeric-mode** shape (gate `0x008addb0(object, 0)`; false → open an opcode-`0x42` record with two 8-bit
header values, a 16-bit object id from `0x008add70`/`0x00bc5610`, the payload, sent to `0x008ae020(object)` via
`0x0086f110`; true → write locally) is shape 2 of the setter taxonomy in the front matter of `spec-lua-api-behaviour.md`
§7, and §13.23 / §17.27 / §28.1 for the helicopter family; `0x00a79470(vehicle)` simply returns `vehicle + 0x100`
(§28.1, Q15). `0x00853ea0(object, 0, 0)` is the object **destroy** primitive (§27.9, Q4). The Lua-hook firing helpers
(`0x00e0cef0` "hook exists", `0x00e0ca80` look up, `0x00e0cd00` dispatch, with the hook record's `+0x14` set to a document
handle) are the coroutine mechanism of the tranche front matter and `spec-lua-api-behaviour.md` §31.3. Name hashes
`0x00d9e8b0` / `0x00d9e740` / `0x00d9e7e0` all return 0 for a null string (tranche 18 I.5).

---

## A. Registrars — where the 25 names live

Six registrars. CONFIRMED — `index.txt`, range run, xref run:

| registrar | reached from | names in this tranche |
|---|---|---|
| gameplay `0x00a20840` | (established) | 14: `homie_mission_lock`, `helicopter_set_large_dspiral`, `helicopter_set_dont_move_in_combat`, `helicopter_set_dont_explode_in_air`, `helicopter_land`, `helicopter_enter_dropoff`, `helicopter_clear_max_bank_angle`, `guardian_angel_start_zone_pinning`, `guardian_angel_release_attack_groups`, `guardian_angel_clear_zone_pinning`, `group_create_hidden_do`, `group_create_do`, `get_stag_active`, `get_closest_object` |
| garage `0x005fc500` (11 rows) | UI `0x008430f0`, call at `0x00843150` (tranche 11) | 7: `garage_vehicle_load_pending`, `garage_remove_vehicle`, `garage_get_garage_type`, `garage_clear_new`, `garage_can_delete`, `garage_can_customize`, `garage_back_to_customize` |
| UI helper `0x00845aa0` (113 rows; `spec-lua-bindings.md` §13.5) | `0x008489e0` (tranche 11/16) | 2: `get_localized_string_for_tag`, `get_char_in_string` |
| pause map `0x007dfae0` (9 rows; §31.1) | UI `0x008430f0`, call at `0x00843195` (tranche 18) | 1: `get_world_income_dollars` |
| gang customization `0x0083a900` (8 rows) | UI `0x008430f0`, call at `0x0084314a` (xref run) | 1: `gang_customization_select_vehicle` |

- **Garage registrar `0x005fc500`, full table** (range run; name-then-function rows, loop count **11** at `0x005fc5c6`, each
  row `lua_pushcclosure` `0x00dfe4f0` then `lua_setfield` `0x00dfe830` into globals): `garage_clear_new` `0x005f7200`,
  `garage_get_repair_cost` `0x005f8710` (§23.10), `garage_can_customize` `0x005f87c0`, `garage_can_delete` `0x005fc2b0`,
  `garage_preview_vehicle` `0x005f8890` (§27.9), `garage_repair_vehicle` `0x005f88d0` (tranche 11 H.1),
  `garage_retrieve_vehicle` `0x005fa570`, `garage_remove_vehicle` `0x005fa6a0`, `garage_get_garage_type` `0x005f7240`,
  `garage_back_to_customize` `0x005f8980`, `garage_vehicle_load_pending` `0x005f7280`. Two of the handlers
  (`0x005fa6a0`, `0x005f8980`) have no Ghidra function boundary in the shared project (the range run shows the pointer
  without a function tag); the `lua`-mode dump read them anyway and their listings are self-consistent to their `RET`.
- **Gang-customization registrar `0x0083a900`, full table — new to this series** (range run; same loop shape, count **8**
  at `0x0083a996`; called once, from the UI bring-up `0x008430f0` at `0x0084314a`): `gang_customization_confirm_vehicle`
  `0x00839ad0`, `gang_customization_revert_vehicle` `0x0083a790`, `gang_customization_select_gang_sign` `0x00839b00`,
  `gang_customization_confirm_gang_sign` `0x00839ba0`, `gang_customization_revert_gang_sign` `0x00838e50`,
  `gang_customization_select_vehicle` `0x0083a630`, `gang_customization_show_tag` `0x0083a270`,
  `gang_customization_show_vehicle` `0x0083a550`. Three of these (confirm/revert/show vehicle) were read in the second
  range run because they share I.1's crash shape.
- `get_world_income_dollars` → `0x007db9c0` is row 3 of the pause-map table already printed in §31.1. CONFIRMED again here.
- The 113-row UI helper registrar's rows for this tranche (`get_localized_string_for_tag` → `0x00842960`,
  `get_char_in_string` → `0x008447e0`) are consecutive, between the row whose handler is `0x00844730` and tranche
  16's `set_char_in_string` `0x00844870`, consistent with the name-then-function table tranche 12 published; both bodies
  match their names (G).
- **Unlock sibling.** The gameplay slot after `homie_mission_lock` holds `0x00a4cf70`, a byte-for-byte copy of E.1's handler
  that writes **2** instead of 1 (range run). HIGH CONFIDENCE it is `homie_mission_unlock` (next slot; next line of
  `tools/lua_gameplay_names_only_1015.txt`); not part of this tranche.

---

## B. Helicopter — 6 functions (gameplay registrar)

### B.1 `helicopter_set_dont_explode_in_air` → `0x00a4cca0`

**Arguments:** 1 string = vehicle (`0x00a281e0`); 2 boolean, **unconditional** (missing = false). **Return:** 0 values.
CONFIRMED — listing.

**Body:** if the vehicle resolves, `0x00a7d630(flag)` with `this` = the vehicle — the **double-gate** shape on the
vehicle handle `+0x8/+0xc`: locally it writes **bit 0 (`0x01`) of vehicle byte `+0x1d7e`** (`0x00a7d755`-`0x00a7d765`);
otherwise, with an owner, it sends opcode `0x46` tagged `"vehicle"` / `"m_force_flagsdont_explode_in_air"`; with neither,
nothing. CONFIRMED — listing. The tag string has exactly one other user, `0x008afc30` (HIGH CONFIDENCE: the receive/apply
side, which references all three `m_force_flags…` vehicle tags of this section).

**No helicopter check:** unlike B.5/B.6 there is no `0x00ad31a0` mode test — the flag is written on **any** resolved
vehicle. CONFIRMED. No crash shape (resolver null-checked; null `this` tolerated by the setter).

### B.2 `helicopter_set_large_dspiral` → `0x00a4cd00`

**Arguments / return:** as B.1. **Body:** `0x00a7d7a0(flag)` — identical double-gate setter writing **bit 1 (`0x02`) of
vehicle byte `+0x1d7e`**; remote tag `"vehicle"` / `"m_force_flagslarge_dspiral"`. CONFIRMED — listing. The boolean
reaches the setter through the stack slot the `lua_toboolean` result was stored into (`[ESP+0x28]` then `[ESP]` after the
pops) — the harmless "stale stack slot" delivery §28.1 already records; the value is correct. HYPOTHESIS: "large death
spiral" selects a wider crash spiral; no reader of the bit was traced. No helicopter check; no crash shape.

### B.3 `helicopter_set_dont_move_in_combat` → `0x00a4cd60`

**Arguments / return:** as B.1. **Body:** `0x00b37900(vehicle, flag)` — the **single-gate** shape: gate true → **bit 1
(`0x02`) of vehicle byte `+0x102`** (sub-object `0x00a79470` + 2, at `0x00b37a76`-`0x00b37a8e`); gate false → opcode-`0x42`
record with header values **20** and **5**, the 16-bit vehicle id and the boolean, sent to `0x008ae020(vehicle)`.
CONFIRMED — listing. Same byte as `helicopter_set_dont_death_spiral` (§28.1: bit `0x08`, header 20/4) — the two are
siblings in one flag byte. No helicopter check; no crash shape.

### B.4 `helicopter_clear_max_bank_angle` → `0x00a4cae0`

**Arguments:** 1 string = vehicle. **Return:** 0 values. CONFIRMED — listing.

**Body:** `0x00b37ee0(vehicle)` — single-gate: gate true → **clears bit 7 (`0x80`) of vehicle byte `+0x101`**
(`0x00b38061`); gate false → opcode-`0x42` record, header **20 / 8**, vehicle id, and one boolean **false**
(`0x00b3801f`), no angle. CONFIRMED — listing.

This is the "off" form of `helicopter_set_max_bank_angle` (§13.23), whose record carries the same header 20/8 with a
literal flag 1 followed by the angle (§13.23, not re-dumped). HIGH CONFIDENCE that bit `0x80` of `+0x101` is §13.23's
"override enabled" bit. The stored angle (§13.23's `+0x15c` field) is **not** reset — only the enable bit is cleared — so a
later re-enable without a new angle would reuse the old one (CONFIRMED that this body does not touch it). No helicopter
check; no crash shape.

### B.5 `helicopter_land` → `0x00a4fce0`

**Arguments:** 1 string = vehicle (unconditional); 2 string = landing target name (unconditional). **Return:** exactly 1
boolean. CONFIRMED — listing.

**Body (CONFIRMED — listing):** pushes **false** unless the vehicle resolves, `0x00ad31a0(vehicle)` is true (AI mode
field `+0xbf4 → +0x2c` is 3 or 4 — the helicopter modes of §17.27), arg 2 is a string, and it resolves through
`0x005982e0` (kind row `+0xb` bit `0x02`) to a live object. Then `0x00b3de00(vehicle, &target +0x40)` and pushes **true**.

`0x00b3de00` (CONFIRMED — dump): single-gate.
- Gate false → opcode-`0x42` record, header **20 / 13**, vehicle id, and the target position (`0x004d2d40`), sent to the
  owner.
- Gate true → re-checks the mode; computes the **squared** distance from the vehicle (`+0x40`) to the target with
  `0x00da1330` (follow-up dump: a plain squared-length, no square root) and does **nothing** if it exceeds **40000.0**
  (float `0x0112d1ec`) — i.e. the target must be within **200 units**; asks `0x00b3d1a0(vehicle, target, 7.0)` (float
  `0x01117a50`) for a landing plan (it reads vehicle byte `+0x1d7d` bit `0x80` — B.6's "allow pilot drop-off" bit — and
  flight-record mode `+0x4b0` == 7; body only skimmed, HIGH CONFIDENCE) and, if one is returned, switches the AI state
  with `0x00b350a0(vehicle, 7, 7)` (the §17.27 transition helper), stores the target position in the flight record
  (vehicle `+0x344`..`+0x34c`, i.e. sub-object `+0x244`..`+0x24c`) and starts it with `0x00b36220(vehicle, plan)`.

**Return-value note — CONFIRMED structure:** the boolean only reports "helicopter in mode 3/4 and the target exists". It
is **true** even when the land request is then dropped (target farther than 200 units, no plan) or merely forwarded to
the owner. A script cannot use it to learn that landing started.

No crash shape (every lookup checked).

### B.6 `helicopter_enter_dropoff` → `0x00a4f8b0`

**Arguments:** 1 string = vehicle (unconditional); 2 string = drop-off point name (unconditional, `0x005982e0`); 3
nil-gated boolean, default **false** — "allow pilot drop-off" (named by the setter's own tag, below); 4 nil-gated string,
default **null** — the drop-off target; 5 nil-gated boolean, default **true**. **Return:** exactly 1 boolean. CONFIRMED —
listing (argument-count register tracked through each `DEC`).

**Body (CONFIRMED — listing):**
1. False unless the vehicle resolves, is in helicopter mode 3/4 (`0x00ad31a0`), and arg 2 resolves to a live object.
2. **Occupant gate:** when arg 3 is false, `0x00ab4e60(vehicle)` counts the live occupants — the occupant slots at vehicle
   `+0x1358` (stride `0x30`; slot count `0x00ab4af0`: 8, or vehicle `+0x824` when byte `+0xbd0` is 1), each resolved by
   handle to kind row `+0xa` bit `0x01`, alive and not dead (`0x0096f4f0`). **Fewer than 2 → pushes false and returns**
   (via `0x0059f8a0`, which pushes a byte the code has just zeroed). HYPOTHESIS: a drop-off needs a passenger besides the
   pilot unless the pilot may get out.
3. `0x00a7d4c0(arg 3)` with `this` = the vehicle — double-gate setter on **bit 7 (`0x80`) of vehicle byte `+0x1d7d`**,
   remote tag `"vehicle"` / `"m_force_flagsallow_pilot_drop_off"`. This is the bit B.5's planner `0x00b3d1a0` reads.
4. Target handle: `0x00a3be60(arg 4, drop-off position)` — `0x00a3bd20` (follow-up dump): null name → nothing; a named
   object (`0x00a27e20`, alive); otherwise **`"#CLOSEST_PLAYER#"` or `"#PLAYER#"` both mean "the player closest to the
   drop-off point"** (walks the player list at world `0x03171a64` `+0x1f0`/`+0x1f8` by squared distance); otherwise the
   character resolver `0x00a28150`. If nothing resolves, the handle pair defaults to vehicle `+0x108`/`+0x10c`
   (sub-object `+0x8`/`+0xc`; HYPOTHESIS: the previous/default drop-off target).
5. `0x006f62b0(vehicle, 2, 0)` (follow-up dump) cancels any pending entry for (vehicle, 2) in the request list
   `0x014f4be0` and allocates a fresh 16-bit token (12-bit counter `0x014f6b28`, wrapping to 1 after `0xfff`, combined with
   `0x008677f0()` in the top 4 bits); **0 when registration fails**, passed on anyway (HYPOTHESIS: a request/ack token).
6. `0x00b3da30(vehicle, drop-off position, target handle, arg 5, token)` — single-gate: gate false → opcode-`0x42` record to
   the owner; gate true → mode re-check, the landing planner `0x00b3d1a0`, AI-state transition `0x00b350a0`, and arg 5
   stored as bit 0 of flight-record byte `+0x252` (`0x00b3dc52`-`0x00b3dc6d`), then `0x00b36220` (skimmed; HIGH
   CONFIDENCE).
7. Pushes **true**.

**Notes (CONFIRMED structure):** as in B.5, **true does not mean the drop-off started** (the dispatch result is not used).
When the occupant gate fails, the allow-flag is **not** written. `"#PLAYER#"` here is "closest player", not "local
player" — on co-op that can be the remote player. No crash shape: `0x00a3bd20` and `0x00a28150` are both null-safe
(second follow-up dump).

---

## C. Guardian angel — 3 functions (gameplay registrar)

Context (interp_tabs Q12, tranche 17 E.3): `0x014b2ddc` is the singleton of the `0x00614a00`–`0x00615dc8` cluster;
`0x00614ca0()` returns **X** = `[0x014b2ddc] + 0x1c` (null-checked) and `0x00614cb0()` returns **Y** = `[X + 0x1c]`.

### C.1 `guardian_angel_start_zone_pinning` → `0x00a4b0f0`

**Arguments:** 1 string = zone-set name (read only after the gates; unconditional). **Return:** 0 values. CONFIRMED —
listing.

**Body (CONFIRMED — listing, follow-up dump):**
1. X must exist and the object at **X `+0xc`** must report **3** from its virtual `+0x8` (HYPOTHESIS: the same "guardian
   angel activity" mode §28.4 / Q12 test through the mission's `+0x164` sub-object); else nothing.
2. `0x00d21720()` — a two-instruction `return 1` stub, result unused (inert, like §28.4's `0x00d34d40`).
3. The name is hashed with `0x00d9e8b0` (CRC name hash; null → 0) and `0x00626550(this = X, hash)`:
   - **first releases any current pin**: if X `+0x28` ≥ 0, `0x0085d930()` and X `+0x28` = −1;
   - then scans X's named zone sets (count X `+0x2c`, entries from X `+0x30`, stride `0x20`, hash at entry `+0`); on a
     match `0x0085fc30(entry + 4)` pins that set's zones and X `+0x28` = its index (returns true); no match → returns false.

`0x0085fc30` (follow-up dump): the set is a list of 16-bit zone ids (pointer at `+0`, count at `+8`). **More than 8 ids →
nothing is pinned.** For each id, class = id >> 13; classes 0 and 4 are skipped; a class of 6 or 7 **stops the
whole loop**; otherwise the id is appended (`0x0085db50`) to the pin list returned by virtual `+0x14` of
`[0x02444fa0 + 4·class]` — except that for **class 1**, if that list already holds an entry, the **whole loop stops**. HYPOTHESIS: the streamer keeps pinned zones resident (the binding's name).

`0x0085d930` (the release, follow-up dump) zeroes the count (`+0x8`) of the lists of **class 3 (`0x02444fac`) and class 2
(`0x02444fa8`) only**.

**Logic notes (CONFIRMED structure):**
- A misspelt or unknown set name **releases the current pin and pins nothing** — the release runs before the lookup.
- A set with more than 8 zones pins nothing, yet X `+0x28` is still set to its index.
- The release empties the class-2/3 lists **wholesale** (not just this binding's ids) and **never touches classes 1 and 5**,
  which the pin path can fill. Whether other systems also use those lists, and what clears classes 1/5, is OPEN
  (`0x0085d930`'s third caller is `0x00722d60`).

**Crash shape (CONFIRMED shape, reachability OPEN):** X `+0xc` is dereferenced for the virtual call without a null check
(`0x00a4b10e`-`0x00a4b111`).

### C.2 `guardian_angel_clear_zone_pinning` → `0x00a4abf0`

**Arguments:** none read. **Return:** 0 values. **Body:** the same two gates as C.1, then `0x00626530(this = X)`: if X
`+0x28` ≥ 0, `0x0085d930()` and X `+0x28` = −1. CONFIRMED — listing. Same release limits and the same unchecked X `+0xc`
(`0x00a4ac09`) as C.1. Calling it with nothing pinned does nothing.

### C.3 `guardian_angel_release_attack_groups` → `0x00a4abd0`

**Arguments:** none read. **Return:** 0 values. CONFIRMED — listing.

**Body:** `0x0062a130()`: only when `0x006d2910()` (the active mission's `+0x164` sub-object, slot `+0x8`; Q12) returns **3**
and Y = `0x00614cb0()` is non-null, `0x00629e40(this = Y)` (follow-up dump): for each record of Y (count Y `+0x1c`, base
Y `+0x18`, stride `0x30`) and each sub-entry (count record `+0x14`, base record `+0x10`, stride `0x28`) whose handle
`+0x8/+0xc` resolves to a **live script group** (kind row `+0xa` bit `0x20`) **that has been created** (`+0x3a` bit `0x02`, D):
`0x00679db0(group)` then `0x00a2b490(this = group, 0, 0)` — **exactly the pair `group_destroy_do` (§25.16) runs**. So this
destroys every spawned attack group the guardian-angel object references. CONFIRMED.

The table entries are not cleared (the handles stay; a second call finds the groups dead and skips them). Note the gate
differs from C.1/C.2 (mission sub-object here, X `+0xc` there); both test for 3 (HYPOTHESIS: the same object). No crash
shape (Y and every group null/liveness-checked).

---

## D. Script groups — 2 functions (gameplay registrar)

### D.1 `group_create_do` → `0x00a4c880` and D.2 `group_create_hidden_do` → `0x00a4c8e0`

**Arguments:** 1 string = script-group name (unconditional). **Return:** 0 values. The two handlers are identical except
for one literal: `group_create_do` passes **hidden = 0**, `group_create_hidden_do` passes **hidden = 1**. CONFIRMED —
listings.

**Body (CONFIRMED — listing):** resolves the group with `0x005eab60` (alive). Then:
1. `0x00677b40(group)` — unless the group is already created (`+0x3a` bit `0x02`): for every member of the NPC ring
   (`+0x40`, next `+0x9c`) `0x00bbb4a0(member +0x84, ">group_npc", [member +0xa4] +0x8, 5)`, and for every member of the
   vehicle ring (`+0x48`, next `+0xa0`) `0x00bbb500(member +0x84, "group_veh", member +0xa4, 5)`. HYPOTHESIS: queue
   resource/streaming requests for the members' character and vehicle types at priority 5 (I.1 uses `0x00bbb500` the same
   way with the tag `"gang_cust"`).
2. `0x00a2bb50(this = group, hidden)` — if `+0x3a` bit `0x02` is already set, nothing; otherwise sets it, and for every
   object in the member ring (`+0x3c`, next `+0x88`): byte `+0x90` bit 2 (`0x04`) = hidden; if `+0x90` bit 0 is clear,
   virtual `+0x54` (HIGH CONFIDENCE: spawn/activate) then virtual `+0x68`; when that reports false, a deferred callback is
   armed with `0x00894330(0x00a2ba50, 0x00a2bae0, member handle, 2, 5000, 0)` and `+0x90` bit 1 set (HYPOTHESIS: retry or
   time out the spawn after 5,000 ms).

**Logic note (CONFIRMED structure):** creation is **once only** — a second `group_create_do` or `group_create_hidden_do`
on an already-created group does nothing, so `group_create_hidden_do` after `group_create_do` does **not** hide anything.
`group_destroy_do` (§25.16) is the counterpart. No crash shape (resolver and liveness checked).

---

## E. Homie missions — 1 function (gameplay registrar)

### E.1 `homie_mission_lock` → `0x00a4cf40`

**Arguments:** 1 string = homie/activity name (unconditional). **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing, follow-up dump):**
1. `0x007e7470(name)`: hash with `0x00d9e7e0` (null → 0), then `0x007e5e10(hash)` searches the two activity-record pools
   of tranche 13 E.1 — pool 1 at `0x022ad780` (count `0x022ad758`), pool 2 at `0x022b0180` (count `0x022ad75c`), stride
   `0x180`, name hash at record `+0x20` — and returns the index (pool-2 indices offset by the pool-1 count) or **−1**.
2. `0x007e8af0(index, 1)`: if 0 ≤ index < total (`0x022ad754`), record **`+0xc0` = 1**, then tail-jumps to `0x007e80a0`.
3. `0x007e80a0` — a **global sweep over every activity record** of both pools: for each whose handle `+0x78/+0x7c`
   resolves to a live object of kind row `+0xa` bit `0x04`, where the single gate `0x008addb0(object, 0)` is true and the
   record is **not available** (`0x007e74f0(record)` false — tranche 13 E.1's availability test, which reads `+0xc0`), the
   object is destroyed (`0x00853ea0(object, 0, 0)`).

So `homie_mission_lock` sets the record's lock state to 1 (the unlock sibling `0x00a4cf70` writes **2**, section A) and
immediately despawns the world object of every record that is now unavailable — HYPOTHESIS: the homie's contact
character/trigger. The records are the same ones `cellphone_dial` dials (tranche 13 E.1), so a locked homie's phone entry
should read "busy" there through the same availability test (HIGH CONFIDENCE).

**Notes (CONFIRMED structure):** an unknown or nil name is silently ignored (index −1 fails the bound). The `+0xc0` write is
local only (no record is sent); the despawn only happens where the `0x008addb0` gate is true. The sweep covers **all**
records, not only the one just locked, so it also despawns objects of records that were already unavailable for other
reasons. Field `+0xc0` therefore has at least three values: 0 (as loaded, HYPOTHESIS), 1 (locked), 2 (unlocked). No crash
shape.

---

## F. World / object queries — 3 functions

### F.1 `get_stag_active` → `0x00a4ab50` (gameplay registrar)

**Arguments:** none read. **Return:** exactly 1 boolean = byte **`0x014ff6b9`** (via the one-instruction getter
`0x00702a10`). CONFIRMED — listing.

The byte is the synced variable `"stag_active"` registered (and zeroed) by `0x00704280` (tranche 07 C.4) and written by
`set_stag_active` (§17.19, which writes it through `0x00702a00`); the dump's global list shows exactly those two writers
plus this reader. Unrelated to the pause-map stag-mode byte `0x0229a317` (tranche 18 D.1;
`interp_pause_map_stag_district_control.md`). Trivially safe.

### F.2 `get_world_income_dollars` → `0x007db9c0` (pause-map registrar, §31.1)

**Arguments:** none read. **Return:** exactly 1 number. CONFIRMED — listing.

**Body (CONFIRMED — listing, follow-up dump):** `0x009ec4d0(&total)` computes an amount in **cents** using the money helpers
(follow-up dump): `0x009601d0` sets from **dollars** (clamped to ±20,000,000, then ×100), `0x00960160` sets from cents,
`0x00960190` adds cents — all saturating at **±2,000,000,000 cents**. The binding then divides the cents by **100,
truncating toward zero** (multiply-by-`0x51eb851f` idiom at `0x007db9da`) and pushes the integer as a number.

`0x009ec4d0` sums three parts:
1. **Districts** — for each entry of the world list at `0x03171a64` `+0x10c` (count `+0x114`, objects via `+0x58`): start
   from the district's float `+0x60`; for each member handle in `+0x50` (count `+0x54`) resolving to a live object of kind
   row `+0x9` bit `0x02`: if the member's own owner handle `+0x58/+0x5c` resolves to a live object of kind row `+0x8` bit
   `0x80`, **subtract** the member's float `+0x3c` from the district total; otherwise, if the member's `+0x3a` bit `0x02` is
   set, **add** `+0x3c` to an "owned" sum. If the remaining total exceeds 0.01, add round(owned ÷ total × district int
   `+0x58`) cents (`0x00dad900` rounds half away from zero).
2. **Income objects** — for each object of the world list `+0x178` (count `+0x180`) with byte `+0x92` bit 0 set: add
   round(round(float `+0x94` × int `[+0x88] +0x14`) × 100) cents.
3. **Player bonus** — if a local player exists, its 16-bit field `+0x27ac`, as dollars.

HYPOTHESIS for the meanings: district members are the purchasable properties, `+0x3c` their weight, `+0x3a` bit `0x02`
"owned by the player's gang", kind `+0x8` bit `0x80` owners the ones excluded from the share; part 2 is per-property
income × upgrade level. CONFIRMED that the result is **whole dollars, rounded down in magnitude**, and that it is
capped at ±$20,000,000 by the money type. No crash shape (every handle checked; the player is null-checked).

### F.3 `get_closest_object` → `0x00a4b5b0` (gameplay registrar)

**Arguments:** 1 string = reference object name (unconditional); 2 table of object names (must be a Lua table — `lua_type`
5 — else ignored). **Return:** exactly 1 string. CONFIRMED — listing.

**Body (CONFIRMED — listing):** the reference resolves through `0x00734e90` (kind row `+0x6` bit `0x02`, §10.5) and must be
alive. Count = `0x0083dff0` (tranche 18 F.5: the table's `n` field if numeric, else a `lua_next` count). For i = 1 … count,
`t[i]` is read as a string, resolved the same way, and its **squared 3-D distance** (`+0x40/+0x44/+0x48`) to the reference
is compared; the first candidate is taken unconditionally and later ones only when strictly closer (ties keep the
earlier entry). The **name string** of the winner is pushed.

**Return-shape note (CONFIRMED):** when the reference does not resolve, arg 2 is not a table, the table is empty, or no
entry resolves, the binding pushes the **empty string** `""` (the zero byte at `0x0129a0e3`), never nil. Entries of other
object kinds are silently skipped. No crash shape.

---

## G. UI string helpers — 2 functions (UI helper registrar `0x00845aa0`)

### G.1 `get_localized_string_for_tag` → `0x00842960`

**Arguments:** 1 string = text tag (unconditional). **Return:** exactly 1 string. CONFIRMED — listing.

**Body (CONFIRMED — listing, follow-up dump):** `0x00849df0(tag)`: null → none; else the hash `0x00d9e740(tag, 0)` is looked
up by `0x00849950` in **14** loaded string tables (records of `0x54` bytes from `0x02319760`, each with a loaded flag, a
bucket array and a mask; a bucket entry whose first dword equals the hash yields the text at entry `+4`). Found → the
UTF-16 text is converted by `0x00e22a30(buffer, 0x400, text)` and pushed; not found → the empty string `""`.

**Return-shape note (CONFIRMED — listing of `0x00e22a30`):** the pushed string is **not plain text**. `0x00e22a30` writes a
leading byte `0x80`, then **3 bytes per character** — a marker byte (1, plus 2 when the low byte is 0, plus 4 when the
high byte is 0, with those zero bytes written as `0xff`), the low byte, the high byte — and ends with `0x08 0x00`; an empty
text gives a single 0 byte. HYPOTHESIS: the engine's own encoding for wide text carried in byte strings, decoded by the UI
text setters; a reimplementation that returns UTF-8 must make sure the consumers accept it. With the 1,024-byte buffer
at most **340 whole characters** survive (the 341st group's two data bytes are overwritten by the terminator).

**Latent defect (CONFIRMED arithmetic, not reachable from this binding):** `0x00e22a30` writes 3 bytes whenever the cursor
is below `size − 1`, so for a buffer size that is a **multiple of 3** its last group lands one byte past the end. 1,024 and
512 (the garage populate caller `0x005fb000`) are not; other callers were not audited.

### G.2 `get_char_in_string` → `0x008447e0`

**Arguments:** 1 string (unconditional); 2 number = index (unconditional, truncated). **Return:** **1 string, or 0
values**. CONFIRMED — listing.

**Body:** if index ≥ 0 and index < the string's length, formats the byte at that position with `"%c"` (`0x01163040`) into a
2-byte buffer (`0x00da78d0`, a bounded `vsnprintf`) and pushes the one-character string; otherwise returns **nothing**.
The index is **0-based** (C style), unlike Lua's `string.sub`; the partner `set_char_in_string` (tranche 16) is 0-based
too. CONFIRMED.

**Crash — CONFIRMED (`0x00844820`):** the length is computed by an inline `strlen` loop on the arg-1 pointer **without a
null check**. A nil or non-string arg 1 with an index ≥ 0 reads address 0. (A negative index returns before the loop.)

---

## H. Garage — 7 functions (garage registrar `0x005fc500`)

Garage state used below (all zero-fill globals): **`0x014a1dc4`** current garage object (type at `+0x7c`, list key byte
`+0x7a`); **`0x014a1dc0`** pointer to the current garage's **entry list** (head at `+0`, count at `+4`; entries linked by
`+0x78` prev / `+0x7c` next); **`0x014a1a80`** an array of up to **168** entry pointers (it ends where the count
**`0x014a1d20`** begins), filled by `0x005fb000` (the `garage_populate` callback named in `spec-lua-bindings.md` and
§27.9) which
resets the count, appends each qualifying entry **without a capacity check**, and never clears slots past the count;
**`0x014a1d1c`** the selected entry (§27.9); **`0x014a1d28/0x014a1d2c`** the handle of the vehicle on display (§23.10).
A garage entry record holds its vehicle handle at `+0/+4`, a vehicle-type id at `+0x8` (compared against `0x022cdf58`),
flag byte `+0xc` (bit 0, bit `0x20`, bit `0x40`, bit `0x80` = "new"), 16-bit type ids at `+0xe`/`+0x10`/`+0x12`, and the
list links; a live vehicle points back to its entry at `+0x1ac8`. CONFIRMED — listings below and follow-up dump of
`0x005fb000`.

### H.1 `garage_vehicle_load_pending` → `0x005f7280`

**Arguments:** none read. **Return:** exactly 1 boolean: **false** with no garage object; otherwise **true** if the
deadline `0x012ece54` is armed (`0x00d9e4c0`: value ≥ 0; −1 = idle, the file value) **or** the pending-load dword
`0x014a1ce8` is non-zero. CONFIRMED — listing. The deadline is armed for 250 units by `0x005f8670` (§27.9) whenever the
display vehicle is released. Safe.

### H.2 `garage_get_garage_type` → `0x005f7240`

**Arguments:** none read. **Return:** exactly 1 number = garage object `+0x7c`, or **−1** with no garage. CONFIRMED.
Safe. (H.6 treats type 0 as the only customisable type.)

### H.3 `garage_clear_new` → `0x005f7200`

**Arguments:** 1 number = list index (unconditional, truncated). **Return:** 0 values. CONFIRMED — listing.

**Body:** if 0 ≤ index **≤** count (`0x014a1d20`), clears bit 7 (`0x80`, "new") of byte `+0xc` of the entry at
`0x014a1a80[index]` (`0x005f7229`-`0x005f7230`).

**Crash — off-by-one, CONFIRMED structure (`JG` at `0x005f7227`):** the bound accepts **index == count**. Slot `[count]` is
not a live entry: it is **null** if the array never held that many entries (zero-fill → write to address `0xc`), a
**stale pointer** to an entry released by a later repopulate or by H.7 (writes into a recycled record), or — when the array
is full (168 entries) — the count dword itself, used as a pointer. Its sibling `garage_back_to_customize` (H.5) bounds the
same array correctly (`index < count`), which is the fix template.

### H.4 `garage_can_delete` → `0x005fc2b0`

**Arguments:** none read. **Return:** exactly 1 boolean. CONFIRMED — listing.

**Body:** false unless the display vehicle (`0x014a1d28/2c`, vehicle kind row `+0x6` bit `0x80`, alive) resolves. Then true
iff **all** of:
1. `0x005fb930(garage handle)` > 1 — the garage object resolves (kind row `+0x9` bit `0x10`, alive), its list key has a
   list in `0x012ece20[+0x7a]`, and `0x005facf0` (follow-up dump) counts the list's entries whose type is valid for this
   garage (`0x00ac1840`, `0x005faac0`), excluding — only when `0x00867830()` is true — entries whose vehicle is currently
   live; with no garage object the count is 0;
2. the vehicle's entry (`+0x1ac8`) exists and its flag bit 0 is clear;
3. the entry's `+0x8` differs from `0x022cdf58`.

So a vehicle cannot be deleted when it is the garage's only (counted) vehicle, has flag bit 0 (HYPOTHESIS: a protected /
mission vehicle), or matches `0x022cdf58` (HYPOTHESIS: the vehicle type the customise screen returns to, H.5). Safe.

### H.5 `garage_back_to_customize` → `0x005f8980`

**Arguments:** none read. **Return:** 0 values. CONFIRMED — listing.

**Body:**
- If the selected entry `0x014a1d1c` exists and its flag byte has bit `0x20`: fires the Lua hook
  **`"store_vehicle_switch_mode"`** (`0x00812b50`) and returns.
- Otherwise it searches `0x014a1a80[0 … count−1]` for the entry whose `+0x8` equals `0x022cdf58`. Found → selection =
  that entry, byte `0x022cdf54` = 1, `0x005f8670()` (release the display vehicle, arm the 250 deadline; §27.9), then fires
  **`"garage_vehicle_load_begin"`** (`0x005f5eb0`) and **`"store_lock_controls"`** (`0x00812ab0`). Not found → fires
  `"store_vehicle_switch_mode"`.

All three hooks are fired only if they exist (`0x00e0cef0`), with record `+0x14` = the document handle `0x022cde34`.
Bounded loop; but the selection pointer it tests is whatever H.7 or `garage_preview_vehicle` left there (K.4). No crash
shape of its own beyond that.

### H.6 `garage_can_customize` → `0x005f87c0`

**Arguments:** none read. **Return:** exactly 1 boolean. CONFIRMED — listing.

**Body:** false unless the display vehicle resolves (as H.4). Then true iff a garage object exists, its type `+0x7c` is
**0**, the vehicle's entry exists with flag bit **`0x20` set** and bit 0 clear, and **`0x006d9cb0()` is false**.
`0x006d9cb0` is tranche 16 B.2's 12-name sibling of the store check: `0x006d9b90` over `"m01"`, `"m02"`, `"m06"`,
`"m09"`, `"m11"`, `"m13"`, `"m15"`, `"m18"`, `"m19"`, `"m23"`, `"dlc2_m01"`, `"dlc3_m03"` (the first ten decoded from
their static dwords, e.g. `0x0031306d` = "m01") — true while one of those missions is the active one. **This resolves
tranche 16 B.2's OPEN "which binding owns `0x005f87c0`".** So customising is refused in those 12 missions (`"m04"`, which
blocks the store, does not block customising). Safe.

### H.7 `garage_remove_vehicle` → `0x005fa6a0`

**Arguments:** none read. **Return:** 0 values. CONFIRMED — listing.

**Body:**
1. `0x005f8be0(list = [0x014a1dc0], entry = selection 0x014a1d1c)`: null entry → nothing. Otherwise clears the back-link
   `+0x1ac8` of the entry's vehicle if that vehicle is live; unlinks the entry from the list (head moves to `+0x7c` when the
   entry is the head; a lone entry empties the list); **decrements the list count**; and calls `0x005f68c0` (register
   `ESI` = entry; follow-up dump), which resets the entry's fields and **returns it to its pool** through `0x005f63e0`
   (second follow-up dump: pool `0x012ecea8` when flag bit 0 is set, else `0x012ece58`; unlinked from the pool's used
   ring and pushed on its free ring, under the pool lock).
2. If the display-vehicle handle is non-zero: resolves it (vehicle kind, alive) and **hides** it (`0x00a87780(handle, 1,
   0)`, the hide chain of `vehicle_hide` §14.15; `vehicle_show` §12.18 calls it with 0) and **destroys** it (`0x00853ea0`); then resets the handle to the null pair `0x01127a80`.

**Logic defect — CONFIRMED structure:** the selection `0x014a1d1c` is **not cleared**, and the entry stays in the
`0x014a1a80` array, although the record has gone back to its pool. Consequences:
- **A second call** (before the selection changes) finds the same entry: it is no longer the head and its links are now
  zero, so nothing is unlinked — but the **list count is decremented again** and the record is **handed to the pool a
  second time**, even if the pool has since given it to another entry. The count can go wrong (and negative), and a live
  record can be released.
- `garage_clear_new` (H.3), `garage_back_to_customize` (H.5) and `garage_preview_vehicle` keep using the stale pointer
  until the next populate.

**Crash shape (CONFIRMED shape, reachability OPEN):** `0x005f8be0` dereferences the list pointer (`CMP [ECX], ESI` at
`0x005f8c4e`) without a null check; with a selection set but no current list (`0x014a1dc0` = 0) that reads address 0.

---

## I. Gang customisation — 1 function (registrar `0x0083a900`)

### I.1 `gang_customization_select_vehicle` → `0x0083a630`

**Arguments:** 1 number = position among the **visible** catalogue items (unconditional, truncated); 2 number = gang
vehicle **slot** (unconditional, truncated). **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing):**
1. Walks the catalogue: categories at `[0x023013fc]` (count byte `0x02301405`, `0x14` bytes each: item pointer array `+0`,
   item count `+4`, a flag byte `+8`), with a running item index into two per-item byte tables (`0x02300ac8`: the item's
   category, `0x023009c8`: its index in that category). Items whose category has flag `+8` == 1 are skipped and not
   counted. When the visible counter equals **arg 1**, the item's pointer is stored in **`0x023013f0[arg 2]`** and passed
   to `0x0083a3c0` (spawn the preview).
2. Then `0x00e25890(0x022cd9cc)` turns the stored UI handle into an element pointer (null when the handle is stale or its
   slot index ≥ 128) and `0x00e26140(element, {type 5, value false}, 1)` sets one property on it; byte `0x013007a8` = 0.

`0x0083a3c0` (dumped at depth 1): `0x0083a300` releases the previous preview, then the item's type ids (`0x00ac27a0`,
`0x00a95d60(item, "saints")`) are requested under the tag `"gang_cust"` (`0x00bbb500`, priority 5, request id in
`0x01300e84`), a vehicle is created at a fixed placement (`0x0080fec0(…, 7, 0)`, `0x00a7a120`, `0x00bb8fe0`), and, if it
exists, its handle is stored in `0x02303bf0/0x02303bf4` and `0x01300e05` = 0.

**Crash 1 — unchecked slot index, an arbitrary-address write. CONFIRMED (`0x0083a6eb`).** `0x023013f0` is a **3-slot** array:
both loops that walk it stop at `0x023013fc` (`0x00839ee2`, `0x0083a07e`, second range run) and nothing references
`0x023013f8` directly (xref run). Arg 2 is used with no bound: **slot 3 overwrites `0x023013fc`, the catalogue-category
pointer this same loop is reading**, slot 4 reaches `0x02301400` (another pointer the slot loader uses), and any other
value writes an item pointer at `0x023013f0 + 4·slot` anywhere in the address space; a negative slot writes below the
array. The three sibling bindings of the same registrar index it the same way, also unchecked (second range run):
`gang_customization_revert_vehicle` **writes** `[0x023013f0 + 4·arg 1]` (`0x0083a7e2`), `…_confirm_vehicle` reads it
(`0x00839aec`) and `…_show_vehicle` reads it (`0x0083a5fd`) — an out-of-range read there feeds an arbitrary dword to
`0x0083a3c0` as an item pointer. Fix: accept slots 0-2 only.

**Crash 2 — null write after a failed lookup. CONFIRMED structure (`0x0083a300`).** When the stored preview handle
`0x02303bf0/0x02303bf4` is non-zero but no longer resolves to a live vehicle, the vehicle pointer is set to 0
(`0x0083a375`) and the routine still calls `0x008ccb90(0, …)` and `0x00853ea0(0, 0, 0)` and then **ORs `0x200000` into
`[0 + 0x16c0]`** (`0x0083a390`) — a write to address `0x16c0`. Trigger: the preview vehicle was destroyed or despawned by
something else (HYPOTHESIS for reachability) and any of `gang_customization_select_vehicle`, `…_revert_vehicle`,
`…_show_vehicle` (all reach `0x0083a3c0`), `0x00810b60` or `0x00811650` runs. Fix: skip the whole release when the lookup
fails, as the zero-handle path already does.

**Crash 3 — CONFIRMED shape, reachability OPEN.** If `0x00e25890` returns null, `0x00e26140` passes the null element as
`this` to `0x00e26040`, which reads through it (element `+0x18` onward, `0x00e2605b`/`0x00e26062`). Normally the element
exists while the gang-customisation screen is open.

**Edges (CONFIRMED):** an arg 1 that matches no visible item stores and spawns nothing but still runs step 2. The per-item
byte tables are 256 entries each; a catalogue of more than 256 items would read past them (HYPOTHESIS: never the case).

---

## J. Method notes for the follow-up runs

- Range run: `0x005fc500-0x005fc5f0` (garage table), `0x0083a900-0x0083a9d0` (gang table), `0x00a4cf70-0x00a4cfa0` (unlock
  sibling).
- Xref run (`xrefs:40`): `0x023013f0 0x023013f4 0x023013f8 0x0083a900 0x014a1dc0 0x014a1d1c 0x0083a630 0x00a4cf40`.
- Follow-up dump (depth 0, `maxinsn:900`): `0x007e5e10 0x007e80a0 0x00a4cf70 0x00da1330 0x00b3d1a0 0x00b36220 0x00a3bd20
  0x006f62b0 0x00b3da30 0x0085d930 0x0085fc30 0x00629e40 0x009601d0 0x00960160 0x00960190 0x00dad900 0x00849950 0x005f68c0
  0x005fb000 0x005f8890 0x005facf0 0x00e26040 0x0083a300 0x00ab4af0 0x0083a900`. `0x00b3d1a0`, `0x00b3da30` and
  `0x00b36220` were skimmed for their calls and flag tests only (HIGH CONFIDENCE wording above).
- Second follow-up dump (depth 0): `0x005f63e0 0x00a28150 0x008ccb90`.
- Second range run: `0x00839e40-0x00839f10` and `0x0083a000-0x0083a0a0` (the two loops that bound the 3-slot array),
  `0x00839ad0-0x00839b00`, `0x0083a790-0x0083a800`, `0x0083a550-0x0083a630` (confirm / revert / show vehicle),
  `0x00810c70-0x00810cb0` (another indexed reader of the slot array).
- All 25 handlers were taken from the `{name, function}` slot pairs (insn offset +1), confirmed for the two UI tables by
  the range run; none needed the pointer-before-name correction.

---

## K. Cross-function observations

1. **Three helicopter flag families in one note.** Vehicle byte `+0x1d7e` bits 0/1 (`dont_explode_in_air`,
   `large_dspiral`) and byte `+0x1d7d` bit 7 (`allow_pilot_drop_off`) use the **double-gate, string-tagged** shape with tags
   `"vehicle"` / `"m_force_flags…"` (one receive site, `0x008afc30`); flight-record bytes `+0x101`/`+0x102` (bank-angle
   override, `dont_move_in_combat`, and §28.1's `dont_death_spiral`) use the **single-gate opcode-`0x42`** shape with header
   20 and a sub-type (4, 5, 8, 13 seen; 10 in §17.27). `0x00b3b1e0` is the other caller of every opcode-`0x42` builder read
   here (`0x00b37900`, `0x00b37ee0`, `0x00b3de00`, `0x00b3da30`) — HIGH CONFIDENCE the opcode-`0x42` receive dispatcher.
2. **"True" that does not mean "done".** `helicopter_land` and `helicopter_enter_dropoff` return true after only the
   cheap checks; the real request can still be dropped (distance, no plan) or merely forwarded. Same family as tranche 18's
   observation that return values often report "call accepted", not "effect achieved".
3. **Sibling pairs where one checks and the other does not** — fix templates:
   - `garage_back_to_customize` bounds the entry array with `< count`; `garage_clear_new` with `<= count` (H.3);
   - the zero-handle path of `0x0083a300` skips the release; the failed-lookup path writes through null (I.1 crash 2);
   - `0x00a3bd20`/`0x00a28150` handle a null name; `get_char_in_string`'s inline `strlen` does not (G.2).
4. **Correction to `spec-lua-api-behaviour.md` §27.9 (CONFIRMED — this tranche's listings):** `0x014a1a80` holds
   **pointers to garage-entry records**, not "vehicle-type/model ids" — `garage_clear_new` writes through `[slot] + 0xc`,
   `garage_back_to_customize` reads `[slot] + 0x8`, and the populate callback `0x005fb000` stores list nodes into it.
   `garage_preview_vehicle` (`0x005f8890`, follow-up dump) indexes the array with **no bound at all** and stores whatever it
   reads as the selection `0x014a1d1c`, which H.4/H.5/H.7 then dereference — an arbitrary-read → wild-pointer chain for any
   out-of-range preview index (CONFIRMED structure; outside this tranche's names, flagged for the backlog).
5. **Off-by-one and capacity on one 168-slot array:** the reader `garage_clear_new` accepts index == count (H.3) and the
   writer `0x005fb000` appends with no capacity check; with 168 or more qualifying entries the writer overwrites the count
   `0x014a1d20` with an entry pointer (CONFIRMED structure; reachability depends on garage capacity, OPEN).
6. **Pool recycling without clearing references** (`garage_remove_vehicle`, H.7) — the record goes back to its pool while
   the selection and the array still point at it; a repeat call releases it twice. Same "state outlives the object" family
   as tranche 18 I.4's null-video-with-playing-flag.
7. **A three-slot array indexed straight from Lua by four bindings** (I.1): two of them **write**. This is the most serious
   memory-safety finding of the tranche — an arbitrary-address write of a pointer-sized value controlled by script
   arguments. A binding layer should reject slots outside 0-2 for every `gang_customization_*_vehicle` name.
8. **Encoded strings cross the Lua boundary.** `get_localized_string_for_tag` returns the engine's `0x80`-prefixed 3-bytes-
   per-character form, not text (G.1); a reimplementation that changes the string representation must change every
   consumer that decodes it.
9. **Lock states are tri-valued.** Activity record `+0xc0`: lock writes 1, unlock writes 2 (E.1). A boolean model would
   lose the "never touched" state.
10. **No session claims in this tranche.** Several bodies here are gated on `0x008addb0` or the double gate, but nothing in
    this note depends on what those gates return in single player; `spec-lua-api-behaviour.md` §8.27/§26.28 already
    establish that single player has no session object, and no inference about sessions is drawn from these setters.

---

## L. OPEN

- A: confirm the slot after `homie_mission_lock` is `homie_mission_unlock` by reading its name string (HIGH CONFIDENCE only).
- B.1/B.2: readers of vehicle `+0x1d7e` bits 0/1 (what "large dspiral" changes).
- B.4: the byte offset §13.23's setter raises the override bit in (assumed `+0x101`); whether a re-enable without an angle
  can happen.
- B.5/B.6: the full bodies of `0x00b3d1a0` (landing planner), `0x00b3da30`, `0x00b36220`; the meaning of AI mode 7 and of
  flight-record `+0x252` bits; what the opcode-`0x42` receiver `0x00b3b1e0` does with sub-types 5/8/13; the role of vehicle
  `+0x108/+0x10c` and of the `0x006f62b0` token.
- C.1/C.2: the identity of the object at X `+0xc` (and whether it is the mission sub-object C.3 tests); whether pin lists of
  classes 1/5 are cleared elsewhere; `0x0085d930`'s third caller `0x00722d60`; whether X `+0xc` can be null.
- C.3: whether released groups are ever removed from Y's table.
- D: what `0x00bbb4a0`/`0x00bbb500` queue; the deferred callbacks `0x00a2ba50`/`0x00a2bae0`.
- E.1: the initial value of `+0xc0`; the kind `+0xa` bit `0x04` object the sweep destroys.
- F.2: the meaning of district `+0x58`/`+0x60`, member `+0x3c`/`+0x3a`, income-object `+0x94`/`+0x88`, and player `+0x27ac`.
- G.1: which consumers decode the `0x80`-prefixed form; other callers of `0x00e22a30` with a size that is a multiple of 3.
- H.4: what `0x00867830` tests; the meaning of entry flag bit 0 and of `0x022cdf58`.
- H.7: whether `0x014a1dc0` can be null while a selection exists; garage capacity versus the 168-slot array.
- I.1: when the stored preview handle can go stale (who else destroys the preview vehicle); whether `0x022cd9cc` can be
  stale while the screen is open.

---

## M. Direct-answer table

| # | name | registrar | handler | status |
|---|---|---|---|---|
| 1 | `homie_mission_lock` | gameplay `0x00a20840` | `0x00a4cf40` | resolved — activity record `+0xc0` = 1, then despawn sweep over all unavailable records (E.1) |
| 2 | `helicopter_set_large_dspiral` | gameplay | `0x00a4cd00` | resolved — double-gate, vehicle `+0x1d7e` bit 1; no helicopter check (B.2) |
| 3 | `helicopter_set_dont_move_in_combat` | gameplay | `0x00a4cd60` | resolved — single-gate `0x42`/20/5, vehicle `+0x102` bit 1 (B.3) |
| 4 | `helicopter_set_dont_explode_in_air` | gameplay | `0x00a4cca0` | resolved — double-gate, vehicle `+0x1d7e` bit 0 (B.1) |
| 5 | `helicopter_land` | gameplay | `0x00a4fce0` | resolved — land at a named point within 200 units; **true ≠ landing started** (B.5) |
| 6 | `helicopter_enter_dropoff` | gameplay | `0x00a4f8b0` | resolved — 5 args; needs ≥ 2 live occupants unless arg 3; `"#PLAYER#"` = closest player (B.6) |
| 7 | `helicopter_clear_max_bank_angle` | gameplay | `0x00a4cae0` | resolved — clears override bit `0x80` of `+0x101`; angle kept (B.4) |
| 8 | `guardian_angel_start_zone_pinning` | gameplay | `0x00a4b0f0` | resolved — release then pin a named zone set (≤ 8 zones); **unknown name clears the pin** (C.1) |
| 9 | `guardian_angel_release_attack_groups` | gameplay | `0x00a4abd0` | resolved — destroys every created attack group (`group_destroy_do`'s pair) (C.3) |
| 10 | `guardian_angel_clear_zone_pinning` | gameplay | `0x00a4abf0` | resolved — empties pin lists of classes 2/3 (C.2) |
| 11 | `group_create_hidden_do` | gameplay | `0x00a4c8e0` | resolved — create once, members flagged hidden (D) |
| 12 | `group_create_do` | gameplay | `0x00a4c880` | resolved — create once; resource requests + spawn (D) |
| 13 | `get_world_income_dollars` | pause map `0x007dfae0` (under UI) | `0x007db9c0` | resolved — district share + income objects + player bonus, cents ÷ 100 truncated (F.2) |
| 14 | `get_stag_active` | gameplay | `0x00a4ab50` | resolved — synced byte `0x014ff6b9` (F.1) |
| 15 | `get_localized_string_for_tag` | UI helper `0x00845aa0` | `0x00842960` | resolved — 14-table lookup; **returns the engine's encoded form**, `""` if unknown (G.1) |
| 16 | `get_closest_object` | gameplay | `0x00a4b5b0` | resolved — nearest name from a table by squared 3-D distance; `""` if none (F.3) |
| 17 | `get_char_in_string` | UI helper `0x00845aa0` | `0x008447e0` | resolved — 0-based char or no value; **nil string crashes** (G.2) |
| 18 | `garage_vehicle_load_pending` | garage `0x005fc500` (under UI) | `0x005f7280` | resolved — deadline armed or load pending (H.1) |
| 19 | `garage_remove_vehicle` | garage | `0x005fa6a0` | resolved — unlink + pool-release selected entry, destroy display vehicle; **selection left dangling** (H.7) |
| 20 | `garage_get_garage_type` | garage | `0x005f7240` | resolved — garage `+0x7c` or −1 (H.2) |
| 21 | `garage_clear_new` | garage | `0x005f7200` | resolved — clears "new" bit; **off-by-one index** (H.3) |
| 22 | `garage_can_delete` | garage | `0x005fc2b0` | resolved — > 1 counted vehicle, entry unprotected, not the return-to type (H.4) |
| 23 | `garage_can_customize` | garage | `0x005f87c0` | resolved — type 0 garage, entry bit `0x20`, not in 12 blocked missions; closes tranche 16 B.2 OPEN (H.6) |
| 24 | `garage_back_to_customize` | garage | `0x005f8980` | resolved — select the return-to entry and reload, or switch store mode (H.5) |
| 25 | `gang_customization_select_vehicle` | gang customisation `0x0083a900` (under UI) | `0x0083a630` | resolved — store/spawn catalogue item in a slot; **unchecked slot write**, null write in preview release (I.1) |

---

## N. Crash-shaped and logic defects (summary)

| # | where | defect | status |
|---|---|---|---|
| 1 | I.1 `0x0083a6eb` (also `0x0083a7e2` revert; reads `0x00839aec`, `0x0083a5fd`) | gang-customisation slot argument indexes the 3-slot array `0x023013f0` with no bound — **script-controlled write anywhere**; slot 3 overwrites the category pointer `0x023013fc` | CONFIRMED |
| 2 | I.1 `0x0083a375` → `0x0083a390` | stale preview-vehicle handle → pointer nulled, then `0x008ccb90(0)`, `0x00853ea0(0)` and an OR into `[0 + 0x16c0]` | CONFIRMED structure, reachability HYPOTHESIS |
| 3 | H.3 `0x005f7227` | `garage_clear_new` accepts index == count; slot `[count]` is null (write at `0xc`), stale, or the count itself | CONFIRMED structure |
| 4 | G.2 `0x00844820` | `get_char_in_string` with nil/non-string arg 1 and index ≥ 0 → `strlen` of null | CONFIRMED |
| 5 | H.7 / K.6 | `garage_remove_vehicle` releases the entry to its pool but leaves selection and array pointing at it; repeat call double-decrements the list count and re-releases the record | CONFIRMED structure |
| 6 | K.4 `0x005f8890` | `garage_preview_vehicle` (sibling, §27.9) indexes `0x014a1a80` with no bound; the read value becomes the selection used by H.4/H.5/H.7 | CONFIRMED (outside tranche names) |
| 7 | K.5 `0x005fb10d` | populate callback appends to the 168-slot array with no capacity check (overflows into the count) | CONFIRMED structure, reachability OPEN |
| 8 | H.7 `0x005f8c4e` | list pointer `[0x014a1dc0]` dereferenced unchecked when a selection exists | CONFIRMED shape, reachability OPEN |
| 9 | I.1 `0x00e2605b` | null UI element from `0x00e25890` used as `this` | CONFIRMED shape, reachability OPEN |
| 10 | C.1 `0x00a4b10e`, C.2 `0x00a4ac09` | guardian-angel object X `+0xc` used unchecked for a virtual call | CONFIRMED shape, reachability OPEN |
| 11 | G.1 `0x00e22a30` | encoder overruns by one byte for buffer sizes that are multiples of 3 (not this binding's 1,024) | CONFIRMED arithmetic, latent |
| 12 | C.1 | unknown zone-set name releases the current pin and pins nothing; > 8 zones pins nothing but records the index | CONFIRMED |
| 13 | C.1/C.2 | release empties class-2/3 pin lists wholesale and never releases classes 1/5 | CONFIRMED structure, consequence OPEN |
| 14 | B.5/B.6 | `helicopter_land` / `helicopter_enter_dropoff` return true without the request being applied | CONFIRMED |
| 15 | B.6 | drop-off refused (false) with fewer than 2 live occupants unless arg 3; `"#PLAYER#"` means closest player | CONFIRMED |
| 16 | B.1-B.4 | the four helicopter flag setters accept any vehicle (no helicopter test) | CONFIRMED |
| 17 | B.4 | clearing the bank-angle override keeps the stored angle | CONFIRMED |
| 18 | D | `group_create_hidden_do` after `group_create_do` (or a second create) is a no-op | CONFIRMED |
| 19 | E.1 | `homie_mission_lock` despawns objects of **every** unavailable record, not only the named one; unknown names ignored | CONFIRMED structure |
| 20 | F.3 | `get_closest_object` returns `""` (never nil) when nothing qualifies | CONFIRMED |
| 21 | G.2 | `get_char_in_string` is 0-based and returns no value out of range | CONFIRMED |
| 22 | G.1 | `get_localized_string_for_tag` returns an encoded byte string; `""` for an unknown tag | CONFIRMED |
| 23 | F.2 | income is whole dollars truncated toward zero, saturating at ±$20,000,000 | CONFIRMED |

---

## O. Clean-room check

- Addresses are plain hex throughout; no Ghidra auto-names for functions, globals or labels, no decompiler variable
  names, and no pasted pseudocode. Library routines are named only where Ghidra identified them as standard C runtime
  (`vsnprintf`, `memset`, `strncmp`; the inline length loop in G.2 is described as "`strlen`", its behaviour) or as the
  public Lua 5.1 API by established project convention (`lua_gettop`, `lua_tolstring`, `lua_gettable`, `lua_next`, …); short game strings (Lua
  hook names, debug tags, mission names, special name tokens) are quoted as data.
- Self-check run on the finished file, as the last step before reporting, with the pattern
  `\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4}|LAB_[0-9a-fA-F]+|FUN_[0-9a-fA-F]+|DAT_[0-9a-fA-F]+)\b`
  (`grep -cP`, and the same pattern through Python `re` line by line): **0 hits**. The pattern was first checked against
  16 controls: 12 positives (auto-named locals and parameters, a register input, a stack array, an `unaff_` register, a
  type name, and the `LAB_`/`FUN_`/`DAT_` label forms), all matched, and 4 negatives ("left undefined", "in ECX", a bare
  `0x…` address, ordinary prose), none matched.
- No spec file was edited. The private Ghidra copy `tools\gp_t20` was deleted after the dumps; raw dumps stayed in the
  session scratchpad.

