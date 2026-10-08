# UI renderer U6 update: exact blend/stencil/sampler state (2026-10-03)

Implements spec-vint-render.md Sec15 (UI draw unit U6, `rl_draw`'s own exact Direct3D 9 state) in
`tools/sr3_viewer.cpp`'s `VintQuadPipeline`/`runVintDoc()`, replacing the earlier CHOSEN placeholders:

- **All 5 real `render_mode` blend codes** (Sec15.4: filter/additive/additive_alpha/subtractive/multiply),
  selected per element from its own composed `render_mode` (Sec13.5's 1-5 enumeration) instead of one fixed
  "filter" blend state applied to everything. `multiply`'s alpha channel uses `DESTALPHA/ZERO/ADD` instead
  of the real engine's own literal `DESTCOLOR/ZERO/ADD` for both channels — D3D11's `CreateBlendState`
  validation rejects a colour-only factor (`SRC_COLOR`/`DEST_COLOR`) in the alpha slot outright (found
  empirically, not assumed); D3D9 permits it. **This substitution is an EXACT equivalence, not an
  approximation** (orchestrator correction, independently confirmed from the blend equation's own
  definition rather than taken on trust): a blend factor's alpha output only ever reads that factor's own
  alpha component regardless of its symbolic name, and `DEST_COLOR`'s alpha component IS `dst.a` — the
  identical value `DEST_ALPHA` supplies. So `DESTALPHA` in the alpha slot produces the numerically identical
  result the real engine's own state would, not a forced platform workaround.
- **Real masking** (Sec15.6): mask subtrees were previously not drawn at all ("mask pass not modelled").
  Now implements the real mode-0/1/2 stencil state machine in the sibling-walk (mask children sort first,
  per the existing `sortChildren`; a mask child in mode 0 triggers a real stencil clear + write-mode; the
  first non-mask sibling after it switches to test-mode; mode resets after the loop) — matching
  spec-vint-render.md §11's own "callback slot +0x0c/+0x08" description exactly. Write-mode forces blend-off
  + zero colour-write mask (Sec15.6) and uses a pixel-shader `discard` for the "final alpha above 0"
  stencil-write gate, since D3D11 has no fixed-function alpha test. Added a real stencil-capable depth
  buffer (`DXGI_FORMAT_D24_UNORM_S8_UINT`) — this pipeline previously had none at all.
- **Wrap sampler addressing** (was CLAMP) and the **real half-texel pixel-shader UV offset** plus its
  matching D3D11 -0.5px vertex shift (Sec15.7) — a genuine correction, not just a new value: U3/U4's own
  earlier text claimed "no half-pixel compensation," which U6 found wrong (the offset is real, applied in
  the pixel shader, not the vertex).
- Submission shape (one `Draw()` per primitive, never batched across elements, Sec15.2) was already
  correct — confirmed by re-reading the existing `quad()`, not re-derived or changed.

Not touched (separate, not requested this pass, flagged rather than silently left out): the scissor test
(Sec15.9 item 1) and `background`'s own depth mode 1 (Sec15.5) — every draw still uses depth mode 0 only.

## A real bug found and fixed during implementation

`CreateBlendState` failed outright for `multiply`'s original all-channels-mirrored blend desc
(`SrcBlendAlpha`/`DestBlendAlpha` = `DEST_COLOR`/`ZERO`) — D3D11 validation rejects a colour-component
factor for the alpha slot. Diagnosed via a granular per-stage error message added temporarily to
`initialise()` (kept in the shipped code, not reverted, since it's cheap and avoids the same ambiguous
"vint pipeline state creation failed" the first failure produced). Fixed per the `makeBlend` doc comment
above.

## Verification

Fresh build (`build_verify_uirender`, same directory already verifying the UI renderer merge and
get_dist/player_names_get_all): clean. `ctest -C Release`: **61/61 passed**.

Re-froze the `vint_hud_running_man` golden scene (`tests/golden/build.bat`, confirmed 2-run MD5-identical
determinism before freezing, same discipline as every prior golden). The OLD baseline is preserved at
`tests/golden/vint_hud_running_man_pre_exact/` (not a registered golden scene, a plain before/after
archive), per the orchestrator's own explicit "re-freeze, keeping the old one as the pre-exact baseline"
instruction. Full golden-scene regression (`golden_scene_check.exe`, from `tests/golden/`'s own checkout,
not `build_verify_uirender`): **all 10 scenes PASS**, including the re-frozen `vint_hud_running_man`
(609695 bytes, genuinely different from the pre-exact baseline — the half-texel/wrap-addressing change
shifts pixels even though this specific document has no mask elements or non-filter render_mode elements
to visually exercise those two new features).

**Gap flagged, not closed here**: `hud_running_man.vint_doc` (this golden's own document) has zero mask
elements and zero non-filter `render_mode` elements (already noted in its own original COMMAND.txt's "why
this document" section) — so the newly-implemented blend codes and stencil mask modes are build-verified
and exercised by the renderer's own code paths, but have no dedicated pixel-level regression coverage yet.
A future golden using a mask-bearing document (e.g. the main `hud.vint_doc`, which has 7 real mask elements
per the existing COMMAND.txt notes) would close that gap. Not done in this pass.

No Lua-host mission-drive re-check: this change is entirely within `tools/sr3_viewer.cpp` (a separate
binary from `lua_host_run.exe`) and touches nothing `lua_host_run`/`engine_state.h`/
`lua_spec_confirmed_stubs.cpp` depend on - there is no code path by which this change could affect mission-
drive behavior, so re-running it would be redundant rather than rigorous.
