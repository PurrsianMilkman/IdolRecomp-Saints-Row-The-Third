# Lua natives backlog recount — correcting the fan-out scoping pass's registration baseline (2026-10-08)

**Why this exists.** `results/lua_natives_sec10_28_fanout_scoping_20261008.md` built its "already registered" baseline by reading only the 7 `namesB0N()` vectors in `src/lua_spec_confirmed_stubs_b01_initial.cpp`...`_b07_sec8_9_teamA.cpp` (302 names) and diffing the spec against that. A wave-1 agent assigned Batch 6 (bare math/RNG/thread-scheduler, 24 names) found all 24 already implemented via a **second, completely separate registration track** — `specBareGlobals()`/`bareGlobalRoster()` (`include/sr3luahost/bare_globals.h`, `src/lua_bare_globals.cpp`) — that the scoping pass's method never looked at. This task re-derives the *real, complete* registered-name baseline (reading source directly, not estimating) and re-checks the scoping pass's 855/633/198-native counts against it.

## 1. The real baseline: **327 names, three tracks — not two**

### Track 1+2: `specConfirmedStubNames()` — verified a TRUE union, but of only two tracks

Read `src/lua_spec_confirmed_stubs_registry.cpp` directly (lines 102-133, not its prose comment). `specConfirmedStubNames()` really does:
```
names = namesB01() ++ namesB02() ++ ... ++ namesB07()   // 302, concatenated
all   = names ++ bareGlobalRoster().name                 // + 24, appended
return all;
```
So the "unions every registration track" claim is **true for exactly two tracks**: the 7 generic-stub sections (b01-b07) and the 24 bare globals. Re-extracted both directly from source (not estimated):

- **b01-b07**: parsed each file's own `static const std::vector<std::string> names = {...}` literal array directly (regex over the real source text, comments stripped). Counts: b01=36, b02=50, b03=34, b04=58, b05=49, b06=45, b07=30 → **302**, zero duplicates. This matches the original scoping report's own §0.1 claim (36+50+34+58+49+45+30=302) and the registry.cpp doc-comment's own "302 call sites" claim — three-way agreement.
- **bare globals**: `bareGlobalRoster()` in `src/lua_bare_globals.cpp` lines 42-67, **24** entries (`abs acos cos sin ceil debug_print assert_msg floor get_frame_time include max min rand_float rand_int round sizeof_table sqrt strstr thread_check_done thread_kill thread_new thread_yield closest_point_on_line_segment which_side_of_2d_line`), zero overlap with the 302.

`specConfirmedStubNames()` therefore returns 302+24 = **326** names.

### Track 3 (the NEW finding): `registerThreadClose()` — a third track `specConfirmedStubNames()` never sees

Grepped `src/lua_host.cpp` for every `register*` call in `Host::Host()` and cross-checked against every `register*`-named function and every raw `lua_setglobal`/`lua_register` call site in `src/` + `include/sr3luahost/` + `tools/lua_host_run.cpp` (two independent sweeps, one by naming convention, one not — same 5 files both times, no stragglers).

`Host::Host()` (`src/lua_host.cpp` lines 30-218) calls exactly 4 registration functions, in order:
1. `registerBareGlobals(...)` ×2 (gameplay_, ui_) — the 24 bare globals.
2. **`registerThreadClose(gameplay_, threadScheduler_, hitLog_, "gameplay")` / `(ui_, ...)`** (lines 58-59) — registers **`thread_close`** into BOTH states, via `src/lua_thread_scheduler.cpp`'s `ThreadScheduler` class (the pre-spec scaffold). The call site's own comment (line 54-57): *"thread_close: the pre-spec ThreadScheduler scaffold's version, kept into both states. It is neither one of the 24 bare globals nor in the registration list, so whether the engine provides it is OPEN."* This is real, active registration code, completely independent of `specConfirmedStubNames()`/`registerSpecConfirmedStubs()` — it is never filtered through `isSpecConfirmed()`, never passed through `wants()`, and `specConfirmedStubNames()`'s own returned list (326 names) does **not** include `"thread_close"`.
3. `registerSpecConfirmedStubs(...)` ×2 — the 302 b01-b07 names, split by cluster tag into `specGameplayNames`/`specUiNames`.
4. `registerStubs(...)` ×2 — `genericStubTrampoline` (`src/lua_stub_registry.cpp`), a **generic "always push nil" logging stub** for every other name in the master registered-name list. Verified this is not a real-implementation track (it unconditionally `lua_pushnil`s and records a hit — this is exactly the "generic stub" category the whole gap analysis measures against, not a 4th hidden implementation track).

**Dead-code note (not a track, but worth flagging):** `registerThreadScheduler()` (the *full* 5-name version — `thread_new`/`thread_yield`/`thread_kill`/`thread_check_done`/`thread_close`, declared in `include/sr3luahost/thread_scheduler.h:172`, defined in `src/lua_thread_scheduler.cpp:151`) is **never called anywhere** in the codebase (confirmed by grep for `registerThreadScheduler(` call sites — only its own declaration and definition exist). Only its narrower sibling `registerThreadClose()` (same file, line 159, registers `thread_close` alone) is actually invoked. If a future pass ever wires the full version in by mistake, it would silently re-install the old scaffold's `thread_new`/`thread_yield`/`thread_kill`/`thread_check_done` *over* the real bare-globals versions the engine order relies on — a latent trap, not a currently-active blind spot.

### Corrected total

```
302 (b01-b07)  ∩  24 (bare globals)      = ∅
302 (b01-b07)  ∩  {thread_close}         = ∅
24 (bare globals) ∩  {thread_close}      = ∅

specConfirmedStubNames()                  = 302 + 24 = 326
REAL COMPLETE BASELINE                    = 326 ∪ {thread_close} = 327 unique names
```

Full baseline list (327 names) extracted to `baseline_all.txt` during this task's working session; reproducible directly from the three source locations cited above.

## 2. Re-checking the 633-native backlog: **0 affected**

**Methodology problem found first:** §6 of the original scoping report does **not** materialize the 633 backlog as a flat name list — it's a 3-row table by spec-document section range (§17-22: 163, §23-26 residue minus the §26.27 math cluster: 98, §38-60: 372 = 633). There is no `.tsv`/`.txt` anywhere in this worktree's `results/` holding the individual 633 names, and the report's own §8 "Files" section points at a *different* worktree (`agent-a19466b37c3ff40a7`) for its sub-agents' raw output, which isn't available here either. So "re-check every one of the 633 names" had to be done structurally rather than name-by-name against a list that doesn't exist.

**What I did instead, directly against the real spec source (`spec-lua-api-behaviour.md`, 15,006 lines):**
1. Mapped all 60 `## N.` section headers to line numbers (same method the original scoping pass used, independently re-run).
2. For each of the 25 "blind-spot" names (24 bare globals + `thread_close`), located every line where that identifier occurs and classified it by which section it falls in.
3. Separately, extracted every `### N.M \`name\`` **header-level** entry (633 of them, document-wide — distinct from the backlog's own 633 count, which is a coincidence of totals, not the same set) and checked for exact matches against the 25 names.

**Result:** of the 25 names, the *only* header-level hit outside §26 anywhere in the entire 15,006-line document is `get_frame_time` at line 13815, inside **§37.2** ("The engine keeps two millisecond clocks, but neither backs `get_frame_time`") — a cross-reference/investigation entry discussing the clock mechanism, explicitly pointing back at §26.27's own already-complete entry ("`get_frame_time`'s own global... detailed in this section's own entry above"), not a new catalogue item. §37 is not one of the backlog's cited ranges (§17-22, §23-26, §38-60) regardless. Every other occurrence of all 25 names in the whole document (abs/acos/cos/sin/ceil/debug_print/assert_msg/floor/include/max/min/rand_float/rand_int/round/sizeof_table/sqrt/strstr/thread_check_done/thread_kill/thread_new/thread_yield/closest_point_on_line_segment/which_side_of_2d_line) is confined to §26 (specifically the §26.1-26.28 run, where §26.27 documents exactly this cluster) — which the backlog's own §23-26 row **already explicitly subtracts** ("§23-25 + §26 minus its math cluster (already Batch 6)"). `thread_close` has **zero** occurrences anywhere in `spec-lua-api-behaviour.md` — consistent with `lua_host.cpp`'s own comment that whether the engine provides it at all is OPEN; it was never a spec-catalogued "cleared" name the backlog's counting method could have picked up in the first place.

**Conclusion: 0 of the 633 backlog natives are affected by the corrected baseline.** The blind spot that inflated Batch 6 does not inflate the 633-backlog count, because the backlog's own §23-26 row already structurally excludes the one spec section (§26.27) where the 24 bare globals live, and `thread_close` isn't a spec name at all. **The 633-native backlog stands: 633 real gaps, 0 false positives found from this specific blind spot.**

## 3. Spot-check of wave-1 Batches 1-5 and 7 (198 names): **0 affected**

Transcribed all 198 dispatched names directly from the scoping report's own §3 batch listings (Batch1_§1-4=21, Batch2_§5-9=25, Batch3_§11-12=40, Batch4_§13-14=55, Batch5_§15-16=41, Batch7_§30=16 — totalling 198, matching the report's own 222-24=198 arithmetic, a transcription sanity check that passed) and cross-checked **every single one** (not a sample — all 198, exact string match, scripted, not eyeballed) against the full corrected 327-name baseline from §1 above.

**Result: zero overlaps.** None of batches 1, 2, 3, 4, 5, or 7's 198 names are already registered via any of the three real tracks. This matches the structural expectation: those batches are gameplay/UI feature natives (triggers, missions, vehicles, HUD, customization, combat AI) — a domain entirely disjoint from the base-library math/RNG/thread-scheduler primitives the blind spot was specifically about. **No false positives found in batches 1-5/7; only Batch 6 (already reported separately) was affected.**

## 4. Confidence that no further blind-spot track exists

High, with the caveat below spelled out honestly rather than glossed over.

**What was checked exhaustively:**
- Every function matching `register*` with external linkage across `src/` and `include/sr3luahost/` (grep by naming convention): exactly 7 — `registerBareGlobals`, `registerB01`..`registerB07` (internal plumbing for `registerSpecConfirmedStubs`, not independently called), `registerSpecConfirmedStubs`, `registerStubs` (generic nil stub), `registerThreadScheduler` (dead code, never called), `registerThreadClose` (active, 1 name).
- Every raw `lua_setglobal`/`lua_register` call site across `src/`, `include/sr3luahost/`, and `tools/lua_host_run.cpp` (grep bypassing the naming convention entirely, in case some ad hoc registration doesn't follow the `register*` pattern): landed in exactly the same 5 files already covered above. `tools/lua_host_run.cpp` itself registers nothing extra — it only drives the real `Host` class.
- `Host::Host()` read start-to-finish (`src/lua_host.cpp` lines 30-218): confirmed the full, exact sequence of registration calls and that nothing else happens between/around them.

**Residual caveat:** this check covers everything that wires names into the two Lua states the real `Host` class constructs (`gameplay_`/`ui_`) — the only thing that matters for "is this native really implemented." It does *not* exhaustively scan `tests/` for private test-only mini-registrations (irrelevant to this question — tests exercise the real `Host`, they don't add their own second implementation), nor every file in the repo outside `src/`+`include/sr3luahost/`+`tools/lua_host_run.cpp`. Given `Host::Host()` is the single, sole constructor for the game-facing Lua states and every one of its registration calls was traced to its definition, I'm confident no 4th active track exists today. The one thing a future pass should re-check if the registration surface changes again: whether `registerThreadScheduler()` (currently dead code) ever gets wired in somewhere — if it does, re-run this same cross-check, since it would reintroduce `thread_new`/`thread_yield`/`thread_kill`/`thread_check_done` under the *old* scaffold semantics alongside the real bare-globals versions.

## 5. Summary table

| Quantity | Original scoping pass | Corrected (this pass) |
|---|---|---|
| "Registered" baseline size | 302 (b01-b07 only) | **327** (302 b01-b07 + 24 bare globals + 1 `thread_close`) |
| Registration tracks the baseline covers | 1 of 3 | **3 of 3** (verified exhaustively, see §4) |
| 855 total cleared-but-unregistered (wave-1 222 + backlog 633) | 855 | Unchanged outside Batch 6 — the 24 Batch 6 names were already separately confirmed false positives (`results/lua_natives_batch6_baselib_already_implemented_20261008.md`); the other 831 stand |
| 633-native backlog | 633 | **633 — 0 false positives from this blind spot** (§2 above) |
| Wave-1 Batches 1-5, 7 (198 names) | 198 real gaps assumed | **198 — 0 false positives found** (§3 above, full 198/198 checked, not sampled) |
| Batch 6 (24 names) | assumed real gaps | **24/24 already a confirmed false positive** (prior task, restated here for completeness) |

**Net effect on wave 2 dispatch: none.** The 633-native backlog is real; dispatching wave 2 against it is not wasting effort on already-done natives, at least not for the specific blind spot this task was asked to re-check (the bare-globals/thread-scheduler tracks). No further corrective action needed on the backlog count itself.

## 6. Files

- This report: `D:\Project Crreish\TEAM B\.claude\worktrees\agent-a14aec079d8b002e0\results\lua_natives_backlog_recount_20261008.md`
- Baseline re-derived directly from: `src/lua_spec_confirmed_stubs_b01_initial.cpp` through `_b07_sec8_9_teamA.cpp` (302), `src/lua_bare_globals.cpp` (24, `bareGlobalRoster()`), `src/lua_thread_scheduler.cpp` + `src/lua_host.cpp` (the `thread_close` third track).
- Union-claim source read directly: `src/lua_spec_confirmed_stubs_registry.cpp`.
- Spec source re-read in full for the backlog cross-check: `spec-lua-api-behaviour.md` (15,006 lines).
- Original report corrected here: `results/lua_natives_sec10_28_fanout_scoping_20261008.md`.
- Prior, narrower finding this task extends: `results/lua_natives_batch6_baselib_already_implemented_20261008.md`.
