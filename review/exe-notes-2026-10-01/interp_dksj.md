# Interpretation of bridge job dksj (Team B request 9): co-op session, tutorial table, vehicle-store flag

Dumps: `/root/crreish-bus/results/20261001T020213-team-a-dksj/` (job `team-a/ghidra/jobs/teamb-request-9-coop-tutorial-store.json`).
Written 2026-10-01 by Team A. Labels: **CONFIRMED — disassembly** only for what these dumps show; otherwise HIGH CONFIDENCE / HYPOTHESIS / OPEN.

Note on the job itself: the `depth=1 maxfuncs=12 maxinsn=900` options were passed as `key=value` and were parsed as item
addresses (each index.txt shows a NullPointerException for `depth`, `maxfuncs`, `maxinsn` and "no function here" for `1`, `12`,
`900`). The dumps ran with the defaults (depth 1, maxfuncs 40, maxinsn 600), which was enough here. Future jobs: write the
options as `key:value` (per the script header), not `key=value`.

---

## Q1. Co-op session singleton (global `0x024d8534`, accessor `0x0087ba20`)

### Answer

**Initial value: zero.** The global lives in `.data` in a block that is *not* file-backed (zero-fill at load), static dword
`0x00000000`. The executable therefore starts with **no session object**. **[CONFIRMED — disassembly:
`globals/xref_0x024d8534.txt` header line.]**

**Accessor `0x0087ba20`** is two instructions: load the dword, return it. No side effects, no arguments.
**[CONFIRMED — disassembly: `coop/func_0x0087ba20.txt`.]** It has 1,867 call sites in 753 functions
(`callers/xref_0x0087ba20.txt`), every one a plain call; none of the writers below is among them (they touch the global directly).

**Every reference to the global in the executable** (10 in 6 functions, `globals/xref_0x024d8534.txt`):

| site | function | what it does | role |
|---|---|---|---|
| `0x0087ba20` | `0x0087ba20` | load | the accessor |
| `0x0087ba68` | `0x0087ba60` | compare a register against the global | an "is this pointer the current session" test (not dumped) |
| `0x0087c341`, `0x0087c359` | **no function** (undefined code; Ghidra reports them as data references) | **unknown — could be reads or writes** | unresolved, see next dumps |
| `0x0087d8cd`, `0x0087d8dc` | `0x0087d8a0` | compare with 0, then **store 0** | a clear/teardown path (not dumped) |
| `0x0087edbf`, `0x0087edd1` | `0x0087ed70` | load the value, then **store 0** | a destroy path (the loaded pointer is presumably released; not visible) |
| `0x0087f10d`, `0x0087f11a` | `0x0087efe0` | compare with a register, then **store that register** | **the only resolved site that can store a non-zero value** — the session setter |

So among the references Ghidra resolved, exactly one function installs a session (`0x0087efe0`) and two functions clear it
(`0x0087d8a0`, `0x0087ed70`). **[CONFIRMED — disassembly for the reference set; the bodies of the three writers were not
dumped, so *when* each runs is OPEN.]** The two sites at `0x0087c341`/`0x0087c359` are in code Ghidra never defined as a function;
they may be further writers. **[OPEN]**

**What single-player startup leaves it as: OPEN.** The exe starts at 0; whether `0x0087efe0` runs when a single-player game
starts (i.e. whether the engine keeps a session object for the local game with the local player as its only member) cannot be
read from these dumps. One piece of evidence bears on it without settling it: the `coop_is_active` core (`0x00867830`, below)
explicitly handles a session whose member count is ≤ 1 by answering *false*, so "a session exists but co-op is not active" is a
state the engine expects. **HYPOTHESIS (do not implement):** single player runs with a one-member session whose local member is
the host, which would make `game_get_is_host` true and `coop_is_active` false in single player. Until `0x0087efe0`'s callers are
read, Team B has two candidate single-player answers: (a) no session: `coop_is_active`=false, `game_get_is_host`=false,
`Completion_is_client`=false; (b) one-member host session: `coop_is_active`=false, `game_get_is_host`=true,
`Completion_is_client`=false. **`coop_is_active` is false in single player under both** (see the member-count rule below), which
covers the bulk of the 876 refusals; `game_get_is_host` is the one that genuinely needs the next dump.

### The three Lua queries — full bodies read

- **`game_get_is_host` (`0x008440a0`)**: pushes true iff the session pointer is non-null AND the dword at session`+0x5c`
  equals the dword at session`+0x58`. No session → false. **[CONFIRMED — disassembly: `coop/func_0x008440a0.txt`.]**
- **`Completion_is_client` (`0x007bfbc0`)**: pushes true iff the session pointer is non-null AND session`+0x5c` ≠ session`+0x58`.
  No session → false. **[CONFIRMED — disassembly: `coop/func_0x007bfbc0.txt`.]**
- **`coop_is_active` (`0x00a42bd0`)**: pushes the result of `0x00867830`, which is true iff ALL of:
  1. the session pointer is non-null;
  2. either the local machine is the host (`+0x5c == +0x58`), **or** the client-side gate passes:
     (session`+0xf4` ≥ 2, OR session`+0x224` ≠ 0, OR session`+0xf4` ≠ session`+0xf8`) AND the byte at session`+0xfd` is 0;
  3. the value `0x00681370` returns — the dword at session`+0x60` — is **≥ 2** (unsigned compare: ≤ 1 fails);
  4. walking the member list from the head pointer at session`+0x54` (next pointer at node`+0xb28c`; stop at null or when
     back at the head), every node **other than the one equal to session`+0x5c`** passes `0x00877a90`: the byte at
     node`+0x158` indexes a 5-byte-stride array at (session`+0x50`)`+0xc` with element count at (session`+0x50`)`+0x10`;
     the node passes when the index is in range, the element's byte `+2` is non-zero, and the element's byte `+0`, read as a
     signed char, is ≥ 1 (an out-of-range or absent slot yields −1 and fails).
  **[CONFIRMED — disassembly: `coop/func_0x00a42bd0.txt` (callee `0x00867830` fully listed), `coop/func_0x00681370.txt`,
  `coop/func_0x00877a90.txt`.]**

Resulting table (CONFIRMED where the row's condition is stated in terms of the fields above):

| session state | `coop_is_active` | `game_get_is_host` | `Completion_is_client` |
|---|---|---|---|
| none (global = 0) | false | false | false |
| host session (`+0x5c == +0x58`) | true iff `+0x60` ≥ 2 and every member other than the `+0x5c` one passes the slot check; **false with a single member** | true | false |
| client session (`+0x5c != +0x58`) | true iff the client-side gate passes AND the same member rule | false | true |

**Field meanings.** `+0x5c` is compared against member-list nodes and is the node skipped in the "are the *other* members
valid" walk, so it is a pointer to a member record — the local member **[HIGH CONFIDENCE — inferred from the skip, not from a
name]**. `+0x58` is the member record it is compared with, so "is host" = "the local member record *is* the host member
record" **[HIGH CONFIDENCE]**. `+0x54` is the list head, `+0x60` the member count (it bounds the list walk)
**[HIGH CONFIDENCE]**. `+0xf4`/`+0xf8`/`+0xfd`/`+0x224` form a client-side connection-state gate (a current/target state pair, a
byte, and a non-zero "override" word): **[OPEN — only their gating role is confirmed]**.

### Relation to the current spec

- **§3.1 `coop_is_active` — corrects.** The OPEN item ("head/tail member-list pointers and a small state counter", field offsets
  not given) is settled: the fields are `+0x54` (list head), `+0x5c`/`+0x58` (the same member-record pair §8.27 uses — the
  host short-circuit is the *first* test in the body), `+0x60` (member count, read via `0x00681370`, must be ≥ 2), and the
  client-side gate `+0xf4`/`+0xf8`/`+0xfd`/`+0x224`. `0x00681370` is a one-line reader of session`+0x60` (not a "network-match-state
  accessor"); `0x00877a90` is the per-member slot-status check described above. The member walk starts at `+0x54`, not at a
  head/tail pair. Replace the body paragraph accordingly.
- **§8.27 `game_get_is_host` — confirms** (body identical to the dump; adds: no-session → false explicitly).
- **§10.2 `Completion_is_client` — confirms** (the accessor is called twice, as the spec's "re-reads" wording says).
- **§6.22 cross-function note — confirms** that `+0x58`/`+0x5c` is the host pair, and adds that `coop_is_active` tests the same
  pair first.
- **Adds (new, for Team B's engine-state model):** initial value 0; writer set (`0x0087efe0` installs, `0x0087d8a0`/`0x0087ed70`
  clear; `0x0087c341`/`0x0087c359` unresolved); a session with < 2 members is never "active".

### Next dumps (Q1)

1. `func` depth 1: `0x0087efe0`, `0x0087d8a0`, `0x0087ed70`, `0x0087ba60` — the writers and the comparer.
2. `xref`: `0x0087efe0` — who installs a session, and from which startup path (this decides the single-player answer).
3. The undefined code at `0x0087c341`/`0x0087c359`: `CrreishDump.java` has no force-disassemble step; add one (create a
   function at the nearest defined start, or a `disasm <addr>` mode) and dump it — these are the only unresolved references.
4. Optional: `xref` of the session`+0x60` / `+0x54` writers is not possible by address (they are field offsets); instead dump
   the function that allocates the object passed to `0x0087efe0` once (2) names it, to see what a one-member session looks like.

---

## Q2. Tutorial table (`0x0151d600` base — see layout; 36-byte stride, 210 entries; name table `0x012f5930`)

### Answer in one paragraph

The table is **run-time state, zero at load**, not static data: its block is `.data`, not file-backed, static dword 0
(`globals/xref_0x0151d608.txt`, `xref_0x0151d60c.txt`). Its entries are 36 bytes (index × 9 dwords everywhere), base
**`0x0151d600`** (the dispatcher forms entry pointers as `index*36 + 0x0151d600` at `0x00716b5a`/`0x00716b78`;
`0x00715bf0` does the same at `0x00715bfa`), so the two addresses in the spec are fields: `0x0151d608` is **entry `+0x08`**, a
*pointer* to a per-tutorial descriptor record, and `0x0151d60c` is **entry `+0x0c`**, a per-entry **run-time state** taking the
values 0–4. The "kind tag 3" of §10.4 is therefore **not a kind: it is state 3 of a per-entry state machine**, and
`tutorial_advance` succeeds only for an entry currently in that state. The 210 names live in a separate, file-backed pointer
table at `0x012f5930` (`.data`, 210 dwords, each pointing at a `.rdata` string); the dumps dereference only its first two
entries, so the full name list needs a new dump (below). **[CONFIRMED — disassembly for everything in this paragraph except
the "not a kind" reading of value 3, which is CONFIRMED as a *written* run-time value but whose meaning is OPEN.]**

### Who fills it, and when

- **Entry `+0x08` (descriptor pointer)** has exactly one writer in the executable: `0x00715850` at `0x0071589f`, which also
  writes the state at `+0x0c` to 0 (`0x007158a5`) or 1 (`0x007158b1`) in the same function. **[CONFIRMED — xref; the function
  was not dumped, so what the descriptors are built from and when it runs are OPEN.]** Readers of `+0x08`: `0x00715bf0`,
  `0x007169e0` (×2), `0x00717390`, and undefined code at `0x0071724e`.
- **Entry `+0x0c` (state)** writers, from `globals/xref_0x0151d60c.txt` (28 uses in 15 functions):

  | value written | by | notes |
  |---|---|---|
  | 0 or 1 | `0x00715850` | at fill time |
  | 0 | `0x00716140` | only if the entry is currently 1 (a "disarm") |
  | 1 | `0x00716170` | bounds-checked, and only while the global byte `0x0151d5a6` is 0 (body dumped) |
  | 1 | `0x007161a0` | reads the state first; also uses the entry base |
  | 1 | `0x007169e0` at `0x00716b63` | the dispatcher, when told to "enable" (see below) |
  | 2 | `0x00715bf0` | the simple-prompt path (body dumped) |
  | 4 | `0x007169e0` at `0x00716c46` | right after the rich-widget path |
  | 4 | `0x00716ed0`, `0x00717390`, undefined code at `0x00717311` | not dumped |
  | register (any) | `0x007163e0` (generic setter), undefined code at `0x00717363` | **the only places that can write 3** |
  | whole-table reset: 0, then 1 under a condition read from byte `0x0151d5a6` | `0x00716940` | not dumped; loop over the table |

  Readers testing specific values: `== 3` in `0x007162a0` and in `0x00716440` (`tutorial_advance`'s core); `== 4` in
  `0x007162d0`; `== 1` in `0x00716140`; plain read in `0x00716360` (the state getter, body dumped: returns the state, or 0 for
  an index ≥ 210).

  **No function writes the literal 3.** State 3 is set only through the generic setter `0x007163e0` (value in a register) or the
  undefined code at `0x00717363`. **What state 3 means is OPEN.** What is CONFIRMED: 0 = untouched (zero-fill); 1 = "armed/enabled"
  (the dispatcher proceeds from it); 2 = "queued as a simple prompt" (set when the entry is linked into the prompt list);
  4 = "rich widget issued" (set after the widget is created; the dispatcher also proceeds from 4 when its 4th Lua argument is true).
  **HYPOTHESIS:** 3 = "displayed, waiting for the player to complete the step", the state in which a script's
  `tutorial_advance` makes sense — but nothing in these dumps shows it.

### Field layout of an entry (base `0x0151d600` + index × 0x24)

| offset | content | evidence | label |
|---|---|---|---|
| `+0x00` | next pointer in a circular doubly-linked list of queued prompts, head global `0x0151d588` | `0x00715bf0` at `0x00715c23`/`0x00715c36` | CONFIRMED |
| `+0x04` | previous pointer of that list | `0x00715c25`/`0x00715c33` | CONFIRMED |
| `+0x08` | pointer to the entry's descriptor record (see below) | `0x00716a86`→`+0x24` test; writer `0x0071589f` | CONFIRMED |
| `+0x0c` | state 0–4 | above | CONFIRMED |
| `+0x10` | not touched in these dumps | — | OPEN |
| `+0x14` | `tutorial_start` argument 2 (the number) | `0x00715c50` | CONFIRMED |
| `+0x18` | 0 if `tutorial_start` argument 5 is true, else the descriptor's `+0x20` dword | `0x00715c59`/`0x00715c6f` | CONFIRMED |
| `+0x1c` | the internal literal 1 (`tutorial_start`'s hidden 4th parameter) | `0x00715c60`/`0x00715c72` | CONFIRMED |
| `+0x20` | not touched in these dumps | — | OPEN |

**Descriptor record** (pointed to by `+0x08`; its own layout only where read): `+0x00` a pointer used as the default widget
source (`0x00715760` reads descriptor`[0]` when no builder is set); `+0x14` optional builder function pointer (`0x00715760` calls
it with the descriptor and 0 if non-null); `+0x20` a dword copied into entry `+0x18` (a default duration/parameter);
`+0x24` **flag bits**: `0x04` = use the rich (animated/video) widget instead of the simple prompt; `0x08` = allowed while a
mission is active; `0x10` = host-only (suppressed on a co-op client, and forces replication from the host).
**[CONFIRMED — disassembly for the bit tests at `0x00716a8d`, `0x00716ae0`, `0x00716ba2`, `0x00716c92`; the English names of the
bits are HIGH CONFIDENCE from their gating role.]**

**Name table `0x012f5930`:** file-backed `.data`, read by `0x00717780` (name → index, case-insensitive linear scan of 210
pointers, −1 on miss) and by `0x00717750` (index → name, not dumped). The dump resolves index 0 → `save` (`0x0113f51c`) and
index 1 → `autosave` (`0x0114368c`). **The other 208 names are not in these dumps.** `0x00717780` has nine callers, all
`tutorial_*` Lua wrappers: `0x00a60060`, `0x00a600b0` (`tutorial_advance`), `0x00a60100`, `0x00a60150`, `0x00a60190`,
`0x00a601f0`, `0x00a60390`, `0x00a603f0` (`tutorial_start`), and undefined code at `0x00a60586` (`callers/xref_0x00717780.txt`).
`0x00716440` (`tutorial_advance`'s core) has exactly one real caller, `0x00a600b0` (the other hit, `0x0078c02b`, is an
immediate coincidence inside a call to `0x00ea2470`).

### What `tutorial_advance` (`0x00a600b0` → `0x00716440`) does

Argument 1 (string) → index via `0x00717780`; −1 → push **false**. Otherwise `0x00716440`: if index ≥ 210 or entry state ≠ 3
→ **false**, no side effect. Else: take the global UI context `0x02a45450` (via `0x00e1a1b0`, which ignores its two zero
arguments), build a named message tagged with the literal string `tutorial_advance` (`0x00e0ca80` → `0x00e0c720`), store the
global `0x0151d5a8` into the message's `+0x14`, dispatch it (`0x00e0cd00`: only if the message's target is non-null and its
`+0x18` bit `0x10` is clear), call a stub that returns 1 (`0x00d2f500`), and push **true**. The entry's state is **not changed**
by `tutorial_advance`. **[CONFIRMED — disassembly: `tutorial/func_0x00716440.txt`, `func_0x00a600b0.txt`.]**
`0x0151d5a8` is zero-fill, written only by `0x00715f20` (which passes the global's address to a creator and stores the result),
and read by the tutorial display functions `0x00716000`, `0x007159e0`, `0x00715a30`, `0x00715f60` and by three UI functions
`0x007fea30`/`0x007fed00`/`0x007ff0a0` that use the same message helpers. **HYPOTHESIS:** `0x0151d5a8` is the handle of the
tutorial UI document and the message is a named UI event delivered to it. This refines §10.4's "named event/profiling scope"
(it is a message to the UI layer, not telemetry) — label it HYPOTHESIS until `0x00e0c720` is read.

### What `tutorial_start` (`0x00a603f0` → `0x007169e0`) does with an entry

Arguments: A = index from argument 1; N = argument 2 (number, default 0); F = argument 3 (bool, default false); the hidden
literal 1 (call it P4); B4 = argument 4 (bool); B5 = argument 5 (bool). The dispatcher (`tutorial/func_0x007169e0.txt`), in order:

1. If A < 189 (`0xbd`) and the global byte `0x0151d5a6` is non-zero → do nothing. (Entries ≥ 189 bypass this byte.)
2. If the global `0x029443bc` is 1, 2 or 3 (`0x00bc55a0`, `0x00bc5610`) → do nothing. **[OPEN: a game-mode/cutscene state;
   its writers are at `0x00bc4fd0`, `0x00bc6d80`, `0x00bc57d0`.]**
3. If A > 209 → do nothing.
4. If A < 189, a session exists and the local machine is **not** the host (`+0x5c != +0x58`), and the descriptor's `+0x24` has
   bit `0x10` → do nothing (host-only tutorial on a client).
5. If `0x00706ab0` (the dword at `0x01503b50[0x012f4a80]`, when that index ≥ 0) equals 6 → do nothing. **[OPEN]**
6. If the byte `0x014f3d34` (`0x006e44f0`) is non-zero: continue only if A == 188 (`0xbc`) or 196 ≤ A ≤ 201 (`0xc4..0xc9`);
   otherwise do nothing. If that byte is zero: continue if the descriptor has bit `0x08`; otherwise continue only if
   `0x006d2910` returns −1 (it returns −1 when the current-mission global `0x014c8460` is null or its class lacks bit `0x10`
   at descriptor-row `+0xc`; else it calls the mission object's virtual slot 2). I.e. without bit `0x08`, a tutorial is refused
   while a mission object is live.
7. If A == 76 (`0x4c`) and the byte `0x0141250d` is zero, A becomes 73 (`0x49`). **[OPEN — an input-device or platform
   alternative, by shape only.]**
8. If A ≥ 189: set state 1 via `0x00716170` (itself gated on byte `0x0151d5a6` == 0).
9. If F is true and byte `0x0151d5a6` == 0 and the entry's state is 0: set state 1.
10. Proceed only if state == 1, or (B4 is true and state == 4), or A ≥ 189. Otherwise do nothing.
11. If the descriptor has bit `0x04` (rich widget): build a parameter block on the stack (N converted to float and scaled by
    the double at `0x012a2d78`, the floats 5.0 and 0.5, several 1/0 fields), call `0x00715760` (descriptor builder or default
    source) then `0x007fc560` with the result, store the returned handle in the global `0x0151d5b4` (the "current rich
    tutorial"), and set state 4. Otherwise (simple prompt) call `0x00715bf0`, which: returns immediately if the entry is already
    in the prompt list; else calls `0x00d1dfd0`, links the entry at the tail of the circular list headed by `0x0151d588`
    (creating the list if empty), sets state 2, stores N at `+0x14`, and sets `+0x18` = 0 if B5 else descriptor`+0x20`, and
    `+0x1c` = P4 (= 1).
12. If a session exists and the local machine **is** the host, and (descriptor bit `0x10` or P4 == 1 — always true from Lua):
    open an opcode `0x54` record (`0x0086f5f0`), write a byte 1, then A (dword), the dword `0x0086be20` returns, P4 (byte),
    N (dword), B5 (byte), commit with `0x0086f1b0`(session, 0, 0) and close (`0x0086eb20`). So **a host always replicates
    `tutorial_start` to its clients**; a non-host or no-session machine never does.

**[CONFIRMED — disassembly for steps 1–12 as control flow; the meanings of the globals in 1, 2, 5, 6, 7 are OPEN as marked.]**

Index thresholds seen: 189 (`0xbd`, start of a "system" range that bypasses the suppression byte and the state gate),
188 (`0xbc`) and 196–201 (`0xc4`–`0xc9`) allowed during the `0x014f3d34` mode, 76→73 substitution. The spec's "repeated
`0xbc` split" is this 188/189 boundary.

### Relation to the current spec

- **§10.4 `tutorial_advance` — corrects.** "kind/type tag at table base `+4` … not shared by every entry" → per-entry
  **run-time state** at entry `+0x0c` (table base `0x0151d600`, so `0x0151d60c` is entry 0's state), compared with 3; the
  function returns false for any entry not in state 3, including every entry on a fresh process (all zero). The "named
  event/profiling scope" is a named UI message carrying the global `0x0151d5a8` (HYPOTHESIS as to the receiver). The rest
  (bounds, stride, literal name, no per-entry stash) is confirmed.
- **§6.19 `tutorial_start` — corrects and extends.** "a matching 210-entry descriptor table (`0x0151d608`, 36 bytes per entry)"
  → the table base is `0x0151d600`; `+0x08` is a pointer to a separately allocated descriptor whose `+0x24` holds the flag
  bits; the table is zero at load and filled by `0x00715850`. Adds the state machine, the 12-step gate order, the
  per-entry field writes, and the exact replication condition (host only; always from Lua). Confirms the two display paths,
  the `0x54` opcode, and the `0xbc`/`0xbd` split.
- **Adds:** the name table `0x012f5930` is static (file-backed) — the only static part; index 0 = `save`, 1 = `autosave`.

### Index → name → kind table

Not possible from these dumps: only indices 0 (`save`) and 1 (`autosave`) are dereferenced, and "kind" does not exist as a
static field (the per-entry flag bits live in run-time descriptors built by `0x00715850`). What Team B can be given now:
**name → index** needs the 210-pointer dump below; **per-entry behaviour** needs `0x00715850` (the descriptor filler) to see
where the `+0x24` bits come from (likely a static source table or a data file — OPEN).

### Next dumps (Q2)

1. **Name list:** `CrreishDump.java` has no data/pointer-table mode. Add a mode (e.g. `ptrs <addr> count:210`) that reads N
   dwords at `0x012f5930` and prints each target string, or run `func 0x00717750` after adding string annotation for indexed
   loads — the `xref` output only resolves the first two pointers.
2. `func` depth 1: `0x00715850` (descriptor filler: where the `+0x24` flag bits and the `+0x20` default come from),
   `0x00716940` (table reset), `0x007163e0` + `xref 0x007163e0` (the generic state setter — the only route to state 3),
   `0x007162a0`, `0x007162d0`, `0x00716140`, `0x007161a0`, `0x00716ed0`, `0x00717390`, `0x00715f20` (writer of `0x0151d5a8`),
   `0x00716420` (reader of `0x0151d5b4`).
3. Force-define the undefined code around `0x00717240`–`0x00717370` (references at `0x0071724e`, `0x00717311`, `0x00717350`,
   `0x00717363`) — one of the two places that can write state 3.
4. `xref` the other `tutorial_*` wrappers' cores to name them: `0x00a60060`, `0x00a60100`, `0x00a60150`, `0x00a60190`
   (calls the state getter `0x00716360`), `0x00a601f0`, `0x00a60390` (likely `tutorial_stop`, which §17 says uses opcode `0x54`).
5. `func 0x00e0c720` to settle what the `tutorial_advance` message is.

---

## Q3. Vehicle-store flag (`0x022cdf08`)

### Answer

**Initial value: zero** — `.data`, not file-backed, static dword 0 (`globals/xref_0x022cdf08.txt` header). So
`store_vehicle_get_state` (§10.1, `0x008133c0`) returns `0.0` until a writer runs. **[CONFIRMED — disassembly.]**

**Writers — the spec's "exactly one writer function" is wrong; there are three** (12 references in 8 functions):

| site | function | writes |
|---|---|---|
| `0x00815202` | `0x00815120` (`store_vehicle_change_mode`) | 0, on the mode-0 (close) path |
| `0x0081524e` | `0x00815120` | 1, on a non-zero mode when `0x005fa760` succeeds |
| `0x005fa8cd` | `0x005fa820` | **1** — not dumped; a sibling of `0x005fa760` by address |
| `0x00820dd6` | `0x00820cd0` | **0** — not dumped |

Readers: `0x005f5f50` (== 1), `0x005f7ae0` (compares with a register), `0x008133c0` (the Lua getter, == 0), `0x00813a80` (load),
and undefined code at `0x0081302d`, `0x0081308d`, `0x00813181`. **[CONFIRMED — xref for the set; the two extra writers'
conditions are OPEN.]**

**`0x00815120` body** (`store/func_0x00815120.txt`), corrected and extended against §10.1:

1. Reads argument 1 as a number M (mode). **If the flag already equals M, returns immediately with no effect** — note this
   compares the mode number with a flag that only ever holds 0 or 1, so "mode 1 while the flag is 1" is a no-op but "mode 2
   while the flag is 1" is not.
2. Increments a call counter `0x022cdf50`; captures the 64-bit handle pair `0x022cde00`/`0x022cde04` (the store's current
   vehicle); if non-zero, resolves it through `0x00458230` against the handle table at `0x024433a8`/`0x031d152c`, requires the
   object's byte `+0x33` bit `0x10` clear and its class row (`0x02cc9900[byte +0x34]`) byte `+6` bit `0x80` set, and the
   liveness check `0x00853b10`; otherwise the resolved object is treated as null.
3. **M == 0 (close):** `0x005f8a00` returns a new handle pair into `0x022cde00/04`; `0x005faa40`, `0x005fa950(0)`,
   `0x008183c0`(that, the dword at `0x022cdf1c`), `0x00820480`(resolved object or null), `0x00820810`,
   `0x00a9adc0`(0, handle pair) run; then **flag = 0** and the store sub-state `0x022cdf0c` = 8.
4. **M ≠ 0 (open):** `0x008208a0(0)`, then `0x005fa760`(the dword at `0x022cdf1c`). On success: `0x005fb9b0`, copy the handle
   pair into `0x022cdf00/04`, **flag = 1**, and reset `0x022cde00/04` from the two `.rdata` dwords at `0x0115e430/34`. On
   failure: sub-state `0x022cdf0c` = 10, flag unchanged.
5. In both branches, if an object was resolved in step 2, `0x00818fb0`(object, 0) runs last. Returns 0 Lua values.

**`0x005fa760`** (`store/func_0x005fa760.txt`): given a location id L (the dword at `0x022cdf1c`), `0x005f7d90` finds the
matching location object — if the local player's `+0x209c` is zero, it scans a world-object list rooted at `0x03171a64`
(count at `+0x1bc`, index table at `+0x1b4`, pointer table at `+0x58`) for objects whose `+0x7c` equals L and keeps the one
nearest the player (`0x00da1330` distance); otherwise it uses `0x005f0f90`(L). With no local player or no match → false.
On a match it fills a store-camera/target block at `0x014a1dc0..0x014a1de4` (object pointer, a per-type pointer from
`0x012ece20[byte +0x7a]`, two zero floats, a zero byte) and sets `0x014a1ce4` = 4; then, if the local player (`0x009da4e0`)
exists, resolves the player's vehicle handle pair (`+0x16c0/+0x16c4`, via `0x004dcf00`) and, if present, runs `0x005f8ab0`
on its position pair with `0x014a1d00` and `0x00a77d30`(vehicle); returns true. **[CONFIRMED — disassembly for the
structure; "location object / store camera block" are HIGH CONFIDENCE readings of the shape.]**

### Relation to the current spec

- **§10.1 — corrects** the writer census: three writer functions, not one (`0x005fa820` sets 1, `0x00820cd0` sets 0). The
  description of `0x00815120`'s two paths is confirmed, with the additions above (early-out when the flag already equals the
  requested mode; the sub-state global `0x022cdf0c` = 8 / 10; the handle-pair bookkeeping). "Is the vehicle-store UI in an
  active mode" remains the right reading (HIGH CONFIDENCE), with the caveat that `0x005fa820`/`0x00820cd0` may set it from
  non-Lua paths (e.g. entering a store on foot / leaving the store), which Team B's single-player run would never see unless a
  script enters a store.
- For Team B now: initial 0 is CONFIRMED and is the right value for the mission run until `store_vehicle_change_mode` is
  called with a non-zero mode from a script and `0x005fa760` can find a store location.

### Next dumps (Q3)

1. `func` depth 1: `0x005fa820`, `0x00820cd0` — the two undocumented writers and their conditions.
2. `xref`: `0x022cdf1c` (where the store location id comes from) and `0x022cdf0c` (the sub-state; values 8/9/10 seen).
3. Force-define the undefined code at `0x00813000`–`0x00813200` (three flag reads there and writes to the handle pair) —
   probably the registered siblings of `0x008133c0` that Ghidra never made functions.

---

## Summary of labels

| item | label |
|---|---|
| `0x024d8534` starts at 0; accessor is a plain load; writer set (`0x0087efe0` installs, `0x0087d8a0`/`0x0087ed70` clear) | CONFIRMED (two unresolved refs at `0x0087c341`/`0x0087c359` OPEN) |
| no session → all three queries false; host/client rows of the table | CONFIRMED |
| `coop_is_active` needs ≥ 2 members and valid other members | CONFIRMED |
| single-player startup creates a session or not | OPEN (hypothesis: one-member host session) |
| tutorial table zero at load, base `0x0151d600`, `+0x08` descriptor pointer, `+0x0c` state 0–4, filled by `0x00715850` | CONFIRMED |
| state 3 meaning / who writes it | OPEN |
| descriptor `+0x24` bits `0x04`/`0x08`/`0x10` | CONFIRMED (tests), HIGH CONFIDENCE (names) |
| 210 names | not in dumps (only `save`, `autosave`); new pointer-table dump needed |
| `0x022cdf08` starts at 0; three writer functions | CONFIRMED |
