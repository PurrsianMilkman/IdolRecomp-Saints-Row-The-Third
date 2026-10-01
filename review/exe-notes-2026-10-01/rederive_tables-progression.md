# Re-derivation from the executable: team-a/spec-tables-progression.md (Team A, 2026-10-01)

Inputs: the spec's NEEDS-EXE units and OPEN notes; desk review `adv_tables-progression.md`; index `exe_index/tables-progression.txt`.
Dumps: bridge job `20261001T123123-team-a-ytgi` (paths below are relative to `/root/crreish-bus/results/20261001T123123-team-a-ytgi/`).
Where an earlier Team A job already holds a deeper (depth-1) dump of a callee this job lacks, it is cited by its job id
(`20261001T021743-team-a-zcxu`, `20261001T021739-team-a-yumq`). Clean-room: own words, addresses only, no pseudocode.

Label rules: **CONFIRMED — disassembly** only for what was read in these dumps; HIGH CONFIDENCE / HYPOTHESIS / OPEN otherwise.
Units ordered by Team B dependency (reader grammar, record layouts, trees first), then the rest.

---

## §1.3 Reader grammar — booleans and the rounding of the ×1000 conversions

**Verdict: PARTLY CONFIRMED, PARTLY CORRECTED, residual OPEN (one callee).**

Evidence:
- `func8/func_0x00dac480.txt` (0x00DAC480, the "always" bool reader): takes (destination byte, node, child name). With a child name it looks the child up (0x00DC4FF0) and takes its text pointer; with no child name it uses the node's own text. If a text exists it is copied (bounded, 0x400) into a 1 KB stack buffer; **the buffer is never cleared when the text is missing**; then 0x00DAB850(buffer, 0x400) is called and its byte result is stored unconditionally. So the "always" caveat of §1.3 is confirmed at the instruction level, and **the literal set (`true`/`yes`/`false`/`no`) lives in 0x00DAB850, which is NOT dumped** — that part of the claim is not re-derived.
- 0x00DAC510 (depth-1 section inside `20261001T021739-team-a-yumq/func/func_0x005984c0.txt`): returns without writing when the node is NULL, the named child is missing, or the text pointer is NULL; otherwise the same copy + 0x00DAB850 path. "Only if present" CONFIRMED.
- `func3/func_0x006049a0.txt` (0x006049A0, notoriety row reader): `Check_Detection` is NOT read by the bool reader. Its text is copied by the bounded string copy 0x00DABA70 into a 64-byte stack buffer and compared case-insensitively with the literal `true` at 0x0129BCE0 (stricmp at 0x00604A62 and 0x00604AF9); the byte stored is 1 only on an exact match. **The §1.3/§6.1 "conflict" is not a contradiction: two different readers.** CONFIRMED — disassembly.
- `Allow_Update_By_Server` (§2.2): the same `true` literal 0x0129BCE0 is referenced at 0x0071422B inside the stats loader 0x00713BB0 (globals section of `func_0x006049a0.txt`), consistent with a direct compare against `true`; 0x00713BB0 itself is not dumped → HIGH CONFIDENCE.
- `func4/func_0x006fb8d0.txt` (cheat row reader): `Dont_Flag_As_Cheating` → 0x00DAC480 at 0x006FB985 (always), `Is_DLC` → 0x00DAC510 at 0x006FBA75 (only if present). CONFIRMED: both are the generic readers.
- Rounding sites (all read in the listings):
  - nags, 0x007050A0 (`func4/func_0x007050a0.txt` 0x00705138–0x007051AB): the float is multiplied in x87 by a double constant (0x012A2D90, rendered 1000.0), then the control word is saved, OR-ed with 0xC00 and reloaded before a 64-bit FISTP — i.e. **truncation toward zero**, not round-to-nearest. Same sequence for `Increment` and `Nag_Time`. CONFIRMED — CORRECTION to §10.3's `round(…)`.
  - metered_sprint, 0x009FFFE0 (`func6/func_0x009fffe0.txt` 0x00A000AC–0x00A000D4): `RechargeTime` int → float → double, `PantPercentage` float → double, double multiply, CVTTSD2SI → **truncation toward zero of the double product**. CONFIRMED (the spec's `int(…)` is right; now precise).
  - `Player_Ram_Delay`, 0x005F4710 (`func3/func_0x005f4710.txt` 0x005F48D3–0x005F48F0): float → double, × the double at 0x012A2D90 (1000.0), back to float, then helper 0x00DAD900. That helper (depth-1 section in `20261001T021743-team-a-zcxu/func1/func_0x005a9640.txt`, 0x00DAD900–0x00DAD92B) adds 0.5 for a non-negative value (subtracts 0.5 for a negative one) and truncates: **round half away from zero**. CONFIRMED.
  - notoriety_levels 0x00604C10 (`func3/func_0x00604c10.txt` 0x00604E96/ED1/EDB/F1E/F28) and notoriety_spawn 0x008ED480 (`func5/func_0x008ed480.txt` 0x008ED52D/548/563/57E) and the nag globals (0x0070520C/0x0070522D): **integer multiply by 1000** — the elements are read as ints, no rounding question exists. CONFIRMED.

Spec text changes:
- §1.3 table row `FUN_00DAC480 / 00DAC510`: strike the OPEN note; append: *"(2026-10-01, disassembly: `Check_Detection` (§6.1) and `Allow_Update_By_Server` (§2.2) do not use this reader — each is a bounded string copy compared case-insensitively with the single literal `true`; the cheat flags `Dont_Flag_As_Cheating`/`Is_DLC` do use it (always / only-if-present). The literal set itself is implemented in the callee 0x00DAB850, not yet dumped. The 'always' flavour's 1 KB buffer is confirmed never cleared on the absent path.)"*
- §1.3 "Numbers in the XML": strike the OPEN note; replace with: *"Rounding (2026-10-01, disassembly): nag `Increment`/`Nag_Time` × 1000 — double product, **truncated toward zero**; `int(RechargeTime × PantPercentage)` — double product, truncated toward zero; `Player_Ram_Delay` × 1000 — double product, then **rounded half away from zero** (helper 0x00DAD900); every other × 1000 (notoriety_levels, notoriety_spawn, nag globals) is an integer multiply."*
- New Review status: **Review status (2026-10-01): CONFIRMED — disassembly for the reader split (`Check_Detection`/`Allow_Update_By_Server` vs the generic bool reader), the uncleared "always" buffer, and every ×1000 / product rounding rule; the four-literal set of the generic reader remains as previously read (its callee 0x00DAB850 not dumped) — re-derived from the executable (job 20261001T123123-team-a-ytgi).**

Next dump: 0x00DAB850 (func), 0x00DABA70 (func; whether an absent child leaves the caller's buffer untouched — matters for §6.1's reused 64-byte buffer).

---

## §4.3 Unlockable record — the three undescribed bytes

**Verdict: CONFIRMED (padding) + CORRECTED (+0xFD is not merely "zeroed").**

Evidence:
- `func4/func_0x0071e5e0.txt` (row reader 0x0071E5E0): before any element is read it writes +0x00 := invalid id, +0x04 := −1, bytes +0xE0/+0xE1/+0xE2 := 0, byte +0xFD := 0, dwords +0xF4/+0xF8 := 0, byte +0xFC := 0xFF; later +0xC0…+0xDC, +0xE4 (Priority hash), +0xE8/+0xEC := 0, +0xF0 (Price), +0xF4, +0xF8, and +0xFC := 0 when Is_DLC = 1. **No instruction touches +0xE3, +0xFE or +0xFF.**
- `func4/func_0x0071dbd0.txt` (save-side id/bit writer 0x0071DBD0) and `func4/func_0x0071fd30.txt` (save-side restorer 0x0071FD30) touch only +0x00, +0xE0, +0xE1, +0xE2, +0xF8, +0xFC, +0xFD. So the three bytes are padding in every function that handles the record.
- +0xFD semantics (0x0071FD30): for each saved id whose record is found, the three saved bits are stored into +0xE0/+0xE1/+0xE2. If bit A is set and the record has Is_DLC = 1 (+0xF8) and the DLC-ownership check 0x0045B3A0 on the gate byte +0xFC fails, +0xE0 and +0xE1 are cleared again and **+0xFD := 1**; otherwise the apply routine 0x0071F170 is run for the record. In 0x0071DBD0 the saved bit A is (+0xE0 ≠ 0 or +0xFD ≠ 0) and bit B is (+0xE1 ≠ 0 or +0xFD ≠ 0), bit C is +0xE2 ≠ 0 — so a purchase of unowned DLC content survives the save round trip without being applied. CONFIRMED — disassembly.

Spec text changes:
- §4.3 row `+0xFD | zeroed` → `+0xFD | **"owned-but-DLC-gated" marker**: zeroed by the row reader; set to 1 by the save loader when a saved purchased (set A) bit belongs to an `Is_DLC = True` record whose DLC gate (+0xFC) is not owned — the state bytes +0xE0/+0xE1 are then left clear, and the save writer ORs this byte back into saved bits A and B (2026-10-01, disassembly of 0x0071FD30 / 0x0071DBD0)`.
- Add row: `+0xE3, +0xFE, +0xFF | padding — written by neither the row reader (0x0071E5E0) nor the save writer/loader (2026-10-01, disassembly)`.
- Strike the OPEN note.
- New Review status: **Review status (2026-10-01), §4.3: CONFIRMED — disassembly (all 0x100 bytes accounted for; +0xFD meaning added) — re-derived from the executable.**

---

## §4.1 Unlockables — where the saved base order comes from

**Verdict: CONFIRMED for the loaders and the save path; the ordering mechanism is still OPEN, but narrowed.**

Evidence:
- `func4/func_0x0071fac0.txt` (0x0071FAC0): zeroes both counts, runs the row loop with framework `main` (0x0111D44C) and the pool 0x01495410, hashes `M17_Vehicle_STAG_Tank_Upgrade`, copies the count into the base-count marker, forces +0xF8 := 2 on the matching record (NULL record if absent — as the spec says), then, only if the byte 0x0149365D is non-zero, runs the row loop again with a **NULL framework** for the patch file. **There is no sort or reorder pass** — the function ends by resetting four globals (0x012F5D10 := 0, 0x015220F0 := 0, 0x015220F4 := record count after the patch load, 0x012F5D14 := 1, 0x012F5D1C := 1). CONFIRMED.
- `func4/func_0x0071edf0.txt` (0x0071EDF0): rows are visited in sibling order via "first child named `Unlockable`" then "next sibling named `Unlockable`"; a NULL framework skips the `Framework` compare entirely; otherwise the row's `Framework` text (default `main`) is stricmp'ed against the argument; the hash of `Name` is compared with every existing record (skip if found; invalid id exempt); the row reader's return value gates the count increment; the loop stops at 0x180 records. CONFIRMED (all as §4.1 states).
- `func4/func_0x0071dbd0.txt` (0x0071DBD0, the id/bit emitter the save code calls): walks the runtime records **in index order** — records [0, base count) for the base block, [base count, count) for the DLC block — writing each record's hash into the output id array and its state bits into three bitsets. For the DLC block it then appends (0x0071C7E0, depth-1 section of `20261001T021743-team-a-zcxu/func1/func_0x0071dbd0.txt`) the ids of a side list of saved ids that matched no loaded record, up to a total of **0x15E = 350** ids. CONFIRMED.
- `func4/func_0x0071fd30.txt` (0x0071FD30, the restorer): for each saved id, linear search of the runtime table by hash; unknown ids go to the side list (0x0071C740) when any bit is set. Order-independent. CONFIRMED.

Conclusion: **the saved base order is exactly the runtime record order, and the runtime record order is the sibling order the XML tree hands the row loop.** Nothing in the loader/save path reorders. Therefore the file-order mismatch Team B measured must come from (a) the parsed tree's sibling order differing from the text order (contradicted by every other table whose row order was validated, e.g. `respect_levels` §14.5), or (b) the game loading a different `unlockables.xtbl` than the one Team A/B read (a same-named copy in `patch_compressed.vpp_pc` / `patch_uncompressed.vpp_pc` wins by §14.2's priority list — §14.17 lists checked copies only for three other tables). (b) is the cheap decisive test. Status: OPEN — HYPOTHESIS (b).

Spec text changes:
- §4.1 OPEN note → replace with: *"[OPEN — 2026-10-01, narrowed by disassembly: the save writer 0x0071DBD0 emits the runtime records in index order (base block = records 0…base−1; DLC block = the rest, plus up to 350 carried-over ids of unloaded records) and the loader 0x0071FAC0 performs no reorder after the row loop of 0x0071EDF0, which appends rows in sibling order. So the saved base order IS the loaded row order; the mismatch with the file order implies either a different sibling order from the parser or a different same-named `unlockables.xtbl` being loaded (patch archives win, §14.2). Data test first: look for `unlockables.xtbl` in `patch_compressed.vpp_pc` / `patch_uncompressed.vpp_pc` and compare its row order with the 16 saves.]"*
- §4.1 Loader row: append *"(no post-load reorder — 2026-10-01, disassembly)"*; §4.1 "Order = save order": note that the DLC-side saved block can also carry ids of records not loaded (side list, cap 350).
- New Review status: **Review status (2026-10-01), §4.1: CONFIRMED — disassembly for the row loop, the patch pass (NULL framework), the M17 special case and the save emitter's record-order enumeration; the base-order mechanism remains OPEN (HYPOTHESIS: a same-named patch copy with a different row order) — re-derived from the executable.**

Next: data test above; then func dumps 0x00B982B0 and 0x00B99810 (the two callers that write the save block), 0x0071C6E0.

---

## §4.5 patch_unlockables — is the flag store unconditional?

**Verdict: CONFIRMED — disassembly.**

Evidence: `func3/func_0x005d1a30.txt` 0x005D1A30–0x005D1A77 and `glob/xref_0x0149365d.txt`. The function sets a register to 1 at 0x005D1A46, makes two calls, and at 0x005D1A6C stores that register into 0x0149365D with no branch in between (straight-line from the function entry). The sibling bytes: 0x0149365C := 1 (same register, 0x005D1A66), 0x0149365E := 0, 0x0149365F := bit 6 of the dword 0x014937FC masked to one bit. The cross-reference listing shows exactly one writer (0x005D1A6C) and eight readers (0x005D24B3, 0x007097CE, 0x007098FE, 0x00709A2E, 0x0071FBA1, 0x007B366B, 0x00849F6E, 0x00BD4247).

Spec text changes:
- §4.5: strike the OPEN note; replace "The flag has exactly one writer … so the file is read on every PC start." with "The flag has exactly one writer in the image (0x005D1A6C in the start-up init 0x005D1A30), an **unconditional** store of 1 — the file is read on every PC start **[CONFIRMED — disassembly 2026-10-01]**. (Its sibling 0x0149365C is stored 1 unconditionally in the same block; 0x0149365E := 0; 0x0149365F := bit 6 of 0x014937FC — note for `spec-tables-world-streaming.md`.)" Heading label → `[CONFIRMED — disassembly for the load and the flag]`.
- New Review status: **Review status (2026-10-01), §4.5: CONFIRMED — disassembly (unconditional store; single writer) — re-derived from the executable.**

---

## §6.1 notoriety.xtbl — Check_Detection reader and the record tail

**Verdict: CONFIRMED (reader, layout) + CORRECTED (tail wording); one small OPEN.**

Evidence: `func3/func_0x006049a0.txt`. Name match: the row's name (passed in by the caller 0x00604B50) is stricmp'ed against the 30-pointer table at 0x012ED0B8 with loop bound 0x1E; no match → return without writing. Reads: `Delay` (always-int) once; `Gang` child → `Points`, `Min_Level`, `Max_Level` (always-int), `Check_Detection` (bounded copy, 64 bytes, then stricmp with `true`); then `Police` child, same four. Stores, record = 0x014A4620 + 0x2C·i: +0x00 gang Points, +0x04 Delay, +0x08 gang Min, +0x0C gang Max, byte +0x10 gang Check; +0x14 police Points, +0x18 Delay (same value), +0x1C police Min, +0x20 police Max, byte +0x24 police Check. **Bytes +0x11…+0x13 and +0x25…+0x2B are never written** by the loader — padding of the 11-dword record. Small OPEN: the gang and police `Check_Detection` texts share one 64-byte stack buffer; whether the copy helper 0x00DABA70 writes an empty string when the element is absent (else an absent police flag would inherit the gang text) is not in this dump.

Spec text changes:
- §6.1 OPEN note → replace with: *"(2026-10-01, disassembly: (a) `Check_Detection` uses its own 64-byte copy + case-insensitive compare with `true` — not the §1.3 bool reader, so 'only `true`' stands; (b) +0x11…+0x13 and +0x25…+0x2B are padding, never written; the two `Check_Detection` reads share one stack buffer — an absent police element's effect depends on the copy helper 0x00DABA70, not yet dumped.)"*
- New Review status: **Review status (2026-10-01), §6.1: CONFIRMED — disassembly (30-name match, reader, all stores, tail = padding); residual OPEN on absent-element behaviour of 0x00DABA70 — re-derived from the executable.**

---

## §6.3 notoriety_spawn — loop bound 24 vs 25 real rows; nested shape

**Verdict: CONFIRMED — disassembly (24; nested wrappers).**

Evidence:
- `func5/func_0x008eec00.txt`: opens the table, then a loop over a byte offset 0, 4, … while < 0x60 — **24 iterations**; each takes the name pointer at 0x013080F8 + offset, calls the row finder 0x008EC4A0 with it, and parses the found row into the destination at 0x01308098 + offset with 0x008ED480; then frees the document. `stag_speedboat` is not in the 24-name table, so its row is never looked up (dead data). CONFIRMED.
- `func5/func_0x008ed480.txt` (decompile lines 824–861, 862–934, 934–1076): finds the single child `level_info`, counts **its** children named `level_info`, allocates count × 0x30 at +0x3E8 and iterates them (the four time fields are integer-multiplied by 1000); +0x3E0 := first `level`, +0x3E4 := first + count − 1; the same wrapper-then-same-name-children pattern for `group_info` and `group_details`. The nested tree of §14.6 is the code's shape. CONFIRMED.

Spec text changes:
- §6.3 OPEN note → replace with: *"(2026-10-01, disassembly: the loop at 0x008EEC00 runs exactly 24 times over the pointer tables 0x013080F8/0x01308098; the 25th real row `stag_speedboat` is never looked up — dead data.)"* Change the italic "most likely never loaded" to "never loaded".
- §6.3 corrected tree block: label `[CONFIRMED — disassembly 2026-10-01: wrapper + same-name children for all three lists; times integer × 1000]`.
- New Review status: **Review status (2026-10-01), §6.3: CONFIRMED — disassembly (24 destinations; nested wrapper shape; integer ×1000) — re-derived from the executable.**

---

## §10.6 spawn_info_* — Spline_Type, category tail, group entry, rank capacity

**Verdict: CONFIRMED with one CORRECTION of wording.**

Evidence:
- `func8/func_0x00be5960.txt` 0x00BE5A19–0x00BE5A54: `Spline_Type` text is stricmp'ed against the pointer table at 0x01312708, index 0…5 (loop bound **6** at 0x00BE5A46); no match → the index becomes **−1** (0x00BE5A4B); the index is stored at +0x24. So `All Roads` / `Surface Roads` store −1 — the "resolver accepts them" reading of §14.12 is refuted; the six-name list is complete. (The dump annotates only the first two pointers, `Highway Only` and `Boat`; the other four names stand as previously read.) CONFIRMED.
- `func8/func_0x00be5300.txt` (category reader): record fields: +0x00 name ptr (first pass only, via 0x00DB12C0), +0x04 hash, +0x08 group-entry count, +0x0C entry array (count × 0x28, from the pool), +0x10…+0x13 four flag bytes, +0x14 law spawn group (0x00BE51C0 on the hash), +0x18 LawCap, +0x1C LawDelay, +0x20…+0x2C CarDay/CarNight/PedDay/PedNight, +0x30…+0x44 the six slot weights, normalised in two passes (day, night) exactly as §10.6 says. **Nothing in +0x48…+0x5F is written by the reader** (runtime fields / padding). The five special-cased names are compared only on the first pass. CONFIRMED.
- Group entry (0x28): +0x00 pointer to the matched spawn-group record (hash scan of the group table, stride 0x30), +0x04 DayChance, +0x08 NightChance, +0x0C DayCap, +0x10 NightCap, +0x14 VehicleDayCap, +0x18 VehicleNightCap, +0x1C Item_Carried (None 0 / Luggage 1 / Shopping Bags 2 / **anything else 0**), +0x20 Carry_Percent, +0x24 Group_Category (General Ped 0 / Special Ped 1 / Special Vehicle 2 / **anything else left unset**). **The per-entry fields are read only when the matched group's byte +0x08 — its `Team` index — equals 7**; an unknown group name or any other team value drops the entry (count decremented). CONFIRMED for the test; which team is index 7 is not in the dumps (HIGH CONFIDENCE `Civilian` from the data: 53/61 groups).
- `func7/func_0x00be4da0.txt` (ranks): zeroes 0x16AA8 bytes at 0x029A91A0 (= 55 × 0x698); the only count compares in the listing are the 8-entry `type` name table (0x013126E8, unmatched → −1), the three 0x7FFF clamps and the 12-entry loadout/personality caps — **no compare against 55**: the 56th row would overflow unchecked. CONFIRMED.

Spec text changes:
- §10.6 `Spline_Type` note → replace with: *"[CONFIRMED — disassembly 2026-10-01: the compare table at 0x01312708 has exactly six entries (loop bound 6 at 0x00BE5A46); a text matching none stores −1 at +0x24. `All Roads` (56/61) and `Surface Roads` (2/61) therefore store −1 — the six-name list is complete, §14.12's 'resolver accepts them' hypothesis is withdrawn.]"*
- §10.6 Category: "(only when the matched group's kind byte is 7:)" → "(only when the matched spawn group's `Team` index byte (+0x08 of the group record) is 7 — HIGH CONFIDENCE `Civilian`; otherwise, or for an unknown group name, the entry is dropped:)". Add the 0x28 entry offset table above and the unmatched enum results (`Item_Carried` → 0, `Group_Category` → unset).
- §10.6 OPEN note → replace with: *"(2026-10-01, disassembly: the category record's +0x48…+0x5F are not written by the reader (runtime/padding); the group-entry layout is tabulated above; the rank loader has no 55-record bound check — a 56th row overflows.)"*
- New Review status: **Review status (2026-10-01), §10.6: CONFIRMED — disassembly (`Spline_Type` six names, −1 fallback; category layout and tail; 0x28 group entry; rank memset 55 × 0x698 with no bound check) — re-derived from the executable.**

Next (low priority): glob range 0x01312708–0x01312720 (the six pointers, to re-read names 3–6).

---

## §10.7 drunk_levels — slot stride, scalar offsets, Freerunning_fail_pct

**Verdict: CORRECTED (layout of the initialiser), rest OPEN (row parser not dumped).**

Evidence: `func6/func_0x009755f0.txt` (block initialiser 0x009755F0, the function the start-up routine calls at 0x005D30AB) and `func6/func_0x00975520.txt` (0x00975520: opens the table, loops `Drunk_Levels`, copies `Drug_Type` into a 64-byte buffer, stricmp against `drunk`, `weed`, `escort tiger`, and calls the row parser 0x00975170 for a match — with the slot/row passed in registers, not visible). The initialiser writes, in order: five effect/texture handles (0x0130A094…0x0130A0A4); **per-slot parallel arrays with a 4-byte slot stride**: 0x02625810/14/18 := −1; 0x0262581C/20/24 := 0; **0x02625828/2C/30 := 120.0** (0x42F00000); **0x02625834/38/3C := 100.0** (0x42C80000); 0x02625840/44/48/4C := 0.0; then a loop over exactly **three** 0x38-byte records at 0x02625BC0, 0x02625BF8, 0x02625C30 (stop at 0x02625C70) presetting +0x00 := 0.0, +0x04 := 1.0, +0x08 := 1.0, +0x0C := 2000, +0x10…+0x30 := 0 (nine dwords), byte +0x34 := 0; then it tail-calls 0x00975520. CONFIRMED — disassembly.

Reading (HYPOTHESIS, consistent with §10.7's addresses): the two scalars are per-slot arrays (slot s at 0x02625828 + 4s and 0x02625834 + 4s — the spec's "+ slot" means + 4·slot), the fifteen level records (3 slots × 5) occupy 0x02625878 … 0x02625BBF (slot s at 0x02625878 + 0x118·s, which is where "0x118 per slot" comes from; the spec's 0x0262587C is the second field of the first record), and the three preset records at 0x02625BC0 + 0x38·s are one working "current level" record per slot. The record field order from the preset pattern: +0x00 Percent_drunk, +0x04/+0x08 camera multipliers (preset 1.0), +0x0C Random_input_switch_time (preset 2000), …, four reticle floats ending at +0x30, **`Sleepy` byte at +0x34** (the spec's "+0x30" is one field short). The whole data block 0x0262581C…0x02625C70 is 0x454 bytes — that is the span the spec quoted, not three slots. `Freerunning_fail_pct`: not in any dumped function; only 0x00975170 can settle it.

Spec text changes:
- §10.7 "Storage: …" paragraph → replace with: *"Storage (2026-10-01, from the block initialiser 0x009755F0, disassembly): per-slot scalars as parallel arrays with a 4-byte slot stride — `Max_Booze_Points` at 0x02625828 + 4·slot (preset 120.0), `Max_Time_Drunk` at 0x02625834 + 4·slot (preset 100.0); 0x02625810 + 4·slot preset −1; 0x02625840 + 4·slot preset 0.0. Level records are 0x38 bytes (fields in read order, `Sleepy` byte at +0x34); the initialiser presets only three records at 0x02625BC0 + 0x38·slot (0.0, 1.0, 1.0, 2000, zeros — HYPOTHESIS: one working record per slot), not the 15 table records, which HYPOTHESIS places at 0x02625878 + 0x118·slot + 0x38·level. The 0x454 span is the module's whole data block (0x0262581C…0x02625C70)."* Strike the OPEN note; keep the `Freerunning_fail_pct` line OPEN.
- New Review status: **Review status (2026-10-01), §10.7: CORRECTED — the initialiser's layout re-derived (parallel scalar arrays, three preset working records, `Sleepy` at +0x34); the table-record base and `Freerunning_fail_pct` remain OPEN until the row parser is dumped — re-derived from the executable.**

Next dump: 0x00975170 (func) — settles the record base, the slot register, and `Freerunning_fail_pct`.

---

## §10.2 gameplay_constants — Pepperspray parent; 20 vs 26 sub-readers

**Verdict: CORRECTED (Pepperspray is under `Gun`, and so are four more leaves) + CONFIRMED (reader census).**

Evidence:
- `func3/func_0x005f5b50.txt` (0x005F5B50): reads inline the top-level leaves and the sections `Death_and_busted`, `Fat_bones`, `Ho_Pimp_AI`, `ControlPatterns`, `TauntReactions`, `Fall_Damage` (six), and calls **21** section readers: twenty in the family 0x005F4E40–0x005F5A10 plus 0x005EE830 (which gets the `Gameplay_Constants` element as an explicit argument; the twenty get it in registers). The literal `gameplay_constants.xtbl` (0x01127A64) has exactly one reference in the image (this function) — no other loader opens the file.
- Depth-1 dump `20261001T021743-team-a-zcxu/func1/func_0x005f5b50.txt`: each callee opens exactly one section — 0x005F4E40 `Fire`, 0x005F4ED0 `Dripping_Wet`, 0x005F4F50 `sticky_fire`, 0x005F4FF0 `player_health`, 0x005F50A0 `Object_Glow_Colors`, 0x005F5100 `ragdoll_damage_factors`, 0x005F5A10 `Melee_attack`, 0x005F51E0 `Firearm_attack`, 0x005F5290 `Bust_Offsets`, 0x005F5300 `Helicopter`, 0x005EE830 `Cribs`, 0x005F5390 `Combat_AI`, 0x005F54D0 `Vehicle_Evade_AI`, 0x005F5530 `Idle_AI`, 0x005F5690 `PepperSpray`, 0x005F5710 `Fight_Club`, 0x005F57E0 `Coop_Meta_Game`, 0x005F58A0 `Wieldable_Prop_Throw`, 0x005F5920 `Watercraft_Params`, 0x005F5990 `Siren_Whoop_Params`. 6 + 20 = 26 sections: the heading's "20 sub-readers" and the table's 26 sections are both right (20 sub-readers + 6 inline). **No reader asks for `throwing_constants`, `Human_Fine_Collision`, or a top-level `Crime_scene`/`Drunk`/`Bust`/`Gunfire_Evade`** — with the callee set enumerated and the literal single-referenced, §14.9's "unread" rises to CONFIRMED.
- 0x005F5390 (`Combat_AI`, same depth-1 dump, 0x005F5393–0x005F54C1): fetches `Combat_AI`, then its child `Gun` (literal 0x0112739C = the bytes `G`,`u`,`n`,NUL); from the **Gun** element it reads `Reposition_Min/Max`, `Cant_Fire_Reposition_Min/Max`, then **`Pepperspray` as a child of `Gun`** → `Spray_Min` twice (0x014A1534, 0x014A1538), then — also from the **Gun** element — `PepperSprayMinUsageDelay`, `StunGunMinUsageDelay`, `Back_Away_Min_Dist`, `Back_Away_Max_Dist`, `Back_Away_Abs_Min_Dist`; then `Gunfire_Evade` and `Bust` from `Combat_AI`. CONFIRMED — disassembly.

Spec text changes:
- §10.2 heading: "(`FUN_005F5B50` and 20 sub-readers)" → "(0x005F5B50: six sections read inline, twenty sub-readers 0x005F4E40–0x005F5A10 and the `Cribs` reader 0x005EE830 — 2026-10-01, disassembly)".
- §10.2 `Combat_AI` row → *"`Gun` → `Reposition_Min` 1524, `Reposition_Max` 1528, `Cant_Fire_Reposition_Min` 152C, `Cant_Fire_Reposition_Max` 1530 (U); `Gun` → `Pepperspray` → `Spray_Min` 1534 and `Spray_Min` again 1538 (…unchanged…); `Gun` → `PepperSprayMinUsageDelay` 153C, `StunGunMinUsageDelay` 1540 (U), `Back_Away_Min_Dist` 1544, `Back_Away_Max_Dist` 1548, `Back_Away_Abs_Min_Dist` 154C (F); `Gunfire_Evade` → …; `Bust` → …"* and strike the OPEN note (here and in §14.9: "Combat_AI → Gun → Pepperspray" is the code's shape).
- §10.2 trailing OPEN note → replace with: *"(2026-10-01: callee census 6 + 20 + Cribs; the six top-level orphan sections of §14.9 have no reader — CONFIRMED unread by this loader, which is the literal's only user.)"* Upgrade §14.9's label on the orphan sections from HIGH CONFIDENCE to CONFIRMED.
- New Review status: **Review status (2026-10-01), §10.2: CONFIRMED — disassembly (Pepperspray and four more leaves are children of `Gun`; 26 = 6 inline + 20 sub-readers; orphan sections unread) — re-derived from the executable.**

---

## §10.1 store_discounts — capacity and record layout

**Verdict: CONFIRMED (layout) + CORRECTED (capacity: none in the loader).**

Evidence: `func5/func_0x0080e930.txt` (0x0080E930). Loop over `StoreDiscounts` rows with **no upper bound**; row i: hash of `Name` → 0x022CD108 + 0x30·i, 0 → +0x04, count of `DiscountElement` children (as a byte) → 0x022CD111 + 0x30·i (byte +0x09), element array (count × 0x18 from the heap allocator with alignment 4, then zeroed) → 0x022CD114 + 0x30·i (+0x0C). Bytes +0x08, +0x0A, +0x0B and +0x18…+0x2F of the 0x30 record are never written. Element: +0x00 `DiscountName` hash, +0x04 `Amount` (preset 0, only-if-present float), +0x08 `RadioEvent` hash (preset invalid id), +0x0C `Hours` (preset 1, only-if-present u32), +0x10 `Triggered` (preset 0.1), +0x14 left zero. Counters 0x022CD0E0 (rows) and 0x022CD05C (elements). CONFIRMED.

Spec text changes:
- §10.1: "allocated `count × 0x18`, zeroed" → "allocated `count × 0x18` (alignment 4), zeroed"; strike the OPEN note; add: *"Capacity (2026-10-01, disassembly): the loader bounds nothing — every `StoreDiscounts` row is stored; bytes +0x08, +0x0A/+0x0B and +0x18…+0x2F of the 0x30 record are not written (runtime/padding). The static array's extent is not visible in the loader; the save's 40-slot arrays are the save's own layout."*
- New Review status: **Review status (2026-10-01), §10.1: CONFIRMED — disassembly (record and element layouts; no loader bound) — the static array extent after 0x022CD108 is OPEN — re-derived from the executable.**

Next: glob range 0x022CD108–0x022CE000 (what follows the 31st record).

---

## §2.3 Stat row — handler-slot naming (slot 1)

**Verdict: CONFIRMED for this document's naming; the conflict with `spec-save-format.md` is narrowed, not closed.**

Evidence:
- `func4/func_0x00710950.txt` (0x00710950, requirement progress of one achievement): single-requirement achievements use the value getters 0x00710730 (class 2, int) / 0x007107B0 (float) and target getters 0x00710840 / 0x00710870; multi-requirement achievements call, per requirement, the pointer at **handler-entry + 4** (0x0151CBFC + 0x18·type) with (stat row, requirement) and count the requirements for which it returns non-zero. Slot 1 is therefore used as a **"requirement satisfied?" predicate** here, not as a value getter. CONFIRMED.
- `func4/func_0x00714770.txt` (0x00714770): 51 entries × 6 pointers from 0x0151CBF8 (0x4C8 bytes, zeroed first). Slot 1 is one of three functions: 0x00712330 for the integer entry and every int-class complex entry, 0x00712380 for the float/distance/time/money entries and the float-class complex ones, 0x007123D0 (boolean), 0x00712400 (percent) — a per-class shape consistent with a comparison predicate. Entries 8–12 (the handlers of stat ids 13–17) and entry 42 (id 45) have non-zero slots 4 and 5; entry 7 and entries 13–50 (other than 42) have slots 4/5 = 0 — matching §2.4's "only 13–17 and 45 have a serialise slot". Slot 2 is type-specific (0x00712520 int, 0x00712540 float, 0x00712570 distance, 0x00712600 time, 0x007137B0 money) — HYPOTHESIS: a display/format handler.

Spec text changes:
- §2.3 `+0x08` row: "slot 1 the *requirement-evaluate* handler used by `FUN_00710950`" → "slot 1 the *requirement-evaluate* predicate — 0x00710950 calls it with (stat row, requirement) and tests the result for true/false **[CONFIRMED — disassembly 2026-10-01]**; its implementation is per value class (0x00712330 int, 0x00712380 float, 0x007123D0 boolean, 0x00712400 percent)".
- §2.3 OPEN note → *"[2026-10-01: the consumer confirms 'requirement-evaluate'; `spec-save-format.md` §7's 'get' is not supported by any dumped consumer — to be reconciled there once 0x00712330/0x00712380 are read. Slots 4/5 are non-zero exactly for handler types 0–6, 8–12 and 42 (CONFIRMED — disassembly).]"*
- New Review status: **Review status (2026-10-01), §2.3: CONFIRMED — disassembly for slot 1's use and for which entries carry slots 4/5; naming in `spec-save-format.md` §7 to be reconciled (note for that document) — re-derived from the executable.**

Next: 0x00712330, 0x00712380, 0x00710CB0 (func).

---

## §5 respect_levels — level-to-row indexing and the capacity store

**Verdict: CONFIRMED (capacity, accessor indexing); the saved-level mapping stays OPEN (callers not dumped).**

Evidence:
- `func3/func_0x0060ecf0.txt` (element constructor 0x0060ECF0): fills **20** dwords from +0x14 with the invalid id (loop counter 0x13 down to 0), then +0x10 := 0 (count), **+0x0C := 0x14 (capacity 20)**, +0x08 := address of +0x14. CONFIRMED — the capacity store the spec said it had not read.
- `func3/func_0x0060eb40.txt` (0x0060EB40): argument n; n ≥ 50 → 22000; else returns the dword at 0x014B033C + 0x64·n, i.e. **record n (0-based), field +0x04 = `Respect`** (record base 0x014B0338). `func3/func_0x0060ebb0.txt` (0x0060EBB0): sums `Respect` of records 0 … min(n, loaded count) − 1. Both take a **0-based record index**. CONFIRMED.
- Mapping of the saved level: the callers (0x007CFB60 at 0x007D01E5 / 0x007D020D; 0x00B97340 at 0x00B9735B / 0x00B97391) are not dumped. HYPOTHESIS: the saved level L (0…50) is passed as is — then 0x0060EB40(L) is the `Respect` of XML row L+1 (the cost of the next level, 22000 once L = 50) and 0x0060EBB0(L) is the total of rows 1…L, a consistent pair if L counts levels already gained.

Spec text changes:
- §5.2 table, `Unlockable` row: "(capacity = 20 ids per level — … HIGH CONFIDENCE, the capacity store itself was not read)" → "(capacity = 20 ids per level: the element constructor stores 0x14 at +0x0C and fills 20 slots from +0x14 — **CONFIRMED — disassembly 2026-10-01**)". §5.3: strike "the capacity constant 0x14 (read from the constructor's loop count, not its store)".
- §5.2 "Helper accessors": add *"Both accessors take a 0-based record index (row 0 = the first `<respect_level>`): 0x0060EB40(n) = record n's `Respect` (22000 for n ≥ 50), 0x0060EBB0(n) = Σ records 0…n−1 capped at the loaded count (CONFIRMED — disassembly 2026-10-01). Whether the saved level L is passed directly (so that 0x0060EB40(L) is the cost of the *next* level) is a consumer question — OPEN (0x007CFB60, 0x00B97340)."*
- New Review status: **Review status (2026-10-01), §5: CONFIRMED — disassembly for the capacity store and the accessors' 0-based indexing; saved-level mapping OPEN (callers 0x007CFB60 / 0x00B97340 not dumped) — re-derived from the executable.**

---

## §3.1 achievements — the loader-to-dispatcher hop

**Verdict: still OPEN, narrowed by one hop.**

Evidence: `func8/func_0x00dac9a0.txt` (0x00DAC9A0): with no parser handed in it formats "xml_table_parse %s" and creates/keeps a parser in 0x029CFF9C, then calls **0x00DC5AC0(file name, parser)**; failure prints the "missing or invalid" message; success stores the `Table` child in 0x029CFFA0. `func8/func_0x00dc5ac0.txt` (0x00DC5AC0): calls **0x00DC5A10** to open the file (failure messages 'File "%s" does not exist.' / 'exists, but appears to be locked'), reads its size (0x00DA7D20), reads it whole (0x00DAA5D0) into the parser's pool and parses (0x00DC5820). So the named-resource resolution of §14.2 sits inside **0x00DC5A10**, which is not dumped; and the first edge (0x00713BB0 → 0x00DAC9A0) is not visible either (0x00713BB0 not dumped; the callers listing of 0x00DAC9A0 is capped). Nothing contradicts the HIGH CONFIDENCE reading; nothing confirms it.

Spec text changes:
- §3.1 OPEN note → *"[OPEN — narrowed 2026-10-01: 0x00DAC9A0 → 0x00DC5AC0 → 0x00DC5A10 (file open) is CONFIRMED — disassembly; the archive-priority lookup of §14.2 must be reached from 0x00DC5A10 (not dumped). The edge 0x00713BB0 → 0x00DAC9A0 is not dumped.]"*
- New Review status: **Review status (2026-10-01), §3.1: OPEN (narrowed to 0x00DC5A10; two hops confirmed) — partially re-derived from the executable.**

Next: 0x00DC5A10 (func), 0x00713BB0 (func).

---

## §10.4 mission_checkpoints — the sort key

**Verdict: CONFIRMED — disassembly.**

Evidence: `func4/func_0x006df740.txt` (comparator 0x006DF740, used by the sort in 0x006DF8B0 at 0x006DF892 and again by 0x006DF920): compares the two records' **mission-name strings (+0x00) byte by byte, case-sensitively**; if they differ the sign of that compare decides (−1 / +1); if equal, compares **`Index` (+0x08) ascending** (−1, 0 or +1). The checkpoint name (+0x04) and `Debug` are not part of the key.

Spec text changes:
- §10.4: "After loading the array is **`qsort`ed** (`FUN_006DF740`)." → "After loading the array is sorted by **mission name (case-sensitive byte compare) then `Index` ascending** (comparator 0x006DF740; the checkpoint name is not a key) **[CONFIRMED — disassembly 2026-10-01]**." Strike the OPEN note.
- New Review status: **Review status (2026-10-01), §10.4: CONFIRMED — disassembly (sort key) — re-derived from the executable.**

---

## §10.3 gameplay_nags — rounding (OPEN note of §1.3)

**Verdict: CORRECTED.** See §1.3: `round(Increment × 1000)` / `round(Nag_Time × 1000)` are **truncations toward zero** of the double product (0x007050A0, 0x00705146–0x0070519D). Also confirmed in the same dump: an unknown `Name` leaves the row loop (the whole load ends), the 20-name table at 0x012F47F0 with stride 0x20, present flag +0x14 := 1, the globals preset to 30 then integer × 1000.

Spec text change: §10.3 "**`+0x10` increment := `round(Increment × 1000)` (ms)**, **`+0x0C` nag time := `round(Nag_Time × 1000)` (ms)**" → "**`+0x10` increment := `trunc(Increment × 1000)` (ms)**, **`+0x0C` nag time := `trunc(Nag_Time × 1000)` (ms)** — double product, truncated toward zero **[CONFIRMED — disassembly 2026-10-01]**"; strike the trailing rounding pointer.
New Review status: **Review status (2026-10-01), §10.3: CONFIRMED — disassembly (loop, abort on unknown name, presets; rounding corrected to truncation) — re-derived from the executable.**

---

## §10.11 metered_sprint — the `Multiplayer` row and the product rounding

**Verdict: rounding CONFIRMED; second caller CORRECTED to "none"; residual OPEN on the single caller's argument.**

Evidence: `func6/func_0x009fffe0.txt`: header lists **one caller only**, 0x009DC815 in 0x009DC780, and 0x009DC780 is exactly what the start-up routine calls at 0x005D34BA (`func3/func_0x005d25f0.txt`). The requested entry name arrives as an argument and is compared **case-sensitively** (inline byte compare) with each row's `Name`; the first match is read and the search ends. The `Multiplayer` literal at 0x01122ED8 has a single use in the image, in 0x005C3D40 (a resource-category compare with `Preload`/`Vehicle`/`Environment`; `20261001T021743-team-a-zcxu/func1/func_0x005c3e70.txt`), unrelated to sprinting. Pant threshold: double product, truncated toward zero (§1.3).

Spec text changes:
- §10.11 OPEN note → *"(2026-10-01, disassembly: 0x009FFFE0 has exactly one call site, 0x009DC815 in 0x009DC780 — the boot path at 0x005D34BA; no second caller exists, and the only `Multiplayer` literal in the image belongs to an unrelated resource-category compare. HIGH CONFIDENCE the `Multiplayer` sprint row is dead data; what 0x009DC780 passes is not dumped.)"* Replace "int(RechargeTime × PantPercentage)" by "trunc(RechargeTime × PantPercentage) (double product, toward zero — CONFIRMED — disassembly)".
- New Review status: **Review status (2026-10-01), §10.11: CONFIRMED — disassembly (single caller, case-sensitive match, truncation); `Multiplayer` row HIGH CONFIDENCE dead — re-derived from the executable.**

Next: 0x009DC780 (func).

---

## §10.9 default_global — the five unexplained elements

**Verdict: still OPEN; the loader side CONFIRMED.**

Evidence: `func7/func_0x00bb70b0.txt` (0x00BB70B0): opens with the plain helper (0x00DC5AC0, no parser argument), reads `skybox_mesh_filename` (default `rfg_skybox`) and `cloud_mesh_filename` (default `skybox_clouds`) into the caller's buffers when supplied, and `orbitals` → `orbital` → `map_name` (non-empty only) into the caller's 0x40-byte name array, stopping at 15. It reads nothing else and does not free the document. `func8/func_0x00dc5ac0.txt` header: the plain-open helper has 17 call sites (0x00DD8E20, 0x00DAC9A0, 0x004CAEE0, 0x004CB510 ×3, 0x00E2B420, 0x004CAE20, 0x008F08A0, 0x00BA5DA0, 0x00BB6400, 0x0073A210, 0x00BB70B0, 0x009F6240, 0x00E222E0, 0x00E21F70, 0x00738880); 0x008F08A0 opens `action_nodes.xtbl`, the others are not dumped. None of the dumped functions references `day_begin`, `day_end`, `tod_segments`, `horizon_mountain_enabled` or `fog_camera_follow`.

Spec text change: §10.9 OPEN note → *"[OPEN — 2026-10-01: 0x00BB70B0 confirmed to read only the three elements (disassembly); the file-name literal's other users and the plain-open helper's 17 call sites (notably 0x00BB6400, 0x00BA5DA0 near the sky/time-of-day code) are not dumped.]"*
New Review status: **Review status (2026-10-01), §10.9: loader CONFIRMED — disassembly; the five extra elements remain OPEN — partially re-derived from the executable.**

Next: xref of the literal 0x0118CF40; func 0x00BB6400, 0x00BA5DA0; string xref for `tod_segments`.

---

## §10.12 activity_types — bit 2 of +0x43

**Verdict: OPEN — 0x006174C0 is not in the dump set (not in the index, not in any job).** No change. New Review status unchanged (NEEDS-EXE). Next: 0x006174C0 (func).

---

## §11 Load order — call-site order vs execution order

**Verdict: CONFIRMED — disassembly, with additions.**

Evidence: `func3/func_0x005d25f0.txt`, listing 0x005D2CB0–0x005D3640: the routine is a straight sequence of stage blocks — each block starts with a call to 0x005D18E0, runs its loaders, then calls 0x00707800 and exits forward to 0x005D36D5 when it returns true. The only conditional branches in the range are those forward exits, a few conditional calls gated on the flag bytes 0x0149365C / 0x01493660 / 0x012EBC04 / a stack byte, and one backward jump (0x005D2EF0 → 0x005D2DCF) that lands in an exit stub (call 0x00707810, jump to 0x005D36D5). **No loop; the ascending call-site order is the execution order.** The sites named in §11 are confirmed as: 0x005D2CCA → 0x0071B420 (tweak_table), 0x005D2E2C → 0x008FB6B0, 0x005D2F9A → 0x006050B0, 0x005D2F9F → 0x007054D0 (which calls the nags loader 0x007050A0 at 0x0070551E — caller list in `func_0x007050a0.txt`), 0x005D30AB → 0x009755F0 (drunk block init + loader), 0x005D3251 → 0x00BE5CD0, 0x005D3311 → 0x005F4710 (via its thunk), 0x005D34BA → 0x009DC780 (metered_sprint caller), 0x005D356E → 0x007105B0, 0x005D3585 → 0x006FBB50, 0x005D3628 → 0x0071FAC0, 0x005D362D → 0x0060EED0. Additions: **`gameplay_constants` is loaded at 0x005D2CC5, immediately before `tweak_table`**, and **`mission_help` (0x00A1FF70) at 0x005D2DAA**, before `collectibles`. Wrappers 0x008FB6B0 / 0x006050B0 / 0x00BE5CD0 / 0x007105B0 / 0x0060EED0 are not dumped (HIGH CONFIDENCE links to the §1.2 loaders).

Spec text changes:
- §11 "Load order that matters" — prefix the list with "`gameplay_constants` (`0x5D2CC5`) → " and insert "`mission_help` (`0x5D2DAA`) → " before `collectibles`; strike the OPEN note; replace "(call sites ascending)" with "(call sites ascending = execution order: the range 0x005D2CB0–0x005D3640 is straight-line with forward abort exits only — CONFIRMED — disassembly 2026-10-01)".
- New Review status: **Review status (2026-10-01), §11: CONFIRMED — disassembly (straight-line control flow; two loaders added to the order) — re-derived from the executable.**

---

## Bonus findings on DESK-PASS units (no NEEDS-EXE status, recorded for completeness)

- **§1.4 / §10.10 — base `tweak_table` framework.** `func4/func_0x0071b420.txt` (0x0071B420): opens the table, loads the address of the `main` literal (0x0111D44C) into the register ECX at 0x0071B432 immediately before calling the row reader 0x0071B070, then frees the document. So the base call passes **`main`** (register argument), not NULL — CORRECTION to §1.4's exception list and §10.10's "(base call passes a NULL framework = accept all)" → "(base call passes `main` in a register — HIGH CONFIDENCE, 0x0071B070 not dumped; no behavioural change on real data: 601/601 rows have no `Framework`)". The `patch_unlockables` NULL-framework exception is CONFIRMED (0x0071FAC0 passes 0; 0x0071EDF0 skips the compare).
- **§1.4 name builder** 0x0045BA50 (`func1/func_0x0045ba50.txt`): if bit 2 of the bundle byte +0x103 is clear and the framework string at bundle +0x80 is non-empty, formats `<framework>_<base>` into a 0x40-byte buffer; otherwise the plain base name. CONFIRMED.
- **§7** `Notoriety_Decay_Mult` reciprocal computed in double (1.0 ÷ value, 0x005F490E) and stored as float; `Player_Ram_Delay` rule in §1.3. CONFIRMED.
- **§6.2** all five ×1000 are integer multiplies (no rounding). CONFIRMED.
- **§8** `Dont_Flag_As_Cheating` always-bool, `Is_DLC` only-if-present (generic readers). CONFIRMED.
- **§4.1** the DLC-side saved id list may include up to 350 ids in total, including ids of records not loaded this session (side list), HIGH CONFIDENCE for the cap's meaning — note for `spec-save-format.md` §6.4.

---

## Summary table

| Unit | Verdict | Dump(s) | Spec change |
|---|---|---|---|
| §1.3 bool / rounding | CONFIRMED (reader split, uncleared buffer, all rounding rules) / OPEN literal set (0x00DAB850) | 00dac480, 006049a0, 006fb8d0, 007050a0, 009fffe0, 005f4710, 00604c10, 008ed480 (+ yumq 00dac510, zcxu 00dad900) | OPEN notes replaced; nags `round` → `trunc` |
| §2.3 slot 1 | CONFIRMED (predicate use; slots 4/5 census) | 00710950, 00714770 | note rewritten; reconcile save-format §7 |
| §3.1 hop | OPEN, narrowed to 0x00DC5A10 | 00dac9a0, 00dc5ac0 | note narrowed |
| §4.1 base order | CONFIRMED loaders/save path; mechanism OPEN (HYPOTHESIS: patch copy) | 0071fac0, 0071edf0, 0071dbd0, 0071fd30 | note replaced; data test named |
| §4.3 three bytes | CONFIRMED padding; +0xFD CORRECTED | 0071e5e0, 0071dbd0, 0071fd30 | rows added/rewritten |
| §4.5 flag store | CONFIRMED unconditional | 005d1a30, xref 0149365d | label upgraded |
| §5 respect | CONFIRMED capacity + 0-based accessors; saved-level mapping OPEN | 0060ecf0, 0060eb40, 0060ebb0 | text added |
| §6.1 notoriety | CONFIRMED reader/layout; tail = padding; small OPEN (0x00DABA70) | 006049a0 | note replaced |
| §6.3 spawn | CONFIRMED 24; nested shape | 008eec00, 008ed480 | "never loaded" |
| §10.1 discounts | CONFIRMED layout; CORRECTED no capacity in loader | 0080e930 | note replaced |
| §10.2 constants | CORRECTED Pepperspray (+4 leaves) under `Gun`; census 6+20(+Cribs) CONFIRMED | 005f5b50 (+ zcxu depth-1) | row + heading rewritten; §14.9 upgraded |
| §10.3 nags | CORRECTED rounding (trunc) | 007050a0 | formula changed |
| §10.4 checkpoints | CONFIRMED sort key | 006df740 | sentence replaced |
| §10.6 spawn_info | CONFIRMED six-name `Spline_Type` (−1 fallback), layouts, no rank bound; wording CORRECTED (team byte 7) | 00be5960, 00be5300, 00be4da0 | notes replaced |
| §10.7 drunk | CORRECTED initialiser layout; row parser OPEN | 009755f0, 00975520 | storage paragraph replaced |
| §10.9 default_global | loader CONFIRMED; five elements OPEN | 00bb70b0, 00dc5ac0 | note narrowed |
| §10.11 sprint | CONFIRMED single caller + trunc; `Multiplayer` HIGH CONFIDENCE dead | 009fffe0, 005d25f0 | note replaced |
| §10.12 bit 2 | OPEN (no dump) | — | — |
| §11 order | CONFIRMED straight-line; two loaders added | 005d25f0 | list extended |
| §1.4/§10.10 (bonus) | CORRECTED: base tweak call passes `main` in ECX (HIGH CONFIDENCE) | 0071b420, 0045ba50 | exception list fixed |

NEEDS-DATA units (§2.1/2.2, §3.2/3.3, §4.6, §14) are outside this pass.

## Next-dump list

| Address | Kind | Settles |
|---|---|---|
| 0x00DAB850 | func | §1.3 generic bool literal set |
| 0x00DABA70 | func | §6.1 (and every 64-byte copy site): behaviour when the child is absent |
| 0x00975170 | func | §10.7 level-record base, slot register, `Freerunning_fail_pct` |
| 0x006174C0 | func | §10.12 bit 2 of +0x43 |
| 0x00DC5A10 | func | §3.1 named-resource open → priority list |
| 0x00713BB0 | func | §3.1 first hop; §2.2 `Allow_Update_By_Server` compare |
| 0x009DC780 | func | §10.11 the entry name actually requested |
| 0x007CFB60, 0x00B97340 | func | §5 saved level → record index |
| 0x00712330, 0x00712380, 0x00710CB0 | func | §2.3 slot naming vs save-format §7 |
| 0x0071B070 | func | §1.4/§10.10 whether the base tweak reader filters on `main` |
| 0x00B982B0, 0x00B99810, 0x0071C6E0 | func | §4.1 save block writers (after the patch-copy data test) |
| 0x00BB6400, 0x00BA5DA0; literal 0x0118CF40 | func / xref | §10.9 readers of the five extra elements |
| 0x01312708–0x01312720 | range | §10.6 the six `Spline_Type` pointers (names 3–6) |
| 0x022CD108–0x022CE000 | range | §10.1 extent of the discount record array |
