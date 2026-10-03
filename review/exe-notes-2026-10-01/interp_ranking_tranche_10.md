# Ranking tranche 10 — 25 previously-unspecced Lua-bound names (Team A, 2026-10-02)

Job definition: `D:\Crreish-sync\for-team-a\team-a\ghidra\jobs\ranking\tranche-10.json` (names 176-200 of the 554
unspecced names, Team B call-count order). Run locally, read-only, on a private copy of the Ghidra project
(`tools\gp_t10`, deleted afterwards), with the job's own arguments: `CrreishDump.java <out>/lua lua depth:1 maxfuncs:15
maxinsn:500 <25 names>`. **All 25 names resolved in that one call.** Every name string occurs exactly once in `.rdata`,
and its handler is the code pointer stored in the slot right after the name (insn offset +1) — CONFIRMED — dump
`index.txt`.

**Three registrars.** 21 names are stored by the gameplay registrar `0x00a20840`. The three `pause_map_*` names are
stored by `0x007dfae0`, and `pause_menu_change_control_scheme` by `0x007e18e0`. Both are small name/function-pair
tables: the strings and code pointers are written into stack slots, then a loop calls `lua_pushcclosure` (`0x00dfe4f0`)
and the set-global primitive (`0x00dfe830`) for each pair (seen at `0x007e1a27`/`0x007e1a35`) — CONFIRMED —
`index.txt`. **Both are UI sub-registrars:** each has exactly one caller, the 311-entry UI registrar `0x008430f0`
(`0x007e18e0` called at `0x0084317d`, `0x007dfae0` at `0x00843195`) — CONFIRMED — xref run. `0x007e18e0` registers
17 names (`pause_menu_change_control_scheme`, `pause_menu_control_scheme_init`, `pause_menu_update_option`,
`pause_menu_accept_options`, `pause_menu_restore_defaults`, `pause_menu_quit_game_internal`,
`get_current_difficulty`, `game_difficulty_select`, six `game_record_*` names,
`pause_menu_is_using_southpaw_control_scheme`, `pause_menu_is_using_vehicle_southpaw_control_scheme`,
`pause_menu_has_seen_display_cal_screen`). `0x007dfae0` registers 9 (`pause_map_stag_takeover_do_reward`,
`pause_map_stag_current_district_control`, `get_world_income_dollars`, `pause_map_is_stag_mode`,
`pause_map_is_tutorial_mode`, `pause_map_set_gps`, `pause_map_add_bookmark`, `pause_map_drag_map`, `pause_map_zoom`)
— CONFIRMED — follow-up dump string literals.

Follow-up runs on the same private copy: a `ptrs` run (16 dwords each at `0x0130c9a8`, `0x0130cb98`), an `xref` run
(`0x007dfae0`, `0x007e18e0`, `0x0130cba8`, `0x0229a47c`, `0x0229a2f4`, `0x0130c9a8`, `0x0130cb98`), and a depth-0
`func` run on 17 callees (cited as "follow-up dump").

Labels: **CONFIRMED** = read in the listing or decompile of a dump made for this note; **HIGH CONFIDENCE** = follows
from a dumped call or reference, but the callee body was not dumped or a meaning is inferred from strong usage;
**HYPOTHESIS** = plausible, not settled; **OPEN** = not settled (collected in section J).

**Shared conventions, cited not re-described:** every entry opens with the `lua_gettop` prologue (`0x00dfde50`) and
reads arguments with negative stack indices counted from the bottom (`-N` = arg 1, `1-N` = arg 2, …). Primitives as in
`spec-lua-api-behaviour.md` §4.1: `0x00dfe210` `lua_tolstring` (null for a non-string), `0x00dfe1e0` `lua_toboolean`,
`0x00dfe040` `lua_type`, `0x00dfe160` `lua_tonumber` (a non-number reads as 0), `0x00ea2596` truncating float-to-int,
`0x00dfe590` `lua_pushboolean`, `0x00dfe3a0` `lua_pushnumber`. The **standard optional idiom** below means: the slot is
read only when enough arguments were passed and `lua_type` of the slot is not nil, otherwise the stated default is
used. An **unconditional** read means no presence check: a missing boolean reads false, a missing number reads 0, and a
missing string reads null.

**Shared machinery this tranche reuses (cited, not re-derived):**

- Name resolvers on the object singleton `0x02442750`: all are the same shape — by-name lookup `0x004588f0` in the
  table at singleton `+0x2660` (skipped when the count at `+0x265c` is not positive), reject when object `+0x33` bit
  `0x10` is set, then filter by one bit of the class-descriptor row `0x02cc9900[object +0x34]`. Bits seen in this
  tranche (all CONFIRMED in this tranche's dumps): `0x005e4dd0` row `+0xb` bit `0x4` (character resolver per spec §3),
  `0x0062a190` row `+0xb` bit `0x8` (vehicle resolver), `0x0062a1f0` row `+0x8` bit `0x2` (used only by
  `path_name_is_path` here; "path" kind is HIGH CONFIDENCE from the caller's name), `0x005eab60` row `+0xa` bit `0x20`
  (used only by the four `*_script_group_do` functions here; "script group" kind is HIGH CONFIDENCE from the callers'
  names). The generic chains `0x00a280c0` (generic resolver, then the `#PLAYER#` sentinel → local player) and
  `0x00a28150` (`#FOLLOWER…#` sentinels, then `0x005e4dd0`, then a virtual call at vtable `+0x70`, which returns a
  narrowed object or null) are as described in spec §3 preamble — CONFIRMED (both bodies dumped again here).
- `0x00853b10`, the liveness guard: returns 1 ("dead") for null, for object `+0x33` bit `0x4`, or for object `+0x34`
  == `0xff` — CONFIRMED.
- **Record-and-replicate, double-gate variant** (spec §7 preamble, variant 1): the setter first calls `0x008ae480`
  (on the object's handle) and then `0x008837a0`. If either returns true, it writes the flag bit directly. If both
  return false, it does **not** write locally; instead it looks up a peer with `0x008ae3a0(handle)`, opens a record
  `0x0086f5f0(0x46)`, appends two debug-tag strings (a category and a "force-flag" name), the 8-byte handle and the new
  boolean, and commits to that peer (`0x0086f110`) — CONFIRMED in four bodies this tranche (`0x0094a520`, `0x009d55c0`,
  `0x004e0b70`, `0x009dfe30`). Each of those four setters has exactly one other caller (`0x008ae650`, `0x008af750`,
  `0x008b14c0`, `0x008b0f80` respectively), and each of those references the same tag string — HIGH CONFIDENCE that
  they are the inbound "apply" handlers on the receiving side.
- **Named hook-slot registration** (spec §10.8): `0x005e4660` (character hook sub-record; handle at sub-record
  `+0x68`/`+0x6c`) and `0x00a56d50` (vehicle hook sub-record; handle at `+0x30`/`+0x34`) are the same body: clear the
  slot through `0x005e4520` (releases the old hook through `0x00a1fc60` only on the session host, otherwise just writes
  -1), then, only for a non-null, non-empty callback name, store `0x00a1fcc0(name)` into the slot, then notify
  `0x00a29920(0, handle, slot, slot != -1)` — CONFIRMED (both bodies dumped again here). **An empty or nil callback name
  clears the slot.**

---

## A. Pause menu / pause map — 4 functions (sub-registrars `0x007e18e0`, `0x007dfae0`)

### A.1 `pause_menu_change_control_scheme` (`0x007e10d0`)

**Arguments:** 1 number (arg 1), read unconditionally and truncated to an integer — a control-scheme index.

**Return:** 0 values.

**Body:** reads a selector global `0x0229a558`. If it is 0, calls `0x005c12c0(index)`; otherwise `0x005c13c0(index)`.
The decompile drops the argument on the first branch, but the listing pushes the index before the branch, so both
callees receive it — CONFIRMED — disassembly (`0x007e10f4`). The only writer of `0x0229a558` is `0x007e1110` =
**`pause_menu_control_scheme_init`**, registered right after this one (follow-up dump). That function reads arg 1 as a
boolean and an optional arg 2 number (default -1). When arg 1 is false it returns nothing. When arg 1 is true it stores
arg 2 into `0x0229a558` and pushes 0, then (for arg 2 of 0) the set-A current index (`0x005bc180`) or (for 1 or 2) the
set-B current index (`0x005bc190`), then 25.0, so it returns 2 or 3 values — CONFIRMED (decompile). So the
selector is **0 → set A, anything else (including the -1 default) → set B**. HYPOTHESIS: A = on-foot schemes, B =
vehicle schemes (the sibling names `…_southpaw_control_scheme` / `…_vehicle_southpaw_control_scheme` suggest two
scheme families).

`0x005c12c0` and `0x005c13c0` are the same body over two different scheme sets — CONFIRMED:

| set | array base | count | current index |
|---|---|---|---|
| A (selector 0) | `0x014183a0` | `0x014183a8` | `0x01411b08` |
| B (selector ≠ 0) | `0x014240c0` | `0x014240c8` | `0x01411b10` |

Each record is `0xfc0` bytes. The body clamps the index to `[0, count-1]`, copies the current record into a
`0xfc0`-byte stack temporary (`0x005c1240`, thiscall copy) and calls `0x005bec10` on it; it then stores the new
index, copies the new record and calls `0x005bd4e0` — CONFIRMED (bodies). That `0x005bec10` unbinds and `0x005bd4e0`
applies a scheme is HIGH CONFIDENCE (not dumped).

**Edge case — empty set (crash-shaped, HYPOTHESIS on reachability):** with `count == 0` the clamp computes `count-1 =
-1`; index 0 passes the `< 0` test and is then clamped **down to -1**, so the copy reads the `0xfc0`-byte record that
sits *before* the array base (and the base itself may be null if the set was never loaded). CONFIRMED that the clamp
does this; whether either set can be empty while the pause menu is open is OPEN.

### A.2 `pause_map_zoom` (`0x007dbcc0`)

**Arguments:** 1 boolean (arg 1), unconditional — true = zoom in, false/missing = zoom out.

**Return:** 0 values.

**Body:** the sign is `+1` or `-1`; the compiled code raises it to the power 3 with an inlined integer-power loop,
which leaves it unchanged. Then the global zoom factor `0x012ff678` (file-backed initial value 0.5) becomes `zoom + sign
* 0.2 * zoom` — a ±20% multiplicative step — CONFIRMED (the 0.2 is the double at `0x012a2e30`, shown as 0.2 in the
decompile). The result is clamped to `[min 0x02299eac, max 0x02299f20]`. Both bounds are zero-fill globals whose only
writer is `0x007d9280` (map setup). Then `0x007d8ce0` recomputes the two pan-offset globals `0x0229a2bc`/`0x0229a2c0`
from the map extents and the new zoom; it clamps them and **divides by the zoom factor** — CONFIRMED (bodies).

**Edge case (HYPOTHESIS, not crash-shaped):** if this runs before `0x007d9280` has set the bounds, both bounds are 0,
the clamp forces the zoom to 0, the pan update divides by zero (float, so inf/NaN, not a fault), and every later step
stays at 0 because the step is multiplicative.

### A.3 `pause_map_set_gps` (`0x007dba10`)

**Arguments:** none read (the `lua_gettop` result is discarded). **Return:** 0 values.

**Body (CONFIRMED — disassembly and decompile):** three exclusive modes, chosen by pause-map state bytes:

1. **`0x0229a317` set:** if the object at `0x0229a2e4` is non-null, its `+0x44` is non-null and its `+0x54` > 0, it is
   copied into `0x0229a2ac`. Then, only if a Lua function named **`"pause_map_stag_completion"`** exists
   (`0x00e0cef0` returns 1), that function is called through the callback-call helpers (`0x00e0ca80` build, field
   `+0x14` set from `0x0229a364`, `0x00e0cd00` call). No GPS is set on this path.
2. **`0x0229a2b3` set (HIGH CONFIDENCE: taxi destination mode):** if a blip is selected (`0x0229a458` != -1), it shows
   a message built from the localization keys `"TAXI_INVALID_DESTINATION"` and `"?STORE_TITLE_PURCHASING"` through
   `0x0084a1b0`/`0x007c3d80`, then returns. No GPS is set.
3. **Otherwise — set a GPS route:**
   - No selection (`0x0229a458` == -1): the target is the map cursor's world position (`0x007d9bc0`), with height 0.
   - Selection of kind 3 (`0x0229a47c` == 3) **and** a co-op partner exists (`0x009df3d0` non-null): the target is the
     partner's position (partner `+0x40..+0x48`), and success flag `0x0229a2b2` is set to 1.
   - Any other selection, **including kind 3 with no partner**: the target is the selected blip's stored position
     (`0x0229a444..0x0229a44c`); a test `0x005812d0(position)` decides the third argument of `0x007d19e0`.
   - `0x007d19e0(target, 0, flag)` stores its result in `0x0229a2b2`. Follow-up dump: it forwards to `0x00a6cf50(target,
     5000.0, 0x08062d00, 4, -1, flag, !flag, 1, 0)`, with 4 replaced by `0x14` when its second argument is 1 (HIGH
     CONFIDENCE: a route/path request with a 5000-unit search limit). On failure, or when `0x007d2210(0)` is non-zero
     and `0x007d2240(0)` is not 1, it calls `0x007d6dc0(0,1,0)` and `0x007d9100(1)`, then returns.
   - **`0x007d9100(code)` is a Lua notification** (follow-up dump): if a Lua function named
     **`"pause_map_interface_event"`** exists, it is called with `code` as a number argument (call-object field
     `+0x14` set from `0x0229a364`) — CONFIRMED. Codes seen in this tranche: **1** = GPS route failed, **0** = GPS set
     (from `pause_map_set_gps`), **2** = bookmark added, **3** = bookmark removed (from `pause_map_add_bookmark`). So
     the pause-map UI script learns the outcome through this callback, not through a return value.
   - On success: for kind 3 it calls `0x007d70e0(partner handle, 0, 1, 0)` (route to a moving object); otherwise
     `0x007d6380`/`0x007d6bc0` with the local player and `0x007d9100(0)`. Finally `0x00bda330(target)`.

**Crash-shaped — CONFIRMED null dereference at `0x007dbc4e`:** on the success path, the kind-3 test re-reads
`0x0229a47c` and calls `0x009df3d0` **again with no null check**, then reads `+0xc` and `+0x8` of the result. The
co-op partner accessor returns null whenever there is no co-op partner. So "kind-3 blip selected (the partner's map
blip), partner gone, position route succeeds" reaches a read of address `0x8`/`0xc`. The earlier branch at `0x007dbb37`
handles the missing partner by falling back to the position path, but that path then rejoins the unguarded call.
Reachability needs a stale kind-3 selection after the partner left (HYPOTHESIS: plausible on co-op disconnect while
the pause map is open). The same unguarded call is reached with no selection at all if `0x0229a47c` still holds 3 from
an earlier selection. The xref run finds **no direct writer** of `0x0229a47c`, only the two reads in this function. It
is therefore written as a field of a larger selected-blip record (the neighbouring globals `0x0229a444..0x0229a458`
belong with it, HIGH CONFIDENCE), and whether it is reset when the selection clears is OPEN.

### A.4 `pause_map_add_bookmark` (`0x007df8a0`)

**Arguments:** none read. **Return:** 0 values.

**Body (CONFIRMED):** does nothing if any of the three mode bytes `0x0229a317`, `0x0229a2b3`, `0x0229a318` is set.
Otherwise it **toggles**:

- **Remove** — when the selected blip id `0x0229a410` is in `0x62..0x6b`: the selected object is fetched from its
  stored handle (`0x0229a418`/`0x0229a41c`) through `0x004f6710` (handle → object, filtered by descriptor row `+0xb`
  bit `0x2`; it may return null). If `0x008addb0(object, 0)` is true, it calls `0x00806e60(0x0229a408, 9)` (HIGH
  CONFIDENCE: remove that blip), `0x007d9100(3)`, `0x00853ea0(object, 0, 0)` (object removal), and decrements the
  bookmark count `0x0229a2f4`.
- **Add** — when nothing is selected (`0x0229a410` == -1) and the count is below 5: for slot `i = 0..4` it builds the
  name `"%d_bookmark%d"` from (a session byte, `i`). The byte is the local session member's `+0x158` byte through
  `0x008677f0`, or 0 with no session. `0x008677f0` takes no stack argument: the `i` pushed before it is the format's
  second value (the cleanup of 5 dwords matches). The first slot whose name does not resolve to a live object
  (`0x005982e0` + liveness) gets a new object: a spawn descriptor is built at the cursor's world position (height 0)
  through `0x008eb580(name, 2, position, 0x01321400)` and `0x008eae10(1)`, then spawned with `0x00456e30(0x29,
  descriptor)`. A blip is added with `0x00808c50(handle, 9, 0x62 + i, -1, 0.0, 0, 0, 3)`, followed by
  `0x007d9100(2)`, count `+1`, `0x00809270(0x0229a438, x, z, 9)`, and `0x0229a468 = 1.5`; the descriptor is destroyed
  (`0x00456380`).

**Crash-shaped — CONFIRMED null dereference at `0x007dfa4b`:** `0x00456e30` returns 0 when its pool allocation
(`0x00456be0`) fails or the virtual init at vtable `+0x2c` fails (body dumped), and the caller reads `+0xc`/`+0x8` of
the result with no check.

**Crash-shaped — CONFIRMED second null-dereference path (remove):** `0x004f6710` returns null when the stored handle is
0, the object no longer resolves, it is the wrong kind, or it is dead. `0x008addb0(null, …)` then returns **true**
(body dumped: null → 1), so the code still calls `0x00853ea0(null, 0, 0)`. That function skips its own guarded block
for null but then calls `0x004571e0(null, mask)`, which reads `object +0x32` unconditionally (follow-up dump) — a read
of address `0x32`. Trigger: "remove" pressed on a selected bookmark blip whose marker object has already been
destroyed (HYPOTHESIS for how that state arises, e.g. cleanup or streaming while the blip survives).

The remove range `0x62..0x6b` is 10 ids wide, while this function only ever assigns 5 (`0x62..0x66`) — CONFIRMED. The
bookmark count `0x0229a2f4` is reset to 0 by `0x007d91e0` and also compared with 5 by `0x007dd700` — xref run.

---

## B. NPC / party behaviour flags — 5 functions

### B.1 `npc_brute_always_throws` (`0x00a55190`)

**Arguments:** arg 1 string (character name), unconditional; arg 2 optional boolean, **default true** (standard idiom).

**Return:** 0 values.

**Body:** resolves arg 1 through `0x00a28150`. If found, and `0x0052a230(object)` is true (reads `object +0xf0` →
`+0x48` bit 0; the `+0xf0` pointer is dereferenced without a null check — HIGH CONFIDENCE it is always set for a
character), it calls `0x0094a520(value)` on the object — CONFIRMED. `0x0094a520` is a double-gate setter for bit
`0x200000` of `object +0x1c9c`, with tags `"human"` / `"human_force_flagsthrowing_brute"` — CONFIRMED. So the call is
a silent no-op for characters that fail the `+0xf0`/`+0x48` bit-0 test. HYPOTHESIS: that test is "is a brute".

### B.2 `npc_detection_enable` (`0x00a55300`)

**Arguments:** arg 1 string (character name), unconditional; arg 2 boolean, **unconditional** (missing → false).

**Return:** 0 values.

**Body:** resolves arg 1 through `0x00a28150`. If found **and** the global byte `0x014ff6b8` is 0, it calls
`0x009d55c0(value)` — a double-gate setter for bit `0x2` of byte `object +0x1e00`, tags `"npc"` /
`"npc_force_flagsdetection_events_enabled"` — CONFIRMED. `0x014ff6b8` is a zero-fill byte whose only writer
(`0x00941c90`) writes 0, and it is also read by `0x00943dc0`, `0x00a3bae0` and code at `0x00a5a84b`. While it is set,
this call is a silent no-op (its meaning is OPEN).

### B.3 `npc_weapon_pickup_override` (`0x00a557d0`)

**Arguments:** arg 1 string (character name), unconditional; arg 2 boolean, **unconditional** (missing → false).

**Return:** 0 values.

**Body:** resolves arg 1 through `0x00a28150`; if found, it calls `0x004e0b70(!value)` on the object's AI sub-record
at `object +0x2b0` — CONFIRMED. `0x004e0b70` is a double-gate setter for bit `0x80` of `sub-record +0xa` (object
`+0x2ba`), handle at sub-record `+0x610`/`+0x614`, tags `"human_ai_data"` / `"ai_force_flagscant_pickup_weapons"` —
CONFIRMED. **The Lua boolean is inverted:** `true` = may pick up weapons (clears "can't pick up"), `false`/missing =
can't pick up. The neighbouring registrar entry `0x00a55830` also calls `0x004e0b70` (not dumped; HYPOTHESIS: the
matching "clear override" call).

### B.4 `override_npc_run_and_cower_run` (`0x00a58ca0`)

**Arguments:** 3 strings, all unconditional: arg 1 character name, arg 2 and arg 3 two animation/action-set names
(HYPOTHESIS: "run" and "cower run", from the function name).

**Return:** 0 values.

**Body:** arg 1 is resolved **directly** through the character resolver `0x005e4dd0` (not the generic chain, so
`#PLAYER#`/`#FOLLOWER#` sentinels are not accepted), then the liveness check, then the vtable `+0x70` narrowing call.
On success: `object +0x1ce0 = 0x004bf810(arg 2)` and `object +0x1cd4 = 0x004bf810(arg 3)` — CONFIRMED.
`0x004bf810` returns -1 for a null name; otherwise it hashes the name (`0x00d9e8b0`) and scans a table of 16-byte
records at `0x03171c10` (count `0x03171c08`, filled by `0x004bf8d0`), matching the hash at record `+4` and then a
case-insensitive compare against the name at record `+0`. It returns the record index or -1 — CONFIRMED. **Passing nil
or an unknown name stores -1** (HIGH CONFIDENCE: "no override"). No network replication on this path.

### B.5 `party_use_melee_weapons_only` (`0x00a59490`)

**Arguments:** 1 boolean (arg 1), unconditional.

**Return:** 0 values.

**Body:** calls `0x009dfe30(value)` on the local player (`0x009da4e0`) and, when `0x009df3d0` returns a co-op partner,
on that partner too — CONFIRMED. `0x009dfe30` is a double-gate setter for bit `0x4` of byte `player +0x28ac`, tags
`"player"` / `"sync_flagsuse_melee_weapons_only"` — CONFIRMED. The decompile shows the value as an argument to
`0x009da4e0`; the listing shows that push is really the argument to the thiscall `0x009dfe30` (callee-cleaned,
`RET 0x4`). `0x009da4e0` takes nothing. Party members other than the two players are not touched here (HIGH
CONFIDENCE: the flag lives on the player and is consulted for followers elsewhere — not traced).

---

## C. Callback registrars (`on_*`) — 5 functions

All five take **arg 1 = Lua callback function name** (string, unconditional) and register it as a named hook; none is
an event poster. An empty or nil name clears the slot (shared machinery above).

| name | handler | target resolution (arg 2) | hook record | slot |
|---|---|---|---|---|
| `on_dismount` | `0x00a57e50` | character resolver `0x005e4dd0` + liveness | character, base `+0xf8` | `0x13` |
| `on_mount` | `0x00a580f0` | see C.2 — only when the generic chain does **not** match | character, base `+0xf8` | `0x12` |
| `on_hit_ped` | `0x00a57fd0` | generic chain `0x00a280c0` → else vehicle `0x0062a190` + liveness | character `+0x1f00` / vehicle `+0xb0` | `0x15` / `5` |
| `on_vehicle_debris_flow_recycled` | `0x00a587c0` | vehicle resolver `0x0062a190` + liveness | vehicle, base `+0xb0` | `0xb` |
| `on_m21_player_choice` | `0x00a56f80` | none (no arg 2) | global record `0x026e9528` | `0xc` |

All CONFIRMED — disassembly (slot literals at `0x00a57eae`, `0x00a58164`, `0x00a5801a`/`0x00a5805c`, `0x00a5881e`,
`0x00a56fa6`).

### C.1 `on_dismount` (`0x00a57e50`)
**Arguments:** arg 1 callback name, arg 2 character name. **Return:** 0 values. **Body:** as in the table; it does not
try `#PLAYER#`, so the local player cannot be targeted by that sentinel (HIGH CONFIDENCE: a player can still be
reached if its own registered name resolves through `0x005e4dd0` — not tested).

### C.2 `on_mount` (`0x00a580f0`) — inverted probe
**Arguments:** arg 1 callback name, arg 2 object name. **Return:** 0 values.

**Body:** first calls the generic chain `0x00a280c0(arg 2, &found)` and **discards the object**. Registration
continues **only if `found` is false**; it then resolves arg 2 again through `0x005e4dd0` and registers slot `0x12` at
base `+0xf8` — CONFIRMED (`0x00a5812f`: `CMP byte [found],0; JNZ exit`). `found` is set when `0x00a27e20` resolves a
live object, or when arg 2 is `"#PLAYER#"`. The consequence is CONFIRMED: `on_mount(cb, "#PLAYER#")` is a silent
no-op. Follow-up dump: `0x00a27e20` is another member of the per-kind resolver family, filtering on descriptor row
**`+0xa` bit `0x2`**, while the registering resolver `0x005e4dd0` filters on **`+0xb` bit `0x4`** — CONFIRMED. So
`on_mount` registers only for live objects that pass the `+0xb`/`0x4` test **and fail** the `+0xa`/`0x2` test.
`on_dismount` (C.1) has no such exclusion. Spec §3/§10.8 show the generic chain's hits keep their hook sub-record at
`+0x1f00` (as `on_hit_ped` C.3 uses), so this probe looks like a deliberate split. HYPOTHESIS: kinds with a `+0x1f00`
hook record are excluded so their mount hook is not written into the wrong (`+0xf8`) record; but no `+0x1f00`
registration for "mount" exists in this function, so for those kinds `on_mount` does nothing. Which concrete kinds
carry each bit is OPEN.

### C.3 `on_hit_ped` (`0x00a57fd0`)
**Arguments:** arg 1 callback name, arg 2 object name. **Return:** 0 values. **Body:** if the generic chain resolves
arg 2 (including `#PLAYER#`), it registers slot `0x15` in the hook sub-record at `object +0x1f00` via `0x005e4660`.
Otherwise it tries the vehicle resolver and registers slot `5` at `vehicle +0xb0` via `0x00a56d50` — CONFIRMED. So
"hit ped" hooks exist both on characters and on vehicles (HIGH CONFIDENCE: the vehicle case is "vehicle ran over a
pedestrian").

### C.4 `on_vehicle_debris_flow_recycled` (`0x00a587c0`)
**Arguments:** arg 1 callback name, arg 2 vehicle name. **Return:** 0 values. **Body:** as in the table — CONFIRMED.

### C.5 `on_m21_player_choice` (`0x00a56f80`)
**Arguments:** arg 1 callback name only. **Return:** 0 values.

**Body:** a hand-inlined copy of the registration helper over a **global** hook record at `0x026e9528` (not an
object): it clears slot `0xc` through `0x005e4520` (this = `0x026e9528`), then stores `0x00a1fcc0(name)` into
`0x026e9558` (= record + `0xc`*4) for a non-empty name. It notifies `0x00a29920` with kind tag **2** and the handle
pair read from `0x026e9568`/`0x026e956c` (record `+0x40`/`+0x44`) — CONFIRMED. Kind tag 2 matches
`on_qte_animation_trigger` (spec §10.7). The same global record is referenced by clothing-store UI code
(`0x00816f00`, `0x0081fe70`, `0x008200f0`) — HIGH CONFIDENCE that it is a player-level/UI hook record rather than
mission 21-specific; the "m21" refers to the script that uses slot `0xc`.

---

## D. Script-group HUD markers — 4 functions

Common shape: arg 1 is a script-group name resolved through `0x005eab60` (descriptor `+0xa` bit `0x20`) plus
liveness. The handlers then walk circular member lists hanging off the group object. A member is included when member
`+0x90` bit 0 is set and, on the first list only, its virtual at vtable `+0x68` returns true. The member's handle is
member `+0x8`/`+0xc` — CONFIRMED.

| list head on group | next pointer | extra virtual gate | walked by |
|---|---|---|---|
| `+0x3c` | member `+0x88` | vtable `+0x68` | `object_indicator_*` only |
| `+0x40` | member `+0x9c` | vtable `+0x68` | `minimap_icon_*` |
| `+0x48` | member `+0xa0` | none | `minimap_icon_*` |
| `+0x44` | member `+0xa4` | none | `minimap_icon_*` |

HYPOTHESIS: the lists hold different member kinds (for example characters, vehicles, items). The object-indicator
pair only reaches the `+0x3c` list, so groups whose members sit only on other lists get no indicators (OPEN).

### D.1 `object_indicator_add_script_group_do` (`0x00a59080`)

**Arguments:** arg 1 string group name (unconditional); arg 2 number (unconditional, truncated); arg 3 number
(unconditional, truncated); arg 4 optional number, default **3** (flags). The 100.0 float that the single-object
sibling takes as an optional arg 5 (spec §10.6) is **hard-coded** here (`0x0111645c`).

**Return:** 0 values (the single-object sibling returns a boolean; this one returns nothing).

**Body:** for each qualifying `+0x3c`-list member it calls `0x008f96f0(handle, arg 2, arg 3, flags, 100.0)` —
CONFIRMED. `0x008f96f0`: takes a 16-bit sequence number from `0x01308570` (post-increment). If flags bit `0x1` is set,
it creates the local indicator (`0x008f9280`), returning null on failure, and stamps the sequence at `+0x24` and the
flags at `+0x2c`. If bit `0x2` is set, it sends a `0x40` record of sub-type 2 carrying the handle (or the network id
from `0x008ae2b0`), arg 2, the sequence, arg 3 and 100.0 through the `0x0086f1b0` broadcast commit. On first use it
lazily looks up the `"object_indicator"` asset (`0x025f38d4`/`0x025f38d8`) — CONFIRMED (body).

**Crash-shaped — CONFIRMED null dereference at `0x00a59122`:** when arg 1 is nil, unknown, or names a dead group, the
code sets the group pointer to 0 (`0x00a59120 XOR EDI,EDI`) and then **unconditionally** reads `[group + 0x3c]`
(`0x00a59122 MOV ESI,[EDI+0x3c]`) — a read of address `0x3c`. The remove twin (D.2) and both minimap twins exit
cleanly on the same failure, so this is a one-off defect, not a pattern.

### D.2 `object_indicator_remove_script_group_do` (`0x00a59220`)

**Arguments:** arg 1 string group name; arg 2 optional number, default **3** (flags).

**Return:** 0 values.

**Body:** exits on a null, unknown or dead group (correct guard, CONFIRMED). For each qualifying `+0x3c`-list member,
it calls `0x008f7a00(handle, flags)`. `0x008f7a00`: if `0x008ae410(handle)` is false and the low 16 bits of the
handle's high dword are 0, it **forces flags to 1** (local only). If bit `0x2` is set, it sends a `0x40` record of
sub-type 4 with the network id. If bit `0x1` is set, it repeatedly finds (`0x008f4ef0`) and removes (`0x008f4f30`)
local indicators on that handle until none are left, so **every** indicator on the object is removed, not just the
script-group one — CONFIRMED (body).

### D.3 `minimap_icon_add_script_group_do` (`0x00a54220`)

**Arguments:** arg 1 string group name (unconditional); arg 2 string icon name (unconditional); arg 3 optional string,
default `""` (`0x0129a0e3`); arg 4 optional number (float), default 0.0; arg 5 optional number, default **3**. This is
the same argument shape as `minimap_icon_add_do` (spec §10.5).

**Return:** 0 values.

**Body:** if arg 2 is empty it returns. Otherwise both arg 2 and arg 3 go through `0x00802f60` (case-insensitive name
→ icon id). It scans 108 entries of 12 bytes from `0x012fff78` (first `"map_other_replaceme"`) and returns the index,
then 6 "icon class" entries of `0x24` bytes from `0x0115d2f4` (first `"icon_class_kill"`) and returns the stored
value; otherwise -1 — CONFIRMED. The function returns if the group fails to resolve or **arg 2's** id is -1. Arg 3's
id is passed through unchecked (default `""` → -1). For each qualifying member on all three lists it calls the
function pointer **`0x0130c9a8[arg 5]`** with (handle, icon id, arg-3 id, arg 4, 0) — CONFIRMED. Unlike the
single-object sibling, there is no descriptor-bit test and no alternative `0x0093d3a0` path: the table call is the
only path.

**Crash-shaped (all CONFIRMED in the listing):**

1. **Arg 2 is dereferenced without a null check** (`0x00a542ef`/`0x00a542f3`: load arg 2's string pointer, `CMP byte
   [EAX],0`). A nil or non-string arg 2 makes `lua_tolstring` return null, so this reads address 0.
2. **Unbounded function-pointer index:** arg 5 is a script-supplied integer used directly as `0x0130c9a8[arg 5]`
   (`0x00a5437e`, `0x00a543c1`, `0x00a54404`) with no range check. Entry 0 is a **file-backed null** (`0x0130c9a8`
   holds `0x00000000`), so `arg 5 = 0` calls address 0; negative or large values call whatever dword lies there. Table
   contents (ptrs run, file-backed): `[0] 0`, `[1] 0x00a3b7e0`, `[2] 0x00807070`, `[3] 0x00a3b810`, `[4]..[7] 0`,
   `[8] 0xffffffff`, `[9] 0xffffffff`, `[10]..[15] 0`. **Only arg 5 ∈ {1, 2, 3} is safe.** 0 and 4–7 call null, 8–9
   call `0xffffffff`. `[3]` = `0x00a3b810` forwards all six arguments to `0x00808ed0` (follow-up dump). HIGH
   CONFIDENCE, from the flag convention used by `0x008f96f0`/`0x008f7a00`: 1 = local only, 2 = remote only, 3 = both.
   **The same table is indexed the same unchecked way by `minimap_icon_add_do`** (spec §10.5; reads at `0x00a540ea`
   and `0x00a541ad`, xref run), so §10.5's OPEN "function-pointer array's individual entries" is answered here and
   the same hazard applies there.
3. A non-nil, non-string arg 3 (for example a table) makes `lua_tolstring` return null, which is then passed to
   `__stricmp` inside `0x00802f60` (HIGH CONFIDENCE crash; the CRT stricmp is not dumped).

### D.4 `minimap_icon_remove_script_group_do` (`0x00a544f0`)

**Arguments:** arg 1 string group name; arg 2 optional number, default **3**.

**Return:** 0 values.

**Body:** exits cleanly on a null, unknown or dead group. For each qualifying member on all three lists it calls the
function pointer **`0x0130cb98[arg 2]`** with (handle) — CONFIRMED. **Same unbounded-index hazard** as D.3 (`0x00a5458e`,
`0x00a545c1`, `0x00a545f4`). Table contents (ptrs run): `[0] 0`, `[1] 0x008070b0`, `[2] 0x008070d0`, `[3]
0x00807140`. Past index 3 the "table" runs straight into unrelated globals: **`[4]` is `0x0130cba8`, the live
`open_vint_dialog_check_done` result (F.3, file value -2)**, `[5] 0x0000ffff`, `[6] 0x02705448`, and so on. So arg 2 = 4
calls the dialog result as a code address. `0x00807140` = remove: `0x00806f70(handle)`, then
`0x00806e60(0x008052f0(handle, 5))` (follow-up dump). The table is also read by `0x00a54430`, the registrar entry just
before this one (HIGH CONFIDENCE: the single-object `minimap_icon_remove_do`), with the same unchecked index (xref
run).

---

## E. Mission help / mission state — 3 functions

### E.1 `mission_help_table_do` (`0x00a53ba0`)

**Arguments (10; optional ones use the standard idiom):**

| # | type | default | use |
|---|---|---|---|
| 1 | string (unconditional) | — | help-text key |
| 2 | boolean (unconditional) | false | also post to a second channel (see end of body) |
| 3 | boolean (unconditional) | false | use arg 7 for a record field |
| 4 | string | `""` | substitution string 1 |
| 5 | string | `""` | substitution string 2 |
| 6 | number → int | 3 | flags (stored in the record; also the "clear first" flags) |
| 7 | number (float) | 15.0 (`0x01117a58`) | value applied when arg 3 is true |
| 8 | number (float) | -1.0 (`0x012a2d54`) | display duration override |
| 9 | number → int | 3 | stored into the record |
| 10 | boolean | **true** | clear existing help first |

**Return:** 0 values.

**Body (CONFIRMED unless tagged):** returns immediately when `0x007205d0` is true, which is when global `0x0153b520`
is in 10..13 (a game-mode range; meaning OPEN). It builds a parameter record on the stack with defaults (duration 7.0
from `0x01117a50`, 0.5 from `0x0126d2cc`, several 1/0 fields, -1 sentinel). If arg 10 is true, it first calls the
`mission_help_clear` worker `0x007fd290(arg 6)` (E.2). If arg 3 is true, a record field gets arg 7. If arg 8 > -1.0,
the duration becomes arg 8. The key is then resolved in this order:

1. `0x00849df0(key)`: hash (`0x00d9e740`), then a localized-string lookup (`0x00849950`). A null key returns null.
2. If that fails and the table object `0x026e7e68` exists, the key is looked up by hash (`0x00604740`) in a table of
   12-byte records {hash, text, duration}. The text comes from `0x00604500(index)`; an out-of-range index returns the
   literal `L"NULL"`. **When arg 8 is exactly the -1.0 default**, the duration is taken from the table entry through
   `0x00604520(index)`. For an unknown key (index -1), that returns **10.0** because the unsigned bounds check fails.
   So an unknown key with the default duration shows for 10 s, not the record's 7 s.
3. Otherwise the raw key is widened (`0x00db06e0`, 8→16-bit, 256 characters, always terminated) and shown as-is.

Args 4 and 5 are widened the same way. The message is issued with `0x007fda30(&out, record, text, sub1, sub2)` →
`0x007fcba0`, and the returned handle goes to `0x006cf030`. If arg 2 is true, it also calls
`0x007e2a00(8, 0x006d28b0(text, arg 6, 1, 0))` (HIGH CONFIDENCE: a log/feed entry). No bounds hazards found:
both table accessors bounds-check, and every widening copy is capped at 256 characters.

### E.2 `mission_help_clear` (`0x00a52ab0`)

**Arguments:** arg 1 optional number → int, default **3** (flags; the presence test is "more than 0 args and slot not
nil").

**Return:** 0 values.

**Body:** returns immediately when `0x007205d0` is true (same game-mode gate as E.1). Otherwise it calls
`0x007fd290(flags)`, which uses only the low byte (CONFIRMED):
- bit `0x2`: if a session exists and the local machine is host (`session +0x5c == +0x58`, spec §8.27), it sends a
  record `0x0086f5f0(0x40)` with sub-type `0x15` through the broadcast commit `0x0086f1b0` (HIGH CONFIDENCE: tells
  clients to clear).
- bit `0x1`: calls `0x007ff270(1, 1)` (HIGH CONFIDENCE: local clear).

The default 3 does both. A client (non-host) with flags 3 clears only locally.

### E.3 `mission_collectible_collected` (`0x00a523b0`)

**Arguments:** none read. **Return:** 1 boolean.

**Body:** returns `0x005eaab0()` — CONFIRMED. `0x005eaab0` scans a static table at `0x0149fdd0`: 4 groups of `0x148`
bytes, each with up to 20 entries of 16 bytes (an 8-byte handle at `+0`, a byte at `+8`) and a count at group `+0x140`.
For each non-zero handle it resolves the object (`0x00458230` on `0x024433a8`, the same handle→object path as
`0x004f6710`) and requires: object `+0x33` bit `0x10` clear, descriptor row `+0xa` bit `0x80` set, alive, and **object
`+0xa8` bit `0x10` set**. It returns **the byte of the first entry that qualifies**; if none qualifies it returns
**true**. CONFIRMED. The reader does not check each group's count against 20 (whether any writer can exceed it is
OPEN). HYPOTHESIS for meaning: the table is the active mission's collectible list, `+0xa8` bit `0x10` marks an item
being collected, and the byte says whether that collection counts. Treat the return as "no blocker found → true".

---

## F. Small globals — 4 functions

### F.1 `parking_spot_set_max` (`0x00a59b20`) / F.2 `parking_spot_reset_max` (`0x00a59360`)

**`parking_spot_set_max`:** arg 1 number (unconditional, truncated); returns 0 values; calls `0x00919e50(value)`,
which stores the value into `0x01308da8` **only if it is ≤ 18 as an unsigned compare** (`CMP EAX,0x12; JA`).
Negative values and values above 18 are **silently ignored**; 0..18 are accepted — CONFIRMED.

**`parking_spot_reset_max`:** no args read; returns 0 values; `0x00919e40` writes **18** (`0x12`) — CONFIRMED.

`0x01308da8` has file-backed initial value 18. It is also reset to 18 by code at `0x00919f91`, read through the getter
`0x00919e60`, and read directly by `0x0091b7f0` and `0x0091cef0`. It is global, not per player. HIGH CONFIDENCE: 18 is
both the default and the hard ceiling of the ambient parked-car limit.

### F.3 `open_vint_dialog_check_done` (`0x00a58f10`)

**Arguments:** none read. **Return:** 1 number.

**Body:** pushes the signed integer global `0x0130cba8` as a Lua number — CONFIRMED. Its file-backed initial value is
**-2**. It is written with **-2** at `0x00a58e96` and with a computed value at `0x00a58d49`. The writers are (follow-up dump of `0x00a58e00`, the registrar
entry just before this one, name not dumped — HYPOTHESIS `open_vint_dialog`):

- `0x00a58e00` takes 4 strings (unconditional). It looks each one up in the help-text table `0x026e7e68` (the same
  `0x00604740`/`0x00604500` hash table E.1 uses), **sets the result to -2**, and opens a dialog
  `0x007c3310(text 1, 2, 0, 1)`. If that succeeds, it sets the body text (`0x007c18a0(text 2)`), adds two options
  (`0x007c2ac0(text 3, 0, 0)`, `0x007c2ac0(text 4, 0, 0)`), and installs the completion callback **`0x00a58d40`** at
  dialog `+0x13c`. That callback writes `0x0130cba8` at `0x00a58d49`. All CONFIRMED.
- So `open_vint_dialog_check_done` returns **-2 while the dialog is pending**, and afterwards the callback's value
  (HIGH CONFIDENCE: the chosen option's index). If `0x007c3310` fails, the result stays -2 forever, so a script that
  polls until it is no longer -2 never finishes (HYPOTHESIS-level logic hazard). Note the initial file value is also
  -2, so a check before any dialog was ever opened also reads "pending".
- Side observation (not one of the 25): `0x00a58e00` uses `0x026e7e68` as `this` for `0x00604740` **without** the
  null check that `mission_help_table_do` performs (`0x00a58e52`/`0x00a58e5c`) — CONFIRMED; whether the table can be
  absent at that point is OPEN.

### F.4 `path_name_is_path` (`0x00a5a790`)

**Arguments:** arg 1 string, unconditional. **Return:** 1 boolean.

**Body:** a null or non-string arg 1 → false. Otherwise `0x0062a1f0(name)` on the object singleton (descriptor row
`+0x8` bit `0x2` filter) followed by the liveness check; it pushes true only for a live object of that kind —
CONFIRMED. No sentinels are accepted.

---

## G. Cross-function observations

1. **Return shapes.** 22 of 25 return no Lua values. Only three push a result: `path_name_is_path` (boolean),
   `mission_collectible_collected` (boolean), `open_vint_dialog_check_done` (number). `pause_menu_change_control_scheme`
   pushes nothing; its sibling `pause_menu_control_scheme_init` (not in this tranche) pushes 2-3 numbers. Every
   failure in this tranche is silent (no Lua error is raised anywhere) — CONFIRMED.
2. **Script-group variants versus single-object siblings** (spec §10.5/§10.6). The group variants:
   (a) resolve with `0x005eab60` instead of `0x00734e90`;
   (b) for minimap icons, always dispatch through the function-pointer table, skipping the descriptor-bit test and the
   `0x0093d3a0` path;
   (c) for object indicators, hard-code the 100.0 float and return nothing where the single-object version returns a
   boolean;
   (d) disagree on the failure guard: three exit cleanly, while `object_indicator_add_script_group_do` dereferences
   null (H1).
   Object-indicator group calls walk only the `+0x3c` member list; minimap group calls walk three (`+0x40`, `+0x48`,
   `+0x44`).
3. **Record-and-replicate (double-gate) setters** in this tranche: `0x0094a520` (brute throws), `0x009d55c0`
   (detection events), `0x004e0b70` (can't pick up weapons), `0x009dfe30` (melee only). Each has exactly one other
   caller, which references the same tag string (inbound apply), and when both gates are false the change is sent to
   the owning peer instead of being applied locally (spec §7 variant 1). `override_npc_run_and_cower_run` writes two
   fields directly with **no** replication — CONFIRMED.
4. **Pause-map scripting surface.** The native pause-map functions report outcomes by calling two Lua globals by name,
   if they exist: `pause_map_interface_event(code)` (codes 0-3, A.3) and `pause_map_stag_completion` (A.3 mode 1). The
   pause-map mode bytes `0x0229a317`, `0x0229a2b3` and `0x0229a318` gate both pause-map entries. HYPOTHESIS: these
   are what `pause_map_is_stag_mode` / taxi mode / `pause_map_is_tutorial_mode` report (those functions not dumped).
5. **Shared game-mode gate.** `mission_help_table_do` and `mission_help_clear` both return immediately when
   `0x007205d0` is true (`0x0153b520` in 10..13) — CONFIRMED; the meaning of that range is OPEN. They share the worker
   `0x007fd290`: `mission_help_table_do`'s default arg 10 = true runs the same clear as `mission_help_clear(arg 6)`
   before posting.
6. **Help-text table `0x026e7e68`** (12-byte records {name hash, text, duration}, accessed through `0x00604740` /
   `0x00604500` / `0x00604520`) backs both `mission_help_table_do` and the vint-dialog opener `0x00a58e00`. Only the
   former null-checks the table pointer.
7. **Resolver-bit census additions** (descriptor row `0x02cc9900[object +0x34]`): `+0x8` bit `0x2` (`0x0062a1f0`,
   "path"), `+0xa` bit `0x2` (`0x00a27e20`, the generic chain's first-tier kind), `+0xa` bit `0x20` (`0x005eab60`,
   "script group"), `+0xa` bit `0x80` (collectible-table entries, E.3), `+0xb` bit `0x2` (`0x004f6710`, pause-map
   bookmark object). All CONFIRMED as bit tests; kind names are HIGH CONFIDENCE from caller names only.
8. **Hook-name registry `0x00a1fcc0`** (shared by all five `on_*` here): a fixed pool of **350** (`0x15e`) 12-byte
   entries at `0x026e83e8` {id, name hash, string copy}, plus a reference-count array at `0x026e7e70`. A name whose
   hash is already present gets that entry's id and a reference-count increment; there is **no string compare**, so
   two callback names with equal hashes would share one hook (HYPOTHESIS-level risk). A full pool returns -1, so the
   registration silently does nothing. When the string-copy allocation (`0x00e0e1e0` → `0x00e0eec0`) returns null,
   the function returns **0** rather than -1 — the generation counter `0x026e7e64` has already been incremented — so
   the slot holds 0, which every caller treats as "registered" (`slot != -1`). CONFIRMED for the control flow; the
   effect of a stored id 0 on later dispatch is OPEN.

## H. Crash-shaped and logic defects (summary)

**Crash-shaped:**

| id | function | site | trigger | status |
|---|---|---|---|---|
| H1 | `object_indicator_add_script_group_do` | `0x00a59120`/`0x00a59122` | group name nil, misspelled, or the group is dead → read `[0x3c]` | CONFIRMED, trivially reachable from script |
| H2 | `minimap_icon_add_script_group_do` | `0x00a542ef`/`0x00a542f3` | arg 2 nil or non-string → read `[0]` | CONFIRMED, trivially reachable from script |
| H3 | `minimap_icon_add_script_group_do`, `minimap_icon_remove_script_group_do` (and siblings `minimap_icon_add_do` §10.5, `0x00a54430`) | table calls via `0x0130c9a8` / `0x0130cb98` | flags argument 0 or ≥ 4 (or negative) → indirect call to null, `0xffffffff`, `0xfffffffe` or unrelated data | CONFIRMED (table contents dumped) |
| H4 | `pause_map_set_gps` | `0x007dbc4e`..`0x007dbc56` | blip kind 3 selected (or stale), co-op partner gone, route succeeds → read `[partner+0x8]` | CONFIRMED code path; reachability HYPOTHESIS |
| H5 | `pause_map_add_bookmark` (add) | `0x007dfa44`..`0x007dfa4e` | marker spawn `0x00456e30` fails (pool full) → read `[0xc]` | CONFIRMED code path |
| H6 | `pause_map_add_bookmark` (remove) | `0x007df925`..`0x007df959` → `0x00853ea0` → `0x004571e0` | selected bookmark's object already gone → read `[0x32]` | CONFIRMED code path; reachability HYPOTHESIS |
| H7 | `pause_menu_change_control_scheme` | `0x005c12c0`/`0x005c13c0` clamp | selected scheme set has count 0 → index -1 → record before array base | CONFIRMED arithmetic; reachability OPEN |
| H8 | `minimap_icon_add_script_group_do` | `0x00802f60` | arg 3 non-nil but not a string → `__stricmp(null)` | HIGH CONFIDENCE (CRT body not dumped) |

**Logic/behaviour traps (not crashes):**

- L1 `on_mount`: `"#PLAYER#"` and every object accepted by `0x00a27e20` are silently ignored (C.2).
- L2 `npc_weapon_pickup_override`: the boolean is inverted relative to the stored flag (true = allowed).
- L3 `parking_spot_set_max`: values above 18 and negative values are silently dropped; 18 is the ceiling.
- L4 `npc_detection_enable`: silently ignored while byte `0x014ff6b8` is set.
- L5 `npc_brute_always_throws`: default is **true**; silently ignored for characters failing the brute test.
- L6 `open_vint_dialog_check_done`: reads -2 forever if the dialog failed to open, and also before any dialog.
- L7 `mission_help_table_do`: an unknown key with the default duration shows for 10 s (table fallback), not 7 s.
- L8 `object_indicator_remove_script_group_do`: removes **all** indicators on each member object, including ones
  added by other calls.
- L9 `mission_help_clear` on a non-host only clears locally (the broadcast bit is host-gated).
- L10 Hook registry: a full pool is a silent no-op, and an allocation failure stores a phantom id 0 (G.8).

## I. Follow-up run inventory

| run | items | used for |
|---|---|---|
| `ptrs count:16` | `0x0130c9a8`, `0x0130cb98` | D.3, D.4, H3 |
| `xref` | `0x007dfae0`, `0x007e18e0`, `0x0130cba8`, `0x0229a47c`, `0x0229a2f4`, `0x0130c9a8`, `0x0130cb98` | registrars, A.3, A.4, D.3/D.4, F.3 |
| `func depth:0 maxinsn:400` | `0x00a27e20`, `0x00807140`, `0x00a3b810`, `0x004571e0`, `0x00457730`, `0x00a58e00`, `0x00e0e1e0`, `0x007e1110`, `0x007dfae0`, `0x007e18e0`, `0x008ae480`, `0x008837a0`, `0x007d19e0`, `0x007d9100`, `0x0091b7f0`, `0x00919e60`, `0x00a1fc60` | C.2, D.3/D.4, A.1, A.3, A.4, F.1, F.3, G.8 |

`0x008ae480`/`0x008837a0` were dumped only to confirm that they are the same gate pair as spec §7 (not re-described).
`0x0091b7f0` (a reader of the parking maximum, 64 KB of listing) was not analysed beyond confirming the read.
`0x00919e60` (the parking-max getter) is called by `0x00907700`, `0x00907750` and code at `0x0090741b`/`0x0090747b`.

## J. OPEN

- A.1: whether either control-scheme set can be empty while the pause menu runs (H7); what `0x005bec10`/`0x005bd4e0`
  do exactly; whether sets A/B really are on-foot/vehicle.
- A.3: who writes the selected-blip record containing `0x0229a47c`, and whether kind 3 is cleared on deselect (H4
  reachability); the meaning of call-object field `+0x14` (`0x0229a364`) in both pause-map Lua callbacks; what
  `0x005812d0` tests.
- A.4: how a bookmark blip can outlive its marker object (H6 reachability); the meaning of spawn kind `0x29`; blip
  type `9` and the `0x62+i` id scheme versus the 10-wide remove range.
- B.1: identity of the `object +0xf0 → +0x48` bit-0 "brute" test. B.2: meaning and writers of `0x014ff6b8` (only a
  0-writer was found). B.3: what `0x00a55830` is. B.4: what the `0x03171c10` name table holds (filled by `0x004bf8d0`)
  and which code reads `+0x1ce0`/`+0x1cd4`.
- C.2: which concrete object kinds carry descriptor `+0xa` bit `0x2` versus `+0xb` bit `0x4`, and therefore whether
  `on_mount` ever fires for ordinary NPCs or players. C.5: what else uses the global hook record `0x026e9528`, and
  which code fires slot `0xc`.
- D: which member kinds sit on group lists `+0x3c`/`+0x40`/`+0x44`/`+0x48`; the meaning of the two integers to
  `0x008f96f0` (object-indicator arg 2/3); entries `[1]`/`[2]` of both minimap tables (`0x00a3b7e0`, `0x00807070`,
  `0x008070b0`, `0x008070d0`) were not dumped.
- E.1: the meaning of record fields set from arg 3/arg 7 and arg 9; what `0x007e2a00(8, …)` posts; the meaning of
  `0x0153b520` ∈ 10..13. E.3: the writer of the collectible table `0x0149fdd0`, the meaning of object `+0xa8` bit
  `0x10` and of the entry byte, and whether any writer can exceed 20 entries per group.
- F.3: the Lua name of `0x00a58e00`, and the value `0x00a58d40` writes (presumed option index).
- G.8: what dispatch does with a stored hook id of 0.

## K. Direct-answer table

| # | name | registrar | handler | status |
|---|---|---|---|---|
| 1 | `pause_menu_change_control_scheme` | UI sub `0x007e18e0` (under UI `0x008430f0`) | `0x007e10d0` | resolved — scheme set A/B by `0x0229a558`; empty-set clamp H7 |
| 2 | `pause_map_zoom` | UI sub `0x007dfae0` (under UI `0x008430f0`) | `0x007dbcc0` | resolved — ±20% step, clamped, pan recomputed |
| 3 | `pause_map_set_gps` | UI sub `0x007dfae0` | `0x007dba10` | resolved — 3 modes; **null deref H4** |
| 4 | `pause_map_add_bookmark` | UI sub `0x007dfae0` | `0x007df8a0` | resolved — toggle, max 5; **null derefs H5, H6** |
| 5 | `path_name_is_path` | gameplay `0x00a20840` | `0x00a5a790` | resolved — boolean, kind `+0x8`/`0x2` |
| 6 | `party_use_melee_weapons_only` | gameplay | `0x00a59490` | resolved — replicated player flag, local + co-op partner |
| 7 | `parking_spot_set_max` | gameplay | `0x00a59b20` | resolved — `0x01308da8`, accepts 0..18 only |
| 8 | `parking_spot_reset_max` | gameplay | `0x00a59360` | resolved — writes 18 |
| 9 | `override_npc_run_and_cower_run` | gameplay | `0x00a58ca0` | resolved — two name-table indices, -1 = none, not replicated |
| 10 | `open_vint_dialog_check_done` | gameplay | `0x00a58f10` | resolved — returns `0x0130cba8` (-2 = pending) |
| 11 | `on_vehicle_debris_flow_recycled` | gameplay | `0x00a587c0` | resolved — vehicle hook slot `0xb` |
| 12 | `on_mount` | gameplay | `0x00a580f0` | resolved — hook slot `0x12` at `+0xf8`; inverted probe L1 |
| 13 | `on_m21_player_choice` | gameplay | `0x00a56f80` | resolved — global hook record `0x026e9528` slot `0xc` |
| 14 | `on_hit_ped` | gameplay | `0x00a57fd0` | resolved — character slot `0x15` / vehicle slot `5` |
| 15 | `on_dismount` | gameplay | `0x00a57e50` | resolved — hook slot `0x13` at `+0xf8` |
| 16 | `object_indicator_remove_script_group_do` | gameplay | `0x00a59220` | resolved — removes all indicators per member (L8) |
| 17 | `object_indicator_add_script_group_do` | gameplay | `0x00a59080` | resolved — **null deref H1** |
| 18 | `npc_weapon_pickup_override` | gameplay | `0x00a557d0` | resolved — inverted, replicated AI flag |
| 19 | `npc_detection_enable` | gameplay | `0x00a55300` | resolved — replicated NPC flag, gated by `0x014ff6b8` |
| 20 | `npc_brute_always_throws` | gameplay | `0x00a55190` | resolved — replicated flag, brute-only, default true |
| 21 | `mission_help_table_do` | gameplay | `0x00a53ba0` | resolved — 10 args; several record-field meanings OPEN |
| 22 | `mission_help_clear` | gameplay | `0x00a52ab0` | resolved — flags default 3 (local + host broadcast) |
| 23 | `mission_collectible_collected` | gameplay | `0x00a523b0` | resolved structurally — semantics HYPOTHESIS |
| 24 | `minimap_icon_remove_script_group_do` | gameplay | `0x00a544f0` | resolved — **unbounded table index H3** |
| 25 | `minimap_icon_add_script_group_do` | gameplay | `0x00a54220` | resolved — **null deref H2, unbounded table index H3, H8** |

## Cleanroom check

This file uses plain hex addresses only, with no Ghidra auto-names and no decompiler variable names. The project's
forbidden-token regex (auto-generated function, data and label names, and decompiler temporaries, locals,
parameters and register-input names) was run over this file before hand-off: **0 hits** (see the hand-off note for the
command). No decompiled pseudocode is reproduced; the instruction excerpts quoted are single listing lines, cited for
location only.
