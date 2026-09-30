# Fuzzing the readers

libFuzzer harnesses (Clang only) for every reader that parses untrusted bytes:
`vpp` (container + entry decompression + nested containers), `xtbl`, `texture`, `geometry` (material
+ geometry block), `mesh` (two-buffer, see `fuzz_mesh.cpp` for the input layout), `rig`, `anim`,
`clmesh`, `zoneheader`, `zonegeom` (two-buffer), `save` (directory + snapshot), `fxo`, `d3d9bc`
(disassembler, CTAB reader, both HLSL targets), `lua` (lexer + parser), `asm`, `vintdoc`.
A harness only swallows the readers' documented malformed-input exceptions; sanitizer reports,
crashes, hangs and allocations over the limits below are findings.

## Seeds

Taken from the synthetic test suites without changing them: `seed_capture.cpp` wraps each reader's
entry point with `-Wl,--wrap=<symbol>` in a relinked copy of every `tests/synthetic_*_test.cpp`
(`seedcap_*` executables), and writes each buffer the tests hand a reader to
`$CRREISH_SEED_DIR/<target>/`. No game data.

## Running

```
cmake -S team-b -B build-fuzz -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCRREISH_FUZZ=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_FLAGS="-fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=undefined" \
  -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=undefined"
cmake --build build-fuzz
team-b/fuzz/run_fuzz.sh build-fuzz 120 fuzz-work      # seconds per target
```

Limits: `-timeout=10 -rss_limit_mb=2048 -malloc_limit_mb=1024`. Crash inputs land in
`fuzz-work/artifacts/<target>/`. On Ubuntu the libFuzzer runtime is in `libclang-rt-18-dev`.

## Regressions

`regressions/<target>/` holds every reproducer a real fix was made for; CI replays them. Each fix
also has a unit test in the relevant synthetic suite (using `tests/alloc_guard.h` where the bug was
an allocation from an untrusted count).

| Found by | Bug | Fix | Unit test |
|---|---|---|---|
| geometry, clmesh, zoneheader | `MaterialBlock::parse` reserved `count` strings before checking the buffer holds them (`0xB0000002` → ~94 GB) | reservation capped at the bytes remaining | `synthetic_ccmesh_test` |
| vpp | `Container::decompressEntry` allocated the declared `+0x0C` size up front (~3 GB over a few hundred bytes) | reject sizes beyond DEFLATE's 1032:1 maximum for the input available | `synthetic_archive_test` |
| vpp | `Container` reserved `entryCount` entries before reading the directory (~61M → ~1.4 GB) | reservation capped at one entry per 24-byte directory record the buffer holds | `synthetic_archive_test` |
| mesh | `MeshBlock::decodeChannel` reserved `elementCount` vertices up front, and a zero stride would re-read the same bytes `elementCount` times | refuse a stride shorter than the layout's fixed fields (FormatError); cap the reservation at what the segment holds | `synthetic_ccmesh_vertex_test` (embedded reproducer) |
