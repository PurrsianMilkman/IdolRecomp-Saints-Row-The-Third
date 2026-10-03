# Ranking tranche 16 — 25 previously-unspecced Lua-bound names (Team A, 2026-10-03)

Job definition: `D:\Project Crreish\TEAM A\ghidra\jobs\ranking\tranche-16.json`. The job file's own title states **names
326-350 of the 554 unspecced names, Team B call-count order**; this note uses that range (the dispatch brief gave no
competing numeric guess, so there is nothing to correct). Run locally, read-only, on a private copy of the Ghidra project
(`tools\gp_t16`, deleted afterwards), with the job's own arguments: `CrreishDump.java <out>/lua lua depth:1 maxfuncs:15
maxinsn:500 <25 names>`. **All 25 names resolved.** Every name string occurs exactly once in `.rdata`. For 23 names the
handler is the code pointer stored in the slot right after the name (insn offset +1) — CONFIRMED — disassembly (dump
`index.txt`, cross-checked against the range run). The other two, `shop_purchase_purchase_shop` and
`screen_capture_preview_should_upload`, are registered by **one-name registrars that push the function pointer before the
name** (the same shape tranche 13 found at `0x0067d660`), so the dumper's offset guess landed on the `lua_setfield`
primitive `0x00dfe830`; the real handlers `0x00a03490` and `0x007afeb0` were read from the range run (section A).
Follow-up runs on the same private copy, cited by name below:

- "range run": `range` mode on the ten registrar bodies of section A;
- "xref run": `xref` mode on the nine non-gameplay registrars (each has exactly one caller, section A);
- "follow-up dump": depth-1 `func` run on `0x00a03490` and `0x007afeb0`;
- "second follow-up dump": depth-0 `func` run on 27 callee addresses (listed in section F);
- "global xref run": `xref` mode on 14 globals (listed in section F);
- "constant read": `ptrs` mode on six float/int constants (section F);
- "second range run": `range` mode on `0x00909190-0x00909210` and `0x00820940-0x00820990`.

Call counts are Team B's (`D:\Crreish-sync\for-team-b\team-b\tools\lua_reconciliation_called_and_registered_1181.tsv`):
**every name in this tranche is 1 call site in 1 script.** Team B tags 15 names `ui` (the eleven `store_*`,
`shop_purchase_purchase_shop`, `sfx_use_load_images`, `set_char_in_string`, `screen_capture_preview_should_upload`) and
10 `gameplay`; section A shows the registrars agree exactly (15 under the UI bring-up, 10 in the gameplay registrar).

Labels: **CONFIRMED** = read in the listing or decompile of a dump made for this note; **HIGH CONFIDENCE** = follows
from a dumped call or reference, but the callee body was not dumped or a meaning is inferred from strong usage;
**HYPOTHESIS** = plausible, not settled; **OPEN** = not settled (collected in section H).

**Shared conventions, cited not re-described:** every entry opens with the `lua_gettop` prologue (`0x00dfde50`) and
reads arguments with negative stack indices counted from the bottom (`-N` = arg 1, `1-N` = arg 2, …). Primitives as in
tranche 09/11/13 front matter: `0x00dfe210` `lua_tolstring` (non-string reads as null), `0x00dfe1e0` `lua_toboolean`,
`0x00dfe040` `lua_type` (0 = nil), `0x00dfe160` `lua_tonumber` (non-number reads as 0), `0x00ea2596` truncating
float-to-int, `0x00dfe590` `lua_pushboolean`, `0x00dfe420` push-C-string (null pushes nil). "Nil-gated optional argument"
= the standard idiom of tranche 13. Resolvers: `0x00a281a0` generic character chain, `0x00a28150` (`#FOLLOWER#`, then the
plain character resolver `0x005e4dd0` on the world-object registry `0x02442750`, liveness `0x00853b10`, virtual `+0x70`).
`0x009da4e0` local player; `0x009df3d0` remote co-op player; `0x0087ba20` session; "host" = session exists and its
`+0x5c` equals `+0x58`. Kind-flag tests through `[0x02cc9900 + 4·(object byte +0x34)]` and the bit helper `0x00853b30`
(tranche 13). Handle resolution: `0x00458230` on `0x024433a8` with key `0x031d152c`, then the `+0x33` bit `0x10`
rejection (tranche 11). Network record helpers (`0x0086f5f0` open with opcode, `0x00881110`/`0x00881040` write bits/bytes,
`0x00a017c0` identity serialiser, `0x0086f1b0` broadcast to session, `0x0086f110` send to one machine, `0x0086eb20`
close) as in `interp_tabs.md` Q6/Q14 and tranche 11. The "double-gate" setter shape (`0x008ae480`, `0x008837a0`, owner
`0x008ae3a0`) is `spec-lua-api-behaviour.md` §7.2 / tranche 13 B.3. The session-synced-variable registrar `0x0086d770`
is tranche 07/08 and tranche 13 F.6. The HUD-screen registry / screen stack `0x012fced8` and its pop `0x007b4790(0)` are
tranche 05 E.3a and tranche 06 D.2. The fade-out request `0x0059f8c0(ms, callback, flag)` is
`spec-lua-api-behaviour.md`'s fade table (state `0x012e6aa4`). The notice dialog `0x007c3d80` and the free-slot pop
`0x007c3310` are `interp_tabs.md` Q7.2/Q7.4; the cancel-line dialog `0x007c3e60` is tranche 09 B.12. The stronghold
object (level `+0x140`, `0x005f1750`, `0x005e8fb0`, trigger enable/disable `0x0093d320`/`0x0093bde0`) is
`interp_tabs.md` Q14. The teleport primitive `0x009e2220` is tranche 07.

**Small new generic facts, CONFIRMED (second follow-up dump / follow-up dump):**
- Named-object lookups on the registry `0x02442750` share one body shape (table at registry `+0x2660`, count `+0x265c`,
  lookup `0x004588f0`, dead-flag `+0x33` bit `0x10` rejection) and differ only in the kind bit they require:
  `0x005e4dd0` `+0xb` bit `0x04` (character), `0x00624610` `+0x7` bit `0x40` (**neighbourhood / "hood"**, this
  tranche), `0x0062a1f0` `+0x8` bit `0x02` (a point-list object, this tranche), `0x005982e0` `+0xb` bit `0x02` (a
  single-point object, this tranche).
- `0x00d9e8b0(name, 0, −1)` (the name hash of the front matter) returns **0 for a null name** (no crash).
- `0x009601d0(dollars)` stores dollars × 100 into its hidden receiver, saturating the input to ±20,000,000 and the
  result to ±2,000,000,000; `0x00960160` saturates to ±2,000,000,000; `0x00960190` adds with the same saturation. So
  player `+0x1ca0` is **cash in cents** (both the shop and the stronghold paths below compare it after a ÷100).
- `0x0094a800(this = player, &cents)` (follow-up dump): null `this` tolerated; when the double-gate says "apply
  locally" it stores the saturated amount into player `+0x1ca0` (an **absolute** set); otherwise, if the owner exists, it
  sends opcode `0x46` tagged `"human"`/`"cash"` with the player handle and the amount to the owner; with no owner it does
  nothing.

---

## A. Registrars — where the 25 names live

Ten registrars. CONFIRMED — `index.txt`, xref run, range run:

| registrar | reached from | names in this tranche |
|---|---|---|
| gameplay `0x00a20840` | (established) | 10: `squad_enable`, `spawning_boats`, `spawn_region_min_spawn_dist_reset`, `spawn_region_min_spawn_dist`, `spawn_override_set_override_category_for_hood`, `spawn_override_clear_override_for_hood`, `spawn_global_override_set_category`, `spawn_global_override_clear_category`, `skydive_move_to_check_done`, `set_attack_peds_flag` |
| `0x008152a0` (store vehicle, 8 names) | UI `0x008430f0`, call at `0x008431da` | 2: `store_vehicle_do_return_to_crib`, `store_vehicle_allow_garage` |
| `0x00812590` (stronghold store, 2 names) | UI `0x008430f0`, call at `0x0084320a` | 2: `store_stronghold_game_purchase_upgrade`, `store_stronghold_upgrade_end_flyby` |
| `0x008111f0` (gang store, 5 names) | UI `0x008430f0`, call at `0x008431d4` | 3: `store_gang_is_unlocked`, `store_gang_bg_covered`, `store_gang_begin_exit` |
| `0x0080fab0` (gallery, 4 names) | UI `0x008430f0`, call at `0x008431ce` | 3: `store_gallery_display_character`, `store_gallery_download_show_list`, `store_gallery_upload_character` |
| `0x0080e540` (crib, 3 names) | UI `0x008430f0`, call at `0x008431e0` | 1: `store_crib_init_crib_garage` |
| `0x00a03600` (one name) | UI `0x008430f0`, call at `0x008431ad` | 1: `shop_purchase_purchase_shop` |
| `0x005a01d0` (screen-fade/sfx, 4 names) | UI `0x008430f0`, call at `0x008431a7` | 1: `sfx_use_load_images` |
| `0x007aff80` (one name) | UI `0x008430f0`, call at `0x008431ec` | 1: `screen_capture_preview_should_upload` |
| `0x00845aa0` (113-name UI helper registrar, `spec-lua-bindings.md` §13.5) | UI bring-up `0x008489e0`, call at `0x00848cdc` | 1: `set_char_in_string` |

- The six table-driven sub-registrars build a local `{name, function}` array and loop over `lua_pushcclosure`
  (`0x00dfe4f0`) then `lua_setfield` into globals (`0x00dfe830`, index `-10002`). Loop counts from the range run: store
  vehicle **8**, stronghold **2**, gang **5**, gallery **4**, crib **3**, sfx **4**. Full tables, for a binding layer:
  store vehicle = `store_vehicle_get_state` `0x008133c0`, `store_vehicle_change_mode` `0x00815120`,
  `store_vehicle_retrieve_car_and_exit` `0x00813020`, `store_vehicle_no_vehicle_exit` `0x00813080`,
  `store_vehicle_do_return_to_crib` `0x00813130`, `store_vehicle_notify_screen_covered` `0x00814060`,
  `store_vehicle_selection_should_lock_controls` `0x00813170`, `store_vehicle_allow_garage` `0x008131c0`; stronghold =
  `store_stronghold_game_purchase_upgrade` `0x00812400`, `store_stronghold_upgrade_end_flyby` `0x00812480`; gang =
  `store_gang_is_unlocked` `0x008101c0`, `store_gang_allow_input` `0x0080fcb0`, `store_gang_show_question_marks`
  `0x00811170`, `store_gang_bg_covered` `0x00811150`, `store_gang_begin_exit` `0x0080fd10`; gallery =
  `store_gallery_display_character` `0x0080fa80`, `store_gallery_download_hide_list` `0x0080f1a0`,
  `store_gallery_download_show_list` `0x0080fa00`, `store_gallery_upload_character` `0x0080f170`; crib =
  `store_crib_init_crib_garage` `0x0080e3e0`, `crib_has_garage_with_vehicles_of_type` `0x0080e490`,
  `crib_is_garage_disabled` `0x0080e0b0`; sfx = `Screen_fade_transition_complete` `0x005a0110`, `sfx_faded_out`
  `0x0059fb30`, `sfx_faded_in` `0x0059fb60`, `sfx_use_load_images` `0x0059fb90`. CONFIRMED — range run.
- The two one-name registrars (`0x00a03600`, `0x007aff80`) push `0`, the handler (`0x00a03490` / `0x007afeb0`) and the
  state for `lua_pushcclosure`, **then** the name and `-10002` for `lua_setfield` — the function pointer precedes the name,
  as at tranche 13's `0x0067d660`. `shop_purchase_purchase_shop` → `0x00a03490` agrees with `spec-lua-bindings.md`'s
  census line ("bound directly to … `0x00a03490`"). CONFIRMED — range run.
- The dumper's second "use" of the six table-driven names (`*_1.txt`, rooted at `0x00dfe830`) is the registrar loop
  reading the **first** row of its table, as in tranche 09/13 — not a second handler. HIGH CONFIDENCE.
- `spawn_region_min_spawn_dist` has a spurious second "use" at `0x009059f5` (`spawn_region_min_spawn_dist_0.txt`, rooted at
  `0x007fffc0`): the raw immediate scan matched the bytes of a `CALL 0x00917190` instruction (opcode `0xe8` followed by the
  displacement `0x00011796`, which reads little-endian as `0x011796e8`, the string's address). CONFIRMED by arithmetic
  (`0x00917190 − 0x009059fa = 0x00011796`). That dump is not a binding.

---

## B. Store screens — 12 functions

### B.1 `store_vehicle_do_return_to_crib` → `0x00813130`

**Arguments:** none read. **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing):** pops the top of the HUD screen stack (`0x007b4790(0)` on `0x012fced8`); then, if the
dword `0x022cdf14` is 0, pops once more; otherwise sets the byte `0x022cdf18` = 1 and does not pop again.

**Dead branch (CONFIRMED for direct references; HYPOTHESIS that nothing reaches it through a pointer):** the global xref
run finds exactly **one** reference to `0x022cdf14` in the whole binary — this read. It is zero-fill (not file-backed) and
never written, so the function **always pops twice** and the `0x022cdf18` write is unreachable. `0x022cdf18` itself has
two references: this write and a read in the customization teardown `0x008208a0` (second range run: when that teardown's
argument is non-zero, the byte is 0 and `0x022cf8d4` is set, it calls `0x005f62c0`; a set byte would skip that call). It
is **never cleared**, so if a reimplementation ever makes the branch live, the flag sticks for the rest of the session.
A binding layer can implement this name as "pop the screen stack twice".

### B.2 `store_vehicle_allow_garage` → `0x008131c0`

**Arguments:** none read. **Return:** exactly 1 boolean = **not** `0x006d9d30()`. CONFIRMED.

`0x006d9d30` (CONFIRMED — dumps) passes a 13-entry list of mission names to `0x006d9b90(list, 13)`: `"m01"`, `"m02"`,
`"m06"`, `"m09"`, `"m11"`, `"m13"`, `"m15"`, `"m18"`, `"m19"`, `"m23"`, `"m04"`, `"dlc2_m01"`, `"dlc3_m03"` (the first
eleven are short strings decoded from their static dwords, e.g. `0x0031306d` = "m01"). `0x006d9b90` returns false when no
mission is active (`0x014c8460` = 0) or the mission state `0x014c7a14` is 8; otherwise it looks each name up in the
mission registry `0x02444db0` (count `0x02444dac`; kind `+0xc` bit `0x04`, alive) and returns true when one of them **is**
the active mission `0x014c8460`.

So the garage is allowed **unless one of those 13 missions is running** (and always allowed between missions or in
mission state 8). Sibling: `0x006d9cb0` (called from `0x005f87c0`) passes the same list **without "m04"** (12 entries) —
a second, slightly different blocked-mission list for another crib/garage query (CONFIRMED difference; which binding owns
`0x005f87c0` is OPEN). HYPOTHESIS: state 8 = "mission ending/failed".

### B.3 `store_stronghold_game_purchase_upgrade` → `0x00812400`

**Arguments:** none read. **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing):**
1. The current stronghold `0x00811f70()`: resolves the handle at `0x022cdbe8`/`0x022cdbec` (handle resolution, kind
   `+0x9` bit `0x20`, alive), or 0 → return with nothing done.
2. Price = stronghold dword `+0xe4 + 4·level` (level = `+0x140`), negated, handed to the cash-adjust delegate
   `0x0094d920` (`spec-lua-api-behaviour.md`, the `cash_adjust` path) with **reason code 11** (`0xb`), receiver = local
   player. The amount is **not** scaled ×100 here, so the table already stores cents (HIGH CONFIDENCE).
3. `0x005f1750(level + 2)` on the stronghold — the set-level routine of `interp_tabs.md` Q14.4 (new level = old + 1).
4. Store sub-state `0x022cdbf4` = 1 (also written 0 by `0x00812600`, read and set to 2/3/4 by `0x008126b0`), and a
   250 ms fade-out (`0x0059f8c0(250, no callback, 0)`).

**Logic defect — cash taken before the upgrade's own gates (CONFIRMED structure):** step 2 is unconditional once a
stronghold resolves, but `0x005f1750` silently does nothing unless **1 ≤ level+2 ≤ 3, a session exists and this machine
is host** (Q14.4). So:
- **at the maximum level** (`+0x140` = 2 → argument 4) the price `+0xec` is charged and no upgrade happens;
- **on a co-op client** the cash is deducted (the delegate routes it to the owner) but the level does not change;
- **with no session** nothing is upgraded at all. Since this is a single-player store flow, this is strong circumstantial
  evidence that single player *does* run with a host session (relevant to HANDOFF's open `game_get_is_host` question —
  HYPOTHESIS, not proof: the UI script may never offer the purchase without one).
- **Unchecked index:** with `+0x140` = −1 (a stronghold never set to a level) the price is read from `+0xe0`
  (CONFIRMED arithmetic; whether the store can be opened on such a stronghold is OPEN).

**Crash shape (CONFIRMED shape):** no local player → `0x0094d920(null)`: its gate `0x008addb0(null)` reads true
(tranche 11), so it takes the local branch and reads `null + 0x1ca0` at `0x0094da81`. Unlikely while a store is open.

### B.4 `store_stronghold_upgrade_end_flyby` → `0x00812480`

**Arguments:** none read. **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing, second follow-up dump):**
1. **Always:** `0x0057ee20()` ends the camera fly-by: it asks `0x0057ece0` for the current fly-by pose (the frozen final
   pose if `0x013cb9a0` is set; the end pose if the fly-by timer `0x012e59d8` has expired; otherwise the start/end
   positions `0x013cb9f0`/`0x013cba00` linearly interpolated by elapsed ÷ duration `0x012e59d4`, with the orientations
   blended by a helper), stores it as the frozen final pose (`0x013cba10`, `0x013cb9b0`), sets `0x013cb9a0` = 1, and stops
   the fly-by's looping sound (`0x0045f1a0(&0x013cb9e4)`, handle cleared). It does **not** restore the saved camera mode —
   that is `0x0057ea90` (blend back to `0x013cb9e0` over 0.4 s), called from the stronghold store's exit path
   `0x008127d0`. The fly-by itself is started by `0x0057eb10(startPos, startOrient, endPos, endOrient)`, which saves the
   camera mode, switches to camera mode `0xf` (blend 0.4 s), arms `0x012e59d8` and plays `"3SYS_MENU_MAP_HOVER"`
   (callers: `0x005f1e50`, `0x00812340`, and `shop_purchase_purchase_shop`, B.12).
2. If a stronghold resolves (`0x00811f70`): collects the **unlocks of the current level** — every entry of the
   stronghold's parallel lists (count `+0x134`, level keys `+0x13c`, values `+0x138`) whose key equals `+0x140` and whose
   value is not the "none" sentinel `0x029c9964` (`0x005eea90` counts, `0x005ef100` fetches the i-th; both refuse a level
   above 2 as unsigned, so level −1 yields none) — into a `0x418`-byte notice record (`0x005e9f80` initialiser: float 0,
   0, −1, 256 sentinel slots, count, two zero dwords from `0x01126010`).
3. `0x007c02d0("Stronghold Upgrade", "upgrade level", record, 0, 0)` (wide strings): if a screen with id 9 or 10 is open or
   queued (`0x007e1f40`), closes all screens (`0x007b4cb0` → `0x007b4990(−1)`); then, **only when the count is non-zero**,
   stores both strings (`0x02282944`/`0x02282948`), mode **6** (`0x02282954`), copies the record (`0x005ea420`,
   `0x007bfd10` into `0x02282534..`, max 256), and pushes HUD screen **`0x39`** (`0x007b4750`) — the unlock-notice screen
   (HYPOTHESIS for the screen's name).
4. Arms the timer `0x01300834` with 3000 ms (`0x00d9e140`; cleared to −1 by `0x00812600` and code at `0x008127bd`).

**Shape (CONFIRMED unchecked, data-bounded):** step 2 writes matches into a 256-slot stack array with **no cap** on the
match count; more than 256 unlock entries for one level would overrun the stack. Bounded only by the stronghold table
data (HYPOTHESIS: real data has a handful per level).

### B.5 `store_gang_is_unlocked` → `0x008101c0`

**Arguments:** none read. **Return:** exactly 1 boolean. CONFIRMED.

**Body:** true iff **bit 1 (`0x02`) of byte `+0x1d99` of the local player** is set **and** this machine is not a co-op
client (no session, or host). A co-op client always reads false.

**Crash shape (CONFIRMED — listing):** the local player is not null-checked (`MOV BL,[EAX+0x1d99]` at `0x008101d4`).
Who sets `+0x1d99` bit 1 is OPEN (HYPOTHESIS: a progression unlock of the gang-customization store).

### B.6 `store_gang_bg_covered` → `0x00811150`

**Arguments:** none read. **Return:** 0 values. **Body:** `0x00811060()`. CONFIRMED.

`0x00811060` (CONFIRMED — listing, second follow-up dump): resolves the first gang-preview character (handle at
`0x022cdac8`/`0x022cdacc`, slot 0 of a 4-slot array, stride `0x40`, `0x022cdac8`..`0x022cdbc8`; kind `+0xa` bit `0x04`,
alive); nothing happens if it does not resolve. Otherwise:
1. `0x0080b9d0(char)`: copies the character's orientation (`+0x4c`, 3×16 bytes) to `0x022cbe50..0x022cbe7f`.
2. `0x00567f80(1, 2, 0, char)` — **enter the store camera** (new, below) with preset id 2, focused on the character.
3. `0x0080bdb0("gang select", char, &orientation copy, 1)`: looks the hashed name up in the store-camera preset table
   `0x022cb378` (stride `0x3c`, count `0x022cbe40`) and places the camera relative to the character; with no match and
   the last argument set, falls back to the preset `"weapon default"` (`0x0080ba30`).
4. `0x00580420()` clears bit 1 of `0x013c88a0`; `0x005dc900(8.0, 0)` (float at `0x0115df88` = 8.0, constant read);
   byte `0x022cd9e7` = 1 (cleared by the store init `0x00811290`, read by `0x008119a0`); `0x005dd190(0)` writes byte
   `0x012ec141` = 0; `0x008103c0(0)`: for each of the four preview slots that resolves (same kind/alive test), the object
   show/hide helper `0x008ccb90(obj, 0, 1, 0)` — with tranche 13's debris use (`…, 1, 1, 0` on deactivate) this is HIGH
   CONFIDENCE **show all four preview characters**.

**Store camera `0x00567f80(on, presetId, a, b)` (CONFIRMED — listing):** `on` = 1: clears `0x012e3d20` to −1; records the
handle of `a` (or a default pair at `0x0111a580`) in `0x013c9140`; focus = `b`, else `a`, else the local player; copies
the focus position (`+0x40`) into `0x013c91a0` and hands it to `0x005341a0` on `0x013c90a0`; then, **only if
`presetId` differs from the current one (`0x012e3d10`)**, saves the float/byte pair read by `0x005dc930` into
`0x013c9134`/`0x013c9090` (flag `0x013c9131` = 1), saves the current camera mode `0x013c87f0` into `0x013c9120`, and
switches to camera mode **`0x10`** (blend 0.4 s, `0x005649d0`). `on` = 0: restores the saved pair (if `0x013c9131`), blends
back to the saved mode, and sets `0x012e3d10` = −1. `0x00567c30()` is "camera mode is `0x10`".

`0x005dc900(f, b)` writes `0x012ec224` = max(f, 0.15) and byte `0x012ec216` = b (constant read: `0x0111643c` = 0.15).
Readers of `0x012ec224`: `0x005ddf20`, `0x005e2290`, `0x005e23a0` (global xref run). Its meaning is OPEN (HYPOTHESIS: a
camera distance/near-plane style tuning value for the store view).

### B.7 `store_gang_begin_exit` → `0x0080fd10`

**Arguments:** none read. **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing):** if byte `0x022cd9e6` is set (set by the store init `0x00811290`, which also saves the
values), restores `0x005dc900(float 0x022cd9b4, byte 0x022cd9b0)` and clears the byte; then, if the camera is in mode
`0x10` (`0x00567c30`), leaves the store camera (`0x00567f80(0, 0, 0, 0)`) and clears `0x013c88a0` bit 1 (`0x00580420`).

Note (CONFIRMED structure): B.6 and B.7 are not symmetric — B.6 shows the preview characters and sets `0x022cd9e7`, B.7
neither hides them nor clears `0x022cd9e7` (the next store init clears it). Safe to call when the store camera is not
active (both steps are gated).

### B.8 `store_gallery_upload_character` → `0x0080f170`

**Arguments:** none read. **Return:** 0 values.
**Body:** arms the timer `0x01300720` with the delay `0x0130071c` (= **1000 ms**, constant read; `0x00d9e140`) and sets
the gallery state `0x022cd884` = **2**. CONFIRMED.

The state machine around it (global xref run): `0x022cd884` is written 0, 1 (`0x0080fb40`), 2 (here), 3 (`0x0080f6b0`
when it finds state 2 and the timer expired, which also clears the timer), 4 and 5 (B.9), and read at `0x0080f209`,
`0x0080f3a1`, `0x0080f3e7`, `0x0080f481`, `0x0080f6e9`. So "upload" here only **schedules** the upload one second later;
the actual upload runs in `0x0080f6b0` (OPEN — not dumped).

### B.9 `store_gallery_download_show_list` → `0x0080fa00`

**Arguments:** none read. **Return:** exactly 1 boolean. CONFIRMED.

**Body (CONFIRMED — listing, second follow-up dump):**
- If the download state `0x02946e3c` is 3 or 4 (`0x00bd1450`, "busy") → returns **false**, nothing else.
- Otherwise: `0x00bd1770()` — only when the state is 0: rebuilds the "character customization cache" (`0x00bd1600`: frees
  and reallocates a `0x40000`-byte block from the allocator `0x01493a78`, then fills 20 entries of `0x64` bytes at
  `0x02946e4c`, each `+0x60` from a virtual `+0x38` call; on any failure frees everything and leaves `0x02946e4c` = 0),
  clears `0x02946e40`, sets the state to 3 and queues request slot 4 (`0x00bcee90`: a sequence number in
  `0x02946d18[4]` if not already pending). Then gallery state `0x022cd884` = **5**, opens a notice with cancel line via
  `0x007c3e60("MENU_TITLE_NOTICE", "MENU_CHARACTER_DOWNLOAD_WAITING", 0, 0, 0)` (both localized by `0x0084a1b0`), stores
  its handle (`+0x12c`) in `0x022cd880`, and returns **true**.

**Crash shape (CONFIRMED shape — same as tranche 09 B.12):** `0x007c3e60` returns null when the four-slot dialog pool is
exhausted (`0x007c3310`), and the root reads `null + 0x12c` at `0x0080fa63` with no check.

### B.10 `store_gallery_display_character` → `0x0080fa80`

**Arguments:** 1 number = list index (truncated). **Return:** 0 values.
**Body:** `0x00bd14e0(index)` (through the thunk `0x00d27530`): when the index is below the entry count `0x02946e50`
(**unsigned** compare, so negative indices are rejected), and the entry pointer at `0x02946e4c + index·0x64 + 0x60` is
non-null and passes `0x00b99100`, it calls `0x00b97740(entry)` (HYPOTHESIS: apply the downloaded appearance to the preview
character). Otherwise nothing. CONFIRMED — bounded, no crash shape.

### B.11 `store_crib_init_crib_garage` → `0x0080e3e0`

**Arguments:** 1 number = vehicle class/type id (truncated). **Return:** 0 values.

**Body (CONFIRMED — listing):** needs a local player and the player's current crib (player `+0x209c`, `interp_tabs.md`
Q14.3), both null-checked. `0x005f0f90(type)` on the crib picks a garage spawn node (below); if one is found: `0x022ccfd4`
= type, `0x022ccfd0` = 1, and the player is **teleported** to the node's position (`+0x40..+0x48`) keeping the player's own
orientation (`+0x4c`): `0x009e2220(player, &pos, &player+0x4c, 1, 1, 0, 0, 1, 0, 0)`.

**`0x005f0f90(type)` (this = crib) — CONFIRMED, closes the spec's "OPEN — 0x005f0f90's internal body":** walks the crib's
node handle list (count `+0xa8`, 8-byte handles at `+0xac`); a node qualifies when it resolves (kind `+0x9` bit `0x10`,
alive) and its `+0x7c` equals `type`. If the crib's key array is empty (`+0xb0` = 0) the **first** qualifying node is
returned. Otherwise the key array (`+0xb4`, one dword per node) must be exactly as long as the node list (`+0xb0` ==
`+0xa8`), else **0**; up to three qualifying nodes are kept with their keys; a single kept node with key −1 is returned;
otherwise the node whose key is the largest one not above (crib level `+0x140` + 1) is returned (smallest non-negative
difference), or 0. HIGH CONFIDENCE: keys are the upgrade level at which a garage spot becomes available. Only the first
three qualifying nodes are considered (CONFIRMED; the local buffer has exactly three 8-byte slots and the store is
guarded, so no overflow).

### B.12 `shop_purchase_purchase_shop` → `0x00a03490`

**Arguments:** none read. **Return:** 0 values. Registered alone by `0x00a03600` (section A). CONFIRMED.

**Body (CONFIRMED — listing, follow-up dump):**
1. `shop = 0x00a02ce0(trigger handle 0x0268d2d4)`: walks the world-object list `0x03171a64` (count `+0x180`, 16-bit
   indices `+0x178`, objects `+0x58`) for the object whose trigger handle `+0xf0/+0xf4` resolves (kind `+0x7` bit `0x02`
   = trigger, alive) to the **same** object as the trigger in `0x0268d2d4`; returns it, or **null**.
2. Price check: the shop-type record `shop +0x88` has the price in **dollars** at `+0x10`; if local-player cash
   (`+0x1ca0`, cents) ÷ 100 is below it → return, nothing done.
3. Pay: `0x009601d0(price)` → cents; amount = cash − price·100 (saturating); `0x0094a800(player, &amount)` (front matter:
   absolute set when local, else a `"human"/"cash"` message to the owner).
4. `0x00a01680(shop)`: once only (byte `+0x92` bit 0), marks the shop owned, increments the owned counter
   `[+0x88] +0xc` while below the total `+0x8`, and posts stat/achievement events (`0x00710e40(0x17)`,
   `0x00710ed0(0x45)`, `0x007059f0(9)`; meanings OPEN).
5. Disables the purchase trigger (`0x0093bde0(this = trigger 0x0268d2d4, 3)`, Q14.3's trigger "disable").
6. `0x005e8fb0(&shop handle, 0)` — Q14.6's owned-object notice (on the host, opcode `0x45` sub-type `0x1d` broadcast; then
   the local per-object update).
7. If both camera anchors resolve (`0x00a02380`: handle `+0xf8/+0xfc`; `0x00a023f0`: handle `+0x100/+0x104`; kind `+0xb`
   bit `0x02`, alive): starts the property **fly-by** from the first anchor's position/orientation to the second's
   (`0x0057eb10`, B.4). Then `0x00bd2b20(shop)` (only in the `"sr3_city"` world, a switch on the shop-type kind `+0x2c`
   0..5; HYPOTHESIS: per-property-kind ownership bookkeeping) and `0x00bd97b0(shop)` (not dumped).

**Crash shapes (CONFIRMED — listing):**
- **Null shop:** step 1 returns null when `0x0268d2d4` is zero, stale, or no world object references that trigger, and the
  root reads `null + 0x88` at `0x00a034d2` with **no check**. Reachable whenever the script calls this outside a live
  shop-trigger context (e.g. a second call after step 5 disabled the trigger is still fine — the handle remains — but a
  call after the trigger object streamed out is not).
- **No local player:** `player + 0x1ca0` is read at `0x00a034ce` without a check (shape only).
- No double-purchase guard beyond the cash check: calling twice charges twice; step 4's owned bit makes the counter and
  stat events once-only, but steps 3, 5, 6 and 7 repeat (CONFIRMED structure).

---

## C. Spawning — 7 functions

### C.1 `spawning_boats` → `0x00a5dd70`

**Arguments:** 1 boolean (read unconditionally; missing = false). **Return:** 0 values.

**Body (CONFIRMED — listing):** calls the return-1 stub `0x00d1def0` (result discarded), then true → `0x00905ef0(1)`
sets byte **`0x0260938d`** = 1; false → `0x00905f20(1)` clears it. (Both helpers take an index into the 8-byte array
`0x0260938c..0x02609393`; `0x00905ef0(−1)` would set all eight, `0x00905f20(−1)` does nothing — an asymmetry not reachable
from this binding, which always passes 1.)

**What the byte is (CONFIRMED — second range run):** the ambient-spawn reset `0x00908fb0` sets all eight bytes to 1 except
byte 0 (preserved), and **registers `0x0260938d` with the session-synced-variable mechanism `0x0086d770` under the name
`"os_boat"`** (1 byte, authority flag = "this machine is host"); its neighbour `0x02609395` is registered as
`"os_parking"`. The global xref run finds **no direct reader** of `0x0260938d`. So this is the host-authoritative,
replicated "ambient boats may spawn" switch; whatever consumes it reads it through the registered pointer (OPEN).

**Network note (CONFIRMED structure; consequence HYPOTHESIS):** unlike tranche 13's `audio_suppress_ambient_player_lines`,
there is **no host gate**: a co-op client's call writes its local copy, which the host's replicated value presumably
overwrites on the next sync.

### C.2 `spawn_region_min_spawn_dist` → `0x00a5dc40` and C.3 `spawn_region_min_spawn_dist_reset` → `0x00a5dc70`

**C.2 arguments:** 1 number = distance (as float). **C.3:** none. **Return:** 0 values. CONFIRMED.

- C.2: `0x00931600(d)` stores **d²** into `0x013092ec`.
- C.3: `0x00931620()` stores the constant `0x011173a4` = **2500.0** (already squared: 50 m) — the same value as the
  file image of `0x013092ec`.

**Reader (CONFIRMED — second follow-up dump):** `0x00931980` (called from `0x005ed7d0`) reads it twice, comparing it with
the squared distance from a candidate spawn point to the local player (`0x009da4e0`) and to the remote co-op player
(`0x009df3d0`); a candidate **closer than the minimum** is rejected. HIGH CONFIDENCE: this is the ambient spawn-region
chooser.

**Edges (CONFIRMED arithmetic):** a negative distance squares to the same positive value; a NaN distance makes every
"too close" comparison false, which disables the minimum entirely. No crash shape.

### C.4 `spawn_override_set_override_category_for_hood` → `0x00a5fb90`

**Arguments:** 1 string = neighbourhood name; 2 string = spawn category name. **Return:** 0 values.

**Body (CONFIRMED — listing):** hood = `0x00624610(name)` (registry `0x02442750`, kind `+0x7` bit `0x40`), alive; else
nothing. Category = `0x00be5940(name 2)` = name hash (`0x00d9e8b0`) looked up by `0x00be5220` in the category table
`0x029a9114` (count `0x029a9110`, stride `0x60`, hash at `+0x4`), **or null**. Then `0x0084c150(category)` on the hood:
sets hood byte `+0x64` bit `0x04`, stores the category at `+0x70`, and — **on the host only** — broadcasts opcode `0x45`,
8-bit sub-type **`0x29`**, 3-bit value 1, the hood's identity and the category's hash (`category +0x4`).

**Crash — CONFIRMED (listing):** the category pointer is **not checked** anywhere on this path. An unknown, misspelt or nil
category name gives null, and:
- **on a host with a session**, `0x0084c150` reads `null + 0x4` at `0x0084c26b` immediately — **null read**;
- **otherwise** (no session, or a client), a null override is stored with bit `0x04` set. The join snapshot `0x0084c790`
  (called from `0x008ba5d0`; it serialises the global override and every hood with bit `0x02`/`0x04`) later reads
  `[hood +0x70] + 0x4` at `0x0084c972` — a **deferred null read** the moment the snapshot is built (HYPOTHESIS: when a
  co-op partner joins). The receive side `0x0084e0e0` can store the same null on a client, from a hash its own category
  table does not have.
- The per-hood category getter `0x0084aed0` (called from `0x00911770`) returns the stored null to the spawner; whether
  `0x00911770` tolerates it is OPEN.

Contrast the **global** setter (C.6), which checks the lookup result and ignores an unknown name.

### C.5 `spawn_override_clear_override_for_hood` → `0x00a5fb40`

**Arguments:** 1 string = neighbourhood name. **Return:** 0 values.
**Body:** for a live hood (as C.4), `0x0084bfc0()`: clears `+0x64` bit `0x04` and `+0x70`, and on the host broadcasts the
same `0x45`/`0x29`/1 record with the hood's identity and the **"none" sentinel hash** (`0x029c9964`) in place of a category.
CONFIRMED. The receiver maps the sentinel back to this clear (`0x0084e0e0`, jump to `0x0084bfc0`).

### C.6 `spawn_global_override_set_category` → `0x00a5dba0` and C.7 `spawn_global_override_clear_category` → `0x00a5dbd0`

**C.6 arguments:** 1 string = category name. **C.7:** none. **Return:** 0 values. CONFIRMED.

- C.6: `0x0084b280(name)`: looks the category up (`0x00be5940`) and, **only if found**, stores it in `0x0242ddd8`.
  An unknown or nil name leaves the previous override in place.
- C.7: `0x0084b2a0()`: `0x0242ddd8` = 0.

**Precedence (CONFIRMED — `0x0084aed0`, this = hood):** a non-null global override `0x0242ddd8` wins everywhere; else a hood
with `+0x64` bit `0x04` uses its script override `+0x70`; else bit `0x02` selects `+0x6c`; else the hood's default `+0x68`.

**Network (CONFIRMED structure):** neither C.6 nor C.7 sends anything. The global override reaches a client only inside the
join snapshot `0x0084c790` (type 3 record) — so a **mid-session** change is not replicated to an already-joined client
(HYPOTHESIS: unless something else re-sends the snapshot; `0x008ba5d0`'s trigger is OPEN).

---

## D. Characters and AI — 3 functions

### D.1 `squad_enable` → `0x00a5ff00`

**Arguments:** 1 string = character name; 2 boolean (read unconditionally; missing = false). **Return:** 0 values.

**Body (CONFIRMED — listing):** resolves with the plain character resolver `0x005e4dd0` (no `#PLAYER#` / `#FOLLOWER#`
handling), alive; else nothing. Then:
1. Sets bit `0x20` of byte `+0xaf` of the resolved object to **not enable** (cleared on enable, set on disable).
2. Virtual `+0x70` on the object (the front matter's "human" step); null → stop.
3. enable → sets bit `0x40` of byte `+0x316`. disable → `0x009d95a0(char, 0)` then clears that bit.

**`0x009d95a0(char, flag)` (dumped at depth 1, CONFIRMED structure):** if the character is not locally owned
(`0x008addb0` false) and no request is pending (`+0x1de8`), sets `+0x1de8` = 1 and sends opcode `0x41` sub-id 2 with the
character and `flag` to its owner (callback `0x009d9540`). Locally: finds the character's squad leader
(`0x0097e7b0(char, 1)`: the leader handle of the record from `0x005083f0`, or 0 when the leader is the character itself);
**only if the leader is a player** (`0x00853b30(leader, 0x21)`) it restores the character's own stats (health fraction,
`+0x984`/`+0x980`/`+0x1cac`, `0x0094cfc0`, and the three records at `+0x988`/`+0x98c`/`+0x990`), removes it from the
player's list (`0x009dd7b0`), releases `+0x15f4`, and calls `0x00653be0(char, leader)`; in every local case it then calls
`0x00508e90(char, 1)` and `0x009d5fd0(0)`, and re-arms a 300 s timer at `+0x1dd8` when `+0x1e02` bit 2 is set.
HIGH CONFIDENCE: **"remove from the player's crew (dismiss a homie/follower)".**

**Asymmetry (CONFIRMED):** `squad_enable(name, false)` dismisses the character from the player's crew; `squad_enable(name,
true)` only flips two flag bits and **does not re-add** it. Readers of `+0xaf` bit `0x20` and `+0x316` bit `0x40` are OPEN
(HYPOTHESIS: "may join a squad" style permissions).

### D.2 `set_attack_peds_flag` → `0x00a5cd90`

**Arguments:** 1 string = character (`0x00a28150`: `#FOLLOWER#` or a character name — not the `#PLAYER#` chain);
2 nil-gated boolean, **default true**. **Return:** 0 values.

**Body (CONFIRMED — listing):** if resolved, `0x004dff80(flag)` with ECX = character `+0x2b0` (the AI-data sub-object):
null `this` tolerated; double-gate on the handle at sub-object `+0x610/+0x614`; when the gates say "apply locally" it
writes the flag into **bit 3 (`0x08`) of byte `+0x8` of the sub-object** (character `+0x2b8`); otherwise, if the owner
exists, it sends opcode `0x46` tagged `"human_ai_data"` / `"ai_force_flagsattack_pedestrians_on_sight"` with the handle
and the boolean to the owner. **With the gates false and no owner it does nothing** (CONFIRMED; note this refines the
"otherwise writes" wording of tranche 13 B.3 for this setter family — worth re-checking `0x009493e0` against the same
branch).

### D.3 `skydive_move_to_check_done` → `0x00a5f970`

**Arguments:** 1 string = character; 2 string = target name; 3 boolean = "target is a point list" (read
unconditionally); 4 number = point index (truncated). **Return:** exactly 1 boolean ("done"). CONFIRMED.

**Body (CONFIRMED — listing):**
- Target point: list mode → `0x0062a1f0(name)` (kind `+0x8` bit `0x02`), alive, and index below the count `+0x33c`
  (**unsigned**, so negative is rejected) → point = `0x00930e90(index)` = object `+0x3c + 12·index`; any failure → **true**.
  Single mode → `0x005982e0(name)` (kind `+0xb` bit `0x02`), alive → point = object `+0x40`.
- Character `0x00a281a0(name, 0)`; unresolved → true. Not in state `+0xcc8` == **14** (`0x009adb00`, HYPOTHESIS:
  skydiving) → true. `0x0095d740(char)` true → true (it checks `+0xda0`, `+0x13c4` bit 2, `+0xdb8` and an animation
  lookup `0x004b4320` on `[+0xd24]+0x10`; meaning OPEN).
- Otherwise → **true iff the squared distance (`0x00da1330`) from character `+0x40` to the point is below 4.0**
  (constant `0x012a2f6c` = 4.0, constant read), i.e. within **2 m**.

**Crash — CONFIRMED (listing):** in single-point mode the "not found" case is computed as **null + 0x40** *before* the
null test: `0x00a5fa25` clears the object to 0, `0x00a5fa27` forms `0 + 0x40` = `0x40`, and the test at `0x00a5fa2a`
sees a non-zero pointer and continues. If the character resolves, is in state 14 and passes `0x0095d740`,
`0x00da1330` then reads floats from **address `0x40`..`0x48`** — an access violation. Trigger: arg 3 false and arg 2 nil,
misspelt, despawned or dead **while the character is mid-skydive**. The list-mode branch handles the same failure
correctly (returns true), which is evidently the intended behaviour.

---

## E. UI helpers — 3 functions

### E.1 `sfx_use_load_images` → `0x0059fb90`

**Arguments:** none read. **Return:** exactly 1 boolean = byte `0x0149365c`. CONFIRMED.

Already characterised in `spec-lua-api-behaviour.md`'s screen-fade table (the `0x0149365c` rows, job `…-nnlt`):
the byte is set to 1 unconditionally by the engine start-up `0x005d1a30` and never cleared, so this returns **true** in
the retail executable. Re-confirmed here: one write (`0x005d1a66` in `0x005d1a30`) among 96 references. Team B can bind it
as a constant `true`.

### E.2 `set_char_in_string` → `0x00844870`

**Arguments:** 1 string `s`; 2 number `i` (truncated); 3 string `c`. **Return:** exactly 1 string. CONFIRMED.

**Body (CONFIRMED — listing):** with L = `strlen(s)`:
- 0 ≤ i < L: result = `s` with byte i replaced by `c[0]` (length L).
- i ≥ L: `s` padded with spaces up to index i, then `c[0]` at i (length i + 1).
- i < 0: `−i` spaces are **prepended** and `c[0]` replaces the first of them (length L − i).

It builds the result in a stack buffer (`__alloca_probe_16`, size = L + 1 + padding), fills it with spaces (`memset`
`0x20`), copies `s` (`strncpy`), stores `c[0]`, terminates, and pushes it (`0x00dfe420`). **Indices are 0-based**, not
Lua's usual 1-based. An empty `c` writes a NUL at i, truncating the result there.

**Crash shapes (CONFIRMED — listing):**
- **Arg 1 not a string** → `strlen(null)`: null read at `0x008448d0`.
- **Arg 3 not a string** → `c[0]` read through null at `0x0084492a`.
- **Unbounded stack allocation:** the buffer size grows with |i| with no cap. A large index (anything near the remaining
  stack, e.g. 10⁶) overruns the stack guard → stack-overflow exception; `i` = −2³¹ (what the truncation returns for
  NaN or out-of-range numbers) makes the size arithmetic go negative/huge. Reachable from script data alone.

### E.3 `screen_capture_preview_should_upload` → `0x007afeb0`

**Arguments:** 1 boolean (read unconditionally). **Return:** 0 values. Registered alone by `0x007aff80`. CONFIRMED.

**Body (CONFIRMED — listing, follow-up dump):** `0x008703f0(4)` (through the thunk `0x0086fe10`; HYPOTHESIS: the
platform's user-generated-content privilege check, privilege 4).
- **Privilege granted and arg true** → capture state `0x02242798` = **7** (upload). (The neighbour at `0x007affb0` writes
  the same state.)
- **Privilege denied** → a warning dialog `0x007c3d80("MENU_TITLE_WARNING", "@USER_CONTENT_PRIV_DENIED", priority 2)`
  (result not used, so pool exhaustion is harmless here), **then the cancel path**.
- **Cancel path** (privilege granted and arg false, or denied): resets the capture object `0x022427c0`
  (`0x00dad590(0, 0)`), clears bit `0x20` of `0x02242735`, state `0x02242798` = 0, `0x022427ac` = −1, bytes
  `0x022427a8`/`0x0224279c` = 0, `0x022427a0` = 0; and if the counter `0x022427a4` is positive, decrements it **once** and
  calls `0x00707540(0x13)`, `0x008b7260(0)`, `0x007f1070(0)` (HYPOTHESIS: leaves the capture/pause mode it entered).

Note (CONFIRMED structure): a denied privilege silently turns "upload" into "cancel" — the script cannot tell the two
apart from this call.

---

## F. Method notes for the follow-up dumps

- Range run: `0x00a03600-0x00a03640`, `0x007aff80-0x007affc0`, `0x008152a0-0x00815380`, `0x00812590-0x008125f0`,
  `0x008111f0-0x00811290`, `0x0080fab0-0x0080fb40`, `0x0080e540-0x0080e5c0`, `0x005a01d0-0x005a0260`.
- Xref run: `0x00a03600 0x007aff80 0x008152a0 0x00812590 0x008111f0 0x0080fab0 0x0080e540 0x005a01d0` (each called once
  from `0x008430f0`) and `0x00845aa0` (called once from `0x008489e0`).
- Follow-up dump (depth 1, `maxfuncs:25`): `0x00a03490 0x007afeb0`.
- Second follow-up dump (depth 0, `maxinsn:900`): `0x006d9b90 0x006d9cb0 0x0057ece0 0x0045f1a0 0x0057eb10 0x0057ea90
  0x007b4cb0 0x007e1f40 0x007bfd10 0x005ea420 0x0080b9d0 0x0080bdb0 0x005dd190 0x008103c0 0x005dc930 0x00bd14e0
  0x00bd1600 0x00bcee90 0x00be5220 0x0084aed0 0x0084c790 0x0084e0e0 0x00931980 0x0097e7b0 0x00d9e8b0 0x004588f0
  0x00710910`.
- Global xref run (`xrefs:60`): `0x022cdf14 0x022cdf18 0x0260938c 0x0260938d 0x022cd9e6 0x022cd9e7 0x022cdac8 0x012ec224
  0x022cd884 0x022cd880 0x01300720 0x022cdbf4 0x01300834 0x013092ec` (plus one mistyped address with 0 uses, ignored).
- Constant read: `0x012a2f6c` = 4.0, `0x0115df88` = 8.0, `0x0111643c` = 0.15, `0x0130071c` = 1000, `0x01126010` /
  `0x01126014` = 0, 0, `0x011192f8` = 0.4.
- Second range run: `0x00909190-0x00909210` (the `"os_parking"`/`"os_boat"` registrations) and `0x00820940-0x00820990`
  (the `0x022cdf18` reader).
- Return-1 stub met in this tranche: `0x00d1def0` (`MOV EAX,1; RET`) — CONFIRMED.

---

## G. Cross-function observations

1. **Three more registrars with pointer-before-name or table shapes, all under the UI bring-up.** Two more one-name
   registrars (`0x00a03600`, `0x007aff80`) push the function before the name, exactly like tranche 13's `0x0067d660`.
   Any future `lua`-mode dump whose only "use" is a `PUSH name` immediately before `CALL 0x00dfe830` should be re-read with
   a range dump of its registrar rather than trusting the dumper's offset guess.
2. **Pointer computed from null before the null test** (`skydive_move_to_check_done`, D.3) is a new crash class for this
   series: the code forms `object + 0x40` and tests the *sum*, so the null case survives. Worth grepping for the same
   `LEA reg,[reg+disp]` / `TEST reg,reg` ordering in sibling `*_check_done` natives.
3. **Unchecked lookups are again the dominant crash class:** shop lookup (B.12), category lookup on the hood path (C.4,
   immediate on a host and deferred elsewhere), dialog pool (B.9, same as tranche 09), and the two string arguments of
   `set_char_in_string` (E.2). The **global** category setter (C.6) checks its lookup while the per-hood one (C.4) does
   not — a direct template for the fix.
4. **Charge-then-validate:** `store_stronghold_game_purchase_upgrade` deducts cash before the upgrade routine's own
   level/session/host gates (B.3); `shop_purchase_purchase_shop` has no "already owned" guard on the payment (B.12).
5. **Host-only state with unguarded writers:** `spawning_boats` writes a host-authoritative session-synced byte
   (`"os_boat"`) with no host gate (C.1); the per-hood override replicates from the host (C.4/C.5) while the global
   override does not replicate at all mid-session (C.6/C.7).
6. **Engine-unread or dead state:** `store_vehicle_do_return_to_crib`'s `0x022cdf14` has no writer, so one of its branches
   is dead (B.1); `0x0260938d` and `0x013092ec` have no direct readers beyond the registered pointer / the spawn chooser.
   `sfx_use_load_images` is a constant true in retail (E.1).
7. **Return shapes a script can observe:** `store_vehicle_allow_garage`, `store_gang_is_unlocked`,
   `store_gallery_download_show_list`, `skydive_move_to_check_done` and `sfx_use_load_images` each return exactly one
   boolean; `set_char_in_string` returns one string (0-based index semantics); the other 19 return nothing.
8. **Index spaces for a binding layer:** B.10 arg 1 = gallery list index (unsigned-bounded by `0x02946e50`); B.11 arg 1 =
   vehicle class compared with garage-node `+0x7c`; D.3 arg 4 = point index (unsigned-bounded by `+0x33c`); E.2 arg 2 =
   0-based byte index (unbounded, negative prepends).
9. **New camera pieces:** the store camera (`0x00567f80`, camera mode `0x10`, presets in `0x022cb378`) and the property
   fly-by (`0x0057eb10` start, `0x0057ee20` freeze, `0x0057ea90` exit; camera mode `0xf`) are shared by the gang, stronghold
   and shop stores.

## H. OPEN

- B.1: whether anything writes `0x022cdf14` through a base pointer; what `0x005f62c0` (skipped when `0x022cdf18` is set)
  does.
- B.2: meaning of mission state 8 (`0x014c7a14`); which binding owns `0x005f87c0` (the 12-name list without "m04").
- B.3: whether the store UI can open on a never-levelled stronghold (`+0x140` = −1, price read from `+0xe0`) or offer a
  purchase at the maximum level; what `0x008126b0`'s sub-states 2/3/4 mean.
- B.4: the HUD screen `0x39`'s identity; whether real stronghold data ever has more than 256 unlocks for one level.
- B.5: the writer of player `+0x1d99` bit 1.
- B.6: the meaning of `0x012ec224` (min 0.15) and byte `0x012ec216`; of byte `0x012ec141`; of `0x013c88a0` bit 1; who
  reads `0x022cd9e7` (`0x008119a0`) and why.
- B.8: the body of `0x0080f6b0` (the deferred upload).
- B.10: `0x00b99100` and `0x00b97740`.
- B.12: `0x00bd2b20`'s per-kind cases and `0x00bd97b0`; the stat ids `0x17`/`0x45`/`9`.
- C.1: who consumes `"os_boat"` (and the other `os_*` bytes); whether the host's sync overwrites a client's local write.
- C.4: whether `0x00911770` tolerates a null category from `0x0084aed0`; when `0x008ba5d0` runs `0x0084c790`.
- D.1: readers of `+0xaf` bit `0x20` and `+0x316` bit `0x40`; the exact effect of `0x00508e90`/`0x009d5fd0`.
- D.2: re-check tranche 13's `0x009493e0` for the same "no owner → no-op" branch.
- D.3: the meaning of `0x0095d740` and of character state 14.
- E.3: the identity of privilege 4 and of the counter `0x022427a4`.

## I. Direct-answer table

| # | name | registrar | handler | status |
|---|---|---|---|---|
| 1 | `store_vehicle_do_return_to_crib` | store vehicle `0x008152a0` (under UI `0x008430f0`) | `0x00813130` | resolved — pops the screen stack twice; other branch dead (no writer of `0x022cdf14`) |
| 2 | `store_vehicle_allow_garage` | store vehicle | `0x008131c0` | resolved — false while one of 13 named missions is active |
| 3 | `store_stronghold_upgrade_end_flyby` | stronghold `0x00812590` (under UI) | `0x00812480` | resolved — freezes the fly-by, shows level-unlock notice (screen `0x39`), 3 s timer |
| 4 | `store_stronghold_game_purchase_upgrade` | stronghold | `0x00812400` | resolved — charges price (reason 11), level+1 via host-only `0x005f1750`; **cash taken even when the upgrade is refused** |
| 5 | `store_gang_is_unlocked` | gang `0x008111f0` (under UI) | `0x008101c0` | resolved — player `+0x1d99` bit 1, false on a co-op client; unchecked player |
| 6 | `store_gang_bg_covered` | gang | `0x00811150` | resolved — enters store camera on preview character, "gang select" preset, shows previews |
| 7 | `store_gang_begin_exit` | gang | `0x0080fd10` | resolved — restores saved camera tuning, leaves store camera |
| 8 | `store_gallery_upload_character` | gallery `0x0080fab0` (under UI) | `0x0080f170` | resolved — schedules upload in 1000 ms (state 2) |
| 9 | `store_gallery_download_show_list` | gallery | `0x0080fa00` | resolved — false if busy; else starts download, waiting dialog; dialog-pool null read |
| 10 | `store_gallery_display_character` | gallery | `0x0080fa80` | resolved — bounded index → apply downloaded entry |
| 11 | `store_crib_init_crib_garage` | crib `0x0080e540` (under UI) | `0x0080e3e0` | resolved — picks garage node by class/level, teleports player; `0x005f0f90` body documented |
| 12 | `squad_enable` | gameplay `0x00a20840` | `0x00a5ff00` | resolved — false dismisses from the player's crew; true only sets flags |
| 13 | `spawning_boats` | gameplay | `0x00a5dd70` | resolved — writes host-synced `"os_boat"` byte, no host gate |
| 14 | `spawn_region_min_spawn_dist_reset` | gameplay | `0x00a5dc70` | resolved — min spawn distance² = 2500 (50 m) |
| 15 | `spawn_region_min_spawn_dist` | gameplay | `0x00a5dc40` | resolved — min spawn distance² = arg² |
| 16 | `spawn_override_set_override_category_for_hood` | gameplay | `0x00a5fb90` | resolved — per-hood category override, host broadcast; **unknown category → null read** |
| 17 | `spawn_override_clear_override_for_hood` | gameplay | `0x00a5fb40` | resolved — clears per-hood override, host broadcast |
| 18 | `spawn_global_override_set_category` | gameplay | `0x00a5dba0` | resolved — global override (checked lookup), not replicated |
| 19 | `spawn_global_override_clear_category` | gameplay | `0x00a5dbd0` | resolved — global override = none |
| 20 | `skydive_move_to_check_done` | gameplay | `0x00a5f970` | resolved — true within 2 m or when not applicable; **read at address 0x40 on a missing single target** |
| 21 | `shop_purchase_purchase_shop` | `0x00a03600` (under UI) | `0x00a03490` | resolved — pay, own, disable trigger, fly-by; **null shop read** |
| 22 | `sfx_use_load_images` | sfx `0x005a01d0` (under UI) | `0x0059fb90` | resolved — constant true in retail (already in spec table) |
| 23 | `set_char_in_string` | UI helper `0x00845aa0` (under `0x008489e0`) | `0x00844870` | resolved — 0-based char replace with space padding; null reads, unbounded alloca |
| 24 | `set_attack_peds_flag` | gameplay | `0x00a5cd90` | resolved — default true; double-gate AI flag bit 3 of character `+0x2b8` |
| 25 | `screen_capture_preview_should_upload` | `0x007aff80` (under UI) | `0x007afeb0` | resolved — privilege-checked upload state 7, else cancel |

## J. Crash-shaped and logic defects (summary)

| # | where | defect | status |
|---|---|---|---|
| 1 | D.3 `0x00a5fa25`–`0x00a5fa2a` → `0x00da1330` | missing single-point target → pointer `0 + 0x40` passes the null test → read at address `0x40` while the character is skydiving | CONFIRMED |
| 2 | C.4 `0x0084c26b` | unknown/nil category → `0x0084c150(null)` reads `null + 0x4` on a host with a session | CONFIRMED |
| 3 | C.4 → `0x0084c790` `0x0084c972` | same, without session or on a client: null override stored, read later by the join snapshot | CONFIRMED shape (trigger timing HYPOTHESIS) |
| 4 | B.12 `0x00a034d2` | shop lookup `0x00a02ce0` returns null (stale/absent trigger) → `null + 0x88` | CONFIRMED |
| 5 | E.2 `0x008448d0`, `0x0084492a` | non-string arg 1 / arg 3 → null reads | CONFIRMED |
| 6 | E.2 `0x00844903` | stack buffer sized by an unbounded script index (large or NaN/out-of-range) → stack overflow | CONFIRMED unchecked |
| 7 | B.9 `0x0080fa63` | dialog pool exhausted → `null + 0x12c` (tranche 09 shape) | CONFIRMED shape |
| 8 | B.5 `0x008101d4`; B.12 `0x00a034ce`; B.3 → `0x0094da81` | local player used without a null check | CONFIRMED shape |
| 9 | B.4 `0x008124ea` | unlock matches copied into a 256-slot stack array without a cap | CONFIRMED unchecked, data-bounded |
| 10 | B.3 | cash charged before the upgrade's level/session/host gates (max level, client, no session) | CONFIRMED structure |
| 11 | B.3 | price index `+0xe4 + 4·level` unchecked for level −1 | CONFIRMED arithmetic, reachability OPEN |
| 12 | B.12 | repeat calls charge again; trigger/notice/fly-by repeat | CONFIRMED structure |
| 13 | C.1 | host-synced `"os_boat"` written without a host gate | CONFIRMED structure, consequence HYPOTHESIS |
| 14 | C.6/C.7 | global spawn override not replicated mid-session | CONFIRMED (no send), consequence HYPOTHESIS |
| 15 | D.1 | `squad_enable(true)` does not undo `squad_enable(false)`'s dismissal | CONFIRMED |
| 16 | B.1 | branch on a never-written global is dead; its flag would never clear | CONFIRMED (direct references) |
| 17 | E.3 | denied privilege reported as a silent cancel | CONFIRMED |
| 18 | C.2 | NaN distance disables the minimum spawn distance | CONFIRMED arithmetic |
| 19 | E.1 | constant true in retail | CONFIRMED |
| 20 | E.2 | 0-based index; negative index prepends spaces | CONFIRMED |

## K. Clean-room check

- Addresses are plain hex throughout; no Ghidra auto-names for functions, globals or labels, no decompiler variable names,
  and no pasted pseudocode. Library routines are named only where Ghidra identified them as standard C runtime
  (`strlen` as the inlined loop's role, `memset`, `strncpy`, `__alloca_probe_16`, `__strnicmp`); short game strings are
  quoted as data.
- Final self-check run on this file with the pattern
  `\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4}|LAB_[0-9a-fA-F]+|FUN_[0-9a-fA-F]+|DAT_[0-9a-fA-F]+)\b`:
  **0 hits** (the agent's own run was interrupted by a session rate-limit kill mid-write before it substituted this
  result in; re-run directly against the finished file after the kill — still 0 hits). The pattern was first checked
  against 22 positive controls (one sample of each auto-named local family, the parameter, register-input, stack-array,
  type-name and the three label-prefix forms), all of which it matched, and 6 negative controls (plain English such as
  "left undefined", "in ECX", a bare `0x…` address), none of which it matched.
- No spec file was edited. The private Ghidra copy `tools\gp_t16` was deleted after the dumps; raw dumps stayed in the
  session scratchpad.
