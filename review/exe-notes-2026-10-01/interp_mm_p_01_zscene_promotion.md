# zscene promotion for a cold `zscene_prep` (Team B blocker, mission `mm_p_01`, scene `'p_z01'`)

Team A, 2026-10-02. Investigative pass against the real executable (local Ghidra project copied to a
job-private `tools/gp_zsprom`, program `SaintsRowTheThird.exe`, headless `-readOnly -noanalysis`,
`ghidra/CrreishDump.java`). Dump output lives in this session's scratchpad (`zs/` sub-folders:
`func1`–`func15`, `calls1`–`calls3`, `xref1`–`xref3`, `range1`–`range2`, `ptrs1`) and is not
committed; everything below is described in prose.

**The question.** Team B's mission-drive trace of `mm_p_01` shows the script calling
`zscene_prep('p_z01', ...)` once and then polling `zscene_is_loaded('p_z01')`, which never turns
true. `spec-lua-api-behaviour.md` §26.25 says the pending entry is promoted to current by
0x00720410 (from 0x007258a0, cutscene states 0 and 2) and completed to load state 2 by 0x007285c0
(from 0x0072d660). What has to be true, frame by frame, for a cold prep to get from "pending" to
"loaded" — and is there a way it can sit forever?

**Inputs read before dumping anything.** `spec-lua-api-behaviour.md` §26.25 (globals table,
lifecycle steps 1–6, host summary, OPEN list) and §14.23 (`zscene_is_loaded` truth table);
`interp_nzxf.md`, `interp_mnao.md` §4, `interp_nnlt.md` §1–§2, `interp_thread_resume_schedule.md`
§1.3–§1.4.1 (the 0x00703c00 loose end), `interp_0153b530_initial_value.md`.

Labels follow the house style: **CONFIRMED — disassembly** = read in a listed instruction stream of
this pass's dumps; **HIGH CONFIDENCE** = follows from a dumped instruction or reference list but the
body it points at was not dumped; **HYPOTHESIS** = plausible, not settled; **OPEN** = not settled,
collected at the end.

---

## 0. Starting point (from prior passes, not re-derived here)

- `zscene_prep(name)` runs the gate 0x007232e0: refuses when `skip_all_cutscenes` (0x0153b556) is
  set or the entry is missing or not kind 1; returns 1 and does nothing when the entry is already
  current; otherwise calls the stub 0x0101b530, the teardown 0x00721c20(1, 0, 0), and writes the
  entry into the pending slot 0x0153b538 (plus the two per-object parameters 0x0153b568/0x0153b56c).
  Re-read this pass; unchanged.
- `zscene_is_loaded(name)` is true when `skip_all_cutscenes` is set, or when the name does not
  resolve to a kind-1 entry, or when the entry is the current entry (0x0153b530) **and** the load
  state 0x0153b51c is 2 (§14.23). So a stall means: the entry exists, is kind 1, and either never
  becomes current or never reaches load state 2. (A missing `p_z01` entry would make the poll return
  true at once, not stall.)
- The spec's open item: 0x007258a0 and 0x0072d660 were said to share "two drivers", 0x00702a50 and
  the bink state machine at 0x00bdbf30, and whether 0x00702a50 runs every frame depended on its
  undumped caller 0x00703c00.

---

## 1. Investigation trail

### 1.1 Who actually runs the two zscene steps each frame: the game-state table — CONFIRMED — disassembly

The executable keeps a table of up to 11 **game states** at 0x01503bc0, 0x24 bytes per state,
filled by the registrar 0x00706a40(stateId, descriptor, flagByte) after the descriptor is zeroed by
0x00706a10. Each descriptor holds eight callback slots (+0x00 … +0x1c), a byte flag at +0x20 and a
"registered" byte at +0x21. A stack of active state ids lives at 0x01503b50 with the top index at
0x012f4a80; 0x00706ab0 returns the id on top of the stack (0 when empty).

The per-frame dispatcher is 0x007068d0, reached only through 0x00706be0(mode). 0x00706be0 first
applies queued pushes and pops (calling slot +0x00 of a newly pushed state and slot +0x04 of a
popped one, slot +0x08 as a pop veto), then calls 0x007068d0 with the mode in EAX:

- **mode 0**: finds the highest stack index whose +0x20 flag is clear (with every state registered
  with flag 0, as states 5 and 7 are, that is simply the top of the stack); calls slot +0x14 of
  every state below that index; calls slots +0x0c and +0x14 of the states from that index to the
  top; then slots +0x18 and +0x10 from the top down to that index and +0x18 below it. So **slot
  +0x0c is "per-frame update while this state is the active (top) state"**.
- **mode 1**: calls slot +0x1c of **every** state on the stack, bottom to top.

The main frame function 0x005d14b0 calls 0x00706be0(0) every frame and then, if the stack is
non-empty, 0x005d4260, which calls 0x00706be0(1) (at 0x005d4332). (0x00707b50 is a second,
shutdown-style loop that calls both modes until the stack empties.) So within one frame, all
mode-0 slots run before all mode-1 slots.

Registrations that matter here:

| state | registered by | +0x00 | +0x04 | +0x0c | +0x14 | +0x1c |
|---|---|---|---|---|---|---|
| 5 | 0x007041b0 (called from start-up 0x005d25f0) | 0x00702f90 | 0x00703f50 | **0x00703c00** | — | **0x007030d0** |
| 7 | 0x00bdc120 (called from 0x005d1a30) | 0x00bdbb20 | 0x00bdbed0 | — | 0x00bdbbb0 | 0x00bdbf30 |

State 5 is gameplay (HIGH CONFIDENCE label: its enter routine 0x00702f90 clears some session
bytes and posts a screen-fade request through 0x0059f8c0; its +0x0c routine is the per-frame
gameplay update that, among other things, steps the script threads). State 7 is the "debug bink" movie state
whose state machine the spec already describes at 0x00bdbf30 (its enter routine pushes a pause
request, see §1.5).

**The two zscene steps sit on different slots of the gameplay state:**

- **0x0072d660 (cutscene stepper; completion; idle auto-select)** is called at 0x00702a7c, near the
  top of 0x00702a50, unconditionally. 0x00702a50's only caller is 0x00703f04 at the end of
  **0x00703c00 = gameplay slot +0x0c**, and every path through 0x00703c00 reaches that call (no
  early return). So 0x0072d660 runs **once per frame, in the mode-0 phase, whenever gameplay is
  the top game state**. Its other caller, 0x00bdbc54, is the tail of the bink state's slot +0x14
  (0x00bdbbb0), and is gated on 0x00720640, which is true only when the cutscene state 0x0153b520
  is ≥ 2 and the cutscene manager 0x0153b528 exists — so while a bink movie is on top, the stepper
  runs only for an in-progress cutscene, never in idle state 0.
- **0x007258a0 (promoter) is NOT inside 0x00702a50.** Its call at 0x00703121 sits in
  **0x007030d0 = gameplay slot +0x1c** (code Ghidra never made a function; the raw scan's only
  pointer to it is the descriptor store at 0x007041e0). 0x007030d0 returns immediately when the
  top game state is 7, 8 or 9; otherwise it runs a sequence of world updates, calls 0x007258a0, and
  near its end pumps the streaming system with 0x00daf9a0(1, 1). So 0x007258a0 runs **once per
  frame, in the mode-1 phase, whenever gameplay is anywhere on the stack and the top state is not
  7, 8 or 9**. Its second caller, 0x00bdc0ef in the bink state's +0x1c routine, is gated on the
  same 0x00720640 test (cutscene state ≥ 2 with a manager).

Consequences for the spec's open items:

- **RESOLVED: 0x00702a50 is a steady per-frame routine**, not a one-shot transition routine. The
  HYPOTHESIS in §26.25 (job `buqd`) is refuted: it is the gameplay state's frame tail, called every
  frame through 0x00703c00. The two `0x00dad740(1000000)` calls inside it are, by the way the same
  routine is used elsewhere (0x007315a0 stores its result as a timer start), timer stamps rather
  than blocking waits (HIGH CONFIDENCE; 0x00dad740 not dumped this pass).
- **Correction:** "0x007258a0 is called from the same two drivers as 0x0072d660 (0x00702a50 …
  0x00703121 …)" is wrong for the first driver. 0x0072d660 is driven by gameplay slot +0x0c
  (via 0x00703c00 → 0x00702a50); 0x007258a0 by gameplay slot +0x1c (0x007030d0). The ordering
  "stepper first, promoter second" still holds within a frame, but because mode 0 runs before mode
  1, not because the calls sit in one routine.
- Side finding for `interp_thread_resume_schedule.md` §1.4: 0x00702a50 calls 0x00e0dfc0 at
  0x00702aac on both branches of its 0x00707490 test, so that "flush script threads" routine is
  reached every gameplay frame, not only on demand. Not followed further here; flagged for that note.

### 1.2 The promoter's switch, read from its own tables — CONFIRMED — disassembly

The `ptrs` dump of 0x00725dbc (six targets) and 0x00725dd4 (the byte index, read as dwords) gives,
for cutscene states 0..0x10: state 0 → 0x00725911, 1 → nothing, **2 → 0x00725911**, 3 → nothing,
4 → 0x00725936 (preload mount), 5 → 0x00725a03, 6..14 → nothing, 15 → 0x00725aa7, 16 → 0x00725a82;
states above 0x10 return. 0x00725911 is the promotion case: call 0x00720320, and only if it returns
true, call 0x00720410. So "promotion only in cutscene states 0 and 2" is now read from the table
itself rather than from the decompiler's reading of it. Nothing else in 0x007258a0 touches the
pending slot; the raw caller scan of 0x00720410 has exactly one hit (0x0072591e).

### 1.3 The promotion precondition 0x00720320, re-read — CONFIRMED — disassembly (with one correction)

0x00720320 takes no arguments. With a current entry, it selects that entry's handle by the usual
rule (0x00dafbb0 "reference count" on +0xc, else +0x10, else the player's female byte +0xa41) and
classifies it with 0x00dafb60:

- class 1 → it sets 0x0153b541 to **1**, load state := 0, (pending := current when 0x0153b542 is
  set), current := 0, and returns true;
- any other class → returns the byte 0x0153b541.

With no current entry it returns true, setting 0x0153b541 := 1 if it was clear.

**Correction to `interp_mnao.md` §4.4 / `interp_nnlt.md` §1.5** ("0x0153b541 := 0x0153b542" /
"(0x0153b542 ≠ 0)"): the instruction pair is a compare of 0x0153b542 followed by a store of AL, and
AL still holds the low byte of the classifier result, which is 1 on this path. The compare only
feeds the later branch. So the byte is set to 1 unconditionally.

**0x0153b542 is permanently zero.** Its only writer is the teardown 0x00721c20 (stores its second
argument), and the raw scan of 0x00721c20 finds exactly seven call sites, every one passing 0 as
that argument: the prep gate 0x007232e0 (1,0,0), the completion 0x007285c0 (1,0,0), the cutscene
start 0x00725df0 twice (1,0,0), the table destructor 0x007231e0 (1,0,0), the cutscene-end helper
0x00722d60 (x,0,0), and the mission-cleanup routine 0x006d9470 (1,0,1 — the same routine that
clears the mission context 0x014c8460). So the "re-queue the current entry" branch never fires and
**nothing but another prep can overwrite a pending entry** (the pending slot's complete writer set,
CONFIRMED from the reference list: 0x007232e0 sets, 0x00720410 clears, 0x00720320's dead re-queue).

### 1.4 Promotion 0x00720410 and the handle start 0x00dafea0 — CONFIRMED — disassembly

Promotion does nothing unless the pending slot is non-null **and** 0x007316a0(1) is true —
0x007316a0 is true when no soundtrack stream is held in 0x0153b71c, or when that stream's status is
exactly 100 (then it clears 0x0153b71c). With both, it selects the pending entry's handle
(0x0071fee0: +0xc unless its reference count is ≤ 0 and +0x10's is positive or the player is
female), starts it with 0x00dafea0, starts the entry's +0xf4 soundtrack with 0x007315a0 (which
records it in 0x0153b71c with a timer only if the audio id is non-zero and not already running,
otherwise clears 0x0153b71c), copies the per-object parameters into the entry, makes it current,
clears pending and writes load state 1.

0x00dafea0 refuses (returns 0, no effect) when called from any thread other than the one recorded
in 0x01329ea4 (written only by the streaming set-up routines 0x00dafa60 and 0x00db0480), or when the
handle is null, or when the handle's flag word (+0x1e) has bit 0x40. Otherwise it increments the
handle's reference count (0x00db1940), resumes a suspended handle (bit 0x10 → sets bit 0x20 via
0x00db1760), takes a handle off the release list (bit 0x04 → 0x00daf040 clears it), and, if the
handle is neither resident (bit 0x01) nor already queued (bit 0x02), **links it into the priority
load queue at 0x029cffd4 and sets bit 0x02 synchronously** (0x00daf120).

The classifier 0x00dafb60, re-read: null → 1; bit 0x40 → 5; bit 0x10 → 2 if bit 0x20 else 4;
bit 0x04 → 0; bit 0x01 → 3; bit 0x02 → 2; none → 1. So immediately after a successful start the
handle is class 2 (queued) or 3 (already resident), never class 1. That matters because 0x00720320
(class 1 → drop the current entry) runs on the promoter's next call; a start that silently did
nothing would leave the handle at class 1 and the freshly promoted entry would be dropped the
following frame (§2, gap 4).

### 1.5 Completion 0x007285c0 and the stepper's own gate — CONFIRMED — disassembly

0x0072d660 begins with a gate: if 0x00707490() — "the pause-request counter 0x01503eb0 is
positive" (incremented by 0x007074a0, which the bink state's enter routine calls; decremented by
0x00707540; HIGH CONFIDENCE label "outstanding pause requests") — **and** 0x00721a70() — true for
cutscene state ≤ 0xc unless certain manager bits are set, so always true in idle state 0 with no
manager — then only the playing states 10/11 get special handling and every other state returns
at once. Otherwise it switches on the cutscene state; the jump table at 0x0072defc sends **state 0
to a tail-jump to 0x007285c0** and **state 2 to a call of 0x007285c0 followed by the cutscene start
0x00725df0 once the load state is 2**. No other case calls 0x007285c0 (raw scan: two hits).

0x007285c0 (re-read): with no current entry it releases a finished soundtrack stream (status 0x66
or 5 s old). Then on the load state: **1** → classify the current entry's selected handle: class 3
together with 0x00731630(1) (no stream, or stream status 0x66, or the stream timer ≥ 5000) → load
state := 2 and 0x0153b534 := the service-9 object's slot +0x5c; class 5 → teardown
0x00721c20(1,0,0); any other class → wait. **2** → nothing. **0** → the idle driver 0x00728440.

So for a zscene the completion needs only: cutscene state 0 or 2, no outstanding pause request,
gameplay on top of the game-state stack, and the scene's request-group handle reporting class 3
(resident flag set, and none of the release/suspend/failure bits). The soundtrack can delay it by
at most 5 s.

**0x0153b543 is not involved.** The "preload packfiles mounted" byte is read by the promoter's
state-4 case (skip the mount if already done), by the stepper's state-4 case (required only for a
kind-2 story cutscene) and by the playback start; neither the zscene promotion path (0x00720320 →
0x00720410) nor the completion path (0x007285c0) reads it. A pure `zscene_prep` never needs the
preload packfiles. CONFIRMED from the listings of 0x007258a0, 0x00720320, 0x00720410, 0x007285c0.

### 1.6 The idle auto-select 0x00728440 and its race with a script prep — CONFIRMED — disassembly

0x00728440 is run by the completion step whenever the load state is 0. If 0x0153b541 is set **and**
the mission-active predicate 0x006cecb0 is false (mission context 0x014c8460 null, or mission
phase 0x014c7a14 = 8; §21.27) **and** 0x00831910 is false (0x02300884 zero and bit 0 of 0x022ccc7e
clear; unidentified), it scans the world-object manager's scene-object list (0x03171a64: count
+0x2f4, index words +0x2ec, object pointers +0x58) for objects whose +0x84 is 1 and whose entry
(through +0x7c) is kind 1 (0x00737500), picks the one nearest the reference position — **with no
distance limit**: the initial best distance is the float maximum unless a current entry's own
object resolves — and calls 0x00737780 on it, which runs the prep gate 0x007232e0(that entry,
object +0x8, object +0xc). (When a current entry exists but its owner object, looked up by its
+0x18/+0x1c parameters through 0x005980f0, no longer resolves, it also clears the current pointer;
a Lua-prepped entry has zero parameters, but this path only runs at load state 0.) Otherwise it
calls 0x00720320.

Because the prep gate does not check the pending slot, **an auto-select overwrites whatever is
pending**. And because the stepper (mode 0) runs before the promoter (mode 1) in every frame
(§1.1), a script prep that lands while (a) no mission is active, (b) 0x0153b541 is set — which it is
whenever there is no current entry, since every call of 0x00720320 with no current sets it — and
(c) at least one scene-bearing object passes the filter, is replaced by the nearest object's entry
before the promoter ever sees it. That entry is then promoted and loaded; the script's entry is
lost. During an active mission the predicate blocks the auto-select entirely, so a mission script's
prep is not exposed to this.

### 1.7 The warm case: what the teardown leaves behind — CONFIRMED — disassembly (one step HIGH CONFIDENCE)

When a prep finds a current entry whose selected handle is live (reference count > 0), the teardown
releases the scene's secondary object 0x0153b534 and lightset, releases the handle through
0x00720240 → 0x00dafad0, releases the soundtrack (first argument 1 → 0x007317a0(1)), sets
0x0153b541 := 0, 0x0153b542 := 0 and load state := 0, and **leaves the current pointer in place**.
0x00dafad0 decrements the reference count; when it reaches zero it unqueues the handle if it was
still queued and calls 0x00daf010, which **sets bit 0x04 and links the handle onto the release list
0x029cffd0**. Bit 0x04 is tested before bit 0x01 in the classifier, so the released handle is
**class 0**, not class 1. (Exception: reference count exactly 1 with bit 0x08 set and bit 0x4000
clear goes to 0x00db1fa0 instead, without decrementing — not read this pass.)

With 0x0153b541 = 0 and a class-0 current entry, 0x00720320 returns false, so the new pending entry
waits. The release list is drained by 0x00daf090, called first thing in the streaming pump
0x00daf9a0 — which 0x007030d0 calls every gameplay frame with (1, 1) — and by the synchronous
loader 0x00daff10. Each drained handle has bit 0x04 cleared and is handed to 0x00db3890 (HIGH
CONFIDENCE: the unload, which clears the resident bit; not dumped). Once unloaded the old handle
classifies 1, the next 0x00720320 call drops the old current entry (current := 0, 0x0153b541 := 1)
and returns true, and the promoter promotes the pending entry. So a warm prep is delayed by roughly
one or two frames, not indefinitely — **provided** the old handle's reference count actually reached
zero (§2, gap 5).

### 1.8 The failure path — CONFIRMED — disassembly for the code; the outcome depends on one undumped routine

If the scene's handle reports class 5 (bit 0x40), completion calls the teardown (1,0,0). With the
handle still referenced, the teardown takes its live path (release, 0x0153b541 := 0, load state 0,
current pointer left). From then on: 0x00720320 sees a current entry whose handle classifies 5 for
as long as bit 0x40 stays set, and returns 0x0153b541 = 0; a later prep of a *different* scene runs
the teardown again, which now finds a reference count of 0 and a class other than 1 and does
nothing; a later prep of the *same* scene hits "already current" and does nothing. Whether bit 0x40
is ever cleared depends on the unload routine 0x00db3890 (not dumped). If it is not, one failed
load wedges the whole zscene subsystem until the mission cleanup 0x006d9470 calls the teardown with
its third argument 1 (which also only acts on a class-1 handle) or the table is destroyed. OPEN.

---

## 2. Direct answer

### 2.1 What promotes a cold-prepped scene to loaded

A **cold** prep here means: no current entry (0x0153b530 = 0), no soundtrack stream held
(0x0153b71c = 0), cutscene state 0, `skip_all_cutscenes` clear, a mission active, gameplay (game
state 5) on top of the game-state stack. Then, CONFIRMED — disassembly at every step:

1. **Script:** `zscene_prep('p_z01')` → gate 0x007232e0: entry kind 1, not current → stub, teardown
   (no current: does nothing) → **pending := p_z01**.
2. **Next mode-0 phase** (gameplay slot +0x0c → 0x00703c00 → 0x00702a50 → 0x0072d660, state 0 →
   0x007285c0): no current, load state 0 → idle driver 0x00728440 → mission active → 0x00720320 (no
   current: true, no effect). Pending untouched.
3. **Next mode-1 phase** (gameplay slot +0x1c → 0x007030d0 → 0x007258a0, state 0): 0x00720320 true
   (no current) → **0x00720410 promotes**: soundtrack check passes (no stream), handle started
   (queued, class 2), soundtrack started if the entry has one, **current := p_z01, pending := 0,
   load state := 1**. Same frame as step 2 or the next, depending on where in the frame the script
   ran.
4. **Following frames:** 0x007285c0 (mode 0) waits while the handle is class 2; the streaming pump
   (0x00daf9a0, called at the end of 0x007030d0 every gameplay frame) loads the request group; when
   the handle reports resident (class 3) and the soundtrack has ended or 5 s have passed, **load
   state := 2**. Meanwhile 0x007258a0 keeps calling 0x00720320, which now returns 0x0153b541 = 1,
   so 0x00720410 runs and returns at once (nothing pending) — harmless.
5. `zscene_is_loaded('p_z01')` → entry is current and load state is 2 → **true**.

So, in the engine, a cold prep is promoted on the first promoter call after it and completes as soon
as its resources are resident. There is no missing transition in the state machine for the cold
case: the machine starts in exactly the state (cutscene 0, load state 0) where both steps run.

### 2.2 Where it can genuinely stall (not "the dump shows no loop")

1. **Cutscene state not 0 or 2.** Both promotion (0x007258a0's table) and completion (0x0072d660's
   table) run only in cutscene states 0 and 2. While a story cutscene runs (states 1, 3–0x13), a
   pending or loading zscene is frozen; if the cutscene machine itself is wedged in another state,
   so is the zscene. Not a gap in the zscene machine, but a hard dependency a host must model.
2. **Gameplay not driving.** The completion only runs while gameplay is the top game state and no
   pause request is outstanding (0x01503eb0 = 0); the promotion only runs while the top state is not
   7, 8 or 9. During a bink movie both run only for a cutscene in state ≥ 2, never for a bare
   zscene. A host that drives the zscene steps only from a "cutscene active" context (state ≥ 2 with
   a manager — exactly the gate the bink driver applies) **will never promote a bare
   `zscene_prep`**, because a bare prep keeps the cutscene state at 0. The spec's current wording
   ("called from the same two drivers as 0x0072d660") makes this easy to get wrong. **This is the
   most likely cause of the `mm_p_01` symptom in a host**, and it is a spec-wording problem, not an
   engine gap.
3. **The resource never reaches "resident".** Load state 2 requires class 3 on the scene's request
   group handle (+0xc, or +0x10 for the female variant) — i.e. the streaming system must actually
   load every file the `<name>.cte_xtbl` parse put in that group (§26.25 entry `+0xc`). A host whose
   streaming stub never marks the group resident leaves the scene at load state 1 forever; there is
   no timeout on this wait (the 120 s stamp is taken by the cutscene machine's loader 0x00722f10,
   which a bare zscene prep never reaches; 0x007285c0 has no timer of its own beyond the 5 s
   soundtrack allowance). The engine itself has no fallback here either: a handle that never resolves to 3
   or 5 waits indefinitely. CONFIRMED.
4. **A start that does nothing.** If 0x00dafea0 refuses (wrong thread, null handle, bit 0x40) the
   handle stays class 1 and on the very next promoter call 0x00720320 drops the just-promoted entry
   (current := 0, load state := 0). Nothing re-queues it (0x0153b542 is always 0), so **the scene is
   silently forgotten and `zscene_is_loaded` stays false for good** unless the script preps again.
   In the engine the promoter runs on the frame thread that owns the streamer, so this needs a null
   or failed handle. CONFIRMED for the mechanism.
5. **Warm prep with an extra reference on the previous scene.** If the previously current scene's
   handle is still referenced by something else (reference count stays > 0 after the teardown's
   release), it stays class 3 or 2, 0x0153b541 stays 0, and the new pending entry is never promoted.
   Also the reference-count-1/bit-0x08 special case in 0x00dafad0 (0x00db1fa0) may leave the handle
   resident. HYPOTHESIS that either arises in practice; the mechanism is CONFIRMED.
6. **A failed load wedges the subsystem** (§1.8) — CONFIRMED for the code path, OPEN on whether the
   failure bit is ever cleared.
7. **Auto-select race outside a mission** (§1.6): a prep made while no mission is active is
   overwritten by the nearest scene-bearing world object's entry. For `mm_p_01` this matters only
   if the host evaluates the mission-active predicate as false at prep time (mission context not yet
   set, or phase 8). CONFIRMED for the mechanism.
8. **Same scene already current but torn down.** If `p_z01` is current with load state 0 (its
   handle released by some earlier teardown), a new prep of it returns 1 and changes nothing; once
   the released handle unloads, 0x00720320 drops it and nothing re-queues it. Not a cold-prep case.
   CONFIRMED for the mechanism.

For Team B: items 2 and 3 are the ones to check first in their host; items 4 and 7 are the next most
likely. None of them is an engine bug for a cold prep.

---

## 3. Proposed spec text (for later transcription into §26.25)

**Globals table, row 0x0153b541 / 0x0153b542** — append: "0x00720320 sets 0x0153b541 to 1 on its
class-1 reset (not to the value of 0x0153b542). 0x0153b542 is written only by the teardown from its
second argument, and all seven callers of the teardown pass 0, so it is always 0 and the re-queue
branch never fires. CONFIRMED — disassembly."

**Lifecycle step 3, replace the driver sentence** ("called from the same two drivers as 0x0072d660
(0x00702a50 … 0x00703121 and 0x00bdbc54 … 0x00bdc0ef …)") with: "The engine keeps a table of game
states (0x01503bc0, 0x24 bytes each, registered by 0x00706a40) whose callbacks the frame function
0x005d14b0 dispatches in two phases each frame: phase 1 (0x00706be0(0)) runs slot +0x0c of the top
state; phase 2 (0x00706be0(1), from 0x005d4260) runs slot +0x1c of every state on the stack. For
gameplay (state 5, registered by 0x007041b0) slot +0x0c is 0x00703c00, which ends by calling
0x00702a50, which calls the cutscene stepper 0x0072d660 (completion); slot +0x1c is 0x007030d0,
which calls the promoter 0x007258a0 unless the top state is 7, 8 or 9. So every gameplay frame
runs completion first and promotion second, independently of any cutscene. The bink movie state
(7, registered by 0x00bdc120) calls the same two routines only when the cutscene state is ≥ 2 with
a manager (0x00720640). 0x00702a50 is a steady per-frame routine. CONFIRMED — disassembly."

**Lifecycle step 3, after the 0x00720320 rule** — add: "The promoter's switch tables at
0x00725dbc/0x00725dd4 send states 0 and 2 to the promotion case (read directly). The stepper
0x0072d660 skips every state except 10/11 while the pause-request counter 0x01503eb0 is positive.
The idle auto-select 0x00728440 runs only when no mission is active (0x006cecb0) and 0x00831910 is
false, has no distance limit, and overwrites the pending slot through the prep gate; because it
runs before the promoter in the same frame, a script prep made outside a mission is replaced by the
nearest scene-bearing object's entry. CONFIRMED — disassembly."

**Lifecycle step 3, completion** — add: "Load state 2 requires the request-group handle to be
resident (class 3); there is no timeout. A freshly started handle is queued synchronously (class
2). A handle released by the teardown is moved to the release list (class 0) and only classifies 1
after the streaming pump has unloaded it, so a prep that replaces a loaded scene is promoted one or
two frames later. If the start is refused the handle stays class 1 and the next 0x00720320 call
drops the promoted entry without re-queueing it. 0x0153b543 plays no part in zscene loading."

**Host summary, replace the second bullet** with: "A host runs, every gameplay frame and regardless
of the cutscene machine (but only in cutscene states 0 and 2): first completion (load state 1 →
2 when the scene's resources are resident and the soundtrack has ended or 5 s have passed; at load
state 0 the idle auto-select, suppressed during missions), then promotion (pending → current, load
state 1, when the 0x00720320 rule allows). Driving these only while a cutscene is active never
loads a bare `zscene_prep`."

**Review status** — move "0x00702a50 as a scene-transition routine" from HYPOTHESIS to refuted,
"0x00703c00's own body" from OPEN to resolved, "the identity of the cutscene machine's two per-frame
drivers" from HIGH CONFIDENCE to CONFIRMED (with the correction above).

---

## 4. OPEN — what to dump next

1. `func 0x00db3890` — the release-list unload: does it clear bit 0x01 and bit 0x40? Settles §1.7's
   "one or two frames" and §1.8's wedge.
2. `func 0x00db1fa0` — the reference-count-1 / bit-0x08 release path in 0x00dafad0 (gap 5).
3. `func 0x007317a0` — whether releasing the soundtrack clears 0x0153b71c; if not, 0x007316a0's
   "status exactly 100" requirement could hold a warm promotion back.
4. The registrations of game states 8 and 9 (callers of 0x00706a40: 0x005d3ac0, 0x00707840,
   0x007a8530, 0x007ad410, 0x007af0f0, 0x007b06f0, 0x007e1d10, 0x0088cf70) — which overlays stop
   the promoter.
5. 0x00831910's globals 0x02300884 / 0x022ccc7e — the second auto-select blocker.
6. Where Lua mission threads resume relative to the two frame phases (0x00703c00's own loop calls
   the script-thread routines 0x00e0cfb0 / 0x00e0ce60 / 0x00e0caa0) — affects only whether a
   prep is promoted in the same frame or the next.

---

## 5. Self-check

Cleanroom pattern (decompiler auto-names: `iVar`/`uVar`/…/`param_`/`local_`/`LAB_`/`in_XXX` etc.)
run over this file with the Grep tool on 2026-10-02: **0 matches**. Addresses are given as
`0xXXXXXXXX` only; no pseudocode.
