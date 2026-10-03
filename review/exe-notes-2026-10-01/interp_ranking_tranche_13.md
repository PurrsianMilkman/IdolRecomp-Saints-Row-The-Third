# Ranking tranche 13 — 25 previously-unspecced Lua-bound names (Team A, 2026-10-02)

Job definition: `D:\Project Crreish\TEAM A\ghidra\jobs\ranking\tranche-13.json` (names 251-275 of the 554 unspecced
names, Team B call-count order). Run locally, read-only, on a private copy of the Ghidra project (`tools\gp_t13`,
deleted afterwards), with the job's own arguments: `CrreishDump.java <out>/lua lua depth:1 maxfuncs:15 maxinsn:500
<25 names>`. **All 25 names resolved.** Every name string occurs exactly once in `.rdata`. For 24 names the handler is
the code pointer stored in the slot right after the name (insn offset +1) — CONFIRMED — disassembly (dump `index.txt`).
The 25th, `autil_hud_mayhem_init`, is registered by a one-name registrar that pushes the function pointer **before**
the name (`0x0067d66b`), so the dumper's offset-based guess pointed at the `lua_setfield` primitive; the real handler
`0x0067d520` was read from the range run (section H). Follow-up runs on the same private copy, cited by name below:

- "follow-up dump": depth-0 `func` run on 34 callee addresses (listed in section H);
- "xref run": `xref` mode on the five registrars `0x007c5630 0x0083db20 0x0083ae10 0x00bcd240 0x0067d660`;
- "range run": `range` mode on those five registrars' bodies;
- "second follow-up dump": depth-1 `func` run on `0x0067d520`;
- "third follow-up dump": depth-0 `func` run on `0x00e1f2f0 0x00e0ce20 0x009a5520 0x0067d360`;
- "second range run": `range` mode on `0x00a216d0-0x00a21740` (gameplay-registrar neighbours of
  `character_fake_revival_end`).

Call counts are Team B's (`D:\Crreish-sync\for-team-b\team-b\tools\lua_reconciliation_called_and_registered_1181.tsv`):
**every name in this tranche is 2 call sites**; distinct scripts are 2 for `flee`, the four `dialog_*` names,
`cutscene_check_exiting`, `customization_swap_player_rig`, `cellphone_dial`, `camera_script_enable`,
`camera_script_disable` and `autil_hud_mayhem_init`, and 1 for the other 14. Team B tags 8 names `ui` (the four
`dialog_*`, `cinema_editor_create_camera_zone`, `cellphone_dial`, `cell_camera_enable`, `autil_hud_mayhem_init`) and
the other 17 `gameplay`; section A shows the registrars agree exactly (8 under the UI registrar, 17 in the gameplay
registrar).

Labels: **CONFIRMED** = read in the listing or decompile of a dump made for this note; **HIGH CONFIDENCE** = follows
from a dumped call or reference, but the callee body was not dumped or a meaning is inferred from strong usage;
**HYPOTHESIS** = plausible, not settled; **OPEN** = not settled (collected in section J).

**Shared conventions, cited not re-described:** every entry opens with the `lua_gettop` prologue (`0x00dfde50`) and
reads arguments with negative stack indices counted from the bottom (`-N` = arg 1, `1-N` = arg 2, …). Primitives as in
tranche 09's and tranche 11's front matter: `0x00dfe210` `lua_tolstring` (non-string reads as null), `0x00dfe1e0`
`lua_toboolean`, `0x00dfe040` `lua_type` (0 = nil), `0x00dfe160` `lua_tonumber` (non-number reads as 0), `0x00ea2596`
truncating float-to-int, `0x00dfe3a0` `lua_pushnumber`, `0x00dfe590` `lua_pushboolean`. "Nil-gated optional argument"
means the standard idiom: if arguments remain and the slot's type is not nil it is read, otherwise the default is used;
either way the slot is consumed. Resolvers: `0x00a281a0` generic character chain (`#PLAYER#`, then `0x00a28150`),
`0x00a28150` (`#FOLLOWER#` via `0x00a26010`/`0x00a26110`, then the character resolver `0x005e4dd0` with a liveness check
and virtual slot `+0x70`) — both per `spec-lua-api-behaviour.md`'s name-resolution front matter. `0x00853b10` liveness
guard (non-zero = dead); `0x009da4e0` local player (global `0x0262edfc`); `0x009df3d0` remote co-op player
(spec §14.31 correction, tranche 04); `0x0087ba20` session; "host" = session exists and its `+0x5c` equals `+0x58`
(spec §8.27 `game_get_is_host`). Kind-flag tests through `[0x02cc9900 + 4·(object byte +0x34)]` and the bits named in
tranche 11's front matter (`+6` bit `0x80` = vehicle, `+0xa` bit `0x02` = player, `+0xb` bit `0x01` = mesh mover).
Handle resolution: `0x00458230` on `0x024433a8` with key `0x031d152c`, then the `+0x33` bit `0x10` rejection
(tranche 11).

**New small generic helper, CONFIRMED (follow-up dump):** `0x00853b30(object, n)` tests bit `n & 7` of byte
`+6 + (n >> 3)` of the object's kind-flag record, and returns 0 for a null object. So `n = 0x21` is `+0xa` bit `0x02`
(player), `n = 7` is `+6` bit `0x80` (vehicle), `n = 5` is `+6` bit `0x20`. Every kind-bit test in tranche 11's table
can be written as a call of this helper.

**Lua callback coroutine mechanism** (`0x00e0ca80`/`0x00e0c720`/`0x00e0cba0`/`0x00e0c610`/`0x00e0c650`, call-object
argument pushers, the 256-record thread table, the 16-deep "current thread" stack at `0x02a44d18`/depth `0x02a44d10`)
is exactly as tranche 09's front matter states. Two additions, CONFIRMED (follow-up dumps): `0x00e0ce20` is a
float-argument pusher on a call object (ECX = call object; it pushes the float as a Lua number and increments the
argument count at `+0x1c`); `0x00e0ceb0()` returns the **top of the current-thread stack** (the running script's
thread record), or 0 when the depth is 0 or above 16.

**Script-group records (new, CONFIRMED — third follow-up dump and second follow-up dump):** `0x00e1f2f0(name)` hashes
the name with the case-insensitive CRC `0x00d9e740` and walks a circular list from `0x02a4d17c` for the record whose
dword `+0x57c` equals the hash, returning it or null. `0x00e1f330(id)` maps a 16-bit id below 64 to the record at
`0x02a4d170 + id·0x5a8` when that record's `+0x580` equals the id, else 0. So a record's `+0x580` is its own handle, and
a thread record's `+0x14` (tranche 09's "inherited from the parent", tranche 11's "cleared to 0") is this
**script-group handle**. HYPOTHESIS: these are the loaded-script ("cell_phone", HUD documents, …) records.

---

## A. Registrars — where the 25 names live

Six registrars. CONFIRMED — `index.txt`, xref run, range run:

| registrar | reached from | names in this tranche |
|---|---|---|
| gameplay `0x00a20840` | (established) | 17: `flee`, `flashpoint_mission_status`, `debris_flow_set_inactive`, `cutscene_was_skipped`, `cutscene_check_exiting`, `customization_swap_player_rig`, `character_take_human_shield_check_done`, `character_set_cannot_exit_rc_vehicle`, `character_hidden`, `character_fake_revival_end`, `cellphone_animate_stop_do`, `camera_script_enable`, `camera_script_disable`, `boss_battle_mars_killbane_active`, `boss_battle_kb_enable`, `audio_suppress_ambient_player_lines`, `ambient_gang_spawn_enable` |
| `0x007c5630` (dialog sub-registrar, 12 names) | UI registrar `0x008430f0`, call at `0x0084313e` | 4: `dialog_box_set_current_option`, `dialog_close_finished`, `dialog_option_accept_kbd_input`, `dialog_option_end_kbd_input` |
| `0x0083db20` (cellphone, 3 names) | UI `0x008430f0`, call at `0x00843204` | 1: `cellphone_dial` |
| `0x0083ae10` (cell camera, 2 names) | UI `0x008430f0`, call at `0x008431f8` | 1: `cell_camera_enable` |
| `0x00bcd240` (cinema editor, 2 names) | UI `0x008430f0`, call at `0x00843210` | 1: `cinema_editor_create_camera_zone` |
| `0x0067d660` (one name) | UI `0x008430f0`, call at `0x008430f6` | 1: `autil_hud_mayhem_init` |

- The four table-driven sub-registrars build a local array of `{name, function}` pairs and loop over
  `lua_pushcclosure` (`0x00dfe4f0`) then `lua_setfield` into globals (`0x00dfe830`, index `-10002`). Loop counts read
  from the range run: dialog **12** (`0x007c5706` `MOV EBX,0xc`), cellphone **3**, cell camera **2**, cinema editor **2**.
  The full tables, for a binding layer: dialog = `dialog_box_set_result` `0x007c2d90`,
  `dialog_box_set_current_option` `0x007c2e40`, `dialog_box_create_internal` `0x007c5130`,
  `dialog_open_pause_display` `0x007c2ee0`, `dialog_close_pause_display` `0x007c2f50`, `dialog_pause_unpause`
  `0x007c2eb0`, `dialog_box_force_close` `0x007c4960`, `dialog_box_disconnect` `0x007c2f80`,
  `dialog_box_force_close_all` `0x007c4b00`, `dialog_close_finished` `0x007c1be0`, `dialog_option_accept_kbd_input`
  `0x007c2fb0`, `dialog_option_end_kbd_input` `0x007c5440`; cellphone = `cellphone_dial` `0x0083d930`,
  `cellphone_end_call` `0x0083d130`, `cellphone_choose_vehicle` `0x0083d5e0`; cell camera = `cell_camera_is_enabled`
  `0x0083adb0`, `cell_camera_enable` `0x0083ade0`; cinema editor = `cinema_editor_create_camera_zone` `0x00bcc1d0`,
  `cinema_editor_save_clip` `0x00bcbd70`. CONFIRMED — range run.
- The dumper's second "use" of `cellphone_dial` and `cinema_editor_create_camera_zone` (`*_1.txt`, rooted at
  `0x00dfe830`) is the registrar loop reading the **first** row of its table, as in tranche 09 — not a second handler.
  HIGH CONFIDENCE.
- The string "flee" has two further uses outside any Lua registrar (`0x00507c62`, between calls to `0x00dab9e0` and
  `0x00dac1d0`; `0x00b2c6b7`, beside a `_stricmp`). Those are unrelated uses of the same word (a named-object
  initializer and a text parser); `flee_0.txt`/`flee_2.txt` are not Lua bindings. HIGH CONFIDENCE.

---

## B. Characters and AI — 6 functions

### B.1 `flee` → `0x00a49b30`

**Arguments:** 1 string = the actor who flees (resolved by `0x00a28150`, i.e. `#FOLLOWER#` or a character name — **not**
the `#PLAYER#` chain); 2 nil-gated string = what to flee from, default the literal `"#PLAYER1#"` (`0x01147698`),
resolved by `0x00a281a0`; 3 nil-gated boolean, default false; 4 nil-gated boolean, default false.

**Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — disassembly):** if both resolve, it posts an AI order through `0x004f5dd0` with: kind `0x30`; the
fleeing actor's 64-bit handle (object `+8`/`+0xc`) twice (stored at record `+0x10` and `+0x18`; the second copy is the
one resolved as the recipient — HYPOTHESIS: the first is the issuer); a duration argument of 0; two byte constants
`0x2d` and `0xb2`; and a 32-byte payload = {the flee-from target's handle, 8 zero bytes, dword 0, flag dword, 8 zero
bytes}. Flag dword = 2, or 3 when arg 3 is true, then OR `0x10` when arg 4 is true.

`0x004f5dd0` (dumped at depth 1, CONFIRMED): resolves the recipient handle; refuses when it is not found, flagged dead
(`+0x33` bit `0x10`), lacks kind bit `+0xa`/`0x01`, fails liveness, or **is a player** (`0x00853b30(obj, 0x21)`). A
negative duration would be replaced by a random 250..750 (`0x00dab660(0xfa, 0x2ee)`; not reached here, the duration is
0). It takes the head of a free-list pool at `0x01361998` (**null-checked: an empty pool silently drops the order**),
fills the kind byte, both handles, two timers through `0x00d9e140` (`+0x20` with 0, `+0x24` with the duration), the two
bytes and the payload (setting bit `0x02` of `+0x2a` when a payload is given), and appends the record to the
recipient's circular order list at object `+0x1c50`.

**Notes:** the meaning of the flag bits (1 and `0x10`) and of the constants `0x2d`/`0xb2` is OPEN (HYPOTHESIS:
"flee on foot/in vehicle" style options and a priority/reason code). No null crash: both resolver results and the pool
are checked. Players can never be made to flee (CONFIRMED).

### B.2 `character_take_human_shield_check_done` → `0x00a46600`

**Arguments:** 1 string = character (taker), 2 string = expected hostage; both resolved by `0x00a281a0`.

**Return — variable count, CONFIRMED (listing):**
- both resolved, character's current hostage == arg 2 → **2 values: true, false**;
- both resolved, hostage differs → **1 value: false**;
- character resolved, arg 2 not resolved → **false**, then the hostage compare runs against null: a character with **no**
  hostage matches null → **3 values: false, true, false**; otherwise 2 values: false, false;
- character **not** resolved → crash (below).

The C function returns 1 + the number of extra pushes. A script reading only the first value sees "true = the taker
holds the expected hostage", which is the intended meaning (HIGH CONFIDENCE from the name).

**Hostage getter `0x009aa130(character)`** (follow-up dump, CONFIRMED): when the character is **not** locally owned
(`0x008addb0` false) and byte `+0x18dd` bit `0x02` is clear, it resolves the handle at `+0x1898`/`+0x189c` through
`0x004d7f30`; otherwise it resolves the handle at `+0x1890`/`+0x1894` (kind bit `+0xa`/`0x01`, alive), or 0. So a
character carries two hostage handles, one used for remote (network) characters and one for local ones.

**Crash — CONFIRMED (listing):** the root does not stop when the character fails to resolve: it pushes false and then
calls `0x009aa130(null)`. `0x008addb0` returns **true for a null object** (tranche 11 front matter, CONFIRMED there), so
the getter goes straight to reading `null + 0x1890` at `0x009aa16d`. **Null read whenever arg 1 does not name a live
character** (misspelt, despawned, nil). Reachable from script data alone.

### B.3 `character_set_cannot_exit_rc_vehicle` → `0x00a42140`

**Arguments:** 1 string (character, `0x00a281a0`); 2 boolean (read unconditionally; missing = false).
**Return:** 0 values. **Body:** if the character resolves, `0x009493e0(flag)` with the character in ECX. CONFIRMED.

`0x009493e0` (dumped at depth 1, CONFIRMED) is the same double-gate (`0x008ae480`, then `0x008837a0`, variant 1)
record-and-replicate setter shape as `spec-lua-api-behaviour.md` §7.2: when both gates are false and the owner
(`0x008ae3a0`) exists, it builds an opcode-`0x46` message tagged `"human"` / `"human_force_flagscannot_exit_rc_vehicles"`
(hashed by `0x00d9e7e0`), the character's handle and the boolean, sent to that owner, **without** changing the local
flag; otherwise it writes the boolean into **bit 9 (`0x200`) of the dword at character `+0x1c9c`**. A null `this` is
tolerated. Who reads the bit is OPEN (HYPOTHESIS: the RC-vehicle exit check).

### B.4 `character_hidden` → `0x00a41540`

**Arguments:** 1 string (`0x00a281a0`). **Return:** exactly 1 boolean: bit 0 of byte `+0x3b` of the resolved object
(`0x008cc4d0`); **true when the name does not resolve.** CONFIRMED.

Note: an unknown or despawned name reads as "hidden" (CONFIRMED). The same `+0x3b` bit 0 is the "skip hidden object"
test in `debris_flow_set_inactive`'s worker (F.2), which supports the reading "object is hidden" (HIGH CONFIDENCE).

### B.5 `character_fake_revival_end` → `0x00a41fc0`

**Arguments:** 1 string (`0x00a281a0`). **Return:** 0 values.

**Body:** clears bit 16 (`0x10000`) of the dword at character `+0xe4`. CONFIRMED.

**Crash — CONFIRMED (listing):** the resolver result is used with **no null check**: `AND dword ptr [EAX+0xe4]` at
`0x00a41fde` writes through null when the name does not resolve. **Null write.**

**Sibling, not in this tranche but sharing the defect** (second range run + follow-up dump): the registrar row just
before it is `character_fake_revival_start` → `0x00a41f80`, which calls `0x009a5520(character, 1, 0)` and then sets
the same bit (`OR dword ptr [ESI+0xe4],0x10000` at `0x00a41faa`). It also skips the null check; `0x009a5520` reads
`character + 0xcd2` as its first access (`0x009a553f`), so an unresolved name crashes there first. CONFIRMED.
`0x009a5520` itself (partly read) requires `+0xcd2` ∉ {0, 6} and `+0xcce` == 8 before doing anything (HYPOTHESIS: a
"downed" state check before a revive). So `+0xe4` bit 16 = "fake revival in progress" (HIGH CONFIDENCE from the pair).

### B.6 `customization_swap_player_rig` → `0x00a43cf0`

**Arguments:** 1 string = rig name; 2 string = second name (see below); 3 nil-gated number = player mask, default 3
(bit 0 = local player, bit 1 = remote co-op player).

**Return:** 0 values.

**Body (CONFIRMED — listing):** bit 0 → `0x009e3400(local player, arg 1, arg 2, 0)`; bit 1 → if `0x009df3d0()` is
non-null, `0x009e3400(remote player, arg 1, arg 2, 0)`.

`0x009e3400(player, name, name2, noSend)` (dumped at depth 1, CONFIRMED):
1. `noSend` is 0 on both calls here, so it **always** builds and sends a network message (opcode `0x41`, sub-id 9,
   the player's 16-bit net id from `0x008add70` — only when the player is non-null — and both strings via `0x0086f530`).
2. Rig lookup: `0x004bc7d0(name, [player +0xf4] +0xd8)` and, if that fails, `0x004bc7d0(name, -1)`. `0x004bc7d0`
   (follow-up dump) returns the 0x60-byte record at `0x035198f0 + i·0x60` for the index `0x004d0d50` finds, or null.
3. `0x004bca80([player +0xd24] +0x14, rig)` (follow-up dump): when that index is valid in the table `0x0344c924`
   (stride `0x3a0`, count `0x0344c91a`, self-index at `+0x50`), stores the rig at `+0x14` and copies **rig `+0x50`**
   into `+0x39c`.
4. `[[player +0xd24] +0x10] +0x30` = `0x004b16c0(name2)` (follow-up dump): a string-table lookup (`0x00db0c50`) into
   the 0x18-byte records at `0x02ef1638` (count `0x03171bec`), or null for a null/unknown name.

**Crash shapes (CONFIRMED shape):**
- **No local player** with mask bit 0 set: `0x009e3400` reads `player +0xf4` and `+0xd24` with no null check.
- **Unknown rig name** (both lookups null) with a valid `+0xd24 → +0x14` index: `0x004bca80` reads `null + 0x50`.
- Name 2 unknown is tolerated (null stored); whether a later reader of `+0x30` tolerates null is OPEN.

**Logic (CONFIRMED structure; consequence HYPOTHESIS):** with mask 3 in co-op, one call sends **two** messages (one
per player) and also applies both locally. There is no host or ownership gate, so if both machines run the script each
player's rig is swapped by the local call and again by the peer's message. Whether the receive side re-applies or
de-duplicates is OPEN (receiver not traced).

---

## C. Cutscene and camera — 5 functions

### C.1 `cutscene_was_skipped` → `0x00a42ea0`

**Arguments:** none read. **Return:** 1 boolean = bit 6 (`0x40`) of the cutscene flag byte `0x0153b524`
(`0x00720530`). CONFIRMED.

**Writer (follow-up dump, CONFIRMED):** the only setter is `0x0072a160`, the cutscene **stop** routine (called from the
state-13 case of the state machine `0x0072d660` at `0x0072db51` and from `0x0072b340`). It acts only when the state
`0x0153b520` is 1..13; it then sets the state to **14**, re-arms the two 1000-unit timers `0x0153b55c`/`0x0153b560`
(`0x00dad740`), and — **only when bit 7 of the manager's byte `+0x35da` is set** — walks the shots after the current
one (`0x0153ba58 + 1` .. manager `+0x34e0`, stride `0x700`, `0x00721640` each) and sets bit `0x40`
(`0x0072a221`). HIGH CONFIDENCE that manager `+0x35da` bit 7 is "skip requested". The bit is cleared only when a new
cutscene starts (`0x00725df0` rewrites the whole byte) or a chained one is created (`0x00725670` keeps only bit 7) —
`interp_nnlt.md` §1.4. So the answer **stays true after the skipped cutscene has ended**, until the next one starts
(CONFIRMED by the writer set). State numbering per `interp_nnlt.md` §2.1.

### C.2 `cutscene_check_exiting` → `0x00a42e70`

**Arguments:** none read. **Return:** 1 boolean = cutscene state `0x0153b520` **> 12** (`0x007207a0`). CONFIRMED.

By `interp_nnlt.md` §2.1 that is states 13 ("CS_STATE_STOP" case) through 19 (final teardown back to 0): the stop
case, the state 14 that C.1's stop routine writes, the chain state 15, and the stopped/final-streaming/teardown states
0x10..0x13. State 0 (no cutscene) reads false. HIGH CONFIDENCE for the phase names.

### C.3 `camera_script_enable` → `0x00a448d0` and C.4 `camera_script_disable` → `0x00a447b0`

**Arguments:** none read. **Return:** 0 values (`XOR EAX,EAX` at the end; the decompile shows `void`). CONFIRMED.

**Body (identical apart from the value, CONFIRMED — listings):** `0x0056b5f0(1)` / `0x0056b5f0(0)` writes the byte
global **`0x013c9382`** (follow-up dump: a one-line setter — the write target is `0x013c9382`, not `0x0056b5f0`).
Then, when the network module byte `0x024d4461` is set (`interp_gdhw.md`: "network module up"), it sends message
opcode `0x43`, sub-id `0x10`, with the same boolean (`0x004d46e0`), to the session (`0x0087ba20`, `0x0086f1b0`,
`0x0086eb20`).

`0x013c9382` is read by `0x0056af80`, `0x00572fa0`, `0x00572fe0` and `0x005745c0` (not dumped; HYPOTHESIS: the camera
system's "script camera owns the view" test). There is **no host check**: either machine broadcasts. OPEN: what the
receiver does with sub-id `0x10`.

### C.5 `cinema_editor_create_camera_zone` → `0x00bcc1d0`

**Arguments:** 1 number = a time in seconds (kept as a float); 2 nil-gated number = an existing zone handle to copy
from, default -1.

**Return:** success → **1 number** = the new zone handle, pushed as **unsigned** (a negative int gets 2³² added, the
double at `0x0113e6c8`); failure → **0 values** (not nil). CONFIRMED.

**Body (CONFIRMED — listings of `0x00bc7520` and `0x00bc7480`):** `0x00bc7520(seconds, template)` converts seconds × 1000
(the double at `0x012a2d90`) to milliseconds with the round-half-away helper `0x00dad900`, then calls `0x00bc7480`
with the milliseconds on the stack and the **template handle in EAX** (hidden register; the decompile drops it).
`0x00bc7480` takes a zone from the pool object `0x02944498` (`0x00bc7110`; null → failure), initializes it
(`0x00bc73e0`), and when the template handle is not -1 and the slot `0x029444ac[handle & 0xff]` (stride `0x90`) holds
that exact handle, copies **0x88 bytes** (34 dwords) from the template. It then writes the new handle = (slot index &
0xff) | (generation counter `0x01311e68` low 16 bits << 8), stores the milliseconds at `+4`, and inserts the zone with
`0x00bc6fb0`.

**Shapes:** the template's slot index (`handle & 0xff`) is **not bounded** by the pool's size before its first dword is
read for the compare — an out-of-bounds **read** when a script passes a stale or foreign handle and the pool has fewer
than 256 slots (CONFIRMED unchecked; pool size OPEN; read-only, the compare then almost surely fails). The copy covers
`+0..+0x87` of a 0x90-byte slot, so the template's own handle and time are copied and then overwritten (CONFIRMED).

---

## D. Dialog boxes — 4 functions, sub-registrar `0x007c5630`

**Dialog pool (CONFIRMED — listings):** four dialog records at `0x02282d40`, stride `0x184`. A **dialog handle** is
validated as: non-zero, low 16 bits < 4 (slot index), and the record's dword `+0x12c` equals the whole handle
(a generation check). This is the same `+0x12c` handle tranche 09 B.12 stores from `0x007c3e60`. All three handle-taking
functions below silently do nothing for an invalid handle.

**Options:** `0x007c1b90(n)` (ECX = dialog) walks the dialog's circular child list (head `+0x110`, next at child `+8`)
counting only children whose virtual `+0xc` returns **3**, and returns the n-th such child, or null. CONFIRMED. So an
"option index" counts type-3 children only.

### D.1 `dialog_box_set_current_option` → `0x007c2e40`

**Arguments:** 1 number = dialog handle; 2 number = option index (both truncated). **Return:** 0 values.
**Body:** for a valid handle, writes the option index into dialog `+0x11c`. **No range check** against the number of
options (CONFIRMED). What reads `+0x11c` is OPEN; an out-of-range index is harmless only if every reader bounds it.

### D.2 `dialog_close_finished` → `0x007c1be0`

**Arguments:** none read. **Return:** 0 values. **Body:** clears the byte **`0x02282d39`**. CONFIRMED.

**What the byte does (follow-up dump of its only other user, `0x007c5740`, CONFIRMED):** `0x007c5740` is the dialog
manager pass. When `0x02282d39` is set it **returns at once and does nothing**. Otherwise it closes finished dialogs in
the two display slots (`0x02282d28`, `0x02282d2c`, via `0x007c54d0`), promotes the highest-priority queued dialog from
the queue `0x02282d10` (priority at `+0x130`, slot at `+0x180`; `0x007c29c0` closes the slot's current one,
`0x007c1740` shows the new one), runs `0x007c3050` on each closed dialog and returns it to the free ring `0x02282d14`;
**when the pass closed at least one dialog and showed none, it sets `0x02282d39` = 1.** So after a dialog closes the
manager **freezes** — no further dialog is shown or closed — until a script calls `dialog_close_finished`
(HYPOTHESIS: the UI script calls it when its close animation ends).

**Logic hazard (CONFIRMED mechanism):** if the UI script that owns the close animation never calls this function
(script error, the script group unloaded, a skipped callback), **every later dialog stays queued forever** — the
dialog system stalls with no timeout. Who calls `0x007c5740` (per frame?) is OPEN.

### D.3 `dialog_option_accept_kbd_input` → `0x007c2fb0`

**Arguments:** 1 number = dialog handle; 2 number = option index. **Return:** 0 values.

**Body (CONFIRMED — listing):** for a valid handle, the option (`0x007c1b90`) must exist, be type 3, have byte `+0x210`
set (keyboard-input capable — HYPOTHESIS) and byte `+0x228` **clear** (not already editing); then
`0x007c2940(option index)` with ECX = option.

`0x007c2940` (follow-up dump, CONFIRMED): requires the option's text buffer pointer `+0x214` (UTF-16); sets the cursor
global `0x02282d3c` = text length, **capped at 191** (`0xbf`); arms a 100 ms timer on `0x012fd464` (`0x00d9e3c0`;
HYPOTHESIS: caret blink); clears the caret phase byte `0x02282d3b`; stores the option index at `+0x224`; sets `+0x228`
= 1 (editing); and calls `0x00dd0190`, which — when the keyboard device flag `0x0132abc0` bit 3 is set — **clears the
256-entry key-state table** at `0x02a38440` (stride 12) and six counters, so keys already held do not type into the
field (HIGH CONFIDENCE).

### D.4 `dialog_option_end_kbd_input` → `0x007c5440`

**Arguments / gate:** as D.3, but requires `+0x228` **set** (editing). **Return:** 0 values.
**Body:** `0x007c4f30()` with ECX = option (follow-up dump, CONFIRMED): cursor `0x02282d3c` = 0; if the text buffer
`+0x214` exists, `0x007c4660(0, 1)` and `+0x228` = 0.

`0x007c4660(caretPhase, notify)` (follow-up dump, CONFIRMED) rebuilds the option's display string: the text (up to 191
units), or that many `*` when byte `+0x220` is set (password field), followed by a caret `"|"` in colour `#000000`
(phase 0) or `#9400c5` (phase 1) using the UI markup `[format][color:…]…[/format]`; it stores the phase in
`0x02282d3b`; encodes the result with the wide-string encoder `0x00e22a30` (tranche 09) and writes it to option `+0x110`
(256 bytes), localizing first when `0x00849df0` recognizes it. With `notify` set it calls
`0x007c21d0(option +0x224, text, length)` — HIGH CONFIDENCE: delivers the finished text for the stored option index.
So ending input both commits the text and leaves a (phase-0) caret glyph in the displayed string (CONFIRMED;
HYPOTHESIS: phase 0 colour is meant to be invisible on the background).

Note: if `+0x214` is null, D.4 leaves `+0x228` set, but D.3 never sets `+0x228` without a buffer, so the two stay
consistent (CONFIRMED).

---

## E. Cellphone — 3 functions

### E.1 `cellphone_dial` → `0x0083d930`

**Arguments:** 1 number = phone-book entry index (truncated); 2 string = callback name; 3 string = second callback
name. **Return:** 0 values.

**Body (CONFIRMED — listing):**
1. Copies arg 2 into `0x023153f8` and arg 3 into `0x023153d8` (`0x00da7930` = `strncpy` 32 then force-terminate).
   Arg 3 is not used by this function (HYPOTHESIS: `cellphone_end_call` or the connect path uses it).
2. Entry pointer `0x0231568c` = `0x023156a8 + index·0x18` — **no bound and no sign check** on the index.
3. Entry `+0x14` = −1 → "hitman target" call: `0x00bda460("hitman_target")`, then **connect** (step 6).
4. Otherwise `0x007e5b40(entry +0x14)` → activity record (`0x022ad780 + i·0x180` for i below `0x022ad758`, else
   `0x022b0180 + (i − 0x022ad758)·0x180`, for i below `0x022ad754`; **null when out of range**), and the root reads
   record `+0xbc` at once (`0x0083d9ce`).
   - record `+0xbc` == 5 → state `0x02315688` = **3**; plays audio event `0x59b17c4e` on the "Cell Phone" bank
     (`0x0083cf30`), storing the playing id in `0x02315424`; calls the **callback** with −1 (`0x0083cfe0`); then
     `0x00bda460(record)` and returns.
   - else if `0x007e74f0(record)` (record is available: `0x00bc1250` on `+0x24`, `0x007e6240` false, and `+0xc0`/`+0xbc`
     combinations) **and** entry byte `+9` is set **and** `0x0083d800(record)` (a further context gate, partly read:
     record `+0xf1` bits 0/1/4, a squared-distance box test against `0x013b8770`, `0x00905d90`, player `+0x2074` bit 2,
     counters `0x00bb88f0(5)`/`(6)`; details OPEN) → **connect**;
   - otherwise → **busy**: state = **2**; "busy signal" audio object with the string "UI_Cell_Line_Busy" set on
     parameter `0xd46667bb`; callback with −1.
5. Every non-hitman path calls `0x00bda460(record)` — HYPOTHESIS: the record begins with its name, which is hashed;
   `0x00bda460` (dumped at depth 1) only acts when `0x013123d1`, `0x029897a5`, `0x029897a6` and a local player are all
   set, and posts the hashed name through `0x00423670` (meaning OPEN; HYPOTHESIS: a tutorial/telemetry event).
6. **Connect:** float `0x0231541c` = 1.0; plays event `0x604161d4` on the "Cell Phone" bank (id → `0x02315424`, 0 when
   the bank object can't be made); state = **1**; `0x02315420` = 1. The callback is **not** called here.

**Callback helper `0x0083cfe0(value)` (follow-up dump, CONFIRMED — listing):** builds a coroutine call object for the
name in `0x023153f8` on the global script state (`0x00e1a1b0`, `0x00e0ca80` — tranche 09), **writes its `+0x14`** with
the handle of the script group "cell_phone" (`0x00e1f2f0("cell_phone")` `+0x580`), pushes `value` as a number
(`0x00e0ce20`) and runs it (`0x00e0cd00`).

**Crash shapes (CONFIRMED — listings):**
- **Unbounded entry index:** any index outside the phone book reads an arbitrary 24-byte "entry" (`0x0083d999`–
  `0x0083d9a9`); a negative index reads below the array.
- **Null read at `0x0083d9ce`** whenever entry `+0x14` is not −1 and not a valid activity index (garbage from the
  unbounded index, or bad data).
- **Null write at `0x0083d013`** inside `0x0083cfe0`: the call object from `0x00e0ca80` is used with no check; it is
  null when arg 2 is nil/empty/not a global **Lua** function, or when the 256-record thread table is full (tranche 09).
  Reached on the "record kind 5" and **busy** paths. `0x00e1f2f0("cell_phone")` is also dereferenced (`+0x580` at
  `0x0083d007`) without a check — null when the "cell_phone" script group is not loaded. `0x0083cfe0` has two more
  callers outside any defined function (`0x0083d211`, `0x0083d28a`, inside the `cellphone_end_call` neighbourhood) that
  inherit the same hazard (HIGH CONFIDENCE).

### E.2 `cellphone_animate_stop_do` → `0x00a46570`

**Arguments:** none read. **Return:** 0 values. **Body:** calls `0x00754410`, which is a single `RET` — the project's
already-annotated **no-op stub** (`spec-extensionless-types.md` §1). **The function does nothing.** CONFIRMED.

### E.3 `cell_camera_enable` → `0x0083ade0`

**Arguments:** 1 boolean (read unconditionally). **Return:** 0 values.
**Body:** `0x02313ddc` = **1** when true, **2** when false. CONFIRMED.

The global starts at 0 (zero-fill) and has exactly **two** references in the whole binary: this write and the read in
the sibling `cell_camera_is_enabled` (`0x0083adb0`, follow-up dump: pushes `0x02313ddc == 1`). CONFIRMED (dump global
reference list, "total uses 2 in 2 functions"). **No engine code reads it**, so this pair is a Lua-side flag only: it
changes nothing in the camera or phone code (CONFIRMED for direct references; HYPOTHESIS that nothing reaches it
through a pointer).

---

## F. Mission and world state — 6 functions

### F.1 `flashpoint_mission_status` → `0x00a49b00`

**Arguments:** none read. **Return:** exactly 1 number = `0x006a7000()`. CONFIRMED.

`0x006a7000` combines two status dwords, A = `0x012f0228` and B = `0x012f022c` (both 1 in the file image), CONFIRMED:
- A == 4 or B == 4 → **4**;
- else A == 3 → **2** when B == 2, else **3**;
- else B == 3 → **2** when A == 2, else **3**;
- else → **A** (B is ignored).

Writers (follow-up dumps, CONFIRMED): `0x006a6ff0` resets both to 1; `0x006a7040` reads 4 bytes from a network
stream (`0x008810b0`) into **B** — HIGH CONFIDENCE B is the peer's status; `0x006a71d0` and `0x006a7220` store a 64-bit
value (object `+0x18`/`+0x1c`, or `0x012f0248`/`0x012f024c`) and set **A = 2** on the host, or call `0x006a70d0(2)`
otherwise; `0x006a75e0` — which opens the "FLASHPOINT_COMPLETE_TITLE"/"FLASHPOINT_COMPLETE_DESC_1" result screen
(`0x007f1ad0`) — sets **A = 4** on the host or calls `0x006a70d0(4)`; a write of 3 sits at `0x006a782b`, outside any
defined function. So **4 = complete** (CONFIRMED by the result-screen strings), **1 = reset/idle**, **2 = started**
(HIGH CONFIDENCE), **3 = HYPOTHESIS: failed/aborted**. The combination reports "in progress" (2) when one side is 3 and
the other 2. `0x006a70d0` (not dumped) is HIGH CONFIDENCE the client → host send.

**Note (CONFIRMED arithmetic):** "no session" takes the `0x006a70d0` branch, so in that case A is only updated if
`0x006a70d0` itself writes it (OPEN).

### F.2 `debris_flow_set_inactive` → `0x00a472d0`

**Arguments:** 1 number = debris-flow id (truncated). **Return:** 0 values. **Body:** `0x006fd110(id, 0)`. CONFIRMED.

`0x006fd110(id, fromNetwork)` (dumped at depth 1, CONFIRMED — listing):
1. `fromNetwork` = 0 → **always** sends message opcode `0x45`, sub-id `0x25`, operation **2**, and the 32-bit id to the
   session. No host, ownership or "id exists" check precedes the send.
2. Searches the **3**-slot table at `0x014fde60` (stride `0x6f0`, id at `+0`) and, for the first match, calls
   `0x006fc770` with ECX = the slot. (The following "slot is null" test is dead code.)

The network receive side is `0x006fde00` (follow-up dump, CONFIRMED): it reads an operation and a 32-bit id and
dispatches 0 → `0x006fbb70`/`0x006fbd80`, 1 → `0x006fce90` (create, with positions and flags), **2 → `0x006fd110(id,
1)`**, 3 → `0x006fbfb0`, 4 → `0x006fd2b0`, 5 → `0x006fd8b0`, 6 → `0x006fc100`. So the deactivate replicates.

**Worker `0x006fc770` (follow-up dump, CONFIRMED — listing):** if slot byte `+0x6ea` is set it just clears it and
returns (HYPOTHESIS: a pending/deferred flag). Otherwise, for each of the slot's entries (array `+0x40`, stride `0x18`,
count `+0x48`), it resolves the entry's 64-bit handle (handle resolution, kind bit `+0xa`/`0x08`, alive) and then
**calls virtual slot `+0x70` on the result**; for the object that returns, unless it is hidden (`+0x3b` bit 0): a
vehicle (`0x00853b30(o, 7)`) → `0x00a87780(handle, 1, 0)`; else a `+6` bit `0x20` object whose kind has `+0xb` bit
`0x01` (mesh mover) → `0x00a32000`; else `0x008ccb90(o, 1, 1, 0)` (the object show/hide helper tranche 11 dumped).
Finally it clears `+0x6ea`.

**Crash — CONFIRMED (listing):** when an entry's handle is zero, unresolvable, flagged dead, of the wrong kind or not
alive, the code sets the object pointer to **null** (`0x006fc7ee` `XOR EDI,EDI`) and still executes `MOV EAX,[EDI]` /
`CALL [EAX+0x70]` at `0x006fc7f0`. **Null dereference / null virtual call** on deactivating any debris flow that
contains an object that has since been destroyed or streamed out. This runs both for the Lua call and for the
replicated message on the peer. Reachability: HIGH CONFIDENCE (debris pieces are exactly the kind of object that gets
destroyed); a single stale entry suffices.

### F.3 `boss_battle_kb_enable` → `0x00a406d0` and F.4 `boss_battle_mars_killbane_active` → `0x00a40770`

**Arguments:** 1 nil-gated boolean, **default true** (no argument or nil = enable). **Return:** 0 values. CONFIRMED.

Both work on the Killbane boss-battle block at `0x0149f918`..`0x0149f924` (all zero-fill). CONFIRMED (follow-up
dumps):
- `kb_enable(true)` → `0x005e40f0`: `0x0149f918` = 1 (battle enabled), clears `0x0149f91c` (punch-react counter),
  `0x0149f920`, `0x0149f921`, `0x0149f923`, `0x0149f924`.
- `kb_enable(false)` → `0x005e53c0`: `0x0149f918` = 0; `0x00d34d20()` (a **return-1 stub**); and if `0x0149f920` is set,
  `0x005e4f50()`.
- `mars_killbane_active(true)` → `0x005e4130`: `0x0149f923` = 1, `0x0149f924` = 0, `0x0101b4f0()` (a **return-1 stub**).
- `mars_killbane_active(false)` → `0x005e4150`: clears **all six** fields, including `0x0149f918` and `0x0149f920`.

`0x005e4f50` (follow-up dump) is the **QTE abort**: only when `0x0149f920` is set, it tears down the "hud_qte" HUD
element (`0x00e25850`, `0x00e26140` with six reset records), calls `0x008023b0`, clears `0x0149f920`, calls
`0x00d34cd0`, and resets the local player's and the "M21_Killbane" character's animation/control state
(`0x0095f9c0`, `0x0094b070(-1)`, `0x0094cca0`, `0x0094f450`, `0x00942110(…, −1.0)`, `0x00986920`). So `0x0149f920` =
"Killbane QTE active" (HIGH CONFIDENCE). `0x005e4370` (follow-up dump) uses the counter `0x0149f91c` to pick
"KillBane_Punch_React_A", "…_B", then "…_C" on successive hits (CONFIRMED).

**Logic defect (CONFIRMED read; effect HYPOTHESIS):** `mars_killbane_active(false)` **clears the QTE-active flag
`0x0149f920` without running the QTE abort** that `kb_enable(false)` runs. If it is called while a Killbane QTE is in
progress, the "hud_qte" element and the player's locked control/animation state are never reset, and a later
`kb_enable(false)` can no longer reach `0x005e4f50` because the flag is already 0. Mission scripting order decides
whether this happens (OPEN).

### F.5 `ambient_gang_spawn_enable` → `0x00a3c2c0`

**Arguments:** 1 boolean (read unconditionally). **Return:** 0 values.
**Body:** `0x00910830(b)` writes the same value to the **four** bytes `0x01308acc`..`0x01308acf`. CONFIRMED.

Context (follow-up dumps, CONFIRMED): the file image has only the first byte set (1, 0, 0, 0); the reset `0x009142a0`
(called from `0x00908fb0`) sets **all four to 1**; `0x00910850` writes one byte by index and `0x00915da0` reads one by
index; `0x00910870` is true only when **all four** are set. HYPOTHESIS: one enable per gang (four ambient gangs); this
Lua call switches all of them at once — there is no per-gang Lua control here.

### F.6 `audio_suppress_ambient_player_lines` → `0x00a3c740`

**Arguments:** 1 boolean (read unconditionally). **Return:** 0 values.
**Body:** `0x0070bcd0(b)`: **only when a session exists and this machine is the host**, calls `0x00d1de80()` (a
**return-1 stub**) and writes the 16-bit global `0x01504478` = b. CONFIRMED.

`0x01504478` is reset to 0 in the audio init `0x0070f920` (follow-up dump), which also registers it with the
session-synced-variable mechanism `0x0086d770` (tranche 07/08) under the name **"persona_supress"** (sic), 2 bytes,
"authoritative" = host. CONFIRMED. So the host's value is replicated to clients, which explains the host-only write.

**Findings:**
- **No direct reader:** the global has 3 references in 2 functions (both writes plus the registration's address
  push). Whatever consumes the suppression reads it through the registered pointer or not at all (OPEN). CONFIRMED.
- **No session → no-op.** If the session object does not exist in single player (OPEN — spec §3.1/§8.27 only establish
  it as the co-op context), the call is ignored there entirely. A co-op **client**'s call is also ignored (CONFIRMED).

---

## G. HUD — 1 function

### G.1 `autil_hud_mayhem_init` → `0x0067d520`

**Arguments:** none read. **Return:** 0 values. Registered alone by `0x0067d660` under the UI registrar. CONFIRMED.

**Body (CONFIRMED — listing, second follow-up dump):**
1. **Once** (while `0x014b4b70` is 0): creates three native callback objects with `0x00e2a740(name, function)`:
   "hud_mayhem_world_cash_update" → `0x0067c070` (stored `0x014b4b70`), "hud_mayhem_world_cash_remove" → `0x0067d3b0`
   (`0x014b4b74`), "hud_mayhem_world_cash_destroy" → `0x0067b5d0` (`0x014b4b78`). `0x00e2a740` takes the head of the
   free list `0x02a74ff0`, initializes it through its virtual `+0x18`, moves it to the in-use list `0x02a74fec`, and
   **returns null when the free list is empty** (or either argument is null).
2. Takes the running script's group: `0x00e0ceb0()` (top of the current-thread stack) `+0x14` → `0x00e1f330` (front
   matter). Looks up two UI elements in that group by hashed name (`0x00d9e740` then `0x00e288a0`, a 64-bucket table at
   `0x02a74ce0` keyed by (hash, group)): "mayhem_cash_grp" → `0x014b4b54`, "mayhem_rise_anim" → `0x014b4b50` (null
   allowed).
3. Subscribes callbacks to the data item "mayhem_local_player_world_cash" with `0x00e25250(hash, event, callback +0x18,
   group)`: events **4** and **3** → the update callback, event **5** → the remove callback. The destroy callback is
   created but **not** subscribed here (CONFIRMED; its use is OPEN). `0x00e25250` subscribes at once when the data item
   exists (`0x00e24ba0`/`0x00e24c40`), otherwise queues a pending subscription from the pool `0x02a5afa0` (silently
   dropped when that pool is empty) and increments the callback's `+0x1c`.

**Crash shapes (CONFIRMED — listing):**
- **Callback pool empty on the first call:** `0x014b4b70` stays null and is read at `0x0067d5ce`/`0x0067d5d3`
  (`[null+0x18]`). The same for `0x014b4b74` at `0x0067d62d`.
- **No running script thread:** `0x00e0ceb0()` returns 0 and its `+0x14` is read at `0x0067d57b` without a check
  (HYPOTHESIS: unreachable from a normal script call, which always runs inside a pushed thread record).

**Logic (CONFIRMED structure):** steps 2-3 run on **every** call, so calling it twice adds the subscriptions twice
(whether `0x00e24c40` de-duplicates is OPEN) and re-binds the element pointers to the **latest** caller's group.

---

## H. Method notes for the follow-up dumps

- Follow-up dump (depth 0, `maxinsn:500`), 34 addresses: `0x006a6ff0 0x006a7040 0x006a71d0 0x006a7220 0x006a75e0
  0x007c5740 0x0072a160 0x0083adb0 0x007c4660 0x00dd0190 0x007c4f30 0x007c2940 0x00853b30 0x004bc7d0 0x004bca80
  0x004b16c0 0x006fc770 0x006fde00 0x00bc7520 0x00bc7480 0x0083cfe0 0x00bda460 0x0083d800 0x0056b5f0 0x005e4f50
  0x00d34d20 0x0101b4f0 0x005e4370 0x0070f920 0x00d1de80 0x009142a0 0x00910870 0x00a41f80 0x009aa130`.
  `0x0072a160` is large (body `0x0072a160`-`0x0072b2a4`); only its first ~0xd0 bytes were read for C.1.
- Xref run: each of the five registrars has exactly one caller, inside `0x008430f0` (section A).
- Range runs: the five registrar bodies; then `0x00a216d0-0x00a21740` for `character_fake_revival_start`'s row
  (`0x00a20e60-0x00a20e80` was also dumped and not needed).
- `0x00754410` (E.2) is already plate-annotated in the project database as a no-op stub.
- Three return-1 stubs met in this tranche, all `MOV EAX,1; RET`: `0x00d34d20`, `0x0101b4f0`, `0x00d1de80` — CONFIRMED.

---

## I. Cross-function observations

1. **Five registrars beyond the gameplay one, all under the UI registrar.** `0x007c5630` (dialog, 12), `0x0083db20`
   (cellphone, 3), `0x0083ae10` (cell camera, 2), `0x00bcd240` (cinema editor, 2) and `0x0067d660` (1) are each called
   once from `0x008430f0`. `0x0067d660` is the first one-name registrar seen in these tranches: it pushes the function
   before the name, which the `lua` dump mode's "next slot" heuristic does not cover.
2. **Unchecked resolver results are the dominant crash class again.** `character_fake_revival_end` (and its untranched
   sibling `_start`) writes through an unchecked `0x00a281a0` result; `character_take_human_shield_check_done` pushes a
   failure value and then continues into a null read. Compare `character_hidden` and
   `character_set_cannot_exit_rc_vehicle`, which check.
3. **Null objects inside loops over stored handles.** `debris_flow_set_inactive`'s worker nulls an unresolved object and
   calls its vtable anyway. Any stored-handle list that can outlive its objects (debris, spawned props) should be
   treated as a crash source until checked.
4. **Unchecked coroutine call objects.** `cellphone_dial`'s helper `0x0083cfe0` is the first place in these tranches
   where the result of `0x00e0ca80` is written through **without** a null check (tranche 09's report functions all
   check). A wrong callback name or a full 256-record thread table turns into a null write rather than a skipped call.
5. **Unconditional network sends.** `customization_swap_player_rig` (opcode `0x41`), `debris_flow_set_inactive`
   (`0x45`/`0x25`), `camera_script_enable`/`_disable` (`0x43`/`0x10`) send on every call with no host or ownership
   gate; `character_set_cannot_exit_rc_vehicle` uses the gated double-gate pattern instead, and
   `audio_suppress_ambient_player_lines` uses a host-only session-synced variable. Opcodes `0x41`, `0x43`, `0x45` and
   `0x46` match tranche 11's list.
6. **Engine-unread flags and no-ops.** `cell_camera_enable` writes a global only `cell_camera_is_enabled` reads;
   `cellphone_animate_stop_do` calls a no-op stub; `audio_suppress_ambient_player_lines`'s global has no direct reader;
   `boss_battle_*` and `audio_suppress_*` call return-1 stubs on the way. A reimplementation can treat the first two as
   pure Lua-side state.
7. **Return shapes a script can observe:** `character_take_human_shield_check_done` returns 1, 2 or 3 values (B.2);
   `cinema_editor_create_camera_zone` returns 1 value or **none** (C.5); `cutscene_was_skipped` stays true after the
   cutscene ends (C.1); `character_hidden` answers true for an unknown name (B.4).
8. **Dialog subsystem latch.** `dialog_close_finished` is a required handshake: the dialog manager stops after every
   close until it is called (D.2).
9. **Index spaces for a binding layer:** D.1/D.3/D.4 arg 1 = dialog handle (slot | generation, slot < 4), arg 2 = index
   among **type-3** children; E.1 arg 1 = phone-book entry index (`0x023156a8`, unbounded); F.2 arg 1 = debris-flow id
   (matched against 3 slots); C.5 arg 2 and result = camera-zone handle (low byte slot, 16-bit generation << 8);
   B.6 arg 3 = player mask (bit 0 local, bit 1 remote).

## J. OPEN

- B.1: meaning of the flee flag bits 1 and `0x10` and of the constants `0x2d`/`0xb2`; who consumes order kind `0x30`.
- B.3: the reader of character `+0x1c9c` bit 9.
- B.5: what `+0xe4` bit 16 changes; the rest of `0x009a5520`.
- B.6: what the second name's record (`0x02ef1638`) is; whether a null `+0x30` is tolerated by its readers; how the
  receiver of opcode `0x41` sub-id 9 applies the swap (double application in co-op?).
- C.3/C.4: the four readers of `0x013c9382`; the receiver of `0x43`/`0x10`.
- C.5: the camera-zone pool size (bounds of `handle & 0xff`); `0x00bc6fb0`.
- D.1: the reader(s) of dialog `+0x11c`. D.2: the caller of `0x007c5740`; whether any timeout clears `0x02282d39`.
  D.4: what `0x007c21d0` does with the committed text.
- E.1: the second callback name `0x023153d8`; `0x0083d800`'s exact gate; `0x00bda460`'s event; the meaning of record
  `+0xbc` == 5 and of the state values 1/2/3 in `0x02315688`.
- F.1: the writer of state 3 (`0x006a782b`); `0x006a70d0`'s body; what the two 64-bit values stored by `0x006a71d0` /
  `0x006a7220` are.
- F.2: when `+0x6ea` is set; `0x00a32000` and `0x00a87780`.
- F.3/F.4: whether any mission calls `boss_battle_mars_killbane_active(false)` during a Killbane QTE.
- F.5: which gang each byte belongs to.
- F.6: whether the session object exists in single player; who consumes "persona_supress".
- G.1: whether `0x00e24c40` de-duplicates subscriptions; where the destroy callback `0x014b4b78` is used.

## K. Direct-answer table

| # | name | registrar | handler | status |
|---|---|---|---|---|
| 1 | `flee` | gameplay `0x00a20840` | `0x00a49b30` | resolved — AI order kind `0x30` to the actor, flee-from target default "#PLAYER1#"; players refused; pool-checked |
| 2 | `flashpoint_mission_status` | gameplay | `0x00a49b00` | resolved — combined local/peer status, 4 = complete |
| 3 | `dialog_option_end_kbd_input` | dialog `0x007c5630` (under UI `0x008430f0`) | `0x007c5440` | resolved — commits text, clears editing |
| 4 | `dialog_option_accept_kbd_input` | dialog | `0x007c2fb0` | resolved — starts text entry (cursor ≤ 191, key-state flush) |
| 5 | `dialog_close_finished` | dialog | `0x007c1be0` | resolved — clears the manager latch; **dialog system stalls until called** |
| 6 | `dialog_box_set_current_option` | dialog | `0x007c2e40` | resolved — writes dialog `+0x11c`, no range check |
| 7 | `debris_flow_set_inactive` | gameplay | `0x00a472d0` | resolved — replicated deactivate; **null virtual call on a stale entry** |
| 8 | `cutscene_was_skipped` | gameplay | `0x00a42ea0` | resolved — bit `0x40` of `0x0153b524`, set by the stop routine when skip requested |
| 9 | `cutscene_check_exiting` | gameplay | `0x00a42e70` | resolved — cutscene state > 12 |
| 10 | `customization_swap_player_rig` | gameplay | `0x00a43cf0` | resolved — per-player rig swap + replication; null player / unknown rig crash shapes |
| 11 | `cinema_editor_create_camera_zone` | cinema `0x00bcd240` (under UI) | `0x00bcc1d0` | resolved — new zone handle or 0 values; unbounded template slot read |
| 12 | `character_take_human_shield_check_done` | gameplay | `0x00a46600` | resolved — 1/2/3 values; **null read on unresolved character** |
| 13 | `character_set_cannot_exit_rc_vehicle` | gameplay | `0x00a42140` | resolved — double-gate setter, `+0x1c9c` bit 9 |
| 14 | `character_hidden` | gameplay | `0x00a41540` | resolved — `+0x3b` bit 0; true when unresolved |
| 15 | `character_fake_revival_end` | gameplay | `0x00a41fc0` | resolved — clears `+0xe4` bit 16; **null write on unresolved name** |
| 16 | `cellphone_dial` | cellphone `0x0083db20` (under UI) | `0x0083d930` | resolved — ring/busy/connect; **unbounded index, null read, null write in callback helper** |
| 17 | `cellphone_animate_stop_do` | gameplay | `0x00a46570` | resolved — **no-op** |
| 18 | `cell_camera_enable` | cell camera `0x0083ae10` (under UI) | `0x0083ade0` | resolved — writes 1/2 to a global only Lua reads back |
| 19 | `camera_script_enable` | gameplay | `0x00a448d0` | resolved — `0x013c9382` = 1 + broadcast |
| 20 | `camera_script_disable` | gameplay | `0x00a447b0` | resolved — `0x013c9382` = 0 + broadcast |
| 21 | `boss_battle_mars_killbane_active` | gameplay | `0x00a40770` | resolved — default true; false clears the QTE flag **without** the QTE abort |
| 22 | `boss_battle_kb_enable` | gameplay | `0x00a406d0` | resolved — default true; false aborts an active QTE |
| 23 | `autil_hud_mayhem_init` | `0x0067d660` (under UI) | `0x0067d520` | resolved — once-only callbacks + per-call subscriptions; pool-exhaustion null read |
| 24 | `audio_suppress_ambient_player_lines` | gameplay | `0x00a3c740` | resolved — host-only session-synced "persona_supress"; no direct reader |
| 25 | `ambient_gang_spawn_enable` | gameplay | `0x00a3c2c0` | resolved — sets all four gang-spawn bytes |

## L. Crash-shaped and logic defects (summary)

| # | where | defect | status |
|---|---|---|---|
| 1 | B.5 `0x00a41fde` | unresolved character → write through null (`+0xe4`) | CONFIRMED |
| 2 | B.5 sibling `character_fake_revival_start` `0x00a41f80` → `0x009a553f`, `0x00a41faa` | same, read then write through null | CONFIRMED |
| 3 | B.2 `0x00a4665c` → `0x009aa16d` | unresolved character passed on after the failure push; null read of `+0x1890` | CONFIRMED (relies on tranche 11's `0x008addb0(null)` = true) |
| 4 | F.2 `0x006fc7ee`/`0x006fc7f0` | stale debris entry → null object → virtual call through null; also on the network peer | CONFIRMED |
| 5 | E.1 `0x0083d999`–`0x0083d9a9` | phone-book index used without bounds | CONFIRMED |
| 6 | E.1 `0x0083d9ce` | null activity record (`0x007e5b40` out of range) dereferenced | CONFIRMED shape |
| 7 | E.1 `0x0083cfe0` (`0x0083d013`, `0x0083d007`) | coroutine call object and "cell_phone" group record used unchecked → null write/read on a bad callback name, full thread table, or unloaded group | CONFIRMED shape |
| 8 | B.6 `0x009e3400` | no local player with mask bit 0 → reads `+0xf4`/`+0xd24` of null | CONFIRMED shape |
| 9 | B.6 `0x004bca80` | unknown rig name → `null + 0x50` read | CONFIRMED shape |
| 10 | G.1 `0x0067d5d3`, `0x0067d62d` | native-callback pool empty → null read | CONFIRMED shape, pool-gated |
| 11 | G.1 `0x0067d57b` | no current script thread → null `+0x14` read | CONFIRMED shape (HYPOTHESIS: unreachable from scripts) |
| 12 | C.5 `0x00bc74b9` | template slot `handle & 0xff` not bounded by the pool size before its read | CONFIRMED unchecked, read-only |
| 13 | D.2 / `0x007c5740` | dialog manager frozen after each close until the script acknowledges; no timeout | CONFIRMED mechanism |
| 14 | F.4 | `mars_killbane_active(false)` clears the QTE flag without the QTE abort; later abort unreachable | CONFIRMED read, effect HYPOTHESIS |
| 15 | D.1 | option index stored without a range check | CONFIRMED unchecked, consumer OPEN |
| 16 | B.6 | two replication messages per call with mask 3, no host gate | CONFIRMED structure, consequence OPEN |
| 17 | F.2, C.3/C.4 | network message sent on every call regardless of host/ownership/existence | CONFIRMED |
| 18 | E.3 | flag has no engine reader (Lua-only state) | CONFIRMED (direct references) |
| 19 | E.2 | function is a no-op | CONFIRMED |
| 20 | F.6 | write ignored on clients and (OPEN) without a session; global has no direct reader | CONFIRMED |
| 21 | B.2 | 1, 2 or 3 return values depending on resolution | CONFIRMED |
| 22 | C.5 | failure returns 0 values, not nil | CONFIRMED |
| 23 | G.1 | repeated calls stack subscriptions (de-dup OPEN) | CONFIRMED structure |

## M. Clean-room check

- Addresses are plain hex throughout; no Ghidra auto-names for functions, globals or labels, no decompiler variable
  names, and no pasted pseudocode. Library routines are named only where Ghidra identified them as standard C runtime
  (`strncpy`, `_stricmp`, `memset`, `wcsncpy`, `wcsncat`); short game strings are quoted as data.
- Final self-check run on this file with the pattern
  `\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4}|LAB_[0-9a-fA-F]+|FUN_[0-9a-fA-F]+|DAT_[0-9a-fA-F]+)\b`:
  **0 hits**. The pattern was first checked against 12 positive controls (one sample of each auto-named local family,
  the parameter, register-input, stack-array, type-name and the three label-prefix forms), all of which it matched, and
  5 negative controls (plain English such as "left undefined", "in ECX", a bare `0x…` address), none of which it
  matched.
- No spec file was edited. The private Ghidra copy `tools\gp_t13` was deleted after the dumps; raw dumps stayed in the
  session scratchpad.
