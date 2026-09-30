@echo off
REM Builds tools/tree_baseline_render.cpp, tools/sr3_viewer.cpp and
REM tools/golden_scene_check.cpp fresh, from real project source (not from
REM build_verify/*.obj - this script intentionally never reads or writes
REM anything under build_verify/, see tools/golden_scene_check.cpp's own
REM header comment for why: it was written while a different task was
REM concurrently editing tools/sr3_viewer.cpp and, plausibly, CMakeLists.txt's
REM linkage for it, so this stays a standalone cl.exe build into
REM tests/golden/_bin/ rather than a new CMake target. A future session with
REM no such race in flight can fold all three into CMakeLists.txt properly
REM instead (add_executable + target_link_libraries against the existing
REM sr3render/sr3tree/etc. targets, same shape as sr3_viewer's own CMake
REM entry) and delete this script.
REM
REM sr3_viewer.cpp is built here (unmodified - tools/sr3_viewer.cpp itself is
REM never edited by this script) because build_verify/sr3_viewer.exe predates
REM this session's `vehicle` command and does not recognise it ("unknown
REM command: vehicle", confirmed directly) - the vehicle_genki/vehicle_standard
REM golden scenes need a binary that actually has it.
REM
REM Run from anywhere; it cd's to the real paths itself. Output:
REM   tests\golden\_bin\tree_baseline_render.exe
REM   tests\golden\_bin\prototype_lit_clmesh_tower.exe
REM   tests\golden\_bin\prototype_lit_clmesh_tower2.exe
REM   tests\golden\_bin\sr3_viewer.exe
REM   tests\golden\_bin\golden_scene_check.exe
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 (echo VCVARS_FAILED & exit /b 1)

set PROJ=D:\Project Crreish\TEAM B
set OUT=%PROJ%\tests\golden\_bin
if not exist "%OUT%" mkdir "%OUT%"
set ZOUT=%OUT%\zlib_obj
if not exist "%ZOUT%" mkdir "%ZOUT%"

echo === zlib ===
REM third_party\zlib itself ships only zconf.h.cmakein/zconf.h.included, not a
REM real zconf.h (that's a generated config header, not hand-maintained) - the
REM second /I below points at the real zconf.h the project's own CMake
REM configure already generated under build\third_party\zlib\ (content-
REM identical to zconf.h.included plus two harmless commented-out lines,
REM confirmed directly). Read-only, and not build_verify\, so this respects
REM this task's own "don't touch build_verify\" instruction while still not
REM hand-copying/inventing a zconf.h of this script's own.
cl.exe /nologo /O2 /c /I "%PROJ%\third_party\zlib" /I "%PROJ%\build\third_party\zlib" ^
  "%PROJ%\third_party\zlib\adler32.c" "%PROJ%\third_party\zlib\compress.c" ^
  "%PROJ%\third_party\zlib\crc32.c" "%PROJ%\third_party\zlib\deflate.c" ^
  "%PROJ%\third_party\zlib\gzclose.c" "%PROJ%\third_party\zlib\gzlib.c" ^
  "%PROJ%\third_party\zlib\gzread.c" "%PROJ%\third_party\zlib\gzwrite.c" ^
  "%PROJ%\third_party\zlib\infback.c" "%PROJ%\third_party\zlib\inffast.c" ^
  "%PROJ%\third_party\zlib\inflate.c" "%PROJ%\third_party\zlib\inftrees.c" ^
  "%PROJ%\third_party\zlib\trees.c" "%PROJ%\third_party\zlib\uncompr.c" ^
  "%PROJ%\third_party\zlib\zutil.c" ^
  /Fo"%ZOUT%\\"
if errorlevel 1 (echo ZLIB_COMPILE_FAILED & exit /b 1)

echo === readers/engine (fresh from src\, not from build_verify) ===
cl.exe /nologo /std:c++17 /EHsc /O2 /c /I "%PROJ%\include" /I "%PROJ%\third_party\zlib" /I "%PROJ%\build\third_party\zlib" ^
  "%PROJ%\src\byte_view.cpp" "%PROJ%\src\format.cpp" "%PROJ%\src\hash.cpp" ^
  "%PROJ%\src\payload_locator.cpp" "%PROJ%\src\container.cpp" "%PROJ%\src\content_validation.cpp" ^
  "%PROJ%\src\texture_pair.cpp" "%PROJ%\src\cpeg_content_validation.cpp" ^
  "%PROJ%\src\material_block.cpp" "%PROJ%\src\geometry_block.cpp" "%PROJ%\src\material_binding.cpp" ^
  "%PROJ%\src\mesh_block.cpp" ^
  "%PROJ%\src\tree.cpp" ^
  "%PROJ%\src\png_writer.cpp" "%PROJ%\src\device.cpp" "%PROJ%\src\texture_upload.cpp" ^
  "%PROJ%\src\quad_renderer.cpp" "%PROJ%\src\window.cpp" "%PROJ%\src\swap_chain.cpp" ^
  "%PROJ%\src\mesh_renderer.cpp" ^
  /Fo"%OUT%\\"
if errorlevel 1 (echo LIB_COMPILE_FAILED & exit /b 1)

echo === extra readers for sr3_viewer's `vehicle` command (rig/anim/vehicle/fxo/d3d9bc) ===
REM sr3_viewer.cpp's own `vehicle` command (added this session, see its "vehicle
REM real-shader viewer integration" section) is the first sr3_viewer command to
REM need sr3rig/sr3anim/sr3vehicle/sr3fxo/sr3d3d9bc, none of which the block
REM above already compiles for tree_baseline_render. Compiled fresh from src\
REM here, same as every other reader above - NOT from build_verify\*.obj, so
REM this stays self-contained (build_verify\ is read-only, per this task's own
REM instruction not to touch it).
cl.exe /nologo /std:c++17 /EHsc /O2 /c /I "%PROJ%\include" /I "%PROJ%\third_party\zlib" /I "%PROJ%\build\third_party\zlib" ^
  "%PROJ%\src\rig.cpp" "%PROJ%\src\pose.cpp" "%PROJ%\src\animated_pose.cpp" "%PROJ%\src\bone_palette.cpp" ^
  "%PROJ%\src\animation.cpp" "%PROJ%\src\payload.cpp" "%PROJ%\src\sample.cpp" ^
  "%PROJ%\src\vehicle.cpp" ^
  "%PROJ%\src\shader_wrapper.cpp" "%PROJ%\src\fxo_content_validation.cpp" ^
  "%PROJ%\src\d3d9bc_disassembler.cpp" "%PROJ%\src\d3d9bc_ctab.cpp" "%PROJ%\src\d3d9bc_hlsl_translator.cpp" ^
  /Fo"%OUT%\\"
if errorlevel 1 (echo VEHICLE_LIB_COMPILE_FAILED & exit /b 1)

echo === extra reader for sr3_viewer's `zone` command (sr3zone) ===
REM sr3_viewer.cpp's new `zone` command (4th golden-scene target, vertex
REM layout code 24 zone-tile geometry) needs sr3zone::ZoneGeometry, which
REM none of the blocks above compile. Same self-contained treatment as
REM every other reader here - fresh from src\, not from build_verify\*.obj.
cl.exe /nologo /std:c++17 /EHsc /O2 /c /I "%PROJ%\include" /I "%PROJ%\third_party\zlib" /I "%PROJ%\build\third_party\zlib" ^
  "%PROJ%\src\zone_header.cpp" "%PROJ%\src\zone_geometry.cpp" ^
  /Fo"%OUT%\\"
if errorlevel 1 (echo ZONE_LIB_COMPILE_FAILED & exit /b 1)

echo === extra reader for sr3_viewer's `clmesh` command (sr3clmesh) ===
REM sr3_viewer.cpp's new `clmesh` command (6th golden-scene target, real
REM `.clmesh_pc`/`.glmesh_pc` static-prop render with real per-material
REM textures - see runClmesh()'s own doc comment) needs
REM sr3clmesh::LevelMesh, which none of the blocks above compile. Same
REM self-contained treatment as every other reader here - fresh from src\,
REM not from build_verify\*.obj. Depends only on sr3geometry (material_block/
REM material_binding, already compiled above) and sr3mesh (mesh_block.cpp,
REM already compiled above) - no new library beyond level_mesh.cpp itself.
cl.exe /nologo /std:c++17 /EHsc /O2 /c /I "%PROJ%\include" /I "%PROJ%\third_party\zlib" /I "%PROJ%\build\third_party\zlib" ^
  "%PROJ%\src\level_mesh.cpp" ^
  /Fo"%OUT%\\"
if errorlevel 1 (echo CLMESH_LIB_COMPILE_FAILED & exit /b 1)

echo === extra reader for prototype_lit_clmesh_tower.cpp's real weather row (sr3xtbl/sr3tables_environment) ===
REM tools/prototype_lit_clmesh_tower.cpp (8th golden-scene target, real
REM 3-pass deferred-lit `.clmesh_pc` render - see that file's own doc
REM comment) needs sr3xtbl::Document/sr3tables_environment's real
REM weather_time_of_day.xtbl reader, which none of the blocks above compile
REM (prototype_lit_car.cpp - the file this one was forked from - was never
REM wired into this script, so this is the first time this script needs
REM these two readers). Same self-contained treatment as every other reader
REM here - fresh from src\, not from build_verify\*.obj.
cl.exe /nologo /std:c++17 /EHsc /O2 /c /I "%PROJ%\include" /I "%PROJ%\third_party\zlib" /I "%PROJ%\build\third_party\zlib" ^
  "%PROJ%\src\xtbl.cpp" "%PROJ%\src\tables_environment.cpp" ^
  /Fo"%OUT%\\"
if errorlevel 1 (echo XTBL_LIB_COMPILE_FAILED & exit /b 1)

echo === tree_baseline_render.cpp ===
cl.exe /nologo /std:c++17 /EHsc /O2 /c /I "%PROJ%\include" /I "%PROJ%\third_party\zlib" /I "%PROJ%\build\third_party\zlib" ^
  "%PROJ%\tools\tree_baseline_render.cpp" /Fo"%OUT%\\"
if errorlevel 1 (echo TREE_TOOL_COMPILE_FAILED & exit /b 1)

echo === link tree_baseline_render.exe ===
link.exe /nologo /OUT:"%OUT%\tree_baseline_render.exe" ^
  "%OUT%\tree_baseline_render.obj" ^
  "%OUT%\byte_view.obj" "%OUT%\format.obj" "%OUT%\hash.obj" "%OUT%\payload_locator.obj" ^
  "%OUT%\container.obj" "%OUT%\content_validation.obj" ^
  "%OUT%\texture_pair.obj" "%OUT%\cpeg_content_validation.obj" ^
  "%OUT%\material_block.obj" "%OUT%\geometry_block.obj" "%OUT%\material_binding.obj" ^
  "%OUT%\mesh_block.obj" "%OUT%\tree.obj" ^
  "%OUT%\png_writer.obj" "%OUT%\device.obj" "%OUT%\texture_upload.obj" ^
  "%OUT%\quad_renderer.obj" "%OUT%\window.obj" "%OUT%\swap_chain.obj" "%OUT%\mesh_renderer.obj" ^
  "%ZOUT%\adler32.obj" "%ZOUT%\compress.obj" "%ZOUT%\crc32.obj" "%ZOUT%\deflate.obj" ^
  "%ZOUT%\gzclose.obj" "%ZOUT%\gzlib.obj" "%ZOUT%\gzread.obj" "%ZOUT%\gzwrite.obj" ^
  "%ZOUT%\infback.obj" "%ZOUT%\inffast.obj" "%ZOUT%\inflate.obj" "%ZOUT%\inftrees.obj" ^
  "%ZOUT%\trees.obj" "%ZOUT%\uncompr.obj" "%ZOUT%\zutil.obj" ^
  d3d11.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib
if errorlevel 1 (echo TREE_TOOL_LINK_FAILED & exit /b 1)

echo === prototype_lit_clmesh_tower.cpp (8th golden-scene target) ===
REM tools/prototype_lit_clmesh_tower.cpp - a real 3-pass deferred-lit
REM `.clmesh_pc` render (G-buffer prepass -> real directional light -> real
REM material pass), forked from tools/prototype_lit_car.cpp - see that
REM file's own top comment for the full provenance/REAL-vs-CHOSEN-vs-OPEN
REM breakdown. Standalone prototype, like every prototype_*.cpp in this
REM family (none are CMake targets) - compiled+linked here the same
REM self-contained way tree_baseline_render.exe above already is, so this
REM golden scene's own check function can invoke a real, fresh, on-disk
REM .exe rather than assuming one already exists.
cl.exe /nologo /std:c++17 /EHsc /O2 /c /I "%PROJ%\include" /I "%PROJ%\third_party\zlib" /I "%PROJ%\build\third_party\zlib" ^
  "%PROJ%\tools\prototype_lit_clmesh_tower.cpp" /Fo"%OUT%\\"
if errorlevel 1 (echo LITCLMESH_TOOL_COMPILE_FAILED & exit /b 1)

echo === link prototype_lit_clmesh_tower.exe ===
link.exe /nologo /OUT:"%OUT%\prototype_lit_clmesh_tower.exe" ^
  "%OUT%\prototype_lit_clmesh_tower.obj" ^
  "%OUT%\byte_view.obj" "%OUT%\format.obj" "%OUT%\hash.obj" "%OUT%\payload_locator.obj" ^
  "%OUT%\container.obj" "%OUT%\content_validation.obj" ^
  "%OUT%\texture_pair.obj" "%OUT%\cpeg_content_validation.obj" ^
  "%OUT%\material_block.obj" "%OUT%\geometry_block.obj" "%OUT%\material_binding.obj" ^
  "%OUT%\mesh_block.obj" ^
  "%OUT%\shader_wrapper.obj" "%OUT%\fxo_content_validation.obj" ^
  "%OUT%\d3d9bc_disassembler.obj" "%OUT%\d3d9bc_ctab.obj" "%OUT%\d3d9bc_hlsl_translator.obj" ^
  "%OUT%\level_mesh.obj" ^
  "%OUT%\xtbl.obj" "%OUT%\tables_environment.obj" ^
  "%OUT%\png_writer.obj" "%OUT%\device.obj" "%OUT%\texture_upload.obj" ^
  "%OUT%\quad_renderer.obj" "%OUT%\window.obj" "%OUT%\swap_chain.obj" "%OUT%\mesh_renderer.obj" ^
  "%ZOUT%\adler32.obj" "%ZOUT%\compress.obj" "%ZOUT%\crc32.obj" "%ZOUT%\deflate.obj" ^
  "%ZOUT%\gzclose.obj" "%ZOUT%\gzlib.obj" "%ZOUT%\gzread.obj" "%ZOUT%\gzwrite.obj" ^
  "%ZOUT%\infback.obj" "%ZOUT%\inffast.obj" "%ZOUT%\inflate.obj" "%ZOUT%\inftrees.obj" ^
  "%ZOUT%\trees.obj" "%ZOUT%\uncompr.obj" "%ZOUT%\zutil.obj" ^
  d3d11.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib
if errorlevel 1 (echo LITCLMESH_TOOL_LINK_FAILED & exit /b 1)

echo === prototype_lit_clmesh_tower2.cpp (re-freeze of the 8th golden-scene target, orchestrator task 2026-09-30) ===
REM tools/prototype_lit_clmesh_tower2.cpp - forked from prototype_lit_clmesh_tower.cpp
REM (untouched, "fork don't modify") - generalizes VS/PS resolution (Lead 1)
REM and wires real per-material B/C shader constants (Lead 2) into the same
REM 3-pass deferred pipeline. See that file's own top comment for the full
REM REAL/CHOSEN/PLACEHOLDER/OPEN breakdown. Same self-contained build shape
REM as every other prototype_*.cpp in this family.
cl.exe /nologo /std:c++17 /EHsc /O2 /c /I "%PROJ%\include" /I "%PROJ%\third_party\zlib" /I "%PROJ%\build\third_party\zlib" ^
  "%PROJ%\tools\prototype_lit_clmesh_tower2.cpp" /Fo"%OUT%\\"
if errorlevel 1 (echo LITCLMESH2_TOOL_COMPILE_FAILED & exit /b 1)

echo === link prototype_lit_clmesh_tower2.exe ===
link.exe /nologo /OUT:"%OUT%\prototype_lit_clmesh_tower2.exe" ^
  "%OUT%\prototype_lit_clmesh_tower2.obj" ^
  "%OUT%\byte_view.obj" "%OUT%\format.obj" "%OUT%\hash.obj" "%OUT%\payload_locator.obj" ^
  "%OUT%\container.obj" "%OUT%\content_validation.obj" ^
  "%OUT%\texture_pair.obj" "%OUT%\cpeg_content_validation.obj" ^
  "%OUT%\material_block.obj" "%OUT%\geometry_block.obj" "%OUT%\material_binding.obj" ^
  "%OUT%\mesh_block.obj" ^
  "%OUT%\shader_wrapper.obj" "%OUT%\fxo_content_validation.obj" ^
  "%OUT%\d3d9bc_disassembler.obj" "%OUT%\d3d9bc_ctab.obj" "%OUT%\d3d9bc_hlsl_translator.obj" ^
  "%OUT%\level_mesh.obj" ^
  "%OUT%\xtbl.obj" "%OUT%\tables_environment.obj" ^
  "%OUT%\png_writer.obj" "%OUT%\device.obj" "%OUT%\texture_upload.obj" ^
  "%OUT%\quad_renderer.obj" "%OUT%\window.obj" "%OUT%\swap_chain.obj" "%OUT%\mesh_renderer.obj" ^
  "%ZOUT%\adler32.obj" "%ZOUT%\compress.obj" "%ZOUT%\crc32.obj" "%ZOUT%\deflate.obj" ^
  "%ZOUT%\gzclose.obj" "%ZOUT%\gzlib.obj" "%ZOUT%\gzread.obj" "%ZOUT%\gzwrite.obj" ^
  "%ZOUT%\infback.obj" "%ZOUT%\inffast.obj" "%ZOUT%\inflate.obj" "%ZOUT%\inftrees.obj" ^
  "%ZOUT%\trees.obj" "%ZOUT%\uncompr.obj" "%ZOUT%\zutil.obj" ^
  d3d11.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib
if errorlevel 1 (echo LITCLMESH2_TOOL_LINK_FAILED & exit /b 1)

echo === sr3_viewer.cpp (fresh, for the `vehicle` golden scenes) ===
REM tools/sr3_viewer.cpp is NEVER modified by this script - only compiled as-is.
REM build_verify\sr3_viewer.exe predates this session's `vehicle` command
REM addition and does not recognise it ("unknown command: vehicle", confirmed
REM directly), and build_verify\ is otherwise off-limits per this task's own
REM instructions, so this is a fresh, self-contained compile+link into
REM tests\golden\_bin\ instead - same shape as tree_baseline_render.exe above.
cl.exe /nologo /std:c++17 /EHsc /O2 /c /I "%PROJ%\include" /I "%PROJ%\third_party\zlib" /I "%PROJ%\build\third_party\zlib" ^
  "%PROJ%\tools\sr3_viewer.cpp" /Fo"%OUT%\\"
if errorlevel 1 (echo VIEWER_COMPILE_FAILED & exit /b 1)

echo === link sr3_viewer.exe ===
link.exe /nologo /OUT:"%OUT%\sr3_viewer.exe" ^
  "%OUT%\sr3_viewer.obj" ^
  "%OUT%\byte_view.obj" "%OUT%\format.obj" "%OUT%\hash.obj" "%OUT%\payload_locator.obj" ^
  "%OUT%\container.obj" "%OUT%\content_validation.obj" ^
  "%OUT%\texture_pair.obj" "%OUT%\cpeg_content_validation.obj" ^
  "%OUT%\material_block.obj" "%OUT%\geometry_block.obj" "%OUT%\material_binding.obj" ^
  "%OUT%\mesh_block.obj" ^
  "%OUT%\rig.obj" "%OUT%\pose.obj" "%OUT%\animated_pose.obj" "%OUT%\bone_palette.obj" ^
  "%OUT%\animation.obj" "%OUT%\payload.obj" "%OUT%\sample.obj" ^
  "%OUT%\vehicle.obj" ^
  "%OUT%\shader_wrapper.obj" "%OUT%\fxo_content_validation.obj" ^
  "%OUT%\d3d9bc_disassembler.obj" "%OUT%\d3d9bc_ctab.obj" "%OUT%\d3d9bc_hlsl_translator.obj" ^
  "%OUT%\zone_header.obj" "%OUT%\zone_geometry.obj" ^
  "%OUT%\level_mesh.obj" ^
  "%OUT%\png_writer.obj" "%OUT%\device.obj" "%OUT%\texture_upload.obj" ^
  "%OUT%\quad_renderer.obj" "%OUT%\window.obj" "%OUT%\swap_chain.obj" "%OUT%\mesh_renderer.obj" ^
  "%ZOUT%\adler32.obj" "%ZOUT%\compress.obj" "%ZOUT%\crc32.obj" "%ZOUT%\deflate.obj" ^
  "%ZOUT%\gzclose.obj" "%ZOUT%\gzlib.obj" "%ZOUT%\gzread.obj" "%ZOUT%\gzwrite.obj" ^
  "%ZOUT%\infback.obj" "%ZOUT%\inffast.obj" "%ZOUT%\inflate.obj" "%ZOUT%\inftrees.obj" ^
  "%ZOUT%\trees.obj" "%ZOUT%\uncompr.obj" "%ZOUT%\zutil.obj" ^
  d3d11.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib
if errorlevel 1 (echo VIEWER_LINK_FAILED & exit /b 1)

echo === golden_scene_check.cpp ===
cl.exe /nologo /std:c++17 /EHsc /O2 /c /I "%PROJ%\third_party\zlib" /I "%PROJ%\build\third_party\zlib" ^
  "%PROJ%\tools\golden_scene_check.cpp" /Fo"%OUT%\\"
if errorlevel 1 (echo HARNESS_COMPILE_FAILED & exit /b 1)

echo === link golden_scene_check.exe ===
link.exe /nologo /OUT:"%OUT%\golden_scene_check.exe" ^
  "%OUT%\golden_scene_check.obj" ^
  "%ZOUT%\adler32.obj" "%ZOUT%\compress.obj" "%ZOUT%\crc32.obj" "%ZOUT%\deflate.obj" ^
  "%ZOUT%\gzclose.obj" "%ZOUT%\gzlib.obj" "%ZOUT%\gzread.obj" "%ZOUT%\gzwrite.obj" ^
  "%ZOUT%\infback.obj" "%ZOUT%\inffast.obj" "%ZOUT%\inflate.obj" "%ZOUT%\inftrees.obj" ^
  "%ZOUT%\trees.obj" "%ZOUT%\uncompr.obj" "%ZOUT%\zutil.obj"
if errorlevel 1 (echo HARNESS_LINK_FAILED & exit /b 1)

echo BUILD_OK
endlocal
