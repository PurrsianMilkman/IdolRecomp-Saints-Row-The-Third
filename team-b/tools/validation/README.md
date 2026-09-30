# Real-data validation harnesses

These are the programs that produced the population statistics quoted
throughout `HANDOFF.md` — the "549/549 paired meshes", "585/585 rigs",
"22,274/22,274 bone-name hashes" figures and the rest. They run the
readers over the shipped `.vpp_pc` archives and compare what comes out
against the numbers the `spec-*.md` documents claim.

They lived in the session scratchpad while the work was happening, which
is a temp directory that does not survive the session. `HANDOFF.md` cited
them by that path, so the evidence behind its headline numbers would have
evaporated along with it. They are preserved here instead.

## These are NOT part of the build

They are standalone diagnostics, deliberately not wired into
`CMakeLists.txt`:

* they need the game archives, which are not part of this repository;
* they print statistics for a human to read rather than asserting
  pass/fail, so they are not tests in the sense `tests/` means;
* several are exploratory and encode hypotheses that were later refuted.

The self-contained tests that *do* gate the build are in `tests/`
(`synthetic_*_test.cpp`), and those need no game data.

## Building one

`build_one.bat <harness-name>` is the short route, run from the project
root. **Invoke it from PowerShell or cmd, not through the Bash tool.**
`cmd /c 'tools\validation\build_one.bat foo'` from Git Bash opens an
interactive shell and silently does nothing — it prints the Windows banner,
exits 0, and leaves the old binary in place, which looks exactly like a
successful no-op build. From PowerShell:

```
& .\tools\validation\build_one.bat validate_anim_field28
```

Success prints `BUILD_OK <name>`; **check for that line**, since exit code 0
alone does not distinguish a build from the failure above. A
`'vswhere.exe' is not recognized` warning is benign noise from
`vcvarsall.bat` and does not affect the build.

It links against the `.obj` files in `build_verify/`, so **that directory is
a build dependency, not disposable output** — deleting it breaks this route
until the full reader build is recreated.

There is no `cmake` in this environment. Each harness is a single
translation unit linked against those object files. The underlying pattern,
from a Developer Command Prompt (`vcvarsall.bat x64`):

```
cl.exe /nologo /std:c++17 /EHsc /O2 ^
  /I include /I third_party\zlib ^
  tools\validation\validate_pose.cpp ^
  build_verify\byte_view.obj build_verify\format.obj build_verify\hash.obj ^
  build_verify\payload_locator.obj build_verify\container.obj ^
  build_verify\content_validation.obj build_verify\material_block.obj ^
  build_verify\geometry_block.obj build_verify\mesh_block.obj build_verify\rig.obj build_verify\pose.obj ^
  build_verify\zlib\*.obj ^
  /Fo build_verify\ /Fe validate_pose.exe
```

Then point it at an archive:

```
validate_pose.exe "...\packfiles\pc\cache\characters.vpp_pc"
```

## What each one covers

| Harness | Backs |
|---|---|
| `validate_pose.cpp` | §9.14 / §9.14.1 — the synthetic-pose skinning test, with its scrambled-index control and the pose-free shell count that resolved `nightblade`. Now also the regression harness for `include/sr3rig/pose.h` (stage 1 of animation playback): its per-vertex skinning loop was extracted into that library rather than kept as a second copy, and it prints two new checks per mesh - "BIND-POSE identity" (all-identity local rotations reduce every Skin_i to Identity) and "HIERARCHICAL vs DIRECT" (the general `computeSkinningMatrices` path reproduces this file's original hand-authored single-joint pose to floating-point precision) |
| `validate_bone_palette.cpp` | HANDOFF §9.62 — the Mesh sub-block's **bone palette** (u16 count at header `+0x38`, u8 rig-bone indices after the channel records) and its **set descriptors** (u16 count at `+0x48`, `[u8 count][u8 start]` pairs after the palette). Sweeps every `<stem>.ccmesh_pc`/`.gcmesh_pc`/`.rig_pc` triple in the archives given (318 across `characters`, `preload_rigs`, `dlc1-3`), counts the structural invariants (complete / in range / max blend index inside the palette and inside the largest set / set starts chain and sum to the palette count — all 318/318), and scores the per-bone cluster residual under four readings with a **shuffled-palette control**: direct `(x,-y,-z)` 0.498 m, palette `(x,-y,-z)` 0.326 m, palette `(-x,-y,-z)` **0.134 m**, shuffled 0.778 m. For the 46 multi-set meshes it prints a per-draw-range vote table and the residual under the per-range majority set (0.111 m), which is what any per-range selection rule must reproduce. Builds with `build_one.bat` (its object list gained the anim + palette objects on 2026-09-13) |
| `validate_palette_set_field.cpp` | HANDOFF §9.63.10 — the **per-draw-range bone-palette SET selector**: an 8-byte record per draw range immediately after the 20-byte draw ranges, `u32` set index at `+0x00`. Over the same 318-triple population §9.63.6 used, it runs four independent lines of evidence and four controls: structural (the array fits 318/318; every value a valid set index 318/318; all zero on single-set meshes 272/272; `arrayEnd + 4 == meshOffset + cLength` 318/318 **and** those 4 bytes are the block's own check value 318/318), a content assertion the field is not used to compute (the named set is big enough for the range's own blend indices — 1,085/1,085 multi-set ranges, against controls at 899 / 924 / 795), agreement with §9.63.7's independent majority-vote oracle (1,070/1,085; the 15 disagreements are classified as tie / oracle-set-impossible / cross-channel / genuine, and only 1 is genuine), and the cluster residual (F 0.1101 m vs oracle 0.1061, concatenated 0.1589, shuffled control 0.6376). Also measures whether a single-vertex-array remap is even well defined — counting **non-degenerate triangles only** — which is how `reynolds` was found to be the 1/318 that is not |
| `diag_palette_set_selector.cpp` | The search that FOUND the above, kept because the method generalises: it takes a known per-range ground truth and scans the whole c-file for any (offset, stride, element size) whose values induce **exactly that partition** of the ranges (`v[i]==v[j]` iff `truth[i]==truth[j]`) — partition-matching, not literal matching, so a set start, a set count or a pointer would all be found. Aggregated over the 46 multi-set meshes one candidate matched 34 and the next-best 8. Also dumps the Mesh header, group records, draw ranges and the region after them, and scores the "unique admissible set by max blend index" rule that was the alternative, no-stored-field answer (670/1,085 ranges unique, 667/670 correct, but only 2/46 meshes fully determined — a clear negative) |
| `diag_range_tail_carriers.cpp` | Scope check for the same field, run BEFORE the reader was allowed to use it (the §9.63 "scoping gap" lesson). Applies only the carrier-neutral structural test — gap == 4 and those 4 bytes are the block's check value — to characters and vehicles: **549/549 on `.ccmesh_pc`, 0/372 on `.ccar_pc`**, with the vehicle byte-`+0x00` histogram spread over ~200 values and bytes `+1..+7` non-zero tens of thousands of times. The structure is character-mesh-specific, and `MeshBlock` is gated accordingly |
| `probe_seam_absolute_displacement.cpp` | §9.56.4's absolute-displacement measurement, unchanged without flags. **`--bone-palette`** (§9.62) reads blend indices through the palette and uses the `(-x,-y,-z)` conjugation; both modes also print a classification-free table over every mesh edge, which is the before/after that does not depend on the dominant-bone split. `brad` + `auto_entrl_extct_shot`, frac 1.0: all-edge max 1.208 m → 0.104 m, edges > 0.25 m 474 → 0 |
| `validate_rig.cpp` | §9.11 — `sr3rig` against 585 rigs, plus the mesh↔rig cross-check |
| `validate_mesh.cpp` | §9.7 — the Mesh sub-block over 549 paired meshes |
| `validate_morph.cpp` | §5 — morph exact-size replay |
| `validate_foliage.cpp` | §6 — foliage / `.cfmesh_pc` |
| `validate_ctdg_csc.cpp` | §4.6, §7 — conversation and cutscene-camera formats |
| `validate_media_bank.cpp` | `spec-audio-format.md` §4/§5 — the `_media.bnk_pc` (`VWSBPC`) block directory over `sounds`/`sounds_common`/`voices`/`cutscene_sounds`. Reproduces 536/536 files walked, 89,631 records, and 260/260 in-archive cross-references to the sibling Wwise SoundBankID, exactly. Carries two controls of its own: the same population re-walked under §4.3's discarded `extra`-ignoring chain, and a value-diversity count behind the cross-reference match rate. It also **contradicts two of that spec's secondary characterisations** — see `diag_media_bank_first_offset.cpp` below and the anchor/`extra` notes in `include/sr3audio/media_bank.h` |
| `probe_bindpose_residual.cpp` | §9.58 — the retraction of §9.56.2's `reggies` "mesh is not at its rig's bind pose" side finding. Measures a mesh's stored stance against its own rig **without using any blend index** (arm droop from the rig's `l-clavicle` rest position to the mesh's extreme +X vertex), prints its own container provenance for both files so a file-selection mismatch cannot hide, and has a `--sweep <archive>` mode that runs the measure over every `<stem>.ccmesh_pc`/`.gcmesh_pc`/`.rig_pc` triple (275 pairs in `characters.vpp_pc`). Established that the shipped humanoid rig is an **A-pose**, not a T-pose, and that `reggies` sits at the population median. Optional `--anim-archive`/`--clip` re-scores the same statistic against a real clip frame via the confirmed §9.53/§9.56 pose path. Builds with `build_one.bat` since 2026-09-13 (its object list now carries the anim objects; before that it needed `animated_pose.obj animation.obj payload.obj sample.obj` added by hand) |
| `validate_clmesh.cpp` | `spec-physics-format.md` §4 / §4.4.6 — the `.clmesh_pc` "Level_Mesh" container and its sub-parser chain, over every archive that carries the format (`dlc1`-`dlc3`, `sr3_city_0`, `sr3_city_1`). The go/no-go test is the one landmark this format has: the trailing nested group is the last structure in the file, so a correct walk ends **exactly** at EOF. **Rewritten 2026-09-13 for the fully-computed single-pass walk (HANDOFF §9.64)**: the MIDDLE is no longer opaque and the tail is no longer searched for, so the ambiguity distribution this harness used to print is gone — the quantity it measured does not exist under a computed walk. Instead it carries **fifteen controls, each disabling exactly ONE confirmed term** (MIDDLE start +4/+16, TAIL start ±16, the material-set name table's leading NUL, the second render group, the `materialCount × 8` lookup array, `roundUp16` instead of the Mesh block's exact c-length, the gated `+0x78` pair costed at the old `kGatedPairBytes = 0` placeholder, the HEAD without array 2's index-list payloads, the trailing group in prose order, `count(+0x70)`+1, `count(+0xfc)`=4, plus the two places §4.4.6's prose is ambiguous and had to be settled by measurement: the collision-hull gate read as mask `0x02` instead of bit index 2, and the do-nothing third branch 16-aligning anyway), scores the **spec-literal one-cursor walk** separately, and censuses the `+0xb8` record, the MIDDLE's byte-length distribution and the gated pair's three branches. See HANDOFF §9.64 |
| `dump_group88.cpp` | HANDOFF §9.60 — decodes `.clmesh_pc`'s `+0x88` nested group (the one `spec-physics-format.md` §4.2 left OPEN as "3 indices or a plane/face descriptor") over the complete 258-file population in `sr3_city_0`/`sr3_city_1` (the only two archives where it is ever populated). Only decompresses a `.glmesh_pc` pair when the matching `.clmesh_pc` actually populates both `+0x88` and `+0x48`, which is why it finished both archives in ~36 minutes against `validate_clmesh.cpp`'s ~1 hour EACH. Writes a full per-element CSV (raw hex, 3×u32, 3×float) plus population-level stats (per-slot min/max, float-magnitude histograms, outer-record padding-field census, count(+0x88)/count(+0x48) co-occurrence). Result: the 12 bytes are 3 little-endian float32s, a local-space position, not either literal reading the spec posed — see HANDOFF §9.60 for the full evidence chain (closed-loop bit-exact checks, cross-tile byte-identity, asset-name/shape correspondence) |
| `diag_media_bank_first_offset.cpp` | The measurement that separated `spec-audio-format.md` §4.2's two conflated claims: the chain rule (reproduced **536/536**, 89,631 records) and the anchor claim "record 0's offset is always `0x800`" (measured **278/536**; the rule that holds 536/536 is `round_up(0x20 + recordCount*16, 0x800)`, i.e. the first block after the header *and the record table*). Written because the first version of the reader implemented both as one predicate and so failed 258 files at record 0 without ever testing the chain — HANDOFF §3's "a refutation is scoped to the CONJUNCTION it tested", in practice |

## A note on reading their output

`validate_pose.cpp` is the one worth studying before trusting any of the
others, because it is the one that failed first. Four of its five metrics
were vacuous on the first run and every one of them looked like a passing
check. What exposed them was running each metric a second time against
deliberately corrupted input and noticing the number did not get worse.
Any new harness added here should carry that control from the start — see
the standing lessons in `HANDOFF.md` §3.
