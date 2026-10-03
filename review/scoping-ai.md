# Scoping note — the AI runtime (NPC decision and behaviour loop)

**Date:** 2026-10-03 · **Type:** scoping note only — no spec content, nothing here is cleared for implementation.
**Question:** what turns a spawned character plus its loaded AI data into a chosen action and then into per-frame behaviour; how big that system is; what is already specified and what is not; and in what order to specify it.
**Method:** read-only Ghidra headless runs against a private copy of the project (`tools\gp_aiscope`, deleted afterwards): call-graph walks up and down from the already-documented anchors, static function-pointer table reads, a push-argument survey at every call site of the request allocator and the two action writers, and instruction counts. Full listings were read for the handful of functions that carry the architecture (per-character update, decision step, chooser wrapper, commit/stage/finish/cancel writers, mode setter). I also read `spec-ai-behavior-format.md` in full, `spec-tables-traffic-ai.md` §1.5/§8–§11, and `spec-lua-api-behaviour.md` §7.30, §18.3, §22, §30.4/§30.7 and §35.2.

**Size figures.** Instruction counts are function-body counts. "Reach" figures count unique functions within N call levels.

**Confidence labels** are the project's usual ones:
- **CONFIRMED**: read directly in this pass.
- **HIGH**: inferred from structure.
- **HYPOTHESIS**: a plausible reading, not checked.

**Naming.** Mode, action and goal names are the engine's own strings, read from the three static tables. They are shipped data.

---

## 0. Verdict up front

**This is real reverse-engineering work, and much bigger than the UI-render path.** The data half is done. The runtime half is almost entirely unspecified.

1. **There are two layers, and only the smaller one was known.**
   - The documented CAE action table and chooser are the **decision layer**: 70 actions, 17 goals, about 3k instructions of core code.
   - Underneath sits an **execution layer** that no spec mentions: a **44-entry AI mode (state) machine**, table `0x012e1538`, with named modes (`Idle`, `Combat Movement`, `Cover stay`, `Flee`, `Panic`, `Veh enter`, …) and eight lifecycle slots each.
   - Each CAE action's category byte is simply the index of the mode that runs it.
   - Only 20 of the 44 modes are reachable from the decision layer at all. The other 24 (flee, cower, panic, vehicle, skydiving, …) are entered by other systems.
2. **The request-id pool is not part of the NPC AI loop.** Goal-driven decisions commit in place, with no id. Ids (kind 3) are attached only to actions *ordered from outside*: Lua, mission/vehicle code, and two forced-action paths inside mode handlers. Their only job is to let the orderer poll or wait for completion.
3. **The data side is complete.** Every AI `.xtbl` is specified, and the behaviour-record assignment chain is CONFIRMED end to end. No RE is needed to load the tables and give each character the right record.

**Rough size:**

| Part | Functions | Instructions |
|---|---|---|
| Decision layer core | ~45 | ~3.2k |
| Measured root set: decision core, 66 action routines and all 44 modes' lifecycle functions | 332 | ~33k own bodies |
| Realistic complete human-AI runtime, including private helpers | ~800–1,200 | ~80–120k |
| Comparison: UI-render core | 60–70 | ~10k |

The complete runtime is therefore about **8–12×** the UI-render core, before pathfinding, driver AI and animation. Pathfinding is partly blocked: the navmesh is a zone section inside `.czn_pc`.

---

## 1. The pipeline: from a world tick to a running behaviour

### 1.0 Shape in one paragraph

The AI runtime is **two layers stacked on one per-character state record**, both hand-written Volition code reached through static function-pointer tables:

- a **decision layer** (the "CAE" combat-action layer, ~70 actions × ~17 goals) that, on a timer, picks *which* action the character should be doing; and
- an **execution layer**, a **44-entry AI mode (state) machine** keyed by a per-character mode byte, whose per-mode handlers run every frame and actually drive animation, movement, firing, cover, vehicle entry and so on.

The decision layer's only output is (a) a filled 0x58-byte action block and (b) a request to switch the character into the mode that the chosen action's *category byte* names. Everything that moves the character happens in the mode handlers. The scripted-request pool is **not** part of this per-frame loop (§1.6).

```
world update 0x00702a50
  └─ 0x0099bba0 → 0x009d94a0 → 0x009d9190 ─┐            (also 0x00702a50 → 0x009ea630 → 0x009e91c0 ─┐)
                                            └─ per-human update 0x0099bcb0 (851 insns) ◄─────────────┘
                                                 └─ human AI update 0x004e8d00 (331)          [also from 0x00af4150 via 0x00952760]
                                                      ├─ (no local authority) proxy/remote branch → 0x004e58a0, 0x004e8310
                                                      ├─ AI-budget throttle (globals 0x013576c6 / 0x013576d0 / 0x013576d4)
                                                      ├─ 0x00507520 (306)  sub-object at char +0x510  (ambient/personality/perception — HYPOTHESIS)
                                                      ├─ 0x004f5b90 (59)   decision timer / gate
                                                      ├─ 0x004f5210 (95)   DECISION STEP
                                                      │     ├─ 0x004f5180 (47)  may-decide gate
                                                      │     ├─ 0x004f3750 (205) goal/action chooser  (spec-ai-behavior-format.md §3.3)
                                                      │     │     ├─ goal table 0x012e22f0 (17 × 0x20, +0x04 goal-test routine)
                                                      │     │     └─ action table 0x012e1c60 (70 × 0x18, +0x08 action-test/setup routine)
                                                      │     ├─ 0x004f4eb0 → 0x004e4b60   clear previous
                                                      │     ├─ 0x004e4130 (46)  REQUEST MODE = action's category byte (table +0x04)
                                                      │     └─ in-place commit of the chosen block into char +0x450 (no request id)
                                                      ├─ 0x004e8310 (248)  post-decision upkeep
                                                      ├─ MODE PROCESS: mode table 0x012e1538 (44 × 0x24), slot +0x0c, indexed by char +0x501
                                                      │     (preceded by 0x004f3a90 / 0x004f28f0 checks on the current action)
                                                      └─ 0x004dec10 (74)  tail
mode handlers ──► 0x004f4f50 (98) "action finished"  (85 callers)  ──► completes the kind-3 request IF the action was a scripted one
```

### 1.1 Frame entry and the per-character AI update

- **`0x004e8d00` (331 insns) — the human AI update. CONFIRMED by full listing.** Two static callers: the per-human update `0x0099bcb0` (851 insns, reached from the world update `0x00702a50` by two routes) and `0x00af4150` (266, reached from `0x00952760`, which has many callers — HYPOTHESIS: a vehicle-occupant or special-case path). In my own words, the update:
  1. returns early for a paused/special state (`0x00722000`) or an unusable object (`0x00853b10`);
  2. splits on local authority (`0x008addb0(obj, 0)`): a **non-authority (network proxy) branch** that only follows replicated state, and an **authority branch** that thinks;
  3. on the authority branch, applies an **AI-budget throttle**: a per-frame counter at `0x013576d0` compared against half of `0x013576d4`, latching `0x013576c6`, with a per-character flag bit at character `+0x1da6` (HIGH: an "AI LOD"/time-slicing scheme — not every character thinks every frame);
  4. runs `0x00507520` on the sub-object at character `+0x510` (see §2);
  5. runs the decision timer `0x004f5b90`, then the **decision step `0x004f5210`**, then `0x004e8310`;
  6. **dispatches the current mode's per-frame handler** through the mode table (§1.4), unless the character is in mode `0x2d` (none), a mode change is pending (`+0x317` bit 3), or an action has just finished (`+0x44c` bit 0);
  7. ends with `0x004dec10`.

### 1.2 Decision step — `0x004f5210` (95 insns, CONFIRMED by full listing)

Its one caller is the human AI update. Per call:
1. **Gate (`0x004f5180`, 47):** refuses to decide in a set of states — e.g. mode `0x22` (`Stunned`) with sub-state 9, a character flag at `+0x2b9` bit 7, a pending switch to `Stunned` or `0x18` (`Panic`), and three external tests (`0x0096f540`, `0x009aa360`, `0x00971cf0`). One branch forces mode `0x1f` (Skydiving) when a movement-state field equals 14. On refusal it clears `+0x44c` bit 0 and returns.
2. **Re-decision timing:** if an action is current (`+0x445` < 70) and not finished, it waits until the elapsed time on the `+0x438` timer reaches a per-action minimum (the word at `0x012e1c70 + 0x18·action`, i.e. row `+0x10`, filled from `combat_actions.xtbl` by its loader — see §1.3). So a running action is not re-decided until its own minimum time has passed (HIGH).
3. **Chooser:** calls `0x004f3750` with a local 0x58-byte block; the chooser returns an action id, `0x47` meaning none (the chooser itself is already described in `spec-ai-behavior-format.md` §3.3).
4. **Commit, in place:** if the result differs from the current action (or the current one has finished), it clears the previous action (`0x004f4eb0`), **requests the mode named by the action's category byte** (row `+0x04`, via `0x004e4130`), and — if that mode switch was accepted immediately — writes the "reason" byte to `+0x444`, the action id to `+0x445`, restamps `+0x438`, and copies the block into the committed slot `+0x450`.

**This path never calls `0x004f4d00`, `0x004f4b90` or `0x006f62b0`. Goal-driven decisions carry no request id.** (CONFIRMED.)

### 1.3 Goal tests and action routines (the two static tables)

- **Goal table `0x012e22f0` (17 rows × 0x20; `+0x04` = goal-test routine).** All 17 routines are small (22–207 insns; total ~1.2k own). Goal 0 (`change mode`) points at a shared 2-instruction stub `0x00db60a0` (HIGH: always-false/trivial). The largest are `advance` (`0x004f5590`, 207) and `cover` (`0x004f4120`, 204).
- **Action table `0x012e1c60` (70 rows × 0x18; `+0x08` = action routine).** 66 distinct routines (rows 24 `move scripted` and 49 `vehicle enter scpt` are NULL — those actions can only be ordered, never chosen; rows 9/14 share the stub `0x00db60a0`; rows 57/60 share `0x00525060`). Own bodies total **~9.7k insns** (one outlier, `throw grenade` `0x0054dce0` at 1,822, may be an over-long session-created function body — ±). The chooser calls these as "can this action start now, and if so fill the block" tests; the actual behaviour then runs in the mode the action's category selects.
- **Category byte (row `+0x04`) = mode index. CONFIRMED (static table read, every row).** The mode setter `0x004e4130` accepts values below `0x2c` (44), the mode table's length, and the 70 category bytes map onto mode names with no exceptions:

  | Mode | Actions that run in it |
  |---|---|
  | `Fire spray` (12) | `fire`, `fire sweep`, `fire suppress`, `fire chaos` |
  | `Combat Movement` (5) | `hold position`, `melee stand back`, all 14 `move …` actions, `avatar chase`, `killbane chase`, `killbane strafe` |
  | `Cover stay` (8) | `cover take def`, `cover take off`, `cover advance`, `cover stay down`, `cover fire` |
  | `Cover popout` (7) | `cover popout` |
  | `Follow` (14) | the `follow …` family except `follow carjack` |
  | `Hijack` (18) | `follow carjack`, `vehicle enter scpt`, `vehicle extract` |
  | `Brute Melee` (3) | the six `brute …` actions and the `avatar …` set except `avatar chase`/`avatar taunt` |
  | `Melee` (23) | `melee`, `melee vehicle` |
  | `Investigate` (22) | the three `investigate …` actions |
  | `Zombie eat` (42) | `zombie eat`, `zombie explode` |
  | `Taunt` (35) | `taunt`, `avatar taunt` |
  | `Grenade throw` (15), `Weapon throw` (36), `Reload` (27), `Pickup item` (26), `Human shield` (21), `Passenger` (25), `Commando` (6), `RollerBlader` (30), `Idle` (0) | one action each: `throw grenade`, `throw weapon`, `reload`, `pickup weapon`, `human shield grab`, `vehicle passenger`, `quick kill`, `roller blader change`, `idle` |

  So **20 of the 44 modes are CAE-driven**, and the other 24 are entered only from outside the decision layer (§1.4).
- **Re-decision word (row `+0x10`).** The decision step's wait uses the word at row `+0x10`. It is zero in the file image and is written per action by the `combat_actions.xtbl` loader `0x004f2650`. (The `300` that `spec-lua-api-behaviour.md` §30.4 reports is the *next* word, row `+0x12`.) Which `combat_actions` field fills it was not checked.

### 1.4 Execution layer — the 44-entry AI mode table `0x012e1538`

**CONFIRMED (static table read).** 44 entries of 0x24 bytes. `+0x00` is a name pointer (engine strings), `+0x04…+0x20` are eight function slots. The human AI update calls slot `+0x0c` every frame for the current mode (character `+0x501`); `0x004e4130` sets the *requested* mode (`+0x504`) and flags the switch (`+0x317` bit 3).

The mode names, in index order (engine data): `Idle`, `Aim`, `Brute Stomp`, `Brute Melee`, `Bust`, `Combat Movement`, `Commando`, `Cover popout`, `Cover stay`, `Cower`, `Dbno`, `Dog`, `Fire spray`, `Flee`, `Follow`, `Grenade throw`, `Gunfire evade`, `Gunfire wary`, `Hijack`, `Holdup`, `Hostage`, `Human shield`, `Investigate`, `Melee`, `Panic`, `Passenger`, `Pickup item`, `Reload`, `Return to shore`, `Riot cop`, `RollerBlader`, `Skydiving`, `Squirted`, `Streaking`, `Stunned`, `Taunt`, `Weapon throw`, `Veh clear door`, `Veh enter`, `Veh evade`, `Veh threat`, `Veh trans`, `Zombie eat`, `Player`.

Slot roles (HYPOTHESIS from fill pattern, not traced): `+0x04` enter/init (44/44 filled), `+0x08` and `+0x10` further lifecycle hooks (36 and 40 filled), `+0x0c` per-frame process (44/44, CONFIRMED as the per-frame call), `+0x14`/`+0x18`/`+0x1c`/`+0x20` sparsely filled; `+0x18`/`+0x1c` are the shared pair `0x004f3eb0`/`0x004f5440` in all the "combat action" modes (5–8, 12, 14, 15, 18, 22, 27, 34–36, 42), i.e. a common "is the current CAE action still valid / interrupt" pair for modes driven by the decision layer (HYPOTHESIS). `0x00754410` (1 insn) is a shared no-op.

**24 of the 44 modes are not CAE-driven at all** (no action's category names them; §1.3): `Aim`, `Brute Stomp`, `Bust`, `Cower`, `Dbno`, `Dog`, `Flee`, `Gunfire evade`, `Gunfire wary`, `Holdup`, `Hostage`, `Panic`, `Return to shore`, `Riot cop` (a 1-instruction no-op handler), `Skydiving`, `Squirted`, `Streaking`, `Stunned`, the five `Veh …` modes, and `Player`. These are ambient, reactive and vehicle behaviours. Other systems enter them (panic reactions, personality reactions, vehicle code, scripts, the decision gate's own forced `Skydiving`) through the same mode setter `0x004e4130`, which has **185 call sites** across the executable. `Idle` is both: action 69 `idle` selects it, and so do many outside callers. So the mode machine, not the CAE layer, is the real core of the AI runtime; CAE is one of its clients.

### 1.5 How an action connects to the CAE record and the staging/commit writers

Per-character action state (layout already partly in `spec-lua-api-behaviour.md` §30.4): staged block `+0x4a8`, committed block `+0x450` (both 0x58 bytes, template at `0x012e14e0`), current action `+0x445`, reason `+0x444`, flags `+0x44c`, request id `+0x4aa`, per-action repeat timers `+0x320 + 4·action`, behaviour-record pointer `+0x31c`.

There are **four writers** into that state, not two:

| Writer | Insns | Who calls it | What it does (own words) | Request id? |
|---|---|---|---|---|
| decision step `0x004f5210` | 95 | human AI update only | goal-driven choice, commits in place | **never** |
| commit `0x004f4d00` | 82 | Lua (`ai_do_scripted_*`, 6 natives), mission/vehicle code, two mode handlers (via `0x004f5360`), network receive `0x004f5830` | stages+commits an *ordered* action immediately, sets `+0x44c` bit 3 ("ordered"), requests its mode | **yes, kind 3**, unless the third argument is non-zero (only the network receive passes 1) |
| stage `0x004f4b90` | 78 | Lua (`ai_suggest_action` and 4 more), `0x00730280`, network receive | stages only (`+0x44c` bits 3+4); the chooser later picks it up | kind 3 only when the "force" argument is set and no id was passed in |
| try-start `0x004f5360` | 85 | mode handlers `Combat Movement` (via `0x005381b0`) and a Brute handler `0x00527b10`; network | checks the repeat timer, the precondition flags and the action's own routine, then commits through `0x004f4d00` | yes (inherits) |

Plus the two terminal writers: **finish `0x004f4f50`** (98 insns, **85 callers**, almost all inside mode handlers) — sets the random repeat cooldown from the behaviour record's `Repeat_min`/`Repeat_max`, marks the action done, and **if it was an ordered action, completes its kind-3 request** (`0x006f66d0`) and appears to wake a script thread (`0x00e0cfb0`/`0x00e0ce60`, HYPOTHESIS); and **cancel `0x004f4c90`** (34 insns, 12 callers, `ai_clear_scripted_action`-style) — cancels the kind-3 request (`0x006f6680`).

The network forwarder `0x004f4830` (184) is shared by all of these (sub-types 0 commit, 1 stage, 2 try-start, 5 cancel), so the action layer is replicated as orders, not as state (HIGH).

### 1.6 Answer to the open question: is NPC AI a client of the request-id pool?

**No — not the per-frame AI.** The scripted-request pool (`0x006f6040`–`0x006f68a2`, ~600 insns, 200 records) is a **completion-tracking side channel bolted onto the "ordered action" seam**, not the mechanism by which NPCs think or act:

- The goal-driven decision path (`0x004f5210` → `0x004f3750`) and the 44 per-frame mode handlers never allocate, query or complete request ids (CONFIRMED for the decision path by listing; for the mode handlers, by the allocator's caller list — none of the 37 allocation sites is a mode handler except mode 43 `Player`).
- Kind 3 (AI action) ids exist **only** for actions *ordered from outside* the decision layer — through `0x004f4d00` / `0x004f4b90`. Their consumers are Lua natives, mission/vehicle routines in `0x0062xxxx`–`0x006bxxxx` (via the `move scripted` wrapper `0x0053dc00` and the vehicle-enter path `0x00521200`), and two internal mode handlers that force an action through `0x004f5360`. The id is completed by the shared "action finished" routine only when the ordered flag is set.
- Allocation sites by kind (push survey of all 37 call sites): kind 0 ×1 (turn-to, Lua), kind 1 ×2 (`0x007e3300`/`0x007e3380`, unknown subsystem, with a callback at `0x007e3010`), **kind 2 ×30** (move-to/pathfind — Lua, mission/vehicle code, and the `Player` mode handler `0x004f1440` ×5), kind 3 ×2 (the two action writers above), kind 4 ×2 (teleport).
- So "the AI runtime as a unit" does **not** include the request pool; the pool is a small, already-specified utility (`spec-lua-api-behaviour.md` §35.2) that the ordered-action seam and several scripted movement/teleport features share.

**How an ordered action reaches the character (answers the §30.4 OPEN "how/whether the AI later commits a staged suggestion", at scoping depth — HIGH, from the chooser's listing).** The chooser `0x004f3750` checks the ordered flag (`+0x44c` bit 3) *before* any goal. If it is set and the `+0x440` timer allows, the staged block `+0x4a8` is copied out as the result, so an ordered action **pre-empts every goal**. If the order is a suggestion only (`+0x44c` bit 4, set by the stage writer), it is first re-validated through the action's own routine; on failure the staged block is reset to the template and a 250 ms hold is started. The decision step then commits it like any other choice. When the mode handler later reports the action finished (`0x004f4f50`), the kind-3 id is completed. The "reason" byte `+0x444` holds the goal id that produced the action; the commit writer stores `0x12` (18) there, i.e. one past the 17 real goals, which reads as an "ordered" pseudo-goal (HIGH).

### 1.7 Where the data-driven half plugs in (already specified)

```
ai_behavior.xtbl ─ loader 0x004f2b50 ─► 44 records × 0x200 at 0x0135bfc8
ai_goals.xtbl    ─ loader 0x004f29b0 ─► goal table action lists (0x012e22f8 + 0x20·goal)
combat_actions   ─ loader 0x004f2650 ─► action-table bytes (+0x10 min-repeat word, +0x15 pre_flags …)
ai_personalities ─ loader 0x00507ac0 ─► 20 records × 0x20 at 0x013b84d0
area modal .xtbl ─ loader 0x006f3d60 ─► Group_AI_Overrides → per-group behaviour/personality index
spawn            ─ 0x006e7280 → assigner 0x004f36a0 (also 0x004f4ee0, 0x006592a0, Lua 0x00a5f1e6) ─► char +0x31c record pointer
per frame        ─ chooser 0x004f3750 reads +0x31c: goal bytes +0x1c6, ability table +0x22, Repeat_min/max (via 0x004f4f50)
```

The behaviour record therefore reaches the runtime in exactly three places: the chooser (gates), the finish writer (cooldowns), and — HYPOTHESIS, not traced — the goal tests and action routines reading the cover/suppress/seen-in-last/aggression fields.

---

## 2. Main functions and data structures, with rough sizes

Instruction counts are own-body counts from the shared project's function bodies (a few targets had no function defined; those were created in the throwaway session only, ±10 %). "Reach" figures are unique functions within N call levels, skipping any single callee over 1,500 instructions.

| Stage | Key addresses | Instructions (approx.) | What it does |
|---|---|---|---|
| World → human update (shared, not AI-only) | `0x00702a50` → `0x0099bba0` → `0x009d94a0` → `0x009d9190` → `0x0099bcb0` | 851 for `0x0099bcb0` alone | per-human per-frame update; one of its steps is the AI update |
| Human AI update ("spine") | `0x004e8d00`; `0x004f5b90`, `0x004e8310`, `0x004e8b10`, `0x004dec10`, `0x004f3a90`, `0x004f28f0` | 331 + 59 + 248 + 164 + 74 + 32 + 9 ≈ 920 | authority split, AI-budget throttle, perception tick, decision, mode dispatch |
| Perception / knowledge (char `+0x510`) — identity HYPOTHESIS | `0x00507520` and its subtree, mostly `0x004f8000`–`0x0050ffff` | 306 own; ~10.7k / 134 functions at 2 levels | a scheduler of many sub-updates on random-interval timers (sensing, target choice, "seen in last" style bookkeeping, HYPOTHESIS) |
| Decision step + chooser | `0x004f5210`, `0x004f5180`, `0x004f3750`, `0x004f3560`, `0x004f25c0`, `0x004f4720`, `0x004f4eb0`, `0x0051db40` | 95 + 47 + 205 + 50 + 33 + 22 + 13 + 18 ≈ 480 | gate, re-decision timing, ordered-action pre-emption, goal walk, in-place commit |
| Goal tests | goal table `0x012e22f0` slot `+0x04` (16 distinct routines `0x004f2910`…`0x004f5590`) | ~1.2k own; ~3.3k incl. direct callees | "does goal N apply now" |
| Action routines | action table `0x012e1c60` slot `+0x08` (66 distinct, `0x004f0b70`…`0x005531a0`, plus `0x006948c0`) | ~9.7k own (largest `throw grenade` `0x0054dce0`, 1,822 — a real 8 KB body); ~25k incl. direct callees (overlapping) | "can action N start now; fill the action block" |
| Action-state writers | `0x004f4d00`, `0x004f4b90`, `0x004f5360`, `0x004f4f50`, `0x004f4c90`, `0x004f4ae0`, `0x004f4b30`, net forwarder `0x004f4830` | 82 + 78 + 85 + 98 + 34 + 30 + 33 + 184 ≈ 625 | ordered commit / stage / try-start / finish+cooldown / cancel / per-action delay / replication |
| Mode machine | setter `0x004e4130` (46) + acceptance test `0x004de110` (86); mode table `0x012e1538` (44 × 8 slots) | 237 distinct slot functions, **~19.9k own** (per-frame slot alone: 44 functions, ~9.2k) | the actual behaviours: Idle, combat, cover, flee, vehicle, special enemies… |
| Largest mode handlers | `Idle` `0x00513270` (1,326; ~29k reach at 2 levels — wander, sidewalk, action nodes, props such as umbrella/newspaper), `RollerBlader` `0x00532180` (1,319), `Player` `0x004f1440` (906), `Skydiving` `0x00535a20` (523), `Cover popout` enter `0x00541c90` (437) | — | — |
| Data loaders (already specified) | `0x004f2b50`, `0x004f29b0`, `0x004f2650`, `0x00507ac0`, `0x006f3d60`, `0x004f27f0`, `0x00507e60`, `0x004f36a0`, `0x004f4ee0`, `0x006e7280` | not re-counted | see §3.1 |
| Scripted-request pool (already specified) | `0x006f6040`–`0x006f68a2` | ~600 | completion ids for ordered actions, move-to, turn-to, teleport (§1.6) |

**Data structures** (scoping identifications only; no layouts claimed beyond what other specs already state):
- the per-character AI block inside the human object: behaviour pointer `+0x31c`, per-action repeat timers `+0x320…`, action state `+0x438…+0x4ff` (§1.5), mode bytes `+0x501` (current) / `+0x504` (requested) / switch flag `+0x317` bit 3, the `+0x510` sub-object, AI-flag bytes around `+0x2b9`/`+0x2bc` (HYPOTHESIS: the `ai_force_flags` storage);
- the 70-row action table `0x012e1c60` (row `+0x04` category = mode index, `+0x08` routine, `+0x10` min re-decision word filled from `combat_actions.xtbl`, `+0x14` a byte the chooser tests, `+0x15` `pre_flags`);
- the 17-row goal table `0x012e22f0` (`+0x04` test routine, `+0x08` 24 action ids);
- the 44-row mode table `0x012e1538` (name + 8 lifecycle slots);
- the 0x58-byte action block and its template `0x012e14e0`;
- the AI-budget counters `0x013576c6`/`0x013576d0`/`0x013576d4`;
- the 44 behaviour records `0x0135bfc8` and 20 personality records `0x013b84d0` (already specified).

**Totals.**

| Scope | Functions | Instructions |
|---|---|---|
| Decision layer core: spine + decision step + chooser + goal tests + action-state writers | ~45 | **~3.2k** |
| + the 66 action routines (own bodies) | ~110 | ~13k |
| Mode machine, all 8 slots of all 44 modes (own bodies) | 237 | ~19.9k |
| **Measured root set** (all of the above, de-duplicated) | **332** | **~32.7k** |
| Root set + one call level (all regions) | 1,228 | ~114k |
| … of which inside the AI code region `0x004d0000`–`0x00556fff` | ~810 | ~84k |
| Root set + three call levels (fans out into character, animation, weapons, vehicles, physics) | 3,146 | ~262k |
| AI code region `0x004d0000`–`0x00556fff`, everything defined there | ~1,590 | ~152k |

**Reading the totals.** The closest analogue to the UI note's "core render path, ~10k" is the **~33k-instruction root set** — the spine, the decision layer, all action routines and all mode lifecycle functions — but those functions are shallow dispatchers into a large private helper layer: one call level more inside the AI region alone adds ~50k. A realistic "complete human-AI runtime" is therefore **~80–120k instructions in ~800–1,200 functions**, i.e. roughly **8–12× the UI core**, before pathfinding, vehicle (driver) AI, animation and network replication. Two caveats: the `0x004d0000`–`0x00556fff` region probably also holds non-AI human code (the region's lower end in particular), and the reach figures count each shared helper once regardless of how many stages use it.

---

## 3. Specified versus missing

### 3.1 What is specified (and can be used without new RE)

- **All AI data formats — pure data, no RE needed for loading.**
  - `ai_behavior.xtbl` (every offset `+0x000…+0x1ff` CONFIRMED), `ai_goals.xtbl`, `ai_personalities.xtbl`, `panic_reactions.xtbl`, the action-node tables, `generic_characters.xtbl` (`spec-tables-traffic-ai.md` §8–§13).
  - `combat_actions.xtbl` / `combat_tricks.xtbl` (`spec-tables-weapons-combat.md` §11).
  - The `Group_AI_Overrides` selector and its whole consumer chain, from area load to the character's `+0x31c` pointer (`spec-ai-behavior-format.md` §3.1, CONFIRMED 2026-10-02).
  - **Verdict for this sub-piece:** a port can load every AI table and assign the right behaviour record to every spawned character today. That part is format work only and is done.
- **The goal/action chooser algorithm** (`spec-ai-behavior-format.md` §3.3, CONFIRMED): fixed goal priority 0…16, per-goal action order from data, gating by record bytes, precondition flags and repeat timers, first success wins, fallback 14/48.
- **The enum tables**: 70 action names, 17 goal names, the precondition-flag names (`spec-tables-traffic-ai.md` §1.5).
- **The Lua-facing surface**: `ai_suggest_action`, `ai_do_scripted_advance`/`take_cover` and their siblings, `ai_clear_scripted_action`, the per-action delay natives, the partial action-record layout (`spec-lua-api-behaviour.md` §7.30, §18.3, §30.4); the `ai_force_flags*` strings as network debug tags (§7.1/§7.6/§7.10).
- **The scripted-request pool**, fully (`spec-lua-api-behaviour.md` §35.2).

### 3.2 What is missing, and whether it needs RE

| Gap | Needs RE? | Notes |
|---|---|---|
| The per-character AI update: authority split, AI-budget throttle, call order, the mode-dispatch rule | **Yes, small** | `0x004e8d00` + `0x004f5b90`; ~0.9k insns; the frame contract everything else hangs off |
| The 44-mode machine: slot roles, mode-switch handshake (`+0x501`/`+0x504`/`+0x317`), acceptance test (category → mode mapping is already read, §1.3) | **Yes, small–medium** | `0x004e4130`, `0x004de110`, table `0x012e1538`; table reads plus ~10 small functions; **entirely unspecified today** — no spec mentions a mode table |
| Decision step: re-decision timing, ordered-action pre-emption, in-place commit, "reason"/goal-id byte | **Yes, small** | `0x004f5210`, `0x004f5180`, chooser prologue; closes the §30.4 OPEN |
| Action-state writers as a set (commit/stage/try-start/finish/cancel, cooldown roll, kind-3 completion) | **Yes, small** | ~0.6k insns; partly described piecemeal in the Lua tranches; needs one consolidated account |
| The 17 goal tests | **Yes, small** | ~1.2k own; OPEN in `spec-ai-behavior-format.md` §3.3; many will read the perception sub-object |
| Perception / knowledge sub-object (`+0x510`): what it senses, how targets are chosen, the "seen in last" clock, FOV tuning (`Ai_fov_*` strings) | **Yes, medium–large** | ~10.7k at 2 levels; identity itself is still a HYPOTHESIS |
| The 66 action routines | **Yes, medium** | ~9.7k own; OPEN in `spec-ai-behavior-format.md` §3.3 |
| Per-mode behaviour (enter/process/exit for 44 modes) | **Yes, large** | ~20k own, ~80k+ with private helpers; the bulk of the system |
| Runtime meaning of `ai_force_flags` (ignore_ai, cant_flee, deaf, always_sees_player, …) | **Yes, small per flag** | only the Lua/network side is documented; each flag's runtime read sites are unknown |
| Ambient behaviour runtime (wander/sidewalk/action nodes in `Idle`; panic and personality reactions) | **Yes, medium** | data side specified (§8, §11, §13 of `spec-tables-traffic-ai.md`); runtime not |
| Movement / pathfinding over the navmesh | **Partly blocked** | the navmesh is a **zone-file section** (the zone tag registry next to `0x013026d0` names it "Navmesh" / "The navmesh data for the zone") — its format sits inside `.czn_pc` and is **blocked on the parked hold**; the path-planning code itself was not located in this pass (OPEN) |
| Vehicle (driver) AI (`vai_force_flags*`: lanes, rails, chase, flee) | **Yes — separate system** | users at `0x008b08d0`, `0x00b26ef0`; a sibling of human AI, not part of this unit; not sized |
| Network replication of AI (the forwarder `0x004f4830`, `human_ai_data` tagged records) | **Yes, if MP matters** | can be skipped for a single-player port |
| Named-character special modes (`Killbane …`, `Avatar …`, `Brute …`, `Zombie …`) | **Yes** | mission-specific; any placement or override that comes from zone data is **blocked on the hold** and was not read |

**Plain answer.** Everything *data* is done: about **one tenth of the work**, the loading and selection of behaviour records, can be written now. Everything *runtime* — the frame contract, the mode machine, the goal tests, the action routines, perception and the per-mode behaviours — needs the executable. Unlike the UI case, there is no clean one-callback seam that could be implemented independently; the decision and execution layers share one character record and call back into each other (finish → request completion; mode handlers → try-start).

---

## 4. Proposed order of specification

Dependency first, then how much decision-making each unit unlocks.

1. **A0 — The spine: per-character AI update + mode machine protocol** (`0x004e8d00`, `0x004f5b90`, `0x004e4130`, `0x004de110`, the 44 × 0x24 mode table with slot roles, the category → mode column of the action table; ~1.5k insns plus two table reads).
   - *Why first:* every other unit is a client of it. It defines when a character thinks, how a mode is requested and accepted, and which handler runs each frame.
   - *Validation:* the mode names are engine data, and the category column already lines up with the action names row for row (§1.3). Each behaviour record's enabled actions therefore determine which modes that behaviour can ever reach.
2. **A1 — Decision step and the action-state writers** (`0x004f5210`, `0x004f5180`, chooser prologue, `0x004f4d00`, `0x004f4b90`, `0x004f5360`, `0x004f4f50`, `0x004f4c90`, `0x004f4ae0`/`0x004f4b30`; ~1.1k).
   - *Why here:* with A0 and the existing chooser spec, this completes the decision layer end to end and consolidates the piecemeal Lua-tranche findings into one account. It also closes `spec-lua-api-behaviour.md` §30.4's OPEN.
   - *Validation:* Lua-observable behaviour — `ai_suggest_action` pre-emption, `ai_do_scripted_*` completion, `*_check_done` results.
3. **A2 — Perception interface, then the 17 goal tests** (the query functions that goal tests and actions call on the `+0x510` sub-object first, then `0x004f2910`…`0x004f5590`; ~1.2k for the tests plus the interface).
   - *Why here:* goal tests are small and decide *which* goal wins. Specifying the perception queries as an interface first lets the tests be specified without the whole perception body.
4. **A3 — The core combat families, action routine plus mode together**, in this order:
   - fire: actions 0–3, mode `Fire spray`, plus the non-CAE `Aim` mode;
   - combat movement: actions 16–29, mode `Combat Movement`;
   - cover: actions 8–13, modes `Cover popout`/`Cover stay`;
   - melee, reload, pickup, grenade/throw: actions 4–7 and 44–45, modes `Melee`/`Reload`/`Pickup item`/`Grenade throw`/`Weapon throw`.
   - *Size:* ~6–8k own insns, ~25k with helpers.
   - *Why:* the shipped behaviours (`Default Unarmed`, `Default Sniper`, the Brute set, `Saint Rifle`) mostly enable these. This is the first point where a port gets recognisable combat.
5. **A4 — Perception body** (the `0x00507520` subtree; ~10k).
   - *Why after A3:* A2 already defined its interface; the body changes *how well* NPCs notice things, not *what* they do.
6. **A5 — Ambient and reactive modes** (`Idle` with wander/sidewalk/action nodes, `Flee`, `Cower`, `Panic`, `Gunfire wary`/`evade`, `Taunt`, `Investigate`; personality and panic-reaction hooks; ~6k own, ~30k+ with helpers).
   - *Why here:* this is what makes the city look alive, and it is independent of combat once A0 exists. `Idle` alone fans out widely.
7. **A6 — Follow/homie and vehicle-interaction modes** (actions 30–40, 48–50; modes `Follow`, `Hijack`, `Passenger`, `Veh …`).
   - Touches the separate vehicle-AI system; scope it together with that.
8. **A7 — Special enemies and one-off modes** (`Brute …`, `Avatar …`, `Zombie …`, `Killbane …`, `RollerBlader`, `Skydiving`, `Streaking`, `Dog`, `Riot cop`, `Dbno`, `Bust`, `Holdup`, `Hostage`, `Human shield`, `Stunned`, `Squirted`).
   - Mission-specific; specify on demand.

**Recommended to park:**
- **Navmesh format and its loader.** Zone-authored data inside `.czn_pc` — blocked on the hold. Path *queries* can be specified as an interface from the movement actions' side without reading it.
- **Vehicle (driver) AI.** A sibling system of comparable size (not measured here). It needs its own scoping note.
- **AI network replication.** Not needed for single player.

**Not needed:** any further format work on the AI tables (§3.1). The `Group_AI_Overrides` → behaviour-record chain is complete as specified.

**Rough weight against the UI-render candidate.** The decision layer alone (A0–A2, ~4k) is smaller than the UI core and cleanly bounded, and would be a good next unit on its own. The whole human-AI runtime is roughly an order of magnitude larger than the UI core (~80–120k vs ~10k). Most of that size sits in the 44 mode handlers and their private helpers, and it decomposes naturally by mode family, which is what makes the A3–A7 split workable.

---

## 5. Notes for whoever takes it up

- **Tooling.** All three static tables (`0x012e1538` modes, `0x012e1c60` actions, `0x012e22f0` goals) are file-backed and read cleanly with `ptrs` mode. The mode table's first entry starts at `0x012e1538`, not at `0x012e1544`: the per-frame call indexes slot `+0x0c`, so reading from the call's own displacement misaligns every row by three slots.
- **Batch arguments.** `analyzeHeadless.bat` splits arguments at commas, as well as at `=`. A comma-joined root list arrives as separate items, so join lists with `+` or pass them one by one.
- **Out of bounds.** Nothing here read `.czn_pc` interior data. The navmesh (§3.2) and any zone-authored per-character overrides (named characters such as `Killbane`) are flagged as blocked, not examined.
- Every label above comes from this scoping pass only. A spec pass must re-derive each claim from the executable before recording it.

## 6. Clean-room check

- Addresses are plain hex throughout. There are no Ghidra auto-names for functions, globals or labels, no decompiler variable names and no pasted pseudocode. Quoted mode, action and goal names are the engine's own data strings, read from the static tables, as elsewhere in this project. Functions that `spec-ai-behavior-format.md` cites under Ghidra auto-names are cited here by bare address only.
- Self-check, run on the finished file as the last step with the project's standard pattern (`grep -noP`, the `iVar…|uVar…|…|FUN_…|DAT_…` auto-name regex): **0 hits.**
- No spec file was edited. The private Ghidra copy `tools\gp_aiscope` was deleted after the dumps.
