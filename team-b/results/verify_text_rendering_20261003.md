# Real Vint text rendering merge verification (2026-10-03)

Merges worktree `agent-a2286c5e7f1950599` (commit `aecc736`) as `51e8747`: real text rendering for
`sr3_viewer vint-doc` (spec-vint-render.md Sec12.2/Sec14, spec-vint-font-format.md, UI render unit U5).

New `sr3font` library: the `.vf3_pc` binary font reader (byte-exact on all 6 shipped files — header,
kerning/glyph/texel tables, the 6-record kerning-pair layout), the default named font table (`font_body`/
`font_header_pc`/`ug-debug`, the "anything else" language-code row only — the 3 non-default arrangements
and derived/scaled game fonts are explicitly out of scope, labelled in code), the metric callbacks
(including the real kerning-lookup quirk — the walk does not re-check the left index of the record it
stops on, reproducing the shipped `'`+`T` kerning bug exactly), and per-glyph draw geometry with the
near-edge-only half-texel UV inset. Full text layout (style stack, line breaking/wrapping, horizontal
alignment within the widest line, vertical centring per line) and all 4 markup tags (`[format]`/`[/format]`/
`[color:]`/`[scale:]`/`[image:]`) — `[image:]` was NOT cut despite being optional in scope, and is used by
one shipped document. `sr3_viewer vint-doc` now draws real text by default through the same
`VintQuadPipeline::quad()` bitmaps use, so render_mode/mask-mode state applies identically to text.

## Review

Reviewed the diff directly against the agent's own real branch point (`3a4105d`, not current `main`'s tip,
to avoid the spurious-deletion artifact from spec files Team A added after the worktree branched) — clean,
additive, 9 files, 2567 insertions/38 deletions, zero overlap with any file `main` touched since that
branch point. Read `include/sr3font/vint_font.h`/`vint_text.h` and `src/vint_font.cpp` directly: careful,
well-labelled CHOSEN calls for every genuinely ambiguous spec point, honest OPEN handling for the
localisation-key form of `text_tag` (never fabricated a lookup), faithful reproduction of the kerning
quirk and the measure/draw mismatch for a character outside the font.

## Verification

Fresh build (`build_verify_text`, separate from the agent's own `build_verify_input`... wait, its own build
dir): clean, exit 0. `ctest -C Release`: **63/63 passed** (61 pre-existing + 2 new: `synthetic_vint_font_test`,
`real_vint_font_test` — the latter checks every spec-vint-font-format.md §1/§2.1/§7 number against all 6
real shipped font files, and every row of spec-vint-render.md §14.3's own "emulated reference vectors"
table, i.e. real shipped-code output, not a reading of it).

`tests/golden/build.bat` rebuilt (minimal, additive change: `vint_font.cpp`/`vint_text.cpp` added to the
`vint-doc` reader section's compile line, confirmed by direct diff against the pre-change backup). Both
goldens re-frozen with real text (`vint_hud_running_man`, `hud_mask`); the previous placeholder-text
baselines preserved at `vint_hud_running_man_pre_text/`/`hud_mask_pre_text/` (plain archive folders, not
registered scenes, same convention as the existing `_pre_exact` folder). Full golden-scene regression: **all
11 scenes PASS** from main's own checkout, including both re-frozen scenes byte-exact against their new
baselines.

**Visually inspected both re-frozen captures directly** (not just trusted the byte-compare): `hud_mask`
shows clearly legible text — "GANG OPERATIONAL 10/20", "GANG OPERATION" in purple (a real `[color:]`
markup override), "$12,000", "888/888", "88" — correctly positioned and coloured.
`vint_hud_running_man` (the Genkibowl mini-game HUD) shows "MONEY SHOT", "ETHICAL", "TIME BONUS", "5X",
"88", and correctly-rendered Japanese glyphs ("大当り", "時間のボーナス") — confirms the font/glyph system
handles non-ASCII characters correctly, not just the ASCII the two golden documents' English strings cover.

No Lua-host mission-drive re-check: this change is entirely within `tools/sr3_viewer.cpp`/the new
`sr3font` library, a separate binary from `lua_host_run.exe`, touching nothing it depends on.

## Known gaps, carried forward honestly (not closed here)

- **Localisation-key `text_tag` form**: 191/484 drawn texts across the full 159-document population are
  localisation keys shown raw (e.g. `MAINMENU_DOWNLOADS`) instead of looked up, since this host has no
  localisation string table. Neither golden document is affected (both use plain strings). Team A is now
  writing a localisation string-table spec (per the orchestrator) — raw keys stay until that lands.
- **Spec discrepancy, already relayed and Team A-confirmed**: spec-vint-font-format.md originally claimed
  `jap_nobdr`/`ug-debug_nobdr` textures were absent from `misc.vpp_pc`; they're present (confirmed by this
  agent, independently confirmed by Team A per the orchestrator's follow-up) — only `fnt_chinese` is
  genuinely absent. The 2048-wide texture pages are also now CONFIRMED.
- Out of scope, labelled: the 3 non-default font-language arrangements, derived/scaled game fonts,
  `line_frame_*` (spec itself: "exercised by no shipped document"), resource slot `+0x28` (string width,
  own caller untraced per spec).
