# Interpretation of bridge job qohu (follow-up to ytgi): the residual NEEDS-EXE questions of `spec-tables-progression.md`

Dumps: `D:\Crreish-sync\for-team-a\bus-results-team-a\results\20261001T173022-team-a-qohu\` (job `20261001T173022-team-a-qohu`,
"tables-progression — residual NEEDS-EXE next dumps (2026-10-01 re-derivation)"; job file
`D:\Crreish-sync\for-team-a\bus-results-team-a\jobs\20261001T173022-team-a-qohu.json`). Written 2026-10-01 by Team A. Labels:
**CONFIRMED — disassembly** only for what these dumps show; **CORRECTED** where a dump contradicts the live spec; otherwise
HIGH CONFIDENCE / HYPOTHESIS / OPEN. Prior context: `rederive_tables-progression.md` (job `20261001T123123-team-a-ytgi`, whose
"Next-dump list" is exactly this job's argument list) and the live spec `spec-tables-progression.md` (its "Review status (2026-10-01)"
line names the units this job targets: §3.1, §4.1, §5, §10.7, §10.9 re-derived but not cleared, §10.12 still NEEDS-EXE, plus the
residual OPEN items left inside cleared units §1.3, §2.3, §6.1, §10.1, §10.6, §10.10, §10.11).

Job note: all three steps ran. Eighteen `func` bodies at depth 1 (`func1/`; every requested root is present and complete; the
option line reports `depth=1 maxfuncs=6 maxinsn=2000`, so each file carries at most five callees and says how many were left
queued), one `xref` listing (`xref/xref_0x0118cf40.txt`) and two `range` windows (`range/`: 0x18 bytes at `0x01312708` and 0xef8
bytes at `0x022cd108`). The question numbering below follows the ytgi next-dump table, one subsection per row. Several bodies
appear as depth-1 callees in more than one file (the plain-open helper `0x00dc5ac0`, the table-open `0x00dac9a0`, the CRT
`_stricmp`); they are cited from whichever file holds them.

---

## Headline

Of the fourteen questions, **eight are settled outright** (§1.3, §6.1, §10.7, §10.12, §10.11, §5, §2.3, §1.4/§10.10), **three are
settled with one residual each** (§3.1: five of six hops read, the archive-priority lookup is now pinned to `0x00daad80` or
below; §4.1: both save-block writers read, no reordering anywhere, the data test is still the next step; §10.9: the reader of the
five extra elements is found, `0x00bb6400`, but the file name its caller passes is not in these dumps), and **three stay OPEN**
(§10.6: the six `Spline_Type` pointers are read but not the strings behind them; §10.1: see Q14; §2.2's absent
`Allow_Update_By_Server` outcome hangs on the CRT's invalid-parameter handler). **Two corrections**: §10.7's two scalar
destinations are the reverse of the 2026-10-01 text, and `Freerunning_fail_pct` is confirmed unread; `spec-save-format.md` §7's
"get" for stat handler slot 1 is wrong (it is requirement-evaluate).

Three side finds worth carrying to other documents: the three-character string at `0x01124348` (jfue's tutorial name 176) reads
`dlc`; the stat update entry `0x00710cb0` is the consumer of `HudUpdateFrequency` (§3.6 OPEN); and the entry-13 save writer
`0x00b982b0` confirms `0x4a60` ← player `+0x1b70` (the melee multiplier) directly, settling §4.6's `0x4A60` argument on the save
side.

Resume-pass addition (2026-10-02, same dumps): the activity reader's own xref block shows that bit 2 of record byte `+0x43` —
set by no `Disable_flags` string — is nonetheless **tested** by the consumer `0x0061a900` (alone and paired with bit 3), so
§10.12's "unused" reads "never set from data, live in code" (Q4 addendum). No other conclusion changes.

---

## Q1. §1.3 — the generic bool reader's literal set (`0x00dab850`) — CONFIRMED — disassembly

`func1/func_0x00dab850.txt`, body `0x00dab850`–`0x00dab8ab`, two callers: the "always" bool reader `0x00dac480` (at `0x00dac4e5`)
and the "only if present" reader `0x00dac510` (at `0x00dac574`) — exactly the pair §1.3 names.

Arguments: a text pointer and a length. Four case-insensitive, length-bounded compares (`_strnicmp` with the caller's length) in
this order against the literals at `0x0129bce0` (`true`), `0x0111d278` (`yes`), `0x012a253c` (`false`), `0x0113fea4` (`no`):

- `true` or `yes` → returns 1;
- `false` → returns 0;
- `no` → the compare is executed but **its result is discarded**; the function falls through to the same return 0 that any
  unrecognised text reaches.

So the four-literal set stands as the spec has it, and the rule "anything else reads as false" is exact: `no` and garbage are
indistinguishable to the caller. The length the callers pass is **0x400** (the body of `0x00dac480` is in
`func1/func_0x00975170.txt`: it copies the child's text into a 1 KB stack buffer whose last byte is forced to zero, then calls this
routine with the buffer and 0x400), so the bounded compare is a whole-string compare in practice. **[CONFIRMED — disassembly.]**

Side find (xref block of the same file): the `true`/`false` literals are also referenced from data at `0x005d3e4f`…`0x005d3fdf`
(nine pairs, 0x32 bytes apart) and `0x005dd058` — a static table of bool-text pointers in undefined code, unrelated to this reader.

## Q2. §6.1 — what the bounded copy helper does when the child is absent (`0x00daba70`) — CONFIRMED — disassembly

`func1/func_0x00daba70.txt`, body `0x00daba70`–`0x00dabaaa` (depth-1 callees `0x00dc4ff0` first-child-by-name and `0x00da7930`
bounded copy are in the same file). Arguments: destination, size, node, child name.

1. If the child name is null, the node itself is the source (a "copy this node's own text" mode); otherwise the first child of that
   name is looked up case-insensitively (`0x00dc4ff0`: walk the child list at node `+0x8` / next at `+0x4`, `_stricmp` on `+0x0`).
2. **If no such child exists, the function returns without touching the destination.**
3. If the child exists but its text pointer (`+0xc`) is null, it likewise **returns without touching the destination**.
4. Otherwise `0x00da7930`(dst, text, size): `_strncpy` of `size` bytes, then `dst[size − 1] = 0` — a bounded copy with a forced
   terminator (and zero-padding of the remainder, which is what `_strncpy` does).

**[CONFIRMED — disassembly.]** Consequence for §6.1's residual: the two `Check_Detection` reads of `0x006049a0` (callers at
`0x00604a50` and `0x00604ae7` in this file's reference list) share one stack buffer, so **an absent `<Police><Check_Detection>` leaves
the buffer holding the `<Gang>` text, and the police flag then takes the gang's value**; an absent gang element reads whatever the
stack held. **[HIGH CONFIDENCE — the helper's behaviour is CONFIRMED; that `0x006049a0` does not clear the buffer between the two
reads is from the ytgi reading, not re-dumped here.]** Real data always supplies both, so no implementation input changes; a host
should document "absent ⇒ inherits the sibling block's text" rather than "absent ⇒ false".

The same rule applies to every other `0x00daba70` site (the reference list shows 30+ callers across the table loaders): the
"bounded string copy" reader of §1.3 **never writes on the absent path**, which is the "only if present" semantics; its sibling
`0x00dabab0` (not dumped) is the one that additionally reports presence. The three "always" readers, by contrast (`0x00daccb0`
float, `0x00dabc70` int, `0x00dac480` bool — all three bodies in `func1/func_0x00975170.txt`), copy the text into a 1 KB stack
buffer only when the child exists and **parse the buffer regardless**: the §1.3 caveat, seen again in full.

## Q3. §10.7 — the `drunk_levels.xtbl` row parser (`0x00975170`) — CONFIRMED — disassembly, with one CORRECTION to the 2026-10-01 storage paragraph

`func1/func_0x00975170.txt`, body `0x00975170`–`0x00975514`, single caller `0x00975520` (at `0x009755b6`). Calling shape: **the
slot index arrives in a register (ECX) and the `<Drunk_Levels>` row element in another (EAX)**; a slot ≥ 3 returns at once. That
settles "the slot register": the caller passes 0, 1 or 2 — the drug-type test order, by ytgi's reading of `0x00975520` (not
re-dumped).

1. **Presets.** For the slot, the parser itself resets **five 0x38-byte level records at `0x02625878` + 0x118·slot + 0x38·level**:
   `+0x00` 0.0, `+0x04` 1.0, `+0x08` 1.0, `+0x0c` 2000, `+0x10`…`+0x14` 0.0, `+0x18`…`+0x20` 0, `+0x24`…`+0x30` 0.0, byte `+0x34` 0.
   So ytgi's HYPOTHESIS for the table-record base is **CONFIRMED**: slot stride 0x118 = 5 × 0x38, records from `0x02625878`.
2. **Scalars** (always-float reader `0x00daccb0`): **`Max_Booze_Points` → `0x02625834` + 4·slot; `Max_Time_Drunk` → `0x02625828` +
   4·slot.** This is the reverse of the attribution in the spec's 2026-10-01 storage paragraph (which put `Max_Booze_Points` at
   `0x02625828`). **[CORRECTED — disassembly: the push order at `0x00975396`–`0x009753bd` is unambiguous.]** With that correction
   the initialiser presets ytgi read become `Max_Time_Drunk` = 120.0 and `Max_Booze_Points` = 100.0 (presets not re-dumped).
3. **Level count** at `0x0262581c` + 4·slot: reset to 0, incremented once per level kept.
4. `Levels` wrapper → each `Level` child in sibling order; **at most 5 are stored** (a sixth and later are skipped while the loop
   still walks them). Per level, in read order, into the record: `Percent_drunk` f `+0x00`, `Camera_rotation_mult` f `+0x04`,
   `Camera_pitch_mult` f `+0x08`, `Random_input_switch_time` i `+0x0c`, `Random_input_amount_foot` f `+0x10`,
   `Random_input_amount_vehicle` f `+0x14`, `Control_delay_on_foot` i `+0x18`, `Control_delay_vehicle` i `+0x1c`,
   `Ragdoll_on_impact_time` i `+0x20`, `Reticle_x_max_offset` f `+0x24`, `Reticle_y_max_offset` f `+0x28`, `Reticle_x_speed` f
   `+0x2c`, `Reticle_y_speed` f `+0x30`, **`Sleepy` bool byte `+0x34`** — ytgi's `+0x34` correction confirmed on the parser side.
5. **`Freerunning_fail_pct` is not read.** The fourteen element names above plus `Max_Booze_Points`, `Max_Time_Drunk`, `Levels` and
   `Level` are every string literal the function pushes; nothing else asks the document for a child. **[CONFIRMED — disassembly for
   this parser; that no other code reads it from the document is HIGH CONFIDENCE — the loader `0x00975520` was not re-dumped.]** So
   the element every real `Level` row carries is dead authoring data on this build.

§10.7 can be cleared: base, slot register, `Sleepy` offset and the read list are CONFIRMED; the one open thread
(`Freerunning_fail_pct`'s consumer) closes as "none".

## Q4. §10.12 — the `activity_types.xtbl` reader (`0x006174c0`): bit 2 of `+0x43` is set by no flag string — CONFIRMED — disassembly (and, per the addendum, is tested by a consumer)

`func1/func_0x006174c0.txt`, body `0x006174c0`–`0x00617982`, callers `0x00617e10` (two sites, `0x00617e60` / `0x00617e86`: the base
and per-DLC calls). Calling shape: file name in a register (ECX), then (framework, pool) on the stack — the (file, framework, pool)
of §10.12.

What it does, in order:

1. For each of the 19 records at `0x014b2de8` (stride 0x94) zero `+0x4c` and `+0x50`; then walk two registered-object lists
   (`0x014b2c68` × count `0x014b2b60`, `0x014b2b68` × count `0x014b2ac4`), ask each object its activity type (virtual slot `+0x8`)
   and **count the instances per type in record `+0x4c`** — a live counter rebuilt at every load, not a table field.
2. Open the table (`0x00dac9a0`(file, 0, 1)); for each `Activity` row: the `Framework` filter exactly as in Q10 (compared only when a
   framework was passed; row default `main`); `Name` → `0x00614d70` → type index; record = `0x014b2de8` + 0x94·index **with no
   range check** (that an unknown name yields −1 is the earlier reading of `0x00614d70`, not re-dumped).
3. `Unlockables` → its `Unlockable` children only (a top-level `<Unlockable>` such as `Fraud`'s is never asked for): text →
   `0x0071cc90` → id; stored at (`+0x54`)[count] when the id is not the invalid id and count + 1 ≤ capacity `+0x58`; count `+0x5c`
   is reset to 0 first.
4. `Completion_Image` → its `Filename` child (`0x00dac950`, 64 bytes) → extension stripped (`0x00da86f0`) → pooled (`0x00a74910`)
   → `+0x08`; absent → `+0x08` = the empty-string constant at `0x0129a0e3`.
5. Word `+0x41`/`+0x42` = 0; the `Activity_type_flags` element is queried once per flag string with `0x00dac7d0` (not dumped; "does a
   `Flag` child carry this text", by its use): `+0x41` bits 0–7 = `stats doesnt count`, `keep screen faded`, `auto advance level`,
   `remove noteriety spawns`, `reset noteriety each level`, `uses button mashing interface`, `fail on death`, `finisher only death`;
   `+0x42` bits 0–2 = `no vehicle eject`, `do initial warp`, `racing`; **`+0x42` bit 3 = set iff the framework argument is not
   `main`** (compared with the literal; the base caller `0x00617e10` pushes the `main` literal at `0x00617e56`, per the xref block
   in `func1/func_0x0071b070.txt`).
6. Word `+0x43`/`+0x44` = 0; only if `Disable_flags` exists, twelve queries: `turn off spawning` → `+0x43` bit 0; `disable all stores`
   → bit 4; `disable crib` → bit 5; `disable distant spawns` → bit 1; `disable HUD` → bit 6; `allow cops to shoot from vehicle` →
   bit 7; `disable helicopters` → `+0x44` bit 0; `disable attack helis` → bit 1; `disable player swap cheats` → bit 2; `disable
   parking spawns` → **`+0x43` bit 3**; `disable warp triggers` → `+0x44` bit 3; `disable roadblocks` → `+0x44` bit 4.
7. `Soundbank_Name` → `0x005540a0` → `+0x48`. Next `Activity`. The document is freed at the end (`0x00dab960`).

**Bit 2 of `+0x43` is written by no flag string**: the twelve `Disable_flags` literals above are every literal the reader pushes
between the `Disable_flags` lookup and `Soundbank_Name`, and they map onto bits 0, 1, 3–7 of `+0x43` and 0–4 of `+0x44` — the
spec's table verbatim. Nothing was missed; nothing in the table sets the bit on this build. **[CONFIRMED — disassembly.]** §10.12 is
cleared.

**Addendum (resume pass, 2026-10-02 — same dump, the xref block for the record-0 flag byte `0x014b2e2b`).** That byte has exactly
two referencing functions in the whole disassembly: this reader (fifteen sites, all the stores above) and **`0x0061a900`**, which
**tests** `+0x43` with the masks `0x01`, `0x02`, `0x04`, `0x10`, `0x20`, `0x80` and **`0x0c`** (`TEST` at `0x0061aa2d` for `0x04`, at
`0x0061ab01` for `0x0c`) and `+0x44` with `0x01`, `0x02`, `0x08`, `0x10`. So **bit 2 of `+0x43` is read by the engine**, alone and
together with bit 3 (`disable parking spawns`), although no `Disable_flags` string sets it: it is a live flag in the consumer with
no data path on this build, not a bit the consumer ignores. The right wording is "never set by the table reader; tested by
`0x0061a900`", and a host that reproduces the reader never sets it and so never reaches the consumer's bit-2 branches.
**[CONFIRMED — xref for the two referencing functions; HIGH CONFIDENCE that no other writer sets the bit — the xref is by resolved
absolute address (record 0's byte) and would miss a writer that reaches a record through a pointer.]** The same block shows
`0x0061a900` testing neither `+0x43` bit 6 (`disable HUD`) nor `+0x44` bit 2 (`disable player swap cheats`), so it is not the only
consumer of these flags — only the one the disassembler resolved to record 0's address. `0x0061a900` is not dumped (next dumps, low
priority).

## Q5. §3.1 — the file open below the table-open helper (`0x00dc5a10`) — CONFIRMED — disassembly; the priority lookup is one hop further

`func1/func_0x00dc5a10.txt`, body `0x00dc5a10`–`0x00dc5aba`; its only caller is the plain-open helper `0x00dc5ac0` (at
`0x00dc5b07`), as ytgi had it. The file name arrives in a register from that caller.

The body builds the three-byte mode string `rb` on the stack (copied from the literal at `0x0129a85c`) and then takes one of two
paths:

- **Memory-resident path.** Only if the byte `0x02a07ad8` is non-zero: `0x00dc59b0` on the object at `0x02a07ae4` hashes the name
  (`0x00dab330`(name, object`+0x20`)), walks the hash chain from object`+0x1c` (entry: `+0x0` pointer, `+0x4` size, `+0x8` next,
  `+0x10` name) with a case-insensitive compare, and on a hit hands back the entry's (pointer, size); the file object is then
  built by `0x00dab0e0`(pointer, size, `rb`, 0, 0) — a 0x16c-byte record whose name defaults to the literal `mem mapped file`,
  with `+0x154` = 1 for a read mode, the size at `+0x160`, and the (pointer, size) pair in a sub-record at `+0x14c`. So
  `0x02a07ae4` is a **name → in-memory blob table**. **The gate byte `0x02a07ad8` has exactly one reference in the whole
  disassembly — this read** (the xref block in the same file); no instruction writes it, so on this build the memory-resident path
  is never taken **[CONFIRMED — xref for the single reference; "never taken" is HIGH CONFIDENCE, an undisassembled writer being the
  only escape]**.
- **Ordinary path** (gate clear, or name not in the table): `0x00dab0c0`(name, `rb`, 0, 0, −1) → **`0x00daad80`**(name in a
  register, mode in a register, 0, 0, −1), the general open-by-name. **Not dumped.**

So the archive-priority lookup §14.2 describes (patch list first, then the mounted archives) is **not** in `0x00dc5a10`; it must
sit inside `0x00daad80` or below. The chain `0x00713bb0` → `0x00dac9a0` → `0x00dc5ac0` → `0x00dc5a10` → `0x00dab0c0` →
`0x00daad80` is now CONFIRMED — disassembly for five hops (the first is Q6); **§3.1's "open inside `0x00dc5a10`" is settled, and
the remaining gap moves one hop down to `0x00daad80`** (next dumps).

Context the same job supplies for §1.3: the plain-open helper `0x00dc5ac0` (body in `func1/func_0x00bb6400.txt` and
`func1/func_0x00ba5da0.txt`) is open → size → allocate → read all → parse → close: after `0x00dc5a10` it reports `File "%s" does
not exist.` or `File "%s" exists, but appears to be locked.` (existence test `0x00da90d0`) when the open fails, `File %s is 0 bytes!`
when the size (`0x00da7d20`) is zero, takes a buffer of that size from the provider object's virtual slot `+0x38` (`Mempool "%s"
not large enough!` on failure), reads the whole file (`0x00daa5d0`; `Failed to read entire file!`), parses it with `0x00dc5820` into
the document root it returns, and closes the file object (`0x00daa440`). The provider defaults to `0x029cff98` when the table-open
`0x00dac9a0` is called with a null one (its body is in `func1/func_0x00713bb0.txt` and `func1/func_0x006174c0.txt`).

## Q6. §3.1 first hop and §2.2 `Allow_Update_By_Server` (`0x00713bb0`) — CONFIRMED — disassembly

`func1/func_0x00713bb0.txt`, body `0x00713bb0`–`0x007142db`, caller `0x007105b0` (the stage wrapper §11 names).

- **First hop.** The function opens with `0x00dac9a0`(`stats.xtbl`, 0, 1) at `0x00713bc0` — a direct call with the literal at
  `0x0114268c`. With Q5 the §3.1 chain is read end to end except its last link. **[CONFIRMED — disassembly.]**
- The body matches §2.1–§2.4 line for line: count `0x0151af30` = 0; per `Stat`: `Name` heap-copied from the pool `0x01495410`
  (`0x00dad460` + `0x00da7930`); scanned against the 217-entry static table at `0x01141bd0` (the loop bound is the literal 0xd9);
  **no match → jump out of the row loop** (`0x00713c64` → `0x00714297`), free the document and go on to `achievements.xtbl` — the
  "an unknown name ends the whole load" rule, CONFIRMED; row = `0x01517228` + 0x48·id (id from `0x01141bd4`); `+0x14` = 0, `+0x04` =
  `0x00849df0`(`DisplayName`), presets `+0x08` = 0x33 and `+0x0c` = 6; the eight `Value_Type` children tested in the order integer,
  float, distance, time, money, boolean, percent, complex with the handler/class pairs of §2.4; `percent` → `+0x3c` = −1, then
  `Percentage_Of_Stat` compared **case-sensitively** (an inlined byte loop) against the name of **every row slot from `0x01517228`
  up to `0x0151af30`** (null names skipped — so the denominator is found only if its row was loaded earlier, as §2.2 says); `complex`
  → a 0x98-entry jump table on the id (ids 13…0xa4), else the message `Handlers not set up for complex stat %s.`.
- **`Allow_Update_By_Server`.** The text pointer from `0x00daba10` (null when the element is absent) goes straight to
  `_stricmp`(text, `true`) at `0x00714230`; equal → set bit 1 of `+0x10`, else clear it. The CRT body of `_stricmp` is in this job
  (`0x00eaa178`, `func1/func_0x0071b070.txt`): a null argument sets `errno` = 22, calls the invalid-parameter routine `0x00ea2742`
  and — **if that routine returns** — yields 0x7fffffff, i.e. "not equal", so the bit is **cleared**. Whether the engine installs a
  returning invalid-parameter handler is not in these dumps (the stock VS2008 release handler terminates the process), so the
  absent-element outcome is **OPEN between "flag cleared" and "process terminated"**; shipped data supplies the element on 217/217
  rows, so no implementation input changes. **[CONFIRMED — the null path; OPEN — the handler.]** This replaces §2.2's "HIGH
  CONFIDENCE, not exercised" with a precise statement.
- `LivePropertyID` / `LiveLeaderboardID`: `+0x40` / `+0x44` preset 0, then the write-only-if-present int reader `0x00dabd20`. The
  count is incremented per accepted row. After the loop: `0x0151af34` = 0, `achievements.xtbl` opened the same way,
  `0x00713840`(document, `main`), the base count copied to `0x0151af38`. **[CONFIRMED — disassembly, as §3.1 states.]**

## Q7. §10.11 — the sprint entry actually requested (`0x009dc780`) — CONFIRMED — disassembly + xref

`func1/func_0x009dc780.txt`, body `0x009dc780`–`0x009dc8d1`, caller `0x005d25f0` at `0x005d34ba` (the boot stage §11 places it
in). At `0x009dc800` it pushes the literal **`Single Player`** (`0x01175ffc`) and calls `0x009fffe0` — the sprint loader, whose
reference list in this file has exactly that one call site. **The `Multiplayer` row is dead data: CONFIRMED** (upgraded from
ytgi's HIGH CONFIDENCE). The function is the player-module initialiser: it resolves the style named `styletest_pc` (`0x00be3360`:
name hash → table `0x029a5e00` → object), loads it (`0x00dafea0` / `0x00daff10` on the object's stream handle), finds an item by
name (`0x00754530`), stores five globals `0x0263ae00`…`0x0263ae14`, calls `0x009ff8f0`(1) and `0x009dc5e0`(), and resolves eleven
animation / camera-shake names (`player_sprinting`, `anim_shake_tiny`, `anim_shake_small`, `anim_shake_medium`, `anim_shake_large`,
`anim_rumble_small`, `anim_rumble_medium`, `anim_rumble_large`, `automobile_looping`, `airplane_looping`, `helicopter_looping`)
through `0x0057bee0` into `0x0263ae4c`…`0x0263ae74`. §10.11's residual closes.

## Q8. §5 — saved level → respect-level record (`0x00b97340`, `0x007cfb60`) — CONFIRMED — disassembly

### Q8.1 `0x00b97340` is the respect award; the saved level indexes the record of the *next* level

`func1/func_0x00b97340.txt`, body `0x00b97340`–`0x00b973ad`, callers `0x006ced90` (two sites, `0x006cedbb` / `0x006cedce`, not
dumped). Arguments: a block P and an amount A. P's fields are addressed as `+0x4418`, `+0x441c`, `+0x4420` — the three respect
fields of the save image (`spec-save-format.md` §10.3), so P is the in-memory progression block laid out like the save.

1. P`+0x4418` (total respect) += A.
2. cost = `0x0060eb40`(P`+0x4420`), i.e. **the `Respect` of record L where L is the saved level, 0-based** (`0x0060eb40`(n) returns
   the dword at `0x014b033c` + 0x64·n, which is record n's `+0x4` — base `0x014b0338`, stride `0x64` — and 22000 for n ≥ 50; body
   in the same file).
3. While P`+0x441c` (respect inside the current level) + A ≥ cost: **L += 1**, A += (P`+0x441c` − cost) (carry the excess),
   P`+0x441c` = 0, cost = `0x0060eb40`(new L).
4. P`+0x441c` += A.

So **a saved level L means "L levels gained", and the next level costs the `Respect` of record L (the (L+1)-th `<respect_level>`
row)**: at a new game (L = 0) the first level-up costs row 1's 2115; at L = 49 it costs row 50's value. §5.2's "level 1 = first
row" is right in the sense that row 1 is the cost of *becoming* level 1 — the mapping is **row = L + 1 (1-indexed), record = L
(0-indexed)**. **[CONFIRMED — disassembly.]** Two consequences for a host, both forced by the loop:

- With §14.5's real values (row 50's `Respect` = 0), when L reaches 49 inside the loop the next cost is 0, so the loop runs once
  more and **L goes 48 → 50 in one award; level 49 is never a resting state**; at L = 50 the cost becomes the 22000 sentinel, which
  the loop can still pass if a single award is that large (nothing caps L at 50) **[CONFIRMED — mechanism; HIGH CONFIDENCE that
  this is what shipped data produces]**.
- Multiple level-ups in one award are allowed, and the excess carries over.

`0x0060eb40` has three callers: the two sites above and **`0x007cfb60` at `0x007d020d`** (Q8.2).

### Q8.2 `0x007cfb60` is the save-slot list builder of the load/save menu; it uses the same convention

`func1/func_0x007cfb60.txt`, body `0x007cfb60`–`0x007d03f0`; its only reference is a stored pointer in undefined code at
`0x007d0885` (a UI callback table). It takes a typed value record and a count. If the save system is busy (`0x00b94680`, byte
`0x0290ceca`) it shows the `SAVELOAD_SAVING_MESSAGE_TITLE` / `SAVELOAD_SAVING_MESSAGE_EXPOSITION` dialog and returns; otherwise it
enumerates the save slots (`0x02297d08`, count `0x02297d0c`, stride 0x60; the header read through `0x00b961f0` / `0x00b949c0` into a
0x58-byte local) and fills a UI list object (`0x00e1e8c0`(`0x02297d10`)) with one entry per slot, seventeen typed fields each
(`0x00e1e990`(index, {type, value}) then `0x00e24930`): 0 slot index; 1 bool "neither corrupt nor bad-version" (header bytes
`+0x5c`, `+0x5d`); 2 and 3 two header bools; 4 a string — `SAVELOAD_BAD_VERSION` / `SAVELOAD_SAVEFILE_CORRUPT_TITLE` /
`SAVELOAD_SAVE_GAME` (when header byte `+0x5e` is set — HYPOTHESIS: an empty slot) / the slot's own name; 5 and 6 ints; 7 a 22-byte
text; 8 the difficulty name (`0x005f4ab0`(index), the difficulty module); 9 a literal or an int; 10 and 11 ints; **12 the saved
level L (int, raw); 13 float = (respect inside the level) ÷ `0x0060eb40`(L) when L < `0x0060eba0`() (the record-count accessor, not
dumped — HIGH CONFIDENCE), else 0.0; 14 int = `0x0060ebb0`(L) = Σ `Respect` of records 0…L−1**; 15 float = two dwords divided as
unsigned (a completion fraction, by shape); 16 int.

So the menu, like the award, treats **record L as the level currently being earned** (its `Respect` is the denominator of the
progress bar) and Σ records 0…L−1 as the respect already banked; the displayed level number is L itself, unadjusted. Together with
Q8.1 the §5 mapping is settled: **saved level L = levels gained; row L+1 (1-indexed) / record L (0-indexed) = the next level's
cost; L = 50 → progress 0, cost sentinel 22000.** **[CONFIRMED — disassembly.]** §5 is cleared.

## Q9. §2.3 — the stat handler-table slots: slot 1 is *requirement-evaluate*, slot 0 is *update* (`0x00712330`, `0x00712380`, `0x00710cb0`)

### Q9.1 The two slot-1 bodies — CONFIRMED — disassembly

`func1/func_0x00712330.txt` (int class) and `func1/func_0x00712380.txt` (float class). Both are stored as data by the handler-table
initialiser `0x00714770` (the int body at 30+ sites, the float body at 12 — the per-type slot-1 pointers), as §2.3 says. Both take
**(stat row, requirement record)** and read requirement `+0x4` (condition) and `+0x8` (target):

| condition `+0x4` | returns 1 when | returns 0 when |
|---|---|---|
| 0 (`at least`) | row `+0x38` ≥ target | row < target |
| 1 (`at most`) | row `+0x38` ≤ target | row > target |
| anything else | — | always (the low byte is cleared; the upper bytes of the return register keep the condition value, so the result is a **byte**, not a dword) |

The int body converts both the row value and the target from int to **single-precision float**, then widens to double for the
compare; the float body loads both as floats and widens. So integer requirements are compared after a float narrowing (exact below
2^24; a target of 16,777,217 compares equal to a value of 16,777,216). The `at least` compare is "not below" and the `at most`
compare is "not above", so an unordered result (NaN in a float stat) returns 0 for both. **[CONFIRMED — disassembly.]**

This settles the naming conflict with `spec-save-format.md` §7 item 4 ("slots: update, **get**, …"): **slot 1 takes a requirement
and returns a truth value; it is not a getter.** "requirement-evaluate" is the right name; `spec-save-format.md` §7 should be
reconciled (note for that document).

### Q9.2 `0x00710cb0` is the stat update entry: slot 0 = *update*, and it is the `HudUpdateFrequency` consumer — CONFIRMED — disassembly

`func1/func_0x00710cb0.txt`, body `0x00710cb0`–`0x00710e3f`, 30+ callers across gameplay code (the reference list: `0x0094d920`,
`0x007112d0`, `0x00621fc0`, …). Arguments (stat id, value). Guards: id in [0, count `0x0151af30`); the byte `0x014f71d4`
(`0x006f8370`; written by `0x006f98e0` / `0x006f9a20` in the cheat module — HIGH CONFIDENCE "a cheat has been used") must be 0; the
byte `0x014f3d34` (`0x006e44f0`; HIGH CONFIDENCE a mission/mode byte) must be 0; the row's handler type must have a non-null
**slot 0** at `0x0151cbf8` + 0x18·type.

Then: (a) for each achievement that references the stat (count row `+0x14`, pointers from `+0x18`) snapshot its progress with
`0x00710950` (body in the file: returns {current, target, fraction}; fraction capped at 0.99 — the float at `0x0111be00` — unless
earned, then 1.0; one-requirement achievements read the stat's int or float value through `0x00710730` / `0x007107b0` and the
target through `0x00710840` / `0x00710870`; multi-requirement ones count the requirements that pass **slot 1**); (b) **call slot 0
with (row, value)** — the update; (c) for each referencing achievement: `0x00710c50` (if cheats are inactive and `+0x40` is clear and
every requirement passes slot 1 → award `0x007108a0`), then, if still unearned and `HudUpdateFrequency` (`+0x4c`) ≠ 0: recompute the
progress and, unless the target is 1, **show the HUD progress notice `0x007fbf70` when the current count has crossed a multiple of
`HudUpdateFrequency` since the snapshot** (the test is `freq − (old mod freq) ≤ new − old`); (d) call every registered stat-change
listener in the pointer list at `0x01517218` (count `0x0151af3c`) with the id; (e) `0x00698bb0`(id, value).

So slot 0 = update (row, value) and slot 1 = requirement-evaluate (row, requirement) → truth value. `HudUpdateFrequency`'s
consumer (§3.6 OPEN) is this routine: a HUD notice every N increments of the tracked stat. **[CONFIRMED — disassembly.]** §2.3's
residual closes.

## Q10. §1.4 / §10.10 — the `tweak_table` reader filters on the framework it is handed (`0x0071b070`) — CONFIRMED — disassembly (upgrade)

`func1/func_0x0071b070.txt`, body `0x0071b070`–`0x0071b17e`, callers `0x0071b420` (base, at `0x0071b437`) and `0x0071b450` (DLC, at
`0x0071b4b8`). The framework string arrives in ECX, the `Table` element on the stack. The registry is fetched lazily (`0x0071afb0`:
first use sets capacity 0x280 at `0x015216dc`, count 0 at `0x015216e0`, array `0x015216e4`, and registers an exit hook). For each
`Tweak_Table_Entry` sibling: **only if the framework pointer is non-null**, the row's `Framework` text (default `main`) is compared
case-insensitively with it and a mismatch skips the row; a null framework accepts every row. Then `Name` (256-byte bounded copy) →
registry lookup `0x0071b000` (index, negative when unknown → row ignored) → `Value` through the always-float reader into a slot
preset to 0.0 → `0x0071ae40`(entry, value), the apply.

Since ytgi read `0x0071b420` loading the `main` literal into ECX right before this call, both halves are now disassembly: **the
base `tweak_table.xtbl` pass filters on `main`; only a null framework (which neither caller passes) would accept all rows.**
**[CONFIRMED — disassembly, upgraded from HIGH CONFIDENCE; no behavioural change on real data — 601/601 rows have no
`Framework`.]**

## Q11. §4.1 — the unlockable save-block writers (`0x00b982b0`, `0x00b99810`, `0x0071c6e0`)

### Q11.1 `0x0071c6e0` resets the unlockable save/restore scratch object — CONFIRMED — disassembly (shape); HIGH CONFIDENCE (identity)

`func1/func_0x0071c6e0.txt`, body `0x0071c6e0`–`0x0071c73c`, thiscall; callers **`0x0071fd30`** (the restorer, at `0x0071fd39` — its
first action) and `0x0071ed80` (at `0x0071ede6`, the function immediately before the row loop `0x0071edf0`; not dumped). It
writes `+0x0` = 0, zeroes `0x578` bytes from `+0x4`, and then zeroes three (pointer, length) buffers held at `+0x600`/`+0x604`,
`+0x610`/`+0x614`, `+0x620`/`+0x624`.

`0x578` = 1400 bytes = **350 dwords = the 0x15e cap of the side list of saved ids that matched no loaded record** (ytgi, from
`0x0071dbd0`; the same literal 0x15e and the same object `0x0153a750` appear in the `0x0071dbd0` body dumped here). So the object
is: a count at `+0x0`, the 350-id side list at `+0x4`, and three state bitsets as out-of-line buffers at `+0x600`/`+0x610`/`+0x620`.
It is a reset, not a writer of order. **[CONFIRMED — disassembly for the writes; HIGH CONFIDENCE for the identification by size.]**
That the loader-side caller `0x0071ed80` runs the same reset before the row loop (so the side list is emptied when the table is
(re)loaded) is HYPOTHESIS from its position.

### Q11.2 `0x00b982b0` and `0x00b99810` are the two save-block writers; neither reorders — CONFIRMED — disassembly

Both have no caller in defined code (they are reached through the serializer table, as `spec-save-format.md` has them: entry 13
and entry 28). Both take the save image block P.

**`0x00b982b0` (entry 13; `func1/func_0x00b982b0.txt`, body `0x00b982b0`–`0x00b98566`).** Local player object L = `0x009da4e0`()
(the global `0x0262edfc`). It copies scalars into P: `+0x4a24`…`+0x4a30` ← the four floats at `0x014a4584`…`0x014a4590` (the
notoriety multipliers; written by the apply routines `0x00605480` / `0x006062c0` and restored by the loader `0x00b98570`, per the
xref block); `+0x4a34`…`+0x4a44` ← L`+0x2758`…`+0x2768` (the five damage scales) and `+0x4a48` ← L`+0x276c` (the sixth slot of that
array — the `script` damage type, index 5, is saved although the unlockable apply rejects it); `+0x4a4c` ← L`+0x2770` (health
modifier); `+0x4a50` ← the sprint float `0x0268d298`; `+0x4a54` ← (`0x0268d2a4` == 3) — the "unlimited sprint" byte, meter state 3,
matching §4.4 type 8; **`+0x4a60` ← L`+0x1b70`** — the melee multiplier, so the save writer confirms §4.6's reading of `0x4A60`
directly; `+0x4a64` ← L`+0x27a4`; `+0x4a68` ← L`+0x27a8` (firearm accuracy); word `+0x4a6e` ← L`+0x27ac` (weekly payments). Then
three 0x2c-byte bitsets at P`+0x49a0`, `+0x49cc`, `+0x49f8` are zeroed and **`0x0071dbd0`(P`+0x4428`, A, B, C, 0)** emits the base
block: record hashes in index order into `+0x4428` and the three state bits per record (body in the file: flag 0 = records [0, base
count); bit A = `+0xe0` or `+0xfd`; bit B = `+0xe1` or `+0xfd`; bit C = `+0xe2` — the `+0xfd` OR is the DLC-gated marker of §4.3).
Finally a 16-byte block at P`+0x4f5c` is zeroed and handed to `0x00b83b80` (not dumped). `+0x4418`…`+0x4420` (respect), `+0x4a58`,
`+0x4a5c` and `+0x4a6c` are **not** written by this routine: the award routine of Q8.1 writes the respect fields into P directly, so
P is the live progression block and entry 13 only refreshes the mirrored fields **[HIGH CONFIDENCE; note for
`spec-save-format.md`]**.

**`0x00b99810` (entry 28, the DLC counterpart; `func1/func_0x00b99810.txt`, body `0x00b99810`–`0x00b99bdf`).** Builds the DLC
wardrobe-item list at P`+0x11d90` (count `+0x11d8c`, cap 0x800: `0x00823c90` keeps the player's items whose record pointer lies
outside the base item table `0x022db9c8`); zeroes three 0x2c-byte bitsets at `+0x16308`, `+0x16334`, `+0x16360` and the 0x578-byte
id array at **`+0x15d90`**; **`0x0071dbd0`(P`+0x15d90`, A, B, C, 1)** emits the DLC block: records [base count, count) plus, through
`0x0071c7e0`, the side-list ids, up to 0x15e in all. Then, if the activity system exists (`0x00614e30` → `0x014b39b8`): the second
activity table (`0x00616fe0` into `+0x1638c` from that object's `+0x308`); the mission-object state list at `+0x18bd0` — every
object in the world list `0x03171a64` whose name begins **`dlc`** (`_strnicmp` with the 3-byte literal at `0x01124348`), up to 64,
then the pending-record queue `0x014c7a18` (count `0x014c7e18`), count at `+0x18fd0`; `0x00b97b50`(P); two flags at `+0x16718` /
`+0x16844`; `0x007164a0`(`+0x18ac8`, `+0x18acc`); a 0x200-byte pair list at `+0x18fd8` from the activity enumerators `0x00614de0` /
`0x00614e00`; `0x00b96900`(`+0x191d8`, `+0x191dc`, 20, 1); `0x00839ef0`(`+0x19aa0`). All of it is what `spec-save-format.md`'s
entry-28 note lists.

For §4.1: **both writers hand the record order straight to `0x0071dbd0`, which walks the runtime table by index; there is no sort,
hash order or priority pass in either.** The saved base order is the loaded row order — the ytgi conclusion is now complete on
the save side — and the only remaining explanations for the file-order mismatch are a different same-named `unlockables.xtbl`
(the patch-archive copy) or a parser sibling-order effect; the data test ytgi proposed is still the next step. **[CONFIRMED —
disassembly for the writers; OPEN — the order mechanism, data test pending.]**

Side find (cross-spec): the three-character string at `0x01124348`, which jfue (§26.28) could only bound in length as tutorial name
176, is **`dlc`** — the dword at that address in the xref block of `func1/func_0x00b99810.txt` is `0x00636c64`, and the decompiler
renders the compare as `_strnicmp`(name, "dlc", 3). Note for `spec-lua-api-behaviour.md` §26.28 and the tutorial table.

## Q12. §10.9 — who reads `default_global.xtbl`'s five extra elements (xref `0x0118cf40`, `0x00bb6400`, `0x00ba5da0`)

### Q12.1 The file-name literal has exactly one user — CONFIRMED — xref

`xref/xref_0x0118cf40.txt`: the literal `default_global.xtbl` at `0x0118cf40` is referenced **once**, at `0x00bb70b3` inside
`0x00bb70b0` (the loader §10.9 already describes). No other function opens this file by that literal.

### Q12.2 `0x00bb6400` reads all five extra elements (and more); `0x00ba5da0` does not — CONFIRMED — disassembly (the reads); the file name it opens is OPEN

`func1/func_0x00bb6400.txt`, body `0x00bb6400`–`0x00bb668f`, callers `0x00b9d3f0` (two sites). The function carries the source tag
`time_of_day_shared.cpp` (a debug path literal handed to the provider wrapper `0x00720cf0`). It takes (file name, provider), opens
the file with the **plain-open helper `0x00dc5ac0`** (document root, no `Table` step — the same shape as `0x00bb70b0`), initialises
the time-of-day object at `0x0290fbb0` (`0x00bb60c0`), and reads from the root:

| element | reader | default | destination |
|---|---|---|---|
| `horizon_mountain_enabled` | bool `0x00dc51b0` | 1 | byte `0x029152f0` |
| `fog_camera_follow` | bool `0x00dc51b0` | 0 | byte `0x029152f1` |
| `day_begin` | int `0x00dc5270` | 600 | `0x029152f4` |
| `day_end` | int `0x00dc5270` | 1800 | `0x029152f8` |
| `skybox_mesh_filename` | text `0x00dc5190` | `rfg_skybox` | 64 bytes at `0x02915388` |
| `cloud_mesh_filename` | text | `skybox_clouds` | 64 bytes at `0x029153c8` |
| `cloud_mesh_filename` → `cloud_mesh_horizon_mat` | text | `Cloud_base_material` | 64 bytes at `0x02915408` |
| `cloud_mesh_filename` → `cloud_mesh_overhead_mat` | text | `Cloud_overhead_material` | 64 bytes at `0x02915448` |
| `cloud_mesh_filename` → `cloud_mesh_skyline_mat` | text | `m_sky_matte_01` | 64 bytes at `0x02915488` |
| `tod_segments` → `segment` (repeated) | `0x00dc5240`(node, 0) per child | — | dword array at `0x029152fc`, capacity `0x02915300`, count `0x02915304` |

The bool reader `0x00dc51b0` (body in the file) returns the default when the node or its text is missing and otherwise true for
`yes`, `true` or `1` (case-insensitive) and false for anything else — a third bool vocabulary beside §1.3's four-literal reader and
§6.1's `true`-only compare. The int reader `0x00dc5270` and the per-segment reader `0x00dc5240` were not dumped (queued past
maxfuncs); "int with default" is by shape. The element-name pointers `skybox_mesh_filename` … `segment` come from a small pointer
table at `0x0131106c`…`0x01311084`; the `segment` pointer targets `0x0118c210`, whose dword is `0x6d676573` (`segm`…), so the child
name is `segment`. The function returns the time-of-day object.

So **the five elements §14.14 found are read by `0x00bb6400`, together with three cloud-material names and a segment list the spec
has not seen** — the same document shape that `0x00bb70b0` reads `orbitals` from. **[CONFIRMED — disassembly for the reads.]** What
is **OPEN** is the file name: `0x00bb6400` opens whatever `0x00b9d3f0` passes, and the `default_global.xtbl` literal has only
`0x00bb70b0` as a user (Q12.1), so the caller either composes the name (per world, say) or uses another literal. HIGH CONFIDENCE
it is the same `default_global.xtbl` (the element set matches §14.14's real file exactly); `0x00b9d3f0` is the next dump. The
`day_begin` / `day_end` defaults 600 and 1800 are in units these dumps do not show (OPEN).

`func1/func_0x00ba5da0.txt` (`0x00ba5da0`–`0x00ba5ece`, caller `0x00b9d190`, same source tag): opens a file by the name it is given
and, if the single 0x9400-byte time-of-day record at `0x029154c8` is still free (count `0x0290fb1c` < 1), parses the document into
it (`0x00ba4a10`, 0x1383 bytes of parser not read here), stores the file's base name at record `+0x93bd` (`0x00da84e0`, 64 bytes)
and bumps the count; on failure it resets the record (`0x00ba4720`). It is the time-of-day *data* loader, not a reader of the five
elements **[HIGH CONFIDENCE — its parser body was not read]**.

## Q13. §10.6 — the `Spline_Type` compare table (range `0x01312708`–`0x01312720`) — table shape CONFIRMED; names still OPEN

`range/range_0x01312708-0x01312720.txt`: 0x18 bytes of `.data`, read as little-endian dwords:

| index | at | pointer |
|--:|---|---|
| 0 | `0x01312708` | `0x01190654` |
| 1 | `0x0131270c` | `0x011388f8` |
| 2 | `0x01312710` | `0x0119064c` |
| 3 | `0x01312714` | `0x01190644` |
| 4 | `0x01312718` | `0x0118468c` |
| 5 | `0x0131271c` | `0x0116f410` |

The byte after the sixth pointer (`0x01312720`) is `0x08`, not the first byte of another `.rdata` pointer, so **the table is exactly
six entries**, matching the loop bound of 6 at `0x00be5a46` that ytgi read. All six targets lie in the string block (`0x0111…`–
`0x0119…`). **The strings themselves were not dumped** (range mode prints raw bytes, and these are pointers), so the names of entries
3–6 are **still OPEN**. What the pointers do show: entries 3, 2 and 0 are 8 bytes apart (`0x01190644`, `0x0119064c`, `0x01190654`),
so the strings at entries 3 and 2 are at most seven characters each if the block is packed — consistent with the spec's `Indoor`
(index 3) and `Offroad` (index 2) in index order, with `Highway Only` at index 0 following them. Consistent, not proof. Next:
`ptrs 0x01312708 6` (or `bytes` at the six targets).

## Q14. §10.1 — the extent of the store-discount record array (range `0x022cd108`–`0x022ce000`) — the window is all zero; the extent stays OPEN and is not an implementation input

`range/range_0x022cd108-0x022ce000.txt` is 2,085 address lines covering all 0xef9 bytes of the window: 337 single `db 0x00`
bytes and 1,748 two-byte zero pairs that the disassembler rendered as the instruction whose opcode is `00 00` (an `ADD` of a
register into memory) — nothing else. **Every byte from `0x022cd108` to `0x022ce000` is zero in the file image.** So the window
contains neither a data table nor code: it is the runtime-filled discount record array (the loader `0x0080e930` stores into it at
start-up; the neighbouring globals the other dumps touch in this region, `0x022db98c` and `0x022db9c8`, are likewise marked "not
file-backed" in their xref headers). A range dump therefore cannot show where the array ends, and no bound exists in the loader
(ytgi). What is known: 31 real rows, the save's 40-slot per-discount arrays (`spec-save-format.md` §12.5), and the next identified
global in this job's xref blocks, the item-table count at `0x022db98c`, 0xe884 bytes above — room for 0x4d4 records if nothing lies
between. **[CONFIRMED — the window's content; OPEN — the extent, not needed by a host, which should simply not cap the row
count below 40.]** The only way to pin it is a listing of every defined or referenced address between `0x022cd108` and
`0x022db98c` (next dumps).

---

## Spec changes

Exact text, keyed to the current sentences of `spec-tables-progression.md`. Job id for citations: `20261001T173022-team-a-qohu`.

### §1.3

1. **Append to the bool-reader row's 2026-10-01 note** (after "not yet dumped."): "**[2026-10-01, job `20261001T173022-team-a-qohu`:**
   0x00DAB850 is read: it compares the text, in order, with `true`, `yes`, `false`, `no`, each bounded by the caller's 0x400
   (whole-string in practice); `true`/`yes` → 1, everything else → 0, the result of the `no` compare being discarded. CONFIRMED —
   disassembly.]**"
2. **Append to the bounded-string-copy row** (`FUN_00DABA70` / `FUN_00DABAB0`): "*(2026-10-01, job qohu: 0x00DABA70 leaves the
   destination untouched when the child is absent or has no text; otherwise it copies `size` bytes and forces a terminator at
   `size − 1`. CONFIRMED — disassembly.)*"
3. **Review status line, append:** "Updated 2026-10-01 (job qohu): the four-literal set and the copy helper's absent-child
   behaviour CONFIRMED — disassembly."

### §1.4 and §10.10

4. **Replace** "(HIGH CONFIDENCE, 0x0071B070 not dumped; no behavioural change on real data: 601/601 rows have no `Framework`)" **with**
   "(CONFIRMED — disassembly 2026-10-01, job qohu: 0x0071B070 compares each row's `Framework`, default `main`, case-insensitively
   with the framework it is handed whenever that pointer is non-null and skips mismatches; only a null framework — which neither
   caller passes — accepts every row; no behavioural change on real data: 601/601 rows have no `Framework`)". Make the same
   replacement in §10.10's `Framework` line ("base call passes `main` in a register — HIGH CONFIDENCE, 2026-10-01, 0x0071B070 not
   dumped" → "base call passes `main` and the reader filters on it — CONFIRMED — disassembly, job qohu").

### §2.2

5. **Replace** "**effectively required** — a missing element passes NULL to the string compare (HIGH CONFIDENCE, not exercised)"
   **with** "**effectively required** — a missing element passes NULL to the CRT `_stricmp`, which sets `errno` and calls the
   invalid-parameter routine; if that routine returns, the compare reads 'not equal' and the bit is cleared, otherwise the process
   terminates — which of the two depends on an installed handler not seen in any dump (CONFIRMED — disassembly for the NULL path,
   job qohu; OPEN — the handler; 217/217 real rows supply the element)".

### §2.3

6. **Replace** "[2026-10-01, job `20261001T123123-team-a-ytgi`: the consumer confirms 'requirement-evaluate'; `spec-save-format.md`
   §7's 'get' is not supported by any dumped consumer — to be reconciled there once 0x00712330/0x00712380 are read (note for that
   document; not edited here)." **with** "[2026-10-01, jobs ytgi and qohu: 0x00712330 (int) and 0x00712380 (float) are read — slot 1
   takes (stat row, requirement) and returns a byte: condition 0 `at least` → value ≥ target, 1 `at most` → value ≤ target, any
   other condition → 0; the int variant narrows both operands to single precision before comparing. Slot 0 is the **update**
   handler: 0x00710CB0(stat id, value) calls it with (row, value) after snapshotting the progress of every achievement that
   references the stat, then evaluates and awards (0x00710C50 → 0x007108A0) and raises the HUD progress notice (0x007FBF70) when the
   count crosses a multiple of `HudUpdateFrequency`. `spec-save-format.md` §7's 'get' for slot 1 is wrong — note for that document.
   CONFIRMED — disassembly.]" Keep the sentence on slots 4/5 and slot 2 unchanged.
7. **Review status (2026-10-01), §2.3, append:** "Updated (job qohu): slots 0 and 1 read; naming settled."

### §3.1

8. **Replace** "**[OPEN — narrowed 2026-10-01 (job `20261001T123123-team-a-ytgi`): 0x00DAC9A0 → 0x00DC5AC0 → 0x00DC5A10 (file open) is
   CONFIRMED — disassembly; the archive-priority lookup of §14.2 must be reached from 0x00DC5A10 (not dumped). The edge 0x00713BB0 →
   0x00DAC9A0 is not dumped.]**" **with** "**[OPEN — narrowed again 2026-10-01 (job `20261001T173022-team-a-qohu`): the edge 0x00713BB0
   → 0x00DAC9A0 is a direct call with the `stats.xtbl` literal; 0x00DC5A10 builds the mode string `rb` and — because the gate byte
   0x02A07AD8 of its memory-resident-file path has no writer anywhere in the disassembly — always goes 0x00DAB0C0 → 0x00DAAD80, the
   general open-by-name (CONFIRMED — disassembly for five of six hops; 0x00DAAD80 not dumped). The archive-priority lookup of §14.2 is
   therefore at or below 0x00DAAD80.]**"
9. **Replace the §3.1 review-status line with:** "**Review status (2026-10-01), §3.1: re-derived from the executable 2026-10-01 (jobs
   ytgi, qohu), partially: 0x00713BB0 → 0x00DAC9A0 → 0x00DC5AC0 → 0x00DC5A10 → 0x00DAB0C0 → 0x00DAAD80 CONFIRMED — disassembly — not
   cleared: the by-name open inside 0x00DAAD80 (archive-priority lookup) is OPEN.**"

### §3.6

10. **Replace** "what consumes `HudUpdateFrequency` (the HUD progress bar — not traced);" **with** "`HudUpdateFrequency` is consumed by
    the stat update entry 0x00710CB0 (2026-10-01, job qohu): after each update of a stat it recomputes the progress of every
    achievement referencing that stat and shows the HUD progress notice (0x007FBF70) when the tracked count has crossed a multiple
    of `HudUpdateFrequency` since the previous value, unless the achievement is earned or its target is 1 (CONFIRMED —
    disassembly);".

### §4.1

11. **Append to the "[OPEN — 2026-10-01, narrowed by disassembly (job ytgi): …]" note:** "**[2026-10-01, job qohu:** the two save-block
    writers are read — entry 13, 0x00B982B0 (base ids at 0x4428, bitsets at 0x49A0/0x49CC/0x49F8, the §4.6 scalars) and entry 28,
    0x00B99810 (DLC ids at 0x15D90, bitsets at 0x16308/0x16334/0x16360, the `dlc`-named object list and the pending queue); both
    hand the order straight to 0x0071DBD0's index walk — **no sort anywhere on the save side**. 0x0071C6E0 is the reset of the
    restore scratch object (350-id side list plus three bitsets), called first by the restorer 0x0071FD30 and by 0x0071ED80 just
    before the row loop. The data test (a same-named patch-archive copy) remains the next step. CONFIRMED — disassembly.]**"

### §4.6

12. **`0x4A60` row, append:** "*(2026-10-01, job qohu: the entry-13 save writer 0x00B982B0 copies player `+0x1B70` — the field the
    melee setters write — into 0x4A60. CONFIRMED — disassembly on the save side.)*"
13. **New row after `0x4A34`–`0x4A47`:** "| `0x4A48` | — | the sixth slot of the same player array (`+0x276C`, damage type index 5
    `script`), copied by the save writer although the unlockable apply rejects index 5 (job qohu) | CONFIRMED — the copy; meaning
    by position |". And a note at the table's end: "0x4418–0x4420, 0x4A58, 0x4A5C and 0x4A6C are not refreshed by the entry-13
    writer; the save image is the live progression block (the respect award 0x00B97340 writes 0x4418–0x4420 in place), so those
    fields are written where they change (HIGH CONFIDENCE, job qohu; note for `spec-save-format.md`)."

### §5

14. **Replace** "Whether the saved level L is passed directly (so that 0x0060EB40(L) is the cost of the *next* level) is a consumer
    question — OPEN (0x007CFB60, 0x00B97340)." **with** "The saved level L is passed directly (2026-10-01, job qohu): the award
    0x00B97340 adds the amount to 0x4418 and, while the within-level respect 0x441C plus the amount reaches 0x0060EB40(L), increments
    L, carries the excess and re-reads the cost; the save/load menu builder 0x007CFB60 shows 0x441C ÷ 0x0060EB40(L) as the level
    progress and 0x0060EBB0(L) as the banked total. **So L = levels gained, and record L (0-based; row L+1) is the cost of the next
    level.** With the real values (row 50's `Respect` = 0) L passes through 49 without stopping, and nothing caps L at 50.
    CONFIRMED — disassembly."
15. **§5.3: strike** "*(2026-10-01: capacity store and 0-based accessor indexing settled, §5.2; the saved-level mapping is still OPEN.)*"
    and **replace the §5 review-status line with:** "**Review status (2026-10-01), §5: re-derived from the executable 2026-10-01
    (jobs ytgi, qohu): CONFIRMED — disassembly for the capacity store, the accessors and the saved-level → record mapping — cleared
    for implementation.**"

### §6.1

16. **Replace** "an absent police element's effect depends on the copy helper 0x00DABA70, not yet dumped (OPEN; real data always
    supplies both)" **with** "an absent police element leaves the shared buffer holding the gang text, so the police flag inherits
    the gang value — 0x00DABA70 never writes when the child is absent (2026-10-01, job qohu, CONFIRMED — disassembly for the helper;
    real data always supplies both)". **Review status, replace** "residual OPEN on the absent-element behaviour of 0x00DABA70 (does
    not arise on real data)" **with** "absent-element behaviour settled (job qohu)".

### §10.1

17. **Append to the 2026-10-01 capacity note:** "*(Job qohu: the window 0x022CD108–0x022CE000 is uniformly zero in the file image — the
    record array is runtime-filled data, so a range dump cannot bound it; the extent stays OPEN and is not an implementation input;
    a host should not cap the row count below the save's 40.)*"

### §10.6

18. **Append to the `Spline_Type` CONFIRMED note:** "*(2026-10-01, job qohu: the table at 0x01312708 holds exactly six `.rdata` pointers
    — 0x01190654, 0x011388F8, 0x0119064C, 0x01190644, 0x0118468C, 0x0116F410 — followed by a non-pointer byte; the strings behind them
    were not dumped, so the six names stand as previously read. Next: `ptrs 0x01312708 6`.)*"

### §10.7

19. **In the "Storage (2026-10-01 …)" paragraph, replace** "`Max_Booze_Points` at 0x02625828 + 4·slot (preset 120.0), `Max_Time_Drunk`
    at 0x02625834 + 4·slot (preset 100.0)" **with** "**`Max_Booze_Points` at 0x02625834 + 4·slot, `Max_Time_Drunk` at 0x02625828 +
    4·slot** (CORRECTED 2026-10-01, job qohu, from the row parser 0x00975170; by that correction the initialiser's presets are
    `Max_Time_Drunk` 120.0 and `Max_Booze_Points` 100.0)"; **replace** "not the 15 table records, which HYPOTHESIS places at
    0x02625878 + 0x118·slot + 0x38·level" **with** "not the 15 table records, which the parser places at **0x02625878 + 0x118·slot +
    0x38·level** (CONFIRMED — disassembly, job qohu: it resets all five records of the slot to the 0.0 / 1.0 / 1.0 / 2000 / zeros
    preset before reading, keeps at most five `Level` children, and takes the slot index 0…2 in a register)".
20. **Replace** "**[OPEN — 2026-10-01: the table-record base, the slot register and whether/where `Freerunning_fail_pct` is read are
    settled only by the row parser 0x00975170, not yet dumped.]**" **with** "**[2026-10-01, job qohu: 0x00975170 read — base
    CONFIRMED, slot in a register (0…2), `Sleepy` at `+0x34` CONFIRMED, and `Freerunning_fail_pct` is not among the parser's
    eighteen element literals: unread, dead authoring data (CONFIRMED — disassembly for the parser).]**" In the tree, change the
    `Freerunning_fail_pct` line's "whether and where it is read is OPEN" to "not read by the loader (job qohu)".
21. **Replace the §10.7 review-status line with:** "**Review status (2026-10-01), §10.7: re-derived from the executable 2026-10-01 (jobs
    ytgi, qohu): CORRECTED — the two scalar destinations are swapped relative to the ytgi text; CONFIRMED — disassembly for the
    record base, the slot register, `Sleepy` at `+0x34` and the complete read list (`Freerunning_fail_pct` unread) — cleared for
    implementation.**"

### §10.9

22. **Replace** "**[OPEN — 2026-10-01 (job `20261001T123123-team-a-ytgi`): 0x00BB70B0 confirmed to read only the three elements
    (disassembly; it does not free the document); the file-name literal's other users and the plain-open helper's 17 call sites
    (notably 0x00BB6400, 0x00BA5DA0 near the sky/time-of-day code) are not dumped.]**" **with** "**[2026-10-01, job
    `20261001T173022-team-a-qohu`:** the literal 0x0118CF40 has exactly one user (0x00BB70B0). **The five extra elements are read by
    0x00BB6400** (time-of-day module; callers 0x00B9D3F0): `horizon_mountain_enabled` (bool, default true → byte 0x029152F0),
    `fog_camera_follow` (bool, default false → 0x029152F1), `day_begin` (int, default 600 → 0x029152F4), `day_end` (int, default
    1800 → 0x029152F8), `tod_segments` → repeated `segment` (one dword each through 0x00DC5240 → array 0x029152FC, capacity
    0x02915300, count 0x02915304), plus the two mesh names again and three children of `cloud_mesh_filename` —
    `cloud_mesh_horizon_mat` (default `Cloud_base_material`), `cloud_mesh_overhead_mat` (`Cloud_overhead_material`),
    `cloud_mesh_skyline_mat` (`m_sky_matte_01`) — into 64-byte globals 0x02915388…0x02915488. Its bool reader 0x00DC51B0 accepts
    `yes`/`true`/`1`. CONFIRMED — disassembly for the reads; **OPEN** — the file name 0x00B9D3F0 passes (HIGH CONFIDENCE: the same
    `default_global.xtbl`, since the element set is exactly §14.14's). 0x00BA5DA0 is the time-of-day data loader, unrelated.]**"
23. **Replace the §10.9 review-status line with:** "**Review status (2026-10-01), §10.9: re-derived from the executable 2026-10-01 (jobs
    ytgi, qohu): loader CONFIRMED; the reader of the five extra elements (0x00BB6400) CONFIRMED — disassembly — not
    cleared: which file name its caller 0x00B9D3F0 passes is OPEN (HIGH CONFIDENCE the same file); nothing else in the unit is
    open.**"

### §10.11

24. **Replace** "what 0x009DC780 passes is not dumped" **with** "0x009DC780 pushes the literal `Single Player` (CONFIRMED — disassembly,
    job qohu), so the `Multiplayer` row is dead data (CONFIRMED, upgraded)".

### §10.12

25. **Replace** "**[OPEN — desk review 2026-09-30: byte `+0x43` lists bits 0, 1, 3–7 with no bit 2; whether bit 2 is unused or a flag
    string was missed is to be settled against the executable (`FUN_006174C0`).]**" **with** "*(2026-10-01, job qohu: 0x006174C0 read in
    full — the twelve `Disable_flags` strings are exactly those in the table; bit 2 of `+0x43` is written by none of them — never
    set from data, though the consumer 0x0061A900 tests it (masks 0x04 and 0x0C), so a host that reproduces the reader leaves it
    clear. Also CONFIRMED: `+0x42` bit 3 = framework ≠ `main`; `Unlockable` children are read only under `Unlockables`; the type
    index is not range-checked; `+0x4C` of each record is a per-type instance counter rebuilt at every load.)*" In the `+0x43` row of the bit table, insert
    "2 *(no flag string — never set from data; tested by 0x0061A900, job qohu)*" between bits 1 and 3.
26. **Replace the two §10.12 review-status lines with:** "**Review status (2026-10-01), §10.12: re-derived from the executable
    2026-10-01 (job qohu): CONFIRMED — disassembly (full element and flag vocabulary; bit 2 unused) — cleared for implementation.**"

### Review status summary (header paragraph)

27. **Append:** "**Update 2026-10-01 (job `20261001T173022-team-a-qohu`, `review/exe-notes-2026-10-01/interp_qohu.md`):** §5, §10.7
    (CORRECTED: scalar destinations swapped; `Freerunning_fail_pct` unread) and §10.12 cleared; §3.1 narrowed to its last hop
    0x00DAAD80; §4.1's save-side writers read (no reorder; data test pending); §10.9's reader of the five extra elements found
    (0x00BB6400; file name OPEN). Totals now: 8 DESK-PASS, 7 DESK-PASS with text fixes, 15 cleared after re-derivation, 3 re-derived
    but not cleared (§3.1, §4.1, §10.9), 0 NEEDS-EXE, 4 NEEDS-DATA, 1 VALIDATED-BY-DATA (38)."

### §13.1 / §13.2 (consolidated open items) — added on the resume pass; these lines still carry the pre-qohu wording

28. **§13.2 item 3, replace** "`HudUpdateFrequency` consumer (still untraced);" **with** "~~`HudUpdateFrequency` consumer (still
    untraced)~~ **RESOLVED 2026-10-01 (job qohu, §3.6): the stat update entry 0x00710CB0, which raises the HUD progress notice when
    the tracked count crosses a multiple of it;**".
29. **§13.2 item 10, replace** "the slot-index register (still OPEN); record offsets inside a level record beyond the read order."
    **with** "~~the slot-index register (still OPEN)~~ **RESOLVED (job qohu, §10.7: the slot 0…2 arrives in a register); the
    level-record offsets are all placed (`+0x00`…`+0x34`, §10.7).**" And **append to** "every real `Level` record carries an
    undocumented `Freerunning_fail_pct` element not in §10.7's tree." the words "**— not read by the loader (job qohu).**"
30. **§13.2 item 2, replace** "Handler-table slots 2–5 and the 51 handlers' semantics remain OPEN;" **with** "Handler-table slots 0
    and 1 are *update* and *requirement-evaluate* (job qohu, §2.3); slots 2–5 and the 51 handlers' semantics remain OPEN;".
31. **§13.2 item 11, replace** "A `Name` that matches no record indexes before the array (still not exercised)." **with** "A `Name`
    that matches no record indexes before the array (the reader has no range check — CONFIRMED, job qohu; still not exercised by
    data)."
32. **§13.1 `0x4A60` bullet, append:** "*(2026-10-01, job qohu: the entry-13 save writer 0x00B982B0 copies player `+0x1B70` into
    0x4A60 — the melee-multiplier reading is CONFIRMED on the save side; 0x4A58 and 0x4A6C are not refreshed by that writer, §4.6.)*"

### Notes for other documents (not edited)

- `spec-save-format.md` §7 item 4: stat handler slot 1 is *requirement-evaluate* (row, requirement) → truth, not *get*; slot 0 is
  *update* (row, value). §10.6: 0x4A60 ← player `+0x1B70` confirmed on the save side; 0x4A48 ← player `+0x276C` (damage slot 5); the
  entry-13 writer does not refresh 0x4418–0x4420, 0x4A58, 0x4A5C, 0x4A6C — they are written in place.
- `spec-lua-api-behaviour.md` §26.28 (tutorial name 176): the string at 0x01124348 is `dlc`.
- `spec-tables-environment.md` (sky / time of day): 0x00BB6400's full element list and globals (Q12.2), including the three cloud
  material names and the `tod_segments`/`segment` list.

## Next dumps (job `20261001T173022-team-a-qohu`)

In order of payoff.

1. **§3.1 last hop:** `func 0x00daad80` at depth 1, then `xref` of whichever list head it reads first (the §14.2 priority list) —
   closes the loader-to-dispatcher chain.
2. **§10.6 names:** `ptrs 0x01312708 6` (or `bytes` of 16 at each of `0x01190654`, `0x011388f8`, `0x0119064c`, `0x01190644`,
   `0x0118468c`, `0x0116f410`) — closes the `Spline_Type` list by reading, not by inheritance.
3. **§10.9 file name and units:** `func 0x00b9d3f0` and `0x00b9d190` (what names they pass to `0x00bb6400` / `0x00ba5da0`); `func
   0x00dc5240 0x00dc5270` (the `segment` and int readers); `xref 0x029152f4 0x029152fc` (who consumes `day_begin`/`day_end` and the
   segments — gives their units).
4. **§4.1 order:** not a dump — extract `unlockables.xtbl` from `patch_compressed.vpp_pc` / `patch_uncompressed.vpp_pc` and compare
   its row order with the 16 saves; optionally `func 0x0071ed80` (the loader-side reset caller) and `0x00b83b80` (the 16-byte block
   at 0x4f5c).
5. **§2.2 absent element:** `func 0x00ea2742` at depth 1 (the invalid-parameter routine — find the handler global), then `xref`
   that global for a `_set_invalid_parameter_handler`-style writer. Low priority: 217/217 real rows supply the element.
6. **§10.1 extent:** a listing of every defined or referenced address in `0x022cd108`–`0x022db98c` (if `CrreishDump.java` has no
   such mode, add `labels <lo>-<hi>`). Low priority: not an implementation input.
7. **§10.7 slot order:** `func 0x00975520` — confirms slots 0/1/2 = `drunk`/`weed`/`escort tiger` by reading rather than by the
   ytgi inference. Low priority.
8. **§10.12 bit-2 consumer:** `func 0x0061a900` at depth 1 — what the engine does when `+0x43` bit 2 (and the `0x0c` pair with
   bit 3) is set, and whether any pointer-relative writer sets it; turns "live flag with no data path" from an xref inference into
   a read consumer. Low priority: unreachable from shipped data.

## Summary of labels

| item | label |
|---|---|
| §1.3 bool reader: `true`/`yes` → 1, `false`/`no`/other → 0; bounded by the callers' 0x400 | CONFIRMED — disassembly |
| §1.3/§6.1 copy helper `0x00daba70` never writes when the child is absent or textless; police `Check_Detection` inherits the gang text when absent | CONFIRMED (helper) / HIGH CONFIDENCE (shared buffer not cleared, from ytgi) |
| §10.7 drunk parser: records at `0x02625878` + 0x118·slot + 0x38·level; slot in a register (0…2); `Sleepy` at `+0x34`; ≤ 5 levels | CONFIRMED — disassembly |
| §10.7 `Max_Booze_Points` → `0x02625834`, `Max_Time_Drunk` → `0x02625828` (+ 4·slot) | CORRECTED — disassembly |
| §10.7 `Freerunning_fail_pct` unread | CONFIRMED (this parser) / HIGH CONFIDENCE (no other reader) |
| §10.12 activity reader: full vocabulary as in the spec; bit 2 of `+0x43` set by no flag string; `+0x42` bit 3 = framework ≠ `main`; `+0x4c` = instance counter | CONFIRMED — disassembly |
| §10.12 bit 2 of `+0x43` is tested by the consumer `0x0061a900` (masks `0x04`, `0x0c`) — live in code, no data path | CONFIRMED (xref: two referencing functions) / HIGH CONFIDENCE (no other writer) |
| §3.1 chain `0x00713bb0` → `0x00dac9a0` → `0x00dc5ac0` → `0x00dc5a10` → `0x00dab0c0` → `0x00daad80` | CONFIRMED (five hops) / OPEN (`0x00daad80`) |
| `0x00dc5a10`'s memory-resident path is gated by `0x02a07ad8`, which nothing writes | CONFIRMED (xref) / HIGH CONFIDENCE (dead path) |
| §2.2 absent `Allow_Update_By_Server`: NULL into `_stricmp` → invalid-parameter routine; cleared bit if it returns | CONFIRMED (path) / OPEN (handler) |
| §2.1–§2.4 stats loader details (unknown name ends the load; case-sensitive denominator scan over all 217 slots; complex jump table) | CONFIRMED — disassembly |
| §10.11 `0x009dc780` pushes `Single Player`; `Multiplayer` row dead | CONFIRMED — disassembly + xref |
| §5 saved level L = levels gained; record L (row L+1) = next level's cost; L passes through 49; no cap at 50 | CONFIRMED — disassembly |
| §5 menu builder `0x007cfb60` uses the same convention (progress = 0x441c ÷ cost(L); banked = Σ 0…L−1) | CONFIRMED — disassembly |
| §2.3 slot 1 = requirement-evaluate (`at least` ≥, `at most` ≤, else 0; int narrowed to float) | CONFIRMED — disassembly |
| §2.3 slot 0 = update, called by `0x00710cb0`(id, value); HUD notice every `HudUpdateFrequency` increments | CONFIRMED — disassembly |
| `0x014f71d4` = "cheat used" gate, `0x014f3d34` = mode byte, both block stat updates | CONFIRMED (gate) / HIGH CONFIDENCE (names) |
| §1.4/§10.10 tweak reader filters on the handed framework; base pass = `main` | CONFIRMED — disassembly (upgrade) |
| §4.1 `0x0071c6e0` = reset of the restore scratch object (350-id side list + three bitsets) | CONFIRMED (writes) / HIGH CONFIDENCE (identity) |
| §4.1 entry 13 `0x00b982b0` and entry 28 `0x00b99810` are the id-block writers; both index-order via `0x0071dbd0`; no sort | CONFIRMED — disassembly |
| §4.1 base-order mechanism | OPEN — data test (patch copy) |
| §4.6 `0x4a60` ← player `+0x1b70`; `0x4a48` ← `+0x276c`; `0x4a58`/`0x4a5c`/`0x4a6c` not refreshed by entry 13 | CONFIRMED (copies) / HIGH CONFIDENCE (live block) |
| String at `0x01124348` (tutorial name 176) = `dlc` | CONFIRMED — xref dword + decompiler |
| §10.9 literal `0x0118cf40` has one user (`0x00bb70b0`) | CONFIRMED — xref |
| §10.9 `0x00bb6400` reads the five extra elements, three cloud materials, `tod_segments`/`segment`; bool reader accepts `yes`/`true`/`1` | CONFIRMED — disassembly |
| … and the file it opens is `default_global.xtbl` | HIGH CONFIDENCE — OPEN (`0x00b9d3f0`) |
| `0x00ba5da0` = time-of-day data loader, not a reader of the five elements | HIGH CONFIDENCE |
| §10.6 `Spline_Type` table = exactly six `.rdata` pointers | CONFIRMED — range |
| §10.6 names of entries 3–6 | OPEN (strings not dumped; spacing consistent with the spec) |
| §10.1 window `0x022cd108`–`0x022ce000` all zero (runtime-filled) | CONFIRMED — range |
| §10.1 array extent | OPEN — not an implementation input |

## Resume pass (2026-10-02)

The first pass was cut off by a rate limit after writing the summary table; nothing above it was lost. This pass did four things.

1. **Re-read the raw dumps behind the high-stakes claims instead of taking them on trust.** All as written: the §10.7 scalar swap
   (`PUSH "Max_Booze_Points"` at `0x00975396` → `0x02625834`, `PUSH "Max_Time_Drunk"` at `0x009753af` → `0x02625828`); the `no`
   compare whose result falls through to the zero return (`0x00dab893`–`0x00dab8a5`); the respect loop (`JL`/`JGE`, signed) and the
   `0x55f0` = 22000 sentinel for n ≥ 0x32; the entry-13 write list (stores to `0x4a24`…`0x4a54`, `0x4a60`, `0x4a64`, `0x4a68`,
   word `0x4a6e` — none to `0x4a58`, `0x4a5c`, `0x4a6c` or `0x4418`–`0x4420`); the slot-1 body's `XOR AL,AL` return on an unknown
   condition; the single reference to `0x02a07ad8`; the single user of `0x0118cf40`; the six pointers and the `0x08` byte at
   `0x01312720`; the dword `0x00636c64` (`dlc`) at `0x01124348`; the twelve `Disable_flags` shift/mask pairs (bits exactly as
   listed in Q4 — the stores are interleaved with the next literal's push, which is compiler scheduling, not a different bit); and
   the large range: 337 `db 0x00` lines plus 1,748 two-byte zero pairs, no other byte.
2. **Added the one fact the same dumps hold that the first pass did not use** — the consumer `0x0061a900` tests bit 2 of `+0x43`
   (Q4 addendum, spec item 25, next-dump item 8, one summary row). It refines §10.12's wording; it does not reopen the unit.
3. **Added spec items 28–32** for the §13.1/§13.2 consolidated open-items list, whose lines still say the `HudUpdateFrequency`
   consumer is untraced, the drunk slot register is OPEN and slots 0/1 unnamed; and made item 23 agree with item 27 on §10.9's
   status (re-derived, not cleared, one OPEN: the file name `0x00b9d3f0` passes).
4. **Clean-room check over the whole file** (first-pass text included): no Ghidra auto-named variable or type tokens; the only
   hits of a deliberately broad pattern were the English words "undefined code" in Q1 and Q8.2, which describe bytes the
   disassembler has not defined as a function and are not generated identifiers.

Cumulative status is unchanged from the Headline: eight questions settled outright, three settled with one residual each (§3.1
→ `0x00daad80`; §4.1 → data test; §10.9 → the file name), three OPEN (§10.6 names, §10.1 extent, §2.2's handler), two
corrections (§10.7 scalar swap; `spec-save-format.md` §7 slot 1).
