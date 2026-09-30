cmake_minimum_required(VERSION 3.16)
project(vpp_container_reader CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4)
endif()

# Vendored zlib (spec Sec3: standard RFC 1950 zlib-wrapped DEFLATE) - used
# as an off-the-shelf, unmodified library per task scope, not reimplemented.
#
# Built here as a static library straight from the vendored sources rather
# than via add_subdirectory(third_party/zlib): upstream's CMakeLists also
# defines the shared `zlib` DLL target, whose win32/zlib1.rc and zlib.map
# inputs are not in the published repo, so configure failed on every
# platform (cloud phase, 2026-09-30). Only `zlibstatic` was ever linked.
# zconf.h is generated into the same binary dir as before, so the existing
# include paths below are unchanged.
enable_language(C)
include(CheckIncludeFile)
check_include_file(unistd.h Z_HAVE_UNISTD_H)
set(ZLIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib)
configure_file(${ZLIB_DIR}/zconf.h.cmakein
    ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib/zconf.h @ONLY)
add_library(zlibstatic STATIC
    ${ZLIB_DIR}/adler32.c  ${ZLIB_DIR}/compress.c ${ZLIB_DIR}/crc32.c
    ${ZLIB_DIR}/deflate.c  ${ZLIB_DIR}/gzclose.c  ${ZLIB_DIR}/gzlib.c
    ${ZLIB_DIR}/gzread.c   ${ZLIB_DIR}/gzwrite.c  ${ZLIB_DIR}/inflate.c
    ${ZLIB_DIR}/infback.c  ${ZLIB_DIR}/inftrees.c ${ZLIB_DIR}/inffast.c
    ${ZLIB_DIR}/trees.c    ${ZLIB_DIR}/uncompr.c  ${ZLIB_DIR}/zutil.c
)
target_include_directories(zlibstatic PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib ${ZLIB_DIR})
if(MSVC)
    target_compile_definitions(zlibstatic PRIVATE
        _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
endif()

# Vendored, real, unmodified Lua 5.1.5 (lua.org's own official release -
# see third_party/lua51/CMakeLists.txt's own top comment for provenance and
# exactly which upstream files this pulls in). Explicitly OUT of this
# project's clean-room scope, same footing as zlib above and as
# spec-lua-bindings.md Sec1/Sec13 already treats the stock Lua 5.1 VM/stdlib
# itself: "public, unmodified, not proprietary." This is the real,
# embeddable reference INTERPRETER (the lua.h/lauxlib.h/lualib.h C API) -
# a new dependency the Lua host scaffold task introduces, distinct from
# sr3lua below (this project's own clean-room Lua 5.1 PARSER, which finds
# call sites/defines and never executes anything).
add_subdirectory(third_party/lua51)

add_library(vpp_container STATIC
    src/byte_view.cpp
    src/format.cpp
    src/hash.cpp
    src/payload_locator.cpp
    src/container.cpp
    src/content_validation.cpp
)
target_include_directories(vpp_container PUBLIC include)
target_include_directories(vpp_container PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib
    ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
)
target_link_libraries(vpp_container PUBLIC zlibstatic)

add_executable(vpp_dump tools/vpp_dump.cpp)
target_link_libraries(vpp_dump PRIVATE vpp_container)

add_executable(vpp_extract tools/vpp_extract.cpp)
target_link_libraries(vpp_extract PRIVATE vpp_container)

# Save-data format reader (spec-save-format.md) - unrelated to the
# vpp/str2 container format beyond reusing the generic ByteView utility
# (byte_view.cpp, compiled once into vpp_container above).
add_library(sr3save STATIC
    src/save_directory.cpp
    src/save_snapshot.cpp
)
target_include_directories(sr3save PUBLIC include)
target_link_libraries(sr3save PUBLIC vpp_container)

add_executable(save_browser tools/save_browser.cpp)
target_link_libraries(save_browser PRIVATE sr3save)

enable_testing()
add_executable(vpp_tests tests/synthetic_archive_test.cpp)
target_link_libraries(vpp_tests PRIVATE vpp_container)
target_include_directories(vpp_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib
    ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
)
add_test(NAME synthetic_archive_test COMMAND vpp_tests)

add_executable(content_validation_tests tests/synthetic_content_validation_test.cpp)
target_link_libraries(content_validation_tests PRIVATE vpp_container)
add_test(NAME synthetic_content_validation_test COMMAND content_validation_tests)

add_executable(sr3save_tests tests/synthetic_save_test.cpp)
target_link_libraries(sr3save_tests PRIVATE sr3save)
add_test(NAME synthetic_save_test COMMAND sr3save_tests)

# .asm_pc manifest reader (spec-asm-format.md) - always raw/uncompressed,
# no exposure to the container format's mode-(a) corruption issue.
add_library(sr3asm STATIC
    src/manifest.cpp
)
target_include_directories(sr3asm PUBLIC include)
target_link_libraries(sr3asm PUBLIC vpp_container)

add_executable(asm_dump tools/asm_dump.cpp)
target_link_libraries(asm_dump PRIVATE sr3asm)

add_executable(sr3asm_tests tests/synthetic_asm_test.cpp)
target_link_libraries(sr3asm_tests PRIVATE sr3asm)
add_test(NAME synthetic_asm_test COMMAND sr3asm_tests)

# .fxo_pc compiled-shader wrapper reader (spec-fxo-format.md), plus its
# content-validation corroborator (depends one-directionally on
# vpp_container for the shared DecompressResult/DecodeStatus types).
add_library(sr3fxo STATIC
    src/shader_wrapper.cpp
    src/fxo_content_validation.cpp
)
target_include_directories(sr3fxo PUBLIC include)
target_link_libraries(sr3fxo PUBLIC vpp_container)

add_executable(fxo_dump tools/fxo_dump.cpp)
target_link_libraries(fxo_dump PRIVATE sr3fxo)

add_executable(sr3fxo_tests tests/synthetic_fxo_test.cpp)
target_link_libraries(sr3fxo_tests PRIVATE sr3fxo)
add_test(NAME synthetic_fxo_test COMMAND sr3fxo_tests)

# .cefct_pc effects reader (spec-effects-format.md §5-§6, HANDOFF §9.76):
# the material-block-fronted root, the six counted arrays and their strides
# (0x88 / 0xC8 / 0x228 ...), built from the spec's CONFIRMED fields only.
# The enumeration harness (tools/validation/validate_cefct_population.cpp)
# is built via tools/validation/build_one.bat like every other harness.
add_library(sr3effects STATIC
    src/effects.cpp
)
target_include_directories(sr3effects PUBLIC include)
target_link_libraries(sr3effects PUBLIC vpp_container)

add_executable(sr3effects_tests tests/synthetic_effects_test.cpp)
target_link_libraries(sr3effects_tests PRIVATE sr3effects)
add_test(NAME synthetic_effects_test COMMAND sr3effects_tests)

# .xtbl / .cte_xtbl XML gameplay-table foundation (spec-xtbl-format.md,
# spec-tables-traffic-ai.md §1, HANDOFF §9.79): a tolerant XML parser (the
# shipped data contains mismatched close tags, a 0x1F control byte in text and
# an element name with a space), the engine's element accessors and float
# grammar, and the CRC-32 row-key hash. No dependency on the container: it
# parses bytes. Typed per-table readers build on this.
add_library(sr3xtbl STATIC
    src/xtbl.cpp
)
target_include_directories(sr3xtbl PUBLIC include)

add_executable(sr3xtbl_tests tests/synthetic_xtbl_test.cpp)
target_link_libraries(sr3xtbl_tests PRIVATE sr3xtbl)
add_test(NAME synthetic_xtbl_test COMMAND sr3xtbl_tests)

# Typed per-table readers on sr3xtbl, one library per spec-tables-*.md group
# (HANDOFF §9.80-9.83): each parses every table's element tree from the
# loader-recovered schemas into a typed struct, validated against the real
# base-game tables (not just the DLC samples the specs themselves used).
add_library(sr3tables_trafficai STATIC
    src/tables_trafficai.cpp
)
target_include_directories(sr3tables_trafficai PUBLIC include)
target_link_libraries(sr3tables_trafficai PUBLIC sr3xtbl)

add_executable(sr3tables_trafficai_tests tests/synthetic_tables_trafficai_test.cpp)
target_link_libraries(sr3tables_trafficai_tests PRIVATE sr3tables_trafficai)
add_test(NAME synthetic_tables_trafficai_test COMMAND sr3tables_trafficai_tests)

add_library(sr3tables_weapons STATIC
    src/tables_weapons.cpp
)
target_include_directories(sr3tables_weapons PUBLIC include)
target_link_libraries(sr3tables_weapons PUBLIC sr3xtbl)

add_executable(sr3tables_weapons_tests tests/synthetic_tables_weapons_test.cpp)
target_link_libraries(sr3tables_weapons_tests PRIVATE sr3tables_weapons)
add_test(NAME synthetic_tables_weapons_test COMMAND sr3tables_weapons_tests)

add_library(sr3tables_progression STATIC
    src/tables_progression.cpp
)
target_include_directories(sr3tables_progression PUBLIC include)
target_link_libraries(sr3tables_progression PUBLIC sr3xtbl)

add_executable(sr3tables_progression_tests tests/synthetic_tables_progression_test.cpp)
target_link_libraries(sr3tables_progression_tests PRIVATE sr3tables_progression)
add_test(NAME synthetic_tables_progression_test COMMAND sr3tables_progression_tests)

add_library(sr3tables_environment STATIC
    src/tables_environment.cpp
)
target_include_directories(sr3tables_environment PUBLIC include)
target_link_libraries(sr3tables_environment PUBLIC sr3xtbl)

add_executable(sr3tables_environment_tests tests/synthetic_tables_environment_test.cpp)
target_link_libraries(sr3tables_environment_tests PRIVATE sr3tables_environment)
add_test(NAME synthetic_tables_environment_test COMMAND sr3tables_environment_tests)

add_library(sr3tables_animation STATIC
    src/tables_animation.cpp
)
target_include_directories(sr3tables_animation PUBLIC include)
target_link_libraries(sr3tables_animation PUBLIC sr3xtbl)

add_executable(sr3tables_animation_tests tests/synthetic_tables_animation_test.cpp)
target_link_libraries(sr3tables_animation_tests PRIVATE sr3tables_animation)
add_test(NAME synthetic_tables_animation_test COMMAND sr3tables_animation_tests)

add_library(sr3tables_audio_radio STATIC
    src/tables_audio_radio.cpp
)
target_include_directories(sr3tables_audio_radio PUBLIC include)
target_link_libraries(sr3tables_audio_radio PUBLIC sr3xtbl)

add_executable(sr3tables_audio_radio_tests tests/synthetic_tables_audio_radio_test.cpp)
target_link_libraries(sr3tables_audio_radio_tests PRIVATE sr3tables_audio_radio)
add_test(NAME synthetic_tables_audio_radio_test COMMAND sr3tables_audio_radio_tests)

add_library(sr3tables_ui_controls STATIC
    src/tables_ui_controls.cpp
)
target_include_directories(sr3tables_ui_controls PUBLIC include)
target_link_libraries(sr3tables_ui_controls PUBLIC sr3xtbl)

add_executable(sr3tables_ui_controls_tests tests/synthetic_tables_ui_controls_test.cpp)
target_link_libraries(sr3tables_ui_controls_tests PRIVATE sr3tables_ui_controls)
add_test(NAME synthetic_tables_ui_controls_test COMMAND sr3tables_ui_controls_tests)

add_library(sr3tables_vehicle_world STATIC
    src/tables_vehicle_world.cpp
)
target_include_directories(sr3tables_vehicle_world PUBLIC include)
target_link_libraries(sr3tables_vehicle_world PUBLIC sr3xtbl)

add_executable(sr3tables_vehicle_world_tests tests/synthetic_tables_vehicle_world_test.cpp)
target_link_libraries(sr3tables_vehicle_world_tests PRIVATE sr3tables_vehicle_world)
add_test(NAME synthetic_tables_vehicle_world_test COMMAND sr3tables_vehicle_world_tests)

add_library(sr3tables_diversions STATIC
    src/tables_diversions.cpp
)
target_include_directories(sr3tables_diversions PUBLIC include)
target_link_libraries(sr3tables_diversions PUBLIC sr3xtbl)

add_executable(sr3tables_diversions_tests tests/synthetic_tables_diversions_test.cpp)
target_link_libraries(sr3tables_diversions_tests PRIVATE sr3tables_diversions)
add_test(NAME synthetic_tables_diversions_test COMMAND sr3tables_diversions_tests)

add_library(sr3tables_customization STATIC
    src/tables_customization_color_pools.cpp
    src/tables_customization_items.cpp
    src/tables_customization_slots_categories.cpp
    src/tables_customization_materials.cpp
    src/tables_customization_gang_actions.cpp
    src/tables_customization_characters.cpp
    src/tables_customization_player_creation.cpp
)
target_include_directories(sr3tables_customization PUBLIC include)
target_link_libraries(sr3tables_customization PUBLIC sr3xtbl)

add_executable(sr3tables_customization_tests tests/synthetic_tables_customization_test.cpp)
target_link_libraries(sr3tables_customization_tests PRIVATE sr3tables_customization)
add_test(NAME synthetic_tables_customization_test COMMAND sr3tables_customization_tests)

# D3D9 SM2/SM3 shader bytecode disassembler (spec-d3d9-sm2-sm3-bytecode.md,
# HANDOFF §9.96/§9.97) - built from a PUBLIC Microsoft format spec Team B
# wrote itself, not from this game's disassembly. Consumes sr3fxo's already-
# located shader blobs.
add_library(sr3d3d9bc STATIC
    src/d3d9bc_disassembler.cpp
    src/d3d9bc_ctab.cpp
    src/d3d9bc_hlsl_translator.cpp
)
target_include_directories(sr3d3d9bc PUBLIC include)
target_link_libraries(sr3d3d9bc PUBLIC vpp_container)

add_executable(sr3d3d9bc_tests tests/synthetic_d3d9bc_test.cpp)
target_link_libraries(sr3d3d9bc_tests PRIVATE sr3d3d9bc)
add_test(NAME synthetic_d3d9bc_test COMMAND sr3d3d9bc_tests)

add_executable(sr3d3d9bc_ctab_tests tests/synthetic_d3d9bc_ctab_test.cpp)
target_link_libraries(sr3d3d9bc_ctab_tests PRIVATE sr3d3d9bc)
add_test(NAME synthetic_d3d9bc_ctab_test COMMAND sr3d3d9bc_ctab_tests)

add_executable(sr3d3d9bc_hlsl_tests tests/synthetic_d3d9bc_hlsl_test.cpp)
target_link_libraries(sr3d3d9bc_hlsl_tests PRIVATE sr3d3d9bc)
add_test(NAME synthetic_d3d9bc_hlsl_test COMMAND sr3d3d9bc_hlsl_tests)

# Runtime vehicle-info entry (spec-vehicle-data.md §7, HANDOFF §9.84): the
# 0xB80-byte per-vehicle struct parsed from <name>_veh.xtbl / vehicle_groups.xtbl
# via sr3xtbl. Deliberately separate from sr3vehicle (.ccar_pc binary assemblies).
add_library(sr3vehicleinfo STATIC
    src/vehicle_entry.cpp
)
target_include_directories(sr3vehicleinfo PUBLIC include)
target_link_libraries(sr3vehicleinfo PUBLIC sr3xtbl)

add_executable(sr3vehicleinfo_tests tests/synthetic_vehicleinfo_test.cpp)
target_link_libraries(sr3vehicleinfo_tests PRIVATE sr3vehicleinfo)
add_test(NAME synthetic_vehicleinfo_test COMMAND sr3vehicleinfo_tests)

# Character/item + vehicle customization readers (spec-customization-data.md,
# HANDOFF §9.85): customization_items.xtbl / customization_outfits.xtbl /
# per-vehicle <name>.xtbl customization variants, parsed via sr3xtbl. The
# spec's base-game "currently inaccessible" limitation is resolved (§9.78).
add_library(sr3customization STATIC
    src/customization.cpp
)
target_include_directories(sr3customization PUBLIC include)
target_link_libraries(sr3customization PUBLIC sr3xtbl)

add_executable(sr3customization_tests tests/synthetic_customization_test.cpp)
target_link_libraries(sr3customization_tests PRIVATE sr3customization)
add_test(NAME synthetic_customization_test COMMAND sr3customization_tests)

# .cpeg_pc/.gpeg_pc (and .cvbm_pc/.gvbm_pc) paired texture container reader
# (spec-texture-format.md), plus its content-validation corroborator - same
# one-directional dependency shape as sr3fxo above.
add_library(sr3texture STATIC
    src/texture_pair.cpp
    src/cpeg_content_validation.cpp
)
target_include_directories(sr3texture PUBLIC include)
target_link_libraries(sr3texture PUBLIC vpp_container)

add_executable(texture_dump tools/texture_dump.cpp)
target_link_libraries(texture_dump PRIVATE sr3texture)

add_executable(sr3texture_tests tests/synthetic_texture_test.cpp)
target_link_libraries(sr3texture_tests PRIVATE sr3texture)
add_test(NAME synthetic_texture_test COMMAND sr3texture_tests)

# .ccmesh_pc/.csmesh_pc reader (spec-geometry-format.md Sec3-4.1.1): the
# shared material-reference block plus the geometry block's six raw
# arrays. No content-validation corroborator (yet) - see
# include/sr3geometry/geometry_block.h's own note on why this reader
# currently requires a trailing Mesh-sub-block marker to locate arrays at
# all, which already gives parse() itself a strong self-check.
add_library(sr3geometry STATIC
    src/material_block.cpp
    src/geometry_block.cpp
    src/material_binding.cpp
)
target_include_directories(sr3geometry PUBLIC include)
target_link_libraries(sr3geometry PUBLIC vpp_container)

add_executable(ccmesh_dump tools/ccmesh_dump.cpp)
target_link_libraries(ccmesh_dump PRIVATE sr3geometry)

add_executable(sr3geometry_tests tests/synthetic_ccmesh_test.cpp)
target_link_libraries(sr3geometry_tests PRIVATE sr3geometry)
add_test(NAME synthetic_ccmesh_test COMMAND sr3geometry_tests)

# The per-material texture-binding reader (src/material_binding.cpp, part of
# sr3geometry above) got a separate test executable rather than folding into
# sr3geometry_tests, matching the sr3anim/sr3animpayload precedent below - a
# distinct concern (HANDOFF §9.69: the direct-arithmetic rewrite that
# unified character and vehicle material binding) with its own fixture
# builder, not a variation on the six-array geometry-block tests already there.
add_executable(sr3materialbinding_tests tests/synthetic_material_binding_test.cpp)
target_link_libraries(sr3materialbinding_tests PRIVATE sr3geometry)
add_test(NAME synthetic_material_binding_test COMMAND sr3materialbinding_tests)

# .anim_pc animation reader (spec-anim-format.md) - the CONFIRMED header
# (magic/version, flags, root-motion transform, the flag-gated optional
# section), PLUS the keyframe/track payload walk and decode (payload.cpp,
# HANDOFF Sec9.37 onward) and real-clip timeline sampling (sample.cpp,
# Sec9.56) added in the 2026-09-12 animation-playback session. All three
# were previously built ONLY via ad-hoc manual cl.exe invocations
# (tools/validation/build_one.bat and the equivalent hand-typed command
# used for sr3_viewer) with their .obj output preserved in build_verify/ -
# never wired into this file, so a real `cmake --build` would not have
# built them (or sr3_viewer's `animpose` command, which calls into
# payload.cpp/sample.cpp - see below) even though their own test suites
# were green. Discovered and fixed while investigating Sec9.56's
# whole-skeleton fragmentation bug (unrelated to that bug itself).
add_library(sr3anim STATIC
    src/animation.cpp
    src/payload.cpp
    src/sample.cpp
)
target_include_directories(sr3anim PUBLIC include)
target_link_libraries(sr3anim PUBLIC vpp_container)

add_executable(anim_dump tools/anim_dump.cpp)
target_link_libraries(anim_dump PRIVATE sr3anim)

add_executable(sr3anim_tests tests/synthetic_anim_test.cpp)
target_link_libraries(sr3anim_tests PRIVATE sr3anim)
add_test(NAME synthetic_anim_test COMMAND sr3anim_tests)

add_executable(sr3animpayload_tests tests/synthetic_anim_payload_test.cpp)
target_link_libraries(sr3animpayload_tests PRIVATE sr3anim)
add_test(NAME synthetic_anim_payload_test COMMAND sr3animpayload_tests)

# .cmorph_pc morph-target structural reader (spec-morph-format.md) - walks
# the full container (headers, directory, descriptors, inline bulk runs,
# trailing sentinel) and exposes the raw 12-byte elements. The quantised
# components are NOT decoded; see include/sr3morph/morph_file.h.
add_library(sr3morph STATIC
    src/morph_file.cpp
)
target_include_directories(sr3morph PUBLIC include)
target_link_libraries(sr3morph PUBLIC vpp_container)

add_executable(morph_dump tools/morph_dump.cpp)
target_link_libraries(morph_dump PRIVATE sr3morph)

add_executable(sr3morph_tests tests/synthetic_morph_test.cpp)
target_link_libraries(sr3morph_tests PRIVATE sr3morph)
add_test(NAME synthetic_morph_test COMMAND sr3morph_tests)

# .cfmesh_pc foliage-mesh reader (spec-foliage-format.md) - the shared
# material block (reused from sr3geometry) plus a new outer 0x0FF1C1A1
# block: the embedded/inline Mesh sub-block (located+version-checked
# only), inline material definitions, the texture-name table, and the
# trailing LOD/fade table. Plus its content-validation corroborator - same
# one-directional-onto-vpp_container shape as sr3fxo/sr3texture, but also
# depends on sr3geometry for MaterialBlock. Real-data validated 19/19 on
# the entire real population (see HANDOFF.md Sec4.5); this wiring plus
# tests/synthetic_foliage_test.cpp bring it into the build/test surface.
add_library(sr3foliage STATIC
    src/foliage_mesh.cpp
    src/foliage_content_validation.cpp
)
target_include_directories(sr3foliage PUBLIC include)
target_link_libraries(sr3foliage PUBLIC sr3geometry vpp_container)

add_executable(foliage_dump tools/foliage_dump.cpp)
target_link_libraries(foliage_dump PRIVATE sr3foliage)

add_executable(sr3foliage_tests tests/synthetic_foliage_test.cpp)
target_link_libraries(sr3foliage_tests PRIVATE sr3foliage)
add_test(NAME synthetic_foliage_test COMMAND sr3foliage_tests)

# .ctdg_pc mission-conversation reader (spec-conversation-format.md) - the
# flattest format here: an 8-byte header, a fixed 32-byte source-name
# field, and a packed array of 12-byte dialogue turns, with NO offsets and
# no pointer fixups anywhere. Pinned by the exact-size identity
# `44 + 12 x count == size` (4,984/4,984). Speaker ids and the third
# per-turn field are surfaced raw - see include/sr3conversation/conversation.h.
add_library(sr3conversation STATIC
    src/conversation.cpp
    src/ctdg_content_validation.cpp
)
target_include_directories(sr3conversation PUBLIC include)
target_link_libraries(sr3conversation PUBLIC vpp_container)

add_executable(ctdg_dump tools/ctdg_dump.cpp)
target_link_libraries(ctdg_dump PRIVATE sr3conversation)

add_executable(sr3conversation_tests tests/synthetic_ctdg_test.cpp)
target_link_libraries(sr3conversation_tests PRIVATE sr3conversation)
add_test(NAME synthetic_ctdg_test COMMAND sr3conversation_tests)

# .csc_pc cutscene camera script reader (spec-cutscene-camera-format.md) -
# a list of contiguous camera shots, each with eight independent keyframe
# channels. Pinned by `record_offset + count x 0x68 == size` (96/96).
# Channel 1's 8-byte encoding is OPEN and channels 3-7's meaning is
# HYPOTHESIS, so both are surfaced without interpretation - see
# include/sr3cutscene/camera_script.h.
add_library(sr3cutscene STATIC
    src/camera_script.cpp
    src/csc_content_validation.cpp
)
target_include_directories(sr3cutscene PUBLIC include)
target_link_libraries(sr3cutscene PUBLIC vpp_container)

add_executable(csc_dump tools/csc_dump.cpp)
target_link_libraries(csc_dump PRIVATE sr3cutscene)

add_executable(sr3cutscene_tests tests/synthetic_csc_test.cpp)
target_link_libraries(sr3cutscene_tests PRIVATE sr3cutscene)
add_test(NAME synthetic_csc_test COMMAND sr3cutscene_tests)

# Wire the .xtbl, .fxo_pc, .cpeg_pc/.cvbm_pc, .cfmesh_pc, .ctdg_pc and
# .csc_pc content-validation corroborators into the tools that
# walk/extract .vpp_pc archives.
target_link_libraries(vpp_dump PRIVATE sr3fxo sr3texture sr3foliage sr3conversation sr3cutscene)
target_link_libraries(vpp_extract PRIVATE sr3fxo sr3texture sr3foliage sr3conversation sr3cutscene)

# Shared "Mesh" sub-block reader: index buffer + per-vertex channels
# (spec-vertex-format.md). The structure sr3geometry and sr3foliage
# locate but deliberately do not parse.
add_library(sr3mesh STATIC
    src/mesh_block.cpp
)
target_include_directories(sr3mesh PUBLIC include)
target_link_libraries(sr3mesh PUBLIC vpp_container)

add_library(sr3vehicle STATIC
    src/vehicle.cpp
)
target_include_directories(sr3vehicle PUBLIC include)
target_link_libraries(sr3vehicle PUBLIC vpp_container)

add_executable(sr3mesh_tests tests/synthetic_ccmesh_vertex_test.cpp)
# sr3rig added test-only, HANDOFF Sec9.68: testResolveVertexSetConflictsByDuplication
# exercises sr3rig::resolveVertexSetConflictsByDuplication() against this
# file's own proven MeshOptions/buildMesh fixture rather than risk a
# second, independently-fallible byte-layout builder in the sr3rig test
# file. sr3rig already depends on sr3mesh (not the other way round), so
# this is a test-target-only edge, not a library dependency change.
target_link_libraries(sr3mesh_tests PRIVATE sr3mesh sr3rig)
add_test(NAME synthetic_mesh_vertex_test COMMAND sr3mesh_tests)

# .rig_pc skeleton reader (spec-rig-format.md v2) - bone hierarchy,
# model-space rest positions, engine-hash lookup table, attachment/IK
# targets. Depends on vpp_container for ByteView and hashFilename.
#
# pose.cpp is the hierarchical-skinning CPU-math library (sr3rig/pose.h):
# per-bone skinning matrices from a bone hierarchy + rest positions + local
# rotations, and CPU vertex skinning against sr3mesh::Vertex blend
# indices/weights. It is the stage-1 animation-playback math (see
# HANDOFF.md "RESUME HERE", 2026-09-12) - no DirectX, no .anim_pc. Depends
# on sr3mesh for the Vertex type it skins.
#
# animated_pose.cpp (stage 2, Sec9.56) layers animated translation deltas
# on top of pose.cpp WITHOUT modifying it - see include/sr3rig/
# animated_pose.h. Like sr3anim/payload.cpp and sample.cpp above, this was
# previously built only by hand and never added here.
add_library(sr3rig STATIC
    src/rig.cpp
    src/pose.cpp
    src/animated_pose.cpp
    src/bone_palette.cpp
)
target_include_directories(sr3rig PUBLIC include)
target_link_libraries(sr3rig PUBLIC vpp_container sr3mesh)

add_executable(sr3rig_tests tests/synthetic_rig_test.cpp)
target_link_libraries(sr3rig_tests PRIVATE sr3rig)
add_test(NAME synthetic_rig_test COMMAND sr3rig_tests)

add_executable(sr3rigpose_tests tests/synthetic_pose_test.cpp)
target_link_libraries(sr3rigpose_tests PRIVATE sr3rig)
add_test(NAME synthetic_pose_test COMMAND sr3rigpose_tests)

# Multi-bone composition regression (added while investigating Sec9.56):
# closes the gap that NO test before this - not this file's own single-
# bone-subtree case above, not validate_pose.cpp, not
# validate_animated_pose.cpp - ever exercised computeSkinningMatrices()/
# computeAnimatedSkinningMatrices() with TWO OR MORE bones simultaneously
# carrying real, different, non-identity rotations (with and without
# translation deltas). Hand-derived expected values, same discipline as
# synthetic_pose_test.cpp. Result: both functions are exact for the
# general multi-bone case (see HANDOFF Sec9.56.1) - this suite is the
# permanent record of that check, not a reaction to a bug found in them.
add_executable(sr3rigmultibonepose_tests tests/synthetic_multibone_pose_test.cpp)
target_link_libraries(sr3rigmultibonepose_tests PRIVATE sr3rig)
add_test(NAME synthetic_multibone_pose_test COMMAND sr3rigmultibonepose_tests)

# Bone palette (HANDOFF Sec9.62): the Mesh sub-block's u16-count-gated
# list at header +0x38 is a per-mesh table of rig bone indices that every
# vertex blend index indexes - NOT a direct rig index. sr3rig/bone_palette.h
# remaps blend indices through it and supplies the (-x,-y,-z) rig->mesh
# conjugation the palette reading implies. Hand-derived expectations with
# a wrong-case mutation per rule (identity remap; Sec9.13's conjugation).
# Opt-in in every consumer (`--bone-palette`); pose.h/animated_pose.h are
# unchanged.
add_executable(sr3bonepalette_tests tests/synthetic_bone_palette_test.cpp)
target_link_libraries(sr3bonepalette_tests PRIVATE sr3rig)
add_test(NAME synthetic_bone_palette_test COMMAND sr3bonepalette_tests)

# `.czh_pc`/`.czn_pc`/`.gzn_pc` zone reader (spec-zone-data-format.md,
# spec-ctorless-types.md Sec5, spec-world-streaming.md) - the CONFIRMED
# parts only: the `.czh_pc` header (shared material block, reused from
# sr3geometry, plus the disassembly-resolved SR3Z block) and the Mesh
# sub-blocks embedded in `.czn_pc`/`.gzn_pc` (reused from sr3mesh). The
# main `.czn_pc` object-placement/property-stream body is deliberately NOT
# attempted here - see include/sr3zone/zone_header.h and
# include/sr3zone/zone_geometry.h for exactly why.
add_library(sr3zone STATIC
    src/zone_header.cpp
    src/zone_geometry.cpp
)
target_include_directories(sr3zone PUBLIC include)
target_link_libraries(sr3zone PUBLIC sr3geometry sr3mesh vpp_container)

add_executable(sr3zone_tests tests/synthetic_zone_test.cpp)
target_link_libraries(sr3zone_tests PRIVATE sr3zone)
add_test(NAME synthetic_zone_test COMMAND sr3zone_tests)

# `_media.bnk_pc` audio-wrapper reader (spec-audio-format.md Sec4/Sec5) -
# the `VWSBPC` block directory and the 16-byte read of a plain `.bnk_pc`'s
# public Wwise `BKHD` prefix that its cross-reference check needs.
#
# SCOPE, since the name invites the wrong assumption: this is NOT an audio
# decoder and must never grow into one. spec-audio-format.md Sec1/Sec3/Sec6.2
# put the Wwise SoundBank format and its Ogg Vorbis payload firmly on the
# third-party side of this project's cleanroom boundary, the same category as
# Bink video and the Steamworks shim. What is IN scope, and all this library
# does, is the Volition-authored directory wrapped around them: a header, a
# 16-byte-record table, and a 0x800-block chain rule structurally identical to
# the one `vpp::Container` already uses. It returns opaque byte ranges and
# never looks inside one. See include/sr3audio/media_bank.h.
#
# BOTH source files are listed here deliberately and this comment is the
# reminder why: HANDOFF Sec9.56.1 records a session that built new modules
# only by hand (build_one.bat / ad-hoc cl.exe) and left this file's target
# graph silently incomplete, so a real `cmake --build` never built them even
# though their suites were green. tools/validation/build_one.bat has been
# extended for media_bank.obj/wwise_bank_id.obj as well - the two must stay
# in step.
add_library(sr3audio STATIC
    src/media_bank.cpp
    src/wwise_bank_id.cpp
)
target_include_directories(sr3audio PUBLIC include)
target_link_libraries(sr3audio PUBLIC vpp_container)

add_executable(sr3audio_tests tests/synthetic_media_bank_test.cpp)
target_link_libraries(sr3audio_tests PRIVATE sr3audio)
add_test(NAME synthetic_media_bank_test COMMAND sr3audio_tests)

# `.clmesh_pc`/`.glmesh_pc` "Level_Mesh" reader (spec-physics-format.md Sec4,
# spec-geometry-format.md Sec4.2) - the static-prop/collision-geometry
# container used throughout the open-world streaming tiles. Reads the
# `0x4fe66afa` header, the two head slot arrays, and the whole trailing
# sub-parser chain; the embedded Mesh sub-blocks go to sr3mesh, and the
# material block to sr3geometry, so neither is reimplemented here.
#
# The `+0xb8` region is NO LONGER OPAQUE and the tail is NO LONGER SEARCHED
# FOR (HANDOFF Sec9.64, spec-physics-format.md Sec4.4.6): `thunk_FUN_00e3e590`
# is a parser, not a resolver, and the whole file walks on ONE computed cursor
# from bodyStart to EOF - 100,384/100,384 (100.0000%) of the shipped
# population, all five archives that carry the format.
#
# WHAT IT DELIBERATELY DOES NOT DO: decode the 56/52/96-byte arrays or the
# 12-/4-byte nested elements (spec-physics-format.md Sec5 lists every one of
# them as OPEN/UNKNOWN, so they are byte ranges and nothing more), decode the
# CONTENTS of the middle region's material records and render-group
# sub-records (they are located, because locating them is what sizes the
# region, but not interpreted), or decode the collision hull's opaque
# precomputed blob. See include/sr3clmesh/level_mesh.h for the places real
# bytes and spec-physics-format.md Sec4.1/Sec4.2 diverge, and HANDOFF Sec9.59
# plus Sec9.64 for the population numbers behind each.
#
# Listed here, and in tools/validation/build_one.bat, at the same time - the
# two must stay in step (HANDOFF Sec9.56.1: a session that built new modules
# only by hand left this file's target graph silently incomplete, so a real
# `cmake --build` never built them even though their suites were green).
add_library(sr3clmesh STATIC
    src/level_mesh.cpp
)
target_include_directories(sr3clmesh PUBLIC include)
target_link_libraries(sr3clmesh PUBLIC sr3geometry sr3mesh vpp_container)

add_executable(sr3clmesh_tests tests/synthetic_clmesh_test.cpp)
target_link_libraries(sr3clmesh_tests PRIVATE sr3clmesh)
add_test(NAME synthetic_clmesh_test COMMAND sr3clmesh_tests)

# Throwaway investigation tool (this session's own task, 2026-09-30): finds a
# real `.clmesh_pc`/`.glmesh_pc` pair in `1018h0.str2_pc` and empirically
# tests where its material records' texture-binding entries resolve their
# name_offset (see tools/clmesh_probe.cpp's own top comment). Needs sr3fxo
# for the real shaderHash->stem CRC join, same mechanism sr3_viewer's
# `vehicle` command already uses.
add_executable(clmesh_probe tools/clmesh_probe.cpp)
target_link_libraries(clmesh_probe PRIVATE sr3clmesh sr3geometry sr3fxo vpp_container)

# Throwaway investigation tool (follow-on task, 2026-09-30): lists real
# entry names matching a substring, recursively - used to find tile 1018's
# own real file names (.czh_pc/.asm_pc/.str2_pc) before assuming a naming
# convention for the placement-correlation investigation.
add_executable(tile1018_list tools/tile1018_list.cpp)
target_link_libraries(tile1018_list PRIVATE vpp_container)

# Throwaway investigation tool (follow-on task, 2026-09-30): tests whether
# SR3Z record `+12`'s name index (include/sr3zone/zone_header.h
# RecordNameIndex(), TARGET explicitly OPEN there) correlates with any real
# registration order this project can already read (sr3asm::AsmManifest),
# for tile 1018 specifically - see tools/tile1018_placement_probe.cpp's own
# top comment for the exact, falsifiable geometric test.
add_executable(tile1018_placement_probe tools/tile1018_placement_probe.cpp)
target_link_libraries(tile1018_placement_probe PRIVATE sr3asm sr3zone vpp_container)

# .csrt_pc/.gsrt_pc tree geometry reader (spec-tree-format.md) - an
# assembly of blocks this project already knows how to parse (the shared
# material-reference block from sr3geometry, the same inline-material-
# definition routine spec-foliage-format.md Sec6 uses, and the shared
# "Mesh" sub-block from sr3mesh) plus one new fixed 0x208-byte 'TREE'
# header, a 0x170-byte geometry sub-structure, and collision capsules.
# Real-data validated 28/28 on the entire real population (sr3_city_0,
# dlc2, dlc3 - HANDOFF.md Sec9.67), reproducing every population
# statistic spec-tree-format.md Sec1/Sec2 states (11 distinct trees, one
# shader hash, capsule radius range 0.06-1.1) exactly.
add_library(sr3tree STATIC
    src/tree.cpp
)
target_include_directories(sr3tree PUBLIC include)
target_link_libraries(sr3tree PUBLIC sr3geometry sr3mesh vpp_container)

add_executable(sr3tree_tests tests/synthetic_tree_test.cpp)
target_link_libraries(sr3tree_tests PRIVATE sr3tree)
add_test(NAME synthetic_tree_test COMMAND sr3tree_tests)

# --- Engine/renderer phase (HANDOFF.md Sec9) -------------------------------
# D3D11-based renderer built on top of the readers above. Windows-only by
# nature; guarded so a non-Windows configure still succeeds with the
# readers alone rather than failing outright.
#
# D3D11 rather than D3D9 is a constraint, not a preference: the Windows SDK
# ships d3d11.h/dxgi.h/d3dcompiler.h but not d3d9.h. See
# include/sr3render/device.h for the full reasoning, including why the
# game's own .fxo_pc (D3D9 SM1-3) shaders cannot be reused here.
if(WIN32)
    add_library(sr3render STATIC
        src/png_writer.cpp
        src/device.cpp
        src/texture_upload.cpp
        src/quad_renderer.cpp
        src/window.cpp
        src/swap_chain.cpp
        src/mesh_renderer.cpp
    )
    target_include_directories(sr3render PUBLIC include)
    target_include_directories(sr3render PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib
        ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
    )
    # sr3texture for the texture records; zlibstatic for the PNG writer's
    # DEFLATE/CRC-32 (reusing the container format's existing zlib rather
    # than adding an image dependency).
    target_link_libraries(sr3render PUBLIC sr3texture sr3mesh zlibstatic)
    target_link_libraries(sr3render PUBLIC sr3rig)
    target_link_libraries(sr3render PRIVATE d3d11 dxgi d3dcompiler user32 gdi32)

    add_executable(sr3_viewer tools/sr3_viewer.cpp)
    # sr3anim added because `animpose` (stage 2, HANDOFF Sec9.56) calls
    # sr3anim::Animation/Payload/sampleClipAtTime - previously missing
    # here, so this target would not have linked via a real `cmake
    # --build` (see sr3anim's own comment above).
    # sr3vehicle added because `scene` (HANDOFF Sec9's engine/renderer-
    # infrastructure task) calls sr3vehicle::Vehicle::parse() to place a
    # real .ccar_pc alongside character meshes - same "keep the target
    # graph in step with a hand-built binary" lesson as sr3anim above.
    # sr3fxo/sr3d3d9bc added for the new `vehicle` command (real per-draw-
    # range shader render, ported from tools/prototype_real_shader_draw_
    # multishader.cpp - see sr3_viewer.cpp's own "vehicle real-shader
    # viewer integration" section): sr3fxo::WrapperHeader/crc32Raw/
    # hashLowerName for the real shaderHash->stem->VS-file join and T8
    # VS<->PS pairing, sr3d3d9bc for disassembling/CTAB-reading/
    # translating that VS/PS bytecode to real HLSL - previously missing
    # here, same "keep the target graph in step with a hand-built binary"
    # lesson as sr3anim/sr3vehicle above (this scratch build already
    # linked read-only against build_verify/*.obj and ran real vehicles
    # successfully; this is what makes an actual `cmake --build` match
    # it). d3d11/dxgi/d3dcompiler/user32/gdi32 are already sr3render's own
    # PRIVATE link libraries (a STATIC library's link libraries still
    # reach the final executable's link line regardless of PRIVATE/PUBLIC
    # - only usage requirements like include dirs are scoped by it), but
    # are named explicitly here too since sr3_viewer.cpp now calls
    # D3DCompile/D3DReflect directly itself, not just through sr3render.
    # sr3zone added for the new `zone` command (4th golden-scene target,
    # vertex layout code 24 zone-tile geometry): calls
    # sr3zone::ZoneGeometry::locate() to find a .czn_pc/.gzn_pc pair's
    # embedded Mesh sub-block(s) - previously missing here, same "keep the
    # target graph in step with a hand-built binary" lesson as
    # sr3anim/sr3vehicle/sr3fxo above (this command was built and verified
    # via a standalone tests/golden/build.bat cl.exe compile, same as every
    # prior addition here; this line is what makes an actual `cmake
    # --build` match it).
    # sr3clmesh added for the new `clmesh` command (this session's own task,
    # 2026-09-30: real `.clmesh_pc`/`.glmesh_pc` static-prop render with real
    # per-material textures via the new LevelMesh::materialTextureBinding() -
    # see runClmesh()'s own doc comment in sr3_viewer.cpp) - same "keep the
    # target graph in step" lesson as every other addition noted above.
    target_link_libraries(sr3_viewer PRIVATE sr3render sr3geometry sr3mesh sr3rig sr3anim sr3vehicle sr3fxo sr3d3d9bc sr3zone sr3clmesh vpp_container d3d11 dxgi d3dcompiler user32 gdi32)
endif()

# Real Lua 5.1 lexer/parser (include/sr3lua) - built from the PUBLIC Lua 5.1
# manual grammar (Sec8) and operator-precedence table, NOT from disassembly
# (spec-lua-bindings.md itself scopes the stock Lua 5.1 VM/stdlib out of
# this project's cleanroom target - see that spec's own Sec1). No
# vpp_container dependency: it parses text handed to it. Used by the
# missions/UI Lua data-side census (tools/lua_mission_ui_census.cpp), which
# DOES depend on vpp_container to actually locate the shipped .lua scripts.
add_library(sr3lua STATIC
    src/lua_lexer.cpp
    src/lua_parser.cpp
)
target_include_directories(sr3lua PUBLIC include)

add_executable(sr3lua_tests tests/synthetic_lua_test.cpp)
target_link_libraries(sr3lua_tests PRIVATE sr3lua)
add_test(NAME synthetic_lua_test COMMAND sr3lua_tests)

add_executable(lua_mission_ui_census tools/lua_mission_ui_census.cpp)
target_link_libraries(lua_mission_ui_census PRIVATE sr3lua vpp_container)

# Lua 5.1 HOST SCAFFOLD (new task, 2026-09-29): embeds the real, vendored
# Lua 5.1 interpreter above (third_party/lua51) rather than merely parsing
# Lua text (sr3lua, above, still does only that). sr3luahost registers
# every real engine-provided name in whichever tagged registration list
# it's given (originally tools/lua_all_registered_1430_tagged.txt, 1,430
# names; superseded 2026-09-29 by the corrected
# tools/lua_all_registered_1435_tagged.txt, 1,435 names, spec-lua-bindings.md
# Sec13.6 - see tools/lua_host_run.cpp's own invocation for which file a
# given run actually loads) as generic logging stub C functions bound into
# two real lua_State globals tables (include/sr3luahost/host.h's own top
# comment: the real gameplay/UI state split, spec-lua-bindings.md Sec13.2).
# Extended, same-day follow-up task: src/lua_hook_registry.cpp is the
# static confirmed/pattern-based engine->Lua hook name table (originally
# 133 entries, extended to 138 same-day per spec Sec14.6 -
# include/sr3luahost/hook_registry.h, spec-lua-bindings.md Sec8/Sec12/
# Sec14) and src/lua_thread_scheduler.cpp is the minimal real-Lua-5.1-
# coroutine-backed thread_new/thread_yield/thread_kill/thread_check_done/
# thread_close registration (include/sr3luahost/thread_scheduler.h) -
# together they let tools/lua_host_run.cpp (below) actually call INTO the
# hook functions real shipped scripts define, not just their own
# top-level statements.
#
# Same-day follow-up task (2026-09-29): src/lua_engine_state.cpp
# (include/sr3luahost/engine_state.h) is a minimal in-memory engine-state
# layer, and src/lua_spec_confirmed_stubs.cpp
# (include/sr3luahost/spec_confirmed_stubs.h) promotes 22 of the generic
# logging stubs above to real, spec-confirmed behavior (12 from
# spec-lua-api-behaviour.md Sec1-9, plus vint_object_find from
# spec-lua-bindings.md Sec13.7/Sec15, folded in mid-task once Team A found
# it was simply unregistered - 43% of all hook-fire errors; +9 more from a
# same-shape follow-up pass 2026-09-30, spec-lua-api-behaviour.md
# Sec10.1-Sec10.9). Links `user32` (new in the original task) for
# game_get_key_name's real Win32 GetKeyNameTextW call - a standard OS API,
# not disassembly-derived.
add_library(sr3luahost STATIC
    src/lua_stub_registry.cpp
    src/lua_host.cpp
    src/lua_hook_registry.cpp
    src/lua_thread_scheduler.cpp
    src/lua_engine_state.cpp
    src/lua_spec_confirmed_stubs.cpp
)
target_include_directories(sr3luahost PUBLIC include)
target_link_libraries(sr3luahost PUBLIC lua51)
if(WIN32)
    target_link_libraries(sr3luahost PUBLIC user32)
endif()

add_executable(sr3luahost_tests tests/synthetic_luahost_test.cpp)
target_link_libraries(sr3luahost_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_test COMMAND sr3luahost_tests)

# The verdict tool: loads game_lib.lua for real and attempts to load/run a
# real, stated subset of the 804 shipped mission/UI scripts against the
# stub environment above, reporting real luaL_loadbuffer/lua_pcall
# results plus real stub-hit counts. Reuses the same archive-walking
# method as lua_mission_ui_census.cpp (see tools/lua_host_run.cpp's own top
# comment) and cross-checks each script's real Lua 5.1 load result against
# sr3lua's own parse result (sr3lua linked for that cross-check only).
# Real per-script state-tag lookup (this task, spec-lua-bindings.md
# Sec14.5): links sr3asm so the tool can walk the real .asm_pc manifests
# and restrict fireConfirmedHooks() to each script's own real state - see
# tools/lua_host_run.cpp's own `statetag` namespace for the full method.
add_executable(lua_host_run tools/lua_host_run.cpp)
target_link_libraries(lua_host_run PRIVATE sr3luahost vpp_container sr3lua sr3asm)

# Per-script entry-point naming census (HANDOFF Sec9.118): for each real
# script, every TOP-LEVEL global function named "<scriptStem>_<suffix>",
# histogrammed. Reuses the same archive-walking method as the tools
# above; needs sr3lua for the real parse (GlobalDefine::isTopLevel).
add_executable(lua_entrypoint_census tools/lua_entrypoint_census.cpp)
target_link_libraries(lua_entrypoint_census PRIVATE sr3lua vpp_container)

# Two clean-side checks for HANDOFF Sec9.119 (how missions/UI screens are
# entered): does any real .xtbl table name a mission-entry function/stem
# (xtbl_name_grep), and does any real script build an entry-point name
# DYNAMICALLY (lua_dynamic_dispatch_grep)? Plain substring scanners, no
# typed-table dependency needed - kept as standalone tools, not part of
# the sr3tables_* domain libraries.
add_executable(xtbl_name_grep tools/xtbl_name_grep.cpp)
target_link_libraries(xtbl_name_grep PRIVATE vpp_container)
add_executable(lua_dynamic_dispatch_grep tools/lua_dynamic_dispatch_grep.cpp)
target_link_libraries(lua_dynamic_dispatch_grep PRIVATE vpp_container)

# Call-SITE census (HANDOFF Sec9.121): for every real <stem>_start/_run/
# _success/_init/_cleanup definition, who calls it (same script/other
# script/never) - closes the "what calls <stem>_start" thread.
add_executable(lua_callsite_census tools/lua_callsite_census.cpp)
target_link_libraries(lua_callsite_census PRIVATE sr3lua vpp_container)

# Bare (non-call/non-define) name-reference census (HANDOFF Sec9.123):
# is any <stem>_start ever handed around as a plain value (table field,
# argument, assignment) rather than called or defined? Token-level scan,
# stated as pattern-matching not full AST tracking - see the file's own
# top comment.
add_executable(lua_bare_reference_census tools/lua_bare_reference_census.cpp)
target_link_libraries(lua_bare_reference_census PRIVATE sr3lua vpp_container)

# Mission-package census (HANDOFF Sec9.125): over stream_grid.asm_pc (and
# every other real .asm_pc manifest, for completeness), which missions
# have a "Mission LUA script" modal container, what else it holds, and
# which of the 54 real _start-defining stems it covers.
add_executable(mission_package_census tools/mission_package_census.cpp)
target_link_libraries(mission_package_census PRIVATE sr3asm vpp_container)

# .vint_doc UI document reader (spec-vint-doc-format.md), cloud phase
# 2026-09-30. CONFIRMED header/string-table/record shapes only - no
# full-document walk yet (three layout facts are OPEN; see
# include/sr3vintdoc/vint_doc.h). vintdoc_validate is the real-data
# population sweep run through the PC bridge
# (bridge-jobs/03_vintdoc_validate.json); it also scores the OPEN layout
# candidates as a labelled hypothesis test.
add_library(sr3vintdoc STATIC src/vint_doc.cpp)
target_include_directories(sr3vintdoc PUBLIC include)
target_link_libraries(sr3vintdoc PUBLIC vpp_container)

add_executable(sr3vintdoc_tests tests/synthetic_vintdoc_test.cpp)
target_link_libraries(sr3vintdoc_tests PRIVATE sr3vintdoc)
add_test(NAME synthetic_vintdoc_test COMMAND sr3vintdoc_tests)

add_executable(vintdoc_validate tools/vintdoc_validate.cpp)
target_link_libraries(vintdoc_validate PRIVATE sr3vintdoc vpp_container)

# libFuzzer harnesses + seed capture (fuzz/fuzz.cmake). Opt-in, Clang only.
option(CRREISH_FUZZ "Build the libFuzzer harnesses (Clang)" OFF)
if(CRREISH_FUZZ)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "CRREISH_FUZZ needs Clang (libFuzzer)")
    endif()
    include(fuzz/fuzz.cmake)
endif()

# Bridge-result diff tool (tools/bridge_diff.py) unit test, when Python is available.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME bridge_diff_py COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/bridge_diff_test.py)
endif()

# Real-data validation harnesses (tools/validation/*.cpp), plus two loose
# tools, as CMake targets - the portable equivalent of
# tools/validation/build_one.bat, which links every harness against every
# reader library. Cloud phase 2026-09-30: lets the PC bridge build them by
# target name and keeps them compiling in Linux CI. Harnesses that need D3D
# (d3dcompiler/d3d11) or sr3render stay Windows-only.
option(CRREISH_VALIDATION_TOOLS "Build tools/validation harnesses (not yet verified on Linux)" OFF)
if(CRREISH_VALIDATION_TOOLS)
    set(CRREISH_ALL_READER_LIBS vpp_container sr3save sr3asm sr3fxo sr3effects sr3xtbl
        sr3tables_trafficai sr3tables_weapons sr3tables_progression sr3tables_environment
        sr3tables_animation sr3tables_audio_radio sr3tables_ui_controls sr3tables_vehicle_world
        sr3tables_diversions sr3tables_customization sr3d3d9bc sr3vehicleinfo sr3customization
        sr3texture sr3geometry sr3anim sr3morph sr3foliage sr3conversation sr3cutscene sr3mesh
        sr3vehicle sr3rig sr3zone sr3audio sr3clmesh sr3tree sr3lua sr3luahost sr3vintdoc)
    set(CRREISH_WINDOWS_ONLY_VALIDATION d3dctest_semantics d3dctest_semantics_sm4
        validate_d3d9bc_hlsl_population validate_d3d9bc_hlsl_sm4_population validate_peg_population)
    file(GLOB CRREISH_VALIDATION_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tools/validation/*.cpp)
    list(APPEND CRREISH_VALIDATION_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/golden_scene_check.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/clmesh_lead_probe.cpp)
    foreach(src ${CRREISH_VALIDATION_SOURCES})
        get_filename_component(name ${src} NAME_WE)
        if(NOT WIN32 AND name IN_LIST CRREISH_WINDOWS_ONLY_VALIDATION)
            continue()
        endif()
        add_executable(${name} ${src})
        target_link_libraries(${name} PRIVATE ${CRREISH_ALL_READER_LIBS} zlibstatic)
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib
            ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib)
        if(WIN32 AND name IN_LIST CRREISH_WINDOWS_ONLY_VALIDATION)
            target_link_libraries(${name} PRIVATE d3dcompiler d3d11 dxgi)
        endif()
    endforeach()
endif()
