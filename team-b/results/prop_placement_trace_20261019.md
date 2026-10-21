# Prop placement trace (feature/prop-placement, spec-world-streaming.md 10.7.1)

Playtest only (loadCell in viewer_playtest_setup_world.cpp); SR3_PROP_GRID=1 restores the old grid stand-in (the "before" captures).
Golden paths (viewer_city.cpp / viewer_drive.cpp) are untouched.

| run | tile records | bound | instances placed per cell | meshes with no record/instance | peak working set |
|---|---|---|---|---|---|
| default spawn (0915) | 992 | 992 (100 %) | h0 178, h1 253, h2 166, h3 405 (= 1002 instances from 194 meshes) | 0 | 3479 MB (grid before: 3463 MB) |
| -1280,-470 (0915/0914/0814) | 992 / 1325 / 933 | 100 % each | 0915h0 178, 0915h1 253, 0914h2 347, 0814h3 298 | 0 | 3496 MB (grid before: 3513 MB) |
| m01 (--mission-timeout=60) | 992 | 992 | as default spawn | 0 | exit 0 |

Meshes loaded = one GPU copy per `.clmesh_pc` entry (194 in 0915); each placed instance is a CityProp copy sharing the renderers
(shared_ptr) with its own world matrix and its own world-space collision BVH. Captures: prop_placement_{before,after}_{default,m1280m470}.png.
