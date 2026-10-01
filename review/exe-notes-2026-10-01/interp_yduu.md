# Interpretation of job yduu (Team B request 8: which Lua state loads which preload, and in what order)

Dumps: `/root/crreish-bus/results/20261001T021641-team-a-yduu/` (str, func, callers), produced by
`team-a/ghidra/jobs/teamb-request-8-preloads.json`. Everything below labelled CONFIRMED was read in
these dumps; where a function body is not in the dumps I say so and label accordingly.

Labels: **CONFIRMED** (disassembly in these dumps), **HIGH CONFIDENCE**, **HYPOTHESIS**, **OPEN**.

## 0. Short answer to Team B

- `vint_lib.lua` is loaded at exactly one place in the binary, into the state that the interface
  bring-up creates and stores in 0x02a45450, after the 55 `vint_*` natives (including
  `vint_is_std_res`) have been registered in that state. `game_ui_globals.lua`, `vdo_base_object.lua`,
  `vdo_anim_object.lua`, `vdo_input_tracker.lua` are loaded from one 4-entry array, in that order, into
  the state returned by the interface-state getter 0x00e1a1b0 (= 0x02a45450). `game_lib.lua` is loaded
  at exactly one place, into the state the gameplay bring-up just created and stored in 0x026e7e6c.
  `system_lib.lua` is loaded by the generic state creator into every state it creates (both).
  **CONFIRMED for every static load site in the binary** (the by-name loader has exactly five callers
  and all five are in these dumps; every preload string's every reference is accounted for).
- What remains outside the static evidence is dynamic only: the Lua `include` queue (a script can ask
  for any file by name; it is loaded into the state of the script that asked) and the mission-stem
  file `<name>.lua` whose stem comes from a caller argument. Neither can load a preload into the wrong
  state unless a shipped script or a caller passes a preload name; see §5.
- Team B's symptom is exactly what the real engine would never produce: `vint_lib.lua` line 96 runs at
  chunk top level and needs `vint_is_std_res`; in the engine that global exists because the registrar
  0x00e1dfb0 runs before the load, in the interface state only.

## 1. The loaders (the mechanism everything else uses)

### 1.1 0x00e0df90 — "load a Lua file by name into a state" (CONFIRMED)

Three arguments, all passed on the stack, in this order: file name, Lua state, allocator object.
(The decompiler shows two; the third is read into a register and handed to the worker. Every one of
the five call sites pushes three.) Behaviour:

1. Calls the worker 0x00e0d800 (name, state; allocator in a register). If that returns false, 0x00e0df90
   returns false immediately and does nothing else.
2. If it returns true, 0x00e0df90 calls the queue drain 0x00e0d980 (state and allocator in registers)
   and returns true.

So the return value is "the named file was opened AND its chunk compiled AND its top-level ran
without error" (see 1.2). The results of the queued files loaded by the drain are ignored.

Callers (whole binary, from `callers/xref_0x00e0df90.txt`): 0x008489e0 (UI preload array loop),
0x00a1fa10 (`game_lib.lua`), 0x00a1fa90 (`<stem>.lua`), 0x00e0e0b0 (`system_lib.lua`), 0x00e1e460
(`vint_lib.lua`). Five uses in five functions. **CONFIRMED.**

### 1.2 0x00e0d800 — the file worker (CONFIRMED)

Arguments: name and state on the stack; the allocator object in a register (it is a C++ object with
a vtable; three virtual methods are called on it).

1. If name, state or allocator is null → return false.
2. Builds the path in a 0x400-byte local buffer with 0x00e0cff0: if the name does not already end in
   `.lua` (case-insensitive), `.lua` is appended; a name that would overflow the buffer makes this
   step fail → return false. (0x00e0cff0 body is in the dump. CONFIRMED.)
3. Opens the file through the engine file API with the mode string at 0x0129a85c, whose bytes are
   `rb` (CONFIRMED bytes; that it is an fopen-style open is HIGH CONFIDENCE). Open failure → return
   false. **This is the only "cannot open" case.**
4. Reads the size; calls the allocator's virtual method at vtable +0x5c (saves the result), then the
   method at +0x38 with the size to get a buffer; a null buffer → return false. Reads the whole file,
   closes it.
5. Calls 0x00e0d150: for every Lua state the engine has created (slot table at 0x02a44da8, count at
   0x02a44f50) it calls 0x00dfeb60(state, 2, 0). HIGH CONFIDENCE this is a full garbage collection
   of every state before each file load (the constant matches Lua 5.1's collect request; 0x00dfeb60's
   body is not in the dumps).
6. Compiles the buffer with 0x00fcb3b0(state, buffer, bytes read, chunk name) where the chunk name is
   the name string exactly as passed (not the `.lua`-appended path). 0x00fcb3b0 wraps a reader
   function around the buffer and calls the core load routine. HIGH CONFIDENCE this is the standard
   buffer-load API.
7. If the compile status is 0, runs the chunk with 0x00dfeaa0(state, 0 args, all results, no error
   handler) — the protected call. HIGH CONFIDENCE this is the standard protected call (the dump shows
   it computing the function slot from the stack top and the "-1 results" case adjusting the stack
   ceiling).
8. Calls 0x005590a0 on the allocator (calls its vtable[0]; if that returns true, clears the two fields
   that step 4 set), then the allocator's method at +0x60 with the value saved in step 4. HYPOTHESIS:
   a mark/release scratch-allocator protocol; the file buffer itself is never freed by name.
9. Returns true only if the final status (compile, or run if compile succeeded) is 0.

Consequences (CONFIRMED from the control flow): a syntax error or a runtime error in the file's
top-level chunk makes the loader return false, exactly like a missing file. No error message is
read from the Lua stack and nothing pops the error value; the code after the protected call does no
Lua stack operations at all.

### 1.3 0x00e0d980 / 0x00e0d930 — the deferred `include` queue (CONFIRMED)

Globals: entry array at 0x02a44e48, count at 0x02a44f48.

- 0x00e0d980 (state and allocator in registers): if the state is null, returns. Otherwise walks the
  queue from index 0, calling 0x00e0d800(entry, state) for each entry; the count is re-read from the
  global on every iteration, so entries appended during the walk (a queued file that itself queues
  another) are processed in the same walk, in order. Each entry's result is ignored. Then it tail-calls
  0x00e0d930.
- 0x00e0d930: frees every entry with the C runtime free (through the pointer at 0x0132b64c), zeroes the
  slots, sets the count to 0.
- Writers of the queue (from the xref list only; the body is not in this dump): 0x00e0e140 writes an
  entry at 0x00e0e1c0 and the count at 0x00e0e1c8, and reads existing entries at 0x00e0e160
  (HYPOTHESIS: a duplicate check). 0x00e0e140 is called only from the Lua global `include`
  (0x00e0f010), per spec §16.3 (job bgcx; not re-read here).
- Readers: 0x00e0d980 (both loaders drain through it: 0x00e0df90 after a successful file load,
  0x00e0de80 after a successful buffer load) and 0x00e0d930 (also called directly by 0x00e0de80 on a
  failed load, i.e. the queue is discarded without loading).

**Behaviour for a host (CONFIRMED mechanism, HIGH CONFIDENCE link to `include`):** `include(name)`
does not load anything when called. It appends the name to a single global queue. When the chunk that
called it finishes (the enclosing loader call returns from the protected call), the loader loads each
queued file by name into the same state, through the same file worker (so `.lua` is appended if
missing, the chunk name is the queued string, and a failed include is silently ignored). Nested
includes are appended to the same queue and loaded in the same pass. The queue is global, not per
state; it is drained by whichever loader call finishes next.

**Correction to spec §16.3:** the 64-slot list that 0x00e0e140 feeds is not "bookkeeping with no
connection to the real file-loading path". It is the input of 0x00e0d980, which both real loaders
call. (CONFIRMED: same globals 0x02a44e48/0x02a44f48, written by 0x00e0e140, read by 0x00e0d980.)

### 1.4 0x00e0de80 — "load a Lua chunk from a memory buffer" (CONFIRMED)

Five stack arguments: buffer, size, name, state, allocator. Not a file loader: it never opens a file.

1. If state or name is null → false.
2. Builds `<name>.lua` into a local buffer with the same 0x00e0cff0 helper; it becomes the chunk name.
3. Full GC of every state (0x00e0d150, as in 1.2 step 5).
4. Pushes the global `_PrepareForDynamicGlobals` (string push 0x00dfe420, then global-table lookup
   0x00dfe670 with the globals pseudo-index -10002), pushes the chunk name, and calls it with 1
   argument, 0 results (0x00dfea40: an unprotected call).
5. Compiles the buffer (0x00fcb3b0) and, on status 0, runs it with the protected call.
6. If the status is one of 2, 3, 4, 5 (Lua 5.1's run/syntax/memory/handler errors): discards the
   queue (0x00e0d930) and returns false. Otherwise: drains the queue into this state (0x00e0d980),
   calls the global `_DynamicGlobalsLoadComplete` with 0 arguments, returns true.

Callers (whole binary): 0x006cfac0 at 0x006cfb58 and 0x00e213d0 at 0x00e214f2. Neither body is in the
dumps. 0x006cfac0 reads the gameplay state global 0x026e7e6c at 0x006cfb38, 32 bytes before the call
(xref list) — HIGH CONFIDENCE that this is the mission-script-resource load into the gameplay state
(spec §14.1/§14.5). 0x00e213d0's state is not visible in these dumps (it does not read 0x02a45450
directly; it may call the getter 0x00e1a1b0, whose caller list is truncated) — OPEN, see §7.

Note the asymmetry with 1.2: the buffer loader fires the two `_…DynamicGlobals…` hooks and checks
the status against the four error codes; the file loader fires no hooks and checks "status == 0".

## 2. The generic state creator 0x00e0e0b0 (CONFIRMED)

Four stack arguments: name string, a flag, allocator object, a function pointer. Callers (whole
binary): 0x00e1e460 (`"interface"`, 0, the Vint heap object, 0) and 0x00a1fa10 (`"game play"`, 1,
0x026e9470, 0x00754410). Two uses in two functions.

1. If the created-state count at 0x02a44f50 is already 4, or the allocator argument is null → return 0.
   (At most four states can ever be created; the second argument — 0 or 1 — is never read by this
   function: the four stack slots read are name, allocator and the function pointer.)
2. 0x00fcb490: creates the raw Lua state (the core state constructor 0x00e00530 with an allocation
   callback 0x00fcb420 and null user data) and installs a panic handler (0x00dfddf0 with 0x00fcb460).
   HIGH CONFIDENCE these are the standard new-state and at-panic calls. Null → return 0.
3. If this is the first state ever created, its pointer is also stored at 0x02a44f4c.
4. Fills slot record `count` (0x30 bytes each, base 0x02a44d88): name copied with a 0x20-byte bounded
   copy; +0x20 = state; +0x24 = the function-pointer argument (0 for interface, 0x00754410 for
   gameplay); +0x28 = 0; +0x2c = 0. Count incremented.
5. 0x00fccb70(state): calls 0x00fcca70 (the base-library opener, per spec §17 — body not in this
   dump) and then registers a library table named `coroutine` (0x00fcb970 with the function table at
   0x012934e8). Returns 2. **CONFIRMED: base + `coroutine` are the only stock libraries opened here;**
   nothing else in the creator opens a library. (That 0x00fcca70 is exactly the base library is from
   §17, not this job.)
6. 0x00e0f900(state): the 24 bare globals (loop count 0x18 read directly; names visible in the dump:
   `acos`, `ceil`, `debug_print`, `assert_msg`, `floor`, `get_frame_time`, `include`, `rand_float`,
   `rand_int`, `round`, `sizeof_table`, `sqrt`, `strstr`, `thread_check_done`, `thread_kill`,
   `thread_new`, `thread_yield`, `closest_point_on_line_segment`, `which_side_of_2d_line`, plus five
   whose string labels Ghidra did not render in this dump). Same mechanism as every other registrar:
   push C closure (0x00dfe4f0) then set field on the globals pseudo-index (0x00dfe830, -10002).
7. 0x00e0df90(`"system_lib.lua"`, state, allocator). **Return value ignored.**
8. Returns the state.

So every state gets, in this order: raw state → base + coroutine → 24 bare globals → `system_lib.lua`.
Confirms spec §16.4's bare-globals bullet (job bgcx) and upgrades its "(stock libraries,
HYPOTHESIS)" to: base and `coroutine` only, opened between the raw state and the 24 globals.

## 3. Interface state: exact load sequence (CONFIRMED)

Top-level UI bring-up 0x008489e0 (single caller 0x005d2400 at 0x005d245c). Everything below is read
from its straight-line body and the bodies of its callees in the dump.

Pre-Lua part (no Lua state exists yet): installs five groups of callback pointers into static tables
(0x02317aec…, 0x02317adc…, 0x02317ab4…, 0x02317a78…, 0x02317a30…) through five registration helpers;
creates a named memory heap with 0x00db52d0 (name `"Vint"`, pool object 0x01493a78) and stores it at
0x02317b60; then calls 0x00e23910(`"game.vint_proj"`, …, 0x014971ac, [0x02317b60]). 0x00e23910 stores
its fifth argument at 0x02a75578 and its sixth — the `Vint` heap — at 0x02a7557c, then runs 25
subsystem initialisers (none of which is a Lua loader or registrar; depth-1 bodies not dumped).

**This closes the OPEN in spec §13.5/§16.1 ("a Lua-state value read from a fixed global before the
call that creates the state"):** the value read at 0x00848cb5 from 0x02a7557c is the `Vint` heap
object written a few instructions earlier by 0x00e23910, not a Lua state. It is the allocator object
that every interface-state file load passes as its third argument (CONFIRMED that it is the same
value as 0x02317b60; HIGH CONFIDENCE that it is a heap/allocator, from its creation through a pool
helper with a name and its use through virtual alloc/release calls in 1.2).

Lua part, in order:

| # | Site | What happens | State |
|---|---|---|---|
| 1 | 0x00848cc0 → 0x00e1e460(callback 0x008430f0, heap 0x02a7557c) | see 1a–1g | — |
| 1a | 0x00e1e473 | 0x00e0e0b0(`"interface"`, 0, heap, 0): raw state, base + coroutine, 24 bare globals, **`system_lib.lua`** (§2) | new |
| 1b | 0x00e1e479 | result stored at 0x02a45450 (the only write to that global in the binary) | 0x02a45450 |
| 1c | 0x00e1e47e → 0x00e1dfb0 | **55 `vint_*` natives** registered (loop count 0x37; names in the dump; `vint_is_std_res` → 0x00e1a150) | 0x02a45450 |
| 1d | 0x00e1e486 → 0x00e19610 | free-list/pool initialisation, no Lua | — |
| 1e | 0x00e1e49a | the callback argument, if non-null, is called with one argument, the state: here 0x008430f0, the 311-name UI registrar (§13.2; body not in this dump) | 0x02a45450 |
| 1f | 0x00e1e4ac | 0x00e0df90(**`vint_lib.lua`**, [0x02a45450], heap). Return value ignored. | 0x02a45450 |
| 1g | 0x00e1e4b7 → 0x00e0d4e0 | sandbox bootstrap variant A: three source strings loaded with the string loader 0x00fcb3e0 and each run with the protected call if it compiled (`_UGGlobals = getfenv()`; `setmetatable(_UGGlobals, {__index = _GetDynamicGlobal, __newindex = _CatchNilAssignment})`; `setfenv(1, _UGGlobals)`). Errors ignored. | 0x02a45450 |
| 1h | 0x00e1e4c0 → 0x00e1e6c0 | second pool initialisation, no Lua; returns 1 | — |
| 2 | 0x00848ccc → 0x00e2c900(0x013012a8, 0x2c) | passes a static table and a count to a small helper (body not examined; not a Lua loader or registrar) | — |
| 3 | 0x00848cd1 → 0x00846a10 | input-binding tables, no Lua | — |
| 4 | 0x00848cd6/0x00848cdc | 0x00845aa0(0x00e1a1b0()) — **113-name registrar** (loop count 0x71) | 0x02a45450 |
| 5 | 0x00848ce6, 0x00848cf0 | two callback pointers stored (0x02a5a020, 0x02a5a024) | — |
| 6 | 0x00848cf8…0x00848d0c | 0x00843000, 0x00844bc0, 0x008464b0, 0x00843090, 0x008416d0: button-name lookups, control-image tables, etc. None calls a loader or the registration primitives (checked: the only loader calls in the whole 0x008489e0 dump are 1f and step 7; the only registration-primitive calls are inside 0x00845aa0). | — |
| 7 | 0x00848d20–0x00848d62 | **preload array loop**: for i in 0..[0x01301a00] (file-backed value **4**): state = 0x00e1a1b0(); ok = 0x00e0df90([0x01162da0 + 4·i], state, [0x02a7557c]); if !ok → see §4. Order from the array slots: 0x01162da0 `game_ui_globals.lua`, 0x01162da4 `vdo_base_object.lua`, 0x01162da8 `vdo_anim_object.lua`, 0x01162dac `vdo_input_tracker.lua`. | 0x02a45450 |
| 8 | 0x00848d73 | 0x008452e0([0x02317c28], 0, 0): compares and stores a handle in statics (0x01301a04/0x02317c2c), calls 0x00e22490; no Lua loading or registration | — |
| 9 | 0x00848d7f | byte 0x02317b06 = 1 ("UI Lua ready" flag, HYPOTHESIS); returns the result of 0x00e23910 | — |

0x00e1a1b0 is a single instruction: load 0x02a45450 and return. **CONFIRMED.** (Agrees with the
2026-10-01 correction already in `spec-lua-api-behaviour.md` §26.23; closes the OPEN notes in
spec-lua-bindings §16.1 step 2 and §13.5.)

**Complete interface-state file order (CONFIRMED):** `system_lib.lua` → `vint_lib.lua` →
`game_ui_globals.lua` → `vdo_base_object.lua` → `vdo_anim_object.lua` → `vdo_input_tracker.lua`,
with natives registered: 24 bare globals before `system_lib.lua`; 55 `vint_*` and the 311-name UI
cluster between `system_lib.lua` and `vint_lib.lua`; the 113-name registrar between `vint_lib.lua`
and `game_ui_globals.lua`. The sandbox metatable on the globals table is installed after
`vint_lib.lua` and before the four array files — so `system_lib.lua` and `vint_lib.lua` run against a
plain globals table, and the four array files run with `__index = _GetDynamicGlobal` /
`__newindex = _CatchNilAssignment` already in force (standard Lua semantics of the bootstrap
strings; the strings are CONFIRMED, the consequence is HIGH CONFIDENCE).

## 4. The "Unable to open custom lua library" path (CONFIRMED)

The string is in this dump after all: `.rdata` 0x01163fbc, one use in the binary, at 0x00848d49. In
the loop of §3 step 7, when 0x00e0df90 returns false for entry i:

- a C-runtime `sprintf` formats the message with that entry's file name into the static buffer at
  0x029f4e00;
- nothing else happens: no print/log call, no flag, no abort, no return. The loop increments i and
  continues with the next entry; the function's return value is unaffected (it returns what
  0x00e23910 returned, kept in a register through the loop).
- The buffer 0x029f4e00 has **exactly one reference in the whole binary** — this write. No code reads
  it by static address. The message therefore has no observable effect beyond leaving text in memory
  (a debugger watch could see it; nothing in the executable consumes it).

What makes 0x00e0df90 return false (from 1.1/1.2): null argument; name too long for the 0x400 buffer;
file cannot be opened; buffer allocation failed; compile error; **runtime error in the top-level
chunk**. All six produce the same silent message. The error object is left on that state's Lua stack.

The other preload loads (`system_lib.lua` in the creator, `vint_lib.lua` in 0x00e1e460, `game_lib.lua`
in 0x00a1fa10) ignore the loader's return value entirely: no message of any kind.

## 5. Gameplay state: exact load sequence (CONFIRMED)

0x00a1fa10 (single caller 0x00a1fc20, not dumped):

| # | Site | What happens | State |
|---|---|---|---|
| 1 | 0x00a1fa21 | 0x00e0e0b0(`"game play"`, 1, 0x026e9470, 0x00754410): raw state, base + coroutine, 24 bare globals, **`system_lib.lua`** | new |
| 2 | 0x00a1fa29 | stored at 0x026e7e6c (the only non-zero write to that global; 0x00a20388 in 0x00a20340 writes 0 — a teardown/reset, body not dumped) | 0x026e7e6c |
| 3 | 0x00a1fa2e–0x00a1fa42 | co-op/host gate: if [0x024d8534] is non-null and its +0x5c ≠ +0x58, **return here** — the state exists with only base/coroutine/24 globals/`system_lib.lua` in it | — |
| 4 | 0x00a1fa4e | 0x00a20840(state): the 1,014-entry gameplay registrar (over the instruction cap, not dumped) | 0x026e7e6c |
| 5 | 0x00a1fa5a | 0x00e0ef80(state): 5 names (`script_assert`, `script_profiler_start_section`, `_end_section`, `_do_printout`, `_reset`; loop count 5) | 0x026e7e6c |
| 6 | 0x00a1fa6f | 0x00e0df90(**`game_lib.lua`**, [0x026e7e6c], 0x026e9470). Return value ignored. | 0x026e7e6c |
| 7 | 0x00a1fa7b | 0x00e0d4e0(state): sandbox bootstrap variant A (same three strings as §3 1g) | 0x026e7e6c |

**Complete gameplay-state file order (CONFIRMED):** `system_lib.lua` → `game_lib.lua`, with the 24
bare globals before `system_lib.lua` and the 1,014 + 5 natives between the two files. Both files run
against a plain globals table; the sandbox metatable is installed after `game_lib.lua`.

The allocator object for gameplay loads is the static object 0x026e9470 (also referenced from
0x01008992 and 0x01019af0, not dumped); the interface state uses the dynamically created `Vint` heap.

### 5.1 The per-stem secondary load (0x00a20670 → 0x00a1fa90) (CONFIRMED chain, OPEN origin)

0x00a20670 (single caller 0x0058b450, not dumped): clears byte 0x026e7e60; runs a 10-entry
initialiser table forwards (0x00a202a0 with 1; with 0 it would run it backwards — an init/teardown
pair); formats `"%s_collectibles"` from its **first** stack argument into 0x029f5200 and resolves it
through 0x005eab60 (a named lookup on the object at 0x02442750); if found and 0x00853b10 says not yet
done, calls 0x00889cf0 / 0x00a2bb50(obj, 0) / 0x00889ce0 (not dumped; no Lua loader involved at depth
1). Then loads a register from the stack slot **two dwords above the first argument** and calls
0x00a1fa90 with the stem in that register.

**Correction to spec §16.2:** the stem used for `<stem>.lua` and the name used for
`<name>_collectibles` come from different stack slots of 0x00a20670 (first argument vs. the slot at
+0xc). Whether the second slot is a real third parameter or a stale slot depends on 0x0058b450, which
is not dumped. OPEN. What the `<name>_collectibles` resource is, is also not settled here.

0x00a1fa90(stem in a register), CONFIRMED:
1. Same co-op/host gate as §5 step 3; if it fails, nothing is loaded.
2. `sprintf(0x029f5200, "%s%s", stem, ".lua")`; 0x00e0df90(that buffer, [0x026e7e6c], 0x026e9470).
   If false → return (no message).
3. 0x00e0d550(state): sandbox bootstrap variant B — a full GC of all states first (0x00e0d150), then
   the same three strings as variant A except `__newindex = _CatchUndefinedGlobalWrite`.
4. Takes the part of the stem after the last backslash (0x00ea5af0 with 0x5c), copies it into a local
   buffer, appends `_init`, and calls 0x00e0ca80(buffer, state, 0, 1); if the result is non-null,
   passes it to 0x00e0cd00 (dispatch). Then rebuilds the buffer with `_main` and calls
   0x00e0ca80(buffer, state, 1, 0) — note the two flag arguments are swapped between the two calls, and
   the `_main` result is not dispatched through 0x00e0cd00. 0x00e0ca80 forwards to 0x00e0c720
   (buffer, 0, flag, flag, state); 0x00e0c720 is not dumped, so what the flags mean and whether a
   missing hook is tolerated is not re-verified here (spec §14.3 covers the dispatcher). The spec's
   "fires `<stem>_init` then `<stem>_main`" is consistent with the dump; the flag difference is new
   and OPEN.

The target state of this load is 0x026e7e6c, CONFIRMED. Whether this trigger is boot or non-boot
(the §16.2 OPEN) is not settled by these dumps: 0x0058b450 was not requested.

## 6. Whole-binary answer: can a preload reach the other state?

Evidence base: (a) the file-name strings each occur once and the str dump lists every reference;
(b) the by-name loader 0x00e0df90 has five callers, all dumped; (c) the file worker 0x00e0d800 has two
callers (0x00e0df90 and the queue drain); (d) the buffer loader 0x00e0de80 has two callers; (e) the
preload array 0x01162da0 has three references; (f) the state globals' complete reference lists.

| File | References to the string | Load site(s) | State at that site |
|---|---|---|---|
| `vint_lib.lua` (0x01255f54) | 1: push at 0x00e1e4a7 | 0x00e1e460 only | the state just created and stored at 0x02a45450 |
| `game_lib.lua` (0x01178384) | 2: push at 0x00a1fa6a; a data cell at 0x01178398 (no instruction) | 0x00a1fa10 only | the state just created and stored at 0x026e7e6c |
| `system_lib.lua` (0x01254708) | 1: push at 0x00e0e126 | 0x00e0e0b0 | every state it creates (both) |
| `game_ui_globals.lua` (0x01161cb8) | array slot 0x01162da0 (+ the two array reads in 0x008489e0) | array loop in 0x008489e0 | 0x00e1a1b0() = 0x02a45450 |
| `vdo_base_object.lua` (0x01161ca4) | array slot 0x01162da4 only | same loop | same |
| `vdo_anim_object.lua` (0x01161c90) | array slot 0x01162da8 only | same loop | same |
| `vdo_input_tracker.lua` (0x01161c78) | array slot 0x01162dac only | same loop | same |

The array 0x01162da0 is read only by 0x008489e0 (0x00848d2c, 0x00848d41). Its third reference,
0x00846d52 in 0x00846c60, is a compare of a register against the array's address (a loop-bound
compare, HIGH CONFIDENCE for the table that ends there), not a load; 0x00846c60 is not a caller of
any loader.

The two state globals are written from disjoint creations: 0x02a45450 only at 0x00e1e479 (from a
creator call with name `"interface"`), 0x026e7e6c only at 0x00a1fa29 (from a creator call with name
`"game play"`) and zeroed at 0x00a20388. The creator makes a fresh raw state each call, so the two
globals never alias.

**Static conclusion — CONFIRMED, whole binary:** no instruction in the executable loads `vint_lib.lua`,
`game_ui_globals.lua` or any `vdo_*.lua` into the gameplay state, and none loads `game_lib.lua` into
the interface state. `system_lib.lua` goes into both. This upgrades spec §16.4's "Team B request 8"
answer from HIGH CONFIDENCE (desk) to CONFIRMED for the static negative.

**What is NOT excluded by static evidence (the honest residue):**

1. **The `include` queue (§1.3).** A shipped script running in the gameplay state could call
   `include("vint_lib")` (the worker appends `.lua`), and the file would load into the gameplay state
   when that script's chunk finishes. Whether any shipped gameplay-side script does so is a script
   question: Team B can grep the gameplay-side scripts (`game_lib.lua`, mission scripts) for `include(`
   with a preload name. Note the str job reported the bare string `vint_lib` as not found, but it also
   reported `game play` as not found while the func dump shows that string at 0x011783b0, so the bare
   negative is not reliable; it does not matter, because the only consumers of bare names are Lua-side.
2. **The stem load (§5.1).** If some caller of 0x00a20670 passed a preload's stem, that file would load
   into the gameplay state. The stem is also used for `<stem>_collectibles` and `<stem>_init/_main`, so
   it is a level/mission stem. HIGH CONFIDENCE it is never a preload name; CONFIRMED impossible only
   once 0x0058b450 is read.
3. **The buffer loader (§1.4).** It takes script bytes from a resource, not a file name. If a preload's
   content were packaged as a mission-script resource it would run in the gameplay state via
   0x006cfac0; that is a data-packaging question, outside the executable. 0x00e213d0's target state is
   OPEN (§7).
4. A cross-state drain of the `include` queue is possible in principle (the queue is global): a chunk
   that calls `include` and then, before returning, causes a load in another state through a native
   (for example a document load) would have its queued file loaded by that other load. No such path was
   traced; HYPOTHESIS only, and irrelevant unless a script does it.

## 7. Comparison with spec-lua-bindings §16.1 / §16.4 (and neighbours)

| Spec statement | Verdict |
|---|---|
| §16.1 steps 1(a)–(g) | **Confirms** every sub-step and the order; adds that the callback (d) is called with the state as its single argument, that the `vint_lib.lua` load result is ignored, and that the creator's second argument is unused. |
| §16.1 step 1(a) "unconditionally loads system_lib.lua for ANY state" | **Confirms**; adds base + `coroutine` as the only stock libraries, opened before the 24 bare globals. |
| §16.1 step 2 (113-name registrar fed by 0x00e1a1b0) and its OPEN about what 0x00e1a1b0 returns | **Confirms** the registrar call (loop 0x71); **closes the OPEN**: 0x00e1a1b0 returns 0x02a45450. |
| §13.5 / §16.1 OPEN "state value read from a fixed global before 0x00e1e460 creates the state" | **Corrects**: the value at 0x02a7557c is the `Vint` named heap (created at 0x00848c43, stored by 0x00e23910), the allocator argument of every interface-state load — not a Lua state. |
| §16.1 step 3 "several unrelated subsystem-init calls" | **Confirms** (listed in §3 steps 2, 3, 5, 6, 8; none loads or registers at depth 1). |
| §16.1 step 4 (4-entry array, order) | **Confirms** count (file-backed 4) and order from the array slots; **adds** the loader call shape (name, state via 0x00e1a1b0, `Vint` heap) and the silent failure path (§4). |
| §16.1 note "sandbox takes effect AFTER vint_lib.lua" | **Confirms**; **adds** that the four array files run after it. |
| §16.2 gameplay order | **Confirms**; **adds** the gate position (state + `system_lib.lua` exist even when the gate fails), the ignored load result, the gameplay allocator object 0x026e9470. |
| §16.2 per-mission secondary load | **Confirms** the chain into 0x026e7e6c and variant B; **corrects** the stem source (different stack slot from the `_collectibles` name); **adds** the swapped flag pairs on the `_init`/`_main` calls; boot/non-boot stays OPEN. |
| §16.3 "0x00e0e140 list … no connection to the real file-loading path" | **Corrects**: it is the deferred `include` queue drained by both loaders (§1.3). |
| §16.4 bullet "preload into UI state …" / "… gameplay state …" | **Confirms** both lists and orders. |
| §16.4 bare-globals bullet (job bgcx) | **Confirms** (24, both states, before `system_lib.lua`); upgrades "(stock libraries, HYPOTHESIS)". |
| §16.4 Team B request 8 answer, HIGH CONFIDENCE (desk) | **Upgrades** the static part to CONFIRMED (whole binary); the OPEN "other callers / other uses of the strings and loaders" is closed; residue listed in §6. |
| §14.1 "fired from 0x006cfac0 the state is 0x026e7e6c; from 0x00e213d0 it is 0x00e1a1b0()'s return" | First half HIGH CONFIDENCE from the xref adjacency; second half not visible in these dumps (OPEN here). |
| `spec-lua-api-behaviour.md` §26.23 (0x00e1a1b0 = interface state, corrected by job nzxf) | **Confirms** again. |

New items a host needs that neither section states: the loader return semantics (§1.1/1.2), the
chunk-name convention (the name as passed, without the appended `.lua`), the full-GC-before-every-load
behaviour (HIGH CONFIDENCE), the `include` semantics (§1.3), the buffer loader's hook pair and error
codes (§1.4), and the fact that no preload failure is ever reported except the silent sprintf (§4).

## 8. Further dumps still needed (addresses)

1. **0x00e213d0** — second caller of the buffer loader: which state it loads into (reads 0x02a45450
   via 0x00e1a1b0?) and what buffers it feeds (closes §6 residue 3 and §14.1's second half).
2. **0x006cfac0** — body, to confirm the mission-resource → 0x00e0de80 → 0x026e7e6c chain directly
   rather than from xref adjacency.
3. **0x00e0e140** and **0x00e0f010** — the `include` queue writer and the Lua `include` global: confirm
   the link and the duplicate check; close §1.3's HIGH CONFIDENCE to CONFIRMED.
4. **0x0058b450** (caller of 0x00a20670) and **0x00a1fc20** (caller of 0x00a1fa10) — origin of the stem
   and the gameplay bring-up trigger (closes §5.1's OPENs and §16.2's boot/non-boot question).
5. **0x00a20340** — writes 0 to 0x026e7e6c: a gameplay-state teardown? Needed to know whether the
   gameplay state is re-created per level (which would re-run `system_lib.lua`/`game_lib.lua` in a
   fresh state).
6. **0x00e0c720** — the named-hook caller behind 0x00e0ca80: meaning of the (0,1)/(1,0) flag pairs for
   `_init`/`_main`.
7. **0x00fcca70** and **0x00fcb970** — confirm "base library" and "register a library table" (currently
   from spec §17 / by shape).
8. **0x00dfeb60** — confirm the full-GC call (constant 2).
9. **0x00846c60** — the compare against 0x01162da0, to close the array's last reference formally.
10. **xref of 0x01178398** — the data cell that points at `game_lib.lua`: is anything reading it?
11. **0x00db52d0** full body / **0x00e23910** — only if the identity of the `Vint` heap object needs
    CONFIRMED rather than HIGH CONFIDENCE.
12. Optional for Team B's blocker: **0x00e1a150** (`vint_is_std_res`) if not already in
    `spec-lua-api-behaviour.md`.
