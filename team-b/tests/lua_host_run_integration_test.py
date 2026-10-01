#!/usr/bin/env python3
"""lua_host_run end to end on a synthetic archive cache (cloud phase, 2026-09-30).

    lua_host_run_integration_test.py <fixture_exe> <lua_host_run_exe> <registered_tagged.txt> <bridge_diff.py>

The fixture (tests/lua_host_run_fixture.cpp) writes two containers holding
game_lib.lua, system_lib.lua, three mission scripts and one file with a syntax
error. lua_host_run is run on that cache twice. The test checks the first run's
per-mission rows, per-script rows, summaries and stub hits, then diffs the two
runs with tools/bridge_diff.py: apart from timing, the output must not change.
"""
import csv
import os
import re
import subprocess
import sys
import tempfile

failures = 0


def check(cond, what):
    global failures
    if not cond:
        failures += 1
        print(f"CHECK FAILED: {what}")


def read_tsv(path):
    with open(path, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE))


def read_kv(path):
    out = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = re.match(r"^([^=\s]+)=(.*)$", line.strip())
            if m:
                out[m.group(1)] = m.group(2)
    return out


def run_host(host, reglist, cache, out, *extra):
    r = subprocess.run([host, cache, reglist, out, *extra], capture_output=True, text=True, timeout=600)
    check(r.returncode == 0, f"lua_host_run exit status {r.returncode}: {r.stderr[-2000:]}")


def main(argv):
    fixture, host, reglist, bridge_diff = argv
    with tempfile.TemporaryDirectory() as tmp:
        cache = os.path.join(tmp, "cache")
        os.makedirs(cache)
        subprocess.run([fixture, cache], check=True)
        out1, out2 = os.path.join(tmp, "run1"), os.path.join(tmp, "run2")
        run_host(host, reglist, cache, out1)
        run_host(host, reglist, cache, out2)

        # Missions: one _start succeeds, one stops on OPEN engine state, one on a Lua error.
        missions = {r["stem"]: r for r in read_tsv(os.path.join(out1, "verdict_mission_drive.tsv"))}
        found = sorted(s for s, r in missions.items() if r["script_found"] == "1")
        check(found == ["dlc1_mm_04", "dlc1_mm_05", "dlc1_mm_06"], f"missions found: {found}")
        m5, m6, m4 = missions.get("dlc1_mm_05", {}), missions.get("dlc1_mm_06", {}), missions.get("dlc1_mm_04", {})
        check(m5.get("start_call_ok") == "1", "dlc1_mm_05 _start succeeds")
        check(m6.get("start_call_ok") == "0", "dlc1_mm_06 _start refused")
        check("0x00723d20" in m6.get("start_call_error", "") and "is OPEN" in m6.get("start_call_error", ""),
              f"dlc1_mm_06 stops on the OPEN zscene table entry: {m6.get('start_call_error')}")
        check(m4.get("start_call_ok") == "0", "dlc1_mm_04 _start fails")
        check("attempt to index local 't'" in m4.get("start_call_error", ""),
              f"dlc1_mm_04 reports the Lua error: {m4.get('start_call_error')}")
        for stem in found:
            check(missions[stem].get("load_ok") == "1", f"{stem} loads")

        # Per script: the syntax error is rejected by both real Lua and sr3lua.
        scripts = {r["entry_name"]: r for r in read_tsv(os.path.join(out1, "verdict_per_script.tsv"))}
        broken = scripts.get("broken_syntax.lua", {})
        check(broken.get("gameplay_load_ok") == "0", "broken_syntax.lua fails real Lua load")
        check(broken.get("sr3lua_parse_ok") == "0", "broken_syntax.lua fails sr3lua parse")
        check(len(scripts) == 6, f"6 scripts listed, got {len(scripts)}")

        summary = read_kv(os.path.join(out1, "verdict_summary.txt"))
        check(summary.get("archives_scanned") == "2", f"archives_scanned={summary.get('archives_scanned')}")
        check(summary.get("game_lib_lua_first_instance_pcall_ok") == "true", "game_lib.lua runs")
        check(summary.get("real_luaL_loadbuffer_ok", "").startswith("5/6"), "5/6 scripts load")
        check(summary.get("sr3lua_parser_agrees_with_real_lua_load_result") == "6/6", "sr3lua agrees on 6/6")
        # Batch 2026-10-01: the CONFIRMED start-up values (no co-op session,
        # tutorial states after the fill, store flag 0, the fade globals'
        # file values) fill 15 of the 40 slots; zscene and vint stay OPEN.
        check(summary.get("open_state_slots_with_values_at_start", "").startswith("15/40 "),
              f"slots with a value at start: {summary.get('open_state_slots_with_values_at_start')}")
        slots = read_tsv(os.path.join(out1, "verdict_open_state.tsv"))
        areas = {r["area"] for r in slots}
        check({"co-op", "tutorial", "vehicle-store", "zscene", "fade", "vint"} <= areas,
              f"open-state areas: {sorted(areas)}")
        check(any("0x00723d20" in r["global"] for r in slots), "zscene table entry listed")

        def slot(fragment):
            rows = [r for r in slots if fragment in r["global"]]
            check(len(rows) == 1, f"one slot matching {fragment}: {len(rows)}")
            return rows[0] if rows else {}

        check(slot("0x024d8534").get("known") == "1", "co-op session presence known (none)")
        check(slot("0x0151d600").get("known_keys") == "210", "210 tutorial entry states known")
        check(slot("0x022cdf08").get("known") == "1", "vehicle-store flag known (0)")
        check(slot("0x012e6aa4").get("known") == "1", "fade state known (2)")
        check(slot("mode stack").get("known") == "0", "mode-stack top OPEN")
        check(all(r["known"] == "0" and r["known_keys"] == "0" for r in slots if r["area"] in ("zscene", "vint")),
              "zscene and vint slots OPEN at start")
        # Fade completion paths (Sec26.24): dlc1_mm_05's fade_out(0) has no
        # screen_fade_do to run, so the labelled host fallback completes it.
        check(summary.get("fade_completion_path") == "real:0 fallback_undefined:1 fallback_no_callback:0",
              f"fade_completion_path={summary.get('fade_completion_path')}")
        check(summary.get("fade_detail", "").startswith("screen_fade_do_calls:0 screen_fade_do_errors:0 "),
              f"fade_detail={summary.get('fade_detail')}")
        drive = read_kv(os.path.join(out1, "verdict_mission_drive_summary.txt"))
        check(re.match(r"^3/\d+$", drive.get("missions_with_script_found", "")) is not None, "3 missions found")
        check(re.match(r"^1/\d+$", drive.get("missions_with_start_call_ok", "")) is not None, "1 _start ok")

        hits = {r["name"]: r for r in read_tsv(os.path.join(out1, "verdict_stub_hits_with_missions.tsv"))}
        check(hits.get("set_mission_author", {}).get("call_count_all_inclusive") == "1", "set_mission_author hit once")
        check(hits.get("zscene_is_loaded:OPEN_STATE", {}).get("call_count_all_inclusive") == "1",
              "zscene_is_loaded OPEN refusal logged once")

        # Determinism: a second run on the same cache diffs clean.
        r = subprocess.run([sys.executable, bridge_diff, out1, out2], capture_output=True, text=True, encoding="utf-8")
        report = r.stdout
        check(r.returncode == 0, "bridge_diff exit status")
        check("No per-mission changes." in report, "no per-mission changes between runs")
        check("## Stub hits" in report, "stub hit section present")
        for marker in ("no longer hit", "newly hit", "Biggest count changes", "| key | before | after |"):
            check(marker not in report, f"second run differs: '{marker}' in diff report")
        if failures:
            print(report)

        # Sec16.4 preload routing (CONFIRMED, cleared 2026-10-01) is the default:
        # game_lib.lua runs in the gameplay state only. --preload-states=tag restores
        # the old per-script tag routing (game_lib.lua is OPEN-tagged here: both states).
        opt1 = read_kv(os.path.join(out1, "verdict_summary.txt")).get("preload_states_option", "")
        check(opt1.startswith("spec16.4 (default") and opt1.endswith("scripts_rerouted=1"), f"default line: {opt1}")
        out3 = os.path.join(tmp, "run3")
        run_host(host, reglist, cache, out3, "--preload-states=tag")
        opt = read_kv(os.path.join(out3, "verdict_summary.txt")).get("preload_states_option", "")
        check(opt.startswith("tag (opt-in") and opt.endswith("scripts_rerouted=0"), f"tag line: {opt}")
        s3 = {r["entry_name"]: r for r in read_tsv(os.path.join(out3, "verdict_per_script.tsv"))}
        s1 = scripts
        check(s1.get("game_lib.lua", {}).get("ui_runChunk_attempted") == "0", "default: game_lib not run in ui")
        check(s1.get("game_lib.lua", {}).get("gameplay_runChunk_attempted") == "1", "default: game_lib run in gameplay")
        check(s3.get("game_lib.lua", {}).get("ui_runChunk_attempted") == "1", "tag: game_lib also run in ui")
        out5 = os.path.join(tmp, "run5")
        run_host(host, reglist, cache, out5, "--preload-states=spec16.4-highconf")
        check(read_kv(os.path.join(out5, "verdict_summary.txt")).get("preload_states_option", "").startswith("spec16.4 (default"),
              "old flag name accepted as an alias")
        bad = subprocess.run([host, cache, reglist, os.path.join(tmp, "run4"), "--bogus"], capture_output=True, text=True)
        check(bad.returncode != 0, "unknown option rejected")

    print("lua_host_run integration: " + ("FAILED" if failures else "all checks passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
