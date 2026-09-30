#!/usr/bin/env python3
"""Crreish bridge: PC agent.

Runs on the owner's PC, next to the game install. It watches a git "bus"
repository for job files written by Claude Code cloud sessions, runs each job
against the local game files, and pushes the text results back.

Jobs never carry code. A job names a git ref of the project repository; the
agent checks that ref out, builds the CMake targets the job asks for, and runs
the resulting tools with the job's arguments. So the only code that runs here
is code already pushed to the project repository you configured.

Python 3.8+ standard library only. See bridge/README.md for setup.

    python pc_agent.py --config pc_config.json          # run forever
    python pc_agent.py --config pc_config.json --once   # process pending jobs, then exit
"""

import argparse
import datetime
import fnmatch
import glob
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import time
import traceback

JOB_ID_RE = re.compile(r"^[A-Za-z0-9._-]{1,120}$")
TARGET_RE = re.compile(r"^[A-Za-z0-9_.+-]{1,100}$")
REF_RE = re.compile(r"^[A-Za-z0-9._/-]{1,200}$")
TEAMS = ("team-a", "team-b")

DEFAULTS = {
    "poll_seconds": 30,
    "heartbeat_minutes": 15,
    "teams": ["team-a", "team-b"],
    "allowed_ref_patterns": ["claude/*", "main"],
    "cmake_configure_args": ["-G", "Visual Studio 17 2022", "-A", "x64"],
    "build_config": "Release",
    "default_timeout_seconds": 3600,
    "max_timeout_seconds": 6 * 3600,
    "max_inline_output_bytes": 64 * 1024,
    "max_artifact_bytes": 4 * 1024 * 1024,
    "max_job_upload_bytes": 24 * 1024 * 1024,
    "artifact_extensions": [".txt", ".tsv", ".csv", ".json", ".md", ".log", ".hlsl", ".asm"],
    "tools": {},
    "ghidra": None,
}


def now_iso():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def log(msg):
    print("[%s] %s" % (now_iso(), msg), flush=True)


class JobRejected(Exception):
    pass


# --------------------------------------------------------------------------- git helpers

def git(args, cwd, check=True, capture=True):
    proc = subprocess.run(["git"] + args, cwd=cwd, text=True,
                          stdout=subprocess.PIPE if capture else None,
                          stderr=subprocess.PIPE if capture else None)
    if check and proc.returncode != 0:
        raise RuntimeError("git %s failed (%d): %s" % (" ".join(args), proc.returncode,
                                                       (proc.stderr or "").strip()))
    return proc


def git_identity(cwd):
    git(["config", "user.name", "Crreish PC agent"], cwd)
    git(["config", "user.email", "crreish-pc-agent@localhost"], cwd)


class Bus:
    """Local clone of the bus repository. One branch per team, plus a 'status' branch."""

    def __init__(self, url, path):
        self.url = url
        self.path = path
        if not os.path.isdir(os.path.join(path, ".git")):
            os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
            log("cloning bus %s -> %s" % (url, path))
            git(["clone", "--no-checkout", url, path], cwd=os.path.dirname(os.path.abspath(path)))
        git_identity(path)

    def fetch(self):
        git(["fetch", "--prune", "origin"], self.path)

    def remote_branches(self):
        out = git(["branch", "-r", "--format=%(refname:short)"], self.path).stdout
        return {line.strip()[len("origin/"):] for line in out.splitlines() if line.strip().startswith("origin/")}

    def checkout(self, branch):
        if branch in self.remote_branches():
            git(["checkout", "-f", "-B", branch, "origin/" + branch], self.path)
        else:
            git(["checkout", "-f", "--orphan", branch], self.path)
            git(["rm", "-rf", "--quiet", "--ignore-unmatch", "."], self.path, check=False)
        git(["clean", "-fdq"], self.path)

    def commit_and_push(self, branch, message, paths):
        for attempt in range(6):
            git(["add", "-A", "--"] + paths, self.path)
            if git(["diff", "--cached", "--quiet"], self.path, check=False).returncode == 0:
                return
            git(["commit", "-q", "-m", message], self.path)
            if git(["push", "-q", "origin", "HEAD:" + branch], self.path, check=False).returncode == 0:
                return
            log("push rejected, rebasing (attempt %d)" % (attempt + 1))
            self.fetch()
            if git(["rebase", "-q", "origin/" + branch], self.path, check=False).returncode != 0:
                git(["rebase", "--abort"], self.path, check=False)
                # Our files are only ever new files under results/, so a hard reset plus
                # re-adding is safe: the caller re-writes them on the next poll if lost.
                raise RuntimeError("rebase conflict on bus branch %s" % branch)
            if git(["push", "-q", "origin", "HEAD:" + branch], self.path, check=False).returncode == 0:
                return
            time.sleep(2 ** attempt)
        raise RuntimeError("could not push to bus branch %s" % branch)

    def force_status(self, payload):
        """Heartbeat on the 'status' branch: always a single parentless commit, force-pushed,
        built with plumbing so the working tree is never touched."""
        data = json.dumps(payload, indent=2) + "\n"
        blob = subprocess.run(["git", "hash-object", "-w", "--stdin"], cwd=self.path, input=data,
                              text=True, capture_output=True, check=True).stdout.strip()
        tree = subprocess.run(["git", "mktree"], cwd=self.path, input="100644 blob %s\tagent.json\n" % blob,
                              text=True, capture_output=True, check=True).stdout.strip()
        commit = git(["commit-tree", tree, "-m", "PC agent heartbeat"], self.path).stdout.strip()
        git(["push", "-q", "-f", "origin", commit + ":refs/heads/status"], self.path, check=False)


# --------------------------------------------------------------------------- agent

class Agent:
    def __init__(self, cfg):
        self.cfg = dict(DEFAULTS)
        self.cfg.update(cfg)
        c = self.cfg
        for key in ("bus_url", "project_url", "game_dir", "work_dir"):
            if not c.get(key):
                raise SystemExit("config: '%s' is required" % key)
        if not os.path.isdir(c["game_dir"]):
            raise SystemExit("config: game_dir does not exist: %s" % c["game_dir"])
        self.work = os.path.abspath(c["work_dir"])
        os.makedirs(self.work, exist_ok=True)
        self.bus = Bus(c["bus_url"], os.path.join(self.work, "bus"))
        self.last_heartbeat = 0.0
        self.current = None

    # ---- secrets we never want to leave the PC in output text
    def scrub(self, text):
        repl = []
        for key, token in (("game_dir", "{GAME}"), ("exe_path", "{EXE}"), ("work_dir", "{WORK}")):
            v = self.cfg.get(key)
            if v:
                repl.append((os.path.abspath(v), token))
        repl.append((os.path.expanduser("~"), "{HOME}"))
        repl.sort(key=lambda p: -len(p[0]))
        for src, token in repl:
            for variant in {src, src.replace("\\", "/"), src.replace("/", "\\")}:
                if variant:
                    text = text.replace(variant, token)
        return text

    def heartbeat(self, force=False):
        if not force and time.time() - self.last_heartbeat < self.cfg["heartbeat_minutes"] * 60:
            return
        self.last_heartbeat = time.time()
        try:
            self.bus.force_status({
                "alive_at": now_iso(),
                "host_os": platform.system(),
                "teams": self.cfg["teams"],
                "busy_with": self.current,
                "poll_seconds": self.cfg["poll_seconds"],
                "ghidra_configured": bool(self.cfg.get("ghidra")),
                "configured_tools": sorted(self.cfg.get("tools", {}).keys()),
            })
        except Exception as e:  # heartbeat is best effort
            log("heartbeat failed: %s" % e)

    def poll_once(self):
        self.bus.fetch()
        branches = self.bus.remote_branches()
        did = 0
        for team in self.cfg["teams"]:
            if team not in branches:
                continue
            self.bus.checkout(team)
            jobs_dir = os.path.join(self.bus.path, "jobs")
            if not os.path.isdir(jobs_dir):
                continue
            for name in sorted(os.listdir(jobs_dir)):
                if not name.endswith(".json"):
                    continue
                job_id = name[:-5]
                if os.path.exists(os.path.join(self.bus.path, "results", job_id + ".json")):
                    continue
                self.process(team, job_id, os.path.join(jobs_dir, name))
                did += 1
                self.bus.checkout(team)  # refresh after our own push
        return did

    def process(self, team, job_id, path):
        log("job %s/%s: start" % (team, job_id))
        self.current = "%s/%s" % (team, job_id)
        self.heartbeat(force=True)
        self.bus.checkout(team)
        result = {"id": job_id, "team": team, "started": now_iso(), "status": "error", "steps": []}
        out_dir = tempfile.mkdtemp(prefix="crreish-%s-" % job_id, dir=self.work)
        try:
            with open(path, encoding="utf-8") as f:
                job = json.load(f)
            self.validate(team, job_id, job)
            result["title"] = job.get("title", "")
            self.run_job(team, job, result, out_dir)
        except JobRejected as e:
            result["status"] = "rejected"
            result["note"] = str(e)
        except Exception as e:
            result["status"] = "error"
            result["note"] = self.scrub("%s\n%s" % (e, traceback.format_exc()[-4000:]))
        result["finished"] = now_iso()
        self.bus.checkout(team)
        self.write_result(team, job_id, result, out_dir)
        shutil.rmtree(out_dir, ignore_errors=True)
        self.current = None
        log("job %s/%s: %s" % (team, job_id, result["status"]))

    # ---- validation
    def validate(self, team, job_id, job):
        if not JOB_ID_RE.match(job_id) or job.get("id") != job_id:
            raise JobRejected("job id must match its file name and %s" % JOB_ID_RE.pattern)
        if job.get("team") != team:
            raise JobRejected("job.team must be '%s' (the bus branch it was posted on)" % team)
        steps = job.get("steps")
        if not isinstance(steps, list) or not steps or len(steps) > 20:
            raise JobRejected("job.steps must be a list of 1..20 steps")
        needs_ref = any(s.get("kind") in ("build", "ghidra") or
                        (s.get("kind") == "run" and s.get("tool") not in self.cfg["tools"]) for s in steps)
        ref = job.get("ref")
        if needs_ref:
            if not isinstance(ref, str) or not REF_RE.match(ref) or ".." in ref:
                raise JobRejected("job.ref (a branch or commit of the project repo) is required")
            is_sha = re.fullmatch(r"[0-9a-f]{7,40}", ref) is not None
            if not is_sha and not any(fnmatch.fnmatch(ref, p) for p in self.cfg["allowed_ref_patterns"]):
                raise JobRejected("ref '%s' does not match allowed_ref_patterns %s"
                                  % (ref, self.cfg["allowed_ref_patterns"]))
        for s in steps:
            kind = s.get("kind")
            if kind == "build":
                t = s.get("targets")
                if not isinstance(t, list) or not t or not all(isinstance(x, str) and TARGET_RE.match(x) for x in t):
                    raise JobRejected("build.targets must be a list of CMake target names")
            elif kind == "run":
                if not isinstance(s.get("tool"), str) or not TARGET_RE.match(s["tool"]):
                    raise JobRejected("run.tool must be a CMake target or a configured tool name")
                tool_cfg = self.cfg["tools"].get(s["tool"])
                if tool_cfg and team not in tool_cfg.get("teams", list(TEAMS)):
                    raise JobRejected("tool '%s' is not enabled for %s" % (s["tool"], team))
                self.check_args(team, s.get("args", []))
            elif kind == "ghidra":
                if team != "team-a":
                    raise JobRejected("ghidra steps are Team A only (clean-room rule)")
                if not self.cfg.get("ghidra"):
                    raise JobRejected("ghidra is not configured on this PC")
                sp = s.get("script", "")
                if not isinstance(sp, str) or not sp.startswith("team-a/") or ".." in sp:
                    raise JobRejected("ghidra.script must be a path under team-a/ in the job's ref")
                self.check_args(team, s.get("args", []))
            elif kind == "ls":
                if not isinstance(s.get("pattern", "*"), str) or ".." in s.get("pattern", ""):
                    raise JobRejected("ls.pattern must be a relative glob without '..'")
            else:
                raise JobRejected("unknown step kind %r (build, run, ghidra, ls)" % kind)
        collect = job.get("collect", [])
        if not isinstance(collect, list) or any((not isinstance(p, str)) or ".." in p or os.path.isabs(p) for p in collect):
            raise JobRejected("collect must be a list of relative globs inside {OUT}")

    def check_args(self, team, args):
        if not isinstance(args, list) or not all(isinstance(a, str) for a in args) or len(args) > 200:
            raise JobRejected("args must be a list of strings")
        if team != "team-a" and any("{EXE}" in a for a in args):
            raise JobRejected("{EXE} is Team A only (clean-room rule)")

    # ---- execution
    def subst(self, team, arg, ctx):
        out = arg.replace("{GAME}", os.path.abspath(self.cfg["game_dir"]))
        out = out.replace("{OUT}", ctx["out"])
        if ctx.get("src"):
            out = out.replace("{SRC}", ctx["src"])
        if ctx.get("build"):
            out = out.replace("{BUILD}", ctx["build"])
        if team == "team-a" and self.cfg.get("exe_path"):
            out = out.replace("{EXE}", os.path.abspath(self.cfg["exe_path"]))
        return out

    def prepare_source(self, team, ref, result):
        src = os.path.join(self.work, "src-" + team)
        if not os.path.isdir(os.path.join(src, ".git")):
            git(["clone", "-q", self.cfg["project_url"], src], cwd=self.work)
        git(["fetch", "-q", "--prune", "origin", "+refs/heads/*:refs/remotes/origin/*"], src)
        target = ref
        if git(["rev-parse", "--verify", "-q", "origin/" + ref], src, check=False).returncode == 0:
            target = "origin/" + ref
        git(["checkout", "-q", "-f", "--detach", target], src)
        git(["clean", "-q", "-fdx"], src)
        result["commit"] = git(["rev-parse", "HEAD"], src).stdout.strip()
        return src

    def run_job(self, team, job, result, out_dir):
        ctx = {"out": out_dir, "src": None, "build": None}
        needs_src = job.get("ref") and any(s["kind"] in ("build", "ghidra", "run") for s in job["steps"])
        if needs_src:
            ctx["src"] = self.prepare_source(team, job["ref"], result)
            ctx["build"] = os.path.join(self.work, "build-" + team)
        status = "ok"
        for i, step in enumerate(job["steps"]):
            rec = {"kind": step["kind"]}
            result["steps"].append(rec)
            timeout = min(int(step.get("timeout", self.cfg["default_timeout_seconds"])),
                          self.cfg["max_timeout_seconds"])
            if step["kind"] == "ls":
                rec.update(self.do_ls(step.get("pattern", "*")))
                continue
            cmd, cwd = self.command_for(team, step, ctx)
            rec["cmd"] = self.scrub(" ".join(cmd))
            rc, out, err, dur = self.execute(cmd, cwd, timeout, os.path.join(out_dir, "_step%02d" % i))
            rec.update({"exit_code": rc, "seconds": round(dur, 1)})
            rec["stdout"], full_out = self.clip(out)
            rec["stderr"], full_err = self.clip(err)
            if full_out:
                self.stash(out_dir, "step%02d.stdout.txt" % i, out)
            if full_err:
                self.stash(out_dir, "step%02d.stderr.txt" % i, err)
            if rc != 0:
                status = "failed"
                if not step.get("continue_on_error"):
                    break
        result["status"] = status
        result["_collect"] = job.get("collect", [])

    def command_for(self, team, step, ctx):
        kind = step["kind"]
        if kind == "build":
            src_dir = os.path.join(ctx["src"], self.cfg.get("cmake_source_subdir", "team-b"))
            build = ctx["build"]
            if not os.path.exists(os.path.join(build, "CMakeCache.txt")):
                os.makedirs(build, exist_ok=True)
                rc = subprocess.run(["cmake", "-S", src_dir, "-B", build] + list(self.cfg["cmake_configure_args"]),
                                    text=True, capture_output=True)
                if rc.returncode != 0:
                    shutil.rmtree(build, ignore_errors=True)
                    raise RuntimeError("cmake configure failed:\n" + self.scrub(rc.stdout[-3000:] + rc.stderr[-3000:]))
            # Re-point an existing cache at this team's checkout (the path is stable per team).
            return (["cmake", "--build", build, "--config", self.cfg["build_config"], "--parallel",
                     "--target"] + step["targets"], None)
        if kind == "run":
            tool_cfg = self.cfg["tools"].get(step["tool"])
            args = [self.subst(team, a, ctx) for a in step.get("args", [])]
            if tool_cfg:
                base = [self.subst(team, a, ctx) for a in tool_cfg["command"]]
                return base + args, tool_cfg.get("cwd") or ctx["out"]
            exe = self.find_built(ctx, step["tool"])
            return [exe] + args, ctx["out"]
        if kind == "ghidra":
            g = self.cfg["ghidra"]
            script = os.path.join(ctx["src"], *step["script"].split("/"))
            if not os.path.isfile(script):
                raise RuntimeError("ghidra script not found in ref: %s" % step["script"])
            args = [self.subst(team, a, ctx) for a in step.get("args", [])]
            return ([g["analyze_headless"], g["project_dir"], g["project_name"],
                     "-process", g["program"], "-noanalysis", "-readOnly",
                     "-scriptPath", os.path.dirname(script),
                     "-postScript", os.path.basename(script)] + args, ctx["out"])
        raise JobRejected("unknown step kind")

    def find_built(self, ctx, tool):
        if not ctx.get("build"):
            raise JobRejected("run of a built tool needs job.ref and a build step")
        cfgname = self.cfg["build_config"]
        for cand in (os.path.join(ctx["build"], cfgname, tool + ".exe"), os.path.join(ctx["build"], tool + ".exe"),
                     os.path.join(ctx["build"], cfgname, tool), os.path.join(ctx["build"], tool)):
            if os.path.isfile(cand):
                return cand
        raise RuntimeError("built tool '%s' not found; add it to a build step first" % tool)

    def execute(self, cmd, cwd, timeout, stem):
        t0 = time.time()
        with open(stem + ".out", "w+b") as fo, open(stem + ".err", "w+b") as fe:
            try:
                p = subprocess.run(cmd, cwd=cwd, stdout=fo, stderr=fe, timeout=timeout)
                rc = p.returncode
            except subprocess.TimeoutExpired:
                rc = -9
                fe.write(("\n[bridge] killed after %d s timeout\n" % timeout).encode())
            except FileNotFoundError as e:
                rc = -2
                fe.write(("[bridge] %s\n" % e).encode())
            fo.seek(0)
            fe.seek(0)
            out = fo.read().decode("utf-8", "replace")
            err = fe.read().decode("utf-8", "replace")
        return rc, self.scrub(out), self.scrub(err), time.time() - t0

    def clip(self, text):
        n = self.cfg["max_inline_output_bytes"]
        if len(text) <= n:
            return text, False
        half = n // 2
        return (text[:half] + "\n[bridge] ... %d chars omitted, full text in files ...\n" % (len(text) - n)
                + text[-half:]), True

    def stash(self, out_dir, name, text):
        os.makedirs(os.path.join(out_dir, "_bridge"), exist_ok=True)
        with open(os.path.join(out_dir, "_bridge", name), "w", encoding="utf-8") as f:
            f.write(text)

    def do_ls(self, pattern):
        root = os.path.abspath(self.cfg["game_dir"])
        hits = []
        for p in sorted(glob.glob(os.path.join(root, pattern), recursive=True))[:5000]:
            if os.path.isfile(p):
                hits.append("%12d  %s" % (os.path.getsize(p), os.path.relpath(p, root).replace("\\", "/")))
        return {"exit_code": 0, "stdout": "\n".join(hits) + "\n", "stderr": "", "count": len(hits)}

    # ---- results
    def write_result(self, team, job_id, result, out_dir):
        res_dir = os.path.join(self.bus.path, "results")
        art_dir = os.path.join(res_dir, job_id)
        os.makedirs(res_dir, exist_ok=True)
        files, skipped, total = [], [], 0
        cands = []
        if os.path.isdir(os.path.join(out_dir, "_bridge")):
            cands += [(os.path.join(out_dir, "_bridge", n), n) for n in sorted(os.listdir(os.path.join(out_dir, "_bridge")))]
        for pat in result.pop("_collect", []):
            for p in sorted(glob.glob(os.path.join(out_dir, pat), recursive=True)):
                if os.path.isfile(p) and "_bridge" not in p and not os.path.basename(p).startswith("_step"):
                    cands.append((p, os.path.relpath(p, out_dir).replace("\\", "/")))
        exts = [e.lower() for e in self.cfg["artifact_extensions"]]
        for path, rel in cands:
            size = os.path.getsize(path)
            with open(path, "rb") as f:
                head = f.read(8192)
            why = None
            if os.path.splitext(rel)[1].lower() not in exts:
                why = "extension not in artifact_extensions"
            elif b"\0" in head:
                why = "binary content"
            elif size > self.cfg["max_artifact_bytes"]:
                why = "larger than max_artifact_bytes (%d)" % size
            elif total + size > self.cfg["max_job_upload_bytes"]:
                why = "job upload budget exhausted"
            if why:
                skipped.append({"file": rel, "bytes": size, "why": why})
                continue
            dst = os.path.join(art_dir, *rel.split("/"))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(path, encoding="utf-8", errors="replace") as f:
                text = f.read()
            with open(dst, "w", encoding="utf-8") as f:
                f.write(self.scrub(text))
            total += size
            files.append(rel)
        result["files"] = files
        if skipped:
            result["files_not_uploaded"] = skipped
        with open(os.path.join(res_dir, job_id + ".json"), "w", encoding="utf-8") as f:
            json.dump(result, f, indent=2)
        self.bus.commit_and_push(team, "result %s: %s" % (job_id, result["status"]), ["results"])

    def loop(self, once=False):
        self.heartbeat(force=True)
        while True:
            try:
                n = self.poll_once()
                if n:
                    self.heartbeat(force=True)
            except Exception as e:
                log("poll error: %s" % e)
            if once:
                return
            self.heartbeat()
            time.sleep(self.cfg["poll_seconds"])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "pc_config.json"))
    ap.add_argument("--once", action="store_true", help="process pending jobs once, then exit")
    a = ap.parse_args()
    with open(a.config, encoding="utf-8") as f:
        cfg = json.load(f)
    Agent(cfg).loop(once=a.once)


if __name__ == "__main__":
    main()
