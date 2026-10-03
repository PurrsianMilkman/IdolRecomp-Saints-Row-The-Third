# Ranking tranche 23 — 25 previously-unspecced Lua-bound names (Team A, 2026-10-03)

Job definition: `D:\Project Crreish\TEAM A\ghidra\jobs\ranking\tranche-23.json`. The job file's own title states **names
501-525 of the 554 unspecced names, Team B call-count order**; this note uses that range and exactly the 25 names the job
lists. Run locally, read-only, on a private copy of the Ghidra project (`tools\gp_t23`, deleted afterwards), with the
job's own arguments: `CrreishDump.java <out>/lua lua depth:1 maxfuncs:15 maxinsn:500 <25 names>`. **All 25 names
resolved.** Every name string occurs exactly once in `.rdata`, and for all 25 the handler is the code pointer stored in
the slot right after the name (insn offset +1) — CONFIRMED — dump `index.txt`, cross-checked by range runs on the crib
purchase registrar (new to this series) and on the crib garage, completion and cinema tables; the dialog, main-menu,
UI-helper and playlist rows match the tables earlier tranches printed (section A). No name in this tranche is registered by
a pointer-before-name one-name registrar. The two `lua`-mode "uses" that landed on the `lua_setfield` primitive
(`crib_get_cash_stash_1.txt` and `completion_call_callback_1.txt`, both rooted at `0x00dfe830`) are their registrars'
loops reading the **first** row of the table (both names are row 0 — range run), exactly as tranche 18 section A
describes — not second handlers.

Follow-up runs on the same private copy, cited by name below (exact address lists in section G):

- "range run": `range` mode on the gameplay rows around three of this tranche's names, and on four sub-registrars;
- "pointer run": `ptrs` mode on a feature-callback table, four import-table rows and three constants;
- "follow-up dump": depth-0 `func` run on 45 callees;
- "second follow-up dump": depth-0 `func` run on 9 more callees;
- "xref run": `xref` mode on six globals/functions;
- "import check": the five import-table slots used by `cinema_editor_save_clip` resolved to their import names by reading
  the executable's own PE import table (a small script of mine; result: `CloseHandle`, `CreateFileW`, `GetLastError`,
  `WriteFile`, `SetFilePointer`).

Call counts are Team B's (`D:\Crreish-sync\for-team-b\team-b\tools\lua_reconciliation_called_and_registered_1181.tsv`):
**every name in this tranche is 1 call site in 1 script.** Team B tags 11 names `ui` (both `dialog_*`, four of the five
`crib_*` — all but `crib_purchasing_unlock` —, `completion_call_callback`, `community_login_open`,
`city_load_img_load_complete`, `cinema_editor_save_clip`, `cell_playlist_save_list`) and 14 `gameplay`; section A shows
the registrars agree exactly (14 rows in the gameplay registrar, 11 under UI registrars).

**Already partly covered elsewhere (cited, re-verified, not re-derived):** `character_fake_revival_start` was read as a
sibling in tranche 13 B.5 (its null-dereference is already on record); this note re-verifies it and adds one logic point
(F.9). `continuous_explosion_follow_target_add` (`spec-lua-api-behaviour.md` §25.23) is the partner of E.1 and this
tranche's listings **correct two statements in §25.23** (H.4). The crib purchase camera fly-by `0x0057eb10` is tranche 16
B.4's routine and is cited, not re-described. The dialog pool, its free/open rings and the body-text/button pools are
`interp_tabs.md` Q7 and tranche 13 D; the cutscene state machine and its state numbering are `interp_nnlt.md` §2 and
`interp_mnao.md` §4.3.

Labels: **CONFIRMED** = read in the listing of a dump made for this note; **HIGH CONFIDENCE** = follows from a dumped call
or reference, but the callee body was not dumped or a meaning is inferred from strong usage; **HYPOTHESIS** = plausible,
not settled; **OPEN** = not settled (collected in section I).

**Shared conventions, cited not re-described:** every entry opens with the `lua_gettop` prologue (`0x00dfde50`) and
reads arguments with negative stack indices counted from the bottom. Primitives as in the tranche 09/13/16/18/20 front
matter: `0x00dfe210` `lua_tolstring` (non-string reads as null), `0x00dfe1e0` `lua_toboolean`, `0x00dfe040` `lua_type`
(0 = nil, 1 = boolean), `0x00dfe160` `lua_tonumber` (non-number reads as 0), `0x00ea2596` truncating float-to-int,
`0x00dfe590` `lua_pushboolean`, `0x00dfe3a0` `lua_pushnumber`, `0x00dfe5e0` `lua_gettable`, `0x00dfde60` `lua_settop`;
`0x0083dff0` is the table-size helper (`spec-lua-api-behaviour.md` `sizeof_table`; tranche 18 F.5: −1 for an absent or nil
slot, else the numeric field `"n"` — the key string at `0x0129d9cc` is `"n"`, pointer run — else a `lua_next` pair
count). "Nil-gated optional argument" = the tranche 13 idiom; an **unconditional** read has no presence check (missing
boolean = false, missing number = 0, missing string = null). Resolvers: the generic character resolvers `0x00a281a0` →
`0x00a280c0` / `0x00a28150` (both null-safe; `0x00a280c0` handles `"#PLAYER#"` and named objects through `0x00a27e20`,
which only accepts kind row `+0xa` bit `0x02`, then liveness), the per-kind named-object lookups on `0x02442750` of
tranche 16/18's front matter (`0x005982e0` kind row `+0xb` bit `0x02`, the teleport-destination resolver; `0x005eab60`
script groups; `0x00734e90` kind row `+0x6` bit `0x02`); handle resolution `0x00458230` on `0x024433a8` with key
`0x031d152c`; liveness `0x00853b10` (true = dead or gone); kind-flag rows `[0x02cc9900 + 4·(object byte +0x34)]`. The
**double-gate** record-and-replicate setter shape (`0x008ae480` then `0x008837a0`; owner `0x008ae3a0`; opcode `0x46`
with two debug-tag strings) and the **single-gate** shape (gate `0x008addb0(object, 0)`) are as in tranche 20's front
matter. The Lua-hook firing helpers (`0x00e0cef0` "hook exists", `0x00e0ca80` allocate a coroutine record — null only
when the thread table is full, `spec-lua-api-behaviour.md` `thread_new` —, `0x00e0cd00` dispatch, record `+0x14` = a
document handle) and the current-thread context `0x00e0ceb0` (null when the context depth `0x02a44d10` is 0 or above 16)
are the coroutine mechanism of the tranche front matter. The local-player getter is `0x009da4e0`; the session getter is
`0x0087ba20` (its `+0x5c` = `+0x58` test is `game_get_is_host`, §8.27). Money values are cents and saturate at
±2,000,000,000 (`0x00960160` set, `0x00960190` add; tranche 20 F.2).

---

## A. Registrars — where the 25 names live

Nine registrars. CONFIRMED — `index.txt`, range run, xref run:

| registrar | reached from | names in this tranche |
|---|---|---|
| gameplay `0x00a20840` | (established) | 14: `cutscene_play_do`, `cutscene_play_check_done`, `crib_purchasing_unlock`, `continuous_explosion_follow_target_remove`, `character_use_synced_finishers`, `character_take_human_shield_do`, `character_set_over_the_shoulder`, `character_prevent_kneecapping`, `character_parachute_play_action`, `character_parachute_open`, `character_is_on_fire`, `character_is_combat_ready`, `character_fake_revival_start`, `character_check_resource_loaded` |
| dialog `0x007c5630` (12 rows, tranche 13 A) | UI `0x008430f0`, call at `0x0084313e` | 2: `dialog_box_create_internal`, `dialog_pause_unpause` |
| crib purchase `0x005f1fc0` (4 rows) — **new to this series** | UI `0x008430f0`, call at `0x0084312c` (xref run) | 3: `crib_get_cash_stash`, `crib_purchase_purchase_crib`, `crib_is_stronghold` |
| crib garage `0x0080e540` (3 rows, tranche 16 A) | UI `0x008430f0`, call at `0x008431e0` | 1: `crib_is_garage_disabled` |
| completion `0x007c04b0` (14 rows, listed in `spec-lua-api-behaviour.md`'s `Completion_is_client` alignment note; loop count `0xe` — range run) | UI `0x008430f0` | 1: `completion_call_callback` |
| main menu `0x007c9fc0` (tranche 11/18) | UI `0x008430f0`, call at `0x0084315f` | 1: `community_login_open` |
| UI helper `0x00845aa0` (113 rows) | `0x008489e0` | 1: `city_load_img_load_complete` |
| cinema editor `0x00bcd240` (2 rows, tranche 13 A) | UI `0x008430f0`, call at `0x00843210` | 1: `cinema_editor_save_clip` |
| playlist `0x0083e850` (3 rows, tranche 17 A) | UI `0x008430f0`, call at `0x0084311a` | 1: `cell_playlist_save_list` |

- **Crib purchase registrar `0x005f1fc0`, full table — new to this series** (range run; name-then-function rows, loop
  count **4** at `0x005f2012`, each row `lua_pushcclosure` `0x00dfe4f0` then `lua_setfield` `0x00dfe830` into globals):
  `crib_get_cash_stash` `0x005eed30`, `building_purchase_is_crib_being_purchased` `0x005eed90`,
  `crib_purchase_purchase_crib` `0x005f1e50`, `crib_is_stronghold` `0x005ef3e0`.
- **Gameplay neighbours read for context** (range run; not in this tranche): the row before `crib_purchasing_unlock` is
  `crib_is_unlocked` → `0x00a46490`, the row after is `crib_unlock_strongold` (sic, the shipped spelling) → `0x00a46500`
  and then `crib_weapon_add_enable` → `0x00a42c20`. Around `continuous_explosion_follow_target_remove`:
  `continuous_explosion_follow_target_add` → `0x00a462c0` (§25.23), `continuous_explosion_start` → `0x00a46340`,
  `continuous_explosion_stop` → `0x00a42b30`. Around the cutscene pair: `customization_swap_player_rig` → `0x00a43cf0`,
  `cutscene_check_exiting` → `0x00a42e70`, `cutscene_was_skipped` → `0x00a42ea0` (tranche 13 C).
- The dialog, crib garage, completion, cinema and playlist tables were already printed by the tranches cited; the rows used
  here match them (`dialog_box_create_internal` `0x007c5130`, `dialog_pause_unpause` `0x007c2eb0`,
  `crib_is_garage_disabled` `0x0080e0b0`, `completion_call_callback` `0x007bfb90`, `cinema_editor_save_clip`
  `0x00bcbd70`, `cell_playlist_save_list` `0x0083e6f0`). `community_login_open` → `0x007c9d40` is the row tranche 18
  printed; `city_load_img_load_complete` → `0x00842b20` follows the row whose handler is `0x00842b00` in the 113-row table.

---

## B. Dialog boxes — 2 functions (dialog registrar `0x007c5630`)

Context (tranche 13 D, `interp_tabs.md` Q7): four dialog slots of `0x184` bytes at `0x02282d40` (+`0x184` each, the last
at `0x022831cc`), free ring `0x02282d14`, open ring `0x02282d10`; the element pool `0x02282d18` holds the body-text
blocks (`[0x02283350, 0x02285450)`), a second kind of block (`[0x02285480, 0x02287580)`) and the sixteen button blocks
(`[0x02287580, 0x02289840)`). A dialog handle is (generation byte `0x02282d1d` << 16) + slot index, stored at slot
`+0x12c`.

### B.1 `dialog_box_create_internal` → `0x007c5130`

**Arguments (CONFIRMED — listing, argument-count register tracked through every `DEC`):**

| # | read | default | goes to |
|---|---|---|---|
| 1 | string, unconditional | — | title (slot `+0x10`, via `0x007c1640`) |
| 2 | string, unconditional | — | body text (a body-text block, via `0x007c1980`) |
| 3 | string, unconditional | — | **callback name, slot `+0x140`** |
| 4 | number, unconditional (truncated) | 0 | priority, slot `+0x130` |
| 5 | boolean, unconditional | false | flag bit `0x20` |
| 6 | boolean, unconditional | false | when true, `0x007c1aa0(0)` (below) |
| 7-10 | four nil-gated strings | null | option/button labels; nulls are skipped and the rest packed in order |
| 11 | nil-gated number | **−1** | slot `+0x118` and `+0x11c` (the current-option field of tranche 13 D.1) |
| 12 | nil-gated boolean | **true** | flag bit `0x04` |
| 13 | nil-gated boolean | false | flag bit `0x08` |
| 14 | nil-gated boolean | **true** | flag bit `0x02` |
| 15 | nil-gated number | **−1** | slot `+0x128`, written unless the value is −2 |

**Return:** **1 number** = the new dialog handle (slot `+0x12c`, pushed as unsigned), or **0 values** when no slot is
free. CONFIRMED.

**Body (CONFIRMED — listing and follow-up dump):** `0x007c4b60(title, body, arg 11, callback name, labels, label count,
priority, flags, 1)`:
1. Pops the head of the free ring `0x02282d14`; none → returns null (at most four dialogs).
2. Links the slot into the open ring and **makes it the head unconditionally** (`0x007c4bd4`). The engine's own opener
   `0x007c3310` (`interp_tabs.md` Q7.2) instead inserts by descending priority; this one does not (H.1).
3. `0x007c1640(title, 0)` (follow-up dump): assigns the new handle (index from the slot address, generation byte
   incremented, never 0), copies the title into slot `+0x10` with `0x00da7930` (`strncpy` of 256 bytes then a forced
   terminator — second follow-up dump), and resets `+0x139`/`+0x13a`, `+0x114`, `+0x134` = 0, `+0x130` = −1, `+0x120` =
   0, `+0x128` = −2, `+0x180` = 1.
4. `0x007c1980(body)` (follow-up dump): takes a body-text block, links it into the slot's element ring `+0x110`; if the
   text is a known localisation tag (`0x00849fa0`, a name-hash lookup) the localised text is encoded with `0x00e22a30`
   (tranche 20 G.1), otherwise the raw bytes are copied with `0x00da7930` (`0x830` bytes); slot `+0x124` = 1. No block
   free → returns null, which this binding ignores.
5. **Copies the callback name into slot `+0x140` with an inline byte-copy loop that has no length limit**
   (`0x007c4bf9`-`0x007c4c08`).
6. Each label → `0x007c4820(label, 0, 0)` (follow-up dump): a button block (localised through `0x00849df0` or copied
   with `0x00da7930`, 256 bytes), slot `+0x0` (button count) + 1; none free → nothing.
7. `+0x114` = the document handle `+0x14` of the **current Lua-thread context** `0x00e0ceb0()`, `+0x130` = priority,
   `+0x120` = flags, `+0x13b` = 0, `+0x180` = 1, `+0x118` = `+0x11c` = arg 11.

Then the binding writes `+0x128` (arg 15) and, when arg 6 is true, calls `0x007c1aa0(0)` with `this` = the slot: it sets
flag bit `0x80`, takes a block from the second range `[0x02285480, 0x02287580)` and links it into the element ring (no
text, since the argument is 0), and sets `+0x124` = 1 (CONFIRMED — listing; what that element is, OPEN).

**What the callback name is (CONFIRMED — follow-up dump of `0x007c54d0`):** on the dialog's close/result path the
manager reads slot `+0x140`; unless it is blank (`0x00da73c0`, `spec-lua-api-behaviour.md` §27.11) and the Lua hook of
that name exists (`0x00e0cef0`), it fires the hook with record `+0x14` = slot `+0x114` (the creating script's document)
and two numbers (−1.0 and 1.0 on the branch read). So arg 3 is the Lua function the UI calls back.

**Crash 1 — unbounded copy into a 64-byte field. CONFIRMED structure (`0x007c4c00`-`0x007c4c08`).** The callback-name
field runs from slot `+0x140` to `+0x17f` (`+0x180` is the next field), so a name of **64 or more characters** writes
past it (the terminator counts). Bytes 64-67 land on `+0x180` (rewritten to 1 a moment later); a name of **68 or more
characters** runs into the **next slot's** `+0x0` (button count) and `+0x4`/`+0x8` (its free/open-ring links), or — for the last slot `0x022831cc` — into
the **first body-text pool block at `0x02283350`**, whose `+0x0` is the pointer the pool calls through
(`[block]+4`, `interp_tabs.md` Q7.3) and whose `+0x8`/`+0xc` are its pool links. Either corruption crashes later in the
dialog manager or the pool. Fix: bound the copy to 63 characters.

**Crash 2 — nil text arguments. CONFIRMED structure.** Arguments 1, 2 and 3 are read unconditionally and used without a
null check: a nil/non-string arg 3 faults in the copy loop at `0x007c4c00` (`MOV CL,[EAX]` on address 0); a nil arg 1
reaches `strncpy` from null in `0x007c1640` (`0x007c1698`); a nil arg 2 does the same in `0x007c1980` (`0x007c1a83`)
whenever a body block is free. All three happen only after a slot has been taken.

**Crash 3 — no current Lua thread. CONFIRMED shape, reachability HYPOTHESIS.** `0x00e0ceb0` returns null when no
coroutine context is active, and `0x007c4c51` reads `[null + 0x14]` — same shape as tranche 18's object-indicator init.
Normally the binding runs from a UI script thread.

**Logic notes (CONFIRMED structure):** the initial option (arg 11) is stored unbounded — the same unchecked `+0x11c`
tranche 13 D.1 flagged for `dialog_box_set_current_option`; labels beyond the free button blocks are silently dropped.

### B.2 `dialog_pause_unpause` → `0x007c2eb0`

**Arguments:** none read. **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing):** only when byte **`0x012fd45c` is 1** (file value 1) it calls `0x007c20a0(0)`, the pause
display's hide routine. `0x012fd45c` is written only by the pause-display opener `0x007c1ef0` from its first argument
(follow-up dump; `dialog_open_pause_display`, §23.17, passes 1 — so a pause display opened from script can be unpaused
from script; the values the engine's other five callers pass were not read, OPEN).

`0x007c20a0(0)` (dumped at depth 1, CONFIRMED):
1. If the deferred-show latch `0x02282d37` is set (set by `0x007c1ef0` when it is asked to show while byte `0x014ff6c6`
   is set), it is **cleared and nothing else happens** — the pending show is cancelled.
2. Otherwise it needs `0x02282d1e` (pause active) and `0x02282d38` (pause display shown). Then: `0x00707540(4)` (the
   release that pairs with the opener's `0x007074a0(4, 1)` — HIGH CONFIDENCE: pause reason 4); `0x02282d1e` is left 1
   only when `0x00867830()` and `0x00707490()` are both true, else cleared; `0x008020b0(0, 0)` (the opener calls it with
   1); it fires the Lua hook **`"dialog_pause_hide"`** with record `+0x14` = `0x02282d20` (the `dialog` document, Q7.1);
   clears `0x02282d38`; if `0x02282d34` is 1 (set by the opener when `0x0059f9f0()` returned 1) and `0x00706ab0()` is
   not 2, requests `0x0059f8c0(0, 0, 0)` (the fade-out request of `interp_mnao.md` §4.3, here with 0 ms) and clears
   `0x02282d34`; finally `0x008b7260(0)` when `0x02282d1f` is clear (the opener calls it with 1).

**Crash shape — CONFIRMED shape, reachability HYPOTHESIS (`0x007c212b`):** the hook record from `0x00e0ca80` is written
(`+0x14`) with no null check and no prior "hook exists" test — unlike the dialog manager's own close path (B.1), which
tests both. `0x00e0ca80` returns null when the thread table is full. The opener has the identical unchecked write for
`"dialog_pause_show"`. A host can mirror the manager's check.

---

## C. Cutscenes — 2 functions (gameplay registrar)

State numbering, the manager `0x0153b528` and the state machine `0x0072d660` are `interp_nnlt.md` §2; `0x00725df0` is the
cutscene start (`interp_mnao.md` §4.3).

### C.1 `cutscene_play_do` → `0x00a46bb0`

**Arguments (CONFIRMED — listing):**
1. string, unconditional — the cutscene name (hashed with `0x00d9e8b0`; null hashes to 0 and finds no entry).
2. either a **table of up to 8 script-group names** — `t[1]` … `t[8]` are read as strings (nil or non-string entries
   allowed), each resolved with `0x005eab60` and kept only if alive; unresolved entries become the null handle pair at
   `0x0117f8e0` (file value 0/0) — or a **boolean**, which is read and **discarded** (`0x00a46c04`; the result register is
   never stored). Nil/absent → eight null handles.
3. a table of **exactly two** destination names: the count from `0x0083dff0` must equal 2 (`0x00a46cf6`); `t[1]` and
   `t[2]` are read as strings either way.
4. nil-gated boolean, default **true**.
5. nil-gated boolean, default **true**.

**Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing, follow-up dumps):** `0x00728870(name, 0, arg 4, &handles, arg 5)` hashes the name and calls
`0x00725df0(hash, 0, arg 4, &handles, arg 5)`; its result is ignored. Then, only when arg 3 had exactly two entries:
`t[1]` resolved through `0x005982e0` (alive) gives a position (`+0x40`) and a 3-row orientation (`+0x4c`, copied with the
fourth column zeroed by `0x004add90`) passed to **`0x00723ea0`**; `t[2]` likewise to **`0x007235c0`**.

`0x00725df0` (follow-up dump; adds to `interp_mnao.md` §4.3):
- **Ignored entirely** (returns 0) when a cutscene is already active (state > 1 **and** a manager exists), when byte
  **`0x0153b556`** is set (only writer: the skip/play command `0x0072df50` — `interp_mnao.md`'s `skip_all_cutscenes`), or
  when the hash names no entry (`0x00721be0`). Two more refusals — `0x006cec90()` true, or a timer on the local player —
  apply only when a session object exists and reports host; without a session they are not evaluated.
- A **kind-1 (zscene) entry whose scene is not yet loaded** (`0x007203e0` false): state := 2, current scene torn down,
  prep gate `0x007232e0`, and the arguments are parked for the state machine — arg 2 (always 0 here) in `0x0153b580`,
  arg 4 in `0x0153b581`, **the eight handle pairs copied (64 bytes) into `0x0153b588`-`0x0153b5c7`**, both teleport
  slots reset — and it returns 1 **without allocating a manager**. The state-2 case of `0x0072d660` later re-calls it
  (`interp_nnlt.md` §2.1).
- Otherwise: arg 4 → `0x0153b4d0`; a manager of `0x45a0` bytes is allocated (`0x00dad360`) and constructed
  (`0x00724be0`); **the eight handle pairs are copied to manager `+0x36a8`**; `0x0153b524` = arg 5 << 7 (the flag byte
  whose bit 6 is tranche 13 C.1's "skipped" bit); many subsystem calls; state := 1 or 3.

The two destination loaders (follow-up dump, CONFIRMED):

| | `0x00723ea0` (`t[1]`) | `0x007235c0` (`t[2]`) |
|---|---|---|
| `0x0153b556` = 1 (cutscenes skipped) | teleports the **local player** at once (`0x009e2220`) | sends opcode `0x47` sub-type 5 with the position and orientation to the session (`0x007229b0`, register-passed arguments) — the co-op partner's teleport |
| state = 2 (scene loading) | parks the destination in `0x0153b5d0`…, flag `0x0153b5c8` = 1 | parks it in `0x0153b620`…, flag `0x0153b610` = 1 |
| a manager exists | writes manager `+0x35f0`…`+0x362c` | manager `+0x3630` = 1, writes `+0x3640`…`+0x367c` |
| none of these | **dropped** | **dropped** |

HIGH CONFIDENCE: `t[1]` is the local player's end-of-cutscene destination and `t[2]` the co-op partner's. By
`interp_nnlt.md` §2.1, arg 4 feeds the end-of-cutscene **fade-in** gate (`0x0153b4d0` → `0x0153b6b5`, state `0x10` →
`0x13`) and bit 7 of `0x0153b524` (arg 5) chooses whether the spawning/audio restore runs in state `0x11` or `0x12` —
HIGH CONFIDENCE for both readings; the purpose of the eight group handles is OPEN.

**Logic defects (CONFIRMED structure):**
1. **Boolean second argument → uninitialised handles.** On the boolean branch the 8-entry handle array (a stack local at
   `[ESP+0x30]`) is never written, yet `0x00725df0` copies all 64 bytes of it into the parked block or the manager. The
   cutscene then holds eight stack-garbage "group handles". Handle resolution normally rejects garbage (HYPOTHESIS for the
   consequence), but the input is undefined. Fix: treat a boolean arg 2 like nil.
2. **A destination table with one (or three) entries is silently ignored** — both teleports need the count to be exactly
   2. A single-player script that passes only its own destination gets no teleport.
3. **The destinations are applied even when the start was refused.** When another cutscene is already running,
   `0x00725df0` returns at once but the loaders still see "a manager exists" and **overwrite the running cutscene's end
   destinations** (manager `+0x35f0`/`+0x3640`).

**Crash shape — CONFIRMED structure, reachability HYPOTHESIS (inside `0x00725df0`):** when the `0x45a0`-byte allocation
fails the manager pointer is set to 0, but the handle copy right after writes through it (`manager + 0x36a8`), followed
by further unguarded writes (`+0x21c0`, `+0x35da`). Latent: a name string from `0x006e1d70()` is copied into manager
`+0x4550` by an unbounded loop with 80 bytes left before the end of the allocation (source not traced; OPEN whether it can
be that long).

### C.2 `cutscene_play_check_done` → `0x00a46690`

**Arguments:** none read. **Return:** exactly 1 boolean. CONFIRMED — listing.

**Body (CONFIRMED — listing):** pushes **false** if any of: `0x00720640()` (state ≥ 2 **and** manager `0x0153b528`
non-null), `0x00720610()` (state = 1), `0x007207a0()` (state ≥ 13, the stop/teardown states that
`cutscene_check_exiting`, tranche 13 C.2, reports); otherwise **true**.

So "done" is true in state 0, and also in **states 2-12 whenever no manager exists**.

**Logic defect — CONFIRMED structure:** for a kind-1 (zscene) cutscene whose scene is not loaded yet, `cutscene_play_do`
leaves state 2 **with no manager** (C.1; the previous manager was cleared in state `0x13`, `interp_nnlt.md` §2.1), so
`cutscene_play_check_done` reports **true while the scene is still loading** — until the state machine re-enters
`0x00725df0` and allocates the manager. A script polling "play, then wait until done" can run straight past a cutscene
that has not started yet. Whether the shipped Lua wrapper waits a frame or polls anything else first is OPEN.

---

## D. Cribs — 5 functions

Context: the world's crib list is `0x03171a64` `+0x1c0` (16-bit indices into the object table `+0x58`, count `+0x1c8`).
A crib record carries an upgrade level `+0x140`, `+0x144`, two three-entry hash sets `+0x148` and `+0x154` compared
against the shared "none" hash value `0x029c9964` (the generic default noted in `spec-lua-api-behaviour.md`'s
cutscene-table constructor entry), a price `+0xe0`, two named-point handles `+0xf0/+0xf4` and `+0x108/+0x10c`, a linked
object handle `+0xd8/+0xdc`, and a flag byte `+0x7a` whose bit `0x02` is cleared by the host purchase path (D.2) —
HYPOTHESIS: "not yet owned". The level/hash-set layout is the crib upgrade routine `0x005f1750` that `interp_tabs.md`
already describes (three hashes per level, achievement `0x2c` on reaching level 3); this tranche re-read it in the
follow-up dump.

### D.1 `crib_purchasing_unlock` → `0x00a42c00` (gameplay registrar)

**Arguments:** none read. **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing, follow-up dump):** `0x005f13d0(1)`:
- writes the byte **`0x014a0f88`** = 1. That byte is the synced variable **`"cribs"`** registered (1 byte, owner flag =
  "session exists and reports host") by `0x005f1a10`, with a change callback at `0x005f1a00` that re-runs `0x005f13d0`
  with the received value — so the unlock is replayed on a peer when the variable arrives (same synced-variable family as
  tranche 07 C.4's `"stag_active"`). `0x005f1a10` also registers `"crib access"` (`0x014a0f8c`, 4 bytes) and
  `"crib interface enabled"` (`0x014a0f89`), zeroes the owned-crib counter `0x014a0f80`, and resets `0x012ecb38` = 1.0.
- walks every crib of the world list; for each with `+0x7a` bit `0x02` set whose linked object (`+0xd8/+0xdc`, resolved
  by `0x005f0de0`: kind row `+0x7` bit `0x02`, alive) exists, calls `0x0093d320(3)` on that object: sets its byte `+0xe4`
  bit `0x02` and — when bit `0x01` is still clear and the object's `+0x7b` bit `0x02` is clear — bit `0x01`, re-arms its
  timer and runs its activation helpers (`0x0093b7a0(3)`, and `0x0093b510`/`0x0093d0e0` for animated objects).
  `0x005f13d0(0)` does the reverse through `0x0093bde0(3)`.

HIGH CONFIDENCE: the linked object is each unowned crib's purchase trigger, and this binding turns all of them on (the
name says "unlock purchasing"). No lock binding is in this tranche; the engine's other caller of `0x005f13d0` is
`0x00b9aaa0` (not dumped). No crash shape (each linked object is resolved and liveness-checked; the world list entries
are used as stored).

### D.2 `crib_purchase_purchase_crib` → `0x005f1e50` (crib purchase registrar)

**Arguments:** none read. **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing):** the crib is the handle in **`0x014a0f90/0x014a0f94`** (the one
`building_purchase_is_crib_being_purchased` `0x005eed90` tests; written by `0x005f0770`, `0x005f0a60` and code at
`0x00a00663`), resolved through `0x00458230` and accepted only with kind row `+0x9` bit `0x20` and alive.
1. If the local player's cash (`+0x1ca0`) ≥ the crib's price (`+0xe0`): the new cash, cash − price saturated, is applied
   through `0x0094a800` (dumped at depth 1) — the **double-gate** setter shape: locally it writes player `+0x1ca0`;
   otherwise, with an owner, it sends opcode `0x46` tagged `"human"` / `"cash"`.
2. `0x005f1cc0` with `this` = the crib (dumped at depth 1; the same routine `crib_unlock_strongold` `0x00a46500` calls):
   - **no session, or a session that is not host** (`+0x5c` ≠ `+0x58`): `0x00b98d00(&crib handle, 1, crib +0x154 ≠ none)`
     (follow-up dump: updates every matching entry of the table at `0x0290f188`, count `0x0290f180`, stride `0x18` —
     clears bit 0 of `+0x10`, stores the two values; HYPOTHESIS: the crib's map/marker entry) and `0x005eea30(0)` (for
     every entry of the crib's `+0x13c` list equal to level 0, `0x0071ccf0` on the matching `+0x138` slot);
   - **session host**: only if `+0x7a` bit `0x02` is set: an opcode-`0x45` record (sub-type 2, the crib) broadcast to the
     session, `0x007161a0(7…10, 0)`, `0x00d2f540()`, then clears `+0x7a` bit `0x02`, runs `0x005f1690` and increments the
     owned-crib counter `0x014a0f80`, and calls the upgrade routine `0x005f1750(1)` (level 1, `interp_tabs.md`);
   - both branches except "host with the bit already clear" then run `0x00bd2950(crib)` and `0x005e8fb0(&handle, 0)`.
3. Whatever the branch, the camera fly-by `0x0057eb10` (tranche 16 B.4: camera mode `0xf`, sound
   `"SYS_MENU_MAP_HOVER"`, an 8,000-unit timer `0x012e59d4`) runs from the crib's named point `+0xf0/+0xf4` to
   `+0x108/+0x10c` when both resolve (kind row `+0xb` bit `0x02`, alive); otherwise no fly-by.

Insufficient cash → nothing at all (no message, no return value).

**Crash 1 — CONFIRMED (`0x005f1edc`):** when the selected handle is zero, stale, not of the crib kind, or dead, the crib
pointer is set to 0 (`0x005f1ed4`) and the very next instruction compares the cash with **`[0 + 0xe0]`** — a null read.
Any call outside the purchase screen's own flow (no crib selected) crashes. Fix: return when the crib does not resolve.

**Crash 2 — CONFIRMED shape, reachability OPEN (`0x005f1ed6`):** the local player from `0x009da4e0` is used
(`[player + 0x1ca0]`) without a null check.

**Logic defects (CONFIRMED structure):**
- **No ownership check before charging.** The price is deducted in step 1 before `0x005f1cc0` looks at `+0x7a`; on the
  host branch a crib whose bit `0x02` is already clear gets **no further effect** — the player pays for nothing.
- The two branches of `0x005f1cc0` are not equivalent: only the host branch clears `+0x7a` bit `0x02`, counts the crib in
  `0x014a0f80` and sets level 1. Where the other branch's ownership change happens (if anywhere — other writers of
  `0x014a0f80` exist at `0x005ef810`, `0x005f19c0`, `0x005f1b00`, `0x005f1c80`) is OPEN; no inference about single
  player is drawn here (H.10).

### D.3 `crib_is_stronghold` → `0x005ef3e0` (crib purchase registrar)

**Arguments:** none read. **Return:** exactly 1 boolean. CONFIRMED — listing.

**Body:** false unless the local player exists and its pointer **`+0x209c`** is non-null; then true iff **any** of the
three dwords at `[+0x209c] + 0x148`, `+0x14c`, `+0x150` differs from `0x029c9964`. CONFIRMED.

`+0x148`…`+0x150` is the crib record's first hash set (one entry per upgrade level, D intro), so `+0x209c` points to a
crib record and "stronghold" means "this crib has upgrade levels defined" (HIGH CONFIDENCE). `spec-lua-api-behaviour.md`
§23.19 calls player `+0x209c` "the garage-vehicle-list pointer"; the same pointer is the `this` of the garage-list search
there, which is consistent with it being the player's current crib record (refinement, not a contradiction — H.5). Safe.

### D.4 `crib_is_garage_disabled` → `0x0080e0b0` (crib garage registrar)

**Arguments:** none read. **Return:** exactly 1 boolean = `0x00a00eb0(5)`. CONFIRMED — listing.

`0x00a00eb0(id)` (dumped at depth 1, CONFIRMED) — a feature-disable test:
1. true if bit `id` of the flag word **`0x0268d2d0`** is set (bits set by `0x00a007d0`, cleared by `0x00a007f0`; the word
   is reset by `0x00a00810`/`0x00a00820`/`0x00a036b0` — the same word whose bits 0/2/3/4 other entries of
   `spec-lua-api-behaviour.md` touch);
2. for id 5 only: true if the handle pair **`0x0268d310/0x0268d314`** is non-zero — written by `0x00a00ef0` (follow-up
   dump) only on the session-host branch, when the local player's `+0x2098` object is in mode 4 or 2, and cleared by
   `0x00a00f90`; HYPOTHESIS: "the host's player is currently inside a garage store";
3. otherwise the per-id callback in the table at `0x0130b8c4` (pointer run: entry 5 = **`0x006d9e10`**), else false.

`0x006d9e10` (second follow-up dump): **no local player → true (disabled)**; player in a vehicle (`0x009b9160`) →
`0x006d9cb0` (the 12-mission list of tranche 20 H.6); on foot → `0x006d9d30` (tranche 16 B.2's 13-mission list, which
adds `"m04"`). So the garage is reported disabled during those missions, with `"m04"` blocking only the on-foot case.
Safe.

### D.5 `crib_get_cash_stash` → `0x005eed30` (crib purchase registrar)

**Arguments:** none read. **Return:** **0 values** — despite the name, nothing is pushed. CONFIRMED — listing.

**Body (CONFIRMED — listing, follow-up dump):** `0x0094d920(&0x014a0fe4, 4)` with `this` = the local player — the
cash-award routine of `spec-lua-api-behaviour.md`'s cash-award entries (single gate `0x008addb0(player, 0)`; locally it
adds the amount to `+0x1ca0` through `0x0094a800` and, for the local player, posts the HUD notice; otherwise it sends
opcode `0x46` `"human"` / `"cash_adjust"`), with the **stash** `0x014a0fe4` (cents) as the amount and reason 4. Then the
stash is set to round(0 × 100.0) = **0** (the constant at `0x012a2dd8` is 100.0, pointer run; the multiplicand is a
literal zero register) through `0x00960160`.

So the binding **collects** the stash into the player's cash and empties it. The stash is emptied even when the award was
only forwarded to the owner (CONFIRMED structure). **Crash shape — CONFIRMED shape, reachability OPEN:** the player from
`0x009da4e0` is passed as `this` without a null check, and `0x0094d920`'s local branch reads `+0x1ca0` through it.

---

## E. World, UI and front-end — 6 functions

### E.1 `continuous_explosion_follow_target_remove` → `0x00a46300` (gameplay registrar)

**Arguments:** 1 string = object name (unconditional). **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing):** `0x00a45800(name)` (dumped at depth 1): null name → null; else `0x00734e90` (kind row
`+0x6` bit `0x02`) **and the liveness check `0x00853b10`** (`0x00a4581c`); when the object's kind row `+0xa` has bit
`0x08` and its virtual `+0x70` returns non-null, the result of that virtual call is returned instead (HYPOTHESIS: the
object a character is riding or attached to). If an object results, its **handle** `+0x8/+0xc` goes to `0x005eb420`.

`0x005eb420(handle)` (dumped at depth 1): only when the handle is non-zero **and** the effect-active byte `0x012ec964` is
set: a linear search of the follow-target list — handle pairs from `0x012ec8f8`, count `0x012ec8f4` — and the first match
is removed by moving the last entry into its place and decrementing the count (the indexing is correct). Not found → no
change.

The partner (second follow-up dump of `0x00a462c0` and follow-up dump of `0x005eb3e0`): `..._add` appends the same handle
**only while the effect is active and the count is below 2**, with no duplicate check. So the list holds **at most two**
targets; a third add is silently dropped, and adding the same target twice and removing it once leaves one copy. The stop
binding `continuous_explosion_stop` (`0x00a42b30` → `0x005eae60`) clears only the active byte; with it clear, removes do
nothing, and the entries stay until the start routine `0x005eb6c0` rewrites the count (`0x012ec8f4` writers: add, remove,
`0x005eb690`, `0x005eb6c0` — xref run). No crash shape.

### E.2 `completion_call_callback` → `0x007bfb90` (completion registrar)

**Arguments:** 1 number (unconditional, truncated; missing = **0**). **Return:** 0 values. CONFIRMED — listing.

**Body:** stores the number in **`0x012fd1b8`** (file value −1). CONFIRMED.

**Consumer (follow-up dump of `0x007bda70`, its only reader; called from `0x007bf750`):** when the completion screen
finishes it clears four screen globals, calls `0x00d218f0`, then switches on `0x012fd1b8`:

| value | action |
|---|---|
| 0 | callback A(1, 0), then the fade-out request `0x0059f8c0(0, 0, 1)` |
| 1 | callback A(1, 1) |
| 2 | callback A(0, 0), then callback B(0) |
| 3 | callback B(1) |
| anything else | nothing |

A = the function pointer at `0x02282514`, B = `0x02282518`; each only if non-null. Afterwards callback C at `0x02282510`
runs if set, and C, A and a flag byte are cleared. Writers of `0x012fd1b8` (xref in the dump): this binding, the screen
set-up `0x007bfae0` (−1) and the mission-success set-up at `0x007c070a` (3, beside the `"cmp_mission_success"` element
lookup — range run). Who installs A/B/C is OPEN (HYPOTHESIS: the mission-completion flow, with A taking
"continue/retry"-style flags). **Note (CONFIRMED structure):** a call with no argument selects case **0**, not "no
callback". No crash shape (switch, null-checked pointers).

### E.3 `community_login_open` → `0x007c9d40` (main-menu registrar)

**Arguments:** none read. **Return:** 0 values. CONFIRMED — listing.

**Body:** `0x007aa650()` (dumped at depth 1; also called from `0x0080fb40` and `0x007b0090`), CONFIRMED — listing:
1. Resets the community-login state (a `0x38`-byte block at `0x022420e0` and a dozen fields/bytes around it),
   `0x00bd5cc0(0)`; when `0x00706ab0()` is 2, `0x007ca730(1)` on `0x02296bf0`.
2. If both stored account strings are non-empty (`0x00bd5d00`: case-insensitive wide compares of `0x029494c8` and
   `0x02949554` against `""` — follow-up dump) **and** the login state `0x02949584` is 3 or in progress (neither 0 nor 3,
   `0x00bd5bf0`/`0x00bd5c00`), it starts the request `0x00bcf040(0x007aa5d0, 1)` and returns. `0x00bcf040` (follow-up
   dump): if no request is pending (`0x02946d8c`) and the state `0x02946d10` is not 2 and `0x00870540()` is true, it opens
   a `"COMMUNITY_LOADING"` / `"MAINMENU_COMMUNITY"` notice dialog (`0x007c3e60`), stores the completion callback, arms a
   1,000-unit timer and remembers the dialog handle; otherwise it calls the callback at once with (0, −1).
3. Otherwise it opens an information dialog (`0x007c3310` with title `"COMMUNITY_BLURB_HEADER"`, priority 1, flags
   `0x800`), body `"COMMUNITY_SITE_BLURB"`, one `"CONTROL_CONTINUE"` button (stored in `0x022420f0[]`), and result
   callback `0x007aa5f0` (follow-up dump): any result other than −1 closes the blurb, releases its body node and
   **tail-jumps to `0x00bcf040(0x007aa5d0, 1)`** — the same request as step 2. No free dialog slot → nothing.

So: "show the community blurb (first time / not signed in), then start the community login". Correction for
`interp_tabs.md` Q7.3 (CONFIRMED — follow-up dump of `0x007c2ac0`): the button helper **returns** the new button block, or
0 when none is free (`0x007c2b5e`); this binding stores that value. No crash shape. A second call while the blurb is open
opens a second blurb and overwrites `0x022420e0`/`0x022420e8` (CONFIRMED structure; consequence HYPOTHESIS: the first
blurb's callback then closes the second one's handle).

### E.4 `city_load_img_load_complete` → `0x00842b20` (UI helper registrar)

**Arguments:** none read. **Return:** 0 values. **Body:** `0x007acd30()`, a one-line setter: byte **`0x0224253a`** = 1
(the write target is the global, not `0x007acd30`). CONFIRMED — listing.

The byte has three users (dump): this setter, the reset `0x007acc10` (follow-up dump: releases the current load-screen
document `0x02242534`, clears `0x02242534`, `0x02242539` and `0x0224253a`, and reloads the `"city_load"` document into
`0x012fca0c`; called from undefined code at `0x007ace9a`), and a reader at `0x007ad28a` in undefined code (range run):
after a time test (`0x00dad810` result must exceed 10,000) and with `0x02242539` clear and the image document
`0x02242534` loaded (`0x007b14f0(0x012fca0c)`), **only a set `0x0224253a` lets the code start the `"city_load_thread"`
worker** (`0x007acfe0`, stack `0x40000`, via `0x00db6920`). HIGH CONFIDENCE: the load-screen script calls this when its
background image has finished loading, and the city load waits for it. **Stall hazard (CONFIRMED structure within the
range read; OPEN whether another path starts the thread):** if the script never calls it, that path never starts the load
thread. No crash shape.

### E.5 `cinema_editor_save_clip` → `0x00bcbd70` (cinema editor registrar)

**Arguments:** 1 string (unconditional). **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing, follow-up dumps, import check):** null string → nothing. Else the string's name hash
(`0x00d9e8b0`) is looked up by `0x008436c0` in a 12-byte-record table (`0x02317d74`, key at `+0`, value at `+4`, count
`0x02317b64`; the "none" hash is rejected); not found → nothing. The value found goes to `0x00bc6540`:
1. `0x00bc5b70(path, 0x104, value)` builds **`<Documents>\My Games\…\<value>.srtt_clip`** (`OpenProcessToken`,
   `SHGetFolderPathW` with folder id `0x8005`, the `"My Games"` part table at `0x01311db0` joined by `0x00bc5a90`, then
   `swprintf_s` with `L"%s\\%s.%s"` and extension `L"srtt_clip"`).
2. `CreateFileW(path, read|write, 0, 0, CREATE_ALWAYS, normal, 0)` — an existing clip is overwritten.
3. Writes a 12-byte header {`0x564d4143` (the bytes `"CAMV"`), 1, 0}; then `0x00bcc010` writes an 8-byte settings block
   (two bytes from `0x02946b6c`/`0x02946b70` and one byte packing eight flag bytes) plus the zone data (`0x00bc7e80`) and
   returns that byte count; then the clip buffer `[0x029443d8]` of `[0x029443dc]` bytes; on a full write it seeks to 0
   (`SetFilePointer`), rewrites the header with the count in its third dword, and calls `0x00d48db0` (a two-instruction
   `return 1` stub); `CloseHandle` on every path.

HIGH CONFIDENCE: the argument names a UI string variable whose value is the clip's file name. **Latent defect —
CONFIRMED structure, reachability HYPOTHESIS:** `0x00bc6540`'s 0x208-byte path buffer is never initialised, and
`0x00bc5b70` returns without writing it when `OpenProcessToken`, `SHGetFolderPathW` or the path join fails — the
`CreateFileW` that follows then reads an uninitialised stack buffer as a wide path (possible over-read until a zero
word). A short write after the header leaves a file whose header says size 0 (CONFIRMED structure). The cinema editor is
a developer tool (HYPOTHESIS: unreachable in normal play).

### E.6 `cell_playlist_save_list` → `0x0083e6f0` (playlist registrar)

**Arguments:** 1 table of track numbers; 2 nil-gated number = start position, default −1. **Return:** 0 values.
CONFIRMED — listing.

**Body (CONFIRMED — listing):**
1. count = `0x0083dff0(arg 1)`; if count > 512 it is cut to 512; then **only the low 16 bits are kept** (`MOVZX` at
   `0x0083e739`).
2. For i = 1 … count: `t[i]` read with `lua_tonumber` (non-number → 0), truncated, and its **low 16 bits** stored into a
   512-entry 16-bit stack array (`[ESP+0x38]`, 1,024 bytes, ending 4 bytes below the frame's security cookie).
3. count 0 → `0x0055e380(0, 0)` (clear the list). Otherwise `0x0055e380(count, array)`, then the start position is
   replaced by 0 unless 0 ≤ position ≤ count, and `0x00560930(position)`.

`0x0055e380(count, ids)` (dumped at depth 1): refuses counts above 512; clears the old entries' back-indices; for each
id: copies it (sign-extended 16-bit) into the playlist of station 0 of the radio station array `0x013c58cc` (`+0x1a4`,
512 words; count `+0x5a4`), **writes its position as a 16-bit value at `0x013bbc24 + 20·id` and reads the dword at
`0x013bbc18 + 20·id`** for `0x00bda2c0`; finally sets the count, `0x013c60e0` = count, and resets the current index.

`0x00560930(position)` (dumped at depth 1): only when position < `0x013c60e0`; takes the local player, its vehicle handle
`+0x16c0/+0x16c4` (vehicle kind, alive), asks `0x009b95c0(player, vehicle)`, and if the vehicle's radio flag
(`[+0xbf4] + 0x870` bit `0x4000000`) is set, selects that playlist position on station number 1 — array entry 0, the
station whose list was just written (`0x0055c8d0(1, position)`, stride `0x690`, bounded by the station count
`0x013c58dc` and the list count) and retunes the vehicle's radio (`0x0055ec10` /
`0x005602e0`, or `0x00560640` with the vehicle position).

**Crash 1 — unchecked track numbers, an out-of-range write. CONFIRMED structure (`0x0055e3ef`, read at `0x0055e3fd`).**
The ids come straight from the script and are never compared with the song count `0x013c045c` (tranche 17 D.1). Any
number outside 0 … count−1 makes `0x0055e380` write a 16-bit value at `0x013bbc24 + 20·id` — anywhere within about
±640 KB of the song table for ids −32,768 … 32,767 — and read a pointer-sized value from `0x013bbc18 + 20·id` for
`0x00bda2c0`. Fix: drop ids outside the song table.

**Crash 2 — negative `n` → stack overflow. CONFIRMED structure (`0x0083e739`, `0x0083e7a6`).** A table whose numeric
field `n` is negative makes `0x0083dff0` return a negative count; it passes the "> 512" test, and its low 16 bits become a
large positive count (n = −1 → 65,535). The loop then writes that many 16-bit entries into the 512-entry stack array —
over the security cookie, saved registers and return address and on up the stack (a hard crash). A missing or nil arg 1
also yields 65,535, but there the first `lua_gettable` on nil raises a Lua error before any write (CONFIRMED by the read
order; HIGH CONFIDENCE for the VM's raise). Fix: treat any count < 0 as 0 before narrowing.

**Crash 3 — CONFIRMED shape, reachability OPEN:** `0x0055e380` reads the station array `0x013c58cc` (`+0x5a4`) without a
null check (radio tables not loaded — same family as tranche 17 D.1); `0x00560930` reads `[player + 0x16c0]` without
checking the local player; and when the player's vehicle handle is stale but its seat state `+0x16d4` is 3,
`0x009b95c0(player, 0)` reports true (follow-up dump: both lookups fail to the same 0), so `0x00560930` reads
`[0 + 0xbf4]`.

**Logic note:** the start position check accepts position == count; harmless, because `0x00560930` rejects it
(CONFIRMED).

---

## F. Characters — 10 functions (gameplay registrar)

All ten take the character name as argument 1 (unconditional string). `0x00a281a0` is used by all but
`character_set_over_the_shoulder` (which calls `0x00a28150` directly); both are null-safe and only return live humans.

### F.1 `character_use_synced_finishers` → `0x00a428e0`

**Arguments:** 1 character; 2 boolean, **unconditional** (missing = false). **Return:** 0 values. CONFIRMED — listing.

**Body:** if the character resolves, `0x00949c80(flag)` with `this` = the character — the **double-gate** setter: locally
it writes **bit 15 (`0x8000`) of the dword at character `+0x1c9c`** (`0x00949da5`-`0x00949dbc`); otherwise, with an owner,
it sends opcode `0x46` tagged `"human"` / `"human_force_flagsuse_synced_finishers"`. CONFIRMED. The tag's other user,
`0x008ae650`, also references the two tags of F.2/F.3 and calls their setters (HIGH CONFIDENCE: the receive side of the
`"human_force_flags…"` family; tranche 13 B.3's `cannot_exit_rc_vehicles` bit 9 lives in the same `+0x1c9c` dword).
HYPOTHESIS: the character uses paired ("synced") finisher animations. No crash shape.

### F.2 `character_prevent_kneecapping` → `0x00a41f20`

**Arguments:** as F.1. **Return:** 0 values. **Body:** `0x00946750(flag)` — double-gate setter on **bit 3 (`0x08`) of the
dword at character `+0x1c98`**; remote tag `"human"` / `"human_force_flagsimmune_to_knee_capping"`. CONFIRMED — listing
(`0x00946875`-`0x0094688c`). No crash shape. Note: a call with the boolean omitted **clears** the immunity.

### F.3 `character_set_over_the_shoulder` → `0x00a426d0`

**Arguments:** 1 character (`0x00a28150`); 2 nil-gated boolean, default **true**. **Return:** 0 values. CONFIRMED —
listing.

**Body (CONFIRMED — listing, follow-up dump):** `0x009ab200(character, flag, 1)`: the third argument 1 skips its
"local or owned" test (gate `0x008addb0` / `0x008ade70`), so it always continues; if bit 22 of `+0x1c98` already equals the
flag, nothing. Otherwise `0x00947890(flag)` — double-gate setter on **bit 22 (`0x400000`) of character `+0x1c98`**, remote
tag `"human"` / `"human_force_flagsis_over_the_shoulder"` — and then, when `0x009aa360(character, 0)` is true,
`0x00964b00(0x009aa130(character), word 0x0262d316, character, −1, 0, 0, 1)` (HYPOTHESIS: refresh the aiming/camera
state). Note (CONFIRMED structure): the refresh runs even on the branch where the setter only forwarded the change to the
owner and the local bit is still the old value. No crash shape.

### F.4 `character_is_on_fire` → `0x00a417c0`

**Arguments:** 1 character. **Return:** exactly 1 boolean = **bit 8 (`0x100`) of the dword at character `+0xe8`**;
**false** when the name does not resolve. CONFIRMED — listing. Safe.

### F.5 `character_is_combat_ready` → `0x00a415a0`

**Arguments:** 1 character. **Return:** **1 boolean, or 0 values when the name does not resolve** (`MOV EAX,EDI` with
EDI = 0 at `0x00a415df`). CONFIRMED — listing.

**Body (dumped at depth 1, CONFIRMED structure; meanings HIGH CONFIDENCE at best):** `0x00943a20(character)`: false when
dead (`0x0096f4f0`); reads the animation player `[+0xd24] + 0x10` (no null check of `+0xd24`) for `0x004b2190`, and when
`0x009b9180(character)` holds, a result of 0 or 12 gives false; the equipped item (`0x005fe3e0` on `+0x1b78`, its
`+0x19c` record) with record flag `+0x10` bit `0x20` gives false; for the player (`0x0091f5e0`) an item of type 7 gives
false unless `+0xccc` is clear and `0x0130ef98` is set; `+0x2b8` bit `0x10` or `+0x2bb` bit `0x80` gives false; otherwise
true when `+0x1c98` bit 0 is set or `0x009ba300(character)` holds, else true only when the timer at `+0x1390` is not
running (`0x00d9e1e0`). Return-shape note: an unknown name gives **no value** (nil to the script), not false.

### F.6 `character_check_resource_loaded` → `0x00a46af0`

**Arguments:** 1 name (unconditional). **Return:** exactly 1 boolean. CONFIRMED — listing.

**Body (CONFIRMED — listing, follow-up dumps):**
- If `0x00a280c0` resolves it (a live human by name, or `"#PLAYER#"` = the local player): `0x0092aa20` returns
  `0x00dafb60([character + 0x7c]) == 3`. `0x00dafb60` (follow-up dump) maps a resource record's 16-bit flags `+0x1e` to a
  state: null record → 1; bit `0x40` → 5; bit `0x10` → 4 (bit `0x20` clear) or 2; bit `0x04` → 0; bit `0x01` → **3**;
  else 2 (bit `0x02`) or 1. So "loaded" = the character's resource record has bit `0x01` and none of `0x40`/`0x10`/`0x04`
  (HIGH CONFIDENCE for "resource state"; a null record reads as not loaded).
- Otherwise it looks the name up with `0x005e4dd0` (kind row `+0xb` bit `0x04`, alive) and returns **`+0x90` bit 0 set and
  bit 1 clear** — the same two bits tranche 20 D reads on script-group members (bit 0 = spawned, bit 1 = deferred spawn
  retry armed; HIGH CONFIDENCE), i.e. "this group member has spawned and is not waiting for a retry".
- Neither → false. Safe.

### F.7 `character_take_human_shield_do` → `0x00a46740`

**Arguments:** 1 taker; 2 victim (both unconditional, `0x00a281a0`); 3 nil-gated boolean, default **false**. **Return:**
0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing):** nothing unless both resolve. Then:
- **arg 3 true:** `0x009acb20(taker, victim)` (dumped at depth 1; read for its main steps, HIGH CONFIDENCE for the
  reading): pairs the two (each one's `+0x1890/+0x1894` = the other's handle), marks the victim (`+0x18dc` bit 0), sets
  both shield states `+0x18d4` = 2, and either starts the grab (timers at `+0x18a0`/`+0x18a4`, the second randomised
  2,000-3,000 through the PRNG `0x00dab660`, state `+0x18d0` = `0xa9`, `0x0095f8d0`, …) or, when `0x009ac750` refuses,
  undoes the pairing; for a remote victim it first requests it (`0x008ade30`, `0x008addd0` with completion `0x009ac7f0`).
  Then taker `+0x458/+0x45c` = the victim's handle and `0x004dd380(taker, 0x15, 0)`: the taker's action byte `+0x501`
  becomes **`0x15`**, with the old state's exit handler and the new one's enter handler from the 36-byte table at
  `0x012e153c`/`0x012e1540` (CONFIRMED — listing of `0x004dd380`).
- **arg 3 false:** taker `+0x458/+0x45c` = the victim's handle; `0x004f4d00(taker, 0x2e, 0)` gives the taker the AI order
  **`0x2e`** (order byte `+0x4a8`; a request token from `0x006f62b0(taker, 3, 0)` — tranche 20 B.6 — in `+0x4aa`; single
  gate: forwarded with a 3,000 value through `0x004f4830` when not local, applied locally otherwise); then the taker's
  timer at `+0xc30` is armed with a random 2,000-4,000 (`0x00d9e180`).

HYPOTHESIS: true = "grab now", false = "go and take the victim as a shield". **Logic notes (CONFIRMED structure):** unlike
its player sibling (`player_take_human_shield_do`, tranche 17 G.3: gates and a squared-range test) this binding has **no
range test, no "taker ≠ victim" test and no "already holding someone" test**; with arg 3 true and taker = victim,
`0x009acb20` pairs a character with itself. No null dereference.

### F.8 `character_parachute_open` → `0x00a41d00`

**Arguments:** 1 character; 2 boolean, **unconditional**; 3 nil-gated string = an animation action name, default
**`"free fall network"`** (`0x0114d624`). **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing):** if the character resolves: action = −1, or, for a non-empty arg 3, the index from
`0x004bf810` (dumped at depth 1: name hash then case-insensitive compare over the action table `0x03171c10`, 16-byte
records, count `0x03171c08`; null or unknown → −1). Then `0x00995f30(character, 1, arg 2, action, 0)` (dumped at depth 1,
read for structure):
- single gate on the character: not local → opcode-`0x41` record, sub-type `0x1d`, the character id, both booleans and the
  14-bit action, sent to the owner (`0x008ae020`), **and nothing local**;
- local: the second argument 1 **skips the "may open now" checks** (`0x00995830` and a timer) that the engine's own callers
  get; sets character `+0xe8` bit `0x4000000`; spawns an object of type `0x1e` at the character (`0x00456e30`); marks it
  `+0xdc` bit 0 (the marker F.9 looks for) with the character's handle at `+0x180/+0x184`; attaches it (`0x009215b0`);
  arg 2 false → plays the object's own animation `0x43c`, true → syncs its animation player to the character's
  (`0x004b1b80`); then plays the action through `0x004b21a0` (whose first step is a lookup that skips −1 — HIGH
  CONFIDENCE); then the character-side updates (`0x009adb00`, `0x00995410`, `0x00957c80`, `0x0070e690`, `0x0070dcf0` with
  `0x1f919344`, camera calls for the player, `0x009499a0`, `0x00ab2260`).

**Logic defect (CONFIRMED structure):** nothing on the forced path tests whether the character already has a parachute
(`+0xe8` bit `0x4000000` or an existing `+0xdc`-marked object) — each call spawns another parachute object. No null
dereference (the spawn result is checked).

### F.9 `character_parachute_play_action` → `0x00a41da0`

**Arguments:** 1 character; 2 action name (both unconditional). **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing):** action index from `0x004bf810` (F.8); −1 → nothing. Otherwise
`0x00a273f0(character, index, 0)` — **called even when the character did not resolve**, but `0x00a273f0` checks for null
(`0x00a2742f`). With its third argument 0 it first **broadcasts** an opcode-`0x43` record (sub-type `0x28`, the character,
the 14-bit index) to the session (`0x0087ba20` → `0x0086f1b0`, the unconditional-send family of
`spec-lua-api-behaviour.md` §45.7), then walks the character's attached-object ring (`+0x1c`, next `+0x20`) for an object
of kind row `+0x9` bit `0x40` — preferring one with `+0xdc` bit 0 (F.8's marker) — and, if it is animated (`+0x33` bit
`0x08`), plays the action on its animation player (`0x004b1b90`, rate 1.0). No parachute → a stray `0x008addb0(character,
0)` call whose result is discarded. HYPOTHESIS: the loop's fall-back keeps the last kind-matching object even without the
marker. No crash shape.

### F.10 `character_fake_revival_start` → `0x00a41f80`

**Arguments:** 1 character. **Return:** 0 values. CONFIRMED — listing.

**Body:** `0x009a5520(character, 1, 0)`, then **sets bit 16 (`0x10000`) of character `+0xe4`** (`0x00a41faa`) — tranche
13 B.5's sibling reading, re-verified. **Crash — CONFIRMED (re-verified):** no null check; an unresolved name crashes
first at `0x009a553f` (`0x009a5520` reads `+0xcd2` of null). **New logic note (CONFIRMED structure):** the bit is set
**whatever `0x009a5520` did** — its first test (`+0xcd2` not 0 or 6 and `+0xcce` = 8, tranche 13's "downed" HYPOTHESIS)
can refuse, and the result is never read, so a character that is not downed is still flagged "fake revival in progress"
until `character_fake_revival_end` clears it.

---

## G. Method notes for the follow-up runs

- Range run: `0x00a21bb0-0x00a21c10`, `0x00a21ae0-0x00a21b30`, `0x00a21d00-0x00a21d70` (gameplay rows),
  `0x005f1fc0-0x005f2040` (crib purchase table), `0x0080e540-0x0080e5a0` (crib garage table), `0x007c04b0-0x007c0600`
  (completion table), `0x007c06d0-0x007c0730` (the `0x012fd1b8` = 3 writer), `0x007ad240-0x007ad2b0` (the
  `0x0224253a` reader), `0x00bcd240-0x00bcd2c0` (cinema table).
- Pointer run (`count:8`): `0x0130b8c4`, `0x0101c0dc`, `0x0101c10c`, `0x0101c180`, `0x0101c1dc`, `0x0129d9cc`,
  `0x012a2e28` (the double 2.0 used as the key of `t[2]` in C.1), `0x012a2dd8` (the double 100.0 of D.5).
- Follow-up dump (depth 0, `maxinsn:900`): `0x007c1640 0x007c1980 0x007c4820 0x007c1ef0 0x007c2020 0x007c2060 0x007c2ac0
  0x00e0ceb0 0x007c3050 0x007c54d0 0x00725df0 0x00723ea0 0x007235c0 0x005f0de0 0x0093d320 0x0093bde0 0x005f1a10 0x00a46490
  0x00a46500 0x005eea30 0x00b98d00 0x005f1750 0x005f1690 0x0094d920 0x00a00ef0 0x00a00f90 0x005eb3e0 0x007bda70 0x007bfae0
  0x00bd5d00 0x00bd5bf0 0x00bd5c00 0x00bcf040 0x007aa5f0 0x007acc10 0x008436c0 0x00bc5b70 0x00bcc010 0x00d48db0 0x00947890
  0x004b21a0 0x00dafb60 0x00a27e20 0x009b95c0 0x0055c8d0`. `0x00725df0`, `0x009acb20` (depth 1 of the `lua` run) and
  `0x00995f30` (depth 1) were read for structure and the listed steps only.
- Second follow-up dump (depth 0): `0x006d9e10 0x007229b0 0x00a462c0 0x00a42b30 0x00a46340 0x00da7930 0x00849fa0
  0x007aa5d0 0x00bc5a90`.
- Xref run: `0x012ec8f4 0x012ec964 0x0153b556 0x014a0f90`, then `0x005f1fc0 0x0224253a`.
- Import check: the five `cinema_editor_save_clip` import slots (`0x0101c0dc`, `0x0101c0e0`, `0x0101c10c`, `0x0101c180`,
  `0x0101c1dc`) hold hint/name addresses; reading the PE import names from the executable gave `CloseHandle`,
  `CreateFileW`, `GetLastError`, `WriteFile`, `SetFilePointer`.
- All 25 handlers were taken from the `{name, function}` slot pairs (insn offset +1); none needed the
  pointer-before-name correction.

---

## H. Cross-function observations

1. **Script-controlled memory corruption, three new instances.** `dialog_box_create_internal` copies the callback name
   into a 64-byte field with no limit (B.1 crash 1); `cell_playlist_save_list` uses script numbers as song-table indices
   for a 16-bit write (E.6 crash 1) and narrows a negative count to 65,535 before filling a 512-entry stack array (E.6
   crash 2). All three are reachable from ordinary Lua arguments and are the highest-priority items of this tranche for a
   binding layer: bound the name to 63 characters, drop ids outside the song table, clamp negative counts to 0.
2. **Resolution failure used as a pointer.** `crib_purchase_purchase_crib` sets the crib pointer to 0 on a failed lookup
   and reads `+0xe0` through it on the next instruction (D.2); `character_fake_revival_start` passes an unchecked
   resolver result on (F.10, tranche 13). The sibling-with-a-check templates are `crib_unlock_strongold` (`0x00a46500`,
   checks before calling the same `0x005f1cc0`) and `character_is_on_fire` (F.4).
3. **Side effects that outlive a refused or premature start.** `cutscene_play_do` applies its two destinations even when
   the start was refused, overwriting a running cutscene's end teleports (C.1 item 3); `cutscene_play_check_done` says
   "done" before a zscene cutscene has a manager (C.2); `crib_purchase_purchase_crib` charges before testing ownership
   (D.2). Same family as tranche 20 K.2's "true that does not mean done".
4. **Corrections for the spec fold (CONFIRMED — this tranche's listings):**
   - `spec-lua-api-behaviour.md` §25.23 (`continuous_explosion_follow_target_add`): (a) the liveness check **is** made,
     inside the resolver `0x00a45800` (`0x00a4581c`) — the "no liveness check, structural outlier" statement and its
     cross-function note should go; (b) `+8/+0xc` is the object's **handle pair**, not a position; (c) `0x005eb3e0`
     keeps **at most two** follow targets and only while the effect is active (`0x012ec964`).
   - `interp_tabs.md` Q7.3: the button helper `0x007c2ac0` returns the new block, or 0 (E.3).
   - `spec-lua-api-behaviour.md` §23.19: player `+0x209c` is, by the offsets read here, a crib record (D.3) — a refinement
     of "garage-vehicle-list pointer", HIGH CONFIDENCE.
5. **Lua-created dialogs ignore priority ordering.** `0x007c4b60` always makes the new dialog the head of the open ring
   (B.1 step 2), whereas the engine opener `0x007c3310` inserts by priority. Whether the manager pass re-sorts or scans
   by priority (tranche 13 D.2 says it "promotes the highest-priority queued dialog") is OPEN; if it trusts the head, a
   low-priority script dialog can pre-empt an engine notice.
6. **Unchecked hook records.** `dialog_pause_unpause`'s hide path (and the matching show path) writes through the
   `0x00e0ca80` result with neither a "hook exists" test nor a null check (B.2) — a second instance after
   `cellphone_dial` (§45.4). The dialog manager's own callback path (B.1) shows the checked form.
7. **Return shapes a reimplementation must copy.** `crib_get_cash_stash` returns nothing and *collects* the stash (D.5);
   `character_is_combat_ready` returns no value for an unknown name (F.5); `dialog_box_create_internal` returns no value
   when all four slots are busy (B.1); `completion_call_callback()` with no argument selects callback case 0 (E.2).
8. **Defaults that act.** `character_prevent_kneecapping(name)` with the boolean omitted clears immunity (F.2);
   `character_set_over_the_shoulder(name)` defaults to on (F.3); `cutscene_play_do` defaults its fade-in and restore
   flags to true (C.1); `dialog_box_create_internal` defaults flags `0x04` and `0x02` on.
9. **Three more `human_force_flags…` bits** with the double-gate setter and one receive dispatcher `0x008ae650`:
   `+0x1c9c` bit 15 (synced finishers), `+0x1c98` bit 3 (kneecap immunity), `+0x1c98` bit 22 (over the shoulder).
10. **No session claims in this tranche.** `0x005f1cc0` (D.2), `0x00a00ef0` (D.4), `0x005f1a10` (D.1) and `0x00725df0`
    (C.1) branch on whether a session exists and reports host; this note describes both branches and draws **no**
    inference from them about single player — `spec-lua-api-behaviour.md` §8.27/§26.28 already settle what session state
    single player has.

---

## I. OPEN

- B.1: what the element added by `0x007c1aa0` (arg 6) is; what slot `+0x128` (arg 15) controls; whether the manager
  depends on the open ring's head order (H.5); the meaning of flag bits `0x02`/`0x04`/`0x08`/`0x20`.
- B.2: the values the engine's other callers pass as `0x007c1ef0`'s first argument; what `0x00707540(4)`,
  `0x008020b0` and `0x008b7260` do.
- C.1: the purpose of the eight group handles (manager `+0x36a8`); the source and length of the `0x006e1d70()` string;
  what the shipped Lua wrapper passes as arg 2 (table or boolean).
- C.2: whether the shipped wrapper polls `cutscene_play_check_done` immediately after `cutscene_play_do`.
- D.1: what `0x0093d320` activates on the linked object; the caller `0x00b9aaa0`.
- D.2: where (if anywhere) the non-host branch of `0x005f1cc0` marks the crib owned; the table at `0x0290f188`; who
  writes the selection `0x014a0f90` in normal flow.
- D.4: the meaning of `+0x2098` modes 2/4 on the host (`0x00a00ef0`).
- E.1: what the follow-target list drives; what `0x005eb6c0` writes as the start count.
- E.2: who installs the completion callbacks `0x02282510`/`0x02282514`/`0x02282518`.
- E.4: whether another path starts `"city_load_thread"` when the script never calls the binding; the 10,000 time test.
- E.5: the string table behind `0x008436c0` and whether its values are wide strings; the zone writer `0x00bc7e80`.
- E.6: the song table's capacity versus the ±640 KB write range; what `0x00bda2c0` does with the value read.
- F.5: `0x004b2190`, `0x009b9180`, `0x009ba300` meanings; whether `+0xd24` can be null for a live human.
- F.7: the remainder of `0x009acb20`; what AI order `0x2e` and action state `0x15` are.
- F.8: whether a second parachute object is harmful or cleaned up; the anim id `0x43c`.

---

## J. Direct-answer table

| # | name | registrar | handler | status |
|---|---|---|---|---|
| 1 | `dialog_pause_unpause` | dialog `0x007c5630` (under UI) | `0x007c2eb0` | resolved — hides a script-openable pause display or cancels a deferred show; unchecked hook record (B.2) |
| 2 | `dialog_box_create_internal` | dialog | `0x007c5130` | resolved — 15-arg dialog builder, returns handle; **unbounded callback-name copy**, nil-text and no-context crashes (B.1) |
| 3 | `cutscene_play_do` | gameplay `0x00a20840` | `0x00a46bb0` | resolved — start cutscene with ≤ 8 groups and exactly-2 destinations; boolean arg 2 → uninitialised handles; destinations overwrite a running cutscene (C.1) |
| 4 | `cutscene_play_check_done` | gameplay | `0x00a46690` | resolved — true in state 0 or states 2-12 without a manager; **true before a zscene cutscene starts** (C.2) |
| 5 | `crib_purchasing_unlock` | gameplay | `0x00a42c00` | resolved — synced `"cribs"` byte = 1 and enable every unowned crib's purchase trigger (D.1) |
| 6 | `crib_purchase_purchase_crib` | crib purchase `0x005f1fc0` (under UI) | `0x005f1e50` | resolved — pay, grant (host/other branches), fly-by; **null read with no selected crib**; charges before ownership test (D.2) |
| 7 | `crib_is_stronghold` | crib purchase | `0x005ef3e0` | resolved — player's crib record has any level hash set (D.3) |
| 8 | `crib_is_garage_disabled` | crib garage `0x0080e540` (under UI) | `0x0080e0b0` | resolved — feature-disable test id 5: flag bit, host-garage handle, or mission list (D.4) |
| 9 | `crib_get_cash_stash` | crib purchase | `0x005eed30` | resolved — awards the stash `0x014a0fe4` and zeroes it; **returns nothing** (D.5) |
| 10 | `continuous_explosion_follow_target_remove` | gameplay | `0x00a46300` | resolved — swap-remove a handle from the ≤ 2-entry follow list; corrects §25.23 (E.1) |
| 11 | `completion_call_callback` | completion `0x007c04b0` (under UI) | `0x007bfb90` | resolved — selects completion-screen callback case 0-3 (E.2) |
| 12 | `community_login_open` | main menu `0x007c9fc0` (under UI) | `0x007c9d40` | resolved — community blurb dialog, then login request (E.3) |
| 13 | `city_load_img_load_complete` | UI helper `0x00845aa0` | `0x00842b20` | resolved — sets `0x0224253a`, gating the city load thread (E.4) |
| 14 | `cinema_editor_save_clip` | cinema editor `0x00bcd240` (under UI) | `0x00bcbd70` | resolved — writes `<Documents>\My Games\…\<name>.srtt_clip`; uninitialised path on API failure (E.5) |
| 15 | `character_use_synced_finishers` | gameplay | `0x00a428e0` | resolved — double-gate, `+0x1c9c` bit 15 (F.1) |
| 16 | `character_take_human_shield_do` | gameplay | `0x00a46740` | resolved — instant grab or AI order `0x2e`; no range/self/busy tests (F.7) |
| 17 | `character_set_over_the_shoulder` | gameplay | `0x00a426d0` | resolved — `+0x1c98` bit 22, default on (F.3) |
| 18 | `character_prevent_kneecapping` | gameplay | `0x00a41f20` | resolved — double-gate, `+0x1c98` bit 3; omitted flag clears (F.2) |
| 19 | `character_parachute_play_action` | gameplay | `0x00a41da0` | resolved — broadcast, then play the action on the attached parachute (F.9) |
| 20 | `character_parachute_open` | gameplay | `0x00a41d00` | resolved — forced open, spawns a parachute object every call (F.8) |
| 21 | `character_is_on_fire` | gameplay | `0x00a417c0` | resolved — `+0xe8` bit 8 (F.4) |
| 22 | `character_is_combat_ready` | gameplay | `0x00a415a0` | resolved — `0x00943a20` readiness test; **no value** for an unknown name (F.5) |
| 23 | `character_fake_revival_start` | gameplay | `0x00a41f80` | resolved — re-verified tranche 13: null crash; bit set even when refused (F.10) |
| 24 | `character_check_resource_loaded` | gameplay | `0x00a46af0` | resolved — human: resource state 3; group member: spawned and not retrying (F.6) |
| 25 | `cell_playlist_save_list` | playlist `0x0083e850` (under UI) | `0x0083e6f0` | resolved — store ≤ 512 track ids and start playback; **unchecked id write**, negative-`n` stack overflow (E.6) |

---

## K. Crash-shaped and logic defects (summary)

| # | where | defect | status |
|---|---|---|---|
| 1 | E.6 `0x0055e3ef` / `0x0055e3fd` | `cell_playlist_save_list` track ids index the song table with no bound: 16-bit write at `0x013bbc24 + 20·id`, pointer read at `0x013bbc18 + 20·id` | CONFIRMED structure |
| 2 | E.6 `0x0083e739` → `0x0083e7a6` | negative table `n` → count narrowed to up to 65,535 → writes past the 512-entry stack array (cookie, return address, caller frames) | CONFIRMED structure |
| 3 | B.1 `0x007c4c00`-`0x007c4c08` | callback name copied unbounded into the 64-byte slot field `+0x140`; ≥ 68 characters corrupt the next slot's ring links or the first body-text pool block | CONFIRMED structure |
| 4 | D.2 `0x005f1ed4` → `0x005f1edc` | `crib_purchase_purchase_crib` with no/stale/dead selected crib → read `[0 + 0xe0]` | CONFIRMED |
| 5 | B.1 `0x007c4c00`, `0x007c1698`, `0x007c1a83` | nil arg 3 / arg 1 / arg 2 → copy or `strncpy` from null | CONFIRMED structure |
| 6 | F.10 `0x009a553f` | `character_fake_revival_start` unresolved name → null read (tranche 13, re-verified) | CONFIRMED |
| 7 | B.1 `0x007c4c51` | no current Lua-thread context → `[0 + 0x14]` | CONFIRMED shape, reachability HYPOTHESIS |
| 8 | B.2 `0x007c212b` (and the show path) | hook record written without null check or existence test | CONFIRMED shape, reachability HYPOTHESIS |
| 9 | C.1 (inside `0x00725df0`) | failed `0x45a0`-byte manager allocation → writes through null (`+0x36a8`, `+0x21c0`, `+0x35da`); unbounded name copy with 80 bytes of room | CONFIRMED structure, reachability HYPOTHESIS |
| 10 | E.6 `0x0055e380`, `0x00560930` | null station array; null local player; stale vehicle handle with seat state 3 → `[0 + 0xbf4]` | CONFIRMED shape, reachability OPEN |
| 11 | D.2 `0x005f1ed6`, D.5 (`0x0094d920`) | local player used without null check | CONFIRMED shape, reachability OPEN |
| 12 | E.5 `0x00bc6540` | uninitialised wide path buffer used by `CreateFileW` when path construction fails | CONFIRMED structure, reachability HYPOTHESIS |
| 13 | F.5 `0x00943a20` | `[+0xd24] + 0x10` read without checking `+0xd24` | CONFIRMED shape, reachability OPEN |
| 14 | C.1 | boolean arg 2 → eight uninitialised stack "group handles" copied into the cutscene | CONFIRMED structure |
| 15 | C.1 | destinations applied after a refused start overwrite the running cutscene's end teleports | CONFIRMED structure |
| 16 | C.1 | a destination table with ≠ 2 entries is silently ignored | CONFIRMED |
| 17 | C.2 | `cutscene_play_check_done` true while a zscene cutscene is still loading (no manager yet) | CONFIRMED structure |
| 18 | D.2 | price charged before the ownership test; host branch does nothing for an owned crib | CONFIRMED structure |
| 19 | D.2 | host and non-host branches of `0x005f1cc0` do different work (only host clears `+0x7a` bit `0x02`, counts, levels) | CONFIRMED structure, consequence OPEN |
| 20 | F.8 | every `character_parachute_open` spawns another parachute object (no already-open test on the forced path) | CONFIRMED structure |
| 21 | F.10 | fake-revival bit set even when `0x009a5520` refused | CONFIRMED structure |
| 22 | F.7 | no range, self or busy test in `character_take_human_shield_do` | CONFIRMED structure |
| 23 | E.1 / §25.23 | follow list capped at 2, no duplicate check, removes ignored while the effect is stopped | CONFIRMED |
| 24 | B.1 | Lua dialogs become the open-ring head regardless of priority | CONFIRMED structure, consequence OPEN |
| 25 | E.4 | city load thread waits on a script call with no timeout on the path read | CONFIRMED structure (within range), OPEN globally |
| 26 | D.5, F.5, B.1, E.2 | return-shape quirks: no value from `crib_get_cash_stash`, from `character_is_combat_ready` (unknown name) and from a full dialog pool; `completion_call_callback()` = case 0 | CONFIRMED |
| 27 | F.2, F.3 | omitted boolean clears kneecap immunity; over-the-shoulder defaults on | CONFIRMED |

---

## L. Clean-room check

- Addresses are plain hex throughout; no Ghidra auto-names for functions, globals or labels, no decompiler variable
  names, and no pasted pseudocode. Library and system routines are named only where they are public: the C run-time
  (`strncpy`, `swprintf_s`, `_stricmp`, `_wcsicmp`), the Win32 API (`CreateFileW`, `WriteFile`, `SetFilePointer`,
  `CloseHandle`, `GetLastError` — from the executable's import table; `OpenProcessToken`, `SHGetFolderPathW` as Ghidra
  identified them), and the public Lua 5.1 API by established project convention; short game strings (Lua hook names,
  debug tags, synced-variable names, localisation keys, file extension) are quoted as data.
- Self-check run on the finished file, as the last step before reporting, with the pattern
  `\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4}|LAB_[0-9a-fA-F]+|FUN_[0-9a-fA-F]+|DAT_[0-9a-fA-F]+)\b`
  (`grep -cP`, and the same pattern through Python `re` line by line): **0 hits** (`grep -cP` 0 lines; Python `re` 0
  tokens). The pattern was first checked against 16 controls: 12 positives (auto-named locals and parameters, a register
  input, a stack array, an `unaff_` register, a type name, and the `LAB_`/`FUN_`/`DAT_` label forms), all matched, and 4
  negatives ("left undefined", "in ECX", a bare `0x…` address, ordinary prose), none matched.
- No spec file was edited. The private Ghidra copy `tools\gp_t23` was deleted after the dumps; raw dumps stayed in the
  session scratchpad.
