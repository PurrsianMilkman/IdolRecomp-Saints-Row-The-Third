# Brief: apply a NEEDS-EXE re-derivation to its spec (Project Crreish, Team A, 2026-10-01)
Source of truth: review/exe-notes-2026-10-01/rederive_<NAME>.md
(per unit: verdict, evidence, exact spec text change, new Review status line). Edit ONLY
/home/user/Project-Crreish/team-a/spec-<NAME>.md. Cite bridge jobs 20261001T123123-team-a-ytgi (tables) or
20261001T123128-team-a-dvje (others), whichever the re-derivation names.
House rules (strict): never delete — strike ~~…~~ or annotate in place; no renumbering. Labels exactly as the
re-derivation gives them (CONFIRMED — disassembly only where it says so). Units whose NEEDS-EXE is now settled
(CONFIRMED/CORRECTED) get a Review status line "re-derived from the executable 2026-10-01 (job …): … — cleared for
implementation" (or "not cleared: <OPEN items>"). Update the spec's "Review status summary" paragraph with new
counts. Where a correction affects Team B code (the spec's FOR TEAM B notes / the re-derivation says so), keep the
note visible. Clean-room: own words, no pseudocode, no auto-names in code-shaped lines; run
grep -nE '\b(iVar|uVar|piVar|puVar|pvVar|fVar|sVar|local_[0-9a-f]|uStack_|unaff_|extraout_|undefined[0-9])'
and introduce no hits. One 2026-10-01 Changelog line citing the job. Do not git add/commit/push.
Write the next-dump list from the re-derivation to
review/exe-notes-2026-10-01/nextdump_<NAME>.txt
as plain lines "func 0xADDR" / "xref 0xADDR" / "range 0xS-0xE" / "ptrs 0xADDR N" (one per line, nothing else).
Reply with a 6-line summary (units cleared / corrected / still OPEN, Team B-relevant corrections, grep result).
