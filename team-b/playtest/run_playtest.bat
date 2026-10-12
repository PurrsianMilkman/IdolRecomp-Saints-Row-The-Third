@echo off
rem run_playtest.bat - launches Project Crreish "playtest v0.12", the v0.12
rem playtest cut of sr3_viewer.exe's `playtest` command.
rem
rem Usage:
rem   run_playtest.bat ["path to your Saints Row 3 CRREISH packfiles\pc\cache"]
rem If you omit the argument, it defaults to this project's own documented
rem real-install path. Nothing from the game is bundled in this folder -
rem this .bat reads the real archives from that cache directory AT RUN TIME
rem ONLY, every time you launch it.
rem
rem Controls: WASD move, hold Left Shift to sprint, mouse to look, left mouse
rem button to fire. Esc: open/close the pause menu. F10: quit. A car is
rem parked a few metres ahead of you at spawn - walk up to it and press E to
rem enter/exit. While driving: W/S throttle/brake, A/D steer, E to exit.
rem Press T to skip the clock forward 3 hours. Press R to arm/disarm a police
rem roadblock (debug feature). Press E near a shop/crib doorway to enter it.
rem Walk over a dropped (non-heavy) weapon to auto-pick it up; press E near a
rem heavy weapon (minigun/flamethrower) to pick it up.
setlocal
set "CACHE=%~1"
if "%CACHE%"=="" set "CACHE=D:\Project Crreish\Saints Row 3 CRREISH\packfiles\pc\cache"

set "HERE=%~dp0"
set "EXE=%HERE%sr3_viewer.exe"
if not exist "%EXE%" (
  echo ERROR: "%EXE%" not found next to this .bat file.
  pause
  exit /b 1
)
if not exist "%CACHE%\characters.vpp_pc" (
  echo ERROR: "%CACHE%\characters.vpp_pc" not found.
  echo.
  echo Pass your own game's packfiles\pc\cache directory as the first
  echo argument, e.g.:
  echo   run_playtest.bat "D:\Path\To\Saints Row 3\packfiles\pc\cache"
  pause
  exit /b 1
)

echo Launching playtest v0.12 from: %CACHE%
echo Controls: WASD move, hold Left Shift to sprint, mouse to look, left mouse
echo button to fire. Esc: pause menu. F10: quit.
echo A car is parked a few metres ahead of you - walk up and press E to
echo enter/exit. While driving: W/S throttle/brake, A/D steer, E to exit.
echo Press E near a shop/crib doorway to enter it. Walk over a dropped
echo (non-heavy) weapon to auto-pick it up; press E near a heavy weapon
echo (minigun/flamethrower) to pick it up.
echo.

rem --hud wires in the real (if currently static, see the console banner
rem sr3_viewer itself prints) health/ammo/money/notoriety overlay. Comment
rem this line out (or remove the --hud ... arguments below) to run without
rem it.
"%EXE%" playtest "%CACHE%\characters.vpp_pc" "%CACHE%\misc_tables.vpp_pc" "%CACHE%\sr3_city_0.vpp_pc" --hud "%HERE%lua_all_registered_1490_tagged.txt" "%CACHE%\misc.vpp_pc"

set "EXITCODE=%ERRORLEVEL%"
if not "%EXITCODE%"=="0" (
  echo.
  echo sr3_viewer.exe exited with code %EXITCODE%.
  pause
)
endlocal
exit /b %EXITCODE%
