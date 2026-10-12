#!/usr/bin/env bash
# 49-mission sweep (2026-10-12, orchestrator's queued next step, HANDOFF.md
# "(e) Pending / queued" list): loop sr3_viewer.exe playtest --auto-objective
# over every real mission stem in tools/mission_package_per_container.tsv
# (col8==1, "covers a start script"), write one baseline-table TSV in the
# same convention lua_host_run.exe's own verdict_mission_drive.tsv uses
# (stem + stop-reason/suspend-point columns), one row per mission.
#
# Usage: tools/mission_sweep_49.sh <game_cache_dir> <out_dir>
set -euo pipefail

CACHE_DIR="${1:?usage: mission_sweep_49.sh <game_cache_dir> <out_dir>}"
OUT_DIR="${2:?usage: mission_sweep_49.sh <game_cache_dir> <out_dir>}"
EXE="build/Release/sr3_viewer.exe"
TIMEOUT=180

mkdir -p "$OUT_DIR"
TSV="$OUT_DIR/verdict_mission_drive.tsv"
printf 'stem\texit_code\tstop_line\tlast_mission_line\thad_error\n' > "$TSV"

# Same 5-archive base set used by the already-merged m05/m13 archive-scan
# re-run (characters/misc_tables/city0/city1/missions/misc/dlc1), plus
# dlc2/dlc3 for the dlc2_m0x/dlc3_m0x stems - cutscene_tables.vpp_pc and
# patch_compressed.vpp_pc are intentionally NOT passed explicitly: the
# merged expandArchivePathsWithCacheDirScan() fix auto-discovers them from
# the cache dir now, same as the m05/m13 fix relies on.
ARGS=(
  "$CACHE_DIR/characters.vpp_pc"
  "$CACHE_DIR/misc_tables.vpp_pc"
  "$CACHE_DIR/sr3_city_0.vpp_pc"
  "$CACHE_DIR/sr3_city_1.vpp_pc"
  "$CACHE_DIR/sr3_city_missions.vpp_pc"
  "$CACHE_DIR/misc.vpp_pc"
  "$CACHE_DIR/dlc1.vpp_pc"
  "$CACHE_DIR/dlc2.vpp_pc"
  "$CACHE_DIR/dlc3.vpp_pc"
)

STEMS=$(awk -F'\t' 'NR>1 && $8=="1" {print $7}' tools/mission_package_per_container.tsv | sort -u)
TOTAL=$(echo "$STEMS" | wc -l)
I=0

for stem in $STEMS; do
  I=$((I+1))
  echo "[$I/$TOTAL] $stem"
  LOG="$OUT_DIR/${stem}_stdout.txt"
  set +e
  "$EXE" playtest "${ARGS[@]}" \
    --hud tools/lua_all_registered_1490_tagged.txt "$CACHE_DIR/misc.vpp_pc" \
    --headless --no-input \
    --mission="$stem" --mission-timeout="$TIMEOUT" \
    --auto-objective --auto-seat-mission-vehicle --auto-answer-hud-prompts \
    > "$LOG" 2>&1
  EXIT=$?
  set -e

  STOP_LINE=$(grep -m1 "^mission  : --mission=$stem stopped" "$LOG" || true)
  LAST_MISSION_LINE=$(grep "^mission  :" "$LOG" | tail -1 || true)
  HAD_ERROR=0
  grep -qi "errored\|LOAD ERROR\|PCALL ERROR" "$LOG" && HAD_ERROR=1 || true

  printf '%s\t%s\t%s\t%s\t%s\n' \
    "$stem" "$EXIT" "${STOP_LINE//$'\t'/ }" "${LAST_MISSION_LINE//$'\t'/ }" "$HAD_ERROR" >> "$TSV"
done

echo "Wrote $TSV"
