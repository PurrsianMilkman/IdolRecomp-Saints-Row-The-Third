# Saints Row: The Third — AI: Scope, Middleware Check, and the Named-Behavior Override Schema

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1, scoping pass (per `HANDOFF.md` §28.4/§28.5 item 4 — turning "AI/physics: statement of shape, not a finding" into a bounded result).
**Scope:** Determine whether AI decision-making is licensed third-party middleware or Volition's own code, and characterize whatever data-driven surface actually exists and is reachable without touching a parked archive or the parked zone-streaming subsystem.
**Method:** (1) The same raw-binary ASCII-string scan used in `spec-physics-format.md`, applied to AI-suggestive literals and every known AI-middleware signature of this game's era. (2) A survey of every top-level `.vpp_pc` archive's container flags (`spec-vpp-container.md` §1/§3.3), followed by `.xtbl` extraction from every archive **not** subject to the already-parked mode-a limitation (`da_tables.vpp_pc`, `misc_tables.vpp_pc` were not touched beyond what is already on record), using the existing `extract_xtbl.py` harness.
**Cleanroom compliance:** No decompiled code is reproduced verbatim; no original Volition identifiers are used as identifiers. `.xtbl` element/field names and literal data values quoted below (e.g. `Group_AI_Override`, `Brute Melee`) are ordinary shipped game data, the same convention this project already applies to every other `.xtbl` schema it has documented.

**Confidence key** (as in prior specs): **CONFIRMED — empirical**, **CONFIRMED — disassembly**, **HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**.

---

## 1. Headline result: no AI middleware — this is Volition's own system

**No licensed AI middleware of any kind is linked into `SaintsRowTheThird.exe`.** `CONFIRMED — empirical`, checked against every AI/behavior middleware plausible for a 2011 title and reported in full in `spec-physics-format.md` §2 (Havok AI, Havok Behavior, Kynapse, NaturalMotion Euphoria/morpheme/Endorphin, RecastNavigation/Detour — **zero occurrences of every one of these**, over the same 142,536-run full-binary string population used for the physics check). This is the decisive split the scoping pass was asked to produce: **physics is third-party (Havok, see `spec-physics-format.md`); AI is Volition's own code, in cleanroom scope.**

**What does exist in the executable's string table is a large population of lower-case, snake_case, `.xtbl`-field-shaped literals** — not middleware class names. Counted over the same 142,536-run population: `ai_` (103), `AI_` (23), `npc_` (131), `NPC_` (31), `cover` (71) / `Cover` (10), `alert` (53), `combat` (37) / `Combat` (8), `navmesh` (7), `pathfind` (11), `waypoint` (9), `squad` (6), `steering`/`Steering` (46 combined — mostly vehicle-camera/UI field names, not behavior). Representative in-context samples, read in full rather than through a truncated window: immediately after `human_ai_data`, a run of concatenated `ai_force_flags` + suffix strings — `ai_force_flagsalways_cower`, `ai_force_flagsalways_follow_in_alternate_vehicle`, `ai_force_flagsalways_sees_player`, `ai_force_flagsattack_non_player_enemies_on_sight`, `ai_force_flagsattack_pedestrians_on_sight`, `ai_force_flagsattack_player_on_sight`, `ai_force_flagscan_leave_behind`, `ai_force_flagscant_attack`, `ai_force_flagscant_cower`, `ai_force_flagscant_enter_vehicle`, `ai_force_flagscant_flee`, and more beyond that — clearly a `.xtbl`-style boolean-flags grid (category name `ai_force_flags` concatenated with each individual flag's own name, the same string-table shape this project has already seen elsewhere) sitting under a `human_ai_data` table. Also present: `combat_actions.xtbl` next to `Invalid CAE action`, `Combat_Tricks`/`combat_tricks.xtbl`, `Group_AI_Override`/`Group_AI_Overrides`/`Timeout_Seconds`. **These are `.xtbl` schema field names and `.xtbl` filenames, exactly like every other data-driven system this project has already characterized** (vehicle tuning, customization, mission/diversion tuning) — the same shape, not a new kind of evidence. **[CONFIRMED — empirical for the exact strings and their concatenated shape; HYPOTHESIS for "these all belong to one `human_ai_data` table" — plausible from proximity, not verified against an actual `.xtbl` schema since none of these specific tables were reached by this pass (§2).]**

## 2. What was reachable, and what was not

`.xtbl` extraction was attempted against all 36 top-level archives other than the two explicitly parked ones. Per-archive flags were read first (`spec-vpp-container.md` §1/§3.3: bit `0x2` clear = mode (a), independent per-entry streams; set = mode (b), one shared stream) — **not assumed**:

| Result | Archives | `.xtbl` found |
|---|---|---|
| `flags=0x0` (no compression, no shared stream), extracted cleanly | `characters`, `customize_item`, `customize_player`, `cutscene_sounds`, `cutscenes`, `decals`, `dlc1`, `dlc2`, `dlc3`, `effects`, `interface`, `interface_startup`, `items`, `patch_uncompressed`, `player_morph`, `preload_effects`, `preload_items`, `skybox`, `sounds`, `sounds_common`, `sr3_city_0`, `sr3_city_1`, `sr3_city_missions`, `vehicles`, `voices` (25 archives) | `cutscenes` 150, `dlc1` 68, `dlc2` 56, `dlc3` 41, `interface` 1, `sr3_city_0` 91, `sr3_city_missions` 1; all others 0 |
| `flags` bit `0x1`/`0x2` set (mode-a compressed or mode-b shared-stream), but every entry present extracted cleanly anyway | `startup` (`flags=0x4801`, mode-a; 2 entries, both genuinely zlib-compressed per their own `+0x10` field — not the raw sentinel — and both decompressed correctly, 0 errors); `soundboot` (`flags=0x4803`, mode-b shared-stream; 930 entries, 0 errors); `high_mips` (`flags=0x4801`; the already-known empty 2,048-byte stub, 0 entries, trivially complete) | 0 |
| Mode-a (`flags` bit `0x2` clear, `0x4801`) or mode-b shared-stream, **extraction failed for effectively every entry** with the existing harness | `cutscene_tables` (117/118 entries failed), `misc.vpp_pc` (219/220), `patch_compressed` (124/126), `preload_anim` (4209/4209 — a mode-b/shared-stream archive already fully characterized by `spec-anim-format.md`, containing no `.xtbl` regardless), `preload_rigs` (374/374, same shape as `preload_anim`), `shaders` (1585/1693), `sound_turbo` (528/550), `vehicles_preload` (110/111) | 0 confirmed (extraction did not complete) |
| Explicitly **not attempted**, per the parked boundary | `da_tables.vpp_pc`, `misc_tables.vpp_pc` | — |

**The `startup`/`soundboot` row matters methodologically, not just for completeness**: it proves the harness's per-entry zlib decompression path, and its whole-payload shared-stream decompression path, both work correctly on genuinely-compressed and genuinely-shared-stream data (`soundboot.vpp_pc` is a real, 4.1 MB, 930-entry mode-b archive, decoded without a single error) — so the failures in the row below are **not** simply "mode-a/mode-b archives never work with this harness." Something else distinguishes the 8 failing archives from `startup`/`soundboot` — larger entry counts and/or larger individual entries is the obvious candidate, not verified this pass.

**The middle row is flagged honestly as OPEN, not chased.** These are **not** the two archives named in the parked mode-a lead (`HANDOFF.md` §28.3) — that prohibition covers `da_tables.vpp_pc`/`misc_tables.vpp_pc` specifically, and this pass respected it by not touching either. The extraction failures on `cutscene_tables`/`misc.vpp_pc`/`patch_compressed`/`shaders`/`sound_turbo`/`vehicles_preload` are a **different, undiagnosed** gap — possibly a new instance of the same class of limitation, possibly an unrelated bug in the existing `extract_xtbl.py` harness's per-entry decompression path when the archive-level flags carry bit `0x1` set. **Next step, if this is picked up again:** diagnose one small case (e.g. `cutscene_tables.vpp_pc`, 118 entries) by comparing the harness's per-entry decompression against a direct `zlib.decompressobj()` call on entry 0 specifically, the same isolation technique `spec-vpp-container.md`'s own methodology notes recommend — **not attempted this pass**, kept out of scope deliberately rather than risking drift toward the parked mode-a reopening lead.

**`misc.vpp_pc`, `cutscene_tables.vpp_pc`, and `shaders.vpp_pc` are the most likely homes for the master `AI\ai_behavior.xtbl` / `AI\ai_personalities.xtbl` tables referenced in §3 below** (by elimination: they were not found in any of the 28 successfully-extracted archives), but this is **HYPOTHESIS — unconfirmed**, not established.

## 3. The confirmed data-driven mechanism: named Behavior/Personality overrides per spawn group

Four real, shipped `.xtbl` files — each a per-mission-area "modal" encounter definition nested inside `sr3_city_0.vpp_pc` (a fully raw, unambiguously reliable archive) — contain a real, fully-schema'd AI-override mechanism:

- `angels_area_modal.str2_pc/angels_area.xtbl`
- `daedelus_area_modal.str2_pc/daedelus_area.xtbl`
- `steel_mill_area_modal.str2_pc/steel_mill_area.xtbl`
- `zombie_area_modal.str2_pc/zombie_area.xtbl`

(All four are horde/wave-defense diversion areas — the same `Wave_Difficulty_Preset`/`Enemy_Groups_Preset` shape as the already-documented diversion-minigame tuning in `da_tables.vpp_pc`, but reached here through a completely different, unblocked archive.)

### 3.1 Schema, quoted from the file's own embedded `TableDescription`

**[CONFIRMED — empirical, the schema definition is embedded literally in the `.xtbl` file itself, the standard convention this project has already documented for every other `.xtbl` container.]**

```
Group_AI_Overrides            (Type: Grid: Description: "Overrides on AI behavior for
                                specific groups. This is required for riot shield NPCs.")
  Group_AI_Override           (Type: Element)
    Group_Name                (Type: String, Unique — "Name of the group with an AI override.")
    Overrides                 (Type: Element)
      Personality              (Type: Reference, Required: false)
        -> File: AI\ai_personalities.xtbl, Type: Personality.Name
      Behavior                 (Type: Reference, Required: false)
        -> File: AI\ai_behavior.xtbl, Type: Behavior.Name
```

**This is a real, designer-facing mechanism, and the file states its own purpose**: a per-area table lets a mission/encounter override a *named enemy spawn group*'s AI behavior and/or personality profile by string reference into two master tables. The document's own comment — "required for riot shield NPCs" — is a genuine piece of design intent, not an inference.

### 3.2 Real content, 4 files, 21 records, 5 distinct named behaviors

**What was counted:** every `<Group_AI_Override>` element across the 4 files above — the complete population reachable from this pass, not a sample. **A failing case for "this is a real, exercised mechanism" would have looked like:** zero populated records (an unused schema stub), or every record using the same single behavior name (no real variation). Neither is what was found:

| File | Records | Behavior values used |
|---|---:|---|
| `angels_area.xtbl` | 11 | `Default Unarmed` ×8, `Default Sniper` ×3 |
| `daedelus_area.xtbl` | 1 | `Default Unarmed` ×1 |
| `steel_mill_area.xtbl` | 6 | `Brute Melee` ×2, `Brute Minigun` ×2, `Brute Flamer` ×2 |
| `zombie_area.xtbl` | 3 | `Brute Melee` ×2, `Default Unarmed` ×1 |
| **Total** | **21** | **5 distinct: `Default Unarmed` (10), `Brute Melee` (4), `Default Sniper` (3), `Brute Minigun` (2), `Brute Flamer` (2)** |

`Group_Name` values are spawn-group identifiers already meaningful from context (`EG_MeleeBrute_01`, `EG_SNIPERHO_2`, `EG_GIMP_5`, …, where `EG_` plausibly abbreviates "Enemy Group" — consistent with the sibling `Enemy_Groups_Preset`/`Enemy_Group` elements documented in the same files' wave-difficulty tables). **`Personality` is never populated in any of the 21 real records** — the field exists in the schema and is marked optional, but this population gives no evidence either way about how it is used. **[CONFIRMED — empirical for all figures in this table, the complete reachable population, not a sample.]**

### 3.3 What this does and does not tell us about AI "logic"

**This is a data-driven *selector*, not a data-driven *behavior*.** The `.xtbl` mechanism lets designers name which pre-existing behavior profile a spawn group uses (`"Brute Melee"`, `"Default Sniper"`, …); it says nothing about what a profile named `"Brute Melee"` actually *does* — that logic is not in any of the 4 files, and the master table that would define it (`AI\ai_behavior.xtbl`) was not reached by this pass (§2). This is consistent with, and gives concrete data-side confirmation of, the shape `HANDOFF.md` §28.4 predicted before this pass ran: **"AI decision logic and physics response are hardcoded engine algorithms with no backing data file, the same shape as the anim keyframe payload and the morph value decode before those were opened."** The `.xtbl` layer here is real and now `CONFIRMED`, but it is a thin naming/selection layer over logic that is very likely still hardcoded engine code — exactly the shape that made `.anim_pc`'s keyframe payload and `.cmorph_pc`'s dequantization formula need a disassembly-driven pass rather than a container hunt.

**Concrete next step, if this thread is picked up again:** find the runtime consumer of a `Behavior`-name string (i.e. whatever code resolves the literal `"Brute Melee"` to actual decision logic) by tracing from the string literal itself or from wherever spawn-group definitions are constructed at mission-load time. This is a clean, disassembly-driven, `.czn_pc`-independent next step — it does not require reading any zone-interior data or the zone-streaming runtime, and no thread in this pass approached that boundary.

## 4. Explicit statement on the hard boundary

This pass never touched `.czn_pc`'s interior or the zone-streaming runtime subsystem. The `Group_AI_Override` mechanism documented above is a per-mission-area `.xtbl` table, structurally unrelated to zone object-placement streams — it was reached through `sr3_city_0.vpp_pc`'s ordinary nested `str2_pc` container structure, the same route `spec-vpp-container.md` already documents for every other nested-archive lookup in this project, not through any zone-specific mechanism. No thread here leads toward "how do NPCs know which zone/streaming category they're in," and the parked item (`HANDOFF.md` §27.3) was not approached.

## 5. Summary of findings, by confidence

- **CONFIRMED — empirical:** no AI middleware (Havok AI, Havok Behavior, Kynapse, NaturalMotion, Recast/Detour) is linked into the executable; the AI-suggestive literal strings in the binary are `.xtbl`-field-shaped data, not middleware signatures; a real, schema'd `Group_AI_Override` mechanism exists, reaching 21 records across 4 shipped files with 5 distinct named `Behavior` values and a never-populated `Personality` field.
- **HIGH CONFIDENCE — inferred:** `misc.vpp_pc`/`cutscene_tables.vpp_pc`/`shaders.vpp_pc` are plausible (not confirmed) homes for the unreached `AI\ai_behavior.xtbl`/`AI\ai_personalities.xtbl` master tables; the actual behavior/personality *logic* behind each named profile is hardcoded Volition engine code rather than further `.xtbl` data, matching the pre-pass prediction in `HANDOFF.md` §28.4.
- **OPEN / UNKNOWN:** the location and content of `AI\ai_behavior.xtbl`/`AI\ai_personalities.xtbl`; why `.xtbl` extraction fails for effectively every entry in `cutscene_tables.vpp_pc`, `misc.vpp_pc`, `patch_compressed.vpp_pc`, `shaders.vpp_pc`, `sound_turbo.vpp_pc`, `vehicles_preload.vpp_pc` (possibly a new instance of a known limitation class, possibly an unrelated harness gap — not distinguished this pass); the runtime consumer that resolves a `Behavior`-name string to actual decision logic; the meaning of the still-unreached literal strings `human_ai_data`, `ai_force_flags` (with its many boolean-flag suffixes), `combat_actions.xtbl`, `combat_tricks.xtbl`.

## 6. Methodology note for `HANDOFF.md` §5

**A clean split between "no middleware found" and "here is the Volition mechanism instead" is itself a complete, well-scoped result — it does not need to be forced deeper than the reachable data supports.** This pass stopped at a real, `CONFIRMED`, data-driven *selector* mechanism and explicitly declined to chase the master table it selects from, because reaching it would have meant either (a) extraction attempts against archives with an undiagnosed, possibly-parked-adjacent failure mode, or (b) speculative reconstruction from the executable's string table alone. Recording the boundary precisely (§2's per-archive result table, §3.3's "selector, not behavior" framing) is more useful to whoever picks this up next than a single further guess would have been.
