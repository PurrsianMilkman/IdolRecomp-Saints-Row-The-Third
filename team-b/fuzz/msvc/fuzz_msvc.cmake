# MSVC-native libFuzzer+ASan harnesses (this task, 2026-10-07). Opt-in via
# CRREISH_FUZZ_MSVC (CMakeLists.txt), OFF by default, never built/run by a
# normal `cmake --build` / `ctest` pass.
#
# This is a SEPARATE, additive sibling to fuzz/fuzz.cmake (CRREISH_FUZZ,
# Clang-only, pre-existing before this task - see fuzz/README.md's own
# bug table for what it already found/fixed). Nothing in this file touches
# fuzz.cmake, fuzz_common.h, fuzz_single.cpp or any other file under fuzz/
# other than this one and the new fuzz/msvc/* files it lists below. The
# normal build here is MSVC/Visual Studio generator, not Clang, so this
# gives the project a fuzzing path that works without installing a
# separate Clang toolchain.
#
# TOOLCHAIN VERIFICATION (done empirically this session, MSVC 14.44.35207 /
# cl.exe 19.44.35228, VS 2022 BuildTools, Windows 11):
#   - This toolset ships clang_rt.fuzzer_{MT,MD}[_no_main]-x86_64.lib and
#     clang_rt.asan_*-x86_64.lib plus <fuzzer>/<sanitizer> headers - i.e.
#     MSVC's own native port of libFuzzer+ASan, no Clang required.
#   - The flag is TWO SEPARATE switches: `/fsanitize=address /fsanitize=fuzzer`.
#     The single comma-joined form `/fsanitize=address,fuzzer` that this
#     task's own brief describes is REJECTED - cl.exe emits warning D9002
#     ("ignoring unknown option") and silently produces a plain, uninstrumented
#     binary. This is a real trap (the build "succeeds" either way), so it is
#     spelled out as two options everywhere below.
#   - Clang's `-fsanitize=fuzzer-no-link` (coverage instrumentation without
#     pulling in libFuzzer's own main - what fuzz.cmake above uses so the
#     WHOLE Clang build can share one set of CXX flags without every normal
#     executable's own main() conflicting) is NOT recognised by this cl.exe
#     (same D9002). Empirically this turns out not to matter: a library
#     object compiled with the full `/fsanitize=address /fsanitize=fuzzer`
#     links cleanly into an executable that defines its own ordinary main()
#     - verified with a standalone two-object link test - because the
#     linker only pulls clang_rt.fuzzer's embedded-by-default main when no
#     other definition already satisfies that symbol. So the reader
#     libraries below get full sanitizer+coverage instrumentation
#     unconditionally, and seed_gen_msvc (own main(), same libraries) still
#     links and runs normally.
#   - At RUNTIME the resulting .exe needs clang_rt.asan_dynamic-x86_64.dll
#     next to it (default /MD dynamic CRT) - copied post-build below.
#   - Verified end-to-end: a trivial LLVMFuzzerTestOneInput with a planted
#     null-deref, built this way, ran as a real libFuzzer binary and found
#     the crash in under a second (~7000 execs) with a genuine ASan
#     access-violation report and a minimized repro file.
#
# Targets added here, and why (see fuzz/msvc/README.md for the full list):
#   fuzz_vpp_msvc        vpp::Container          - archive/`.vpp_pc`, the
#                         foundational format every other reader's real-data
#                         test loads its sample bytes through.
#   fuzz_xtbl_msvc        sr3xtbl::ParseDocument  - the tolerant hand-rolled
#                         XML parser every sr3tables_* typed reader builds on.
#   fuzz_le_strings_msvc  sr3locstr::parseLeStrings - NOT fuzzed by the
#                         existing Clang infra at all (no `le_strings` target
#                         in fuzz/fuzz_single.cpp); its own header explicitly
#                         claims to bounds-check every array/record read that
#                         "the real loader does no bounds check on" at all -
#                         exactly the kind of claim worth fuzzing.
#   fuzz_d3d9bc_msvc      sr3d3d9bc disassemble/ctab/hlsl - the D3D9 SM2/SM3
#                         shader bytecode disassembler named explicitly in
#                         this task's own brief.
# Explicitly OUT OF SCOPE, per this task: anything touching the `0x2237`
# block / the `.czn_pc` object-placement/property-stream body (sr3zone's own
# zone_header.h/zone_geometry.h already document that body as deliberately
# unparsed here) - no target below references sr3zone at all.

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "CRREISH_FUZZ_MSVC needs the x64 MSVC toolset (libFuzzer/ASan are x64-only here)")
endif()

set(CRREISH_FUZZ_MSVC_FLAGS /fsanitize=address /fsanitize=fuzzer /Zi)

# Instrument the specific reader libraries this round's harnesses exercise.
# Additive to each: appends to whatever compile options the target already
# has (e.g. /W4 from the top of this file) rather than replacing them, and
# only ever applies when CRREISH_FUZZ_MSVC is explicitly turned ON - the
# normal (option OFF) build of these same targets is entirely unaffected.
foreach(_crreish_fuzz_lib vpp_container sr3xtbl sr3locstr sr3d3d9bc)
    target_compile_options(${_crreish_fuzz_lib} PRIVATE ${CRREISH_FUZZ_MSVC_FLAGS})
endforeach()
# sr3fxo is only a seed_gen_msvc dependency (real `.fxo_pc` shader-blob
# location), never one of the fuzz targets themselves - /fsanitize=address
# only (no `/fsanitize=fuzzer`), just to keep MSVC's STL container
# annotation setting (annotate_string/annotate_vector) consistent with
# vpp_container above across seed_gen_msvc's own link (LNK2038 otherwise;
# that flag is all-or-nothing per final executable, not per-library).
target_compile_options(sr3fxo PRIVATE /fsanitize=address /Zi)

# Locate the ASan dynamic runtime DLL next to cl.exe, to post-build-copy
# beside every harness exe below (the default /MD CRT needs it on-disk at
# process start, not just at link time).
get_filename_component(_crreish_cl_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
set(_crreish_asan_dll "${_crreish_cl_dir}/clang_rt.asan_dynamic-x86_64.dll")

function(crreish_add_msvc_fuzz_target name source)
    add_executable(${name} ${source})
    target_compile_options(${name} PRIVATE ${CRREISH_FUZZ_MSVC_FLAGS})
    target_link_options(${name} PRIVATE /DEBUG)
    if(EXISTS "${_crreish_asan_dll}")
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_crreish_asan_dll}" "$<TARGET_FILE_DIR:${name}>"
            COMMENT "Copying ASan runtime next to ${name}")
    endif()
endfunction()

crreish_add_msvc_fuzz_target(fuzz_vpp_msvc fuzz/msvc/fuzz_vpp_msvc.cpp)
target_link_libraries(fuzz_vpp_msvc PRIVATE vpp_container)

crreish_add_msvc_fuzz_target(fuzz_xtbl_msvc fuzz/msvc/fuzz_xtbl_msvc.cpp)
target_link_libraries(fuzz_xtbl_msvc PRIVATE sr3xtbl)

crreish_add_msvc_fuzz_target(fuzz_le_strings_msvc fuzz/msvc/fuzz_le_strings_msvc.cpp)
target_link_libraries(fuzz_le_strings_msvc PRIVATE sr3locstr)

crreish_add_msvc_fuzz_target(fuzz_d3d9bc_msvc fuzz/msvc/fuzz_d3d9bc_msvc.cpp)
target_link_libraries(fuzz_d3d9bc_msvc PRIVATE sr3d3d9bc vpp_container)

# Seed-corpus generator (fuzz/msvc/seed_gen_msvc.cpp) - plain, unsanitized
# executable (its own ordinary main(), not a fuzz target itself) that
# extracts REAL shipped sample bytes from CRREISH_GAME_CACHE_DIR into
# whatever output directory it is given at run time. Never copies game data
# into the repository - see the file's own top comment. Needs vpp_container
# (now sanitizer-instrumented above, which is harmless for a normal tool -
# verified it still links/runs with its own main intact) and sr3fxo for the
# same wrapper-header-first/ShaderWrapper-fallback blob location
# tools/ctab_census.cpp already uses for real shader bytecode.
add_executable(seed_gen_msvc fuzz/msvc/seed_gen_msvc.cpp)
target_link_libraries(seed_gen_msvc PRIVATE vpp_container sr3fxo)
target_compile_definitions(seed_gen_msvc PRIVATE CRREISH_GAME_CACHE_DIR="${CRREISH_GAME_CACHE_DIR}")
# /fsanitize=address only (no `/fsanitize=fuzzer` - this tool keeps its own
# ordinary main()): MSVC's STL container annotation feature
# (annotate_string/annotate_vector), auto-enabled by /fsanitize=address, is
# an all-or-nothing per-link-unit setting - LNK2038 otherwise, since this
# target links the now-instrumented vpp_container.lib above.
target_compile_options(seed_gen_msvc PRIVATE /fsanitize=address /Zi)
if(EXISTS "${_crreish_asan_dll}")
    add_custom_command(TARGET seed_gen_msvc POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_crreish_asan_dll}" "$<TARGET_FILE_DIR:seed_gen_msvc>"
        COMMENT "Copying ASan runtime next to seed_gen_msvc")
endif()

add_custom_target(fuzz_msvc_all DEPENDS fuzz_vpp_msvc fuzz_xtbl_msvc fuzz_le_strings_msvc fuzz_d3d9bc_msvc seed_gen_msvc)
