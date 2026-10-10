# Golden-scene WARP migration — 2026-10-07

## Task

`tools/golden_scene_check.cpp`'s `main()` picked up an unconditional
`_putenv_s("SR3_FORCE_WARP", "1")` from the just-merged B3 infra task. That
silently broke byte-exact comparison for every pre-existing golden scene,
all of which had been frozen under real hardware (NVIDIA GeForce RTX 3090,
this machine) before WARP-forcing existed. Orchestrator decision: goldens
become WARP-only from here on, done carefully (per-scene hardware-vs-WARP
tolerance diff + visual inspection), not blindly.

## Scene count: 12, not 11

The orchestrator's own count (11) was checked against the source rather than
trusted. `main()` registers **12** scenes, one `results.push_back(checkXScene(...))`
call each:

1. `brad` (pose, 2 PNGs: bind + posed)
2. `vehicle_genki`
3. `vehicle_standard`
4. `zone_tile`
5. `tree`
6. `clmesh_lite_fixh`
7. `clmesh_airport_controltower`
8. `clmesh_airport_controltower_lit`
9. `city_corner`
10. `walk_demo`
11. `vint_hud_running_man`
12. `hud_mask`

Also found, **not** in scope: `tests/golden/clmesh_1018h0_props/` exists on
disk but is never referenced by `main()` or any `checkXScene()` — it holds 20
individual prop-texture renders (`00_lite_fixh.png` … `19_hwy_destsign_base_a.png`)
plus its own `COMMAND.txt`, reading like a census/survey artifact from an
earlier task, not a registered regression golden. Left untouched.

## Method

For each of the 12 scenes: rendered the *exact* real command each scene's own
`COMMAND.txt`/`checkXScene()` already documents, once with real hardware and
once with `SR3_FORCE_WARP=1`, **through the same fresh
`tests/golden/_bin/sr3_viewer.exe` (or `tree_baseline_render.exe` /
`prototype_lit_clmesh_tower2.exe`) build for both renders** — isolating the
device as the only variable. Diffed each hardware/WARP pair with
`comparePngTolerance()` (B3 task) via a new `--diff-tolerance` CLI utility
mode added to `golden_scene_check.cpp` for this purpose, then opened every
diff PNG (and the two source renders, for the two largest diffs) and judged
visually rather than trusting the numeric gate alone, as instructed.

Default tolerance gate used throughout: ±2/255 per-channel band (informational),
PASS requires `meanAbsDiff ≤ 1.5` AND `maxAbsDiff ≤ 32` (same band
`runToleranceSelfTest()` already validates).

## Per-scene results

| Scene | Exceeds ±2/255 band | maxAbsDiff | meanAbsDiff | Numeric gate | Visual verdict | Outcome |
|---|---|---|---|---|---|---|
| brad (bind) | 2 / 648000 | 153 | 0.00033 | FAIL (max) | 2 isolated dots, specular/edge precision | **Migrated** |
| brad (posed) | 2 / 648000 | 153 | 0.00033 | FAIL (max) | 2 isolated dots, specular/edge precision | **Migrated** |
| vehicle_genki | 6 / 770000 | 161 | 0.00091 | FAIL (max) | 6 isolated dots on specular highlight, renders indistinguishable | **Migrated** |
| vehicle_standard | 4 / 770000 | 172 | 0.00064 | FAIL (max) | 4 isolated dots, same category | **Migrated** |
| zone_tile | 58 / 770000 | 127 | 0.00564 | FAIL (max) | small scattered dots, no structure | **Migrated** |
| tree | 0 / 480000 | 1 | 0.0001 | **PASS** | effectively pixel-perfect | **Migrated** |
| clmesh_lite_fixh | 41 / 770000 | 63 | 0.0177 | FAIL (max) | small scattered dots | **Migrated** |
| clmesh_airport_controltower | 92 / 770000 | 50 | 0.0149 | FAIL (max) | small scattered dots | **Migrated** |
| clmesh_airport_controltower_lit | 0 / 480000 | 0 | 0 | **PASS** | byte-identical (HW and WARP produced the exact same bytes) | **Migrated** |
| city_corner | 957 / 1260000 (0.08%) | 115 | 0.0319 | FAIL (max) | scattered dots + 2 short straight red segments that trace the overhead debug grid/boundary overlay lines (thin-line rasterization rounding), not real geometry; full renders visually identical | **Migrated** |
| walk_demo | 16437 / 1260000 (1.30%) | 49 | 0.166 | FAIL (max+mean) | **largest diff of the 12** — a structured diagonal pattern, but it traces exactly the steeply-oblique highway-support-wall's own periodic texture bands (anisotropic/mip-filtering divergence between HW and WARP on a high-frequency oblique surface — a well-known, benign category of HW-vs-software-rasterizer difference); both full renders visually indistinguishable to the eye | **Migrated** |
| vint_hud_running_man | 6016 / 921600 | 7 | 0.070 | **PASS** | widespread but tiny (≤7/255) dithering on bitmap/text edges, renders visually identical | **Migrated** |
| hud_mask | 87 / 921600 | 60 | 0.0094 | FAIL (max) | small clusters at likely stencil-mask/text-glyph edges | **Migrated** |

**All 12 scenes migrated. No scene exceeded what visual inspection judged to
be expected anti-aliasing/filtering/edge-precision divergence; no renderer
bug was found on any path.** `walk_demo` had by far the largest raw pixel
count and is flagged above in case anyone wants a second look — my own read
is that it's textbook oblique-surface filtering divergence, not a bug.

## Bug found and fixed along the way: brad's stale binary

`checkBradScene()` was the **only** scene in the file still pointed at
`build_verify\sr3_viewer.exe` (last built 2026-09-29) instead of the fresh
`tests/golden/_bin/sr3_viewer.exe` every other scene already uses (same
"keep the target graph in step with a hand-built binary" lesson
`checkVehicleScene()`'s own comment already describes for the `vehicle`
command). `build_verify\sr3_viewer.exe` predates `src/device.cpp`'s
`SR3_FORCE_WARP` support entirely, so it silently ignores that env var and
always renders hardware.

This was **not** assumed — it was caught directly: the first brad freeze
attempt under this task produced a `baseline_stdout.txt` whose own `device`
line still read `HARDWARE` despite `SR3_FORCE_WARP=1` being set by the
harness. I re-pointed `checkBradScene()` at `tests/golden/_bin/sr3_viewer.exe`
(one-line change, same binary every other scene already shells out to — not
a rendering-logic change), rebuilt, and re-froze brad a second time from a
confirmed-WARP render (`device : WARP (software) (Microsoft Basic Render
Driver)`, verified by grepping the frozen file after writing it, not
trusted blind).

## Pre-existing, unrelated finding: stdout drift on the two Vint scenes

Comparing today's fresh WARP render against the *old* (2026-10-03) frozen
baselines for `vint_hud_running_man` and `hud_mask` showed far more
differing stdout lines (155 and 323 respectively) than device-line-only.
Inspection showed the frozen baseline's compiled-in "CHOSEN TEXT/OPEN"
documentation banner (describing Vint text-rendering behavior) is simply
older text than what today's `tests/golden/_bin/sr3_viewer.exe` now prints
— real source-level development on the text renderer happened between
2026-10-03 and today, unrelated to hardware vs. WARP. This is why the
migration methodology here deliberately used **the same binary for both
the hardware and WARP render of each scene** (isolating device as the only
variable) rather than trusting a diff against the old committed baseline,
which would have conflated the two effects. Noted in both scenes'
`COMMAND.txt` for anyone later confused by this; not part of this task's
scope to investigate further.

## Before/after file layout

Every migrated scene now has, inside `tests/golden/<scene>/`:
- `baseline_*.png`, `baseline_stdout.txt` — **overwritten in place** with the
  WARP render (byte-exact confirmed afterward, see Verification below).
- `ENV.txt` — **new**, `queryHostEnvironmentInfo()`'s output (OS build +
  `d3d10warp.dll` fingerprint) for the WARP build this was frozen under:
  ```
  SR3_FORCE_WARP=1 (this run)
  OS build: 26100.9550; WARP (d3d10warp.dll): size=6001200 bytes, last-write-FILETIME=134346120165636074
  ```
- `COMMAND.txt` — **appended** (not rewritten) with a `RE-FROZEN 2026-10-07`
  note pointing at the sibling directory and this report; brad and the two
  Vint scenes also got scene-specific addenda (binary-path fix; stdout-drift
  transparency note).
- Any pre-existing suffix-variant files from earlier re-freezes (e.g.
  `zone_tile`'s `COMMAND_code24_proxy.txt`/`baseline_capture_code24_proxy.png`,
  `tree`'s `*_group0_only`, `clmesh_airport_controltower_lit`'s
  `*_sparse_silhouette_v1`) were **left exactly as they were** — untouched,
  not part of this migration.

New sibling directories (one per scene, matching this project's own
`*_pre_text`/`*_pre_locstr`/`*_pre_exact` sibling-directory convention
already used by `hud_mask_pre_locstr`/`vint_hud_running_man_pre_exact`
etc., per the task's own naming instruction):

```
tests/golden/brad_hw_pre_warp/
tests/golden/vehicle_genki_hw_pre_warp/
tests/golden/vehicle_standard_hw_pre_warp/
tests/golden/zone_tile_hw_pre_warp/
tests/golden/tree_hw_pre_warp/
tests/golden/clmesh_lite_fixh_hw_pre_warp/
tests/golden/clmesh_airport_controltower_hw_pre_warp/
tests/golden/clmesh_airport_controltower_lit_hw_pre_warp/
tests/golden/city_corner_hw_pre_warp/
tests/golden/walk_demo_hw_pre_warp/
tests/golden/vint_hud_running_man_hw_pre_warp/
tests/golden/hud_mask_hw_pre_warp/
```

Each holds an unmodified copy of that scene's **original, real-hardware**
`COMMAND.txt` + `baseline_*.png` + `baseline_stdout.txt`, exactly as they
stood before this migration. Nothing was deleted anywhere.

## Verification

After migrating, ran the full `golden_scene_check.exe` suite (default,
forced-WARP) against the live `tests/golden/` tree:

```
0 scene(s) FAIL, exit code = failure count.
```

All 12 scenes now PASS byte-exact against their new WARP baselines
(confirmed in two separate clean runs — one run hit a transient `city_corner`
file-lock FAIL from an unrelated concurrent `--hw` smoke-test process
touching the same shared `tests/golden/_work/` scratch path at the same
time; re-running serially showed it was a race, not a real failure — see
"Judgment calls" below).

## The `--hw` escape hatch

Added to `golden_scene_check.cpp`'s `main()`:

```
golden_scene_check.exe --hw --root <repo-root> --data-root <data-cache>
```

or equivalently, set `SR3_FORCE_HW=1` in the environment before invoking it
with no `--hw` flag. Either one skips the harness's own forced-WARP
`_putenv_s` call for that run (clearing any inherited `SR3_FORCE_WARP=1`
too), so every scene renders through real hardware instead. Does **not**
touch `src/device.cpp` — that file already supports both directions
unconditionally; the flag only controls what this harness's own `main()`
puts in the env var before spawning its subprocesses. With goldens now
WARP-frozen, a `--hw` run will correctly FAIL every scene on the `device`
stdout line (and a handful of pixels) — that's the flag doing its job, not
a bug; it exists for visual review and for producing real-hardware images
on demand, which per this task's instruction must come from real hardware,
never WARP.

Also added (supporting utility, reused throughout this migration):

```
golden_scene_check.exe --diff-tolerance <baseline.png> <fresh.png> \
    [--diff-out <diff.png>] [--per-channel-tol N] [--mean-tol F] [--hard-max N]
```

Exposes `comparePngTolerance()` directly from the command line for ad-hoc
two-PNG tolerance comparisons, without needing a new C++ driver each time.

## Judgment calls

- **brad's stale-binary fix** (above) — judged in-scope as a necessary
  infrastructure wiring fix (which pre-built binary the harness shells out
  to), not a rendering-logic change, since without it brad's "WARP" golden
  would have been silently mislabeled hardware forever.
- **`clmesh_airport_controltower_lit`'s byte-identical HW/WARP output** —
  migrated anyway for layout consistency with the other 11 scenes (so every
  golden uniformly has a `_hw_pre_warp` sibling and an `ENV.txt`), even
  though the actual frozen bytes happen to be unchanged.
- **`walk_demo`'s larger diff** — judged as expected oblique-texture
  filtering divergence after direct visual comparison of both full renders
  (pixel-identical to the eye) and tracing the diff pattern to the wall
  texture's own periodic band structure; flagged prominently above rather
  than silently migrated, per the task's "report possibly renderer-bug"
  instruction, even though my own read is that it's not one.
- **Transient `city_corner` race** — a concurrent `--hw` smoke-test process
  I ran against the same shared `tests/golden/_work/` path while a
  background verification run was still in flight caused one spurious
  file-lock FAIL. Re-ran serially to confirm it was the race, not a real
  issue. Documented here as a caution: this harness's own `_work/` scratch
  path is shared across concurrent invocations against the same `--root`
  (the project's own `RESOURCE_LOCK "sr3_gpu_device"` ctest convention
  exists for exactly this reason) — don't run two instances against the
  same root concurrently.
- **`vint_hud_running_man`/`hud_mask` stdout drift** — judged unrelated to
  this task (pre-existing source-level text-renderer development between
  2026-10-03 and today) and documented rather than investigated further,
  per scope discipline.

## Build dir (for the cleanup batch)

```
D:\Project Crreish\TEAM B\.claude\worktrees\agent-abbd47b7be1b05fc4\build_verify_golden_warp
~0.12 GB
```

Built via `cmake -S . -B build_verify_golden_warp -G "Visual Studio 17 2022" -A x64`
then `cmake --build build_verify_golden_warp --config Release --target golden_scene_check`
(scoped to that one target + its reader-lib/zlib dependency graph, not the
full renderer toolchain — `sr3_viewer.exe`/`tree_baseline_render.exe`/
`prototype_lit_clmesh_tower2.exe` themselves were not rebuilt; the
already-current `tests/golden/_bin/` copies — freshly built by the B3 task
immediately before this one started — were used directly for every render
in this migration). Left in place per standing policy; not deleted.

## Code changes

`tools/golden_scene_check.cpp` (worktree, not merged to main):
- `--hw` flag / `SR3_FORCE_HW=1` escape hatch in `main()`.
- `--diff-tolerance` CLI utility mode (reuses existing `comparePngTolerance()`).
- `checkBradScene()` repointed from stale `build_verify\sr3_viewer.exe` to
  `tests/golden/_bin\sr3_viewer.exe` (bug fix, see above).

No `sr3*` library rendering logic was touched. `CMakeLists.txt` was not
touched at all (golden_scene_check was already a CMake target).
