# Starting a Claude Code cloud session on this repo

Read this first if you are a Claude Code session running on Anthropic's servers (`claude --cloud`,
claude.ai/code, or the desktop app's *Cloud* option) against this repository.

## What you have, and what you don't

You have this repository only:

- Team A's specifications and logs (`team-a/`);
- Team B's C++17 source, tools and tests (`team-b/`).

You do **not** have, and must never try to obtain, download or reconstruct:

- the game install or executable;
- the game's archives (`.vpp_pc`, `.str2_pc`) or any extracted game files;
- Team A's Ghidra project and dumps.

Those stay on the owner's PC. So in the cloud:

- Nothing can be checked against real game data. Every tool that takes a game path will have nothing
  to point at.
- No new disassembly is possible. Team A's work (writing specs from the executable) cannot be done here.
- The golden-scene reference images are not in the repo, so the golden regression cannot run here.

## Clean-room rules (they still apply)

The project is a clean-room reimplementation. Team B writes code **only** from `team-b/spec-*.md` and
data. If you are doing Team B work, which is anything under `team-b/`:

- Read `team-b/` only. **Do not open `team-a/HANDOFF.md`, `team-a/WALLS.md`, or anything else in
  `team-a/`** except the `spec-*.md` files, which are identical to the copies in `team-b/`.
- Implement only what a spec labels CONFIRMED. HYPOTHESIS and OPEN items stay as clearly labelled
  stubs. Never invent a value to make something work; record the gap instead.
- Specs are Team A's to change. If you find a contradiction, write it up. Don't edit the spec.

## Work that fits a cloud session

1. **Portability and CI.** Make the platform-independent libraries (`team-b/include`, `team-b/src`,
   except the D3D11 renderer and Windows-only tools) build with GCC or Clang on Linux. Make the
   `tests/synthetic_*_test.cpp` suites run, and add a GitHub Actions workflow for them. These tests use
   synthetic inputs, not game data.
2. **Spec consistency review.** Look within the specs for internal contradictions, broken `§`
   cross-references and stale OPEN flags that a later section already closed. Report them in a new file
   `review/spec-consistency.md`. Don't edit the specs.
3. **Lua host work that needs no game data.** Implement CONFIRMED functions from
   `team-b/spec-lua-api-behaviour.md` as pure logic with unit tests. The runtime validation against
   real scripts has to happen later on the owner's PC.
4. **Documentation.** Write build instructions and a per-library overview for new contributors.

## Where things stand

See `team-b/STATE.md` (real denominators) and the resume notes at the top of `team-b/HANDOFF.md` and
`team-a/HANDOFF.md`. One item, the `.czn_pc` object-stream interior, is **on hold pending the owner's
explicit decision**. Don't work on it.

Commit small, verified steps. Put each claim's evidence in the commit message or in `review/`.
