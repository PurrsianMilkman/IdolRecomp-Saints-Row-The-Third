# Project Crreish: notes for Claude sessions

- Read `CLOUD-START.md` (rules for cloud sessions) and `TEAMS.md` (roles, branches, work queue) first.
- Know which role you have: Manager, Team A (specs) or Team B (implementation). If Team B, never open
  anything in `team-a/` other than `spec-*.md`, and never look at Team A's bridge results.
- Game data is only reachable through `bridge/` (see `bridge/README.md`). Never commit game content or
  anything copied out of the game files; only text results that describe them.
- Commit small verified steps on your own branch; put evidence in the commit message.
