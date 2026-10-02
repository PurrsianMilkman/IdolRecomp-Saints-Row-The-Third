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
        # 2026-10-01 (Sec26.25 scene table): 'scene_a' is a kind-1 entry of the
        # fixture's cutscene_tables.vpp_pc, so the table is no longer the
        # blocker; the next read, the skip_all_cutscenes byte, has no specced
        # start-up value.
        check("0x0153b556" in m6.get("start_call_error", "") and "is OPEN" in m6.get("start_call_error", ""),
              f"dlc1_mm_06 stops on the OPEN skip_all_cutscenes byte: {m6.get('start_call_error')}")
        check("0x00723d20" not in m6.get("start_call_error", ""), "the scene table entry is no longer OPEN")
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
        check(summary.get("archives_scanned") == "3", f"archives_scanned={summary.get('archives_scanned')}")
        # Sec26.25 item 5: the fixture's scene table - 4 names, "dlc_skipped"
        # skipped by the main filter, "no_file_c" has no scene file.
        zt = summary.get("zscene_table", "")
        for part in ("installed source=cutscene_tables.vpp_pc", "names_accepted=3", "names_skipped=1", "capacity=15",
                     "entries=2", "missing_scene_files=1", "kind1_zscene=1", "kind2_story=1", "kind_open=0"):
            check(part in zt, f"zscene_table has {part}: {zt}")
        # Sec26.26: the UI subsystem init ran with the CHOSEN default display.
        check(summary.get("display_option", "").startswith("1280x720 ") and "display_mode=-1" in summary.get("display_option", ""),
              f"display_option={summary.get('display_option')}")
        # Sec26.25: the cutscene frame ran once per mission tick and stops on
        # the cutscene state, which has no specced start-up value.
        cf = summary.get("cutscene_frame", "")
        check(cf.startswith("frames:") and "last_blocker=[0x0153b520" in cf, f"cutscene_frame={cf}")
        m = re.match(r"^frames:(\d+) blocked_on_open:(\d+) ", cf)
        check(m is not None and m.group(1) == m.group(2) and int(m.group(1)) > 0, f"every cutscene frame blocked: {cf}")
        check(summary.get("game_lib_lua_first_instance_pcall_ok") == "true", "game_lib.lua runs")
        check(summary.get("real_luaL_loadbuffer_ok", "").startswith("5/6"), "5/6 scripts load")
        check(summary.get("sr3lua_parser_agrees_with_real_lua_load_result") == "6/6", "sr3lua agrees on 6/6")
        # Batch 2026-10-01: the CONFIRMED start-up values (no co-op session,
        # tutorial states after the fill, store flag 0, the fade globals'
        # file values) fill 15 of the first 40 slots; the nnlt batch adds the
        # safe-frame constants, the display init's 6 vint values and the scene
        # table. The summary line must agree with the TSV.
        slots = read_tsv(os.path.join(out1, "verdict_open_state.tsv"))
        known_rows = sum(1 for r in slots if r["known"] == "1" or r["known_keys"] != "0")
        check(summary.get("open_state_slots_with_values_at_start", "").startswith(f"{known_rows}/{len(slots)} "),
              f"slots with a value at start: {summary.get('open_state_slots_with_values_at_start')}")
        areas = {r["area"] for r in slots}
        check({"co-op", "tutorial", "vehicle-store", "zscene", "cutscene", "fade", "vint"} <= areas,
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
        # zscene / cutscene: only the scene table (2 entries) has a value; vint:
        # everything but the safe-frame source object's +0x8 / +0xc.
        check(all(r["known"] == "0" and r["known_keys"] == "0" for r in slots
                  if r["area"] in ("zscene", "cutscene") and r["kind"] != "table"),
              "zscene and cutscene slots OPEN at start")
        # "scene table" alone is ambiguous: it's also a substring of the per-entry
        # "zscene table entry with kind 1 (...)" slot. 0x0153b294 is the table-level
        # slot's own unique address (src/lua_engine_state.cpp).
        check(slot("0x0153b294").get("known") == "1" and slot("0x0153b294").get("known_keys") == "2", "scene table installed")
        check(slot("0x00e236f0)+4").get("known") == "1" and slot("0x0132bd80").get("known") == "1", "UI init ran")
        check(slot("0x0115ba60").get("known") == "1" and slot("0x0116dfc0").get("known") == "1", "safe-frame constants")
        check(slot("+0x8 ((context").get("known") == "0" and slot("+0xc ((context").get("known") == "0",
              "safe-frame source object OPEN")
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

        # --host-rng (HYPOTHESIS / host substitute): off unless explicitly given.
        rng1 = read_kv(os.path.join(out1, "verdict_summary.txt")).get("host_rng_option", "")
        check(rng1.startswith("off (default"), f"host_rng_option off by default: {rng1}")
        out5 = os.path.join(tmp, "run5")
        run_host(host, reglist, cache, out5, "--host-rng=7")
        rng5 = read_kv(os.path.join(out5, "verdict_summary.txt")).get("host_rng_option", "")
        check(rng5.startswith("on seed=7 (HYPOTHESIS"), f"host_rng_option with --host-rng=7: {rng5}")
        badseed = subprocess.run([host, cache, reglist, os.path.join(tmp, "run6"), "--host-rng=abc"],
                                 capture_output=True, text=True)
        check(badseed.returncode != 0, "bad --host-rng seed rejected")

        # --display (Sec26.26 host input): none keeps the record OPEN; a bad value is rejected.
        out7 = os.path.join(tmp, "run7")
        run_host(host, reglist, cache, out7, "--display=none")
        check(read_kv(os.path.join(out7, "verdict_summary.txt")).get("display_option", "").startswith("none "),
              "display_option none")
        slots7 = {r["global"]: r for r in read_tsv(os.path.join(out7, "verdict_open_state.tsv"))}
        check(all(r["known"] == "0" for g, r in slots7.items() if "0x00e236f0" in g), "record OPEN with --display=none")
        out8 = os.path.join(tmp, "run8")
        run_host(host, reglist, cache, out8, "--display=1024x768")
        check(read_kv(os.path.join(out8, "verdict_summary.txt")).get("display_option", "").startswith("1024x768 "),
              "display_option 1024x768")
        baddisp = subprocess.run([host, cache, reglist, os.path.join(tmp, "run9"), "--display=12x"],
                                 capture_output=True, text=True)
        check(baddisp.returncode != 0, "bad --display rejected")

    print("lua_host_run integration: " + ("FAILED" if failures else "all checks passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
