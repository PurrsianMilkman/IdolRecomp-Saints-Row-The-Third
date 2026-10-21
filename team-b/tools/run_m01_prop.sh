#!/bin/bash
# m01 sweep command (.claude/sweep_combat300.sh shape) with this worktree's exe and --mission-timeout=60.
C="/d/IdolRecomp Saints Row The Third/Saints Row 3 CRREISH/packfiles/pc/cache"
R="/d/IdolRecomp Saints Row The Third/TEAM B/.claude/worktrees/agent-a99562d5ed4dac987"
cd "$R"
mkdir -p results/m01_prop
"$R/build/Release/sr3_viewer.exe" playtest "$C/characters.vpp_pc" "$C/misc_tables.vpp_pc" "$C/sr3_city_0.vpp_pc" "$C/sr3_city_1.vpp_pc" "$C/sr3_city_missions.vpp_pc" "$C/misc.vpp_pc" "$C/dlc1.vpp_pc" "$C/dlc2.vpp_pc" "$C/dlc3.vpp_pc" "$C/interface.vpp_pc" \
  --headless --no-input --mission=m01 --mission-timeout=60 --auto-objective --auto-combat --auto-seat-mission-vehicle --auto-answer-hud-prompts --auto-skip-cutscenes \
  --hud tools/lua_all_registered_1490_tagged.txt "$C/misc.vpp_pc" > results/m01_prop/m01_stdout.txt 2>&1
echo "m01 exit=$?" | tee results/m01_prop/exit.txt
