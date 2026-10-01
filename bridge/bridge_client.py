#!/usr/bin/env python3
"""Crreish bridge: cloud-side client.

Used by Claude Code cloud sessions to send jobs to the owner's PC (which has the
game files) and read the results back. Transport is a git "bus" repository:
one branch per team, jobs in jobs/<id>.json, results in results/<id>.json and
results/<id>/.

    bridge_client.py setup  --url https://github.com/OWNER/BUS --team team-b
    bridge_client.py status                      # is the PC agent alive?
    bridge_client.py submit job.json             # or '-' for stdin; prints the job id
    bridge_client.py quick --title T --build lua_host_run -- lua_host_run {GAME} --missions
    bridge_client.py wait <id> [--timeout 3600]
    bridge_client.py show <id>
    bridge_client.py list
    bridge_client.py cancel <id> [<id> ...] [--reason TEXT]   # withdraw queued jobs

Job fields: team, title, ref (project-repo branch or commit; 'auto' = the
current branch of the project checkout, which must be pushed), steps, collect.
Step kinds: build {targets}, run {tool, args, timeout, continue_on_error},
ghidra {script (under team-a/), args} (Team A only), ls {pattern},
files {root, pattern, max_files} (a folder the owner whitelisted in read_roots).
Arg placeholders: {GAME} game install, {OUT} per-job output dir (files matched
by 'collect' globs come back), {SRC} checkout root, {BUILD} build dir,
{EXE} game executable (Team A only).
"""

import argparse
import datetime
import json
import os
import random
import string
import subprocess
import sys
import time

CONF = os.path.expanduser("~/.crreish-bridge.json")
DEFAULT_DIR = os.path.expanduser("~/crreish-bus")


def die(msg):
    print("bridge: " + msg, file=sys.stderr)
    sys.exit(1)


def git(args, cwd, check=True):
    p = subprocess.run(["git"] + args, cwd=cwd, text=True, capture_output=True)
    if check and p.returncode != 0:
        die("git %s failed: %s" % (" ".join(args), p.stderr.strip()))
    return p


def load_conf():
    if not os.path.exists(CONF):
        die("not set up; run: bridge_client.py setup --url <bus repo url> --team team-a|team-b")
    with open(CONF) as f:
        return json.load(f)


def sync(c):
    d, team = c["dir"], c["team"]
    for i in range(5):
        if git(["fetch", "-q", "origin"], d, check=False).returncode == 0:
            break
        time.sleep(2 ** (i + 1))
    if git(["rev-parse", "-q", "--verify", "origin/" + team], d, check=False).returncode == 0:
        git(["checkout", "-q", "-B", team, "origin/" + team], d)
    return d


def cmd_setup(a):
    d = os.path.abspath(os.path.expanduser(a.dir))
    if not os.path.isdir(os.path.join(d, ".git")):
        os.makedirs(d, exist_ok=True)
        git(["init", "-q"], d)
        git(["remote", "add", "origin", a.url], d)
    # Clean room: a team's clone only ever fetches its own branch and the heartbeat,
    # so Team B never has Team A's results on disk (and vice versa).
    git(["config", "--unset-all", "remote.origin.fetch"], d, check=False)
    git(["config", "--add", "remote.origin.fetch", "+refs/heads/%s*:refs/remotes/origin/%s*" % (a.team, a.team)], d)
    git(["config", "--add", "remote.origin.fetch", "+refs/heads/status*:refs/remotes/origin/status*"], d)
    git(["config", "user.name", "Crreish cloud (%s)" % a.team], d)
    git(["config", "user.email", "crreish-cloud@localhost"], d)
    git(["fetch", "-q", "origin"], d, check=False)
    if git(["rev-parse", "-q", "--verify", "origin/" + a.team], d, check=False).returncode != 0:
        git(["checkout", "-q", "--orphan", a.team], d)
        git(["rm", "-rfq", "--ignore-unmatch", "."], d, check=False)
        os.makedirs(os.path.join(d, "jobs"), exist_ok=True)
        with open(os.path.join(d, "jobs", ".keep"), "w") as f:
            f.write("")
        git(["add", "jobs/.keep"], d)
        git(["commit", "-qm", "init %s bus branch" % a.team], d)
        git(["push", "-q", "-u", "origin", a.team], d)
    with open(CONF, "w") as f:
        json.dump({"dir": d, "team": a.team, "url": a.url}, f)
    sync({"dir": d, "team": a.team})
    print("bus ready at %s (branch %s)" % (d, a.team))


def cmd_status(a):
    c = load_conf()
    sync(c)
    p = git(["show", "origin/status:agent.json"], c["dir"], check=False)
    if p.returncode != 0:  # heartbeats from agents before the CRLF fix were stored as "agent.json\r"
        p = git(["show", "origin/status:agent.json\r"], c["dir"], check=False)
    if p.returncode != 0:
        print("PC agent has never reported in (no 'status' branch). Is pc_agent.py running on the PC?")
        return 1
    st = json.loads(p.stdout)
    seen = datetime.datetime.strptime(st["alive_at"], "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=datetime.timezone.utc)
    age = (datetime.datetime.now(datetime.timezone.utc) - seen).total_seconds() / 60
    print(json.dumps(st, indent=2))
    print("last heartbeat %.0f min ago -> %s" % (age, "ALIVE" if age < 40 else "PROBABLY OFFLINE"))
    return 0


def resolve_ref(ref):
    if ref != "auto":
        return ref
    here = os.getcwd()
    br = git(["rev-parse", "--abbrev-ref", "HEAD"], here).stdout.strip()
    git(["fetch", "-q", "origin", br], here, check=False)
    ahead = git(["rev-list", "--count", "origin/%s..HEAD" % br], here, check=False)
    if ahead.returncode != 0 or ahead.stdout.strip() != "0":
        die("ref auto: branch %s has commits not on origin; push first (the PC builds from GitHub)" % br)
    return git(["rev-parse", "HEAD"], here).stdout.strip()


def submit(job):
    c = load_conf()
    job.setdefault("team", c["team"])
    if job["team"] != c["team"]:
        die("this bus clone is set up for %s" % c["team"])
    if "ref" in job:
        job["ref"] = resolve_ref(job["ref"])
    stamp = datetime.datetime.utcnow().strftime("%Y%m%dT%H%M%S")
    job["id"] = "%s-%s-%s" % (stamp, c["team"], "".join(random.choice(string.ascii_lowercase) for _ in range(4)))
    job["created"] = stamp
    d = sync(c)
    with open(os.path.join(d, "jobs", job["id"] + ".json"), "w") as f:
        json.dump(job, f, indent=2)
    git(["add", "jobs"], d)
    git(["commit", "-qm", "job %s: %s" % (job["id"], job.get("title", ""))], d)
    for i in range(5):
        if git(["push", "-q", "origin", "HEAD:" + c["team"]], d, check=False).returncode == 0:
            print(job["id"])
            return job["id"]
        time.sleep(2 ** (i + 1))
        git(["pull", "-q", "--rebase", "origin", c["team"]], d, check=False)
    die("could not push job")


def cmd_submit(a):
    raw = sys.stdin.read() if a.file == "-" else open(a.file).read()
    submit(json.loads(raw))


def cmd_quick(a):
    if not a.command:
        die("quick: give the command after --")
    job = {"title": a.title or " ".join(a.command)[:80], "ref": a.ref, "steps": []}
    if a.build:
        job["steps"].append({"kind": "build", "targets": a.build.split(",")})
    job["steps"].append({"kind": "run", "tool": a.command[0], "args": a.command[1:], "timeout": a.run_timeout})
    if a.collect:
        job["collect"] = a.collect.split(",")
    jid = submit(job)
    if a.wait:
        return wait_for(jid, a.timeout)


def result_path(c, jid):
    return os.path.join(c["dir"], "results", jid + ".json")


def print_result(c, jid):
    with open(result_path(c, jid)) as f:
        r = json.load(f)
    print("== %s  [%s]  %s" % (jid, r["status"].upper(), r.get("title", "")))
    if r.get("commit"):
        print("commit %s" % r["commit"])
    if r.get("note"):
        print("note: %s" % r["note"])
    for i, s in enumerate(r.get("steps", [])):
        print("-- step %d %s exit=%s %ss  %s" % (i, s["kind"], s.get("exit_code"), s.get("seconds", ""), s.get("cmd", "")))
        if s.get("stdout"):
            print(s["stdout"].rstrip())
        if s.get("stderr"):
            print("[stderr]\n" + s["stderr"].rstrip())
    if r.get("files"):
        print("files in %s:" % os.path.join(c["dir"], "results", jid))
        for f in r["files"]:
            print("  " + f)
    for s in r.get("files_not_uploaded", []):
        print("  (not uploaded) %s: %s" % (s["file"], s["why"]))
    return 0 if r["status"] == "ok" else 2


def wait_for(jid, timeout):
    c = load_conf()
    t0 = time.time()
    while True:
        sync(c)
        if os.path.exists(result_path(c, jid)):
            return print_result(c, jid)
        if time.time() - t0 > timeout:
            print("timed out waiting for %s (check 'status'; the job stays queued)" % jid)
            return 3
        time.sleep(20)


def cmd_wait(a):
    return wait_for(a.id, a.timeout)


def cmd_show(a):
    c = load_conf()
    sync(c)
    if not os.path.exists(result_path(c, a.id)):
        print("%s: no result yet" % a.id)
        return 3
    return print_result(c, a.id)


def cmd_cancel(a):
    """Withdraw queued jobs: writes a 'cancelled' result so the PC agent skips them. A job the agent has
    already started still runs; its own result then fails to push and the cancellation stands."""
    c = load_conf()
    d = sync(c)
    done = []
    for jid in a.ids:
        if not os.path.exists(os.path.join(d, "jobs", jid + ".json")):
            print("%s: no such job on %s" % (jid, c["team"]))
            continue
        rp = result_path(c, jid)
        if os.path.exists(rp):
            print("%s: already has a result, not cancelled" % jid)
            continue
        os.makedirs(os.path.dirname(rp), exist_ok=True)
        with open(rp, "w") as f:
            json.dump({"id": jid, "team": c["team"], "status": "cancelled", "note": a.reason,
                       "finished": datetime.datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ"), "steps": []}, f, indent=2)
        done.append(jid)
    if not done:
        return 1
    git(["add", "results"], d)
    git(["commit", "-qm", "cancel %s" % " ".join(done)], d)
    for i in range(5):
        if git(["push", "-q", "origin", "HEAD:" + c["team"]], d, check=False).returncode == 0:
            print("cancelled: " + " ".join(done))
            return 0
        time.sleep(2 ** (i + 1))
        git(["pull", "-q", "--rebase", "origin", c["team"]], d, check=False)
    die("could not push cancellation")


def cmd_list(a):
    c = load_conf()
    d = sync(c)
    jobs = sorted(n[:-5] for n in os.listdir(os.path.join(d, "jobs")) if n.endswith(".json"))
    for jid in jobs[-a.n:]:
        rp = result_path(c, jid)
        st = json.load(open(rp))["status"] if os.path.exists(rp) else "queued"
        title = json.load(open(os.path.join(d, "jobs", jid + ".json"))).get("title", "")
        print("%-40s %-9s %s" % (jid, st, title))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    p = sp.add_parser("setup"); p.add_argument("--url", required=True); p.add_argument("--team", required=True, choices=["team-a", "team-b"]); p.add_argument("--dir", default=DEFAULT_DIR); p.set_defaults(f=cmd_setup)
    p = sp.add_parser("status"); p.set_defaults(f=cmd_status)
    p = sp.add_parser("submit"); p.add_argument("file"); p.set_defaults(f=cmd_submit)
    p = sp.add_parser("quick"); p.add_argument("--title"); p.add_argument("--ref", default="auto"); p.add_argument("--build"); p.add_argument("--collect"); p.add_argument("--run-timeout", type=int, default=3600); p.add_argument("--wait", action="store_true"); p.add_argument("--timeout", type=int, default=7200); p.add_argument("command", nargs=argparse.REMAINDER); p.set_defaults(f=cmd_quick)
    p = sp.add_parser("wait"); p.add_argument("id"); p.add_argument("--timeout", type=int, default=7200); p.set_defaults(f=cmd_wait)
    p = sp.add_parser("show"); p.add_argument("id"); p.set_defaults(f=cmd_show)
    p = sp.add_parser("cancel"); p.add_argument("ids", nargs="+"); p.add_argument("--reason", default="superseded"); p.set_defaults(f=cmd_cancel)
    p = sp.add_parser("list"); p.add_argument("-n", type=int, default=30); p.set_defaults(f=cmd_list)
    a = ap.parse_args()
    if a.cmd == "quick" and a.command and a.command[0] == "--":
        a.command = a.command[1:]
    sys.exit(a.f(a) or 0)


if __name__ == "__main__":
    main()
