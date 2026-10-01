# Team A headless Ghidra scripts (PC bridge)

Scripts here run on the owner's PC through `bridge/` as `ghidra` steps (headless, `-readOnly`,
`-noanalysis`, against Team A's existing Ghidra project). Their output comes back as text in the
private bus repository and is **never committed here**; specs describe the findings in plain English.

- `CrreishDump.java` — general dump tool. Modes `lua` (find a Lua-registered engine function by its
  registered name and dump it), `func` (dump functions by address), `xref` (every use of an address),
  `str` (find a string and its uses). Usage and options are in the file header.
- `jobs/*.json` — ready-to-submit job files: `python3 bridge/bridge_client.py submit team-a/ghidra/jobs/<name>.json`
  (the job's `"ref": "auto"` builds from the pushed head of `claude/crreish-team-a`).

Checked locally only for Java syntax/typing against API stubs (no Ghidra in the cloud container); the
first live run is its real test.

## Before submitting a job

Run `python3 team-a/ghidra/check_jobs.py <job.json>` first. The PC agent starts Ghidra through `analyzeHeadless.bat`, so an argument containing a character cmd.exe interprets (`% ^ & | < > ! " ( )` or a newline) or splits arguments on (`= , ;`, spaces, tabs) makes the launch fail before Ghidra starts (job `gdvf`, 2026-10-01). Search strings that need such characters cannot be passed as `str` items; find them through the function that uses them instead.
Write CrreishDump options as `key:value` (for example `depth:2`); `key=value` is split into two items by the batch launcher, so the option is silently lost and both halves are treated as search items (all jobs up to 2026-10-01 ran with the defaults depth 1, maxfuncs 40, maxinsn 600, xrefs 25).

## Changing CrreishDump.java (standing rule, 2026-10-01)

Under the owner's full-permission grant, Team A may submit a changed `CrreishDump.java` without waiting for review, provided the change is announced to the manager and stays inside these limits: read-only on the Ghidra project (`-readOnly`, nothing saved; in-memory disassembly/function creation is fine), output only through `writer()`, no network access, no files outside the job's output folder, no process launches. Anything outside those limits needs the manager's review first. Modes so far: `lua`, `str`, `func`, `xref`, `ptrs` (static pointer tables), `range` (disassemble undefined code, capped at 0x10000 bytes), `calls` (raw scan for CALL/JMP rel32, `[abs32]` and dwords targeting an address).
