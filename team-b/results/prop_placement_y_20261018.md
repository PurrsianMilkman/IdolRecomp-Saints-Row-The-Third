# Prop placement Y investigation (2026-10-18, TEAM B)

Branch `fix/prop-placement-y` from main 176363f. Diagnosis only: NO behaviour change. Added one read-only probe
(`tools/probes/prop_placement_probe.cpp`, target `prop_placement_probe`); raw output `results/prop_placement_probe_20261018.txt`.

## Q1. Where do prop transforms come from today?
`viewer_playtest_setup_world.cpp` `sess.loadCell` (~line 392-412) (and the same shape in viewer_city.cpp / viewer_drive.cpp):
every `.clmesh_pc` in a fine cell's `<CCRR>hN.str2_pc` is laid on a regular cols x rows grid across the 160 x 140 m cell:
`buildWorldMatrix(sx - lcx, 0.0f, sz - lcz, 0, 1, p.world)`. Y is the literal 0.0 (the prop's own local Y is kept, only
centred in x/z). So the "grid at y = 0" is a CHOSEN stand-in. No real transform is read at all, so no per-tile offset is
"missing": there is nothing to offset. The reason is the object -> mesh binding is unknown: spec-world-streaming.md
10.7 says a record's `+12` name index "is not possible to map to a mesh file from the header alone - OPEN"; Team B's own
`tile1018_placement_probe` (HANDOFF 9.136) refuted the candidate orderings (candidate B 1 match, C 8 matches/50%, D 0;
base rate 35.5%).

## Q2. Render-mesh bounds vs collision mesh (the ~s render channel is not the props)
`SR3_GROUND_TEX_DEBUG=1` world bounds of the ~s render mesh (results/prop_placement_y_groundbounds_stdout.txt):
- 0915: x[-22.3,22.3] y[-0.21,0.21] z[-10.4,13.0]  (26 tris, near-origin local space, spec 13.2)
- 0914: x[-2.0,2.0]  y[-0.50,0.50]  z[-6.2,6.2]    (156 tris, local)
- 0814: x[-1593,-1533] y[9.65,20.98] z[-255,-222]  (19 tris; already world-space, small piece)
Collision tail mesh (13.5.1, identity placement): 0915 x[-1461.6,-1094.5] y[3.77,42.56] z[-722.7,-380.0]; 0814
x[-1614.6,-1263.9] y[-9.94,41.36] z[-449,-125]. The render meshes are tiny local-space pieces, so NO single per-tile
translation lines them up with the collision mesh (best-fit translation is meaningless: the render pieces are 45 m x 23 m
against a 367 x 343 m collision tile). The props are hN `.clmesh_pc`, which are separate from the ~s render channel.

## Q3. What DOES line up: the SR3Z placement records (spec-world-streaming.md 10.7)
`~f<CCRR>.czh_pc` records: position = header origin (f32 x3) + s16/64 m. Measured against the topmost collision surface
(13.5.1 recipe, strip stand-in, start y = AABB max + 1) directly under each record's (x,z):

| tile | records | in collision XZ AABB | with ground hit | recordY - groundY: p10 / median / p90 | within 1 m |
|---|---|---|---|---|---|
| 0915 | 992 | 992 | 373 | -0.16 / +0.02 / +2.79 | 309 |
| 0814 | 933 | 933 | 378 | -0.62 / 0.00 / +3.65 | 292 |
| 0815 | 1411 | 1411 | 521 | -0.12 / +0.02 / +0.64 | 474 |
| 0916 | 923 | 922 | 451 | -0.19 / +0.04 / +12.95 | 309 |
| 0914 | 1325 | 1325 | 557 | -0.17 / +0.02 / +3.89 | 469 |
| 1015 | 1246 | 1245 | 491 | -0.16 / +0.02 / +1.01 | 413 |

The median residual is 0.00 to 0.04 m in all 6 tiles: record world Y (header origin Y + y/64) is the real world Y, equal to
the collision ground where a building/prop stands on ground (the p90 tail is rooftops/elevated objects). So the records
ARE correctly aligned to the collision ground with NO extra offset; the 0915 record XZ extents [-1439.9,-1119.2] x
[-699.9,-414.3] sit inside the collision AABB (tile lattice, 10.6(g)). What is missing is only the name-index -> mesh
binding (and the rotation word +6, HYPOTHESIS), not a transform or an offset.

## Q4. Change
None. A truthful fix needs the record -> hN mesh binding (OPEN) so each prop can use its own record position. A per-tile
translation is not applicable (Q2). The only available stand-in would be "drop each grid slot to the collision ground
under it" (a new CHOSEN with no measured alignment; the strip stand-in also misses about 62 % of columns), which I did not
implement, per the brief (no alignment is unambiguous).

## Team A questions
1. SR3Z record +12 name index: what list is it an index into (the registration `0x00858e40` count/second pass compares it
   with "a list of the source zone file's level-mesh names")? Which exact list/order (it is not the tile `.asm_pc` order:
   refuted on 1018)? With it, 992-1411 real placements per tile become usable.
2. Record +6 (s16 / 2^12): confirm it is a yaw in radians; are +8/+10 scale or pitch/roll?
3. The ~f czh record Y median matches collision ground to 0.02 m in 6/6 tiles; can that be added to 10.7 as confirmation
   that header origin Y + y/64 is world Y?
