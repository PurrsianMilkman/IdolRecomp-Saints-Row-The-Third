# Verification: wave-9 merge (MSVC-native fuzzing infrastructure)

**Date**: 2026-10-07
**Merge commit verified**: `4029515` (chain: `0373110` → `4029515`)
**Fresh build dir**: `build_verify_wave9` (default CMake options, `CRREISH_FUZZ_MSVC` at its default OFF)

## Merge covered

**`4029515`** — MSVC-native libFuzzer+ASan fuzzing infrastructure, opt-in via `CRREISH_FUZZ_MSVC`
(default OFF). Source branch `worktree-agent-a3c10f4724914245e`, commit `015d9ad`.

## What it does

An additive sibling to a pre-existing Clang-only `fuzz/` setup already in this repo (left completely
untouched — it has already found/fixed 4 real bugs historically, per its own README). New
`fuzz/msvc/` targets: `vpp::Container` (archive/`.vpp_pc`), `sr3xtbl::ParseDocument` (the tolerant
hand-rolled XML parser), `sr3locstr::parseLeStrings` (`.le_strings` — not fuzzed anywhere before this,
and its own header explicitly claims bounds-checks the real loader lacks), and the D3D9 SM2/SM3
shader-bytecode disassembler/CTAB reader/HLSL translator. Confirmed empirically that this MSVC toolchain
needs `/fsanitize=address /fsanitize=fuzzer` as two separate switches — the comma-joined form is silently
rejected (D9002) with no error, producing an uninstrumented binary. Seeds are extracted from the real
shipped archive cache at run time, nothing copied into the repo. A brief 90-second-per-target proof-of-life
run found no crashes (a legitimate outcome for that scope).

## Verification performed (this pass, independent of the source agent's own run)

- Fresh build directory `build_verify_wave9`, configured via `cmake -S . -B build_verify_wave9 -G "Visual
  Studio 17 2022" -A x64` (default options — `CRREISH_FUZZ_MSVC` OFF), built Release (`cmake --build
  build_verify_wave9 --config Release -j 8`, exit 0), against merge commit `4029515`.
- `ctest -C Release`: **100% tests passed, 81/81** (unchanged from the wave-8 baseline — expected, since
  the fuzz option is off by default and adds no ctest entries in that state).
- `git status --short` confirmed only the 7 expected new files under `fuzz/msvc/` plus the additive
  `CMakeLists.txt` block were staged before commit.

## Outcome

Merge verified clean (no regression, as expected for an opt-in-only addition). Worktree
`agent-a3c10f4724914245e` is a candidate for the pending cleanup batch (not deleted, per the user's "ask
each time" policy).
