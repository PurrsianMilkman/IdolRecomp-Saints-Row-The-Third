# Free-roam ground height investigation (2026-10-18, TEAM B)

Branch `fix/freeroam-ground-height` from main 213e0d9. Diagnosis only; NO behaviour change (see "What changed").
Tools used (both read-only/opt-in): `tools/probes/ground_height_probe.cpp` (new, target `ground_height_probe`) and the
`SR3_TRACE_PLAYER_Y=1` env diagnostic in `tools/sr3_viewer/viewer_playtest_frame_player.cpp`.
Sources of engine facts: spec-zone-data-format.md 13.2a (road graph, absolute world metres, Y = height) and 13.5 (0x2244).

## Q1. Player Y over time, ground source (default headless free roam, spawn (-1280,-560), tile 0915)

`sr3_viewer playtest ... --headless --no-input --frames 600`, trace every 30 frames:

    frame   0 pos y  0.00  rawGround=-5.00 source=heightmap
    frame  60 pos y -0.65  ...
    frame 150 pos y -1.63
    frame 300 pos y -3.27
    frame 450 pos y -4.90
    frame 480..600 pos y -5.00 (settled)

- The player is warped to `finalGroundY = 0.00`, then eases DOWN at ~0.33 m / 30 frames to the heightmap value -5.00 and rests there.
  Ground source is the Heightmap (0x2244) link of the chain for 100% of frames; the 0x2237 mesh never hits
  ("groundmesh: NO real 0x2237 collision-mesh triangle resolved at spawn").
- City tiles ARE loaded in headless mode (same code path as live; the streaming/ground-tile load is not gated on --headless),
  but tile 0915's `~s0915` ground mesh is 26 triangles, collision bounds x[-22.3,22.3] z[-10.4,13.0], world-Y [-0.21,0.21]:
  that is the known "near-origin / local-space" content (spec-zone-data-format.md 13.2 ground-mesh-origin paragraphs), not
  geometry under the spawn (-1280,-560). So `finalGroundY` = 0.00 comes from the rule "bounds within 3 m of 0 keep y=0"
  (viewer_playtest_setup_world.cpp ~line 543) applied to that junk. Live (non-headless) mode: code paths are identical
  (headless only hides the window), so live shows the same.
- The same tile area is "void" in the live report; this is the long-open ground-missing bug (13.2/13.5 notes), not new.

## Q2. Spawn Y vs nearest road/sidewalk node Y (ground_height_probe)

Nearest nodes to (-1280,-560): sidewalk (-1272.0, **32.15**, -584.4) 25.7 m; road (-1266.9, 32.13, -584.4) 27.7 m;
sidewalk (-1311.1, 28.75, -544.6) 34.7 m. Within 100 m: n=136, Y min 24.11 / median 31.60 / max 36.15.
Within 300 m: n=1182, Y 6.13 / 19.10 / 40.62. The spawn Y (0.00) is inconsistent with the road graph by ~32 m; the player
sinks to -5, i.e. ~37 m below the road. Ambient actors (node Y 18-38) are therefore 32 m above the player, as observed.
actor-placement-trace in the 300-frame run: actors=26, mean|dy|=31.8, max 36.5 vs player ground.

## Q3. What the heightmap read decodes, vs node Y

Decode per spec 13.5 (what the code does): origin i32 x/z (= tile centre - (160,140), verified: tile 0915 origin (-1440,-700)),
u16/i16 samples row-major, 32x28 @ 10 m, value read raw (spec: "no scale or bias", metres). Units/offset/scale are as spec'd.
Code check: src/zone_heightmap.cpp matches the spec (col=(x-ox)/10, row=(z-oz)/10, int16 raw).

THE LOADER PICKS THE WRONG-LOOKING RECORD. `loadCityHeightmapTile()` (viewer_playtest.cpp ~155) tries the `~f<CCRR>` "base"
file FIRST and returns its first 0x2244 record. Both `~f` and `~s` files of a tile carry a valid 0x2244 record (same origin,
32x28). Measured over the 31 tiles of sr3_city_0 that contain road nodes:

- `~f` records: almost entirely the value -5 (e.g. tile 0915: 884/896 cells = -5, max 40; tiles 0814/0815/0816/0914/1015/1017
  are 896/896 = -5). Where non--5 cells exist they are 0..19 (sparse). Against 17,800 road+sidewalk nodes (tiles with a record: 4,989):
  4,836 read exactly -5, 153 read non-flat and those match node Y to within +1.9/-33 (median -0.3) - i.e. only sea-level cells.
  Spec 13.5: -5 is also the out-of-bounds sentinel. So the `~f` layer is effectively "no height" over land.
- `~s` records: land-shaped data. Tile 0915: raw min 13, max 77, zero -5 cells; land tiles 0814-0817, 0913-0917, 1015, 1017
  have 0 cells of -5; sea-edge tiles are mostly -5 (0616: 855/896). So `~s` is the layer that looks like terrain/land.

20 sample points (spawn region, 450 m ring, every k-th node; node Y vs heightmap ~f vs ~s, metres):

    sidewalk -1272.0 -584.4  nodeY 32.15 | ~f -5 (dY -37.2) | ~s 60 (dY +27.8)
    road     -1282.8 -478.6        36.06 | -5 (-41.1)       | 49 (+12.9)
    road     -1318.6 -681.3        23.82 | -5 (-28.8)       | 26 (+2.2)
    road     -1175.4 -444.0        36.53 | -5               | 66 (+29.5)
    road     -1383.0 -417.7        22.33 | -5               | 30 (+7.7)
    road     -1186.9 -390.3        32.13 | -5               | 41 (+8.9)
    sidewalk -1417.4 -399.7        24.40 | -5               | 50 (+25.6)
    sidewalk -1087.0 -431.8        36.26 | -5               | 47 (+10.7)
    road     -1044.7 -644.7        13.33 | -5               | 23 (+9.7)
    sidewalk -1392.8 -795.8         6.15 | -5               | 33 (+26.8)
    road     -1008.7 -523.9        35.14 | -5               | 43 (+7.9)
    sidewalk -1425.0 -309.4        31.39 | -5               | 83 (+51.6)
    road     -1581.9 -590.1         8.85 | -5               | 39 (+30.1)
    sidewalk -1261.8 -873.8        11.15 | -5               | 21 (+9.9)
    sidewalk -1351.5 -879.6         8.60 | -5               | 20 (+11.4)
    road     -1278.5 -898.2        11.10 | -5               | 21 (+9.9)
    sidewalk  -982.0 -743.5        18.15 | -5               | 27 (+8.9)
    sidewalk -1447.5 -236.8        40.15 | -5               | 52 (+11.8)
    road     -1519.1 -849.7         8.13 | -5               | 48 (+39.9)
    road     -1415.5 -195.0        40.13 | -5               | 57 (+16.9)
    (+4 more rows in the probe output; all ~f = -5)

Distributions (nodes minus heightmap; all nodes in sr3_city_0 tiles with a record):

- `~f`: n=153 non-sentinel, hm-node median -0.3, p90 +0.9; 4,836 exactly -5.
- `~s`: n=4,940 non-sentinel, hm-node min -4.0, p10 +8.7, median +17.7, p90 +31.9, max +133.9; 99.2% of nodes have hm >= node Y;
  corr(nodeY,hm)=0.59, slope 0.90, intercept +22.3. Lower envelope: min of `~s` over +/-20 m minus node Y: median +7.9 (p10 +0.3);
  over +/-40 m: median +4.8, p25 -0.1.
  => `~s` is metres-consistent in scale (slope ~0.9, no x2/x0.5 factor) but sits ABOVE the street surface almost everywhere:
  it behaves like a top-surface field (terrain plus buildings/rooftops) rather than walking-ground height. Using it as player ground
  would put the player ~8-18 m above the street (probably on rooftops). Neither layer, as the current code reads them, is "road-level ground".

## Root cause (best supported)

1. Immediate cause of the -5 sink: heightmap loader takes the `~f` record, which is -5 over land; the sampler treats -5 as a valid height.
2. Underlying cause of "ground wrong": no spec-backed source of walkable ground exists at tile 0915: the 0x2237 mesh for `~s0915` is only
   near-origin local-space geometry (known 13.2 open item), 0x2244 `~s` is a top-surface-like field above the street, `~f` is blank.
   The only spec-backed per-position height near the spawn that agrees with street level is the road/sidewalk graph node Y
   (13.2a: "median 0.07 m from the nearest placed object").
3. Actors use node Y (they look right relative to the road graph) while the player uses the broken chain, hence the 32 m gap.

## What changed (and why no fix)

No behaviour change. Reasons: (a) which 0x2244 family the engine reads is not stated by spec 13.5 (it says 1,194 records = 597 fine +
597 `~al` coarse; it does not say `~f` vs `~s`), (b) neither candidate gives street height, (c) a node-Y ground fallback is a CHOSEN design
decision, not a spec fact. Added only: `SR3_TRACE_PLAYER_Y=1` opt-in printf (viewer_playtest_frame_player.cpp, after the
`setGroundY` call) and `tools/probes/ground_height_probe.cpp` + its CMake target.

Candidate CHOSEN stand-ins if Team A/orchestrator wants a fix (not implemented): (1) treat a heightmap value of exactly -5 as "no data"
(spec 13.5 itself flags the collision with the OOB sentinel) so the player no longer sinks; (2) as the player's ground reference use the nearest
road/sidewalk node Y within ~40 m (label CHOSEN), falling back to the mesh chain; (3) fix `finalGroundY` measurement to ignore ground tiles whose
collision bounds do not contain the spawn x/z.

## Questions for Team A (spec sync)

1. 0x2244 in `~f` vs `~s` zone files: which is the registered/queried heightmap for an ordinary tile (both exist with identical origin/grid)? Does a
   later registration overwrite, or does the first one win (13.5 "already registered fast-path")?
2. What does the `~s` field represent, given it is ~8-18 m above street-node Y (99.2% >= node Y) and metres-scaled? Terrain, or terrain+structures?
   Is the player's ground query (the consumer chain 0x00b41670 / 0x00b41530) post-processed with something else (e.g. min over a window, or a physics raycast preferred)?
3. For `~s0915` the 0x2237 mesh is local-space/near-origin only. Where does retail get street collision for that tile (still the open 13.2 item)?
4. Is the 0x2244 -5 value "no data/sea" or "out of bounds"? (13.5 leaves this OPEN; it determines whether -5 should ever be a ground height.)

## Follow-up: CHOSEN stand-in implemented (2026-10-18, rebased onto 28b2e2a, no conflicts)

All CHOSEN, labelled in code with a pointer to this file; `--no-node-ground` restores the old behaviour exactly.
1. Heightmap value -5 = "no data" (`sampleRealGroundHeight`, viewer_playtest_setup_world.cpp).
2. `finalGroundY` is measured only from ground renderers whose own XZ bounds contain the spawn; if none, it starts at the nearest road/sidewalk node Y (<= 40 m).
3. `sampleGroundHeight` chain: real 0x2237 mesh hit -> heightmap with data -> nearest road/sidewalk node Y within 40 m (nearest node, overpass tie-break by Y closeness to the reference; no edge interpolation) -> old fallback.
4. Pure parts in `include/sr3player/node_ground.h`, unit test `tests/synthetic_node_ground_test.cpp` (`synthetic_node_ground_test`).
Test audit: real_playtest_smoke / streaming_longwalk assert no ground values; ground_pavement_collision pins player/vehicle Y in [15,35] at spawn (-1085,-820) where the real mesh hits, so it does not encode the -5/0 bug.

Evidence (headless default free roam, 1800 frames, SR3_TRACE_PLAYER_Y=1):
- before: Y 0.00 -> sinks to -5.00 by frame 480; actors vs player |dy| mean 31.8 m, max 36.5 m.
- after: spawn groundY 32.15 (node Y); player Y tracks nearest node Y (frame 120: 32.05 vs 32.05; frame 300 onward 36.15 vs 36.15, no sinking); actors |dy| mean 3.5 m, max 11.1 m; max sample-vs-node 41.5 m -> 9.5 m.
- Missions (60 s timeout, same flags as sweep_combat300.sh): m01 and m08 exit 0 before and after; both still stop by wall-clock timeout while suspended in both builds. m01 identical (player teleports to (2257.9,180.5,2694.1), counters equal). m08: final player Y -5.00 -> 36.15; overtake_gate_fire_count 147 = 147, signal switches 85 -> 80, ped crossings 17 = 17, path_searches 39 -> 51, arrivals 13 -> 18 (live-time dependent counters; no mission progression change seen).
