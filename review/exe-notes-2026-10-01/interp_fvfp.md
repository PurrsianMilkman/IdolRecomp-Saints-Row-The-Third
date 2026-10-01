# Interpretation of bridge job fvfp (follow-up to dksj): co-op session writers, tutorial descriptor writer and state setter, vehicle-store flag writers

Dumps: `/root/crreish-bus/results/20261001T114716-team-a-fvfp/` (job `team-a/ghidra/jobs/dksj-followup.json`).
Written 2026-10-01 by Team A. Labels: **CONFIRMED — disassembly** only for what these dumps show; otherwise HIGH CONFIDENCE /
HYPOTHESIS / OPEN. Prior context: `interp_dksj.md` (same directory).

Job note: the `key:value` option syntax worked this time (each index.txt reports `depth=1 maxfuncs=25 maxinsn=1500` etc.).
The two addresses `0x0087c341` and `0x0087c359` came back as "no function or instruction here": the `func` mode cannot dump
undefined bytes, so those two references are still unread (see Q1, next dumps).

---

## Headline

**`game_get_is_host` in single player is still OPEN — and the previous lead was wrong.** The function dksj called "the session
installer" (`0x0087efe0`) does not install anything: the register it stores into the singleton `0x024d8534` is a register that
was zeroed at the top of the function and never modified, so it stores **zero**. It is the session subsystem's shutdown routine.
All three resolved writers of the singleton clear it. **No code the disassembler resolved ever stores a non-null session
pointer.** The only remaining references to the singleton are the two in undefined code at `0x0087c341`/`0x0087c359`, so that
is where a session must be installed. Until that code is disassembled and its callers traced, Team B should keep single player
at "no session" (`game_get_is_host` = false, `coop_is_active` = false, `Completion_is_client` = false).

---

## Q1. Co-op session: the three writers, their callers, and the two unresolved references

### Q1.1 `0x0087efe0` is the subsystem shutdown, not an installer — CONFIRMED — disassembly

Body (`coop/func_0x0087efe0.txt`), in order:

1. A register is cleared at `0x0087f030` and from then on serves as the constant zero for every comparison and store in the
   function (it is never written again). The two singleton references, `0x0087f10d` (compare) and `0x0087f11a` (store), use
   that register: the function tests "is the singleton non-null" and then **stores 0**. The decompiler agrees. **The dksj table
   row "compare with a register, then store that register — the only resolved site that can store a non-zero value" was a
   misreading: it stores zero.**
2. Before that, a loop: sleep 10 ms (`0x00dc4440` is a one-line wrapper around the Win32 sleep import), lock the session pool
   (virtual slot `+0xc` of the pool object at `0x024d8544`), walk every pooled session (see Q1.4 for the pool layout), and for
   each one: if the session is *idle* (the predicate in Q1.5) **or** its byte `+0xfd` is set, destroy it with `0x0087ed70`;
   otherwise tick it with `0x0087e9e0` (the per-session update, also used by the frame-time pool walker `0x0087ec60`) and mark
   the pass as "not done". Unlock (slot `+0x10`). Repeat until a pass destroys or skips everything.
3. Then: if the singleton is non-null, flush the deferred-callback queue (`0x00894430`, Q1.6) and store 0 into it.
4. Then: if the pool's "initialised" byte (`0x024d8564`) is set, release every remaining slot (`0x0087d900`: lock, walk the
   active list, release each slot via `0x0087d0e0`, unlock), zero the pool's bookkeeping fields `0x024d8564`..`0x024d8574`
   (initialised byte, capacity, free-list words, active head index, active count, slot storage pointer, link-table pointer),
   and clear the subsystem's "ready" byte `0x024d8540`. If the pool was not initialised it only clears `0x024d8540`.

Caller: exactly one, `0x008676f0` at `0x008676f1` — i.e. the call is the second instruction of a tiny wrapper. **[CONFIRMED —
xref: `coopcallers/xref_0x0087efe0.txt`.]** Who calls `0x008676f0` is not in these dumps **[OPEN]**.

### Q1.2 `0x0087d8a0` is the subsystem initialiser — CONFIRMED — disassembly

Body (`coop/func_0x0087d8a0.txt`): gated on the "ready" byte `0x024d8540` being 0 (so it runs once per init/shutdown cycle).
It takes one argument, an allocator object (virtual slot `+0x1c` is called before and after the pool build; slot `+0x38` is
"allocate(size, alignment, 0, 0)"). It builds the session pool with `0x0087c390(allocator, 2)`:

- slot storage: 2 × 0x2d0 bytes, 8-aligned → pool `+0x2c` (`0x024d8570`);
- link table: 2 × 4 bytes → pool `+0x30` (`0x024d8574`);
- capacity word `+0x22` (`0x024d8566`) = 2; initialised byte `+0x20` (`0x024d8564`) = 1;
- each slot index 0..1 is pushed on the free list at pool `+0x24` via `0x005c5a40` (the same call `0x0087d0e0` makes when it
  releases a slot, so `0x005c5a40` = "return index to the free list" — HIGH CONFIDENCE).

Then: if the singleton is non-null, flush the deferred-callback queue (`0x00894430`) and store 0 into it; store 0 into
`0x024d853c` (a second session-pointer global, see Q1.7); set the ready byte `0x024d8540` = 1.

So **the engine can hold at most two session objects at once, each 0x2d0 (720) bytes** **[CONFIRMED]** — consistent with the
highest session field offsets seen so far (`+0x2cc` timer, `+0x2c8`, `+0x245`). The singleton is cleared here *defensively*,
at init; nothing is installed.

Caller: exactly one, `0x008675c0` at `0x0086763d`. **[CONFIRMED — xref.]** `0x008675c0` and `0x008676f0` are adjacent, which
reads as a module init/shutdown pair **[HIGH CONFIDENCE — by shape]**; their callers are not in these dumps **[OPEN]**.

### Q1.3 `0x0087ed70` destroys one session — CONFIRMED — disassembly

Body (`coop/func_0x0087ed70.txt`), argument = a session pointer S:

1. If S is not idle (Q1.5), call `0x0087df40(S)`, "begin disconnect": if either `+0xf4` (current state) or `+0xf8` (target
   state) is ≥ 2, then: if a pending-transition object `+0x224` exists and its `+0x4` ≠ 1, cancel it (`0x0087b640`); cap the
   target state `+0xf8` to 1; if byte `+0xfe` is set, set `+0x100` = 1; and if there is no pending object and current == target,
   run one step of `0x0087d950`. Returns 2 if nothing was needed, else 1 or the step result.
2. Then wait: while `0x0059fbe0(S)` ("is idle") is false, sleep 10 ms and tick S with `0x0087e9e0`. This is a **synchronous
   join**: the destroy call blocks until the session has wound down.
3. If S is the singleton `0x024d8534` (and non-null): flush the deferred-callback queue and store 0 into the singleton.
   If S equals `0x024d853c` → store 0 there. If S equals `0x024d8538` → store 0 there.
4. Tear S down (`0x0087e670`): for every member node in the list headed at `+0x54` (next at node `+0xb28c`, stop at null or
   back at head) whose byte `+0x00` is 0, call `0x0087e400(node, 3)`; call `0x0101b460`; if the member-slot table `+0x50`
   exists: if the local member's (`+0x5c`) byte `+0x160` is 1, clear it, call `0x0087b960` and `0x0087b7e0(0x1000, local)`;
   then `0x0087aa90(S)`, free the slot table (`0x008798e0`), `+0x50` = 0. Finally `+0xf4` = `+0xf8` = 0, remove the local
   member (`0x0087e400(local, 3)`), and `+0x5c` = 0.
5. Lock the pool, release S's slot with `0x0087d0e0(pool, S)` (unlink the slot index from the circular active list, fix the head
   if S was the head, decrement the active count, push the index on the free list; returns the next active session or null),
   and unlock (tail call).

The teardown order — every other member first, the local member last, and `+0x5c` zeroed at the very end — is further support
that `+0x5c` is the local member record **[HIGH CONFIDENCE, as in dksj]**.

Callers (`coopcallers/xref_0x0087ed70.txt`): `0x0087eea0` at `0x0087ef90` (a pool-walking function that also reads the ready
byte `0x024d8540` and calls the "begin disconnect" `0x0087df40` at `0x0087efa2` — reads as the public "leave/end the session"
entry **[HYPOTHESIS]**), `0x0087efe0` (the shutdown, Q1.1), and **undefined code at `0x0088b862`** (which also calls
`0x0087df40` at `0x0088b82c`) **[OPEN]**.

### Q1.4 The session pool (object at `0x024d8544`) — CONFIRMED — disassembly

| field | global | meaning | evidence |
|---|---|---|---|
| `+0x00` | `0x024d8544` | vtable; slot `+0xc` lock, `+0x10` unlock (called around every pool walk) | `0x0087f028`, `0x0087f103`, `0x0087ee13`, `0x0087ee2e` |
| `+0x20` | `0x024d8564` | initialised byte | `0x0087c3da`, `0x0087f139` |
| `+0x22` | `0x024d8566` | capacity (2) | `0x0087c3d6` |
| `+0x24`..`+0x27` | `0x024d8568`/`0x024d856a` | free-list words, managed by `0x005c5a40` | `0x0087c3e3`, `0x0087d162` |
| `+0x28` | `0x024d856c` | index of the first active slot | `0x0087f048`, `0x0087d15d` |
| `+0x2a` | `0x024d856e` | active count | `0x0087f03f`, `0x0087d16a` |
| `+0x2c` | `0x024d8570` | slot storage, 0x2d0 bytes per slot | `0x0087f054`, `0x0087d0ef` |
| `+0x30` | `0x024d8574` | link table, 4 bytes per slot: word `+0` previous index, word `+2` next index (circular) | `0x0087d140`..`0x0087d150` |

Session pointer ↔ slot index: `(ptr − storage) / 0x2d0`. Walkers stop when the next index equals the head index. Eighteen
functions read these fields (`0x00708060`, `0x00869240`, `0x00871f20`, `0x0087cac0`, `0x0087cbf0`, `0x0087cce0`, `0x0087cd90`,
`0x0087ce40`, `0x0087cf60`, `0x0087e210`, `0x0087ec60`, `0x0087eea0`, …): the "for every session" family. The writes to the same
fields from undefined code at `0x00ffbda5`..`0x00ffbdd2` mirror the shutdown's zeroing sequence and are unexplained
**[OPEN — probably an unwind/cleanup copy]**.

### Q1.5 Session connection state — CONFIRMED — disassembly

`0x0059fbe0(S)` returns true iff `+0xf4` < 2 **and** `+0x224` == 0 **and** `+0xf4` == `+0xf8`. The shutdown loop and the
destroy path both use exactly this predicate as "the session is idle and can be released"; the teardown sets `+0xf4` and
`+0xf8` to 0. So `+0xf4`/`+0xf8` are the current/target connection state (0 after teardown; values ≥ 2 mean connected or
connecting, since "begin disconnect" caps the target at 1), `+0x224` is a pending transition object, and `+0xfd` is a byte that
forces destruction in the shutdown walk. **Consequence for §3.1:** the "client-side gate" of `coop_is_active`
(`+0xf4` ≥ 2 OR `+0x224` ≠ 0 OR `+0xf4` ≠ `+0xf8`, AND byte `+0xfd` == 0) is precisely "the session is **not** idle, and not
flagged for destruction". **[CONFIRMED — the gate's three terms are the negation of `0x0059fbe0`'s three terms.]**

### Q1.6 `0x00894430` — the deferred-callback flush — CONFIRMED — disassembly

Walks a global doubly-linked list headed at `0x024e4d80` (next at node `+0x20`, previous at `+0x24`), unlinking each node, calling
its callback (`+0x8`) with the two stored arguments (`+0x10`, `+0x14`) if non-null, and returning the node to the allocator
`0x01304ed0` via `0x00ad0f30`. It runs on every path that clears the singleton (init, destroy, shutdown) and from `0x00703f50`.
HIGH CONFIDENCE: pending session-related callbacks are drained before the session pointer goes away.

### Q1.7 Three session-pointer globals

- `0x024d8534` — the singleton `game_get_is_host`/`coop_is_active`/`Completion_is_client` read (accessor `0x0087ba20`).
- `0x024d8538` — accessor `0x0087ba10`, setter `0x0087ba50`; cleared by destroy when it equals the destroyed session.
- `0x024d853c` — accessor `0x0087ba30`, setter `0x0087ba40`; cleared by init and by destroy.

**[CONFIRMED — xref blocks in `coop/func_0x0087efe0.txt`.]** The two secondary pointers have ordinary setters that store a
register; the primary one has **none that the disassembler resolved**. With a two-slot pool, "current session" plus one other
(a lobby/pending or previous session) is the natural reading **[HYPOTHESIS]**.

### Q1.8 The references at `0x0087c341` and `0x0087c359` — OPEN, but now decisive

The dump reports "no function or instruction here" for both. They are 0x18 bytes apart, both inside `.text`, in the undefined gap
before the pool-build function `0x0087c390`. The resolved writers show the engine's standard pattern "compare the singleton,
then store" 0xd–0xf bytes apart (`0x0087d8cd`/`0x0087d8dc`, `0x0087f10d`/`0x0087f11a`); a compare-then-store of a *new* session
pointer at `0x0087c341`/`0x0087c359` fits the same pattern. By elimination (ten references in total, eight read; the other two
resolved writes store zero), **the non-null store must be in this undefined code** **[HIGH CONFIDENCE — by elimination; the
only alternative is a write through a pointer derived from a neighbouring global, for which there is no evidence]**. A plausible
reason the disassembler never defined it: the pool object's vtable lives in zero-fill `.data` (`0x024d8544`), so a function
reached only through that vtable has no direct code reference **[HYPOTHESIS]**.

### Q1.9 Single-player answer

- CONFIRMED: the executable starts with no session; init (`0x0087d8a0`) does not create one; the pool holds at most two; the
  three resolved writers only clear.
- OPEN: whether starting/loading a single-player game runs the undefined installer. Nothing in these dumps touches that path.
  Two weak pointers from the store dumps (Q3) show engine code that treats "no session" as a normal running state (not just a
  pre-init state): `0x005f74d0` picks a per-player slot by "session exists and local is host → slot A, otherwise slot B", and
  `0x009e1c40` registers a replicated name only when a session exists and local is host. Both are consistent with single player
  running without a session, but they are also consistent with defensive coding. **Not evidence either way.**
- Team B guidance unchanged: no session in single player (`game_get_is_host` false) until the installer is read.

### Next dumps (Q1)

1. **Add a range-disassembly mode to `CrreishDump.java`** (`disasm <start> <end>` that forces disassembly and prints the listing,
   or `mkfunc <addr>`), then dump `0x0087ba70`–`0x0087c390` (the gap containing `0x0087c341`/`0x0087c359`) and
   `0x0088b7c0`–`0x0088b8a0` (the undefined caller of destroy/disconnect).
2. `xref`: `0x008675c0`, `0x008676f0` (the init/shutdown pair — who runs them, and whether the game's new-game/load path is nearby),
   `0x0087ba40`, `0x0087ba50` (the setters of the two secondary pointers; their callers are probably next to the primary installer).
3. `func` depth 1: `0x0087eea0` (likely "leave session"), `0x0087ec60` (the frame-time pool tick).
4. Once the installer is found: `xref` of it, which answers the single-player question directly.

---

## Q2. Tutorial table

### Q2.1 Descriptor writer `0x00715850` — CONFIRMED — disassembly

Arguments: an index I and a pointer to a 40-byte source record. Body (`tutorial/func_0x00715850.txt`): copies the 40 bytes
(five 8-byte moves) into slot I of a run-time descriptor array at `0x0151f388` (40-byte stride, zero-fill `.data`), stores the
slot's address into table entry I `+0x08`, sets the entry state `+0x0c` to **0**, then, if I ≥ 189 (`0xbd`), overwrites it with
**1**. Nothing else. The 40-byte record is the "descriptor" dksj described through its reads (`+0x00` default widget source,
`+0x14` builder pointer, `+0x20` default duration, `+0x24` flag bits): `+0x24` is its last dword, so the dksj layout fits inside
the 40 bytes exactly.

**When it runs:** it has 210 call sites, all in **one** function, `0x007178c0`, laid out in a straight line from `0x00717909` to
`0x0071acad` (`tutxref/xref_0x00715850.txt`: "total uses 210 in 1 functions"). So the whole table is filled in one pass by
`0x007178c0` — the tutorial registration routine. The same function also writes the 7-entry id table `0x0152145c` (at
`0x0071accc`, after the last fill call) that the generic setter uses (Q2.2). **[CONFIRMED — xref.]** With ~60 bytes between
consecutive call sites, each site has room only to build a record from immediates and call: the descriptors are compiled-in
constants, not loaded from a data file **[HIGH CONFIDENCE — by shape; the record contents are not in these dumps]**. When
`0x007178c0` itself runs (process start? level load?) is **OPEN** — its callers were not requested.

**Consequence:** after the fill, entries 0–188 are in state 0 and **entries 189–209 are already in state 1 ("armed")**. This
matches the dispatcher rule from dksj that entries ≥ 189 bypass the state gate and get state 1 via `0x00716170`. `tutorial_advance`
is still false for every name after the fill (no entry is in state 3).

Side note: the "(no function)" writes into `0x0151f388`..`0x0151f3a8` at `0x00ff7b40`..`0x00ff7b5e` are register-relative
stores whose base the disassembler guessed; they are almost certainly unrelated **[OPEN — likely spurious]**.

### Q2.2 Generic state setter `0x007163e0` — CONFIRMED — disassembly

Arguments: a count N and a pointer to N pairs of dwords (id, value). For each pair: resolve the id with `0x007177c0`, which
scans the 7-dword run-time table at `0x0152145c` and returns **189 + position** on a match, −1 otherwise; on a match, store
`value` into that entry's state `+0x0c` **unvalidated**. So this setter can only touch **entries 189–195** (names, per the kyoi
list in §26.28: `dlc1_act_genki_escort_into`, `dlc1_act_panda_blazing`, `dlc1_act_ball_mayhem`, `dlc2_near_crash`,
`dlc2_alien_aircraft`, `dlc1_act_ball_mayhem_shockwave`, `airplane_control_pc`), and the values it writes come from its caller.

Caller: exactly one, `0x00b9ae60` at `0x00b9b022` (`tutxref/xref_0x007163e0.txt`) — not dumped. The pair-list shape (id →
state) reads like a saved/restored state block for the DLC-range tutorials **[HYPOTHESIS]**. Whether it ever writes 3 is
**OPEN**.

### Q2.3 The code at `0x00717363` — CONFIRMED as a fragment; its meaning OPEN

What the dump could define there is only a **function tail**: store a register value into entry[EDI]'s state `+0x0c`; call
`0x00d9e140` — a timer helper that writes "time base `0x01320d94` + argument" (wrapped into 0..1,800,000,000) through a pointer
in ECX, i.e. "arm a deadline"; then a stack-cookie check and a return that pops a 0xac-byte frame. The register's value is set
by code *before* `0x00717363` that the dump does not contain, so **the value written is unknown**. The surrounding undefined
region (`0x00717240`–`0x0071738f`) also holds the state-4 write at `0x00717311`, the descriptor-pointer read at `0x0071724e` and
the entry-base computation at `0x00717259` (`tutxref/xref_0x0151d600.txt`). The last defined function before it is
`0x00717020` (which indexes the descriptor array `0x0151f388` at `0x00717093`), the next is `0x00717390`; so this region is the
truncated remainder of `0x00717020` **[HIGH CONFIDENCE — by layout; the frame size and cookie check fit a large function]**.
"Set a state, then arm a timer" is the shape of a transition into a timed waiting state **[HYPOTHESIS]**.

### Q2.4 What state 3 means, and how an entry reaches it — still OPEN

Writer census after this job (all of `0x0151d60c`'s 28 references):

| value | writers | status |
|---|---|---|
| 0 / 1 | `0x00715850` (fill), `0x00716140` (disarm), `0x00716170`, `0x007161a0`, `0x007169e0`, `0x00716940` (reset) | CONFIRMED |
| 2 | `0x00715bf0` (queued simple prompt) | CONFIRMED |
| 4 | `0x007169e0`, `0x00716ed0`, `0x00717390`, undefined `0x00717311` | CONFIRMED (value) |
| any | `0x007163e0` — entries 189–195 only, values from `0x00b9ae60` | CONFIRMED (range); value OPEN |
| any | undefined tail `0x00717363` — register value, followed by arming a timer | value OPEN |

**No defined function writes the literal 3.** For entries 0–188 the only candidate is the undefined code before `0x00717363`
(inside what is probably `0x00717020`). For entries 189–195, `0x007163e0` is a second candidate. Readers of 3: `0x007162a0`
(a query) and `0x00716440` (`tutorial_advance`'s core). The dksj HYPOTHESIS — 3 = "displayed, waiting for the player to
complete the step" — gains a little support from the timer being armed right after the store at `0x00717363`, but **remains a
hypothesis**. Do not implement a route to 3 yet; `tutorial_advance` returning false everywhere is still the correct
single-player behaviour until the next dump.

### Next dumps (Q2)

1. Range-disassemble `0x00717240`–`0x00717390` (or re-dump `0x00717020` after forcing its body to extend to `0x00717386`) — the
   state-3 question lives there. Also `func 0x00717020` as it is, to see what the update loop does before the gap.
2. `xref 0x007178c0` (when the table is filled) and `func 0x00b9ae60` (what feeds the DLC-range setter).
3. `func 0x007162a0` (the other reader of 3) and `xref 0x007162a0` — whoever asks "is it in state 3" names the state.

---

## Q3. Vehicle-store flag `0x022cdf08`: the two non-Lua writers

### Q3.1 `0x005fa820` writes 1 — "enter the vehicle store at this object" — CONFIRMED — disassembly

Argument: a pointer to a 64-bit object handle. Body (`store/func_0x005fa820.txt`):

1. If the handle is zero → nothing. Resolve it through the handle table (`0x00458230` on `0x024433a8`/`0x031d152c`); require the
   object's byte `+0x33` bit `0x10` clear, its class row (`0x02cc9900[byte +0x34]`) byte **`+9` bit `0x10`** set, and the
   liveness check `0x00853b10` false; otherwise nothing. (The `+9`/`0x10` class bit is the same one `0x005f8410` uses to find
   "the store object" among a building's children — so it marks the vehicle-store *location* class, whereas the Lua writer
   `0x00815120` tested `+6`/`0x80`, the vehicle class. HIGH CONFIDENCE.)
2. `0x005f9dc0`: if the store state `0x014a1ce4` is 3, finish the previous interaction — resolve the store's vehicle handle
   `0x014a1dc8/cc` (via `0x004dcf00`) and, if the location object's byte `+0x7a` < 3 and `0x005f9230` succeeds, run
   `0x005f92a0`/`0x005f6ff0`/`0x005f7c90` on it — then set the state to 0; if the state was anything else, just set it to 0.
3. Fill the store-target block: `0x014a1dc0` = the per-type pointer `0x012ece20[byte +0x7a]`; `0x014a1dc4` = the location
   object; `0x014a1dc8/cc` = the null-handle constant pair from `.rdata` `0x01127a80/84` (both static 0). (Correction to dksj's
   "two zero floats" for these two: they are the store's **vehicle handle pair**, reset to the null handle; the two floats are
   `0x014a1dd0/d4`, below.)
4. `0x009e1c40(local player, 0, 0)`: clears the player's last-vehicle handle pair (`+0x2880/+0x2884`) and, unless `0x008addb0`
   says the local machine should not, sends an opcode `0x46` record carrying the player and the (null) vehicle, looked up through
   the named fields `"player"` and `"player_last_vehicle_handle"`; when a session exists and local is host it also releases the
   replicated name `"player2"` (`0x00bbb5d0`) slot at player `+0x2878`. **[CONFIRMED — disassembly of `0x009e1c40`; "last
   vehicle" from the literal field name.]**
5. **Flag `0x022cdf08` = 1** — unconditionally at this point.
6. `0x0080cc90` on the store UI object `0x022cde10` (result ignored): bail if the object's byte `+0xcb` is set and
   `0x009b9160(local player)` is true; else `0x00d2f560`, then try three ways of reaching UI document `0x28` on the UI manager
   `0x012fced8` (`0x007b3ba0`, `0x007b3c50`, `0x007b4750`), then `0x007b5fe0(object)`, falling back to `0x007b4790(manager, 0)`.
7. Zero the two floats `0x014a1dd0/d4` and the byte `0x014a1de4`; store state `0x014a1ce4` = **4**.

This is the handle-driven sibling of `0x005fa760` (dksj: location-id-driven, called from the Lua writer), ending in the same
state (4) with the same block filled. **Callers:** `0x0080e2f0` at `0x0080e339` (not dumped), and undefined code at `0x005fb406`
and `0x005fbdd5` (`store/func_0x005fa820.txt` header). The undefined sites sit in the store-logic region and the dksj census also
shows undefined writes of 4 into `0x014a1ce4` at `0x005fb3e2`/`0x005fb40e`, i.e. right next to one of them. **When the game
takes this path (walk-in trigger? menu?) is OPEN** until those callers are read.

### Q3.2 `0x00820cd0` writes 0 — "leave the vehicle store with the current vehicle" — CONFIRMED — disassembly

Argument: an object B with a child list (`+0x1c` head, `+0x20` next, circular) — a building/interior **[HYPOTHESIS]**. Body
(`store/func_0x00820cd0.txt`):

1. Read the local player's vehicle handle pair (`+0x16c0/+0x16c4`). If present, resolve it; require byte `+0x33` bit `0x10`
   clear, class row byte `+6` bit `0x80` (vehicle), liveness check false → vehicle V. If V passes and `0x00a367f0(V)`
   (a one-jump wrapper to `0x00a36750`, not dumped) returns true → **return false immediately, flag untouched**. If the handle
   is absent or any check fails, V = null and the function continues.
2. `0x0080efc0(B)` (a wide-string copy helper; sets a UI string — HYPOTHESIS).
3. If B is non-null: L = `0x005f8410(B)`, the first child whose class row has `+9` bit `0x10` (the store-location class).
   `0x005fc440(L, V)` must return true: L non-null **and** L == the current store location `0x014a1dc4` **and** V's handle pair
   (`+0x8/+0xc`) == the store's vehicle pair `0x014a1dc8/cc` **and** `0x005fbae0(V, buffer, 0x80)` succeeds — on that last
   failure it first shows a message through `0x007ff430(0x014a2940, buffer, 0, string)`. If false → **return false, flag
   untouched.** If true: `0x022cf8d4` = L and `0x005f74d0(L)`: copy L's handle pair into slot `0x014a1d30` when a session exists
   and local is host, otherwise into `0x014a1d38`.
4. `0x022cf8dc` = B; `0x00922bf0`; tell four objects (`0x025e8740`, `0x025e9000`, `0x025e98c0`, `0x025e9460`) the value 1 via
   `0x008ecda0` (not dumped — the same call `0x008208a0` makes four times; HYPOTHESIS: re-enable four gameplay systems on
   leaving the store); `0x00820480(V)` — the same post-store routine the Lua close path runs: enumerate up to 0x80 nearby
   handles (`0x0075d4b0`, filter 0x2c), and for each live object whose class row has `+7` bit `0x4`, call `0x00853ea0(obj,0,0)`;
   then record V's handle pair into `0x022cf948/4c`, byte `0x022cf934` = bit 5 of V's byte `+0x1d7a`, `0x022cf935` =
   `0x00ab6cd0(V)`.
5. `0x0080cc90` on `0x022cde10` (the same UI call as Q3.1 step 6) — **only if it returns true: flag `0x022cdf08` = 0.** Return
   that result.

**Caller:** exactly one, in undefined code at `0x005fc207` (`store/func_0x00820cd0.txt` header). That region also writes the
store vehicle handle `0x014a1dc8` (`0x005fc1f0`, from the dksj census). So the game-level trigger is **OPEN**; the function's
own preconditions (player sitting in the store's vehicle, at the store's location) are CONFIRMED.

### Q3.3 Flag semantics after this job

- 1 is written by `0x00815120` (Lua, mode ≠ 0, after `0x005fa760` succeeds) and by `0x005fa820` (handle-driven entry) — both
  right after the store-target block is filled and before the state becomes 4.
- 0 is written by `0x00815120` (Lua, mode 0) and by `0x00820cd0` (vehicle-driven exit, only when the UI call succeeds).
- The store state `0x014a1ce4` (values 0–4; 3 = "interaction in progress, to be finished by `0x005f9dc0`"; 4 = "store target
  set"; writers in the dksj census plus `0x005f9e40` = 3, `0x005fb4c0` = 1/2/0, `0x005fbb60` = 1) is a separate machine from
  the flag.
- "Vehicle-store UI in an active mode" (dksj HIGH CONFIDENCE) stands; both non-Lua writers bracket the same UI call
  `0x0080cc90` on `0x022cde10`.
- For Team B's single-player mission run: initial 0 is still right; the non-Lua writers need a store object in the world and
  either the `0x0080e2f0`/undefined entry path or a player sitting in the store's vehicle, none of which a script-only run
  produces.

### Next dumps (Q3)

1. `func 0x0080e2f0` + `xref 0x0080e2f0` (the one defined caller of the enter path).
2. Range-disassemble `0x005fb380`–`0x005fb420`, `0x005fbd80`–`0x005fbe00` and `0x005fc1c0`–`0x005fc220` (the undefined callers
   of both writers, next to the undefined `0x014a1ce4`/`0x014a1dc8` writes).
3. `func 0x00a36750` (the vehicle test that aborts the exit path), `func 0x0080cc90` depth 2 (what document `0x28` is).

---

## Spec changes

Exact sentences, and what they become. Labels as derived above.

### §26.28

1. **"Of the references the disassembler resolved, one function can install a session (`0x0087efe0`) and two clear it
   (`0x0087d8a0`, `0x0087ed70`) [CONFIRMED — disassembly for the reference set]; two more references at `0x0087c341`/`0x0087c359`
   sit in undefined code and may be further writers [OPEN]."**
   → "All three resolved writer functions store **zero**: `0x0087d8a0` is the session subsystem's initialiser (it builds a
   two-slot pool of 0x2d0-byte session objects and clears the singleton), `0x0087ed70` destroys one session (waiting for it to
   go idle, then clearing the singleton if it pointed at that session), and `0x0087efe0` is the subsystem shutdown (it destroys
   every pooled session, flushes the deferred-callback queue `0x024e4d80`, clears the singleton and frees the pool).
   **[CONFIRMED — disassembly, job `20261001T114716-team-a-fvfp`.]** No resolved code installs a session. The only other
   references, at `0x0087c341`/`0x0087c359`, are in code the disassembler never defined and are, by elimination, where a
   session pointer is stored **[HIGH CONFIDENCE — by elimination; OPEN until that code is disassembled]**."
2. **"**What single-player startup leaves it as: OPEN.** The executable starts at 0, but whether starting a single-player game runs
   the installer `0x0087efe0` and creates a one-member session … Follow-up job `20261001T114716-team-a-fvfp` dumps the installer
   `0x0087efe0`. Until it lands, a host should start with no session."**
   → "**What single-player startup leaves it as: OPEN.** Job fvfp showed that `0x0087efe0` is the shutdown, not the installer; the
   installer is in undefined code at `0x0087c341`/`0x0087c359`, so whether a single-player start creates a one-member host
   session cannot be read yet. The HYPOTHESIS (single player keeps a one-member host session) stays a hypothesis. A host
   starts with no session: `game_get_is_host` false, `coop_is_active` false, `Completion_is_client` false."
3. **Add after the table:** "A session's connection state is the pair `+0xf4` (current) / `+0xf8` (target) plus a pending
   transition object at `+0x224`; `0x0059fbe0` reports the session *idle* when `+0xf4` < 2, `+0x224` is null and `+0xf4` ==
   `+0xf8`; teardown zeroes both and the local member pointer `+0x5c`. The engine holds at most two session objects at once
   (pool at `0x024d8544`, capacity 2). **[CONFIRMED — disassembly, job fvfp.]**"
4. **"The 210-entry table at `0x0151d600` (36-byte entries, §6.19) is zero at load, so every entry starts in state 0 and
   `tutorial_advance` returns false on a fresh process for every name. [CONFIRMED — disassembly.] The entries' descriptor
   pointers (`+0x08`) are filled by `0x00715850`; when that runs is OPEN."**
   → "The 210-entry table at `0x0151d600` (36-byte entries, §6.19) is zero at load. It is filled in one pass by the registration
   routine `0x007178c0`, which calls `0x00715850` once per entry (210 straight-line call sites); each call copies a 40-byte
   descriptor into a run-time descriptor array at `0x0151f388`, points the entry's `+0x08` at it, and sets the state to 0 for
   entries 0–188 and to **1** for entries 189–209. **[CONFIRMED — disassembly and xref, job fvfp.]** So after the fill, entries
   189–209 are already armed; no entry is in state 3, so `tutorial_advance` returns false for every name on a fresh process.
   When `0x007178c0` runs is OPEN."
5. **Review status line, OPEN items:** "whether single-player startup installs a session (job `20261001T114716-team-a-fvfp`)" →
   "whether single-player startup installs a session (the installer is undefined code at `0x0087c341`; needs a
   range-disassembly dump)"; drop "the two undefined references at `0x0087c341`/`0x0087c359`" as a separate item (it is the same
   item); add "CONFIRMED — the three resolved singleton writers all clear it; the session pool holds two 0x2d0-byte objects;
   tutorial table filled by `0x007178c0`, entries 189–209 start in state 1".

### §8.27

6. **Append to the "[Re-derived 2026-10-01 …]" note:** "Job fvfp: the function dksj took for the session installer (`0x0087efe0`)
   stores zero and is the subsystem shutdown; no resolved code stores a non-null session pointer (the installer is undefined
   code at `0x0087c341`/`0x0087c359`). Whether single player has a session, and so whether this function is true in single
   player, remains OPEN; implement false. Teardown (`0x0087e670`) zeroes `+0x5c` before the singleton is cleared, so during a
   session's wind-down this function is false. **[CONFIRMED — disassembly.]**"

### §10.1

7. **"[corrected 2026-10-01, job `20261001T020213-team-a-dksj`: three writer functions, not one — `0x00815120` (below),
   `0x005fa820` (writes 1) and `0x00820cd0` (writes 0); the conditions of the last two are OPEN, they were not dumped]"**
   → "[corrected …: three writer functions, not one — `0x00815120` (below), `0x005fa820` (writes 1) and `0x00820cd0` (writes 0).
   Job fvfp read both: `0x005fa820` is the handle-driven store entry — given a live object of the store-location class, it
   finishes any interaction left in store state 3, fills the store-target block (`0x014a1dc0`..`0x014a1dcc`, vehicle handle reset
   to null), clears and replicates the player's last-vehicle handle (opcode `0x46`), **sets the flag to 1 unconditionally**,
   runs the store UI call `0x0080cc90` on `0x022cde10`, and sets store state `0x014a1ce4` = 4; `0x00820cd0` is the vehicle-driven
   store exit — it requires the player's current vehicle to be the store's vehicle (`0x014a1dc8/cc`) at the current store
   location (`0x014a1dc4`) and `0x005fbae0` to accept it, records the location and vehicle (`0x022cf8d4`, `0x022cf8dc`,
   `0x022cf948/4c`), runs the post-store cleanup `0x00820480` and the same UI call, and **sets the flag to 0 only if that UI call
   succeeds**. **[CONFIRMED — disassembly.]** Their game-level triggers are OPEN: `0x005fa820` is called from `0x0080e2f0` and
   two undefined sites (`0x005fb406`, `0x005fbdd5`); `0x00820cd0` from one undefined site (`0x005fc207`).]"
8. **"The other two writers may set the flag from paths that do not start in Lua (for example, entering or leaving a store)
   [HYPOTHESIS]."**
   → "The other two writers are the non-Lua entry and exit paths described above; whether their callers are Lua-reachable is OPEN
   (one defined caller, `0x0080e2f0`, not yet read)."
9. **Review status "OPEN — when `0x005fa820` and `0x00820cd0` run."** → "CONFIRMED — the bodies and preconditions of `0x005fa820`
   and `0x00820cd0` (job fvfp). OPEN — their callers (`0x0080e2f0`; undefined code at `0x005fb406`, `0x005fbdd5`, `0x005fc207`)."

### §10.4

10. **"On a fresh process every entry is in state 0, so it returns false for every name until something puts an entry into
    state 3 (§26.28)."**
    → "On a fresh process every entry is in state 0; after the registration routine `0x007178c0` fills the table, entries 0–188
    are in state 0 and entries 189–209 in state 1; either way no entry is in state 3, so it returns false for every name until
    something writes 3 (§26.28)."
11. **Review status "OPEN — what state 3 means and who sets it (§6.19)."**
    → "OPEN — what state 3 means and who sets it. Narrowed by job fvfp: no defined function writes the literal 3; the generic
    setter `0x007163e0` reaches only entries 189–195 (through the 7-id table `0x0152145c`) with values supplied by its single
    caller `0x00b9ae60`; for entries 0–188 the only candidate is the undefined code before the function tail at `0x00717363`
    (which stores a register value and then arms a timer), inside the undefined region `0x00717240`–`0x0071738f` that is probably
    the rest of `0x00717020`. HYPOTHESIS — 3 = displayed and waiting for the player."

### Also touched (not asked, but affected)

- **§3.1 `coop_is_active`:** the "client-side gate" is exactly "the session is not idle (`0x0059fbe0` false) and byte `+0xfd`
  is clear" — add this one sentence **[CONFIRMED]**.
- **§6.19 `tutorial_start`:** "the table is zero at load and filled by `0x00715850`" → "filled by `0x007178c0` through 210
  calls to `0x00715850`, which also writes the 7-id table `0x0152145c`; entries 189–209 start in state 1".

---

## Summary of labels

| item | label |
|---|---|
| `0x0087efe0` stores zero; it is the subsystem shutdown (join-and-destroy all sessions, flush callbacks, free pool) | CONFIRMED |
| `0x0087d8a0` is the subsystem init: two-slot pool of 0x2d0-byte sessions; clears singleton and `0x024d853c`; sets ready byte | CONFIRMED |
| `0x0087ed70` destroys one session synchronously; clears whichever of the three session globals pointed at it | CONFIRMED |
| idle predicate `0x0059fbe0` = (`+0xf4` < 2, `+0x224` = 0, `+0xf4` = `+0xf8`); §3.1 client gate is its negation | CONFIRMED |
| no resolved code installs a session; installer is at `0x0087c341`/`0x0087c359` (undefined) | HIGH CONFIDENCE (by elimination) |
| single-player startup installs a session | OPEN (hypothesis unchanged, untested) |
| `0x00715850` copies a 40-byte descriptor to `0x0151f388`, sets state 0 / 1 (index ≥ 189); 210 callers all in `0x007178c0` | CONFIRMED |
| descriptors are compiled-in constants | HIGH CONFIDENCE |
| `0x007163e0` writes caller-supplied values into entries 189–195 only; single caller `0x00b9ae60` | CONFIRMED (range), OPEN (values) |
| `0x00717363` is a function tail: register → state, then arm a timer; value unknown | CONFIRMED (fragment), OPEN (value) |
| state 3 meaning / writer | OPEN |
| `0x005fa820` = handle-driven store entry, flag = 1 unconditionally | CONFIRMED |
| `0x00820cd0` = vehicle-driven store exit, flag = 0 only when the UI call succeeds | CONFIRMED |
| game-level triggers of both store writers | OPEN (callers undefined / unread) |
