# hud_mask golden: real stencil masking + non-filter render_mode coverage (2026-10-03)

Closes the coverage gap flagged in `results/verify_u6_render_20261003.md`: `hud_running_man.vint_doc`'s
own golden exercises neither of U6's two biggest additions (real stencil masking, the 5 real `render_mode`
blend codes), since that document has zero mask elements and zero non-filter `render_mode` elements. New
`checkHudMaskScene()` (`tools/golden_scene_check.cpp`) freezes `hud.vint_doc` instead — the main HUD
document, which has both: 7 mask elements (11 elements total render while inside a mask subtree — the mask
quads themselves in write-mode, their masked siblings in test-mode) and 15 non-filter `render_mode`
elements (additive/additive_alpha/subtractive/multiply).

## Visual confirmation before freezing

Read the rendered frame directly (not just trusted the byte count): the ammo-wheel ring (bottom-left) and
the melee-weapon/grenade icon (bottom-right) both render as real, visible circular cutouts — exactly what
real stencil masking should produce, and exactly what the old code (which simply never drew mask subtrees
at all) could never have shown. A square quad in either spot would have meant the masking wasn't actually
working despite the summary line claiming 11 masked elements.

## Verification

`tests/golden/build.bat` rebuilt (picks up the new `checkHudMaskScene()`): clean. Confirmed 2-run
determinism before freezing — same out path (`_work/hud_mask.png`) across both runs, MD5-identical PNG
(`d7c3200162d5211f795117f4a8731e1d`) and byte-identical stdout, same discipline as every prior golden.

Full golden-scene regression (`golden_scene_check.exe`, from `tests/golden/`'s own checkout): **all 11
scenes PASS** (the 10 pre-existing ones unaffected, plus the new `hud_mask`). `golden_scene_check.cpp`
also confirmed to build clean in `build_verify_uirender` (the separate verification tree already used for
the rest of this session's UI renderer work).

## Verdict

The coverage gap is closed: both of U6's major additions now have dedicated, visually-confirmed pixel
regression coverage, not just build-time verification.
