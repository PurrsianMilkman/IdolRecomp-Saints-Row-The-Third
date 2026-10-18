#pragma once

// Platform / standard-library setup shared by every sr3_viewer translation unit
// (split out of the former single tools/sr3_viewer.cpp). Must be included FIRST
// in every viewer_*.cpp/.h so each TU sees the same macro setup.

#include <cstdio>
#include <cstdlib>
#include <direct.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// NOMINMAX: without this, <windows.h>'s own bare min/max macros silently
// rewrite every later `std::min(...)`/`std::max(...)` call in the translation
// unit into broken syntax (see the original sr3_viewer.cpp history,
// playtest-nearfield-traffic-roadblock, 2026-10-09). Safe: no code here relies
// on the Windows macros.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>

#include <cstddef>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <d3d11shader.h>
#include <d3dcompiler.h>

#include <cfloat>
#include <functional>
#include <optional>

#pragma comment(lib, "d3dcompiler.lib")
