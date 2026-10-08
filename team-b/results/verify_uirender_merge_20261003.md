# UI HUD bitmap renderer merge verification (2026-10-03)

Merges worktree `agent-a422b99a371906549` (commit `7d60e47`) as `f426eaa`: the first on-screen Vint UI
render (spec-vint-render.md Sec12, unit U3). New `sr3_viewer vint-doc` command draws every real `bitmap`
element of a real `.vint_doc` through D3D11 (real sheet sub-images, real composed geometry; `source_se` as
an extent not a corner, no half-texel offset); `group` draws nothing (Sec12.3, CONFIRMED empty); `text`
gets an honestly-labelled magenta-outline placeholder (real layout/glyph drawing wasn't specced at the
time this agent ran — it is now, see spec-vint-render.md §14/spec-vint-font-format.md, separately synced).

`EngineState::loadVintDocument()` gains an optional `VintImageSizeResolver` parameter implementing Sec12.1's
image-setter side effects (`unscaled_size`/`source_nw`/`source_se` reseed, `auto_offset` reapply) at
load-order position. Reviewed directly (not taken on the agent's own report): every new code path is gated
on `imageSize &&` (the resolver being non-null), so every existing caller that omits it — which is every
caller except `sr3_viewer vint-doc` itself — is provably unaffected. Confirmed empirically too (see
mission-drive section below).

## Merge

Worktree branched from `b544558`; main had since moved on via the `action_play_do`/Sec34.5 work, touching
`engine_state.h`/`lua_engine_state.cpp` in non-overlapping regions. `git merge --no-ff` auto-merged clean,
zero conflicts. Diffed the agent's own commit against its direct parent (not against current main, which
would have shown spurious deletions from files main added after the worktree's branch point) to confirm
the real changeset: `CMakeLists.txt` (+7/-3), `engine_state.h` (+60), `lua_engine_state.cpp` (+72), new
`tests/synthetic_vintdoc_document_test.cpp` (104 lines), `tools/golden_scene_check.cpp` (+73),
`tools/sr3_viewer.cpp` (+846, new file content for the `vint-doc` command).

## Manual golden-file copy

The agent's own permission classifier blocked it from writing to shared `tests/golden/` directly
("Modify Shared Resources"). Copied manually from the worktree: `tests/golden/vint_hud_running_man/`
(`COMMAND.txt`, `baseline_capture.png`, `baseline_stdout.txt`) and `tests/golden/build.bat` (backed up
main's old copy first as `build.bat.bak_pre_uirender`). Diffed the two `build.bat` versions directly:
additive only — an optional `GOLDEN_PROJ` override (unset falls through to the exact same hardcoded main
path as before) plus one new self-contained reader section (`vint_doc.cpp` + the 13 `sr3luahost` sources +
Lua 5.1 compiled as C++ into a new `vint_luahost.lib`), matching the agent's own description exactly.

## Verification

Fresh build directory `build_verify_uirender` (separate from the agent's own `build_verify_vintdoc`):
`cmake --build build_verify_uirender --config Release` — clean, exit 0. `ctest -C Release` — **61/61
passed**.

`tests/golden/build.bat` rebuilt from main's own checkout (not the worktree), then
`golden_scene_check.exe` run from main's own `_bin/`: **all 10 scenes PASS**, including the new
`vint_hud_running_man` scene byte-exact against its copied-in baseline (`capture.png`, 575688 bytes) — this
confirms the manual golden copy was done correctly, not just that the agent's own worktree copy worked.

Mission-drive sanity check (`--tick-budget=721 --probe-unresolved-names`, same 49-mission real archive run
used throughout this session): **`missions_start_after_ticks=still_suspended:23 finished:10 errored:14
killed:0`**, byte-identical aggregate to the immediately-prior baseline
(`results/verify_s345_mission_drive/`). Full per-mission `verdict_mission_drive.tsv` diffed directly
(not just the aggregate) — **zero differences, confirmed with `diff`'s own exit code 0**. This was the
expected result (viewer/rendering tooling, no Lua-host stub behavior touched by a caller that omits the
new optional parameter) but confirmed rather than assumed, per this project's own standing discipline.

## Verdict

Clean merge, no regressions, all verification layers pass. Worktree/branch cleanup follows.
