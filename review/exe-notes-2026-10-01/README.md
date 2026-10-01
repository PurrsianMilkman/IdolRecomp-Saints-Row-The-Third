# Executable interpretation notes, 2026-10-01 (Team A)

Written by Team A while the bridge results came in; all in our own words (clean-room grep clean), no pasted
decompiler output. They are the working layer between the raw dumps on the private team-a bus branch and the
spec text; every finding that is labelled in a spec cites the bridge job it came from.

- `interp_<job>.md` — interpretation of one bridge job (nzxf, dksj, bgcx, yduu, mnao, lgdz, fvfp, gdhw, jfue, nnlt).
- `rederive_<spec>.md` — NEEDS-EXE re-derivation of one spec against the depth-0 batch dumps
  (`rederive_27.md`/`_28.md` are the §27/§28 re-derivation of spec-lua-api-behaviour).
  `rederive_tables-vehicle-world.md` is complete (finished just after the wrap-up) but NOT yet applied to its spec.
- `nextdump_<spec>.txt` — residual dumps per spec, one `func|xref|range|ptrs` line each; `nd2job.py <name> <file>
  <out.json>` turns one into a bridge job (then run `team-a/ghidra/check_jobs.py`).
- `exe_index/<spec>.txt` — which dump file in batch jobs `20261001T123123-team-a-ytgi` (tables) /
  `20261001T123128-team-a-dvje` (other formats) answers each address a spec's desk review asked for.
- `REDERIVE_BRIEF.md` / `APPLY_REDERIVE_BRIEF.md` — the two briefs used (interpret on Fable, apply on Opus).
