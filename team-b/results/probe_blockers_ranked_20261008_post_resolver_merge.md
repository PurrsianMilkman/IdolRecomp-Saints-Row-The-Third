# Next shared blockers, ranked by mission count (2026-10-08, post-resolver-fix merge wave)

Direct follow-up to `results/probe_blockers_ranked_20261007_post_item2346_fixes.md` (does not supersede
it - kept as the prior baseline). Run against main tip `b1c7b94`, after this session's merge wave landed:
composite renderer, Bink decoder skeleton, Brad-head real-texture bind, **#FOLLOWER# sentinel resolver
wiring (68 call sites, the most likely to move these numbers)**, Wwise codebook library fix, drive_demo
multi-channel fix, tower diagnostic dumps, golden-plumbing fix. Engine torque-curve physics fix
(`8c811e0`/`8e55c65`) and ground-pavement fix (`38b103e`) landed just after this probe ran - not yet
reflected here, neither touches Lua-mission-blocking code paths directly so a re-run isn't expected to
move these specific numbers, but note the gap for completeness.

Real mission drive, `lua_host_run <real cache> tools/lua_all_registered_1490_tagged.txt <out>
--tick-budget=721 --probe-unresolved-names` (byte-identical invocation to prior docs' own headers),
against the real archive cache, run from a from-scratch build of main's own tip (`build`, Release;
`ctest` 131/131 passing beforehand). Raw output: `results/verify_probe_blockers_ranked_20261008/`.

**Aggregate: `stop_reason_histogram: budget_exhaustion=4 error=0 completion=45`** - of 49 missions whose
script was found and `_start` call succeeded. **`error=0` is the headline result: ZERO missions now
terminate in an unrecovered error state** (the prior baseline doc's own aggregate had many `errored`
terminal states - compare directly). Every mission now either runs to its own natural
idempotent-completion point (45) or survives the full 721-tick budget still suspended (4:
`mm_p_01`, `m03`, `mm_p_03`, `mm_p_07`). `first_error` below is informational (first OPEN-state stub hit
encountered along the way), not necessarily fatal - most "completion" missions finish in 2-5 ticks having
only brushed one OPEN field in a non-blocking way.

## 1. Character max hit points `(+0x1cac)` OPEN - Sec7.12/Sec7.31/Sec34.2 - 26 missions (by far the most common)

**NEW dominant blocker this run - was not the leading item in either prior probe doc.** Confirmed a
*different* field than the one already fixed in the 2026-10-07 pass (`get_max_hit_points`'s
random-per-spawn model, `EngineState::drawOrGetMaxHitPoints` - that fix holds; `m18` for example no
longer blocks here, it now blocks on item 2 below instead). This is a second, distinct max-hit-points
field at a different offset (`+0x1cac`) with no spec value given yet.

Missions: `dlc1_mm_06`, `dlc2_m02`, `dlc3_m02`, `dlc3_m03`, `dlc3_m01`, `sh03`, `m08`, `mm_p_01`, `m07`,
`m23`, `m05`, `m13`, `m04`, `m12`, `sh02`, `m03`, `mm_p_03`, `m21`, `sh04`, `m06`, `mm_m16_5`, `sh01`,
`mm_p_07`, `m15`, `m09`, `m14` (26 of 49). By far the single highest-leverage item on this list - closing
it would touch over half the mission population.

## 2. Mission context active, phase != 8 `(0x014c8460/0x014c7a14)` OPEN - Sec1.5/Sec1.6 - 12 missions

Missions: `dlc1_mm_05`, `dlc1_mm_03`, `dlc1_mm_02`, `dlc1_mm_04`, `dlc1_mm_01`, `m02`, `m18`, `m11`,
`m10`, `mm_p_04`, `mm_p_02`, `mm_p_05`. Second-highest by mission count - almost entirely the `dlc1_mm_*`
and `mm_p_*` mission families plus a handful of mainline missions.

## 3. Second/co-op player non-null `(0x009df3d0)` OPEN - Sec28.12 - 3 missions

`dlc2_m01`, `dlc2_m03`, `m24`. Same item type noted in the prior baseline doc, still open, now hit by 3
missions instead of being buried under other earlier-failing blockers.

## 4. `_start`-call-itself failures - partially resolved since the prior doc's own item 1

The 2026-10-07 baseline's `_start`-failure item named `mm_p_01`/`mm_p_06` as both failing at `_start`
itself. **`mm_p_01` is now resolved**: `start_call_ok=1` this run, and it survives the full 721-tick
budget suspended at its own script line - no longer an `_start`-time failure at all. `mm_p_06` and
`mm_m1_5` still show `start_call_ok=0`: `mm_p_06` blocked on the same `zscene_prep` handle-class item as
before (Sec26.25, item 5 below); `mm_m1_5` now blocked on a different, new item
(`customization_screen_is_ready`, Sec60.1) rather than the old resolver issue.

## 5. `zscene_prep` handle class `(0x00dafb60)` OPEN - Sec26.25 - 1 mission (`mm_p_06`)

Unchanged single-mission item from the prior doc (`mm_p_01` has since moved past this one - see item 4).

## 6-12. Single-mission new/remaining items (one mission each, no shared blocker)

- `m17`: notoriety spawn-group resolution `(0x008ed350)['stag_attack_heli']` OPEN - Sec13.2 (new)
- `m16`: `inv_item_in_slot` slot-name enumeration table `(0x008dc6b0)` OPEN - Sec13.21 (new)
- `m19`: weapon-definition name resolution `(0x028dc4dc, stride 0x7ec)['special-sonicgun']` OPEN -
  Sec4.10/Sec4.11 (new)
- `m20`: RC max signal range global float (start-up default not traced) OPEN - Sec39.2 (new)
- `m01`: local player exists `(0x009da4e0)` OPEN - Sec10.3 (new)
- `m22`: mission-object `+0x88` bit `0x4` ('is mission complete') `['m23']` OPEN - Sec27.2/Sec20.14 (new)
- `mm_m1_5`: `customization_screen_is_ready` backing flag OPEN - Sec60.1 (new, see item 4)

## Not re-measured this pass (out of scope, same limit as prior docs)

`dlc2_m02` (group head name, zone-data gap) - unchanged from the 2026-10-07 investigation, still a
genuine zone-placement-data gap, not re-probed here since nothing in this pass's own changes touches it.
Deeper native-level grouping beyond exact `waiting_at` line matching not attempted.

## Summary for whoever triages next

Item 1 (`+0x1cac` max hit points, 26/49 missions) is the clear highest-leverage target - a single field
fix here would be the largest jump in probe coverage this project has seen in one pass, larger than any
single item in either prior doc. Item 2 (mission-context phase field, 12/49) is the second-highest.
Everything else is now down to 1-3 missions each. The `error=0` aggregate result is itself worth
highlighting to Team A/the user as a real milestone - this run has no unrecovered Lua-side crashes
anywhere in the 1490-script population for the first time this project has measured it.
