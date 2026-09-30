@echo off
rem Crreish bridge PC agent. Keep this window open while you want cloud sessions to use your game files.
rem First time: copy pc_config.example.json to pc_config.json and edit it (see README.md).
cd /d "%~dp0"
if not exist pc_config.json (
  echo pc_config.json not found. Copy pc_config.example.json to pc_config.json and edit it first.
  pause
  exit /b 1
)
rem Find a real Python 3 (the py launcher first; plain "python" can be the Microsoft Store stub).
set "PY="
py -3 --version >nul 2>&1 && set "PY=py -3"
if not defined PY python --version >nul 2>&1 && set "PY=python"
if not defined PY (
  echo Python 3 is not installed. Install it from https://www.python.org/downloads/
  echo and tick "Add python.exe to PATH" in the installer, then run this again.
  pause
  exit /b 1
)
where git >nul 2>&1 || (
  echo Git is not installed. Install Git for Windows from https://git-scm.com/download/win and run this again.
  pause
  exit /b 1
)
rem CMake: use the one on PATH, else the copy bundled with Visual Studio 2022 (found via vswhere).
where cmake >nul 2>&1 || call :find_vs_cmake
where cmake >nul 2>&1 || (
  echo CMake was not found. Install Visual Studio 2022 with "Desktop development with C++" ^(it includes CMake^),
  echo or CMake from https://cmake.org/download/ with "Add to PATH", then run this again.
  pause
  exit /b 1
)
:loop
%PY% pc_agent.py --config pc_config.json
echo Agent exited, restarting in 30 seconds. Close this window to stop.
timeout /t 30 /nobreak >nul
goto loop

:find_vs_cmake
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 0
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR exit /b 0
set "VSCMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
if exist "%VSCMAKE%\cmake.exe" (
  set "PATH=%VSCMAKE%;%PATH%"
  echo Using the CMake bundled with Visual Studio: %VSCMAKE%
)
exit /b 0
