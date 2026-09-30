// The ONLY include point in this project for the real, vendored,
// unmodified Lua 5.1.5 C API (third_party/lua51/src/{lua,lauxlib,lualib}.h -
// the real reference implementation, out of clean-room scope per this
// project's own established convention for public third-party libraries,
// same footing as third_party/zlib and exactly what spec-lua-bindings.md
// Sec1/Sec13 itself already treats the stock Lua 5.1 VM/stdlib as: "public,
// unmodified, not proprietary" - never a disassembly finding).
//
// WHY THIS WRAPPER EXISTS: Lua 5.1's own public headers do not guard
// themselves with `extern "C" { ... }` (that was only added in later Lua
// versions) - a C++ translation unit that includes them directly gets C++
// name-mangled declarations, which will not link against the C object code
// third_party/lua51 actually compiles. Every other header/source file in
// this project includes the real Lua API ONLY through this file, never
// directly, so this `extern "C"` wrapping happens exactly once.
#pragma once

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
