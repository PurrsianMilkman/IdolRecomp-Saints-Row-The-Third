# Ranking tranche 12 — 25 previously-unspecced Lua-bound names (Team A, 2026-10-02)

Job definition: `D:\Project Crreish\TEAM A\ghidra\jobs\ranking\tranche-12.json`. **Range note:** the job file's own title
says **names 226-250** of the 554 unspecced names (Team B call-count order), not 276-300 as the dispatch brief guessed;
the 25 names below are exactly the job file's list. Run locally, read-only, on a private copy of the Ghidra project
(`tools\gp_t12`, deleted afterwards), with the job's own arguments: `CrreishDump.java <out>/lua lua depth:1 maxfuncs:15
maxinsn:500 <25 names>`. **All 25 names resolved in that one call.** Every name string occurs exactly once in `.rdata`,
and its handler is the code pointer stored in the stack slot right after the name (insn offset +1) — CONFIRMED —
dump `index.txt`. Follow-up runs on the same private copy, cited by name below:

- "follow-up dump": depth-0 `func` run on 32 addresses (`0x00bc68a0 0x007abbb0 0x00bc6270 0x00bc5b70 0x00bc5230
  0x0085d980 0x00706ab0 0x00706fe0 0x00706e00 0x007aebb0 0x0087f540 0x0087e0e0 0x007029b0 0x0087c250 0x0088c1d0
  0x008ba170 0x00843220 0x00843560 0x00844160 0x007ab5c0 0x006e3fa0 0x006e3ff0 0x0059f8c0 0x007aad90 0x004f5dd0
  0x00eb3a70 0x007efa30 0x00681370 0x00877a90 0x00870810 0x008444b0 0x00d221a0`);
- "second follow-up dump": depth-0 `func` run on `0x00eb3a8e 0x00ea3ac8 0x00eb1118 0x00e22ac0 0x00706f40 0x0084e290
  0x00bc5170 0x00e0ce00 0x0087e400 0x00853b30 0x007f0fc0 0x008fa240`;
- "third follow-up dump": depth-0 `func` run on `0x008425d0 0x00842640 0x007027b0 0x007027d0 0x008426e0 0x00e0d340
  0x00bc5a90`;
- "ptrs run": `ptrs count:2` on `0x012a30d8` and `0x012a2d70`.

The full 113-row name/handler table of the `game_` registrar `0x00845aa0` already on disk
(`tools/lua_game845aa0_full.txt`, spec-lua-bindings §13.5) was used to name aliases of shared handlers.

Call counts are Team B's (`for-team-b/team-b/tools/lua_reconciliation_called_and_registered_1181.tsv`): **every name in
this tranche is 2 call sites**; 2 distinct scripts for `game_show_party_ui`, `game_show_coop_partner_gamercard`,
`game_show_community_sessions_ui`, `game_set_coop_join_type`, `game_record_mode_is_active`,
`game_machinima_playback_enter`, `game_join_syslink_game`, `game_is_connected_to_internet`, `game_difficulty_select`,
`game_coop_kick_player` and `flee_to_navpoint`, and 1 for the other 14. Team B tags 23 names `ui` and
`fov_check_xz_plane`/`flee_to_navpoint` `gameplay`, which agrees with the registrars below.

Labels: **CONFIRMED** = read in the listing or decompile of a dump made for this note; **HIGH CONFIDENCE** = follows
from a dumped call or reference, but the callee body was not dumped or a meaning is inferred from strong usage;
**HYPOTHESIS** = plausible, not settled; **OPEN** = not settled (collected in section I).

**Shared conventions, cited not re-described:** every entry opens with the `lua_gettop` prologue (`0x00dfde50`) and
reads arguments with negative stack indices counted from the bottom (`-N` = arg 1, `1-N` = arg 2, …). Primitives as in
`spec-lua-api-behaviour.md` §4.1 and tranches 09-11: `0x00dfe210` `lua_tolstring` (null for a non-string),
`0x00dfe1e0` `lua_toboolean`, `0x00dfe040` `lua_type` (0 = nil), `0x00dfe160` `lua_tonumber` (a non-number reads as 0),
`0x00ea2596` truncating float-to-int, `0x00dfe590` `lua_pushboolean`, `0x00dfe3a0` `lua_pushnumber`, `0x00dfe420` push C
string (nil when null). `0x009da4e0` local player (global `0x0262edfc`), `0x0087ba20` session (global `0x024d8534`;
"host" below means session `+0x5c` == `+0x58`, as tranche 09 B.12 uses it), `0x008b7100` first remote player (tranche
09 B.12). Resolvers `0x00a281a0`, `0x00a28150`, `0x005982e0`, the liveness guard `0x00853b10`, the class-flag rows
`0x02cc9900[object +0x34]` and the handle table `0x00458230`/`0x031d152c` are as tranches 10-11 state. The **Lua
callback coroutine mechanism** (`0x00e0ca80` → `0x00e0c720`, run through `0x00e0cd00`/`0x00e0cba0`, released by
`0x00e0c610`/`0x00e0c650`, 256-record thread table, argument pushers on the call object) is exactly as tranche 09's
front matter ("Lua callback helpers") states; `0x00e0ce00` (pushes a boolean, increments the call object's argument
count) is CONFIRMED again here (second follow-up dump). The UI-safe wide-string **encoder** `0x00e22a30` is as tranche
09's front matter states.

## A. Registrars — where the 25 names live

Five registrars. CONFIRMED — `index.txt` plus the follow-up dumps of `0x00bc68a0` and `0x007abbb0`:

| registrar | reached from | names in this tranche |
|---|---|---|
| `0x00845aa0` (113-row `game_` table, spec-lua-bindings §13.5; not under the UI registrar, tranche 11 §A) | `0x008489e0` | 14: `game_show_party_ui`, `game_show_coop_partner_gamercard`, `game_show_community_sessions_ui`, `game_set_coop_join_type`, `game_join_syslink_game`, `game_is_pc_dx11`, `game_is_local_player_in_vehicle`, `game_is_joinable`, `game_is_connected_to_internet`, `game_is_autosaving`, `game_hud_hide`, `game_get_ps3_button_swap`, `game_coop_kick_player`, `game_coop_get_starting_syslink` |
| `0x007e18e0` (difficulty / pause-menu sub-registrar, tranche 10) | UI registrar `0x008430f0`, call at `0x0084317d` | 3: `game_record_mode_is_supported`, `game_record_mode_is_active`, `game_difficulty_select` |
| **`0x007abbb0` (lobby sub-registrar, new)** | UI registrar `0x008430f0`, call at `0x00843144` | 4: `game_lobby_update_map_selection`, `game_lobby_update_char_selection`, `game_lobby_set_horde_mode_data`, `game_lobby_get_local_player_name` |
| **`0x00bc68a0` (machinima sub-registrar, new)** | UI registrar `0x008430f0`, call at `0x00843216` | 2: `game_machinima_playback_enter`, `game_machinima_is_recording` |
| gameplay `0x00a20840` | (established) | 2: `fov_check_xz_plane`, `flee_to_navpoint` |

- **`0x007abbb0`** builds 9 `{name, function}` pairs on the stack and loops 9 times over `lua_pushcclosure`
  (`0x00dfe4f0`) then `lua_setfield(L, -10002, name)` (`0x00dfe830`). Its 9 names: `game_lobby_get_local_player_name`,
  `game_lobby_get_remote_player_name`, `game_lobby_set_horde_mode_data`, `game_lobby_horde_continue_from_wave`,
  `game_lobby_update_map_selection`, `game_lobby_update_char_selection`, `game_lobby_update_ready_state`,
  `game_lobby_finished_loading`, `game_lobby_coop_finished`. CONFIRMED (follow-up dump, string literals).
- **`0x00bc68a0`** does the same for 7 names: `game_machinima_record` (`0x00bc5840`), `game_machinima_is_recording`
  (`0x00bc5860`), `game_machinima_playback_enter` (`0x00bc67d0`), `game_machinima_clip_exists` (`0x00bc5c80`),
  `game_machinima_copy_clip` (`0x00bc5d20`), `game_machinima_overwrite_clip` (`0x00bc5e60`),
  `game_machinima_delete_clip` (`0x00bc5fa0`). CONFIRMED (follow-up dump).
- **Index oddities, not second bindings:** `game_lobby_get_local_player_name` is reported with 2 uses; the second
  (`0x007abc6e`, candidate dump rooted at `lua_setfield` `0x00dfe830`) is the loop reading the table's first row.
  `0x00bc5860` shows a second DATA reference at `0x00bc6935` for the same reason (the loop's function-pointer push,
  annotated with the first two rows' values). CONFIRMED (follow-up dump listings at `0x007abc65`/`0x00bc6935`), same
  artefact as tranche 09's `pcu_bra_required`.
- **Shared handlers (aliases), from `tools/lua_game845aa0_full.txt` plus the dumps' reference lists — CONFIRMED:**
  - `0x00a3c670` (push false) is bound to `game_is_pc_dx11`, `game_get_ps3_button_swap`, `game_is_german_build`,
    `game_is_demo`, `game_is_coop_start_screen`, `game_is_debug_reloading_level` (all `0x00845aa0`), to
    `game_record_mode_is_supported` and `game_record_mode_is_active` (`0x007e18e0`, plus one more slot at
    `0x007e1980`), and to further slots in `0x00a20840` (`0x00a20c4d`, `0x00a22d0b`), `0x0083bd90` and `0x008377d0`.
  - `0x007c9f50` (the shared no-op stub of spec §6.1/§24.2/§27.17) is bound to `game_show_party_ui`,
    `game_show_community_sessions_ui`, `game_send_party_invites`, `game_peg_load` and
    `game_event_tracking_interface_exit` in `0x00845aa0`, among many others.
  - `0x008428e0` is bound to both `game_is_connected_to_internet` and **`game_is_connected_to_service`**.

## Shared machinery new in this tranche

- **Constant-1 stubs whose results are discarded:** `0x00d34d20`, `0x00d34cf0`, `0x00d2f520`, `0x0101b5b0`,
  `0x00d34c90`, `0x00d48d10`, `0x00d34cb0`, `0x0101b570`, `0x00d221a0` — each is "return 1" and every call in this
  tranche ignores the result. CONFIRMED (bodies dumped). HYPOTHESIS: compiled-out debug/profiling hooks, the same
  family as tranche 11's `0x00d34cd0`/`0x00d21330`. They are omitted from the bodies below.
- **`0x00db0510(buffer, format, …)`**: a wide `sprintf` wrapper that calls the CRT `_vsnwprintf(buffer, 0x400, format,
  args)` — **the character limit is always 0x400 (1024 wide characters, 2048 bytes)**, whatever the caller's buffer
  size. CONFIRMED (listing, kick dump). Three callers in this tranche pass a **0x200-byte (256-wide-character) stack
  buffer** (C.2), so the limit does not protect them (section K).
- **`0x00e22ac0(out, count, in)`** — the **decoder** that reverses tranche 09's encoder `0x00e22a30`: it requires the
  input's first byte to be `0x80`, then reads 3-byte groups (tag, low, high; tag bit 1 → low byte is 0, tag bit 2 →
  high byte is 0) until a tag of 8 or until `count − 1` characters, and terminates the output. **If the input is
  null, empty, or does not start with `0x80`, it returns false and writes nothing at all** — the output buffer is
  left as it was. CONFIRMED (listing, playback dump).
- **`0x00e0ceb0()`** returns the **currently running script-thread record** — the top of tranche 09's 16-deep
  "current thread" stack (depth `0x02a44d10`, slots from `0x02a44d18`) — or null when the depth is 0 or above 16.
  CONFIRMED (kick dump). Callers copy its `+0x14` into the coroutines they create, as tranche 09's `0x00e0c8f0` does.
- **`0x00e0d340(name, bool, L)`** sets a **Lua global** to a boolean, **only if that global already exists** (it reads
  it first and returns if it is nil). CONFIRMED (third follow-up dump).
- **`0x004f5dd0` is a queued per-character AI event, not an effect call.** Arguments: (event-type byte, source
  handle low/high, recipient handle low/high, delay, byte, byte, optional 32-byte payload). The recipient's 64-bit
  handle is resolved through the handle table (`0x00458230` with key `0x031d152c`, tranche 11 "Handle resolution");
  the object must not have `+0x33` bit `0x10`, must have class-row `+0xa` bit `0x01`, must be alive (`0x00853b10`) and
  must **not** have class-row `+0xa` bit `0x02` (tested as flag index `0x21` by `0x00853b30`; tranche 11 reads that
  bit as "player"). A negative delay becomes a random 250..750 (`0x00dab660(0xfa, 0x2ee)`). A record is taken from the
  free list at `0x01361998`, filled (type at `+8`, source at `+0x10`, recipient at `+0x18`, two timers through
  `0x00d9e140`, the two bytes at `+0x28`/`+0x29`, the payload at `+0x30..+0x4f` with flag bit `0x02` of `+0x2a`), and
  appended to the recipient's circular list at object `+0x1c50`. **An empty free list silently drops the event.**
  CONFIRMED (follow-up dump). This bears on spec-lua-api-behaviour §15.4 (`set_always_cower_flag`), which reads its
  `0x004f5dd0(0x26, …)` call as "a visual/VFX cue at the character's position" built from the character's `+8`/`+0xc`
  fields: those two dwords are passed exactly where `flee_to_navpoint` passes a character's 64-bit handle, and the body
  resolves them as a handle. HIGH CONFIDENCE that §15.4's call queues AI event `0x26` on the character, not an effect
  (section H.4).
- **Co-op start flags:** byte `0x014ff6c1` ("starting a co-op game") and byte `0x014ff6c2` ("starting a system-link
  game"). `0x007027b0(v)` stores v in the first and clears the second when v is true; `0x007027d0(v)` does the mirror
  image; `0x00703f50` writes both. Their Lua setters are `game_coop_start_new_live` (`0x008425d0`) and
  `game_coop_start_new_syslink` (`0x00842640`), each taking an optional boolean that **defaults to true**; the getter
  `game_coop_get_starting_coop` (`0x008426e0`) returns true if **either** flag is set. CONFIRMED (third follow-up
  dump). Meaning of the two flags from the sibling names: HIGH CONFIDENCE.

---

## B. Constant stubs — 6 functions

### B.1 `game_is_pc_dx11`, `game_get_ps3_button_swap`, `game_record_mode_is_supported`, `game_record_mode_is_active` → `0x00a3c670`

**Arguments:** none read. **Return:** exactly **1 boolean, always false**. CONFIRMED (four-instruction body: prologue,
push false, return 1).

- `game_is_pc_dx11` answers false in **this** executable, which is the Direct3D 9 build (`SaintsRowTheThird.exe`;
  `spec-output.md` §1.2 lists a separate `SaintsRowTheThird_DX11.exe`). HYPOTHESIS: the DX11 executable binds a
  different handler that answers true — not checked (OPEN).
- `game_get_ps3_button_swap` is a console leftover: always false on PC. CONFIRMED.
- `game_record_mode_is_supported` and `game_record_mode_is_active`: the recording feature reports itself as
  unsupported and inactive, unconditionally. CONFIRMED. (These are two of the six `game_record_*` names tranche 10
  listed in `0x007e18e0`.)

### B.2 `game_show_party_ui`, `game_show_community_sessions_ui` → `0x007c9f50`

**Arguments:** none read. **Return:** 0 values. **Body:** the shared no-op stub (spec §6.1). CONFIRMED. Both are console
party/community overlays with no PC implementation.

---

## C. Co-op, network and Steam — 7 functions (`0x00845aa0`)

### C.1 `game_set_coop_join_type` (`0x00844440`)

**Arguments:** 1 number, truncated. **Return:** 0 values.

**Body:** the value is mapped to a join type: **0 → 0, 1 → 1, any other value (2, 3, -1, …) → 2**, and passed to
`0x00703210(type)`. CONFIRMED (listing: two `SUB`/`JZ` tests then a default).

`0x00703210(type)` (dumped at depth 1): does nothing unless there is **no session or this machine is the host**. If
the type differs from the global **`0x012f44fc`** (file-backed initial value **1**), it stores it there; then, only
with a session and as host, it copies the 16-byte settings block at **session `+0x40`** into a local, writes the type
into that copy's dword `+8`, and calls `0x0087e0e0(&copy)` with the session in ECX. CONFIRMED.

`0x0087e0e0` (follow-up dump): returns at once when session `+0xf4` < 2 or this is not the host; otherwise copies the
16 bytes into a pending block at **session `+0x230`**, and either publishes now (`0x0087dc40`, setting byte `+0x22c`)
when the rate timer at session `+0x240` is idle or expired (`0x00d9e4c0`/`0x00d9e400`), or just marks `+0x22c`
pending. CONFIRMED. So the join type is a lobby setting (dword `+8` of the settings block; current value at session
`+0x48`) published to the matchmaking layer, rate-limited.

**Getter** `game_get_coop_join_type` (`0x008444b0`, sibling): returns 0, 1 or 2 from `0x012f44fc` (any other stored
value reads as 0). CONFIRMED (follow-up dump). Meaning of 0/1/2 (open / friends only / invite only?) is OPEN.

**Logic shapes (CONFIRMED):** a **client's** call is ignored entirely (not even the global is written); an
out-of-range number is silently turned into 2 rather than rejected.

### C.2 `game_coop_kick_player` (`0x00844250`)

**Arguments:** optional arg 1 = name of a Lua callback (read only when present and not nil). **Return:** 0 values.

**Body (CONFIRMED unless tagged):**
1. Records the current script thread's `+0x14` (`0x00e0ceb0`, or 0) in **`0x02317b58`**.
2. **Remote player exists** (`0x008b7100`): format the localized "COOP_KICK_PLAYER_CONFIRM" text with the remote
   player's name at **player `+0xc2`** (passed to a wide format, so UTF-16 — HIGH CONFIDENCE) into a **0x200-byte stack buffer** through `0x00db0510`; copy the callback
   name (or an empty string) into the 32-byte global **`0x02317b38`** (`strncpy` with 31 and a forced terminator,
   `0x00da7930`); open a two-button dialog through `0x007c3de0(title "CONTROL_KICK_PLAYER", body = that buffer,
   callback 0x00843560, 1, 0, 0)` (null-checked; sets dialog `+0x118` = 1).
3. **No remote player:** if a callback name was given, run it at once as a coroutine (`0x00e0ca80` on the global script
   state `0x00e1a1b0`, record `+0x14` = step 1's value) **with no arguments**; then clear `0x02317b38`.

**Dialog callback `0x00843560(dialog, choice)`** (follow-up dump): only for choice 0 (confirm). If this machine is the
host and a remote player still exists, it formats "COOP_PLAYER_KICKED" with the name into a 0x200-byte buffer (again
`0x00db0510`), shows it as a notice (`0x007c3d80`), and **removes the player with reason `0xf`** through
`0x0087e400(player, 0xf)` (session in ECX; follow-up dump: it unlinks the player from the session's circular list at
`+0x54`/`+0xb28c`, decrements the player count at session `+0x60`, and runs the host-side cleanup). Then, if a callback
name is stored, it runs it as a coroutine **with one argument, true**.

**Findings (CONFIRMED):**
- **Callback arity differs by path:** no remote player → 0 arguments; confirm → 1 argument (true); **cancel → the
  callback is never called** and the stored name stays in `0x02317b38`.
- The dialog is offered **on a client too**; confirming there kicks nobody but still calls back with true.
- **Stack-buffer overflow shape:** both formatting calls pass a 256-wide-character buffer to `0x00db0510`, whose limit
  is 1024 wide characters. A localized format string plus player name longer than 255 characters overruns the stack
  frame. Data-gated: the shipped format strings are short and Steam persona names are short (HYPOTHESIS: not reachable
  with shipped data and real names). The sibling **`game_kick_coop_player`** (`0x00844160`, not in this tranche; kicks
  at once with optional "silent" boolean) has the same buffer pattern.

### C.3 `game_show_coop_partner_gamercard` (`0x008425a0`)

**Arguments:** none read. **Return:** 0 values.

**Body:** if a remote player exists (`0x008b7100`), call `0x00870810(player + 0x142)`, which reads the **64-bit Steam
ID at player `+0x142`** and calls `SteamFriends()` → virtual slot **`+0x50`** with (`"steamid"`, id). CONFIRMED.
That slot is Steamworks `ISteamFriends::ActivateGameOverlayToUser` — HIGH CONFIDENCE (two arguments, a C-string dialog
name plus a 64-bit id, the documented `"steamid"` dialog value, and the neighbouring slot `+0x4c` that tranche 11 C.5's
`game_steam_open_overlay` uses for the one-argument overlay call). Unlike tranche 11 C.5, the `SteamFriends()` result is
**not null-checked** here (CONFIRMED; reachable only when the Steam API is not initialised).

### C.4 `game_is_joinable` (`0x00842510`)

**Arguments:** none read. **Return:** exactly 1 boolean. No session → false.

**Body:** `0x0087c8b0(0)` with the session in ECX. With a null argument it first computes **free slots** with
`0x0087c250` = byte session `+0x4d` (capacity) − (number of the two reserved-slot checks `0x00877670` that pass) −
session `+0x60` (player count), and fails when that is 0. Then it requires session `+0xf4` ≥ 2 and `+0xf8` ≥ 2, bytes
`+0xfd`, `+0xfe` and `+0x4c` all zero, and either bit `0x10` of byte `+0x4e` or both `+0xf4` and `+0xf8` below 3.
CONFIRMED (follow-up dump of `0x0087c250`; `0x0087c8b0` at depth 1).

**Logic shape (CONFIRMED arithmetic):** the free-slot test is "not zero", not "greater than zero", so a negative
count (more players than capacity) passes. Reachability OPEN. Meaning of `+0xf4`/`+0xf8` (both "≥ 2" here; the start
check in D.3 compares them too) and of the flag bytes is OPEN; HYPOTHESIS: two lobby state machines (local / remote).

### C.5 `game_is_connected_to_internet` (= `game_is_connected_to_service`) → `0x008428e0`

**Arguments:** none read. **Return:** exactly 1 boolean = `0x008703c0()`: `SteamUser()` is non-null **and** its virtual
slot `+0x4` returns true. CONFIRMED. Slot `+0x4` with no arguments is `ISteamUser::BLoggedOn` — HIGH CONFIDENCE. Note
the difference from spec §27.19's `0x008703b0`, which only tests that the interface exists (interp_tabs Q5.2). So
"internet" and "service" both mean "logged on to Steam"; `game_is_connected_to_network` (`0x008427e0`) is a different
handler, not dumped.

### C.6 `game_join_syslink_game` (`0x00844610`)

**Arguments:** 1 number = **1-based** server index (truncated). **Return:** 0 values.

**Body:** `0x008728d0(index − 1)` (depth 1), which takes its argument as a **byte** and walks the find-server ring
rooted at **`0x024d6834`** (next at record `+0x454`; the 64 × `0x460`-byte pool that `game_start_find_syslink_servers`
builds, tranche 11 C.6) for the record whose dword `+0x450` equals it. On a match: `0x0088c1d0(record + 0x408, 0, 0)`
copies that record's address block into the join target `0x024e4868` and sets `0x024e48b2` = 1 (follow-up dump);
`0x008ba170(1, 0)` starts the join flow (follow-up dump: depending on the save state it either opens the
"COOP_LOAD_TITLE_TEXT" Load / Create new / Cancel dialog or begins loading behind a fade); `0x00843220(0)` decrements
the counter `0x02317b70` (clamped at 0). No match → nothing. The result is ignored. CONFIRMED.

**Logic shapes (CONFIRMED):** the index is compared as 8 bits, so arg 257 (256 after the −1) matches the record
numbered 0 — the same server as arg 1 — and arg 0 looks for a record numbered 255; an unknown index is a silent
no-op; no ring (searches never started) is also a no-op.

### C.7 `game_coop_get_starting_syslink` (`0x008426b0`)

**Arguments:** none read. **Return:** exactly 1 boolean = byte **`0x014ff6c2`** (`0x00702800`). CONFIRMED. See "Co-op
start flags" above: set by `game_coop_start_new_syslink`, cleared by `game_coop_start_new_live(true)`.

---

## D. Lobby — 4 functions, sub-registrar `0x007abbb0`

Lobby globals (all zero-fill at start): **`0x022423a4`** = selected horde map index, **`0x022423ac`** = selected
character index (host / single player), **`0x022423a8`** = character index as written by a client, **`0x022423b0`** =
"start requested" byte, **`0x022423b8`** = host "start latch" byte. Committed selections: **`0x012f3b28`** = map (file
value **-1**), **`0x012f3b30`** = character (file value **-1**), **`0x014f3d30`** = continue flag. Bounds: map count
**`0x014f3d94`** (map records at `[0x014f3d8c]`, stride `0x114`), character count **`0x014f4418`**. CONFIRMED (dump
global listings).

### D.1 `game_lobby_update_map_selection` (`0x007aba90`)

**Arguments:** 1 number, truncated. **Return:** 0 values. **Body:** stores it in `0x022423a4`, **no bounds check**.
CONFIRMED. The consumers (`0x006e6750`, `0x006e6650`, D.3) do check it, so an out-of-range value is not a crash here,
but see D.3 for what it does to the start flow.

### D.2 `game_lobby_update_char_selection` (`0x007abac0`)

**Arguments:** 1 number, truncated. **Return:** 0 values.

**Body:** as a **client in a session**, it stores the value in `0x022423a8` and applies it at once with
`0x006e6690(value)`, which writes `0x012f3b30` only when 0 ≤ value < `0x014f4418`. **Host or no session:** it only
stores `0x022423ac`; D.3 commits it later. CONFIRMED.

### D.3 `game_lobby_set_horde_mode_data` (`0x007ab8d0`)

**Arguments:** 1 boolean, read unconditionally (missing → false). HYPOTHESIS: "continue from the saved wave" (the
sibling `game_lobby_horde_continue_from_wave` is in the same registrar). **Return:** 0 values.

**Body (CONFIRMED):**
1. co-op = byte `0x014ff6c1` or byte `0x014ff6c2` (the co-op start flags).
2. saved = `0x006e6750(map 0x022423a4, co-op)`: **0** when the map index is out of range; otherwise the map record's
   byte `+0x104` is a 3-bit key looked up in a 6-entry progress table — single-player `0x014f3c80` (12-byte entries,
   `0x006e3fa0`) or co-op `0x014f3cc8` (16-byte entries, `0x006e3ff0`): an entry matches when bit 29 of its control
   dword is set and bits 26..28 equal the key; the result is **(low 6 bits of the entry's value dword) + 1**, or **-1**
   when no entry matches (follow-up dumps). HYPOTHESIS: the stored value is the last wave reached.
3. **If arg 1 is true, or saved is 1, or saved is -1 → start now:** if `0x00867830()` is true, set `0x022423b0` = 1;
   otherwise call `0x007ab570` (as host in a session, set `0x022423b8` once; then, unless the fade state `0x0059f9f0`
   is 1 or 3, fade out through `0x0059f8c0(-1, 0x007aad90, 1)`, whose callback runs `0x007b47f0(0x3e)` — HIGH
   CONFIDENCE a game-state change to start the mode). Then commit `0x006e6650(map, character 0x022423ac, arg 1)`, which
   writes `0x012f3b28`, `0x012f3b30` and `0x014f3d30` **only if both indices are in range**.
4. **Otherwise → warning dialog:** `0x007c3310` with title "MENU_TITLE_WARNING", body "ACT_WHORED_RESTART_WARNING",
   buttons "CONTROL_CONTINUE" / "CONTROL_CANCEL", dialog `+0x128` = `+0x118` = 1 and callback **`0x007ab5c0`** (the
   dialog pointer **is** null-checked here). Callback (follow-up dump): choice 0 → the same start as step 3 but with
   the continue flag forced to **0**; any other choice → run the Lua global **`game_lobby_start_dialog_canceled`** as a
   coroutine (record `+0x14` = `0x022423e4`).

`0x00867830` (depth 1): true only when a session exists, the host/client state test on `+0xf4`/`+0xf8`/`+0x224`/`+0xfd`
passes, the player count (session `+0x60`, read by `0x00681370`) is above 1, and every other player's status byte
(`0x00877a90`: a 5-byte-stride table indexed by player byte `+0x158`) is at least 1. CONFIRMED. HYPOTHESIS: "the other
players are ready", so in a populated session the start is only *requested* (`0x022423b0`) and the lobby update loop
(`0x007ab410`/`0x007abe40` read the byte) performs it; alone, it starts at once.

**Logic shape (CONFIRMED):** an out-of-range map index makes `0x006e6750` return 0, which is neither 1 nor -1, so the
player is shown the *restart* warning although there is no saved progress; confirming then starts the mode while
`0x006e6650` refuses to commit the map — the committed map stays whatever it was, **-1 on a fresh boot**. Whether a
horde start with map -1 is handled downstream is OPEN.

### D.4 `game_lobby_get_local_player_name` (`0x007ab030`)

**Arguments:** none read. **Return:** **no local player → 0 values**; otherwise **1 string**: the UTF-16 name at local
player **`+0x1fb4`**, encoded with tranche 09's encoder `0x00e22a30` into a 192-byte stack buffer (so at most 63
characters survive). CONFIRMED (the 0-value return relies on EAX still holding the null player across the cookie check
— CONFIRMED in the listing).

---

## E. Machinima — 2 functions, sub-registrar `0x00bc68a0`

The machinima state dword is **`0x029443bc`**: 1 = recording, 2 = playing back, 3 = a third active state, 0 = idle
(CONFIRMED values from the writers and readers in the dumps; the names of 1 and 2 from E.1/E.2).

### E.1 `game_machinima_is_recording` (`0x00bc5860`)

**Arguments:** none. **Return:** exactly 1 boolean = (`0x029443bc` == 1). CONFIRMED.

### E.2 `game_machinima_playback_enter` (`0x00bc67d0`)

**Arguments:** 1 string. **Return:** 0 values.

**Body (CONFIRMED):**
1. arg 1 is read with `lua_tolstring` and **immediately compared, inline, with "machinima"** — no null check.
2. Equal → the clip path is the literal wide string **`L"srtt clip"`** (with a space, no folder, no extension) and the
   "literal path" flag is 1. Otherwise → arg 1 is decoded with **`0x00e22ac0`** into a 260-wide-character stack buffer,
   flag 0. So scripts are expected to pass a **UI-encoded** (tranche 09 `0x00e22a30`) clip name.
3. `0x00bc6350(name, flag)` (depth 1):
   - first `0x00bc6270` (follow-up dump) **resets all machinima state**: stops a recording (state 3 → `0x00bcc190`) or
     playback (`0x00bc6160`), clears `0x029443c7`, calls **`0x007f1070(0)`** (one HUD *show*, see F.1), posts the
     "MACHINIMA_RADIO_UNMUTE" audio event, frees the clip buffers `0x029443d0`/`0x029443d8`, sets the state to 0;
   - path: flag 1 → the name as is; flag 0 → `0x00bc5b70`, which builds
     **`<CSIDL Documents>\My Games\…\<name>.srtt_clip`** with `_swprintf_s(buffer, 0x104, "%s\%s.%s", …)` after
     creating the folders (`0x00bc5a90`) (follow-up dumps);
   - `0x00bc5230` opens the file — **path passed in EAX**, a hidden register (CONFIRMED in the listing at
     `0x00bc63b3`-`0x00bc63ba`) — reads a 12-byte header (magic **`0x564d4143`**, version **1**, a skip size), skips the
     optional block, reads the rest of the file into a heap buffer, parses it (`0x00dab0e0`) and loads the clip header
     fields (`0x00bc5170`, which among other things fills the 0x41-byte world name at **`0x02944360`**);
   - on failure, reset again and return. On success: post "MACHINIMA_RADIO_MUTE", require `0x0085d980(world name)`
     (follow-up dump: the world's `.vpp`/`.zpp_pc` archive set exists) or reset and return; then, by the game-play-mode
     stack top (`0x00706ab0` = `0x01503b50[0x012f4a80]`): **not 1 or 2** → build a load request for that world
     (`0x0084e390`, `0x0084e290`, `0x0084e360`) and adjust the mode stack (`0x00706fe0(3, 4)`); **1 or 2** → if 1, push
     mode 2 (`0x00706e00(2)`), then `0x007aebb0(world)` (copy the name to `0x02242558`, `0x0088ba10(4, 2)`,
     `0x007ae0a0(1)`, fade out to `0x007b4cb0`); finally state `0x029443bc` = **2** and `0x029443e0` = 1.

**Crash and logic shapes:**
- **Null read (CONFIRMED):** a missing, nil or table/boolean arg 1 makes `lua_tolstring` return null and the inline
  compare reads `[0]` at `0x00bc6810`.
- **Uninitialised path (CONFIRMED shape):** a string that is not "machinima" and does not start with byte `0x80`
  (e.g. a plain ASCII clip name) makes `0x00e22ac0` write nothing, so **an uninitialised 520-byte stack buffer** is used
  as the clip name. `0x00bc5b70` then reads it as a `%s` argument (no guaranteed terminator → read past the buffer) and
  `_swprintf_s` with a 260-character limit: an over-long result triggers the CRT invalid-parameter handler, which
  terminates the process in a default release CRT (HIGH CONFIDENCE for the CRT behaviour; the garbage contents are
  not predictable).
- **Clip loader (`0x00bc5230`, CONFIRMED in the listing):** the **file handle is leaked** when the magic or version is
  wrong (the early return at `0x00bc528a` skips `CloseHandle`); `ReadFile` results are never checked (a file shorter
  than 12 bytes compares uninitialised header bytes); the body size is "file size − current position", which
  **underflows** when the header's skip size points past the end, and the heap allocation (`0x005d79e0`) result is
  not checked before the read and the parse. Clips live in a user-writable folder, so these are reachable with a
  damaged or hostile `.srtt_clip` file.
- **"machinima" literal (HYPOTHESIS):** `L"srtt clip"` is a relative path with a space and no extension, unlike every
  real clip path (`.srtt_clip`); it will normally fail to open, so `game_machinima_playback_enter("machinima")`
  effectively only resets machinima state. Leftover debug path.

---

## F. HUD and state — 4 functions

### F.1 `game_hud_hide` (`0x00841d80`)

**Arguments:** 1 boolean, read unconditionally (**missing → false**). **Return:** 0 values.

**Body:** `0x007f1070(hide)` then `0x008020b0(0, hide)`. CONFIRMED.

`0x007f1070(hide)` (depth 1): returns at once if byte `0x022b792c` is set or bit `0x20000` of `0x014f71e8` is set.
Otherwise it is **reference-counted**: true increments the hide counter **`0x022b7930`**, false decrements it
**clamped at 0**. It also passes the value to `0x007efa30`, a second counter `0x022b7934` whose transition to/from 0
sets the Lua global **`Hud_has_focus`** (true when the counter is 0) through `0x00e0d340`. Only when "counter > 0"
changes (cached in `0x022b792d`) does it apply the new visibility: it sets visibility on the `safe_frame` element of the
main HUD document (`0x022b794c`) and of the documents "tutorial", "hud_diversion", "hud_qte", "hud_whored",
"Hud_touch_combo", "hud_running_man", "genki_run", "genki_fly", "genki_ball" and "genki_escort"; calls `0x008fa240` and
`0x007f0fc0` (two more HUD groups, follow-up dumps); and hides/restores "hud_btnmash". CONFIRMED.
`0x008020b0(0, hide)` refreshes the QTE prompt document "hud_qte" with the hide flag (depth 1; prompt-table details
not followed).

**Findings (CONFIRMED):** hide/show calls must be balanced — two hides need two shows; an extra show is absorbed by
the clamp; a call **without** an argument counts as a show. The machinima reset (E.2) also issues one show.

### F.2 `game_is_local_player_in_vehicle` (`0x00842040`)

**Arguments:** none. **Return:** exactly 1 boolean = `0x009b9160(local player)`: false for a null player, else
player `+0x16d4` == 3 — the same "in a vehicle" state test `character_is_in_vehicle` uses (spec-lua-api-behaviour
§3.10). CONFIRMED. Null-safe.

### F.3 `game_is_autosaving` (`0x008427b0`)

**Arguments:** none. **Return:** exactly 1 boolean = `0x00b94690()` = (byte `0x0290ceca` == 1 **and** byte
`0x0290cecb` set) **or** byte `0x0290ceda` set. CONFIRMED. These are exactly the second and third "busy" tests at the
top of the autosave routine `0x00b94ff0` (interp_tabs.md Q5.2; `0x0290ceda` is its "deferred autosave pending" flag).
The first test there (byte `0x0290cedc`) is **not** included, so this can report false while an autosave request
would still be refused. CONFIRMED by comparison of the two bodies.

### F.4 `game_difficulty_select` (`0x007e18a0`, sub-registrar `0x007e18e0`)

**Arguments:** 1 number, truncated. **Return:** 0 values. **Body:** if the value differs from the current difficulty
(`0x005f4aa0` → `0x012ece10`), call the setter `0x005f4a80`, which stores it only when it is below 3 as an unsigned
compare. CONFIRMED. **This settles tranche 11's OPEN item 6** ("Lua name of `0x007e18a0`"): it is
`game_difficulty_select`. A negative or ≥ 3 value is ignored silently.

---

## G. Gameplay — 2 functions (`0x00a20840`)

### G.1 `fov_check_xz_plane` (`0x00a4a490`)

**Arguments:** 1 string = viewer; 2 string = target; 3 optional number = angle in degrees (default **30.0**, the float
at `0x01173de0`).

**Return:** **target inside → 2 booleans (true, false); otherwise → 1 boolean (false).** CONFIRMED — the in-view path
pushes true, sets the result count to 1, and then falls into the common tail that pushes false and returns count + 1.

**Body (CONFIRMED):**
- Viewer: first `0x00a4a270` (by-name lookup on singleton `0x02442750`, class-row `+6` bit `0x80` — the vehicle bit,
  tranche 11), rejected if dead (`0x00853b10`); otherwise the generic chain `0x00a281a0`. Target: `0x00a281a0` only.
  Either missing → false.
- Both vectors are flattened onto the XZ plane by `0x00d9ffe0` (drop Y, normalize X/Z; length within 1e-7 of 0 gives
  (1, 0, 0)): the viewer's **`+0x64/+0x68/+0x6c`** row (HIGH CONFIDENCE the forward axis of the orientation matrix) and
  target `+0x40..+0x48` − viewer `+0x40..+0x48` (positions).
- In view when the dot product ≥ **cos(angle × k)**, with k the double at `0x012a30d8` = `0x3f91df3300000000` ≈
  0.0174530 (π/180 to five significant figures; ptrs run). `0x00ea3a70` is the CRT `cos` (its x87 path `0x00ea3ac8`
  executes `fcos` and names "cos" for the error handler; second follow-up dump). So arg 3 is a **half-angle** from the
  forward axis: the default gives a 60° cone.

**Findings:** the extra trailing false is harmless for `if fov_check_xz_plane(…) then` but wrong for any caller that
counts or forwards results (CONFIRMED logic defect). A non-number angle reads as 0 → cos 0 = 1 → only an exactly aligned
target passes. A target at the viewer's XZ position compares against the arbitrary (1, 0, 0). No crash shape.

### G.2 `flee_to_navpoint` (`0x00a4a310`)

**Arguments:** 1 string = fleeing character; 2 string = navpoint; 3 optional string = threat (default
**`"#PLAYER1#"`**; a nil slot is skipped correctly); 4 optional boolean (default false). **Return:** 0 values.

**Body (CONFIRMED):** character via `0x00a28150` (follower sentinels / character resolver / virtual narrowing — **not**
`0x00a281a0`, so `"#PLAYER#"` is not accepted for arg 1); navpoint via `0x005982e0` (class-row `+0xb` bit `0x02`)
with a liveness check; threat via `0x00a281a0`. All three must resolve. Then `0x004f5dd0` (front matter) queues AI
event **`0x30`** on the character itself (source = recipient = its handle, delay 0, bytes `0x2d` and `0xb2`) with a
32-byte payload: threat handle (8 bytes), navpoint position x, y, z (navpoint `+0x40..+0x48`), a mode dword
**`0x102`, or `0x103` when arg 4 is true**, and 8 zero bytes.

**Findings (CONFIRMED):** the order is **silently dropped** when the event pool `0x01361998` is empty, when the
character is dead, or when it is a player-class object (class-row `+0xa` bit `0x02`). No crash shape (every resolver
result is checked). Meaning of arg 4 / mode bit 0 is OPEN.

---

## G2. Method notes for the follow-up dumps

Facts from the follow-up dumps that the sections rely on but do not spell out (all CONFIRMED):
- `0x006e6690(c)` writes `0x012f3b30` only for 0 ≤ c < `0x014f4418`; `0x006e6650(map, char, flag)` writes
  `0x012f3b30`, `0x012f3b28` and `0x014f3d30` only when both indices are in range.
- `0x007ab570` is also called by `0x007abe40` and two undefined call sites (`0x007ab5d9`, `0x007ab609`); the lobby
  start flow is shared.
- `0x0059f8c0(delay, callback, flag)` is the screen-fade driver: if already faded (states `0x012e6aa4`/`0x012e6aa8` both
  3) it calls the callback at once; otherwise it starts the Lua `screen_fade_do` coroutine when that global exists and
  stores the callback in `0x013effcc` (or `0x013effd0` when the fade state is 0).
- `0x00706e00(mode)` pushes a game-play mode onto a 16-entry request queue (`0x012f4a88`, count `0x012f4a90`, capacity
  `0x012f4a8c` = 16) unless it is already on the current stack; `0x00706f40(n)` queues pops down to depth n; a full
  queue drops the request silently.
- `0x00e22ac0`, `0x00db0510`, `0x00e0ceb0`, `0x00e0d340`, `0x004f5dd0`, `0x00853b30` — as in the front matter.

---

## H. Cross-function observations

1. **Two new UI sub-registrars:** the lobby table `0x007abbb0` (9 names, UI registrar call at `0x00843144`) and the
   machinima table `0x00bc68a0` (7 names, call at `0x00843216`). A spec registrar table should add both under
   `0x008430f0`.
2. **Eight of the 25 names are stubs.** Four return a constant false (`0x00a3c670`) and two return nothing
   (`0x007c9f50`); with the aliases in section A, `0x00a3c670` alone answers ≥ 9 Lua names. A binding layer can
   implement these as constants.
3. **Host / client asymmetry:** `game_set_coop_join_type` is ignored on clients; `game_lobby_update_char_selection`
   writes different globals on clients (applied at once) and on the host (applied at start); the kick dialog is shown
   on clients but cannot kick. CONFIRMED.
4. **`0x004f5dd0` is the per-character AI event queue.** `flee_to_navpoint` (event `0x30`) and spec §15.4's
   `set_always_cower_flag` (event `0x26`) both use it. §15.4's "+8/+0xc position pair" and "VFX cue" readings should
   be revisited: the pair is passed where a 64-bit object handle belongs, and the callee resolves it through the handle
   table and appends an event record to object `+0x1c50`. HIGH CONFIDENCE.
5. **Fixed-limit wide formatting:** `0x00db0510` always tells `_vsnwprintf` the buffer holds 1024 wide characters. In
   this tranche all three callers (C.2 twice, plus `game_kick_coop_player`) pass 256-character buffers; tranche 09
   B.12's `0x007c3e60` is another caller. Every caller of `0x00db0510` with a buffer smaller than 2048 bytes is a latent
   stack overflow; a whole-binary audit of its callers (many, per its reference list) is a candidate follow-up (OPEN).
6. **UI string encoding is two-way and fails silently.** Strings going to Lua are encoded by `0x00e22a30` (D.4) and
   strings coming from Lua are decoded by `0x00e22ac0` (E.2). A plain string fed to the decoder leaves its output
   untouched — callers must pre-initialise (E.2 does not).
7. **Lua callbacks are coroutines (tranche 09) and the arity is not fixed:** `game_coop_kick_player` calls its
   callback with 0 or 1 arguments, or not at all, depending on the path; the lobby start dialog calls the fixed global
   `game_lobby_start_dialog_canceled` on cancel. Each callback copies the caller's thread-record `+0x14` taken from
   `0x00e0ceb0` or a saved global.
8. **Reference-counted HUD hiding:** `game_hud_hide` maintains a counter (`0x022b7930`) and a focus counter
   (`0x022b7934`) that drives the Lua global `Hud_has_focus`; the machinima reset path performs one implicit show.
9. **Hidden-register conventions:** `0x00bc5230` takes its path in EAX (new); `0x0087e0e0`, `0x0087c8b0`,
   `0x0087c250`, `0x0087e400` and `0x00703210`'s callees take the session in ECX; `0x00a4a270`/`0x005982e0` take the
   singleton in ECX.
10. **Constant-1 debug stubs** appear in the call paths of seven handlers (C.1, C.2, C.3, C.6, D.1, D.2, D.3) and
    never affect behaviour.

## I. OPEN

1. Whether `SaintsRowTheThird_DX11.exe` binds `game_is_pc_dx11` to a "true" handler.
2. Meaning of join types 0/1/2 (C.1) and of session fields `+0xf4`, `+0xf8`, `+0xfd`, `+0xfe`, `+0x4c`, `+0x4e` bit
   `0x10`, `+0x224` (C.4, D.3).
3. `0x00877670` (the two reserved-slot checks in the free-slot count) — not dumped.
4. Whether horde can actually start with the committed map still -1 (D.3 out-of-range path), and the meaning of the
   6-bit progress value (last wave?) and of the map record byte `+0x104`.
5. What `0x022423b0` triggers in the lobby update loop (`0x007ab410`, `0x007abe40`), and the meaning of game state
   `0x3e` (`0x007b47f0`).
6. Machinima: meaning of state 3, of `0x029443e0`, and of game-play modes 1, 2, 3, 4 (`0x00706ab0` stack); the clip
   format beyond the 12-byte header (`0x00dab0e0`, `0x00bc5170`, `0x00bcbf60`).
7. Arg 4 of `flee_to_navpoint` (mode `0x102` vs `0x103`) and the two literal bytes `0x2d`/`0xb2`; class-row `+0xa` bit
   `0x01` (the AI-event recipient requirement).
8. Audit of all `0x00db0510` callers' buffer sizes (H.5).
9. `game_is_connected_to_network` (`0x008427e0`) and `game_get_game_play_mode` (`0x00841f70`) — siblings, not dumped.
10. The prompt-table logic of `0x008020b0` (QTE HUD refresh) beyond its hide flag.

## J. Direct-answer table

| # | name | registrar | handler | status |
|---|---|---|---|---|
| 1 | `game_show_party_ui` | `0x00845aa0` | `0x007c9f50` | resolved — no-op stub, 0 values (B.2) |
| 2 | `game_show_coop_partner_gamercard` | `0x00845aa0` | `0x008425a0` | resolved — Steam overlay to remote player's Steam ID (+0x142); slot identity HIGH CONFIDENCE (C.3) |
| 3 | `game_show_community_sessions_ui` | `0x00845aa0` | `0x007c9f50` | resolved — no-op stub (B.2) |
| 4 | `game_set_coop_join_type` | `0x00845aa0` | `0x00844440` | resolved — 0/1/else→2, host-only lobby setting, rate-limited publish (C.1) |
| 5 | `game_record_mode_is_supported` | `0x007e18e0` | `0x00a3c670` | resolved — always false (B.1) |
| 6 | `game_record_mode_is_active` | `0x007e18e0` | `0x00a3c670` | resolved — always false (B.1) |
| 7 | `game_machinima_playback_enter` | `0x00bc68a0` | `0x00bc67d0` | resolved — **null read on non-string arg; uninitialised path on plain string**; clip loader leaks handle (E.2) |
| 8 | `game_machinima_is_recording` | `0x00bc68a0` | `0x00bc5860` | resolved — state `0x029443bc` == 1 (E.1) |
| 9 | `game_lobby_update_map_selection` | `0x007abbb0` | `0x007aba90` | resolved — unchecked store to `0x022423a4` (D.1) |
| 10 | `game_lobby_update_char_selection` | `0x007abbb0` | `0x007abac0` | resolved — client applies at once, host defers (D.2) |
| 11 | `game_lobby_set_horde_mode_data` | `0x007abbb0` | `0x007ab8d0` | resolved — progress lookup, restart warning dialog, start + commit (D.3) |
| 12 | `game_lobby_get_local_player_name` | `0x007abbb0` | `0x007ab030` | resolved — 0 or 1 value; encoded name from player `+0x1fb4` (D.4) |
| 13 | `game_join_syslink_game` | `0x00845aa0` | `0x00844610` | resolved — 1-based, 8-bit index into the find-server ring; silent miss (C.6) |
| 14 | `game_is_pc_dx11` | `0x00845aa0` | `0x00a3c670` | resolved — always false in the DX9 executable (B.1) |
| 15 | `game_is_local_player_in_vehicle` | `0x00845aa0` | `0x00842040` | resolved — player `+0x16d4` == 3, null-safe (F.2) |
| 16 | `game_is_joinable` | `0x00845aa0` | `0x00842510` | resolved — free slots ≠ 0 plus session-state tests (C.4) |
| 17 | `game_is_connected_to_internet` | `0x00845aa0` | `0x008428e0` | resolved — Steam logged on; alias `game_is_connected_to_service` (C.5) |
| 18 | `game_is_autosaving` | `0x00845aa0` | `0x008427b0` | resolved — two of the autosave routine's three busy tests (F.3) |
| 19 | `game_hud_hide` | `0x00845aa0` | `0x00841d80` | resolved — reference-counted hide of ~11 HUD documents; missing arg = show (F.1) |
| 20 | `game_get_ps3_button_swap` | `0x00845aa0` | `0x00a3c670` | resolved — always false (B.1) |
| 21 | `game_difficulty_select` | `0x007e18e0` | `0x007e18a0` | resolved — difficulty setter, 0..2 (F.4; closes tranche 11 OPEN 6) |
| 22 | `game_coop_kick_player` | `0x00845aa0` | `0x00844250` | resolved — confirm dialog + callback; **256-char buffers formatted with a 1024 limit** (C.2) |
| 23 | `game_coop_get_starting_syslink` | `0x00845aa0` | `0x008426b0` | resolved — byte `0x014ff6c2` (C.7) |
| 24 | `fov_check_xz_plane` | `0x00a20840` | `0x00a4a490` | resolved — XZ cone test, half-angle in degrees (default 30); **returns 2 values when true** (G.1) |
| 25 | `flee_to_navpoint` | `0x00a20840` | `0x00a4a310` | resolved — queues AI event `0x30` with navpoint position and threat handle (G.2) |

## K. Crash-shaped and logic defects (summary)

| # | where | defect | status |
|---|---|---|---|
| 1 | E.2 `0x00bc6810` | `lua_tolstring` result compared inline with "machinima" without a null check → null read for a missing/non-string arg | CONFIRMED |
| 2 | E.2 `0x00bc686c` → `0x00bc5b70` | decoder `0x00e22ac0` writes nothing for a plain string → uninitialised 520-byte stack buffer used as `%s` in `_swprintf_s` (unterminated read; CRT invalid-parameter termination on overflow) | CONFIRMED shape; consequence HIGH CONFIDENCE |
| 3 | E.2 `0x00bc5230` (`0x00bc528a`) | file handle leaked on a bad header | CONFIRMED |
| 4 | E.2 `0x00bc5230` | `ReadFile` results unchecked; size underflow from the header skip field; allocation result unchecked before read/parse (user-writable clip files) | CONFIRMED shapes |
| 5 | C.2 `0x008442e7`, `0x00843560` (and sibling `0x00844160`) | `0x00db0510` formats with a 1024-wide-char limit into 256-wide-char stack buffers | CONFIRMED latent; data-gated (HYPOTHESIS: unreachable with shipped strings and real names) |
| 6 | C.3 `0x00870810` | `SteamFriends()` result used without a null check (tranche 11's sibling checks it) | CONFIRMED shape; needs an uninitialised Steam API |
| 7 | G.1 `0x00a4a634`→`0x00a4a64a` | returns (true, false) when in view, (false) otherwise | CONFIRMED |
| 8 | C.2 / `0x00843560` | callback called with 0 args, 1 arg, or never (cancel); clients get the dialog and a "true" callback without a kick | CONFIRMED |
| 9 | D.3 | out-of-range map index → spurious restart warning; start proceeds while the map commit is refused (map stays -1 on a fresh boot) | CONFIRMED; downstream effect OPEN |
| 10 | D.1 | map index stored without bounds check (consumers check) | CONFIRMED; benign |
| 11 | C.6 `0x008728d0` | server index compared as 8 bits; unknown index silently ignored | CONFIRMED |
| 12 | C.1 | any value other than 0/1 becomes join type 2; client calls ignored | CONFIRMED |
| 13 | C.4 `0x0087c8b0` | free-slot test is "≠ 0", so a negative count passes | CONFIRMED arithmetic; reachability OPEN |
| 14 | F.1 | hide counter clamps at 0 (unbalanced shows absorbed); missing arg counts as a show | CONFIRMED |
| 15 | F.3 | omits the `0x0290cedc` test that the autosave routine applies | CONFIRMED |
| 16 | G.2 / `0x004f5dd0` | flee order silently dropped when the event pool is empty, the NPC is dead, or it is player-class; arg 1 cannot be `#PLAYER#` | CONFIRMED |
| 17 | E.2 | `"machinima"` maps to the relative path `L"srtt clip"` (no folder, no extension) | CONFIRMED; intent HYPOTHESIS (debug leftover) |

## L. Clean-room check

- Addresses are plain hex throughout; no Ghidra auto-names for functions, globals or labels, no decompiler variable
  names, no pasted pseudocode. Library routines are named only where Ghidra identified them as standard C runtime
  (`_vsnwprintf`, `_swprintf_s`, `strncpy`, `wcsncpy`, `cos`) or as imported public APIs (`SteamFriends`, `SteamUser`,
  `CreateFileW`, `ReadFile`, `CloseHandle`), and Steamworks method names are given only as HIGH CONFIDENCE
  identifications with their stated basis.
- Final self-check run on this file with the pattern
  `\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4}|LAB_[0-9a-fA-F]+|FUN_[0-9a-fA-F]+|DAT_[0-9a-fA-F]+)\b`:
  **0 hits** (positive control: the same pattern finds 280 hits in one raw dump file of this tranche, so the pattern
  is live).
- No spec file was edited. The private Ghidra copy `tools\gp_t12` was deleted after the dumps; raw dumps stayed in the
  session scratchpad.
