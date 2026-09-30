@echo off
rem Crreish bridge PC agent. Keep this window open while you want cloud sessions to use your game files.
rem First time: copy pc_config.example.json to pc_config.json and edit it (see README.md).
cd /d "%~dp0"
if not exist pc_config.json (
  echo pc_config.json not found. Copy pc_config.example.json to pc_config.json and edit it first.
  pause
  exit /b 1
)
:loop
python pc_agent.py --config pc_config.json
echo Agent exited, restarting in 30 seconds. Close this window to stop.
timeout /t 30 /nobreak >nul
goto loop
