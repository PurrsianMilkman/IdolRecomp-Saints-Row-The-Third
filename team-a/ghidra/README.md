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
