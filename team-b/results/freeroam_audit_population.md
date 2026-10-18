# Free-roam population audit (spec-ai-runtime.md 20.6, ce704b4)

Status after branch feature/freeroam-population. "I" = implemented, "P" = partial/stand-in, "M" = missing.
Code: include/sr3airuntime/freeroam_population.h (pure rules, tested by tests/freeroam_population_test.cpp), wiring in tools/sr3_viewer.cpp (runPlaytest).

| # | Rule (spec ref) | Status | Where |
|---|---|---|---|
| 1 | TOD density index 01:00/07:15/12:00/17:45 (20.6.0a) | I | TodDensityIndex, wired via todFraction |
| 2 | Category day/night blend {night, avg, day, day} (20.6.0a) | I | TodBlend |
| 3 | Notoriety scale 1,1,.75,.5,.25,.1 (20.6.0c) | I (rule) / P (viewer passes level 0; no notoriety state read) | NotorietyDensityScale |
| 4 | Weather PedDensity (20.6.0d) | I (rule) / P (viewer hardcodes Light Rain 0.5 = playtest stage) | PedTargetLiveCount |
| 5 | Hood -> category (20.6.0e / 20.6.8) | M (CHOSEN stand-in: Default Spawn .8/.5, .8/.4) | DefaultSpawnCategory; needs hood BSP from fawld.czn_pc |
| 6 | Spawn-distance FOV scale (20.6.0f) | M (scale fixed 1.0) | - |
| 7 | Vehicle target = max(1, trunc(12|18 * min(d*notor, capV))) (20.6.1a) | I | VehicleTargetLiveCount; spawn gated live < target |
| 8 | Co-op split, activity-mode overrides, S scale (20.6.1a) | M | - |
| 9 | One spawn per pass, 200-300 ms timer, 3 attempts (20.6.1b) | I (existing NearFieldVehicleDriver/SelectNearFieldVehicleSpawn 3 slots) | near_field_vehicle_population.h |
| 10 | Chase 4000-6000 ms timer, 32-candidate list (20.6.1b/c) | M (3 candidates, not 32) | - |
| 11 | 130 m ring crossing, 65 m hidden fallback (20.6.1c) | P (rule I; synthetic radial chords, no road graph) | ChordCircleCrossing, GatherNearFieldSpawnCandidatesAroundPlayer |
| 12 | Moving cone minDot (20.6.1c) | I | MovingConeMinDot; camera velocity from frame delta |
| 13 | Closing test, segment capacity 3/5, highway rule, masks, obstruction, zone-loaded, co-op partner (20.6.1c) | M (need 13.2a road graph) | - |
| 14 | Vehicle distance despawn 140/65/90/250, +60 driving, x scale (20.6.1d) | I (visibility treated false; mode-2/special flags unwired) | VehicleDespawnDistance; viewer erase pass |
| 15 | Fade 500 ms (20.6.1d) | M | - |
| 16 | Vehicle model choice (20.6.1e) | M | - |
| 17 | Ped target = min(base, max(1, trunc(40*s))) with weather (20.6.2a) | I (+21 bit / co-op OPEN) | PedTargetLiveCount; passed as localCap |
| 18 | Bulk fill n = trunc(.33*min(target-live, headroom)) (20.6.2b) | I (rule only, not wired: viewer has no populate-request path) | PedBulkFillCount |
| 19 | Regular maintenance strategies (20.6.2b) | M (placement geometry OPEN in spec) | - |
| 20 | Candidate edges within 70 m, 100 max, rebuild 500 ms/30 m (20.6.2c) | P (3 synthetic edges within 10-70 m, rebuilt every tick) | GatherNearFieldPedSpawnCandidateSegmentsAroundPlayer |
| 21 | Lateral offset +-width/2 (20.6.2c) | I (3 m walkway stand-in width) | PedLateralOffsetFromRand |
| 22 | Rejects: far 90 / near 10 / visible<70 (20.6.2c 1-3) | I (visibility = ApproxVisibleToCamera cone, CHOSEN, not frustum) | EvaluatePedSpawnReject; near const fixed to 10 in near_field_ped_population.h; far gate 90 in viewer |
| 23 | Crowd reject >= 7 peds within 20 m (20.6.2c 5) | I | viewer gate (isGroundValid hook) + EvaluatePedSpawnReject |
| 24 | Resource gate, zone loaded, ground ray, clearance box, nav query (20.6.2c 4,6-9) | M | gates left at defaults |
| 25 | Ped despawn 90 m horizontal (20.6.2d) | I (50 m behind in mode 2, 8 m height rule, eligibility preds, fade: M) | ShouldDespawnPedByDistance; viewer erase pass + side-map cleanup |
| 26 | Ped character choice (20.6.2e) | M | - |
| 27 | Lane follower, car following, lane change (20.6.3, 20.6.7) | M (not in scope here) | traffic_lane_follower.h unchanged |

Counts: implemented 14 (1,2,7,9,12,14,17,21,22,23,25 plus rule-level 3,4,18), partial 4 (3/4 viewer inputs, 11, 20), missing/stand-in 10+ (5,6,8,10,13,15,16,19,24,26,27).
Real ring/edge placement needs the 13.2a zone-data road and sidewalk graph, not loaded (not cheap: per-tile streamed graph); synthetic lane/sidewalk remains a labelled stand-in.
