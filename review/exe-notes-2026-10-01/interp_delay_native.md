# Script "delay": what it is, what clock it measures, and whether that clock can stop

Team A, 2026-10-02. Investigative pass against the real executable. Two private scratch copies of
`tools/ghidra_projects` were used (`tools/gp_delay`, `tools/gp_delay2`, deleted afterwards), with
program `SaintsRowTheThird.exe`, headless `-readOnly -noanalysis` and `ghidra/CrreishDump.java`.
Packfile *directories* (headers and name tables only) were also read with small scripts. Dump output
lives in this session's scratchpad (`delay/` sub-folders) and is not committed; everything below is
described in prose. **Investigation only — no spec file was edited for this note.**

**The question.** After the resume-scheduler fix, one mission's script thread enters a timed wait
(a "delay") and never comes back. Three questions: what that delay is; what clock it measures; and
whether anything can stop that clock while scripts keep running.

Labels follow the house style: **CONFIRMED** means read in this pass's own dumps. **HIGH
CONFIDENCE** means it follows from a dumped instruction or reference list, but a body it depends on
was not dumped. **HYPOTHESIS** means plausible, not settled. **OPEN** means not settled; those items
are collected in §3.

## 0. Starting point

- `spec-lua-api-behaviour.md` thread-table section and `interp_thread_resume_schedule.md`: ordinary
  gameplay-state threads are resumed by `0x00e0cf50`, called from `case 6` of the dedicated
  background loop `0x00702070` (minimum 30 ms per iteration, no maximum).
- `spec-lua-api-behaviour.md` system-library roster: `get_frame_time` (`0x00e0f400`) returns the
  single-precision float at `0x0132a0b0` (file value 1/30). Its **writer is OPEN** there and in
  `interp_lgdz.md` item 3.

## 1. Investigation trail

### 1.1 There is no native "delay" — CONFIRMED (string census + raw yield-caller scan)

- **String census.** Every NUL-terminated identifier-shaped string in the executable containing
  `delay`, `sleep`, `wait`, `timer`, `elapsed`, `game_time`, `get_time` or `pause` was listed
  (scratchpad `delay/strs.py`, about 120 hits). The only bare `"delay"` (0x011156a4) is referenced
  by two `.rdata`/`.data` table cells (0x01138a74, 0x012e1320), not by any registrar. The
  timed-sounding Lua-registered names are all unrelated domain functions (`hud_timer_*`,
  `trigger_set_delay_between_activations`, `notoriety_spawn_group_set_delay`, `ai_set_action_delay`,
  `cellphone_clear_between_call_delay`, `player_override_*_sprint_delay`, ...). `get_game_time`
  (registered from the gameplay registrar 0x00a20840 at 0x00a2247e, native 0x00a4ae30) returns a
  **calendar table** — `seconds`, `minutes`, `hours`, a day field, `month`, `year` — read from the
  in-world clock through 0x006ff1f0; it is the time-of-day, not an elapsed-time counter.
  `vint_debug_sleep` is a UI-state debug name (registrar 0x00e1dfb0), not gameplay.
- **Only two functions in the binary can suspend a Lua thread.** The VM's yield primitive is
  0x00dffb20 (it raises "attempt to yield across metamethod/C-call boundary" when the C-call depth
  guard trips, otherwise marks the state suspended and returns -1). A raw CALL/JMP/dword scan finds
  **exactly two callers**: `thread_yield` (0x00e0f0d0, which yields with zero results) and
  0x00fcca20, the stock `coroutine.yield`, registered through the `{name, fn}` table at 0x012934e8
  (`create`, `resume`, `running`, `status`, `wrap`, `yield`) used by 0x00fccb70 — the standard
  base-library opener, which is why the system-library creator's opener "returns 2" (base table plus
  `coroutine`). **No gameplay native yields on a timer**: any script "delay" is necessarily a Lua
  function that loops around `thread_yield()` (or `coroutine.yield`) and decides for itself when
  enough time has passed.

### 1.2 Where the Lua-side "delay" lives, and why its text is not read here — CONFIRMED (location), not read (body)

Directory-only scan of every `.vpp_pc` in the install (header + entry table + name table; no
payload decompressed; scratchpad `delay/dirs.py`): `game_lib.lua` exists **once**, as entry of
`misc.vpp_pc` (uncompressed 125,147 bytes, stored 24,964 bytes). `misc.vpp_pc` has container flags
0x4801 — per-entry compressed, not a shared stream — i.e. it is exactly the "mode-a" container
shape whose decompression is a **parked** item. It was not opened. `game_lib.lua` occurs in no
uncompressed or shared-stream container (`interface.vpp_pc`, which earlier passes extracted, holds
`system_lib.lua` but not `game_lib.lua`), and no extracted copy exists anywhere under
`D:\Project Crreish`. So the exact Lua text of the script-side delay is **not read in this note**;
the native side below is what that function can possibly be built on, and the two candidate time
sources are fully characterised.

The only time-shaped natives a Lua delay loop can read are: `get_frame_time` (system library,
0x00e0f400, returns the float at 0x0132a0b0) and the gameplay calendar `get_game_time` (§1.1).
`get_frame_time` and `thread_yield` are referenced by exactly one data cell each, inside the
system-library registrar 0x00e0f900 (0x00e0f99c and 0x00e0fa99), so both are present in every
created state including the gameplay state.

### 1.3 Who writes the frame-time global 0x0132a0b0 — CONFIRMED (disassembly, dumps `delay/x1`, `delay/f1`)

Whole-binary xref of 0x0132a0b0 (43 uses, 30 functions): every use is a read except **two
writers**, 0x00db97e0 and 0x00db9970. This closes the OPEN item in the system-library roster.

**0x00db97e0 — timer defaults.** Stores, in one straight run: 0x0132a0b0 and 0x0132a0ac := 1/30
(0x3d088889); 0x0132a0b8 := 30.0; 0x0132a0bc := 1.0; 0x0132a0c0 := 1/120 (0x3c088889);
0x0132a0c4 := 0.25; and zeroes the counters 0x029f4da4, 0x029f4da8, 0x029f4db0, 0x029f4db8,
0x029f4dbc, 0x029f4dc0 and the 16-slot float ring at 0x029f4d60. Called once, lazily, from
0x00db9970's first run (latch byte 0x029f4dc4), and from 0x00db9960 (which also sets that latch),
whose one caller is 0x005d1a30.

**0x00db9970(quantize, quantum, fixedRate, externalMs) — the per-tick timer update.** In order:

1. Bumps a call counter (0x029f4dac); applies the defaults once (above).
2. A whole-body gate on the dword 0x029f4dc8: when non-zero, the function returns without touching
   anything. **That dword has no writer anywhere** (its one reference is this read), so the gate is
   always open in the shipped build.
3. Takes "now" in microseconds from 0x00dad740(1,000,000) (or, when `externalMs` is non-zero, uses
   `externalMs × 1000` added to the previous stamp), and subtracts the previous stamp kept at
   0x029f4dbc. A negative difference (counter wrap) is replaced by the previous delta.
4. **0x0132a0b0 := that difference in seconds** (or `1 / fixedRate` when `fixedRate > 0`). The same
   value is copied to 0x0132a0ac.
5. **Lower bound with a real sleep.** If the delta is below the minimum at 0x0132a0c0 (1/120 s by
   default), it sleeps the shortfall in whole milliseconds through 0x00dc4440 (only when the
   shortfall is 1..499 ms), re-reads the clock and recomputes 0x0132a0b0; on a second shortfall it
   stops trying and clamps only the 0x0132a0ac copy to the minimum. So 0x0132a0b0 is the measured
   wall-clock gap, normally at least 1/120 s.
6. **Upper bound on the copy only.** If the delta exceeds the maximum at 0x0132a0c4 (0.25 s by
   default), 0x0132a0ac := 0.25. **0x0132a0b0 is not capped.**
7. Optional quantisation (`quantize == 1`): busy-waits on the microsecond clock until the delta is a
   whole multiple of `quantum`, and adds the waited time to both globals.
8. Stores the new stamp; pushes the clamped delta into the 16-slot ring and derives a frames-per-
   second estimate into 0x0132a0b8; saves the unscaled clamped delta into 0x0132a0b4; then
   **0x0132a0ac := clamped delta ÷ 0x0132a0bc** (a time-scale divisor, default 1.0) and passes it
   (in ms) to 0x00d9dff0.
9. **Two millisecond accumulators.** If the counter 0x029f4dcc is **zero**, adds the rounded scaled
   delta (0x0132a0ac in ms, rounding helper 0x00dad900) to **0x029f4db0**; unconditionally adds the
   rounded raw delta (0x0132a0b0 in ms) to **0x029f4db4**. Finally broadcasts 0x0132a0ac into the
   four floats at 0x029f4dd0.

So the engine keeps two clocks side by side: a **pausable, scaled "game" millisecond counter**
(0x029f4db0, stops while 0x029f4dcc > 0, scaled by 0x0132a0bc) and an **unpausable, unscaled
"real" millisecond counter** (0x029f4db4). The frame-time global that `get_frame_time` returns is
neither gated nor scaled: it is the raw wall-clock gap since the previous call of 0x00db9970.

### 1.4 Who calls the timer update, and therefore what "previous call" means — CONFIRMED (raw scan, dump `delay/c1`)

Raw CALL/JMP/dword scan of 0x00db9970: three sites — 0x005d1548 inside 0x005d14b0 (the render
thread's per-frame system update), 0x007021a6 inside 0x00702070 (the background script-pump
thread), and one undefined-code site at 0x007c8962. Arguments and gating at each site: §1.6.

### 1.5 The game-pause push/pop also freezes the gameplay Lua state's threads — CONFIRMED (dumps `delay/x4`, `delay/c4`, `delay/f3`)

The pausable-clock counter 0x029f4dcc has exactly one incrementer (0x00db9940) and one decrementer
(0x00db9950). Raw scans: 0x00db9940 is called only from 0x007074a0 and 0x00708a80; 0x00db9950 only
from 0x00707540 and 0x00708330. **These are the same four functions that
`interp_thread_resume_schedule.md` §1.6 found as the only callers of the scheduler-exempt
register/unregister 0x00e0d710** — and reading them shows what they pass to it:

- **0x007074a0(reason, flag) — pause push.** Under the lock pair 0x00d9f620/0x00d9f630. When the
  pause depth 0x01503eb0 is **not yet positive** (first push): 0x00d9e0f0, 0x0087cce0, the
  pausable-clock increment 0x00db9940, (unless `reason` is 2) 0x0057ba60 — skipped for reason 0
  when 0x007bdd10 says so — then 0x00d48db0, **0x00e0d710(the dword at 0x026e7e6c, 1)** and
  0x007f1da0(1). Always: if `flag`, 0x00554290; if `reason != -1`, sets the per-reason byte
  0x01503e00[reason] := 1; depth += 1.
- **0x00707540(reason) — pause pop.** Does nothing when the depth is already 0. Otherwise depth
  −= 1 and clears the per-reason byte (`reason == -1` clears the whole reason block
  0x01503e00..0x01503e18). **Only when the depth reaches 0**: 0x005542b0, 0x00d9e120/0x00d9e100,
  0x0087cd90, the pausable-clock decrement 0x00db9950, **0x00e0d710(the dword at 0x026e7e6c, 0)**
  and 0x007f1da0(0).
- **0x00708a80(label) — a second, labelled pause push** (latched by the byte 0x01503eac so it
  pushes at most once until cleared; copies `label` into the 128-byte buffer 0x01503e20). When the
  byte 0x01503ead is set it performs the same first-push sequence (pausable-clock increment, then
  **0x00e0d710(0x026e7e6c, 1)**), increments the same depth 0x01503eb0 and marks reason slot
  0x01503e13 (reason 0x13 = 19).
- **0x00708330 — the world-start routine** (it creates the `"default_start"` navpoint, with an
  `sr3_city` special case, then runs a long fixed chain of subsystem resets). In the middle it
  **pops every outstanding pause level** in a loop while the depth is positive, with the same
  depth-0 tail (pausable-clock decrement, **0x00e0d710(0x026e7e6c, 0)**).

**0x026e7e6c is the gameplay Lua state** (`spec-lua-bindings.md` §16.2: the state 0x00a1fa10
creates, loads `game_lib.lua` into, and the Lua-state destructor 0x00a20340 nulls). So the earlier
note's reading of the exempt list — "the interface/UI state opting its own threads out; nothing adds
the gameplay state" — is **wrong on the key**: the only code that ever adds an exempt key adds the
**gameplay** state, and it does so as part of entering game pause. While any pause level is held,
`0x00e0cf50` skips every gameplay-state thread record (it still clears their bit 0), so **no
mission thread runs at all** — neither its delay loop nor anything else. The pausable "game"
millisecond counter 0x029f4db0 stops in the same step. The frame-time global `get_frame_time`
reads keeps being rewritten by every tick (§1.3) and is not affected.

### 1.6 The timer's call sites, and a correction: `0x00702070` is the boot loading-screen thread, not the steady-state pump — CONFIRMED (dumps `delay/r3`–`r6`, `delay/f6`–`f8`, `delay/c6`–`c8`, `delay/x5`, `delay/x6`)

**Arguments at each call of 0x00db9970.**

- 0x005d14b0 (the main loop's per-frame system update; its one caller is the main loop 0x005d3ac0)
  calls it once per frame with `quantize` 0, `externalMs` 0, `quantum` 0, and `fixedRate` = 0 —
  unless the byte 0x01493664 is set, in which case `fixedRate` = the float at 0x012ebc08, **60.0**,
  so every frame reports exactly 1/60 s. That byte is zero-fill and has no instruction writer; its
  address is handed out as data at 0x0071fec2/0x0071fed1 (HIGH CONFIDENCE: an option-registration
  call, i.e. a debug/config toggle, off by default).
- 0x00702070 calls it at the top of every iteration of its loop (loop head 0x0070211e, back-jumps at
  0x00702685 and 0x0070269b), with the same two variants (an extra path through 0x007553c0 when
  the bytes 0x01493662/0x0149abf9 select it).
- The undefined-code site 0x007c8962 calls it once with all-zero arguments on a one-off path
  (HIGH CONFIDENCE: re-baselining the clock after a blocking step; not followed).

**0x00702070 is the "Boot_render" thread.** The spawn site the earlier note did not inspect, read
this pass (0x005d38b2–0x005d38d7 inside 0x005d3ac0's start-up code): it pushes the stack size
0x40000, the literal name **`"Boot_render"`** (0x01123d6c), a priority 5 and the entry point
0x00702070 to the thread-creation helper 0x00db6920. The loop's only exit (0x0070266b–0x007026a0)
fires when 0x00701fb0 reports false, the byte **0x014ff67d is 1** and the state has reached 6; on
exit it clears its "running" byte 0x01493665 and sets the "finished" byte 0x01493663. The main
thread sets **0x014ff67d := 1 at 0x005d395f, right after start-up initialisation**
(0x005d2400, 0x005d25f0, ...), and then spins (10 ms sleeps, 0x00dc4440) until 0x01493663 becomes
1. So the thread exists only to keep a loading screen and the script pump alive **while the game
boots**; it terminates before the first gameplay frame. This corrects
`interp_thread_resume_schedule.md` §1.3 and its answer 1–2 ("a dedicated, always-running
background thread", "~33 Hz") — the loop shape and the `case 6` call it read are right, but that
loop is boot-only.

**The steady-state script tick is 0x00e0dfc0, on the main thread, once per game-state frame.**
0x00e0dfc0 (five call sites, already listed in the earlier note) reads two callbacks at
0x02a44f58 (Lua memory in use) and 0x02a44f54 (the Lua memory budget). If the budget × 0.8 is not
positive it returns **without running scripts**; if usage exceeds 80 % of the budget it runs a full
collection (0x00e0d150) and then the scheduler 0x00e0cf50; if 0x0132b654 is positive and usage
exceeds 60 % it runs an incremental collection step (VM GC option 5) on every created state and
then 0x00e0cf50; otherwise it just runs 0x00e0cf50. The steady callers are per-game-state "update"
callbacks: 0x007ae420 (the front-end/main-menu state update, ending in 0x00846ae0 then
0x00e0dfc0 — the same pair the boot thread runs, minus 0x00848e40), and **0x007b05f0, the update
callback that 0x007b06f0 registers for game state 9** through 0x00706a40 (registered once from the
start-up routine 0x005d25f0, together with enter/exit/query callbacks 0x007b0560, 0x007b05a0,
0x007b05b0, 0x007b0650). 0x007b05f0 runs a fixed list of world updaters (0x00894280(2, 0),
0x007c5740, 0x00704c40, 0x006db140, 0x0090faf0, 0x00a73740, 0x00af9260) and then 0x00e0dfc0 —
**but only when 0x00dafa90 reports false**; that function returns true when the caller is not the
main thread (thread id at 0x01329ea4) or when the streaming module's current-request pointer
0x029cffd4 is set (with a flag variant on its byte +0x1e bit 3). HIGH CONFIDENCE that state 9 is
the in-game state and that 0x00dafa90 means "a blocking load is in progress"; the remaining two
callers are the cutscene driver 0x00702a50 and the debug movie driver (0x00bdbc29).

Consequence for timing: in normal play the timer update (0x005d14b0) and the script tick
(0x007b05f0 → 0x00e0dfc0 → 0x00e0cf50) both run on the main thread, once per main-loop frame. A
mission thread that resumes and reads `get_frame_time` therefore sees **the wall-clock length of
the current main-loop frame** — one frame's delta per resume. (The earlier note's fix
recommendation — "resume on every rendered frame is a strict superset" — is in fact the shipped
behaviour.)

### 1.7 The engine's two millisecond clocks are exposed to engine code through one timestamp object — CONFIRMED (dump `delay/f9`, `delay/r8`)

A 12-byte timestamp object `{u32 deadline, u32 heldRemaining, u8 useRealClock}` is served by the
0x0086be20–0x0086c04a family. "Now" is the pausable game clock 0x029f4db0 when `useRealClock` is 0
and the unpausable real clock 0x029f4db4 when it is 1 — or, when the session object from 0x0087ba20
exists (multiplayer), that session's synchronised counterparts at session +0x68 (game) and +0x74
(real). Set: `deadline := now + ms` (0x0086be90). Hold/release: 0x0086bef0 re-arms `deadline :=
now + heldRemaining` and clears it. Remaining: `deadline − now`, or 0 once passed or while held
(0x0086bf60, 0x0086bfb0). Expired: true when set, not held, and `now ≥ deadline` (0x0086c000).
0x0086be20 and the undefined stub at 0x0086be40 return the game and real "now" respectively. So
every engine-side countdown built on a game-clock timestamp **stops during a game pause**; one built
on the real clock does not. None of these is reachable from a script as a generic delay; they back
domain natives (the HUD/countdown timers and AI/respawn delays seen in §1.1).

### 1.8 The two resume-veto hooks in the runner — CONFIRMED (dumps `delay/f2`, `delay/f3`, `delay/r2`)

Re-reading the runner 0x00e0cba0 shows two optional callbacks consulted before every resume, both
installed once by 0x00a1ff70 (setters 0x00e0ced0 → 0x02a44d60, 0x00e0cee0 → 0x02a44d64):

- **0x02a44d60 = 0x00a1f7b0, "should this thread be killed"**: checked after the kill bit; a true
  result sends the record straight to release.
- **0x02a44d64 = 0x00a1f810, "skip this resume"**: checked after the re-entrancy test; a true result
  returns "alive, not resumed".

Both act **only on records whose `+0x18` flags have bit 0x08 set**; for any other record both
return false. For a flagged record they resolve the record's first 16-bit word through 0x00a33d30 (a
per-object lookup): the kill hook fires when that lookup fails; the skip hook also fires when the
object's slot-0x70 sub-object exists and is rejected by 0x00ad3820 or has a status byte at +0x501
other than 0 or 0x19, and otherwise defers to 0x0096f4f0. So an **object-bound thread** (bit 0x08)
can be held un-resumed indefinitely while its object is in a non-runnable state. A plain
`thread_new` thread is not flagged (its allocator stores flags 0, per the earlier note §1.5), so
this does not affect a mission's main thread or a delay loop inside it. Who sets bit 0x08 was not
traced (OPEN).

### 1.9 Who holds a game-pause level — CONFIRMED for sites and reason codes, HIGH CONFIDENCE for the labels (dumps `delay/c5`, `delay/r1`, `delay/r7`)

Raw scan: **28** call sites of the push 0x007074a0, **32** of the pop 0x00707540, **4** of the labelled
push 0x00708a80, **1** of the world-start full pop 0x00708330 (from 0x0084e3b0). The `reason` each
site passes (read from the immediately preceding pushes):

| reason | push sites | pop sites | where |
|---|---|---|---|
| 0x01 | 0x0072d19f (flag 1); 0x0072bf6d (register-held arguments) | 0x0072d6a2 | the cutscene machine: pushed when a pause/skip menu is opened over a playing cutscene (it also sets bits 0x40/0x80 at cutscene manager +0x35d9); popped by 0x0072d660 in cutscene states 10/11 when bit 0x40 is set and 0x007c0d80 reports the menu closed |
| 0x02 | 0x005b9436, 0x005dd011 | 0x005b9407, 0x005dd048, 0x0071fe8b | gameplay module (0x005b9390) and undefined code |
| 0x04 | 0x007c17ef, 0x007c1f80, 0x008b75c3 | 0x007c20d3, 0x007c30cf | UI dialog module |
| 0x05 | 0x007093f5 | — | undefined code next to 0x00708d10 |
| 0x0a | 0x007abdb3 | 0x007ab839 (beside `"game_lobby_client_ready"`) | multiplayer lobby |
| 0x0b | — | 0x008b6c02 | — |
| 0x0c | 0x007c7244, 0x007c9152 | 0x007c693b, 0x007c6f05 | UI module |
| 0x0d | 0x007af9be, 0x007b3f07, 0x007e1db1, 0x00840f6c | 0x007af5af, 0x007b3f23, 0x007e1b6a, 0x00840f91 | UI/pause-menu module |
| 0x0e | 0x006dbced | 0x006dae4d (and the tail-jump 0x006dadf5) | gameplay module |
| 0x12 | 0x008b6b4e | 0x008b6b93 | — |
| 0x13 | 0x007afaa0, 0x007ea3e3 | 0x007afad3, 0x007afe5b, 0x007aff50, 0x007b0011, 0x007b0078 | UI module (the labelled push 0x00708a80 also marks slot 0x13) |
| 0x17 | 0x00bdbb33 | 0x00bdbf17 | the debug movie driver |
| 0x18 | 0x00bc500e, 0x00bc53fd, 0x00bc549c, 0x00bc578e, 0x00bc5a4e, 0x00bc6e07 | 0x00bc5467, 0x00bc5752, 0x00bc59ac, 0x00bc5a12, 0x00bc6339 | the 0x00bc4xxx–0x00bc6xxx movie-playback module |
| 0x00 / −1 | 0x007c07b1, 0x007c089d (reason 0) | 0x007bf809, 0x007bf9c2, 0x007bfa37 (0); 0x007040b6 (−1, clears every reason byte) | UI module; 0x00703f50 |

Two properties matter for a stuck thread:

1. **The depth is one shared counter; the per-reason bytes are bookkeeping only.** The pop
   decrements the depth whenever it is positive, without checking that its own reason byte was set.
   The gameplay state stays exempt for exactly as long as the shared depth is positive.
2. **No Lua-registered function pushes or pops a pause.** None of the 60 sites lies in the Lua
   binding range (0x00a2xxxx–0x00a6xxxx), and the push/pop functions have no data references. A
   script can only cause a pause indirectly, through the engine flows above (a cutscene with its
   pause menu, a movie, a UI dialog).

0x00707490 is the "game is paused" query (depth > 0); about 30 callers, among them 0x00a295a0 in
the Lua module and the cutscene machine 0x0072d660 at its very top.

---

## 2. Direct answers (drafted for transcription into `spec-lua-api-behaviour.md`'s thread-table / system-library sections)

**1. What is the "delay"?** CONFIRMED negative, HIGH CONFIDENCE positive. There is **no native**
delay, wait or sleep: no registered name of that kind exists, and only `thread_yield` and the stock
`coroutine.yield` can suspend a Lua thread (§1.1). Whatever the mission calls is a **Lua function**,
and by location it can only come from `game_lib.lua` (or the mission's own file). Its text sits in
`misc.vpp_pc`, a per-entry-compressed (mode-a) container whose decompression is parked, so the body
was **not read** (§1.2). The only clock a script can read is `get_frame_time`; the other
time-shaped native, `get_game_time`, returns the calendar time of day. So the delay is necessarily
of the form "repeat: `thread_yield()`, add `get_frame_time()` to a running total, until the total
reaches the requested seconds" (HIGH CONFIDENCE as a constraint on the shape; the loop order and
comparison are not read). A host whose delay differs from that shape should be checked against the
real `game_lib.lua` when the parked container is opened.

**2. What clock does it measure, and can that clock be paused?** CONFIRMED.
- `get_frame_time` returns 0x0132a0b0. That value is **wall-clock time**: the gap in microseconds
  between consecutive calls of the timer update 0x00db9970, converted to seconds. In normal play
  that is once per main-loop frame from 0x005d14b0, on the same thread that runs the script tick,
  so each resume of a delay loop adds **one frame's real duration**.
- It is **not** scaled (the time-scale divisor 0x0132a0bc applies only to the 0x0132a0ac copy),
  **not** capped above (the 0.25 s cap applies only to that copy, so a long hitch frame counts in
  full), and **not** gated by pause: the only whole-function gate (0x029f4dc8) has no writer, and
  the pause counter 0x029f4dcc gates only the separate game-millisecond accumulator 0x029f4db0.
- It has a floor: when a frame is shorter than 1/120 s, the update sleeps the shortfall and
  remeasures. Under the fixed-step option (byte 0x01493664, off by default) every frame reports
  exactly 1/60 s. It is 1/30 before the first update.
- So the value a delay sums **never stops advancing while the delay loop runs**, and is never 0 in
  practice. Game pause, cutscenes and loading cannot freeze it.

**3. Then how can a delay never finish? It is the thread that freezes, not the clock.** CONFIRMED
mechanism; which condition applies to the stuck mission is not known from here. The delay loop only
adds time when its thread is resumed, and the engine stops resuming the gameplay state's threads
in two situations:

- **(a) A game-pause level is held** (§1.5, §1.9). The first pause push adds the gameplay Lua state
  (0x026e7e6c) to the scheduler's exempt list via 0x00e0d710; the scheduler then skips every
  mission thread until the matching last pop removes it. A pause that is pushed and never popped —
  for example the cutscene pause-menu level (reason 1, popped only from cutscene states 10/11 of
  0x0072d660), a movie level (reason 0x18), or a dialog level — freezes every mission thread
  indefinitely, delay included. In the shipped game the world-start routine 0x00708330 pops every
  outstanding level when a world starts, which bounds the damage to one session.
- **(b) The in-game tick is skipped while a load is in progress** (§1.6). 0x007b05f0 does not reach
  0x00e0dfc0 while 0x00dafa90 reports true. HIGH CONFIDENCE that this covers the time a blocking
  stream request is active. It is normally short, but a load that never completes in a host would
  look exactly like a delay that never returns.

Also out-of-band: an object-bound thread (`+0x18` bit 0x08) can be held by the runner's skip hook
(§1.8), but a delay inside a mission's ordinary `thread_new` thread is not subject to it.

**What this means for the stuck mission.** Under the shipped engine's rules, a delay cannot stall
on its clock. If the partner host's delay loop keeps running (its thread is resumed every tick) but
never reaches the threshold, the defect is in the **host's `get_frame_time`**: it is probably
returning 0, a stale value, or a value refreshed on a cadence that does not match the resume
cadence. The engine refreshes it once per main-loop frame, immediately before the same frame's
script tick. A host that drives its mission scheduler from a loop that never calls its frame-time
update, such as a headless "mission drive" loop separate from rendering, reproduces exactly "delay
never returns". The fix is to recompute the frame delta (wall-clock, floor 1/120 s) once per
scheduler tick, before resuming threads. If instead the stuck thread is **not being resumed at
all**, check for an unbalanced pause push (a host-side mirror of 0x007074a0/0x00707540, especially
the cutscene and movie reasons) or a load-in-progress gate that never clears. In both cases this is
a resume problem, not a clock problem.

**Correction to carry into the spec.** `interp_thread_resume_schedule.md`'s statements that
0x00702070 is the steady-state ~33 Hz scheduler, and that "nothing adds the gameplay state to the
exempt list", are both wrong (§1.5, §1.6): 0x00702070 is the boot-only `"Boot_render"` thread, the
steady-state tick is 0x00e0dfc0 called once per frame from the active game state's update, and the
exempt list's only user is game pause, which exempts the gameplay state.

---

## 3. What remains open

- The text of the Lua-side delay in `game_lib.lua` (parked mode-a container). It would settle the
  loop order (yield before or after the add), the comparison (`>=` or `>`), and whether a zero or
  negative argument returns at once.
- The value 0x0072bf6d passes (register-held, the second cutscene push site) and the full meaning
  of reasons 0x02, 0x05, 0x0b, 0x0e and 0x12.
- 0x00dafa90's exact meaning (what 0x029cffd4 holds; writers in 0x00daf120, 0x00daf270, 0x00db0480).
- Who sets thread-record flag bit 0x08 (object-bound threads), and 0x00a33d30's lookup.
- 0x007c8962's one-off timer call (undefined code); 0x01493664's registration call at 0x0071fec2.
- Whether 0x0132b654 (the GC step size gating the 60 % branch) is ever non-zero in the shipped
  configuration.

---

## 4. Cleanroom self-check

Grep of this file against the decompiler-artifact pattern
`\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4}|LAB_[0-9a-fA-F]+)\b`:
**0 hits.** The file also contains no `FUN_`/`DAT_` auto-names and no pseudocode. Control flow is
described in prose over addresses and offsets. The only text from data quoted is short string
literals read from the executable's own `.rdata` (`"Boot_render"`, `"seconds"` and similar).
