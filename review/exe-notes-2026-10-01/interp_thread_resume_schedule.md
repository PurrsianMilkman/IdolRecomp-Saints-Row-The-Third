# Script-thread per-frame resume scheduling: who calls the runner, how often, and why 39/49 missions never wake up

Team A, 2026-10-02. Investigative pass against the real executable (local Ghidra project, two
scratch copies of `tools/ghidra_projects` made to avoid lock contention — `tools/gp_threadsched`
and `tools/gp_threadsched2` — program `SaintsRowTheThird.exe`, headless `-readOnly -noanalysis`,
`ghidra/CrreishDump.java`). Dump output lives in this session's scratchpad (`ts_out/A`..`ts_out/H`)
and is not committed; everything below is described in prose. **Investigation only — no spec file
was edited for this note**, per the standing instruction that another agent is mid-edit on
`spec-lua-api-behaviour.md`.

**The question.** `spec-lua-api-behaviour.md`'s thread-table section fully documents the 256-slot
record table, the current-thread stack, the runner `0x00e0cba0`, and `thread_new`/`thread_yield`/
`thread_kill`/`thread_check_done`, but leaves OPEN who drives the runner on an ongoing basis. A
partner team's mission-drive trace found this is the live blocker for 39 of 49 missions: their
script threads `thread_yield()` inside a `while ... do thread_yield() end` poll (in `game_lib.lua`'s
`fade_out_block`/`fade_in_block`) and are never resumed again.

Labels follow the house style: **CONFIRMED** (read in this pass's own dumps, listing and/or
decompile), **HIGH CONFIDENCE** (follows from a dumped instruction/reference list but a body it
depends on was not itself dumped, or the link is strong but not literally read end-to-end),
**HYPOTHESIS** (plausible, not settled), **OPEN** (not settled, collected at the end).

---

## 0. Starting point (already established, not re-derived here)

- The 256-slot, 32-byte thread-record table at `0x02a42d10`, count at `0x02a44d58`; fields +0x00 id,
  +0x04 coroutine pointer, +0x08 "key", +0x0c not-yet-started byte, +0x10 function-name pointer,
  +0x14 inherited context, +0x18 flags (bit 0 started, bit 2 killed, bit 4 do-not-run), +0x1c arg
  count. The current-thread stack (depth `0x02a44d10`, array `0x02a44d14`, ≤16 deep). The runner
  `0x00e0cba0`'s resume/yield-check/release cycle. `thread_new` (`0x00e0f680`) allocates via
  `0x00e0ca80` → `0x00e0c720` and starts the thread synchronously to its first yield. All this is
  CONFIRMED in the existing spec text and re-used here, not re-derived.
- Three callers of the runner were already named: `0x00e0cd00` (a thin guard, called from dozens of
  per-subsystem holder sites), `0x006280d0` (a 12-way condition dispatcher, one case resumes), and
  `0x00e0cf50` (named but **not characterized** — the explicit lead for this pass).

---

## 1. Investigation trail

### 1.1 `0x00e0cf50` is the central per-frame scheduler: it walks the *entire* live table every call — CONFIRMED (disassembly + decompile, dump `ts_out/A/func_0x00e0cf50.txt`)

Full body, 0x5d bytes, no sub-calls except the two cited below. In its own words, the loop:

- starts a slot index at 0 and a record pointer at the table base `0x02a42d10`;
- for the record at the current slot: clears bit 0 of that record's `+0x18` flags word
  unconditionally; then asks `0x00e0d6e0` whether this record's `+0x08` owning-state is one of up
  to four exempt keys; if it is **not** exempt, calls the runner `0x00e0cba0` on it, and if the
  runner reports the record is no longer alive (released), steps the slot index and record pointer
  *back* by one before the unconditional step forward below — so the next loop pass re-examines the
  same slot index;
- steps the slot index and record pointer forward by one record;
- repeats while the slot index is below the live count, **re-reading the live count from
  `0x02a44d58` on every pass** (not cached at loop entry).

Two load-bearing facts:

1. **It walks every slot from index 0 to the current count, re-reading the count each iteration.**
   Because the release path (§1.5) swap-compacts the last live record into a freed slot and
   decrements the count, the back-step-then-forward-step compensation on a release means the
   *same index* is re-checked next loop step (the record that got moved into this slot by the
   swap). This is a correct "walk while allowing in-place removal" pattern, not a bug.
2. **It clears record `+0x18` bit 0 for every record it visits, before the exempt check and
   regardless of whether it ends up calling the runner.** This matters: the runner (`0x00e0cba0`,
   disassembly re-read this pass) does `*(ushort*)(record+0x18) |= 1` immediately before resuming
   the coroutine, and **nothing inside the runner itself ever clears that bit again.** The runner's
   own re-entrancy guard (`if ((flags & 3) != 0) return "alive, no resume"`) means that **once a
   record has been resumed at all, it is only resumable again because `0x00e0cf50` cleared bit 0
   on a later pass** — this is true even for records the scheduler itself chooses not to resume
   (the exempt ones, §1.7). So `0x00e0cf50` is not just "the resumer of ordinary threads"; it is
   the piece of housekeeping that the *entire* script-thread subsystem depends on to remain
   resumable at all, including threads some other subsystem drives directly through `0x00e0cd00`.

### 1.2 `0x00e0cf50`'s only two caller functions, exhaustively — CONFIRMED (raw byte scan, dump `ts_out/D`)

A raw CALL/JMP/pointer scan of the whole binary (not just the reference manager) finds **exactly
four call sites, in exactly two functions**:

- `0x00702070`, one call site (`0x007025b7`, a plain `CALL`).
- `0x00e0dfc0`, three call sites (`0x00e0e042`, `0x00e0e095`, `0x00e0e0a2`), each a tail-jump
  (`JMP`, not `CALL`) — three separate code paths inside `0x00e0dfc0` that each end by jumping into
  `0x00e0cf50`, not three sequential calls per invocation.

The same raw scan on the runner `0x00e0cba0` finds exactly four callers total, matching the spec's
existing list plus `thread_new`'s own first-run call: `0x006280d0`, `0x00e0cd00`, `0x00e0cf50`, and
one undefined-code site at `0x00e0f6fb` (inside `thread_new`'s own body, consistent with "the runner
starts the thread immediately").

### 1.3 `0x00702070` is a dedicated, always-running background-thread loop, and its steady state calls the scheduler every iteration — CONFIRMED (full disassembly + decompile, dump `ts_out/E/func_0x00702070.txt`)

`0x00702070`'s *only* reference anywhere in the binary is a bare data dword at `0x005d38cb`
(confirmed again by an independent raw scan, `ts_out/H/calls_0x00702070.txt`: 1 hit, a plain
`dword`, not a `CALL`/`JMP`). That address sits inside `0x005d3ac0` — the function the existing spec
already calls "the main loop" (caller of `0x005d14b0`, `0x005d1a30`, `0x00706be0`). The shape is
exactly what a `CreateThread`-style entry-point constant looks like, and `0x00702070`'s own body
confirms it: it is a bare `while(true)` loop with no return path except a global quit flag — it can
only be a thread's own proc, never an ordinary per-frame subroutine (calling it would never return).
**HIGH CONFIDENCE** that it is spawned once, as a dedicated OS thread, from the main loop's own
start-up (the exact `CreateThread`/equivalent instruction bytes around `0x005d38ca` were not
individually inspected this pass); **CONFIRMED** everything about the loop body itself.

The loop body, every iteration:

```
while (true) {
  ... poll time, early fade/loading-indicator calls (0x00db9970, 0x005d1880, ...) ...
  switch (state) {           // state 0..6, a ONE-TIME start-up sequence
    case 0: poll a device/handle "class" (0x00dafb60) until ready, else advance an index
    case 1..5: a graphics-device/loading-screen bring-up sequence (explicit "wait up to 1000"
               polls, 0x005d9d70 loading-screen renders, 0x00d9e1e0 completion checks) —
               advances state 0→1→2→3→4→5→6 over however many iterations the device needs
    case 6: FUN_00846ae0(); FUN_00e0cf50(); FUN_00848e40();     // <-- THE SCHEDULER
  }
  ... vsync/profiler bookkeeping ...
  elapsed = now() - iteration_start;
  if (quit_requested && state == 6) break;      // only exit path
  if (elapsed < 0x1e /* 30 (ms) */) FUN_00dc4440(0x1e - elapsed);   // sleep the remainder
}
```

Once `state` reaches 6 it never changes again (no write to the state variable inside `case 6`), so
**every iteration of this loop for the rest of the process's life executes `case 6`**, which calls
`0x00846ae0()`, then the scheduler `0x00e0cf50()`, then `0x00848e40()`. The loop is throttled to a
**minimum** of 30 ms per iteration (it sleeps the remainder if an iteration finished early) and has
**no maximum** — it runs as fast as `0x1e` minus whatever the iteration's own work cost, so in
practice at most ~33 Hz and potentially slower under load, but **never tied to the display's
render-frame boundary** the way `0x005d14b0` is. CONFIRMED for the loop shape, the state machine's
one-way settling into state 6, and the throttle; HIGH CONFIDENCE that this cadence (not the render
frame rate) is what every ordinary script thread actually resumes on.

### 1.4 `0x00e0dfc0` is NOT a per-frame driver — it is an on-demand "flush all script threads" utility used by a handful of subsystems — HIGH CONFIDENCE (dumps `ts_out/A` globals section, `ts_out/H/calls_0x00e0dfc0.txt`, and three independent prior-session decompile excerpts: `tools/te1_h.txt:897-934`, `tools/sl_batch6.txt:40-44`, `tools/hn1_dec15.txt:38-42`)

An exhaustive raw scan of `0x00e0dfc0`'s callers finds **exactly five**: `0x00702a50` (the
cutscene machine's per-frame driver, §1.4.1 below), `0x007ae420` (not investigated this pass),
one undefined-code site at `0x007b062a`, the one-time **installer** flow at `0x0088cefb`
(`spec-format-inventory.md`/HANDOFF's `installer-caller.json` region — a one-shot disc-to-HDD
install, not steady state), and the "debug bink" cutscene driver's tail at `0x00bdbc29`
(the spec's "second driver"). `0x00e0dfc0` itself reads the created-Lua-state bookkeeping
(`0x02a44f50`/`0x02a44f54`/`0x02a44f58`) and ends with three mutually-exclusive tail-jumps into the
scheduler — the shape of "before I reset/tear down, drain every pending script thread one more
time," called on-demand by a handful of subsystem-reset paths (cutscene start/stop, the installer,
a debug movie driver), not a steady per-frame pump.

**1.4.1 — a loose end knowingly left loose.** `0x00702a50`'s own only caller, exhaustively, is
`0x00703c00` (`ts_out/H/calls_0x00702a50.txt`, 1 hit) — this is the exact function the existing
spec's `0x0072d660`/cutscene passage flags OPEN ("does `0x00702a50` really run every frame, or only
at cutscene start"). This pass did not dump `0x00703c00`. It does not change the answer below:
whether or not `0x00702a50` is steady-state, it only reaches the scheduler *indirectly* through
`0x00e0dfc0`'s teardown path, which is a different thing from `0x00702070`'s direct, unconditional,
state-6 call. Settling `0x00703c00` would sharpen the cutscene spec's own OPEN item but would not
change this note's answer to "who is the per-frame scheduler."

### 1.5 Registration and removal happen in the exact array the scheduler walks — CONFIRMED (disassembly + decompile, dumps `ts_out/A/func_0x00e0c720.txt`, `ts_out/A/func_0x00e0c650.txt`, exhaustive whole-binary xref `ts_out/C`)

- **`0x00e0c720` (the allocator thread_new's chain bottoms out at) appends directly to the live
  table and bumps the count** — `(&DAT_02a42d14)[count*8] = coroutine; DAT_02a44d58 = count + 1;`
  then fills in the rest of the new record's fields (id, `+0x08` key, `+0x14` inherited context,
  `+0x18` flags := 0, `+0x0c` not-yet-started := 1, `+0x10` := a private heap copy of the function
  name string). **There is no separate "pending" list anywhere: the exact same contiguous array
  `0x00e0cf50` walks is the only place a new thread record ever lives**, and it is visible to the
  scheduler from the very next time `0x00e0cf50` runs (capacity check `0xff < count` → refuse,
  i.e. the hard cap is exactly 256 live records — CONFIRMED precisely, not just inferred).
- **Record `+0x08` is literally the owning Lua state pointer, not just an opaque "key".** The
  allocator's fifth argument (which becomes `+0x08`) is passed directly to `0x00dfe610`/`0x00dfe420`/
  `0x00dfe670` as a Lua state argument to push/`getglobal` the named function (pseudo-index
  `0xffffd8ee` = -10002 = `LUA_GLOBALSINDEX`, the same idiom already established elsewhere in this
  project for state-table lookups). This refines the existing spec's "+0x08: key inherited from the
  parent record; selects an error callback" — both are true; the key *is* the state, and the state
  is what both the error-callback dispatch and the global-function lookup use.
- **`0x00e0c650` (release) is CONFIRMED to be exactly the swap-compaction §1.1 inferred:** copy the
  current last live record (index `count-1`) over the released slot (unless it already *is* the
  last), zero out the vacated last slot's 8 dwords, decrement `0x02a44d58`, and null the caller's
  record pointer. Called from the runner (on finish/error/kill-noticed) and from `0x00e0cdb0`
  (a bulk "release every record whose `+0x08` equals this state" — used only at Lua-state teardown,
  §1.6). An exhaustive whole-binary xref of the table base `0x02a42d10` and the count `0x02a44d58`
  (dump `ts_out/C`, reference-manager *and* raw-pointer scan, not capped) finds **every direct
  reference to either address confined to ten functions, all inside the same `0x00e0c6xx`-`0x00e0cfxx`
  module**: the allocator pair (`0x00e0c720`, `0x00e0c8f0` — a second create-entry point, not
  examined), release (`0x00e0c650`), four finder/kill helpers (`0x00e0caa0` = `thread_check_done`'s
  finder, `0x00e0cb20`, `0x00e0cd20`, `0x00e0cdb0`, `0x00e0ce80` — the latter three not examined this
  pass), and the scheduler itself. **Nothing outside this module ever touches the table directly.**

### 1.6 The exempt list: who opts a thread out of the scheduler's automatic resume — CONFIRMED shape, HIGH CONFIDENCE role (dumps `ts_out/G`)

`FUN_00e0d6e0` (the exempt test `0x00e0cf50` calls) is a 4-entry linear search over
`0x02a44d78..0x02a44d84`. Its full lifecycle, now read:

- **At start-up it is empty.** `0x00e0d0b0` (the thread subsystem's one-time init, called once from
  `0x005d2400`) zeroes all four slots unconditionally, alongside zeroing the table count and
  current-thread-stack depth. **CONFIRMED — it does not seed any real key.**
- **The only remover is `0x00e0d5c0`**, whose *only* caller (`0x00a20340`, one site) is the Lua-state
  **destructor** (the function that also nulls the gameplay state global per `interp_yduu.md` §2):
  on tearing down a state it clears that state from the exempt slots, bulk-releases every thread
  record owned by it (`0x00e0cdb0`), and removes it from the per-created-state table. This is
  teardown-only, not a steady-state participant.
- **The only adder/refcounter is `0x00e0d710`** (a proper refcounted register/unregister: finds an
  existing slot for the key and increments, or claims a free slot and sets refcount 1), with
  **exactly four callers, all in the `0x00707xxx` UI/interface-subsystem module**
  (`0x00707540`, `0x00708a80`, `0x007074a0`, `0x00708330`) — the same module family that holds the
  dozens of `0x00e0cd00` per-subsystem holder sites the existing spec already describes, and the
  fade routine's own direct holder calls (§1.7). **HIGH CONFIDENCE** (the exact value the four UI
  callers pass was not individually traced) that this is the **interface/UI Lua state opting its own
  threads out** of the central scheduler, because the UI subsystem drives them itself through its
  own per-document holder sites. **Nothing adds the gameplay state to this list** in any dump this
  pass took — the mission/gameplay thread's `+0x08` key has no code path reaching `0x00e0d710` at
  all, so ordinary mission threads are never exempt and are exactly the threads `0x00e0cf50` walks
  and resumes.

### 1.7 A concrete, already-working example of the "per-subsystem holder" pattern: the fade routine — CONFIRMED (disassembly, dump `ts_out/B/func_0x005d14b0.txt`, following into `0x0059fe70`)

`0x005d14b0` (the main loop's per-frame **render-thread** system update, confirmed already in
`interp_nnlt.md` §4.1 to run once per rendered frame, called from `0x005d3ac0`) calls `0x0059fe70`
(the fade per-frame routine) every frame, unconditionally. Freshly disassembling `0x0059fe70` this
pass (it was previously read for its fade-state-machine behaviour only) shows it makes **two direct
calls to `0x00e0cd00`** (`0x0059ff12`, `0x005a0017`) — the exact "thin guard, resume via the runner
if the held record pointer is non-null and runnable" pattern the existing spec already names. This
is the UI/fade subsystem driving **its own** persistent script-thread record directly, every
rendered frame, independent of the central scheduler — a different thread from, and unrelated to, a
*mission's own* `thread_new`'d coroutine that happens to be waiting on the fade's C-side state. It is
cited here only as hard confirmation that the "per-subsystem holder calling `0x00e0cd00` every
frame" mechanism genuinely exists and runs, for at least one concrete subsystem (fades/UI) — it is
not what drives a mission thread.

### 1.8 No per-call resume budget was found — CONFIRMED (disassembly, §1.1)

`0x00e0cf50`'s loop condition compares the slot index against the live count at `0x02a44d58`,
re-read each iteration; there is no counter, no early-exit, and no "resume at most N this pass"
logic anywhere in its body. Every live,
non-exempt record is walked and offered a resume on every call. The only real "budget" in the whole
mechanism is (a) the hard 256-record table capacity, and (b) the **cadence** budget imposed by the
enclosing loop in `0x00702070` — a minimum 30 ms between calls, no fixed maximum.

---

## 2. Direct answers (drafted for transcription into `spec-lua-api-behaviour.md`'s thread-table section)

**1. Who calls the script-thread runner each frame, and for which threads, in what order?**
CONFIRMED. There is exactly one steady-state, unconditional caller: **`0x00e0cf50`**, a tight loop
with no sub-calls besides an exempt-test and the runner itself, that walks the live-thread table
**array-index order from 0 to the current count** (re-read every step, so a mid-walk release
re-examines the same index rather than skipping the record swapped into it) and, for **every** live
record whose `+0x08` owning-Lua-state is not one of up to 4 keys an "I'll drive my own threads"
subsystem has opted out (in practice: the UI/interface state only — §1.6), calls the runner on it.
`0x00e0cf50` itself is called from exactly one steady-state site: **the `case 6` branch of the
infinite loop in `0x00702070`**, a dedicated background thread spawned once at start-up from the
main loop (§1.3), reached every iteration once a one-time graphics/loading-screen bring-up sequence
settles. A second caller, `0x00e0dfc0`, is an on-demand "flush everything before I tear down" utility
used by cutscene start/stop, the installer, and a debug-movie driver — not a steady per-frame
mechanism (§1.4). **Separately and additionally**, specific subsystems (UI/fade, confirmed
concretely in §1.7; "dozens" of others per the existing spec's `0x00e0cd00` caller census) drive
their *own* held thread record directly, every render frame, via `0x005d14b0` → their own update →
`0x00e0cd00` → the runner — a parallel, independent path for threads whose owning state opted out of
`0x00e0cf50`'s walk.

**2. What exactly does a bare `thread_yield()` wait for: one frame, or a condition the engine checks?**
CONFIRMED/refined. `thread_yield()` itself still does nothing but discard its arguments and yield
with no values (existing spec, unchanged). What actually wakes the thread back up depends on which
of the two mechanisms above owns it: a UI-exempted thread wakes on its owning subsystem's next
render-frame call (so, in practice, once per rendered frame); an **ordinary (non-exempt) thread —
which includes every mission/gameplay-state thread, since nothing ever adds the gameplay state to
the exempt list — wakes on the next iteration of the separate ~33 Hz (minimum 30 ms, no fixed
maximum) background-thread loop in `0x00702070`, not on the next rendered frame.** A bare
`thread_yield()` in `game_lib.lua` therefore waits for "the next tick of the engine's independent
script-thread pump thread," which only coincides with a rendered frame by chance of timing, not by
design.

**3. How does `thread_new` register a thread so the per-frame pass picks it up, and when does a
finished thread get removed?**
CONFIRMED. `thread_new`'s allocator (`0x00e0ca80` → `0x00e0c720`) appends the new record directly
into the same contiguous, fixed-capacity (256) array `0x00e0cf50` walks and increments the shared
count in the same instruction sequence — there is no separate pending/active list, no promotion
step, and no delay: the record is visible to the scheduler's very next pass. Removal is swap-
compaction (`0x00e0c650`): the record is overwritten by whatever was the last live record, the
vacated last slot is zeroed, and the count is decremented; it runs from inside the runner itself
whenever a resume finishes without yielding, errors, or finds the kill bit set (so `thread_kill`'s
"removal happens at the next scheduled resume attempt," already in the spec, is unchanged — "next
scheduled resume attempt" now has a precise meaning: the next time `0x00e0cf50` visits that slot, or
the next time the thread's own dedicated holder calls `0x00e0cd00`, whichever owns it), or in bulk
via `0x00e0cdb0` when an entire Lua state is torn down.

**4. Is there a per-frame budget or cap on resumes?**
CONFIRMED, but not the kind of cap the question implies. There is no cap on *how many* records a
single scheduler pass resumes — every live, non-exempt record is offered a resume every time
`0x00e0cf50` runs, unconditionally (§1.8). The real limiter is **cadence**: `0x00e0cf50` only runs as
often as `0x00702070`'s background loop iterates, which is throttled to a *minimum* of 30 ms and has
no enforced maximum, so in steady state it runs at roughly, but not exactly, 33 Hz — a different,
and generally slower, cadence than the render frame rate `0x005d14b0`'s own per-frame subsystems run
at.

**Fix recommendation for a host implementation.** The missing piece is almost certainly architectural
rather than a logic bug in any one function: a host needs a loop that (a) runs continuously and
independently of whatever drives rendering, on a cadence of roughly 30 ms (or safely more often —
calling it on every rendered frame instead is a strict superset of the original behaviour and cannot
under-resume anything), and (b) on every tick, walks *every* currently-allocated thread record and
calls the equivalent of the runner on each one whose owning Lua state has not separately opted itself
out. A host that only ever resumes script threads from inside its per-rendered-frame update (mirroring
`0x005d14b0` alone) reproduces exactly the reported symptom: any thread not individually held and
re-driven by a specific subsystem's own update code — which, for an ordinary mission's `thread_new`'d
coroutine inside `game_lib.lua`, is *all of them*, since nothing in the real engine special-cases
mission threads for direct driving — yields once and is never resumed again, because nothing ever
calls the one piece of code (`0x00e0cf50`'s equivalent) responsible for both resuming it *and*
clearing the single re-entrancy flag that even a directly-driven thread depends on. Implementing this
one missing loop, with no other change, should be sufficient to unstick all 39 affected missions,
provided the host's own `thread_yield`/`thread_new`/record-table semantics already match the rest of
the spec (which they are reported to).

---

## 3. What remains open

- `0x00703c00`'s own body (the caller of the cutscene driver `0x00702a50`) — does not change this
  note's answer, but would settle a pre-existing OPEN item in `spec-lua-api-behaviour.md`'s cutscene
  passage ("does `0x00702a50` run every frame, or only at cutscene start").
- The exact value the four `0x00707xxx` UI callers pass into `0x00e0d710` (confirming it really is
  the interface Lua-state pointer specifically, not some narrower per-document key) — HIGH CONFIDENCE
  as written, not CONFIRMED down to the literal value.
- `0x00e0d0b0`'s install of `0x00754410` (the project's already-confirmed generic no-op stub, see
  `spec-format-inventory.md` §4a) as a default callback via `0x00dff400`/`0x00dff410` — not pursued;
  almost certainly a harmless default-hook placeholder, same pattern as every other sighting of that
  stub in this project, not re-verified here.
- `0x00e0c8f0`, `0x00e0cb20`, `0x00e0cd20`, `0x00e0ce80` (the thread-table module's other finder/
  allocator siblings, found only by the exhaustive xref in §1.5) — not individually read; nothing in
  this pass's evidence suggests any of them reaches outside the module or behaves unlike its already-
  characterized sibling.
- The exact `CreateThread`-style instruction sequence around `0x005d38ca` in `0x005d3ac0` (confirming
  the thread-spawn mechanically, not just by shape-of-the-callee inference).
- `0x007ae420` and the undefined-code site at `0x007b062a` (two of `0x00e0dfc0`'s five callers) —
  not investigated; given `0x00e0dfc0`'s established on-demand-flush role, unlikely to change the
  answer, but not verified.

---

## 4. Cleanroom self-check

Grep of this file's own prose against the decompiler-artifact pattern
`\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4}|LAB_[0-9a-fA-F]+)\b`:

**Result: 0 hits.** Every control-flow description in this note (the `0x00e0cf50` loop in §1.1, the
`0x00702070` loop skeleton in §1.3, the register/refcount logic in §1.6) is written in plain English
over named offsets and addresses, not transcribed decompiler pseudocode — no `iVar`/`uVar`/`local_`/
`param_`/`undefined*`/etc. token appears anywhere in the file.
