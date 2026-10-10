# Player<->animation coupling ("a character walks") verification (2026-10-07)

New library `sr3playeranim` (`include/sr3playeranim/bridge.h`, `src/player_anim_bridge.cpp`,
`tests/synthetic_player_anim_bridge_test.cpp`, plus an additive `CMakeLists.txt` registration). This is the
first unit that references both `sr3player` (spec-player-runtime.md) and `sr3animruntime`
(spec-anim-runtime.md) - previously independent libraries with no code path between them.

## What this is

Not a ported engine unit - there is no single `0x......` address for "the bridge" the way other units have.
What is real and spec-cited:

1. **The field -> control-parameter writer** (spec-anim-runtime.md Sec4.2, the `0x00958690` census), every
   row reproduced individually in `WriteHumanWriterParams()` - see that function's own citation comment in
   `bridge.h` for the row-by-row account of what's wired vs. left `OpenValue`/`OpenStateError`.
2. **Driving the player's own real graph** through the already-merged, already-tested U2/U3 loader +
   evaluator (`sr3animruntime::NodeGraphRuntime`), both passes, every frame - bypassing `Controller`'s
   slot/parameter-block/child-ring machinery entirely (a single continuously-running graph instance has
   nothing for that machinery to do), writing values directly into the `GraphInstance`'s alias slots - the
   same direct-write pattern `tests/synthetic_anim_node_graph_runtime_test.cpp`'s own `World::set()` already
   established.
3. **The scoped-down post step** (spec-anim-runtime.md Sec6.10 step 5 / Sec8.9 step 5 - the gap flagged
   explicitly in `results/verify_anim_node_graph_20261004.md`'s "Known gaps"). `PostOutputList()` turns the
   graph evaluator's real 32-entry output list into a T/E split using the clip's own loop bit as a proxy for
   the real engine's missing layer flag `0x01` ("phase-synchronised") - see `bridge.h`'s own long banner
   comment for the full justification (this project has no animation-instance/layer object anywhere to carry
   the real bit on).
4. **The playback-rate formula** (spec-player-runtime.md Sec5.4.3 step 7), `PlaybackRateTracker`, clamped to
   `[0.1, 1.5]` with the asymmetric rise/fall rates exactly as specced.
5. **Frame ordering** matching Sec5.1/Sec5.9 exactly, including the real one-frame lag on `current speed`/
   `Vertical Speed`/`Speed Along rvec` - achieved by calling `PlayerMovement`'s individual phases
   (`crouchRoutine`/`sprintRoutine`/`moveSlot`, then `composeVelocity`/`integrate`/`publish`) instead of its
   monolithic `runFrame()`, so the graph's own `E` output can feed the SAME frame's composer, exactly like
   the real engine's `0x0099d1b0` consuming `0x004c58c0`'s output same-frame.
6. **Camera hookup** (`DriveCameraTarget()`/`OrientationFromFacing()`): plain field assignment from the
   character's current position/orientation into `CameraSystem::target` - no new camera behaviour, since U4
   already exists.

## Judgement calls made (documented, not silent)

- **`locomotion` parameter**: direct inspection of the real `player_state_machine` graph (name has a space -
  `"player state machine"`) shows it gates idle vs. its nested `locomotion BT` blend tree on a `locomotion`
  control parameter that Sec4.2's writer census never writes (one of Sec7.13's ten "referenced by a graph, no
  compiled-in writer" names). CHOSEN: write it from `PlayerMovementState::locomotionState` (+0xccc) - the
  same concept under the same name, owned by the locomotion bridge rather than the human writer.
- **Movement-direction bools (rows 8-11)**: Sec4.2 cites `+0xd40` bits, but `+0xd40` has no real writer
  anywhere in this project (`movement.h`'s own documented OPEN 38) - a dead always-0 stand-in there would make
  these bools constantly read "forward" regardless of real input. CHOSEN DEVIATION: derive them from the
  published camera-relative walk axes (`ControlsGlobals::publishedA/B`, already written by `moveSlot`'s own
  `readAxes()` this frame), bucketed with the exact precedence rule row 8 itself describes. Rows 12-13
  (backpedal/alternate-strafe) are NOT given this treatment - they read `+0xd40` directly as specced, since
  there's no equivalently simple real signal to substitute and no spec body to derive one from.
- **Root motion (T/E) cannot be real translation vectors**: `sr3anim` never decodes the `.anim_pc`
  keyframe/track payload (a long-standing, deliberate, pre-existing scope limit, re-confirmed here, not
  something this task touched) - there is no real per-clip root-motion delta anywhere in this codebase.
  `ClipRootMotionSource` is an explicit injectable seam (OPEN by default) so the real, specced, testable logic
  (which entries land in T vs E, un-normalised vs. normalised weighting, the loop-bit bucketing, the 0.0001
  drop threshold) is exercised honestly with caller-supplied numbers, never fabricated ones.
- **Playback rate's undefined sub-case**: the spec's rise/fall formula is explicitly conditioned on
  `desiredSpeed > 1e-4`; it does not describe what happens below that. CHOSEN: hold the previous rate (the
  minimal-invention reading of "no write this frame").
- **Slot weight = 1.0**: a single continuously-running graph instance has no cross-fade slot array to scale
  output-list weights by, so the real "multiply by slot weight" step (Sec6.10 step 5) is the identity here.

## OPEN left genuinely open (never guessed)

`AnimParamWorld`: `combat ready`, `aim bool`, `fire bool`, `Parachute Open Bool`, `Free Fall Vert Control`
(only reached if `world.freeFall` is ever true), `Aim Pitch`, `Aim Heading`, `vehicle surf lean`,
`Reloading Bool` - each a named, unmodelled subsystem (combat/aim/weapons/parachute/vehicle), each
independently false/0.0 in the common-case seeder (`SeedPlayerAnimCommonCase`), each throwing
`OpenStateError` only if a caller actually drives the gate that needs it (e.g. `+0xccd == 4/5` for the
climb/dive rows, `combat ready == true` for the 21a/21b direction branch).

## Build / test

Fresh build (`build_verify_bridge2`, Release, Visual Studio 17 2022 / x64) - **clean, exit 0**, including
`sr3_viewer` and every pre-existing target. `ctest -C Release`: **75/75 passed** (74 pre-existing + the new
`synthetic_player_anim_bridge_test`), including the real-data population tests
(`real_anim_node_graph_test`, `real_anim_control_parameters_test`, etc.) against the actual game archive
cache - no regressions anywhere.

The new test (`tests/synthetic_player_anim_bridge_test.cpp`) covers, per the task's own required list:

- the parameter-writing bridge in isolation (hand-built `GraphAliasTable`, every category of row: stance
  bits, one-frame-lagged fields, the `direction` heading formula, the axis-derived movement-direction bools,
  the skip-when-unaliased behaviour, and the three genuine `OpenStateError` gates: `+0xccd == 4`, `combat
  ready == true`, `world.freeFall == true`);
- graph evaluation across many frames through the full `PlayerAnimBridge`, driving a small REAL loaded graph
  (via `sr3animruntime::LoadNodeGraphs`) that uses the same `locomotion`-parameter gate the real
  `player_state_machine` graph uses, with a `ForwardSource` scripted input - 90 frames, no throw, a real
  state transition from "Idle" to a looping "Moving" clip, non-zero `T` once that clip holds full weight;
- the T/E split for looping vs. one-shot entries, including a normalisation sanity check (two one-shot
  entries at different weights still average correctly) and the 0.0001-weight drop threshold's bit-1
  exception (Sec8.9 item 6);
- the playback-rate formula, cross-checked against an independent re-derivation of the same formula written
  directly in the test (not just compared against the production code's own output) across rise (both
  below/above rate 1.0), fall (with and without the `> 2q` extra `dt`), and the hold-when-`s<=1e-4` case;
- the camera tracking the character's position/orientation across a simulated move, plus
  `OrientationFromFacing`'s own round-trip/orthonormality.

## Known gaps, carried forward honestly (not closed here)

- The viewer demo (brad on `city_corner`, scripted walk, deterministic golden) is tracked separately - see
  whatever report lands from that in-flight piece; this document covers only the coupling library itself.
- `recordForTag`/`recordForClipName`/`recordDuration` stay OPEN by default in `PlayerGraphServices` (a
  caller must wire `tagToRecord`/`clipNameToRecord`/`duration`) - there is no real clip-name/tag-name ->
  loaded-animation-record resolution pipeline anywhere in this project (confirmed: even the 249-graph
  real-data population test, `tests/real_anim_node_graph_test.cpp`, stubs `recordDuration` to a constant
  `1.0f`, the same honest limitation this unit inherits rather than tries to solve).
- `0x004cc000`'s clip-node collection and the real `Controller`/`LayerApi` layer system stay out of scope, as
  the task specified (U4/U5 territory; this unit's whole point is to not need them for a single player
  graph).
