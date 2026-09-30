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

Each job was checked against `bridge/pc_agent.py`'s own `validate()` before it was committed.
