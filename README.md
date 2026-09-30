# Project Crreish

A clean-room reimplementation of **Saints Row: The Third** (PC, 2011), aiming at a full recompilation of
the game from specifications and the player's own game files.

**No game files are in this repository.** No executable, no archives, no textures, models, audio or
scripts from the game, no decompiled code, no Ghidra projects and no rendered images of game assets.
To run anything here you need your own legitimate copy of the game.

## How the project works

The work is split between two teams, following the clean-room method:

| | Team A (`team-a/`) | Team B (`team-b/`) |
|---|---|---|
| Role | Studies the original game and writes specifications | Builds the reimplementation |
| Reads | The game executable (disassembly) and the game's data files | Only Team A's specifications and the game's data files |
| Produces | `spec-*.md` documents | C++17 readers, a D3D11 renderer, a Lua 5.1 host |

Specifications flow one way, from Team A to Team B. Team B never sees the original code. Specs describe
file layouts and behaviour in plain language, with confidence labels: **CONFIRMED** (empirical or
disassembly), **HIGH CONFIDENCE**, **HYPOTHESIS** and **OPEN**. Anything unresolved is recorded as
open rather than guessed.

## What's here

- `team-a/`: Team A's specifications (`spec-*.md`), its running log (`HANDOFF.md`), and `WALLS.md`,
  the list of approaches that were tried and ruled out.
- `team-b/`: the reimplementation.
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

## Status (September 2026)

- Every covered format is validated against the full shipped population, not a sample.
- A D3D9 SM2/SM3 shader bytecode disassembler, plus a translator to HLSL SM4/5. All 7,276 shipped
  shaders translate and compile.
- A deferred D3D11 renderer. It draws characters with animation, vehicles with their original shaders
  and paint, trees, the always-loaded city tier, and textured level meshes (buildings and props).
- An embedded Lua 5.1 host. It registers all 1,490 engine functions and runs all 804 shipped scripts.
  About 20 engine functions are reimplemented from reviewed specs so far; about 570 have written specs.

This is not yet a playable game.

## Building

Requirements: CMake, MSVC (Visual Studio 2022) and the Windows SDK (for D3D11). Most tools need a path
to your own game install; see each tool's header comment and `team-b/HANDOFF.md`.

## Legal

Saints Row and Saints Row: The Third are trademarks of their respective owners. This project is not
affiliated with or endorsed by them. The code and documents here are original work, released under the
MIT licence (`LICENSE`).
