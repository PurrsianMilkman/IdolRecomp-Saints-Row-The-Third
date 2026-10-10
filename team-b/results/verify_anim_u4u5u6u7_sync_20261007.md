# Checking the U5/U4/U6/U7 spec sync against the existing anim-runtime evaluator (2026-10-07)

Task: Team A landed four new `spec-anim-runtime.md` sections (§9 U5 root motion/post-sample, §10 U7 other
controllers, §11 U4 time/layers/pose hand-off, §12 U6 human procedural modifiers) plus "§8 precision fixes",
in commit `5ea0de6`. The orchestrator's framing was that §8's fixes settle two discrepancies this project's
own U2/U3 merge (`26fbd80`, `results/verify_anim_node_graph_20261004.md`) reported as FYI findings against
the spec text at the time (output-list margin 30 vs 31; a 0.4 s ramp taking 12 vs 13 float-precision updates),
and that §11 (U4) might let `sr3playeranim`'s bridge replace a CHOSEN simplification with real behaviour. This
doc records what full reads of §8-§12 actually found, independently checked against this project's existing
code rather than assumed.

## 1. The two known discrepancies: both genuinely resolved, confirmed independently

Re-read §8 in full (not a diff) and compared it against the existing evaluator (`src/anim_node_graph_runtime.cpp`,
`tests/real_anim_node_graph_test.cpp`, `tests/synthetic_anim_node_graph_runtime_test.cpp`):

- **Output-list margin (30 vs 31).** §8.10's "Output-list bound" paragraph is now split into exactly the two
  readings this project's own agent reported: the as-shipped/as-evaluated peak is **30** (`ride_left_ready_sm`/
  `ride_right_ready_sm`), the format's own worst case is **31** (a nested state machine's own in-flight
  transition stacked on its parent's). The spec's own changelog entry (line 3614) states this was
  **"re-derived from Team B's own real-data test over all 249 shipped files ... which independently reproduces
  every other census figure in this section"** - i.e. `real_anim_node_graph_test.cpp`'s own output-list-bound
  block, not a Team A re-derivation from scratch. The existing test already asserted `CHECK_EQ(bounds[0].first,
  30)` (non-recursive) and `31` (recursive) before this sync landed; both still pass unchanged. The 32-entry
  `GraphOutputList::kCapacity` guard was never in question either way (31 < 32, margin 1 either reading).
- **Ramp updates (12 vs 13).** §8.10's ramp line now says **13**, not 12, for `max_blend_rate` = 0.4, with the
  exact mechanism this project's own agent found: the running weight is a 32-bit float, after 12 updates it
  sits `1.1920929e-7` short of 1.0 (one float32 ULP), and the 13th update is what actually snaps it. The spec
  credits this to disassembling `0x004bc0d0` directly ("pure SSE/SSE2, no x87... no extra precision survives
  between frames"), settling it by evidence rather than by trusting either side's abstract reasoning. The
  existing `NodeGraphRuntime::multiWayPass1` ramp code was never changed - it already produces 13 by plain
  float arithmetic on the float fields, which is what the comment already said before this sync.

**Both are resolved, and resolved in the direction this project's existing code and tests already assert.**
No behavioural change was needed. Code comments were lightly annotated (not changed) to record the resolution
and its provenance, since the prior comments described the port's own behaviour as a "finding" against the
old spec text, which read oddly once the spec agreed:
- `src/anim_node_graph_runtime.cpp` (`multiWayPass1`'s ramp comment)
- `tests/real_anim_node_graph_test.cpp` (output-list-bound block comment)
- `tests/synthetic_anim_node_graph_runtime_test.cpp` (the ramp test's comment, and its 0.4 s case description)

No test changed its assertions; the pre-existing `CHECK`s already covered 13/31 (several as a lenient
`== 12 || == 13` / `== 30 || == 31` pair, left as-is since tightening them is unnecessary now that both
numbers are explained, and loosening nothing was required for correctness).

## 2. New ground from U4/U5/U6/U7: none implementable in this file's scope, and why

Read §9 (U5), §10 (U7), §11 (U4) and §12 (U6) in full (not skimmed). Finding, stated plainly: **all four units
describe what happens to a SAMPLED POSE** - bone matrices produced by a rig/skeleton, held in a real
animation-instance object with its own per-layer array, pose-request queue and bone-hierarchy compose
pipeline. Concretely:

- **§9 (U5).** Its own headline result (§9.0 item 1) is that the function the scoping note called "root
  motion" is actually the human's **spine-bend and look chain**: nine bones, per-axis joint limits, headroom,
  proportional distribution, a shared follower, written back as **per-bone rotation overrides** consumed by
  the compose step. The rest of §9 is the **motion-warp correction step** (`0x004b6530`, confined to authored
  `start correction`/`dest correction` trigger windows) and the velocity composer's remaining animation-side
  inputs (clip speed, the `force use velocity` window, controller `+0x1a4`'s bits). All of it operates on
  sampled bone matrices, clip-header root-motion transforms and a real controller/instance object.
- **§10 (U7).** Five non-human clients (vehicle, `object_rig`, held-weapon, two generic "secondary rig"
  helpers) that reuse the exact same U0-U4 API surface this project already models at the control-parameter
  level, but build real per-bone matrix overlays and attached-object placement on top of a real
  rig/instance/pose-buffer pipeline this project does not have.
- **§11 (U4).** The pose-request queue (`0x004bf4c0`/`0x004bf360`/`0x004bf270`), the full `+0xcd2` mode table,
  and the two compose functions (`0x004c31b0` per-slot setup, `0x004c36f0` the actual bone-hierarchy
  concatenation, resolving the model-space-vs-skin-space question at model-space) - all bone/rig/instance
  machinery.
- **§12 (U6).** The aim/look bearing calculator and the real spine-bend chain (`0x009b34f0`→`0x009b0e90`,
  corrected from the scoping note's wrong grouping) - again bone-rotation output, consumed downstream of
  anything this project's node-graph evaluator produces.

**This project's `sr3animruntime` has no rig/skeleton model and does not decode `.anim_pc` keyframe/track
payloads** (`sr3anim`'s own README: "the obvious model is measurably wrong and it needs the runtime sampling
subsystem, not more parsing"; also restated in `include/sr3playeranim/bridge.h`'s own banner). The node-graph
evaluator's whole contract (§8, already implemented) is to produce a weighted list of (record, weight, loop,
clip-time) entries - it never touches a bone. Implementing any part of §9/§10/§12, or the bone/pose/queue
parts of §11, would mean inventing the missing rig/instance/layer-array object from whole cloth, which this
project's own `LayerApi`/`EvaluationBoundary` boundaries exist specifically to avoid (inject the real object
when one exists; throw `OpenStateError` until then) - exactly what the task's scope discipline says not to do
("maintenance ... not a new one"). **Conclusion: no new ground from §9/§10/§12, and from §11, within this
library's existing scope.** This is an honest "nothing to implement" finding, not a shortcut - verified by
reading every one of the four sections in full and checking each against what this project's evaluator
actually models (control parameters and the abstract node graph only, no bones).

A note recording this finding (with the specific reasoning above) was added to `include/sr3animruntime/controller.h`'s
file banner, so a future reader checking this spec sync again does not have to redo the investigation.

## 3. The `sr3playeranim` bridge's CHOSEN SIMPLIFICATION: still correct, confirmed not closable by U4

The task specifically asked whether U4's new text lets the bridge's post-step T/E split (`PostOutputList` in
`include/sr3playeranim/bridge.h`) replace its loop-bit proxy for the real layer flag `0x01` with the real
mechanism. Checked directly: **no.** §11.0 (U4's own "what is cited, not repeated" section) states outright
that the layer-flag-`0x01` phase-synchronisation split itself is cited from `spec-player-runtime.md` §5.4.3,
**"not re-derived"** by this unit - i.e. U4 adds nothing new on that exact question. What U4 *does* add (the
pose-request queue, the per-job sample buffer, the bone-hierarchy compose pipeline, the real per-layer array
at instance `+0x70`) all presuppose the real animation-instance object this project does not have (§2 above).
So the missing piece - a real layer object to carry the real bit - is still missing, and nothing in this sync
supplies it. The bridge's own banner comment was updated with this finding (dated, cited) so the CHOSEN
SIMPLIFICATION is not re-litigated on a stale premise next time U4-adjacent text lands.

## 4. An unprompted finding: Sec6 and Sec11.7 disagree on two real addresses

While checking U4's layer-API findings (§11.7) against this project's existing `LayerApi` boundary
(`include/sr3animruntime/controller.h`), a genuine, previously-unflagged contradiction turned up, **not
mentioned by Team A's own changelog** for this sync:

- §6.6 (stop-with-fade), §6.7 (phase handling) and §6.8 (re-trigger) - three independent, mutually consistent,
  already-implemented passages - all describe `0x004c7510` as **the phase setter** ("set the current layer's
  phase... to 0.0 / to p") and `0x004be510` as **the rate setter** ("the layer's rate is set to 0.0
  (`0x004be510`...)"; a child's rate "scaled by... (`0x004be510`)").
- §11.7 (today's new U4 text, "CONFIRMED by full listing past its first `ret`") describes the **same two
  addresses with the roles swapped**: `0x004c7510` as "the rate setter" (storing at a newly-named layer
  `+0x1c`), `0x004be510` as "the explicit-phase setter" (storing at layer `+0x18`) - with no cross-reference
  to §6 at all.

This project's existing code (`LayerApi::setLayerRate`/`setLayerPhase`, and their call sites in
`src/anim_controller.cpp` - `setLayerRate(h, 0.0f)` to freeze, `setLayerPhase(h, 0.0f)`/`setLayerPhase(h, p)`
to restart/sync a phase) matches §6's reading, which is also the reading three independent passages agree on.
**I did not change the code or its address citations.** Flipping them to match §11.7 on the strength of one
new, isolated, non-cross-referenced passage - when three older passages that match the already-tested
behaviour disagree with it - is exactly the kind of "pick a side without being able to verify it" call the
project's own discipline says to avoid. (This project has no way to independently re-disassemble the real
`.exe` to settle it either; that is Team A's methodology, not something this clean-room port does.) Both
addresses are pure citations on an abstract virtual interface - no behaviour depends on which hex value is in
the comment - so leaving the comments flagged-but-unresolved costs nothing functionally and avoids asserting
something unverified. Flagged in `controller.h` at both virtuals, for relay to Team A.

A second, smaller version of the same pattern: `0x004bdd40` (`LayerApi::layerGone`) now has **three** different
descriptions across the spec's own history (original: "inactive with zero weight"; 2026-10-03 changelog
correction: "no play time left (`0x004c6350`), not zero weight"; today's §11.7: a plain instance-handle
validity test with no second condition at all). Flagged in the same comment block; code unchanged (same
reasoning - an abstract virtual, no behaviour at stake, not safely resolvable from here).

## 5. Build and tests

Fresh build directory `build_verify_u4u5u6u7` (kept in place per policy, not deleted):
`cmake -S . -B build_verify_u4u5u6u7 -G "Visual Studio 17 2022" -A x64` then
`cmake --build build_verify_u4u5u6u7 --config Release` - clean, no errors (one pre-existing, unrelated MSVC
warning in `tools/validation/validate_save_snapshot.cpp` about shadowing a declaration of `id`, not touched
by this pass). `ctest -C Release`: **79/79 passed**, including all 9 anim-related tests
(`synthetic_anim_test`, `synthetic_anim_payload_test`, `synthetic_anim_runtime_test`,
`synthetic_anim_node_graph_test`, `synthetic_anim_node_graph_runtime_test`, `synthetic_player_anim_bridge_test`,
`synthetic_tables_animation_test`, `real_anim_control_parameters_test`, `real_anim_node_graph_test`). No test
assertions changed; this pass is comment/documentation-only, confirmed clean rather than assumed clean.

Build dir for the cleanup batch: `build_verify_u4u5u6u7` (339 MB) - not deleted, per standing policy; path
given here for the orchestrator to batch.

## 6. Files touched (all additive/comment-only; no CMakeLists.txt change, no behaviour change)

- `include/sr3animruntime/controller.h` - file-banner note on §9-§12 scope (§2 above); `LayerApi` comments
  flagging the §6-vs-§11.7 address contradiction and the three-way `layerGone` disagreement (§4 above).
- `src/anim_node_graph_runtime.cpp` - ramp comment updated to record the spec's own 12→13 correction and its
  provenance.
- `tests/real_anim_node_graph_test.cpp` - output-list-bound block comment updated similarly (30/31 margin).
- `tests/synthetic_anim_node_graph_runtime_test.cpp` - ramp test comment updated similarly.
- `include/sr3playeranim/bridge.h` - banner note recording that §9-§12 were checked and do not close the
  post-step CHOSEN SIMPLIFICATION gap, with the specific §11.0 citation.

No new tests were added: nothing new was implemented (§2), and the two citation/documentation findings (§4)
are not behavioural, so there is nothing a test could regress-guard. The existing, unmodified test assertions
already serve as the regression evidence for the two resolved discrepancies (§1).
