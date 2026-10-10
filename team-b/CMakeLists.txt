cmake_minimum_required(VERSION 3.16...3.25)
project(vpp_container_reader CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4)
endif()

# --- Build-infra: compiler cache (sccache) support (B1 task, 2026-10-07) --
# Wires sccache (https://github.com/mozilla/sccache, Apache-2.0) as the
# CXX/C compiler launcher WHEN it's actually on PATH - find_program() below
# gates this so a machine/agent without sccache installed just builds
# normally; no hard dependency is added. NOT present in this sandbox by
# default (confirmed directly, 2026-10-07): install via
# `winget install --id Mozilla.sccache -e --accept-package-agreements
# --accept-source-agreements` (confirmed working) or `cargo install
# sccache`. Either puts `sccache` on PATH for NEW shells/processes only - a
# shell or agent host process already running before the install keeps its
# old PATH until restarted (observed directly installing it this session).
#
# ONE shared cache dir across every worktree of this project, not
# per-worktree: this project's whole agent fleet builds from many
# concurrent git worktrees (.claude/worktrees/<agent-id>/, each its own
# full source checkout), so a per-worktree cache would mean every single
# agent's first build is always a 100% miss - the entire point of a
# compiler cache is lost. Resolution order: respect $SCCACHE_DIR from the
# real environment if an operator already set it (e.g. once in a machine-
# wide profile); otherwise default to a fixed path keyed by username only
# (via %LOCALAPPDATA%, which is per-user, not per-source-checkout) so it's
# naturally the same path regardless of which worktree configures first.
# The env var is injected directly into the generated per-TU compile
# command itself (`cmake -E env SCCACHE_DIR=... sccache <real compiler>`)
# rather than via `set(ENV{...})` here, because a plain configure-time
# `set(ENV{...})` only affects THIS cmake process and is invisible to the
# later, separate `cmake --build`/ninja/msbuild invocation that actually
# runs the compiler - `cmake -E env` bakes the variable into the build
# rule itself, so it's correct regardless of the ambient shell's own env
# at build time, for both the Ninja and Visual Studio generators.
if(DEFINED ENV{SCCACHE_DIR})
    set(CRREISH_SCCACHE_DIR "$ENV{SCCACHE_DIR}" CACHE PATH
        "Shared sccache cache directory (same across every worktree)")
else()
    set(CRREISH_SCCACHE_DIR "$ENV{LOCALAPPDATA}/sccache" CACHE PATH
        "Shared sccache cache directory (same across every worktree)")
endif()
find_program(CRREISH_SCCACHE_PROGRAM sccache)
if(CRREISH_SCCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env
        "SCCACHE_DIR=${CRREISH_SCCACHE_DIR}" ${CRREISH_SCCACHE_PROGRAM})
    set(CMAKE_C_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env
        "SCCACHE_DIR=${CRREISH_SCCACHE_DIR}" ${CRREISH_SCCACHE_PROGRAM})
    message(STATUS "sccache found (${CRREISH_SCCACHE_PROGRAM}) - wired as "
        "compiler launcher, shared cache dir: ${CRREISH_SCCACHE_DIR}")

    # MSVC's default /Zi writes debug info to one separate, shared .pdb
    # that concurrent TUs all touch - a moving target sccache can't hash
    # consistently (sccache's own docs call this out as the standard MSVC
    # caveat). /Z7 (CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded) embeds
    # debug info directly in each .obj instead, which is what actually
    # makes MSVC cache hits possible. This variable and its gating policy
    # (CMP0141) were both introduced in CMake 3.25; this project's own
    # cmake_minimum_required() floor is 3.16, so this file's line 1 uses
    # the RANGE form `cmake_minimum_required(VERSION 3.16...3.25)` rather
    # than a plain `cmake_policy(SET CMP0141 NEW)` call here - confirmed
    # directly (an isolated minimal CMakeLists.txt reproduced this exactly)
    # that setting the policy AFTER project() is too late: CMake has
    # already baked the legacy "/Zi" into CMAKE_CXX_FLAGS_DEBUG's default
    # by then, so the newer MSVC_DEBUG_INFORMATION_FORMAT abstraction's own
    # flag gets ADDED alongside the stale one instead of replacing it. The
    # range form resolves every policy up to 3.25 (CMP0141 included) to
    # NEW before project() even runs, on any CMake new enough to know it -
    # exactly like `cmake_policy(VERSION ...)` would, but expressible at
    # the mandatory first line instead of requiring project() to move. On
    # a CMake older than 3.16...3.25's upper bound, the unknown policies
    # just keep their old default (same graceful degradation as before);
    # the 3.16 floor itself is unchanged, so nothing breaks for anyone.
    # Setting the variable itself is harmless either way - an older CMake
    # that has never heard of CMAKE_MSVC_DEBUG_INFORMATION_FORMAT simply
    # ignores an unused variable, same as setting any other unknown name.
    #
    # Also deliberately NOT paired with target_precompile_headers anywhere:
    # checked this project's CMakeLists.txt for existing PCH usage first
    # (none - no target_precompile_headers call exists here) - a PCH would
    # fight sccache's own hashing the same way /Zi's shared PDB does, so
    # this note is left here for whoever adds the first PCH target later.
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded")
elseif(DEFINED ENV{SR3_REQUIRE_SCCACHE})
    message(FATAL_ERROR "sccache not found on PATH and SR3_REQUIRE_SCCACHE "
        "is set - install via 'winget install --id Mozilla.sccache -e' or "
        "'cargo install sccache', then restart the shell")
else()
    message(STATUS "sccache not found on PATH - building without a "
        "compiler cache (install via 'winget install --id Mozilla.sccache "
        "-e' or 'cargo install sccache', then restart the shell/agent host "
        "process so the PATH change takes effect)")
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

# Vendored, real, unmodified libogg 1.3.5 + libvorbis 1.3.7 (Xiph.org's own
# official release tarballs, downloads.xiph.org, retrieved 2026-10-08;
# BSD-style license, see third_party/libogg/COPYING and
# third_party/libvorbis/COPYING) - the audio bring-up task's own decode
# backend (spec-audio-runtime.md Sec11.11's own text: "libvorbis, BSD-
# licensed, covers that for Team B" for the generic Vorbis-bitstream math;
# spec-bink-boundary.md's FFmpeg precedent is the same category of
# off-the-shelf, non-clean-room, third-party codec dependency). Not a single
# .c/.h file has been edited in either tree - everything under
# third_party/libogg and third_party/libvorbis beyond this comment is the
# untouched upstream distribution (including each project's own upstream
# CMakeLists.txt/Makefile.am, kept for provenance but unused - same
# "our own CMake wrapper, not upstream's" choice this project already made
# for zlib above, because both libogg's and libvorbis's own CMake expect an
# installed/found sibling package and a generated config.h this project does
# not want to run autotools or a second CMake configure step to produce).
#
# No generated config header is needed on this platform: libogg's own
# include/ogg/os_types.h has a native `#elif defined(_WIN32)` branch (MSVC
# 2013+: plain <stdint.h> types) that is checked BEFORE the `#include
# <ogg/config_types.h>` fallback, so config_types.h (which only the
# configure/cmake-generated #else path reaches) is never included when
# building for Windows - verified by reading the header directly, not
# assumed. libvorbis's lib/os.h only `#include "config.h"` when
# HAVE_CONFIG_H is defined, which nothing here defines, so no config.h is
# needed either; its own platform `#ifdef jail` already covers MSVC x64
# (the `_MSC_VER && _M_X64` branch, SSE2 `vorbis_ftoi`) and MSVC x86
# (`_MSC_VER && _M_IX86`, inline asm) natively.
set(OGG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libogg)
add_library(oggstatic STATIC
    ${OGG_DIR}/src/bitwise.c
    ${OGG_DIR}/src/framing.c
)
target_include_directories(oggstatic PUBLIC ${OGG_DIR}/include)
if(MSVC)
    target_compile_definitions(oggstatic PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# libvorbis core (decode + the shared analysis/synthesis machinery) +
# vorbisenc (encoder) + vorbisfile (the high-level stdio/Ogg-page-aware
# convenience API) - upstream's own three-library split (lib/Makefile.am's
# libvorbis_la_SOURCES / libvorbisenc_la_SOURCES / libvorbisfile_la_SOURCES,
# read directly to get this exact file list), folded into one static target
# since this project has no shared-library versioning concern.
set(VORBIS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libvorbis)
add_library(vorbisstatic STATIC
    ${VORBIS_DIR}/lib/mdct.c      ${VORBIS_DIR}/lib/smallft.c   ${VORBIS_DIR}/lib/block.c
    ${VORBIS_DIR}/lib/envelope.c  ${VORBIS_DIR}/lib/window.c    ${VORBIS_DIR}/lib/lsp.c
    ${VORBIS_DIR}/lib/lpc.c       ${VORBIS_DIR}/lib/analysis.c  ${VORBIS_DIR}/lib/synthesis.c
    ${VORBIS_DIR}/lib/psy.c       ${VORBIS_DIR}/lib/info.c      ${VORBIS_DIR}/lib/floor1.c
    ${VORBIS_DIR}/lib/floor0.c    ${VORBIS_DIR}/lib/res0.c      ${VORBIS_DIR}/lib/mapping0.c
    ${VORBIS_DIR}/lib/registry.c  ${VORBIS_DIR}/lib/codebook.c  ${VORBIS_DIR}/lib/sharedbook.c
    ${VORBIS_DIR}/lib/lookup.c    ${VORBIS_DIR}/lib/bitrate.c
    ${VORBIS_DIR}/lib/vorbisenc.c
    ${VORBIS_DIR}/lib/vorbisfile.c
)
target_include_directories(vorbisstatic PUBLIC ${VORBIS_DIR}/include)
target_include_directories(vorbisstatic PRIVATE ${VORBIS_DIR}/lib)
target_link_libraries(vorbisstatic PUBLIC oggstatic)
if(MSVC)
    # Upstream assumes a C89/C99 Unix toolchain (fopen/sprintf/strcpy
    # "insecure CRT" deprecation warnings under /W4, plus benign signed/
    # unsigned and unreferenced-parameter warnings) - same narrow,
    # unmodified-vendor-code silencing precedent as third_party/lua51's own
    # CMakeLists.txt (/wd4996 etc.), not a project-wide change.
    target_compile_definitions(vorbisstatic PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(vorbisstatic PRIVATE /wd4996 /wd4244 /wd4267 /wd4100 /wd4701 /wd4706 /wd4305)
endif()

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

# --- Persistent decoded-asset cache (this task, 2026-10-08) ---------------
# Retrofits vpp_container's Container with an optional, pluggable
# decompressEntry() cache (vpp::DecodeCache, container.h) and supplies the
# real, disk-backed implementation here: content-hash-keyed (never mtime -
# see vppcache/cache_paths.h's header comment), bounded-size (LRU-ish
# eviction, CRREISH_DECODED_CACHE_MAX_BYTES), persists across process runs
# under CRREISH_DECODED_CACHE_DIR (env var; falls back to the compiled-in
# default below, <repo>/.cache, when unset). Also the within-process
# shared-archive registry (openArchive() memoizes by path per process) and
# the generic parsed-object registry (object_registry.h). See
# include/vppcache/archive_cache.h's own file-level comment for the full
# design and the NAMING NOTE in cache_paths.h for why this is
# CRREISH_DECODED_CACHE_DIR, not the task brief's literal
# CRREISH_GAME_CACHE_DIR (already a different, pre-existing CMake variable
# a few lines below this one).
set(CRREISH_DECODED_CACHE_DIR_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/.cache" CACHE PATH
    "Default persistent decoded-asset cache directory when the CRREISH_DECODED_CACHE_DIR env var is unset (this task, 2026-10-08)")
add_library(vppcache STATIC
    src/vppcache_hash.cpp
    src/vppcache_cache_paths.cpp
    src/vppcache_file_blob.cpp
    src/vppcache_archive_cache.cpp
    src/vppcache_shader_cache.cpp
)
target_include_directories(vppcache PUBLIC include)
target_link_libraries(vppcache PUBLIC vpp_container)
target_compile_definitions(vppcache PRIVATE
    CRREISH_DECODED_CACHE_DIR_DEFAULT="${CRREISH_DECODED_CACHE_DIR_DEFAULT}")

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

add_executable(vppcache_tests tests/synthetic_vppcache_test.cpp)
target_link_libraries(vppcache_tests PRIVATE vppcache)
target_include_directories(vppcache_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib
    ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
)
add_test(NAME synthetic_vppcache_test COMMAND vppcache_tests)

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

# Player runtime U0 (input query contract), U1 (control-state machine), U2
# (movement and intent), U3 (locomotion bridge) and U4 (camera mode machine
# and follow camera), spec-player-runtime.md Sec1/Sec2/Sec3/Sec4/Sec5. Pure
# machinery with injectable device/collision sources;
# depends only on the header-only sr3luahost/open_state.h (same include
# root) for the OPEN-state discipline.
add_library(sr3player STATIC
    src/player_input.cpp
    src/player_control_state.cpp
    src/player_movement.cpp
    src/player_locomotion.cpp
    src/player_camera.cpp
    src/player_follow_camera.cpp
    src/player_character_collision.cpp
)
target_include_directories(sr3player PUBLIC include)
# character_collision.h drives the REAL sr3physics::CharacterProxyStepper/
# CharacterProxyListener (2026-10-07 character-proxy/collision follow-up
# task) - see that header's own doc comment. sr3physics is defined below;
# the link is declared there (target_link_libraries(sr3player PUBLIC
# sr3physics)) since both targets must already exist.

add_executable(sr3player_tests tests/synthetic_player_runtime_test.cpp)
target_link_libraries(sr3player_tests PRIVATE sr3player)
add_test(NAME synthetic_player_runtime_test COMMAND sr3player_tests)

add_executable(sr3player_character_collision_tests tests/synthetic_player_character_collision_test.cpp)
target_link_libraries(sr3player_character_collision_tests PRIVATE sr3player)
add_test(NAME synthetic_player_character_collision_test COMMAND sr3player_character_collision_tests)

# Equip-time hitscan fire dispatch (spec-weapons-runtime.md Sec2.2a-Sec2.2c/
# Sec2.3, this task 2026-10-08): the real weapon::+0x8c/item::+0x8c body
# (vehicle-weapon short-circuit, refuse gate, dry-fire-vs-normal sound
# dispatch, rounds-in-clip decrement with infinite-ammo bit, the 3 generic
# timer-primitive writes including the HIGH-CONFIDENCE 1000ms refire-delay
# candidate), plus a CHOSEN (not traced - spec's own hit-detection gap is
# genuinely OPEN) raycast+damage-sink wiring built on sr3player's own
# existing RayVsTriangle3D primitive. See include/sr3weapons/weapon_fire.h's
# own top doc comment for the full citation. Depends on sr3tables_weapons
# (the weapons.xtbl row type) and sr3player (Vec3/RayVsTriangle3D) only -
# same tier as sr3airuntime/sr3physics, deliberately not wired to sr3luahost
# (see that header's own LAYERING NOTE).
add_library(sr3weapons STATIC
    src/weapon_fire.cpp
)
target_include_directories(sr3weapons PUBLIC include)
target_link_libraries(sr3weapons PUBLIC sr3tables_weapons sr3player)

add_executable(sr3weapons_tests tests/synthetic_weapon_fire_test.cpp)
target_link_libraries(sr3weapons_tests PRIVATE sr3weapons)
add_test(NAME synthetic_weapon_fire_test COMMAND sr3weapons_tests)

# Ground-follow helper (include/sr3player/ground_follow.h, header-only,
# added 2026-10-08 for the live-playtest ground-missing fix) - see that
# header's own file banner for the full CONFIRMED-contract-shape/CHOSEN-
# mechanism breakdown and its two consumers (the player's own ground-
# target smoothing feeding CharacterProxyDriver::setGroundY(), and the
# parked vehicle's own Y, which has no other vertical model at all).
add_executable(sr3player_ground_follow_tests tests/synthetic_ground_follow_test.cpp)
target_link_libraries(sr3player_ground_follow_tests PRIVATE sr3player)
add_test(NAME synthetic_ground_follow_test COMMAND sr3player_ground_follow_tests)

# Physics runtime boundary: the frame contract (Delta-t clamp, the
# interlocked physics window and its queue/drop mutation discipline), the
# bring-up/shutdown order, the character-proxy lock-step pass cap with
# MyCharacterListener's queue-only contract, and (job_dispatch.h, unit W-U0)
# the Havok job queue's own per-job-type dispatch table standing behind
# IPhysicsBackend::drainJobs(). spec-physics-runtime.md Sec4.3/Sec4.4/
# Sec4.5/Sec4.6/Sec9, Sec2.1/Sec2.2; cross-referenced to
# spec-player-runtime.md Sec5.6.4/Sec5.8 for the lock-step passes and the
# listener's queue targets. NOT a real Havok integration - IPhysicsBackend/
# IGameJobScheduler/JobTypeTable are injectable seams; the null/inline/
# job-queue stand-ins still obey the observable contract (queue vs. drop,
# pass cap, shutdown order, per-job-type routing). Depends only on the
# header-only sr3luahost/open_state.h (same include root) for job_dispatch.h's
# one OPEN value (the job-type count N, Sec9.2 OPEN item 36).
add_library(sr3physics STATIC
    src/physics_frame_contract.cpp
    src/physics_lifecycle.cpp
    src/physics_character_proxy.cpp
    src/physics_job_dispatch.cpp
    src/physics_vehicle_substeps.cpp
    src/physics_vehicle_driving_slice.cpp
    src/physics_vehicle_world_collision.cpp
)
target_include_directories(sr3physics PUBLIC include)

add_executable(sr3physics_tests tests/synthetic_physics_runtime_test.cpp)
target_link_libraries(sr3physics_tests PRIVATE sr3physics)
add_test(NAME synthetic_physics_runtime_test COMMAND sr3physics_tests)

# vehicle_substeps.h (2026-10-08 Havok-vehicle-sub-steps bring-up task) models
# spec-physics-runtime.md Sec6's 13 confirmed vehicle sub-steps, consuming
# real vehicle tuning only through sr3vehicleinfo::VehicleEntry - the link is
# declared further down (target_link_libraries(sr3physics PUBLIC
# sr3vehicleinfo)) since sr3vehicleinfo is defined later in this file and
# both targets must already exist (same pattern as sr3player/sr3physics
# above).
add_executable(sr3physics_vehicle_substeps_tests tests/synthetic_physics_vehicle_substeps_test.cpp)
target_link_libraries(sr3physics_vehicle_substeps_tests PRIVATE sr3physics)
add_test(NAME synthetic_physics_vehicle_substeps_test COMMAND sr3physics_vehicle_substeps_tests)

# vehicle_world_collision.h (vehicle-world-collision follow-up task,
# 2026-10-08, spec-physics-runtime.md Sec10a) - the vehicle-vs-world
# collision RESPONSE (plain velocity-clamp impulse at a swept-sphere
# contact), decoupled from any concrete collision-world implementation so it
# is testable here with a synthetic double, independent of
# tools/sr3_viewer.cpp's real `.clmesh_pc`/D3D11 wiring (PlaytestVehicleRig::
# ApplyWorldCollisionResponse() there is the one real caller).
add_executable(sr3physics_vehicle_world_collision_tests tests/synthetic_physics_vehicle_world_collision_test.cpp)
target_link_libraries(sr3physics_vehicle_world_collision_tests PRIVATE sr3physics)
add_test(NAME synthetic_physics_vehicle_world_collision_test COMMAND sr3physics_vehicle_world_collision_tests)

# vehicle_driving_slice.h (2026-10-08 follow-up task) models spec-physics-
# runtime.md Sec10's 7 named hkpVehicle component override classes - see that
# header's own doc comment for its relationship to vehicle_substeps.h above
# (not wired together this pass).
add_executable(sr3physics_vehicle_driving_slice_tests tests/synthetic_physics_vehicle_driving_slice_test.cpp)
target_link_libraries(sr3physics_vehicle_driving_slice_tests PRIVATE sr3physics)
add_test(NAME synthetic_physics_vehicle_driving_slice_test COMMAND sr3physics_vehicle_driving_slice_tests)

# sr3player/character_collision.h (2026-10-07 follow-up task) drives this
# library's real CharacterProxyStepper/CharacterProxyListener - see that
# header's own doc comment.
target_link_libraries(sr3player PUBLIC sr3physics)

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

# sr3physics/vehicle_substeps.h (2026-10-08 Havok-vehicle-sub-steps bring-up
# task) builds its VehicleTuning only from a real sr3vehicleinfo::VehicleEntry
# - see that header's own doc comment.
target_link_libraries(sr3physics PUBLIC sr3vehicleinfo)

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
    src/character_matlib.cpp
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

# Per-identity matlib resolution/attachment model (src/character_matlib.cpp,
# part of sr3geometry above) - spec-tables-customization.md §12.1b. A
# distinct concern from the in-mesh material binding above (this is the
# identity-keyed, name-keyed CROSS-FILE attachment step, not a within-mesh
# slot reader), so it gets its own test executable rather than folding into
# either existing one.
add_executable(sr3charactermatlib_tests tests/synthetic_character_matlib_test.cpp)
target_link_libraries(sr3charactermatlib_tests PRIVATE sr3geometry)
add_test(NAME synthetic_character_matlib_test COMMAND sr3charactermatlib_tests)

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

# Animation runtime U0 (control-parameter contract) and U1 (controller state
# slots, play/query API, cross-fade), spec-anim-runtime.md Sec1-Sec6. A
# stateful runtime library in the shape of sr3player: pure machinery with
# injected data / layer (U4) / evaluator (U3) boundaries; depends on sr3xtbl
# for control_parameters.xtbl, sr3anim for clip header timing, and the
# header-only sr3luahost/open_state.h for the OPEN-state discipline.
#
# U2 (node-graph file format, Sec7: anim_node_graph.cpp) and U3 (node-graph
# runtime, Sec8: anim_node_graph_runtime.cpp) live in the same library; U3
# implements U1's EvaluationBoundary.
add_library(sr3animruntime STATIC
    src/anim_control_parameters.cpp
    src/anim_crossfade.cpp
    src/anim_controller.cpp
    src/anim_node_graph.cpp
    src/anim_node_graph_runtime.cpp
)
target_include_directories(sr3animruntime PUBLIC include)
target_link_libraries(sr3animruntime PUBLIC sr3xtbl sr3anim)

add_executable(sr3animruntime_tests tests/synthetic_anim_runtime_test.cpp)
target_link_libraries(sr3animruntime_tests PRIVATE sr3animruntime)
add_test(NAME synthetic_anim_runtime_test COMMAND sr3animruntime_tests)

add_executable(sr3animruntime_node_graph_tests tests/synthetic_anim_node_graph_test.cpp)
target_link_libraries(sr3animruntime_node_graph_tests PRIVATE sr3animruntime)
add_test(NAME synthetic_anim_node_graph_test COMMAND sr3animruntime_node_graph_tests)

add_executable(sr3animruntime_node_graph_runtime_tests tests/synthetic_anim_node_graph_runtime_test.cpp)
target_link_libraries(sr3animruntime_node_graph_runtime_tests PRIVATE sr3animruntime)
add_test(NAME synthetic_anim_node_graph_runtime_test COMMAND sr3animruntime_node_graph_runtime_tests)

# Player<->animation coupling ("a character walks"): the field -> control-
# parameter writer (spec-anim-runtime.md Sec4.2), driving the player's own
# node graph through sr3animruntime's real U2/U3 loader+evaluator every
# frame, the scoped-down post step (T/E split, see include/sr3playeranim/
# bridge.h's own banner for the CHOSEN simplification), and the playback-
# rate formula (spec-player-runtime.md Sec5.4.3 step 7). Depends on both
# sr3player and sr3animruntime - the first unit that does.
add_library(sr3playeranim STATIC src/player_anim_bridge.cpp)
target_include_directories(sr3playeranim PUBLIC include)
target_link_libraries(sr3playeranim PUBLIC sr3player sr3animruntime)

add_executable(sr3playeranim_tests tests/synthetic_player_anim_bridge_test.cpp)
target_link_libraries(sr3playeranim_tests PRIVATE sr3playeranim)
add_test(NAME synthetic_player_anim_bridge_test COMMAND sr3playeranim_tests)

# Animation runtime U4 ("Time, layers and the pose hand-off",
# spec-anim-runtime.md Sec11) - the layer-sampler -> compose bridge this
# project never implemented: turns a frame's real GraphOutputList into one
# posed skeleton by calling the EXISTING sr3anim clip sampler (sample.cpp)
# per active layer and blending with the real per-layer weight formula
# (spec-player-runtime.md Sec5.4.3 step 3, reused here - see
# include/sr3animpose/pose_compose.h's own file banner for the exact
# citation and confidence tier). pose_compose.cpp is pure blend math
# (depends only on sr3anim's BoneSample/Quat types); clip_pose_bridge.cpp
# adds the record-resolution seam (ClipSource, OPEN by default) and the
# shared-phase clock a looping clip's own absolute sample time needs
# (neither OutputEntry nor PostedClip carries one - see
# LoopPhaseTracker's own comment).
add_library(sr3animpose STATIC
    src/anim_pose_compose.cpp
    src/clip_pose_bridge.cpp
)
target_include_directories(sr3animpose PUBLIC include)
target_link_libraries(sr3animpose PUBLIC sr3anim sr3animruntime)

add_executable(sr3animpose_tests tests/synthetic_anim_pose_compose_test.cpp)
target_link_libraries(sr3animpose_tests PRIVATE sr3animpose)
add_test(NAME synthetic_anim_pose_compose_test COMMAND sr3animpose_tests)

# Population/stress test (this U4 task's own requirement, not a ctest): runs
# the real compose pipeline against many real locomotion-family clips over
# several real rigs from the real shipped archives, checking for NaN/Inf/
# off-unit-length transforms. Kept as its own standalone tool (not folded
# into the tools/validation/*.cpp glob below) so it does not change that
# glob's shared CRREISH_ALL_READER_LIBS link set for every other harness.
add_executable(validate_anim_pose_compose_population tools/validate_anim_pose_compose_population.cpp)
target_link_libraries(validate_anim_pose_compose_population PRIVATE sr3animpose sr3rig vpp_container)

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
    src/cvtf.cpp
)
target_include_directories(sr3vehicle PUBLIC include)
target_link_libraries(sr3vehicle PUBLIC vpp_container)

# .cvtf_pc compiled vehicle-customization catalogue (spec-geometry-format.md
# §5.1) - was never wired into any CMake target before this reader-
# correctness pass (2026-10-03, the 48-byte-header fix); added here so it
# is actually compiled and tested going forward.
add_executable(sr3vehicle_cvtf_tests tests/synthetic_cvtf_test.cpp)
target_link_libraries(sr3vehicle_cvtf_tests PRIVATE sr3vehicle)
add_test(NAME synthetic_cvtf_test COMMAND sr3vehicle_cvtf_tests)

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
# spec-ctorless-types.md Sec5, spec-world-streaming.md) - the `.czh_pc`
# header (shared material block, reused from sr3geometry, plus the
# disassembly-resolved SR3Z block), the Mesh sub-blocks embedded in
# `.czn_pc`/`.gzn_pc` (reused from sr3mesh), and - added 2026-10-07,
# spec-zone-data-format.md Sec11, after the owner lifted the standing
# `.czn_pc`-interior hold - the Objects (0x2234) placement/property stream
# itself (include/sr3zone/zone_objects.h, src/zone_objects.cpp): the
# FBOW/version-5 payload header, the 28-byte per-object record head
# (tail-length at on-disk +0x16), the variable property-list tail, and the
# 32-bit-hash-through-510-bucket class-hash resolution (bucket index only -
# the runtime-populated class table beyond it is Team A's own open U2
# work, not resolved here; see zone_objects.h's own file-level comment).
add_library(sr3zone STATIC
    src/zone_header.cpp
    src/zone_geometry.cpp
    src/zone_objects.cpp
    src/traffic_graph.cpp
    src/zone_heightmap.cpp
)
target_include_directories(sr3zone PUBLIC include)
target_link_libraries(sr3zone PUBLIC sr3geometry sr3mesh vpp_container)

add_executable(sr3zone_tests tests/synthetic_zone_test.cpp)
target_link_libraries(sr3zone_tests PRIVATE sr3zone)
add_test(NAME synthetic_zone_test COMMAND sr3zone_tests)

add_executable(sr3zone_objects_tests tests/synthetic_zone_objects_test.cpp)
target_link_libraries(sr3zone_objects_tests PRIVATE sr3zone)
add_test(NAME synthetic_zone_objects_test COMMAND sr3zone_objects_tests)

# Traffic (0x2236) record reader (spec-zone-data-format.md Sec13.2,
# include/sr3zone/traffic_graph.h/src/traffic_graph.cpp, added 2026-10-08
# for the traffic-AI lane-following task) - the segment/extended-segment/
# point-pool graph a vehicle's lane-following logic walks
# (sr3airuntime/traffic_lane_follower.h below consumes this, does not
# re-parse it).
add_executable(sr3zone_traffic_graph_tests tests/synthetic_traffic_graph_test.cpp)
target_link_libraries(sr3zone_traffic_graph_tests PRIVATE sr3zone)
add_test(NAME synthetic_traffic_graph_test COMMAND sr3zone_traffic_graph_tests)

# Heightmap (0x2244) record reader + grid sampler (spec-zone-data-format.md
# Sec13.5, include/sr3zone/zone_heightmap.h/src/zone_heightmap.cpp, added
# 2026-10-08 for the live-playtest ground-following fix) - the real per-
# tile elevation data tools/sr3_viewer.cpp's playtest command samples
# through sr3player/ground_follow.h instead of its old flat groundY=0.0
# plane. See zone_heightmap.h's own file banner for the full CONFIRMED/
# HIGH-CONFIDENCE/CHOSEN breakdown.
add_executable(sr3zone_heightmap_tests tests/synthetic_zone_heightmap_test.cpp)
target_link_libraries(sr3zone_heightmap_tests PRIVATE sr3zone)
add_test(NAME synthetic_zone_heightmap_test COMMAND sr3zone_heightmap_tests)

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
#
# src/hirc.cpp (2026-10-09, spec-audio-runtime.md Sec8/Sec9.7/Sec9.8 "wave
# 6" spec sync) joins this same library for the same reason: it reads the
# PLAIN `.bnk_pc` sibling's own HIRC chunk far enough to resolve a Sound
# object's declared source/media id against this library's own
# `MediaBank::walk()` record `tag` field - see include/sr3audio/hirc.h's own
# top comment for the full scope line against media_bank.h/wwise_bank_id.h,
# both of which are deliberately left untouched by this addition.
add_library(sr3audio STATIC
    src/media_bank.cpp
    src/wwise_bank_id.cpp
    src/hirc.cpp
)
target_include_directories(sr3audio PUBLIC include)
target_link_libraries(sr3audio PUBLIC vpp_container)

add_executable(sr3audio_tests tests/synthetic_media_bank_test.cpp)
target_link_libraries(sr3audio_tests PRIVATE sr3audio)
add_test(NAME synthetic_media_bank_test COMMAND sr3audio_tests)

add_executable(sr3audio_hirc_tests tests/synthetic_hirc_media_lookup_test.cpp)
target_link_libraries(sr3audio_hirc_tests PRIVATE sr3audio)
add_test(NAME synthetic_hirc_media_lookup_test COMMAND sr3audio_hirc_tests)

# Audio runtime: the Wwise adapter contract (spec-audio-runtime.md). NOT a
# Wwise SDK integration and NOT the sr3audio format reader above (that one
# stays firmly a directory-wrapper/BKHD-prefix reader, per its own header
# comment) - this is the GAME-side call contract (the 25 Sec1 crossings,
# the play-record/voice record layouts of Sec2, the Sec3 init/shutdown
# order, the Sec4 callback path) implemented against an injected
# WwiseBackend interface, satisfied here by a deterministic, logging
# NullWwiseBackend. Pure machinery in the shape of sr3player/
# sr3animruntime: depends only on the header-only sr3luahost/open_state.h
# (same include root) for the OPEN-state discipline (Sec6's open items).
# src/wwise_codebook_library_data.cpp holds the real, static, compiled-in
# Wwise Vorbis codebook-body DATA (71778 raw bytes transcribed from the
# shipped game executable, spec-audio-codebook-library.md's own Provenance
# section) - excluded from this project's public source export per the
# "public repo excludes the game/middleware DATA" decision. OPTIONAL here: if
# present (as it is in every non-public checkout today), it is compiled in as
# before; if absent (as it will be from the public export), this target still
# configures and links cleanly against src/wwise_codebook_library_blob_stub.cpp
# instead (which sr3runtimedata's runtime loader can fill at runtime from the
# player's own game install) - see that stub's own header comment and
# sr3audioruntime::isWwiseCodebookLibraryDataAvailable() (include/
# sr3audioruntime/wwise_codebook_library.h) for how callers (src/
# wwise_setup_rebuild.cpp) detect and report the absent-data case instead of
# silently decoding against a zeroed placeholder.
# 2026-10-10 (runtime data loader task): the 599-row INDEX table
# (blobOffset/byteLen etc.) moved out to its own ALWAYS-compiled source
# (src/wwise_codebook_library_index_data.cpp - derived analysis metadata,
# not excluded game bytes, per spec-runtime-data-sources.md's own framing -
# see that file's own header comment). Only the raw 71778-byte blob itself
# stays EXISTS-gated between a compiled-in dev copy
# (src/wwise_codebook_library_data.cpp) and a runtime-loadable stub
# (src/wwise_codebook_library_blob_stub.cpp, filled at runtime by
# sr3runtimedata's loader instead - see that stub's own header comment).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/wwise_codebook_library_data.cpp")
    set(SR3AUDIORUNTIME_CODEBOOK_BLOB_SOURCE src/wwise_codebook_library_data.cpp)
else()
    set(SR3AUDIORUNTIME_CODEBOOK_BLOB_SOURCE src/wwise_codebook_library_blob_stub.cpp)
endif()
add_library(sr3audioruntime STATIC
    src/wwise_backend.cpp
    src/global_rtpc.cpp
    src/audio_runtime.cpp
    src/wwise_internal.cpp
    src/real_vorbis_decode.cpp
    src/wwise_setup_rebuild.cpp
    src/wwise_codebook_library.cpp
    src/wwise_codebook_library_index_data.cpp
    ${SR3AUDIORUNTIME_CODEBOOK_BLOB_SOURCE}
    src/wav_writer.cpp
)
target_include_directories(sr3audioruntime PUBLIC include)
# real_vorbis_decode.cpp (the audio bring-up task's real libvorbis decode
# attempt, see include/sr3audioruntime/real_vorbis_decode.h) is the only
# translation unit in this library that needs the vendored libogg/libvorbis
# headers - linked PRIVATE since no PUBLIC header of this library includes
# either (real_vorbis_decode.h's own public surface is an opaque
# DecodeAttemptResult struct, no <ogg/*>/<vorbis/*> include), so no
# downstream consumer of sr3audioruntime needs to see libvorbis's own
# headers just to call attemptWwiseVorbisDecode().
target_link_libraries(sr3audioruntime PRIVATE vorbisstatic)

# 2026-10-10 (runtime data loader task): sr3binkcodec is no longer purely
# header-only - the 5 static lookup-table regions (kRunLength/kClassByte/
# kLutOffset/kLutBlob/kIntraQuant/kInterQuant, include/sr3binkcodec/
# entropy_prestage_runtime.h) are now mutable runtime storage defined once
# in src/bink_entropy_tables.cpp (filled from the compiled-in dev data when
# present, or left for sr3runtimedata's loader to fill from the player's
# own game install otherwise - see that header's own top comment), so every
# consumer of include/sr3binkcodec/entropy_prestage.h needs this small
# library linked in, not just "include" on its path. Defined here (not
# nearer its own Bink section further below) so sr3runtimedata right below
# - and sr3audioruntime_codebook_library_tests a little further down, which
# also needs it - can link against it.
add_library(sr3binkcodec STATIC src/bink_entropy_tables.cpp)
target_include_directories(sr3binkcodec PUBLIC include)

# --- sr3runtimedata: the runtime loader for the data regions be602fb made
# build-time-optional (Wwise codebook blob, 5 Bink static table regions),
# spec-runtime-data-sources.md, 2026-10-10 ---------------------------------
# CRREISH_GAME_INSTALL_DIR follows the SAME "default to the real install
# path on this machine, not empty" convention as CRREISH_GAME_CACHE_DIR
# (set further below, alongside the real_*_test family) - this is the
# directory that directly holds SaintsRowTheThird.exe/_DX11.exe/binkw32.dll
# (CRREISH_GAME_CACHE_DIR's own default is a packfiles/pc/cache
# subdirectory of this same install root).
set(CRREISH_GAME_INSTALL_DIR "D:/Project Crreish/Saints Row 3 CRREISH" CACHE PATH
    "Directory holding SaintsRowTheThird.exe/_DX11.exe/binkw32.dll, for sr3runtimedata's runtime loader (data stays absent, not a hard error, when this path or the files in it don't exist)")
add_library(sr3runtimedata STATIC src/runtime_data_loader.cpp)
target_include_directories(sr3runtimedata PUBLIC include)
target_compile_definitions(sr3runtimedata PRIVATE CRREISH_GAME_INSTALL_DIR="${CRREISH_GAME_INSTALL_DIR}")
target_link_libraries(sr3runtimedata PRIVATE sr3audioruntime sr3binkcodec)

add_executable(sr3audioruntime_tests tests/synthetic_audio_runtime_test.cpp)
target_link_libraries(sr3audioruntime_tests PRIVATE sr3audioruntime)
add_test(NAME synthetic_audio_runtime_test COMMAND sr3audioruntime_tests)

# Wwise-internal mechanics past the WwiseBackend boundary (U-W1 HIRC load
# dispatch, U-W2a event/action walk, U-W2b container/voice instantiation -
# spec-audio-runtime.md Sec7/Sec8/Sec9), src/wwise_internal.cpp above.
# Separate test binary (additive, does not touch synthetic_audio_runtime_test).
add_executable(sr3audioruntime_internal_tests tests/synthetic_wwise_internal_test.cpp)
target_link_libraries(sr3audioruntime_internal_tests PRIVATE sr3audioruntime)
add_test(NAME synthetic_wwise_internal_test COMMAND sr3audioruntime_internal_tests)

# Audio bring-up task (wwise_vorbis_setup.h/real_vorbis_decode.h/
# wav_writer.h) - synthetic, container-level-only tests (no real libvorbis
# setup-packet dependency, see the test file's own header comment).
add_executable(sr3audioruntime_vorbis_setup_tests tests/synthetic_wwise_vorbis_setup_test.cpp)
target_link_libraries(sr3audioruntime_vorbis_setup_tests PRIVATE sr3audioruntime)
add_test(NAME synthetic_wwise_vorbis_setup_test COMMAND sr3audioruntime_vorbis_setup_tests)
# real_vorbis_decode_tests (the real-data regression for this same decode
# attempt) is registered further below, alongside the other real_*_test
# targets, since it needs CRREISH_GAME_CACHE_DIR/CRREISH_VERIFY_BUILD, both
# defined later in this file - see that registration for the full story.

# Codebook-library cross-check (spec-audio-codebook-library.md,
# spec-audio-runtime.md Sec11.11.3a's MAJOR CORRECTION): synthetic - all
# data needed is the compiled-in library itself, no real game archive
# required. Verifies the compiled-in blob's SHA-256 first, then cross-checks
# this project's own bitstream parse of all 599 real codebook bodies
# against the spec's own pre-decoded header-field columns.
add_executable(sr3audioruntime_codebook_library_tests tests/wwise_codebook_library_crosscheck_test.cpp)
target_link_libraries(sr3audioruntime_codebook_library_tests PRIVATE sr3audioruntime sr3runtimedata)
add_test(NAME synthetic_wwise_codebook_library_crosscheck_test COMMAND sr3audioruntime_codebook_library_tests)
if(NOT CRREISH_VERIFY_BUILD)
    set_tests_properties(synthetic_wwise_codebook_library_crosscheck_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

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
        src/composite_renderer.cpp
        src/tod_lighting.cpp
        src/sun_shadow_map.cpp
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
    # sr3tables_environment (this task, 2026-10-08, sky/lighting/TOD): the new
    # runtime weather/TOD blend consumer (src/tod_lighting.cpp,
    # include/sr3render/tod_lighting.h) reads sr3tables_environment's
    # WeatherTimeSegment/WeatherTimeOfDayCell/ColorIntensity types directly -
    # previously sr3render had no dependency on the xtbl-reader group at all.
    target_link_libraries(sr3render PUBLIC sr3tables_environment)
    # Compiled-shader/PSO cache (this task, 2026-10-08): mesh_renderer.cpp/
    # quad_renderer.cpp's own fixed-source D3DCompile() calls go through
    # vppcache/shader_cache.h - see those files' own compileShader().
    target_link_libraries(sr3render PUBLIC vppcache)
    target_link_libraries(sr3render PRIVATE d3d11 dxgi d3dcompiler user32 gdi32)

    # Item 4 (optional, lowest priority) of the persistent-cache task
    # (2026-10-08): a minimal warm-"golden server" proof-of-concept - see
    # tools/golden_server_poc.cpp's own top comment for the full design and
    # the honest scope split (NOT wired into golden_scene_check.cpp).
    add_executable(golden_server_poc tools/golden_server_poc.cpp)
    target_link_libraries(golden_server_poc PRIVATE sr3render vppcache d3d11 dxgi d3dcompiler user32 gdi32)

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
    # sr3luahost/sr3vintdoc/sr3xtbl/sr3tables_environment added for the new
    # `vint-doc` command (2026-10-03, UI render unit U3 - see runVintDoc()'s
    # own doc comment): EngineState::loadVintDocument/vintQueryTransform for
    # the live element tree, sr3xtbl + ParseBitmapSheetsTable for the
    # bitmap-sheet tables (spec-tables-environment.md Sec11.7/Sec17.4).
    # sr3font added for `vint-doc`'s real text drawing (2026-10-03, UI render
    # unit U5): .vf3_pc fonts, layout and glyph geometry (defined below with
    # its tests; CMake resolves the name at generate time). sr3locstr added
    # for the same command's `text_tag` localisation-key lookup (2026-10-03,
    # spec-localization-format.md).
    # sr3playeranim/sr3tables_animation added for the new `walk` command
    # (2026-10-07, the deterministic "character walks" viewer demo - see
    # runWalkDemo()'s own doc comment in tools/sr3_viewer.cpp): brad driven
    # by sr3playeranim::PlayerAnimBridge's real player<->animation coupling
    # (the real "player state machine" node graph, loaded through
    # sr3animruntime) on the real city_corner ground, with the real follow
    # camera's target kept in sync every frame. sr3tables_animation is
    # needed directly (not just transitively through sr3playeranim) for
    # ParseAnimStatesTable/ParseAnimActionsTable/ParseAnimTriggersTable,
    # which build the StateActionRegistry/TriggerRegistry the node-graph
    # loader needs from anim_states.xtbl/anim_actions.xtbl/
    # anim_triggers.xtbl - same "keep the target graph in step with a
    # hand-built binary" lesson as every other addition noted above. (This
    # link line was added one task earlier than the command that uses it,
    # 2026-10-07 same day - see this file's own prior history for that one
    # sub-task's own disk-space-incident note, now resolved.)
    # sr3physics added for the new `drive_demo` command (2026-10-08, the vehicle
    # twin of `walk`/walk_demo): sr3physics::VehicleSubstepSimulator drives a
    # real vehicle mesh through a scripted straight/turn/brake sequence - see
    # runDriveDemo()'s own doc comment in tools/sr3_viewer.cpp. sr3physics
    # already PUBLIC-links sr3vehicleinfo (see this file's own comment just
    # above that target_link_libraries(sr3physics ...) line), so
    # sr3vehicleinfo::VehicleEntry/ParseVehicleEntry reach this executable
    # transitively - no separate sr3vehicleinfo link line is added here.
    # sr3airuntime added for the new `traffic_lane_demo` command (2026-10-08,
    # traffic-AI lane-following task): sr3airuntime::TrafficLaneGraph/
    # StepLaneFollow (include/sr3airuntime/traffic_lane_follower.h) walks
    # the real sr3zone::TrafficRecord graph (sr3zone already linked below)
    # and feeds the real sr3physics::VehicleSubstepSimulator (already
    # linked above) - see runTrafficLaneDemo()'s own doc comment in this
    # file (tools/sr3_viewer.cpp).
    # sr3tables_vehicle_world (vehicle-occupant-hide-flag task, 2026-10-09):
    # the real vehicle_interaction_info.xtbl reader (`Hide Occupant`, spec-
    # render-pipeline.md Sec23.14) runPlaytest()'s "VEHICLE OCCUPANT
    # VISIBILITY" block now consumes - not previously reachable from this
    # target (sr3physics only PUBLIC-links the separate sr3vehicleinfo lib).
    target_link_libraries(sr3_viewer PRIVATE sr3render sr3geometry sr3mesh sr3rig sr3anim sr3vehicle sr3physics sr3fxo sr3d3d9bc sr3zone sr3airuntime sr3clmesh sr3luahost sr3vintdoc sr3xtbl sr3tables_environment sr3tables_vehicle_world sr3font sr3locstr sr3playeranim sr3animpose sr3tables_animation vppcache d3d11 dxgi d3dcompiler user32 gdi32)

    # --- Fold tests/golden/build.bat's remaining standalone tools into real
    # CMake targets (B1 task, 2026-10-07). tests/golden/ (including
    # build.bat itself) is gitignored ("Large real-data test fixtures and
    # golden-scene reference images... regenerated locally" - see
    # .gitignore's own comment) and therefore absent from this fresh
    # worktree checkout entirely - it was never read directly here. This
    # list of 7 tool names and what each #includes (hence what library each
    # one needs) was instead cross-checked straight from these tools' own
    # already-committed sources under tools/ (which ARE tracked) against
    # tools/golden_scene_check.cpp's own extensive doc comments (also
    # tracked), which already name exactly these 7 executables as the ones
    # still built ONLY by build.bat (sr3_viewer above and golden_scene_check
    # itself, the validation-tools glob below, were already folded into
    # CMakeLists.txt by earlier sessions - see sr3_viewer's own "keep the
    # target graph in step with a hand-built binary" comments above). Once
    # these targets exist, a future new #include in any of these 7 files
    # becomes a normal CMake dependency instead of a build.bat edit nobody
    # remembered to make - the whole point of this task.
    add_executable(prototype_lit_car tools/prototype_lit_car.cpp)
    target_link_libraries(prototype_lit_car PRIVATE sr3render sr3d3d9bc sr3fxo sr3geometry sr3mesh sr3tables_environment sr3texture sr3vehicle sr3xtbl vpp_container d3d11 dxgi d3dcompiler user32 gdi32)

    add_executable(prototype_lit_clmesh_tower tools/prototype_lit_clmesh_tower.cpp)
    target_link_libraries(prototype_lit_clmesh_tower PRIVATE sr3render sr3clmesh sr3d3d9bc sr3fxo sr3geometry sr3mesh sr3tables_environment sr3texture sr3vehicle sr3xtbl vpp_container d3d11 dxgi d3dcompiler user32 gdi32)

    # prototype_lit_clmesh_tower2 (NOT a modification of tower above - its
    # own top comment calls it "a NEW fork... tower.cpp is itself untouched"
    # - same "fork, don't modify" convention as prototype_real_shader_draw's
    # own multishader/multishader_paintcompose forks below): the tool
    # tools/golden_scene_check.cpp's checkClmeshAirportControlTowerLitScene()
    # actually invokes for the clmesh_airport_controltower_lit golden.
    add_executable(prototype_lit_clmesh_tower2 tools/prototype_lit_clmesh_tower2.cpp)
    target_link_libraries(prototype_lit_clmesh_tower2 PRIVATE sr3render sr3clmesh sr3d3d9bc sr3fxo sr3geometry sr3mesh sr3tables_environment sr3texture sr3xtbl vpp_container d3d11 dxgi d3dcompiler user32 gdi32)

    add_executable(prototype_real_shader_draw tools/prototype_real_shader_draw.cpp)
    target_link_libraries(prototype_real_shader_draw PRIVATE sr3render sr3d3d9bc sr3fxo sr3geometry sr3mesh sr3texture sr3vehicle vpp_container d3d11 dxgi d3dcompiler user32 gdi32)

    add_executable(prototype_real_shader_draw_multishader tools/prototype_real_shader_draw_multishader.cpp)
    target_link_libraries(prototype_real_shader_draw_multishader PRIVATE sr3render sr3d3d9bc sr3fxo sr3geometry sr3mesh sr3texture sr3vehicle vpp_container d3d11 dxgi d3dcompiler user32 gdi32)

    add_executable(prototype_real_shader_draw_multishader_paintcompose tools/prototype_real_shader_draw_multishader_paintcompose.cpp)
    target_link_libraries(prototype_real_shader_draw_multishader_paintcompose PRIVATE sr3render sr3d3d9bc sr3fxo sr3geometry sr3mesh sr3texture sr3vehicle vpp_container d3d11 dxgi d3dcompiler user32 gdi32)

    # tree_baseline_render: the tool tools/golden_scene_check.cpp's
    # checkTreeScene() invokes (the 5th golden scene) - previously built
    # only by build.bat, same "keep the target graph in step" reasoning.
    add_executable(tree_baseline_render tools/tree_baseline_render.cpp)
    target_link_libraries(tree_baseline_render PRIVATE sr3render sr3mesh sr3tree vpp_container d3d11 dxgi d3dcompiler user32 gdi32)

    # Ambient (distant/LOD) pedestrian+vehicle spawn-despawn demo + schematic
    # top-down capture (spec-ai-runtime.md Sec17, this task 2026-10-08) - the
    # tool tools/golden_scene_check.cpp's checkAmbientPopulationDemoScene()
    # invokes for the ambient_population_demo golden scene. sr3airuntime for
    # the real spawn/despawn/driver module (header-only,
    # include/sr3airuntime/ambient_population.h - no new sr3airuntime source
    # file needed). sr3render for writePng only (this tool creates no D3D11
    # device), but sr3render's own d3d11/dxgi/d3dcompiler link requirement
    # still reaches the final executable as a STATIC library's link
    # libraries (same note sr3_viewer's own link line above already
    # explains).
    add_executable(ambient_population_demo tools/ambient_population_demo.cpp)
    target_link_libraries(ambient_population_demo PRIVATE sr3airuntime sr3render d3d11 dxgi d3dcompiler user32 gdi32)

    # Near-field (fully-simulated) ambient-vehicle maintenance demo
    # (spec-ai-runtime.md Sec17.4c, this task 2026-10-08) - sibling to
    # ambient_population_demo above, proving the near-field tier's own
    # 48-cap/150-human-ceiling/despawn-eligibility behaviour end to end,
    # including that its own cap and the distant tier's own 75-cap are
    # independent (both driven in the same run, two separate pools). NOT
    # wired into tools/golden_scene_check.cpp (see this tool's own top
    # comment for why - no frozen baseline exists for a brand-new tool in a
    # gitignored, "regenerated locally" tests/golden/ directory); its own
    # stdout narrative is the proof, same companion relationship to
    # tests/synthetic_near_field_vehicle_population_test.cpp as
    # ambient_population_demo has to its own synthetic test file. sr3airuntime
    # only (header-only, near_field_vehicle_population.h + ambient_population.h -
    # no new sr3airuntime source file needed); no sr3render/D3D11 need here
    # (no PNG capture - stdout-only proof).
    add_executable(near_field_vehicle_demo tools/near_field_vehicle_demo.cpp)
    target_link_libraries(near_field_vehicle_demo PRIVATE sr3airuntime)

    # Character skin composite-bake renderer (this task, 2026-10-08) - see
    # include/sr3render/composite_renderer.h's own top comment for the full
    # spec citation (spec-tables-customization.md Sec12.1e/Sec12.1f).
    # sr3tables_customization for ParseCustomizationCompositingTable (the
    # real Composite_Layer reader); sr3texture/vpp_container for the by-name
    # PEG/texture-registry lookup tools/skin_composite_render.cpp copies
    # verbatim from tools/prototype_lit_clmesh_tower2.cpp (see that file's
    # own top comment).
    add_executable(skin_composite_render tools/skin_composite_render.cpp)
    target_link_libraries(skin_composite_render PRIVATE sr3render sr3tables_customization sr3xtbl sr3texture vpp_container d3d11 dxgi d3dcompiler user32 gdi32)

    # Unit tests for sr3render::CompositeSheetRenderer/
    # BuildCharacterSkinComposite (tests/composite_renderer_test.cpp) - the
    # first sr3render ctest unit test to create a real D3D11 device itself
    # (every other sr3render check so far is a golden-scene tool + checked-
    # in baseline, tools/golden_scene_check.cpp); sr3render::RenderDevice's
    # own WARP fallback (device.h) means this still runs headless with no
    # physical GPU present.
    add_executable(sr3render_composite_tests tests/composite_renderer_test.cpp)
    target_link_libraries(sr3render_composite_tests PRIVATE sr3render d3d11 dxgi d3dcompiler user32 gdi32)
    add_test(NAME composite_renderer_test COMMAND sr3render_composite_tests)

    # Unit tests for sr3render::{ComputeTodSegmentBlend,BlendTodCorners,
    # ComputeTodLighting,...} (tests/synthetic_tod_lighting_test.cpp, this
    # task, 2026-10-08, sky/lighting/TOD) - spec-tables-environment.md
    # Sec18's weather/TOD runtime blend, built on top of
    # sr3tables_environment's real table readers via tiny synthetic XML
    # fixtures (same "synthetic" convention as sr3tables_environment_tests
    # above - hand-rolled fixtures, not real game data). Pure math/logic,
    # no D3D11 device created - WIN32-gated only because it links sr3render
    # (whose own PRIVATE d3d11/dxgi/d3dcompiler/user32/gdi32 link libraries
    # still reach this executable's link line regardless of PRIVATE/PUBLIC,
    # same note as skin_composite_render's own comment above).
    add_executable(sr3render_tod_lighting_tests tests/synthetic_tod_lighting_test.cpp)
    target_link_libraries(sr3render_tod_lighting_tests PRIVATE sr3render d3d11 dxgi d3dcompiler user32 gdi32)
    add_test(NAME synthetic_tod_lighting_test COMMAND sr3render_tod_lighting_tests)

    # Unit tests for sr3render::{kTodSecondsPerDay, kTodHoursPerTKeyPress,
    # AdvanceTodFraction, JumpTodFractionHours, TodFractionToHourMinute}
    # (include/sr3render/tod_clock.h, tests/synthetic_tod_clock_test.cpp,
    # v0.3 polish task, 2026-10-09: "slow the TOD cycle + add a time-skip
    # key") - runPlaytest()'s own per-frame TOD-advance and `T`-key time-
    # skip formulas, pulled out of that window/D3D-coupled function so they
    # have real test coverage. Pure double arithmetic, no D3D11 device -
    # WIN32-gated only because it links sr3render (same note as
    # sr3render_tod_lighting_tests just above).
    add_executable(sr3render_tod_clock_tests tests/synthetic_tod_clock_test.cpp)
    target_link_libraries(sr3render_tod_clock_tests PRIVATE sr3render d3d11 dxgi d3dcompiler user32 gdi32)
    add_test(NAME synthetic_tod_clock_test COMMAND sr3render_tod_clock_tests)

    # Output-path guard (include/sr3io/output_path_guard.h, called from
    # src/png_writer.cpp's writePng and src/wav_writer.cpp's writeWavPcm16):
    # regression for the 2026-10-08 incident in which a capture tool's raw
    # argv[1] output path overwrote the live sr3_city_0.vpp_pc archive with a
    # 40584-byte PNG. Fully synthetic (temp-dir sandbox; the real install
    # roots are exercised by path string only); WIN32-gated only because
    # sr3render is.
    add_executable(sr3io_output_path_guard_tests tests/synthetic_output_path_guard_test.cpp)
    target_link_libraries(sr3io_output_path_guard_tests PRIVATE sr3render sr3audioruntime d3d11 dxgi d3dcompiler user32 gdi32)
    add_test(NAME synthetic_output_path_guard_test COMMAND sr3io_output_path_guard_tests)
    set_tests_properties(synthetic_output_path_guard_test PROPERTIES LABELS "fast")
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
# layer, and src/lua_spec_confirmed_stubs_b0*.cpp
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
    src/vint_property_types.cpp
    src/lua_spec_initial_state.cpp
    # lua_spec_confirmed_stubs.cpp was split 2026-10-07 (structural refactor,
    # zero behaviour change - see src/lua_spec_confirmed_stubs_registry.cpp's
    # own top comment) into a shared header pair plus 7 per-batch files and
    # a small coordinator, so parallel agents adding new natives each get
    # their own file instead of all contending on one shared translation
    # unit's name array/registration function/stub bodies.
    src/lua_spec_confirmed_stubs_b01_initial.cpp
    src/lua_spec_confirmed_stubs_b02_sec27_28.cpp
    src/lua_spec_confirmed_stubs_b03_vintui_r03_r05.cpp
    src/lua_spec_confirmed_stubs_b04_r04_r06_08.cpp
    src/lua_spec_confirmed_stubs_b05_invuln_r15_23.cpp
    src/lua_spec_confirmed_stubs_b06_playerrt_sec6_7.cpp
    src/lua_spec_confirmed_stubs_b07_sec8_9_teamA.cpp
    # b08: wave 1 batch 1 (2026-10-08), spec-lua-api-behaviour.md §1-4
    # (trigger/mission/teleport/combat-hook core) - a NEW file per the
    # refactor's own reviewer guidance (parallel agents each own one b0N
    # file rather than retrofitting into an existing one). See that file's
    # own top comment for the 15/21 names actually implemented (6 deferred,
    # each still carrying its own un-superseded "NOT yet cleared for
    # implementation" Review-status line).
    src/lua_spec_confirmed_stubs_b08_batch1_core.cpp
    # b09: wave 1, Batch 2 of the 2026-10-08 7-agent parallel fan-out
    # (results/lua_natives_sec10_28_fanout_scoping_20261008.md) - see that
    # file's own top comment for the full citation list.
    src/lua_spec_confirmed_stubs_b09_batch2_hud_audio.cpp
    # b14 (2026-10-08, wave 1 Batch 7 of a 7-agent parallel fan-out): a new
    # file per the refactor's own reviewer guidance (never retrofitted into
    # b01-b07) - spec-lua-api-behaviour.md Sec30 ("ranking tranche 03"),
    # the 16 of its 25 names not already wired by b03.
    src/lua_spec_confirmed_stubs_b14_batch7_tranche03.cpp
    src/lua_spec_confirmed_stubs_b12_batch5_hud_customization.cpp
    # b11: wave 1 batch 4 of the Sec10-28 fan-out (2026-10-08) - Sec13 (28
    # names) + a 27-name subset of Sec14 ("flag-setters/hooks/combat
    # cluster"), 55 names total. New file per that refactor's own reviewer
    # guidance (do not retrofit into b01-b07).
    src/lua_spec_confirmed_stubs_b11_batch4_flags_combat.cpp
    # b10: wave 1, Batch 3 of a 7-agent parallel fan-out (results/
    # lua_natives_sec10_28_fanout_scoping_20261008.md), spec-lua-api-
    # behaviour.md Sec11+Sec12 (40 names) - a NEW file per that scoping
    # report's own guidance, not a retrofit into b01-b07.
    src/lua_spec_confirmed_stubs_b10_batch3_pcr_vehicle.cpp
    # b15: wave 2 batch W2-1 (2026-10-08) - the 13 "core dispatcher"
    # natives (vehicle enter/exit family, move_to_do, vehicle_pathfind_
    # navmesh_do, teleport_to_object, character_damage, qte_start). New file
    # per the same reviewer guidance (never retrofitted into b01-b14).
    src/lua_spec_confirmed_stubs_b15_wave2_core_dispatchers.cpp
    # b16: wave 2, Batch W2-3 (2026-10-08) - spec-lua-api-behaviour.md
    # Sec20+Sec21 ("ranks 351-450 tranche" parts A/C gameplay residue), 57
    # names. A NEW file, not a retrofit into any existing bNN.
    src/lua_spec_confirmed_stubs_b16_wave2_sec20_21.cpp
    # b22/b23: wave 2 batch W2-4 (2026-10-08) - spec-lua-api-behaviour.md
    # Sec22+Sec24+Sec25 gameplay residue (78 names), split into two files per
    # the orchestrator's own dispatch (Sec22 alone in b22, Sec24+Sec25 in
    # b23) - new files per the established fan-out convention, not a
    # retrofit into b01-b07/b10-b14.
    src/lua_spec_confirmed_stubs_b22_wave2_sec22.cpp
    src/lua_spec_confirmed_stubs_b23_wave2_sec24_25.cpp
    # b26/b27: wave 2, Batch W2-7 (2026-10-08) - spec-lua-api-behaviour.md
    # Sec38-60, "gameplay A: vehicles/world/spawn/effects/audio/camera"
    # (113 names, split vehicles/helicopter/airplane/traffic | spawn/world/
    # effects/audio/camera/mission-specific) - new files, not a retrofit.
    src/lua_spec_confirmed_stubs_b26_wave2_vehicles_helicopter.cpp
    src/lua_spec_confirmed_stubs_b27_wave2_spawn_world_audio.cpp
    # b24/b25: wave 2, Batch W2-5 (2026-10-08) - "Customization frontends"
    # (PCU/PCR/customization_* + vcust/store/garage/crib/gang), 123 names,
    # split 2 files per that batch's own suggested split. HARDENING batch -
    # see each file's own top comment for the 28 crash-shaped findings and
    # the 4 NAMED memory-safety fixes.
    src/lua_spec_confirmed_stubs_b24_wave2_pcu_pcr.cpp
    src/lua_spec_confirmed_stubs_b25_wave2_vcust_store_garage.cpp
    # b17/b18 (2026-10-08, wave 2 batch W2-6, "UI frontend (143 natives) -
    # HARDENING BATCH") - 2 new files, split per that batch's own report
    # (b17: Sec19/Sec23/Sec26 + lobby/coop/session/steam `game_*`, 66
    # names; b18: menus/pause/dialog/cellphone/cell/save/machinima/
    # completion, 77 names, including this batch's 2 own named memory-
    # safety fixes, dialog_box_create_internal and cell_playlist_save_
    # list). See each file's own top comment for the full citation list.
    src/lua_spec_confirmed_stubs_b17_wave2_ui_frontend_a.cpp
    src/lua_spec_confirmed_stubs_b18_wave2_ui_frontend_b.cpp
    # b19/b20 (2026-10-08, wave 2 batch W2-8) - spec-lua-api-behaviour.md
    # §38-60 "gameplay B" (93 names), split character/NPC/inventory/party
    # (b19, 56 names) vs. HUD/minimap/mission-flow/hooks (b20, 37 names) -
    # see each file's own top comment for the full citation list.
    src/lua_spec_confirmed_stubs_b19_wave2_char_npc_inv.cpp
    src/lua_spec_confirmed_stubs_b20_wave2_hud_minimap_hooks.cpp
    # b21: wave 2 batch W2-2 (2026-10-08) - spec-lua-api-behaviour.md Sec17
    # (30 names) + Sec18 (30 names), 60 names total, the highest real call
    # volume in the entire 680-name backlog. New file, not a retrofit.
    src/lua_spec_confirmed_stubs_b21_wave2_sec17_18.cpp
    # b28: the 6-native clearance fast-follow onto wave 1 batch 1
    # (2026-10-08) - on_dismiss/character_prevent_flinching/character_
    # allow_ragdoll/npc_combat_enable/inv_weapon_add_temporary/inv_weapon_
    # remove_temporary. New file, not a retrofit into b08.
    src/lua_spec_confirmed_stubs_b28_batch1_deferred6.cpp
    # b29: Sec6 backlog close-out (2026-10-08) - the 10 of Sec6's 21 names
    # deliberately deferred by the earlier Sec6 batch (engine_state.h's own
    # "deliberately NOT in this batch" note) - disable_specific_notoriety_
    # group/notoriety_set_min_and_max/notoriety_set_max/notoriety_set/
    # notoriety_force_no_spawn_group/notoriety_set_min/mission_end_success/
    # notoriety_reset/mission_end_to_activity/mission_set_cancel_warp_
    # location. New file, not a retrofit.
    src/lua_spec_confirmed_stubs_b29_sec6_notoriety_mission.cpp
    # b30: cross-reference backlog close-out (2026-10-08) - character_kill
    # (Sec3.15) and vehicle_is_destroyed (Sec4.9), both newly cleared by the
    # 2026-10-08 tranche-F spec sync, plus camera_restrict_set_active
    # (Sec8.12), already CONFIRMED but missed by every earlier batch. New
    # file, not a retrofit.
    src/lua_spec_confirmed_stubs_b30_tranchef_camera.cpp
    # b31: 3 natives cleared for implementation (2026-10-08) -
    # continuous_spawn_start (Sec4.8), on_vehicle_enter (Sec4.12, re-
    # resolved by commit e07a46a), tutorial_start (Sec6.19, 210-entry name
    # table transcribed by commit 3816870). New file, not a retrofit.
    src/lua_spec_confirmed_stubs_b31_continuous_spawn_vehicle_enter_tutorial.cpp
    src/lua_spec_confirmed_stubs_registry.cpp
    src/lua_screen_fade.cpp
    src/lua_cutscene.cpp
    src/lua_vint_resolution.cpp
    src/lua_tutorial_names.cpp
    src/lua_bare_globals.cpp
    # zone-data fast-follow (2026-10-08): the real name<->sr3zone::
    # ObjectRecord resolver (include/sr3luahost/zone_object_resolver.h),
    # wired into EngineState::loadZoneObjectsSection() below.
    src/lua_zone_object_resolver.cpp
    # spec-save-format.md Sec15 wiring (2026-10-08): mission_autosave/
    # game_autosave/mission_set_checkpoint's real writer/reader bridge
    # (include/sr3luahost/save_game_bridge.h), on top of the sr3save
    # library's SaveSnapshot reader/writer.
    src/lua_save_game_bridge.cpp
    # spec-frontend-menu-runtime.md wiring (2026-10-08): the 11-slot game-
    # mode table, the real new-game/horde-start/continue/zone-jump flows,
    # the shared input-lock dispatcher and fade-completion teardown, and
    # the mission-start marker/phase machine (include/sr3luahost/
    # menu_runtime.h) - on top of save_game_bridge.h's own real load chain.
    src/lua_menu_runtime.cpp
)
target_include_directories(sr3luahost PUBLIC include)
# sr3vintdoc (2026-10-03): EngineState::loadVintDocument builds the live
# Vint element tree from a parsed .vint_doc (sr3vintdoc::parseDocument).
# sr3zone (2026-10-08): EngineState::loadZoneObjectsSection()/
# resolveZoneObject() consume sr3zone::ObjectsSection/ObjectRecord directly
# (include/sr3luahost/zone_object_resolver.h) - the real `.czn_pc` Objects
# reader, not reimplemented here.
# sr3save (2026-10-08): save_game_bridge.h's real SaveSnapshot writer/
# reader (spec-save-format.md Sec15 wiring).
# sr3cutscene (2026-10-08): cutscene_play_do's real camera-script pipeline -
# EngineState::cutsceneCameraScriptStep()/sampleCutsceneCamera() parse and
# sample a real .csc_pc via the EXISTING sr3cutscene::camera_script reader
# (spec-lua-api-behaviour.md Sec57.2a / spec-cutscene-camera-format.md
# Sec10.6 item 7).
target_link_libraries(sr3luahost PUBLIC lua51 sr3vintdoc sr3player sr3zone sr3save sr3cutscene)
if(WIN32)
    target_link_libraries(sr3luahost PUBLIC user32)
endif()

add_executable(sr3luahost_tests tests/synthetic_luahost_test.cpp)
target_link_libraries(sr3luahost_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_test COMMAND sr3luahost_tests)

# Real name<->sr3zone::ObjectRecord resolver (2026-10-08) -
# EngineState::loadZoneObjectsSection()/resolveZoneObject(), the real
# `.czn_pc` Objects reader wired into this host's own named-object lookup
# (zone_object_resolver.h's own top comment). Synthetic ObjectRecord/
# ObjectsSection fixtures only - builds its own in-memory records directly
# (every ObjectRecord field is public/default-constructible), no raw byte
# buffer needed.
add_executable(sr3luahost_zone_object_resolver_tests tests/synthetic_zone_object_resolver_test.cpp)
target_link_libraries(sr3luahost_zone_object_resolver_tests PRIVATE sr3luahost)
add_test(NAME synthetic_zone_object_resolver_test COMMAND sr3luahost_zone_object_resolver_tests)

# Minimap live-transform (2026-10-08, minimap live-transform task) -
# include/sr3luahost/minimap_live_transform.h's pure, GPU-free corner-
# minimap background transform (spec-hud-runtime.md Sec6.4), plus sr2_map's
# own zoom/map_mode properties through the existing typed VINT property
# store (vint_property_types.cpp/.h, corrected this same task).
add_executable(sr3luahost_minimap_live_transform_tests tests/synthetic_minimap_live_transform_test.cpp)
target_link_libraries(sr3luahost_minimap_live_transform_tests PRIVATE sr3luahost)
add_test(NAME synthetic_minimap_live_transform_test COMMAND sr3luahost_minimap_live_transform_tests)

# Mission-host Phase 2 (2026-10-09, branch feature/mission-host-phase2-
# live-bridge) - EngineState's new live position bridge + name->live-object
# resolver (liveObjectKind()/isLiveObject()/bridgeLiveCharacterPosition()/
# bridgeLiveVehiclePosition()/clearLiveObjectsForTesting(), engine_state.h,
# right after getOrCreateVehicle()/vehicles()) - the mechanism
# tools/sr3_viewer.cpp's own runPlaytest() now calls once per rendered
# frame for "#PLAYER1#" (see that function's own "MISSION-HOST PHASE 2" doc
# comment). Covers the resolver's own independence from objectResolves()
# and an end-to-end Lua pass through the REAL, unmodified get_dist native.
add_executable(sr3luahost_mission_host_live_bridge_tests tests/synthetic_mission_host_live_bridge_test.cpp)
target_link_libraries(sr3luahost_mission_host_live_bridge_tests PRIVATE sr3luahost)
add_test(NAME synthetic_mission_host_live_bridge_test COMMAND sr3luahost_mission_host_live_bridge_tests)

# Roster test on the real tools/lua_all_registered_1490_tagged.txt (names
# only): vint_* UI-only (Sec13.7), 8 dual registrations (Sec13.5), the 24
# bare globals still unregistered pending their roster (Sec13.2/Sec16.3).
# The 24 bare globals of 0x00e0f900 (include/sr3luahost/bare_globals.h,
# spec-lua-api-behaviour.md Sec26.27, spec-lua-bindings.md Sec13.2/Sec16.4).
add_executable(sr3luahost_bare_globals_tests tests/synthetic_luahost_bare_globals_test.cpp)
target_link_libraries(sr3luahost_bare_globals_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_bare_globals_test COMMAND sr3luahost_bare_globals_tests)

# In-world trigger-volume overlap test + callback dispatch (2026-10-10,
# spec-lua-api-behaviour.md Sec1.14): EngineState::TriggerVolumeContainsPoint
# plus the enter/exit-transition -> BareThreadTable::allocate() dispatch
# pattern tools/sr3_viewer.cpp's own runPlaytest() now uses.
add_executable(sr3luahost_trigger_volume_overlap_tests tests/synthetic_trigger_volume_overlap_test.cpp)
target_link_libraries(sr3luahost_trigger_volume_overlap_tests PRIVATE sr3luahost)
add_test(NAME synthetic_trigger_volume_overlap_test COMMAND sr3luahost_trigger_volume_overlap_tests)

add_executable(sr3luahost_roster_tests tests/synthetic_luahost_roster_test.cpp)
target_link_libraries(sr3luahost_roster_tests PRIVATE sr3luahost)
target_compile_definitions(sr3luahost_roster_tests PRIVATE CRREISH_TOOLS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tools")
add_test(NAME synthetic_luahost_roster_test COMMAND sr3luahost_roster_tests)

# Lua-natives registration unification task (2026-10-08, results/
# lua_natives_backlog_recount_20261008.md): end-to-end check that
# specConfirmedStubNames() - the single source of truth for every name
# this project's Host actually registers through a real, non-generic-stub
# implementation (the b01-b07/bare-globals/thread_close union) - matches
# what a real Host's real Lua global tables actually hold, by real
# introspection rather than by comparing two hand-written C++ lists against
# each other. See the test's own top comment for the exact method.
add_executable(sr3luahost_unified_registry_tests tests/synthetic_luahost_unified_registry_test.cpp)
target_link_libraries(sr3luahost_unified_registry_tests PRIVATE sr3luahost)
target_compile_definitions(sr3luahost_unified_registry_tests PRIVATE CRREISH_TOOLS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tools")
add_test(NAME synthetic_luahost_unified_registry_test COMMAND sr3luahost_unified_registry_tests)

# Refusal stress (cloud phase 2026-09-30, after bridge job jklk's MSVC heap
# corruption): every spec stub refusing many times, in both states. Mainly an
# MSVC regression check - see the test's top comment.
add_executable(sr3luahost_refusal_stress_tests tests/synthetic_luahost_refusal_stress_test.cpp)
target_link_libraries(sr3luahost_refusal_stress_tests PRIVATE sr3luahost)
target_compile_definitions(sr3luahost_refusal_stress_tests PRIVATE CRREISH_TOOLS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tools")
add_test(NAME synthetic_luahost_refusal_stress_test COMMAND sr3luahost_refusal_stress_tests)

# zscene lifecycle driver, cutscene machine and UI resolution state
# (spec-lua-api-behaviour.md Sec26.25 / Sec26.26, 2026-10-01 nnlt text).
add_executable(sr3luahost_cutscene_tests tests/synthetic_luahost_cutscene_test.cpp)
target_link_libraries(sr3luahost_cutscene_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_cutscene_test COMMAND sr3luahost_cutscene_tests)

# cutscene_play_do's real camera-script pipeline (spec-lua-api-behaviour.md
# Sec57.2a / spec-cutscene-camera-format.md Sec10.6 item 7, 2026-10-08
# wiring task): the kind gate (Designer skips; Zscene/Story load) and the
# real name -> .csc_pc bytes -> sr3cutscene::CameraScript -> sampled pose
# pipeline, end to end.
add_executable(sr3luahost_cutscene_camera_script_tests tests/synthetic_luahost_cutscene_camera_script_test.cpp)
target_link_libraries(sr3luahost_cutscene_camera_script_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_cutscene_camera_script_test COMMAND sr3luahost_cutscene_camera_script_tests)

# Batch 2026-10-01: spec-lua-api-behaviour.md Sec27 (25 UI-cluster names)
# and Sec28 (25 gameplay-cluster names), "ranks 551-650" tranche parts D/C
# - see src/lua_spec_confirmed_stubs_b0*.cpp and this test's own top comment.
add_executable(sr3luahost_batch2728_tests tests/synthetic_luahost_batch2728_test.cpp)
target_link_libraries(sr3luahost_batch2728_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_batch2728_test COMMAND sr3luahost_batch2728_tests)

# Batch 2026-10-02: spec-lua-api-behaviour.md Sec30 ("ranking tranche 03"),
# 9 of its 25 CONFIRMED names - see src/lua_spec_confirmed_stubs_b0*.cpp's own
# Sec30 batch header comment and this test's own top comment.
add_executable(sr3luahost_tranche03_tests tests/synthetic_luahost_tranche03_test.cpp)
target_link_libraries(sr3luahost_tranche03_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_tranche03_test COMMAND sr3luahost_tranche03_tests)

# Batch 2026-10-08 (wave 1 Batch 7 of a 7-agent parallel fan-out): spec-lua-
# api-behaviour.md Sec30 ("ranking tranche 03"), the 16 of its 25 names NOT
# already wired by the tranche03 test above - see
# src/lua_spec_confirmed_stubs_b14_batch7_tranche03.cpp's own top comment
# and this test's own top comment.
add_executable(sr3luahost_tranche03_batch7_tests tests/synthetic_luahost_tranche03_batch7_test.cpp)
target_link_libraries(sr3luahost_tranche03_batch7_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_tranche03_batch7_test COMMAND sr3luahost_tranche03_batch7_tests)

# Wave 2 batch W2-1 (2026-10-08): the 13 "core dispatcher" natives of
# src/lua_spec_confirmed_stubs_b15_wave2_core_dispatchers.cpp.
add_executable(sr3luahost_wave2_w21_tests tests/synthetic_luahost_wave2_w21_test.cpp)
target_link_libraries(sr3luahost_wave2_w21_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_wave2_w21_test COMMAND sr3luahost_wave2_w21_tests)

# Follower-sentinel-resolver pass (2026-10-08): the "#FOLLOWER#"/
# "#FOLLOWER1#"/"#FOLLOWER2#"/"#FOLLOWER3#" name-resolution sentinel
# family (spec-lua-api-behaviour.md Sec3 front matter) and the shared
# resolveFollowerAwareObject()/isFollowerSentinelLiteral() helper
# (lua_spec_confirmed_stubs_common.h) wired into ~2 dozen existing
# natives across many stub batches - see this test's own top comment.
add_executable(sr3luahost_follower_sentinel_tests tests/synthetic_luahost_follower_sentinel_test.cpp)
target_link_libraries(sr3luahost_follower_sentinel_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_follower_sentinel_test COMMAND sr3luahost_follower_sentinel_tests)

# Batch 2026-10-02: spec-lua-api-behaviour.md Sec31 (pause_map_stag_current_
# district_control + 4 confirmed pause-map siblings, 5 `ui` names) and Sec32
# (ranking tranche 04, 25 `gameplay` names), plus the 0x009df3d0 remote-co-op-
# player correction (Sec14.31/Sec32.8) - see src/lua_spec_confirmed_stubs_b0*.cpp
# and this test's own top comment.
add_executable(sr3luahost_batch31_32_tests tests/synthetic_luahost_batch31_32_test.cpp)
target_link_libraries(sr3luahost_batch31_32_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_batch31_32_test COMMAND sr3luahost_batch31_32_tests)

# Batch 2026-10-02: spec-lua-api-behaviour.md Sec38/Sec39/Sec40 ("ranking
# tranches 06/07/08"), 16 of their combined 75 CONFIRMED names - see
# src/lua_spec_confirmed_stubs_b0*.cpp's own Sec38/Sec39/Sec40 batch header
# comments and this test's own top comment.
add_executable(sr3luahost_tranche060708_tests tests/synthetic_luahost_tranche060708_test.cpp)
target_link_libraries(sr3luahost_tranche060708_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_tranche060708_test COMMAND sr3luahost_tranche060708_tests)

# Batch 2026-10-02 (resumed session): the Sec3.7/Sec20.1 vehicle-
# invulnerability bit-conflict resolution (Sec46.1) plus a curated subset of
# ranking tranches 12-14 (Sec44/Sec45/Sec46) - see src/lua_spec_confirmed_
# stubs.cpp's own batch header comment (right before stub_turn_invulnerable)
# for the full selection reasoning.
add_executable(sr3luahost_sec44_46_tests tests/synthetic_luahost_sec44_46_test.cpp)
target_link_libraries(sr3luahost_sec44_46_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_sec44_46_test COMMAND sr3luahost_sec44_46_tests)

# Batch 2026-10-03: spec-lua-api-behaviour.md Sec49 ("ranking tranche 16"),
# 9 of its 25 CONFIRMED names (8 explicitly mandated regardless of call
# count, plus spawning_boats which this batch's own fresh mission-drive
# run found a real measured hit for) - see src/lua_spec_confirmed_stubs_b0*.cpp's
# own Sec49 batch header comment and this test's own top comment.
add_executable(sr3luahost_tranche16_tests tests/synthetic_luahost_tranche16_test.cpp)
target_link_libraries(sr3luahost_tranche16_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_tranche16_test COMMAND sr3luahost_tranche16_tests)

# Ranking tranches 15/17/20/22/23 (Sec52/Sec51/Sec53/Sec55/Sec57), the 8
# real-hit names found across the whole Sec50-Sec59 backlog (2026-10-03,
# orchestrator-directed pass) - see src/lua_spec_confirmed_stubs_b0*.cpp's own
# batch header comment (right before stub_tutorial_lock) for the full
# citation.
add_executable(sr3luahost_tranche_realhits_tests tests/synthetic_luahost_tranche_realhits_test.cpp)
target_link_libraries(sr3luahost_tranche_realhits_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_tranche_realhits_test COMMAND sr3luahost_tranche_realhits_tests)

# Player runtime batch (2026-10-03): player_controls_disable/_enable and
# player_movement_disable (spec-lua-api-behaviour.md Sec7.22/Sec7.7/Sec7.25)
# on the real control-state stack (spec-player-runtime.md Sec2).
add_executable(sr3luahost_player_controls_tests tests/synthetic_luahost_player_controls_test.cpp)
target_link_libraries(sr3luahost_player_controls_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_player_controls_test COMMAND sr3luahost_player_controls_tests)

# Native-implementation batch 2026-10-07 (fresh mission-drive stub-hit
# ranking against a from-scratch build, cross-referenced against spec
# coverage): 16 names - audio_object_post_event (Sec2.4), character_is_ready
# (Sec7.27), character_playing_combat_move (Sec25.27), mesh_mover_hide/
# _show/_reset/_set_invulnerable (Sec9.16/9.17/9.12/14.9), door_close/
# door_lock (Sec9.14/9.15), object_spawn_pause/mip_streaming_pause
# (Sec60.1), city_zone_swap (Sec1.9), traffic_disable_lanes (Sec12.19),
# party_dismiss_all (Sec14.17), sidewalk_disable_nodes (Sec15.3),
# group_create_check_done (Sec22.18) - see src/lua_spec_confirmed_stubs_b0*.cpp's
# own batch header comment and this test's own top comment.
add_executable(sr3luahost_native_batch1007_tests tests/synthetic_luahost_native_batch1007_test.cpp)
target_link_libraries(sr3luahost_native_batch1007_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_native_batch1007_test COMMAND sr3luahost_native_batch1007_tests)

# character_is_in_vehicle (spec-lua-api-behaviour.md Sec3.10, 2026-10-07) -
# cleared for implementation separately from the batch above once Team A
# resolved the one remaining OPEN item (the vtable +0x70 call's argument
# shape). See src/lua_spec_confirmed_stubs_b0*.cpp's own doc comment above
# stub_character_is_in_vehicle and this test's own top comment.
add_executable(sr3luahost_character_is_in_vehicle_tests tests/synthetic_luahost_character_is_in_vehicle_test.cpp)
target_link_libraries(sr3luahost_character_is_in_vehicle_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_character_is_in_vehicle_test COMMAND sr3luahost_character_is_in_vehicle_tests)

# Section 6 batch (spec-lua-api-behaviour.md Sec6.4/6.6/6.8/6.9/6.13/6.14/
# 6.15/6.17/6.18, 2026-10-07 full-document spec sweep): set_ped_density/
# set_traffic_density, notoriety_set_can_decay, waypoint_remove,
# spawning_pedestrians/spawning_vehicles, traffic_disable_drawbridge_area,
# guardian_angel_register_script_group/_unregister_script_group - see
# src/lua_spec_confirmed_stubs_b0*.cpp's own batch header comment above
# stub_set_ped_density and this test's own top comment.
add_executable(sr3luahost_sec6_sweep_tests tests/synthetic_luahost_sec6_sweep_test.cpp)
target_link_libraries(sr3luahost_sec6_sweep_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_sec6_sweep_test COMMAND sr3luahost_sec6_sweep_tests)

# Section 7 batch (spec-lua-api-behaviour.md Sec7.2/7.3/7.4/7.6/7.8/7.9/7.13/
# 7.14/7.15/7.16/7.18/7.19/7.20/7.23/7.26/7.28/7.29, 2026-10-07 full-document
# spec sweep): character_exists, human_is_downed, character_show,
# character_ragdoll_set_last_resort_position, on_damage, on_mover_destroyed,
# on_detection, follower_make_independent, follower_set_can_abandon,
# set_dont_attack_me_on_sight_flag, npc_suppress_persona,
# clear_animation_state, character_prevent_explosion_fling,
# character_prevent_bumping, character_set_never_catch_fire,
# ai_scripted_action_is_complete, player_movement_enable - see
# src/lua_spec_confirmed_stubs_b0*.cpp's own batch header comment above
# stub_character_exists and this test's own top comment.
add_executable(sr3luahost_sec7_sweep_tests tests/synthetic_luahost_sec7_sweep_test.cpp)
target_link_libraries(sr3luahost_sec7_sweep_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_sec7_sweep_test COMMAND sr3luahost_sec7_sweep_tests)

# Section 9 batch (spec-lua-api-behaviour.md Sec9.1/9.2/9.5/9.8/9.9/9.11/9.13/
# 9.20, 2026-10-07 full-document spec sweep): vehicle_speed_override,
# vehicle_infinite_mass, set_unjackable_flag, vehicle_exists,
# teleport_vehicle, effect_play, effect_stop, inv_weapon_disable_all_slots -
# see src/lua_spec_confirmed_stubs_b0*.cpp's own batch header comment above
# stub_vehicle_speed_override and this test's own top comment.
add_executable(sr3luahost_sec9_sweep_tests tests/synthetic_luahost_sec9_sweep_test.cpp)
target_link_libraries(sr3luahost_sec9_sweep_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_sec9_sweep_test COMMAND sr3luahost_sec9_sweep_tests)

# Wave 1 Batch 4 of the Sec10-28 lua-natives fan-out (2026-10-08):
# spec-lua-api-behaviour.md Sec13 (28 names) + a 27-name subset of Sec14
# ("flag-setters/hooks/combat cluster"), 55 names total - see
# src/lua_spec_confirmed_stubs_b11_batch4_flags_combat.cpp's own batch
# header comment and this test's own top comment.
add_executable(sr3luahost_sec13_14_batch4_tests tests/synthetic_luahost_sec13_14_batch4_test.cpp)
target_link_libraries(sr3luahost_sec13_14_batch4_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_sec13_14_batch4_test COMMAND sr3luahost_sec13_14_batch4_tests)

# Wave 2, Batch W2-3 (2026-10-08): spec-lua-api-behaviour.md Sec20+Sec21
# ("ranks 351-450 tranche" parts A/C gameplay residue), 57 names - see
# src/lua_spec_confirmed_stubs_b16_wave2_sec20_21.cpp's own top comment and
# this test's own top comment.
add_executable(sr3luahost_wave2_w23_tests tests/synthetic_luahost_wave2_w23_test.cpp)
target_link_libraries(sr3luahost_wave2_w23_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_wave2_w23_test COMMAND sr3luahost_wave2_w23_tests)

# vehicle_chase (spec-lua-api-behaviour.md Sec9.4), dedicated pass,
# 2026-10-07: this document's own single highest real call-volume
# CONFIRMED-but-unimplemented native (72 static call sites), deliberately
# left out of the Section 9 sweep batch above as "MEDIUM complexity, not a
# data gap" - see src/lua_spec_confirmed_stubs_b0*.cpp's own batch header
# comment above stub_vehicle_chase and this test's own top comment.
add_executable(sr3luahost_vehicle_chase_tests tests/synthetic_luahost_vehicle_chase_test.cpp)
target_link_libraries(sr3luahost_vehicle_chase_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_vehicle_chase_test COMMAND sr3luahost_vehicle_chase_tests)

# Section 8 batch (spec-lua-api-behaviour.md Sec8.1/8.2/8.3/8.4/8.6/8.8/8.9,
# 2026-10-07 full-document spec sweep): audio_conversation_end,
# audio_conversation_load_direct, audio_persona_remove_2d, audio_stop/
# audio_stop_emitting_id, audio_is_playing, audio_persona_line_playing,
# audio_remove_mission_cellphone - see src/lua_spec_confirmed_stubs_b0*.cpp's
# own batch header comment above stub_audio_conversation_end and this
# test's own top comment.
add_executable(sr3luahost_sec8_sweep_tests tests/synthetic_luahost_sec8_sweep_test.cpp)
target_link_libraries(sr3luahost_sec8_sweep_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_sec8_sweep_test COMMAND sr3luahost_sec8_sweep_tests)

# Team-A-cleared batch (commit 97a9b8d): spec-lua-api-behaviour.md Sec1.1
# trigger_enable, Sec1.2 on_trigger, Sec1.4 objective_text_clear, Sec1.5
# mission_end_failure, Sec1.6 mission_set_checkpoint, Sec2.1
# game_get_platform, Sec2.5 audio_play_persona_line, Sec2.6
# game_is_active_input_gamepad, Sec3.2 on_death, Sec3.3
# on_vehicle_destroyed, Sec3.5 character_is_dead, Sec3.6
# notoriety_force_no_spawn - see src/lua_spec_confirmed_stubs_b0*.cpp's own
# doc comment above each stub_* function and this test's own top comment.
add_executable(sr3luahost_trigger_mission_cluster_tests tests/synthetic_luahost_trigger_mission_cluster_test.cpp)
target_link_libraries(sr3luahost_trigger_mission_cluster_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_trigger_mission_cluster_test COMMAND sr3luahost_trigger_mission_cluster_tests)

# spec-save-format.md Sec15 wiring (2026-10-08): round-trip proof for
# mission_autosave/game_autosave/mission_set_checkpoint's real writer/reader
# bridge (save_game_bridge.h) - see tests/synthetic_save_game_bridge_test.cpp's
# own top comment.
add_executable(sr3save_game_bridge_tests tests/synthetic_save_game_bridge_test.cpp)
target_link_libraries(sr3save_game_bridge_tests PRIVATE sr3luahost)
add_test(NAME synthetic_save_game_bridge_test COMMAND sr3save_game_bridge_tests)

# spec-frontend-menu-runtime.md wiring (2026-10-08): the 11-slot game-mode
# table, real new-game/horde-start/continue/zone-jump flows, the shared
# input-lock dispatcher and fade-completion teardown, and the mission-start
# marker/phase machine (menu_runtime.h) - see
# tests/synthetic_menu_runtime_test.cpp's own top comment for the critical
# negative result (new game never binds/starts a mission) this test proves
# alongside the real, data-driven marker-arm/trigger path to a mission's
# own `_start`.
add_executable(sr3menu_runtime_tests tests/synthetic_menu_runtime_test.cpp)
target_link_libraries(sr3menu_runtime_tests PRIVATE sr3luahost)
add_test(NAME synthetic_menu_runtime_test COMMAND sr3menu_runtime_tests)

# Wave 1 batch 1 (2026-10-08): spec-lua-api-behaviour.md §1-4
# (trigger/mission/teleport/combat-hook core) - objective_text, on_trigger_
# exit, spawn_region_enable, door_open, audio_conversation_play,
# game_peg_unload, pop_screen, object_is_in_trigger, ai_do_scripted_move,
# on_attack_performed, teleport, release_to_world, vehicle_suppress_npc_exit,
# set_player_can_enter_exit_vehicles, continuous_spawn_stop - see
# src/lua_spec_confirmed_stubs_b08_batch1_core.cpp's own top comment for the
# 6 names deferred from the original 21-name batch assignment and this
# test's own top comment.
add_executable(sr3luahost_batch1_sec1_4_core_tests tests/synthetic_luahost_batch1_sec1_4_core_test.cpp)
target_link_libraries(sr3luahost_batch1_sec1_4_core_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_batch1_sec1_4_core_test COMMAND sr3luahost_batch1_sec1_4_core_tests)

# Wave 1, Batch 5 (2026-10-08): spec-lua-api-behaviour.md Sec15 (25 names,
# HUD-slot/customization/pause-menu gameplay cluster) + Sec16 (16 names,
# UI cluster) - see
# src/lua_spec_confirmed_stubs_b12_batch5_hud_customization.cpp's own top
# comment and this test's own top comment.
add_executable(sr3luahost_batch5_hud_customization_tests tests/synthetic_luahost_batch5_hud_customization_test.cpp)
target_link_libraries(sr3luahost_batch5_hud_customization_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_batch5_hud_customization_test COMMAND sr3luahost_batch5_hud_customization_tests)

# Wave 1, Batch 3 (results/lua_natives_sec10_28_fanout_scoping_20261008.md,
# 2026-10-08): spec-lua-api-behaviour.md Sec11 (12 PCR/PCU/store/pause-menu
# UI-cluster names) + Sec12 (28 of its 29 gameplay-cluster names) - see
# src/lua_spec_confirmed_stubs_b10_batch3_pcr_vehicle.cpp's own top comment
# and this test's own top comment.
add_executable(sr3luahost_b10_batch3_tests tests/synthetic_luahost_b10_batch3_test.cpp)
target_link_libraries(sr3luahost_b10_batch3_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_b10_batch3_test COMMAND sr3luahost_b10_batch3_tests)

# The 6-native clearance fast-follow onto wave 1 batch 1 (2026-10-08) - see
# src/lua_spec_confirmed_stubs_b28_batch1_deferred6.cpp's own top comment
# and this test's own top comment.
add_executable(sr3luahost_b28_batch1_deferred6_tests tests/synthetic_luahost_b28_batch1_deferred6_test.cpp)
target_link_libraries(sr3luahost_b28_batch1_deferred6_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_b28_batch1_deferred6_test COMMAND sr3luahost_b28_batch1_deferred6_tests)

# b29 (2026-10-08): Sec6 backlog close-out - see
# src/lua_spec_confirmed_stubs_b29_sec6_notoriety_mission.cpp's own top
# comment and this test's own top comment.
add_executable(sr3luahost_b29_sec6_notoriety_mission_tests tests/synthetic_luahost_b29_sec6_notoriety_mission_test.cpp)
target_link_libraries(sr3luahost_b29_sec6_notoriety_mission_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_b29_sec6_notoriety_mission_test COMMAND sr3luahost_b29_sec6_notoriety_mission_tests)

# b30 (2026-10-08): tranche-F close-out (character_kill, vehicle_is_destroyed)
# + camera_restrict_set_active - see
# src/lua_spec_confirmed_stubs_b30_tranchef_camera.cpp's own top comment and
# this test's own top comment.
add_executable(sr3luahost_b30_tranchef_camera_tests tests/synthetic_luahost_b30_tranchef_camera_test.cpp)
target_link_libraries(sr3luahost_b30_tranchef_camera_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_b30_tranchef_camera_test COMMAND sr3luahost_b30_tranchef_camera_tests)

# b31 (2026-10-08): continuous_spawn_start/on_vehicle_enter/tutorial_start -
# see src/lua_spec_confirmed_stubs_b31_continuous_spawn_vehicle_enter_tutorial.cpp's
# own top comment and this test's own top comment.
add_executable(sr3luahost_b31_tests tests/synthetic_luahost_b31_continuous_spawn_vehicle_enter_tutorial_test.cpp)
target_link_libraries(sr3luahost_b31_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_b31_continuous_spawn_vehicle_enter_tutorial_test COMMAND sr3luahost_b31_tests)

# Wave 2, Batch W2-4 (2026-10-08): spec-lua-api-behaviour.md Sec22+Sec24+
# Sec25 gameplay residue (78 names) - see
# src/lua_spec_confirmed_stubs_b22_wave2_sec22.cpp/
# src/lua_spec_confirmed_stubs_b23_wave2_sec24_25.cpp's own top comments.
add_executable(sr3luahost_wave2_w24a_tests tests/synthetic_luahost_wave2_w24a_test.cpp)
target_link_libraries(sr3luahost_wave2_w24a_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_wave2_w24a_test COMMAND sr3luahost_wave2_w24a_tests)

add_executable(sr3luahost_wave2_w24b_tests tests/synthetic_luahost_wave2_w24b_test.cpp)
target_link_libraries(sr3luahost_wave2_w24b_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_wave2_w24b_test COMMAND sr3luahost_wave2_w24b_tests)

# Wave 2, Batch W2-7 (2026-10-08): spec-lua-api-behaviour.md Sec38-60 (113
# names, vehicles/helicopter/airplane/traffic | spawn/world/effects/audio/
# camera/mission-specific) - see src/lua_spec_confirmed_stubs_b26_wave2_
# vehicles_helicopter.cpp / src/lua_spec_confirmed_stubs_b27_wave2_spawn_
# world_audio.cpp's own top comments.
add_executable(sr3luahost_wave2_w27a_tests tests/synthetic_luahost_wave2_w27a_test.cpp)
target_link_libraries(sr3luahost_wave2_w27a_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_wave2_w27a_test COMMAND sr3luahost_wave2_w27a_tests)

add_executable(sr3luahost_wave2_w27b_tests tests/synthetic_luahost_wave2_w27b_test.cpp)
target_link_libraries(sr3luahost_wave2_w27b_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_wave2_w27b_test COMMAND sr3luahost_wave2_w27b_tests)

# Wave 2, Batch W2-5 (2026-10-08) - "Customization frontends," split 2 files
# (PCU/PCR/customization_* = b24, vcust/store/garage/crib/gang = b25). See
# each src/lua_spec_confirmed_stubs_b2{4,5}_wave2_*.cpp's own top comment
# and these tests' own top comments - HARDENING coverage for all 4 NAMED
# memory-safety fixes (both the valid-input and hardened-refusal paths).
add_executable(sr3luahost_wave2_w25a_tests tests/synthetic_luahost_wave2_w25a_test.cpp)
target_link_libraries(sr3luahost_wave2_w25a_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_wave2_w25a_test COMMAND sr3luahost_wave2_w25a_tests)

add_executable(sr3luahost_wave2_w25b_tests tests/synthetic_luahost_wave2_w25b_test.cpp)
target_link_libraries(sr3luahost_wave2_w25b_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_wave2_w25b_test COMMAND sr3luahost_wave2_w25b_tests)

# Wave 2 batch W2-6 (2026-10-08), "UI frontend (143 natives) - HARDENING
# BATCH" - split A (file b17: Sec19/Sec23/Sec26 + lobby/coop/session/steam
# `game_*`, 66 names) - see src/lua_spec_confirmed_stubs_b17_wave2_ui_
# frontend_a.cpp's own top comment and this test's own top comment.
add_executable(sr3luahost_wave2_w26a_tests tests/synthetic_luahost_wave2_w26a_test.cpp)
target_link_libraries(sr3luahost_wave2_w26a_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_wave2_w26a_test COMMAND sr3luahost_wave2_w26a_tests)

# Wave 2 batch W2-6 (2026-10-08) - split B (file b18: menus/pause/dialog/
# cellphone/cell/save/machinima/completion, 77 names, including this
# batch's 2 own named memory-safety fixes) - see src/lua_spec_confirmed_
# stubs_b18_wave2_ui_frontend_b.cpp's own top comment and this test's own
# top comment.
add_executable(sr3luahost_wave2_w26b_tests tests/synthetic_luahost_wave2_w26b_test.cpp)
target_link_libraries(sr3luahost_wave2_w26b_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_wave2_w26b_test COMMAND sr3luahost_wave2_w26b_tests)

# Wave 2, batch W2-8 (2026-10-08): spec-lua-api-behaviour.md Sec38-60
# "gameplay B" (93 names) - see src/lua_spec_confirmed_stubs_b19_wave2_
# char_npc_inv.cpp / _b20_wave2_hud_minimap_hooks.cpp's own top comments
# and this test's own top comment.
add_executable(sr3luahost_wave2_w28_tests tests/synthetic_luahost_wave2_w28_test.cpp)
target_link_libraries(sr3luahost_wave2_w28_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_wave2_w28_test COMMAND sr3luahost_wave2_w28_tests)

# Wave 2, Batch W2-2 (2026-10-08): spec-lua-api-behaviour.md Sec17 (30
# names) + Sec18 (30 names), 60 names total - see
# src/lua_spec_confirmed_stubs_b21_wave2_sec17_18.cpp's own top comment and
# this test's own top comment.
add_executable(sr3luahost_wave2_w22_tests tests/synthetic_luahost_wave2_w22_test.cpp)
target_link_libraries(sr3luahost_wave2_w22_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_wave2_w22_test COMMAND sr3luahost_wave2_w22_tests)

# Player runtime U2/U3 (spec-player-runtime.md Sec3/Sec5). Links sr3luahost
# only for the CharacterState +0x1d99 binding check, and
# sr3tables_progression for the header-only tweak_table.xtbl seeding
# adapter (sr3player/movement_tweak_table.h); sr3player itself uses just the
# header-only open_state.h.
add_executable(sr3player_movement_tests tests/synthetic_player_movement_test.cpp)
target_link_libraries(sr3player_movement_tests PRIVATE sr3player sr3luahost sr3tables_progression)
add_test(NAME synthetic_player_movement_test COMMAND sr3player_movement_tests)

# Player runtime U4 (camera mode machine and follow camera,
# spec-player-runtime.md Sec4). Links sr3tables_environment only for the
# header-only camera_free.xtbl seeding adapter (sr3player/camera_free_seed.h).
add_executable(sr3player_camera_tests tests/synthetic_player_camera_test.cpp)
target_link_libraries(sr3player_camera_tests PRIVATE sr3player sr3tables_environment)
add_test(NAME synthetic_player_camera_test COMMAND sr3player_camera_tests)

# Player runtime "U6 vehicle minimum slice" (vehicle-interaction slot
# registry/state skeleton U6.0, the element-selection rule U6.1, the seating
# commit U6.4, and the exit request + non-animated exit states 0x14-0x19
# U6.6; spec-player-runtime.md Sec6/Sec7/Sec8/Sec9). Kept as its OWN target
# (not appended to sr3player's own source list above) purely to keep this
# round's shared CMakeLists.txt edits additive-only - see
# include/sr3player/vehicle_occupancy.h's own banner for the full reasoning.
# Only needs sr3tables_vehicle_world/sr3luahost's headers (struct types /
# OpenStateError), both header-only for this file's purposes, via the same
# shared include/ root every target already uses - no link dependency.
add_library(sr3player_vehicle STATIC
    src/player_vehicle_occupancy.cpp
    src/player_seated_ride_pose.cpp
)
target_include_directories(sr3player_vehicle PUBLIC include)

# seated_ride_pose.cpp (2026-10-09 seated-driving-pose task, spec-player-
# runtime.md Sec7.4b) calls into sr3animpose's real U4 compose pipeline
# (pose_compose.h/clip_pose_bridge.h) to turn a resolved ride-clip name into
# an actual posed skeleton - the ONE new link dependency this target needed
# (everything else it uses, sr3tables_vehicle_world's own struct types, stays
# header-only per this target's pre-existing convention, see
# vehicle_occupancy.h's own banner).
target_link_libraries(sr3player_vehicle PUBLIC sr3animpose)

add_executable(sr3player_vehicle_tests tests/synthetic_player_vehicle_occupancy_test.cpp)
target_link_libraries(sr3player_vehicle_tests PRIVATE sr3player_vehicle)
add_test(NAME synthetic_player_vehicle_occupancy_test COMMAND sr3player_vehicle_tests)

add_executable(sr3player_seated_ride_pose_tests tests/synthetic_seated_ride_pose_test.cpp)
target_link_libraries(sr3player_seated_ride_pose_tests PRIVATE sr3player_vehicle)
add_test(NAME synthetic_seated_ride_pose_test COMMAND sr3player_seated_ride_pose_tests)

# Real-data end-to-end test for the same module (Sec7.4b): loads the real
# shipped vi_ride.xtbl/vehicle_interaction_info.xtbl, a real brad.rig_pc-
# compatible clip and brad.rig_pc itself, and checks ResolveRideAnimationString()/
# ChosenRealClipForRideAnimation()/ComposeSeatedPose() end to end against real
# data. Exit 77 = SKIPPED when the archive is absent.
# sr3_viewer's playtest command (tools/sr3_viewer.cpp, defined earlier in
# this file, line ~1239) now calls into sr3player::seated_ride_pose.h -
# added here, AFTER sr3player_vehicle/sr3tables_vehicle_world both exist,
# rather than in that earlier target_link_libraries(sr3_viewer ...) call
# (CMake requires a target to already exist when it is linked against).
if(TARGET sr3_viewer)
    target_link_libraries(sr3_viewer PRIVATE sr3player_vehicle sr3tables_vehicle_world)
endif()

# real_seated_ride_pose_tests itself moved below (seated-driving-pose merge,
# 2026-10-10 fix): it needs BOTH CRREISH_GAME_CACHE_DIR's own canonical `set(
# ... CACHE PATH)` declaration AND the CRREISH_VERIFY_BUILD `option()` to have
# already executed - this file is read top-to-bottom, and both of those live
# further down (right before real_vintdoc_document_tests, the project's own
# established convention for every real_*_test target). Defining this block
# HERE, ahead of either, silently compiled an EMPTY CRREISH_GAME_CACHE_DIR
# into the test binary on a truly fresh configure (no stale CMakeCache.txt to
# mask it) - found via this merge's own full fresh-build+ctest run: the test
# SKIPped (exit 77, "not present under <empty>") instead of exercising the
# real archive data its own file comment promises. sr3player_vehicle (this
# target's own link dependency, added just above) is unaffected and stays
# here; only the two targets that read CRREISH_GAME_CACHE_DIR/
# CRREISH_VERIFY_BUILD move.

# The zscene / cutscene scene table (Sec26.25 item 5): cutscene.xtbl names +
# one <name>.cte_xtbl per scene, on sr3xtbl like the other table readers.
add_library(sr3tables_cutscene STATIC src/tables_cutscene.cpp)
target_include_directories(sr3tables_cutscene PUBLIC include)
target_link_libraries(sr3tables_cutscene PUBLIC sr3xtbl)

add_executable(sr3tables_cutscene_tests tests/synthetic_tables_cutscene_test.cpp)
target_link_libraries(sr3tables_cutscene_tests PRIVATE sr3tables_cutscene)
add_test(NAME synthetic_tables_cutscene_test COMMAND sr3tables_cutscene_tests)

# sr3_viewer's own --mission zscene-table fix (coordinator-routed, 2026-10-10):
# needs sr3tables_cutscene, added here (AFTER it exists, same "CMake requires a
# target to already exist when it is linked against" reasoning as the
# sr3player_vehicle block above) rather than in sr3_viewer's own original
# target_link_libraries call.
if(TARGET sr3_viewer)
    target_link_libraries(sr3_viewer PRIVATE sr3tables_cutscene)
endif()

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
target_link_libraries(lua_host_run PRIVATE sr3luahost vpp_container sr3lua sr3asm sr3tables_cutscene sr3tables_weapons sr3tables_progression)

# Integration check (cloud phase 2026-09-30): lua_host_run end to end on a
# synthetic two-archive cache written by tests/lua_host_run_fixture.cpp
# (mission _start success, OPEN-state refusal, Lua runtime error, syntax
# error), and two runs diffed with tools/bridge_diff.py for determinism.
add_executable(lua_host_run_fixture tests/lua_host_run_fixture.cpp)
target_link_libraries(lua_host_run_fixture PRIVATE vpp_container)

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
# include( census (spec-lua-bindings.md Sec16.4 OPEN residue 1): which scripts call the bare global
# `include`, and with which names; flags calls naming a preload file.
add_executable(lua_include_census tools/lua_include_census.cpp)
target_link_libraries(lua_include_census PRIVATE vpp_container)

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

# Full-document walk + its Lua-host wiring (2026-10-03): synthetic (no game
# data) and real-data (interface_startup.vpp_pc from CRREISH_GAME_CACHE_DIR;
# exit 77 = SKIPPED when the archive is absent, e.g. CI).
add_executable(sr3vintdoc_document_tests tests/synthetic_vintdoc_document_test.cpp)
target_link_libraries(sr3vintdoc_document_tests PRIVATE sr3vintdoc sr3luahost)
add_test(NAME synthetic_vintdoc_document_test COMMAND sr3vintdoc_document_tests)

# Typed vint_set_property/vint_get_property through the per-type property
# descriptor tables (spec-lua-bindings.md Sec9.1/9.2/9.2a/20.1) and the
# query-time screen_nw/screen_se/screen_size geometry + screen_size setter
# (spec-vint-render.md Sec7/Sec8), 2026-10-03. No game data.
add_executable(sr3luahost_vint_property_tests tests/synthetic_vint_property_test.cpp)
target_link_libraries(sr3luahost_vint_property_tests PRIVATE sr3luahost)
add_test(NAME synthetic_vint_property_test COMMAND sr3luahost_vint_property_tests)

set(CRREISH_GAME_CACHE_DIR "D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache" CACHE PATH
    "Directory holding the shipped .vpp_pc archives, for real-data tests (skipped when absent)")

# --- Canonicalizing the real-data test config (this task, 2026-10-07) -----
# CRREISH_GAME_CACHE_DIR above already DEFAULTS to the real install path (not
# empty), so on a machine where that path exists, a plain `cmake -S . -B
# <dir>` with no override already compiles the real_*_test family to look
# there, and they run for real rather than SKIP - this was already true
# before this task; the risk this task actually closes is silent, UNNOTICED
# skipping: every real_*_test below still exits 77 (ctest SKIP, not a
# failure) whenever CRREISH_GAME_CACHE_DIR does not exist on the machine
# doing the build (a fresh machine, CI, a worktree configured before the
# game was installed, or an operator who overrode the cache variable to "").
# A plain `ctest` summary line does not distinguish "92/92 passed, 0 real-
# data tests ever ran" from "92/92 passed, including every real-data test" -
# see this task's own Sec4 for exactly that honest breakdown on this
# machine, run once with CRREISH_VERIFY_BUILD below to prove it isn't
# silently skipping anything.
#
# CRREISH_VERIFY_BUILD (default OFF: NO behavior change for a normal dev
# build/worktree that may legitimately have no game cache available) turns
# every real_*_test's SKIP_RETURN_CODE 77 registration below OFF, so exit 77
# is no longer special-cased: ctest then reports it as an ordinary FAILURE
# (nonzero exit), not SKIPPED/"Not Run". A verify/gating build - one meant to
# certify "the real-data checks actually ran and passed" - should configure
# with `-DCRREISH_VERIFY_BUILD=ON` (and a CRREISH_GAME_CACHE_DIR that really
# exists); doing so turns a missing-cache run from a quiet pass-through into
# a hard, visible failure instead of letting it hide inside an "N/N passed"
# figure that never actually exercised real shipped data.
option(CRREISH_VERIFY_BUILD
    "Verify/gating build: real-data tests (real_*_test) FAIL outright when CRREISH_GAME_CACHE_DIR is absent, instead of ctest-SKIPping (exit 77). Leave OFF for normal dev builds with no game cache available."
    OFF)

# Explicit SKIPPED count (this task's own 3rd ask): checked directly against
# the ctest/CMake version actually installed here (CMake 4.4.3) - its plain
# one-line summary ("100% tests passed out of N") silently EXCLUDES skipped
# tests from both the numerator and denominator, so it alone cannot tell "no
# real-data test ever ran" apart from "every real-data test ran and passed".
# ctest DOES already itemize them explicitly by name right below that line
# ("The following tests did not run: ... (Skipped)"), and a run with
# `--output-junit <file>.xml` ALSO already gets an unambiguous machine-
# readable count for free - no CMakeLists/wrapper change needed - via that
# file's own <testsuite tests="N" failures="F" skipped="S"> attributes
# (verified directly: a deliberately-skipped test produced skipped="1").
# CONVENTION for this project from now on: a verify/gating ctest run should
# pass `--output-junit <path>` and read tests/failures/skipped from the
# result, rather than quoting ctest's bare "N/M passed" headline (see this
# task's own Sec4 for a worked example on this machine).
if(CRREISH_VERIFY_BUILD)
    message(STATUS "CRREISH_VERIFY_BUILD=ON: real-data tests will FAIL (not SKIP) if "
        "CRREISH_GAME_CACHE_DIR ('${CRREISH_GAME_CACHE_DIR}') is absent at test time")
endif()

# Real-data end-to-end test for sr3player::seated_ride_pose.h (Sec7.4b,
# seated-driving-pose task): loads the real shipped vi_ride.xtbl/
# vehicle_interaction_info.xtbl, a real brad.rig_pc-compatible clip and
# brad.rig_pc itself, and checks ResolveRideAnimationString()/
# ChosenRealClipForRideAnimation()/ComposeSeatedPose() end to end against
# real data. Exit 77 = SKIPPED when the archive is absent. MOVED here
# (2026-10-10 merge fix) from right after sr3player_vehicle's own definition,
# above - it must come after CRREISH_GAME_CACHE_DIR's `set(... CACHE PATH)`
# and CRREISH_VERIFY_BUILD's `option()`, both just above, same as every
# other real_*_test target below.
add_executable(real_seated_ride_pose_tests tests/real_seated_ride_pose_test.cpp)
target_link_libraries(real_seated_ride_pose_tests PRIVATE sr3player_vehicle sr3tables_vehicle_world sr3rig vppcache)
target_compile_definitions(real_seated_ride_pose_tests PRIVATE CRREISH_GAME_CACHE_DIR="${CRREISH_GAME_CACHE_DIR}")
add_test(NAME real_seated_ride_pose_test COMMAND real_seated_ride_pose_tests)
if(NOT CRREISH_VERIFY_BUILD)
    set_tests_properties(real_seated_ride_pose_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

add_executable(real_vintdoc_document_tests tests/real_vintdoc_document_test.cpp)
target_link_libraries(real_vintdoc_document_tests PRIVATE sr3vintdoc sr3luahost vppcache)
target_compile_definitions(real_vintdoc_document_tests PRIVATE CRREISH_GAME_CACHE_DIR="${CRREISH_GAME_CACHE_DIR}")
add_test(NAME real_vintdoc_document_test COMMAND real_vintdoc_document_tests)
if(NOT CRREISH_VERIFY_BUILD)
    set_tests_properties(real_vintdoc_document_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Real-data smoke test for the "playtest v0" milestone's new `sr3_viewer
# playtest` interactive command (2026-10-08) - shells out to the real,
# freshly-built sr3_viewer.exe (tests/real_playtest_smoke_test.cpp's own
# header comment has the full account) with --headless --no-input for a
# deterministic 300-frame run against the real game cache, and checks a
# clean exit, a real non-trivial capture PNG, and the expected final
# stdout line. SKIPPED (exit 77) when CRREISH_GAME_CACHE_DIR is absent,
# same convention as real_vintdoc_document_test above. WIN32-only (shells
# out via CreateProcess/cmd.exe, same as golden_scene_check). --bin-dir
# uses the SAME $<TARGET_FILE_DIR:sr3_viewer> genex golden_scene_check's
# own ctest registration already relies on, not a hardcoded path.
if(WIN32)
    add_executable(real_playtest_smoke_tests tests/real_playtest_smoke_test.cpp)
    target_compile_definitions(real_playtest_smoke_tests PRIVATE CRREISH_GAME_CACHE_DIR="${CRREISH_GAME_CACHE_DIR}")
    add_test(NAME real_playtest_smoke_test
             COMMAND real_playtest_smoke_tests --bin-dir $<TARGET_FILE_DIR:sr3_viewer>)
    set_tests_properties(real_playtest_smoke_test PROPERTIES
        LABELS "golden"
        RESOURCE_LOCK "sr3_gpu_device")
    add_dependencies(real_playtest_smoke_tests sr3_viewer)
    if(NOT CRREISH_VERIFY_BUILD)
        set_tests_properties(real_playtest_smoke_test PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endif()

# Real-data smoke test for playtest's new vehicle entry/driving interaction
# (2026-10-08, playtest-vehicle task) - a SIBLING of real_playtest_smoke_test
# above (tests/real_playtest_vehicle_smoke_test.cpp's own header comment has
# the full account): --headless --no-input --script-vehicle, --spawn/
# --vehicle-spawn placed inside the real proximity gate so the scripted
# E-press exercises the SAME real code path a live player's walk-up would.
# Checks a clean exit, the vehicle actually loading, both "[vehicle]
# entered"/"[vehicle] exited" firing, a real non-trivial mid-drive capture
# PNG, and the expected final stdout line. SKIPPED (exit 77) when
# CRREISH_GAME_CACHE_DIR is absent, same convention as every other real_*
# test. WIN32-only, same reason as real_playtest_smoke_test above.
if(WIN32)
    add_executable(real_playtest_vehicle_smoke_tests tests/real_playtest_vehicle_smoke_test.cpp)
    target_compile_definitions(real_playtest_vehicle_smoke_tests PRIVATE CRREISH_GAME_CACHE_DIR="${CRREISH_GAME_CACHE_DIR}")
    add_test(NAME real_playtest_vehicle_smoke_test
             COMMAND real_playtest_vehicle_smoke_tests --bin-dir $<TARGET_FILE_DIR:sr3_viewer>)
    set_tests_properties(real_playtest_vehicle_smoke_test PROPERTIES
        LABELS "golden"
        RESOURCE_LOCK "sr3_gpu_device")
    add_dependencies(real_playtest_vehicle_smoke_tests sr3_viewer)
    if(NOT CRREISH_VERIFY_BUILD)
        set_tests_properties(real_playtest_vehicle_smoke_test PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endif()

# Real-data smoke test for playtest's new near-field ambient-vehicle-traffic/
# police-roadblock WIRING (2026-10-09, playtest-nearfield-traffic-roadblock
# task) - a SIBLING of real_playtest_vehicle_smoke_test above
# (tests/real_playtest_traffic_roadblock_smoke_test.cpp's own header comment
# has the full account): --headless --no-input --script-roadblock, checks a
# clean exit, a non-zero near-field traffic live count actually appearing in
# stdout, both "[roadblock] armed"/"[roadblock] disarmed" firing from the
# scripted 'R'-press pair, a non-zero roadblock filled-slot count while
# armed, a real non-trivial capture PNG, and the expected final stdout line.
# Tests ONLY the new per-frame call-site wiring (this task's own scope note:
# the underlying population algorithms already have their own full synthetic
# coverage, tests/synthetic_near_field_vehicle_population_test.cpp/tests/
# synthetic_police_roadblock_spawn_test.cpp - not re-tested here). SKIPPED
# (exit 77) when CRREISH_GAME_CACHE_DIR is absent, same convention as every
# other real_* test. WIN32-only, same reason as real_playtest_smoke_test above.
if(WIN32)
    add_executable(real_playtest_traffic_roadblock_smoke_tests tests/real_playtest_traffic_roadblock_smoke_test.cpp)
    target_compile_definitions(real_playtest_traffic_roadblock_smoke_tests PRIVATE CRREISH_GAME_CACHE_DIR="${CRREISH_GAME_CACHE_DIR}")
    add_test(NAME real_playtest_traffic_roadblock_smoke_test
             COMMAND real_playtest_traffic_roadblock_smoke_tests --bin-dir $<TARGET_FILE_DIR:sr3_viewer>)
    set_tests_properties(real_playtest_traffic_roadblock_smoke_test PROPERTIES
        LABELS "golden"
        RESOURCE_LOCK "sr3_gpu_device")
    add_dependencies(real_playtest_traffic_roadblock_smoke_tests sr3_viewer)
    if(NOT CRREISH_VERIFY_BUILD)
        set_tests_properties(real_playtest_traffic_roadblock_smoke_test PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endif()

# Real-data regression for the world-streaming reconcile loop's steady-state
# behaviour (2026-10-08, world-streaming-reconcile task, spec-world-
# streaming.md Sec10.4/Sec11) - a SIBLING of real_playtest_smoke_test above
# (tests/real_playtest_streaming_longwalk_test.cpp's own header comment has
# the full account): a long (750-frame), --no-collision scripted walk far
# enough to cross real level-4 fine-cell/tile boundaries, proving the
# wanted-cell-list actually changes, previously-resident cells/ground tiles
# are actually evicted (not just silently left loaded), and new ones load -
# not merely that reconcile() was called. SKIPPED (exit 77) when
# CRREISH_GAME_CACHE_DIR is absent, same convention as every other real_*
# test. WIN32-only, same reason as real_playtest_smoke_test above.
if(WIN32)
    add_executable(real_playtest_streaming_longwalk_tests tests/real_playtest_streaming_longwalk_test.cpp)
    target_compile_definitions(real_playtest_streaming_longwalk_tests PRIVATE CRREISH_GAME_CACHE_DIR="${CRREISH_GAME_CACHE_DIR}")
    add_test(NAME real_playtest_streaming_longwalk_test
             COMMAND real_playtest_streaming_longwalk_tests --bin-dir $<TARGET_FILE_DIR:sr3_viewer>)
    set_tests_properties(real_playtest_streaming_longwalk_test PROPERTIES
        LABELS "golden"
        RESOURCE_LOCK "sr3_gpu_device")
    add_dependencies(real_playtest_streaming_longwalk_tests sr3_viewer)
    if(NOT CRREISH_VERIFY_BUILD)
        set_tests_properties(real_playtest_streaming_longwalk_test PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endif()

# Real-data integration test for playtest's new MISSION-HOST PHASE 1
# `--mission=<stem>` debug flag (2026-10-09, branch feature/mission-host-
# phase1-debug-flag) - a SIBLING of real_playtest_smoke_test above (tests/
# real_playtest_mission_test.cpp's own header comment has the full account):
# drives the real shipped `mm_p_03` mission script once per rendered frame
# through the SAME Host/EngineState this command already builds for --hud,
# checking (A) the real resolve/preload(system_lib.lua+game_lib.lua)/bind/
# per-frame-scheduler-pass-drive path actually runs and a real later mission
# error does not crash the rest of the live loop, and (B) the NEW wall-clock
# --mission-timeout safety cutoff fires cleanly and deterministically.
# CRREISH_TOOLS_DIR (same compile-definition convention tests/synthetic_
# luahost_roster_test.cpp already uses) locates this repo's own tools/
# lua_all_registered_1490_tagged.txt - a real file this project's own
# source tree ships, not copied game content. SKIPPED (exit 77) when
# CRREISH_GAME_CACHE_DIR is absent, same convention as every other real_*
# test. WIN32-only, same reason as real_playtest_smoke_test above.
if(WIN32)
    add_executable(real_playtest_mission_tests tests/real_playtest_mission_test.cpp)
    target_compile_definitions(real_playtest_mission_tests PRIVATE
        CRREISH_GAME_CACHE_DIR="${CRREISH_GAME_CACHE_DIR}"
        CRREISH_TOOLS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tools")
    add_test(NAME real_playtest_mission_test
             COMMAND real_playtest_mission_tests --bin-dir $<TARGET_FILE_DIR:sr3_viewer>)
    set_tests_properties(real_playtest_mission_test PROPERTIES
        LABELS "golden"
        RESOURCE_LOCK "sr3_gpu_device")
    add_dependencies(real_playtest_mission_tests sr3_viewer)
    if(NOT CRREISH_VERIFY_BUILD)
        set_tests_properties(real_playtest_mission_test PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endif()

# Real-data test for the engine-curve crossed-mapping wiring (2026-10-08,
# engine-curve/slip-table follow-up task, spec-physics-runtime.md Sec10.2/
# Sec10.8, synced commit 4c7d3f2): loads real base-game *_veh.xtbl files out
# of misc_tables.vpp_pc (CRREISH_GAME_CACHE_DIR) and checks the real crossed
# mapping, the retired-vs-live engine model difference, and distinct
# acceleration behaviour across 3 real vehicles - see the test file's own
# header comment. Exit 77 = SKIPPED when the archive is absent.
add_executable(real_vehicle_engine_torque_crossmap_tests tests/real_vehicle_engine_torque_crossmap_test.cpp)
target_link_libraries(real_vehicle_engine_torque_crossmap_tests PRIVATE sr3physics sr3vehicleinfo vppcache)
target_compile_definitions(real_vehicle_engine_torque_crossmap_tests PRIVATE CRREISH_GAME_CACHE_DIR="${CRREISH_GAME_CACHE_DIR}")
add_test(NAME real_vehicle_engine_torque_crossmap_test COMMAND real_vehicle_engine_torque_crossmap_tests)
if(NOT CRREISH_VERIFY_BUILD)
    set_tests_properties(real_vehicle_engine_torque_crossmap_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Real-data test for ResolveDriverHideOccupant()/FindSeatByResolverIndex()
# (vehicle-occupant-hide-flag task, 2026-10-09, spec-render-pipeline.md
# Sec23.14): loads the real vehicle_interaction_info.xtbl out of
# misc_tables.vpp_pc (CRREISH_GAME_CACHE_DIR) and checks that exactly the 2
# real tank rows ("Tank NatGrd"/"Tank STAG") hide their driver seat's
# occupant, their own gunner seat does not, and all 49 other real rows
# resolve to "shown" - a full real-data census, not a spot check. Exit 77 =
# SKIPPED when the archive is absent.
add_executable(real_vehicle_interaction_hide_occupant_tests tests/real_vehicle_interaction_hide_occupant_test.cpp)
target_link_libraries(real_vehicle_interaction_hide_occupant_tests PRIVATE sr3tables_vehicle_world sr3xtbl vppcache)
target_compile_definitions(real_vehicle_interaction_hide_occupant_tests PRIVATE CRREISH_GAME_CACHE_DIR="${CRREISH_GAME_CACHE_DIR}")
add_test(NAME real_vehicle_interaction_hide_occupant_test COMMAND real_vehicle_interaction_hide_occupant_tests)
if(NOT CRREISH_VERIFY_BUILD)
    set_tests_properties(real_vehicle_interaction_hide_occupant_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Real-data regression for sr3zone's Objects (0x2234) reader
# (include/sr3zone/zone_objects.h, spec-zone-data-format.md Sec11,
# 2026-10-07) - a SMALL, FIXED archive (sr3_city_0.vpp_pc only; not the
# whole population - see tools/zone_objects_population.cpp for that),
# pinned to exact population counts obtained from this reader's own
# whole-population tool and cross-checked for internal consistency (see
# the test file's own header comment). SKIPPED (exit 77) when
# CRREISH_GAME_CACHE_DIR is absent, same convention as
# real_vintdoc_document_test above.
add_executable(real_zone_objects_tests tests/real_zone_objects_test.cpp)
target_link_libraries(real_zone_objects_tests PRIVATE sr3zone vpp_container sr3save vppcache)
target_compile_definitions(real_zone_objects_tests PRIVATE CRREISH_GAME_CACHE_DIR="${CRREISH_GAME_CACHE_DIR}")
add_test(NAME real_zone_objects_test COMMAND real_zone_objects_tests)
if(NOT CRREISH_VERIFY_BUILD)
    set_tests_properties(real_zone_objects_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Real-data regression for the audio bring-up task's Wwise-Vorbis decode
# attempt (include/sr3audioruntime/{wwise_vorbis_setup.h,
# real_vorbis_decode.h}, spec-audio-runtime.md Sec11.11) - every real
# `_media.bnk_pc` entry in cutscene_sounds.vpp_pc (the smallest of the four
# raw/uncompressed audio archives), pinning this project's own 2026-10-08
# real-data correction to the vorb-chunk block-size-exponent byte position
# and the exact, deterministic libvorbis outcome of the genuinely-open
# setup-packet hypothesis (see the test file's own header comment). SKIPPED
# (exit 77) when CRREISH_GAME_CACHE_DIR is absent, same convention as
# real_zone_objects_test above.
add_executable(real_vorbis_decode_tests tests/real_vorbis_decode_test.cpp)
target_link_libraries(real_vorbis_decode_tests PRIVATE sr3audioruntime sr3audio vpp_container sr3runtimedata)
target_compile_definitions(real_vorbis_decode_tests PRIVATE CRREISH_GAME_CACHE_DIR="${CRREISH_GAME_CACHE_DIR}")
add_test(NAME real_vorbis_decode_test COMMAND real_vorbis_decode_tests)

if(NOT CRREISH_VERIFY_BUILD)
    set_tests_properties(real_vorbis_decode_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

# DIAGNOSTIC-ONLY tool (2026-10-08, out-of-range-codebook-index bring-up
# task, NOT a ctest - same "one-shot real-data probe" convention as
# zone_object_resolver_probe below): empirically checks whether
# wwise_setup_rebuild.cpp's OUTER per-codebook 10-bit-index bit grammar
# itself is correct against the real population, by walking past an
# out-of-range index instead of aborting on it - see
# sr3audioruntime::probeSetupPacketBitBudget()'s own header comment and
# tools/wwise_setup_budget_probe.cpp's own top comment for the full
# rationale.
add_executable(wwise_setup_budget_probe tools/wwise_setup_budget_probe.cpp)
target_link_libraries(wwise_setup_budget_probe PRIVATE sr3audioruntime sr3audio vpp_container)

# DIAGNOSTIC-ONLY tool (2026-10-08, vorb-chunk explicit-offset-field
# hypothesis task, NOT a ctest - same convention as wwise_setup_budget_probe
# above): tests spec-audio-runtime.md Sec11.11.2's own "+0x7c/+0x80...two
# offset-shaped fields" table row (vorb bytes [4:8)/[8:12) as explicit
# setupPacketOffset/firstAudioPacketOffset fields, per the public, generic
# Wwise-RIFX/Vorbis container format ww2ogg/vgmstream document) against the
# real population - see tools/wwise_vorb_offset_probe.cpp's own top comment.
add_executable(wwise_vorb_offset_probe tools/wwise_vorb_offset_probe.cpp)
target_link_libraries(wwise_vorb_offset_probe PRIVATE sr3audioruntime sr3audio vpp_container)

# DIAGNOSTIC-ONLY tool (2026-10-08, vorb-chunk seek-table-relative-offset
# confirmation task, NOT a ctest - same convention as wwise_vorb_offset_probe
# above): tests the disassembly-CONFIRMED formula spec-audio-runtime.md
# Sec11.11.5 now states (commit 64f705a / c1f586e on this branch) -
# setupPacketStart = dataChunkOffset + vorb[4:8) + vorb[16:20), and the
# analogous firstAudioPacketStart - against the real population. See
# tools/wwise_vorb_seektable_offset_probe.cpp's own top comment.
add_executable(wwise_vorb_seektable_offset_probe tools/wwise_vorb_seektable_offset_probe.cpp)
target_link_libraries(wwise_vorb_seektable_offset_probe PRIVATE sr3audioruntime sr3audio vpp_container)

# DIAGNOSTIC-ONLY tool (2026-10-08, vorb-chunk setup/audio-offset-pair
# re-hypothesis task, NOT a ctest - same convention as
# wwise_vorb_seektable_offset_probe above, whose own REJECTED result this
# tool follows up on): tests setupPacketStart = dataChunkOffset +
# vorb[16:20) (the seek-table-length field doubling as its own end-offset)
# and firstAudioPacketStart = dataChunkOffset + vorb[20:24) (the NEXT dword,
# not vorb[8:12) which the REJECTED formula used) against the real
# population - see tools/wwise_vorb_setup_audio_dword_probe.cpp's own top
# comment, including the public-source (vgmstream) corroboration cited there.
add_executable(wwise_vorb_setup_audio_dword_probe tools/wwise_vorb_setup_audio_dword_probe.cpp)
target_link_libraries(wwise_vorb_setup_audio_dword_probe PRIVATE sr3audioruntime sr3audio vpp_container)

# DIAGNOSTIC-ONLY tool (2026-10-08, same task follow-up): verbose bit-by-bit
# trace of one real record's setup packet, using the NEW (now-corroborated)
# offset formula, to locate the exact field causing every record's uniform
# "residue 0, EOF reading booklist entry" failure - see
# tools/wwise_setup_packet_bit_trace_probe.cpp's own top comment.
add_executable(wwise_setup_packet_bit_trace_probe tools/wwise_setup_packet_bit_trace_probe.cpp)
target_link_libraries(wwise_setup_packet_bit_trace_probe PRIVATE sr3audioruntime sr3audio vpp_container oggstatic)

# DIAGNOSTIC-ONLY tool (2026-10-08, independent-oracle verification task, NOT
# a ctest - same convention as the probes above): dumps, for every real
# record tests/real_vorbis_decode_test.cpp reports ok==true, both this
# project's own decoded WAV (ours_<name>.wav, via the real
# attemptWwiseVorbisDecode() path) and the raw, untouched Wwise-wrapped
# record bytes it was decoded FROM (raw_<name>.wem) - inputs for an
# independent, off-the-shelf oracle decoder (vgmstream-cli) run as a black
# box outside this build, per the orchestrator's correction that "accepted
# by this project's own internal checks" is not evidence of bit-correctness
# on its own. See tools/wwise_vorbis_oracle_extract.cpp's own top comment.
add_executable(wwise_vorbis_oracle_extract tools/wwise_vorbis_oracle_extract.cpp)
target_link_libraries(wwise_vorbis_oracle_extract PRIVATE sr3audioruntime sr3audio vpp_container)

# DIAGNOSTIC-ONLY tool (2026-10-08, same independent-oracle verification task,
# NOT a ctest): duplicates real_vorbis_decode.cpp's own per-audio-packet
# vorbis_synthesis() loop with per-packet pass/fail logging (run-length
# pattern, first-failing-packet index) - the "find the first divergent
# packet" diagnostic once the oracle comparison above found the prior pass's
# "5/493 real decodes" do not correlate with the real audio at all. See
# tools/wwise_vorbis_packet_divergence_probe.cpp's own top comment.
add_executable(wwise_vorbis_packet_divergence_probe tools/wwise_vorbis_packet_divergence_probe.cpp)
target_link_libraries(wwise_vorbis_packet_divergence_probe PRIVATE sr3audioruntime sr3audio vpp_container oggstatic vorbisstatic)

# Real-data regression for the HEAD -> MIDDLE g-cursor handoff fix
# (sr3clmesh::LevelMesh::gCursorAfterHeadChain(), include/sr3clmesh/
# level_mesh.h note 9, 2026-10-08) - 5 named real `.clmesh_pc` street/ramp
# props (hwy_yellow_st_r, hwy_sw, hwy_sw_l, hwy_sw_r, hwy_yellow_ap_r) that
# were silently SKIPPED before this fix. SKIPPED (exit 77) when
# CRREISH_GAME_CACHE_DIR is absent, same convention as real_zone_objects_test
# above.
add_executable(real_hwy_ramp_props_tests tests/real_hwy_ramp_props_test.cpp)
target_link_libraries(real_hwy_ramp_props_tests PRIVATE sr3clmesh sr3mesh sr3geometry vppcache)
target_compile_definitions(real_hwy_ramp_props_tests PRIVATE CRREISH_GAME_CACHE_DIR="${CRREISH_GAME_CACHE_DIR}")
add_test(NAME real_hwy_ramp_props_test COMMAND real_hwy_ramp_props_tests)
if(NOT CRREISH_VERIFY_BUILD)
    set_tests_properties(real_hwy_ramp_props_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

# One-shot population analysis tool (not a ctest - see tools/validation/
# README.md's standing rule that whole-archive sweeps are diagnostics, not
# gating tests): walks every shipped .czn_pc across the archives given on
# argv and reports, per file, the Objects-section object count, plus a
# whole-population histogram of property-tail lengths/shapes - built for
# Team A's own open U2 ("record interiors") work, spec-zone-data-format.md
# Sec11.9 item 8.
add_executable(zone_objects_population tools/zone_objects_population.cpp)
target_link_libraries(zone_objects_population PRIVATE sr3zone vpp_container)

# Throwaway investigation tool (2026-10-08, road/sidewalk texture
# resolution task): empirically locates where a real `.czn_pc` ground
# tile's embedded `.tga` texture name(s) (spec-zone-data-format.md
# Sec13.2) sit relative to its located `0x2237` Mesh sub-block(s) - see
# tools/zone_texture_probe.cpp's own top comment for why this was needed
# (no existing reader locates a `0x2237` record's own body span).
add_executable(zone_texture_probe tools/zone_texture_probe.cpp)
target_link_libraries(zone_texture_probe PRIVATE sr3zone vpp_container)

# One-shot real-data probe (2026-10-08, zone-data fast-follow task, NOT a
# gating test - same convention as zone_objects_population above): confirms
# the real `Killbane` before/after example this task's own report cites -
# see tools/zone_object_resolver_probe.cpp's own top comment.
add_executable(zone_object_resolver_probe tools/zone_object_resolver_probe.cpp)
target_link_libraries(zone_object_resolver_probe PRIVATE sr3luahost vpp_container)

add_executable(vintdoc_validate tools/vintdoc_validate.cpp)
target_link_libraries(vintdoc_validate PRIVATE sr3vintdoc vpp_container)

# sr3font (2026-10-03, UI render unit U5 - text): the .vf3_pc bitmap-font
# reader, the game's named font table (DEFAULT language arrangement only),
# the metric callbacks, per-glyph draw geometry (spec-vint-font-format.md,
# spec-vint-render.md Sec14.1), and Vint text string building / markup /
# layout (spec-vint-render.md Sec14.2-Sec14.5). No D3D, no game data at
# build time. Synthetic test: no game data. Real-data test: the six shipped
# .vf3_pc files in misc.vpp_pc from CRREISH_GAME_CACHE_DIR, checked against
# the spec's own emulated reference vectors (exit 77 = SKIPPED when absent).
add_library(sr3font STATIC src/vint_font.cpp src/vint_text.cpp)
target_include_directories(sr3font PUBLIC include)

add_executable(sr3font_tests tests/synthetic_vint_font_test.cpp)
target_link_libraries(sr3font_tests PRIVATE sr3font)
add_test(NAME synthetic_vint_font_test COMMAND sr3font_tests)

add_executable(real_vint_font_tests tests/real_vint_font_test.cpp)
target_link_libraries(real_vint_font_tests PRIVATE sr3font vppcache)
target_compile_definitions(real_vint_font_tests PRIVATE CRREISH_GAME_CACHE_DIR="${CRREISH_GAME_CACHE_DIR}")
add_test(NAME real_vint_font_test COMMAND real_vint_font_tests)
if(NOT CRREISH_VERIFY_BUILD)
    set_tests_properties(real_vint_font_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

# sr3locstr (2026-10-03): the `.le_strings` localisation string-table
# reader and key-hash lookup (spec-localization-format.md). No D3D, no
# game data at build time. Synthetic test: no game data. Real-data test:
# the 11 US tables loaded from misc.vpp_pc/patch_compressed.vpp_pc in
# CRREISH_GAME_CACHE_DIR, checked against the spec's own validated record
# counts and known key->text answers (exit 77 = SKIPPED when absent).
add_library(sr3locstr STATIC src/le_strings.cpp)
target_include_directories(sr3locstr PUBLIC include)
target_link_libraries(sr3locstr PUBLIC sr3save)

# sr3font -> sr3locstr (2026-10-07): formatInsertions' `{N}`/`{N:text_tag_crc}`
# rows (spec-vint-render.md Sec14.2) now call the real `.le_strings` lookup
# (spec-localization-format.md Sec6/Sec9) instead of staying OPEN. Linked here,
# after sr3locstr's own add_library, so this stays an additive edit at
# sr3font's declaration site above.
target_link_libraries(sr3font PUBLIC sr3locstr)

add_executable(sr3locstr_tests tests/synthetic_le_strings_test.cpp)
target_link_libraries(sr3locstr_tests PRIVATE sr3locstr)
add_test(NAME synthetic_le_strings_test COMMAND sr3locstr_tests)

add_executable(real_le_strings_tests tests/real_le_strings_test.cpp)
target_link_libraries(real_le_strings_tests PRIVATE sr3locstr vppcache)
target_compile_definitions(real_le_strings_tests PRIVATE CRREISH_GAME_CACHE_DIR="${CRREISH_GAME_CACHE_DIR}")
add_test(NAME real_le_strings_test COMMAND real_le_strings_tests)
if(NOT CRREISH_VERIFY_BUILD)
    set_tests_properties(real_le_strings_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Real-data test for sr3animruntime U0 (spec-anim-runtime.md Sec1.4 / Sec4):
# loads the shipped control_parameters.xtbl (misc_tables.vpp_pc entry 946)
# and checks the 87-row registry and all 62 compiled-in names against it.
# Exit 77 = SKIPPED when the archive is absent.
add_executable(real_anim_control_parameters_tests tests/real_anim_control_parameters_test.cpp)
target_link_libraries(real_anim_control_parameters_tests PRIVATE sr3animruntime sr3tables_ui_controls vppcache)
target_compile_definitions(real_anim_control_parameters_tests PRIVATE CRREISH_GAME_CACHE_DIR="${CRREISH_GAME_CACHE_DIR}")
add_test(NAME real_anim_control_parameters_test COMMAND real_anim_control_parameters_tests)
if(NOT CRREISH_VERIFY_BUILD)
    set_tests_properties(real_anim_control_parameters_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Real-data population test for sr3animruntime U2/U3 (spec-anim-runtime.md
# Sec7.12 / Sec8.10): loads all 249 node-graph files listed by
# node_graph_files.xtbl (misc_tables.vpp_pc) plus the patched free_fall_bt
# (patch_compressed.vpp_pc) and checks every reference, census and replay
# number. Exit 77 = SKIPPED when the archives are absent.
add_executable(real_anim_node_graph_tests tests/real_anim_node_graph_test.cpp)
target_link_libraries(real_anim_node_graph_tests PRIVATE sr3animruntime sr3tables_animation vppcache)
target_compile_definitions(real_anim_node_graph_tests PRIVATE CRREISH_GAME_CACHE_DIR="${CRREISH_GAME_CACHE_DIR}")
add_test(NAME real_anim_node_graph_test COMMAND real_anim_node_graph_tests)
if(NOT CRREISH_VERIFY_BUILD)
    set_tests_properties(real_anim_node_graph_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

# CTAB constant census over every shipped D3D9 shader (bridge job 06):
# per-constant name -> register set/index/count TSV.
add_executable(ctab_census tools/ctab_census.cpp)
target_link_libraries(ctab_census PRIVATE sr3d3d9bc sr3fxo vpp_container)

# Bink container header probe (brand-new Bink subsystem bring-up): parses +
# validates a real shipped .bik file's fixed 44-byte header strictly per
# spec-bink-codec.md/spec-bink-boundary.md, then reports the spec's own OPEN
# gaps that block going further (frame-offset-table location, entropy-coding
# bit-level math, transform semantics, final pixel-write/colorspace step) -
# see tools/bink_frame0_probe.cpp's own top-of-file comment for the full
# account. Standalone: no project library dependency needed, plain file I/O.
add_executable(bink_frame0_probe tools/bink_frame0_probe.cpp)

# Generalized sibling of bink_frame0_probe.cpp: extracts ANY frame index's
# raw compressed bytes (not just frame 0), reusing the same clean-room
# header-parse + empirical frame-offset-table-location logic. Added for this
# task's own FFmpeg black-box validation step, which needs frame 60 (not
# just frame 0) of the logo movie plus a later frame of main_menu_bg.bik.
# Standalone, no project library dependency, same convention as
# bink_frame0_probe.cpp itself.
add_executable(bink_frameN_extract tools/bink_frameN_extract.cpp)

# Bink real-data test-vector extractor (fix/bink-decoder-test-vector-
# validation task, 2026-10-08): parses spec-bink-video-test-vectors.md's own
# committed, human-readable hex-dump data appendix and writes out its 22 raw
# binary blobs, verifying EACH one's SHA-256 (tests/bink_sha256.h, a sibling
# of tests/bink_md5.h) against both the per-blob header hash and the CSV
# index table's own hash before writing - see tools/bink_test_vector_extract.cpp's
# own top comment. This is how tests/golden/bink_main_menu_bg/vectors/ (used
# by real_bink_test_vector_validation_tests below) gets (re)populated locally
# - the blobs themselves are gitignored, same "regenerated locally" rule
# tests/golden/ already applies to every other real-data fixture.
add_executable(bink_test_vector_extract tools/bink_test_vector_extract.cpp)
target_include_directories(bink_test_vector_extract PRIVATE tests)

# Bink decoder SKELETON (include/sr3binkcodec/*.h, this task's own Bink
# decode-skeleton bring-up, 2026-10-08): a bitstream reader matching the
# CONFIRMED shared-accumulator shape (spec-bink-codec.md Sec5.3a), the
# 10-way block-type dispatch + case 1's 7-way sub-dispatch (Sec5.3), a
# planar YUV 4:2:0 output buffer matching the CONFIRMED 0x30-stride record
# layout (Sec5.6; spec-bink-boundary.md Sec5.2), and a viewing-only
# limited-range YUV->RGB step (NOT a codec fact - Sec5.6 itself confirms no
# such conversion exists inside the DLL). Header-only (include/sr3binkcodec/),
# same convention as include/sr3audioruntime/vorbis_container.h - no
# separate STATIC library target; consumers just need "include" on their
# path. Reuses include/sr3luahost/open_state.h's OpenValue/OpenStateError
# convention for every case whose exact pixel math is still an OPEN spec
# gap - see block_dispatch.h's own doc comments for exactly which block
# cases are fully reconstructed (block copy primary 0/9, DC fill, skip-flag,
# the dual-row-duplication raw copy primary 1 sub 6 - CLOSED 2026-10-08 per
# spec-bink-codec.md Sec5.3b) vs. an explicit OPEN hook (the two RLE/
# permutation "giant1" engines - primary 1 sub 0 and primary 3 - blocked on
# their own bit-level run/literal token format, not just coefficient
# ordering; the three real-DCT engines - primary 1 sub 2, primary 5, primary
# 7 - blocked on undumped quantization-table byte contents plus an
# undetermined AC-coefficient index mapping; the two PATTERN blends -
# primary 1 sub 5, primary 8 - blocked on an undetermined control-byte
# spatial mapping even though their blend-mask tables are now fully dumped;
# the two motion-coded cases - primary 2, primary 4 - blocked on an
# undetermined motion-vector bit-width/encoding; primary 6, never
# individually mapped by any spec pass at all).

# Synthetic regression (no real game data needed): hand-built bitstreams
# exercising the reader's own confirmed shape and the fully-reconstructed
# block cases, deterministically. Also, since 2026-10-10, the designated
# place a skip guard detects caseSelectorTablesAvailable()==false (neither
# the compiled-in dev tables nor sr3runtimedata's runtime loader supplied
# real case-selector table data) and SKIPs (exit 77) instead of asserting
# against the zeroed placeholder and failing - this test's own hardcoded
# expected output bytes are derived from the REAL table values, so it has
# no meaningful synthetic-only subset to fall back to.
add_executable(sr3binkcodec_tests tests/synthetic_bink_codec_test.cpp)
target_include_directories(sr3binkcodec_tests PRIVATE include)
target_link_libraries(sr3binkcodec_tests PRIVATE sr3binkcodec sr3runtimedata)
add_test(NAME synthetic_bink_codec_test COMMAND sr3binkcodec_tests)
if(NOT CRREISH_VERIFY_BUILD)
    set_tests_properties(synthetic_bink_codec_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Real-data regression: frame 0 of the game's own logo_thq_and_voli_hd.bik,
# extracted by tools/bink_frame0_probe.cpp per spec-bink-codec.md Sec1.5/
# Sec1.5a/Sec1.6 (tests/golden/bink_logo_frame0/frame0_raw.bin, gitignored,
# local-only). MD5-verified at test time against this task's own designated
# hash before use (see the test file's own top comment for why: "stop and
# report rather than re-deriving it from anywhere" if missing/mismatched).
# SKIPPED (exit 77) when the fixture is absent or its MD5 doesn't match,
# same convention as every other real_*_test in this repo.
add_executable(real_bink_frame0_decode_tests tests/real_bink_frame0_decode_test.cpp)
target_include_directories(real_bink_frame0_decode_tests PRIVATE include)
target_link_libraries(real_bink_frame0_decode_tests PRIVATE sr3binkcodec sr3runtimedata)
target_compile_definitions(real_bink_frame0_decode_tests PRIVATE
    BINK_FRAME0_GOLDEN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/bink_logo_frame0/frame0_raw.bin")
add_test(NAME real_bink_frame0_decode_test COMMAND real_bink_frame0_decode_tests)
if(NOT CRREISH_VERIFY_BUILD)
    set_tests_properties(real_bink_frame0_decode_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Real-data STAGE-BY-STAGE validation against Team A's own Unicorn-CPU-
# emulation capture of binkw32.dll's real decode (fix/bink-decoder-test-
# vector-validation task, 2026-10-08): spec-bink-video-test-vectors.md's 22
# real stage buffers (case-selector scratch, DC/AC coefficients, a verified
# post-IDCT block, final plane crops) vs. sr3binkcodec's own real decode of
# main_menu_bg.bik frame 0/1. tests/golden/bink_main_menu_bg/{frame0_raw.bin,
# frame1_raw.bin,vectors/*.bin} - gitignored/local-only, regenerated via
# tools/bink_frameN_extract.cpp (the two raw frames, off the real asset) and
# tools/bink_test_vector_extract.cpp (the 22 vectors, off the spec file).
# SKIPPED (exit 77) if any fixture is absent, same convention as every other
# real_*_test. See tests/real_bink_test_vector_validation_test.cpp's own top
# comment for exactly what this test checks and does NOT claim.
add_executable(real_bink_test_vector_validation_tests tests/real_bink_test_vector_validation_test.cpp)
target_include_directories(real_bink_test_vector_validation_tests PRIVATE include)
target_link_libraries(real_bink_test_vector_validation_tests PRIVATE sr3binkcodec sr3runtimedata)
target_compile_definitions(real_bink_test_vector_validation_tests PRIVATE
    BINK_MMBG_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/bink_main_menu_bg")
add_test(NAME real_bink_test_vector_validation_test COMMAND real_bink_test_vector_validation_tests)
if(NOT CRREISH_VERIFY_BUILD)
    set_tests_properties(real_bink_test_vector_validation_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Targeted real-data confirmation for the merge-group bit-consumption fix
# (commit 84820a4, fix/bink-merge-group-bit-consumption branch): unlike
# real_bink_test_vector_validation_tests above (which drives the FULL
# decodeLumaPlane() path and is the wrong harness for isolating this one
# fix), this test constructs a BitstreamReader directly at the exact cold
# starting position spec-bink-codec.md Sec5.3g's own 23-call trace is about
# (frame0_raw.bin absolute byte offset 4) and checks buildCaseSelectorTable()
# (entropy_prestage.h) 23 times in a row against that section's own
# per-call bit-position table, then the following LEN field (expected 528 at
# bit 588, not the pre-fix 0 at bit 580). Same gitignored/local-only fixture
# as real_bink_test_vector_validation_tests (tests/golden/bink_main_menu_bg/
# frame0_raw.bin) and the same SKIPPED (exit 77) convention.
add_executable(real_bink_merge_group_trace_tests tests/real_bink_merge_group_trace_test.cpp)
target_include_directories(real_bink_merge_group_trace_tests PRIVATE include)
target_link_libraries(real_bink_merge_group_trace_tests PRIVATE sr3binkcodec)
target_compile_definitions(real_bink_merge_group_trace_tests PRIVATE
    BINK_MMBG_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/bink_main_menu_bg")
add_test(NAME real_bink_merge_group_trace_test COMMAND real_bink_merge_group_trace_tests)
if(NOT CRREISH_VERIFY_BUILD)
    set_tests_properties(real_bink_merge_group_trace_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Dedicated coverage for frame_decoder.h's plane sub-chunk span-field
# navigation (PlaneSubChunkSpan/readPlaneSubChunkSpan()/
# nextPlaneSubChunkPosition(), fix/bink-merge-group-bit-consumption's own
# follow-up task, spec-bink-codec.md Sec5.3g). Two parts in one binary: a
# SYNTHETIC deterministic check (always runs, no real data needed) plus a
# REAL-DATA check against the same gitignored/local-only frame0_raw.bin
# fixture real_bink_merge_group_trace_tests above uses - SKIPPED (exit 77)
# for the real-data part only if that fixture is absent/mismatched (the
# synthetic part's own failures are never masked by that skip - see the
# test's own main()).
add_executable(bink_plane_subchunk_span_tests tests/bink_plane_subchunk_span_test.cpp)
target_include_directories(bink_plane_subchunk_span_tests PRIVATE include)
target_link_libraries(bink_plane_subchunk_span_tests PRIVATE sr3binkcodec)
target_compile_definitions(bink_plane_subchunk_span_tests PRIVATE
    BINK_MMBG_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/bink_main_menu_bg")
add_test(NAME bink_plane_subchunk_span_test COMMAND bink_plane_subchunk_span_tests)
if(NOT CRREISH_VERIFY_BUILD)
    set_tests_properties(bink_plane_subchunk_span_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Driver tool (NOT a ctest - a manual-run visual-inspection aid, same
# convention as tools/prototype_lit_car.cpp etc.): runs the decoder
# skeleton against a real extracted frame and writes a viewing-only PNG via
# sr3render's own existing PNG writer (reused rather than adding a new
# image dependency, per this task's own instruction). Windows-only, same
# d3d11-driven guard as sr3render itself.
if(WIN32)
    add_executable(bink_frame0_decode tools/bink_frame0_decode.cpp)
    target_include_directories(bink_frame0_decode PRIVATE include)
    target_link_libraries(bink_frame0_decode PRIVATE sr3render sr3binkcodec)
endif()

# libFuzzer harnesses + seed capture (fuzz/fuzz.cmake). Opt-in, Clang only.
option(CRREISH_FUZZ "Build the libFuzzer harnesses (Clang)" OFF)
if(CRREISH_FUZZ)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "CRREISH_FUZZ needs Clang (libFuzzer)")
    endif()
    include(fuzz/fuzz.cmake)
endif()

# MSVC-native libFuzzer+ASan harnesses (fuzz/msvc/fuzz_msvc.cmake, this
# task, 2026-10-07). Opt-in, OFF by default, never built/run by a normal
# `cmake --build` / `ctest` pass. A separate, additive sibling to
# CRREISH_FUZZ above (that one is Clang-only; the normal build here is
# MSVC/Visual Studio, so this path needs no separate Clang toolchain) - see
# fuzz/msvc/fuzz_msvc.cmake's own top comment for the toolchain verification
# (cl.exe's own `/fsanitize=address /fsanitize=fuzzer`, confirmed to work on
# this machine's MSVC 14.44.35207 toolset) and fuzz/msvc/README.md for the
# target list and how to run it.
option(CRREISH_FUZZ_MSVC "Build MSVC-native /fsanitize=address,fuzzer libFuzzer harnesses" OFF)
if(CRREISH_FUZZ_MSVC)
    if(NOT MSVC)
        message(FATAL_ERROR "CRREISH_FUZZ_MSVC needs MSVC (cl.exe's native /fsanitize=fuzzer)")
    endif()
    include(fuzz/msvc/fuzz_msvc.cmake)
endif()

# Bridge-result diff tool (tools/bridge_diff.py) unit test, when Python is available.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME bridge_diff_py COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/bridge_diff_test.py)
    add_test(NAME lua_host_run_integration_py
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/lua_host_run_integration_test.py
                     $<TARGET_FILE:lua_host_run_fixture> $<TARGET_FILE:lua_host_run>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/lua_all_registered_1490_tagged.txt
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/bridge_diff.py)
endif()

# Real-data validation harnesses (tools/validation/*.cpp), plus two loose
# tools, as CMake targets - the portable equivalent of
# tools/validation/build_one.bat, which links every harness against every
# reader library. Cloud phase 2026-09-30: lets the PC bridge build them by
# target name and keeps them compiling in Linux CI. Harnesses that need D3D
# (d3dcompiler/d3d11) or sr3render stay Windows-only.
option(CRREISH_VALIDATION_TOOLS "Build tools/validation harnesses" ON)
if(CRREISH_VALIDATION_TOOLS)
    set(CRREISH_ALL_READER_LIBS vpp_container sr3save sr3asm sr3fxo sr3effects sr3xtbl
        sr3tables_trafficai sr3tables_weapons sr3tables_progression sr3tables_environment
        sr3tables_animation sr3tables_audio_radio sr3tables_ui_controls sr3tables_vehicle_world
        sr3tables_diversions sr3tables_customization sr3d3d9bc sr3vehicleinfo sr3customization
        sr3texture sr3geometry sr3anim sr3morph sr3foliage sr3conversation sr3cutscene sr3mesh
        sr3vehicle sr3rig sr3zone sr3audio sr3clmesh sr3tree sr3lua sr3luahost sr3vintdoc)
    set(CRREISH_WINDOWS_ONLY_VALIDATION d3dctest_semantics d3dctest_semantics_sm4
        validate_d3d9bc_hlsl_population validate_d3d9bc_hlsl_sm4_population validate_peg_population
        golden_scene_check) # drives the D3D11 sr3_viewer.exe
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

    # golden_scene_check was only ever a standalone executable above (built,
    # never registered as a ctest test) - this is the B2 task's own fold-in:
    # a real `ctest` entry for the D3D11 golden-scene regression harness,
    # labelled "golden" (see this task's own LABELS block further below) and
    # RESOURCE_LOCKed on the one D3D11 device every golden-scene/GPU test
    # here contends for, so `ctest -j N` serializes GPU work instead of
    # racing several processes for one adapter while everything else still
    # parallelizes freely. `--root ${CMAKE_CURRENT_SOURCE_DIR}` overrides
    # golden_scene_check.cpp's own hardcoded default root (its main()
    # defaults to the literal path "D:\Project Crreish\TEAM B" - the
    # orchestrator's own main checkout, NOT necessarily this configure's
    # source tree) so this test checks THIS worktree's own tests/golden/,
    # never a different worktree's - needed because this project's whole
    # agent fleet configures from many concurrent worktrees (see the
    # sccache section's own comment above for the same reasoning). Every
    # scene legitimately SKIPs (not a failure) when tests/golden/ - gitignored,
    # "regenerated locally" per this repo's own .gitignore comment - is
    # absent from a given checkout, exactly like the existing real_*_test
    # SKIP_RETURN_CODE 77 convention; golden_scene_check's own main() already
    # treats an all-SKIP run as a vacuous pass (failures=0), not touched here.
    if(WIN32)
        # --bin-dir (this task, 2026-10-08 - "fold tests/golden/build.bat into
        # CMake properly"): golden_scene_check.cpp's own checkXScene()
        # functions used to hardcode tests/golden/_bin/<exe>, the directory
        # ONLY build.bat's cl.exe/link.exe steps ever populated - a real
        # `cmake --build` instead puts sr3_viewer.exe/tree_baseline_render.exe/
        # prototype_lit_clmesh_tower2.exe (every tool a scene below actually
        # runs) in this generator's own per-config output directory, the SAME
        # one golden_scene_check.exe itself lands in. $<TARGET_FILE_DIR:
        # sr3_viewer> names that directory generically (works for the VS
        # generator's <config>/ subdirectory and for a single-config Ninja
        # build alike) without hardcoding either shape. This is what makes a
        # genuinely fresh `cmake --build` + `ctest` actually run the real
        # scene checks instead of every one FAILing "exe not found" (the
        # gap build.bat's own removal would otherwise reopen).
        add_test(NAME golden_scene_check
                 COMMAND golden_scene_check --root ${CMAKE_CURRENT_SOURCE_DIR}
                         --data-root ${CRREISH_GAME_CACHE_DIR}
                         --bin-dir $<TARGET_FILE_DIR:sr3_viewer>)
        set_tests_properties(golden_scene_check PROPERTIES
            LABELS "golden"
            RESOURCE_LOCK "sr3_gpu_device")
        # Belt-and-suspenders dependency (this task): ctest does not build
        # anything itself - a plain `cmake --build` already builds every
        # target below since none carry EXCLUDE_FROM_ALL, but
        # add_dependencies() on the test's own driving executable makes the
        # "golden_scene_check needs these built first" relationship explicit
        # in the CMake graph itself, not just an implicit consequence of the
        # default ALL target, matching this task's own "properly DEPEND on
        # them being built first" instruction.
        add_dependencies(golden_scene_check sr3_viewer tree_baseline_render prototype_lit_clmesh_tower2 ambient_population_demo)
    endif()
endif()

# --- CTest labels + parallel-safety properties (B2 task, 2026-10-07) ------
# fast / golden / real-data per this task's own convention: synthetic_* ->
# fast (in-memory unit tests over the readers, no real shipped archive, no
# golden baseline); anything touching tests/golden/ or doing an MD5/byte-
# exact determinism check -> golden (today that's only golden_scene_check,
# labelled directly above, right next to its own add_test()); anything
# reading a real shipped game archive -> real-data (the existing real_*_test
# family plus the two Python-driven tests, bridge_diff_py and
# lua_host_run_integration_py, which both exercise tools/lua_host_run.cpp
# against the real registered-natives list).
#
# One set_tests_properties() call per label (not 79 separate ones scattered
# through the file) so this whole task is a single additive block that
# touches no existing add_test() line - every other parallel agent's own
# edits to those lines this round are unaffected.
#
# ctest -j parallel-safety: audited directly (grepped+read every tests/*.cpp
# and tests/*.py for fixed/shared paths, ports, or other mutable shared
# state two concurrent tests could collide on) - no fixed shared-write path,
# no fixed port, found anywhere. Every test that writes a file already uses
# either a per-invocation-unique tempfile.TemporaryDirectory() (both Python
# tests, and tests/lua_host_run_fixture.cpp, which only ever writes under a
# directory ITS caller - lua_host_run_integration_test.py - chose uniquely)
# or a self-named file immediately read back and removed within the same
# single-threaded test binary (tests/synthetic_luahost_test.cpp's own
# "synthetic_luahost_test_taglist.tmp.txt"). The five real_*_test binaries
# only ever READ from CRREISH_GAME_CACHE_DIR, never write - concurrent reads
# of the same shipped archive are safe. No test besides golden_scene_check
# touches the D3D11 device, so no other RESOURCE_LOCK is needed today; a
# future GPU-touching ctest test should reuse the same "sr3_gpu_device"
# resource-lock name golden_scene_check uses above.
#
# UPDATE 2026-10-08 (spec-save-format.md Sec15 wiring): game_autosave/
# mission_autosave now perform a REAL file write (sr3save_00.sr3s_pc, via
# EngineState::saveGameDirectory()) once their own existing gates pass, so
# synthetic_luahost_batch2728_test/synthetic_luahost_wave2_w22_test (which
# already drove those gates to "passed" before this change, just to count a
# trigger) gained a real filesystem side effect. Still parallel-safe: every
# EngineState defaults saveGameDirectory() to the OS temp directory
# qualified by THIS PROCESS's own id (save_game_bridge.h's
# defaultSaveGameDirectory()) - ctest runs each test binary as its own
# process, so no two concurrent ctest invocations, nor these two pre-
# existing tests and the new synthetic_save_game_bridge_test below, ever
# resolve to the same path. synthetic_save_game_bridge_test's own file-
# writing blocks go further and override saveGameDirectory() to a per-call,
# timestamp-qualified sandbox directory it removes at the end of each block
# (same "per-invocation-unique" convention as the two Python tests above).
set_tests_properties(
    synthetic_archive_test
    synthetic_content_validation_test
    synthetic_save_test
    synthetic_asm_test
    synthetic_fxo_test
    synthetic_effects_test
    synthetic_xtbl_test
    synthetic_tables_trafficai_test
    synthetic_tables_weapons_test
    synthetic_tables_progression_test
    synthetic_tables_environment_test
    synthetic_tables_animation_test
    synthetic_tables_audio_radio_test
    synthetic_tables_ui_controls_test
    synthetic_player_runtime_test
    synthetic_physics_runtime_test
    synthetic_tables_vehicle_world_test
    synthetic_tables_diversions_test
    synthetic_tables_customization_test
    synthetic_d3d9bc_test
    synthetic_d3d9bc_ctab_test
    synthetic_d3d9bc_hlsl_test
    synthetic_vehicleinfo_test
    synthetic_customization_test
    synthetic_texture_test
    synthetic_ccmesh_test
    synthetic_material_binding_test
    synthetic_character_matlib_test
    synthetic_anim_test
    synthetic_anim_payload_test
    synthetic_anim_runtime_test
    synthetic_anim_node_graph_test
    synthetic_anim_node_graph_runtime_test
    synthetic_player_anim_bridge_test
    synthetic_morph_test
    synthetic_foliage_test
    synthetic_ctdg_test
    synthetic_csc_test
    synthetic_cvtf_test
    synthetic_mesh_vertex_test
    synthetic_rig_test
    synthetic_pose_test
    synthetic_multibone_pose_test
    synthetic_bone_palette_test
    synthetic_zone_test
    synthetic_media_bank_test
    synthetic_hirc_media_lookup_test
    synthetic_audio_runtime_test
    synthetic_clmesh_test
    synthetic_tree_test
    synthetic_lua_test
    synthetic_luahost_test
    synthetic_luahost_bare_globals_test
    synthetic_luahost_roster_test
    synthetic_luahost_unified_registry_test
    synthetic_luahost_refusal_stress_test
    synthetic_luahost_cutscene_test
    synthetic_luahost_cutscene_camera_script_test
    synthetic_luahost_batch2728_test
    synthetic_luahost_tranche03_test
    synthetic_luahost_tranche03_batch7_test
    synthetic_luahost_wave2_w21_test
    synthetic_luahost_follower_sentinel_test
    synthetic_luahost_batch31_32_test
    synthetic_luahost_tranche060708_test
    synthetic_luahost_sec44_46_test
    synthetic_luahost_tranche16_test
    synthetic_luahost_tranche_realhits_test
    synthetic_luahost_player_controls_test
    synthetic_luahost_native_batch1007_test
    synthetic_luahost_trigger_mission_cluster_test
    synthetic_save_game_bridge_test
    synthetic_player_movement_test
    synthetic_player_camera_test
    synthetic_player_vehicle_occupancy_test
    synthetic_seated_ride_pose_test
    synthetic_tables_cutscene_test
    synthetic_vintdoc_test
    synthetic_vintdoc_document_test
    synthetic_vint_property_test
    synthetic_vint_font_test
    synthetic_mission_host_live_bridge_test
    synthetic_le_strings_test
    synthetic_wwise_vorbis_setup_test
    synthetic_bink_codec_test
    synthetic_tod_lighting_test
    synthetic_tod_clock_test
    PROPERTIES LABELS "fast")

set_tests_properties(
    real_vintdoc_document_test
    real_zone_objects_test
    real_vint_font_test
    real_le_strings_test
    real_anim_control_parameters_test
    real_anim_node_graph_test
    real_hwy_ramp_props_test
    real_vorbis_decode_test
    real_vehicle_engine_torque_crossmap_test
    real_vehicle_interaction_hide_occupant_test
    real_seated_ride_pose_test
    real_bink_frame0_decode_test
    real_bink_test_vector_validation_test
    real_bink_merge_group_trace_test
    PROPERTIES LABELS "real-data")

if(Python3_Interpreter_FOUND)
    set_tests_properties(
        bridge_diff_py
        lua_host_run_integration_py
        PROPERTIES LABELS "real-data")
endif()

# AI runtime (spec-ai-runtime.md): A0 the 44-mode machine and the mode
# request/apply/transition/acceptance protocol (Sec2-Sec4, Sec6), A1 the
# decision step/gate/chooser/17 goal tests (Sec9), A2 perception's write
# side - 22 lists, producers, the query (Sec10), A3 the action routines
# (Sec11). Pure machinery with injectable geometry/weapon/vehicle/navmesh
# seams (actions.h's ActionWorldFacts); depends only on the header-only
# sr3luahost/open_state.h (same include root) for the OPEN-state discipline.
#
# The Navpoint/cover_node movement-target-selection unit (Sec15,
# include/sr3airuntime/navpoint_selector.h, added 2026-10-08) is
# header-only (inline functions, same house pattern as common.h's
# Vec3/EngineTimer - no new .cpp needed), but it is the first part of this
# library to consume real zone-placement data, so sr3airuntime now links
# sr3zone PUBLIC (sr3zone::ObjectRecord and its position/navpoint_flags
# property accessors, spec-zone-data-format.md Sec11).
#
# line_of_sight.h (Sec12.4 / spec-physics-runtime.md Sec8.8-Sec8.11,
# 2026-10-08 castRay/shape-cast follow-up task) is likewise header-only,
# but it reuses sr3player::RayVsTriangle3D (character_collision.h) as its
# own real ray-vs-triangle primitive rather than duplicating that math -
# see that header's own top doc comment for why - so sr3airuntime now also
# links sr3player PUBLIC. sr3player is defined above (this file's earlier
# "Player runtime" section) so it already exists here, same ordering
# discipline this file's own sr3physics/sr3vehicleinfo comments already
# document for forward-referenced targets.
#
# The cover_node selection unit (Sec15.7, include/sr3airuntime/
# cover_node_selector.h, added 2026-10-08) is also header-only. Its step-6
# path-reachability stand-in reuses sr3player's real swept-circle-vs-
# triangle collision primitive (sr3player::SweepCircleVsTriangleXZ,
# include/sr3player/character_collision.h) rather than reinventing that
# math, so sr3airuntime now also links sr3player PUBLIC (same target, both
# units cite it independently - one PUBLIC link, not two).
add_library(sr3airuntime STATIC
    src/ai_mode_table.cpp
    src/ai_perception.cpp
    src/ai_decision.cpp
    src/ai_actions.cpp
)
target_include_directories(sr3airuntime PUBLIC include)
target_link_libraries(sr3airuntime PUBLIC sr3zone sr3player)

add_executable(sr3airuntime_tests tests/synthetic_ai_runtime_test.cpp)
target_link_libraries(sr3airuntime_tests PRIVATE sr3airuntime)
add_test(NAME synthetic_ai_runtime_test COMMAND sr3airuntime_tests)

add_executable(sr3airuntime_navpoint_selector_tests tests/synthetic_navpoint_selector_test.cpp)
target_link_libraries(sr3airuntime_navpoint_selector_tests PRIVATE sr3airuntime)
add_test(NAME synthetic_navpoint_selector_test COMMAND sr3airuntime_navpoint_selector_tests)

add_executable(sr3airuntime_line_of_sight_tests tests/synthetic_line_of_sight_test.cpp)
target_link_libraries(sr3airuntime_line_of_sight_tests PRIVATE sr3airuntime)
add_test(NAME synthetic_line_of_sight_test COMMAND sr3airuntime_line_of_sight_tests)

add_executable(sr3airuntime_cover_node_selector_tests tests/synthetic_cover_node_selector_test.cpp)
target_link_libraries(sr3airuntime_cover_node_selector_tests PRIVATE sr3airuntime)
add_test(NAME synthetic_cover_node_selector_test COMMAND sr3airuntime_cover_node_selector_tests)

# Ambient (distant/LOD) pedestrian+vehicle spawn-despawn unit (Sec17,
# include/sr3airuntime/ambient_population.h, added 2026-10-08). Header-only,
# same house pattern as navpoint_selector.h/cover_node_selector.h above - no
# new .cpp, and no new sr3airuntime dependency (it only reuses
# navpoint_selector.h's own DeterministicRng/pickUniform seam, already
# linked in via this library's own headers).
add_executable(sr3airuntime_ambient_population_tests tests/synthetic_ambient_population_test.cpp)
target_link_libraries(sr3airuntime_ambient_population_tests PRIVATE sr3airuntime)
add_test(NAME synthetic_ambient_population_test COMMAND sr3airuntime_ambient_population_tests)

# Near-field (fully-simulated) ambient-vehicle maintenance unit (Sec17.4c,
# include/sr3airuntime/near_field_vehicle_population.h, added 2026-10-08) -
# a genuinely SEPARATE pool/module from the distant tier above (see that
# file's own top comment), sibling test file following the exact same
# shape. Header-only, same house pattern - no new .cpp, reuses
# navpoint_selector.h's DeterministicRng/pickUniform, common.h's
# EngineTimer, and ambient_population.h's AmbientToggleState/ToggleCategory
# (all already reachable via this library's own headers).
add_executable(sr3airuntime_near_field_vehicle_population_tests tests/synthetic_near_field_vehicle_population_test.cpp)
target_link_libraries(sr3airuntime_near_field_vehicle_population_tests PRIVATE sr3airuntime)
add_test(NAME synthetic_near_field_vehicle_population_test COMMAND sr3airuntime_near_field_vehicle_population_tests)

# Near-field (fully-simulated) on-foot ambient-pedestrian unit (spec-ai-
# runtime.md Sec20, include/sr3airuntime/near_field_ped_population.h, added
# 2026-10-09) - the ped sibling of near_field_vehicle_population.h above:
# sidewalk-graph spawn (length-weighted, not the vehicle side's flat-uniform
# pick), Idle-mode sidewalk/wander sub-behaviours, action_node_npcs.xtbl-
# driven Idle cooldown data (sr3airuntime stays decoupled from
# sr3tables_trafficai - a caller feeds the post-conversion ms values, same
# split ambient_population.h's own VehicleTrafficType struct documents), and
# the CIVILIAN-team-byte-gated gunfire flee/cower chain, including the
# regression guard that Panic (mode 24) is never requested. Header-only,
# same house pattern - no new .cpp; reuses navpoint_selector.h's
# DeterministicRng/pickUniform, ambient_population.h's AmbientToggleState/
# ToggleCategory/PickWeightedIndex, and near_field_vehicle_population.h's
# own shared 150-human headroom ceiling (all already reachable via this
# library's own headers).
add_executable(sr3airuntime_near_field_ped_population_tests tests/synthetic_near_field_ped_population_test.cpp)
target_link_libraries(sr3airuntime_near_field_ped_population_tests PRIVATE sr3airuntime)
add_test(NAME synthetic_near_field_ped_population_test COMMAND sr3airuntime_near_field_ped_population_tests)

# Traffic-AI lane-following unit (spec-ai-runtime.md Sec18,
# include/sr3airuntime/traffic_lane_follower.h, added 2026-10-08) - CHOSEN
# graph-hopping target-point/junction-choice logic over the real,
# CONFIRMED-structure sr3zone::TrafficRecord (Sec13.2, sr3zone/
# traffic_graph.h) already pulled in transitively via sr3airuntime's own
# PUBLIC link to sr3zone above. Header-only (reuses navpoint_selector.h's
# DeterministicRng/MakePickUniform - same RNG-seam idiom, not a second
# copy), so no new .cpp is added to the add_library() list above - same
# "header-only, no new .cpp needed" note that file's own comment already
# gives for navpoint_selector.h/cover_node_selector.h.
add_executable(sr3airuntime_traffic_lane_follower_tests tests/synthetic_traffic_lane_follower_test.cpp)
target_link_libraries(sr3airuntime_traffic_lane_follower_tests PRIVATE sr3airuntime)
add_test(NAME synthetic_traffic_lane_follower_test COMMAND sr3airuntime_traffic_lane_follower_tests)

# Police/roadblock spawn MECHANISM unit (spec-ai-runtime.md Sec19.7,
# include/sr3airuntime/police_roadblock_spawn.h, added 2026-10-08) - a
# THIRD, sibling per-frame population system riding the same 0x0090faf0
# dispatcher ambient_population.h's own driver uses, structurally separate
# from both that system AND from 0x00606b80's notoriety-level-change
# dispatcher (Sec19.4). Header-only, same house pattern as
# ambient_population.h/traffic_lane_follower.h above - no new .cpp, and no
# new sr3airuntime dependency (only common.h's Vec3/Dot/Length).
add_executable(sr3airuntime_police_roadblock_spawn_tests tests/synthetic_police_roadblock_spawn_test.cpp)
target_link_libraries(sr3airuntime_police_roadblock_spawn_tests PRIVATE sr3airuntime)
add_test(NAME synthetic_police_roadblock_spawn_test COMMAND sr3airuntime_police_roadblock_spawn_tests)

# Console-only demo for the same unit (tools/police_roadblock_demo.cpp) -
# deterministic stdout trace proving the throttle/anchor/spike-strip
# behaviours; see that file's own top comment for why this is a plain
# console trace rather than a PNG golden scene. sr3airuntime only (header-
# only consumer, no sr3render/D3D11 needed - no visual capture here).
add_executable(police_roadblock_demo tools/police_roadblock_demo.cpp)
target_link_libraries(police_roadblock_demo PRIVATE sr3airuntime)

# Notoriety tracking MECHANISM unit (spec-ai-runtime.md Sec19.1-19.4,
# include/sr3airuntime/notoriety.h, added 2026-10-09) - the team-relationship
# hostility-flip-at-the-zero-boundary (a 4th writer of the already-documented
# team-relationship matrix, Sec12.7/Sec19.4), crimes-to-points/decay rules
# (real shipped notoriety.xtbl/notoriety_levels.xtbl numbers, spec-tables-
# progression.md Sec14.6), the clear-to-zero-only roster/timer sequence, and
# the TRACK-indexed (not level-indexed) heat-code table. Header-only, same
# house pattern as police_roadblock_spawn.h above - no new .cpp, and no new
# sr3airuntime dependency (only common.h's EngineTimer). Deliberately
# separate from, and NOT wired to, police_roadblock_spawn.h - see
# notoriety.h's own "THE REMAINING OPEN GATE" note for exactly why.
add_executable(sr3airuntime_notoriety_tests tests/synthetic_notoriety_test.cpp)
target_link_libraries(sr3airuntime_notoriety_tests PRIVATE sr3airuntime)
add_test(NAME synthetic_notoriety_test COMMAND sr3airuntime_notoriety_tests)

# Steamworks call boundary (spec-steamworks-boundary.md): the GAME side's
# own call contract into the 17 imported Steamworks entry points - the
# SteamAPI_Init silent-skip-on-failure path (Sec2.2), the LIFO shutdown-hook
# registry (Sec2.3), the mandatory per-frame RunCallbacks pump (Sec3), and
# the request-queue/lobby-callback/cloud-storage on-demand clusters (Sec4)
# that have enough CONFIRMED detail to model. NOT a Steamworks SDK
# integration (explicitly out of scope per the spec's own Sec0 - the real
# SDK links against this exact contract once the user downloads it);
# SteamBackend/NullSteamBackend follows the same real-contract/injected-
# null-backend shape as sr3audioruntime's WwiseBackend/NullWwiseBackend.
add_library(sr3steam STATIC
    src/steam_backend.cpp
    src/steam_boundary.cpp
)
target_include_directories(sr3steam PUBLIC include)

add_executable(sr3steam_tests tests/synthetic_sr3steam_test.cpp)
target_link_libraries(sr3steam_tests PRIVATE sr3steam)
add_test(NAME synthetic_sr3steam_test COMMAND sr3steam_tests)

# Agora Games "Hydra SDK" / GDS telemetry call boundary
# (spec-agora-hydra-boundary.md): the GAME side's own call contract into the
# 31-export GDS_value_* tagged-value type (Sec1), the init/shutdown wrapper
# pair with its one-shot "active" flag that is never rolled back on failure
# (Sec2/Sec3 - the one headline divergence from sr3steam's own retry-after-
# failure guard), the shared queue wrapper and its two report shapes
# (Sec4), and the HTTP response status-code dispatch (Sec5). Per the task's
# own explicit scope decision: boundary only, null backend - the real
# servers (sr3.gds.thqconnect.com, sr3.hydra.agoragames.com) are dead, so
# unlike sr3steam/sr3physics/sr3audioruntime there is no real backend to
# build toward later; NullAgoraBackend's defaults are the permanent
# behaviour. AgoraBackend/NullAgoraBackend follows the same real-contract/
# injected-null-backend shape as sr3steam's SteamBackend/NullSteamBackend.
add_library(sr3agora STATIC
    src/agora_backend.cpp
    src/agora_boundary.cpp
)
target_include_directories(sr3agora PUBLIC include)

add_executable(sr3agora_tests tests/synthetic_sr3agora_test.cpp)
target_link_libraries(sr3agora_tests PRIVATE sr3agora)
add_test(NAME synthetic_sr3agora_test COMMAND sr3agora_tests)

# Vehicle-enter eligibility gate (2026-10-09): spec-player-runtime.md
# Sec7.4a, 0x00b0ca60 - implemented as vehicleEnterEligibilityGate() in
# src/lua_spec_confirmed_stubs_b15_wave2_core_dispatchers.cpp. Exercises
# each of the 9 refusal paths and the success tail through vehicle_enter_do/
# vehicle_enter_guardian_angel.
add_executable(sr3luahost_vehicle_enter_eligibility_gate_tests tests/synthetic_luahost_vehicle_enter_eligibility_gate_test.cpp)
target_link_libraries(sr3luahost_vehicle_enter_eligibility_gate_tests PRIVATE sr3luahost)
add_test(NAME synthetic_luahost_vehicle_enter_eligibility_gate_test COMMAND sr3luahost_vehicle_enter_eligibility_gate_tests)
