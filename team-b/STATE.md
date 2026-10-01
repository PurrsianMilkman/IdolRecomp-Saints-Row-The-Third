# STATE — real denominators, updated as of 2026-09-30 (cloud phase; see HANDOFF.md resume note)

**Cloud phase 2026-09-30:** portable GCC build + CI (45 ctest suites), fuzzing (16 harnesses, 5 allocation bugs fixed), 105 validation tools built on Linux, `sr3vintdoc` CONFIRMED-scope reader, Lua host with no invented engine values (OPEN-state refusals). See HANDOFF's cloud entries. **Correction:** see the mission-driving row — the `vint_is_std_res` claim, and its first correction, were both partly wrong. **Implemented pre-review, pending exe re-clearance** (manager decision 2026-09-30, option (a): kept working, no behaviour change; fixed or reverted to a labelled stub when Team A's re-derivation lands): `game_UI_audio_play` (§2.2), `game_get_key_name` (§2.3), `coop_is_active` (§3.1), `set_ignore_ai_flag` (§3.4), `ai_add_enemy_target` (§3.9), `on_take_damage` (§3.13). No new work from §1-§5 until cleared.

Adopted from external decompilation-project research (`AI-WORKFLOW-INSTRUCTIONS.md` §12: "keep a
state-of-the-project file with real metrics and denominators... the orchestrator can then answer 'how
far along are we' from numbers instead of estimates"). This is a snapshot table, not prose — every
number below is sourced to a `HANDOFF.md` section with the full real-data validation behind it; this
file does not re-derive or re-argue anything, it just makes the numbers scannable in one place. Update
this file whenever a HANDOFF §9.xx entry changes one of these counts; if this file and HANDOFF.md ever
disagree, HANDOFF.md is authoritative (this is an index, same relationship as `MEMORY.md` to memory
files elsewhere).

## Binary/container format readers (`include/sr3*` domain libraries, excluding the xtbl-table-group
libraries below and the render-side libraries, which get their own tables)

| Format | Status | Real-data validation | HANDOFF |
|---|---|---|---|
| `.vpp_pc`/`.str2_pc` container | Complete | after the container-offset fix every entry decodes: mode (a) 4,272/4,272 (was 9) + mode (b) 385,168/385,168 = **389,440** entries; the 76 PEG c-files and 847 `.fxo_pc_dx11` that were unreachable are inside those totals (the old "76+847+…" added subsets) | §9.78 |
| Save data | Complete (confirmed fields only) | 16 real saves, CRC reproduced | §9.74, §9.79 |
| `.asm_pc` manifest | Complete (non-empty sub-groups) | 805/805 exact | §9.71 |
| `.fxo_pc`/`.fxo_pc_dx11` shader wrapper | Complete | 844/844 + 847/847 exact | §9.76, §9.77 |
| `.cpeg_pc`/`.gpeg_pc` texture container | Complete | 70,524/70,524 c-files, 76,651/76,651 records | §9.77 |
| `.anim_pc` header (not keyframe payload) | Complete (header scope) | 4,209/4,209 | §1 table |
| `.cmorph_pc` morph targets (container, not dequant) | Complete (container scope) | 1,541/1,541 | §1 table |
| `.ccmesh_pc`/`.csmesh_pc` geometry (raw arrays) | Complete (raw-array scope) | 1,803/1,803 | §1 table |
| `.cfmesh_pc` foliage mesh | Complete | 19/19 (full population) | §9.75 |
| `.ctdg_pc` conversation | Complete | 4,984/4,984 | §1 table |
| `.csc_pc` cutscene camera | Complete | 96/96 | §9.73 |
| Shared "Mesh" sub-block (vertex channels + draw ranges) | Complete | 549/549 paired meshes, 4,312,125/4,312,125 indices | §9.7, §9.10 |
| `.rig_pc` skeleton | Complete | 585/585 | §9.11, §9.15 |
| `.csrt_pc`/`.gsrt_pc` tree | Complete | 28/28 | §9.67, §9.72 |
| `.clmesh_pc` collision/static-prop | Complete | 100,384/100,384 | §9.64, §9.65 |
| `.czh_pc`/`.czn_pc`/`.gzn_pc` zone | Solid (object-stream body OPEN, parked on Team A) | 38,152/38,152 geometry | §9.55, §9.86 |
| `.ccar_pc`/`.gcar_pc` vehicle assembly | Complete | part records 372/372 files, 9,536 parts (our run; 393/9,951 is the spec's figure); channel decode, material bindings and `high16` join 393/393 | §9.25, §9.69, §9.84, §9.89, §9.102 |
| `_media.bnk_pc` audio wrapper | Structure only, no Wwise/Vorbis decode | 536/536 | §9.57 |
| `sr3d3d9bc` D3D9 SM2/3 bytecode disassembler | Stage 1 complete | 7,276/7,276 blobs, 0 unknown opcodes | §9.97 |

**Count: 19 binary/container format readers, all with a full-population real-data validation figure
attached — zero readers in this project ship without one.**

## `.xtbl` table-group coverage (typed readers over `sr3xtbl`'s shared foundation)

| Domain | Tables implemented | HANDOFF |
|---|---|---|
| Environment (weather/lighting/VFX) | 26/27 (1 no-loader, correctly skipped) | §9.80 |
| Progression (economy) | 26/26 | §9.81 |
| Traffic/ambient/AI | 31/31 | §9.82 |
| Weapons/combat | 21/22 (1 no-loader) | §9.83 |
| Animation data | 14/16 (2 no-loader) | §9.90 |
| Audio/radio/foley | 16/16 | §9.91 |
| UI/controls/QTE/camera-presets | 17/17 | §9.92 |
| Vehicle-interaction/world-objects | 24/24 | §9.93 |
| Side-activities/diversions/stunts | 25/25 | §9.94 |
| Character/vehicle customization | 34/34 live (+12 confirmed-dead, +1 deferred to `sr3customization`) | §9.95 |
| Customization items/outfits (older, narrower spec) | item catalogue + outfits + vehicle-cust variants | §9.85 |
| Runtime vehicle-info entry | 1 struct (`0xB80` bytes), all fields | §9.84 |

**Count: ~236 individual `.xtbl`-family tables have a typed reader, real-data-validated against every
shipped archive location (not just the first found), across 10 domains + 2 earlier-scoped libraries.**
Every domain's own harness prints per-table PASS/FAIL with the exact row count against the spec's own
stated figure — see each cited section for the individual numbers; this table intentionally doesn't
re-list all ~236 of them.

## Rendering milestones (the engine-on-top-of-readers phase, §9 of HANDOFF)

| Milestone | Status | HANDOFF |
|---|---|---|
| Rendering scaffold (window, swap chain, device) | DONE | §9.2, §9.6 |
| Real texture on screen | DONE | §9.3 |
| Real game mesh on screen (own shaders) | DONE | §9.7 |
| Animation playback (CPU skinning) | DONE | §9.53, §9.56 |
| Multi-object scene + free camera | DONE | §9.62 |
| Multi-channel character-mesh rendering (`pose`/`animpose`) | **DONE (§9.105)** — the 2 known multi-channel meshes (`alien_e01`/`alien_es01`) no longer refuse; MD5-identical regression on single-channel meshes, real fix independently rebuilt/rerun | §9.105 |
| Vehicle rendering in `sr3_viewer` | **DONE (§9.111)** — new `sr3_viewer vehicle` command, real per-range shader draw (§9.104 mechanism) folded into the viewer as an ordinary target; regression PASS (mesh/pose byte-identical, 4-way MD5 agreement); `car_4dr_genki_0` 20/20 ranges/6 shaders, `car_4dr_standard03_4` 13/13/8 shaders, both independently reproduced exactly | §9.111 |
| D3D9 shader bytecode: structural disassembly | DONE, population-clean, 7,276/7,276 | §9.97 |
| D3D9 shader bytecode: per-opcode HLSL semantics (spec) | DONE (35 real opcodes) | §9.98 |
| D3D9 CTAB constant-reflection reader | DONE, 7,276/7,276 well-formed, 0 disagreements | §9.98 |
| D3DDisassemble independent cross-check oracle | DONE, 7,276/7,276 exact match vs Microsoft's own tool | §9.98 |
| HLSL translator (bytecode → HLSL), SM3 reference target | DONE — 100% translate, 100% D3DCompile success (7,276/7,276), 24/24 numeric checks | §9.100 |
| HLSL translator, SM4/5 target (what D3D11 actually needs) | **DONE — 100% translate, 100% D3DCompile success (7,276/7,276), ZERO warnings, 54/54 numeric+structural checks** | §9.100 |
| Draw one real mesh with its ORIGINAL shader pair | **MECHANISM CONFIRMED, per-range real shaders (§9.104)** — all 20 real draw ranges of `car_4dr_genki_0` group 0 resolved via their own material's on-disk `shaderHash` to 6 distinct real shaders (not one assumed shader), drawn through the real D3D11 pipeline; visually-confirmed, substantially more complete car-body output than the single-shader version. Several constants/the texture remain honestly-labelled placeholders; a rear-edge artifact remains unexplained | §9.100, §9.101, §9.102, §9.103, §9.104 |
| *Note (2026-09-30)* | **HYPOTHESIS-BASED since the render-pipeline provenance downgrade:** every real-shader render above and below (`sr3_viewer vehicle`, the `prototype_real_shader_draw*`/`prototype_lit_*` tools, the deferred-lit renders) fills `projTM` (VS c28×4) with a fused view×projection and `world2view` (c48) with the view matrix — spec §20.12.5/§20.12.9, now HYPOTHESIS pending re-derivation from shipped shaders (bridge job 06, `ctab_census`). Results unchanged; the label is the change. | spec-render-pipeline.md changelog |
| Real vehicle paint colour (`Base_Paint_Color`) | **DONE (§9.106)** — real per-material runtime float4 constant in the vehicle's own `.ccar_pc` record (array B/C name-hash→Vector4 table), NOT `.cvtf_pc`/xtbl/a texture; `car_4dr_genki_0` real values independently reproduced byte-exact for 3 distinct paint groups + 2 other shaders' own paint constants. Constant-fill now uses real engine defaults throughout (§9.106 2nd addendum) | §9.106 |
| First real per-pixel-LIT render, deferred pipeline (G-buffer → 1 light → material) | **DONE (§9.109)** — all 6 real shaders' real role4/role6 passes, real `IR_LBufferSampler`@s12 wired end to end; hood hue matches `Base_Paint_Color`'s real channel ordering (B largest). Light colour/ambient now REAL (`weather_time_of_day.xtbl` noon/Overcast row, §9.109 addendum) — pure-white blow-out improved 3.04%→2.52%, not eliminated. G-buffer format CHOSEN (Team A exhausted the real search); light DIRECTION still CHOSEN (no real field in this table). `Tint_color` confirmed the shader's FINAL multiplier and confirmed zero everywhere in the exe — real runtime producer still unlocated | §9.109 |
| Golden-scene regression baselines | **5/5 frozen — ALL FROZEN** — brad, 2 vehicles (`vehicle_genki`/`vehicle_standard`), `zone_tile` (vertex layout code 24, §9.116), and `tree` (vertex layout codes 11/12/13, §9.120, re-frozen §9.124 with all 3 present LOD slots/materials — trunk+branch+needles, 1,612 triangles, old group0-only baseline preserved) via `tools/golden_scene_check.cpp` (byte-exact PNG+stdout diff, self-test verified for all 5). Tree Position is FLOAT16×3 at `+0`, HIGH CONFIDENCE not CONFIRMED (`spec-vertex-format.md` §12.12) — no CPU/GPU consumer found yet, `+0`/`+16` duplicate pair's staleness question still open; Normal/Tangent CONFIRMED. All 9 synthetic test suites depending on `sr3mesh` independently rebuilt/rerun, all pass | §9.110, §9.112, §9.115, §9.116, §9.120, §9.124 |
| Lua 5.1 host execution scaffold | **DONE (§9.113)** — real embedded Lua 5.1.5 (`third_party/lua51/`, MIT), all 1,430 real registered names stubbed across the real gameplay/ui state split, `game_lib.lua` loaded first per spec sequence, all 804 real shipped scripts load-tested (804/804 OK, 0 disagreements vs `sr3lua`'s own parser) and pcall-tested (788/804 gameplay, 798/804 UI — low-hit-count top-level runs are the expected, honest result for hook-defining scripts). Independently rebuilt from fresh source (incl. compiling vendored Lua myself) and rerun, every number incl. the 5-name/56-call stub-hit breakdown reproduced exactly | §9.113 |
| Engine→script hook-firing pass + `thread_*` scheduler | **DONE, superseded by per-script real-state restriction (§9.140)** — was 138 hooks × 804 scripts × 2 states unconditionally (221,904 theoretical max); now restricted to each script's own real `.asm_pc`-derived state tag (ui:727/gameplay:67/OPEN:10/conflict:0), all 8 `spec-lua-bindings.md` §13.5 dual-registrations wired — current real tally **12,687 ok / 14,485 erred** (was 21,041/32,852 pre-restriction), bit-identical on independent rebuilt rerun. `tools/lua_runtime_stub_ranking.tsv` superseded by the all-inclusive hook-triggered ranking in `verdict_stub_hits_by_hook...` files (see §9.140's own citation). minimal `thread_new/yield/kill/check_done/close` scheduler unchanged. | §9.117, §9.122, §9.140 |
| Per-script entry-point naming census | **DONE (§9.118)** — 760/804 real scripts (94.5%) define a top-level `<ownStem>_<suffix>` function, 7,623 matches/3,724 distinct suffixes; dominant convention `init`/`cleanup` (699/673 scripts) directly confirms `spec-lua-bindings.md` §8.4's sprintf hook templates from the real-script side; `start`/`run` (63/51 scripts) is real, partial evidence for a mission-entry naming convention | §9.118 |
| Per-script real Lua-state tagging (`.asm_pc` container-kind/type) | **DONE (§9.140), `runChunk()` restriction DONE+verified (§9.143)** — real population split ui:727/gameplay:67/OPEN:10/conflict:0 (804 total). Combined tally 12,157 ok/12,545 erred (was 21,041/32,852 pre-§9.140). Wrong-state leak closed for several classic hooks; a few reveal a real separate bug in their own state, now exposed cleanly. | §9.140, §9.143 |
| Mission-driving pass (`<stem>_start`, real modal missions) | **DONE (§9.143)** — 49/49 real missions found+started, only 9/49 get past `_start` cleanly. 40/49 hit a real watchdog-caught infinite loop, traced to busy-poll stubs that never return true: `fade_is_fully_faded_out` (30.8M calls), `fade_is_fully_faded_in` (1.67M), `zscene_is_loaded` (714K) — concrete next-implementation targets. The 9/49 that get past that uniformly hit `vint_is_std_res` (missing) next. New `verdict_stub_hits_with_missions.tsv` ranking — this is what Team A's next spec tranches should follow. **⚠ CORRECTED TWICE 2026-09-30 (cloud phase):** bridge job `knyf` (at `d0e6843`) settles it with per-mission `first_error_message`: the 9 do hit "attempt to call global `vint_is_std_res` (a nil value)" at `vint_lib.lua:96` (the first correction's "0 mission-pass calls" was wrong: the stub-hit ranking never sees a call to a nil global). It is registered in the UI state (§13.7); `vint_lib.lua` is OPEN-tagged, so the host runs it in both states and the gameplay state gets its functions without `vint_*`. Very likely a host dual-state artifact, not a missing engine function (stretch 5c). **After invented values were removed** (job `tkjl`, at `88cbe87`): still 9/49 past `_start`, 0 watchdog loops; every mission stops on a named OPEN engine value (co-op session ×36, tutorial table ×10) — HANDOFF stretch 5c. | §9.143 |
| `.clmesh_pc` real per-material shader constants (B/C name-hash/vec4 arrays) | **DONE (§9.141)** — `sr3clmesh::LevelMesh::materialShaderConstants()`, 95/95 (100%) real B-hash→CTAB-name matches on the tower's materials; VS/PS resolution generalized beyond `_v`/`_mv` suffix search, population rate 44/81→58/81 (54%→72%) | §9.141 |
| Deferred-lit render extended from vehicles to `.clmesh_pc` props | **DONE, honest partial (§9.139/§9.141/§9.142)** — real render achieved (14.15% silhouette coverage, up from 1.15%), but a "position-correlated colour" artifact's root cause is genuinely OPEN (`eyePos` empirically refuted as the cause, traced to `Fog_dist`/other placeholder-zero constants producing a GPU-`min()`-clamped NaN; full isolation would need per-constant A/B across ~5 shader families, correctly out of scope so far) | §9.139, §9.141, §9.142 |
| Golden-scene regression baselines | **8/8 frozen** — the original 5 (brad, 2 vehicles, `zone_tile`, `tree`) + `clmesh_lite_fixh`/`clmesh_airport_controltower` (§9.136/§9.138) + `clmesh_airport_controltower_lit` (§9.139, re-frozen §9.141 with the old sparse baseline preserved under renamed files). All independently rebuilt from fresh objects + reran by me, byte-exact + `--selftest` OK, every time a new scene lands | §9.110, §9.112, §9.116, §9.120, §9.124, §9.136, §9.138, §9.139, §9.141 |
| `.vint_doc` UI document reader (`sr3vintdoc`) | **CONFIRMED-scope reader DONE, cloud phase 2026-09-30** — header, string table, all record shapes as positioned decoders, synthetic suite, population validator. **No full-document walk** (3 layout facts OPEN, requested from Team A). **No real-data run yet** (bridge job `03` queued). | HANDOFF cloud stretch 1 |

## Missions/UI Lua domain (data-side census, §9.108)

Real `sr3lua` Lua 5.1 parser (public grammar). 804/804 real shipped scripts parsed clean, plain text (not
bytecode). 1,282 distinct engine-provided names by construction, reconciled exactly against Team A's
corrected 1,430-name `lua_register` roster (a 3rd, 113-name loop registrar found after the first
1,325-name pass, §9.108): **1,181 called-and-registered = the real implementation priority list**
(`tools/lua_reconciliation_called_and_registered_1181.tsv`, sorted by real call count); 101
called-but-unregistered (`tools/lua_reconciliation_called_not_registered_101.tsv` — mostly `vint_*`/
`thread_*`/Lua stdlib, small real long-tail, the earlier `game_*` gap is now fully closed); 249
registered-but-never-called, lowest priority (`tools/lua_reconciliation_registered_not_called_249.tsv`).
Reimplementation of any specific engine function is NOT STARTED — this is the sizing/priority census only.

## Open / parked / walls count

- **Genuinely OPEN** (spec-confirmed gap, not a wall, may resolve with more work or more data):
  `.czn_pc` object-stream body (parked, Team A side); `sr3_viewer` `.ccar_pc` integration; the
  dequantisation formula for `.cmorph_pc`; array 1/2/3/5 semantics in `.ccmesh_pc`/`.csmesh_pc`;
  speaker-id pre-image names in `.ctdg_pc`; `IFC`'s comparison-operator bit table and `RSQ`/`NRM`
  edge-case behavior in `spec-d3d9-sm2-sm3-bytecode.md` §12.
- **WALLS** (tried, ruled out, documented so nobody re-tries): see `WALLS.md`, 10 entries as of
  2026-09-29 (one 2026-09-28 entry on hash-joining vehicle shaderHash was itself corrected/reopened
  same day — see its struck-through note and `HANDOFF.md` §9.101; a new entry added same day on the
  submeshIndex/high16 stale-header-comment lesson, `HANDOFF.md` §9.103).
- **Explicitly out of scope**: see `WALLS.md`'s own last section, 4 entries.
