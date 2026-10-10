# Next shared blockers, ranked by mission count (2026-10-07, post native-fix pass)

Direct follow-up to `results/probe_blockers_ranked_20261007.md` (does not supersede it - kept as the
pre-fix baseline). Run immediately after Team B's own native-implementation fixes for that doc's own
items **2** (`get_max_hit_points`'s random-per-spawn model, Sec7.12/Sec34.2/Sec60.4), **4**
(`continuous_explosion_start`'s definition-row resolution, Sec28.16), and **6** (`zscene_prep`'s real
cutscene-state guard address, Sec26.25/Sec60.4) - all three triaged by Team A as implementation-side,
no new spec work needed. That doc's own item **3** (`dlc2_m02`'s group-head-name blocker, Sec28.6) was
investigated and found to be a genuine zone-data gap, not something Team B's own code could fix - see
below.

Real mission drive, `lua_host_run <real cache> tools/lua_all_registered_1490_tagged.txt <out>
--tick-budget=721 --probe-unresolved-names` (byte-identical invocation to both prior docs' own
headers), against the real archive cache, run fresh from a from-scratch build of this worktree's tip
(`build_verify_native_impl`, Release; `ctest` 88/88 passing beforehand). Raw output:
`results/verify_probe_20261007_post_item2346_fixes/`.

Aggregate: `missions_start_after_ticks=still_suspended:21 finished:10 errored:15 killed:0` (of the 47/49
missions whose `_start` call suspended; **two** missions, `mm_p_01` and `mm_p_06`, now both fail at the
`_start` call itself - see item 1 below, up from one before this pass; one, `mm_m1_5`, finishes
synchronously inside `_start` with no suspend, unchanged).

## Confirmed fixed this pass (diff against `probe_blockers_ranked_20261007.md`)

- **Item 2 (`m18`, max hit points OPEN): GONE.** `get_max_hit_points`/`set_current_hit_points`
  (`EngineState::drawOrGetMaxHitPoints`, `src/lua_engine_state.cpp`) no longer refuse for an ordinary
  (non-script-NPC-bound) character. `m18` now runs the full 721-tick budget with no `first_error`,
  waiting at its own script line (`m18.lua:1593`, `m18_run_cargo_plane_open_cockpit`) exactly like any
  other `still_suspended` mission - the max-hit-points blocker itself is gone, not merely masked.
  `get_max_hit_points` fires 16,261 times this run (`verdict_stub_hits_with_missions.tsv`) - effectively
  unusable before this fix.
- **Item 4 (`m23`, continuous-explosion definition row `['Daedalus']` OPEN): GONE.** The real
  `continuous_explosions.xtbl` row for `'Daedalus'` now resolves
  (`EngineState::installContinuousExplosionTableFromCache`, real data installed from
  `patch_compressed.vpp_pc`: `rows=3`, all 3 real rows, matching spec-tables-weapons-combat.md
  Sec18.1/Sec18.5's own population census exactly). `m23` no longer errors on that resolver - it now
  errors one native-call step later, inside the SAME function, on a different, previously-masked OPEN
  item. See item 3 below (the next tier this fix exposed).
- **Item 6 (`mm_p_06`, `zscene_prep` -> cutscene state `0x0153b520` OPEN, `_start` itself failing):
  the real guard address.** `EngineState::zsceneCutsceneGuard` no longer throws on this global -
  `applySpecInitialState` now seeds it to `0` ("idle") at construction, per Sec60.4's 2026-10-07
  closure (zero-fill `.data`, no static initializer, same mechanism as `0x0153b530`/`0x0153b556`).
  `mm_p_06`'s `_start` call still fails - but now on a *different*, deeper OPEN item one step further
  into the same zscene lifecycle (see item 1 below). A **second** mission, `mm_p_01` (previously
  `still_suspended`, not failing at `_start` at all), now *also* reaches and fails at this exact same
  deeper point, once the cutscene-state fix let its own `_start`-time `zscene_prep` call run far enough
  to get there.

## Item 3 (`dlc2_m02`, group head name) - investigated, NOT fixed: a genuine zone-data gap

**Unchanged:** `engine state group +0x40 head name['grp_cspawn_pistol_01'] is OPEN (Sec28.6)`.
`group_get_first_npc`'s implementation (`src/lua_spec_confirmed_stubs.cpp`) was checked line-by-line
against Sec28.6 (which the spec itself marks CONFIRMED and cleared for implementation) - it matches
exactly: resolves the group name via the shared singleton, then reads the group's own `+0x40`
member-list head and pushes that member's name from its own `+0x18` field, returning 0 on failure. The
code side is correct and complete. The blocker is that **nothing in this project's scope ever builds a
script group's member list in the first place**: per Sec28.5/Sec18.14, the only two things that ever
read the `+0x40` head / `+0x9c` next-pointer list are `group_get_first_npc` and `group_get_next_npc`
themselves (confirmed directly: no writer exists anywhere in this codebase for
`groupFirstNpcName_`/`groupNextNpcName_` outside those two readers), and no Lua-visible native anywhere
in the spec ever adds an NPC to a named script group. Membership is therefore assigned entirely by
engine/zone data - almost certainly a placed NPC's own world-editor record tagging it into a named
group, the same category of gap this spec already accepts for Killbane's named-object resolution and
the script-NPC `script_npc_hp`/`ignore_ai` overrides (Sec29.4/Sec34.4), both explicitly parked behind
the `.czn_pc` object-placement interior. **What's missing, precisely:** which NPC(s), if any, are placed
as members of the script group `grp_cspawn_pistol_01` for mission `dlc2_m02` - that is zone-placement
data this project does not have and has no code path to derive. Not faked.

Note for Team A, relayed via the orchestrator: the project memory notes the standing `.czn_pc` hold was
lifted directly by the user on 2026-10-07 and Team A is re-deriving the zone interior in stages now -
but nothing in this worktree's own `spec-lua-api-behaviour.md` sync reflects that yet (§29.4/§34.4 still
read as PARKED as of this probe run), so this item correctly stays OPEN here. It may well close from
your side without any further action needed on Team B's own part - flagging so it isn't duplicated.

## 1. `_start` call itself fails: `zscene_prep` -> selected request-group handle class (`0x00dafb60`) of scene entry `['02_z01']` OPEN - Sec26.25 - 2 missions (`mm_p_01`, `mm_p_06`)

The successor to the OLD item 6 (`0x0153b520` OPEN), now closed - see above. Both missions' `_start`
calls invoke `zscene_prep` on the SAME scene entry, `'02_z01'` (almost certainly each mission's own
intro cutscene/zscene), and both now reach the teardown's handle-class check for that entry's
*previously*-current scene before anything else can run - `EngineState::zsceneHandleClass()` has never
been given a value for that key, so the teardown genuinely cannot proceed without knowing whether the
prior handle is live, not-live, or failed (`0x00dafb60`'s own three-way classification, Sec26.25
lifecycle step 2 "Handle not live"/"Handle live"). `zscene_prep:OPEN_STATE` fires exactly 2 times this
run (once per mission, `verdict_stub_hits_with_missions.tsv`) - distinct from `zscene_prep` itself (6
calls, the missions that get further before hitting this). Per this project's own `OpenValue` discipline
this is correctly refused rather than guessed (whether a handle is resident/failed/loading is real,
per-run streaming state no spec value could substitute for) - flagged for Team A/orchestrator awareness
as the next real blocker in this specific lifecycle, not something Team B invents a value for.

## 2. Character state enum `(+0x16d4)` OPEN - Sec3.4/Sec3.10 - 8 missions, 8 real calls

- **Missions (errored - `first_error` is the terminal state, thread never resumed again):** `dlc2_m01`,
  `dlc3_m02`, `dlc3_m03`, `sh03`, `m07`, `m05`, `m04`, `m21` - all `engine state character state enum
  (+0x16d4) is OPEN (spec-lua-api-behaviour.md Sec3.4/Sec3.10)`.
- Up from 5 missions in the 2026-10-07 baseline doc (`dlc2_m01`, `dlc3_m03`, `m07`, `m05`, `m21`) - three
  more missions (`dlc3_m02`, `sh03`, `m04`) now run far enough (past the fixes this pass made) to reach
  this same, already-known blocker. Real call count is low (8) because the terminal error fires on the
  FIRST touch per mission; the much larger "broad reach" figure the baseline doc already measured
  (41,176 real calls from `character_is_in_vehicle` and the already-implemented group-liveness/
  action-override checks that share this same field) is unaffected by this pass and not re-measured
  here. Per the baseline doc's own note, a Team A agent was already working this field (Sec3.10,
  `character_is_in_vehicle`'s vehicle vtable `+0x70` accessor) as of 2026-10-07 - `character_is_in_vehicle`
  itself now fires 3 times this run with no error (its own gate elsewhere already resolves), consistent
  with partial progress on that side; the stateEnum field itself is still OPEN for these 8 missions'
  own direct reads.

## 3. Continuous-explosion global running state `0x012ec964` OPEN - Sec28.16/Sec22.24 - 1 mission (`m23`)

**New since the item-4 fix above - this is the next, previously-masked blocker in the exact same
native.** `continuous_explosion_start` (Sec28.16) now resolves both its arguments (the `'Daedalus'`
definition row and the target) for real, then checks whether a continuous explosion is "already active"
before starting one - that check reads the single global running flag `0x012ec964`
(`EngineState::continuousExplosion().active`), which has never been given a start-up value. Sec28.16/
Sec22.24 together CONFIRM the field's *role* (cleared by `continuous_explosion_stop`, set by the start
routine, reconciled as one shared running flag) but **not** its boot-time default - unlike item 6's
`0x0153b520`, no section states outright that this global is zero-fill `.data` with no static
initializer, so Team B is not assuming it here (the same no-invented-fixes discipline this pass already
applied to item 1 above). Flagged for Team A: if `0x012ec964` is confirmed zero-fill at load (plausible
by the same pattern as every other never-explicitly-initialized byte this spec has resolved, but not
yet stated), this would be a second same-shape, low-effort closure.

## 4. `character_playing_combat_move` predicate `(0x009634f0)['#PLAYER1#']` OPEN - Sec25.27 - 1 mission (`m16`)

New this run (not present in the 2026-10-07 baseline - `m16` did not reach this point before). Already
flagged OPEN in this project's own source comments (`src/lua_spec_confirmed_stubs.cpp`: "not decompiled -
OPEN until a test seeds it").

## 5. Audio persona line currently playing `(0x0094c9e0/0x0046afd0)['#PLAYER1#']` OPEN - Sec8.4 - 1 mission (`sh02`)

New this run.

## 6. Game clock hour byte `(0x014ff33c)` OPEN - Sec32.1/Sec48.2/Sec48.4 - 1 mission (`m19`)

Same single-mission item as both prior reports. Expected, not new: Sec48.4's own documented
forward-delta-then-forget behaviour after a SECOND `set_time_of_day` call (the clock fix already closes
the first call; `m19` calls it a second time).

## 7. Character world position (`get_dist`'s own `0x00a455a0` lookup) OPEN - Sec4.2 - 1 mission (`m20`)

New this run.

## 8. Character alive/not-gone predicate `(0x0096f4f0)` OPEN - Sec30.5 - 1 mission (`m01`)

New this run.

## Remaining still_suspended missions with no shared blocker (21 of 21), each at its own distinct script line

`dlc2_m03`, `dlc3_m01`, `m02`, `m08`, `m17`, `m18`, `m11`, `m24`, `m10`, `m13`, `m12`, `m03`, `mm_p_03`,
`sh04`, `m06`, `sh01`, `m22`, `mm_p_07`, `m15`, `m09`, `m14` - no two share an exact `waiting_at`
script:line this run (verified directly against `verdict_mission_drive.tsv`, not assumed). `m18` is new
to this list this pass (it used to error on the now-fixed max-hit-points item; it now runs the full
budget and waits at its own line like the rest of this group) - a genuine improvement, not a new
blocker. No cross-mission overlap by location was found; a deeper native-level grouping (shared helper
calls inside `game_lib.lua`, not just identical yield lines) was not attempted this pass, same scope
limit as the baseline doc.
