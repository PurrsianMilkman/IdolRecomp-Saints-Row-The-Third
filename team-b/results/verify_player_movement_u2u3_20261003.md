# Player movement (U2) + locomotion bridge (U3) merge verification (2026-10-03)

Merges worktree `agent-aa3e5ac5f3a6d5dbc` (commit `06c1309`) into `sr3player`: spec-player-runtime.md Sec3
(U2, movement/intent) and Sec5 (U3, the locomotion bridge), extending the U0/U1 input-query/control-state
machine already merged this session.

## Review

Reviewed the diff directly against the agent's own real branch point (`4de3780`, not current `main`'s tip)
- clean, additive, 6 files, 2744 insertions/4 deletions (the 4 deletions are the `CMakeLists.txt` lines the
new library/test registration displaces). One expected overlap with `main`'s own concurrent work: both this
branch and the just-merged animation-runtime branch added a library/test section to `CMakeLists.txt` -
resolved automatically by git this time (no manual conflict, unlike the anim merge's own CMakeLists
conflict), content confirmed correct by the subsequent build.

Read `include/sr3player/movement.h`/`movement_tweak_table.h` and `src/player_movement.cpp`/
`player_locomotion.cpp` directly: careful adherence to the Sec3.5.5/Sec5.4.4 closed forms, honest `OpenValue`
gating for every subsystem this project doesn't model (stamina, drunk-effect, facing override, NPC shaping,
moving-support velocity, swimming, move-to-object), and the Sec5.11-item-5-licensed simple integrator in
place of real Havok internals.

**The tweak-constant table gap was closed in this same pass**: `tweak_table.xtbl` (spec-tables-progression.md
Sec10.10/Sec14.15) landed as `36f9fca` while this agent was running, and it wired the real reader in rather
than leaving a second round of `OpenValue` scaffolding to revisit later - `include/sr3player/
movement_tweak_table.h` is a header-only adapter over the already-existing `sr3tables_progression` reader
(avoiding a new library dependency from `sr3player`), applying the spec's own load-time rule (case-
insensitive Framework filter, case-insensitive name match, later rows win). Honest about its own limits: no
fallback default exists for a name absent from the loaded table (neither spec publishes the compiled-in
image defaults), and it was tested only against synthetic XML, not the real shipped file - worth a quick
real-data check in a follow-up pass, same pattern as `real_anim_control_parameters_test.cpp`.

## Verification

Fresh build (`build_verify_player_movement`, separate from the agent's own `build_verify_u2u3`): clean, exit
0. `ctest -C Release`: **70/70 passed** (69 pre-existing at this branch point + `synthetic_player_movement_test`,
209 CHECK sites covering the closed-form math, the approach/smoothing convergence sequence, the sprint/walk/
crouch state machines including the keyboard-idle grace window, the velocity composer's both branches, the
locomotion-state transitions and ramp, and frame ordering).

No golden-scene re-check: this change touches nothing under `tools/sr3_viewer.cpp` or any shared rendering
path.

## Known gaps, carried forward honestly (not closed here)

Per the agent's own report: root-motion E stays injectable (its real derivation is a separate, still-open
question - see the "Follow-up" note below, since the answer landed on main while this review was in
progress); 14 tweak constants (`Player_max_run_speed`, `Player_acceleration`, etc.) resolve from the real
table now but will throw `OpenStateError` if a name is ever absent from a loaded file, since no compiled-in
fallback is published anywhere in this project's specs; several subsystem gates (stamina, drunk effect, the
facing-override predicate, NPC-style shaping, moving-support/swimming velocity, move-to-object targets) stay
explicitly OPEN/injectable since this project has no independent source for them; U4 (the follow camera) and
real Havok internals were out of scope by design, not overlooked.

## Follow-up already identified, not done in this merge review

Team A answered the root-motion T/E split while this review was wrapping up (`a1f90ec`): the discriminator
is the animation INSTANCE's own layer flag `0x01` ("phase-synchronised"), not a controller-slot `+0x80` bit
and not `Anim_Movement` - set (T, locomotion cycles) for plain clips/blend-tree States/looping graph clips,
clear (E) for non-looping graph clips. This means `src/player_locomotion.cpp`'s composer can stop treating E
as injectable and derive it from real per-clip data instead, once that data is available from wherever the
animation-instance layer bookkeeping actually lives (likely `sr3animruntime`'s controller, from the just-
merged animation-runtime work, or a new field there) - not done in this pass since it landed mid-review and
the merge itself was already clean and tested; flagged for the next pass to pick up. Separately checked (no
code change needed): this project has never implemented `vehicle_exit_do` or `vehicle_enter_check_done` at
all (confirmed by direct grep of `src/lua_spec_confirmed_stubs.cpp` - zero hits for either name), so the
peer's "check your comments against the corrected wording" note for those two natives doesn't apply to any
code that exists in this repository.
