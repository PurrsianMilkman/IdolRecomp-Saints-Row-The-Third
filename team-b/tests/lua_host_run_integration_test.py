#!/usr/bin/env python3
"""lua_host_run end to end on a synthetic archive cache (cloud phase, 2026-09-30).

    lua_host_run_integration_test.py <fixture_exe> <lua_host_run_exe> <registered_tagged.txt> <bridge_diff.py>

The fixture (tests/lua_host_run_fixture.cpp) writes two containers holding
game_lib.lua, system_lib.lua, five mission scripts and one file with a syntax
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

        # Missions: one _start succeeds, one stops on OPEN engine state, one on a
        # Lua error, one yields after starting a child thread, one runs away.
        missions = {r["stem"]: r for r in read_tsv(os.path.join(out1, "verdict_mission_drive.tsv"))}
        found = sorted(s for s, r in missions.items() if r["script_found"] == "1")
        check(found == ["dlc1_mm_02", "dlc1_mm_03", "dlc1_mm_04", "dlc1_mm_05", "dlc1_mm_06"], f"missions found: {found}")
        m5, m6, m4 = missions.get("dlc1_mm_05", {}), missions.get("dlc1_mm_06", {}), missions.get("dlc1_mm_04", {})
        m3, m2 = missions.get("dlc1_mm_03", {}), missions.get("dlc1_mm_02", {})
        # Also a regression test for the real dlc2_m02 bug (spec-lua-api-behaviour.md
        # Sec36.4/spec-lua-bindings.md Sec14.11, fixed 2026-10-02): dlc1_mm_05_start
        # indexes a checkpoint table by `cp` the same way dlc2_m02.lua does; if `cp`
        # ever regressed back to the old CHOSEN number 0 instead of the CONFIRMED
        # string "mission start", this would fail with the real "attempt to index
        # local 'cp_data' (a nil value)" error instead of running to its end.
        check(m5.get("start_call_ok") == "1" and m5.get("start_suspended") == "0", "dlc1_mm_05 _start runs to its end")
        check(m5.get("start_call_error", "") == "", f"dlc1_mm_05 no checkpoint-argument error: {m5.get('start_call_error')}")
        # Request 11 (Sec26.27): _start is a script-thread record - thread_new
        # inside it finds a parent, and thread_yield suspends it (no error).
        check(m3.get("start_call_ok") == "1" and m3.get("start_suspended") == "1",
              f"dlc1_mm_03 _start yields after thread_new: {m3.get('start_call_error')}")
        # The watchdog sits on the _start coroutine.
        check(m2.get("start_call_ok") == "0" and "watchdog" in m2.get("start_call_error", ""),
              f"dlc1_mm_02 runaway _start stopped by the watchdog: {m2.get('start_call_error')}")
        # 'scene_a' is a kind-1 entry of the fixture's cutscene_tables.vpp_pc;
        # the skip_all_cutscenes byte is false at start (Sec26.25 Globals,
        # 2026-10-01) and the current scene entry 0x0153b530 is null at start
        # (Sec26.25 Globals, RESOLVED 2026-10-02, CONFIRMED), so
        # zscene_is_loaded answers false and helper_wait_scene yields - no
        # OPEN refusal any more.
        check(m6.get("start_call_ok") == "1" and m6.get("start_suspended") == "1",
              f"dlc1_mm_06 _start waits on the scene: {m6.get('start_call_error')}")
        check(m6.get("start_call_error", "") == "", f"dlc1_mm_06 no error: {m6.get('start_call_error')}")
        check("game_lib.lua\"]:2 (in helper_wait_scene)" in m6.get("start_suspended_at", ""),
              f"dlc1_mm_06 parks in helper_wait_scene: {m6.get('start_suspended_at')}")
        # The scheduler 0x00e0cf50 (thread-table section, MAJOR CORRECTION
        # 2026-10-02: the real driver runs once per rendered/game frame, not
        # from an independent ~30 ms pump - lua_host_run now maps 1 tick to 1
        # frame to 1 scheduler pass, kSchedulerPassesPerTick == 1) resumes it
        # every pass; nothing ever loads the scene, so it is still waiting, in
        # the same place, after the whole tick budget.
        check(m6.get("start_after_ticks") == "still_suspended", f"dlc1_mm_06 after ticks: {m6.get('start_after_ticks')}")
        check("game_lib.lua\"]:2 (in helper_wait_scene)" in m6.get("start_still_suspended_at", ""),
              f"dlc1_mm_06 still in helper_wait_scene: {m6.get('start_still_suspended_at')}")
        check(m6.get("ticks_survived") == "20" and int(m6.get("scheduler_resumes", "0")) == 20 * 1,
              f"dlc1_mm_06 resumed 1 pass x 20 ticks: {m6.get('ticks_survived')} {m6.get('scheduler_resumes')}")
        # dlc1_mm_03: _start and its child are both resumed by the first pass
        # and run to their ends.
        check(m3.get("start_after_ticks") == "finished", f"dlc1_mm_03 after ticks: {m3.get('start_after_ticks')}")
        check(m3.get("scheduler_resumes") == "2" and m3.get("scheduler_thread_errors") == "0",
              f"dlc1_mm_03 resumes: {m3.get('scheduler_resumes')} errors: {m3.get('scheduler_thread_errors')}")
        check(m5.get("start_after_ticks") == "", "dlc1_mm_05 never suspended")
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
        check(len(scripts) == 8, f"8 scripts listed, got {len(scripts)}")

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
        # Sec26.25: the cutscene frame ran once per mission tick. 2026-10-02
        # correction (the `mm_p_01` zscene-promotion-driver investigation):
        # completion and promotion each run unconditionally and independently
        # now (cutsceneHostFrame no longer stops the whole frame on the
        # cutscene state alone) - none of this fixture's missions ever call
        # zscene_prep, so the pending slot 0x0153b538 stays OPEN the whole
        # run and is what the *last* blocked frame now reports (completion's
        # own OPEN zscene load state 0x0153b51c blocks first every frame, but
        # promotion's attempt - now unconditional - is the one still pending
        # when the run ends, so it is the one whose OPEN read is recorded
        # last). The cutscene state 0x0153b520 itself is never even read here
        # (no cutscene_play call either), so it stays OPEN but is no longer
        # what the per-frame driver reports stopping on.
        cf = summary.get("cutscene_frame", "")
        check(cf.startswith("frames:") and "last_blocker=[0x0153b538" in cf, f"cutscene_frame={cf}")
        m = re.match(r"^frames:(\d+) blocked_on_open:(\d+) ", cf)
        check(m is not None and m.group(1) == m.group(2) and int(m.group(1)) > 0, f"every cutscene frame blocked: {cf}")
        check(summary.get("game_lib_lua_first_instance_pcall_ok") == "true", "game_lib.lua runs")
        check(summary.get("real_luaL_loadbuffer_ok", "").startswith("7/8"), "7/8 scripts load")
        check(summary.get("sr3lua_parser_agrees_with_real_lua_load_result") == "8/8", "sr3lua agrees on 8/8")
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
        # zscene / cutscene: only the scene table (2 entries) and the skip byte
        # (false, Sec26.25 Globals 2026-10-01) have a value; vint: everything
        # but the safe-frame source object's +0x8 / +0xc.
        check(slot("0x0153b556").get("known") == "1", "skip_all_cutscenes byte known (false)")
        check(slot("0x0153b530").get("known") == "1", "current scene entry known (null, 2026-10-02)")
        check(all(r["known"] == "0" and r["known_keys"] == "0" for r in slots
                  if r["area"] in ("zscene", "cutscene") and r["kind"] != "table" and "0x0153b556" not in r["global"]
                  and "0x0153b530" not in r["global"]),
              "other zscene and cutscene slots OPEN at start")
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
        check(re.match(r"^5/\d+$", drive.get("missions_with_script_found", "")) is not None, "5 missions found")
        check(re.match(r"^3/\d+$", drive.get("missions_with_start_call_ok", "")) is not None,
              f"3 _start ok: {drive.get('missions_with_start_call_ok')}")
        check(re.match(r"^2/\d+ ", drive.get("missions_with_start_suspended", "")) is not None,
              f"2 _start suspended: {drive.get('missions_with_start_suspended')}")
        check(drive.get("missions_start_after_ticks", "").startswith("still_suspended:1 finished:1 errored:0 killed:0 "),
              f"after ticks: {drive.get('missions_start_after_ticks')}")
        # Only dlc1_mm_06_start is left: dlc1_mm_03's records were resumed to their ends.
        check(drive.get("live_script_threads_after_missions", "").startswith("1 "),
              f"live threads after missions: {drive.get('live_script_threads_after_missions')}")

        hits = {r["name"]: r for r in read_tsv(os.path.join(out1, "verdict_stub_hits_with_missions.tsv"))}
        check(hits.get("set_mission_author", {}).get("call_count_all_inclusive") == "1", "set_mission_author hit once")
        check("zscene_is_loaded:OPEN_STATE" not in hits, "no zscene_is_loaded OPEN refusal (0x0153b530 known)")
        # Polled once on the first run, then once per resume of dlc1_mm_06_start:
        # its own mission's passes, plus the passes of the missions driven after
        # it, where it is a leftover record (the only one: dlc1_mm_03's finish).
        sp = re.search(r"leftover_resumes:(\d+) ", drive.get("scheduler_passes", ""))
        check(sp is not None, f"scheduler_passes line: {drive.get('scheduler_passes')}")
        leftover = int(sp.group(1)) if sp else -1
        check(int(hits.get("zscene_is_loaded", {}).get("call_count_all_inclusive", "0")) ==
              1 + int(m6.get("scheduler_resumes", "0")) + leftover,
              f"zscene_is_loaded polled per resume: {hits.get('zscene_is_loaded', {}).get('call_count_all_inclusive')} "
              f"own {m6.get('scheduler_resumes')} leftover {leftover}")

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
