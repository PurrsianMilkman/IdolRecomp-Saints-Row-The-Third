# Ranking tranche 18 — 25 previously-unspecced Lua-bound names (Team A, 2026-10-03)

Job definition: `D:\Project Crreish\TEAM A\ghidra\jobs\ranking\tranche-18.json`. The job file's own title states **names
376-400 of the 554 unspecced names, Team B call-count order**; this note uses that range and exactly the 25 names the job
lists. Run locally, read-only, on a private copy of the Ghidra project (`tools\gp_t18`, deleted afterwards), with the
job's own arguments: `CrreishDump.java <out>/lua lua depth:1 maxfuncs:15 maxinsn:500 <25 names>`. **All 25 names
resolved.** Every name string occurs exactly once in `.rdata`. For 22 names the handler is the code pointer stored in the
slot right after the name (insn offset +1) — CONFIRMED — dump `index.txt`, cross-checked against the range run. The other
three — `options_display_pc_set_options`, `object_indicator_init_lua` and `msn_killbane_selected_option` — are
registered by **one-name registrars that push the function pointer before the name** (the tranche 13 / tranche 16
shape), so the dumper's offset guess landed on the `lua_setfield` primitive `0x00dfe830`; the real handlers
`0x007cca80`, `0x008f8370` and `0x00840e90` were read from the range run (section A).

Follow-up runs on the same private copy, cited by name below:

- "range run": `range` mode on the six UI registrar bodies of section A;
- "xref run": `xref` mode on those six registrars and on the shared stub `0x007c9f50`;
- "follow-up dump": depth-1 `func` run on `0x007cca80` and `0x008f8370`;
- "second follow-up dump": depth-0 `func` run on 15 callee addresses (listed in section H);
- "second range run": `range` mode on `0x00840e90-0x00840ef0`, `0x00a58d40-0x00a58e00`, `0x00a53f90-0x00a54110` and
  `0x00a23280-0x00a232e0`;
- "constant read": `ptrs` mode on `0x012ed130` (12 dwords) and three double constants (section H);
- "third follow-up dump" / "third xref run" / "third range run": four more callees, five globals and four short ranges
  (section H).

Call counts are Team B's (`D:\Crreish-sync\for-team-b\team-b\tools\lua_reconciliation_called_and_registered_1181.tsv`):
**every name in this tranche is 1 call site in 1 script.** Team B tags 12 names `ui` (the three `pause_menu_*`, the
four `pause_map_*`, `options_display_pc_set_options`, `object_indicator_init_lua`, `msn_killbane_selected_option` and
the two `main_menu_*`) and 13 `gameplay`; section A shows the registrars agree exactly (12 under the UI bring-up, 13 in
the gameplay registrar).

**Already partly covered elsewhere (cited, re-verified, not re-derived):** four of the 25 names are not new to the
project. `pause_map_stag_takeover_do_reward` is `spec-lua-api-behaviour.md` §31.6; `pause_map_is_stag_mode` and
`pause_map_is_tutorial_mode` are named with their flag bytes in §31.5 / §32.1 but have no entry of their own; and the
body of `open_vint_dialog_do` (`0x00a58e00`) was already read by tranche 10 (F.3) as the writer of
`open_vint_dialog_check_done`'s result, without being matched to its name. Each was re-read in this tranche's own dumps;
section D and C.4 below add what those notes did not say (a crash-shape precision for §31.6, and a correction to
tranche 10 F.3's reading of the dialog result).

Labels: **CONFIRMED** = read in the listing or decompile of a dump made for this note; **HIGH CONFIDENCE** = follows
from a dumped call or reference, but the callee body was not dumped or a meaning is inferred from strong usage;
**HYPOTHESIS** = plausible, not settled; **OPEN** = not settled (collected in section J).

**Shared conventions, cited not re-described:** every entry opens with the `lua_gettop` prologue (`0x00dfde50`) and
reads arguments with negative stack indices counted from the bottom (`-N` = arg 1, `1-N` = arg 2, …). Primitives as in
tranche 09/13/16 front matter: `0x00dfe210` `lua_tolstring` (non-string reads as null), `0x00dfe1e0` `lua_toboolean`,
`0x00dfe040` `lua_type` (0 = nil), `0x00dfe160` `lua_tonumber` (non-number reads as 0), `0x00ea2596` truncating
float-to-int, `0x00dfe590` `lua_pushboolean`, `0x00dfe3a0` `lua_pushnumber`. New to this series and CONFIRMED in this
tranche's dumps: `0x00dfe5e0` is `lua_gettable` (key on top, table at the given index), `0x00dfde60` is `lua_settop`
(here always −2, i.e. pop one), `0x00dfe380` pushes nil and `0x00dfec70` is `lua_next`. "Nil-gated optional argument" =
the standard idiom of tranche 13 (read only when enough arguments were passed and the slot is not nil, else the stated
default); an **unconditional** read has no presence check (missing boolean = false, missing number = 0, missing string =
null). Resolvers: `0x00a28150` (`#FOLLOWER#` sentinels, then the plain character resolver `0x005e4dd0` on the
world-object registry `0x02442750`, liveness `0x00853b10`, virtual `+0x70`). `0x009da4e0` local player; `0x0087ba20`
session; "host" = session exists and its `+0x5c` equals `+0x58`. Kind-flag tests through `[0x02cc9900 + 4·(object byte
+0x34)]`; `0x00853b30(obj, 0x21)` = "is a player" (tranche 13). Handle resolution: `0x00458230` on `0x024433a8` with key
`0x031d152c`, then the `+0x33` bit `0x10` rejection (tranche 11). Network record helpers (`0x0086f5f0` open with
opcode, `0x00881110`/`0x00881040` write bits/bytes, `0x004d46e0` write one boolean bit, `0x00a017c0` identity serialiser
(null → a zero pair), `0x0086f1b0` broadcast to session, `0x0086f110` send to one machine, `0x0086eb20` close) as in
tranche 11/16. The "double-gate" setter shape (`0x008ae480`, `0x008837a0`, owner `0x008ae3a0`, opcode `0x46` with two
debug-tag strings; with the gates false and no owner it does nothing) is `spec-lua-api-behaviour.md` §7.2 / tranche 13
B.3 / tranche 16 D.2. The Lua-hook dispatch used by the main-menu video (`0x00e0ca80` look up a hook by name,
`0x00e0ce00` push one boolean onto the hook record, `0x00e0cd00` dispatch) is the coroutine mechanism of the tranche
front matter and `spec-lua-api-behaviour.md` §31.3. The dialog pool (`0x007c3310` pops a free dialog or returns null)
is `interp_tabs.md` Q7.2/Q7.4. The named-object lookup body shape on `0x02442750` (table `+0x2660`, count `+0x265c`,
lookup `0x004588f0`, `+0x33` bit `0x10` rejection, one kind bit) is tranche 16's front matter; this tranche meets two
more members of that family: **`0x005f4c30`** (kind row `+0xc` bit `0x04` — **mission**, from the mission-name callers
below) and **`0x00734e90`** (kind row `+0x6` bit `0x02`, the minimap-icon object resolver of `spec-lua-api-behaviour.md`
§10.5).

---

## A. Registrars — where the 25 names live

Seven registrars. CONFIRMED — `index.txt`, range run, xref run:

| registrar | reached from | names in this tranche |
|---|---|---|
| gameplay `0x00a20840` | (established) | 13: `party_dismiss_do`, `open_vint_dialog_do`, `npc_is_in_brute_grab_qte`, `npc_dont_auto_equip_or_unequip_weapons`, `npc_brute_set_no_downed_finisher`, `npc_aim_at_point`, `notoriety_get`, `mission_maybe_uncomplete_m22`, `minimap_icon_remove_radius_do`, `minimap_icon_add_radius_do`, `message_remove`, `mesh_mover_reset_by_zone`, `melee_get_move_id_from_name` |
| `0x007af8d0` (pause menu, 7 names) | UI `0x008430f0`, call at `0x0084318f` | 3: `pause_menu_cat_mouse_active`, `pause_menu_common_top_finished_loading`, `pause_menu_cant_retry_because_host_in_creation` |
| `0x007dfae0` (pause map, 9 names; `spec-lua-api-behaviour.md` §31.1) | UI `0x008430f0`, call at `0x00843195` | 4: `pause_map_stag_takeover_do_reward`, `pause_map_is_stag_mode`, `pause_map_is_tutorial_mode`, `pause_map_drag_map` |
| `0x007c9fc0` (main menu, 18 names) | UI `0x008430f0`, call at `0x0084315f` | 2: `main_menu_video_has_stopped`, `main_menu_restart_video` |
| `0x007ccfd0` (one name) | UI `0x008430f0`, call at `0x00843183` | 1: `options_display_pc_set_options` |
| `0x008f8710` (one name) | UI `0x008430f0`, call at `0x00843177` | 1: `object_indicator_init_lua` |
| `0x00840ef0` (one name) | UI `0x008430f0`, call at `0x00843165` | 1: `msn_killbane_selected_option` |

- The three table-driven UI sub-registrars build a local `{name, function}` array and loop over `lua_pushcclosure`
  (`0x00dfe4f0`) then `lua_setfield` into globals (`0x00dfe830`, index `-10002`). Loop counts from the range run: pause
  menu **7**, main menu **18** (`0x12`), pause map 9 (§31.1). Full tables not previously on record, for a binding layer:
  - pause menu `0x007af8d0`: `pause_menu_open_achievements` `0x007c9f50`, `pause_menu_error_message_while_saving_PS3`
    `0x007c9f50`, `pause_menu_horde_mode_retry` `0x007af760`, `pause_menu_horde_mode_new` `0x007af780`,
    `pause_menu_cat_mouse_active` `0x007af7a0`, `pause_menu_common_top_finished_loading` `0x007af7d0`,
    `pause_menu_cant_retry_because_host_in_creation` `0x007af870`;
  - main menu `0x007c9fc0`: `main_menu_money_shot_check` `0x007c9f50`, `main_menu_new_game` `0x007c9d20`,
    `community_login_open` `0x007c9d40`, `main_menu_horde_start` `0x007c9d60`, `main_menu_matchmaking` `0x007c9da0`,
    `main_menu_check_online_privilege` `0x00842910`, `main_menu_check_user_content` `0x007c9e30`,
    `main_menu_check_chat_priv` `0x00842940`, `main_menu_maybe_show_voice_dialog` `0x007c9f50`,
    `main_menu_check_open_nat` `0x007c9f00`, `main_menu_supress_profile_change` `0x007c9e60`,
    `main_menu_video_has_stopped` `0x007c9e90`, `main_menu_continue` `0x007c9eb0`, `main_menu_set_coop_menu_type`
    `0x007c9ed0`, `main_menu_check_messages` `0x007c9f50`, `main_menu_redeem_code` `0x007c9d80`,
    `main_menu_start_from_load` `0x007c9f60`, `main_menu_restart_video` `0x007c9f90`.
  CONFIRMED — range run. Five of those 25 slots are the shared no-op stub `0x007c9f50` (below).
- The three one-name registrars (`0x007ccfd0`, `0x008f8710`, `0x00840ef0`) each null-check the Lua state, push `0`,
  the handler (`0x007cca80` / `0x008f8370` / `0x00840e90`) and the state for `lua_pushcclosure`, **then** the name and
  `-10002` for `lua_setfield` — the function pointer precedes the name, exactly as tranche 13's `0x0067d660` and
  tranche 16's `0x00a03600`/`0x007aff80`. CONFIRMED — range run. `0x00840e90` has no Ghidra function boundary (the range
  run shows the pointer without a function tag); it was read with a range dump (section E.3).
- **`npc_aim_at_point` → `0x007c9f50`, the shared no-op stub** (`spec-lua-bindings.md` §13.6; `spec-lua-api-behaviour.md`
  §6.1 `set_mission_author`, §24.2 `bink_play`, §27.17 `game_send_party_invites`): it calls `lua_gettop`, discards the
  result and returns 0 values. CONFIRMED — `npc_aim_at_point_0.txt`. The xref run lists 30+ data references to it,
  including six slots in the gameplay registrar (`0x00a20b87`, `0x00a20f65`, `0x00a236c1`, `0x00a23a4c`, `0x00a24071`,
  `0x00a24ac1`); `0x00a236c1` is this name's slot. **A binding layer can implement `npc_aim_at_point` as a no-op that
  ignores every argument** — the retail executable does nothing with them.
- **`minimap_icon_remove_radius_do` → `0x00a54430`, the same handler as `minimap_icon_remove_do`** (tranche 04 E.3). The
  dumper found the code pointer `0x00a54430` both at insn offset −1 (`[ESP+0xf90]`, written at `0x00a232a1`, the
  preceding slot, which tranche 04 identified as `minimap_icon_remove_do`'s) and at insn offset +1 (`[ESP+0xf98]`,
  written at `0x00a232b7`, this name's slot). CONFIRMED — `index.txt`, second range run. The two names are byte-for-byte
  the same binding; nothing in the handler is radius-specific (section C.2).
- `pause_map_stag_takeover_do_reward`'s second "use" (`pause_map_stag_takeover_do_reward_1.txt`, rooted at `0x00dfe830`)
  is the registrar loop reading the **first** row of its table, as in tranche 09/13/16 — not a second handler.
  CONFIRMED — range run (the loop body `0x007dfb90`-`0x007dfbb3`, 9 iterations, pushes each row's name from a register
  at `0x007dfb9e`; the dumper's data-flow annotation tied that push to this name only because it is row 1).

---

## B. Pause menu and main menu — 5 functions

### B.1 `pause_menu_common_top_finished_loading` → `0x007af7d0`

**Arguments:** none read. **Return:** 0 values. **Body:** sets byte `0x0229ce30` = 1. CONFIRMED — listing.

The byte is zero-fill (not file-backed) and has five direct references (dump global list): this write; clears to 0 at
`0x007e1e3f` (in `0x007e1e30`), `0x007e1fca` (in `0x007e1fb0`) and `0x007e2007` (code with no function boundary); and a
single read at `0x007af4fe` (code with no function boundary, just before this registrar's handlers). The third range run
shows what it gates (CONFIRMED — listing `0x007af4f4`-`0x007af52f`): a **pending pause-menu sub-screen request**
`0x012fcae4` (−1 = none) is acted on only when this flag is set **and** the top of the HUD screen stack `0x012fced8`
is screen id **10** (`+0x30` = `0xa`); request 1 selects the Lua hook name `"pause_menu_common_open_invite"`, request
2 `"pause_menu_common_open_cheat_save"` (request 0's target lies past the range read — OPEN). The three clears are the
screen-id-10 initialiser `0x007e1fb0` (which sets `+0x30`/`+0x10` = 10), its teardown `0x007e1e30`, and `0x007e1ff0`.
So this is a "the pause menu's common top section has finished loading" handshake: an invite or cheat/save request
that arrives while the pause menu is still building is held until the UI script raises this flag. Calling it outside
the pause menu is harmless (the reader also requires screen 10 on top).

### B.2 `pause_menu_cat_mouse_active` → `0x007af7a0`

**Arguments:** none read. **Return:** exactly 1 boolean = (`0x006a3910()` == 14). CONFIRMED — listing.

`0x006a3910` is the minigame mode query of `interp_tabs.md` Q8.1 / `spec-lua-api-behaviour.md` §10.1: with the
cat-and-mouse minigame singleton `0x014c2d10` absent it returns −1; otherwise it tail-calls virtual `+0x8` of the object
at singleton `+0x14` (CONFIRMED — listing). So this returns **true only while the minigame singleton exists and its
state/mode object reports 14** (HYPOTHESIS: 14 = the "cat and mouse" variant or its active phase; §10.1 records value
8 as "disables the mechanic"). The virtual target was not resolved (OPEN). No crash shape: the singleton is
null-checked and the sub-object at `+0x14` is assumed valid only while the singleton lives.

### B.3 `pause_menu_cant_retry_because_host_in_creation` → `0x007af870`

**Arguments:** none read. **Return:** exactly 1 boolean. CONFIRMED — listing.

**Body:** true iff **all three** hold:
1. `0x006d7240("mm_m1_5")` — the mission named `"mm_m1_5"` (string `0x0113a888`) resolves through `0x005f4c30`
   (mission kind, alive) **and is the active mission** `0x014c8460`, with the mission phase `0x014c7a14` ≠ 8 (same
   test as tranche 16 B.2's `0x006d9b90`; an unknown name compares the active mission against 0 and so reads false);
2. a session exists (`0x0087ba20` non-null);
3. this machine is **not** the host (session `+0x5c` ≠ `+0x58`).

So it reports "a co-op **client** cannot offer Retry because the host is still inside mission `mm_m1_5`" (HIGH
CONFIDENCE that `mm_m1_5` is the character-creation step of the first mission, from the name; the mission table was not
read). It is false for the host, in single player without a session, and in every other mission.

### B.4 `main_menu_video_has_stopped` → `0x007c9e90`

**Arguments:** none read. **Return:** 0 values. **Body:** `0x007adb90()` only. CONFIRMED — listing.

**`0x007adb90` — "stop the main-menu background video" (CONFIRMED — listing):** if the playing flag `0x022425f9` is
set, it calls virtual `+0x40(0, 0)` on the video object `0x022425fc`, then virtual `+0x80` on it and virtual `+0x40(0,
0)` on the object that returns, releases the video with `0x00a73bd0` (which unlocks a `"pregame split lock"` slot and
decrements the live-video count `0x0271a24c`), calls the stored completion callback `0x02242600` if non-null and clears
it; in every case it clears the playing flag. So this name is the script telling the engine "the video reached its end —
tear it down".

### B.5 `main_menu_restart_video` → `0x007c9f90`

**Arguments:** none read. **Return:** 0 values. CONFIRMED — listing.

**Body:** three steps:
1. `0x007adb30(0)`: bytes `0x022425fa` = 0 and `0x022425fb` = 1, completion callback `0x02242600` = **0**; then fires the
   Lua hook **`"main_menu_stop_video"`** (`0x00e0ca80`) with the hook record's `+0x14` set to the document handle
   (`+0x580`) of the loaded interface document `"bg_saints"` (`0x00e1f2f0` finds a loaded document by name hash in the
   list `0x02a4d17c`), and dispatches it (`0x00e0cd00`). If either lookup fails nothing is fired.
2. `0x007adb90()` — B.4's teardown.
3. `0x007adc00()`: asks `0x00a73c00` for the screen size, subtracts `0x007c59e0()` from the width, creates a new video
   with `0x00a73e40(width, height, "main menu video")`, stores it in `0x022425fc` and in `0x02317b5c` (`0x00843300`),
   sets the playing flag `0x022425f9` = 1, `0x022425fa` = 1, `0x022425fb` = 0, and fires the Lua hook
   **`"main_menu_start_video"`** into `"bg_saints"` with one boolean argument, byte `0x022425e8` (`0x00e0ce00` pushes a
   boolean onto the hook record).

The only other caller of `0x007adb30` is `0x007c5a50` (a screen-init method reached from data at `0x0115740c`), which
passes the callback `0x007c59f0` — so a non-null completion callback exists only on that path; this binding always
clears it.

**Crash — CONFIRMED structure (listing of `0x007adc00`, `0x00a73e40`, `0x007adb90`):** `0x00a73e40` **returns null**
when the video system refuses (`0x00706ad0(5)` true, the busy byte `0x0271a249` set, **seven or more live videos**
(`0x0271a24c` ≥ 7), or the allocator `0x00a73d50` fails). `0x007adc00` does **not** test the result: it stores the null
video and **still sets the playing flag**. The next `main_menu_video_has_stopped` or `main_menu_restart_video` then
reaches `0x007adb90`, sees the flag, and makes a virtual call through the null object (`MOV EAX,[ECX]` at
`0x007adb9f`) — an access violation at address 0. The stored null also reaches `0x02317b5c`, whose one reader
`0x00842c50` (reached as data from the UI bring-up `0x008489e0`) copies it unchecked into a draw record for
`0x00bddbd0` (third follow-up dump; whether `0x00bddbd0` tolerates a null video is OPEN). Trigger: any condition that makes video creation fail
while the main menu restarts its background video. A reimplementation should create the video first and set the
playing flag only on success.

**Logic note (CONFIRMED structure):** `main_menu_restart_video` fires `"main_menu_stop_video"` **before** the old video
is torn down and `"main_menu_start_video"` after the new one exists, so a UI script sees stop → start in that order
even though both happen inside one call.

---

## C. Minimap icons, messages, dialogs and object indicators — 5 functions

### C.1 `minimap_icon_add_radius_do` → `0x00a54110`

**Arguments:** 1 string = object name (unconditional; non-string reads as null); 2 nil-gated number = radius (float),
default **0.0**; 3 nil-gated number = local/replicate selector (truncated), default **3**. **Return:** 0 values.
CONFIRMED — listing.

**Body (CONFIRMED — listing):** the object is resolved with `0x00734e90` (named-object lookup, kind row `+0x6` bit
`0x02`) and the liveness test `0x00853b10`. Then, by kind:
- **object kind row `+0x7` bit `0x02` set** (the "carries its own icon fields" kind of `spec-lua-api-behaviour.md`
  §10.5 / tranche 04 E.3): `0x0093d3a0` with `this` = the object and (**5**, −1, −1, radius, selector) — icon id 5 with
  no secondary ids; the record-and-replicate body of tranche 04 E.3 (opcode `0x40` sub-tag 9 when bit `0x2` of the
  selector is set and the object is authoritative; local store of the new values, release of the old handle, and
  `0x0093b7a0(selector)` when bit `0x1` is set and something changed).
- **any other kind:** the function pointer **`0x0130c9a8[selector]`** with (object handle low, handle high, −1, −1,
  radius, 0). Note the **icon id here is −1, not 5** — the two kinds are not given the same request (CONFIRMED
  difference; whether the table targets ignore the id for a radius request is OPEN).

**Crash — CONFIRMED (listing `0x00a5419b`-`0x00a541a7`):** when the name is null, the lookup fails, or the object is
dead, the code jumps to `0x00a5419b`, which **zeroes the object register and then reads the handle from it**
(`[0 + 0x8]` at `0x00a541a1`, `[0 + 0xc]` at `0x00a541a7`) before the table call — an access violation at address 8.
So **any `minimap_icon_add_radius_do` call whose object is nil, misspelt, not yet streamed in, despawned or dead
crashes.** Its sibling `minimap_icon_add_do` (`0x00a53f90`, second range run) has the correct shape — it returns early
when the object is missing (`TEST ESI,ESI` / `JZ` at `0x00a540b8`) — which is the direct template for the fix.

**Second crash shape — CONFIRMED (same as tranche 10 H3):** the selector is used as an unchecked index into
`0x0130c9a8` (`MOV EAX,[EDI·4 + 0x0130c9a8]` at `0x00a541ad`). Tranche 10's constant read: `[0]` = null, `[1]` =
`0x00a3b7e0`, `[2]` = `0x00807070`, `[3]` = `0x00a3b810`, `[4]`–`[7]` = null, `[8]`/`[9]` = `0xffffffff`. **Only 1, 2
and 3 are safe** on the non-icon-kind path; 0 and 4–7 call address 0, 8–9 call `0xffffffff`, anything else calls
whatever dword follows. (On the icon-kind path the selector is only tested bitwise inside `0x0093d3a0` — no index.)

### C.2 `minimap_icon_remove_radius_do` → `0x00a54430` (shared with `minimap_icon_remove_do`)

**Arguments / return / body:** identical to tranche 04 E.3 `minimap_icon_remove_do`, because it **is** that function
(section A; CONFIRMED — second range run, both registrar slots store `0x00a54430`). Arg 1 object name, arg 2 nil-gated
selector (default 3); icon-kind objects get `0x0093d3a0(−1, −1, −1, 0.0, selector)`; other kinds call
**`0x0130cb98[selector]`** with the object's handle. It removes whatever icon the object carries — there is no
radius-specific handling.

**Crash shape — CONFIRMED (tranche 04 E.3 / tranche 10 D.4, re-read here):** the same unchecked selector index, into
`0x0130cb98` (`[0]` null, `[1]` `0x008070b0`, `[2]` `0x008070d0`, `[3]` `0x00807140`, `[4]` = the live
`open_vint_dialog_check_done` result `0x0130cba8`); the index read is at `0x00a544cf`. Unlike C.1, the remove path
**does** return early on a missing name, a failed lookup or a dead object (`0x00a5447e`, `0x00a5448f`, `0x00a5449c`)
— CONFIRMED in this tranche's own dump.

**Binding note:** a reimplementation should register both names to one function; a script that pairs
`minimap_icon_add_radius_do` with `minimap_icon_remove_do` (or `…_remove_radius_do` with a plain `minimap_icon_add_do`
icon) behaves identically either way.

### C.3 `message_remove` → `0x00a52a30`

**Arguments:** 1 number = message handle (truncated, unconditional); 2 nil-gated boolean = "remove immediately",
default **true**. **Return:** 0 values. CONFIRMED — listing.

**Body:** `0x007ff1f0(handle, immediately)` (CONFIRMED — listing): a handle of 0 is ignored; the slot index is `handle &
0x1fff`, which must be below **60** (`0x3c`); the 72-byte (`0x48`) slot at `0x022be198 + 72·index` must still hold
exactly this handle at `+0x20` (a generation check — a stale handle from a message that was already removed and whose
slot was reused does nothing). Then:
- immediately = true → tail-jumps to `0x007fee20` (not dumped; HIGH CONFIDENCE: unlink and free the message now);
- immediately = false → if the slot's state `+0x38` is not already 4, sets it to **4** and its float `+0x14` = 0.0
  (HYPOTHESIS: state 4 = fading out, `+0x14` = the fade timer restarting).

**No crash shape** (bounded and generation-checked; a negative number is masked to a positive slot and then fails the
exact-handle comparison). Note the slot count: at most 60 live messages.

### C.4 `open_vint_dialog_do` → `0x00a58e00`

**Arguments:** 1–4 strings (all unconditional): title key, body key, option-1 key, option-2 key. **Return:** 0 values.

**Body (CONFIRMED — listing; already read by tranche 10 F.3 without its name, re-confirmed here):** each key is hashed
(`0x00d9e7e0`, which returns 0 for a null string — second follow-up dump) and looked up in the help-text table whose
object pointer is `0x026e7e68` (`0x00604740` → index or −1; `0x00604500(index)` → text, or the literal `"NULL"` for an
out-of-range index, so an unknown key shows the word NULL rather than crashing). It sets the result global
`0x0130cba8` = **−2** ("pending"), pops a dialog from the four-slot pool with `0x007c3310(title, 2, 0, 1)` and, only if
one was free, sets the body (`0x007c18a0`), adds two options (`0x007c2ac0(text, 0, 0)` twice) and installs the
completion callback **`0x00a58d40`** at dialog `+0x13c`.

**Correction to tranche 10 F.3 (CONFIRMED — second range run, `0x00a58d40`-`0x00a58d4e`):** the callback is five
instructions long: it writes `0x0130cba8` **only when its second argument equals −1**, and the value written is that
−1. It never stores an option index. Tranche 10 F.3's "afterwards the callback's value (HIGH CONFIDENCE: the chosen
option's index)" is therefore wrong: `open_vint_dialog_check_done` can only ever report **−2** (pending, also the file
value and the value before any dialog) or **−1** (the dialog finished with argument −1). Which dialog events pass −1
— every close, or only cancel — is OPEN (it depends on the dialog code that calls `+0x13c`, not dumped); if choosing an
option passes its index instead, **a script that waits for "not −2" after the player picks an option waits forever**.
Either way the script cannot learn **which** option was chosen through this pair of bindings.

**Crash / hang shapes:**
- **Pool exhausted** (`0x007c3310` returns null when all four dialogs are in use): handled — nothing is shown — but the
  result stays −2, so a polling script never finishes (tranche 10's L6, re-confirmed). CONFIRMED structure.
- **Help-text table absent:** `0x026e7e68` is used as `this` with no null check (`0x00a58e52`-`0x00a58e60`), while its
  sibling `mission_help_table_do` checks it (tranche 10 F.3 side note). CONFIRMED shape; reachability OPEN (written once
  by `0x00a1ff70`).

### C.5 `object_indicator_init_lua` → `0x008f8370`

**Arguments:** none read. **Return:** 0 values. Registered alone by `0x008f8710` (section A). CONFIRMED — follow-up
dump.

**Body (CONFIRMED — listing):**
1. **Once per process** (gated on `0x025f38f8` = 0): creates three engine callback objects with `0x00e2a740(name,
   function)` — `"object_indicator_update"` → `0x008f5db0` (stored `0x025f38f8`), `"object_indicator_remove"` →
   `0x008f8270` (`0x025f38fc`), `"object_indicator_title_fade_cb"` → `0x008f6f70` (`0x025f3900`).
   `0x00e2a740` takes a node from the free list `0x02a74ff0`, initialises it through its virtual `+0x18`, and moves
   it to the live list `0x02a74fec`; **it returns null when the free list is empty**.
2. In the **current interface document** (the document handle at `+0x14` of the current Lua-callback context
   `0x00e0ceb0()`, resolved by `0x00e1f330`, which bounds the handle to 64 documents of `0x5a8` bytes and checks the
   stored handle at `+0x580`), it looks up elements by name (`0x00e288a0`, a 64-bucket hash) and caches them:
   `"oi_grp"` → `0x025f38e4`, `"oi_pulse_anim"` → `0x025f38e8`, `"pulse_circle"` → `0x025f38ec` (then sets its
   `"visible"` property false via `0x00e28d50`), `"title_grp"` → `0x025f38f0` (also set invisible),
   `"title_fade_out_anim"` → `0x025f38f4`.
3. **Language tweak:** `0x00849920()` returns the current language code (`0x01301a90` indexes a string table at
   `0x0116409c`; negative → `""`). Unless it is `"JP"` or `"US"` (case-insensitive `__stricmp`), it finds
   `"title_txt"` and `"body_txt"` under the title group (`0x00e28c30`) and sets `"text_scale"` = (0.4, 0.4) on the
   title (constant `0x011192f8` = 0.4) and `"wrap_width"` = 320 on both — i.e. **non-US, non-Japanese builds shrink the
   indicator title and wrap both texts at 320 units** (CONFIRMED values; HIGH CONFIDENCE for the purpose: longer
   translated strings).
4. Subscribes the callbacks to the `"object_indicator"` event in the current document with `0x00e25250`: the update
   callback for event ids **4** and **3**, the remove callback for event id **5** (the title-fade callback is created but
   not subscribed here). `0x00e25250` updates an existing subscription instead of adding a duplicate
   (`0x00e24ba0`/`0x00e24c40`), so calling this twice does not double-register.

**Crash shapes (CONFIRMED shape, reachability HYPOTHESIS):**
- **No current callback context:** `0x00e0ceb0` returns 0 when the context depth `0x02a44d10` is 0 or above 16, and
  the root reads `[0 + 0x14]` (first at `0x008f83ef`). It is normally called from a document's own script, so a
  context exists.
- **Missing elements:** `0x00e288a0` returns null for an element name the document lacks, and `0x00e28d50` /
  `0x00e28c30` are then called with a null `this` — `0x00e28d50` immediately reads the vtable (`MOV EAX,[EDI]` at
  `0x00e28d56`). A reimplementation loading a modified or partial indicator document must check each lookup.
- **Callback pool empty on the first call:** `0x00e2a740` returns null, `0x025f38f8` stays 0, and step 4 reads
  `[0 + 0x18]` at `0x008f866d`.

---

## D. Pause map — 4 functions (registrar `0x007dfae0`, `spec-lua-api-behaviour.md` §31)

### D.1 `pause_map_is_stag_mode` → `0x007d98b0` and D.2 `pause_map_is_tutorial_mode` → `0x007d98e0`

**Arguments:** none read. **Return:** exactly 1 boolean each: byte `0x0229a317` (stag mode) and byte `0x0229a318`
(tutorial mode) respectively. CONFIRMED — listings.

Both flags are already characterised: stag mode in `spec-lua-api-behaviour.md` §31.5 (set only by `0x0071b640`, the
reward-type-48 "free district takeover" path; cleared by D.4 and by the close handler `0x007dab20`); tutorial mode in
§32.1 (`pause_map_tutorial_mode` writes it through `0x007d9ab0`/`0x007d9ac0`). The third follow-up xref run lists all 11
references to `0x0229a318`: the two setters, the close handler `0x007dab20`, this getter, and seven pause-map readers
(`0x007d9280`, `0x007d9a70`, `0x007db5b0`, `0x007dea30`, `0x007df420` twice, `0x007df8a0` — the bookmark function) —
matching §32.1's "read by 7 further functions". These two getters were the only part of the pair without a spec entry;
both are trivially safe.

### D.3 `pause_map_drag_map` → `0x007d9910`

**Arguments:** none read. **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing, constant read):**
1. `0x00dcfcc0(&dx, &dy, &wheel)` copies the mouse deltas `0x02a381d8` / `0x02a381d4` / `0x02a381d0` into the three
   locals **only when bit `0x10` of the input-flags byte `0x0132abc0` is set**; otherwise it writes nothing.
2. If dx or dy is non-zero: scale = 1.0 (`0x012a2d70`) ÷ zoom (`0x012ff678`, file value 0.5); map centre X
   `0x0229a4e0` += dx · scale, centre Y `0x0229a4e4` −= dy · scale (HYPOTHESIS: screen Y grows downward while map Y
   grows upward).
3. Clamps X to [`0x0229a4e8`, `0x0229a4f0`] and Y to [`0x0229a4ec`, `0x0229a4f4`] (the four bounds are written only
   by `0x007d9280`, the map/zoom setup), then calls `0x007d8ce0`, which recomputes the derived view offsets
   `0x0229a2bc`/`0x0229a2c0` from the centre, the zoom and fixed screen constants.

**Logic defect — CONFIRMED structure (listing `0x007d9914`-`0x007d9942`):** the three locals are **never initialised**
before `0x00dcfcc0`. When the `0x0132abc0` bit is clear, dx and dy are whatever the stack held, so the map can jump by
garbage amounts (or, by luck, not move). Whether that bit can be clear while the pause map is open on PC (e.g. with a
gamepad as the active device) is OPEN (34 references to `0x0132abc0`; its writer was not traced). A reimplementation
should zero the deltas first.

**Edge (CONFIRMED arithmetic):** a zoom of 0 would make the scale infinite. The zoom `0x012ff678` has 36 references
(the dump's capped list shows three writes inside `0x007d9280`); whether any writer can store 0 is OPEN. NaN deltas
cannot arise from integer mouse deltas.

### D.4 `pause_map_stag_takeover_do_reward` → `0x007de1c0` — refinement of `spec-lua-api-behaviour.md` §31.6

Re-read in this tranche's dump; §31.6's description is CONFIRMED (cash `+0x1ca0` ÷ 100.0 — constant `0x012a2dd8` =
100.0 — and respect `0x009db890` = player `+0x1ed0` captured before and after; every member of the selected zone
claimed through `0x005e97a0`; `0x008b85d0`; summary `0x007ef9a0`; stag byte `0x0229a317` cleared at `0x007de2e3`;
autosave `0x00b95060`). Two precisions for the crash table:
- **The selected zone pointer itself is unchecked**, not only its `+0x44`: `0x007de1fc` loads `0x0229a2ac` and
  `0x007de202` reads `[zone + 0x44]`. §31.2/§31.3 establish that `0x0229a2ac` is **0 whenever nothing is selected**
  (and is cleared by the teardown `0x007db3e0`), so calling this binding before a selection reads address `0x44`.
  CONFIRMED.
- The local player (`0x009da4e0`) is used unchecked at `0x007de1d8` (`[player + 0x1ca0]`). CONFIRMED shape.

---

## E. PC options and the Killbane choice — 2 functions

### E.1 `options_display_pc_set_options` → `0x007cca80`

**Arguments:** 12, **all unconditional** (missing booleans read false, missing numbers 0; every number truncated):
1 boolean **VSync**; 2 number **resolution width**; 3 number **resolution height**; 4 boolean **fullscreen**; 5 number
**MSAA**; 6 number **anisotropic level**; 7 number **lighting detail**; 8 number **shadow detail**; 9 number **scene
detail**; 10 number **reflections**; 11 number **post processing**; 12 number **ambient occlusion**. The labels are
the engine's own: each value is reported under the literal strings `"VSync"`, `"Resolution X"`, `"Resolution Y"`,
`"Fullscreen"`, `"MSAA"`, `"Anisotropic"`, `"Lighting Detail"`, `"Shadow Detail"`, `"Scene Detail"`,
`"Reflections"`, `"Post Processing"`, `"Ambient Occlusion"` (and `"Performance Preset"`). **Return:** 0 values.
Registered alone by `0x007ccfd0` (section A). CONFIRMED — follow-up dump (listing; argument order checked against the
stack-slot arithmetic of the listing as well as the decompile).

**Body (CONFIRMED — listing):**
1. `0x005dcc00(0, vsync)` stores VSync at once (byte `0x012ec240`).
2. Reads the current mode (`0x005deed0`, virtual `+0xbc` of the render-device wrapper `0x0351f8f8`). If **any** of
   width, height, VSync, MSAA, fullscreen, lighting detail + 1 or the raw arg 12 differs from the current mode, it
   files a **mode-change request** with `0x005dd240(width, height, &{MSAA, fullscreen, VSync, lighting + 1, arg 12})`
   (stores `0x012ec1a8` = width, `0x012ec1a4` = height, the block at `0x0149ab50`, and raises the pending byte
   `0x0149abdc` = 1, `0x0149abdd` = 0) and saves the **previous** settings for a revert into `0x02296f78`-`0x02296fa8`
   with the flag `0x02296fad` = 1 (old width/height, old mode block, old anisotropic index, old shadow index, old
   scene/reflection/post/AO values and the old performance preset). Those globals are read back only by `0x007cb610`
   (HIGH CONFIDENCE: the "keep these settings?" revert path), which also clears the flag.
3. Reports every setting's old and new value through `0x00bda860` (numbers) / `0x00bda7f0` (booleans) — both gated on
   the bytes `0x013123d1` (file value 1) and `0x029897a5`, hashing the label and posting a record to the sink
   `0x02949788` (HYPOTHESIS: an analytics/telemetry event; it has no effect on the settings).
4. Applies the detail settings:
   - anisotropic (arg 6) → `0x007cab80`, register-passed: 1→`0x005dd3f0(1)`, 2→2, 3→4, 4→8, 5→16; **anything else →
     2** (bounded jump table);
   - shadow detail (arg 8) → `0x007caae0`, register-passed: 1 → size `0x400` with shadows **off** (`0x0149ab4c` = 0);
     2 → `0x400`, 3 → `0x800`, 4 → `0x1000`, all with `0x0149ab4c` = 2; **anything else → `0x400`, on** (bounded);
   - scene detail (arg 9) + 1 → `0x005dd2c0`, which stores it in `0x012ec1b0` and picks LOD scale factors for < 3, = 3,
     > 3 and > 4 (`0x00a71250`) — any integer accepted;
   - reflections (arg 10) → `0x012ec1b4` raw (`0x005dd380`);
   - post processing (arg 11) − 1 → `0x012ec1ec` raw (`0x005dd3a0`);
   - ambient occlusion: min(arg 12, 2) − 1 → `0x012ec1f0` (`0x005dd360`) — **clamped above only**;
   - commits the performance preset: `0x012fdd50` = `0x012fdd4c`.

**Range notes (CONFIRMED structure):** reflections, post processing and the lower end of ambient occlusion are stored
with **no range check** (arg 12 = 0 stores −1; arg 11 = 0 stores −1); what the renderer does with out-of-range values is
OPEN. Anisotropic and shadow detail are safely defaulted.

**Crash shapes (CONFIRMED shape, reachability OPEN):** `0x005dedd0(width, height, …)` is called on the
**script-supplied** width/height to look the mode up for the report in step 3:
- for a **negative width** it takes a thread-local branch that reads the object at TLS `+0x674`, and when that pointer is
  null it still dereferences it (`JZ` to `0x005dedf7`, `MOV EDX,[EAX+0x8]` with EAX = 0) — a null read;
- for a non-negative width it calls virtual `+0xac` of `0x0351f8f8` and then **divides** two of the returned fields
  (`IDIV` at `0x005dee59`); if the wrapper leaves the denominator 0 for a resolution it does not know, that is an
  integer divide-by-zero. Whether the wrapper can do so is OPEN (WALLS.md: the `0x0351f8f8` vtable is not the public
  device interface, so no API semantics are assumed here).

### E.2 `msn_killbane_selected_option` → `0x00840e90`

**Arguments:** 1 number = the chosen option (truncated, unconditional). **Return:** 0 values. Registered alone by
`0x00840ef0`; `0x00840e90` has no Ghidra function boundary and was read with the second range run. CONFIRMED.

**Body (CONFIRMED — listing, third follow-up dump):** looks up **hook slot 12** (`0xc`) of the global named-hook record
`0x026e9528` with `0x005e44c0` and, if that returns a call record, pushes the option as a Lua number onto it
(`0x00e0ce40`: `lua_pushnumber` on the record's state at `+0x4`, argument count `+0x1c` incremented).

Slot 12 of `0x026e9528` is exactly the slot **`on_m21_player_choice`** registers its callback name into (tranche 10 C,
slot literal at `0x00a56fa6`). So this is the UI side of mission 21's Killbane choice: **the choice screen hands the
selected option number to whatever script function the mission registered with `on_m21_player_choice`.**
`0x005e44c0` is the shared "find the registered script callback" step of tranche 06 C.1 / tranche 08 E.10: on the
**host** (session exists and host) it prepares the call with `0x00a1f990(slot value, 0)`; on a **client or with no
session** it returns null when the slot is −1 and otherwise goes through `0x00a295a0(2, record +0x40, record +0x44,
12)` (the remote-call path). When the prepared call actually runs is OPEN (same OPEN as tranche 06 C.1).

**Edges (CONFIRMED structure):** with no callback registered, nothing happens on a client; on the host
`0x00a1f990` receives −1, which `spec-lua-bindings.md` §8.5 records as guarded inside that function (HIGH CONFIDENCE —
not re-dumped here). No crash shape in this body.

---

## F. Characters, crew, melee and notoriety — 7 functions

### F.1 `npc_is_in_brute_grab_qte` → `0x00a55140`

**Arguments:** 1 string = character (`0x00a28150`: `#FOLLOWER#` sentinels or a character name; not the `#PLAYER#`
chain). **Return:** exactly 1 boolean: true iff the character resolves **and its byte `+0x445` equals `0x36` (54)**;
false otherwise. CONFIRMED — listing. HYPOTHESIS: `+0x445` is the character's current action/animation-state id and 54
the brute grab QTE state. No crash shape (resolver result null-checked).

### F.2 `npc_dont_auto_equip_or_unequip_weapons` → `0x00a55360`

**Arguments:** 1 string = character (`0x00a28150`); 2 boolean (read **unconditionally** — missing = false, not the
default-true idiom). **Return:** 0 values. CONFIRMED — listing.

**Body:** if the character resolves, `0x004e12e0(flag)` with `this` = the AI-data sub-object (character `+0x2b0`):
null `this` tolerated; the double-gate setter shape on the handle at sub-object `+0x610/+0x614`; locally it writes
**bit 2 (`0x04`) of byte `+0xb` of the sub-object** (character `+0x2bb`); otherwise, with an owner, it sends opcode
`0x46` tagged `"human_ai_data"` / `"ai_force_flagsdont_auto_equip_or_unequip_weapons"`; with neither, nothing.
CONFIRMED — listing. The same debug tag is also referenced by `0x008b14c0` (HIGH CONFIDENCE: the receive/apply side).

### F.3 `npc_brute_set_no_downed_finisher` → `0x00a55210`

**Arguments:** 1 string = character (`0x00a28150`); 2 nil-gated boolean, **default true**. **Return:** 0 values.
CONFIRMED — listing.

**Body:** acts only if the character resolves **and** `0x0052a230(character)` is true — bit 0 of byte `+0x48` of the
record at character `+0xf0` (HYPOTHESIS: the character's type/template record, bit 0 = "is a brute"). Then
`0x0094a690(flag)` with `this` = the character: double-gate on the character handle `+0x8/+0xc`; locally **bit 22
(`0x400000`) of character `+0x1c9c`** (the same force-flags dword as `set_seatbelt_flag` / `set_trailing_aim_flag`,
`spec-lua-api-behaviour.md` §32.2); remotely opcode `0x46` tagged `"human"` / `"human_force_flagsno_brute_finisher"`.
CONFIRMED — listing. Calling it on a non-brute is a silent no-op.

**Crash shape (CONFIRMED shape):** `0x0052a230` reads `[character + 0xf0] + 0x48` without checking `+0xf0`. Every
resolved character presumably has that record (HYPOTHESIS), so this is shape only.

### F.4 `npc_aim_at_point` → `0x007c9f50` — no-op

See section A: the shared no-op stub. Every argument is ignored and nothing is returned. CONFIRMED.

### F.5 `party_dismiss_do` → `0x00a5b0e0`

**Arguments:** 1 table of character names. **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing):**
1. Count = `0x0083dff0(L, index of arg 1)`: −1 if the slot is absent or nil; otherwise the table's numeric field `"n"`
   (key string `0x0129d9cc`) if that is a number (converted by `0x00ea2560`); otherwise the number of entries counted with
   `lua_next`.
2. For i = 1 … count: `t[i]` is fetched (`lua_gettable`), read as a string (non-string → null), popped, resolved with
   `0x00a28150`, and passed to `0x0097e610`.

`0x0097e610(character)` (CONFIRMED — listing, second follow-up dump): null → nothing. If the character **is a player**
(`0x00853b30(c, 0x21)`) it goes straight to the last step. Otherwise: `0x005088c0` finds the leader of the character's
squad (squad index byte `+0x994` < 40, squad record `0x013b8780 + 0xa8·index` with a non-null `+0x10`, then
`0x00508640`); if there is one, `0x0097e570(character)` re-scales the character's hit points (ratio `+0x1cb8` ÷
`+0x1cac`, then `0x0094d380`/`0x0094cfc0`, at least 1.0 — HIGH CONFIDENCE: restoring its own health scale when it
leaves the player's crew), and **if that leader is the local player** and character byte `+0x1da5` bit `0x10` is
clear, `0x00bda680` posts a telemetry record (same gates and sink `0x02949788` as E.1 — HYPOTHESIS: "homie dismissed"
analytics). Last step, always: `0x009d95a0(character, 0)` — tranche 16 D.1's **"remove from the player's crew"**
routine (owner request when not local; local restore and removal when the leader is a player).

**Edges (CONFIRMED structure):** names that do not resolve, non-string entries and holes are skipped silently; an `n`
larger than the real length just reads nils. **A non-table arg 1** (for example a single name string) makes
`lua_gettable` index a non-table — a Lua error raised inside the binding (HIGH CONFIDENCE: the standard Lua 5.1
behaviour, not dumped), so a script that passes one name instead of a table aborts rather than dismissing. Passing a
player is accepted and goes straight to `0x009d95a0(player, 0)` (OPEN what that does to a player).

### F.6 `melee_get_move_id_from_name` → `0x00a52820`

**Arguments:** 1 string = melee move name (unconditional). **Return:** exactly 1 number. CONFIRMED — listing.

**Body:** `0x009828f0(name)`: a null name returns **65535** (`0xffff`); otherwise the name is hashed with the front
matter's name hash `0x00d9e8b0(name, 0, −1)` and `0x00982780` scans the move table (base `0x0262ba38`, count
`0x0262ba3c`, stride `0x11c`, hash at `+0x20`) for it, returning the **0-based index** or **65535** when not found. The
low 16 bits are pushed as a number. CONFIRMED — second follow-up dump. So scripts must treat **65535 as "no such
move"** (not −1 or nil). No crash shape (bounded scan; null name handled).

### F.7 `notoriety_get` → `0x00a55950`

**Arguments:** 1 string = notoriety track name (unconditional). **Return:** **1 number, or 0 values** — see below.
CONFIRMED — listing.

**Body:** `0x00605160(name)` — the cross-title alias resolver of `spec-lua-api-behaviour.md` §6.22 / §8.18 (constant read
re-confirms the three 4-entry tables at `0x012ed130`/`0x012ed140`/`0x012ed150`: slot 0 = `"luchadores"` /
`"brotherhood"` / `"los_carnales"`, slot 1 = `"deckers"` / `"ronin"` / `"vice_kings"`, slot 2 = `"morningstar"` /
`"samedi"` / `"rollers"`, slot 3 = `"police"` in all three) → 0-3, or **5** for no match.
- Slot 0-3 → pushes `0x00605200(slot)` = the dword at `0x014a4f70 + 0x4c·slot` as a number (HYPOTHESIS: the track's
  current notoriety level; the field's writers were not traced).
- **No match (5) → returns with nothing pushed.** CONFIRMED (`0x00a55974`-`0x00a5597c`).

**Logic defect — CONFIRMED structure:** an unknown or misspelt track name makes `notoriety_get` return **no value**, so
the script sees nil; arithmetic or comparisons on the result then raise a Lua error in the script. A binding layer
should decide explicitly (nil, or 0 as `0x00605200` itself returns for an out-of-range slot).

**Crash shape (CONFIRMED path, outcome depends on the CRT handler):** a non-string argument gives a null name, which
reaches the CRT `__stricmp` inside `0x00605160` unchecked (`0x00605179`). The follow-up dump includes `__stricmp`
(`0x00eaa178`): in the default locale (`0x02cc473c` = 0) a null argument goes to `__invalid_parameter` (errno 22) and,
if that returns, yields `0x7fffffff` ("no match"). With the stock release-CRT handler that call **terminates the
process**; only if the game installs a handler that returns does the lookup fall through to "no match" (→ no value,
above). Whether SR3 installs such a handler is OPEN. This is the same hazard as tranche 10 D.3 item 3, now with the CRT
side read.

---

## G. Mission state and world reset — 2 functions

### G.1 `mission_maybe_uncomplete_m22` → `0x00a53500`

**Arguments:** none read. **Return:** 0 values. CONFIRMED — listing.

The three mission names are short strings stored as single dwords: `0x01117da4` = `"m23"`, `0x011169a0` = `"m24"`,
`0x0113ab90` = `"m22"` (decoded from the static values `0x0033326d`, `0x0034326d`, `0x0032326d`, as tranche 16 B.2
decoded `"m01"`).

**Body (CONFIRMED — listing):**
1. A = `"m23"` is the active mission (`0x006d7240`, B.3's test) **or** `"m23"` is completed (`0x006d6f30`: the mission
   object resolves through `0x005f4c30`, is alive, and **bit 2 (`0x04`) of its byte `+0x88`** is set). B = the same for
   `"m24"`.
2. **Only when exactly one of A and B holds** (A xor B): resolve `"m22"` (`0x005f4c30`, alive, else null) and
   - `0x006cfc70(m22)`: if non-null **and this machine is the host** (session exists, `+0x5c` == `+0x58`), clears bit
     `0x04` (the "completed" bit read above) and sets bit `0x40` of `m22 +0x88`;
   - **always** (whether or not anything was cleared, and on host and client alike) broadcasts a record: opcode
     `0x43`, 8-bit sub-type **`0x1d`**, the identity of `m22` (`0x00a017c0`, a zero pair when null), and one boolean bit
     **0**, committed with `0x0086f1b0(session, 0, 0)` and closed (`0x0086eb20`); with no session the commit does
     nothing.
3. Both or neither → nothing at all.

So it **un-completes mission 22 when the player has progressed down exactly one of the m23/m24 branches** (HYPOTHESIS:
m23 and m24 are the two mutually exclusive continuations after m22, and bit `0x40` marks m22 as available again /
replayable — the bit's readers were not traced). CONFIRMED that the gate is an exclusive-or of "active or completed".

**Network / logic notes (CONFIRMED structure):**
- **Client:** the local un-complete is skipped (host-only), but the client **still broadcasts** the `0x43`/`0x1d`
  record. Whether the receiver applies it from a client is OPEN.
- **No session (single player without a host session):** `0x006cfc70`'s host test fails, so **nothing is changed** —
  the same "needs a host session" pattern tranche 16 B.3 found for the stronghold upgrade. If single player always runs
  with a host session (tranche 16's HYPOTHESIS) this works; otherwise this binding is a no-op in single player.
- Idempotence: after the first call m22's completed bit is clear, so a second call changes nothing more but sends
  another record.

No crash shape (`m22` null-checked in `0x006cfc70`; the serialiser tolerates null).

### G.2 `mesh_mover_reset_by_zone` → `0x00a52c20`

**Arguments:** 1 string = zone name (unconditional). **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing, second follow-up dump):**
1. Zone lookup `0x0085fd10(name)`: bucket = `0x00dab330(name, 0x3f0)` (a case-insensitive ×33-xor string hash taken
   modulo 1008), then `0x0085c410` walks that bucket's chain in the zone directory `0x0244abb8` comparing names through
   a virtual call; the 16-bit result indexes the zone-pointer table `0x0244cb40`, or `0xffff` → null → **nothing done**.
2. The zone's 16-bit id at `+0x134` must pass `0x00861ec0`: at least one of its top three bits (`0xe000`) set, then
   `0x00861e60` (id `0xdfff` → `0x0085fd50`; otherwise `0x0085f8b0` on `0x02444f50`) — HYPOTHESIS: "the zone is
   currently loaded". Else nothing.
3. For every entry of the zone's object array (count `+0x24`, pointers `+0x28`) whose flags `+0x64` have bit `0x100`
   and whose handle `+0x0/+0x4` is non-zero: resolve the handle (`0x00458230`, `+0x33` bit `0x10` rejection), require
   kind row `+0x6` bit `0x20` (HIGH CONFIDENCE: the **mesh-mover** kind, from the binding's name) and liveness, and
   require `0x00a078a0(mover)` = null (that helper resolves the handle at mover `+0x10/+0x14` to a live object of kind
   row `+0x9` bit `0x04`; HYPOTHESIS: an object the mover is attached to or held by — such movers are skipped). Then:
   - `0x00a0a9f0(mover)`: unlinks the mover from a circular list (`+0xe8`/`+0xec`, head `0x0268d898`) if it is on it;
   - `0x00a0a790` (`this` = mover) — the **reset**: virtual `+0x40` restores the transform saved at `+0xfc`/`+0x108`
     (`0x00a05510`, HIGH CONFIDENCE), clears state bits on the mover (`+0xdd` bit 1) and on its zone entry (`+0x170 →
     +0x64`, mask `0xffffff0b` and bit 22), writes 1.0 to field `+0x3c` of the entry's `+0x10` sub-record (or of the
     object `0x00581310` returns for it), refreshes visibility
     and attached children (`0x008ccb90`, `0x008e07d0`), sets `+0xf0` = −1, re-inserts the mover into the list when its
     `+0x33` bit `0x08` is set (`0x00a0b780`), re-poses its physics body when it has one (`+0xb4` ≠ −1), and releases
     the handle pair `+0x168/+0x16c` (`0x005cfe00`) back to the "none" pair `0x01177dc8`.
4. Finally `0x00582ae0()` raises the byte `0x013cec32` (consumed and cleared by `0x00586230`; HYPOTHESIS: "movers
   changed — rebuild").

**Crash — CONFIRMED (second follow-up dump):** `0x00dab330` reads the first character of the name **without a null
check** (`MOV AL,[EDI]` at `0x00dab336`). A nil or non-string argument makes `lua_tolstring` return null, so
**`mesh_mover_reset_by_zone(nil)` reads address 0.** An unknown zone name is handled (step 1 returns null).

**Inconsistency — CONFIRMED structure, reachability HYPOTHESIS:** `0x00a0a790` dereferences the zone-entry pointer
`+0x170` unconditionally at its start (`0x00a0a798`/`0x00a0a79e`, also inside `0x00a05510` at `0x00a05547`) but tests it
for null later (`0x00a0a802`). On this binding's path `+0x170` should always be the entry the mover was found
through, so this is a latent hazard for other callers (`0x008cbe10`, `0x00a0c340`, `0x00a30ad0`, `0x00a323a0`) rather
than for this name.

**Network note (CONFIRMED structure):** there is no host gate in the binding; each machine resets its own movers. The
only network-facing step is the handle release `0x005cfe00` (a caller of the record opener `0x0086f5f0` per that
function's caller list; not dumped here).

---

## H. Method notes for the follow-up runs

- Range run: `0x007ccfd0-0x007cd010`, `0x008f8710-0x008f8750`, `0x00840ef0-0x00840f30`, `0x007af8d0-0x007af9a0`,
  `0x007dfae0-0x007dfbc0`, `0x007c9fc0-0x007ca150`.
- Xref run: `0x007ccfd0 0x008f8710 0x00840ef0 0x007af8d0 0x007dfae0 0x007c9fc0` (each called exactly once, from the UI
  registrar `0x008430f0`) and `0x007c9f50`.
- Follow-up dump (depth 1, `maxfuncs:25 maxinsn:600`): `0x007cca80 0x008f8370`.
- Second follow-up dump (depth 0, `maxinsn:900`): `0x00982780 0x00dab330 0x0085c410 0x00861e60 0x00a05510 0x007c5a50
  0x00a73e40 0x00843300 0x00e1f2f0 0x005088c0 0x0097e570 0x00bda680 0x00d9e7e0 0x00e0ce00 0x00a73bd0`.
- Second range run: `0x00840e90-0x00840ef0` (E.2's handler, no function boundary), `0x00a58d40-0x00a58e00` (C.4's
  completion callback), `0x00a53f90-0x00a54110` (`minimap_icon_add_do`, for the C.1 comparison), `0x00a23280-0x00a232e0`
  (the gameplay-registrar slots around the shared remove handler).
- Constant read (`ptrs count:12`): `0x012ed130` (alias tables), `0x012a2d70` (double 1.0), `0x012a2dc0` (double 0.5),
  `0x012a2dd8` (double 100.0).
- Third follow-up dump (depth 0): `0x005e44c0 0x00e0ce40 0x00842c50 0x0085fd10`; third xref run (`xrefs:40`):
  `0x026e9528 0x0229a318 0x0229ce30 0x02317b5c 0x022425f9`; third range run: `0x007af4c0-0x007af530` (B.1's reader),
  `0x007e1e30-0x007e1e50`, `0x007e1fb0-0x007e1fd0`, `0x007e1ff0-0x007e2010` (B.1's three clears).
- The one-name registrar handlers were taken from the `PUSH imm32` before `lua_pushcclosure`, never from the dumper's
  offset guess (section A).

---

## I. Cross-function observations

1. **Three more pointer-before-name one-name registrars**, all under the UI bring-up `0x008430f0` (`0x007ccfd0`,
   `0x008f8710`, `0x00840ef0`) — six in this series now (tranche 13's `0x0067d660`, tranche 16's `0x00a03600` /
   `0x007aff80`). The tell is unchanged: a `lua`-mode "use" that is a `PUSH name` right before `CALL 0x00dfe830`. One of
   the three handlers (`0x00840e90`) has no Ghidra function boundary at all — a `func`-mode dump of it would have failed.
2. **Two names that are not real bindings.** `npc_aim_at_point` is the shared no-op stub, and
   `minimap_icon_remove_radius_do` is a second name for `minimap_icon_remove_do`. Name-based expectations ("aims",
   "removes the radius") would mislead a reimplementation; the registrar slots are the ground truth.
3. **Sibling pairs where one sibling checks and the other does not** — the most useful fix templates in this tranche:
   - `minimap_icon_add_do` returns early on a missing object; `minimap_icon_add_radius_do` reads the handle out of a
     null object (C.1);
   - `minimap_icon_remove_do`/`…_radius_do` return early (C.2); the add-radius path does not;
   - `mission_help_table_do` checks the help-text table; `open_vint_dialog_do` does not (C.4, tranche 10);
   - `pause_map_stag_current_district_control` tolerates "nothing selected"; `pause_map_stag_takeover_do_reward` reads
     through the null selection (D.4).
4. **"State flag set even when the object it describes failed to be created"** — a new shape for this series:
   `0x007adc00` stores a null video and still sets the playing flag, so the crash happens on the *next* call (B.5).
   Worth grepping other `create → store → set active` sequences for the same ordering.
5. **Unchecked string arguments reaching a hash or compare primitive.** `0x00dab330` (G.2) reads a null name;
   `0x00605160` hands a null name to the CRT `__stricmp` (F.7). By contrast `0x00d9e740`, `0x00d9e7e0` and
   `0x00d9e8b0` all return 0 for null (C.4, C.5, F.6), so the safe/unsafe split is per primitive, not per binding
   family. A binding layer that rejects non-string arguments up front closes all of them.
6. **Return-shape surprises a script can observe:** `notoriety_get` returns **no value** for an unknown track (F.7);
   `melee_get_move_id_from_name` returns **65535**, not −1/nil, for an unknown move (F.6);
   `open_vint_dialog_check_done` can only ever report −2 or −1 (C.4 correction). Exactly-one-boolean returners:
   `pause_menu_cat_mouse_active`, `pause_menu_cant_retry_because_host_in_creation`, `pause_map_is_stag_mode`,
   `pause_map_is_tutorial_mode`, `npc_is_in_brute_grab_qte`. The other 18 return nothing.
7. **Host-only effects with unconditional broadcasts:** `mission_maybe_uncomplete_m22` only changes state on the host
   but broadcasts from every machine (G.1). Combined with tranche 16 B.3, a second "does nothing without a host
   session" binding — more circumstantial weight for "single player runs with a host session" (still HYPOTHESIS).
8. **Uninitialised-stack read:** `pause_map_drag_map` uses mouse deltas that are only written when an input-flag bit is
   set (D.3) — the first uninitialised-local defect in this series.
9. **Telemetry family:** `0x00bda860`, `0x00bda7f0` and `0x00bda680` share the gate bytes `0x013123d1`/`0x029897a5`
   and the sink `0x02949788` (E.1, F.5). HYPOTHESIS: an analytics channel; it never affects game state, so a
   reimplementation can drop it.
10. **Callback hook slot 12 of `0x026e9528` closes a loop:** `on_m21_player_choice` (tranche 10) registers the callback;
    `msn_killbane_selected_option` (E.2) delivers the player's choice to it.

---

## J. OPEN

- A: whether any other registrar slot (outside this tranche) also aliases `0x00a54430` or `0x007c9f50` under a
  misleading name (the xref run lists 30+ references to the stub).
- B.1: request 0's target in the pause-menu sub-screen dispatch at `0x007af51c`; the writer of `0x012fcae4`.
- B.2: the virtual `+0x8` target of the minigame's `+0x14` object and the meaning of value 14.
- B.3: confirm `"mm_m1_5"` is the character-creation mission (mission table not read).
- B.5: whether `0x00bddbd0` tolerates a null video from `0x02317b5c`; what the boolean `0x022425e8` passed to
  `"main_menu_start_video"` means; `0x007c59f0` (the only non-null completion callback).
- C.1: whether the `0x0130c9a8` table targets ignore the icon id (−1) for a radius request.
- C.3: `0x007fee20` (immediate removal) and the meaning of message state 4.
- C.4: which dialog events call `+0x13c` with −1 (every close, or only cancel) — decides whether
  `open_vint_dialog_check_done` can hang after an option is chosen; whether `0x026e7e68` can be null.
- C.5: whether `0x02a74ff0` (callback-object free list) can be empty on first use.
- D.3: who writes `0x0132abc0` bit `0x10` and whether it can be clear on the pause map; whether the zoom can be 0.
- E.1: what the renderer does with out-of-range reflections/post-processing/AO values; whether TLS `+0x674` can be null;
  whether `0x0351f8f8` virtual `+0xac` can leave a zero denominator; the role of `0x007cb610`.
- E.2: when the prepared call from `0x005e44c0` actually runs (shared with tranche 06 C.1).
- F.1: confirm `+0x445` = 54 is the brute-grab state.
- F.3: confirm `+0xf0 → +0x48` bit 0 is "is a brute".
- F.5: what `0x009d95a0(player, 0)` does when a player is in the table; the identity of the telemetry sink.
- F.7: the meaning of the dword at `0x014a4f70 + 0x4c·slot`; whether SR3 installs a returning CRT invalid-parameter
  handler.
- G.1: readers of `m22 +0x88` bit `0x40`; whether the `0x43`/`0x1d` record is applied when it comes from a client.
- G.2: `0x0085f8b0` (the zone "loaded" test), `0x00a078a0`'s kind (`+0x9` bit `0x04`), and the consumer
  `0x00586230` of `0x013cec32`.

---

## K. Direct-answer table

| # | name | registrar | handler | status |
|---|---|---|---|---|
| 1 | `pause_menu_common_top_finished_loading` | pause menu `0x007af8d0` (under UI) | `0x007af7d0` | resolved — raises `0x0229ce30`, releasing a held invite / cheat-save sub-screen request on pause screen 10 |
| 2 | `pause_menu_cat_mouse_active` | pause menu | `0x007af7a0` | resolved — true iff minigame mode query = 14 |
| 3 | `pause_menu_cant_retry_because_host_in_creation` | pause menu | `0x007af870` | resolved — true iff `"mm_m1_5"` active, session, and not host |
| 4 | `pause_map_stag_takeover_do_reward` | pause map `0x007dfae0` (under UI) | `0x007de1c0` | resolved (§31.6, re-confirmed) — **null selection read at `0x44`** |
| 5 | `pause_map_is_tutorial_mode` | pause map | `0x007d98e0` | resolved — returns byte `0x0229a318` |
| 6 | `pause_map_is_stag_mode` | pause map | `0x007d98b0` | resolved — returns byte `0x0229a317` |
| 7 | `pause_map_drag_map` | pause map | `0x007d9910` | resolved — pans by mouse delta ÷ zoom, clamped; **uninitialised deltas** without the input bit |
| 8 | `party_dismiss_do` | gameplay `0x00a20840` | `0x00a5b0e0` | resolved — dismisses every named character in a table from the crew |
| 9 | `options_display_pc_set_options` | `0x007ccfd0` (under UI) | `0x007cca80` | resolved — 12 PC graphics settings; mode-change request + revert snapshot |
| 10 | `open_vint_dialog_do` | gameplay | `0x00a58e00` | resolved — two-option dialog; **result can only be −2/−1** (corrects tranche 10 F.3) |
| 11 | `object_indicator_init_lua` | `0x008f8710` (under UI) | `0x008f8370` | resolved — creates indicator callbacks, caches elements, language tweak |
| 12 | `npc_is_in_brute_grab_qte` | gameplay | `0x00a55140` | resolved — byte `+0x445` == 54 |
| 13 | `npc_dont_auto_equip_or_unequip_weapons` | gameplay | `0x00a55360` | resolved — AI-data bit 2 of `+0x2bb`, double-gate; arg 2 defaults false |
| 14 | `npc_brute_set_no_downed_finisher` | gameplay | `0x00a55210` | resolved — brutes only; bit 22 of `+0x1c9c`, default true |
| 15 | `npc_aim_at_point` | gameplay | `0x007c9f50` | resolved — **shared no-op stub** |
| 16 | `notoriety_get` | gameplay | `0x00a55950` | resolved — track record dword; **no value for an unknown track** |
| 17 | `msn_killbane_selected_option` | `0x00840ef0` (under UI) | `0x00840e90` | resolved — hands the choice to the `on_m21_player_choice` callback |
| 18 | `mission_maybe_uncomplete_m22` | gameplay | `0x00a53500` | resolved — m23 xor m24 → host un-completes m22, always broadcasts |
| 19 | `minimap_icon_remove_radius_do` | gameplay | `0x00a54430` | resolved — **same function as `minimap_icon_remove_do`**; unchecked selector index |
| 20 | `minimap_icon_add_radius_do` | gameplay | `0x00a54110` | resolved — icon id 5 / table call with radius; **null-object read at 8**, unchecked selector index |
| 21 | `message_remove` | gameplay | `0x00a52a30` | resolved — generation-checked remove (default immediate) |
| 22 | `mesh_mover_reset_by_zone` | gameplay | `0x00a52c20` | resolved — resets free-standing movers of a loaded zone; **null name read** |
| 23 | `melee_get_move_id_from_name` | gameplay | `0x00a52820` | resolved — move index, 65535 when unknown |
| 24 | `main_menu_video_has_stopped` | main menu `0x007c9fc0` (under UI) | `0x007c9e90` | resolved — tears down the background video |
| 25 | `main_menu_restart_video` | main menu | `0x007c9f90` | resolved — stop hook, teardown, recreate, start hook; **null video → later null vcall** |

---

## L. Crash-shaped and logic defects (summary)

| # | where | defect | status |
|---|---|---|---|
| 1 | C.1 `0x00a5419b`-`0x00a541a7` | `minimap_icon_add_radius_do` with a nil/unknown/despawned/dead object reads the handle from address 8 (sibling `minimap_icon_add_do` returns early) | CONFIRMED |
| 2 | C.1 `0x00a541ad`; C.2 `0x00a544cf` | selector argument used as an unchecked index into the `0x0130c9a8` / `0x0130cb98` function tables (only 1-3 safe) | CONFIRMED (tables read by tranche 10) |
| 3 | G.2 `0x00dab336` | `mesh_mover_reset_by_zone(nil)` / non-string → zone hash reads address 0 | CONFIRMED |
| 4 | B.5 `0x007adc00` → `0x007adb9f` | video creation failure (`0x00a73e40` null) still sets the playing flag; the next stop/restart makes a virtual call through null | CONFIRMED structure |
| 5 | D.4 `0x007de202` | `pause_map_stag_takeover_do_reward` with nothing selected reads `[0 + 0x44]` | CONFIRMED |
| 6 | F.7 `0x00605179` | non-string `notoriety_get` argument → CRT `__stricmp(null)` → invalid-parameter handler (process termination with the stock handler) | CONFIRMED path, outcome depends on handler |
| 7 | C.5 `0x008f83ef`, `0x00e28d56`, `0x008f866d` | `object_indicator_init_lua` without a callback context, with missing document elements, or with an empty callback pool → null reads | CONFIRMED shape, reachability HYPOTHESIS |
| 8 | E.1 `0x005dedf7` | negative script width → TLS branch dereferences a possibly-null `+0x674` object | CONFIRMED shape, reachability OPEN |
| 9 | E.1 `0x005dee59` | integer divide by a value returned for a script-chosen resolution | HYPOTHESIS |
| 10 | C.4 `0x00a58e52` | `0x026e7e68` help-text table used unchecked (sibling checks) | CONFIRMED shape, reachability OPEN |
| 11 | D.4 `0x007de1d8` | local player used unchecked | CONFIRMED shape |
| 12 | F.3 `0x0052a230` | character `+0xf0` record used unchecked | CONFIRMED shape |
| 13 | G.2 `0x00a0a79e` vs `0x00a0a802` | mover `+0x170` dereferenced before its own null test (latent, other callers) | CONFIRMED structure |
| 14 | D.3 `0x007d9914`-`0x007d9942` | mouse deltas uninitialised when input bit `0x10` of `0x0132abc0` is clear → map jumps by stack garbage | CONFIRMED structure, reachability OPEN |
| 15 | C.4 | `open_vint_dialog_check_done` can only report −2/−1; never the chosen option; may never leave −2 after a choice | CONFIRMED (callback body), consequence OPEN |
| 16 | C.4 | dialog pool exhausted → result stays −2 forever (tranche 10 L6) | CONFIRMED structure |
| 17 | F.7 | unknown track → `notoriety_get` returns no value | CONFIRMED |
| 18 | F.6 | unknown move → 65535 (not −1/nil) | CONFIRMED |
| 19 | F.5 | non-table argument to `party_dismiss_do` → Lua error inside the binding | HIGH CONFIDENCE |
| 20 | G.1 | host-only un-complete, unconditional broadcast; no effect without a host session | CONFIRMED structure |
| 21 | E.1 | reflections / post processing / AO lower bound stored unchecked | CONFIRMED structure |
| 22 | A, F.4, C.2 | `npc_aim_at_point` is a no-op; `minimap_icon_remove_radius_do` is `minimap_icon_remove_do` | CONFIRMED |
| 23 | F.2 | `npc_dont_auto_equip_or_unequip_weapons` arg 2 defaults **false** (unconditional read), unlike the default-true setters | CONFIRMED |

---

## M. Clean-room check

- Addresses are plain hex throughout; no Ghidra auto-names for functions, globals or labels, no decompiler variable
  names, and no pasted pseudocode. Library routines are named only where Ghidra identified them as standard C runtime
  (`__stricmp`, `__invalid_parameter`, `_tolower`) or as the public Lua 5.1 API by established project convention
  (`lua_gettop`, `lua_tolstring`, `lua_gettable`, `lua_next`, …); short game strings (Lua hook names, element names,
  settings labels, mission and team names) are quoted as data.
- Self-check run on the finished file, as the last step before reporting, with the pattern
  `\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4}|LAB_[0-9a-fA-F]+|FUN_[0-9a-fA-F]+|DAT_[0-9a-fA-F]+)\b`
  (`grep -cP`): **0 hits**. The pattern was first checked against 15 controls: 12 positives (one each of the
  auto-named local, parameter, register-input, stack-array, type-name and the three label-prefix forms), all matched,
  and 3 negatives ("left undefined", "in ECX", a bare `0x…` address), none matched.
- No spec file was edited. The private Ghidra copy `tools\gp_t18` was deleted after the dumps (verified absent); raw
  dumps stayed in the session scratchpad.
