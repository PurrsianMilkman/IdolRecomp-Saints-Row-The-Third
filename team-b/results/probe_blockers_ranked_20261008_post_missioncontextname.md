# Next shared blockers, ranked by mission count (2026-10-08, post `currentMissionName` identity-gate fix)

Direct follow-up to `results/probe_blockers_ranked_20261008_post_maxhp_phase8_fix.md` (does not supersede
it - kept as the prior baseline). Run against main tip `db93a71` (the `currentMissionName`/
`mission_end_failure` identity-gate fix - HANDOFF.md item 9: "fixed item-3's 9-mission blocker -
`bindMissionPhaseMachine()` set the phase-machine's own facade but never the SEPARATE
`currentMissionName`/`currentMissionEnding` fields 3 natives' gate reads"), the fix the prior doc's own
item 2 (8 missions blocked on "current mission name OPEN") flagged as its natural next step.

Real mission drive, `lua_host_run <real cache> tools/lua_all_registered_1490_tagged.txt <out>
--tick-budget=721 --probe-unresolved-names` (byte-identical invocation to the prior docs' own headers),
against the real archive cache. Raw output:
`results/verify_probe_blockers_ranked_20261008_post_missioncontextname/` (`verdict_mission_drive.tsv`/
`verdict_mission_drive_summary.txt`/`verdict_open_state.tsv`/`verdict_stub_hits_with_missions.tsv`).

**IMPORTANT - read before triaging from this doc alone:** two more fixes landed on main *after* this
exact raw data was captured but *before* this write-up (both verified directly against their own raw
data below, not trusted from a commit message alone):

- **`1e44506`** ("Default the generic/#PLAYER# chain's player-class-bit gate...", lua-api Sec3.3/Sec3.5/
  Sec4.13) **already fixes this doc's own item 1** (the 9-mission player-class-bit gate below) - its own
  commit message cites this exact raw data as its source. Re-verified independently here by re-tallying
  `results/verify_player_class_chain_gate_default_20261008/verdict_mission_drive.tsv`: zero "class-bit
  gate" citations remain anywhere in the 49-mission run, confirming the commit's own claim rather than
  repeating it. See "Already superseded" below for the full before/after and what moved where.
- **This task's own fix** (`zscene_prep`/the handle-class, re-queue-on-reset and nearest-world-object
  defaults, Sec26.25 - see this branch's own commit) targets item 6 below (`zscene_prep`, `mm_p_06`) -
  see that item's own entry for the investigation and the separately-measured before/after.

**Aggregate: `stop_reason_histogram: budget_exhaustion=7 error=0 completion=42`** (prior baseline:
`budget_exhaustion=4 error=0 completion=45`) - **`error=0` still holds**: zero missions terminate in an
unrecovered error state. 16 of the 49 probed missions hit no OPEN-state blocker at all this run: 9 run to
their own natural idempotent-completion point cleanly (`dlc1_mm_01`-`dlc1_mm_06`, `mm_p_02`, `mm_p_04`,
`mm_p_05` - exactly the `currentMissionName` fix's own 9 target missions, now DOUBLY clean: past both the
phase-8 gate and the identity gate with nothing else in their way) and 7 survive the full 721-tick budget
with no blocker at all, genuinely still running their own script logic (`dlc3_m01`, `m03`, `m11`,
`mm_p_01`, `mm_p_03`, `mm_p_07`, `sh03`).

## 1. Generic/`#PLAYER#` chain class-bit gate `(0x00a27e20)` OPEN - Sec3.3/Sec3.5 - 9 missions (highest in
   this snapshot - see "Already superseded" below, this is fixed on main as of `1e44506`)

Missions: `m02`, `m08`, `m10`, `m14`, `m15`, `m18`, `sh01`, `sh02`, `sh04` (9 of 49). Every one of these
previously failed on an unconditional `OpenStateError` the first time a NAMED (non-`#PLAYER#`-sentinel)
object touched this gate - `on_vehicle_destroyed`/`character_is_dead` were the only two call sites this
field's own doc comment named, but a repo-wide grep this project later ran (the `1e44506` fix) found at
least 10 more native stubs reaching the same gate.

## 2. Second/co-op player non-null `(0x009df3d0)` OPEN - Sec28.12 - 6 missions

Missions: `dlc2_m01`, `dlc2_m02`, `dlc2_m03`, `dlc3_m02`, `m12`, `m24` (6 of 49). Unchanged item from both
prior docs - still genuinely OPEN, no spec gives this field's value.

## 3. `0x00af22a0` enter-submitter result OPEN - spec-player-runtime.md Sec7.4 / spec-lua-api-behaviour.md
   Sec38.1 - 3 missions

Missions: `m05`, `m07`, `m09` (3 of 49) - unchanged.

## 4. Weapon-definition name resolution `(0x028dc4dc, stride 0x7ec)` OPEN - Sec4.10/Sec4.11 - 2 missions

`m13` (`'molotov'`), `m19` (`'special-sonicgun'`) - unchanged from both prior docs.

## 5. `release_to_world` resolved-object kind OPEN - Sec4.4 - 2 missions

`m04` (`'Starting_Vehicles'`), `m06` (`'Script_Vehicle 001'`) - unchanged.

## 6. `zscene_prep` handle class `(0x00dafb60)` OPEN - Sec26.25 - 1 mission (`mm_p_06`) - ORCHESTRATOR-
   FLAGGED, investigated and fixed this task (see below)

Unchanged single-mission item from both prior docs in THIS raw snapshot. **Not unchanged on main's current
tip**, however: `1e44506` (landed after this data, fixing item 1 above) has its own side effect here -
removing the player-class-bit gate's old masking throw let `mm_p_01` run deeper into its own script and
reach this SAME pre-existing gate too (previously `mm_p_01` just quietly exhausted its budget stuck at an
unrelated yield). So on main's actual tip *before* this task's own fix, this item is 2 missions
(`mm_p_01`, `mm_p_06`), not 1 - independently re-confirmed against
`results/verify_player_class_chain_gate_default_20261008/verdict_mission_drive.tsv` directly.

**Investigation (this task):** read `spec-lua-api-behaviour.md` Sec26.25 in full (the zscene lifecycle,
~250 lines, CONFIRMED - disassembly for nearly all of it) plus Sec2.7's handle-classifier mapping, and the
existing implementation (`include/sr3luahost/engine_state.h`, `src/lua_cutscene.cpp`,
`include/sr3tables_cutscene/scene_table.h`, `src/lua_spec_confirmed_stubs_b01_initial.cpp`). Found a real,
spec-confirmed (not invented) fix: Sec26.25's own Host summary gives the FULL, CONFIRMED consequence for
exactly this host's shape (no real streaming subsystem) in two passages - "a host whose streaming model
never marks the group resident leaves the scene at load state 1 forever... not a host bug to work around,
a real property of the mechanism a host must still satisfy" and "a refused handle start silently drops
the scene... silently forgotten" - and Sec2.7 independently confirms "null handle" as the handle
classifier's own first-checked, catch-all state (1). Implemented: `EngineState::zscenePromotionStep()`
now writes that real default (class 1, "not live") the first time a name is EVER promoted, narrowly
(never overriding an explicit test value or a post-teardown `.forget()`, which represents the SEPARATE,
still-genuinely-OPEN "what `0x00dafad0` leaves the flag word as" question Sec26.25's own review status
flags); `applySpecInitialState()` now also defaults the re-queue-on-reset byte (CONFIRMED permanently
false - "all seven of its call sites in the binary pass 0") and the nearest-scene-bearing-world-object
query (CONFIRMED "" - this host has no world-object manager at all, anywhere).

**Measured before/after (this task, fresh Release build, real cache, byte-identical invocation):** raw
output `results/verify_zscene_prep_handle_class_default_20261009/`. Both `mm_p_01` and `mm_p_06` now run
to `stop_reason=[budget exhaustion (721 ticks)]` with **no `first_error=` at all** - zero OPEN citations,
genuinely still running their own script logic for the full budget (`mm_p_01` parked at
`mm_p_01.lua:87`, `mm_p_06` at `mm_p_06.lua:95`, both inside their own `_start`, not a host refusal).
Aggregate moved from this doc's own "main's current tip" baseline
(`results/verify_player_class_chain_gate_default_20261008/verdict_mission_drive.tsv`:
`budget_exhaustion=8 error=0 completion=41`) to `budget_exhaustion=10 error=0 completion=39` - exactly
+2/-2, the two missions this fix targets and no others; `error=0` still holds. Independently re-tallied
from both raw TSVs directly (not from either doc's own prose) as part of this task's own verification
pass, not merely asserted.

**Verification note (this task):** this write-up's own primary cited raw directory,
`results/verify_probe_blockers_ranked_20261008_post_missioncontextname/`, does not exist anywhere in this
worktree or in this repo's git history on any branch (checked directly: `find`/`git log --all
--diff-filter=A` both come back empty) - it was apparently produced in a different, non-worktree checkout
session and never committed. Per this project's clean-room/verification discipline, this task did not
treat that gap as license to invent the missing numbers, nor did it reach across the worktree boundary to
read the other checkout's own working tree. Instead: every claim in this doc that cites a file this
worktree DOES have (`verify_player_class_chain_gate_default_20261008`, `verify_mission_context_name_20261008`,
both git-tracked) was independently re-tallied from the raw TSV directly and checks out exactly as
written, including the `mm_p_01`+`mm_p_06` 2-mission handle-class citation above; and this task's own
fix is measured fresh from this task's own real run, not inherited from the inaccessible snapshot. The
one thing this task genuinely cannot re-verify is the exact 49-mission aggregate/ranked list *of the
missing snapshot itself* (`budget_exhaustion=7 error=0 completion=42` and items 1-16 above) - flagged
here rather than silently trusted or silently discarded, per this project's own no-invented-fixes/
skip-and-revisit rule.

## 7-16. Single-mission items (one mission each, no shared blocker) - unchanged from the prior doc

- `m17`: notoriety spawn-group resolution `(0x008ed350)['stag_attack_heli']` OPEN - Sec13.2
- `m01`: local player exists `(0x009da4e0)` OPEN - Sec10.3
- `m16`: `inv_item_in_slot`'s own fixed slot-name enumeration table `0x008dc6b0` OPEN - Sec13.21
- `mm_m1_5`: `customization_screen_is_ready` backing flag OPEN - Sec60.1
- `m22`: mission object `+0x88` bit `0x4` ('is mission complete') `['m23']` OPEN - Sec27.2/Sec20.14
- `m21`: character attacker/threat reference OPEN - Sec3.4
- `mm_m16_5`: audio 'current conversation' lookup resolves `(0x006de8a0)` OPEN - Sec2.10
- `dlc3_m03`: `ai_scripted_action_is_processing`'s own gate `0x008addb0` OPEN - Sec13.7
- `m20`: RC max signal range global float (start-up default not traced) OPEN - Sec39.2
- `m23`: `0x012ec964` (continuous explosion global state) OPEN - Sec28.16

## Already superseded (fixed after this data, before this write-up) - item 1, `1e44506`

Re-verified directly (own re-tally of `results/verify_player_class_chain_gate_default_20261008/
verdict_mission_drive.tsv`, not just the commit's own prose): zero "class-bit gate" citations remain in
the 49-mission population after `1e44506`. Of the 9 missions this doc's own item 1 names: `m02`, `m08`,
`m10`, `m14`, `m15`, `m18`, `sh01`, `sh02`, `sh04` all moved past this gate into a LATER blocker or a clean
run -
- `m14`, `sh01`: now survive the full 721-tick budget with no further blocker at all (clean).
- `m10`, `mm_m16_5` (already in this doc's item 6-16): both now cite the SAME audio
  `'current conversation'` lookup (Sec2.10) instead - a 2-mission shared item on main's current tip.
- `m18`, `m16` (already in this doc's item 6-16): both now cite the SAME `inv_item_in_slot` table
  (Sec13.21) instead - a 2-mission shared item on main's current tip.
- `m15`: now cites `release_to_world` resolved-object kind `['group_extra_homies']` (Sec4.4) instead -
  joining this doc's item 5, making it 3 missions on main's current tip.
- `m02`, `m08`: now cite two NEW single-mission items (tutorial dispatcher byte `0x0151d5a6` Sec6.19;
  light-group name resolution `0x00739690` Sec17.24) not present in this doc's own snapshot at all.
- `sh02`: now cites audio persona line `0x0094c9e0`/`0x0046afd0` Sec8.4 (new single-mission item).
- `sh04`: now cites `continuous_spawn_start` arg 4 default Sec4.8 (new single-mission item).

So the REAL highest-leverage shared blocker on main's actual current tip (post-`1e44506`, pre-this-task's
own fix) is this doc's own item 2 (co-op player non-null, 6 missions) - not item 1, which is already
closed. This doc still reports item 1 as its own #1 because it is a faithful snapshot of the raw data it
was asked to summarize (captured before `1e44506`), not a claim about the current tip - see the flag at
the top of this file.

## Not re-measured this pass (out of scope, same limit as prior docs)

`dlc2_m02`'s own prior "group head name, zone-data gap" finding (2026-10-07) was not re-probed here -
`dlc2_m02` is hidden behind the higher-priority co-op-player-non-null item (2) before that deeper gap is
even reached. Deeper native-level grouping beyond exact `waiting_at` line matching not attempted.

## Summary for whoever triages next

Item 1 (9/49) in this raw snapshot is CLOSED on main's current tip (`1e44506`, independently re-verified,
not merely cited) - the real current #1 shared blocker is item 2 (co-op player non-null, 6/49), still
genuinely OPEN with no spec value. The orchestrator-flagged `zscene_prep` item (item 6, `mm_p_06`/
`mm_p_01` on current tip) got a real, spec-confirmed fix this task (Sec26.25's own Host summary text,
Sec2.7's classifier mapping) rather than an invented default - see this branch's own commit for the
measured before/after. Everything else is down to 1-3 missions each, same shape as the prior doc. The
`error=0` aggregate still holds across all three fix waves measured so far (resolver merge, max-HP/
phase-8, `currentMissionName`, player-class-bit gate, and now `zscene_prep`) - zero unrecovered Lua-side
crashes anywhere in the 1490-script population remains true at every measured point.
