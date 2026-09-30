# WALLS — ruled-out approaches, read this before starting new investigative work

Adopted 2026-09-28 from external decompilation-project research (relayed via the orchestrator,
`AI-WORKFLOW-INSTRUCTIONS.md` §4: "stop on the walls earlier waves kept rediscovering"). This file
exists so a fresh session or a dispatched agent can check in one place whether an approach has
already failed, instead of re-deriving the failure from scattered `HANDOFF.md` sections. Each entry:
what was tried, why it failed, what evidence would reopen it. Every new-format/new-investigation
agent brief should say "check WALLS.md first" for approaches in its own domain.

Full narrative for any entry lives in its cited `HANDOFF.md` section — this file is deliberately
terse, an index of failure modes, not the story.

## Format/container walls

- **The `<root><Table>` shape is NOT universal for `.xtbl`-family content.** A structural validator
  that requires it will false-positive-reject 260/2,222 real shipped files (blend_tree, state_machine,
  `action_nodes.xtbl`, `node_graph_files.xtbl`, others). Removed as a check, not just disabled.
  Reopen only if a NEW real-data population shows every real file actually has the shape (it doesn't,
  measured exhaustively). — `HANDOFF.md` §9.79, content_validation.h's own history comment.
- **A "fall back to root's children when there's no `<Table>` wrapper" table reader must filter by row
  TAG NAME.** Without it, a metadata/count leaf element (e.g. `<num_objects>`) silently counts as an
  extra row. Caught via `action_nodes.xtbl` (Team A's own tooling had this bug; this project's readers
  were already scoped correctly, but the trap is real and worth checking on every new reader). —
  `HANDOFF.md` §D.
- **A population harness reporting only the FIRST archive location found for a same-named table
  silently undercounts** when a patch/DLC archive carries a superset (or, separately, a genuinely
  different row count — not always the same content). Hit repeatedly this project's own history
  (ammo.xtbl/continuous_explosions.xtbl, then again across the 2026-09-28 table-reader batch:
  audio_banks/audio_personas/foley_engine, vehicle_interaction_info, npc_color_palette/
  customization_materials). Every new population harness must report every location found, never
  just the first. — `HANDOFF.md` §D, §9.91/§9.93/§9.95.
- **A decoder that CAPS output at the expected byte count can "succeed" on a WRONG offset** — a prefix
  of a neighbouring stream happens to have the right length. An oracle must be exact-length-and-stop
  with output NOT silently capped, plus an independent corroborating field. Two of this project's own
  early container oracles were wrong this exact way before the container-offset fix. —
  `HANDOFF.md` §9.78, §D rule 12.
- **16-byte-aligned candidate-offset searches for a draw-group/range array location are ambiguity-
  prone** — the search must accept only a candidate that ALSO satisfies the format's own structural
  invariants (contiguous-from-zero ranges, full index-buffer coverage, valid min/maxVertex), and the
  match-rate-with-no-ambiguity-count must be measured, not asserted. — `HANDOFF.md` (draw-group
  search harness, `validate_groupsearch.cpp`).

- ~~**Hashing a mesh/vehicle material's `shaderHash` field against real `.fxo_pc` filenames does not
  join them, under any hash function.**~~ **CORRECTED 2026-09-29 — WRONG, do not rely on this entry.**
  The join DOES work — the 6 combinations originally tried (`NameHash`/`NameHashCaseSensitive`/
  `vpp::hashFilename` × full-name/stage-suffixed-stem) used the wrong hash function AND the wrong input
  string. The real scheme: CRC-32 (reflected `0xEDB88320`, init `0`, no final XOR — `sr3fxo::crc32Raw`)
  of the filename's **stage-suffix-stripped** stem (strip `.fxo_pc`, then one of 13 stage suffixes per
  `spec-fxo-format.md` §6.6 — `_s`/`_bs`/`_ms`/`_bms`/`_c`/`_mc`/`_bc`/`_bmc`/`_t`/`_ts`/`_fd`/`_v`/`_mv`).
  Independently reproduced 17/17 on real vehicle data plus the tree control value, both exact —
  `HANDOFF.md` §9.101's correction note. **Real lesson to keep**: a negative from a hash-join test is
  only as trustworthy as its exact input string AND hash parameterization, stated explicitly — either
  one alone being wrong produces an indistinguishable false negative. State both before trusting or
  relaying a negative. (Original wrong entry kept below, struck through, per standing practice.)

- **A field known by two names (a header comment's own name, a spec section's own name) can silently
  carry two different open/closed statuses.** `include/sr3mesh/mesh_block.h`'s `DrawRange::submeshIndex`
  was marked "HYPOTHESIS... still-OPEN" a full day AFTER the exact same field, called `high16` in
  `spec-vehicle-geometry.md` §11.1, was CLOSED (`HANDOFF.md` §9.89, 2026-09-28: real disassembly read +
  full-population per-item join, 393/393 vehicles, 90,290/90,290 draw ranges, zero exceptions). A fresh
  session reading the stale header comment re-derived the same closed answer from scratch (`HANDOFF.md`
  §9.103, 2026-09-29) before a peer caught that it wasn't new. Fixed in place. A second, independent
  instance found the SAME day, same root cause: `include/sr3rig/bone_palette.h`'s comment (and two
  matching refusal comments in `tools/sr3_viewer.cpp`) still said the per-draw-range bone-palette-set
  selector was "still OPEN," when that exact question had been closed even earlier (§9.63.10/§9.68,
  2026-09-13/14) — the comment described state as of the section BEFORE the one that closed it and was
  never updated. Both fixed in place. `sr3_viewer.cpp`'s own refusal comments additionally revealed a
  REAL, live functional gap hiding behind the stale text (not just documentation): character meshes
  with multiple channels (e.g. `alien_e01`) still get refused at render time for "which channel" reasons
  that `submeshIndex` already answers — verified directly against real data (28/28 draw ranges across
  all 8 real groups of `alien_e01`, including the specific group the refusal cites, fit their
  `submeshIndex`-selected channel exactly) — the CODE just doesn't read `submeshIndex` per range yet, a
  real, scoped, not-yet-done follow-up flagged in the comment itself, not silently left implied fixed.
  A dedicated sweep agent found this and 8 further real STALE — CLOSED ELSEWHERE instances across
  `include`/`src` (64 real candidates checked project-wide, ~50 confirmed genuinely still open, ~2 nuanced
  wording-only issues, 9 clean stale-and-fixed). Spot-checked 4 of the 9 personally against the cited
  primary source before trusting the rest — all 4 confirmed exact. **All 9 fixed in place** (`sr3anim/
  animation.h`, `sr3render/device.h`, `sr3render/mesh_renderer.h` ×2, `sr3geometry/geometry_block.h`,
  `sr3mesh/mesh_block.h`, `sr3tables_vehicle_world/tables.h` ×2, `sr3tables_environment/tables.h`, plus
  `sr3rig/bone_palette.h`/`tools/sr3_viewer.cpp` above) — compile-checked together, clean. `tools/`
  outside `sr3_viewer.cpp` was only partially covered (~30 hits in standalone diagnostic tools, mostly
  self-describing their own test methodology rather than library field status) — low priority, deferred.
  **When closing an open question, grep for every OTHER name that field might carry (code comments,
  structs, earlier spec sections) and update all of them** — not just the one being read at the time. —
  `HANDOFF.md` §9.103/§9.104's correction, rule 16.

## Statistical/measurement walls

- **Never trust a "success rate" without a random-shuffle control baseline.** A naively-impressive
  match fraction (e.g. hash-sort/alphabetical-sort candidates for mode-(a) physical packing order) can
  be statistically indistinguishable from chance once compared against a shuffled control. —
  `HANDOFF.md` §3.
- **A name-level census (which element names occur, ever) is NOT a per-row presence count.** Don't
  write a fraction like "20/20" for something the instrument only measured as "seen somewhere in the
  file." (`aim_drift.xtbl`, corrected 2026-09-23.) — `HANDOFF.md` §D.
- **Don't narrate a filename PATTERN as if it were a measured field.** An unverified inference from
  naming convention (`heli_*`/`plane_*` implying vehicle class) was wrong for real cases
  (`plane_giant01`/`02` are genuinely `<Helicopter>`-classed). Write down what was measured, not what
  a name suggests. — `HANDOFF.md` §D, §9.84's correction note.

## Cross-team / disassembly-adjacent walls (Team B doesn't do disassembly, but reviews Team A's claims)

- **A whole-binary vtable-offset match, by itself, is not proof of a specific method/class identity** —
  small-displacement offsets (`+0x24`, `+0x28`, `+0x3c` etc.) recur across unrelated objects/classes
  constantly; a match needs an independent corroborator (argument count against the public API,
  calling-convention check, or a value/byte-offset convergence with real DATA this project can
  measure) before treating it as confirmed. Bit Team A's own render-pipeline work more than once
  (the `FUN_004a2e30`/`FUN_004a8900` false lead; `rl_renderer`'s own 28-slot shared-vtable-target
  puzzle, later resolved as a `_purecall` idiom rather than 28 distinct methods). Team B's own
  contribution when reviewing such a claim is exactly what closed `high16` (§9.89): an independent
  real-data population check the disassembly claim would have to survive. — `HANDOFF.md` §9.89,
  `spec-render-pipeline.md` §16/§18.10 (Team A's own document, cited here for cross-reference only).

## Explicitly out of scope (not walls in the "tried and failed" sense — deliberate scope decisions)

- Collision-simulation renderer feature (user chose "keep closing reading items," 2026-09-14).
- Whole-binary searches of any kind (outside this project's cleanroom boundary by construction).
- `.czn_pc` object-stream body internals (parked on Team A's side).
- Recomputing tree `+0xF0`/`+0xF4` scaled-vs-unscaled (spec silent on which; exposed raw instead of
  guessing).
