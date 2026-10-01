# Saints Row: The Third — Lua-to-Engine Binding Surface Specification

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1, Target #9 (from `spec-output.md` §4 (unpublished Team A working document, not in this repository))
**Scope:** The custom C-side API surface connecting the embedded Lua 5.1 VM to the engine — mission control, UI hooks, animation/object binding. **Explicitly out of scope: the Lua 5.1 VM and its standard library themselves** (public, unmodified, not proprietary).
**Method:** Ghidra-based static analysis (decompiler + cross-reference database) of `SaintsRowTheThird.exe`, using the existing local Ghidra project. No dynamic analysis/debugging was used. A broad, automated scan (every `CALL` instruction in `.text`, checked for a nearby reference to an identifier-shaped string in `.rdata` and a nearby reference to another function in `.text`) surfaced the dispatch/registration call sites described below; individual sites and their containing functions were then decompiled and read manually to confirm what each pattern actually does, rather than trusting the automated scan's matches at face value — consistent with this project's standing rule that pattern-matching alone (without reading and understanding the actual mechanism) is not sufficient evidence.
**Cleanroom compliance:** No decompiled code, disassembly listings, or original internal identifiers are reproduced verbatim below — every mechanism is described in this document's own words. Ghidra's auto-generated placeholder names (`FUN_00xxxxxx`) are not Volition-authored identifiers; they are not reproduced here except as addresses-as-evidence, consistent with how prior specs in this project cite function addresses as supporting evidence for a Ghidra-derived finding. The bound/hooked function **names** quoted throughout (e.g. `garage_vehicle_load_begin`, `screen_fade_do`) are literal, human-readable strings stored as data in the executable — they are the actual, load-bearing API surface this document exists to catalog, exactly analogous to quoting real `.xtbl` field names or table filenames elsewhere in this project. The standard Lua 5.1 API and its well-known public type constants (e.g. `LUA_TFUNCTION` = 6) are cited as public facts about a public library, not proprietary information.

**Confidence key** (as in prior specs): **CONFIRMED — empirical/disassembly**, **HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**.

**Review status summary (2026-09-30).** A desk adversarial review checked every unit of this spec against the other specs (and, for §12–§17, against Team B's name and data files); nothing was re-derived from the executable. §1–§11 (24 units): 7 DESK-PASS, 9 DESK-PASS with text fixes applied, 8 NEEDS-EXE. §12–§17 (33 units): 9 DESK-PASS, 12 DESK-PASS with text fixes applied, 12 NEEDS-EXE. Each unit ends with its own review-status line. **A desk pass does not clear a unit for implementation; clearing needs re-derivation against the executable.** High-impact findings for implementers:
(a) the 8 dual-registered names of §13.5 (`audio_object_post_event`, `audio_stop`, `coop_is_active`, `game_is_active_input_gamepad`, `hud_display_set_element`, `hud_display_create_state`, `hud_display_commit_state`, `hud_display_remove_state`) are registered into BOTH Lua states, although the single-tag name lists (`lua_all_registered_1430/1435/1490_tagged.txt`) tag all 8 `gameplay` only;
(b) the 24-pair bare-global block registered by `0x00e0f900` (§13.2, §16.3) is Lua-visible but outside the 1,490 count, and it is not pure standard library (§16.3: it includes the engine primitive `FUN_00e0f010`); Team B's `lua_reconciliation_called_not_registered_101.tsv` shows real scripts calling bare globals absent from every census file — `debug_print` 816 calls, `max` 704, `floor` 449, `rand_int` 166, `abs` 97, `min` 87, `rand_float` 84, `round` 37, `ceil` 28 — ~~but which of these are among the 24 pairs, and which state receives the block, is OPEN (§13.2)~~ **[Settled 2026-10-01, job `20261001T020218-team-a-bgcx`: all nine of these names are among the 24, and 15 of the 24 are engine functions, not standard math; the 24 are bare globals, registered into BOTH Lua states by the generic state creator `0x00e0e0b0` before `system_lib.lua` and before every other registrar. CONFIRMED — disassembly. Roster: §13.2; behaviour of `rand_int`, `rand_float`, `round`, `debug_print`/`assert_msg`: `spec-lua-api-behaviour.md` §26.27.]**;
(c) other fixes: `DAT_02319570` is `game_peg_load_with_cb`'s request-slot table, not a mission trigger table (§8.5); the `0x0087ba20` gate is the co-op host check, not "mission active" (§12.5, §13.6); the §9.3/§12.7 setter-slot orientation is still unresolved.

---

## 1. Overview

The engine embeds a genuine, unmodified **Lua 5.1** interpreter. **[CONFIRMED — empirical.]** Evidence: a literal `"Lua 5.1"` version string, a panic-handler message (`"PANIC: unprotected error in call to Lua API (%s)"`) matching Lua's own standard startup behavior, and — most concretely — the actual, byte-for-byte standard-library registration tables for Lua's built-in `base` library (`assert`, `collectgarbage`, `dofile`, `error`, `gcinfo`, `getfenv`, ...) and `coroutine` library (`create`, `resume`, `running`, `status`, `wrap`, `yield`), found as ordinary `{name, function}` static arrays in `.rdata`. **None of this — the VM or its standard library — is investigated further here**, per the target's own scope.

On top of this stock VM, the engine layers **two distinct custom binding surfaces**, both real and both confirmed this pass, connecting Lua/UI scripts to engine code:

1. **A C++ callback-object mechanism (`vint_callback_lua`) for exposing individual C functions to be called *from* Lua** — the direction most people mean by "Lua bindings." Confirmed to exist and to be backed by a dedicated object pool; the exact per-binding name↔function mapping was **not** extracted at the byte level this pass (§3). **[⚠ Refuted §7.3: `vint_callback_lua` holds Lua-supplied callback names, not C functions exposed to Lua; the real fixed C-function roster is the `lua_register` surface of §13.]**
2. **A much more thoroughly-confirmed "named hook" mechanism for the engine to call *into* Lua/UI scripts by well-known, conventional name** — e.g. "if the currently-loaded UI script defines a function called `garage_vehicle_load_begin`, call it when a garage vehicle load begins." This direction yielded hundreds of real, extracted hook names across mission, UI, HUD, and minigame systems (§4), plus a related, structurally distinct **UI document element-type registry** covering the "animation/object binding" part of this target (§5).

Both mechanisms are described in Volition's own terms (found via string evidence, §2) as part of a system called **"Vint"** — the game's UI/interface-document scripting layer, itself implemented in Lua.

**⚠ A third, later pass (2026-09-29, §13) found a genuinely separate mechanism from both of the above: the
plain, public Lua 5.1 C API's own `lua_register` idiom (`lua_pushcclosure`+`lua_setfield`/`LUA_GLOBALSINDEX`),
used directly — not via any custom wrapper — to expose 1,325 [⚠ current total 1,490, across five registrar sites — §13.7; 1,325 was the first count, §13.2] individually-named engine C functions to two Lua
states (a UI state and a separate "game play"/mission state). This is the classic "mission script calls a
large roster of engine functions by name" surface (`waypoint_add`, `vehicle_show`, `ai_attack_region`,
`world_despawn_all_pedestrians`, etc.) that neither mechanism above actually is. See §13 for the full account.**

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

## 2. The "Vint" custom-Lua-library system

A dedicated startup routine loads a fixed set of named custom Lua script files by iterating a counted array of file-path strings and "opening" each one (a call taking the path string and a context value; a load failure is logged via a literal, Volition-authored message: `"Unable to open custom lua library %s"`). **[CONFIRMED — disassembly.]** Real library names found this way (and independently corroborated by a broader string scan of the executable) include: **[⚠ §16.1: the counted array (`0x01162da0`, count 4) holds only 4 of these — `game_ui_globals`, `vdo_base_object`, `vdo_anim_object`, `vdo_input_tracker`; `system_lib`/`vint_lib`/`game_lib` load via other paths, §16.1–§16.2.]**

- `vint_lib.lua`, `system_lib.lua`, `game_lib.lua`, `game_ui_globals.lua` — general-purpose Lua-side library/utility scripts.
- `vdo_base_object.lua`, `vdo_anim_object.lua`, `vdo_input_tracker.lua` — "VDO" = **Vint Document Object**, the base classes for UI-layout elements (ties directly into §5's element-type registry, which uses the identical naming theme).

Related, corroborating strings found elsewhere: `"Lua_Script"`, `"mission lua temp"`, `"Mission LUA script"`, `"Vint LUA script"`, `"Lua script HUD"`, `"LUA 2D"`, `"LUA 3D"` — consistent with Lua scripting being used across mission logic, the HUD, and both 2D and 3D UI presentation layers, matching the target's original scope description exactly. **[CONFIRMED — empirical, string presence; HIGH CONFIDENCE — inferred, as to the breakdown by subsystem, since these are debug/category label strings rather than directly-traced subsystem boundaries.]**

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

## 3. `vint_callback_lua` — the C-function-exposed-to-Lua object type

A real C++ class, located via its MSVC RTTI type descriptor (the standard type-descriptor → Complete Object Locator → vtable chain, the same technique already used successfully in `spec-vpp-container.md` §3.5). **[CONFIRMED — disassembly, RTTI chain fully resolved.]**

- **Small vtable (6 function-pointer slots).** Consistent with a lightweight interface: a destructor plus a handful of virtual methods (plausibly something like "invoke," "get name," "get argument count" — not individually identified this pass).
- **Instances live in a fixed-capacity object pool: 95 pre-allocated slots, 104 bytes each**, all pre-stamped with the same vtable pointer by a static-initialization loop at process startup (confirmed via direct disassembly of that loop — a straightforward "stamp vtable, advance by stride, repeat N times" pattern). **[CONFIRMED — disassembly.]** **[OPEN — desk review 2026-09-30: §13.2 says the UI bring-up routine `0x008489e0` stamps this vtable, while §16.1's step list for that routine names no stamping step; the writers of vtable `0x01255920` were not listed; to be settled against the executable.]**
- **The pool is managed by a generic intrusive doubly-linked free-list allocator** — an engine-wide idiom, not specific to Lua: the exact same free-list push/pop/insert pattern (down to the field layout) was independently found managing a *different*, differently-sized object pool elsewhere in the engine (a UI-element pool). **[CONFIRMED — disassembly, cross-checked against a second independent pool.]** This means the presence of this allocator pattern is not, by itself, Lua-specific evidence — it's a general resource-pooling convention this engine uses broadly.

**What was not resolved this pass:** the exact byte layout of a bound-callback's 104-byte object (where within it the bound name string and the target C function pointer actually live), and — more importantly — **the specific call site(s) where a real, named C function actually gets registered into this pool with its Lua-visible name.** The pool's "claim a slot" function was traced and found to move objects between two lists based on a virtual-method match check rather than doing straightforward name/function field assignment, and its small number of callers (7, each called once **[→ 9 call sites: 7 functions plus 2 un-boundaried sites, §7.4]**) look like generic "claim a pending resource" call sites spread across unrelated engine subsystems rather than one central Lua-registration routine — consistent with this being shared pool-allocation infrastructure rather than something that, by itself, reveals binding names. **[OPEN / UNKNOWN — the class and its pool are real and confirmed; the actual roster of C functions exposed to Lua through it was not extracted.]** A dynamic-analysis pass (breakpointing actual registration at runtime) would likely resolve this far faster than further static tracing, given the lesson already learned elsewhere in this project about diminishing returns on purely static approaches to a well-hidden mechanism.

**⚠ CORRECTION / MAJOR UPDATE, this pass — see §7.** The framing above ("the actual roster of C functions registered *into* Lua") is refuted as a description of what this class actually does. Disassembly of every confirmed caller of the pool's claim function shows they are themselves Lua-*callable* entry points (implementing something shaped like `object:insert(...)`/`:remove(...)`/`:update(...)`, called FROM a running Lua script) that let the *currently loaded* script register its *own* callback against a target object and an event name. The pool holds native-side handles to Lua-supplied callbacks — it is not a compiled-in roster of C functions exposed for Lua to call. See §7 for the full, disassembly-confirmed call chain and for why this also answers the dynamic-vs-static question raised in the paragraph above: there is no fixed roster for *any* pass, static or dynamic, to recover — only a specific run's transient registrations, which turned out to be a different and narrower question than the one this section originally posed.

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

## 4. Named Lua/UI event hooks — the best-confirmed part of this surface

This is the opposite direction from §3: **engine code checking whether the currently-loaded Lua/UI script defines a function with a specific, well-known name, and calling it if so** — an optional-hook pattern, not a required registration. This was directly confirmed by decompiling one of the dispatcher functions found via the automated scan: it looks up a Lua global variable by the given name (via an internal helper whose own literal argument name — `"_GetAnyGlobalSilent"` — is itself Volition's own debug/API label, quoted here as data), then checks the result's Lua type against the value `6` — which is exactly Lua 5.1's own public `LUA_TFUNCTION` type constant — before treating it as callable. **[CONFIRMED — disassembly, exact match against Lua 5.1's own public type-constant numbering.]**

The automated scan surfaced **several dozen distinct dispatcher call targets, collectively covering several hundred individual call sites** **[OPEN — desk review 2026-09-30: §8.1 found one dispatcher (`FUN_00e0cef0`) with 65 direct sites; this figure is not reconciled with that census (it may have counted other functions such as `FUN_00e0ca80`, or loose matches); to be settled against the executable.]**, each passing a literal, human-readable, snake_case name string. A representative sample of real, extracted hook names, grouped by evident subsystem (grouping is this document's own inference from the names themselves, not a traced boundary):

| Subsystem | Example hook names |
|---|---|
| **Mission/activity completion screens** | `cmp_mission_success`, `cmp_activity_success`, `cmp_fail_populate`, `cmp_rewards_success`, `cmp_stronghold_success`, `credits_grab_credits`, `horde_results_populate`, `cat_mouse_results_populate`, `coop_diversion_success_responder` |
| **Main menu / pause menu / options** | `main_menu_internet_disconnected`, `main_menu_ethernet_disconnected`, `Main_menu_gameboot_complete`, `pause_map_interface_event`, `pause_map_stag_completion`, `options_display_populate_display_modes`, `options_display_populate_msaa`, `vint_remap_update_display`, `vint_remap_get_action_binding` |
| **HUD elements** | `hud_running_man_event_update`, `hud_running_man_load_complete`, `hud_touch_combo`, `hud_qte`, `hud_diversion`, `hud_zombie`, `hud_btnmash`, `hud_mayhem_world_cash_update`, `object_indicator_update`, `object_indicator_remove` |
| **Garage / vehicles** | `garage_vehicle_load_begin`, `garage_vehicle_load_completed`, `garage_populate`, `garage_performance_stats` |
| **Vehicle customization** | `vcust_populate_menu`, `vcust_populate_palette_menu`, `vcust_populate_underglow_color`, `vcust_populate_color_grid`, `vcust_populate_wheel_menu`, `vcust_populate_wheel_grid_menu` |
| **Tutorials** | `tutorial_advance`, `tutorial_responder` |
| **Minigames** | `button_mashing_minigame`, `sr2_balance_meter`, `mayhem_local_player_world_cash`, `whored_countdown_timer_update` |
| **Screen transitions / misc UI** | `screen_fade_do`, `screen_fade_auto_save_show`, `screen_fade_auto_save_hide`, `countdown_display`, `countdown_unpause`, `newsticker_populate`, `store_gallery_init_complete`, `map_filter`, `map_district_names` |

**[CONFIRMED — empirical: every name above was read directly as a literal string argument at a real call site to a confirmed name→Lua-global-lookup dispatcher.]** **[⚠ Partly refuted, §14.3: 5 names in this table — `cmp_mission_success`, `cmp_fail_populate`, `cmp_activity_success`, `garage_populate`, `garage_performance_stats` — are NOT Lua-hook dispatcher calls.]** **[OPEN — desk review 2026-09-30: the CONFIRMED label above covers only the names re-confirmed by the §8 method — the 5 shared with §8.2 (`screen_fade_do`, `garage_vehicle_load_begin`, `garage_vehicle_load_completed`, `pause_map_interface_event`, `pause_map_stag_completion`) plus `hud_running_man_event_update` (§14.4). §14.3 refuted 5 of the 7 names it checked (all 5 were §4 names); the remaining §4 names rest on the original automated scan and should be re-checked against every literal pushed at the dispatcher's call sites; to be settled against the executable.]** The full extracted set is much larger (hundreds of names across dozens of dispatcher call targets); the table above is a representative sample chosen for subsystem coverage, not the complete roster — a full list was not transcribed into this document since it would be closer to a data dump than a specification, but the extraction method (§ above) is fully reusable to regenerate the complete set.

**What determines which hooks exist for a given screen/mission** is presumably which `.lua` script file is currently loaded for that context (§2) — a script only needs to define the functions relevant to the situation it handles; the engine's own lookup is explicitly "silent" (the `_GetAnyGlobalSilent` name) about a hook not being present, confirming this is a genuinely optional/best-effort convention, not a required contract.

**Review status (2026-09-30): NEEDS-EXE: hook-name census and dispatcher counts not reconciled with §8.1; most §4 names not re-confirmed by the §8 method — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

## 5. UI document element-type registry (the "object binding" surface)

A second, structurally distinct mechanism, matching the "VDO" (Vint Document Object) naming convention from §2: a registration function taking a type tag and **two function pointers** (very likely a constructor and a destructor, or a constructor and an update/render callback — not distinguished further this pass) was found called for exactly this set of real, extracted UI element-type names: `element`, `tween`, `animation`, `group`, `clip`, `bitmap`, `text`, `point`, `video`, `sr2_map`, `bitmap_circle`, `document`, `gradient`. **[CONFIRMED — disassembly for the registration function's own logic (validates its inputs, stores the type tag and both function pointers into a small record, then calls a further "insert into registry" step); HIGH CONFIDENCE — inferred for the exact association between each literal name above and its specific registration call, since the automated scan located the name near the call rather than proving it is the literal argument at a specific parameter position.]**

**⚠ CORRECTION / MAJOR UPDATE, this pass — see §9.** The parameter-layout guess above is refuted at the specific-argument level, using all 13 real call sites plus the callee's own logic (not proximity). The type name occupies a confirmed, fixed argument position (3rd of 4 explicit arguments, CONFIRMED on 13 of 13 sites by reading the exact string at the pushed address). The two pointer-shaped arguments are **not** a constructor/destructor pair at all — one is a pointer to that type's own table of *named properties*, the other is that table's entry count. The get/set function-pointer pair the original automated scan detected is real, but it lives one level deeper than this guess placed it: once per named *property*, not once per element *type*. See §9 for the complete, disassembly-confirmed layout.

This is very likely the mechanism that lets a Vint UI-layout description (authored data, possibly XML- or Lua-table-driven — not identified this pass) reference element types like `<bitmap>`, `<text>`, `<animation>`, etc. by name and have the engine construct the right C++ object for each — i.e., the "object binding" component of this target's scope, as distinct from the "mission control"/"UI hooks" components covered in §4.

**Review status (2026-09-30): DESK-PASS — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

## 6. Open Items

1. **The actual roster of C functions registered *into* Lua via `vint_callback_lua`** (§3) — the class, its pool, and the pool's generic allocator are confirmed; the specific bound names and function pointers were not extracted. This is the single biggest gap relative to a complete picture of "the custom C-side API surface," and — per this project's own established lesson about diminishing returns from purely static tracing of a well-hidden mechanism — would likely be resolved faster with a dynamic pass (breakpoint the pool's claim/insert function and observe real registrations happen at startup) than with more static analysis. **Real, partial evidence toward this, 2026-09-29 (`tools/vint_functions_roster.txt`, a full names-only cross-check of every `vint_*` identifier across the real `interface.vpp_pc` Lua corpus against §13.7's 55-name registrar and the wider 1,490-name census): 4 names — `vint_clear_tween_event_reference`, `vint_reset_child_tween_object`, `vint_set_child_tween_reverse`, `vint_set_tween_event_reference` — are called throughout `vdo_anim_object.lua` (each with a `(self.handle, ..., self.doc_handle)`-shaped call) but defined nowhere in the extracted Lua corpus and absent from the 1,490-name census. These are plausible candidates for this exact open item — real bound `vint_callback_lua` names, not yet confirmed by disassembly. **[⚠ Note: §7.3 establishes that `vint_callback_lua` holds Lua-supplied callbacks with no fixed roster of bound C names; compare the `lua_register`-style registrars of §13.5–§13.7.]** Not chased further this pass; a concrete, narrow next step for whoever picks this up (trace these 4 specific name strings' own registration call sites, rather than the pool mechanism in general).** **[Status: see §11 / §12.8.]**
2. **The complete list of named Lua/UI event hooks** (§4) — a representative sample is given; the full set (likely several hundred entries) was not transcribed, though the extraction method is fully reusable. **[Status: see §11 / §12.8.]**
3. **The exact parameter layout of the element-type registration function** (§5) — confirmed to take a type tag and two function pointers and to be called once per named UI element type, but which specific argument position the type name occupies, and what the two function pointers actually do (constructor/destructor? constructor/updater?), were not pinned down. **[Status: see §11 / §12.8.]**
4. **Whether `vint_callback_lua` (§3) and the named-hook dispatchers (§4) are actually the same underlying mechanism viewed from two ends, or two genuinely separate systems** — both clearly serve the C++/Lua boundary and both live in similarly-named/-themed code regions, but no direct code path was traced connecting them. Treat them as two confirmed-real, but not confirmed-related, mechanisms. **[Status: see §11 / §12.8.]**
5. **Mission-script-specific bindings** (as opposed to the UI-hook surface documented in §4, which leans heavily toward menus/HUD/minigames) were not separately isolated — the `"Mission LUA script"`/`"mission lua temp"` strings confirm mission logic does use this same general Lua infrastructure, but no mission-specific dispatcher was individually traced and distinguished from the general UI-hook family. **[Status: see §11 / §12.8.]**

None of these gaps prevent using this document to understand the shape of the binding surface: an implementation aiming for Lua-script compatibility would need to (a) support looking up and calling optional, well-known-named Lua global functions at the documented trigger points (§4 gives real examples to reference), and (b) support Lua scripts referencing UI element types by the names in §5 — while (c), exposing genuinely new C functions to Lua scripts in the original engine's own style (§3), remains an open reverse-engineering task rather than a solved one.

**Review status (2026-09-30): DESK-PASS — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

## 7. The `vint_callback_lua` mechanism, resolved — closes Open Item 1

### 7.1 Method

Static analysis only, this pass — the EnvDTE/Visual-Studio dynamic-debugging apparatus documented
elsewhere in this project (see `HANDOFF.md`, search "EnvDTE") was **not invoked**. The reason is stated
plainly rather than left implicit: the static trace below changed what question a dynamic pass would even
be answering (§7.4), so it was not run this pass. This is offered as the honest, load-bearing result the
task asked for — "a well-characterised 'static analysis stalls here' is a real result" — except the
outcome here is the opposite of a stall: static tracing of the pool's own confirmed consumers (§3) was
pushed one level further than the previous pass and it resolved the shape of the mechanism.

The method was to read, in full, the seven confirmed static callers of the pool's claim function
(`FUN_00e19770`), plus every function they call in turn to link, unlink, or invoke a claimed object. This is
disassembly, not automated pattern-scanning — every function named below was decompiled and read.

**Review status (2026-09-30): DESK-PASS — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 7.2 The confirmed call chain

**[CONFIRMED — disassembly, all seven callers read in full.]** Two of the seven callers
(`FUN_00e1a5e0`, `FUN_00e1a7a0`) read a Lua string argument off the interpreter stack (via the same internal
Lua-C-API wrapper functions used throughout this binary) and compare it, case-insensitively, against the
literal strings `"update"`, `"remove"`, and `"insert"` — selecting one of three numeric mode values (3, 5,
4 respectively) based on which literal matched. The remaining five callers (`FUN_00e1acd0`, `FUN_00e1bc80`,
`FUN_00e1bee0`, `FUN_00e1c050`, `FUN_00e1c410`) are fixed-purpose variants: each hardcodes one specific mode
value and skips the string comparison, calling the same underlying link/unlink primitives described below
with a constant argument instead of a runtime-selected one. All seven read further Lua arguments (a target
object reference and, in most cases, a callback value) via the same family of internal Lua-C-API wrapper
functions — i.e. **all seven are themselves C functions callable FROM Lua**, not callers reaching out to a
fixed C-side roster.

The shared sequence, present in all seven: **[OPEN — desk review 2026-09-30: §10.1 says the three getter-shaped callers `FUN_00e1bc80`/`FUN_00e1bee0`/`FUN_00e1c050` call `FUN_00e2b7e0` (a third pool) instead of the step-4 link helpers, and the paragraph below says `FUN_00e1c410` has a different shape, so step 4 is not shared by all seven; also, mode 5 ("remove") is listed as accepted by the inserting helper `FUN_00e24c40` while removal is `FUN_00e24d70`, and §10.1 credits `FUN_00e24c40` with a mode-4 immediate-fire branch that step 4 gives only to `FUN_00e25540`; to be settled against the executable.]**

1. A name/key value taken from the Lua arguments is hashed by `FUN_00d9e740` — a byte-at-a-time,
   lowercasing, table-driven rolling hash (256-entry table at `DAT_01320da0`). **[CONFIRMED — disassembly.]**
   This is a *third* address distinct from the two engine-wide string hashes already catalogued elsewhere in
   this project (`FUN_00da7890`, the rotate-6/XOR hash; `FUN_00d9e8b0`, the table-driven CRC-32) —
   whether it is a genuine third hash routine or an alias/inlined copy of one of those two was **not**
   reconciled this pass. ~~**[OPEN / UNKNOWN.]**~~ **[Resolved: not a third hash. `0x00d9e740(str, seed)` is the lower-casing two-argument entry point of the engine's reflected CRC-32 (the same table `0x01320DA0`, sibling of `0x00d9e8b0`), per `spec-tables-environment.md` §1.4 and `spec-tables-progression.md` §1.3.]**
2. `FUN_00e2a6d0` looks the hash up in a 64-bucket hash table (`DAT_02a74ee8`), walking a linked list of
   already-claimed objects and comparing each one's own stored key (at object offset `+0x14`) against the
   hash. If found, a refcount at the found object's `+0x1c` is incremented in place and no new pool object is
   claimed.
3. If not found, `FUN_00e19770` (the pool's claim function, §3) is called, passing the *same*
   pre-hash value used in step 1 (a Lua-supplied string/value, not the hash itself) as its own argument. Its
   internal "does this match" virtual-method call (vtable slot 2, offset `+8`) is therefore given that value
   directly. Because all 95 pool objects share one identical vtable (§3), this virtual call cannot be a
   per-instance name comparison baked into the vtable — it is necessarily a generic gate (e.g. an
   allocability check), and this pass did **not** pin down its exact predicate beyond that it is invoked once,
   only on the free list's current head, with no further list walk if it returns false. **[HIGH CONFIDENCE
   — inferred, that it is a generic gate rather than a per-name comparison; OPEN / UNKNOWN, its exact
   condition.]**
4. The claimed (or found) object is linked into — or unlinked from, or immediately dispatched via — a
   doubly-linked list rooted at a field on the **target object**, not on the vint_callback_lua object itself:
   `FUN_00e24c40` (mode gate: accepts 3, 4, or 5, i.e. exactly the update/insert/remove values above) inserts
   a new node into the list at the target's `+0x18`; `FUN_00e24d70` finds and unlinks a matching node by a
   3-field comparison; `FUN_00e25540` is a third, fixed-mode (2) variant that inserts at a *different* target
   field (`+0x324`) and, unlike the other two, immediately invokes the newly-linked object's own virtual
   method before returning — i.e. an "insert and fire immediately" variant rather than "insert for later
   dispatch". Each of these three operates on nodes drawn from a **second, separate** 32-byte object pool
   (free-list head `DAT_02a5afa0`) — distinct from the 95×104-byte vint_callback_lua pool itself.
   **[CONFIRMED — disassembly.]**
5. Release goes through `FUN_00e2a600`, which decrements the vint_callback_lua object's own refcount
   (`+0x1c`) and, only when it reaches zero, invokes the object's own vtable slot 5 (`FUN_00e2a620`). That
   function is now **[CONFIRMED — disassembly]** to be the class's destructor: it unlinks the object from
   *two* separate 64-bucket hash tables (`DAT_02a74ee8`, keyed by the object's own `+0x14`, the lookup table
   from step 2; and a second one, `DAT_02a74de8`, keyed by the object's `+0x18` — a second index whose key
   meaning was not traced further this pass, **[OPEN / UNKNOWN]**), then chains to two further virtual
   methods at vtable slots 3 and 4 (offsets `+0xc`, `+0x10`).

One of the seven callers, `FUN_00e1c410`, is a different shape: a large switch statement over a property-kind
selector that implements several typed property get/set paths (floats, positions, table references); one
case (selector value 8) also claims a vint_callback_lua slot unconditionally. This is consistent with
"assigning a Lua function to a callback-typed named property" being one case among a general property
get/set dispatcher — which ties directly into §9's property-table finding for element-type
registration. **[HIGH CONFIDENCE — inferred.]**

**Review status (2026-09-30): NEEDS-EXE: which callers share step 4, and the per-mode behaviour of the link helpers — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 7.3 What this means for "the roster" — and for the dynamic-pass question

**[CONFIRMED — disassembly, for the mechanism; HIGH CONFIDENCE — inferred, for the interpretive
conclusion.]** `vint_callback_lua` is not "a roster of C functions exposed to Lua" in the sense the original
document searched for — read literally, the class's own name says the opposite of what was assumed: a
"callback into Lua" object, not a callback exposed *to* Lua callers. It is the object type used to hold a
**native-side handle to a Lua-supplied callback** **[→ §10.1: the "callback" is a name string at `+0x28`, invoked as a Lua global, not a stored closure]**, created on demand whenever the currently-running Lua/UI
script calls one of the confirmed insert/remove/update entry points (§7.2) to register (or unregister) a
callback against a specific target object and event name.

**There is no fixed, compile-time "roster of 95 named C functions" for any method to recover, because the
name and the callback are both runtime data supplied by whichever Lua script happens to be loaded** —
which is precisely why a purely static search for "the roster" never found one in the previous pass: it is
not static data to find. This is why no EnvDTE dynamic pass was run this time: a dynamic pass would only ever
enumerate one specific run's transient registrations (which names got registered, by which script, against
which targets, during that particular play session) — a real, answerable, but much narrower question than
"the roster", and one that does not change the mechanism finding above. If a future pass wants that narrower
answer, breakpointing `FUN_00e19770` (the claim function) and logging its argument plus the calling script's
identity across a played session is the concrete next step, exactly as the original document proposed —
just re-scoped to "log one run's registrations" rather than "recover a fixed roster".

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 7.4 Residual open thread

~~Two of the claim function's nine total call sites (`0x00e1d76a`, `0x00e1ce5e`) did not resolve to a
containing function even on a second attempt (Ghidra's function-boundary detection has no `Function` object
covering either address in the analysis state used this pass). **[OPEN / UNKNOWN.]** Concrete next step: force
function creation at those two addresses (or disassemble a fixed byte window backward from each, as done
successfully elsewhere this pass for un-boundaried code — §9.3) and read the containing logic; given the
uniform shape found across all seven resolved callers, these two are likely further thin Lua-callable
wrappers of the same kind, but this was not verified.~~

**⚠ RESOLVED, this pass — and the prediction above turns out wrong.** `createFunction` succeeded at both
addresses, but the decompiles it produced are ill-formed (registers `EDI` and `EBP`, and a stack slot at
offset `0x38`, are all reported by the decompiler as carrying incoming values it could not trace back to this
function's own entry, with similar unresolved values throughout) — the standard Ghidra tell that the real
incoming register/stack state comes from code *before* the forced start, i.e. neither address is a genuine,
independent function entry.
Raw disassembly at both sites (not the unreliable forced decompile) settles what they actually are:
**[CONFIRMED — disassembly.]** `0x00e1ce5e` and `0x00e1d76a` are each the address of the `CALL 0x00e19770`
instruction itself, reached by straight-line fallthrough, immediately preceded at *both* sites by the
identical 3-step idiom already confirmed for the other seven callers — `CALL 0x00d9e740` (hash) → `CALL
0x00e2a6d0` (bucket lookup) → on a hit, `INC dword ptr [reg+0x1c]` (the refcount); on a miss, `CALL
0x00e19770` (claim) — byte-for-byte the same shape at both addresses, and the same shape the decompiled
forced-function bodies show once picked up mid-stream.

What they are *not* is two more instances of the six-caller **uniform thin-wrapper shape**. Both call sites
sit deep inside a much larger body: the control flow reachable forward from `0x00e1ce5e` (recovered by the
decompiler despite the bad entry point) is a `switch` over a per-index property-descriptor array — cases
`1`/`2`/`3`/`4`/`5`/`6`/`7`/`8`/`10`/`0xb`/`0xd` — with `case 8` performing the hash/lookup/claim sequence;
`0x00e1d76a` sits after the same repeating type-check-then-`FUN_00dfe040` blocks. This is structurally the
same species as the *seventh* caller, `FUN_00e1c410`, which §7.2 already flags as the one exception among
the seven ("a large switch statement over a property-kind selector... one case (selector value 8) also
claims a vint_callback_lua slot unconditionally") — not the six plain wrappers. **[CONFIRMED — disassembly,
for the immediate call site and its directly-surrounding control flow; HIGH CONFIDENCE — inferred by shape
parallel, that this is the same species of property-switch dispatcher as `FUN_00e1c410`, not a proof that it
is literally the same function.]**

Two things were deliberately left open rather than guessed at: the true entry point of the containing
function(s) — still outside any `Function` object in this pass, and `0x00e1ce5e` sits ~~roughly 0x2cc~~ 0x4cc **[arithmetic corrected: 0x00e1ce5e − 0x00e1c992 = 0x4cc; the conclusion is unchanged]** bytes
past `FUN_00e1c410`'s own confirmed end (`[0x00e1c410, 0x00e1c992)`), so it is not literally a continuation
of `FUN_00e1c410`'s own body, but a separate, still-unboundaried piece of code — and therefore whether
`0x00e1ce5e` and `0x00e1d76a` belong to the same containing function as each other. **[OPEN / UNKNOWN —
neither is needed to answer the shape question above, which this pass does answer.]**

**Net result: the residual thread closes with the opposite of its own prediction.** "These two are likely
further thin Lua-callable wrappers" is refuted, not confirmed — they are property-switch case bodies, the
`FUN_00e1c410` species, not the six-caller species.

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

## 8. Complete named Lua/UI event hook census — closes Open Item 2

### 8.1 Method — the same one, scaled up as promised

§4 stated the extraction method was "fully reusable" and gave a representative sample rather than the
full set. This pass reused it exhaustively rather than by sampling: every cross-reference to the single
confirmed dispatcher (`FUN_00e0cef0`, the `_GetAnyGlobalSilent` + `LUA_TFUNCTION`-check function) was
enumerated directly from Ghidra's reference database, then every resulting function was decompiled and read
by hand — not pattern-matched.

**[CONFIRMED — disassembly, population fully enumerated, not sampled.]** `FUN_00e0cef0` has exactly **65
direct call sites**, resolving to **46 distinct calling functions** (some functions call it from two
near-identical loop bodies, which is why the site count exceeds the function count). All 46 were
decompiled and read in full: 44 on the first pass; a 45th (`FUN_00843310`) recovered on a follow-up after an
address-recording slip pointed at a call site instead of the containing function's entry point in the first
attempt (an error caught by re-checking rather than trusted); the 46th is `FUN_00843310` itself counted once.
Fifteen further call sites elsewhere in the binary that also feed the same underlying "check + call a named
Lua global" primitive were seen during this trace but resolved to no containing `Function` object at all in
the current analysis state, and were not chased further. **[OPEN / UNKNOWN for those 15 sites' containing
functions; does not affect the 46-function census above, which is complete for direct callers of the one
confirmed dispatcher address.]** **[OPEN — desk review 2026-09-30: the text lists 44 + 1 functions and names `FUN_00843310` twice, which does not reach 46; §14.6 treats the 15 orphan sites as part of the dispatcher's own 65 direct sites, while this section places them elsewhere, and §14.6's breakdown of the 15 (10 + 5 + 2 + 1) adds up to 18; to be settled against the executable.]**

**Review status (2026-09-30): NEEDS-EXE: 65-site/46-function arithmetic and the 15-vs-18 orphan sites — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 8.2 The literal (compile-time) names — confirmed, exact strings

**[CONFIRMED — empirical: every name below was read directly as a literal string argument at a real call
site to the confirmed dispatcher, exactly as §4's own confirmation standard requires.]** This supersedes
§4's 36-name **[⚠ recounted: 53, §12.2]** representative sample with a much larger, still-partial set (see §8.4–§8.7 for why
"complete" is still not claimed):

| Subsystem (inferred grouping) | Hook names found this pass |
|---|---|
| Screen fade | `screen_fade_do` (both the fade-in and fade-out call sites use this same name) |
| HUD / inventory | `hud_inventory_hide`, `hud_inventory_show`, `hud_hit_clear_all`, `hud_msg_hide_region` |
| Garage / vehicles | `garage_vehicle_load_begin`, `garage_vehicle_load_completed` (two independent call sites), `store_vehicle_switch_mode`, `store_vehicle_signal_swap_complete`, `store_vehicle_exit_begin_bg_clear`, `clones_m3_pow` |
| Store / controls | `store_lock_controls`, `store_unlock_controls`, `store_weapon_cover_weapon` |
| Pause map | `pause_map_stag_completion`, `pause_map_interface_event`, `pause_map_is_taxi_mode` |
| Completion / mission-complete screens | `cmp_common_screen_start`, `completion_coop_disconnected` |
| Cell phone / minigame | `cell_missions_button_b` |
| City streaming | `city_load_hide_images` |
| Misc | `gis_grain_fade_out`, `vint_lib_init_constants` |
| **Mission-specific** (see §10.2) | `m24_killbane_on_death`, `m24_killbane_on_undowned` |

That is 25 distinct literal names confirmed this pass (several via more than one call site), on top of the
36 already published in §4 — 61 distinct confirmed literal names total **[⚠ recounted: 53 in §4 + 20 genuinely new here = 73, §12.2]** across both passes, still described
as a large-but-partial set rather than exhaustive, for the reasons in §8.3–§8.7. **[→ §14.3 removed 5 §4 names (not hooks); §14.6 added 5 new literal hooks. `hud_running_man_event_update`, confirmed at 7 dispatcher sites by §14.4, is not in the table above, so the table is neither the full literal set of the 46 functions nor only the new names.]**

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 8.3 The full mechanism, now traced end-to-end

**[CONFIRMED — disassembly.]** Every one of the 46 functions above follows the identical five-step
sequence: (1) `FUN_00e1a1b0()` fetches the current Lua state pointer from a global; (2) `FUN_00e0cef0(name,
state)` performs the confirmed exists-and-is-function check (§4); (3) if true, `FUN_00e1a1b0()` is called
again and `FUN_00e0ca80(name, state, ...)` prepares an actual call context, returning a non-null pointer on
success; (4) zero or more calls to a small family of typed argument-push helpers (`FUN_00e0ce00`
byte/bool, `FUN_00e0ce20` float, `FUN_00e0ce40` double, `FUN_00e0cfb0` string) load the call's arguments; (5)
`FUN_00e0cd00(&callContext)` performs the actual call into the Lua global. `FUN_00e0ca80` itself is confirmed
to be a thin forwarder to `FUN_00e0c720`. This is offered as a corrected, complete description of the
call-in mechanism that §4 described only at the "checks then treats it as callable" level. **[OPEN — desk review 2026-09-30: `spec-lua-api-behaviour.md` §26.23 describes `0x00e1a1b0` as an accessor returning one fixed global "value-list builder" singleton, while §14.1 says it returns the UI Lua state `DAT_02a45450`; no spec says whether these are the same object. §26.23 also lists a nil-shaped push, `0x00e0ce60`, which is missing from the helper list above; to be settled against the executable.]**

**Review status (2026-09-30): NEEDS-EXE: what `0x00e1a1b0` returns; missing `0x00e0ce60` push — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 8.4 Templated (sprintf-built) hook names — a second, distinct family

**[CONFIRMED — disassembly.]** Several of the 46 functions do not pass a compile-time literal at all: they
first `sprintf`-format a per-object name into a stack buffer, using a fixed format string and a per-instance
name field (typically at a fixed offset on a UI-widget object), then pass *that* buffer to the same
`FUN_00e0cef0`/`FUN_00e0ca80` sequence. The exact, confirmed format strings found this pass:

`"%s_reset"`, `"%s_exited"`, `"%s_gained_focus"`, `"%s_lost_focus"`, `"%s_cleanup"` (two separate call
contexts), `"%s_init"`, `"%s_play_exit_anim"`. **[OPEN — desk review 2026-09-30: §16.2 has `FUN_00a1fa90` firing `"<name>_init"` and `"<name>_main"` (with `<name>` a mission/level stem) through the dispatcher; `"%s_main"` is not in this list, so either this family is not UI-widget-only, or those names are built another way, or `FUN_00a1fa90` sits outside §8.1's 46; to be settled against the executable.]**

**Cross-team confirmation from the clean side, 2026-09-29:** a real-script census (Team B, all 804 real
scripts) found 699/804 define a top-level `<own-stem>_init` function and 673/804 define
`<own-stem>_cleanup` — closing the loop on this disassembly-side finding from the data side. The same
census also found 63/804 define `<own-stem>_start`, 51/804 `<own-stem>_run`, 50/804 `<own-stem>_success`,
and 144 (pooled) an `initialize`/`initialize_checkpoint`/`initialize_common` variant — real evidence a
mission-entry naming convention exists, though not universal, and NOT reached via this section's own
confirmed sprintf mechanism (§14.6 found no mission-name-suffixed sprintf sibling exists). Most likely
explanation, not yet confirmed: either xtbl-authored data names these functions directly, or the engine
(or the script itself) assembles the call target via `strcpy`/`strcat`-style concatenation rather than
`sprintf` — a mechanism the sprintf-caller sweep in §14.6 would not have caught, since it only followed
`sprintf`-equivalent callers. **OPEN — flagged for a follow-up xref search on the literal suffix strings
`"_start"`/`"_run"`/`"_success"` themselves (without a leading `%s`), feeding `lua_getglobal`/
`lua_getfield(LUA_GLOBALSINDEX, ...)` directly; not yet attempted.** **[→ DONE §14.7 (exhaustive negative); see also §14.8–§14.10, §16.2.]**

This means the *true* population of nameable hooks is larger than any static literal-string count can show:
every UI widget instance that has its own name effectively gets its own family of these suffixed hooks at
runtime. **[HIGH CONFIDENCE — inferred, that this generalizes to "every named widget instance"; not
verified against a live widget list this pass.]**

**Review status (2026-09-30): NEEDS-EXE: whether §16.2's `_init`/`_main` firing breaks the UI-widget-only reading — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 8.5 Data-driven hook tables — present, but not statically readable

**[CONFIRMED — disassembly, for the tables' existence and consumers; OPEN / UNKNOWN, for their contents.]**
Two **[⚠ count: three functions are listed below, reading two tables]** of the 46 functions read their hook name out of an **array of string pointers** rather than a literal or
a format string:

- `FUN_00a1f990` / `FUN_00a1fe10` index a table at `DAT_026e83f0` (stride 12 bytes, i.e. 3 dwords per
  entry, name pointer at the first dword of the entry selected by a packed 16:16 index field — the same
  packed-word idiom this project has flagged elsewhere as a recurring hazard, correctly masked here by the
  reading code itself with `>> 0x10` and an overflow-style guard).
- `FUN_00845210` walks a table at `DAT_02319570` (stride 0x68 bytes, name pointer at each entry's `+0x28`,
  bounded to roughly 4 entries by the loop's own upper address check) — ~~sitting immediately next to the
  `cell_missions_button_b` hook and matching that same cell-phone-mission-completion theme.~~ **[Superseded: `spec-lua-api-behaviour.md` §8.24 identifies `DAT_02319570` as the request-slot table of `game_peg_load_with_cb` (`0x00845510`) and `0x00845210` as its completion sweep; Lua argument 1 fills the slot's name field, so the names here are Lua-supplied callback names filled at runtime, not a per-mission cell-phone trigger table. The address proximity to `cell_missions_button_b` is coincidence.]**

**Both tables read as entirely null in the static executable image.** Exact figures, predicate stated: of
**80** sampled entries of `DAT_026e83f0` (indices 0–79, every entry a name-pointer field checked for
non-zero), **0 of 80** are non-null; of the **4** entries `DAT_02319570`'s own bound permits, **0 of 4** carry
a non-null name pointer. A failing case for this predicate would have been any entry reading a valid
in-image address that decodes to printable text — none did, so this is a real, checked negative, not an
unexamined zero. This matches a pattern this project has already documented for other runtime-populated
arrays (customization slider state, HANDOFF §5): **the data is populated at runtime (per mission/session
state), not present in the shipped static image**, so no static pass — including this one — can recover
these names. **Concrete next step, if pursued:** a dynamic snapshot (breakpoint on `FUN_00845210`, or a
memory watch on either DAT_ address, during actual mission play) is the only route to these names; not
attempted this pass.

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 8.6 A generic, parametrized helper — a further, unbounded source not chased

`FUN_00843310` (§8.1) is itself a thin wrapper identical in shape to §8.3's five-step sequence, except
its hook name is a **plain incoming parameter** rather than a literal or a per-instance buffer. ~~Its own
callers were not enumerated this pass. **[OPEN / UNKNOWN — a concrete, bounded next step:** enumerate
`FUN_00843310`'s callers exactly as §8.1 enumerated `FUN_00e0cef0`'s; any literal or format string found at
those call sites is a further hook name this census does not yet include.]~~

**⚠ RESOLVED, this pass — see §8.7.** `FUN_00843310`'s callers were enumerated exactly as this section's
own next step proposed. The population is small — 3 call sites, 2 distinct calling functions — and the
result is a confirmed **negative** for new literal hook names: neither call site passes a compile-time
literal, and a follow-up trace found no reachable writer, anywhere in the binary, of either underlying
per-instance name field the two callers actually do pass. See §8.7 for the full account, including why this
is a checked negative rather than an unexamined absence.

**Review status (2026-09-30): DESK-PASS — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 8.7 `FUN_00843310`'s callers, enumerated — §8.6's next step executed, closed as a checked negative

**Method — identical to §8.1's, applied to the new target.** Every cross-reference to `FUN_00843310` was
enumerated directly from Ghidra's reference database (the same mechanism §8.1 used against `FUN_00e0cef0`),
then every resulting calling function was decompiled and read by hand — not pattern-matched. Stated before
searching, per house rule: a **positive** finding here would be a literal or format-string constant sitting
directly at one of these call sites (a new confirmed hook name, exactly like §8.2's table); a **negative**
finding is a call site whose name argument is itself a parameter or variable with no static literal
reachable anywhere further up the call chain — a real limit of static analysis, not a search failure, and
distinguished below from an *unexamined* absence by tracing that chain as far as it statically goes.

**[CONFIRMED — disassembly, population fully enumerated, not sampled.]** `FUN_00843310` has exactly **3
direct call sites**, resolving to **2 distinct calling functions** — the same shape §8.1 saw with its own
dispatcher (one function accounting for more sites than functions, here via two near-identical guarded
blocks inside one function rather than a loop). Both functions were decompiled and read in full.

**Both callers sit in the binary's Bink-video (cutscene/FMV movie) playback subsystem**
(`0x00bdc000`–`0x00bde000`), not in any UI/menu code seen elsewhere in this document. This is Volition's own
glue code around the third-party Bink codec (already out of cleanroom scope per `spec-output.md` §1.3) — the
glue itself, which decides *when* and *by what name* to fire a Lua hook during movie playback, is in scope
and is what follows:

- **2 of the 3 call sites** are in the movie subsystem's per-frame tick function. They fire a **movie
  marker-frame** hook: the current Bink frame number is compared against a per-movie target-frame field, and
  on a match the hook named by a 32-byte buffer at a fixed offset in that movie's own playback-slot record is
  invoked — but only if that buffer is non-empty (an explicit "is the string non-empty" guard precedes the
  call).
- **The remaining call site** is in the movie subsystem's completion/cleanup function. It fires a
  **movie-complete** hook, named by a *second*, distinct 32-byte buffer elsewhere in the same slot record,
  guarded the same way.
- Neither buffer is a compile-time literal, a per-instance `sprintf`-built name (§8.4's family), or an entry
  read from one of §8.5's two data tables — it is a **third, distinct pattern**: a raw, unformatted,
  per-instance string field, addressed directly, with no templating step at all.

**Followed one level further, per this document's own instruction, then followed further still because it
was cheap and the chain kept extending cleanly:**

1. The movie-slot constructor explicitly **zeroes both name buffers to an empty string** (and sets the
   marker target-frame field to `-1`, a value no real Bink frame count will ever match) at slot allocation.
2. The "start playing a movie" entry point — reached from two Lua/game-facing trampolines — copies only the
   movie's filename and a couple of flag bytes from its caller's parameter block into the new slot. **It
   never touches either name buffer.**
3. Every function in the **entire binary** that references the movie-slot table's own base address (not a
   sample — the full Ghidra cross-reference population for that global, 11 functions total) was decompiled
   and read. **None of them writes anything but the constructor's own zero to either buffer.** Since
   resolving the packed slot-handle this subsystem hands back to calling code requires exactly that base
   address, this rules out an unfound setter reached through the handle, not just an unfound setter among the
   functions already read.

**This is the negative case stated before searching, confirmed rather than assumed:** no literal, and —
going further than a literal search requires — no string of any kind, reaches either name field anywhere in
this program's statically-reachable call graph. The mechanism is real (a working, non-vacuous guard exists
at both consuming call sites, and a dedicated sentinel value guards the marker-frame check), but in the
shipped executable it is never wired to a value. Whether some other, currently un-carved routine (this
project's own recurring finding elsewhere) or a runtime/scripted path outside static reach populates it is
left **OPEN / UNKNOWN** — explicitly not claimed closed by this static pass.

**Population figure, in §8.1's own terms:** 3 call sites / 2 distinct calling functions / **0 new confirmed
literal names** (§8.2's 61-name total is unchanged **[⚠ recounted as 73, §12.2]**) / **2 newly-identified, structurally-confirmed
hook-name slots with no static writer** — a checked negative added to the record, not a gap left
unexamined. This closes §8.6's named next step in full: `FUN_00843310`'s callers are now enumerated exactly
as `FUN_00e0cef0`'s were, and the answer this census gains is that no further hook name is recoverable from
this particular helper by static means.

**Review status (2026-09-30): DESK-PASS — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

## 9. Element-type registration — exact parameter layout, corrected — closes Open Item 3

### 9.1 The corrected argument roles

**[CONFIRMED — disassembly, all 13 real call sites read directly, plus the callee's own logic.]** The
registration function (`FUN_00e290c0`) takes an implicit target-record pointer (conventional `this`) plus
four further explicit arguments. Reading the actual pushed values at every one of the 13 call sites named in
§5 (`element`, `tween`, `animation`, `group`, `clip`, `bitmap`, `text`, `point`, `video`, `sr2_map`,
`bitmap_circle`, `document`, `gradient`) settles their roles:

- **Argument position 3 of the 4 explicit arguments holds the type name.** Read directly as a C string at
  the exact pushed address, it matches the expected literal exactly on **13 of 13** sites — not merely
  "located nearby" as §5 recorded it, but confirmed as the literal value at a specific, fixed argument
  position.
- **Argument position 4 is a pointer to that type's own table of named properties** — not a bare function
  pointer.
- **Argument position 5 is that table's entry count** — not a second function pointer.
- **Argument position 2** is stored into the target record (only when validation of arguments 3–5 passes)
  but is **not** type-specific: across all 13 sites it is drawn from just **three** distinct global storage
  cells, shared: `element`/`tween`/`animation` share one (`0x012570f0`); nine of the remaining ten
  (`group`, `clip`, `bitmap`, `text`, `point`, `video`, `sr2_map`, `bitmap_circle`, `document`) share a second
  (`0x0132bdd0`); `gradient` alone uses a third (`0x0132c868`). **[HIGH CONFIDENCE — inferred:** this
  is a shared parent-type/base-class link rather than a per-type identity — consistent with a VDO
  class-inheritance registry — but its actual consumer was not traced this pass. **OPEN / UNKNOWN,** the
  exact semantics.]

This directly answers §6 item 3's first question ("which argument position holds the type name") and
refutes its second framing ("two function pointers... constructor/destructor, or constructor/updater") as a
description of *this* function's own two pointer-shaped arguments — see ~~§9.5~~ §9.4.

**Review status (2026-09-30): DESK-PASS — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 9.2 The property-descriptor table

**[CONFIRMED — disassembly, verified in full for 2 of the 13 types.]** Immediately after filling the
target record, `FUN_00e290c0` calls `FUN_00e28bd0`, which walks exactly *entry-count* (argument 5) records of
32 bytes each, starting at the argument-4 pointer, computing a hash of each record's own leading field (via
the general engine string-hash `FUN_00d9e740`, byte-at-a-time lowercased, ~~distinct from the two hashes
`HANDOFF.md` already documents — see §7.2's identical caveat~~ **[Resolved: the engine's lower-cased CRC-32 name hash, same table as `0x00d9e8b0` — see §7.2]**) and writing that hash back into the same
record at offset `+0x18`.

Reading every record's raw bytes directly (not merely trusting the walk's arithmetic) for **`point`'s single
entry** and **all 17 of `element`'s entries** gives a consistent, fully decoded 32-byte layout:

| Offset | Field | Content, as observed |
|---|---|---|
| `+0x00` | name pointer | a property name string, e.g. `render_mode`, `visible`, `mask`, `offset`, `anchor`, `tint`, `alpha`, `depth`, `mouse_depth`, `screen_size`, `screen_nw`, `screen_se`, `rotation`, `scale`, `auto_offset`, `unscaled_size`, `background` (`element`'s full 17); `screen_size` alone for `point` |
| `+0x04` | small integer, values seen: 1, 3, 5, 6, 7, 0xd | a per-property type/size code (not decoded to specific types this pass) |
| `+0x08` | pointer into `.text` | a **setter** trampoline (§9.3) — **CORRECTED 2026-09-30, see below: originally labeled "getter," swapped with `+0x0c`** |
| `+0x0c` | pointer into `.text` | a **getter** trampoline (§9.3) — **CORRECTED 2026-09-30, see below: originally labeled "setter," swapped with `+0x08`** |
| `+0x10`, `+0x14` | always 0 in every record read | reserved / unused this pass |
| `+0x18` | 0 in the static image | the name-hash cache `FUN_00e28bd0` computes and writes at first registration |
| `+0x1c` | `0x100` or `0x000` | a flags field; `0x100` on every settable property observed, `0x000` on the four geometry-derived, apparently read-only properties (`screen_size`, `screen_nw`, `screen_se`, `unscaled_size`) |

Population for the flags observation: **13 of 17** `element` properties carry `0x100`; the **4 of 17** that
carry `0x000` are exactly the ones whose name suggests a computed/derived value (screen-space geometry), a
correlation checked by inspection of all 17, not assumed. **[HIGH CONFIDENCE — inferred** that the flag
distinguishes writable from read-only/computed properties; not confirmed against the setter trampoline's own
behavior for a read-only entry, since none of the four read-only properties' trampolines were disassembled
this pass.]

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 9.3 The get/set trampoline pattern

**[CONFIRMED — disassembly, 6 of 6 trampolines disassembled directly by linear address range rather than
by decompiling a `Function` object, since none of these addresses had a `Function` boundary defined in the
project's current analysis state — the same situation already noted for `vint_callback_lua`'s own vtable
slots in §3 [⚠ pointer: that situation is recorded in §7.4 and §10.1 (`0x00e19d50`), not §3].]** The `+0x08` and `+0x0c` pointers for `render_mode`, `visible`, and `mask` (6 addresses
total) are every one of them a tiny, near-identical thunk: guard the incoming object pointer against null,
then forward to a **fixed slot in the target object's own vtable**, passing through its two remaining
arguments unchanged. The setter and getter of the *same* property consistently target vtable slots exactly
8 bytes (2 slots) apart **[⚠ arithmetic: each listed pair, e.g. `+0x34`/`+0x30`, is 4 bytes (1 slot) apart; 8 bytes is the spacing between consecutive properties' pairs. Which slot of each pair is the setter also conflicts with §12.7 Group 7 (#150–155), which lists them the other way round — not resolved here]**: `render_mode` set/get = vtable `+0x34`/`+0x30`; `visible` = `+0x3c`/`+0x38`; `mask` =
`+0x44`/`+0x40`. This is the confirmed shape of a compiler- or macro-generated property-accessor family: each
named property occupies a fixed 2-slot `[get, set]` pair in the *concrete* VDO subclass's own vtable, and the
property table's real job is to give that pair a Lua-visible **name**, a **type/size code**, and a
**hash-indexed lookup** — not to hold construction/destruction logic for the type as a whole.

**⚠ CORRECTION, 2026-09-30 (`.vint_doc` format bootstrap + adversarial review) — the `+0x08`/`+0x0c` get/setter labels above were SWAPPED in the original write-up, now fixed at §9.2's own table too.** The `.vint_doc` file-loading code (a genuine third consumer of this property-descriptor table beyond the two trampoline-disassembly checks that established the shape above) calls the descriptor's `+0x08` function with the freshly-parsed file value as a payload argument (`(targetObject, parsedValue, 0)`) while loading a document — i.e. `+0x08` is unambiguously the **setter**, and `+0x0c` is therefore the **getter**, the reverse of this section's own original labeling. The vtable-slot offsets/distances themselves (the "8 bytes apart" finding **[⚠ itself wrong: the pair members are 4 bytes apart — see the arithmetic note above]**, and each named property's own pair of slot numbers) are unaffected by this correction — only which physical descriptor field (`+0x08` vs `+0x0c`) is the getter vs. the setter changes. See `spec-vint-doc-format.md` §5 for the file-loading call site that caught this. **[OPEN — desk review 2026-09-30: the spec records neither the six trampoline addresses nor which descriptor field's thunk targets which vtable slot, so which slot of each pair (`+0x30`/`+0x34` etc.) is the setter is unsettled, and this section and §12.7 Group 7 still disagree; dump `element`'s descriptor records 0–2 (`+0x08`/`+0x0c`), disassemble the six thunks, and read the slot bodies; to be settled against the executable.]**

**Review status (2026-09-30): NEEDS-EXE: which vtable slot of each pair is the setter (conflict with §12.7 Group 7) — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 9.4 What this settles about §5's original guess

**[CONFIRMED — disassembly.]** "Two function pointers... very likely a constructor and a destructor, or a
constructor and an update/render callback" is refuted as a description of `FUN_00e290c0`'s own two
pointer-shaped arguments: this registration call has no per-type constructor/destructor pair in that sense
at all. The get/set pair the original automated scan's proximity heuristic actually detected is real and
disassembly-confirmed (§9.3) — it exists one level deeper than §5 placed it: once per **named
property** (up to 17 pairs for `element` alone), not once per **element type**. §5's automated-scan caveat
("the scan located the name near the call rather than proving it is the literal argument at a specific
parameter position") is resolved outright: the name **is** the literal argument at a specific, now-named
position (§9.1), and it was proximity, not the argument-position claim, that needed the correction.

**Review status (2026-09-30): DESK-PASS — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

## 10. Secondary items — mechanism relationship and mission-specific bindings

### 10.1 Item 4 — relationship between §7 and §8's mechanisms

**[CONFIRMED — disassembly, for structural separation; CONFIRMED — disassembly, for the narrower "same
bottom-most primitive" question, resolved this pass — see below.]** With both mechanisms traced
substantially further this pass, the answer firms up from §6's "no code path traced, treat as unrelated" to
a positive structural finding: `vint_callback_lua` (§7) and the named-hook dispatch family (§8) are
**confirmed to be two separate subsystems** by every structural measure available — different object pools
(95×104-byte fixed pool with a uniform vtable, vs. a 32-byte free-pool of doubly-linked list nodes),
different code regions (`0x00e19xxx`/`0x00e1axxx`–`0x00e1cxxx`/`0x00e24xxx`–`0x00e25xxx` for §7, vs.
`0x00e0cxxx`–`0x00e0dxxx` for §8) **[⚠ incomplete: §7's code also sits at `0x00e1f330`, `0x00e2a600`–`0x00e2a700` and `0x00e2b7e0`, and §8's family includes `0x00e1a1b0`]**, and different strategies for finding the target Lua value (§7: an
explicit script-side registration call, keyed by a runtime hash lookup; §8: a fixed or sprintf-templated
*name*, looked up fresh against the Lua globals table on every call via `_GetAnyGlobalSilent`).

~~What remains genuinely open is narrower: does §7's eventual "invoke the registered callback" step (traced
here only as far as a virtual-method call inside `FUN_00e25540`, or the vtable-slot-3/4 chain inside `§7.2`
step 5's destructor path) ever bottom out in the *same* low-level "call a Lua value" primitive
(`FUN_00e0ca80`/`FUN_00e0cd00`) that §8 uses, or does it use a wholly separate Lua-invocation path? **Concrete
next step:** trace the virtual method actually reached at the end of `FUN_00e25540`'s dispatch, and the
value stored at the claimed vint_callback_lua object's own callback-holding field, forward to see which (if
either) of §8's primitives it reaches.~~

**⚠ RESOLVED, this pass — same primitive, CONFIRMED.** Raw disassembly of `FUN_00e25540`'s dispatch
(`0x00e25615`–`0x00e25628`) shows `ECX` loaded with the object `FUN_00e2a700` resolved,
immediately before the call — a genuine `__thiscall` virtual dispatch on that object, through
vint_callback_lua's own single shared vtable (`0x01255920`, the same vtable §7.2 already ties slot 5 /
`FUN_00e2a620` to). `FUN_00e2a700`'s resolved object has its `+0x1c` field incremented immediately after the
call — the exact refcount field §7.2 already documents — confirming the resolved object really is a
vint_callback_lua pool object, so the vtable being called through is vint_callback_lua's own. The target at
slot 1 (offset `+0x4`, address `0x00e19d50` — previously un-boundaried; `createFunction` succeeded cleanly
here, with none of §7.4's `unaff_`/`in_stack_` artifacts) is a small trampoline: it resolves its argument 2
(the pool-2 list node's own callback field) via `FUN_00e1f330` — the same 64-slot/`0x5a8`-stride
handle-table resolver, gated by a per-slot `+0x580` back-check, that every one of §7.2's seven callers
already uses **[⚠ pointer: §7.2 does not name `FUN_00e1f330` or `FUN_00e0ceb0`; this rests on this section's own reads]** to resolve *the current script/interpreter context* for itself — then dispatches on the mode
value to one of three functions: `FUN_00e19a80` (mode 0), `FUN_00e19950` (mode 1), or `FUN_00e19c50` (modes
2–5, which covers both call sites actually traced back to it — `FUN_00e25540`'s fixed mode 2, and
`FUN_00e24c40`'s mode-4 immediate-fire branch **[OPEN — desk review 2026-09-30: §7.2 step 4 says only `FUN_00e25540` fires immediately; whether `FUN_00e24c40` has a mode-4 fire path is unsettled; to be settled against the executable.]**). **All three were force-created and decompiled. All three
call `FUN_00e0ca80` then `FUN_00e0cd00` — §8's own two confirmed primitives, verbatim** (`FUN_00e19c50`
additionally calls `FUN_00e0cef0`, §8's "exists and is function" check, first; `FUN_00e19950`'s mode-1 path
branches on whether its name string contains a colon, calling either that string directly or the fixed
literal `"vdo_base_execute_callback"` — either branch still ends in `FUN_00e0ca80`/`FUN_00e0cd00`).

The "name" argument all three pass to `FUN_00e0cef0`/`FUN_00e0ca80` is offset `+0x28` of its argument 1 — a field inline on
the vint_callback_lua object itself (passed as a raw `char*`, no dereference), which is the "callback-holding
field" this item asked to trace. It does not hold a stored Lua closure or table reference at all: it holds a
**name string**, and "invoking the callback" turns out to mean calling that name as a Lua global through the
identical `FUN_00e0cef0`→`FUN_00e0ca80`→[typed-arg-push]→`FUN_00e0cd00` sequence §8.3 already documents
end-to-end for the named-hook family. The value `FUN_00e1f330` resolves (the pool-2 node's callback field)
turns out to supply *which script/interpreter context* to call that name in — its resolved struct's own
`+0x580` field is copied into the call-context's `+0x14` right before `FUN_00e0cd00`, the same "current
script state" field every §7.2 caller already reads for itself via `FUN_00e0ceb0`/`FUN_00e1f330` — not the
callback's identity. **[CONFIRMED — disassembly, for the dispatch chain, the vtable resolution, and both
primitives reached; HIGH CONFIDENCE — inferred, for reading `+0x28` specifically as a name-string buffer
inline on the 104-byte object, since the code that first writes it was not located this pass — see below.]**

**What this settles, and what is newly, separately open.** Settled: §7's invocation path is *not* a separate
Lua-closure-calling primitive — it bottoms out in exactly the two functions (`FUN_00e0ca80`, `FUN_00e0cd00`,
plus the `FUN_00e0cef0` gate) that §8's named-hook family uses, making those the binary's only confirmed
"call a Lua value" primitives, reached by both subsystems this spec documents. ~~Newly open, and not chased
this pass because it isn't needed to answer the question this item asked: which code first writes the name
string into the vint_callback_lua object's `+0x28` field. The claim function `FUN_00e19770` itself does not
— its body was re-read directly and only manages free-list/active-list linkage at offsets `+0x20`/`+0x24` —
so `+0x28` is populated by some caller-side or later step not traced this pass. **[OPEN / UNKNOWN — who
writes `+0x28`; does not affect the CONFIRMED "same primitive" finding above.]**~~

**⚠ NARROWED, this pass — the writer is still not located, but the search space is now much smaller and
several specific candidates are ruled out by direct evidence, not inference.** Both the decompiled bodies of
all 7 confirmed callers (already available from the prior pass, `tools/lua_claim_callers.txt`) and fresh raw
disassembly of every instruction from each caller's own `CALL FUN_00e19770` through to that caller's own
function end (`tools/lua_claim_write_check.txt`) were read looking specifically for a store to `[reg+0x28]`
on the claimed/found object register (`ESI`/`EDI`/`EBX` at each site, per caller). **[CONFIRMED —
disassembly, all 7 callers, full remaining function body in each case, not just the instructions immediately
after the call.]** None of the 7 write `+0x28` themselves. Each hands the claimed-object pointer forward into
one of a small, now fully-enumerated set of helpers, all of which were also decompiled and read in full —
`FUN_00e24c40`/`FUN_00e24d70` (the target-list insert/unlink pair, §7.2 step 4), `FUN_00e25540`/`FUN_00e25920`/
`FUN_00e25250` (the fixed-mode-2 insert-and-fire path and two variant dispatchers that either delegate to it or
perform their own pool-2 insertion), `FUN_00e2a600` (release), and `FUN_00e243a0` (a small helper that stamps a
generic property-value struct's own type tag to 8 and copies the claimed object's `+0x18` into it, incrementing
the claimed object's `+0x1c` again) — **none of these ~~six~~ seven either write `+0x28`** **[count corrected: seven helpers are listed]** on the vint_callback_lua object;
all of their writes land on pool-2's 32-byte nodes (offsets `0x00`–`0x1c` only — too small to have a `+0x28` at
all) or back on the vint_callback_lua object's already-documented `+0x1c`/`+0x18`. **[CONFIRMED — disassembly,
all ~~six~~ seven decompiled and read in full.]** `FUN_00e2b7e0` — called instead of the above by the three getter-shaped
callers (`FUN_00e1bc80`, `FUN_00e1bee0`, `FUN_00e1c050`) — turns out to be the allocator for a **third**,
distinct pool (0x58-byte stride, free list `DAT_02a7cadc`) used to hand the calling Lua script back a numeric
handle; its own `+0x28` field is a self-referential slot-index/generation value computed inline in
`FUN_00e2b7e0` itself — the object's own offset from the pool's base divided by the `0x58` stride, combined
via OR with a generation counter shifted left by `0x10` bits — unrelated to vint_callback_lua's `+0x28` —
the `int→double` conversion these three callers perform right after the call (the classic unsigned-to-double
`_DAT_0113e6c8` idiom, confirmed in raw disassembly at `0x00e1be3e`/`0x00e1bff9`/`0x00e1c175`) reads *this third
pool's own* field, not the vint_callback_lua object's `+0x28`. **[CONFIRMED — disassembly.]** Separately: the raw
Lua-supplied name pointer that gets hashed (`FUN_00d9e740`) and passed as `FUN_00e19770`'s own claim argument is
not retained in any register past the claim call in any of the 7 sites — so if something later writes it to
`+0x28`, it must re-fetch the name from the Lua call stack rather than reuse the already-loaded pointer.
**[CONFIRMED — disassembly.]** The two extra `CALL FUN_00e19770` sites §7.4 already located (`0x00e1ce5e`,
`0x00e1d76a`, embedded in an un-boundaried property-switch of the same species as `FUN_00e1c410`) were not
checked this pass — they sit outside "the 7" this item was scoped to — and, along with a runtime/dynamic trace,
are the two concrete remaining leads. **[OPEN / UNKNOWN — who writes `+0x28`, narrowed to: not the claim
function, not any of the 7 confirmed callers, not their ~~six~~ seven immediate linking/dispatch helpers, not
`FUN_00e2b7e0`'s third-pool allocator. Two concrete next steps, neither attempted this pass: the two
un-boundaried extra `FUN_00e19770` call sites, or a runtime/dynamic write-breakpoint trace on `+0x28` across the
95×104-byte vint_callback_lua pool's address range.]**

**Review status (2026-09-30): NEEDS-EXE: mode-4 fire path of `FUN_00e24c40` and the mode→target mapping — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 10.2 Item 5 — mission-specific bindings

**[CONFIRMED — disassembly.]** Mission-specific named hooks exist and use **exactly the same mechanism** as
the general UI-hook family in §8, settling this item outright rather than leaving it open. Direct evidence:
`FUN_00973140`, a gameplay function specific to a named mission character interaction ("m24"/Killbane), calls
the identical `FUN_00e1a1b0`/`FUN_00e0cef0`/`FUN_00e0ca80`/`FUN_00e0cd00` sequence documented in §8.3, with
the literal names `"m24_killbane_on_death"` and `"m24_killbane_on_undowned"`. **[OPEN — desk review 2026-09-30: §14.1/§14.3 say these m24 call sites use the gameplay state `DAT_026e7e6c`, while §14.1 says `FUN_00e1a1b0()` returns the UI state `DAT_02a45450`; either this function does not use `FUN_00e1a1b0` for the state argument or `FUN_00e1a1b0` is not the UI-state accessor; to be settled against the executable.]** There is no separate
mission-specific dispatcher: mission bindings are ordinary members of the §8 family, distinguished only by
a mission-specific naming convention (an `m24_`-style prefix here) rather than by any different code path.

~~A separate, genuine per-mission-trigger **data table** was also found in this trace (`DAT_02319570`, §8.5),
sitting immediately beside the `cell_missions_button_b` hook and clearly belonging to the same
cell-phone-mission theme — but, as recorded in §8.5, its contents are runtime-populated and read as null in
the static image, so its actual per-mission hook names are **OPEN / UNKNOWN** from static analysis alone.~~ **[Superseded: `DAT_02319570` is `game_peg_load_with_cb`'s request-slot table, not a per-mission trigger table — `spec-lua-api-behaviour.md` §8.24; see §8.5.]**

**Review status (2026-09-30): NEEDS-EXE: which Lua state the m24 hooks use (`FUN_00e1a1b0` vs `DAT_026e7e6c`) — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

## 11. Status of §6's open items after this pass

This section maps §6's five original open items to this pass's results. **§6's own text is left
untouched above** — per this project's standing rule that corrections stay visible in place — this is a
status pointer, not a rewrite.

| §6 item | Status after this pass | Where |
|---|---|---|
| 1. Roster of C functions via `vint_callback_lua` | **Reframed and closed** (`vint_callback_lua` itself has no fixed roster — it holds Lua-supplied callbacks). **⚠ Superseded at the broader question level, 2026-09-29 — see §13: a genuinely separate, classic `lua_pushcclosure`+`lua_setfield`(`LUA_GLOBALSINDEX`) mechanism DOES provide a large, real, fixed, compile-time roster — 1,325 named C engine functions, structurally unrelated to `vint_callback_lua`.** **[⚠ current total 1,490, §13.7]** | §7; §13 |
| 2. Complete named hook list | **Substantially expanded, not claimed exhaustive.** 46 of 46 direct callers of the confirmed dispatcher enumerated and read; two further hook families (sprintf-templated, data-table-driven) identified; the one further generic helper's own callers (3 call sites / 2 functions) are now also enumerated — a checked negative, no further literal names recovered. **[→ §14.3 (5 §4 names are not hooks) and §14.6 (5 new literal names; orphan sites examined)]** | §8 |
| 3. Element-type registration parameter layout | **Closed.** Type-name argument position confirmed exactly; the "two function pointers" guess corrected to a per-property get/set pair one level deeper than originally placed. **[⚠ desk review 2026-09-30: closed for argument roles only; which vtable slot of each pair is the setter is still OPEN (§9.3 vs §12.7 Group 7)]** | §9 |
| 4. `vint_callback_lua` vs named-hook relationship | **Closed outright, this pass.** Structural separation confirmed, and the "same bottom-most primitive" question resolved: both subsystems bottom out in the identical `FUN_00e0ca80`/`FUN_00e0cd00` pair (plus the `FUN_00e0cef0` gate); §7's "callback" turns out to be a name string on the vint_callback_lua object, not a stored closure. **[⚠ who writes `+0x28` is still OPEN, §10.1]** | §10.1 |
| 5. Mission-specific bindings | **Closed.** Confirmed to be ordinary members of the §8 named-hook family; ~~a separate mission-trigger data table exists but is runtime-populated and unreadable statically.~~ **[Superseded: that table is `game_peg_load_with_cb`'s request-slot table, `spec-lua-api-behaviour.md` §8.24.]** | §10.2 |

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

## 12. Missions-Lua domain scoping pass — numbered inventory and size estimate

**Purpose, stated up front per this task's own scope:** this section is a **scoping pass, not a full trace**. The
goal is to size the remaining missions-Lua domain — how many distinct engine-side hook names / callable
entry points exist for mission scripting — not to explain what each one's body does. Breadth over depth
throughout; several items below are addressed by name/address and category only, exactly as scoped.

### 12.1 Method

Two of this section's three inputs reuse the exact method §4 called "fully reusable" and §8.1 already scaled
up once: enumerate every cross-reference to a confirmed dispatcher via Ghidra's reference database, then
decompile and read every resulting caller by hand. The third input is new: the `"Mission LUA script"`/`"mission
lua temp"` strings §6 (original) flagged as an untraced anchor were located directly (`Memory.findBytes`),
their cross-references enumerated, and every referencing function decompiled and read — the same discipline,
applied to a new starting point. A fourth, smaller input is a whole-`.rdata`/`.data` literal-string census
(regex `^m[0-9]{1,3}_[a-z_0-9]{2,60}$`) run to size the mission-specific naming convention §10.2 already
confirmed exists for one mission (`m24_`); this is presented as **string-presence evidence**, not
individually-confirmed dispatcher call sites, and is labeled accordingly throughout. All four inputs are
disassembly/data, not sampling.

**Review status (2026-09-30): DESK-PASS — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 12.2 Correction found while consolidating — §4/§8's own name count was off

**[CONFIRMED — direct recount of §4's and §8.2's own tables, both re-read in full.]** While building the
consolidated inventory below, §4's table was recounted directly rather than trusted at its stated
description: it contains **53** distinct literal names, not the "36" §8.2 referred to when it wrote "on top
of the 36 already published in §4." Separately, five of §8.2's own 25 listed names are literal duplicates of
names already in §4's table (`screen_fade_do`, `garage_vehicle_load_begin`, `garage_vehicle_load_completed`,
`pause_map_interface_event`, `pause_map_stag_completion` — all five confirmed present in both tables by direct
text comparison), so only **20** of §8.2's 25 are genuinely new. The correct distinct total across §4+§8 is
therefore **53 + 20 = 73**, not the "61" both §8.2 and §11 state. Per this project's standing convention,
§4/§8/§11's own text is left untouched above — this is a pointer to the correction, not a silent rewrite — and
every count from here on in this section uses the corrected figure (73), confirmed by direct enumeration in
§12.3 below.

**Review status (2026-09-30): DESK-PASS — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 12.3 Group 1 — the general named-hook family, recounted and consolidated (73 entries, #1–73)

**⚠ §14.3 later showed 5 of these 73 are not Lua hooks (`cmp_mission_success`, `cmp_activity_success`, `cmp_fail_populate`, `garage_populate`, `garage_performance_stats`) — 68 remain. The list below is kept as written.** **[→ §14.6 later found 5 further literal hooks (`store_weapon_uncover_weapon`, `store_gallery_upload_complete`, `store_gallery_download_list_complete`, `screen_capture_open_preview_dialog`, `dialog_build`), so the literal family stands at 73 again. §14.3 re-checked only 9 of these names (5 refuted, 4 confirmed); the others still rely on §4's scan or on §8.2.]**

**[CONFIRMED — disassembly/empirical, per §4 and §8's own confirmation standard; this section adds no new
names here, only a corrected consolidated count.]** All 73 distinct literal names from §4+§8, deduplicated,
by category:

1–11. **Mission/activity completion screens (11):** `cmp_mission_success`, `cmp_activity_success`,
`cmp_fail_populate`, `cmp_rewards_success`, `cmp_stronghold_success`, `credits_grab_credits`,
`horde_results_populate`, `cat_mouse_results_populate`, `coop_diversion_success_responder`,
`cmp_common_screen_start`, `completion_coop_disconnected`.

12–21. **Main menu / pause / options (10):** `main_menu_internet_disconnected`,
`main_menu_ethernet_disconnected`, `Main_menu_gameboot_complete`, `pause_map_interface_event`,
`pause_map_stag_completion`, `options_display_populate_display_modes`, `options_display_populate_msaa`,
`vint_remap_update_display`, `vint_remap_get_action_binding`, `pause_map_is_taxi_mode`.

22–35. **HUD / inventory (14):** `hud_running_man_event_update`, `hud_running_man_load_complete`,
`hud_touch_combo`, `hud_qte`, `hud_diversion`, `hud_zombie`, `hud_btnmash`, `hud_mayhem_world_cash_update`,
`object_indicator_update`, `object_indicator_remove`, `hud_inventory_hide`, `hud_inventory_show`,
`hud_hit_clear_all`, `hud_msg_hide_region`.

36–43. **Garage / vehicle (8):** `garage_vehicle_load_begin`, `garage_vehicle_load_completed`,
`garage_populate`, `garage_performance_stats`, `store_vehicle_switch_mode`,
`store_vehicle_signal_swap_complete`, `store_vehicle_exit_begin_bg_clear`, `clones_m3_pow`.

44–49. **Vehicle customization (6):** `vcust_populate_menu`, `vcust_populate_palette_menu`,
`vcust_populate_underglow_color`, `vcust_populate_color_grid`, `vcust_populate_wheel_menu`,
`vcust_populate_wheel_grid_menu`.

50–51. **Tutorials (2):** `tutorial_advance`, `tutorial_responder`.

52–56. **Minigames (5):** `button_mashing_minigame`, `sr2_balance_meter`, `mayhem_local_player_world_cash`,
`whored_countdown_timer_update`, `cell_missions_button_b`.

57–68. **Screen transitions / misc UI / system (12):** `screen_fade_do`, `screen_fade_auto_save_show`,
`screen_fade_auto_save_hide`, `countdown_display`, `countdown_unpause`, `newsticker_populate`,
`store_gallery_init_complete`, `map_filter`, `map_district_names`, `gis_grain_fade_out`,
`vint_lib_init_constants`, `city_load_hide_images`.

69–71. **Store / controls (3):** `store_lock_controls`, `store_unlock_controls`, `store_weapon_cover_weapon`.

72–73. **Mission-specific, general dispatcher (2):** `m24_killbane_on_death`, `m24_killbane_on_undowned`
(§10.2 — confirmed to use this exact family, not a separate mechanism).

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 12.4 Group 2 — new this pass: the "dynamic globals" lifecycle hooks (2 entries, #74–75)

**[CONFIRMED — disassembly, new this pass.]** Following the `"mission lua temp"` string anchor from §6's
open item to its containing function, `FUN_006cfac0` (the `"Mission LUA script"` resource-type's own
constructor — see §12.5), led to `FUN_00e0de80`, a previously-undocumented mechanism, structurally distinct
from §8's family: it calls two Lua globals **unconditionally**, with no `_GetAnyGlobalSilent` existence
check first (unlike every §8 entry) — consistent with these being system-lifecycle hooks the Lua side is
*expected* to always define, not optional per-screen hooks:

74. `_PrepareForDynamicGlobals` — called before the actual script chunk is loaded/run (category:
    **system/lifecycle**).
75. `_DynamicGlobalsLoadComplete` — called after, only on a successful load (category: **system/lifecycle**).

`FUN_00e0de80` itself has exactly 2 confirmed callers: `FUN_006cfac0` (Mission LUA script ctor) and
`FUN_00e213d0` (itself one of Group 1's 46 general-hook-family callers, §8.1) — so "dynamic globals loading"
is shared infrastructure, used by mission-script loading but not exclusive to it. This directly informs
§6 item 5 (see §12.8).

**Review status (2026-09-30): DESK-PASS — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 12.5 The "Mission LUA script" resource type — what the §6 anchor strings actually are

**[CONFIRMED — disassembly.]** `"Mission LUA script"` is **not** a hook-dispatcher string at all — it is one
literal name field inside the master resource-type registration table (`FUN_00700780`, the same ~45-entry **[⚠ 43 entries, `spec-format-inventory.md`; also §14.5]**
table `spec-format-inventory.md` already fully catalogs), entry type ID `0x20`. Its registered constructor is
`FUN_006cfac0`; destructor `LAB_006cfba0`. This makes mission Lua scripts a genuine, separately-registered
**resource type**, on par with `.anim_pc`/`.rig_pc`/`.xtbl`/etc., and structurally distinct from two other,
separately-registered Lua-related resource types in the same table: `"VINT doc"` (type `0x1a`, ctor
`FUN_007b6760`) and `"Vint LUA script"` (type `0x1b`, ctor at `LAB_007b6810`) — three different constructor
addresses for three different Lua-related resource types, not one shared loader.

`FUN_006cfac0` gates on a runtime flag (`FUN_0087ba20`, a simple global-read, `DAT_024d8534` — checked at
~200+ unrelated call sites across gameplay code, ~~consistent with a general "is a mission/level currently
active" state flag, not traced further here~~ **[Superseded: `spec-lua-api-behaviour.md` §8.27/§6.22 identify `0x0087ba20`/`0x024d8534` as the co-op/network session-context accessor and the `+0x5c` == `+0x58` field pair as the host check, so this gate reads "session present and this machine is host", not "mission active"]**) and a counter-pair equality check **[OPEN — desk review 2026-09-30: whether this counter-pair check is that same `+0x58`/`+0x5c` host test; to be settled against the executable.]**, then builds the
`"mission lua temp"` debug/scratch-buffer name and calls `FUN_00e0de80` (§12.4) to actually load and run the
script. **This directly answers §6 item 5's "no mission-specific dispatcher was individually traced" gap**:
there is a mission-specific *resource type and constructor* (this section), even though — per §12.4 and
§10.2 — the actual Lua-call-in/call-out primitives it bottoms out in are shared, general infrastructure, not
a separate mission-only C-function roster.

**Review status (2026-09-30): NEEDS-EXE: the gate instructions of `0x006cfac0` and the counter-pair check — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 12.6 Group 3 — mission-numbered hook-name string census (58 new entries, #76–133)

**[CONFIRMED — empirical, string presence: all 58 strings below were read directly from `.rdata`/`.data` at
the addresses shown.]** **[HIGH CONFIDENCE — inferred, not individually confirmed, that each one is actually
passed to a hook dispatcher:** only 2 of the underlying 61 raw matches (`m24_killbane_on_death`,
`m24_killbane_on_undowned`, already counted in Group 1, #72–73) were individually traced to a real dispatcher
call site (§10.2); the other 58 share the identical `m<mission-number>_<name>` naming convention and are
presented as strong sizing evidence for the missions domain, not as individually re-confirmed dispatcher
targets — consistent with this task's own "breadth over depth" scope.] One further raw match, `m3_pow` @
`0x01161af3`, was excluded as a **false independent hit**: it is not a separate string, but a byte-offset
substring landing inside the already-known, already-counted `clones_m3_pow` (#43) — the address arithmetic
checks out exactly (`"clones_"` is 7 bytes, and `0x01161af3` is 7 bytes past `clones_m3_pow`'s own start),
confirmed rather than assumed, per this project's own repeated lesson about naive substring/whole-scan counts
(`WALLS.md`).

76–78. **Cutscene/film-trigger (3):** `m02_clapboards_get`, `m02_clapboards_reset`, `m02_clapboards_set`
(a film-slate/take marker, matching the "movie set" mission's own theme).

79–81. **Costume/customization (3):** `m02_ear`, `m02_gloves`, `m02_suit_f_bod`.

82–84. **Vehicle/traversal (3):** `m02_plane`, `m02_pull_chute_1`, `m02_pull_chute_2`.

85–87. **Cutscene trigger (3):** `m02_shaundi_pushed_away`, `m02_shoot_plane_window`, `m10_strip`.

88. **Minigame/QTE (1):** `m02_skyqte_01`.

89–92. **Combat (4):** `m03_fake_tag_punch_hit`, `m03_punch_load`, `m03_punch_unload`,
`m03_set_sprint_waning`.

93. **Combat/ability (1):** `m03_super_powers`.

94. **Vehicle/combat (1):** `m05_heli_pierce`.

95–112. **Camera (18):** `m12_josh_overshoulder_run_back/_forward/_left/_right/_stand` **[⚠ desk review 2026-09-30: this shorthand reads as `…_run_stand`; the closing description ("run+walk × 4 directions + stand each") and the `plym` entry suggest `m12_josh_overshoulder_stand` — not re-read from the image]**,
`m12_josh_overshoulder_walk_back/_forward/_left/_right` (9), `m12_plym_overshoulder_run_back/_forward/_left/_right`,
`m12_plym_overshoulder_stand`, `m12_plym_overshoulder_walk_back/_forward/_left/_right` (9) — a scripted
over-the-shoulder follow-cam, two named characters ("josh"/"plym"), run+walk × 4 directions + stand each.

113–128. **Gameplay-modifier/minigame-effect (16):** `m16_boss_invert`, `m16_boss_invert_stop`,
`m16_boss_slow`, `m16_boss_slow_stop`, `m16_cheat_slow`, `m16_cheat_slow_stop`, `m16_glitch`,
`m16_hotdog_mute_cannon`, `m16_hotdog_unmute_cannon`, `m16_invert`, `m16_invert_stop`, `m16_qte_complete`,
`m16_reset_all`, `m16_shockwave_impact`, `m16_shrink`, `m16_shrink_stop` — a reality-distortion/simulation
mission (invert/slow/shrink/glitch effects), consistent with SR3's STAG virtual-reality mission.

129–132. **Combat/boss-fight (4):** `m21_crowd_cheer_mid`, `m21_killbane_damaged`,
`m21_killswitch_qte_complete_cb`, `m21_wieldable_prop_created_cb` — the Killbane boss encounter, matching
Group 1's already-confirmed `m24_killbane_*` pair thematically (two different mission numbers, same named
antagonist, consistent with a recurring boss across multiple missions).

133. **Narrative choice (1):** `m21_player_choice`.

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 12.7 Groups 4–7 — mechanisms and Lua-callable surfaces already traced in §7–§9, folded into one inventory

These were not re-derived this pass; they are the confirmed, addressed findings from §7–§9, restated here as
numbered inventory entries so the total in §12.9 reflects the whole missions-adjacent Lua surface, not just
the newly-censused hook names.

**Group 4 — templated (sprintf) per-widget hook-name families (7 mechanism entries, #134–140), §8.4:**
134. `"%s_reset"` 135. `"%s_exited"` 136. `"%s_gained_focus"` 137. `"%s_lost_focus"` 138. `"%s_cleanup"`
(2 call contexts, 1 format string) 139. `"%s_init"` 140. `"%s_play_exit_anim"` — category **UI/HUD,
per-widget-instance**; each generates an unbounded, runtime-only set of concrete names (one family per named
widget instance), not individually enumerable statically.

**Group 5 — data-driven hook tables (2 mechanism entries, #141–142), §8.5:**
141. `DAT_026e83f0` (stride 12 bytes, packed 16:16 index) 142. `DAT_02319570` (stride 0x68 bytes, ~4-entry
bound, sits immediately beside `cell_missions_button_b`) **[⚠ superseded: `game_peg_load_with_cb`'s request-slot table (Lua-supplied callback names), `spec-lua-api-behaviour.md` §8.24 — not a mission trigger table; see §8.5]** — category **mission trigger, data-driven,
runtime-populated**; both read as entirely null in the static image (§8.5's checked-negative population
figures), so their real per-mission contents are OPEN/UNKNOWN from static analysis.

**Group 6 — Lua-callable `vint_callback_lua` registration entry points (7 entries, #143–149), §7.2:**
143. `FUN_00e1a5e0` 144. `FUN_00e1a7a0` (both runtime string-mode-selecting: `"update"`/`"remove"`/`"insert"`)
145. `FUN_00e1acd0` 146. `FUN_00e1bc80` 147. `FUN_00e1bee0` 148. `FUN_00e1c050` (fixed-mode variants)
149. `FUN_00e1c410` (large property-switch dispatcher, one case of which also claims a slot) — category
**Lua→engine callback registration** (`object:insert(...)`/`:remove(...)`/`:update(...)`-style Lua-callable
methods; this is the genuine "Lua calls C" direction, as opposed to Groups 1–5's "engine calls Lua").

**Group 7 — VDO element property accessor trampolines (6 entries, #150–155), §9.3:**
150. `render_mode` getter (vtable `+0x34`) 151. `render_mode` setter (`+0x30`) 152. `visible` getter (`+0x3c`)
153. `visible` setter (`+0x38`) 154. `mask` getter (`+0x44`) 155. `mask` setter (`+0x40`) — category
**Lua→engine property accessor**, reached via the per-type property-descriptor table (§9.2); only 6 of the
(up to 17 properties × 2 × 13 element types =) much larger structurally-implied population were individually
disassembled and addressed — the rest are HIGH CONFIDENCE by structural pattern only, per §9.3's own scope,
and are **not** included in the count below to keep every counted entry individually addressed. **[OPEN — desk review 2026-09-30: the getter/setter orientation of #150–155 is the reverse of §9.3's current text; see §9.3; to be settled against the executable.]**

**Review status (2026-09-30): NEEDS-EXE: Group 7 getter/setter slot orientation (conflict with §9.3) — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 12.8 Status of §6 items after this pass

| §6 item | Status after this pass | Where |
|---|---|---|
| 2. Complete named hook list | **Further expanded and consolidated; a pre-existing count error corrected.** 73 confirmed literal names (was misreported as 61) **[⚠ 68 after §14.3; +5 in §14.6]**, plus 2 newly-found unconditional lifecycle hooks, plus a 58-name mission-numbered census (string-presence evidence, not individually dispatcher-confirmed). Still not claimed fully exhaustive — the templated (Group 4) and data-driven (Group 5) families remain unbounded/runtime-only by nature. | §12.2–§12.4, §12.6 |
| 5. Mission-specific bindings | **Further resolved with a concrete mechanism.** §10.2 already closed the "is there a separate dispatcher" question (no). This pass adds the concrete "is there a separate mission resource type/loader" question (yes — §12.5: `"Mission LUA script"`, type `0x20`, ctor `FUN_006cfac0`, distinct from `"VINT doc"`/`"Vint LUA script"`'s own ctors) and sizes the mission-specific naming convention itself at 58+2=60 confirmed-present names across at least 8 missions (§12.6). | §12.4–§12.6 |

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 12.9 Total count and category breakdown

**155 distinct, individually-addressed engine-side entries** **[⚠ 150 after §14.3 removed 5 non-hook names from Group 1; the category table below is not recomputed]** **[⚠ desk review 2026-09-30: 155 again — §14.3 −5, §14.6 +5. Recomputed from the lists: Completion 11→8, Vehicle 17→15, Menu/options/tutorials/store-controls 15→16 (`store_weapon_uncover_weapon`), HUD/UI 39→43 (the two gallery hooks, `screen_capture_open_preview_dialog`, `dialog_build`); the table below is kept as written]** across the missions/UI-Lua binding surface
inventoried this pass (Groups 1–7, §12.3–§12.7 — every number below sums the exact entries listed above, not
a rounded estimate):

| Category | Count |
|---|---|
| HUD/UI (general hooks + templated families + property accessors) | 39 |
| Vehicle (garage/traversal/customization) | 17 |
| Camera | 18 |
| Gameplay-modifier/minigame-effect (STAG-style) | 16 |
| Menu / options / tutorials / store-controls | 15 |
| Mission/activity completion screens | 11 |
| Combat (general + boss-fight + ability + vehicle-combat) | 10 |
| Lua→engine callback registration (`vint_callback_lua`) | 7 |
| Minigame/QTE (general) | 6 |
| Cutscene/mission-scripted triggers | 6 |
| Mission-specific named hooks + system/lifecycle | 4 |
| Costume/customization | 3 |
| Data-driven mission-trigger tables (unresolved, runtime-only) | 2 |
| Narrative choice | 1 |
| **Total** | **155** |

**What this total does and doesn't mean, stated plainly per this task's scoping intent:** 155 is the count of
individually-named-or-addressed entries this pass can point at directly. It is **not** an upper bound on the
runtime domain — Group 4's 7 template families and Group 5's 2 data tables each expand, per mission/per
widget/per session, into a further population this static pass cannot enumerate (§8.4, §8.5), and Group 7's
6 disassembled property accessors structurally imply a much larger (~100+, unaddressed) sibling population
across the other 12 VDO element types (§9.2). For sizing purposes: the confirmed-plus-high-confidence static
surface is **~155 entries**, with a further **unbounded, runtime-only tail** on top of it — a materially
different (and much more concrete) picture than "several hundred call sites" (§4's original, unscoped
figure, which counted raw call sites rather than distinct names/functions).

**⚠ Important scope note added this pass, see §13.** Everything counted above (§4–§12) is the **engine-calls-
INTO-Lua** direction (named optional hooks) plus the confirmed-but-nameless Lua-registers-a-callback direction
(`vint_callback_lua`, §7). A peer team asked the natural remaining question directly: is there also a classic,
large, **Lua-calls-a-named-C-function** roster — the "`SpawnNPC`/`SetWaypoint`"-style mission API a Lua-scripted
game of this kind would be expected to have? §13 answers this: **yes, CONFIRMED, and it is large (1,325
entries)** — a mechanism structurally separate from every one of §3/§7's and §4/§8's findings, using the real,
public Lua 5.1 C API's own `lua_register` idiom directly rather than any custom wrapper.

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

## 13. The classic Lua 5.1 `lua_register` idiom — CONFIRMED, large, and structurally separate from §3/§7 and §4/§8

**Prompted by a precise peer-team question:** neither `vint_callback_lua` (§3/§7 — confirmed to hold
Lua-*supplied* callbacks registered at runtime, not a compiled-in C-function roster) nor the named-hook family
(§4/§8/§12 — the opposite direction, engine checking for an optionally-defined Lua function) is the classic
"large roster of engine C functions a mission script calls directly" pattern real Saints Row-style games are
expected to have. **This section confirms that pattern exists, separately from both, and sizes it.**

### 13.1 Method and anchor

The real, public Lua 5.1 C API has an unmistakable, well-documented registration idiom for exposing a C
function to Lua: `lua_pushcfunction`/`lua_pushcclosure` (push a C function as a callable value) followed by
`lua_setglobal`/`lua_setfield(L, LUA_GLOBALSINDEX, name)` (bind it to a global name) — the literal expansion of
the public `lua_register` macro. This is standard, non-proprietary Lua VM usage, citable on the same footing
already established in this document for `"Lua 5.1"`, `LUA_TFUNCTION`, and the base/coroutine standard-library
tables (§1).

**Anchor:** this document's own §1 already confirmed the real base (`_G`) and coroutine standard-library
tables exist as ordinary `{name, function}` static arrays in `.rdata`. Locating the exact bytes of the known
name strings from those two tables (e.g. `"collectgarbage"`, `"resume"`) and reading every data
cross-reference *to* each string recovered the arrays directly: two adjacent, NULL-terminated,
8-byte-stride `{namePtr, funcPtr}` arrays at `0x01293410` (23 entries — the base/`_G` library) and
`0x012934e8` (6 entries — the coroutine library), exactly matching real Lua 5.1 source (`lbaselib.c`'s
`base_funcs[]`/`co_funcs[]`, each terminated by a `{NULL,NULL}` sentinel). **[OPEN — desk review 2026-09-30: stock Lua 5.1 `base_funcs[]` has 24 entries, not 23, so either one stock entry is missing here or the count is off; dump `0x01293410` to its terminator; to be settled against the executable.]** **[CONFIRMED — disassembly, raw
`.rdata` dump, both arrays read in full including their terminators.]**

The only two direct (data) references to these two array base addresses are both at call sites passing the
array pointer plus a library-name string to one function, `0x00fcb970` — matching the real Lua 5.1
`luaL_register(L, libname, l)` macro's exact 3-argument shape. `0x00fcb970` is itself a one-line forwarder to
`0x00fcb7a0` with a constant fourth argument of `0`, matching `luaL_register`'s own real-source definition as
`luaI_openlib(L, libname, l, 0)`. Reading `0x00fcb7a0`'s body confirms it structurally against real
`luaI_openlib` source line-for-line: it walks the array until a null name field, and for each live entry calls
`0x00dfe4f0` — matching `lua_pushcclosure(L, l->func, nup)`, including copying `nup` upvalues off the stack and
tagging the pushed value with Lua's own public `LUA_TFUNCTION` type tag (already established in this
document, §4) — then calls `0x00dfe830` with a stack index computed as `-(nup+2)`, matching
`lua_setfield(L, -(nup+2), l->name)`. **[CONFIRMED — disassembly, both `0x00fcb970` and `0x00fcb7a0` read in
full and matched against real Lua 5.1 `lauxlib.c` source structure.]**

This pins down the two load-bearing primitives with certainty:

- **`0x00dfe4f0` = the real public `lua_pushcclosure(L, fn, nup)`** (with `nup`=0, this is exactly
  `lua_pushcfunction`).
- **`0x00dfe830` = the real public `lua_setfield(L, idx, name)`.** The constant seen as its index argument at
  the base-library call site, `0xffffd8ee`, is exactly `-10002` in 32-bit two's-complement — Lua 5.1's own
  public `LUA_GLOBALSINDEX` constant. `lua_setfield(L, LUA_GLOBALSINDEX, name)` is the real-source definition
  of `lua_setglobal(L, name)`.

With both primitives address-identified, **every** direct caller of `0x00dfe830` anywhere in the binary was
enumerated directly from Ghidra's reference database — not a proximity guess — giving **62 call sites total**,
each one's preceding instruction window read for the identifier-shaped name-string argument and for a paired
call to `0x00dfe4f0` (confirming a function, not some other value type, is being bound). **[CONFIRMED —
disassembly, population fully enumerated: 62/62 call sites read directly.]**

**Review status (2026-09-30): NEEDS-EXE: 23 vs 24 `base_funcs` entries — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 13.2 The result: 1,325 individually-registered, named engine C functions, in two clusters tied to two Lua states

Of the 62 sites, 4 belong to the already-documented base-library open routine itself (binding `_G`,
`_VERSION`, an `ipairs` alias, and an internal upvalue-table field — Lua-internal bookkeeping, not new). One
math-related cluster of names (`sin`/`cos`/`tan`/`floor`/`ceil`/etc.) turned out, on closer inspection, to
reuse the same literal name strings as the MSVC CRT's own `___libm_error_support` domain-error helper — but
those exact strings are *also* independently used as real Lua-visible global names by a 24-pair block inside
one of this section's own registration wrappers (`0x00e0f900`), confirmed by reading that wrapper's own
`lua_pushcclosure`/`lua_setfield` call pair sequence directly rather than trusting the string coincidence —
~~this is Lua's own `math` library, registered by hand rather than via a static table, and is not counted
further here since it is standard-library, not new API surface.~~ **[Corrected 2026-10-01, job `20261001T020218-team-a-bgcx`: the block is registered by hand (one push-closure/set-field pair per name, 24 iterations), but it is not Lua's `math` library. Only 9 of the 24 names are standard math names, and 15 are engine functions; all 24 are bound as bare globals, not into a table. It is API surface a host must provide. Full roster below. CONFIRMED — disassembly.]** **[⚠ Qualified §16.3: one of these 24 pairs is `FUN_00e0f010`, an engine "mark library opened" primitive, so the block is not pure standard library; and these names are bound as bare globals, not into a `math` table. Team B's `lua_reconciliation_called_not_registered_101.tsv` shows real scripts calling bare globals absent from every census file (e.g. `max` 704 calls, `floor` 449, `rand_int` 166, `rand_float` 84, `round` 37, `debug_print` 816); ~~which of them are among the 24 pairs is not established.~~ **[2026-10-01, job `20261001T020218-team-a-bgcx`: all six are among the 24 (roster below). CONFIRMED — disassembly.]**]** **[OPEN — desk review 2026-09-30: the 62-site budget does not close: 4 base-library sites + the `0x00e0f900` block + ~57 wrapper sites leaves no room for the `0x00e0ef80` (§13.6) and `0x00e1e445` (§13.7) sites, and §17 partitions the 62 differently (57 `LUA_GLOBALSINDEX` = 55 closure + 2 base, plus 5 other); list all 62 sites of `0x00dfe830` with containing function, index and pushed value type, and ~~dump the 24 pairs of `0x00e0f900`~~ **[done 2026-10-01, job `20261001T020218-team-a-bgcx`; roster below]**; to be settled against the executable. **[Still OPEN 2026-10-01: the 62-site budget itself; this job shows only that the registrar contributes one call site (inside its loop) and does not list all 62.]**]**

**The 24 bare globals of `0x00e0f900` (re-derived from the executable 2026-10-01, job `20261001T020218-team-a-bgcx`).**
`0x00e0f900` takes the Lua state as its only argument. It fills a local array of 24 name/function
pairs, then loops 24 times; each pass pushes the function as a closure with no upvalues
(`lua_pushcclosure`, `0x00dfe4f0`) and stores it under its name in the globals table (`lua_setfield`,
`0x00dfe830`, at the globals pseudo-index -10002). No table is created first. This is the
`lua_register` idiom of §13.1, so **the 24 names are bare globals**: a script that calls `floor` gets
`0x00e0f3a0`, and `math.floor` (if the stock library is opened) is a separate binding. **CONFIRMED —
disassembly.** Five of the names are three-letter strings that the dump shows only as raw dwords; they
decode to `abs`, `cos`, `sin`, `max` and `min`. **CONFIRMED.**

| # | Lua global | Native | Kind |
|---|---|---|---|
| 1 | `abs` | `0x00e0f140` | standard math name |
| 2 | `acos` | `0x00e0f180` | standard math name |
| 3 | `cos` | `0x00e0f1c0` | standard math name |
| 4 | `sin` | `0x00e0f210` | standard math name |
| 5 | `ceil` | `0x00e0f260` | standard math name |
| 6 | `debug_print` | `0x007c9f50` | engine (shared no-op stub, §13.6) |
| 7 | `assert_msg` | `0x007c9f50` | engine (same stub) |
| 8 | `floor` | `0x00e0f3a0` | standard math name |
| 9 | `get_frame_time` | `0x00e0f400` | engine |
| 10 | `include` | `0x00e0f010` | engine ("mark library opened", §16.3) |
| 11 | `max` | `0x00e0f2c0` | standard math name |
| 12 | `min` | `0x00e0f330` | standard math name |
| 13 | `rand_float` | `0x00e0f430` | engine |
| 14 | `rand_int` | `0x00e0f4c0` | engine |
| 15 | `round` | `0x00e0f530` | engine |
| 16 | `sizeof_table` | `0x00e0f580` | engine |
| 17 | `sqrt` | `0x00e0f5c0` | standard math name |
| 18 | `strstr` | `0x00e0f080` | engine (C-library name) |
| 19 | `thread_check_done` | `0x00e0f610` | engine |
| 20 | `thread_kill` | `0x00e0f650` | engine |
| 21 | `thread_new` | `0x00e0f680` | engine |
| 22 | `thread_yield` | `0x00e0f0d0` | engine |
| 23 | `closest_point_on_line_segment` | `0x00e0f740` | engine |
| 24 | `which_side_of_2d_line` | `0x00e0f830` | engine |

Names, order and native addresses: **CONFIRMED — disassembly.** "Standard math name" describes the
name only; the nine math-named bodies were not dumped, so whether they behave like stock `math.*` is
**OPEN** (follow-up job `team-a/ghidra/jobs/bgcx-followup.json`). The libm name-string coincidence
noted above is real, but the binding is too, because the registrar stores the function pointers
itself. All six script-called names Team B listed (`max`, `floor`, `rand_int`, `rand_float`, `round`,
`debug_print`) are in the roster, as are `abs`, `min` and `ceil`. **CONFIRMED.**

**Which state.** `0x00e0f900` has exactly one caller in the binary, a plain call inside the generic
state creator `0x00e0e0b0` (§16.1 step 1(a), §16.2). That creator calls it unconditionally for every
state it makes: after it creates the raw state and calls `0x00fccb70`, and before it loads
`system_lib.lua`. Its two callers are the interface bring-up (`0x00e1e460`) and the gameplay bring-up
(`0x00a1fa10`). **Both Lua states therefore get all 24 bare globals, before any preload file and before
every other registrar.** **CONFIRMED — disassembly** (this job's xref of `0x00e0f900`; the body of
`0x00e0e0b0` from sibling job `20261001T021641-team-a-yduu`). That `0x00fccb70` opens the stock
standard libraries is **HYPOTHESIS** (position only; body not dumped). Behaviour of the dumped
functions: `spec-lua-api-behaviour.md` §26.27.

The remaining ~57 sites, spread across 43 distinct, otherwise-unrelated engine functions between `0x005a01d0`
and `0x00bcd240`, each push one real C function pointer via `lua_pushcclosure` (`nup`=0) and bind it to one
literal name string via `lua_setfield(L, LUA_GLOBALSINDEX, name)` — the exact, hand-unrolled expansion of
`lua_register(L, name, func)`, repeated individually rather than gathered into one static table. Several of
these 43 functions register more than one name each, by filling a local stack array with several
`{namePtr,funcPtr}` pairs and looping over it with the identical two-call sequence — the same idiom as the
math-library wrapper above, just built on the stack rather than in `.rdata`. **[CONFIRMED — disassembly, all
57 sites read directly; every one resolves to a real identifier-shaped name string plus a real paired
closure-push.]**

These 43 wrapper functions are not scattered arbitrarily — they resolve into exactly **two** clusters, each
tied to a distinct engine subsystem and (very likely) a distinct Lua state:

1. **A 42-wrapper, 311-name UI/menu/store/HUD cluster**, all called in one fixed, straight-line sequence (no
   loop, no table — 49 direct calls, one per wrapper **[OPEN — desk review 2026-09-30: 42 wrappers vs 49 calls does not match "one per wrapper"; also, `0x00845aa0` (§13.5) lies inside the 43-function range, so it may be the 43rd function; to be settled against the executable (callee list of `0x008430f0`).]**) from a single function, `0x008430f0`. That function's
   own address is itself ~~stored into a subsystem dispatch table~~ **[corrected §16.1(d): passed as a callback into `FUN_00e1e460` and invoked once]** by the game's top-level UI/engine bring-up
   routine (`0x008489e0`) — the same routine that stamps `vint_callback_lua`'s shared vtable (§3) and opens
   the `vint_lib.lua`/`system_lib.lua`/`game_ui_globals.lua` custom libraries already documented in §2.
   Representative real names, read directly as literal strings at their own registration call sites:
   `garage_remove_vehicle`, `cellphone_dial`, `shop_purchase_purchase_shop` (bound directly to a real,
   separately-existing engine function at `0x00a03490`, confirmed by reading the exact pushed closure
   argument), `vcust_revert_color`, `cell_missions_clear_new_status`, `cell_music_menu_toggle_station`,
   `playlist_play_track`, `sb_select_chop_shop_vehicle`, `msn_killbane_selected_option`,
   `msn_text_adventure_set_screen`, `object_indicator_init_lua`,
   `game_machinima_clip_exists`, `cinema_editor_create_camera_zone`, `pause_map_set_gps`,
   `save_system_cancel_coop_load`, `game_record_set_quality_level`, `options_display_pc_set_options`,
   `vint_options_remap_set_key_binding`, `online_validator_set_checking`, `game_lobby_update_char_selection`,
   plus the full `store_*` family (`store_character_lineup_loaded`, `store_clothing_get_store_id`,
   `store_common_bg_anim_complete`, `store_crib_init_crib_garage`, `store_gallery_display_character`,
   `store_gang_allow_input`, `store_stronghold_game_purchase_upgrade`, `store_vehicle_do_return_to_crib`,
   `store_weapon_purchase_ammo`, `store_dlc_is_offer_free`).
2. **One single wrapper function, `0x00a20840`, registering 1,014 names by itself** — by far the largest
   single registration site found anywhere in this binary, filling a roughly 8 KB local stack array (MSVC's
   own `__alloca_probe` stack-check helper fires on entry, consistent with an allocation this large) with
   1,014 `{namePtr,funcPtr}` pairs and looping the identical `lua_pushcclosure`/`lua_setfield` pair over all
   of them. This function is called from `0x00a1fa10`, which **creates a separate Lua state named
   `"game play"`** (a literal string argument read directly at the state-creation call), registers this
   1,014-name roster into that state's globals, then loads `"game_lib.lua"` into the same state — i.e., this
   is the dedicated **gameplay/mission** Lua VM, distinct from the UI Lua state cluster (1) feeds. Real names
   sampled directly from the start, middle, and end of the array (not cherry-picked): `action_nodes_enable`,
   `action_play_do`, `action_sequence_advance`, `add_object_indicator_to_closest_npc`,
   `airplane_fly_to_do`/`airplane_land_do`/`airplane_takeoff_do`, `ambient_cop_spawn_enable`,
   `ambient_gang_spawn_enable`, `ai_add_enemy_target`, `ai_attack_region`, `ai_clear_priority_target`,
   `ai_do_scripted_advance`/`ai_do_scripted_fire`/`ai_do_scripted_move`, `vehicle_show`,
   `vehicle_speed_override`, `vehicle_skydiving_start`/`vehicle_skydiving_stop`, `vehicle_stop_do`,
   `vehicle_suppress_flipping`, `vehicle_turret_base_to_do`, `vehicle_is_vtol_hover`/`vehicle_is_vtol_jet`,
   `wander_start`/`wander_stop`, `waypoint_add`/`waypoint_is_placed`/`waypoint_remove`,
   `wind_override_set`/`wind_override_clear`, `world_despawn_all_pedestrians`/`world_despawn_all_vehicles`,
   `zscene_prep`/`zscene_is_loaded`. **[CONFIRMED — disassembly: function body address range read in full
   (`0x00a20840`–`0x00a25f7e`), 1,014 of the ~1,016 expected name-slot immediates resolved directly to real
   `.rdata` strings, 539 of the paired function-slot immediates independently resolved to real `.text`
   function entry points — both counts consistent with one continuous run of `{name,func}` pairs, not a
   coincidental scatter.]**

**Grand total: 1,325 individually-named, compile-time-fixed C engine functions exposed to Lua via the real,
public `lua_register` idiom** (311 in the UI/menu/store cluster + 1,014 in the gameplay/mission/world/AI/
vehicle cluster) — a population an order of magnitude larger than the 155-entry named-hook census (§12), and,
critically, running in the **opposite call direction** from every one of §4/§8/§12's entries: this is Lua
scripts calling *into* the engine by these names, not the engine checking Lua for them.

**Full (non-sampled) name lists, one name per line in original array/call order, moved to `tools/` (a permanent, citable location) from the original per-agent scratchpad copy:**
`tools/lua_gameplay_api_1014.txt` (1,014), `tools/lua_ui_api_311.txt` (311), the flat combined
`tools/lua_all_registered_1325.txt` (1,325), and a machine-reconciliation-friendly tab-separated
`tools/lua_all_registered_1325_tagged.txt` (1,325 lines, `name<TAB>gameplay` or `name<TAB>ui` per line,
gameplay cluster first then UI cluster, matching the two source files exactly) — all read directly from
`.rdata` via a full array walk, not sampled. **Superseded, 2026-09-29, by §13.5's third-cluster finding, itself superseded the same day by §13.6's
fourth-registrar finding: the current, correct, names-only merged list is
`tools/lua_all_registered_1435_tagged.txt`** (the 1,325 above, plus 105 net-new names from a third
registrar §13.5, plus 5 net-new names from a fourth registrar §13.6) — **cite that file to a peer team,
not this one, and not the now-superseded 1,430-line file.**
**This is this project's own disassembly-derived data — citing these files directly to a peer team for
machine reconciliation needs no filtering** (unlike dirty-side runtime-observation sources, see `HANDOFF.md`
§35) — **but never cite a raw per-instruction Ghidra dump (e.g. `tools/lua_game845aa0_full.txt`) directly;
those carry addresses and decompiler artifacts and must stay Team-A-internal. Always build and cite a clean
names-only file instead.**

**Review status (2026-09-30): NEEDS-EXE: 42 wrappers vs 49 calls, the 43-function count, and the 62-site partition — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

**Review status (2026-10-01): partly re-derived from the executable (job `20261001T020218-team-a-bgcx`): the `0x00e0f900` block — CONFIRMED: 24 pairs, names, order, addresses, bare-global binding, registered in both states by `0x00e0e0b0`; the "Lua's own `math` library" framing is corrected (15 of 24 are engine functions); HYPOTHESIS: `0x00fccb70` opens the stock libraries; OPEN: the 20 undumped bodies, and still NEEDS-EXE for the 42/49 wrapper count, the 43-function count and the 62-site partition.**

### 13.3 What this settles

**[CONFIRMED — disassembly.]** This directly answers the peer team's question: yes, a genuinely separate,
classic, public-Lua-C-API-shaped mechanism for exposing engine C functions to Lua by name exists in this
binary, is large, and covers exactly the domain a mission-scripted open-world game would be expected to need
(`waypoint_add`, `vehicle_show`, `ai_attack_region`, `world_despawn_all_pedestrians`, `airplane_fly_to_do`,
`ambient_cop_spawn_enable`, etc.). It is structurally distinct from both mechanisms this document already
confirmed:

- **Not `vint_callback_lua` (§3/§7):** that mechanism's pool objects, hash tables, and 7 Lua-callable
  `object:insert()`/`:remove()`/`:update()` entry points are untouched by anything in this section; this
  section's two clusters live in entirely different code regions (`0x005axxxx`–`0x00bcxxxx`,
  `0x00a20xxx`–`0x00a25xxx`) and use entirely different primitives (`lua_pushcclosure`/`lua_setfield` directly,
  not a hash-keyed object pool). §7's own finding — that `vint_callback_lua` holds Lua-*supplied* callbacks
  with no fixed compiled-in roster — stands unchanged; this section answers a different question the original
  §6 Item 1 conflated with it.
- **Not the named-hook family (§4/§8/§12):** those ~155 entries are the engine optionally *calling into* Lua
  by a well-known name (`_GetAnyGlobalSilent` + `LUA_TFUNCTION` check, §4); this section's 1,325 entries are
  Lua *calling into* the engine — the reverse direction, using the reverse pair of public API calls
  (`lua_pushcclosure`+`lua_setfield` to *expose* a function, vs. a global lookup to *find* one).

**Updates §6 Item 1's status (§11's table) and supersedes its framing at the "is there a fixed roster"
level — not by contradicting §7, but by showing the fixed roster the original open item was looking for was
never going to be found inside `vint_callback_lua` at all; it exists, fully intact, immediately adjacent in
the binary, built with the plain public Lua C API instead of any custom wrapper.**

**Review status (2026-09-30): DESK-PASS — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 13.4 Cross-team reconciliation against the real shipped scripts, and a real, coherent third cluster — CONFIRMED gap, unresolved location

**A peer team independently built a clean-room census of every global function name real shipped mission `.lua` scripts actually call (804 real scripts, 1,282 distinct names by construction — any global a script calls that no script defines is real engine API by construction) and joined it against this section's full 1,325-name registered list.** Both join directions were checked exhaustively:

- **1,093 of the 1,325 registered names are genuinely called by real shipped scripts** — the real, load-bearing implementation-priority subset, with real call/script counts (top three: `trigger_enable` 563 calls/59 scripts, `on_trigger` 513/59, `coop_is_active` 505/45). **[CONFIRMED — empirical, cross-team join.]**
- **232 of the 1,325 are registered but never called by any of the 804 real scripts** — real, but unexercised; lowest implementation priority. **[CONFIRMED — empirical.]**
- **189 names are called by real scripts but do not appear anywhere in this section's 1,325.** Most are already explained by other mechanisms this document documents: 44 are `vint_*` names (§3/§7's own callback-hookup mechanism, a different registration path entirely **[⚠ superseded §13.7: 55 `vint_*` names are ordinary `lua_register` bindings from the registrar `FUN_00e1dfb0`]**), 5 are `thread_*` (a scripting-coroutine primitive family, not yet separately traced but plausibly Lua-side/stdlib-adjacent), and ~18 are genuine Lua 5.1 standard-library functions (out of this document's own stated scope, §1, same exclusion already applied to the `math` cluster). **[⚠ arithmetic: 44 + 5 + ~18 + 75 (below) = ~142 of 189, so ~47 are not accounted for here; part of them is likely the bare-global family noted at §13.2.]**
- **75 names remain a real, unexplained residual — a coherent, single-prefix family (`game_*`), called by real shipped scripts, absent from every registration mechanism this document has found so far.** Real examples: `game_coop_*`, `game_steam_*`, `game_is_connected_to_network`, `game_sign_into_network`, `game_get_platform`, `game_UI_audio_play`, and cross-platform-shaped names (`game_is_pc_dx11`, `game_get_ps3_button_swap`) — a coherent platform/coop/network-bridge API, mostly low call-count/single-script, structurally distinct in both naming convention and apparent purpose from the mission/gameplay (`waypoint_*`/`ai_*`/`vehicle_*`) and UI (menu/HUD/store) clusters §13.2 already documents. **[CONFIRMED — empirical, real names genuinely called by real scripts; RESOLVED 2026-09-29, see §13.5 — the registration mechanism was in fact already inside this section's own 62-site `lua_setfield` census all along, just never separately walked/attributed.]**

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 13.5 RESOLVED — the third cluster is a 113-name loop registrar, `0x00845aa0`, already inside the existing census but never separately walked

**The peer team's own literal example strings pin this down directly.** A targeted `.rdata` string search for six of the peer's concrete example names (`game_is_connected_to_network`, `game_get_platform`, `game_sign_into_network`, `game_is_pc_dx11`, `game_get_ps3_button_swap`, `game_UI_audio_play`) plus a prefix search for `game_coop_` and `game_steam_` found every one of them exactly once in `.rdata`, each with exactly one data (non-instruction) reference — and **all eight land in the same single containing function, `0x00845aa0`.** **[CONFIRMED — disassembly, direct string search and xref, no sampling.]**

`0x00845aa0` (body `0x00845aa0`–`0x00846446`, 2,470 bytes) uses the **identical, already-identified idiom** this section's §13.1/§13.2 established for the other two clusters: it calls the same real `lua_pushcclosure(L, fn, 0)` (`0x00dfe4f0`) immediately followed by the same real `lua_setfield(L, LUA_GLOBALSINDEX, name)` (`0x00dfe830`, index `0xffffd8ee`) in a fixed loop, iterating **113 times** over a 113-entry `{namePtr, funcPtr}` array — structurally the same "build the array on the stack, then loop" idiom §13.2 already documented for the 42 smaller UI wrappers, just at a scale close to the 1,014-entry gameplay table. **[CONFIRMED — disassembly, full function body read, loop trip count and pair stride both read directly from the instruction stream, not inferred.]**

**All 113 name/function-pointer pairs were walked directly** (every `MOV dword ptr [stack+off], imm32` in the function body, in address order, resolved against `.rdata` for a string or against `.text` for a defined function entry): **113 of 113 name slots resolved to a real, printable, identifier-shaped `.rdata` string with zero exceptions**; 64 of 113 function-pointer slots resolved to an already-defined Ghidra function entry point, and the remaining 49 are valid `.text`-range code addresses that simply hadn't been marked as function starts in this project's database yet (not a resolution failure — every slot is a real code address, just not yet boundary-fixed). **[CONFIRMED — disassembly, full array walked, not sampled; raw output preserved at `tools/lua_game845aa0_full.txt`.]** All eight of the peer team's example names above appear verbatim among the 113. **93 of the 113 names carry the exact `game_` prefix** the peer's gap census flagged (`game_audio_*`, `game_peg_*`, `game_hud_*`, `game_award_*`, `game_coop_*` ×~~7~~ 5 **[count corrected: 5 of the 113 carry the exact `game_coop_` prefix; the other coop names are `game_get_coop_*`, `game_set_coop_*`, etc.]**, `game_steam_*` ×4, `game_is_connected_to_*` ×3, `game_sign_into_network`, `game_show_coop_*`, `game_get_coop_*`, `game_set_coop_*`, etc.); the remaining 20 share the same array without the `game_` prefix (`coop_is_active`, `hud_display_create_state`/`commit_state`/`set_element`/`remove_state`, `push_screen`/`pop_screen`, `horde_mode_is_active`, `get_localized_crc_for_tag`/`get_localized_string_for_tag`, `get_char_in_string`/`set_char_in_string`, `tutorial_set_hud_help_closing`, `hud_diversion_remove_callback`, `audio_object_post_event`, `audio_stop`, `vint_set_mouse_cursor`, `autil_mashing_minigame_faded_out`, `city_load_img_load_complete`, and one real, literal doubled-word name as shipped — `get_get_key_names_for_axis_action` — a genuine typo in the game's own compiled data, not a transcription error here).

**Why this was invisible until now, resolving the task's own approach #2 hypothesis exactly as anticipated:** `0x00845aa0`'s single static call site to `lua_setfield` (address `0x0084642d`) was already one of the 62 call sites this section's exhaustive `lua_setfield`-caller census enumerated from the very first pass (§13.1) — it appears in `tools/lua_setfield_callers_full.txt` and `tools/lua_setfield_names_v2.txt` exactly as `0084642d in 00845aa0`. But because the call sits inside a 113-iteration loop, the census's per-call-site backward name-resolution heuristic (a fixed ~20-instruction backward scan, built for the straight-line, one-call-per-name wrappers of the 311-name cluster) only ever reached back far enough to catch whichever name happened to be a few pairs before the loop body — `hud_display_set_element` — and reported that as the site's one name, exactly the way the 1,014-entry gameplay table (`0x00a20840`) would have looked with only one name if it hadn't been given its own dedicated full-array-walk script (`SampleHugeTable.java`/`DumpLuaGameplayFull.java`). `0x00845aa0` never got that dedicated walk in the original pass, so its other 112 names were never added to the 1,325 total or the two named-list files. **[CONFIRMED — the call site's presence in the pre-existing raw census output was verified directly, not re-derived.]**

**Not a third Lua state.** `0x00845aa0`'s only caller is a single call site, `0x00848cdc`, inside `0x008489e0` — the *same* top-level UI/engine bring-up routine §13.2 Item 1 already documents as the one that stores `0x008430f0`'s address (the 42-wrapper/311-name UI cluster's own dispatcher) into a subsystem table **[corrected §16.1(d): passed as a callback to `FUN_00e1e460`, not stored in a table]** and stamps `vint_callback_lua`'s vtable. The two calls sit 33 bytes apart in the same straight-line code (`0x00848cbb` pushes `0x008430f0` and a Lua-state value read from a fixed global right before calling a registration-table-store routine **[OPEN — desk review 2026-09-30: §16.1 says that routine (`FUN_00e1e460`) itself creates the interface state, so a state value read before the call is inconsistent; disassemble `0x00848cb0`–`0x00848ce5` and the prologue of `0x00e1e460`; to be settled against the executable.]**; `0x00848cdc` calls `0x00845aa0` a few instructions later using a Lua-state value obtained from the same accessor routine used elsewhere against that same global). This is real, direct evidence that `0x00845aa0` feeds the same UI Lua state the 311-name cluster does, not a separate third state — the task brief's approach-#3 "separate Lua state" hypothesis is not needed to explain this gap and was not pursued further once this positive was found. **[CONFIRMED — disassembly of the calling function's instruction stream around both call sites.]**

**Overlap check against the existing 1,325 — corrected same day: the original "exactly 2" count below was an undercount, caught by a full programmatic set-intersection rather than a manual spot-check.** ~~Of the 113 names, exactly 2 (`hud_display_set_element`, `coop_is_active`) already appear in the existing tagged list.~~ **The real count is 8**: `hud_display_set_element`, `coop_is_active`, and also `audio_object_post_event`, `audio_stop`, `game_is_active_input_gamepad`, `hud_display_create_state`, `hud_display_commit_state`, `hud_display_remove_state` — all 8 already present in the existing 1,325-entry tagged list. This is either a genuine dual-registration of the same engine-side function into both Lua states, or distinct call sites sharing pooled `.rdata` string literals (MSVC commonly dedupes identical string constants); which of the two was not resolved further and does not affect the main finding.

**Resolved for `audio_object_post_event` specifically, 2026-09-30 (prompted by a peer team hitting a real 1,395-failure runtime gap):** this is a genuine dual-registration, not string-pooling coincidence. The literal string `"audio_object_post_event"` exists exactly once in `.rdata`, but has exactly 2 real code cross-references, both inside registrar function bodies, both writing the SAME function-pointer immediate (`0x00a3cb00`) — one inside the 1,014-entry gameplay registrar (`0x00a20840`), one inside this section's own 113-entry loop registrar (`0x00845aa0`). Independently re-walked both registrars' own single caller each: `0x00a20840`'s sole caller (`0x00a1fa10`) is unambiguously the gameplay-state bring-up path (stores its Lua-state pointer into the confirmed gameplay-state global, `spec-lua-bindings.md` §16); `0x00845aa0`'s sole caller (`0x008489e0`) is unambiguously the UI/interface-state bring-up path (five instructions after the call that creates the interface state via the 311-entry UI dispatcher) — two structurally disjoint call chains, no shared call site. **So the same native function is genuinely reachable from both Lua states, through two separate registration call sites — not one function registered once and miscounted twice.** **[CONFIRMED — disassembly, exhaustive on the single string instance's own 2 xrefs, and on both registrars' own single-caller chains.]**

**Resolved for all remaining 7, 2026-09-30 (prompted by the same peer, generalizing their own gap-fix): all 8 of the 8 overlap names are genuinely dual-registered — zero coincidental `.rdata` string-pooling cases in this set.** `game_is_active_input_gamepad`/`coop_is_active` were each already independently confirmed elsewhere (`spec-lua-api-behaviour.md` §2.6/§3.1) and re-verified directly against both registrars' own already-published full raw array walks (`tools/lua_game845aa0_full.txt`, `tools/scratchpad/lua_gameplay_1014_raw_pairs.txt`) rather than re-derived from scratch. The remaining 5 (`hud_display_set_element`, `audio_stop`, `hud_display_create_state`, `hud_display_commit_state`, `hud_display_remove_state`) were checked fresh, same method: each name's own `{namePtr,funcPtr}` pair in BOTH registrars' full raw array walks resolves to the identical function pointer (`hud_display_set_element`→`0x00a4ed30`, `audio_stop`→`0x00a3d100`, `hud_display_create_state`→`0x00a4f0c0`, `hud_display_commit_state`→`0x00a4ef10`, `hud_display_remove_state`→`0x00a4f230` — all 5 exact matches, both registrars). Cross-checked against 3 failure modes: none of the 5 addresses fall inside either registrar's own body range (ruling out the registrar-array-interior-address mistake `WALLS.md` flags); none appear in the 311-name UI cluster or the two later small registrars (§13.6/§13.7); and all 5 were independently decompiled as real, mutually-distinct, substantial function bodies in unrelated tranche passes (`spec-lua-api-behaviour.md` §8.9, §15.12, §15.24, §15.25, §17.26) whose addresses match the raw-pair reads exactly, with no awareness of this overlap question. **The (d) criterion (two structurally disjoint bring-up chains) is a fixed property of these same two registrar functions, established once above and inherited by every name found in them — not re-derived per name.** **[CONFIRMED — disassembly + raw full-array cross-check for all 8; the two registrars' own caller-chain disjointness re-derived once, not per name.]** Real-world corroboration: the real shipped `vint_lib.lua` (loaded into the UI state, `spec-lua-bindings.md` §16) calls `audio_object_post_event` at its own line 483 inside an unconditional one-line wrapper (`ui_audio_post_event`), with no gating condition — consistent with a peer team's Lua host seeing 1,395 real nil-global failures there if their own registration table only ever attributed this name to the gameplay state (the same "one-name-per-static-lua_setfield-call-site" undercount this project's own original 1,325-name census made before finding this exact loop registrar, §13.5 above). The other **105 of 113** are net-new relative to the 1,325-name census. **Corrected grand total: 1,430 individually-registered, named engine C functions across three registrar call sites** (1,014 gameplay + 311 UI + 105 net-new from `0x00845aa0`, after removing all 8 confirmed overlaps) — superseding both §13.2's "1,325"/"two clusters" framing and this subsection's own first-pass "1,436" figure. There are (at least) **three** master/loop-style registrar call sites feeding the two known Lua states, not two. **[⚠ desk review 2026-09-30: the tagged name files (1430/1435/1490) carry one tag per name, and all 8 dual-registered names above are tagged `gameplay`; a host building per-state registration from the tags must still register these 8 into BOTH states.]** The corrected, machine-verified, names-only merged list (`name<TAB>cluster`, existing 1,325 unchanged, the 105 net-new entries appended tagged `ui`) is `tools/lua_all_registered_1430_tagged.txt` — this is the file to cite to a peer team, not the raw per-instruction dump. Full raw walk preserved at `tools/lua_game845aa0_full.txt` (Team-A-internal evidence, carries addresses, not for direct peer citation); search/xref evidence at `tools/lua_game_api_search.txt`; calling-context disassembly at `tools/lua_848cdc_context.txt`.

**Closed the same day: the peer team independently re-ran their own reconciliation against the corrected, published `tools/lua_all_registered_1430_tagged.txt`** (verified by them directly first — 1,430 lines exact, zero address-shaped content — before use). Real result: 1,181 overlap (up from 1,093), 249 registered-but-never-called, and only **101 called-but-not-registered — the `game_*` gap is now fully closed, zero remaining.** The residual 101 is a materially different shape from the original 75-name gap: mostly already-known `vint_*`/`thread_*`/Lua-5.1-stdlib names (§13.4's own existing explanations; for the `vint_*` part see §13.7) plus a small (~30) low-call-count long tail (`persona_*`, `cell_*`, a handful of `_cb`-suffixed callback-shaped names) that reads as genuine long-tail noise, not a second coherent unexplained cluster. **[CONFIRMED — empirical, cross-team join, both directions exact, re-run against the corrected file.]** **[⚠ desk review 2026-09-30: not re-run for 1,435/1,490 — 41 of these 101 names are in `tools/lua_all_registered_1490_tagged.txt` (checked by set intersection), leaving 60.]** **Superseded same day by §13.6's small fourth-registrar correction — the 5 additional names it finds were not part of this reconciliation and do not change its qualitative conclusion, only the total by 5.**

**Review status (2026-09-30): NEEDS-EXE: state value read before `FUN_00e1e460` creates the state — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 13.6 A fourth registrar found the same day, while checking an unrelated agent's flagged anomaly — 5 more names, corrected total 1,435

**A `spec-lua-api-behaviour.md` agent decompiling `set_mission_author` found its address (`0x007c9f50`) shared with four other named bindings** registered at a call site (`0x00a1fa10`) that didn't obviously match any of this section's three known registrar sites (`0x00a20840`/`0x008430f0`/`0x00845aa0`) — flagged for independent verification rather than acted on, per this project's own standing practice. Verified directly this pass:

**`FUN_00a1fa10` is the gameplay/mission Lua state's own bring-up routine — not a stray call site, but the direct sibling of the already-known 1,014-entry registrar.** Its full body (116 bytes, no arguments, no return value), read via fresh disassembly:

1. Creates the mission/gameplay Lua state itself, by calling `FUN_00e0e0b0` with the state name `"game play"`, flag 1, the address of `DAT_026e9470` and the function address `FUN_00754410`, and storing the result in the state global `DAT_026e7e6c` — the first confirmed sighting of this exact state global's own CREATION, not just its later use (§14.1 already established `DAT_026e7e6c` as the mission/gameplay state by usage, without tracing its origin).
2. Gates on the same co-op ~~head/tail-list-empty check~~ **[corrected: host check, `+0x5c` == `+0x58` — `spec-lua-api-behaviour.md` §6.22 correction and §8.27]** (`FUN_0087ba20`) the peer-cluster work in `spec-lua-api-behaviour.md` (§6.22) already found gating the record-and-replicate idiom's "am I sole/local authority" decision — here gating whether this bring-up runs AT ALL (returns early if not sole authority ~~and the co-op member-list isn't empty~~ **[superseded wording — see the host-check correction above]**).
3. If the new state is non-null, calls, IN ORDER: **`FUN_00a20840`** on the new state — this is exactly the confirmed 1,014-entry gameplay registrar (§13.2) — **then `FUN_00e0ef80`** on the same state — a second, smaller registrar, immediately adjacent to the first, never separately catalogued — **then `FUN_00e0df90`** with the file name `"game_lib.lua"`, the state and the address of `DAT_026e9470` (loads/runs a script file by name — a further, un-investigated mechanism, OPEN **[→ §16.2]**) **then `FUN_00e0d4e0`** on the state (purpose not traced, OPEN **[→ §14.10: the dynamic-globals sandbox bootstrap]**).

**`FUN_00e0ef80` (137 bytes) is a genuine fourth loop-style registrar, using the identical confirmed idiom** (`lua_pushcclosure`/`FUN_00dfe4f0` immediately followed by `lua_setfield`/`FUN_00dfe830`, index `0xffffd8ee` = `LUA_GLOBALSINDEX`) in a fixed 5-iteration loop, registering, into the SAME gameplay state (`DAT_026e7e6c`) `FUN_00a20840` registers into:

| Name | Bound function | Notes |
|---|---|---|
| `script_assert` | `FUN_00e0ef20` | Real, distinct body — see below, NOT the shared stub |
| `script_profiler_start_section` | `FUN_007c9f50` | The same trivial 2-instruction stub `set_mission_author` uses |
| `script_profiler_end_section` | `FUN_007c9f50` | Same stub |
| `script_profiler_do_printout` | `FUN_007c9f50` | Same stub |
| `script_profiler_reset` | `FUN_007c9f50` | Same stub |

**[CONFIRMED — disassembly, both the containing bring-up function and the registrar loop, read directly, not inferred.]**

**`script_assert`'s own body (`FUN_00e0ef20`, 89 bytes) is real, distinct Lua-callable behaviour, not dead code — an assert-with-optional-message shape:** it checks the caller's own `lua_gettop` count; if at least 1 argument was passed, it reads that first argument's Lua type (`lua_type`) and, if non-nil, converts it via `lua_toboolean`; if a second argument is also present, it converts THAT via `lua_tolstring`. The function unconditionally `return`s `0` (no Lua values pushed) regardless of what it read — it consumes a condition and an optional message string but does not itself call `error()` or push any result, so its real behavioural effect (a genuine assert that halts on failure, vs. a log-only/no-op diagnostic) is **OPEN** — not traced further this pass.

**Corrected count and file:** none of these 5 names were in the existing 1,430-name census (checked directly, zero matches) — **corrected grand total: 1,435 individually-registered, named engine C functions across FOUR registrar call sites**, not three (1,014 gameplay + 311 UI + 105 UI-cluster net-new from §13.5 + 5 gameplay-cluster net-new from this section). The corrected file is `tools/lua_all_registered_1435_tagged.txt` (the 1,430-line file plus these 5, tagged `gameplay`) — **this supersedes `tools/lua_all_registered_1430_tagged.txt`; cite the 1435 file to a peer team going forward.** The peer team's own 1,181/101/249 reconciliation (above) is not qualitatively affected — these 5 names are a rounding-error-scale addition, not a new unexplained cluster — but their own total-registered-count figure should be bumped by 5 if they want it to stay exactly in sync.

**Open question this raises, not investigated further this pass:** is `FUN_00e0ef80` really the FOURTH such site, or could an equally-adjacent small registrar sit next to the UI bring-up routine too (i.e., is this pattern — a small loop-style registrar placed immediately after the main per-state registrar call, inside that state's own bring-up function — itself a recurring idiom worth a dedicated whole-binary check)? Flagged as a concrete, bounded next step for whoever picks this up, not claimed as answered. **[→ Answered §13.7: yes — a second such table-driven registrar, `FUN_00e1dfb0`, sits in the UI bring-up.]**

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 13.7 A FIFTH registrar found, same day, while investigating a high-impact runtime error — 55 more names, corrected total 1,490

**Directly answers §3's own long-standing open item ("the actual roster of C functions registered into Lua... not extracted") **[⚠ qualified by this section's own later paragraph: §3's open item is a different question; this registrar is adjacent to it, not an answer]**, and, separately, closes a real, high-impact runtime gap a peer team flagged: a Lua-host run showed `vint_object_find` resolving to `nil` accounting for 43% of all hook-fire errors.**

**`FUN_00e1dfb0` is a genuine 55-entry `lua_register`-idiom batch registrar — the exact same classic mechanism §13.1/§13.2 already documented for the 1,325/1,435-name census, just compiled as an unrolled `{namePtr,funcPtr}` data table plus one loop instead of straight-line calls.** Confirmed by reading its own body directly: for each of 55 pairs, it calls `FUN_00dfe4f0(L, funcPtr, 0)` (`lua_pushcclosure`) then `FUN_00dfe830(L, 0xFFFFD8EE, namePtr)` (`lua_setfield(L, LUA_GLOBALSINDEX, name)` — `0xFFFFD8EE` is Lua 5.1's real `LUA_GLOBALSINDEX`, `-10002`). **This is exactly why the existing census's proximity-heuristic scan missed it**: the loop's one static call site to `lua_setfield` (`0x00e1e445`) IS already present in `tools/lua_setfield_names_v2.txt`, but since the name argument is loop/data-driven rather than a call-site literal, the heuristic grabbed an arbitrary nearby string (`"vint_get_avg_processing_time"`, itself just one of the other 54 real names in the same table) instead of the real per-iteration name — the identical false-attribution failure mode already recorded in `WALLS.md` for the third registrar (§13.5). All 55 names independently confirmed via direct string-dump xref (each individually referenced from `0x00e1dfb0`).

**The caller closes the loop completely:** `FUN_00e1dfb0` has exactly one caller, `FUN_00e1e460` — ~~the same "open custom Lua library" routine §2 already describes~~ **[corrected: the UI-state bring-up helper, §16.1 step 1 — not §2's counted-array loader, which is §16.1 step 4]**: it creates/fetches the `"interface"` Lua state (`DAT_02a45450`), calls this 55-function registrar on it, then ~~immediately~~ **[⚠ not immediately: §16.1 (c) pool init and (d) the 311-name `FUN_008430f0` come in between]** opens `vint_lib.lua` on that same state. **This is a genuinely distinct mechanism from `vint_callback_lua` (§3/§7)** — §3's own open item about "the roster of C functions... via `vint_callback_lua`" is a different question (that mechanism holds Lua-SUPPLIED callbacks dynamically, confirmed to have no fixed roster, §7); this registrar is the classic compile-time `lua_register` idiom applied to a table, landing fixed native functions into the SAME `"interface"`/UI Lua state as the already-known 311-name cluster and the 113-name third registrar (§13.5) — a third contributor to that one state, not a fourth state.

**All 55 registered names** (each independently confirmed via string xref to `0x00e1dfb0`): `vint_debug_sleep`, `vint_datagroup_add_subscription`, `vint_datagroup_remove_subscription`, `vint_datagroup_insert_item`, `vint_datagroup_remove_item`, `vint_dataitem_add_subscription`, `vint_dataitem_find`, `vint_dataitem_get`, `vint_dataitem_set`, `vint_dataresponder_post`, `vint_dataresponder_finished`, `vint_debug_decode_wide_string`, `vint_document_find`, `vint_document_load`, `vint_document_unload`, `vint_document_get_name_from_handle`, `vint_document_get_depth`, `vint_document_set_depth`, `vint_force_lua_gc`, `vint_get_property`, `vint_get_safe_frame`, `vint_get_screen_size`, `vint_get_time_index`, `vint_set_time_index`, `vint_object_rename`, `vint_insert_values_in_string`, `vint_internal_dataresponder_request`, `vint_object_add_child`, `vint_object_clone`, `vint_object_clone_rename`, `vint_object_create`, `vint_object_destroy`, **`vint_object_find`**, `vint_object_first_child`, `vint_object_next_sibling`, `vint_object_parent`, `vint_object_set_parent`, `vint_object_get_name_from_handle`, `vint_set_input_params`, `vint_set_property`, `vint_set_property_typed`, `vint_sound_load`, `vint_sound_play`, `vint_subscribe_to_input_event`, `vint_unsubscribe_to_input_event`, `vint_subscribe_to_raw_input`, `vint_unsubscribe_to_raw_input`, `vint_subscribe_to_mouse_input`, `vint_unsubscribe_to_mouse_input`, `vint_apply_start_values`, `vint_get_avg_processing_time`, `vint_get_global_anchor`, `vint_is_std_res`, `vint_force_mouse_move_event`, `vint_get_current_clickable_element`. **Given `vdo_base_object.lua` (the base class every UI widget derives from) calls several of these directly (confirmed by reading the real shipped source — see §15), the object-lifecycle names (`vint_object_create`/`destroy`/`first_child`/`next_sibling`/`get_name_from_handle`/`parent`/`set_parent`/`add_child`) are near-certain to be similarly high-frequency error sources if a Lua host is missing this whole family, not just `vint_object_find` alone.**

**Corrected count and file: none of the 55 names overlap the existing 1,435-name census (checked directly, zero matches) — corrected grand total: 1,490 individually-registered, named engine C functions across FIVE registrar call sites.** Corrected file: `tools/lua_all_registered_1490_tagged.txt` (the 1,435-line file plus these 55, tagged `ui`) — **supersedes `tools/lua_all_registered_1435_tagged.txt`; cite the 1490 file to a peer team going forward.** This also directly confirms §13.6's own closing question ("is this pattern — a small loop-style registrar next to a state's bring-up routine — a recurring idiom worth a whole-binary check?") — yes: this is now the SECOND such table-driven batch registrar found by chasing an unrelated lead (§13.5's 113-entry one, and now this 55-entry one), which itself suggests a **direct, systematic whole-binary scan for this exact idiom** (an unrolled `{namePtr,funcPtr}` stack table feeding one `lua_pushcclosure`+`lua_setfield` loop) is a well-motivated next step, rather than continuing to find these only incidentally.

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

## 14. Hook argument contracts — the dynamic-globals pair, mission start/end, a per-frame candidate, and a real correction to part of §12.3's census (2026-09-29)

**Scope.** Team B's live engine→script hook driver needed real argument contracts (count/type/order) for the highest-value named hooks, not the 0-args placeholder it was built with. This pass targeted §12.4's dynamic-globals pair, the question of whether a dedicated mission-start/mission-end hook exists, a per-frame/update candidate among the named family, and boot/mission-start firing order.

### 14.1 The dynamic-globals lifecycle pair (§12.4) — full argument contracts, CONFIRMED

Both hooks fire from `FUN_00e0de80`, confirmed at its 2 real `CALL` sites (`FUN_006cfac0` @ `0x006cfb58`; `FUN_00e213d0` @ `0x00e214f2`):

- **`_PrepareForDynamicGlobals`** — fires FIRST, before the script chunk loads. **1 argument: a STRING** — the canonicalized script filename (built by `FUN_00e0cff0`, which force-appends a `.lua` suffix if missing). `nargs=1, nresults=0`.
- **`_DynamicGlobalsLoadComplete`** — fires AFTER, only when the chunk load/run did NOT return a Lua 5.1 error status (the code checks the result against the real Lua 5.1 status codes `2/3/4/5` = `LUA_ERRRUN`/`ERRSYNTAX`/`ERRMEM`/`ERRERR`, skipping `0`=OK/`1`=YIELD). **0 arguments.**
- **Neither call is preceded by a `_GetAnyGlobalSilent` existence check** — both are unconditional "select-by-name, push args, fire," confirming §12.4's own original observation.
- **Dual-state infrastructure, not mission-exclusive:** fired from the mission-script path (`FUN_006cfac0`), the Lua state used is the global `DAT_026e7e6c` — independently cross-confirmed as the mission/gameplay state (the same global used explicitly by the confirmed `m24_killbane_on_death`/`m24_killbane_on_undowned` call sites, §14.3). Fired from `FUN_00e213d0` (a UI-document-tree walker, see §14.4), the state used is `FUN_00e1a1b0()`'s return — the general UI Lua state, `DAT_02a45450`.
- **Correction to how the mission path is described:** `FUN_006cfac0`'s only reference from the master resource-type table `FUN_00700780` is a DATA reference (registration), not a CALL — the real runtime trigger that instantiates a "Mission LUA script" resource (presumably a mission package streaming in) was not traced this pass. **OPEN.** **[→ Largely resolved §14.5: the trigger is the world-streaming grid loading a mission's own `_modal` container; which specific container streams remains open.]**

**[CONFIRMED — disassembly, both call sites, both hooks' full argument shape and firing condition.]**

**Review status (2026-09-30): DESK-PASS — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 14.2 Mission start / mission end — no dedicated named hook exists

Re-read §12.3's 73-entry family and §12.6's 58-entry mission-numbered census in full: **no hook literally named `mission_start`/`mission_end` or equivalent exists in either.** The best real "mission start" signal is §14.1's dynamic-globals pair, fired via the Mission LUA script resource constructor. **No confirmed "mission end" signal was found.** Team B's driver should not expect a dedicated mission-start/end callback among the named-hook family; if it needs one, the dynamic-globals pair (with the OPEN caveat on its own real trigger, §14.1) is the closest real mechanism on record.

**Review status (2026-09-30): DESK-PASS — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 14.3 CONFIRMED (independently re-verified twice) — 5 names carried into §12.3's census are NOT Lua hooks at all

Checking 5 "mission end" candidates carried into §12.3 from §4's original loose automated scan (not re-derived by §8.1/§8.2's stricter exhaustive method) found they are **not Lua-hook-dispatcher calls at all**:

- **`cmp_mission_success`** — an unbounded code block at `0x007c0700`–`0x007c0770` (real, live code, no Ghidra Function wrapper — confirmed by raw disassembly, not a registrar-array-interior artifact) pushes the literal string, calls `FUN_00e1e7e0`, and on success stores `FUN_007bdeb0`'s own address into the returned pointer's `+0x2a0` field. No call to the Lua dispatcher anywhere in the block.
- **`cmp_fail_populate`** — real bounded function `FUN_007c0840` (103 bytes, 1 caller): calls `FUN_00e1e7e0("cmp_fail_populate")`, stores `FUN_007bf1e0`'s address into `+0x2a0`.
- **`cmp_activity_success`** — same unbounded-block pattern as the first, at `0x007c07c0`–`0x007c0837`: stores `FUN_007be600`'s address into `+0x2a0`.
- **`garage_populate`** — real bounded function `FUN_005fb9b0` (233 bytes): calls `FUN_00e1e7e0("garage_populate")`, stores a fourth native pointer, `FUN_005fb000`, into `+0x2a0` — same mechanism, different callback.
- **`garage_performance_stats`** — real bounded function `FUN_005f6920` calls the third, distinct name-registry function `FUN_00e25040("garage_performance_stats")` and stores its own `+0x14` field (not `+0x2a0`) via a further helper trio (`FUN_00e25c40`/`FUN_00e26040`/`FUN_00e24930`) — a UI-population idiom, still not a Lua call.

**Cross-checked directly against the confirmed dispatcher for contrast:** `FUN_00e0cef0` itself (the real dispatcher entry, §8.1) calls the literal Lua C-API name `_GetAnyGlobalSilent` and checks `lua_type()==6` — a completely different code shape from `FUN_00e1e7e0`/`FUN_00e25040`, which are ordinary circular-linked-list registry insert/remove routines with zero calls to `_GetAnyGlobalSilent` or the dispatcher trio, and no Lua-shaped calling convention at all. `FUN_007bf1e0`/`FUN_007bdeb0`/`FUN_007be600`/`FUN_005fb000` are all real, substantial, coherently-decompiling native functions built from the same UI-field-population helper family (`FUN_00e1e940`/`FUN_00e1e990`/`FUN_00e24930`/`FUN_00e22a30`) — never the Lua argument-push helpers. WALLS.md's "registrar-array entry vs. real call" and coincidental-offset traps were both explicitly checked against and ruled out.

By contrast, two names from the same "completion screens" category that WERE derived via §8's exhaustive method (not carried from §4) genuinely do go through the real Lua dispatcher, with real confirmed arg contracts: **`cmp_common_screen_start`** — 1 argument, a DOUBLE, value = global `DAT_02282958` (real-world meaning not traced, OPEN) — and **`completion_coop_disconnected`** — 0 arguments. `m24_killbane_on_death`/`m24_killbane_on_undowned` also confirmed genuine, 0 arguments each, on the mission/gameplay state.

**[CONFIRMED — disassembly, independently re-derived twice from scratch (fresh string search, fresh Ghidra project copy, raw instruction-level disassembly for every unbounded-block site) — the second pass found no refutation and additionally confirmed the exact native pointer for `garage_populate` was a fourth address, `FUN_005fb000`, not one of the three originally spot-checked.]** Team B's live hook driver should not fire any of these 5 names as Lua hooks, with any argument count — there is no Lua call to make for them. **A stale plate comment on `FUN_00e0cef0` in the master Ghidra project asserting the OLD, wrong link has been corrected (2026-09-29); a matching stale line in `HANDOFF.md`'s own key-addresses reference has also been fixed.** This pattern (7 samples checked total **[⚠ 5 refuted + 2 completion hooks = 7; with the m24 pair confirmed in the paragraph above, 9 names were checked]**) has NOT been extrapolated to the rest of §12.3's 73-entry or §12.6's 58-entry census — a systematic re-audit of the remaining names remains a real, bounded next step, not attempted here.

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 14.4 Per-frame/update candidate: `hud_running_man_event_update`

Confirmed via the real dispatcher at 7 call sites across 5 containing functions, all through the confirmed float-push helper `FUN_00e0ce20`. Arity is call-site-dependent: most sites push **1 float** (a per-site value — a global, or a caller's own parameter cast to float — no single unified meaning). One site (`FUN_0063c300`) pushes a **2nd float conditionally**, when an event-type code passed by the caller equals `0x16` (22) — i.e. this hook's real arity is 1 or 2 depending on that code. One containing function, `FUN_00642390`, is reached only via a DATA reference (`0x0112ceb0`) consistent with a vtable slot — i.e. a virtual per-object "Update" method, not a one-shot event callback. **HIGH CONFIDENCE, not fully confirmed, that this fires every game-logic tick for active "running man" minigame instances** — the actual top-level per-frame caller of that vtable slot was not traced this pass. **OPEN.**

**Review status (2026-09-30): DESK-PASS — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 14.5 Boot / mission-start firing order — real caller-chain traces for both paths, two genuine walls found (both matching this project's own existing precedent), relative order inferred not proven

**Direct follow-up, same day: both firing paths' own top-level triggers traced as far as real disassembly allows.**

**The UI-document path (`FUN_007b0ca0` → `FUN_00e213d0`, §14.1) traces to a container-load dedup mechanism, not a boot-only event.** Its real caller chain (`FUN_007b66a0`/`LAB_007b6700` → `FUN_007b1f60` → `FUN_007b0ca0` → `FUN_00e213d0` → the dynamic-globals pair) bottoms out in the SAME callback field (row `+0x44`) two adjacent container kinds share: kind `21` ("UI", byte-exact name string) via `LAB_007b6700`, and kind `22` ("UI peg", byte-exact name string) via `FUN_007b66a0` — the identical field shape `spec-asm-format.md` §6.1 already documents for the "destub" kind's own per-kind callback. Both callback bodies resolve a second object, read a status field, and compare it against ONE shared global (`0x02247bfc`) — a "has the currently-active thing changed since I last checked" dedup, not a one-shot "container just finished loading" notification. **Genuine wall, matching §6.1's own already-flagged gap exactly (not a new failure): no reader/invoker of this callback field exists anywhere in the binary** — the three other functions that touch this row array all read different fields (backend/pool pointers), never this one. **HIGH CONFIDENCE, not disassembly-proven: this fires whenever any UI document loads (which structurally includes the main menu at boot, but is not boot-exclusive), since kind 21 alone has 727 real Vint-LUA-script entries across 183 shipped containers** — i.e. this is general UI-document-load infrastructure, not a distinguished boot path.

**The mission-LUA path (`FUN_006cfac0`, §14.1) is reframed: its "no CALL xref" gap is not special — it matches this project's own already-documented pattern for ALL 43 registered resource types** (`spec-resource-dispatch.md`: every type's constructor is invoked only indirectly, through the registration row's own ctor-pointer field inside the generic dispatcher `FUN_00dd2e30`). **New population-empirical finding closing most of the "what triggers this" question: 79 real type-32 (`"Mission LUA script"`) entries exist across all shipped `.asm_pc` manifests** (76 under container kind `39`, "mission model data"; 3 under kind `40`, "large mission model data"), every one a `<mission-code>.lua` file inside a `<mission-code>_modal` container in `stream_grid.asm_pc` **[⚠ desk review 2026-09-30: per Team B's `team-b/tools/mission_package_per_container.tsv` (79 rows): 55 in `stream_grid.asm_pc`, 12 in the DLC `*_stream_grid.asm_pc` manifests and 12 in the DLC `*_sr3_city.asm_pc` manifests]** — the SAME manifest family that streams open-world city geometry by grid coordinate (already flagged in `spec-mission-packages.md` §5 as "a large, distinct, currently-undocumented system," without previously knowing it also carries mission Lua scripts). `m24` is among the 79, cross-confirming against §14.3's own independently-traced `m24_killbane_*` hooks. Type `27` ("Vint LUA script") has 727 entries, ALL under kind `21` — the two Lua resource types are cleanly, exhaustively separated by container kind. **This upgrades "presumably a mission package streaming in" (~~§12.5~~ §14.1) from a guess to a population-confirmed structural fact: the real trigger is the world-streaming grid loading a mission's own `_modal` container**, via the same already-fully-characterized generic dispatcher, not a separate undiscovered mechanism. **Genuine remaining wall: the exact trigger that decides to stream in one SPECIFIC named `_modal` container was not traced** — that lives in the world/zone-streaming request-issuing code (`FUN_00db1fd0`, `spec-asm-format.md` §10.2 item 1; `FUN_005d58f0`), both already flagged project-wide as deliberately-unopened doors, not new gaps.

**Relative order: no direct trace links the two paths — they are structurally independent** (disjoint Lua states — UI on `DAT_02a45450`, mission on `DAT_026e7e6c` — and disjoint container-kind namespaces, `21`/`22` vs `39`/`40`), sharing only the native firing function `FUN_00e0de80` and the two hook names, never a caller. **HIGH CONFIDENCE, inferred from game structure rather than a shared trace: the UI-document path fires at least once (via the main menu) before any mission-LUA-script instance can fire, since the mission path's own gate requires a "mission active" flag that cannot be true before the player has passed through menus depending on the first path.** **[⚠ the "mission active" reading of this gate is superseded (§12.5: it is the co-op/network host check), so this inference needs re-arguing.]** **[OPEN — desk review 2026-09-30: the relative order of the two paths; to be settled against the executable.]** Re-checked `spec-save-format.md`/`spec-format-inventory.md` again specifically for a documented boot/mission-start sequence — neither has one, confirming this pass's own prior finding; the only adjacent anchor anywhere in this project (`spec-tables-progression.md` §14.2's "boot mounts `misc_tables.vpp_pc`" finding) is an unrelated subsystem (xtbl table mounting), not connected to either path.

**[CONFIRMED — disassembly, both caller chains and the population-empirical type-32/kind-39/40 finding; the two remaining walls (the UI-callback's real invoker, and the specific-container streaming trigger) are genuine, matching this project's own existing precedent, not newly-discovered gaps; the relative-order claim is HIGH CONFIDENCE inference, not a proven shared trace.]**

**Review status (2026-09-30): NEEDS-EXE: the firing-order inference rests on the superseded 'mission active' gate reading — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 14.6 No mission-name-suffixed sprintf hook family exists — exhaustive checked negative, plus 5 new confirmed hook names found along the way

**Direct follow-up on a peer lead: is there a sibling of §8.4's UI-widget sprintf-templated hook family where `%s` is a MISSION name instead?** Checked two independent, complementary ways, both clean negatives:

- **Every caller of the confirmed sprintf-equivalent `FUN_00da78d0`** (the function all 7 of §8.4's own known cases use) — 94 distinct functions binary-wide, a general-purpose formatter (xtbl filenames, save filenames, etc.), not Lua-specific. Every one of its ~140 format-string call sites was grepped for a literal combining `_start`/`_end`/`_success`/`_failure`/`_complete`/`_begin` with `%s`. One candidate, `"activity_start_%s_%s"` (`FUN_0061c6e0`, a diversion/activity xtbl loader), was traced and REFUTED — its built buffer goes to `FUN_005982e0`, an object/spawn-name lookup, never the Lua dispatcher. Cross-checked which of the 94 functions also call the dispatcher chain (`FUN_00e0cef0`/`ca80`/`cd00`): only the same 7 §8.4 already documents.
- **Every direct call site to the dispatcher itself**, re-derived fresh (65/195/175 raw refs to `FUN_00e0cef0`/`ca80`/`cd00` respectively), specifically the 15 "orphan" sites never previously resolved to a containing Function. None involve a sprintf call: 10 push a plain compile-time literal, 5 push a raw per-instance struct field (`this+0x140`, see below), 2 dereference global pointer slots with zero writers found anywhere (a checked-negative sibling of §8.7's Bink finding, unrelated to missions), 1 goes through a generic string-resolve helper unrelated to missions. **[OPEN — desk review 2026-09-30: 10 + 5 + 2 + 1 = 18, not 15; and §16.2's `FUN_00a1fa90` fires `<name>_init`/`<name>_main` through the dispatcher, which this sweep (searching only `_start`/`_end`/`_success`/`_failure`/`_complete`/`_begin`) would not have caught — it may be one of the 7 or an eighth; to be settled against the executable.]**

**[CONFIRMED — exhaustive, both methods; the sprintf-templated hook family is UI-widget-only exactly as §8.4 already states; no mission-name sibling exists anywhere reachable from the confirmed dispatcher.]**

**§12.6's 58 mission-numbered names are confirmed literal, not runtime-assembled, for all 58 (not a sample) — re-checked directly.** The existing census's own scan required the entire null-terminated printable run at each hit to match `^m[0-9]{1,3}_[a-z_0-9]{2,60}$`, anchored both ends — this structurally rules out sprintf assembly (a template fragment would contain `%s`/`%d`, breaking the match; a sprintf *result* only ever exists in a runtime stack buffer, never a static `.rdata` byte run). Scanned all 60 names (58 plus the 2 already-dispatcher-confirmed) for a systematic `m<N>_start`/`m<N>_end`-shaped pair — none exists; a few names contain lifecycle-sounding words as part of a bespoke one-off event (`m16_qte_complete`, `m21_killswitch_qte_complete_cb`) but have no corresponding `_start` counterpart. **§14.2's "no dedicated mission_start/mission_end hook exists" is independently re-confirmed, on firmer footing.**

**§8.6/§8.7 re-checked directly — NOT stale, already fully covers this.** Fresh re-derivation of `FUN_00843310`'s own callers (ignoring the existing spec text until after) found the identical population §8.7 already documents: 3 raw references, 2 containing functions, both inside the same Bink-movie-playback address range, zero orphans. A peer's "unchased" framing was inaccurate for this specific item.

**5 new confirmed literal hook names, found as a side effect while chasing the dispatcher's 15 orphan call sites (verified absent from this document by direct grep):** `store_weapon_uncover_weapon` (pairs with the already-known `store_weapon_cover_weapon`), `store_gallery_upload_complete`, `store_gallery_download_list_complete`, `screen_capture_open_preview_dialog`, `dialog_build` (fired from the modal-dialog/popup pool's own cleanup path, see below). The remaining orphan literals were duplicates of already-known names reached via a second, previously-unbounded code path.

**A real, previously-undocumented raw-buffer hook-name family — structurally parallel to §8.7's Bink pattern, on a different subsystem, and explicitly NOT mission/activity-related despite an initial false lead.** A small pooled modal-dialog/popup manager (fixed 4-slot pool, `DAT_02282d40`, stride `0x184`, linked via `DAT_02282d10`/`14`/`18`) whose cleanup path fires the literal `"dialog_build"` hook; its 62 callers of the notify primitive `FUN_007c31b0` (and siblings `FUN_00...34b0`/`37a0`/`3f40`/`54d0`) are DLC-install dialogs, community/login error dialogs, and co-op "MISSION_PARTNER_DECLINED/BUSY" popups — generic UI-dialog plumbing, not a mission/activity lifecycle system (an initial hypothesis tying this to the diversion/activity xtbl loader above was checked and refuted). Each record's `+0x140` field is a raw, non-sprintf per-instance Lua callback name; no writer of that field was found in the functions traced. **OPEN — the setter is unlocated, but the domain (UI dialogs, not missions) is settled; do not re-chase the "activity descriptor" hypothesis.**

**Review status (2026-09-30): NEEDS-EXE: orphan-site count 15 vs 18, and §16.2's `_init`/`_main` firing — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 14.7 The strcat/strcpy-concatenation lead for `<stem>_start`/`_run`/`_success` — exhaustive EXE-side negative, with an important reframing of where the real mechanism must live

**Direct follow-up on a peer lead: since §14.6 ruled out sprintf specifically, could a mission name be assembled via `strcpy`/`strcat`-style concatenation (not sprintf) and passed to `lua_getglobal`/`lua_getfield(LUA_GLOBALSINDEX, ...)`?** Checked five independent, complementary ways, all clean negatives, for the exact literals `"_start"`, `"_run"`, `"_success"`:

1. Ghidra-defined-string exact match across every defined string in the image: 0/0/0.
2. Raw byte-level scan (not relying on Ghidra's auto-detection) for the standalone null-terminated sequences, requiring the preceding byte also be NUL (i.e. genuinely standalone, not a tail-substring of a longer literal): 0/0/0. Cross-validated against a positive control (unbounded substring scan): 172 raw occurrences found, confirming the method works — but every one is embedded inside a longer, unrelated, already-complete literal (`scripted_mission_start`, `cmp_mission_success`, `activity_start_%s_%s`, etc. — several of which are C++ RTTI class names for an unrelated action-node/entity-type system, not Lua hooks).
3. Mid-string pointer-reuse (does anything hold a reference pointing directly at a suffix's start byte inside one of those 172 longer literals, reusing its tail as a de-facto standalone constant): 0/172.
4. Inlined byte-copy/split-immediate construction (the raw 4-byte ASCII chunks `"_sta"`/`"_run"`/`"_suc"`/`"cess"`/`"art\0"` a compiler would need across two `MOV`-immediate instructions to build these strings without a single contiguous literal): 0 occurrences anywhere in `.text`.
5. Without the leading underscore (`"start"`/`"run"`/`"success"` alone, in case the underscore were inserted separately): exactly 1 standalone hit each, all traced and unrelated — an `.xtbl`-schema field name and two AI/animation debug-state-name arrays.

**[CONFIRMED — exhaustive, five independent methods; no strcat/strcpy source operand for these three exact suffixes exists anywhere in the executable, so nothing can feed such a concatenation into the dispatcher chain or a `lua_getfield(LUA_GLOBALSINDEX,...)`-style lookup from the EXE side.]**

**Direct follow-up, same day: `mission_checkpoints.xtbl`'s real consumer traced end-to-end (every hop's caller population fully enumerated, not sampled — populations of 1 at every step) — a genuine negative for the mission-dispatch question, from a different direction.** The table (`FUN_006DF8B0`, `spec-tables-progression.md` §10.4, loaded only when the world name starts with `sr3_city`) has exactly ONE consumer in the whole binary, a bsearch-by-mission-name range lookup (`FUN_006df920`), with exactly one caller (`FUN_006df9e0`) that only does anything when its own checkpoint-name argument is the literal `"mission start"` — it looks up that mission's checkpoint rows for one whose `CheckpointName` also equals `"mission start"` and returns its authored `Index`. That function's only caller (`FUN_00bd9c10`) is telemetry-event posting, gated by debug/telemetry flags, called only from the real checkpoint-hit handler (`FUN_006d5060`, which also fires a HUD toast and co-op replication). **`FUN_006d5060` is called from `FUN_00a52600` — the confirmed native handler for the Lua-callable global `mission_set_checkpoint`** (verified two ways: the registrar's own `{namePtr,funcPtr}` pair at `0x00a2340c`/`0xa52600`, and `FUN_00a52600`'s own body reading Lua arguments through the confirmed `lua_gettop`/arg-read helper family before forwarding). **Direction is Lua calling OUT to the engine (a mission script reporting a checkpoint hit via `mission_set_checkpoint`), the OPPOSITE of the engine-calls-INTO-Lua direction the `_start`/`_run`/`_success` question is about.** `MissionName` here is used only as a lookup key against the CURRENTLY RUNNING mission's own name, solely to test for a `"mission start"` sentinel checkpoint row for a telemetry payload — checkpoint/analytics bookkeeping, not a mission-entry mechanism. **[CONFIRMED — exhaustive trace, does not explain the real 35/54 `MissionName`↔`_start`-stem correlation a peer team found; that correlation's real consumer remains unfound.]** One loose end, not chased further since it converges on the same already-traced handler: a second thin forwarder to `FUN_006d5060`, `FUN_00a52694`, has zero inbound references found in this pass (possibly an unwalked registrar slot, or dead code).

**Narrowing from the script side, same day (peer team, real evidence): `<stem>_start` is called directly, natively, not built at runtime.** A full call-site census of all 804 real scripts' own 497 `<stem>_start`/`_run`/`_success`/`_init`/`_cleanup` definitions found: 441/497 (88.7%) are never called anywhere in shipped Lua source at all; of the 48 that ARE Lua-called, EVERY mission's `<stem>_run` is called from that SAME mission's own `<stem>_start` (3/3 spot-checked directly, 0 exceptions — e.g. `m01_start(m01_checkpoint, is_restart)` calls `m01_run(m01_checkpoint)` internally). **`_start` itself is essentially never called from Lua anywhere (0/54 real mission-shaped `_start` names appear as a Lua call site) — so whatever calls it is native, consistent with this section's own §14.6/§14.7 sprintf/strcat negatives, and is most likely either a literal per-mission call site or a table-resolved name, not a runtime-built string.** This narrows the remaining search specifically to a direct or table-driven native call into `<stem>_start` (2 arguments: `checkpoint`, `is_restart`) — not a generic template mechanism. **Update: the concrete next step this paragraph originally proposed (a literal-string search for complete `<mission-stem>_start` strings) was carried out the same day — see §14.9, also a clean negative.**

**Important reframing, not itself resolved this pass: the peer's original 63/51/50-of-804 figures come from the shipped `.lua` script files themselves, a completely different data source than the executable.** If a script builds its own call target via Lua's `..` operator (e.g. `SCRIPT_NAME.."_start"`), that concatenation runs inside the Lua VM at runtime using string constants stored in the `.lua` file's own source/bytecode — never in the EXE's `.rdata`/`.text`. A Ghidra search of the executable is structurally blind to that case by construction, however exhaustive. **However, a peer team's own separate, independent check (2026-09-29) already found 0/804 real scripts contain `_G[...]`, `loadstring`, `getfenv`/`setfenv`, OR a quoted `"_start"`/`"_run"`/`"_success"` string literal anywhere** — meaning the Lua-VM-side concatenation hypothesis is ALSO ruled out, by the same exhaustive standard, on the script side. **Net effect: both plausible concatenation mechanisms (EXE-side and Lua-VM-side) are now exhaustively excluded. How `<stem>_start`/`_run`/`_success` functions are ever actually invoked remains genuinely OPEN** — real possibilities include: these functions are dead/unreferenced code from an earlier design; they are invoked through a numeric/hash-based dispatch rather than a name-string mechanism (not yet searched for); or `mission_checkpoints.xtbl`'s own `MissionName` field (a peer finding, real data confirmed) is consumed by native engine code that builds the call target through some construction this pass's specific literal/byte-pattern search didn't anticipate — tracing that table's real consumer is the concrete next step, not yet attempted as of this entry. **[→ Done: see the `mission_checkpoints.xtbl` consumer paragraph above — a negative.]**

**Review status (2026-09-30): DESK-PASS — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 14.8 A fourth exhaustive negative: no two-part `"%s_%s"`/`"%s%s"` format combining a mission stem with a bare-suffix lookup table exists either

**Direct follow-up on a peer lead: could a two-part format string (`"%s_%s"`/`"%s%s"`) combine a mission stem with a bare suffix word (`"start"`/`"run"`/`"success"`, no leading underscore, pulled from a small lookup table) — producing the exact final string without ever containing the literal `"_start"` substring §14.7 searched for?** Checked exhaustively, a clean negative:

- **Exactly one standalone `"%s_%s"` and one standalone `"%s%s"` exist in the whole binary** (both `.rdata`). Every one of their 46 combined call sites (plus 6 more recovered by manual disassembly where no Function boundary existed) was decompiled and read. Every site falls into an already-explained bucket — per-zone/per-level `.xtbl` override filenames (12+ callers, each its own hardcoded literal suffix, never a shared array), DLC/mission-table filenames (`"missions"` baked into the format string itself, not supplied via `%s`), cutscene/streaming container filenames, generic object naming, localization/customization filenames, standard libcurl HTTP format strings, and one Lua-chunk-name/error-string builder next to the confirmed dispatcher block (`FUN_00e0f010`, builds `"%s%s"` from a Lua string value plus the fixed literal `".lua"` — not a suffix table) **[⚠ corrected §16.3: `FUN_00e0f010` is a Lua-callable "mark library opened" primitive, not a chunk-name builder]**. **None passes an entry from a shared small array of alternative suffixes as either `%s` argument; none is mission-name-shaped.** **[OPEN — desk review 2026-09-30: §16.2 later found `FUN_00a1fa90` building `"%s%s"` + `".lua"` from a mission/level stem (the decompiler dropped the register argument), which conflicts with "none is mission-name-shaped"; the `".lua"` site attributed here to `FUN_00e0f010` may really be `FUN_00a1fa90`; to be settled against the executable.]**
- **A general sweep of the entirety of `.rdata`/`.data` (38.8 MB combined) for any run of 3–10 consecutive pointers to short bare lowercase-alpha strings** (the general shape a "start"/"run"/"success"-style suffix table would have) found 61 qualifying runs — all explainable as material/state/UI-category enums or genuine Lua 5.1 VM-internal tables (reserved keywords, `collectgarbage` options, `coroutine.status()` names). None resembles a lifecycle-suffix set, and none overlaps with the `%s_%s`/`%s%s` call sites above.
- **§14.7's own three bare-word hits (`"start"`/`"run"`/`"success"` without underscore) re-verified to still hold exactly as documented**, and confirmed to have zero overlap with this pass's call-site list.
- **One inert coincidence flagged so it isn't rediscovered and mistaken for a live table:** a 4-slot pointer run `[body, head, eyes, start]` at `0x01300ce4` happens to include the same `"start"` string's address in its 4th slot — but has ZERO Ghidra-detected code references anywhere, to either the array base or that slot. Dead/inert, not a functioning lifecycle table.

**Review status (2026-09-30): NEEDS-EXE: whether §16.2's `"%s%s"` mission-stem site is among the 46 call sites — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 14.9 A fifth exhaustive negative closes off pre-baked literals too — the real mechanism most likely isn't name-based at all

**Direct follow-up, same day: does a complete, pre-baked literal string like `"m01_start"` exist anywhere in the executable, for any real mission stem, as its own standalone string (not built from parts)?** Using the real, runtime-loaded `mission_checkpoints.xtbl` data itself (the 166-row patch copy, `spec-tables-progression.md` §10.4) to get the authoritative stem list — `m01`–`m24`, `sh01`–`sh04`, `mm_p_03`, `dlc2_m01`/`m02`/`m03`, `dlc3_m01`/`m02`/`m03` = 35 real stems, cross-checked against §12.6's own mission-numbered census as complete, not a sample — a raw byte-level scan for `"<stem>_start"` as a standalone NUL-bounded string found **0/35 hits**. A stronger companion scan removed the NUL-boundary requirement entirely (unbounded substring, anywhere in memory at all, validated against a known-present positive control to confirm the method works): **still 0/35.**

**This closes off the third and last plausible string-construction mechanism.** Combined with §14.7 (no EXE-side strcat/strcpy concatenation, 0/0/0 across five methods) and the peer-confirmed Lua-VM-side negative (0/804 scripts use `..`-style dynamic name-building or contain a quoted `"_start"` literal): a pre-baked literal, a runtime concatenation, and a Lua-side concatenation are now ALL exhaustively ruled out. Since `lua_getglobal(L, "m01_start")`-style lookup fundamentally requires those exact ASCII bytes to exist somewhere at call time — as a literal or as something built at runtime — and every construction mechanism this project can conceive of is excluded, **the most defensible remaining explanation is that the real invocation is not name-string-based at all.** **[CONFIRMED — exhaustive, for all 3 string-construction hypotheses across all 35 real stems.]**

**Two live candidates for what it actually is, neither confirmed, both genuinely worth a future pass:** (a) a numeric/hash-based dispatch that bypasses string names entirely (not yet searched for); (b) the mission-script loader captures a direct reference to the `<stem>_start` function VALUE at chunk-load time — e.g. via the loaded chunk's own return value, or a per-mission slot in the still-open, runtime-populated Group 5 data tables §12.7 already flagged (`DAT_026e83f0`/`DAT_02319570`, both confirmed entirely null in the static image, §8.5) **[⚠ `DAT_02319570` no longer qualifies: it is `game_peg_load_with_cb`'s request-slot table, `spec-lua-api-behaviour.md` §8.24]** — meaning the native side never needs to look the function up by name at all. **OPEN, genuinely hard, recorded as such rather than as a solved mechanism.**

**Review status (2026-09-30): DESK-PASS — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 14.10 A sixth exhaustive negative — no runtime-generated Lua source text (sprintf into `luaL_loadbuffer`/`loadstring`/`lua_load`) either; parking this thread. Bonus real finding: the dynamic-globals sandbox bootstrap mechanism

**Direct follow-up, same day: could the engine generate a small piece of Lua SOURCE TEXT at runtime (e.g. `sprintf(buf, "%s_start(%d, %s)", stem, checkpoint, restart_str)`) and execute it directly via `luaL_loadbuffer`/`luaL_loadstring`/`lua_load` — a mechanism none of §14.6–§14.9 would catch, since all four assumed the `lua_getglobal`+`lua_pcall` calling shape?** Checked exhaustively, a clean negative:

- **Part (a):** re-examined every one of the 172 raw `_start`/`_run`/`_success` substring occurrences already catalogued (§14.7) against a code-template shape test (parens, commas, `%d`, etc.) — 0/172 contain any parenthesis, comma, or `%d`. The 3 near-misses are all already-explained or newly resolved as non-code: `"activity_start_%s_%s"` (§14.6, an object/spawn-name lookup, not a load call), a newly-traced `"mission_start %s"` (built by `FUN_0083bc10`, fed to a telemetry/event-name sink, `FUN_00d9f250` — a checkpoint-hit event TAG, not code to execute), and coincidental substring matches on unrelated compound identifiers/xtbl field names.
- **Part (b):** identified every real `lua_load`/`luaL_loadbuffer`/`luaL_loadstring` call site in the binary (no symbolic names exist for any Lua C-API function in this binary — resolved structurally via the base-library registration table instead: `lua_load`=`0x00dfeb10`, `luaL_loadbuffer`=`0x00fcb3b0`, `luaL_loadstring`=`0x00fcb3e0`). `lua_load` has exactly 3 callers binary-wide; expanding to all 9 real call sites across both wrappers: 2 are file-based script loaders (chunkname = a real filename, buffer = file bytes — not runtime-built, one of them the already-confirmed §14.1 mission-script loader, the other a previously-undocumented sibling for a different, UI/VINT-family resource type **[OPEN — desk review 2026-09-30: §14.1 gives `FUN_00e0de80` only 2 callers, so the preload files of §16 (`system_lib.lua`, `game_lib.lua`, `vint_lib.lua`, the 4-entry array, the per-mission file) must reach `lua_load` through the other site, which would then be the general file loader rather than a UI/VINT-family sibling; to be settled against the executable.]**); 1 is the Lua stdlib's own `loadstring()`/`load()` global, reachable only if a real script calls it directly (already confirmed 0/804 do, §14.7); the remaining 6 are ALL fixed-literal, non-parameterized chunks (see below) — zero sites build a buffer from a mission stem or checkpoint value.

**[CONFIRMED — exhaustive; this rules out a sixth distinct mechanism class for `<stem>_start` invocation. Combined with §14.6–§14.9, every string-based mechanism this project can conceive of — pre-baked literal, EXE-side concatenation, Lua-VM-side concatenation, sprintf-templated hook name, two-part format, and now runtime-generated-and-compiled source text — is exhaustively excluded.]** This thread is parked here per the attempt-budget rule: the real invocation mechanism for `<stem>_start(checkpoint, is_restart)` remains genuinely OPEN, most likely a structural (non-string) mechanism — §14.9's two live candidates (numeric/hash dispatch; direct function-value capture at chunk-load time) — neither yet attempted. **Reopen only with a genuinely new angle, not a repeat of any string-construction search above.**

**Bonus real finding, unrelated to mission dispatch but newly confirmed along the way: a Lua sandbox-bootstrap mechanism via `luaL_loadstring` with fixed literals.** The 6 `luaL_loadstring` call sites above are all in 2 sibling functions (`FUN_00e0d4e0`, called from `FUN_00a1fa10`/`FUN_00e1e460`; `FUN_00e0d550`, called from `FUN_00a1fa90`), each firing 3 fixed literal chunks in sequence: `"_UGGlobals = getfenv()"`, `"setmetatable(_UGGlobals, {__index = _GetDynamicGlobal, __newindex = _CatchNilAssignment})"` (or `_CatchUndefinedGlobalWrite` in the second variant), `"setfenv(1, _UGGlobals)"`. This is the real runtime bootstrap for the "dynamic globals" sandbox — a metatable trick intercepting undefined-global reads/writes in a script's own environment — executed ~~immediately before the actual mission/UI script file loads~~ **[corrected per §16.1(f)/§16.2: after the state's first library file loads (`vint_lib.lua` / `game_lib.lua`), and after the per-mission file in the §16.2 path]**. **[CONFIRMED — disassembly, all 6 call sites and both literal-triplet variants read directly.]**

**[CONFIRMED — exhaustive; combined with §14.6 (no dedicated sprintf mission-hook family) and §14.7 (no strcat/strcpy-built suffix on either the EXE or Lua-VM side), every static string-concatenation mechanism this project has been able to conceive of for `<stem>_start`/`_run`/`_success` is now exhaustively excluded.]** The real invocation mechanism for `<stem>_start` remains genuinely OPEN — per §14.7's own script-side narrowing, the search should now focus on a literal, complete, per-mission `"<stem>_start"` string (e.g. `"m01_start"` as its own standalone string, not built from parts) or a `lua_next`-based globals-table iteration matching by suffix/hash rather than by building a name — neither yet attempted as of this entry. **[Superseded: the standalone-literal search was run in §14.9 (0/35 hits); see §14.10 for where the thread is parked.]**

**Review status (2026-09-30): NEEDS-EXE: loader census vs §14.1's 2-caller claim — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

## 15. `vint_object_find` and the Vint interface-state API — real behavior, closing a high-impact runtime gap (2026-09-29)

**Scope.** A peer team's real Lua-host run found `vint_object_find` resolving to `nil` accounted for 43% of all hook-fire errors — by far the largest single error cause, since a missing/nil global throws on the call itself. This section documents the confirmed real behavior needed to close that gap; the 55-name registrar this function belongs to is documented structurally in §13.7 — this section covers `vint_object_find`'s own behavior in full, plus enough about its closest siblings to explain why they're likely equally high-impact if also missing.

**Not a missing Lua-library load — genuinely native.** The real, shipped Lua source files (`vint_lib.lua`, `vdo_base_object.lua`, `vdo_anim_object.lua`, `vdo_input_tracker.lua`, `system_lib.lua`, `game_ui_globals.lua`, extracted directly from `interface.vpp_pc`) were grepped for a Lua-side definition (`function vint_object_find`/`vint_object_find =`) — zero hits, despite the function being called extensively from real script code (`vdo_base_object.lua:33`/`:93`, `vint_lib.lua:1227`). **[CONFIRMED — direct extraction and grep of real shipped source, not inferred.]**

**Real signature (`FUN_00e1b3d0`), variable arity 1–3, NOT fixed:**
- `name` (string) — **mandatory.**
- `parent_handle` (number) — **optional.** Absent or wrong type → falls back to a document-relative search instead of a parent-relative one.
- `doc_handle` (number) — **optional.** Absent → falls back to a "current default document" global lookup (`FUN_00e0ceb0`). **[⚠ cross-reference: `spec-lua-api-behaviour.md` §8.24 and §23.15 read `0x00e0ceb0` as a zero-argument context accessor whose `+0x14` field is the calling script's context, and §10.1 here uses the same function for the current script state; whether both readings describe the same object is not stated.]**

**Mechanism:** `name` is hashed once via the same general engine string-hash already confirmed elsewhere in this document (`FUN_00d9e740`); `parent_handle`, if given, resolves through `FUN_00e28850` — the IDENTICAL handle→object-pointer resolver `vint_object_create`/`vint_object_destroy` also call (confirmed by decompiling all three together) — then the hashed name is looked up among that resolved parent's children, or document-wide if no parent was resolved.

**Return — the critical fact for a stub implementation: always exactly ONE Lua value, a number, on every path, including every failure path. It never returns `nil` and never returns zero results.** Found → the object's real handle, read from offset `+0x28` of the resolved native record (an unsigned 32-bit id, with a sign-reinterpretation fixup for negative raw reads). Not found / bad parent / bad document → the literal number `0.0`. **[CONFIRMED — disassembly, every return path read directly, no path omitted.]**

**What a "vint object handle" is:** an opaque unsigned 32-bit numeric id (not a raw pointer visible to Lua) stored at offset `+0x28` of the underlying native VDO UI-element-instance record, mapped back to the live object pointer via the shared resolver `FUN_00e28850` above. `vint_object_destroy`'s own cleanup path dispatches through the same shared VDO-element-type-registry base-class cell (`0x0132bdd0`) already identified elsewhere in this document — independent corroboration that "vint object" means a VDO UI element instance specifically, not some other handle space.

**A real, honestly-flagged quirk in the ORIGINAL game's own scripts, not a reconciliation this document is asserting — do not "fix" this when stubbing:** the native's failure return is the number `0.0`, but `game_ui_globals.lua:9` defines `INVALID_VDO_HANDLE = -1`, and `vdo_base_object.lua:34`/`:67` compare the result against `-1`, never `0`. These never numerically match — the "not found" warning branch in the real shipped scripts is dead code on this specific path in the real game too. Implement the native's real `0.0`-on-failure behavior; do not change it to `-1` to make the Lua-side check "work," since that would diverge from the real game's own (quirky) behavior.

**Siblings likely equally high-impact if also unregistered, listed for stub-priority purposes (names only, roster in full at §13.7):** `vint_object_create`, `vint_object_destroy`, `vint_object_first_child`, `vint_object_next_sibling`, `vint_object_parent`, `vint_object_set_parent`, `vint_object_add_child`, `vint_object_get_name_from_handle` — all object-lifecycle functions on `vdo_base_object.lua`, the base class every UI widget derives from, so calls into this family are structurally as frequent as UI itself. Not individually traced this pass beyond confirming `vint_object_create`/`destroy` share `vint_object_find`'s own `FUN_00e28850` handle resolver — a further pass on these specific names, if still needed after `vint_object_find` lands, is a concrete, bounded next step.

**[CONFIRMED — disassembly for the registrar, the caller, `vint_object_find`'s full signature/mechanism/return semantics, and the handle resolver shared with `create`/`destroy`; the 54 sibling names are name-confirmed (§13.7) but not individually behavior-traced beyond `create`/`destroy`'s shared resolver.]**

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

## 16. Real preload order for both Lua states — closes a live Team B host gap (2026-09-29)

**Scope.** §13.7's own discovery (14 `vint_*` functions defined in `vint_lib.lua`/`game_ui_globals.lua`) **[⚠ attribution: this figure is not in §13.7; it comes from Team B's `team-b/tools/vint_functions_roster.txt` (12 in `vint_lib.lua` + 2 in `game_ui_globals.lua`)]** raised the question of which script files each Lua state preloads at startup, and in what order — needed because a peer team's Lua host currently preloads only `game_lib.lua`, and only into the gameplay state. This section gives the real, disassembly-confirmed answer for both states.

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 16.1 UI/"interface" state (`DAT_02a45450`) — full preload order confirmed

The top-level UI/engine bring-up (`FUN_008489e0`, real boot caller `0x005d245c` in `FUN_005d2400`, not a registrar-array artifact) does, in this exact order:

1. `FUN_00e1e460(FUN_008430f0, ...)` — one function that itself does, in order: (a) creates the state via `FUN_00e0e0b0("interface", 0, ...)`, which **unconditionally loads `system_lib.lua`** for ANY state it creates (a generic mechanism, confirmed to fire for the gameplay state too, §16.2); (b) calls `FUN_00e1dfb0` — the 55-name `vint_*` registrar (§13.7); (c) free-list pool init (generic, not Lua-specific); (d) invokes `FUN_008430f0` — the 311-name UI/menu/store/HUD cluster (§13.2) — **correction to §13.2's own prose: this address is passed as a callback function pointer into `FUN_00e1e460` and invoked once, indirectly, right here — not "stored into a subsystem dispatch table" as previously described**; (e) loads `vint_lib.lua`; (f) runs the dynamic-globals sandbox bootstrap (§14.10's variant A: `_UGGlobals = getfenv()` / `setmetatable(...)` / `setfenv(1, _UGGlobals)`) — **note: this sandbox only takes effect AFTER `vint_lib.lua` loads, so `vint_lib.lua`'s own top-level chunk runs unsandboxed**; (g) a second free-list pool init.
2. `FUN_00845aa0(FUN_00e1a1b0())` — the 113-name `game_*`/misc registrar (§13.5), confirmed called with the interface state fetched via its own accessor. **[OPEN — desk review 2026-09-30: `spec-lua-api-behaviour.md` §26.23 says `0x00e1a1b0` returns a fixed "value-list builder" singleton, not a `lua_State`; and §13.5 describes a state value read from a fixed global before the `FUN_00e1e460` call that, per step 1 here, creates the state; to be settled against the executable.]**
3. Several unrelated subsystem-init calls, not Lua-file-loading (not chased further).
4. **The confirmed "dedicated startup routine iterating a counted array of file-path strings"** (§2's own citation of the literal string `"Unable to open custom lua library %s\n"`, confirmed to match exactly): a 4-entry array at `0x01162da0` (count confirmed `4` via direct memory dump), loaded in order: `game_ui_globals.lua` → `vdo_base_object.lua` → `vdo_anim_object.lua` → `vdo_input_tracker.lua`.

**Real, complete file order for the interface state: `system_lib.lua` → `vint_lib.lua` → `game_ui_globals.lua` → `vdo_base_object.lua` → `vdo_anim_object.lua` → `vdo_input_tracker.lua`**, with the 55/311/113-name registrars interleaved exactly as above. **[CONFIRMED — disassembly, the full straight-line bring-up body read directly, not sampled.]**

**Review status (2026-09-30): NEEDS-EXE: what `0x00e1a1b0` returns, and the state read in `0x008489e0` — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 16.2 Gameplay state (`DAT_026e7e6c`) — `system_lib.lua` was missing from the record, plus a real per-mission secondary load found

`FUN_00a1fa10` (§13.6) does exactly what was already recorded, but with state-creation internals now filled in: `FUN_00e0e0b0("game play", 1, ...)` **also loads `system_lib.lua`** — the same generic mechanism as §16.1's interface-state creation. **A peer host preloading only `game_lib.lua` into the gameplay state is missing `system_lib.lua`.** After the co-op sole-authority gate, the confirmed sequence continues: the 1,014-entry registrar → the 5-name registrar (§13.6) → `game_lib.lua` → the sandbox bootstrap (loads no files itself, confirmed by full decompile — only 3 inline `luaL_loadstring` literals).

**A real, previously-undocumented per-mission secondary script load, reached from a separate, later trigger point (not boot):** **[OPEN — desk review 2026-09-30: the chain given below ends in a "real boot path", which conflicts with "not boot"; and this load of `<name>.lua` through `FUN_00e0df90` is not reconciled with §14.5's load of each `<mission>.lua` as a type-32 resource through `FUN_006cfac0`/`FUN_00e0de80` — either the same file loads twice or `<name>` is not a mission stem. Team B's `lua_entrypoint_suffix_histogram.tsv` shows only 1 of 804 scripts defining a `main`-suffixed entry point, which weakens the `_main` → `_start` lead below; to be settled against the executable.]** `FUN_00a20670` (reached from `FUN_0058b450`, itself from real boot path `FUN_00708330@0x00708581`) → `FUN_00a1fa90`: re-runs the co-op gate, builds `"<name>.lua"` via `sprintf("%s%s", <name-string>, ".lua")` (disassembly-recovered — the decompiler dropped the implicit register argument; raw asm at `0x00a1fac5`–`0x00a1fad5` confirms the real call), loads that file into the SAME persistent gameplay state via `FUN_00e0df90`, runs the sandbox bootstrap's variant B (`__newindex=_CatchUndefinedGlobalWrite` instead of `_CatchNilAssignment`), then fires `"<name>_init"` then `"<name>_main"` through the confirmed named-hook dispatcher (§14.3/§14.6) if each is defined. `FUN_00a20670` also builds a sibling `"<name>_collectibles"` string from the same caller-supplied name argument — consistent with `<name>` being a mission/level stem.

**Flagged as a strong, NOT-yet-closed lead for the long-parked §14.6–§14.10 `<stem>_start`/`_run` invocation mystery, not claimed as resolving it:** the suffixes confirmed here are `_init`/`_main`, not `_start`/`_run` — a mission's own `_main` plausibly calls `_start` from the Lua side (consistent with §14.7's own finding that `_run` is called from `_start` internally), but this was not verified this pass. A concrete next step for whoever reopens that thread, not a new negative or positive on it.

**[CONFIRMED — disassembly for the full chain in both items; the exact ultimate source of `<name>` (§16.2's per-mission load) was not traced back further than `FUN_00a20670`'s own caller argument.]**

**Review status (2026-09-30): NEEDS-EXE: boot vs non-boot trigger, and reconciliation with §14.5's type-32 load — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

### 16.3 `FUN_00e0f010` ("Lua chunk-name builder," §14.8) — re-examined fully, is NOT part of either preload sequence

**Its only reference anywhere in the binary is a DATA reference, never a CALL** — it is itself one of the 24 `{namePtr,funcPtr}` pairs the hand-rolled ~~`math`-library~~ **[bare-globals; not a `math` library, corrected 2026-10-01, §13.2]** registrar (`FUN_00e0f900`, §13.2) registers, i.e. a Lua-CALLABLE function **[2026-10-01, job `20261001T020218-team-a-bgcx`: it is pair #10, bound to the bare Lua global `include`; its only reference is the data store at `0x00e0f9a8` inside the registrar. CONFIRMED — disassembly. The body described below was not re-read by that job.]**, not a native helper invoked during bring-up. **This is the exact "registrar-array entry vs. real call" trap `WALLS.md` already warns about — caught here on a second, independent instance.** Its real body: reads its Lua-stack string argument, checks case-insensitively whether it already contains `".lua"`, appends the suffix if not, then passes the result to `FUN_00e0e140` — a fixed 64-slot "has this library name already been opened" bookkeeping list, with exactly 2 callers (both inside `FUN_00e0f010`) and no connection to the real file-loading path (`FUN_00e0d800`/`FUN_00e0df90`, both fully traced). **Correction to §14.8's own characterization: this is a Lua-callable "mark library opened" primitive, not a chunk-name builder invoked during native bring-up — it plays no role in either state's preload sequence.**

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

**Review status (2026-10-01): partly re-derived from the executable (job `20261001T020218-team-a-bgcx`): CONFIRMED — `0x00e0f010` is the Lua global `include` (pair #10 of `0x00e0f900`) and its only reference is the registrar's data store; the "mark library opened" body is not re-read (still desk-level).**

### 16.4 Bottom line for a peer Lua host

- Preload into the UI/interface state, in order: `system_lib.lua`, `vint_lib.lua`, `game_ui_globals.lua`, `vdo_base_object.lua`, `vdo_anim_object.lua`, `vdo_input_tracker.lua`.
- Preload into the gameplay state, in order: `system_lib.lua`, `game_lib.lua`.
- A per-mission secondary file (`<mission-stem>.lua`) also loads into the gameplay state at a separate, later (non-boot) trigger point, firing `<stem>_init` then `<stem>_main` afterward if defined — real and confirmed, exact stem-source chain not fully traced to its ultimate origin. **[⚠ also see §14.5: each `<mission>.lua` also exists as a type-32 "Mission LUA script" resource; the two loads are not reconciled (§16.2 OPEN note).]**
- **[Added, desk review 2026-09-30:]** Register the 8 dual-registered names of §13.5 (`audio_object_post_event`, `audio_stop`, `coop_is_active`, `game_is_active_input_gamepad`, `hud_display_set_element`, `hud_display_create_state`, `hud_display_commit_state`, `hud_display_remove_state`) into BOTH states, even though the tagged name files show them as `gameplay` only.
- **[Added, desk review 2026-09-30:]** The 24 bare globals registered by `0x00e0f900` (§13.2, §16.3) are Lua-visible and outside the 1,490 count; ~~which state receives them is not stated, and which script-called bare names (e.g. `max`, `floor`, `rand_int`, `round`, `debug_print`) are among them is OPEN (§13.2).~~ **[Settled 2026-10-01, job `20261001T020218-team-a-bgcx`:]** they are **bare globals** (not a `math` table), registered into **BOTH** Lua states by the generic state creator `0x00e0e0b0`, after the raw state is created and before `system_lib.lua` and every other registrar. A host must define all 24 (§13.2 roster) as globals in both states before running any preload file, so `system_lib.lua` may use them at top level. All of `max`, `floor`, `rand_int`, `rand_float`, `round`, `debug_print`, `abs`, `min` and `ceil` are among them. Per-state order: (stock libraries, HYPOTHESIS) → the 24 bare globals → `system_lib.lua` → that state's own registrars and files (§16.1/§16.2). Contracts: `rand_int(a, b)` is inclusive at both ends, truncates both arguments toward zero and accepts them in either order; `round(x)` rounds half away from zero; `debug_print`/`assert_msg` accept anything and return nothing (`spec-lua-api-behaviour.md` §26.27). **CONFIRMED — disassembly.**

**Team B request 8 (2026-09-30): does each preload run ONLY in its named state?** Team B's mission run (bridge job `…-knyf`) found that all 9 missions that get past `_start` stop on an attempt to call the nil global `vint_is_std_res` at `vint_lib.lua` line 96. That happens because their host runs `vint_lib.lua` in both states, while the `vint_*` natives are registered only in the interface state (§13.7).

*What this spec supports now:*
- **Interface state:** `vint_lib.lua` is loaded at one site only, step 1(e) of §16.1, inside `FUN_00e1e460`. That is the same function that creates the interface state and runs the `vint_*` registrar, before the load. The four later UI preloads come from the 4-entry array at `0x01162da0` in the interface bring-up (§16.1 step 4).
- **Gameplay state:** its bring-up (§16.2, `FUN_00a1fa10`) creates the state through `FUN_00e0e0b0` directly, not through `FUN_00e1e460`. It loads only `system_lib.lua`, which every created state gets, and `game_lib.lua`.
- **Conclusion:** `vint_lib.lua`, `game_ui_globals.lua` and the three `vdo_*` files load into the interface state only, and `game_lib.lua` into the gameplay state only; `system_lib.lua` loads into both. **HIGH CONFIDENCE (desk).** The positive load sites are CONFIRMED (§16.1 and §16.2), but the negative, that no other path loads them into the other state, has not been checked across the whole binary.
- **For a host:** it should run `vint_lib.lua` (and the other UI preloads) in the UI state only. A mission script running in the gameplay state never sees `vint_lib`'s functions.

*What is OPEN:*
- Whether any other caller of `FUN_00e1e460`, or any other use of the `vint_lib.lua` / `game_lib.lua` strings or of the loaders `FUN_00e0df90` / `FUN_00e0d800` / `FUN_00e0de80`, loads a preload into the other state.
- §16.1 step 2's state accessor (`0x00e1a1b0`), which is also OPEN.

*How it will be settled:* exe job `team-a/ghidra/jobs/teamb-request-8-preloads.json`. It lists every use of each preload file-name string and every caller of the state-creation and file-load functions, and dumps both bring-up bodies.

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

**Review status (2026-10-01): partly re-derived from the executable (job `20261001T020218-team-a-bgcx`): CONFIRMED — the bare-globals bullet (both states, before any preload, all script-called names present); the other bullets and the Team B request 8 answer are unchanged by this job.**

## 17. `ID_MISSIONS` and the cell-phone UI enum — not a missing native registration, a real cross-document Lua dependency; and a genuine negative on native-pushed global constants (2026-09-30)

Prompted by a peer team's Lua host hitting a real runtime failure: `cell_menu_main.lua` (loaded into both Lua states **[⚠ desk review 2026-09-30: no section of this spec shows `cell_menu_main.lua` loading into the gameplay state — §16.2 lists only `system_lib.lua`, `game_lib.lua` and the per-mission file; read this as the peer host's behaviour]**) fails on a nil global `ID_MISSIONS`, and no shipped `.lua` script defines it as a function or a `local` — the two shapes this project's own name census has ever looked for.

**Exhaustive native-side negative: there is no `ID_MISSIONS`, or any sibling `ID_*` name, anywhere in the executable's own string data — and, more broadly, no native mechanism exists anywhere in this binary for pushing a bare numeric/boolean Lua global by name at all.** Searched all initialized memory two ways (exact null-terminated string; raw byte substring, to catch an embedded literal inside a larger blob) for `"ID_MISSIONS"` and ten plausible siblings (`ID_ACTIVITIES`, `ID_STORE`, `ID_MAP`, `ID_SAINTSBOOK`, `ID_REWARDS`, `ID_MUSIC`, `ID_CAMERA`, `ID_PHONE`, `ID_CASH`, `ID_EXTRAS`) — zero hits, every name, both methods. Independently re-walked every one of the 62 real call sites to the confirmed `lua_setfield` primitive (`0x00dfe830`, this document's own ~~front matter~~ §13.1) fresh, not trusting any prior count: 62, confirmed. Of the 57 targeting `LUA_GLOBALSINDEX`, classified what each one actually pushes: 55 push a real registered C function via `lua_pushcclosure` (the classic idiom this whole document catalogues); the remaining 2 both sit inside one single base-library bootstrap function (`0x00fcca70`) and are the standard Lua 5.1 internals binding `"_G"` (globals table to itself) and `"_VERSION"` (to the literal string `"Lua 5.1"`) — real, but not game-specific and not numeric/boolean constants. **Zero of the 62 real `lua_setfield` sites in this entire binary push a bare number or boolean under any name.** **[OPEN — desk review 2026-09-30: this partition (57 `LUA_GLOBALSINDEX` = 55 closure + 2 base-library at `0x00fcca70`, plus 5 other) conflicts with §13.2's (4 base-library sites + the `0x00e0f900` block + ~57 wrapper sites); the two "57"s are different populations; to be settled against the executable.]** **[CONFIRMED — exhaustive, whole-binary string search plus a fresh full re-walk of every real `lua_setfield` call site with value-type classification.]**

**The real mechanism, found in real shipped Lua source, not disassembly: `ID_MISSIONS` and ten siblings are ordinary Lua-side global assignments (`NAME = value`, no `local` keyword) inside `cell_foreground.lua`** — the cell-phone UI's own parent/container document, a separate `.str2_pc` bundle from `cell_menu_main.lua`:

Described in this project's own words (no script text reproduced): the file assigns eleven integer globals, one per cell-phone menu button plus two sentinels — `ID_INVALID` = `-2`, `ID_MAIN` = `-1`, `ID_MAP` = `0`, `ID_MISSIONS` = `1`, `ID_SAINTSBOOK` = `2`, `ID_REWARDS` = `3`, `ID_MUSIC` = `4`, `ID_CAMERA` = `5`, `ID_PHONE` = `6`, `ID_CASH` = `7`, `ID_EXTRAS` = `8`.

Once this chunk has run once in a given Lua state, these 11 names sit as ordinary globals in that state's `_G` for the state's lifetime — which is exactly what `cell_menu_main.lua` depends on for its own very first executable line (`[ID_MAP] = {`, i.e. it fails immediately, at table-construction time, if the enum isn't already defined). A grep of every real `.lua` file inside `interface.vpp_pc` (632 files) for the name family found only 3 other hits, none of them a second definition: `cell_menu_main.lua`/`pause_map.lua` only *use* the names; `game_lobby.lua` has an unrelated same-named **local** (`local ID_MAP = 3`, a different value, correctly scoped to that one file, a harmless collision). None of the ~~three~~ six **[corrected: §16.1 lists six boot-preload UI files, adding `vdo_base_object.lua`, `vdo_anim_object.lua`, `vdo_input_tracker.lua`; the 632-file grep above covers all six]** fixed boot-preload UI files (`system_lib.lua`, `game_ui_globals.lua`, `vint_lib.lua`, `spec-lua-bindings.md` §16) define any of these names either — this enum lives nowhere except `cell_foreground.lua`. **[CONFIRMED — read from real shipped Lua source (names and values only; the script text itself is not reproduced here), cross-checked against every other real hit of the name family inside `interface.vpp_pc`; the multi-gigabyte city/mission archives were not exhaustively swept for a duplicate definition, architecturally implausible for a client-UI cell-phone enum, flagged rather than silently assumed.]**

**Direct, actionable conclusion: this was never a missing native registration.** `cell_menu_main.lua`'s real dependency is that `cell_foreground.lua` (its own parent/container document) must be loaded and run in the same Lua state *before* `cell_menu_main.lua`'s own chunk executes. A host seeing this specific nil-global failure is very likely loading `cell_menu_main.lua` standalone, without its sibling/parent document.

**Net effect on this document's own scope: the function-only census is not missing a "constants" tier — there isn't one to miss.** No native `lua_pushnumber`/`lua_pushboolean` + `lua_setglobal`/`lua_setfield(LUA_GLOBALSINDEX)` batch of any kind exists anywhere in this binary; every Lua-visible global this engine ever creates by name is either a registered C function (this document's own subject) or ordinary Lua-side script data, resolved the normal way by loading the right chunk first. **[CONFIRMED — exhaustive negative, whole-binary.]**

**Review status (2026-09-30): NEEDS-EXE: the 62-site partition vs §13.2 — desk review only (checked against the other specs, not re-derived from the executable); NOT yet cleared for implementation.**

## Changelog

- 2026-09-30 (cloud consistency review): §17 — replaced a verbatim quote of shipped game Lua source (`cell_foreground.lua`, including its comment) with a plain name→value list; the facts are unchanged. See `review/spec-consistency.md`.
- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): fixed 5 cross-references (§9.5→§9.4, §12.5→§14.1, front matter→§13.1, §6.22→`spec-lua-api-behaviour.md` §6.22, §14.1 chain naming `FUN_00e213d0`) and marked `spec-output.md` as unpublished; marked 11 stale OPEN/next-step items resolved in place (§6 items 1–5 status pointers, §8.4, §13.6 ×3, §14.1, §14.7 ×2); annotated stale text superseded by later sections (§2 preload array vs §16.1, §4/§12.3/§12.9 counts vs §14.3, §8.2/§8.7 "36"/"61" vs §12.2, §13.2/§13.5 "stored in a table" vs §16.1(d), §13.4 `vint_*` vs §13.7, §13.7 opener, §14.8 vs §16.3, §12.5 "~45" vs 43, §6 item 1 vs §7.3); flagged the §9.3 slot-spacing arithmetic (4 not 8 bytes) and the unresolved §9.3/§12.7 setter-slot conflict; reworded decompiler-shaped text (§9.3/§10.1 `param_N`, §13.6 decompiler signature and call statements).
- 2026-09-30 (cloud, self-containment pass): restated 0 load-bearing HANDOFF/WALLS-only facts inline (every HANDOFF/WALLS citation here is provenance/methodology, or the fact is already stated in this or another spec); repointed 0 `HANDOFF.md` §27.x references to the archived headings; 2 left (see review).
- 2026-09-30 (cloud, desk adversarial review of §1–§17, `review/spec-consistency.md`): added a review-status line to all 57 reviewed units (§1–§11: 7 DESK-PASS, 9 with text fixes, 8 NEEDS-EXE; §12–§17: 9, 12, 12) and a review-status summary after the front matter; applied text fixes in place (1,325→1,490 pointers in §1/§11, §7.4 0x2cc→0x4cc, §8.5 two→three functions, §10.1 six→seven helpers, §13.5 `game_coop_*` 7→5, §17 three→six preload files, §12.9 banner 155 again, §14.5 55/12/12 manifest split, §13.4 arithmetic and post-1490 residual); closed the §7.2/§9.2 third-hash OPEN (CRC-32 `0x00d9e740`, `spec-tables-environment.md` §1.4); marked the `DAT_02319570` mission-trigger reading superseded by `spec-lua-api-behaviour.md` §8.24 (§8.5, §10.2, §11, §12.7, §14.9) and the `0x0087ba20` "mission active"/"head/tail-list-empty" wording superseded by §6.22/§8.27 (§12.5, §13.6, §14.5); added OPEN notes for every NEEDS-EXE finding, including §4's CONFIRMED label.
- 2026-09-30 (cloud, Team B request 8): §16.4 answers "does each preload run only in its named state" at desk level (HIGH CONFIDENCE: `vint_lib`/UI preloads interface-only, `game_lib` gameplay-only, `system_lib` both; the whole-binary negative is OPEN), with exe job `teamb-request-8-preloads.json`.
- 2026-10-01 (cloud, executable re-derivation from bridge job `20261001T020218-team-a-bgcx`, with sibling `20261001T021641-team-a-yduu` for the body of `0x00e0e0b0`): §13.2 — struck the "Lua's own `math` library, not counted" framing (15 of the 24 pairs are engine functions), added the full 24-name roster and the state answer (both states, from the generic state creator, before any preload); §16.3 — `0x00e0f010` is the Lua global `include`; §16.4 — bare-globals bullet settled; front matter item (b) annotated. Old text struck or annotated in place.
