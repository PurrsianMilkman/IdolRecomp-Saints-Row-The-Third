# Interpretation of bridge job jfue (follow-up to gdhw): who runs the session installer, the session constructor, the mode-manager lifecycle, tutorial state 3, store-flag triggers

Dumps: `/root/crreish-bus/results/20261001T124128-team-a-jfue/` (job `team-a/ghidra/jobs/gdhw-followup.json`).
Written 2026-10-01 by Team A. Labels: **CONFIRMED — disassembly** only for what these dumps show; otherwise HIGH CONFIDENCE /
HYPOTHESIS / OPEN. Prior context: `interp_dksj.md`, `interp_fvfp.md`, `interp_gdhw.md` (same directory).

Job note: all three steps ran. Nine `range` windows, nine `func` bodies at depth 1 (one callee, `0x007178c0`, was skipped for
size — it was already read in gdhw) and nine `xref` listings came back. Two earlier results were used for cross-checking only:
the fvfp dump of `0x0087ed70`/`0x0087efe0` (for the second session pointer) and the kyoi pointer listing (for tutorial name 176).

---

## Headline

**`game_get_is_host` in single player stays OPEN; Team B keeps "no session, answer false".** The decisive negative is now firm:
**no disassembled instruction in the executable references the installer `0x0087c340`** (the `xref` mode reports zero uses, and
it does record references from undefined code, as the `0x005fb930` and `0x008676f0` listings in this very job show). The three
undefined lifecycle blocks gdhw named as the best candidates have been read in full and none of them calls the installer or the
session constructor. **[CONFIRMED — disassembly.]** The installer can therefore only be reached from bytes the disassembler never
turned into instructions, or through a pointer held in untyped data; a raw byte search is the next step (see Next dumps).

What the job did settle changes the shape of the question:

1. **`0x0087d1d0` is the session constructor, and a freshly constructed session is already a one-member, local-is-host,
   offline-flagged session** (local member = host member = the one member it creates; capacity 1; settings byte `+0x4e` = `0x42`,
   i.e. the offline bit `0x40` set; connection state 1/1). It has exactly one call site in the executable, `0x0087f1b0`
   (not dumped). **[CONFIRMED — disassembly + xref.]**
2. **The host member `+0x58` is reassigned away from the local member only by the slot-table event handler `0x0087c430`
   (event `0x40`: host assignment / migration)**, and cleared when the host member is removed. So a session is a *client* session
   only after that handler has run; every session starts as a host session. **[CONFIRMED — disassembly for the handler; its
   role name is HIGH CONFIDENCE.]** Hence: *if* a single-player start installs a session at all, `game_get_is_host` is true.
3. **The lifecycle code works on a different session pointer.** The "leave", "destroy when idle" and "open" steps in
   `0x0088b7c0`–`0x0088b8a0` fetch their session through `0x0087ba10`, which reads **`0x024d8538`** (fvfp Q1.7's second pointer,
   setter `0x0087ba50`), not the Lua-visible singleton `0x024d8534`. **[CONFIRMED — disassembly; the accessor's target from the
   fvfp xref block.]** So the engine keeps "the session the mode manager is working on" apart from "the session the game is in";
   the installer promotes a session to the second role, and its caller is still unread.
4. **Network module init and shutdown are driven by a generic three-callback state machine** (`0x0088bb30`) whose instance is
   the mode-manager object at `0x01304ab4`; `0x0088c090` restarts that machine (requested state 0, 1 or 6 chosen by two bytes
   `0x024e48b2`/`0x024e48b8`) and runs the module init on the way. `0x00872760` is the other init caller (server-browser pool
   setup, called from `0x007ade60`). Shutdown runs through `0x00867740` from `0x007ae950`, `0x00872830` and undefined code at
   `0x007aea30`. When any of these runs on a single-player start is OPEN. **[CONFIRMED — disassembly + xref for the structure.]**

Tutorial: **no writer of state 3 was found again**, but the census is now provably incomplete in one specific way (writes through
the queue-head pointer `0x0151d588` are invisible to it), and the engine's own name for the "state == 3" test is
**`tutorial_active`** (§20.5), so 3 is the *active* state by the engine's naming. The gdhw pairing of the undefined region
`0x00717240`+ with the queue serialiser is **withdrawn**: the region is the receiver of the **opcode `0x54` `tutorial_start`
replication record** (field order matches §6.19 step 12 exactly). Two new undefined routines `0x00716380`/`0x007163b0` export and
import the states of entries 0–188 as a flat 189-dword array, so those states are save-persisted too.

Store: the three function heads are read. The two handle-driven entries and the vehicle-driven exit are **engine trigger
handlers** — the player walking (or driving) into a vehicle store location — gated by localisation-keyed HUD messages for "no
vehicles", "trigger in use by the co-op partner", "garage full" and "can't use mechanic". None is Lua-reachable directly; their own
callers are undefined code with no references.

---

## Q1. Session install, constructor, lifecycle, and when init/shutdown run

### Q1.1 The installer `0x0087c340` has no reference anywhere — CONFIRMED — disassembly + xref

`xref/xref_0x0087c340.txt`: "total uses 0 in 0 functions". The same mode, in this job, lists references that originate in
undefined code (`0x005fbdb6 (no function)` as a caller of `0x005fb930`; `0x0088b560 (no function)` as a caller of `0x008676f0`;
`0x007aea30 (no function)` as a caller of `0x00867740`), and data references (`0x0088c090` is referenced only as a stored pointer
from `0x0088cf70`). So the zero is not an artefact of the installer being undefined: **no instruction the disassembler produced,
defined or not, calls, jumps to or takes the address of `0x0087c340`**, and no typed data holds its address.

The three range windows gdhw proposed were read in full and contain no call to `0x0087c340` and no call to `0x0087d1d0`
(Q1.3–Q1.5 below say what they do contain). **[CONFIRMED — disassembly.]**

Consequences. The singleton `0x024d8534` is read at 1,867 call sites (dksj), so "never set" is not credible for the shipped game.
The installer must be reached from one of: (a) code bytes the disassembler never reached (no flow into them, so no instructions
and no references), (b) a pointer in an untyped data region (a callback or state table the analyser did not type), or (c) a
computed address. All three are found the same way: a raw search of the image for the little-endian dword `0x0087c340` (an
absolute pointer or immediate) and for the five-byte relative-call encoding whose target is `0x0087c340`. That is the first item
in Next dumps. Until it lands, **when the installer runs is OPEN**, and Team B keeps: no session in single player.

### Q1.2 `0x0087d1d0` is the session constructor; a new session is a one-member, local-is-host, offline session — CONFIRMED — disassembly

Body (`func/func_0x0087d1d0.txt`, `0x0087d1d0`–`0x0087d42d`, thiscall on a 0x2d0-byte session object S), in order:

1. Byte S`+0x00` = the running counter `0x024d8578`, which is then incremented (wrapping from 0xff to 0): a per-session serial.
2. S`+0x50` (slot table) = 0. **Connection state S`+0xf4` = 1 and target S`+0xf8` = 1**; bytes `+0xfc`..`+0xff` = 0; S`+0x100` = 1;
   S`+0x220` = S`+0x21c` = S`+0x224` = 0 (no pending transition).
3. Two embedded list nodes at S`+0x104` and S`+0x190` are linked into a ring headed at S`+0x21c` (so the ring holds exactly these
   two nodes after construction).
4. **S`+0x40` is initialised by `0x0087f420`**: dword `+0x48` = 1, dword `+0x44` = 0, byte `+0x4c` = 1, **byte `+0x4d` = 1**,
   **byte `+0x4e` = `0x42`**. gdhw read `+0x4d` as the member capacity (free slots = `+0x4d` − reserved − present) and `+0x4e` bit
   `0x40` as the offline bit (no Steam identity fetched, no leave event sent). So the **default settings of a session are:
   capacity 1, offline, plus bit `0x02`** (unread meaning). The same 0x10-byte settings initialiser is called by eight other
   functions (`0x00870db0`, `0x00880bd0`, `0x007aece0`, `0x0088b6b0`, `0x0088bfe0`, `0x007ae190`, `0x0088ba80`, undefined
   `0x007aea1f`) — the places that build a settings block before hosting or joining, presumably overriding these defaults
   **[HIGH CONFIDENCE as the role; none dumped]**.
5. S`+0x20` is reset by `0x0087f3b0` (a 16-byte handle slot cleared by `0x00877700`, its identity block at `+0x8` zeroed, byte
   `+0x1c` = 0xff) and then overwritten by `0x00877650` with the 8-byte value at `0x024d78d0` (a global identity pair — the
   local machine's, by the DEFAULT_PLAYER_NAME handling below **[HIGH CONFIDENCE]**). Byte S`+0x3c` = 0; the 0x18-byte block at
   S`+0x8` is zeroed by `0x0087f290`.
6. The two clock triples S`+0x68..+0x7c` = 0, S`+0x84` = 0, S`+0x88` = −1, the timer S`+0x90` armed with 0 (`0x00d9e3c0`:
   now + 0, wrapped into a 1,800,000,000 ms ring), S`+0x94` = 0, float S`+0x80` = 0, S`+0xc4`/`+0xd0`/`+0xdc`/`+0xe8` = 0,
   byte S`+0x22c` = 0.
7. **A second settings block at S`+0x230` gets the same `0x0087f420` defaults.** S`+0x240` = −1, byte `+0x244` = 0, S`+0x2c8` = 0,
   S`+0x2cc` = −1 (the "leaving" fields gdhw saw in `0x0087c2e0`), **member list head S`+0x54` = 0**.
8. A 0x14-byte identity record on the stack is zeroed twice (`0x0086bc00`, `0x0086bbb0`) and passed to **`0x0086b9f0`**(record,
   0, S, `0x024d78d0`, 1), which takes a member record from the member pool at `0x024d48c8` (`0x0086b880`), writes the 64-wchar
   name UNKNOWN at member`+0xc2`, zeroes member`+0x140`, and initialises it with `0x008699c0`. The result M is stored into
   **both S`+0x5c` (local member) and S`+0x58` (host member)**. S`+0x60` = S`+0x64` = 0.
9. `0x0086ff30`(M`+0x42`, 64): fill the member's 64-wchar display name from `0x00870450`; if that fails, copy the localised string
   for the key `DEFAULT_PLAYER_NAME` (`0x0084a1b0`) and terminate it. Then `0x008681e0`(M): copy the name at M`+0x42` to M`+0xc2`
   through `0x0084a030` with a flag of 1 (a width/normalisation pass — by shape).
10. The two 16-byte handle slots at S`+0x98` and S`+0xa8` are cleared (`0x00877700`, twice each).
11. `0x00d34d60` (returns 1, no effect), then **`0x0087c830`(S, M)**: increments the member count S`+0x60` (now 1), stamps M`+0x14c`
    and M`+0xb204` with the frame counter `0x029f4db4`, links M into the circular member list headed at S`+0x54` (links at
    M`+0xb28c`/`+0xb290`), clears M's handle slot at `+0x142` (`0x0087c1d0`) and calls `0x00879920`(M).

So the constructed object is exactly the shape §26.28 has carried as a HYPOTHESIS for single player: **one member, who is both
the local member and the host, in an offline, capacity-1 session**. This is the default of *every* session, including one that
will go on to host or join online; it is not by itself evidence that single player constructs one.

**Caller: exactly one, `0x0087f1b0` at `0x0087f1c5`** (`xref/xref_0x0087d1d0.txt`). `0x0087f1b0` lies between the subsystem shutdown
`0x0087efe0` and the field initialisers `0x0087f290`/`0x0087f3b0`/`0x0087f420`; by position and by being the only constructor
caller it is the pool's "allocate and construct a session" routine **[HIGH CONFIDENCE; not dumped]**. Its callers are the
next lead for *who creates* a session; the installer's caller is the lead for *who promotes* one.

### Q1.3 `0x0087c390`–`0x0087c560`: pool builder, slot-table event handler with the host assignment, and the session "open" — CONFIRMED — disassembly

| start | shape | what it does |
|---|---|---|
| `0x0087c390` | thiscall(provider, count), the pool-build routine gdhw named | allocates count × 0x2d0 bytes (provider slot `+0x38`, alignment 8) into `+0x2c` and count × 4 bytes into `+0x30`, sets word `+0x22` = count and byte `+0x20` = 1, then pushes every index 0..count−1 onto the free list at `+0x24` with `0x005c5a40` |
| `0x0087c400` | thiscall | constructs an object: vtable `0x0116608c`, `+0x4` = `+0x8` = 0, byte `+0x34` = 0, second vtable `0x0116607c` at `+0x30`, `+0x40..+0x4c` = 0, `+0x38` = `0x201` |
| `0x0087c430` | cdecl(table, event, member) | the **slot-table event handler** registered by `0x0087c520` below. Events are a bit value 2..0x40 dispatched through a 63-entry jump table: **event 2** — if the member's byte `+0x160` (announced) is clear and the member is not the local member of its session (member`+0x154` → session, compare `+0x5c`), call `0x008681a0`(3); **event 8** — announce the member (`0x0087c110`, gdhw: set `+0x160`, event code `0x400`); **event 0x10** — if announced, un-announce it (`0x0087c140`, event code `0x1000`); **event 0x40** — take the session from table`+0x1c`; if session byte `+0xfc` is set, or the pending transition `+0x224` exists and its kind (`+0x4`) is 6, then **store the member held in the slot table's `+0x20` into the session's host pointer `+0x58`** and clear byte `+0xfc`. Every other event does nothing |
| `0x0087c520` | thiscall(provider) | **open the session on a provider**: build the slot table with `0x00879870`(provider, S, 1) into S`+0x50`, register `0x0087c430` on it with `0x00877820`(table, handler, S, 0xff), then `0x0087aa70`(S) |

So the only code that makes `+0x58` differ from `+0x5c` is the event-0x40 branch of `0x0087c430` (the other `+0x58` writers,
from the earlier census, are the constructor — same member as `+0x5c` — and the host-member removal, which clears it). Event
0x40 is therefore the **host assignment** that turns a freshly constructed (host-shaped) session into a client session when a
join completes (transition kind 6) or into a session with a new host after migration (byte `+0xfc` set) **[HIGH CONFIDENCE for
the names; the branch itself is CONFIRMED]**. For `game_get_is_host`: **true for any installed session on which this event has
not reassigned the host.**

### Q1.4 `0x0088b480`–`0x0088b5a0`: nine small mode-manager steps; no session creation — CONFIRMED — disassembly

| start | what it does |
|---|---|
| `0x0088b480` | cdecl(session): `0x0087e000`(S, 0), return 1 |
| `0x0088b490` | if `0x0086dce0`() ≠ 1 → return 2; else `0x009ed510` (no-op stub), and if `0x0086dd40`() is true → `0x0086ddf0`(`0x024e48c4`), return 0; else return 1 + (`0x0086dd30`() ≠ 0) |
| `0x0088b4d0` | `0x0088e040`(`0x024e48d0`) true → return 1, false → return 2 |
| `0x0088b4f0` | `0x0101ba60`(); session = `0x0087ba10`(); `0x0088a080`(session, 1); return 1 |
| `0x0088b510` | `0x00867770` (tests the module byte `0x024d4461`), `0x0088a040`, tail-jump `0x0086cd50` |
| `0x0088b520` | if byte `0x024e4826` is clear: set it, `0x008adc40`(provider `0x01493978`), `0x008672b0` (the "reset part of the module if up" entry), return 0. Else return (`0x0087cac0`() ≠ 0) via the thunk `0x008672e0` |
| `0x0088b560` | `0x008676f0` (module shutdown), clear byte `0x024e4826` |
| `0x0088b570` | release the buffer `0x024e4828` through the provider's slot `+0x60` and clear the pointer |
| `0x0088b5a0` | the once-guarded init gdhw read (starts the next window) |

The return values 0/1/2 are the convention of the state machine in Q1.6 (1 = still running; any other value is the state's
result handed to its exit callback). Nothing here creates, constructs or installs a session. **[CONFIRMED — disassembly.]**

### Q1.5 `0x0088b7c0`–`0x0088b8a0`: session-state query tail, "begin leaving", "destroy when idle", state selector — CONFIRMED — disassembly

- `0x0088b7c0`–`0x0088b7fc` is the tail of a function (head before the window) that holds a session in a register and its
  `+0xf4` in another: returns **2** when the session is idle in fvfp's sense (`+0xf4` < 2, no pending transition, `+0xf4` = `+0xf8`),
  **0** when fully connected (`+0xf4` ≥ 2, `+0xf8` ≥ 2, no pending transition, equal), **1** otherwise (in transition).
- `0x0088b800`: `0x0101ba90`(); **session = `0x0087ba10`()**; if none → return 0; if the session is not idle → `0x0087df40`(S)
  ("begin disconnect", fvfp); return 1. A "leave the session" step.
- `0x0088b840`: **session = `0x0087ba10`()**; if it is idle → `0x0087ed70`(S) (destroy) and return 0; else return 1. A "wait until
  idle, then destroy" step.
- `0x0088b880`: cdecl(machine): machine`+0xc` (the requested state, Q1.6) = **10 if byte `0x024e48b8` is zero, else 6** — the
  same byte that makes `0x0088c090` pick state 6 when non-zero.
- `0x0088b8a0` begins the next function (again fetching the session with `0x0087ba10`).

**`0x0087ba10` reads `0x024d8538`, not `0x024d8534`** (fvfp Q1.7, from the xref block in its `0x0087ed70` dump: the only
writers of `0x024d8538` are the setter `0x0087ba50` and the clear in destroy). So the mode manager's leave/destroy steps operate
on the *secondary* session pointer. The Lua-visible singleton is a different variable, set only by the installer. Two pointers,
two roles: HYPOTHESIS — `0x024d8538` is "the session being set up or torn down by the mode manager", `0x024d8534` is "the
session the running game belongs to", and the installer is the promotion from the first to the second when play starts (which
is also why it flushes the deferred callbacks and publishes the host answer). The callers of the setter `0x0087ba50` were never
dumped and are now on the list.

### Q1.6 `0x0088c090` restarts the mode-manager state machine and brings the network module up — CONFIRMED — disassembly

`0x0088bb30` (dumped as a callee) is a generic state machine over an object with: `+0x0` a table of 12-byte entries (enter,
update, exit callbacks per state), `+0xc` requested state, `+0x10` current state, `+0x14` status, byte `+0x18` "force re-enter",
`+0x1c` a context pointer. Each pass, while `0x00615380`(object) allows: if the status is 1 (running) call the current state's
update with the context; otherwise, if the requested state differs or force is set, switch to it, clear force, set status 1
and call its enter. A result other than 1 becomes the status and is handed to the state's exit callback (object, result,
context). The argument bounds the number of passes (0 = unbounded).

`0x0088c090` (body `0x0088c090`–`0x0088c1cc`): clear `0x024e4820`; **`0x008675c0` (network module init)**; `0x008adbb0`(provider)
— runs seventeen set-up routines against the provider (`0x00889ca0`(provider, `0x01302860`, 0x28a, 0x10, 0x400000) then sixteen
one-argument calls; message-handler registration by shape, none dumped); store the callback `0x0088b5d0` into `0x024d787c` (invoked later through the pointer by
`0x00874f30`); `0x0086fd90`(1) and `0x0086fdb0`(byte `0x014f3d34` ≠ 0) store two globals (`0x024d626c`, `0x024d6268`); five
labelled no-op hook calls (`0x009ed510` with ids `0x10000003`, `0x20000005`..`0x20000008`) around `0x0045afc0` (builds a 32-byte
bit set of the registered 0x100-byte records whose byte `+0x103` bit 1 is set, under the lock `0x031d154c`); then **reset the
machine at `0x01304ab4`** (its fields are the globals `0x01304ab4` + offset): `+0x8` `0x01304abc` = 0, `+0xc` `0x01304ac0`
(requested state) = 0, `+0x10` `0x01304ac4` (current state) = 0, `+0x14` `0x01304ac8` (status) = 0, byte `+0x18` `0x01304acc`
(force) = 1, `+0x1c` `0x01304ad0` (context) = 0; then choose the requested state:
**0 if byte `0x024e48b2` is zero; else 1 if byte `0x024e48b8` is zero; else 6**, in which case also allocate a 100-entry dword
array at `0x01304ad4` through the provider (`0x0088bf90`) and set byte `0x024e4827`; finally run the machine once with no pass
limit (`0x0088bb30`(0)). **[CONFIRMED — disassembly.]** The state table itself is static data at `0x01304a30` (the machine's `+0x0`,
file-backed) and was not dumped; `0x0088c380` compares the current state with 5 and undefined code at `0x0088cf09` with 10, so the
table has at least eleven states. `0x024e48b2` is written by `0x0088bb00` and `0x0088c1d0`; `0x024e48b8` by `0x0088bb00` (neither
dumped). HYPOTHESIS: `0x024e48b2` = "a multiplayer mode is requested", `0x024e48b8` = "join (rather than host)" — from the
6-vs-10 and 1-vs-6 choices and nothing else.

**Reference to `0x0088c090`: one, a stored pointer at `0x0088cf84` inside `0x0088cf70`** (not dumped), i.e. it is itself a callback
handed to something. When that fires is OPEN.

`0x00872760` (the other init caller; its only caller is `0x007ade60`): `0x008675c0`, then builds the server-browser pool: a
0x11800-byte block named `findserver_pool` from the allocator object `0x01493a78` (`0x00db52d0`), 64 records of 0x460 bytes linked
into a ring (`0x024d6838` head, links at `+0x454`/`+0x458`), count byte `0x024d6830` = 0, bytes `0x024d6831` = 1 and `0x01304378` = 1.
`0x00872830` (not dumped) frees it and tail-jumps to the shutdown wrapper. **[CONFIRMED — disassembly + xref.]** So the
server-browser setup brings the module up as a side effect — a menu path (HYPOTHESIS).

### Q1.7 `0x00867740` is the full mode-manager teardown; what it does with flag 4 — CONFIRMED — disassembly

Only when the module byte `0x024d4464` is set: `0x00875910` (`0x024d7880` = 0), `0x0086e980`, `0x0087a080` (if `0x0087ae70` is
true twice and byte `0x013043b4` is 1: `0x0087aea0`, clear it, `0x013043b8` = −1, `0x013043c8` = 0; then `0x0087ae60`; always clear
`0x013043b4`), **`0x0088bdf0`**, no-op, **`0x0087eea0`**, then tail-jump to the module shutdown `0x008676f0` (gdhw Q1.4).

`0x0088bdf0` (guarded by the once-byte `0x024e4824` that the init `0x0088b5a0` sets): release `0x024e4820` if set (`0x008798e0`),
`0x0088d080`, the deferred-callback flush (`0x00894430` via `0x00894600`), one `0x009ed5e0` per entry of the list at
`0x03171a64`+`0x1f0` (count `+0x1f8`, objects from `+0x58`, field `+0x2088`), `0x008938b0`, `0x009f5220`(1), `0x009ed580`,
`0x009da270`, then **five calls with the literal 4: `0x00905e70`(4), `0x009075e0`(4), `0x00905f60`(4), `0x00905fc0`(4),
`0x00905ec0`(4)**, clear `0x024e4824`, tail-jump `0x00831f60`.

`0x0087eea0`: under the pool lock, walk every pooled session from the head index `0x024d856c` through the link table; destroy
the idle ones (`0x0087ed70`) and begin disconnecting the rest (`0x0087df40`); byte `+0xfd` forces destruction.

**Flag 4.** `xref/xref_0x02609398.txt`: the global the installer's host branch reaches is written by **`0x00905ec0`** (at
`0x00905ed3`), `0x00908ec0` and `0x00908fb0` — and **not** by `0x00905ea0`. So the installer's two tails write *different*
globals (host → `0x02609398`; client → somewhere else), which removes gdhw's "set / clear the same flag" reading. The teardown
above calls the host-branch routine `0x00905ec0`(4) as part of a family of five routines taking 4. What is CONFIRMED by call
identity alone: **the state `0x00905ec0`(4) produces is both the state a freshly installed host session selects and the state the
mode-manager teardown leaves behind.** Whether "4" is a flag number, a player slot or a group id, and what the global means, is
OPEN (bodies not dumped). The three Lua queries never read it.

### Q1.8 Answer to the decisive question

| question | answer | label |
|---|---|---|
| What calls the installer `0x0087c340`? | Nothing the disassembler produced; not the three candidate blocks, not the constructor's caller chain as far as read | CONFIRMED (negative) |
| Is it called on a plain single-player start or load? | Unreadable from these dumps | OPEN |
| Does a one-member offline session exist as a designed state? | Yes: it is the **default-constructed** session (capacity 1, `+0x4e` = `0x42`, local = host, one member) | CONFIRMED |
| `game_get_is_host` if single player installs a session | true (host is reassigned only by slot event `0x40` on join/migration) | CONFIRMED (mechanism) |
| `game_get_is_host` in single player | **OPEN — implement false (no session)** | OPEN |
| When does network init run? | from `0x0088c090` (mode-machine restart, itself a stored callback of `0x0088cf70`) and from `0x00872760` (server-browser pool, from `0x007ade60`); single-player start: OPEN | CONFIRMED (callers) / OPEN (timing) |
| When does shutdown run? | `0x00867740` from `0x007ae950`, `0x00872830`, undefined `0x007aea30`; and `0x008676f0` directly from undefined `0x0088b560` | CONFIRMED (callers) / OPEN (timing) |

---

## Q2. Tutorial state 3

### Q2.1 `0x00716360`–`0x007163e0`: the state getter and a save export/import pair for entries 0–188 — CONFIRMED — disassembly

- `0x00716360` (defined): cdecl(index) → the entry's state `+0x0c` (base `0x0151d600`, 36-byte stride), 0 when index > 209.
- **`0x00716380` (undefined, the `0x00716385` reference): cdecl(out)** — copies the state dword of every entry from index 0 up
  to, but not including, the entry at `0x0151f0a0` into a flat dword array. `0x0151f0a0` − `0x0151d60c` = 0x1a94 = 189 × 36, so it
  exports **exactly entries 0–188**.
- **`0x007163b0` (undefined, the `0x007163b5` reference): cdecl(in)** — the inverse: stores a flat 189-dword array back into the
  states of entries 0–188, **unvalidated**.

The split at 189 is the same split as the fill routine's (entries 189–209 start in state 1; the 7-id hash table covers
189–195) and as the save loader's section 5 (hash-keyed pairs for 189–195). So the engine persists the base-game tutorial states
as a positional 189-dword block and the DLC additions as hash-keyed pairs **[HIGH CONFIDENCE — the pair is CONFIRMED, "it is
the save path" follows from the 189 boundary and the section-5 parallel; the caller of either routine is in undefined code and
has no reference]**. **A loaded save can therefore put any of entries 0–188 into state 3** if the game ever stored 3 — which again
depends on an unread writer.

### Q2.2 `0x0071716b`–`0x00717240`: the region after the serialiser is the receiver of the opcode `0x54` record, not of the queue stream — CONFIRMED — disassembly (field order); HIGH CONFIDENCE (identification)

`0x00717170` (defined): a bit-stream helper that reads a 16-bit value if at least 16 bits remain (`0x008811f0`(&local, 16)),
returning the value with its upper half set to 0xffff on success and 0 when the stream is exhausted. Its only caller is
`0x007171c0`.

**`0x007171c0`** (undefined; frame 0xac with a cookie; this is the head gdhw asked for): with a stream object as its argument it
reads, in order: **one bit** (`0x004d4750`), **16 bits** (via `0x00717170`), **4 bytes** (`0x008810b0`), **one value through
`0x00a29be0`** (not dumped; a byte by the §6.19 record), **4 bytes**, **one bit**; then the session clock `0x0086be20` minus the
first 4-byte value gives the elapsed time, and the gdhw tail takes over (if the lead bit is set: rebuild the tutorial with the
remaining duration and the two flags, state 4 for the rich widget; if the lead bit is clear: state 4 unless already 4, and arm
the 1-second timer).

§6.19 step 12 says the host's `tutorial_start` writes an opcode `0x54` record as: a 1, the index A, a dword from `0x0086be20`,
P4 (byte), N, B5 (bit). That is **bit, 16-bit index, 32-bit clock, byte, 32-bit duration, bit — the exact read order above**.
The queue serialiser `0x00717020`, by contrast, writes a count byte, one clock, and per entry 16 + 8 + 32 + 1 bits (gdhw Q2.3).
So gdhw's pairing of this region with the queue serialiser is **withdrawn**: `0x007171c0` is the **receiver of the `tutorial_start`
replication record**. The "lead bit clear" path, which writes 4, is reached by a record of the same opcode that the dumped
`tutorial_start` never produces (it always writes 1); a second sender that writes 0 — a "tutorial finished" notification —
exists somewhere **[HIGH CONFIDENCE by elimination; unread]**. State 4 is thus also what a client reaches when the host reports a
tutorial finished.

The queue serialiser's real consumer: **`0x008ba5d0`** (`func/func_0x008ba5d0.txt`, single caller `0x008b6bb0`, not dumped) is a
join-in-progress state sync for one target member M: it opens an opcode **`0x3c`** record, writes a sub-tag byte 0 and the mission-
object flag block from `0x006d1530`, commits it to M (`0x0086f110`(M, 0, 0, 0)); opens a second `0x3c` record with sub-tag 1 and
writes, in order, `0x005ef540`, **`0x00717020` (the queued-tutorial list)**, `0x006f9250`, `0x005e8cf0`, `0x0084a970`,
`0x00a6b060`, commits it to M; then `0x008c3de0`(M), two timer re-arms (`0x005a79c0`, `0x006fe370`), `0x0084c790`, and, unless
`0x00bc55a0` says otherwise, an opcode **`0x56`** record carrying one bit from `0x008b70a0`. **[CONFIRMED — disassembly; "join-in-
progress sync" is HIGH CONFIDENCE from the per-member commit and the contents.]** The receiver of the `0x3c` sub-record 1 is where
the queue is rebuilt on a client; it was not in this job.

Side find: `0x006d1530` compares object names (`0x03171a64` list at `+0x2bc`/`+0x2c4`, name at object `+0x18`) against the **string
at `0x01124348` with a 3-character limit**. That address is the still-OPEN tutorial name at index 176 (kyoi: "shorter than 4
characters"). So name 176 is a string of at most three characters that also serves as an object-name prefix. A `bytes` dump of
8 bytes at `0x01124348` resolves it.

### Q2.3 `0x007162a0` and who asks "is it in state 3" — CONFIRMED — xref

`0x007162a0`(index) returns index ≤ 209 and state == 3. Callers: `0x006227b0` (four call sites), `0x00625a30`, `0x00705550` (two),
and **`0x00a60060` = `tutorial_active` (§20.5)**. So the engine's own name for the state-3 test is *active*: **state 3 is the
"active" state** by the registered name **[CONFIRMED — the name; what being active entails is still HYPOTHESIS: displayed and
waiting for the player]**. The three engine callers are gameplay code that behaves differently while a particular tutorial is
active (not dumped; their identity would name which tutorials matter).

### Q2.4 `0x00715f20` runs from the systems initialiser `0x005d2400` — CONFIRMED — xref + disassembly

`0x00715f20`: fill the table (`0x007178c0`), then `0x007b1cb0`(name `tutorial`, 1) — loads the UI document of that name in mode 1
(the mode-1 path closes every document listed in the three name arrays at `0x012fcc68`/`0x012fccb8`/`0x012fcd08`, which is the
shape of "make this the foreground document"), then `0x00e1f2f0`(name) — finds the live document whose `+0x57c` equals the name's
hash (`0x00d9e740`) in the ring at `0x02a4d17c` — and stores that document's `+0x580` into `0x0151d5a8`. **Caller: one,
`0x005d2400` at `0x005d2558`** — the routine §(fade) lists among the init callers (`0x005d14b0`/`0x005d1a30`/`0x005d2400`), which
also uses the allocator `0x01493a78`. So the tutorial subsystem is initialised with the other game systems, once, not per level
**[HIGH CONFIDENCE — `0x005d2400` not dumped]**.

### Q2.5 Writer census: now provably incomplete in one way — what to dump next

Direct references to the state field (`0x0151d60c`) are exhausted: every writer is read, and none writes 3 except the two
unvalidated import paths (section 5 for 189–195; `0x007163b0` for 0–188). But a write of the form "take the queue head from
`0x0151d588`, store 3 into its `+0xc`" is recorded by the disassembler as a reference to `0x0151d588` only, never to the state
field. The queue head is used by twelve functions; three were read (`0x00715bf0` writes 2; `0x00716940` resets; `0x00717020`
serialises) and **nine were not: `0x00715a30`, `0x00715ad0`, `0x00715f60`, `0x00716000`, `0x007161e0`, `0x00716220`, `0x00716300`,
`0x007166e0`, `0x00716d90`**. The state-3 writer, if it exists in live code, is in one of these (or in `0x00715c80`, the
index helper the receiver calls). HYPOTHESIS: the queued prompt moves 2 → 3 when the HUD actually shows it, in one of the queue
consumers. For Team B nothing changes: no writer of 3 is confirmed; `tutorial_advance` and `tutorial_active` return false.

---

## Q3. Store-flag triggers: the three function heads and the two helpers

All localisation keys below are functional identifiers the engine resolves by name (owner's ruling, 2026-10-01); no UI text is
quoted.

### Q3.1 `0x005fb930` = "deliverable vehicles at this location" — CONFIRMED — disassembly

`0x005fb930`(handle pair*): resolve the pair as a live object (`0x00458230` on the object map `0x024433a8`); require its flag byte
`+0x33` bit `0x10` clear, its class row (`0x02cc9900`[byte `+0x34`]) to have bit `0x10` at `+0x9` (the store-location class, the
same test `0x005fa820` makes), `0x00853b10` to report it loaded, and the location's type list `0x012ece20`[byte `+0x7a`] to exist;
then tail-call **`0x005facf0`** on the location. `0x005facf0` (now read): walk the garage list for the location's type (head at
`0x012ece20`[type]; type 0's list is `0x014a1d00`, the list the save loader's section 8 fills); for each record whose vehicle
info (`0x00ac1840`(record`+0xe`)) exists and fits the location (`0x005faac0`(location, info, 0, 0)), count it **unless**
`0x00867830`() is true and the record's own handle pair resolves to a live, loaded object of the vehicle class (row `+0x6` bit
`0x80`) — i.e. unless that stored vehicle is already spawned in the world. Returns the count. Callers: undefined `0x005fbdb6`
(F_B below), undefined `0x005b83fd`, and the Lua function at `0x005fc2b0` (one of the store cluster; it reads `0x014a1d28` —
identity not in the spec, see Next dumps).

### Q3.2 F_A = `0x005fb330`: "use the vehicle-delivery store at location L" — CONFIRMED — disassembly (body); HIGH CONFIDENCE (trigger role)

cdecl(L). `0x005f8410`(L) → location object, or return false. If `0x006a3910`() == 8 → HUD message `CANT_USE_MECHANIC`
(`0x0084a1b0` then `0x007ff3e0` with the style record `0x012ffd14`) and fall into exit 1. Else if `0x005facf0`(location) > 0 → exit 2:
**enter the store there (`0x005fa820` with the location's handle pair), store state `0x014a1ce4` = 4, return true**. Else a
"need vehicles" HUD message chosen by the location's type dword `+0x7c`: 0 → `HUD_GARAGE_NEED_VEHICLES`, 1 →
`HUD_HELIPORT_NEED_VEHICLES`, 2 → `HUD_HANGAR_NEED_VEHICLES`, 3 → `HUD_DOCK_NEED_VEHICLES`, other → the garage key; then exit 1:
state 4, return true. So the four location types are garage, heliport, hangar, dock **[CONFIRMED — the key names]**, and the
flag goes to 1 only when at least one stored vehicle of the right type is deliverable. `0x006a3910` is a global mode/activity
query whose value 8 disables the mechanic (OPEN: not dumped).

### Q3.3 F_B = `0x005fbd10`: the walk-in trigger handler — CONFIRMED — disassembly (body); HIGH CONFIDENCE (role)

cdecl(L, object O). O must exist and its class row must have bit 2 at `+0xa` (by the family of class tests in this document, the
human/player class — HIGH CONFIDENCE); `0x009b9160`(O) must be false (O not in a vehicle, by the parallel with F_C — HYPOTHESIS);
`0x005f7e60`(L) → the location. Then: `0x00a00eb0`(5) true → return false; `0x006a3910`() == 8 → return false; `0x005f7560`(location)
false → HUD message `HUD_TRIGGER_DISABLED_COOP_USING` and return false (the store is in use by the other co-op player — from the
key name). Otherwise the gdhw tail: loop over candidates calling `0x005fb930` and **enter the store (`0x005fa820`) at the first
location with a deliverable vehicle**, return true. So the flag's second non-Lua path to 1 is the player entering a store
trigger volume on foot, with the co-op "in use" lock and the mode-8 lock as gates.

### Q3.4 F_C = `0x005fbf60`: the drive-in handler (mechanic / garage) — CONFIRMED — disassembly (body); HIGH CONFIDENCE (role)

cdecl(L). Local player P = `0x009da4e0`(); its current vehicle V from the handle pair at P`+0x16c0`/`+0x16c4` (must resolve to a
live, loaded object of the vehicle class with `+0x33` bit `0x10` clear) and V`+0x1a3c` = 0; `0x005f7e60`(L) → location; `0x00a00eb0`(5)
true → false. If the location's byte `+0x7a` ≤ 2 and `0x005f9230`(location) is non-null, `0x005fbaa0`(location, V, 0, 0) must
accept. If `0x005f91c0`(location) (the owning object) is non-null, `0x00a00a40`(it) must be true. Then **`0x00a9bbe0`(V) = the repair
cost**:

- cost > 0 (**mechanic path**): the record obtained through `0x009601d0`(cost) must hold at least the cost at `+0x1ca0` (the
  player's cash, by what follows), else false; full repair (`0x00a9f980`(V, 1.0, −1, 1, 1)), then `0x009601d0`(−cost) (the debit), a stat event (`0x0094d920`(…, 0xb)), a HUD message
  `VEHICLE_REPAIR_HUD_MESSAGE` (`0x0084a280` + `0x007fc8a0` with `0x014a293c`), and unless V`+0x1704` bit 4 is set a wash/repaint
  pass (`0x00ad39d0`(V, 0, 1) or `0x00ad33b0`(V, 1)); return true. **The store flag is not touched on this path.**
- cost = 0 (**garage path**): the owner must exist; `0x00a978b0`(V, 0, 0) true; V`+0x16c0` bit `0x80000` clear; then if
  **`0x005f75c0`(0)** is true: write the store-target globals (`0x014a1dc4` = location, `0x014a1dc8/cc` = V's handle pair) and call
  **`0x00820cd0`(owner)** — the vehicle-driven exit, flag → 0 if its UI call succeeds — and return true; if false: a notice dialog
  (`0x007c3d80` with `MENU_TITLE_NOTICE` and `CUSTOMIZATION_GARAGE_FULL`) and return false.

So `0x00820cd0` runs when the player **drives an undamaged vehicle into a garage/mechanic location and the garage has room**;
it stores the vehicle and closes the store UI (flag 0). The sibling `0x005fc230`(L, player object) resolves that player's current vehicle the same way
and calls `0x005fbb60`(location, V) (sets `0x014a1dc0..`, state 1 unless 4 — from the xref block) — a non-UI "put vehicle in garage"
variant **[HIGH CONFIDENCE]**.

### Q3.5 `0x0080e2f0` and the Lua reachability of the writers — CONFIRMED — disassembly

`0x0080e2f0` is a method of a UI object: if its `+0xd8` equals 1, resolve the nearest location of the type id held in `+0xdc`
(`0x005f7d90`: through the player's garage context `+0x209c` when set, else the closest object in the `0x03171a64` list at
`+0x1b4`/`+0x1bc` whose `+0x7c` matches), call `0x005fa820` with its handle pair, and set `+0xdc` = −1. Its only caller is
`0x0080e5c0` (not dumped) — the store UI re-entering the garage at the nearest location of a requested type after a menu choice
**[HYPOTHESIS]**.

Lua reachability: F_A, F_B, F_C and `0x005fc230` use no Lua API and have **no references** (they are undefined code); their callers
are the trigger/collision dispatch, themselves unresolved. `0x005fc2b0` **is** a Lua function (it calls the argument-count
primitive) in the store cluster and calls `0x005fb930`, but it only *reads* the deliverable count; it does not write the flag.
So for Team B the rule stands: in a script-only run the flag moves only through `store_vehicle_change_mode`; the engine paths
need the player to walk or drive into a store location.

---

## Spec changes

Exact text, keyed to the current sentences. Job id for citations: `20261001T124128-team-a-jfue`.

### §26.28

1. **Replace** "It has no caller in defined code (an earlier reference search for `0x0087c340` was empty and no function dumped by
   this job calls it), so its callers are themselves undefined code and **when it runs is OPEN**. The best places to look are the
   undefined lifecycle blocks `0x0088b4c0`–`0x0088b5a0`, `0x0088b7c0`–`0x0088b8a0` and `0x0088cac0`–`0x0088cb60` **[HIGH CONFIDENCE
   as the place to look; no call is shown yet]**."
   **with:** "**[2026-10-01, job `20261001T124128-team-a-jfue`:]** It has **no reference of any kind** in the disassembly: the reference
   listing for `0x0087c340` is empty, although the same listing mode records calls from undefined code and stored pointers, and the
   three undefined lifecycle blocks `0x0088b480`–`0x0088b5a0`, `0x0088b7c0`–`0x0088b8a0` and `0x0088cac0`–`0x0088cb60` have been read
   in full and call neither the installer nor the session constructor. The installer is therefore reached only from bytes the
   disassembler never turned into instructions, from an untyped pointer table, or through a computed address; a raw byte search
   for its address is the next step. **When it runs is OPEN. [CONFIRMED — disassembly and xref for the negative.]**"

2. **Replace** "The host-session constructor is `0x0087d1d0`: it stores one new member into both `+0x5c` and `+0x58` **[HIGH
   CONFIDENCE — from an earlier field census; its body was not dumped]**. Session objects carry an *offline* bit (byte `+0x4e`,
   bit `0x40`); when it is set, no Steam identity is fetched and no leave event is sent **[CONFIRMED — disassembly for the bit's
   gating role; that single player uses an offline session is HYPOTHESIS]**."
   **with:** "`0x0087d1d0` is the **session constructor** (job `20261001T124128-team-a-jfue`): it initialises every field of a
   0x2d0-byte session, sets the connection state `+0xf4` = `+0xf8` = 1, takes one member record from the member pool, names it from
   the local profile (falling back to the localised `DEFAULT_PLAYER_NAME`), stores it into **both `+0x5c` (local) and `+0x58` (host)**,
   links it as the only entry of the member list at `+0x54` with count `+0x60` = 1, and fills the settings block at `+0x40` with
   defaults **capacity `+0x4d` = 1 and byte `+0x4e` = `0x42`, i.e. the offline bit `0x40` set**. So **a newly constructed session is
   by default a one-member, offline, local-is-host session** — the shape this unit has carried as a hypothesis for single player.
   This is the default of every session, including ones that go on to host or join online, so it does not by itself show that
   single player constructs one. The constructor has exactly one call site, `0x0087f1b0` (unread). The host pointer `+0x58` is
   changed away from the local member only by the slot-table event handler `0x0087c430` on event `0x40` (a completed join —
   transition kind 6 — or a host migration — byte `+0xfc`), which copies the member in the slot table's first slot into `+0x58`;
   host-member removal clears it. **Hence, for any installed session on which no such event has run, `game_get_is_host` is true.**
   **[CONFIRMED — disassembly and xref; "join / migration" as the event's meaning is HIGH CONFIDENCE.]** Session objects carry an
   *offline* bit (byte `+0x4e`, bit `0x40`); when it is set, no Steam identity is fetched and no leave event is sent **[CONFIRMED —
   disassembly for the bit's gating role; that single player uses an offline session is HYPOTHESIS]**."

3. **Replace** "Init is called from `0x00872760` and `0x0088c090`; shutdown from the guarded wrapper `0x00867740` and from undefined
   code at `0x0088b560`. The module byte `0x024d4461` is the engine-wide "network module up" test (47 reading functions).
   **[CONFIRMED — disassembly and xref, job `20261001T121532-team-a-gdhw`; when those callers run is OPEN.]**"
   **with:** "Init is called from `0x00872760` and `0x0088c090`; shutdown from the guarded wrapper `0x00867740` and from undefined
   code at `0x0088b560`. The module byte `0x024d4461` is the engine-wide "network module up" test (47 reading functions).
   **[CONFIRMED — disassembly and xref, job `20261001T121532-team-a-gdhw`.]** **[2026-10-01, job `20261001T124128-team-a-jfue`:]**
   `0x0088c090` brings the module up and then **restarts the mode-manager state machine** — a generic three-callback machine
   (`0x0088bb30`: a table of enter/update/exit callbacks per state, a requested state, a current state and a status) whose
   instance lives at `0x01304ab4` with its static table at `0x01304a30` — into requested state 0 when byte `0x024e48b2` is zero,
   1 when byte `0x024e48b8` is zero, and 6 otherwise. `0x0088c090` is itself only a stored callback (held by `0x0088cf70`).
   `0x00872760` is the server-browser pool setup (`findserver_pool`, 64 records), called only from `0x007ade60`, and `0x00872830`
   frees it and tail-jumps into the shutdown wrapper. The shutdown wrapper `0x00867740` is called from `0x007ae950`, `0x00872830`
   and undefined code at `0x007aea30`; it runs the mode-manager teardown `0x0088bdf0`, walks the session pool destroying idle
   sessions and disconnecting the rest (`0x0087eea0`), then the module shutdown. The leave/destroy steps of the mode manager
   (`0x0088b800`, `0x0088b840`) act on the **second session pointer `0x024d8538`** (accessor `0x0087ba10`), not on the singleton
   the three Lua queries read. **[CONFIRMED — disassembly and xref; when any of these callers runs on a single-player start is
   OPEN.]**"

4. **Replace** "`game_get_is_host` in single player stays OPEN. The HYPOTHESIS above (single player keeps a one-member host session)
   stays a hypothesis. A host starts with no session: `game_get_is_host` false, `coop_is_active` false, `Completion_is_client` false."
   **with:** "`game_get_is_host` in single player stays OPEN. The HYPOTHESIS above (single player keeps a one-member host session)
   stays a hypothesis, now with the fact that this is exactly a default-constructed session (job `20261001T124128-team-a-jfue`).
   A host starts with no session: `game_get_is_host` false, `coop_is_active` false, `Completion_is_client` false."

5. **Replace** "`0x007178c0` is called exactly once, as the first action of `0x00715f20`, the tutorial subsystem initialiser, which
   also creates the tutorial UI handle `0x0151d5a8` (§10.4); when `0x00715f20` runs is OPEN."
   **with:** "`0x007178c0` is called exactly once, as the first action of `0x00715f20`, the tutorial subsystem initialiser, which then
   loads the UI document named `tutorial` in the foreground mode (`0x007b1cb0`(name, 1)), finds the live document by name hash
   (`0x00e1f2f0`) and stores its `+0x580` into `0x0151d5a8` (§10.4). `0x00715f20` is called once, from the systems initialiser
   `0x005d2400` (job `20261001T124128-team-a-jfue`) — so the table is filled with the other game systems, not per level
   **[CONFIRMED — xref; HIGH CONFIDENCE that `0x005d2400` is the one-time systems initialiser]**."

6. **Replace** the paragraph "**Tutorial state 3 (job `20261001T121532-team-a-gdhw`).** … **Who writes state 3 is OPEN.**"
   **with:** "**Tutorial state 3 (jobs `20261001T121532-team-a-gdhw`, `20261001T124128-team-a-jfue`).** The engine's own name for the
   state-3 test is `tutorial_active` (§20.5, which calls the same reader `0x007162a0` as three engine routines `0x006227b0`,
   `0x00625a30`, `0x00705550`), so **3 is the *active* state** **[CONFIRMED — the registered name; what being active entails is
   HYPOTHESIS: displayed and waiting for the player]**. The undefined region `0x00717240`–`0x0071738f` is the tail of
   **`0x007171c0`, the receiver of the opcode `0x54` `tutorial_start` replication record** (§6.19 step 12): it reads one bit, a
   16-bit index, a 32-bit send clock, one byte, a 32-bit duration and one bit — the record's exact field order — and either
   rebuilds the tutorial with the remaining duration (lead bit set) or marks it state 4 (lead bit clear, a "finished" record that
   `tutorial_start` itself never sends). Both of its state stores write 4. The earlier pairing of that region with the queue
   serialiser `0x00717020` is withdrawn; the serialiser's consumer is the join-in-progress sync `0x008ba5d0`, which writes the
   queued-tutorial list into sub-record 1 of an opcode `0x3c` record sent to the joining member. Tutorial states are
   save-persisted twice over: the generic setter `0x007163e0` is fed by the save loader `0x00b9ae60` (section 5, hash-keyed pairs
   for entries 189–195), and two undefined routines `0x00716380` / `0x007163b0` export and import the states of **entries 0–188** as
   a flat 189-dword array, unvalidated. No code read so far writes 3 in live play; every direct reference to the state field is
   now read. A write made through the queue-head pointer `0x0151d588` would be invisible to that census, and nine users of the
   queue head are unread (`0x00715a30`, `0x00715ad0`, `0x00715f60`, `0x00716000`, `0x007161e0`, `0x00716220`, `0x00716300`,
   `0x007166e0`, `0x00716d90`). **Who writes state 3 is OPEN.** **[CONFIRMED — disassembly for the receiver's read order, the
   export/import pair, the sync's contents; HIGH CONFIDENCE — the receiver is the `0x54` handler.]**"

7. **Replace** the "**Next dumps (job `20261001T121532-team-a-gdhw`, queued as follow-up …)**" paragraph **with** the list under
   "Next dumps" at the end of this file, headed "**Next dumps (job `20261001T124128-team-a-jfue`).**".

8. **Review status line, append:** "Updated 2026-10-01 (job `20261001T124128-team-a-jfue`): CONFIRMED — the installer `0x0087c340`
   has no reference anywhere in the disassembly and the three candidate lifecycle blocks do not call it; `0x0087d1d0` is the session
   constructor and builds a one-member, offline (`+0x4e` = `0x42`), capacity-1, local-is-host session by default, with one call site
   `0x0087f1b0`; the host pointer `+0x58` is reassigned only by slot-table event `0x40` in `0x0087c430`; the mode-manager lifecycle
   steps use the second session pointer `0x024d8538`; `0x0088c090` restarts the mode-manager state machine at `0x01304ab4`;
   `0x00715f20` is called from `0x005d2400`; the region after the queue serialiser is the opcode-`0x54` receiver; entries 0–188 are
   exported/imported by `0x00716380`/`0x007163b0`; the store-flag function heads `0x005fb330`, `0x005fbd10`, `0x005fbf60` (§10.1).
   HIGH CONFIDENCE — event `0x40` = join / host migration; `0x0087f1b0` = allocate-and-construct. HYPOTHESIS — `0x024d8538` is the
   session being set up, `0x024d8534` the session the game is in. OPEN — the installer's caller (raw byte search next);
   `game_get_is_host` in single player (implement false); who writes tutorial state 3 (nine queue-head users next); tutorial name
   176 (a string of at most three characters at `0x01124348`)."

### §8.27

9. **Append to the "[2026-10-01, job `20261001T121532-team-a-gdhw`: …]" note:** "**[2026-10-01, job `20261001T124128-team-a-jfue`:**
   the installer has no reference of any kind in the disassembly, and the three candidate lifecycle blocks call neither it nor
   the session constructor. The constructor `0x0087d1d0` stores the one member it creates into **both** `+0x5c` and `+0x58`, and
   `+0x58` is changed only by the slot-table event-`0x40` handler `0x0087c430` (a completed join or a host migration) — so
   **every session is a host session until a join completes**, and this function is true for any installed session that has
   not been joined to a remote host. Whether single player installs one is still OPEN; implement false. CONFIRMED — disassembly.]**"

10. **Review status line, append:** "Updated 2026-10-01 (job `20261001T124128-team-a-jfue`): CONFIRMED — the host pointer is reassigned
    only by slot-table event `0x40`; a constructed session is host-shaped by default. Still OPEN — the installer has no reference;
    implement false. Next: raw byte search for `0x0087c340`, callers of `0x0087f1b0` and `0x0087ba50` (§26.28)."

### §10.1

11. **Replace** "The heads of all three functions lie before the dumped windows, so the triggers stay OPEN. CONFIRMED — disassembly
    for the tails. Next dumps (follow-up job `team-a/ghidra/jobs/gdhw-followup.json`): range `0x005fb200`–`0x005fb3e0`,
    `0x005fbc80`–`0x005fbdb0`, `0x005fbdf0`–`0x005fc2c0`; function bodies `0x005fb930` and `0x0080e2f0`.]]]**"
    **with:** "**[2026-10-01, job `20261001T124128-team-a-jfue`:** the three heads are read. The first `0x005fa820` caller is
    **`0x005fb330`**(location id): it resolves the location (`0x005f8410`), shows `CANT_USE_MECHANIC` when the mode query
    `0x006a3910` returns 8, and otherwise enters the store only if `0x005facf0` counts at least one deliverable stored vehicle of
    the location's type (a stored vehicle not already spawned in the world); with none it shows one of
    `HUD_GARAGE_NEED_VEHICLES` / `HUD_HELIPORT_NEED_VEHICLES` / `HUD_HANGAR_NEED_VEHICLES` / `HUD_DOCK_NEED_VEHICLES` by the
    location's type `+0x7c` (0–3) and still sets store state 4. The second is **`0x005fbd10`**(location id, object): the object
    must be of the human/player class and not in a vehicle, `0x00a00eb0`(5) must be false, the mode must not be 8, and
    `0x005f7560`(location) must be true (else `HUD_TRIGGER_DISABLED_COOP_USING`); it then enters the store at the first location
    whose deliverable count (`0x005fb930`, which wraps `0x005facf0`) is positive — the on-foot store trigger. The `0x00820cd0`
    caller is **`0x005fbf60`**(location id): the local player's current vehicle is resolved; if its repair cost (`0x00a9bbe0`) is
    positive the mechanic path repairs it for cash with `VEHICLE_REPAIR_HUD_MESSAGE` and **does not touch the flag**; if the cost
    is zero and `0x005f75c0`(0) reports room, it writes the store-target globals and calls `0x00820cd0` (the garage stores the
    vehicle and the UI closes, flag 0); with no room it shows `CUSTOMIZATION_GARAGE_FULL` under `MENU_TITLE_NOTICE`. So the
    non-Lua writers fire when the player walks (flag → 1, if a vehicle can be delivered) or drives an undamaged vehicle (flag →
    0, if the garage has room) into a vehicle-store location. `0x0080e2f0` is a store-UI method that re-enters the nearest
    location of a requested type after a menu action (its caller `0x0080e5c0` unread). None of the four trigger routines is
    Lua-reachable and none has a reference in the disassembly (they are undefined code). CONFIRMED — disassembly for the bodies;
    HIGH CONFIDENCE — their trigger roles, from the HUD keys and the vehicle/player resolution.]]]**"

12. **Review status line, append:** "Updated 2026-10-01 (job `20261001T124128-team-a-jfue`): CONFIRMED — the three function heads
    (`0x005fb330`, `0x005fbd10`, `0x005fbf60`) and the helpers `0x005fb930`/`0x005facf0`; the flag's engine-side 1 needs a deliverable
    stored vehicle, its engine-side 0 a drive-in with garage room. HIGH CONFIDENCE — these are the walk-in and drive-in store
    triggers. OPEN — their own callers (undefined, no references); `0x006a3910`'s value 8; `0x0080e5c0`."

### §10.4

13. **Replace** "The only unread direct references to the state field are in undefined code at `0x00716385`/`0x007163b5`, between
    the state getter and the generic setter. OPEN — who writes 3; next dumps in follow-up job `team-a/ghidra/jobs/gdhw-followup.json`
    (§26.28). HYPOTHESIS — 3 = displayed and waiting for the player."
    **with:** "Updated 2026-10-01 (job `20261001T124128-team-a-jfue`): the two undefined references at `0x00716385`/`0x007163b5` are
    an export/import pair (`0x00716380` / `0x007163b0`) that copies the states of entries 0–188 out to, and back from, a flat
    189-dword array, unvalidated — the positional save block for the base-game tutorials, matching section 5's hash-keyed pairs
    for 189–195 (CONFIRMED — disassembly; "save path" HIGH CONFIDENCE). Every direct writer of the state field is now read and none
    writes 3; a write through the queue-head pointer `0x0151d588` would escape that census, and nine queue-head users are unread
    (§26.28). The state-3 reader `0x007162a0` is also the body of `tutorial_active` (§20.5), so **3 is the engine's "active" state**
    (CONFIRMED — the registered name). The region after the serialiser is the receiver of the opcode-`0x54` `tutorial_start`
    record, not of the queue stream (§26.28). OPEN — who writes 3. HYPOTHESIS — active = displayed and waiting for the player."

14. **Append to the "[2026-10-01, job `20261001T121532-team-a-gdhw`: the writer of `0x0151d5a8` …]" note:** "**[2026-10-01, job
    `20261001T124128-team-a-jfue`: `0x00715f20` loads the UI document named `tutorial` in the foreground mode and stores that
    document's `+0x580` into `0x0151d5a8`; its only caller is the systems initialiser `0x005d2400`. CONFIRMED — disassembly and
    xref.]**"

---

## Next dumps (job `20261001T124128-team-a-jfue`)

In order of payoff. Item 1 needs a new dump mode: a raw byte search over the mapped image.

1. **Raw search for the installer's address** — the little-endian dword `0x0087c340` anywhere in `.text`/`.rdata`/`.data` (a
   pointer or `push`/`mov` immediate), and every five-byte relative call or jump whose target is `0x0087c340` (for each `E8`/`E9`
   byte in `.text`, check whether the following rel32 lands on it). Report each hit's address and whether the disassembler has
   an instruction or a function there. The same search for `0x0087f1b0` (the constructor's caller), `0x00716380`, `0x007163b0`,
   `0x007171c0`, `0x005fb330`, `0x005fbd10`, `0x005fbf60`, `0x005fc230` (all undefined, all reference-less) finds their callers in one
   pass. If `CrreishDump.java` has no such mode, add `find <hex dword>…` (absolute) and `findcall <addr>…` (rel32).
2. `func` depth 1 + `xref`: `0x0087f1b0` (allocate-and-construct), `0x0087ba50` and `0x0087ba40` (setters of the two secondary
   session pointers), `0x0087c520` (open on provider), `0x0088cf70` (holds the `0x0088c090` callback), `0x0088bb00` and `0x0088c1d0`
   (writers of `0x024e48b2`/`0x024e48b8`), `0x007ade60`, `0x007ae950`, `0x00872830`; `ptrs 0x01304a30 36` (the mode-manager state table:
   name the states whose callbacks are the init/leave/destroy steps); `func 0x00905ec0 0x00905ea0 0x00908ec0` (flag 4).
3. Tutorial: `func` depth 1 of the nine unread queue-head users `0x00715a30`, `0x00715ad0`, `0x00715f60`, `0x00716000`,
   `0x007161e0`, `0x00716220`, `0x00716300`, `0x007166e0`, `0x00716d90`, plus `0x00715c80` and `0x00a29be0`; `bytes 0x01124348 8` (name
   176); `func 0x006227b0 0x00625a30 0x00705550` (which tutorials gate gameplay while active); `func 0x008b6bb0` (the join sync's
   caller) and the opcode-`0x3c` receiver (search the handler registrations made by `0x008adbb0`'s sixteen callees for `0x3c`/`0x54`).
4. Store: `func 0x0080e5c0 0x006a3910 0x00a00eb0 0x005f7560 0x005f75c0 0x005f7e60 0x005f8410 0x00867830`; `lua` lookup of the
   registered name at `0x005fc2b0`; the callers of the four trigger routines from item 1.

## Summary of labels

| item | label |
|---|---|
| `0x0087c340` has no reference anywhere in the disassembly; the three candidate blocks call neither it nor `0x0087d1d0` | CONFIRMED — disassembly + xref |
| its caller / whether single player installs a session | OPEN — implement "no session", `game_get_is_host` false |
| `0x0087d1d0` = session constructor; default session = one member, local = host, capacity 1, `+0x4e` = `0x42` (offline), state 1/1 | CONFIRMED — disassembly |
| `0x0087f1b0` = its only caller, allocate-and-construct | CONFIRMED (sole caller) / HIGH CONFIDENCE (role) |
| `+0x58` reassigned only by `0x0087c430` event `0x40`; event = completed join (kind 6) or migration (`+0xfc`) | CONFIRMED (branch) / HIGH CONFIDENCE (meaning) |
| `game_get_is_host` true for any installed session not yet joined to a remote host | CONFIRMED (mechanism) |
| lifecycle steps `0x0088b800`/`0x0088b840` act on `0x024d8538` via `0x0087ba10` | CONFIRMED — disassembly + fvfp xref |
| two pointers = "being set up" vs "the game's session" | HYPOTHESIS |
| `0x0088bb30` generic state machine; `0x01304ab4` instance; `0x0088c090` restarts it (0/1/6) and runs init | CONFIRMED — disassembly |
| `0x024e48b2` = multiplayer requested, `0x024e48b8` = join | HYPOTHESIS |
| init/shutdown callers as listed; their timing on a single-player start | CONFIRMED / OPEN |
| installer's host branch (`0x00905ec0`) writes `0x02609398`, client branch does not; teardown also calls `0x00905ec0`(4) | CONFIRMED — xref / disassembly; meaning OPEN |
| `0x00716380`/`0x007163b0` export/import states of entries 0–188 | CONFIRMED (bodies) / HIGH CONFIDENCE (save path) |
| `0x007171c0` = receiver of the opcode-`0x54` `tutorial_start` record; gdhw's serialiser pairing withdrawn | CONFIRMED (read order) / HIGH CONFIDENCE (identification) |
| `0x008ba5d0` = join-in-progress sync carrying the queued-tutorial list in opcode `0x3c` sub-record 1 | CONFIRMED (contents) / HIGH CONFIDENCE (role) |
| state 3 = the engine's "active" state (`tutorial_active`) | CONFIRMED (name) / HYPOTHESIS (displayed, waiting) |
| who writes 3 | OPEN (nine queue-head users next) |
| `0x00715f20` called once from `0x005d2400`; loads the `tutorial` UI document | CONFIRMED — xref + disassembly |
| tutorial name 176 = a string of at most three characters at `0x01124348`, also an object-name prefix | CONFIRMED (length bound) / name OPEN |
| store: `0x005fb330` (use store at location), `0x005fbd10` (walk-in trigger), `0x005fbf60` (drive-in: mechanic or garage), `0x005fb930`/`0x005facf0` (deliverable count) | CONFIRMED (bodies) / HIGH CONFIDENCE (roles) |
| store triggers' own callers | OPEN (undefined, no references) |
