# Character spawn-time state: max-hit-points (`+0x1cac` / `+0x1cb0`) and the ignore-AI flag (`+0x2bc`)

Team A, 2026-10-02. Investigative pass against the real executable (private copy of the local Ghidra
project at `tools/gp_spawnstate`, program `SaintsRowTheThird.exe`, headless `-readOnly -noanalysis`,
`ghidra/CrreishDump.java`). Dump output lives in this session's scratchpad (`ss/` sub-folders, plus
the `cs/` and `charspawn/` folders left by the two earlier, rate-limit-killed attempts at this same
brief, which were re-read rather than re-run); nothing is committed and everything below is prose.

**The question.** A partner team's mission-drive trace reaches an `OPEN_STATE` on two character fields
in 98 missions, because the spec only documents their Lua *setters*: `+0x1cac` (current max-hit-points
cap, int; written by `set_max_hit_points`' delegate `0x0094d1f0`) and `+0x2bc` (the ignore-AI flag
byte; flipped through `0x004e2050` by `set_ignore_ai_flag`, `0x00a5e290`). What value do these fields
hold when a character comes into existence, before any script has called a setter? And is that value
an engine constant, a data-table value, or something carried by the character's zone placement record?

**Standing restriction, kept throughout.** The `.czn_pc` object-placement/property-stream interior
(top-level record `0x2234`, "Game objects, as placed in the WE") is parked: no real `.czn_pc` bytes
were opened, requested or printed. Code that *reads* that stream is in scope. If a value turns out to
be a field of a placement record, that is reported as the answer and the trail stops there.

Labels follow the house style: **CONFIRMED — disassembly** = read in a listed instruction stream of
this pass's dumps; **HIGH CONFIDENCE** = follows from a dumped instruction or reference list but the
body it points at was not dumped; **HYPOTHESIS** = plausible, not settled; **OPEN** = not settled,
collected at the end.

---

## 0. Starting point (from prior passes, not re-derived here)

- `spec-lua-api-behaviour.md` §7.12 / §7.31 / cross-cutting finding 7: `+0x1cac` = current max cap
  (int), `+0x1cb8` = current hit points (float); `0x0094d1f0` sets `+0x1cac` to `+0x1cb0` plus its
  argument; `0x0094cfc0` clamps and writes current HP. §3.4: `0x00a5e290` tests bit 0x02 of `+0x2bc`
  and calls `0x004e2050`, with an OPEN note on which `this` that helper runs against.
- `interp_named_object_resolution.md` §1.6–§2.5: every registry object, a character included, is
  constructed through a class descriptor's slot 0 and then initialised by registry virtual slot 11
  (`0x00456d80` / `0x008544b0`), which calls the object's own vtable slot 1 with its 40-byte record
  ("read my properties from the record tail"). Records come from the zone `0x2234` batch, from
  vehicle cover-node batches, or are built in memory by engine code (173 sites calling `0x00456e30`).

---

## 1. Investigation trail

(Appended incrementally below as each dump is read.)

### 1.1 The character object's constructor: `0x00954340`, and what it writes into the three fields — CONFIRMED — disassembly

The full-reference scans for the field offsets (dumps `cs/xref1`, displacement scans for `0x1cac`,
`0x1cb0`, `0x1cb8`, `0x2bc`) give, for `+0x1cac`, 91 uses in 69 functions of which only four
*write*: `0x00942480`, `0x0094d1f0`, `0x0094d380`, and `0x00954340`. `0x00954340` is a 5,400-byte
constructor body: it calls a base-class constructor (`0x0091f8d0`), installs the vtable `0x01171374`,
zeroes EDI and sets EBX to all-ones at the top and then initialises hundreds of fields in offset
order. In the run covering the hit-point block it writes, in this order:

- `+0x1ca4` = 0.0, `+0x1ca8` = the byte 9;
- **`+0x1cac` = 0**, **`+0x1cb0` = 0**, `+0x1cb4` = 0 (all from the zeroed EDI);
- **`+0x1cb8` = 0.0** (current hit points);
- `+0x1cbc` / `+0x1cc0` = a float constant read from `0x01117a4c`.

So a freshly constructed character has max HP 0, bonus 0 and current HP 0. That is a placeholder,
not the spawn value: §1.4 shows the real spawn-time value is written later by a separate routine.

The same constructor builds a sub-object in place at **`+0x2b0`** by calling `0x004e46c0` with
`ECX = this + 0x2b0` (instruction at `0x009543fd`/`0x0095440d`). That sub-object is the per-character
AI-data block (§1.3).

Who calls `0x00954340`: two derived-class constructors, `0x009d4700` (installs vtable `0x011758d4`
and initialises fields `+0x1d94`..`+0x1e58`, including **`+0x1da8` = 0**, which matters in §1.7–§1.8) and
`0x009de970`. `0x009d4700`'s one caller is `0x009d9a10`. So "character" is at least two concrete
classes over one shared base constructor; both inherit the zeros above.

### 1.2 Correction to the existing spec reading of `+0x1cb0`: it is the max-HP *bonus*, not a base — CONFIRMED — disassembly

`0x0094d380` (dump `cs/func1`), the fourth writer, is a twin of `0x0094d1f0`. Both open a network
record tagged `"human"` when the local side is not authoritative; the twin's second tag is the literal
**`"max_hit_point_bonus"`** (string at `0x01167ce0`, exactly two users: this function and the network
receive routine `0x008ae650`), where `0x0094d1f0`'s is `"max_hit_points"`. When authoritative, the twin
adds the *difference* between the new argument and the old `+0x1cb0` onto `+0x1cac`, stores the
argument into `+0x1cb0`, and clamps current HP (`+0x1cb8`) down to the new cap.

Read together: `+0x1cb0` is a **bonus** that rides on top of the max; `0x0094d1f0` (the Lua
`set_max_hit_points` delegate) sets `+0x1cac` = *argument + current bonus*, and `0x0094d380` changes
the bonus while keeping the cap consistent. The Lua argument is therefore the **un-bonused max**, not a
delta in the gameplay sense; the existing spec wording ("a base value plus the argument — the Lua
argument is a delta onto a base") has the two roles swapped. The arithmetic in the spec is right; the
label on `+0x1cb0` is not. At construction both are 0 (§1.1), so with no bonus applied the two
readings coincide numerically. Callers of `0x0094d380`: `0x0097e480`, `0x0097e570`, `0x008ae650`
(network receive), `0x0071e510` — not read this pass.

The other `+0x1cb0` writers in the scan (`0x00895370`, `0x008a9960`, `0x00a82fe0`, and one
undefined-code site at `0x00a7eab7`) act on objects with vehicle-only fields (`+0xbd0`, the vehicle
module address range); `+0x1cb0` is a different field there. Not part of the character story.

### 1.3 The ignore-AI flag: whose `+0xc` `0x004e2050` writes, and the AI-data constructor — CONFIRMED — disassembly

This settles the OPEN note in spec §3.4. `0x00a5e290` (dump `cs/func1`) resolves the character, reads
the byte at character `+0x2bc`, shifts right by one and masks with 1 — i.e. tests **mask 0x02** — and
compares that with the requested boolean; on a difference it loads **`ECX = character + 0x2b0`** and
calls `0x004e2050(0 or 1)`. `0x004e2050` (dump `cs/func1`), when authoritative, replaces bit 0x02 of
the byte at `this + 0xc` with the argument; when not, it sends a network record tagged
`"human_ai_data"` / `"ai_force_flagsignore_ai"` keyed by the handle the AI block keeps at its own
`+0x610`/`+0x614`. So `this + 0xc` = character `+0x2b0 + 0xc` = **character `+0x2bc`, mask 0x02**:
the read and the write are the same bit. Both of the spec's open readings ("bit 1" as an index vs.
a mask) resolve to: bit index 1, mask `0x02`.

The AI-data block constructor `0x004e46c0` (called only from the character constructor, dump
`ss/func1`) fills a long run of fields with all-ones, two 0x58-byte sub-blocks, a nested
sub-object at its own `+0x260` (constructed by `0x004fd930` — whose own `+0x2bc` write is *relative
to that nested block*, so it is the AI block's `+0x51c`, not character `+0x2bc`), the handle pair at
`+0x610`/`+0x614` from the null-handle constant at `0x01115d50`, and then calls `0x004dde30` on the
whole block. It does **not** itself write its `+0x0c` byte; whether `0x004dde30` does is read in
§1.5.

Other writers of character `+0x2bc` found by the displacement scan (excluding stack-frame hits,
which are `[ESP + 0x2bc]` locals in unrelated functions, and the unrelated parsers `0x004c1020` and
`0x004fd930`): `0x004ea530` (writes a 16-bit value read from a network bit-stream — a replication
receive, caller `0x0089e580`), `0x00635d40` (ORs in mask 0x04, a different bit), and two
undefined-code sites at `0x00a3355b`/`0x00a33567` in the Lua binding module (AND-masks; §1.9).

### 1.4 The character's own "initialise from record" step: base vtable slot 1, `0x0096ec80` — CONFIRMED — disassembly

The base character vtable `0x01171374` (dump `ss/ptrs1`) has `0x0096ec80` in slot 1 (`+0x04`) — the
slot the registry's initialise-from-record step (slot 11, `interp_named_object_resolution.md` §1.7)
calls with the object's 40-byte record. The derived vtable `0x011758d4` overrides slot 1 with
`0x009d8930` (§1.7), and `0x0096ec80` also has two direct callers at `0x009d8a36` and `0x009e886c` —
consistent with derived overrides chaining to the base. Read in full (dump `ss/func1`), in order:

1. Property lookups on the record (through `0x00456590`, the record's find-property routine): a 4-byte
   **`human_preset`** (a pointer), else the string **`human_preset_name`** passed to `0x00be3360`
   (a name → preset lookup in the character-table module). The result is stored at **character
   `+0xf0`** (the preset pointer every later step reads); preset `+0x08` is copied to `+0xf4`.
   Further properties read here: `human_creation_flags` (into a flag byte at `+0x1928`),
   `human_server_create`, `human_scale`, `human_variant_checksum`, `human_persona` /
   `human_persona_name`, `npc_script_ptr`. None of them is a hit-point or AI-ignore value.
2. `+0x1ca8` (the byte the constructor set to 9) is taken from preset `+0x24`, or for one class
   family from another object's `+0x1ca8`.
3. **The spawn-time max-HP computation.** A level index is chosen — `0x0096e830()` for ordinary
   classes, 0 for the class family whose descriptor row (`0x02cc9900[class] + 10`) has bit 0x02 — and
   handed to `0x00942290`. Then `0x00be4830(preset+0x28, levelIndex)` returns a row of a global
   stat table (§1.6); `0x009445f0(character)` returns a float multiplier (§1.6); the row's
   **signed 16-bit value at row `+0x0a`** is multiplied by that multiplier and rounded to an integer
   (`0x00ea2560`, the CRT float-to-int). That integer is passed to **`0x0094d1f0` with its "force"
   argument = 1**, i.e. the authoritative branch is taken unconditionally (no network record), so
   **`+0x1cac` = `+0x1cb0` (still 0 from the constructor) + the computed value**. Immediately after,
   **`+0x1cb8` (current HP) is set to the same value as a float** — a character spawns at full health.
   The same row's 16-bit value at `+0x0e` goes to `+0x1320` (int) and `+0x1324` (float).
4. `0x004e5a50` is then called with `ECX = character + 0x2b0` (the AI block), arguments
   `(character, 0)` — the AI block's per-spawn initialiser, read in §1.5.

So for a character created from a record — zone-placed or built in memory — max HP is **table-
sourced**: it is a 16-bit value from a per-preset, per-level stat row, scaled by a multiplier, and
the bonus `+0x1cb0` is 0. It is not a hard-coded engine constant and not a property of the
character's own record (the record only selects the preset).

### 1.5 The AI block at spawn: `0x004dde30` clears the ignore-AI bit, `0x004e5a50` may set it — CONFIRMED — disassembly

- `0x004dde30` (dump `ss/func2`), called from the AI-block constructor `0x004e46c0` and from
  `0x004e5a50` when its mode argument is non-zero, zeroes the AI block's first three dwords and
  **stores a 16-bit 0 at AI block `+0x0c`** — i.e. character `+0x2bc` and `+0x2bd` are both cleared.
  So at construction the ignore-AI bit (mask 0x02) is **0**, along with every other bit of that byte.
- `0x004e5a50(character, mode)` (dump `ss/func2`), `this` = AI block: if mode is non-zero it re-runs
  the clear above; it stores the character's own handle into AI `+0x610`/`+0x614`; then, **only if the
  character's class descriptor row has bit 0x04 at `+10` AND the character's `+0x1da8` pointer is
  non-null**, it copies a dozen bits from that `+0x1da8` object's flag bytes `+0xac`..`+0xaf` into
  the AI block's flag bytes `+0x08`..`+0x0c`. Among them: **AI `+0x0c` mask 0x02 (ignore-AI) := bit
  0x10 of the `+0x1da8` object's byte `+0xad`** (bit 12 of the 32-bit flag word at `+0xac`), and AI
  `+0x0c` mask 0x08 := bit 0x40 of byte `+0xaf`. With no `+0x1da8` object the ignore-AI bit is left
  as cleared. Mode 0 (the call from slot 1) additionally seeds AI `+0x64` from `0x00be5280(character)`
  and randomises AI `+0x09` mask 0x02 against a percentage from `0x00507940`; mode 2 copies `+0xf8`
  and `+0x1cac` into AI `+0x6d0`/`+0x6d4` (a snapshot used by the replication receive `0x004ea530`).

At slot-1 time the `+0x1da8` pointer is still the 0 the derived constructor wrote (§1.1), so the
slot-1 call leaves ignore-AI at 0; the bits are applied when a controller is attached (§1.7–§1.9).

### 1.6 The data table behind max HP: `spawn_info_ranks.xtbl` — CONFIRMED (lookup, offsets) / HIGH CONFIDENCE (preset field)

- `0x00be4830(rank, level)` (dump `ss/func2`), read in full: given a pointer to a 0x698-byte record,
  it returns that record unchanged if its dword at `+0x04` is 7, 6 or 2, or if its byte at `+0x08`
  already equals `level`; otherwise it scans the global array at **`0x029a91a0`** (stride 0x698,
  count at **`0x029a9118`**) for the first record with the same `+0x04` value and `+0x08` byte equal
  to `level`, falling back to the original record if none matches. CONFIRMED — disassembly.
- The only writer of that array and count is `0x00be4da0` (dump `ss/xref3`: the count is reset and
  bumped there, and the base address is used there and in the two lookups `0x00be4780` /
  `0x00be4830` plus `0x00be5280`). `spec-tables-progression.md` §10.6 already identifies
  `0x00be4da0` as the reader of **`spawn_info_ranks.xtbl`**, filling 0x698-byte `<Rank>` records at
  `0x029a91a0` with: `+0x00` name hash, **`+0x04` `type`** (Civilian, Gang, Homie, Law enforcement,
  Stag, National Guard, Other, Specialist, or −1), **`+0x08` `rank_numeric`**, and **`Hit_Points`,
  `Bleed_Out_Hit_Points`, `Knockdown_Points`, each clamped to 0x7fff, as 16-bit values at `+0x0a`,
  `+0x0c`, `+0x0e`**. That matches the code here exactly: slot 1 reads row `+0x0a` as the max-HP
  base and row `+0x0e` into `+0x1320`/`+0x1324` (knockdown points). The three types `0x00be4830`
  refuses to re-level (2, 6, 7) are Homie, Other and Specialist if the enum is numbered in the order
  the spec lists it (HIGH CONFIDENCE; the spec gives the names, not the numbers).
- The starting pointer is **preset `+0x28`**, the preset being the `character_definitions.xtbl`
  record selected by `human_preset` / `human_preset_name` (`0x00be3360` is the character-definition
  registry lookup, `spec-tables-traffic-ai.md` names it so). That `+0x28` holds a rank-record pointer
  is HIGH CONFIDENCE from use (it is the first argument of a rank lookup in three places), not from
  the definition reader `0x00be0630`, which this pass did not open. So the definition row names a
  default rank; the character's level index picks a same-type rank with that `rank_numeric`.
- The multiplier `0x009445f0(character)` (dump `ss/func2`): **1.0** unless `0x00853b30(character,
  0x22)` is true (a class-membership test on the object); then, if `+0x1ca8` (seeded from preset
  `+0x24`, §1.4) is 0, a float at `+0x20` of the object returned by `0x005f4a50()`, otherwise — only
  when `0x0052a230(character)` holds — a float at `+0x1c` of that same object. What `0x005f4a50`
  returns (a difficulty/game-settings block is the obvious guess) was not read: HYPOTHESIS.
- The level index for ordinary classes is `0x0096e830()`; what it reads (district, notoriety, player
  level...) was not traced this pass. OPEN, but only for the *choice of row*, not for the mechanism.

So: **max HP at spawn = round(multiplier × `Hit_Points` of the selected `spawn_info_ranks` row)**,
bonus 0, current HP = that same value. CONFIRMED — disassembly for the formula and the fields;
HIGH CONFIDENCE for preset `+0x28` being the definition's rank reference.

### 1.7 The second pass: derived slot 1 `0x009d8930` and the re-initialiser `0x009d7480` — CONFIRMED — disassembly

The derived character vtable `0x011758d4`'s slot 1, `0x009d8930` (dump `ss/func4`), calls the base
`0x0096ec80` first (at `0x009d8a36`), sets a few flag bits from preset `+0x48`, and near its end
looks up one more 4-byte record property (descriptor `0x0130aed0`, read through `0x005623c0`):

- **present** → it calls `0x00a33380` with `ECX` = the object that property points to and the
  character as argument (§1.8: this is the *script NPC* object binding itself to the character);
- **absent** → it calls **`0x009d7480` on the character** with arguments (1, 0).

`0x009d7480` (dump `ss/func1`, eight callers including the bind routine, `0x00a33090` — vtable slot
24 of the script-NPC class — and several gameplay modules) is a "reset this character to its
spawn state" routine. The parts that touch the two fields, in order:

1. Calls `0x004e5a50(character, 1)` with `ECX` = AI block (instruction `0x009d75fd`): the mode-1 path
   re-runs the AI-block clear (ignore-AI → 0) and then, if a controller is attached at `+0x1da8`,
   copies its flag bits in (§1.5), so **ignore-AI := the controller's flag bit, or 0 without one**.
2. If `+0x1da8` is null: **`+0x1cac` := round(multiplier × `Hit_Points`)** by the same
   `0x00be4830` / `0x009445f0` / `0x00ea2560` sequence as slot 1, written straight through the raw
   setter `0x00942480` (one instruction: store the pointed-to int into `+0x1cac`; no bonus added, no
   network record); `+0x1320` := the row's knockdown points.
3. If `+0x1da8` is non-null: **if the controller's dword at `+0xb4` is ≥ 1, `+0x1cac` := that value
   verbatim; otherwise the same table formula**. Likewise `+0x1320` := controller `+0xbc` if ≥ 1, else
   the row's knockdown points. It also copies controller `+0xc1`, `+0xd4`, `+0xdc`, uses `+0xc2`
   (personality, through `0x004e36b0`), `+0xe8` (into character `+0x318` if non-negative), `+0xec`
   (if non-negative, through `0x004e39b0`) and `+0xf0` (team byte into `+0x1ca8`, 9 = "use preset"),
   and maps flag bits from `+0xac`..`+0xb0` into character flag words `+0x1c98`, `+0x1e00`..`+0x1e02`.
4. Later it snapshots `+0x1324` from `+0x1320` and calls a long list of refresh helpers; `+0x1cb0`
   (bonus) and `+0x2bc` are not touched again.

`+0x1cb8` (current HP) is not rewritten by `0x009d7480` itself; it keeps the full-health value slot 1
wrote (§1.4). When the controller supplies a different cap, current HP may therefore differ from the
cap until something clamps or refills it — HYPOTHESIS that one of `0x009d7480`'s trailing helpers
(e.g. `0x0096e6e0`, run when the second argument is 1, as on the bind path) refills it; not read.

### 1.8 The `+0x1da8` object is a "script NPC": its own record supplies `script_npc_hp` and `script_npc_flags` — CONFIRMED — disassembly

Writers of character `+0x1da8` (dump `ss/xref2`, 43 uses): the two derived constructors (0), `0x009e35b0`
/ `0x009e38e0` (not read), `0x00a348f0` (0), the undefined-code site `0x00a334af` (0), and
**`0x00a33380`**, which stores its `this` into the character's `+0x1da8` and the character into its
own `+0x180` — a two-way bind. `this` there is an object of the class whose vtable is **`0x0117efec`**
(constructor `0x00a33600`, installed after base constructor `0x00a2bee0`; dump `ss/ptrs2`, `ss/func6`):
slot 1 = `0x00a35b00`, slot 21 = `0x00a350f0` (spawns its character through `0x00a34f00`), slot 23 =
`0x00a33480` (unbind), slot 24 = `0x00a33090` (re-run `0x009d7480` on its character), slot 25 =
`0x00a350c0` (class-checked forward to the bind). This is the same Lua-module object family
`interp_named_object_resolution.md` §2.7 met as "script interior" / "script mover"; its property
vocabulary (descriptor table at `0x0130c6c8`, dump `ss/ptrs4`) is `script_npc_*`.

**Its constructor defaults** (`0x00a33600`): `+0xb4`, `+0xb8`, `+0xbc` = −1; the 64-bit flag word at
`+0xac`/`+0xb0` = 0 except bit 0 of `+0xb0` = 1; `+0xa4` (preset) = 0; `+0xc2` (personality) = the
"none" byte at `0x011177e5`; `+0xe8`, `+0xa8` = a float constant; `+0xec` = another float constant;
`+0xf0` (team) = 9.

**Its slot 1, `0x00a35b00`** (dump `ss/func3`), reads its record's properties. If `script_npc_peer`
(4 bytes) names an existing character it just binds to it; otherwise:

| property (descriptor) | kind/size the code requires | stored at |
|---|---|---|
| `script_npc_char_preset` (`0x0130c6dc`) or `script_npc_char_preset_name` (`0x0130c6e4`, string → `0x00be3360`) | 4-byte pointer / string | `+0xa4` (no preset → slot 1 fails) |
| **`script_npc_hp`** (`0x0130c6f4`) | 4 bytes | **`+0xb4`** (only if present) |
| `script_npc_bleed_hp` (`0x0130c6fc`) | 4 bytes | `+0xb8` |
| `script_npc_knockdown_hp` (`0x0130c704`) | 4 bytes | `+0xbc` |
| `script_npc_hearing_distance` (`0x0130c71c`) | 4 bytes | `+0xe8` |
| `script_npc_leash` (`0x0130c724`) | 4 bytes | `+0xec` (default written as −1.0 just before the lookup) |
| `script_npc_team` (`0x0130c72c`) | enum via `0x008cdf80` | `+0xf0` |
| `script_npc_personality_idx` (`0x0130c73c`, 1 byte) or `script_npc_personality` (`0x0130c734`, string → `0x00507e60`) | | `+0xc2` |
| `script_npc_char_scale` (`0x0130c6ec`) | 4 bytes | `+0xa8` |
| `script_npc_items` / `script_npc_loots` (`0x0130c70c` / `0x0130c714`) | up to 4 × 4 bytes each | counts `+0xc0` / `+0xc1`, arrays `+0xc4` / `+0xd4` |
| **`script_npc_flags`** (`0x0130c74c`) | a list of hashes (kind 1) or one space-separated string (kind 0) | **64-bit mask at `+0xac`/`+0xb0`** (bit 0 of `+0xb0` cleared first) |

(`script_npc_force_sync`, `script_npc_cower_flee` are in the descriptor table but not read by this
routine.) The flag parser `0x00a33ed0` hashes each name with `0x00d9e740` and ORs in the 64-bit mask
`0x00a337d0` finds for that hash in the flag-name set at `0x027039d0`.

### 1.9 The flag name that drives ignore-AI is literally `ignore_ai`, mask 0x1000 — CONFIRMED — disassembly

The flag-name set at `0x027039d0` is filled once at start-up by `0x00a33ff0` (dump `ss/func5`; called
from a static initialiser at `0x01009105`). For each name it builds an 8-byte mask on the stack,
hashes the name and stores the mask in the set's slot for that hash. For the literal **`"ignore_ai"`**
(string `0x0117f1a8`, its only use) the code zeroes the low dword and then writes the byte 0x10 into
its second byte, the high dword staying 0: **mask = 0x00001000**, i.e. bit 0x10 of byte `+0xad` once
OR'd into the script NPC's flag word at `+0xac`. That is exactly the bit `0x004e5a50` copies into
AI `+0x0c` mask 0x02 (§1.5). Neighbouring entries in the same routine, for orientation:
`gunfire_invulnerable`, `fire_zombie`, `ignore_attacks`, `ignore_player_group`. So the chain is:

`script_npc_flags` property contains `ignore_ai` → script NPC `+0xac` bit 0x1000 → (bind →
`0x009d7480` → `0x004e5a50` mode 1) → character `+0x2bc` mask 0x02 = 1.

And the reverse: the script NPC's unbind slot `0x00a33480` (range dump `ss/range1`; this is the
undefined code holding the two `+0x2bc` AND-masks of §1.3) clears character `+0x1da8`, and on a
still-alive character ANDs `+0x2bc` with 0xfd and 0xfb — **ignore-AI (and the 0x04 bit) are cleared
when a character is released from its script NPC**.

### 1.10 Where script NPCs' records come from (code only) — CONFIRMED (in-memory producers) / HIGH CONFIDENCE (zone) / boundary

- **`commando_spawn`**'s record builder `0x00a33b80` (dump `ss/func4`; nine callers, including the Lua
  wrapper `0x00a46130` and several gameplay modules) adds only a generated name, `script_npc_char_preset`
  and an optional position. **No `script_npc_hp`, no `script_npc_flags`** — so a commando-spawned
  character keeps the constructor defaults: `+0xb4` = −1 → table max HP; flags 0 → ignore-AI 0.
- The script-NPC spawn routine `0x00a34f00` (slot 21) builds the *character's* record through
  `0x009d73f0(preset, position, orientation, class 7 or 8, creation flags)` and creates it with
  `0x00bb9070(record, 5)`; the creation flags it forwards come from its own `+0xad` bits 0x08/0x40 and
  `+0xb0` bit 0 — not the ignore-AI bit.
- The co-op/stream deserialiser `0x00a36a80` (its kind-0x27 target `0x00a354e0` sits beside this
  class's code — HYPOTHESIS that it rebuilds script-NPC records on the receiving side) and the
  mission-module callers of the bind (`0x00735940`, `0x0072c260`, `0x006d2fc0`) were not read this
  pass.
- The only **file-borne** producer of registry records found by `interp_named_object_resolution.md`
  is the zone's top-level `0x2234` record ("Game objects, as placed in the WE"). A script NPC placed
  in the world editor therefore reaches `0x00a35b00` with whatever `script_npc_hp` / `script_npc_flags`
  its placement record carries. **That record is the parked `.czn_pc` interior. This is where the
  trail stops**: whether a given mission character (e.g. a named boss) is a placed script NPC, and
  whether its record carries `script_npc_hp` or an `ignore_ai` flag, is zone *data* — no `.czn_pc`
  bytes were opened, requested or read.

---

## 2. Direct answer

**Spawn sequence, as the code runs it** (registry slot 11 → character slot 1):

1. Constructor `0x00954340`: `+0x1cac` = 0, `+0x1cb0` = 0, `+0x1cb8` = 0.0; AI block at `+0x2b0`
   constructed, its `+0x0c` word cleared by `0x004dde30` → `+0x2bc` = 0. Placeholders. (§1.1, §1.5)
2. Base slot 1 `0x0096ec80`: preset from `human_preset`/`human_preset_name`; level index into `+0xf8`;
   **`+0x1cac` = round(multiplier × `Hit_Points`)** via the forced branch of `0x0094d1f0` (bonus 0);
   `+0x1cb8` = the same value. (§1.4, §1.6)
3. Derived slot 1 `0x009d8930`: no `npc_script_ptr` → `0x009d7480` recomputes the same max HP and
   clears ignore-AI again; with `npc_script_ptr` → the script NPC binds and `0x009d7480` applies
   **its** `script_npc_hp` (if ≥ 1) and its `ignore_ai` flag. (§1.7–§1.9)

**`+0x1cac` (max HP cap) — HIGH CONFIDENCE table-sourced, CONFIRMED mechanism.**
- Default: **round(M × `Hit_Points`)**, where `Hit_Points` is the 16-bit field at `+0x0a` of a
  **`spawn_info_ranks.xtbl`** `<Rank>` record (array `0x029a91a0`, `spec-tables-progression.md`
  §10.6). The record is the one the character's `character_definitions.xtbl` preset points to at
  preset `+0x28` (HIGH CONFIDENCE from use), re-picked by `0x00be4830` to the record of the same
  `type` whose `rank_numeric` equals the character's level index (except types 2/6/7, used as-is).
  M is 1.0 unless the class test `0x00853b30(character, 0x22)` holds, in which case it is a float
  from the block `0x005f4a50()` returns (`+0x20` or `+0x1c`). CONFIRMED — disassembly for the formula.
- Override: a character bound to a **script NPC** whose record carries a `script_npc_hp` ≥ 1 gets
  that value verbatim. A script NPC without that property (its constructor default is −1) — for
  example every `commando_spawn` NPC — falls back to the table value. CONFIRMED — disassembly.
- `+0x1cb0` is 0 at spawn on every path (the only non-constructor writer is the bonus setter
  `0x0094d380`). It is the max-HP **bonus**, not a base (§1.2): the spec's label for it should be
  corrected; its arithmetic is right.
- `+0x1cb8` (current HP) starts equal to the table value (slot 1). Whether it is refilled to a
  `script_npc_hp` override when that differs is not settled (§1.7, HYPOTHESIS).

**`+0x2bc` mask 0x02 (ignore-AI) — CONFIRMED engine default 0, with one data-driven override.**
- Default: **0** (cleared by `0x004dde30` at construction and again in `0x009d7480`). Plain placed or
  engine-spawned characters with no script NPC attached spawn with ignore-AI off.
- Override: a character bound to a script NPC whose `script_npc_flags` include the name
  **`ignore_ai`** (mask 0x1000 in the script NPC's flag word) spawns with ignore-AI **on**. Releasing
  the character from its script NPC clears it again. CONFIRMED — disassembly.
- Spec §3.4's OPEN note resolves: `0x004e2050`'s `this` is character `+0x2b0`, so its `+0x0c` is
  character `+0x2bc`, and "bit 1" means mask 0x02.

**`.czn_pc` boundary — yes, reached, and stopped at.** For a mission character placed in the world
editor as a script NPC, both overrides (`script_npc_hp`, `script_npc_flags` → `ignore_ai`) are
properties of that object's record in the zone's `0x2234` stream — the same situation as a placed
character's `ctg_object_name` (`interp_named_object_resolution.md` §2.8). This note does not say
what any real zone record contains; that needs the parked interior opened, which is the owner's
decision.

**Practical reading for the partner team:** without the zone data, a faithful spawn state is: max HP
= the rank-table value (bonus 0, current HP = max), ignore-AI = off — **correct for every character
whose placement record carries neither `script_npc_hp` nor `ignore_ai`, and wrong exactly for those
that do**. Which of the 98 missions' characters are in the second group cannot be known from code.

---

## 3. OPEN items and what would settle each (all code-only)

1. **Level index for ordinary classes**: `0x0096e830` (EAX = preset) returns either the preset's own
   rank's `rank_numeric` or, for preset `+0x1c` categories 0/2–6 with a re-levellable rank type,
   `0x005f4ad0()`. Dump `0x005f4ad0` and `0x006d2910` (its gate, compared with 4) to learn what
   picks the rank row (likely a progression or difficulty value).
2. **The multiplier block**: dump `0x005f4a50` (what object; are `+0x1c`/`+0x20` difficulty
   scalars?), `0x0052a230`, and the class test `0x00853b30` with argument 0x22.
3. **Preset `+0x28` = rank pointer**: dump the `character_definitions.xtbl` reader `0x00be0630`
   (searching its body for the store to record `+0x28` and the XML tag that feeds it) to move this
   from HIGH CONFIDENCE to CONFIRMED.
4. **Current HP under a `script_npc_hp` override**: dump `0x0096e6e0`, `0x00943cc0`, `0x009659f0` (the
   tail helpers of `0x009d7480`) for a write to `+0x1cb8`.
5. **Other producers of script-NPC records**: `0x00a354e0` (stream kind 0x27), the mission-module
   binders `0x00735940` / `0x0072c260` / `0x006d2fc0`, and `0x009e35b0` (another `+0x1da8` writer),
   to see whether any engine path sets `script_npc_hp` or `ignore_ai` from code rather than data.
6. **Zone data** (not code, and parked): which placed objects in a mission's zones are script NPCs and
   which carry `script_npc_hp` / `script_npc_flags`. Owner's decision only.

Dumps for this note are in the session scratchpad under `ss/` (`func1`–`func6`, `xref2`, `xref3`,
`ptrs1`–`ptrs4`, `range1`, `str1`), reusing the earlier attempts' `cs/` and `charspawn/` dumps.

---

## 4. Cleanroom self-check

Prose only; addresses as `0x…` literals; no decompiler local or parameter names, no pseudocode.
Pattern checked against this file (result recorded below after the final edit):

`\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4})\b`

Result: **0 matches** (ripgrep over the whole file, 2026-10-02). A second check for Ghidra's
auto-label prefixes (`FUN_`, `DAT_`, `LAB_`, `PTR_`) also finds 0 matches.
