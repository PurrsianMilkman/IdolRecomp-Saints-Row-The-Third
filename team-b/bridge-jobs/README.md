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
| `06_ctab_census.json` | `ctab_census` over every archive in the cache (`--cache-dir`): one TSV row per CTAB constant of every `.fxo_pc` VS/PS blob (name, register set incl. samplers, index, count, type), a per-blob table and a summary. Expect `blobs=7276` and `ctab_well_formed + not_present = 7276, malformed=0` (§9.98). For Team A's render-pipeline re-derivation; the TSVs stay on the bus. | yes |

Each job was checked against `bridge/pc_agent.py`'s own `validate()` before it was committed.

## Reading results: before/after

```
python3 bridge/bridge_client.py wait <id>                       # results land in ~/crreish-bus/results/<id>/
python3 team-b/tools/bridge_diff.py <before dir or .tsv> ~/crreish-bus/results/<id> --out diff.md
```

`bridge_diff.py` finds the known outputs anywhere under each side (mission drive per-mission changes,
summary `key=value` lines, stub-hit deltas, vint_doc per-file layout results). For the first mission
re-run, use `team-b/results/verdict_stub_hits_with_missions.tsv` as the "before" (the only §9.143 output
in the repo). Plain-stdout validators (job 05): `diff` the two runs' `step*.stdout.txt`.
