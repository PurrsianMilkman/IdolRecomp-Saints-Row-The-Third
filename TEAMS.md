# Teams and sessions (cloud phase, from 2026-09-30)

The project is **un-paused** as of 2026-09-30: the owner asked for work to continue on Anthropic's servers,
with two separate teams and one session managing both. The pause banners at the top of
`team-a/HANDOFF.md` and `team-b/HANDOFF.md` are historical from this date on.

## Who does what

| Session | Branch | Reads | Writes |
|---|---|---|---|
| **Manager** (`session_011LDv9wRaDB64XptUsyvxmx`) | `claude/project-crreish-teams-imlxay` (integration) | everything | `TEAMS.md`, `bridge/`, merges, spec sync |
| **Team A** (specs, `session_01HKD2RtWHkUAmraBrnepDmd`) | `claude/crreish-team-a` | everything | `team-a/`, `review/` |
| **Team B** (build, `session_01GDLnHaxBfXDAc7uTdrRLZq`) | `claude/crreish-team-b` | `team-b/`, `bridge/`, root docs only | `team-b/`, `.github/` |

Team B's session is started with a sparse checkout (`team-b/` and `bridge/` only), so `team-a/HANDOFF.md`,
`team-a/WALLS.md` and Team A's bridge results are not even on its disk. The clean-room rules in
`CLOUD-START.md` still apply in full.

## How work flows

1. **Team A** writes or corrects `team-a/spec-*.md` and pushes to its branch.
2. **The manager** reviews the spec change, merges Team A's branch into the integration branch, and copies
   each changed `team-a/spec-*.md` to `team-b/` (the copies are kept identical). Team B only ever receives
   specs this way.
3. **Team B** merges the integration branch into its own branch to pick up new specs, implements CONFIRMED
   items only, and pushes.
4. **The manager** merges Team B's branch into the integration branch after checking that its tests pass.
5. Requests go the other way as short written asks: Team B → manager → Team A ("need a spec for
   `fade_is_fully_faded_out`"). Team B never receives Team A's reasoning, only the resulting spec.

The manager talks to the team sessions with session messages and keeps the queue below current.

## The PC bridge

Game files and the executable stay on the owner's PC. Sessions reach them through `bridge/`
(see `bridge/README.md`): jobs go through a private bus repository, the PC builds the pushed branch and runs
it against the real data, and text results come back. Team A can also run headless Ghidra scripts from
`team-a/ghidra/`.

Before relying on it, check it is up: `python3 bridge/bridge_client.py status`. When the PC agent is
offline, jobs wait in the queue; work on the items below that need no game data.

## Work queue

Taken from the resume notes in both HANDOFFs and `team-b/STATE.md` (§9.143).

**Team B**
1. Portable build: the platform-independent libraries and `tests/synthetic_*_test.cpp` built with GCC on
   Linux, plus a GitHub Actions workflow running them. (No game data needed.)
2. The mission-driving blockers from §9.143: `fade_is_fully_faded_out`, `fade_is_fully_faded_in`,
   `zscene_is_loaded`, then `vint_is_std_res`. Implement what `spec-lua-api-behaviour.md` marks CONFIRMED;
   ask for specs where it doesn't. Re-run the mission-driving pass on the PC through the bridge.
3. `sr3vintdoc` reader from `spec-vint-doc-format.md` (the re-dispatch brief in `team-b/HANDOFF.md` §C),
   validated on the PC against `interface_startup.vpp_pc` and `interface.vpp_pc`.

**Team A**
1. Spec consistency review into `review/spec-consistency.md` (no game data needed).
2. Specs for Team B's mission-driving blockers above, following `team-b/results/verdict_stub_hits_with_missions.tsv`,
   using Ghidra scripts run through the bridge.
3. Continue the next spec tranches in the order that ranking gives.

**Held:** the `.czn_pc` object-stream interior stays on hold until the owner decides.

## Which model for which job

The team leads run on Opus 5.5 and orchestrate. Each subagent they dispatch gets the model that suits its
task (the Agent tool's `model`: `fable`, `opus`, `sonnet`, `haiku`). This replaces the pre-pause habit of
dispatching every agent on the same model.

| Model | Strength | Use it for |
|---|---|---|
| **Fable 5.1** (`fable`) | Most capable; the deepest reasoning and long-horizon work | Team A: interpreting disassembly into behaviour (state machines, walkers, layouts), adversarial re-derivation against the executable, resolving conflicting evidence. Either team: root-causing hard bugs that resisted a first attempt (e.g. the clmesh NaN/`Fog_dist` artifact). |
| **Opus 5.5** (`opus`) | The default: strong reasoning and coding | Writing specs from Ghidra dumps; non-trivial C++ (new format readers, host engine-state modelling); independent verification of another agent's work (rebuild + rerun); reviewing diffs before merge. |
| **Sonnet 5.5** (`sonnet`) | Fast, capable everyday coding | Well-specified implementation: CONFIRMED-spec Lua functions with unit tests, fuzz harnesses, tool ports, CI, scripts for comparing results, bridge job files; desk reviews of cross-references and consistency. |
| **Haiku 4.5** (`haiku`) | Cheapest, fastest | Mechanical work: greps and censuses, spec-copy/format checks, bulk renames, reading large bridge outputs and summarising or tabulating them. |

Rules: verification of an agent's work is done by a model at least as capable as the one that did it. When
a task turns out harder than expected (two failed attempts, or conflicting evidence), escalate it one tier
instead of retrying on the same model.

## Data and content rules (owner decision, 2026-10-01)

- **The owner's own data is not private.** Their saves and profiles (including anything typed in the game),
  their machine paths, and bridge outputs from their PC may be used, committed and shared freely. Validation
  tools that read the saves are allowed in bridge jobs. This replaces the earlier "no player-authored
  content" rule.
- **Still excluded, for copyright and clean-room reasons rather than privacy:** verbatim game content (script
  or source bodies, UI and dialogue text, comments, bulk asset or string dumps). Functional identifiers the
  engine or scripts resolve by name (function names, table keys, enum and kind values, slot names) remain
  allowed, even as full lists.
- **Still excluded, as third parties' personal data:** developer usernames, developer machine paths, and real
  people's names found in the files.

## Reporting (owner decision, 2026-10-01)

Teams report to the manager with status only: done (with commit ids and test results), in progress, and
blocked (and on what). Technical detail stays in specs, HANDOFF, STATE.md and commit messages. Decisions and
reviews still go to the manager straight away. The manager gives the owner an overall project status, not
per-item detail.
