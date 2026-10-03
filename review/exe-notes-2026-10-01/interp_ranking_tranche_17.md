# Ranking tranche 17 — 25 previously-unspecced Lua-bound names (Team A, 2026-10-03)

Job definition: `D:\Project Crreish\TEAM A\ghidra\jobs\ranking\tranche-17.json`. The job file's own title states **names
351-375 of the 554 unspecced names, Team B call-count order**; this note uses that range and the job's own 25-name list
(the dispatch brief gave no competing numeric guess). Run locally, read-only, on a private copy of the Ghidra project
(`tools\gp_t17`, deleted afterwards), with the job's own arguments: `CrreishDump.java <out>/lua lua depth:1 maxfuncs:15
maxinsn:500 <25 names>`. **All 25 names resolved.** Every name string occurs exactly once in `.rdata`. For all 25 the
handler is the code pointer stored in the slot right after the name (insn offset +1) — CONFIRMED — `index.txt`,
cross-checked instruction by instruction against a range run of every non-gameplay registrar (section A). **No
pointer-before-name registrar occurs in this tranche** (the tranche 13/16 quirk was checked for and is absent): every name
is a row of a `{name, function}` stack table. Follow-up runs on the same private copy, cited by name below:

- "range run": `range` mode on the five non-gameplay registrar bodies (section A);
- "xref run": `xref` mode on those five registrars and on the shared stub `0x007c9f50`;
- "callee dump": depth-0 `func` run (`maxinsn:900`) on 32 callee addresses (listed in section I);
- "second callee dump": depth-0 `func` run on six more (`0x006cf930 0x0045ed50 0x00469ff0 0x0082a630 0x00a5b570 0x00a5c290`);
- "global xref run": `xref` mode (`xrefs:60`) on 12 globals (section I);
- "table read": `ptrs` mode, 332 dwords at `0x0117faf0` (the input-action name table, G.6);
- "constant read": `ptrs` mode, 2 dwords each at eight addresses (section I);
- "receiver range": `range` mode on `0x00a293f0-0x00a29480` (the sing-along network receiver, F.2).

Call counts are Team B's (`D:\Crreish-sync\for-team-b\team-b\tools\lua_reconciliation_called_and_registered_1181.tsv`):
**every name in this tranche is 1 call site in 1 script.** Team B tags 13 names `ui` (the three `sb_*`, the four
save-system names, the two `playlist_*`, the four `pause_menu_*`) and 12 `gameplay`; section A shows the registrars agree
exactly (13 under the UI bring-up `0x008430f0`, 12 in the gameplay registrar `0x00a20840`).

Labels: **CONFIRMED** = read in the listing of a dump made for this note; **HIGH CONFIDENCE** = follows from a dumped call
or reference, but the callee body was not dumped or a meaning is inferred from strong usage; **HYPOTHESIS** = plausible,
not settled; **OPEN** = not settled (collected in section K).

**Shared conventions, cited not re-described:** every entry opens with the `lua_gettop` prologue (`0x00dfde50`) and
reads arguments with negative stack indices counted from the bottom (`-N` = arg 1, `1-N` = arg 2, …). Primitives as in
tranche 09/11/13/16 front matter: `0x00dfe210` `lua_tolstring` (non-string reads as null), `0x00dfe1e0` `lua_toboolean`,
`0x00dfe040` `lua_type` (0 = nil), `0x00dfe160` `lua_tonumber` (non-number reads as 0), `0x00ea2596` truncating
float-to-int, `0x00dfe590` `lua_pushboolean`, `0x00dfe420` push-C-string (null pushes nil). New this tranche (CONFIRMED —
listings): `0x00dfe3a0` = `lua_pushnumber` (stores a double, type tag 3), `0x00dfe6f0` = `lua_createtable` (type tag 5),
`0x00dfe800` = `lua_settable`. "Nil-gated optional argument" = the standard idiom of tranche 13. Resolvers: `0x00a281a0`
generic character chain; `0x00a28150` (`#FOLLOWER#`, then the plain character resolver `0x005e4dd0` on the world-object
registry `0x02442750`, liveness `0x00853b10`, virtual `+0x70`). `0x009da4e0` local player; `0x009df3d0` remote co-op
player; `0x0087ba20` session. Kind-flag tests through `[0x02cc9900 + 4·(object byte +0x34)]` and the bit helper
`0x00853b30` (tranche 13). Handle resolution: `0x00458230` on `0x024433a8` with key `0x031d152c`, then the `+0x33` bit
`0x10` rejection (tranche 11). Network record helpers (`0x0086f5f0` open with opcode, `0x00881110`/`0x00881040` write
bits/bytes, `0x0086f1b0` broadcast to session, `0x0086f110` send to one machine, `0x008ae020` owner machine of an object,
`0x008addb0` "is locally owned", `0x0086eb20` close) as in `interp_tabs.md` Q6/Q14 and tranche 11. Timer arm
`0x00d9e140` and the notice dialog `0x007c3d80` as in tranche 16. The Lua-callback hook mechanism (`0x00e0ca80` and
siblings) is tranche 13's; the 350-slot hook-name pool `0x00a1fcc0` and the opcode-`0x43` hook broadcast are
`spec-lua-api-behaviour.md` §42/§47. The audio-wrapper routines used by the radio/playlist names (`0x0045f1a0` stop a
playing instance by handle — tranche 16 B.4; `0x0045f5b0`, `0x0045ea70`, `0x00462960`) are described where first used
(D.1).

**Small new generic facts, CONFIRMED (callee dump / second callee dump):**
- `0x00a280c0(name, &found)` — the resolver used by `player_*`/`qte_*`/`perform_killswitch` for "player-like" arguments:
  null name → 0; otherwise a registry lookup `0x00a27e20` that accepts only objects whose kind byte `+0xa` has bit `0x02`
  (alive), and failing that a literal compare with `"#PLAYER#"` → local player. It does **not** try `#FOLLOWER#` or plain
  character names (that is `0x00a281a0`, which calls it first). HIGH CONFIDENCE: kind `+0xa` bit `0x02` is the player
  class (the human-shield gate, G.3, refuses exactly this kind as a victim).
- `0x0062a190(name)` — registry lookup accepting kind `+0xb` bit `0x08` (alive); used by the vehicle resolver
  `0x00a281e0` (F.1). HIGH CONFIDENCE: vehicles.
- `0x00a281e0(name)` — **vehicle resolver**: if `0x00a280c0(name, 0)` finds a player whose vehicle handle
  (`+0x16c0/+0x16c4`) is non-zero, returns that vehicle through `0x004dcf00` (handle resolution with kind `+0x6` bit
  `0x80`, alive unless the third argument allows dead); otherwise looks the name up with `0x0062a190`, requires it alive
  and its virtual `+0x68` to answer true, and returns its virtual `+0x70`.
- `0x0043aa90` returns the **squared** length of a 3-vector (no square root) — relevant to G.3's range test.
- `0x00d9f9d0(vec)` is true iff all three components lie strictly inside ±1.0e-7 (constant read: doubles at `0x012490b0` /
  `0x011233e0` = `0xbe7ad7f2a0000000` / `0x3e7ad7f2a0000000`) — an "is zero vector" test.

---

## A. Registrars — where the 25 names live

Six registrars. CONFIRMED — `index.txt`, xref run, range run:

| registrar | reached from | rows (loop count) | names in this tranche |
|---|---|---|---|
| gameplay `0x00a20840` | (established) | 1,016 | 12: `satellite_weapon_mode_set`, `radio_unblock`, `radio_set_sing_along_track`, `radio_newsbreak_clear`, `radio_clear_sing_along_track`, `qte_start_m02_skyqte_01`, `players_naked`, `player_take_human_shield_do`, `player_names_get_all`, `player_get_custom_voice`, `player_action_is_pressed`, `perform_killswitch` |
| `0x00840420` (Saintsbook) | UI `0x008430f0`, call at `0x0084319b` | 3 | 3: `sb_select_hitman_target`, `sb_select_chop_shop_vehicle`, `sb_saintsbook_is_available` |
| `0x007d0ec0` (save system) | UI `0x008430f0`, call at `0x008431a1` | 7 | 4: `save_system_set_operation`, `save_system_delete_game`, `save_system_change_device`, `save_game_wrap_up` |
| `0x0083e850` (playlist) | UI `0x008430f0`, call at `0x0084311a` | 3 | 2: `playlist_stop_track`, `playlist_play_track` |
| `0x007e18e0` (pause-menu options) | UI `0x008430f0`, call at `0x0084317d` | 17 (`0x11`) | 2: `pause_menu_is_using_vehicle_southpaw_control_scheme`, `pause_menu_is_using_southpaw_control_scheme` |
| `0x007af8d0` (pause-menu misc.) | UI `0x008430f0`, call at `0x0084318f` | 7 | 2: `pause_menu_horde_mode_retry`, `pause_menu_horde_mode_new` |

- The five sub-registrars have the tranche 16 table shape: a local `{name, function}` array, then a loop of
  `lua_pushcclosure` (`0x00dfe4f0`) and `lua_setfield` into globals (`0x00dfe830`, index `-10002`). Each is called exactly
  once, from `0x008430f0`. Full tables, for a binding layer (CONFIRMED — range run):
  - Saintsbook: `sb_select_chop_shop_vehicle` `0x00840380`, `sb_select_hitman_target` `0x008403d0`,
    `sb_saintsbook_is_available` `0x0083ef10`.
  - save system: `save_system_save_game` `0x007d0dd0`, `save_system_load_game` `0x007d0e60`,
    `save_system_change_device` **`0x007c9f50`**, `save_system_cancel_coop_load` `0x007d0e90`, `save_system_delete_game`
    `0x007d07b0`, `save_system_set_operation` `0x007d0800`, `save_game_wrap_up` **`0x007c9f50`**.
  - playlist: `playlist_play_track` `0x0083e670`, `playlist_stop_track` `0x0083e1e0`, `cell_playlist_save_list`
    `0x0083e6f0`.
  - pause-menu options (17): `pause_menu_change_control_scheme` `0x007e10d0`, `pause_menu_control_scheme_init`
    `0x007e1110`, `pause_menu_update_option` `0x007e13e0`, `pause_menu_accept_options` `0x007e0430`,
    `pause_menu_restore_defaults` `0x007e11f0`, `pause_menu_quit_game_internal` `0x007e0470`, `get_current_difficulty`
    `0x00a47300`, `game_difficulty_select` `0x007e18a0`, `game_record_mode_can_encode` `0x00a3c670`,
    `game_record_mode_enable` **`0x007c9f50`**, `game_record_mode_is_active` `0x00a3c670`,
    `game_record_mode_is_supported` `0x00a3c670`, `game_record_mode_show_hud` **`0x007c9f50`**,
    `game_record_set_quality_level` **`0x007c9f50`**, `pause_menu_is_using_southpaw_control_scheme` `0x007e03e0`,
    `pause_menu_is_using_vehicle_southpaw_control_scheme` `0x007e0390`, `pause_menu_has_seen_display_cal_screen`
    `0x007e0490`.
  - pause-menu misc.: `pause_menu_open_achievements` **`0x007c9f50`**, `pause_menu_error_message_while_saving_PS3`
    **`0x007c9f50`**, `pause_menu_horde_mode_retry` `0x007af760`, `pause_menu_horde_mode_new` `0x007af780`,
    `pause_menu_cat_mouse_active` `0x007af7a0`, `pause_menu_common_top_finished_loading` `0x007af7d0`,
    `pause_menu_cant_retry_because_host_in_creation` `0x007af870`.
- **`0x007c9f50` is a shared no-op stub** (CONFIRMED — listing): it calls the `lua_gettop` prologue, discards the result
  and returns 0 values. The xref run finds it stored as a handler **32 times in 10 functions** — the five UI sub-registrars
  above (7 rows), the gameplay registrar (6 rows), the UI helper registrar `0x00845aa0` (5 rows), and `0x00e0ef80`,
  `0x00e0f900`, `0x00e1dfb0`, `0x007c9fc0`, `0x007c04b0`. Every name bound to it is a no-op in the retail PC build.
- The dumper's second "use" of `sb_select_chop_shop_vehicle` and `playlist_play_track` (`*_1.txt`, rooted at
  `0x00dfe830`) is the registrar loop reading the **first** row of its table, as in tranche 09/13/16 — not a second
  handler. CONFIRMED (range run: both names are row 0 of their table).
- The 12 gameplay rows (name store → handler, from `index.txt`): `satellite_weapon_mode_set` `0x00a248d2` → `0x00a5dfa0`;
  `radio_unblock` `0x00a24612` → `0x00a5c2f0`; `radio_set_sing_along_track` `0x00a246ee` → `0x00a5bb10`;
  `radio_newsbreak_clear` `0x00a24680` → `0x00a5b5a0`; `radio_clear_sing_along_track` `0x00a2463e` → `0x00a5bad0`;
  `qte_start_m02_skyqte_01` `0x00a245e6` → `0x00a5b420`; `players_naked` `0x00a24562` → `0x00a59af0`;
  `player_take_human_shield_do` `0x00a244c8` → `0x00a5aeb0`; `player_names_get_all` `0x00a243ec` → `0x00a5a2b0`;
  `player_get_custom_voice` `0x00a24326` → `0x00a5a0a0`; `player_action_is_pressed` `0x00a241f2` → `0x00a59d50`;
  `perform_killswitch` `0x00a24100` → `0x00a59510`.

---

## B. Saintsbook (diversion contract book) — 3 functions

### B.1 `sb_select_hitman_target` → `0x008403d0`

**Arguments:** 1 number = list index; 2 number = target index (both truncated). **Return:** exactly 1 boolean. CONFIRMED.

**Body (CONFIRMED — listing, callee dump):** returns `0x006b0010(list, target)` on the hitman-diversion manager
`0x014c3390` (through the thunk `0x006b2010`):
- false unless the manager's `+0x11` byte is set (HYPOTHESIS: "initialised/active");
- list index **unsigned**-bounded by `+0x35e8` (lists of `0x7b0` bytes from `+0x18`), target index unsigned-bounded by the
  list's `+0x7a8` (targets of `0x118` bytes); either out of range → `0x006afd20()` then false;
- a target with `+0x111` bit `0x20` set → false (HYPOTHESIS: already completed);
- **re-selecting the current target** (`0x006a9600()` returns this same record) → false, nothing changed;
- otherwise: `0x006a10b0()` — a thunk that operates on the **chop-shop** manager `0x014c1cf0` (global listing of the dump)
  — then `0x006a9760` on this manager, stores the pair at `+0x3d70/+0x3d74`, sets target `+0x111` bit 0, clears bits
  `0x2c` of `+0x3db0` and sets its bit 1 from a mission-state test (`0x006ced00` not −1 and not 4); if the target's
  `+0x6c` is `0x19` and `0x00822a00(target +0x40)` returns an object, calls `0x009dac20(local player, obj, obj +0x2c, 0)`
  (HYPOTHESIS: places a waypoint/marker); returns **true**.

Bounded on both indices — no crash shape.

### B.2 `sb_select_chop_shop_vehicle` → `0x00840380`

**Arguments:** 1 number = list index; 2 number = entry index (both truncated). **Return:** exactly 1 boolean. CONFIRMED.

**Body (CONFIRMED — listing, callee dump):** returns `0x006a0190(list, entry)` on the chop-shop manager `0x014c1cf0`
(through `0x006a1090`):
- false unless `+0x11` is set; list index unsigned-bounded by `+0x798` (lists of `0x180` bytes from `+0x18`); entry index
  unsigned-bounded by the list's `+0x1c` (entries of `0x38` bytes from `+0x20`); out of range → `0x0069f2e0(1)` then false;
- entry `+0x36` set → false;
- an entry whose `+0x35` is 2 and which equals the pair at `+0x9c0/+0x9c4` (HYPOTHESIS: the co-op partner's current
  contract) → after the current-selection handling below, shows the notice `0x007c3d80("DIVERSION_CHOP_SHOP_TITLE",
  "DIVERSION_CHOP_SHOP_PARTNER_TOOK_CONTRACT", …)` (both localized by `0x0084a1b0`; dialog result unused, so pool
  exhaustion is harmless here) and returns false;
- if a current selection exists (`0x0069c4f0`), it is cleared (`0x0069f2e0`) and, **when the request equals the current
  selection, false is returned** — re-selecting toggles the contract off;
- otherwise: `0x006afed0()` — a thunk that operates on the **hitman** manager `0x014c3390` — then stores the pair at
  `+0x9b8/+0x9bc`, updates bits 3, 1 and 5 of `+0x9d8` (mission-state test, `0x0101b4a0`, `0x0069d250`, `0x0069c5d0`),
  returns **true**.

**Cross-manager (HIGH CONFIDENCE):** selecting a hitman target calls into the chop-shop manager and vice versa —
HYPOTHESIS: each clears the other diversion's active contract, so only one Saintsbook contract is active at a time.
**Asymmetry (CONFIRMED structure):** re-selecting the current chop-shop entry deselects it; re-selecting the current
hitman target does nothing. Bounded — no crash shape.

### B.3 `sb_saintsbook_is_available` → `0x0083ef10`

**Arguments:** none read. **Return:** exactly 1 boolean. CONFIRMED.

**Body (CONFIRMED — listing, callee dump):** true iff any of
1. `0x006a7f40(1)` on the hitman manager — counts, over every list and every target (the same `0x7b0`/`0x118` strides
   and the `+0x7a8` per-list count as B.1), the targets whose flag byte `+0x111` has bit `0x80` set **and** whose byte
   `+0x112` has bit 0 set (with argument 0 it would count every target) — is non-zero;
2. `0x0069b6f0(1)` on the chop-shop manager — sums, over lists whose byte `+0x178` is set, min(list `+0x18` + 1, the
   list's entry count `+0x1c`) (HYPOTHESIS: entries completed so far plus the one now offered) — is non-zero;
3. `0x00697110()` — returns the dword `0x014bf548` — is non-zero.

**Dead term (CONFIRMED for direct references):** the global xref run finds exactly **one** reference to `0x014bf548` in
the whole binary — this read. It is zero-fill and never written, so term 3 is always false. A binding layer can implement
this name as "hitman targets available or chop-shop entries available".

---

## C. Save system — 4 functions

### C.1 `save_system_change_device` and C.2 `save_game_wrap_up` → both `0x007c9f50`

**Arguments:** none read. **Return:** 0 values. CONFIRMED — both rows of the save-system table point at the shared no-op
stub (section A). Implement as no-ops.

### C.3 `save_system_set_operation` → `0x007d0800`

**Arguments:** 1 number (truncated). **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing):** if the value is **unsigned** below 2 (i.e. 0 or 1), stores it into `0x02297e14`; any other
value (including negatives) is ignored.

**Dead store (CONFIRMED for direct references):** the global xref run finds exactly **one** reference to `0x02297e14` —
this write. Nothing reads it, so the call has no observable effect in the retail executable (HYPOTHESIS: a console-era
save/load operation selector whose reader was compiled out). Implement as a no-op.

### C.4 `save_system_delete_game` → `0x007d07b0`

**Arguments:** 1 number = index into the save list (truncated). **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing, callee dump):** when 0 ≤ index < `0x02297d0c` (**signed** test both ways), the save-list
entry is `0x02297d08 + 0x60·index`, and:
1. `0x00b96760(entry)`: only if the Steamworks `SteamUser()` import (through `0x0101c54c`) returns non-null — clears a
   local scratch record (`0x00b949f0`), reaps any finished storage job (`0x00b95ff0`: if the job handle `0x0130f5d8` is not
   the idle value `0xffff` and `0x00db6680` reports it done, sets it back to `0xffff`), and then, **only if the job handle
   is idle**, takes the slot number from the entry's first dword and, when **0 ≤ slot < 24** (`0x18`): clears the slot's
   name byte `0x0290da00 + 25·slot`, decrements the used-slot count `0x0290d9fc`, clears the flag byte `0x0290d9d0 + slot`,
   and starts an asynchronous job named `"delete_save"` (`0x00db6920`, completion `0x00b96630` → `0x00b963f0`), storing its
   handle in `0x0130f5d8`.
2. `0x007cfa20()` on the save/load screen object `0x02297d88`: if its `+0x1e` bit `0x08` is set, starts the Lua callback
   `"save_load_refresh"` through `0x00e0ca80` (tranche 13 mechanism) and stores a value from `0x007b10a0(+0x20) +0x580` into
   the callback record's `+0x14`.

**Logic notes (CONFIRMED structure; consequences HYPOTHESIS):**
- A delete requested **while another storage job is still running**, or with no Steam user interface, is **silently
  dropped**, yet step 2 still refreshes the list — the script sees the entry survive with no error.
- The slot bookkeeping (count decrement, flag clears) is done **before** the asynchronous job is known to succeed; whether
  the completion `0x00b963f0` repairs it on failure is OPEN.
- Both indices are bounded (save-list index signed-checked, slot 0..23) — no crash shape.

---

## D. Radio-preview playlist — 2 functions

### D.1 `playlist_play_track` → `0x0083e670`

**Arguments:** 1 number = track index (truncated). **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing, callee dump):**
1. Stops the current preview: `0x0045f1a0(&0x02317234)` (tranche 16 B.4: stop the instance whose handle is stored there and
   clear the handle on success).
2. `entry = 0x0055df80(index)`: the 20-byte record `0x013bbc18 + 20·index` **if 0 ≤ index < the 16-bit count
   `0x013c045c`, else null**. The comparison uses only the **low 16 bits** of the index.
3. `0x0045f5b0(0x00462960("Radio_Songs"), entry[0], [0x02317238])` — an audio-wrapper call that looks up the audio object
   whose id is in `0x02317238` (`0x0045c720`), applies `0x0045c510(object, entry[0])` with the `"Radio_Songs"` id, and sets
   the object's `+0x98` bit `0x20` (returns −0x65 when the object is unknown, −1 when audio is not initialised,
   `0x031728c2` = 0). HYPOTHESIS: "set switch group `Radio_Songs` to the track's state on the preview emitter".
4. `0x0045ea70([0x02317238], 0x00462960("Radio_Preview"), &0x02317234, 0, 0)` — validates both ids (not 0, not −1), looks
   the object up, takes an instance record from the pool `0x031d4cf0`, stores the event id at `+0x8`, arms the record's
   timer with the last argument, links it (`0x0045cb50`) and writes the instance handle to `0x02317234`. HYPOTHESIS:
   "post event `Radio_Preview` on the preview emitter and remember the playing handle". `0x00462960` is the audio-name → id
   helper (through `0x0046fd00`).

`0x02317238` (the preview emitter id) and `0x02317234` are written only by code at `0x0083e609`/`0x0083e61d` (no defined
function — HYPOTHESIS: the playlist screen's set-up) — global xref run.

**Crash — CONFIRMED (listing):** step 2's null result is **not checked**: `0x0083e6a1` reads `[entry]` directly. An index
that is negative, at or above the song count, or any index while the song table is empty (count 0, e.g. before the radio
tables load) is a **null read**. Because only the low 16 bits are compared, an index of 65,536 + k aliases track k
(CONFIRMED arithmetic); NaN or out-of-range numbers truncate to −2³¹, whose low 16 bits are 0 — so they play track 0
rather than crash, as long as at least one song is loaded.

### D.2 `playlist_stop_track` → `0x0083e1e0`

**Arguments:** none read. **Return:** 0 values. **Body:** `0x0045f1a0(&0x02317234)` — stops the preview instance (D.1
step 1). CONFIRMED. Safe when nothing is playing (a zero handle returns −0x64 without effect).

---

## E. Pause menu — 4 functions

### E.1 `pause_menu_is_using_southpaw_control_scheme` → `0x007e03e0` and E.2 `pause_menu_is_using_vehicle_southpaw_control_scheme` → `0x007e0390`

**Arguments:** none read. **Return:** exactly 1 boolean. CONFIRMED.

**Body (CONFIRMED — listing):** `0x005bc1d0(slot, alt)` reads the 34-entry input-binding table at `0x01411418` (stride
`0x34`; `alt` = 0 reads `+0x0`, non-zero reads `+0x8`). E.1 returns true iff slot **4** reads 1 **and** slot **5** reads 0;
E.2 is identical with slots **13** and **14**. The table is filled to −1 by `0x005c1f70` (34 entries, `0x6e8` bytes) and
then loaded from the `"General Controls"` / `"Satellite Controls"` sections and two further control blocks; the scheme
switcher `0x007e10d0` (`pause_menu_change_control_scheme`) picks the on-foot loader `0x005c12c0` or the vehicle loader
`0x005c13c0` by `0x0229a558`. HYPOTHESIS: slots 4/5 (13/14 for vehicles) are the move/look stick assignments and
"southpaw" = move on stick 1, look on stick 0. Fixed in-range indices — no crash shape.

### E.3 `pause_menu_horde_mode_retry` → `0x007af760` and E.4 `pause_menu_horde_mode_new` → `0x007af780`

**Arguments:** none read. **Return:** 0 values. Bodies: `0x006e6820()` (retry) and `0x006e8dd0()` (new). CONFIRMED.

**Shared part (CONFIRMED — listing, callee dumps):** nothing happens unless the horde-mode byte `0x014f3d34` is set **and**
`0x00614cb0()` (the pointer chain `[[0x014b2ddc] +0x1c] +0x1c`, each link null-checked) is non-null. Then:
1. Maps that object's `+0x1740` to `0x012f3b5c`: 0 → 1, 1 → 3, anything else → 2 (HYPOTHESIS: a difficulty/wave-set
   selector).
2. `0x006da400(0, "", 0, null handle ×2, 0)` (the empty string is the zero dword at `0x0111fe04`; null handle pair from
   `0x0113c868`, constant read) — the shared **mission-end routine** (seven callers). Its gate `0x006cf930` (second callee
   dump) requires an active mission `0x014c8460`, mission state `0x014c7a14` not 6/7/8, two mission timers
   (`0x012f2630`, `0x012f2634`) idle and `0x007bfa70` false; it then sets `0x014c8326` = 1 and calls `0x006cf820`. When the
   gate passes, `0x006da400` (body read, not every callee): if `0x009da830(local player)` is true calls `0x009da840` and
   `0x009dcf10` on it (HYPOTHESIS: clears a downed state); closes the dialog found by `0x007c1dd0(0x014c833c)` if any;
   with the active mission's `+0xa0` equal to 2 hands off to `0x006d9f70(mission, 1)`, otherwise records the reason pointer
   in `0x014c7a08` and arms the timer `0x012f2630` for 4,000 or 2,000 ms; and, **only on a co-op client** (session exists
   and `+0x5c` ≠ `+0x58`), sends an opcode-`0x44` record to the machine from `0x008684d0`. HYPOTHESIS: horde mode runs as
   a mission and "retry"/"new" end the current one (as a failure with an empty reason) before re-arming horde state.
3. `0x006e6230()` resets the horde state block: bytes `0x014f3d30`..`0x014f3d36` = 0 (except `0x014f3d34`), `0x014f3d2c` = 0,
   dwords `0x012f3b28`/`0x012f3b30`/`0x012f3b34` = −1, `0x012f3cd8` = 0, and `0x008f7be0(3)`.

**Retry only (CONFIRMED):** the three dwords `0x012f3b28`/`0x012f3b30`/`0x012f3b34` are saved **before** step 2 and written
back after step 3 (so a retry keeps them; HYPOTHESIS: the current wave/round selection), then bytes `0x014f3d30`,
`0x014f3d34`, `0x014f3d35` = 1 and float `0x014f3d28` = 0.0.

**New only (CONFIRMED):** the dwords stay −1; byte `0x014f3d32` = 1, `0x014f3d34` = 1, `0x014f3d2c` = 0; every object in
the world-object sub-list at `[0x03171a64] +0x1fc` (count `+0x204`, objects `+0x58`) gets `0x008ccb90(obj, 1, 1,
locally-owned)` (with tranche 13/16's reading of `0x008ccb90`, HIGH CONFIDENCE: deactivate/hide); then an opcode-`0x44`
record with 8-bit sub-type **`0x31`** is broadcast to the session (`0x0086f1b0`) — **with no host gate** (CONFIRMED
structure; consequence on a client OPEN).

Both are no-ops outside horde mode. No crash shape found (every pointer on these paths is null-checked or gated).

---

## F. Radio — 4 functions

### F.1 `radio_unblock` → `0x00a5c2f0`

**Arguments:** 1 string = vehicle or player/character name. **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing, callee dump):** vehicle = `0x00a281e0(name)` (generic facts: a player's current vehicle or a
named vehicle); if none, the generic character chain `0x00a281a0(name, 0)` and that character's vehicle handle
(`+0x16c0/+0x16c4`) through `0x004dcf00(…, 0)`. If a vehicle results, `0x005566c0(vehicle)` clears **bit 0 of byte
`+0x163e`** of the vehicle. Nil or unknown name → nothing. Sibling (registered in the preceding slot, second callee dump):
`0x00a5c290` resolves the vehicle identically and calls `0x00556680(vehicle, 0)` — HIGH CONFIDENCE the matching "block".

No network record is sent (CONFIRMED structure) — HYPOTHESIS: the block bit is local to each machine.

### F.2 `radio_set_sing_along_track` → `0x00a5bb10`

**Arguments:** 1 string = song name; 2 string = second name. **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing, callee dump, receiver range):**
- Both names go through `0x0055c910`: audio id (`0x00462960`) then a linear search of the song table `0x013bbc18` (20-byte
  records, id at `+0`, 16-bit count `0x013c045c`) → 16-bit index, or `0xffff` when not found (or when the argument is nil).
- `0x0055f660(index1)` — the **second argument is never read by it** (CONFIRMED: only its first stack argument is used).
  It needs a local player whose voice/persona hash (`0x00941c40`) is one of seven values, each mapped to a cue name
  `"M04_Singalong_{WF,BF,BM,WM,HF,WMA,Z}_Player"`; any other hash, or no local player → nothing. The cue's song-table index
  is looked up (−1 if absent). Then, if 0 ≤ index1 < count and the record's `+0x8` object is non-null: object `+0x40` =
  100, `+0x24` = the voice hash, `+0x14` = the cue index, `+0x47` = 1, and the global `0x012e3b04` = index1.
- `0x00a5b960(index1, index2)` then **always** broadcasts an opcode-`0x45` record, 8-bit sub-type **`0x17`**, 8-bit
  value 5, and the two 16-bit indices, to the session — no host gate, sent even when nothing was set locally.
- Receiver (receiver range, case 5 of the sub-type-`0x17` handler at `0x00a29411`): reads the two 16-bit values; **if
  the second is `0xffff` it calls the clear routine `0x0055e340(first)`**, otherwise `0x0055f660(first, second)`.

**Logic defect — CONFIRMED structure:** because the second name is used only on the wire, a second name that does not
resolve (`0xffff`) makes every **remote** machine *clear* the sing-along on the song the local machine just *set*. The
two machines diverge silently. Also: each machine applies its **own** local player's voice cue (the hash is read locally on
both sides), which is presumably intended.

### F.3 `radio_clear_sing_along_track` → `0x00a5bad0`

**Arguments:** 1 string = song name. **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing):** index = `0x0055c910(name)`; **only if the 16-bit index is > 0** (`JLE` skips 0 and
`0xffff`): `0x0055e340(index)` (bounded 0 ≤ index < count; if the record's `+0x8` object is non-null, clears its `+0x47`),
then `0x00a5b960(index, −1)` broadcasts the same `0x45`/`0x17`/5 record with `0xffff` as the second value (which the
receiver maps to the clear).

**Logic notes (CONFIRMED structure):**
- **Song index 0 can be set (F.2 accepts 0) but never cleared from script** (F.3 rejects 0); the network receiver clears
  index 0 fine. HYPOTHESIS: record 0 is never a sing-along song in shipped data, so this is latent.
- The clear does **not** reset `0x012e3b04` (the index F.2 stored); its other writers are `0x0055fd90` and `0x00560a50`
  (global xref run), so it stays stale until one of those runs.

### F.4 `radio_newsbreak_clear` → `0x00a5b5a0`

**Arguments:** none read. **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing, callee dump):** `0x0055dd00()` stores 0 into `0x013c60e8`, the **pending news-break** audio id.
The sibling registered in the preceding slot, `0x00a5b570` (second callee dump), is its setter: `0x0055e4f0(name)` stores the
name's audio id there (null name ignored). The consumer `0x0055dc80(vehicle)` (called from `0x0055a930`) plays a pending
news-break once — only when no blocking condition holds (`0x00469ff0`: any of the flag bytes `0x0322d87a`, stride `0x58`, up
to `0x0322d982`, is set; `0x0045ed50`: the previous news-break instance `0x013c60ec` is still alive) — by setting switch
`0xa73f0527` to the pending id and posting event `0xa73f0527` on the vehicle's audio object (`+0x159c`) with a 1000 ms
argument, then clears the pending id.

So the clear **cancels a news-break that has been requested but not started**; it does not stop one already playing
(`0x013c60ec` is untouched). No network record. No crash shape.

---

## G. Players and characters — 7 functions

### G.1 `qte_start_m02_skyqte_01` → `0x00a5b420`

**Arguments:** 1 string = character (`0x00a280c0`: a player-class name or `"#PLAYER#"`); 2 string = completion callback
name; 3 nil-gated string = partner character (`0x00a28150`); 4 nil-gated string = a single-point object name
(`0x005982e0`, kind `+0xb` bit `0x02`, alive — tranche 16). **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing, callee dumps):** unresolved character → nothing. Otherwise:
1. `0x005e4660(callback, &slot 0x18)` on the character's hook table (`character +0x1f00`): clears slot `0x18`
   (`0x005e4520`), and for a non-empty name interns it through the hook-name pool `0x00a1fcc0` into that slot; then
   `0x00a29920(0, table owner handle +0x68/+0x6c, 0x18, has-callback)` broadcasts the opcode-`0x43` hook record.
2. `0x00d1dfd0()` — the return-1 stub (result discarded).
3. Partner = `0x00a28150(arg 3)` or **null**; point handle = the object's `+0x8/+0xc` if arg 4 resolves, else the null
   handle from `0x01180028` (constant read: 0, 0).
4. `0x009af790(character, partner, point handle)`:
   - **character not locally owned:** sends opcode `0x41`, sub-type `0x1c`, a zero dword, byte 9, the 16-bit network ids of
     the character and of the partner (the partner id is written as 0 when the partner is null — this branch tests it at
     `0x009af8d3`), a zero byte and the point handle, to the character's owner (`0x0086f110`); done.
   - **locally owned:** if the point handle resolves (kind `+0xb` bit `0x02`, alive), virtual `+0x44` on the character
     with (character `+0x40`, point `+0x44`, character `+0x48`) and flag 1 — HIGH CONFIDENCE: moves the character to the
     point's height while keeping its own horizontal position — and character `+0xa60` = 0.0. **Then, unconditionally,
     reads the partner's handle `[partner +0x8]`/`[partner +0xc]`** into `0x0262d3c0`/`0x0262d3c4`, and starts the QTE:
     `0x0060dd60(character, name hash of "Mission02_SKYQTE1", completion 0x009af330, partner handle, 0, 0, 0, 0x01174bd8
     pair)` (the pair reads 0, 0).
5. The completion `0x009af330` resolves the stored partner handle (kind `+0xa` bit `0x01`, alive, else null), calls
   `0x009aee20(local player, 3, partner, 0, 0)` and `0x009e09b0(this = local player, 1)`, and resets the stored handle to
   the null pair.

**Crash — CONFIRMED (listing):** on the locally-owning machine, `0x009af9f1` loads `[partner + 0x8]` **with no null check**
(the remote branch does test it, at `0x009af8d3`). Arg 3 omitted, nil, misspelt, despawned or dead → partner null → **read at
address 8**. This is the single-player path (the local player is locally owned). The one shipped call site evidently
always passes a live partner.

### G.2 `players_naked` → `0x00a59af0`

**Arguments:** none read. **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing, callee dumps):** `0x006192f0(local player)`, then `0x006192f0(remote co-op player)` if one
exists. `0x006192f0(player)`:
- not locally owned → sends opcode `0x44`, sub-type `0x0d`, one zero byte, to the player's owner (the owner strips);
- locally owned → `0x00824f80(player)`: null tolerated; with the customization record `player +0x2088` and its slot
  index `+0x14`, walks the worn-item list for that slot (count `0x022d0918[slot]`, items of `0x4c` bytes in a block of
  `0x720` per slot at `0x022d0934`), and removes every item whose `[item] +0x2c` type passes `0x0082a630` (a switch over
  types 0..`0x16`; above that, every type except 4 passes) via `0x00821590(record, i, 1)` (re-testing the same index
  after each removal), then `0x00820df0` and `0x00824ca0(record, 0)` (HYPOTHESIS: rebuild and apply the outfit); then
  `0x00891c70(0x008b7100(), player, 1)` — `0x008b7100` returns the first session machine other than the local one, or
  null (no session → null → `0x00891c70` returns at once); otherwise it queues an appearance sync of the player to that
  machine (`0x00894610`, type `0x13`, 60,000 ms timeout) unless the player is owned by that machine. HIGH CONFIDENCE:
  "strip every removable clothing item from both players and resync".

No crash shape found: a null local player is tolerated through `0x008addb0`→`0x00824f80`→`0x00891c70` as far as dumped
(`0x0091f5e0(null)` inside `0x00891c70` is reached only with a session; OPEN). `player +0x2088` itself is not null-checked
(data invariant for a spawned player — HYPOTHESIS).

### G.3 `player_take_human_shield_do` → `0x00a5aeb0`

**Arguments:** 1 string = taker (`0x00a280c0`, player-class or `"#PLAYER#"`); 2 string = victim (generic chain
`0x00a281a0`); 3 nil-gated boolean = "force", **default true**. **Return:** exactly 1 boolean. CONFIRMED.

**Body (CONFIRMED — listing, callee dumps):** either unresolved → false. Otherwise `0x009ad4c0(taker, victim, force)`:
- null either, or a **victim of the player class** (kind `+0xa` bit `0x02`) → false;
- **taker is not the local player** → sends opcode `0x41`, sub-type `0x39`, taker identity, victim identity, `force`, to the
  taker's owner, and returns **true immediately** (optimistic — the real outcome is decided elsewhere);
- local player: two eligibility gates — `0x009aa520(taker, 0)` (rejects on `+0x2ba` bit `0x40`, `+0x1c98` bit 29, ten more
  state tests; dumped, details not needed) and `0x009a9830(taker, victim, 1)` (victim tests, incl. the `0x21` kind-bit tests
  through `0x00853b30` and the squad-leader check `0x0097e7b0`); either false → false;
- **range test:** squared distance between the two `+0x40..+0x48` positions (`0x0043aa90`, squared) compared with the
  square of the float `0x02624d3c` (`0x009635e0`; zero-fill, set at run time). Within range → returns
  `0x009acb20(taker, victim, 1)`. Out of range: `force` false → false; `force` true → if the victim is locally owned,
  returns `0x009ad050(taker, victim, 1)` (HYPOTHESIS: the forced grab that brings the victim to the taker); otherwise asks
  for the victim's ownership (`0x008addd0(victim, continuation 0x009a9fe0, 0)`) and returns **true** at once.

Units are consistent (squared vs squared) — no defect there. **Logic note (CONFIRMED structure):** the default is
`force = true`, so an out-of-range victim is grabbed anyway unless the script passes `false`; and two branches return
true before anything has happened (remote taker; remote victim). HYPOTHESIS: a non-player taker that this machine owns
also takes the "not the local player" branch and is sent to its own owner — whether that loops back is OPEN.

### G.4 `player_names_get_all` → `0x00a5a2b0`

**Arguments:** none read. **Return:** exactly 1 table. CONFIRMED.

**Body (CONFIRMED — listing):** creates a new table (`0x00dfe6f0`), then walks the player list on `0x03171a64` (16-bit
indices at `+0x1f0`, count `+0x1f8`, objects `+0x58` — the same list `0x009df3d0` scans for the remote co-op player) and,
for each player whose `+0x18` string pointer is non-null, sets `table[n] = that string` with n = 1, 2, … counting **only
the players that have a name** (dense, 1-based). HIGH CONFIDENCE: `+0x18` is the object's script name; whether that is the
gamer tag or an internal name is OPEN.

### G.5 `player_get_custom_voice` → `0x00a5a0a0`

**Arguments:** 1 string = player (`0x00a280c0`). **Return:** exactly 1 number. CONFIRMED.

**Body (CONFIRMED — listing):** pushes `0x00831970(player)` = the dword at `[player +0x2088] +0x10` (the customization
record's voice field; HIGH CONFIDENCE) as a number.

**Crash — CONFIRMED (listing):** the resolver result is **not checked**; `0x00831974` reads `[null + 0x2088]` when the name
is nil, misspelt, an ordinary NPC (not player-class) or a dead/despawned player. The `+0x2088` record is not checked either.

### G.6 `player_action_is_pressed` → `0x00a59d50`

**Arguments:** 1 string = action name. **Return:** exactly 1 boolean. CONFIRMED.

**Body (CONFIRMED — listing, table read):** a case-insensitive C-runtime compare (`_stricmp`) against the 166-row table
`0x0117faf0` of `{action id, name}` pairs; first match → its id, no match or nil → −1. Then `0x005bd7b0(id, 0)`:
**unsigned** id > `0xa3` (so −1) → false; otherwise "pressed" if any of — the action's two digital bit indices
(`0x0140ff98 + 32·id`, `+0`/`+4`, −1 = none) hit the mask `0x01411b68 & 0x01411b70`; `0x005ba8c0(+8)`; the key state pair
`0x01411bbc/0x01411bbd + 8·key` (`+0xc`) both set; or one of three further sources on `0x01411b18` (`+0x10`, `+0x14`, `+0x18`).

**Table facts (CONFIRMED — table read):** row 0 is `{−1, "BUTTON_UNBOUND"}`; the other 165 names all start `CBA_`
(`CBA_OFC_WEAPON_MENU` = 0 … `CBA_MENU_PC_TEXT_CHAT` = 163). Ids 0..163 except **29** appear; ids **0x17** and **0x18** each
have **two names** (`CBA_OFC_TAUNT_TWO` / `CBA_OFC_AUDIO_PLAYER_PREV_TRACK`, `CBA_OFC_TAUNT_THREE` /
`CBA_OFC_AUDIO_PLAYER_NEXT_TRACK`) — aliases, harmless. No table id exceeds `0xa3`, so the bound never rejects a real
name. Bounded — no crash shape. Only "is held now" is reported (no edge detection) — CONFIRMED.

### G.7 `perform_killswitch` → `0x00a59510`

**Arguments:** 1 string = victim (`0x00a28150`); 2 string = killer (`0x00a280c0`, player-class or `"#PLAYER#"`).
**Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing, callee dumps):** both must resolve. `0x005e4350(victim)`: true iff `[victim +0xf0] +0x4`
equals the dword `0x0149f928` (written once, by start-up code at `0x00ff28a5`; HYPOTHESIS: the Killbane character-type
record). Then `0x005e5440(victim, killer)` repeats that test and calls `0x005e5190(killer)` and the return-1 stub
`0x00d2f540`. `0x005e5190(killer)`:
1. Looks up **the character named `"M21_Killbane"`** (plain resolver `0x005e4dd0`, alive) — **not** the victim argument —
   and calls its virtual `+0x70`; a null result → nothing more.
2. Sets the killer's hook slot `0x18` to `"m21_killswitch_qte_complete_cb"` (same `0x005e4660` path as G.1, same slot).
3. Starts the QTE `"killbane_killswitch"` (`0x0060e740(killer, name, 0, Killbane handle +0x8/+0xc, 0, 0, null pair
   0x01125700)`), then `0x005e45a0(0x006cf0e0())`.

**Crash — CONFIRMED (listing):** when the `"M21_Killbane"` lookup fails or the object is not alive, `0x005e51b8` sets the
object to 0 and `0x005e51ba` immediately loads its vtable — **virtual call through null**. Reachable when the victim passes
the type test (a live Killbane-type character) but no live object is registered under exactly that script name (e.g. a
second Killbane-type spawn, or a renamed one). Narrow; the one shipped call site presumably passes Killbane himself.
**Logic note:** the victim argument is used only for the type test; the QTE always targets `"M21_Killbane"`. No network
forwarding on this path (contrast G.1) — CONFIRMED structure for the code dumped.

---

## H. Satellite weapon — 1 function

### H.1 `satellite_weapon_mode_set` → `0x00a5dfa0`

**Arguments:** 1 boolean = enable (read unconditionally); 2 nil-gated number = player mask, **default 3** (truncated).
**Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing, callee dumps):** mask bit 0 → the local player, bit 1 → the remote co-op player (only if one
exists). For each selected player, on the satellite-weapon controller `0x0130eee8`:
- **enable** → `0x00b70720(player, &0x029cdb98, 0)`. The target is `0x029cdb98` unless that vector is (near) zero
  (`0x00d9f9d0`), in which case the player's own position (`+0x40..+0x48`) is used; it is stored at controller `+0x54`.
  HYPOTHESIS: `0x029cdb98` is a zero vector (zero-fill, read by many math routines), so from this binding the target is
  always the player's own position. Gates, any failing → nothing (the failure sound `0xd46667bb` plays only when the third
  argument is set; here it is 0): player non-null; player state `+0xcc8` not 13; `0x009aa260(player, 0)` false;
  `0x009b9160(player)` false; if player `+0x1d9a` bit 1 is set, `0x006d7240(0x0111f724)` must be true; and the overhead
  test `0x00b6f9c0` — **128** probe points (`0x0130eee0`, constant read) around an offset of the target, each required to be
  at least **10.0** (`0x0130eee4`) above what `0x00b41670` returns (division by the probe count is guarded by a `> 0` test).
  Then: not locally owned → opcode `0x41`, sub-type `0x38`, true, target, to the owner; locally owned → activates the
  controller (`+0xb0` = 1, timers, `"Satellite Drone"` HUD element via `0x007efab0`/`0x007efd80`/`0x007efb40`,
  `0x00b6fde0(player, 0)`, `0x00905f40(7)`/`0x00905fa0(7)`, byte `+0` = 1).
- **disable** → `0x00b709d0(player)`: null → nothing; not locally owned → opcode `0x41`/`0x38` with false to the owner;
  locally owned and active (`+0xb0`) → tears down (HUD elements released, camera/HUD restores `0x007ff270`, `0x00564d70`,
  `0x0056cc30`, `0x005dcd70(1.0)`, `0x00b6ff90(player)`, `0x00905f60(7)`/`0x00905fc0(7)`, …, `+0xb0` = 0).

**Notes (CONFIRMED structure):** one controller object per machine; the remote player's request is forwarded to its own
machine. Enabling while already active is not separately guarded in the code dumped (OPEN whether `0x00b6f9c0` or the
state tests reject it). No crash shape found.

---

## I. Method notes for the follow-up dumps

- Range run: `0x00840420-0x008404a0`, `0x007d0ec0-0x007d0f80`, `0x0083e850-0x0083e8d0`, `0x007e18e0-0x007e1a80`,
  `0x007af8d0-0x007af980`.
- Xref run: `0x00840420 0x007d0ec0 0x0083e850 0x007e18e0 0x007af8d0` (each called once, from `0x008430f0`) and
  `0x007c9f50` (32 uses in 10 functions).
- Callee dump (depth 0, `maxinsn:900`): `0x0055dc80 0x0055e4f0 0x006b0010 0x006a0190 0x006a7f40 0x0069b6f0 0x00b95ff0
  0x00b949f0 0x00b96630 0x00614cb0 0x006da400 0x006e6230 0x00824f80 0x008b7100 0x00891c70 0x009635e0 0x0043aa90
  0x009aa520 0x009a9830 0x009ad050 0x009acb20 0x005e5190 0x00d2f540 0x00d9f9d0 0x00a27e20 0x0062a190 0x005c1f70
  0x007e10d0 0x0060dd60 0x00a29920 0x009af330 0x00b6f9c0` (`0x009ad050`, `0x009acb20` and `0x0060dd60` dumped but only
  their entries used).
- Global xref run (`xrefs:60`): `0x02297e14 0x014bf548 0x0149f928 0x029cdb98 0x02317234 0x02317238 0x012e3b04 0x013c60e8
  0x014f3d2c 0x014f3d32 0x0262d3c0 0x0130eee8`.
- Constant read: `0x012490b0` = −1.0e-7 and `0x011233e0` = +1.0e-7 (doubles); `0x0130eee0` = 128 and `0x0130eee4` = 10.0;
  `0x0111fe04` = 0 (empty string); `0x01125700`, `0x0113c868`, `0x01174bd8`, `0x01180028` = 0, 0 (null handle pairs).
- Table read: `0x0117faf0`, 332 dwords = 166 `{id, name}` rows; checked by script for duplicate ids/names, gaps and the
  `0xa3` bound (G.6 facts).
- Return-1 stubs met in this tranche: `0x00d1dfd0`, `0x00d2f540` (`MOV EAX,1; RET`) — CONFIRMED. No-op Lua stub:
  `0x007c9f50` — CONFIRMED.

---

## J. Cross-function observations

1. **No pointer-before-name registrar this time.** All 25 names are rows of `{name, function}` stack tables (one gameplay,
   five UI sub-registrars); the two `*_1.txt` dumps rooted at `0x00dfe830` are the loop-row artefact already known from
   tranches 09/13/16.
2. **A shared no-op Lua stub, `0x007c9f50`, backs 32 registered rows** across 10 registrars, including two of this
   tranche's names (`save_system_change_device`, `save_game_wrap_up`) and their table-mates `game_record_mode_enable`,
   `game_record_mode_show_hud`, `game_record_set_quality_level`, `pause_menu_open_achievements`,
   `pause_menu_error_message_while_saving_PS3`. A binding layer can treat every name bound to `0x007c9f50` as a no-op.
   Worth a one-shot census of the other 25 rows for a later tranche (the gameplay registrar's six rows at `0x00a20b87`,
   `0x00a20f65`, `0x00a236c1`, `0x00a23a4c`, `0x00a24071`, `0x00a24ac1`).
3. **Write-only and read-only globals again make whole bindings dead:** `save_system_set_operation` writes a global nothing
   reads (C.3); `sb_saintsbook_is_available`'s third term reads a global nothing writes (B.3). Same family as tranche 16's
   `0x022cdf14`.
4. **Unchecked lookups remain the dominant crash class:** a missing optional partner (G.1), an unresolved player name
   (G.5), an out-of-range track index (D.1), a hard-coded character name lookup (G.7). G.1 is the clearest: the same
   function checks the partner on its remote branch and not on its local branch — a direct template for the fix.
5. **The "QTE complete" hook slot is `0x18` on the starter's hook table** for both QTE natives in this tranche (G.1 with a
   script-supplied callback, G.7 with the fixed `"m21_killswitch_qte_complete_cb"`); both go through `0x005e4660` and its
   opcode-`0x43` broadcast.
6. **Network asymmetries:** `radio_set_sing_along_track` broadcasts a second index it never uses locally, and the receiver
   reads `0xffff` as "clear" (F.2) — a set/clear divergence; `pause_menu_horde_mode_new` broadcasts with no host gate
   (E.4); `radio_unblock`, `radio_newsbreak_clear` and `perform_killswitch` send nothing (F.1, F.4, G.7);
   `player_take_human_shield_do` reports success optimistically when the work is forwarded (G.3).
7. **Return shapes a script can observe:** `sb_select_hitman_target`, `sb_select_chop_shop_vehicle`,
   `sb_saintsbook_is_available`, both southpaw queries, `player_take_human_shield_do`, `player_action_is_pressed` return
   exactly one boolean; `player_get_custom_voice` one number; `player_names_get_all` one table; the other 16 return nothing.
8. **Index spaces for a binding layer:** B.1/B.2 two unsigned-bounded indices into the two diversion managers; C.4 a
   signed-bounded save-list index and a 0..23 slot; D.1 a song-table index compared on its low 16 bits only and **not**
   bounded against a null result; F.2/F.3 song-table indices found by audio-name id (16-bit, `0xffff` = not found; F.3
   rejects index 0); G.6 action ids 0..163 (29 unused, 0x17/0x18 aliased); H.1 a 2-bit player mask.
9. **Two diversion managers cancel each other** (B.1/B.2) — selecting a contract in one calls into the other first.

## K. OPEN

- B.1/B.2: what `0x006a10b0` and `0x006afed0` do to the other manager (HYPOTHESIS: clear its active contract); the
  meaning of hitman target flags `+0x111` bits `0x20`/`0x80`/0 and `+0x112` bit 0; what `0x009dac20` places.
- C.3: whether a reader of `0x02297e14` exists through a base pointer.
- C.4: whether the delete completion `0x00b963f0` repairs the slot count/flags on failure; whether two deletes of one slot
  can double-decrement `0x0290d9fc`.
- D.1: who creates the preview emitter `0x02317238` (code at `0x0083e61d`, no defined function).
- E.1/E.2: the meaning of binding slots 4/5/13/14 and of the values 0/1.
- E.3/E.4: the identity of `0x00614cb0`'s object and its `+0x1740` field; of the sub-list at `+0x1fc`; whether a client
  receiving sub-type `0x31` misbehaves; `0x006cf820`.
- F.1: what reads vehicle `+0x163e` bit 0, and whether the radio block is ever replicated.
- F.2: what the song record's `+0x8` object is (HYPOTHESIS: the playing station instance) and what reads `+0x40`/`+0x47`.
- G.1: `0x0060dd60`'s own body (1,072-line dump, not read in full) — it indexes a per-machine table `0x014b01c0` by the
  owner record's `+0x158` byte without a visible null check of `0x008ae020`'s result; whether that can be null in single
  player.
- G.2: whether `0x0091f5e0(null)` is reachable with a session and a null local player.
- G.3: whether a locally-owned non-player taker's forwarded request loops back.
- G.4: whether `+0x18` holds the gamer tag.
- G.7: the identity of `0x0149f928`; what `0x006cf0e0`/`0x005e45a0` do after the QTE starts.
- H.1: the exact meaning of the overhead test `0x00b41670`; what `0x00905f40`/`0x00905fa0`(7) toggle; whether `0x029cdb98`
  is always zero.

## L. Direct-answer table

| # | name | registrar | handler | status |
|---|---|---|---|---|
| 1 | `sb_select_hitman_target` | Saintsbook `0x00840420` (under UI `0x008430f0`) | `0x008403d0` | resolved — bounded select on hitman manager `0x014c3390`; cancels chop-shop side; re-select = false |
| 2 | `sb_select_chop_shop_vehicle` | Saintsbook | `0x00840380` | resolved — bounded select on chop-shop manager `0x014c1cf0`; re-select toggles off; partner-took notice |
| 3 | `sb_saintsbook_is_available` | Saintsbook | `0x0083ef10` | resolved — hitman or chop-shop work available; third term dead (`0x014bf548` never written) |
| 4 | `save_system_set_operation` | save system `0x007d0ec0` (under UI) | `0x007d0800` | resolved — stores 0/1 into a write-only global; no effect |
| 5 | `save_system_delete_game` | save system | `0x007d07b0` | resolved — bounded; async Steam delete (`"delete_save"`) if idle, else silently dropped; `"save_load_refresh"` callback |
| 6 | `save_system_change_device` | save system | `0x007c9f50` | resolved — shared no-op stub |
| 7 | `save_game_wrap_up` | save system | `0x007c9f50` | resolved — shared no-op stub |
| 8 | `satellite_weapon_mode_set` | gameplay `0x00a20840` | `0x00a5dfa0` | resolved — enable/disable on controller `0x0130eee8` for a player mask (default 3); target = own position |
| 9 | `radio_unblock` | gameplay | `0x00a5c2f0` | resolved — clears vehicle `+0x163e` bit 0 (vehicle by name or player's vehicle); local only |
| 10 | `radio_set_sing_along_track` | gameplay | `0x00a5bb10` | resolved — sets player-voice sing-along on a song; **arg 2 only broadcast; unknown arg 2 makes remotes clear** |
| 11 | `radio_newsbreak_clear` | gameplay | `0x00a5b5a0` | resolved — cancels a pending (not yet playing) news-break |
| 12 | `radio_clear_sing_along_track` | gameplay | `0x00a5bad0` | resolved — clears sing-along flag and broadcasts; **index 0 never clearable** |
| 13 | `qte_start_m02_skyqte_01` | gameplay | `0x00a5b420` | resolved — hook slot `0x18`, optional height snap, QTE `"Mission02_SKYQTE1"`; **null partner read on local path** |
| 14 | `playlist_stop_track` | playlist `0x0083e850` (under UI) | `0x0083e1e0` | resolved — stops the radio preview instance |
| 15 | `playlist_play_track` | playlist | `0x0083e670` | resolved — `Radio_Songs` switch + `Radio_Preview` event; **out-of-range index → null read** |
| 16 | `players_naked` | gameplay | `0x00a59af0` | resolved — strips removable clothing from both players (forwarded when remote), resyncs appearance |
| 17 | `player_take_human_shield_do` | gameplay | `0x00a5aeb0` | resolved — gates + squared-range test, `force` default true; optimistic true when forwarded |
| 18 | `player_names_get_all` | gameplay | `0x00a5a2b0` | resolved — dense 1-based table of player `+0x18` names |
| 19 | `player_get_custom_voice` | gameplay | `0x00a5a0a0` | resolved — `[player +0x2088] +0x10`; **unresolved name → null read** |
| 20 | `player_action_is_pressed` | gameplay | `0x00a59d50` | resolved — `_stricmp` lookup in 166-row action table, held-state test; bounded |
| 21 | `perform_killswitch` | gameplay | `0x00a59510` | resolved — Killbane-type victim gate, QTE `"killbane_killswitch"` on `"M21_Killbane"`; **null vtable call if that name is absent** |
| 22 | `pause_menu_is_using_vehicle_southpaw_control_scheme` | pause options `0x007e18e0` (under UI) | `0x007e0390` | resolved — binding slot 13 == 1 and slot 14 == 0 |
| 23 | `pause_menu_is_using_southpaw_control_scheme` | pause options | `0x007e03e0` | resolved — binding slot 4 == 1 and slot 5 == 0 |
| 24 | `pause_menu_horde_mode_retry` | pause misc. `0x007af8d0` (under UI) | `0x007af760` | resolved — horde only: end mission, reset state keeping three dwords, re-arm |
| 25 | `pause_menu_horde_mode_new` | pause misc. | `0x007af780` | resolved — horde only: end mission, full reset, deactivate sub-list objects, broadcast `0x44`/`0x31` |

## M. Crash-shaped and logic defects (summary)

| # | where | defect | status |
|---|---|---|---|
| 1 | G.1 `0x009af9f1` (in `0x009af790`) | optional partner (arg 3) nil/unresolved → `[null + 0x8]` read on the locally-owning (single-player) path; remote path checks | CONFIRMED |
| 2 | G.5 `0x00831974` | `player_get_custom_voice` with nil/unknown/non-player name → `[null + 0x2088]` | CONFIRMED |
| 3 | D.1 `0x0083e6a1` | `playlist_play_track` index < 0, ≥ count, or no songs loaded → `0x0055df80` null not checked | CONFIRMED |
| 4 | G.7 `0x005e51b8`–`0x005e51ba` | Killbane-type victim but no live `"M21_Killbane"` object → virtual call through null | CONFIRMED shape, narrow trigger |
| 5 | F.2 / receiver `0x00a2942d` | unknown second name sent as `0xffff`; remote machines clear the sing-along the local machine set | CONFIRMED structure |
| 6 | F.3 `0x00a5baf5` | song index 0 can be set but never cleared from script (`> 0` test) | CONFIRMED structure, latent |
| 7 | F.3 | clear leaves `0x012e3b04` (current sing-along index) stale | CONFIRMED structure |
| 8 | C.4 | delete silently dropped while a storage job runs or Steam is absent, yet the list refresh still fires | CONFIRMED structure |
| 9 | C.4 | slot count/flags updated before the async delete is known to succeed | CONFIRMED structure, consequence OPEN |
| 10 | G.3 | returns true before acting when the taker or victim is remote | CONFIRMED structure |
| 11 | G.3 | `force` defaults to true — out-of-range grab unless the script passes `false` | CONFIRMED |
| 12 | E.4 | horde "new" broadcasts opcode `0x44`/`0x31` with no host gate | CONFIRMED structure, consequence OPEN |
| 13 | C.3 | `save_system_set_operation` stores to a global nothing reads | CONFIRMED (direct references) |
| 14 | B.3 | third availability term reads a global nothing writes | CONFIRMED (direct references) |
| 15 | C.1/C.2 | `save_system_change_device`, `save_game_wrap_up` are the shared no-op stub | CONFIRMED |
| 16 | D.1 | track index compared on its low 16 bits only (65,536 + k aliases k) | CONFIRMED arithmetic |
| 17 | B.1/B.2 | re-select asymmetry (hitman: no-op false; chop shop: deselect false) | CONFIRMED structure |
| 18 | G.7 | victim argument used only for the type test; QTE always on `"M21_Killbane"`; no network forwarding | CONFIRMED structure |
| 19 | G.6 | ids 0x17/0x18 have two names each; id 29 has none | CONFIRMED (data) |

## N. Clean-room check

- Addresses are plain hex throughout; no Ghidra auto-names for functions, globals or labels, no decompiler variable names,
  and no pasted pseudocode. Library and platform routines are named only where Ghidra identified them as standard C
  runtime or a public import (`_stricmp`, the Steamworks `SteamUser()` import, the public Lua C API roles); short game
  strings are quoted as data.
- Self-check run on this complete file (sections A–N) with the pattern
  `\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4}|LAB_[0-9a-fA-F]+|FUN_[0-9a-fA-F]+|DAT_[0-9a-fA-F]+)\b`:
  **0 hits.** The pattern was first checked against 24 positive controls (one sample of every auto-named local family,
  the parameter, register-input, stack-array, type-name and three label-prefix forms, plus one embedded in an expression),
  all matched, and 12 negative controls (plain English such as "left undefined", "in ECX", "local player", a bare `0x…`
  address, `_stricmp`, the pattern's own `in_[A-Z]{2,4}` text), none matched. Run by script
  (`scratchpad\t17\cleancheck.py`), first on sections A–M, then again on the finished file after this section was written.
- No spec file was edited. The private Ghidra copy `tools\gp_t17` was deleted after the dumps; raw dumps stayed in the
  session scratchpad.
