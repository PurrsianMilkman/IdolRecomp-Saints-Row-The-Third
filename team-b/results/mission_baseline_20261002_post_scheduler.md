# Mission-drive baseline, post resume-scheduler + 0x0153b530 + named-object map (2026-10-02)

Run: `lua_host_run.exe` against the real cache `D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache`
with `tools/lua_all_registered_1490_tagged.txt`, built fresh from `main` at commit `7592a42` (resume-scheduler
+ Killbane-map + weapons-combat fix all merged). Output dir `results/verify_sched_check/` (TSV + summary).
Names, counts, and TSV line numbers only — no error-message text (that's in the TSV itself if needed).

## Aggregate (verdict_mission_drive_summary.txt)

- `missions_with_start_call_ok` = 48/49 (unchanged from pre-scheduler baseline)
- `missions_with_start_suspended` = 39/49 (measured before any tick, so unchanged by definition)
- `missions_start_after_ticks` = finished:31 errored:2 still_suspended:6 killed:0
- `scheduler_passes` = own_resumes:3915 own_thread_errors:2 leftover_resumes:12872 leftover_thread_errors:0
- `live_script_threads_after_missions` = 5 (capacity 256) — was 39 before this fix
- `total_ticks_survived_across_all_missions` = 464 (was 113)
- `stop_reason_histogram`: budget_exhaustion=8 error=0 completion=41
- First-error tally (first_error_message column, all 49): 47 named-object resolution['Killbane'], 1 zscene
  promotion pending 'p_z01' (0x0153b520), 1 dlc2_m02 script-level nil index.

## Per-mission (TSV line, stem, start_call_ok, start_suspended, ticks_survived, start_after_ticks outcome, scheduler_resumes)

| TSV line | stem | start_call_ok | start_suspended | ticks_survived | start_after_ticks | scheduler_resumes |
|---|---|---|---|---|---|---|
| 2 | dlc1_mm_06 | 1 | 0 | 2 | - | 0 |
| 3 | dlc1_mm_05 | 1 | 0 | 2 | - | 0 |
| 4 | dlc1_mm_03 | 1 | 0 | 2 | - | 0 |
| 5 | dlc1_mm_02 | 1 | 0 | 2 | - | 0 |
| 6 | dlc1_mm_04 | 1 | 0 | 2 | - | 0 |
| 7 | dlc1_mm_01 | 1 | 0 | 2 | - | 0 |
| 8 | dlc2_m01 | 1 | 1 | 20 | still_suspended | 220 |
| 9 | dlc2_m03 | 1 | 1 | 9 | finished | 79 |
| 10 | dlc2_m02 | 1 | 1 | 2 | **errored** | 2 |
| 11 | dlc3_m02 | 1 | 1 | 9 | finished | 79 |
| 12 | dlc3_m03 | 1 | 1 | 17 | finished | 169 |
| 13 | dlc3_m01 | 1 | 1 | 9 | finished | 79 |
| 14 | m02 | 1 | 1 | 20 | still_suspended | 220 |
| 15 | sh03 | 1 | 1 | 20 | finished | 79 |
| 16 | m08 | 1 | 1 | 9 | finished | 79 |
| 17 | mm_p_01 | **0** | 0 | 2 | - | 0 |
| 18 | m17 | 1 | 1 | 9 | finished | 80 |
| 19 | m16 | 1 | 1 | 2 | finished | 2 |
| 20 | m18 | 1 | 1 | 18 | finished | 178 |
| 21 | m07 | 1 | 1 | 9 | finished | 79 |
| 22 | m11 | 1 | 1 | 20 | still_suspended | 220 |
| 23 | m24 | 1 | 1 | 9 | finished | 79 |
| 24 | m10 | 1 | 1 | 9 | finished | 79 |
| 25 | m23 | 1 | 1 | 13 | finished | 131 |
| 26 | m05 | 1 | 1 | 9 | finished | 79 |
| 27 | m13 | 1 | 1 | 20 | still_suspended | 220 |
| 28 | m04 | 1 | 1 | 9 | finished | 79 |
| 29 | mm_p_04 | 1 | 0 | 2 | - | 0 |
| 30 | m12 | 1 | 1 | 9 | finished | 79 |
| 31 | sh02 | 1 | 1 | 9 | finished | 79 |
| 32 | m19 | 1 | 1 | 20 | still_suspended | 220 |
| 33 | m03 | 1 | 1 | 9 | finished | 79 |
| 34 | mm_p_03 | 1 | 1 | 9 | finished | 79 |
| 35 | m21 | 1 | 1 | 5 | **errored** | 35 |
| 36 | mm_p_02 | 1 | 0 | 2 | - | 0 |
| 37 | sh04 | 1 | 1 | 9 | finished | 79 |
| 38 | m20 | 1 | 1 | 9 | finished | 79 |
| 39 | m06 | 1 | 1 | 9 | finished | 79 |
| 40 | mm_m16_5 | 1 | 1 | 2 | finished | 1 |
| 41 | sh01 | 1 | 1 | 9 | finished | 79 |
| 42 | m01 | 1 | 1 | 9 | finished | 79 |
| 43 | m22 | 1 | 1 | 11 | finished | 101 |
| 44 | mm_p_05 | 1 | 1 | 9 | finished | 79 |
| 45 | mm_p_07 | 1 | 1 | 20 | still_suspended | 220 |
| 46 | mm_p_06 | 1 | 1 | 9 | finished | 79 |
| 47 | m15 | 1 | 1 | 9 | finished | 79 |
| 48 | mm_m1_5 | 1 | 0 | 20 | - | 0 |
| 49 | m09 | 1 | 1 | 9 | finished | 79 |
| 50 | m14 | 1 | 1 | 9 | finished | 79 |

## Resume-specific errors (start_after_resume_error column, the 2 "errored" rows)

- **dlc2_m02** (TSV line 10): script-level nil index at `dlc2_m02.lua:1245` on local `cp_data` — flagged by the
  implementing agent as possibly related to the CHOSEN `start_checkpoint_arg=0.0`, not yet root-caused.
- **m21** (TSV line 35): a DIFFERENT, deeper OPEN item than the mission's own first-error tally suggests. Its
  first_error_message (Killbane, non-fatal at that point) is not what stops its resume — the resume itself
  fails on a distinct per-name map: `character +0xa41 gender byte (1=female, else male)['#PLAYER1#'] is OPEN
  (spec-lua-api-behaviour.md Sec28.21/Sec28.12/Sec16.8)`. This means '#PLAYER1#' now correctly resolves through
  the named-object map (Sec29's 5-literal fix works), but a SEPARATE per-name character-property map (gender
  byte) is queried next and isn't populated for '#PLAYER1#' — worth checking Sec28.21/Sec28.12/Sec16.8 for
  whether a similarly-scoped small-literal-set fix applies here (the #PLAYER1#/#PLAYER2# sentinels are
  explicitly spec-confirmed special cases elsewhere, so this may be the same kind of narrow, implementable gap).

## Still-genuinely-waiting missions (6, from the implementing agent's report, not independently re-traced line-by-line here)

- 3 in `teleport_coop` (`game_lib.lua:2956`): dlc2_m01, m13, m19.
- 1 in `delay` (`system_lib.lua:114`): m02.
- 1 in `m11_initialize_checkpoint` (`m11.lua:1124`): m11.
- 1 in `mm_p_07_run` (`mm_p_07.lua:116`): mm_p_07.

## mm_p_01 (still fails `start_call_ok`)

Before this fix: stopped on `zscene_prep: 0x0153b530 is OPEN`. After: stops on a later, different blocker —
promotion of pending zscene `'p_z01'` (per-frame driver `0x007258a0` last stopped on OPEN `0x0153b520`,
cutscene state) — confirms the fix moved the blocker forward rather than papering over it.
