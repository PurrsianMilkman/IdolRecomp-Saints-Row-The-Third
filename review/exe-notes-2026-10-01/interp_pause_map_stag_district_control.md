# `pause_map_stag_current_district_control`: location, behaviour, and what "stag" means

Team A, 2026-10-02. Investigation-only pass against the real executable (local Ghidra project, program
`SaintsRowTheThird.exe`, headless `-readOnly -noanalysis`, `ghidra/CrreishDump.java`; the shared project
was lock-contended, so most dumps ran against a private copy `tools/gp_pmstag_b`, deleted afterwards).
Raw dump output lives in this session's scratchpad (`g1`..`g7`) and is not committed; everything below is
described in prose. No spec file was edited.

**Why.** A partner team's runtime stub ranking shows this Lua-bound name called 303 times with no host
implementation. `spec-lua-api-behaviour.md` has zero mentions of it, so this is a genuine gap (unlike the
five sibling names in the same batch, which were already specced).

Labels follow the house style: **CONFIRMED — disassembly** = read in a listed instruction stream of this
pass's dumps; **HIGH CONFIDENCE** = follows from a dumped instruction or reference list but a body it points
at was not dumped (or a role name is inferred); **HYPOTHESIS** = plausible, not settled; **OPEN** = not
settled, collected at the end.

---

## 1. Findings

### 1.1 It is a UI-registrar name, not a gameplay-registrar name — CONFIRMED

The brief assumed the name lives in the 1,014-name gameplay registrar `0x00a20840`. It does not. The
project's own name census (`tools/lua_all_registered_1490_tagged.txt`) tags it `ui`, and the string
`"pause_map_stag_current_district_control"` (`.rdata` `0x0115aad0`, one occurrence, one code use) is used
only at `0x007dfb02`, inside the wrapper `0x007dfae0`. That wrapper's single caller is `0x008430f0` — the
311-name UI registrar already named in `spec-lua-api-behaviour.md`'s preamble and in `spec-lua-bindings.md`.

The wrapper builds a 9-entry {name pointer, function pointer} array on the stack (name at the lower slot,
function in the next one), then loops 9 times: push the function as a C closure with zero upvalues
(`0x00dfe4f0(L, fn, 0)`), then store it as a global under the name (`0x00dfe830(L, -10002, name)`, i.e. the
globals pseudo-index). The whole pause-map family it registers: **CONFIRMED — disassembly**

| # | Name | Function |
|--:|---|---|
| 1 | `pause_map_stag_takeover_do_reward` | `0x007de1c0` |
| 2 | **`pause_map_stag_current_district_control`** | **`0x007de0d0`** |
| 3 | `get_world_income_dollars` | `0x007db9c0` |
| 4 | `pause_map_is_stag_mode` | `0x007d98b0` |
| 5 | `pause_map_is_tutorial_mode` | `0x007d98e0` |
| 6 | `pause_map_set_gps` | `0x007dba10` |
| 7 | `pause_map_add_bookmark` | `0x007df8a0` |
| 8 | `pause_map_drag_map` | `0x007d9910` |
| 9 | `pause_map_zoom` | `0x007dbcc0` |

So **the function's address is `0x007de0d0`** (body `0x007de0d0`–`0x007de1be`).

### 1.2 The body of `0x007de0d0` — CONFIRMED — disassembly

- Calls the argument-count helper `0x00dfde50` (`lua_gettop`) and **discards the result**. No stack slot is
  ever read. **Arguments: none; any arguments passed are ignored.**
- Reads the selected-zone global `0x0229a2ac` (see 1.3). Call it Z.
- **If Z is 0 (nothing selected):** pushes the number `0.0` first and counts it as one return value. It
  then re-reads Z (still 0) and carries on into the loop below — it does **not** return early.
- Walks a list rooted at the world singleton global `0x03171a64`: count = int at `+0x198`, a `u16` index
  array whose pointer is at `+0x190`, and an object-pointer table whose pointer is at `+0x58`; member *i*
  is `table[index[i]]`. The count is read once, before the loop. (`0x03171a64` is the same global
  `spec-lua-api-behaviour.md` §8.10/§8.11 and the cutscene idle driver walk, but at different offsets;
  this is a different typed sub-list of the same world singleton.)
- For each member whose pointer field `+0x4c` equals Z: adds the member's float `+0x3c` (its weight) to a
  running **total**; if bit `0x02` of the member's byte `+0x3a` is set, also adds the same weight to a
  running **owned** sum. Both sums start at 0 and are kept in single precision (each addition is done in
  double and rounded back to single).
- After the loop: if total > 0, the result is owned ÷ total (division in double, rounded to single);
  otherwise (total ≤ 0, which also covers "no members matched") the result is **1.0** (the `.rdata` double
  at `0x012a2d70`, read this pass as `0x3ff00000:00000000`). A NaN total also falls into the 1.0 branch
  (the compare is an unordered "not above").
- Pushes the result as a Lua number (`0x00dfe3a0`, tag 3) and returns the count of pushed values.

**Return:** 1 number in the normal case (Z non-zero): the weighted fraction, in [0, 1], of the selected
zone's members that are owned. **2 numbers when Z is 0**: first `0`, then the fraction computed over
members whose `+0x4c` is null (in practice almost certainly none, giving 1.0 — HYPOTHESIS). The
2-value shape is a faithful reading of the code, not a guess; it looks like a missing early return in the
original, but a reimplementation should reproduce it (a Lua caller doing `local c = ...` sees 0 either way).

**Side effects:** none. Read-only query; Z is only compared, never dereferenced, so it is safe to call
with nothing selected.

### 1.3 Where the selected zone comes from — CONFIRMED (writers/readers), HIGH CONFIDENCE (roles)

Full reference census of `0x0229a2ac` (7 uses): written only by `0x007dba10` (`pause_map_set_gps`) and
by `0x007db3e0` (writes 0); read only by `0x007de0d0` (this function) and three times by `0x007de1c0`
(`pause_map_stag_takeover_do_reward`). **CONFIRMED — disassembly.**

- `pause_map_set_gps` (`0x007dba10`), when the stag-mode flag `0x0229a317` is set, does **not** set a GPS
  route. Instead, if the hovered zone `0x0229a2e4` is non-null, its pointer field `+0x44` is non-null and
  its int `+0x54` is greater than 0, it copies the hovered zone into `0x0229a2ac` and fires the Lua hook
  `"pause_map_stag_completion"` through the existing hook-lookup/dispatch trio `0x00e0cef0` / `0x00e0ca80`
  / `0x00e0cd00` (the same `pause_map_stag_completion` site `spec-lua-bindings.md` already lists), with the
  dispatch record's `+0x14` set to the value of the global `0x0229a364`. In stag mode with no valid hovered
  zone it does nothing. **CONFIRMED — disassembly.**
- The hovered zone `0x0229a2e4` is written by the pause map's per-update refresher `0x007dd700` with the
  result of `0x0084ba60(cursor position, previous)` — the zone-containment lookup that
  `spec-tables-environment.md` §11.8 already documents (`Map_district` `+0xB0` holds the `+0x44` of that
  same lookup's result). The refresher compares the hovered zone's `+0x44` with each `Map_district` row's
  `+0xB0` to pick the row whose name/team text to display. So the hovered/selected object is a **map zone**
  and its `+0x44` is its **parent district record**. **CONFIRMED — disassembly** (the lookup and the
  comparison); **HIGH CONFIDENCE** ("zone"/"district" as role names).
- `0x007db3e0` clears both `0x0229a2ac` and `0x0229a2e4`; its only caller is `0x007dd3f0`, which is
  referenced only as data from `0x0115a668` — a pause-map screen callback table (teardown/close handler,
  **HIGH CONFIDENCE**).

So the "district" in the Lua name is, at engine level, the zone selected on the pause map (the object the
cursor resolves to), and the value returned is computed over that zone's own members, not read from the
parent district record's own control figures.

### 1.4 What the member flag means — HIGH CONFIDENCE

Bit `0x02` of member byte `+0x3a` is the "owned by the player" flag:
`pause_map_stag_takeover_do_reward` calls `0x005e97a0` on each matching member, which (only when bit `0x02`
is still clear) calls `0x005e8b30` — and `0x005e8b30` is the routine that sets bit `0x02` (and, under a
session-state condition, bit `0x01`) of `+0x3a`, notifies the parent through `0x0084aeb0` when `+0x4c` is
non-null, and (with both of its arguments 0, and only under a session-state condition) replicates network
opcode `0x45` sub-type `0x1d` for the member's id pair at `+0x08`/`+0x0c`. **CONFIRMED — disassembly** for the bit writes
and the opcode; **HIGH CONFIDENCE** for the "owned" reading. Collateral: this is the "per-object effect"
of `0x005e8b30` that `spec-lua-api-behaviour.md` §28.15's `0x005e8fb0` note leaves OPEN — it marks the object
owned (bits `0x02`/`0x01` of `+0x3a`), and the opcode `0x45`/`0x1d` replication is inside it too.

The member weight `+0x3c` is a plain float summed per zone (role HYPOTHESIS: the member's contribution to
territory control).

### 1.5 "stag" is a pause-map mode, not "staging" — CONFIRMED

There is no staged/committed value pair. "stag" names a pause-map **mode flag**, the byte `0x0229a317`.
Full reference census (12 uses, 10 functions), **CONFIRMED — disassembly**:

- **Set to 1** only by `0x0071b640`, which then makes four further calls starting with `0x007e1d00(1)`
  (opening the pause map — HIGH CONFIDENCE, those bodies were not dumped).
  Its sole caller is `0x0071f74e`, the target of **case 48** of the reward-apply switch in `0x0071f170`
  (jump table `0x0071f8d4`, 61 entries; entry 48 is the only one pointing there; the function walks records
  tagged `"Reward"`). Reward type 48 is **`Auto_Complete_City_Takeover_District`** in
  `spec-tables-progression.md`'s reward-type table. So stag mode = "the player has been granted a free
  district takeover and must pick a zone on the map".
- **Cleared** by `pause_map_stag_takeover_do_reward` (`0x007de1c0`, at `0x007de2e3`) and by `0x007dab20`, a
  pause-map close handler referenced only as data from `0x0115a684` (which also clears the tutorial-mode
  flag `0x0229a318`, the byte `pause_map_is_tutorial_mode` returns).
- **Read** by `pause_map_is_stag_mode` (`0x007d98b0`, returns it as a boolean via `0x00dfe590`), by
  `pause_map_set_gps` (1.3), by the map refresher `0x007dd700` (in stag mode it shows district values from
  `0x0084ce80` — owned ÷ total of the district record's `+0x94`/`+0x98`, 0 if the total is ≤ 0.01 —
  instead of `0x0084ce30`), by the bookmark function `0x007df8a0`, by `0x007d8c80`, `0x0083c040`, a
  data reference at `0x0083e928`, and by the autosave gate `0x00b95060` (`spec-lua-api-behaviour.md` §27.12),
  which **suppresses autosave while stag mode is on**.

**No committed sibling exists.** No registered name `pause_map_district_control` (or any non-"stag"
district-control getter) appears among the 1,490 registered names; the only other district names are the
gameplay-table `district_set_pulsing` and `on_district_changed`. The gameplay-table `get_stag_active` /
`set_stag_active` / `set_stag_notoriety_area_active` are unrelated to this flag: none of them is among its
three writers. The natural "commit" partner is `pause_map_stag_takeover_do_reward` (1.6). Whether "stag"
in these names echoes the story faction of that name is a naming question only (HYPOTHESIS; irrelevant to
behaviour).

### 1.6 The committing partner, `pause_map_stag_takeover_do_reward` (`0x007de1c0`) — CONFIRMED — disassembly, roles HIGH CONFIDENCE

For the spec entry's cross-reference (not a full entry): reads no arguments; captures the local player's
(`0x009da4e0`) int `+0x1ca0` (divided by 100.0, the `.rdata` double at `0x012a2dd8`) and the int returned
by `0x009db890` (player `+0x1ed0`, the total-respect field per `spec-save-format.md`) and the parent
district's control fraction (`0x0084ce80` on selected zone `+0x44`); walks the same member list and claims
every member of the selected zone (1.4); calls `0x008b85d0`; re-reads the three values; passes
(change in `+0x1ca0`/100, change in respect, fraction before, fraction after, 0) to `0x007ef9a0` (a reward
summary display, HYPOTHESIS); clears stag mode; requests an autosave (`0x00b95060`, now no longer
suppressed). Returns 0 values. Unlike the query, it dereferences the selected zone (`+0x44`) without a null
check, so it must only be called after a selection.

---

## 2. Direct answer — draft spec entry (transcribable)

### N.N `pause_map_stag_current_district_control` (`0x007de0d0`)

*UI registrar `0x008430f0` → pause-map wrapper `0x007dfae0` (9 names, §1.1 table above), not the gameplay
registrar.*

**Arguments:** none. The argument count is fetched and discarded; any arguments are ignored. **[CONFIRMED —
disassembly.]**

**Return:** normally 1 number — the weighted owned fraction of the zone currently selected on the pause
map, in [0, 1]; **1.0** when that zone has no members or their total weight is ≤ 0. When no zone is
selected (`0x0229a2ac` is 0) it returns **2 numbers**: `0`, then the same fraction computed over members
with no zone (in practice 1.0, HYPOTHESIS). **[CONFIRMED — disassembly; the two-value case is a faithful
reading of the code.]**

**Body:** reads the selected-zone global `0x0229a2ac`; walks the world singleton's (`0x03171a64`) typed
member list (count `+0x198`, `u16` indices at `+0x190`, object pointers at `+0x58`); for each member whose
`+0x4c` equals the selected zone, adds its float `+0x3c` to a total and, if bit `0x02` of its byte `+0x3a`
is set, to an owned sum (single-precision accumulation); returns owned ÷ total, or 1.0 if total ≤ 0.
**[CONFIRMED — disassembly.]**

**Side effects/subsystem:** none — a read-only query, safe with no selection. Subsystem: pause map,
"stag mode" (the free-district-takeover picker entered by reward type 48
`Auto_Complete_City_Takeover_District`). The selected zone is set by `pause_map_set_gps` while in stag mode,
which then fires the Lua hook `pause_map_stag_completion`; the matching commit is
`pause_map_stag_takeover_do_reward` (`0x007de1c0`), which marks every member of the zone owned and leaves
stag mode. "stag" is this mode, not a staged/committed pair; no non-stag sibling exists. **[CONFIRMED —
disassembly for the flow and globals; HIGH CONFIDENCE for "owned" (bit `0x02`, set by `0x005e8b30`) and the
zone/district role names; HYPOTHESIS for the meaning of the weight `+0x3c`.]**

**Implementation note for a host stub:** outside stag mode the selection global is 0 (it is cleared by the
pause-map teardown `0x007db3e0`), so a minimal faithful stub returns `0, 1.0` there; inside stag mode it
must compute the fraction from the host's territory model.

---

## 3. OPEN

1. The type of the members in the `+0x190`/`+0x198` sub-list of `0x03171a64` (ownable territory objects —
   HYPOTHESIS) and the meaning of their weight `+0x3c`.
2. The meaning of the selected zone's `+0x54` (must be > 0 for a selection to be accepted) and of the
   value `0x0229a364` written into the `pause_map_stag_completion` hook record's `+0x14`.
3. Player `+0x1ca0` (divided by 100 in the reward summary) and the bodies of `0x008b85d0` and `0x007ef9a0`.
4. The Lua side was not read (no extracted UI scripts are available locally): which return value(s) the
   shipped `pause_map_stag_completion` handler consumes, and why the partner's ranking counts 303 calls.
5. `0x0084ce30` (the non-stag district value the map shows) was not dumped.
