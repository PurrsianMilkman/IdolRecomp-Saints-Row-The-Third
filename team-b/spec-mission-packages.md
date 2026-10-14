# Saints Row: The Third — Per-Mission/Activity Package Contents Specification

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1, Target #12 (from `spec-output.md` §4 (unpublished Team A working document, not in this repository)) — the last item from the original Phase 0 target list.
**Scope:** The individually-named `.str2_pc` packages inside `sr3_city_missions.vpp_pc` (Trafficking/Heli/Escort-style activity packages) and similarly-organized archives — what's actually inside them, and how reliably it can be extracted given everything already learned about the container format.
**Method:** Black-box extraction using the already-confirmed container format (`spec-vpp-container.md`), cross-checked against the corrected `.asm_pc` manifest model (`spec-asm-format.md`, corrected as part of this same pass) and the `.xtbl`/geometry work already done. No new disassembly this pass.
**Cleanroom compliance:** No decompiled code or internal identifiers appear below. Real file/activity names quoted below (e.g. `Trafficking`, `Horde Mode`) are ordinary shipped data (container directory entries and manifest strings), not code.

**Confidence key** (as in prior specs): **CONFIRMED — empirical**, **HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**.

**Review status summary (2026-09-30)** — adversarial desk review (`review/adv_mission-packages.md`), 8 units: DESK-PASS 1 (§6); DESK-PASS, text fixes applied 3 (§2, §3 `.cefct_pc` characterization, §4 — §4's aggregate-size field is also backed by Team B's population figure); VALIDATED-BY-DATA 1 (§3.1 — names and sizes are exact-match anchors in Team B's `.cefct_pc` population harness); NEEDS-DATA 3 (§1, §3.2, §5). A desk pass alone does not clear a unit — it was read for internal consistency, not re-derived from the executable or re-measured; only the VALIDATED-BY-DATA material is already backed by Team B's full-population run. **Awaiting the executable:** none (no exe address is needed by this spec). **Awaiting real data:** §1 (header `0x14C` of the top-level archive and all 15 nested containers; entry counts), §3.2 (size of `running_man_globals.xtbl`; the "first worked example" cross-reference), §4 (`source_name` length of the 13 zero-entry records; same-name collisions of the 15 record names in other archives), §5 (top-level name lists of `sr3_city_0/1` — the odd count 1,033 — and of `dlc1/2/3`). Units are marked with **[OPEN — desk review 2026-09-30: …]** at the claim and a **Review status** line at their end.

---

## 1. Overview and reliability — good news, for once

`sr3_city_missions.vpp_pc` is a **fully raw/uncompressed top-level archive** (flags `0x0` — i.e. the container header field at offset `0x14C`, `spec-vpp-container.md` §1) — every one of its 16 entries is directly, unconditionally extractable, with none of the mode-(a) reliability concerns that have limited other targets in this project. **[CONFIRMED — empirical.]** Its 15 named `.str2_pc` entries are per-activity packages (the "Trafficking/Heli/Escort-style" packages this target names); the 16th is `sr3_city_missions.asm_pc`, this archive's manifest (§4).

Better still: **every one of those 15 nested per-activity containers is a mode-(b) shared-stream container** (flags `0x4803`), which — per `spec-vpp-container.md` §3.6 — means **all of their entries decode reliably, not just the first one**. So unlike the vehicle-stats, customization, and geometry passes (which all ran into the parked mode-(a) decode-failure limitation blocking most of their content **[resolved 2026-09-20, `spec-vpp-container.md` §7]**), **this target has no such blocker: everything described below was read directly and reliably.** **[CONFIRMED — empirical, all 15 nested containers checked.]** *(Desk review 2026-09-30: only 2 of the 15 nested containers have any entries (§2), so "all entries decode reliably" is exercised on exactly two containers; for the 13 empty ones it holds trivially.)* **[OPEN — desk review 2026-09-30: the archive census (16 top-level entries, flags `0x0`; `0x4803` on all 15 nested containers) has not been re-run by Team B; to be settled against real data.]**

**Review status (2026-09-30): NEEDS-DATA: header `0x14C` of the top-level archive (expect `0x0`) and of each nested container (expect `0x4803` ×15), and entry counts (16; 0 ×13, 2 ×2), are not re-measured; text fix applied (named the header offset) — desk review (not re-derived from the executable).**

## 2. The roster: 15 named packages, only 2 with real content

| # | Package name | Sibling entries | Content |
|---|---|---|---|
| 0 | `Trafficking` | 0 | *(empty)* |
| 1 | `Horde Mode` | 2 | Particle effects — §3.1 |
| 2 | `Guardian Angel` | 0 | *(empty)* |
| 3 | `Tank Mayhem` | 0 | *(empty)* |
| 4 | `Fraud` | 0 | *(empty)* |
| 5 | `Snatch_Kinzie` | 0 | *(empty)* |
| 6 | `Running Man` | 2 | Table + particle effect — §3.2 |
| 7 | `Heli` | 0 | *(empty)* |
| 8 | `Snatch` | 0 | *(empty)* |
| 9 | `Escort_Tiger` | 0 | *(empty)* |
| 10 | `Escort` | 0 | *(empty)* |
| 11 | `Mayhem` | 0 | *(empty)* |
| 12 | `Human Torch` | 0 | *(empty)* |
| 13 | `Zombie` | 0 | *(empty)* |
| 14 | `Human Torch Cyber` | 0 | *(empty)* |

**13 of the 15 named activity packages are genuinely, verifiably empty** — real, valid mode-(b) containers with an `entry_count` of exactly `0`. **[CONFIRMED — empirical, all 13 checked directly.]** *(Clarified 2026-09-30, desk review: each of the 13 is a real sibling `.str2_pc` whose own directory `entry_count` is `0`, **and** has a manifest record in `sr3_city_missions.asm_pc` whose `entry_count` is `0` (§4) — "empty" here does not mean "no sibling container".)* This was not expected going in, and is worth stating plainly: **most of what this archive "reserves a slot for" by name has no archive-specific content at all.** Only `Horde Mode` and `Running Man` carry any package-specific assets.

**What this means:** the bulk of a side activity's actual behavior (spawn logic, scoring, UI, objective text) does **not** live in this archive at all — it lives in the systems already documented elsewhere in this project: the named Lua/UI event hooks (`spec-lua-bindings.md` §4, which already includes real hook names for several of these exact activities — `button_mashing_minigame`, `sr2_balance_meter`, `mayhem_local_player_world_cash`, `whored_countdown_timer_update`), and the shared `.xtbl` tables in `misc_tables.vpp_pc` (`spec-xtbl-format.md`, `spec-vehicle-data.md` — many activity-adjacent tables like `distant_vehicle_spawn_parameters.xtbl` live there, though most are currently blocked by the parked container limitation since that archive is mode-(a) **[resolved 2026-09-20, `spec-vpp-container.md` §7]**). **This archive's actual role is narrower than its name list suggests: it's a place for an activity to ship a small number of *package-specific* assets it can't get from those shared systems — most don't need any.** **[HIGH CONFIDENCE — inferred, from the consistent emptiness pattern plus the direct thematic overlap with already-confirmed hook names.]**

**Review status (2026-09-30): DESK-PASS, text fixes applied (sibling-container vs manifest-record wording clarified; the 15 package names and the hook names are kept as functional identifiers the engine resolves) — desk review (not re-derived from the executable).**

## 3. The two non-empty packages

### 3.1 `Horde Mode`

Two entries, both particle/cinematic-effect files: `vfx_shockwave_kill_all.cefct_pc` (12,480 bytes) and `vfx_whored_invulnerable.cefct_pc` (5,264 bytes) — named exactly as their gameplay purpose suggests (a kill-all shockwave effect, an invulnerability effect for the "Whored" horde-mode activity). **[CONFIRMED — empirical.]**

**Review status (2026-09-30): VALIDATED-BY-DATA: both names and sizes (12,480 / 5,264) are exact-match anchors in Team B's `.cefct_pc` population harness (`team-b/tools/validation/validate_cefct_population.cpp`, all 1,812 `.cefct_pc` entries decode); containment in this package rests on §4's manifest check — desk review (not re-derived from the executable).**

### 3.2 `Running Man`

Two entries: `running_man_globals.xtbl` (31,474 bytes — ~~already fully documented as this project's very first `.xtbl` worked example, `spec-xtbl-format.md`~~ **described in `spec-xtbl-format.md` as one early sample (§4), the mode-(b) decode test (§1) and a row-element example (§3.1) — not as a "first worked example" or "fully documented," wording corrected 2026-10-03** **[OPEN — desk review 2026-09-30: `spec-xtbl-format.md` names this file only as "one early sample … seen in the container-format pass" (§4), as the mode-(b) decode test (§1) and as a row-element example (§3.1); it does not give the 31,474-byte size, and no section there presents it as a "first worked example" or documents it fully. The size and the cross-reference are to be settled against real data / the xtbl spec's owner.]**) and `vfx_runningman_fireworks.cefct_pc` (14,416 bytes — a fireworks particle effect, matching the "Running Man" activity's finale). **[CONFIRMED — empirical.]**

**Review status (2026-10-03): DESK-PASS: `running_man_globals.xtbl`'s size (31,474 bytes, backed since 2026-10-01 by the owner's PC-era extraction, bridge job `20261001T020155-team-a-kbhb`) and `vfx_runningman_fireworks.cefct_pc`'s 14,416 bytes (an exact-match anchor in Team B's `.cefct_pc` population harness) were already settled; the "first worked example" claim needed a wording fix, not data (`review/exe-notes-2026-10-01/interp_needs_data_triage_20261003.md` §3) — now corrected above.**

### `.cefct_pc` — light characterization only

Not a target of its own this pass, but worth noting: every `.cefct_pc` sample opened here **starts with the exact same shared "material reference block" header already found in `.matlib_pc` and `.ccmesh_pc`** (`spec-geometry-format.md` §3.1) — same constant, same name-table-length field, same texture-slot-count-then-names structure. `vfx_shockwave_kill_all.cefct_pc`'s block declares 3 texture references: `vfx_corona_white.tga`, `vfx_smoke_anim_v01.tga`, `vfx_shockwave_01.tga`. **[CONFIRMED — empirical, and folded back into `spec-geometry-format.md` as a third confirmation of that shared block.]** The rest of a `.cefct_pc` file (presumably the actual particle-emitter/timeline parameters) was **not investigated** at the time — since resolved with a light, dedicated follow-on pass: see `spec-effects-format.md` for a fixed sub-header marker, the effect's self-referencing `.effectx` source filename, and its named sub-object list (confirmed, e.g., `FX_BlastWave01`, `Object01`, `VFX Filter01` for these same two samples — *a subset only; the structured array in `spec-effects-format.md` §5.4 lists further names (e.g. `FX_BlastWave_flat`, `p_vel_sparks_01`) that the informal scan missed*) — the per-object parameter/timeline data itself remains open there too.

**Review status (2026-09-30): DESK-PASS, text fixes applied (sub-object list marked as a subset, pointer to `spec-effects-format.md` §5.4; texture names kept as functional identifiers). The shared material-block structure is backed by Team B parsing 1,812/1,812 `.cefct_pc` with it — desk review (not re-derived from the executable).**

## 4. The `.asm_pc` manifest — corrected model, confirmed against this archive

`sr3_city_missions.asm_pc` was decoded using (and this investigation directly motivated a correction to) `spec-asm-format.md`. The archive-specific table's record for each of the 15 packages was checked byte-for-byte:

- **All 13 empty packages produce the exact zero-entry record shape** now documented in `spec-asm-format.md` §4 **[§4's record model is itself superseded — see `spec-asm-format.md` §7.3/§7.4 and §10.1]** (a fixed 19-byte tail, `entry_count = 0`, no per-entry sub-blocks) *(Qualified 2026-09-30, desk review: per the current model, `spec-asm-format.md` §7.3, the tail after `name` is 19 bytes **only when `source_name` is empty** — it is `19 + len(source_name)` bytes, `extra_len` being 0 on 8,362/8,362 records, `spec-asm-format.md` §9.3.)* **[OPEN — desk review 2026-09-30: whether all 13 zero-entry records here have an empty `source_name` is not stated; to be settled against real data.]** — this is the same shape the implementation team had already flagged from this same archive's `Trafficking` entry specifically; checking all 13 confirms it's the general zero-entry case, not something specific to `Trafficking`.
- **`Horde Mode`'s and `Running Man`'s records each produce exactly 2 per-entry sub-blocks**, matching their real sibling entry counts, and — this is what actually drove the `spec-asm-format.md` correction — **each sub-block's own name and uncompressed-size field match that specific sibling entry exactly** (e.g. `Horde Mode`'s record correctly names both `vfx_shockwave_kill_all.cefct_pc` and `vfx_whored_invulnerable.cefct_pc`, with their real uncompressed sizes, 12,480 and 5,264). The records' one aggregate size field was confirmed to match each sibling container's own header field `0x168` (its total compressed payload size) exactly — `2,459` for `Horde Mode`, `6,025` for `Running Man`. **[CONFIRMED — empirical, both samples, cross-checked against independently-established container-format ground truth.]** *(Desk review 2026-09-30: Team B's population run matches manifest `payload_length` to the sibling header field `0x168` on 7,990/7,990 records with a compressed sibling (`team-b/HANDOFF.md` l.10974–10975), and the manifest file list to the sibling directory case-insensitively on 8,057/8,057. Case note: the nested entry names are lowercase (0 of 384,479 contain an uppercase letter, `team-b/HANDOFF.md` l.10990), while the package names in this spec (`Horde Mode`, `Running Man`) are the archive's top-level entries and the manifest keeps authored case — compare names case-insensitively.)* **[OPEN — desk review 2026-09-30: whether any of the 15 record names also matches a same-named `.str2_pc` in another archive (Team B found 68 such records population-wide) is not checked; to be settled against real data.]**

See `spec-asm-format.md` ~~§4~~ §7.3/§7.4 (which supersede §4; see its §10.1) for the full corrected record model; this section exists to record that the correction was driven by, and is fully consistent with, this archive.

**Review status (2026-09-30): DESK-PASS, text fixes applied (19-byte tail qualified to empty `source_name`; case-sensitivity note). Aggregate-size field VALIDATED-BY-DATA: `payload_length` == sibling `0x168` on 7,990/7,990 (Team B) — desk review (not re-derived from the executable).**

## 5. Related archives checked and distinguished

To address the "similar mission archives" part of this target's scope, the following were checked and found to be **structurally different, serving different purposes** — not further documented here:

- **`sr3_city_0.vpp_pc` / `sr3_city_1.vpp_pc`** — large (1,033 and 1,516 entries) fully-raw archives holding **open-world city geometry, organized by map grid tile** (entries named by coordinate, e.g. `1024.str2_pc`/`1024.asm_pc`), not by mission or activity. A world-streaming system, not a mission-package one. **[CONFIRMED — empirical, name pattern and entry count checked.]** **[OPEN — desk review 2026-09-30: if every tile contributes a `.str2_pc`/`.asm_pc` pair, a top-level count is even; 1,033 is odd, so at least one entry of `sr3_city_0.vpp_pc` is not a coordinate-named pair member (a shared manifest such as `stream_grid.asm_pc` is a candidate, §6 item 4). Which entry it is, and whether "named by coordinate" holds for all other entries, is to be settled against real data.]**
- **`cutscenes.vpp_pc` / `cutscene_sounds.vpp_pc`** — cinematic-camera/animation data and cutscene audio banks, organized by cutscene-shot name (e.g. `16_out.str2_pc`, `ct_kinzie_01.bnk_pc`) rather than by mission/activity name. Mission-adjacent (cutscenes obviously belong to specific missions) but a genuinely separate archive family with its own naming convention — not investigated further here, and already partially covered by `spec-fxo-format.md`'s and `spec-vpp-container.md`'s use of this same archive family for other worked examples.
- **DLC archives (`dlc1/2/3.vpp_pc`)** — checked for an equivalent "named activity package" archive and found none; DLC mission-specific content instead appears as **flat, directly-named `.xtbl` files** at the archive's top level (e.g. `dlc1_rm_01.xtbl`, `dlc1_a_bm_nw_01.xtbl` — already noted in passing in `spec-vehicle-data.md`), not as nested named `.str2_pc` packages. **This means the "named activity package" pattern documented in this spec is specific to `sr3_city_missions.vpp_pc` and does not generalize to the DLC content — flagged as a real structural difference, not an oversight.** **[HIGH CONFIDENCE — inferred from a direct check of all three DLC archives' top-level entries; the DLC mission `.xtbl` files themselves were not individually opened/characterized this pass.]**
  **[CORRECTED 2026-10-12, FINAL — the "no nested packages" claim is WRONG for `dlc2`/`dlc3`.** A raw
  name scan of `dlc2.vpp_pc` finds the same per-mission package set the main game uses, for each of
  `dlc2_m01..m03`: `dlc2_mNN.str2_pc`, `dlc2_mNN_modal.str2_pc`, `dlc2_mNN.bnk_pc`/`_media.bnk_pc`,
  `dlc2_mNN_pc.cvbm_pc`/`.gvbm_pc` and `dlc2_mNN_tod_override.xtbl`. `dlc3.vpp_pc` follows the same
  pattern for `dlc3_m01..m03`. So the mission script `dlcN_mNN.lua` sits inside
  `dlcN_mNN_modal.str2_pc` inside `dlcN.vpp_pc`, the same `<code>_modal` → `<code>.lua` rule as
  the `sr3_city_missions.vpp_pc` missions (`spec-lua-bindings.md` §14.5). **Port consequence:** a
  mission runner must include `dlc2.vpp_pc`/`dlc3.vpp_pc` in its archive search set for the DLC
  missions. Team B's 2026-10-12 sweep fails all six (`dlc2_m01..dlc3_m03`, exit 1, "entry
  'dlcN_mNN.lua' (container 'dlcN_mNN_modal') not found in any of the 5 given") for that reason
  alone. The `dlc1` "flat `.xtbl`" observation above still stands; dlc1 holds the `dlc1_mm_*`
  activities, which already run.]**

**Review status (2026-09-30): NEEDS-DATA: top-level counts 1,033 / 1,516 are not in Team B's figures and the odd count is unexplained; DLC top-level listing not re-run — desk review (not re-derived from the executable).**

## 6. Open Items

1. **The full internal structure of `.cefct_pc`** beyond its shared material-reference-block header (§3) — a light follow-on pass (`spec-effects-format.md`) has since documented a fixed sub-header marker, the effect's source filename, and its named sub-object list; the actual per-object particle/effect timeline data is still open there.
2. **The DLC mission `.xtbl` files** (`dlc1_rm_01.xtbl` and siblings, §5) were noted by name but not opened or characterized — a real, likely-accessible (DLC archives are raw) follow-on if mission-scripting content becomes a priority.
3. **Why most activities have no package-specific content** (§2) is inferred from the pattern and from thematic overlap with already-confirmed Lua hooks and shared tables, not directly traced through the loading code that decides what (if anything) a given activity package should contain.
4. **`sr3_city_0.vpp_pc`/`sr3_city_1.vpp_pc`'s grid-tile world-streaming format** (§5) is a large, distinct, currently-undocumented system, noted here only to rule it out as "similar" to this target — it would be a substantial target of its own if picked up later. **Update, 2026-09-29: this system is confirmed to also carry mission Lua scripts** — `spec-lua-bindings.md` §14.5 found 79 real "Mission LUA script" resource entries (type 32) across this same manifest family's `stream_grid.asm_pc` files, each a `<mission-code>.lua` inside a `<mission-code>_modal` container (e.g. `m24_modal`→`m24.lua`), confirming the real trigger for mission-script loading is this world-streaming grid, not a separate mechanism. Still not opened as a format in its own right — this is a citation, not new characterization of the grid format itself.

None of these gaps affect the core deliverable: every package in `sr3_city_missions.vpp_pc` — empty or not — can be reliably enumerated and, where content exists, fully and correctly extracted, with no dependency on the still-blocked mode-(a) container limitation **[resolved 2026-09-20, `spec-vpp-container.md` §7]** that has constrained several other targets in this project. *(Desk review 2026-09-30: "fully and correctly extracted" is exercised on the 2 non-empty packages only.)*

**Review status (2026-09-30): DESK-PASS — desk review (not re-derived from the executable).**

---

This closes out all 12 original Phase 0 targets (`spec-output.md` §4).

## Per-mission objective routes (test vectors, 2026-10-13)

**What this is.** For each of the 49 missions in Team B's sweep `mission_sweep_49_20261013_post7b3e110`, this section gives the ordered list of blocking waits the mission script passes through: the route a headless runner must satisfy for the main thread to advance. It is script-derived, from this project's own Lua dumps (`scratchpad/lua_dumps/`), not exe-derived. It complements `spec-mission-objective-vectors.md`, which lists objective-text keys and phase order; that data is not repeated here.

**How it was produced.** `tools/scratchpad/mission_routes.py` starts at `<mission>_start` and walks the mission's own functions in source order, depth-first. It binds call-site arguments to parameter names, so shared helpers such as `m15_goto_a_trigger(trigger, ...)` resolve to the real trigger. At each step it records:
- every `while`/`repeat` loop that yields (`thread_yield`/`delay`);
- `while true` loops, through their `if ... then break` conditions;
- statement-level `delay(N)` with N >= 5;
- the blocking helpers `cutscene_play`, `fade_*_block`, `mission_start_fade_in` and `audio_conversation_wait_for_end`.

When a wait condition is a script flag, the extractor finds the function that sets it to a satisfying value. Resets to false/nil/0 and `_cleanup` functions are skipped. It then uses the native that registers that function as a callback:
- `on_trigger` maps to go-to-trigger;
- `on_vehicle_enter` maps to enter-vehicle;
- `on_death`, `on_vehicle_destroyed` and `on_random_*_killed` map to kill;
- `audio_play_for_mission_cellphone` arg 4/5 maps to a phone wait.

When several callbacks set the same flag, the one registered most recently on the walk wins. Names go through the mission's global tables and constants. Per-mission TSVs, which hold every step including filler, plus `<mission>.hazards.tsv`, are in `tools/scratchpad/mission_routes/`. `tools/scratchpad/mission_routes_stall.py` matches Team B's "still suspended at file:line (function)" lines against the TSVs. `tools/scratchpad/mission_routes_md.py` renders the tables below.

**Limits.**
- The walk is static. It enters every checkpoint branch of `<m>_run` in source order, and every `if` arm, so a few steps are alternatives rather than a strict sequence.
- Loops inside helpers called several times appear once per call.
- Waits inside `thread_new` threads are not walked as route steps. Their callback registrations are still resolved.
- **Trigger sizes and positions are zone data, not script data.** They are now extracted in "Route target positions (zone data, 2026-10-13)" below. No mission script sets a trigger radius; there is no `trigger_set_radius`-style call in any dump. The only radii a script supplies are the literal thresholds it compares `get_dist` against, shown as `dist <N`. A runner needs each trigger's position and shape from the zone files, as Team B's harness already loads them ("REAL position ... REAL shape from zone data").
- Names in `{...}` are a table of candidates: the script waits on any of them, or loops over all of them.

**Step kinds and how a runner satisfies each.**

| kind | what the script waits on | runner action |
|---|---|---|
| go-to-trigger | `on_trigger(cb, T)` flag, or an `object_is_in_trigger(T, player)` poll | Teleport the player *into* T's zone-data volume **after** the wait is armed. Entry is edge-triggered: if the player is already inside when the script resets its flag (m22 line 1591), step out and back in. For `object_is_in_trigger` polls (dlc2_m02) the player must *stay* inside while the loop runs, since no callback fires. |
| go-to-navpoint | `get_dist(a, b) < N` | Teleport to within N of the navpoint. When the mover is an NPC or vehicle (`mover=`), the *NPC/vehicle* must arrive, which is AI pathing, not the player. `dist >N` means "get at least N away". |
| enter-vehicle | `character_is_in_vehicle(player[, V])`, or an `on_vehicle_enter` callback | Warp the player into the driver seat of the named vehicle, or any vehicle if none is named. A named vehicle (dlc3_m02 `start_truck`) must be that exact one. |
| exit-vehicle | `not character_is_in_vehicle(...)` | Player on foot. |
| drive-to | a go-to-trigger right after enter-vehicle, or one whose callback checks the vehicle | Teleport the *vehicle with the player in it* into the trigger. |
| kill-target | `on_death`/`on_vehicle_destroyed`/`on_random_*_killed` callbacks, or `character_is_dead`/`vehicle_is_destroyed` polls | Mark the named targets dead or destroyed through the engine's kill path, so the death callbacks fire. `on_take_damage`/`on_damage` callbacks may need a *specific* damage kind (m18: sonic). |
| interact | store purchase, crate/use triggers, waypoint placement | Perform the purchase or use action (for example `mission_store_purchased`, `spec-lua-api-behaviour.md` §25.3). |
| wait-cutscene-or-phone | `cutscene_play`, `zscene_is_loaded`, cellphone cb2, `action_play_is_finished` | **No player action.** The engine must complete it: cellphone cb2 must fire when the call ends (`spec-lua-api-behaviour.md` §12.2), and zscenes must report loaded. |
| timed-wait | `delay(N)` with N >= 5 | Only game time. The run must last long enough, or advance its clock. |
| engine-wait / wait-flag / wait-thread | other native predicates, script flags, helper threads | Engine-state or script-internal waits, listed for completeness. |
| terminal | `while true` with no break | Never exits. The mission ends via a failure or death callback. |

**Cross-mission findings from the hand check (sweep 49, 30 missions still suspended).**
1. **The runner chooses the wrong trigger.** It teleports into whichever trigger it saw waited on first, which is often not the objective. **Hazard triggers** it entered: m24 `Lava_Death_Trigger`, m16 `Death_Region` and m08 `Trigger Massacre` (all three call `character_kill`). m05's `m05_trigger_kill` is an `on_trigger_exit` kill zone. It also entered area or conversation triggers: sh01 `trig_AlleyConvo`, sh04 `Mission Area 001`, dlc3_m03 `Bridge_area_check_tgr`, dlc2_m02 `trg_clapboard_01`. The route tables say which trigger each wait needs, and the hazard lists say which to avoid.
2. **The runner makes one move per mission.** m06, m10, m15 and m19 got the first trigger right, then stalled at the next one.
3. **Not a route problem; engine-side:** cellphone cb2 (m03, m13, m14, mm_p_07), zscene load (mm_p_01, mm_p_06), engine fade (dlc3_m03), customization UI (mm_m1_5).
4. **Not a route problem; time budget:** m08 (survive 60 s), m16 (about 50 s of intro timers), sh02 (15 s), m21 (5 s plus 3 s/6 s between waves), dlc3_m01 (8 s after the decoy location). These stalls sit in `delay`, which is why Team B reports `system_lib.lua:114`.
5. **Needs a specific action:** an exact vehicle (dlc3_m02), any vehicle (dlc2_m01, m04), a store purchase (mm_p_03), crate triggers (m09), sonic-gun damage (m18), slot-machine kills (sh04), and the tower hack with the player on foot (sh03).

### Batch 1 - the 30 missions suspended at session end in sweep 49 (FINAL)

#### dlc2_m01 (Team B stall: `dlc2_m01.lua:1694`, route step 3)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(dlc2_gis_intro)` | engine-driven (blocking helper) | `dlc2_m01_start:1127` |
| 3 | enter-vehicle | `any vehicle (cb registered on {LOCAL_PLAYER})` | on_vehicle_enter->dlc2_m01_entered_courtesy_vehicle_cb | `dlc2_m01_get_in_courtesy_vehicle_take1_process:1693` **<- sweep stall** |
| 4 | drive-to | `tgr_armory_parking_spot` | on_trigger->dlc2_m01_armory_parking_triggered_cb; cb checks vehicle | `dlc2_m01_go_to_armory_take1_process:1744` |
| 5 | go-to-trigger | `tgr_heli_pad_volume` | on_trigger->dlc2_m01_heli_pad_triggered_cb | `dlc2_m01_go_to_helipad_take1:1827` |
| 6 | go-to-trigger | `tgr_rescue_kwilanna` | on_trigger->dlc2_m01_resuce_kwilanna_cb | `dlc2_m01_rescue_kwilanna_take1_process:1878` |
| 7 | wait-flag | `Dlc2_m01_conversations.untie_kwilanna_take1` | set by script | `dlc2_m01_armory_take1_wrap:1915` |
| 8 | wait-flag | `Dlc2_m01_conversations.armory_take1_wrap` | set by script | `dlc2_m01_armory_take1_wrap:1974` |
| 10 | wait-flag | `Dlc2_m01_dialog_lines.armory_take2_action` | set by script | `dlc2_m01_go_to_armory_take2_process:2168` |
| 11 | go-to-trigger | `tgr_heli_pad_volume` | on_trigger->dlc2_m01_heli_pad_triggered_cb | `dlc2_m01_go_to_armory_take2_process:2214` |
| 12 | go-to-trigger | `tgr_rescue_kwilanna` | on_trigger->dlc2_m01_resuce_kwilanna_cb | `dlc2_m01_rescue_kwilanna_take2_process:2420` |
| 13 | wait-flag | `Dlc2_m01_conversations.player_heli_downed` | set by script | `dlc2_m01_process_armory_take2_wrap:2778` |
| 14 | wait-flag | `Dlc2_m01_conversations.director_cut_good` | set by script | `dlc2_m01_process_armory_take2_wrap:2810` |
| 15 | wait-thread | `dlc2_m01_keep_players_in_crash_area_thread` | thread started at dlc2_m01_process_armory_take2_wrap:2765 | `dlc2_m01_process_armory_take2_wrap:2814` |
| 16 | timed-wait | `delay(12.0)` | 12.0 s of game time, no player action | `dlc_m01_rail_sequence_dt_loop_02_process:3017` |
| 17 | wait-cutscene-or-phone | `thread_check_done` | engine-driven | `dlc_m01_rail_sequence_dt_loop_02_process:3027` |
| 18 | wait-cutscene-or-phone | `thread_check_done` | engine-driven | `dlc2_m01_alien_ambush_process:3142` |
| 19 | wait-flag | `(true)` | set by script | `dlc2_m01_alien_ambush_process:3166` |
| 20 | wait-cutscene-or-phone | `thread_check_done` | engine-driven | `dlc2_m01_alien_ambush_process:3207` |
| 21 | engine-wait | `group_create_check_done()` | engine/native predicate | `dlc_m01_rail_sequence_nw_loop_process:3477` |
| 22 | engine-wait | `group_create_check_done()` | engine/native predicate | `dlc_m01_rail_sequence_nw_loop_process:3485` |
| 23 | wait-cutscene-or-phone | `thread_check_done` | engine-driven | `dlc_m01_rail_sequence_nw_loop_process:3493` |
| 24 | wait-flag | `Dlc2_m01_conversations.jump_setup` | set by script | `dlc2_m01_jump_take1_process:3556` |
| 25 | wait-cutscene-or-phone | `thread_check_done` | engine-driven | `dlc2_m01_jump_take1_process:3575` |
| 26 | engine-wait | `vehicle_in_water()` | engine/native predicate | `dlc2_m01_jump_take1_process:3583` |
| 27 | wait-flag | `Dlc2_m01_conversations.jump_fail` | set by script | `dlc2_m01_jump_take1_process:3605` |
| 29 | timed-wait | `delay(5.0)` | 5.0 s of game time, no player action | `dlc2_m01_jump_take2_process:3709` |
| 30 | wait-cutscene-or-phone | `thread_check_done` | engine-driven | `dlc2_m01_jump_take2_process:3726` |
| 31 | wait-cutscene-or-phone | `thread_check_done` | engine-driven | `dlc2_m01_alien_finale_process:3774` |
| 32 | wait-flag | `Dlc2_m01_dialog_lines.mission_end` | set by script | `dlc2_m01_alien_finale_process:3788` |
| 33 | wait-flag | `Dlc2_m01_dialog_lines.mission_end_02` | set by script | `dlc2_m01_alien_finale_process:3800` |

4 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/dlc2_m01.tsv`.

Hand check: Stall = wait for **any vehicle** (on_vehicle_enter registered on each player; the callback sets `Dlc2_m01_runtime.entered_coutesy_vehicle`). The runner instead teleported into `tgr_heli_pad_volume`, which is not this step. Fix: seat the player in any vehicle so on_vehicle_enter fires.

#### dlc2_m02 (Team B stall: `dlc2_m02.lua:2435`, route step 5)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 2 | go-to-trigger | `Dlc2_m02_backstage_data[ 1 ].enter_trigger` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_wait_for_player_to_enter_trigger:2424` |
| 5 | go-to-trigger | `trg_loadout_1` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_wait_for_player_to_enter_trigger:2424` **<- sweep stall** |
| 8 | go-to-trigger | `trg_backstage_exit_1` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_wait_for_player_to_enter_trigger:2424` |
| 13 | go-to-trigger | `nav_machine_1` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_handle_machine:1316` |
| 14 | go-to-navpoint | `nav_machine_1` | dist >15 | `dlc2_m02_handle_machine:1324` |
| 17 | go-to-trigger | `nav_machine_1` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_homie_disable:1956` |
| 18 | go-to-navpoint | `npc_alien_homie -> nav_machine_1` | dist >3; mover=npc/vehicle npc_alien_homie | `dlc2_m02_homie_disable:1985` |
| 21 | go-to-trigger | `nav_machine_1` | on_trigger->dlc2_m02_on_trigger_machine | `dlc2_m02_player_disable_machine:1891` |
| 22 | wait-flag | `current_hit_points` | set in dlc2_m02_player_destroy_machine, dlc2_m02_homie_destroy_machine, dlc2_m02_obj_sh | `dlc2_m02_homie_destroy_machine:2104` |
| 23 | go-to-trigger | `nav_machine_1` | on_trigger->dlc2_m02_on_trigger_machine | `dlc2_m02_both_disable_machine:2723` |
| 24 | go-to-navpoint | `npc_alien_homie -> nav_machine_1` | dist >3; mover=npc/vehicle npc_alien_homie | `dlc2_m02_both_disable_machine:2733` |
| 27 | wait-flag | `current_hit_points` | set in dlc2_m02_player_destroy_machine, dlc2_m02_homie_destroy_machine, dlc2_m02_obj_sh | `dlc2_m02_player_destroy_machine:2063` |
| 30 | wait-flag | `Dlc2_m02_status.explosion_timer_complete` | set in dlc2_m02_machine_exploding_countdown | `dlc2_m02_handle_machine:1383` |
| 33 | go-to-trigger | `Dlc2_m02_triggers.machine_2` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_handle_machine:1316` |
| 34 | go-to-navpoint | `Dlc2_m02_triggers.machine_2` | dist >15 | `dlc2_m02_handle_machine:1324` |
| 37 | go-to-trigger | `Dlc2_m02_triggers.machine_2` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_homie_disable:1956` |
| 38 | go-to-navpoint | `npc_alien_homie -> Dlc2_m02_triggers.machine_2` | dist >3; mover=npc/vehicle npc_alien_homie | `dlc2_m02_homie_disable:1985` |
| 41 | go-to-trigger | `Dlc2_m02_triggers.machine_2` | on_trigger->dlc2_m02_on_trigger_machine | `dlc2_m02_player_disable_machine:1891` |
| 42 | wait-flag | `current_hit_points` | set in dlc2_m02_player_destroy_machine, dlc2_m02_homie_destroy_machine, dlc2_m02_obj_sh | `dlc2_m02_homie_destroy_machine:2104` |
| 43 | go-to-trigger | `Dlc2_m02_triggers.machine_2` | on_trigger->dlc2_m02_on_trigger_machine | `dlc2_m02_both_disable_machine:2723` |
| 44 | go-to-navpoint | `npc_alien_homie -> Dlc2_m02_triggers.machine_2` | dist >3; mover=npc/vehicle npc_alien_homie | `dlc2_m02_both_disable_machine:2733` |
| 47 | wait-flag | `current_hit_points` | set in dlc2_m02_player_destroy_machine, dlc2_m02_homie_destroy_machine, dlc2_m02_obj_sh | `dlc2_m02_player_destroy_machine:2063` |
| 50 | wait-flag | `Dlc2_m02_status.explosion_timer_complete` | set in dlc2_m02_machine_exploding_countdown | `dlc2_m02_handle_machine:1383` |
| 53 | go-to-trigger | `trg_backstage_enter_2` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_wait_for_player_to_enter_trigger:2424` |
| 56 | go-to-trigger | `trg_loadout_2` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_wait_for_player_to_enter_trigger:2424` |
| 59 | go-to-trigger | `trg_backstage_exit_2` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_wait_for_player_to_enter_trigger:2424` |
| 66 | go-to-trigger | `trg_backstage_exit_2` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_wait_for_player_to_enter_trigger:2424` |
| 71 | go-to-trigger | `nav_machine_3` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_handle_machine:1316` |
| 72 | go-to-navpoint | `nav_machine_3` | dist >15 | `dlc2_m02_handle_machine:1324` |
| 75 | go-to-trigger | `nav_machine_3` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_homie_disable:1956` |
| 76 | go-to-navpoint | `npc_alien_homie -> nav_machine_3` | dist >3; mover=npc/vehicle npc_alien_homie | `dlc2_m02_homie_disable:1985` |
| 79 | go-to-trigger | `nav_machine_3` | on_trigger->dlc2_m02_on_trigger_machine | `dlc2_m02_player_disable_machine:1891` |
| 80 | wait-flag | `current_hit_points` | set in dlc2_m02_player_destroy_machine, dlc2_m02_homie_destroy_machine, dlc2_m02_obj_sh | `dlc2_m02_homie_destroy_machine:2104` |
| 81 | go-to-trigger | `nav_machine_3` | on_trigger->dlc2_m02_on_trigger_machine | `dlc2_m02_both_disable_machine:2723` |
| 82 | go-to-navpoint | `npc_alien_homie -> nav_machine_3` | dist >3; mover=npc/vehicle npc_alien_homie | `dlc2_m02_both_disable_machine:2733` |
| 85 | wait-flag | `current_hit_points` | set in dlc2_m02_player_destroy_machine, dlc2_m02_homie_destroy_machine, dlc2_m02_obj_sh | `dlc2_m02_player_destroy_machine:2063` |
| 88 | wait-flag | `Dlc2_m02_status.explosion_timer_complete` | set in dlc2_m02_machine_exploding_countdown | `dlc2_m02_handle_machine:1383` |
| 91 | go-to-trigger | `Dlc2_m02_triggers.machine_4a` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_handle_machine:1316` |
| 92 | go-to-navpoint | `Dlc2_m02_triggers.machine_4a` | dist >15 | `dlc2_m02_handle_machine:1324` |
| 95 | go-to-trigger | `Dlc2_m02_triggers.machine_4a` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_homie_disable:1956` |
| 96 | go-to-navpoint | `npc_alien_homie -> Dlc2_m02_triggers.machine_4a` | dist >3; mover=npc/vehicle npc_alien_homie | `dlc2_m02_homie_disable:1985` |
| 99 | go-to-trigger | `Dlc2_m02_triggers.machine_4a` | on_trigger->dlc2_m02_on_trigger_machine | `dlc2_m02_player_disable_machine:1891` |
| 100 | wait-flag | `current_hit_points` | set in dlc2_m02_player_destroy_machine, dlc2_m02_homie_destroy_machine, dlc2_m02_obj_sh | `dlc2_m02_homie_destroy_machine:2104` |
| 101 | go-to-trigger | `Dlc2_m02_triggers.machine_4a` | on_trigger->dlc2_m02_on_trigger_machine | `dlc2_m02_both_disable_machine:2723` |
| 102 | go-to-navpoint | `npc_alien_homie -> Dlc2_m02_triggers.machine_4a` | dist >3; mover=npc/vehicle npc_alien_homie | `dlc2_m02_both_disable_machine:2733` |
| 105 | wait-flag | `current_hit_points` | set in dlc2_m02_player_destroy_machine, dlc2_m02_homie_destroy_machine, dlc2_m02_obj_sh | `dlc2_m02_player_destroy_machine:2063` |
| 108 | wait-flag | `Dlc2_m02_status.explosion_timer_complete` | set in dlc2_m02_machine_exploding_countdown | `dlc2_m02_handle_machine:1383` |
| 111 | go-to-trigger | `Dlc2_m02_triggers.machine_4b` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_handle_machine:1316` |
| 112 | go-to-navpoint | `Dlc2_m02_triggers.machine_4b` | dist >15 | `dlc2_m02_handle_machine:1324` |
| 115 | go-to-trigger | `Dlc2_m02_triggers.machine_4b` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_homie_disable:1956` |
| 116 | go-to-navpoint | `npc_alien_homie -> Dlc2_m02_triggers.machine_4b` | dist >3; mover=npc/vehicle npc_alien_homie | `dlc2_m02_homie_disable:1985` |
| 119 | go-to-trigger | `Dlc2_m02_triggers.machine_4b` | on_trigger->dlc2_m02_on_trigger_machine | `dlc2_m02_player_disable_machine:1891` |
| 120 | wait-flag | `current_hit_points` | set in dlc2_m02_player_destroy_machine, dlc2_m02_homie_destroy_machine, dlc2_m02_obj_sh | `dlc2_m02_homie_destroy_machine:2104` |
| 121 | go-to-trigger | `Dlc2_m02_triggers.machine_4b` | on_trigger->dlc2_m02_on_trigger_machine | `dlc2_m02_both_disable_machine:2723` |
| 122 | go-to-navpoint | `npc_alien_homie -> Dlc2_m02_triggers.machine_4b` | dist >3; mover=npc/vehicle npc_alien_homie | `dlc2_m02_both_disable_machine:2733` |
| 125 | wait-flag | `current_hit_points` | set in dlc2_m02_player_destroy_machine, dlc2_m02_homie_destroy_machine, dlc2_m02_obj_sh | `dlc2_m02_player_destroy_machine:2063` |
| 128 | wait-flag | `Dlc2_m02_status.explosion_timer_complete` | set in dlc2_m02_machine_exploding_countdown | `dlc2_m02_handle_machine:1383` |
| 131 | go-to-trigger | `Dlc2_m02_triggers.machine_4c` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_handle_machine:1316` |
| 132 | go-to-navpoint | `Dlc2_m02_triggers.machine_4c` | dist >15 | `dlc2_m02_handle_machine:1324` |
| 135 | go-to-trigger | `Dlc2_m02_triggers.machine_4c` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_homie_disable:1956` |
| 136 | go-to-navpoint | `npc_alien_homie -> Dlc2_m02_triggers.machine_4c` | dist >3; mover=npc/vehicle npc_alien_homie | `dlc2_m02_homie_disable:1985` |
| 139 | go-to-trigger | `Dlc2_m02_triggers.machine_4c` | on_trigger->dlc2_m02_on_trigger_machine | `dlc2_m02_player_disable_machine:1891` |
| 140 | wait-flag | `current_hit_points` | set in dlc2_m02_player_destroy_machine, dlc2_m02_homie_destroy_machine, dlc2_m02_obj_sh | `dlc2_m02_homie_destroy_machine:2104` |
| 141 | go-to-trigger | `Dlc2_m02_triggers.machine_4c` | on_trigger->dlc2_m02_on_trigger_machine | `dlc2_m02_both_disable_machine:2723` |
| 142 | go-to-navpoint | `npc_alien_homie -> Dlc2_m02_triggers.machine_4c` | dist >3; mover=npc/vehicle npc_alien_homie | `dlc2_m02_both_disable_machine:2733` |
| 145 | wait-flag | `current_hit_points` | set in dlc2_m02_player_destroy_machine, dlc2_m02_homie_destroy_machine, dlc2_m02_obj_sh | `dlc2_m02_player_destroy_machine:2063` |
| 148 | wait-flag | `Dlc2_m02_status.explosion_timer_complete` | set in dlc2_m02_machine_exploding_countdown | `dlc2_m02_handle_machine:1383` |
| 151 | go-to-trigger | `nav_machine_4` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_handle_machine:1316` |
| 152 | go-to-navpoint | `nav_machine_4` | dist >15 | `dlc2_m02_handle_machine:1324` |
| 155 | go-to-trigger | `nav_machine_4` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_homie_disable:1956` |
| 156 | go-to-navpoint | `npc_alien_homie -> nav_machine_4` | dist >3; mover=npc/vehicle npc_alien_homie | `dlc2_m02_homie_disable:1985` |
| 159 | go-to-trigger | `nav_machine_4` | on_trigger->dlc2_m02_on_trigger_machine | `dlc2_m02_player_disable_machine:1891` |
| 160 | wait-flag | `current_hit_points` | set in dlc2_m02_player_destroy_machine, dlc2_m02_homie_destroy_machine, dlc2_m02_obj_sh | `dlc2_m02_homie_destroy_machine:2104` |
| 161 | go-to-trigger | `nav_machine_4` | on_trigger->dlc2_m02_on_trigger_machine | `dlc2_m02_both_disable_machine:2723` |
| 162 | go-to-navpoint | `npc_alien_homie -> nav_machine_4` | dist >3; mover=npc/vehicle npc_alien_homie | `dlc2_m02_both_disable_machine:2733` |
| 165 | wait-flag | `current_hit_points` | set in dlc2_m02_player_destroy_machine, dlc2_m02_homie_destroy_machine, dlc2_m02_obj_sh | `dlc2_m02_player_destroy_machine:2063` |
| 168 | wait-flag | `Dlc2_m02_status.explosion_timer_complete` | set in dlc2_m02_machine_exploding_countdown | `dlc2_m02_handle_machine:1383` |
| 171 | go-to-trigger | `trg_room4_escape` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_wait_for_player_to_enter_trigger:2424` |
| 174 | go-to-trigger | `trg_backstage_enter_3` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_wait_for_player_to_enter_trigger:2424` |
| 177 | go-to-trigger | `trg_loadout_3` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_wait_for_player_to_enter_trigger:2424` |
| 180 | go-to-trigger | `trg_backstage_exit_3` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_wait_for_player_to_enter_trigger:2424` |
| 185 | go-to-trigger | `trg_hanger_start_saucer_camera` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_wait_for_trigger_hit:2942` |
| 192 | go-to-trigger | `trg_saucer` | on_trigger->dlc2_m02_saucer_approach | `dlc2_m02_go_to_saucer:1448` |
| 195 | go-to-navpoint | `npc_brutina -> nav_brutina_mark_1` | dist >1; mover=npc/vehicle npc_brutina | `dlc2_m02_brutina:2966` |
| 198 | kill-target | `npc_brutina` | kill | `dlc2_m02_brutina:2974` |
| 201 | go-to-trigger | `trg_machine_approach_gen` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_wait_for_trigger_hit:2942` |
| 202 | go-to-trigger | `trg_machine_approach_gen` | object_is_in_trigger(LOCAL_PLAYER) | `dlc2_m02_obj_shut_down_forcefield:3001` |
| 205 | go-to-navpoint | `npc_alien_homie -> nav_generator` | dist >3; mover=npc/vehicle npc_alien_homie | `dlc2_m02_obj_shut_down_forcefield:3031` |
| 206 | timed-wait | `delay(5.25)` | 5.25 s of game time, no player action | `dlc2_m02_obj_shut_down_forcefield:3046` |
| 211 | wait-flag | `current_hit_points` | set in dlc2_m02_player_destroy_machine, dlc2_m02_homie_destroy_machine, dlc2_m02_obj_sh | `dlc2_m02_obj_shut_down_forcefield:3073` |
| 214 | engine-wait | `character_is_ragdolled()` | engine/native predicate | `dlc2_m02_obj_shut_down_forcefield:3103` |
| 215 | go-to-navpoint | `npc_alien_homie` | dist >2 | `dlc2_m02_obj_shut_down_forcefield:3112` |
| 218 | go-to-trigger | `trg_saucer` | on_trigger->dlc2_m02_saucer_approach | `dlc2_m02_escape_in_saucer:1476` |
| 219 | exit-vehicle | `veh_saucer` | player must be ON FOOT | `dlc2_m02_escape_in_saucer:1492` |
| 220 | timed-wait | `delay(5)` | 5 s of game time, no player action | `dlc2_m02_escape_in_saucer:1524` |

127 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/dlc2_m02.tsv`.

Hand check: Backstage room 1 has no `enter_trigger` (the field is nil, so the call is skipped at run time). The first live wait is the `trg_loadout_1` **poll** (`object_is_in_trigger`, no callback): the player must be *standing inside* the volume while the loop runs. Team B's auto-objective reacts only to on_trigger waits and teleported into `trg_clapboard_01`, which is not on the main route.

#### dlc3_m01 (Team B stall: `system_lib.lua:114 (delay)`, route step 5)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(dlc3_mm_in)` | engine-driven (blocking helper) | `dlc3_m01_start:1223` |
| 3 | go-to-trigger | `friendly_fire_north_trigger` | trigger table DLC3_M01_trigger.friendly_fire_north cb dlc3_m01_search_location_cb | `dlc3_m01_search_for_tag:1607` |
| 4 | go-to-trigger | `friendly_fire_trigger` | trigger table DLC3_M01_trigger.friendly_fire cb dlc3_m01_search_location_cb | `dlc3_m01_search_for_tag:1621` |
| 5 | timed-wait | `delay(8.0)` | 8.0 s of game time, no player action | `dlc3_m01_search_for_tag:1626` **<- sweep stall** |
| 8 | wait-flag | `hostage_taken` | set in dlc3_m01_interrogate_owner_taunt_thread, dlc3_m01_interrogate_target | `dlc3_m01_interrogate_target:2988` |
| 13 | go-to-trigger | `strip_club_trigger` | trigger table DLC3_M01_trigger.strip_club | `dlc3_m01_go_to_strip_club:1754` |
| 15 | wait-thread | `dlc3_m01_interrogate_owner_taunt_thread` | thread started at dlc3_m01_interrogate_owner:1782 | `dlc3_m01_interrogate_target:2988` |
| 19 | wait-flag | `num_objs_alive` (x2) | set in dlc3_m01_kill_list_wait_for_complete | `dlc3_m01_kill_list_wait_for_complete:2919` |
| 20 | wait-flag | `DLC3_M01_Strip_Club.players_outside[LOCAL_PLAYER] / DLC3_M01_Strip_Club.players_outside[RE` | set by script | `dlc3_m01_defend_area:1969` |
| 22 | wait-flag | `players_in_vehicle` | set in dlc3_m01_vehicle_rail_start | `dlc3_m01_vehicle_rail_start:2117` |
| 23 | wait-flag | `num_objs_alive` | set in dlc3_m01_kill_list_wait_for_complete | `dlc3_m01_kill_list_wait_for_complete:2919` |
| 24 | wait-flag | `path_success` | set in dlc3_m01_vehicle_rail_start | `dlc3_m01_vehicle_rail_start:2273` |
| 25 | wait-cutscene-or-phone | `thread_check_done` | engine-driven | `dlc3_m01_vehicle_rail_start:2314` |

13 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/dlc3_m01.tsv`.

Hand check: Step 3 accepts **either** `friendly_fire_north_trigger` (decoy) **or** `near_friendly_fire<001>` (line 1607: `while not ...friendly_fire_north.hit and not ...near_friendly_fire.hit`). The runner entered the decoy, so the main thread sits in `delay(8.0)` (line 1626) inside the step-4 loop, which waits for `friendly_fire_trigger`. Next action: go to `friendly_fire_trigger`.

#### dlc3_m02 (Team B stall: `dlc3_m02.lua:3831`, route step 2)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 2 | enter-vehicle | `start_truck` | needs vehicle | `dlc3_m02_get_in_truck:3825` **<- sweep stall** |
| 3 | go-to-navpoint | `car_trigger_ps02` | dist >120; mover=npc/vehicle car_trigger_ps02 | `dlc3_m02_go_to_ps_01:1794` |
| 4 | wait-cutscene-or-phone | `group_is_loaded` | engine-driven | `dlc3_m02_setup_peds:3277` |
| 5 | go-to-trigger | `car_trigger_ps02` | object_is_in_trigger(LOCAL_PLAYER) | `dlc3_m02_wait_for_ps_trigger:1810` |
| 6 | wait-cutscene-or-phone | `group_is_loaded` | engine-driven | `dlc3_m02_peds_reaction:3293` |
| 7 | go-to-navpoint | ` -> nav_pierce_sing02` | dist >2; mover=npc/vehicle  | `dlc3_m02_kill_guards_at_ps_01:1874` |
| 9 | timed-wait | `delay(8.0)` | 8.0 s of game time, no player action | `dlc3_m02_kill_guards_at_ps_01:1896` |
| 10 | timed-wait | `delay(5.0)` | 5.0 s of game time, no player action | `dlc3_m02_kill_guards_at_ps_01:1904` |
| 11 | wait-flag | `DLC3_M02_PROGRESS` | set in dlc3_m02_kill_guards_at_ps_02, dlc3_m02_kill_guards_at_ps_01 | `dlc3_m02_kill_guards_at_ps_01:1907` |
| 12 | wait-flag | `npcs_alive` | set in dlc3_m02_kill_list_wait_for_complete | `dlc3_m02_kill_list_wait_for_complete:2721` |
| 13 | enter-vehicle | `start_truck` | needs vehicle | `dlc3_m02_get_in_truck:3825` |
| 14 | go-to-navpoint | `car_trigger_ps01` | dist >120; mover=npc/vehicle car_trigger_ps01 | `dlc3_m02_go_to_ps_02:1538` |
| 15 | wait-cutscene-or-phone | `group_is_loaded` | engine-driven | `dlc3_m02_setup_peds:3277` |
| 16 | go-to-navpoint | `tagNav02` | dist >80; mover=npc/vehicle tagNav02 | `dlc3_m02_go_to_ps_02:1560` |
| 17 | go-to-trigger | `car_trigger_ps01` | object_is_in_trigger(LOCAL_PLAYER) | `dlc3_m02_wait_for_ps_trigger:1810` |
| 18 | go-to-navpoint | `car_trigger_ps01` | dist >120; mover=npc/vehicle car_trigger_ps01 | `dlc3_m02_go_to_ps_02:1538` |
| 19 | wait-cutscene-or-phone | `group_is_loaded` | engine-driven | `dlc3_m02_setup_peds:3277` |
| 20 | go-to-navpoint | `tagNav02` | dist >80; mover=npc/vehicle tagNav02 | `dlc3_m02_go_to_ps_02:1560` |
| 21 | go-to-trigger | `car_trigger_ps01` | object_is_in_trigger(LOCAL_PLAYER) | `dlc3_m02_wait_for_ps_trigger:1810` |
| 22 | wait-cutscene-or-phone | `group_is_loaded` | engine-driven | `dlc3_m02_peds_reaction:3293` |
| 24 | go-to-navpoint | ` -> nav_pierce_sing01` | dist >2; mover=npc/vehicle  | `dlc3_m02_kill_guards_at_ps_02:1605` |
| 26 | timed-wait | `delay(5.0)` | 5.0 s of game time, no player action | `dlc3_m02_kill_guards_at_ps_02:1629` |
| 27 | wait-flag | `DLC3_M02_PROGRESS` | set in dlc3_m02_kill_guards_at_ps_02, dlc3_m02_kill_guards_at_ps_01 | `dlc3_m02_kill_guards_at_ps_02:1643` |
| 28 | wait-cutscene-or-phone | `group_is_loaded` (x2) | engine-driven | `dlc3_m02_setup_attack_chopper:3063` |
| 29 | wait-flag | `dlc3_m02_tag_downed` | set in dlc3_m02_initialize_checkpoint, dlc3_m02_tag_run_away | `dlc3_m02_kill_guards_at_ps_02:1686` |
| 31 | kill-target | ` heli_table [*]` | on_vehicle_destroyed->dlc3_m02_heli_dead_cb | `dlc3_m02_kill_guards_at_ps_02:1728` |
| 32 | enter-vehicle | `start_truck` | needs vehicle | `dlc3_m02_get_in_truck:3825` |
| 33 | wait-cutscene-or-phone | `group_is_loaded` | engine-driven | `dlc3_m02_setup_powder:3307` |
| 34 | timed-wait | `delay(5.0)` | 5.0 s of game time, no player action | `dlc3_m02_go_to_powder:1983` |
| 35 | go-to-trigger | `car_trigger_pwdr` | object_is_in_trigger(LOCAL_PLAYER) | `dlc3_m02_wait_for_ps_trigger:1810` |
| 37 | kill-target | `{ms_attacker01, ms_attacker02, ms_attacker03, ms_attacker04, ms_attacker05}` | on_death->dlc3_m02_mstar_attacker_dead_cb | `dlc3_m02_powder_defend_tag:2059` |
| 38 | wait-thread | `dlc3_m02_tag_run_away` | thread started at dlc3_m02_kill_guards_at_ps_02:1713 | `dlc3_m02_revive_tag:2071` |
| 40 | wait-thread | `dlc3_m02_thread_powder_tag_behavior` | thread started at dlc3_m02_powder_defend_tag:2051 | `dlc3_m02_fight_tag:2107` |
| 41 | wait-flag | `Dlc3_m02_tag_calmed` | set in dlc3_m02_thread_powder_calm_tag | `dlc3_m02_fight_tag:2146` |
| 44 | go-to-navpoint | `brutella_truck -> brute_veh_dest` | dist >10; mover=npc/vehicle brutella_truck | `dlc3_m02_brute_moment:3450` |
| 46 | go-to-navpoint | `brutella -> brute_dest01` | dist >3; mover=npc/vehicle brutella | `dlc3_m02_brute_moment:3477` |
| 47 | go-to-navpoint | `cte_tag -> brute_dest` | dist >3; mover=npc/vehicle cte_tag | `dlc3_m02_brute_moment:3493` |
| 49 | engine-wait | `audio_any_conversation_playing()` | engine/native predicate | `dlc3_m02_run:1077` |

11 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/dlc3_m02.tsv`.

Hand check: Stall = enter the **specific** vehicle `start_truck` (`character_is_in_vehicle(LOCAL_PLAYER, truck)`, loop 3825-3832). Any other vehicle does not count. Team B made no auto-objective move.

#### dlc3_m03 (Team B stall: `game_lib.lua:1399 (fade_in_block)`, route step 4)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 2 | engine-wait | `ai_scripted_action_is_processing()` | engine/native predicate | `dlc3_m03_intro:2197` |
| 4 | wait-cutscene-or-phone | `fade_in_block()` | engine-driven (blocking helper) | `dlc3_m03_intro:2263` **<- sweep stall** |
| 8 | go-to-trigger | `Shaundis_ex_battle_start` | trigger table DLC3_M03_trigger.shaundis_ex_battle_start | `dlc3_m03_go_to_shaundis_ex:2357` |
| 9 | go-to-trigger | `Shaundis_ex_location` | trigger table DLC3_M03_trigger.goto_shaundis_ex | `dlc3_m03_go_to_shaundis_ex:2361` |
| 11 | kill-target | `{Apt_ng_veh_1, Apt_ng_veh_2, Apt_ng_veh_3, Apt_ng_veh_4}` | on_damage->dlc3_m03_shaundis_ex_vehicle_preemtive_attack_cb; damage, not necessarily kill | `dlc3_m03_kill_guards_at_ex:2453` |
| 12 | wait-flag | `npcs_alive` | set in dlc3_m03_kill_list_wait_for_complete | `dlc3_m03_kill_list_wait_for_complete:4372` |
| 14 | go-to-trigger | `Planet_saints_location` | trigger table DLC3_M03_trigger.goto_planet_saints | `dlc3_m03_go_to_planet_saints:2634` |
| 16 | timed-wait | `delay(10.0)` | 10.0 s of game time, no player action | `dlc3_m03_kill_guards_at_planet_saints:2739` |
| 17 | kill-target | `{{ps_mstar_1_veh, ps_mstar_2_veh, ps_mstar_3_veh}, {ps_mstar_4_veh, ps_mstar_5_veh, ps_mst` | on_damage->dlc3_m03_planet_saints_preemtive_attack_cb; damage, not necessarily kill | `dlc3_m03_kill_guards_at_planet_saints:2745` |
| 18 | timed-wait | `delay(10.0)` | 10.0 s of game time, no player action | `dlc3_m03_kill_guards_at_planet_saints:2752` |
| 19 | wait-flag | `npcs_alive` | set in dlc3_m03_kill_list_wait_for_complete | `dlc3_m03_kill_list_wait_for_complete:4372` |
| 21 | timed-wait | `delay(20.0)` | 20.0 s of game time, no player action | `dlc3_m03_go_to_bridge:2891` |
| 24 | go-to-trigger | `Bridge_battle_start` | trigger table DLC3_M03_trigger.bridge_battle_start | `dlc3_m03_go_to_bridge:2907` |
| 26 | engine-wait | `teleport_check_done()` | engine/native predicate | `dlc3_m03_tag_cte:3016` |
| 27 | engine-wait | `teleport_check_done()` | engine/native predicate | `dlc3_m03_tag_cte:3021` |
| 28 | engine-wait | `teleport_check_done()` | engine/native predicate | `dlc3_m03_tag_cte:3028` |
| 29 | engine-wait | `teleport_check_done()` | engine/native predicate | `dlc3_m03_tag_cte:3033` |
| 30 | wait-cutscene-or-phone | `character_is_ready` | engine-driven | `dlc3_m03_tag_cte:3039` |
| 32 | timed-wait | `delay(5.5)` | 5.5 s of game time, no player action | `dlc3_m03_tag_cte:3067` |
| 35 | wait-flag | `npcs_alive` | set in dlc3_m03_kill_list_wait_for_complete | `dlc3_m03_kill_list_wait_for_complete:4372` |
| 39 | go-to-trigger | `Heli_pickup_location` | trigger table DLC3_M03_trigger.goto_heli_rail | `dlc3_m03_get_to_heli:3258` |
| 41 | wait-flag | `done` | set in dlc3_m03_do_rail | `dlc3_m03_do_rail:3440` |

19 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/dlc3_m03.tsv`.

Hand check: Stall is an **engine fade** (`fade_in_block` after the intro teleport), not a route step. `Bridge_area_check_tgr`, which the runner entered, only toggles notoriety spawning (`DLC3_M03_player_in_bridge_area`) and is not an objective.

#### m03 (Team B stall: `m03.lua:1205`, route step 5)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(nil)` | engine-driven (blocking helper) | `m03_start:878` |
| 2 | go-to-navpoint | `Nav CP Start Local` | dist <10 | `m03_initialize_start:1160` |
| 4 | wait-cutscene-or-phone | `action_play_is_finished` | engine-driven | `m03_steal_a_car:1190` |
| 5 | wait-cutscene-or-phone | `phone call m03_phillipe_call` | cellphone cb2 (ended) | `m03_steal_a_car:1204` **<- sweep stall** |
| 6 | enter-vehicle | `any vehicle` | needs vehicle | `m03_steal_a_car:1217` |
| 7 | drive-to | `Trigger Weapon Store2` | trigger table M03_trigger.weapon_store2; follows enter-vehicle | `m03_go_to_weapon_store:1254` |
| 8 | engine-wait | `store_interface_is_active()` | engine/native predicate | `m03_weapon_upgrade_tutorial:1282` |
| 9 | go-to-trigger | `Trigger Armory Entrance` | trigger table M03_trigger.armory_entrance | `m03_go_to_armory:1344` |
| 10 | wait-flag | `completed_takeover` | set in m03_go_to_armory | `m03_go_to_armory:1369` |
| 11 | go-to-trigger | `Trigger Weapons Cache` | trigger table M03_trigger.weapons_cache | `m03_objective_find_the_weapons:1403` |
| 12 | timed-wait | `delay(6.0)` | 6.0 s of game time, no player action | `m03_objective_hold_your_ground:1475` |
| 13 | timed-wait | `delay(60.0)` | 60.0 s of game time, no player action | `m03_objective_hold_your_ground:1488` |
| 16 | wait-cutscene-or-phone | `cutscene_play(03_Z01)` | engine-driven (blocking helper) | `m03_cutscene_pierce_entrance:1510` |
| 18 | go-to-trigger | `Trigger Satellite Weapon` | trigger table M03_trigger.satellite_weapon | `m03_objective_protect_the_stash:1620` |
| 19 | timed-wait | `delay(10.0)` | 10.0 s of game time, no player action | `m03_objective_protect_the_stash:1643` |
| 20 | wait-cutscene-or-phone | `thread_check_done` | engine-driven | `m03_objective_protect_the_stash:1647` |
| 21 | kill-target | `{M03_tank_attack1, M03_tank_attack2, M03_tank_attack3}` | kill | `m03_objective_protect_the_stash:1683` |
| 22 | engine-wait | `Veh Chopper Bomb` | waits for NOT dead/destroyed (no player action) | `m01_restore_heli:2329` |
| 23 | engine-wait | `Veh Chopper Pierce Normal` | waits for NOT dead/destroyed (no player action) | `m01_restore_heli:2329` |
| 24 | engine-wait | `Veh Chopper Backup` | waits for NOT dead/destroyed (no player action) | `m01_restore_heli:2329` |
| 25 | go-to-navpoint | `Veh Chopper Pierce Normal -> Nav Heli Landing Pos` | dist <5; mover=npc/vehicle Veh Chopper Pierce Normal | `m03_objective_helicopter_ride:1830` |
| 26 | go-to-trigger | `Enter_Heli_trigger` | trigger table M03_trigger.enter_heli | `m03_objective_helicopter_ride:1846` |
| 28 | exit-vehicle | `any vehicle` | player must be ON FOOT | `m03_objective_helicopter_ride:1873` |
| 29 | exit-vehicle | `any vehicle` | player must be ON FOOT | `m03_objective_helicopter_ride:1879` |
| 30 | engine-wait | `vehicle_enter_teleport()` | engine/native predicate | `m03_objective_helicopter_ride:1905` |
| 31 | engine-wait | `vehicle_enter_teleport()` | engine/native predicate | `m03_objective_helicopter_ride:1911` |
| 32 | engine-wait | `vehicle_enter_teleport()` | engine/native predicate | `m03_objective_helicopter_ride:1915` |
| 33 | go-to-navpoint | `Veh Chopper Bomb -> Nav Shaundi House` | dist <25; mover=npc/vehicle Veh Chopper Bomb | `m03_objective_helicopter_ride:2081` |
| 34 | go-to-navpoint | `Veh Chopper Bomb -> Nav Shaundi House` | dist <20; mover=npc/vehicle Veh Chopper Bomb | `m03_objective_helicopter_ride:2089` |
| 35 | timed-wait | `delay(5)` | 5 s of game time, no player action | `m03_objective_helicopter_ride:2097` |

5 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/m03.tsv`.

Hand check: Stall = **Phillipe cellphone call** (`audio_play_for_mission_cellphone(..., "m03_call_complete_cb")`, cb2). No player action applies: the host must fire cb2 when the call ends (`spec-lua-api-behaviour.md` §12.2). The runner's earlier teleport to `Nav CP Start Local` (step 2, get_dist < 10) was correct. The car wait (step 6) comes after.

#### m04 (Team B stall: `m04.lua:507`, route step 5)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(m04_cte_01)` | engine-driven (blocking helper) | `m04_start:277` |
| 5 | enter-vehicle | `any vehicle` (x2) | needs vehicle | `m04_get_into_car:506` **<- sweep stall** |
| 6 | engine-wait | `store_interface_is_active()` | engine/native predicate | `m04_use_mechanic:604` |
| 8 | enter-vehicle | `any vehicle` | needs vehicle | `m04_play_sing_along:988` |
| 10 | timed-wait | `delay(7.0)` | 7.0 s of game time, no player action | `m04_play_sing_along:1017` |
| 11 | wait-flag | `Clothes_Store_Reached` | set in m04_reached_clothes2 | `m04_go_to_clothes_store:639` |
| 12 | interact | `` | on_purchase->m04_clothes_purchase_cb | `m04_buy_clothes:687` |
| 13 | engine-wait | `store_interface_is_active()` | engine/native predicate | `m04_buy_clothes:706` |
| 14 | kill-target | `{Morningstar Brute}[*]` | on_death->m04_enemy_killed | `must_kill_setup:1234` |
| 15 | go-to-trigger | `Notoriety_Trigger 001` | on_trigger->m04_reached_notoriety | `m04_return_to_safehouse:914` |
| 16 | wait-flag | `Safehouse_Reached` | set in m04_reached_safehouse2 | `m04_return_to_safehouse:919` |

5 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/m04.tsv`.

Hand check: Stall = get into **any vehicle** (`character_is_in_vehicle(LOCAL_PLAYER) or (REMOTE_PLAYER)`). The runner teleported into `Mechanic_Trigger 001/002`, which belong to later steps and had no callback registered yet.

#### m05 (Team B stall: `m05.lua:1153`, route step 4)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(05_IN)` | engine-driven (blocking helper) | `m05_start:420` |
| 3 | wait-flag | `M05_jumped` | set in m05_jump_local, m05_jump_remote | `m05_parachute_in:1140` |
| 4 | go-to-trigger | `m05_trigger_land` | on_trigger->m05_land_trigger_reached | `m05_parachute_in:1152` **<- sweep stall** |
| 5 | wait-cutscene-or-phone | `phone call M05_Landed` | cellphone cb2 (ended) | `m05_get_info:1183` |
| 6 | wait-flag | `hostage_taken` | set in m05_interrogate | `m05_interrogate:965` |
| 8 | engine-wait | `character_has_specific_human_shield()` | engine/native predicate | `m05_get_info:1208` |
| 9 | go-to-trigger | `m05_trigger_downstairs` | on_trigger->m05_trigger_reached | `m05_let_saints_in:1249` |
| 10 | go-to-trigger | `m05_trigger_grenades` | on_trigger->m05_trigger_reached | `m05_let_saints_in:1277` |
| 11 | go-to-trigger | `m05_trigger_barricade` | on_trigger->m05_barricade_triggered | `m05_let_saints_in:1301` |
| 12 | go-to-trigger | `m05_trigger_saints` | on_trigger->m05_trigger_reached | `m05_let_saints_in:1325` |
| 13 | timed-wait | `delay(15)` | 15 s of game time, no player action | `m05_let_saints_in:1352` |
| 16 | wait-flag | `num_targets` | set in m05_clear_penthouse | `m05_clear_penthouse:1420` |
| 17 | wait-thread | `m05_elevator_spawn_thread` | thread started at m05_clear_penthouse:1410 | `m05_clear_penthouse:1437` |
| 19 | go-to-trigger | `m05_trigger_chase` | on_trigger->m05_trigger_reached | `m05_get_on_heli:1534` |
| 20 | go-to-navpoint | `m05_heli_chase_host -> m05_nav_hover_host` | dist >10; mover=npc/vehicle m05_heli_chase_host | `m05_chase_heli:1612` |
| 21 | timed-wait | `delay(25)` | 25 s of game time, no player action | `m05_chase_heli:1632` |
| 22 | wait-thread | `m05_chase_thread` | thread started at m05_chase_heli:1617 | `m05_chase_heli:1638` |
| 23 | go-to-trigger | `m05_trigger_chase_end` | on_trigger->m05_trigger_reached | `m05_chase_heli:1663` |
| 24 | wait-flag | `hostage_taken` | set in m05_interrogate | `m05_interrogate:965` |
| 26 | kill-target | `m05_target_chase_host` | kill | `m05_chase_heli:1685` |
| 27 | wait-cutscene-or-phone | `phone call M05_Tell_Shaundi_Color` | cellphone cb2 (ended) | `m05_chase_heli:1697` |

6 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/m05.tsv`.

Hazards (never enter, or for `on_trigger_exit` never leave): `m05_trigger_kill` (on_trigger_exit -> `m05_trigger_kill_zone`, character_kill, line 721); `m05_trigger_left` (on_trigger_exit -> `m05_trigger_left`, mission_end_failure, line 447); `m05_trigger_lt_kill` (on_trigger_exit -> `m05_kill_lt`, character_kill, line 776).

Hand check: Stall = landing trigger `m05_trigger_land` after the jump (on_trigger -> `m05_land_trigger_reached`). The runner entered `m05_trigger_kill`. That is an **on_trigger_exit kill zone**: leaving it kills the player, so it must never be exited (see hazards).

#### m06 (Team B stall: `m06.lua:1099`, route step 8)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(06_in)` | engine-driven (blocking helper) | `m06_start:742` |
| 2 | engine-wait | `vehicle_enable_physics()` (x2) | engine/native predicate | `m06_spawn_ball:2061` |
| 3 | engine-wait | `vehicle_enter_teleport()` | engine/native predicate | `m06_ball_setup:2093` |
| 4 | engine-wait | `vehicle_enter_teleport()` | engine/native predicate | `m06_ball_setup:2098` |
| 6 | wait-cutscene-or-phone | `action_play_is_finished` | engine-driven | `m06_go_to_dock:1049` |
| 7 | go-to-trigger | `Dock_trigger` | trigger table M06_trigger.dock | `m06_go_to_dock:1059` |
| 8 | go-to-trigger | `notice_snipers` | trigger table M06_trigger.notice_snipers | `m06_kill_dock_guards:1098` **<- sweep stall** |
| 9 | kill-target | `{Dock_sniper 001, Dock_thug 007, Dock_thug 008, Dock_thug 009, Dock_thug 011, Dock_thug 00` | on_death->m06_dock_goon_dead_cb | `m06_kill_dock_guards:1106` |
| 10 | kill-target | `{Dock_sniper 001, Dock_thug 007, Dock_thug 008, Dock_thug 009, Dock_thug 011, Dock_thug 00` | on_death->m06_dock_goon_dead_cb | `m06_kill_dock_guards:1115` |
| 11 | kill-target | `{Dock_thug 002, Dock_thug 001, Dock_thug 018}` | on_death->m06_dock_goon_dead_cb | `m06_kill_dock_guards:1135` |
| 12 | kill-target | `{Dock_sniper 003, Dock_thug 003, Dock_thug 004, Dock_thug 006}` | on_death->m06_dock_goon_dead_cb | `m06_kill_dock_guards:1153` |
| 13 | kill-target | `{Dock_thug 013, Dock_thug 015, Dock_thug 017}` | on_death->m06_dock_goon_dead_cb | `m06_kill_dock_guards:1175` |
| 14 | go-to-trigger | `bomb_trigger` | trigger table M06_trigger.arm_bomb | `m06_set_bomb:1226` |
| 15 | go-to-trigger | `factory_breadcrumb_003` | on_trigger->m06_factory_breadcrumb3_cb | `m06_go_to_factory:1238` |
| 16 | wait-flag | `M06_group.factory_brute.brute_killed` | set in m06_brute_killed_cb | `m06_fight_through_factory:1319` |
| 17 | go-to-trigger | `Oleg_trigger` | trigger table M06_trigger.oleg_cutscene | `m06_fight_through_factory:1328` |
| 18 | wait-cutscene-or-phone | `cutscene_play(06_Mid)` | engine-driven (blocking helper) | `m06_meet_oleg:1390` |
| 19 | go-to-trigger | `factory_breadcrumb_003` | on_trigger->m06_factory_breadcrumb3_cb | `m06_go_to_top_floor:1412` |
| 20 | engine-wait | `vehicle_enable_physics()` | engine/native predicate | `m06_spawn_ball:2061` |
| 21 | go-to-trigger | `At_stairs_trigger 001` | trigger table M06_trigger.stairs1 | `m06_fight_through_top_floor:1477` |
| 22 | go-to-trigger | `At_office_trigger` | trigger table M06_trigger.office | `m06_fight_through_top_floor:1483` |
| 23 | go-to-trigger | `Phillipe_trigger` | trigger table M06_trigger.phillipe | `m06_fight_through_top_floor:1490` |
| 24 | wait-cutscene-or-phone | `cutscene_play(06_Z01)` | engine-driven (blocking helper) | `m06_watch_phillipe:1507` |
| 25 | engine-wait | `vehicle_enter_teleport()` | engine/native predicate | `m06_ball_setup:2093` |
| 26 | engine-wait | `vehicle_enter_teleport()` | engine/native predicate | `m06_ball_setup:2098` |
| 27 | timed-wait | `delay(10.0)` | 10.0 s of game time, no player action | `m06_ball_stop1:1544` |
| 28 | timed-wait | `delay(10.5)` | 10.5 s of game time, no player action | `m06_ball_stop1:1546` |
| 29 | engine-wait | `vehicle_enable_physics()` | engine/native predicate | `m06_play_next_ball_animations:2443` |
| 31 | timed-wait | `delay(6.0)` | 6.0 s of game time, no player action | `m06_ball_stop_brute:1608` |
| 32 | engine-wait | `vehicle_enable_physics()` | engine/native predicate | `m06_play_next_ball_animations:2443` |
| 34 | wait-cutscene-or-phone | `action_play_is_finished` | engine-driven | `m06_scrape_off_brute:1662` |
| 35 | terminal | `(endless loop)` | never exits; mission ends via failure/death callback | `m06_scrape_off_brute:1669` |
| 36 | wait-cutscene-or-phone | `cutscene_play(06_Z_ballsmash)` | engine-driven (blocking helper) | `m06_ball_stop2:1715` |
| 38 | go-to-trigger | `bomb_disarm` | on_trigger->m06_choice_disarm_cb | `m06_final_decision:1798` |

4 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/m06.tsv`.

Hand check: The runner correctly entered `Dock_trigger` (step before), then performed no further move. Stall = next trigger `notice_snipers` (on_trigger -> `m06_notice_snipers_cb`). Fix: keep walking the route, not one teleport per mission.

#### m08 (Team B stall: `system_lib.lua:114 (delay)`, route step 3)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(08_in)` | engine-driven (blocking helper) | `m08_start:358` |
| 3 | timed-wait | `delay(60)` | 60 s of game time, no player action | `m08_survive_initial_assault:733` **<- sweep stall** |
| 6 | engine-wait | `human_is_downed()` | engine/native predicate | `m08_protect_pierce:828` |
| 7 | timed-wait | `delay(5)` | 5 s of game time, no player action | `m08_protect_pierce:834` |
| 11 | kill-target | `sniper` | on_death->m08_sniper_died_cb | `m08_kill_snipers:908` |
| 12 | go-to-trigger | `Trigger Rooftop` | on_trigger->m08_trigger_reached | `m08_kill_snipers:959` |
| 13 | kill-target | `sniper` | on_death->m08_sniper_died_cb | `m08_kill_snipers:987` |
| 15 | go-to-trigger | `Trigger Circuit Breaker` | on_trigger->m08_power_trigger_activated | `m08_restore_power:1025` |
| 16 | wait-cutscene-or-phone | `phone call m08_convo_5` | cellphone cb2 (ended) | `m08_cellphone_call_wait:1078` |
| 17 | kill-target | `sniper` | on_death->m08_sniper_died_cb | `m08_kill_helis:1128` |
| 18 | kill-target | `sniper` | on_death->m08_sniper_died_cb | `m08_kill_helis:1163` |

7 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/m08.tsv`.

Hazards (never enter, or for `on_trigger_exit` never leave): `Trigger Massacre` (on_trigger -> `m08_massacre_cb`, character_kill, line 609).

Hand check: Stall = `delay(60)` = **survive 60 s** (`m08_survive_initial_assault`, line 733). This is a time budget, not a position. The runner entered `Trigger Massacre`, a **hazard**: its callback `character_kill`s anyone on foot inside.

#### m09 (Team B stall: `m09.lua:2181`, route step 4)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(09_in)` | engine-driven (blocking helper) | `m09_start:1839` |
| 3 | wait-flag | `M09_local_on_boat / M09_remote_on_boat` | set by script | `m09_go_to_boat:2098` |
| 4 | wait-flag | `crate_set / M09_found_girl_crates / num_girl_crates` | set by script | `m09_find_girls:2180` **<- sweep stall** |
| 7 | go-to-trigger | `North_vantage_point_trigger` | trigger table M09_trigger.north_vantage cb m09_north_vantage_cb | `m09_defend_girls:2221` |
| 8 | wait-flag | `waves_done` | set by script | `m09_defend_girls:2284` |
| 9 | wait-flag | `M09_enemies_on_the_boat` | set by script | `m09_defend_girls:2308` |
| 10 | go-to-trigger | `heli_entrance_trigger` | trigger table M09_trigger.heli_entrance cb m09_heli_entrance_trigger_cb | `m09_call_the_helis:2360` |
| 17 | go-to-trigger | `to_saints_trigger` | trigger table M09_trigger.to_saints_trigger cb m09_set_trigger_hit_cb | `m09_truck_route:2770` |
| 18 | wait-flag | `m09_player_in_truck()` | script predicate | `m09_get_into_truck:2720` |

11 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/m09.tsv`.

Hand check: Stall = crate set 1 (loop 2179-2182). Exits when every crate in the set is opened, or when the girl crates are found. Open a crate by entering its trigger (on_trigger -> `m09_trigger_crate_cb` opens the door). Set 1: `Girls_trigger 001`, `Ammo_Trigger 001`, `Ammo_Trigger 002`, `Gag_Trigger 001`. Set 2: `Brute_Trigger 001`, `Gag_Trigger 002`, `Gag_Trigger 003`, `Girls_trigger 002`. Set 3: `Girls_trigger 003`, `Girls_trigger 004`, `Ammo_Trigger 003`, `Minigun_Brute_Trigger 001`. The runner entered `on_boat_trigger` and the `dist_warn`/`dist_fail` triggers instead.

#### m10 (Team B stall: `m10.lua:1475`, route step 5)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(10_in)` | engine-driven (blocking helper) | `m10_start:1079` |
| 4 | go-to-trigger | `tgr_club_door 001` | on_trigger->m10_player_reached_objective_cb | `m10_run_get_to_the_front_door:1464` |
| 5 | go-to-trigger | `tgr_front_door 001` | on_trigger->m10_player_reached_objective_cb | `m10_run_get_to_the_front_door:1474` **<- sweep stall** |
| 6 | go-to-trigger | `tgr_front_door 001` | on_trigger->m10_player_reached_objective_cb | `m10_run_get_to_the_front_door:1489` |
| 7 | wait-cutscene-or-phone | `cutscene_play(10_Mid)` | engine-driven (blocking helper) | `m10_run_lobby_cutscene:1575` |
| 8 | wait-cutscene-or-phone | `thread_check_done` | engine-driven | `m10_run_rooftop_intro:1631` |
| 10 | engine-wait | `ai_scripted_action_is_complete()` | engine/native predicate | `m10_run_rooftop_intro:1640` |
| 11 | wait-cutscene-or-phone | `vehicle_pathfind_check_done` | engine-driven | `m10_vtol_transport_drop_off_thread:3528` |
| 12 | wait-thread | `m10_vtol_transport_drop_off_thread` | thread started at m10_run_rooftop_intro:1627 | `m10_vtol_transport_drop_off_thread:3597` |
| 13 | wait-thread | `m10_vtol_transport_drop_off_thread` | thread started at m10_run_rooftop_intro:1627 | `m10_vtol_transport_drop_off_thread:3609` |
| 14 | kill-target | `Group_stag_transport 002` | on_death->m10_stag_transport_soldier_killed_cb | `m10_run_vtol_transports_01:1741` |
| 15 | kill-target | `Group_stag_transpot 002b` | on_death->m10_stag_transport_soldier_killed_cb | `m10_run_vtol_transports_01:1753` |
| 17 | kill-target | `Group_stag_transpot 002b` | on_death->m10_stag_transport_soldier_killed_cb | `m10_run_vtol_transports_01:1760` |
| 18 | wait-cutscene-or-phone | `vehicle_pathfind_check_done` | engine-driven | `m10_vtol_transport_drop_off_thread:3528` |
| 19 | wait-thread | `m10_vtol_transport_drop_off_thread` | thread started at m10_run_rooftop_intro:1627 | `m10_vtol_transport_drop_off_thread:3597` |
| 20 | wait-thread | `m10_vtol_transport_drop_off_thread` | thread started at m10_run_rooftop_intro:1627 | `m10_vtol_transport_drop_off_thread:3609` |
| 21 | wait-flag | `total_soldiers` | set in m10_run_vtol_transports_01, m10_run_rooftop_attack_vtols, m10_solider_count_thre | `m10_run_vtol_transports_01:1806` |
| 22 | wait-flag | `total_soldiers` | set in m10_run_vtol_transports_01, m10_run_rooftop_attack_vtols, m10_solider_count_thre | `m10_run_vtol_transports_01:1823` |
| 24 | timed-wait | `delay(7.0)` | 7.0 s of game time, no player action | `m10_run_rooftop_attack_vtols:2034` |
| 25 | timed-wait | `delay(5.0)` | 5.0 s of game time, no player action | `m10_run_rooftop_attack_vtols:2045` |
| 26 | wait-flag | `M10_runtime.roof_attack_vtols_active` | set in m10_setup_stag_attack_vtol | `m10_run_rooftop_attack_vtols:2049` |
| 27 | wait-thread | `m10_solider_count_thread` | thread started at m10_run_rooftop_attack_vtols:2031 | `m10_run_rooftop_attack_vtols:2057` |
| 29 | engine-wait | `ai_scripted_action_is_processing()` | engine/native predicate | `m10_run_oleg_gets_downed:2120` |
| 30 | wait-flag | `break_loop` | set in m10_run_oleg_gets_downed | `m10_run_oleg_gets_downed:2133` |
| 31 | wait-flag | `char_oleg 001` | on_revived->m10_oleg_revived_cb | `m10_run_oleg_gets_downed:2171` |
| 32 | engine-wait | `ai_scripted_action_is_complete()` | engine/native predicate | `m10_run_oleg_jump_on_vtol:2235` |
| 33 | engine-wait | `ai_scripted_action_is_processing()` | engine/native predicate | `m10_run_oleg_jump_on_vtol:2237` |
| 34 | wait-flag | `success` | set in m10_run_oleg_jump_on_vtol | `m10_run_oleg_jump_on_vtol:2257` |
| 35 | wait-flag | `elapsed_time` | set in m10_process_oleg_vtol_panic_thread | `m10_process_oleg_vtol_panic_thread:2344` |
| 36 | engine-wait | `vehicle_anim_finished()` | engine/native predicate | `m10_process_oleg_vtol_panic_thread:2362` |
| 37 | go-to-trigger | `tgr_rooftop_elevator 001` | on_trigger->m10_player_reached_objective_cb | `m10_run_rooftop_escape:2410` |
| 40 | go-to-trigger | `tgr_front_door 001` | on_trigger->m10_player_reached_objective_cb | `m10_run_escape_club_aftermath:2547` |
| 41 | go-to-trigger | `tgr_club_exit 001` | on_trigger->m10_player_reached_objective_cb | `m10_run_escape_club_aftermath:2558` |
| 42 | go-to-trigger | `tgr_saints_hq 001` | on_trigger->m10_player_reached_objective_cb | `m10_run_escape_to_saints_hq:2589` |

8 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/m10.tsv`.

Hazards (never enter, or for `on_trigger_exit` never leave): `tgr_roof_kill_region 001` (on_trigger -> `m10_roof_kill_region_cb`, character_kill, line 2808); `vtol_table.rooftop_trigger` (on_trigger_exit -> `m10_rooftop_trigger_exit_cb`, character_kill, line 1926).

Hand check: The runner correctly entered `tgr_club_door 001` (previous step). Stall = `tgr_front_door 001` (loop also exits when `ambush_soldiers_alive` reaches 0).

#### m13 (Team B stall: `m13.lua:4189`, route step 4)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(13_in)` | engine-driven (blocking helper) | `m13_start:1230` |
| 3 | go-to-trigger | `trigger_pr_center_front` | on_trigger->m13_trigger_cback | `m13_goto_a_trigger:2954` |
| 4 | wait-cutscene-or-phone | `phone call M13_Banners` | cellphone cb2 (ended) | `m13_cellphone_call_wait:4188` **<- sweep stall** |
| 5 | kill-target | `{mover_mission_accomplished 001, mover_mission_accomplished 002}` | on_take_damage->m13_banner_damaged; damage, not necessarily kill | `m13_destroy_banner:1897` |
| 6 | kill-target | `{mover_mission_accomplished 001, mover_mission_accomplished 002}` | on_mover_destroyed->m13_banner_destroyed | `m13_destroy_banner:1911` |
| 7 | kill-target | `{mover_mission_accomplished 001, mover_mission_accomplished 002}` | on_mover_destroyed->m13_banner_destroyed | `m13_destroy_banner:1920` |
| 8 | wait-cutscene-or-phone | `phone call {M13_Return_To_Crib}.name.name` | cellphone cb2 (ended) | `m13_cellphone_call_wait:4188` |
| 9 | wait-cutscene-or-phone | `phone call {M13_Return_To_Crib}.name` | cellphone cb2 (ended) | `m13_cellphone_call_wait:4188` |
| 11 | go-to-trigger | `trigger_saints_hq_front` | on_trigger->m13_trigger_cback | `m13_goto_a_trigger:2954` |
| 12 | go-to-trigger | `trigger_elevator_proximity` | on_trigger->m13_trigger_cback | `m13_goto_a_trigger:2954` |
| 15 | wait-cutscene-or-phone | `phone call M13_Wave_One_Start` | cellphone cb2 (ended) | `m13_cellphone_call_wait:4188` |
| 16 | wait-thread | `m13_wave_thread` | thread started at m13_start_wave:3450 | `m13_wait_for_waves_to_complete_state:3545` |
| 17 | wait-cutscene-or-phone | `phone call M13_Use_Car_Bombs_01` | cellphone cb2 (ended) | `m13_cellphone_call_wait:4188` |
| 18 | wait-cutscene-or-phone | `phone call M13_Use_Car_Bombs_02` | cellphone cb2 (ended) | `m13_cellphone_call_wait:4188` |
| 19 | wait-flag | `north_side` | set in m13_scene_1 | `m13_flag_closest_car_bomb:2573` |
| 20 | timed-wait | `delay(30.0)` | 30.0 s of game time, no player action | `m13_scene_1:2688` |
| 21 | wait-flag | `wave.thread` | set in m13_start_wave | `m13_wait_for_required_kills:3522` |
| 22 | wait-cutscene-or-phone | `phone call M13_Focus_On_South_Side` | cellphone cb2 (ended) | `m13_cellphone_call_wait:4188` |
| 23 | wait-thread | `m13_wave_thread` | thread started at m13_start_wave:3450 | `m13_wait_for_waves_to_complete_state:3545` |
| 24 | wait-cutscene-or-phone | `phone call M13_Use_Car_Bombs_01` | cellphone cb2 (ended) | `m13_cellphone_call_wait:4188` |
| 25 | wait-cutscene-or-phone | `phone call M13_Use_Car_Bombs_02` | cellphone cb2 (ended) | `m13_cellphone_call_wait:4188` |
| 26 | wait-flag | `north_side` | set in m13_scene_1 | `m13_flag_closest_car_bomb:2573` |
| 27 | timed-wait | `delay(15.0)` | 15.0 s of game time, no player action | `m13_scene_3:2747` |
| 28 | wait-cutscene-or-phone | `phone call M13_Snipers_On_South_01` | cellphone cb2 (ended) | `m13_cellphone_call_wait:4188` |
| 29 | wait-flag | `wave.thread` | set in m13_start_wave | `m13_wait_for_required_kills:3522` |
| 30 | wait-cutscene-or-phone | `phone call M13_Tank_On_North_01` | cellphone cb2 (ended) | `m13_cellphone_call_wait:4188` |
| 31 | wait-flag | `wave.thread` | set in m13_start_wave | `m13_wait_for_required_kills:3522` |
| 32 | wait-cutscene-or-phone | `vehicle_pathfind_check_done` | engine-driven | `m13_pierce_drive_in:2390` |
| 33 | wait-cutscene-or-phone | `phone call M13_Elevator` | cellphone cb2 (ended) | `m13_cellphone_call_wait:4188` |
| 34 | go-to-trigger | `trigger_elevator_to_ground` | on_trigger->m13_trigger_cback | `m13_goto_a_trigger:2954` |
| 37 | wait-cutscene-or-phone | `phone call M13_In_trouble_02` | cellphone cb2 (ended) | `m13_cellphone_call_wait:4188` |
| 38 | terminal | `(endless loop)` | never exits; mission ends via failure/death callback | `m13_save_pierce:2456` |
| 39 | wait-cutscene-or-phone | `phone call {M13_Thanks}.name.name` | cellphone cb2 (ended) | `m13_cellphone_call_wait:4188` |
| 40 | wait-cutscene-or-phone | `phone call {M13_Thanks}.name` | cellphone cb2 (ended) | `m13_cellphone_call_wait:4188` |
| 42 | go-to-trigger | `trigger_elevator_proximity` | on_trigger->m13_trigger_cback | `m13_goto_a_trigger:2954` |
| 45 | wait-cutscene-or-phone | `phone call {M13_Laser_Target}.name.name` | cellphone cb2 (ended) | `m13_cellphone_call_wait:4188` |
| 46 | wait-cutscene-or-phone | `phone call {M13_Laser_Target}.name` | cellphone cb2 (ended) | `m13_cellphone_call_wait:4188` |
| 48 | wait-flag | `wave.thread` | set in m13_start_wave | `m13_wait_for_required_kills:3522` |
| 49 | wait-cutscene-or-phone | `phone call {M13_Helicopters}.name.name` | cellphone cb2 (ended) | `m13_cellphone_call_wait:4188` |
| 50 | wait-cutscene-or-phone | `phone call {M13_Helicopters}.name` | cellphone cb2 (ended) | `m13_cellphone_call_wait:4188` |
| 52 | wait-flag | `wave.thread` (x2) | set in m13_start_wave | `m13_wait_for_required_kills:3522` |
| 53 | wait-cutscene-or-phone | `phone call M13_STAG_Inside_Oleg` (x2) | cellphone cb2 (ended) | `m13_cellphone_call_wait:4188` |
| 55 | wait-flag | `wave.thread` | set in m13_start_wave | `m13_wait_for_required_kills:3522` |
| 56 | wait-cutscene-or-phone | `phone call M13_Losing_Viola` (x2) | cellphone cb2 (ended) | `m13_cellphone_call_wait:4188` |
| 58 | wait-cutscene-or-phone | `phone call {M13_Retreating}.name.name` | cellphone cb2 (ended) | `m13_cellphone_call_wait:4188` |
| 59 | wait-cutscene-or-phone | `phone call {M13_Retreating}.name` | cellphone cb2 (ended) | `m13_cellphone_call_wait:4188` |
| 61 | go-to-trigger | `trigger_elevator_to_penthouse` | on_trigger->m13_trigger_cback | `m13_goto_a_trigger:2954` |
| 64 | go-to-trigger | `trigger_penthouse_cutscene` | on_trigger->m13_trigger_cback | `m13_goto_a_trigger:2954` |
| 65 | wait-cutscene-or-phone | `cutscene_play(13_Z01)` | engine-driven (blocking helper) | `m13_penthouse_cutscene:1948` |
| 66 | wait-cutscene-or-phone | `character_is_ready` | engine-driven | `m13_penthouse_cutscene:1959` |
| 67 | wait-flag | `M13_josh_given_to_cyrus` | set in m13_josh_thrown_to_cyrus | `m13_player_decision:2080` |
| 68 | engine-wait | `ai_scripted_action_is_processing()` | engine/native predicate | `m13_player_decision:2140` |
| 69 | wait-flag | `r` | set in m13_cyrus_enter_vtol, m13_create_clutter | `m13_cyrus_enter_vtol:2162` |
| 70 | wait-flag | `r` | set in m13_cyrus_enter_vtol, m13_create_clutter | `m13_cyrus_enter_vtol:2167` |
| 71 | leave-trigger | `trigger_penthouse_jumped` | on_trigger_exit->m13_jumped_off_penthouse | `m13_player_leave_penthouse:2227` |

16 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/m13.tsv`.

Hand check: Stall = **cellphone call** cb2 (`m13_cellphone_call_cb` clears `cellphone_call_in_progress`). This is engine-side, with no route action. The runner had correctly entered `trigger_pr_center_front` first.

#### m14 (Team B stall: `m14.lua:2265`, route step 3)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(14_in)` | engine-driven (blocking helper) | `m14_start:365` |
| 3 | wait-cutscene-or-phone | `phone call M14_Mission_Start` | cellphone cb2 (ended) | `m14_cellphone_call_wait:2264` **<- sweep stall** |
| 4 | go-to-navpoint | `?` | dist <60 | `m14_objective_go_to_heli_smiling_jacks:628` |
| 6 | enter-vehicle | `` | needs vehicle | `m14_wait_for_player_to_enter_vehicle:1869` |
| 7 | enter-vehicle | `` | needs vehicle | `m14_wait_for_player_to_enter_vehicle:1873` |
| 9 | go-to-trigger | `Trigger Heli Validation 001` | object_is_in_trigger() | `m14_objective_go_to_first_tower:721` |
| 11 | go-to-trigger | `Trigger Tower 001` | on_trigger->m14_transmitter_placement_started_cb | `m14_objective_plant_first_transmitter:799` |
| 12 | enter-vehicle | `` | needs vehicle | `m14_wait_for_player_to_enter_vehicle:1869` |
| 13 | enter-vehicle | `` | needs vehicle | `m14_wait_for_player_to_enter_vehicle:1873` |
| 14 | go-to-navpoint | `Nav T2 Helipad` | dist <25; mover=npc/vehicle Nav T2 Helipad | `m14_objective_go_to_second_tower:875` |
| 16 | go-to-trigger | `Trigger Heli Validation 002` | object_is_in_trigger() | `m14_objective_go_to_second_tower:893` |
| 17 | go-to-trigger | `Trigger Tower 002` | on_trigger->m14_transmitter_placement_started_cb | `m14_objective_plant_second_transmitter:947` |
| 19 | enter-vehicle | `` | needs vehicle | `m14_wait_for_player_to_enter_vehicle:1869` |
| 20 | enter-vehicle | `` | needs vehicle | `m14_wait_for_player_to_enter_vehicle:1873` |
| 22 | wait-flag | `target_progress` | set in m14_objective_target_the_van | `m14_objective_target_the_van:1057` |
| 24 | go-to-navpoint | `Trigger Helipad` | dist <100; mover=npc/vehicle Trigger Helipad | `m14_objective_go_to_interview:1271` |
| 28 | go-to-trigger | `Trigger Helipad` | on_trigger->m14_helipad_reached_cb | `m14_objective_go_to_interview:1285` |
| 29 | go-to-trigger | `Trigger Interview Stairs` | on_trigger->m14_interview_stairs_reached_cb | `m14_objective_enter_building:1305` |
| 31 | kill-target | `{NPC Decker Studio 001, NPC Decker Studio 002, NPC Decker Studio 003, NPC Decker Studio 00` | on_death->m14_remove_marker_cb | `m14_objective_clear_deckers:1394` |
| 32 | kill-target | `{NPC Decker Elevator RB 001, NPC Decker Elevator RB 002}` | on_death->m14_remove_marker_cb | `m14_objective_clear_deckers:1432` |
| 33 | kill-target | `{NPC Decker Side RB 001}` | on_death->m14_remove_marker_cb | `m14_objective_clear_deckers:1495` |
| 34 | kill-target | `{NPC Decker Stairs RB 001, NPC Decker Stairs RB 002, NPC Decker Stairs RB 003}` | on_death->m14_remove_marker_cb | `m14_objective_clear_deckers:1530` |
| 35 | wait-thread | `m14_abandon_heli_thread` | thread started at m14_initialize_checkpoint:1783 | `m14_objective_talk_to_jane:1552` |
| 37 | go-to-trigger | `Trigger Stairs1` | on_trigger->m14_helipad_reached_cb | `m14_objective_get_in_heli_chase_killbane:1577` |
| 38 | go-to-trigger | `Trigger Stairs2` | on_trigger->m14_helipad_reached_cb | `m14_objective_get_in_heli_chase_killbane:1584` |
| 39 | go-to-trigger | `Trigger Helipad` | on_trigger->m14_helipad_reached_cb | `m14_objective_get_in_heli_chase_killbane:1591` |
| 40 | enter-vehicle | `` | needs vehicle | `m14_wait_for_player_to_enter_vehicle:1869` |
| 41 | enter-vehicle | `` | needs vehicle | `m14_wait_for_player_to_enter_vehicle:1873` |
| 42 | timed-wait | `delay(5)` | 5 s of game time, no player action | `m14_objective_catch_killbane:1623` |
| 44 | wait-flag | `local_caught_kb` | set in m14_objective_catch_killbane | `m14_objective_catch_killbane:1629` |
| 45 | go-to-navpoint | `Veh Killbane Escape` | dist >55 | `m14_objective_catch_killbane:1635` |

14 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/m14.tsv`.

Hazards (never enter, or for `on_trigger_exit` never leave): `Trigger Studio Massacre` (on_trigger -> `m14_studio_massacre_cb`, character_kill, line 1290); `Trigger Studio Massacre` (on_trigger -> `m14_studio_massacre_cb`, character_kill, line 1361); `Trigger T1 Massacre` (on_trigger -> `m14_studio_massacre_cb`, character_kill, line 792); `Trigger T2 Massacre` (on_trigger -> `m14_studio_massacre_cb`, character_kill, line 944).

Hand check: Stall = **cellphone call** `M14_Mission_Start` (called from line 620, cb2 = `m14_cellphone_call_cb`). This is engine-side, with no route action.

#### m15 (Team B stall: `m15.lua:1013`, route step 4)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(15_in)` | engine-driven (blocking helper) | `m15_start:649` |
| 3 | go-to-trigger | `trigger_pr_center_radius` | on_trigger->m15_trigger_cback | `m15_goto_a_trigger:1012` |
| 4 | go-to-trigger | `trigger_pr_center_front` | on_trigger->m15_trigger_cback | `m15_goto_a_trigger:1012` **<- sweep stall** |
| 5 | go-to-trigger | `trigger_pr_center_enterance` | on_trigger->m15_trigger_cback | `m15_goto_a_trigger:1012` |
| 6 | timed-wait | `delay(5.0)` | 5.0 s of game time, no player action | `m15_run:722` |
| 7 | wait-flag | `M15_waves.lobby_stag.thread` | set in m15_setup_outside_garage, m15_close_roadblock, m15_chase_truck | `m15_run:727` |
| 8 | wait-flag | `M15_extra_npcs_in_lobby` | set by script | `m15_run:732` |
| 9 | go-to-trigger | `trigger_elevator_to_garage 002` | on_trigger->m15_trigger_cback | `m15_goto_a_trigger:1012` |
| 12 | kill-target | `vehicle_tank_garage` | kill | `m15_enter_vehicle:985` |
| 13 | go-to-trigger | `trigger_garage_door` | on_trigger->m15_trigger_cback | `m15_goto_a_trigger:1012` |
| 14 | timed-wait | `delay(5.0)` | 5.0 s of game time, no player action | `m15_chase:1700` |
| 15 | wait-flag | `m15_player_is_close_to_chase_start()` | script predicate | `m15_chase:1706` |
| 16 | wait-thread | `m15_convo_to_pr_center` | thread started at m15_run:672 | `m15_vehicle_follow_path:1914` |
| 17 | wait-flag | `in_progress` | set in m15_vehicle_follow_path | `m15_vehicle_follow_path:1924` |
| 18 | timed-wait | `delay(5.0)` | 5.0 s of game time, no player action | `m15_vehicle_follow_path:1947` |
| 19 | wait-thread | `m15_thread_chase_rubber_band` | thread started at m15_chase:1748 | `m15_vehicle_follow_path:1957` |

3 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/m15.tsv`.

Hand check: All four go-to steps share one helper loop (1012-1014). The runner entered `trigger_pr_center_radius` (step 3), so the stall is the **next** call, `trigger_pr_center_front`, followed by `trigger_pr_center_enterance`.

#### m16 (Team B stall: `system_lib.lua:114 (delay)`, route step 8)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(16_IN)` | engine-driven (blocking helper) | `m16_start:1170` |
| 6 | wait-cutscene-or-phone | `cutscene_play(m16_cte_01)` | engine-driven (blocking helper) | `m16_intro_sequence:1494` |
| 8 | timed-wait | `delay(25)` | 25 s of game time, no player action | `m16_intro_sequence:1508` **<- sweep stall** |
| 9 | timed-wait | `delay(10)` | 10 s of game time, no player action | `m16_intro_sequence:1519` |
| 10 | wait-flag | `safe_to_transform` | set in m16_transform_player_into_human_avatar | `m16_transform_player_into_human_avatar:2673` |
| 11 | wait-flag | `m16_players_are_ready_to_transform()` | script predicate | `m16_transform_player_into_human_avatar:2675` |
| 12 | timed-wait | `delay(5)` | 5 s of game time, no player action | `m16_transform_player_into_human_avatar:2681` |
| 13 | timed-wait | `delay(10)` | 10 s of game time, no player action | `m16_intro_sequence:1525` |
| 14 | go-to-trigger | `Gate1_Trigger` | on_trigger->m16_cb_firewall_area_reached | `m16_intro_sequence:1536` |
| 15 | go-to-trigger | `Trigger_Firewall` | on_trigger->m16_cb_firewall_reached | `m16_firewall_sequence:1549` |
| 17 | wait-flag | `UI_completion[ LOCAL_PLAYER ] / UI_completion[ REMOTE_PLAYER ]` | set by script | `m16_text_adventure_process:1660` |
| 19 | timed-wait | `delay(5.0)` | 5.0 s of game time, no player action | `m16_watch_firewall_disintegrate:1688` |
| 21 | go-to-trigger | `Gate2_Trigger` | on_trigger->m16_cb_antivirus_reached | `m16_goto_antivirus:1724` |
| 22 | wait-flag | `Process_enemy_set_cleared` (x8) | set by script | `m16_process_enemy_set:2972` |
| 24 | engine-wait | `human_is_downed()` | engine/native predicate | `m16_run:1215` |
| 26 | wait-flag | `m16_tank_combat_end_battle()` | script predicate | `m16_tank_combat_process:1884` |
| 27 | timed-wait | `delay(6)` | 6 s of game time, no player action | `m16_tank_combat_process:1910` |
| 30 | go-to-trigger | `Gate3_Trigger` | on_trigger->m16_cb_internet_security_reached | `m16_goto_internet_security:2018` |
| 31 | timed-wait | `delay(8)` | 8 s of game time, no player action | `m16_internet_security_process:2070` |
| 32 | wait-flag | `Process_enemy_set_cleared` (x2) | set by script | `m16_process_enemy_set:2972` |
| 33 | wait-flag | `UI_completion[ LOCAL_PLAYER ] / UI_completion[ REMOTE_PLAYER ]` | set by script | `m16_error_screen_process:2111` |
| 36 | go-to-trigger | `Final_Trigger` | on_trigger->m16_final_cb | `m16_goto_cathedral:2176` |
| 37 | wait-cutscene-or-phone | `zscene_is_loaded` | engine-driven | `m16_matt_intro_zscene:2542` |
| 38 | wait-cutscene-or-phone | `cutscene_play(16_z01)` | engine-driven (blocking helper) | `m16_matt_intro_zscene:2546` |
| 40 | wait-flag | `M16_conv.matt_battle_begins` | set by script | `m16_run_matt_phase_one:2276` |
| 42 | engine-wait | `character_exists()` | engine/native predicate | `m16_wait_for_matt_health:2187` |
| 43 | wait-thread | `m16_matt_health_thread` | thread started at m16_set_phase:2246 | `m16_wait_for_matt_health:2193` |
| 44 | wait-cutscene-or-phone | `action_play_is_finished` | engine-driven | `m16_matt_land:2591` |
| 45 | wait-cutscene-or-phone | `action_play_is_finished` | engine-driven | `m16_matt_land:2597` |
| 46 | interact | `` | on_qte_animation_trigger->m16_final_qte_fade_cb | `m16_wait_for_qte_complete:2211` |
| 47 | engine-wait | `character_exists()` | engine/native predicate | `m16_wait_for_matt_health:2187` |
| 48 | wait-thread | `m16_matt_health_thread` | thread started at m16_set_phase:2246 | `m16_wait_for_matt_health:2193` |
| 49 | wait-cutscene-or-phone | `action_play_is_finished` | engine-driven | `m16_matt_land:2591` |
| 50 | wait-cutscene-or-phone | `action_play_is_finished` | engine-driven | `m16_matt_land:2597` |
| 51 | engine-wait | `character_exists()` | engine/native predicate | `m16_wait_for_matt_health:2187` |
| 52 | wait-thread | `m16_matt_health_thread` | thread started at m16_set_phase:2246 | `m16_wait_for_matt_health:2193` |
| 53 | engine-wait | `character_exists()` | engine/native predicate | `m16_wait_for_matt_health:2187` |
| 54 | wait-thread | `m16_matt_health_thread` | thread started at m16_set_phase:2246 | `m16_wait_for_matt_health:2193` |
| 55 | wait-cutscene-or-phone | `action_play_is_finished` | engine-driven | `m16_prep_avatar_for_decker_battle:3329` |
| 56 | wait-cutscene-or-phone | `action_play_is_finished` | engine-driven | `m16_prep_avatar_for_decker_battle:3335` |
| 57 | timed-wait | `delay(45)` | 45 s of game time, no player action | `m16_run_decker_phase_four:2364` |
| 58 | engine-wait | `character_exists()` | engine/native predicate | `m16_wait_for_matt_health:2187` |
| 59 | wait-thread | `m16_matt_health_thread` | thread started at m16_set_phase:2246 | `m16_wait_for_matt_health:2193` |
| 60 | wait-flag | `m16_wait_for_qte_complete()` | script predicate | `m16_run_matt_phase_five:2438` |
| 61 | engine-wait | `character_exists()` | engine/native predicate | `m16_wait_for_matt_health:2187` |
| 62 | wait-thread | `m16_matt_health_thread` | thread started at m16_set_phase:2246 | `m16_wait_for_matt_health:2193` |
| 63 | wait-cutscene-or-phone | `zscene_is_loaded` | engine-driven | `m16_matt_defeated_zscene:2551` |
| 64 | wait-cutscene-or-phone | `cutscene_play(16_Z02)` | engine-driven (blocking helper) | `m16_matt_defeated_zscene:2554` |
| 66 | wait-thread | `m16_thread_play_convo` | thread started at m16_helper_conversation_play:3014 | `m16_player_choice:2517` |
| 67 | go-to-trigger | `Company_B_Trigger` | on_trigger->m16_player_choice_made_cb | `m16_player_choice:2523` |

17 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/m16.tsv`.

Hazards (never enter, or for `on_trigger_exit` never leave): `Death_Region` (on_trigger -> `m16_death_trigger`, character_kill, line 2720).

Hand check: Stall = scripted intro timers: `delay(25)` (1508), then `delay(10)` (1519), `delay(5)`, `delay(10)` (1525). That is about 50 s of game time before `Gate1_Trigger` is even armed: a time budget. The runner teleported into `Death_Region`, a **hazard** (`m16_death_trigger` calls `character_kill`).

#### m18 (Team B stall: `m18.lua:1593`, route step 3)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(18_in)` | engine-driven (blocking helper) | `m18_start:1108` |
| 3 | kill-target | `mov_cabin_door 001` | on_take_damage->m18_cb_damaged_cabin_door; damage, not necessarily kill | `m18_run_cargo_plane_open_cockpit:1592` **<- sweep stall** |
| 5 | kill-target | `mov_cockpit_door 001` | on_take_damage->m18_cb_damaged_cockpit_door; damage, not necessarily kill | `m18_run_cargo_plane_open_cockpit:1608` |
| 7 | timed-wait | `delay(5.0)` | 5.0 s of game time, no player action | `m18_run_cargo_plane_escape:1996` |
| 9 | go-to-trigger | `tgr_tank_region 001` | on_trigger->m18_cb_tank_region_entered | `m18_run_cargo_plane_escape:2001` |
| 10 | wait-cutscene-or-phone | `cutscene_play(18_Z01)` | engine-driven (blocking helper) | `m18_run_cargo_plane_escape_scene:2212` |
| 13 | wait-flag | `elapsed_time` | set in m18_run_ff_set_01, m18_run_ff_set_02 | `m18_run_ff_set_01:2358` |
| 15 | wait-cutscene-or-phone | `thread_check_done` | engine-driven | `m18_run_ff_vtol_flybys_01:2416` |
| 17 | wait-flag | `elapsed_time` | set in m18_run_ff_set_01, m18_run_ff_set_02 | `m18_run_ff_set_02:2470` |
| 19 | wait-cutscene-or-phone | `thread_check_done` | engine-driven | `m18_run_ff_vtol_flybys_02:2508` |
| 21 | kill-target | `{}` | on_vehicle_destroyed->m18_dive_bomb_vtol_destroyed_cb | `m18_run_ff_vtol_dive_bombers_01:2604` |
| 22 | go-to-navpoint | `veh_player_tank 001 -> nav_player_tank_center` | dist >1.0; mover=npc/vehicle veh_player_tank 001 | `m18_run_bail_out_of_tank:2712` |
| 23 | timed-wait | `delay(5.0)` | 5.0 s of game time, no player action | `m18_run_bail_out_of_tank:2715` |
| 25 | engine-wait | `vehicle_anim_finished()` | engine/native predicate | `m18_run_bail_out_of_tank:2764` |
| 27 | timed-wait | `delay(5.0)` | 5.0 s of game time, no player action | `m18_run_hijack_skydiving_tank:2967` |
| 28 | wait-flag | `player_reached_tank` | set in m18_run_hijack_skydiving_tank | `m18_run_hijack_skydiving_tank:2977` |

12 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/m18.tsv`.

Hazards (never enter, or for `on_trigger_exit` never leave): `tgr_falling_tank 001` (on_trigger -> `m18_cb_triggered_falling_tank_region`, character_kill, line 2926); `tgr_falling_tank 002` (on_trigger -> `m18_cb_triggered_falling_tank_region`, character_kill, line 2927); `tgr_falling_tank 003` (on_trigger -> `m18_cb_triggered_falling_tank_region`, character_kill, line 2928); `tgr_falling_tank 004` (on_trigger -> `m18_cb_triggered_falling_tank_region`, character_kill, line 2929).

Hand check: Stall = cabin door destroyed. `m18_cb_damaged_cabin_door` sets the flag only for **sonic-gun damage** while `sonic_gun_aquired` is true **and** the attacker is inside `tgr_cabin_door_destroy 001`. The runner entered both cabin triggers correctly. Still missing: the sonic gun (enter `tgr_gun_crate 003`, on_trigger -> `m18_cb_gun_crate_give_sonic_gun`, registered at line 1674) and a sonic damage event on `mov_cabin_door 001`.

#### m19 (Team B stall: `m19.lua:890`, route step 3)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(19_in)` | engine-driven (blocking helper) | `m19_start:554` |
| 3 | go-to-trigger | `doors_trigger` | trigger table M19_trigger.doors | `m19_go_to_entrance:889` **<- sweep stall** |
| 4 | wait-cutscene-or-phone | `cutscene_play(m19_cte_01)` | engine-driven (blocking helper) | `m19_guards_scene:921` |
| 5 | engine-wait | `mesh_mover_ready()` (x3) | engine/native predicate | `m19_setup_container:1632` |
| 6 | wait-flag | `size` | set by script | `m19_do_containers_checkpoints:1052` |
| 7 | wait-flag | `M19_convo.help_me_call.thread` | set in m19_start_convo_thread | `m19_do_rescue:1115` |
| 8 | go-to-trigger | `m19_trigger_dumpster` | trigger table M19_trigger.dumpster | `m19_do_rescue:1122` |
| 9 | go-to-trigger | `m19_trigger_roof` | trigger table M19_trigger.roof | `m19_do_rescue:1132` |
| 10 | go-to-navpoint | `Suko_inside` | dist >25; mover=npc/vehicle Suko_inside | `m19_do_rescue:1143` |
| 11 | engine-wait | `group_members_alive()` | engine/native predicate | `m19_do_rescue:1162` |
| 12 | timed-wait | `delay(10)` | 10 s of game time, no player action | `m19_do_rescue:1172` |
| 13 | wait-flag | `size` | set by script | `m19_do_containers_checkpoints:1052` |
| 14 | wait-flag | `M19_convo.broken_mask.thread` | set in m19_start_convo_thread | `m19_do_mask:1202` |
| 15 | leave-trigger | `mask_fix_trigger` | on_trigger_exit->m19_mask_trigger_exit_cb | `m19_do_mask:1227` |
| 16 | engine-wait | `door_is_open()` | engine/native predicate | `m19_do_mask:1242` |
| 17 | go-to-navpoint | ` -> viola_mask_search_nav` | dist >2.0; mover=npc/vehicle  | `m19_do_mask:1267` |
| 18 | engine-wait | `ai_scripted_action_is_complete()` | engine/native predicate | `m19_do_mask:1271` |
| 19 | timed-wait | `delay(15.0)` | 15.0 s of game time, no player action | `m19_do_mask:1300` |
| 20 | timed-wait | `delay(15.0)` | 15.0 s of game time, no player action | `m19_do_mask:1310` |
| 21 | timed-wait | `delay(15.0)` | 15.0 s of game time, no player action | `m19_do_mask:1320` |
| 22 | leave-trigger | `mask_fix_trigger` | on_trigger_exit->m19_mask_trigger_exit_cb | `m19_do_mask:1321` |
| 23 | go-to-navpoint | `?` | dist >5.0; mover=npc/vehicle  | `m19_do_mask:1341` |
| 24 | wait-flag | `size` | set by script | `m19_do_containers_checkpoints:1052` |
| 25 | wait-flag | `M19_convo.zombie_killing_03.thread` | set in m19_start_convo_thread | `m19_take_or_destroy_chem_truck:1414` |
| 26 | enter-vehicle | `chem_truck` | on_vehicle_enter->m19_player_in_chem_truck_cb | `m19_take_or_destroy_chem_truck:1419` |
| 27 | wait-flag | `M19_convo.take_sample.thread` | set in m19_start_convo_thread | `m19_take_or_destroy_chem_truck:1426` |
| 28 | go-to-trigger | `Escape_trigger 001` | trigger table M19_trigger.escape1 | `m19_take_or_destroy_chem_truck:1448` |
| 29 | enter-vehicle | `chem_truck` | on_vehicle_enter->m19_player_in_chem_truck_cb | `m19_take_or_destroy_chem_truck:1469` |

3 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/m19.tsv`.

Hand check: The runner entered `Trigger_island_area` (`m19_island_entrance_cb`, not waited on). Stall = `doors_trigger` (on_trigger -> `m19_doors_cb`).

#### m21 (Team B stall: `system_lib.lua:114 (delay)`, route step 3)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(21_in)` | engine-driven (blocking helper) | `m21_start:679` |
| 3 | timed-wait | `delay(5.0)` | 5.0 s of game time, no player action | `m21_run_defend_the_ring:1150` **<- sweep stall** |
| 4 | wait-flag | `Process_enemy_set_cleared` | set by script | `m21_process_enemy_set:2478` |
| 5 | timed-wait | `delay(6)` | 6 s of game time, no player action | `m21_run_defend_the_ring:1165` |
| 6 | wait-cutscene-or-phone | `group_is_loaded` | engine-driven | `m21_run_defend_the_ring:1168` |
| 7 | wait-flag | `Process_enemy_set_cleared` (x2) | set by script | `m21_process_enemy_set:2478` |
| 8 | timed-wait | `delay(6)` | 6 s of game time, no player action | `m21_run_defend_the_ring:1180` |
| 9 | wait-cutscene-or-phone | `group_is_loaded` | engine-driven | `m21_run_defend_the_ring:1183` |
| 10 | wait-flag | `Process_enemy_set_cleared` | set by script | `m21_process_enemy_set:2478` |
| 11 | wait-cutscene-or-phone | `thread_check_done` | engine-driven | `m21_run_defend_the_ring:1193` |
| 12 | wait-cutscene-or-phone | `zscene_is_loaded` | engine-driven | `m21_run_angel_throw_out:1462` |
| 13 | wait-cutscene-or-phone | `cutscene_play(21_Z01)` | engine-driven (blocking helper) | `m21_run_angel_throw_out:1469` |
| 15 | kill-target | `Chainsaw_Wave1_Group` | on_death->m21_chainsaw_luchador_killed_cb | `m21_run_defend_angel:1558` |
| 16 | kill-target | `Chainsaw_Wave2_Group` | on_death->m21_chainsaw_luchador_killed_cb | `m21_run_defend_angel:1571` |
| 17 | kill-target | `Chainsaw_Wave3_Group` | on_death->m21_chainsaw_luchador_killed_cb | `m21_run_defend_angel:1584` |
| 18 | kill-target | `Chainsaw_Wave4_Group` | on_death->m21_chainsaw_luchador_killed_cb | `m21_run_defend_angel:1597` |
| 19 | go-to-trigger | `Player_Reached_Angel_Trigger 001` | on_trigger->m21_player_reached_angel | `m21_run_goto_angel:1846` |
| 20 | wait-flag | `M21_conversations.player_killbane_fight` | set by script | `m21_run_goto_angel:1856` |
| 21 | go-to-trigger | `Player_Enter_Ring_Trigger 001` | on_trigger->m21_player_enter_ring | `m21_run_enter_the_ring:1895` |
| 22 | wait-flag | `M21_conversations.player_killbane_banter` | set by script | `m21_fight_killbane:2030` |
| 23 | wait-flag | `player_win` | set in m21_killbane_qte_beatup_finished | `m21_fight_killbane:2041` |
| 24 | wait-cutscene-or-phone | `zscene_is_loaded` | engine-driven | `m21_killbane_unmask_threat:2104` |
| 25 | wait-cutscene-or-phone | `cutscene_play(21_Z02)` | engine-driven (blocking helper) | `m21_killbane_unmask_threat:2118` |
| 27 | wait-flag | `Play_choice_selection` | set in m21_player_made_choice_cb | `m21_player_choice_sequence:2153` |
| 28 | wait-cutscene-or-phone | `zscene_is_loaded` | engine-driven | `m21_player_unmasked_killbane:2178` |
| 29 | wait-cutscene-or-phone | `cutscene_play(21_Z03)` | engine-driven (blocking helper) | `m21_player_unmasked_killbane:2184` |
| 30 | wait-cutscene-or-phone | `zscene_is_loaded` | engine-driven | `m21_player_didnt_unmask_killbane:2191` |
| 31 | wait-cutscene-or-phone | `cutscene_play(21_Z04)` | engine-driven (blocking helper) | `m21_player_didnt_unmask_killbane:2197` |

3 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/m21.tsv`.

Hand check: Stall = opening `delay(5.0)` (1150), then `delay(3.0)`/`delay(6)` between waves: a time budget. `Process_enemy_set_cleared` is set by the shared library's enemy-set helpers (not in m21.lua) once the wave group is dead: kill `wave1` / `wave2` / `wave3`.

#### m22 (Team B stall: `m22.lua:1593`, route step 5)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(22_in)` | engine-driven (blocking helper) | `m22_start:1194` |
| 2 | wait-cutscene-or-phone | `zscene_is_loaded` | engine-driven | `m22_setup_kill_killbane:2492` |
| 4 | go-to-trigger | `{BZ2_Spawn_Trigger, BZ3_Spawn_Trigger, BZ4_Spawn_Trigger}` | on_trigger->m22_zone_triggered_cb | `m22_process_battle_zone_sequence:1547` |
| 5 | go-to-trigger | `{BZ2_Spawn_Trigger, BZ3_Spawn_Trigger, BZ4_Spawn_Trigger}` | on_trigger->m22_zone_triggered_cb | `m22_process_battle_zone_sequence:1592` **<- sweep stall** |
| 6 | wait-cutscene-or-phone | `thread_check_done` | engine-driven | `m22_process_battle_zone_sequence:1620` |
| 7 | go-to-trigger | `Armory_Spawn_Trigger` | on_trigger->m22_armory_reached_cb | `m22_goto_armory_sequence:1962` |
| 8 | kill-target | `` | on_human_killed->m22_battle_zone_initial_died_cb | `m22_process_battle_zone_kill_enemies:1699` |
| 9 | kill-target | `` | on_human_killed->m22_battle_zone_initial_died_cb | `m22_process_battle_zone_kill_enemies:1704` |
| 10 | wait-flag | `Current_bz.bosses_killed` | set in m22_bz_boss_killed_cb | `m22_process_battle_zone_finale:1753` |
| 11 | wait-thread | `m22_helper_play_conversation` | thread started at m22_process_armory_fight:2021 | `m22_wait_for_foley_safe:2995` |
| 15 | wait-thread | `m22_helper_play_conversation` | thread started at m22_process_armory_fight:2021 | `m22_wait_for_foley_safe:2995` |
| 17 | wait-cutscene-or-phone | `cutscene_play(22_choice)` | engine-driven (blocking helper) | `m22_run:1419` |
| 18 | wait-cutscene-or-phone | `thread_check_done` | engine-driven | `m22_process_player_choice_sequence:2174` |
| 21 | go-to-trigger | `Kill_Killbane_Trigger` | on_trigger->m22_player_choice_cb | `m22_process_player_choice_sequence:2204` |
| 24 | go-to-navpoint | `Angel_Car -> Airport_Rail_Begin` | dist >10.0; mover=npc/vehicle Angel_Car | `m22_in_transit_to_airport:2335` |
| 25 | kill-target | `Killbane_Plane` | kill | `m22_process_killbane_chase_sequence:2412` |
| 26 | wait-cutscene-or-phone | `zscene_is_loaded` | engine-driven | `m22_setup_kill_killbane:2492` |
| 28 | wait-cutscene-or-phone | `cutscene_play(22_Z01)` | engine-driven (blocking helper) | `m22_killbane_plane_destroyed_scene:2546` |
| 30 | interact | `` | on_qte_animation_trigger->on_killbane_qte_fade_out_trigger | `m22_process_kill_killbane:2513` |
| 33 | wait-cutscene-or-phone | `cutscene_play(m22_cte_03)` | engine-driven (blocking helper) | `m22_c4_objective_cte:2944` |
| 35 | go-to-trigger | `Statue_Elevator_Trigger` | on_trigger->m22_player_used_elevator_cb | `m22_go_to_statue_sequence:2606` |
| 38 | kill-target | `{C4_Mover 005, C4_Mover 006, C4_Mover 007, C4_Mover 008}` | on_mover_destroyed->m22_c41_destroyed_cb | `m22_process_save_the_saints:2688` |
| 39 | go-to-trigger | `Second_Floor_Trigger` | on_trigger->m22_second_floor_cb | `m22_process_save_the_saints:2702` |
| 40 | kill-target | `{C4_Mover 005, C4_Mover 006, C4_Mover 007, C4_Mover 008}` | on_mover_destroyed->m22_c41_destroyed_cb | `m22_process_save_the_saints:2711` |
| 41 | go-to-trigger | `Kia_Breadcrumb_Trigger` | on_trigger->m22_player_reached_breadcrumb_cb | `m22_cleanup_save_the_saints:2740` |
| 42 | go-to-trigger | `Player_reached_top_trigger` | on_trigger->m22_player_reached_top_cb | `m22_cleanup_save_the_saints:2748` |
| 44 | timed-wait | `delay(5)` | 5 s of game time, no player action | `m22_kia_fight_cte:2976` |
| 46 | kill-target | `Kia_gameplay` | kill | `m22_process_kill_kia:2842` |

19 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/m22.tsv`.

Hazards (never enter, or for `on_trigger_exit` never leave): `Explosion_Region` (on_trigger_exit -> `m22_human_left_statue_cb`, character_kill, line 2676); `Trigger_Kia_Boss_Area 001` (on_trigger_exit -> `m22_human_left_statue_cb`, character_kill, line 2825).

Hand check: Stall = wait for any battle zone. Line 1591 resets `Current_bz.zone = NO_ZONE` **just before** the loop, and trigger entry is edge-triggered. If the player is already inside `BZ2_Spawn_Trigger` when the loop starts (the runner put them there), no new ENTER fires. Fix: (re-)enter a `BZn_Spawn_Trigger` after the wait is active.

#### m24 (Team B stall: `m24.lua:950`, route step 7)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(22_out2)` | engine-driven (blocking helper) | `m24_start:507` |
| 3 | wait-cutscene-or-phone | `cutscene_play(m24_cte_02)` | engine-driven (blocking helper) | `m24_cutscene_killbane:1133` |
| 6 | go-to-trigger | `{nil}` | on_trigger->m24_on_breadcrumb_reached | `m24_hack_terminal:933` |
| 7 | go-to-trigger | `Terminal_Trigger 001` | on_trigger->m24_terminal_hack_cb | `m24_hack_terminal:949` **<- sweep stall** |
| 8 | wait-flag | `M24_threads.homie_death_thread_handle / INVALID_THREAD_HANDLE` | set by script | `m24_hack_terminal:979` |
| 9 | go-to-trigger | `{breadcrumb_trigger_01, breadcrumb_trigger_02}` | on_trigger->m24_on_breadcrumb_reached | `m24_hack_terminal:933` |
| 10 | go-to-trigger | `Terminal_Trigger 002` | on_trigger->m24_terminal_hack_cb | `m24_hack_terminal:949` |
| 11 | wait-flag | `M24_threads.homie_death_thread_handle / INVALID_THREAD_HANDLE` | set by script | `m24_hack_terminal:979` |
| 12 | go-to-trigger | `{breadcrumb_trigger_03}` | on_trigger->m24_on_breadcrumb_reached | `m24_hack_terminal:933` |
| 13 | go-to-trigger | `Terminal_Trigger 003` | on_trigger->m24_terminal_hack_cb | `m24_hack_terminal:949` |
| 14 | wait-flag | `M24_threads.homie_death_thread_handle / INVALID_THREAD_HANDLE` | set by script | `m24_hack_terminal:979` |
| 15 | go-to-trigger | `{{nil}, {breadcrumb_trigger_01, breadcrumb_trigger_02}, {breadcrumb_trigger_03}}` | on_trigger->m24_on_breadcrumb_reached | `m24_hack_terminal:933` |
| 16 | go-to-trigger | `Terminal_Trigger 004` | on_trigger->m24_terminal_hack_cb | `m24_hack_terminal:949` |
| 17 | wait-flag | `M24_threads.homie_death_thread_handle / INVALID_THREAD_HANDLE` | set by script | `m24_hack_terminal:979` |
| 18 | go-to-trigger | `{{nil}, {breadcrumb_trigger_01, breadcrumb_trigger_02}, {breadcrumb_trigger_03}}` | on_trigger->m24_on_breadcrumb_reached | `m24_hack_terminal:933` |
| 19 | go-to-trigger | `Terminal_Trigger 005` | on_trigger->m24_terminal_hack_cb | `m24_hack_terminal:949` |
| 20 | wait-flag | `M24_threads.homie_death_thread_handle / INVALID_THREAD_HANDLE` | set by script | `m24_hack_terminal:979` |
| 22 | go-to-trigger | `breadcrumb_trigger_04` | on_trigger->m24_on_breadcrumb_reached | `m24_process_junction_boxes:992` |
| 23 | go-to-trigger | `{Junction_Trigger 002, Junction_Trigger 003}` | on_trigger->m24_junction_bomb_cb | `m24_process_junction_boxes:1004` |
| 24 | wait-flag | `door_exploded` | set in m24_door_explosion_cb | `m24_process_junction_boxes:1020` |
| 26 | wait-cutscene-or-phone | `cutscene_play(m24_cte_01)` | engine-driven (blocking helper) | `m24_door_blown_cutscene:1059` |
| 29 | go-to-trigger | `End_of_bridge_trigger` | on_trigger->m24_objective_reached_cb | `m24_bridge_sequence:1098` |
| 30 | go-to-trigger | `End_of_bridge_trigger` | on_trigger->m24_objective_reached_cb | `m24_bridge_sequence:1111` |
| 33 | wait-cutscene-or-phone | `cutscene_play(m24_cte_02)` | engine-driven (blocking helper) | `m24_cutscene_killbane:1133` |
| 35 | kill-target | `Killbane` | kill | `m24_kill_killbane_sequence:1173` |

10 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/m24.tsv`.

Hazards (never enter, or for `on_trigger_exit` never leave): `Lava_Death_Trigger` (on_trigger -> `m24_lava_death_callback`, character_kill, line 1292).

Hand check: Stall = `Terminal_Trigger 001` (first terminal; the same loop runs for 001..005, on_trigger -> `m24_terminal_hack_cb`). The runner teleported into `Lava_Death_Trigger`, a **hazard** (`character_kill`).

#### mm_m1_5 (Team B stall: `mm_m1_5.lua:100`, route step 2)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play()` | engine-driven (blocking helper) | `mm_m1_5_start:79` |
| 2 | engine-wait | `customization_screen_is_ready()` | engine/native predicate | `mm_m1_5_run:99` **<- sweep stall** |
| 3 | engine-wait | `customization_creation_is_open()` | engine/native predicate | `mm_m1_5_run:106` |

Hand check: Stall = customization UI (`customization_screen_is_ready()`). This is engine/UI state, with no route action.

#### mm_p_01 (Team B stall: `mm_p_01.lua:87`, route step 1)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `zscene_is_loaded` | engine-driven | `mm_p_01_start:86` **<- sweep stall** |
| 2 | wait-cutscene-or-phone | `cutscene_play(P_Z01)` | engine-driven (blocking helper) | `mm_p_01_start:89` |

3 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/mm_p_01.tsv`.

Hand check: Stall = intro zscene load (`zscene_is_loaded`, line 86-88). This is engine-side, with no route action.

#### mm_p_03 (Team B stall: `mm_p_03.lua:342`, route step 3)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(mmp03_cte_01)` | engine-driven (blocking helper) | `mm_p_03_start:185` |
| 3 | interact | `store false` | buy at store | `mm_p_03_go_to_storefront:341` **<- sweep stall** |
| 4 | timed-wait | `delay(6)` | 6 s of game time, no player action | `mm_p_03_go_to_storefront:348` |
| 5 | engine-wait | `mission_collectible_collected()` | engine/native predicate | `mm_p_03_collectibles:407` |
| 6 | interact | `store true` | buy at store | `mm_p_03_go_to_property:380` |
| 7 | timed-wait | `delay(5)` | 5 s of game time, no player action | `mm_p_03_go_to_flashpoint:422` |
| 8 | wait-flag | `flashpoint_num` | set in mm_p_03_go_to_flashpoint | `mm_p_03_go_to_flashpoint:426` |
| 9 | go-to-navpoint | `MM_P_03_Nav_FP02` | dist <17; mover=npc/vehicle MM_P_03_Nav_FP02 | `mm_p_03_go_to_flashpoint:451` |
| 10 | wait-flag | `flashpoint_complete` | set in mm_p_03_go_to_flashpoint | `mm_p_03_go_to_flashpoint:461` |
| 11 | engine-wait | `tutorial_visible()` | engine/native predicate | `mm_p_03_lower_notoriety:519` |

1 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/mm_p_03.tsv`.

Hand check: Stall = buy the mission store (`mission_store_purchased(false)`; the script gives $100 and marks `MM_P_03_Nav_Store`). Interact = purchase at that store (`spec-lua-api-behaviour.md` §25.3).

#### mm_p_06 (Team B stall: `mm_p_06.lua:95`, route step 1)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `zscene_is_loaded` | engine-driven | `mm_p_06_start:94` **<- sweep stall** |
| 2 | wait-cutscene-or-phone | `cutscene_play(P_Z02)` | engine-driven (blocking helper) | `mm_p_06_start:99` |

1 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/mm_p_06.tsv`.

Hand check: Stall = intro zscene load (`zscene_is_loaded`). This is engine-side, with no route action.

#### mm_p_07 (Team B stall: `mm_p_07.lua:116`, route step 2)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 2 | wait-cutscene-or-phone | `phone call MM_P_07_End_Phone_Call` | cellphone cb2 (ended) | `mm_p_07_run:115` **<- sweep stall** |

1 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/mm_p_07.tsv`.

Hand check: Stall = Pierce **cellphone call** cb2 (`mm_p_07_end_call`). This is engine-side (§12.2).

#### sh01 (Team B stall: `sh01.lua:433`, route step 3)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(MS_SH01)` | engine-driven (blocking helper) | `sh01_start:352` |
| 3 | go-to-trigger | `Warehouse_Door_Trigger 001` | on_trigger->sh01_reached_warehouse_door | `sh01_run:432` **<- sweep stall** |
| 4 | go-to-trigger | `trig_BreakRoom` | on_trigger->sh01_reached_breakroom | `sh01_run:464` |
| 5 | go-to-trigger | `Upstairs_Trigger 001` | on_trigger->sh01_reached_upstairs | `sh01_run:470` |
| 7 | wait-cutscene-or-phone | `cutscene_play(sh01_cte_01)` | engine-driven (blocking helper) | `sh01_run:495` |
| 9 | kill-target | `MiniGun_Brute 001` | on_death->sh01_brute_killed | `sh01_run:531` |
| 10 | wait-flag | `office_triggered` | set in sh01_reached_office | `sh01_run:562` |
| 11 | wait-thread | `sh01_shaundi_hack_thread` | thread started at sh01_run:568 | `sh01_run:577` |
| 12 | wait-thread | `sh01_shaundi_hack_thread` | thread started at sh01_run:568 | `sh01_run:584` |

3 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/sh01.tsv`.

Hand check: Stall = `Warehouse_Door_Trigger 001` (on_trigger -> `sh01_reached_warehouse_door`). The runner entered `trig_AlleyConvo`, which only starts a conversation thread.

#### sh02 (Team B stall: `system_lib.lua:114 (delay)`, route step 2)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(MS_SH02)` | engine-driven (blocking helper) | `sh02_start:613` |
| 2 | timed-wait | `delay(15.0)` | 15.0 s of game time, no player action | `sh02_start:622` **<- sweep stall** |
| 6 | wait-flag | `sh02_check_homies_thread()` | script predicate | `sh02_run:665` |
| 7 | go-to-trigger | `trig_collection<001>` | trigger table sh02_trigger.collection01 cb sh02_collection01_cb | `sh02_run:683` |
| 8 | go-to-trigger | `trig_collection<002>` | trigger table sh02_trigger.collection02 cb sh02_collection02_cb | `sh02_run:688` |
| 9 | go-to-trigger | `trig_override<001>` | trigger table sh02_trigger.override01 cb sh02_override01_cb | `sh02_run:693` |
| 11 | go-to-trigger | `trig_stairwell<001>` | trigger table sh02_trigger.stairwell01 cb sh02_stairwell01_cb | `sh02_run:751` |
| 12 | go-to-trigger | `trig_stairwell<002>` | trigger table sh02_trigger.stairwell02 cb sh02_stairwell02_cb | `sh02_run:769` |
| 13 | go-to-trigger | `trig_stairwell<003>` | trigger table sh02_trigger.stairwell03 cb sh02_stairwell03_cb | `sh02_run:775` |
| 14 | go-to-trigger | `trig_stairwell<004>` | trigger table sh02_trigger.stairwell04 cb sh02_stairwell04_cb | `sh02_run:781` |
| 15 | go-to-trigger | `trig_free_hos<001>` | trigger table sh02_trigger.freehos01 cb sh02_freehos01_cb | `sh02_run:832` |
| 16 | go-to-trigger | `trig_jailcell01<001>` | trigger table sh02_trigger.jailcell01_01 cb sh02_jailcell01_01_cb | `sh02_run:844` |
| 17 | go-to-trigger | `trig_jailcell01<002>` | trigger table sh02_trigger.jailcell01_02 cb sh02_jailcell01_02_cb | `sh02_run:854` |
| 18 | go-to-trigger | `trig_free_hos<002>` | trigger table sh02_trigger.freehos02 cb sh02_freehos02_cb | `sh02_run:873` |
| 19 | go-to-trigger | `trig_jailcell02<001>` | trigger table sh02_trigger.jailcell02_01 cb sh02_jailcell02_01_cb | `sh02_run:878` |
| 20 | go-to-trigger | `trig_jailcell02<002>` | trigger table sh02_trigger.jailcell02_02 cb sh02_jailcell02_02_cb | `sh02_run:888` |
| 21 | go-to-trigger | `trig_jailcell03<001>` | trigger table sh02_trigger.jailcell03_01 cb sh02_jailcell03_01_cb | `sh02_run:926` |
| 22 | go-to-trigger | `trig_jailcell03<002>` | trigger table sh02_trigger.jailcell03_02 cb sh02_jailcell03_02_cb | `sh02_run:936` |
| 23 | go-to-trigger | `trig_override<004>` | trigger table sh02_trigger.override04 cb sh02_override04_cb | `sh02_run:951` |
| 24 | kill-target | `grp_jailcell03_wave` | on_death->sh02_enemy_killed_cb | `sh02_run:972` |
| 25 | go-to-trigger | `trig_garage` | trigger table sh02_trigger.garage cb sh02_garage_cb | `sh02_run:988` |
| 27 | wait-cutscene-or-phone | `cutscene_play(sh02_cte_01)` | engine-driven (blocking helper) | `sh02_run:1028` |
| 29 | kill-target | `npc_garage_brute` | on_death->sh02_brute_killed_cb | `sh02_run:1049` |

6 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/sh02.tsv`.

Hand check: Stall = `delay(15.0)` right after the intro cutscene (line 622): a time budget. The trigger route starts at `trig_collection<001>`.

#### sh03 (Team B stall: `sh03.lua:546`, route step 5)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(DK_SH01)` | engine-driven (blocking helper) | `sh03_start:489` |
| 3 | wait-flag | `SH03_OBJECTIVE_ONE_COMPLETE` | set in sh03_obj_one | `sh03_run:518` |
| 4 | wait-flag | `SH03_OBJECTIVE_THREE` | set in sh03_setup_obj_two | `sh03_run:543` |
| 5 | go-to-trigger | `{Tower_Trigger 001, Tower_Trigger2, Tower_Trigger3}` | on_trigger->sh03_obj_two | `sh03_run:544` **<- sweep stall** |
| 6 | exit-vehicle | `any vehicle` | player must be ON FOOT | `sh03_run:545` |
| 7 | engine-wait | `ai_scripted_action_is_complete()` | engine/native predicate | `sh03_kinzie_hack_tower:1676` |
| 8 | wait-flag | `progress` | set in sh03_kinzie_hack_tower | `sh03_kinzie_hack_tower:1708` |
| 9 | drive-to | `Helicopter_trigger` | on_trigger->sh03_get_to_the_choppa; cb checks vehicle | `sh03_obj_three_heli:1084` |
| 10 | wait-thread | `sh03_heli_arrival` | thread started at sh03_obj_three_heli:1076 | `sh03_obj_three_heli:1093` |
| 11 | go-to-navpoint | `Rail_Heli` | dist <=9; mover=npc/vehicle Rail_Heli | `sh03_obj_three_heli:1118` |
| 12 | engine-wait | `NPC_Kinzie` | waits for NOT dead/destroyed (no player action) | `sh03_obj_three_heli:1121` |
| 13 | engine-wait | `human_is_downed()` | engine/native predicate | `sh03_obj_three_heli:1125` |
| 14 | exit-vehicle | `any vehicle` | player must be ON FOOT | `sh03_obj_three_heli:1129` |
| 15 | engine-wait | `qte_human_is_used()` | engine/native predicate | `sh03_obj_three_heli:1133` |
| 16 | go-to-trigger | `inside_trigger` | on_trigger->sh03_inside_tower | `sh03_obj_three_heli:1170` |
| 17 | go-to-trigger | `{LinkTrigger1, LinkTrigger2, LinkTrigger3, LinkTrigger4}` | on_trigger->sh03_obj_four | `sh03_run:596` |

1 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/sh03.tsv`.

Hand check: Stall = tower loop (546-553). Enter any unhacked tower trigger (`Tower_Trigger 001`, `Tower_Trigger2`, `Tower_Trigger3`) (on_trigger -> `sh03_obj_two` sets `CURRENT_TOWER`), then be **on foot** (repeat-until not in a vehicle). Kinzie's hack then counts `progress += SH03_TIMER` up to `SH03_BASE_TIME` = 25000 while the player stays inside. Do this three times. The runner went to `GPS_NAV_01` and `start_convo_trigger` (no callback) instead.

#### sh04 (Team B stall: `sh04.lua:773`, route step 4)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(ld_sh01)` | engine-driven (blocking helper) | `sh04_start:415` |
| 3 | wait-thread | `sh04_play_convo` | thread started at sh04_initialize_checkpoint:1512 | `sh04_run:450` |
| 4 | kill-target | `sh04` | on_random_mover_killed->sh04_slot_died | `sh04_wait_for_slots:772` **<- sweep stall** |
| 5 | go-to-navpoint | `sh04_manager` | dist >20; mover=npc/vehicle sh04_manager | `sh04_obj_manager:1253` |
| 6 | wait-flag | `SH04_INTERROGATION` | set in sh04_grab_manager_local, sh04_manager_interrogation_cb | `sh04_obj_manager:1270` |
| 8 | go-to-trigger | `Mask Cutscene` | on_trigger->sh04_hit_mask_cutscene | `sh04_wait_on_reach_vault:763` |
| 10 | wait-cutscene-or-phone | `cutscene_play(cte_s04_angel_gets_mask)` | engine-driven (blocking helper) | `sh04_play_mask_cutscene:796` |
| 12 | kill-target | `{SM Killbane Statue 1F SE, SM Killbane Statue 1F NW, SM Killbane Statue 1F SW, SM Killbane` | on_mover_destroyed->sh04_statue_died | `sh04_wait_on_statues_destroyed:1193` |
| 13 | kill-target | `{Post Statue, Post Statue 001, Post Statue 002, Post Statue 003}` | on_death->sh04_remaining_luchador_died | `sh04_wait_on_leftovers_killed:1204` |
| 14 | kill-target | `{brute02, brute03}` | on_death->sh04_brute_killed | `sh04_run:519` |

4 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/sh04.tsv`.

Hand check: Stall = destroy slot machines: `on_random_mover_killed("sh04_slot_died", "sh04")` counts `SH04_destroyed_slots` up to `SH04_TOTAL_DAMAGE` = 125 (200 in coop). Kill movers of that random-mover set. The runner entered `Mission Area 001` (area flag only).

### Batch 2 - the 19 missions not suspended at session end in sweep 49 (FINAL)

#### dlc1_mm_01 (Team B: not suspended at session end)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(dlc1_gb_in)` | engine-driven (blocking helper) | `dlc1_mm_01_start:79` |

1 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/dlc1_mm_01.tsv`.

Hand check: Cutscene-only mission (intro `cutscene_play`, then `fade_out_block`). It finished cleanly in the sweep, so there is no route to satisfy. dlc1_mm_02..06 have the same shape.

#### dlc1_mm_02 (Team B: not suspended at session end)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play()` | engine-driven (blocking helper) | `dlc1_mm_02_start:79` |

1 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/dlc1_mm_02.tsv`.

Hand check: Same shape as dlc1_mm_01 (cutscene, then fade). Finished cleanly.

#### dlc1_mm_03 (Team B: not suspended at session end)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play()` | engine-driven (blocking helper) | `dlc1_mm_03_start:79` |

1 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/dlc1_mm_03.tsv`.

Hand check: Same shape as dlc1_mm_01. Finished cleanly.

#### dlc1_mm_04 (Team B: not suspended at session end)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play()` | engine-driven (blocking helper) | `dlc1_mm_04_start:79` |

1 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/dlc1_mm_04.tsv`.

Hand check: Same shape as dlc1_mm_01. Finished cleanly.

#### dlc1_mm_05 (Team B: not suspended at session end)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play()` | engine-driven (blocking helper) | `dlc1_mm_05_start:79` |

1 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/dlc1_mm_05.tsv`.

Hand check: Same shape as dlc1_mm_01. Finished cleanly.

#### dlc1_mm_06 (Team B: not suspended at session end)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(dlc1_gb_out)` | engine-driven (blocking helper) | `dlc1_mm_06_start:79` |

1 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/dlc1_mm_06.tsv`.

Hand check: Same shape as dlc1_mm_01. Finished cleanly.

#### dlc2_m03 (Team B: not suspended at session end)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play()` | engine-driven (blocking helper) | `dlc2_m03_start:1144` |
| 3 | go-to-trigger | `Trigger_transmitter<001>` | on_trigger->dlc2_m03_trigger_cb | `dlc2_m03_first_transmitter:1500` |
| 4 | kill-target | `DLC2_M03_global.current_transmitter_sequence.shields[*]` | on_mover_destroyed->dlc2_m03_shield_destroyed_cb | `dlc2_m03_process_transmitter_sequence:2166` |
| 5 | wait-thread | `dlc2_m03_virus_transfer_thread` | thread started at dlc2_m03_process_transmitter_sequence:2182 | `dlc2_m03_process_transmitter_sequence:2191` |
| 6 | timed-wait | `delay(25.0)` | 25.0 s of game time, no player action | `dlc2_m03_first_transmitter:1522` |
| 7 | wait-flag | `DLC2_M03_global.near_crash_completed` | set in dlc2_m03_on_near_crash_cb | `dlc2_m03_stunt_sequence:1541` |
| 8 | go-to-trigger | `Trigger_Barnstorm 001` | trigger table DLC2_M03_trigger.barnstorm_trigger_01 | `dlc2_m03_stunt_sequence:1558` |
| 9 | go-to-trigger | `Trigger_transmitter<002>` | on_trigger->dlc2_m03_trigger_cb | `dlc2_m03_second_transmitter:1587` |
| 10 | kill-target | `DLC2_M03_global.current_transmitter_sequence.shields[*]` | on_mover_destroyed->dlc2_m03_shield_destroyed_cb | `dlc2_m03_process_transmitter_sequence:2166` |
| 11 | wait-thread | `dlc2_m03_virus_transfer_thread` | thread started at dlc2_m03_process_transmitter_sequence:2182 | `dlc2_m03_process_transmitter_sequence:2191` |
| 12 | go-to-trigger | `Trigger_trap_spawn` | trigger table DLC2_M03_trigger.trap_region_spawn | `dlc2_m03_national_guard_trap:1611` |
| 13 | go-to-trigger | `Trigger_trap_region` | trigger table DLC2_M03_trigger.trap_region | `dlc2_m03_national_guard_trap:1623` |
| 14 | wait-flag | `npcs_alive` | set in dlc2_m03_kill_list_wait_for_complete | `dlc2_m03_kill_list_wait_for_complete:2063` |
| 15 | go-to-trigger | `DLC2_M03_transmitter_regions.three.trigger` | on_trigger->dlc2_m03_trigger_cb | `dlc2_m03_third_transmitter:1673` |
| 16 | kill-target | `DLC2_M03_global.current_transmitter_sequence.shields[*]` | on_mover_destroyed->dlc2_m03_shield_destroyed_cb | `dlc2_m03_process_transmitter_sequence:2166` |
| 17 | wait-thread | `dlc2_m03_virus_transfer_thread` | thread started at dlc2_m03_process_transmitter_sequence:2182 | `dlc2_m03_process_transmitter_sequence:2191` |
| 18 | timed-wait | `delay(9.0)` | 9.0 s of game time, no player action | `dlc2_m03_execute_the_virus:1693` |
| 19 | go-to-trigger | `Trigger_execute_region` | trigger table DLC2_M03_trigger.execute_region | `dlc2_m03_execute_the_virus:1696` |
| 20 | timed-wait | `delay(6.0)` | 6.0 s of game time, no player action | `dlc2_m03_execute_the_virus:1703` |
| 21 | timed-wait | `delay(5.0)` | 5.0 s of game time, no player action | `dlc2_m03_escape_the_virus:1717` |
| 22 | go-to-trigger | `Trigger_Barnstorm 002` | trigger table DLC2_M03_trigger.barnstorm_trigger_02 | `dlc2_m03_escape_the_virus:1723` |
| 23 | go-to-trigger | `Trigger_escape_region` | trigger table DLC2_M03_trigger.escape_region | `dlc2_m03_escape_the_virus:1740` |

1 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/dlc2_m03.tsv`.

Hand check: The thread errored on an OPEN engine state (tutorial descriptor `['193']`, `open_blockers.tsv`) before any route step. This is not a route problem.

#### m01 (Team B: not suspended at session end)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(01_IN)` | engine-driven (blocking helper) | `m01_start:1039` |
| 2 | wait-cutscene-or-phone | `character_is_ready` | engine-driven | `m01_initialize_checkpoint:2502` |
| 3 | engine-wait | `vehicle_enable_physics()` | engine/native predicate | `m01_vault_setup_part1_thread:5562` |
| 4 | engine-wait | `vehicle_enable_physics()` | engine/native predicate | `m01_vault_setup_part2_thread:5591` |
| 5 | engine-wait | `vehicle_enter_teleport()` | engine/native predicate | `m01_vault_setup_part2_thread:5603` |
| 6 | engine-wait | `vehicle_enter_teleport()` | engine/native predicate | `m01_vault_setup_part2_thread:5608` |
| 7 | enter-vehicle | `heli_vault 001` | needs vehicle | `m01_vault_setup_part2_thread:5613` |
| 8 | enter-vehicle | `heli_vault 001` | needs vehicle | `m01_vault_setup_part2_thread:5618` |
| 10 | wait-thread | `m01_josh_is_dumbass_thread` | thread started at m01_kill_lobby_guards:1332 | `m01_kill_lobby2_guards:1391` |
| 11 | engine-wait | `ai_scripted_action_is_complete()` | engine/native predicate | `m01_kill_lobby2_guards:1400` |
| 12 | go-to-trigger | `Freerun_trigger 001` | trigger table M01_trigger.freerun | `m01_go_to_vault:1512` |
| 13 | go-to-trigger | `vault_trigger 000` | trigger table M01_trigger.pre_vault | `m01_go_to_vault:1537` |
| 14 | go-to-trigger | `vault_trigger 001` | trigger table M01_trigger.vault | `m01_go_to_vault:1549` |
| 15 | wait-cutscene-or-phone | `cutscene_play(01_z01)` | engine-driven (blocking helper) | `m01_plan_b_scene:1571` |
| 16 | engine-wait | `group_create_check_done()` | engine/native predicate | `m01_plan_b_scene:1591` |
| 17 | engine-wait | `ai_scripted_action_is_processing()` | engine/native predicate | `m01_set_bomb:1636` |
| 18 | go-to-trigger | `bomb_trigger 000` | trigger table M01_trigger.pre_bomb | `m01_set_bomb:1645` |
| 19 | timed-wait | `delay(5.0)` | 5.0 s of game time, no player action | `m01_set_bomb:1653` |
| 20 | go-to-trigger | `bomb_trigger 001` | trigger table M01_trigger.bomb | `m01_set_bomb:1674` |
| 21 | wait-cutscene-or-phone | `cutscene_play(01_Z02)` | engine-driven (blocking helper) | `m01_detonate_bomb_scene:1695` |
| 24 | timed-wait | `delay(10.0)` | 10.0 s of game time, no player action | `m01_survive_cops:1840` |
| 25 | timed-wait | `delay(10.0)` | 10.0 s of game time, no player action | `m01_survive_cops:1848` |
| 27 | timed-wait | `delay(10.0)` | 10.0 s of game time, no player action | `m01_survive_cops:1857` |
| 28 | timed-wait | `delay(5.0)` | 5.0 s of game time, no player action | `m01_survive_cops:1864` |
| 29 | timed-wait | `delay(10.0)` | 10.0 s of game time, no player action | `m01_survive_cops:1868` |
| 31 | timed-wait | `delay(10.0)` | 10.0 s of game time, no player action | `m01_survive_cops:1887` |
| 32 | wait-thread | `m01_respawn_pair_thread` | thread started at m01_start_swat_respawn_pairs:3254 | `m01_survive_cops:1897` |
| 33 | timed-wait | `delay(6.0)` | 6.0 s of game time, no player action | `m01_survive_cops:1912` |
| 35 | kill-target | `police_heli 003` | on_damage->m01_attack_heli_damaged_cb; damage, not necessarily kill | `m01_survive_cops:1934` |
| 36 | wait-cutscene-or-phone | `cutscene_play(01_Z03)` | engine-driven (blocking helper) | `m01_heli_arrive_scene:1973` |
| 37 | engine-wait | `vehicle_enable_physics()` | engine/native predicate | `m01_vault_setup_part1_thread:5562` |
| 38 | engine-wait | `vehicle_enable_physics()` | engine/native predicate | `m01_vault_setup_part2_thread:5591` |
| 39 | engine-wait | `vehicle_enter_teleport()` | engine/native predicate | `m01_vault_setup_part2_thread:5603` |
| 40 | engine-wait | `vehicle_enter_teleport()` | engine/native predicate | `m01_vault_setup_part2_thread:5608` |
| 41 | enter-vehicle | `heli_vault 001` | needs vehicle | `m01_vault_setup_part2_thread:5613` |
| 42 | enter-vehicle | `heli_vault 001` | needs vehicle | `m01_vault_setup_part2_thread:5618` |
| 44 | timed-wait | `delay(6.0)` | 6.0 s of game time, no player action | `m01_vault_roof_setup:2037` |
| 45 | engine-wait | `vehicle_anim_playing()` | engine/native predicate | `m01_path_complete_thread:4531` |
| 46 | wait-thread | `m01_fighter_attack_run_thread` | thread started at m01_vault_roof_setup:2055 | `m01_vault_roof_setup:2065` |
| 48 | engine-wait | `vehicle_anim_playing()` | engine/native predicate | `m01_path_complete_thread:4531` |
| 49 | kill-target | `transport_table.vehicle` | on_vehicle_destroyed->m01_transport_destroyed_cb | `m01_roof_guards1:2127` |
| 50 | wait-thread | `m01_fighter_attack_run_thread` | thread started at m01_vault_roof_setup:2055 | `m01_shoot_heli_alt:2155` |
| 52 | timed-wait | `delay(8.0)` | 8.0 s of game time, no player action | `m01_building_crash:2184` |
| 53 | engine-wait | `vehicle_anim_playing()` | engine/native predicate | `m01_path_complete_thread:4531` |
| 54 | go-to-navpoint | `police_heli 003 -> attack_position2_nav` | dist >20.0; mover=npc/vehicle police_heli 003 | `m01_heli_crash_other_building:2224` |
| 55 | engine-wait | `helicopter_shoot_human()` | engine/native predicate | `m01_fighter_shoot_vault:3127` |
| 57 | engine-wait | `vehicle_anim_playing()` | engine/native predicate | `m01_path_complete_thread:4531` |
| 58 | timed-wait | `delay(5.0)` | 5.0 s of game time, no player action | `m01_break_chains:2279` |
| 60 | timed-wait | `delay(5.0)` | 5.0 s of game time, no player action | `m01_shoot_most:2304` |
| 61 | kill-target | `police_heli 003` | on_damage->m01_attack_heli_damaged_cb; damage, not necessarily kill | `m01_shoot_most:2325` |
| 62 | engine-wait | `vehicle_anim_finished()` | engine/native predicate | `m01_shoot_most:2347` |
| 63 | terminal | `(endless loop)` | never exits; mission ends via failure/death callback | `m01_run:1126` |

11 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/m01.tsv`.

Hazards (never enter, or for `on_trigger_exit` never leave): `trigger` (on_trigger -> `m01_kill_trigger_cb`, character_kill, line 3078).

Hand check: The thread errored at once on an OPEN engine state (mover `+0xc4` control sub-record, `open_blockers.tsv`). This is not a route problem.

#### m02 (Team B: not suspended at session end)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(02_in)` | engine-driven (blocking helper) | `m02_start:718` |
| 4 | interact | `mvr_cargo_plane_door 004` | on_door_opened->m02_door_to_cargo_bay_opened_cb | `m02_run_go_to_cargo_hold:1501` |
| 5 | go-to-trigger | `tgr_cargo_plane_exit 001` | on_trigger->m02_cb_triggered_grab_parachute | `m02_run_grab_parachute:1569` |
| 7 | wait-cutscene-or-phone | `cutscene_play(02_z01)` | engine-driven (blocking helper) | `m02_run_cargo_plane_ejection_scene:1771` |
| 9 | wait-flag | `M02_flags.local_gun_grapple_finished` | set in m02_cb_local_gun_grapple_finished | `m02_run_free_failling_initial_dive:1935` |
| 10 | kill-target | `Group_sky_goons 001` | on_death->m02_cb_sky_goon_killed | `m02_run_kill_sky_goons_01:2013` |
| 11 | wait-flag | `M02_flags.local_gun_grapple_finished` | set in m02_cb_local_gun_grapple_finished | `m02_run_kill_sky_goons_01:2018` |
| 12 | timed-wait | `delay(5.0)` | 5.0 s of game time, no player action | `m02_run_kill_sky_goons_02_03:2056` |
| 13 | engine-wait | `get_char_skydiving_state()` | engine/native predicate | `m02_run_kill_sky_goons_02_03:2066` |
| 14 | kill-target | `Group_sky_goons 002` | on_death->m02_cb_sky_goon_killed | `m02_run_kill_sky_goons_02_03:2075` |
| 15 | kill-target | `Group_sky_goons 003` | on_death->m02_cb_sky_goon_killed | `m02_run_kill_sky_goons_02_03:2084` |
| 16 | go-to-trigger | `tgr_vertical_dive 001` | on_trigger->m02_cb_triggered_final_dive_01 | `m02_run_dive_to_shaundi:2149` |
| 17 | engine-wait | `action_play_synced_is_finished()` | engine/native predicate | `m02_run_catch_shaundi_first_time:2194` |
| 18 | engine-wait | `action_play_synced_is_finished()` | engine/native predicate | `m02_run_catch_shaundi_first_time:2201` |
| 19 | engine-wait | `action_play_synced_is_finished()` | engine/native predicate | `m02_run_catch_shaundi_first_time:2208` |
| 20 | engine-wait | `action_play_synced_is_finished()` | engine/native predicate | `m02_run_ram_player_with_plane:2249` |
| 21 | engine-wait | `action_play_synced_is_finished()` | engine/native predicate | `m02_run_ram_player_with_plane:2258` |
| 22 | engine-wait | `vehicle_anim_finished()` | engine/native predicate | `m02_run_ram_player_with_plane:2274` |
| 23 | wait-flag | `(true)` | set by script | `m02_run_ram_player_with_plane:2298` |
| 24 | wait-cutscene-or-phone | `cutscene_play(02_z02)` | engine-driven (blocking helper) | `m02_run_fly_through_plane_scene:2376` |
| 25 | kill-target | `Group_sky_goons 005` | on_death->m02_cb_sky_goon_killed | `m02_run_kill_sky_goons_05:2512` |
| 26 | wait-flag | `M02_flags.local_final_grapple_finished` | set in m02_cb_local_final_grapple_finished | `m02_run_final_dive_qte:2595` |
| 27 | go-to-trigger | `tgr_vertical_dive 001` | on_trigger->m02_cb_triggered_final_dive_01 | `m02_run_final_dive:2649` |
| 28 | engine-wait | `action_play_synced_is_finished()` | engine/native predicate | `m02_run_catch_shaundi_again:2691` |
| 29 | engine-wait | `action_play_synced_is_finished()` | engine/native predicate | `m02_run_catch_shaundi_again:2697` |
| 30 | timed-wait | `delay(6.0)` | 6.0 s of game time, no player action | `m02_run:1127` |

5 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/m02.tsv`.

Hand check: The thread errored on an OPEN engine state (tutorial descriptor `['12']`). This is not a route problem.

#### m07 (Team B: not suspended at session end)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(07_In)` | engine-driven (blocking helper) | `m07_start:755` |
| 2 | engine-wait | `vehicle_enter_teleport()` | engine/native predicate | `m07_setup_pony_carts:1771` |
| 3 | engine-wait | `vehicle_enter_teleport()` | engine/native predicate | `m07_setup_pony_carts:1786` |
| 5 | go-to-navpoint | `Trigger Barge Dock` | dist >100; mover=npc/vehicle Trigger Barge Dock | `m07_get_to_the_barge:1234` |
| 6 | go-to-trigger | `Trigger Barge Dock` | on_trigger->m07_reached_goal_cb | `m07_get_to_the_barge:1255` |
| 7 | wait-cutscene-or-phone | `character_is_ready` | engine-driven | `m07_rescue_kinzie:1284` |
| 8 | timed-wait | `delay(5)` | 5 s of game time, no player action | `m07_rescue_kinzie:1296` |
| 11 | go-to-trigger | `Trigger Barge Wheelhouse` | on_trigger->m07_reached_wheelhouse | `m07_rescue_kinzie:1319` |
| 12 | go-to-trigger | `Trigger Untie Kinzie` | on_trigger->m07_kinzie_untie_cb | `m07_rescue_kinzie:1339` |
| 15 | wait-cutscene-or-phone | `cutscene_play(M07_FREE_KINZIE)` | engine-driven (blocking helper) | `m07_rescue_kinzie:1385` |
| 17 | timed-wait | `delay(5)` | 5 s of game time, no player action | `m07_get_to_bdsm:1430` |
| 18 | wait-cutscene-or-phone | `phone call m07_convo_17` | cellphone cb2 (ended) | `m07_get_to_bdsm:1434` |
| 19 | timed-wait | `delay(6)` | 6 s of game time, no player action | `m07_get_to_bdsm:1440` |
| 20 | go-to-trigger | `Trigger BDSM Club` | on_trigger->m07_reached_goal_cb | `m07_get_to_bdsm:1445` |
| 22 | go-to-trigger | `Trigger Club Owner` | on_trigger->m07_reached_goal_cb | `m07_zimos_part_one_run:1603` |
| 24 | drive-to | `Trigger grab club owner` | on_trigger->m07_bdsm_owner_interrogate_cb; cb checks vehicle | `m07_zimos_part_one_run:1629` |
| 25 | go-to-trigger | `Trigger Pony Stairs` | on_trigger->m07_reached_goal_cb | `m07_zimos_part_one_run:1671` |
| 26 | go-to-trigger | `Trigger Pony Stairs Two` | on_trigger->m07_reached_goal_cb | `m07_zimos_part_one_run:1681` |
| 27 | go-to-trigger | `Trigger Pony Barn` | on_trigger->m07_reached_goal_cb | `m07_zimos_part_one_run:1693` |
| 28 | go-to-trigger | `Trigger Pony Carts` | on_trigger->m07_reached_goal_cb | `m07_zimos_part_one_run:1706` |
| 29 | wait-cutscene-or-phone | `cutscene_play(07_Z01)` | engine-driven (blocking helper) | `m07_zimos_part_one_run:1718` |
| 30 | engine-wait | `vehicle_enter_teleport()` | engine/native predicate | `m07_setup_pony_carts:1771` |
| 31 | engine-wait | `vehicle_enter_teleport()` | engine/native predicate | `m07_setup_pony_carts:1786` |
| 33 | engine-wait | `vehicle_enter_teleport()` | engine/native predicate | `m07_setup_pony_carts:1771` |
| 34 | engine-wait | `vehicle_enter_teleport()` | engine/native predicate | `m07_setup_pony_carts:1786` |
| 36 | kill-target | `{{Pony Cart Enemy 001, Pony Cart Enemy 002}, {Pony Cart Enemy 003, Pony Cart Enemy 004}, {` | on_death->m07_pony_enemy_killed_cb | `m07_zimos_part_two_run:1855` |
| 37 | kill-target | `{{Pony Cart Enemy 001, Pony Cart Enemy 002}, {Pony Cart Enemy 003, Pony Cart Enemy 004}, {` | on_death->m07_pony_enemy_killed_cb | `m07_zimos_part_two_run:1864` |
| 38 | wait-cutscene-or-phone | `cutscene_play(07_Z02)` | engine-driven (blocking helper) | `m07_zimos_part_two_run:1905` |
| 41 | wait-cutscene-or-phone | `phone call m07_convo_17` | cellphone cb2 (ended) | `m07_angel_part_one_find_angel:1999` |
| 44 | timed-wait | `delay(5)` | 5 s of game time, no player action | `m07_angel_part_one_find_angel:2037` |
| 45 | go-to-trigger | `Trigger Outside Casino` | on_trigger->m07_outside_casino_reached_cb | `m07_angel_part_one_find_angel:2045` |
| 46 | kill-target | `Luch Out Brute` | kill | `m07_angel_defeat_luchadores:2100` |
| 48 | kill-target | `Luch Out Brute` | on_death->m07_enemy_killed_cb | `m07_angel_defeat_luchadores:2118` |
| 49 | engine-wait | `group_create_check_done()` | engine/native predicate | `m07_angel_defeat_luchadores:2126` |
| 50 | kill-target | `Luch Out Brute` | on_death->m07_enemy_killed_cb | `m07_angel_defeat_luchadores:2151` |
| 51 | go-to-trigger | `Trigger Casino Lobby` | on_trigger->m07_inside_casino_reached_cb | `m07_angel_defeat_luchadores:2171` |
| 52 | wait-cutscene-or-phone | `cutscene_play(07_Z03)` | engine-driven (blocking helper) | `m07_angel_mid_checkpoint_cutscene:2209` |
| 55 | timed-wait | `delay(8.0)` | 8.0 s of game time, no player action | `m07_angel_part_two_casino_defense:2296` |
| 56 | kill-target | `Cas Def Brute` | kill | `m07_angel_part_two_casino_defense:2300` |
| 57 | engine-wait | `character_exists()` | engine/native predicate | `m07_angel_part_two_casino_defense:2356` |
| 58 | kill-target | `Cas Def Brute Minigun` | on_death->m07_brute_killed_cb | `m07_angel_part_two_casino_defense:2369` |
| 60 | engine-wait | `character_exists()` | engine/native predicate | `m07_angel_part_two_casino_defense:2422` |
| 61 | engine-wait | `character_exists()` | engine/native predicate | `m07_angel_part_two_casino_defense:2431` |
| 62 | kill-target | `Cas Def Brute Flamer 002` | on_death->m07_brute_killed_cb | `m07_angel_part_two_casino_defense:2446` |

18 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/m07.tsv`.

Hand check: The runner followed the route correctly (get_dist, then on_trigger to `Trigger Barge Dock`, steps 5-6). The thread then errored on the OPEN character alive/not-gone predicate (Team B blocker). The next route steps are `Trigger Barge Wheelhouse`, then `Trigger Untie Kinzie`.

#### m11 (Team B: not suspended at session end)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(11_in)` | engine-driven (blocking helper) | `m11_start:708` |
| 2 | engine-wait | `vehicle_enter_teleport()` | engine/native predicate | `m11_initialize_checkpoint:1123` |
| 4 | go-to-trigger | `t_sb1_restricted` | on_trigger->m11_reached_callback | `m11_go_to_first_base:1238` |
| 5 | wait-cutscene-or-phone | `group_is_loaded` | engine-driven | `m11_hijack_vtol:1260` |
| 7 | wait-thread | `m11_player_vtol_tracking_thread` | thread started at m11_initialize_checkpoint:1132 | `m11_wait_for_audio:2460` |
| 8 | drive-to | `{v_sb1_vtol 001, v_sb1_vtol 002}` | on_vehicle_enter_water->m11_vtol_destroyed_callback | `m11_hijack_vtol:1290` |
| 9 | wait-thread | `m11_player_vtol_tracking_thread` | thread started at m11_initialize_checkpoint:1132 | `m11_wait_for_audio:2460` |
| 10 | kill-target | `{sb 001, sb 002, sb 003, sb 005}` | on_mover_destroyed->m11_box_destroyed | `m11_distract_stag:1373` |
| 11 | wait-thread | `m11_radio_chatter_thread` | thread started at m11_run:755 | `m11_wait_for_audio:2460` |
| 12 | kill-target | `{sb 001, sb 002, sb 003, sb 005}` | on_mover_destroyed->m11_box_destroyed | `m11_distract_stag:1373` |
| 13 | wait-thread | `m11_radio_chatter_thread` | thread started at m11_run:755 | `m11_wait_for_audio:2460` |
| 15 | wait-thread | `m11_radio_chatter_thread` (x2) | thread started at m11_run:755 | `m11_wait_for_audio:2460` |
| 17 | go-to-trigger | `rb 001` | on_trigger->m11_reached_callback | `m11_help_convoy:1434` |
| 18 | kill-target | `rb 001` | on_vehicle_destroyed->m11_target_destroyed | `m11_help_convoy:1467` |
| 19 | wait-thread | `m11_radio_chatter_thread` | thread started at m11_run:755 | `m11_wait_for_audio:2460` |
| 20 | timed-wait | `delay(5)` | 5 s of game time, no player action | `m11_run:807` |
| 22 | kill-target | `{sb 001, sb 002, sb 003, sb 005}` | on_mover_destroyed->m11_box_destroyed | `m11_distract_stag:1373` |
| 23 | wait-thread | `m11_radio_chatter_thread` | thread started at m11_run:755 | `m11_wait_for_audio:2460` |
| 24 | go-to-trigger | `t_oleg_battle` | on_trigger->m11_reached_callback | `m11_help_oleg:1753` |
| 26 | timed-wait | `delay(5)` | 5 s of game time, no player action | `m11_help_oleg:1818` |
| 27 | wait-thread | `m11_radio_chatter_thread` | thread started at m11_run:755 | `m11_wait_for_audio:2460` |
| 28 | kill-target | `{M11_groups.oleg_battle.tank1, M11_groups.oleg_battle.tank2, M11_groups.oleg_battle.tank3}` | on_vehicle_destroyed->m11_target_destroyed | `m11_help_oleg:1826` |
| 29 | kill-target | `{v_ob_apc 001, v_ob_apc 002, v_ob_apc 003}` | on_vehicle_destroyed->m11_target_destroyed | `m11_help_oleg:1863` |
| 31 | go-to-navpoint | `v_sba_target 001` | dist >600; mover=npc/vehicle v_sba_target 001 | `m11_air_base:1597` |
| 32 | wait-thread | `m11_stag_vtol_radio_chatter_thread` | thread started at m11_help_oleg:1860 | `m11_wait_for_audio:2460` |
| 33 | timed-wait | `delay(10)` | 10 s of game time, no player action | `m11_air_base:1647` |
| 34 | kill-target | `{v_sba 002, v_sba 003}` | on_vehicle_destroyed->m11_guard_destroyed | `m11_air_base:1652` |
| 35 | kill-target | `{v_sba_target 002, v_sba_target 003}` | on_vehicle_destroyed->m11_transport_destroyed | `m11_air_base:1677` |
| 36 | kill-target | `v_sba_target 001` | on_damage->m11_final_plane_destroyed; damage, not necessarily kill | `m11_air_base:1690` |
| 37 | timed-wait | `delay(6)` | 6 s of game time, no player action | `m11_air_base:1708` |

7 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/m11.tsv`.

Hand check: The runner reached `t_sb1_restricted` (step 4) through `t_sb1_location`. The thread then errored on the OPEN conversation-channel slot `mission_conv`. The next step is to drive or hijack one of `v_sb1_vtol 001/002`.

#### m12 (Team B: not suspended at session end)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(12_In)` | engine-driven (blocking helper) | `m12_start:516` |
| 2 | wait-cutscene-or-phone | `thread_check_done` | engine-driven | `m12_setup_josh:1871` |
| 4 | engine-wait | `character_is_ragdolled()` | engine/native predicate | `m12_teleport_team_to:995` |
| 5 | wait-cutscene-or-phone | `thread_check_done` | engine-driven | `m12_teleport_team_to:1008` |
| 7 | wait-cutscene-or-phone | `thread_check_done` | engine-driven | `m12_setup_josh:1871` |
| 9 | engine-wait | `character_is_ragdolled()` | engine/native predicate | `m12_teleport_team_to:995` |
| 10 | wait-cutscene-or-phone | `thread_check_done` | engine-driven | `m12_teleport_team_to:1008` |
| 13 | go-to-trigger | `trigger_pr_center_front` | on_trigger->m12_trigger_cback | `m12_goto_a_trigger:1289` |
| 14 | go-to-trigger | `trigger_pr_center_enterance` | on_trigger->m12_trigger_cback | `m12_goto_a_trigger:1289` |
| 15 | go-to-trigger | `trigger_reception` | on_trigger->m12_trigger_cback | `m12_goto_a_trigger:1289` |
| 16 | timed-wait | `delay(6.0)` | 6.0 s of game time, no player action | `m12_run:554` |
| 17 | go-to-trigger | `trigger_lobby_middle` | on_trigger->m12_trigger_cback | `m12_goto_a_trigger:1289` |
| 18 | go-to-trigger | `trigger_lobby_top` | on_trigger->m12_trigger_cback | `m12_goto_a_trigger:1289` |
| 19 | leave-trigger | `M12_elevator.lobby_upper` | on_trigger_exit->m12_elevator_exit_cback | `m12_goto_a_elevator:1167` |
| 21 | engine-wait | `character_is_ragdolled()` | engine/native predicate | `m12_teleport_team_to:995` |
| 22 | wait-cutscene-or-phone | `thread_check_done` | engine-driven | `m12_teleport_team_to:1008` |
| 24 | wait-cutscene-or-phone | `thread_check_done` | engine-driven | `m12_setup_josh:1871` |
| 25 | go-to-trigger | `trigger_door_to_josh` | on_trigger->m12_trigger_cback | `m12_goto_a_trigger:1289` |
| 26 | go-to-navpoint | `npc_josh` | dist >14.0 | `m12_wait_until_josh_is_in_range:956` |
| 27 | wait-cutscene-or-phone | `cutscene_play(12_z01)` | engine-driven (blocking helper) | `m12_play_z_scene:1147` |
| 29 | engine-wait | `character_is_ragdolled()` | engine/native predicate | `m12_teleport_team_to:995` |
| 30 | wait-cutscene-or-phone | `thread_check_done` | engine-driven | `m12_teleport_team_to:1008` |
| 32 | go-to-trigger | `trigger_studio 001` | on_trigger->m12_trigger_cback | `m12_goto_a_trigger:1289` |
| 33 | go-to-trigger | `trigger_studio 002` | on_trigger->m12_trigger_cback | `m12_goto_a_trigger:1289` |
| 34 | go-to-trigger | `trigger_studio 003` | on_trigger->m12_trigger_cback | `m12_goto_a_trigger:1289` |
| 35 | go-to-trigger | `trigger_studio_door_to_stairs` | on_trigger->m12_trigger_cback | `m12_goto_a_trigger:1289` |
| 36 | go-to-trigger | `trigger_cubicle 001` | on_trigger->m12_trigger_cback | `m12_goto_a_trigger:1289` |
| 37 | leave-trigger | `M12_elevator.cubical` | on_trigger_exit->m12_elevator_exit_cback | `m12_goto_a_elevator:1167` |
| 39 | engine-wait | `character_is_ragdolled()` | engine/native predicate | `m12_teleport_team_to:995` |
| 40 | wait-cutscene-or-phone | `thread_check_done` | engine-driven | `m12_teleport_team_to:1008` |
| 42 | go-to-trigger | `trigger_lobby_middle` | on_trigger->m12_trigger_cback | `m12_goto_a_trigger:1289` |
| 43 | leave-trigger | `M12_elevator.lobby_lower` | on_trigger_exit->m12_elevator_exit_cback | `m12_goto_a_elevator:1167` |
| 45 | engine-wait | `character_is_ragdolled()` | engine/native predicate | `m12_teleport_team_to:995` |
| 46 | wait-cutscene-or-phone | `thread_check_done` | engine-driven | `m12_teleport_team_to:1008` |
| 48 | go-to-trigger | `trigger_load_josh` | on_trigger->m12_on_load_josh | `m12_load_josh_into_car:1662` |
| 49 | enter-vehicle | `Script_Vehicle 004` | on_vehicle_enter->m12_escape_vehicle_entered | `m12_load_josh_into_car:1718` |
| 50 | drive-to | `trigger_garage_door` | on_trigger->m12_trigger_cback; follows enter-vehicle | `m12_goto_a_trigger:1289` |
| 51 | timed-wait | `delay(15.0)` | 15.0 s of game time, no player action | `m12_run:700` |
| 52 | go-to-trigger | `trigger_saints_hq` | on_trigger->m12_trigger_cback | `m12_run:703` |

13 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/m12.tsv`.

Hand check: The thread errored at once on an OPEN engine state (tutorial descriptor `['204']`). This is not a route problem.

#### m17 (Team B: not suspended at session end)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(17_in)` | engine-driven (blocking helper) | `m17_start:586` |
| 4 | timed-wait | `delay(6)` | 6 s of game time, no player action | `m17_hijack_heli:1208` |
| 6 | timed-wait | `delay(6)` | 6 s of game time, no player action | `m17_hijack_heli:1217` |
| 7 | enter-vehicle | `any vehicle (cb registered on REMOTE_PLAYER)` | on_vehicle_enter->m17_heli_entered | `m17_hijack_heli:1223` |
| 9 | timed-wait | `delay(6)` | 6 s of game time, no player action | `m17_board_ship:1280` |
| 11 | timed-wait | `delay(6)` | 6 s of game time, no player action | `m17_board_ship:1289` |
| 12 | go-to-trigger | `m17_trigger_convo_close` | on_trigger->m17_trigger_reached | `m17_board_ship:1292` |
| 13 | go-to-trigger | `m17_trigger_carrier` | on_trigger->m17_trigger_reached | `m17_board_ship:1304` |
| 14 | wait-cutscene-or-phone | `cutscene_play(17_mid)` | engine-driven (blocking helper) | `m17_board_ship_cutscene:1325` |
| 16 | wait-cutscene-or-phone | `character_is_ready` | engine-driven | `m17_follow_kia:1381` |
| 17 | timed-wait | `delay(20.0)` | 20.0 s of game time, no player action | `m17_follow_kia:1433` |
| 18 | wait-flag | `sel` | set in m17_meeting | `m17_meeting:1572` |
| 19 | wait-cutscene-or-phone | `cutscene_play(17_z01)` | engine-driven (blocking helper) | `m17_meeting_cutscene:1595` |
| 21 | engine-wait | `door_is_open()` | engine/native predicate | `m17_free_friends:1695` |
| 22 | go-to-trigger | `trigger_enter_tank_bay` | on_trigger->m17_trigger_reached | `m17_free_friends:1718` |
| 23 | timed-wait | `delay(8)` | 8 s of game time, no player action | `m17_free_friends:1749` |
| 24 | engine-wait | `door_is_open()` | engine/native predicate | `m17_free_friends:1760` |
| 25 | engine-wait | `door_is_open()` | engine/native predicate | `m17_free_friends:1788` |
| 26 | go-to-trigger | `m17_trigger_shaundi` | on_trigger->m17_shaundi_freed | `m17_free_friends:1815` |
| 28 | go-to-trigger | `m17_trigger_brig_stairs` | on_trigger->m17_trigger_reached | `m17_destroy_reactor:1910` |
| 29 | go-to-trigger | `m17_trigger_rspawn` | on_trigger->m17_trigger_reached | `m17_destroy_reactor:1930` |
| 30 | go-to-trigger | `m17_trigger_rhall` | on_trigger->m17_trigger_reached | `m17_destroy_reactor:1953` |
| 31 | go-to-trigger | `trigger_enter_reactor` | on_trigger->m17_trigger_reached | `m17_destroy_reactor:1969` |
| 33 | engine-wait | `mesh_mover_destroyed()` | engine/native predicate | `m17_destroy_reactor:1998` |
| 34 | go-to-trigger | `trigger_enter_reactor` | on_trigger->m17_trigger_reached | `m17_escape:2112` |
| 35 | go-to-trigger | `m17_trigger_escape_hall` | on_trigger->m17_trigger_reached | `m17_escape:2127` |
| 36 | engine-wait | `door_is_open()` | engine/native predicate | `m17_escape:2165` |
| 37 | go-to-trigger | `trigger_enter_hangar` | on_trigger->m17_trigger_reached | `m17_escape:2204` |
| 38 | go-to-navpoint | `m17_vtol` | dist >20; mover=npc/vehicle m17_vtol | `m17_escape:2235` |

9 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/m17.tsv`.

Hazards (never enter, or for `on_trigger_exit` never leave): `m17_trigger_ehall<001>` (on_trigger -> `m17_escape_event_1`, character_kill, line 2089); `m17_trigger_ehall<002>` (on_trigger -> `m17_escape_event_2`, character_kill, line 2091).

Hand check: The thread errored at once on an OPEN engine state (tutorial descriptor `['204']`). This is not a route problem.

#### m20 (Team B: not suspended at session end)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(20_in)` | engine-driven (blocking helper) | `m20_start:436` |
| 2 | enter-vehicle | `Veh Angel Heli MS` | needs vehicle | `m20_initialize_crash_the_car:745` |
| 3 | enter-vehicle | `Veh Angel Heli MS` | needs vehicle | `m20_initialize_crash_the_car:757` |
| 4 | enter-vehicle | `Veh Angel Heli MS` | needs vehicle | `m20_initialize_crash_the_car:765` |
| 5 | wait-cutscene-or-phone | `character_is_ready` | engine-driven | `m20_mascot_brawl:714` |
| 6 | enter-vehicle | `Veh Angel Heli MS` | needs vehicle | `m20_initialize_crash_the_boat:1036` |
| 7 | enter-vehicle | `Veh Angel Heli MS` | needs vehicle | `m20_initialize_crash_the_boat:1047` |
| 8 | enter-vehicle | `Veh Angel Heli MS` | needs vehicle | `m20_initialize_crash_the_boat:1055` |
| 9 | enter-vehicle | `Veh Angel Heli MS` | needs vehicle | `m20_initialize_crash_the_helicopter:1245` |
| 10 | enter-vehicle | `Veh Angel Heli MS` | needs vehicle | `m20_initialize_crash_the_helicopter:1254` |
| 11 | enter-vehicle | `Veh Angel Heli MS` | needs vehicle | `m20_initialize_crash_the_helicopter:1260` |
| 12 | enter-vehicle | `Veh Angel Heli MS` | needs vehicle | `m20_initialize_airport:1504` |
| 13 | enter-vehicle | `Veh Angel Heli MS` | needs vehicle | `m20_initialize_airport:1513` |
| 14 | enter-vehicle | `Veh Angel Heli MS` | needs vehicle | `m20_initialize_airport:1519` |
| 17 | go-to-trigger | `Trigger Gym` | object_is_in_trigger(LOCAL_PLAYER) | `m20_objective_go_to_the_gym:670` |
| 18 | wait-cutscene-or-phone | `character_is_ready` | engine-driven | `m20_mascot_brawl:714` |
| 20 | wait-flag | `player_controls_car` | set in m20_objective_take_control_of_the_car | `m20_objective_take_control_of_the_car:816` |
| 22 | wait-flag | `M20_status.rc_car_peds_hit` | set in m20_rc_car_hit_ped_cb | `m20_objective_crash_peds:970` |
| 24 | kill-target | `Veh RC Car` | kill | `m20_objective_crash_the_car:889` |
| 26 | go-to-trigger | `Trigger Boat` | on_trigger->m20_boat_reached_cb | `m20_objective_crash_the_boat:1093` |
| 27 | go-to-navpoint | `Veh Angel Heli MS -> Nav Heli Float Boat` | dist <60; mover=npc/vehicle Veh Angel Heli MS | `m20_objective_crash_the_boat:1102` |
| 28 | wait-flag | `M20_status.player_has_rc_vehicle` | set in m20_objective_crash_the_boat, m20_objective_crash_the_helicopter, m20_objective_ | `m20_objective_crash_the_boat:1126` |
| 30 | wait-thread | `m20_beach_party_polling_thread` | thread started at m20_objective_crash_the_boat:1160 | `m20_objective_crash_the_boat:1164` |
| 32 | wait-thread | `m20_beach_party_polling_thread` | thread started at m20_objective_crash_the_boat:1160 | `m20_objective_crash_the_boat:1174` |
| 34 | go-to-trigger | `Trigger Circular Path` | on_trigger->m20_circular_path_reached_cb | `m20_objective_crash_the_helicopter:1288` |
| 35 | wait-cutscene-or-phone | `group_is_loaded` | engine-driven | `m20_objective_crash_the_helicopter:1300` |
| 36 | wait-cutscene-or-phone | `group_is_loaded` | engine-driven | `m20_objective_crash_the_helicopter:1316` |
| 37 | wait-flag | `M20_status.player_has_rc_vehicle` | set in m20_objective_crash_the_boat, m20_objective_crash_the_helicopter, m20_objective_ | `m20_objective_crash_the_helicopter:1322` |
| 38 | wait-cutscene-or-phone | `group_is_loaded` | engine-driven | `m20_objective_crash_the_helicopter:1347` |
| 39 | kill-target | `Veh Heli Wrestler` | kill | `m20_objective_crash_the_helicopter:1364` |
| 41 | go-to-trigger | `Trigger Tank Path` | on_trigger->m20_tank_path_reached_cb | `m20_objective_airport:1546` |
| 42 | kill-target | `NPC Last Wrestler` | on_death->m20_last_wrestler_killed_cb | `m20_objective_tanks_to_you:1579` |
| 43 | wait-flag | `M20_status.player_has_rc_vehicle` | set in m20_objective_crash_the_boat, m20_objective_crash_the_helicopter, m20_objective_ | `m20_objective_tanks_to_you:1616` |
| 44 | drive-to | `{Veh STAG Tank 001, Veh STAG Tank 002, Veh STAG Tank 003, Veh STAG Tank 004, Veh STAG Tank` | on_vehicle_enter_water->m20_rc_tank_destroyed_cb | `m20_objective_tanks_to_you:1669` |

10 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/m20.tsv`.

Hand check: The runner entered `Trigger Gym`. The thread had already errored on the OPEN character alive/not-gone predicate. The repeated enter-vehicle rows are per-checkpoint initializers that seat the player in `Veh Angel Heli MS` (a script-side `vehicle_enter_teleport`, then a poll).

#### m23 (Team B: not suspended at session end)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play(23_in)` | engine-driven (blocking helper) | `m23_start:839` |
| 3 | go-to-trigger | `tgr_saints_hq_entry` | on_trigger->m23_saints_hq_entry_cb | `m23_get_to_saints_hq:1600` |
| 4 | wait-flag | `M23_flags.player_got_heli[1] / M23_flags.player_got_heli[2]` | set by script | `m23_aquire_helicopter:1674` |
| 5 | go-to-trigger | `tgr_daedalus_airspace 001` | on_trigger->m23_daedalus_airspace_entered_cb | `m23_get_to_daedalus:1814` |
| 6 | timed-wait | `delay(9.0)` | 9.0 s of game time, no player action | `m23_daedalus_destroy_guns:1953` |
| 7 | kill-target | `{}` | on_mover_destroyed->m23_callback_daedalus_gun_destroyed_cb | `m23_daedalus_destroy_guns:1959` |
| 10 | exit-vehicle | `any vehicle (cb registered on REMOTE_PLAYER)` | on_vehicle_exit->m23_daedalus_landing_player_exited_vehicle | `m23_land_on_the_daedalus:2091` |
| 11 | go-to-trigger | `tgr_plant_bomb_1` | on_trigger->m23_bomb_planted_or_flown_to_cb | `m23_set_bomb_or_fly_to:2216` |
| 12 | go-to-trigger | `tgr_plant_bomb_2` | on_trigger->m23_bomb_planted_or_flown_to_cb | `m23_set_bomb_or_fly_to:2216` |
| 13 | go-to-trigger | `tgr_fly_to_side` | on_trigger->m23_bomb_planted_or_flown_to_cb | `m23_set_bomb_or_fly_to:2216` |
| 14 | go-to-trigger | `tgr_plant_bomb_3` | on_trigger->m23_bomb_planted_or_flown_to_cb | `m23_set_bomb_or_fly_to:2216` |
| 15 | go-to-trigger | `tgr_plant_bomb_4` | on_trigger->m23_bomb_planted_or_flown_to_cb | `m23_set_bomb_or_fly_to:2216` |
| 17 | kill-target | `M23_groups.cyrus_vtol.vtol` | on_vehicle_destroyed->m23_cyrus_vtol_destroyed_cb | `m23_kill_cyrus:2507` |
| 18 | timed-wait | `delay(5)` | 5 s of game time, no player action | `m23_do_boss_battle_self_destruct:2714` |
| 19 | wait-flag | `(true)` | set by script | `m23_do_boss_battle_self_destruct:2722` |
| 20 | wait-cutscene-or-phone | `cutscene_play(23_z01)` | engine-driven (blocking helper) | `m23_play_cyrus_death_scene:2751` |
| 21 | wait-flag | `M23_flags.escape_timer_expired` | set in m23_cb_boss_battle_self_destruct_time_expired, m23_cb_escape_time_expired | `m23_escape_from_daedalus:2826` |
| 22 | timed-wait | `delay(20.0)` | 20.0 s of game time, no player action | `m23_escape_from_daedalus:2860` |

5 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/m23.tsv`.

Hazards (never enter, or for `on_trigger_exit` never leave): `tgr_cyrus_combat 001` (on_trigger_exit -> `m23_failure_leave_cb`, mission_end_failure, line 2462); `tgr_daedalus_gunfire` (on_trigger_exit -> `m23_exited_daedalus_gunfire`, mission_end_failure, line 1169).

Hand check: The runner teleported into `tgr_daed_eng_kill 001`, which is not on the opening route (step 3 is `tgr_saints_hq_entry`). The run ended on the OPEN on_vehicle_enter resolved-object kind for `veh_daedalus_vtol 001`. Two `on_trigger_exit` failure zones must not be left once they are armed (see hazards).

#### mm_m16_5 (Team B: not suspended at session end)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|

2 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/mm_m16_5.tsv`.

Hand check: Cutscene, then fade. Finished cleanly.

#### mm_p_02 (Team B: not suspended at session end)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play()` | engine-driven (blocking helper) | `mm_p_02_start:89` |

1 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/mm_p_02.tsv`.

Hand check: Cutscene, then fade. Finished cleanly.

#### mm_p_04 (Team B: not suspended at session end)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play()` | engine-driven (blocking helper) | `mm_p_04_start:86` |

1 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/mm_p_04.tsv`.

Hand check: Cutscene, then fade. Finished cleanly.

#### mm_p_05 (Team B: not suspended at session end)

| # | kind | name | radius / vehicle / kill (callback) | Lua fn:line |
|---|---|---|---|---|
| 1 | wait-cutscene-or-phone | `cutscene_play()` | engine-driven (blocking helper) | `mm_p_05_start:86` |

2 filler waits omitted (fades, conversation-helper waits); full list in `tools/scratchpad/mission_routes/mm_p_05.tsv`.

Hand check: Cutscene, then fade. Finished cleanly.

### Route target positions (zone data, 2026-10-13)

**Status: FINAL** for zone file, class, position and shape type (cross-checked against Team B's own zone loader output, below) and for shape sizes (decoded with the `Trigger_Volume` parser's own property-to-field mapping, re-read this pass). The two exceptions are marked where they apply: the cylinder axis direction, and box centres for tilted boxes.

**What this is.** For every named object a route step waits on, this gives the object's zone file, class, world position, and its shape and size. The kinds covered are go-to-trigger, drive-to, go-to-navpoint (target and mover), enter/exit-vehicle, kill-target and interact. The full list for all 49 missions is in `tools/scratchpad/mission_routes/trigger_positions.tsv`: 417 rows, 409 of 413 distinct (mission, name) pairs found. The tables below cover the 30 stalled missions: 284 of 287 names found, 3 not found.

**How it was produced.**
- `tools/scratchpad/zone_named_objects.py` memory-maps one archive per process. It walks the nested `.str2_pc` containers and parses every `.czn_pc` Objects section (`0x2234`) using `spec-zone-data-format.md` §11, reusing `tools/harnesses/czn_u2_replay.py`. Each named object is written to `tools/scratchpad/zone_objects/<archive>.tsv`. It scanned 2,971 zones: `sr3_city_0` 2,068, `sr3_city_1` 810, `dlc1` 46, `dlc2` 31 and `dlc3` 16. `sr3_city_missions` and the two patch archives contain no `.czn_pc`.
- `tools/scratchpad/route_target_positions.py` joins the route TSVs to those objects. Names match case-insensitively, as the engine's name map does. Zone preference:
  1. the mission's own package, `<m>.str2_pc` (for example `m01.str2_pc` → `` sr3_city~fm01`.czn_pc ``);
  2. other packages whose name starts with the mission code (for example `m24_mars`);
  3. anywhere else.

  Every match came from tier 1 or 2; none needed tier 3. The only out-of-package hit was sh04's `sh04`, which is listed below as not a target.
- Symbolic names the route walker left unresolved were resolved by hand from the Lua dumps:
  - `Dlc2_m02_triggers.machine_2/4a/4b/4c` → `trg_machine_2`, `trg_machine4a_local`, `trg_machine4b_local`, `trg_machine4c_local`;
  - `DLC2_M03_transmitter_regions.three.trigger` → `Trigger_transmitter<003>`;
  - the dlc2_m03 `shields[*]` → `Transmitter_shield_mover<001>`-`<008>`;
  - `M03_tank_attackN` → `TankN_tank` and `TankN_group`;
  - m08 `sniper` → `NPC Sniper 001`-`007`;
  - `M11_groups.oleg_battle.tankN` → `v_ob_tank 002`-`004`;
  - `M23_groups.cyrus_vtol.vtol` → `veh_cyrus_vtol 001`.
- Four route-TSV name lists are cut off at the column width: m07 pony-cart enemies, m14 Decker NPCs, m20 STAG tanks and sh04 statues. They were completed by name stem, and each completion was checked against the Lua table.
- **Route correction, dlc2_m02.** At `dlc2_m02_handle_machine:1316` the route TSV names the navpoint (`nav_machine_1/3/4`) or the trigger-table entry. The script actually polls `machine_data.approach_trigger` there (`Dlc2_m02_machine_data`, around `dlc2_m02.lua` line 718). The real approach triggers are added as rows marked "added": `trg_machine_approach_1/2/3/4` and `trg_machine4a/4b/4c_approach`. Machines 1 and 3 have no `trigger` field in the script table; their zone triggers `trg_machine_1` and `trg_machine_3` are listed as well. A runner should use the approach trigger for the `object_is_in_trigger` poll, not the navpoint.
- Not looked up by design: "any vehicle", "store true/false", unnamed interact or kill steps (m04, m16, m22), and the two `?` navpoints (m14 step 1, m19) that the route walker could not name.

**Zone-file property → runtime field mapping (`Trigger_Volume`).** The parser was re-read with capstone (`tools/scratchpad/tv_parser_disasm.py`). It is a jump table on the shape code written to `+0x8c`.

| zone property | shape | meaning | runtime field |
|---|---|---|---|
| `object_smart_transform` (id `0xC8BEEEC5`), kind 2 / 12 bytes | all | position x, y, z, with no rotation | `+0x40` |
| `object_smart_transform`, kind 3 / 28 bytes | all | position x, y, z, then a quaternion stored x, y, z, w (w last) | `+0x40` / `+0x4c` |
| `trigger_shape_type` (string) | — | `"bounding box"` → 0, `"sphere"` → 1, `"cylinder"` → 2. Counts among named triggers: 866 / 223 / 220 | `+0x8c` |
| `trigger_shape_param1` (vec3) | box | local MIN corner | `+0x90`..`+0x98` |
| `trigger_shape_param2` (vec3) | box | local MAX corner | `+0x9c`..`+0xa4` |
| `trigger_shape_param1` (f32) | sphere | radius (`trigger_shape_param2` is not read) | `+0x90` |
| `trigger_shape_param1` (f32) | cylinder | **axis LENGTH** | `+0x90` |
| `trigger_shape_param2` (f32) | cylinder | **RADIUS** | `+0x94` |

- **Cylinder mapping.** The parser stores param1 at `+0x90` and param2 at `+0x94`. §21.13 of `spec-lua-api-behaviour.md` shows that containment uses `+0x90` as the axis length and `+0x94` as the radius, so **param1 = length and param2 = radius**. §1.14 of that spec labels param1 "(radius)" and param2 "(length)"; those two labels are wrong, while its offsets are right. The data agrees with this reading: `tgr_landing_area` has length 13.3 and radius 208 (a flat landing disc), and `tgr_armory_island_volume` has length 311 and radius 440. Many mission cylinders use the authoring default of radius 1 and length 1 (for example `tgr_rescue_kwilanna`, `trg_machine_1/2/3`).
- **Cylinder axis direction: not stored in these properties.** The runtime axis-endpoint field `+0x58` still has no identified writer (§1.14). HIGH CONFIDENCE, not checked: the axis runs from the position along the object's local up axis, as boxes do.
- **Boxes stand on their position.** In all 866 shipped boxes, min.y = 0 and the box is symmetric in x and z. So half-extents = (max − min) / 2, and the box occupies position.y .. position.y + 2·half_y. The centre is position + R(q)·(0, half_y, 0); with no quaternion it is position + (0, half_y, 0). Only 39 of 1,309 named triggers in the whole game are tilted (a non-zero x or z quaternion part). On every route in this section the rotation is about the vertical axis only, so the centre is exact. The one exception is m02 `tgr_cargo_plane_exit 001`, which has a 0.005 tilt and is not a stall target. The rotation direction was not checked for tilted boxes.
- **Point objects** (`navpoint`, `Script_Npc`, `script_vehicle`, `Script_Mover`, `script_group`) carry a position and an optional rotation, and no volume. For an NPC or vehicle the position is its placed spawn point. For a `script_group` it is the group's own marker.

**Checks.**
- All 104 distinct triggers that Team B's harness printed as "REAL position (…) and REAL shape '…' from zone data" in sweep `mission_sweep_49_20261013_post7b3e110` match this extraction to within 0.01 in position, with the same shape string (`tools/scratchpad/route_positions_crosscheck.py`).
- Sizes are not printed by Team B, so their check is the parser read above.

**Found, by class (all 49 missions):** 219 `Trigger_Volume` (134 box, 69 cylinder, 16 sphere), 74 `Script_Npc`, 56 `script_vehicle`, 23 `Script_Mover`, 22 `navpoint`, 19 `script_group`.

**Names not found (all 49 missions).**

- dlc2_m02: `Dlc2_m02_backstage_data[ 1 ].enter_trigger` - script value is nil (entry 1 has no enter_trigger)
- dlc3_m02: `heli_table [*]` - function parameter, built at run time
- sh04: `sh04` - not an object: on_random_mover_killed is registered without a named target (any slot-machine mover); the zone name "sh04" is only the open-world scripted_mission_start marker
- m01: `transport_table.vehicle` - function parameter, built at run time

Also not counted: names the route TSV leaves blank or as `?` (see above).

**Per-mission tables (30 stalled missions).** Steps are the route-table step numbers; **(stall)** marks the Team B stall step. "(mover)" is the NPC or vehicle that must reach a navpoint.

#### dlc2_m01 (stall step 3)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 4 | `tgr_armory_parking_spot` | Trigger_Volume | `` sr3_city~fdlc2_m01`.czn_pc `` | (-871.966, 4.15, 219.427) | box, half-extents (10.924, 0.5, 5.78); base at position, centre (-871.966, 4.65, 219.427); quat (-0,0.066,-0,0.998) |
| 5,11 | `tgr_heli_pad_volume` | Trigger_Volume | `` sr3_city~fdlc2_m01`.czn_pc `` | (-873.501, 0.783, 311.327) | box, half-extents (47.59, 9.227, 29.719); base at position, centre (-873.501, 10.01, 311.327) |
| 6,12 | `tgr_rescue_kwilanna` | Trigger_Volume | `` sr3_city~fdlc2_m01`.czn_pc `` | (-910.55, 10.626, 324.279) | cylinder, radius 1, length 1 |

#### dlc2_m02 (stall step 5)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 5 **(stall)** | `trg_loadout_1` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2574.258, -299.025, -2682.644) | cylinder, radius 1.458, length 2.512 |
| 8 | `trg_backstage_exit_1` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2591.104, -301.293, -2701.986) | box, half-extents (7.796, 7.348, 6.109); base at position, centre (-2591.104, -293.946, -2701.986) |
| 13,14,17,18,21,23,24 | `nav_machine_1` | navpoint | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2555.481, -300.552, -2710.479) | point (no volume) |
| 13,17,21,23 | `trg_machine_approach_1` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2563.068, -303.297, -2714.57) | box, half-extents (12.383, 5.772, 8.198); base at position, centre (-2563.068, -297.525, -2714.57) |
| 13,17,21,23 | `trg_machine_1` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2555.479, -300.662, -2710.479) | cylinder, radius 1, length 1; quat (0,0.172,0,0.985) |
| 18,24,38,44,76,82,96,102,116,122,136,142,156,162,205 | `npc_alien_homie` (mover) | Script_Npc | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2603.138, -298.682, -2659.3) | point (no volume) |
| 33,34,37,38,41,43,44 | `trg_machine_2` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2646.26, -294.779, -2722.79) | cylinder, radius 1, length 1 |
| 33,37,41,43 | `trg_machine_approach_2` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2646.278, -294.731, -2721.991) | box, half-extents (3.943, 4.017, 3.906); base at position, centre (-2646.278, -290.714, -2721.991) |
| 53 | `trg_backstage_enter_2` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2688.874, -301.293, -2715.264) | box, half-extents (14.207, 7.348, 30.711); base at position, centre (-2688.874, -293.946, -2715.264) |
| 56 | `trg_loadout_2` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2678.663, -298.686, -2688.954) | box, half-extents (1.163, 1.256, 1.163); base at position, centre (-2678.663, -297.43, -2688.954) |
| 59,66 | `trg_backstage_exit_2` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2658.573, -301.293, -2673.872) | box, half-extents (4.797, 7.348, 4.279); base at position, centre (-2658.573, -293.946, -2673.872) |
| 71,72,75,76,79,81,82 | `nav_machine_3` | navpoint | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2664.457, -302.627, -2661.823) | point (no volume) |
| 71,75,79,81 | `trg_machine_approach_3` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2656.01, -302.902, -2661.735) | box, half-extents (11.508, 4.017, 3.735); base at position, centre (-2656.01, -298.884, -2661.735) |
| 71,75,79,81 | `trg_machine_3` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2664.473, -302.649, -2661.811) | cylinder, radius 1, length 1; quat (-0,-0.989,-0,0.149) |
| 91,92,95,96,99,101,102 | `trg_machine4a_local` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2712.466, -298.82, -2614.505) | cylinder, radius 1, length 1.625 |
| 91,95,99,101 | `trg_machine4a_approach` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2710.282, -298.793, -2615.8) | box, half-extents (4, 2, 4); base at position, centre (-2710.282, -296.793, -2615.8) |
| 111,112,115,116,119,121,122 | `trg_machine4b_local` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2709.68, -302.734, -2591.098) | cylinder, radius 1, length 1.625; quat (-0,-0.998,-0,0.059) |
| 111,115,119,121 | `trg_machine4b_approach` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2708.478, -302.774, -2591.991) | box, half-extents (4, 2, 4); base at position, centre (-2708.478, -300.774, -2591.991) |
| 131,132,135,136,139,141,142 | `trg_machine4c_local` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2702.215, -294.755, -2572.621) | cylinder, radius 1, length 1.625; quat (-0,-1,-0,-0.001) |
| 131,135,139,141 | `trg_machine4c_approach` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2705.228, -294.946, -2571.362) | box, half-extents (4, 2, 4); base at position, centre (-2705.228, -292.946, -2571.362) |
| 151,152,155,156,159,161,162 | `nav_machine_4` | navpoint | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2708.565, -290.675, -2604.64) | point (no volume) |
| 151,155,159,161 | `trg_machine_approach_4` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2704.493, -290.774, -2599.398) | box, half-extents (8.368, 4.417, 7.735); base at position, centre (-2704.493, -286.357, -2599.398) |
| 171 | `trg_room4_escape` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2707.579, -295.644, -2561.072) | box, half-extents (15.061, 4.017, 20.498); base at position, centre (-2707.579, -291.627, -2561.072) |
| 174 | `trg_backstage_enter_3` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2683.671, -294.893, -2522.018) | box, half-extents (31.867, 5.748, 26.447); base at position, centre (-2683.671, -289.144, -2522.018) |
| 177 | `trg_loadout_3` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2661.824, -295.035, -2545.645) | box, half-extents (1.163, 1.256, 1.163); base at position, centre (-2661.824, -293.779, -2545.645) |
| 180 | `trg_backstage_exit_3` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2645.945, -294.982, -2564.324) | box, half-extents (4.353, 5.748, 4.126); base at position, centre (-2645.945, -289.234, -2564.324) |
| 185 | `trg_hanger_start_saucer_camera` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2639.473, -286.947, -2583.56) | box, half-extents (2.755, 4.017, 4.087); base at position, centre (-2639.473, -282.93, -2583.56) |
| 192,218 | `trg_saucer` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2577.431, -299.393, -2568.301) | box, half-extents (19.521, 8.044, 17.528); base at position, centre (-2577.431, -291.349, -2568.301) |
| 195 | `npc_brutina` (mover) | Script_Npc | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2562.735, -302.6, -2593.455) | point (no volume) |
| 195 | `nav_brutina_mark_1` | navpoint | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2582.694, -298.659, -2588.923) | point (no volume) |
| 198 | `npc_brutina` | Script_Npc | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2562.735, -302.6, -2593.455) | point (no volume) |
| 201,202 | `trg_machine_approach_gen` | Trigger_Volume | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2597.094, -286.868, -2593.558) | box, half-extents (11.687, 4.417, 5.545); base at position, centre (-2597.094, -282.451, -2593.558) |
| 205 | `nav_generator` | navpoint | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2591.839, -286.605, -2591.357) | point (no volume) |
| 215 | `npc_alien_homie` | Script_Npc | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2603.138, -298.682, -2659.3) | point (no volume) |
| 219 | `veh_saucer` | script_vehicle | `` sr3_city~fdlc2_m02`.czn_pc `` | (-2575.351, -292.625, -2567.524) | point (no volume) |

#### dlc3_m01 (stall step 5)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 3 | `friendly_fire_north_trigger` | Trigger_Volume | `` sr3_city~fdlc3_m01`.czn_pc `` | (1511.837, 8.176, -465.78) | box, half-extents (12.276, 7.013, 22.091); base at position, centre (1511.837, 15.189, -465.78) |
| 4 | `friendly_fire_trigger` | Trigger_Volume | `` sr3_city~fdlc3_m01`.czn_pc `` | (1368.563, 25.125, 236.269) | box, half-extents (33.762, 5.856, 20.132); base at position, centre (1368.563, 30.981, 236.269) |
| 13 | `strip_club_trigger` | Trigger_Volume | `` sr3_city~fdlc3_m01`.czn_pc `` | (1050.948, 12.425, -608.675) | box, half-extents (1.636, 1.636, 1.636); base at position, centre (1050.948, 14.061, -608.675); quat (0,0.383,0,0.924) |

#### dlc3_m02 (stall step 2)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 2,13,32 **(stall)** | `start_truck` | script_vehicle | `` sr3_city~fdlc3_m02`.czn_pc `` | (-1299.374, 2.16, 249.995) | point (no volume) |
| 3,5 | `car_trigger_ps02` | Trigger_Volume | `` sr3_city~fdlc3_m02`.czn_pc `` | (-507.442, 11.852, -86.379) | box, half-extents (11.579, 2, 15.533); base at position, centre (-507.442, 13.852, -86.379) |
| 7 | `nav_pierce_sing02` | navpoint | `` sr3_city~fdlc3_m02`.czn_pc `` | (-517.92, 13.887, -86.444) | point (no volume) |
| 14,17,18,21 | `car_trigger_ps01` | Trigger_Volume | `` sr3_city~fdlc3_m02`.czn_pc `` | (418.483, 8.273, 831.396) | box, half-extents (8.369, 2, 16.401); base at position, centre (418.483, 10.273, 831.396) |
| 16,20 | `tagNav02` | navpoint | `` sr3_city~fdlc3_m02`.czn_pc `` | (428.208, 8.273, 811.406) | point (no volume) |
| 24 | `nav_pierce_sing01` | navpoint | `` sr3_city~fdlc3_m02`.czn_pc `` | (429.347, 9.282, 831.177) | point (no volume) |
| 35 | `car_trigger_pwdr` | Trigger_Volume | `` sr3_city~fdlc3_m02`.czn_pc `` | (228.536, 5.337, 643.492) | box, half-extents (20.427, 4.554, 16.086); base at position, centre (228.536, 9.891, 643.492); quat (0,-0.259,0,0.966) |
| 37 | `ms_attacker01` | Script_Npc | `` sr3_city~fdlc3_m02`.czn_pc `` | (222.613, 8.238, 644.564) | point (no volume) |
| 37 | `ms_attacker02` | Script_Npc | `` sr3_city~fdlc3_m02`.czn_pc `` | (224.184, 8.242, 640.096) | point (no volume) |
| 37 | `ms_attacker03` | Script_Npc | `` sr3_city~fdlc3_m02`.czn_pc `` | (227.545, 8.238, 652.161) | point (no volume) |
| 37 | `ms_attacker04` | Script_Npc | `` sr3_city~fdlc3_m02`.czn_pc `` | (223.677, 8.07, 636.374) | point (no volume) |
| 37 | `ms_attacker05` | Script_Npc | `` sr3_city~fdlc3_m02`.czn_pc `` | (227.22, 7.824, 635.042) | point (no volume) |
| 44 | `brutella_truck` (mover) | script_vehicle | `` sr3_city~fdlc3_m02`.czn_pc `` | (170.199, 5.246, 604.727) | point (no volume) |
| 44 | `brute_veh_dest` | navpoint | `` sr3_city~fdlc3_m02`.czn_pc `` | (242.296, 7.989, 641.395) | point (no volume) |
| 46 | `brutella` (mover) | Script_Npc | `` sr3_city~fdlc3_m02`.czn_pc `` | (170.115, 5.367, 606.424) | point (no volume) |
| 46 | `brute_dest01` | navpoint | `` sr3_city~fdlc3_m02`.czn_pc `` | (238.384, 8.238, 648.615) | point (no volume) |
| 47 | `cte_tag` (mover) | Script_Npc | `` sr3_city~fdlc3_m02`.czn_pc `` | (224.448, 8.238, 644.361) | point (no volume) |
| 47 | `brute_dest` | navpoint | `` sr3_city~fdlc3_m02`.czn_pc `` | (236.226, 8.238, 647.375) | point (no volume) |

#### dlc3_m03 (stall step 4)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 8 | `Shaundis_ex_battle_start` | Trigger_Volume | `` sr3_city~fdlc3_m03`.czn_pc `` | (1276.681, 27.041, 136.879) | box, half-extents (91.455, 57.583, 91.455); base at position, centre (1276.681, 84.624, 136.879) |
| 9 | `Shaundis_ex_location` | Trigger_Volume | `` sr3_city~fdlc3_m03`.czn_pc `` | (1274.954, 27.041, 121.687) | box, half-extents (10.01, 7.782, 10.01); base at position, centre (1274.954, 34.823, 121.687) |
| 11 | `Apt_ng_veh_1` | script_vehicle | `` sr3_city~fdlc3_m03`.czn_pc `` | (1235.075, 26.791, 83.558) | point (no volume) |
| 11 | `Apt_ng_veh_2` | script_vehicle | `` sr3_city~fdlc3_m03`.czn_pc `` | (1233.638, 26.791, 90.218) | point (no volume) |
| 11 | `Apt_ng_veh_3` | script_vehicle | `` sr3_city~fdlc3_m03`.czn_pc `` | (1230.873, 26.791, 175.173) | point (no volume) |
| 11 | `Apt_ng_veh_4` | script_vehicle | `` sr3_city~fdlc3_m03`.czn_pc `` | (1231.3, 26.791, 181.342) | point (no volume) |
| 14 | `Planet_saints_location` | Trigger_Volume | `` sr3_city~fdlc3_m03`.czn_pc `` | (1789.133, 12.15, 183.916) | box, half-extents (8.787, 7.066, 8.787); base at position, centre (1789.133, 19.216, 183.916) |
| 17 | `ps_mstar_1_veh` | script_vehicle | `` sr3_city~fdlc3_m03`.czn_pc `` | (1794.428, 12.161, 271.511) | point (no volume) |
| 17 | `ps_mstar_2_veh` | script_vehicle | `` sr3_city~fdlc3_m03`.czn_pc `` | (1743.359, 12.161, 258.935) | point (no volume) |
| 17 | `ps_mstar_3_veh` | script_vehicle | `` sr3_city~fdlc3_m03`.czn_pc `` | (1879.463, 5.148, 245.245) | point (no volume) |
| 17 | `ps_mstar_4_veh` | script_vehicle | `` sr3_city~fdlc3_m03`.czn_pc `` | (1725.2, 12.161, 243.505) | point (no volume) |
| 17 | `ps_mstar_5_veh` | script_vehicle | `` sr3_city~fdlc3_m03`.czn_pc `` | (1821.378, 12.161, 184.664) | point (no volume) |
| 17 | `ps_mstar_6_veh` | script_vehicle | `` sr3_city~fdlc3_m03`.czn_pc `` | (1832.525, 12.161, 174.048) | point (no volume) |
| 24 | `Bridge_battle_start` | Trigger_Volume | `` sr3_city~fdlc3_m03`.czn_pc `` | (-56.377, -7.759, -34.596) | box, half-extents (218.786, 131.559, 127.116); base at position, centre (-56.377, 123.799, -34.596) |
| 39 | `Heli_pickup_location` | Trigger_Volume | `` sr3_city~fdlc3_m03`.czn_pc `` | (-127.804, 31.81, -32.084) | box, half-extents (5.446, 2.266, 13.029); base at position, centre (-127.804, 34.076, -32.084) |

#### m03 (stall step 5)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 2 | `Nav CP Start Local` | navpoint | `` sr3_city~fm03`.czn_pc `` | (44.333, 12.212, 5.022) | point (no volume) |
| 7 | `Trigger Weapon Store2` | Trigger_Volume | `` sr3_city~fm03`.czn_pc `` | (-468.782, 6.542, -849.18) | sphere, radius 3.276 |
| 9 | `Trigger Armory Entrance` | Trigger_Volume | `` sr3_city~fm03`.czn_pc `` | (-796.771, -0.057, 288.407) | box, half-extents (203.942, 52.639, 139.698); base at position, centre (-796.771, 52.583, 288.407); quat (-0,-1,-0,-0) |
| 11 | `Trigger Weapons Cache` | Trigger_Volume | `` sr3_city~fm03`.czn_pc `` | (-790.672, 4.04, 261.317) | box, half-extents (3.774, 0.743, 3.605); base at position, centre (-790.672, 4.783, 261.317); quat (0,0.702,0,0.712) |
| 18 | `Trigger Satellite Weapon` | Trigger_Volume | `` sr3_city~fm03`.czn_pc `` | (-784.774, 4.017, 261.731) | cylinder, radius 1.624, length 2.618; quat (-0,-0.969,-0,-0.249) |
| 21 | `Tank1_tank` | script_vehicle | `` sr3_city~fm03`.czn_pc `` | (-549.182, 8.238, 323.656) | point (no volume) |
| 21 | `Tank1_group` | script_group | `` sr3_city~fm03`.czn_pc `` | (0, 0, 0) | point (no volume) |
| 21 | `Tank2_tank` | script_vehicle | `` sr3_city~fm03`.czn_pc `` | (-917.093, 11.784, 60.01) | point (no volume) |
| 21 | `Tank2_group` | script_group | `` sr3_city~fm03`.czn_pc `` | (0, 0, 0) | point (no volume) |
| 21 | `Tank3_tank` | script_vehicle | `` sr3_city~fm03`.czn_pc `` | (-1058.918, 8.312, 193.447) | point (no volume) |
| 21 | `Tank3_group` | script_group | `` sr3_city~fm03`.czn_pc `` | (0, 0, 0) | point (no volume) |
| 25 | `Veh Chopper Pierce Normal` (mover) | script_vehicle | `` sr3_city~fm03`.czn_pc `` | (-776.847, 24.022, 294.644) | point (no volume) |
| 25 | `Nav Heli Landing Pos` | navpoint | `` sr3_city~fm03`.czn_pc `` | (-714.986, 8.578, 255.191) | point (no volume) |
| 26 | `Enter_Heli_trigger` | Trigger_Volume | `` sr3_city~fm03`.czn_pc `` | (-712.171, 4.103, 252.516) | box, half-extents (8.966, 1.526, 8.966); base at position, centre (-712.171, 5.629, 252.516); quat (0,-0.712,0,0.702) |
| 33,34 | `Veh Chopper Bomb` (mover) | script_vehicle | `` sr3_city~fm03`.czn_pc `` | (-779.296, 24.021, 316.824) | point (no volume) |
| 33,34 | `Nav Shaundi House` | navpoint | `` sr3_city~fm03`.czn_pc `` | (1268.533, 43.582, 104.573) | point (no volume) |

#### m04 (stall step 5)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 14 | `Morningstar Brute` | Script_Npc | `` sr3_city~fm04`.czn_pc `` | (1728.448, 5.136, -437.572) | point (no volume) |
| 15 | `Notoriety_Trigger 001` | Trigger_Volume | `` sr3_city~fm04`.czn_pc `` | (1273.108, 27.105, 123.173) | sphere, radius 333.142 |

#### m05 (stall step 4)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 4 **(stall)** | `m05_trigger_land` | Trigger_Volume | `` sr3_city~fm05`.czn_pc `` | (95.992, 139.808, 1048.1) | box, half-extents (33.122, 8.813, 43.813); base at position, centre (95.992, 148.621, 1048.1); quat (-0,-1,-0,0) |
| 9 | `m05_trigger_downstairs` | Trigger_Volume | `` sr3_city~fm05`.czn_pc `` | (124.763, 143.965, 1078.718) | box, half-extents (2.058, 1.669, 2.035); base at position, centre (124.763, 145.634, 1078.718) |
| 10 | `m05_trigger_grenades` | Trigger_Volume | `` sr3_city~fm05`.czn_pc `` | (120.455, 141.545, 1087.835) | box, half-extents (0.493, 0.731, 0.779); base at position, centre (120.455, 142.276, 1087.835); quat (0,0.383,0,0.924) |
| 11 | `m05_trigger_barricade` | Trigger_Volume | `` sr3_city~fm05`.czn_pc `` | (91.459, 141.328, 1078.699) | box, half-extents (22.475, 1.361, 16.747); base at position, centre (91.459, 142.69, 1078.699) |
| 12 | `m05_trigger_saints` | Trigger_Volume | `` sr3_city~fm05`.czn_pc `` | (98.325, 141.544, 1090.527) | cylinder, radius 1.826, length 2.39; quat (0,0.707,0,0.707) |
| 19 | `m05_trigger_chase` | Trigger_Volume | `` sr3_city~fm05`.czn_pc `` | (97.617, 90.619, 1022.485) | box, half-extents (31.502, 28.786, 20.624); base at position, centre (97.617, 119.405, 1022.485); quat (-0,-1,-0,0) |
| 20 | `m05_heli_chase_host` (mover) | script_vehicle | `` sr3_city~fm05`.czn_pc `` | (71.905, 90, 1019.567) | point (no volume) |
| 20 | `m05_nav_hover_host` | navpoint | `` sr3_city~fm05`.czn_pc `` | (78.359, 158.105, 1002.683) | point (no volume) |
| 23 | `m05_trigger_chase_end` | Trigger_Volume | `` sr3_city~fm05`.czn_pc `` | (2100.062, -4.61, -278.673) | box, half-extents (73.712, 30, 32.28); base at position, centre (2100.062, 25.39, -278.673) |
| 26 | `m05_target_chase_host` | Script_Npc | `` sr3_city~fm05`.czn_pc `` | (86.653, 140.757, 1010.573) | point (no volume) |

#### m06 (stall step 8)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 7 | `Dock_trigger` | Trigger_Volume | `` sr3_city~fm06`.czn_pc `` | (-193.76, 16.159, -769.237) | cylinder, radius 14.082, length 11.621 |
| 8 **(stall)** | `notice_snipers` | Trigger_Volume | `` sr3_city~fm06`.czn_pc `` | (-243.699, 19.457, -767.511) | box, half-extents (25.756, 4.29, 27.655); base at position, centre (-243.699, 23.747, -767.511) |
| 9,10 | `Dock_sniper 001` | Script_Npc | `` sr3_city~fm06`.czn_pc `` | (-259.579, 23.813, -794.752) | point (no volume) |
| 9,10 | `Dock_thug 007` | Script_Npc | `` sr3_city~fm06`.czn_pc `` | (-241.487, 19.659, -787.55) | point (no volume) |
| 9,10 | `Dock_thug 008` | Script_Npc | `` sr3_city~fm06`.czn_pc `` | (-251.061, 20.145, -788.595) | point (no volume) |
| 9,10 | `Dock_thug 009` | Script_Npc | `` sr3_city~fm06`.czn_pc `` | (-243.969, 19.651, -781.147) | point (no volume) |
| 9,10 | `Dock_thug 011` | Script_Npc | `` sr3_city~fm06`.czn_pc `` | (-251.414, 23.842, -799.253) | point (no volume) |
| 9,10 | `Dock_thug 004<001>` | Script_Npc | `` sr3_city~fm06`.czn_pc `` | (-255.732, 20.126, -791.425) | point (no volume) |
| 11 | `Dock_thug 002` | Script_Npc | `` sr3_city~fm06`.czn_pc `` | (-257.21, 19.674, -803.412) | point (no volume) |
| 11 | `Dock_thug 001` | Script_Npc | `` sr3_city~fm06`.czn_pc `` | (-254.531, 19.643, -802.524) | point (no volume) |
| 11 | `Dock_thug 018` | Script_Npc | `` sr3_city~fm06`.czn_pc `` | (-223.843, 19.766, -791.647) | point (no volume) |
| 12 | `Dock_sniper 003` | Script_Npc | `` sr3_city~fm06`.czn_pc `` | (-257.993, 23.668, -741.491) | point (no volume) |
| 12 | `Dock_thug 003` | Script_Npc | `` sr3_city~fm06`.czn_pc `` | (-256.698, 23.696, -740.52) | point (no volume) |
| 12 | `Dock_thug 004` | Script_Npc | `` sr3_city~fm06`.czn_pc `` | (-255.205, 23.705, -741.823) | point (no volume) |
| 12 | `Dock_thug 006` | Script_Npc | `` sr3_city~fm06`.czn_pc `` | (-257.238, 23.742, -739.064) | point (no volume) |
| 13 | `Dock_thug 013` | Script_Npc | `` sr3_city~fm06`.czn_pc `` | (-277.337, 21.581, -759.178) | point (no volume) |
| 13 | `Dock_thug 015` | Script_Npc | `` sr3_city~fm06`.czn_pc `` | (-277.279, 21.581, -760.335) | point (no volume) |
| 13 | `Dock_thug 017` | Script_Npc | `` sr3_city~fm06`.czn_pc `` | (-278.841, 21.581, -759.171) | point (no volume) |
| 14 | `bomb_trigger` | Trigger_Volume | `` sr3_city~fm06`.czn_pc `` | (-240.355, 19.78, -780.088) | cylinder, radius 6.158, length 2.168 |
| 15,19 | `factory_breadcrumb_003` | Trigger_Volume | `` sr3_city~fm06`.czn_pc `` | (-249.34, 80.295, -791.301) | box, half-extents (7.565, 2.181, 2.084); base at position, centre (-249.34, 82.476, -791.301); quat (-0,-1,-0,0) |
| 17 | `Oleg_trigger` | Trigger_Volume | `` sr3_city~fm06`.czn_pc `` | (-248.857, 84.234, -747.253) | box, half-extents (3.792, 4.35, 7.426); base at position, centre (-248.857, 88.584, -747.253) |
| 21 | `At_stairs_trigger 001` | Trigger_Volume | `` sr3_city~fm06`.czn_pc `` | (-264.677, 911.88, -785.837) | cylinder, radius 6.757, length 2.333; quat (0,0.467,0,0.884) |
| 22 | `At_office_trigger` | Trigger_Volume | `` sr3_city~fm06`.czn_pc `` | (-263.209, 915.727, -770.062) | cylinder, radius 6.393, length 2.59; quat (0,-0.145,0,0.989) |
| 23 | `Phillipe_trigger` | Trigger_Volume | `` sr3_city~fm06`.czn_pc `` | (-255.175, 919.791, -749.55) | box, half-extents (3.054, 1.21, 3.054); base at position, centre (-255.175, 921.001, -749.55) |
| 38 | `bomb_disarm` | Trigger_Volume | `` sr3_city~fm06`.czn_pc `` | (-238.961, 19.78, -779.862) | box, half-extents (3.914, 1.084, 2.303); base at position, centre (-238.961, 20.864, -779.862) |

#### m08 (stall step 3)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 11,13,17,18 | `NPC Sniper 001` | Script_Npc | `` sr3_city~fm08`.czn_pc `` | (244.971, 124.746, 1052.165) | point (no volume) |
| 11,13,17,18 | `NPC Sniper 002` | Script_Npc | `` sr3_city~fm08`.czn_pc `` | (156.806, 161.774, 1029.103) | point (no volume) |
| 11,13,17,18 | `NPC Sniper 003` | Script_Npc | `` sr3_city~fm08`.czn_pc `` | (135.955, 137.265, 968.771) | point (no volume) |
| 11,13,17,18 | `NPC Sniper 004` | Script_Npc | `` sr3_city~fm08`.czn_pc `` | (23.66, 208.012, 971.2) | point (no volume) |
| 11,13,17,18 | `NPC Sniper 005` | Script_Npc | `` sr3_city~fm08`.czn_pc `` | (161.513, 171.99, 1044.312) | point (no volume) |
| 11,13,17,18 | `NPC Sniper 006` | Script_Npc | `` sr3_city~fm08`.czn_pc `` | (73.769, 208.047, 971.184) | point (no volume) |
| 11,13,17,18 | `NPC Sniper 007` | Script_Npc | `` sr3_city~fm08`.czn_pc `` | (21.148, 167.452, 1027.149) | point (no volume) |
| 12 | `Trigger Rooftop` | Trigger_Volume | `` sr3_city~fm08`.czn_pc `` | (97.934, 153.7, 1073.721) | box, half-extents (26.907, 1.348, 22.617); base at position, centre (97.934, 155.048, 1073.721) |
| 15 | `Trigger Circuit Breaker` | Trigger_Volume | `` sr3_city~fm08`.czn_pc `` | (84.179, 141.559, 1066.782) | cylinder, radius 1, length 1; quat (0,0.707,0,0.707) |

#### m09 (stall step 4)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 7 | `North_vantage_point_trigger` | Trigger_Volume | `` sr3_city~fm09`.czn_pc `` | (2473.771, 12.145, -22.593) | box, half-extents (2.624, 1.039, 2.6); base at position, centre (2473.771, 13.185, -22.593) |
| 10 | `heli_entrance_trigger` | Trigger_Volume | `` sr3_city~fm09`.czn_pc `` | (2473.486, 12.529, -23.994) | box, half-extents (1.661, 1.498, 0.682); base at position, centre (2473.486, 14.027, -23.994) |
| 17 | `to_saints_trigger` | Trigger_Volume | `` sr3_city~fm09`.czn_pc `` | (598.11, 4.417, -152.854) | box, half-extents (12.21, 12.21, 12.21); base at position, centre (598.11, 16.627, -152.854) |

#### m10 (stall step 5)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 4 | `tgr_club_door 001` | Trigger_Volume | `` sr3_city~fm10`.czn_pc `` | (996.067, 12.443, -604.469) | box, half-extents (1.667, 1.312, 1.667); base at position, centre (996.067, 13.754, -604.469); quat (0,0.383,0,0.924) |
| 5,6,40 **(stall)** | `tgr_front_door 001` | Trigger_Volume | `` sr3_city~fm10`.czn_pc `` | (1031.468, 12.282, -600.467) | cylinder, radius 2.351, length 2.175; quat (0,0.38,0,0.925) |
| 14 | `Group_stag_transport 002` | script_group | `` sr3_city~fm10`.czn_pc `` | (0, 0, 0) | point (no volume) |
| 15,17 | `Group_stag_transpot 002b` | script_group | `` sr3_city~fm10`.czn_pc `` | (0, 0, 0) | point (no volume) |
| 37 | `tgr_rooftop_elevator 001` | Trigger_Volume | `` sr3_city~fm10`.czn_pc `` | (995.106, 31.524, -625.976) | cylinder, radius 1.29, length 2.175; quat (0,0.38,0,0.925) |
| 41 | `tgr_club_exit 001` | Trigger_Volume | `` sr3_city~fm10`.czn_pc `` | (1051.275, 12.15, -608.837) | box, half-extents (1.804, 1.673, 1.804); base at position, centre (1051.275, 13.823, -608.837); quat (0,0.383,0,0.924) |
| 42 | `tgr_saints_hq 001` | Trigger_Volume | `` sr3_city~fm10`.czn_pc `` | (98.276, 8.706, 1015.348) | cylinder, radius 12.919, length 2.407 |

#### m13 (stall step 4)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 3 | `trigger_pr_center_front` | Trigger_Volume | `` sr3_city~fm13`.czn_pc `` | (-416.274, 11.889, -139.44) | box, half-extents (55.953, 4.925, 56.137); base at position, centre (-416.274, 16.813, -139.44) |
| 5,6,7 | `mover_mission_accomplished 001` | Script_Mover | `` sr3_city~fm13`.czn_pc `` | (-402.947, 22.527, -142.918) | point (no volume) |
| 5,6,7 | `mover_mission_accomplished 002` | Script_Mover | `` sr3_city~fm13`.czn_pc `` | (-425.276, 22.381, -142.683) | point (no volume) |
| 11 | `trigger_saints_hq_front` | Trigger_Volume | `` sr3_city~fm13`.czn_pc `` | (98.124, 7.838, 986.561) | box, half-extents (21.989, 14.32, 29.316); base at position, centre (98.124, 22.159, 986.561) |
| 12,42 | `trigger_elevator_proximity` | Trigger_Volume | `` sr3_city~fm13`.czn_pc `` | (98.215, 8.583, 1052.196) | box, half-extents (3.616, 1.474, 1.803); base at position, centre (98.215, 10.058, 1052.196); quat (-0,-1,-0,0) |
| 34 | `trigger_elevator_to_ground` | Trigger_Volume | `` sr3_city~fm13`.czn_pc `` | (98.017, 31.947, 1043.249) | box, half-extents (1.173, 1.306, 0.5); base at position, centre (98.017, 33.253, 1043.249); quat (-0,-1,-0,0) |
| 61 | `trigger_elevator_to_penthouse` | Trigger_Volume | `` sr3_city~fm13`.czn_pc `` | (98.017, 31.947, 1043.26) | box, half-extents (1.357, 1.511, 0.578); base at position, centre (98.017, 33.457, 1043.26); quat (-0,-1,-0,0) |
| 64 | `trigger_penthouse_cutscene` | Trigger_Volume | `` sr3_city~fm13`.czn_pc `` | (95.776, 143.683, 1030.36) | box, half-extents (14.728, 3.14, 2.426); base at position, centre (95.776, 146.823, 1030.36) |

#### m14 (stall step 3)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 9 | `Trigger Heli Validation 001` | Trigger_Volume | `` sr3_city~fm14`.czn_pc `` | (1415.997, 99.311, 199.313) | box, half-extents (11.451, 1.839, 11.59); base at position, centre (1415.997, 101.15, 199.313) |
| 11 | `Trigger Tower 001` | Trigger_Volume | `` sr3_city~fm14`.czn_pc `` | (1376.791, 93.068, 162.044) | cylinder, radius 0.714, length 1.32 |
| 14 | `Nav T2 Helipad` | navpoint | `` sr3_city~fm14`.czn_pc `` | (203.66, 236.196, 238.519) | point (no volume) |
| 16 | `Trigger Heli Validation 002` | Trigger_Volume | `` sr3_city~fm14`.czn_pc `` | (203.628, 233.99, 238.702) | box, half-extents (12.619, 2.288, 12.823); base at position, centre (203.628, 236.278, 238.702) |
| 17 | `Trigger Tower 002` | Trigger_Volume | `` sr3_city~fm14`.czn_pc `` | (174.191, 265.222, 239.513) | cylinder, radius 0.689, length 1.473; quat (0,-0.259,0,0.966) |
| 24,28,39 | `Trigger Helipad` | Trigger_Volume | `` sr3_city~fm14`.czn_pc `` | (-813.084, 61.377, -260.274) | box, half-extents (18.07, 2.01, 24.015); base at position, centre (-813.084, 63.386, -260.274) |
| 29 | `Trigger Interview Stairs` | Trigger_Volume | `` sr3_city~fm14`.czn_pc `` | (-827.142, 57.311, -238.306) | box, half-extents (3.883, 1.63, 1.9); base at position, centre (-827.142, 58.941, -238.306) |
| 31 | `NPC Decker Studio 001` | Script_Npc | `` sr3_city~fm14`.czn_pc `` | (-805.283, 53.265, -245.156) | point (no volume) |
| 31 | `NPC Decker Studio 002` | Script_Npc | `` sr3_city~fm14`.czn_pc `` | (-813.962, 53.265, -281.87) | point (no volume) |
| 31 | `NPC Decker Studio 003` | Script_Npc | `` sr3_city~fm14`.czn_pc `` | (-802.199, 53.261, -277.876) | point (no volume) |
| 31 | `NPC Decker Studio 004` | Script_Npc | `` sr3_city~fm14`.czn_pc `` | (-807.769, 53.262, -251.284) | point (no volume) |
| 31 | `NPC Decker Studio 005` | Script_Npc | `` sr3_city~fm14`.czn_pc `` | (-804.147, 53.273, -257.665) | point (no volume) |
| 31 | `NPC Decker Studio 008` | Script_Npc | `` sr3_city~fm14`.czn_pc `` | (-807.068, 53.262, -268.613) | point (no volume) |
| 31 | `NPC Decker Studio 006` | Script_Npc | `` sr3_city~fm14`.czn_pc `` | (-797.607, 53.281, -250.559) | point (no volume) |
| 31 | `NPC Decker Studio 007` | Script_Npc | `` sr3_city~fm14`.czn_pc `` | (-808.264, 53.271, -280.477) | point (no volume) |
| 32 | `NPC Decker Elevator RB 001` | Script_Npc | `` sr3_city~fm14`.czn_pc `` | (-798.391, 53.293, -263.243) | point (no volume) |
| 32 | `NPC Decker Elevator RB 002` | Script_Npc | `` sr3_city~fm14`.czn_pc `` | (-797.986, 53.289, -261.881) | point (no volume) |
| 33 | `NPC Decker Side RB 001` | Script_Npc | `` sr3_city~fm14`.czn_pc `` | (-812.067, 53.271, -239.301) | point (no volume) |
| 34 | `NPC Decker Stairs RB 001` | Script_Npc | `` sr3_city~fm14`.czn_pc `` | (-829.093, 57.063, -239.883) | point (no volume) |
| 34 | `NPC Decker Stairs RB 002` | Script_Npc | `` sr3_city~fm14`.czn_pc `` | (-829.112, 57.091, -241.361) | point (no volume) |
| 34 | `NPC Decker Stairs RB 003` | Script_Npc | `` sr3_city~fm14`.czn_pc `` | (-826.463, 57.42, -238.588) | point (no volume) |
| 37 | `Trigger Stairs1` | Trigger_Volume | `` sr3_city~fm14`.czn_pc `` | (-829.093, 53.187, -250.664) | box, half-extents (1.925, 1.63, 1.9); base at position, centre (-829.093, 54.817, -250.664) |
| 38 | `Trigger Stairs2` | Trigger_Volume | `` sr3_city~fm14`.czn_pc `` | (-827.21, 57.311, -238.306) | box, half-extents (3.883, 1.63, 1.9); base at position, centre (-827.21, 58.941, -238.306) |
| 45 | `Veh Killbane Escape` | script_vehicle | `` sr3_city~fm14`.czn_pc `` | (-1054.747, 25.292, -301.591) | point (no volume) |

#### m15 (stall step 4)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 3 | `trigger_pr_center_radius` | Trigger_Volume | `` sr3_city~fm15`.czn_pc `` | (-434.065, -0, -154.384) | sphere, radius 180.492 |
| 4 **(stall)** | `trigger_pr_center_front` | Trigger_Volume | `` sr3_city~fm15`.czn_pc `` | (-414.341, 12.174, -156.117) | box, half-extents (26.512, 13.162, 19.917); base at position, centre (-414.341, 25.336, -156.117); quat (-0,-1,-0,-0) |
| 5 | `trigger_pr_center_enterance` | Trigger_Volume | `` sr3_city~fm15`.czn_pc `` | (-414.343, 12.174, -135.471) | box, half-extents (4.351, 2.103, 0.97); base at position, centre (-414.343, 14.277, -135.471); quat (-0,-1,-0,-0) |
| 9 | `trigger_elevator_to_garage 002` | Trigger_Volume | `` sr3_city~fm15`.czn_pc `` | (-410.159, 12.176, -90.073) | box, half-extents (1.645, 0.5, 1.724); base at position, centre (-410.159, 12.676, -90.073); quat (-0,-1,-0,-0) |
| 12 | `vehicle_tank_garage` | script_vehicle | `` sr3_city~fm15`.czn_pc `` | (-336.376, 2.58, -95.822) | point (no volume) |
| 13 | `trigger_garage_door` | Trigger_Volume | `` sr3_city~fm15`.czn_pc `` | (-325.151, 2.519, -128.935) | box, half-extents (4.12, 2.272, 2.587); base at position, centre (-325.151, 4.791, -128.935) |

#### m16 (stall step 8)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 14 | `Gate1_Trigger` | Trigger_Volume | `` sr3_city~fm16`.czn_pc `` | (-1903.733, 1100.81, 1661.506) | cylinder, radius 45.985, length 3.091 |
| 15 | `Trigger_Firewall` | Trigger_Volume | `` sr3_city~fm16`.czn_pc `` | (-1905.823, 1100.375, 1636.487) | box, half-extents (34.72, 1.546, 5.911); base at position, centre (-1905.823, 1101.921, 1636.487) |
| 21 | `Gate2_Trigger` | Trigger_Volume | `` sr3_city~fm16`.czn_pc `` | (-1951.695, 1108.055, 1491.013) | cylinder, radius 36.558, length 3.091; quat (-0,0.879,-0,0.477) |
| 30 | `Gate3_Trigger` | Trigger_Volume | `` sr3_city~fm16`.czn_pc `` | (-2116.731, 1109.671, 1639.418) | cylinder, radius 35.627, length 3.091; quat (0,-1,0,0) |
| 36 | `Final_Trigger` | Trigger_Volume | `` sr3_city~fm16`.czn_pc `` | (-2025.432, 1132.084, 1619.109) | cylinder, radius 2.728, length 3.091 |
| 67 | `Company_B_Trigger` | Trigger_Volume | `` sr3_city~fm16`.czn_pc `` | (-1949.995, 1142.102, 1920.975) | cylinder, radius 1.782, length 1 |

#### m18 (stall step 3)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 3 **(stall)** | `mov_cabin_door 001` | Script_Mover | `` sr3_city~fm18`.czn_pc `` | (810.458, 1405.197, 415.766) | point (no volume) |
| 5 | `mov_cockpit_door 001` | Script_Mover | `` sr3_city~fm18`.czn_pc `` | (810.401, 1405.197, 452.702) | point (no volume) |
| 9 | `tgr_tank_region 001` | Trigger_Volume | `` sr3_city~fm18`.czn_pc `` | (815.367, 1401.242, 363.62) | cylinder, radius 3.4, length 3.364 |
| 22 | `veh_player_tank 001` (mover) | script_vehicle | `` sr3_city~fm18`.czn_pc `` | (807.478, 1345.055, 28.756) | point (no volume) |
| 22 | `nav_player_tank_center` | navpoint | `` sr3_city~fm18`.czn_pc `` | (807.376, 1346.314, 28.504) | point (no volume) |

#### m19 (stall step 3)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 3 **(stall)** | `doors_trigger` | Trigger_Volume | `` sr3_city~fm19`.czn_pc `` | (508.479, 7.856, -153.395) | box, half-extents (56.478, 1.589, 7.375); base at position, centre (508.479, 9.445, -153.395) |
| 8 | `m19_trigger_dumpster` | Trigger_Volume | `` sr3_city~fm19`.czn_pc `` | (912.149, 10.81, -184.767) | box, half-extents (2.321, 1.194, 1.167); base at position, centre (912.149, 12.003, -184.767) |
| 9 | `m19_trigger_roof` | Trigger_Volume | `` sr3_city~fm19`.czn_pc `` | (915.336, 13.167, -175.447) | box, half-extents (8.4, 0.973, 7.676); base at position, centre (915.336, 14.139, -175.447) |
| 10 | `Suko_inside` | Script_Npc | `` sr3_city~fm19`.czn_pc `` | (915.164, 13.151, -176.06) | point (no volume) |
| 17 | `viola_mask_search_nav` | navpoint | `` sr3_city~fm19`.czn_pc `` | (1023.196, 8.178, 32.223) | point (no volume) |
| 26,29 | `chem_truck` | script_vehicle | `` sr3_city~fm19`.czn_pc `` | (839.926, 8.438, -152.909) | point (no volume) |
| 28 | `Escape_trigger 001` | Trigger_Volume | `` sr3_city~fm19`.czn_pc `` | (1115.893, 7.062, -262.327) | box, half-extents (25, 10.785, 25); base at position, centre (1115.893, 17.847, -262.327); quat (0,0.383,0,0.924) |

#### m21 (stall step 3)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 15 | `Chainsaw_Wave1_Group` | script_group | `` sr3_city~fm21`.czn_pc `` | (0, 0, 0) | point (no volume) |
| 16 | `Chainsaw_Wave2_Group` | script_group | `` sr3_city~fm21`.czn_pc `` | (0, 0, 0) | point (no volume) |
| 17 | `Chainsaw_Wave3_Group` | script_group | `` sr3_city~fm21`.czn_pc `` | (0, 0, 0) | point (no volume) |
| 18 | `Chainsaw_Wave4_Group` | script_group | `` sr3_city~fm21`.czn_pc `` | (0, 0, 0) | point (no volume) |
| 19 | `Player_Reached_Angel_Trigger 001` | Trigger_Volume | `` sr3_city~fm21`.czn_pc `` | (2700.345, 128.488, -2521.517) | cylinder, radius 0.517, length 1 |
| 21 | `Player_Enter_Ring_Trigger 001` | Trigger_Volume | `` sr3_city~fm21`.czn_pc `` | (2703.684, 128.068, -2527.19) | box, half-extents (0.296, 0.5, 0.296); base at position, centre (2703.684, 128.568, -2527.19) |

#### m22 (stall step 5)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 4,5 **(stall)** | `BZ2_Spawn_Trigger` | Trigger_Volume | `` sr3_city~fm22`.czn_pc `` | (-66.482, 12.15, 64.91) | sphere, radius 193.885 |
| 4,5 **(stall)** | `BZ3_Spawn_Trigger` | Trigger_Volume | `` sr3_city~fm22`.czn_pc `` | (1296.773, 8.488, -453.883) | sphere, radius 142.919 |
| 4,5 **(stall)** | `BZ4_Spawn_Trigger` | Trigger_Volume | `` sr3_city~fm22`.czn_pc `` | (1199.977, 12.527, 1142.887) | sphere, radius 177.178 |
| 7 | `Armory_Spawn_Trigger` | Trigger_Volume | `` sr3_city~fm22`.czn_pc `` | (-881.265, 4.26, 213.975) | sphere, radius 239.762 |
| 21 | `Kill_Killbane_Trigger` | Trigger_Volume | `` sr3_city~fm22`.czn_pc `` | (-1689.661, 8.552, -419.708) | sphere, radius 211.065 |
| 24 | `Angel_Car` (mover) | script_vehicle | `` sr3_city~fm22`.czn_pc `` | (-1708.072, 8.165, -419.164) | point (no volume) |
| 24 | `Airport_Rail_Begin` | navpoint | `` sr3_city~fm22`.czn_pc `` | (-1549.169, 16.089, -1142.674) | point (no volume) |
| 25 | `Killbane_Plane` | script_vehicle | `` sr3_city~fm22`.czn_pc `` | (-1160.523, 18.278, -1181.221) | point (no volume) |
| 35 | `Statue_Elevator_Trigger` | Trigger_Volume | `` sr3_city~fm22`.czn_pc `` | (-638.428, 6.779, 1050.721) | cylinder, radius 1.549, length 1 |
| 38,40 | `C4_Mover 005` | Script_Mover | `` sr3_city~fm22`.czn_pc `` | (-643.514, 45.661, 990.381) | point (no volume) |
| 38,40 | `C4_Mover 006` | Script_Mover | `` sr3_city~fm22`.czn_pc `` | (-611.242, 45.576, 1012.187) | point (no volume) |
| 38,40 | `C4_Mover 007` | Script_Mover | `` sr3_city~fm22`.czn_pc `` | (-666.106, 45.578, 1037.925) | point (no volume) |
| 38,40 | `C4_Mover 008` | Script_Mover | `` sr3_city~fm22`.czn_pc `` | (-633.628, 45.664, 1044.599) | point (no volume) |
| 39 | `Second_Floor_Trigger` | Trigger_Volume | `` sr3_city~fm22`.czn_pc `` | (-664.51, 64.541, 1018.448) | cylinder, radius 1, length 1 |
| 41 | `Kia_Breadcrumb_Trigger` | Trigger_Volume | `` sr3_city~fm22`.czn_pc `` | (-627.189, 66.56, 997.547) | cylinder, radius 1, length 1 |
| 42 | `Player_reached_top_trigger` | Trigger_Volume | `` sr3_city~fm22`.czn_pc `` | (-648.736, 94.249, 1031.383) | cylinder, radius 1.66, length 1 |
| 46 | `Kia_gameplay` | Script_Npc | `` sr3_city~fm22`.czn_pc `` | (-639.452, 94.643, 1013.188) | point (no volume) |

#### m24 (stall step 7)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 7 **(stall)** | `Terminal_Trigger 001` | Trigger_Volume | `` sr3_city~fm24`.czn_pc `` | (-142.276, 1080.206, -2593.748) | cylinder, radius 1.427, length 1 |
| 9,15,18 | `breadcrumb_trigger_01` | Trigger_Volume | `` sr3_city~fm24`.czn_pc `` | (-101.296, 1081.325, -2643.7) | box, half-extents (12.986, 5.649, 1.495); base at position, centre (-101.296, 1086.973, -2643.7); quat (0,-0.333,0,0.943) |
| 9,15,18 | `breadcrumb_trigger_02` | Trigger_Volume | `` sr3_city~fm24`.czn_pc `` | (-19.491, 1084.276, -2625.581) | box, half-extents (6.486, 5.633, 2.308); base at position, centre (-19.491, 1089.909, -2625.581) |
| 10 | `Terminal_Trigger 002` | Trigger_Volume | `` sr3_city~fm24`.czn_pc `` | (3.328, 1080.188, -2593.113) | cylinder, radius 1.427, length 1 |
| 12,15,18 | `breadcrumb_trigger_03` | Trigger_Volume | `` sr3_city~fm24`.czn_pc `` | (58.406, 1079.63, -2626.658) | box, half-extents (1.675, 3.393, 19.731); base at position, centre (58.406, 1083.024, -2626.658) |
| 13 | `Terminal_Trigger 003` | Trigger_Volume | `` sr3_city~fm24`.czn_pc `` | (94.803, 1087.849, -2701.037) | cylinder, radius 1.427, length 1 |
| 16 | `Terminal_Trigger 004` | Trigger_Volume | `` sr3_city~fm24`.czn_pc `` | (-91.597, 1087.704, -2689.908) | cylinder, radius 1.427, length 1 |
| 19 | `Terminal_Trigger 005` | Trigger_Volume | `` sr3_city~fm24`.czn_pc `` | (2.381, 1086.37, -2551.617) | cylinder, radius 1.427, length 1 |
| 22 | `breadcrumb_trigger_04` | Trigger_Volume | `` sr3_city~fm24`.czn_pc `` | (33.372, 1089.618, -2752.907) | cylinder, radius 29.913, length 15.288 |
| 23 | `Junction_Trigger 002` | Trigger_Volume | `` sr3_city~fm24`.czn_pc `` | (30.983, 1095.348, -2776.687) | cylinder, radius 1.427, length 1 |
| 23 | `Junction_Trigger 003` | Trigger_Volume | `` sr3_city~fm24`.czn_pc `` | (4.192, 1095.376, -2762.961) | cylinder, radius 1.427, length 1; quat (0,-0.035,0,0.999) |
| 29,30 | `End_of_bridge_trigger` | Trigger_Volume | `` sr3_city~fm24`.czn_pc `` | (-38.433, 1113.685, -2888.597) | box, half-extents (2.08, 4.086, 8.474); base at position, centre (-38.433, 1117.771, -2888.597); quat (0,-0.51,0,0.86) |
| 35 | `Killbane` | Script_Npc | `` sr3_city~fm24`.czn_pc `` | (-96.448, 1117.363, -2944.932) | point (no volume) |

#### mm_m1_5 (stall step 2)

No named zone object on this route: every wait is engine-, timer- or "any vehicle"-driven, or the names are not zone objects.

#### mm_p_01 (stall step 1)

No named zone object on this route: every wait is engine-, timer- or "any vehicle"-driven, or the names are not zone objects.

#### mm_p_03 (stall step 3)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 9 | `MM_P_03_Nav_FP02` | navpoint | `` sr3_city~fmm_p_03`.czn_pc `` | (-355.543, 16.152, -751.658) | point (no volume) |

#### mm_p_06 (stall step 1)

No named zone object on this route: every wait is engine-, timer- or "any vehicle"-driven, or the names are not zone objects.

#### mm_p_07 (stall step 2)

No named zone object on this route: every wait is engine-, timer- or "any vehicle"-driven, or the names are not zone objects.

#### sh01 (stall step 3)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 3 **(stall)** | `Warehouse_Door_Trigger 001` | Trigger_Volume | `` sr3_city~fsh01`.czn_pc `` | (377.469, 12.184, 70.42) | cylinder, radius 1.877, length 1.028 |
| 4 | `trig_BreakRoom` | Trigger_Volume | `` sr3_city~fsh01`.czn_pc `` | (405.52, 12.178, 101.164) | box, half-extents (1.552, 1.279, 1.552); base at position, centre (405.52, 13.457, 101.164) |
| 5 | `Upstairs_Trigger 001` | Trigger_Volume | `` sr3_city~fsh01`.czn_pc `` | (404.674, 12.336, 111.329) | cylinder, radius 1, length 1; quat (0,-0.708,0,0.707) |
| 9 | `MiniGun_Brute 001` | Script_Npc | `` sr3_city~fsh01`.czn_pc `` | (382.3, 12.207, 109.345) | point (no volume) |

#### sh02 (stall step 2)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 7 | `trig_collection<001>` | Trigger_Volume | `` sr3_city~fsh02`.czn_pc `` | (-1213.053, 40.195, -534.636) | box, half-extents (2, 2, 4); base at position, centre (-1213.053, 42.195, -534.636); quat (0,0.707,0,0.707) |
| 8 | `trig_collection<002>` | Trigger_Volume | `` sr3_city~fsh02`.czn_pc `` | (-1198.931, 40.195, -542.639) | box, half-extents (10, 2, 6); base at position, centre (-1198.931, 42.195, -542.639); quat (0,0.707,0,0.707) |
| 9 | `trig_override<001>` | Trigger_Volume | `` sr3_city~fsh02`.czn_pc `` | (-1200.166, 40.195, -545.392) | cylinder, radius 1, length 1; quat (0,0.787,0,0.617) |
| 11 | `trig_stairwell<001>` | Trigger_Volume | `` sr3_city~fsh02`.czn_pc `` | (-1135.088, 40.195, -571.549) | cylinder, radius 2, length 3; quat (0,0.707,0,0.707) |
| 12 | `trig_stairwell<002>` | Trigger_Volume | `` sr3_city~fsh02`.czn_pc `` | (-1131.18, 36.203, -576.197) | box, half-extents (0.5, 1.75, 2); base at position, centre (-1131.18, 37.953, -576.197); quat (0,0.707,0,0.707) |
| 13 | `trig_stairwell<003>` | Trigger_Volume | `` sr3_city~fsh02`.czn_pc `` | (-1131.18, 32.199, -576.197) | box, half-extents (0.5, 1.75, 2); base at position, centre (-1131.18, 33.949, -576.197); quat (0,0.707,0,0.707) |
| 14 | `trig_stairwell<004>` | Trigger_Volume | `` sr3_city~fsh02`.czn_pc `` | (-1143.128, 32.195, -573.402) | cylinder, radius 2, length 4 |
| 15 | `trig_free_hos<001>` | Trigger_Volume | `` sr3_city~fsh02`.czn_pc `` | (-1142.969, 31.832, -559.619) | box, half-extents (2, 2, 7); base at position, centre (-1142.969, 33.832, -559.619) |
| 16 | `trig_jailcell01<001>` | Trigger_Volume | `` sr3_city~fsh02`.czn_pc `` | (-1139.003, 31.832, -538.602) | box, half-extents (2, 2, 1.875); base at position, centre (-1139.003, 33.832, -538.602) |
| 17 | `trig_jailcell01<002>` | Trigger_Volume | `` sr3_city~fsh02`.czn_pc `` | (-1159.05, 31.859, -534.729) | box, half-extents (2, 2, 2.125); base at position, centre (-1159.05, 33.859, -534.729) |
| 18 | `trig_free_hos<002>` | Trigger_Volume | `` sr3_city~fsh02`.czn_pc `` | (-1155.251, 31.906, -561.682) | box, half-extents (2, 2, 7); base at position, centre (-1155.251, 33.906, -561.682) |
| 19 | `trig_jailcell02<001>` | Trigger_Volume | `` sr3_city~fsh02`.czn_pc `` | (-1160.118, 32.195, -574.995) | cylinder, radius 2, length 4 |
| 20 | `trig_jailcell02<002>` | Trigger_Volume | `` sr3_city~fsh02`.czn_pc `` | (-1171.056, 32.008, -553.712) | cylinder, radius 2, length 4 |
| 21 | `trig_jailcell03<001>` | Trigger_Volume | `` sr3_city~fsh02`.czn_pc `` | (-1179.092, 32.195, -539.608) | cylinder, radius 2, length 4 |
| 22 | `trig_jailcell03<002>` | Trigger_Volume | `` sr3_city~fsh02`.czn_pc `` | (-1187.049, 31.768, -562.747) | box, half-extents (5.75, 2, 10.25); base at position, centre (-1187.049, 33.768, -562.747) |
| 23 | `trig_override<004>` | Trigger_Volume | `` sr3_city~fsh02`.czn_pc `` | (-1186.517, 32, -562.405) | cylinder, radius 1, length 1 |
| 24 | `grp_jailcell03_wave` | script_group | `` sr3_city~fsh02`.czn_pc `` | (0, 0, 0) | point (no volume) |
| 25 | `trig_garage` | Trigger_Volume | `` sr3_city~fsh02`.czn_pc `` | (-1200.27, 32.195, -534.67) | cylinder, radius 2, length 4 |
| 29 | `npc_garage_brute` | Script_Npc | `` sr3_city~fsh02`.czn_pc `` | (-1209.637, 31.23, -578.258) | point (no volume) |

#### sh03 (stall step 5)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 5 **(stall)** | `Tower_Trigger 001` | Trigger_Volume | `` sr3_city~fsh03`.czn_pc `` | (1191.74, 60.15, -1279.115) | box, half-extents (24, 8, 30); base at position, centre (1191.74, 68.15, -1279.115); quat (-0,-0.926,-0,0.377) |
| 5 **(stall)** | `Tower_Trigger2` | Trigger_Volume | `` sr3_city~fsh03`.czn_pc `` | (1585.929, 60.15, -1205.401) | box, half-extents (19.785, 8, 16.505); base at position, centre (1585.929, 68.15, -1205.401); quat (-0,-0.927,-0,0.375) |
| 5 **(stall)** | `Tower_Trigger3` | Trigger_Volume | `` sr3_city~fsh03`.czn_pc `` | (1371.3, 60.15, -1370.052) | cylinder, radius 21.76, length 16; quat (-0,-0.924,0,0.383) |
| 9 | `Helicopter_trigger` | Trigger_Volume | `` sr3_city~fsh03`.czn_pc `` | (1182.022, 59.937, -1129.69) | cylinder, radius 38.057, length 8.176; quat (-0,-0.997,-0,0.075) |
| 11 | `Rail_Heli` | script_vehicle | `` sr3_city~fsh03`.czn_pc `` | (678.683, 21.203, -822.599) | point (no volume) |
| 16 | `inside_trigger` | Trigger_Volume | `` sr3_city~fsh03`.czn_pc `` | (1349.317, 25.207, -1232.482) | cylinder, radius 52.458, length 8.77 |
| 17 | `LinkTrigger1` | Trigger_Volume | `` sr3_city~fsh03`.czn_pc `` | (1321.512, 10.215, -1236.505) | box, half-extents (0.75, 0.5, 0.75); base at position, centre (1321.512, 10.715, -1236.505); quat (-0,-0.676,-0,0.737) |
| 17 | `LinkTrigger2` | Trigger_Volume | `` sr3_city~fsh03`.czn_pc `` | (1350.732, 10.215, -1204.789) | box, half-extents (0.75, 0.5, 0.75); base at position, centre (1350.732, 10.715, -1204.789); quat (-0,-0.999,-0,0.044) |
| 17 | `LinkTrigger3` | Trigger_Volume | `` sr3_city~fsh03`.czn_pc `` | (1381.262, 10.215, -1234.719) | box, half-extents (0.75, 0.5, 0.75); base at position, centre (1381.262, 10.715, -1234.719); quat (0,0.707,0,0.707) |
| 17 | `LinkTrigger4` | Trigger_Volume | `` sr3_city~fsh03`.czn_pc `` | (1352.089, 10.215, -1265.551) | box, half-extents (0.75, 0.5, 0.75); base at position, centre (1352.089, 10.715, -1265.551); quat (-0.001,-0.999,0,0.047) |

#### sh04 (stall step 4)

| steps | name | class | zone | position (x, y, z) | shape and size |
|---|---|---|---|---|---|
| 5 | `sh04_manager` | Script_Npc | `` sr3_city~fsh04`.czn_pc `` | (1199.055, 23.789, 1455.443) | point (no volume) |
| 8 | `Mask Cutscene` | Trigger_Volume | `` sr3_city~fsh04`.czn_pc `` | (1236.423, 23.789, 1459.795) | box, half-extents (6.271, 2.285, 5.554); base at position, centre (1236.423, 26.074, 1459.795) |
| 12 | `SM Killbane Statue 1F SE` | Script_Mover | `` sr3_city~fsh04`.czn_pc `` | (1185.263, 23.839, 1402.629) | point (no volume) |
| 12 | `SM Killbane Statue 1F NW` | Script_Mover | `` sr3_city~fsh04`.czn_pc `` | (1229.092, 24.686, 1402.909) | point (no volume) |
| 12 | `SM Killbane Statue 1F SW` | Script_Mover | `` sr3_city~fsh04`.czn_pc `` | (1226.067, 24.588, 1452.623) | point (no volume) |
| 12 | `SM Killbane Statue 2F NE` | Script_Mover | `` sr3_city~fsh04`.czn_pc `` | (1181.499, 31.845, 1413.791) | point (no volume) |
| 12 | `SM Killbane Statue 2F NW` | Script_Mover | `` sr3_city~fsh04`.czn_pc `` | (1230.858, 31.853, 1409.121) | point (no volume) |
| 12 | `SM Killbane Statue 2F SE` | Script_Mover | `` sr3_city~fsh04`.czn_pc `` | (1179.522, 31.889, 1437.777) | point (no volume) |
| 13 | `Post Statue` | Script_Npc | `` sr3_city~fsh04`.czn_pc `` | (1212.854, 23.789, 1457.024) | point (no volume) |
| 13 | `Post Statue 001` | Script_Npc | `` sr3_city~fsh04`.czn_pc `` | (1207.157, 25.79, 1392.805) | point (no volume) |
| 13 | `Post Statue 002` | Script_Npc | `` sr3_city~fsh04`.czn_pc `` | (1191.463, 25.79, 1393.08) | point (no volume) |
| 13 | `Post Statue 003` | Script_Npc | `` sr3_city~fsh04`.czn_pc `` | (1213.158, 23.789, 1455.506) | point (no volume) |
| 14 | `brute02` | Script_Npc | `` sr3_city~fsh04`.czn_pc `` | (1190.297, 25.79, 1395.492) | point (no volume) |
| 14 | `brute03` | Script_Npc | `` sr3_city~fsh04`.czn_pc `` | (1210.028, 25.79, 1394.159) | point (no volume) |

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): marked the three "blocked by the parked mode-(a) limitation" statements resolved (`spec-vpp-container.md` §7); repointed the `.asm_pc` record-model references from the superseded `spec-asm-format.md` §4 to §7.3/§7.4; flagged the `spec-output.md` citation as unpublished.
- 2026-09-30 (adversarial desk review, `review/adv_mission-packages.md`): added review-status summary and per-unit status lines (8 units); named header offset `0x14C` (§1); clarified sibling-container vs manifest-record emptiness (§2); marked the `running_man_globals.xtbl` "first worked example"/31,474-byte cross-reference OPEN (§3.2); marked the `.cefct_pc` sub-object list a subset of `spec-effects-format.md` §5.4; qualified the 19-byte zero-entry tail to empty `source_name` and added a name-case note (§4); OPEN markers for the unre-measured archive census (§1), `source_name`/name-collision checks (§4) and the odd 1,033 entry count (§5).
- 2026-10-01 (cloud): `running_man_globals.xtbl` 31,474-byte size backed by the PC-era extraction listed in bridge job `…-kbhb`; the cross-reference wording stays OPEN.
- 2026-10-13: added "Per-mission objective routes (test vectors, 2026-10-13)": ordered blocking-wait routes for all 49 sweep missions (`tools/scratchpad/mission_routes.py`, TSVs in `tools/scratchpad/mission_routes/`), hand-checked against Team B sweep `mission_sweep_49_20261013_post7b3e110` (30 suspended missions, stall step marked). Batches 1-2 FINAL.
- 2026-10-13: added "Route target positions (zone data, 2026-10-13)": zone file, class, position and shape/size for every named route target (409 of 413 found; 30 stalled-mission tables), from `tools/scratchpad/zone_named_objects.py` + `route_target_positions.py`; positions cross-checked 104/104 against Team B sweep output; cylinder param1 = length (+0x90), param2 = radius (+0x94) re-read from the parser; dlc2_m02 approach-trigger route correction. FINAL.
