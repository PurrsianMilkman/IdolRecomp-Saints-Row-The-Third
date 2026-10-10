# MSVC-native fuzzing (`CRREISH_FUZZ_MSVC`)

A separate, additive sibling to `fuzz/fuzz.cmake` (`CRREISH_FUZZ`, Clang-only,
pre-existing before this task - see `fuzz/README.md`'s own bug table). This
one uses cl.exe's **own** native libFuzzer+ASan support, so it needs no
separate Clang toolchain - just the normal MSVC/Visual Studio build this
project already uses. Opt-in, OFF by default, never built or run by a plain
`cmake --build` / `ctest` pass.

## Toolchain verification (done empirically, 2026-10-07)

MSVC 14.44.35207 / cl.exe 19.44.35228 (VS 2022 BuildTools) ships
`clang_rt.fuzzer_{MT,MD}[_no_main]-x86_64.lib`, `clang_rt.asan_*-x86_64.lib`,
and `<fuzzer>`/`<sanitizer>` headers - i.e. MSVC's own port of libFuzzer and
AddressSanitizer, confirmed by:

1. Compiling+linking a trivial `LLVMFuzzerTestOneInput` with a planted
   null-deref via `cl /EHsc /fsanitize=address /fsanitize=fuzzer t.cpp
   /Fe:t.exe`, and running it: it behaves exactly like a real libFuzzer
   binary (`-help=1` prints the standard usage banner) and found the
   planted crash in ~7000 execs (well under a second), with a genuine
   `AddressSanitizer: access-violation` report, stack trace, and a
   minimized repro file written to disk.
2. Confirming that `/fsanitize=address /fsanitize=fuzzer`-compiled *library*
   object code links cleanly into an executable that defines its own
   ordinary `main()` (a standalone two-object link test) - the linker only
   pulls `clang_rt.fuzzer`'s own main from its embedded default-lib
   directive when nothing else already satisfies that symbol, so the
   reader libraries get full instrumentation and `seed_gen_msvc` (own
   `main()`, same libraries) still links and runs normally.

**Two real traps, both silent (the build "succeeds" either way, just
uninstrumented):**

- The flag is **two separate switches**: `/fsanitize=address /fsanitize=fuzzer`.
  The single comma form from this task's own brief,
  `/fsanitize=address,fuzzer`, is **rejected** - cl.exe emits warning D9002
  ("ignoring unknown option") and silently compiles a plain binary.
- Clang's `-fsanitize=fuzzer-no-link` (coverage instrumentation without
  pulling in libFuzzer's own `main` - what `fuzz.cmake` uses so the whole
  Clang build can share one set of flags without every normal test
  executable's `main()` conflicting) is **not recognised** here either
  (same D9002). Point 2 above is why that doesn't end up mattering for this
  file's own design.
- At **runtime** the resulting `.exe` needs `clang_rt.asan_dynamic-x86_64.dll`
  next to it (default `/MD` dynamic CRT) or it fails with
  `STATUS_DLL_NOT_FOUND` (exit `0xC0000135`) and *no* stderr output at all -
  `fuzz_msvc.cmake` copies it post-build automatically.

## Targets

| Target | Reader | Why this one |
|---|---|---|
| `fuzz_vpp_msvc` | `vpp::Container` | archive/`.vpp_pc` - the foundational format every other reader's real-data test loads its sample bytes through. |
| `fuzz_xtbl_msvc` | `sr3xtbl::ParseDocument` | the tolerant hand-rolled XML parser (survives real shipped mismatched tags, a stray 0x1F byte, a space in an element name) that every `sr3tables_*` typed reader builds on. |
| `fuzz_le_strings_msvc` | `sr3locstr::parseLeStrings` | **not fuzzed anywhere before this task** - `fuzz/fuzz_single.cpp`'s Clang-only target list has no `le_strings` branch. Its own header claims to bounds-check every array/record read that "the real loader does no bounds check on any count or offset" - worth fuzzing to confirm. |
| `fuzz_d3d9bc_msvc` | `sr3d3d9bc::disassemble` / `readConstantTable` / `translateToHlsl` | the D3D9 SM2/SM3 shader bytecode disassembler, named explicitly in this task's own brief. |

**Explicitly out of scope**: anything touching the `0x2237` block / the
`.czn_pc` object-placement/property-stream body. `sr3zone`'s own headers
already document that body as deliberately unparsed; no target here
references `sr3zone` at all.

A harness only swallows the readers' own documented malformed-input
exceptions (`FormatError` subclasses of `std::runtime_error`,
`std::out_of_range` from `ByteView`) via the SAME `fuzzcommon::guarded()`
helper the Clang-only harnesses use (`fuzz/fuzz_common.h`, reused as-is, not
copied). A sanitizer report, crash, or hang is a finding.

## Seeds

`seed_gen_msvc <out_dir>` extracts REAL shipped sample bytes from
`CRREISH_GAME_CACHE_DIR` into `<out_dir>/<target>/` - reusing the exact
`readEntry()` pattern every `tests/real_*_test.cpp` file already uses, and
the same wrapper-header-first/`ShaderWrapper`-fallback blob location
`tools/ctab_census.cpp` uses for real `.fxo_pc` shader bytecode. It writes
only to whatever output directory it is given; nothing is copied into this
repository. Silently skips any archive absent from the cache (same
exit-77-is-not-a-failure discipline the `real_*` ctest targets use).

## Running

```
cmake -S . -B build_fuzz_msvc -G "Visual Studio 17 2022" -A x64 -DCRREISH_FUZZ_MSVC=ON
cmake --build build_fuzz_msvc --config RelWithDebInfo --target fuzz_vpp_msvc fuzz_xtbl_msvc fuzz_le_strings_msvc fuzz_d3d9bc_msvc seed_gen_msvc

build_fuzz_msvc\RelWithDebInfo\seed_gen_msvc.exe build_fuzz_msvc\seeds

build_fuzz_msvc\RelWithDebInfo\fuzz_vpp_msvc.exe        -max_total_time=120 build_fuzz_msvc\seeds\vpp
build_fuzz_msvc\RelWithDebInfo\fuzz_xtbl_msvc.exe       -max_total_time=120 build_fuzz_msvc\seeds\xtbl
build_fuzz_msvc\RelWithDebInfo\fuzz_le_strings_msvc.exe -max_total_time=120 build_fuzz_msvc\seeds\le_strings
build_fuzz_msvc\RelWithDebInfo\fuzz_d3d9bc_msvc.exe     -max_total_time=120 build_fuzz_msvc\seeds\d3d9bc
```

Only selected targets are built here (never the whole project) in this
sanitizer-flagged build directory, deliberately - see `fuzz_msvc.cmake`'s own
comment on why that matters (it is what lets the shared reader libraries be
instrumented unconditionally without touching their normal, non-sanitized
build elsewhere).

Crash inputs land in the current directory as `crash-<sha1>` (default
libFuzzer behaviour); minimize with
`fuzz_xxx_msvc.exe -minimize_crash=1 -runs=<N> crash-<sha1>`.

## Regressions

`regressions/<target>/` holds every reproducer a real fix was made for. Each
fix also gets a permanent unit test in the relevant synthetic suite (added
as a normal `ctest` test - CTest `LABELS` had not landed in this
CMakeLists.txt as of this round, so nothing here is labelled `fast`; a
later pass can add the label once that convention exists).

| Found by | Bug | Fix | Unit test |
|---|---|---|---|
| _(none yet)_ | | | |

A full ~1h-per-target campaign across all 4 targets (`campaign_20261007`, run
in two sessions across a rate-limit kill) found zero crashes, zero sanitizer
reports, and zero hangs over ~113M total executions - see
`results/verify_fuzz_msvc_campaign_20261007.md` for the per-target breakdown.
