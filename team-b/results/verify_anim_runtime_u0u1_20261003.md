# Animation runtime U0+U1 merge verification (2026-10-03)

Merges worktree `agent-af86d6872446c0637` (commit `dc952ee`): a new `sr3animruntime` library implementing
spec-anim-runtime.md §1-§6 — U0 (the control-parameter registry: 87-row table, name resolution, the typed
per-controller value block) and U1 (controller state slots, the play/query API, the §6.7 cross-fade, the
slot-array/finder machinery).

## Review

Reviewed the diff directly against the agent's own real branch point (`c29c7d0`, not current `main`'s tip,
to avoid the spurious-deletion artifact from files `main` added after the worktree branched) - clean,
additive, 9 files, 3343 insertions/0 deletions. One real overlap with `main`'s own concurrent work: both
this branch and my own localisation-lookup commit (`4de3780`) added a new library/test section to
`CMakeLists.txt` at the same point in the file - a straightforward content conflict (not a semantic one,
each library's own registration is independent), resolved by keeping both blocks.

Two deliberate, spec-grounded departures from my own task brief, both correctly reasoned and confirmed by
the agent with a test that distinguishes the two readings:
- **Where the cross-fade step is halved** (§6.7 step 2.6): the agent's reading (half at k=1, the
  second-heaviest of the OTHER members, numbered from heaviest) matches the spec's own numbering; my
  brief's "second-from-lightest" phrasing was imprecise. The two readings only coincide at 2-3 other
  members; a 4-other-member test distinguishes them and the agent confirmed the wrong reading fails 3
  checks.
- **Weight conservation is not unconditional** - only guaranteed while the other members hold enough
  weight; breaks when the current slot is capped at 1.0 or a member snaps to zero within 0.001 of the
  step. Both cases are tested.

Read the real/stub boundary directly: node-graph evaluation (`0x004cc6c0`, `0x004cc000`, the named-clip
search, the blend-tree evaluator `0x004b0080`) throws `OpenStateError` through an overridable
`EvaluationBoundary`, exactly the pattern this project already uses elsewhere for "real code with a
deliberately stubbed dependency." Confirmed via the test file that the stub is exercised (a fake boundary
lets every surrounding mechanism - slots, cross-fade, value propagation - run end to end).

## U2 (node-graph file format) follow-up

Team A's spec-anim-runtime.md §7 (U2) landed (`a2e2bed`) partway through this agent's run. It does not
remove any of the 4 stubbed call sites (3 are U3 evaluation, the 4th - the blend-tree evaluator - was
already out of this unit's scope), and the agent's worktree predates that spec sync. Left as explicit
follow-up, not done here: a §7 loader would supply the real node/alias data this unit's `AnimDataSource::
graphAliases` currently takes from an injected fake.

## Verification

Fresh build (`build_verify_anim_merge`, separate from the agent's own build dirs): clean, exit 0.
`ctest -C Release`: **69/69 passed** (67 pre-existing + `synthetic_anim_runtime_test` +
`real_anim_control_parameters_test`, the latter checking the shipped `control_parameters.xtbl` directly:
87 rows/52 bool/29 float/6 int, the 3 true defaults, all 62 compiled-in names resolving with exact case
and matching setter type, and the complement matching spec §4.6's own 25 unwritten rows).

No golden-scene check needed: this merge touches nothing under `tools/sr3_viewer.cpp` or any shared
rendering path.

## Known gaps, carried forward honestly (not closed here)

Per the agent's own report: node-graph evaluation and the blend-tree evaluator stay stubbed (pending the
§7 loader follow-up above); the re-trigger machinery (§6.8) and the `0x2000`-flag free routine stay OPEN
(spec itself says the re-trigger machinery is inert in shipped data, and `0x004b7f80` was never read);
Lua-native wiring, the writer census (§4), network/object-rig replication, and the Action branch (no
shipped blend tree uses it) are explicitly out of scope for this unit. Judgment calls on three
unspecified rider "...Abs" parameter names (derived from the shipped table's complement of §4.6's 25
unwritten rows, both directions checked by the real-data test) and the slot finder's id-range signedness
are documented in the agent's own code comments.
