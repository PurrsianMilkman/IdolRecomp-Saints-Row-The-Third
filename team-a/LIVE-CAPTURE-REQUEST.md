# Live-capture request (Team A, 2026-10-12)

**Nothing here has been run.** Each item needs your approval to attach a debugger/hook to the
running game. Static analysis (and DLL-only emulation) is exhausted for all of them.

**Common to every item**
- **Target:** `SaintsRowTheThird.exe` (DX9), 1.0.0.1, SHA-256
  `17E4D85B05596D65E8386480F74D83A4ABD119C3CB29A7947C2D5A8FEA4547D7`. This is the build every spec
  address refers to.
- **Read-only:** a breakpoint or logging hook, then a dump of registers, the call stack and a few
  memory words. No patching and no save modification.
- **Session:** a normal single-player session. "Hook" means log-and-continue, not a pause.

You can approve all of them, some of them, or none. Items 1, 3 and 5 unblock the most Team B work.

| # | What we'd capture | Addresses / hooks | What the game must be doing | Rough time | Team B blocker it closes |
|---|---|---|---|---|---|
| 1 | The real function behind the unidentified "can this object fight?" check that `set_attack_player_flag` / `set_attack_enemies_flag` call | Log-hook `0x00a5f139` (vtable `+0x68` call) and `0x00a5f14b` (vtable `+0x70` call): object vtable, call target, return value | Start DLC mission `dlc2_m01` (it calls `set_attack_player_flag` early) or `dlc3_m02` (`set_attack_enemies_flag`) | ~10 min | `dlc2_m01`, `dlc3_m02`. They run on a labelled guess (spec-lua-api-behaviour §7.10/§17.5) |
| 2 | How the clothing store is opened | Breakpoint at entry `0x0082F840`; log the return address and stack | Free-roam: walk into any clothing store (e.g. Planet Saints) and press interact | ~5 min | Shop/store entry for clothing (spec-store-runtime §2). Its opener has no static reference anywhere |
| 3 | The engine code that calls a mission's Lua trigger callback, and with which arguments | Log-hook `0x00e0ca80` (the Lua call helper): caller address, argument count and the pushed values | Any mission with a "go here" trigger, e.g. m03 walking into the armory entrance, or m11 reaching a base | ~10 min | Confirms the 2-argument trigger callback rule (spec §1.14). That rule is behind m11's crash and probably many other missions; today it is HIGH confidence from the scripts only |
| 4 | Who grants a dropped weapon when the player walks over it | Log-hook `0x00601380` and `0x00B84330`: caller stack | Free-roam: kill an armed gang member, then walk over the dropped pistol | ~10 min | Weapon walk-over pickup (spec-weapons-runtime §5.1). Four static passes found nothing; Team B uses a guessed 1.5 m radius |
| 5 | Which code applies gun damage and melee damage | Log-hook the damage sink `0x0096fa20`: caller stack, damage value and flags | Free-roam: shoot a pedestrian with a pistol, then punch one | ~10 min | Hitscan and melee damage hand-off (coverage summary flagships #1 and #2). Combat damage in Team B is otherwise wired to the modifier pipeline, but not to the real call sites |
| 6 | What consumes a grenade's fuse and applies its explosion damage | The same hook as #5, during a grenade detonation; log the stack | Free-roam: throw a grenade near a car or pedestrian | ~5 min (do it in the same session as #5) | Projectile detonation (flagship #3) |
| 7 | The small leftover condition in the "is holding a throwable" check | Log-hook `0x00b88610`: argument and result | Same session as #6, while holding and throwing a grenade; or sh04 (an NPC is told to throw) | ~5 min | Residue in §55.4/§4.10 (sh04's check is otherwise RESOLVED) |
| 8 | What actually draws the ground under the default spawn point (tile 0915) | One frame capture (RenderDoc/PIX-style) at spawn | New game or a free-roam load at the default spawn | ~15 min | The "sky-blue void" at spawn (coverage flagship #8, spec-terrain §5). Team B's ground is missing there |

| 9 | Gamepad dead-zone, trigger-threshold and button-repeat timing values | One memory read (no breakpoint) of the pad record `0x029f4440 + pad·0x488`: stick `+0x248`/`+0x288`, button `+0x08`/`+0x0c`/`+0x10` | Any time after the main menu with a controller connected | ~2 min | Controls feel (spec-player-runtime OPEN 1). The writer was not found statically, and the standard XInput constants are absent from the exe |

## How to run it (prepared 2026-10-12; dry-run OK, nothing attached to the game yet)

The tools are in `TEAM A\tools\live_capture\`:
- `capture_agent.js`: read-only Frida hooks that only log, never write.
- `capture.py <set>`: attaches to `SaintsRowTheThird.exe` and writes `capture_<set>_<time>.jsonl` next to itself.

Python is `C:\Users\Purrsian\AppData\Local\Programs\Python\Python312\python.exe`, with Frida 17.17.0 already installed. All three sets passed a dry run with `--dry-run`, which attaches to the launcher itself.

**Session A — free roam (items 2, 4, 5, 6, 7, then 9, then 8). About 45 minutes.**
1. Start the game normally, the DX9 `SaintsRowTheThird.exe`. Load into free roam and stop outside any mission.
2. In a terminal, run `python "…\tools\live_capture\capture.py" freeroam`. It prints `hooked item2,item4,item56,item7 base=…`, then a stats line every 5 seconds.
3. Do these in order, pausing about 10 seconds between them:
   - (5) shoot a pedestrian with a pistol;
   - (5) punch a pedestrian;
   - (6) throw a grenade near a car;
   - (4) kill an armed gang member and walk over the pistol they drop;
   - (2) walk into a clothing store, e.g. Planet Saints, and open it.
4. Press Ctrl+C in the terminal. The log path is printed.
5. With a controller connected, run `python "…\capture.py" pads` (item 9, a single read that exits by itself).
6. Item 8 (frame capture) needs RenderDoc or PIX: launch the game through it, load at the default spawn, capture one frame, and save the `.rdc` next to the logs. Skip this if RenderDoc isn't installed.

**Session B — missions (items 1, 3, 7). About 25 minutes.**
1. Start `dlc2_m01` (or `dlc3_m02`). Run `python "…\capture.py" mission` before the first enemies appear.
2. Play until enemies turn hostile (item 1). Walk into a mission trigger area, i.e. any "go here" objective (item 3).
3. Press Ctrl+C and send Team A the `.jsonl` paths.

**Safety notes:**
- Everything is read-only: no game memory writes and no save changes.
- Item 1 hooks two instructions in the middle of a function. That is less safe than hooking a function entry: if the game crashes there, rerun Session B without item 1 by removing `'item1'` from `SETS.mission` in `capture_agent.js`.
- If the game fails to start with the hooks attached, just close the terminal. Nothing is persisted.

**Suggested batch if you approve all:** one free-roam session for items 2, 4, 5, 6 and 8 (about 45 minutes),
plus one mission session for items 1, 3 and 7 (about 25 minutes).

## Results — Session A run 1 (2026-10-13 10:19, user-approved, user reported "done")
Log: `tools/live_capture/capture_freeroam_20261013_101932.jsonl`. About 316 s, 441 records. The agent's `ret` field is wrong (it holds the hook address), so callers come from `bt[0]`. The game closed during the run; I haven't asked the user yet whether that was a crash.
- **Item 2 (clothing store): REFUTED.** `0x0082F840` never fired, although the user reports opening the store. It is not the clothing opener (or not in this build path). The real opener is still OPEN; next step is to hook the walk-in path `0x00A03940` / `0x0080CC90` and log the per-type callback.
- **Item 4 (walk-over pickup):** `0x00B84330` never fired. `0x00601380` fired 1,381 times, almost all from `0x009d37ba`, in per-frame bursts. That makes it a general inventory or weapon query, not the pickup grant. The only other caller was `0x008dcf54` (via `0x00b97d8f`/`0x00b98c5b`/`0x00b94bdf`/`0x00708695`), at t=0, which is the load or spawn loadout. **The pickup grant is not identified; it is OPEN.**
- **Items 5/6 (damage sink `0x0096fa20`), 41 calls from 4 call sites.** a0 = victim, a1 = attacker or 0, a2 = damage as a float:
  - `0x0096873f` (call site; return address `0x00968740`), via `0x00969a5a`/`0x0096b4a9`/`0x009be2f8`/`0x009cb1f6`/`0x009cdc6c`: fixed amounts **2400, 160, 160, 160, 160** with an attacker. The constant values look like weapon-table damage (the hitscan/melee path); this is a HYPOTHESIS until it is matched to a specific action. One more call came via `0x00969179`/`0x00884792`/`0x00434f51` with amount 190.
  - `0x007844a2` / `0x0078446e` via `0x00769ae1`/`0x0075addd`/`0x00703f09`: varying amounts (2.7–187). `0x0078446e` always has attacker 0, and its victim is the same object `0x1ac0ab30` each time, likely the player. These read as physics/impact damage (collisions, falls, blast impulse): HIGH CONFIDENCE from the varying floats.
  - `0x00962806` via `0x0079e97f`/`0x007a58c1`: values 263, 106 and 59 in the same frame as bursts of `0x007844a2`. This is a candidate explosion/area-damage path (grenade, item 6): HYPOTHESIS.
- **Item 7 (`0x00b88610`):** 138,553 calls, always returning 0, from `0x00b85685`, `0x00b81617` and `0x00b83a2c`. Even while the player held a grenade the result stayed 0, so this predicate is not "holding a throwable" for the player. Its meaning is still OPEN, and LOW.
- Items 8 and 9 were not run (8 needs RenderDoc; 9 is not approved).

**Run 2 prepared (2026-10-13):** `capture.py freeroam2` hooks the walk-in store path (`0x00a03940`, `0x0080cc90`) and the add-weapon helpers called by `inv_item_add_do` (`0x009d7e30`, `0x00602900`). Steps: in free roam, walk into Planet Saints and open it, then kill an armed gang member and walk over the dropped pistol. About 5 minutes. The user closed the game normally after run 1; it did not crash. Damage findings are in spec-weapons-runtime §4.5.5a.

## Mission launcher (requested 2026-10-13) — design ready, NOT built (permission block)
**Path found statically:** the cellphone native `C_cell_phone_mission_start` (`0x0083bc10`, registered at `0x0083bdba`) runs the console command `mission_start <name>` through the printf-style console executor `0x00d9f250` (`"mission_start %s"` at `0x0116119c`). The launcher would make that same single call from the game's Lua thread: Frida hooks the Lua call helper `0x00e0ca80` once, makes the call in its onLeave, removes the hook and calls `session.detach()`. A `--dry-run` flag would only check the bytes at `0x00d9f250` (`53 8b dc 83 ec`) and detach.
**Status:** writing `tools/live_capture/mission_launcher.py` was refused by the Claude Code auto-mode permission classifier ("Remote Shell Writes"), because the tool writes into a running process. It needs the user's explicit permission, e.g. an allow rule for that file, or a direct yes via user_say.

**Launcher BUILT and tested 2026-10-13 17:05 (user-approved via user_say).** `python tools/live_capture/mission_launcher.py <name> [--dry-run]`. The dry run passed on the live game (executor bytes `538bdc83ec`). `dlc2_m01` launched: the call returned with no error and the game kept responding. **But** a mission-only Lua hook saw no `dlc2_*` callbacks afterwards, so whether the mission actually started is unconfirmed (OPEN for the user). If it didn't start, the next step is to test with a base mission (e.g. `m03`) or call `C_cell_phone_mission_start` via Lua.
**Session B2 (17:05–~19:00) results:** the store dispatcher `0x00A03940` ran 6 times and the opener `0x0080CC90` ran twice, from `0x005eeeb0` (via `0x005b7417`) and from `0x005fa8dc`. None of the four type callbacks fired. **Open question for the user: which store was opened?** All hooks were detached cleanly through the STOP file.
**Launcher caveat (static check, 2026-10-13 evening):** `0x00d9f250` is a console executor. It formats the text, special-cases `debug`/`?`, then dispatches through `0x006f68b0`. No null-terminated `mission_start` command-name string exists in the exe; the only hit is the format string. The command may therefore be missing or stripped from the retail command table, which would explain why `dlc2_m01` didn't start. Next steps: (a) trace `0x006f68b0`'s command table to check whether the commands are registered by hash; (b) otherwise drive the real path, i.e. bind the mission and enter the game-mode state handler `0x006D9310`, which launches `script_mission_start`. Until then, consider the launcher unproven.

**Hands-free capture (2026-10-13):** run `powershell -File tools\live_capture\autowatch.ps1` once, then just play. It waits for the game, then for 90 s of responsive running (time to load in), then attaches all read-only hook sets detached: `freeroam`, `freeroam2`, `item1only` and `item3b`. The hooks end when the game closes. To stop them early, create `tools\live_capture\STOP`. The write features (teleport, weapons, spawning and so on) are not included; they still need the user's own approval.

## sr3_testkit (built 2026-10-13 under the user's standing approval via user_say)
`python tools\sr3_testkit\testkit.py <cmd> [arg] [--dry-run]`
- `missions`: lists 68 mission names (works offline).
- `status`: player position and HP.
- `tp <navpoint>`: teleports the player to a named spot.
- `weapon <name>`: gives a weapon.
- `cash <n>`: adds cash.
- `invuln on|off`, `noto off|on`: toggle invulnerability and notoriety spawns.

Each run attaches, makes one fixed call of the game's own script natives on the game thread, and detaches. There is no free-form code input. `--dry-run` checks the Lua API bytes and prints the call it would make.
**Added:** ``time HH:MM`` (``set_time_of_day``) and ``weather <name>`` (``set_weather``; its argument form is unverified). ``status`` now reports HP only: there is no coordinate-getter native in the exe. **Not built yet:** spawning NPCs (needs zone group data), numpad hotkeys, and in-game auto-start. Mission start is still `tools\live_capture\mission_launcher.py` (console path, unproven). **No live test yet**; the game was not running.

## Hands-free session (built 2026-10-13, standing approval)
**Run once before starting the game:** `powershell -ExecutionPolicy Bypass -File "D:\IdolRecomp Saints Row The Third\TEAM A\tools\sr3_testkit\run_session.ps1"`
It starts, hidden: the auto-attach watcher (all read-only hooks, attached about 90 s after the game is up and responsive) and the numpad hotkeys:
- Num1: test setup (god mode, notoriety off, Pistol-Gang, grenade).
- Num2: teleport to the clothing store. Its navpoint name isn't known yet; for now it only logs a note.
- Num3: start dlc2_m01 (unproven console path).
- Num4: log a checkpoint.
- Num0: status.
- Num9: clean stop.

Everything exits when the game closes.
**Known gaps:**
- Spawning NPCs isn't supported.
- The grenade name `Thrown-Grenade` is unverified.
- The clothing-store navpoint isn't known.
- The watcher waits a fixed 90 s rather than detecting the player object.
- None of it has been tested live yet.
**Testkit gap update (2026-10-13 evening, static):**
- **Closed:**
  - The grenade's name is `grenade` (weapons.xtbl; the full weapon list is in `tools/scratchpad/xtbl_weapons`).
  - There is no NPC-spawn native in the exe (no character/npc/squad create or spawn). Num1 now uses `gang Morningstar:2` (`notoriety_set`, as the m04 script does), which sends armed gang members to the player. `noto off` now targets `Police`.
  - The watcher now waits for the player object: it polls `testkit status` until it returns `hp=<n>`, up to 15 min, instead of waiting a fixed 90 s.
- **Still open:**
  - The clothing-store and gun-store navpoint names for `tp`: not in the Lua dumps.
  - The mission-start path: the game's own scripts start missions through `scripted_mission_start` objects (the `0x006D9310` handler), not the console path, so Num3 stays unproven.

## Status after the 2026-10-14 14:20 session (single-session kit, user played m04)

| Item | Status | Where |
|---|---|---|
| 1 enemies turn hostile (vtable +0x68/+0x70) | **DONE, FINAL**: script_npc vtable `0x0117EFEC`; +0x68 = `0x00a330b0` (has spawned human, [+0x180] != 0); +0x70 = `0x00a330d0` (returns it) | spec-lua-api-behaviour.md ~L2313 |
| 2 walk-in store | **DONE, FINAL**: chain `0x00A03940` → registry → type 0 `0x0082B0C0` / type 1 `0x00815490` → opener `0x0080CC90`; Planet Saints uses both types | spec-store-runtime.md §9 |
| 3 trigger / callback dispatch | **DONE, FINAL**: trigger enter `0x00a1feb0` (ret `0x00a1fecf`) → `0x00e0ca80`; callback-kind table for 60 live calls | spec-lua-api-behaviour.md "Engine callback dispatch: death and destruction" |
| 4 walk-over pickup | **PARTIAL**: `0x00B84330` fired once, from `0x0060e2b4` ← `0x0095af64` - but that caller pushes "QTE", so it was a QTE/scripted grant, not the walk-over pickup; pickup still OPEN, needs one checkpointed pickup | spec-weapons-runtime.md §5.1 note |
| 5/6 damage sink | DONE earlier (§4.5.5a) | spec-weapons-runtime.md |
| 7 `0x00b88610` | LOW, left OPEN (not a throwable check) | — |
| 8 frame capture | not done (needs RenderDoc) | — |
| 9 pad read | NOT approved | — |

**What's left:** one short session: Num1, then Num4, kill a gang member, walk over the dropped gun (item 4). Start the game alone, get into free roam, the orchestrator removes `GAME_HANDS_OFF`, then run `run_session.ps1 -NoLaunch`.

### Next session (item 4 only, about 3 minutes)
1. Start the game on its own and load into free roam.
2. Tell the orchestrator "in". It removes `GAME_HANDS_OFF` and runs `run_session.ps1 -NoLaunch`.
3. Wait for the rising double beep.
4. Press **Num1**: god mode, pistol, and armed gang members are sent at you.
5. Kill one gang member and **don't move** until its gun is on the ground.
6. Press **Num4**, then walk straight over the dropped gun.
7. Press **Num4** again, then **Num9**, then quit the game normally.
