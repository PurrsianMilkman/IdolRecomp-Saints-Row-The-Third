# Ranking tranche 15 — 25 previously-unspecced Lua-bound names (Team A, 2026-10-03)

Job definition: `D:\Project Crreish\TEAM A\ghidra\jobs\ranking\tranche-15.json`. The job file's own title states **names
301-325 of the 554 unspecced names, Team B call-count order**; this note uses exactly the 25 names listed in that file.
Run locally, read-only, on a private copy of the Ghidra project (`tools\gp_t15` — a complete copy left behind by an
earlier, rate-limit-killed attempt at this same tranche; checked before use to match the master project file for file and
byte for byte, 10 files / 488,965,201 bytes, no lock; deleted afterwards), with the job's own arguments:
`CrreishDump.java <out>/lua lua depth:1 maxfuncs:15 maxinsn:500 <25 names>`. **All 25 names resolved.** Every name string
occurs exactly once in `.rdata`, and for all 25 the handler is the code pointer stored in the slot right after the name
(insn offset +1) — CONFIRMED — disassembly (dump `index.txt`, cross-checked for the UI table against the range run). No
registrar in this tranche uses the "function pointer before the name" shape of tranches 13/16: all three are table-driven
(section A). Follow-up runs on the same private copy, cited by name below:

- "range run": `range` mode on the vehicle-customisation registrar `0x00820aa0-0x00820cd0`;
- "registrar xref": `xref` mode on `0x00820aa0`;
- "follow-up dump 1": depth-0 `func` run on 20 callee addresses (made by the earlier attempt on this same project and
  binary; every file re-read in full for this note — listed in section F);
- "follow-up dump 2": depth-0 `func` run on 13 callee addresses (section F);
- "global xref run 1" (earlier attempt, re-read): `0x01300b80 0x022cf928 0x022cf924 0x022cf8e4 0x022cf8e0` and the bare
  displacement `0x1d84`;
- "global xref run 2": 11 globals (section F); "displacement scan": the bare displacements `0x15a0` and `0x16cc`;
- "constant read": `ptrs` mode on ten 8-byte constants (section F).

Call counts are Team B's (`D:\Crreish-sync\for-team-b\team-b\tools\lua_reconciliation_called_and_registered_1181.tsv`):
**every name in this tranche is 1 call site in 1 script.** Team B tags 11 names `ui` (the nine `vcust_*` and the two
`store_vehicle_*`) and 14 `gameplay`; section A shows the registrars agree exactly.

Labels: **CONFIRMED** = read in the listing or decompile of a dump made for this note; **HIGH CONFIDENCE** = follows
from a dumped call or reference, but the callee body was not dumped or a meaning is inferred from strong usage;
**HYPOTHESIS** = plausible, not settled; **OPEN** = not settled (collected in section H).

**Shared conventions, cited not re-described:** every entry opens with the `lua_gettop` prologue (`0x00dfde50`) and
reads arguments with negative stack indices counted from the bottom (`-N` = arg 1, `1-N` = arg 2, …). Primitives as in
tranche 09/11/13/16 front matter: `0x00dfe210` `lua_tolstring` (non-string reads as null), `0x00dfe1e0` `lua_toboolean`,
`0x00dfe040` `lua_type` (0 = nil), `0x00dfe160` `lua_tonumber` (non-number reads as 0), `0x00ea2596` truncating
float-to-int, `0x00dfe590` `lua_pushboolean`, `0x00dfe3a0` `lua_pushnumber`. "Nil-gated optional argument" = the
standard idiom of tranche 13; a "bare" boolean is an unconditional `lua_toboolean` (missing = false). Resolvers:
`0x00a281e0` the dedicated vehicle resolver (a character name resolves to that character's current vehicle through
`+0x16c0/+0x16c4`, tranche 05-08), `0x00a281a0` generic character chain, `0x00a29200` general object resolver,
`0x004dcf00` handle-pair → object. `0x009da4e0` local player; `0x009df3d0` remote co-op player; `0x0087ba20` session;
"host" = session exists and its `+0x5c` equals `+0x58`. Kind-flag tests through `[0x02cc9900 + 4·(object byte +0x34)]`,
liveness `0x00853b10` ("true = dead"), and the flag test `0x00853b30` (`0x21` = player). Handle resolution:
`0x00458230` on `0x024433a8` with key `0x031d152c`, then the `+0x33` bit `0x10` rejection (tranche 11). Double gate
`0x008ae480`/`0x008837a0` with owner `0x008ae3a0`, network record helpers `0x0086f5f0`/`0x0086f4b0`/`0x0086f110`/
`0x0086f1b0`/`0x0086eb20` as in tranche 13/16. The object destroy primitive `0x00853ea0(object, 0, 0)` is `interp_tabs.md`
Q4; the guarded destroy-by-handle `0x00854410` is `interp_named_object_resolution.md` §1.3. The vehicle "in air"
predicate `0x00ad4040` and its two halves are `interp_tabs.md` Q11. The vehicle occupancy controller (`0x00b08400`,
8 slots of `0x100`), the exit request `0x00af3cd0` and the exit-record initialiser `0x004dccd0` are tranche 06 (A.11/A.12).
The tutorial table (base `0x0151d600`, 36-byte entries, state at `+0x0c`, prompt list head `0x0151d588`, 210-name table
`0x012f5930` read by `0x00717780`) is `interp_dksj.md` Q2 / `interp_jfue.md` Q2. The vehicle store globals (mode flag
`0x022cdf08`, sub-state `0x022cdf0c`, selection `0x022cde00/04`, pending pair `0x014a1d28/2c`, `0x005f8a00`, screen stack
`0x012fced8` with `0x007b4790`/`0x007b3ba0`) are tranche 06 D.1-D.3. The purchase-callback mechanism (`0x005e44c0({4})` on
`0x026e9528`, two strings pushed by `0x00e0cfb0`) and the cash-adjust delegate `0x0094d920(this = player, &cents,
reason)` are tranche 06 C.1/E.4/F.7 and tranche 16 B.3; `0x00960160`/`0x009601d0` saturation is tranche 16's front matter.

**Small new generic facts, CONFIRMED (dumps / constant read):**
- `0x00dad900(f)` rounds half away from zero (adds or subtracts 0.5, constant `0x012a2dc0`, then truncates).
- `0x00ea4e80` is the C runtime's floor routine (SSE2 fast path plus the x87 fallback with the runtime's own
  exception helpers); `0x00ea2b90` is a sibling C-runtime math dispatcher of the same two-path shape (arc-cosine by usage,
  B.10 — HIGH CONFIDENCE for the function, CONFIRMED for the shape).
- `0x00434f10` is a 3-component dot product (receiver in ECX, other vector on the stack).
- `0x00d9fe30` normalises the 3-vector in ECX into its argument and substitutes `(1, 0, 0)` when the length is within the
  tolerance at `0x012a2f88` of zero.
- Constants: `0x01171c60` = 2.2369363307952881 (the metres-per-second → miles-per-hour factor rounded through single
  precision); `0x012a3038` = −1.0; `0x012a2d70` = 1.0; `0x012a2d88` = 255.0; `0x012a2dd8` = 100.0; `0x012a2e28` = 2.0;
  `0x012a30d8` = 0.017453292… (π/180 rounded through single precision); `0x012a3078` = 4294967296.0 (single);
  `0x01243130` = the single-precision sign mask.

---

## A. Registrars — where the 25 names live

Three registrars. CONFIRMED — `index.txt`, range run, registrar xref:

| registrar | reached from | names in this tranche |
|---|---|---|
| gameplay `0x00a20840` | (established) | 14: `vehicle_is_flipped`, `vehicle_in_air`, `vehicle_get_max_speed`, `vehicle_flee`, `vehicle_evacuate`, `vehicle_detonate_random_parked_car`, `vehicle_component_shatter_glass`, `vehicle_component_is_shattered`, `vehicle_allow_ownership_transfer`, `vault_heli_hack_for_m01`, `tutorial_stop_current`, `tutorial_lock`, `turn_to_check_done`, `trigger_allow_dead_guys` |
| vehicle customisation `0x00820aa0` (20 names) | UI `0x008430f0`, call at `0x008431fe` | 9: `vcust_show_nos_hydraulic_warning`, `vcust_set_underglow`, `vcust_select_paint_slot`, `vcust_purchase_performance_upgrade`, `vcust_purchase_paint`, `vcust_purchase_component`, `vcust_preview_rim_sizing`, `vcust_change_top_level_menu`, `vcust_adjust_camera_angle` |
| store vehicle `0x008152a0` (8 names, tranche 16 A) | UI `0x008430f0`, call at `0x008431da` | 2: `store_vehicle_notify_screen_covered` `0x00814060`, `store_vehicle_no_vehicle_exit` `0x00813080` (both already listed in tranche 16's table) |

- `0x00820aa0` returns at once for a null Lua state; otherwise it builds a 20-pair `{name, function}` stack table and
  loops `0x14` = **20** times over `lua_pushcclosure` (`0x00dfe4f0`) then `lua_setfield` (`0x00dfe830`, index `-10002`).
  Full table, for a binding layer (CONFIRMED — range run): `vcust_using_lightset` `0x00842940`,
  `vcust_change_top_level_menu` `0x00818df0`, `vcust_select_paint_slot` `0x0081f2b0`, `vcust_set_camera_pos`
  `0x0081fa80`, `vcust_adjust_camera_angle` `0x0081fb30`, `vcust_preview_component` `0x0081f2e0`,
  `vcust_preview_wheel_sizing` `0x0081f5d0`, `vcust_preview_rim_sizing` `0x0081f750`, `vcust_preview_color` `0x0081f860`,
  `vcust_preview_palette` `0x0081f8d0`, `vcust_purchase_component` `0x0081fd20`, `vcust_purchase_wheels` `0x0081fe70`,
  `vcust_purchase_paint` `0x0081fff0`, `vcust_purchase_performance_upgrade` `0x008200f0`, `vcust_revert_wheels`
  `0x0081fa60`, `vcust_revert_wheel_choice` `0x0081f970`, `vcust_revert_color` `0x0081f890`, `vcust_set_underglow`
  `0x0081f110`, `vcust_show_nos_hydraulic_warning` `0x008202b0`, `vcust_is_camera_tool_mode` `0x00818390`.
- Several of these handlers (`0x0081fd20`, `0x0081fff0`, `0x0081f2e0`, `0x0081fa60`, `0x0081f970`, `0x0081f890`) have
  never been made Ghidra functions in the saved project (the xref runs report their instructions as "no function"); the
  dumper defines them in-session, so their dumps are complete. CONFIRMED.

---

## B. Vehicles — 10 functions (gameplay)

All take the vehicle reference as arg 1 through `0x00a281e0`; an unresolved reference does nothing or returns the
stated default.

### B.1 `vehicle_is_flipped` → `0x00a642e0`

**Arguments:** 1 string (vehicle). **Return:** exactly 1 boolean. CONFIRMED.

**Body (CONFIRMED — listing):** unresolved → false. Otherwise true when the float at vehicle `+0x5c` is ≤ 0.0 (static
`0x0125e270`), **or** when the dot product of the row `+0x58/+0x5c/+0x60` with world up `(0, 1, 0)` — computed in double
as 0·x + y + 0·z — is ≤ 0.5 (static `0x0126d2cc`). Both tests reduce to **"the up-row's vertical component is at most
0.5"**, i.e. tilted 60° or more from upright. HIGH CONFIDENCE that `+0x58..+0x60` is the up row of the orientation (rows
`+0x4c`/`+0x58`/`+0x64`; B.10 uses `+0x64` as forward). A NaN component takes the "≤" branch (unordered compare) and
reads as flipped (CONFIRMED flag semantics; harmless).

### B.2 `vehicle_in_air` → `0x00a626c0`

**Arguments:** 1 string. **Return:** exactly 1 boolean. Unresolved → false; otherwise `0x00ad4040(vehicle)` — the same
predicate `get_char_vehicle_is_in_air` uses (`interp_tabs.md` Q11: no world contact on the body, no wheel reporting
contact, not in water). CONFIRMED.

### B.3 `vehicle_get_max_speed` → `0x00a63fa0`

**Arguments:** 1 string. **Return:** exactly 1 number. CONFIRMED.

Unresolved → **−1.0** (`0x012a3038`). Otherwise the float at vehicle `+0x1d84` × **2.2369363307952881** (`0x01171c60`),
widened to double and pushed. HIGH CONFIDENCE: `+0x1d84` is a top speed in metres per second and the binding returns
**miles per hour** (the factor is exactly the m/s → mph conversion rounded to single precision). Writers of `+0x1d84` by
displacement (global xref run 1: `0x00a7ded0`, `0x00a82fe0`, `0x00a9ee70`, `0x00a9f980`, `0x00d589f0`, an 8-byte store in
`0x00954340`) are not class-resolved (WALLS.md: a bare displacement has no selectivity) — which one sets the value is
OPEN.

### B.4 `vehicle_allow_ownership_transfer` → `0x00a63050`

**Arguments:** 1 string; 2 bare boolean (read before the resolve; missing = false). **Return:** 0 values. CONFIRMED.

**Body:** for a resolved vehicle, bit `0x20` of dword `+0x16cc` := **not** arg 2 (cleared = transfer allowed). No session
gate, no network record. Displacement scan: a set of the same bit in `0x00637e30` (`OR …,0x20`) and a test of it in
`0x008ada30`, which sits beside the ownership gate `0x008addb0` — HYPOTHESIS that `0x008ada30` is the reader that refuses
a migration of network ownership while the bit is set (class of its object not resolved). Note a one-argument call
**forbids** transfer.

### B.5 `vehicle_component_is_shattered` → `0x00a63a60`

**Arguments:** 1 string (vehicle); 2 string (component name). **Return:** exactly 1 boolean. CONFIRMED.

**Body (CONFIRMED — listing):** false unless the vehicle resolves, its byte `+0xbd0` is 1, and the component name is
found. Lookup `0x00a89120(vehicle, name)` (also requires `+0xbd0` == 1): linear, case-insensitive (`_stricmp`) over the
vehicle's component list — count `+0x688`, pointer array from `+0x68c` — comparing each component's name
`[[component +0x80] +0x40]` (null names skipped); index or −1. The answer is `0x00a8eeb0(vehicle handle +8/+0xc, index)`:
index below **92** (`0x5c`, unsigned), the handle re-resolved (kind `+0x6` bit `0x80`, alive), then **bit 23
(`0x800000`) of the component's dword `+0xf0`**. The 92 bound is the array's real capacity: `+0x68c + 92·4` = `+0x7fc`,
where the wheel-set pointer begins (CONFIRMED arithmetic).

**Crash shape — CONFIRMED (listing):** a nil or non-string arg 2 becomes a null pointer that `0x00a89120` passes as the
second operand of `_stricmp` at `0x00a89161`, once per component with a non-null name. The statically linked runtime
validates `_stricmp` arguments; with the default invalid-parameter handler that terminates the process (HIGH CONFIDENCE —
whether the game installs its own handler is OPEN). Same path in B.6.

**Latent (unreachable from this binding):** `0x00a8eeb0` selects its array base with the "select-or-null" idiom (G.2):
when `+0xbd0` is not 1 the base becomes 0 and it reads address `4·index + 4`. The root has already tested `+0xbd0` on the
same vehicle, and this binding is `0x00a8eeb0`'s only caller (CONFIRMED caller list), so it cannot fire here.

### B.6 `vehicle_component_shatter_glass` → `0x00a63cb0`

**Arguments:** as B.5. **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing):** same gates and lookup as B.5, then `0x00a8cd50(component, vehicle, 0, 0, 0)`:
1. Component `+0xf0` bit 23 already set → nothing (idempotent).
2. Builds an effect transform from the component's definition record (`+0x84` when `+0xf0` bit `0x2000`, else `+0x80`;
   its `+0x60`/`+0x70` rows), the component's local position (`+0xdc`/`+0xe0`) and the runtime default orientation
   `0x029f5670`, and hands it to `0x00595de0` (HYPOTHESIS: spawn the shatter effect).
3. `0x00a896a0(component, vehicle, 0x80002)` (walks the component's attachment chain and plays a per-piece effect —
   HYPOTHESIS), sets **`+0xf0` bit 23** (the bit B.5 reads), `0x00abf810(vehicle, component, 0)` (after `0x008cc930(0x8000)`
   sets the component's bit in a per-vehicle bit set at component-list `+0x444` — HYPOTHESIS: a replication dirty mask),
   then `0x00a88980` (component in EDX).

**Not glass-specific (CONFIRMED for the root and `0x00a8cd50`):** nothing tests the component's kind, so any named
component receives the "shattered" bit and the effect.

**Latent wild write (unreachable from this binding):** `0x00abf810` searches the component in the vehicle's list and,
when it is **not** found, keeps index −1 and sets a bit in the byte at list `+0x444 + 0x1fffffff` (CONFIRMED arithmetic,
`0x00abf857` onward). Here the component came from that same list, so it is always found; its four other callers
(`0x00a8eab0`, `0x00a8b2e0`, `0x00a89ed0`, `0x00a89be0`) were not checked (OPEN).

### B.7 `vault_heli_hack_for_m01` → `0x00a62020`

**Arguments:** 1 string. **Return:** 0 values. CONFIRMED.

**Body:** `0x00557270(vehicle)` (this binding is its only caller): only when `+0xbd0` is 1, finds the **first component
named `"body"`** (`_stricmp`, constant `0x0111972c`) and stores its index in vehicle **`+0x15a0`**; no match → unchanged.
Unlike `0x00a89120`, this loop does not skip a null name pointer (data-bounded `_stricmp(null, "body")`). Displacement
scan: the nearest reader is `0x00557e10` (same code neighbourhood) — HYPOTHESIS: the component used as the lifting /
attachment body when the mission-1 vault is carried by the helicopter. Readers OPEN.

### B.8 `vehicle_flee` → `0x00a63ea0`

**Arguments:** 1 string (vehicle); 2 nil-gated string, **default `"#PLAYER1#"`** (`0x01147698`) — the character to flee
from, through `0x00a281a0(name, 0)`. **Return:** 0 values. CONFIRMED.

**Body:** both must resolve; then `0x00b464c0(vehicle +0xc68, fleeFrom handle +8/+0xc)` on the vehicle-AI block
(`+0xc68`, tranche 05/07/14). `0x00b464c0` (CONFIRMED — listing, follow-up dumps 1/2), in order:
1. `0x00ab63f0(AI vehicle handle +0x4e0/+0x4e4)` (resolve, kind `+0x6` bit `0x80`, alive, then `0x00ab5100` — HIGH
   CONFIDENCE the driver) has flag `0x21` → nothing: **a player-driven vehicle never flees.**
2. AI byte `+0x4` bit `0x80` set → nothing.
3. `0x006c4420()` (dword `0x012f10d8` == 2) and `0x006c48f0(AI vehicle handle)` (equal to the pair at
   `0x012f10c0/c4`) → nothing. **`0x012f10c0` has exactly one reference in the whole binary — this read** (global xref
   run 2), and is file-backed zero, so by direct references this veto can never fire (HYPOTHESIS: written through a
   structure base pointer).
4. A remote co-op player exists, `0x006c4990(remote)` is true (a game-mode test on `0x0091faf0`/`0x012f10d8`, or player
   kind with byte `+0x1a11` bit 1), and `0x00b56570(AI vehicle, remote)` (a 25 m — 625.0 squared — proximity and view test
   against a world-object list at singleton `+0x1f0/+0x1f8`) → nothing. `0x00b56570`'s return register is not cleanly
   recoverable from the decompile (OPEN).
5. Double gate `0x008ae480(AI vehicle handle, 0)` false → **opcode `0x42`** record (a sub-code through `0x00898cb0`, the
   AI vehicle handle, byte 1, the flee-from handle) sent to the owner (`0x008ae3a0` → `0x0086f110`); nothing local.
6. Locally: AI mode byte `+0` is already 2 and the stored target `+0x58/+0x5c` equals the new one (`0x00456920`) →
   nothing.
7. `0x00b45580(vehicle in ESI)` — true when (`0x00ad2030(vehicle)` and AI mode 8) or vehicle `+0x1228` == 1 — selects a
   special path `0x00b455b0(vehicle, target, &0x029cdb98, 1)`: a 1000-2000 ms timer at `+0xe9c` (`0x00d9e180`), `+0xe90`
   bits 0/1 := 0/1, `+0xcc0/+0xcc4` := target, `+0xea0..+0xea8` := the runtime vector `0x029cdb98`, `+0xee0` bit 2, then
   on the AI block `0x00b22a80(0)` (obey-traffic-lights off, tranche 14) and `0x00b22d70(1)` (a double-gate setter of AI
   byte `+4` bit `0x04`, network tag `"vehicle_ai"`/`"vai_force_flagsignore_rail_obstacles"`). Meaning of the special path
   OPEN.
8. Otherwise the normal flee: `0x00b26250(AI, 2, 5)` → the AI mode setter `0x00b24e40` (mode byte `+0` := 2, sub-byte
   `+1` := 5, previous pair kept at `+2/+3`, the old mode's exit and the new mode's enter handlers from the 5-dword-stride
   table at `0x01186da0`/`0x01186da8`); target stored at `+0x58/+0x5c`; AI byte `+0x228` bit 0 cleared and bit 1 set; and
   if the target resolves (`0x004d7f30(target, 0)`) its position `+0x40..+0x48` is copied to AI `+0x238`.

HIGH CONFIDENCE: **AI mode 2 = flee** (consistent with tranche 14's mode list). The target position is captured once
(whether the mode refreshes it is OPEN). No crash shape: every pointer is either resolved or produced by a resolve of the
same live vehicle.

### B.9 `vehicle_evacuate` → `0x00a62420`

**Arguments:** 1 string; 2 nil-gated boolean **A** (default false); 3 nil-gated boolean **B** (default false).
**Return:** 0 values. **Body:** `0x00af3f20(vehicle, A, B, 0)`. CONFIRMED.

`0x00af3f20` (CONFIRMED — listing): finds the vehicle's occupancy controller (`0x00b08400` on `0x02845cb0`: **48**
controllers of `0x1640` bytes keyed by vehicle handle — loop bound and stride CONFIRMED), none → nothing. For each of the 8
seat slots (`+0x6a0 + 0x100·i`: handle pair, state dword at `+0x6a8`), an occupied slot whose handle resolves (kind
`+0xa` bit `0x01`):
- **A true**, occupant not a player (`0x21`), not `0x0094f690` (human with `+0x1da8` non-zero), locally owned
  (`0x008addb0(occupant, 0)`) and `0x0097e280` == 0 (not in the player's crew, via `0x005088c0` — HYPOTHESIS) →
  **destroyed** with `0x00853ea0(occupant, 0, 0)`.
- Otherwise by slot state: **1** → `0x00afe570(controller, slot, 8, !B)`; **2** → `0x00af24d0(occupant, !B)` (if the
  slot's action `+0x6ac` is already `0x14` it sets `+0x770` bit 0 and starts action `0x15`, else starts action `0x19`, both
  through `0x00b058f0`); **3** → an exit record from `0x004dccd0` with `+0x30` bit 0 set and `+0x32` bit 1 := !B (bit 2 :=
  the always-zero 4th argument), then the exit request `0x00af3cd0(occupant, record)`.

HYPOTHESIS: states 1/2/3 = entering / in transition / seated; A = "despawn ambient passengers instead of ordering them
out"; B inverts a "normal (animated) exit" flag passed to every path.

**Logic note (CONFIRMED — listing `0x00af3fcf`):** the occupant's liveness test result is **discarded**, so dead
occupants are ordered out or destroyed too (consequence HYPOTHESIS: benign).

### B.10 `vehicle_detonate_random_parked_car` → `0x00a64ba0`

**Arguments (all nil-gated, default 0/none):** 1 string = reference object name; 2 number = minimum angle (degrees);
3 number = maximum angle (degrees); 4 number = minimum distance; 5 number = maximum distance. **Return:** 0 values.
CONFIRMED.

**Body (CONFIRMED — listing):** angles × 0.017453292 (`0x012a30d8`), distances squared. Reference = `0x00a29200(name, 1)`
when a name was given. Walks the vehicle list of the world singleton `0x03171a64` (count `+0xc0`, 16-bit index array
`+0xb8`, object table `+0x58`) **in list order**. A vehicle qualifies when:
- `0x00ad3c00(v)`: if `+0x16c8` bit 31 → `+0x16d0` bit 3; else `+0x16cc` bit 0; else the handle pair `+0xc40/+0xc44` is
  non-zero (HYPOTHESIS: "is parked");
- it is non-null, object byte `+0x33` bit `0x08` is set, `0x00ad3340` (`+0x16c8` bit 0 — the same bit `0x00a9f4f0` treats
  as already destroyed, HIGH CONFIDENCE) is clear, and `+0xbf8` == 0 (tranche 06 F.8's "own vehicle" field — HIGH
  CONFIDENCE: not a player-owned vehicle);
- with a reference: min² ≤ distance² ≤ max², and min ≤ angle ≤ max, where angle = arc-cosine (`0x00ea2b90`) of the dot
  product (`0x00434f10`) of the reference's forward row `+0x64..+0x6c` with the normalised offset (`0x00d9fe30`).

The **first** qualifying vehicle gets `0x00a9f4f0(vehicle, 0, 0, 0, 0, &v, 0, 100.0, 0x7fffffff, 0xc, &zero16, 0, 0, 0, 0)`
— the vehicle damage entry with the maximum integer amount and damage type `0xc` (HIGH CONFIDENCE: lethal explosion
damage); `v` is built from vehicle `+0x1ddc/+0x1de0/+0x1de4` with the fourth lane repeating the third. Then it returns:
at most one vehicle per call.

**Findings:**
- **Not random (CONFIRMED):** no generator call; it is a deterministic first match in list order.
- **Misspelt reference = "anywhere" (CONFIRMED):** a name that does not resolve leaves the reference null, which takes
  exactly the no-reference branch — the first parked car in the whole world is blown up instead of one near the intended
  object.
- **Reference with default bounds (CONFIRMED arithmetic):** all-zero angle and distance bounds accept only a vehicle at
  distance 0 and angle 0, so a one-argument call with a valid reference effectively never detonates anything.
- **Pointer used before its null test (CONFIRMED order, `0x00a64d79` then `0x00a64d89`):** `0x00ad3c00` reads
  `v +0x16c8` before the loop tests `v` for null. Whether the object table can hold a null for a listed index is OPEN
  (HYPOTHESIS: no).
- NaN angle (a dot product a hair above 1): the x87 "above" test and the SSE "below or equal" test both treat unordered as
  in range, so a NaN angle passes both bounds (CONFIRMED flag semantics; rare edge).

---

## C. Characters, triggers, tutorials — 4 functions (gameplay)

### C.1 `turn_to_check_done` → `0x00a61dd0`

**Arguments:** 1 string (character, `0x00a281a0(name, 0)`). **Return:** exactly 1 boolean = `0x006f6380(character, 0)`
≠ 0. CONFIRMED.

`0x006f6380(character, kind)` (CONFIRMED — follow-up dump 2): `0x006f6260` (character in ECX, kind in EDI) walks the
circular action-record list headed at `0x014f4be0` for a record whose `+0x18/+0x1c` equals the character's handle and
whose `+0xc` equals the kind; a null character gives no record. **No record → 2** (true). A record with `+8` bit 0 set is
**moved to the free list** `0x014f4be4` (`0x006f6040`, record in EAX) **→ 1** (true). Otherwise → 0 (false).

So the binding answers **true** for an unresolved character, for a character that never started a turn, and for a
finished turn — and the finished record is consumed by the query itself (the next call answers true through "no
record"). It answers false only while the turn is in progress. HIGH CONFIDENCE: kind 0 = "turn to" (the only kind this
binding passes; `0x006f6380` has many other callers).

### C.2 `trigger_allow_dead_guys` → `0x00a61a10`

**Arguments:** 1 string (trigger name); 2 nil-gated boolean, **default true**. **Return:** 0 values. CONFIRMED.

**Body:** null name → nothing. `0x005e4e30(name)` on the registry `0x02442750` — the generic named-lookup shape of tranche
16's front matter with kind `+0x7` bit `0x02` (trigger) — alive → **bit `0x08` of trigger byte `+0x7b`** := arg 2. No
session gate, no network record. Readers OPEN (HYPOTHESIS: the trigger's "character inside" test then also counts dead
characters).

### C.3 `tutorial_lock` → `0x00a60150`

**Arguments:** 1 string (tutorial name). **Return:** 0 values. CONFIRMED.

**Body:** index = `0x00717780(name)` (210-name table); −1 → nothing. Else `0x00716140(index)`: **only if index ≤ 188
(`0xbc`, unsigned)** and the entry's state (`0x0151d60c + 36·index`) is **1** (armed), the state becomes **0** — the
"disarm" writer already listed in `interp_dksj.md` Q2.

**Findings (CONFIRMED):** the bound is 188, not 209: the 21 entries 189-209 (the second group that the fill routine
starts in state 1, `interp_jfue.md` Q2.1) can never be locked through this name — a silent no-op. And "lock" touches
only an *armed* tutorial: one already queued, shown or issued (states 2/3/4) is not affected.

### C.4 `tutorial_stop_current` → `0x00a60230`

**Arguments:** none read. **Return:** 0 values. **Body:** `0x00716d90()`. CONFIRMED.

`0x00716d90` (CONFIRMED — listing, follow-up dumps 1/2): the current entry is the head of the queued-prompt list
`0x0151d588`; none → nothing. Otherwise:
1. Entry state := **4**.
2. `0x00715a30` (entry in ESI): unlinks the entry from the prompt list (head, single-node and middle cases all handled
   correctly — CONFIRMED by tracing), sends the named UI message **`"tutorial_close"`** to the tutorial document
   `0x0151d5a8` (`0x00e1a1b0`, `0x00e0ca80`, `0x00e0cd00`), and — only if the message was created — sets entry `+0x18` to 2
   when it was 0, and sets the state back to **1** when the descriptor's `+0x24` has **bit `0x02`** (a descriptor bit not
   in `interp_dksj.md`'s list; HYPOTHESIS: "repeatable", the tutorial re-arms after being stopped).
3. A discarded call to the return-1 stub `0x00d1de80`.
4. **On a session host only**, unless `0x00bc55a0()` (dword `0x029443bc` == 1), and only when the descriptor is host-only
   (`+0x24` bit `0x10`) or entry `+0x1c` is 1 (the literal every script-started entry carries): broadcast **opcode
   `0x54`** with a byte 0 and the entry index (computed as (entry − `0x0151d600`) / 36 by reciprocal multiplication) through
   `0x00711560` and `0x0086f1b0`.

**Network note (CONFIRMED structure; consequence HYPOTHESIS):** a co-op client stops its own prompt but sends nothing,
so the host's copy keeps running.

---

## D. Vehicle customisation (`vcust_*`) — 9 functions (UI registrar `0x00820aa0`)

**Shared state (CONFIRMED across the dumps):** the customisation vehicle is the handle pair **`0x022cf948/0x022cf94c`**,
re-resolved by every function (handle resolution, kind `+0x6` bit `0x80`, alive). Its customisation record is
`[vehicle +0x1e00] + 0x10`, valid only when the record's byte `+0x60` is 1; the record's slot list is at `+0x5c` (count
dword `+0`, 16-byte slots from `+4`; slot `+0` = a definition pointer whose `+4` is a name hash, slot `+4` = a preset hash,
slot `+8` = a variant/colour set). The vehicle's component set is `+0x688` (valid only when `+0xbd0` is 1) with an
installed-item list at set `+0x400`. Menu globals: current node `0x022cf8e0`, current top-level node `0x022cf8e4`, list
head `0x022cf8e8` (written only by `0x00818210`, global xref run 2). Both validity tests are usually applied with the
**select-or-null** idiom (G.2), which yields a null base instead of branching.

### D.1 `vcust_change_top_level_menu` → `0x00818df0`

**Arguments:** 1 number **d** (truncated, unconditional); 2 nil-gated number **n** (truncated, default 0).
**Return:** 1 number when a node is selected, else **0 values**. CONFIRMED.

**Body (CONFIRMED — listing):** d = −1 → the previous sibling (`+0x8c` of `0x022cf8e4`); d = 1 → the next sibling
(`+0x88`); any other d → start at the head `0x022cf8e8` and step n times along `+0x88`, where a step that comes back to
the head yields null. The result is stored in `0x022cf8e4` **unconditionally, even when null**; a non-null result is also
stored in `0x022cf8e0` and the function pushes **1** when node byte `+0xa0` bit 0 is set, **2.0** (`0x012a2e28`) when bit 1
is set, else 0.

**Crash — CONFIRMED (listing):** d = ±1 dereferences `0x022cf8e4` without a test (`0x00818e48` / `0x00818e5b`). That
pointer is zero until the customisation init `0x008205e0` sets it, **and this function itself writes null** after an
absolute selection past the end (n ≥ node count, or a negative n, which counts down through one full lap and ends on
null — no hang). So `vcust_change_top_level_menu(0, 99)` followed by `vcust_change_top_level_menu(1)` reads address
`0x88`. Also, after a null result `0x022cf8e0` still names the old node, so the two globals disagree.

### D.2 `vcust_select_paint_slot` → `0x0081f2b0`

**Arguments:** 1 number (slot index, truncated). **Return:** 0 values. **Body:** `0x00819d90` (index in EAX). CONFIRMED.

`0x00819d90` (CONFIRMED — listing): resolves the customisation vehicle; record = select-or-null; slot = slot list + 16 ·
index (**no bounds check**), stored in `0x022cf914`; `0x022cf90a` := the slot's definition hash is `"Rim Color"` or
`"Trim Color"` (`0x00d9e8b0`); `0x022cf904` := 0, `0x022cf908` := 0; then every colour entry of the slot's set (count byte
`+0x10`, entries of `0x28` from `+0x14`, capped at `0x500` bytes = 32 entries) that passes `0x008181b0` is appended to the
array `0x022cf850[]` (count `0x022cf904`; the array spans 36 dwords up to `0x022cf8e0`, so the 32 cap fits), and any
appended entry with byte `+0x27` bit 0 sets `0x022cf908` := 1.

**Crash shapes — CONFIRMED:** unresolved vehicle → `null + 0x1e00` at `0x00819df8`; record byte `+0x60` ≠ 1 → read at
address `0x5c` (`0x00819e24`); an out-of-range or negative index → a slot record outside the list whose `+8` and
`[+0]+4` are dereferenced (`0x00819e2e`, `0x00819e42`).

### D.3 `vcust_adjust_camera_angle` → `0x0081fb30`

**Arguments:** 1 number **i** (truncated). **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing):** reads byte `+0xa0` of the current node `0x022cf8e0` **without a null check**
(`0x0081fb64`), then:
- node bit 0 set: i below the slot count (**signed** compare, so a negative i passes) → slot = list + 16·i;
- node bit 0 clear and i = −1000 (`0xfffffc18`): the slot whose definition hash equals hash(`"Chassis"`);
- otherwise: i below node `+0x98` (unsigned) → item = `[node +0x9c][i]`, and the slot whose definition pointer equals it;

and calls `0x00819810(slot in EDI)`: resolves the vehicle (null tolerated) and calls `0x00a9b080(vehicle, slot +4, 0, 0)`
— the **customisation camera-preset placer** (follow-up dump 1: looks the hash up in the class's preset table from
`0x00a9afd0(class +0xbf4)`, 60-byte entries with the hash at `+0x38`, computes a camera position around the vehicle and
blends to it through `0x00568de0`, blend time `0x1e` or the global `0x01300544`; the preset `"Standard"` is special-cased;
null vehicle → nothing) — then clears byte `0x01300b88` (file-backed 1; set to 1 by the init `0x008205e0`, written by
`vcust_set_camera_pos` `0x0081fa80`, read by the store-vehicle lock-controls helper `0x008131f0` of tranche 07).

**Crash shapes — CONFIRMED:** null current node (any call before a menu is set up) at `0x0081fb64`; unresolved vehicle →
`null + 0x1e00` at `0x0081fbc1`, `0x0081fc45` or `0x0081fcc4` (one per branch); record byte `+0x60` ≠ 1 → read at
address `0x5c`; a negative i in the bit-0 branch builds a slot pointer before the list (its `+4` is then only used as a
lookup key — harmless unless the address is unmapped).

### D.4 `vcust_preview_rim_sizing` → `0x0081f750`

**Arguments:** 1 number **i** (size index, truncated). **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing):** vehicle resolved (null tolerated here); `0x022cf7c0` := i; idx = **i + 5·k** with
k = `0x022cf7b4` (the rim style, written by `0x0081bf90` and the vcust code at `0x0081f470`/`0x0081f9af`) — **no bounds
check**. Per the wheel selector `0x022cf248` (−1 = both, 0, 1): for −1/0 the record `0x022cf350[idx]` goes to `0x008194f0`
(record in EDI: resolves the vehicle, select-or-null component set, `0x00a999f0([set +0x400], record, vehicle, 0)`, then
`0x00819480`) and into `0x022cf798`; for −1/1 the record `0x022cf450[idx]` likewise into `0x022cf79c`; each sets the dirty
byte `0x022cf24c` := 1. Then `0x00ad8090(vehicle)` (rebuild the wheel set) and `0x00abf070(vehicle)` (suspension refresh) —
the same pair tranche 06 E.3 found in `vcust_preview_wheel_sizing`.

**Crash shapes — CONFIRMED:**
- **Unresolved vehicle:** `0x00abf070` tests for null but `0x00ad8090` does not — it reads `null + 0xbd0` at
  `0x00ad80a9`.
- **Unchecked index:** the two tables are filled by `0x0081bf90` (the first is 64 dwords wide by address spacing); an
  out-of-range size or style reads unrelated `.data` and hands it to `0x008194f0`/`0x00a999f0` as a component record.
- Select-or-null set when `+0xbd0` ≠ 1 → read at address `0x400` (`0x00819569`), data-dependent.

### D.5 `vcust_set_underglow` → `0x0081f110`

**Arguments:** 1 number **c** (colour index, truncated); 2 number **p** (price in dollars, truncated). **Return:** 0
values. CONFIRMED.

**Body (CONFIRMED — listing):** unresolved customisation vehicle → nothing (this one checks). Palette =
`0x00746750(&count, 2)` (table `0x015535b8[2]`, count `0x015535a8[2]`); a zero count or null table → nothing. **c is not
checked against the count**: entry = table + 48·c, its floats `+0xc/+0x10/+0x14` × 255.0 (`0x012a2d88`) truncated to
bytes, alpha `0xff`, applied by `0x00aac990(vehicle, colour)` (only when `+0xbd0` is 1: virtual `+0x60` of the
sub-object at vehicle `+0xa24` — HYPOTHESIS: the vehicle's light/underglow controller). Then, **only if p > 0**: cents = `0x009601d0(p)`, charged as `0x0094d920(local player,
−cents, reason 11)`, stat `0x47` credited with cents/100 (`0x00710cb0`), `0x006b2050(0x13)`. Finally, always, `0x0081f070()`.

`0x0081f070()` (CONFIRMED — listing): resolves the customisation vehicle; on the first customisation of that vehicle
(`+0x16c4` bit `0x40` clear) posts stat `0x4e` (`0x00710e40`) and sets the bit; then `0x005f77b0(0, vehicle, garage block
0x014a1d00, 0.0)` and `0x00812ff0(this = store object 0x022cde10, vehicle)` (sets store byte `+0x14c` when the vehicle's
`+0x1ac8` record matches store `+0x148`). **It dereferences the vehicle without a null test** (`0x0081f0ca`).

**Findings:** unchecked palette index (an out-of-table colour or a read fault past the table); the colour is applied
before payment; the price comes from the script; no affordability check; unlike D.6-D.8 there is **no purchase
callback and no telemetry** call. Local player unchecked → tranche 16 B.3's shape (`0x0094d920(null)` reads
`null + 0x1ca0` at `0x0094da81`).

### D.6 `vcust_purchase_component` → `0x0081fd20`

**Arguments:** none read. **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing):** item = `[0x022cf924 +8] + 60 · [0x01300b80]` (the selected item of the current item list).
1. `0x0081ef00()` — **nitrous / hydraulics exclusivity**: only for an item of type `+0xc` == 2 with byte `+0x11` or
   `+0x10` set; looks up the *other* kit's definition (`"Nitrous"` when `+0x11` is 1, else `"Hydraulics"`, via
   `0x00a96130`: hash, then a 16-byte-stride table at `0x027b15e4`, count `0x027b15dc`), finds that slot on the vehicle and
   its installed record (`0x00a94450` on the installed list); if that record's `+0x11` or `+0x10` is 1, applies
   (`0x008194f0`) the first variant of that slot (entries of `0x3c` from slot `+8`, count byte `+0xc`) that is not the
   installed one. HIGH CONFIDENCE: buying one kit swaps the other out.
2. If item byte `+0x38` bit 0 is clear and the price dword `+0x8` is > 0: amount = round(`0x0080ede0(−price)` × 100) —
   `0x0080ede0` scales by the store price modifier (globals `0x022cd0ec`/`0x022cd0f0`, constant 1.0) and floors
   (`0x00ea4e80`); formula details OPEN — saturated (`0x00960160`), charged reason 11 to the local player (unchecked); stat
   `0x47` credited with the dollar price; `0x006b2050(0x13)`; the **purchase callback** with `"vehicle-cust"` and the
   player's script name (`0x00a353d0`) — tranche 06 F.7 predicted this function shares the mechanism; now CONFIRMED;
   item `+0x38` bit 0 := 1 (owned); `0x00bdb790(vehicle handle, definition hash, item +0)` (only when bytes `0x013123d1`,
   `0x029897a5`, `0x029897a6` are all set: posts a record carrying the vehicle class hash `[+0xbf4]+0x18` through
   `0x004234b0` — HYPOTHESIS: an online/telemetry purchase event).
3. If item ≠ `0x022cf928` → `0x0081f070()`. Then `0x022cf928` := 0 and **`0x01300b80` := −1**.

**Crash / corruption shapes — CONFIRMED (listing):**
- **Index −1 → out-of-bounds item, then a 1-bit write before the array.** `0x01300b80` is file-backed **−1** and this
  function resets it to −1 on every exit, so any call without a fresh selection — including the second of two
  back-to-back calls — computes item = array − 60 (`0x0081fd3f`-`0x0081fd4e`). It then reads the "owned" bit and "price"
  from the 60 bytes before the array, and if that garbage price is positive and the bit clear it **charges the garbage
  price** and executes `OR byte [item +0x38], 1` at `0x0081fe0c` — a write into the dword just before the array (heap
  header or a neighbouring allocation). Actual memory contents OPEN.
- `0x022cf924` null (zero-fill; written only by `0x0081d500`, cleared by the init `0x008205e0` — global xref run 1) →
  read at address 8 (`0x0081fd4b`).
- Unresolved customisation vehicle → `null + 0x1e00` in `0x0081ef00` (`0x0081ef8f`, type-2 kit items) and `null + 0x16c4`
  in `0x0081f070` (`0x0081f0ca`) whenever item ≠ `0x022cf928`.
- Select-or-null component set when `+0xbd0` ≠ 1 → read at address `0x400` (`0x0081f00b`), data-dependent.

### D.7 `vcust_purchase_paint` → `0x0081fff0`

**Arguments:** 1 number **p** (price, truncated, unconditional). **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing):** with **no test on p**: amount = round(`0x0080ede0(−p)` × 100), saturated, charged reason 11
to the local player (unchecked); stat `0x47` credited with p; `0x006b2050(0x13)`; the purchase callback (`"vehicle-cust"`,
player name); `0x00bdb8f0(vehicle handle)` (telemetry sibling of D.6's, same three-byte gate); `0x0081f070()`; byte
**`0x022cf90b` := 1** — read only by `vcust_revert_color` (`0x0081f89d`) and cleared by `0x00819ed0`/`0x0081acc0` (global xref
run 2): HIGH CONFIDENCE "the previewed paint was bought, do not revert it".

**Findings — CONFIRMED:**
- **Negative price credits cash; zero price still fires the stat, callback and telemetry** — unlike
  `vcust_purchase_wheels` (tranche 06 E.4) and D.5, which skip all of it for p ≤ 0.
- **Crash:** this function never resolves the vehicle itself, so a call while the customisation vehicle is not live
  reaches `0x0081f070`'s unchecked `null + 0x16c4` at `0x0081f0ca`.
- No affordability check.

### D.8 `vcust_purchase_performance_upgrade` → `0x008200f0`

**Arguments:** 1 number **c** (upgrade category, truncated). **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing):** unresolved vehicle → nothing. Level array = `[select-or-null(+0x688) +0x400]`, read before
any test (when `+0xbd0` ≠ 1 this reads address `0x400` at `0x00820193`, data-dependent). Current level L = levels[c]
when c < 4 (**signed** compare), else 0. Price = the class record's (`+0xbf4`) dword at `+0xb38 + 4·(L + 4·c)` — i.e. a
4 × 4 table (category × current level, five levels 0-4). `0x00ab7800(this = levels, c, +1, vehicle)`: refuses c ≥ 4 *or
negative* (**unsigned** compare) and a result above 4; otherwise stores L + 1 and applies it (`0x00ab7070(vehicle)`).
**Only on success:** the price, read as an unsigned 32-bit value (2³² added when negative, `0x012a3078`), goes through
`0x0080e740` (the single-precision twin of the price modifier), is negated, × 100, rounded, saturated and charged reason 11
to the local player (unchecked); stat `0x47` credited with the modified dollar price; `0x006b2050(0x13)`; telemetry
`0x00bdb990`; the purchase callback; `0x0081f070()`.

**Findings:** the only purchase in this family that validates before charging (CONFIRMED); still no affordability check.
A negative c reads the byte before the level array and an out-of-range class dword as the price, but `0x00ab7800` then
refuses, so nothing is charged (CONFIRMED — harmless out-of-bounds read).

### D.9 `vcust_show_nos_hydraulic_warning` → `0x008202b0`

**Arguments:** none read. **Return:** exactly 1 boolean. CONFIRMED.

**Body (CONFIRMED — listing):** `0x022cf928` == 0 → false (written by `0x00819590`, `0x0081d500`, cleared by D.6 and the
init — global xref run 1). Otherwise resolve the vehicle — **unresolved → `null + 0x1e00` at `0x00820338`**; record byte
`+0x60` ≠ 1 → false. Looks up `"Nitrous"` and `"Hydraulics"` (`0x00a96130`), finds each among the vehicle's slots and its
installed record (`0x00a94450` on the select-or-null component set's `+0x400`; a null set reads address `0x400` at
`0x008203f7`/`0x0082040a`). **True iff both records exist, the nitrous record's byte `+0x10` is 1 and the hydraulics
record's byte `+0x11` is 1.** HYPOTHESIS: the warning that both kits are fitted and one will replace the other (D.6
step 1 performs the swap).

---

## E. Vehicle store — 2 functions (UI registrar `0x008152a0`)

### E.1 `store_vehicle_notify_screen_covered` → `0x00814060`

**Arguments:** none read. **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing):** only when the store sub-state `0x022cdf0c` is 0: `0x00813d90(this = store object
0x022cde10)` places characters, then sub-state := **1** and byte **`0x022ccee8`** := 1 (also written by `0x0080dbfd` and
cleared in five store functions; read by `0x00584b70`, `0x00586fb0`, `0x00587230` — HYPOTHESIS: "the world is hidden
behind a store screen").

`0x00813d90`: for each of `[this +0x138]` character handles at `this +0x118` (stride 8) that resolves (kind `+0xa` bit
`0x01`, alive): position = the k-th 16-byte entry of the table at `this +0x150` (`0x022cdf60`) while k < `[this +0x1c0]`
(`0x022cdfd0`), else the local player's position (`+0x40`, local player **not null-checked** at `0x00813e6f`); orientation
built (`0x00da4170`) from the three rows at `this +0x190..+0x1bc` (`0x022cdfa0`-`0x022cdfcc`); then the teleport routine
`0x00996b50(character, &position, &orientation, 1, 1, 1, 0, 0, 0)`. HYPOTHESIS: the player's companions are placed in the
garage while a fade covers the screen.

**Findings:**
- **Four-slot array directly before its count (CONFIRMED layout arithmetic):** the handles occupy `+0x118..+0x137`, and
  the count lives at `+0x138`; a count above 4 would read the count itself and the following members as handles. The
  writer of `+0x138` was not read (OPEN).
- The absolute addresses `0x022cdf60`, `0x022cdfa0`-`0x022cdfcc` and `0x022cdfd0` have no other direct references because
  they are members of the global store object, written through `this` elsewhere (HIGH CONFIDENCE) — not dead data.
- The sub-state doubles as a once-only guard: a second notification does nothing until something resets the sub-state
  to 0 (CONFIRMED structure).

### E.2 `store_vehicle_no_vehicle_exit` → `0x00813080`

**Arguments:** none read. **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — listing):** if the store flag `0x022cdf08` is 1, the pending vehicle is taken over into the selection
`0x022cde00/04` (`0x005f8a00`, exactly as `store_vehicle_retrieve_car_and_exit`, tranche 06 D.2). Then the selection is
**destroyed** (`0x00854410(selection, 0, 0)`, the guarded destroy-by-handle: protected kinds, remote objects and objects
without `+0x3c` are skipped), byte `0x022cdf4c` := 0, the top screen is popped (`0x007b4790(0)` on `0x012fced8`) and, if
screen `0x2e` is on the stack (`0x007b3ba0(0x2e)`), popped once more.

**Findings:**
- **Dead byte (CONFIRMED by direct references, global xref run 2):** `0x022cdf4c` has three references in the whole
  binary — this write of 0 and two reads in the garage teardown `0x005fa950`. Nothing writes it non-zero, so this write
  is a no-op and the teardown's two branches on it are constant (same pattern as tranche 16 B.1's `0x022cdf14`).
- In mode 1 the just-retrieved pending vehicle is moved into the selection and destroyed in the same call (CONFIRMED
  order): "exit without a vehicle" despawns the car being retrieved.
- The selection pair is not cleared after the destroy; later users re-resolve it and fail safely (HIGH CONFIDENCE).

---

## F. Method notes for the follow-up runs

- Range run: `0x00820aa0-0x00820cd0` (the vcust table and loop; the tail is the next function `0x00820c60`).
- Registrar xref (run as the first item of global xref run 2): `0x00820aa0` → one caller, `0x008431fe` in
  `0x008430f0`.
- Follow-up dump 1 (depth 0, `maxinsn:800`, earlier attempt, re-read): `0x00ab63f0 0x00b45580 0x00b26250 0x006c4420
  0x006c48f0 0x00b56570 0x00b455b0 0x00b08400 0x00853ea0 0x0094f690 0x0097e280 0x00afe570 0x00af24d0 0x00af3cd0 0x00a7ded0
  0x00a9b080 0x00457820 0x00715a30 0x00abf810 0x00a896a0`.
- Follow-up dump 2 (depth 0, `maxinsn:900`): `0x006f6260 0x006f6040 0x00d1de80 0x00bc55a0 0x00ad2030 0x00b24e40 0x00ea4e80
  0x00812ff0 0x005f77b0 0x00b22d70 0x006c4990 0x00a9afd0 0x00a88b70`.
- Global xref run 2 (`xrefs:40`): `0x00820aa0 0x012f10c0 0x022cf8e8 0x022cf7b4 0x022cf248 0x022cf350 0x022cf450 0x01300b88
  0x022cf90b 0x022cdf4c 0x022ccee8`. Displacement scan (`xrefs:80`): `0x15a0`, `0x16cc` (48 sites in 30 functions — read
  only for candidates, per WALLS.md).
- Constant read (`ptrs`, `count:2`): `0x01171c60 0x012a30d8 0x012a2d70 0x012a2dd8 0x012a2d88 0x012a2e28 0x012a3078
  0x012a2dc0 0x01243130 0x012a3038`; the single-precision statics `0x0125e270` = 0.0 and `0x0126d2cc` = 0.5 come from the
  main dump's global section.
- Return-1 stub met in this tranche: `0x00d1de80` (C.4) — CONFIRMED.

---

## G. Cross-function observations

1. **No pointer-before-name registrars this time.** All 25 names sit in table-driven registrars (gameplay, the 20-name
   vcust table, the 8-name store-vehicle table). The vcust table adds 11 sibling names with handler addresses (section A)
   for a binding layer.
2. **The select-or-null idiom is a crash factory.** `CMP byte [x],1 / SETNZ / DEC / AND` turns "flag is not 1" into a
   **null base pointer** instead of a branch, and the callers then read fixed offsets from it (`+0x400`, `+0x5c`,
   `4·i + 4`). Seen on vehicle `+0xbd0` and customisation record `+0x60` in `0x00a8eeb0`, `0x00abf810`, `0x00a896a0`,
   `0x008194f0`, `0x00819d90`, `0x0081fb30`, `0x0081ef00`, `0x008202b0`, `0x008200f0`. Fix template: test the flag and
   return.
3. **The customisation vehicle is checked by only 2 of the 9 vcust names** (D.5, D.8); D.2, D.3, D.4, D.6, D.7 and D.9
   all dereference the null result of a failed resolve (D.9 only after its `0x022cf928` gate). A binding layer should
   treat every vcust name as "requires a live customisation vehicle" and refuse otherwise.
4. **The purchase family is inconsistent (CONFIRMED):**

   | name | price source | price ≤ 0 | charge vs. validation | callback / telemetry | affordability |
   |---|---|---|---|---|---|
   | `vcust_set_underglow` (D.5) | script | skipped | colour applied first | neither | none |
   | `vcust_purchase_paint` (D.7) | script | **charged (negative credits)** | no validation | both | none |
   | `vcust_purchase_component` (D.6) | item data | skipped | owned bit, but index can be −1 | both | none |
   | `vcust_purchase_performance_upgrade` (D.8) | class table | n/a (unsigned) | **validated first** | both | none |
   | `vcust_purchase_wheels` (tranche 06 E.4) | script | skipped | — | both | none |

   All charge through `0x0094d920` with reason 11, credit stat `0x47` and run `0x006b2050(0x13)`; none null-checks the
   local player.
5. **Names that do not mean what they say:** `vehicle_detonate_random_parked_car` is a deterministic first match and a
   misspelt reference widens it to the whole world (B.10); `vehicle_component_shatter_glass` shatters any named component
   (B.6); `tutorial_lock` only disarms an armed tutorial and cannot reach entries 189-209 (C.3); `turn_to_check_done`
   consumes the finished record (C.1); `vehicle_get_max_speed` returns mph and −1 for "no vehicle" (B.3).
6. **Never-written state (by direct references):** `0x022cdf4c` (E.2) and `0x012f10c0` (B.8 veto). Together with tranche
   16's `0x022cdf14`, the store code has several flags whose only writers are clears.
7. **Null into `_stricmp`:** B.5/B.6 pass a nil component name straight into the runtime's validated `_stricmp`; B.7's
   helper does not skip a null component name. A binding layer can simply refuse a nil name.
8. **Index spaces for a binding layer:** B.5/B.6 component index < 92 (internal); D.1 n = 0-based top-level menu position
   (out of range → null, poisons the next ±1 call); D.2 slot index (unchecked); D.3 index per node kind, −1000 = chassis;
   D.4 size index + 5 × style (unchecked); D.5 palette index (unchecked); D.8 category 0-3, levels 0-4.

## H. OPEN

- B.3: which writer sets vehicle `+0x1d84`.
- B.4: whether `0x008ada30` is the ownership-transfer reader of `+0x16cc` bit `0x20`.
- B.5/B.6: whether the game installs its own invalid-parameter handler (decides crash vs. silent return for a nil
  name); `0x00abf810`'s other four callers.
- B.7: the reader(s) of `+0x15a0` (`0x00557e10` is the first candidate).
- B.8: the meaning of `0x00b45580`'s special flee path and of AI byte `+0x4` bit `0x80`; `0x00b56570`'s return value;
  how `0x012f10c0` is written, if at all.
- B.9: slot-state names 1/2/3 and the exact meaning of A and B; `0x005088c0`.
- B.10: whether the object table can hold a null entry for a listed vehicle index.
- C.1: the other kinds passed to `0x006f6380`.
- C.2: readers of trigger byte `+0x7b` bit `0x08`.
- C.4: descriptor bit `0x02`'s exact role; what `0x029443bc` == 1 means.
- D.1: what the 1 / 2 / 0 return values mean to the UI script.
- D.6: `0x0080ede0`'s exact price formula; the contents of the 60 bytes before the item array (decides whether index −1
  charges and corrupts in practice); `0x00bdb790`'s destination.
- D.9: the meaning of record bytes `+0x10`/`+0x11`.
- E.1: who writes store object `+0x138`, and whether it can exceed 4.

## I. Direct-answer table

| # | name | registrar | handler | status |
|---|---|---|---|---|
| 1 | `vehicle_is_flipped` | gameplay `0x00a20840` | `0x00a642e0` | resolved — up-row vertical ≤ 0.5 (≥ 60° tilt); false when unresolved |
| 2 | `vehicle_in_air` | gameplay | `0x00a626c0` | resolved — `0x00ad4040` (no body/wheel contact, not in water) |
| 3 | `vehicle_get_max_speed` | gameplay | `0x00a63fa0` | resolved — `+0x1d84` × 2.2369363 (mph); −1 when unresolved |
| 4 | `vehicle_flee` | gameplay | `0x00a63ea0` | resolved — AI mode 2 from a target (default `#PLAYER1#`); vetoes; opcode `0x42` to owner |
| 5 | `vehicle_evacuate` | gameplay | `0x00a62420` | resolved — per-seat exit orders, optional despawn of ambient occupants; dead occupants included |
| 6 | `vehicle_detonate_random_parked_car` | gameplay | `0x00a64ba0` | resolved — **first** qualifying parked car, optional distance/angle window; misspelt reference = anywhere |
| 7 | `vehicle_component_shatter_glass` | gameplay | `0x00a63cb0` | resolved — sets component `+0xf0` bit 23 + effect; any component; nil name → `_stricmp(null)` |
| 8 | `vehicle_component_is_shattered` | gameplay | `0x00a63a60` | resolved — component `+0xf0` bit 23; nil name → `_stricmp(null)` |
| 9 | `vehicle_allow_ownership_transfer` | gameplay | `0x00a63050` | resolved — `+0x16cc` bit `0x20` := not arg 2 |
| 10 | `vcust_show_nos_hydraulic_warning` | vcust `0x00820aa0` (under UI) | `0x008202b0` | resolved — both kits fitted; unresolved vehicle null read |
| 11 | `vcust_set_underglow` | vcust | `0x0081f110` | resolved — palette colour (unchecked index), optional script price |
| 12 | `vcust_select_paint_slot` | vcust | `0x0081f2b0` | resolved — builds colour list for slot; unchecked index; null vehicle read |
| 13 | `vcust_purchase_performance_upgrade` | vcust | `0x008200f0` | resolved — validated level +1 (max 4), then charge |
| 14 | `vcust_purchase_paint` | vcust | `0x0081fff0` | resolved — charges script price unguarded (negative credits); null vehicle read in `0x0081f070` |
| 15 | `vcust_purchase_component` | vcust | `0x0081fd20` | resolved — kit swap, charge, owned bit; **index −1 OOB read and write** |
| 16 | `vcust_preview_rim_sizing` | vcust | `0x0081f750` | resolved — unchecked size/style index; null vehicle read in `0x00ad8090` |
| 17 | `vcust_change_top_level_menu` | vcust | `0x00818df0` | resolved — ±1 / absolute; **stores null, next ±1 call reads null** |
| 18 | `vcust_adjust_camera_angle` | vcust | `0x0081fb30` | resolved — camera preset per slot; null node / null vehicle reads |
| 19 | `vault_heli_hack_for_m01` | gameplay | `0x00a62020` | resolved — index of component `"body"` into `+0x15a0` |
| 20 | `tutorial_stop_current` | gameplay | `0x00a60230` | resolved — closes head prompt (state 4, maybe re-armed), host broadcast `0x54` |
| 21 | `tutorial_lock` | gameplay | `0x00a60150` | resolved — armed → 0, only indices ≤ 188 |
| 22 | `turn_to_check_done` | gameplay | `0x00a61dd0` | resolved — true unless a turn is in progress; consumes finished record |
| 23 | `trigger_allow_dead_guys` | gameplay | `0x00a61a10` | resolved — trigger `+0x7b` bit `0x08`, default true |
| 24 | `store_vehicle_notify_screen_covered` | store vehicle `0x008152a0` (under UI) | `0x00814060` | resolved — teleports up to 4 store characters, sub-state 1 |
| 25 | `store_vehicle_no_vehicle_exit` | store vehicle | `0x00813080` | resolved — retrieve-and-exit plus destroy of the selection; dead byte `0x022cdf4c` |

## J. Crash-shaped and logic defects (summary)

| # | where | defect | status |
|---|---|---|---|
| 1 | D.6 `0x0081fd3f`-`0x0081fd4e`, write at `0x0081fe0c` | selected index `0x01300b80` is −1 initially and after every call → item read from 60 bytes before the array; garbage price charged and a bit written just before the array | CONFIRMED arithmetic; contents OPEN |
| 2 | D.1 `0x00818e48` / `0x00818e5b` | out-of-range absolute selection stores null into `0x022cf8e4`; the next ±1 call reads null + `0x8c`/`0x88` (also null before init) | CONFIRMED |
| 3 | D.2 `0x00819df8`; D.3 `0x0081fbc1`/`0x0081fc45`/`0x0081fcc4`; D.4 → `0x00ad80a9`; D.6 → `0x0081ef8f`; D.6/D.7 → `0x0081f0ca`; D.9 `0x00820338` | customisation vehicle not live → null + `0x1e00` / `0xbd0` / `0x16c4` | CONFIRMED |
| 4 | D.3 `0x0081fb64` | current menu node `0x022cf8e0` read without a null check | CONFIRMED shape |
| 5 | D.6 `0x0081fd4b` | item list `0x022cf924` null → read at address 8 | CONFIRMED |
| 6 | D.2 `0x00819e2e` / `0x00819e42` | unchecked slot index → slot record outside the list dereferenced | CONFIRMED unchecked |
| 7 | D.4 `0x0081f7ec` / `0x0081f81a` → `0x008194f0` | unchecked size + 5 × style index into `0x022cf350`/`0x022cf450` → wild component record | CONFIRMED unchecked |
| 8 | D.5 `0x0081f1d1` | unchecked palette index → out-of-table colour reads | CONFIRMED unchecked |
| 9 | select-or-null bases: `0x00819e24`, `0x0081fbd5`, `0x00819569`, `0x00820193`, `0x0081f00b`, `0x008203f7`/`0x0082040a` | flag ≠ 1 → read at `0x5c` / `0x400` | CONFIRMED shape, data-dependent |
| 10 | B.5/B.6 `0x00a89161` | nil component name → `_stricmp(name, null)` (runtime parameter check, fatal by default) | CONFIRMED null reaches it; outcome HIGH CONFIDENCE |
| 11 | D.5/D.6/D.7/D.8 → `0x0094da81`; E.1 `0x00813e6f` | local player used without a null check | CONFIRMED shape |
| 12 | B.10 `0x00a64d79` / `0x00a64d89` | `0x00ad3c00` reads the vehicle before the null test | CONFIRMED order; reachability OPEN |
| 13 | B.10 | misspelt reference → detonates the first parked car anywhere; "random" is a deterministic first match | CONFIRMED |
| 14 | D.7 | no price test: negative price credits cash, zero price still fires stat/callback/telemetry | CONFIRMED |
| 15 | D.5-D.8 | no affordability check; D.5/D.7 trust a script price; D.5 applies before charging | CONFIRMED structure |
| 16 | E.1 | four-slot handle array directly before its count | CONFIRMED layout; writer OPEN |
| 17 | E.2 | `0x022cdf4c` never written non-zero → dead write and dead teardown branches | CONFIRMED (direct references) |
| 18 | B.8 | `0x012f10c0` never written by direct reference → flee veto 3 can never fire | CONFIRMED (direct references) |
| 19 | C.3 | `tutorial_lock` bound 188 vs. 210 names → entries 189-209 silently ignored | CONFIRMED |
| 20 | B.9 `0x00af3fcf` | occupant liveness result discarded | CONFIRMED |
| 21 | B.5 `0x00a8eeb0`; B.6 `0x00abf810` | null base on `+0xbd0` ≠ 1 / wild bit write on a missing component | CONFIRMED shape, unreachable from these bindings |
| 22 | C.1 | the check consumes the finished record (second call answers through "no record") | CONFIRMED |
| 23 | C.4 | a co-op client stops locally without telling the host | CONFIRMED structure |
| 24 | B.4 / C.2 | flag setters with no session gate and no replication | CONFIRMED structure |

## K. Clean-room check

- Addresses are plain hex throughout; no Ghidra auto-names for functions, globals or labels, no decompiler variable names,
  and no pasted pseudocode. Library routines are named only where Ghidra identified them as standard C runtime
  (`_stricmp`, the floor routine and the math dispatcher described by role); short game strings are quoted as data.
- Self-check run on this file (sections A-J, before this section was added) with the pattern
  `\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4}|LAB_[0-9a-fA-F]+|FUN_[0-9a-fA-F]+|DAT_[0-9a-fA-F]+)\b`:
  **0 hits**. The pattern was first checked against 23 positive controls (one sample of every auto-named local family,
  both stack-array spellings, the register-input, parameter and type-name forms, and the three label prefixes), all 23
  matched, and 6 negative controls (plain English such as "left undefined", "in ECX", "the local player", a bare `0x…`
  address and the binding name `vehicle_in_air`), none matched. The same check re-run on the finished file including
  this section also gives **0 hits** (the pattern text above cannot match itself: every alternative needs a digit, hex
  digit, letter or underscore where the text has a bracket).
- No spec file was edited. The private Ghidra copy `tools\gp_t15` was deleted after the dumps; raw dumps stayed in the
  session scratchpad.
