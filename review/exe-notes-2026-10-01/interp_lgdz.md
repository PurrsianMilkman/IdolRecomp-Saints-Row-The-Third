# Interpretation of bridge dumps `20261001T114555-team-a-lgdz` (follow-up on the 24 bare globals)

Team A, 2026-10-01. Source: CrreishDump output for `team-a/ghidra/jobs/bgcx-followup.json`, three steps:
`roster/` (func mode, depth 1: the 18 roster bodies not read in job `bgcx`), `rng/` (func mode, depth 1:
0x00dab5a0, 0x00dab810, 0x00dab830, 0x00fccb70) and `rngxref/` (reference lists for the ring 0x013214d4, the
cursor 0x013214d0 and the stray writer 0x010145e1). This note continues `interp_bgcx.md` (same scratchpad
folder), which covered the registrar, the state question and the four bodies `rand_int`, `rand_float`,
`round`, `debug_print`/`assert_msg`. Nothing from that note is re-derived here unless said so.

Dump caveats: none this time. The option tokens were honoured (`roster/index.txt` reports `mode func
depth=1 maxfuncs=12 maxinsn=1500`), every requested body is complete, and each callee at depth 1 is
present. The `rngxref/` request for 0x010145e1 returned an empty list, which is itself a finding (§10).

Labels: **CONFIRMED** = read in a listed instruction stream of these dumps; **HIGH CONFIDENCE** = follows
from a dumped instruction or reference list, but the body it points at was not dumped (typically a C
run-time routine identified by its calling shape, or a Lua primitive identified by the same method the
specs already use); **HYPOTHESIS** = plausible reading, not settled; **OPEN** = not settled, listed in §14.

**Lua primitives used below.** Established identities from `spec-lua-api-behaviour.md` §2 and the front
matter, used unchanged: argument count 0x00dfde50, number read 0x00dfe160, number push 0x00dfe3a0,
string read 0x00dfe210, type tag 0x00dfe040, push-string 0x00dfe420, keyed read 0x00dfe5e0, set-top
0x00dfde60, push-nil 0x00dfe380, next-pair 0x00dfec70, push-boolean 0x00dfe590. Of these, the bodies of
0x00dfde50, 0x00dfe160, 0x00dfe3a0, 0x00dfe210 and 0x00dfe590 are in these dumps and match their
identities exactly (0x00dfe210: tag-4 check, in-place conversion through the coercion helper, optional
length out-parameter, null on failure; 0x00dfe590: writes tag 1 and a 0/1 value, bumps the top by 16).
**CONFIRMED** for those five, which upgrades the §2 labels of 0x00dfde50 (HIGH CONFIDENCE) and 0x00dfe210
(HIGH CONFIDENCE) to CONFIRMED. The float-to-integer helpers 0x00ea2596 (truncation toward zero, settled in
`interp_bgcx.md` §4.1; body in these dumps again) and 0x00ea2560 (new, §3) are also read in full. Three
further bodies that the thread section depends on are in the dumps: the cross-state value mover 0x00dfdd80,
the yield primitive 0x00dffb20, and the two helpers 0x00e0cef0 (the `_GetAnyGlobalSilent` check that
`spec-lua-bindings.md` §8.1 already describes) and 0x00e0ceb0 (the current-thread accessor that
`spec-lua-api-behaviour.md` §8.24/§23.15 already cite). Identities assigned by shape only, not dumped:
globals-field read 0x00dfe610, stack-insert 0x00dfdf00, coroutine resume 0x00e00080, call-frame probe
0x00e01710, protected-free call 0x00dfea40; each is labelled where used.

---

## 1. Shape shared by all 18 bodies (CONFIRMED)

Every one of the 18 reads the argument count `n` first, then reads its arguments at **negative indices
counted from the top: `-n` for the first argument, `1-n` for the second, and so on**. Three consequences,
all CONFIRMED from the index arithmetic in every listing:

- **Extra arguments are ignored from the end, not the start.** `max(a, b, c)` uses `a` and `b`; `c`
  plays no part. The same holds for every function below: the first `k` arguments are used, where `k` is
  the function's fixed arity.
- **Missing arguments** make the index land at 0 or above the top. The index resolver 0x00dfdc60 was not
  dumped (again), so this stays a **HYPOTHESIS** from stock Lua 5.1: a positive index past the top reads
  as nil (hence 0 for a number, null for a string); index 0 reads a stale slot. A host may treat a
  missing argument as nil; the engine's behaviour for `max(5)` or `floor()` is unspecified.
- **No type checks and no errors.** A nil or non-numeric argument reads as 0 (`lua_tonumber`), a
  non-string reads as null (`lua_tolstring`); nothing raises.

**Single precision everywhere.** Every numeric wrapper stores each argument into a 32-bit float slot
immediately after reading it, computes on those floats, stores its result into a 32-bit float slot, and
only then widens it to a double for the push. **CONFIRMED** for all 15 numeric bodies (the 32-bit
stores are in each listing). A host that returns doubles will differ from the engine in the low bits:
`max(0.1, 0)` is `0.10000000149011612`, not `0.1`; `abs(16777217)` is `16777216`. The integer-valued
functions (`floor`, `ceil`, `sizeof_table`, `thread_new`, `get_frame_time` excluded) go through a
32-bit integer and are exact.

**Return count.** One value for 15 of them; two for `closest_point_on_line_segment`; none for
`thread_kill`; `thread_yield` yields instead of returning. **CONFIRMED** (the constant in the return
register of each body).

---

## 2. `max` (0x00e0f2c0) and `min` (0x00e0f330) — first, as requested (CONFIRMED)

**Arguments: exactly two numbers**, `a` at `-n` and `b` at `1-n`, each narrowed to single precision on
read. There is no loop over the argument count and no third read: **`max`/`min` are binary**, and
`max(1, 2, 3)` returns **2** (the first two arguments), not 3. **CONFIRMED — disassembly** (listing
0x00e0f2c0..0x00e0f327 and 0x00e0f330..0x00e0f397: two reads, one compare, one push).

**Selection rule.** Each body does one x87 compare of `b` against `a` and picks one of the two float
slots by address. Read from the status-word masks (`max` tests the below/unordered bits with a
parity jump; `min` tests the below/equal bits with a zero jump):

| case | `max(a, b)` returns | `min(a, b)` returns |
|---|---|---|
| `a < b` | `b` | `a` |
| `a > b` | `a` | `b` |
| `a == b` (including `0` vs `-0`) | `b` (the second argument) | `b` (the second argument) |
| either is NaN (unordered) | `b` (the second argument) | `b` (the second argument) |

So the rule is: **`max` returns `a` only when `b < a` holds as an ordered comparison; `min` returns `a`
only when `a < b` holds; in every other case, ties and NaN included, the second argument is returned.**
`max(NaN, 1)` is `1`, `max(1, NaN)` is NaN; `max(0, -0)` is `-0`. **CONFIRMED — disassembly.**

**Non-numbers** read as 0 and take part in the comparison like any other 0: `max(nil, -3)` is `0`,
`max("7", 2)` is `7` (strings convert). **CONFIRMED** (0x00dfe160's body: coercion, else 0).

**Precision.** Both arguments are compared *after* narrowing, so values that differ only beyond
single precision are "equal" and the second one wins: `max(16777217, 16777216)` returns `16777216`.
The result is the chosen float widened. **CONFIRMED** (both stores are 32-bit).

**Return:** one number.

Spec consequence for Team B: a host must not use a variadic `max`/`min`; it must use the two-argument,
second-wins rule above, and it should narrow to float before comparing.

---

## 3. `floor` (0x00e0f3a0) and `ceil` (0x00e0f260) (CONFIRMED)

**Argument: one number**, narrowed to single precision, then widened to double for the C run-time call.

**`floor`** calls the C run-time's floor (0x00ea4e80; its body is in the dump: the SSE2 version
clears the fraction bits below the exponent and subtracts one for a negative value that had a fraction,
with an x87 fallback under the same control-word gate). **`ceil`** calls the C run-time's ceil
(0x00ea4d60, same construction, adding one instead). Both results are mathematically exact floors and
ceilings of the (float) argument. **CONFIRMED — disassembly** (the 0x00ea4e80 and 0x00ea4d60 bodies).

The floored value is then narrowed to float (exact, since a float's floor is representable) and
**converted to a 32-bit integer** by 0x00ea2560, which is read in full here: if the run-time's SSE2
flag at 0x035213c8 is set, it uses the truncating SSE2 conversion; otherwise it falls through into
0x00ea2596, the x87 truncation helper settled in `interp_bgcx.md`. Either way the conversion is exact
for an integral value in range. The integer is pushed as a number. **CONFIRMED — disassembly.**

**Semantics:** `floor(x)` = the largest integer ≤ `float(x)`; `ceil(x)` = the smallest integer ≥
`float(x)`; both return an **integer-valued number**, never a fraction and never negative zero.
Examples: `floor(2.5) = 2`, `floor(-2.5) = -3`, `ceil(2.1) = 3`, `ceil(-2.1) = -2`, `ceil(-0.5) = 0`,
`floor(nil) = 0`, `floor("3.7") = 3`. **CONFIRMED.**

**Edges a host must copy if it wants equality with the engine:**
- *Float narrowing first.* `floor(0.99999999)` is **1** (the double rounds to the float 1.0 before the
  floor); `floor(-1e-9)` is **-1**; `floor(16777217)` is **16777216**. **CONFIRMED** (the 32-bit store
  precedes the call).
- *32-bit range.* Any argument of **2147483584 or more** rounds to the float 2^31 and the conversion
  overflows; so does anything below -2^31, and NaN. On the SSE2 path the result is the "integer
  indefinite" **-2147483648**. Which path runs is decided by 0x035213c8, whose writer is not in the
  dump; on any SSE2-capable PC it is the SSE2 path. **HIGH CONFIDENCE** for the -2147483648 outcome,
  CONFIRMED that the result is unspecified-by-design beyond ±2^31 (the x87 fallback returns the low 32
  bits of a 64-bit conversion instead, which differs). No script should depend on it.

**Return:** one number.

---

## 4. The other math names: `abs`, `sqrt`, `sin`, `cos`, `acos`

All take **one number**, narrowed to single precision, return **one number** (a float widened), read
no second argument, and raise nothing. **CONFIRMED** for the shape of all five.

**`abs` (0x00e0f140).** The float argument's sign bit is cleared (one x87 absolute-value instruction) and
the result pushed. `abs(-0) = 0`, `abs(NaN)` = NaN with the sign cleared, `abs(nil) = 0`. **CONFIRMED —
disassembly.** No C run-time involvement.

**`sqrt` (0x00e0f5c0).** The float is passed to 0x00ea3f60, which stores it as a double and calls the
C run-time's square-root entry pair (0x00eb1118 then 0x00ea3f7d, not dumped); the result is narrowed to
float. That 0x00ea3f60 is the run-time `sqrt` is **HIGH CONFIDENCE** (name binding plus the run-time's
calling shape; the inner bodies were not dumped). A negative argument therefore yields the run-time's
NaN, with no guard in the wrapper (**CONFIRMED** that there is no guard; **HIGH CONFIDENCE** on the NaN).
`sqrt(nil) = 0`.

**`sin` (0x00e0f210) and `cos` (0x00e0f1c0).** The float is passed, unchanged and unscaled, to
0x00ea2470 (`sin`) or 0x00ea3a70 (`cos`). Both callees are in the dump and are the C run-time's
dispatchers: if the run-time's SSE2 flag at 0x035213c4 is set and the SSE and x87 control words hold
their default exception masks, they jump to an SSE2 implementation (0x00eb0cf0, 0x00eb3a70); otherwise
they take the x87 implementation (0x00ea24c8, 0x00ea3ac8). None of those four leaves was dumped. The
result is narrowed to float.
- **Units: radians.** The wrapper applies no conversion of any kind between the read and the call
  (**CONFIRMED — disassembly**: the float slot is loaded straight onto the x87 stack and the dispatcher
  called), and the callees are the C run-time's `sin`/`cos`, which take radians (**HIGH CONFIDENCE**:
  identified by the dispatcher shape and the name binding, leaves not dumped). A script that passes
  degrees gets garbage, exactly as it would in stock Lua.
- `sin(nil) = 0`, `cos(nil) = 1`. Results are single-precision.

**`acos` (0x00e0f180).** The only one of the five with engine logic of its own. The float is passed to
0x00e0f0f0 (its single caller is `acos`; body in the dump), which **clamps the argument into [-1, 1]
before calling the C run-time's arc-cosine (0x00ea2b90, in the same SSE2-dispatcher family, not
dumped)**: an argument ≤ -1 is replaced by -1, an argument ≥ 1 by 1, and only a value strictly inside the
interval is passed through. The result is narrowed to float. **CONFIRMED — disassembly** for the clamp
(two x87 compares against -1.0 and 1.0 with the argument slot overwritten on the out-of-range paths);
**HIGH CONFIDENCE** that 0x00ea2b90 is the run-time `acos`.
- **Units: radians**, result in [0, π] (single precision, so `acos(-1)` = 3.1415927).
- `acos(2) = 0`, `acos(-5) = π`, `acos(0) = π/2`, `acos(nil) = π/2`.
- **`acos(NaN) = π`**: the first compare is unordered, which the clamp treats like "below -1" and
  substitutes -1. **CONFIRMED** from the status-word test (below-or-equal-or-unordered mask). Harmless
  but worth copying for equality.

There is **no `asin`, `atan`, `atan2`, `pow`, `exp`, `log`, `fmod`, `deg`, `rad`, `pi`, `huge` or
`random`** anywhere in the roster, and (§11) no `math` table to supply them.

---

## 5. `get_frame_time` (0x00e0f400) (CONFIRMED)

**Arguments:** none are read (the count is fetched and discarded). **Return:** one number: the
single-precision float stored at the engine global **0x0132a0b0**, widened to double. **CONFIRMED —
disassembly** (the body is a load of that float and a push).

**What the global is.** Its file image is 0x3d088889 = **1/30 = 0.033333335 (as a float)**, and the
reference list (43 uses in 30 functions, capped) shows it read by engine systems all over the binary
(0x00575e30, 0x005b6af0, 0x00610020, 0x00611b00, 0x006c9df0, 0x006cd130, 0x0072c980, 0x007dafc0,
0x007feb90, ...). It is the engine's **frame delta time**. **Units: seconds** — the initial value is one
thirtieth of a second, i.e. one frame at 30 Hz. **HIGH CONFIDENCE** on the unit and the role: no writer of
the global appears in the capped list, so *who* updates it per frame, and whether it is the measured
delta or a clamped/fixed step, is **OPEN**. CONFIRMED only that the function returns that global and that
its value before the first update is 1/30.

For a host: return the current frame's delta in seconds as a single-precision value; before the first
frame, return 1/30. The exact double a script sees for the default is 0.03333333507180214.

---

## 6. `sizeof_table` (0x00e0f580) (CONFIRMED)

**Argument:** one value, expected to be a table. The wrapper passes the state, the argument count and a
literal 0 to the engine's table-size helper 0x0083dff0 (body in the dump; 13 callers in the binary, so
other bindings share it) and pushes the helper's integer result as a number. **Return:** one number.

The helper, read in full:
1. If the argument count is 0, or the first argument (at `-n`) has the nil type tag, the result is
   **-1**. (The tag test is "not 0": nil only; a `none` past-the-top index passes.)
2. It pushes the one-character string **"n"** (0x0129d9cc) and does a keyed read of the table with it
   (0x00dfe5e0, the metamethod-honouring `gettable`). If the value found is a **number**, that number,
   **truncated toward zero** by 0x00ea2560, is the result; the value is popped and the traversal below
   is skipped. So a table carrying the Lua 5.0-style explicit `n` field reports `n`, whatever its
   contents (`sizeof_table({n = 2.9})` is `2`; `sizeof_table({1, 2, 3, n = 0})` is `0`).
3. Otherwise it pops that value, pushes nil, and walks the table with the next-pair primitive
   (0x00dfec70), popping each value and counting, until the walk ends. The count of **every key/value
   pair in the table** is the result: array part and hash part alike, holes do not stop it.
   `sizeof_table({1, 2, nil, 4})` is `3`; `sizeof_table({a = 1, b = 2})` is `2`; `sizeof_table({})` is `0`.

**CONFIRMED — disassembly** for steps 1-3 (the helper's body; the push-nil/next identities are the
HIGH CONFIDENCE ones from `spec-lua-api-behaviour.md` §2, but the loop shape here is unambiguous).

**Non-table arguments.** A number, string, boolean or function reaches the keyed read, which in this
VM raises the usual "attempt to index" error for a value without an index metamethod (and strings have
none here, since the string library is not opened, §11). **HIGH CONFIDENCE** (VM behaviour; the raise
is inside 0x00dfe5e0's callee, not dumped). A host should raise or return -1; no script should rely on it.

This is **not** the length operator `#`: it is a full pair count with an `n` override. A host must not
implement it as `#t`.

---

## 7. `strstr` (0x00e0f080) (CONFIRMED)

**Arguments:** two strings, `s` at `-n` and `sub` at `1-n`, each read with the string primitive
0x00dfe210 (a number is converted to its string form in place; any other type reads as a null pointer).
**Return: one boolean.**

The two pointers go, `s` first, to the C run-time's substring search 0x00ea48b0 (body in the dump:
the byte-wise MSVC search; an empty `sub` returns `s` itself, a miss returns null, a hit returns the
address of the first occurrence). The wrapper then pushes **`true` if the result is non-null and
`false` otherwise** (0x00dfe590, push-boolean). **CONFIRMED — disassembly.**

So the "1-based or 0-based, nil when not found" question is moot: **`strstr(s, sub)` is a plain,
case-sensitive, byte-wise "does `sub` occur in `s`" test returning a boolean.** No Lua patterns, no
position, no nil. `strstr("abc", "")` is `true`; `strstr("", "a")` is `false`; `strstr(123, "2")` is
`true` (number converted).

**Non-string arguments.** A nil, boolean, table or function reads as a null pointer, and the run-time
search dereferences both pointers unconditionally (`sub`'s first byte first), so **the engine crashes
on `strstr(nil, x)` or `strstr(x, nil)`**. **CONFIRMED** that neither the wrapper nor the search tests
for null. A host should raise an error (or return `false`); no working script can be relying on it.

---

## 8. The four `thread_*` functions and the engine's script-thread table

### 8.1 The data model (CONFIRMED where a field is named below)

The engine keeps a fixed table of **script-thread records at 0x02a42d10, 32 bytes each, count at
0x02a44d58** (the allocator and its sibling compare the count against 0x100, so the capacity is **256**
— HIGH CONFIDENCE, the allocators 0x00e0c720/0x00e0c8f0 were not dumped). Fields read in these dumps:

| offset | size | meaning | evidence |
|---|---|---|---|
| +0x00 | u16 | **thread id**, the handle scripts hold | compared against the handle in the finder 0x00e0caa0; returned by `thread_new` |
| +0x04 | ptr | the **coroutine** (a Lua thread state) | resumed, probed and moved into by the runner |
| +0x08 | u32 | a key inherited from the parent record; used to look up an error callback (0x00e0d070) | `thread_new`, runner |
| +0x0c | u8 | **not-yet-started** flag | runner clears it after pushing the function |
| +0x10 | ptr | **name of the Lua global function** the thread runs | runner reads the global by this name |
| +0x14 | u32 | a context value inherited from the parent record (the `+0x14` that §8.24/§23.15 of the behaviour spec call the calling script's context) | `thread_new` copies it |
| +0x18 | u16 | **flags**: bit 0 = started; bit 2 (0x4) = **killed**; bit 4 (0x10) = a further "do not run" state, never set in these dumps | `thread_kill`, finder, runner |
| +0x1c | u32 | **argument count** handed to the coroutine | `thread_new`, runner |

A **"current thread" stack** of up to 16 record pointers lives at 0x02a44d14 with its depth at
0x02a44d10. The runner pushes a record before resuming it and pops afterwards; **0x00e0ceb0 returns
the record on top of that stack, or null when the depth is 0 or above 16**. **CONFIRMED — disassembly**
(both bodies). This settles the cross-reference flagged in `spec-lua-bindings.md` §10.1 vs
`spec-lua-api-behaviour.md` §8.24/§23.15: 0x00e0ceb0 is "the script-thread record currently being
resumed", and its `+0x14` is a per-thread context value that child threads inherit from their parent.

### 8.2 The runner 0x00e0cba0 (CONFIRMED; needed to state `thread_new` and `thread_kill` precisely)

Takes the *address of* a record pointer and an optional out-flag. In order:
1. If the record's bit 4 is set: return "not alive" without touching it.
2. If the **killed bit** is set, or an optional engine hook at 0x02a44d60 says so: skip to step 9
   (release).
3. If bits 0-1 are already set (the thread is being resumed right now), or a second hook at
   0x02a44d64 says so: return "alive" without resuming (re-entrancy guard).
4. If **not yet started**: push the global named by `+0x10` onto the coroutine's stack (0x00dfe610 with
   the globals pseudo-index: **HIGH CONFIDENCE** that this is the field-read primitive), and, if the
   argument count is positive, move it below the arguments (0x00dfdf00: **HIGH CONFIDENCE** stack
   insert); clear the flag.
5. Set bit 0; push the record on the current-thread stack (if depth < 16).
6. **Resume the coroutine with `+0x1c` arguments** (0x00e00080: **HIGH CONFIDENCE** that this is the
   coroutine resume; the result is tested as "greater than 1 = error", which fits the stock status
   values 0 = finished, 1 = yielded, 2 and up = error).
7. Pop the current-thread stack; call 0x00e0d6a0 (not dumped); write the error flag to the out-param.
8. Ask 0x00e0c610 (not dumped; takes the coroutine) whether it is suspended. **Yielded and no error →
   return "alive"** (with an optional stack-clearing call under a debug byte 0x02a44d5e). **Finished
   without error → step 9.** **Error → look up an error callback by the `+0x08` key (0x00e0d070) and
   call it with the record, then step 9.**
9. **Release**: 0x00e0c650 (not dumped) with the address of the record pointer; return "not alive".
   **HIGH CONFIDENCE** that the release nulls the caller's pointer: it is passed by address, and
   `thread_new` tests that pointer for null right after (§8.3).

Besides `thread_new`, the runner is called from 0x00e0cd00, 0x00e0cf50 and 0x006280d0 (reference list).
Which of those is the per-frame scheduler, and therefore **how often a yielded thread is resumed, is
OPEN** (none dumped). HYPOTHESIS, not evidence: once per frame tick.

### 8.3 `thread_new(name, ...)` (0x00e0f680) — CONFIRMED unless labelled

**Arguments:** a string `name` (a number is accepted and converted), followed by any number of values
to pass to the function. **Return: one number**, the new thread's id, or **65535** on every failure.

1. Reads `name` with the string primitive. **The parent record (0x00e0ceb0) is fetched next and its
   `+0x08` and `+0x14` fields are read later without a null check**: `thread_new` called when no script
   thread is current (depth 0) dereferences a null pointer. **CONFIRMED — disassembly.** In the engine
   this evidently never happens, which implies all script execution that can reach `thread_new` runs
   inside a thread record (the top-level entry that pushes the first record is not in these dumps:
   **OPEN**). A host should make the same guarantee or raise an error.
2. `name` null (not a string or number) → 65535.
3. `_GetAnyGlobalSilent(name)` must return a function: 0x00e0cef0 pushes the global
   `_GetAnyGlobalSilent`, calls it with `name` (one argument, one result) and tests the result's type
   tag for function (6). Otherwise → 65535. **CONFIRMED — disassembly** (body in the dump, as §8.1 of
   the bindings spec already had it). `_GetAnyGlobalSilent` is a script-defined global; it must exist
   before any script calls `thread_new` (HYPOTHESIS: defined by `system_lib.lua`, which loads right
   after the 24 globals; its body is game content and is not described here).
4. Allocate a record via 0x00e0ca80 (a thin wrapper that calls 0x00e0c720 with `name`, three zero
   flags and the parent's `+0x08`). **HIGH CONFIDENCE** (allocator not dumped, but its writes are in the
   reference lists: id word at +0, state at +4, flags at +0x18, "not started" at +0xc) that this creates
   the coroutine, records the name and assigns the id. Null (table full) → 65535.
5. Copy the parent's `+0x14` into the new record.
6. **Move the remaining `n-1` arguments from the caller's stack onto the coroutine's stack**, in order
   (0x00dfdd80, body in the dump: pops `k` values from one state and pushes the same `k` on another;
   **CONFIRMED**), and store `n-1` as the record's argument count.
7. **Run the thread immediately** through the runner (§8.2) — the global function named `name` is
   looked up in the coroutine's globals at that moment and called with the moved arguments, and runs
   **synchronously until its first `thread_yield` or its end**.
8. If the record pointer is still non-null (the thread yielded and is alive): return its 16-bit id.
   Otherwise (it ran to completion, or raised an error, during that first run): **return 65535.**
   **CONFIRMED** for the test and the two returns; HIGH CONFIDENCE that completion is what nulls the
   pointer (§8.2 step 9).

So, for a host: `thread_new` = "create a coroutine for global function `name`, start it now with the
given arguments, and hand back a handle only if it is still suspended afterwards." The handle space is
16-bit with 65535 reserved as "no thread"; the id assignment policy is **OPEN** (allocator not dumped).

### 8.4 `thread_yield(...)` (0x00e0f0d0) — CONFIRMED

Reads and discards the argument count, then calls the **stock yield primitive with zero results**
(0x00dffb20, body in the dump: the C-call-depth check that raises "attempt to yield across
metamethod/C-call boundary", then base = top, status = yielded, return -1). **Any arguments are
ignored; nothing is passed to the resumer.** The runner sees the yield and keeps the thread alive.

Calling it outside a coroutine (from the main state) raises that same "across metamethod/C-call
boundary" error in this Lua version (**HIGH CONFIDENCE**: stock 5.1 behaviour, the depth counters never
satisfy the test on the main thread). What `thread_yield()` evaluates to *when resumed* is **OPEN**: the
runner resumes with the record's original argument count every time (§8.2 step 6), which in stock Lua
would hand back that many values from the top of the coroutine's stack; scripts should treat the call
as returning nothing.

### 8.5 `thread_check_done(id)` (0x00e0f610) — CONFIRMED

**Argument:** one number; truncated toward zero (0x00ea2596) and **reduced to its low 16 bits**. **Return:
one boolean.** The finder 0x00e0caa0 (body in the dump) scans all records and returns the first one
that is (a) **not killed**, (b) still **alive** — not yet started, or with an active call frame
(0x00e01710 with level 0: **HIGH CONFIDENCE** call-frame probe), or with a non-empty stack — and (c)
has that id. `thread_check_done` returns **`true` when no such record exists**: the thread finished, was
killed, never existed, or the number is not a valid handle. `thread_check_done(nil)` tests id 0.

### 8.6 `thread_kill(id)` (0x00e0f650) — CONFIRMED

**Argument:** one number, truncated (the finder compares only the low 16 bits). **Return: nothing.**
Finds the record as in §8.5 and **sets the killed bit (0x4)**; nothing else happens at that moment. The
coroutine is not resumed again: at its next scheduled resume the runner sees the bit, skips the resume
and releases the record (§8.2 step 2 → 9). Because the finder skips killed records, immediately after
`thread_kill(id)`, `thread_check_done(id)` is `true` and a second `thread_kill(id)` is a no-op. A thread
that kills **itself** keeps running until its next `thread_yield` (nothing in the kill path touches the
running coroutine: **CONFIRMED**), and is released at the following resume.

---

## 9. The two geometry helpers (CONFIRMED)

Both take **six numbers**, each narrowed to single precision on read, with the order below fixed by the
slot arithmetic in the listings (read at `-n`, `-(n-1)`, ..., `5-n`).

### 9.1 `closest_point_on_line_segment(px, py, ax, ay, bx, by)` → `x, y`

Arguments: a point **P = (arg1, arg2)** and a segment from **A = (arg3, arg4)** to **B = (arg5,
arg6)**. The engine helper 0x00db96b0 (body in the dump; three other callers) computes the point of the
**closed** segment nearest to P:
- If A and B are exactly equal in both components, the result is A.
- Otherwise `t` = ((P − A) · (B − A)) / |B − A|², **clamped to [0, 1]**, and the result is A + t·(B − A).
  A `t` that compares as unordered (NaN) clamps to 0, so a degenerate but unequal segment yields A.

Rounding, for a host that wants bit-equality: the components of B − A are computed in double and
rounded to single; the numerator and the denominator are each rounded to single before the division;
the quotient is rounded to single; each output component is rounded to single. **CONFIRMED —
disassembly** (every narrowing is a packed-double-to-single conversion in the listing).

**Return: two numbers**, the x then the y of the nearest point (the wrapper's return count is 2).

### 9.2 `which_side_of_2d_line(a1, a2, a3, a4, a5, a6)` → signed number

The helper 0x00db9230 (body in the dump; `which_side_of_2d_line` is its only caller) returns, in x87
extended precision rounded once to single:

**(a2 − a4) · (a5 − a3) − (a1 − a3) · (a6 − a4)**

**CONFIRMED — disassembly** (the operand order is read off the x87 stack operations). Reading the
arguments the same way as the sibling in §9.1, with **P = (a1, a2)** and a directed line from
**A = (a3, a4)** to **B = (a5, a6)**, this is the 2-D cross product (B − A) × (P − A): **positive when
P lies to the left of the direction A→B** (with x to the right and y up), **negative on the right, zero
on the line**. That role assignment is **HIGH CONFIDENCE** (by analogy with §9.1; the alternative
reading "line (a1,a2)→(a3,a4), point (a5,a6)" gives the same magnitude with the opposite sign
convention). A host that implements the formula literally in terms of a1..a6 is correct either way.
It returns the **magnitude too**, not just a sign (twice the signed area of the triangle A, B, P).

**Return: one number.**

---

## 10. The random ring: object, fill, seed, cursor (CONFIRMED unless labelled)

**The object.** The three "cursor by address" routines all load **0x013214d0 into the this-register**
and call a method on it. So 0x013214d0 is not a bare cursor but the base of a **random-source object**:
`+0x0000` the cursor (0x013214d0), `+0x0004` the ring of 8192 32-bit values (0x013214d4..0x013294d3),
and **`+0x8004` the state of a generator (0x013294d4)**. **CONFIRMED — disassembly** (the method
bodies index the cursor at +0, the ring at +4 and pass +0x8004 to the generator calls).

**Fill and seed: 0x00dab5a0(seed) → method 0x00dab4d0.** Read in full:
1. If `seed` is non-zero, call **0x00dab370(state, seed)**: the generator's seeding routine (not dumped;
   **HIGH CONFIDENCE** on the role: it is the only call that receives the seed, and it targets the
   state block).
2. **Set the cursor to 0.**
3. Call the **generator step 0x00dab3c0(state)** exactly **8192 times**, storing each result into the
   ring in order.

So **the ring is a cache of 8192 outputs of a real generator**, refilled wholesale, never
incrementally. A zero `seed` refills from the generator's current state (continuing its sequence);
a non-zero `seed` reseeds first. **The generator algorithm is OPEN** (0x00dab3c0 and 0x00dab370 not
dumped; the state block's size is unknown). 0x00dab4d0 has two more callers, 0x00599c20 and 0x00dab720,
which may act on other instances of the same object type (their this-register is not visible here):
**OPEN**.

**When it is filled.** 0x00dab5a0 has **three callers: 0x00dd98f0, 0x009c1ff0, 0x005d25f0**, none dumped.
What seed each passes (zero, a clock, a save value) is **OPEN**. Before the first of them runs, the
ring is whatever the file image holds: the cursor and the first ring entry are both **0 in the file**
(**CONFIRMED** from the reference-list headers), and **HIGH CONFIDENCE** the whole ring is zero, so until
the first fill every `rand_int(lo, hi)` returns `lo` and every `rand_float(lo, hi)` returns `lo`.

**Cursor behaviour, complete list of writers (CONFIRMED, the reference list has all 18 uses in 8
functions):** the five draw routines (0x00dab5b0, 0x00dab5e0, 0x00dab630, 0x00dab660, 0x00dab6a0) and the
two helpers below each advance it by one and **reset it to 0 when it reaches 8192**; the fill routine
**resets it to 0** at every fill. **Nothing else writes it**: there is no save/restore of the cursor, and
no reseed-by-cursor. The cursor is therefore reset in exactly two situations: wrap-around and refill.

**The stray writer 0x010145e1.** The reference list for that address as a target is empty (0 uses), and
the ring's own list shows only the two instructions there. It is an undisassembled stretch of code
that stores through a register loaded with the ring's base; now that the real fill is found
(0x00dab4d0 writes the ring through a register, which is why the ring's reference list never showed it),
0x010145e1 is a side issue: **OPEN** whether it is dead code, an inlined copy or a second filler. It
need not be chased for the host.

**The two remaining "cursor by address" routines are vector helpers, not RNG management:**
- **0x00dab810(out) → method 0x00dab750:** draws the next ring value `r`, converts it as an unsigned
  32-bit integer (adds 2^32 when the signed load is negative), multiplies by **2^-32** and narrows to a
  float `u` in [0, 1), then **broadcasts `u` into all four lanes of a 16-byte vector** at `out` and
  returns `out`. One ring entry consumed.
- **0x00dab830(out, a, b) → method 0x00dab7b0:** draws `u` the same way (through 0x00dab750) and stores
  **a + (b − a)·u, lane-wise, with the same `u` for all four lanes** — a random point on the segment
  between two vectors. One ring entry consumed. Returns `out`.
  **CONFIRMED — disassembly** for both.
- Side result: the decompiler's reading of the full 8 bytes at **0x0111e4c0 is 2.3283064e-10 = 2^-32**,
  and of 0x012a3078 is 4.2949673e+09 = 2^32. That upgrades `interp_bgcx.md`'s HIGH CONFIDENCE on the
  `rand_float` scale constant to **CONFIRMED** (the decompile in `rng/func_0x00dab810.txt` prints it).

**Bottom line for a host (unchanged in substance from `interp_bgcx.md` §7, sharpened):** sequence
equality with the game needs the generator (OPEN). Until then: a ring of 8192 values from any
generator, a cursor that wraps at 8192 and resets on refill, and the two Lua draw formulas already
specified. A host that does not refill at all matches the engine's *shape* (the engine itself only
refills when one of three undumped callers runs).

---

## 11. 0x00fccb70: which stock libraries are opened (CONFIRMED)

0x00fccb70 (single caller: the state creator 0x00e0e0b0, as `interp_bgcx.md` §2 placed it) does exactly
two things and **returns 2**:
1. calls **0x00fcca70**, the **base-library opener**, read in full here: it binds `_G` to the globals
   table itself, registers the 23-entry function table at 0x01293410 into the globals under the
   library name `_G`, sets `_VERSION` to `"Lua 5.1"`, installs `ipairs` and `pairs` as closures over
   their iterator functions, builds the weak-keys-and-values metatable (`__mode` = `"kv"`) and installs
   `newproxy` as a closure over it. This is the stock Lua 5.1 base opener, line for line.
2. registers the **6-entry `coroutine` table** at 0x012934e8 under the global name `coroutine` through
   the stock library-registration helper (0x00fcb970 → 0x00fcb7a0 with a zero upvalue count, the
   `luaL_register` identity `spec-lua-bindings.md` §13.1 already established).

**CONFIRMED — disassembly.** Returning 2 after exactly those two steps is the stock `luaopen_base`
(which opens base and coroutine together). **It does not open `math`, `string`, `table`, `os`, `io`,
`debug` or `package`.** Combined with the creator's step list (`spec-lua-bindings.md` §16.1: raw state,
slot record, 0x00fccb70, the 24 bare globals, `system_lib.lua`, return), **no `math` table exists in
either Lua state when `system_lib.lua` runs: CONFIRMED.** That none is added by any later registrar is
**HIGH CONFIDENCE** (those registrars were catalogued as engine-name registrars in the bindings spec and
none is a library opener; not re-checked here). So **`math.floor` is an index into nil; the bare
`floor` is the only floor.** Likewise there is no `string.format`, `string.sub`, `table.insert`,
`os.time`: whatever the scripts use for those must come from the engine's own registrars or from
`system_lib.lua`, which is outside this note.

The bindings spec's §1 OPEN item (base table has 23 entries where stock has 24) is unchanged by this
dump: the table's contents were not listed, only its address and that it is registered whole.

---

## 12. Label summary (one line per claim)

| Claim | Label | Evidence |
|---|---|---|
| All 18 read args at `-n`, `1-n`, ...; extra trailing args ignored; no checks, no errors | CONFIRMED | every roster listing |
| Every numeric arg narrowed to single precision; results single precision widened | CONFIRMED | 32-bit stores in every numeric listing |
| Missing-argument behaviour (index 0 / past top) | HYPOTHESIS | 0x00dfdc60 not dumped |
| `max`/`min`: binary; `max(1,2,3)` = 2 | CONFIRMED | 0x00e0f2c0, 0x00e0f330 |
| `max`/`min`: ties and NaN return the second argument | CONFIRMED | status-word masks in both listings |
| `floor`/`ceil`: C run-time floor/ceil on the float, then 32-bit truncating conversion, integer result | CONFIRMED | 0x00ea4e80, 0x00ea4d60, 0x00ea2560 bodies |
| `floor`/`ceil` out of ±2^31 or NaN → -2147483648 | HIGH CONFIDENCE | SSE2 path read; flag writer at 0x035213c8 not dumped |
| `abs`: sign bit cleared on the float | CONFIRMED | 0x00e0f140 |
| `sqrt`, `sin`, `cos`: C run-time routines, no unit conversion, radians | CONFIRMED (no conversion) / HIGH CONFIDENCE (callee identity) | 0x00ea3f60, 0x00ea2470, 0x00ea3a70 dispatchers; leaves not dumped |
| `acos`: argument clamped to [-1, 1] first; `acos(NaN)` = π | CONFIRMED | 0x00e0f0f0 |
| `get_frame_time`: returns the float at 0x0132a0b0, file value 1/30 | CONFIRMED | 0x00e0f400 + data header |
| `get_frame_time` unit = seconds, role = frame delta | HIGH CONFIDENCE | value and 43 engine readers; writer not in capped list |
| `sizeof_table`: -1 for none/nil; `n` field if numeric (truncated); else count of all pairs | CONFIRMED | 0x0083dff0 |
| `sizeof_table` on a non-table raises | HIGH CONFIDENCE | VM behaviour, raise site not dumped |
| `strstr`: boolean "contains", byte-wise, numbers converted | CONFIRMED | 0x00e0f080, 0x00ea48b0, 0x00dfe590 |
| `strstr` with a non-string crashes the engine | CONFIRMED (no null check anywhere on the path) | same |
| Thread table 0x02a42d10, 32-byte records, count 0x02a44d58; field roles in §8.1 | CONFIRMED (fields used) / HIGH CONFIDENCE (capacity 256) | 0x00e0caa0, 0x00e0cba0, 0x00e0f680 |
| 0x00e0ceb0 = top of a 16-deep current-thread stack at 0x02a44d14 | CONFIRMED | body |
| `thread_new`: name check via `_GetAnyGlobalSilent`, args moved, run synchronously to first yield, id or 65535 | CONFIRMED | 0x00e0f680, 0x00e0cef0, 0x00dfdd80, 0x00e0cba0 |
| `thread_new` returns 65535 when the thread completes in its first run | HIGH CONFIDENCE | release routine 0x00e0c650 not dumped; pointer passed by address and tested after |
| `thread_new` with no current thread record dereferences null | CONFIRMED | no null test on the parent record |
| Resume primitive, getfield, insert, getstack identities | HIGH CONFIDENCE | shape only |
| Scheduler cadence (how often a yielded thread resumes) | OPEN | 0x00e0cd00/0x00e0cf50/0x006280d0 not dumped |
| `thread_yield`: stock yield with 0 results; args ignored | CONFIRMED | 0x00dffb20 |
| Value of `thread_yield()` on resumption | OPEN | runner resumes with the original argc |
| `thread_check_done`: true unless a live, unkilled record with that 16-bit id exists | CONFIRMED | 0x00e0f610, 0x00e0caa0 |
| `thread_kill`: sets bit 0x4; reaped at next resume; no return | CONFIRMED | 0x00e0f650, runner |
| `closest_point_on_line_segment`: (P, A, B) → clamped projection, two results | CONFIRMED | 0x00db96b0 |
| `which_side_of_2d_line`: the exact a1..a6 formula | CONFIRMED | 0x00db9230 |
| ... its (P, A, B) reading / sign convention | HIGH CONFIDENCE | analogy with the sibling |
| Ring = 8192 cached generator outputs; fill resets cursor, optional reseed | CONFIRMED | 0x00dab4d0 |
| Generator algorithm; seeder; what the three fill callers pass | OPEN | 0x00dab3c0, 0x00dab370, 0x00dd98f0, 0x009c1ff0, 0x005d25f0 not dumped |
| Ring and cursor are zero in the file image | CONFIRMED (first entry, cursor) / HIGH CONFIDENCE (whole ring) | data headers |
| Cursor writers: five draws, two vector helpers, the fill; wrap at 8192 | CONFIRMED (complete list) | xref 0x013214d0 |
| 0x00dab810 / 0x00dab830 are vector random helpers | CONFIRMED | bodies |
| 2^-32 scale constant | CONFIRMED (decompiler reads the full 8 bytes) | rng/func_0x00dab810.txt |
| 0x00fccb70 opens base + `coroutine` only, returns 2 | CONFIRMED | body + 0x00fcca70 |
| No `math` table at state creation | CONFIRMED | above + creator step list |
| No `math` table ever | HIGH CONFIDENCE | later registrars not re-checked |
| `include` re-read | OPEN | 0x00e0f010 not in this job |

---

## 13. Spec changes: what replaces the OPEN items in `spec-lua-api-behaviour.md` §26.27

Exact edits, in document order. Text in quotation marks is the current wording to strike or replace.

**(a) Host paragraph.** Replace "Whether the stock `math` table also exists depends on 0x00fccb70,
called just before the registrar; that it opens the stock libraries is a **HYPOTHESIS**." with:
"0x00fccb70, called just before the registrar, is the stock base-library opener: it installs the base
functions, `_G`, `_VERSION`, `pairs`/`ipairs`, `newproxy` and the `coroutine` table, and nothing else.
**There is no `math`, `string`, `table`, `os` or `io` table in either state when `system_lib.lua` runs
(CONFIRMED — disassembly), and no later registrar adds one (HIGH CONFIDENCE).** `math.floor` is an index
into nil; the 24 bare names are the whole arithmetic surface."

**(b) Roster table.** Replace '"Standard math" describes the name only; those bodies were not dumped.'
with: 'The nine "standard math" rows are engine wrappers that share only the name with Lua's `math`
library: each narrows to single precision, `max`/`min` are strictly binary, `floor`/`ceil` return
32-bit integers, `acos` clamps. Their behaviour is below.' Add `include` stays as is (§16.3 of the
bindings spec; its re-read is still OPEN).

**(c) "Shared argument handling" paragraph.** Change "(the four dumped bodies)" to "(all 22 engine
bodies; `include` and the stub excepted)" and append: "Every numeric wrapper narrows each argument to
single precision on read and returns a single-precision value widened to double. Arguments beyond a
function's arity are ignored from the end (`max(a, b, c)` uses `a` and `b`)."

**(d) New entries, inserted after the `round` entry, in roster order** (wording as in §§2-9 above,
condensed; each carries its label from §12):

- **`abs(x)`** (0x00e0f140): `|float(x)|`; `abs(nil) = 0`; one number. CONFIRMED.
- **`acos(x)`** (0x00e0f180): `float(x)` clamped to [-1, 1], then the C run-time arc-cosine, radians,
  narrowed to float; `acos(2) = 0`, `acos(-5) = π`, `acos(NaN) = π`, `acos(nil) = π/2`; one number.
  CONFIRMED (clamp) / HIGH CONFIDENCE (run-time callee).
- **`cos(x)`, `sin(x)`** (0x00e0f1c0, 0x00e0f210): C run-time cosine/sine of `float(x)` in **radians**, no
  conversion, narrowed to float; `cos(nil) = 1`, `sin(nil) = 0`; one number each. CONFIRMED (no
  conversion) / HIGH CONFIDENCE (callee).
- **`ceil(x)`, `floor(x)`** (0x00e0f260, 0x00e0f3a0): ceiling/floor of `float(x)` as a **32-bit integer**
  (truncating conversion of an integral value, exact); `floor(0.99999999) = 1` and `floor(16777217) =
  16777216` because of the narrowing; `ceil(-0.5) = 0`; `floor(nil) = 0`; outside ±2^31 or NaN:
  -2147483648 (HIGH CONFIDENCE, SSE2 path); one number. CONFIRMED.
- **`get_frame_time()`** (0x00e0f400): the engine's frame delta as a float, **seconds**, default 1/30
  before the first frame; arguments ignored; one number. CONFIRMED (the global) / HIGH CONFIDENCE
  (unit and role; the writer is OPEN).
- **`max(a, b)`, `min(a, b)`** (0x00e0f2c0, 0x00e0f330): binary; compared as floats; `max` returns `a`
  only when `b < a`, `min` returns `a` only when `a < b`; otherwise (ties, NaN) the **second argument**;
  `max(1, 2, 3) = 2`; one number. CONFIRMED.
- **`sizeof_table(t)`** (0x00e0f580): -1 if no argument or nil; if `t.n` is a number, `trunc(t.n)`;
  otherwise the count of **all** key/value pairs (array and hash, holes included), not `#t`; raises on
  a non-table (HIGH CONFIDENCE); one number. CONFIRMED.
- **`sqrt(x)`** (0x00e0f5c0): C run-time square root of `float(x)`, narrowed; negative → NaN (HIGH
  CONFIDENCE); `sqrt(nil) = 0`; one number. CONFIRMED (shape) / HIGH CONFIDENCE (callee).
- **`strstr(s, sub)`** (0x00e0f080): **boolean**, true iff `sub` occurs in `s` byte-wise (no patterns,
  no position, never nil); numbers are converted; empty `sub` → true; a non-string argument crashes
  the engine, so a host raises; one boolean. CONFIRMED.
- **`thread_new(name, ...)`** (0x00e0f680): requires a current script thread (else the engine reads
  through null); `name` string or number, else 65535; `_GetAnyGlobalSilent(name)` must return a
  function, else 65535; allocates one of 256 thread records (full → 65535), moves the remaining
  arguments to a new coroutine, **runs it at once until its first `thread_yield`**; returns the 16-bit
  thread id if still suspended, **65535 if it completed or errored during that run** (HIGH CONFIDENCE
  for the completion case); one number. CONFIRMED.
- **`thread_yield(...)`** (0x00e0f0d0): yields the current coroutine with no values; arguments ignored;
  from outside a coroutine raises the C-call-boundary error (HIGH CONFIDENCE). How often the engine
  resumes a yielded thread is OPEN. CONFIRMED.
- **`thread_check_done(id)`** (0x00e0f610): `id` truncated to its low 16 bits; **true** unless a live,
  unkilled thread record with that id exists (not started, or has an active frame, or a non-empty
  stack); one boolean. CONFIRMED.
- **`thread_kill(id)`** (0x00e0f650): marks the record killed; it is never resumed again and is
  released at its next scheduled resume; `thread_check_done` is true immediately after; killing a
  finished, unknown or already-killed id does nothing; a self-kill takes effect at the next yield;
  **no return value**. CONFIRMED.
- **`closest_point_on_line_segment(px, py, ax, ay, bx, by)`** (0x00e0f740): nearest point of the closed
  segment A–B to P (A if A = B; projection parameter clamped to [0, 1], NaN → 0); computed in single
  precision at each step; **returns two numbers** `x, y`. CONFIRMED.
- **`which_side_of_2d_line(a1, a2, a3, a4, a5, a6)`** (0x00e0f830): returns `(a2 − a4)(a5 − a3) −
  (a1 − a3)(a6 − a4)` as a float; with P = (a1, a2) and the line A = (a3, a4) → B = (a5, a6) that is
  (B − A) × (P − A): > 0 left of A→B, < 0 right, 0 on the line (role assignment HIGH CONFIDENCE, the
  formula CONFIRMED); one number.

**(e) "The shared random ring" block.** Replace the "**OPEN — seeding and filling**" bullet with:
"**Filling (CONFIRMED — disassembly).** 0x013214d0 is a random-source object: cursor at +0, the
8192-entry ring at +4, a generator state at +0x8004 (0x013294d4). Its fill method 0x00dab4d0, reached
through 0x00dab5a0(seed), reseeds the generator with `seed` when `seed` is non-zero (0x00dab370, not
dumped), resets the cursor to 0, and regenerates all 8192 entries from the generator step 0x00dab3c0.
The ring and cursor are zero in the file image, so every draw returns 0 (`rand_int` returns `lo`) until
the first fill. 0x00dab5a0 has three callers (0x00dd98f0, 0x009c1ff0, 0x005d25f0, not dumped). The
cursor's complete writer set is the five draw routines, two vector helpers (0x00dab810: one uniform
broadcast to a 4-vector; 0x00dab830: lerp between two 4-vectors by one uniform) and the fill; it resets
only on wrap-around and refill. **OPEN:** the generator algorithm (0x00dab3c0/0x00dab370), the seeds
the three callers pass, and the undisassembled writer at 0x010145e1." Also change the `rand_float`
bullet's label for the 2^-32 constant from HIGH CONFIDENCE to CONFIRMED (the decompiler prints the full
value in `rng/func_0x00dab810.txt`).

**(f) "OPEN — the remaining bodies" block.** Delete it; everything in it is settled by (d) except the
`include` re-read, which becomes a single line: "**OPEN:** re-read of `include` (0x00e0f010; desk-level
description in `spec-lua-bindings.md` §16.3) — not in job `lgdz` either."

**(g) Review-status paragraph.** Replace with: "**Review status (2026-10-01): re-derived from the
executable (jobs `20261001T020218-team-a-bgcx` and `20261001T114555-team-a-lgdz`): CONFIRMED — the
roster, binding and both-states registration; all 22 engine bodies' argument handling, narrowing and
return counts; `max`/`min` second-wins rule; `floor`/`ceil` integer results; `acos` clamp; `abs`;
`get_frame_time`'s global and default; `sizeof_table`; `strstr` boolean; the four `thread_*`
semantics against the 256-record thread table and its runner; both geometry formulas; the ring's fill
and cursor rules; 0x00fccb70 = base + `coroutine`, no `math`. HIGH CONFIDENCE — the C run-time identities
of `sqrt`/`sin`/`cos`/`acos`; frame-time unit; -2147483648 overflow result; `thread_new` → 65535 on
first-run completion; thread capacity 256; the geometry role assignment of `which_side_of_2d_line`.
HYPOTHESIS — missing-argument behaviour. OPEN — the generator and its seeds; thread scheduling cadence
and id policy; the writer of 0x0132a0b0; `thread_yield`'s value on resumption; the `include` re-read."

**Related edits outside §26.27.**
- `spec-lua-api-behaviour.md` §2 primitive notes: 0x00dfde50 (argument count) and 0x00dfe210
  (`lua_tolstring`) move from HIGH CONFIDENCE to CONFIRMED (bodies in these dumps); add 0x00dfe590
  (`lua_pushboolean`, CONFIRMED), 0x00dfdd80 (cross-state value move, CONFIRMED), 0x00dffb20 (yield,
  CONFIRMED).
- `spec-lua-api-behaviour.md` §8.24 / §23.15 and `spec-lua-bindings.md` §10.1: 0x00e0ceb0 is the current
  script-thread record (top of the 16-deep run stack at 0x02a44d14); its `+0x14` is a per-thread
  context inherited by child threads. The three readings describe the same object.
- `spec-lua-bindings.md` §13.2: "this is Lua's own `math` library" → strike for good (bare engine
  wrappers; no `math` table exists). §16.1 step 3: drop "That 0x00fcca70 is the base library comes
  from §17, not from this job" — 0x00fcca70 is now read directly and is the stock base opener, and
  0x00fccb70 returns 2 after base + `coroutine`.
- `spec-lua-bindings.md` §16.4 / `team-b` note: Team B's host must expect scripts that call
  `max`/`min` with three arguments to get the first two compared, and must provide
  `_GetAnyGlobalSilent` (from the real `system_lib.lua`) before any `thread_new`.

---

## 14. What to dump next (only if a question above matters to Team B)

1. **RNG sequence equality** (only if ever needed): 0x00dab3c0 (generator step), 0x00dab370 (seeder),
   and the three fill callers 0x00dd98f0, 0x009c1ff0, 0x005d25f0 to learn the seed. Low priority: no
   script outcome can be checked against the exact sequence.
2. **Thread scheduling:** 0x00e0cd00, 0x00e0cf50 and 0x006280d0 (the runner's other callers) to fix the
   resume cadence; 0x00e0c720 (allocator: id policy, coroutine creation) and 0x00e0c650 (release); the
   top-level entry that pushes the first record (needed to state when `thread_new` is legal).
3. **Frame time:** a write-reference search for 0x0132a0b0 (the capped list shows only reads) to see
   whether it is the measured delta, clamped, or a fixed step.
4. **Missing arguments:** 0x00dfdc60 (index resolver), one small body, closes the HYPOTHESIS for every
   function in this family.
5. **`include`:** 0x00e0f010, still never dumped.
