# Batch 6 (bare math/engine-globals/RNG/thread-scheduler, Sec26.27) — already fully implemented; false gap in the fan-out scoping report

**Task as assigned**: wave-1 Batch 6 of the Sec10-28 lua-natives fan-out
(`results/lua_natives_sec10_28_fanout_scoping_20261008.md` §3, "Batch 6"),
24 natives — `abs`, `acos`, `cos`, `sin`, `ceil`, `debug_print`,
`assert_msg`, `floor`, `get_frame_time`, `include`, `max`, `min`,
`rand_float`, `rand_int`, `round`, `sizeof_table`, `sqrt`, `strstr`,
`thread_check_done`, `thread_kill`, `thread_new`, `thread_yield`,
`closest_point_on_line_segment`, `which_side_of_2d_line` — with instructions
to create a new `src/lua_spec_confirmed_stubs_b13_batch6_baselib.cpp`
following the b01-b07 generic-stub pattern, after first designing a shared
RNG ring and thread/coroutine scheduler.

**Finding: this batch does not exist as a gap. All 24 natives were already
implemented, tested, and wired live into the host before this scoping pass
ran.** No new file was created; no code was changed.

## 1. What's actually there

`include/sr3luahost/bare_globals.h` + `src/lua_bare_globals.cpp` implement
all 24 names as "the 24 bare globals of 0x00e0f900"
(spec-lua-bindings.md Sec13.2/Sec16.3/Sec16.4, spec-lua-api-behaviour.md
**Sec26.27 — the exact section this task cited**), registered through a
dedicated `registerBareGlobals()` call, *not* through the generic
`registerSpecConfirmedStubs()`/`namesB0N()`/`registerB0N()` machinery the
task asked me to extend with a `b13` file.

Verified live wiring (`src/lua_host.cpp:39-50`):
```cpp
for (const auto& bg : specBareGlobals()) { ... }
registerBareGlobals(gameplay_, bareGlobals_, hitLog_, "gameplay", bareGameplay);
registerBareGlobals(ui_, bareGlobals_, hitLog_, "ui", bareUi);
```
`specBareGlobals()` (`src/lua_spec_confirmed_stubs_registry.cpp:88-100`) and
`bareGlobalRoster()` (`src/lua_bare_globals.cpp:40-69`) list exactly these 24
names, in exactly the order given in my task brief, each mapped to its real
native address (e.g. `abs` -> `0x00e0f140`, `thread_new` -> `0x00e0f680`).
Cross-checked directly against `spec-lua-api-behaviour.md:11800-11858`
(Sec26.27's own roster table) — identical set, identical order, identical
addresses.

**The shared infrastructure the task asked me to design first is already
built, to a higher standard than a fresh design would likely produce**:
- `BareRandomSource` (`bare_globals.h:69-95`) — the single 8192-entry RNG
  ring `rand_int`/`rand_float` both draw from, cursor-advance-and-wrap
  semantics, a `FileImage` mode (matches "before first fill" CONFIRMED
  all-zero behaviour) and an opt-in `HostRng` mode (splitmix64 substitute,
  clearly labelled HOST SUBSTITUTE/HYPOTHESIS per spec-lua-api-behaviour.md
  Sec26.27's own "host note").
- `BareThreadTable` (`bare_globals.h:212-342`) — the full script-thread
  table/runner/scheduler (`0x00e0cba0`/`0x00e0cf50`), including the
  per-frame 11-slot-dispatch-adjacent scheduler pass, swap-compaction
  removal, the up-to-16-deep current-thread stack, and **the pause-exempt
  list** (`exemptState`/`unexemptState`/`isExempt`, up to 4 keys) the task
  brief specifically flagged as part of this batch's shared state. This is
  *not* the older `ThreadScheduler` class (`thread_scheduler.h`) the task
  told me to check for — that class's own top comment states plainly:
  "2026-10-02: superseded for everything but `thread_close`... The engine's
  real script-thread table, its runner and its scheduler 0x00e0cf50 ... are
  modelled by `BareThreadTable`". `ThreadScheduler::newThread` (the
  "existing precedent" the task brief pointed at) is kept alive today only
  to back `thread_close`, a 25th name that is *not* in this batch and not
  in the Sec26.27 roster.
- `IncludeQueue` (`bare_globals.h:110-137`) — the 64-slot deferred `include`
  queue.
- `max`/`min` are implemented as the task brief's own described
  non-standard binary semantics: `bare_max`/`bare_min`
  (`lua_bare_globals.cpp:581-594`) — second-argument-wins on ties/NaN,
  confirmed against Sec26.27's own text ("the first argument wins only on a
  strict ordered comparison, otherwise (ties, 0 vs -0, NaN) the second").
- Single-precision-narrowing-everywhere is centralized in one `narrow()`
  helper (`lua_bare_globals.cpp:476-483`) and `argFloat()`/`pushFloat()`
  wrappers, used by every numeric native — not 24 independent
  re-derivations.

All of this was built across three real commits in this project's history
(`git log --oneline -- src/lua_bare_globals.cpp`):
`1ee65f1` "Implement script-thread resume scheduler (0x00e0cf50) +
0x0153b530=null (spec-lua-api-behaviour.md Sec26.27/Sec26.25)" and
`fadf762` "Fix resume-scheduler cadence: 1 pass/tick, not 11 (stuck-delay
investigation)", both part of a 2026-10-02 "stuck-delay-mission
investigation" that long predates this 2026-10-08 scoping pass.

## 2. Hardening already present (policy check)

The task's HARDENING POLICY asked me to flag any genuine memory-safety bug
in the original and harden rather than replicate. The existing
implementation already does this, independently, for the two spots in this
batch's own spec text that call out undefined/crashing original behaviour:
- `rand_int`'s `hi - lo + 1 == 0` (full 32-bit range) divides by zero in the
  original; the spec's own text says "a host need not reproduce this" —
  implemented as a raised `PendingLuaError`, not a crash
  (`lua_bare_globals.cpp:682-684`).
- `strstr` with a non-string/non-number argument reaches the original's
  search through a null pointer and crashes the engine; implemented as a
  raised error instead (`lua_bare_globals.cpp:781-784`).

Both are labelled inline with their spec citation, matching the spirit of
the task's `CHOSEN (hardened divergence)` convention even though they
predate this task.

## 3. Root cause of the false gap (relayed-claim verification, not dismissal)

Per this project's own peer-relay-authorization discipline, I verified
rather than trusted the scoping report's "cleared but unregistered" claim
for this batch, and it does not hold up. The scoping report's own §0
Method section states its registered-name baseline was built by "reading
each `namesB0N()` vector directly out of `src/lua_spec_confirmed_stubs_
b01_initial.cpp` through `_b07_sec8_9_teamA.cpp`" — i.e. only the generic
stub-registration track. It never consulted `specBareGlobals()`/
`bareGlobalRoster()`, a structurally separate registration path (its own
struct type `SpecBareGlobal`, its own registration function
`registerBareGlobals()`, called directly from `Host`, not from
`registerSpecConfirmedStubs()`) added by the thread-scheduler work in
commits `1ee65f1`/`fadf762`. `specConfirmedStubNames()` does append the 24
bare-global names at the very end of its returned vector
(`lua_spec_confirmed_stubs_registry.cpp:122-131`), so a tool that called
that *public* API would have seen them as registered — but the scoping
report bypassed that API and read the private `namesB0N()` vectors instead
the same shortcut that makes the b01-b07 count easy to verify by hand
breaks down for the one section that register through a different path.

A second, independent artifact shows the same stale-baseline pattern:
`tools/lua_reconciliation_called_not_registered_101.tsv` (a static
call-site census, `git log` shows it has had exactly one commit — the
squashed worktree-baseline — i.e. it was never regenerated after the
bare-globals work landed) lists all 24 of this batch's names (e.g.
`thread_yield	1474	94	thread`, `debug_print	816	161	debug`,
`max	704	192	max`, ..., `cos	2	1	cos`) as "called, not registered." This
is very plausibly the actual data source (or a sibling of it) the fan-out
scoping agents' sub-agents cross-referenced — it is simply out of date.

This is the same class of error the scoping report itself caught 4
instances of in its own §0.4 ("4 genuine false positives" — individual
names wrongly re-flagged because they were already registered in b03/b06/
b07) — except at batch scale: an entire dedicated-agent batch, not
individual strays, because the whole registration *mechanism* for this
batch sits outside where the scoping method looked.

## 4. Action taken

**None of the task's requested code changes were made, deliberately.**
Creating `src/lua_spec_confirmed_stubs_b13_batch6_baselib.cpp` and wiring
a `registerB13()` call for these 24 names would have:
1. Registered the same 24 Lua globals a second time in both states
   (`lua_setglobal` is last-write-wins), and
2. Depending on registration order, **silently shadowed the existing,
   well-tested, spec-citation-complete implementation with a fresh,
   independently-derived (and necessarily lower-confidence, since it
   would not have had this analysis session's evidence trail) stub** —
   actively regressing a correct implementation, not filling a gap.

This is the genuine decision-fork this project's no-invented-fixes policy
is for: building the assigned deliverable would have made the codebase
worse, not better, so I verified the premise instead of executing on it
and stopped to document the correction.

## 5. Independent verification performed this pass

- Read `spec-lua-api-behaviour.md:11800-11889` (Sec26.27 in full) directly
  against the implementation; roster name/order/address table matches
  exactly; `rand_int`'s swap-and-inclusive-modulus semantics, the
  single-precision-narrowing-everywhere rule, and the `closest_point_on_
  line_segment`/`thread_kill`/`thread_yield` non-standard return-count
  notes all match the implementation's own comments line-for-line.
- Grepped all of `src/lua_spec_confirmed_stubs_b0*.cpp` for all 24 names as
  string literals: zero matches — confirmed no competing/duplicate
  registration already exists in the generic-stub track either.
- Fresh `cmake -B build -S .` (Visual Studio 17 2022 generator) + full
  `cmake --build build --config Release -j 16`: **exit code 0, clean**
  (one transient `LNK1104` mid-session was self-inflicted — I accidentally
  ran two concurrent `cmake --build` invocations against the same build
  directory from the same session; killed both, confirmed no build
  processes remained, and reran a single clean build, which succeeded).
- `build/Release/sr3luahost_bare_globals_tests.exe` (the dedicated 726-line
  suite for exactly these 24 names,
  `tests/synthetic_luahost_bare_globals_test.cpp`): **`ALL sr3luahost
  BARE-GLOBAL TESTS PASSED`**.
- Full `ctest --test-dir build -C Release --output-on-failure`: see below.

## 6. Full ctest -C Release results

Fresh `ctest --test-dir build -C Release --output-on-failure`:

```
100% tests passed out of 93

Label Time Summary:
fast         =  12.81 sec*proc (73 tests)
golden       =   0.05 sec*proc (1 test)
real-data    =   1.95 sec*proc (7 tests)

Total Test time (real) =  25.42 sec
```

All 93 tests pass, including `synthetic_luahost_bare_globals_test` (#53,
0.03s) — the dedicated suite for this batch's 24 natives — and every other
`synthetic_luahost_*` test (registry/registration wiring, refusal-stress,
roster, cutscene, every prior batch's own suite). Zero failures, zero
changes made by this pass, so this is a health confirmation of pre-existing
work, not a verification of anything new.

## 7. Build directory for cleanup batch

- `D:\Project Crreish\TEAM B\.claude\worktrees\agent-aaef9c41d541185e3\build`
- Size: 0.37 GB
- Why it exists: a fresh `cmake -B build -S .` + full `cmake --build build
  --config Release -j 16` + full `ctest`, run only to independently verify
  the existing bare-globals implementation and the project as a whole are
  healthy (no code was changed). Safe to delete whenever the orchestrator
  batches cleanup — not left behind for any other reason.

## 8. Recommendation for the orchestrator / next scoping pass

- Correct `results/lua_natives_sec10_28_fanout_scoping_20261008.md`: Batch 6
  is **0 real gap**, not 24. This wave's real new-native total (§3's "222
  natives across 7 agents") should read **198**, not 222 (21+25+40+55+41+0+16).
  The 633-natives backlog in §6 is unaffected (Batch 6 was never counted in
  it).
- Any future fan-out scoping pass for the remaining backlog should build its
  "already registered" baseline from the **public**
  `specConfirmedStubNames()` API (which already unions both tracks,
  `lua_spec_confirmed_stubs_registry.cpp:102-133`) rather than re-deriving it
  from the private per-file `namesB0N()` vectors, and should either
  regenerate `tools/lua_reconciliation_called_not_registered_101.tsv` first
  or treat it as stale, since it predates the bare-globals work.
