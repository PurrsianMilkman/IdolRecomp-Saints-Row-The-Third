#!/usr/bin/env python3
"""Before/after diff of two bridge result sets (cloud phase, 2026-09-30).

    python3 team-b/tools/bridge_diff.py <before> <after> [--top N] [--out report.md]

<before>/<after> are directories: a bridge result folder (~/crreish-bus/results/<id>/), a local
lua_host_run / vintdoc_validate output dir, or any folder holding those files (searched
recursively). A single .tsv file also works for the stub-hit ranking, e.g. the historical
team-b/results/verdict_stub_hits_with_missions.tsv as the baseline.

Sections, each emitted only when both sides have the file:
  * mission drive  - verdict_mission_drive.tsv: per-mission changes of start_call_ok, stop reason
                     (class), ticks survived, first unimplemented stub, first error message;
                     plus mission stems added/removed.
  * summaries      - every key=value line of verdict_mission_drive_summary.txt, verdict_summary.txt
                     and vintdoc_summary.txt that changed.
  * stub hits      - verdict_stub_hits_with_missions.tsv (else verdict_stub_hits_with_hooks.tsv):
                     names that appeared / disappeared, and the biggest call-count changes.
  * vint_doc       - vintdoc_per_file.tsv: per-file changes of combos_landed and each layout combo.
Output is Markdown. Exit status 0 always (a diff is information, not a verdict).
"""
import argparse
import csv
import os
import re
import sys

csv.field_size_limit(1 << 30)

STUB_FILES = ("verdict_stub_hits_with_missions.tsv", "verdict_stub_hits_with_hooks.tsv")
SUMMARY_FILES = ("verdict_mission_drive_summary.txt", "verdict_summary.txt", "vintdoc_summary.txt")


def find(root, name):
    if os.path.isfile(root):
        return root if os.path.basename(root) == name else None
    for dirpath, _, files in os.walk(root):
        if name in files:
            return os.path.join(dirpath, name)
    return None


def read_tsv(path):
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        return list(csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE))


def read_kv(path):
    out = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            m = re.match(r"^([A-Za-z0-9_()./,:+-]+)=(.*)$", line)
            if m:
                out[m.group(1)] = m.group(2)
    return out


def stop_class(reason):
    reason = (reason or "").strip()
    for c in ("budget exhaustion", "error", "completion", "script not found"):
        if reason.startswith(c):
            return c
    return reason.split(" ")[0] if reason else ""


def short(text, n=70):
    text = (text or "").replace("|", "/").replace("`", "'").strip() or "-"
    return text if len(text) <= n else text[: n - 1] + "…"


# Summary keys that change on every run without meaning anything.
NOISY_KEYS = {"elapsed_seconds"}


def to_int(v):
    try:
        return int(float(v))
    except (TypeError, ValueError):
        return 0


def diff_missions(before, after, lines):
    b = {r["stem"]: r for r in read_tsv(before)}
    a = {r["stem"]: r for r in read_tsv(after)}
    lines.append("## Mission drive (`verdict_mission_drive.tsv`)\n")
    for label, rows in (("before", b), ("after", a)):
        ok = sum(1 for r in rows.values() if r.get("start_call_ok") == "1")
        lines.append(f"- {label}: {len(rows)} missions, {ok} past `_start` cleanly")
    added, removed = sorted(set(a) - set(b)), sorted(set(b) - set(a))
    if added:
        lines.append(f"- missions only in after: {', '.join(added)}")
    if removed:
        lines.append(f"- missions only in before: {', '.join(removed)}")
    lines.append("")
    changes = []
    for stem in sorted(set(a) & set(b)):
        rb, ra = b[stem], a[stem]
        d = []
        if rb.get("start_call_ok") != ra.get("start_call_ok"):
            d.append(f"start_call_ok {rb.get('start_call_ok')}→{ra.get('start_call_ok')}")
        if stop_class(rb.get("stop_reason")) != stop_class(ra.get("stop_reason")):
            d.append(f"stop `{stop_class(rb.get('stop_reason'))}`→`{stop_class(ra.get('stop_reason'))}`")
        tb, ta = to_int(rb.get("ticks_survived")), to_int(ra.get("ticks_survived"))
        if tb != ta:
            d.append(f"ticks {tb}→{ta}")
        if rb.get("first_unimplemented_stub") != ra.get("first_unimplemented_stub"):
            d.append(f"first stub `{rb.get('first_unimplemented_stub') or '-'}`→`{ra.get('first_unimplemented_stub') or '-'}`")
        if rb.get("first_error_message") != ra.get("first_error_message"):
            d.append(f"first error `{short(rb.get('first_error_message'))}`→`{short(ra.get('first_error_message'))}`")
        if d:
            changes.append(f"| {stem} | {'; '.join(d)} |")
    if changes:
        lines.append(f"{len(changes)} mission(s) changed:\n")
        lines.append("| mission | change |")
        lines.append("|---|---|")
        lines.extend(changes)
    else:
        lines.append("No per-mission changes.")
    lines.append("")


def diff_kv(name, before, after, lines):
    b, a = read_kv(before), read_kv(after)
    changed = [(k, b.get(k), a.get(k)) for k in sorted(set(a) | set(b)) if b.get(k) != a.get(k) and k not in NOISY_KEYS]
    lines.append(f"## Summary `{name}`\n")
    if not changed:
        lines.append("No changes.\n")
        return
    lines.append("| key | before | after |")
    lines.append("|---|---|---|")
    for k, vb, va in changed:
        lines.append(f"| `{k}` | {vb if vb is not None else '-'} | {va if va is not None else '-'} |")
    lines.append("")


def diff_stubs(before, after, top, lines):
    def load(p):
        rows = read_tsv(p)
        key = "call_count_all_inclusive" if rows and "call_count_all_inclusive" in rows[0] else None
        if key is None and rows:
            key = next((c for c in rows[0] if c != "name" and "count" in c), None)
        return {r["name"]: to_int(r.get(key)) for r in rows}, key
    b, kb = load(before)
    a, ka = load(after)
    lines.append(f"## Stub hits (`{os.path.basename(after)}`, column `{ka}`)\n")
    # OPEN-state refusals (sr3luahost logs them as "OPEN_STATE:<value>"): the
    # engine values that blocked the run, most-hit first.
    open_rows = sorted(((a.get(n, 0), b.get(n, 0), n) for n in set(a) | set(b) if n.startswith("OPEN_STATE:")), reverse=True)
    if open_rows:
        lines.append(f"- OPEN-state refusals ({len(open_rows)} distinct values; after / before):")
        for ca, cb, n in open_rows[:top]:
            lines.append(f"  - `{n[len('OPEN_STATE:'):]}`: {ca} / {cb}")
    b = {n: c for n, c in b.items() if not n.startswith("OPEN_STATE:")}
    a = {n: c for n, c in a.items() if not n.startswith("OPEN_STATE:")}
    gone = sorted(set(b) - set(a), key=lambda n: -b[n])
    new = sorted(set(a) - set(b), key=lambda n: -a[n])
    lines.append(f"- before: {len(b)} names, {sum(b.values())} calls; after: {len(a)} names, {sum(a.values())} calls")
    if gone:
        lines.append(f"- no longer hit ({len(gone)}): " + ", ".join(f"`{n}` ({b[n]})" for n in gone[:top]))
    if new:
        lines.append(f"- newly hit ({len(new)}): " + ", ".join(f"`{n}` ({a[n]})" for n in new[:top]))
    deltas = sorted(((a[n] - b[n], n) for n in set(a) & set(b) if a[n] != b[n]), key=lambda t: -abs(t[0]))
    if deltas:
        lines.append(f"\nBiggest count changes (top {min(top, len(deltas))} of {len(deltas)}):\n")
        lines.append("| name | before | after | delta |")
        lines.append("|---|---|---|---|")
        for d, n in deltas[:top]:
            lines.append(f"| `{n}` | {b[n]} | {a[n]} | {d:+d} |")
    lines.append("")


def diff_vintdoc(before, after, lines):
    def load(p):
        return {(r.get("archive"), r.get("entry")): r for r in read_tsv(p)}
    b, a = load(before), load(after)
    lines.append("## vint_doc per file (`vintdoc_per_file.tsv`)\n")
    lines.append(f"- before: {len(b)} files; after: {len(a)} files")
    combos = [c for c in (list(next(iter(a.values())).keys()) if a else []) if re.match(r"^[AB]-P\d-O\d$", c)]
    changes = []
    for k in sorted(set(a) & set(b), key=lambda t: (t[0] or "", t[1] or "")):
        rb, ra = b[k], a[k]
        d = [f"{c} {rb.get(c)}→{ra.get(c)}" for c in combos if rb.get(c) != ra.get(c)]
        if rb.get("combos_landed") != ra.get("combos_landed"):
            d.insert(0, f"combos_landed {rb.get('combos_landed')}→{ra.get('combos_landed')}")
        if d:
            changes.append(f"| {k[0]} | {k[1]} | {'; '.join(d)} |")
    only_a, only_b = len(set(a) - set(b)), len(set(b) - set(a))
    if only_a or only_b:
        lines.append(f"- files only in after: {only_a}; only in before: {only_b}")
    if changes:
        lines.append(f"\n{len(changes)} file(s) changed:\n")
        lines.append("| archive | entry | change |")
        lines.append("|---|---|---|")
        lines.extend(changes)
    else:
        lines.append("\nNo per-file changes.")
    lines.append("")


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("before")
    ap.add_argument("after")
    ap.add_argument("--top", type=int, default=25)
    ap.add_argument("--out")
    args = ap.parse_args(argv)
    lines = [f"# Bridge result diff\n", f"- before: `{args.before}`", f"- after: `{args.after}`\n"]
    sections = 0
    pb, pa = find(args.before, "verdict_mission_drive.tsv"), find(args.after, "verdict_mission_drive.tsv")
    if pb and pa:
        diff_missions(pb, pa, lines)
        sections += 1
    for name in SUMMARY_FILES:
        pb, pa = find(args.before, name), find(args.after, name)
        if pb and pa:
            diff_kv(name, pb, pa, lines)
            sections += 1
    for name in STUB_FILES:
        pb = args.before if os.path.isfile(args.before) else find(args.before, name)
        pa = args.after if os.path.isfile(args.after) else find(args.after, name)
        if pb and pa and pb.endswith(".tsv") and pa.endswith(".tsv"):
            diff_stubs(pb, pa, args.top, lines)
            sections += 1
            break
    pb, pa = find(args.before, "vintdoc_per_file.tsv"), find(args.after, "vintdoc_per_file.tsv")
    if pb and pa:
        diff_vintdoc(pb, pa, lines)
        sections += 1
    if sections == 0:
        lines.append("No comparable files found on both sides.")
    text = "\n".join(lines) + "\n"
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(text)
    # UTF-8 bytes regardless of the console encoding: the report uses '→' and
    # Windows' default cp1252 stdout cannot encode it (failed on windows-latest CI).
    sys.stdout.buffer.write(text.encode("utf-8"))
    sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
