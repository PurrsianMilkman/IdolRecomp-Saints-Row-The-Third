# CMake build wrapper for vendored, real, unmodified Lua 5.1.5 (lua.org's
# own official release: https://www.lua.org/ftp/lua-5.1.5.tar.gz, retrieved
# 2026-09-29). Everything else under this directory (src/, doc/, etc/,
# test/, COPYRIGHT, README, HISTORY, INSTALL, the upstream Makefile) is the
# untouched upstream distribution tree - not a single .c/.h file has been
# edited. This CMakeLists.txt is the only file in this directory Team B
# wrote itself: upstream Lua ships a plain Unix `Makefile`
# (src/Makefile), not a CMake build, so a small CMake wrapper is needed to
# fold it into this project's existing CMake build the same way
# third_party/zlib already is (that vendor's CMakeLists.txt happens to be
# upstream-shipped; Lua's is not, hence this file).
#
# Source list is copied directly from the real upstream src/Makefile's own
# CORE_O + LIB_O object lists (read 2026-09-29) - the actual Lua 5.1
# interpreter/compiler/standard-library core, exactly as lua.org packages
# it. Deliberately EXCLUDED, matching upstream's own LUA_A ("liblua.a")
# target rather than its LUA_T/LUAC_T targets: lua.c (the standalone `lua`
# CLI's own main()) and luac.c/print.c (the standalone `luac` bytecode
# compiler) - neither is needed to EMBED Lua as a library, which is this
# project's own scope (spec-lua-bindings.md Sec1/Sec13: the engine embeds
# Lua 5.1 as a library, it is not shelling out to a `lua`/`luac` binary).
#
# LUA_BUILD_AS_DLL is deliberately left undefined (luaconf.h: LUA_API then
# expands to plain `extern`), matching a static-library embed, not a DLL.
project(lua51 C)

add_library(lua51 STATIC
    src/lapi.c
    src/lcode.c
    src/ldebug.c
    src/ldo.c
    src/ldump.c
    src/lfunc.c
    src/lgc.c
    src/llex.c
    src/lmem.c
    src/lobject.c
    src/lopcodes.c
    src/lparser.c
    src/lstate.c
    src/lstring.c
    src/ltable.c
    src/ltm.c
    src/lundump.c
    src/lvm.c
    src/lzio.c
    src/lauxlib.c
    src/lbaselib.c
    src/ldblib.c
    src/liolib.c
    src/lmathlib.c
    src/loslib.c
    src/ltablib.c
    src/lstrlib.c
    src/loadlib.c
    src/linit.c
)
target_include_directories(lua51 PUBLIC src)

if(MSVC)
    # Upstream Lua 5.1 assumes a C89/C99 Unix toolchain and triggers MSVC's
    # "insecure CRT" deprecation warnings (fopen/sprintf/getenv/strcpy/...)
    # and a handful of benign signed/unsigned and unreferenced-parameter
    # warnings under /W4 - all in unmodified upstream code, not anything
    # this project wrote. Silenced for this target only, exactly as narrow
    # as the zlib precedent already sets (this project's own /W4 stays on
    # everywhere else).
    target_compile_definitions(lua51 PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(lua51 PRIVATE /wd4996 /wd4244 /wd4100 /wd4702)
endif()
