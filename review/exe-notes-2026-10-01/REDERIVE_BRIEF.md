# Brief: re-derive a spec's NEEDS-EXE units from the executable (Project Crreish, Team A, 2026-10-01)
You are Team A (specifications), clean-room reimplementation of Saints Row: The Third.
Inputs (read-only):
- The spec: /home/user/Project-Crreish/team-a/spec-<NAME>.md. Every unit has a "Review status (2026-09-30)" line;
  the NEEDS-EXE ones name what the executable must settle; OPEN notes "[OPEN — desk review …]" mark the claims.
- The desk review: review/exe-notes-2026-10-01/adv_<NAME>.md
  (its consolidated exe list explains why each address was asked for).
- The index: review/exe-notes-2026-10-01/exe_index/<NAME>.txt
  — one line per address: the dump file (CrreishDump func dump at depth 0, or xref listing) or NOT-DUMPED.
  Dumps come from bridge jobs 20261001T123123-team-a-ytgi (tables) / 20261001T123128-team-a-dvje (others).
For each NEEDS-EXE unit / OPEN note: read the relevant dumps and give a verdict:
 CONFIRMED (claim matches the code — say what you read), CORRECTED (exactly what is wrong and what is right),
 or still OPEN (what the dump lacks; next dump with addresses — callees are not dumped at depth 0).
Labels: "CONFIRMED — disassembly" only for what you read in these dumps; otherwise HIGH CONFIDENCE/HYPOTHESIS/OPEN.
Never guess. Clean-room: own words, no pseudocode, no auto-names, addresses as 0xXXXXXXXX; element names are fine.
Output: review/exe-notes-2026-10-01/rederive_<NAME>.md,
written incrementally: per unit (section number) verdict + evidence (dump file, address) + EXACT spec text change
(sentence to strike → replacement, or note to add) + new Review status line text; then a summary table and a
next-dump list (addresses, func/xref/range). Do not edit any repo file. Reply with a 6-line summary.
