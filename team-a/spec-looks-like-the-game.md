# Saints Row: The Third — "Looks like the game" index (2026-10-19)

The track is low priority, behind First Playable. This file indexes what Team C needs to make free roam look like the game: ground/street, sky, sun and time-of-day colour, material shading, and HUD art. Each row points to the owning spec section. This file only adds the street diagnosis summary and the residuals.

Labels: **FINAL** = byte- or instruction-exact, codeable as written; **HIGH** = strongly supported, one link not traced; **GUESS** = chosen reading, to be confirmed; **MISSING** = no spec.

## 1. Coverage (2026-10-19)

| # | item | before this pass | after | where |
|---|---|---|---|---|
| 1a | Street/ground render source | PARTIAL: `0x2237` named as pavement carrier; tile 0915 wrongly called "sparse" (~170 vertices) | **FINAL** | `spec-zone-data-format.md` §13.2, **§13.5.2**; `spec-terrain-format.md` §5 item 2 (2026-10-19 correction) |
| 1b | Walkable ground (collision) | FINAL | FINAL | `spec-zone-data-format.md` §13.5.1 |
| 1c | Mesh sub-block decode, chain rule, strips | FINAL | FINAL | `spec-zone-data-format.md` §10.4/§10.5; `spec-vertex-format.md` §5/§6/§12.13 |
| 1d | Near-origin local-space blocks (placement) | OPEN | OPEN (skip them; recipe in §13.5.2) | `spec-zone-data-format.md` §13.5.2 Residual |
| 2a | Sky data (dome, clouds, mountains, sun/moon textures, LUTs) | MISSING (only `skybox_mesh_filename` names) | **FINAL** | `spec-tables-environment.md` **§19.4** |
| 2b | Sky dome colour (`Hemisphere_colors` order, dome VS) | MISSING | **FINAL** | `spec-tables-environment.md` §19.4 |
| 2c | Clouds / mountains pixel shading | MISSING | HIGH (inputs FINAL, summary math) | `spec-tables-environment.md` §19.4 |
| 2d | Sun/moon sprites (orbitals) | PARTIAL (records undescribed) | **FINAL** | `spec-tables-environment.md` **§19.3** |
| 3a | Sun direction from time of day | MISSING (§18: "OPEN, no angle computation found") | **FINAL** | `spec-tables-environment.md` **§19.2** |
| 3b | Base TOD colour curves (sun, ambient, back ambient, fog, sky) | PARTIAL (blend mechanism only; base file unknown) | **FINAL** (`default_district.xtbl`, 48-slot resample) | `spec-tables-environment.md` **§19.1**, §18, §3 |
| 3c | Weather override layer | FINAL schema, HIGH merge | FINAL schema, HIGH merge | `spec-tables-environment.md` §3, §18, §19.1 |
| 3d | Fog | PARTIAL (fields only) | **FINAL** shader formula; GUESS CPU → `Fog_dist` component mapping | `spec-tables-environment.md` §19.4 |
| 4a | Sun light feed (colour, back colour, frame) | PARTIAL | **FINAL** | `spec-render-pipeline.md` **§25.1**, §23.6 |
| 4b | Deferred directional light model | PARTIAL (register map only) | **FINAL** (bytecode transcription) | `spec-render-pipeline.md` **§25.2** |
| 4c | Material constants, G-buffer, technique roles | PARTIAL | PARTIAL (upload site, ambient setter link, world fog body open) | `spec-render-pipeline.md` §20.12, §22, §23, §25.3; `spec-fxo-format.md` |
| 4d | Texture decode | FINAL | FINAL | `spec-texture-format.md` |
| 5a | vint document format and rendering | FINAL | FINAL | `spec-vint-doc-format.md`, `spec-vint-render.md`, `spec-vint-font-format.md` |
| 5b | HUD data bindings, minimap, GPS line | FINAL | FINAL | `spec-hud-runtime.md` H.1–H.6 |
| 5c | HUD document list and containers | MISSING | **FINAL** | `spec-hud-runtime.md` **H.7 (a)(b)** |
| 5d | HUD art packfiles (bitmap sheets) | MISSING | **FINAL** | `spec-hud-runtime.md` **H.7 (c)**; `spec-tables-environment.md` §17.4 |

Count over the 14 rows that were not already FINAL (all except 1b, 1c, 3c, 4d, 5a, 5b): before, 0 FINAL / 8 PARTIAL-or-OPEN / 6 MISSING; after, 11 FINAL / 1 HIGH (2c) / 2 PARTIAL-or-OPEN (1d, 4c) / 0 MISSING. Fog (3d) counts as FINAL on the shader side, with a GUESS on the CPU mapping.

## 2. Why the street is missing at spawn (tile 0915) (2026-10-19)

**FINAL.** Full detail is in `spec-zone-data-format.md` §13.5.2.

- **Which data draws the street.** The render geometry of the `0x2237` record in `sr3_city~s0915.czn_pc`/`.gzn_pc` (`sr3_city_0.vpp_pc` → `0915.str2_pc`). It is world-space, with an identity transform. It is not `~f0915` (no `.gzn_pc`), not the `0x2244` heightmap, not the `h0`–`h3` `.clmesh_pc` props, and not any separate road or decal format.
- **What the tile holds.** 143 Mesh sub-blocks (chain rule): 98 world-space blocks with 29,836 vertices, and 45 near-origin local-space blocks with 13,566 vertices.
- **Render triangles under the probes.** Each matches the collision ground within 0.02 m:

| probe | height | block | `.czn_pc` | `.gzn_pc` |
|---|---|---|---|---|
| spawn (−1280, −560) | 32.68 m | #98 | `0x0eaf8` | `0x1222bc` |
| road (−1266.9, −584.4) | 32.13 m | #55 | `0x0c264` | `0x05dd9c` |
| sidewalk (−1272.0, −584.4) | 32.15 m | #55 | `0x0c264` | `0x05dd9c` |
| road (−1282.8, −478.6) | 36.04 m | #38 | `0x0b208` | `0x03d8e4` |
| road (−1318.6, −681.3) | 23.81 m | #136 | `0x10edc` | `0x18346c` |

- **The defect is in the build, not the data.** Team B/C's ground loader keeps only the **first** sub-block that has a layout-0/2 channel, and only that channel. In `~s0915` that is block #0 (`.czn_pc` `0x08e20`): a 32-vertex near-origin template (x −22..22, y ±0.2, z −10..13). Nothing is drawn at the spawn, so the sky shows under the street.
- **Fix (spec recipe).** Draw every world-space block and every draw range of the `~s` pair. Skip near-origin blocks until their placement is specified.

## 3. Residuals (2026-10-19)

1. Placement of the near-origin `0x2237` blocks (`spec-zone-data-format.md` §13.5.2; lead `0x2233`).
2. CPU → `Fog_dist` component mapping and the world-pass fog body (`spec-tables-environment.md` §19.4/§19.5).
3. ~~Merge order of `weather_time_of_day` overrides with the base curves inside `0x00BA8540`~~ **CLOSED [2026-10-20 per Team D emulation: FINAL] (D-24; spec-tables-environment.md section 19.1):** base from the curve sets or the override record; from/to cells from one segment; out = f32(f32(F x f32(1-t)) + f32(T x t)); flag `0x012E6B78` cleared by the script weather override. Still open: loader of `default_district_night`/`_special` (§19.5).
4. Consumer of TOD packet `+0xF0` (90° elevation blend, likely shadows) and the tail of the curve sampler `0x00BA2E30`.
5. `V_ambient_render`/`V_back_ambient_render` setter → register link; the material-constant upload call site (`spec-render-pipeline.md` §25.3, §20.12.10).
6. Cloud and mountain pixel-shader math is summarised, not transcribed line by line (`spec-tables-environment.md` §19.4; `tools/scratchpad/looks_2026-10-19/sm3dis.py` reproduces the listing).
7. vint critical-resource selector 1 meaning; PC controller-glyph sheet swap (`spec-hud-runtime.md` H.7).

## 4. Emulation checks proposed for Team D (2026-10-19)

These are listed for the orchestrator; Team D was not contacted.

1. `0x00B9E670` (cdecl `out*, f32 H, f32 yaw, f32 minElev`; byte `0x01310EF8` = 1): H = 12.0, yaw 0, minElev 40 → out (0.34369, −0.82975, 0.43977); H = 6.0, minElev 40 → (0.80786, −0.56206, −0.17735); H = 7.5, minElev 15 → (0.96810, −0.25056, 0).
2. `0x00B9EA40` (cdecl `out*, rec*, f32 H`; rec `+0x20` rot x, `+0x24` rot y, `+0x28` distance, `+0x58` time offset): rec (−110, 10, 0, 4.5), H = 12 → (−0.43173, 0.86817, −0.24471); rec (45, 12, 0, 0), H = 6 → (−0.14702, 0.70711, −0.69166).
3. `0x00BA36E0` (thiscall; slots `+0x00`, presence `+0xC0`, default `+0xF0`) with keys 10 = 0, 19 = 0.6, 29 = 0, 42 = 0: slot 24 → 0.3; slot 45 → 0.0 (lerp 0 → 0); with key 42 = 1.0 instead: slot 45 → 0.8 (w = 0.2), slot 47 → 0.6667 and slot 0 → 0.6667 (w = 5/15 both, from the 47 wrap span).
4. `0x00BA5F80` (cdecl `out*, src*`; `0x01310EF9` = 1, `[0x013110D4]` = 1.0): src (1.0, 0.692243, 0.483099, 16.5) → (16.5, 11.42201, 7.97113, 16.5).
5. `0x00BA2E30` (thiscall curve, `f32 H`): H = 12.25 → slot 24, frac 0.5; H = 23.9 → slot 47, frac 0.8 (and the blended value against slot 0).
