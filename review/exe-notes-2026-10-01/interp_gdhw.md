# Interpretation of bridge job gdhw (follow-up to fvfp): the co-op session installer, session init/shutdown, tutorial registration and state setter, store-flag call sites

Dumps: `/root/crreish-bus/results/20261001T121532-team-a-gdhw/` (job `team-a/ghidra/jobs/coop-installer.json`).
Written 2026-10-01 by Team A. Labels: **CONFIRMED — disassembly** only for what these dumps show; otherwise HIGH CONFIDENCE /
HYPOTHESIS / OPEN. Prior context: `interp_dksj.md`, `interp_fvfp.md` (same directory).

Job note: the new `range` mode worked. All five ranges came back as instruction listings with resolved call targets and
Ghidra's own function labels where a range crossed into defined code. The `xref` and `func` modes ran with the `key:value`
options as intended. One earlier Team A artefact was also used for cross-checking: a 2026-10-01 PC-side listing of the same
bytes at `0x0087c340` (`results/20261001T021658-team-a-xqjl/_files/teama-pc/tools/save_x_25.txt`, `save_x_25b.txt`,
`save_x_27.txt`), which agrees instruction for instruction with the new range dump and adds a decompiler rendering.

---

## Headline

**`game_get_is_host` in single player is still OPEN, but the installer is now read.** The two unresolved references at
`0x0087c341`/`0x0087c359` are the load and store of a 70-byte function at **`0x0087c340`** that takes one session pointer and
installs it into the singleton `0x024d8534`. It is the only code in the executable that can store a non-null session.
**[CONFIRMED — disassembly.]** It has **no caller in defined code** (the earlier PC listing's reference search for `0x0087c340`
is empty, and none of this job's dumps calls it), so its callers are also in undefined code; *when* it runs — co-op host, co-op
join, or also a plain single-player start — cannot be read yet. Team B guidance is unchanged: single player runs with no
session (`game_get_is_host` false, `coop_is_active` false, `Completion_is_client` false) until the installer's callers are
dumped (next-dump list in Q1.6).

Two other findings overturn earlier working assumptions:

- The undefined region `0x00717240`–`0x0071738f` writes **4** at both of its state stores (the one fvfp called "a register of
  unknown value" is the immediate 4), and it is **not** the rest of `0x00717020` (that function ends with a return at
  `0x0071716a`). So the region is not a route to tutorial state 3. **[CONFIRMED — disassembly.]** The generic setter
  `0x007163e0` is fed from a loaded **save-game block** (`0x00b9ae60`, version-gated section 5), so entries 189–195 are
  save-persisted; whether a saved value can be 3 depends on the (undumped) save side. Who writes 3 remains **OPEN**; two
  unread references to the state field in undefined code at `0x00716385`/`0x007163b5` are the last direct leads.
- The three store-flag call sites are function **tails**; their heads were outside the requested ranges, so the game-level
  triggers stay OPEN. What the tails show is below (Q3).

---

## Q1. Session install, init and shutdown

### Q1.1 `0x0087c340` is the session installer — CONFIRMED — disassembly

Listing (`range/range_0x0087ba70-0x0087c390.txt`, `0x0087c340`–`0x0087c385`), argument = one session pointer S on the stack,
caller cleans up (plain return, no immediate):

1. Load the singleton. If S equals it, return (installing the current session again is a no-op).
2. If the singleton is non-null, call `0x00894430` — the deferred-callback flush fvfp Q1.6 described (no argument; it drains the
   global list at `0x024e4d80`). The old session is **not** destroyed here; only its pending callbacks are run.
3. Store S into `0x024d8534` (the `0x0087c359` reference). If S is null, return — so the installer doubles as a "clear" when
   called with 0, and it is the only clear that also runs for a *replacement*.
4. If S is non-null, compare S`+0x5c` with S`+0x58` (the same local-member / host-member pair `game_get_is_host` tests):
   equal → tail-call `0x00905ec0` with the literal 4; different → tail-call `0x00905ea0` with the literal 4. The earlier PC
   decompile renders these two as setting and clearing an entry at `0x02609398`; by shape they are a "set flag N" /
   "clear flag N" pair on a global flag table, and flag 4 is "the local machine is the host" **[HYPOTHESIS — the two callees
   were not dumped]**.

So installing a session publishes the host/client answer once, at install time, to some other system through flag 4; the
three Lua queries do not read that flag — they re-derive the answer from the session object on every call (dksj).

**Who calls it: OPEN.** The PC listing's "references to `0x0087c340`" section is empty, and the `callers` and `func` dumps of this
job contain no call to it. Every reference Ghidra holds for the singleton is now explained: one accessor, one comparer, the
installer's load/store, and the three zero-stores (init, destroy, shutdown). Because the installer is undefined code with no
defined caller, its callers must themselves be undefined code (or reach it through a pointer computed at run time). The best
candidates are the undefined blocks that already handle session lifetime: `0x0088b4c0`–`0x0088b5a0` (reads the provider
pointer `0x01493978` at `0x0088b529` and calls the subsystem shutdown at `0x0088b560`), `0x0088b7c0`–`0x0088b8a0` (calls
"begin disconnect" `0x0087df40` at `0x0088b82c` and destroy `0x0087ed70` at `0x0088b862`, per fvfp), and `0x0088cac0`–`0x0088cb60`
(three provider-pointer reads in undefined code). **[HIGH CONFIDENCE as the place to look; nothing in these dumps shows the
call.]**

### Q1.2 What else lives in `0x0087ba70`–`0x0087c390` — CONFIRMED — disassembly (bodies), HIGH CONFIDENCE (roles)

The whole gap is a run of small, never-defined functions of the session class. They matter because they show session fields
that the Lua queries and the teardown use, and because they strengthen the field readings from dksj/fvfp:

| start | shape | what it does |
|---|---|---|
| `0x0087ba70`, `0x0087ba76` | two one-instruction stubs | return 1; return 0 |
| `0x0087ba80` | thiscall, 1 arg | deleting-destructor shape: if the argument's bit 0 is set, free `this` (`0x005d7a10`) |
| `0x0087baa0` | thiscall, 1 arg | constructs a 12-byte object: vtable `0x0116603c`, `+0x4` = the argument, `+0x8` = 0 |
| `0x0087bac0` | thiscall, (int, float) | advances a counter triple (`+0x4`/`+0x8`) by the int, then moves `+0x0` towards it by the float fraction, guarded by `0x00bc5610` (a global state test) |
| `0x0087bba0` | thiscall, 1 arg | sets `+0x8` to the argument and, if it is within 1000 of `+0x4` or `0x00bc5610` says so, snaps `+0x4` to it |
| `0x0087bbd0` | thiscall, (a, b) | initialises `+0x68`/`+0x6c`/`+0x70` = a, `+0x74`/`+0x78`/`+0x7c` = b, `+0x84` = a, `+0x88` = −1, arms the timer at `+0x90` with 0 (`0x00d9e3c0`), zeroes `+0x94` and the float `+0x80` |
| `0x0087bc30` | thiscall | the per-session **clock tick**: computes a frame delta from the global frame time `0x0132a0ac`; if local is host (`+0x5c` = `+0x58`) and `0x00bc5610` is false, advances the two counter triples (`+0x68..` by the delta, `+0x74..` by the raw delta), and — when the timer at `+0x90` has expired, `0x0059fc10` (session) is false and `0x00bc55a0` is false — re-arms it for 2000 ms and sends an **opcode `0x11`** record carrying the two counters (`0x0086f5f0`, two `0x0091dbc0` writes, `0x0086f1b0`(session,0,0), `0x0086eb20`); otherwise (client) smooths both triples towards the host's values with `0x0087bac0`, and — when the timer has expired and the connection gate passes (`+0xf4` ≥ 2 or `+0x224` ≠ 0 or `+0xf4` ≠ `+0xf8`, and byte `+0xfd` = 0) — re-arms it and sends an **opcode `0x1c`** record with no payload |
| `0x0087bee0` | thiscall, (t1, t2) | drives the above from two incoming time values: unless `0x00bc5610`, requires `+0x58` non-null with its byte `+0x0` ≠ 1; stores t2 into `+0x84` and re-arms the `+0x88` timer for 1000 ms; snaps `+0x70`/`+0x6c` and `+0x7c`/`+0x78` to t1/t2 when within 1000 ms; then, if `0x00bc5610`, runs the tick |
| `0x0087bfa0`, `0x0087c070` | thiscall, 1 arg (an identity record) | fill an identity record (`0x0086bbb0` resets it first). `0x0087bfa0`: when the session is **not** idle (the `+0xf4`/`+0x224`/`+0xf8` gate) and byte `+0xfd` is 0 — if byte `+0x4e` bit `0x40` is clear, ask the Steam user interface for the local Steam id (vtable slot 2), validate it with `0x00877670`, store it at record `+0x7`/`+0xb`, set record `+0x10` = 1 and return 0; if the bit is set, or Steam is absent or the id invalid, build an offline identity with `0x0087f1d0` and return 0 if record `+0x10` is still 0, else 2. When the session is idle (or `+0xfd` set), copy the local member's (`+0x5c`) identity block at member `+0x28` with `0x0086bbd0` and return 0. `0x0087c070` is the same shape for the *session's own* identity block at session `+0x28`, and in the idle case copies the host member's (`+0x58`) block, falling back to the session's own when there is no host |
| `0x0087c110`, `0x0087c140` | thiscall, (member) | set / clear the member's byte `+0x160`, call `0x0087b960`, then `0x0087b7e0` with `0x400` / `0x1000` and the member — the same pair fvfp saw in the teardown ("if the local member's byte `+0x160` is 1, clear it, call `0x0087b960` and `0x0087b7e0(0x1000, local)`"), so `+0x160` is a per-member "announced" bit and `0x400`/`0x1000` are the join/leave event codes **[HIGH CONFIDENCE]** |
| `0x0087c170` | thiscall, (member) | host-only **remove member**: requires `+0xf4` ≥ 2, member`+0x154` = this session, bytes `+0xfd` and `+0xfe` zero, and local = host; then `0x00d34d40`, `0x00878a50`(slot table `+0x50`, member), and returns whether `0x00867ec0`(session) returned 1 |
| `0x0087c1d0`, `0x0087c210` | thiscall | for two 16-byte handle slots at `+0xa0`/`+0x98`: if `0x00877670` says valid, release both halves with `0x00877700` |
| `0x0087c250` | thiscall | returns byte `+0x4d` − (number of valid handle slots among the two at `+0xa0`) − `+0x60` (member count): **free member slots** = capacity − reserved − present **[HIGH CONFIDENCE]** |
| `0x0087c2a0` | thiscall | true if local = host; else walks the member list from `+0x54` (next at `+0xb28c`) and returns false if any member's `+0x3c` differs from the host member's `+0x3c` — "every member agrees with the host on `+0x3c`" (a level/version id by shape — HYPOTHESIS) |
| `0x0087c2e0` | thiscall, (bool) | if `0x0086fde0` (via `0x008703c0`) is true and byte `+0x4e` bit `0x40` is clear: set byte `+0x244` = 1, `+0x2c8` = 0, and either run `0x0087b9a0` at once (bool true, `+0x2cc` = −1) or arm the `+0x2cc` timer for 10 000 ms — a "begin leaving / end session" entry with an optional 10 s grace **[HYPOTHESIS]** |
| `0x0087c340` | cdecl, (session) | the installer, Q1.1 |

Two consequences:

- **Byte `+0x4e` bit `0x40` marks an offline session** (no Steam identity, no leave notification). The engine therefore has a
  session type that never touches the network. That is the first concrete evidence that "a session object without network
  activity" is a designed state — compatible with, but not proof of, a single-player session **[HYPOTHESIS — only its gating
  role is confirmed]**.
- `0x0086be20` (used by `tutorial_start`'s replication record and by the queue serialiser in Q2.3) returns session`+0x68`
  when a session exists and the global `0x029f4db0` otherwise; `+0x68` is the host-advanced clock that opcode `0x11` carries.
  So the engine's "session time" has a no-session fallback — again consistent with running without a session.
  **[CONFIRMED — disassembly for the fallback; "clock" is HIGH CONFIDENCE from `0x0087bc30`.]**

### Q1.3 `0x008675c0` is the network-module initialiser; it never installs a session — CONFIRMED — disassembly

Body (`func/func_0x008675c0.txt`):

1. Guarded by the byte `0x024d4464` ("module up"): if already 1, return.
2. `0x00867400(1)` — switch the network mode global `0x024d4470` to 1. That function (dumped) does, when the mode changes to 1:
   call the two provider objects at `0x01495e78` and `0x01495ec8` (their vtable slot 0 and slot `+0x40`), reset their state
   bytes, `0x005d49e0(0,0)` and `0x00dad590(0,0)` on them, select **`0x01495f70` as the active provider** (`0x01493978` =
   `0x01495f70`), set `0x01302868` = 1 and `0x01302860` = `0x01302864` = 2, and call `0x00dcdbc0` with the name `coop-mp`; and,
   when the mode leaves 0, call `0x00dcdbc0` with `multiplayer` and `deltacomp_global`. `0x00dcdc20` with the same names is the
   inverse on the way back to 0. (`0x00dcdbc0`/`0x00dcdc20` take a name and nothing else; a named-context or named-pool
   enable/disable pair — **OPEN**.) The file-backed defaults of `0x01302860`/`0x01302864`/`0x01302868` are 2, 2, 2.
3. Set the module bytes `0x024d4461` = 1 and `0x024d4462` = 1, clear `0x024d4466` and the dword `0x024d4468`.
4. Then, with a call to the active provider's vtable slot `+0x5c` between every step (a pump/progress call — by shape only),
   in order: `0x0086bd80` (clear `0x024d49aa`, `0x01302888` = −1), `0x00871480` (zero a block at `0x024d6804..0x024d6829`,
   `0x01304370` = −1), `0x0086ab70`(provider) — allocate through the provider's slot `+0x38`: `0x01302860`×128 records of
   `0x224` bytes linked into a ring at `0x024d48a0`, 50 records of `0xb4` bytes with five embedded sub-lists at `0x024d48a4`,
   and a `0x86c4`-byte buffer registered with `0x00db57a0` under the name `reliable in-order payloads` — `0x00878bc0`(provider,
   10, `0x01302860`) — allocate 10 member records of `0x5c` bytes linked at `0x024d78d8`/`0x024d78e4`, each owning a
   `0x01302860`×5-byte sub-block — `0x0087d8a0`(provider) — the session pool init fvfp Q1.2 described, which **clears** the
   singleton — `0x0086dc40`, `0x00754410` (the no-op stub), `0x0087a000` (reset two 0x1c-byte slots at `0x024d7900`, then
   if `0x0087ae70` is true initialise `0x013043b4..0x013043c8` and arm a timer), `0x0088b5a0` (once, guarded by
   `0x024e4824`: `0x0088fad0`, `0x009da260`, `0x009ed570`, `0x00893890`, `0x00894110`, then `0x01309744` = 1),
   `0x0086e980` (clear `0x024d4a58`, `0x0086e800`, `0x01302980` = 0), `0x00876700` (walk and release a 0x14-byte-record list
   under the lock object `0x024d7890`, then `0x024d7880` = 0).
5. `0x01309746` = (`0x024d4462` ≠ 0), i.e. 1; then `0x024d4464` = 1.

**Nothing here creates or installs a session object.** The singleton is cleared defensively by `0x0087d8a0`. The function's
own evidence says "co-op multiplayer" (the mode name `coop-mp`, player count 2): this is the bring-up of the network layer.

**Callers (`callers/xref_0x008675c0.txt`): `0x00872760` at `0x00872762` (the call is that function's second instruction — a
wrapper) and `0x0088c090` at `0x0088c0a7`.** Neither was dumped; `0x0088c090` also reads the provider pointer twice, and the
wider block `0x0088b500`–`0x0088cb60` holds every other provider-pointer reader (`0x0088b570`, `0x0088b9f0`, `0x0088c750`, and
undefined code at `0x0088b529`, `0x0088caf3`, `0x0088cb2b`, `0x0088cb3e`) — the game-mode / lobby manager by all appearances
**[HYPOTHESIS]**. **When init runs is OPEN.**

Module bytes: `0x024d4461` is read by 47 functions (a "network module up" test used all over the engine), `0x024d4462` by 13
(including `0x009da260`, which the init itself calls); both are written only by init (1) and shutdown (0).
**[CONFIRMED — xref blocks in `func/func_0x008675c0.txt`.]**

### Q1.4 `0x008676f0` is the matching shutdown — CONFIRMED — disassembly

Body: `0x0087efe0` (the session-subsystem shutdown fvfp Q1.1 read: destroy every pooled session, flush callbacks, clear the
singleton, free the pool), `0x00754410` (no-op), `0x008778b0` (release the member-record list: walk from `0x024d78e0`, zero
`0x024d78d8`/`0x024d78dc`/`0x024d78ec`, clear `0x024d78e8`), `0x0086b990` (release the message pool: `0x0086b7c0` on
`0x024d48c8`, zero `0x024d48b4..0x024d48c4` if `0x024d48b4` was set, `0x024d48a0` = 0), `0x005d7d10(0)` (a guarded
re-sync of two frame counters `0x014983e8`/`0x01498408` with a 1 s wait and `0x00dd8100` on `0x014983fc`), `0x00867400(0)`
(mode back to 0: disables the `multiplayer` and `deltacomp_global` names), `0x00ddef00` (drain a receive queue at `0x02a3ddb4`:
repeatedly `0x00dde790` / `0x00dde920`, marking each slot −1 and bumping `+0x18a84`), then the four module bytes
`0x024d4461`/`0x024d4462`/`0x024d4464`/`0x024d4466` = 0.

**Callers:** `0x00867740` at `0x00867767` — a wrapper that first tests `0x024d4464` and tail-jumps to the shutdown (it also calls
`0x0086e980` at `0x0086774e`), and **undefined code at `0x0088b560`**. The block `0x0088b4c0`–`0x0088b5a0` is therefore a
mode-change or "leave multiplayer" path that reads the provider at `0x0088b529` and shuts the module down at `0x0088b560`
**[HIGH CONFIDENCE by position; OPEN until dumped]**.

A third guarded entry exists: `0x008672b0` tests `0x024d4464` and calls `0x0086e980` — a "reset part of the module if it is
up" **[CONFIRMED — xref; body not dumped]**.

### Q1.5 Who writes the host member (`+0x58`) and the local member (`+0x5c`)

From the earlier PC field census (`save_x_27.txt`, cross-checked against the fvfp teardown): `+0x58` is stored by `0x0087d1d0`
(at `0x0087d3d0`, together with `+0x5c` at `0x0087d3cd`, from the same register), by `0x0087e400` (the member-removal routine,
clearing it when the host member is removed), and by **undefined code at `0x0087c4bf`** (`MOV [EAX+0x58],EDX`, with a
`+0x5c` compare at `0x0087c461` — the gap right after the pool-build function `0x0087c390`). `+0x5c` is stored by `0x0087d1d0`,
by `0x0087e670` (teardown, zero) and by `0x0087e400`'s companion `0x0087e670` only.

So **`0x0087d1d0` makes a freshly created member both the local member and the host** — the "host a session" constructor — and
the undefined code after `0x0087c390` sets a *different* host member — the "join as client" counterpart **[HIGH CONFIDENCE
by the write shape; neither body was in this job]**. Whoever calls `0x0087d1d0` and then `0x0087c340` is the single-player
answer.

### Q1.6 Single-player answer and next dumps

- CONFIRMED: the installer exists, is `0x0087c340`, is the only non-null store, and is called by no defined code; the network
  module's init and shutdown never create a session; session objects have an "offline" flag (byte `+0x4e` bit `0x40`).
- OPEN: whether a plain single-player start or load calls `0x0087d1d0` + `0x0087c340`. Implement "no session".
- Next dumps, in order of payoff:
  1. `range` `0x0088b480`–`0x0088b5a0`, `0x0088b7c0`–`0x0088b8a0`, `0x0088cac0`–`0x0088cb60` — the undefined lifecycle blocks;
     look for a call to `0x0087c340` and to `0x0087d1d0`.
  2. `func` depth 1 + `xref`: `0x0087d1d0` (host-session constructor), `0x0088c090` and `0x00872760` (init callers),
     `0x00867740` (shutdown wrapper), `0x0088b570`, `0x0088b9f0`, `0x0088c750` (the provider's other readers).
  3. `range` `0x0087c390`–`0x0087c560` — the client-side host assignment after the pool build.
  4. `xref` `0x02609398` and `func` `0x00905ec0`/`0x00905ea0` — who consumes the "flag 4 = host" the installer publishes.
  5. Optional: `func` `0x0087f1d0` (offline identity) and `xref` of its callers — whether the offline session type is the
     single-player one.

---

## Q2. Tutorial state 3, the registration routine, and the save-restore feed

### Q2.1 `0x007178c0` fills the table once, from compiled-in records; called only by `0x00715f20` — CONFIRMED — disassembly

Body (`func/func_0x007178c0.txt`, `0x007178c0`–`0x0071ace1`): 210 straight-line calls to `0x00715850` with indices 0..209, each
preceded by building a 40-byte record on the stack from immediates. The record layout as built (entries 0, 1, 208, 209 read in
full; the decompiler's rendering of the first entries agrees): `+0x00` the body text id (a `TUT_…` localisation key),
`+0x04..+0x14` zero, `+0x18` the title text id (`TUT_TITLE_…`), `+0x1c` a pointer to an empty string in `.rdata`
(`0x0129a0e3`), `+0x20` = 2 for those four entries, `+0x24` = 0 for those four. (dksj read `+0x20` as the default duration
copied into entry `+0x18`, and `+0x24` as the flag bits; both fit.) `0x00715850` (dumped) copies the 40 bytes into slot I of the
run-time descriptor array `0x0151f388`, points entry I`+0x08` at it, sets state `+0x0c` = 0, and overwrites it with 1 for
I ≥ 189 — exactly as fvfp read it.

After the last fill, a loop over the **seven name pointers at `0x012f5c24`** (the first two resolve to the names at indices 189
and 190 of the kyoi list) hashes each name with `0x00d9e8b0` (the CRC-32 of the lower-cased string, seed 0, no length limit)
and stores the hash into `0x0152145c[i]`, i = 0..6. So the 7-dword id table holds **name hashes**, and `0x007177c0`
(fvfp Q2.2) maps a hash to index 189 + position.

**Caller: exactly one, `0x00715f20`, and the call is the first instruction of that function** (`callers/xref_0x007178c0.txt`:
the reference is at `0x00715f20` itself). dksj identified `0x00715f20` as the sole writer of the UI handle `0x0151d5a8`. So
`0x00715f20` is the **tutorial subsystem initialiser**: fill the table, then create the tutorial UI document. **When
`0x00715f20` runs is OPEN** (its callers were not requested).

### Q2.2 `0x00b9ae60` applies a loaded save block; section 5 restores the DLC-range tutorial states — CONFIRMED — disassembly (body), HIGH CONFIDENCE (it is the save loader)

Arguments: a pointer B to a large block (fields read up to B`+0x19aa0`) and a boolean F. Body (`func/func_0x00b9ae60.txt`): a
chain of sections, each applied only when the dword at B`+0x11d88` (call it V) is at least the section number — the shape of a
versioned save-game image:

| applies when | what |
|---|---|
| V ≥ 1 | if F: `0x00823d30`(B`+0x11d90`, count B`+0x11d8c`, 0) — gets the local player (`0x009da4e0`), and for each 8-byte record converts it (`0x00823ae0`) and applies it to the player (`0x009dad20`). Then three bit-set descriptors over B`+0x16308`/`+0x16334`/`+0x16360` and `0x0071fd30`(B`+0x15d90`, the three, 350): for each of 350 ids, three bits are read; an id found in the 256-byte-stride table `0x01522750` (count `0x015226fc`) gets its bytes `+0xe0..+0xe2` set from them (with a special case when its `+0xf8` is 1 and `0x0045b3a0`(byte `+0xfc`) fails); an id not in the table is registered with `0x0071c740` if any bit is set. Then three releases (`0x0071b8c0`). |
| V ≥ 2, F | `0x0061bdf0`(B`+0x1638c`, 1): merge a 64-entry list into the global table at `0x014b39b8` (second copy at `+0x308`), then for every object in `0x014b2ac4`/`0x014b2b68` recompute a counter in `0x014b2e38`; when a session exists and local is **not** host, set each object's byte `+0x1d5` from the table |
| V ≥ 6 | if F: for each 16-byte record at B`+0x18bd0` (count B`+0x18fd0`) `0x006d4eb0`: resolve its handle; if it is not a live object of class bit (`+0xc` & 4) push the record onto the 64-slot pending list `0x014c7a18`; else store it into the object's slot 8 or 9 (9 when a session exists and local is a client) and, if local is host and the record's flag bit 0 is set, `0x006e0810`(1) and a named UI message (`0x00e0ca80`). Then `0x006d7860(0)`: if a session exists and local is host, `0x006d7580` on two name-pointer tables (`dlc2_m01`, `dlc3_m01`) with 3 |
| V ≥ 3 | `0x00b9a6b0`(B, F): if F, resolve the handle pair at B`+0x58`/`+0x5c` as a live object of class bit (`+9` & `0x20`), else null; then for each of B`+0x16714` records at B`+0x16694` `0x00b97a00`(object, 0); finally `0x007e2dc0` |
| V ≥ 4, F | `0x00822cf0`(B`+0x1671c`, byte B`+0x16718`) — per entry, find a record (`0x0082a680`) and either `0x009dd620` or `0x009dafd0` on the local player; `0x00824300`(B`+0x16848`, B`+0x16844`, 0) — per 0x114-byte record, copy a wide string, map its sub-records through `0x00823fc0` and apply via `0x00821ae0`/`0x009dafd0` |
| **V ≥ 5** | **`0x007163e0`(count B`+0x18ac8`, pairs at B`+0x18acc`)** — the generic tutorial state setter: for each (name hash, value) pair whose hash is one of the seven at `0x0152145c`, store value into entry (189 + position)`+0x0c`, unvalidated |
| V ≥ 7 | up to 64 handle pairs at B`+0x18fd8` (stop at the first zero pair): resolve as a live object of class bit (`+0xc` & 4), then `0x006e08d0` (its `+0x150` handle as a live object of class bit (`+7` & 2)) and `0x0093b190`(0) on that (set bit 0 of its byte `+0x7b` and call `0x008030a0` with its `+0xc8`/`+0xcc`) |
| V ≥ 8 | for each 0x70-byte record at B`+0x191dc` (count B`+0x191d8`): `0x005f7880`(0, `0x014a1d00`, record) — allocate (`0x005f62f0`) and fill an entry from the record and link it into the list at `0x014a1d00` (the list the store code uses, dksj Q3) |
| V ≥ 9 | `0x0083a090`(B`+0x19aa0`): four (name, name) pairs resolved with `0x00be3320`/`0x00be3290`, falling back to `npc_questionmark` / `generic` |

Reference to `0x00b9ae60`: exactly one, a **data** reference from `0x0118c08c` (`callers/xref_0x00b9ae60.txt`) — its address sits
in a static table, i.e. it is a callback **[CONFIRMED — xref]**; a save-system "apply loaded data" hook **[HYPOTHESIS]**.

**What it feeds the setter:** the pairs stored in the loaded block at `+0x18acc`, count at `+0x18ac8`, each (CRC-32 name hash,
state dword). The values are whatever the matching *save* writer stored (not in these dumps). So the states of entries 189–195
are **save-persisted**; on a fresh profile this section writes nothing new, and Team B's single-player mission run (no save
load) never reaches it. **[CONFIRMED — disassembly for the data flow; the save-side writer is OPEN.]**

### Q2.3 `0x00717240`–`0x0071738f` is the queue **receiver**, writes only 4, and is not part of `0x00717020` — CONFIRMED — disassembly

**`0x00717020` is complete and self-contained** (`func/func_0x00717020.txt`: body `0x00717020`–`0x0071716a`, an 8-byte frame
without a stack cookie, ending in a return). Its argument is a bit-stream writer (`0x00881040` appends bytes, `0x00881110`
appends bits, `0x004d46e0` appends one bit). It reserves one byte for a count, and if the queued-prompt list `0x0151d588`
is non-empty writes the session clock (`0x0086be20`, 4 bytes) and then, for each queued entry in ring order: its descriptor
index ((entry`+0x08` − `0x0151f388`) / 40, 16 bits), entry`+0x1c` (8 bits), entry`+0x14` (4 bytes), and the bit
"entry`+0x18` = 0"; bumping the count byte each time. **It is the serialiser of the queued-tutorial list.** Caller:
`0x008ba5d0` at `0x008ba70c` (not dumped). **[CONFIRMED — disassembly; "for join-in-progress replication" is HYPOTHESIS.]**

The dumped region starts 0xd6 bytes after that return, has a 0xac-byte frame with a stack cookie, and belongs to a function
that **starts in the undumped gap `0x0071716b`–`0x0071723f`** (the gap calls `0x0086be20` at `0x00717239` — listed among that
function's callers in this job — so it, too, reads the session clock). The visible tail is the **receiver** that rebuilds one
queued tutorial from the stream:

1. `0x00717240`: subtract a saved time from the elapsed-time register (now − stamp); call `0x00715c80` with the stream's
   descriptor index in a register (not dumped; it returns the table index); take the entry and its descriptor `+0x08`.
2. If the descriptor's flag bit `0x04` is set (rich widget): build the same parameter block the dispatcher builds
   (`tutorial_start` step 11: the float at `0x01187ecc`, the stream's duration converted and scaled by the double at
   `0x012a2d78`, 0.0, the float at `0x0126d2cc`, the 1/0 fields, −1), call the descriptor's builder `+0x14` if present else
   `0x0084a1b0` with the descriptor's first field, then `0x007fc560`; store the handle in `0x0151d5b4`; **state = 4**
   (`0x00717311`, an immediate through the entry pointer).
3. Else (simple prompt) call `0x00715bf0` with the index in a register and three stack values: the **remaining** duration
   (stream duration − elapsed), the stream's `+0x18` bit, and the stream's `+0x1c` byte — the exact fields the serialiser
   wrote. `0x00715bf0` sets state 2 (dksj).
4. A separate early-out path at `0x00717348` (reached from the undumped head; it returns without the fourth saved register,
   the pattern MSVC emits for an exit taken before that register is pushed): if the entry's state is already 4, return; else
   **state = 4 (the immediate 4 loaded at `0x0071734b`, stored at `0x00717363`)** and arm the global 1-second timer object
   `0x012f58ec` (`0x00d9e140` with 1000).

So fvfp Q2.3's "a register of unknown value is stored at `0x00717363`" is settled: the value is **4**. The remaining-duration
arithmetic and the 1:1 field correspondence with `0x00717020` make this the serialiser's counterpart **[HIGH CONFIDENCE]**;
the pair would be the join-in-progress sync of tutorials queued on the host **[HYPOTHESIS]**.

### Q2.4 Where state 3 can still come from — OPEN, narrowed

Writer census of the state field after this job (the `0x0151d60c` reference block in `func/func_0x00b9ae60.txt`, 28 uses; the
address-sorted listing is capped at 25, and the three entries it drops are the highest addresses, `0x00717350`, `0x00717363`
and `0x00717390`'s write, all known from dksj — so the census below is complete):

| value | writers | status |
|---|---|---|
| 0 / 1 | `0x00715850`, `0x00716140`, `0x00716170`, `0x007161a0`, `0x007169e0`, `0x00716940` | CONFIRMED |
| 2 | `0x00715bf0` | CONFIRMED |
| 4 | `0x007169e0`, `0x00716ed0`, `0x00717390`, and the receiver at `0x00717311` and `0x00717363` | CONFIRMED (all five) |
| save value | `0x007163e0`, entries 189–195 only, from the loaded save block (Q2.2) | CONFIRMED (feed); value OPEN |
| unread | **undefined code at `0x00716385` and `0x007163b5`** — two references to the state field that Ghidra places outside any defined function, between the state getter `0x00716360` and the generic setter `0x007163e0` | **OPEN — new lead** |

Those two sites were not in dksj's or fvfp's tables (both were working from capped listings). A function of ~0x60 bytes that
touches the state field twice, sitting between the getter and the setter, is the natural place for a "set entry's state"
or "advance 2 → 3" helper. Writes made through an entry pointer held in a register (not resolved to the global by the
disassembler) also remain possible and are invisible to this census. Readers of 3 are unchanged: `0x007162a0` and
`tutorial_advance`'s core `0x00716440`.

**State 3's meaning stays a HYPOTHESIS** ("displayed, waiting for the player"). Nothing here supports or contradicts it. For
Team B: `tutorial_advance` returns false for every name in single player until a writer of 3 is found; do not implement one.

### Next dumps (Q2)

1. `range` `0x00716360`–`0x007163e0` (the two unread state-field references) and `0x0071716b`–`0x00717240` (the receiver's
   head — confirms the field order and what `0x00715c80` is given).
2. `func` depth 1 + `xref`: `0x007162a0` (the other "is state 3" reader — its callers name the state), `0x00715f20` (tutorial
   init: when the table is filled), `0x008ba5d0` (the serialiser's caller: what the stream is for).
3. `func` depth 1 of the queue consumers that rewrite `0x0151d588` (from the xref block in `func/func_0x00717020.txt`):
   `0x00715a30`, `0x00715ad0`, `0x00715f60`, `0x00716000`, `0x00716220`, and the readers `0x007161e0` — the prompt-display
   machinery that would move a queued entry (2) onward.
4. `xref` `0x0118c08c` (the callback table holding `0x00b9ae60`) to name the save hook, and the save-side writer of the
   `+0x18ac8`/`+0x18acc` pairs (search for a reader of `0x0152145c` other than `0x007177c0`, or of entries 189–195's state).

---

## Q3. Store-flag call sites: three function tails read; triggers still OPEN

All three ranges came back as the ends of functions whose heads were outside the requested windows. What they show:

### Q3.1 `0x005fb3e0`–`0x005fb420`: two success exits of one function — CONFIRMED — disassembly

The function (call it F_A; frame: one saved register, 8 bytes of locals, caller-cleaned return) holds an object in a register
whose handle pair is at `+0x8`/`+0xc` (the handle layout dksj saw for the store's vehicle and location objects). Exit 1
(`0x005fb3e2`): store state `0x014a1ce4` = 4, return true. Exit 2 (`0x005fb3f3`): copy the object's handle pair to a local pair,
call **`0x005fa820`** with its address (the handle-driven store entry: flag `0x022cdf08` = 1, fvfp Q3.1), then store state = 4
and return true. So F_A enters the store at the object it holds, or merely marks the store state, depending on a test before
`0x005fb3e0` that is not in the dump. By the class check inside `0x005fa820` (location class), the object is the store-location
object **[HIGH CONFIDENCE]**. Head and condition: **OPEN** — dump `range 0x005fb200–0x005fb3e0` (the two bytes at
`0x005fb3e0`/`0x005fb3e1` are the tail of an `ADD ESP` — the window began mid-instruction).

### Q3.2 `0x005fbdb0`–`0x005fbdf0`: the end of a search loop — CONFIRMED — disassembly

Function F_B (same frame shape; a false exit at `0x005fbde4` that pops no register, so it is an early-out taken before the
register was saved). The visible part is the bottom of a loop: store a value into the local at `+0xc`, call `0x005fb930` with
one argument, and loop back to `0x005fbd98` while the result is ≤ 0; on the first positive result copy the held object's
handle pair and call **`0x005fa820`**, return true. So F_B scans something, asking `0x005fb930` a "positive count/score?"
question, and enters the store at the first hit. `0x005fbdf0` begins the next function (a 0x11c-byte frame). Head: **OPEN** —
dump `range 0x005fbc80–0x005fbdb0` and `func 0x005fb930`.

### Q3.3 `0x005fc1e0`–`0x005fc220`: the exit path pre-loads the store target — CONFIRMED — disassembly

Inside a larger function F_C (three saved registers, 0xb0-byte frame with a stack cookie; its main body continues at
`0x005fc16b`/`0x005fc16d`, before the window). The block at `0x005fc1e7`: write the store-location global `0x014a1dc4` = the
location object held in a register; write the store-vehicle handle `0x014a1dc8`/`0x014a1dcc` = the handle pair (`+0x8`/`+0xc`) of
the vehicle object held in another; call `0x005f91c0` as a method of the location (returns an object — the owning building by
what `0x00820cd0` then does with it, HYPOTHESIS); pass that to **`0x00820cd0`** (the vehicle-driven store exit: flag = 0 only
if its UI call succeeds, fvfp Q3.2); discard the result and rejoin the main body. Since `0x00820cd0` requires "L equals
`0x014a1dc4` and V's handle equals `0x014a1dc8/cc`", F_C satisfies those checks **by construction** immediately before the
call; the real gate is therefore whatever selects this block, plus `0x00820cd0`'s own vehicle test (`0x00a367f0`) and
`0x005fbae0`. The alternative path at `0x005fc1e0` returns false. Head: **OPEN** — dump `range 0x005fbdf0–0x005fc2c0`.

### Q3.4 Status for Team B

Unchanged: the flag starts at 0 and only `store_vehicle_change_mode` can move it in a script-only run. The two handle-driven
entries and the vehicle-driven exit are reached from engine code whose triggers are still unread; nothing in these tails is
Lua-reachable.

---

## Spec changes

Exact sentences, and what they become. Labels as derived above. Job id for citations: `20261001T121532-team-a-gdhw`.

### §26.28

1. **"No resolved code installs a session. The only other references, at `0x0087c341`/`0x0087c359`, are in code the disassembler
   never defined and are, by elimination, where a session pointer is stored **[HIGH CONFIDENCE — by elimination; OPEN until
   that code is disassembled]**."**
   → "No resolved code installs a session. The only other references, at `0x0087c341`/`0x0087c359`, are the load and store of
   the **session installer `0x0087c340`** (undefined code, range-disassembled in job `20261001T121532-team-a-gdhw`): given a
   session pointer, it returns at once if that pointer is already installed; otherwise, if a session is currently installed, it
   flushes the deferred-callback queue (`0x00894430`) without destroying the old session, stores the new pointer, and — if the
   new pointer is non-null — publishes the host answer (`+0x5c` = `+0x58`) to a global flag table as flag 4 (`0x00905ec0`(4) when
   host, `0x00905ea0`(4) otherwise). It is the only code that can store a non-null session. **[CONFIRMED — disassembly, job
   gdhw.]** It has no caller in defined code (the earlier reference search for `0x0087c340` is empty and no dumped function
   calls it), so when it runs is OPEN; the undefined lifecycle blocks at `0x0088b4c0`–`0x0088b5a0`, `0x0088b7c0`–`0x0088b8a0`
   and `0x0088cac0`–`0x0088cb60` are the next places to look."

2. **"the installer is in undefined code at `0x0087c341`/`0x0087c359`, so whether a single-player start creates a one-member
   host session cannot be read yet."**
   → "the installer `0x0087c340` has now been read (job `20261001T121532-team-a-gdhw`) but has no caller in defined code, so
   whether a single-player start creates a one-member host session still cannot be read. Two further facts bear on it without
   settling it: the host-session constructor is `0x0087d1d0` (it stores one new member into both `+0x5c` and `+0x58`), and session
   objects carry an *offline* flag (byte `+0x4e` bit `0x40`) under which no Steam identity is fetched and no leave event is sent
   **[CONFIRMED — disassembly for the flag's gating role; its use for single player is HYPOTHESIS]**."

3. **Add after the "A session's connection state…" paragraph:** "The network module is brought up by `0x008675c0`, once (guarded
   by the byte `0x024d4464`): it switches the mode global `0x024d4470` to 1 through `0x00867400` (which selects the provider object
   `0x01495f70` into `0x01493978`, sets `0x01302868` = 1 and `0x01302860` = `0x01302864` = 2, and enables the names `coop-mp`,
   `multiplayer` and `deltacomp_global` with `0x00dcdbc0`), sets the module bytes `0x024d4461`/`0x024d4462` to 1, allocates the
   message, payload and member pools (`0x0086ab70`, `0x00878bc0`), builds the two-slot session pool (`0x0087d8a0`) and runs a
   handful of one-time inits; it never creates a session. `0x008676f0` is the matching shutdown: it destroys every pooled session
   (`0x0087efe0`), frees the pools, drains the receive queue, switches the mode back to 0 (disabling the three names) and clears
   the four module bytes. Init is called from `0x00872760` and `0x0088c090`; shutdown from the guarded wrapper `0x00867740` and
   from undefined code at `0x0088b560`. **[CONFIRMED — disassembly and xref, job `20261001T121532-team-a-gdhw`; when those
   callers run is OPEN.]**"

4. **"When `0x007178c0` runs is OPEN."** (tutorial paragraph)
   → "`0x007178c0` is called exactly once, as the first action of `0x00715f20` — the tutorial subsystem initialiser, which also
   creates the UI handle `0x0151d5a8` (§10.4); when `0x00715f20` runs is OPEN. The 210 records are compiled-in constants (each
   built from immediates: body and title localisation keys, an empty string pointer, a default of 2 at `+0x20`, flags at `+0x24`).
   After the last fill, `0x007178c0` stores the CRC-32 name hashes (`0x00d9e8b0`, lower-cased, seed 0) of the seven names at
   `0x012f5c24` — the names at indices 189–195 — into the 7-dword id table `0x0152145c`, which is what the generic setter
   `0x007163e0` matches against. **[CONFIRMED — disassembly and xref, job `20261001T121532-team-a-gdhw`.]**"

5. **Add to the tutorial paragraph, after the sentence above:** "State 3 (job `20261001T121532-team-a-gdhw`): the undefined
   region `0x00717240`–`0x0071738f` is not part of `0x00717020` (which ends at `0x0071716a`); it is the tail of the receiver that
   rebuilds a queued tutorial from a stream (the counterpart of the serialiser `0x00717020`), and both of its state stores
   (`0x00717311`, `0x00717363`) write the literal 4. `0x007163e0` is fed by `0x00b9ae60`, which applies a loaded save block to the
   world in version-gated sections; its section 5 passes the block's (name hash, state) pairs at `+0x18acc` (count `+0x18ac8`)
   to the setter, so the states of entries 189–195 are save-persisted. No read code writes 3. The only unread direct references
   to the state field are in undefined code at `0x00716385`/`0x007163b5`. **[CONFIRMED — disassembly for the two 4-writes, the
   receiver/serialiser pairing's field order and the save-restore feed; OPEN — who writes 3.]**"

6. **Review status line:** replace "OPEN — … whether single-player startup installs a session (the installer is undefined code at
   `0x0087c341`; needs a range-disassembly dump)" with "OPEN — whether single-player startup installs a session: the installer
   `0x0087c340` is read (job `20261001T121532-team-a-gdhw`) but its callers are undefined code (`0x0088b4c0`–`0x0088cb60`
   region; next dump)"; replace "HIGH CONFIDENCE (by elimination) — the installer is the undefined code at
   `0x0087c341`/`0x0087c359`" with "CONFIRMED — the installer is `0x0087c340`; the network module init/shutdown pair
   `0x008675c0`/`0x008676f0` never creates a session; `0x007178c0` is called once from the tutorial initialiser `0x00715f20`;
   the region `0x00717240`–`0x0071738f` writes only state 4; entries 189–195 are restored from the save block by `0x00b9ae60`";
   add "OPEN — the store-flag triggers: the three undefined call sites are function tails (`0x005fb3e0`, `0x005fbdb0`,
   `0x005fc1e0` windows); their heads need `range` dumps (§10.1)".

### §8.27

7. **Append to the "[2026-10-01, job `20261001T114716-team-a-fvfp`: …]" note:** "**[2026-10-01, job `20261001T121532-team-a-gdhw`:
   the installer is `0x0087c340` — it stores the session pointer and publishes this same `+0x5c` = `+0x58` test as global flag 4
   (`0x00905ec0`/`0x00905ea0`); it has no caller in defined code, so the single-player answer stays OPEN; implement false. The
   network module init `0x008675c0` does not create a session. CONFIRMED — disassembly.]**"

8. **Review status line:** "Still OPEN — the installer (undefined code at `0x0087c341`); implement false." → "Still OPEN — the
   installer `0x0087c340` is read (job `20261001T121532-team-a-gdhw`) but has no defined caller; implement false."

### §10.1

9. **"Their game-level triggers are OPEN: `0x005fa820` is called from `0x0080e2f0` and two undefined sites (`0x005fb406`,
   `0x005fbdd5`); `0x00820cd0` from one undefined site (`0x005fc207`).]]"**
   → "Their game-level triggers are OPEN: `0x005fa820` is called from `0x0080e2f0` and two undefined sites (`0x005fb406`,
   `0x005fbdd5`); `0x00820cd0` from one undefined site (`0x005fc207`). **[2026-10-01, job `20261001T121532-team-a-gdhw`:** the
   three undefined sites were range-disassembled and are function tails. Both `0x005fa820` callers copy the handle pair
   (`+0x8`/`+0xc`) of an object they hold — the store-location object, by `0x005fa820`'s own class test (HIGH CONFIDENCE) — into a
   local pair and pass its address; the first (`0x005fb406`) then sets store state `0x014a1ce4` = 4 and returns true, and has a
   sibling exit that sets state 4 without entering the store; the second (`0x005fbdd5`) is the end of a loop that enters the
   store at the first object for which `0x005fb930` returns a positive value. The `0x00820cd0` caller (`0x005fc207`) first writes
   the store-target globals itself — `0x014a1dc4` = the location object, `0x014a1dc8/cc` = the vehicle's handle pair — then passes
   `0x005f91c0`(location) to `0x00820cd0`, so the exit routine's location/vehicle checks are satisfied by construction and its real
   gates are its own vehicle test and `0x005fbae0`. The heads of all three functions lie before the dumped windows, so the
   triggers remain OPEN. CONFIRMED — disassembly for the tails.]]"

10. **Review status line:** "OPEN — their callers (`0x0080e2f0`; undefined code at `0x005fb406`, `0x005fbdd5`, `0x005fc207`)." →
    "OPEN — their callers: `0x0080e2f0` unread; the three undefined sites are read as tails (job `20261001T121532-team-a-gdhw`)
    and need the function heads (`range 0x005fb200–0x005fb3e0`, `0x005fbc80–0x005fbdb0`, `0x005fbdf0–0x005fc2c0`)."

### §10.4

11. **"for entries 0–188 the only candidate is the undefined code before the function tail at `0x00717363` (which stores a
    register value and then arms a timer), inside the undefined region `0x00717240`–`0x0071738f` that is probably the rest of
    `0x00717020`."**
    → "for entries 0–188 no writer of 3 has been found: job `20261001T121532-team-a-gdhw` showed that the region
    `0x00717240`–`0x0071738f` is not part of `0x00717020` (which ends at `0x0071716a` and is the serialiser of the queued-prompt
    list) but the tail of the matching stream receiver, and that both of its state stores (`0x00717311`, `0x00717363`) write the
    literal 4 — the second after an 'already 4?' test, followed by arming the global 1-second timer `0x012f58ec`. For entries
    189–195 the generic setter `0x007163e0` is fed by the save loader `0x00b9ae60` (section 5 of a version-gated save block:
    (name hash, state) pairs at `+0x18acc`), so their states are save-persisted and could be 3 only if the save side ever
    stored 3. The only unread direct references to the state field are in undefined code at `0x00716385`/`0x007163b5`
    (between the state getter and the generic setter)."

12. **Append to the "[Re-derived 2026-10-01: the value stored into the message's `+0x14` …]" note:** "The writer of
    `0x0151d5a8`, `0x00715f20`, is the tutorial subsystem initialiser: its first action is the table fill `0x007178c0` (§26.28)
    and it then creates the handle. **[CONFIRMED — xref, job `20261001T121532-team-a-gdhw`; when it runs is OPEN.]**"

---

## Next dumps (consolidated, in priority order)

1. `range` `0x0088b480`–`0x0088b5a0`, `0x0088b7c0`–`0x0088b8a0`, `0x0088cac0`–`0x0088cb60`; `func` depth 1 + `xref`
   `0x0087d1d0`, `0x0088c090`, `0x00872760`, `0x00867740`, `0x0088b570`, `0x0088b9f0`, `0x0088c750` — the installer's callers
   and the single-player answer.
2. `range` `0x0087c390`–`0x0087c560`; `xref` `0x02609398`; `func` `0x00905ec0`, `0x00905ea0`, `0x0087f1d0`.
3. `range` `0x00716360`–`0x007163e0`, `0x0071716b`–`0x00717240`; `func` depth 1 + `xref` `0x007162a0`, `0x00715f20`,
   `0x008ba5d0`; `func` `0x00715a30`, `0x00715ad0`, `0x00715f60`, `0x00716000`, `0x00716220`, `0x007161e0`; `xref` `0x0118c08c`.
4. `range` `0x005fb200`–`0x005fb3e0`, `0x005fbc80`–`0x005fbdb0`, `0x005fbdf0`–`0x005fc2c0`; `func` `0x005fb930`, `0x0080e2f0`.

## Summary of labels

| item | label |
|---|---|
| `0x0087c340` is the session installer; body as in Q1.1; only non-null store of `0x024d8534` | CONFIRMED — disassembly |
| its callers | OPEN (no defined caller; undefined lifecycle blocks next) |
| flag 4 at `0x02609398` = "local is host" | HYPOTHESIS |
| `0x008675c0` / `0x008676f0` = network module init / shutdown; neither creates a session; callers as listed | CONFIRMED — disassembly + xref (when they run: OPEN) |
| session byte `+0x4e` bit `0x40` = offline session | CONFIRMED (gating role), HYPOTHESIS (single-player use) |
| `0x0087d1d0` = host-session constructor (writes `+0x5c` and `+0x58` from one new member) | HIGH CONFIDENCE (earlier field census; body not dumped) |
| single-player start installs a session | OPEN — implement "no session" |
| `0x007178c0` called once, first instruction of `0x00715f20`; records compiled-in; 7-id table = CRC-32 name hashes | CONFIRMED — disassembly + xref |
| `0x00b9ae60` applies a loaded save block; section 5 feeds `0x007163e0` | CONFIRMED (body), HIGH CONFIDENCE (save loader) |
| `0x00717020` = queued-tutorial serialiser; `0x00717240`.. = its receiver; both stores there write 4 | CONFIRMED (stores, bounds), HIGH CONFIDENCE (pairing) |
| who writes tutorial state 3 | OPEN (leads: `0x00716385`/`0x007163b5`) |
| store-flag call sites: tails as in Q3 | CONFIRMED — disassembly; triggers OPEN |
