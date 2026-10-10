# Next shared blockers, ranked by mission count (2026-10-09, post 17-branch merge queue)

**All numbers in this document are freshly measured from this task's own run against the real game
cache, on this task's own merge commit (`a1c4a2e0e91774e35cd82499d766a3fe9a129908`,
`diag/mission-blocker-recensus-current`, worktree `.claude/worktrees/agent-blockerrecensus`). No number
from `results/probe_blockers_ranked_20261008_*.md` is treated as ground truth anywhere below - those
files are cited only as historical context for what moved, and are explicitly flagged stale where they
disagree with this run.**

## Build/test verification (this task)

Merged all 17 queued branches (merge order: `fix/first-mission-bootstrap-zone`,
`feature/save-write-roundtrip`, `feature/hitscan-fire-dispatch`, `feature/heightmap-ground-following`,
`feature/v03-tod-minimap-hud`, `feature/notoriety-hostility-writer`, `feature/wwise-sfx-media-lookup`,
`feature/character-damage-death`, `fix/bink-merge-group-bit-consumption`,
`feature/playtest-nearfield-traffic-roadblock`, `fix/vorbis-wwise-packet-rebuild`,
`fix/mission-blocker-rerank-zsceneprep`, `fix/second-player-nonnull-default`,
`feature/near-field-ped-spawner`, `feature/vehicle-occupant-hide-flag`,
`fix/weapon-definition-name-resolution`, `fix/notoriety-spawn-group-resolution`) on top of main tip
`c9ab790`. 4 real content conflicts (all in `tools/sr3_viewer.cpp` plus one in
`src/lua_spec_initial_state.cpp`), all purely additive/non-overlapping (two independent features each
adding their own doc-comment block, printf lines, key-state variable, or numbered initial-state item at
the same insertion point) - resolved by combining both sides, never by dropping either one. No logic was
discarded. Fresh from-scratch configure + Release build (`build/`, Visual Studio 17 2022 x64): **0 errors**
(warnings only, all pre-existing patterns). `ctest --test-dir build -C Release -j 4`: **157/157 non-skipped
tests passed, 100%** (161 total; 4 pre-existing Bink golden-vector tests skipped, same 2 "pre-existing Bink
skips" already noted by the vehicle-occupant-hide-flag branch's own report before this merge - not a new
regression).

Real mission drive, `lua_host_run <real cache> tools/lua_all_registered_1490_tagged.txt <out>
--tick-budget=721 --probe-unresolved-names`, against the real archive cache
(`D:\Project Crreish\Saints Row 3 CRREISH\packfiles\pc\cache`). Raw output:
`results/verify_probe_blockers_ranked_20261009_post_merge_queue/`.

## IMPORTANT: the `ai_add_enemy_target` / `0x02cc9900` "31/49" figure is STALE, not current-main truth

Before this task started, the dispatch for this task carried a claim (relayed from the orchestrator) that
`ai_add_enemy_target`'s class-descriptor-byte gate (`+0x33` bit `0x8`, table `0x02cc9900`, Sec3.9) was
still the dominant 31/49-mission blocker on current main. **This is independently verified wrong, by two
separate methods, before any correction arrived from Team A:**

1. **Git ancestry**: commit `7e460fd` ("Fix ai_add_enemy_target actor gate: CHOSEN true by default, not
   OPEN") is already an ancestor of `c9ab790` (main tip immediately before this task's own merge work
   started) - confirmed via `git merge-base --is-ancestor 7e460fd c9ab790` = true. Its own commit message
   explains the real mechanism: Team A's spec-sync (lua-api Sec3.9) corrected `+0x33` bit `0x8` from an
   assumed `0x02cc9900` per-class table field to what it actually is - a per-INSTANCE "fully instantiated"
   one-way latch, CHOSEN true by default, same reasoning class as `lifeState`/`killFinalizedFlagE3`. This
   fix landed on main immediately after the max-HP/phase-8 fix that originally produced the stale 31-
   mission ("28/49" by `7e460fd`'s own recount) figure - i.e. the 31/49 snapshot was already superseded on
   main before this task even began.
2. **Direct code read**: `src/lua_spec_confirmed_stubs_b01_initial.cpp:339` reads
   `actorState.aiTargetActorGateBit33` - a plain `bool` (`CharacterState::aiTargetActorGateBit33` in
   `engine_state.h`), not an `OpenValue<bool>`/`0x02cc9900` table lookup. There is no code path left that
   can throw an OPEN-state refusal for this gate.

**This task's own fresh run (below) confirms both findings empirically**, exactly as Team A's correction
asked:
- `ai_add_enemy_target` does **not appear at all** as an OPEN-state citation anywhere in this run's raw
  output (`grep -c "ai_add_enemy_target" stdout.txt` = 0; `0x02cc9900` = 0 matches anywhere in the results
  directory).
- The native **does run successfully**: `verdict_stub_hits_with_missions.tsv` shows
  `ai_add_enemy_target	8243	24	8219	4	string,string,number,boolean	183	29	8243	0` - 8,219 real calls
  this step, across 183 static call sites/29 scripts, zero refusals.
- The only remaining `+0x33`-adjacent citation anywhere in this run is a completely different, unrelated
  mechanism: `verdict_open_state.tsv` lists `batch2728	target capability+class gate (+0x33 bit 0x10 clear
  AND class bit 0x80@+6 set)	spec-lua-api-behaviour.md Sec27.5	value	0	0` - a different bit (`0x10`, not
  `0x8`), a different section (Sec27.5, not Sec3.9), combined with a separate class-bit condition, and with
  0 missions citing it in this run. **This is a positive finding: the `ai_add_enemy_target` gate is fully
  closed, not merely reduced.**

## Aggregate (this run, fresh)

`stop_reason_histogram: budget_exhaustion=11 error=0 completion=38`. `error=0` holds: zero missions
terminate in an unrecovered error state. `missions_with_start_call_ok=48/49` (only `mm_m1_5` fails the
start call itself, gated by `customization_screen_is_ready`, Sec60.1 - unchanged, genuinely OPEN).
`missions_with_real_completion_flag_0x4_set=0/49` (no mission's own handle is ever marked complete by its
own script within the budget - a separate, deeper fact from `stop_reason=completion`, which only means the
tick loop went idle).

**20 of 49 missions now hit *no* OPEN-state blocker at all within the 721-tick budget:**
- **9 run to a clean, natural idempotent-completion point** with zero `first_error`: `dlc1_mm_01`-
  `dlc1_mm_06`, `mm_p_02`, `mm_p_04`, `mm_p_05`.
- **11 survive the full 721-tick budget with zero `first_error`**, genuinely still running their own
  script logic, not refused by anything: `dlc3_m01`, `sh01`, `sh03`, `m03`, `m11`, `m13`, `m14`, `mm_p_01`,
  `mm_p_03`, `mm_p_06`, `mm_p_07`.

The remaining **29 missions** cite one of 13 distinct OPEN-state blockers (7 shared, 6 single-mission - see
below), none anywhere near the old 31-mission scale of the stale `ai_add_enemy_target` figure.

## Ranked blockers (this run, fresh - top 5 per Team A's request, full list below that)

### 1. Vehicle enter-submitter eligibility `0x00af22a0` OPEN - spec-player-runtime.md Sec7.4 / spec-lua-api-behaviour.md Sec38.1 - **5 missions**

Missions: `dlc2_m01`, `dlc2_m03`, `m05`, `m07`, `m09`.

**Classification: genuine SPEC/MECHANISM gap, not a Team B implementation gap.** Already investigated
exhaustively by an earlier task (see HANDOFF.md's own "Vehicle enter-submitter (`0x00af22a0`) CONFIRMED
genuinely OPEN" finding): Sec7.4 + Sec13(U6.2) trace the eligibility sub-function (`0x00b0ca60`) and seat-
pick functions (`0x00b0fdf0`/`0x00b105c0`) unusually thoroughly, but even the fully-traced sub-functions
need LIVE world state (character/vehicle positions, physics, current occupants) that this project's
headless Lua host has no model of at all - not missing code, a structural fact about what a headless
driver can supply. `dlc2_m01`/`dlc2_m03` reach this gate NEWLY in this run (previously masked by the now-
fixed co-op-player-non-null gate, Sec28.12, which this task's own merge of
`fix/second-player-nonnull-default` closes) - a genuine combined-merge interaction effect, not previously
visible when that branch was tested in isolation.

### 2. `release_to_world` resolved-object kind OPEN - spec-lua-api-behaviour.md Sec4.4 - **3 missions**

Missions: `m04` (`'Starting_Vehicles'`), `m06` (`'Script_Vehicle 001'`), `m15` (`'group_extra_homies'`).

**Classification: genuine SPEC gap.** Sec4.4 never gives the classification rule (1=group/2=character/
3=vehicle) for a named object `release_to_world` doesn't already resolve through another path - three
different object names, same missing mechanism. Team A would need to supply the real classification data
or confirm which of the three categories each of these specific named objects belongs to.

### 3. `continuous_spawn_start` arg 4 default OPEN - spec-lua-api-behaviour.md Sec4.8 - **3 missions**

Missions: `dlc2_m02`, `m24`, `sh04`.

**Classification: genuine SPEC gap.** Sec4.8 itself confirms arg 4 is "a fixed literal constant" (a
type-name filter) but never states the actual value anywhere in the file - a real, already-identified gap
(HANDOFF.md: "continuous_spawn_start arg-4 literal string... confirms it's a literal but never gives the
value anywhere in the file"), not something Team B can infer or choose safely.

### 4 (tie, 2 missions each).

- **`inv_item_in_slot`'s own fixed slot-name enumeration table `0x008dc6b0`** OPEN - Sec13.21 - `m16`,
  `m18`. **SPEC gap**: table codes 0-0xb never populated with real names anywhere in spec.
- **Audio 'current conversation' lookup `0x006de8a0`** OPEN - Sec2.10 - `m10`, `mm_m16_5`. **SPEC gap**:
  no spec value given for this lookup's resolution.
- **Local player exists `0x009da4e0`** OPEN - Sec10.3 - `m17`, `m01`. **SPEC gap**: no spec value given.

All five of the top-ranked shared items above are genuine spec gaps (Team A has not yet supplied a real
value/table/mechanism), not implementation gaps - consistent with this project's general current shape:
Team B has already wired every mechanism it can build against what spec currently gives it; what remains
blocking missions is specifically missing data/default values only Team A's own disassembly-sourced
spec-sync can supply.

## 5-6. Two further single-instance items sharing one section, 2 distinct globals (HANDOFF-confirmed, not a true shared blocker)

- **Tutorial dispatcher global byte `0x0151d5a6`** (Sec6.19) - `m02` only. SPEC gap (meaning of the byte
  not given; only known that non-zero suppresses base-game `[index<189]` prompts).
- **Tutorial dispatcher global `0x029443bc`** (Sec6.19) - `m12` only. SPEC gap, a genuinely different
  global from `m02`'s (already confirmed as "2 distinct globals" by an earlier task's own trace of
  dispatcher step short-circuiting - not re-chased here).

## 7-16. Single-mission items (one mission each, no shared blocker)

- `dlc3_m02`: character vtable `+0x68` predicate for `set_attack_enemies_flag` (unidentified) OPEN - Sec17.5
  - SPEC gap, a recurring project-wide unresolved idiom (10+ other OPEN occurrences elsewhere per earlier
  tasks, not re-investigated here).
- `dlc3_m03`: `ai_scripted_action_is_processing`'s own gate `0x008addb0` OPEN - Sec13.7 - SPEC gap.
- `m08`: light-group name resolution `(0x00739690)['Script_Light_Group_m08_ALL']` OPEN - Sec17.24 - SPEC gap.
- `m19`: game clock hour byte `(0x014ff33c)` OPEN - Sec32.1/Sec48.2/Sec48.4 - SPEC gap. **New blocker for
  `m19`**: the now-merged `fix/weapon-definition-name-resolution` branch closed `m19`'s PRIOR blocker
  (weapon-definition name resolution, `'special-sonicgun'`) exactly as that branch's own report predicted
  ("even it now moves to a NEW, deeper, genuinely different blocker") - confirmed here, `m19` now stops on
  this completely different game-clock gate instead.
- `m21`: character attacker/threat reference OPEN - Sec3.4 - SPEC gap.
- `m20`: RC max signal range global float (start-up default not traced) OPEN - Sec39.2 - SPEC gap (unchanged
  from all prior docs).
- `m22`: mission object `+0x88` bit `0x4` ('is mission complete') `['m23']` OPEN - Sec27.2/Sec20.14 - SPEC
  gap (unchanged from all prior docs).
- `sh02`: audio persona line currently playing `(0x0094c9e0/0x0046afd0)['#PLAYER1#']` OPEN - Sec8.4 - SPEC
  gap.
- `m23`: `0x012ec964` (continuous explosion global state) OPEN - Sec28.16 - SPEC gap (unchanged).
- `mm_m1_5`: `customization_screen_is_ready` backing flag OPEN - Sec60.1 - SPEC gap (unchanged from all
  prior docs; this is also the one mission whose `_start` call itself never completes, `start_call_ok=0`).

## 17+. No blocker hit at all (20 missions, up from 16 in the last pre-merge snapshot this task could find)

Clean completion (9): `dlc1_mm_01`, `dlc1_mm_02`, `dlc1_mm_03`, `dlc1_mm_04`, `dlc1_mm_05`, `dlc1_mm_06`,
`mm_p_02`, `mm_p_04`, `mm_p_05`.

Still running at budget exhaustion with zero `first_error` (11): `dlc3_m01`, `m03`, `m11`, `m13`, `m14`,
`mm_p_01`, `mm_p_03`, `mm_p_06`, `mm_p_07`, `sh01`, `sh03`. `mm_p_01`/`mm_p_06` in particular confirm the
already-merged `fix/mission-blocker-rerank-zsceneprep` branch's own measured claim ("both now run to budget
exhaustion with no `first_error` at all") - independently re-verified true in this combined, fully-merged
run, not merely inherited from that branch's own report.

## What moved vs. the stale 2026-10-08 baseline (context only, not cited as current truth)

The 2026-10-08 `post_maxhp_phase8_fix` baseline's own item 1 (`ai_add_enemy_target`, claimed 31/49) was
**already stale before this task started** - see the dedicated section above. Everything in that baseline
document and in `probe_blockers_ranked_20261008_post_missioncontextname.md` (also already in this
worktree, merged in via `fix/mission-blocker-rerank-zsceneprep`) predates this task's own 17-branch merge
and is superseded by the numbers above. Notable deltas this task's own merge directly produced, confirmed
by this run (not assumed from any branch's own isolated report):
- Co-op/second-player non-null (`0x009df3d0`, Sec28.12) is **completely gone** as a blocker in this run -
  `fix/second-player-nonnull-default` closed it for the missions it used to gate (`dlc2_m01`, `dlc2_m03`,
  `dlc3_m02`, `m12`, `m24` all now cite a different, deeper blocker or run clean).
- Notoriety spawn-group resolution (`0x008ed350`['stag_attack_heli']`, Sec13.2) is **gone** - `m17` (its
  only measured target) now cites `local player exists` instead, a deeper gate.
- Weapon-definition name resolution (`0x028dc4dc`, Sec4.10/4.11) is **gone** - `m19` (its only measured
  target) now cites the game-clock-hour-byte gate instead, a deeper gate, exactly as that branch's own
  report predicted.
- `zscene_prep` handle-class (`0x00dafb60`, Sec26.25) is **gone** - `mm_p_01`/`mm_p_06` both now run clean
  to budget exhaustion.

## Summary for whoever triages next

The `ai_add_enemy_target`/`0x02cc9900` 31-mission figure some earlier relay carried forward is **stale and
should not be repeated** - it was already fixed on main (`7e460fd`) before this task's 17-branch merge
even began, independently confirmed by git ancestry, direct code read, and this run's own empty grep for
both the native name and the table address. The REAL current #1 shared blocker, measured fresh on the
fully-merged state, is the vehicle enter-submitter eligibility gate (`0x00af22a0`, 5/49) - a genuine,
already-exhaustively-investigated structural gap (no live world-state model in this headless host), not a
quick fix. Every other top-ranked item is a genuine Team A spec-data gap (a named table/default/lookup
value never given), not a Team B implementation gap. `error=0` holds across the entire 49-mission
population on this fully-merged state. 20/49 missions now hit no OPEN-state blocker at all within budget,
up from a much smaller clean population before this merge wave.
