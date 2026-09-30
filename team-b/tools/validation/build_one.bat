@echo off
REM Builds one validation harness. Usage:  build_one.bat validate_pose
REM Run from the project root. Requires the objects in build_verify\ to
REM already exist (they are produced by the normal reader build).
setlocal
if "%~1"=="" (echo usage: build_one.bat ^<harness-name-without-.cpp^> & exit /b 1)
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 exit /b 1
set PROJ=%~dp0..\..
set BUILD=%PROJ%\build_verify
cl.exe /nologo /std:c++17 /EHsc /O2 ^
  /I "%PROJ%\include" /I "%PROJ%\third_party\zlib" ^
  "%PROJ%\tools\validation\%~1.cpp" ^
  "%BUILD%\byte_view.obj" "%BUILD%\format.obj" "%BUILD%\hash.obj" ^
  "%BUILD%\payload_locator.obj" "%BUILD%\container.obj" ^
  "%BUILD%\content_validation.obj" "%BUILD%\material_block.obj" ^
  "%BUILD%\geometry_block.obj" "%BUILD%\mesh_block.obj" "%BUILD%\rig.obj" "%BUILD%\pose.obj" "%BUILD%\material_binding.obj" "%BUILD%\vehicle.obj" ^
  "%BUILD%\animated_pose.obj" "%BUILD%\bone_palette.obj" "%BUILD%\animation.obj" "%BUILD%\payload.obj" "%BUILD%\sample.obj" ^
  "%BUILD%\zone_header.obj" "%BUILD%\zone_geometry.obj" ^
  "%BUILD%\media_bank.obj" "%BUILD%\wwise_bank_id.obj" ^
  "%BUILD%\level_mesh.obj" "%BUILD%\tree.obj" "%BUILD%\foliage_mesh.obj" ^
  "%BUILD%\save_directory.obj" "%BUILD%\save_snapshot.obj" "%BUILD%\camera_script.obj" ^
  "%BUILD%\effects.obj" "%BUILD%\texture_pair.obj" "%BUILD%\shader_wrapper.obj" "%BUILD%\manifest.obj" "%BUILD%\xtbl.obj" ^
  "%BUILD%\tables_trafficai.obj" "%BUILD%\tables_weapons.obj" "%BUILD%\tables_progression.obj" "%BUILD%\tables_environment.obj" "%BUILD%\vehicle_entry.obj" "%BUILD%\customization.obj" "%BUILD%\tables_animation.obj" "%BUILD%\tables_audio_radio.obj" "%BUILD%\tables_ui_controls.obj" "%BUILD%\tables_vehicle_world.obj" "%BUILD%\tables_diversions.obj" "%BUILD%\tables_customization_color_pools.obj" "%BUILD%\tables_customization_items.obj" "%BUILD%\tables_customization_slots_categories.obj" "%BUILD%\tables_customization_materials.obj" "%BUILD%\tables_customization_gang_actions.obj" "%BUILD%\tables_customization_characters.obj" "%BUILD%\tables_customization_player_creation.obj" "%BUILD%\d3d9bc_disassembler.obj" "%BUILD%\d3d9bc_ctab.obj" "%BUILD%\d3d9bc_hlsl_translator.obj" "%BUILD%\shader_wrapper.obj" ^
  "%BUILD%\zlib\adler32.obj" "%BUILD%\zlib\compress.obj" "%BUILD%\zlib\crc32.obj" ^
  "%BUILD%\zlib\deflate.obj" "%BUILD%\zlib\gzclose.obj" "%BUILD%\zlib\gzlib.obj" ^
  "%BUILD%\zlib\gzread.obj" "%BUILD%\zlib\gzwrite.obj" "%BUILD%\zlib\inflate.obj" ^
  "%BUILD%\zlib\infback.obj" "%BUILD%\zlib\inftrees.obj" "%BUILD%\zlib\inffast.obj" ^
  "%BUILD%\zlib\trees.obj" "%BUILD%\zlib\uncompr.obj" "%BUILD%\zlib\zutil.obj" ^
  /Fo"%BUILD%\\" /Fe"%BUILD%\%~1.exe"
if errorlevel 1 exit /b 1
echo BUILD_OK %~1
