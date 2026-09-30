# Saints Row: The Third — Per-Mission/Activity Package Contents Specification

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1, Target #12 (from `spec-output.md` §4) — the last item from the original Phase 0 target list.
**Scope:** The individually-named `.str2_pc` packages inside `sr3_city_missions.vpp_pc` (Trafficking/Heli/Escort-style activity packages) and similarly-organized archives — what's actually inside them, and how reliably it can be extracted given everything already learned about the container format.
**Method:** Black-box extraction using the already-confirmed container format (`spec-vpp-container.md`), cross-checked against the corrected `.asm_pc` manifest model (`spec-asm-format.md`, corrected as part of this same pass) and the `.xtbl`/geometry work already done. No new disassembly this pass.
**Cleanroom compliance:** No decompiled code or internal identifiers appear below. Real file/activity names quoted below (e.g. `Trafficking`, `Horde Mode`) are ordinary shipped data (container directory entries and manifest strings), not code.

**Confidence key** (as in prior specs): **CONFIRMED — empirical**, **HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**.

---

## 1. Overview and reliability — good news, for once

`sr3_city_missions.vpp_pc` is a **fully raw/uncompressed top-level archive** (flags `0x0`) — every one of its 16 entries is directly, unconditionally extractable, with none of the mode-(a) reliability concerns that have limited other targets in this project. **[CONFIRMED — empirical.]** Its 15 named `.str2_pc` entries are per-activity packages (the "Trafficking/Heli/Escort-style" packages this target names); the 16th is `sr3_city_missions.asm_pc`, this archive's manifest (§4).

Better still: **every one of those 15 nested per-activity containers is a mode-(b) shared-stream container** (flags `0x4803`), which — per `spec-vpp-container.md` §3.6 — means **all of their entries decode reliably, not just the first one**. So unlike the vehicle-stats, customization, and geometry passes (which all ran into the parked mode-(a) decode-failure limitation blocking most of their content), **this target has no such blocker: everything described below was read directly and reliably.** **[CONFIRMED — empirical, all 15 nested containers checked.]**

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

**13 of the 15 named activity packages are genuinely, verifiably empty** — real, valid mode-(b) containers with an `entry_count` of exactly `0`. **[CONFIRMED — empirical, all 13 checked directly.]** This was not expected going in, and is worth stating plainly: **most of what this archive "reserves a slot for" by name has no archive-specific content at all.** Only `Horde Mode` and `Running Man` carry any package-specific assets.

**What this means:** the bulk of a side activity's actual behavior (spawn logic, scoring, UI, objective text) does **not** live in this archive at all — it lives in the systems already documented elsewhere in this project: the named Lua/UI event hooks (`spec-lua-bindings.md` §4, which already includes real hook names for several of these exact activities — `button_mashing_minigame`, `sr2_balance_meter`, `mayhem_local_player_world_cash`, `whored_countdown_timer_update`), and the shared `.xtbl` tables in `misc_tables.vpp_pc` (`spec-xtbl-format.md`, `spec-vehicle-data.md` — many activity-adjacent tables like `distant_vehicle_spawn_parameters.xtbl` live there, though most are currently blocked by the parked container limitation since that archive is mode-(a)). **This archive's actual role is narrower than its name list suggests: it's a place for an activity to ship a small number of *package-specific* assets it can't get from those shared systems — most don't need any.** **[HIGH CONFIDENCE — inferred, from the consistent emptiness pattern plus the direct thematic overlap with already-confirmed hook names.]**

## 3. The two non-empty packages

### 3.1 `Horde Mode`

Two entries, both particle/cinematic-effect files: `vfx_shockwave_kill_all.cefct_pc` (12,480 bytes) and `vfx_whored_invulnerable.cefct_pc` (5,264 bytes) — named exactly as their gameplay purpose suggests (a kill-all shockwave effect, an invulnerability effect for the "Whored" horde-mode activity). **[CONFIRMED — empirical.]**

### 3.2 `Running Man`

Two entries: `running_man_globals.xtbl` (31,474 bytes — already fully documented as this project's very first `.xtbl` worked example, `spec-xtbl-format.md`) and `vfx_runningman_fireworks.cefct_pc` (14,416 bytes — a fireworks particle effect, matching the "Running Man" activity's finale). **[CONFIRMED — empirical.]**

### `.cefct_pc` — light characterization only

Not a target of its own this pass, but worth noting: every `.cefct_pc` sample opened here **starts with the exact same shared "material reference block" header already found in `.matlib_pc` and `.ccmesh_pc`** (`spec-geometry-format.md` §3.1) — same constant, same name-table-length field, same texture-slot-count-then-names structure. `vfx_shockwave_kill_all.cefct_pc`'s block declares 3 texture references: `vfx_corona_white.tga`, `vfx_smoke_anim_v01.tga`, `vfx_shockwave_01.tga`. **[CONFIRMED — empirical, and folded back into `spec-geometry-format.md` as a third confirmation of that shared block.]** The rest of a `.cefct_pc` file (presumably the actual particle-emitter/timeline parameters) was **not investigated** at the time — since resolved with a light, dedicated follow-on pass: see `spec-effects-format.md` for a fixed sub-header marker, the effect's self-referencing `.effectx` source filename, and its named sub-object list (confirmed, e.g., `FX_BlastWave01`, `Object01`, `VFX Filter01` for these same two samples) — the per-object parameter/timeline data itself remains open there too.

## 4. The `.asm_pc` manifest — corrected model, confirmed against this archive

`sr3_city_missions.asm_pc` was decoded using (and this investigation directly motivated a correction to) `spec-asm-format.md`. The archive-specific table's record for each of the 15 packages was checked byte-for-byte:

- **All 13 empty packages produce the exact zero-entry record shape** now documented in `spec-asm-format.md` §4 (a fixed 19-byte tail, `entry_count = 0`, no per-entry sub-blocks) — this is the same shape the implementation team had already flagged from this same archive's `Trafficking` entry specifically; checking all 13 confirms it's the general zero-entry case, not something specific to `Trafficking`.
- **`Horde Mode`'s and `Running Man`'s records each produce exactly 2 per-entry sub-blocks**, matching their real sibling entry counts, and — this is what actually drove the `spec-asm-format.md` correction — **each sub-block's own name and uncompressed-size field match that specific sibling entry exactly** (e.g. `Horde Mode`'s record correctly names both `vfx_shockwave_kill_all.cefct_pc` and `vfx_whored_invulnerable.cefct_pc`, with their real uncompressed sizes, 12,480 and 5,264). The records' one aggregate size field was confirmed to match each sibling container's own header field `0x168` (its total compressed payload size) exactly — `2,459` for `Horde Mode`, `6,025` for `Running Man`. **[CONFIRMED — empirical, both samples, cross-checked against independently-established container-format ground truth.]**

See `spec-asm-format.md` §4 for the full corrected record model; this section exists to record that the correction was driven by, and is fully consistent with, this archive.

## 5. Related archives checked and distinguished

To address the "similar mission archives" part of this target's scope, the following were checked and found to be **structurally different, serving different purposes** — not further documented here:

- **`sr3_city_0.vpp_pc` / `sr3_city_1.vpp_pc`** — large (1,033 and 1,516 entries) fully-raw archives holding **open-world city geometry, organized by map grid tile** (entries named by coordinate, e.g. `1024.str2_pc`/`1024.asm_pc`), not by mission or activity. A world-streaming system, not a mission-package one. **[CONFIRMED — empirical, name pattern and entry count checked.]**
- **`cutscenes.vpp_pc` / `cutscene_sounds.vpp_pc`** — cinematic-camera/animation data and cutscene audio banks, organized by cutscene-shot name (e.g. `16_out.str2_pc`, `ct_kinzie_01.bnk_pc`) rather than by mission/activity name. Mission-adjacent (cutscenes obviously belong to specific missions) but a genuinely separate archive family with its own naming convention — not investigated further here, and already partially covered by `spec-fxo-format.md`'s and `spec-vpp-container.md`'s use of this same archive family for other worked examples.
- **DLC archives (`dlc1/2/3.vpp_pc`)** — checked for an equivalent "named activity package" archive and found none; DLC mission-specific content instead appears as **flat, directly-named `.xtbl` files** at the archive's top level (e.g. `dlc1_rm_01.xtbl`, `dlc1_a_bm_nw_01.xtbl` — already noted in passing in `spec-vehicle-data.md`), not as nested named `.str2_pc` packages. **This means the "named activity package" pattern documented in this spec is specific to `sr3_city_missions.vpp_pc` and does not generalize to the DLC content — flagged as a real structural difference, not an oversight.** **[HIGH CONFIDENCE — inferred from a direct check of all three DLC archives' top-level entries; the DLC mission `.xtbl` files themselves were not individually opened/characterized this pass.]**

## 6. Open Items

1. **The full internal structure of `.cefct_pc`** beyond its shared material-reference-block header (§3) — a light follow-on pass (`spec-effects-format.md`) has since documented a fixed sub-header marker, the effect's source filename, and its named sub-object list; the actual per-object particle/effect timeline data is still open there.
2. **The DLC mission `.xtbl` files** (`dlc1_rm_01.xtbl` and siblings, §5) were noted by name but not opened or characterized — a real, likely-accessible (DLC archives are raw) follow-on if mission-scripting content becomes a priority.
3. **Why most activities have no package-specific content** (§2) is inferred from the pattern and from thematic overlap with already-confirmed Lua hooks and shared tables, not directly traced through the loading code that decides what (if anything) a given activity package should contain.
4. **`sr3_city_0.vpp_pc`/`sr3_city_1.vpp_pc`'s grid-tile world-streaming format** (§5) is a large, distinct, currently-undocumented system, noted here only to rule it out as "similar" to this target — it would be a substantial target of its own if picked up later. **Update, 2026-09-29: this system is confirmed to also carry mission Lua scripts** — `spec-lua-bindings.md` §14.5 found 79 real "Mission LUA script" resource entries (type 32) across this same manifest family's `stream_grid.asm_pc` files, each a `<mission-code>.lua` inside a `<mission-code>_modal` container (e.g. `m24_modal`→`m24.lua`), confirming the real trigger for mission-script loading is this world-streaming grid, not a separate mechanism. Still not opened as a format in its own right — this is a citation, not new characterization of the grid format itself.

None of these gaps affect the core deliverable: every package in `sr3_city_missions.vpp_pc` — empty or not — can be reliably enumerated and, where content exists, fully and correctly extracted, with no dependency on the still-blocked mode-(a) container limitation that has constrained several other targets in this project.

---

This closes out all 12 original Phase 0 targets (`spec-output.md` §4).
