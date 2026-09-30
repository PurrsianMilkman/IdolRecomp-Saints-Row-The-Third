# Saints Row: The Third — Audio, Radio and Foley Data Tables: XML Schemas Recovered from the Loaders

**Prepared by:** SPEC TEAM, agent AO
**Phase:** Schema-from-loader campaign (`HANDOFF.md` §31, archived §27.2) — group "audio, radio and foley"
**Scope:** For each `.xtbl` gameplay table of the audio/radio/foley group whose literal filename appears in the executable: the loader, the element tree its reader accepts, each element's type and destination offset in the runtime record, defaults and required-vs-optional behaviour, unit conversions, name-hash keys, cross-table references and fixed capacities — then, where the base-game container is readable, validation against the real shipped rows.
**Method:** Exact filename literals were located by a raw case-insensitive scan of the executable (`tools/harnesses/tbl_exe_lit.py`) and followed by string cross-reference in Ghidra 12.1.3 (disposable project copy `tools/gp_ao1`) to each table's loader; the per-row reader and its callees were decompiled (`tools/scripts/AhDecMk.java`, reusing agent AH's generic "disassemble+decompile" harness) and read to the end. Literal addresses referenced only as data (element names, enum strings) were resolved with a small purpose-built script, `tools/scripts/AoStrAt.java` (prints the ASCII string located *at* a given address, the reverse of `AfStrXrefs.java`). Base-game table content is now readable (`spec-vpp-container.md` §7); this pass additionally extracted all 16 assigned tables from `misc_tables.vpp_pc` via the standard mode-(a) reader (`tools/harnesses/vpp_modea.py`) and validated the loader-derived schema against the real rows with a tolerant, regex-based reader (never a strict XML parser, per `spec-xtbl-format.md` §7's tolerance requirement) — `tools/harnesses/ao_validate.py`-equivalent logic, run from the session scratchpad and reported per table below. No whole-binary predicate search was used; negative claims ("only N of M rows are reachable") rest on a complete read of the loader body plus an exhaustive regex count against the real extracted file, not a spot check.
**Cleanroom compliance:** No decompiled code is reproduced and no original internal identifiers are used. XML table/element names, enum literals and flag literals are *data* and are listed freely; offsets, sizes, strides, constants and function addresses are evidence anchors. Function addresses use the form `FUN_00XXXXXX` for the image address only.
**Confidence key:** CONFIRMED — disassembly (read directly from the reader's code) / CONFIRMED — empirical (checked against real shipped rows extracted this pass) / HIGH CONFIDENCE — inferred / HYPOTHESIS — unconfirmed / OPEN / UNKNOWN.

---

## 1. Overview, method, and the shared reader grammar

### 1.1 Scope and boundary

All 16 tables assigned to this group (`audio_banks.xtbl`, `audio_constants.xtbl`, `audio_line_tags.xtbl`, `audio_personas.xtbl`, `audio_settings.xtbl`, `foley_collision.xtbl`, `foley_engine.xtbl`, `foley_touch.xtbl`, `radio_activities.xtbl`, `radio_events.xtbl`, `radio_stations.xtbl`, `persona_radio_prefs.xtbl`, `playlist_artist_track.xtbl`, `voc_sb_line_sit.xtbl`, `commercial_events.xtbl`, `commercials.xtbl`) have their exact filename literal in the executable, **each with exactly one exact NUL-delimited hit** (`tbl_exe_lit.py`, run over the full image) — none had to be skipped. Every one of the 16 was reached from its loader and specced below; none was left untouched. All 16 were additionally extracted whole from `misc_tables.vpp_pc` (entry indices given per table in §19) and validated. **[CONFIRMED — disassembly + empirical for every table in this group.]**

This is a genuinely rich pass: several of these tables turn out to be the *direct, disassembly-confirmed data source* for open items in two already-published specs — `spec-audio-format.md`'s bank load/unload state machine (§8's "what sets the trigger flags" question) and `spec-save-format.md` §12.8's radio/mixtape/commercial save fields. §18 collects every such link; do not re-derive those other documents' own claims here, only cite them.

### 1.2 The XML node model and shared accessor family — reused, not re-derived

Every table in this group is an ordinary `<root><Table><Row>…` document (`spec-xtbl-format.md` §2–§3) read by the engine's generic in-memory `.xtbl` parser and its shared node-walking/scalar-accessor family. **This family was already fully documented by agent AH in `spec-tables-weapons-combat.md` §1.2–§1.4 and is reused here by citation, not re-derived**: the node layout (`+0x00` name, `+0x04` next sibling, `+0x08` first child, `+0x0C` text, all comparisons case-insensitive `_stricmp`), the document opener `FUN_00DAC9A0`/in-memory twin `FUN_00DACA90`, child/sibling/count navigation (`FUN_00DC4FF0`/`FUN_00DC5030`/`FUN_00DC5150`), the "always" vs "if present" scalar reader pairs (`s32` `FUN_00DABC70`/`FUN_00DABD20`, `u32` `FUN_00DABDF0`/`FUN_00DABE80`, `f32` `FUN_00DACCB0`/`FUN_00DACD40`, `bool` `FUN_00DAC480`/`FUN_00DAC510`, `s8` `FUN_00DAC300`/`FUN_00DAC3B0`), the float/integer text grammars, and the engine's own table-driven CRC-32 name hash (`FUN_00D9E8B0`/`FUN_00D9E740`, reflected, table `0x01320DA0`, input lower-cased, no final XOR, seed passed per call site). Every table below is read against this same family; per-table sections only note *which* members are used and where the results land. **[CONFIRMED — disassembly, citing the already-published trace.]**

Every reader in this group also calls **`FUN_00462960`**, already identified in `spec-tables-weapons-combat.md` §1.4 as the path by which "sound-event names go to the audio middleware's own string-to-id function" — this pass confirms `FUN_00462960` is a thin pass-through to `FUN_0046FD00` (no argument transformation visible; a register-forwarding wrapper) and is the **audio middleware (Wwise/AK) string→id hash**, distinct from the engine's own CRC-32. This distinction matters throughout this document: several tables hash the *same* row's `Name` with *different* functions depending on which table it is (§1.3, §9, §11).

### 1.3 New hashing/parsing quirks found this pass (not in the already-published family)

Three small, table-specific pieces of code were found this pass that are **not** members of the shared family above and are worth flagging once, here, since they recur:

1. **A bespoke, non-shared integer-prefix parser for `audio_banks.xtbl`'s `wwise_id` (`FUN_00464730`)** — not the shared `s32`/`u32` reader. Grammar: an optional leading `-` aborts to 0 immediately (rejects negatives outright, does not return a negative value); otherwise it accumulates ASCII digits `0`–`9` and stops at the first non-digit; the value is kept only if the character that stopped the scan is `\0` or `.` (any other trailing character — e.g. a letter — makes the whole parse return 0). **[CONFIRMED — disassembly, full body read; CONFIRMED — empirical: all 496 real `wwise_id` values in the shipped file match this grammar exactly, §19.]**
2. **A second CRC-32 entry point, `FUN_00D9E7E0`, sharing the documented hash's table (`0x01320DA0`) but *not* lower-casing its input** — used by `foley_touch.xtbl`'s row-name hash (§9). This contradicts the "input lower-cased" clause `spec-tables-weapons-combat.md` §1.4 states for the nearby `FUN_00D9E8B0`/`FUN_00D9E740` entry points; the full body of `FUN_00D9E7E0` was read and contains no case-folding step anywhere. Whether the lower-casing happens in a wrapper this particular call bypasses, or `FUN_00D9E7E0` is a genuinely separate case-sensitive entry point into the same table, was not resolved further. **[CONFIRMED — disassembly that this specific call path is case-sensitive; OPEN why it differs from its documented siblings.]**
3. **Several inline, fully-unrolled `strcmp`-shaped byte loops in `audio_banks.xtbl`'s loader** (checking a row's `Name` against `Init`, and `streaming`/`load_at_boot`/`ram_bank_pc`/`cacheable_pc` against `True`/`False`) that are **case-sensitive** — verified directly in raw disassembly (a `CMP`/`JNZ` byte loop with no `OR 0x20`-style case-fold anywhere), unlike the shared element-*name* lookup's documented `_stricmp`. In the real shipped file this has zero practical effect (every `streaming`/`load_at_boot` value present is spelled exactly `True` or `False`, §19), but a mod authoring `false` (lower-case) would silently fail to clear the flag. **[CONFIRMED — disassembly for the case-sensitivity; CONFIRMED — empirical that it doesn't currently matter.]**

---

## 2. `audio_banks.xtbl` — the Wwise bank registry, and the confirmed data source for `spec-audio-format.md`'s open trigger question

**Priority table.** This is the direct, disassembly-confirmed schema for the `0x5c`-byte-stride bank record `spec-audio-format.md` §7.4–§7.5 already found and characterised from the consumer side (the Volition-authored bank load/unload state machine and its Low-Level I/O hook). That document left open "what sets the load/unload state machine's own trigger flags" (§8, its own words: "a data-driven table... naming which banks belong to which mission, zone, or trigger" was one of three untested candidates). **This table is that data source, at least for the boot-time/always-loaded half of the answer** — see §18.1 for the precise hand-off.

### 2.1 Loaders and filename

Two separate functions reference the `audio_banks.xtbl` literal (`0x0129ee4c`, 3 code xrefs: `FUN_00561c00`, and twice inside `FUN_00464a70`):

- **`FUN_00464a70(char dlc_index)`** is the real per-row bank *loader*. `dlc_index > 0` builds `dlc%d_audio_banks.xtbl`; otherwise it opens plain `audio_banks.xtbl` (guarded by an existence check, `FUN_00DA90D0`). It walks `<NewEntity>` rows directly under `<Table>` and, per row, allocates/fills one `0x5c`-byte bank record via `FUN_004648C0` (§2.3). **[CONFIRMED — disassembly.]**
- **`FUN_00561C00`** is a *separate*, smaller pass over the *same* file: for every `<NewEntity>` row whose `Name` starts with `wep_` (case-insensitive `strnicmp`, 4 chars), it hashes the name through the Wwise string-to-id function (`FUN_00462960`) and registers the id into a small fixed hash/refcount structure via `FUN_004641F0` (three-pass find-or-insert-or-evict over a table reached through an implicit register argument, capacity not resolved). **HIGH CONFIDENCE** this is a dedicated **weapon-bank preload cache**, parallel to the weapon-effect preload bookkeeping `spec-tables-weapons-combat.md` §2 already documents for `weapons.xtbl`'s own `FUN_00AD0180` pass — nothing is stored back into the bank record itself. **[CONFIRMED — disassembly for the mechanism; HIGH CONFIDENCE for its purpose.]**

### 2.2 Required elements — and a hard-failure quirk on a missing one

Both `Name` and `wwise_id` are read with the "always get child" accessor and, if either is `NULL` (element absent), the code jumps to the function's own epilogue — **abandoning every remaining `<NewEntity>` row in the file, not just the current one.** This is the same severity class as the already-documented "an unrecognised `Vehicle_Type`/`Surface.Type` fails the whole vehicle/surface load" quirks (`spec-vehicle-data.md` §7.5). **In the real shipped file this never triggers** — all 496 rows carry both elements (§19) — but it is a real, load-bearing fact for anyone hand-editing or modding this table. **[CONFIRMED — disassembly; CONFIRMED — empirical that it doesn't fire on retail data.]**

### 2.3 Per-row fields and the `0x5c`-byte record

| Element | Type / reader | Destination | Notes |
|---|---|---|---|
| `Name` | text, required (§2.2) | record `+0x00`, bounded copy (`FUN_00DA7930`, max `0x40`); `"Unknown"` if literally absent (dead in practice) | Row identity. Compared case-sensitively (§1.3 item 3) against the literal `Init` — an exact, unique hard-coded special case. |
| *(Name == `Init`)* | — | sets a "boot-load" flag and, later, bit 0 of `+0x5b` | Exactly **one** row in the shipped file is named `Init` (§19); this is the bank the engine always wants resident. |
| `wwise_id` | text, bespoke digit-prefix parser `FUN_00464730` (§1.3 item 1), **not** the shared `s32`/`u32` family | `+0x40` | The numeric Wwise bank id. This is the field `spec-audio-format.md` §7.5 already found being read from "the record's own `+0x40`" by the bank state machine and passed to the numeric-id `LoadBank`/`UnloadBank` overloads — **this pass closes that link**: `+0x40` is populated directly from this element's text. **[CONFIRMED — disassembly, both sides now traced.]** |
| `streaming` | text, case-sensitive compare against `False` (§1.3 item 3) | `+0x44` (byte, "has media/streaming companion"; default 1/true) | If not exactly `False`, the loader resolves and opens a `<name>` + optional `_<platform>` + `_media.bnk_pc` companion filename (built from literals `_` and a platform-token global) via a generic named-resource lookup (`FUN_00DAB0C0`), storing the resulting handle at `+0x48`. |
| `load_at_boot` | text, case-sensitive compare against `True` (base-game rows only — the DLC branch skips this element entirely) | contributes to the same "boot-load" flag as `Name==Init` | Only checked for `dlc_index<=0` (base-game) rows; a DLC-framework bank cannot set this itself in this reader. |
| `ram_bank_pc` | text, case-sensitive compare against `True` | `+0x5b` bit 3 | |
| `cacheable_pc` | text, case-sensitive compare against `True` | `+0x5b` bit 2 | |
| `ram_size_pc` | `u32`, "if present" (`FUN_00DABE80`), default 0 | not stored in the record itself | Only used as a trigger: if non-zero **and** the row is boot-load, a dedicated Wwise memory pool is created via `AK::MemoryMgr::CreatePool`/`SetPoolName` (`FUN_00470160`), sized `ram_size_pc + 0x2000` bytes and named after the bank — a genuinely new, disassembly-confirmed extension of `spec-audio-format.md` §6's statically-linked Wwise SDK surface (that document had not traced any `CreatePool` call site). **[CONFIRMED — disassembly.]** |
| `voice` | text, case-insensitive `_stricmp` against `True` (the one boolean-like check in this loader that *is* case-insensitive, via a genuine library call rather than an inlined loop) | a transient local only — not stored in the record | |

**Record allocation** is from a fixed free list (address family rooted at `DAT_0320e520`), not a simple growing array; exhausting it logs the literal message `"Bump AUDIOLIB_MAX_SOUNDBANKS!\n"` through a registered log callback. The exact capacity constant was not resolved (**OPEN**) — the shipped 496-row file did not exhaust it.

Fields `+0x50` (unconditionally set to `0xFFFFFFFF` after the flags above are applied) and `+0x54`/`+0x56` (written only for the boot-load path, from a direct 44-byte header read of the resolved `_media.bnk_pc` file at the point the record's `+0x44` "has streaming companion" flag is set) belong to the *consumer* side already covered by `spec-audio-format.md` §4/§7 and are not re-derived here; only the fact that `audio_banks.xtbl` is what populates `+0x40`/`+0x44`/`+0x54` bit 15/`+0x5b` bits 0–3 is new.

### 2.4 Validation against the real base-game file **[CONFIRMED — empirical]**

Extracted from `misc_tables.vpp_pc` entry 51 (285,851 bytes, exact length match). **496 `<NewEntity>` rows, all 496 with both `Name` and `wwise_id`** (§2.2's hard-fail path is never exercised); exactly **1** row named `Init` (case-sensitive and case-insensitive counts agree — no near-miss casing exists in the data); `load_at_boot` = `False` in 473 rows, `True` in 23; `streaming` = `True` in 494, `False` in exactly 2 (both exact-case, so §1.3 item 3's case-sensitivity quirk has zero practical effect on this file); every one of the 496 `wwise_id` values matches the bespoke digit-prefix grammar (§1.3 item 1) with zero exceptions.

---

## 3. `audio_constants.xtbl` — global audio tuning constants

A single-row "globals" table (`spec-xtbl-format.md` §3's pattern), read once by `FUN_00553580` into a flat set of named globals — no runtime array, no per-row record. Row: `<AudioConstants>` directly under `<Table>`, containing:

- **`PlayTimers`** (11 `f32`, all the shared "always" `u32`/`f32` family, `FUN_00DABDF0`/`FUN_00DACCB0` — **corrected 2026-09-28, Team B: the prose label read "if present," contradicting both this document's own §1.2 catalogue (line 22, which assigns these two addresses to the "always" pair) and this document's own later correct usage of `FUN_00DACCB0` as "always" at §5's `MinimumSpeed` row — the cited addresses were always right, only the prose label was wrong**): `BrassCollision`, `GlassShatter`, `BulletImpactHuman`, `BulletImpactWall`, `ObjectDebris`, `VehicleImpactCollision`, `VehicleImpactDistance` (squared in place after load — `distance²`, presumably compared against a squared position-delta to avoid a `sqrt`), `VehicleScrapeCollision`, `VehicleScrapeDistance` (squared likewise), `RagdollBoneImpactCollision`, `SmallDeformation`, `LargeDeformation`.
- **`OnFootSettings`**: `FootstepRange` (`f32`, squared in place).
- **`DrivingSettings`**: `AmbientSpawnAcquireRadio` (`f32`); child `Alarm` → `Percentage` (`f32`), `TimeMin`/`TimeMax` (each read as `u32` then truncated to a stored `u16`); child `Passby_whoosh` → `min_distance_on_foot`, `min_distance_driving`, `min_speed` (all `f32`).
- **`Wind`** (element name resolved by address — the literal is `Wind`, not a generic label): child `high_altitude` → `min_speed`, `max_speed`, `wind_change_rate`, `min_altitude`, `max_altitude`; child `player_falling` → `min_speed`, `max_speed`, `parachute_speed` (all `f32`).
- **`Player_Health`**: `med_health` — read as an **`s32`** (`FUN_00DABC70`, the shared "always" integer reader, not a float) then converted: `stored = (float)(int)med_health * 0.01` (the multiplier is a genuine global constant, value `0.009999999776482582` — i.e. a percent-to-fraction scale). **[CONFIRMED — disassembly for the constant's read address and value.]**

**Validation [CONFIRMED — empirical]**, extracted from `misc_tables.vpp_pc` entry 52 (14,541 bytes): all five named sub-blocks present in the real file; sample values read cleanly — `PlayTimers.BrassCollision = 200`, `PlayTimers.VehicleImpactDistance = 12` (→ squared to 144 at load), `PlayTimers.LargeDeformation = 1000`.

---

## 4. `audio_settings.xtbl` — global Wwise-adjacent tuning

Another single-row globals table. `FUN_00467FC0` opens the file and, only if a `<global_settings>` child exists at all, calls `FUN_00467F40`, which reads two further sub-blocks, both via the shared "if present" `f32` reader (`FUN_00DACD40`) so an absent element simply keeps whatever default was primed beforehand:

- **`general_settings`** → `Speed_of_sound` (default primed from a global constant, value **343.5**, real-world m/s — never actually written by the shipped row, §4.1) and `Health_adjust_rate` (no explicit default-priming instruction found in this reader; whatever the `.data` section already holds is the fallback if absent).
- **`Doppler_settings`** → `Doppler_multiplier` (default primed from a global constant, value **11.0**).

### 4.1 Validation **[CONFIRMED — empirical]**

Extracted from `misc_tables.vpp_pc` entry 57 (1,888 bytes). The real file's `<global_settings>` block supplies only **`Health_adjust_rate = 0.5`** and **`Doppler_multiplier = 11.0`** — `Speed_of_sound` is genuinely absent from the shipped row (confirmed by direct inspection of the raw file, not a parsing artefact), so the retail game runs on the coded default of 343.5 m/s for this field. Incidentally, the file's own embedded `TableDescription` schema metadata (`spec-xtbl-format.md` §3.2) documents an *authoring-tool* default of `0.75` for `Health_adjust_rate` — irrelevant here since the shipped row supplies an explicit value, but worth noting as a real (if inert) discrepancy between the schema's stated default and the loader's own runtime default-priming code.

---

## 5. `audio_line_tags.xtbl` — a striking, confirmed capacity mismatch

`FUN_00709D90` opens the file and walks `<Audio_line>` rows **directly under `<Table>`** (confirmed not nested — see §5.1). Per row it reads `Name` (bounded copy into a **local scratch buffer that is never used again** — read, then discarded, not stored anywhere persistent) and `wwise_id` (`u32`, "if present", `FUN_00DABE80`) into a flat array of bare `u32` ids at `DAT_01504484` (allocated once, `0x1E0` = 480 bytes = **120** `u32` slots). The loop explicitly **breaks once the stored count exceeds `0x76` (118)** — i.e. after storing index 118 (the 119th entry, 0-based), giving an effective cap of **119** stored ids out of a 120-slot allocation.

This file's exact-filename literal has **exactly one** code cross-reference in the whole executable (`tbl_exe_lit.py`/`AfStrXrefs.java`, §1.1) — this loader is the *only* code that ever opens `audio_line_tags.xtbl`.

### 5.1 Validation — the mismatch is real and large **[CONFIRMED — empirical]**

Extracted from `misc_tables.vpp_pc` entry 54 (797,216 bytes — by far the largest file in this group). The real file contains **4,626** `<Audio_line>` elements, every single one confirmed to sit as a **direct child of `<Table>`** (2-tab indentation, one level below `<Table>`'s own 1-tab — checked directly against the raw bytes, ruling out a nesting artefact in the row count). Given the loader's 119-entry effective cap and the single, exhaustively-confirmed loader for this file: **over 97% of this table's rows (4,507 of 4,626) never reach the runtime array this loader builds.** Each of those rows' `Name` text is read into a scratch buffer and discarded (not even logged, from the traced code); their `wwise_id` is never read at all once the count check fails, since the loop simply stops calling the per-element reader.

**What this means, stated carefully:** this is a genuine, confirmed structural fact about the *traced loader*, not a claim that the game "loses" this data through some other path — the exhaustive single-cross-reference scan makes a second, unfound reader for this exact filename very unlikely, but not impossible (a differently-named wrapper calling into the same open-and-walk primitives at a different call site was not separately searched for). **[CONFIRMED — disassembly + empirical for the traced loader's behaviour and the real row count; HIGH CONFIDENCE, not fully exhaustive, that no second loader exists — see §20 item 1.]**

---

## 6. `audio_personas.xtbl` — persona demographic classification and its own name-suffix convention

**Loader `FUN_0070B090`** (DLC-aware: `dlc%d_audio_personas.xtbl` or plain). Row `<Audio_Persona>`. First base-game load allocates a **300-entry**, `0x30`-byte-stride record array (`0x3840` bytes = exactly `300 × 0x30`) at `DAT_01504094`; capacity is bound-checked directly (the loader fails, returning null, once the count exceeds 299).

Per row, `Name` and an "if present" `wwise_id` (`u32`) are read by the loader itself; if a persona with the same `wwise_id` already exists in the table it is skipped (deduplication by id, linear scan), otherwise a new record is filled by **`FUN_00709F40`**:

| Record offset | Field | Source | Default |
|---|---|---|---|
| `+0x00`–`+0x1F` | `Name` | bounded copy, `0x20` bytes | — |
| `+0x20` | `wwise_id` | the caller's already-parsed value | 0 |
| `+0x24` | *(unused by this reader)* | — | `0xFF` | see §7 — this is exactly the byte `persona_radio_prefs.xtbl` fills in later. |
| `+0x26` | *(unused by this reader)* | — | `0xFFFF` | not traced further; **OPEN**. |
| `+0x28` | **gender** (`u8`) | derived from `Name`, see below | `0` (unspecified) |
| `+0x29` | **ethnicity** (`u8`) | derived from `Name`, see below | `0` (unspecified) |
| `+0x2A` | **age** (`u8`) | derived from `Name`, see below | `0` (unspecified) |
| `+0x2C` | *(unused by this reader)* | — | `0` | not traced further; **OPEN**. |

**The gender/ethnicity/age fields are not read from any XML element at all** — they are inferred by *substring-searching the persona's own `Name` text* (via a helper, `FUN_00EA48B0`, behaving as a plain substring search — its own body was not independently decompiled, only its call pattern) for a fixed underscore-prefixed suffix convention: `_WM`/`_WF`/`_BM`/`_BF`/`_HM`/`_HF`/`_AM`/`_AF` (White/Black/Hispanic/Asian × Male/Female — searched only after first confirming the name contains an underscore at all), giving gender `1`=male/`2`=female and ethnicity `1`=White/`2`=Black/`3`=Hispanic/`4`=Asian (first match in that fixed priority order wins; no match leaves both `0`). Independently, the name is searched for the literal substrings `Young`→age `1`, `Middle`→age `2`, `Elderly`→age `3` (first match wins; none leaves `0`). **[CONFIRMED — disassembly, full body of `FUN_00709F40` read.]**

### 6.1 Validation **[CONFIRMED — empirical]**

Extracted from `misc_tables.vpp_pc` entry 56 (27,886 bytes). **265** `<Audio_Persona>` rows (under the 300 cap). **233/265 (88%) of names match one of the eight ethnicity/gender suffixes** (breakdown: `_WM` 47, `_BM` 33, `_HM` 31, `_WF` 30, `_HF` 24, `_BF` 24, `_AM` 23, `_AF` 21 — confirming this is the dominant, not a niche, naming convention for generic ped voices). **84/265 (32%) match an age token** (`Young` 40, `Middle` 40, `Elderly` 4 — the remaining 181 personas, including named characters like `Angel`/`Bobby`/`Gat`, carry no age token, consistent with those being unique characters rather than generic demographic ped voices).

---

## 7. `persona_radio_prefs.xtbl` — patches the persona record's unused `+0x24` byte

Short, self-contained loader (`FUN_0070A100`): row `<Audio_Persona>` (same row-element name as §6's table, but a genuinely separate file). For each row with both `Name` and `Radio_Station` present, it resolves `Name` to an existing persona record (`FUN_00709EB0`, a name-lookup over the table §6 built) and `Radio_Station`'s text to a station index (`FUN_0055DD50`, not independently decompiled — its return value is stored as a single byte), writing that station index into the resolved persona record's **`+0x24`** — closing the "unused by this reader" note left in §6's table for that exact byte (default `0xFF`, presumably "no station preference").

**Radio_Station's exact matching rule against `radio_stations.xtbl` was not traced** (`FUN_0055DD50`'s body was not decompiled), but the real data shows a strong structural correspondence rather than an exact string match: `persona_radio_prefs.xtbl`'s `Radio_Station` values are short callsign-like tokens (`THE_MIX`, `KRHYME`, `K12`, `KLASSIC`, `KRUNCH`, `KABRON`, `ADULT_SWIM`, `GENX`, …) that visibly correspond to a *stem* of `radio_stations.xtbl`'s own `Genre` values (`RADIO_STATION_GENRE_KRHYME`, `RADIO_STATION_GENRE_K12FM`, `RADIO_STATION_GENRE_KLASSIC`, `RADIO_STATION_GENRE_GEN_X`, `RADIO_STATION_GENRE_ADULT_SWIM`, …) rather than to any `Name` field (§11 — stations have no `Name` element in this loader's own reads). **[HIGH CONFIDENCE — the correspondence pattern is empirical and unambiguous; OPEN — the exact string-matching/normalisation rule inside `FUN_0055DD50` was not read.]**

### 7.1 Validation **[CONFIRMED — empirical]**

Extracted from `misc_tables.vpp_pc` entry 198 (41,523 bytes). **242** rows, **all 242** with both `Name` and `Radio_Station` present. Most common `Radio_Station` values: `THE_MIX` (46), `KRHYME` (45), `K12` (30), `KLASSIC` (24), `KRUNCH` (24), `KABRON` (22), `ADULT_SWIM` (18), `GENX` (18).

---

## 8. `foley_collision.xtbl` — collision foley switches

**Loader `FUN_00561450`.** Row `<FoleyCollision>` directly under `<Table>`; capacity is exactly the real row count (`thunk_FUN_00DC5150`), array allocated `count × 0x14` bytes at `DAT_013C85B0` (count itself kept at `DAT_013C85B4`, `u16`). This table's shared resolver was already named — but not specced — by `spec-tables-weapons-combat.md` §1.6: *"`FUN_00561370` … foley-collision records (`0x013C85B0`, stride 20 bytes, count = `u16` at `0x013C85B4`) … Wwise-style id compared with the record's first dword."* **This pass supplies the schema behind that already-found resolver.**

Per-row fields (`0x14` = 20-byte record):

| Offset | Field | Source element | Type / reader | Notes |
|---|---|---|---|---|
| `+0x00` | name id | `Name` (direct child of `<FoleyCollision>`) | Wwise/AK hash (`FUN_00462960`) | The key `FUN_00561370` matches against. |
| `+0x04` | `MinimumSpeed` | child of `<CollisionFoleySet>` | `f32`, "always" (`FUN_00DACCB0`) | **Converted mph→m/s** in place, ×`0.44704` (the exact constant already established for vehicle speeds, `spec-vehicle-data.md` §7.1). |
| `+0x08` | `MaximumSpeed` | child of `<CollisionFoleySet>` | `f32`, "always" | Same mph→m/s conversion. |
| `+0x0C` | `Frequency` | child of `<CollisionFoleySet>` | `u32`, "if present" (`FUN_00DABE80`) | |
| `+0x10` | `Wwise_switch` id | child of `<CollisionFoleySet>` | Wwise/AK hash (`FUN_00462960`) | |

Only the **first** `<CollisionFoleySet>` child of a row is ever read (`thunk_FUN_00DC4FF0`, not iterated) — a row with more than one such child would have the rest silently ignored. **[CONFIRMED — disassembly.]**

**Validation [CONFIRMED — empirical]**, `misc_tables.vpp_pc` entry 131 (20,792 bytes): **55** rows, all 55 with `Name`, a `CollisionFoleySet` block, `Wwise_switch`, `Frequency`, `MinimumSpeed` and `MaximumSpeed` present (100% coverage on every field); zero rows carry more than one `CollisionFoleySet` block, so the "only the first is read" caveat has no practical effect on retail data.

---

## 9. `foley_touch.xtbl` — touch/handling foley switches

**Loader `FUN_00561AB0`**, structurally the twin of §8 but with a smaller, 12-byte (`0xC`) record at `DAT_013C85D0`/count `DAT_013C85D4`. Row `<FoleyTouch>`.

| Offset | Field | Source element | Type / reader | Notes |
|---|---|---|---|---|
| `+0x00` | name id | `Name` (direct child of `<FoleyTouch>`) | **`FUN_00D9E7E0`** — the engine CRC-32 table, **not** the Wwise/AK hash `FUN_00462960` used by every other Name-hash in this group, and (§1.3 item 2) **not lower-cased** | The one table in this group whose row-name key is hashed differently from its siblings — flagged explicitly since it is easy to assume uniformity across the family. |
| `+0x04` | `Frequency` | child of `<TouchFoleySet>` | `u32`, "if present" | |
| `+0x08` | `Wwise_switch` id | child of `<TouchFoleySet>` | Wwise/AK hash (`FUN_00462960`) | |

Only the first `<TouchFoleySet>` child is read, same caveat as §8.

**Validation [CONFIRMED — empirical]**, `misc_tables.vpp_pc` entry 133 (3,524 bytes): **8** rows, all 8 with `Name`, `TouchFoleySet`, `Wwise_switch` and `Frequency` present; no row with more than one `TouchFoleySet` block.

---

## 10. `foley_engine.xtbl` — closes an open destination in `spec-vehicle-data.md`'s vehicle record

**Loader `FUN_005591B0`** (DLC-aware). Row `<Engine>` directly under `<Table>`. Unlike every other table in this group, records are **not** a single contiguous array: each is an individually heap-allocated `0x10`-byte block, addressed through a **pointer array** `DAT_013BB8A0[index]`, where `index` is a running counter (`DAT_013BBAA0`) incremented once per row across the base-game load and any DLC frameworks loaded afterward — i.e. indices are assigned **in file/load order**, not by any hash. No bound check on this pointer array was found in the traced code (**OPEN** — exact capacity unknown).

Per-row fields, read by **`FUN_005590C0`** (verified against raw disassembly, since the decompiler elided several implicit-register arguments):

| Offset | Field | Source element | Type / reader | Notes |
|---|---|---|---|---|
| `+0x00` | engine CRC-32 of `Name` | `Name` (direct child of `<Engine>`) | `FUN_00D9E8B0`, seed 0, no length limit — lower-cased per the documented family | |
| `+0x04` | Wwise/AK hash of `"Veh_" + Name` | derived from `Name` (sprintf-built, not a separate element) | `FUN_00462960` | The actual Wwise switch/state name driving the engine sound is `Veh_<Name>`, not `<Name>` itself. |
| `+0x08` | Wwise/AK hash of `Vehicle_Model` | `Vehicle_Model` (optional child) | `FUN_00462960`, 0 if absent | |
| `+0x0C` | `NPC_Only` | optional child | `bool`, "if present" (`FUN_00DAC510`), default `false` | |
| `+0x0D` | `dlc_framework_id` | optional child | `s8`, "if present" (`FUN_00DAC3B0`), default `0xFF` | Same "0xFF = not DLC" sentinel convention already documented for other tables' `Is_DLC`-style gate bytes. |

**Cross-reference — closes an open item in `spec-vehicle-data.md`.** That document's §7.3/§7.4 records the vehicle entry's `Foley` block as: `Engine` **`u16`** at `+0x7AC`, followed by *u32 hashes* of every other named Foley sound (`Gear_Shift`, `Gear_Grind`, …) starting `+0x7B0` — and flags no distinction for why `Engine` alone is a `u16` rather than a hash. **It is a `u16` because it is an index into this exact `foley_engine.xtbl`-built pointer array (`DAT_013BB8A0`)**, not a Wwise hash at all — the vehicle record's `Foley > Engine` field selects one of this table's rows by position, and everything downstream (the two derived hash fields above) comes from *this* table, not from anything stored in the vehicle record itself. **[HIGH CONFIDENCE — inferred from the matching field width (`u16` index vs the pointer-array's own index type) and the shared subject matter (per-vehicle engine sound); the actual consumer code that reads the vehicle's `+0x7AC` value and indexes `DAT_013BB8A0` with it was not independently traced from the vehicle side, so this is not re-verified disassembly on both ends — flagged honestly rather than claimed as fully closed.]**

**Validation [CONFIRMED — empirical]**, `misc_tables.vpp_pc` entry 132 (15,425 bytes): **84** `Engine` rows, all 84 with `Name` (no duplicates) and `Vehicle_Model` (100% coverage — every base-game row supplies it, higher than might be guessed for an "optional" element); **zero** rows use `NPC_Only` at all (the field exists in the reader but is unused in the entire base game); **zero** rows carry `dlc_framework_id` (expected — that field is DLC-file-only).

---

## 11. `radio_stations.xtbl` — the station array `spec-save-format.md` §12.8 already reads from the save side

**Priority table.** `FUN_0055E760` builds the exact runtime object `spec-save-format.md` §12.8 already names from the *consumer* side: **"the radio manager is the object behind the pointer at `0x013C58CC` (stations at stride `0x690`...)"** — this loader is where that pointer, that stride, and every field offset that document's §12.8.1/§12.8.3 cite (`+0x140` id, `+0x689` flag byte) actually come from.

### 11.1 Loader shape

Row `<NewEntity>` (the file's single settings block) directly under `<Table>`, containing:

- `Simultaneous_NPC_Radios` (int, via a helper not independently decompiled, `FUN_00EA47E1`) → global `DAT_013BBAB0`, default 1.
- `Radio_Station_List` → a nested container whose children are named **`<Info>`** (resolved by address; not `<Station>` as might be guessed). Count of `<Info>` children **+ 1** becomes the allocated station-array capacity (the `+1` reserves station index 0).
- Station array allocated at `DAT_013C58CC`, stride **`0x690`** bytes, zeroed, then **station 0 is hard-initialised in code, not from any row**: `+0x40` = the literal display name `"My Radio 85.5"`, `+0x689` bit 5 cleared then set (bits 3/5 of the flag byte set directly, matching `spec-save-format.md` §12.8.1's identification of record 0 as **"Mix Tape"**), and a block of default audio-processing floats (EQ-like constants at `+0x5B0`–`+0x668`, not xtbl-derived, not decoded further here — out of this document's scope).
- The `<Info>` rows are then walked to fill stations **1..N** (station 0 is never touched by this walk).

### 11.2 Per-`<Info>`-row fields → station record (base = station-array base `+ 0x689` for the *first* row, `+0x690` per subsequent row — i.e. offsets below are relative to each station's own base)

| Offset | Field | Source element | Type / reader | Notes |
|---|---|---|---|---|
| `+0xC0` | station config filename | `xtbl_name` → `Filename` (nested) | bounded string copy, `0x40` bytes | |
| `+0x100` | `Genre` | direct child | bounded string copy, `0x40` bytes, raw text pointer forwarded (not re-fetched) | e.g. `RADIO_STATION_GENRE_KRHYME` — a **localisation-string key**, not free text (§7's cross-reference). |
| `+0x689`/`+0x68A` | `Station_flags` → `Flag` children | direct children, case-insensitive `_stricmp` against 4 literals (an if/else chain, not an enum table) | see below | |
| `+0x140` | `wwise_id` | direct child | text hashed via the **Wwise/AK string-to-id function** (`FUN_00462960`) — **not** parsed as a number the way `audio_banks.xtbl`'s field of the same name is (§1.3 item 1) | **Confirms the exact field `spec-save-format.md` §12.8.3 already names** ("its id (`+0x140`)") and how it is derived. |

`Station_flags`'s `<Flag>` children are matched (case-insensitive) against exactly four literals, each setting bits of `+0x689`/`+0x68A` and bumping a matching-count global; **any other `Flag` text is silently ignored** (not an exhaustive-enum check):

| Literal | Bits set | Running count global |
|---|---|---|
| `Selectable` | `+0x689` bits 5 **and** 6 (`0x60`, together) | `DAT_013C58D8` |
| `Police_Station` | `+0x68A` bit 1 (`0x02`) | `_DAT_013C58D4` |
| `FBI_Station` | `+0x68A` bit 2 (`0x04`) | `_DAT_013C58D0` |
| `News_Station` | `+0x68A` bit 3 (`0x08`) | *(no counter)* |

Before applying flags, the loader clears `+0x689 &= 0x9F` (bits 5/6) and `+0x68A &= 0xF1` (bits 1/2/3) — i.e. these four bits are wholly determined by this element, not accumulated across loads.

**A confirmed structural fact with no counterpart element:** **no station display-name field is ever written for stations 1..N by this loader** — unlike station 0's hard-coded `"My Radio 85.5"`, real `<Info>` rows carry no `<Name>` child at all in the shipped data (§11.3), and this loader never reads one for them. Wherever the in-game radio UI gets a per-station display string, it is not from this element in this file. **[CONFIRMED — disassembly + empirical.]**

### 11.3 Validation **[CONFIRMED — empirical]**

Extracted from `misc_tables.vpp_pc` entry 221 (5,377 bytes). **11** `<Info>` rows (→ 12 total station slots allocated, matching the `+1` rule). `Genre` values include a station whose genre is literally `Off` — consistent with a hard-coded `"RADIO_OFF"` name string the runtime "selectable station" enumerator (`FUN_0055DDC0`, a consumer not itself re-derived from an xtbl element) explicitly filters out by `strncmp`. `Station_flags` in the real data use only `selectable` (9 of 11 stations) and `news_station` (1) — **zero** `Police_Station`/`FBI_Station` flags appear in the base game, and **zero** unrecognised `Flag` text was found (no silently-ignored data exists in retail content, though the mechanism for it is real). 9/11 rows carry `wwise_id`; all 11 carry the `xtbl_name`/`Filename` pair; **0/11** rows have their own `<Name>` child, confirming §11.2's "no display name" finding is not merely unread — the data genuinely doesn't have one there.

---

## 12. `playlist_artist_track.xtbl` — the track/artist catalog, and its relationship to `spec-save-format.md`'s mix-tape song ids

**Priority table.** `FUN_0083E0B0` opens `<Track_Listing><Tracks><Track>` (three levels of nesting) and, for up to **145** (`0x91`) rows, fills a flat catalog at `DAT_02316B60` (stride `0xC` = 12 bytes: `+0x00` `WWise_ID` (`u32`, "if present"), `+0x04` `Artist_Name` (interned/localised string handle via `FUN_00DB12C0`), `+0x08` `Track_Name` (same helper)). Extra rows beyond 145 would be silently dropped by the loop's own bound check (`DAT_02317230 < 0x91`); the real file (§12.3) is under this cap.

### 12.1 This is a display-text catalog, not the runtime "song table" `spec-save-format.md` §12.8 names

`spec-save-format.md` §12.8 states the radio manager keeps **"a song table at `0x013BBC18`, stride 20"**. This catalog (`DAT_02316B60`, stride 12) is a **different object at a different address** — confirmed by an exhaustive reference scan: `DAT_02316B60` has exactly 5 code references total, all inside two small functions (`FUN_0083E200`, `FUN_0083E230`) in a completely separate region of the executable from the ~30 functions that read/write `DAT_013BBC18` throughout the `0x0055Cxxx`–`0x0056xxx` radio-manager code range. They are related but **not the same structure** — see §12.2.

### 12.2 The confirmed join: `WWise_ID` is the key that resolves a played song to display text

Two functions read `DAT_02316B60`:

- **`FUN_0083E200(id)`** — a plain linear search: for each catalog entry, if `+0x00` (`WWise_ID`) equals the argument, return `+0x08` (`Track_Name`). A simple id→track-name lookup.
- **`FUN_0083E230`** — a larger, script/Lua-return-marshalling function (its call shape matches the return-value-pushing pattern documented for the engine's script bindings, `spec-lua-bindings.md`) that, for a given station, walks that station's **own current playlist slot array** — reached through **`FUN_0055DF50`** (returns the slot *count*, reading station `+0x690 − 0x500 = +0x190`) and **`FUN_0055DFB0`** (returns a pointer into the shared song table at `DAT_013BBC18`, indexed through a per-station `u16` array whose **pointer lives at station `+0x194`** and whose **count/bound lives at station `+0x5A4`**). For each slot, the returned song-table record's own first dword is compared against `DAT_02316B60`'s per-entry `WWise_ID` (a **linear scan of the whole catalog**) to find the matching `Artist_Name`/`Track_Name` for display.

**This is the confirmed mechanism**: `playlist_artist_track.xtbl`'s `WWise_ID` field is the join key between whatever numeric song identity the runtime "song table" (`0x013BBC18`, stride 20, §12.8's own address) stores per slot, and the artist/track text this table supplies for display. **[CONFIRMED — disassembly for the whole chain.]**

**Relationship to the save format's `u16` mix-tape song ids (`spec-save-format.md` §12.8.1).** That document's entry 17 is "the manager's `u16` list of song indices (`+0x1A4`, count at `+0x5A4`)" for station 0 specifically. This pass finds the **general**, per-station mechanism one field over: `+0x194` (a *pointer*, not an inline array) with the **same** count field at `+0x5A4`. The natural, unforced reading — **not independently confirmed by reading the code that initialises station 0's own `+0x194`** — is that station 0 (the "Mix Tape") is the one station whose `+0x194` pointer is set to point at its *own* inline `+0x1A4` buffer, unifying the general per-station mechanism with the special-cased save-format field; every other station's `+0x194` presumably points at a separately-built or shared array. **[HIGH CONFIDENCE for the general per-station mechanism (disassembly-read); HYPOTHESIS for the specific claim that station 0's pointer self-references `+0x1A4` — this exact link was not traced on the station-0 side.]** Either way, the **`u16` values the save format persists are indices into the stride-20 song table at `0x013BBC18`, not `WWise_ID` values directly** — `playlist_artist_track.xtbl` supplies the display text *for whatever song a `0x013BBC18` slot's own (still-uncharacterised) `WWise_ID`-shaped field names**, one indirection removed from the save file's own `u16`s. The exact byte layout of the stride-20 song table itself was not mapped this pass (**OPEN**, §20).

### 12.3 Validation **[CONFIRMED — empirical]**

Extracted from `misc_tables.vpp_pc` entry 214 (21,996 bytes). **138** `<Track>` rows — under the 145-row cap, so no data loss in the shipped file. All 138 carry `WWise_ID`; zero duplicate `WWise_ID` values (the catalog's linear-scan join key is unique in practice, though the code does not enforce this).

---

## 13. `radio_activities.xtbl` — an 8-slot, unchecked-bound chance table

`FUN_0060E790` pre-zeros **exactly 8** module-level globals (stride 8 bytes: `Level` `u32` + a normalised `Percentage` `f32`) before walking `<RadioActivities><ChancesToPlay><ChanceToPlay>` rows into them at `DAT_014B02A0` — **with no bound check in the row-walking loop itself.** A 9th row in this file would silently overrun the fixed 8-slot region.

Per row: `Level` (`u32`, "if present") and `Percentage` (`u32`, "if present", then **reinterpreted as a signed value**: if negative, `4294967296.0` (`2³²`) is added — i.e. the raw bit pattern is corrected back to its unsigned reading — before dividing by a denominator global (`DAT_012A2DD8`) that reads as **0** in the static image, meaning its real value is primed by code not traced this pass; **OPEN**). The add-back-2³² step is dead in practice on retail data (§13.1) but is real, disassembly-confirmed code.

### 13.1 Validation **[CONFIRMED — empirical]**

Extracted from `misc_tables.vpp_pc` entry 219 (2,325 bytes). **Exactly 8** `<ChanceToPlay>` rows — the unchecked 8-slot bound is an exact match in the shipped file, not exceeded. `Level` values are **1..8** (one-indexed, not 0-based as might be assumed). Raw `Percentage` values: `1, 2, 3, 5, 10, 15, 20, 25` — all non-negative, so the negative-wraparound branch is never exercised by retail data.

---

## 14. `radio_events.xtbl` — a second, more severe uninitialised-memory quirk, and the `EventType` enum

`FUN_0055C1D0` allocates an array at `DAT_013BBBF8`, stride **`0x34`** (52 bytes), sized to the exact row count — but **only `memset`s the first `count × 4` bytes of that `count × 0x34`-byte buffer**, not the whole thing. Per row, **`FUN_0055C100`** writes only a handful of the 52 bytes (`+0x08` engine CRC-32 of `Name`, `+0x0C` `EventType` enum index, `+0x10` `Post_Time`, `+0x14` a cleared byte, `+0x2C` a `u16` slot index, `+0x2F` `MaxTimesPlayed`) — **roughly 17 of the 52 bytes per record.** The remaining ~35 bytes of every record (including all of `+0x00`–`+0x07`, `+0x15`–`+0x2B`, `+0x2D`–`+0x2E`, `+0x30`–`+0x33`) are **never written by this reader at all**, and — because the `memset` only clears a `count`-scaled *prefix* of the whole buffer rather than each record individually — the large majority of those bytes across the array start as genuine uninitialised heap memory, not zero. This is a stronger version of the already-documented "always-write accessor gives an unspecified value when the element is absent" pattern (`spec-tables-weapons-combat.md` §1.3): here entire *unwritten* byte ranges of every record, not just one absent-field slot, are left as heap residue. **[CONFIRMED — disassembly, full body of both the allocator and the per-row reader read.]**

Per-row fields actually written:

| Offset | Field | Source | Type / reader | Notes |
|---|---|---|---|---|
| `+0x08` | engine CRC-32 of `Name` | `Name` (required — absent skips the rest silently for that row via a 0-length hash, not traced further) | `FUN_00D9E8B0`, lower-cased | Very likely the same value the "posted radio events" runtime ring (`spec-save-format.md` §12.8.2) stores as its own "event id hash" field — **not independently confirmed on the ring side**, but both are the same hash family over the same kind of name. |
| `+0x0C` | `EventType` | direct child | enum, `FUN_00DAC830` over a fixed 5-literal table | Values: `Commercial` (0), `News` (1), `Police` (2), `FBI` (3), `Police and FBI` (4). Ties directly to `radio_stations.xtbl`'s own `Police_Station`/`FBI_Station`/`News_Station` flags (§11.2) — an event's type is which category of station it's eligible to interrupt. |
| `+0x10` | `Post_Time` | direct child, optional | `s32`, "if present", default **1** | Matches `spec-save-format.md` §12.8.2's own description of the runtime ring's timer as "posted... with a `Post_Time`-hours timer" — this closes the field-name side of that already-published claim. |
| `+0x2C` | a `u16` slot index | derived from `Name` (Wwise/AK-hashed, then resolved via `FUN_0055E470`, the same "hash → small index" resolver `commercials.xtbl` uses, §16) | — | Exact consuming structure for this specific index not traced (**OPEN**). |
| `+0x2F` | `MaxTimesPlayed` | direct child, optional | `s32`, "if present", truncated to a byte, default **1** | |

### 14.1 Validation **[CONFIRMED — empirical]**

Extracted from `misc_tables.vpp_pc` entry 220 (4,762 bytes). **13** `Event` rows, all 13 with `Name`, `Post_Time` and `MaxTimesPlayed` present. **All 13 real rows use `EventType = News`** — the `Commercial`/`Police`/`FBI`/`Police and FBI` enum values, while fully supported by the reader, have **zero occurrences** in the shipped base-game table (worth stating plainly rather than assuming even coverage of a documented enum).

---

## 15. `commercial_events.xtbl` — the 30-slot registry `spec-save-format.md` §12.8.2 already persists

**`FUN_0055BD30`** walks `<Event>` rows and, for each with both `Name` and an `EventValue` strictly **less than 30** (`0x1E` — values `≥30` are silently dropped, not clamped), stores the engine CRC-32 (`FUN_00D9E740`, lower-cased) of `Name` into a fixed 30-slot array `DAT_013BBB08` (stride 2 dwords; only the first dword — the name hash — is written by this loader, indexed by `EventValue`), keyed by that same `EventValue` as the array index.

**This is, field-for-field, the table `spec-save-format.md` §12.8.2 already describes empirically from the save side**: *"30 bytes: one 0/1 flag per commercial event (30 events named in the commercial-events data table, slot = its `EventValue`)."* This pass supplies the loader: `EventValue` (`s32`, "always", `FUN_00DABC70`) is the slot index, bound-checked `< 30`, and `Name`'s hash is what a commercial (§16) references by `EnableEvent`/`DisableEvent` to select a slot.

### 15.1 Validation, including a cross-check against the already-published save spec **[CONFIRMED — empirical]**

Extracted from `misc_tables.vpp_pc` entry 80 (2,769 bytes). **15** rows, `EventValue` values: **1, 2, 6, 10, 13, 14, 16, 17, 21, 23, 24, 25, 26, 27, 28** — all `< 30`, no duplicates. **This independently reproduces `spec-save-format.md` §12.8.2's own empirical observation** that "new game has flags 1, 2, 6 and 27 set" — every one of those four slot numbers is confirmed here to be a real, defined `EventValue` in the shipped table, a clean cross-check between two independently-derived documents.

---

## 16. `commercials.xtbl` — resolves against `commercial_events.xtbl` with a clean, exhaustive empirical match

**`FUN_0055BDE0`** walks `<Commercial>` rows. Each commercial is identified by the Wwise/AK hash (`FUN_00462960`) of its own `Name`, used to find a **pre-existing** commercial record through a separate registry (`FUN_0055E470` hash→index, `FUN_0055DF80` index→record — neither independently decompiled; these commercial records are not built by this loader itself, only patched by it). Per row:

| Field | Type / reader | Destination (in the resolved commercial record) | Notes |
|---|---|---|---|
| `InitialState` | text, case-insensitive `_stricmp` against the single literal `Disabled` | `+0x13` (enabled flag, default **1**/enabled; only `Disabled`, exactly, clears it) | |
| `EnableEvent` | text, optional | `+0x11` (`s8`, default `-1`) | Hashed via the **engine** CRC-32 (`FUN_00D9E740`, **not** the Wwise hash used for the commercial's own `Name`) then **linearly searched against `commercial_events.xtbl`'s own 30-slot name-hash array** (§15) to find its `EventValue` slot; not found → `-1`. |
| `DisableEvent` | text, optional | `+0x12` (`s8`, default `-1`) | Same resolution as `EnableEvent`. |
| `Length` | `s32`, "always" | `+0x04` | |

### 16.1 Validation **[CONFIRMED — empirical]**

Extracted from `misc_tables.vpp_pc` entry 79 (19,914 bytes). **81** `Commercial` rows, all 81 with `Name`. `InitialState`: 48 `Enabled`, 33 `Disabled`. **33/81** rows carry `EnableEvent`, and **all 33 of those values are found in `commercial_events.xtbl`'s real `Name` set** — a clean 100% match. **28/81** carry `DisableEvent`, and **all 28 likewise match**. This is an exhaustive, empirical confirmation (not a spot check) that §15/§16's cross-reference mechanism is exactly as traced from the loaders.

---

## 17. `voc_sb_line_sit.xtbl` — resolves a genuinely open item in `spec-audio-format.md`: the `.lm_pc`/`DMLV` file type

**Priority finding.** `FUN_00468ED0` walks `<Entries><Entry>` rows, reading `Persona_id` (`u32`, "if present"), `Soundbank` (bounded string copy, `0x41` bytes) and `Num_line_situations` (`u32`, "if present"). For each entry it resolves `Soundbank` against the **already-built `audio_banks.xtbl` registry** (`FUN_004647C0`, a name→record lookup) and, **only if that bank's own `+0x54` flag word has bit `0x8000` set** — i.e. only for a bank `audio_banks.xtbl` marked "boot-loaded" (§2.3) — it:

1. Accumulates `Num_line_situations` into a running total.
2. Builds a filename: `Soundbank + ".lm_" + "pc"` — i.e. **`<Soundbank>.lm_pc`** — and (up to a 600-entry scratch capacity) queues it for a further per-file open/read pass (`FUN_00468CD0`, not traced — reading the actual `.lm_pc` file's own interior is `spec-audio-format.md`'s territory, out of this document's xtbl-schema scope).

**This directly answers `spec-audio-format.md` §9.2's open item.** That document found `.lm_pc` files in `sound_turbo.vpp_pc`, sharing base names with `voices.vpp_pc`'s per-character banks, beginning with an unidentified 4-byte magic `DMLV`, and stated: *"a per-character lip-sync or line-timing metadata table is a plausible gloss given the naming correspondence with voice banks (HYPOTHESIS — unconfirmed, no further evidence gathered)."* **This pass substantially strengthens that hypothesis with a disassembly-confirmed mechanism**: `voc_sb_line_sit.xtbl` ("**vo**ice **s**ound**b**ank **line sit**uation[s]", per its own filename and element vocabulary) is the table that, for every boot-loaded voice soundbank, names and opens exactly a `<name>.lm_pc` file and expects it to supply (or at least be sized consistently with) `Num_line_situations` records. **[CONFIRMED — disassembly for the filename construction and the gating condition; HIGH CONFIDENCE, not fully CONFIRMED, that this is the *only* or *primary* consumer of `.lm_pc` files, and the acronym `DMLV` itself remains unresolved — that is `spec-audio-format.md`'s own open item, not re-derived here.]**

### 17.1 Validation, including a clean cross-check against `audio_banks.xtbl` **[CONFIRMED — empirical]**

Extracted from `misc_tables.vpp_pc` entry 323 (41,373 bytes). **265** `Entry` rows (coincidentally the same count as §6's `audio_personas.xtbl`, consistent with — but not proven to be — a 1:1 correspondence via `Persona_id`), all 265 with `Persona_id`, `Soundbank` and `Num_line_situations` present. **265 distinct `Soundbank` values, and every single one of them is a real `Name` found in the extracted `audio_banks.xtbl`** (§2) — a clean, exhaustive 100% match, strongly supporting the cross-table link traced above.

---

## 18. Cross-table reference map

Every cross-reference below was independently checked this pass (disassembly and/or the empirical extraction of §2–§17), not assumed from another document's text.

### 18.1 → `spec-audio-format.md`

- **§7.4–§7.5's `0x5c`-byte bank record**: `+0x40` (numeric bank id), `+0x44` (streaming-companion flag), `+0x54` bit 15 / bits (boot-load), `+0x5b` bits 0–3 are all populated from `audio_banks.xtbl` (§2.3) — this is the concrete "data-driven table naming which banks... load" candidate that document's §8 left as one of three untested options for what sets the load/unload state machine's trigger flags. **It answers the boot-time/always-loaded half of that question**; the *dynamic*, mission/zone-triggered half (§8's other two candidates: Lua, or hardcoded per-subsystem calls) is not addressed by any table in this group and remains open there.
- **§6's statically-linked Wwise SDK surface** gains one further, previously-untraced call site: `audio_banks.xtbl`'s `ram_size_pc` element, for a boot-load bank, drives a direct `AK::MemoryMgr::CreatePool`/`SetPoolName` call (§2.3) — a genuine extension of that document's Wwise-export census, not a contradiction of it.
- **§9.2's `.lm_pc`/`DMLV` open item** is substantially advanced by `voc_sb_line_sit.xtbl` (§17) — the naming and gating mechanism that produces `.lm_pc` filenames is now disassembly-confirmed; the file's own interior format and the `DMLV` acronym remain that document's open items, correctly left there.

### 18.2 → `spec-save-format.md`

- **§12.8.1 (entry 17, the mix-tape playlist)**: the manager pointer `0x013C58CC`, station stride `0x690`, and (per-station, generalised) the `+0x194`/`+0x5A4` pointer/count pair are all built by `radio_stations.xtbl`'s loader (§11); `playlist_artist_track.xtbl` (§12) is the display-text catalog joined to the runtime song table by `WWise_ID`, one indirection away from the save file's own `u16` song indices (§12.2).
- **§12.8.2 (entry 16, radio events + 30 commercial flags)**: `Post_Time` and the event-id hash both trace directly to `radio_events.xtbl` (§14); the 30-slot commercial-event array is `commercial_events.xtbl` (§15), and its own empirical "flags 1, 2, 6, 27 set on a new game" claim is independently reproduced from the real table content (§15.1).
- **§12.8.3 (entry 18, flagged/favourite radio stations, `+0x140`/`+0x689` bit 7)**: `+0x140` (the station id persisted per flagged station) is confirmed here as the Wwise/AK hash of `radio_stations.xtbl`'s `wwise_id` element (§11.2); the *meaning* of the bit-7 flag itself (favourite? unlocked? discovered?) is not resolved by anything in this group and remains that document's own open item.

### 18.3 → `spec-tables-weapons-combat.md`

- §1's shared reader-grammar family is used unmodified throughout this group (§1.2).
- §1.6's already-named-but-unspecced `FUN_00561370` foley-collision resolver is now fully specced (§8) — the schema behind that resolver's `0x013C85B0`/stride-20/`+0x00`-key description.
- §1.4's `FUN_00462960` (Wwise/AK string-to-id) recurs constantly in this group; a previously-undocumented sibling hash entry point (`FUN_00D9E7E0`, case-sensitive) is flagged as new (§1.3 item 2, §9).

### 18.4 → `spec-vehicle-data.md`

- §7.3/§7.4's vehicle-record `Foley > Engine` field (`u16` at `+0x7AC`) is very likely an index into `foley_engine.xtbl`'s pointer array, not a hash like its sibling Foley fields (§10) — presented at HIGH CONFIDENCE, not fully closed, since the vehicle-side consumer of `+0x7AC` was not independently re-traced this pass.
- The mph→m/s conversion constant (`0.44704`) that document establishes for vehicle speeds is confirmed reused verbatim by `foley_collision.xtbl`'s `MinimumSpeed`/`MaximumSpeed` (§8).

### 18.5 → `spec-xtbl-format.md`

- No new structural quirk requiring that document's own tolerant-parsing rules (§7) was found in any of these 16 files beyond what it already documents — all 16 parsed cleanly under a tolerant regex reader with no mismatched tags, control characters, or spaced element names encountered.
- §8's "299 distinct row-element names, 58,089 rows (top: `CRC` 10,630, `Entry` 5,590, `NewEntity` 5,539)" census is corroborated from this angle: `NewEntity` is confirmed here as the row-element name for both `audio_banks.xtbl` (496 rows) and `radio_stations.xtbl` (1 settings row), and `Entry`/`Entries` for `voc_sb_line_sit.xtbl` (265 rows) — concrete contributors to that document's aggregate counts.

---

## 19. Validation summary table

All rows below were extracted from `misc_tables.vpp_pc` this pass (byte-length exact match against the container's declared uncompressed size, per `vpp_modea.py`) and read with a tolerant regex reader, never a strict XML parser.

| Table | Archive entry | Bytes | Real row count | Loader capacity | Headline validation result |
|---|---:|---:|---:|---|---|
| `audio_banks.xtbl` | 51 | 285,851 | 496 `NewEntity` base **(544 base+DLC merged, see note below table)** | fixed free list, size OPEN | 496/496 have `Name`+`wwise_id`; exactly 1 `Init`; 0/496 `wwise_id` fail the bespoke grammar |
| `audio_constants.xtbl` | 52 | 14,541 | 1 (globals) | n/a | all 5 sub-blocks present |
| `audio_line_tags.xtbl` | 54 | 797,216 | **4,626** `Audio_line` | **119** effective | **4,507/4,626 (97%) rows never reach the runtime array** |
| `audio_personas.xtbl` | 56 | 27,886 | 265 `Audio_Persona` base **(293 base+DLC merged, see note below table)** | 300 | 233/265 match a demographic suffix; 84/265 match an age token |
| `audio_settings.xtbl` | 57 | 1,888 | 1 (globals) | n/a | `Speed_of_sound` genuinely absent (coded default 343.5 applies) |
| `foley_collision.xtbl` | 131 | 20,792 | 55 `FoleyCollision` | exact | 55/55 every field present, 0 multi-`CollisionFoleySet` rows |
| `foley_engine.xtbl` | 132 | 15,425 | 84 `Engine` base **(87 base+DLC merged, see note below table)** | unbounded (OPEN) | 84/84 `Vehicle_Model`; 0/84 `NPC_Only`; 0/84 `dlc_framework_id` |
| `foley_touch.xtbl` | 133 | 3,524 | 8 `FoleyTouch` | exact | 8/8 every field present |
| `radio_activities.xtbl` | 219 | 2,325 | 8 `ChanceToPlay` | **8**, unchecked | exact match at the boundary; `Level` 1..8 |
| `radio_events.xtbl` | 220 | 4,762 | 13 `Event` | exact (but under-zeroed, §14) | all 13 `EventType == News` |
| `radio_stations.xtbl` | 221 | 5,377 | 11 `Info` | count+1 | 9 `selectable`, 1 `news_station`, 0 unrecognised flags |
| `persona_radio_prefs.xtbl` | 198 | 41,523 | 242 `Audio_Persona` | patches §6's table | 242/242 have both fields |
| `playlist_artist_track.xtbl` | 214 | 21,996 | 138 `Track` | 145 | 0 duplicate `WWise_ID` |
| `voc_sb_line_sit.xtbl` | 323 | 41,373 | 265 `Entry` | 600 (scratch, loader-local) | 265/265 `Soundbank` values match a real `audio_banks.xtbl` `Name` |
| `commercial_events.xtbl` | 80 | 2,769 | 15 `Event` | **30**, bound-checked | reproduces the save-format spec's own "1,2,6,27" observation |
| `commercials.xtbl` | 79 | 19,914 | 81 `Commercial` | patches a separate registry | 33/33 `EnableEvent` + 28/28 `DisableEvent` resolve |

**Multi-archive correction (2026-09-28, Team B, independently re-verified by SPEC TEAM):** three of this group's tables — `audio_banks.xtbl`, `audio_personas.xtbl`, `foley_engine.xtbl` — each ship a SECOND, THIRD and FOURTH real copy, one per DLC archive, under a `dlcN_`-prefixed filename (`dlc1_audio_banks.xtbl` etc.) rather than the shared base filename — the same pattern already seen for `customization_items.xtbl`/`customization_outfits.xtbl` (`spec-customization-data.md` §6). Neither patch archive (`patch_compressed.vpp_pc`/`patch_uncompressed.vpp_pc`) carries any of the 16 tables in this group, checked directly. Confirmed merged totals, orchestrator-recounted from scratch against all 38 real archives: `audio_banks.xtbl` 496 base + 15 + 17 + 16 (dlc1/2/3) = **544**; `audio_personas.xtbl` 265 base + 7 + 11 + 10 = **293**; `foley_engine.xtbl` 84 base + 1 + 1 + 1 = **87**. The other 13 tables in this group have no DLC-prefixed counterpart. This does not change any of this section's per-table structural/validation findings above, which were run against the base copy only and remain accurate as base-copy findings.

---

## 20. Open items

1. **`audio_line_tags.xtbl`'s 97%-unreachable rows (§5.1)** — the single-cross-reference scan makes a second loader for this exact filename unlikely but not impossible; a differently-named entry point reaching the same generic open/walk primitives at a different call site was not separately searched for. If confirmed as the only path, the natural follow-up question — *why does the authored table carry 39× the loader's own capacity* — is a design/tooling question this project cannot answer from code alone.
2. **`spec-audio-format.md`'s dynamic/mission-triggered bank-loading question** remains open; this group only closes the boot-time/always-loaded half (§18.1).
3. **The stride-20 runtime song table at `0x013BBC18`** (`spec-save-format.md` §12.8's own address) was reached and its per-station indexing mechanism traced (§12.2), but its own internal field layout was not mapped — specifically, confirming that one of its fields is itself a `WWise_ID` matching `playlist_artist_track.xtbl`'s field of the same name would close the indirection this document currently states as HIGH CONFIDENCE rather than CONFIRMED.
4. **Whether station 0's `+0x194` pointer self-references its own `+0x1A4` inline buffer** (§12.2) — plausible and consistent with every fact gathered, but not read directly from the code that initialises station 0.
5. **`persona_radio_prefs.xtbl`'s exact `Radio_Station`-to-station matching rule** (`FUN_0055DD50`, §7) — the correspondence to `radio_stations.xtbl`'s `Genre` values is empirically strong but the normalisation rule itself was not decompiled.
6. **`foley_engine.xtbl`'s pointer-array capacity** and the vehicle-side consumer of `+0x7AC` that would fully close §10's HIGH CONFIDENCE cross-reference to `spec-vehicle-data.md`.
7. **Several small fields left unread by their own tables' loaders**: `audio_personas.xtbl` record `+0x26`/`+0x2C` (§6); `radio_activities.xtbl`'s `DAT_012A2DD8` percentage-denominator global, which reads as 0 in the static image (§13); `radio_events.xtbl`'s `+0x2C` slot-index field's actual consumer (§14).
8. **The `.lm_pc`/`DMLV` interior itself** is correctly left to `spec-audio-format.md` — not attempted here, per this project's scope discipline.

No further tables remain unassigned to this document — all 16 tables named in this group's task were reached, specced, and validated (§1.1, §19).

---

## 21. Artifacts

Ghidra disposable project copy: `tools/gp_ao1` (robocopied from `tools/ghidra_projects`, safe to delete once consolidated). Scripts (`tools/scripts/`): `AfStrXrefs.java`, `AfCallers.java`, `AhDecMk.java`, `AhAsm.java`, `AhTab.java` (all reused from agents AF/AH, unmodified) and one new, small script written this pass, `AoStrAt.java` (prints the ASCII string located *at* a given address — the reverse lookup `AfStrXrefs.java` doesn't provide). Dumps: `tools/ao_strxrefs1.txt`, `ao_strxrefs2.txt` (filename-literal cross-references), `ao_dec_*.txt` (decompiles), `ao_asm_*.txt` (annotated disassembly), `ao_strat*.txt`/`ao_tab1.txt`/`ao_mem*.txt` (literal/constant resolution). Extracted real base-game table content: `tools/ao_xtbl/` (all 16 files, byte-exact from `misc_tables.vpp_pc`). Runner: `tools/run_ao.ps1` (PowerShell headless-analyzer wrapper against `gp_ao1`, same shape as agent AH's `run_tbl.ps1`).

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): moved the orphaned `commercials.xtbl` row of the §19 validation table back above the "Multi-archive correction" paragraph so it renders inside the table, and reworded 2 decompiler-shaped expressions (§6 capacity check, §11.2 auto-named local).
- 2026-09-30 (cloud, self-containment pass): restated 0 load-bearing HANDOFF/WALLS-only facts inline; repointed 1 `HANDOFF.md` §27.x references to the archived headings (§27.2 → §31, header); 0 left (see review).
