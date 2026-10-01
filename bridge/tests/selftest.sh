#!/usr/bin/env bash
# End-to-end test of the bridge with local bare repos standing in for GitHub.
# Needs git, python3, cmake and a C++ compiler. Run: bash bridge/tests/selftest.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
export HOME="$T/home"; mkdir -p "$HOME"
git config --global user.name t; git config --global user.email t@t; git config --global init.defaultBranch main
git config --global protocol.file.allow always

git init -q --bare "$T/bus.git"
git init -q --bare "$T/project.git"
# fake project: team-b/CMakeLists.txt with one tool that reads a "game" file
git clone -q "$T/project.git" "$T/proj"; mkdir -p "$T/proj/team-b" "$T/proj/team-a"
cat > "$T/proj/team-b/CMakeLists.txt" <<'C'
cmake_minimum_required(VERSION 3.15)
project(fake CXX)
add_executable(count_bytes count_bytes.cpp)
C
cat > "$T/proj/team-b/count_bytes.cpp" <<'C'
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
int main(int c, char** v){ if(c<3) return 2; std::ifstream f(v[1], std::ios::binary);
  std::string s((std::istreambuf_iterator<char>(f)), {}); std::printf("file=%s bytes=%zu\n", v[1], s.size());
  std::ofstream(std::string(v[2]) + "/summary.tsv") << "bytes\t" << s.size() << "\n";
  std::ofstream(std::string(v[2]) + "/blob.bin") << std::string("a\0b", 3); return 0; }
C
(cd "$T/proj" && git checkout -qb claude/team-b-work && git add -A && git commit -qm init && git push -q origin claude/team-b-work)
REF=$(git -C "$T/proj" rev-parse HEAD)

mkdir -p "$T/game"; printf 'hello game' > "$T/game/data.vpp_pc"; mkdir -p "$T/game/sub"; printf x > "$T/game/sub/a.str2_pc"
cat > "$T/pc_config.json" <<J
{"bus_url": "$T/bus.git", "project_url": "$T/project.git", "game_dir": "$T/game", "work_dir": "$T/work",
 "cmake_configure_args": [], "build_config": "Release", "poll_seconds": 1}
J

# cloud side (as team-b)
python3 "$HERE/bridge_client.py" setup --url "$T/bus.git" --team team-b --dir "$T/cloudbus" >/dev/null
cat > "$T/job.json" <<J
{"title": "count", "ref": "$REF", "collect": ["*.tsv", "*.bin"],
 "steps": [{"kind": "ls", "pattern": "**/*_pc"},
           {"kind": "build", "targets": ["count_bytes"]},
           {"kind": "run", "tool": "count_bytes", "args": ["{GAME}/data.vpp_pc", "{OUT}"]}]}
J
JID=$(python3 "$HERE/bridge_client.py" submit "$T/job.json")
BAD=$(echo '{"title":"bad","ref":"'$REF'","steps":[{"kind":"run","tool":"count_bytes","args":["{EXE}"]}]}' | python3 "$HERE/bridge_client.py" submit -)

CJ=$(echo '{"title":"to cancel","steps":[{"kind":"ls"}]}' | python3 "$HERE/bridge_client.py" submit -)
python3 "$HERE/bridge_client.py" cancel "$CJ" >/dev/null
python3 "$HERE/pc_agent.py" --config "$T/pc_config.json" --once > "$T/agent.log" 2>&1 || { cat "$T/agent.log"; exit 1; }
! grep -q "$CJ" "$T/agent.log"                        # cancelled job never started
R=$(python3 "$HERE/bridge_client.py" show "$CJ" || true); grep -q "CANCELLED" <<<"$R"

OUT=$(python3 "$HERE/bridge_client.py" wait "$JID" --timeout 30) || { echo "$OUT"; cat "$T/agent.log"; exit 1; }
echo "$OUT"
grep -q "bytes=10" <<<"$OUT"
grep -q "file={GAME}/data.vpp_pc" <<<"$OUT"          # local path scrubbed
grep -q "sub/a.str2_pc" <<<"$OUT"                    # ls step
grep -q "summary.tsv" <<<"$OUT"
grep -q "(not uploaded) blob.bin" <<<"$OUT"          # binary refused
! grep -rq "$T/game" "$T/cloudbus/results"           # no raw local paths anywhere
test "$(cat "$T/cloudbus/results/$JID/summary.tsv")" = "$(printf 'bytes\t10')"
R=$(python3 "$HERE/bridge_client.py" show "$BAD" || true); grep -q "REJECTED" <<<"$R"; grep -q "Team A only" <<<"$R"
python3 "$HERE/bridge_client.py" status | grep -q ALIVE
# clean room: a team-a job must never reach the team-b clone
AJ=$(HOME="$T/homeA" bash -c 'mkdir -p "$HOME" && python3 "$0/bridge_client.py" setup --url "$1/bus.git" --team team-a --dir "$1/cloudbusA" >/dev/null && echo "{\"title\":\"a\",\"steps\":[{\"kind\":\"ls\"}]}" | python3 "$0/bridge_client.py" submit -' "$HERE" "$T")
python3 "$HERE/pc_agent.py" --config "$T/pc_config.json" --once >> "$T/agent.log" 2>&1
python3 "$HERE/bridge_client.py" list >/dev/null
! git -C "$T/cloudbus" branch -r | grep -q team-a
! git -C "$T/cloudbus" cat-file -e "origin/team-a" 2>/dev/null
HOME="$T/homeA" python3 "$HERE/bridge_client.py" show "$AJ" | grep -q "\[OK\]"
echo "SELFTEST PASS"
