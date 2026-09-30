# Saints Row: The Third — `.ctdg_pc` Mission Conversation Format Specification

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1, target selected from `spec-format-inventory.md` §6 (type 42, internal name `Mission Conversation`, no `g`-side extension).
**Scope:** The whole file — an 8-byte header, a fixed 32-byte source-name field, a bounded record count, and a list of 12-byte dialogue-turn records — plus the registry and playback machinery the loader wires it into.
**Method:** Registration row → constructor → **the constructor turned out to register rather than parse**, so the trail ran through its 330-slot global registry to the playback tick and on to the real consumer (`tools/ctdg_ctor.txt`, `ctdg_consumers.txt`, `ctdg_player.txt`); then a loader replay against **all 4,984 shipped files** with an exact-file-size check. Harnesses: `scratchpad/ctdg_bulk.py`, `ctdg_replay.py`, `ctdg_w0.py`, `ctdg_names.py`.
**Cleanroom compliance:** No decompiled code or original identifiers. The magic value, offsets, strides and bounds are load-bearing format data. Filenames quoted are shipped data.

**Confidence key**: **CONFIRMED — empirical**, **CONFIRMED — disassembly**, **HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**.

---

## 1. Headline results

- **The structure is completely determined and the replay is exact: `44 + 12 × record_count == file size` in 4,984/4,984 files**, with zero failures. Every field below is confirmed twice — once from the consumer's own reads, once from the population. **[CONFIRMED — disassembly + empirical.]**
- **A second stash-only constructor, and a second global registry.** Like `.csc_pc`, the registered constructor parses nothing; it normalises the filename to a lowercased basename, keys it, and files the buffer into a **330-slot global table** of 20-byte entries. Parsing happens later, when a conversation is actually played. **[CONFIRMED — disassembly.]** With 4,984 shipped conversations against 330 slots, conversations are evidently streamed in and out per mission rather than all resident. **[HIGH CONFIDENCE — inferred.]**
- **The engine has (at least) two different string hashes, and this file uses both.** The constructor's registry key is a **table-driven CRC-32** over the lowercased name — *not* the rotate-6/XOR hash this project has been calling "the engine-wide string hash". Meanwhile the speaker ids **inside** the file are that rotate-6/XOR hash. `spec-vpp-container.md` §2.2's claim that "a reimplementation needs exactly one" is **corrected by this pass**: it needs two. **[CONFIRMED — disassembly for CRC-32; CONFIRMED — empirical with control for rol6, §6.]** *(Revised again 2026-09-10: there are **at least three**, and the breadth was backwards — the CRC-32 met here has ≈835 call sites against the rotate-6/XOR hash's ≈18, so the latter is the narrow one despite this project having called it "engine-wide". See `spec-extensionless-types.md` §4.)*
- **Record semantics come straight from the consumer**: each 12-byte record is one **dialogue turn** — a **speaker id**, a **line id**, and a third field. The consumer collects the *distinct* speaker ids into a set (capacity 30) and validates each **once** per conversation, then resolves each turn as the pair (speaker, line). **[CONFIRMED — disassembly.]**
- **The embedded name is the authoring-source filename**, `<name>.ctd`, in a fixed 32-byte field — the same authoring/shipped split already seen at `.fmeshx` → `.cfmesh_pc` and `.animx` → `.anim_pc`. It is **truncated at 31 characters in 1,187 files**, so it is a label, not a reliable key. **[CONFIRMED — disassembly (a 32-byte copy) + empirical.]**

## 2. Population facts

| | |
|---|---|
| Files | **4,984**, every one a distinct name |
| Archives | `sr3_city_0.vpp_pc` (4,096), `dlc2` (604), `dlc3` (253), `dlc1` (31) |
| Sizes | 56 … 248 bytes; **all divisible by 4**, all satisfying `44 + 12n` |
| Records per file | 1 … 17 (the loader's hard limit is 30) — most common 2 (1,556), then 3 (1,074) |
| Total records | **19,440** |
| Distinct speakers per file | 2 in 4,149 files; 3 in 673; 1 in 114; 4 in 48 — overwhelmingly two-handers |
| Naming | `<context>_<NN>_<event>_<voice>`; the trailing token is a voice family (`bf`, `bm`, `hf`, `hm`, `wf`, `wm`, `wma`, `wfa`, `z`), with ~640 files per family — the same conversation re-authored per pedestrian voice type |

## 3. File layout

| Offset | Size | Content | Confidence |
|---|---|---|---|
| `+0x00` | 4 | **Magic `0x56414344`** — the bytes `D C A V` in file order. Rejected if different | **[CONFIRMED — disassembly + 4,984/4,984.]** |
| `+0x04` | 4 | **Version — must be `1`** | **[CONFIRMED — disassembly + 4,984/4,984.]** |
| `+0x08` | **32** | **Source name**, NUL-terminated and zero-padded; the consumer copies the field **wholesale as 0x20 bytes**, so it is fixed-width, not variable. Holds the authoring filename `<name>.ctd`, truncated to 31 characters when longer | **[CONFIRMED — disassembly for the width; empirical for the content.]** |
| `+0x28` | 4 | **Record count.** The consumer rejects `0` and anything **above 30**; shipped files use 1…17 | **[CONFIRMED — disassembly for the bound; empirical for the range.]** |
| `+0x2C` | `12 × count` | **Dialogue-turn records**, §4. Ends exactly at EOF in 4,984/4,984 | **[CONFIRMED.]** |

Every byte of every file is accounted for. There are no offsets and no pointer fixups in this format — unusually for this project, it is a flat, fully-packed structure.

## 4. Dialogue-turn record (12 bytes)

| Offset | Content | Confidence |
|---|---|---|
| `+0x00` | **Speaker id** — a hashed participant name (§6). The consumer gathers the distinct values of this field across the file into a set of at most 30 and validates each one **once** before playback; a failure aborts the conversation. Only **57 distinct values exist across all 19,440 records** | **[CONFIRMED — disassembly for the role; empirical for the vocabulary.]** |
| `+0x04` | **Line id** — resolved together with the speaker as the pair *(speaker, line)*, which is what makes this field a per-speaker line reference rather than a global one. 4,153 distinct values; **distinct within a file in 4,983 of 4,984 files** | **[CONFIRMED — disassembly for the pairing; empirical for the distribution.]** |
| `+0x08` | A third per-turn field, stored alongside the other two on the turn's runtime node. **Zero in 19,109 of 19,440 records**; the non-zero values are signed multiples of 100 spanning roughly −500 … +600. A timing offset in milliseconds fits that shape exactly | **[CONFIRMED — that it is stored per turn; HYPOTHESIS — that it is a millisecond offset.]** |

Records are consumed strictly in file order, one runtime node per record appended to a list — so **record order is conversation order**. **[CONFIRMED — disassembly.]**

## 5. Load and playback path

**⚠ Missing cross-reference added 2026-09-13: none of the three functions below were ever named with their addresses in this document, though all three were already independently catalogued in `spec-format-inventory.md`'s registration-table dump (line for type 42, `.ctdg_pc`).** Constructor `FUN_006dea20`, destructor `FUN_006deb10`, real consumer **`FUN_0046ac60`**. Anyone picking up §6.1's still-open speaker-id-naming question should start from `FUN_0046ac60` directly rather than re-deriving which function "the consumer" is.

1. **Constructor (registration type 42, `FUN_006dea20`).** Reduces the filename to a basename via a 64-byte scratch buffer, lowercases it, computes the **CRC-32 key**, scans the 330-slot registry for that key (deduplicating re-registration), and on a free slot stores `{ name, key, buffer, size }` in a 20-byte entry. **No file bytes are read.** **[CONFIRMED — disassembly.]**
2. **Playback tick.** Walks a small table of pending playback slots, matches each against the registry by key, and hands the matching entry's **buffer and size** to the consumer. **[CONFIRMED — disassembly.]**
3. **Consumer (`FUN_0046ac60`).** Allocates a conversation slot from a fixed pool (entries of `0x58` bytes), validates magic/version/count, copies the 32-byte name, then walks the records — building the distinct-speaker set, allocating a node per turn, and resolving *(speaker, line)* for each. On success it stamps the slot active and returns a handle. **[CONFIRMED — disassembly.]**

The validation ladder returns a distinct error code for each failure mode — bad magic or version, out-of-range count, speaker validation failure, and line-resolution failure — which is useful for anyone reimplementing: **the count bound of 30 is enforced before any record is touched.**

## 6. Which hash is which — and the correction it forces

**The speaker ids are the rotate-6/XOR engine hash.** The diagnostic is the female/male pairs of the same voice family:

| family | female id | male id | XOR |
|---|---|---|---|
| `w` | `0xEDDF70CA` | `0xEDDF70C1` | `0x0B` |
| `b` | `0xFADF8545` | `0xFADF854E` | `0x0B` |
| `h` | `0x04DF9487` | `0x04DF948C` | `0x0B` |

`0x0B` is exactly `'f' XOR 'm'`. Under `h = rotl32(h, 6) XOR c`, two strings differing only in their **final** character produce hashes differing by exactly that character's XOR — so this is that hash, and the paired names differ only in a trailing `f`/`m`. **The control rules out the alternative:** CRC-32 of two strings differing only in the last character gives `0x97D2D988`, not `0x0B` — the whole word scrambles. **[CONFIRMED — empirical, controlled.]**

**The registry key is a table-driven CRC-32.** *(Scope note, 2026-09-10: this routine is **not** specific to conversations — a call-site census puts it at ≈835 call sites, making it the engine's general-purpose name hash. `spec-extensionless-types.md` §4.)* The constructor's key routine is the standard reflected CRC-32 update — `h = (h >> 8) XOR table[(tolower(c) XOR h) & 0xFF]`, seeded `0xFFFFFFFF`, over a 256-entry table — applied to the lowercased basename. **[CONFIRMED — disassembly.]**

**Correction to published text.** `spec-vpp-container.md` §2.2 and `spec-rig-format.md` §5 established the rotate-6/XOR hash as "engine-wide" and stated that a reimplementation "needs exactly one". The first half stands — that hash really is used across archives, rig bone names, the foliage registry and now speaker ids — but **the second half is wrong**: this format uses a *different* hash for its registry key. A reimplementation needs both, and must not assume a 32-bit name hash in an unfamiliar structure is the rotate-6/XOR one. **[CONFIRMED — disassembly.]**

### 6.1 What the speaker ids are *not*

An attempt to name the 57 ids failed: **0 of 57** matched the rotate-6/XOR hash of any of 3,917 curated candidates (voice-family codes, expanded forms, character names, filename tokens). The control matched 0 as well, so the search was simply unsuccessful rather than mis-calibrated — no false-positive rate to discount. Recovering the names needs the string table the validator consults, which this pass did not open. ~~**[OPEN — next step named 2026-09-13: the consumer is `FUN_0046ac60` (§5, cross-referenced from `spec-format-inventory.md`'s registration-table dump); trace what it or its callees do with a resolved speaker id — a runtime lookup table, a separate name-list file, or a call into another already-documented subsystem (the UI/HUD Lua layer's own string handling is a plausible candidate given the shared engine-wide hash) — rather than re-deriving which function "the validator" is from scratch.]**~~ **Next step executed 2026-09-13 — see §6.2. Bounded negative: `FUN_0046ac60` and every function reachable from its own resolution calls, and from the resolved handle's forward path to the Wwise-facing playback queue, use the speaker/line ids purely as opaque 32-bit keys into audio-only structures (a loaded-bank presence cache and a runtime-only Wwise game-object registry). No name/string table was found anywhere in the reachable static code. [CONFIRMED — disassembly, for the negative; see §6.2 for the full trace and the one table that WAS found, and why it cannot resolve the 57 ids even in principle.]**

Also ruled out: **no hash of the conversation's own name appears in any record** — neither CRC-32 nor rotate-6/XOR, over the filename or the embedded source name, in 0 of 4,984 files, with a same-shaped control also at 0. The records reference *speakers and lines*, never the file itself. **[CONFIRMED — empirical.]**

**Second, independent, much larger negative, same day (2026-09-29) — complementary confirmation, not a correction.** Re-ran the same hash-match test with a fresh, real-data candidate pool ~125× the original size: 63,323 base candidates (42,260 distinct identifiers + 15,506 distinct string literals from 277 real shipped `.lua` files, plus 15,571 distinct leaf-text values from 286 real `.xtbl` files — `audio_personas.xtbl`, `foley_engine.xtbl`, `audio_banks.xtbl`, `persona_radio_prefs.xtbl`, `radio_stations/events/activities.xtbl`, DLC1–3 character tables, etc.), expanded with case/underscore-space/trailing-digit variants to **489,002 distinct final candidates**. The real 57 speaker ids were independently re-derived directly from the shipped `.ctdg_pc` records (4,984 files, 19,440 records — population figures matching this section's own citations exactly) rather than trusted from this document's own text, and the diagnostic `h`-family pair (`0x04DF9487`/`0x04DF948C`, XOR `0x0B`) reproduced exactly, confirming the correct hash routine and the correct 57-value target set. **Result: 0/489,002 matched any of the 57 ids; a character-shuffled control over the same pool also matched 0** (expected chance-level hits for a pool this size against 57 32-bit targets is ≈0.0065, consistent with a well-calibrated negative, not a broken test). **[CONFIRMED — empirical, second independent test, real-data-sourced, well-powered.]** The names behind these 57 ids remain unrecoverable from any static string source found in this project so far, consistent with §6.2's independent finding that no name/string table exists anywhere in the reachable static code.

### 6.2 The speaker-id resolution mechanism, traced — §6.1's next step, executed (2026-09-13)

**Predicate stated before searching, per house rule**: a positive finding is a lookup table or function that, given a speaker id, produces a name/string, or that resolves at least some of the 57 known ids to a human-readable label (tested with a control showing the resolution rate is above chance). A negative finding is a documented, bounded trace that reaches no such table.

**Step 1 — does `FUN_0046ac60` (the consumer itself) resolve names? No.** Full decompilation (`tools/ctdg_player.txt`, already on file; re-verified this pass) shows the speaker id is stored as a plain 32-bit value at offset `+8` on the per-turn node, copied straight in from the record's own speaker-id field, and inserted into a fixed 30-slot distinct-value array by integer equality only — never dereferenced as a pointer, never passed to any string/format function. The three functions it calls to "validate"/"resolve" the id are, in call order:

- **`FUN_004657e0(speakerId)`** — per-record, gated by `DAT_031728c2`. Walks up to 5 small caches (`DAT_02cea93c[0..4]`) via `FUN_0046f530` (a linked-list membership scan) and `FUN_0046f280` (a loaded-state check, success only on state `3`). Pure presence/loaded-state check.
- **`FUN_0046af70(speakerId, lineId)`** — per-record, same gate. Computes `speakerId XOR lineId`, reduces it modulo a table capacity, and probes a hash bucket (`FUN_0046b2b0` → `FUN_0046b470`, an index-based bucket resolver with an inline 8-byte key-pair comparator — `tools/ctdg_registry_xrefs.txt`) for a matching *(speaker, line)* entry; on a miss, falls back to the same 5-cache walk as above (`FUN_00465790`/`FUN_0046f5f0`). The value produced on a hit is a **table-slot index**, immediately discarded by the caller except as a boolean (found vs not) — never read as a string.
- **`FUN_0046ab10(speakerId)`** — once per *distinct* speaker (the dedup pass). Locks a manager object (`DAT_031f0280`) and linearly scans a 700-entry, `0xa8`-byte-stride table (`DAT_031f02c0`, chain-linked via `DAT_031f02c4`) for an entry whose `+0x14` or `+0x18` field equals the speaker id.

**Step 2 — what is the `DAT_031f0280`/`DAT_031f02c0` table? A Wwise audio-object registry, not a name table — confirmed, not inferred.** A cross-reference census of the six related globals (`tools/ctdg_registry_xrefs.txt`: 58/23/16/16/21/23 total references respectively) shows this table is shared by a dozen-plus functions outside the `.ctdg_pc` module entirely (address range `0x0045c8xx`–`0x00461xxx`), i.e. it is generic engine infrastructure, not conversation-specific. One sibling consumer, `FUN_0045d1e0` (a per-tick "flush pending parameter changes" walk over every entry in this same table), calls the **literal, publicly-exported Wwise SDK functions `AK::SoundEngine::SetRTPCValue` and `AK::SoundEngine::SetSwitch`**, passing the entry's own address as the `AkGameObjectID` argument (`tools/ctdg_registry_writers.txt`) — the same public-API-citation standard this project already uses for Wwise in `spec-audio-format.md` §6–§7. The table's init function (`FUN_00460230`) allocates it fresh at runtime (`0x1cb60` bytes for 700 `0xa8`-byte entries, `0xaf0` bytes for the companion chain-index array — `700 × 0xa8 = 0x1cb60` and `700 × 4 = 0xaf0` exactly) and the three "clear" functions (`FUN_0045fab0`, `FUN_004602a0`, `FUN_004602f0`) explicitly zero every field. **The table is empty in the shipped executable image and is populated only at runtime, as in-game entities register/unregister live Wwise game objects.** This is the same "runtime-populated, null-in-static-image" shape already documented for the two Lua/UI hook data tables (`spec-lua-bindings.md` §8.5) and the customization slider state (`HANDOFF.md` §5). This registry is a **new, previously undocumented sibling** of the bank-load/unload state machine `spec-audio-format.md` §7 already found (a different, `0x5c`-byte-stride record keyed by numeric bank id) — worth a cross-reference there if that document is revisited, not chased further here.

**Step 3 — resolution test against the 57 known ids: not applicable, and that is itself the finding.** The win condition named for this task was to resolve the 57 speaker-id hashes against whatever table `FUN_0046ac60` consults. That table exists (`DAT_031f02c0`) and was found and characterised, but it is **confirmed empty in the static image** (Step 2) — there is nothing to test the 57 ids against without a live game session, and even a live session's table holds `AkGameObjectID`-keyed audio-object bookkeeping, not names. Running the test anyway would report 0/57 with no chance-level meaning, since an empty table resolves every query to "not found" regardless of input. This is the "wrong table" branch this task's own instructions anticipated, not a "found it but it resolves nothing real" branch: the table is real and its purpose is now known, but it was never going to hold names.

**Step 4 — traced forward from the resolved handle, per this task's step 2.** `spec-lua-bindings.md`'s hook census was checked first, per instruction: none of its 61 confirmed literal hook names, its two sprintf-templated families, or its two data-driven hook tables (§8.2–§8.5 there) mention dialogue, subtitles, captions, or conversations — that avenue is exhausted and contributes nothing here. Following the resolved handle through the engine's own code instead: the slot's advance-to-next-turn function (`FUN_0046a860`) calls **`FUN_0046a210(speakerId, lineId, clampedOffset, sessionId)`** — the actual "play this turn" call — which re-runs the *same* `DAT_031f0280` registry lookup (Step 2) to find the live game-object entry for the speaker, then calls `FUN_0045ec00(entry, lineId, sessionId, 0, clampedOffset, 0x14)`, which stores the **raw line id, unmodified**, into a queued playback-event record at that record's own offset `+8`, for a downstream tick to post to Wwise. No string, `sprintf`, or text-table call appears anywhere in this path (`tools/ctdg_play_turn.txt`). The one caller found outside the `.ctdg_pc` module of "is any conversation currently active" (`FUN_00554950`) uses that boolean purely as one input to an audio-streaming-priority calculation (substring-matching bank *filenames* like `"radio_"`/`"interface_media"`, unrelated to which speaker or line is involved) — not a per-speaker or per-line consumer at all.

**Conclusion — a bounded, documented negative, not an invented mechanism (per `HANDOFF.md` §27.8).** Across every function reachable from `FUN_0046ac60`'s own resolution calls and from the resolved handle's forward path to the Wwise-facing playback queue — eleven functions fully decompiled this pass (`FUN_004657e0`, `FUN_0046f530`, `FUN_0046f280`, `FUN_0046af70`, `FUN_0046b2b0`, `FUN_0046b470`, `FUN_00465790`, `FUN_0046f5f0`, `FUN_0046ab10`, `FUN_0046a860`, `FUN_0046a210`, `FUN_0045ec00`), plus the sibling registry consumer `FUN_0045d1e0` that identified the table's real purpose — the speaker id and line id are used **exclusively as opaque 32-bit keys**: into small loaded-audio-bank presence caches and into a runtime-only Wwise game-object registry, **never** into a name, subtitle, or display string. A positive finding would have looked like a `char*`/string field read out of a matched table entry, or a call from any of these functions into a `sprintf`/text-formatting/localization routine; none was found. This closes the mechanism question cleanly: **the names, if they are recoverable at all, are not present anywhere in this executable's static image.** They would have to come from a live-session memory snapshot of the runtime-populated registry, or from whatever authoring-side data assigns names to actors before the rotate-6/XOR hash is taken — both out of this project's reach from static disassembly alone. Per this format's own §1/§2 finding that the voice-family population (`bf`/`bm`/`hf`/`hm`/`wf`/`wm`/`wma`/`wfa`/`z`, ~640 files each) reads as generic pedestrian voice types rather than named story characters, it is also plausible that these particular 57 ids were never assigned human-readable display names at all — offered here explicitly as an unconfirmed reading of the population shape, **not** a traced fact, per this project's standing policy against dressing up a gap as an answer.

**Tooling this pass:** `tools/scripts/DecompileCtdgSpeakerRes.java`, `CtdgRegistryXrefs.java`, `CtdgRegistryWriters.java`, `CtdgSlotConsumers.java`, `CtdgFullApi.java`, `CtdgPlayTurn.java` (Ghidra project copy `tools/gp_conv`, independent per-task robocopy); outputs `tools/ctdg_speaker_res.txt`, `tools/ctdg_registry_xrefs.txt`, `tools/ctdg_registry_writers.txt`, `tools/ctdg_slot_consumers.txt`, `tools/ctdg_full_api.txt`, `tools/ctdg_play_turn.txt`.

## 7. Cross-format notes

- **Stash-only constructors are now the rule for non-geometry types, not the exception** — morph types 11/12, `.csc_pc` type 24, and `.ctdg_pc` type 42. Two of the three additionally register into a fixed-size global table (foliage's 64 slots, this format's 330), which caps how many can be resident.
- **No shared engine block appears** — the known-magic scan finds none of the material, Mesh, Morph, `ANIM`, foliage or tree magics in any of the 4,984 files. Standalone, like `.csc_pc`.
- The `.ctd` authoring extension joins `.fmeshx`, `.animx` and `.effectx` in this project's list of authoring-vs-shipped name pairs.

## 8. Methodology notes worth keeping

1. **The stash-only pattern paid off immediately.** This was the second consecutive format whose registration-row constructor was a dead end, and the technique from the previous one — enumerate the readers of whatever global it writes — went straight to the parser with no searching. A pattern recorded once is worth the write-up the first time.
2. **A hash function can be identified from its algebra rather than its output.** Nothing needed to be brute-forced to prove the speaker ids are the rotate-6/XOR hash: two ids known to come from names differing in one character differ by exactly that character's XOR, which is a property CRC-32 provably lacks. A single structural identity beat a 3,917-candidate search that returned nothing.
3. **"Engine-wide" deserves periodic re-testing.** The rotate-6/XOR hash was labelled engine-wide on strong evidence (22,274 bone names, archive filenames, a disassembled implementation). It took a format that uses *both* hashes to show the label had quietly hardened into "the only one". Breadth claims should be re-checked whenever a new subsystem is opened.

## 9. Open Items

1. The names behind the 57 speaker ids (§6.1) — closable via the validator's string table.
2. The line-id namespace: what `+0x04` resolves against (an audio bank? a subtitle table?) and whether ids are unique per speaker or global.
3. Whether `+0x08` is milliseconds (§4) — the shape fits, nothing read confirms it.
4. What the playback tick's small slot table's per-slot fields mean (only its role as a pending-playback list was established).
5. Whether any shipped file approaches the 30-record or 30-speaker limits (max observed 17 and 4) — i.e. whether the caps are generous or were once tight.
