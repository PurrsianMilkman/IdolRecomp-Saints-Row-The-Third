# Fresh-start checkpoint value: what the engine passes as `<stem>_start`'s first argument (Team B blocker, mission `dlc2_m02`)

Team A, 2026-10-02. Investigative pass against the real executable (local Ghidra project
`tools/ghidra_projects/SR3`, program `SaintsRowTheThird.exe`, headless `-readOnly -noanalysis`,
`ghidra/CrreishDump.java`). Dump output lives in this session's scratchpad (`fsc/` sub-folders) and is
not committed; everything below is described in prose.

**The question.** Team B's host calls a mission's `<stem>_start(checkpoint, is_restart)` with a chosen
`checkpoint` of `0`; `dlc2_m02`'s script then indexes its checkpoint-data table with that argument and
gets `nil`. What value does the real engine pass on a genuine fresh start, and from which native call
site?

**Inputs read before dumping anything.** `spec-lua-bindings.md` §14.5–§14.10 (the six exhaustive
string-construction negatives and the `mission_checkpoints.xtbl` consumer trace), §16.2/§16.4 (the
per-stem `<stem>_init`/`<stem>_main` load); `spec-tables-progression.md` §10.4 (the table's record
layout). Not re-derived here.

Labels as house style: **CONFIRMED — disassembly**, **HIGH CONFIDENCE**, **HYPOTHESIS**, **OPEN**.

---

## 1. Investigation trail

(Appended incrementally below as each dump is read.)

### 1.1 The string census that reopens the thread (raw scan of the executable's string data) — CONFIRMED

A raw scan for strings mentioning checkpoints or mission starts finds, in the mission module's
`.rdata`, a cluster the earlier passes did not follow: the literal `"mission start"` at 0x0113aa20
(with `"mission checkpoint"` 0x0113ab10, `"mission restart"` 0x0113ab24 and `"checkpoint script
vehicle"` 0x0113aa04 beside it), and, further on, a run of property-name strings at 0x0113c374–
0x0113c500: `script_mission_flags`, `script_mission_level`, **`script_mission_start`**,
`script_mission_cleanup`, `script_mission_setup`, `script_mission_success`, `script_mission_skipped`,
`script_mission_respect`, `script_mission_cash`, `script_mission_display_group`,
`script_mission_unlockables`, `script_mission_display_name`, `script_mission_story_tag` and three
`script_mission_phone_convo_*` names, next to the class names `scripted_mission_start` (0x0113c288)
and `silent_mission_start` (0x0113c530, with its own `silent_cutscene_name` /
`silent_success_function`). §14.7 had seen `scripted_mission_start` only as an RTTI class name and
set it aside; the property names beside it are what matter.

### 1.2 `script_mission_start` is a string property read into a mission object (dumps `fsc/range1`, `fsc/ptrs1`, `fsc/xref2`, `fsc/func2`) — CONFIRMED

- The property names are `{namePtr, id}` descriptors in `.data` at 0x012f39f8–0x012f3a70 (eight bytes
  each); a run of tiny static initialisers at 0x00ff7060–0x00ff72c5 registers each one with
  0x00d9e740 at start-up and stores the returned id in the descriptor's second dword. This is the
  same hash-keyed property vocabulary as `ctg_object_name` (`interp_named_object_resolution.md`
  §2.4).
- Each descriptor has exactly one code reader: 0x006e30d0, the `scripted_mission_start` class's
  "read my properties" method (its only reference is a vtable slot at 0x0113c2a8). It reads, through
  the record-property getters (0x00456590 typed value, 0x00456900 string, 0x00456870 blob,
  0x0084bc50 / 0x00a2efd0 enum/flags):
  `script_mission_flags` → +0x1cc (bit 0 set forces +0xa0 = 3); `script_mission_level` (1-byte)
  → +0x17c; `script_mission_display_group` → +0x180; `script_mission_respect` (4-byte) → +0x178;
  `script_mission_cash` (4-byte float) → +0x174; `script_mission_unlockables` → +0x188 (count at
  +0x184); then the strings: **`script_mission_start` → +0x1f0**, `script_mission_cleanup` → +0x1f4,
  `script_mission_setup` → +0x1ec, `script_mission_success` → +0x1f8, `script_mission_skipped` →
  +0x1fc, display name / story tag → localisation handles at +0x1d0 / +0x1d4, phone-convo name /
  persona / message → +0x1d8 / +0x1e0 / +0x1e4 (message defaults to `"MSN_OBJ_PRE_PHONE_CALL"`).
- **If `script_mission_start` is absent the method returns failure** (and the object is torn down by
  the registry's slot-11 path): a mission object without a start-function name cannot exist.

So the name of a mission's `_start` function is **authored data**, carried as a string property of
the mission's `scripted_mission_start` object, not a literal or a runtime-built string in the
executable. That is exactly why §14.6–§14.10's six string-construction searches all came back
empty: the bytes `m01_start` are not in the executable at all; they arrive with the object's record.

### 1.3 The native call site: 0x006d5460 (dump `fsc/func1/func_0x006d5460.txt`; raw sweep for `+0x1f0` reads in 0x006c0000–0x00700000) — CONFIRMED

A linear sweep of the mission module for reads of a `+0x1f0` field finds only two outside the
property reader and constructors: 0x006d5620 (in 0x006d5460) and 0x006d93e5 (undefined code,
§1.6). 0x006d5460 is the fresh-start launcher. In order:

1. Requires the global current-mission object pointer at 0x014c8460 to be non-null and of a class
   whose descriptor row (0x02cc9900, +0xc) has bit 0x8; refuses (returns false) on several game-state
   gates (0x009a1dc0 on its second argument, 0x006c4420, player-state checks through 0x009da4e0 /
   0x009df3d0 / 0x009dda80 / 0x006c4990), and requires 0x006e07a0(first argument) to resolve.
2. Posts a mission-type event (0x00802eb0 with 0x86 / 0x85 / 0x84 according to the resolved
   object's +0xa0 = 0 / 1 / 3).
3. In the branch taken when the local session is the authority (0x0087ba20's +0x5c equals +0x58):
   0x006097a0(1,1), 0x006d86f0(5), builds and sends a 0x44-byte network message of type 2, then
   0x006d43d0(0,0) on the checkpoint block at 0x014c8570, then — **with ESI = the literal
   `"mission start"` (0x0113aa20) — calls 0x006ce930**.
4. **0x006ce930 is "set current checkpoint name"**: it frees the previous heap string held at
   0x014c8354, allocates `strlen+1` bytes (allocator 0x00e0e1e0, tag 6), copies the name in and
   stores the new pointer at 0x014c8354. Twelve call sites (0x006d5060 — the `mission_set_checkpoint`
   handler's target — among them), so 0x014c8354 always holds the name of the mission's current
   checkpoint.
5. Then: `0x00e0ca80(name = currentMission+0x1f0, state = 0x026e7e6c, 0, 0)`. Per
   `interp_thread_resume_schedule.md` §0 and this pass's own read of 0x00e0c720, this looks the name
   up in the gameplay state's globals (flags 0,0 = plain global lookup), requires a Lua (not C)
   function, creates a new coroutine, moves the function onto it, and appends a thread record
   (not-yet-started byte = 1, argument count +0x1c = 0, name copy at +0x10). It returns the record,
   or null if the global is missing / not a Lua function / the table is full.
6. If a record came back: its 16-bit id is saved at 0x014c848e (the mission thread id), then
   **`0x00e0cfb0(this = record, [0x014c8354])`** pushes the current checkpoint name onto the new
   thread as a Lua string (0x00e0cfb0 pushes nil instead when the pointer is null; either way it
   bumps the record's argument count), and **`0x00e0ce00(this = record, byte [0x014c8324])`** pushes a
   boolean. No 0x00e0cd00 call follows: the thread is left not-yet-started for the per-frame
   scheduler 0x00e0cf50 to start with those two arguments.
7. Afterwards 0x006e13e0(1,1) and 0x006d1e30 (ESI = first argument) run; returns true.

**So the real engine calls `<script_mission_start value>(checkpointName, flag)` with
`checkpointName` = the string `"mission start"` on this path**, set one instruction pair before the
push. It is a Lua string, never a number.

### 1.4 Where 0x006d5460 is reached from: the mission-start marker's "entered" callback (dump `fsc/range2/range_0x006d5730-0x006d5a60.txt`) — CONFIRMED

0x006d5460 has no ordinary caller except one undefined-code site; its main use is as a stored
callback. Inside 0x006d86f0's body, the block at 0x006d5730–0x006d5981 (reached for the current
mission when its phone-call pre-start is in progress, flag 0x014c8358 clear) plays the mission's
pre-start phone conversation (object +0x1e4 message; +0x1d8 conversation name, tested for the
`"dlc1_"` / `"dlc2_"` / `"dlc3_"` prefixes to pick a DLC string-table hash), places the start
marker (effect `"vfx_missioncheckPointicon"`), and **writes 0x006d5460 into the marker's +0xe8
callback slot** (0x006d5810). The block at 0x006d5990–0x006d5a25 calls it directly
(`0x006d5460(marker, player)`, 0x006d5a1b) when the marker's callback slot is set and the session
is the authority; the non-authority branch reads a byte from a network stream and forwards
instead. So 0x006d5460 is "the player walked into the mission's start marker": the ordinary way a
story mission begins.

### 1.5 The second launch site 0x006d9310 (undefined function; dump `fsc/range2/range_0x006d9300-0x006d9470.txt`) — CONFIRMED

0x006d9310 is not a Ghidra function; its only reference is a dword at 0x012f473c in the per-state
callback table of the game-mode state machine at 0x012f45d8 (current state at 0x012f45c8, pending
at 0x012f45cc, driven from 0x00704420–0x00704ea9), paired with 0x006d9820 at 0x012f4740 — i.e. it
is the "enter" handler of one game-mode state (which numbered state was not resolved; OPEN, minor).
It calls 0x006d4a10 (mission set-up: HUD, notoriety, co-op handshake, and — when the flag
0x014c8324 is set and the session is the authority — 0x006d3780, the restore-player-state-from-
checkpoint routine), then:

- **if the flag byte 0x014c8324 is clear and 0x006e2ba0(current mission) is true** — 0x006e2ba0
  returns "the mission object's +0x1d8 phone-conversation name is non-null" (and false while the
  byte 0x014f3b20 is set) — it does *not* start the script: it moves to state 2 (0x014c7a14 = 2)
  and enters the phone-call / start-marker sequence of §1.4 through 0x006d86f0's code at
  0x006d5720. The script then starts later, from 0x006d5460, with `"mission start"`.
- **otherwise** (no pre-start phone call, or a restart), when the session is the authority it does
  exactly what §1.3 step 5–6 does, at 0x006d93df–0x006d9421: `0x00e0ca80(currentMission+0x1f0,
  0x026e7e6c, 0, 0)`, saves the record id at 0x014c848e, pushes **the string at 0x014c8354** and
  **the byte 0x014c8324 as a boolean**. Here the checkpoint name is *not* re-set immediately before
  the push; it is whatever 0x014c8354 holds (§1.6).

These two are the only reads of a `+0x1f0` field in 0x006c0000–0x00700000 apart from the property
reader and the constructor's clear (raw sweep; 0x006e2dec / 0x006e324b / 0x006ed35a are writes), and
the only places 0x014c848e (the mission thread id) is written with a fresh record id
(other writers — 0x006d8cfe stores 0xffff while clearing the current-mission block; 0x006d8c48 in
0x006d89a0, 0x006d97a0 in 0x006d9470 and 0x006db131 in 0x006db050 sit in similar block-reset /
block-copy sequences and store a register whose value was not traced — none follows a 0x00e0ca80
call).
HIGH CONFIDENCE that these are the only two native call sites of a mission's start function; a
raw-scan census of every 0x00e0ca80 caller whose name argument comes from a `+0x1f0` field was not
run binary-wide (see §3).

### 1.6 The lifecycle of the current-checkpoint name 0x014c8354 — CONFIRMED (disassembly at each site)

- **Initial value.** 0x006d6010, the mission system's one-time initialiser (single caller,
  0x00707de0 in the game set-up routine 0x00707c80), clears the 0x110-byte current-mission block at
  0x014c8460, clears the pending-checkpoint mission handle 0x014c8328/+0x2c/+0x30, creates the two
  pools named `"mission restart"` and `"mission checkpoint"`, and then **allocates 14 bytes and
  copies `"mission start"` into them as the initial 0x014c8354** (0x006d60f3–0x006d6129). So the
  name is never null after start-up, and its resting value is `"mission start"`.
- **Every "begin a mission" entry resets it to `"mission start"`** before calling 0x006d6920 (the
  "make this the current mission" routine, §1.7): 0x006d7990, 0x006d7b40, 0x006d82a0 and the
  undefined block at 0x006d7920 unconditionally; 0x006d8450 (the district-mini-mission auto start —
  it walks the `district_*` / `mm_*` pairs at 0x012f2540) and 0x006db140 when the name is non-null
  (always true after §1.6's first bullet); 0x006d5460 (§1.3) and 0x006d5f90 (§1.8)
  unconditionally.
- **The one parameterised entry, 0x006d7120(mission, flag, checkpointName)**, sets the name to its
  third argument when that differs from the current one, **substituting `"mission start"` when the
  argument is null** (0x006d7197–0x006d71aa). Its callers include 0x006db140 at 0x006dbc75, which
  passes the pending mission handle 0x014c8328 together with the current checkpoint name — the
  "restart from last checkpoint" path. (Other callers: 0x00672350, 0x006e8a10, and undefined code at
  0x0061b47e / 0x0061b492 / 0x006d71f4 — not read.)
- **Script-reported checkpoints.** 0x006d5060 — the target of `mission_set_checkpoint`'s handler
  0x00a52600 (`spec-lua-bindings.md` §14.7) — passes its first argument, the checkpoint name the
  script supplied, straight to 0x006ce930 (0x006d50de–0x006d50e0), after gating on a current mission
  existing, the mission state not being 8, and two global handles at 0x012f2630 / 0x012f2634 being
  clear. So once a script reports e.g. `"m01_checkpoint_survive"`, that string becomes the value a
  later restart passes back into the start function.

### 1.7 The second argument: 0x014c8324 is "this is a restart of the mission that was in progress" — CONFIRMED

0x006d6920(ESI = mission), called by every begin-a-mission entry above, makes the mission current
(0x006d1bf0 on 0x014c8460) and then sets **0x014c8324 = (0x014c8328 == this mission)** and clears
0x014c8328 (0x006d6b2d–0x006d6b3e). 0x014c8328 is the handle of the mission the player was in the
middle of: it is written from a save/network stream by 0x006d5f90, set to the failing mission by
the mission-failed handler 0x006d8fb0 just before it invokes its retry callback (0x006d9100,
callback 0x006d8de0 or the block at 0x006d8ca0), and cleared by the initialiser, by 0x006d6920
itself after the test, and by 0x006d89a0 (mission end). The same failure handler string-compares
0x014c8354 against `"mission start"` (0x006d903c–0x006d9066) and uses the result to decide what the
failure screen offers (0x007bd890's sixth argument) — i.e. the engine itself treats
`"mission start"` as "no checkpoint has been reached yet". The only other
writer of the flag 0x014c8324 itself, 0x006d4a10 at 0x006d4be0–0x006d4be9, overrides it for the
co-op authority when the mission type (+0xa0) is 1, with the result of comparing the dword
0x014b2d70 against ESI (the register's value there was not traced; HYPOTHESIS: a "no partner"
sentinel test).
So on a genuine fresh start — the mission has never been in progress — **the second argument is the
Lua boolean `false`**, and it is a boolean (0x00e0ce00 → the boolean push), not a number.

### 1.8 Save-game load does not carry a checkpoint name (dump `fsc/func1/func_0x006d5f90.txt`) — CONFIRMED (shape), HIGH CONFIDENCE (role)

0x006d5f90 (a vtable entry at 0x011697f8, the mission system's load-from-stream method) reads a
flag byte; if set it reads a handle into 0x014c8328 (the in-progress mission). It then
**unconditionally** resets the current checkpoint name to `"mission start"` and clears the
checkpoint block 0x014c8570 (0x006d43d0(0,0)). So a mission interrupted by a save/quit and later
resumed from that save restarts with `checkpoint = "mission start"` and `is_restart = true`; the
script-reported checkpoint name is not persisted through this path.

### 1.9 Cross-check against `mission_checkpoints.xtbl` — CONFIRMED (data, both shipped copies read)

Neither copy of the table (`tools/ai_base_xtbl/mission_checkpoints.xtbl`, 139 rows, and the
166-row patch copy) has a row whose `CheckpointName` is `"mission start"`, and every mission's
authored `Index` values start at 1 (no row has `Index` 0). `CheckpointName` values are strings
(`"m01_checkpoint_plant_bombs"`, `"get the goods"`, ...); `dlc2_m02`'s four rows are the strings
`"1"`, `"2"`, `"3"`, `"4"` with indices 1–4. This is consistent with §1.6: the table lists only the
checkpoints a script reports through `mission_set_checkpoint`, and the start of a mission is the
implicit, unlisted name `"mission start"` (so 0x006df9e0's special case for that name,
`spec-lua-bindings.md` §14.7, can never find a matching row in the shipped data; what it returns
then was not re-read this pass). Nothing anywhere in the engine's
start path produces the number 0.

---

## 2. Direct answer

**What does the real engine pass as `<stem>_start`'s first argument on a genuine fresh start?**
The **Lua string `"mission start"`** — the literal at 0x0113aa20, copied into the current-checkpoint
name 0x014c8354 one call before the push (§1.3 step 4; also the value 0x014c8354 is initialised to
and reset to by every begin-a-mission entry, §1.6). It is pushed with the string push 0x00e0cfb0.
**`0` is never passed** by any path read here; nor is `-1` or nil (nil would need 0x014c8354 to be
null, which it never is after mission-system initialisation). On a restart the same argument is the
last name the script reported through `mission_set_checkpoint` (or `"mission start"` again if none
was reported, or after a save-game load, §1.8).

**Second argument (`is_restart`):** the Lua **boolean** `false` on a genuine fresh start; `true`
when the mission being started is the one recorded as in progress (retry after failure, resume
from a save), §1.7.

**Which function is called, and from where:** not a name built from the mission stem. The function
name is the `script_mission_start` string property of the mission's `scripted_mission_start`
object (authored data, object +0x1f0, §1.2). Two native call sites start it, both through the
thread-creating path 0x00e0ca80 → 0x00e0c720 on the gameplay state 0x026e7e6c with lookup flags
(0,0), followed by exactly two pushes (string, boolean) and no immediate run — the per-frame
scheduler starts the coroutine:

| site | when | first argument |
|---|---|---|
| 0x006d5460 (callback in the start marker's +0xe8 slot; direct call at 0x006d5a1b) | player enters the start marker after the pre-start phone call | `"mission start"`, set immediately before |
| undefined code at 0x006d9310 (enter handler of a game-mode state, table slot 0x012f473c) | mission with no pre-start phone call, or any restart | current checkpoint name 0x014c8354: `"mission start"` on a fresh start, the last reported name on a retry |

**For a host (Team B):** call the mission's start function as
`start_fn("mission start", false)` on a fresh start, as a new coroutine, not as a plain call; the
function to call is the object's `script_mission_start` name (which for shipped missions follows
the `<stem>_start` pattern the peer census found, but that is a property of the data, not of the
engine). To resume at a checkpoint, pass the checkpoint's **name string** exactly as the script
reported it through `mission_set_checkpoint` (equivalently, the `CheckpointName` column of
`mission_checkpoints.xtbl`, e.g. `"1"` — a string — for `dlc2_m02`), with `true`.

**What this resolves in the existing spec (for whoever transcribes it):**
- `spec-lua-bindings.md` §14.7–§14.10's long-OPEN "how is `<stem>_start` invoked": by name, but the
  name is a **data-borne string property**, so all six string-construction negatives were correct
  and simply searched the wrong place. §14.9's candidate (b) "function value captured at chunk
  load" is not what happens; candidate (a) "numeric dispatch" is not either — it is a plain global
  lookup of an authored name.
- §14.2's "no dedicated mission start hook": still true as a *named hook*; the mission start is this
  data-named coroutine launch.
- The same object carries the names of the mission's other script entry points:
  `script_mission_setup` (+0x1ec), `script_mission_cleanup` (+0x1f4), `script_mission_success`
  (+0x1f8), `script_mission_skipped` (+0x1fc). Their call sites were not traced this pass (§3).

Labels: the two call sites, the pushes, the initial value, the reset sites, the flag computation
and the property reader are **CONFIRMED — disassembly**. "These are the only two start sites" is
**HIGH CONFIDENCE** (module-wide `+0x1f0` sweep plus the mission-thread-id writer census, not a
binary-wide census of 0x00e0ca80 callers). That shipped missions' `script_mission_start` values are
`<stem>_start` is **HYPOTHESIS** (the object data lives in parked `.czn_pc` placement records or
another unread carrier; not opened). That `dlc2_m02`'s script indexes its checkpoint data by these
name strings (so `"mission start"` hits and `0` misses) is a **script-side question for Team B**, not
settled from the executable.

---

## 3. OPEN / what would settle the remainder

1. **Binary-wide census of 0x00e0ca80 / 0x00e0c720 callers whose name argument is a `+0x1ec` /
   `+0x1f0` / `+0x1f4` / `+0x1f8` / `+0x1fc` field** (raw `calls` scan of both, then a one-line
   disassembly check of the instruction feeding the name). Moves "only two start sites" to
   CONFIRMED and finds where setup / cleanup / success / skipped are fired, with their argument
   shapes.
2. **Where `scripted_mission_start` objects come from.** The descriptor 0x012f3a08 has no writer in
   code (only the registration initialiser and the reader), so the value can only arrive in a
   serialised record (`interp_named_object_resolution.md` §2.5: zone 0x2234 batch, vehicle batch,
   or the 0x00a36a80 stream). Trace the class's constructor 0x006e33a0 / instantiator 0x006e2c60
   outward to its creator, and whether a mission `_modal` container or the city zone carries it.
   Opening the actual record bytes would need the parked `.czn_pc` interior — a user decision.
3. **Which game-mode state 0x006d9310 enters** (the stride and index of the 0x012f45d8 table), and
   0x006d9820, its paired exit handler.
4. **The `silent_mission_start` sibling class** (`silent_cutscene_name`, `silent_success_function`,
   descriptors 0x012f3adc / 0x012f3ae4, reader in undefined code near 0x006e36f9) — a second kind
   of mission start that may call a script function with a different argument shape.
5. **0x006d7120's remaining callers** (0x00672350, 0x006e8a10, undefined 0x0061b47e / 0x0061b492 /
   0x006d71f4) — whether any of them (e.g. a mission-replay menu) passes a checkpoint name other
   than the current one or `"mission start"`.
6. **Script side, Team B:** confirm in `dlc2_m02.lua` that the checkpoint table is keyed by the
   string names (`"mission start"` plus `"1"`..`"4"` or constants equal to them).

Dumps for this note are in the session scratchpad under `fsc/` (`xref1`–`xref3`, `func1`–`func5`,
`range1`–`range2`, `ptrs1`–`ptrs2`, `calls1`) plus the capstone sweeps `fsc/cdis.py` and the string
scans `fsc/strscan.py` / `fsc/strrange.py`; the private project copy `tools/gp_fsc` is deleted at
the end of the pass.
