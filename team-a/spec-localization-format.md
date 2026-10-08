# Saints Row: The Third — `.le_strings` Localisation String Tables and Key Lookup Specification

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Scope:** The shipped localisation string-table file (`<table>_<LANG>.le_strings`: header, hash buckets, records), the character encoding its text uses (font character codes assigned through the `charlist_<LANG>.dat` files, which this unit also specifies), the localisation key hash, the game-side loader and its 14-slot table set, start-up language selection (the language code at `0x01301a90`), DLC table load/unload, the key-hash → text lookup and its wrappers, and the registration of that lookup into the Vint callback table that `spec-vint-render.md` §9/§14.2 calls. Vint's own side of the call (when `text`'s builder and the insertion formatter ask, and what they do with the answer) is `spec-vint-render.md` §14.2 and is cited, not re-derived. This unit answers the boundary `spec-vint-render.md` §14.2 step 2 leaves open, and the language-code question `spec-vint-font-format.md` §8 item 4 leaves open.
**Method:** direct read of `SaintsRowTheThird.exe`'s bytes (no Ghidra project): the project's VA reader `tools/harnesses/exe_va.py` plus a scratch capstone lister, a raw immediate-operand scan for every reference to the table addresses, and a raw `call`/`jmp` scan for every caller of the functions involved. All 165 `.le_strings` files and all 15 `charlist_*.dat` files were pulled from the shipped archives with a new mode-(a)-aware extractor, `tools/harnesses/locstr_extract.py` (same container walk as `find_cefct_big.py`; output in `tools/locstr_samples/`). The format was checked on every file, and the lookup replayed against the 159 distinct shipped `.vint_doc` documents, by `tools/harnesses/locstr_check.py`; the encoding was checked by `tools/harnesses/locstr_charlist.py`. Results reproduced that way are marked *replayed*.
**Cleanroom compliance:** no decompiled source, pseudocode, or internal variable names are quoted — plain hex addresses and offsets as evidence anchors only, as in every other spec here. File names, table names, language codes, localisation keys, format strings and localised texts are the game's own shipped data, quoted as data.
**Confidence key:** **CONFIRMED** (read directly in disassembly this pass, or reproduced over the real shipped files — the latter marked *replayed*), **HIGH CONFIDENCE** (inferred from structure), **HYPOTHESIS** (plausible, unchecked), **OPEN**.

---

## 1. Population

**Where the files are — CONFIRMED (*replayed*, `locstr_extract.py` over every non-audio archive):**

| Archive | Files | Content |
|---|---|---|
| `misc.vpp_pc` | 117 `.le_strings` + 15 `charlist_*.dat` | 13 tables × 9 languages (`cz`, `de`, `es`, `fr`, `it`, `nl`, `pl`, `ru`, `us`): `activity`, `customize`, `diversion`, `hud`, `menu`, `mission`, `multiplayer`, `platform_pc`, `platform_ps3`, `platform_xbox360`, `static`, `subtitle`, `voice_script` |
| `patch_compressed.vpp_pc` | 15 | `patch0` × 11 languages (the 9 above plus `JP`, `SK`); a replacement `hud` for `ES`, `FR`, `IT`, `RU` (same keys as the `misc` copy; 9, 2, 1 and 3 texts changed) |
| `dlc1.vpp_pc`, `dlc2.vpp_pc`, `dlc3.vpp_pc` | 11 each | `dlc1`/`dlc2`/`dlc3` × 11 languages (the 9 plus `jp`, `sk`) |

No other archive holds a `.le_strings` file. The name case differs between archives (`hud_us` in `misc`, `hud_ES` in the patch archive); the loader asks for upper-case codes (§5.2) and the archive lookup is case-insensitive (HIGH — the lower-case `misc` names are the ones that load).

**Records per table (US, the language this install runs, §5.3) — CONFIRMED (*replayed*):**

| Table | Records | Buckets | Table | Records | Buckets |
|---|---|---|---|---|---|
| `static` | 1,089 | 128 | `customize` | 3,060 | 512 |
| `hud` | 1,621 | 256 | `subtitle` | 675 | 128 |
| `activity` | 818 | 128 | `platform_pc` | 525 | 64 |
| `diversion` | 672 | 128 | `patch0` | 29 | 32 |
| `mission` | 739 | 128 | `dlc1` / `dlc2` / `dlc3` | 148 / 216 / 117 | 32 each |
| `menu` | 926 | 128 | `platform_ps3` / `platform_xbox360` | 132 / 101 | 32 each (never loaded on PC, §5.3) |
| `multiplayer` | 76 | 32 | `voice_script` | 1 | 32 (never loaded, §2.5) |

The eleven tables the PC game loads at start-up for US hold **10,230 records with 10,230 distinct key hashes** — no key appears in two tables, and the three DLC tables collide with none of them. The other eight languages carry the same tables with slightly different key sets (for example 1,171 `static` keys against US's 1,089; the full per-table differences are printed by `locstr_check.py`). The longest US text is 369 code units; 18 records hold an empty text.

---

## 2. On-disk layout

All values little-endian. The loader reads the whole file into one block and turns the offsets into pointers in place (§5.1), so the file doubles as the in-memory table.

### 2.1 Header (`0x0c` bytes)

| Offset | Type | Meaning | Shipped values | Evidence |
|---|---|---|---|---|
| `+0x00` | u32 | magic `0xa84c7f73` (bytes `73 7f 4c a8`) | all 165 | CONFIRMED — the loader rejects anything else (`0x00849bf0`, compare at `0x00849d19`) |
| `+0x04` | u16 | version; must be exactly **1** | 1 in all 165 | CONFIRMED (`0x00849d20`) |
| `+0x06` | u16 | bucket count `B`; **must be a power of two** — the loader stores `B − 1` as the bucket mask and never checks it | 32 / 64 / 128 / 256 / 512 | CONFIRMED (`0x00849d8a`–`0x00849d9f`) |
| `+0x08` | u32 | record count | equals the number of records reachable from the buckets in 161/161 well-formed files | CONFIRMED *replayed*; **not read by the loader** (the code steps from `+0x06` straight to `+0x0c`) |

### 2.2 Bucket table (`8·B` bytes at `+0x0c`)

`B` entries of (u32 count, u32 offset). A non-empty bucket's offset is the **file offset of an array of `count` u32 record offsets**; an empty bucket has count 0 and offset `0xffffffff` in every shipped file (the loader skips an empty bucket without touching its offset). A record lives in bucket `hash & (B − 1)` — CONFIRMED for every record of the 161 well-formed files (107,435 records, *replayed*), and it is the only rule the lookup uses (§6.1).

### 2.3 Record-offset arrays and records

- **Offset arrays** start right after the bucket table (`0x0c + 8·B`) and are packed back to back in bucket order (161/161, *replayed*). Each entry is the file offset of one record.
- **Records** start right after the last array and run to end-of-file with no padding between them (all 107,274 adjacent pairs are contiguous; the record area ends exactly at end-of-file in 161/161). Records are not stored in bucket order (only the one-record file happens to be), and they are 2-byte aligned only — the lookup reads the hash unaligned.
- **One record** = u32 key hash (§4), then the text as **16-bit code units, terminated by a 0 unit**. The text is returned in place; nothing records its length. The code units are font character codes, not Unicode, outside English (§3).

| Offset in record | Type | Content |
|---|---|---|
| `+0x00` | u32 | key hash |
| `+0x04` | u16[] | text, 0-terminated |

**For a writer:** the loader checks only the magic, the version and that a slot is free; it does no bounds check on any count or offset (CONFIRMED). The shipped bucket counts fit "the largest power of two not above records ÷ 5, but at least 32" on every file (HYPOTHESIS for the original tool's rule — any power of two works for the game).

### 2.4 Worked example (`voice_script_us.le_strings`, 302 bytes, read directly)

Header: magic, version 1, `B` = 32, count 1. Bucket 9 = (1, `0x10c`); the other 31 are (0, `0xffffffff`). At `0x10c` the single array entry `0x110`. At `0x110`: hash `0x3893e529` (`& 31` = 9), then `I need that!` and its 0 unit, ending at byte 302 = end-of-file.

### 2.5 Shipped anomalies

- **Four big-endian files.** `voice_script_cz`, `_nl`, `_pl`, `_ru` (`misc.vpp_pc`, 378–463 KB, 4,069 records, `B` = 512) have a little-endian header and bucket table but **big-endian records**: the hash and the text are byte-swapped (the hash swapped back lands in the right bucket for 4,069/4,069 records; the text decodes as big-endian UTF-16, for example `A doprdele.`, `O shit...`). Loaded as-is, none of their keys could ever match. CONFIRMED (*replayed*).
- **`voice_script` is never loaded.** The executable contains no `voice_script` string at all; it is not in the start-up list (§5.3), so all nine `voice_script` files (including the 1-record US file) are inert shipped data. CONFIRMED (byte search of the executable).
- **`platform_ps3`/`platform_xbox360`** are never loaded on PC (§5.3); 87 and 85 of their keys duplicate `platform_pc` keys.

---

## 3. Text encoding: font character codes and `charlist_<LANG>.dat`

### 3.1 The stored code units are font codes — CONFIRMED (*replayed*)

The text in a record is drawn directly: the glyph drawer takes each code unit as a font character code (glyph index = code − the font's first character, `spec-vint-font-format.md` §2.1). Outside English the shipped text is therefore **not Unicode** above U+00FF:

| Language files | Highest code unit in any record | Highest code the language's font has (`spec-vint-font-format.md` §1, §4.2) |
|---|---|---|
| `cz`, `de`, `es`, `fr`, `it`, `nl`, `pl`, `ru` | `0x016f` | `0x016f` — `font_body`/`font_header_pc`: 336 glyphs from `0x20` |
| `sk` (DLC and `patch0` only) | `0x0660` | `0x0666` — `font_sk`: 1,607 glyphs from `0x20` |
| `jp` (DLC and `patch0` only) | `0x086d` | `0x08af` — `jap`: 2,192 glyphs from `0x20` |
| `us` | `0x2122` | `0x016f` (see below) |

The range `0x0100`–`0x016f` of the default fonts is a **private code page shared by Czech, Polish, Russian and the Western languages**: in the Russian files `0x0145` is `а`, `0x0154` is `п` and so on, so `hud_ru`'s text reads as Latin-Extended garbage if taken as Unicode.

### 3.2 `charlist_<LANG>.dat` — the code-page definition

Fifteen plain-text files in `misc.vpp_pc`: `us`, `de`, `es`, `fr`, `it`, `nl`, `cz`, `pl`, `ru`, `se`, `dk` (eleven byte-identical copies, 1,485 bytes), `jp` (14,507), `sk` (10,392), `ko` (901), `ar` (1,982). Read by `0x0084a460` (CONFIRMED):

1. Leading lines that begin with `/` are skipped (comments; the shipped headers name their generator, and **`charlist_sk.dat` says `LANGUAGE=Korean`**).
2. The next line is parsed with the format `count=%u` (`0x0116425c`).
3. Then up to `count` lines, each parsed with `%hu` (`0x01164258`): a **Unicode code point in decimal**.
4. Each entry becomes a 4-byte pair (u16 Unicode, u16 font code). The font code is the code point itself when it is below `0x100`; otherwise it is the **next free code from `0x100` upwards**, in file order.
5. The pairs land in a buffer from the `char_list_pool` pool (`0x01164268`); pointer at `0x0242dbc8`, count at `0x0242dbcc`. A missing file leaves both 0. A short file (fewer entries than `count`) is rejected.

| File | `count` | Last font code assigned |
|---|---|---|
| shared Western/Central-European/Russian list | 266 | `0x016f` (the last entry is U+2122 `™`) |
| `charlist_sk.dat` (Korean) | 1,514 | `0x0666` |
| `charlist_jp.dat` | 2,104 | `0x08b1` (two codes beyond the `jap` font's last glyph) |

**Decoding the shipped text through these lists gives correct text in every case tested — CONFIRMED (*replayed*, `locstr_charlist.py`).** Every code unit of `hud_ru`, `menu_ru`, `menu_cz`, `menu_pl`, `menu_de`, `dlc1_sk` and `dlc1_jp` is in the list except line feeds. For example, `MENU_GAME_PAUSED` decodes as `ИГРА ПРИОСТАНОВЛЕНА` (ru), `HRA POZASTAVENA` (cz), `GRA ZATRZYMANA` (pl) and `SPIELPAUSE` (de); `dlc1_sk` decodes as Hangul (`묵시록 겐키`) and `dlc1_jp` as Japanese (`ゲンキ博士の黙示録`).

**The English files are the exception.** `us` text contains raw Unicode above `0x00ff`, mostly in character-set test strings: `Ą`…`Ž`, Cyrillic `Ё`/`А`…, and U+2122. Drawn as font codes, those characters come out as the wrong glyph or a missing glyph (HIGH — follows from §3.1; not checked visually).

### 3.3 Where the charlist is used (runtime text the game did not author)

The lists convert text from outside the string tables — player names, typed input — into font codes (CONFIRMED in outline, by reading):
- `0x00849ae0` maps one code point to a font code. Four characters below `0x7f` are always mapped to `_` (`0x5f`) by a fixed table at `0x011640d4` (count `0x01301a94` = 4): `%`, `[`, `\` and `{`. These are exactly the characters that start the insertion syntax and markup of `spec-vint-render.md` §14.2/§14.4. Other characters from `0x7f` up are looked up in the pair list. A character not in the list becomes `_` and, if asked, sets the flag `0x0242dbc5`.
- `0x008499d0` converts a buffer in place, and `0x0084a030` copies while converting, reporting whether any `_` was substituted. `0x00849aa0` maps a font code back to Unicode.
- When the flag is set, `0x0084a340` raises the `MENU_TITLE_WARNING` / `FOREIGN_CHARACTER_DETECTED_WARNING_MESSAGE` dialog. The US text of that message is `A player has an ID with unsupported characters. These characters now appear as "_".`

**For a replacement:** feed the stored code units to the font unchanged — that is what the game does. To show the text as Unicode (a modern font, a log, a tool), decode it through the charlist of the active language. Run user-supplied text through the same mapping, including the four `_` substitutions.

---

## 4. The key hash — CONFIRMED

A key is hashed by `0x00d9e740(key, 0)`, the lower-casing entry point of the engine's reflected CRC-32. This is the same table (`0x01320da0`, entry 1 = `0x77073096`) and the same per-character step as the length-bounded `0x00d9e8b0` that `spec-vint-font-format.md` §4.1 and `spec-extensionless-types.md` call the engine string hash. For each byte: lower-case it (`0x00ea739d`, which under the default "C" locale changes only `A`–`Z`), then `h = (h >> 8) XOR table[(h XOR byte) & 0xff]`. Start value 0 (the seed argument), no final inversion. So it is **not** zlib's `crc32`, which starts at `0xffffffff` and inverts at the end. It is *not* the rotate-6/XOR hash `0x00da7890`.

Consequences, all *replayed*:
- Keys are **case-insensitive**: `CONTROL_OKAY` and `control_okay` hash alike.
- The five keys the executable itself spells out resolve against the shipped US tables to the expected texts: `CONTROL_OKAY` → `OK` (`static`, hash `0xda87172c`), `CONTROL_YES` → `YES`, `CONTROL_NO` → `NO`, `MENU_TITLE_WARNING` → `WARNING` (`menu`), and `FOREIGN_CHARACTER_DETECTED_WARNING_MESSAGE` → the message quoted in §3.3 (`multiplayer`).
- The same function hashes Vint property names (`spec-vint-doc-format.md` §5), and the hash of `text_tag` computed this way finds every one of the 513 `text_tag` records in the 159 documents (§8).
- **Hash 0 means "no key"**: the lookup returns nothing for 0 without searching (§6.1), and the empty string hashes to 0.

---

## 5. Loading

### 5.1 The table set and the loader `0x00849bf0(name, allocator)` — CONFIRMED

The game keeps **14 slots** of `0x54` bytes at `0x02319720` (to `0x02319bb8`):

| Slot offset | Content |
|---|---|
| `+0x00` | table name, up to 64 characters (as passed: `static`, `platform_PC`, `patch0`, a DLC name) |
| `+0x40` | loaded flag (byte) |
| `+0x44` | bucket count `B` |
| `+0x48` | pointer to the bucket table (file buffer + `0x0c`) |
| `+0x4c` | byte: 1 when the caller supplied its own allocator, 0 for the default one at `0x0242dbd8` (HIGH that this marks who owns the buffer) |
| `+0x50` | bucket mask `B − 1` |

The loader:
1. Builds the file name with `%s_%s.%s_strings` (`0x0116417c`), giving the table name, the active language code string (§5.2; the empty string if the code is negative) and `le` (`0x01164190`). For example `hud_US.le_strings`.
2. Opens it (`rb`), takes its size, allocates that many bytes from the allocator and reads the file. A missing file, an empty file or a read error returns false.
3. Checks the magic and the version (§2.1). It takes the **first free slot** in slot order; with none free, or on a bad header, it frees the buffer and returns false.
4. Copies the name, then for every bucket with a non-zero count adds the buffer address to the bucket's offset and to every entry of its array. Each offset becomes a pointer in place; the record bytes are untouched.
5. Sets the loaded flag and returns true.

**Unloading** (`0x00849870(name)`) finds the slot by name and only clears its loaded flag. Freeing the buffer is the caller's business (HIGH — the only caller is the DLC path, §5.4, whose allocator owns it).

### 5.2 Language selection — CONFIRMED (closes `spec-vint-font-format.md` §8 item 4)

**The language code** is `0x01301a90`: an index into a 14-entry table of two-letter codes at `0x0116409c`. Its initial value in the data section is `0xffffffff`. `0x00849940` returns it and `0x00849920` returns its code string. Only `0x0084a390` and `0x00849e10` ever write it (every reference was scanned).

| Code | String | Platform language name mapped to it | Can be active on PC | Base tables shipped |
|---|---|---|---|---|
| 0 | `US` | `english` (and anything unrecognised) | yes | yes |
| 1 | `ES` | `spanish` | yes | yes |
| 2 | `IT` | `italian` | yes | yes |
| 3 | `JP` | `japanese` | **no** | no (DLC/`patch0` only) |
| 4 | `DE` | `german` | yes | yes |
| 5 | `FR` | `french` | yes | yes |
| 6 | `NL` | `dutch` | yes | yes |
| 7 | `SE` | `swedish` | no | no |
| 8 | `DK` | `danish` | no | no |
| 9 | `CZ` | `czech` | yes | yes |
| 10 | `PL` | `polish` | yes | yes |
| 11 | `SK` | **`korean`** | **no** | no (DLC/`patch0` only) |
| 12 | `RU` | `russian` | yes | yes |
| 13 | `CH` | (none) | **no** | no |

**Start-up** (`0x0084a390`, called once from `0x005d23db`). This runs just before the font set-up `0x00b9cab0`, which is called from `0x005d23e0` (`spec-vint-font-format.md` §4.2), so the language code is final before the fonts read it.
1. Clear all 14 loaded flags.
2. If the code is still −1, detect it (`0x00849660`). The platform's current-language name is read through the `steam_api.dll` import `SteamApps` (import slot `0x0101c580`; the method is the interface's slot `+0x10`, HIGH that this is its current-game-language query). The name is compared as a whole string, terminator included, with the 13 names in the table above, and the matching index is taken; no match, or no interface, gives 0. **The result is then kept only if its code appears in the supported-language string `US DE ES FR IT NL CZ PL RU` (`0x011640e4`); otherwise it becomes 0 (`US`).**
3. Choose the language string. A start-up override at `0x0242dbd0` wins if it is non-empty: up to 3 characters, stored by `0x00849900`, whose only caller passes a start-up option value from `0x005d2268` (HIGH that this is a command-line/config option). Otherwise the code's own string is used.
4. Mark start-up done (`0x0242dbc4` = 1) and call `0x00849e10` with that string. It:
   - sets the code to −1, upper-cases the first two characters and looks for them in the same supported string;
   - if found, sets the code to that string's index in the 14-entry table (`0x008498c0`);
   - **if not found, sets the code to 0 and returns without loading anything.** So an override naming an unsupported language leaves the game with no string tables at all, and every lookup fails;
   - otherwise resets the font state (`0x00dcada0`, `0x00b9bbb0` — which empties the game's named font table, `spec-vint-font-format.md` §4 — and `0x00dcad50`) and flushes a cache at `0x01495604` (HIGH: resource/packfile related);
   - loads, with the default allocator, in this order: the 9 names at `0x01301a98` (count `0x01301abc` = 9): `static`, `hud`, `activity`, `diversion`, `mission`, `menu`, `multiplayer`, `customize`, `subtitle`; then `platform_PC` (built with `platform_%s` and `PC`); then `patch0` if the byte `0x0149365d` is set (written to 1 by the start-up routine `0x005d1a30`; HIGH that it means "patch packfiles in use", since `0x005d24b3` tests it before mounting `patch` archives);
   - finally reads `charlist_<code>.dat` (`0x00849b60`, §3.2).
5. Resolve three fixed button texts with `0x0084a280` (§6.3): `CONTROL_OKAY` (fallback `OK`), `CONTROL_YES` (`Yes`) and `CONTROL_NO` (`No`), into `0x01301a80`/`0x01301a84`/`0x01301a88`. Then call `0x0073ce70` with "code ≠ 0" (not read).

**So on PC the language code is always one of 0, 1, 2, 4, 5, 6, 9, 10, 12.** Codes 3 (`JP`), 11 (`SK` = Korean) and 13 (`CH`, Chinese per its font name `fnt_chinese`) can never be set — not by Steam's language and not by the override — without changing the executable or memory. This is the same supported set as the nine languages of base tables in `misc.vpp_pc`. Russian, Czech and Polish run in the default font arrangement through the private code page of §3.1.

### 5.3 What is loaded in this install

Eleven slots at start-up, for the active code (US here): the nine named tables, `platform_PC`, and `patch0` from the patch archive. A language whose `hud` is also in `patch_compressed.vpp_pc` (`ES`, `FR`, `IT`, `RU`) gets the patched copy: `hud_ES.le_strings` is resolved by the packfile layer, not by this loader (HIGH — the loader only asks for a name). `platform_ps3`, `platform_xbox360` and `voice_script` are never requested.

### 5.4 DLC tables — CONFIRMED

`0x0084a0f0` walks a list of content packages (entries with a status byte at `+0x101` that is neither 0 nor `0xff` and flag bit 2 of `+0x103`). For each one it calls the loader with the package's name at `+0x80` and its own allocator (`0x0293cbd8`). `0x0084a170` unloads the same names. That this name is `dlc1`/`dlc2`/`dlc3`, matching the shipped files, is HIGH. **Eleven base tables plus three DLC tables fill the 14 slots exactly.** A fifteenth table would fail to load (no free slot).

---

## 6. The lookup

### 6.1 `0x00849950(hash)` → text — CONFIRMED (read in full; *replayed* over every shipped key)

- **Hash 0** returns null at once.
- Otherwise the 14 slots are searched **in slot order**, skipping any not loaded. In each slot: bucket = `hash & mask`, then the bucket's array is scanned linearly, comparing each record's u32 hash. **The first match, in the first slot that has it, wins.** The result is a pointer to that record's text (record + 4), inside the loaded file buffer — no copy and no conversion.
- No match in any slot returns null. The function writes nothing: **there is no cache**. Every call repeats the search, which is cheap (at most one bucket of at most 18 records per table in the shipped data).
- Slot order is load order (first free slot), so base tables are searched before DLC ones. With the shipped keys unique across all US tables (§1), the order never changes a result.
- **An empty text is a hit.** 18 shipped keys return a non-null pointer to an empty string. Vint tests only for null (`spec-vint-render.md` §14.2, the test at `0x00e31cb3`), so such a key draws nothing rather than falling back to the raw string (HIGH — the null test was read; nothing else in that path was).

### 6.2 Wrappers

| Address | Input → output | Behaviour |
|---|---|---|
| `0x00849df0` | key string → text | null string → null; else hash (§4) + `0x00849950` |
| `0x00849fa0` | key string → hash | null string → 0; else the hash **if `0x00849950` finds it, otherwise 0** |
| `0x00849fd0` | hash → text | `0x00849950`, else the fixed text `!!Unrecognized hash value` (`0x011641a8`) |
| `0x0084a1b0` | key, wide fallback → text | found → the table text; otherwise a new text is built and interned in the string pool `0x0242dc28` (`0x00db0ba0`), and the pool's copy is returned. The new text is `!!` + the fallback (up to 254 units), or, with no fallback, `!!` + the key passed through the "title-case" pass `0x00849800` |
| `0x0084a280` | key, narrow fallback → text | same as `0x0084a1b0`, but the fallback is a byte string, widened first |

**The title-case pass** `0x00849800` (CONFIRMED by reading) works in place on the whole `!!`-prefixed buffer:
- `_` becomes a space and starts a new word;
- a lower-case letter at the start of a word is upper-cased;
- any other upper-case letter inside a word is lower-cased;
- a space starts a new word.

The leading `!!` counts as the start of the first word, so a missing key `CONTROL_OKAY` with no fallback comes out as `!!control Okay`: first word all lower case, later words capitalised. The `!!` prefix is the house marker for "missing localisation". Designer placeholder strings in the shipped documents use the same marker (§8).

These are the helpers the `.xtbl` loaders call for display names. `spec-tables-vehicle-world.md`, `spec-tables-weapons-combat.md`, `spec-tables-progression.md` and `spec-tables-traffic-ai.md` cite `0x0084a1b0`, `0x0084a280`, `0x00849fa0` and `0x00849df0` by address. Their "text handle" is the text pointer returned here. *Observation, not a cross-spec ruling:* a table that stores a `0x00849fa0` hash gets 0 for an unknown key, and later shows nothing or the `!!Unrecognized hash value` text, depending on which reader it goes through.

### 6.3 Registration into Vint's third callback table — CONFIRMED

The game's UI set-up `0x008489e0` fills the table `0x02317aec` and hands it to Vint through `0x00e22620`, which stores the pointer at `0x02a5a014` (the only writer, `0x00e22624`):

| Slot | Game body | Does | Vint thunk | Vint callers seen |
|---|---|---|---|---|
| `+0x00` | `0x00842b40` → `0x00849df0` | **text by key *string*** (null → null) | `0x00e227a0` | `0x00e22e98` (insertion formatter: a string insertion value "first tried as a localisation key"); `0x00e1deab`, `0x00e1df00` (Lua-binding code, HIGH); `0x0067afbc` (game side) |
| `+0x04` | `0x00842b60` → `0x00849950` | **text by key *hash*** | `0x00e227c0` | `0x00e31cab` (`text`'s stream builder, step 2 of §14.2); `0x00e22e49` (`{N:text_tag_crc}`); `0x00e1ddce` |
| `+0x08` | `0x00842b70` → `0x00849fa0` | **key string → hash, or 0 if unknown** | `0x00e227e0` | `0x00e31f79` (the `text_tag` string setter) |
| `+0x0c` | `0x008437a0` | named insertion symbol | — | `spec-vint-render.md` §14.2 |
| `+0x10` | `0x00841a80` | case folding | `0x00e22800` | `spec-vint-render.md` §9 |
| `+0x14` | 0 | — | — | zeroed at `0x00848a3b` |

All thunks return null/0 when the table or the slot is null, so with no game attached every lookup simply fails.

### 6.4 Lifetime

A table is loaded once at start-up (or when its DLC package mounts) and stays until a DLC unload. Text pointers point into the loaded buffers. A string set as a `text_tag` is turned into a hash **when the property is set** (slot `+0x08`), against whatever tables are loaded at that moment. The hash is turned back into text **every time** `text` rebuilds its stream (slot `+0x04`). A DLC text therefore resolves only while its DLC is mounted, and an element built before the DLC mounted keeps hash 0, and with it its raw string (HIGH — follows from §6.1–§6.3; not exercised).

---

## 7. What `spec-vint-render.md` §14.2 should say (flags for whoever folds this in — not edited here)

1. **§14.2 step 2 is right but incomplete.** Add that slot `+0x04` is the game's `0x00849950` (§6.1): a search of up to 14 loaded tables, with no cache, that returns null for hash 0 or an unknown hash.
2. **The `text_tag` row ("a string sets both (the hash through the game, table `0x02a5a014` slot `+0x08`)").** Slot `+0x08` returns **0 for a string that is not a key in a loaded table**. So for any non-key `text_tag` string, `+0xf4` is 0 and the raw `+0xf8` string is drawn. "The key hash `+0xf4` is non-zero" happens only for real keys, or when an integer is set directly (`text_tag_crc` or an integer `text_tag`). This is why plain-text `text_tag` values in shipped documents work at all.
3. **§9's third-table list omits slot `+0x00`** (text by key *string*, `0x00842b40`). That slot, not `+0x04`, is the "game lookup" the §14.2 insertion table means by "a string value is first tried as a localisation key" (the call at `0x00e22e98`).
4. **Correction to `spec-vint-font-format.md` §8 item 1** ("this install can reach codes 3 and `0xb` in full"). The font files for codes 3 and `0xb` are present, but **on PC the language code can never be 3, `0xb` or `0xd`** (§5.2: both the Steam path and the override are filtered through `US DE ES FR IT NL CZ PL RU`). Those two font arrangements are dead in this build, not merely unexercised.
5. **`spec-vint-font-format.md` §8 item 4 closes:** code 3 = Japanese (`JP`); code `0xb` = **Korean** (`SK`, the name `korean` maps to it, and `charlist_sk.dat` says `LANGUAGE=Korean`); code `0xd` = `CH` (Chinese, HIGH, from `fnt_chinese`). The default arrangement covers US, ES, IT, DE, FR, NL, CZ, PL and RU. That item's note about `font_sk` "spanning the Greek and Cyrillic blocks" is a misreading of the glyph range: `font_sk`'s codes `0x0100`–`0x0666` are charlist-assigned Hangul (§3.2), not Unicode.

---

## 8. Validation against the shipped documents — CONFIRMED (*replayed*, `locstr_check.py`)

Walking the 159 distinct documents of `tools/scratchpad/vintdoc_samples` with the loader-faithful grammar of `spec-vint-doc-format.md` §4/§5 (the override list and the baseline list of every element) finds **513 `text_tag` records, all string-valued (tag 4); no `text_tag_crc` record ships.** Each string was put through the game's own rule (§6.3 slot `+0x08`: hash, keep it only if it is found) against the eleven US tables loaded in slot order:

| `text_tag` string | Records | Resolve | Do not resolve |
|---|---|---|---|
| key-shaped (letters/digits joined by `_`, optionally `!!`-prefixed) | 100 | **89** | 11 |
| anything else (placeholder English, numbers, credits text, …) | 412 | 1 | 411 |
| empty | 1 | — (hash 0) | 1 |
| **total** | **513** | **90** (85 distinct keys) | **423** |

- **The 89 key-shaped strings that resolve** come from `menu` (58 of the 90 hits), `static` (12), `hud` (11), `mission` (4), `platform_pc` (4) and `customize` (1). Examples: `MAINMENU_DOWNLOADS` → `DOWNLOADABLE CONTENT`, `MENU_GAME_PAUSED` → `GAME PAUSED`, `HORDE_MODE_GAMEPLAY_TITLE` → `WHORED MODE`, `PLAYER_CHOICE_M21_OPTION1_TITLE` → `[format][color:red]UNMASK KILLBANE[/format]` (markup is in the table text and goes through §14.4 as usual), `COOP_DISCONNECT` → `Press {MAP_MENU_CLOSE_IMG} to disconnect.` (a named insertion symbol, §14.2).
- **The 11 key-shaped strings that do not resolve** are in **no `.le_strings` file of any language, base, patch or DLC**: `!!DIVERSION_TITLE`, `!!DIVERSION_VALUE`, `!!WEAPON_TITLE`, `CONTENT_DESCRIPTION`, `HELP_TEXT_CHEATS_SUBTITLE`, `HELP_TEXT_MISSIONS_TITLE`, `KEYS_TEXT`, `METER_LABEL` (×2), `TEXT_LOCALIZED` and `additive_alpha`. The real game also draws these raw unless Lua replaces the text (HYPOTHESIS that it always does; not checked).
- **The one plain word that resolves** is `default` → `DEFAULT` (`customize`). Any `text_tag` string is tried as a key, so a lower-case placeholder that happens to equal a key is replaced. A replacement must do the same rather than guess from the string's shape.
- **Team B's figure** (191 of 484 drawn texts "are localisation keys", `TEAM B/results/verify_text_rendering_20261003.md`) was not reproduced item by item. Their list is not on disk, and their count is of drawn texts, not records. Read against this table: **at most 89 of the shipped `text_tag` records are real, resolvable keys**. Any other string they counted — all-caps placeholder text such as `PRICE`, `SHOP`, `ZIMOS`, or a `!!`-prefixed designer placeholder — is **not** a key. The game draws it raw exactly as their renderer already does. Applying §6.1 to their 484 texts settles their number exactly.

---

## 9. What a replacement has to do, in short

1. At start-up, pick the language: one of `US ES IT DE FR NL CZ PL RU`, anything else falls back to `US`. Load `<table>_<LANG>.le_strings` for `static`, `hud`, `activity`, `diversion`, `mission`, `menu`, `multiplayer`, `customize`, `subtitle`, `platform_PC`, `patch0`, in that order (the patch archive's copies override `misc`'s), plus each mounted DLC's table. Load `charlist_<LANG>.dat`.
2. Parse each file as in §2: header, power-of-two buckets, `hash & (B − 1)`, records of u32 hash + 0-terminated u16 text. Reject a bad magic or version. Skip the big-endian `voice_script` files (they are never loaded anyway).
3. Key hash = §4 (lower-cased, table CRC-32, start 0, no final inversion). Hash 0 = no key.
4. Lookup = first hit across the tables in load order; return the stored text as is. An empty text is a hit; a miss is null.
5. Vint's three entry points: by string, by hash, and string → hash-if-known-else-0 (§6.3). With these, `spec-vint-render.md` §14.2's builder and insertion formatter work unchanged.
6. Draw the code units as font codes (§3.1). Convert outside text with the charlist, mapping `%`, `[`, `\` and `{` to `_` (§3.3).

---

## 10. OPEN items

1. Which object vtable slot `+0x10` of the `SteamApps` interface is (HIGH: the current-game-language query, from the strings it is compared with). Not needed to reproduce behaviour: the result is filtered to the nine supported codes anyway.
2. The start-up option behind the override at `0x0242dbd0` (`0x005d2268`, read from the object at `0x0149379c`): its name and source.
3. The DLC package list behind `0x0084a0f0` (`+0x80` name, `+0x101` status, `+0x103` flags): which subsystem owns it, and confirmation that the names are `dlc1`–`dlc3`.
4. Who frees a DLC table's buffer after `0x00849870` clears its flag.
5. `0x0073ce70` (called with "language ≠ US") and the cache at `0x01495604`: not read.
6. Whether the case-insensitive file-name match (`hud_US` request, `hud_us` stored) is the packfile layer's general rule (`spec-vpp-container.md`), or specific to these files.
7. Not crossed: the `.czn_pc` zone-data interior.

---

## 11. Size

| Set | Functions | Instructions |
|---|---|---|
| Lookup core: hash `0x00d9e740`, lookup `0x00849950`, wrappers `0x00849df0`, `0x00849fa0`, `0x00849fd0`, callback bodies `0x00842b40`, `0x00842b60`, `0x00842b70` | 8 | ≈130 |
| Loading and language: `0x00849bf0`, `0x00849870`, `0x00849e10`, `0x0084a390`, `0x00849660`, `0x008498c0`, `0x00849900`, `0x00849920`, `0x00849940`, `0x0084a0f0`, `0x0084a170` | 11 | ≈730 |
| Missing-key texts: `0x0084a1b0`, `0x0084a280`, `0x00849800` | 3 | ≈160 |
| Charlist: `0x00849b60`, `0x0084a460`, `0x008499d0`, `0x00849aa0`, `0x00849ae0`, `0x0084a030`, `0x0084a340` | 7 | ≈435 |

---

## Changelog

- 2026-10-03 (local, new spec): initial version. First specification of the `.le_strings` format (magic `0xa84c7f73`, version 1, power-of-two hash buckets, u32 hash + 0-terminated u16 text). Layout confirmed on 161 of the 165 shipped files; the other 4 are big-endian-record `voice_script` files the game never loads. Also first specification of the `charlist_<LANG>.dat` code-page files, and of the finding that shipped non-English text is stored as font codes, not Unicode (decoded correctly for ru/cz/pl/de/sk/jp).
- Runtime covered: the 14-slot table set, the loader, start-up language selection (Steam language or override, filtered to nine supported codes), DLC load/unload, the lookup `0x00849950` (no cache, first hit in load order), its wrappers including the `!!` missing-key texts, and the registration of slots `+0x00`/`+0x04`/`+0x08` into Vint's third callback table.
- Key hash confirmed as the engine's lower-casing CRC-32 (`0x00d9e740`, seed 0).
- Validated against the 159 shipped `.vint_doc` documents: 90 of 513 `text_tag` records resolve, including 89 of the 100 key-shaped ones; the 11 key-shaped strings that fail exist in no shipped table.
- Flags for `spec-vint-render.md` §9/§14.2 and `spec-vint-font-format.md` §8 items 1 and 4 are in §7; those files were not edited.
- Cleanroom self-check: 0 hits (the regex and the ten-fragment plain-substring backstop, run over the finished file).
