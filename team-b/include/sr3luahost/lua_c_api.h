// The ONLY include point in this project for the real, vendored,
// unmodified Lua 5.1.5 C API (third_party/lua51/src/{lua,lauxlib,lualib}.h -
// the real reference implementation, out of clean-room scope per this
// project's own established convention for public third-party libraries,
// same footing as third_party/zlib and exactly what spec-lua-bindings.md
// Sec1/Sec13 itself already treats the stock Lua 5.1 VM/stdlib as: "public,
// unmodified, not proprietary" - never a disassembly finding).
//
// LUA IS COMPILED AS C++ (cloud phase 2026-10-01, manager ruling after
// bridge job jklk): third_party/lua51/CMakeLists.txt builds the unmodified
// Lua 5.1.5 sources with the C++ compiler, so luaconf.h's own documented
// `__cplusplus` branch makes LUAI_THROW/LUAI_TRY a C++ throw / try-catch
// instead of longjmp/setjmp, and a Lua error unwinds C++ frames with their
// destructors run. The API therefore has C++ linkage and is included here
// WITHOUT `extern "C"` (the wrapper this file used to hold would now fail to
// link). The rule that no Lua error is raised across a C++ frame holding
// state (lua_spec_confirmed_stubs.cpp, "NO LUA ERROR MAY CROSS A C++ FRAME")
// stays, as a second line of defence. Every other file still includes the
// Lua API only through this header.
#pragma once

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
