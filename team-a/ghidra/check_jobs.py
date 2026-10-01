#!/usr/bin/env python3
"""Pre-submit check for Team A bridge job files.

The PC agent launches Ghidra through analyzeHeadless.bat, so every argument passes through cmd.exe.
Characters cmd.exe interprets (% ^ & | < > ! " ( ) and newlines) break the launch before Ghidra
starts (job gdvf, 2026-10-01: exit 255, ". was unexpected at this time"). Run this on every job
file before `bridge_client.py submit`:

    python3 team-a/ghidra/check_jobs.py team-a/ghidra/jobs/<file>.json [...]

With no arguments it checks every job file under team-a/ghidra/jobs/. Exit code 1 if any is unsafe.
"""
import glob, json, re, sys

UNSAFE = re.compile(r'[%^&|<>!"()\r\n]')

def check(path):
    bad = []
    job = json.load(open(path, encoding="utf-8"))
    for i, step in enumerate(job.get("steps", [])):
        for a in step.get("args", []):
            if UNSAFE.search(a):
                bad.append("%s: step %d: batch-unsafe argument %r" % (path, i, a))
    return bad

paths = sys.argv[1:] or sorted(glob.glob("team-a/ghidra/jobs/**/*.json", recursive=True))
problems = [b for p in paths for b in check(p)]
print("\n".join(problems) if problems else "ok: %d job file(s), no batch-unsafe arguments" % len(paths))
sys.exit(1 if problems else 0)
