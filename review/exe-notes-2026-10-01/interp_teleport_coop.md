# `teleport_coop`: what it is, and what makes it finish in single player (Team B blocker: dlc2_m01, m13, m19)

Team A, 2026-10-02. Investigation-only pass against the real executable (job-private copy of the local
Ghidra project at `tools/gp_tc`, program `SaintsRowTheThird.exe`, headless `-readOnly -noanalysis`,
`ghidra/CrreishDump.java`) and against the shipped `game_lib.lua` (extracted read-only from
`misc.vpp_pc` with Team B's `vpp_extract`). Dump output and the extracted script live in this session's
scratchpad (`tc/` sub-folders) and are not committed; everything below is described in prose. **No spec
file was edited.**

**The question.** After the resume-scheduler fix (the 0x00e0cf50 finding), three missions (dlc2_m01,
m13, m19) still sit suspended at `game_lib.lua:2956 (in teleport_coop)`; Team B's stub-hit table shows
`teleport_check_done` called 9,497 times during the drive, all returning nil. Questions: (1) what
`teleport_coop` does; (2) what makes it complete in single player with no co-op session; (3) whether it is
gated on the co-op/host check, and what happens down the no-session branch.

Labels follow the house style: **CONFIRMED — disassembly** = read in a listed instruction stream of this
pass's dumps; **CONFIRMED — script text** = read directly in the shipped Lua source; **HIGH CONFIDENCE** =
follows from a dumped instruction or reference list but the body it points at was not dumped, or the role
name is inferred from strong structural evidence; **HYPOTHESIS** = plausible, not settled; **OPEN** = not
settled, collected at the end.

---

## 0. Starting point (from prior passes, not re-derived here)

- With no co-op session, the session singleton 0x024d8534 is null, so `coop_is_active` (0x00a42bd0) and
  `game_get_is_host` are false (`spec-lua-api-behaviour.md` §3.1, §8.27, §26.28; CONFIRMED there). Reused
  as given.
- `teleport` is 0x00a60e30 (§4.3) and `teleport_check_done` is 0x00a61d90 (§15.17), both in the 1,014-entry
  gameplay registrar 0x00a20840.
- `"#PLAYER1#"`/`"#PLAYER2#"` are registered into the global name map by 0x00a28260 for the player objects
  (`interp_named_object_resolution.md` §1.5).
- The fade state machine (`spec-lua-api-behaviour.md` §26.24): 0x012e6aa8 is the requested fade target
  (3 = out), read by 0x0059f9f0; 0x0059fa10 is the "fade settled" predicate.
- The game-mode registrar 0x00706a40 keeps one record per mode index below 0xb (§26.24); modes 4/5 were
  left unidentified there.

---

## 1. Investigation trail

### 1.1 `teleport_coop` is not an engine function — CONFIRMED (registrar walks + script text)

- `spec-lua-api-behaviour.md` has no entry and no mention of the name.
- The full ordered `{namePtr,funcPtr}` walk of the 1,014-entry gameplay registrar 0x00a20840
  (`tools/scratchpad/lua_gameplay_1014_raw_pairs.txt`) has no `teleport_coop` slot: its teleport block is
  `teleport`, `teleport_check_done`, `teleport_to_object`, `teleport_player_vehicles`, `teleport_vehicle`,
  `teleport_vehicle_to_object` (name slots 0x00a25062–0x00a250d0). The 113-entry loop registrar 0x00845aa0
  (`tools/lua_game845aa0_full.txt`) has no such name either, and the 1,490-name tagged roster
  (`tools/lua_all_registered_1490_tagged.txt`) does not list it.
- **`teleport_coop` is a Lua function defined in the shared library script `game_lib.lua`** (definition at
  line 2940 of the shipped file; Team B's stuck line 2956 is its last statement). It is composed entirely
  of registered engine functions: `assert_screen_is_faded_out` (0x007c9f50), `coop_is_active`
  (0x00a42bd0), `teleport` (0x00a60e30), `waiting_for_player_dialog` (0x00a68ea0) and
  `teleport_check_done` (0x00a61d90), plus the scheduler's `thread_yield`. The same file defines
  `LOCAL_PLAYER` as the string `"#PLAYER1#"` and `REMOTE_PLAYER` as `"#PLAYER2#"` (lines 77–78).

So the "Lua-bound native called `teleport_coop`" in the partner report is really a script helper; the
engine-side behaviour that decides completion is `teleport` + `teleport_check_done`.

### 1.2 What the script helper does — CONFIRMED (script text, described in own words)

Three parameters: the local player's destination navpoint name, the remote player's destination navpoint
name, and an optional "exit vehicles" boolean (its doc comment says default false; the helper passes it
through unchanged, so nil reaches `teleport`, whose own nil test supplies the default). Steps, in order:

1. Calls `assert_screen_is_faded_out()` (a mission-responsibility assertion per the source comment).
2. If `coop_is_active()` is true: calls `teleport` for `REMOTE_PLAYER` to the remote destination.
3. Always: calls `teleport` for `LOCAL_PLAYER` to the local destination.
4. If `coop_is_active()` is true: calls `waiting_for_player_dialog(true)`, then loops "yield one tick,
   then test `teleport_check_done(REMOTE_PLAYER)`" until it is true, then `waiting_for_player_dialog(false)`.
5. Always: loops "yield one tick, then test `teleport_check_done(LOCAL_PLAYER)`" until it is true
   (line 2956 — where all three missions are parked).
6. Returns nothing.

Both waits are repeat-until loops, so the thread always yields at least once per loop before the first
test. With `coop_is_active()` false (single player), steps 2 and 4 are skipped entirely and the helper
reduces to: teleport the local player, then poll `teleport_check_done("#PLAYER1#")` once per tick.

`assert_screen_is_faded_out` is 0x007c9f50, the shared no-op stub already recorded at §6.1/§24.2: it reads
the argument count and returns zero Lua values (CONFIRMED — disassembly; the same address is registered
under six gameplay names and five loop-registrar names). It neither blocks nor fails.

### 1.3 The scripted-request pool behind every `*_check_done` (dumps `tc/func1`, `tc/func2`, `tc/func3`) — CONFIRMED

`teleport_check_done` is a thin query over a small engine module (0x006f6040–0x006f68a2) that tracks
"scripted requests" of several kinds. Read in full:

- **Storage.** 200 records of 40 bytes in one static block at 0x014f4be8 (8,000 bytes), threaded on two
  circular doubly-linked lists: active list head 0x014f4be0, free list head 0x014f4be4. 0x006f60b0 zeroes
  the block, resets the id counter 0x014f6b28 and puts every record on the free list; its one caller is
  0x00708330 (start-of-game set-up, HIGH CONFIDENCE on role).
- **Record layout (as the code uses it).** +0x00/+0x04 list links; +0x08 flags byte, bit 0x1 = done;
  +0x0c kind (a small integer); +0x10 request id (16-bit); +0x18/+0x1c the target object's 8-byte handle
  (copied from object +0x08/+0x0c); +0x20 optional completion callback (a code pointer, may be null).
- **Allocate — 0x006f62b0(object, kind, callback).** Null object → returns 0. First walks the active list
  and releases (0x006f6040) any existing record with the same object handle and the same kind — so a new
  request silently supersedes the old one. Then bumps the 16-bit counter 0x014f6b28 (wrapping 0x1000 back
  to 1) and forms the id as `(machine index << 12) | counter`, where the machine index comes from
  0x008677f0 (session +0x5c → +0x158 byte when a session exists, **0 when there is none**). 0x006f61b0 then
  takes a record from the free list — refusing if 0x00853b10 reports the object unusable or the free list
  is empty — links it onto the active list, stores the handle, clears the done bit, stores kind, id and
  callback. Returns the id, or 0 on refusal.
- **Find — 0x006f6260.** A register-argument helper: the object pointer arrives in ECX and the kind in
  EDI. Null object → no record. Otherwise walks the active list for a record whose handle equals the
  object's handle **and** whose kind equals the requested kind.
- **Status — 0x006f6380(object, kind).** Loads the object into ECX and the kind into EDI and calls the
  finder. Returns **2 if no matching record exists; 1 if the record exists and its done bit is set — and
  in that case releases the record back to the free list (0x006f6040) before returning; 0 if the record
  exists and is not yet done.** CONFIRMED — disassembly, every instruction of the 13-instruction body.
- **Complete — 0x006f66d0(id, a, b).** Ignores id 0. Calls 0x00d48d10 (a one-instruction function that
  returns 1; no effect). If the id's top nibble equals the local machine index: finds the record by id, and
  if not already done sets the done bit and, when a callback is stored, calls it with (a, b). If the
  nibble names another machine: forwards to 0x006f6550, which builds an opcode-0x38 network record with
  sub-type 0, the id and the 8-byte (a, b) payload, addressed to that machine through the session
  (0x0087ba20 → 0x0087b8f0), and commits it with the single-target commit 0x0086f110.
- **Cancel — 0x006f6680(id).** Local id → releases the record; remote id → 0x006f6430, the same
  opcode-0x38 record with sub-type 1.
- **Network receiver — 0x006f6750** (installed by data reference from the message-handler set-up
  0x0088f230): reads the sub-type and the id; sub-type 0 → find by id, set done, call the callback;
  sub-type 1 → release the record.
- **Reaper — 0x006f6820**, called from the large world-update routine 0x00702a50 (HIGH CONFIDENCE that this
  runs every frame): releases every active record whose handle no longer resolves in the handle table
  0x024433a8, whose object is still flagged uninitialised (+0x33 bit 0x10), whose class row lacks bit 0x8
  at +6, or which 0x00853b10 reports unusable.

**Kind census** (from the allocating and querying call sites read this pass): kind 0 =
`turn_to_check_done` (0x00a61dd0 queries kind 0); kind 2 = move-to / pathfind requests
(`move_to_check_done` 0x00a54620, already recorded in §22.15); **kind 4 = teleport** (`teleport`
0x00a60e30 and `teleport_to_object` 0x00a612a0 both allocate kind 4 with a null callback;
`teleport_check_done` queries kind 4).

### 1.4 `teleport_check_done` (0x00a61d90), re-read (dump `tc/func1`) — CONFIRMED

Arguments: one string (character/object name), resolved through 0x00a281a0 — the player-first character
chain (registry lookup gated on class-row +0xa bit 0x2 via 0x00a27e20, then the literal `"#PLAYER#"` →
local player 0x009da4e0, then the plain character resolver 0x005e4dd0 fallback via 0x00a28150). The
resolved pointer and the literal kind 4 go to the status function 0x006f6380; the Lua result is
`status != 0` pushed as a boolean (0x00dfe590); the wrapper returns 1 Lua value.

Therefore the pushed boolean is:

| status | meaning | `teleport_check_done` returns |
|---|---|---|
| 0 | a kind-4 request for this object exists and is still pending | **false** |
| 1 | a kind-4 request for this object exists and is done (record is released by this very call) | **true** |
| 2 | no kind-4 request for this object exists (never issued, already consumed, superseded-and-dropped, reaped, or the name did not resolve) | **true** |

The name argument **is** used: it selects whose request is examined. The pool is shared, but the lookup
is per (object, kind). A second call after a "done" read returns true again (status 2), so polling is
idempotent once the request has finished.

### 1.5 `teleport` (0x00a60e30), re-read (dump `tc/func3`) — CONFIRMED unless marked

Arguments (all extracted by position from the argument count, nil-tested in order): arg 1 string, the
object to move; arg 2 string, the destination object; arg 3 boolean, default false (the slot
`teleport_coop` fills with its "exit vehicles" flag); arg 4 boolean, default true; args 5–7 numbers,
default 0.0, added to the destination position; arg 8 boolean, default true. Returns no Lua values.

Body, in order:

1. Resolves arg 2 through the registry resolver 0x005982e0 and rejects it if 0x00853b10 reports it
   unusable. Reads the destination's position (+0x40/+0x44/+0x48) and builds an orientation from its +0x4c
   record (0x00da4130); adds args 5–7 to the position.
2. Resolves arg 1 through 0x00a281a0 (§1.4).
3. If either resolution fails, returns **without allocating any request** (so a later
   `teleport_check_done` sees status 2 → true).
4. **Allocates a kind-4 request: 0x006f62b0(target, 4, null callback)**, keeping the 16-bit id.
5. Tests the target's class row (0x02cc9900 indexed by object +0x34) byte +0xa bit 0x2:
   - **set → 0x009e2220(target, position, orientation, 1, 1, arg 3, id, arg 4, 1, 0)** — the asynchronous
     path (§1.6);
   - **clear →** converts and streams the destination's zone/cell (0x00861ec0 on destination +0x70), calls
     0x004e4130(target, 0), builds a matrix from the orientation (0x00da38e0), adjusts the target's +0x2b0
     sub-object with a -1.0 constant (0x004e39b0), then **0x00996b50(target, position, orientation, 1, 1, 0,
     id, arg 4, arg 8)** — the immediate path (§1.7). Arg 3 is not forwarded on this path.

**What class-row +0xa bit 0x2 is — HIGH CONFIDENCE: the player class.** Three independent uses agree:
(a) 0x00a27e20, the first step of the player-first character chain, accepts a registry hit only if the
object's row has this bit — the step that resolves the registered `"#PLAYER1#"`/`"#PLAYER2#"` names
before the plain-character fallback is tried; (b) `move_to_check_done` calls 0x009e3200 only on objects with
this bit, and 0x009e3200 writes the object's +0x2888 "script mode" or replicates it in a record tagged
with the literal strings `"player"` and `"script_mode_params"`; (c) the bit-set teleport path is the one that
moves the streaming focus and runs a loading UI (§1.6), which only makes sense for a player. The
descriptor row of the player class itself was not read (it is built at run time).

### 1.6 The player path: an asynchronous teleport game mode (dumps `tc/func4`, `tc/func7`, `tc/func8`, `tc/func9`, `tc/range1`, `tc/xref1`) — CONFIRMED unless marked

**0x009e2220(target, position, orientation, …, id, …)**:

1. If 0x00707800 is true, does nothing. 0x00707800 reads byte 0x01503ec3, whose only writer is
   0x00707810, which sets it and then pushes game mode 0xa — read as the game-exit/shutdown flag (HIGH
   CONFIDENCE on role). Never set during play.
2. **Authority gate.** Takes the local branch if 0x008addb0(target, 0) is true, **or** if 0x00bc5610 is
   true. Otherwise it builds an opcode-0x41 record (inner type 0x17) carrying the target, position,
   orientation, the flag bytes and the id, sends it to the target's owner with the single-target commit
   0x0086f110, and returns — **no local completion on this branch**; completion would come back later as an
   opcode-0x38 sub-type-0 message (§1.3).
3. **Local branch.** 0x00bda550 (a guarded side call, active only when three global bytes are all set;
   not traced further), 0x007b4bf0 on the UI object 0x012fced8 (with a literal 1: if 0x007b3d80 agrees,
   calls 0x007b4990(-1)), then **0x007a7f10** stores the request into a two-slot ring at 0x02242068 (stride
   0x3c; current index 0x0224205c; if the current slot is busy the other slot is used): +0x00 target,
   +0x04 position (12 bytes), +0x10 orientation (36 bytes), +0x34..+0x37 four flag bytes (here 1, 1, arg 3,
   arg 4), **+0x38 the 16-bit request id**, +0x3a busy = 1, +0x3b one more flag (here 0).
4. Finally, unless game mode 8 is already on the mode stack (0x00706ad0(8)) or is the current mode
   (0x00706b90), pushes **game mode 8** (0x00706e00(8)).

**Game mode 8 is the asynchronous teleport.** Its record is registered once at start-up by 0x007a8530
(called from the start-up routine 0x005d25f0) through the mode registrar 0x00706a40 with: enter handler
0x007a7cb0, exit handler 0x007a7de0, a predicate 0x007a7db0, per-frame handler 0x007a82c0, and one further
handler 0x007a84e0. The executable's own log strings around these handlers are `"teleport_enter"`,
`"teleport_exit"`, `"UI_Async_Teleport"` and `"UI_Async_Teleport_Stop"`.

- **Enter (0x007a7cb0)** sets byte 0x0224205b to 1 only when no cutscene is active (0x00720640 false)
  and the requested fade target (0x0059f9f0) is neither 1 nor 3 — i.e. the screen is not already fading
  or faded out; when it is set, the handler itself requests a fade-out (0x0059f8c0). Depending on further
  bytes (0x02242058, 0x02242554) it may show the `"UI_Async_Teleport"` UI; when 0x007b3ba0(0x2e) agrees it
  temporarily overrides a timing field of the screen-fade document (document +0x58c := 5000, old value
  saved at 0x012fc78c); it logs `"teleport_enter"`. So a mission that has already faded out (as
  `teleport_coop` expects) gets no automatic fade; one that has not gets a fade-out here and a fade-in at
  exit.
- **Per frame (0x007a82c0)**, while mode 8 is on the stack:
  1. If mode 5 is not on the stack: if mode 8 is current, stops the async-teleport UI (0x007a7c60) and pops
     the mode (0x00706e70) — i.e. abandons the move; the exit handler still runs (below). Mode 5's identity
     stays OPEN (§26.24); HIGH CONFIDENCE it is the in-game/gameplay mode.
  2. Calls 0x0059fe70 (fade hold-timer service). **If the requested fade target is "out" (3), waits until
     the fade has settled (0x0059fa10)** before going further.
  3. On the first frame for a slot: sets the streaming focus to the slot's destination (0x0085bce0 — the
     single writer of the streaming focus point, `spec-world-streaming.md` §11.3) and applies the slot's
     orientation to a view (0x00da38e0 → 0x00564c20).
  4. If the *other* slot has since become busy (a newer request), retires the current slot, switches the
     ring index to the newer one, refocuses streaming on it and waits for the next frame.
  5. Calls 0x007a8140 — a pump-and-test helper that services several loaders and returns 0x00dafa90's
     "background load outstanding" answer. **While loading is outstanding, it waits.**
  6. Loading done and the slot busy: **moves the target with 0x009e69d0(target, position, orientation,
     four flag bytes)** and clears the slot's busy byte. (0x009e69d0 handles vehicle exit, camera and
     state reset, and itself applies through 0x00996b50 — but with a **zero** request id, so that inner
     applier completes nothing.)
  7. On a later frame with the slot no longer busy: stops the UI and pops mode 8 (once; guarded by
     0x02242064).
- **Exit (0x007a7de0)**: if mode 5 is on the stack, re-applies the current slot's move through 0x009e69d0
  (HIGH CONFIDENCE this is idempotent — same destination); then **0x006f66d0(slot id, 8-byte constant from
  0x01154c40) — the completion of the kind-4 request**; clears the slot's busy byte; then, if the enter
  handler had set 0x0224205b (or the slot's +0x3b flag is set), requests a fade-in (0x0059fc40) whose
  completion callback 0x007a7c10 restores the saved fade-document field, otherwise restores that field at
  once; and logs `"teleport_exit"`.

Net effect for a player target on the local branch: `teleport` returns at once with a **pending** kind-4
request; the request becomes **done** when mode 8 exits — after (at least) one frame that performs the
move once streaming at the destination is idle and any requested fade-out has finished, plus one frame to
pop the mode. If gameplay mode 5 is absent, mode 8 pops without moving and the request is still marked
done. There is no path through mode 8 that leaves the request pending (apart from a superseded ring slot,
OPEN item 4).

### 1.7 The non-player path: immediate application (dump `tc/func3/func_0x00996b50`) — CONFIRMED

0x00996b50(target, position, orientation, a4, a5, a6, id, a8, a9) (also used by `teleport_to_object`,
§22.4, and many engine callers):

- **Direct-apply condition:** 0x008addb0(target, 0) true, **or** the cutscene test 0x00720640 true
  (0x0153b520 > 1 and 0x0153b528 non-zero), **or** (0x00bc5610 true **and** the target is the local player,
  0x0091f5e0). Otherwise it builds an opcode-0x41 record (inner type 0x16) with the target, position,
  orientation, the flag bytes and the id, commits it to the owner (0x0086f110) and returns — no local
  completion.
- **Direct apply** (vehicle-exit handling, physics/animation/camera resets, the actual placement through
  the target's own vtable +0x40, local-player camera snap) — every branch of it converges on 0x009970b3,
  which calls **0x006f66d0(id, 0, 0)** (the 8-byte constant at 0x011741c8 is file-backed zero). So for a
  non-player target with local authority, the kind-4 request is marked done **before `teleport` returns**.

### 1.8 Co-op/host gating and the no-session branch — CONFIRMED unless marked

`teleport` and `teleport_check_done` themselves contain **no** co-op or host test. The co-op dependence is
in three places, each traced here with no session installed (0x024d8534 null, §26.28):

- **The script helper** tests `coop_is_active()`; false → the remote-player teleport, the "waiting for
  player" dialog and the remote wait are all skipped (§1.2).
- **Request ids.** 0x008677f0 calls 0x0087ba20; null session → machine index 0. Every id is then
  0x001–0xfff with top nibble 0, which equals the local index, so 0x006f66d0 always takes the **local**
  completion branch (set done bit) and never the network send 0x006f6550. Ids never collide with a
  remote machine's because no remote exists.
- **Authority gate 0x008addb0(object, 0)** — not the `+0x5c`==`+0x58` host predicate. Null object → true.
  Otherwise it tail-calls **0x00884760(object +0x3c, 0)**, where object +0x3c is the object's network
  record (may be null):
  - if the record is non-null and 0x00bc5610 is true (global 0x029443bc equals 2 or 3) → **false**;
  - else if the record's +0x03 owner byte is 0xff → **true**;
  - else if the record is non-null with +0x4c equal to 0 **and** the network manager 0x024e4818
    (0x00889e00) is non-null → true exactly when the owner byte equals the local machine byte read from
    manager +0x1c → +0x5c → +0x158;
  - in every other case → **true** (in particular: object without a network record, or no network
    manager).

  For `"#PLAYER1#"` in single player this is true (HIGH CONFIDENCE — it can only be false if the network
  manager exists and the player's owner byte differs from the local machine byte, which would mean the
  local player is owned by another machine). And on the player path, even a false authority answer is
  overridden whenever 0x00bc5610 is true (§1.6 step 2); on the non-player path the local player is
  likewise always applied directly when 0x00bc5610 is true (§1.7). The meaning of 0x029443bc's states 2/3
  was not traced (OPEN item 2) but neither value can send a local-player teleport down the replicate
  branch.

So the **no-session branch is the plain local branch**: request allocated with a local id, move performed
locally (player: through mode 8; others: immediately), completion set locally, no network record sent and
nothing awaited from another machine.

---

## 2. Draft spec entries (for transcription)

### `teleport_coop` — `game_lib.lua` script helper, not an engine binding (used by dlc2_m01, m13, m19 at least)

**Kind:** Lua function defined in the shared library script `game_lib.lua` (line 2940 of the shipped file);
**not** registered by any of the engine registrars (absent from 0x00a20840's 1,014 pairs, 0x00845aa0's 113
pairs and the 1,490-name roster). A host must not register a native under this name; it must load
`game_lib.lua`'s own definition. **[CONFIRMED — registrar walks + script text.]**

**Arguments:** 1. string — navpoint name for the local player; 2. string — navpoint name for the remote
player (only used when co-op is active); 3. optional boolean — "exit vehicles", passed through to `teleport`'s
arg 3 unchanged (nil → `teleport`'s default, false).

**Return:** none.

**Body:** calls the no-op assertion `assert_screen_is_faded_out`; when `coop_is_active()` is true, teleports
`"#PLAYER2#"` to arg 2; teleports `"#PLAYER1#"` to arg 1; when co-op is active, shows the "waiting for
player" dialog and polls `teleport_check_done("#PLAYER2#")` once per scheduler tick until true, then hides
the dialog; finally polls `teleport_check_done("#PLAYER1#")` once per tick until true. Each poll loop
yields before its first test.

**Side effects / subsystem:** whatever `teleport` does for each player (§4.3); blocks the calling script
thread until the local player's teleport request is no longer pending. Teleport / co-op glue.

**Single player:** with no session, reduces to one `teleport` of the local player and a poll that ends on
the first tick at which the kind-4 request for `"#PLAYER1#"` is done or absent (see `teleport_check_done`).

### `teleport_check_done` (0x00a61d90) — corrected body for §15.17

**Arguments:** 1 string — a character name, resolved through the player-first chain 0x00a281a0.

**Return:** 1 boolean.

**Body:** queries the scripted-request status function 0x006f6380 for (resolved object, kind 4).
0x006f6380 takes the object in ECX and the kind in EDI through the register-argument finder 0x006f6260,
which matches on both the object's 8-byte handle and the kind. Status 2 = no matching request; 1 = request
done (the record is released as a side effect); 0 = request pending. The boolean pushed is `status != 0`:
**true when the request is done or when there is no request at all; false only while a teleport request
for that character is pending.**

**Side effects / subsystem:** releases a done kind-4 record for that character. Scripted-request pool
(0x014f4be0/0x014f4be4, 200 records). **[CONFIRMED — disassembly.]**

### `teleport` (0x00a60e30) — completion behaviour, to add to §4.3

Allocates a kind-4 scripted request for the moved object (null callback) after both names resolve; does
nothing (and allocates nothing) if either fails. Player targets (class row +0xa bit 0x2, HIGH CONFIDENCE =
player class) go through 0x009e2220: with local authority the move is queued in a two-slot ring and game
mode 8 (asynchronous teleport) is pushed; mode 8 waits for any requested fade-out and for background
loading at the destination, moves the player, pops itself, and its exit handler completes the request.
Other targets go through 0x00996b50 and, with local authority, the request is completed before `teleport`
returns. Without local authority both paths send an opcode-0x41 record to the owner (inner type 0x17 for
players, 0x16 otherwise) and the request completes only when the owner's opcode-0x38 sub-type-0 reply
arrives. **[CONFIRMED — disassembly; player-class reading HIGH CONFIDENCE.]**

---

## 3. Direct answer

**(1) What `teleport_coop` does.** It is not a native. It is a `game_lib.lua` helper: teleport the remote
player (co-op only), teleport the local player, then block the script thread — polling once per tick — until
`teleport_check_done` says the remote player's (co-op only) and then the local player's teleport request is
no longer pending. No return value. The real engine work is `teleport` (0x00a60e30) and
`teleport_check_done` (0x00a61d90).

**(2) What makes it complete in single player.** Nothing co-op-related; **no co-op state needs to be
synthesised.** With no session, `coop_is_active()` is false, so the remote half and the "waiting for player"
dialog are skipped, and request ids get machine nibble 0 so completion is always local. The one wait left
is `teleport_check_done("#PLAYER1#")`, which is **true unless a kind-4 teleport request for the local player
is still pending**. In the real engine that request is genuinely asynchronous for a player (the call has a
synchronous half and an asynchronous half): `teleport` returns at once, and the request is marked done by
game mode 8's exit handler a few frames later, once any requested fade-out has finished and background
loading at the destination has gone idle. Within the same tick on which mode 8 exits, the done bit is set;
the script's next poll reads status 1 (and releases the record) and the loop ends. Nothing else has to be
noticed by the script — there is no callback name and no separate flag: the poll itself is the
notification.

**Why the three missions stall in Team B's host:** their `teleport_check_done` is an unmodelled stub that
returns nil, so `until teleport_check_done(LOCAL_PLAYER)` can never be satisfied (9,497 polls in the
drive). The fix belongs entirely in the host's model of `teleport` / `teleport_check_done`:

- **Minimum correct model.** `teleport_check_done(name)` returns true unless the host is tracking a pending
  teleport for that character. A host that does not model asynchronous teleports at all should therefore
  return **true** — that is the engine's own answer for "no request". `teleport_coop` then finishes after
  exactly one yield.
- **Faithful model.** Keep a per-(character, kind) request table. `teleport` allocates a pending kind-4
  record (replacing any earlier one for the same character) only when both names resolve. For a non-player
  target mark it done before returning. For a player target mark it done when the host's "async teleport"
  finishes — i.e. no earlier than the next pump tick, after any requested fade-out has completed and the
  host's streaming at the destination (if modelled) is idle. `teleport_check_done` returns false while
  pending; on a done read it returns true and deletes the record; with no record it returns true.
- The same rule ("true when done **or absent**") applies to every boolean `*_check_done` built on
  0x006f6380 with this wrapper shape — confirmed here for `turn_to_check_done` (0x00a61dd0, kind 0); see §4
  for the others.

**(3) Is it gated on the co-op/host check?** Not on the `0x0087ba20` `+0x5c`==`+0x58` host predicate. The
only co-op test on the path is the script's own `coop_is_active()`. The engine side branches on a different
**authority** gate (0x008addb0 → 0x00884760, the object's network-record owner byte) plus the global state
test 0x00bc5610, and on the session only to form the id's machine nibble. Down the no-session branch:
`coop_is_active` false → remote half skipped; machine nibble 0 → local completion; authority true for the
local player (and forced local whenever 0x00bc5610 is true) → move performed locally through mode 8 →
request marked done at mode 8 exit → next poll returns true.

---

## 4. Corrections and cross-impacts for the existing spec (flagged, not applied)

1. **§15.17 `teleport_check_done` is wrong on three counts** and should be replaced by §2's corrected entry:
   (a) the argument is used — 0x006f6380 passes the object and kind to 0x006f6260 in ECX/EDI, which the
   decompiler's zero-parameter signature hid; (b) the status codes are swapped: 2 = none, 0 = pending (the
   entry says 0 = none, 2 = pending); (c) consequently the Lua boolean is true for done-or-none and false
   only for pending — not "true whenever any teleport is tracked". The summary item 7 after §15 (line
   ~6979, "genuinely global, argument-ignoring query") repeats (a) and needs the same correction.
2. **§9.10 `vehicle_pathfind_check_done`** already reads 0x006f6380 correctly (0 pending, 1 done and
   consumed, 2 none) — it is the reference reading and contradicts §15.17.
3. **§22.15 `move_to_check_done`** labels status 1 as "pending → push false". The disassembly
   (0x00a547a1–0x00a549ce) shows status 1 goes to the clear-sub-record path and pushes **true**; status 0
   goes on to further checks. The label (and possibly the described result) needs re-reading against
   0x00a54620.
4. **§4.3 `teleport`** says the bit-set path is "a dedicated path for one vehicle sub-category"; §22.4
   calls 0x009e2220 "a fast per-type path". Both should read: the **player** path (HIGH CONFIDENCE), which
   queues an asynchronous teleport in game mode 8. §4.3 should also record the kind-4 request allocation,
   the early return without a request when either name fails, and that arg 3 is forwarded only on the
   player path.
5. **`spec-world-streaming.md` §11.3 item 2** describes 0x007a82c0 as "a camera-cut / view state machine"
   with "camera shot" records. It is the **per-frame handler of game mode 8, the asynchronous player
   teleport**; the two-slot ring at 0x02242068 holds pending teleports (target, destination, orientation,
   flags, request id), and its 0x0085bce0 call moves the streaming focus to the teleport destination. That
   also settles the item's own desk-review OPEN: the focus is written once per pending teleport, not every
   frame.
6. **§26.24** leaves modes 4/5 open; this pass adds **mode 8 = asynchronous teleport** (registered by
   0x007a8530) and **mode 0xa** as the mode pushed by the shutdown routine 0x00707810. Mode 5 is HIGH
   CONFIDENCE the gameplay mode (mode 8 abandons its move without it).

---

## 5. OPEN items

1. **Class row +0xa bit 0x2 = player class** is HIGH CONFIDENCE (three consistent uses, §1.5), not read
   from a descriptor row. Settling it needs the run-time table 0x02cc9900 or the row builder.
2. **0x00bc5610 / global 0x029443bc** (true for values 2 and 3; one writer 0x00bc4fd0 stores 2) — role not
   traced. It cannot push a local-player teleport onto the replicate branch, so it does not affect the
   answer.
3. **Mode 5's identity** (gameplay mode, HIGH CONFIDENCE) and the exact meaning of 0x00dafa90 ("background
   load outstanding", HIGH CONFIDENCE from its body: true off the main thread, else from the current job
   pointer 0x029cffd4).
4. **Superseded ring slot.** When a second player teleport arrives while the first slot is still busy,
   0x007a82c0 retires the older slot without completing its id. For the same character the older request
   was already dropped by 0x006f62b0, so nothing is left pending; for two different player objects the
   older request would stay pending until the reaper frees it. Not reachable in single player.
5. **0x009e69d0's use of arg 3 ("exit vehicles")** — passed in as one of its flag bytes, not traced inside.
6. **0x00bda550** (guarded side call on the player path) and **0x007b4bf0/0x007b4990** (UI object
   0x012fced8) — roles not traced; neither touches the request pool.

---

## 6. Cleanroom self-check

Pattern run over this file:
`\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4}|LAB_[0-9a-fA-F]+)\b`
— **0 matches** (`grep -c -E`, run 2026-10-02 over the final text, unfiltered; the pattern line above does
not match itself). No decompiler auto-names, no `FUN_` labels and no pseudocode are used; addresses only.
Script behaviour is paraphrased, not quoted; the only literal strings reproduced are identifiers and the
executable's own log/tag strings.

Dumps for this pass are in the session scratchpad under `tc/` (`func1`–`func11`, `range1`, `xref1`,
`misc/` = the extracted `misc.vpp_pc`). The job-private Ghidra copy `tools/gp_tc` and its helper
`tools/gp_tc_run.ps1` were deleted at the end of the pass.
