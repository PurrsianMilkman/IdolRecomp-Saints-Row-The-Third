# Ranking tranche 14 — 25 previously-unspecced Lua-bound names (Team A, 2026-10-02)

Job definition: `D:\Project Crreish\TEAM A\ghidra\jobs\ranking\tranche-14.json` (names 276-300 of the 554 unspecced
names, Team B call-count order). Run locally, read-only, on a private copy of the Ghidra project (`tools\gp_t14`,
deleted afterwards), with the job's own arguments: `CrreishDump.java <out>/lua lua depth:1 maxfuncs:15 maxinsn:500
<25 names>`. **All 25 names resolved.** Every name string occurs exactly once in `.rdata`. For 23 names the handler is
the code pointer stored in the stack slot right after the name (insn offset +1) of the gameplay registrar
`0x00a20840` — CONFIRMED — dump `index.txt`. The other two (`whored_countdown_finished`,
`vint_options_remap_reset_bindings`) are registered by small push/set-global sub-registrars; the dumper rooted both at
the set-global primitive `0x00dfe830`, so their real handlers were taken from a follow-up dump of each sub-registrar
(section A) — CONFIRMED. Follow-up runs on the same private copy are listed in section H and cited as "follow-up dump".

Team B's reconciliation table (`for-team-b/team-b/tools/lua_reconciliation_called_and_registered_1181.tsv`): the seven
`action_*`/`ai_*`/`ambient_*` names have **2 call sites** each (`ai_force_team_idle` and the three `action_sequence_*`
in 2 distinct scripts; `ambient_cop_spawn_enable` and the two `action_nodes_*` in 1); the other 18 have **1 call site in
1 script**. `whored_countdown_finished` and `vint_options_remap_reset_bindings` are tagged `ui`, everything else
`gameplay` — agrees with the registrars found here.

Labels: **CONFIRMED** = read in the listing or decompile of a dump made for this note; **HIGH CONFIDENCE** = follows
from a dumped call or reference, but the callee body was not dumped or a meaning is inferred from strong usage;
**HYPOTHESIS** = plausible, not settled; **OPEN** = not settled (collected in section J).

**Shared conventions, cited not re-described:** every entry opens with the `lua_gettop` prologue (`0x00dfde50`) and
reads arguments with negative stack indices counted from the bottom (`-N` = arg 1, `1-N` = arg 2, …). Primitives as in
`spec-lua-api-behaviour.md` §4.1 and tranches 09-11: `0x00dfe210` `lua_tolstring` (**null** for nil or a non-string,
non-number slot), `0x00dfe1e0` `lua_toboolean` (missing → false), `0x00dfe040` `lua_type` (0 = nil), `0x00dfe160`
`lua_tonumber` (non-number → 0), `0x00ea2596` truncating float-to-int, `0x00dfe590` `lua_pushboolean`, `0x00dfe3a0`
`lua_pushnumber`. The **standard optional idiom** (tranche 10) means the slot is read only when enough arguments were
passed and `lua_type` is not nil, otherwise a stated default is used. `0x009da4e0` = local player (global
`0x0262edfc`), `0x0087ba20` = session; "host" = session `+0x5c` == `+0x58`. Name hash `0x00d9e8b0` as tranche 09.

**Shared machinery this tranche reuses (cited, not re-derived):**

- **Vehicle resolver `0x00a281e0`** (tranche 11 front matter; spec §4.5): name → vehicle object or null; a character
  name yields that character's vehicle.
- **Local-ownership test `0x008addb0(obj, 0)`** — returns **true for a null object** (tranche 11, CONFIRMED there). This
  matters for crash item 4 below.
- **Liveness guard `0x00853b10`** (tranche 10): 1 = dead / null.
- **Handle → object** `0x00458230` on table `0x024433a8` with key `0x031d152c`, then the `+0x33` bit `0x10` rejection
  and a kind-row test `0x02cc9900[obj +0x34]` (tranche 11). Kind bits seen here: row `+6` bit `0x80` = vehicle, row
  `+0xa` bit `0x04` = human, row `+0xb` bit `0x02` = the `0x005982e0` "object" resolver kind, row `+0x8` bit `0x02` =
  path (`0x0062a1f0`, tranche 10).
- **Record-and-replicate, double-gate variant** (spec §7 preamble variant 1; tranche 10 front matter): `0x008ae480`
  on the owner handle, then `0x008837a0`; either true → write the flag bit locally; both false → look up the owner peer
  `0x008ae3a0(handle)`, open an opcode-`0x46` record with two debug-tag strings, the handle and the boolean, send it to
  that peer (`0x0086f110`) and **do not write locally**; no peer → silently nothing.
- **Single-gate broadcast variant** (spec §7 preamble variant 2): `0x008addb0` true → apply locally; otherwise open a
  record and commit it with `0x0086f1b0(session, 0, 0)`, which is a whole-session broadcast and a no-op without a
  session (`interp_tabs.md` Q6). The 16-bit object id is written by `0x008add70` followed by `0x00bc5610`
  (tranche 04 §13.20 pair).

**New flag-bit facts established in this tranche** (all CONFIRMED — disassembly of the setter's merge idiom; the
quoted tag is the setter's own second debug string):

| field | bit | meaning (from tag) | writer | reached from |
|---|---|---|---|---|
| vehicle `+0x1d7a` | `0x01` | `m_force_flagsinvulnerable` | `0x00a7a830` | read by `vehicle_is_invulnerable` (E.10) |
| vehicle `+0x1d7a` | `0x02` | `m_force_flagsplayer_damage_always_applied` | `0x00a7a9a0` | (not in this tranche) |
| vehicle `+0x1d7b` | `0x04` | `m_force_flagsforce_lights_on` | `0x00a7b800` | `vehicle_lights_on` |
| vehicle `+0x1d7b` | `0x08` | `m_force_flagsforce_lights_off` | `0x00a7b970` | `vehicle_lights_on` (always cleared) |
| vehicle `+0x1d7c` | `0x04` | `m_force_flagsradio_controls_locked` | `0x00a7c380` | `vehicle_set_radio_controls_locked` |
| vehicle `+0x1d7c` | `0x08` | `m_force_flagssirenlights_on` | `0x00a7c4f0` | `vehicle_set_sirens` |
| vehicle `+0x1d7c` | `0x10` | `m_force_flagsheadlights_flash_on` | `0x00a7c660` | `vehicle_set_sirens` |
| vehicle `+0x1d7e` | `0x40` | `m_force_flagsdisable_velocity_sync` | `0x00a7dbf0` | `vtol_flyby_animation_for_m18` |
| vehicle AI block (`+0xc68`) byte `+4` | `0x01` | `vai_force_flagsobey_traffic_lights` | `0x00b22a80` | `vehicle_set_obey_traffic_lights` |
| vehicle AI block byte `+6` (= vehicle `+0xc6e`) | `0x20` | `vai_force_flagsuse_short_cuts` | `0x00b24550` | `vehicle_set_use_short_cuts` |

The first row **settles the open conflict in `spec-lua-api-behaviour.md` §3.7/§20.1** ("§20.1 gives bit `0x8` for
`0x00a7a830`; to be re-derived"): `0x00a7a830` merges the boolean into **bit `0x01`** of `+0x1d7a` (`0x00a7a955`-
`0x00a7a965`: load, XOR with the argument, AND `0x1`, XOR back — no shift), and `0x00a7a9a0` into bit `0x02`
(`0x00a7aacc` doubles the argument first, mask `0x2`). The reader `vehicle_is_invulnerable` tests bit `0x01` of the same
byte. §3.7's `0x01`/`0x02` is right; §20.1's `0x8` is wrong. CONFIRMED — follow-up dump. The AI-block setters are
`this` calls on vehicle `+0xc68` and use the AI block's own `+0x4e0`/`+0x4e4` as the owner handle; the vehicle-level
setters use vehicle `+8`/`+0xc`.

---

## A. Registrars — where the 25 names live

| registrar | reached from | names |
|---|---|---|
| gameplay `0x00a20840` | (established) | 23: everything except the two below |
| `0x006147c0` | UI registrar `0x008430f0`, call at `0x008431f2` | `whored_countdown_finished` → handler **`0x006147a0`** |
| `0x007cdc00` | UI registrar `0x008430f0`, call at `0x00843189` | `vint_options_remap_set_key_binding` → `0x007cd3d0`; `vint_options_remap_reset_bindings` → handler **`0x007cd0e0`** |

Both sub-registrars push the handler with `lua_pushcclosure` (`0x00dfe4f0`) and store it with the set-global
primitive `0x00dfe830` using index -10002 (`0xffffd8ee`, the globals pseudo-index). `0x006147c0` null-checks the Lua
state first; `0x007cdc00` does not (harmless; the UI registrar passes a live state). Neither handler is a defined
function in the project database (both were read with a `range` run). CONFIRMED — follow-up dump and range run.

**Index oddity, not a second binding:** `action_nodes_restrict_spawning`'s handler `0x00a3c050` shows a second DATA
reference at `0x00a25f55`. That is the gameplay registrar's own `lua_pushcclosure`/`lua_setfield` loop
(`0x00a25f47`-`0x00a25f70`) reading the second table row; the first row is `action_nodes_enable` → `0x00a3c020`. Same
artifact as tranche 09's `pcu_bra_required`. The loop counter is loaded with **`0x3f8` (1016)** at `0x00a25f4b`
(range run) — see section I item 9.

---

## B. Mission action sequences — 3 functions sharing one state block

All three drive a single global "action sequence" state block at `0x012f2ae0` (CONFIRMED — the reset routine below is
called with `ECX = 0x012f2ae0`):

| address | role |
|---|---|
| `0x012f2ae0` | the sequence vehicle (raw object pointer) |
| `0x012f2ae4` (byte) | "started" latch |
| `0x012f2ae8` | step counter (reset to -1) |
| `0x012f2aec` | replicated step number (reset to -1) |
| `0x012f2af0` | sequence type: 0 or 1 when set up; **reset value and static initial value 2** |
| `0x012f2af4`, `0x012f2af8` | per-step integers copied from the step record (reset 0x42b) |
| `0x012f2b00`/`0x012f2b04` | per-step pair copied from step `+0x70`/`+0x74` |
| `0x012f2b10` | a countdown timer (`0x00d9e4c0` "is running" = value ≥ 0) |
| `0x012f2b14`-`0x012f2b40` | position/orientation copied from a step's navpoint object |
| `0x012f2b50`/`0x012f2b60`, `0x012f2b80`/`0x012f2b90` | two camera-ish vector pairs blended with 1000 ms timers |
| `0x012f264c` | a control-layer slot handle (see B.1) |
| `0x014e95d4`/`0x014e95d5`/`0x014e95d6` | "busy" byte, a co-op mode byte, "advance pending" byte |

The reset routine `0x006dc310` (`this` = `0x012f2ae0`): vehicle 0, latch 0, counter -1, replicated step -1, type 2,
both per-step integers `0x42b`, vectors set to the zero vector `0x014e95f0`, timers -1. CONFIRMED — follow-up dump.

**Step tables** (follow-up `ptrs` run, 288 dwords at `0x012f2660`): records are **0x80 bytes** (32 dwords).
Type 0 = 6 records at `0x012f2660` (indices 0..5), type 1 = 3 records at `0x012f2960` (indices 0..2); the bound checks
in B.3 match these sizes exactly (CONFIRMED arithmetic). Field use (CONFIRMED from B.3's reads; meanings HYPOTHESIS):
`+0`/`+4` two vehicle-animation names, `+8` a navpoint/object name, `+0xc`/`+0x10` local-player animation names,
`+0x14` an integer, `+0x18`/`+0x1c` co-op-partner animation names, `+0x20` an integer, `+0x24` an effect-object name,
`+0x28` a "break objects" trigger, `+0x30`/`+0x40`/`+0x50`/`+0x60` four 16-byte vectors, `+0x70`/`+0x74` two
integers. Type 0's strings are `"heli state00"`…`"heli state05"`, `"Heli trans00"`…, `"heli stand"`, `"vault_nav 001"`,
`"Second_wall_vfx"`, `"bank"`, `"Vfx_hit_building"`; type 1's are `"Start"`, `"ball_spot"`, `"m06 player aim a"`,
`"m06 coop aim a"`, `"StageOneOne"`, `"BreakThirdCable"`, `"Player Fall A"`, `"Coop Brute Fall"`, `"plat_break"`,
`"stage_mover 001"`, `"ball_spot 001"`. HYPOTHESIS: type 0 is the opening mission's helicopter-lifts-the-vault
sequence, type 1 a mission-6 set piece; both are hard-coded in the executable, not data-driven.

### B.1 `action_sequence_setup` (`0x00a3c8c0` → `0x006dd3d0`)

**Arguments:** 1 number = sequence type (truncated, read unconditionally — missing → 0); 2 string = vehicle name
(`0x00a281e0`). **Return:** 0 values. CONFIRMED.

**Body (`0x006dd3d0(type, vehicle)`, CONFIRMED — disassembly):**
1. On a session **host** with a null vehicle → return at once (nothing reset). Then type > 1 (unsigned) → return.
2. Reset the block (`0x006dc310`), zero `0x014e95e0`/`0x014e95e4`, store the type.
3. Null vehicle → store 0 and return (so outside a host session `setup(t, nil)` clears the sequence).
4. Store the raw vehicle pointer in `0x012f2ae0`. On a session host, broadcast an opcode-`0x49` record (byte 0, the
   vehicle's id via `0x0050f790`, the type via `0x0091dbc0`) with `0x0086f1b0`; otherwise set `0x012f2aec` = 0.
5. Type 0 only: `0x006dcab0(vehicle)` — looks up a `"helivault"` resource (`0x00904c10`), formats a name with the
   suffix `".rig"` (`0x00da8790`), loads/attaches it, and creates an object at the local player's position and
   orientation (`0x008ff850`) — HIGH CONFIDENCE.
6. Allocate a control-layer slot `0x007efab0()` into `0x012f264c` (a 15-slot pool of 0x40-byte records at
   `0x022b7095`, **-1 when full**), set all of its 12 categories to 0 (`0x007efd80(slot, 0xf, 0)`), category 2 to 2,
   and activate it under the debug name `"Mission Action Sequence"` (`0x007efb40`). HYPOTHESIS: this locks player
   controls for the sequence. `0x007efd80` bound-checks the slot (`< 0x14`) and `0x007efb40` skips -1, so a full pool
   does not corrupt memory — the lock is just missing (CONFIRMED).
7. Run the first step: `0x006dcdd0(1)` (B.3, forced), then `0x0056cc30`.

**Logic shapes (CONFIRMED):** the slot write at `0x006dd51d` overwrites `0x012f264c` without releasing the previous
slot; only `0x006dc8e0`/`0x006dc930` release it (they write -1). Calling setup twice without the release path leaks one
of the 15 slots each time. The host-vs-single-player difference in step 1/3 means `setup(t, nil)` can clear a sequence
in single player but never on a host.

### B.2 `action_sequence_is_playing` (`0x00a3c240` → `0x006dc680`)

**Arguments:** none read. **Return:** exactly 1 boolean. CONFIRMED.

**Body (`0x006dc680`, CONFIRMED — disassembly):** true when any of:
1. the local player exists and `0x00956fd0(player, player+0xd24 → +0x10 → +0x64)` < 1.0 (an animation-progress
   fraction: 0 when the clip is done-flagged, 1 when it has no length, else elapsed / length — HIGH CONFIDENCE);
2. a co-op partner exists (`0x00867830()` true and `0x009df3d0()` non-null — the first player in
   the player list that is not the local player, null unless byte `0x024d4462` is set) and the same progress test on
   that partner is < 1.0;
3. `0x00a75fe0(sequence vehicle)`: the vehicle is type 1 (`+0xbd0` == 1) with an animation player at `+0xa78` that has
   any active track (bit 0 set, bits 1-2 clear, 0x84-byte stride);
4. the `0x012f2b10` timer is running;
5. the busy byte `0x014e95d4` is set.
Otherwise it returns false — **and if the pending byte `0x014e95d6` is set it clears it and runs a deferred
`0x006dcdd0(0)` step (B.3) from inside this query.**

**Side effect hidden in the query (CONFIRMED):** between tests 1 and 2 it always calls `0x009e3200(0, 0)` as a `this`
call on the **local player** (`MOV ECX,ESI` at `0x006dc6b4`). `0x009e3200(mode, target)` is the player "script mode"
setter: when `0x008addb0(player, 0)` is true it writes `mode` into player `+0x2888`, the target handle into
`+0x2890`/`+0x2894` for mode 1, and bit `0x2` of `+0x28ac`; otherwise it sends an opcode-`0x46` record tagged
`"player"`/`"script_mode_params"` to the owner. So **every call to `action_sequence_is_playing` whose local animation
has finished resets the local player's script mode to 0.**

**Crash shape — null `this` write (CONFIRMED shape):** when there is **no local player**, test 1 is skipped (`JZ
0x006dc6b0`) and `0x009e3200` is entered with `ECX` = 0. `0x008addb0(null, 0)` returns true (front matter), so the
function writes `[0 + 0x2888]` at `0x009e3258` — access violation. Reachable whenever a script polls
`action_sequence_is_playing` while the local player object does not exist (loading, teardown); the same path is reached
from B.3's unforced step when the busy logic calls `0x006dc680`.

### B.3 `action_sequence_advance` (`0x00a3c1d0` → `0x006dcdd0`)

**Arguments:** 1 optional boolean "force" (standard optional idiom; missing or nil → false). **Return:** 0 values.
CONFIRMED.

**Body (`0x006dcdd0(force)`, CONFIRMED — disassembly unless tagged):**
1. Busy byte `0x014e95d4` set and not forced → set the pending byte `0x014e95d6` and return (B.2 later runs it).
2. Latch `0x012f2ae4` clear → require a non-null sequence vehicle with `+0xbd0` == 1 and an animation player at
   `+0xa78`, else return; then set the latch.
3. Not forced: call `0x006dc680` (B.2) and, if it says "playing", call the session getter twice and **discard the
   result** (`0x006dce68`-`0x006dce7a`). The step is **not** held back: advancing never waits for the current
   animations, so scripts must poll B.2 themselves.
4. Counter `0x012f2ae8` += 1; timer `0x012f2b10` = -1. Type 0 → step record `0x012f2660 + counter·0x80` if counter ≤ 5;
   type 1 → `0x012f2960 + counter·0x80` if counter ≤ 2; past the end → return (the counter keeps growing).
5. Step `+8` names an object (`0x00643a80`: the `0x005982e0` resolver plus liveness): copy its position and
   orientation into `0x012f2b14`… and move the vehicle there through its vtable `+0x40` with flag 1.
6. Step `+0`/`+4` vehicle animations: each name → index `0x004bf810` (case-insensitive lookup in the animation-name
   table `0x03171c10`, count `0x03171c08`, 16-byte records; -1 when unknown). Both found → `0x00a75ea0(vehicle, a, b)`
   (queue a then b, blend 0.3); only the second → `0x00a75f00(vehicle, b, 0x1d4c, 0, 1.0)`. Both functions only act on
   a type-1 vehicle with an animation player (`0x00a75f00` returns -1 otherwise).
7. Step `+0xc` (and `0x014e95d5` clear) → `0x006dc140` with the animation names `+0xc`/`+0x10` on the local player
   (on a session client: on the co-op partner); step `+0x18` (when `0x00867830()` or `0x014e95d5`) → the same with
   `+0x18`/`+0x1c` on the co-op partner (on a client, or when `0x014e95d5` is set: on the local player) — so the two
   rows swap roles on a client. `0x006dc140` takes the character in **ESI** and the first name in **EAX** (hidden
   registers); it plays the animation on the character's animation player (`+0xd24 → +0x10`) and, except when the
   counter is 1 (the second record) of type 0, calls `0x009e3200(2, 0)` on the character (script mode 2) — HIGH
   CONFIDENCE.
8. Step `+0x14`, `+0x20` (unless -1) → `0x012f2af4`, `0x012f2af8`; step `+0x70`/`+0x74` → `0x012f2b00`/`0x012f2b04`.
9. Type 0 only: `0x006dcc90(0)` with `ECX` = step `+0x24` (spawns an effect at that object); if step `+0x28` is
   non-null, every live object named `"Break_roof"`, `"Break_ceiling"`, `"Break_wall"`, `"Break_light<001>"`,
   `"Break_light<002>"` (mesh-mover resolver `0x006434a0`) gets `0x00a32000` (HIGH CONFIDENCE: break it).
10. The two vector pairs blend toward step `+0x30`/`+0x50` and `+0x40`/`+0x60` (local vs. co-op variants) with
    1000 ms timers (`0x00d9e4d0(1000)`), or snap when the previous value is the zero vector.
11. Session host and counter > 0 → `0x012f2aec` = counter, broadcast an opcode-`0x49` record (byte 1, the force byte).

**Latent shapes (CONFIRMED shape, not reachable from Lua — HIGH CONFIDENCE):** with type 2 (the reset/static value)
step 4 selects **no** table and step 5 reads the null record's `+8`. Every path that leaves type 2 also clears the
vehicle and the latch, so step 2 returns first. `0x006dc140` reads character `+0xd24` with no null check when the
character in ESI is null (its only guard is "non-null and `0x0096f4f0` true → return") — reachable if a step names a
player animation while no local player exists.

**Dangling pointer (CONFIRMED shape; reachability HYPOTHESIS):** the sequence keeps the **raw vehicle pointer** from
setup and B.2/B.3 dereference it (`+0xbd0`, `+0xa78`, vtable `+0x40`) with only a null test — no handle re-resolution
and no liveness check. If the vehicle is destroyed or despawned mid-sequence (for example by
`world_despawn_all_vehicles`, D.1), the next advance or poll reads freed memory.

---

## C. AI / spawning switches — 4 functions

### C.1 `ambient_cop_spawn_enable` (`0x00a3c290`)

**Arguments:** 1 boolean (unconditional; missing → false). **Return:** 0 values.

**Body:** calls the one-line setter `0x00910820`, which stores the byte in the global **`0x01308aba`** (the setter
function is not the global). The global is file-backed with initial byte **1** (enabled). Readers: the getter
`0x00910860`, used by the spawn-group spawner `0x008bdfb0` for groups whose `+0xc8` == 1 (a sibling getter
`0x00910870` gates groups with `+0xc8` == 2), and a direct read in the spawn evaluator `0x00915da0`, case 3 of its
spawn-kind switch, which returns "no spawn" when the byte is 0. CONFIRMED — disassembly and follow-up dumps.
HIGH CONFIDENCE: kind 3 / group type 1 are the police. No replication, no host gate: each machine keeps its own value.

### C.2 `action_nodes_shouldnt_flee` (`0x00a3c090`)

**Arguments:** 1 boolean (unconditional). **Return:** 0 values.

**Body:** one-line setter `0x008bb260` → global byte **`0x024ec294`** (zero-initialised, so off by default). The getter
`0x008bb270` has two callers, the human predicates `0x004d8ff0` and `0x004eaa40` (large caller lists in the human AI
code); both return false early for a human whose byte `+0x1da7` has bit `0x10` set when the flag is 1. CONFIRMED —
follow-up dump. HIGH CONFIDENCE: bit `0x10` marks a human using an action node, and the two predicates are "may flee /
may react"; the flag makes such humans stay put.

### C.3 `action_nodes_restrict_spawning` (`0x00a3c050`)

**Arguments:** 1 boolean (unconditional). **Return:** 0 values.

**Body:** calls `0x00d223d0` (a stub that always returns 1; the result is discarded), then the one-line setter
`0x008bb250` → global byte **`0x024ea301`** (zero-initialised). Other uses: the action-node manager's init
`0x008c7540` clears it and registers its address as the debug variable `"action_node_restricted"` (`0x0086d770`), and
its tick `0x008c7890` sets the derived byte `0x024ec271` = 1 whenever the flag is set, otherwise
`0x024ec271` = (`0x00605540()` > 2). CONFIRMED — follow-up dump. HYPOTHESIS: `0x024ec271` restricts which action
nodes may spawn people, and `0x00605540` is a heat/notoriety level, so the script forces the restriction that the game
otherwise applies only at level 3 or more.

### C.4 `ai_force_team_idle` (`0x00a3c360` → `0x00609db0` → `0x00608be0`)

**Arguments:** 1 string = team name (unconditional). **Return:** 0 values.

**Body (CONFIRMED — disassembly):**
1. Team id = `0x0094cc60(name)`: hashes the name with `0x00dab330` (lowercased multiply-by-33/XOR hash modulo 32) and
   looks it up in the name→code table at **`0x02623a80`** (`0x005961a0`); found → the byte `0x02623b48[index·4]`;
   **not found → 9**. This is the same runtime-populated table WALLS.md records for `notoriety_force_no_spawn` (its
   storage is zero-fill, not file data).
2. `0x00608be0(team)`: on a session **host**, first broadcast an opcode-`0x3f` record (8-bit sub-code 6, the team byte).
3. Every human in the singleton list (`0x03171a64 +0x204` count, `+0x1fc` 16-bit index array, `+0x58` object table)
   whose team byte `+0x1ca8` equals the team — **or every human when the team is 9** — that is alive, not
   `0x0096f4f0`, and locally owned: cancel its activity (`0x004e6b70`, `0x009659f0(h, 0)`), then a state-dependent reset
   (`0x009b9160` → `0x004e4130(h, 0x19)`; `0x009b9210` or `0x00af1a00(handle)` → clear the human's slot in
   `0x00b08380`'s table; `0x009b9230` → zero `+0x1770`/`+0x1774` and clear the associated vehicle's `+0x16c0` bit
   `0x10` and `+0x16e8`/`+0x16ec`; otherwise optional `0x00989900`, then `+0x501` == 9 → `0x004f5dd0(0x37, …)` or a
   fallback `0x004e4130(h, 0)`), and finally `0x004f5c50(h)` and `0x004ed260(handle)`. (Per-branch meanings HYPOTHESIS.)
4. Every vehicle in the circular list at `0x027c2eb8` (next at `+0xbec`) whose team byte `+0x16e0` matches (or team 9),
   alive, not `0x00ad5100`, locally owned: by AI mode byte `+0xc68` — 2, 4, 9, 10 → `0x00b63ac0(AI, 0, 0, 0)`;
   3 → zero `+0xff8`/`+0xffc` and `0x00521030(v, 0, 0)`; 12 → `0x00b39ab0(v)`.

**Crash shape — null read (CONFIRMED):** a nil or non-string argument makes `lua_tolstring` return null and
`0x00dab330` reads the first character through it (`0x0094cc69` → `0x00dab330`, no null check).

**Logic hazard (CONFIRMED):** an unknown or misspelt team name maps to 9, which the loops treat as "all teams", so the
call idles **every** human and vehicle the machine owns (including, if not excluded by `0x0096f4f0`, the player's
allies). A co-op client sends nothing and only affects objects it owns.

---

## D. World despawn — 2 functions

### D.1 `world_despawn_all_vehicles` (`0x00a686b0` → `0x0090f7e0`)

**Arguments:** none read. **Return:** 0 values.

**Body (CONFIRMED — disassembly):** walks the circular record list at **`0x026093ac`** (records: `+0` next, `+4` prev,
`+8`/`+0xc` a vehicle handle). For each record it computes the successor **before** acting (successor = next, or stop
when next is the current head), resolves the handle (vehicle kind, not dead; otherwise treated as null) and asks
`0x009092d0` (vehicle in **EDI**, hidden register): null vehicle → yes; otherwise yes only if the vehicle is locally
owned, `0x00ad39d0` is false, none of the three per-seat objects (`0x00ab5070`, `0x00ab5150`, `0x00ab51a0`, for each
seat up to `0x00ab4af0`) has flag `0x21` (`0x00853b30`), human bit 4 of `+0x1da5` (`0x0094f6c0`), `0x0097e280` or
`0x0094f690`, and no player's vehicle-handle pair `+0x2880`/`+0x2884` equals this vehicle. Yes → `0x0090ccd0` (record
in EDI): despawn the vehicle with `0x00905800` (its locally-owned seat occupants first, then the vehicle, all through
`0x00853ea0`) and move the record to the free list `0x02609124` (`0x00905790`, record in EAX; count `0x02609120`
decremented).

The successor rule is correct when the head record itself is removed (CONFIRMED by hand-tracing the unlink in
`0x00905790`). HYPOTHESIS: `0x026093ac` is the ambient/traffic vehicle list, so mission vehicles not in it are untouched.

### D.2 `world_despawn_all_pedestrians` (`0x00a68a10`)

**Arguments:** none read. **Return:** 0 values.

**Body (CONFIRMED — disassembly):**
1. `0x008d98a0`: every record in the singleton's group list (`+0x300` count, `+0x2f8` index array) whose state `+0x31c`
   is non-zero gets `0x008d9450` (`this` call) and its state set to **10**. `0x008d9450` releases all 20 member slots
   (0x20-byte entries at `+0x88`): unless the group is the one whose handle is recorded at `0x0250ab58`/`0x0250ab60`
   (host vs. non-host copy), each live human member gets `+0x1da4` bit `0x80` cleared and its AI reset, and when the
   slot's second handle resolves to a live object of kind row `+0xc` bit `0x01` and the member is locally owned,
   `0x008c4d10(object, member, 0, 1)`; then every slot is zeroed. HYPOTHESIS: pedestrian groups / crowds.
2. Every human in the human list (`+0x204`/`+0x1fc`) with `+0x1da5` bit `0x80` set, **not** `0x0094f6c0` (human with
   `+0x1da5` bit `0x10`), `0x0097e280` == 0 and locally owned → `0x00853ea0(h, 0, 0)` (release through
   `0x00457730`).

**Iterate-while-deleting (HIGH CONFIDENCE safe; OPEN):** the loop caches the count once and re-reads the index array
each step. `0x00457730` does not remove the object from that array itself: it sets object flag bits at `+0x33`, calls
the object's vtable `+0x28` hook, and queues it on a per-bucket pending list (`0x00457030`, singleton `+0x2684`) —
deletion is deferred. The vtable hook was not traced.

HYPOTHESIS: `+0x1da5` bit `0x80` = "ambient pedestrian", bit `0x10` = "protected / scripted".

---

## E. Vehicle flags and queries — 11 functions

All take the vehicle name as arg 1 (`0x00a281e0`; an unresolved name → nothing happens, or false/0 for queries).

### E.1 `vehicle_set_use_short_cuts` (`0x00a63490`)
Arg 2 boolean (unconditional). `0x00b24550` on the AI block (`vehicle +0xc68`): double-gate setter, bit `0x20` of
AI byte `+6`. CONFIRMED. The same byte (vehicle `+0xc6e`, mask `0x21`) is read by `vehicle_turret_base_to_do` (F.1) and
inside the drive-to routine `0x00b2b630` (adds `0x8000` to its flags) — so this flag feeds scripted driving.

### E.2 `vehicle_set_obey_traffic_lights` (`0x00a63110`)
Arg 2 boolean (unconditional). `0x00b22a80` on the AI block: double-gate setter, bit `0x01` of AI byte `+4`. CONFIRMED.

### E.3 `vehicle_set_radio_controls_locked` (`0x00a63170`)
Arg 2 boolean (unconditional). `0x00a7c380` on the vehicle: double-gate setter, vehicle `+0x1d7c` bit `0x04`. CONFIRMED.

### E.4 `vehicle_lights_on` (`0x00a62900`)
Arg 2 optional boolean, **default true**. Calls `0x00a7b800(arg)` (force-lights-on, `+0x1d7b` bit `0x04`) and then
**always** `0x00a7b970(0)` (force-lights-off, bit `0x08`, cleared). CONFIRMED. So `vehicle_lights_on(v, false)` clears
**both** forces — the lights return to automatic; it does not switch them off. (Naming trap. The neighbouring
registration slot `0x00a62880` also calls `0x00a7b800`, at `0x00a628ee` — HYPOTHESIS: a "lights off" partner; its name
was not checked.)

### E.5 `vehicle_set_sirens` (`0x00a644f0` → `0x00ad57a0`)
Arg 2 boolean (unconditional). `0x00ad57a0(handle, on)` (CONFIRMED — disassembly):
1. `0x00ad55a0(handle, on)`: for a live type-1 vehicle, switch on the vehicle-class field (vehicle `+0xbf4` → `+0x4c4`):
   classes 3, 5, 6, 7, 8, 11 → headlights-flash (`0x00a7c660`) **and** sirenlights (`0x00a7c4f0`) = on; class 12 →
   sirenlights only; any other class → both = on if the capability bit (`+0xa88 → +0x50` bit `0x2`) is set, else
   **both forced off whatever the argument** and return. Then the siren-sound controller `0x00559b40(handle, on)`.
2. Re-resolve the handle; if the vehicle is alive and `+0x163c` bit `0x10` is clear, call `0x00559b40(handle, on)`
   again — so for most vehicles the sound controller runs twice, and for a vehicle without sirens it still runs once.

`0x00559b40` toggles `+0x163d` bit 0 and starts (needs a seat-0 occupant and `0x005598d0`) or stops the looping sound
held at `+0x1628` (sound hash `0x762b3179`). HIGH CONFIDENCE. The two light flags are double-gate setters; the sound
part is local. HYPOTHESIS: classes 3/5-8/11 are emergency vehicles.

### E.6 `vehicle_set_npc_engine_audio` (`0x00a630b0`)
Arg 2 boolean (unconditional). Writes bit `0x10` of vehicle dword `+0x16c4` **directly** — no ownership gate, no
replication. CONFIRMED. Reader not traced (OPEN).

### E.7 `vehicle_set_kneecappers_damage` (`0x00a64430`)
Arg 2 number, truncated. If ≥ 0: `0x00ab6c70(vehicle, n)` stores the integer at `+0x78` of the sub-object at vehicle
`+0x1550` (skipped when that pointer is null). Negative values are ignored. Local only, no replication. CONFIRMED.
Meaning of the field OPEN.

### E.8 `vehicle_set_tire_durability` (`0x00a65910`) and E.9 `vehicle_set_tire_damage_multiplier` (`0x00a65760`)
Arg 2 number (float, unconditional). Single-gate broadcast variant (CONFIRMED — disassembly):
- vehicle resolved **and** locally owned → `0x00ab6b70` / `0x00ab6ba0`: store the float at `+0x1d0` / `+0x1d4` of the
  sub-object at vehicle `+0x1544` — **only when the value is > 0**.
- otherwise (not owned **or not resolved**) → broadcast an opcode-`0x43` record with 8-bit sub-code **`0x23`** /
  **`0x24`**, the 16-bit vehicle id (left 0 when the name did not resolve) and the float, via `0x0086f1b0`.

So 0 or a negative value is silently ignored (the field cannot be reset to 0 from Lua), and an unresolved name in a
session broadcasts a record for id 0 (in single player there is no session, so nothing is sent). CONFIRMED logic.

### E.10 `vehicle_is_invulnerable` (`0x00a627d0`)
**Return:** exactly 1 boolean = vehicle `+0x1d7a` bit `0x01`; false for an unresolved name. CONFIRMED. Settles the
§3.7/§20.1 conflict (front matter).

### E.11 `vehicle_tire_indicators_alive` (`0x00a64690` → `0x00a3af60`)
**Return:** exactly 1 number (pushed as unsigned); 0 for an unresolved name. `0x00a3af60(v)` (CONFIRMED — disassembly):
- vehicle-kind object with `+0x33` bit `0x08` set → count the entries i < `0x00ad6770(v)` for which
  `0x00ad6880(v, i, 0)` is non-zero (type-1 vehicles only: block at `+0x7fc`, count `+0x20`, entry pointers at
  `+0x24 + 4i`, entry `+0x10` ≥ 0);
- otherwise use the object at vehicle `+0xbf8` (null → 0): if its byte `+0xf4` is 0 → **8**; else the number of the
  eight floats `+0xf8`…`+0x114` that are > 0.

**Logic shape (CONFIRMED):** a disabled indicator object (`+0xf4` = 0) reports 8 = "all alive". HYPOTHESIS: the eight
floats are per-tire health for a mission vehicle with tire indicators.

---

## F. Scripted vehicle motion — 2 functions

### F.1 `vehicle_turret_base_to_do` (`0x00a683f0`)

**Arguments:** 1 string vehicle; 2 string target name; 3 boolean (unconditional). **Return:** exactly 1 value —
**false** (boolean) when the vehicle doesn't resolve, has no seat-0 occupant (`0x00ab5070(v, 0)`), or the target name
resolves to nothing; otherwise a **number** 0, 1 or 2. CONFIRMED.

**Body (CONFIRMED — disassembly):**
1. Require the vehicle and its seat-0 occupant (HIGH CONFIDENCE: `0x00ab5070` returns the occupant of seat i from the
   handle at vehicle `+0x1358 + 0x30·i`; seat count `+0x824` for type 1, else a fixed 8).
2. Target string empty → no target. Otherwise try it as a **path** (`0x0062a1f0`, alive); else as an object
   (`0x005982e0`, alive); neither → push false (`0x0059f8a0`) and return.
3. Path → `0x00a3b860` fills a stack array of 16-byte points: the vehicle's own position first, then the path's nodes
   (`0x00930e90`, count at path `+0x33c`), **capped at 25 points in total**; count < 1 → push false. Object → one point
   (its position, `0x00da38e0`).
4. `0x006f62b0(v, 2, 0)` cancels pending "kind 2" requests for this vehicle and allocates a 16-bit request id
   (12-bit counter at `0x014f6b28` plus a machine nibble from `0x008677f0`).
5. Result = `0x00b2b630(v, points, count, vehicle +0xc6e & 0x21 ≠ 0, arg 3, 0, 0, 0, -1.0, id)`, pushed as a number.
   `0x00b2b630` (the shared "drive along points" routine) returns **0** when count is outside **2..25**
   (`0x00b2b679`-`0x00b2b683`) or no route was built, **2** after forwarding an opcode-`0x42` record (sub-code `0x1b`)
   to the owner when the vehicle is not locally owned, and **1** after starting the drive locally.

**Crash shape — null string read (CONFIRMED):** the target-string length loop runs **before** its null test:
`0x00a68481` copies the arg-2 pointer, `0x00a68492` reads a byte through it, and the null test is only at `0x00a6849d`.
A nil or non-string arg 2 (with a resolvable vehicle that has a driver) dereferences null.

**Logic shapes (CONFIRMED):** an **object** target always yields 0 (one point, below the minimum of 2), so only path
targets can work; a path with no nodes also yields 0; a path longer than 24 nodes is silently truncated. Return type
is mixed (boolean false vs. number 0).

### F.2 `vtol_flyby_animation_for_m18` (`0x00a66600`)

**Arguments:** 1 string vehicle; 2 string animation name; 3 optional string target object; 4 optional number (default
the float at `0x01117a4c`, HIGH CONFIDENCE 1.0). Nil arg 3 is skipped correctly (arg 4 is still read from slot 4).
**Return:** 0 values. CONFIRMED.

**Body (CONFIRMED — disassembly):** returns if the vehicle doesn't resolve or the animation name is unknown
(`0x004bf810` → -1). Then: `0x00abadc0(v, 1)` (type-1 vehicles: set `+0x800` bit `0x2`, then `0x00aba3e0` when
`+0x33` bit 8); `0x00abe510(handle, 1)` (owned type-1 vehicle with `+0x81c` set: zero two vectors through
`0x00524870`/`0x00524890`, set `+0x16c8` bit `0x2000000`, call `0x009eccb0` when it is the local player's vehicle
(player `+0x2880`/`+0x2884`) — meanings HYPOTHESIS; not owned → opcode-`0x42` sub-code `0x18` to the owner);
`0x00a7dbf0(1)` (disable-velocity-sync flag, double gate). If arg 3 names a live object (`0x005982e0`),
`0x00a874f0(v, its position, its orientation, 0, 1)` teleports the vehicle there (locally, or opcode-`0x42` sub-code
`0x17` to the owner). Then `0x00a75f00(v, anim, 0x1d4c, 0x200000, arg 4)` plays the animation, and finally an
opcode-`0x43` record (sub-code **`0x25`**, vehicle id, animation index, target id via `0x00a017c0`, arg 4) is broadcast
with `0x0086f1b0` — **no host or ownership gate**.

HYPOTHESIS: a one-off helper for mission 18's VTOL fly-by; the unconditional broadcast suggests the receiving side
replays it on every peer.

---

## G. Miscellaneous — 3 functions

### G.1 `waypoint_is_placed` (`0x00a68680`)
**Return:** exactly 1 boolean = `0x007d2210(0)` non-null. `0x007d2210(p)` selects player p's waypoint record
(`0x012fdff0` for 0, `0x012feb20` otherwise) and returns record `+0xa84` (HIGH CONFIDENCE the waypoint position) when
`+0xa70` is non-zero or byte `+0xa91` is set, else null. CONFIRMED. Only record 0 is consulted, so a co-op partner's
waypoint never counts (CONFIRMED; whether record 0 is always the local player is OPEN).

### G.2 `whored_countdown_finished` (`0x006147a0`, UI)
**Arguments:** none read. **Return:** 0 values. **Body:** unless byte `0x014b2a9d` is set, `0x00614690`: set byte
`0x014b2abc` = 1, call `0x00d34d20` (a stub returning 1), broadcast an opcode-`0x45` record (sub-code **`0x15`**, one
zero byte) through `0x0086f1b0` — no host gate. CONFIRMED — range run and follow-up dump.

**Dead guard / no direct reader (CONFIRMED — xref run, reference manager plus raw scan):** `0x014b2a9d` has exactly
one use (this read) and is never written, so the guard always passes; `0x014b2abc` has two writers (here and
`0x00614530`) and **no direct reader**. Unless it is read through a structure base (OPEN), in single player this call
has no observable effect; in a session it only sends the record. HYPOTHESIS: "Whored" is the wave/horde mode and the
record tells peers the pre-wave countdown ended.

### G.3 `vint_options_remap_reset_bindings` (`0x007cd0e0`, UI)
**Arguments:** none read. **Return:** 0 values. **Body:** `0x005be990()` (CONFIRMED — follow-up dump):
1. For each record of the array at `0x0142a010` (count `0x0142a018`, stride `0xce8`): `0x005bcc80(record)` — walks the
   record's 20-byte default entries (`+0` action, `+4`/`+8` keys, `+0xc`/`+0x10` modifiers), skips all-zero entries,
   and copies keys < 256 and modifiers that are -1 or < 7 into the binding table `0x0140ffa4 + action·0x20`.
2. For each record of `0x01436ea0` (count `0x01436ea8`, stride `0x568`): `0x005bcd80(record)` — re-applies its 0x28-byte
   entries through `0x005babd0`.
3. `0x005bab40()`: clear and rebuild the 256-byte "key in use" map `0x014123bc` from both binding tables.

HYPOTHESIS: the two arrays are the on-foot and vehicle control-scheme defaults (cf. tranche 10's set A / set B), so this
restores default key bindings in memory; whether the result is saved is OPEN.

**Latent shapes (CONFIRMED shape, data-gated):** the action index from the defaults data is used **unbounded** to index
`0x0140ffa4`; the modifier test is signed, so any negative modifier passes, not just -1.

---

## H. Method notes for the follow-up dumps

All on the private copy `tools\gp_t14`:
- **func run 1** (depth 0, `maxinsn:900`): `0x006147c0 0x007cdc00 0x006dc310 0x006dcab0 0x006dc140 0x006dcc90
  0x00643a80 0x00a75ea0 0x009e3200 0x00956fd0 0x00867830 0x009df3d0 0x00a75fe0 0x00d9e4c0 0x006dc2a0 0x00dab330
  0x005961a0 0x009092d0 0x0090ccd0 0x008d9450 0x00457730 0x00ad55a0 0x00559b40 0x00ad6770 0x00ad6880 0x00910860
  0x00915da0 0x008bb270 0x008c7890 0x008c7540 0x00930e90`;
- **range run**: `0x006147a0-0x006147bf`, `0x007cd0e0-0x007cd3cf` (the two UI handlers, undefined as functions), then
  `0x00a25f40-0x00a25f70` and `0x00a20850-0x00a20880` (gameplay registrar loop and table rows);
- **func run 2**: `0x00614690 0x005be990 0x004d8ff0 0x004eaa40 0x007efab0 0x007efd80 0x007efb40 0x0056cc30 0x008bdfb0
  0x00a7c4f0 0x00a7c660`;
- **func run 3**: `0x00d34d20 0x005bcc80 0x005bcd80 0x005bab40 0x006dc8e0 0x006dc930`; **xref run**: `0x014b2a9d
  0x014b2abc 0x012f264c`; **ptrs run**: 288 dwords at `0x012f2660`;
- **func run 4** (listing only): `0x00a7a830 0x00a7a9a0`; **func run 5**: `0x00457030 0x00905800 0x00905790`.

Callee bodies read from the main dump's depth-1 sections (not re-dumped): `0x00910820`, `0x008bb250`, `0x008bb260`,
`0x00d223d0`, `0x0094cc60`, `0x00608be0` (decompile only), `0x006dc680`, `0x006dd3d0`, `0x006dcdd0`, `0x0090f7e0`,
`0x008d98a0`, `0x0094f6c0`, `0x0097e280`, `0x00853ea0`, `0x007d2210`, `0x00ab6c70`, `0x00ab6b70`, `0x00ab6ba0`,
`0x00a3af60`, `0x00b24550`, `0x00b22a80`, `0x00a7c380`, `0x00a7b800`, `0x00a7b970`, `0x00ab5070`, `0x0059f8a0`,
`0x00a3b860`, `0x006f62b0`, `0x00b2b630`, `0x004bf810`, `0x00abadc0`, `0x00abe510`, `0x00a7dbf0`, `0x00a874f0`,
`0x00a75f00`, `0x008add70`, `0x00bc5610`.

---

## I. Cross-function observations

1. **The §3.7/§20.1 invulnerable-bit conflict is settled** (front matter): bit `0x01` of vehicle `+0x1d7a`, set by
   `0x00a7a830` and read by `vehicle_is_invulnerable`. `spec-lua-api-behaviour.md` §20.1 should be corrected from `0x8`.
2. **Ten new vehicle flag bits** with their engine tag names (front-matter table), across vehicle `+0x1d7a`-`+0x1d7e`
   and the vehicle AI block at `+0xc68`. The AI block has its own owner handle at `+0x4e0`/`+0x4e4`.
3. **Four replication behaviours in one tranche:** double-gate setters (E.1-E.5, F.2's velocity-sync flag); single-gate
   broadcast with **no local fallback for an unresolved vehicle** (E.8/E.9); unconditional broadcast with no host or
   ownership gate (F.2, G.2); and **no replication at all** (C.1-C.3, E.6, E.7). In a co-op session a client calling
   E.6/E.7 changes only its own copy.
4. **Opcode-`0x43` sub-codes** seen: `0x23` tire durability, `0x24` tire damage multiplier, `0x25` VTOL fly-by;
   opcode `0x42` sub-codes `0x17` (teleport, `0x00a874f0`), `0x18` (`0x00abe510`), `0x1b` (drive-to, `0x00b2b630`);
   opcode `0x49` (action sequence: byte 0 = setup, byte 1 = advance); opcode `0x3f` sub-code 6 (team idle); opcode `0x45`
   sub-code `0x15` (Whored countdown). Wire formats beyond the field order given here are not covered.
5. **Hard-coded mission content.** The action-sequence step tables (B) and `vtol_flyby_animation_for_m18` (F.2) embed
   mission-specific object, animation and effect names in the executable; a reimplementation must carry the tables.
6. **Hidden-register conventions:** `0x006dc140` takes the character in ESI and a name in EAX (B.3); `0x009092d0` and
   `0x0090ccd0` take the vehicle / record in EDI and `0x00905790` the record in EAX (D.1); `0x009e3200`, `0x00b24550`,
   `0x00b22a80`, `0x00a7c380`, `0x00a7b800`, `0x00a7b970`, `0x00a7dbf0`, `0x00a7a830` are `this` calls (ECX); the
   decompiler hides all of these.
7. **Queries with side effects:** `action_sequence_is_playing` resets the local player's script mode and can run a
   deferred sequence step (B.2).
8. **"Name not found" defaults that widen scope:** `ai_force_team_idle`'s unknown team → all teams (C.4). Compare
   `notoriety_force_no_spawn` (spec §3.6), which uses the same name table.
9. **Registrar size:** the gameplay registrar's push/set loop runs **`0x3f8` = 1016** times (range run, `0x00a25f4b`).
   WALLS.md describes the same table as 1,014 entries (from a count of its unrolled stores). Either the table has 1016
   rows or two loop rows read past it; not checked here (OPEN).

## J. OPEN

- B: the writer of the busy byte `0x014e95d4` and of `0x014e95d5`; what `0x006dd580` (the other big reader of the
  state block — HIGH CONFIDENCE the network-apply / per-frame handler) does; what releases the control-layer slot in
  normal play (`0x006dc8e0`/`0x006dc930` were dumped but their callers were not traced); meaning of the step integers
  `+0x14`/`+0x20`/`+0x70`/`+0x74`; whether the sequence vehicle can be destroyed mid-sequence (dangling pointer).
- C.1: whether "kind 3" / "group type 1" is certainly the police.
- C.3: what `0x00605540` returns and what `0x024ec271` restricts.
- C.4: the per-state branches of `0x00608be0` (which AI states they reset); what `0x0096f4f0` excludes (player allies?).
- D.1: the exact membership of the `0x026093ac` list; `0x00ad39d0`, `0x0094f690`, flag `0x21`.
- D.2: the vtable `+0x28` release hook of `0x00457730` (whether it can touch the human index array synchronously);
  what the group state 10 means.
- E.5: the vehicle-class enumeration at `+0xbf4 → +0x4c4`; `0x005598d0`.
- E.6/E.7: readers of vehicle `+0x16c4` bit `0x10` and of the `+0x1550` sub-object's `+0x78`.
- E.11: what kind of object sits at vehicle `+0xbf8`, and which vehicles have `+0x33` bit 8.
- F.1: what `0x006f62b0`'s request ids are used for; the drive-to argument meanings beyond the count check.
- G.1: whether waypoint record 0 is always the local player in co-op.
- G.2: whether `0x014b2abc` is read through a structure base; the receiver of opcode `0x45` sub-code `0x15`.
- G.3: whether the reset bindings are saved to the profile.
- I.9: the 1016 vs 1,014 registrar row count.

## K. Direct-answer table

| # | name | registrar | handler | status |
|---|---|---|---|---|
| 1 | `ambient_cop_spawn_enable` | gameplay `0x00a20840` | `0x00a3c290` | resolved — byte `0x01308aba` (default on), gates police spawning; local only |
| 2 | `ai_force_team_idle` | gameplay | `0x00a3c360` | resolved — **nil name → null read**; unknown team → **all teams** idled |
| 3 | `action_sequence_setup` | gameplay | `0x00a3c8c0` | resolved — hard-coded step tables, type 0/1; control-layer slot leak; host can't clear |
| 4 | `action_sequence_is_playing` | gameplay | `0x00a3c240` | resolved — **null-player write via `0x009e3200`**; resets player script mode; runs deferred step |
| 5 | `action_sequence_advance` | gameplay | `0x00a3c1d0` | resolved — never waits for animations; raw vehicle pointer (dangling risk) |
| 6 | `action_nodes_shouldnt_flee` | gameplay | `0x00a3c090` | resolved — byte `0x024ec294`, read by two human predicates |
| 7 | `action_nodes_restrict_spawning` | gameplay | `0x00a3c050` | resolved — byte `0x024ea301` forces `0x024ec271`; stub call discarded |
| 8 | `world_despawn_all_vehicles` | gameplay | `0x00a686b0` | resolved — list `0x026093ac`, safe successor rule, protected-occupant filter |
| 9 | `world_despawn_all_pedestrians` | gameplay | `0x00a68a10` | resolved — dissolves groups, releases ambient humans (deferred deletion) |
| 10 | `whored_countdown_finished` | UI sub `0x006147c0` | `0x006147a0` | resolved — dead guard; write-only flag; opcode `0x45`/`0x15` broadcast |
| 11 | `waypoint_is_placed` | gameplay | `0x00a68680` | resolved — record 0 only |
| 12 | `vtol_flyby_animation_for_m18` | gameplay | `0x00a66600` | resolved — teleport + animation + ungated `0x43`/`0x25` broadcast |
| 13 | `vint_options_remap_reset_bindings` | UI sub `0x007cdc00` | `0x007cd0e0` | resolved — re-applies default binding sets, rebuilds key-in-use map |
| 14 | `vehicle_turret_base_to_do` | gameplay | `0x00a683f0` | resolved — **nil target → null read**; object targets always 0; mixed return type |
| 15 | `vehicle_tire_indicators_alive` | gameplay | `0x00a64690` | resolved — count; disabled indicator reports 8 |
| 16 | `vehicle_set_use_short_cuts` | gameplay | `0x00a63490` | resolved — AI byte `+6` bit `0x20` (double gate) |
| 17 | `vehicle_set_tire_durability` | gameplay | `0x00a65910` | resolved — `+0x1544`→`+0x1d0`, > 0 only; `0x43`/`0x23` broadcast otherwise |
| 18 | `vehicle_set_tire_damage_multiplier` | gameplay | `0x00a65760` | resolved — `+0x1544`→`+0x1d4`, > 0 only; `0x43`/`0x24` broadcast otherwise |
| 19 | `vehicle_set_sirens` | gameplay | `0x00a644f0` | resolved — class-dependent light flags + siren sound (runs twice) |
| 20 | `vehicle_set_radio_controls_locked` | gameplay | `0x00a63170` | resolved — `+0x1d7c` bit `0x04` (double gate) |
| 21 | `vehicle_set_obey_traffic_lights` | gameplay | `0x00a63110` | resolved — AI byte `+4` bit `0x01` (double gate) |
| 22 | `vehicle_set_npc_engine_audio` | gameplay | `0x00a630b0` | resolved — `+0x16c4` bit `0x10`, direct, unreplicated |
| 23 | `vehicle_set_kneecappers_damage` | gameplay | `0x00a64430` | resolved — `+0x1550`→`+0x78` int ≥ 0, unreplicated |
| 24 | `vehicle_lights_on` | gameplay | `0x00a62900` | resolved — force-on = arg (default true), force-off always cleared |
| 25 | `vehicle_is_invulnerable` | gameplay | `0x00a627d0` | resolved — `+0x1d7a` bit `0x01`; settles §3.7/§20.1 conflict |

## L. Crash-shaped and logic defects (summary)

| # | where | defect | status |
|---|---|---|---|
| 1 | C.4 `0x0094cc69` → `0x00dab330` | nil/non-string team name hashed through a null pointer | CONFIRMED |
| 2 | F.1 `0x00a68492` (null test only at `0x00a6849d`) | nil/non-string target: string length read through null | CONFIRMED |
| 3 | B.2 `0x006dc6b4` → `0x009e3258` | no local player: `this`-call with null ECX, `0x008addb0(null)` true, write to `null+0x2888` | CONFIRMED shape |
| 4 | B.3 `0x006dc140` | null character in ESI reads `+0xd24` (only a non-null guard precedes) | CONFIRMED shape, reachability HYPOTHESIS |
| 5 | B.1/B.2/B.3 (`0x012f2ae0`) | raw vehicle pointer kept across frames, no re-resolve or liveness → dangling after despawn/destroy | CONFIRMED shape, reachability HYPOTHESIS |
| 6 | B.3 type 2 | null step record read when type is 2; guarded by the vehicle/latch invariants | CONFIRMED shape, not reachable from Lua (HIGH CONFIDENCE) |
| 7 | C.4 | unknown team name → 9 → every team idled | CONFIRMED |
| 8 | B.2 | query resets local player script mode and executes a deferred step | CONFIRMED |
| 9 | B.3 | advance never waits for running animations (session calls discarded) | CONFIRMED |
| 10 | B.1 `0x006dd51d` | control-layer slot overwritten without release; pool of 15 → lock silently missing | CONFIRMED shape |
| 11 | B.1 | host ignores `setup(t, nil)`; single player resets | CONFIRMED |
| 12 | F.1 | object target always fails (1 point < minimum 2); empty path fails; > 24 nodes truncated; boolean-or-number return | CONFIRMED |
| 13 | E.8/E.9 | values ≤ 0 ignored; unresolved name broadcasts a record for id 0 in a session | CONFIRMED |
| 14 | E.4 | `vehicle_lights_on(v, false)` returns lights to automatic, does not turn them off | CONFIRMED |
| 15 | E.5 | vehicles outside the class list without the capability bit get both lights forced off; sound controller runs once or twice | CONFIRMED |
| 16 | E.11 | disabled indicator object reports 8 ("all alive") | CONFIRMED |
| 17 | E.6/E.7, C.1-C.3 | no replication: a co-op client changes only its own copy | CONFIRMED |
| 18 | G.1 | only waypoint record 0 consulted | CONFIRMED |
| 19 | G.2 | guard byte never written; set flag never read directly | CONFIRMED (xref), meaning OPEN |
| 20 | G.3 `0x005bcc80` | action index from defaults data used unbounded; any negative modifier accepted | CONFIRMED shape, data-gated |
| 21 | D.2 | iterate-while-releasing over the human array | HIGH CONFIDENCE safe (deferred deletion), OPEN |
| 22 | F.2, G.2 | records broadcast with no host or ownership gate | CONFIRMED |

## M. Clean-room check

- Addresses are plain hex throughout; no Ghidra auto-names for functions, globals or labels, no decompiler variable
  names, and no pasted pseudocode. Engine strings are quoted only as short literal data (debug tags, object and
  animation names). Library routines are not named.
- Final self-check run on this file with the pattern
  `\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4}|LAB_[0-9a-fA-F]+|FUN_[0-9a-fA-F]+|DAT_[0-9a-fA-F]+)\b`:
  **0 hits**.
- No spec file was edited. The private Ghidra copy `tools\gp_t14` was deleted after the dumps; raw dumps stayed in the
  session scratchpad.
