# Interpretation of bridge dumps `20261001T020218-team-a-bgcx` (the 24 bare globals of 0x00e0f900)

Team A, 2026-10-01. Source: CrreishDump output for
`team-a/ghidra/jobs/teamb-request-9b-bare-globals.json` (Team B request 9). Three steps:
`registrar/` (func mode, 0x00e0f900 and its two callees), `callers/` (xref mode, 0x00e0f900),
`named/` (lua mode: `rand_int`, `rand_float`, `round`, `debug_print`, each with callees at depth 1).

Dump caveats, so nobody re-derives them: the option tokens (`depth=1`, `maxfuncs=40`, `maxinsn=1500`,
`depth=2`, `maxfuncs=12`, `maxinsn=900`) were again taken as targets, not options. In `registrar/` they
produced four "no function here" entries and NullPointerException lines, and the run used the defaults
(`maxinsn=600`); nothing was lost, the registrar body is 398 lines and complete. In `named/` the words
`depth` and `2` were looked up as strings, producing three irrelevant dumps (`depth_0.txt` = 0x00d9e740,
`2_0.txt` = the CRT `strncat`, `2_1.txt` = 0x00d9e140); they are noise and are not used below. Every
`named/` tree ran at callee depth 1, which was enough.

One dump from a sibling job is also used, because it closes the "which state" question outright:
`20261001T021641-team-a-yduu/func/func_0x00a1fa10.txt` (Team B request 8, preloads) contains the body
of 0x00e0e0b0, the single caller of 0x00e0f900. Where a claim rests on that file it is said so.

Labels: **CONFIRMED** = read in a listed instruction stream of these dumps (or the named sibling dump);
**HIGH CONFIDENCE** = follows from a dumped instruction or reference list but the body it points at was
not dumped; **HYPOTHESIS** = plausible reading, not settled; **OPEN** = not settled, see §8.

Shared Lua primitives cited by address, already established by `spec-lua-api-behaviour.md`'s front
matter and `spec-lua-bindings.md` §13.1, used unchanged: `lua_pushcclosure` 0x00dfe4f0,
`lua_setfield` 0x00dfe830, `lua_gettop` 0x00dfde50, `lua_tonumber` 0x00dfe160, `lua_pushnumber`
0x00dfe3a0. The bodies of 0x00dfde50, 0x00dfe160 and 0x00dfe3a0 are in these dumps and match those
identities exactly (top minus base, shifted by 4 = slot count; type tag 3 = number read with a
fallback coercion call; store a double with tag 3 and bump the top by 16). **CONFIRMED.** The
float-to-integer helper 0x00ea2596 is also in these dumps and is re-read in §4.1 below, which settles
an OPEN item in the other spec.

---

## 1. The registrar 0x00e0f900 and its roster (CONFIRMED)

**Shape.** 0x00e0f900 takes one argument, the Lua state. It fills a 24-entry local array of
`{name pointer, function pointer}` pairs (8-byte stride, at frame offsets 0x0c..0xc8) with immediate
stores, then loops exactly 24 times (counter loaded with 0x18). Each iteration does, in this order:

1. 0x00dfe4f0(state, function pointer, 0) — push the C function as a closure with no upvalues;
2. 0x00dfe830(state, 0xffffd8ee, name pointer) — set the field named `name` of the pseudo-index
   -10002, which is Lua 5.1's `LUA_GLOBALSINDEX`.

That is the literal expansion of `lua_register(L, name, f)`, the same idiom §13.1 already identified.
No table is created or pushed first, and the index is the globals pseudo-index, not -2 or -3: **the 24
names are bound as bare globals, not into a `math` table.** **CONFIRMED — disassembly** (listing
0x00e0fac9..0x00e0faf5, both callee bodies also in the dump).

**The roster, in registration order.** Five name strings were printed by the dumper only as raw
dwords because they are 3 characters long; they decode as ASCII (`0x00736261` = "abs",
`0x00736f63` = "cos", `0x006e6973` = "sin", `0x0078616d` = "max", `0x006e696d` = "min").
**CONFIRMED** (the dword values are in the listing's data annotations).

| # | Lua global | Native function | What it is |
|---|---|---|---|
| 1 | `abs` | 0x00e0f140 | standard math name (body not dumped) |
| 2 | `acos` | 0x00e0f180 | standard math name (body not dumped) |
| 3 | `cos` | 0x00e0f1c0 | standard math name (body not dumped) |
| 4 | `sin` | 0x00e0f210 | standard math name (body not dumped) |
| 5 | `ceil` | 0x00e0f260 | standard math name (body not dumped) |
| 6 | `debug_print` | 0x007c9f50 | **engine; the shared no-op stub** (§4.4) |
| 7 | `assert_msg` | 0x007c9f50 | **engine; the same no-op stub** |
| 8 | `floor` | 0x00e0f3a0 | standard math name (body not dumped) |
| 9 | `get_frame_time` | 0x00e0f400 | engine (body not dumped) |
| 10 | `include` | 0x00e0f010 | engine; the "mark library opened" primitive of §16.3 |
| 11 | `max` | 0x00e0f2c0 | standard math name (body not dumped) |
| 12 | `min` | 0x00e0f330 | standard math name (body not dumped) |
| 13 | `rand_float` | 0x00e0f430 | engine (§4.2) |
| 14 | `rand_int` | 0x00e0f4c0 | engine (§4.1) |
| 15 | `round` | 0x00e0f530 | engine (§4.3) |
| 16 | `sizeof_table` | 0x00e0f580 | engine (body not dumped) |
| 17 | `sqrt` | 0x00e0f5c0 | standard math name (body not dumped) |
| 18 | `strstr` | 0x00e0f080 | C-library name, engine wrapper (body not dumped) |
| 19 | `thread_check_done` | 0x00e0f610 | engine, coroutine helper by name (body not dumped) |
| 20 | `thread_kill` | 0x00e0f650 | engine (body not dumped) |
| 21 | `thread_new` | 0x00e0f680 | engine (body not dumped) |
| 22 | `thread_yield` | 0x00e0f0d0 | engine (body not dumped) |
| 23 | `closest_point_on_line_segment` | 0x00e0f740 | engine geometry helper (body not dumped) |
| 24 | `which_side_of_2d_line` | 0x00e0f830 | engine geometry helper (body not dumped) |

Name strings and function pointers: **CONFIRMED — disassembly** (each pair is an immediate store in
the listing; the `named/index.txt` string search independently found each of the four requested names
exactly once in the binary, at the store inside 0x00e0f900). The "standard math name" column is the
name only: the nine math-named bodies (`abs`, `acos`, `cos`, `sin`, `ceil`, `floor`, `max`, `min`,
`sqrt`) were not dumped, so whether they behave like stock Lua `math.*` (argument count, NaN handling,
`max`/`min` with more than two arguments) is **OPEN** (§8). Note the name strings for `acos`, `cos`,
`sin`, `ceil`, `floor` are shared with the CRT's libm error-reporting helper at 0x00eb6690 (the xref
lists show it), which is the string coincidence §13.2 already noted; the binding is nonetheless real,
because the registrar stores the pointer itself.

**Three things the roster settles immediately.**

- **All six script-called bare names Team B listed are here: `max` (#11), `floor` (#8),
  `rand_int` (#14), `rand_float` (#13), `round` (#15), `debug_print` (#6).** **CONFIRMED.**
- `debug_print` and `assert_msg` share one function with many other names across the binary: the
  reference list of 0x007c9f50 is capped at 30 entries and already shows registration stores in
  0x00e0ef80 (five, the `script_profiler_*` names of §13.6), 0x00a20840 (six), 0x00845aa0 (five),
  0x00e1dfb0, 0x007af8d0, 0x007d0ec0, 0x007c04b0, 0x007e18e0 and 0x007c9fc0; the spec's
  `set_mission_author` is one of the 0x00a20840 ones (§13.6). It is a no-op (§4.4).
- `include` is the function §16.3 describes; this dump confirms its only reference is the data store at
  0x00e0f9a8 inside the registrar. **CONFIRMED.**

---

## 2. Which Lua state receives the block (CONFIRMED: both)

**The caller.** 0x00e0f900 has exactly one reference in the whole binary: a plain call at 0x00e0e11f
inside 0x00e0e0b0. **CONFIRMED — disassembly** (`callers/xref_0x00e0f900.txt`: "total uses 1 in 1
functions").

0x00e0e0b0 is the generic Lua-state creator the spec already traced (§13.6, §16.1 step 1(a), §16.2):
called with `"interface"` from 0x00e1e460 and with `"game play"` from 0x00a1fa10. The sibling dump
`yduu` has its body, and its reference list names exactly those two callers (0x00e1e473 and
0x00a1fa21). Its straight-line success path, read from that listing:

1. refuse if the state-slot counter at 0x02a44f50 is already 4, or if the name argument is null
   (returns 0);
2. create the raw state via 0x00fcb490 (returns the state; on null, return);
3. if this is the first state, also store it in 0x02a44f4c; copy up to 0x20 bytes of the name into a
   48-byte slot record at 0x02a44d88 + 48*index, store the state at +0x20 and the caller's fourth
   argument at +0x24, zero +0x28/+0x2c, bump the counter;
4. call 0x00fccb70(state) — **not dumped; by position this is where the stock standard libraries would
   be opened (HYPOTHESIS, §8)**;
5. **call 0x00e0f900(state) — the 24 bare globals, unconditionally**;
6. load `system_lib.lua` via 0x00e0df90(name, state, flag);
7. return the state.

So **both the interface state and the gameplay state receive all 24 bare globals, and they receive
them before `system_lib.lua` runs and before every other registrar** (0x00e1dfb0's 55 `vint_*`
names, 0x008430f0's cluster and 0x00845aa0's 113 names for the interface state; 0x00a20840's 1,014
and 0x00e0ef80's 5 names for the gameplay state all run after 0x00e0e0b0 returns, per §16.1/§16.2).
**CONFIRMED — disassembly** (this job's xref for the single call site; the sibling `yduu` dump for the
body of 0x00e0e0b0 and its two callers). Nothing further needs dumping for this question. The call is
not gated on the flag argument (0 for interface, 1 for game play); that argument is only stored into
the slot record and passed to the `system_lib.lua` load.

---

## 3. Bare globals, not a table (CONFIRMED)

Already covered in §1: every one of the 24 names goes through `lua_setfield(state, -10002, name)`,
i.e. a direct write into the globals table. There is no `math` table involved, no `luaL_register`
with a library name, no `lua_newtable` in the body. A script that writes `math.floor` gets whatever
the stock library (if opened at step 4 of §2) provides; a script that writes `floor` gets 0x00e0f3a0.
The two are independent bindings. **CONFIRMED — disassembly.**

Consequence for a host: `max`, `floor`, `rand_int`, `rand_float`, `round`, `debug_print` and the other
18 names must exist as globals in **both** states before any preload file is run.

---

## 4. Behaviour of the dumped functions

All four read their arguments the same way: `n` = 0x00dfde50 (argument count), then
`lua_tonumber(state, -n)` for argument 1 and, where a second is used, `lua_tonumber(state, 1-n)` for
argument 2. Negative indices counted from the top; with the usual 2 arguments these are -2 and -1.
`lua_tonumber` returns 0 for a value that is neither a number nor a string convertible to one (its
body is in the dumps: tag check for 3, else the coercion call, else 0). None of the four checks the
count, the types, or raises an error. **CONFIRMED** for every function below.

Edge: with fewer arguments than the function reads, the missing index is 0 or positive-past-top. In
stock Lua 5.1 index 0 resolves to the slot just above the top (stale stack contents), and a positive
index past the top resolves to the nil object; 0x00dfe160's index resolver (0x00dfdc60) was not
dumped, so this is **HYPOTHESIS** from the stock source. A host may treat a missing argument as 0
(what a nil slot yields) and note that the real engine's result for `rand_int(5)` is unspecified.

### 4.1 `rand_int(a, b)` — 0x00e0f4c0 (CONFIRMED)

- **Arguments:** two numbers, read as above. Each is converted to a 32-bit integer by the helper
  0x00ea2596, read in full in this dump: it stores the x87 value to a 64-bit integer with the current
  rounding mode, re-loads it, subtracts to get the fractional remainder, then corrects by one toward
  zero when the rounding went away from zero (sign of the original decides the direction; the
  remainder's float sign bit decides whether a correction is needed). Net effect: **truncation toward
  zero, independent of the FPU rounding mode** — the MSVC run-time's classic `(int)` cast helper.
  `rand_int(1.9, 3.9)` draws from [1, 3]; `rand_int(-1.5, 1.5)` from [-1, 1]. **CONFIRMED —
  disassembly** (0x00ea2596 listing, lines 0x00ea2596..0x00ea260a). This settles the OPEN rounding
  question `spec-lua-api-behaviour.md` §4.1 carries for this helper: it is truncation, not
  round-to-nearest and not banker's rounding (see §6).
- **Ordering:** if the second integer is smaller than the first they are swapped, so the call takes
  `lo = min(a, b)`, `hi = max(a, b)`. `rand_int(10, 1)` is the same as `rand_int(1, 10)`. **CONFIRMED.**
- **Range: inclusive on both ends.** The draw helper 0x00dab660(lo, hi) computes
  `lo + (sample mod (hi - lo + 1))` with an unsigned 32-bit division. **CONFIRMED — disassembly**
  (the `+1` after the subtraction and the unsigned divide are explicit). If `hi - lo + 1` is 0 (only
  possible by 32-bit overflow, e.g. `rand_int(-2^31, 2^31-1)`) the engine divides by zero; not a case
  a host needs to reproduce.
- **The RNG (CONFIRMED, shape only):** there is no generator in the draw path. `sample` is read from a
  **ring of 8192 pre-filled 32-bit values at 0x013214d4** (so the ring spans 0x013214d4..0x013294d3),
  indexed by a cursor at 0x013214d0; the cursor is advanced by one after each read and reset to 0
  when it reaches 0x2000. The sequence therefore repeats every 8192 draws, counting draws from every
  consumer of the ring: 0x00dab660 has at least 30 callers (listing capped) across the engine, and the
  sibling draw routines 0x00dab5b0, 0x00dab5e0, 0x00dab630 and 0x00dab6a0 (the one `rand_float`
  uses) advance the same cursor. **Lua's randomness is not isolated from the engine's.**
  **CONFIRMED — disassembly** for the ring, cursor, wrap and the shared cursor; the caller count is from
  the capped reference list.
- **Seeding/filling (OPEN):** the only writer of the ring's contents in the reference list is code at
  0x010145e1..0x010145f0 that Ghidra has not made a function of (it loads the ring's base into a
  register and stores through it). That is the fill routine; it was not dumped, so what the 8192
  values are (a fixed table, an LCG unrolled at startup, a time seed) is **OPEN**. Three further
  routines take the cursor's address rather than its value (0x00dab5a0, 0x00dab810, 0x00dab830),
  which is the shape of save/restore or reseed-by-cursor helpers; also not dumped. See §8.
- **Return:** exactly one value, the integer pushed as a Lua number (0x00dfe3a0). **CONFIRMED.**

### 4.2 `rand_float(a, b)` — 0x00e0f430 (CONFIRMED)

- **Arguments:** two numbers, each stored as a **single-precision float** immediately after
  `lua_tonumber` (both results go through a 32-bit store). **CONFIRMED.**
- **Ordering:** compared as floats; swapped only when the second is strictly less than the first, so
  again `lo = min, hi = max`. (An unordered compare, i.e. a NaN, takes the no-swap path.) **CONFIRMED.**
- **Draw:** 0x00dab6a0(lo, hi) reads the next ring value (same ring, same cursor as §4.1, same wrap at
  0x2000), converts it to floating point **as an unsigned 32-bit value** (adds 2^32, constant 0x4f800000
  at 0x012a3078, when the signed load is negative), multiplies by a double constant at 0x0111e4c0,
  and stores the product as a single-precision float `u`. The result is `lo + (hi - lo) * u`, computed
  in x87 and stored as a single-precision float, then widened to a double for `lua_pushnumber`.
  **CONFIRMED — disassembly** for every step. The constant at 0x0111e4c0 is 2^-32 (the dump prints
  only its low dword, which is 0; the decompiler's reading of the full 8 bytes gives 2.3283064e-10 =
  2^-32, and only 2^-32 makes sense next to the 2^32 unsigned fix-up): **HIGH CONFIDENCE** on the exact
  value, CONFIRMED on the shape.
- **Range:** nominally `[lo, hi)`, since `u` = sample / 2^32 is in [0, 1). But `u` is rounded to a
  single-precision float, so samples within about 2^-25 of 2^32 round `u` to exactly 1.0 and the call
  returns exactly `hi`. A host should treat the range as **`[lo, hi]` with `hi` reachable but rare**, and
  should produce single-precision results (callers that compare against the bounds would otherwise
  see values the engine cannot produce). **CONFIRMED** (the store of `u` and of the result through
  32-bit slots is in the listing).
- **Return:** one Lua number. **CONFIRMED.**

### 4.3 `round(x)` — 0x00e0f530 (CONFIRMED)

- **Argument:** one number via `lua_tonumber(-n)`, stored as a **single-precision float** before use
  (so a double such as 16777217 is first rounded to float). **CONFIRMED.**
- **Rule:** 0x00dad900, read in full: compare `x` with 0.0f; if `x >= 0`, widen to double, add 0.5 and
  convert with truncation; otherwise widen, subtract 0.5 and convert with truncation (`CVTTSD2SI`).
  That is **round half away from zero**: `round(2.5) = 3`, `round(3.5) = 4`, `round(-2.5) = -3`,
  `round(-0.5) = -1`, `round(0.4) = 0`, `round(-0.4) = 0` (the integer 0, so no negative zero).
  Not banker's rounding and not floor(x + 0.5). **CONFIRMED — disassembly.**
- **Edge cases:** the comparison is an ordered compare that takes the "negative" branch on NaN; NaN and
  any magnitude outside the 32-bit range come out of the truncating conversion as 0x80000000, i.e.
  **-2147483648**. `round(nil)` is `round(0)` = 0. **CONFIRMED** for the instruction behaviour; whether
  any script relies on it is not known.
- **Return:** one Lua number holding the 32-bit integer. **CONFIRMED.**

### 4.4 `debug_print(...)` and `assert_msg(...)` — 0x007c9f50 (CONFIRMED)

The whole body is: read the argument count (0x00dfde50) and discard it, return 0. **It is a no-op
that pushes nothing and reads none of its arguments; the retail binary does not log.** Variadic in
the sense that any number of arguments of any type is accepted. **CONFIRMED — disassembly** (the
function is 16 bytes; the only call is to the argument-count accessor). The same body serves
`assert_msg` (pair #7) and the many other stubbed names whose registrations reference it (per the
spec, `set_mission_author` and the four `script_profiler_*` names, §13.6; the reference list in this
dump is capped at 30 entries spread over at least nine registering functions, so the real set is
larger). A host should bind `debug_print` and `assert_msg` to a function that accepts
anything and returns nothing; it may log the arguments for its own diagnostics, since the engine's
observable behaviour (no return values, no side effects on the Lua stack) is identical either way.

### 4.5 `max` (0x00e0f2c0) and `floor` (0x00e0f3a0) — among the 24, bodies not dumped

Both are in the roster (pairs #11 and #8). Their bodies were not part of this job (the `named/` step
listed only the four requested names), so argument count, type handling and `max`'s behaviour with
more than two arguments or with NaN are **OPEN** (§8). The name strings for `max`/`min` at
0x0116ed44/0x0116ed48 are also referenced from 0x00904d10 and 0x00b7e7b0 and from four data tables
(0x01192bd8.., 0x011a2090.., 0x01243bb8..), which is consistent with the same short strings being
reused elsewhere in the engine (e.g. as table keys); those references are not registrations.
**CONFIRMED** that they are in the roster; everything else about them is OPEN.

---

## 5. Label summary (one line per claim)

| Claim | Label | Evidence |
|---|---|---|
| 0x00e0f900 registers exactly 24 pairs, in the order of §1's table | CONFIRMED | `registrar/func_0x00e0f900.txt` listing, counter 0x18 |
| Names bound as bare globals (`lua_setfield` with -10002), no table | CONFIRMED | same listing, 0x00e0fae1 |
| 0x00e0f900 has one caller, 0x00e0e11f in 0x00e0e0b0 | CONFIRMED | `callers/xref_0x00e0f900.txt` |
| 0x00e0e0b0 calls it unconditionally for every state it creates, after 0x00fccb70 and before `system_lib.lua` | CONFIRMED | sibling dump `yduu`, body of 0x00e0e0b0 |
| 0x00e0e0b0's two callers are the interface (0x00e1e460) and game-play (0x00a1fa10) bring-ups, so both states get the 24 | CONFIRMED | sibling dump's reference list; spec §16.1/§16.2 for the state names |
| 0x00fccb70 opens the stock standard libraries | HYPOTHESIS | position only; body not dumped |
| `rand_int`: args truncated toward zero, swapped to lo/hi, inclusive `[lo, hi]`, one number returned | CONFIRMED | `named/rand_int_0.txt` |
| 0x00ea2596 is truncation toward zero (settles the other spec's OPEN) | CONFIRMED | same file, full body |
| The RNG is an 8192-entry ring at 0x013214d4 with cursor 0x013214d0, shared engine-wide | CONFIRMED | same file (0x00dab660) and `rand_float_0.txt` (0x00dab6a0) |
| How the ring is filled/seeded; what 0x00dab5a0/0x00dab810/0x00dab830 do | OPEN | writer at 0x010145e1 not dumped |
| `rand_float`: single-precision, `lo + (hi-lo)*u`, `u` = ring value / 2^32 as float, `hi` reachable by rounding | CONFIRMED (shape, stores) / HIGH CONFIDENCE (constant is exactly 2^-32) | `named/rand_float_0.txt` |
| `round`: half away from zero, via float, NaN/out-of-range gives -2147483648 | CONFIRMED | `named/round_0.txt` (0x00dad900) |
| `debug_print` and `assert_msg`: no-op, return 0 values, no logging | CONFIRMED | `named/debug_print_0.txt` |
| `max`, `floor` are pairs #11 and #8 | CONFIRMED | registrar listing |
| Behaviour of `max`, `floor` and the other 15 undumped bodies | OPEN | not in this job |
| Behaviour with missing arguments (index 0 reads the slot above the top) | HYPOTHESIS | stock Lua 5.1 source; 0x00dfdc60 not dumped |

---

## 6. Comparison with the current spec text

**`spec-lua-bindings.md` §13.2** (the 24-pair block inside 0x00e0f900).
- *Confirms:* the block exists, is 24 pairs, uses `lua_pushcclosure` + `lua_setfield(-10002)`, and is
  "registered by hand rather than via a static table".
- *Corrects:* "this is Lua's own `math` library" is wrong in substance. Only 9 of the 24 names are
  math-library names, and they are bound as bare globals; the other 15 are engine functions
  (`debug_print`, `assert_msg`, `get_frame_time`, `include`, `rand_float`, `rand_int`, `round`,
  `sizeof_table`, `strstr`, four `thread_*` names, two geometry helpers). It is therefore API surface
  that a host must provide, not "standard library, not counted further". The qualification already
  attached to §13.2 (one of the pairs is 0x00e0f010; bare globals) is confirmed and should become the
  main text. The OPEN item "dump the 24 pairs of 0x00e0f900" is closed by §1 above.
- *Adds:* the full roster with addresses (§1), the fact that all six of Team B's missing bare names are
  in it, and the state answer (§2).
- *On the 62-site budget:* this dump contributes two of the 62 `lua_setfield` call sites (0x00e0fae7 in
  the registrar; the other sites listed in the callee's capped reference list include 0x00e0eff8 in
  0x00e0ef80 and 0x00e1e445 in 0x00e1dfb0, as §13.6/§13.7 say). The budget question itself is not
  settled here; it needs the full 62-entry list the OPEN item asks for.

**§16.3** (0x00e0f010 / `include`).
- *Confirms:* its only reference is the data store inside the registrar (0x00e0f9a8), and the Lua name
  it is bound to is `include` (pair #10) — §16.3 did not name it. The "mark library opened" behaviour
  itself is not re-examined here (body not in this job).

**§16.4** (bottom line for a peer host).
- *Corrects/closes* the added desk-review bullet: "which state receives them is not stated" — both
  states, from inside the state creator, before `system_lib.lua`; "which script-called bare names are
  among them is OPEN" — all six named ones are. The preload order for a host becomes, for each state:
  (stock libraries, HYPOTHESIS) → the 24 bare globals → `system_lib.lua` → that state's own registrars
  and files as §16.1/§16.2 list them.
- *Adds:* that `system_lib.lua` can legitimately use any of the 24 at top level, since they exist
  before it loads.

**§16.1 step 1(a) / §16.2** ("0x00e0e0b0 unconditionally loads `system_lib.lua`"): confirmed again
from the sibling dump, with the addition that the 24-global registration sits between state creation
and that load. Also newly visible there: a hard cap of 4 Lua states (counter at 0x02a44f50) and a
48-byte-per-state slot table at 0x02a44d88 holding a 32-byte name copy, the state pointer and the
creator's fourth argument; the first state created is also stored at 0x02a44f4c. These are side
findings, CONFIRMED from that listing, not needed by Team B now.

**`spec-lua-api-behaviour.md` §4.1 and front matter** (0x00ea2596's rounding semantics, OPEN and
flagged NEEDS-EXE): **settled — truncation toward zero** (§4.1 above). Every "integer-coerced
argument" in that document that goes through `lua_tonumber` + 0x00ea2596 truncates; `2.9` becomes 2,
`-2.9` becomes -2. The descriptions calling it "round-half-correcting" or "banker's rounding" should be
struck. Separately, 0x00dad900 (cited in that spec §16.2 and §22 as "round to integer") is a different
helper and really does round, half away from zero (§4.3 above); the two must not be conflated.

---

## 7. What a host should implement from this (CONFIRMED items only)

- In both Lua states, before any preload file: 24 globals per §1. Six have behaviour fixed here:
  - `rand_int(a, b)`: `lo, hi = trunc(a), trunc(b)` (toward zero, 32-bit); swap if `hi < lo`; return
    `lo + (r mod (hi - lo + 1))` with `r` an unsigned 32-bit draw; inclusive both ends; one number.
  - `rand_float(a, b)`: `lo, hi = float(a), float(b)`; swap if `hi < lo`; `u = float(r / 2^32)`;
    return `float(lo + (hi - lo) * u)`; one number; `hi` itself occurs when `u` rounds to 1.
  - `round(x)`: `x = float(x)`; `x >= 0 ? trunc(x + 0.5) : trunc(x - 0.5)` in double; 32-bit result.
  - `debug_print(...)`, `assert_msg(...)`: accept anything, return nothing.
  - Missing or non-numeric arguments read as 0 (what `lua_tonumber` yields for nil or a
    non-convertible value); the engine raises no error.
- The draw source `r`: an 8192-entry ring of 32-bit values with a wrapping cursor, shared by both
  Lua functions and the rest of the engine. Until the fill routine is read (§8), a host should use its
  own generator and not promise sequence equality with the game; nothing in the mission scripts can be
  checked against the exact sequence anyway.
- The 18 remaining names need their bodies (§8) before implementation beyond stubs. For the 9
  math-named ones the stock `math.*` semantics is the obvious HYPOTHESIS, but it is not confirmed:
  stock `math.max` is variadic and errors on a non-number, while the pattern seen in the four dumped
  functions (no checks, `lua_tonumber` defaults to 0) suggests these engine versions do not.

---

## 8. What to dump next

1. **The other 20 bodies of the roster** (func mode, depth 1): 0x00e0f140 `abs`, 0x00e0f180 `acos`,
   0x00e0f1c0 `cos`, 0x00e0f210 `sin`, 0x00e0f260 `ceil`, 0x00e0f3a0 `floor`, 0x00e0f400
   `get_frame_time`, 0x00e0f2c0 `max`, 0x00e0f330 `min`, 0x00e0f580 `sizeof_table`, 0x00e0f5c0
   `sqrt`, 0x00e0f080 `strstr`, 0x00e0f610 `thread_check_done`, 0x00e0f650 `thread_kill`, 0x00e0f680
   `thread_new`, 0x00e0f0d0 `thread_yield`, 0x00e0f740 `closest_point_on_line_segment`, 0x00e0f830
   `which_side_of_2d_line`, plus 0x00e0f010 `include` if §16.3's reading is to be re-checked. `max`
   (704 call sites) and `floor` (449) first. Expect them to be short; `maxinsn=600` is enough.
2. **The ring fill/seed:** force-disassembly of 0x01014500..0x01014700 (the writer at 0x010145e1 lies
   in a region with no function), and func dumps of 0x00dab5a0, 0x00dab810, 0x00dab830 (cursor by
   address) and 0x00dab5b0, 0x00dab5e0, 0x00dab630 (sibling draws), with the xref of 0x013214d0 to
   find the seed caller.
3. **0x00fccb70** (func mode, depth 1) to confirm or refute that the stock Lua standard libraries,
   including a `math` table, are opened for every state before the 24 bare globals. This decides
   whether `math.floor` and `floor` both exist.
4. Not needed: the caller side (closed, §2); `debug_print`, `rand_int`, `rand_float`, `round` (closed).
