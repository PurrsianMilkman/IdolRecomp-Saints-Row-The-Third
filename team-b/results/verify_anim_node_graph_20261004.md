# Animation node-graph loader (U2) + runtime evaluator (U3) merge verification (2026-10-04)

Merges worktree `agent-a7e5b3211a655533a` (commits `710d531`/`d5d6ae4`/`d5f8c10`): the real node-graph file
loader (spec-anim-runtime.md Sec7) and the frame-by-frame evaluator (Sec8), replacing `sr3animruntime`'s
`EvaluationBoundary` stub from the earlier U0/U1 merge entirely, plus a bonus real implementation of U1
Sec6.9's 1-D blend-tree evaluator (also previously stubbed).

## Review

Reviewed the diff directly against the agent's own real branch point (`b536d50`, not current `main`'s tip)
- additive except for two already-merged files it correctly extended rather than duplicated:
`include/sr3animruntime/control_parameters.h`/`src/anim_control_parameters.cpp` (minor, 12/15 lines) and
`include/sr3animruntime/controller.h`/`src/anim_controller.cpp` (84/104 lines, replacing the Sec6.9 stub
with a real evaluator - needed two small interface additions, `BlendTreeRowInfo`'s ramp/curve fields and
two `LayerApi` phase hooks, both still throwing OPEN by default since their own data sources are U4's). One
file overlap with `main`'s own concurrent work (`CMakeLists.txt`, from the player-movement merge) resolved
automatically by git with no manual conflict.

Read `include/sr3animruntime/node_graph.h`/`node_graph_runtime.h` and their `.cpp` files directly: careful
adherence to every structural rule the spec calls out explicitly (the left-to-right fold with no
precedence, the `loop_flag` polarity correction applied from the start rather than fixed after the fact,
the silent-skip-unvalidated-graph rule, the 6th-control-point truncation, the switch-selector-shared-across-
instances quirk, the stateful-filter-evaluated-twice quirk - all kept deliberately for fidelity, not "fixed").

## Verification

**Population test, from an actual run, not asserted** (`tests/real_anim_node_graph_test.cpp`): all 249
shipped graph files load and pass every per-file structural check. Every one of spec Sec7.12's own census
numbers reproduced exactly: the 122/127 state-machine/blend-tree split, 530 transitions (304 `start`/171
`conditions`/7 both/62 neither), the token/comparison/connective counts, the 9 unresolved `tag_name`s by
name, the 155 `child_node_graph` references with zero cycles, node-kind counts, control-point counts, the
829 clip blocks, the largest alias table (10, `driving_sm`) and filter list (2). One extra nuance the agent
measured and reported honestly: 18 out-of-order blend inputs with the PATCHED `free_fall_bt` in effect (the
file this project actually loads), 21 with the unpatched base copy - the test checks both counts and that
the two copies differ only in that one input's point order, exactly as spec Sec7.1 describes the patch.

**Runtime replay against spec Sec8.10's own worked numbers**: all 4 fold-order traps give the engine's
answer (not precedence's) through the real evaluator; the `free_fall_bt` multi-way-blend weights match at
every one of the spec's own sample points (-60/-10/60/150) for both the base and patched copies; the filter
math matches exactly (`direction degrees`, `speed linear map filter`, `Turn Angle Cap`, and the
deliberately-kept-as-a-quirk `Turn Angle Delta Multi` double-evaluation giving 0.75/0.9375/0.984375); the
32-entry output-list guard was confirmed to actually fire on a synthetic 33rd-clip graph, not merely coded
and left untested.

**Two findings worth flagging independently** (both honestly reported by the agent, not glossed over):
- **Output-list bound is 31 in the worst case, not 30.** Spec Sec8.10's own survey of the 249 shipped
  graphs found 30 as the peak (`ride_left_ready_sm`/`ride_right_ready_sm`). The agent's own replay matches
  that exactly for the graphs AS SHIPPED. But if every nested state machine inside those same two graphs
  ALSO has a transition in flight simultaneously (a combination the shipped data doesn't happen to trigger,
  but isn't ruled out by the file format itself), the theoretical peak is 31 - still safely under the
  32-entry capacity, but the margin is 1, not 2 as spec's own text states. Not a defect in the port; a
  slightly tighter real margin than the spec's own survey implied. Worth relaying to Team A as an FYI, not
  a correction request - it doesn't change anything about the 32-entry guard already being necessary and
  already being implemented.
- **A `max_blend_rate` = 0.4 ramp takes 13 updates to reach 1.0 in this port, not 12 as spec Sec8.10
  states.** Root cause identified precisely: 12 × (1/30)/0.4 equals exactly 1.0 in real-number terms, but
  with the weight stored as a `float` (not `double`), the 12th update lands within 1e-6 of 1.0 rather than
  exactly at it, so a 13th update is needed to cross the snap threshold. The spec's own figure likely came
  from a double-precision Python replay. The 0.1 s and 1.0 s cases (3 and 30-31 updates) match exactly. This
  is a float-vs-double rounding difference, not a logic error - the test asserts either 12 or 13 explicitly
  and checks the actual weight after exactly 12 updates to document the exact margin.

**Build/test**: fresh build (`build_verify_graph_merge`, separate from the agent's own build dirs): clean,
exit 0. `ctest -C Release`: **73/73 passed** (70 pre-existing at this branch point + `synthetic_anim_node_
graph_test`, `synthetic_anim_node_graph_runtime_test`, `real_anim_node_graph_test`).

No golden-scene re-check: this merge touches nothing under `tools/sr3_viewer.cpp` or any shared rendering
path.

## Known gaps, carried forward honestly (not closed here)

Per the agent's own report, still `OpenStateError`:
- **The post step** (spec Sec6.10 step 5, U4's territory): `NodeGraphEvaluator::postOutput` and the write-
  back of node-instance `+0x38`/`+0x3c` (clip index / layer handle). Confirmed harmless for the 9
  unresolved-`tag_name` states specifically: none of them tests `clip time remaining` on its own first
  frame (the one case that would need `+0x38` before it's ever written); the one that tests `clip time as
  percent` correctly answers 0 either way.
- **`0x004cc000` clip-node collection** - the spec itself only has this function's first 140 instructions;
  `Controller::update` with live graph rows still throws until it's supplied, exactly as before this merge.
- Services this unit doesn't specify (tag->record via the anim-group tables, `clip_name`->record lookup,
  animation-instance phase, stopping a layer) are supplied through a `ControllerGraphServices` adapter over
  this project's existing interfaces; the blend tree's `Action` branch (no shipped blend tree uses it) and
  zone data (`.czn_pc`, never opened) are explicitly out of scope.
- The 1,318 compiled-in State/Action presets' real contents stay OPEN (spec-tables-animation.md Sec5.3
  itself leaves this OPEN) - the registry is built from graph file names only, and the test asserts exactly
  the 9 known-bad tags fail, nothing more.
