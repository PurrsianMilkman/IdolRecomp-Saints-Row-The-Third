# libFuzzer harnesses (cloud phase, 2026-09-30). Opt-in, Clang only:
#   cmake -S team-b -B build-fuzz -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
#     -DCRREISH_FUZZ=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo \
#     -DCMAKE_C_FLAGS="-fsanitize=fuzzer-no-link,address,undefined" \
#     -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer-no-link,address,undefined"
# then fuzz/run_fuzz.sh <build dir> <seconds per target>. See fuzz/README.md.

set(CRREISH_FUZZ_LIBS vpp_container sr3xtbl sr3texture sr3geometry sr3mesh sr3rig sr3anim
    sr3clmesh sr3zone sr3save sr3fxo sr3d3d9bc sr3lua sr3asm sr3vintdoc)

foreach(t vpp xtbl texture geometry rig anim clmesh zoneheader save fxo d3d9bc lua asm vintdoc)
    add_executable(fuzz_${t} fuzz/fuzz_single.cpp)
    target_compile_definitions(fuzz_${t} PRIVATE CRREISH_FUZZ_TARGET_${t}=1)
    target_link_libraries(fuzz_${t} PRIVATE ${CRREISH_FUZZ_LIBS})
    target_link_options(fuzz_${t} PRIVATE -fsanitize=fuzzer)
endforeach()
foreach(t mesh zonegeom)
    add_executable(fuzz_${t} fuzz/fuzz_${t}.cpp)
    target_link_libraries(fuzz_${t} PRIVATE ${CRREISH_FUZZ_LIBS})
    target_link_options(fuzz_${t} PRIVATE -fsanitize=fuzzer)
endforeach()

# Seed capture: every synthetic suite relinked with --wrap on the readers'
# entry points (fuzz/seed_capture.cpp). Tests unchanged.
set(CRREISH_WRAPPED_SYMBOLS
    _ZN10sr3texture11TexturePair5parseEN3vpp8ByteViewE
    _ZN10sr3vintdoc11parseHeaderEN3vpp8ByteViewE
    _ZN11sr3geometry13MaterialBlock5parseEN3vpp8ByteViewE
    _ZN11sr3geometry13GeometryBlock5parseEN3vpp8ByteViewERKNS_13MaterialBlockE
    _ZN3vpp9ContainerC1ENS_8ByteViewE
    _ZN6sr3asm11AsmManifest5parseEN3vpp8ByteViewE
    _ZN6sr3fxo13ShaderWrapper5parseEN3vpp8ByteViewE
    _ZN6sr3lua6Parser7analyzeERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE
    _ZN6sr3rig3Rig5parseEN3vpp8ByteViewE
    _ZN7sr3anim9Animation5parseEN3vpp8ByteViewE
    _ZN7sr3mesh9MeshBlock5parseEN3vpp8ByteViewEmS2_mm
    _ZN7sr3save12SaveSnapshot5parseEN3vpp8ByteViewE
    _ZN7sr3save13SaveDirectory5parseEN3vpp8ByteViewENS0_10HashPolicyE
    _ZN7sr3xtbl13ParseDocumentEPKhmRKNS_12ParseOptionsE
    _ZN7sr3xtbl13ParseDocumentESt17basic_string_viewIcSt11char_traitsIcEERKNS_12ParseOptionsE
    _ZN7sr3zone10ZoneHeader5parseEN3vpp8ByteViewE
    _ZN7sr3zone12ZoneGeometry6locateEN3vpp8ByteViewES2_
    _ZN9sr3clmesh9LevelMesh5parseEN3vpp8ByteViewERKNS_11WalkOptionsE
    _ZN9sr3d3d9bc11disassembleEN3vpp8ByteViewE)
set(CRREISH_WRAP_FLAGS "")
foreach(s ${CRREISH_WRAPPED_SYMBOLS})
    list(APPEND CRREISH_WRAP_FLAGS "-Wl,--wrap=${s}")
endforeach()
add_library(crreish_seed_capture STATIC fuzz/seed_capture.cpp)
target_link_libraries(crreish_seed_capture PUBLIC ${CRREISH_FUZZ_LIBS})

file(GLOB CRREISH_SYNTH_TESTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/synthetic_*_test.cpp)
set(CRREISH_SEEDCAP_TARGETS "")
foreach(src ${CRREISH_SYNTH_TESTS})
    get_filename_component(stem ${src} NAME_WE)
    add_executable(seedcap_${stem} ${src})
    # A test's own library dependencies are a subset of these plus the Lua host.
    target_link_libraries(seedcap_${stem} PRIVATE crreish_seed_capture sr3luahost sr3tree sr3foliage
        sr3morph sr3conversation sr3cutscene sr3rig sr3vehicle sr3audio sr3effects sr3customization
        sr3vehicleinfo sr3tables_trafficai sr3tables_weapons sr3tables_progression sr3tables_environment
        sr3tables_animation sr3tables_audio_radio sr3tables_ui_controls sr3tables_vehicle_world
        sr3tables_diversions sr3tables_customization ${CRREISH_FUZZ_LIBS} crreish_seed_capture)
    # Tests that read the real tools/ lists (roster, refusal stress) need this.
    target_compile_definitions(seedcap_${stem} PRIVATE CRREISH_TOOLS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tools")
    target_link_options(seedcap_${stem} PRIVATE ${CRREISH_WRAP_FLAGS})
    list(APPEND CRREISH_SEEDCAP_TARGETS seedcap_${stem})
endforeach()
add_custom_target(fuzz_all DEPENDS ${CRREISH_SEEDCAP_TARGETS})
