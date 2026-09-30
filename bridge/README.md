# Crreish bridge: cloud sessions ↔ your PC

The game files never leave your PC. Claude Code sessions running on Anthropic's servers do the
thinking, writing and building. When they need real game data, they send a **job** to your PC. A small
agent on your PC runs the job against your game install and sends back **text results** only.

```
 Anthropic cloud                           GitHub (private bus repo)                Your PC
 ┌────────────────────┐   push job     ┌──────────────────────────┐   poll    ┌────────────────────────┐
 │ Team A session     │ ─────────────► │ branch team-a: jobs/     │ ◄──────── │ pc_agent.py            │
 │ Team B session     │                │ branch team-b: jobs/     │           │  - checks out the ref  │
 │ Manager session    │ ◄───────────── │   results/<id>.json      │ ────────► │  - builds (MSVC/CMake) │
 └────────────────────┘   pull result  │ branch status: heartbeat │   push    │  - runs vs. game files │
                                       └──────────────────────────┘           └────────────────────────┘
```

Neither side needs an open port. Both only talk to GitHub.

## What a job can do

A job never carries code. It names a **ref** (a branch or commit) of the project repository. The agent
checks that ref out, builds the CMake targets it asks for, and runs them. So the only code that runs on
your PC is code that has already been pushed to `Project-Crreish`, where you can read it.

| Step kind | What it does | Who |
|---|---|---|
| `build` | `cmake --build` of the listed targets from `team-b/` at the job's ref | both teams |
| `run` | runs a built tool (or one you configured under `tools`) with the job's arguments | both teams |
| `ghidra` | runs a Ghidra script from `team-a/` at the job's ref, headless and `-readOnly`, against your Ghidra project | Team A only |
| `ls` | lists files in the game folder (names and sizes) | both teams |

Arguments can use placeholders: `{GAME}` (game folder), `{OUT}` (a fresh output folder for this job),
`{SRC}`, `{BUILD}`, and `{EXE}` (the game executable, Team A only).

What comes back: exit codes and stdout/stderr (clipped to 64 KB inline, the full text as a file), and any
text files the tool wrote into `{OUT}` that match the job's `collect` globs. Binary files, and files with
an extension outside `artifact_extensions`, are listed but not uploaded, so textures, models, audio and
archives can't come back. Local paths are replaced by `{GAME}`, `{EXE}`, `{WORK}` and `{HOME}` in
everything that is uploaded.

**Clean room:** each team has its own bus branch. Team B sessions only clone the `team-b` branch, so they
never see Team A's disassembly output. Ghidra jobs and `{EXE}` are refused on the `team-b` branch.

## Setting it up on your PC (once, about 10 minutes)

1. **Create a private repository** on GitHub for the bus, e.g. `PurrsianMilkman/Project-Crreish-bus`.
   Make it **private**: results are derived from your game files and from the executable. Tick
   "Add a README" so it isn't empty.
2. **Let Claude reach it.** If the Claude GitHub App is installed on selected repositories only, add the
   new repository at <https://github.com/apps/claude/installations/select_target>.
3. **Install** Python 3.8+ (python.org, tick "Add to PATH"), Git for Windows, CMake, and Visual Studio 2022
   with the C++ workload (you already have these if you built Team B's tools before). Make sure
   `git push` works from a terminal without a password prompt (Git for Windows' credential manager does
   this after one sign-in).
4. **Configure:** in this `bridge` folder, copy `pc_config.example.json` to `pc_config.json` and set:
   - `bus_url`: the private repository from step 1;
   - `game_dir`: your Saints Row: The Third folder; `exe_path`: the game executable;
   - `work_dir`: an empty folder with a few GB free (checkouts and builds go there);
   - `ghidra`: the paths to `analyzeHeadless.bat` and to Team A's Ghidra project, or `null` if you
     don't want Team A to run Ghidra scripts.
5. **Start it:** double-click `start_pc_agent.bat`. Leave the window open while you want the teams to be
   able to use your game files. It checks for jobs every 30 seconds and reports a heartbeat every 15
   minutes. Close the window to stop it. Jobs posted while it is off wait in the queue.

To test it without the cloud, from a terminal in this folder:

```
python pc_agent.py --config pc_config.json --once
```

## Safety, plainly

Running the agent means **code pushed to your project repository by Claude sessions gets built and run on
your PC**. That is the point of the bridge, but it is still code execution. To keep it contained:

- Only refs matching `allowed_ref_patterns` (default `claude/*` and `main`) of `project_url` are built.
  Nobody can make your PC run code that isn't in that repository.
- Nothing runs through a shell, and `ghidra` runs with `-readOnly`, so your Ghidra project isn't modified.
- For extra isolation, run the agent under a separate Windows user account that can read the game folder
  but not your personal files, or in a VM.
- Every job and result is a commit in the bus repository, so you have a full audit trail.

## Using it from a cloud session

```
python3 bridge/bridge_client.py setup --url https://github.com/PurrsianMilkman/Project-Crreish-bus --team team-b
python3 bridge/bridge_client.py status          # is the PC agent alive?

# build a tool from the current (pushed) branch and run it against the real data:
python3 bridge/bridge_client.py quick --wait --build lua_host_run --collect '*.tsv' -- \
    lua_host_run {GAME} --out {OUT}

# or a full job file:
python3 bridge/bridge_client.py submit job.json
python3 bridge/bridge_client.py wait <id>
```

Example job file:

```json
{
  "title": "sr3vintdoc population sweep",
  "ref": "auto",
  "steps": [
    {"kind": "build", "targets": ["vintdoc_validate"]},
    {"kind": "run", "tool": "vintdoc_validate", "args": ["{GAME}/packfiles/pc/cache/interface.vpp_pc", "{OUT}"], "timeout": 1800}
  ],
  "collect": ["*.tsv", "*.json"]
}
```

`"ref": "auto"` means the current commit of the checkout you run the client from; it must be pushed,
because your PC builds from GitHub. Results land in `~/crreish-bus/results/<id>.json` and
`~/crreish-bus/results/<id>/`.

## Testing the bridge itself

`bash bridge/tests/selftest.sh` runs the whole loop on Linux with local repositories standing in for
GitHub: it builds a small tool, runs it on a fake game folder, and checks path scrubbing, the binary
filter and the clean-room refusal.
