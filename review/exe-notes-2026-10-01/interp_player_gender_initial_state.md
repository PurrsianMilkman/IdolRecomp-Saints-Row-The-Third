# Player character-record gender/ethnicity (`+0xa41` / `+0xa40`): where the initial value comes from

Team A, 2026-10-02. Investigative pass against the real executable (private copy of the local Ghidra
project at `tools/gp_gender`, program `SaintsRowTheThird.exe`, headless `-readOnly -noanalysis`,
`ghidra/CrreishDump.java`). Dump output lives in this session's scratchpad (`pg/` sub-folders) and is
not committed; everything below is described in prose. Real save files were read as data (authorised
for this brief); the shipped `misc_tables.vpp_pc` tables `player_presets.xtbl` and `character.xtbl`
were read as data through the existing reader `tools/harnesses/vpp_modea.py`; no `.czn_pc` bytes were
opened.

**The question.** A partner team's mission drive (`m21`) resolves `'#PLAYER1#'` correctly but then
reads the player's gender byte `+0xa41` and finds no meaningful value, because a host building a fresh
player object does not know what to put there. Where does the real engine's player get `+0xa41`
(gender) and `+0xa40` (ethnicity) when it is created, before any Lua runs: an engine default, the save,
or a customization table? And what should a host initialise them to?

Labels follow the house style: **CONFIRMED — disassembly** = read in a listed instruction stream of
this pass's dumps; **CONFIRMED — data** = read from a shipped table or a real save; **HIGH
CONFIDENCE** = follows from a dumped instruction or reference list but the body it points at was not
dumped; **HYPOTHESIS** = plausible, not settled; **OPEN** = not settled, collected at the end.

**Status: complete (§2 is the direct answer; §3 the host rule; §4 what stays open).**

## 0. Starting point (from prior passes, not re-derived here)

- `spec-lua-api-behaviour.md` §16.8: `pcr_get_player_preset` (0x00830790) reads the current player
  (0x009da4e0) and indexes the string table at 0x01160028 by `+0xa41` (`male`, `female`) and the one at
  0x01160030 by `+0xa40` (`asian`, `black`, `hispanic`, `white`). §28.12/§28.21 re-confirm `+0xa41`
  = 1 means female. The same section records the writer as OPEN.
- `spec-save-format.md` §10.1/§10.4: the local-player record is the object whose pointer sits in the
  global 0x0262edfc (getter 0x009da4e0). The save snapshot's character record (entry 1, file offset
  0x900, size 0x2254) carries two "character-selector" bytes at record +0x203c / +0x203d, copied from
  player-record `+0xa41` / `+0xa40`; the loader looks up a character preset by the pair (0x00831930).
  Observed: +0x203d = 3 in 16/16 samples, +0x203c in {0,1}, 0 in the new-game save.

## 1. Investigation trail

(Appended below as each dump is read.)

### 1.1 Every use of the two offsets (dump `pg/xref1`, raw displacement scan) — CONFIRMED

`+0xa41`: 78 uses in 61 functions; `+0xa40`: 43 uses in 34 functions (many of the `+0xa40` hits are
unrelated stack offsets or other objects' fields — vehicle module `LEA ECX,[ESI+0xa40]`, stack frames).
Every *store* of a byte to `+0xa41`/`+0xa40` on a character-shaped object:

| store site | function | value stored |
|---|---|---|
| 0x0095448c / 0x00954493 | 0x00954340 (character constructor, see the spawn-state note §1.1) | `+0xa40` := 5, `+0xa41` := 3 (literals), `+0xa42` := 0 |
| 0x0096ed4b | 0x0096ec80 (character vtable slot 1, "read properties from record") | `+0xa41` := character-definition byte `+0x18` |
| 0x00837370 / 0x00837379 | 0x00836d90 (apply a `player_presets.xtbl` preset) | `+0xa41` := preset `+0x5d`, `+0xa40` := preset `+0x5c` |
| 0x00b9754d / 0x00b97563 | 0x00b97510 (save-game character-record load) | `+0xa41` := record `+0x203c`, `+0xa40` := record `+0x203d` |
| 0x00bcaf21 / 0x00bcaf2e | 0x00bcaef0 (same shape as the save load, but from a global copy of the record at 0x02944570) | `+0xa41` := byte 0x029465ac, `+0xa40` := byte 0x029465ad |
| 0x00830fda | undefined code 0x00830fa0 = Lua `pcr_set_identity` (§1.13) | `+0xa41` := 0 for `"male"`, else 1, when different (compare-then-store) |
| 0x0089313f / 0x00893153 | undefined code 0x0089310d, a co-op appearance-message handler (§1.13) | `+0xa41` := message `+0x406`, `+0xa40` := message `+0x405` |

The 0x00954340 store is CONFIRMED — disassembly (range dump `pg/range1`): the constructor writes the
two bytes as immediates in its offset-ordered initialisation run, between `+0xa3c` and `+0xa42`.

### 1.2 The gender/ethnicity string tables (dump `pg/ptrs1`) — CONFIRMED

0x01160028 is a pointer array: `[0]` "male", `[1]` "female", `[2]` "asian", `[3]` "black",
`[4]` "hispanic", `[5]` "white", then zero. So the gender table is the two entries at 0x01160028 and
the ethnicity table the four at 0x01160030 (asian 0, black 1, hispanic 2, white 3), overlapping in
one array. The constructor's literal values are both **outside** their table's valid range: gender 3
would read "black" through 0x01160028 and ethnicity 5 would read a null pointer through 0x01160030.
They are "not assigned yet" sentinels, not defaults that mean anything (see §1.6 on 3).

### 1.3 The save-game load writer, 0x00b97510 (dump `pg/func1`) — CONFIRMED

Called from 0x00b97c90 (entry 1's load routine, `spec-save-format.md` §10) and 0x00b97740. It takes
the current player (0x009da4e0), copies the snapshot record's `+0x203c` into `+0xa41` and `+0x203d`
into `+0xa40`, then calls 0x00831930(gender, ethnicity) and passes the result to 0x00836d90 (preset,
0, player, -1, -1, 1, 0, 0), then restores the morph sliders, worn items, selection bytes and the
remaining appearance ids from the same record. So on a save load the two bytes come straight from
the save file and then select a preset.

### 1.4 The preset lookup 0x00831930 and the preset apply 0x00836d90 (dump `pg/func1`) — CONFIRMED

- 0x00831930(gender, ethnicity) walks a table of 0x64-byte (100-byte) records whose base pointer is
  the global 0x02300858 and count 0x0230085c, and returns the first record whose byte `+0x5d` equals
  `gender` and byte `+0x5c` equals `ethnicity`; null if none. That is exactly the
  `player_presets.xtbl` record `spec-tables-customization.md` §16.2 recovered (`+0x5c` Race, `+0x5d`
  Gender, 100-byte stride; its loader 0x00835f90).
- 0x00836d90(preset, character, ...) does nothing on a null preset. Otherwise it switches on the
  preset's gender byte `+0x5d`: 1 loads the female body item (`"female_body"`, `"cf_body.cmeshx"`,
  variant `"01 - Default"`), 0 the male branch; it applies hair, skin colour and the composite
  layers from the preset, and near the end **writes `+0xa41` := preset `+0x5d` and `+0xa40` :=
  preset `+0x5c`**, then records the (character, preset hair pointer) pair in a two-slot list at
  0x022ffe00 (count 0x02300880). Five callers: 0x00b97510 (save load), 0x00bcaef0, 0x006e5180,
  0x008373d0, 0x00837640 (the last three followed in §1.7).

### 1.5 Second record-shaped writer 0x00bcaef0 — CONFIRMED (shape); role settled in §1.11

Reached only through the thunk 0x00bc5630. It is the save-load routine of §1.3 with its source
replaced by a global buffer at 0x02944570 (0x02944570 + 0x203c = 0x029465ac, + 0x203d = 0x029465ad,
+0x2040 = 0x029465b0, +0x2044 = 0x029465b4 ...). It writes the two bytes, looks up and applies the
preset, then restores the rest of the look from the buffer. The buffer is filled only by 0x00bcaec0,
which zeroes 0x2264 bytes (the 0x2254-byte character record plus its 16-byte tail, the same pair the
save snapshot keeps at 0x900/0x2b54) and reads them from a stream; its one caller is 0x00bc5170 in
the network-session module. An earlier draft of this note guessed this was the character-gallery
"restore" step; §1.11 shows instead that it is the co-op-guest branch of game start.

### 1.6 The character-definition writer 0x0096ec80 — CONFIRMED

0x0096ec80 is referenced from 0x01171378, i.e. slot 1 of the character vtable 0x01171374 that the
constructor 0x00954340 installs — the per-class "read my properties from my 40-byte record" step
that `interp_named_object_resolution.md` §1.7 described (registry slot 11 calls it). Besides that
vtable entry it has two direct callers in the player module, 0x009d8a36 and 0x009e886c (undefined
code). It looks up a 4-byte property (descriptor 0x01309ddc) holding a character-definition pointer,
or else a string property (descriptor 0x01309dac) that it resolves through 0x00be3360 — the
character-definition registry lookup already cited in `spec-tables-traffic-ai.md` (CRC, `% 500`
bucket hash) — stores the definition at `+0xf0`, and **copies the definition's byte `+0x18` into
`+0xa41`**. It does not touch `+0xa40`. So an ordinary (NPC) character's gender is the gender field
of its character-table row (what that field holds, and that it is itself 3 for the player's row, is
settled in §1.12).

### 1.7 The two vocabularies, and why the constructor writes 3 and 5 (dump `pg/func4`) — CONFIRMED

- 0x00831fc0(text) — the `Gender` parser used by the `player_presets.xtbl` loader 0x00835f90 and the
  `Horde_Mode` loader 0x006ea230: case-insensitive compare against the two strings at 0x01160028;
  returns 0 for `male`, 1 for `female`, and **3 for anything else** (including absent).
- 0x00832000(text) — the `Race` parser, same two callers: compares against the four strings at
  0x01160030; returns 0 `asian`, 1 `black`, 2 `hispanic`, 3 `white`, and **5 for anything else**.

So the character constructor's literals (`+0xa41` := 3, `+0xa40` := 5) are exactly these parsers'
"unrecognised / not specified" results. A freshly constructed character has gender "unspecified"
and ethnicity "unspecified" until something overwrites them. The gender value 3 is also the
"either" value `customization_default_items.xtbl` uses for an absent `Gender` (`spec-tables-
customization.md` §8), and the default-items walker (§1.10) treats 3 as "matches any gender".

### 1.8 The other callers of the preset apply — CONFIRMED (bodies), HIGH CONFIDENCE (roles)

- **0x008373d0 — "apply the default preset to the current player".** It calls 0x00836d90 with the
  global 0x02300854 as the preset and the current player (0x009da4e0) as the character, then applies
  the body-morph entry named `"male_athletic"` (0x00838d70 / 0x00838850). 0x02300854 is written only
  by the `player_presets.xtbl` loader 0x00835f90: it is the row whose `Default` is `yes`, falling back
  to the first loaded row if none is marked (`spec-tables-customization.md` §16.2 already records the
  fallback; xref dump `pg/xref4` confirms 0x00835f90 is the only writer). Its one caller is 0x00827010
  (§1.10).
- **0x00837640** — reached through a data reference from 0x008377d0 and opens with the Lua stack
  helpers (0x00dfde50 get-top, 0x00dfe210 to-string): a Lua-bound function that takes a preset name,
  finds the `player_presets.xtbl` row by `Name` hash, and applies it to the current player with a
  regional-preset category/preset pair. A script-driven preset change, not creation.
- **0x006e5180** — applies a preset chosen by (gender `+0x45`, race `+0x44`) bytes of a 0x184-byte
  record from a table at 0x014f4410 (count 0x014f4418), then wardrobe items from the same record. It
  sits beside the `Horde_Mode` loader 0x006ea230 (which calls the same two parsers), so HIGH
  CONFIDENCE it is the horde/wave mode's character-look selection. Not single-player creation.

### 1.9 How the player object is created (dumps `pg/xref2`, `pg/func2`, `pg/func3`, `pg/func5`) — CONFIRMED

- The current-player global 0x0262edfc (zero-fill `.data`, no static initialiser) has exactly three
  writers: 0x009e1fc0 (create), 0x009e3bf0 (debug character swap) and 0x009da150 (clear to null).
- **0x009e1fc0 is the single-player player creation.** Its one caller 0x00941c90 is a fixed start-up
  sequence (it calls 0x009e1fc0 among half a dozen subsystem initialisers and ends by fetching the
  player). 0x009e1fc0 marks both player slots at 0x0130b050/0x0130b058 free, builds a 40-byte
  registry record with 0x009de780(record, definition = null, flag = 1), optionally adds a position
  when a network session supplies one, and creates the object through the registry's generic
  create-from-record method 0x00456e30 with **class index 0x21** (`interp_named_object_resolution.md`
  §2.5). The result is stored straight into 0x0262edfc.
- **0x009de780 (the player-record builder)**: with a null definition it claims the first free player
  slot (0x009de3b0 on 0x0130b048 → 0 or 1) and takes the character definition from the per-slot table
  at 0x0263ae08 (8-byte entries; slot 0 at 0x0263ae08, slot 1 at 0x0263ae10). It then calls
  0x0096eb30, which adds the 4-byte `human_preset` property (descriptor 0x01309ddc, name string
  `"human_preset"`) holding that definition pointer — plus, if given, a `human_creation_flags`
  property — and finally adds the spawn position/orientation and a one-byte property (descriptor
  0x0130b040) set from the flag argument.
- **The per-slot definition table is filled with `"StyleTest_PC"` for both slots.** Its writers
  (`pg/xref4`) are 0x009dc780 — called once from the game start-up routine 0x005d25f0, which also
  marks the player slots free and then sets up the `"Single Player"` session — and the two resets
  0x009da270 and 0x009df430: each looks up the literal `"styletest_pc"` through the character
  registry 0x00be3360 and stores the result in both slots. The only other writer is the debug branch
  inside 0x009e1fc0 (gated by the byte 0x024d4462), which uses `"styletest_pc_mp2"` instead.
- **The player class's record-reading step calls the shared character one.** The two direct
  callers of 0x0096ec80 (§1.6) are undefined-code bodies in the player module (around 0x009d8a36 and
  0x009e886c — HIGH CONFIDENCE the class-0x21 vtable slot 1 and a sibling); each calls 0x0096ec80 with
  the record and continues with player-specific set-up on success. So when the player object is
  created, 0x0096ec80 runs and sets `+0xa41` := (StyleTest_PC definition) `+0x18`. It does not set
  `+0xa40`, which keeps the constructor's 5.

### 1.10 The default-look routine 0x00827010 (dump `pg/func5`) — CONFIRMED

0x00827010(character, ?, flag) first calls 0x008373d0 — so **the default preset's gender and race
are written into `+0xa41`/`+0xa40`** — and then walks the `customization_default_items.xtbl` table
(base 0x022d60f4, count 0x022d60f8; record byte `+0x4c` = gender, `+0x4d` = race), equipping every
default item whose gender is 3 ("either") or equals the character's `+0xa41` and whose race equals
`+0xa40`. Its two callers are 0x00b97510 (save load, only when the saved worn-item count at record
`+0x438` is zero) and 0x00708330 (§1.11).

### 1.11 The order of events at game start: 0x00708330 (dumps `pg/func6`, `pg/func8`, `pg/func10`) — CONFIRMED

0x00708330 is the "level is up, start the game" step `spec-save-format.md` §11.1 already names (row
3a: it calls the save gate when the game-flow object's byte `+0xd4` has bit 0x4 set, "otherwise it
starts a fresh game"). Read in full this pass, in order:

1. If no game is running yet (global 0x01503eb4 zero) it creates a `"default_start"` navpoint
   (0x008eb580, the named-navpoint builder of `interp_named_object_resolution.md` §2.6) at a fixed
   position chosen by whether the zone name starts with `"sr3_city"`.
2. A long fixed list of subsystem initialisers, among them **0x0091f550, whose callee 0x00941c90 is
   the player-creation sequence of §1.9**. So the player object — with `+0xa41` = 3 and `+0xa40` = 5
   (§1.12) — exists from this point on.
3. Then exactly one of three look sources is applied:
   - **Loading a save** (`+0xd4` bit 0x4): 0x00b94b90(buffer, 1) — the save gate and per-entry
     driver; entry 1's load reaches 0x00b97510 (§1.3), which writes both bytes from the snapshot's
     character record. (The in-memory checkpoint restore 0x006d3780 enters the same gate with the
     "full" flag 0; whether entry 1's load re-applies the look on that path was not checked.)
   - **Fresh game, solo or host**: unless 0x00bc5610 is true or 0x00707380 is true, it calls
     **0x00827010(player, 0, 0)** — the default-look routine of §1.10, which applies the
     `player_presets.xtbl` default row (writing both bytes) and the matching default clothing.
   - **Co-op guest**: after the above, if 0x00bc5610 is true it calls 0x00bc5630 → 0x00bcaef0 (§1.5),
     which writes both bytes from the 0x2264-byte character record received through the session.
4. More subsystem start-up follows; mission scripts run only after this step.

0x00bc5610 is "the session state global 0x029443bc is 2 or 3" — the state block of the network
session module (0x00bcxxxx); the existing exe notes (`interp_gdhw.md`) already use it as the "not the
host" test of the co-op clock. HIGH CONFIDENCE: true = this machine joined someone else's game.

0x00707380 is a narrow story-progress predicate, read but not fully interpreted: it is false unless
the zone name starts with `"sr3_city"`, the constant byte 0x011239d0 (1 in the file) or 0x011239d1 (0)
is set, the byte 0x014f3d34 is clear, the game-flow `+0xd4` byte has bit 0x1 or 0x4, and 0x006d74b0
holds — which (host or solo only) looks up the mission object `"m01"` through the mission-kind
resolver 0x005f4c30 and tests completion of `"mm_m1_5"`, `"m02"`, `"m03"`, `"m22"`, `"m23"`, `"m24"`
(0x006d6f30 / 0x006d6ff0). When it holds, the fresh-game branch skips the default look. What game
situation that is (and who sets the look in that case) is OPEN (§4); it does not affect a save load.

### 1.12 What the player's character-table row contributes: nothing — CONFIRMED (code + data)

- The character registry 0x00be3360 (CRC of the lower-cased name, mixed with 0x00dab2b0 into 500
  buckets, table object 0x029a5e00, pointer array 0x029a7578) is filled by the `character.xtbl` row
  reader at 0x00be3a50 (undefined code; found through its use of the `"Gender"` string 0x0113cf80,
  dump `pg/str1`, read with range mode, dump `pg/range4`). Each row is built into a 0x54-byte slot of
  a static array at 0x0299b8a0 (cap 0x1d4 = 468 rows) after the reset 0x00be1910, which sets **slot
  `+0x18` := 3** and **`+0x19` := 5** — the same two "unspecified" values as the parsers of §1.7.
- `+0x18` is then written once, and only if the row has a `Filters` element containing `Gender`: it
  becomes 0 when the text's first character is an upper-case `M`, else 1 (a one-character test, not
  the 0x00831fc0 vocabulary). A raw scan for the slot shows no other writer (dump `pg/xref6`).
- **Real data** (`character.xtbl`, `misc_tables.vpp_pc` entry 70): the player's row
  `StyleTest_PC` (category `Player_Character`; flags `player_character`, `no_ambient_spawning`) has no
  `Filters` element and no `Gender`; nor does the debug row `StyleTest_PC_MP2`. So the definition's
  `+0x18` stays 3, and when 0x0096ec80 copies it into the new player's `+0xa41` (§1.6, §1.9) the value
  is still **3**. Ethnicity is never copied from the character row; `+0xa40` stays the constructor's
  5. (For comparison, the file's `Gender` elements read `Male` 314 times and `Female` 143 times, on
  NPC rows.)
- The `character_definitions.xtbl` row of the same name (`Anim_Set` `PLYM` / `PLYF`, rig
  `cm_body.rigx`, persona `Player_WM`) has no gender field either; it is not what 0x00be3360 returns
  and is not involved in `+0xa41`.

So, **between creation and the look step of §1.11, the real player object holds `+0xa41` = 3 and
`+0xa40` = 5** — neither is a valid index into the gender/ethnicity tables (§1.2).

### 1.13 Other writers, for completeness — CONFIRMED (bodies), HIGH CONFIDENCE (roles)

- **`pcr_set_identity` (0x00830fa0)** — registered in the `pcr_*` Lua table built by 0x008377d0
  (the name/function pair sits at 0x00837b01/0x00837b0c, dump `pg/range5`). One string argument:
  compared case-insensitively against `"male"`; the result is 0 for `male` and **1 for any other
  string**; if it differs from the current player's `+0xa41` it writes it, refreshes the body
  (0x00827960) and re-syncs clothing categories 0xa, 0xb, 0x15, 0x16, 0x17 (0x0082e210). Does not
  touch `+0xa40`. Not previously catalogued in `spec-lua-api-behaviour.md`.
- **`pcr_apply_preset` (0x00837640)** — already §23.8 of that spec; writes both bytes through
  0x00836d90 from the named preset.
- **0x0089310d** (undefined code; registered at 0x0089366a as a handler for a deferred message
  through 0x00894610, dump `pg/range5`) — for an object found by a 16-bit network id (0x00837bc0)
  that is **not** the local player, copies message bytes `+0x406` into `+0xa41` and `+0x405` into
  `+0xa40`, then rebuilds the look. HIGH CONFIDENCE: co-op replication of the *remote* player's
  appearance — this is how a host's `#PLAYER2#` object gets the joining player's gender.
- **0x006e5180** — horde-mode look (§1.8).

### 1.14 Real data: the shipped preset table and the owner's saves — CONFIRMED — data

- **`player_presets.xtbl`** (`misc_tables.vpp_pc` entry 212, 8 rows, in file order): `male_asian`,
  `male_black`, `male_hispanic`, `male_white`, `female_asian`, `female_hispanic`, `female_black`,
  `female_white`. Exactly one row has `Default` = `yes`: **`male_white`** (`Race` white = 3, `Gender`
  male = 0). So the engine default the fresh-game branch applies is gender 0, ethnicity 3.
- **The owner's saves** (script `pg/savegender.py`, read-only, same four sample folders and the same
  de-duplication as `tools/harnesses/save_region_map.py`; 16 distinct snapshots). Snapshot character
  record = file offset 0x900; byte `+0x203c` (gender) and `+0x203d` (ethnicity); the worn-item list
  at `+0x438` was searched for the two body items by name hash (`male_body`, `female_body`):

  | gender byte | ethnicity byte | body item worn | snapshots |
  |---|---|---|---|
  | 0 | 3 | `male_body` | 3 (the level-0 new-game save dated 09.14.26; two old saves dated 2011 and 2013) |
  | 1 | 3 | `female_body` | 13 (the owner's current play-through, levels 22 to 50) |

  The gender byte agrees with the worn body item in **16/16**, which settles the
  `spec-save-format.md` §10.4 HYPOTHESIS: `+0x203c` is the gender (0 male, 1 female) and `+0x203d` the
  ethnicity. No sample holds 3 for gender or 5 for ethnicity. The new-game save holds exactly the
  default preset's pair (0, 3), as the fresh-game branch of §1.11 predicts. The owner's own character
  is female and white: in this play-through the real engine answers `#PLAYER1#`'s gender as 1.

---

## 2. Direct answer

**Where does the real player's gender come from at creation?** From nowhere meaningful. Creating the
player (0x009e1fc0, class 0x21, via the registry's generic create-from-record 0x00456e30) leaves
`+0xa41` = **3** and `+0xa40` = **5**: the character constructor 0x00954340 writes those literals, and
the player's character-table row `StyleTest_PC` carries no `Gender`, so the record-reading step
0x0096ec80 copies the row's own "unspecified" 3 back in (§1.6, §1.9, §1.12). Both values are the
engine's explicit "not specified" sentinels — the exact results of its gender/race parsers for an
unrecognised string (§1.7) — and are outside both lookup tables (§1.2). **CONFIRMED — disassembly +
data.**

**The first real value is written in the same game-start step, before any mission script runs**
(0x00708330, §1.11), from exactly one of three sources:

| situation | writer | source of `+0xa41` / `+0xa40` | value |
|---|---|---|---|
| loading a save | 0x00b97510 | **the save file**: snapshot character record (file 0x900) bytes `+0x203c` / `+0x203d` | whatever was saved; the owner's current saves: **1 / 3** (female, white) |
| fresh game, solo or host | 0x00827010 → 0x008373d0 → 0x00836d90 | **customization table**: the `player_presets.xtbl` row marked `Default` = `yes` (global 0x02300854; first row if none is marked) | shipped data: `male_white` = **0 / 3** |
| joining someone's co-op game (guest) | 0x00bcaef0 | the guest's 0x2264-byte character record delivered through the session (same layout as the save record) | the guest's own look |

So the precise answer to the three candidates: **not an engine default** (the engine's only built-in
value is the 3/5 "unspecified" pair); **the save data** whenever a save is loaded — which is the case
for any mid-story mission such as `m21` — and **the customization table** (`player_presets.xtbl`'s
default row, not `character.xtbl` and not `character_definitions.xtbl`) only for a brand-new game.
The save and the preset table are the same kind of value: the save stores the two bytes the player
last had, which themselves came from a preset (default, chosen in the creator, or set by
`pcr_set_identity` / `pcr_apply_preset`).

**`#PLAYER2#`.** On the host, the remote player's object takes its two bytes from the network
appearance message (0x0089310d, bytes `+0x406` / `+0x405`, §1.13) — i.e. from the guest machine's own
record. On the guest, its own player takes them from the session-delivered record (0x00bcaef0). In
neither case is there a local default. HIGH CONFIDENCE (role of both co-op paths).

## 3. What a host should do

1. **Construct with 3 / 5**, as the engine does, but never let a script read the object in that state.
   The engine never exposes it: by the time missions run, step 3 of §1.11 has replaced it.
2. **Driving a mission from a save (the normal case for `m21`):** copy the two bytes from the save
   snapshot — `+0xa41` := file byte `0x900 + 0x203c` (absolute `0x293C`), `+0xa40` := file byte
   `0x900 + 0x203d` (absolute `0x293D`). For the owner's saves this gives **1** (female) and **3**
   (white). A faithful host also selects the preset row whose `Gender`/`Race` match (0x00831930) and
   applies its body, but only the two bytes matter to `character_get_gender` and
   `pcr_get_player_preset`.
3. **No save available (fresh game):** use the `player_presets.xtbl` row whose `Default` is `yes`
   (fall back to the first row) — in the shipped data **gender 0 (male), ethnicity 3 (white)**. This
   is the real engine's fresh-game value and matches the owner's level-0 new-game save byte for byte.
4. **`#PLAYER2#` in a co-op run:** take the joining player's own pair; with no second machine, a test
   host can reuse the rule of item 3 for it, but that is a host choice, not engine behaviour.
5. **Do not hand out the raw 3.** `character_get_gender` would return 3, and `pcr_get_player_preset`
   would index the gender table with 3 and read the ethnicity string `"black"` (and with ethnicity 5
   a null pointer) — garbage, not a value any real run produces after game start.

## 4. What is still open

1. **0x00707380's exact meaning** (§1.11): a fresh-game branch condition in `sr3_city`, tied to
   progress of `m01`, `mm_m1_5`, `m02`, `m03`, `m22`, `m23`, `m24`, under which the default look is
   *not* applied at game start. Who sets the bytes in that situation was not traced (the candidates
   are the character creator's Lua calls `pcr_set_identity` / `pcr_apply_preset`). It cannot apply to
   a save load, so it does not change items 2 or 3 of §3 for `m21`.
2. **The class-0x21 vtable**: the two player-module bodies that call 0x0096ec80 (around 0x009d8a36
   and 0x009e886c) are undefined code; that one of them is the player class's slot 1 is HIGH
   CONFIDENCE from their contents (player slots 0x0130b050, the current-player global), not from a
   read of the class descriptor's vtable.
3. **The co-op transport**: that 0x00bc5170 (filler of the guest buffer) and the 0x0089310d message
   handler are the co-op join/appearance messages is HIGH CONFIDENCE from the module and the
   "not the local player" test; the message framing was not read.
4. **Ethnicity's other writers**: `pcr_set_identity` changes gender only, so a creator that changes
   race must go through a preset; that `+0xa40` = 3 in all 16 owner saves is consistent with this but
   does not prove how the creator sets race.

## 5. Corrections and additions for the spec (to transcribe)

- `spec-lua-api-behaviour.md` §16.8 note "their producers remain OPEN": now closed — producers are
  0x00954340 (3/5 sentinels), 0x0096ec80 (character row, gender only), 0x00836d90 (preset apply,
  callers 0x00b97510, 0x00bcaef0, 0x008373d0, 0x00837640, 0x006e5180), 0x00b97510 / 0x00bcaef0
  (record loads), `pcr_set_identity` 0x00830fa0 (gender only), and the co-op handler at 0x0089310d.
  The §16.8 "two adjacent tables" reading is CONFIRMED by the parsers 0x00831fc0 (2 entries, else 3)
  and 0x00832000 (4 entries, else 5).
- `spec-lua-api-behaviour.md`: new entry `pcr_set_identity` (0x00830fa0), §1.13.
- `spec-save-format.md` §10.4, record `+0x203c` / `+0x203d`: HYPOTHESIS → **CONFIRMED (code + 16/16
  data)**: gender (0 male, 1 female) and ethnicity (0 asian, 1 black, 2 hispanic, 3 white), written by
  0x00b98f00 from player `+0xa41` / `+0xa40` and loaded by 0x00b97510.
- `spec-tables-customization.md` §16.2: the `Default` row is applied to the player on a fresh game
  by 0x008373d0 (via 0x00827010 ← 0x00708330); the `Gender`/`Race` helper vocabularies (OPEN there)
  are male 0 / female 1 / other 3 and asian 0 / black 1 / hispanic 2 / white 3 / other 5. §13.3
  (`character.xtbl`, "schema OPEN"): row reader 0x00be3a50, 0x54-byte rows at 0x0299b8a0 (cap 468),
  reset 0x00be1910; `Filters` > `Gender` → `+0x18` (first letter `M` → 0, else 1; absent 3).

## 6. Cleanroom self-check

The house pattern for decompiler auto-names (`iVar…`, `uVar…`, `local_…`, `param_…`, `in_…`,
`undefined…`, `LAB_…` and the rest of the list in the brief) was run over this file with `grep -E`
after writing; result recorded in the final line below. Addresses appear only as `0xXXXXXXXX`; no
pseudocode or decompiler text is reproduced; save and table contents are described, not dumped.

Cleanroom grep result (2026-10-02, whole file, after the last edit): **0 matches**; a second check for
`FUN_` / `DAT_` prefixes matches only this sentence.

Dumps for this pass are in the session scratchpad under `pg/` (`xref1`–`xref8`, `func1`–`func10`,
`range1`–`range5`, `ptrs1`–`ptrs3`, `str1`, `calls1`; data scripts `savegender.py`, `getpresets.py`,
`rdstr.py`). The private Ghidra copy `tools/gp_gender` was deleted after the pass.
