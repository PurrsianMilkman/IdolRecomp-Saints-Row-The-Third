# Ranking tranche 19 — 25 previously-unspecced Lua-bound names (Team A, 2026-10-03)

Job definition: `D:\Project Crreish\TEAM A\ghidra\jobs\ranking\tranche-19.json`. The job file's own title states **names
401-425 of the 554 unspecced names, Team B call-count order**; this note uses that range and exactly the 25 names the job
lists. Run locally, read-only, on a private copy of the Ghidra project (`tools\gp_t19`, deleted afterwards), with the
job's own arguments: `CrreishDump.java <out>/lua lua depth:1 maxfuncs:15 maxinsn:500 <25 names>`. **All 25 names
resolved.** Every name string occurs exactly once in `.rdata`, and for all 25 the handler is the code pointer stored in
the registrar slot right after the name (insn offset +1) — CONFIRMED — dump `index.txt`. None of the 25 is registered
by a pointer-before-name one-name registrar (the tranche 13/16/18 quirk); the two registrars involved are both
table-driven (section A), and the range run confirms the name/handler pairing for the one slot where the dumper's
neighbourhood listed several code pointers of the same registrar (`homie_mission_unlock`, next to `homie_mission_lock`).
One pointer-before-name registrar does appear, for a neighbour outside the tranche (`game_proto_select`, G.4 / J.6).

Follow-up runs on the same private copy, cited by name below (exact address lists in section I):

- "first follow-up dump": depth-0 `func` run on 24 callee addresses;
- "range run": `range` mode on seven short ranges (callback bodies with no Ghidra function boundary, and registrar slots);
- "xref run": `xref` mode on eight addresses;
- "second follow-up dump", "second range run", "second xref run": a further twelve callees, one range and seven
  addresses;
- "third range run", "third xref run": four short ranges and three addresses.

Call counts are Team B's (`D:\Crreish-sync\for-team-b\team-b\tools\lua_reconciliation_called_and_registered_1181.tsv`):
**every name in this tranche is 1 call site in 1 script.** Team B tags the eight `main_menu_*` names `ui` and the other 17
`gameplay`; section A shows the registrars agree exactly (8 under the UI main-menu table, 17 in the gameplay registrar).

**Already partly covered elsewhere (cited, re-verified, not re-derived):** the main-menu registrar table, including the
handler addresses of all eight `main_menu_*` names in this tranche and the fact that three of them land on the shared
no-op stub, is already on record in tranche 18 section A (re-confirmed here, section A). `is_syn_tower_destroyed` is a
hard-coded-name sibling of `city_zone_swap_is_active` (`rederive_28.md` §28.18; section D.1). The HUD "x of y" slot
table is the one `hud_x_of_y_remove` indexes (`spec-lua-api-behaviour.md` §41.4; section G). The record table behind
`homie_mission_unlock` is the activity/mission record table of tranche 13 E.1 (section H.3).

Labels: **CONFIRMED** = read in the listing or decompile of a dump made for this note; **HIGH CONFIDENCE** = follows
from a dumped call or reference, but the callee body was not dumped or a meaning is inferred from strong usage;
**HYPOTHESIS** = plausible, not settled; **OPEN** = not settled (collected in section K).

**Shared conventions, cited not re-described:** every entry opens with the `lua_gettop` prologue (`0x00dfde50`) and
reads arguments with negative stack indices counted from the bottom (`-N` = arg 1, `1-N` = arg 2, …). Primitives as in
the tranche 09/13/16/18 front matter: `0x00dfe210` `lua_tolstring` (non-string reads as null), `0x00dfe1e0`
`lua_toboolean`, `0x00dfe040` `lua_type` (0 = nil), `0x00dfe160` `lua_tonumber` (non-number reads as 0), `0x00ea2596`
truncating float-to-int, `0x00dfe590` `lua_pushboolean`, `0x00dfe3a0` `lua_pushnumber`, `0x00dfe420` `lua_pushstring`
(a null string pushes nil — CONFIRMED in this tranche's dump: the null branch writes type 0). "Nil-gated optional
argument" = the standard idiom of tranche 13; an **unconditional** read has no presence check (missing boolean = false,
missing number = 0, missing string = null). Resolvers: `0x00a281a0` (generic: `0x00a280c0` player-like sentinels
first, then the plain character resolver `0x00a28150`); `0x0087ba20` session; "host" = session exists and its `+0x5c`
equals `+0x58`. **Single player has no session object at all** (`spec-lua-api-behaviour.md` §8.27 / §26.28, exhaustive
negative check of the session installer's call sites) — so every "needs a session" or "host-only" branch below simply
takes its no-session path in single player; that is stated per function, not treated as evidence of anything. Kind-flag
tests through `[0x02cc9900 + 4·(object byte +0x34)]`; `0x00853b10` is the "dead or invalid" test (true for null, `+0x33`
bit `0x04`, or kind byte `0xff` — CONFIRMED listing in this tranche, so "alive" = this returns false); `0x00853b30(obj,
0x21)` = "is a player" (tranche 13). Handle resolution: `0x00458230` on `0x024433a8` with key `0x031d152c`, then the
`+0x33` bit `0x10` rejection (tranche 11). The named-object lookup body shape on `0x02442750` (table `+0x2660`, count
`+0x265c`, lookup `0x004588f0`, `+0x33` bit `0x10` rejection, one kind bit) is tranche 16's front matter; this tranche
meets two members again: **`0x00739690`** (kind row `+0xd` bit `0x10`, light group — `spec-lua-api-behaviour.md` §17.24)
and **`0x0062a190`** (kind row `+0xb` bit `0x08`, the per-kind lookup underneath the vehicle resolver `0x00a281e0` —
tranche 11). Kind-row meanings used below are the ones tranche 11 recorded: `+0xa` bit `0x02` = player, `+0xa` bit
`0x04` = human. Network record helpers (`0x0086f5f0` open with opcode, `0x00881110`/`0x00881040` write bits/bytes,
`0x0086f1b0` broadcast to session — a no-op without a session, `0x0086eb20` close) as in tranche 11/16/18. The Lua-hook
dispatch (`0x00e0ca80` look up a hook by name, `0x00e0ce00` push one boolean onto the hook record, `0x00e0cd00`
dispatch) is the coroutine mechanism of `spec-lua-api-behaviour.md` §31.3 / tranche 18 B.5. The dialog pool (`0x007c3310`
pops a free dialog or returns null; `0x007c18a0` body; `0x007c2ac0` option; `+0x13c` completion callback) is
`interp_tabs.md` Q7.2/Q7.4 and tranche 18 C.4. The shared no-op stub `0x007c9f50` is `spec-lua-bindings.md` §13.6 /
tranche 18 A. The name hash `0x00d9e8b0` (case-insensitive table CRC-32, null → 0) and its sibling `0x00d9e7e0` (null
→ 0) are tranche 18 front matter / F.6, C.4.

---

## A. Registrars — where the 25 names live

Two registrars. CONFIRMED — `index.txt`, range run:

| registrar | reached from | names in this tranche |
|---|---|---|
| gameplay `0x00a20840` | (established) | 17: `los_check_do`, `light_group_detach_from_camera`, `light_group_attach_to_camera`, `level_light_enable_all_do`, `is_syn_tower_destroyed`, `is_a_vehicle`, `inv_item_get_weapon_slot`, `inv_item_get_anim_group`, `inv_item_dual_wield`, `human_downed_set_mars_kb_interactable`, `human_being_revived_by`, `human_being_revived`, `hud_x_of_y_update`, `hud_x_of_y_add`, `hud_pop_screen`, `hud_get_proto_selection`, `homie_mission_unlock` |
| main menu `0x007c9fc0` (18-row table, tranche 18 A) | UI `0x008430f0`, call at `0x0084315f` | 8: `main_menu_money_shot_check`, `main_menu_maybe_show_voice_dialog`, `main_menu_matchmaking`, `main_menu_continue`, `main_menu_check_open_nat`, `main_menu_check_online_privilege`, `main_menu_check_messages`, `main_menu_check_chat_priv` |

- The eight main-menu handlers re-read in this tranche's `index.txt` match tranche 18 A's table exactly:
  `main_menu_money_shot_check`, `main_menu_maybe_show_voice_dialog` and `main_menu_check_messages` → the shared no-op
  stub `0x007c9f50`; `main_menu_matchmaking` → `0x007c9da0`; `main_menu_continue` → `0x007c9eb0`;
  `main_menu_check_open_nat` → `0x007c9f00`; `main_menu_check_online_privilege` → `0x00842910`;
  `main_menu_check_chat_priv` → `0x00842940`. CONFIRMED.
- `main_menu_money_shot_check`'s second "use" (`main_menu_money_shot_check_1.txt`, rooted at `lua_setfield`
  `0x00dfe830`) is the registrar loop pushing the name of the table's **first** row from a register (the loop at
  `0x007ca11d`-`0x007ca12b`) — not a second handler, the same artefact tranche 09/13/16/18 recorded. CONFIRMED.
- **Two of the main-menu handlers are shared with other registrars under other names** (range run, `0x008460c0`-
  `0x00846110` and `0x00820aa0`-`0x00820ae0`): `0x00842910` is also registered as **`game_user_has_online_privilege`**
  by the game-state registrar `0x00845aa0` (slot written at `0x008460e5`), and `0x00842940` is also
  **`game_user_has_voice_privilege`** (same registrar, `0x008460fb`) and **`vcust_using_lightset`** (vehicle
  customisation registrar `0x00820aa0`, `0x00820ac0`; tranche 15 already listed that slot). CONFIRMED. So B.2 below
  answers for five names, not two.
- The slot just before `homie_mission_unlock` is **`homie_mission_lock` → `0x00a4cf40`** (range run,
  `0x00a22922`/`0x00a2292d`; name written before handler as everywhere in this registrar). Not in this tranche, but its
  body differs from H.1's by one constant and is read here for the comparison (second follow-up dump).
- The neighbouring gameplay slots confirm two siblings already on record: the slot after `hud_x_of_y_update` is
  `hud_x_of_y_remove` `0x00a4e9e0` (`spec-lua-api-behaviour.md` §41.4), and the slot before `light_group_attach_to_camera`
  is `light_group_use_intensity` `0x00a51fe0` (§20.15). CONFIRMED — `index.txt` neighbourhoods.

---

## B. Main menu — 8 functions (registrar `0x007c9fc0`)

**Shared helper, localised strings — `0x0084a1b0(key, wide override)` (CONFIRMED — listing, first follow-up dump):**
a non-null key is hashed with `0x00d9e740` and looked up by `0x00849950`, which scans 14 string tables of `0x54` bytes
starting at `0x02319760` (each used only when its flag byte is set; hash-bucketed, mask at table `+0x10`) and returns
the text; when the key is missing it builds a fallback — the two wide characters `"!!"` (`0x011641dc`) followed by the
key widened (`0x00db06e0`, `0x00849800`), or by the caller's wide override when one is given — and interns it in the
string pool `0x0242dc28` (`0x00db0ba0`). The decompiler types this routine as returning nothing, but every caller read
in this tranche consumes its result register as the string (listing), so it returns the localised wide string, or the
interned `"!!"` + key fallback. HIGH CONFIDENCE for the return.

### B.1 `main_menu_money_shot_check`, `main_menu_maybe_show_voice_dialog`, `main_menu_check_messages` → `0x007c9f50`

All three are the **shared no-op stub** (section A): it calls `lua_gettop`, discards the result and returns **0
values**. CONFIRMED — `main_menu_money_shot_check_0.txt` etc. A binding layer can implement all three as no-ops that
ignore every argument; a script that tests the result (for example `if main_menu_check_messages() then …`) sees nil.

### B.2 `main_menu_check_online_privilege` → `0x00842910` and `main_menu_check_chat_priv` → `0x00842940`

**Arguments:** none read. **Return:** exactly 1 boolean — **always true** for both. CONFIRMED — listings.

- `0x00842940` pushes the literal `true`.
- `0x00842910` pushes the result of `0x008703f0(1)` (through the thunk `0x0086fe10`). `0x008703f0` (second follow-up
  dump) is **"1 ≤ argument ≤ 8"** and nothing else — no platform call, no state — so with the literal 1 it is always
  true.

Both handlers are shared (section A): `game_user_has_online_privilege` is `0x00842910` and
`game_user_has_voice_privilege` and `vcust_using_lightset` are `0x00842940` — all **constant true** on PC. A
reimplementation can return `true` for all five names.

**Correction to tranche 16 E.3 (CONFIRMED — second follow-up dump):** tranche 16 described `0x008703f0(4)` in
`screen_capture_preview_should_upload` as "HYPOTHESIS: the platform's user-generated-content privilege check". Its body
is the 1..8 range test above, so with 4 it is always true: the **"privilege denied" branch of that binding (the
`"@USER_CONTENT_PRIV_DENIED"` warning dialog) is unreachable** on PC (tranche 16's call is the thunk caller at
`0x007afecf` in the dump's caller list). The thunk's two remaining callers (`0x0087c9eb` in `0x0087c920`, and
`main_menu_check_user_content` at `0x007c9e3d`) were not read here; the same reasoning applies to any constant argument
from 1 to 8 (OPEN which values they pass).

### B.3 `main_menu_check_open_nat` → `0x007c9f00`

**Arguments:** none read. **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing, first follow-up dump):** calls `0x0086fe50`, a jump to `0x006b89a0`, whose whole body is
"return 0" (two instructions). Only when that returned non-zero would it show a one-option dialog through `0x007c3d80`
(title `"MENU_TITLE_WARNING"`, body `"MAIN_MENU_NAT_WARNING"`, default option label `0x01301a80` → the wide string
`"OK"` at `0x01164094`). Since the gate is constant zero, **this binding never does anything** on PC.

Note on naming: Ghidra labels `0x006b89a0` with two audio-middleware library symbols (a memory-manager pool-name getter
and a monitor time-stamp getter) because the linker folded identical "return 0" bodies into one address. That label says
nothing about what the main menu meant to ask (a NAT-type query); per `WALLS.md`'s "coincidental match" entries, the
folded symbol is not evidence of the caller's intent — only the body (constant 0) is.

### B.4 `main_menu_matchmaking` → `0x007c9da0`

**Arguments:** 1 nil-gated boolean, default **false**. **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing, first and second follow-up dumps, range run):**
1. Stores the boolean in `0x02296958` (its only reader is the completion callback, step 4).
2. Looks up three localised strings (`"SAVELOAD_AUTOSAVE_NO_DEVICE"`, `"SIGN_IN_DENIAL_CONFIRM"`,
   `"SIGN_IN_AUTOSAVE_PROMPT"`) and hands them, with the completion callback `0x007c9b70` and option bytes, to the
   **pre-game check flow** `0x007aac60`. That routine stores the callback in `0x02242378`, builds the flag word
   `0x02242380` (here `0xfe`: base `0x0e`, plus `0x10`, `0x20`, `0x40`, `0x80`) and stores the three strings in
   `0x02242384`/`0x02242388`/`0x0224238c`. **Those three globals have exactly one reference each — the write** (xref in
   the dump, second xref run for `0x02242384`): the strings are computed and never shown. CONFIRMED dead stores.
3. Because flag `0x10` is set it tail-jumps to the check step `0x007aaa10` (first follow-up dump):
   - flag `0x80` and `0x00b94fd0()` true (it refreshes the save-device state and returns `0x00b94cc0`'s answer —
     HYPOTHESIS: "an autosave would be overwritten") → warning dialog `"SAVELOAD_AUTOSAVE_OVERWRITE"` with
     `"CONTROL_CONTINUE"` / `"CONTROL_CANCEL"`, flow state `0x012fc7e0` = 5, completion `0x007aa840`;
   - else, flag `0x10`: no free save slot (`0x00b95340` false) → `"START_GAME_OUT_OF_SAVE_SLOTS"` with
     `"CONTINUE_WITHOUT_SAVING"` / `"CONTROL_CANCEL"`, state 2; no storage space (`0x00b94880(0)` false) →
     `"START_GAME_OUT_OF_STORAGE_SPACE"`, state 3;
   - otherwise keeps only flag `0x100` and calls the callback with **7** at once.
4. Callback `0x007c9b70` (no Ghidra function boundary; range run): code **1** → `0x00754410(1)`; codes **5-6** → only if
   the top HUD screen's id (`[0x012fced8] + 0x30`) is 7, `0x007ca260(0)` on `0x022969e0`; **every other code,
   including 7** → `0x007adae0(stored boolean)`. All paths finish with `0x007aa960` (flow reset: state −1, closes the
   flow's dialog through `0x007c31b0(0)` if one exists, clears the flags and the callback).
5. `0x007adae0(boolean)` (second follow-up dump): copies the literal `"sr3_city"` (`0x012fca18`, 128-byte bounded copy
   `0x00da7930`) into the level-name buffer `0x02242558`, then `0x0088bb00(boolean)`, `0x008ba170(0, boolean)` and
   `0x00d34d20()`. HYPOTHESIS: start a networked session in the main city map, the boolean selecting a session mode
   (for example public versus private); the three callees were not read.

So `main_menu_matchmaking(flag)` = "run the save/storage pre-checks, then start matchmaking into `sr3_city` with
`flag`". A reimplementation can skip the dead prompt strings.

**Crash shape — CONFIRMED (listing `0x007aaa62`-`0x007aaada`, and the same pattern at `0x007aab15` onward):** each
warning dialog is popped from the four-slot pool with `0x007c3310`, stored in `0x0224237c`, and then used **without a
null check** — as `this` for the body and option calls and for direct writes at `+0x128` (`0x007aaab6`) and `+0x13c`
(`0x007aaada`). With the pool exhausted (`0x007c3310` returns null, interp_tabs Q7.2) these become writes near address
0. Reachability: only when four other dialogs are already open while the main menu starts matchmaking — HYPOTHESIS that
this is rare. A reimplementation should check the dialog before using it (as `0x007c3d80` does, B.3).

### B.5 `main_menu_continue` → `0x007c9eb0`

**Arguments:** none read. **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing, second follow-up dump):** `0x007ade30()`:
1. Raises the **continue request** `0x02242605` = 1 and the **save-enumeration request** `0x02242604` = 1, clears the
   chosen save `0x0224260c` = 0 and sets the save count `0x012fcaa0` = −1 ("not enumerated yet").
2. `0x007ca730(1)` on the main-menu screen object `0x02296bf0`: **only if that screen is the top of the HUD stack**
   (`[0x012fced8]`) and its byte `+0x1e` bit `0x08` is set, fires the Lua hook **`"main_menu_top_input_lock"`** with
   the boolean `true` into the screen's document (`+0x24` → hook record `+0x14`) — i.e. tells the menu script to lock
   input while the continue runs.

The work then happens in the main-menu per-frame update `0x007ae420` (second follow-up dump, CONFIRMED — decompile):
- while `0x02242604` is set and the main menu is the top screen: if the count is −1, starts the save enumeration
  `0x00b94760(list 0x02242608, count 0x012fcaa0, at most 24)`; on a later frame, with the count known, picks one save
  record (records of `0x60` bytes; records with byte `+0x5c` or `+0x5d` set are skipped; the choice key comes from
  `0x00b94c70`, not read — HYPOTHESIS: the newest save) into `0x0224260c`, fires the hook
  **`"main_menu_controller_selected"`** with "a usable save exists" and clears `0x02242604`;
- then, with `0x02242605` set: clears it; **no usable save** → notice dialog `"MAIN_MENU_CONTINUE_UNAVAILABLE"` (title
  `"MENU_TITLE_NOTICE"`, completion `0x007ada00`, via `0x007c3de0`); **a save** → `0x00b95210(save, 1)`; false →
  `0x007ca730(0)` (fires `"main_menu_top_input_lock"` with `false`, unlocking the menu); true → `0x022425ea` = 1,
  `0x007ae0a0(1)`, `0x0059f8c0(−1, 0x007b4cb0, 1)` (HYPOTHESIS: begin loading the chosen save).

**Logic notes (CONFIRMED structure):**
- If the main menu is not the top screen, the request simply waits (both request bytes stay set) — no timeout.
- The lock hook is gated on the screen being on top **and** on its `+0x1e` bit `0x08`; the matching unlock in the
  failure path uses the same gate, so they stay paired. The "unavailable" notice relies on its completion callback
  `0x007ada00` to undo the lock (not read — OPEN).
- No crash shape in the binding; the notice dialog path goes through `0x007c3de0` (not read).

---

## C. Line of sight and lights — 4 functions

### C.1 `los_check_do` → `0x00a52100`

**Arguments:** 1 string = first object, 2 string = second object (both unconditional; both resolved with the generic
resolver `0x00a281a0`). **Return:** exactly 1 number. CONFIRMED — listing.

**Body (CONFIRMED — listing, first follow-up dump):**
1. If either name fails to resolve → pushes **0** and returns.
2. Otherwise asks the line-of-sight cache `0x004ed1e0` about the pair of object handles (`+0x8/+0xc` of each). The pair
   is put in a canonical order first (smaller 64-bit handle first — `0x004ecfc0`/`0x004ecf40` both swap), so
   `los_check_do(a, b)` and `los_check_do(b, a)` share one entry. `0x004ed1e0` answers:
   - a **finished result** for the pair exists in the results list `0x01357790` (`0x004ecfc0`) → 2 or 3, from bit 0 of
     the result's byte `+0x1c` (bit set → 2, clear → 3) — HIGH CONFIDENCE for "finished": it is a second list,
     consulted first, whose entries carry the result bit, while new requests go to the other list;
   - otherwise a **pending request** for the pair exists in the request ring `0x0135779c` (`0x004ecf40`) → 1;
   - otherwise → 0.
3. Jump table (`0x00a52234`, four entries, values above 3 return no value — unreachable):
   - 0 (nothing known) → files a new request `0x004ed320(a, b, priority 300, non-evictable)` and pushes **−1**;
   - 1 (pending) → pushes **−1**;
   - 2 → pushes **1**; 3 → pushes **0**.

So the script contract is a **poll**: −1 = "ask again later", then 1 or 0 (HYPOTHESIS: 1 = clear line of sight, i.e.
result bit 0 = visible). The request creator `0x004ed320` (CONFIRMED — listing): an already-pending pair only has its
priority lowered to the new value if smaller; a new request needs **both** objects to resolve by handle, pass the
`+0x33` bit `0x10` rejection, have kind row `+0x6` bit `0x08`, and be alive; it takes a node from the free list
`0x013577a0` (count `0x013577a4`) and, when the free list is empty, first evicts a pending request whose byte `+0x22`
is set (this binding's own requests are filed with that byte clear, so they are never evicted). It returns null on
any failure.

**Hang shape — CONFIRMED structure, reachability OPEN:** step 3 pushes −1 for state 0 **without looking at whether
`0x004ed320` actually created a request**. If either object fails the creator's own tests (kind row `+0x6` bit `0x08`
absent, or the object dies between the two resolutions) or the pool is exhausted with nothing evictable, every call
re-files, fails again and returns −1 — **a script that loops until the answer is not −1 never finishes**. Whether every
object `0x00a281a0` can return also carries kind bit `+0x6`/`0x08` is OPEN (tranche 11 records that bit only as an
object-show gate). A reimplementation should return 0 (or nil) when the request cannot be filed.

**Ambiguity (CONFIRMED structure):** an unresolvable name returns **0**, the same value as "no line of sight", so a
script cannot tell a misspelt name from a blocked view.

**Staleness (OPEN):** the cache is consulted before anything else, so the first finished result for a pair is returned
by every later call for as long as it stays in `0x01357790`. When the LOS system retires results was not traced.

### C.2 `light_group_attach_to_camera` → `0x00a52050`

**Arguments:** 1 string = light-group name (unconditional). **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing):** resolves the group with `0x00739690` (named lookup on `0x02442750`, kind row `+0xd`
bit `0x10` — the light-group resolver of `spec-lua-api-behaviour.md` §17.24) and requires it alive; then
`0x00596980(handle low, handle high)`:
- the **camera-attached light table** is 16 slots of 16 bytes at `0x013dd5b0` (handle pair, reference count), count
  `0x013dd6b0` (cleared with the whole 256-byte table by `0x00596910`);
- if the handle is already present (`0x00596930`, linear search) its **reference count is incremented**;
- else, if fewer than 16 entries are in use, it is appended with count 1;
- else (table full) **nothing happens** (the false return is ignored by the binding).

**What "attached" does (CONFIRMED — first follow-up dump, `0x00596c00`):** an update pass walks the table and, for each
handle that still resolves to a live light group (same `+0xd` bit `0x10` test), calls **`0x00a2f8b0`** on the group with
the three-float position `0x013c8740`/`0x013c8744`/`0x013c8748` and "replicate" = 0 — skipped entirely while
`0x013c87c8` equals 1. `0x00a2f8b0` (second follow-up dump) is the light group's **set-position** method: it stores the
vector at group `+0x58..+0x60`, sets bit `0x08` of `+0x40`, and pushes the change to every light on the group's list
(`+0x3c`, `0x00a2ebf0` per light) — the position-setting sibling of §17.24's intensity setter `0x00a2f320`, with the same
host-only opcode-`0x43` / sub-type `0x26` replication path (unused here, replicate = 0). HIGH CONFIDENCE that
`0x013c8740` is the camera position (from the binding's name; the global's writer was not traced) — so an attached
group is **moved onto the camera every pass**, locally on each machine.

No crash shape (bounded table, resolved object checked).

### C.3 `light_group_detach_from_camera` → `0x00a520a0`

**Arguments:** 1 string = light-group name (unconditional). **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing):** same resolution; a dead group is treated as "not found". Calls **`0x00d1e1d0`**, which
is a bare "return 1" whose result is discarded (a dead call, CONFIRMED listing — the same family as the dead stubs of
`spec-lua-api-behaviour.md` §41.8). If the group resolved: `0x005969f0(handle)` — find it; if absent, nothing;
otherwise **decrement its reference count**, and only when the count reaches 0 remove it by moving the last table entry
into its slot, clearing the vacated last slot to the "none" handle pair read from `0x0111d558`/`0x0111d55c` with count
0, and decrementing `0x013dd6b0`. CONFIRMED.

**Logic notes (CONFIRMED structure):**
- **Reference-counted, not a toggle:** attaching a group twice needs two detaches before it leaves the camera.
- **Full-table asymmetry:** an attach refused because 16 groups are attached leaves nothing to detach, so the pair is
  harmless, but the refused attach is silent — the 17th group simply never follows the camera.
- **Detach of a dead group cannot clean up:** if the group was attached and then despawned, detach resolves nothing and
  leaves the stale handle (and its count) in the table forever; the update pass skips it (it no longer resolves), but
  it still occupies one of the 16 slots until `0x00596910` clears the table.

### C.4 `level_light_enable_all_do` → `0x00a52250`

**Arguments:** 1 boolean = enable (unconditional — missing = false); 2 number = mode (truncated, unconditional —
missing = 0). **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing, first follow-up dump):** walks one typed object list of the world object manager
`0x03171a64` (index array `+0x28c`, count `+0x294`, each index looked up in the object table `+0x58`; the same manager
whose `+0xb8`/`+0xc0` vehicle list `vehicle_clear_all_radio_locks` walks, `spec-lua-api-behaviour.md` §30.5) and calls `0x008e07d0(enable, 4, mode)` on every object, i.e. on **light slot 4** of each object (an
object carries seven per-slot state bytes at `+0x7a..+0x80`; `0x008df2a0(slot, state)` writes one, mirrors slot 0
into a packed table `0x0250c4a0`, and refreshes the object through `0x008ddb80` when its `+0x33` bit `0x08` is set):
- mode **0** → slot-4 state = 0 when enabling, 1 when disabling;
- mode **1** → state = 2 when enabling, 3 when disabling;
- mode **2** → if the slot is not already 4 (enabling) / 5 (disabling): releases the object's current dynamic light
  instance (`+0xbc`, `0x008df310`), asks the pool for a new one with `0x00596660(object, enable)` and stores it in
  `+0xbc`; state = 4/5 when an instance was obtained, else falls back to 2/3;
- **any other mode → nothing** (no default branch).

`0x00596660` (CONFIRMED — first follow-up dump): takes an instance from the free list `0x013dcc5c`; when that is empty,
it steals one from the active ring `0x013dcc58` whose owner no longer resolves or fails a range/visibility test
(`0x005de950` against `0x008df270()`), releasing the old owner's slot; it then picks a random light definition with the
project's PRNG `0x00dab660` from one of two per-type tables (`0x012e6608`/`0x012e6610` with counts at `+4`, indexed by
the object's byte `+0x92`, the table chosen by the enable flag), and links the instance into the active ring. Returns
null when nothing can be stolen.

**Meaning (HYPOTHESIS):** the `+0x28c` list is the level's light-bearing objects, slot 4 their "level light" channel,
and modes 0/1/2 three ways of switching it (static on/off, a second static pair, and a pooled dynamic light). The
game-clock note's dusk pass (`interp_game_clock.md` §3.4, `0x008cc930(2)`) touches a related object flag, not this slot
— not the same mechanism.

**Crash shapes (CONFIRMED shape, reachability HYPOTHESIS):** the manager pointer `0x03171a64` and every object pointer
taken from `+0x58` are used without null checks (`0x00a5228b`-`0x00a522a8`, `0x00a522c8`-`0x00a522da`); a call before
the world manager exists, or with a hole in the object table, would read through null. In practice the manager exists
whenever scripts run (HYPOTHESIS). **No network replication** — each machine switches its own lights.

---

## D. World and object queries — 2 functions

### D.1 `is_syn_tower_destroyed` → `0x00a50db0`

**Arguments:** none read. **Return:** **2 values (true, false) when destroyed, 1 value (false) otherwise.** CONFIRMED —
listing.

**Body (CONFIRMED — listing):** hashes the literal `"tower_dmg"` (string `0x0117f9bc`) with the name hash `0x00d9e8b0`
and scans the **active city-zone-swap list** `0x0242dc90` (count `0x0242dc88`) with `0x0084a7c0` — exactly the lookup
of `city_zone_swap_is_active` (`rederive_28.md` §28.18, re-read in this tranche's range run at `0x00a42a60`). So it is
`city_zone_swap_is_active("tower_dmg")` with the name hard-coded (HIGH CONFIDENCE that "tower_dmg" is the zone swap that
shows the destroyed Syndicate tower, from the name).

**Return-shape defect — CONFIRMED (listing `0x00a50dde`-`0x00a50dfc`):** when the swap is active the handler pushes
`true`, **then falls through and also pushes `false`**, and returns 2 (it computes the count as "found + 1"). When not
active it pushes only `false` and returns 1. `city_zone_swap_is_active` pushes exactly one boolean in both cases. A
script using the result in a plain `if` or assignment sees the first value (`true`) and is unaffected; a script that
captures two values, passes the call straight into another call's argument list, or wraps it in a table constructor
sees an extra `false`. A reimplementation should match the observable shape (two values when destroyed) only if a
script depends on it; otherwise one boolean is the evident intent.

### D.2 `is_a_vehicle` → `0x00a51a60`

**Arguments:** 1 string = object name (unconditional). **Return:** exactly 1 boolean. CONFIRMED — listing.

**Body (CONFIRMED — listing):** null name → false. Otherwise the named-object lookup `0x0062a190` (kind row `+0xb` bit
`0x08`; the per-kind lookup underneath the vehicle resolver `0x00a281e0`, tranche 11), alive, and then **virtual
`+0x68`** of the object must return true. Any failure → false.

HYPOTHESIS: kind `+0xb` bit `0x08` is a broad "physical / movable object" class and virtual `+0x68` the "is vehicle"
query (from the binding's name; the vtable target was not resolved). It does not go through the vehicle resolver `0x00a281e0` or the
`#PLAYER#` sentinels and takes no handle — only a name registered for an object of that kind can answer true. No
crash shape.

---

## E. Inventory — 3 functions

All three use the **weapon-table lookup `0x00b81220`** (CONFIRMED — listing): null name → null; otherwise a linear scan
of the weapon records at `0x028dc4dc` (count `0x028dc4e0`, stride **`0x7ec`**), considering only records with byte
`+0x18` bit `0x04` set, comparing the record's name pointer (`+0x0`) with CRT `__stricmp` — case-insensitive, first
match wins. No crash shape (null name handled before `__stricmp`).

### E.1 `inv_item_get_weapon_slot` → `0x00a50f90`

**Arguments:** 1 string = weapon name (unconditional). **Return:** exactly 1 number. CONFIRMED — listing.

**Body (CONFIRMED — listing, first follow-up dump):** unknown or null weapon → **255** (double constant `0x012a2d88`).
Known weapon → `0x008dc690(weapon)`, pushed as a number. `0x008dc690` returns 10 when `0x00b81870(weapon)` is true and
the byte `+0x54` otherwise — but `0x00b81870` is exactly "byte `+0x54` equals 10" (CONFIRMED — first follow-up dump),
so **both branches return byte `+0x54`**; the test is redundant. So the result is the weapon record's slot byte `+0x54`
(HYPOTHESIS: the weapon-wheel slot index, 10 being a special slot), or 255 for no such weapon. Scripts must treat
**255** as "no such weapon".

### E.2 `inv_item_get_anim_group` → `0x00a50860`

**Arguments:** 1 string = weapon name (unconditional). **Return:** exactly 1 value — a string, or nil. CONFIRMED —
listing.

**Body (CONFIRMED — listing):** unknown or null weapon → the literal string **`"Default"`** (`0x0111c504`). Known weapon
→ the record's anim-group index `+0x30` through `0x004ccfe0`: a **bounded** lookup (index ≥ 0 and below `0x03171c1c`)
into the name-pointer table `0x03519838`, returning null out of range; the string (or nil, via `lua_pushstring(null)`)
is pushed. So the return is the weapon's animation-group name, `"Default"` for an unknown weapon, and **nil** only for a
known weapon whose group index is out of range (or whose table entry is null). No crash shape.

### E.3 `inv_item_dual_wield` → `0x00a50ef0`

**Arguments:** 1 string = character (unconditional; generic resolver `0x00a281a0`); 2 number = inventory slot
(truncated, unconditional — missing = 0). **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing, first follow-up dump):**
1. Calls `0x00d1e240` — another bare "return 1" whose result is discarded (dead call, like C.3's `0x00d1e1d0`).
2. If the character resolves: the slot number is passed through `0x00a50610`, a jump table that maps 0..7 to
   themselves and **anything else to `0xff`** (bounded).
3. The weapon item in that slot: `0x005fec20(slot)` on the character's inventory sub-record (`+0x1b78`, the same
   sub-record `inv_item_remove` uses, `spec-lua-api-behaviour.md` §17.25) — for slots 0..8 it resolves the item handle
   stored at sub-record `+8·slot` (covering 0..7, all the binding can pass besides `0xff`); `0xff`, 9, 11 and 13+
   return null; 10 and 12 use dedicated fields. So an out-of-range slot yields null and the binding does nothing.
4. With an item: **re-equips slot 0** (`0x00600720(0, 1, 0, 0)`), sets **bit `0x04` of the item's byte `+0x1a1`**, then
   **re-equips the requested slot** (`0x00600720(slot, 1, 0, 0)`).

`0x00600720` is the inventory's equip-slot routine (first follow-up dump, 892 instructions, head read only): with its
fourth argument false it first runs the authority check `0x008ae480` (the double-gate setter's first gate, tranche 13
B.3), and **when not authoritative it sends an opcode-`0x41` / sub-type-7 request to the owner and returns** without
equipping locally; otherwise it switches on the slot. CONFIRMED for that head; the equip body itself was not read.

**Meaning (HIGH CONFIDENCE):** "make the weapon in this slot dual-wielded": bit `0x04` of item `+0x1a1` is the
dual-wield flag, and the slot-0 / slot round trip forces the character to re-draw the weapon so the flag takes effect.

**Network / logic notes (CONFIRMED structure):**
- The flag bit is written **locally only**, on every machine that runs the script; only the two re-equips are
  forwarded to the owner when this machine is not authoritative. So on a non-owner the flag is set on the local copy of
  the item, the owner re-equips without the flag (unless it ran the same script) — HYPOTHESIS that this can desync the
  visible dual-wield state in co-op.
- There is **no way to clear** the flag through this binding (it only ORs the bit in), and no check that the weapon
  supports dual wielding.

---

## F. Downed and revived characters — 3 functions

### F.1 `human_being_revived` → `0x00a4d1c0`

**Arguments:** 1 string = character (unconditional; `0x00a281a0`). **Return:** exactly 1 boolean = **bit 0 of the
character's byte `+0x1905`**; false when the name does not resolve. CONFIRMED — listing. HYPOTHESIS: bit 0 = "a revive
is in progress on this character" (the byte is the same one F.3 writes bits `0x30` into; its writers were not traced).
No crash shape.

### F.2 `human_being_revived_by` → `0x00a4f800`

**Arguments:** 1 string = character (unconditional; `0x00a281a0`). **Return:** exactly 1 value — a string, or nil.
CONFIRMED — listing.

**Body (CONFIRMED — listing, first follow-up dump):**
1. Reads the **reviver handle** at character `+0x18f0`/`+0x18f4`; a zero handle → nil.
2. Resolves it by handle and keeps it only if it is **player kind** (kind row `+0xa` bit `0x02`) and alive; anything
   else (an NPC homie, a dead player, a stale handle) → nil.
3. Pushes `0x00a353d0(reviver)` — the object-to-script-name helper of tranche 11 (§41.6 `get_closest_npc_to_object`):
   for a player-kind object, **on the host** (session exists, host) it returns the name at object `+0x18`; **otherwise**
   it returns `0x00a1fda0(object)`.

`0x00a1fda0` (CONFIRMED — first follow-up dump) formats into one shared static buffer `0x026e7e20`: on a co-op
**client** `"#NETID#"` followed by the object's 16-bit network id (`0x008add70`), and **with no session or on the host**
`"#NETID#"` followed by the digit `0` (the format string at `0x011783cc` reads `%s0`; the host never gets here from
`0x00a353d0`, so in practice this branch is the no-session case).

**Consequences (CONFIRMED structure):**
- **Only players can be reported.** A character revived by a follower or other NPC yields nil, the same as "never
  revived" — scripts cannot tell those apart through this binding.
- **What a script sees depends on the session state.** Host: the player's own script name. Client: `"#NETID#<id>"`.
  **Single player (no session object, §26.28): always the literal `"#NETID#0"`**, never the player's script name. A
  script that compares the result against `"#PLAYER#"` or a player's name therefore never matches in single player.
  Whether `"#NETID#…"` strings are accepted back by the name resolvers (`0x00a280c0` tries a per-kind registry lookup
  `0x00a27e20` and then an exact `"#PLAYER#"` compare — neither parses a `#NETID#` prefix in the code read) is OPEN; if
  not, feeding the result back into another binding resolves nothing.
- The returned string lives in a shared static buffer, so it is only valid until the next `0x00a1fda0` call — harmless
  here because `lua_pushstring` copies it at once.

No crash shape (every step null-checked).

### F.3 `human_downed_set_mars_kb_interactable` → `0x00a4d180`

**Arguments:** 1 string = character (unconditional; `0x00a281a0`). **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing, first and second follow-up dumps):** `0x00973a10(character)`:
1. Requires state `+0xcc8` == **6** — the downed state of `spec-lua-api-behaviour.md` §7.13 `human_is_downed` — else
   nothing.
2. Requires the **downed-interaction class** `0x009735d0(character)` == **3**. `0x009735d0` (CONFIRMED — first
   follow-up dump) returns −1 when force-flag bit 17 of `+0x1c9c` is set; 0 for a follower (`0x0097e280`: not a player
   and has a squad leader), and 0 for a player when a co-op partner exists (`0x009df3d0`) and a further set of gates
   passes (local ownership `0x008addb0` or `0x008847c0` on its handle, minigame mode `0x006a3910` ≠ 14, and
   `0x00614d50`/`0x00682ad0` both false); otherwise it classifies: **2** when the character's template id
   (`[+0xf0] + 4`) equals the global `0x0149f928`; **3** when it equals `0x0149f92c`; 4 for a further class
   (`0x005e6ce0` and not `0x00971cf0`); **1** for a brute (`0x0052a230`, tranche 18 F.3) that passes `0x00943190`, fails
   `0x009422d0`, and does **not** have force-flag bit 22 — the very bit `npc_brute_set_no_downed_finisher` sets (tranche
   18 F.3), which ties class 1 to the brute downed finisher; −1 otherwise.
3. Adds an **object indicator** on the character: `0x008f96f0(handle, type 7, 0x13, flags 3, 100.0)` — the indicator
   creator (local add when flag bit 1 is set, broadcast of an opcode-`0x40` / sub-type-2 record when bit 2 is set; it
   allocates the 16-bit indicator id from the counter `0x01308570` and returns it) — and stores the id at character
   `+0x18fc`.
4. Sets bits `0x30` of character byte `+0x1905` (the byte F.1 reads bit 0 of).
5. Tail-calls `0x005e4190(1)`, which only writes the global byte **`0x0149f924` = 1** — the flag that sits right next to
   the two template-id globals of step 2 (`0x0149f924`/`0x0149f928`/`0x0149f92c`; readers `0x005e4180`, called from
   `0x005b7e90` and `0x00973140`).

**Meaning (HYPOTHESIS):** the two template ids at `0x0149f928`/`0x0149f92c` are two special story characters whose
downed state has a scripted interaction; class 3 is the one this binding (whose name reads "Mars" / "KB", HYPOTHESIS:
Killbane in the final mission) makes interactable with an on-screen indicator, and `0x0149f924` announces that the
interaction is armed.

**Logic defects (CONFIRMED structure):**
- **No "already done" check:** neither `+0x18fc` nor bits `0x30` of `+0x1905` are tested first, so a second call while
  the character is still downed adds a **second indicator** and overwrites `+0x18fc` with the new id — the first
  indicator's id is lost (HYPOTHESIS: it stays on screen until the indicator system times it out).
- Silent no-op for any other state or class — a script calling it a frame before the character reaches state 6 gets
  nothing and no error.
- The indicator broadcast (flags bit 2) goes out from **every** machine that runs the script, with no host gate in this
  path (HYPOTHESIS: duplicate indicators on co-op peers that also run it).

No crash shape in the binding itself (the character is resolved and checked; `0x009735d0` reads `[+0xf0] + 4` through
`0x005e4350`/`0x005e4450` without checking `+0xf0`, the same unchecked template pointer tranche 18 F.3 noted — shape
only).

---

## G. HUD — 4 functions

### G.1 `hud_x_of_y_add` → `0x00a4e890`

**Arguments:** 1 number = slot index (truncated, unconditional); 2 string = label key (unconditional); 3 number = **x**
(the current count, truncated, unconditional); 4 number = **y** (the total, truncated, unconditional); 5 nil-gated
boolean, default **false**; 6 nil-gated number = flags (truncated), default **3**. **Return:** 0 values. CONFIRMED —
listing (argument order checked against the stack-slot arithmetic of the listing).

**Body — `0x007edf10(index, key, x, y, boolean, flags)` (CONFIRMED — listing):**
- **Bounded:** does nothing unless `index` < 4 (unsigned compare at `0x007edf58`, so negatives are rejected too). The
  "x of y" HUD table is **4 slots of `0x34` bytes at `0x022b6758`** (CONFIRMED by the HUD initialiser `0x007eb0e0`,
  which walks exactly 4 such slots, and calls virtual `+0xc` on each — the slots are objects with a vtable at `+0`).
- Label: the key is localised with `0x0084a1b0` (the localised-string lookup with a `"!!"`-prefixed fallback for a
  missing key, front matter of section B); `0x00849ff0` maps the result to the string's id when it lies in one of the
  two string pools (`0x0242dc10`/`0x0293cc10` ranges), and slot `+0x8` takes that id, or the default `0x029c9960` when
  the id is −1. Slot `+0x4` receives **the raw key pointer from Lua** (see OPEN).
- x → `+0x24` and, if different, `+0x28`; y → `+0x2c`; boolean → `+0x30`; flags → `+0x1c`; `+0xd` = 1; each real change
  sets the dirty byte `+0x20`.
- **flags bit 2 (`0x2`)**: broadcasts an opcode-`0x3e` record (8-bit sub-type 3, 8-bit slot index) to the session
  (nothing without one) — HYPOTHESIS: tells co-op peers to show the same counter.
- **flags bit 1 (`0x1`)**: `+0xc` = 1, `+0x18` = index + 1, and queues the slot for the HUD update with `0x007eaec0`
  (which de-duplicates; its 8-entry queue `0x022b65e0` cannot overflow from this path — the other adders stop at 4
  queued entries and there are only 4 x-of-y slots — CONFIRMED arithmetic, third range run and second xref run).

No crash shape (index bounded).

### G.2 `hud_x_of_y_update` → `0x00a4e980`

**Arguments:** 1 number = slot index, 2 number = x (both truncated, unconditional). **Return:** 0 values. CONFIRMED —
listing.

**Body (CONFIRMED — listing):** slot = `0x007eb200(index)` = `0x022b6758 + 0x34·index` — **no bound and no sign check
at all** (`0x007eb200` is three instructions: multiply, add, return). Writes `+0x24` = x and, if `+0x28` differs,
`+0x28` = x and the dirty byte `+0x20` = 1.

**Crash shape — CONFIRMED (listing `0x00a4e9b5`-`0x00a4e9c8`):** `hud_x_of_y_add` refuses indices ≥ 4, but
`hud_x_of_y_update` **writes** two dwords and one byte at `0x022b6758 + 0x34·index + {0x20, 0x24, 0x28}` for **any**
32-bit index. Index 4 lands inside the next HUD element table that `0x007eb0e0` initialises right after this one
(`0x022b6828`, objects with vtables), larger indices anywhere in the writable image or beyond, and a large or negative
index (e.g. the −2³¹ that truncation gives for NaN or huge numbers) at an unmapped address — an access violation. This is
a **script-controlled out-of-bounds write**, worse than its sibling `hud_x_of_y_remove`'s unbounded slot (§41.4).
Reachable whenever a script passes a slot number outside 0..3 — for example a 1-based slot 4, or a computed index
(a nil or non-number argument reads as 0 and is harmless). A reimplementation must bound the index to 0..3 (and probably require the slot to be
active, `+0xd` = 1, which this binding does not check either — an update to an unused slot silently writes values a
later `add` overwrites).

### G.3 `hud_pop_screen` → `0x00a4e710`

**Arguments:** 1 string = screen name (unconditional); 2 nil-gated number = flags (truncated), default **1**.
**Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing):** `0x007b4ad0(name)` on the HUD screen manager `0x012fced8`: hashes the name with
`0x00d9e8b0` and returns the registered screen (manager array `+0x30`, count `+0x2c`) whose `+0x74` equals the hash, or
null. With a screen:
- **flags bit 1** → `0x007b4790(screen)` on the manager: pops **only if the screen is the top of the stack** (depth
  `+0x28` > 0, `+0x24` > 0, and the entry at manager `+4·depth` is this screen; a null argument would pop whatever is on
  top, but this binding never passes null). Under the critical section `0x02247b80` (`0x00d9f620`/`0x00d9f630`) it
  decrements the depth and zeroes manager `+0x148`, `+0x16c`, `+0x170`. **A screen that is open but not on top is
  silently not popped.**
- **flags bit 2** → `0x008b8d20(screen)`: broadcasts an opcode-`0x40` record, sub-type **`0x24`**, carrying the screen's
  id (`+0x30`, 4 bytes) to the session (nothing without one).

**Logic notes (CONFIRMED structure):** the broadcast (bit 2) is sent **whether or not the local pop happened** and
whether or not bit 1 is set, so a peer can be told to pop a screen this machine kept. The pop only removes the stack
entry and clears three manager fields; it does not call any close method of the screen itself in the code read
(HYPOTHESIS: the screen's own teardown runs from the per-frame manager update). No crash shape (the manager is a static
object; the screen pointer is checked).

### G.4 `hud_get_proto_selection` → `0x00a4dd70`

**Arguments:** none read. **Return:** exactly 1 number. CONFIRMED — listing.

**Body (CONFIRMED — listing, first follow-up dump):** `0x00840f30()`: if the "selection made" byte `0x02317620` is
clear → **−1**; otherwise clears the byte `0x02317621` and returns the dword `0x013010fc` (file value −1).

Writers (second range run, CONFIRMED — listing): `0x00840f50` (code with no function boundary) sets `0x02317621` = 1,
clears `0x02317620` and opens UI screen 13 (`0x007074a0(0xd, 1)`) — HYPOTHESIS: "open the prototype-selection
screen"; and the Lua function **`game_proto_select`** → `0x00840fa0` (registered alone by the pointer-before-name
registrar `0x00841020`, called from the UI bring-up at `0x0084316b` — third range run and third xref run) computes the selection and stores it in
`0x013010fc` with `0x02317620` = 1 (section J item 6 for its own defect).

So the script contract is a poll: −1 until the selection screen reports, then the selected index (0-based, see J.6),
**repeatedly** — the getter does not clear "selection made", it clears the other byte (HYPOTHESIS: "screen open").
No crash shape.

---

## H. Homie missions — 1 function (plus its `lock` sibling)

### H.1 `homie_mission_unlock` → `0x00a4cf70`

**Arguments:** 1 string = mission/activity name (unconditional). **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing, first and second follow-up dumps):**
1. `0x007e7470(name)`: hashes the name with `0x00d9e7e0` (null → 0) and looks it up with `0x007e5e10` in the
   **activity/mission record table** of tranche 13 E.1 (two parts: `0x022ad780`, count `0x022ad758`, then `0x022b0180`,
   count `0x022ad75c`; stride `0x180`; hash at record `+0x20`) → combined index, or −1.
2. `0x007e8af0(index, 2)`: **bounded** (index ≥ 0 and < the total `0x022ad754`, else nothing); sets record **`+0xc0` =
   2**, then tail-calls the refresh pass `0x007e80a0`.
- `0x007e80a0` (CONFIRMED — first follow-up dump) walks every record; for each one whose contact handle `+0x78/+0x7c`
  resolves to a live human (kind row `+0xa` bit `0x04`) owned by this machine (`0x008addb0(object, 0)`) **and** whose
  record is currently **not** available (`0x007e74f0` false), it calls `0x00853ea0(object, 0, 0)` (HYPOTHESIS: release /
  despawn the contact character).
- `0x007e74f0` (second follow-up dump) shows what `+0xc0` means: after its base checks (`0x00bc1250` on `+0x24`, not
  `0x007e6240`), **`+0xc0` = 2 → available**, **`+0xc0` = 1 → never available**, and `+0xc0` = 0 → available only for
  progress states `+0xbc` 0 (with `0x0071dfd0(+0x20)`), 1 or 8.

**`homie_mission_lock` → `0x00a4cf40`** (sibling, second follow-up dump) is byte-for-byte the same body with value **1**.
So the pair is a script override: unlock forces "available", lock forces "never available"; **neither can restore the
default 0** (no binding in this pair writes 0).

**Notes (CONFIRMED structure):** an unknown name is ignored safely (index −1 fails the bound). There is **no host gate
and no network replication** — each machine overrides its own copy of the record; whether these records are synchronised
some other way is OPEN. The refresh pass runs on every call, even when the value did not change. No crash shape.

---

## I. Method notes for the follow-up runs

- First follow-up dump (depth 0, `maxinsn:1000`): `0x007aaa10 0x007ca730 0x006b89a0 0x00849950 0x004ecfc0 0x004ecf40
  0x00596930 0x00596c00 0x00596660 0x008df2a0 0x008df310 0x00b81870 0x009735d0 0x008f96f0 0x005e4190 0x00a1fda0
  0x007eb0e0 0x00840f30 0x007e5e10 0x007e80a0 0x007eaec0 0x00849ff0 0x00596910 0x00600720` (the last over 892
  instructions; only its head was read).
- Range run: `0x007c9b70-0x007c9c40` (B.4's callback, no function boundary), `0x00a22915-0x00a22948`
  (`homie_mission_lock`/`unlock` slots), `0x008460c0-0x00846110` and `0x00820aa0-0x00820ae0` / `0x00820c10-0x00820c40`
  (the other registrations of `0x00842910`/`0x00842940`), `0x00596a70-0x00596ae0` (two list-walking callers of the
  camera-light attach/detach helpers), `0x00a42a60-0x00a42ab0` (`city_zone_swap_is_active`, for D.1).
- Xref run (`xrefs:40`): `0x013dd6b0 0x02296958 0x00596980 0x005969f0 0x007eb200 0x00a4e980 0x022b6fe8 0x013577a0`.
- Second follow-up dump (depth 0, `maxinsn:1500`): `0x007adae0 0x007aa960 0x00a280c0 0x005e4350 0x005e4450 0x00a4cf40
  0x008703f0 0x007e74f0 0x00a2f8b0 0x007ae420 0x00b94fd0 0x0097e280`.
- Second range run: `0x00840f50-0x00841030` (writers of the proto selection, G.4). Second xref run (`xrefs:60`):
  `0x022b6600 0x007eaec0 0x0149f924 0x02317620 0x00596c00 0x005e4180 0x02242384`.
- Third range run: `0x00841020-0x00841060` (`game_proto_select`'s one-name registrar), `0x007edb70-0x007edbb0`,
  `0x007eeae0-0x007eeb50`, `0x007eec40-0x007eec80` (the other HUD-queue adders' count checks). Third xref run:
  `0x00841020 0x013c8740 0x022b65e0`.
- All handlers were taken from the registrar slot immediately after the name string (`index.txt`), cross-checked by
  the range run for the one neighbourhood where the dumper listed several code pointers of the same registrar.

---

## J. Cross-function observations

1. **Five names that do nothing, or nothing variable.** Three `main_menu_*` names are the shared no-op stub (B.1); two
   more return a constant `true` (B.2) — and those two handlers are also bound to three further names elsewhere
   (`game_user_has_online_privilege`, `game_user_has_voice_privilege`, `vcust_using_lightset`). `main_menu_check_open_nat`
   is a real function whose only gate is a folded "return 0" body, so it is a no-op too (B.3). So **6 of the
   main-menu table's 18 rows are effectively inert on PC** (its three stub rows, `check_open_nat`, and the two
   constant-true rows). Name-based expectations
   ("checks the NAT", "checks the chat privilege") would mislead a reimplementation.
2. **The "privilege" helper `0x008703f0` is a range test, not a platform query** (B.2) — this corrects tranche 16 E.3's
   HYPOTHESIS and makes that binding's "privilege denied" dialog dead code. Worth re-checking any other note that cites
   `0x0086fe10`/`0x008703f0` as a privilege check.
3. **Poll-style bindings and their sentinels:** `los_check_do` (−1 = pending, 0/1 = answer, 0 also for "unknown name",
   C.1), `hud_get_proto_selection` (−1 until chosen, then sticky, G.4), `inv_item_get_weapon_slot` (255 = unknown, E.1),
   `inv_item_get_anim_group` (`"Default"` for unknown, nil for a known weapon with a bad group, E.2),
   `human_being_revived_by` (nil for "no player reviver", `"#NETID#…"` instead of a script name off-host, F.2). A
   reimplementation must keep each sentinel exactly.
4. **Sibling pairs where one checks and the other does not** (fix templates, as in tranche 18 I.3):
   - `hud_x_of_y_add` bounds the slot to 0..3; `hud_x_of_y_update` (G.2) and `hud_x_of_y_remove` (§41.4) do not;
   - `city_zone_swap_is_active` returns one boolean; `is_syn_tower_destroyed`, the same lookup with a fixed name, returns
     two values when true (D.1);
   - `0x007c3d80` (B.3's dialog helper) null-checks the pooled dialog; the pre-game check flow `0x007aaa10` (B.4) does
     not.
5. **Dead calls inside live bindings:** `0x00d1e1d0` (C.3) and `0x00d1e240` (E.3) are bare "return 1" functions whose
   results are discarded — two more members of the dead-stub family `spec-lua-api-behaviour.md` §41.8 recorded
   (`0x00d34cd0`, `0x00d21330`). `0x008dc690`'s "is slot 10" test (E.1) is redundant. The three sign-in prompt strings
   of B.4 are computed and never read.
6. **A crash-shaped neighbour found while tracing G.4 — `game_proto_select` → `0x00840fa0` (outside this tranche):** it
   reads two numbers, subtracts 1 from each and, **for any result below 3 — including negative ones** (signed compare
   at `0x00840fda`, `JGE` skips only values ≥ 3) — writes the byte 1 at `[local + value]`, where the local is a 4-byte
   stack slot directly above the four saved registers (EBX, EBP, ESI, EDI at 4-byte steps below it). So an argument of
   **0, nil or a non-number** (value −1) sets the top byte of the **caller's saved EBX**, and any argument from −15 to
   0 (values −16..−1) overwrites one byte of the saved EDI/ESI/EBP/EBX — registers the Lua VM's C-call dispatcher
   relies on after the return. (More negative values land below the stack pointer, in dead space.) CONFIRMED structure (second range run, listing `0x00840fa0`-`0x0084101e`); outcome HYPOTHESIS
   (likely a crash or a corrupted VM pointer). It then stores the lowest index 0..3 whose byte is still 0 as the
   selection (for valid arguments 1..3 that is the lowest of 0..2 not named, 1-based, by either argument) and raises
   `0x02317620`. Not one of the 25 names; recorded here because it is the only writer of G.4's value and it feeds the
   compatibility-fix backlog.
7. **Local-only effects with no host gate:** `level_light_enable_all_do` (C.4), the camera light attach/detach (C.2/C.3,
   replicate flag passed as 0), `inv_item_dual_wield`'s flag bit (E.3), `homie_mission_lock`/`unlock` (H.1). Paired
   with **unconditional broadcasts** in `human_downed_set_mars_kb_interactable` (F.3, indicator broadcast from every
   machine) and `hud_pop_screen` (G.3, broadcast even when nothing was popped). In single player every broadcast is a
   no-op because there is no session object (§26.28) — that is the expected no-session path, not evidence of anything
   about single player.
8. **Session-dependent return values:** `human_being_revived_by` returns a script name on the host, `"#NETID#<id>"` on a
   client and `"#NETID#0"` with no session (F.2) — the first binding in this series whose *return value* (not just its
   side effect) differs between single player and the co-op host.
9. **`0x013c8740`** (C.2) is a 16-byte position vector with 239 references across 185 functions (third xref run), read
   by the camera-light pass; HIGH CONFIDENCE it is the camera position, recorded here because several earlier notes
   meet it without naming it.

---

## K. OPEN

- A/B.2: the constants passed to `0x008703f0` by `0x0087c920` and `main_menu_check_user_content`.
- B.4: `0x00b94fd0`/`0x00b94cc0`, `0x00b95340`, `0x00b94880` (what each pre-check really tests); `0x0088bb00`,
  `0x008ba170`, `0x00d34d20` (what the matchmaking boolean selects); `0x00754410` (callback code 1).
- B.5: `0x00b94c70` (save choice key), `0x00b95210`, `0x007ada00` (does the "unavailable" notice unlock input?).
- C.1: whether every object `0x00a281a0` resolves carries kind row `+0x6` bit `0x08` (decides whether the −1 hang is
  reachable); when finished LOS results leave `0x01357790`; the meaning of result bit 0.
- C.2: the writer(s) of `0x013c8740` (camera position HYPOTHESIS) and of `0x013c87c8`; who calls the update pass
  `0x00596c00` (its one caller is `0x00702a50`).
- C.4: the identity of the `+0x28c` object list and of light slot 4; `0x005de950`'s test.
- D.2: the target of virtual `+0x68` on kind `+0xb` bit `0x08` objects.
- E.1: what slot byte value 10 means.
- E.3: whether the dual-wield bit is ever cleared elsewhere; the equip body of `0x00600720`.
- F.1/F.3: the writers and readers of `+0x1905` bits 0 and `0x30`; which characters `0x0149f928`/`0x0149f92c` hold;
  readers of `0x0149f924` (`0x005b7e90`, `0x00973140`).
- F.2: whether any resolver accepts a `"#NETID#…"` string (round-trip of the returned name).
- G.1: who reads slot `+0x4` (the raw Lua key pointer stored by `hud_x_of_y_add`) — if anything dereferences it after
  the Lua string is collected, it is a dangling pointer; the meaning of argument 5 (`+0x30`).
- G.3: whether the HUD manager runs the popped screen's close logic elsewhere; the receiver of opcode `0x40`/`0x24`.
- G.4: the screen opened by `0x007074a0(0xd, 1)` and the role of `0x02317621`.
- H.1: whether activity records are synchronised between co-op machines; `0x00853ea0`'s effect on contact characters.

---

## L. Direct-answer table

| # | name | registrar | handler | status |
|---|---|---|---|---|
| 1 | `main_menu_money_shot_check` | main menu `0x007c9fc0` (under UI) | `0x007c9f50` | resolved — **shared no-op stub** |
| 2 | `main_menu_maybe_show_voice_dialog` | main menu | `0x007c9f50` | resolved — **shared no-op stub** |
| 3 | `main_menu_matchmaking` | main menu | `0x007c9da0` | resolved — save/storage pre-checks, then start matchmaking into `sr3_city` with the boolean; **unchecked pooled dialog** in the pre-check flow |
| 4 | `main_menu_continue` | main menu | `0x007c9eb0` | resolved — input-lock hook, enumerate saves, load the chosen save or show "unavailable" |
| 5 | `main_menu_check_open_nat` | main menu | `0x007c9f00` | resolved — gate is a folded "return 0": **never does anything** |
| 6 | `main_menu_check_online_privilege` | main menu | `0x00842910` | resolved — **always true** (also `game_user_has_online_privilege`) |
| 7 | `main_menu_check_messages` | main menu | `0x007c9f50` | resolved — **shared no-op stub** |
| 8 | `main_menu_check_chat_priv` | main menu | `0x00842940` | resolved — **always true** (also `game_user_has_voice_privilege`, `vcust_using_lightset`) |
| 9 | `los_check_do` | gameplay `0x00a20840` | `0x00a52100` | resolved — cached LOS poll: −1 pending, 1/0 answer, 0 for unknown names; **−1 forever if the request cannot be filed** |
| 10 | `light_group_detach_from_camera` | gameplay | `0x00a520a0` | resolved — reference-counted removal from the 16-slot camera-light table |
| 11 | `light_group_attach_to_camera` | gameplay | `0x00a52050` | resolved — reference-counted add; attached groups are moved to the camera position each pass |
| 12 | `level_light_enable_all_do` | gameplay | `0x00a52250` | resolved — sets light slot 4 on every object of one world list, three modes, local only |
| 13 | `is_syn_tower_destroyed` | gameplay | `0x00a50db0` | resolved — `city_zone_swap_is_active("tower_dmg")`; **returns (true, false) when true** |
| 14 | `is_a_vehicle` | gameplay | `0x00a51a60` | resolved — named lookup (kind `+0xb`/`0x08`), alive, virtual `+0x68` |
| 15 | `inv_item_get_weapon_slot` | gameplay | `0x00a50f90` | resolved — weapon record byte `+0x54`, 255 when unknown |
| 16 | `inv_item_get_anim_group` | gameplay | `0x00a50860` | resolved — anim-group name, `"Default"` when unknown, nil for a bad group index |
| 17 | `inv_item_dual_wield` | gameplay | `0x00a50ef0` | resolved — sets the slot's item dual-wield bit locally and re-equips |
| 18 | `human_downed_set_mars_kb_interactable` | gameplay | `0x00a4d180` | resolved — downed class-3 character gets an indicator; **no repeat guard** |
| 19 | `human_being_revived_by` | gameplay | `0x00a4f800` | resolved — player reviver's name; **`"#NETID#0"` in single player**, nil for NPC revivers |
| 20 | `human_being_revived` | gameplay | `0x00a4d1c0` | resolved — bit 0 of `+0x1905` |
| 21 | `hud_x_of_y_update` | gameplay | `0x00a4e980` | resolved — **unbounded slot index → out-of-bounds write** |
| 22 | `hud_x_of_y_add` | gameplay | `0x00a4e890` | resolved — fills one of 4 "x of y" slots (bounded), optional broadcast/queue |
| 23 | `hud_pop_screen` | gameplay | `0x00a4e710` | resolved — pops a named screen only if on top; optional broadcast regardless |
| 24 | `hud_get_proto_selection` | gameplay | `0x00a4dd70` | resolved — −1 until `game_proto_select` reports, then the sticky selection |
| 25 | `homie_mission_unlock` | gameplay | `0x00a4cf70` | resolved — activity record `+0xc0` = 2 (forced available) + refresh pass; `lock` = 1 |

---

## M. Crash-shaped and logic defects (summary)

| # | where | defect | status |
|---|---|---|---|
| 1 | G.2 `0x007eb200` / `0x00a4e9bd`-`0x00a4e9c8` | `hud_x_of_y_update` writes two dwords and a byte at `0x022b6758 + 0x34·index + {0x20..0x28}` for any index — script-controlled out-of-bounds write into the next HUD tables (vtable-bearing objects) or an unmapped address (sibling `add` bounds to 0..3) | CONFIRMED |
| 2 | J.6 `0x00840fda`-`0x00840fdf` (`game_proto_select`, outside the tranche) | argument ≤ 0 / nil / non-number → byte write into the caller's saved registers (EBX for nil) | CONFIRMED structure, outcome HYPOTHESIS |
| 3 | B.4 `0x007aaa62`-`0x007aaada` (and the two sibling dialogs) | pre-game check dialogs used without a null check after `0x007c3310`; pool exhausted → writes near address 0 (`+0x128`, `+0x13c`) | CONFIRMED shape, reachability HYPOTHESIS |
| 4 | C.1 `0x00a5217c`-`0x00a521ad` | `los_check_do` returns −1 ("pending") even when the request could not be created → a polling script never finishes | CONFIRMED structure, reachability OPEN |
| 5 | C.4 `0x00a5228b`-`0x00a522da` | world manager `0x03171a64` and its object-table entries used unchecked | CONFIRMED shape, reachability HYPOTHESIS |
| 6 | D.1 `0x00a50dde`-`0x00a50dfc` | `is_syn_tower_destroyed` returns two values (true, false) when true, one value otherwise | CONFIRMED |
| 7 | F.2 | `human_being_revived_by` returns `"#NETID#0"` in single player instead of the player's script name; nil for NPC revivers | CONFIRMED structure |
| 8 | F.3 | `human_downed_set_mars_kb_interactable` has no repeat guard — a second call adds a second indicator and loses the first id | CONFIRMED structure |
| 9 | C.3 | camera-light table entries of despawned groups can never be detached; they hold one of 16 slots until the table is reset | CONFIRMED structure |
| 10 | C.2 | 17th attached light group silently ignored | CONFIRMED |
| 11 | G.3 | `hud_pop_screen` broadcasts the pop even when the local pop was refused (screen not on top) | CONFIRMED structure |
| 12 | E.3 | dual-wield bit set locally only, cannot be cleared through the binding | CONFIRMED structure |
| 13 | H.1 | `homie_mission_lock`/`unlock` cannot restore the default state 0; no replication | CONFIRMED structure |
| 14 | C.1 | unknown object name returns 0, indistinguishable from "no line of sight" | CONFIRMED |
| 15 | B.2 / tranche 16 E.3 | `0x008703f0` is a 1..8 range test: the "privilege" bindings are constant true and tranche 16's "privilege denied" branch is dead | CONFIRMED (correction) |
| 16 | B.1, B.3 | four main-menu names do nothing on PC (three stubs, one folded-zero gate) | CONFIRMED |
| 17 | B.4 | three localised sign-in strings computed and never read | CONFIRMED |
| 18 | G.1 | raw Lua string pointer stored in HUD slot `+0x4` (dangling if ever read later) | CONFIRMED store, consequence OPEN |
| 19 | F.3 `0x005e4350`/`0x005e4450` | character `+0xf0` template pointer read unchecked (same shape as tranche 18 F.3) | CONFIRMED shape |

---

## N. Clean-room check

- Addresses are plain hex throughout; no Ghidra auto-names for functions, globals or labels, no decompiler variable
  names, and no pasted pseudocode. Library routines are named only where Ghidra identified them as standard C runtime
  (`__stricmp`) or as the public Lua 5.1 API by established project convention (`lua_gettop`, `lua_tolstring`,
  `lua_pushstring`, `lua_setfield`, …); the two audio-middleware symbols Ghidra attaches to the folded "return 0" body
  (B.3) are described, not quoted. Short game strings (Lua hook names, localisation keys, `"sr3_city"`, `"tower_dmg"`,
  `"#NETID#"`, `"Default"`) are quoted as data.
- Self-check run on the finished file, as the last step before reporting, with the pattern
  `\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4}|LAB_[0-9a-fA-F]+|FUN_[0-9a-fA-F]+|DAT_[0-9a-fA-F]+)\b`
  (`grep -cP`): **0 hits**. The pattern was first checked against 15 controls: 12 positives (auto-named locals and
  pointers, parameter, register input, stack array, unaffected register, type name and the three label-prefix forms)
  all matched, and 3 negatives ("left undefined", "in ECX", a bare `0x…` address) did not.
- No spec file was edited. The private Ghidra copy `tools\gp_t19` was deleted after the dumps (verified absent); raw
  dumps stayed in the session scratchpad.
