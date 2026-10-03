# Ranking tranche 06 — 25 previously-unspecced Lua-bound names (Team A, 2026-10-02)

Job definition: `D:\Crreish-sync\for-team-a\team-a\ghidra\jobs\ranking\tranche-06.json` (names 76-100 of the 554
unspecced names, Team B call-count order). Run locally, read-only, on a private copy of the Ghidra project
(`tools\gp_t06`, deleted afterwards), with the job's own arguments: `CrreishDump.java <out>/lua lua depth:1 maxfuncs:15
maxinsn:500 <25 names>`. **All 25 names resolved in that one call.** Every name string occurs exactly once in `.rdata`
and is stored exactly once by a registrar; its function pointer is the code pointer in the very next slot (insn
offset +1) — CONFIRMED — disassembly (dump `index.txt`). A second, depth-0 `func` run on 36 callee addresses filled in
the bodies the depth-1 dump stopped at (the per-name `maxfuncs:15` cap cut several of the larger vehicle-entry
bodies short); it is cited below as "follow-up dump".

**Registrars — not all of these are gameplay names.** CONFIRMED — disassembly (index plus follow-up dumps of the
three small registrars):

| registrar | called from | names in it | this tranche's names |
|---|---|---|---|
| `0x00a20840` (gameplay, 1,014 entries) | — | — | the 12 `vehicle_*`, both `tutorial_*`, `team_make_hostile` |
| `0x008152a0` | the UI registrar `0x008430f0` (call at `0x008431da`) | 8 `store_vehicle_*` | 3 |
| `0x00817570` | the UI registrar `0x008430f0` (call at `0x008431e6`) | 5 `store_weapon_*` | 3 |
| `0x00820aa0` | the UI registrar `0x008430f0` (call at `0x008431fe`) | 20 `vcust_*` | 4 |

The three store sub-registrars share one shape: a stack table of {name, function} pairs filled by straight-line
`MOV`s, then a loop that pushes each function as a C closure (`0x00dfe4f0`) and stores it into the globals table
(`0x00dfe830` with index `-10002`). Each name pairs with exactly one function. The index also shows a second
DATA reference to `store_vehicle_change_mode`'s and `store_weapon_purchase_ammo`'s function from inside the loop body
(`0x00815345`, `0x008175e5`); those are Ghidra's stack-reference attribution of the loop's table reads, not a second
registration — the table itself holds each pointer once (HIGH CONFIDENCE). The spec already calls `0x00817570` the
"wrapper" of the "311-entry UI-wrapper table" (§16.15); these three are the same family.

Call counts are Team B's (`for-team-b/team-b/tools/lua_reconciliation_called_and_registered_1181.tsv`, total call sites
/ distinct scripts). All 25 are low-traffic: 2 call sites each, in 1 or 2 scripts.

Labels as in the other notes of this folder: **CONFIRMED — disassembly** = read in the listing or decompile of the
dumps; **HIGH CONFIDENCE** = follows from dumped instructions but a body it relies on was not dumped, or a meaning is
inferred from strong usage; **HYPOTHESIS** = plausible, not settled; **OPEN** = not settled.

**Shared conventions, cited not re-described:** every entry opens with the `lua_gettop` prologue (`0x00dfde50`) and
reads arguments with negative stack indices counted from the bottom (`-N` = arg 1, `1-N` = arg 2, …). Primitives as in
§4.1: `0x00dfe210` `lua_tolstring`, `0x00dfe1e0` `lua_toboolean`, `0x00dfe040` `lua_type`, `0x00dfe160` `lua_tonumber`,
`0x00ea2596` truncating float-to-int, `0x00dfe3a0` `lua_pushnumber`, `0x00dfe590` `lua_pushboolean`. "Standard nil-gated
idiom" = consulted only when enough arguments were passed, and `lua_type == 0` takes the default. A "bare" boolean is a
plain `lua_toboolean` with no gate, so an absent argument reads `false`. Resolvers: `0x00a281e0` dedicated vehicle
resolver (§4.5), `0x00a281a0` generic/`#PLAYER#` character chain, `0x004dcf00` the cached-vehicle-association resolver
(handle pair → vehicle-class object; third argument non-zero = also accept a dead object; §-reference at the
`+0x16c0/+0x16c4` correction near §18.31), `0x00853b10` liveness ("true = dead/invalid"), `0x00458230` on
`0x024433a8` the handle-pair lookup. Class-descriptor table `0x02cc9900` indexed by the object's kind byte `+0x34`.
Replication shapes are §7-preamble variants 1-4 ("double gate `0x008ae480`/`0x008837a0`, string-tagged", "single
gate `0x008addb0`", "single gate `0x008ae480`-only", "always both"). Session accessor `0x0087ba20`; "host" = session
`+0x5c == +0x58`. Local player `0x009da4e0`.

**Character vehicle-state field `+0x16d4` (new, used by many entries below).** CONFIRMED — disassembly (follow-up
dumps): `0x009b9160` = state is **3**; `0x009b9210` = state is **1** (no null check); `0x009b91e0` = state is **1 or 2**
(null → false); `0x009b9290(character, vehicle)` = state is 3 **and** the character's cached vehicle handle pair
(`+0x16c0/+0x16c4`) equals the vehicle's own pair (`+8/+0xc`) — with a null vehicle, any state-3 character passes.
HYPOTHESIS: 3 = seated, 1/2 = the two phases of getting in; the spec already reads `0x009b9160` as "seated" (§18.31
correction).

---

## A. Vehicle cluster — 12 functions

### A.0 Shared machinery for the four "enter" entries

**The entry request record and its submitter `0x00af22a0`.** `vehicle_enter_do`, `vehicle_enter_guardian_angel` and
`vehicle_enter_group_do` all build a **0x44-byte request record** on the stack and hand it to `0x00af22a0(character,
vehicle, record)`. Record fields as written by those callers (offsets hand-computed from the listings, CONFIRMED —
disassembly): `+0x00` seat index (`-1` = any), `+0x04` `-1`, `+0x08` `2`, `+0x0c` 1 if the seat index is `-1` else 0
(group-do writes 0), `+0x10`..`+0x1c` `-1`, `+0x20`..`+0x23` a flag dword, `+0x24` a flag byte, `+0x28`..`+0x30` `-1`
(`+0x30` replaced by a seat-name id in one path), `+0x34`, `+0x3c` 0.

`0x00af22a0` itself (CONFIRMED — disassembly, follow-up of the guardian-angel dump):
1. If the character's `+0x1c9c` bit 3 is set, it forces record `+0x20` bit 0.
2. If record `+0x20` bit 0 is set ("instant" — see below), it cancels the character's special-movement state when
   `0x009a1dc0` reports one (`0x009a1e10(character, 0)`).
3. Eligibility: `0x00b0ca60(character, instant-or-record-`+0x23`-bit-3, record `+0x24` bit 0)` must pass. That
   predicate (follow-up dump) refuses a character that is seated (`+0x16d4`==3) or entering (1/2), whose `+0x16d8/+0x16dc`
   pair is non-zero, who is in a special-movement state (`0x009a1dc0`), or for whom `0x0096f540` is true; when the second
   argument is 0 it further refuses on `0x009562d0`, `0x00985d80(…,4,0)`, `0x00985d80(…,6,0)` and an `0x004d7e00`/
   `0x004de2d0` check (those bodies not dumped — OPEN).
4. Vehicle: a null vehicle is replaced by `0x00b105c0(character, record)` (not dumped; HYPOTHESIS: "pick a vehicle");
   a given vehicle must pass `0x00b0fdf0(character, vehicle)`. A vehicle with `+0x1d7b` bit `0x40` is refused unless
   record `+0x20` bit 2 is set.
5. A per-vehicle occupancy controller is fetched (`0x00b08400` on the vehicle's handle pair) or created
   (`0x00b083d0`/`0x00b08860`/`0x00b08560`). The character gets a slot from `0x00b085d0`: the first empty one of **8**
   slots of 0x100 bytes (CONFIRMED, follow-up dump: loop bound 8, returns -1 when full); `-1` → refusal. Note
   `0x00b085d0` does not look for an existing slot of the same character — it always claims a new empty one
   (HYPOTHESIS: a repeated order for the same character can hold two slots until the controller clears one; the
   clearing code was not dumped).
6. Record `+0x20` bit 0 is also forced on when the vehicle class's `+0xbf4`→`+0xa7c` is 0 or the controller's
   `+0xea4` is 0. If record `+0x22` bit 3 is set and `0x00ab5070(vehicle, record +0x18)` finds an occupant, record
   `+0x08` becomes 1.
7. The 0x44 bytes are copied to controller `+0x6fc + slot*0x100`, and `0x00afe570(controller, slot, 0, 1)` starts it;
   returns `true`. Every refusal returns `false`.

HIGH CONFIDENCE: record `+0x20` bit 0 is "instant / teleport into the seat" (it is what the teleport-style callers
set, and the submitter forces it exactly when no walk-up path exists). The `+0x6a8 + slot*0x100` dword of the same
controller is the slot's state that `vehicle_exit_guardian_angel` (A.12) tests.

**"Independent follower" side effect.** `vehicle_enter_do` and `vehicle_enter_guardian_angel` call `0x009d5fd0(1)` **on
the entering character itself** (ECX = the character, CONFIRMED — listing at `0x00a671b4`/`0x00a671b6`) when the
character's class has descriptor `+10` bit `0x04` and `0x0097e7b0(character, 0)` finds a leader that lacks descriptor
bit `0x21`. `0x009d5fd0` is `follower_make_independent`'s own delegate (§7.2: variant-1 replicated setter, tags `"npc"`
+ `"npc_force_flagsindependent_follower"`, bit 0 of `+0x1e01`). `0x0097e7b0` (dumped) returns the object behind
`0x005083f0(character)`'s first handle pair unless that is the character itself, and returns null at once if the
character already has `+0x1e01` bit 0. So: ordering a follower into a vehicle silently makes it an independent
follower, and that flag is never cleared by these entries. CONFIRMED — disassembly.

### A.1 `vehicle_hidden` (`0x00a62610`) — 2 calls / 2 scripts

**Arguments:** 1 mandatory string (vehicle reference), via `0x00a281e0`.

**Return:** exactly 1 boolean.

**Body:** resolved → pushes bit 0 of the object's byte `+0x3b` (`0x008cc4d0`). **Unresolved → pushes `true`.**
CONFIRMED — disassembly. So a bad name, a destroyed vehicle or a character name reads as "hidden".

**Side effects:** none. HIGH CONFIDENCE that `+0x3b` bit 0 is the same hidden flag `vehicle_show` (§12.18) clears:
both go through `0x00a87780`, whose body (dumped here, A.7) compares `0x008cc4d0` against its flag argument and then
writes it via `0x008ccb90`.

### A.2 `vehicle_anim_playing` (`0x00a62050`) — 2 calls / 2 scripts

**Arguments:** 1 mandatory string, via `0x00a281e0`.

**Return:** exactly 1 boolean; unresolved → `false`.

**Body:** `0x00a75fe0(vehicle)`: false unless the vehicle's byte `+0xbd0` is 1 and `+0xa78` holds an animation-set
object. It then scans that object's entries (count at `+0x48`, array base at `+0x4c`, entry stride 0x84 bytes, flag
dword at entry `+0x80`) and answers true if any entry has flag bit 0 set and bits 1-2 clear. CONFIRMED — disassembly.

**Side effects:** none. HYPOTHESIS: entry bit 0 = active, bits 1-2 = finishing/paused. `+0xbd0 == 1` recurs across
this tranche (A.7, E.1-E.4) as the gate for "full vehicle with part tables" — meaning OPEN.

### A.3 `vehicle_engine_check_running` (`0x00a63e50`) — 2 calls / 1 script — **defect: never returns `true`**

**Arguments:** 1 mandatory string, via `0x00a281e0`.

**Return:** **0 or 1 Lua values.** Failure (unresolved, or object byte `+0x33` bit `0x08` clear) → pushes `false`,
returns 1. **Success → calls `0x00aa92a0(vehicle)` (the engine-running bit, `+0x16c0` bit 0), discards the result,
pushes nothing and returns 0.** CONFIRMED — listing `0x00a63e79`-`0x00a63e85`: `CALL 0x00aa92a0`, `ADD ESP,4`,
`XOR EAX,EAX`, `RET`.

**Consequence:** a script's `vehicle_engine_check_running(v)` evaluates to `nil` for every valid vehicle, running or
not, and to `false` otherwise — it can never be truthy. A reimplementation that wants byte-for-byte behaviour must
return no value on the success path; one that wants the evident intent returns the bit. The two call sites' script
logic was not checked against this (OPEN).

**Side effects:** none. OPEN: what `+0x33` bit `0x08` is (it is also tested by `0x00a87780` before its "unhide"
side-call `0x00a85180`).

### A.4 `vehicle_engine_start` (`0x00a62380`) — 2 calls / 1 script

**Arguments:** arg 1 mandatory string (vehicle, via `0x00a281e0`); arg 2 optional boolean, standard nil-gated idiom,
default `true`. Note arg 1 is read before arg 2 is checked, and the resolve happens after both reads.

**Return:** none (0).

**Body:** on a resolved vehicle: if it is already running (`0x00aa92a0`) it is **stopped first** — `0x00aa9260(vehicle,
!arg2)`: clears `+0x16c0` bit 0, sets `+0x15c8` to 6, runs `0x00556f20` (six shutdown calls, bodies not dumped) and
`0x00aefae0(vehicle, !arg2)`. Then it is started — `0x00aa91c0(vehicle, 1, !arg2)`: because the running bit was just
cleared, this always takes its first branch, `0x00aefa50(vehicle, !arg2)`, then `0x00557ea0(vehicle, 1)`, then sets
`+0x16c0` bit 0. CONFIRMED — disassembly (both helpers dumped).

`0x00aefa50` / `0x00aefae0` (follow-up dumps) write an engine-state byte chosen by `0x00ad2f80(vehicle)`: class 2 →
`+0x328`, classes 3/4 → `+0x5b8`, any other class → nothing at all. Start sets bit 0 and clears bit 1; stop sets bit 1
and clears bit 0; both put the (inverted) Lua flag in bit 2. On a mode-4 (VTOL, `0x00ad3200`) vehicle with a non-zero
`+0x159c` they also fire `0x0045f5b0(0xb24d8b1d, 0xae932353 | 0x15c3b8b5, +0x159c)` and `0x00556750`. CONFIRMED.

**Side effects:** an already-running engine is restarted, not left alone. `0x00557ea0` re-reads the vehicle's class
`+0xbf4`→`+0x4d0` value into `0x00559410`, notifies the seat-0 occupant when it is a human-class object (`0x0055afa0`),
and, when `0x00ab54d0(vehicle, local player)` holds, calls `0x0045e980(+0x159c, 0)`. HYPOTHESIS: arg 2 `false` = "start
instantly, skip the spin-up" (bit 2); classes 2/3/4 = car / helicopter / VTOL — but `0x00ad2f80` was not dumped, so the
class meaning is OPEN, and a class outside 2-4 gets no engine-state write at all.

### A.5 `vehicle_disable_weapon_physics` (`0x00a62330`) — 2 calls / 1 script

**Arguments:** 1 optional boolean, standard nil-gated idiom, default `true`.

**Return:** none.

**Body:** writes `NOT arg` into the global byte `0x0130efe4` (`0x00b72c60`, file-initialised to 1). CONFIRMED.

**Side effects:** **calling it with no argument disables.** The only readers are `0x00b73f60` and `0x00b75390`
(the global has exactly 3 uses). `0x00b73f60` (follow-up dump) is a per-hit routine: when the hit record has flag bits
0 and 1, this global is set, and the vehicle's `+0xbd0` is 1, it takes up to two part indices from `0x00b72f30`, flags
each part (`+0xf0 |= 0x100000`), runs `0x00a8c870`/`0x00a896a0`/`0x00b73a60` on it and kicks it with `0x00c36160(…,
1.0)`. HIGH CONFIDENCE: the flag gates "weapon hits knock vehicle parts loose". **No host gate, no replication, and no
reset anywhere** (only one writer) — once a script disables it, it stays disabled for the rest of the process, across
missions and loads. CONFIRMED for the writer count; OPEN whether that is intended.

### A.6 `vehicle_forced_corpse_removal_enabled` (`0x00a624d0`) — 2 calls / 1 script

**Arguments:** 1 bare boolean (absent = `false`).

**Return:** none.

**Body:** `0x00ac2d10(arg)`: writes the global byte `0x0130dfe0` **only when a session exists and this machine is the
host**; otherwise the call does nothing. CONFIRMED — disassembly. (If `0x0087ba20` can return null in single player,
the call is a no-op there — OPEN.)

**Side effects / where the flag goes** (follow-up dumps, CONFIRMED): the world-init routine `0x00ac42c0` resets the
flag to 1 and registers it under the name `"vehicle_forced_corpse_removal"` through `0x0086d770(name, &flag, 1,
is-host, 0)` (HYPOTHESIS: a replicated/console game variable, which would explain the host-only write). Its consumer
is the tick routine `0x00ac3610` (called from `0x00ac54b0`): with the flag on, it walks the circular list headed at
`0x027c2eb4` (link `+0xbec`, stops on wrap-around or null), keeps every entry whose `+0x16c8` bit `0x40` is clear, that
is alive and locally authoritative (`0x008addb0`), scores each with a 4-float value (`0x00a85ec0`/`0x00da38e0`/
`0x009dfa90`) and a visibility-like byte (`0x0091f310(2)`), picks the best one — preferring entries whose byte is 0 —
and removes it with `0x00a846f0(vehicle, 1000, 1, 0)`. One removal per tick.

**Crash-shaped (engine side, enabled by this flag):** `0x00ac3610` collects candidates into fixed stack arrays of
**48** entries (a 48-dword object array, a 48-byte flag array and a ~48×4-float score array, sizes from the frame
layout) and the collection loop has **no count check** — the only exits are list wrap-around and null (listing:
`INC EDI` at `0x00ac36d6`, loop test only against the head at `0x00ac36e0`). More than 48 qualifying list entries
would overrun the stack frame. CONFIRMED for the missing bound; OPEN whether that list can reach 49 (pool size not
read).

HIGH CONFIDENCE that `0x027c2eb4` is the wrecked-vehicle ("corpse") list: it is the list this flag's consumer and
`vehicle_delete_all_corpses` (A.7) both walk, and both names say "corpse". It is one of four list heads the world-init
routine clears together (`0x027c2eac`, `0x027c2eb0`, `0x027c2eb4`, `0x027c2eb8`).

### A.7 `vehicle_delete_all_corpses` (`0x00a63d30`) — 2 calls / 1 script

**Arguments:** none (prologue only).

**Return:** none.

**Body:** walks the same `0x027c2eb4` list (next pointer cached before the current entry is processed; stops when the
next pointer equals the head). For each entry that is locally authoritative (`0x008addb0(entry, 0)`):
- seat count `0x00ab4af0` (`+0x824` when `+0xbd0` is 1, else a fixed 8);
- **if `0x00a367f0(entry)` is false:** every occupant found by `0x00ab5070(entry, i)` (handle pair at `+0x1358 +
  i*0x30`) is released with `0x00853ea0(occupant, 0, 0)`, then the vehicle itself is released the same way;
- **if it is true:** the vehicle is kept but hidden — each occupant and the object `0x00941f40` returns for it get
  `0x00920170(…, 0)` (sets bit 7 of `+0x3a`, recursing into attached children of descriptor bit `+6`/`0x08`); the
  vehicle's attached effect handles are released (`0x00aa2ab0`, `0x00aa2be0`, `0x00aa3750`, each with 0 = "release" via
  `0x005cfe00`); and `0x00a87780(vehicle handle pair, 1, 0)` hides it (the same helper `vehicle_show` §12.18 calls with
  0, recursing into an attached vehicle from `0x00ad2e30`).
CONFIRMED — disassembly.

`0x00a367f0` → `0x00a36750` (follow-up dump) answers by class: human-class (`+10` bit `0x04`) → `+0x1da8 != 0`;
vehicle-class (`+6` bit `0x80`) → `+0xbf8 != 0`; two further classes → `+0xf0`/`+0x130 != 0`. The same predicate is
§10.1's "own vehicle test" of the store exit. HYPOTHESIS: `+0xbf8` non-zero = "script/mission-owned or otherwise
protected", so protected wrecks are hidden rather than deleted.

**Side effects:** deletes/hides wrecks and their occupants; no replication of its own (only locally-authoritative
entries are touched). `0x00853ea0` (dumped) is a generic object release that skips remote-owned objects with a non-zero
`+0x3c` when the global `0x024d4461` is 1. Note §12.18 describes `0x00a87780`'s first two arguments as the vehicle's
"position pair"; the dump here shows `0x00a87780` resolves them through the handle lookup `0x00458230`, so they are the
**handle pair** (suggested correction for §12.18).

### A.8 `vehicle_enter_do` (`0x00a67000`) — 2 calls / 1 script

**Arguments:** arg 1 string (character), arg 2 string (vehicle), arg 3 bare boolean ("instant"), then four optional
arguments each with the standard nil-gated idiom: arg 4 number (seat index, default `-1`), arg 5 boolean (default
`false`), arg 6 string (seat name, default none), arg 7 boolean (default `false`). CONFIRMED — disassembly.

**Return:** exactly 1 boolean — the result of the order call; `false` if the character or the vehicle does not
resolve.

**Body:**
1. Vehicle: `0x00a281e0(arg 2)`; if that fails, arg 2 is tried as a character (`0x00a281a0`) and **that character's
   current vehicle** (`0x004dcf00` on `+0x16c0/+0x16c4`, live only) is used. Character: `0x00a281a0(arg 1)`.
2. Leader/independent-follower side effect (A.0).
3. Record (A.0): seat index from arg 4; arg 5 → `+0x22` bit 4; arg 7 → `+0x23` bit 4.
4. **arg 3 true ("instant"):** cancel the special-movement state if `0x009a1dc0` (character `+0xcd2` not 0 and not 6)
   via `0x009a5520(character, 0, 0)` and `0x009a1e10(character, 0)`; if the character's class lacks descriptor `+10` bit
   `0x02`, `0x004e4130(character, 0)` (clears its AI command byte `+0x504` when `0x004de110` allows); record `+0x20`:
   bits 0-2 set, bit 4 cleared; `+0x22 |= 0xe0`; `+0x24` bit 0; submit through `0x00af22a0`.
5. **arg 3 false, arg 6 given:** `+0x30` = `0x004bf810(arg 6)` (seat-name lookup: case-insensitive scan of a runtime
   table at `0x03171c10`, count `0x03171c08`, 16-byte entries, -1 if absent — follow-up dump), `+0x20` bit 1; submit
   through `0x00af22a0`.
6. **arg 3 false, no arg 6:** the AI order path `0x00521200(character, vehicle handle pair, record, 1)` (follow-up
   dump): refuses a seated or entering character, needs `0x004dcf00` to find the vehicle and `0x00ad5180` to be false;
   because the caller passes 1 the `0x00b0fdf0` path check is skipped; it then copies a 0x58-byte default order template
   (`0x012e14e0`) to character `+0x4a8`, stores the vehicle handle at `+0x4b0/+0x4b4`, the seat index at `+0x4e4`, two
   flag bits at `+0x4fd`/`+0x4fe` and the record's `+0x20` dword at `+0x4f8`, and calls `0x004f4d00(character, 0x31, 0)`.
   On success, **if the character's class has descriptor `+10` bit `0x02`**, `0x009e3200(2, 0)` is called on it — a
   variant-2 replicated setter (follow-up dump: tags `"player"` + `"script_mode_params"`) that stores mode 2 at
   `+0x2888` and sets `+0x28ac` bit 1.
CONFIRMED — disassembly.

**Side effects:** issues an AI enter order or an instant seat placement; may flip the independent-follower flag; for
a player-class character on the order path, switches its script mode to 2. HYPOTHESIS: descriptor `+10` bit `0x02` =
player class, bit `0x04` = NPC/human class; `0x004f4d00(…, 0x31, …)` starts AI action 0x31.

### A.9 `vehicle_enter_guardian_angel` (`0x00a67670`) — 2 calls / 1 script

**Arguments:** arg 1 string (character), arg 2 string (vehicle, same character-fallback as A.8), arg 3 optional
number (seat index, standard nil-gated idiom, default `-1`).

**Return:** exactly 1 boolean (submitter result; `false` if either side fails to resolve).

**Body:** same leader/independent-follower side effect as A.8; record with seat index from arg 3, `+0x20` dword
**`0x08e00006`** (bits 1-2 of `+0x20`, `0xe0` in `+0x22`, bit 3 of `+0x23`), `+0x24` = 1; submitted through
`0x00af22a0`. CONFIRMED — disassembly.

Compared with A.8's instant path, record `+0x20` bit 0 is **clear** (a walk-up entry), but `+0x23` bit 3 makes the
submitter pass "forced" to the eligibility check, skipping its `0x009562d0`/`0x00985d80`/`0x004de2d0` refusals.
HYPOTHESIS: "guardian angel" = the escort/backup family of §6.6/§6.8/§12.17, and this is the relaxed-eligibility entry
used for it.

### A.10 `vehicle_enter_group_do` (`0x00a67330`) — 2 calls / 1 script

**Arguments:** arg 1 string (vehicle, same character-fallback as A.8), arg 2 bare boolean ("instant"), arg 3 a table
of character names (counted by the §-documented table-walk helper `0x0083dff0`: the table's own `n` field if numeric,
else a `lua_next` count; -1 if arg 3 is absent or nil).

**Return:** exactly 1 boolean (see below).

**Body:** for i = 1..count: `t[i]` read with push/gettable, resolved via `0x00a281a0`; skipped unless alive
(`0x00853b10`) and not dead (`0x0096f4f0`). Each kept member whose class has descriptor `+10` bit `0x04` gets
`0x009d5fd0(1)` **unconditionally** (ECX = the member, listing `0x00a67461`-`0x00a67465`; no leader test, unlike A.8).
- **instant:** cancel special movement (as A.8); a player-class member (descriptor `+10` bit `0x02`) is first moved by
  `0x00996b50` to the vehicle position (`+0x40..+0x48`) **plus 5 × the vector at `0x013213e4`** with orientation
  `0x01321400`; then a record with **seat index = i − 1** (the member's table position, CONFIRMED — listing `DEC ECX`
  at `0x00a67588`), `+0x0c` = 0, `+0x20` = 7, `+0x22` bit 6, `+0x24` = 1, submitted through `0x00af22a0`. A refusal sets
  a failure flag but the loop continues with the remaining members.
- **not instant:** `0x004e33b0(0)` on the member's AI sub-object at `+0x2b0`; then the AI order `0x00521700(member,
  vehicle handle pair, 1, -1, 1, 0, 0)`; if `character_is_ready`'s predicate `0x00a3bae0(member, 0)` (§7.27) was true,
  `0x004e33b0(1)` afterwards. `0x004e33b0` (follow-up dump) is a variant-1 replicated setter of bit 1 of `+0x2b0`'s byte
  `+0xd`, tags `"human_ai_data"` + `"ai_force_flagsvehicle_enter_group_started"`. `0x00521700` (follow-up dump) forwards
  the order to the owner as an opcode-`0x41` record (sub-code `0x1b`, byte `0x26`) when the member is not locally
  authoritative, else builds a record (seat -1, flag bit 3 from its 4th argument) and calls `0x00521200` (A.8 step 6).
  The order result is not checked; every member reaching this branch counts as success.

**Return value:** `false` if the vehicle did not resolve, or if any instant placement was refused; otherwise `true`
exactly when at least one member reached its order/placement. CONFIRMED — disassembly.

**Side effects:** per-member AI orders or seat placements, replicated flags. HIGH CONFIDENCE: the instant path assigns
seats strictly by table position (member 1 → seat 0 = driver), so a table that does not start with the intended driver
puts the wrong member in the driver seat. HYPOTHESIS: the 5-unit lift (if `0x013213e4` is the up axis) places player
members above the vehicle before the seat snap; the vector's value was not read (file-backed, first dword 0).

### A.11 `vehicle_enter_group_check_done` (`0x00a66920`) — 2 calls / 1 script

**Arguments:** arg 1 string (vehicle, `0x00a281e0` only — **no character fallback**, unlike A.8-A.10), arg 2 bare
boolean ("do not re-order"), arg 3 table of character names (`0x0083dff0`).

**Return:** exactly 1 boolean. **Unresolved vehicle → `true`** ("done"). CONFIRMED — listing `0x00a66962` (pushes 1).

**Body:** "done" starts true. For each member (resolved, alive, not dead):
- if the class flag at object `+0x2bd` bit 1 is set, the member is only considered when it is seated (state 3) or in
  entry phase 1 (`0x009b9210`) or its byte `+0x501` is `0x26`; otherwise it is skipped entirely;
- if `0x009b9290(member, vehicle)` (seated in *this* vehicle) → fine, next member;
- otherwise "done" becomes **false**, and — unless `+0x501` is `0x26`, or the member is entering (`0x009b91e0`), or arg 2
  is true — the order is **re-issued**: `0x00521700(member, vehicle, 1, -1, 1, 0, 0)` (preceded by nothing; followed by
  `0x004e33b0(1)` on `+0x2b0` only when `+0x2bd` bit 1 is clear and `0x00a3bae0` is true — the decompile order is
  "test, order, set flag").
CONFIRMED — disassembly.

**Side effects:** a "check" that also re-issues enter orders to stragglers each time it is polled, unless arg 2 is
true. HIGH CONFIDENCE that `+0x501 == 0x26` marks a member already running the enter action (action index 0x26 also
appears as the byte `0x00521700` sends, and §7.27 already saw `&` = 0x26 at `+0x500/+0x501`). HYPOTHESIS: `+0x2bd` bit 1
= "player-controlled". **Edge case:** with fewer than 3 Lua arguments the table helper returns -1 and the function
answers `true` without looking at anyone; with exactly one argument the boolean is read at stack index 0 (no
presence check) — CONFIRMED that no check exists, effect of index 0 in this Lua build OPEN.

### A.12 `vehicle_exit_guardian_angel` (`0x00a66c70`) — 2 calls / 1 script

**Arguments:** 1 mandatory string (character), via `0x00a281a0(name, 0)`.

**Return:** exactly 1 boolean: `true` iff the character resolved and was seated (`+0x16d4` == 3). CONFIRMED — listing
(`MOV EAX,1` before the cookie check at `0x00a66d20`).

**Body:** builds an exit record with `0x004dccd0` (initialiser: `+0x00`/`+0x20`/`+0x24`/`+0x2c` = -1, `+0x08`..`+0x1c` 0,
word `+0x30` 0, byte `+0x32` bits 0/2 cleared and bit 1 set), then sets `+0x28` = -1 and `+0x31` bit 4; calls
`0x00af3cd0(character, record)`; then `0x004f4ae0(character, 0x25 / 0x26 / 0x27, 3000)`.

`0x00af3cd0` (dumped): needs the character's vehicle pair to be non-zero and `0x004dcf00(pair, 1)` to find the vehicle
(dead allowed); calls `0x009496c0(0)`; sets record `+0x30` bit 0 when the vehicle class `+0xbf4`→`+0xa7c` is 0 or the
character's `+0x1c9c` bit 3 is set; requires `0x00b0b170(character, vehicle, that bit)`; then finds the occupancy
controller and the character's slot (`0x00b08400`/`0x00b08390`), and only if the slot exists and its state dword
(`+0x6a8 + slot*0x100`) is neither 1 nor 2 copies the record (`0x00af2250`) and starts slot action `0x14`
(`0x00b058f0(controller, slot, 0x14, 1, 0)`). Its result is not used.

`0x004f4ae0` is the per-action delay setter that §7.30 `ai_set_action_delay` uses (body now dumped): for an action index
below 0x46 (70), either applies the delay locally (`0x00d9e140`, a wrap-around millisecond timer set relative to
`0x01320d94`) when `0x008addb0` says so, or forwards it to the owner (`0x004f4830`: opcode `0x41`, sub-code `0x1a`,
mode 3). So the exit also puts a **3-second cooldown on AI actions 37, 38 and 39** — names in the 70-entry table at
`0x012e1c60`, not read (OPEN).

**Side effects:** asks the seated character to get out (unless its seat slot is in state 1/2) and blocks three AI
actions for 3 s. The `true` result means "was seated", not "exit started" — the exit can be silently refused.
CONFIRMED — disassembly.

---

## B. Team and tutorial — 3 functions

### B.1 `team_make_hostile` (`0x00a607a0`) — 2 calls / 1 script — **crash-shaped and out-of-bounds**

**Arguments:** 2 mandatory strings (team names), read with no checks.

**Return:** none (0 — listing `XOR EAX,EAX` at `0x00a60910`).

**Body:** each name goes through the shared team/gang-name → id helper `0x0094cc60` (§21.16/§5.2: hash `0x00dab330` mod
32, table `0x02623a80`, id byte from `0x02623b48`, **9 when not found**). Then `0x009b7130(a, b, 0, 1)` writes **0**
into the byte relationship matrix at `0x0262d940`, row stride 8: `[a*8+b] = 0`, and because the last argument is 1, also
`[b*8+a] = 0`. When the second team of a pair is 5 it also writes `[row*8+6]`; when the first is 5 it also writes
`0x0262d970[col]` (= row 6) — team 5's relations are mirrored into team 6. Then it **always** broadcasts an
opcode-`0x43` record (8-bit sub-code `0x30`, then the bytes a, b, 0) to the session via `0x0086f1b0(session, 0, 0)` —
no host gate and no "already replicating" gate (shape 4, "always both"). CONFIRMED — disassembly.

**Crash-shaped / memory-safety findings:**
1. **Non-string argument → null dereference.** `lua_tolstring` returns null for a non-string; `0x0094cc60` passes it
   straight to `0x00dab330`, which reads the first character with no null check (both bodies dumped). CONFIRMED.
2. **Unknown team name → out-of-bounds / cross-row writes.** The not-found id 9 is larger than the row stride, so
   `[9*8+b]` lands past row 7 and `[b*8+9]` lands in **row b+1, column 1** — an unrelated team pair's relationship is
   silently set to hostile. Nothing bounds-checks either id. CONFIRMED for the arithmetic (decompile of `0x009b7130`);
   the matrix's row count (8 assumed from the stride) is OPEN — its initialiser `0x009b70e0` was not dumped. Peers that
   apply the replicated record presumably repeat the corruption (HYPOTHESIS: receiver not dumped).

**Side effects:** relationship matrix update + network broadcast. HIGH CONFIDENCE: 0 = hostile (from the name; the
sibling `0x00a605b0` also calls `0x009b7130` — HYPOTHESIS: the "friendly/neutral" counterpart with another value).
HYPOTHESIS: team 5 = the player team and 6 its co-op/ally mirror.

### B.2 `tutorial_visible` (`0x00a602e0`) — 2 calls / 1 script — **not a pure query**

**Arguments:** none.

**Return:** exactly 1 boolean = `0x00716300()`.

**Body:** `0x00716300` is true only when `0x00715ad0()` is true **and** the head entry of the queued-prompt list
(`0x0151d588`, §6.19) is in state 3, or in state 2 with its `+0x14` (`tutorial_start`'s argument 2) equal to 0.
`0x00715ad0` (follow-up dump) requires: byte `0x0151d5a5` clear, `0x006cffc0` false, a non-empty queue,
`0x007bfa70`/`0x007b6e80` false, `0x007c3420` zero, `0x00720640` false, `0x0059fa00` true, `0x0059f9f0() == 2`, the local
player's `0x007e2b10` false, `0x00a373a0` false, and `0x0059f9c0()` ≤ 0.7. **Then, if the head's descriptor (`+0x08`)
has `+0x24` bit `0x01` set and `0x006cecb0()` is true, it unlinks the head and re-inserts it at the tail of the
circular list and returns false.** CONFIRMED — disassembly.

**Side effects:** polling `tutorial_visible` can rotate the tutorial queue. This adds a descriptor bit to §6.19's table
(`0x01` = "may be deferred to the back of the queue"; HIGH CONFIDENCE for the mechanism, HYPOTHESIS for the wording)
and confirms state 3 = "showing" in practice (§6.19 left state 3 OPEN). The individual gate predicates are OPEN.

### B.3 `tutorial_get_case` (`0x00a60390`) — 2 calls / 1 script

**Arguments:** 1 mandatory string.

**Return:** exactly 1 value: a **number** when the name is found, `false` when not.

**Body:** `0x00717780` (the 210-name case-insensitive table of §6.19/§10.4) → index; found → pushes the dword at
`0x0151d610 + index*0x24`, i.e. **entry `+0x10`** of the §6.19 runtime table (base `0x0151d600`). `0x00716280` bounds-
checks the index (< 0xd2, else -1). CONFIRMED.

**Crash-shaped:** the scan calls the CRT `__stricmp(table name, arg)` with no null check on the argument; this build's
`__stricmp` (dumped) routes a null pointer to `__invalid_parameter` (errno 22), whose default handler terminates the
process unless the game installed its own. CONFIRMED for the missing check and the CRT path; OPEN whether a custom
invalid-parameter handler is installed. Same exposure in C.2.

**Side effects:** none. This settles one §6.19 OPEN partially: entry `+0x10` is what `tutorial_get_case` reports. Its
writer is still OPEN.

---

## C. Weapon store — 3 functions (UI sub-registrar `0x00817570`)

### C.1 `store_weapon_purchase_ammo` (`0x008165c0`) — 2 calls / 2 scripts — **null-write paths**

**Arguments:** arg 1 mandatory number (slot / category, truncated); arg 2 optional number (item id, default 0); arg 3
optional number (amount, default 0); arg 4 optional number (price, kept as float, default 0.0). Args 2-4 use the
standard nil-gated idiom. Arg 1 is handed to the helper `0x00816430` **in EAX** (listing `0x00816674`), the rest on the
stack. CONFIRMED.

**Return:** none (0).

**Body (`0x00816430`):** needs a local player (`0x009da4e0`), otherwise does nothing.
- **arg 1 ≠ 1000:** the player's inventory (`player + 0x1b78`) slot lookup `0x005ff110` (thunk to `0x005fec20`; slots
  0-10 and 12 via the inventory's handle pairs, 9/0xff → none) → slot record; its `+0x19c` item must be non-null; then
  ammo is added: `0x00b6ee40(item, amount, 0, 0)` on `player + 0x1e48`.
- **arg 1 == 1000:** scans the store list (count `0x022ce35c`, array `0x022ce8b0`) for an entry whose `0x008dc820` code
  (`+0x44`: 1→3, 2→2, 3→1, else 0) equals arg 2. Matched and owned (`0x005fe900`, the inventory "find live weapon that
  accepts this item" scan) → ammo as above; matched and not owned → `0x00601380(entry +0x5c, 0)` on the inventory (gives
  the weapon) and then writes 0 to `[result + 0x1a4]`.
- Then, in every path that got this far: charges `price × 100` (constant `0x012a2dd8` = 100.0, converted by `0x00dad900`)
  as a negative cash adjustment through `0x0094d920(player, &amount, 11)` (variant-2 shape: local, or an opcode-`0x46`
  `"human"`/`"cash_adjust"` record), records the purchase with `0x00a03d40(player, price, item's first dword, 0)`, and
  notifies a script callback (below).
CONFIRMED — disassembly.

**Crash-shaped findings (all CONFIRMED in the listing):**
1. **No match in the 1000 path:** the scan leaves the item pointer at the **last array entry it read** (or 0 if the
   list is empty — `XOR ESI,ESI` at `0x00816446`, `JLE` straight to the charge at `0x0081645b`) and still charges and
   records. Empty list → `MOV ECX,[ESI]` at `0x00816533` reads address 0. Non-empty list without a match → the price is
   charged and the purchase is recorded against an unrelated item, with no ammo given.
2. **Give-weapon failure:** `0x00601380` returns 0 on every failure path (amount argument 0.0, network-forward branch,
   no slot record — the follow-up listing shows each one reaching `XOR EAX,EAX` at `0x006014de`), and its success path
   returns the slot record (`MOV EAX,EBP` at `0x00601861`). The caller writes `MOV dword ptr [EAX+0x1a4],0` at
   `0x008164a1` without a check → a write to address `0x1a4` whenever the weapon could not be given. HIGH CONFIDENCE that
   this is reachable for an unowned weapon whose `+0x5c` field is 0 or on a non-authoritative inventory.
3. **No affordability check** in this function (cash may go negative or be clamped inside `0x0094d920` — OPEN).

**Script callback:** host → `0x00a1f990(handle at 0x026e9538, 0)`; non-host → if that handle is not -1,
`0x00a295a0(2, 0x026e9568, 0x026e956c, 4)`. A non-null result gets two string arguments pushed by `0x00e0cfb0`
(follow-up dump: pushes onto the result's Lua state at `+4` — `lua_pushstring`, or `lua_pushnil` for null — and counts
it at `+0x1c`): `"weapon-ammo"` and `0x00a353d0(local player)` (the name-string helper §12.x already uses for occupant
names). HIGH CONFIDENCE: a registered script function is invoked with (`"weapon-ammo"`, player name); `0x00a1f990`
(dumped) looks the handle up in a 12-byte-stride table at `0x026e83f0` (index = handle >> 16) and prepares the call
with `0x00e0cef0`/`0x00e0ca80`. When and how the call actually runs is OPEN.

### C.2 `store_weapon_change_weapon` (`0x00815f40`) — 2 calls / 2 scripts — **unchecked index**

**Arguments:** arg 1 mandatory string (slot category, case-insensitively matched against the 9 names at `0x01300a78`:
`"unarmed"`, `"melee"`, …); arg 2 mandatory number (item index; -1 = "the player's current weapon in that slot");
arg 3 and arg 4 bare booleans; arg 5 optional number (standard nil-gated idiom; `-1` is treated like absent).

**Return:** none (0) on every path, including "no category matched".

**Body:**
- No category match → returns immediately. (The decompile also shows a "-2" check and a negative-index branch reading
  `0x022ce004`; both are dead code because the loop index is always 0-8 — CONFIRMED in the listing.)
- If no item is currently previewed (`0x022ce904` = 0), `0x01300a9c` is reset to -2.
- **arg 2 == -1:** `0x01300a9c` = -2; inventory slot (category+1) of the local player via the `0x005ff110` thunk on
  `player + 0x1b78` — **the local-player pointer is not null-checked** (listing: `ADD ECX,0x1b78` directly on the
  result at `0x00815fde`); item = slot `+0x19c`; `0x022ce90c` = slot byte `+0x1a1` bit 2. No slot → item 0.
- **otherwise:** `0x022ce90c` = arg 3; item = `0x022ce048[arg2 + category*20]` if arg 4 is true, else
  `0x022ce630[arg2 + category*20]` — **arg 2 is not bounds-checked**, so any script number reads an arbitrary dword
  near those arrays as an item pointer, which is then stored in `0x022ce904` for later use. CONFIRMED.
- End: item null → the swap timer `0x01300aa0` is set to "expired" (`0x00d9e140(-1)`); item non-null and an item was
  already previewed → timer = 500 ms and `0x01300a9c` = arg 5 (or -1/-2 from arg 4 when arg 5 is absent: -2 if arg 4
  true, -1 if false); first preview → timer expired. `0x022ce904` = item.
CONFIRMED — disassembly.

**Side effects:** UI preview state only (globals read by the store's per-frame code — `0x00817610` reads `0x01300a9c`,
`0x00816f00` reads `0x022ce90c`; not dumped). `store_weapon_hide_weapon` (§16.15) resets the same globals. Same
`__stricmp`-with-null exposure as B.3 for a non-string arg 1.

### C.3 `store_weapon_process_post_bg_covered` (`0x00815460`) — 2 calls / 2 scripts

**Arguments:** none. **Return:** none.

**Body:** `0x005dd190(0)`: writes 0 to the global byte `0x012ec141` (file-initialised to 1). CONFIRMED.

**Side effects:** the only reader is the per-frame render-scene setup `0x005e14a0`, which copies the byte into the
renderer state (`[0x0149abb0 + 0x114] + 0x35c0`) next to the fog/atmosphere parameters (`0x012ec140`-`0x012ec1e8`).
HYPOTHESIS: a "draw the world behind the UI" switch — once the store's background covers the screen, world rendering
(or a post effect) is turned off. The other writers of `0x005dd190` (`0x00811060`, and two undefined sites
`0x0080fe07`, `0x0081555f`) were not read, so which call turns it back on is OPEN.

---

## D. Vehicle store — 3 functions (UI sub-registrar `0x008152a0`)

Context: the store mode flag `0x022cdf08` is what `store_vehicle_get_state` (§10.1) reports; the selected vehicle
handle pair is `0x022cde00/04`; the pending "retrieved" vehicle pair is `0x014a1d28/2c`.

### D.1 `store_vehicle_change_mode` (`0x00815120`) — 2 calls / 1 script

**Arguments:** 1 mandatory number (truncated).

**Return:** none.

**Body:** if the number equals the current flag `0x022cdf08`, nothing happens. Otherwise a counter `0x022cdf50` is
incremented and the currently selected vehicle (`0x022cde00/04`, live) is looked up.
- **number == 0:** the pending vehicle is taken over (`0x005f8a00`: moves `0x014a1d28/2c` into `0x014a1dc8/cc`, clears
  the pending pair, fires `0x008ade90(0xd, vehicle)` when the object's `+0x3c` is 0, returns its handle) into
  `0x022cde00/04`; garage state is torn down (`0x005fa950(0)`), the customisation location is set up from
  `0x005faa40()` and `0x022cdf1c` (`0x008183c0`), the previous selection is cleaned up (`0x00820480`: releases nearby
  objects of descriptor `+7` bit `0x04` found by `0x0075d4b0` and records the vehicle at `0x022cf948/4c`),
  `0x00820810()` (applies the `"Standard"` preset via `0x00a9b080`), the record at `0x027b3458` gets {0, 0, handle}
  (`0x00a9adc0`); flag := 0, store sub-state `0x022cdf0c` := 8.
- **number ≠ 0:** customisation teardown `0x008208a0(0)`; then `0x005fa760(0x022cdf1c)` (needs a store location
  `0x005f7d90` and a local player; stores the player's current vehicle into the garage block `0x014a1d00` via
  `0x005f8ab0`; sets store state `0x014a1ce4` = 4). Success → `0x005fb9b0()`, `0x022cdf00/04` := selection, **flag :=
  1 (literal, not the argument)**, selection cleared. Failure → sub-state := 10, flag unchanged.
- Finally, the previously selected vehicle (if it was live) gets `0x00818fb0(vehicle, 0)`.
CONFIRMED — disassembly.

**Edge:** any non-zero argument other than 1 never equals the stored flag (which is only ever 0 or 1 here), so
calling with 2 while already in mode 1 re-runs the whole mode-1 entry. CONFIRMED arithmetic; whether scripts do this
is OPEN.

### D.2 `store_vehicle_retrieve_car_and_exit` (`0x00813020`) — 2 calls / 1 script

**Arguments:** none. **Return:** none.

**Body:** if the flag is 1, the pending vehicle is taken over into the selection (`0x005f8a00`, as D.1; selection
becomes 0 if nothing live is pending). Then on the interface object at `0x012fced8` (279 uses project-wide): pop the top
entry (`0x007b4790(0)`: requires a non-empty stack, decrements the top index `+0x28`, clears `+0x148/+0x16c/+0x170`), and
if an entry with id `0x2e` is currently on the stack (`0x007b3ba0(0x2e)`) pop once more. CONFIRMED — disassembly.

**Side effects:** UI screen-stack pops plus the hand-over of the retrieved car. HYPOTHESIS: `0x012fced8` is the
interface/screen-stack manager and `0x2e` the garage list screen.

### D.3 `store_vehicle_selection_should_lock_controls` (`0x00813170`) — 2 calls / 1 script

**Arguments:** none. **Return:** exactly 1 boolean.

**Body:** flag 0 → `true` iff the sub-state `0x022cdf0c` ≠ 9. Flag non-zero → `true` unless the sub-state is 9 and
`0x005f60e0()` is false; `0x005f60e0` = store location `0x014a1dc4` set and (`0x00d9e4c0` on the timer `0x012ece54` is
true or `0x014a1ce8` ≠ 0). CONFIRMED. Pure query. HYPOTHESIS: sub-state 9 = "selection idle, controls free"; the timer
is a hand-over delay.

---

## E. Vehicle customisation — 4 functions (UI sub-registrar `0x00820aa0`)

Context: target vehicle handle `0x022cf948/4c`; per-zone records `0x022cf850[]`; current zone index `0x022cf91c`;
current paint context `0x022cf914`; current palette `0x022cf910`; wheel-edit target `0x022cf248` (-1 = both).

**Shared hazard:** E.1, E.2 (indirectly) and E.3 derive the vehicle's paint/part object as `vehicle + 0x688` when
`+0xbd0` is 1 and as **0** otherwise, then read `[that + 0x400]` — so a target whose `+0xbd0` is not 1 dereferences
address `0x400`. CONFIRMED arithmetic (listing `SETNZ`/`DEC`/`AND` at `0x0081f683`-`0x0081f687`, same shape in
`0x008199c0`/`0x00819b30`).

### E.1 `vcust_preview_color` (`0x0081f860`) — 2 calls / 1 script

**Arguments:** 1 mandatory number (colour index, truncated). **Return:** none.

**Body:** `0x008199c0(index, 0)`: needs a live target and a paint context; resets the preview (`0x00a999f0(paint object,
context +8, vehicle, 0)`); if no palette is current, picks one (`0x00a94570(0x00819890())`, which falls back to the
first palette rather than null — dumped); then applies `palette +0x10 [index]` to the current zone through `0x00a99b70`.
CONFIRMED.

**Crash-shaped:** the colour index is **not bounds-checked** (arbitrary dword read used as a colour), and this path
has no `+0xbd0` guard (shared hazard above). CONFIRMED. The second caller of `0x008199c0` (undefined site `0x0081f8bb`)
uses mode 1, which restores every zone from the saved arrays `0x022cf7d0`/`0x022cf850` (HYPOTHESIS: `vcust_revert_color`).

### E.2 `vcust_preview_palette` (`0x0081f8d0`) — 2 calls / 1 script — **unguarded dereference**

**Arguments:** arg 1 zone index, arg 2 palette index, arg 3 colour index — all mandatory numbers, truncated.

**Return:** none.

**Body:** arg 1 goes in EAX to `0x00819890` (listing `0x0081f8ee`-`0x0081f8f1`), which returns the zone's material on the
target vehicle (null unless the target is live, has `+0xbd0` = 1 and the zone record exists). **Before that result is
checked**, the function reads `zone record = 0x022cf850[arg 1]`, `palette list = record +0xc`, `palette = list[arg 2]`
(listing `0x0081f908`-`0x0081f912`). Only then: material null → return. Otherwise the colour is `palette +0x10 [arg 3]`,
or, when the "keep closest colour" byte `0x022cf90a` is set, the closest match to the current colour `0x022cf918` found
by `0x008188b0` (RGB squared distance over the `"Base_Paint_Color"`/`"Glass_Color"`/`"Diffuse_Color"` channels);
`0x022cf910` := palette; `0x00819b30(colour, 0)` applies it. CONFIRMED.

**Crash-shaped:** none of the three indices is bounds-checked, and a zone index whose record is null crashes at
`record + 0xc` even when the vehicle is gone. **Inconsistency:** the palette is chosen from zone arg 1, but
`0x00819b30` paints the **current** zone `0x022cf91c`, not arg 1. CONFIRMED.

### E.3 `vcust_preview_wheel_sizing` (`0x0081f5d0`) — 2 calls / 1 script — **null dereference with no target**

**Arguments:** 2 mandatory numbers, each truncated to an int and then to its **low byte** (256 wraps to 0).

**Return:** none.

**Body:** the target vehicle is looked up; **if it is not live the pointer becomes 0 and the next instruction reads
`[0 + 0xbd0]`** (listing `XOR ESI,ESI` at `0x0081f672`, `CMP byte ptr [ESI+0xbd0],1` at `0x0081f676`) — an access
violation whenever the function is called without a live customisation target. Otherwise: value 1, if it differs from
`0x022cf7c4` and lies in [`0x022cf7c8`, `0x022cf7c9`], becomes current and is written into the part object's bytes
`+0x40` and `+0x41` (both when `0x022cf248` is -1, else `+0x40 + 0x022cf248`); value 2 likewise with `0x022cf7c5`,
range [`0x022cf7c6`, `0x022cf7c7`], bytes `+0x42`/`+0x43`. If either changed: dirty flag `0x022cf24c` := 1, the wheel
parts are rebuilt (`0x00ad8090`) and the suspension refreshed (`0x00abf070`). CONFIRMED.

**Crash-shaped:** the null-target read above, plus the shared `+0xbd0` hazard. `0x022cf248` is used as a byte offset
without a range check (HIGH CONFIDENCE it is only ever -1, 0 or 1; writers not read). HYPOTHESIS: the two values are
tyre/rim size, bytes `+0x40/+0x41` front/rear.

### E.4 `vcust_purchase_wheels` (`0x0081fe70`) — 2 calls / 1 script

**Arguments:** 1 mandatory number (price, truncated).

**Return:** none.

**Body:** price > 0 → the amount (`0x009601d0`: × 100, clamped to ±2,000,000,000) is charged as a negative cash
adjustment (`0x0094d920(local player, &amount, 11)` — **no null check on the local player and no affordability
check** in this function), stat `0x47` is credited with price/100 (`0x00710cb0`), `0x006b2050(0x13)` runs, and the
script callback found by `0x005e44c0({4})` on `0x026e9528` (host: `0x00a1f990`; client: `0x00a295a0(2, …)` — dumped) gets
`"vehicle-cust"` and the player name (same mechanism as C.1); then `0x00bdb850(target handle pair)`. Price ≤ 0 skips
all of that. **Either way**, the preview is committed for wheel slot 0 and/or 1 (per `0x022cf248`): committed objects
`0x022cf76c[]`/`0x022cf764[]`/`0x022cf774[]` := preview `0x022cf7a0[]`/`0x022cf798[]`/`0x022cf7a8[]`, each committed
object's `+0x38` bit 0 set, `0x022cf77c[]` := `0x022cf7c0`, byte sizes `0x022cf784[]`/`0x022cf786[]` := current sizes
`0x022cf7c4`/`0x022cf7c5`; dirty flag cleared; `0x0081f070()`. CONFIRMED — disassembly.

**Crash-shaped:** the first two committed pointers are dereferenced (`OR byte ptr [ECX+0x38],DL` at `0x0081ff8a` and
`0x0081ff94`) **without a null check**, while the third one is checked (`TEST ECX,ECX` at `0x0081ff9e`). A purchase
before any wheel preview was set up writes to address `0x38`. CONFIRMED for the missing check; OPEN whether the UI flow
can reach it with null previews.

---

## F. Cross-function observations

1. **Three new UI sub-registrars.** `store_*` and `vcust_*` are not gameplay names: they sit in `0x008152a0`,
   `0x00817570` and `0x00820aa0`, each called from the UI registrar `0x008430f0`. Any spec table that lists registrar by
   name should put these 10 names under the UI side. CONFIRMED.
2. **Return-count defect, new shape.** A.3 returns *zero* values on success (tranche 05 found the opposite shape — an
   extra value). Both come from a success branch that forgets to push or forgets to return. A.3 is the more serious:
   the query cannot answer `true`.
3. **"Unresolved" defaults differ and are sometimes surprising:** `vehicle_hidden` → `true`;
   `vehicle_enter_group_check_done` → `true` ("done"); the enter functions → `false`; `vehicle_anim_playing` → `false`.
4. **Vehicle argument fallback is inconsistent.** A.8-A.10 accept a character name for the vehicle (its current
   vehicle); A.11 does not. A script that passes a driver's name to both will see `do` work and `check_done` answer
   `true` at once.
5. **Hidden side effects of "query" names:** `tutorial_visible` can rotate the tutorial queue; `vehicle_enter_group_
   check_done` re-issues enter orders; `vehicle_enter_do`/`_guardian_angel`/`_group_do` mark followers independent.
6. **Global switches without reset:** `vehicle_disable_weapon_physics` (process lifetime, no other writer) vs.
   `vehicle_forced_corpse_removal_enabled` (host-only, reset to on at every world init).
7. **Purchase notification mechanism** shared by C.1 and E.4: a registered script handle (`0x026e9538`; `0x026e9528`
   indexed by 4), host-direct lookup `0x00a1f990` vs. client `0x00a295a0(2, …)`, two string arguments pushed through
   `0x00e0cfb0`. The spec's other purchase functions (`vcust_purchase_component`, `_paint`, `_performance_upgrade`,
   `store_weapon_purchase_weapon`) likely share it (HYPOTHESIS).
8. **Spec back-fills from this tranche's dumps:** `0x004f4ae0` body (§7.30 OPEN): per-action delay, 70 actions,
   local timer or opcode-`0x41`/sub-code `0x1a` forward. `0x009a1dc0` body (§7.27 OPEN): character byte `+0xcd2` not 0 and
   not 6. `0x00a367f0`/`0x00a36750` body (§10.1's "own vehicle test"): per-class non-zero test, vehicle `+0xbf8`.
   §6.19 entry `+0x10` is `tutorial_get_case`'s value; descriptor `+0x24` bit `0x01` is the queue-defer bit. §12.18's
   "position pair" for `0x00a87780` is a handle pair.
9. **AI action ids in object bytes.** `0x26` (38) appears as an object byte (`+0x501`, A.11/§7.27), as the byte in
   `0x00521700`'s forwarded order, and as one of the three actions A.12 delays — HIGH CONFIDENCE it is one id space (the
   70-entry action table at `0x012e1c60`); the names are OPEN.

## G. Crash-shaped / memory-safety findings — summary

| # | function | finding | status |
|---|---|---|---|
| 1 | `vcust_preview_wheel_sizing` | no live target → read of address `0xbd0` | CONFIRMED (listing) |
| 2 | `store_weapon_purchase_ammo` | slot 1000, empty store list → read of address 0; no match → wrong item charged | CONFIRMED (listing) |
| 3 | `store_weapon_purchase_ammo` | give-weapon failure → write to address `0x1a4` | CONFIRMED (both sides' listings) |
| 4 | `team_make_hostile` | non-string name → null read in the hash | CONFIRMED |
| 5 | `team_make_hostile` | unknown name (id 9) → writes past the row / into an unrelated pair, replicated | CONFIRMED arithmetic; matrix size OPEN |
| 6 | `vcust_preview_palette` | three unchecked indices; null zone record read before the guard | CONFIRMED |
| 7 | `vcust_preview_color` | unchecked colour index; no `+0xbd0` guard → read of `0x400` | CONFIRMED |
| 8 | `vcust_purchase_wheels` | two unchecked committed-preview pointers → write to `0x38` | CONFIRMED (reachability OPEN) |
| 9 | `store_weapon_change_weapon` | unchecked item index into global arrays; unchecked local player | CONFIRMED |
| 10 | `tutorial_get_case`, `store_weapon_change_weapon` | non-string name → CRT invalid-parameter path in `__stricmp` | CONFIRMED path; handler OPEN |
| 11 | engine tick `0x00ac3610` (gated by `vehicle_forced_corpse_removal_enabled`) | >48 candidates overrun three stack arrays | CONFIRMED missing bound; reachability OPEN |
| 12 | `vehicle_engine_check_running` | success path returns no value | CONFIRMED (logic defect, not a crash) |

## H. OPEN items

- A.3: meaning of object byte `+0x33` bit `0x08`; how the two call sites cope with the `nil` result.
- A.4: `0x00ad2f80`'s class values; the real meaning of the start/stop bit 2 (arg 2).
- A.6: whether `0x0087ba20` is ever null in single player (then the setter is a no-op); the variable system behind
  `0x0086d770`; whether the corpse list can exceed 48 entries.
- A.7: what vehicle `+0xbf8` marks; `0x00941f40`'s returned object.
- A.0/A.8: `0x00b105c0`, `0x00b0fdf0`, `0x00ad5180`, `0x004f4d00`; whether repeated orders leak occupancy slots.
- A.10: the vector at `0x013213e4`.
- A.12: names of AI actions 37-39; `0x00b0b170`, `0x00b058f0`.
- B.1: the relationship matrix's real size and the meaning of value 0 / teams 5-6; the network receiver of opcode
  `0x43`/`0x30`.
- B.2: each of `0x00715ad0`'s eleven gate predicates.
- B.3: the writer of tutorial entry `+0x10`.
- C.1: affordability/clamping inside `0x0094d920`; when the prepared script callback actually runs.
- C.3: which code sets `0x012ec141` back to 1.
- D.2: the identity of the interface object `0x012fced8` and screen id `0x2e`.
- D.3: meaning of store sub-states 8, 9, 10.
- E.3: the meaning and writers of `0x022cf248`.

## I. Direct-answer table

| # | name | address | registrar | status |
|---|---|---|---|---|
| 1 | `vehicle_hidden` | `0x00a62610` | gameplay `0x00a20840` | resolved, body CONFIRMED |
| 2 | `vehicle_forced_corpse_removal_enabled` | `0x00a624d0` | gameplay | resolved, CONFIRMED (consumer tick read; stack-bound defect) |
| 3 | `vehicle_exit_guardian_angel` | `0x00a66c70` | gameplay | resolved, CONFIRMED (action names OPEN) |
| 4 | `vehicle_enter_guardian_angel` | `0x00a67670` | gameplay | resolved, CONFIRMED |
| 5 | `vehicle_enter_group_do` | `0x00a67330` | gameplay | resolved, CONFIRMED |
| 6 | `vehicle_enter_group_check_done` | `0x00a66920` | gameplay | resolved, CONFIRMED |
| 7 | `vehicle_enter_do` | `0x00a67000` | gameplay | resolved, CONFIRMED |
| 8 | `vehicle_engine_start` | `0x00a62380` | gameplay | resolved, CONFIRMED (class meaning OPEN) |
| 9 | `vehicle_engine_check_running` | `0x00a63e50` | gameplay | resolved, CONFIRMED — **returns nothing on success** |
| 10 | `vehicle_disable_weapon_physics` | `0x00a62330` | gameplay | resolved, CONFIRMED |
| 11 | `vehicle_delete_all_corpses` | `0x00a63d30` | gameplay | resolved, CONFIRMED (list identity HIGH CONFIDENCE) |
| 12 | `vehicle_anim_playing` | `0x00a62050` | gameplay | resolved, CONFIRMED |
| 13 | `vcust_purchase_wheels` | `0x0081fe70` | UI sub-registrar `0x00820aa0` | resolved, CONFIRMED (null-write path) |
| 14 | `vcust_preview_wheel_sizing` | `0x0081f5d0` | `0x00820aa0` | resolved, CONFIRMED (**null read without target**) |
| 15 | `vcust_preview_palette` | `0x0081f8d0` | `0x00820aa0` | resolved, CONFIRMED (unchecked indices) |
| 16 | `vcust_preview_color` | `0x0081f860` | `0x00820aa0` | resolved, CONFIRMED (unchecked index) |
| 17 | `tutorial_visible` | `0x00a602e0` | gameplay | resolved, CONFIRMED (queue-rotation side effect) |
| 18 | `tutorial_get_case` | `0x00a60390` | gameplay | resolved, CONFIRMED (field writer OPEN) |
| 19 | `team_make_hostile` | `0x00a607a0` | gameplay | resolved, CONFIRMED (**null read, out-of-bounds write**) |
| 20 | `store_weapon_purchase_ammo` | `0x008165c0` | UI sub-registrar `0x00817570` | resolved, CONFIRMED (**two null-access paths**) |
| 21 | `store_weapon_process_post_bg_covered` | `0x00815460` | `0x00817570` | resolved, CONFIRMED (reset writer OPEN) |
| 22 | `store_weapon_change_weapon` | `0x00815f40` | `0x00817570` | resolved, CONFIRMED (unchecked index) |
| 23 | `store_vehicle_selection_should_lock_controls` | `0x00813170` | UI sub-registrar `0x008152a0` | resolved, CONFIRMED |
| 24 | `store_vehicle_retrieve_car_and_exit` | `0x00813020` | `0x008152a0` | resolved, CONFIRMED |
| 25 | `store_vehicle_change_mode` | `0x00815120` | `0x008152a0` | resolved, CONFIRMED |
