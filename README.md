# IdolRecomp Saints Row The Third

A clean-room reimplementation of **Saints Row: The Third** (PC, 2011), aiming at a full recompilation of
the game from specifications and the player's own game files.

**No game files are in this repository.** No executable, no archives, no textures, models, audio or
scripts from the game, no decompiled code, no Ghidra projects and no rendered images of game assets.
To run anything here you need your own legitimate copy of the game.

## How the project works

The work is split between four teams on two sides of a clean-room wall:

| | Team A | Team D | Team B | Team C |
|---|---|---|---|---|
| Side | Dirty | Dirty | Clean | Clean |
| Role | Studies the original game and writes specifications | Runs the original game's own code in an emulator and compares it with ours | Builds the reimplementation (`sr3.exe`, world, AI) and merges all clean-side work | Builds menus, HUD, saving, activities, visuals and Team D's fixes |
| Reads | The game executable (disassembly) and the game's data files | The game executable, Team A's specs and the clean side's code | Only specifications and the game's data files | Only specifications and the game's data files |
| Produces | `spec-*.md` documents | `spec-differential-*.md`: test vectors and match/mismatch findings | C++17 code, a D3D11 renderer, a Lua 5.1 host | C++17 code on its own branches, merged by Team B |

- **Only specification documents cross the wall,** from the dirty side to the clean side, and every one passes
  a leak check that blocks decompiler-style names and code. The clean side never sees the original code.
- **Teams on the same side talk to each other** (A with D, B with C). The dirty side can never message the
  clean side; the clean side asks for specs through written requests.
- **Team D is the referee.** It feeds thousands of inputs through a routine from the original executable and
  through our version, then reports exactly where they differ (findings D-1 to D-28 so far). Fixes go
  through Team A as corrected specs, then to Team C or Team B.

Specs describe file layouts and behaviour in plain language, with confidence labels: **CONFIRMED** (empirical,
emulated or disassembly), **HIGH CONFIDENCE**, **HYPOTHESIS** and **OPEN**. Anything unresolved is recorded as
open rather than guessed. See [`TEAMS.md`](TEAMS.md) for the full setup.

**Running Claude Code in the cloud against this repo? Read [`CLOUD-START.md`](CLOUD-START.md) first.**

## What's here

- `team-a/`: Team A's specifications (`spec-*.md`), its running log (`HANDOFF.md`), and `WALLS.md`,
  the list of approaches that were tried and ruled out.
- `team-a/` also holds Team D's published results: `spec-differential-results.md` and the
  `spec-differential-vectors-*.md` test vectors.
- `team-b/`: the reimplementation, including Team C's merged work.
  - `include/`, `src/`: C++17 libraries. There are readers for about 19 binary formats (containers,
    textures, meshes, rigs, animation headers, trees, zones, vehicles, collision meshes, shaders and
    saves) and for about 236 data tables.
  - `tools/`: the viewer (`sr3_viewer`), rendering prototypes, the Lua host runner, and census and
    validation tools.
  - `tests/`: synthetic unit tests, plus the golden-scene regression harness. Reference images aren't
    included, because they are renders of game assets; regenerate them locally from your own copy.
  - `third_party/lua51/`: stock Lua 5.1.5 (MIT licence, unmodified). `third_party/zlib/`: zlib (zlib
    licence).
  - `STATE.md`: current status, with real denominators. `HANDOFF.md`: the full engineering log.

## Status (October 2026)

**First Playable reached.** `sr3.exe` boots, skips the intro videos, and shows the main menu. New Game and Load
Save work; you can free roam on foot, in cars, boats and aircraft with the HUD and minimap; the pause menu
resumes, saves, quits to the main menu or quits to the desktop; and a saved game reloads where you left it.

- Every covered file format is validated against the full shipped population, not a sample.
- A D3D9 shader bytecode translator to HLSL SM4/5: all 7,276 shipped shaders translate and compile.
- A deferred D3D11 renderer: characters, vehicles, trees, buildings, the street ground, sky, sun direction and
  time-of-day colour.
- An embedded Lua 5.1 host that registers all 1,490 engine functions and runs all 804 shipped scripts.
- Systems built and tested: mission scripting, HUD/minimap, save/load, respect and progression, radio,
  physics, weather, roads and traffic, pedestrians, NPC and combat AI, aircraft and boats, death and arrest,
  GPS, homies, menus, and eight activities.
- Missions: 14 of 49 complete end to end in the last full automated run; the rest are being worked through.
- Original-code checks: 28 routines compared against the original executable with tens of thousands of
  inputs; most mismatches found are already fixed.

It is a playable build, not a finished game: it does not look like the original yet, and missions are incomplete.

## Building

Requirements: CMake, MSVC (Visual Studio 2022) and the Windows SDK (for D3D11). Most tools need a path
to your own game install; see each tool's header comment and `team-b/HANDOFF.md`.

## Legal

Saints Row and Saints Row: The Third are trademarks of their respective owners. This project is not
affiliated with or endorsed by them. The code and documents here are original work, released under the
MIT licence (`LICENSE`).
