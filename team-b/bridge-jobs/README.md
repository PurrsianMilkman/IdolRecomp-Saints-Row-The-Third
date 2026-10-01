# Team B bridge job queue

Jobs waiting for the PC bridge (see `bridge/README.md`). Submit in this order once the bus is live,
from a checkout of `claude/crreish-team-b` whose HEAD is pushed (`"ref": "auto"`):

```
python3 bridge/bridge_client.py submit team-b/bridge-jobs/<file>.json
```

| File | What it checks | Needs game data |
|---|---|---|
| `01a/01b/01c_msvc_build_and_tests.json` | MSVC build of every target (`ALL_BUILD`) and the 42 synthetic suites, split in three because a job holds at most 20 steps. Confirms the cloud-phase portable-build change (zlib target, `_WIN32` guards) didn't break Windows. Pass = every run step exits 0 and prints its "passed" line. | no |
| `02_mission_drive_rerun.json` | `lua_host_run` over the real scripts after `zscene_is_loaded` (HANDOFF §9.143 follow-up). Compare `verdict_mission_drive_summary.txt` and `verdict_stub_hits_with_missions.tsv` with §9.143 (9/49 past `_start`); read `zscene_is_loaded_open_branch_hits`. | yes |
| `03_vintdoc_validate.json` | `sr3vintdoc` population sweep over `interface_startup.vpp_pc` and `interface.vpp_pc` (HANDOFF §C). | yes |

| `04_ls_cache.json` | Lists `packfiles/pc/cache` so population jobs can name every archive explicitly (the agent does not glob). | yes |
| `05_realdata_regression_fuzz_fixes.json` | Re-runs `validate_container_decode`, `validate_clmesh` (dlc1-3) and `validate_mesh` after the fuzz fixes (container reservations, DEFLATE-ratio bound, material-name reservation, mesh stride check). Compare against HANDOFF's recorded baselines: container decode mode (a) 4,272/4,272 and mode (b) 385,168/385,168 Ok (full population - this job covers a subset, so expect all-Ok, not those totals); clmesh dlc1-3 4,232/4,232 land on EOF (§ "GO/NO-GO"); mesh 549/549 stride law and g-length (§9.63.9 table, archive set not recorded - read the per-archive lines). Any refusal the new mesh stride check causes shows up as a `failed` count. | yes |
| `03b_vintdoc_raw_count.json` | Re-runs job 03 without `--dump`, with the raw u32 at `0x1E` recorded per file (`u32_1E` column) plus a per-version histogram and spec §3.2's refutation test `hdr[0x16] < 0x22 + 4N`. Job 03 (`zlbw`) found only 1/256/257 at `0x1E` and 68 rows too large to fit; this gives Team A the raw values (Request 7). No game text is collected. | yes |
| `06_ctab_census.json` | `ctab_census` over every archive in the cache (`--cache-dir`): one TSV row per CTAB constant of every `.fxo_pc` VS/PS blob (name, register set incl. samplers, index, count, type), a per-blob table and a summary. Expect `blobs=7276` and `ctab_well_formed + not_present = 7276, malformed=0` (§9.98). For Team A's render-pipeline re-derivation; the TSVs stay on the bus. | yes |

Each job was checked against `bridge/pc_agent.py`'s own `validate()` before it was committed.

## Upload limits (read before trusting a missing file)

The PC agent uploads each collected text file up to 4 MB and at most 24 MB per job
(`bridge/pc_agent.py` `max_artifact_bytes` / `max_job_upload_bytes`); anything over is listed under
`files_not_uploaded` in the result JSON, not silently lost. Checked against the tools' real output shapes
(synthetic runs, 2026-09-30):
- `lua_host_run` (jobs 02): every file is small except `verdict_hook_fires_detail.tsv` (~42 KB per script,
  so ~34 MB for the 804 real scripts) — it will be skipped by design; nothing in `bridge_diff.py` needs it.
- `ctab_census` (job 06): the constants table is written normalised and split into
  `ctab_census_constants_NNN.tsv` parts under 3.5 MB each, so it survives the per-file limit.
- `vintdoc_validate` (job 03): per-file TSV well under the limit; the `--dump` JSON is one document.
- Plain-stdout validators (job 05): stdout over 64 KB is clipped inline and uploaded in full as
  `_bridge/stepNN.stdout.txt` regardless of `collect`.

## Reading results: before/after

```
python3 bridge/bridge_client.py wait <id>                       # results land in ~/crreish-bus/results/<id>/
python3 team-b/tools/bridge_diff.py <before dir or .tsv> ~/crreish-bus/results/<id> --out diff.md
```

`bridge_diff.py` finds the known outputs anywhere under each side (mission drive per-mission changes,
summary `key=value` lines, stub-hit deltas, vint_doc per-file layout results). For the first mission
re-run, use `team-b/results/verdict_stub_hits_with_missions.tsv` as the "before" (the only §9.143 output
in the repo). Plain-stdout validators (job 05): `diff` the two runs' `step*.stdout.txt`.

## Post-review real-data baseline (jobs 10a-10g)

One full real-data run of every population validator, so later changes can be diffed against it
(`diff` the per-step `step*.stdout.txt`; `bridge_diff.py` for the Lua and vint-doc outputs). Submit them in
any order; each is independent, every run step is `continue_on_error`, and archive lists are explicit
because the agent does not glob. "ALL" = all 38 archives in `packfiles/pc/cache`; the HANDOFF-recorded
subset is used where one is recorded (clmesh = dlc1-3 + sr3_city_0/1, §"go/no-go"; tree = sr3_city_0/dlc2/dlc3,
§9.67; media bank = the four audio archives, §9.57; zone = the ten archives of spec Sec9.4/10; bone palette and
palette set = characters+preload_rigs+dlc1-3, §9.63.6; binding roles = §9.69; vintdoc = interface_startup +
interface, §C; anim = preload_anim, §4.3). Tables and the other population walkers use ALL ("against all 38
real archives", §9.82-§9.97).

| File | Steps | Covers |
|---|---|---|
| `10a_baseline_tables.json` | 14 | the ten `validate_tables_*_population`, `validate_xtbl_population` (no `--saves`), `validate_vehicleinfo_population`, `validate_customization_population` |
| `10b_baseline_geometry_mesh.json` | 11 | mesh, material binding/map, group search, material-id packing, binding roles, pipeline, vehicle, vehicle runs, morph |
| `10c_baseline_world_zone_cutscene.json` | 12 | clmesh, tree, tree wind/LOD, foliage, foliage LOD, zone, zone chain, zone-header extension, asm, cutscene sampler, ctdg/csc |
| `10d_baseline_anim_rig.json` | 12 | anim field28/payload, anim-vs-rig cross-format and stratified (each over preload_rigs and characters), rig, rotfield, bone palette, palette set, pose |
| `10e_baseline_effects_shaders.json` | 8 | fxo header, cefct (collects the occurrence TSV), D3D9 disassembly/CTAB/HLSL SM3 and SM4 populations (the two HLSL ones and peg are Windows-only targets: d3dcompiler, no GPU), `ctab_census` (collects its TSVs) |
| `10f_baseline_containers_textures_audio_ui.json` | 6 | container decode/offsets, peg textures (Windows-only), media bank, `vintdoc_validate` (no `--dump`) |
| `10g_baseline_lua.json` | 6 | `lua_host_run` (tagged list 1490; `verdict_hook_fires_detail.tsv` not collected, too big), and the lua mission/UI, entrypoint, call-site and dynamic-dispatch censuses |

Excluded on purpose:
- Player data: `validate_save_*`, `validate_profile` (default inputs are the owner's save/profile folders);
  `validate_xtbl_population` is included but never given `--saves`.
- One-off investigation tools: `probe_*`, `diag_*`, `measure_*`, `precheck_topology`, `clmesh_probe`,
  `clmesh_lead_probe` (hard-coded Windows cache path, no argv), `tile1018_list`, `tile1018_placement_probe`,
  and the `d3dctest_semantics*` compile probes.
- Not population runs: `validate_animated_pose` (synthetic, no archive input), `numeric_verify_hlsl*` (embedded
  shaders), the single-file `*_dump`/`dump_*`/`list_*`/`match_anim_rig`/`find_small_shaders`/`rank_shaders_*`
  helpers, `smoke_sample`, `vpp_dump`/`vpp_extract`/`save_browser`.
- Need a hand-made input file that is not committed: `mission_package_census` (start-stems file),
  `lua_bare_reference_census` and `xtbl_name_grep` (needles file).
- Need a GPU/display: `golden_scene_check`, `sr3_viewer`, `tree_baseline_render`, `prototype_*`.

### 10h: saves and profile (added 2026-10-01)

The owner approved using their saves and profiles in bridge jobs (TEAMS.md "Data and content rules",
2026-10-01, and direct approval in the Team B session). Job 10h adds the save/profile validators to the
baseline: `validate_save_crc`, `validate_save_snapshot`, `validate_save_summary` and `validate_profile` (each
with its own default folders on the PC: Documents, LocalAppData, cloud-sync backup),
`validate_save_activity_names` over its 20 default archives, and `validate_xtbl_population --saves <each save
folder>` over all 38 archives. The earlier exclusion of these tools in the 10a-10g notes is withdrawn.

### 10i: validate_save_snapshot re-run (added 2026-10-01)

Manager ruling after job hzim: a format validator should not fail because the owner saved again.
`validate_save_snapshot` now gates only format invariants (`[ok  ]`/`[FAIL]`). The spec's Sec10.7 table and the
other save-set figures (build-stamp and sub-version distributions, worked values, sample counts, the 201,363
non-zero-byte total, statistics 163/164 vs the 0x3F0/0x3F8 counters) are a historical sample (`[match]`/`[DRIFT]`).
They print per-row results with snapshot ids (`#n`, listed at load) and do not fail the run. Job 10i runs the
tool with the default mode (expected exit 0) and with `--strict` (the old behaviour, expected exit 1 while the
drift lasts).

### 02f: mission drive after each Team A answer (template, added 2026-10-01)

Pre-written for Team A's Ghidra answers. They arrive in this order: co-op / tutorial / vehicle-store, then the 24 bare
globals (`rand_int` first), then fade / zscene, then the §16.4 preloads. For each answer:
1. Put the confirmed values in `src/lua_spec_initial_state.cpp` (or the stub, for a rule).
2. Update the inventory test in `tests/synthetic_luahost_test.cpp`.
3. Push, then set this file's title to name the answer and the commit, and submit it with
   `bridge_client.py submit`.
The run's `verdict_open_state.tsv` and the `open_state_slots_with_values_at_start=` summary line show which slots
are now filled. Diff it against the previous mission run with `tools/bridge_diff.py`. The baseline for the first
answer is `20261001T003228-team-b-ugaa`, or `20261001T000923-team-b-hxmr` with `--preload-states=spec16.4-highconf`.

### 11: include( census (added 2026-10-01)

`spec-lua-bindings.md` §16.4 leaves one item for Team B to check (OPEN residue 1): does any shipped script call the
bare global `include` with a preload name? Such a call in a gameplay-side script would load that file into the
gameplay state. `lua_include_census` lists every `include` call (script, line, literal argument or
`<non-literal>`) and flags the ones that name a preload file. It writes no script text.
