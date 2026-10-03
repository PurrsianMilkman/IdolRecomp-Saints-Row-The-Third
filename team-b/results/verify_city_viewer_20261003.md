# Streaming city viewer (`sr3_viewer city`) merge verification (2026-10-03)

Verified independently against a fresh build (`build_verify_city_merge`), separate from the implementing
agent's own dev build (`build_verify_city`). Clean merge into `main` (281136c), no conflicts (purely
additive, single file).

`ctest -C Release`: **56/56 passed** (unchanged — no tests added or touched by this batch, as the
implementing agent's own report stated).

Independently re-ran, against the real archives (not taken on the agent's word):

- `city --list` on `sr3_city_0.vpp_pc`: 91 default `CCRRhN` cells, 46 `^name` variants, 0 duplicates —
  matches the agent's reported census exactly.
- `city --lattice-check` on both archives: median |residual| **dx=2.19 dz=1.99 m** for the CC=col/RR=row
  parse vs **dx=1279.93 dz=1119.86 m** for the swapped parse — matches the agent's reported numbers
  exactly (~2 m vs ~1280/1120 m).
- A short scripted flight (`--fly-to`, 40 frames, no textures) over real tiles 0916/1016/1015: ran clean
  end-to-end, load/evict pairs fire at the expected box-crossing frames, no crash. The "check value: found
  0x0" mesh-parse skips seen on a handful of props are the same pre-existing clmesh-reader limitation the
  agent's own report already disclosed, not a new defect.

Verdict: merge is sound. The streaming viewer's own within-cell placement stays the same honest fake grid
`clmesh-tile` already uses — labelled as such in its own stdout banner on every run, not a regression or
new gap.
