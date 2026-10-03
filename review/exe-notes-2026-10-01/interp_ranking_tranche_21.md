# Ranking tranche 21 — 25 previously-unspecced Lua-bound names (Team A, 2026-10-03)

Job definition: `D:\Project Crreish\TEAM A\ghidra\jobs\ranking\tranche-21.json`. The job file's own title states **names
451-475 of the 554 unspecced names, Team B call-count order**; this note uses that range and exactly the 25 names the job
lists. Run locally, read-only, on a private copy of the Ghidra project (`tools\gp_t21`, deleted afterwards), with the
job's own arguments: `CrreishDump.java <out>/lua lua depth:1 maxfuncs:15 maxinsn:500 <25 names>`. **All 25 names
resolved.** Every name string occurs exactly once in `.rdata`. For 24 names the handler is the code pointer stored in the
slot right after the name (insn offset +1) — CONFIRMED — dump `index.txt`. **One name, `game_proto_select`, is registered
by a pointer-before-name one-name registrar** (`0x00841020`): the `lua`-mode dump landed on the `lua_setfield` primitive
`0x00dfe830`, and the range run of the registrar shows the real handler `0x00840fa0` pushed before the name (section E),
exactly the quirk tranches 13/16/18/19 describe and the registrar tranche 19 G.4 / J.6 already found. The two other
`lua`-mode "uses" that landed on `0x00dfe830` (`gang_customization_confirm_vehicle_1.txt`,
`game_machinima_record_1.txt`) are the table registrars' loops reading their **first** row, as tranche 18 section A
describes — not second handlers.

Follow-up runs on the same private copy, cited by name below (full lists in section H):

- "follow-up dump": depth-0 `func` run on 49 callees;
- "ptrs run": `ptrs count:24` on `0x0115d290` (pause-map filter table) and `0x01311db0` (machinima folder names);
- "range run": the one-name registrar `0x00841020`, `game_proto_select`'s body, `0x007aae00-0x007aae40`,
  `0x00842100-0x00842130`;
- "xref runs": three `xref` passes over the gang-customisation globals, the terminate latch, the units global and two
  lobby bytes (section H);
- "second range run": `0x0083a850-0x0083a900` and `0x00839210-0x00839290` (the remaining references to the
  gang-customisation globals around `0x02303bd0`).

Call counts are Team B's (`D:\Crreish-sync\for-team-b\team-b\tools\lua_reconciliation_called_and_registered_1181.tsv`,
rows 1074-1098): **every name in this tranche is 1 call site in 1 script**, and Team B tags all 25 `ui`. Section A shows
the registrars agree: all 25 live in registrars reached from the UI bring-up `0x008430f0` or in the 113-row `game_`
helper table; none is in the gameplay registrar.

**Already partly covered elsewhere (cited, re-verified, not re-derived):**
- tranche 20 section A printed the gang-customisation registrar `0x0083a900` (all 8 rows) and, in I.1 crash 1, flagged
  that `gang_customization_revert_vehicle` **writes** and `…_confirm_vehicle` **reads** the 3-slot array `0x023013f0`
  with no bound. Re-confirmed here (B.4/B.5); this note adds the rest of both bodies and two new crash shapes.
- tranche 17's table of the pause-options registrar `0x007e18e0` already lists the four `game_record_*` handlers of this
  tranche (`0x00a3c670` once, the no-op `0x007c9f50` three times). Re-confirmed (section C).
- tranche 19 J.6 described `game_proto_select` → `0x00840fa0` as an out-of-tranche neighbour with a saved-register byte
  write. Re-read in full here (section E); the description stands, with one refinement.
- tranche 12 section A listed the lobby (`0x007abbb0`) and machinima (`0x00bc68a0`) sub-registrars, including the
  handler addresses of the five machinima names and the existence of the three lobby names of this tranche; tranche 12
  C.2 named `game_kick_coop_player` → `0x00844160` as a sibling with the same 256-character buffer pattern.

Labels: **CONFIRMED** = read in the listing of a dump made for this note; **HIGH CONFIDENCE** = follows from a dumped call
or reference, but the callee body was not dumped or a meaning is inferred from strong usage; **HYPOTHESIS** = plausible,
not settled; **OPEN** = not settled (collected in section J).

**Shared conventions, cited not re-described:** every entry opens with the `lua_gettop` prologue (`0x00dfde50`) and
reads arguments with negative stack indices counted from the bottom (`-N` = arg 1, `1-N` = arg 2, …). Primitives as in
the tranche 09/13/16/18/20 front matter: `0x00dfe210` `lua_tolstring` (non-string reads as null; a number is converted),
`0x00dfe1e0` `lua_toboolean`, `0x00dfe040` `lua_type` (0 = nil), `0x00dfe160` `lua_tonumber` (non-number reads as 0),
`0x00ea2596` truncating float-to-int, `0x00dfe590` `lua_pushboolean`, `0x00dfe3a0` `lua_pushnumber`, `0x00dfe420` push C
string (null pushes nil). "Nil-gated optional argument" = the tranche 13 idiom (count > 0 and `lua_type` ≠ 0); an
**unconditional** read has no presence check (missing boolean = false, missing number = 0, missing string = null).
Session getter `0x0087ba20` (global `0x024d8534`; "host" = session `+0x5c` == `+0x58`, tranche 09 B.12); first remote
player `0x008b7100`; CRC name hash `0x00d9e8b0` (null → 0, tranche 18 I.5); the Lua-callback coroutine mechanism
`0x00e0ca80` → `0x00e0c720`, dispatched by `0x00e0cd00` with the record's `+0x14` set to a document handle (tranche 09
front matter, tranche 20 front matter); the shared no-op stub `0x007c9f50` (§6.1/§24.2/§27.17) and the constant-false
pusher `0x00a3c670` (tranche 12 B.1); the UI-encoded wide-string **encoder** `0x00e22a30` and **decoder** `0x00e22ac0`
(tranche 09 / tranche 12 "shared machinery": the decoder writes **nothing** unless the input starts with byte `0x80`);
the wide `sprintf` wrapper `0x00db0510` (character limit always 1024, tranche 12); the UI element-handle resolver
`0x00e25890` (null for a stale handle, tranche 20 I.1) and the property setter `0x00e26140` → `0x00e26040`, which uses the
element as `this` without a check (tranche 20 I.1 crash 3). Constant-1 stubs whose results are discarded
(`0x00d34d60`, `0x00d34cd0`, `0x0101b460`, `0x00d21330`, `0x00d21720`) are the tranche 12 / 19 dead-stub family and are
omitted from the bodies below. Steam: `0x0101c570` is the import slot Ghidra resolves as `SteamFriends`; vtable slots
`+0x4c`/`+0x50` were identified in tranches 11 C.5 / 12 C.3.

---

## A. Registrars — where the 25 names live

Six registrars. CONFIRMED — `index.txt`, range run:

| registrar | reached from | names in this tranche |
|---|---|---|
| gang customisation `0x0083a900` (8 rows, tranche 20 A) | UI `0x008430f0`, call at `0x0084314a` | 5: `gang_customization_select_gang_sign`, `…_revert_vehicle`, `…_revert_gang_sign`, `…_confirm_vehicle`, `…_confirm_gang_sign` |
| `game_` helper `0x00845aa0` (113 rows; `spec-lua-bindings.md` §13.5) | `0x008489e0` (tranche 11/16) | 7: `game_use_imperial_units`, `game_steam_open_url`, `game_steam_open_dlc_page_overlay`, `game_request_game_terminate`, `game_pause_map_filter`, `game_kick_coop_player`, `game_is_waiting_for_partner` |
| pause options `0x007e18e0` (tranche 10/17) | UI `0x008430f0`, call at `0x0084317d` | 4: `game_record_set_quality_level`, `game_record_mode_show_hud`, `game_record_mode_enable`, `game_record_mode_can_encode` |
| machinima `0x00bc68a0` (7 rows, tranche 12 A) | UI `0x008430f0`, call at `0x00843216` | 5: `game_machinima_record`, `…_overwrite_clip`, `…_delete_clip`, `…_copy_clip`, `…_clip_exists` |
| lobby `0x007abbb0` (9 rows, tranche 12 A) | UI `0x008430f0`, call at `0x00843144` | 3: `game_lobby_update_ready_state`, `game_lobby_horde_continue_from_wave`, `game_lobby_finished_loading` |
| one-name registrar `0x00841020` (pointer-before-name) | UI `0x008430f0`, call at `0x0084316b` (tranche 19) | 1: `game_proto_select` |

- **`0x00845aa0` rows** (CONFIRMED — `index.txt`, cross-checked against the on-disk table `tools/lua_game845aa0_full.txt`
  pairs 18, 38, 39, 48, 59, 61, 91): `game_use_imperial_units` → `0x00841d50`, `game_steam_open_dlc_page_overlay` →
  `0x00842130`, `game_steam_open_url` → `0x008421c0`, `game_pause_map_filter` → `0x00842430`, `game_kick_coop_player` →
  `0x00844160`, `game_is_waiting_for_partner` → `0x008424e0`, `game_request_game_terminate` → `0x00842730`. The row
  before `…_dlc_page_overlay` (`game_steam_open_store_overlay`, pair 37) holds `0x00842100`, not in this tranche.
- **`0x007e18e0` rows** (CONFIRMED — `index.txt`): `game_record_mode_can_encode` name at `0x007e1978`, handler
  `0x00a3c670` at `0x007e1980`; `game_record_mode_enable` `0x007e1988` / **`0x007c9f50`** at `0x007e1990`;
  `game_record_mode_show_hud` `0x007e19b8` / **`0x007c9f50`** at `0x007e19c0`; `game_record_set_quality_level`
  `0x007e19c8` / **`0x007c9f50`** at `0x007e19d0` — matching tranche 17's table.
- **`0x00bc68a0` rows** (CONFIRMED — `index.txt`, matching tranche 12): `game_machinima_record` → `0x00bc5840`,
  `…_clip_exists` → `0x00bc5c80`, `…_copy_clip` → `0x00bc5d20`, `…_overwrite_clip` → `0x00bc5e60`, `…_delete_clip` →
  `0x00bc5fa0`.
- **`0x007abbb0` rows** (CONFIRMED — `index.txt`): `game_lobby_horde_continue_from_wave` → `0x007aba20`,
  `game_lobby_update_ready_state` → `0x007abb20`, `game_lobby_finished_loading` → `0x007ab150`.
- **`0x0083a900` rows** (CONFIRMED — `index.txt`, matching tranche 20 A): `…_confirm_vehicle` → `0x00839ad0`,
  `…_revert_vehicle` → `0x0083a790`, `…_select_gang_sign` → `0x00839b00`, `…_confirm_gang_sign` → `0x00839ba0`,
  `…_revert_gang_sign` → `0x00838e50`.
- **`0x00841020`** (range run, CONFIRMED): pushes 0, then the code pointer `0x00840fa0`, then `L`, calls
  `lua_pushcclosure` `0x00dfe4f0`; then pushes the name `"game_proto_select"` (`0x01161be0`), `-10002` (globals) and `L`
  and calls `lua_setfield` `0x00dfe830`. The handler is `0x00840fa0`.

---

## B. Gang customisation — 5 functions (registrar `0x0083a900`)

**Gang-customisation state used below** (all zero-fill unless stated; CONFIRMED from the follow-up dumps of the
initialiser `0x00839cc0`, the save writer `0x00839e40`, the sign-list callback `0x00839920` and the screen set-up
`0x0083a9d0`):
- `0x02301400` — base of the **gang-sign records**, 8 bytes each (a localisation key dword at `+0`, a sign/decal id at
  `+4`); `0x02301406` — the number of signs offered (a byte);
- `0x02300bc8` — a dword table mapping **visible list position → record index**, filled by `0x00839cc0` for positions
  0 … count−1 only (the next global referenced after it is `0x023013cc`, 513 dwords further on; its real size is not
  declared anywhere read here);
- `0x023013ec` — **pointer to the current gang-sign record**; initialised by `0x00839cc0` to base + 8 × the saved byte
  `0x0230140b` (no check against the count); saved by `0x00839e40` as the byte (pointer − base) ÷ 8;
- `0x02303bec` — copy of `0x023013ec` taken by the sign-list callback `0x00839920` when the list is built (the revert
  target);
- `0x01300e0c` (file value −1) — the **sign id being previewed**; the per-frame preview `0x00810d40` (called from
  `0x008119a0`) applies it with `0x00959b50(…, id, 0x1d9f, 0x40, …)` whenever it changes and is not −1 (HYPOTHESIS: a
  decal preview on the tag wall seen by the `"underground_$gang_cust_tag_cam"` camera that `0x0083a9d0` looks up);
- `0x023013f0` — the 3-slot **gang-vehicle array** of tranche 20 I.1 (catalogue item pointers; initialised by
  `0x00839cc0` from the three saved id bytes `0x02301408`-`0x0230140a` through `0x00ac1840`; saved by `0x00839e40` through
  `0x00ac1ae0`); `0x02303be8` — the **vehicle revert baseline**, set by `gang_customization_show_vehicle` (`0x0083a550`,
  outside the tranche: slot item when the slot is first shown), by `…_confirm_vehicle` (B.4) and by `0x00810b60`.

### B.1 `gang_customization_select_gang_sign` → `0x00839b00`

**Arguments:** 1 number = mode (unconditional, truncated); 2 number = list position (unconditional, truncated).
**Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing):**
- **mode 0** — preview: `0x01300e0c` = the id (`+4`) of record `0x02300bc8[position]`, i.e. reads
  `[0x02301400] + 8 × 0x02300bc8[position] + 4`. Nothing else.
- **mode 1** — reads the pointer at **`0x02303be0`**, calls its virtual `+0x70` (no arguments) to obtain an object, and,
  if that object's dword `+0x1c70` (`0x00941c50`) is non-zero, calls `0x0070dd10(object, 0x27bcc435, 0)` — the same object
  is then passed to `0x0070d9b0` (which, among other checks, requires the object to be the local player `0x009da4e0` in
  some states and indexes a 110-entry table `0x01504088` with `+0x1c70`) and, when `0x008add90(object)` is 1, to
  `0x0070cd40`. HYPOTHESIS: "play the tag-spraying action (hash `0x27bcc435`) on the preview character".
- any other mode: nothing.

**Crash 1 — mode 1 always dereferences a null pointer. CONFIRMED structure (`0x00839b44`-`0x00839b4a`), static
negative for any other writer.** The only instruction anywhere that writes `0x02303be0` is the screen set-up
`0x0083a9d0`, which **zeroes** it (`MOVQ` of a cleared register over `0x02303be0`-`0x02303be7` at `0x0083ac30`, together
with `0x02303bd8`). Xref runs over `0x02303be0`, `0x02303be4` and every dword from `0x02303b80` to `0x02303bdc` (raw
immediate scan plus the reference manager) found no other writer and no block copy based anywhere in that span (the
remaining references are reads, single-dword writes of other fields, or handle arguments to the UI helpers, read in the
second range run). The binding loads `[0x02303be0]` = 0 and immediately reads the vtable through it — an access
violation at address 0 on every mode-1 call made while the screen is set up (and also before, the global being zero-fill).
Caveat, per `WALLS.md`'s runtime-populated-table entry: a write through a computed pointer cannot be excluded
statically. HYPOTHESIS: mode 1 is an unfinished or debug path; whether the shipped script ever passes mode 1 is OPEN
(Team B can check its call site). Fix: reject mode 1 (or null-check `0x02303be0` and the returned object; the returned
object's `+0x1c70` read is unchecked too).

**Crash 2 — mode 0 position is unbounded. CONFIRMED structure (`0x00839b7c`-`0x00839b89`).** The position indexes
`0x02300bc8` with no check against the count `0x02301406` or the table size; any value reads a dword from
`0x02300bc8 + 4 × position` and then a second dword at `base + 8 × that value + 4` — a script-controlled double
arbitrary **read** (a page fault for most out-of-range values). Positions from the count up to 512 read zero-fill or stale
table entries (entry 0's record, harmless). If called before the sign records are loaded (`0x02301400` = 0) the read is
near address 0 (reachability HYPOTHESIS). Fix: require 0 ≤ position < byte `0x02301406`.

### B.2 `gang_customization_confirm_gang_sign` → `0x00839ba0`

**Arguments:** 1 number = list position (unconditional, truncated). **Return:** 0 values. CONFIRMED — listing.

**Body:** current sign `0x023013ec` = `[0x02301400] + 8 × 0x02300bc8[position]`; preview `0x01300e0c` = −1. CONFIRMED.

**Logic defect / crash shape — unbounded position, persisted. CONFIRMED structure.** Same unchecked index as B.1, but
here the **computed record pointer is stored** rather than read. It is dereferenced later by the sign-list callback
(`0x00839920` reads `[0x023013ec] + 4` with no check, `0x008399a1`-`0x008399a7`) and **written to the save** by
`0x00839e40` as the byte ((pointer − base) ÷ 8) truncated to 8 bits (`0x00839eb7`-`0x00839ec9`); on the next load
`0x00839cc0` rebuilds base + 8 × byte with **no check against the count** (`0x00839cf7`-`0x00839d09`). So an out-of-range
position (a) crashes or reads garbage on the next list build, and (b) can persist into the save as an index past the
real sign table, which the loader then trusts (CONFIRMED structure; whether consumers beyond the two read here tolerate
a past-the-end record is OPEN). Fix: as B.1.

### B.3 `gang_customization_revert_gang_sign` → `0x00838e50`

**Arguments:** none read. **Return:** 0 values. **Body:** current sign `0x023013ec` = the copy `0x02303bec` taken when
the sign list was last built; preview `0x01300e0c` = −1. CONFIRMED — listing.

**Edge — CONFIRMED structure, reachability HYPOTHESIS:** `0x02303bec` is written only by `0x00839920`, and only when the
list's UI element exists (`0x00e1e8c0(0x022cd9c8)` non-null). A revert before the list has ever been built sets the
current sign to **null**; the next list build then reads `[0 + 4]` (`0x008399a1`) and the next save stores
(0 − base) ÷ 8 truncated (garbage). The function has no Ghidra boundary in the shared project (xref lists its
instructions as "no function"); the `lua`-mode dump read it to its `RET`.

### B.4 `gang_customization_confirm_vehicle` → `0x00839ad0`

**Arguments:** 1 number = slot (unconditional, truncated). **Return:** 0 values. **Body:** the revert baseline
`0x02303be8` = `0x023013f0[slot]`. Nothing else (no preview, no save). CONFIRMED — listing.

**Crash shape — CONFIRMED, already tranche 20 I.1 crash 1 (`0x00839aec`):** unbounded slot → arbitrary dword read; the
value read becomes the baseline that B.5 later writes back into the array and hands to the preview spawner as an item
pointer. Fix: slots 0-2 only.

### B.5 `gang_customization_revert_vehicle` → `0x0083a790`

**Arguments:** 1 number = slot (unconditional, truncated). **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing):** if `0x023013f0[slot]` already equals the baseline `0x02303be8`, nothing. Otherwise:
`0x023013f0[slot]` = baseline (`0x0083a7e2`); `0x0083a3c0(baseline)` respawns the preview vehicle (tranche 20 I.1: release
the previous preview with `0x0083a300`, request the item's types under `"gang_cust"`, create the vehicle); then the
stored UI handle `0x022cd9cc` is resolved (`0x00e25890`) and one property `{type 5, false}` set on it (`0x00e26140`);
byte `0x013007a8` = 0.

**Crash 1 — unbounded slot write. CONFIRMED, already tranche 20 I.1 crash 1 (`0x0083a7d9` read, `0x0083a7e2` write).**

**Crash 2 — null baseline reaches a null-unsafe callee. NEW. CONFIRMED structure (`0x0083a7e2` → `0x0083a3c0` →
`0x00a95d60` at `0x00a95d81`), reachability HYPOTHESIS.** The baseline `0x02303be8` is zero-fill until
`gang_customization_show_vehicle` or B.4 runs. If `revert_vehicle` is called first for a slot that holds an item, the
slot is overwritten with **null** and `0x0083a3c0(null)` runs: `0x00ac27a0(null)` only hashes the pointer as a name string
(the hash is null-safe; what `0x00ac2560` then does with hash 0 was not read), but `0x00a95d60(null, "saints")` reads the item's `+0x24` count and `+0x28`
array **without a null check** (it only checks the second argument) — a read at address `0x24`. The same callee is
reached with a null item by `…_show_vehicle` (`0x0083a5fd` → `0x0083a60b`) when a slot was initialised to null because
`0x00ac1840` failed for a saved id (HYPOTHESIS for reachability). Fix: skip the respawn when the item is null, or have
`0x00a95d60` check its first argument.

**Crash 3 — CONFIRMED shape, reachability OPEN:** the UI element from `0x00e25890` is not checked before the property
call (`0x0083a7f6`-`0x0083a81c`), the same shape as tranche 20 I.1 crash 3.

**Edge (CONFIRMED):** the respawn and the UI property update only run when the slot actually changes; reverting a slot
that already matches does nothing, even if the preview currently shows something else.

---

## C. Video-recording options — 4 functions (registrar `0x007e18e0`)

### C.1 `game_record_set_quality_level`, C.2 `game_record_mode_show_hud`, C.3 `game_record_mode_enable` → `0x007c9f50`

**Arguments:** none read. **Return:** 0 values. **Body:** the shared no-op stub — `lua_gettop` only. CONFIRMED —
listing. On this PC build the three setters do nothing (no argument is read, so wrong-typed arguments are harmless).

### C.4 `game_record_mode_can_encode` → `0x00a3c670`

**Arguments:** none read. **Return:** exactly 1 boolean, always **false**. CONFIRMED — listing (`0x00a3c67b`-`0x00a3c67e`
pushes literal 0).

Together with tranche 12 B.1 (`game_record_mode_is_supported` / `…_is_active` → the same constant-false handler) **all
six `game_record_*` names of `0x007e18e0` are inert on PC**: three report false, three do nothing. HYPOTHESIS: console
video-capture features compiled out of the PC build (the DX9/DX11 question is tranche 12's OPEN).

---

## D. `game_` helpers — 5 functions (registrar `0x00845aa0`)

### D.1 `game_use_imperial_units` → `0x00841d50`

**Arguments:** none read. **Return:** exactly 1 boolean = (dword `0x0151720c` == **60**) via `0x00710570`. CONFIRMED —
listing.

**Where the value comes from (CONFIRMED — follow-up dump of `0x007104f0`, its only writer; xref run):** at start-up
(`0x007104f0`, called from `0x005d25f0`) the game asks Windows for the user's default locale (`GetUserDefaultLCID`) and
then `GetLocaleInfoW(locale, 0x2000000d, buffer, 4)`; `0x0151720c` = 60 when the returned number is **1**, else 0. The
same routine sets `0x012f4f88` = 8 and byte `0x01517210` = 1. HIGH CONFIDENCE (public Win32 constants):
`0x2000000d` = `LOCALE_IMEASURE` | `LOCALE_RETURN_NUMBER`, whose value 1 means the U.S. measurement system and 0 the
metric one. So **imperial units follow the Windows user locale, decided once at boot**; there is no in-game option and no
Lua setter. Before `0x007104f0` runs the dword is 0 (metric). The meaning of 60 itself (a second getter `0x00710560`
returns the raw dword) is OPEN. Safe.

### D.2 `game_steam_open_url` → `0x008421c0`

**Arguments:** 1 string = URL (unconditional; read only after the Steam check). **Return:** 0 values. CONFIRMED — listing.

**Body:** if `SteamFriends()` (import slot `0x0101c570`) returns non-null, it is called again and its virtual
**`+0x54`** is called with the string. HIGH CONFIDENCE that slot `+0x54` is Steamworks
`ISteamFriends::ActivateGameOverlayToWebPage` (one C-string argument; the next slot after `+0x50` =
`ActivateGameOverlayToUser`, tranche 12 C.3, and `+0x4c` = `ActivateGameOverlay`, tranche 11 C.5). A missing or
non-string argument is passed as **null** to Steam (no check) — what Steam does with it is OPEN, as for tranche 11 C.5.
The second `SteamFriends()` result is not re-checked (same as the first, harmless). No URL validation: a script can open
any URL in the overlay.

### D.3 `game_steam_open_dlc_page_overlay` → `0x00842130`

**Arguments:** none read. **Return:** 0 values. CONFIRMED — listing.

**Body:** if `SteamFriends()` is non-null, formats `"http://store.steampowered.com/dlc/%u/"` with the literal **55230**
(`0xd7be`) into a zeroed 128-byte stack buffer (`sprintf`; the result is 40 characters, it fits) and passes it to the same
`+0x54` overlay call as D.2. HIGH CONFIDENCE that 55230 is the game's own Steam application id, so this opens the DLC list
of the base game. Safe.

### D.4 `game_request_game_terminate` → `0x00842730`

**Arguments:** none read. **Return:** 0 values. CONFIRMED — listing.

**Body:** `0x00707810` (CONFIRMED — dump): if the byte `0x01503ec3` is clear, it sets it to 1, calls `0x00daf480` (sets
byte `0x029cffdd` = 1 and tail-calls `0x00dc26f0`, not dumped), and — unless the game-mode stack top (`0x00706ab0`, tranche
12 E.2) is **4** — queues a push of game mode **10** with `0x00706e00` (follow-up dump: appends {push, mode} to the pending
transition queue `0x012f4a88` (count `0x012f4a90`, capacity `0x012f4a8c`) unless that mode is already on the current
stack; **silently drops the request when the queue is full**; marks the queue dirty, byte `0x012f4a84`).

The latch has exactly one writer (this routine) and one reader, the one-instruction getter `0x00707800`, which is called
from the start-up / main-loop functions `0x005d1a30`, `0x005d2400`, `0x005d25f0`, `0x005d3ac0` and others (xref run).
HYPOTHESIS: "quit to desktop" — the main loop sees the latch and shuts down; mode 10 is the exit/teardown state.

**Logic notes (CONFIRMED structure):** **once-only** — the latch is never cleared, so a second call does nothing; when the
mode stack top is 4 the mode push is skipped but the latch and `0x029cffdd` are still set. No confirmation dialog in the
binding itself.

### D.5 `game_pause_map_filter` → `0x00842430`

**Arguments:** 1 optional string = filter name (nil-gated). **Return:** **0 values when a filter is set; otherwise 1
string** (the current filter's name). CONFIRMED — listing.

**Filter table (CONFIRMED — ptrs run, `0x0115d290`, 8 rows of 12 bytes {id, key, label}):** 0 `"PFT_ALL"` /
`"PAUSE_MAP_FILTER_ALL"`, 1 `"PFT_ACTIVITIES"`, 2 `"PFT_STORES"`, 3 `"PFT_PROPERTIES"`, 4 `"PFT_CRIBS"`, 5
`"PFT_FLASHPOINTS"`, 6 `"PFT_COLLECTIBLES"`, 7 `"PFT_NONE"` (each with a matching `"PAUSE_MAP_FILTER_…"` label).

**Body (CONFIRMED — listing and follow-up dumps):**
- **Set:** if arg 1 is present, not nil, and `lua_tolstring` returns non-null, `0x00802ed0(name)` compares it
  **case-insensitively** (`_stricmp`) with the eight keys; on a match the current filter `0x022bfd34` = id and
  `0x00802d40` rebuilds the category mask `0x022bfd30` from it; no match → nothing. Returns **0 values** either way.
- **Get:** otherwise, value = `0x022bfd34` (zero-fill → 0 = `"PFT_ALL"`): if value ≥ `0x80` (signed compare) it pushes
  **`"PFT_FILTER_LOCKED"`**; else `0x00802f30` looks the id up and pushes its key — or **nil** for an id 8 … `0x7f` that
  is not in the table.

`0x00802d40`'s switch (decompile of the resolved jump table, HIGH CONFIDENCE for the exact bit per case): id 0 → mask
`0x6f`, 1 → `0x08`, 2 → `0x01`, 3 → `0x02`, 4 → `0x04`, 5 → `0x40`, 6 → `0x80`, **7 (`PFT_NONE`) → only `0x20`**, and
the "locked" ids `0x81`-`0x83` → `0x63`, `0x84`-`0x86` → `0x21`, `0x87` → `0x20`, `0x88` → `0x03`; every case also ORs
`0x20`. The locked ids are written by game code through `0x00802eb0` (callers in `0x006b66f0`, `0x006b6e40`,
`0x006d4a10`, `0x006d5460`, `0x006d9470`, `0x006e48a4`) — HYPOTHESIS: missions forcing a filter.

**Logic notes (CONFIRMED structure):**
- **Arity depends on the argument type:** a string or a number (which `lua_tolstring` converts, then fails to match)
  → 0 values; a boolean or table (which `lua_tolstring` reads as null) → falls through to the getter and returns 1
  value. A caller passing a wrong type gets a value it did not ask for.
- **The setter ignores the lock:** a script can replace a mission-locked filter (`0x81`-`0x88`) with any of the eight
  public ones; the lock is only visible through the getter. Whether that is intended is OPEN.
- An unknown name is silently ignored (no error, no return value).

No crash shape (`_stricmp` is only reached with a non-null name).

---

## E. Prototype selection — 1 function (one-name registrar `0x00841020`)

### E.1 `game_proto_select` → `0x00840fa0`

**Arguments:** 1 number, 2 number (unconditional, truncated; a fixed loop of exactly two reads). **Return:** 0 values.
CONFIRMED — range run (`0x00840fa0`-`0x0084101e`).

**Body (re-verified against tranche 19 J.6, which stands):** a 4-byte stack local holds four flag bytes; bytes 0-2 are
cleared (byte 3 is **not** — it keeps the top byte of the value pushed at entry). For each argument v, v − 1 is computed
and, **if it is below 3 as a signed number**, byte `[local + v − 1]` = 1. Then the first index 0 … 3 whose byte is 0
becomes the selection `0x013010fc`, and byte `0x02317620` ("selection made", tranche 19 G.4) = 1.

**Refinement (CONFIRMED structure):** with only two writes into bytes 0-2, one of bytes 0-2 is always still 0, so the
uninitialised byte 3 is never consulted and the "nothing free" exit (`0x00840ffe`, which sets the flag without updating
the selection) is unreachable for valid arguments. For arguments 1-3 the selection is **the lowest of 0, 1, 2 not named
by either argument** (e.g. (1, 2) → 2; (2, 3) → 0; (1, 1) → 1). HYPOTHESIS: the screen offers three prototypes and the
script reports the two not taken, or the two already owned.

**Crash shape — CONFIRMED structure (tranche 19 J.6), outcome HYPOTHESIS (`0x00840fda`-`0x00840fdf`):** an argument of
0, nil or a non-number gives v − 1 = −1 and writes byte 1 into the top byte of the **caller's saved EBX**; arguments −15
… 0 reach the saved EDI/ESI/EBP/EBX of the Lua C-call dispatcher. Fix: accept 1-3 only.

---

## F. Machinima clips and recording — 5 functions (registrar `0x00bc68a0`)

**Shared path builder `0x00bc5b70(buffer, 260, name)` (CONFIRMED — listing in the `lua`-mode dumps):** opens the process
token (`OpenProcessToken`) and closes it again **without using it** (the folder query passes a null token), asks
`SHGetFolderPathW` for CSIDL `0x8005` (Documents, create flag), builds the folder with `0x00bc5a90` from the three
components at `0x01311db0` (ptrs run: `"My Games"`, `"Saints Row The Third"`, and a third, wide, component not decoded),
then `_swprintf_s(buffer, 260, "%s\%s.%s", folder, name, "srtt_clip")`.

**Path-builder defects (CONFIRMED structure, used by every function below):**
- on **any** of its three failure exits (token open fails, `SHGetFolderPathW` fails, folder creation fails —
  `0x00bc5bb6`, `0x00bc5bed`, `0x00bc5c3f`) it returns **without writing the output buffer**, and no caller checks:
  the callers then pass an **uninitialised stack buffer** as a path to `FindFirstFileW`, `CopyFileW` or `DeleteFileW`
  (environment-gated; reachability HYPOTHESIS);
- the 260-character limit of `_swprintf_s` is not checked beforehand: a long Documents path plus a long clip name makes
  the CRT invalid-parameter handler fire, which terminates the process in a default release CRT (tranche 12 E.2, HIGH
  CONFIDENCE for the CRT behaviour).

**Name sources differ by binding** (CONFIRMED — listings): `game_machinima_delete_clip` and the *source* of copy/overwrite
take a **UI-encoded** name (decoded with `0x00e22ac0`, so a plain ASCII string leaves the decode buffer uninitialised —
tranche 12 E.2's defect, again); `game_machinima_clip_exists` and the *destination* of copy/overwrite take a **tag**,
hashed with `0x00d9e8b0` and looked up by `0x00845170` → `0x008436c0` in a runtime **hash → wide-string registry** at
`0x02317d74` (12-byte records {hash, string pointer, …}, count `0x02317b64`, maintained by `0x008449f0` / `0x00844b10` /
`0x00844bc0`, not dumped; a hash equal to the global `0x029c9964` is rejected). HYPOTHESIS: the registry holds text the
UI stored under a tag, e.g. a clip name typed on the on-screen keyboard.

### F.1 `game_machinima_clip_exists` → `0x00bc5c80`

**Arguments:** 1 string = registry tag (unconditional, null-checked). **Return:** exactly 1 boolean. CONFIRMED — listing.

**Body:** tag null → false; registry lookup null → false; otherwise builds the path into a 520-byte stack buffer and
returns whether `FindFirstFileW` finds it (`0x00bc4ef0`: closes the find handle; any error → false). Note the argument is
the **tag**, not the file name. Crash shape only through the path-builder defects above.

### F.2 `game_machinima_copy_clip` → `0x00bc5d20` and F.3 `game_machinima_overwrite_clip` → `0x00bc5e60`

**Arguments:** 1 string = source clip name (unconditional, null-checked); 2 string = destination tag (unconditional,
null-checked). **Return:** 0 values. The two handlers are identical except for the last `CopyFileW` argument:
**copy passes 1 (fail if the destination exists), overwrite passes 0**. CONFIRMED — listings
(`0x00bc5e24` vs `0x00bc5f64`).

**Body (CONFIRMED — listing):**
1. Source null → nothing. Source equal to `"machinima"` (an inline, **case-sensitive** byte compare) → the wide literal
   `L"machinima"` is copied (`wcsncpy`, 260) as the source name; otherwise the source is decoded with `0x00e22ac0` into a
   520-byte buffer.
2. Destination null, or not in the registry → nothing.
3. Both paths are built (`0x00bc5b70`) and `CopyFileW(source, destination, flag)` is called; **its result is discarded**.

**Logic notes (CONFIRMED structure):**
- **A plain (not UI-encoded) source name other than `"machinima"` leaves the source buffer uninitialised** and that
  garbage is used as the file name (same defect as tranche 12 E.2's playback); the copy then most likely fails silently,
  or copies an unintended file if the stale stack happens to hold a valid name.
- The script cannot learn whether the copy happened (destination existed for `copy`, source missing, path failure).
- `"machinima"` here names `<folder>\machinima.srtt_clip`, whereas `game_machinima_playback_enter`'s special case
  (tranche 12 E.2) maps the same word to the relative path `L"srtt clip"`. HYPOTHESIS: `machinima.srtt_clip` is the
  working clip the recorder writes, which supports tranche 12's reading of `L"srtt clip"` as a leftover.

### F.4 `game_machinima_delete_clip` → `0x00bc5fa0`

**Arguments:** 1 string = clip name, **UI-encoded** (unconditional, null-checked). **Return:** 0 values. CONFIRMED —
listing.

**Body:** null → nothing. Otherwise decodes the name (`0x00e22ac0`, no `"machinima"` special case), builds the path and
calls `DeleteFileW`; result discarded.

**Defect — CONFIRMED shape (`0x00bc5fe0`):** a plain-text name leaves the decode buffer uninitialised and **a file
whose name is whatever the stack held is deleted** — confined to `<folder>\<garbage>.srtt_clip`, so in practice a
silent no-op, but a stale buffer holding an earlier clip's name would delete that clip (HYPOTHESIS for likelihood). The
decoded name is not sanitised (no check for `..\` or separators): an encoded name with path components can delete a
`.srtt_clip` file outside the clip folder (HYPOTHESIS for relevance — the names come from game scripts).

### F.5 `game_machinima_record` → `0x00bc5840`

**Arguments:** none read. **Return:** 0 values. CONFIRMED — listing.

**Body (`0x00bce0d0`, CONFIRMED — dump):**
1. **Always** first runs the full machinima reset `0x00bc6270` (tranche 12 E.2: stops recording/playback, posts the
   radio-unmute event, frees the clip buffers, state `0x029443bc` = 0).
2. Then refuses silently unless all of: `0x006cecb0()` false (the object at `0x014c8460` is absent, or the state dword
   `0x014c7a14` is 8); `0x006ced00()` == −1 (follow-up dump: no current object at `0x014c8460` outside state 8, no
   `0x006a3900()` result, and no fallback object `0x014c8328` in states 6/7 — HYPOTHESIS: "no mission or activity is
   running"); `0x00a006b0()` false (ten UI/screen-busy checks — `0x00831910` (`0x02300884` non-zero or byte `0x022ccc7e`
   bit 0), `0x0082b160`, `0x00818550`, `0x005f5f50`, two `0x012fced8` queries with `0x33`, `0x005ee970`, and three
   `0x005f5e50` objects); `0x00831910()` false again; `0x012fd5c8` == 0 (`0x007c68e0`); state == 0 (always true after
   step 1).
3. If allowed: radio-mute event (`0x00bc5570(1)` → `"MACHINIMA_RADIO_MUTE"`), `0x0268d2d0` = −1, `0x006d0c10(0, 0)`
   (walks the world list `0x03171a64` `+0x2bc`/`+0x2c4` calling `0x006e0810(0)` on each entry, then `0x006a44c0(0, 0)`;
   HYPOTHESIS: suspends ambient activity), sets bit 3 of `0x014a0f8c` (`0x005ee8e0(3)`), **state = 1** (recording),
   `0x00bcdb20`, `0x00bcdf80` (start capture, skimmed), and copies `0x02946ce8` to `0x02946cec`.

**Logic notes (CONFIRMED structure):**
- Calling it while already recording **stops and restarts** the recording (step 1), discarding the previous take unless
  the reset saves it (OPEN).
- A refusal is silent and also leaves the reset's side effects (radio unmuted, buffers freed, playback stopped); the
  script must poll `game_machinima_is_recording` (tranche 12 E.1) to know whether recording began.

No crash shape in the binding.

---

## G. Lobby and co-op — 5 functions

### G.1 `game_lobby_update_ready_state` → `0x007abb20` (lobby registrar)

**Arguments:** 1 boolean = ready (unconditional; missing = false). **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing):**
- **Client in a session** (session exists and `+0x5c` ≠ `+0x58`): byte `0x022423b1` = ready. Nothing else.
- **Host, or no session:** ready **false** → byte `0x022423b0` ("start requested", tranche 12 D) = 0; ready **true** →
  runs the Lua global **`"game_lobby_countdown_game_start"`** as a coroutine (record `+0x14` = document handle
  `0x022423e4`); `0x022423b0` is not touched.

Consumer (follow-up dump of `0x007ab410`, called from the lobby update `0x007abe40`): when lobby flag byte `0x022423de`
has bits `0x1` and `0x4` set and `0x2` clear, the Lua global **`"game_lobby_update_ready"`** is called — **on a client with
one boolean** (`0x022423b0`), **on the host or without a session with two booleans** (`0x022423b1`, then a literal true).
The lobby update `0x007abe40` takes the addresses of both bytes (`PUSH 0x22423b0` / `PUSH 0x22423b1`, xref run) —
HYPOTHESIS: registers them as synchronised lobby variables, so a client's ready byte reaches the host and the host's start
byte reaches the client.

**Logic notes (CONFIRMED structure):** the binding's meaning differs by role — on a client it only reports readiness; on
the host "ready = true" starts the countdown script and "ready = false" cancels a pending start. The
`"game_lobby_update_ready"` callback's arity also differs by role. A second, unexplained reference to the handler's
address is an immediate inside `0x007aae20` (an index → pointer switch whose case 1 returns the string `"city_load"`;
range run) — OPEN whether that is a real use or a coincidental constant. No crash shape.

### G.2 `game_lobby_horde_continue_from_wave` → `0x007aba20` (lobby registrar)

**Arguments:** none read. **Return:** exactly 1 number. CONFIRMED — listing.

**Body:** co-op = either co-op start flag (`0x014ff6c1` via `0x007027f0`, `0x014ff6c2` via `0x00702800`; tranche 12 "Co-op
start flags"); result = `0x006e6750(selected map 0x022423a4, co-op)` (tranche 12 D.3: **0** when the map index is out of
range, **−1** when the progress table has no entry, else (stored value & 63) + 1); a result of **1 is replaced by −1**;
the number is pushed.

So the script sees **−1 = "nothing to continue"** (no entry, or the first wave), **N ≥ 2 = "can continue from wave N"**
(HYPOTHESIS: the stored value is the last wave reached), and **0 = the selected map index is invalid** (CONFIRMED
structure). The 0 case is the same inconsistency tranche 12 D.3 found from the other side: `game_lobby_set_horde_mode_data`
treats 0 as "has saved progress" and shows the restart warning. A script that tests `> 0` is safe; one that tests
`~= -1` would offer "continue from wave 0" for a bad map index. The co-op byte is passed through a stack slot whose upper
three bytes are stale; the callee compares only the low byte (CONFIRMED, harmless). No crash shape.

### G.3 `game_lobby_finished_loading` → `0x007ab150` (lobby registrar)

**Arguments:** none read. **Return:** 0 values. **Body:** byte **`0x022423b3`** = 1. CONFIRMED — listing.

The byte is cleared by the lobby screen's set-up method `0x007ab680` (reached through a vtable at `0x01155588`) and by
code at `0x007abcc7`, read by the lobby update `0x007abe40` (`CMP …,0` at `0x007abe54`) and by the one-instruction getter
`0x007aaf80` (called from `0x006e8a10`) — xref run. HYPOTHESIS: "the lobby UI has finished loading", gating the lobby's
per-frame logic. Trivially safe; idempotent.

### G.4 `game_kick_coop_player` → `0x00844160` (`game_` helper registrar)

**Arguments:** 1 nil-gated boolean = **silent** (default false). **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing):** only when a session exists, **this machine is the host**, and a remote player exists
(`0x008b7100`):
1. unless silent: formats the localised `"COOP_PLAYER_KICKED"` with the player's name (remote player `+0xc2`) into a
   **512-byte (256 wide-character) stack buffer** with `0x00db0510`, and shows it as a notice through `0x007c3d80`
   with the localised title `"CONTROL_KICK_PLAYER"`;
2. removes the player with reason **`0xf`** through `0x0087e400` (session in ECX; tranche 12 C.2: unlinks the player from
   the session ring, decrements the player count, host-side clean-up).

Unlike `game_coop_kick_player` (tranche 12 C.2) there is **no confirmation dialog and no Lua callback**: the kick is
immediate. On a client, or with no remote player, nothing happens and nothing tells the script so.

**Stack-buffer overflow shape — CONFIRMED structure, data-gated (`0x008441ee`):** the 1024-character limit of
`0x00db0510` does not protect the 256-character buffer, exactly the shape tranche 12 C.2 recorded for this sibling;
reachable only with a localised format string plus a remote persona name longer than 255 characters (HYPOTHESIS: not
reachable with shipped data and real names).

### G.5 `game_is_waiting_for_partner` → `0x008424e0` (`game_` helper registrar)

**Arguments:** none read. **Return:** exactly 1 boolean = `0x008b6c60()`. CONFIRMED — listing.

**Body (CONFIRMED — follow-up dumps):** X = dword `0x024e4820` (`0x0088b610`; written by `0x0088c090`, cleared by
`0x0088bdf0`).
- **X null → true.**
- Otherwise `0x00877d60(4)` with X in ECX: if X `+0x1c` is null → false; else it walks the player ring of the object at X
  `+0x1c` (head `+0x54`, next `+0xb28c` — the session player ring layout of tranche 12 C.2) and returns **true if any
  player** whose status index (player byte `+0x158`) is below X `+0x10` has a status record (5 bytes each from X `+0xc`)
  with byte `+2` set and **status byte `+0` below 4** (signed). The walk includes the local player's own entry.

So "waiting for partner" = "some player in the session has not reached status 4", with two defaults: **true when the
object at `0x024e4820` does not exist**, **false when it exists without the object at `+0x1c`**. The status table is the
same 5-byte-stride table tranche 12 D.3 read through `0x00877a90` (there: "every other player's status ≥ 1"). What
`0x024e4820` is and when it exists is OPEN; no inference about single player is drawn from the "absent → true" default
(see I.9). No crash shape (both pointers checked; the ring walk stops on null or wrap).

---

## H. Method notes for the follow-up runs

- Main run: the job's own `lua` run (25 names, `index.txt` + 28 dump files).
- Follow-up dump (depth 0, `maxinsn:900`): `0x00839920 0x00839cc0 0x0083a9d0 0x00810d40 0x00810b60 0x0070d9b0 0x0070cd40
  0x008add90 0x00839e40 0x0083a000 0x00838f90 0x00839290 0x0083a550 0x00ac27a0 0x00a95d60 0x0083a300 0x008100c0
  0x00839710 0x00839be0 0x007104f0 0x00710560 0x00daf480 0x00707800 0x00706e00 0x00706ab0 0x00802d40 0x00802eb0
  0x00bcdb20 0x00bcdf80 0x006cecb0 0x006ced00 0x00a006b0 0x00831910 0x007c68e0 0x00bc5570 0x00a00810 0x006d0c10
  0x005ee8e0 0x008436c0 0x00bc5a90 0x0088b610 0x00877d60 0x0101b460 0x0087e400 0x00840fa0 0x00842100 0x007ab680
  0x007aaf80 0x007ab410`. `0x0083a9d0`, `0x00839290`, `0x00810d40`, `0x00bcdb20`/`0x00bcdf80` and `0x0070d9b0` were read
  only for the globals and calls cited (HIGH CONFIDENCE wording where it matters).
- Ptrs run (`count:24`): `0x0115d290`, `0x01311db0`.
- Range run: `0x00841020-0x00841060`, `0x00840f50-0x00841020`, `0x007aae00-0x007aae40`, `0x00842100-0x00842130`.
- Xref runs (`xrefs:40`): (1) `0x007abb20 0x0151720c 0x02303be0 0x02303be4 0x01503ec3 0x022423b3 0x022423b1`;
  (2) `0x02303bb0 0x02303bb8 0x02303bc0 0x02303bc8 0x02303bd0 0x02303bd4 0x02303bd8 0x02303bdc 0x02303be8 0x02303bec
  0x02303bf0 0x02300bc8 0x02301406`; (3) every dword `0x02303b80` … `0x02303bcc` (20 addresses) — the exhaustive
  writer check behind B.1 crash 1.
- Second range run: `0x0083a850-0x0083a900` (the screen tear-down: releases `0x02303bc4`/`0x02303bcc`/`0x02303bd0`
  handles, never writes `0x02303be0`), `0x00839210-0x00839290` (`0x02303bd4` is only a UI handle argument).
- 24 handlers were taken from the `{name, function}` slot pairs (insn offset +1); `game_proto_select` needed the
  pointer-before-name correction (A, range run).

---

## I. Cross-function observations

1. **The gang-customisation table indexes everything straight from Lua.** After tranche 20's slot array, this tranche
   adds the sign side: `select_gang_sign` (read) and `confirm_gang_sign` (stored pointer, persisted to the save) index
   the sign table with no bound against the byte count `0x02301406`. **Every one of the five gang-customisation names
   in this tranche takes an unchecked index or an unchecked baseline**; with tranche 20's `select_vehicle` and
   `show_vehicle`, all six index-taking rows of `0x0083a900` read so far are unchecked (the eighth row,
   `gang_customization_show_tag` → `0x0083a270`, was not read). One bound check per binding (sign position
   < `0x02301406`, slot < 3, mode ∈ {0}) would close all of them.
2. **A dead pointer behind a live mode.** `select_gang_sign` mode 1 reads `0x02303be0`, which the binary only ever
   zeroes (B.1). This is the clearest crash of the tranche: deterministic, not data-gated, triggered by a single argument
   value.
3. **Inert feature families on PC:** all six `game_record_*` names (C + tranche 12 B.1). Together with tranche 19 J.1's
   main-menu stubs, a reimplementation can bind these to constants without loss.
4. **Same verb, two name conventions.** The machinima file bindings mix **UI-encoded names** (`delete_clip`, the source of
   copy/overwrite, `playback_enter`) and **registry tags** (`clip_exists`, the destination of copy/overwrite). Passing
   the same Lua string to `clip_exists` and `delete_clip` refers to different things. A reimplementation must keep both
   conventions — and the encoded-name decoder's "write nothing on a plain string" behaviour is the root of three
   uninitialised-buffer shapes (copy, overwrite, delete), plus tranche 12's playback one.
5. **Results that are computed and then dropped:** `CopyFileW` / `DeleteFileW` results (F.2-F.4), the recording refusal
   (F.5), the kick on a client (G.4), the filter setter's "unknown name" (D.5). Same family as tranche 18/20's "call
   accepted ≠ effect achieved".
6. **Return arity that depends on input or state:** `game_pause_map_filter` (0 or 1 values by argument type),
   `"game_lobby_update_ready"` (1 or 2 callback arguments by role). Joins tranche 19 J.3's sentinel list:
   `game_lobby_horde_continue_from_wave` (−1 / 0 / N ≥ 2), `game_pause_map_filter` (`"PFT_FILTER_LOCKED"`, nil for an
   unlisted id).
7. **Environment-derived behaviour:** `game_use_imperial_units` follows the Windows user locale's measurement setting,
   read once at boot (D.1); the clip folder follows the Windows Documents folder (F). A reimplementation running under a
   different locale or a redirected Documents folder will diverge from the original in exactly these two places.
8. **Once-only latches:** `game_request_game_terminate` (never cleared, D.4), `game_lobby_finished_loading` (cleared only
   by the lobby screen's own set-up, G.3).
9. **No session claims in this tranche.** `game_lobby_update_ready_state`, `game_kick_coop_player` and
   `game_is_waiting_for_partner` branch on the session getter or on `0x024e4820`; `spec-lua-api-behaviour.md`
   §8.27/§26.28 already establish that single player has no session object, so the "no session" branches are simply the
   single-player paths. No inference about single player is drawn from them, and `0x024e4820`'s identity is left OPEN.

---

## J. OPEN

- B.1: whether the shipped script ever calls `gang_customization_select_gang_sign` with mode 1 (Team B's call-site
  data); what object `0x02303be0` was meant to hold; the meaning of hash `0x27bcc435` and of `0x0070d9b0`'s table
  `0x01504088`.
- B.1/B.2: the declared size of the position table `0x02300bc8`; how the other consumers of `0x023013ec` treat a record
  past the end of the sign table.
- B.3: whether a revert can run before the sign list is first built.
- B.5: whether `revert_vehicle` can run before `show_vehicle`/`confirm_vehicle` in shipped UI flow; what `0x00ac2560`
  does with hash 0; when `0x00ac1840` can return null for a saved id.
- D.1: what the value 60 in `0x0151720c` means beyond "imperial" (the raw getter `0x00710560` has other callers).
- D.2: Steam's behaviour for a null URL.
- D.4: what `0x00dc26f0` does; the meaning of game modes 4 and 10.
- D.5: whether scripts are meant to be able to override a locked filter.
- E.1: what the two `game_proto_select` arguments mean (excluded vs owned prototypes).
- F: the third folder component at `0x01311db0` (`0x0118eb5c`, wide, not decoded); who fills the tag registry
  `0x02317d74`; whether the recording reset saves the take before discarding it; the third-party routines skimmed in F.5
  (`0x00bcdb20`, `0x00bcdf80`, `0x006e0810`, `0x006a44c0`).
- G.1: whether `0x007abe40` really synchronises `0x022423b0`/`0x022423b1`; the immediate `0x007abb20` inside `0x007aae20`.
- G.5: what object `0x024e4820` is (written by `0x0088c090`) and when it exists; the meaning of status value 4.

---

## K. Direct-answer table

| # | name | registrar | handler | status |
|---|---|---|---|---|
| 1 | `gang_customization_select_gang_sign` | gang customisation `0x0083a900` (under UI) | `0x00839b00` | resolved — mode 0 previews sign id (unbounded position); **mode 1 dereferences a never-set pointer** (B.1) |
| 2 | `gang_customization_revert_vehicle` | gang customisation | `0x0083a790` | resolved — restore slot from baseline, respawn preview; unbounded slot write; **null baseline crash** (B.5) |
| 3 | `gang_customization_revert_gang_sign` | gang customisation | `0x00838e50` | resolved — current sign = copy taken at list build; preview cleared (B.3) |
| 4 | `gang_customization_confirm_vehicle` | gang customisation | `0x00839ad0` | resolved — baseline = slot item; unbounded slot read (B.4) |
| 5 | `gang_customization_confirm_gang_sign` | gang customisation | `0x00839ba0` | resolved — current sign = record at position; unbounded, persisted to the save (B.2) |
| 6 | `game_use_imperial_units` | `game_` helper `0x00845aa0` | `0x00841d50` | resolved — true when the Windows locale's measurement system is U.S., read at boot (D.1) |
| 7 | `game_steam_open_url` | `game_` helper | `0x008421c0` | resolved — Steam overlay web page with the given URL (D.2) |
| 8 | `game_steam_open_dlc_page_overlay` | `game_` helper | `0x00842130` | resolved — overlay opens the store DLC page for app 55230 (D.3) |
| 9 | `game_request_game_terminate` | `game_` helper | `0x00842730` | resolved — once-only exit latch + queue game mode 10 (D.4) |
| 10 | `game_record_set_quality_level` | pause options `0x007e18e0` (under UI) | `0x007c9f50` | resolved — no-op (C.1) |
| 11 | `game_record_mode_show_hud` | pause options | `0x007c9f50` | resolved — no-op (C.2) |
| 12 | `game_record_mode_enable` | pause options | `0x007c9f50` | resolved — no-op (C.3) |
| 13 | `game_record_mode_can_encode` | pause options | `0x00a3c670` | resolved — always false (C.4) |
| 14 | `game_proto_select` | one-name `0x00841020` (pointer-before-name, under UI) | `0x00840fa0` | resolved — selection = lowest of 0-2 not named; **≤ 0 / nil argument corrupts saved registers** (E.1; tranche 19 J.6) |
| 15 | `game_pause_map_filter` | `game_` helper | `0x00842430` | resolved — set by `"PFT_*"` name (0 values) or get name / `"PFT_FILTER_LOCKED"` / nil (D.5) |
| 16 | `game_machinima_record` | machinima `0x00bc68a0` (under UI) | `0x00bc5840` | resolved — reset, then start recording if no mission/UI blocks it; silent refusal (F.5) |
| 17 | `game_machinima_overwrite_clip` | machinima | `0x00bc5e60` | resolved — copy encoded-name clip to tag-named clip, overwriting (F.3) |
| 18 | `game_machinima_delete_clip` | machinima | `0x00bc5fa0` | resolved — delete encoded-name clip; **plain name deletes a garbage path** (F.4) |
| 19 | `game_machinima_copy_clip` | machinima | `0x00bc5d20` | resolved — as 17 but fails if the destination exists (F.2) |
| 20 | `game_machinima_clip_exists` | machinima | `0x00bc5c80` | resolved — `FindFirstFileW` on the tag-named clip (F.1) |
| 21 | `game_lobby_update_ready_state` | lobby `0x007abbb0` (under UI) | `0x007abb20` | resolved — client: ready byte; host: start countdown script / cancel start (G.1) |
| 22 | `game_lobby_horde_continue_from_wave` | lobby | `0x007aba20` | resolved — −1 none, N ≥ 2 wave, **0 = invalid map** (G.2) |
| 23 | `game_lobby_finished_loading` | lobby | `0x007ab150` | resolved — sets byte `0x022423b3` (G.3) |
| 24 | `game_kick_coop_player` | `game_` helper | `0x00844160` | resolved — host-only immediate kick, optional notice; 256-character buffer (G.4) |
| 25 | `game_is_waiting_for_partner` | `game_` helper | `0x008424e0` | resolved — any session player below status 4; true when `0x024e4820` absent (G.5) |

---

## L. Crash-shaped and logic defects (summary)

| # | where | defect | status |
|---|---|---|---|
| 1 | B.1 `0x00839b44`-`0x00839b4a` | `select_gang_sign` mode 1 reads the vtable through `0x02303be0`, which is only ever **zeroed** (`0x0083ac30`) — null dereference on every mode-1 call | CONFIRMED structure; static-negative for other writers (computed-pointer write not excludable) |
| 2 | B.1 `0x00839b7c`-`0x00839b89` | `select_gang_sign` mode 0: unbounded position → double arbitrary read (`0x02300bc8[pos]`, then the record) | CONFIRMED structure |
| 3 | B.2 `0x00839bbc`-`0x00839bcc` → `0x00839e40`/`0x00839cc0` | `confirm_gang_sign`: unbounded position → stored record pointer past the table, dereferenced by the list callback, **persisted to the save as a truncated byte and trusted on load** | CONFIRMED structure |
| 4 | B.5 `0x0083a7e2` → `0x00a95d81` | `revert_vehicle` with a null baseline writes null into the slot and passes it to `0x00a95d60`, which reads `[0 + 0x24]` | CONFIRMED structure, reachability HYPOTHESIS |
| 5 | B.4 `0x00839aec`, B.5 `0x0083a7e2` | unbounded slot read / write on the 3-slot array `0x023013f0` | CONFIRMED (tranche 20 I.1 crash 1) |
| 6 | B.5 `0x0083a7f6`-`0x0083a81c` | null UI element used as `this` | CONFIRMED shape, reachability OPEN (tranche 20 I.1 crash 3) |
| 7 | B.3 `0x00838e5a` | revert before the sign list was ever built → current sign = null → `[0 + 4]` read at the next list build, garbage index in the next save | CONFIRMED structure, reachability HYPOTHESIS |
| 8 | E.1 `0x00840fda`-`0x00840fdf` | `game_proto_select` argument ≤ 0 / nil / non-number → byte write into the caller's saved registers | CONFIRMED structure (tranche 19 J.6), outcome HYPOTHESIS |
| 9 | F `0x00bc5bb6`, `0x00bc5bed`, `0x00bc5c3f` | path builder leaves the output buffer unwritten on its three failure exits; callers use the uninitialised buffer as a path for `FindFirstFileW` / `CopyFileW` / `DeleteFileW` | CONFIRMED structure, environment-gated |
| 10 | F `0x00bc5c5d` | `_swprintf_s` with a 260-character limit and no prior length check → CRT invalid-parameter termination for long Documents paths / names | CONFIRMED structure; outcome HIGH CONFIDENCE |
| 11 | F.2/F.3 `0x00bc5dce`, `0x00bc5f0e`; F.4 `0x00bc5fe0` | plain (non-encoded) clip name → decoder writes nothing → uninitialised name used; for `delete_clip` a garbage-named file is deleted | CONFIRMED shape |
| 12 | F.4 | decoded clip names are not sanitised (`..\` / separators) before `DeleteFileW` | CONFIRMED structure, relevance HYPOTHESIS |
| 13 | G.4 `0x008441ee` | 256-character stack buffer formatted with a 1024-character limit | CONFIRMED structure, data-gated (tranche 12 C.2) |
| 14 | D.5 | `game_pause_map_filter` returns 0 or 1 values depending on the argument's type; setter overrides mission-locked filters; unknown names silently ignored | CONFIRMED |
| 15 | G.2 | `game_lobby_horde_continue_from_wave` returns 0 for an invalid map index, which callers treating "≠ −1" read as "continue from wave 0" | CONFIRMED structure |
| 16 | F.5 | `game_machinima_record` always resets first (restart on repeat; reset side effects even when refused); refusal is silent | CONFIRMED |
| 17 | F.2-F.4 | copy/overwrite/delete discard the Win32 result; the script cannot detect failure | CONFIRMED |
| 18 | D.4 | `game_request_game_terminate` is once-only; the mode-10 push is dropped silently when the transition queue is full | CONFIRMED structure |
| 19 | G.1 | `game_lobby_update_ready_state` means "report ready" on a client but "start countdown / cancel start" on the host | CONFIRMED |
| 20 | G.4 | `game_kick_coop_player` does nothing on a client or without a remote player, with no feedback | CONFIRMED |
| 21 | C | all `game_record_*` setters are no-ops and the queries constant false on PC | CONFIRMED |
| 22 | D.1 | imperial units come from the Windows user locale at boot; no in-game control | CONFIRMED structure; constant meaning HIGH CONFIDENCE |

---

## M. Clean-room check

- Addresses are plain hex throughout; no Ghidra auto-names for functions, globals or labels, no decompiler variable
  names, and no pasted pseudocode. Library routines are named only where Ghidra identified them as standard C runtime or
  Win32 imports (`sprintf`, `_swprintf_s`, `wcsncpy`, `_stricmp`, `GetUserDefaultLCID`, `GetLocaleInfoW`,
  `OpenProcessToken`, `SHGetFolderPathW`, `FindFirstFileW`, `CopyFileW`, `DeleteFileW`), as the public Steamworks API
  (`SteamFriends`, `ISteamFriends::…`) or as the public Lua 5.1 API by established project convention (`lua_gettop`,
  `lua_tolstring`, `lua_pushcclosure`, `lua_setfield`, …); short game strings (Lua hook names, localisation keys, filter
  keys, folder names, a URL format) are quoted as data.
- Self-check run on the finished file, as the last step before reporting, with the pattern
  `\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4}|LAB_[0-9a-fA-F]+|FUN_[0-9a-fA-F]+|DAT_[0-9a-fA-F]+)\b`
  — (Python `re`, line by line, on the finished file): **0 hits**. The pattern was first checked against 16 controls: 12 positives (auto-named locals and parameters, a register input, a stack array, an `unaff_` register, a type name, and the `LAB_`/`FUN_`/`DAT_` label forms), all matched, and 4 negatives ("left undefined", "in ECX", a bare `0x…` address, ordinary prose), none matched.
- No spec file was edited. The private Ghidra copy `tools\gp_t21` was deleted after the dumps; raw dumps stayed in the
  session scratchpad.
