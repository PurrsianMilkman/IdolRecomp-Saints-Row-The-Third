#!/usr/bin/env python3
"""Unit test for tools/bridge_diff.py on synthetic result folders (no game data)."""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "tools", "bridge_diff.py")
failures = 0


def check(cond, msg):
    global failures
    if not cond:
        failures += 1
        print("CHECK FAILED:", msg)


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)


MISSION_HDR = ("stem\tentry_name\tcontainer_name\tscript_found\tload_ok\tload_error\tpcall_ok\tpcall_error\t"
               "start_func_name\tstart_existed\tstart_attempted\tstart_call_ok\tstart_call_error\t"
               "ticks_survived\tstop_reason\tfirst_error_message\tfirst_unimplemented_stub\n")


def mrow(stem, ok, ticks, stop, stub, err=""):
    return f"{stem}\t{stem}.lua\tc\t1\t1\t\t1\t\t{stem}_start\t1\t1\t{ok}\t\t{ticks}\t{stop}\t{err}\t{stub}\n"


with tempfile.TemporaryDirectory() as d:
    b, a = os.path.join(d, "before"), os.path.join(d, "after", "nested")
    write(os.path.join(b, "verdict_mission_drive.tsv"), MISSION_HDR +
          mrow("m01", 0, 5, "budget exhaustion (watchdog)", "fade_is_fully_faded_out") +
          mrow("m02", 1, 100, "completion", "") +
          mrow("m03", 0, 1, "error: x", "vint_is_std_res"))
    write(os.path.join(a, "verdict_mission_drive.tsv"), MISSION_HDR +
          mrow("m01", 1, 40, "completion", "") +
          mrow("m02", 1, 100, "completion", "") +
          mrow("m04", 0, 2, "error: y", "zscene_prep"))
    write(os.path.join(b, "verdict_mission_drive_summary.txt"),
          "=== x ===\nmissions_with_start_call_ok=9/49\ntotal_ticks_survived_across_all_missions=10\n")
    write(os.path.join(a, "verdict_mission_drive_summary.txt"),
          "missions_with_start_call_ok=12/49\ntotal_ticks_survived_across_all_missions=10\nnew_key=1\n")
    stub_hdr = "name\tcall_count_all_inclusive\tother\n"
    write(os.path.join(b, "verdict_stub_hits_with_missions.tsv"),
          stub_hdr + "fade_is_fully_faded_out\t30833183\tx\nzscene_is_loaded\t714285\tx\nvint_x\t7\tx\n")
    write(os.path.join(a, "verdict_stub_hits_with_missions.tsv"),
          stub_hdr + "zscene_is_loaded\t500\tx\nvint_x\t7\tx\nnew_stub\t3\tx\n")
    vd_hdr = "archive\tentry\tsize\tA-P1-O1\tA-P3-O1\tcombos_landed\n"
    write(os.path.join(b, "vintdoc_per_file.tsv"), vd_hdr + "i.vpp_pc\ta.vint_doc\t10\tfail\tLAND\t1\n")
    write(os.path.join(a, "vintdoc_per_file.tsv"), vd_hdr + "i.vpp_pc\ta.vint_doc\t10\tLAND\tLAND\t2\n")

    out = subprocess.run([sys.executable, TOOL, b, os.path.join(d, "after"), "--top", "5"],
                         capture_output=True, text=True)
    t = out.stdout
    check(out.returncode == 0, "exit 0")
    check("before: 3 missions, 1 past `_start` cleanly" in t, "before mission count")
    check("after: 3 missions, 2 past `_start` cleanly" in t, "after mission count")
    check("missions only in after: m04" in t and "missions only in before: m03" in t, "added/removed")
    check("| m01 | start_call_ok 0→1; stop `budget exhaustion`→`completion`; ticks 5→40; "
          "first stub `fade_is_fully_faded_out`→`-` |" in t, "m01 row")
    check("| m02 |" not in t, "unchanged mission not listed")
    check("| `missions_with_start_call_ok` | 9/49 | 12/49 |" in t, "summary change")
    check("total_ticks_survived" not in t, "unchanged summary key not listed")
    check("| `new_key` | - | 1 |" in t, "new summary key")
    check("no longer hit (1): `fade_is_fully_faded_out` (30833183)" in t, "stub gone")
    check("newly hit (1): `new_stub` (3)" in t, "stub new")
    check("| `zscene_is_loaded` | 714285 | 500 | -713785 |" in t, "stub delta")
    check("`vint_x`" not in t.split("Biggest count changes")[-1], "unchanged stub not in deltas")
    check("| i.vpp_pc | a.vint_doc | combos_landed 1→2; A-P1-O1 fail→LAND |" in t, "vintdoc change")

    # Single-file mode: historical ranking vs a new one.
    out2 = subprocess.run([sys.executable, TOOL, os.path.join(b, "verdict_stub_hits_with_missions.tsv"),
                           os.path.join(a, "verdict_stub_hits_with_missions.tsv")], capture_output=True, text=True)
    check("## Stub hits" in out2.stdout and "## Mission drive" not in out2.stdout, "single-file mode")

    out3 = subprocess.run([sys.executable, TOOL, d, os.path.join(d, "nothing")], capture_output=True, text=True)
    check("No comparable files found on both sides." in out3.stdout, "nothing comparable")

if failures:
    print(f"{failures} CHECK(S) FAILED")
    sys.exit(1)
print("ALL bridge_diff TESTS PASSED")
