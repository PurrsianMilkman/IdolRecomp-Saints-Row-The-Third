# Next shared blockers, ranked by mission count (2026-10-08, post max-HP/phase-8 fix)

Direct follow-up to `results/probe_blockers_ranked_20261008_post_resolver_merge.md` (does not supersede
it - kept as the prior baseline). Branch `fix/maxhp-formula-mission-phase8` (worktree
`.claude/worktrees/agent-missionblockers`), on top of main tip `5a58409`. This session implemented and
measures two spec-confirmed fixes (`spec-lua-api-behaviour.md` Sec34.2 max-HP formula, Sec21.27a mission-
phase-8 machine) plus one sibling defect of the same root cause found while re-measuring the first fix
(ignore-AI/`script_npc_flags`, Sec34.3) - see this project's own commit on this branch for the full
per-fix rationale, and the "Root cause" section below for the precise mechanism.

Real mission drive, `lua_host_run <real cache> tools/lua_all_registered_1490_tagged.txt <out>
--tick-budget=721 --probe-unresolved-names` (byte-identical invocation to the prior doc's own header),
against the real archive cache, run from a from-scratch build of this branch's own tip (`build`, Release;
`ctest` 132/132 passing beforehand, re-confirmed again after the sibling ignore-AI fix, also 132/132).
Raw output: `results/verify_probe_blockers_ranked_20261008_post_maxhp_phase8_fix/`.

**Aggregate: `stop_reason_histogram: budget_exhaustion=7 error=0 completion=42`** (prior baseline:
`budget_exhaustion=4 error=0 completion=45`) - **`error=0` still holds**: zero missions terminate in an
unrecovered error state, same as the prior baseline. `budget_exhaustion` rose from 4 to 7 missions
because several missions that used to fail instantly now run far enough to reach a genuine long-running
in-script wait (`total_ticks_survived_across_all_missions` roughly doubled, 3109 -> 5267;
`total_stub_calls_this_step_contributed` roughly doubled, 49601 -> 97543) - this is real additional
coverage, not a regression.

## Root cause of the 26-mission max-HP blocker (item 1 of the prior doc)

**Not a missing accessor, not an unpopulated `spawnRankHitPoints_`, and not a genuinely zone-data-gated
case.** `EngineState::drawOrGetMaxHitPoints()` (`src/lua_engine_state.cpp`) already had a correct,
non-throwing path for an ordinary (non-`scriptNpcBound`) character, and `spawnRankHitPoints_` was
correctly populated from the real `patch_compressed.vpp_pc`/`spawn_info_ranks.xtbl` in every real run
(`tools/lua_host_run.cpp`'s `installSpawnRankHitPointsFromCache()`, unchanged). The 26 blocked missions'
characters are genuinely `scriptNpcBound` (their names resolve against real `.czn_pc` `script_npc`-class
zone records, via the already-landed real zone-object resolver, `EngineState::loadZoneObjectsSection()`)
- but **the bug was in how that same method recorded "no `script_npc_hp` property on this record."**

`sr3zone::ScriptNpcHp(rec)` correctly returns `std::nullopt` for a resolved record that carries no
`script_npc_hp` property - the documented common case (`spec-zone-data-format.md` Sec11.7: only 1,028 of
6,788 `script_npc` objects carry one). The pre-existing code treated that `nullopt` by calling
`c.scriptNpcHp.forget()` - i.e. "unknown," the same state used for a name that has no resolved zone
record at all. But `spec-lua-api-behaviour.md` Sec34.2 is explicit and CONFIRMED (disassembly) that the
absence has a real, known meaning: *"A script NPC's own constructor defaults that field to -1 ('use the
table'); `commando_spawn`-produced script NPCs ... never set `script_npc_hp`, so they always fall back to
the table."* Once the record is resolved (which it now is, via the real `.czn_pc` ingest - a fact Team A
confirmed CLOSED with real-data proof, Sec11.7), the absence of the property on THAT SPECIFIC RECORD is
a CONFIRMED fact (`script_npc_hp = -1`), not an open question - conflating it with "unknown" left
`drawOrGetMaxHitPoints()` refusing (`OpenStateError`) forever for every `script_npc` character lacking an
authored override (the large majority - `get_max_hit_points`/`set_current_hit_points`, Sec7.12/Sec7.31,
are called 3,133 times across the real mission population, and essentially every `script_npc`-bound
character without an authored `script_npc_hp` hit this exact refusal).

**Fix:** `loadZoneObjectsSection()` now calls `c.scriptNpcHp.set(-1)` instead of `.forget()` when the
resolved record carries no `script_npc_hp` property, letting `drawOrGetMaxHitPoints()`'s own
already-correct precedence rule (">= 1 wins verbatim; a known value < 1 falls through to the ordinary
table draw") take the real table path. The one case this task was told to leave alone - a `scriptNpcBound`
character with NO resolved zone record at all (`markScriptNpcBoundForTesting()`'s own test-only stand-in
for "we don't even know if this name is a script NPC") - is untouched and still correctly OPEN.

**Sibling defect found while re-measuring this fix (not in the original two-fix brief, fixed anyway since
it is the identical root cause in the identical function):** once the max-HP fix landed, re-running the
probe immediately surfaced a **new** 31-mission blocker on `character ignore-AI flag (+0x2bc)`
(Sec3.4/Sec34.3) - the exact same conflation, for the exact same `loadZoneObjectsSection()` method, just
for `script_npc_flags`/ignore-AI instead of `script_npc_hp`/max-HP: the old code unconditionally called
`c.ignoreAI.forget()` for every `script_npc`-classified record, whether or not its `script_npc_flags`
property (present on 6,702 of 6,788 real `script_npc` objects, Sec11.7) contains the literal token
`ignore_ai` (true for only 425 of those 6,702). Fixed the same way: once the record is resolved, whether
it carries that token is a CONFIRMED, fully-decidable fact (Sec34.3), not an open question -
`loadZoneObjectsSection()` now tokenizes the real `script_npc_flags` string on whitespace and sets
`ignoreAI` to the real true/false answer, matching the real engine's own per-token flag-hash mechanism
(not a raw substring probe, which could in principle false-positive on an unrelated token).

## Mission-phase-8 fix (item 2 of the prior doc)

Per `spec-lua-api-behaviour.md` Sec21.27a: phase 8 is the FIRST phase a newly-bound mission enters, not a
steady/terminal value, auto-resolving to phase 1 within a few frames - well before any script code,
including a mission's own `_start`, is reachable. `EngineState::beginMissionPhaseTracking()` (new) flips
all three separately-scoped `!= 8` facades this project tracks
(`MissionContext::activeAndRunning`/`InProgressType::activeMissionPresent`/
`MissionExcludedRewardsState::activeAndRunning` - all three found via a project-wide grep for every
`OpenValue<bool>` citing `0x014c8460`/`0x014c7a14`) to the real "ready" value, called from
`tools/lua_host_run.cpp`'s per-mission loop immediately before its one `callMissionStart()` call - the
one hook this host has for "a mission's own `_start` is about to be invoked," which Sec21.27a's own table
confirms is always past the one-shot phase-8 window. The companion idle default (`false`, the real
engine's own `0x006d6010` start-up value - context null, phase 0) is now applied once at
`EngineState::EngineState()` construction, replacing "permanently unknown" with the real engine's two
actually-reachable states.

**Result: the literal "mission context active, phase != 8" blocker is gone from this run - 0 missions
now cite it.** 4 of the 12 previously-blocked missions (`m02`, `m18`, `m11`, `m10`) ran straight past it
into whatever their own script does next; `m24` (previously counted under the co-op-player item) also ran
past it. The other 8 (`dlc1_mm_01`-`dlc1_mm_05`, `mm_p_02`, `mm_p_04`, `mm_p_05`) now stop one field later,
on a genuinely different, DELIBERATELY NOT-FIXED field - see item 2 below.

## 1. `ai_add_enemy_target` actor gate, character descriptor byte `+0x33` bit `0x8` OPEN - Sec3.9 - 31 missions (NEW #1, by far the most common)

**Genuinely OPEN, not attempted by this task - do not treat as a quick follow-on of the two fixes above.**
Per Sec3.9: `ai_add_enemy_target`'s acting character must have bit `0x8` set in its own class-descriptor
byte `+0x33` (table `0x02cc9900`) or the call is a silent no-op; this project has never had real
population/table data for that descriptor table's own row values (unlike `script_npc_hp`/
`script_npc_flags`, for which Team A's Sec11.7 population scan gave real numbers this session could use) -
there is no equivalent census to close this one the same way. Closing it for real would need either that
table's own loader (disassembly - out of Team B's scope per this project's standing rule) or a further
Team A spec-sync pass giving real per-class values. **This is now the single highest-leverage remaining
item** - all 26 of the original max-HP-blocked missions carried straight through both fixes into this
same gate, joined by the 5 missions the phase-8 fix freed up (`m02`, `m18`, `m11`, `m10`, `m24`).

Missions: `dlc1_mm_06`, `dlc2_m02`, `dlc3_m02`, `dlc3_m03`, `dlc3_m01`, `m02`, `sh03`, `m08`, `mm_p_01`,
`m18`, `m07`, `m11`, `m24`, `m10`, `m23`, `m05`, `m13`, `m04`, `m12`, `sh02`, `m03`, `mm_p_03`, `m21`,
`sh04`, `m06`, `mm_m16_5`, `sh01`, `mm_p_07`, `m15`, `m09`, `m14` (31 of 49).

## 2. Current mission name OPEN (`0x014c8460`'s own target) - Sec1.5 - 8 missions (NEW #2)

**Deliberately not fixed this pass - a different, separately-scoped field from the phase-8 gates.**
`mission_end_failure`'s own identity gate (`src/lua_spec_confirmed_stubs_b07_sec8_9_teamA.cpp`) reads
`MissionContext::activeAndRunning` (now fixed, reads true) AND `MissionContext::currentMissionName`
(still genuinely OPEN - this task's brief scoped the fix to the three `!= 8` boolean facades only, not
this separate identity fact). The 8 missions below call `mission_end_failure`/a sibling very early in
their own `_start` and now stop here instead. A real fix would need `beginMissionPhaseTracking()` (or a
sibling) to also set `currentMissionName` to the mission actually being started - not attempted here,
flagged as the natural next step for whoever picks this up, same spirit as Sec21.27a's own fix.

Missions: `dlc1_mm_05`, `dlc1_mm_03`, `dlc1_mm_02`, `dlc1_mm_04`, `dlc1_mm_01`, `mm_p_04`, `mm_p_02`,
`mm_p_05` (8 of 49) - exactly the `dlc1_mm_*`/`mm_p_*` families minus the 4 that moved to item 1 above.

## 3. Second/co-op player non-null (`0x009df3d0`) OPEN - Sec28.12 - 2 missions

`dlc2_m01`, `dlc2_m03` - unchanged item from the prior doc, now 2 missions instead of 3 (`m24` moved to
item 1 above once both fixes let it run past max-HP/ignore-AI).

## 4-10. Single-mission items (one mission each, no shared blocker) - unchanged from the prior doc

- `m17`: notoriety spawn-group resolution `(0x008ed350)['stag_attack_heli']` OPEN - Sec13.2
- `m16`: `inv_item_in_slot` slot-name enumeration table `(0x008dc6b0)` OPEN - Sec13.21
- `m19`: weapon-definition name resolution `(0x028dc4dc, stride 0x7ec)['special-sonicgun']` OPEN -
  Sec4.10/Sec4.11
- `m20`: RC max signal range global float (start-up default not traced) OPEN - Sec39.2
- `m01`: local player exists `(0x009da4e0)` OPEN - Sec10.3
- `m22`: mission-object `+0x88` bit `0x4` ('is mission complete') `['m23']` OPEN - Sec27.2/Sec20.14
- `mm_m1_5`: `customization_screen_is_ready` backing flag OPEN - Sec60.1
- `mm_p_06`: `zscene_prep` handle class `(0x00dafb60)` OPEN - Sec26.25

## Summary for whoever triages next

Both items this task was asked to fix (max-HP formula, mission-phase-8) are closed for real, plus one
sibling defect (ignore-AI/`script_npc_flags`) sharing the identical root cause - all three were genuine
Team-B implementation-lag bugs (a real, confirmed zone-data fact misfiled as "unknown"), not missing spec.
`error=0` held throughout. The new #1 item (31/49, `ai_add_enemy_target`'s class-descriptor gate) is
*not* the same kind of bug - it is a genuinely-missing per-class-type table value with no real population
census behind it yet, same category as this project's other still-open `0x02cc9900`-table gaps elsewhere
in the spec. The new #2 item (8/49, current mission name) is a quick, well-understood follow-on to the
phase-8 fix this task deliberately left alone (out of this task's own stated scope - the brief named only
the three `!= 8` boolean facades). Recommended next step for whoever picks this up: relay the
class-descriptor-table-`0x02cc9900`-row-value question to Team A as a spec-sync question (Team B does not
disassemble), and pick up `currentMissionName` as a small, self-contained follow-on fix.
