# Spec consistency review — Team A, cloud phase (2026-09-30)

Scope: all 46 `team-a/spec-*.md` files (36,890 lines at the start), checked for internal contradictions,
broken `§` cross-references, stale OPEN flags that a later section already closed, clean-room violations,
and drift between the `team-a/` and `team-b/` copies. No game data was used; everything below is
checked against the spec text itself.

## How it was done

1. **Mechanical cross-reference check.** A script collected every numbered heading in every spec and
   resolved every `§N.M` mention, attributing it to the spec named just before it on the same line (or to
   the same spec). 198 references did not resolve. Many of those were false positives: bold-numbered
   paragraphs, references to `HANDOFF.md`, and misattributions. So every one was triaged by hand.
2. **Nine reviewers, one per group of specs** (`spec-lua-api-behaviour.md` split into three ranges), each
   reading its files in full. Each finding carries a line number, a quote and a CONFIRMED/PLAUSIBLE label.
3. **Seven fixers, one per disjoint group of files.** Each fixer re-verified every finding against the
   spec text before editing. It applied only clear-cut fixes, where the correct text is already established
   in the specs, and listed everything else.
4. **Orchestrator check before each commit.** I read each group's word diff, confirmed that no confidence
   label was raised, confirmed that every "superseded/resolved → §X" pointer lands on text that says what
   the pointer claims, and re-ran the clean-room grep.

House style was kept throughout. Superseded text is struck through or annotated in place, never silently
rewritten, and no heading was renumbered, because section numbers are cited from other documents and from
Team B's code. Every changed spec ends with a `## Changelog` line for this pass.

## team-a / team-b copy drift

At the start of the pass, all 45 `team-a/spec-*.md` files that have a team-b counterpart were
byte-identical to their `team-b/` copies. `team-b/spec-d3d9-sm2-sm3-bytecode.md` is Team B's own
transcription of a public Microsoft spec and has no team-a counterpart, by design. After this pass,
every spec listed below differs from its team-b copy until the manager syncs it.

## Headline findings

1. **Clean-room: verbatim game script in a spec (fixed, commit `43bd711`).** `spec-lua-bindings.md` §17
   quoted 13 lines of shipped `cell_foreground.lua` byte for byte, including a developer comment. It is
   now a plain name→value list. The manager synced both copies. The block remains in the public repo's
   git history, which is the owner's decision.
2. **Clean-room: decompiler-shaped text.** About 110 places across 14 specs quoted Ghidra output: decompiled
   signatures, pseudocode expressions (`(**(code**)(…))()`, `goto LAB_…`), `unaff_ESI`, `sVar1`, `pvVar4`,
   and `param_N` used inside expressions. All were reworded into plain English citing addresses and offsets.
   What remains flagged by the standard grep is prose *about* Ghidra's naming, such as "reject anything with
   `unaff_` registers", or a type name cited in a sentence. That is allowed per `team-a/WALLS.md`.
3. **Stale text contradicting later corrections, unmarked.** This was the most common class. Examples:
   - `spec-zone-data-format.md` still said in seven places that no `{tag,length}` walker exists (§9.5 found
     it).
   - `spec-vpp-container.md`, `spec-fxo-format.md`, `spec-xtbl-format.md` and five more carried the retired
     mode-(a) "only entry 0 is readable" limitation, which §7's physical-offset rule superseded.
   - `spec-audio-format.md` had the compression-mode letters inverted.
   - `spec-low-mips.md` and `spec-format-inventory.md` said type 17 "ships nothing". `spec-asm-format.md`
     §9.3/§10.2 counts 109,219 shipped type-17 entries, all `.cvbm_pc`.
   - `spec-vertex-format.md` / `spec-vehicle-geometry.md` still called `high16` a sub-mesh index; it is a
     vertex-channel index.
   - `spec-save-format.md` still gave `0x4A60` as the cash-gain multiplier at HIGH CONFIDENCE, against its
     own CONTRADICTED note.
   - `spec-anim-format.md` §6c.1's translation branch still used `align2(p+1)`, the ambiguous form that
     cost Team B's first attempt. It now uses the block's own `alignUp2(p)`/`alignUp4(p)`.
4. **Broken or misnumbered cross-references.** About 70 were fixed. Examples: nine `§17.x` references in
   `spec-tables-environment.md` were off by one; eight leftover `§1.x` placeholder references in
   `spec-vertex-format.md` should have read `§12.x`; three "§33" references in `spec-lua-api-behaviour.md`
   should have read `§7.33`.
5. **Stale OPEN flags.** About 150 across the set were marked "Resolved: see §X" in place.

## Per-spec commits

| Commit | Specs |
|---|---|
| `43bd711` | lua-bindings §17 (verbatim Lua) |
| `7e27e89` | vertex-format, vehicle-geometry |
| `df93b7b` | tables-animation, tables-customization, tables-audio-radio, tables-ui-controls, texture, geometry, tree, vpp-container, fxo |
| `1d0ae96` | vehicle-data, tables-diversions, asm, cutscene-camera, audio, customization-data, xtbl, resource-dispatch, vint-doc, conversation, format-inventory, ctorless-types, ai-behavior, mission-packages, terrain, extensionless-types, low-mips |
| `b012f9c` | zone-data, tables-environment, tables-traffic-ai, tables-vehicle-world, physics, effects |
| `124ff0a` | anim, render-pipeline, morph |
| `a531f64` | rig, lua-bindings |
| `a835e33` | save, tables-progression, tables-weapons-combat, world-streaming |
| `428c840` | lua-api-behaviour (~250 edits: 49 refs, 47 stale OPEN, 127 superseded statements, 13 clean-room) |

`spec-foliage-format.md` needed no change.

## Open: conflicts the specs cannot settle (need the executable or the data)

Both sides now carry an inline conflict marker pointing at each other. Nobody picked a side.

### Needs the executable (queued as bridge job `review-conflicts.json` where marked ✱)

| Spec(s) | Conflict |
|---|---|
| lua-api-behaviour §20.14 / §27.2 ✱ | `0x00a525a0` given to both `mission_is_complete` and `cell_is_mission_complete`, with incompatible bodies |
| lua-api-behaviour §20.1 / §3.7 ✱ | setter `0x00a7a830` writes bit `0x8` vs bit `0x01` at vehicle `+0x1d7a` |
| lua-api-behaviour §18.2 / §18.15 ✱ | `0x005e5af0`, `0x008d73d0` cited as globals but are setter-function entries (WALLS.md bug class); the real global is one hop in |
| lua-api-behaviour §2 / §3 / §4.1 ✱ | `0x00ea2596` rounding described as truncation, round-half-even and round-to-nearest |
| lua-bindings §9.3 / §12.7 | getter/setter vtable slot order for `render_mode`/`visible`/`mask` inverted between the two sections |
| render-pipeline §22.5 / §23.6.2 / §23.x | live `rl_renderer` calls cited at vtable `+0x48`/`+0x54`/`+0x58`/`+0x5c`, inside the `+0x10..+0x7c` range §18.10/§20.8/§21 prove are `_purecall` traps |
| render-pipeline §20.12.4 / §22.11 | `+0x68/+0x6c` vs `+0x60/+0x64`; two labels for vtable `+0x40` |
| tables-animation l.145 / l.228 | `anim_blend_trees` sets `+0x2C` bit `0x80000000` vs `0x40000000` |
| tables-animation / tables-traffic-ai | validator `FUN_004C8690` vs `0x004C8780` (one function or two) |
| tables-customization | `Default` row 58 bytes vs `0x50`; store cap 255 vs 254 |
| vpp-container l.186 | which structure `+0x164`/`+0x168` belong to |
| save-format / tables-weapons-combat / tables-vehicle-world | `items_inventory` live byte at `+0x2C` vs `+0x30` |
| save-format | `0x028DC4E0`: loop bound is the count or the capacity |
| world-streaming | category `0x27` literal spelling (`…_cameras` vs `…_camera`) |
| tables-environment | `bitmap_sheets` field at `+0x18` inside a `0x18`-stride record |
| lua-api-behaviour §15.18 / §15.2 | default at `0x012a2d54` vs `0x01117a4c` |
| lua-api-behaviour §9.21/§10.5 vs §15 onward | which of `0x0086f110`/`0x0086f1b0` is the single-target and which the broadcast commit: §15 onward says the opposite of §9.21/§10.5 at ~40 sites. One reading note added at §15.29 item 5; the sites are not individually edited |

### Needs the data (a re-count)

- **tables-traffic-ai / xtbl-format:** 18 vs 33 exponent-form floats in `roadblock_layouts.xtbl`.
- **tables-traffic-ai:** 159 `human_elm` vs a cap of 140 (may be a per-row cap, so possibly not a
  conflict).
- **physics:** what "725/833" measures; 100,379 vs 100,384 in the population.
- **zone-data:** 69 vs 70 Team B failures.
- **tables-environment:** 12 vs 16 instances.
- **audio:** 253 entries vs 251.
- **vertex-format:** the same totals attributed to 20 archives and to 38.
- **vehicle-geometry:** 30/386 vs a population of 393.
- **vpp-container:** 9 vs 10 mode-(a) archives.
- **tables-progression:** the 44th unlockable type and the 12th `Check_Detection` activity are missing
  from their lists.
- **lua-bindings:** orphan sites 18 vs 15; 42 wrappers vs 49 calls; the 57/55/2 split of `lua_setfield`
  sites.
- **rig-format:** 70 candidates vs 29 + 40; six vs seven call targets.

## Open: structural and policy items for the Team A owner

- **References into `team-a/HANDOFF.md`.** Team B cannot read HANDOFF, and a few hundred spec references
  point into it. Most are methodology asides and harmless. Two problems remain:
  - A few are load-bearing: the fact they cite is stated only in HANDOFF. The worst one, the §4.7/§4.8
    header-address correction in `spec-lua-api-behaviour.md`, is now stated in the spec itself.
  - **`HANDOFF.md` contains several `## 27.` sections** (the live resume note plus archived copies), so
    "HANDOFF §27.x" is ambiguous. Several §27.x targets have also been rewritten since they were cited
    (render-pipeline cites about 19 of them).

  Recommendation: a follow-up pass that restates each load-bearing HANDOFF-only fact in the spec, and that
  gives archived resume notes distinct headings.
- **`WALLS.md` references** in lua-api-behaviour, lua-bindings and others: Team B cannot open that file
  either. They are not load-bearing.
- **`spec-output.md`** is cited from about 12 specs but deliberately unpublished. It is now annotated at its
  first mention in each spec.
- **render-pipeline l.1145** names `shader-map.md`, an external document that is not in this repository.
  Naming it is not a clean-room violation, but Team B cannot follow it.
- **rig-format:**
  - Six paraphrased pseudocode blocks use descriptive names, not decompiler locals, so they are allowed but
    worth converting to prose.
  - There is one raw two-column disassembly listing (l.1780–1790).
  - RTTI class names are used even though the front matter says "no original identifiers". The owner
    should amend either the front matter or the names.
- **Numbering gaps and misplacements, left alone:**
  - vpp-container §3.4 and §5.2–§5.8
  - a geometry "3.1.1" heading inside §4.1.2
  - geometry items 11/12 out of order
  - resource-dispatch 5a before 5
  - render-pipeline: a §23.6.2 block that sits inside §23.6.3

## Method notes for the next pass

- The mechanical checker's false-positive rate was high (about 60%) because many specs number items
  as bold paragraphs, not headings, and cite HANDOFF sections without naming the file. A future checker
  should parse `**N.M**` paragraph labels as targets and treat a bare `§` next to "HANDOFF" as external.
- Two reviewer "fixes" were themselves wrong and were rejected by the fixers on re-reading:
  - the extensionless-types "four patterns" rewrite;
  - one render §6.6 row.

  Verifying before applying was necessary.

## Follow-up (same day): self-containment pass for Team B

The manager asked for this follow-up. Team B cannot read `team-a/HANDOFF.md` or `team-a/WALLS.md`, so every
reference to them in the specs was checked: about 220 HANDOFF references and 70 WALLS mentions across 32 specs.

- **HANDOFF headings.** The four archived copies of old resume notes, inside §38, §41, §42 and §43, no longer
  use `## 27.`. They are now `### N.27 Archived resume note` with subsections `#### N.27.k` (commit
  `536b06a`). Only the live resume note is §27.
- **Load-bearing facts.** Six facts that were stated only in HANDOFF or WALLS are now in the specs:
  - vehicle-geometry §11.6: the "zero CreateVertexDeclaration calls" premise is refuted;
  - render-pipeline §12.1: the `DAT_0351f8f8` slot refutation;
  - render-pipeline §22.2: the material-handle cache;
  - physics §4.4.6(h);
  - tables-animation §1.3;
  - tables-vehicle-world §1.4.

  Each of these is either restated inline or cited to the spec that states it. The §4.7/§4.8 address
  correction in lua-api-behaviour §5.2 was restated in the main pass.
- **Repointed references.** About 75 `HANDOFF §27.x` references were repointed to the archive that actually
  holds the cited text: §36 for the full "[archived] 27.4–27.9" standing practices, and §30/§31/§38/§41 for
  dated items. References to the old §27.4 "ruled out" list now point to `WALLS.md`, where that list moved
  on 2026-09-28. Bare "§5" references that meant HANDOFF are now explicit.
- **Left as is (about 30, none load-bearing).**
  - References to pre-2026-09-20 §27.2 text, which no archive preserves. The facts are stated in the
    specs.
  - References to the on-hold `.czn_pc` interior.
  - 11 attributions that credit HANDOFF or WALLS with a point neither file contains: 6 in
    lua-api-behaviour, 2 in lua-bindings, 2 in effects and 1 in conversation. The point itself is stated
    in the spec each time, so only the attribution is wrong.

## Provenance downgrades (manager ruling, 2026-09-30)

A render-pipeline desk review found claims whose CONFIRMED label rested on a runtime capture made outside
this project's own tooling. The manager ruled that each such label is downgraded to HYPOTHESIS. The claim
text stays, together with a provenance note. Each claim is re-derived only from clean sources (our own
disassembly through the bridge, or Team B's CTAB reader output over the shipped shaders), and CONFIRMED is
restored claim by claim with the new evidence. **Team B: check any code that depends on these.**

| Spec / section | Claim (downgraded to HYPOTHESIS) | Clean re-derivation route |
|---|---|---|
| render-pipeline §20.12.5 | `projTM` (c28) is a fused view-projection; `IR_World2View` (c48) is the pure view; the register assignment holds across 3,398 / 2,357 / 2,203 shaders | Team B CTAB reader over all shipped shaders (register/name census) |
| render-pipeline §20.12.9 | the `projTM` row of the summary table ("CONFIRMED by runtime evidence") | same as §20.12.5 |
| render-pipeline §23.11 addendum | address `14E90F00` bound at draws 1483–1485, with no render-target header | none static; needs the `CreateTexture` path through our own disassembly (render job `20260930T223855-team-a-ueqn`) |
| vertex-format §6.6 | "base decode mechanism": raw non-normalising SHORT2 texcoords with a universal 1/1024 scale and tiling (heading "CLOSED" is now HYPOTHESIS; §12.9.4 and §12.10.5 notes follow it) | the tiling and 1/1024 multiply are already re-derived from the shipped shaders (§12.13 item 5); only the raw SHORT2 declaration type rests on the capture, so it needs the declaration-building code through our own disassembly (`0x00476ca0` path) |

Passages that rest on the same capture but never carried a CONFIRMED label were not struck. They got the
provenance note, and unlabelled ones are now marked HYPOTHESIS: §20.12.12 (`Tint_color` runtime values),
§23.6, §23.6.1 and §23.11 (render-target formats). The light-shader register numbers in §23.6/§23.6.1 were
independently re-derived by Team B's CTAB census, so they do not depend on the capture.

## Desk adversarial review of the Lua and format specs (2026-09-30, evening)

The manager asked for a second pass after the consistency review. Every unit in the two Lua specs, and in
every format spec Team B implements from, now carries a review status line. The verdicts are DESK-PASS,
DESK-PASS with text fixes, NEEDS-EXE, NEEDS-DATA, and VALIDATED-BY-DATA (backed by a Team B full-population
run). A summary follows each spec's front matter.

By the manager's rule, a desk pass does not clear a unit for implementation. Clearing needs re-derivation
against the executable. The executable items are queued as bridge jobs, which are listed in `team-a/HANDOFF.md`
§27.C.

The pass found three kinds of problem. Each spec's Changelog line and the commit messages list the findings.

1. **Live defects an implementer would have hit:**
   - vertex-format §9 step 7b still gave the retracted skinning mapping.
   - vehicle-geometry said paint lives outside the mesh.
   - save-format §8.1 had the bit-writer remainder in the wrong half.
   - The Lua specs described the host-check gate as "mission active" or a head/tail check.
2. **Claims contradicted by Team B's full-population data, now superseded or scoped:**
   - zone tiling: 928/1,002 files in the spec, against 1,002/1,002 in Team B's data.
   - audio clean banks: 530/536 in the spec, against 255/536.
   - effects: the end pointer does not equal EOF in 344 of 1,812 files, and the abstract-base-class idea is refuted by 502 real records.
   - fxo register label: scoped to exclude Team B's 8 mismatches.
   - `spec-vint-doc-format` §3.1 string array: see Team B request 7.
3. **Clean-room and privacy items removed:**
   - verbatim game Lua;
   - player-typed save text;
   - developer machine paths;
   - verbatim engine error strings;
   - decompiler pseudocode, raw x86 and bulk string lists.
