# Re-derivation of spec-lua-api-behaviour.md §27 against fresh dumps (2026-10-01)

Inputs
- Spec: `team-a/spec-lua-api-behaviour.md`, §27.1–§27.26.
- Desk review: `scratchpad/review/adv_27.md`.
- Dump A (entries, wrappers, names): `/root/crreish-bus/results/20261001T021703-team-a-pwgq/` (`func/`, `wrap/`, `names/index.txt`).
- Dump B (callees, globals): `/root/crreish-bus/results/20261001T021707-team-a-jxxz/` (`callees/`, `globals/`).
- One supporting read outside the two inputs, used only for §27.2: `/root/crreish-bus/results/20261001T021721-team-a-hjzz/lua/index.txt` (the name-pair walk for `mission_is_complete` / `cell_is_mission_complete`).

Both jobs ran at depth 1 (the option lines in the job files were lost), 40 functions per item, 25 xrefs per global. Where that cap matters it is said below.

Labels: **CONFIRMED — disassembly** only for what is read in these dumps. HIGH CONFIDENCE / HYPOTHESIS / OPEN otherwise.

Shared facts used by several entries (all CONFIRMED — disassembly in Dump B):
- 0x00853b10 (`callees/func_0x00853b10.txt`): returns 1 for a null handle, or when bit 0x4 of the object's +0x33 byte is set, or when its +0x34 class byte is 0xff; otherwise 0. So "proceed when it reports zero" means "proceed when the object is alive", exactly as §1.1 and the §27.5 desk fix say.
- 0x0087ba20 (`callees/func_0x0087ba20.txt`): a one-instruction read of the global 0x024d8534 (the co-op session singleton). Takes no arguments.
- 0x00e1a1b0 (`callees/func_0x00e1a1b0.txt`): a one-instruction read of the global 0x02a45450 — the interface Lua state, as §26.23's 2026-10-01 correction already says. It is not a "builder handle".
- 0x00e0cef0 (`callees/func_0x00e0cef0.txt`): (name, luaState) → true when a Lua global of that name exists and is a function (it goes through the `_GetAnyGlobalSilent` helper and tests the Lua type code 6).
- 0x00e0ca80 (`callees/func_0x00e0ca80.txt`): takes four stack parameters and forwards them to 0x00e0c720 as (name, 0, p3, p4, luaState). The four-parameter signature §26.23's footnote describes is confirmed.
- The "return 1" stub family. Every one of these is a two-instruction function (load 1, return) with dozens of unrelated callers: 0x00d2f500, 0x00d2f540, 0x00d2f560, 0x00d34c90, 0x00d34d40, 0x00d34db0, 0x00d34dd0, 0x00d48d10, 0x00d48db0, 0x00d48e50, 0x0101b4a0, 0x0101b4f0, 0x0101b530, 0x0101b570, 0x0101b5b0, 0x0101ba60, 0x0101ba90, 0x0101bac0 (`callees/func_0x*.txt` for each; several also appear at depth 1 inside the entry dumps). They do nothing. Also 0x00db60a0 (returns 1 in AL) and 0x00754410 (a bare return, the stripped instrumentation stub the decompiler database already annotates as such).

---

## 27.1 `cat_mouse_results_select` (0x007bd240) — CONFIRMED

Evidence: `func/func_0x007bd240.txt`; `names/index.txt` (name string at 0x011569ec pushed inside 0x007bd280 next to the function pointer); `wrap/func_0x007bd280.txt` (0x007bd280 is called from 0x008430f0, the UI-wrapper anchor).

- Registration: name and function registered by 0x007bd280, which is a single-entry registrar (one closure push plus one table set). CONFIRMED — disassembly.
- Arguments: one number read from stack position 1 (the index is taken as the negated argument count, i.e. the first argument whatever the count), rounded through 0x00ea2596; no nil gate. Return: 0 values. CONFIRMED — disassembly.
- Hidden `this`: the zero-argument accessor 0x006a3900 is called; if its result is non-null it is moved into ECX and 0x006c8050 is called with the rounded index as the one pushed argument. 0x006c8050 pops 4 bytes on return and reads ECX as its object. CONFIRMED — disassembly (both sides of the call).
- New, from Dump B (`callees/func_0x006a3900.txt`, `callees/func_0x006c8050.txt`): 0x006a3900 is a one-instruction read of the global 0x014c2d10 (the cat-and-mouse / minigame singleton; the same pointer makes §27.25's query answer 4, see below). 0x006c8050's own body: for index 0 it sets the object's +0xe0 field to 1 when the object's +0x1c field equals 1, otherwise to 2; for index 1 it sets +0xe0 to 2; any other index leaves +0xe0 alone. If +0xe0 is then non-zero it builds a small message record whose type field is 0x2d and hands it to the co-op session object (0x0087ba20 → 0x024d8534) through 0x0086f1b0. HIGH CONFIDENCE that this is "set the local results choice and tell the session"; the exact meaning of 1 versus 2 is OPEN.

Spec text changes
- Strike: "[... OPEN — 0x006a3900/0x006c8050's own internal bodies.]"
- Replace with: "0x006a3900 reads the minigame singleton pointer at 0x014c2d10. 0x006c8050 (this = that object, index) sets the object's +0xe0 selection field (index 0 → 1 if the object's +0x1c field is 1, else 2; index 1 → 2; other indices leave it unchanged) and, when the field is non-zero, sends a type-0x2d message record to the co-op session object (0x0087ba20 → global 0x024d8534) via 0x0086f1b0. [CONFIRMED — disassembly for the field writes and the call chain; HIGH CONFIDENCE for the 'message to session' reading; OPEN — meaning of values 1/2.]"

## 27.2 `cell_is_mission_complete` (0x00a525a0) — CORRECTED (the OPEN marker and its hypothesis are wrong; the body is right)

Evidence: `func/func_0x00a525a0.txt` (its callers/refs line lists two data references: 0x00a233d5 inside the gameplay registrar 0x00a20840 and 0x0083bdca inside the cell wrapper 0x0083bd90); `wrap/func_0x0083bd90.txt` (the pair "cell_is_mission_complete" / 0x00a525a0 is stored at 0x0083bdc2/0x0083bdca, in an 8-entry table that also holds `cell_foreground_transition_out`, `C_cell_phone_mission_start`, `cell_is_map_disabled`, `cell_is_closing`, `cell_debug_all_enabled`, `cell_machinima_is_recording`, `cell_allow_camera`); `names/index.txt`; supporting: `20261001T021721-team-a-hjzz/lua/index.txt` lines 725–747 (the "mission_is_complete" string at 0x0117b57c is stored at 0x00a233ca in 0x00a20840 with 0x00a525a0 at 0x00a233d5 as its pointer, and "cell_is_mission_complete" is stored at 0x0083bdc2 with the same pointer).

Resolution of the address conflict: there is no conflict of function pointers. **One function, 0x00a525a0, is registered twice** — as `mission_is_complete` in the 1,014-entry gameplay table and as `cell_is_mission_complete` in the cellphone wrapper 0x0083bd90. The desk-review hypothesis ("this entry's real function pointer is probably a different address") is refuted; `cell_is_mission_complete`'s real pointer is 0x00a525a0. CONFIRMED — disassembly (both registrations read from the executable).

The "different body" worry is also resolved: §20.14 describes the body of 0x00a525a0 together with its callee 0x006d6f30 inlined (resolver 0x005f4c30, liveness gate, bit 0x4 of the +0x88 byte), while §27.2 describes 0x00a525a0 as a passthrough to 0x006d6f30. Both are correct readings of the same code.

Body of 0x00a525a0 (CONFIRMED — disassembly, 20 instructions): string at position 1 via `lua_tolstring`, passed to 0x006d6f30, result zero-extended from AL and pushed as a boolean, returns 1.

Body of 0x006d6f30 (`callees/func_0x006d6f30.txt`, `callees/func_0x005f4c30.txt`, CONFIRMED — disassembly): returns false for a null name. Otherwise resolves the name through 0x005f4c30 with ECX = the global object table at 0x02442750; that resolver requires the table's +0x265c count to be positive, looks the name up in a name-keyed hash map at table +0x2660 (bucket hash 0x00dab330, case-insensitive string compare), rejects objects with bit 0x10 set in their +0x33 byte, and requires bit 0x4 at offset +0xc of the class-descriptor row (0x02cc9900 indexed by the object's +0x34 class byte) — the "is a mission-kind object" check §20.14 cites. If the object resolves and 0x00853b10 reports it alive, the result is bit 0x4 of the object's +0x88 byte; otherwise false.

Spec text changes
- Strike (heading): "**[OPEN — address conflict with §20.14/§27.2, to be re-derived. HYPOTHESIS (desk review 2026-09-30, most likely reading): 0x00a525a0 is §20.14 `mission_is_complete`'s function — ... do not implement §20.14 or §27.2 from this text until then.]**"
- Replace with: "**[Resolved 2026-10-01, jobs 20261001T021703-team-a-pwgq / 20261001T021721-team-a-hjzz: 0x00a525a0 is registered under two names — `mission_is_complete` in the gameplay registrar 0x00a20840 (pair stored at 0x00a233ca/0x00a233d5) and `cell_is_mission_complete` in the cellphone wrapper 0x0083bd90 (pair stored at 0x0083bdc2/0x0083bdca). Same function, same body; §20.14 describes it with 0x006d6f30 inlined. CONFIRMED — disassembly.]**"
- Strike: "Note: this wrapper's paired function address lands in the address range ... **[Superseded: the address is in conflict with §20.14 — see the marker in this heading.]**"
- Replace with: "Note: the function sits in the gameplay-table address range because it is the gameplay table's `mission_is_complete`; the cellphone wrapper simply reuses the same pointer."
- Strike: "OPEN — 0x006d6f30's own internal body."
- Replace with: "0x006d6f30(name): false for a null name; resolves the name through 0x005f4c30 (this = the object table at 0x02442750; name-keyed hash map at table +0x2660; rejects +0x33 bit 0x10; requires class-row +0xc bit 0x4); false unless 0x00853b10 reports the object alive; then returns bit 0x4 of the object's +0x88 byte. [CONFIRMED — disassembly.]"
- Review-status line: replace "NEEDS-EXE ..." with "Re-derived against the executable 2026-10-01: CONFIRMED."
- Collateral at §20.14: strike its "[OPEN — address conflict ...]" marker and add "registered a second time as `cell_is_mission_complete` (§27.2)". Its body text is confirmed as written.

## 27.3 `Completion_should_wait_for_coop` (0x007bfc10) — CONFIRMED

Evidence: `func/func_0x007bfc10.txt`; `callees/func_0x00878ca0.txt`; `callees/func_0x00877ca0.txt`; `globals/xref_0x02282282.txt`; `names/index.txt` (registered in 0x007c04b0; `wrap/func_0x007c04b0.txt` is called from 0x008430f0).

- Arguments none (the count call's result is discarded); one boolean returned. CONFIRMED — disassembly.
- Control flow exactly as the spec says: internal flag preset to 1; 0x00878ca0 called with the literal "completion_screen" (string at 0x01156c9c); on success, if the byte at 0x02282282 is zero the flag becomes (the byte reached through the context's +4 pointer ≥ 1, signed compare), otherwise the flag is the result of 0x00877ca0 with ECX = the context and the literal 1 as the one pushed argument (that function pops 4 bytes and reads ECX, so the hidden `this` is confirmed on both sides); the pushed Lua value is the negation of the flag. CONFIRMED — disassembly.
- 0x02282282: four references — written 0 by 0x007bd9f0, written from AL by 0x007c05e0, read here and at 0x007bf710. It lies between 0x02282281 (§26.6) and 0x02282283 (§16.5) as the spec says. CONFIRMED — disassembly.
- New, 0x00878ca0(name): computes the CRC-32 of the name through 0x00d9e7e0 (hidden-ECX output slot on its own stack, seed 0, no length cap — the §23.8 convention), then walks the circular list rooted at the global 0x024d78e0 (link field +0x54) and returns the first node whose first byte equals the low byte of that hash, else null. CONFIRMED — disassembly for the mechanism; the one-byte match is what the code does (the node's first byte is a one-byte screen id and only the hash's low byte is compared) — HIGH CONFIDENCE that screen records store that byte at registration, not re-checked.
- New, 0x00877ca0(this = context, threshold byte): false if the context's +0x1c pointer is null; otherwise walks the player list hanging off that pointer's +0x54 field (next link at +0xb28c, circular), and for each player takes the byte at player +0x158 as an index into the context's per-slot table (+0xc, 5-byte rows, count at +0x10); returns false if the index is out of range, the row's +2 byte is zero, or the row's first byte is below the threshold; true once every player passes. With threshold 1 this reads as "every player's recorded state is at least 1". Together with §27.4 (which writes the local state byte to 1 and broadcasts it) the name fits: wait for co-op unless everyone is done viewing. HIGH CONFIDENCE for that reading; CONFIRMED — disassembly for the field walk.

Spec text changes
- Strike: "OPEN — 0x00878ca0/0x00877ca0's own internal bodies."
- Replace with: "0x00878ca0(name) hashes the name (CRC-32 via 0x00d9e7e0, hidden-ECX output slot) and returns the first record in the circular screen list rooted at 0x024d78e0 (link +0x54) whose first byte equals the low byte of the hash. 0x00877ca0(this = context, threshold) walks the player list reachable through the context's +0x1c pointer (+0x54 head, +0xb28c next) and returns true only when every player's per-slot row (context +0xc, 5-byte rows indexed by the player's +0x158 byte, count at +0x10) is enabled (row +2 non-zero) and has state (row +0) ≥ threshold. [CONFIRMED — disassembly; HIGH CONFIDENCE for the 'state' naming, see §27.4.]"

## 27.4 `Completion_user_is_done_viewing` (0x007bdc80) — CONFIRMED (the §2.10 question narrowed)

Evidence: `func/func_0x007bdc80.txt`; `callees/func_0x008788e0.txt` (callers list is capped and does not reach 0x00a3c9d0).

- Arguments none, return none; same "completion_screen" resolve; if resolved, ECX = context and the literal 1 is the one pushed argument to 0x008788e0. CONFIRMED — disassembly.
- 0x008788e0's own body settles its calling shape: it reads ECX as its object from the first instruction after the prologue (it dereferences the object's +4 pointer), takes exactly one stack argument (one byte, read at +0x45c into the frame) and pops 4 bytes on return. So it is a thiscall with one argument at every call site, including §2.10's; a "plain call with the conversation id" cannot execute this body. CONFIRMED — disassembly for the signature. Whether §2.10's site at 0x00a3c9d0 loads ECX from the current conversation object is HIGH CONFIDENCE (it must load something) but the site itself is not in these dumps — OPEN only for what that `this` is.
- 0x008788e0(this = context, state byte): if the context's +4 record is enabled (its +2 byte non-zero) and the state is not 0x81: writes the state byte into the record's first byte, builds a five-byte message (type 4, then the state byte), passes it through 0x008779b0(this = context, &message), calls the stripped instrumentation stub 0x00754410 with the format text "Changed your state to %u." (a no-op, see 27.20), then 0x00877860(this = context, 4, the +0x5c field of the context's +0x1c object). CONFIRMED — disassembly. Reading: "set my completion-screen state and broadcast it" — HIGH CONFIDENCE. So the literal 1 here is state 1 = done viewing, which is the threshold §27.3 tests.

Spec text changes
- Strike: "**[OPEN — desk review 2026-09-30: 0x008788e0 is a thiscall (this = context, 1) here but a plain call with the conversation id at §2.10; to be settled against the executable.]**"
- Replace with: "**[2026-10-01, job 20261001T021707-team-a-jxxz: 0x008788e0 reads its object from ECX unconditionally and pops exactly one 4-byte argument, so it is a thiscall with one byte-sized state argument at every site; §2.10's call therefore also carries a hidden `this` (what that object is at 0x00a3c9d0 remains OPEN until that site is dumped). CONFIRMED — disassembly for the signature.]**"
- Strike: "OPEN — 0x008788e0's own real signature/role, and the cross-reference to §2.10 above."
- Replace with: "0x008788e0(this = context, state): when the context's +4 record is enabled and state ≠ 0x81, stores the state byte in that record, sends a type-4 message carrying it through 0x008779b0/0x00877860, and logs 'Changed your state to %u.' through the no-op stub 0x00754410. [CONFIRMED — disassembly.] Here the state written is 1 ('done viewing'), the same threshold §27.3 checks across all players."
- Review-status line: "Re-derived 2026-10-01: CONFIRMED; the §2.10 receiver is the only remaining OPEN."

## 27.5 `vcust_set_camera_pos` (0x0081fa80) — CORRECTED

Evidence: `func/func_0x0081fa80.txt` (root listing 0x0081fa80–0x0081fb24); `callees/func_0x00812e40.txt`; `callees/func_0x00458230.txt`; `callees/func_0x00d9e8b0.txt`; `callees/func_0x00a9b080.txt` (root only read to its parameter uses); `globals/xref_0x01300b88.txt`; `names/index.txt` (registered in 0x00820aa0, which 0x008430f0 calls).

What is confirmed as written: one string argument; 0x00812e40 returns the 8-byte identity pair (it reads the globals 0x022cde00/0x022cde04 into EAX:EDX); the hash-keyed resolver 0x00458230 with ECX = 0x024433a8 and key 0x031d152c; the +0x33 bit-0x10 exclusion; class-row +6 bit 0x80; 0x00853b10 must report alive; the final case-insensitive compare against "Standard" (0x0115ee70) whose result is written to the byte at 0x01300b88; return none. CONFIRMED — disassembly.

What is wrong (item 1 and item 2 of the spec):
- The three stack dwords are pushed as 0, 0, then ECX (ECX still holds the zero-extended class byte from the class-row check, so that register happens to be what gets pushed into the lowest slot); ECX is then pointed at that lowest slot; -1, 0 and the name string are pushed; 0x00d9e8b0 is called. 0x00d9e8b0 is the lower-casing CRC-32 (table at 0x01320da0) and **always writes its result through ECX** — into the lowest of the three dwords, overwriting the class byte. It pops its three explicit arguments. Then the resolved object is pushed and 0x00a9b080 is called, reading the four dwords now on the stack: (object, hash of the preset name, 0, 0). So the class-index byte never reaches 0x00a9b080; the second argument is the name hash. The "hidden ECX buffer {0, 0, classIndexByte}" is an output slot for the hash, and the push of ECX is just the cheapest way to reserve that slot.
- 0x00a9b080's own body confirms it: it fetches a record through the object's +0xbf4 pointer (0x00a9afd0), iterates that record's camera entries (count at +0xac8, 0x1c-byte stride) comparing its second parameter against each entry's +0x38 field, and in one branch hashes the literal "plane_fighter02" with the same 0x00d9e8b0 and compares the hash against a field — i.e. its second parameter is a name hash. Its fourth parameter is read as a byte flag (zero here). Its third parameter is not read in the part of the body dumped. CONFIRMED — disassembly for the parameter roles read; the rest of 0x00a9b080 (camera positioning math) is not re-derived.
- Memory order of the three dwords: lowest = hash output (was the class byte), then 0, then 0. Push order was 0, 0, class byte.
- 0x01300b88 is file-backed with initial value 1, so "Standard preset active" starts true; it is also cleared by 0x00819810 and set by 0x008205e0. CONFIRMED — disassembly (xref list).

Spec text changes
- Strike item 1 entirely ("A hidden-ECX buffer-pointer argument ... The real call is **0x00d9e8b0(this = pointer to {0, 0, classIndexByte}, presetNameString, 0, -1)** ... [OPEN — desk review ... to be settled against the executable.]")
- Replace with: "1. 0x00d9e8b0 is the lower-casing CRC-32 name hash (§11.8/§13.29) with a hidden-ECX output pointer, the same convention as §23.8's 0x00d9e7e0. Three dwords are pushed (0, 0, then the class-index register, whose value is irrelevant because the slot is about to be overwritten), ECX is pointed at the lowest of them, and the hash of the Lua-given preset name is written there; the call pops only its three explicit arguments (name, seed 0, no length cap). [CONFIRMED — disassembly.]"
- Strike item 2's "The real call is **0x00a9b080(resolvedObject, classIndexByte, 0, 0)**".
- Replace with: "The real call is 0x00a9b080(resolvedObject, crc32(presetName), 0, 0): the three dwords left on the stack are its 2nd–4th parameters, and the 2nd is the hash just written, not the class byte. 0x00a9b080 matches that hash against the +0x38 field of the vehicle's camera-preset entries (count at record +0xac8, 0x1c-byte stride, record reached through the object's +0xbf4 pointer via 0x00a9afd0) and reads the 4th parameter as a byte flag. [CONFIRMED — disassembly for the argument roles; OPEN — the positioning math in 0x00a9b080.]"
- Side-effects sentence: strike "calls 0x00d9e8b0 (... exact role here OPEN, see item 1) and applies it via 0x00a9b080"; replace with "hashes the preset name and selects the vehicle's camera preset with that hash via 0x00a9b080".
- Add to the globals sentence: "0x01300b88 starts at 1 in the file (Standard preset assumed) and is also written by 0x00819810 (clears) and 0x008205e0 (sets)."
- Review-status line: "Re-derived 2026-10-01: CORRECTED (hash output slot / second argument of 0x00a9b080)."

## 27.6 `pause_menu_has_seen_display_cal_screen` (0x007e0490) — CONFIRMED

Evidence: `func/func_0x007e0490.txt`; `globals/xref_0x02297c80.txt`; `wrap/func_0x007e18e0.txt` (called from 0x008430f0; pair at 0x007e1a01/0x007e1a0c).

Nine instructions: count call, zero-extended byte read of 0x02297c80, push boolean, return 1. CONFIRMED — disassembly. The byte is written to 1 by 0x007cf850 and from a register by 0x00841320, read also by 0x008411a0. Not file-backed (starts 0).

Spec text change
- Append to the body sentence: "(set to 1 by 0x007cf850, written from a register by 0x00841320, read by 0x008411a0; zero at start). [CONFIRMED — disassembly, xref list capped at 25 but only 5 uses exist.]"

## 27.7 `msn_text_adventure_set_screen` (0x008410c0) — CORRECTED (wrapper address)

Evidence: `func/func_0x008410c0.txt`; `wrap/func_0x00841100.txt` (the dump root for the requested 0x00841100 is 0x008410f0 — 0x00841100 is an instruction in the middle of that function); `globals/xref_0x01301124.txt`.

- Body as written: rounded number written to 0x01301124, no call, return none. CONFIRMED — disassembly.
- Wrapper: the registrar is **0x008410f0** (a single-entry registrar: one closure push, one table set; called from 0x008430f0 at 0x00843171). 0x00841100 is the address of the `push` instruction inside it, not a function entry.
- 0x01301124 is file-backed with initial value -1, and 0x00841090 reads it and immediately resets it to -1 — a consume-once getter. So the Lua setter posts a screen index that the game side consumes and clears. CONFIRMED — disassembly (xref list, 3 uses in 2 functions).

Spec text changes
- Strike (heading): "wrapper 0x00841100"; replace with "wrapper 0x008410f0 (single-entry registrar)".
- Append to the body: "0x01301124 starts at -1 in the file and is read-then-reset to -1 by 0x00841090, i.e. the value is consumed once by the game side. [CONFIRMED — disassembly.]"

## 27.8 `horde_results_set_end_action` (0x007c6370) — CORRECTED (wrapper pinned; callee is a global setter)

Evidence: `wrap/func_0x007c63a0.txt` (root 0x007c63a0, body 0x007c63a0–0x007c63ca, called from 0x008430f0 at 0x00843159; pushes 0x007c6370 then sets the table entry for the string at 0x0115754c); `func/func_0x007c6370.txt`; `callees/func_0x006e4600.txt`.

- The wrapper is exactly 0x007c63a0, a single-entry registrar. CONFIRMED — disassembly.
- Body as written (rounded number forwarded to 0x006e4600). CONFIRMED — disassembly.
- 0x006e4600 stores its argument into the global 0x012f3b48 (file-backed, initial value 2). CONFIRMED — disassembly. The desk review's OPEN is closed; the "OPEN — 0x006e4600's own internal body" is closed.

Spec text changes
- Strike (heading): "wrapper 0x007c63a0-adjacent **[OPEN — desk review 2026-09-30: wrapper not pinned ...]**"; replace with "wrapper 0x007c63a0 (single-entry registrar, called from 0x008430f0)".
- Strike: "OPEN — 0x006e4600's own internal body."; replace with "0x006e4600 is a bare setter of the global 0x012f3b48 (initial value 2 in the file). [CONFIRMED — disassembly.]"

## 27.9 `garage_preview_vehicle` (0x005f8890) — CONFIRMED

Evidence: `func/func_0x005f8890.txt`; `callees/func_0x005f8670.txt`; `globals/xref_0x014a1a80.txt`, `xref_0x014a1d1c.txt`, `xref_0x014a1d28.txt`; `wrap/func_0x005fc500.txt` (called from 0x008430f0).

- Body as written: rounded index into the dword array at 0x014a1a80, compare with 0x014a1d1c, update and call 0x005f8670 only on change, return none. CONFIRMED — disassembly.
- 0x014a1a80 is written by 0x005fb000 (the array fill); 0x014a1d1c is reset to 0 by 0x005fb9b0 and read by 0x005fa2e0/0x005fa570. CONFIRMED — disassembly.
- 0x005f8670 (new): if the flag 0x014a1ce8 is set and the identity pair 0x014a1cf0/0x014a1cf4 is non-zero, it resolves that pair through the same 0x00458230/0x031d152c resolver, applies the same vehicle-kind and liveness gates as §27.5, clears the object's +0x1ac8 field and calls 0x00853ea0(object, 0, 0); then clears 0x014a1ce8 and calls 0x00d9e140 with ECX = 0x012ece54 and the literal 0xfa. So it tears down the current preview object; HIGH CONFIDENCE that 0x00853ea0 is the object-release primitive. CONFIRMED — disassembly for the control flow.

Spec text change
- Strike: "OPEN — 0x005f8670's own internal body."; replace with "0x005f8670 releases the current preview vehicle (identity pair 0x014a1cf0/0x014a1cf4 resolved through 0x00458230/0x031d152c, same gates as §27.5, then 0x00853ea0(object, 0, 0)), clears 0x014a1ce8 and calls 0x00d9e140(this = 0x012ece54, 0xfa). [CONFIRMED — disassembly; HIGH CONFIDENCE that 0x00853ea0 is the release primitive.]"

## 27.10 `game_lobby_coop_finished` (0x007ab170) — CONFIRMED (the OPEN on 0x005a0580 is closed)

Evidence: `func/func_0x007ab170.txt`; `callees/func_0x005a0580.txt`; `globals/xref_0x0224244c.txt`; `wrap/func_0x007abbb0.txt` (called from 0x008430f0).

- The byte at 0x0224244c is set to 1. Four pushed values reach 0x005a0580: -1, 1, 0 and a 32-bit float 1.0. The float is not pushed: a load-one / store-float writes it into the slot that still holds the state pointer from the count call (the stack is cleaned up once, 0x10 bytes, after the call). The count is right, the mechanism is the stale-slot reuse §26.2 names. CONFIRMED — disassembly.
- 0x005a0580(a, b, c, f) is a composite of the established fade family: 0x005a0270(a, b, c, f), then 0x00722090(1), then 0x0059f8c0(a, 0, b). With §26.23's signature for 0x0059f8c0 (durationMs, callback, flag) this is (duration -1, no callback, flag 1). So it is a fade-family sibling, as the spec guessed. CONFIRMED — disassembly for the three forwarded calls.

Spec text changes
- Strike: "all 4 arguments confirmed genuinely explicit via raw disassembly ... not a hidden argument"; replace with "the first three are pushed; the fourth, a 32-bit float 1.0, is stored into the stale stack slot left by the count call (one 0x10-byte cleanup after the call) — the §26.2 stale-slot mechanism, here used by the compiler deliberately."
- Strike: "OPEN — 0x005a0580's own internal body and its relationship (if any) to the established fade-command family."; replace with "0x005a0580(a, b, c, f) calls 0x005a0270(a, b, c, f), then 0x00722090(1), then 0x0059f8c0(a, 0, b) — i.e. a fade request with duration -1, no callback, flag 1 (§26.23's signature). [CONFIRMED — disassembly.]"

## 27.11 `dialog_box_force_close` (0x007c4960) — CORRECTED

Evidence: `func/func_0x007c4960.txt` (root listing 0x007c4960–0x007c4af7); `callees/func_0x00d9e4c0.txt`, `func_0x00d9e400.txt`, `func_0x00da73c0.txt`, `func_0x00e1a1b0.txt`, `func_0x00e0cef0.txt`, `func_0x00e0ca80.txt`, `func_0x007c3050.txt`; `globals/xref_0x02282d10.txt`, `xref_0x02282d14.txt`, `xref_0x02282d40.txt`; `wrap/func_0x007c5630.txt` (called from 0x008430f0).

Confirmed as written: the §23.18 gate (non-zero id, low 16 bits < 4, slot = 0x02282d40 + 0x184·(id & 0xffff), slot +0x12c must equal the id); the two ECX-receiver predicates on slot +0xc; the +0x13a "close pending" byte; the +0x13b early-out; +0x139/+0x13b set; the +0x13c function pointer called with (slot, -1); the 0x00da73c0 test on slot +0x140; the 0x00e1a1b0 → 0x00e0cef0 → 0x00e1a1b0 → 0x00e0ca80 chain with the two zero pushes being 0x00e0ca80's 3rd/4th parameters (0x00e1a1b0 reads no stack at all); the two float pushes (-1.0 from the constant at 0x012a2d54, then 1.0) through 0x00e0ce20 with ECX = the builder, and the dispatch 0x00e0cd00(&builder); the final 0x007c3050(this = slot, 0) and the list surgery. CONFIRMED — disassembly.

Corrections and closures:
1. 0x00d9e4c0 and 0x00d9e400 are not "thiscall here but zero-argument elsewhere": each reads a dword through ECX unconditionally and takes no stack argument anywhere. 0x00d9e4c0 returns true when that dword is ≥ 0 ("timer armed"). 0x00d9e400 returns false when it is negative, otherwise compares it with the running tick counter 0x01320d9c with wrap-around (a 900,000,000-unit window) and returns true when the stored time has been reached ("timer expired"). The same pair is applied to another object's +0x240 field inside 0x0087e0e0 (`callees/func_0x00703210.txt`, depth 1), confirming the pattern. So: if the slot's +0xc timer is armed and not yet expired, the dialog cannot close yet and +0x13a is set. The four other sites the desk review lists necessarily pass ECX too (the body cannot run otherwise). CONFIRMED — disassembly.
2. 0x00da73c0(string) returns true when the string is empty or consists only of spaces, tabs, CR and LF. Slot +0x140 is therefore a callback-name string, and the named callback is fired only when that name is non-blank. CONFIRMED — disassembly.
3. 0x00e1a1b0 is the interface Lua state (global 0x02a45450), not a "builder handle"; 0x00e0cef0(name, luaState) answers "does a Lua global function of this name exist"; 0x00e0ca80(name, luaState, 0, 0) returns the builder. The spec's wording "resolves a builder handle (0x00e1a1b0)" and "checks it against the slot's +0x140 sub-field via 0x00e0cef0(targetPointer, builderHandle)" must be reworded (§26.23 already corrected 0x00e1a1b0 on 2026-10-01).
4. The field copied into the builder's +0x14 comes from the **slot's +0x114** field, not "+0x14" (instruction at 0x007c4a3f reads [slot + 0x114]). In 0x007c3050 the same builder field is filled from the global 0x02282d20, and in 0x00841af0 (§27.20) from 0x02317b34 — it is the script-context tag the §23.15/§8.24 "+0x14 field" idiom caches. CONFIRMED — disassembly.
5. 0x007c3050(this = slot, flag): when the flag is non-zero it fires the named callback "dialog_build" with (2.0, the slot's id) through the same builder family; here the flag is 0 so that is skipped. It then resets the slot: +0x130 and +0xc (the timer) to -1, the +0x140 name cleared, +0x13c/+0x12c/+0x138/+0x139/+0x13a zeroed, +0x118 to -2, +0x120 zeroed, and every node on the slot's +0x110 sub-list is moved to the pool rooted at 0x02282d18. CONFIRMED — disassembly for the field resets; the +0x110 list is HIGH CONFIDENCE a per-dialog button/option list.
6. The two anchors: 0x02282d10 is the head of the active dialog ring (+4 next, +8 prev); after unlinking, the slot is appended at the tail of the ring rooted at 0x02282d14 (inserted before the anchor). 0x007c4c90 initialises both anchors. The "closed/free pool" reading stays HYPOTHESIS; what is confirmed is that 0x02282d14 is a second ring on the same records and that every close path in the dialog code (0x007c31b0, 0x007c34b0, 0x007c37a0, 0x007c3f40, 0x007c5740 and this function) performs the same move.
7. The §26.6 comparison ("the same on-close callback vtable-style slot idiom ... cf. §26.6's dialog-close-shaped callback") is a loose analogy: §26.6's 0x007bffe0 is a fade-completion callback. What +0x13c actually is comes from 0x007c3d80 (`func/func_0x00842840.txt` depth 1): the dialog-open helper stores its fourth parameter into the new slot's +0x13c. So +0x13c is the result callback supplied when the dialog was opened, invoked here with -1 ("closed without a result"). CONFIRMED — disassembly.

Spec text changes
- Strike: "calls two predicates via thiscall (`this` = the slot's own +0xc sub-object; both rendered by the decompiler with zero visible arguments at all, including the implicit receiver — 0x00d9e4c0 then, conditionally, 0x00d9e400) to decide whether the dialog can close immediately; if not closeable, sets a 'close pending' byte at the slot's own +0x13a and returns. **[OPEN — desk review ... to be settled against the executable.]**"
- Replace with: "tests the slot's +0xc timer with the ECX-receiver pair 0x00d9e4c0 ('armed': the dword is ≥ 0) and 0x00d9e400 ('expired': the dword has been reached by the tick counter 0x01320d9c, wrap-around safe). Neither takes a stack argument at any site. If the timer is armed and not yet expired, the close is deferred: the 'close pending' byte at +0x13a is set and the function returns. [CONFIRMED — disassembly.]"
- Strike: "if a per-slot function-pointer field at +0x13c is non-null, calls it (this = slot, explicit arg -1) — the same 'on-close callback' vtable-style slot idiom already seen elsewhere (cf. §26.6's dialog-close-shaped callback)"; replace with "if the slot's +0x13c result callback (installed by the dialog-open helper 0x007c3d80 from its fourth parameter) is non-null, calls it with (slot, -1), both pushed".
- Strike: "if a further predicate on the slot's own +0x140 sub-field (0x00da73c0 — new address, not previously catalogued) returns false"; replace with "if the slot's +0x140 callback-name string is not blank (0x00da73c0 returns true for an empty or all-whitespace string)".
- Strike: "resolves a builder handle (0x00e1a1b0), checks it against the slot's +0x140 sub-field via **a newly-observed 9th member of that family, 0x00e0cef0(targetPointer, builderHandle) → boolean** ... confirming it's a generic 'does this target have a resolvable named callback' check, not fade-specific); if true, resolves a target handle via 0x00e0ca80, sets the target's own +0x14 field from the slot's own +0x14 field"; replace with "fetches the interface Lua state (0x00e1a1b0 → 0x02a45450), asks 0x00e0cef0(name, luaState) whether a Lua global function of that name exists, and if so opens the builder with 0x00e0ca80(name, luaState, 0, 0) and tags it with the slot's +0x114 script-context field (builder +0x14)".
- Strike: "calls 0x007c3050(this = slot, 0), then removes the slot from a doubly-linked list"; replace with "calls 0x007c3050(this = slot, 0) — which resets the slot's fields (timer to -1, name/callback/id/flags cleared) and returns its +0x110 sub-list nodes to the pool at 0x02282d18 without firing the 'dialog_build' callback (that needs a non-zero flag) — then unlinks the slot from the active ring".
- Strike: "OPEN — 0x00d9e4c0/0x00d9e400/0x00da73c0's own internal bodies and the precise distinction between the two list-anchor globals"; replace with "OPEN — the role of the 0x02282d14 ring (HYPOTHESIS: closed/free pool; every close path in 0x007c31b0/0x007c34b0/0x007c37a0/0x007c3f40/0x007c5740 appends to it)".
- Review-status line: "Re-derived 2026-10-01: CORRECTED (+0x114 field, predicate roles, Lua-state accessor naming, +0x13c origin)."

## 27.12 `game_autosave` (0x00841f50) — CONFIRMED

Evidence: `func/func_0x00841f50.txt`; `callees/func_0x00b95060.txt`; `names/index.txt` (registered in 0x00845aa0 at 0x00845d54/0x00845d5f).

Seven-instruction body as written. CONFIRMED — disassembly. 0x00b95060 (new): autosaves (tail-jump to 0x00b94ff0) only when the byte 0x012fcadc is zero (file-backed, initial value 1 — autosave is blocked until something clears it), the byte 0x0290ceca is zero, the mission-active predicate 0x006cecb0 (§1.9's correction: mission pointer 0x014c8460 non-null and phase 0x014c7a14 ≠ 8) is false, 0x006f8370 is false, and the byte 0x0229a317 is zero. CONFIRMED — disassembly for the gates; 0x006f8370 and 0x00b94ff0 not read.

Spec text change
- Append: "0x00b95060 performs the save (0x00b94ff0) only when 0x012fcadc (initial 1 in the file) and 0x0290ceca are zero, no mission is active (0x006cecb0), 0x006f8370 reports false and 0x0229a317 is zero; otherwise it does nothing. [CONFIRMED — disassembly for the gate; OPEN — 0x006f8370, 0x00b94ff0.]"

## 27.13 `game_send_pause_menu_player_invite` (0x008440e0) — CORRECTED (0x00d34dd0 is a stub)

Evidence: `func/func_0x008440e0.txt` (0x00d34dd0 and 0x007c93b0 at depth 1); `callees/func_0x00d34dd0.txt`.

- Shape as written. CONFIRMED — disassembly.
- 0x00d34dd0 is a two-instruction "return 1" stub with many unrelated callers. It is not a syslink-discovery helper; the proximity argument is void. CONFIRMED — disassembly.
- 0x007c93b0(slot): false unless slot < the count at 0x02289b94; otherwise returns whether 0x0088e690(record, 0) returned 0, where record = 0x02289c28 + slot·0x20c (a player/friend list of 0x20c-byte records). CONFIRMED — disassembly; 0x0088e690 not read.

Spec text changes
- Strike: "calls a zero-argument helper (0x00d34dd0 — in the same narrow address neighborhood as the already-documented syslink-discovery helpers 0x00d34cf0/§23.11) **[Qualified (desk review 2026-09-30): proximity only — ... no syslink role is established for 0x00d34dd0.]**"; replace with "calls 0x00d34dd0, a two-instruction `return 1` stub (same family as §10.9's 0x0101ba60 and §2.8's 0x00d34db0) — inert".
- Strike: "OPEN — 0x00d34dd0/0x007c93b0's own internal bodies"; replace with "0x007c93b0(slot) bounds-checks the slot against the count at 0x02289b94, then reports whether 0x0088e690(0x02289c28 + slot·0x20c, 0) returned zero. [CONFIRMED — disassembly; OPEN — 0x0088e690.]"

## 27.14 `game_can_send_player_invite` (0x00844120) — CORRECTED (0x0101b530 is a stub)

Evidence: `func/func_0x00844120.txt`; `callees/func_0x0101b530.txt`.

- Shape as written. CONFIRMED — disassembly.
- 0x0101b530 is a two-instruction "return 1" stub. The paragraph reinterpreting it as a generic "begin/poll an async task" primitive is wrong, and §8.21's HIGH CONFIDENCE reading was already withdrawn there on 2026-10-01 ("0x0101b530 is a stub that returns 1 and does nothing"). CONFIRMED — disassembly.
- 0x007c93e0(slot): bounds check against 0x02289b94, then a tail call to 0x0088dfd0 with the record pointer 0x02289c28 + slot·0x20c. CONFIRMED — disassembly; 0x0088dfd0 not read.

Spec text changes
- Strike the whole bold sentence "**0x0101b530 is the SAME address §8.21 ... rather than something zscene-specific.** **[Qualified ... This also bears on §8.21's HIGH CONFIDENCE reading of 0x0101b530.]**"
- Replace with: "0x0101b530 is a two-instruction `return 1` stub (§8.21's 2026-10-01 correction agrees); its call here is inert."
- Strike: "the 0x0101b530 cross-reference to §8.21 is a new scope-broadening observation, not a contradiction; OPEN — 0x007c93e0's own internal body"; replace with "0x007c93e0(slot) bounds-checks against 0x02289b94 and tail-calls 0x0088dfd0 on the record at 0x02289c28 + slot·0x20c. [CONFIRMED — disassembly; OPEN — 0x0088dfd0.]"

## 27.15 `game_set_coop_friendly_fire` (0x00844510) — CONFIRMED (text clarified; helpers are stubs)

Evidence: `func/func_0x00844510.txt` (0x0101ba90, 0x0101bac0, 0x00d2f500, 0x007031e0 at depth 1); `callees/func_0x00844440.txt` (the join-type setter, for comparison); `globals/xref_0x012f4500.txt`.

- The branch table is exactly as the spec says: mode 0 → register set to 1, then the shared tail (0x00d2f500(); 0x007031e0(1)); mode 1 → 0x0101bac0(); 0x007031e0(2); mode 2 → 0x0101ba90(); 0x007031e0(0); any other value → register stays 0, then the same shared tail (0x00d2f500(); 0x007031e0(0)). There is no contradiction: "the default path" is one code block whose argument register is preset to 1 for mode 0 and left 0 for an invalid mode. CONFIRMED — disassembly.
- All three helpers are "return 1" stubs. CONFIRMED — disassembly.
- 0x007031e0(value): writes the value to the global 0x012f4500 unless a co-op session object exists (0x0087ba20 → 0x024d8534) whose +0x5c field differs from its +0x58 field (the §16.9 head/tail test, i.e. only the host or a solo player may change it). 0x012f4500 is file-backed with initial value 1 — internal 1 = external 0, matching the save-format note in §27.16. CONFIRMED — disassembly.
- Comparison with the join-type setter 0x00844440 (not a §27 entry): it maps 0 → 0, 1 → 1, anything else → 2 with no rotation, calls three other stubs (0x00d48d10, 0x00d34c90, 0x0101b5b0) and its setter 0x00703210 additionally broadcasts the new value to the session when it is the host. §10.9's "no rotation there" is confirmed; the friendly-fire setter has no broadcast.

Spec text changes
- Strike: "**[OPEN — desk review 2026-09-30: as written, mode 0 and invalid input both take 'the default path' yet pass different literals (1 vs 0), which the text does not explain, and the per-branch helpers may be inert `return 1` stubs as in the §10.9 sibling; to be settled against the executable.]**"
- Replace with: "The 'default path' is a single code block whose argument is pre-loaded: 1 for mode 0, 0 for any unrecognised mode. 0x00d2f500, 0x0101bac0 and 0x0101ba90 are all two-instruction `return 1` stubs; the only effect of each branch is the 0x007031e0 call. [CONFIRMED — disassembly.]"
- Strike: "OPEN — 0x00d2f500/0x0101bac0/0x0101ba90/0x007031e0's own internal bodies"; replace with "0x007031e0(v) stores v into 0x012f4500 (file value 1) unless a co-op session object (0x0087ba20) exists whose +0x5c ≠ +0x58, i.e. only the host or a solo player may change it; unlike the join-type setter 0x00703210 it does not broadcast. [CONFIRMED — disassembly.]"

## 27.16 `game_get_coop_friendly_fire` (0x00844580) — CONFIRMED (gap filled)

Evidence: `func/func_0x00844580.txt`; `callees/func_0x007029a0.txt`; `globals/xref_0x012f4500.txt`.

- Raw value from 0x007029a0 (a one-instruction read of 0x012f4500): 0 → 2, 1 → 0, 2 → 1; **any other raw value → 0** (the result register is zeroed before the branch and only the three matches change it). Pushed as a number. CONFIRMED — disassembly.
- Initial value of 0x012f4500 in the file is 1, so the Lua-visible value at start is 0. Writers: only 0x007031e0 (§27.15); 0x00704280 passes the address somewhere (settings load/save, HYPOTHESIS). CONFIRMED — disassembly for the xref list.

Spec text changes
- Strike: "**[OPEN — desk review 2026-09-30: the value pushed when the raw value is outside {0,1,2} is not stated.]**"; replace with "A raw value outside {0,1,2} is pushed as 0. The raw global 0x012f4500 is 1 in the file, so the Lua-visible default is 0; its only writer is 0x007031e0 (§27.15), and 0x00704280 takes its address (settings persistence, HYPOTHESIS). [CONFIRMED — disassembly.]"
- Strike: "OPEN — 0x007029a0's own internal body"; replace with "0x007029a0 is a one-instruction read of 0x012f4500."

## 27.17 `game_send_party_invites` (0x007c9f50) — CONFIRMED

Evidence: `func/func_0x007c9f50.txt` (six instructions: count call, zero return); `names/index.txt` shows 0x007c9f50 also as the pointer for the names adjacent to `game_set_coop_friendly_fire`, `game_get_coop_friendly_fire` and `Completion_should_wait_for_coop`, i.e. the shared stub the desk review cites. CONFIRMED — disassembly. No text change beyond the already-applied cross-reference.

## 27.18 `game_is_connected_to_network` (0x008427e0) — CORRECTED (thunk reading inverted; always true)

Evidence: `func/func_0x008427e0.txt`; `callees/func_0x0086fdf0.txt` (0x0086fdf0 is a single jump to 0x00db60a0; 0x00db60a0 is two instructions: load 1 into AL, return).

- The call instruction's operand is 0x0086fdf0, as the spec says. But 0x0086fdf0 is itself a one-instruction jump stub to 0x00db60a0, and the decompiler's thunk label named exactly that target. The spec has the caution backwards: the label was right, and "the real target is 0x0086fdf0" names the jump stub rather than the function. The body that runs is 0x00db60a0, which returns 1 unconditionally. So this binding always returns true on PC. CONFIRMED — disassembly.
- 0x0086fdd0 / 0x0086fde0 / 0x0086fdf0 are three adjacent jump stubs to 0x008703b0 / 0x008703c0 / 0x00db60a0 (see §27.19); they are not three accessors.

Spec text changes
- Strike: "**A further, confirmed instance of this document's thunk-label caution (§23.13/§23.1).** The decompiled pseudocode labels the call as a thunk to one address; raw disassembly shows the real target is **0x0086fdf0** — a third confirmed member of the small adjacent platform-state-accessor family this tranche resolves (0x0086fdd0/0x0086fde0/0x0086fdf0, each exactly 16 bytes apart; 0x0086fde0 is the already-established real target behind `game_is_connected_to_service`, §23.13)."
- Replace with: "The call operand is 0x0086fdf0, which is a one-instruction jump stub to 0x00db60a0; the decompiler's thunk label names that target correctly. 0x00db60a0 returns 1 unconditionally, so this binding always reports connected. 0x0086fdd0/0x0086fde0/0x0086fdf0 are three adjacent jump stubs (to 0x008703b0, 0x008703c0, 0x00db60a0), not accessors in their own right. [CONFIRMED — disassembly.]"
- Side-effects: strike "Platform network-connectivity check."; replace with "Always true on PC (the platform check was compiled out)."

## 27.19 `game_is_signed_in` (0x00842810) — CORRECTED (same inversion; body identified)

Evidence: `func/func_0x00842810.txt`; `callees/func_0x0086fdd0.txt` (jump to 0x008703b0; 0x008703b0 calls the Steam user-interface accessor through the import slot 0x0101c54c and returns whether the pointer is non-null); `callees/func_0x0086fde0.txt` and `func_0x008703c0.txt` for the sibling.

- Real body: 0x008703b0 — "Steam user interface pointer is non-null". The sibling behind §23.13 (0x0086fde0 → 0x008703c0) additionally calls the first virtual method after the vtable's slot 0 on that interface (the logged-on query) and returns its result. CONFIRMED — disassembly.

Spec text changes
- Strike: "Same thunk-label caution as item 27.18: the decompiler's displayed thunk label does not match the real target, which raw disassembly confirms is **0x0086fdd0** — a confirmed member (~~the fourth~~; the family has three, per §27.18) of the same 16-byte-spaced platform-accessor family (0x0086fdd0/e0/f0)."
- Replace with: "The call operand 0x0086fdd0 is a one-instruction jump stub to 0x008703b0, which returns true when the Steam user interface pointer (fetched through the import at 0x0101c54c) is non-null. The sibling 0x0086fde0 (§23.13) jumps to 0x008703c0, which also requires that interface's logged-on query to return true. [CONFIRMED — disassembly.]"
- Collateral at §23.13: its sentence "the actual instruction at this call site is `CALL 0x0086fde0` — the REAL target address, not the thunk's own displayed entry point at 0x008703c0" has it backwards too: 0x0086fde0 is the jump stub and 0x008703c0 is the function. Strike and replace with the same wording.

## 27.20 `game_sign_into_network` (0x00842840) — CORRECTED (no callback is registered; the string is a Lua callback name)

Evidence: `func/func_0x00842840.txt` (with 0x00e0ceb0, 0x00da7930, 0x009ed510, 0x0084a1b0, 0x007c3d80 at depth 1); `callees/func_0x009ed510.txt` (0x009ed510 is a jump to 0x00754410; 0x00754410 is a single return instruction, annotated in the decompiler database as the stripped instrumentation stub with ~1146 references); `callees/func_0x00841af0.txt`; `globals/xref_0x02317b14.txt`, `xref_0x02317b34.txt`, `xref_0x02317b68.txt`.

Confirmed as written: argument 1 boolean (position 1), argument 2 string (position 2, read as index 1 − count — a one-argument call would hand index 0 to the string read); 0x00e0ceb0 (an accessor that returns entry 0x02a44d14[i] for the index i held at 0x02a44d10 when 1 ≤ i ≤ 16, else null) and the cache of its +0x14 into 0x02317b34; bounded copy of the string into 0x02317b14 with limit 0x1f via 0x00da7930 (a `strncpy` plus forced terminator); the two localisation lookups through 0x0084a1b0 ("MENU_PC_SIGN_IN_ERROR", then "MENU_TITLE_NOTICE"); the dialog open through 0x007c3d80; return none. CONFIRMED — disassembly.

Corrections:
1. The "thunk-target correction" is inverted exactly as in §27.18/19: the call operand 0x009ed510 is a jump stub to 0x00754410, and 0x00754410 is a bare return. **Nothing is registered.** The two pushed values (the boolean and the code address 0x00841af0) are discarded. CONFIRMED — disassembly.
2. 0x00841af0, the never-registered callback: if the string buffer 0x02317b14 is non-empty it fires the Lua named callback whose name is that buffer (0x00e0ca80 with the interface Lua state, builder tagged with 0x02317b34, one float argument converted from the callback's integer parameter, dispatched via 0x00e0cd00). So the Lua-given string is the **name of a Lua function to call back on completion**, and 0x02317b34 is the script-context tag for it — the same pattern as §27.11 item 4. On PC that callback can never fire. CONFIRMED — disassembly for 0x00841af0's body; the "never fires" conclusion follows from 0x00754410 being empty.
3. The notice dialog: 0x0084a1b0(key, override) resolves a localisation key by CRC (0x00d9e740 → 0x00849950) and falls back to a "!!key!!"-style wide string when missing. The three extra zero dwords pushed before the first 0x0084a1b0 call are not its arguments (it takes two) — they are left on the stack and read by 0x007c3d80 as its 3rd–5th parameters, the §26.2 stale-slot mechanism again. 0x007c3d80(title, body, 0, 0, 0) opens a dialog through 0x007c3310, sets its text via 0x007c18a0, uses the default button label from 0x01301a80 (the wide string "OK" at 0x01164094) when the 5th parameter is zero, and stores the 4th parameter (zero here) into the slot's +0x13c result-callback field. So the function always shows a "sign-in error" notice with an OK button and no result callback. CONFIRMED — disassembly (0x007c3d80 read in full; 0x007c3310/0x007c18a0/0x007c2ac0 not read).
4. Return: none (no push). The desk review's "no Return line" gap is filled.

Spec text changes
- Add after Arguments: "**Return:** none."
- Strike the whole paragraph "**A further, confirmed thunk-label mismatch — ... the third new instance in this tranche ... of this recurring caution:** the decompiled pseudocode shows the boolean argument and a fixed code address (0x00841af0) passed to a thunk-displayed call; raw disassembly confirms the real target is **0x009ed510**, called with 2 genuinely explicit arguments (the boolean, and the literal address 0x00841af0 — most plausibly registering an async completion callback at that address, tagged with the boolean). No stack argument was dropped here — both arguments genuinely match the decompiled count; only the call TARGET address was mis-displayed."
- Replace with: "The call operand 0x009ed510 is a one-instruction jump stub to 0x00754410, and 0x00754410 is a bare return (the stripped instrumentation stub). The boolean and the code address 0x00841af0 are pushed and discarded: no completion callback is registered on PC. 0x00841af0 itself, had it run, would fire the Lua named callback whose name is the string cached at 0x02317b14, tagged with the context cached at 0x02317b34, with one numeric argument — so the string argument is the name of a Lua completion callback, and that callback never fires. [CONFIRMED — disassembly.]"
- Strike: "Unconditionally afterward (not gated on anything checked this pass), resolves two localization keys ... and opens a dialog via 0x007c3d80 (a distinct address from §23.15's own dialog-open call, 0x007c3e60 — a sibling dialog-open variant, not the same function)."
- Replace with: "Unconditionally afterward resolves 'MENU_PC_SIGN_IN_ERROR' then 'MENU_TITLE_NOTICE' through 0x0084a1b0 (key hashed via 0x00d9e740, looked up via 0x00849950, '!!key!!'-style fallback) and opens a notice dialog via 0x007c3d80(title, body, 0, 0, 0) — the three zeros are stale dwords left over from the first 0x0084a1b0 call (§26.2 mechanism); the zero 5th parameter selects the default 'OK' button label (0x01301a80 → 0x01164094) and the zero 4th parameter leaves the dialog's +0x13c result callback empty."
- Strike the side-effects sentence from "registers an async completion callback; and — given the unconditional call ..." to "... not fully closed)."; replace with "does not register any callback (the registration call is an empty stub), and always surfaces the PC 'cannot sign in' notice with an OK button. [CONFIRMED — disassembly.]"
- Strike: "OPEN — 0x009ed510/0x007c3d80's own internal bodies"; replace with "OPEN — 0x007c3310/0x007c18a0 (dialog creation internals)".

## 27.21 `game_show_coop_gamercard` (0x00844640) — CORRECTED (family note)

Evidence: `func/func_0x00844640.txt` (0x0101b4a0 and 0x007c9440 at depth 1); `callees/func_0x0101b4a0.txt`, `func_0x0101b4f0.txt`, `func_0x0101b530.txt`, `func_0x0101b570.txt`, `func_0x0101ba60.txt`, `func_0x0101ba90.txt`, `func_0x0101bac0.txt`.

- Order and shape as written (stub first, then read/round, then 0x007c9440). CONFIRMED — disassembly.
- 0x0101b4a0 is a "return 1" stub. Every address of the "0x0101b4a0–0x0101bac0 family" in these dumps — 0x0101b4a0, 0x0101b4f0, 0x0101b530, 0x0101b570, 0x0101b5b0, 0x0101ba60, 0x0101ba90, 0x0101bac0 — is the same two-instruction stub. The "generic platform-SDK helper" reading is refuted; the desk review's "compiled-out platform calls" reading is what the code shows. CONFIRMED — disassembly.
- 0x007c9440(slot): if slot < the count at 0x02289b94, tail-calls the jump stub 0x0086fe40 (→ 0x00870810, not dumped) with the address of the record field at 0x02289c28 + slot·0x20c + 0x80 (the dump spells it 0x02289ca8 + slot·0x20c). HIGH CONFIDENCE that +0x80 is the player's platform id and 0x00870810 the overlay call; CONFIRMED — disassembly for the bounds check and pointer arithmetic.

Spec text changes
- Strike the whole "**A new family sighting worth flagging ...**" paragraph including its qualification.
- Replace with: "0x0101b4a0 is a two-instruction `return 1` stub, as are 0x0101b4f0 (§13.24/§27.22), 0x0101b530 (§8.21/§27.14), 0x0101b570 (§22.24), 0x0101b5b0, 0x0101ba60 (§10.9), 0x0101ba90 and 0x0101bac0 (§27.15): the whole 0x0101b4a0–0x0101bac0 range is compiled-out platform calls folded into one stub body each. [CONFIRMED — disassembly.]"
- Strike: "OPEN — 0x0101b4a0/0x007c9440's own internal bodies"; replace with "0x007c9440(slot) bounds-checks against 0x02289b94 and hands the +0x80 field of the record at 0x02289c28 + slot·0x20c to 0x0086fe40 → 0x00870810. [CONFIRMED — disassembly; OPEN — 0x00870810.]"

## 27.22 `game_main_menu_join_friend_in_progress` (0x00844670) — CONFIRMED

Evidence: `func/func_0x00844670.txt`. Join call 0x007c9410(slot) first (bounds check against 0x02289b94, tail call to 0x0088e040 on the record at 0x02289c28 + slot·0x20c), result kept in a register, then the stub 0x0101b4f0, then the boolean push. CONFIRMED — disassembly.

Spec text change
- Strike: "the zero-argument helper 0x0101b4f0 (already OPEN at §13.24, see item 27.21's family note)"; replace with "the `return 1` stub 0x0101b4f0 (closes §13.24's OPEN on it: inert)". Strike "OPEN — 0x007c9410's own internal body"; replace with "0x007c9410(slot) bounds-checks and tail-calls 0x0088e040 on the player record. [CONFIRMED — disassembly; OPEN — 0x0088e040.]"

## 27.23 `game_coop_start_new_live` (0x008425d0) — CONFIRMED

Evidence: `func/func_0x008425d0.txt` (0x00d2f540 and 0x007027b0 at depth 1).

- 0x00d2f540 is a "return 1" stub (this also settles §8.12's use of it: inert). The argument gate is "count > 0 and the Lua type at position 1 is not nil" (type code 0): an explicit nil also takes the default true. 0x007027b0(b) writes the byte 0x014ff6c1 and, when b is non-zero, clears the byte 0x014ff6c2. CONFIRMED — disassembly.

Spec text changes
- Strike: "unconditionally calls a zero-argument helper, 0x00d2f540 (this exact address also appears at §8.12 ... pattern already noted in items 27.14/27.21 above)"; replace with "unconditionally calls 0x00d2f540, a `return 1` stub (also inert at §8.12)".
- Strike: "if a real (non-absent) argument was actually given"; replace with "if at least one argument is present and the first is not nil".
- Strike: "OPEN — 0x00d2f540/0x007027b0's own internal bodies"; replace with "0x007027b0(b) stores b in the byte 0x014ff6c1 ('live session') and clears 0x014ff6c2 ('system-link session') when b is true. [CONFIRMED — disassembly.]"

## 27.24 `game_coop_start_new_syslink` (0x00842640) — CONFIRMED

Evidence: `func/func_0x00842640.txt` (0x00d34d40 and 0x007027d0 at depth 1). Instruction-for-instruction the same shape as §27.23; 0x00d34d40 is a "return 1" stub; 0x007027d0(b) stores b in 0x014ff6c2 and clears 0x014ff6c1 when b is true — the mirror of §27.23. CONFIRMED — disassembly.

Spec text changes
- Strike the "0xd34xxx = syslink" sentence and its qualification; replace with "unconditionally calls 0x00d34d40, a `return 1` stub".
- Strike: "OPEN — 0x00d34d40/0x007027d0's own internal bodies"; replace with "0x007027d0(b) stores b in the byte 0x014ff6c2 and clears 0x014ff6c1 when b is true: the two flags are mutually exclusive session-type markers. [CONFIRMED — disassembly.]"

## 27.25 `game_get_in_progress_type` (0x008447b0) — CONFIRMED

Evidence: `func/func_0x008447b0.txt` (0x006ced00 at depth 1). Twelve-instruction passthrough as written. CONFIRMED — disassembly.

0x006ced00 (new): if the mission pointer 0x014c8460 is non-null and the phase 0x014c7a14 ≠ 8, returns the mission's +0xa0 field; else if the minigame singleton 0x014c2d10 (via 0x006a3900, §27.1) is non-null, returns 4; else if the phase is 6 or 7 and the pointer 0x014c8328 is non-null, returns that object's +0xa0 field (and if that pointer is null, resets the phase to 0 as a side effect); otherwise returns -1. CONFIRMED — disassembly.

Spec text change
- Append: "0x006ced00: active mission (0x014c8460 non-null, phase 0x014c7a14 ≠ 8) → its +0xa0 type field; else minigame singleton 0x014c2d10 present → 4; else phase 6 or 7 with 0x014c8328 non-null → that object's +0xa0 (a null pointer there resets the phase to 0); else -1. So 'in progress type' is the active mission's or activity's type, 4 for the cat-and-mouse minigame, -1 for none. [CONFIRMED — disassembly.]"

## 27.26 Cross-function observations — CORRECTED

1. Observation 1 (thunk caution) is wrong in direction for all three new "instances" and for the §23.13 precedent: in every case the decompiler's thunk label named the real function and the "real target" the spec cites is a one-instruction jump stub (0x0086fdd0 → 0x008703b0, 0x0086fde0 → 0x008703c0, 0x0086fdf0 → 0x00db60a0, 0x009ed510 → 0x00754410). Strike the observation; replace with: "Four call operands in this document (0x0086fdd0/e0/f0, 0x009ed510; §23.13, §27.18–§27.20) are one-instruction jump stubs; the decompiler's thunk label names the function that actually runs. Cite the jump target, not the stub. Two of those targets are trivial (0x00db60a0 returns 1; 0x00754410 is a bare return)."
2. Observation 2 (0x0101bxxx = generic platform-SDK family): refuted; every member dumped is a `return 1` stub. Strike; replace with the §27.21 wording.
3. Observation 3: 0x00d2f540 is a stub, so its "reuse across feature areas" means nothing; 0x00458230/0x031d152c reuse stands. Reword accordingly.
4. Observation 4: 0x00e0cef0(name, luaState) is the "does this Lua global function exist" check, and 0x00e1a1b0 is the interface Lua state; the stale-slot reconfirmation of 0x00e0ca80's four parameters stands (its body reads all four). Reword.
5. Observation 5 stands (the rotation pair is confirmed by both bodies; the join-type pair 0x00844440/0x008444b0 has none).
6. Observation 6 stands.
7. Observation 7: the §2.10 question narrows to "what object is in ECX at 0x00a3c9d0"; 0x008788e0 is a thiscall everywhere. Reword.
8. Observation 8: add the initial values now known (0x01300b88 = 1, 0x01301124 = -1 in the file; the rest zero-filled) and the two session-type bytes 0x014ff6c1/0x014ff6c2 (§27.23/§27.24), the friendly-fire global 0x012f4500 (file value 1), and 0x012f3b48 (§27.8, file value 2).
9. Observation 9: the §27.5 hidden-ECX case is an output slot for the hash, and the "stale slot the decompiler reconstructs correctly" is only partly right — the decompiler kept four arguments but showed the wrong value for the second (the register's old contents rather than the hash). Strike the OPEN marker and reword.

---

## Summary

| Entry | Verdict | Load-bearing change |
|---|---|---|
| 27.1 | CONFIRMED | callee bodies added (0x006a3900 reads 0x014c2d10; 0x006c8050 sets +0xe0 and messages the session) |
| 27.2 | CORRECTED | one function 0x00a525a0 registered under both names; marker/hypothesis wrong; body right; §20.14 marker to clear |
| 27.3 | CONFIRMED | 0x00878ca0 / 0x00877ca0 bodies added |
| 27.4 | CONFIRMED | 0x008788e0 is a thiscall at every site (§2.10 receiver still OPEN) |
| 27.5 | CORRECTED | second argument of 0x00a9b080 is the name hash, written by 0x00d9e8b0 into the lowest stack dword; class byte never passed |
| 27.6 | CONFIRMED | writers listed |
| 27.7 | CORRECTED | wrapper is 0x008410f0, not 0x00841100; consume-once global |
| 27.8 | CORRECTED | wrapper pinned 0x007c63a0; 0x006e4600 sets 0x012f3b48 |
| 27.9 | CONFIRMED | 0x005f8670 body added |
| 27.10 | CONFIRMED | 0x005a0580 is a fade-family composite; 4th arg via stale slot |
| 27.11 | CORRECTED | +0x114 not +0x14; timer predicate roles; 0x00e1a1b0 = Lua state; +0x13c origin |
| 27.12 | CONFIRMED | autosave gates added |
| 27.13 | CORRECTED | 0x00d34dd0 is a stub |
| 27.14 | CORRECTED | 0x0101b530 is a stub; async-primitive reading withdrawn |
| 27.15 | CONFIRMED | no contradiction; helpers are stubs; setter gate |
| 27.16 | CONFIRMED | out-of-range → 0; initial external value 0 |
| 27.17 | CONFIRMED | — |
| 27.18 | CORRECTED | thunk reading inverted; always returns true |
| 27.19 | CORRECTED | thunk reading inverted; body = Steam user pointer non-null |
| 27.20 | CORRECTED | 0x009ed510 → 0x00754410 is empty: nothing registered; string = Lua callback name; Return: none |
| 27.21 | CORRECTED | whole 0x0101bxxx range is stubs |
| 27.22 | CONFIRMED | — |
| 27.23 | CONFIRMED | flag bytes identified |
| 27.24 | CONFIRMED | flag bytes identified |
| 27.25 | CONFIRMED | 0x006ced00 body added |
| 27.26 | CORRECTED | observations 1, 2, 4, 9 rewritten |

Counts: CONFIRMED 14, CORRECTED 12, OPEN 0 (residual sub-items below). Collateral corrections outside §27: §20.14 (clear its OPEN marker), §23.13 (jump-stub direction), §2.10 (hidden `this` on 0x008788e0 is now certain; the receiver is not), §8.12 (0x00d2f540 inert), §13.24 (0x0101b4f0 inert).

## Next dump (residual OPEN items)

Function entries, depth 1:
- 0x00a3c9d0 (§2.10) — to read what ECX holds at its 0x008788e0 call.
- 0x0088e690, 0x0088dfd0, 0x0088e040, 0x00870810 — the player-record operations behind §27.13/§27.14/§27.22/§27.21 (records at 0x02289c28, stride 0x20c, count 0x02289b94).
- 0x00a9b080 beyond its parameter checks (camera-preset selection math), 0x00a9afd0.
- 0x00853ea0 (object release, §27.9), 0x006f8370 and 0x00b94ff0 (§27.12).
- 0x007c3310, 0x007c18a0, 0x007c2ac0 (dialog creation behind 0x007c3d80, §27.20) and 0x007c4c90 (dialog ring initialiser, §27.11).
- 0x0086f1b0 and 0x008779b0/0x00877860 (session message senders, §27.1/§27.4).

Globals whose uses to list (uncapped, the 25-xref cap truncated these): 0x02282d10 (60 uses), 0x02282d14 (33), 0x014c2d10 (46), 0x024d78e0 (22), 0x012fcadc, 0x0290ceca, 0x0229a317, 0x014ff6c1, 0x014ff6c2, 0x012f3b48.
