# Front-end flow for First Playable (2026-10-19)

**Scope.** Everything the front end needs for the First Playable path: boot, main menu, New Game or Load Save, the loading screens, the hand-off into free roam, pause, save, and quit. Where another spec already owns a fact, this file points to it and does not repeat it.

**Method.** Static work only on the shipped `SaintsRowTheThird.exe` (PC retail, image base `0x00400000`), using pefile and capstone through the helper scripts `tools/scratchpad/fe_tool.py` (strings, imm32 refs, rel32 callers, linear disassembly), `fe_modepush.py` (every game-mode request site with its immediate arguments), `fe_screens.py` + `fe_vtcode.py` (UI screen object to doc name to screen code), `fe_mount.py` (archive registry), `fe_progress.py` (boot progress constants) and `fe_natives.py` (natives called by the menu Lua). Raw disassembly dumps: `scratchpad/frontend_flow_2026-10-19/`. Lua is the shipped `interface.vpp_pc` text in `scratchpad/ui_lua_dumps/`. The game was not run. Addresses are evidence anchors, not names.

**Confidence labels used on every claim.** **FINAL** = read directly from the disassembly or the shipped Lua/data, cleared for implementation. **HIGH** = strong call-shape or context evidence; one link not opened. **GUESS** = plausible, not shown; implement as a choice, not as a fact.

---

## 0. Coverage table

"Before" is the state of the other specs at the start of this pass. "After" is the state with this file.

| # | Step | Before | After | Where |
|---|---|---|---|---|
| 1 | Boot: init order, packfile mounts, logos, legal screen, first UI, intro-video skip | PARTIAL (`spec-render-pipeline.md` §6.6 "relative order NOT established"; `spec-bink-boundary.md` §6.2 splash caller OPEN; `spec-tables-progression.md` §14.2 mount passes described, table not listed) | **FINAL** (two internals HIGH) | §2 here |
| 2 | Main menu: docs, items and order, Continue rule, DLC items, input, transitions | PARTIAL (`spec-frontend-menu-runtime.md` §2/§3 natives only; `0x007AE420` misnamed; screen codes OPEN) | **FINAL** | §3 |
| 3 | New Game: difficulty prompt, opening mission vs free roam, loading, initial state | PARTIAL (frontend §2, `spec-save-format.md` A.3) | **FINAL** for the flow; initial cash stays OPEN (cross-ref) | §4 |
| 4 | Load Save: list, metadata, sorting, load flow, restore | PARTIAL (`spec-save-format.md` §8, §11.1, A.1; list screen not specced) | **FINAL** (A.1's two predicates stay OPEN there) | §5 |
| 5 | Pause | FINAL (`spec-tables-ui-controls.md` §18) | **FINAL**, with one correction to the open/freeze mechanism | §6 |
| 6 | Save screen from pause: slot choice, overwrite confirm, timing, autosave rule | PARTIAL (`spec-save-format.md` §15.1, A.2) | **FINAL** | §7 |
| 7 | Quit: to main menu, to desktop, prompts | PARTIAL (`spec-lua-api-behaviour.md` §26.10 callback chain OPEN) | **FINAL** | §8 |
| 8 | Vint documents, their Lua, the natives the menu Lua calls | PARTIAL (natives specced one by one; no doc/screen-code map) | **FINAL** | §9 |
| 9 | Loading screens | MISSING (frontend §1 "mode 3 = loading screen", HYPOTHESIS, wrong) | **FINAL** (one wait predicate HIGH) | §10 |

---

## 1. The backbone: the game-mode stack

Every front-end transition is a request on the game-mode stack (`spec-frontend-menu-runtime.md` §1 described the table; this section completes it). **FINAL** unless marked.

### 1.1 The eleven modes

Each mode is registered by `0x00706A40(mode, block, overlay)`. The 0x24-byte block holds callbacks: `+0x00` enter(previous mode), `+0x04` leave(next mode), `+0x08` "may leave now?" predicate (a false result stalls the pop until a later frame), `+0x0C` per-frame, `+0x14` per-frame while covered, `+0x1C` a further per-frame hook. The `overlay` byte (`+0x20`) marks a mode that does not hide the one beneath it.

| Mode | Meaning | Registrar | enter / leave / per-frame | overlay |
|---|---|---|---|---|
| 1 | Boot (bottom of the stack for the whole session) | `0x005D3AC0` (the WinMain-shaped entry, `ret 0x10`) | `0x005D3850` / `0x005D39E0` / — (`+0x14` `0x005D14A0`) | 0 |
| 2 | Main menu | `0x007AF0F0` | `0x007AE9E0` / `0x007AEAF0` / `0x007AED40` (`+0x08` `0x007ADCB0`, `+0x1C` `0x007ADB10`) | 0 |
| 3 | Session start (single player too) | `0x0088CF70` | `0x0088C090` / `0x0088B560` / `0x0088CEE0` | 0 |
| 4 | City loading screen | `0x007AD410` | `0x007ACE10` / `0x007ACEC0` / `0x007AD210` (`+0x08` `0x007ACBF0`, `+0x1C` `0x007ACCB0`) | **1** |
| 5 | Gameplay | `0x007041B0` | `0x00702F90` / `0x00703F50` / `0x00703C00` (`+0x1C` `0x007030D0`) | 0 |
| 6 | **Pause menu / map** (corrects frontend §1 "control-scheme UI") | `0x007E1D10` | `0x007E1A70` / `0x007E1AC0` / `0x007E1AB0` | **1** |
| 7 | Bink movie | `0x00BDC120` | as `spec-bink-boundary.md` | 0 |
| 8 | Async player teleport | `0x007A8530` | as `spec-lua-api-behaviour.md` | 0 |
| 9 | Co-op synced pause handshake (`"synced_pause_handshake"`, `"MS_WAITING_TO_START"`) | `0x007B06F0` | `0x007B0560` / `0x007B05B0` / `0x007B05F0` | 0 |
| 10 | Shutdown (all callbacks null) | `0x00707840` | — | 0 |

This **replaces** the frontend spec's mode-3 ("loading screen") and mode-4 ("new-game request") hypotheses: 3 is the session starter, 4 is the city loading screen. The `+0x27C` field of the session object `0x024E4860`, which `0x0088BA10(4, 2)` writes, is **the mode the session starter pushes when it is done**: the session state handler `0x0088CD40` (stored at `0x01304AA4` in the session state table) calls `push(0x024E4ADC)` (HIGH: the state-table slot was not walked to its state number).

### 1.2 Request API (queued, applied by the pump `0x00706BE0` once per frame)

| Call | Effect |
|---|---|
| `0x00706E00(m)` | push `m`, unless the projected stack already holds `m`. Depth cap 15. |
| `0x00706F40(m)` | pop down to `m` (leave `m` on top). `m = 0` pops everything (`0x00706EC0`). |
| `0x00706FE0(a, b)` | pop down to `a`, then push `b`. |
| `0x00706E70()` | pop one. |
| `0x00706AB0()` / `0x00706B90()` | current top / projected top after queued requests. |

Pushing calls the new mode's enter with the old top. Popping first asks `+0x08`; if it answers false the pop waits. The pump calls `+0x0C` for the topmost run of overlay modes plus the first non-overlay mode under them, and `+0x14` for every mode below that (HIGH for the `+0x14`/`+0x1C` split; the `+0x0C` rule is read from `0x007068D0`).

### 1.3 Stack sequences (test vectors in §11)

- Boot: `[1]` → `[1,2]` (`0x007077E0` pushes 2 at the end of mode 1's enter).
- New Game or Load from the main menu: `[1,2]` → `pop_to(1)+push(3)` → `[1,3]` → session done → `push(4)` → `[1,3,4]` → load done → `push(5)` → `[1,3,4,5]`.
- Load from the pause menu: `pop_to(3)+push(4)` → `[1,3,4]` → `[1,3,4,5]` (`0x00B947A0`, call at `0x00B94859`, taken when a session object exists).
- Pause: `push(6)` → `[1,3,4,5,6]`; close pops 6.
- Quit to main menu: `pop_to(1)+push(2)` → `[1,2]`.
- Quit to desktop: `push(10)`.

---

## 2. Boot sequence — FINAL unless marked

### 2.1 Order on the main thread

1. **Entry** `0x005D3AC0` (4 stack arguments, the WinMain shape): `GetCommandLineA`, platform inits (`0x005D1440`, `0x00DC3F00`, `0x00DC4040`, `0x00DC3F90`).
2. **Engine start-up** `0x005D1A30`: device, `"vlib"` allocator, and **mount pass 1** (`0x00709790`, archives whose registry flag has bit `0x01`).
3. Register mode 1 and push it. Then the loop: pump messages (`0x00DC44C0`), run one frame (`0x005D14B0`), until it reports exit.
4. **Mode 1 enter** `0x005D3850`, in order:
   1. **Logo movies** `0x005D3700` (§2.3). They run in their own render loop before anything else is loaded.
   2. `0x005D2370`: **mount pass 2** (`0x007098C0`, bit `0x02`).
   3. Set the boot progress float `0x014FF680 = 0.05` and start the **`"Boot_render"` thread** (`0x00702070`, §2.4).
   4. **UI systems init** `0x005D2400`: UI Lua state, UI subsystem (`0x008489E0`), mount `"interface_startup.vpp"`, the interface packs, the screen system (`0x007B4D70`), the fade machine (`0x0059FA30`), **opening of the main-menu screens 3 and 4** (`0x007ADCE0(1)`, §3.1), `"interface-backend.peg"`, `"ui_bms_dlc_al.peg"`, the **`"save_warning"` document**, and finally the byte `0x014FF67E = 1` ("UI ready").
   5. **Engine bootstrap** `0x005D25F0`: **mount pass 3** (`0x007099F0`, bit `0x04`) and the bulk loads. It advances the progress float through **0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9** (ten stores, constants read from the image).
   6. Byte `0x014FF67D = 1` ("bootstrap done"); then wait in 10 ms sleeps until the boot thread sets `0x01493663`; join the boot thread; `0x007077E0` pushes mode 2; tail-jump to the render-thread start `0x005D7C60` (`spec-render-pipeline.md` §6.5).

This settles `spec-render-pipeline.md` §6.6's "relative order NOT established": `0x005D1A30` → logos → `0x005D2370` → boot thread → `0x005D2400` → `0x005D25F0`.

### 2.2 Packfile mount registry (35 archives) — FINAL

Table at `0x012F4C20`, 12-byte records `{name, flags, 0}`, count `0x012F4DC4`. Three passes walk it forward, each taking one flag bit (above). Bit `0x80` archives mount only when the patch byte `0x0149365D` is set and are also put in the priority list of `spec-tables-progression.md` §14.2. Archives with none of bits 1/2/4 are not mounted by these passes (world and streaming archives come later; `spec-world-streaming.md`).

| Pass | Archives (table order) |
|---|---|
| 1 (bit 1) | `startup`, `shaders` |
| 2 (bit 2) | `misc_tables`, `misc`, `interface_startup`, `interface`, `patch_uncompressed`, `patch_compressed` |
| 3 (bit 4) | `voices`, `decals`, `effects`, `items`, `vehicles_preload`, `vehicles`, `sound_turbo`, `skybox`, `cutscene_sounds`, `customize_item`, `cutscenes`, `customize_player`, `cutscene_tables`, `sounds_common`, `characters`, `sounds` |
| none | `preload_effects`, `preload_rigs`, `preload_items`, `player_morph`, `preload_anim`, `soundboot`, `da_tables`, `sr3_city_missions`, `sr3_city_1`, `sr3_city_0`, `high_mips` |

Flags as stored: voices 0x44, startup 0x49, shaders 0x49, misc_tables/misc/interface_startup/interface 0x5A, decals/effects/vehicles_preload/vehicles/sound_turbo/skybox/customize_item/customize_player 0x54, preload_* /player_morph/soundboot 0x50, items/characters 0x14, cutscene_sounds 0x44, cutscenes/cutscene_tables 0x5C, da_tables/high_mips 0x10, sr3_city_missions 0x38, sr3_city_0/1 0x30, sounds_common/sounds 0x04, patch_uncompressed 0x8A, patch_compressed 0x82. All names carry `.vpp` in the table and resolve to the `.vpp_pc` files. The meaning of bits 0x08/0x10/0x20/0x40 is not needed for boot (Residual R10).

### 2.3 Logo movies (intro video skip) — FINAL

- `0x007AD6F0` starts them: if the display option **`SkipIntroVideo`** is set (byte `0x0224253C`, read from `display.ini` by `0x007CB200`, key literal `"SkipIntroVideo "`), the state goes straight to "done" (3). Otherwise it opens `videos/logo_thq_and_voli_hd.bik`, state 1.
- Per frame (`0x007AD780`): when the movie ends **or a skip input is seen**, close it and advance; state 2 opens `videos/logo_amd_hd.bik`; state 3 is done. `0x007AD580` reports done; the logo loop in `0x005D3700` exits on done or on a quit request, then tears down (`0x007AD510`).
- **Skip inputs** (`0x007AD460`, only while the window is active): keyboard scan codes **1 (Esc), 0x39 (Space), 0x1C (Enter)**; gamepad 0 or 1 buttons **0, 1, 2, 3, 0x0C** (A, B, X, Y, Start in the `spec-tables-ui-controls.md` §4.6 numbering, HIGH for the letter names). One press skips the current movie only.
- Window inactive: the movie pauses, and a minimised window sleeps 500 ms per frame.
- Placement (`0x007AD590`): a 16:9 rectangle fitted to the back buffer (when the screen is wider than 16:9 the width is `height × 16/9`, centred; otherwise the height is `width × 9/16`, centred). HIGH for the exact rounding. **This resolves `spec-bink-boundary.md` §6.2**: the logo path is `0x005D3700` → `0x007AD6F0`/`0x007AD590` → `0x00BDDBD0`, called directly. The `0x00BDDCD0` gate of §6.1 there is a separate, still uncalled path.
- The `01_crawl.bik` intro and the other `videos/*.bik` are not played by the boot code (they are mission cutscene content).

### 2.4 Boot thread `"Boot_render"` (`0x00702070`): legal screen, save warning, loading bar — FINAL

A 7-state machine, 30 ms per iteration:

| State | What it draws | Leaves when |
|---|---|---|
| 0 | — | the legal image's load status is "ready" (→1) or every image was skipped (→6) |
| 1 | builds the legal texture, aspect-fit, centred | at once (→2) after starting a 1000 ms timer |
| 2 | legal, alpha ramps 0→1 | timer done; starts 2500 ms (→3) |
| 3 | legal, alpha 1 | timer done **and** UI ready (`0x014FF67E`); starts 1000 ms (→4) |
| 4 | legal, alpha 1→0 | timer done (→5) |
| 5 | — | next image; starts the **5000 ms save-warning timer**; →6 when no images remain |
| 6 | the UI (all live vint documents: `save_warning`, then `main_menu_load`) | thread exit condition below |

- **One legal image** (`0x00701DA0`): `ui_legal_pc.cpeg_pc` (texture `ui_legal_pc.tga`), replaced by `ui_legal_pc_us` when Steam `GetIPCountry` answers `US`, `CA` or `MX`.
- **Save warning**: while its timer runs, `0x00701FB0` reports "busy". At expiry it hides the `"safe_frame"` of the `save_warning` document. No loading percentage is pushed while busy.
- **Loading bar**: whenever the progress float changes and the `main_menu_load` document exists, the thread calls Lua `main_menu_load_complete(progress)` (`0x007ADD80`), which also sets Lua global `Main_menu_gameboot_complete = true`. At ≥ 1.0 it also requests the fade (`0x0059F8C0(0, none, 1)`).
- **Exit**: save warning done **and** bootstrap done (`0x014FF67D`) **and** state 6 → one last `main_menu_load_complete(1.0)`, then `0x01493663 = 1`.

### 2.5 Boot state machine

```
[entry] --0x005D1A30--> [mode 1 enter]
  -> LOGOS (thq_voli -> amd; skip: Esc/Space/Enter/pad A,B,X,Y,Start; SkipIntroVideo -> none)
  -> mount pass 2 -> BOOT THREAD starts (legal 1s in / >=2.5s hold until UI ready / 1s out
     -> save_warning 5 s -> main_menu_load "GAME_LOADING n%")
  -> UI init (opens main_menu_common + main_menu_top) -> engine bootstrap (progress 0.1..0.9)
  -> wait boot thread (progress 1.0) -> push mode 2 -> MAIN MENU (press start auto-accepted, §3.2)
```

---

## 3. Main menu — FINAL unless marked

### 3.1 Documents and screen codes

UI screens are global objects built by static initialisers; each stores its **screen code** at `+0x30`. `0x007B4750(code)` opens a screen; `0x007B3BA0(code)` asks if it is open.

| Code | Document (`.vint_doc` + Lua) | Screen object |
|---|---|---|
| 2 | `main_menu_campaign` | `0x022968D8` |
| 3 | `main_menu_common` (logo, list/header widgets, hint bar, transition anims) | `0x02296960` |
| 4 | `main_menu_top` (press start, top list, newsticker; pulls in `main_menu_load` as a dependency) | `0x02296BF0` |
| 5 | `main_menu_new_game` (difficulty) | `0x02296B68` |
| 9 | `pause_menu_common` | `0x02242688` |
| 10 | `pause_menu_top` | `0x0229CE38` |
| 11 | `pause_save_game` (both save and load lists) | `0x02297D88` |
| 12 | `pause_options_menu` | `0x02297048` |
| 17 | `pause_options_display_cal` | `0x02297C88` |

Also used: `bg_saints` (background video `main_menu_bg.bik`, Lua `main_menu_start_video`/`main_menu_stop_video`), `save_warning`, `city_load`, `dialog`. **This closes `spec-frontend-menu-runtime.md` §9 item 3**: the input-lock dispatcher `0x007AE0A0` fires `main_menu_new_game_block_input` when screen 5 is open and 17 is not, and `save_load_lock_input(true)` when screen 11 is open.

### 3.2 Mode 2 enter and per-frame

- **Enter** `0x007AE9E0`: unload the legal images and the `save_warning` document, tear down networking (`0x00867740`), release the menu-music handle, fade requests (`0x0059F8C0(0,none,1)`, `0x0059FC40(1000,none,1)`). Then, **if the `main_menu_top` document handle is already known** (`0x022425F8`) **and the static byte `0x012FCA11` is set** (shipped value 1, never written by code), it clears "started" and immediately accepts press start for the last controller (`0x012E6EC8`, −1 → 0) through `0x007ADEA0`. FINAL for the code [2026-10-19 corrected per Team D emulation 1d: FINAL]: `0x007AE9E0` is the enter callback of game state 2 (registered at `0x007AF121`); it accepts (`0x007ADEA0`, last controller `0x012E6EC8`, or 0 if -1) only when byte `0x022425F8` (runtime "main_menu_top shown" flag, starts 0 in BSS; set to 1 at `0x007ADD20` when `0x007ADCE0(1)` finds the vint document `main_menu_top` and at `0x007AEDA9`; cleared at `0x007ADD44`, `0x007AE618`, `0x007AEB83`) and byte `0x012FCA11` (1 in the file, never written, so always on for PC) are both non-zero. On the first entry it is taken only if `main_menu_top` was already shown (callers `0x005D2564`, `0x007040F8`, `0x007AD3FC` ran `0x007ADCE0(1)` first): HIGH. **Effect on PC: the press-start prompt is accepted automatically; the player lands on the top list.**
- **Per-frame** `0x007AED40`: until screens 3 and 4 are both loaded (class bit 8), keep requesting them. Then cache the `main_menu_top` doc handle (`0x022425EC`) and either show press start (`main_menu_top_press_start_show`, when returning after a disconnect) or `0x007AE160`. Then run the **main-menu worker `0x007AE420`** every frame.
- **Correction:** `0x007AE420` is that per-frame worker, **not** the Lua native `main_menu_continue`. The native is `0x007C9EB0` → `0x007ADE30` (registrar `0x007C9FC0`). Frontend §3 described the worker's body correctly.

### 3.3 Press start

- Lua side: `main_menu_load_finished` shows `press_start_grp` (pulse anim) and the logo.
- Native poll `0x007AE340` (only while not "started", i.e. when the automatic accept of §3.2 did not happen): **any keyboard key, mouse buttons 0–6, or pad 0/1 button 0x0C (Start) or 0 (A)** → `0x007ADEA0(controller)`. Both routes run the same code: play `UI_Main_Menu_Select`, mark "started" (`0x022425E8`), and run the storage/sign-in check `0x007AAC60`. On PC that check calls back at once with result **7** ("ok") unless it was asked for the slot/space checks (it is not, here).
- Callback `0x007ADA30(7)`: re-read the save directory (`0x00B94760`, up to 24 entries) and call Lua **`main_menu_controller_selected(show_continue)`**. Lua hides press start and builds the top list (`main_menu_top_menu_show`).

### 3.4 Top list (`main_menu_create_top_menu`)

Built in this order, each only when its condition holds:

1. **Continue** (`MAINMENU_CONTINUE`) — only if `Show_continue`.
2. **Campaign** (`MAINMENU_CAMPAIGN`).
3. **Co-op** (`MAINMENU_COOP_OPTION`).
4. **Whored** (`MAINMENU_WHORED`) — not in the German build.
5. **DLC** (`MAINMENU_DLC`) — on PC opens the Steam DLC overlay (`game_steam_open_dlc_page_overlay`).
6. **Options** (`MAINMENU_OPTIONS`) — PC only → screen `pause_options_menu` (§18.2 of `spec-tables-ui-controls.md`).
7. **Extras** (`MAINMENU_EXTRAS`) → `main_menu_extras`.
8. **Quit Game** (`PLT_QUIT_GAME`) — PC only.

Community and Machinima entries are commented out in the shipped Lua. A newsticker shows unless `game_is_demo()`.

**Continue rule (FINAL).** `Show_continue` is true exactly when the save directory has **at least one entry whose descriptor bytes `+0x5C` and `+0x5D` are both 0** (readable, right version). The worker picks, among those, the one with the **largest date-time key** (`spec-save-format.md` §8.4 formula) — the autosave counts. The list is first drawn without Continue; the worker re-fires `main_menu_controller_selected(true)` one frame later when it finds a candidate.

**Input on the top list** (`Vdo_input_tracker`, priority 50): `select` → activate; `back` → on PC opens the quit confirmation (§8); `nav_up`/`nav_down` → move; `map` is swallowed; mouse move/click on items.

### 3.5 Continue (`main_menu_continue`)

`0x007ADE30` sets the request bits; the worker re-enumerates and picks the newest readable save. None → one-button dialog `MENU_TITLE_NOTICE` / `MAIN_MENU_CONTINUE_UNAVAILABLE`. Found → `0x00B95210(entry, 1)` reads the file; on success: load flag `0x022425EA = 1`, input lock, fade out with teardown (`0x0059F8C0(-1, 0x007B4CB0, 1)`). The rest is §5.3.

### 3.6 Main-menu state machine

```
MODE2_ENTER --main_menu_top handle known (PC: always, HIGH)--> auto-accept --> TOP
PRESS_START --any key / mouse / pad A,Start--> TOP (list built; Continue if a readable save)
TOP --Continue--> [§5.3 load]          TOP --Campaign--> CAMPAIGN
TOP --Options--> pause_options_menu    TOP --Extras--> main_menu_extras
TOP --DLC (PC)--> Steam overlay        TOP --Quit / Esc (PC)--> QUIT_CONFIRM (§8)
CAMPAIGN: [New Game, Load Game]; back -> TOP
CAMPAIGN --New Game--> NEW_GAME (§4)   CAMPAIGN --Load Game--> LOAD_LIST (§5)
```

---

## 4. New Game — FINAL unless marked

### 4.1 Screens

1. **Campaign** list: `MAINMENU_NEW` (id 1), `SAVELOAD_LOAD_GAME` (id 2). Back pops to the top list.
2. **Difficulty** (`main_menu_new_game`, header `MAINMENU_NEW_GAME_DIFF_HDR`): `DLT_CASUAL`, `DLT_NORMAL`, `DLT_HARDCORE`; the cursor starts on `get_current_difficulty() + 1`; the description text follows the cursor (`DLT_DESC_*`).
   - **Input is ignored until the engine calls Lua `device_selected()`.** The screen's open method (`0x007CA530`) runs the storage check `0x007AAC60` with the **slot, space and autosave** options:
     - an autosave exists → warning `SAVELOAD_AUTOSAVE_OVERWRITE`, buttons `CONTROL_CONTINUE` / `CONTROL_CANCEL`;
     - no free slot → `START_GAME_OUT_OF_SAVE_SLOTS`, buttons `CONTINUE_WITHOUT_SAVING` / `CONTROL_CANCEL`;
     - no space → `START_GAME_OUT_OF_STORAGE_SPACE`, same buttons;
     - otherwise result 7 at once.
     Continue (or "continue without saving", which also disables saving via `0x00B946C0(0)` when no autosave exists) → result 7 → the screen flag `+0x80` (`0x02296BE8`) → next frame `device_selected()` → input on. Cancel → the screen pops. **On a fresh PC install with no saves, there is no prompt.**
3. **Select** a difficulty: `game_difficulty_select(id - 1)` (`spec-tables-ui-controls.md` §18.4). Then:
   - first time this session (`pause_menu_has_seen_display_cal_screen()` false, byte `0x02297C80`): push `pause_options_display_cal` with `Start_game_after_display = true`. Its A button saves the brightness (`pause_menu_accept_options()`) and calls `main_menu_new_game()`. Back returns to the difficulty list.
   - otherwise call `main_menu_new_game()` directly.

### 4.2 Engine side (`main_menu_new_game` → `0x007AE950`)

Already traced in `spec-frontend-menu-runtime.md` §2. Additions:

- The zone copied into the pending-zone buffer `0x02242558` comes from the buffer **`0x012FCA18`**, whose shipped content is `"sr3_city"` (a debug console handler, `0x007AD870`, can overwrite it; the frontend spec's "hard-coded literal" is the default content of that buffer).
- `0x0088BA10(4, 2)` stores **4 = the mode the session starter pushes next** (city load). The second value (2) is passed to `0x00704500`/`0x00704980` by the same session state (HIGH: a game-type code).
- After the fade-out teardown closes every screen, the main-menu worker sees the screen count `0x012FCEFC` reach 0. Without the load flag it builds a game-flow request (`0x0084E390`/`0x0084E290` with the pending zone), sets request bit 0 ("new game") because `0x022425DD` is set, submits it (`0x0084E360`) and calls `pop_to(1)+push(3)`.
- Then: mode 3 → `push(4)` → city load (§10.2) → `push(5)`.

### 4.3 Opening mission vs free roam

- **The engine never offers a free-roam start for New Game** [2026-10-19 corrected per Team D emulation 1d: FINAL]. The wrapper `0x00707380` returns 0 unless ALL hold: the level name at `0x013CDBA0` is "sr3_city" (8-character compare), byte `0x011239D0` is non-zero (build constant 1, never written, no pointer to it), not horde mode (`0x014F3D34`), and the session object (`0x0084E380()`) flags at `+0xD4` have bit 0 (new game) or bit 2 (loaded save) set; then it returns `0x006D74B0()`. `0x006D74B0` returns 1 ("the story owns the player") and 0 (free roam); if `0x0087BA20` returns an object whose `+0x5C` differs from `+0x58` it returns 0 first. With the mission lookups (`0x005F4C30` on table `0x02442750`, record `+0x88`: bit 2 = complete, bit 0 tested for m23) it gives 1 until m01, `mm_m1_5`, m02 and m03 are all complete, and again between m22 and m24 unless m23 bit 0 is set. Emulated results:
  - all missions present, none complete: 1
  - m01 only: 1
  - m01 + mm_m1_5 + m02: 1
  - m01 + mm_m1_5 + m02 + m03 complete: 0
  - opening done + m22 complete: 1
  - same + m23 bit 0: 0
  - same + m24 complete: 0
  - m01 record missing: 1
  - byte `0x011239D0` = 0: 0
  - wrapper: sr3_city / switch 1 / no horde / flags 1: 1; flags 4: 1; flags 0 or 2: 0; switch 0: 0; only `0x011239D1` set: 0 (the inner routine needs `0x011239D0`); horde: 0; level "dlc1": 0
  When true, the mission flow (`0x006D8FB0`) launches the story (HIGH that it is the launcher; `spec-zone-data-format.md` §11, `Start_player_nav` in `sr3_city~fm01`).
- **Minimal First Playable path into free roam (design choice):** the engine skips the story if `0x011239D0` is 0, or if all four opening missions (m01, `mm_m1_5`, m02, m03) are flagged complete (bit 2 of `+0x88`) before the check, or if the session flags at `+0xD4` lack bits 0 and 2. For First Playable, mark the four opening missions complete before the check, or start the session with flags lacking bits 0 and 2; `0x00707380` then answers false. Whether the game then lands cleanly in free roam (player spawned, HUD up) is a whole-game effect, still UNVERIFIED. Place the player at the single registered player-start marker or at a crib (`spec-save-format.md` A.3, ten crib transforms). The switch `0x011239D0` also gates `0x006CFFE0`, `0x0084AF70` and `0x00A53B40`, so mission-start markers stay inert if it is used.
- Character creation: the lineup/creation screens are driven by the opening mission's scripts (`store_character.lua`, `spec-frontend-menu-runtime.md` §9 item 1); not part of the menu flow (Residual R1).

### 4.4 Initial player state

Owned by `spec-save-format.md` A.3: zone `sr3_city`, time 10:00:00, play time 0, respect 0/0/0, multiplier 1.0, default identity from `player_presets.xtbl`, wardrobe from `pcr_add_default_clothing`, starting cash **OPEN** (recommended $57.00; Team D 2026-10-19: there is no code constant, so the value comes from a script, a table or a save default; the recommendation stands). Difficulty = the value chosen in §4.1.

### 4.5 New Game state machine

```
DIFFICULTY(locked) --storage check: ok / continue--> DIFFICULTY(input on)
DIFFICULTY --cancel in a storage warning--> CAMPAIGN
DIFFICULTY --select, cal not seen--> DISPLAY_CAL --A--> START   (B -> DIFFICULTY)
DIFFICULTY --select, cal seen--> START
START: input lock -> fade out -> all screens close -> request(zone, new) -> pop_to(1)+push(3)
      -> SESSION -> push(4) CITY_LOAD -> push(5) GAMEPLAY (opening mission auto-starts, §4.3)
```

---

## 5. Load Save — FINAL unless marked

### 5.1 The list screen (`pause_save_game`, operation LOAD)

- Opened from Campaign → Load Game (main menu) or Pause → Load Game. Header `SAVELOAD_LOAD_GAME`, width 800, 8 visible rows, columns: description (left), completion %, play time `h:mm`, date-time string (right). Input waits for `device_selected()` (fired by the screen's own storage check, `0x007D0830` → callback `0x007D0410`; immediate on PC).
- **Rows come from the data responder `save_system_populate`** (`0x007CFB60`), which emits, per directory entry, 17 values in this order: handle (directory index), is_loadable (`+0x5C` and `+0x5D` both 0), is_autosave (summary row 8), is_cheat, description, completion %, play time (minutes), date-time string, difficulty (localised name), missions completed, districts owned, cash, rank (level), respect in level as a fraction, total respect, district-control fraction, last mission id. The side panel shows play time, completion, difficulty, missions, cash, a respect meter, a district meter, a cheat marker and a picture `ui_save_<last_mission+1>` (`ui_save_mm` past 24).
- Description override: `SAVELOAD_BAD_VERSION` when `+0x5D` = 1; `SAVELOAD_SAVEFILE_CORRUPT_TITLE` when `+0x5C` = 1.
- **No entries in LOAD mode** → one row `SAVELOAD_NOTHING_TO_LOAD` (handle −1, not selectable). Directory read error → one row with handle −1.
- **Order** (built by the directory reader `0x00B967E0` → sorted insert `0x00B94DB0`): (1) the **autosave** first; a newer autosave replaces an older one in place; (2) readable saves, **newest date-time first**; (3) unreadable entries (corrupt or wrong version) last. At most 24; when full, the last row is dropped before an insert.
- Delete: **Del** (scan code 211) or pad Y → `MENU_TITLE_WARNING` / `MENU_DELETE_SAVE` confirmation → `save_system_delete_game` (`spec-lua-api-behaviour.md` §39.4, silent-drop defect noted there). Back → previous screen.

### 5.2 Confirmations

- **Main menu:** no confirmation; select loads at once.
- **In game (pause):** destructive confirmation `SAVELOAD_LOAD_GAME` / `SAVELOAD_LOAD_GAME_CONFIRM_EXPOSITION`. Load is refused while autosaving (`SAVELOAD_CANT_ENTER_AUTOSAVE`) or while a co-op partner joins (`SAVELOAD_CANT_ENTER_JOINING`).
- The Lua does not check `is_loadable`: choosing a corrupt row calls the load, the read fails, and the error dialog of §7.4 appears.

### 5.3 Load flow

`save_system_load_game(handle)` (`0x007D0E60`) stores the handle and runs `0x007D0CA0`:

- **Main menu (mode 2 on top):** `0x00B95210(entry, 1)` reads the file (`spec-save-format.md` §11.1 steps 1–2). On success `0x007AEC20`: load flag, input lock, fade out with teardown. When all screens are closed the worker calls **`0x00B947A0`** (apply/level change). With no session object it calls `0x0088BA10(4, 2)` and `pop_to(1)+push(3)` → mode 3 → `push(4)` → city load → `push(5)`.
- **In game:** with no co-op guests, a 0 ms fade whose completion runs `0x007D0C50` (read, flag 0); with guests, `0x007D0C50` directly. `0x00B947A0` then takes the session branch `pop_to(3)+push(4)`.
- The handle bound check accepts `handle == count` (off-by-one, `spec-lua-api-behaviour.md` §39.4).

**Restoring state:** the order and placement rules of `spec-save-format.md` A.1 (streaming wait first, then the ~50 resets, version gate, entries 0→31, crib placement). Not repeated here.

---

## 6. Pause — FINAL (cross-reference plus one correction)

Owner: `spec-tables-ui-controls.md` §18 (top list, options, rebinding, difficulty). This pass corrects only the open/close mechanism:

- **Open (FINAL):** the screen-system per-frame `0x007B5A50` calls `0x007B4190`, which returns true when the top mode is **5 (gameplay)**, the fade is settled, no pause/map screen is already open, no cutscene runs, and **`CBA_GAC_PAUSE_MENU` (0x55) or `CBA_GAC_MAP_MENU` (0x56)** is edge-pressed. Then `push(6)` with a 500 ms re-press guard. Mode 6's enter opens **screens 9 + 10 (`pause_menu_common` + `pause_menu_top`)**, or **screen 25 (the map)** when the map action was the trigger (byte `0x0229CD98`). Default keys: Esc (pause), Tab (map).
- **Close (FINAL):** `pause_menu_common`'s close method `0x007AF580` pops pause reason `0xD` if it is active, and pops mode 6 when it is on top.
- **Freeze (HIGH):** mode 6 is an overlay, so gameplay's per-frame still runs. The freeze is the shared pause depth (`spec-lua-api-behaviour.md` §37.3), raised with reason `0xD` by the dialog-class rule of `spec-frontend-menu-runtime.md` §4 while the pause screens are open, and dropped by the close method above.
- **Correction to `spec-frontend-menu-runtime.md` §7 and `spec-tables-ui-controls.md` §18.1:** the chain `0x00673160` → `0x007C1EF0` → pause push reason 4 → `"dialog_pause_show"` drives the `dialog.lua` "PAUSED" / partner-paused overlay (`dialog_pause_show(partner_paused, just_show_disconnect)`), not the pause menu. The pause-menu open is the mode-6 path above. The Lua-side behaviour in §18.1 is unchanged.

---

## 7. Save screen (from the pause menu) — FINAL unless marked

### 7.1 Entry

Pause → Save Game. Refused while autosaving (`SAVELOAD_CANT_ENTER_AUTOSAVE`). The first time per session during a mission, silent mission or stronghold → `SAVELOAD_MISSION_WARNING`; during an activity or active diversion → `SAVELOAD_ACTIVITY_WARNING`. The warning shows once and then proceeds (`spec-save-format.md` A.2). The screen is `pause_save_game` with operation SAVE (header `SAVELOAD_SAVE_GAME`).

### 7.2 List in SAVE mode

- First row **`SAVELOAD_NEW_SAVE_GAME`** (handle 99), then the directory entries in the §5.1 order.
- **The autosave is not listed in SAVE mode** (the populate routine skips entries with the autosave marker when the operation is SAVE). A player cannot overwrite the autosave by hand.
- Hints on PC: Back; Del to delete (not on the New row); Enter = `TRIGGER_SAVE`.

### 7.3 Save flow and timing

`save_system_save_game(handle)` (`0x007D0DD0`, `spec-save-format.md` §15.1):

- **New row (99):** slot-availability check (`SAVELOAD_NOT_ENOUGH_SLOTS_NEW`), space check (`SAVELOAD_NOT_ENOUGH_SPACE_NEW`), then the save entry `0x00B94A10`.
- **Existing row:** two-button confirmation `SAVELOAD_SAVE_GAME` / `SAVELOAD_SAVE_GAME_CONFIRM_EXPOSITION`; Yes → save into that slot (**overwrite**); No → back to the list.
- The Lua locks input before the call (`save_load_lock_input(true)`).
- **Screen state machine** (`0x007D0930`, screen object `0x02297D88`): state 0 shows `SAVELOAD_SAVING_MESSAGE_TITLE` / `SAVELOAD_SAVING_MESSAGE_EXPOSITION` → state 1 polls "saving" (`0x00B94680`) every frame and pumps the writer (`0x00B94F70`) → state 2 closes the dialog. If the last error is −1 (none): `save_load_refresh` (list rebuilt) and input unlock. Otherwise: the error dialog (§7.4) and input unlock. **The screen stays open after a successful save.** The list cannot be populated while a save runs: the responder shows the Saving dialog and returns "not finished".
- The game is paused during all of this (pause depth from §6). The write itself is asynchronous (`spec-save-format.md` §6.3).

### 7.4 Error dialogs (`0x007D05A0`, title `SAVELOAD_ERROR`)

| Last error | Body |
|---|---|
| 0 | `SAVELOAD_NO_PROFILE_FOUND` |
| 1 | `SAVELOAD_SAVE_GAME_DEVICE_FULL_EXPOSITION` |
| 2 | `SAVELOAD_SAVE_DEVICE_UNAVAILABLE_FULL` |
| 3, 5 | `SAVELOAD_FILE_NOT_LOADABLE` |
| other (4, 6) | `SAVELOAD_UNKNOWN_ERROR` |

FINAL for the strings and the jump table; the error-code meanings are in `spec-save-format.md` §11.1.

### 7.5 Autosave rule

Owned by `spec-save-format.md` A.2 and §15.2: `game_autosave` after a building purchase or a finished collection, `mission_autosave` from mission scripts, refused while a mission is active (`game_autosave` only), while already saving, or before start-up ends. The autosave goes to the slot with the autosave marker (summary row 8). Not repeated.

### 7.6 Save-screen state machine

```
LIST(locked until device_selected) --New row--> [slot/space checks] --ok--> SAVING
LIST --existing row--> CONFIRM_OVERWRITE --Yes--> SAVING ; --No--> LIST
SAVING (dialog, polled per frame) --done ok--> LIST (refreshed, unlocked)
SAVING --done error--> ERROR_DIALOG --> LIST (unlocked)
LIST --Back--> pause_menu_top ; LIST --Pause key--> close pause entirely (3 pops)
```

---

## 8. Quit — FINAL unless marked

### 8.1 From the pause menu: quit to the main menu

- Pause → Quit → destructive confirmation `PAUSE_MENU_QUIT_TITLE` with body `QUIT_GAME_TEXT_FULL` (horde: `HORDE_MODE_QUIT_TEXT`). **There is no separate unsaved-progress check**; the warning is in that fixed text (the localised text is not decoded here, Residual R8).
- Yes → `pause_menu_quit_game_internal()` → 0 ms fade with callback `0x007E0450`:
  1. flag `0x01503EA4 = 1` ("quit to menu");
  2. `0x00877410`;
  3. `0x00707880(0)`: a 1000 ms fade out (only when a world is loaded), close every screen, set `0x01503EBA = 1`;
  4. an audio event (`0x00554270`, event id `0x1AF954E9`).
- Every frame the main loop calls `0x00707710`: when `0x01503EBA` is set and the top is not 4 or 8 and the fade has settled, the world teardown predicate `0x007029F0` is polled; when ready: a 250 ms fade (`0x0059FC40`) and **`pop_to(1)+push(2)`**. Once mode 2 is on top the flag clears.
- Mode 2 enter then re-opens the main menu, skipping press start (§3.2). Gameplay's leave (`0x00703F50`) re-opens screens 3 and 4 (`0x007ADCE0`).

### 8.2 From the main menu: quit to the desktop

- Quit Game, or **Esc / back on the top list (PC)** → destructive confirmation `PLT_QUIT_GAME` / `PLT_QUIT_GAME_WARNING`. Yes → `game_request_game_terminate()` (`0x00842730` → `0x00707810`): set the once-only quit latch `0x01503EC3`, and `push(10)` unless the city-load mode is on top (that mode pushes 10 itself when it sees the latch).
- The window close button and other quit requests go through the same latch (HIGH).

### 8.3 Quit state machine

```
PAUSE_TOP --Quit--> CONFIRM --No--> PAUSE_TOP
CONFIRM --Yes--> FADE(0ms) -> close UI, 1 s fade -> wait teardown -> 250 ms fade -> pop_to(1)+push(2) -> MAIN MENU (no press start)
MAIN_TOP --Quit / Esc--> CONFIRM --Yes--> push(10) -> exit ; --No--> MAIN_TOP
```

---

## 9. Vint documents and the natives the menu Lua calls — FINAL

### 9.1 Documents in flow order

`save_warning` (boot) → `main_menu_load` (boot loading text `GAME_LOADING n%`) → `main_menu_common` + `main_menu_top` + `bg_saints` (main menu) → `main_menu_campaign` → `main_menu_new_game` → `pause_options_display_cal` → `city_load` (loading screen) → in game: `pause_menu_common` + `pause_menu_top` → `pause_save_game` → `dialog` (all confirmations). Doc format: `spec-vint-doc-format.md`; rendering: `spec-vint-render.md`.

### 9.2 Engine → Lua calls (hooks the engine fires)

| Lua function | Fired by | When |
|---|---|---|
| `main_menu_load_complete(p)` + global `Main_menu_gameboot_complete` | `0x007ADD80` (boot thread) | progress changes; last with 1.0 |
| `main_menu_controller_selected(show_continue)` | `0x007ADA30`, `0x007AE420` | after press start; again when Continue is found |
| `main_menu_top_press_start_show` | `0x007AEC40` | back to press start after a disconnect |
| `main_menu_start_video(has_pressed_start)` / `main_menu_stop_video` | `0x007ADC00` / `0x007ADB30` | background video |
| `main_menu_new_game_block_input`, `save_load_lock_input(b)` | `0x007AE0A0` | before a start/load fade |
| `device_selected` | `0x007CA590` (new game), `0x007D0AF9` (save/load) | storage check passed |
| `save_load_refresh`, `save_load_button_b` | `0x007CFA20`, `0x007D0A60` | after a save; forced close |
| `city_load_hide_images` | `0x007ACD40` | load done |
| `main_menu_internet_disconnected`, `main_menu_ethernet_disconnected` | `0x007AED40` | network loss (co-op) |
| global `Pause_menu_save_for_server_drop` | `0x007ADCE0`, mode 6 | co-op host drop |

### 9.3 Lua → engine natives used by these screens

Every native the front-end Lua calls (`fe_natives.py`, 70 names) already has an entry in `spec-lua-api-behaviour.md` or `spec-lua-bindings.md`. Those on the First Playable path:

- **Boot and menu:** `main_menu_new_game`, `main_menu_continue`, `game_difficulty_select`, `get_current_difficulty`, `pause_menu_has_seen_display_cal_screen`, `pause_menu_accept_options`, `game_request_game_terminate`, `main_menu_restart_video`, `main_menu_video_has_stopped`, `main_menu_money_shot_check` (no-op), `game_get_platform`, `game_get_language`, `game_is_german_build`, `game_is_demo`, `game_steam_open_dlc_page_overlay`.
- **Save and load:** `save_system_set_operation` (dead write), `save_system_save_game`, `save_system_load_game`, `save_system_delete_game`, `save_system_change_device` (no-op on PC), `save_game_wrap_up` (no-op), `game_is_autosaving`, `game_get_in_progress_type`, the data responder `save_system_populate` (through Lua `vint_dataresponder_request`).
- **Pause and quit:** `pause_menu_quit_game_internal`.
- **Loading screen:** `city_load_img_load_complete`.
- **UI plumbing:** `push_screen`, `pop_screen`, `vint_document_find`, `vint_object_find`, `vint_set_property`, `vint_get_property`, `vint_apply_start_values`, `vint_insert_values_in_string`, `vint_is_std_res`, `game_peg_load_with_cb`, `game_peg_unload`, `thread_new`, `thread_yield`, `thread_kill`, `game_UI_audio_play`, `game_get_key_name`.

`lua_play_anim`, `vint_dataresponder_request` and `menu_common_transition_push`/`_pop` are Lua (`vint_lib.lua`, `menu_common.lua`): transitions play the out-animation, then `push_screen`/`pop_screen` run in its end callback.

---

## 10. Loading screens — FINAL unless marked

### 10.1 Boot loading

§2.4: legal screen → save warning (5 s) → `main_menu_load` with "GAME_LOADING n%". The percentage takes the values 5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100. It is drawn by the boot thread at about 33 frames per second while the main thread loads.

### 10.2 City loading screen (mode 4, overlay)

- **Enter** `0x007ACE10`: reset the flags, stop the menu music, record the start time, and create the `city_load` document (`0x007B1CB0("city_load", 0)`) when mode 4 is the top.
- **Lua** `city_load_init`: picks 3 different random images from `ui_mainmenu_load_01..11` (with `_pc` suffix), avoiding those already loaded by `screen_fade`. When they are loaded it calls `city_load_img_load_complete()` and shows them after **5 s** (`IMAGE_DELAY`), cycling a transition animation; spinner and "loading" pulse loop.
- **Per-frame** `0x007AD210`:
  1. Wait until one of `0x00BC0E50`, `0x00BC0DD0`, `0x007076B0` is true, or **10 s** have passed (then `0x00BC0E40`). HIGH: these are cutscene/teardown readiness checks; their meaning is Residual R4.
  2. Wait for the document and the image-loaded flag.
  3. Start the worker thread **`"city_load_thread"`** (`0x007ACFE0`): it builds the Havok job pool, runs the world bring-up `0x0084E3B0` (`spec-save-format.md` A.1 steps 3–6), and stores the result in `0x012FCA04` (+1 ok, −1 failed).
  4. Menu music: the static bytes `0x012FCA00` and `0x012FCA10` are 1 in the shipped image, so enter does not stop the `"Menu Music"` bus and the per-frame restarts `main_menu_music` if it ended. **The menu music plays through the loading screen.**
- **Result:**
  - quit latch set → `push(10)`;
  - ok and the player object is ready (`0x009F26B0(player +0x2088, 1)`) → **`push(5)`** (gameplay); the document is hidden (`city_load_hide_images`) and destroyed on leave;
  - failed → destroy the document, re-open screens 3/4 (`0x007ADCE0(0)`), `pop_to(1)+push(2)` (back to the main menu).
- **Leave** `0x007ACEC0`: joins the thread if needed, resets session teardown helpers, destroys the document.

### 10.3 Loading state machine

```
CITY_LOAD.enter (doc created) -> WAIT(ready-check or 10 s) -> WAIT(doc + images)
  -> THREAD(0x0084E3B0 world bring-up) -> ok:   push(5) GAMEPLAY
                                       -> fail: pop_to(1)+push(2) MAIN MENU
                                       -> quit: push(10)
```

---

## 11. Test vectors

| # | Input / situation | Expected |
|---|---|---|
| T1 | `display.ini` has `SkipIntroVideo = true` | no logo movie; boot goes straight to the legal screen |
| T2 | THQ logo playing, Space pressed once | THQ closes, AMD logo starts; a second press skips AMD |
| T3 | Steam country `US` | legal image `ui_legal_pc_us`; any other country or no Steam → `ui_legal_pc` |
| T4 | Boot with fast loading | legal ≥ 4.5 s total (1 + ≥2.5 + 1), then save warning 5 s, then `main_menu_load` at 100 %, then the top list (press start accepted automatically, HIGH) |
| T5 | Boot progress stores | the `main_menu_load_complete` argument sequence is a subsequence of 0.05, 0.1, …, 0.9, 1.0, ending with exactly 1.0 |
| T6 | Mode stack after boot | `[1, 2]` |
| T7 | PC, English, no saves | top list: Campaign, Co-op, Whored, DLC, Options, Extras, Quit Game |
| T8 | PC, German build, one readable manual save | Continue, Campaign, Co-op, DLC, Options, Extras, Quit Game |
| T9 | Only save is corrupt (`+0x5C` = 1) | no Continue; the Load list shows it last, titled `SAVELOAD_SAVEFILE_CORRUPT_TITLE` |
| T10 | Saves: autosave 2026-01-02, manual A 2026-01-05, manual B 2026-01-03 | Load list: autosave, A, B; Continue loads A (newest readable) |
| T11 | Same saves, Save list | New Save Game, A, B (autosave hidden) |
| T12 | 24 entries listed, then one more is inserted | still 24 rows; the row that was last drops off (the list only, not the disk) |
| T13 | New Game, no saves, cal not seen | difficulty screen → display cal → `main_menu_new_game`; stack `[1,2]` → `[1,3]` → `[1,3,4]` → `[1,3,4,5]` |
| T14 | New Game while an autosave exists | warning `SAVELOAD_AUTOSAVE_OVERWRITE`; Cancel → back to Campaign; Continue → difficulty input enabled |
| T15 | New Game with the four opening missions (m01, mm_m1_5, m02, m03) flagged complete (record +0x88 bit 2), or session flags +0xD4 without bits 0/2 (§4.3, FINAL by Team D emulation) | `0x006D74B0` returns 0, so `0x00707380` returns false; no mission launch; free roam at the start marker (the clean landing is unverified) |
| T16 | Load from the main menu | no confirmation; fade; `0x00B947A0`; stack `[1,3]` → `[1,3,4]` → `[1,3,4,5]` |
| T17 | Load from pause | confirmation; Yes → stack `[1,3,4,5,6]` → `[1,3,4]` → `[1,3,4,5]` |
| T18 | Esc in gameplay | `push(6)`; screens 9 and 10 open; Tab instead opens screen 25 |
| T19 | Save over manual A | confirmation; Yes → "Saving" dialog until done → list refreshed with A's new date at the top of the manual saves; screen stays open |
| T20 | Save while autosaving | `SAVELOAD_CANT_ENTER_AUTOSAVE`; no screen |
| T21 | Pause → Quit → Yes | 1 s fade; stack `pop_to(1)+push(2)` = `[1,2]`; main menu without press start |
| T22 | Main menu Esc → Yes | `push(10)`; exit; a second request does nothing (latch) |
| T23 | City load fails (`0x012FCA04` = −1) | stack `pop_to(1)+push(2)`; main menu |
| T24 | Screen-code queries | `open(5)` = main_menu_new_game; `open(11)` = pause_save_game; `open(17)` = pause_options_display_cal |

---

## 12. Corrections made to other specs by this pass

1. `spec-frontend-menu-runtime.md` §1: mode 3 = session start, mode 4 = city loading screen, mode 6 = pause menu/map, mode 9 = co-op synced pause (§1.1 here). Its §9 item 5 is closed (`+0x27C` = the mode the session pushes next).
2. `spec-frontend-menu-runtime.md` §3: `0x007AE420` is the main-menu per-frame worker; `main_menu_continue` is `0x007C9EB0` → `0x007ADE30`.
3. `spec-frontend-menu-runtime.md` §6/§9 item 3: screen codes 2/3/4/5/9/10/11/12/17 resolved (§3.1). Its §7 sentence about `0x007AF660`/`0x007AF6C0` ("controller reconfiguration prompt") is wrong: codes 9/10 are `pause_menu_common`/`pause_menu_top`, so those functions re-open the pause menu.
4. `spec-frontend-menu-runtime.md` §7 and `spec-tables-ui-controls.md` §18.1: the pause-menu open is the mode-6 path (§6 here); reason 4 / `dialog_pause_show` is the "PAUSED" overlay.
5. `spec-render-pipeline.md` §6.6: boot order established (§2.1).
6. `spec-bink-boundary.md` §6.2: the logo movies are played by `0x007AD590`, not through `0x00BDDCD0` (§2.3).

Short pointer notes were added in place to the frontend and UI-controls specs; the full text lives here.

---

## 13. Residual (not FINAL, and why)

| # | Item | Why it is open | Blocks First Playable? |
|---|---|---|---|
| R1 | Where character creation sits relative to the opening mission | Driven by mission Lua, not by menu code; not traced | No (the free-roam path of §4.3 skips it) |
| R2 | Starting cash | Owned by `spec-save-format.md` A.3: exhaustive negative, $57.00 recommended | No |
| R3 | Free-roam New Game via the story gate | The gate logic is FINAL (Team D emulation, §4.3); that the whole game lands cleanly in free roam with the opening flagged complete is unverified (whole-game behaviour) | No (design choice) |
| R4 | The city-load readiness checks `0x00BC0E50`/`0x00BC0DD0`/`0x00BC0E40` | Bodies not opened; the 10 s fallback is FINAL | No: a reimplementation can proceed at once |
| R5 | Second argument of `0x0088BA10` (2) | Passed to `0x00704500`/`0x00704980`, not opened | No |
| R6 | Descriptor byte `+0x5E` (from `0x0290D9D0[slot]`) | Makes the row read `SAVELOAD_SAVE_GAME` with blank missions; producer not traced | No |
| R7 | Whether the display-calibration "seen" byte persists across sessions | Set by the calibration screen; no profile write traced | No (shows once per session at worst) |
| R8 | Text of `QUIT_GAME_TEXT_FULL`, `PLT_QUIT_GAME_WARNING` | Localisation strings not decoded here | No |
| R9 | `0x007075E0`, `0x007E1B10`, `0x007E1C80` | Transition helpers with no reference anywhere in the image (dead or computed); behaviour is consistent with §8 | No |
| R10 | Registry flag bits 0x08/0x10/0x20/0x40 | Not needed for mounting; meaning not traced | No |
| R11 | Mode slots `+0x14`/`+0x1C` exact call rule | `+0x0C` rule FINAL; the other two HIGH | No |
| R12 | The object that owns `0x00673160` (the "PAUSED" overlay trigger) | Reached only through a function-pointer table | No |
| R13 | Press start auto-accepted on the very first main-menu entry | Code path FINAL; that the `main_menu_top` handle is already cached at that moment is HIGH (inferred from the boot loading bar needing it); a live boot would settle it | No (either way the player reaches the top list) |

---

## Changelog

- 2026-10-19: file created (SPEC TEAM, static RE with pefile/capstone; read-only game data). Coverage audit of the nine First Playable steps; filled boot order, mount registry, logo skip, legal/save-warning/boot-loading thread, game-mode table (modes 3/4/6/9 identified), screen-code map, main-menu worker, press start, top list and Continue rule, New Game storage gate and story auto-start predicate, save/load list fields and ordering, save-screen state machine and error table, pause open/close mechanism, both quit paths, city loading screen; 24 test vectors; 13 residual items. Pointer notes added to `spec-frontend-menu-runtime.md` (top) and `spec-tables-ui-controls.md` §18.1. Cleanroom: no leaked source; no decompiler-style identifiers.
